# Timing targets

Numbers the firmware must meet, with where each one comes from. Phase 2 and
Phase 3 exit gates measure against this page.

| Quantity | Target | Source | Measured in |
| --- | --- | --- | --- |
| Class A observation interval | 125 us; one AAF frame per interval at 48 kHz (6 samples) | IEEE 802.1Q FQTSS; Milan base format | P4 |
| Stream frame spacing jitter at the talker | Frames released by the ENET_1G credit-based shaper, never by software timing | Sacha's note on sub-125 us spacing; RT1170 ENET_1G AVB queues | P4 |
| gPTP Sync interval | 125 ms | Milan 4.2.6.2.2 | P2 |
| gPTP Announce and Pdelay intervals | 1 s | Milan 4.2.6.2.2 | P2 |
| asCapable qualification | after 2 to 5 successful Pdelay exchanges | Milan 4.2.6.2.4 | P2 |
| neighborPropDelayThresh | 800 ns | Milan 4.2.6.1.1 | P2 |
| gPTP synchronisation accuracy (EVKB to EVKB through one bridge) | within 1 us, stable for 24 h | 802.1AS end-station budget; Phase 2 gate | P2 |
| Listener presentation buffer | at least 2.126 ms | Milan 4.4.2.1 | P4 |
| Media clock lock | no sample slip in 72 h between two boards locked through CRF | Phase 3 gate | P3 |
| Media clock recovery adjustment | audio PLL numerator steps through on-the-fly fractional update | RT1170 audio PLL (NXP AN14420) | P3 |
| ADP advertise period | 5 s with a random delay up to 4 s (2 s at start-up with link up); valid time 10 (20 s) | Milan 5.6 | P5 |
| LOCK_ENTITY expiry | 60 s | Milan 5.3.4.1 | P5 |

Rules for adding a row: give the number, its source (clause, datasheet or
measurement) and the phase whose gate checks it. Paraphrase; do not copy
specification text.
