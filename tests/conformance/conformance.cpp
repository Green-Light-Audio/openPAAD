/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// ATDECC conformance checks, run from a controller built on la_avdecc (the
// library Hive uses) against a talker and a listener on a network interface.
//
// Each check names the requirement rows it covers in
// docs/requirements/matrix.csv. A check whose rows are all marked
// "implemented" there must pass; the others only report, so that a gap the
// matrix already records does not fail the run, and a check that starts to
// pass shows that the matrix can be updated.
//
// la_avdecc is a test-only dependency (LGPL): it is never linked into
// firmware.

#include <la/avdecc/avdecc.hpp>
#include <la/avdecc/controller/avdeccController.hpp>
#include <la/avdecc/executor.hpp>
#include <la/avdecc/logger.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;
namespace avdecc = la::avdecc;
namespace model = la::avdecc::entity::model;
using la::avdecc::UniqueIdentifier;
using la::avdecc::controller::ControlledEntity;
using la::avdecc::controller::Controller;
using AemStatus = la::avdecc::entity::ControllerEntity::AemCommandStatus;
using ControlStatus = la::avdecc::entity::ControllerEntity::ControlStatus;

constexpr auto CommandTimeout = 5s;

/* --- Requirement matrix ------------------------------------------------------ */

// id -> GenAVB status column ("implemented", "partial", "gap", ...).
std::map<std::string, std::string> readMatrix(std::string const& path)
{
	auto rows = std::map<std::string, std::string>{};
	auto file = std::ifstream{ path };
	auto line = std::string{};
	auto header = std::vector<std::string>{};
	auto statusColumn = std::optional<std::size_t>{};

	while (std::getline(file, line))
	{
		auto fields = std::vector<std::string>{};
		auto field = std::string{};
		auto quoted = false;
		for (auto const c : line)
		{
			if (c == '"')
				quoted = !quoted;
			else if (c == ',' && !quoted)
			{
				fields.push_back(field);
				field.clear();
			}
			else
				field.push_back(c);
		}
		fields.push_back(field);

		if (header.empty())
		{
			header = fields;
			for (auto i = std::size_t{ 0 }; i < header.size(); ++i)
				if (header[i].rfind("genavb", 0) == 0)
					statusColumn = i;
			continue;
		}
		if (statusColumn && fields.size() > *statusColumn)
			rows[fields[0]] = fields[*statusColumn];
	}

	return rows;
}

/* --- Results ------------------------------------------------------------------ */

struct Result
{
	std::string name;
	std::vector<std::string> requirements;
	bool passed{ false };
	std::string detail;
	bool mandatory{ false };
	std::string matrixStatus; // the rows that are not "implemented", if any
};

class Report
{
public:
	explicit Report(std::map<std::string, std::string> matrix)
		: _matrix{ std::move(matrix) }
	{
	}

	// partial: the check covers only part of its requirements, so passing
	// it says nothing about the rest of what the matrix row asks.
	void add(std::string name, std::vector<std::string> requirements, bool const passed, std::string detail = {}, bool const partial = false)
	{
		auto r = Result{};
		r.name = std::move(name);
		r.requirements = std::move(requirements);
		r.passed = passed;
		r.detail = std::move(detail);
		r.mandatory = !r.requirements.empty();
		for (auto const& id : r.requirements)
		{
			auto const it = _matrix.find(id);
			auto const status = it == _matrix.end() ? std::string{ "not in the matrix" } : it->second;
			if (status != "implemented")
			{
				r.mandatory = false;
				r.matrixStatus += (r.matrixStatus.empty() ? "" : ", ") + id + " " + status;
			}
		}
		std::printf("%-6s %-24s %s%s%s\n", r.passed ? "PASS" : (r.mandatory ? "FAIL" : "info"), join(r.requirements).c_str(), r.name.c_str(), r.detail.empty() ? "" : ": ", r.detail.c_str());
		if (!r.matrixStatus.empty())
			std::printf("       %-24s (matrix: %s%s)\n", "", r.matrixStatus.c_str(), r.passed && !partial ? "; passes now, the matrix may need updating" : "");
		std::fflush(stdout);
		_results.push_back(std::move(r));
	}

