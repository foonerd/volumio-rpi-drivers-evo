# Raspbian trixie on armhf: the distribution, architecture and compiler
# package that built the shipped 32-bit kernels.
#
# Raspbian publishes no container image, so the first stage bootstraps one
# from the Raspbian archive, verified against the archive's signing key. The
# bootstrap runs on Debian bookworm, whose tools accept that key.
#
# The Rust toolchain is not used to build modules. It is here because the
# shipped kernel's configuration records it, and the build requires that
# configuration to be reproduced exactly.
FROM --platform=linux/arm/v7 debian:bookworm-slim AS bootstrap
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl debootstrap gnupg \
 && curl -fsSL http://raspbian.raspberrypi.com/raspbian.public.key -o /raspbian.key \
 && gpg --show-keys --with-colons /raspbian.key \
      | grep -q '^fpr:::::::::A0DA38D0D76E8B5D638872819165938D90FDDD2E:' \
 && gpg --dearmor -o /raspbian.gpg /raspbian.key \
 && debootstrap --arch=armhf --variant=minbase --keyring=/raspbian.gpg \
      trixie /rootfs http://raspbian.raspberrypi.com/raspbian \
 && rm -rf /rootfs/var/cache/apt/archives/*.deb /rootfs/var/lib/apt/lists/*

FROM scratch
COPY --from=bootstrap /rootfs /
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      bc bindgen bison build-essential ca-certificates cpio curl \
      device-tree-compiler dpkg-dev dwarves flex git gcc-14 kmod libelf-dev \
      libssl-dev patch python3 rsync rust-src rustc xz-utils \
 && rm -rf /var/lib/apt/lists/*
