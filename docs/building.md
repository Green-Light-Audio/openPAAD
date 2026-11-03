# Building openPAAD

## Workspace layout

openPAAD uses [west](https://docs.zephyrproject.org/latest/develop/west/index.html)
to fetch its dependencies at pinned commits. The repository is the west
manifest; dependencies land next to it:

```
openpaad-workspace/
  .west/
  openpaad/        this repository
  deps/
    mcuxsdk/       MCUXpresso SDK 26.03.00 (drivers, device files, FreeRTOS, components, examples)
    rtos-abstraction-layer/
    rtos-apps/
```

`scripts/bootstrap.sh` runs `west init -l openpaad` and `west update`. The SDK
examples repository (about 1.5 GB, needed only for the EVKB board files) is
in the `board-files` group; bootstrap enables it unless you pass
`--host-only`.

## Toolchain

Firmware builds use the xPack GNU Arm Embedded GCC 14.2.1-1.1 (decision
0006). Install it and point `ARMGCC_DIR` at it:

```sh
curl -LO https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v14.2.1-1.1/xpack-arm-none-eabi-gcc-14.2.1-1.1-linux-x64.tar.gz
echo "ed8c7d207a85d00da22b90cf80ab3b0b2c7600509afadf6b7149644e9d4790a6  xpack-arm-none-eabi-gcc-14.2.1-1.1-linux-x64.tar.gz" | sha256sum -c
tar -xzf xpack-arm-none-eabi-gcc-14.2.1-1.1-linux-x64.tar.gz -C /opt
export ARMGCC_DIR=/opt/xpack-arm-none-eabi-gcc-14.2.1-1.1
```

On macOS use the `darwin-arm64` or `darwin-x64` archive from the same
release. The devcontainer (`.devcontainer/`) has everything installed.

Host builds need a C/C++ compiler (GCC 12+ or Clang 15+), CMake 3.22+ and
Ninja. GoogleTest is used from the system if present, otherwise fetched.
libpcap lets simulated devices use real network interfaces (`--net`): it is
part of macOS; on Linux install `libpcap-dev` (without it, or with
`-DOPENPAAD_SIM_PCAP=OFF`, everything else builds and runs).
Linux and macOS are both supported hosts (decision 0008); on macOS:

```sh
xcode-select --install
brew install cmake ninja googletest
pip3 install west
```

## Builds

| Preset | What | Output |
| --- | --- | --- |
| `host` | GenAVB protocol code and openPAAD for the build machine; unit tests | `build/host/` |
| `evkb-rt1170` | Firmware for the MIMXRT1170-EVKB (Cortex-M7, XIP) | `build/evkb-rt1170/apps/ref-endpoint/openpaad-ref-endpoint.{elf,bin,hex}` |

```sh
cmake --preset host && cmake --build --preset host && ctest --preset host
cmake --preset evkb-rt1170 && cmake --build --preset evkb-rt1170
```

`scripts/ci.sh` runs both, exactly as CI does.

## Running the simulator

The host build also produces `openpaad-sim`, the stack on simulated hardware:
GenAVB under FreeRTOS (POSIX port) on a board with two network ports joined
by a virtual cable. Port 0 runs the full stack; port 1 runs a second gPTP
instance whose clock starts 1000 s away and ticks 20 ppm fast.

```sh
build/host/apps/openpaad-sim/openpaad-sim -t 30
```

Every second it prints port 1's gPTP time minus port 0's. GenAVB's own log
goes to `openpaad-sim.log` (`--log <file>` to choose, `-v` to show it in the
terminal instead). `--check-gptp <ns>`
turns it into a test (CI runs `--virtual -t 15 --check-gptp 100`). The offset is computed
exactly, for one simulated instant, so it shows gPTP's own accuracy and not
the time it takes to read two clocks.

### Two devices

Each `openpaad-sim` process can also be one device with the full stack,
cabled to a device in another process on the same machine (UNIX sockets, no
privileges needed):

```sh
build/host/apps/openpaad-sim/openpaad-sim --cable demo --side a -t 30 &
build/host/apps/openpaad-sim/openpaad-sim --cable demo --side b -t 30
```

Side a is grandmaster capable with an exact clock; side b starts 1000 s
away, runs 20 ppm fast and locks onto a. Each prints its gPTP time minus the
host clock, the same number on both sides once locked, and logs to
`openpaad-sim-a.log` / `openpaad-sim-b.log`, where each device's ATDECC
discovery of the other shows up after a few seconds (random advertising
delays). `tests/e2e/two_devices.sh` checks both.

Each device is an ATDECC entity in Milan mode (decision 0015), built from
GenAVB's reference listener and talker model: one AAF stream (2 channels,
32 bits, 48 kHz) and one CRF stream each way. Its entity ID is the EUI-64
of its MAC address: `02005afffead0001` for a, `02005afffead0002` for b.