	int failures() const
	{
		return static_cast<int>(std::count_if(_results.begin(), _results.end(),
			[](auto const& r)
			{
				return r.mandatory && !r.passed;
			}));
	}

	std::size_t size() const
	{
		return _results.size();
	}

	std::size_t passed() const
	{
		return static_cast<std::size_t>(std::count_if(_results.begin(), _results.end(),
			[](auto const& r)
			{
				return r.passed;
			}));
	}

private:
	static std::string join(std::vector<std::string> const& ids)
	{
		auto s = std::string{};
		for (auto const& id : ids)
			s += (s.empty() ? "" : ",") + id;
		return s.empty() ? "-" : s;
	}

	std::map<std::string, std::string> _matrix;
	std::vector<Result> _results;
};

/* --- What the controllers see -------------------------------------------------- */

class Observer final : public Controller::DefaultedObserver
{
public:
	struct EntityState
	{
		bool online{ false };
		std::vector<std::string> queryErrors;
		std::optional<model::ClockSourceIndex> notifiedClockSource;
	};

	EntityState state(UniqueIdentifier const id)
	{
		auto const lock = std::lock_guard{ _lock };
		return _entities[id];
	}

	// Waits until pred(state) holds or the timeout expires.
	bool waitFor(UniqueIdentifier const id, std::chrono::milliseconds const timeout, std::function<bool(EntityState const&)> const& pred)
	{
		auto lock = std::unique_lock{ _lock };
		return _changed.wait_for(lock, timeout,
			[&]
			{
				return pred(_entities[id]);
			});
	}

private:
	void onEntityQueryError(Controller const* const, ControlledEntity const* const entity, Controller::QueryCommandError const error) noexcept override
	{
		update(entity->getEntity().getEntityID(),
			[error](EntityState& s)
			{
				s.queryErrors.push_back(std::to_string(static_cast<int>(error)));
			});
	}

	void onEntityOnline(Controller const* const, ControlledEntity const* const entity) noexcept override
	{
		update(entity->getEntity().getEntityID(),
			[](EntityState& s)
			{
				s.online = true;
			});
	}

	void onEntityOffline(Controller const* const, ControlledEntity const* const entity) noexcept override
	{
		update(entity->getEntity().getEntityID(),
			[](EntityState& s)
			{
				s.online = false;
			});
	}

	void onClockSourceChanged(Controller const* const, ControlledEntity const* const entity, model::ClockDomainIndex const, model::ClockSourceIndex const clockSourceIndex) noexcept override
	{
		update(entity->getEntity().getEntityID(),
			[clockSourceIndex](EntityState& s)
			{
				s.notifiedClockSource = clockSourceIndex;
			});
	}

	void update(UniqueIdentifier const id, std::function<void(EntityState&)> const& fn)
	{
		{
			auto const lock = std::lock_guard{ _lock };
			fn(_entities[id]);
		}
		_changed.notify_all();
	}

	std::mutex _lock;
	std::condition_variable _changed;
	std::map<UniqueIdentifier, EntityState> _entities;
};

class LogPrinter final : public avdecc::logger::Logger::Observer
{
	void onLogItem(avdecc::logger::Level const level, avdecc::logger::LogItem const* const item) noexcept override
	{
		std::fprintf(stderr, "la_avdecc %s: %s\n", avdecc::logger::Logger::getInstance().levelToString(level).c_str(), item->getMessage().c_str());
	}
};

/* --- Helpers -------------------------------------------------------------------- */

template<typename T>
std::optional<T> waitResult(std::future<T>& f)
{
	if (f.wait_for(CommandTimeout) != std::future_status::ready)
		return std::nullopt;
	return f.get();
}

std::string statusText(std::optional<AemStatus> const s)
{
	return s ? avdecc::entity::LocalEntity::statusToString(*s) : "no response";
}

std::string statusText(std::optional<ControlStatus> const s)
{
	return s ? avdecc::entity::LocalEntity::statusToString(*s) : "no response";
}

std::string hex(UniqueIdentifier const id)
{
	char s[24];
	std::snprintf(s, sizeof(s), "%016llx", static_cast<unsigned long long>(id.getValue()));
	return s;
}

