# openPAAD development container: host and firmware builds.
FROM ubuntu:24.04

ARG XPACK_VERSION=14.2.1-1.1
ARG XPACK_SHA256=ed8c7d207a85d00da22b90cf80ab3b0b2c7600509afadf6b7149644e9d4790a6

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git curl ca-certificates python3 python3-pip python3-venv \
      libgtest-dev clang-format gdb tcpdump iproute2 \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/xpack.tgz \
      https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v${XPACK_VERSION}/xpack-arm-none-eabi-gcc-${XPACK_VERSION}-linux-x64.tar.gz \
    && echo "${XPACK_SHA256}  /tmp/xpack.tgz" | sha256sum -c - \
    && tar -xzf /tmp/xpack.tgz -C /opt && rm /tmp/xpack.tgz

RUN pip3 install --break-system-packages west==1.5.0

ENV ARMGCC_DIR=/opt/xpack-arm-none-eabi-gcc-${XPACK_VERSION}
ENV PATH=${ARMGCC_DIR}/bin:${PATH}