### Virtual time

`--virtual` runs the simulation in virtual time (decision 0013): time stands
still while the stack has work and jumps to the next event when it is idle,
so every timer fires exactly on time, whatever the machine's load. Both
devices of a cable must use it; they then advance in lockstep. `-t` counts
simulated seconds; two devices run about 4 times slower than real time, one
process about as fast as real time.

### Connecting the devices

A device streams when a controller connects it, as with Hive. `--control`
adds a controller entity to a device and gives it a script: at each given
second, once it has discovered both devices, it connects b's audio input to
a's audio output (setting b's clock source to that input the first time) or
disconnects it:

```sh
build/host/apps/openpaad-sim/openpaad-sim --virtual --cable demo --side a -t 20 \
    --control connect@1,disconnect@9,connect@12 &
build/host/apps/openpaad-sim/openpaad-sim --virtual --cable demo --side b -t 20
```

Each device prints what ATDECC does (binding, stream connected or
disconnected, clock source) and, every second, its streams: frames sent by
the talker, frames received and gaps for the listener, or `no stream
connected`. In virtual time every frame arrives (`tests/e2e/audio.sh` runs
this script). In real time the stream loses data: FreeRTOS on a host cannot
keep GenAVB's 125 us network period.

Each simulated device also has an audio clock (24.576 MHz, its own
oscillator error: a +40 ppm, b −30 ppm), recovered by GenAVB (decision
0014): to gPTP when its clock source is internal, to the stream once the
controller has made the stream input its clock source. Both report it every
second:

```text
openpaad-sim[b]: t=10s media clock: locked to stream, adjust +29.998 ppm, error -0.002 ppm, phase -24 ns, max 24 ns, resets 0
```

`adjust` is the correction GenAVB applies. `error` and `phase` come from the
simulator, which knows the audio clock exactly: its rate over the last
second against gPTP time, and how far it has drifted from a perfect
gPTP-rate clock since recovery locked (`max`: the largest drift since then).

### On a real network (Hive)

`--net IFNAME` plugs a device into a network interface of the machine
instead of a cable (decision 0016): its frames go out on that interface, so
Hive or any other ATDECC controller can discover it, read its entity model
and connect it. It runs in real time, and needs the right to capture on the
interface (root, or capture rights such as those Wireshark's ChmodBPF
gives on macOS).

Use a pair of virtual interfaces, which make a private network on the
machine. Several devices can share one end of the pair, as on one network
segment; the controller goes on the other end. On macOS (feth):

```sh
sudo ifconfig feth0 create && sudo ifconfig feth1 create
sudo ifconfig feth0 peer feth1 && sudo ifconfig feth0 up && sudo ifconfig feth1 up
sudo build/host/apps/openpaad-sim/openpaad-sim --net feth0 --side a --log /tmp/openpaad-sim-a.log &
sudo build/host/apps/openpaad-sim/openpaad-sim --net feth0 --side b --log /tmp/openpaad-sim-b.log
```

In Hive, choose the PCap protocol interface and the interface `feth1`: the
two entities (`openpaad-sim a` and `openpaad-sim b`) appear, and b's audio
input can be connected to a's audio output (set b's clock source to its
audio input first, to see its media clock follow the stream). Remove the
pair with `sudo ifconfig feth0 destroy && sudo ifconfig feth1 destroy`.

On Linux, the same with veth:

```sh
sudo ip link add paad0 type veth peer name paad1
sudo ip link set paad0 up && sudo ip link set paad1 up
sudo build/host/apps/openpaad-sim/openpaad-sim --net paad0 --side a &
sudo build/host/apps/openpaad-sim/openpaad-sim --net paad0 --side b
```

A physical interface works too, but the devices then talk to that
network, gPTP announcements included. On a host interface timestamps are
software ones: gPTP locks to a few microseconds instead of nanoseconds,
and audio loses frames as in any real-time run. `tests/e2e/net.sh` runs
two devices and the scripted controller on a veth pair (Linux, root or
passwordless sudo; skipped otherwise).

## Flashing (Phase 2)

The EVKB's on-board MCU-Link probe accepts the `.bin` through its USB mass
storage drive, or the `.elf` through LinkServer or J-Link. Board bring-up
instructions arrive with Phase 2.
