# 0008. The simulator runs on Linux and macOS

- Status: accepted
- Date: 2026-11-02
- Extends: 0005

## Context

Decision 0005 was written with a Linux host in mind (raw sockets on virtual
Ethernet pairs). The people who will use the simulator first work on Macs,
and Hive, the controller used to test against, runs on macOS. The FreeRTOS
POSIX port supports macOS, and the host build of all GenAVB protocol code
passes with Clang once two harmless warning classes are silenced for
GenAVB's files.

## Decision

The simulator and the host tests are supported on both Linux and macOS, and
CI runs the host build and tests on both (`host-macos` job, Apple silicon
runner). The simulator gets three network back-ends behind one interface:

| Back-end | Hosts | Use |
| --- | --- | --- |
| In-process virtual wire | Linux, macOS | Several simulated endpoints in one process; the default for CI and tests |
| Raw sockets on veth pairs | Linux | Packet capture with standard tools, multi-process tests |
| BPF on `feth` pairs or a real interface | macOS | Hive and la_avdecc on the same Mac see the simulated endpoints |

## Consequences

A Mac is enough to develop, run the tests and demonstrate simulated
endpoints in Hive. macOS needs administrator rights to open BPF devices for
the real-network back-end; the in-process back-end needs none.