// Reads something from an entity's model with the controller's lock held only
// for that time (never across a command: its answer needs the same lock).
template<typename T>
std::optional<T> read(Controller const& c, UniqueIdentifier const id, std::function<T(ControlledEntity const&)> const& fn)
{
	auto const guard = c.getControlledEntityGuard(id);
	if (!guard)
		return std::nullopt;
	try
	{
		return fn(*guard);
	}
	catch (avdecc::Exception const& e)
	{
		std::fprintf(stderr, "reading %s: %s\n", hex(id).c_str(), e.what());
		return std::nullopt;
	}
	catch (std::exception const& e)
	{
		std::fprintf(stderr, "reading %s: %s\n", hex(id).c_str(), e.what());
		return std::nullopt;
	}
}

/* --- The checks ------------------------------------------------------------------ */

struct Options
{
	std::string interfaceName;
	UniqueIdentifier talker;
	UniqueIdentifier listener;
	std::string matrix;
	std::chrono::seconds discoveryTimeout{ 30 };
	bool verbose{ false };
};

void checkAdvertisement(Report& report, Controller const& c, UniqueIdentifier const id, bool const isTalker)
{
	using la::avdecc::entity::EntityCapability;
	using la::avdecc::entity::ListenerCapability;
	using la::avdecc::entity::TalkerCapability;

	auto const info = read<avdecc::entity::Entity>(c, id,
		[](ControlledEntity const& e)
		{
			return e.getEntity();
		});
	if (!info)
	{
		report.add(hex(id) + " advertises itself as expected", { "ADP-001" }, false, "not discovered");
		return;
	}

	auto const& common = info->getCommonInformation();
	auto missing = std::string{};
	auto need = [&missing](bool const ok, char const* what)
	{
		if (!ok)
			missing += (missing.empty() ? "" : ", ") + std::string{ what };
	};
	need(common.entityCapabilities.test(EntityCapability::AemSupported), "AEM supported");
	need(common.entityCapabilities.test(EntityCapability::ClassASupported), "class A");
	need(common.entityCapabilities.test(EntityCapability::GptpSupported), "gPTP");
	need(common.entityCapabilities.test(EntityCapability::AemInterfaceIndexValid), "interface index");
	need(common.entityCapabilities.test(EntityCapability::AemIdentifyControlIndexValid), "identify control index");
	if (isTalker)
	{
		need(common.talkerStreamSources > 0, "talker stream sources");
		need(common.talkerCapabilities.test(TalkerCapability::Implemented), "talker implemented");
		need(common.talkerCapabilities.test(TalkerCapability::AudioSource), "audio source");
	}
	else
	{
		need(common.listenerStreamSinks > 0, "listener stream sinks");
		need(common.listenerCapabilities.test(ListenerCapability::Implemented), "listener implemented");
		need(common.listenerCapabilities.test(ListenerCapability::AudioSink), "audio sink");
	}

	auto validTimes = std::set<unsigned>{};
	auto gptp = true;
	for (auto const& [index, intf] : info->getInterfacesInformation())
	{
		validTimes.insert(intf.validTime);
		gptp = gptp && intf.gptpGrandmasterID.has_value();
	}
	need(gptp, "gPTP grandmaster");

	report.add(hex(id) + " advertises the expected capabilities", { "ADP-001" }, missing.empty(), missing.empty() ? std::string{} : "missing " + missing, true);

	auto vt = std::string{};
	for (auto const v : validTimes)
		vt += (vt.empty() ? "" : ",") + std::to_string(v);
	report.add(hex(id) + " advertisement valid time is 10", { "ADP-001" }, validTimes == std::set<unsigned>{ 10u }, "valid time " + vt, true);
}

