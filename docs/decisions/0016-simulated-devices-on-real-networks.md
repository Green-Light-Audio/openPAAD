# 0016. Simulated devices on real networks, through libpcap

- Status: accepted
- Date: 2026-11-03
- Refines: 0005, 0012

## Context

Simulated devices only talked to each other, through a virtual cable
between processes. To check them against the tools and equipment openPAAD
must work with (Hive and other ATDECC controllers first, Milan devices
later), a simulated device has to send and receive real Ethernet frames on
an interface of the host.

## Decision

1. A simulated port can be plugged into a host interface instead of a
   cable (`paad_sim_eth_connect_host`, `openpaad-sim --net IFNAME`). It uses
   libpcap: BPF on macOS, packet sockets on Linux, one code path for both.
   libpcap is part of macOS and one package on Linux; builds without it
   keep everything else.
2. The port is promiscuous and keeps the frames sent to its address or its
   multicast groups. The host also shows it the frames it sent, and the
   frames other programs send on the same interface: it drops its own, by
   source address, and keeps the others. Several simulated devices can so
   share one interface, as on one network segment.
3. Received frames carry the time the kernel received them, converted to
   the simulation's clock. Transmit times are read just before sending.
   These software timestamps are good to some microseconds, so on a host
   interface gPTP accepts a link delay up to 1 ms (the usual 800 ns limit
   assumes hardware timestamps).
4. Real time only: virtual time cannot hold back the rest of the network.
5. Virtual interface pairs make an isolated network on one machine: veth
   on Linux, feth on macOS. The end-to-end test runs two simulated devices
   on a veth pair, where root or passwordless sudo is available (CI);
   elsewhere it is skipped.

## Consequences

Hive, or any controller on the host, can discover a simulated device,
read its entity model and connect it, through the same ATDECC code that
runs on the boards. gPTP locks between simulated devices on a host
interface, to a few microseconds. Audio still loses frames in real time on
a host (decision 0013): streaming audio to real equipment waits for the
boards (Phase 2).

A simulated device on a physical interface puts its frames on that
network, gPTP announcements included; the documentation recommends
virtual pairs.

Update, 2026-11-03: Hive does not list virtual interfaces such as feth, and
macOS drops frames sent on a port without a link, so on a Mac Hive needs an
Ethernet port with a link. Automated controller checks go through
la_avdecc instead (decision 0017).
