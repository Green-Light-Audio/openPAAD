/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Stream reservation sizes and bandwidth (requirements BW-001 to BW-004).
 *
 * Expected values are the published figures of Milan v1.3 Table 4.4; the
 * openPAAD implementation must reproduce them exactly. The GenAVB tests are
 * characterisation tests: they pin today's upstream behaviour so that a
 * change is noticed and the requirement matrix updated.
 */

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "paad/bandwidth.h"
#include "genavb_shim.h"

namespace {

constexpr uint64_t kbps(uint64_t v) { return v * 1000; }

/* AAF stream format, 8 bytes in network order: PCM, 32-bit integer,
 * 32 valid bits, `channels` channels, `samples` samples per frame. */
std::array<uint8_t, 8> aaf_pcm32_format(uint8_t nsr, unsigned channels, unsigned samples)
{
	const uint32_t packing = (channels << 22) | (samples << 12);

	return { 0x02, nsr, 0x02, 32,
		 uint8_t(packing >> 24), uint8_t(packing >> 16), uint8_t(packing >> 8), uint8_t(packing) };
}

constexpr uint8_t kNsr48k = 0x05;

} // namespace

/* BW-001: Tspec MaxFrameSize for AAF PCM32 streams. */
TEST(PaadBandwidth, AafMaxFrameSize)
{
	for (unsigned n = 1; n <= 8; n++) {
		EXPECT_EQ(paad_aaf_max_frame_size(n, 4, 6), 24 * n + 24) << "48 kHz, N=" << n;
		EXPECT_EQ(paad_aaf_max_frame_size(n, 4, 12), 48 * n + 24) << "96 kHz, N=" << n;
		EXPECT_EQ(paad_aaf_max_frame_size(n, 4, 24), 96 * n + 24) << "192 kHz, N=" << n;
	}
}

/* BW-002: bandwidth of AAF PCM32 streams. */
TEST(PaadBandwidth, AafBandwidthMatchesPublishedTable)
{
	for (unsigned n = 1; n <= 64; n++) {
		EXPECT_EQ(paad_stream_bandwidth_bps(paad_aaf_max_frame_size(n, 4, 6), 1, 8000), kbps(1536 * n + 4288));
		EXPECT_EQ(paad_stream_bandwidth_bps(paad_aaf_max_frame_size(n, 4, 12), 1, 8000), kbps(3072 * n + 4288));
		EXPECT_EQ(paad_stream_bandwidth_bps(paad_aaf_max_frame_size(n, 4, 24), 1, 8000), kbps(6144 * n + 4288));
	}
}

/* BW-003: CRF stream, one timestamp per PDU. Exercises the minimum
 * Ethernet frame size rule, applied before the drift allowance. */
TEST(PaadBandwidth, CrfStream)
{
	EXPECT_EQ(paad_crf_max_frame_size(1), 28u);
	EXPECT_EQ(paad_stream_bandwidth_bps(paad_crf_max_frame_size(1), 1, 8000), kbps(5696));
}

/* Characterisation of GenAVB 7.6.2 (BW-001 gap): the Talker Tspec still
 * follows Milan v1.2, which included the one-byte drift allowance in
 * MaxFrameSize. Milan v1.3 declares the bare size. Fix tracked for Phase 5. */
TEST(GenavbCharacterisation, AafTspecIsMilan12Style)
{
	for (unsigned n : { 1u, 2u, 4u, 6u, 8u }) {
		unsigned int mif = 0;
		auto fmt = aaf_pcm32_format(kNsr48k, n, 6);
		unsigned int mfs = shim_genavb_tspec(fmt.data(), &mif);

		EXPECT_EQ(mif, 1u);
		EXPECT_EQ(mfs, paad_aaf_max_frame_size(n, 4, 6) + 1) << "N=" << n;
	}
}

/* Characterisation of GenAVB 7.6.2: the shaper idle slope adds the drift
 * byte again, so it reserves 64 kbit/s more than the published bandwidth.
 * Harmless (the shaper is slightly generous) but documented. */
TEST(GenavbCharacterisation, IdleSlopeOneByteAboveBandwidth)
{
	for (unsigned n : { 1u, 2u, 8u }) {
		unsigned int mif = 0;
		auto fmt = aaf_pcm32_format(kNsr48k, n, 6);
		unsigned int mfs = shim_genavb_tspec(fmt.data(), &mif);

		EXPECT_EQ(shim_genavb_idle_slope(mfs, mif), kbps(1536 * n + 4288) + 64000) << "N=" << n;
	}
}