void checkModel(Report& report, Controller const& c, UniqueIdentifier const id, Observer& observer)
{
	auto const state = observer.state(id);
	auto const fatal = read<bool>(c, id,
		[](ControlledEntity const& e)
		{
			return e.gotFatalEnumerationError();
		});
	auto errors = std::string{};
	for (auto const& e : state.queryErrors)
		errors += (errors.empty() ? "query errors " : ",") + e;
	report.add(hex(id) + " entity model read completely", { "CTL-004", "CTL-005", "AEM-002" }, state.online && fatal == false && state.queryErrors.empty(), errors);

	struct Facts
	{
		bool has48k{ false };
		std::size_t streamInputs{ 0 };
		std::size_t streamOutputs{ 0 };
		bool streamFormats{ true };
		std::size_t avbInterfaces{ 0 };
		bool internalSource{ false };
		bool streamSource{ false };
		std::size_t clockDomains{ 0 };
		bool domainListsSources{ true };
		bool unsolicited{ false };
		std::optional<model::MilanInfo> milan;
		std::string compatibility;
		std::vector<std::string> compatibilityEvents;
		bool avbCounters{ false };
		bool inputCounters{ false };
		bool outputCounters{ false };
		bool domainCounters{ false };
	};

	auto const facts = read<Facts>(c, id,
		[](ControlledEntity const& e)
		{
			auto f = Facts{};
			auto const& cfg = e.getCurrentConfigurationNode();
			for (auto const& [i, au] : cfg.audioUnits)
				for (auto const& rate : au.staticModel.samplingRates)
					f.has48k = f.has48k || rate.getNominalSampleRate() == 48000.0;
			f.streamInputs = cfg.streamInputs.size();
			f.streamOutputs = cfg.streamOutputs.size();
			for (auto const& [i, s] : cfg.streamInputs)
			{
				f.streamFormats = f.streamFormats && !s.staticModel.formats.empty();
				f.inputCounters = f.inputCounters || s.dynamicModel.counters.has_value();
			}
			for (auto const& [i, s] : cfg.streamOutputs)
			{
				f.streamFormats = f.streamFormats && !s.staticModel.formats.empty();
				f.outputCounters = f.outputCounters || s.dynamicModel.counters.has_value();
			}
			f.avbInterfaces = cfg.avbInterfaces.size();
			for (auto const& [i, a] : cfg.avbInterfaces)
				f.avbCounters = f.avbCounters || a.dynamicModel.counters.has_value();
			for (auto const& [i, cs] : cfg.clockSources)
			{
				f.internalSource = f.internalSource || cs.staticModel.clockSourceType == model::ClockSourceType::Internal;
				f.streamSource = f.streamSource || cs.staticModel.clockSourceType == model::ClockSourceType::InputStream;
			}
			f.clockDomains = cfg.clockDomains.size();
			for (auto const& [i, cd] : cfg.clockDomains)
			{
				f.domainListsSources = f.domainListsSources && !cd.staticModel.clockSources.empty();
				f.domainCounters = f.domainCounters || cd.dynamicModel.counters.has_value();
			}
			f.unsolicited = e.isSubscribedToUnsolicitedNotifications();
			f.milan = e.getMilanInfo();

			auto const flags = e.getCompatibilityFlags();
			using Flag = ControlledEntity::CompatibilityFlag;
			auto add = [&f](char const* s)
			{
				f.compatibility += (f.compatibility.empty() ? "" : ", ") + std::string{ s };
			};
			if (flags.test(Flag::IEEE17221))
				add("IEEE 1722.1");
			if (flags.test(Flag::IEEE17221Warning))
				add("IEEE 1722.1 with warnings");
			if (flags.test(Flag::Milan))
				add(("Milan " + e.getMilanCompatibilityVersion().to_string()).c_str());
			if (flags.test(Flag::MilanWarning))
				add("Milan with warnings");
			if (flags.test(Flag::Misbehaving))
				add("misbehaving");
			if (f.compatibility.empty())
				f.compatibility = "none";
			for (auto const& ev : e.getCompatibilityChangedEvents())
				f.compatibilityEvents.push_back("[" + ev.specClause + "] " + ev.message);
			return f;
		});
	if (!facts)
		return;

	auto const name = hex(id) + " ";
	report.add(name + "audio unit offers 48 kHz", { "AEM-003" }, facts->has48k);
	report.add(name + "describes its stream inputs and outputs with formats", { "AEM-004" }, facts->streamInputs > 0 && facts->streamOutputs > 0 && facts->streamFormats,
		std::to_string(facts->streamInputs) + " in, " + std::to_string(facts->streamOutputs) + " out");
	report.add(name + "describes an AVB interface", { "AEM-005" }, facts->avbInterfaces > 0);
	report.add(name + "describes internal and stream clock sources", { "AEM-006" }, facts->internalSource && facts->streamSource);
	report.add(name + "clock domain lists its sources", { "AEM-008" }, facts->clockDomains > 0 && facts->domainListsSources);
	report.add(name + "accepts registration for unsolicited notifications", { "CTL-013" }, facts->unsolicited);
	auto const milan13 = facts->milan && (facts->milan->specificationVersion.getMajorVersion() > 1 || (facts->milan->specificationVersion.getMajorVersion() == 1 && facts->milan->specificationVersion.getMinorVersion() >= 3));
	report.add(name + "answers GET_MILAN_INFO as a Milan 1.3 entity", { "CTL-030" }, milan13,
		facts->milan ? "protocol " + std::to_string(facts->milan->protocolVersion) + ", specification " + facts->milan->specificationVersion.to_string() : std::string{ "no answer" }, true);
	report.add(name + "reports AVB interface counters", { "AEM-012" }, facts->avbCounters, {}, true);
	report.add(name + "reports stream output counters", { "AEM-013" }, facts->outputCounters || facts->streamOutputs == 0, {}, true);
	report.add(name + "reports stream input counters", { "AEM-015" }, facts->inputCounters || facts->streamInputs == 0, {}, true);
	report.add(name + "reports clock domain counters", { "AEM-016" }, facts->domainCounters, {}, true);
	report.add(name + "la_avdecc's verdict", {}, true, facts->compatibility);
	for (auto const& ev : facts->compatibilityEvents)
		std::printf("       %-24s %s\n", "", ev.c_str());
}

