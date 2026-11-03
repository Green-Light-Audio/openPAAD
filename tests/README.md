# Tests

| Directory | Level | Runs | Status |
| --- | --- | --- | --- |
| `unit/` | Host unit tests (GoogleTest) against openPAAD and GenAVB code, on Linux and macOS | every commit | 48 tests, 11 of them inside FreeRTOS (POSIX port) |
| `e2e/` and `apps/openpaad-sim` | End to end: the full stack on simulated hardware. One process with two ports, gPTP must lock; two devices in two processes, gPTP must lock and ATDECC discover (`ctest -L e2e`) | every commit | 2 tests, about 45 s |
| `fuzz/` | libFuzzer targets for every PDU parser (ADP, AECP, ACMP, MRP, AVTP) | nightly | Phase 1 |
| `conformance/` | Scripted checks per requirement row, driven by la_avdecc (test-only dependency, LGPL, never linked into firmware) | every commit, on the simulator | Phase 1 |
| `hil/` | Hardware-in-the-loop on the lab runner: flash, gPTP lock, streaming, soak | per merge to main | Phase 2 |

Every test names the requirement IDs it covers (`docs/requirements/matrix.csv`).
