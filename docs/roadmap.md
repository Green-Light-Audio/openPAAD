# Roadmap

Each phase ends on a gate that is a test result, not a date.

| Phase | Weeks | Exit gate |
| --- | --- | --- |
| 0. Foundations | 2 | CI green and reproducible; requirement matrix published; simulator spike verdict; timing targets written down |
| 1. Simulation | 4 | Two simulated endpoints stream audio, discovered and connected from Hive and la_avdecc, on every commit |
| 2. Board bring-up | 6 | gPTP within target for 24 hours between two EVKBs; asCapable and Pdelay rules pass |
| 3. Audio and media clock | 8 | Two boards locked through CRF with no sample slip over 72 hours |
| 4. AVB device | 4 | Audio both ways with a commercial Milan device, connected from Hive |
| 5. Milan v1.3 completeness | 6 | At least 95% of applicable requirements passing; the rest documented |
| 6. Hardening and 1.0 | 4 | Release 1.0 |

## Phase 0 status

- [x] Repository, licence, contribution rules, decision records
- [x] Dependencies pinned with west; public sources only
- [x] GenAVB 7.6.2 builds for the RT1176 with `-Werror`
- [x] Reference endpoint image links with a valid boot header
- [x] Protocol code builds for the host; first unit tests
- [x] Simulator spike verdict (decision 0005)
- [x] Requirement matrix v0, timing targets
- [x] CI workflow and devcontainer
- [ ] CI run on GitHub (needs the public repository)
