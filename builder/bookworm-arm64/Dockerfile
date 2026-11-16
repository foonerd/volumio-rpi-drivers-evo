# Debian bookworm on arm64: the distribution, architecture and compiler
# package that built the shipped 64-bit kernels.
FROM --platform=linux/arm64 debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      bc bison build-essential ca-certificates cpio curl device-tree-compiler \
      dpkg-dev dwarves flex git gcc-12 kmod libelf-dev libssl-dev patch python3 \
      rsync xz-utils \
 && rm -rf /var/lib/apt/lists/*