std::optional<model::ClockSourceIndex> streamClockSource(Controller const& c, UniqueIdentifier const id)
{
	return read<model::ClockSourceIndex>(c, id,
		[](ControlledEntity const& e)
		{
			for (auto const& [i, cs] : e.getCurrentConfigurationNode().clockSources)
				if (cs.staticModel.clockSourceType == model::ClockSourceType::InputStream && cs.staticModel.clockSourceLocationIndex == 0)
					return i;
			throw std::runtime_error("no clock source follows stream input 0");
		});
}

std::optional<model::ClockSourceIndex> currentClockSource(Controller const& c, UniqueIdentifier const id)
{
	return read<model::ClockSourceIndex>(c, id,
		[](ControlledEntity const& e)
		{
			return e.getCurrentConfigurationNode().clockDomains.at(0).dynamicModel.clockSourceIndex;
		});
}

std::optional<model::StreamInputConnectionInfo> inputConnection(Controller const& c, UniqueIdentifier const id)
{
	return read<model::StreamInputConnectionInfo>(c, id,
		[](ControlledEntity const& e)
		{
			return e.getCurrentConfigurationNode().streamInputs.at(0).dynamicModel.connectionInfo;
		});
}

std::optional<model::AudioMappings> inputMappings(Controller const& c, UniqueIdentifier const id)
{
	return read<model::AudioMappings>(c, id,
		[](ControlledEntity const& e)
		{
			return e.getStreamPortInputNode(e.getCurrentConfigurationIndex(), 0).dynamicModel.dynamicAudioMap;
		});
}

std::optional<AemStatus> changeMappings(Controller const& c, UniqueIdentifier const id, model::AudioMappings const& mappings, bool const add)
{
	auto p = std::promise<AemStatus>{};
	auto f = p.get_future();
	auto const done = [&p](ControlledEntity const*, AemStatus const s)
	{
		p.set_value(s);
	};
	if (add)
		c.addStreamPortInputAudioMappings(id, 0, mappings, done);
	else
		c.removeStreamPortInputAudioMappings(id, 0, mappings, done);
	return waitResult(f);
}

bool waitConnection(Controller const& c, UniqueIdentifier const id, model::StreamInputConnectionInfo::State const state)
{
	auto const end = std::chrono::steady_clock::now() + 10s;
	while (std::chrono::steady_clock::now() < end)
	{
		auto const info = inputConnection(c, id);
		if (info && info->state == state)
			return true;
		std::this_thread::sleep_for(100ms);
	}
	return false;
}

