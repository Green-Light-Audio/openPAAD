# 0015. Simulated devices are connected through ATDECC

- Status: accepted
- Date: 2026-11-03
- Refines: 0012

## Context

Until now the simulator's audio demo opened its streams itself: the talker
declared its stream to SRP and the listener registered for it, with stream
IDs and addresses fixed in the application. Real Milan devices do not work
that way. A controller (Hive, for example) binds a listener's stream input
to a talker's stream output; the listener then asks the talker for the
stream's parameters, both reserve it with SRP, and only then do the two
applications open the stream. GenAVB implements all of this in its ATDECC
stack, in Milan mode, with entity models it ships as reference code.

## Decision

1. Every simulated device is a Milan-mode ATDECC entity built from GenAVB's
   reference listener and talker model (one AAF and one CRF stream each
   way, one clock domain whose source is internal or a stream input). The
   entity ID is the EUI-64 of port 0's MAC address; the simulated addresses
   are locally administered, and so are these IDs.
2. The device application only follows what the ATDECC stack tells it: it
   opens and closes the streams it is given, and applies the clock source
   the entity says. It no longer talks to SRP, and the entity only starts
   once the application listens.
3. A controller is a second entity of one of the devices, driven by a
   script of connect and disconnect actions at given seconds
   (`--control`). Like a real controller it waits until it has discovered
   both entities, and its commands cross the simulated network. The first
   connection also sets the listener's clock source to its audio input, as
   a user would in Hive.
4. The `--audio` option, which ran the old demo, is removed.

## Consequences

The end-to-end test now covers what a user does: discovery, binding, the
listener's probing of the talker, SRP reservation, streaming with media
clock recovery, unbinding (both streams close and the reservation goes) and
rebinding. In virtual time the stream flows less than a second after the
bind command, and stops as soon as it is unbound.

Running it found a GenAVB bug: stream connect notifications to the
application were not zeroed, so a listener's stream flags could carry
random bits (fixed, see `third_party/README.md`).

The entity model is GenAVB's reference one, with NXP's names for vendor and
model; openPAAD's own entity model belongs to its pro-audio layer (`paad/`)
and replaces it later. The same entities will be reachable from Hive once
the simulator has a real-network back-end.
