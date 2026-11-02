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

## Flashing (Phase 2)

The EVKB's on-board MCU-Link probe accepts the `.bin` through its USB mass
storage drive, or the `.elf` through LinkServer or J-Link. Board bring-up
instructions arrive with Phase 2.
