# Roadmap

Each phase ends on a gate that is a test result, not a date.

| Phase | Weeks | Exit gate |
| --- | --- | --- |
| 0. Foundations | 2 | CI green and reproducible; requirement matrix published; simulator spike verdict; timing targets written down |
| 1. HAL and simulation | 5 | HAL specified as headers; two simulated endpoints, built on that HAL, stream audio and are discovered and connected from Hive and la_avdecc, on every commit |
| 2. Board bring-up | 6 | gPTP within target for 24 hours between two EVKBs; asCapable and Pdelay rules pass |
| 3. Audio and media clock | 8 | Two boards locked through CRF with no sample slip over 72 hours |
| 4. AVB device | 4 | Audio both ways with a commercial Milan device, connected from Hive |
| 5. Milan v1.3 completeness | 6 | At least 95% of applicable requirements passing; the rest documented |
| 6. Hardening and 1.0 | 4 | Release 1.0, with a porting guide |
| After 1.0 | | Second hardware port (candidates in `hal/README.md`) |

## Phase 0 status

- [x] Repository, licence, contribution rules, decision records
- [x] Dependencies pinned with west; public sources only
- [x] GenAVB 7.6.2 builds for the RT1176 with `-Werror`
- [x] Reference endpoint image links with a valid boot header
- [x] Protocol code builds for the host; first unit tests
- [x] Simulator spike verdict (decision 0005)
- [x] Requirement matrix v0, timing targets
- [x] CI workflow and devcontainer
- [x] CI run on GitHub: Linux and macOS green

## Phase 1 status

- [x] HAL v0.1 specified as C headers (decision 0010), checked as C11, C++17, Mach-O and Arm
- [x] GenAVB `clz` made portable; 90 of 95 OS-layer files build on the host
- [x] First HAL implementation: simulated gPTP clock, with tests
- [x] Simulation world (virtual and real time), simulated timers and Ethernet ports joined by a virtual cable, with tests (gPTP-style peer delay measured to within 2 ns)
- [x] GenAVB port driver on top of the HAL (`paad/genavb`, GenAVB driver type `HAL_t`), tested inside FreeRTOS on the host
- [x] `openpaad-sim`: full GenAVB stack on simulated hardware; gPTP locks port 1 to port 0 (end-to-end test in CI)
- [x] `openpaad-sim`: independent simulated devices in separate processes, joined by a local virtual cable; gPTP lock and ATDECC discovery checked end to end
- [x] Virtual-time simulation (decision 0013): time moves only when every task is idle, to the next simulated event or FreeRTOS tick; cabled devices advance in lockstep. Exact GenAVB timing whatever the host's load.
- [x] Audio between two simulated devices: AAF (2 ch, 32 bit, 48 kHz, class A), reserved with SRP, timed by GenAVB's gPTP-derived media clock; in virtual time every frame arrives (end-to-end test). In real time on a host it loses data.
- [x] Media clock recovery on the HAL (decision 0014): GenAVB's own loop on the HAL media clock (steer and capture), simulated audio oscillators; in virtual time the talker's audio clock locks to gPTP and the listener's to the stream, both within a few ppb and about 50 ns of phase (end-to-end test)
- [x] Connection the Milan way (decision 0015): each simulated device is a Milan-mode ATDECC entity, a scripted controller binds and unbinds them; the end-to-end test covers discovery, binding, probing, SRP reservation, audio with media clock recovery, unbinding and rebinding
- [x] Real-network back-end (decision 0016): a simulated device on a host interface through libpcap (veth on Linux, feth on macOS, or a real port), for Hive; end-to-end test on a veth pair in CI
- [x] Conformance harness: la_avdecc controller against simulated devices, every commit (decision 0017)
- [ ] Fuzz targets
