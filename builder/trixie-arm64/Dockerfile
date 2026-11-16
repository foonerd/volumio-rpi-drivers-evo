# Debian trixie on arm64: the distribution, architecture and compiler
# package that built the shipped 64-bit kernels.
#
# The Rust toolchain and the 32-bit companion compiler are not used to build
# modules. They are here because the shipped kernel's configuration records
# them, and the build requires that configuration to be reproduced exactly.
FROM --platform=linux/arm64 debian:trixie-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      bc bindgen bison build-essential ca-certificates cpio curl \
      device-tree-compiler dpkg-dev dwarves flex git gcc-14 \
      gcc-arm-linux-gnueabihf kmod libelf-dev libssl-dev patch python3 rsync \
      rust-src rustc xz-utils \
 && rm -rf /var/lib/apt/lists/*