void checkCommands(Report& report, Controller const& c, Options const& o, Observer& observer, Observer& other)
{
	auto const listener = o.listener;
	auto const talker = o.talker;

	// Lock, then unlock.
	{
		auto p = std::promise<AemStatus>{};
		auto f = p.get_future();
		c.lockEntity(listener,
			[&p](ControlledEntity const*, AemStatus const s, UniqueIdentifier)
			{
				p.set_value(s);
			});
		auto const locked = waitResult(f);
		auto const isLocked = read<bool>(c, listener,
			[](ControlledEntity const& e)
			{
				return e.isLocked();
			});
		auto u = std::promise<AemStatus>{};
		auto g = u.get_future();
		c.unlockEntity(listener,
			[&u](ControlledEntity const*, AemStatus const s, UniqueIdentifier)
			{
				u.set_value(s);
			});
		auto const unlocked = waitResult(g);
		report.add("LOCK_ENTITY then unlock", { "CTL-002", "AEM-010" }, locked == AemStatus::Success && isLocked == true && unlocked == AemStatus::Success,
			"lock " + statusText(locked) + ", unlock " + statusText(unlocked));
	}

	// Entity name.
	{
		auto name = avdecc::entity::model::AvdeccFixedString{ "openpaad conformance" };
		auto p = std::promise<AemStatus>{};
		auto f = p.get_future();
		c.setEntityName(listener, name,
			[&p](ControlledEntity const*, AemStatus const s)
			{
				p.set_value(s);
			});
		auto const status = waitResult(f);
		auto const now = read<std::string>(c, listener,
			[](ControlledEntity const& e)
			{
				return e.getEntityNode().dynamicModel.entityName.str();
			});
		report.add("SET_NAME on the entity", { "CTL-008" }, status == AemStatus::Success && now == name.str(), statusText(status), true);
	}

	// Identify.
	{
		auto p = std::promise<AemStatus>{};
		auto f = p.get_future();
		c.identifyEntity(listener, std::chrono::milliseconds{ 1000 },
			[&p](ControlledEntity const*, AemStatus const s)
			{
				p.set_value(s);
			});
		auto const status = waitResult(f);
		report.add("identify through the IDENTIFY control", { "CTL-011" }, status == AemStatus::Success, statusText(status));
	}

	// Clock source of the listener: its stream input.
	{
		auto const source = streamClockSource(c, listener);
		auto status = std::optional<AemStatus>{};
		if (source)
		{
			auto p = std::promise<AemStatus>{};
			auto f = p.get_future();
			c.setClockSource(listener, 0, *source,
				[&p](ControlledEntity const*, AemStatus const s)
				{
					p.set_value(s);
				});
			status = waitResult(f);
		}
		auto const now = currentClockSource(c, listener);
		report.add("SET_CLOCK_SOURCE to the stream input", { "CTL-010" }, source && status == AemStatus::Success && now == source, statusText(status));

		// A second controller, registered for unsolicited notifications,
		// must hear about it.
		auto const heard = source && other.waitFor(listener, 3s,
														[&source](Observer::EntityState const& s)
														{
															return s.notifiedClockSource == source;
														});
		report.add("another controller is told of the clock source change", { "NTF-001" }, heard, {}, true);
	}

	// Dynamic audio mappings of the listener's stream port input 0: read,
	// remove one, put it back, and try one that names no cluster.
	{
		auto const before = inputMappings(c, listener);
		report.add("listener's stream port input has dynamic mappings", { "AEM-007" }, before && !before->empty(),
			before ? std::to_string(before->size()) + " mappings" : std::string{});
		if (before && !before->empty())
		{
			auto const one = model::AudioMappings{ before->front() };
			auto const removed = changeMappings(c, listener, one, false);
			auto const afterRemove = inputMappings(c, listener);
			auto const added = changeMappings(c, listener, one, true);
			auto const afterAdd = inputMappings(c, listener);
			auto bad = before->front();
			bad.clusterOffset = 999;
			auto const rejected = changeMappings(c, listener, model::AudioMappings{ bad }, true);
			report.add("REMOVE then ADD_AUDIO_MAPPINGS", { "CTL-017" },
				removed == AemStatus::Success && afterRemove && afterRemove->size() + 1 == before->size() && added == AemStatus::Success && afterAdd && afterAdd->size() == before->size(),
				"remove " + statusText(removed) + ", add " + statusText(added));
			report.add("a mapping to a cluster that does not exist is refused", { "CTL-017" }, rejected && rejected != AemStatus::Success, statusText(rejected));
		}
	}

	// Bind the listener's input 0 to the talker's output 0, then unbind it.
	auto const talkerStream = model::StreamIdentification{ talker, 0 };
	auto const listenerStream = model::StreamIdentification{ listener, 0 };
	{
		auto p = std::promise<ControlStatus>{};
		auto f = p.get_future();
		c.connectStream(talkerStream, listenerStream,
			[&p](ControlledEntity const*, ControlledEntity const*, model::StreamIndex, model::StreamIndex, ControlStatus const s)
			{
				p.set_value(s);
			});
		auto const status = waitResult(f);
		report.add("bind the listener to the talker", { "ACMP-001" }, status == ControlStatus::Success, statusText(status));

		auto const connected = waitConnection(c, listener, model::StreamInputConnectionInfo::State::Connected);
		auto const info = inputConnection(c, listener);
		report.add("the listener reports the connection", { "ACMP-001", "ACMP-005" }, connected && info && info->talkerStream == talkerStream);

		auto q = std::promise<std::pair<ControlStatus, std::uint16_t>>{};
		auto g = q.get_future();
		c.getListenerStreamState(listenerStream,
			[&q](ControlledEntity const*, ControlledEntity const*, model::StreamIndex, model::StreamIndex, std::uint16_t const count, avdecc::entity::ConnectionFlags, ControlStatus const s)
			{
				q.set_value({ s, count });
			});
		auto const state = waitResult(g);
		report.add("GET_RX_STATE while bound", { "ACMP-004" }, state && state->first == ControlStatus::Success,
			state ? statusText(state->first) + ", connection count " + std::to_string(state->second) : std::string{ "no response" }, true);

		auto running = read<bool>(c, listener,
			[](ControlledEntity const& e)
			{
				return e.isStreamInputRunning(e.getCurrentConfigurationIndex(), 0);
			});
		report.add("the bound input is running", { "CTL-012" }, running == true);
	}
	{
		auto p = std::promise<ControlStatus>{};
		auto f = p.get_future();
		c.disconnectStream(talkerStream, listenerStream,
			[&p](ControlledEntity const*, model::StreamIndex, ControlStatus const s)
			{
				p.set_value(s);
			});
		auto const status = waitResult(f);
		auto const disconnected = waitConnection(c, listener, model::StreamInputConnectionInfo::State::NotConnected);
		report.add("unbind the listener", { "ACMP-001", "ACMP-006" }, status == ControlStatus::Success && disconnected, statusText(status));
	}
	(void)observer;
}

