# 0012. A simulated device is a process

- Status: accepted
- Date: 2026-11-03
- Refines: 0005, 0011

## Context

GenAVB keeps its state in global variables, so one process holds one
stack. Two endpoint ports of one stack can be cabled together (the
`openpaad-sim` bench), but they share SRP, AVDECC and the rest: not two
devices. Testing devices against each other, and later controllers such as
Hive, needs independent stacks.

## Decision

1. One `openpaad-sim` process is one device.
2. Devices on the same machine are cabled with UNIX datagram sockets
   (`paad_sim_eth_connect_socket()`): no privileges, Linux and macOS alike.
   Each message carries a frame and the simulated times its first and last
   bit reach the far end; hellos every 100 ms carry the link state.
3. Timing stays exact across processes because every process takes its
   simulated time from the host's monotonic clock, which they all share.
   A frame that crosses between processes later than it would cross the
   cable is delivered late, but timestamped as if it had not been.
4. The socket is polled by the simulated interrupt task (decision 0011),
   which then wakes at least every tick.

## Consequences

Two devices lock gPTP to within a few nanoseconds and discover each other
with ATDECC, checked end to end in CI (`tests/e2e/two_devices.sh`).
Machines are not bridged: connecting to real equipment or controllers needs
the real-network back-ends (veth, feth), which come next.
