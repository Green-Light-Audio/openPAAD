# 0006. Pin the Arm toolchain to xPack GNU Arm Embedded GCC 14.2.1-1.1

- Status: accepted
- Date: 2026-11-02

## Context

GenAVB builds with `-Werror` and prints 64-bit values with `PRIx64`. The
`gcc-arm-none-eabi` package of Ubuntu 24.04 (GCC 13.2.1, newlib 4.4) does not
define `PRIx64`: its `inttypes.h` tests a macro its `stdint.h` no longer
sets. The build fails on the first protocol file.

## Decision

Use the xPack GNU Arm Embedded GCC 14.2.1-1.1 release, downloaded from its
GitHub release and checked against its SHA-256
(`ed8c7d207a85d00da22b90cf80ab3b0b2c7600509afadf6b7149644e9d4790a6`
for linux-x64). The devcontainer and CI install exactly this archive. The
CMake toolchain file looks in `ARMGCC_DIR` first, then `PATH`.

## Consequences

Builds are identical on every machine. Upgrading the compiler is a
deliberate change with its own record.