bool parseEid(char const* s, UniqueIdentifier& id)
{
	auto* end = static_cast<char*>(nullptr);
	auto const v = std::strtoull(s, &end, 16);
	if (!end || *end)
		return false;
	id = UniqueIdentifier{ v };
	return true;
}

int usage()
{
	std::fprintf(stderr,
		"usage: openpaad-conformance --interface IFNAME --talker EID --listener EID --matrix matrix.csv [--discovery-timeout S] [-v]\n"
		"Runs ATDECC checks, through la_avdecc, against a talker and a listener reachable on IFNAME.\n");
	return 2;
}

} // namespace

int main(int argc, char* argv[])
{
	auto o = Options{};
	for (auto i = 1; i < argc; ++i)
	{
		auto const arg = std::string{ argv[i] };
		auto const next = i + 1 < argc ? argv[i + 1] : nullptr;
		if (arg == "--interface" && next)
			o.interfaceName = argv[++i];
		else if (arg == "--talker" && next && parseEid(next, o.talker))
			++i;
		else if (arg == "--listener" && next && parseEid(next, o.listener))
			++i;
		else if (arg == "--matrix" && next)
			o.matrix = argv[++i];
		else if (arg == "--discovery-timeout" && next)
			o.discoveryTimeout = std::chrono::seconds{ std::atoi(argv[++i]) };
		else if (arg == "-v")
			o.verbose = true;
		else
			return usage();
	}
	if (o.interfaceName.empty() || !o.talker || !o.listener || o.matrix.empty())
		return usage();

	auto const matrix = readMatrix(o.matrix);
	if (matrix.empty())
	{
		std::fprintf(stderr, "cannot read the requirement matrix %s\n", o.matrix.c_str());
		return 2;
	}

	auto logs = LogPrinter{};
	avdecc::logger::Logger::getInstance().setLevel(o.verbose ? avdecc::logger::Level::Trace : avdecc::logger::Level::Warn);
	avdecc::logger::Logger::getInstance().registerObserver(&logs);

	// Each controller dispatches its messages on its own executor.
	auto& executors = avdecc::ExecutorManager::getInstance();
	auto const executor1 = executors.registerExecutor("openpaad-conformance-1", avdecc::ExecutorWithDispatchQueue::create("openpaad-conformance-1", avdecc::utils::ThreadPriority::Highest));
	auto const executor2 = executors.registerExecutor("openpaad-conformance-2", avdecc::ExecutorWithDispatchQueue::create("openpaad-conformance-2", avdecc::utils::ThreadPriority::Highest));

	auto observer = Observer{};
	auto other = Observer{};
	auto controller = Controller::UniquePointer{ nullptr, nullptr };
	auto second = Controller::UniquePointer{ nullptr, nullptr };
	try
	{
		controller = Controller::create(avdecc::protocol::ProtocolInterface::Type::PCap, o.interfaceName, 0x0c01, UniqueIdentifier::getNullUniqueIdentifier(), "en", nullptr, std::string{ "openpaad-conformance-1" }, nullptr);
		second = Controller::create(avdecc::protocol::ProtocolInterface::Type::PCap, o.interfaceName, 0x0c02, UniqueIdentifier::getNullUniqueIdentifier(), "en", nullptr, std::string{ "openpaad-conformance-2" }, nullptr);
	}
	catch (avdecc::Exception const& e)
	{
		std::fprintf(stderr, "cannot start a controller on %s: %s\n", o.interfaceName.c_str(), e.what());
		return 3;
	}
	// Both controllers enumerate as la_avdecc does by default, as Hive
	// does. (Its fast enumeration, through GET_DYNAMIC_INFO, fails on
	// GenAVB's answers to sub-commands it does not implement: see CTL-018
	// in the matrix.)
	controller->registerObserver(&observer);
	second->registerObserver(&other);

	std::printf("openPAAD ATDECC conformance, la_avdecc %s, on %s: talker %s, listener %s\n", avdecc::getVersion().c_str(), o.interfaceName.c_str(), hex(o.talker).c_str(), hex(o.listener).c_str());

	auto report = Report{ matrix };
	auto online = [](Observer::EntityState const& s)
	{
		return s.online;
	};
	auto const talkerOnline = observer.waitFor(o.talker, o.discoveryTimeout, online);
	auto const listenerOnline = observer.waitFor(o.listener, o.discoveryTimeout, online);
	other.waitFor(o.listener, 10s, online);
	report.add("talker discovered and enumerated", { "ADP-002" }, talkerOnline);
	report.add("listener discovered and enumerated", { "ADP-002" }, listenerOnline);

	if (talkerOnline && listenerOnline)
	{
		checkAdvertisement(report, *controller, o.talker, true);
		checkAdvertisement(report, *controller, o.listener, false);
		checkModel(report, *controller, o.talker, observer);
		checkModel(report, *controller, o.listener, observer);
		checkCommands(report, *controller, o, observer, other);
	}

	second.reset();
	controller.reset();
	avdecc::logger::Logger::getInstance().unregisterObserver(&logs);

	auto const failures = report.failures();
	std::printf("%zu checks, %zu passed; %d required check%s failed\n", report.size(), report.passed(), failures, failures == 1 ? "" : "s");

	return failures ? 1 : 0;
}
