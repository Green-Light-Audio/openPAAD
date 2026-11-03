# Tests

| Directory | Level | Runs | Status |
| --- | --- | --- | --- |
| `unit/` | Host unit tests (GoogleTest) against openPAAD and GenAVB code, on Linux and macOS | every commit | 61 tests, 11 of them inside FreeRTOS (POSIX port) |
| `e2e/` and `apps/openpaad-sim` | End to end: the full stack on simulated hardware. One process with two ports, gPTP must lock (virtual time); two devices in two processes, gPTP must lock and ATDECC discover (real time); a controller connects two devices through ATDECC, disconnects and reconnects them: every command must succeed, every frame arrive while connected, no stream remain when disconnected, and both audio clocks lock, to gPTP and to the stream (virtual time); the same over a real veth pair, through the kernel (real time; Linux with root or passwordless sudo, skipped elsewhere) (`ctest -L e2e`) | every commit | 4 tests, about 2 min 30 s |
| `fuzz/` | libFuzzer targets for every PDU parser (ADP, AECP, ACMP, MRP, AVTP) | nightly | Phase 1 |
| `conformance/` | Checks per requirement row from a controller built on la_avdecc (test-only dependency, LGPL, never linked into firmware), against two simulated devices on a veth pair (`ctest --preset conformance`; Linux with root or passwordless sudo) | every commit | 45 checks; failures only count on rows marked implemented |
| `hil/` | Hardware-in-the-loop on the lab runner: flash, gPTP lock, streaming, soak | per merge to main | Phase 2 |

Every test names the requirement IDs it covers (`docs/requirements/matrix.csv`).
