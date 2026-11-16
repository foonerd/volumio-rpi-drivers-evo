# volumio-rpi-drivers-evo

Kernel modules and device tree overlays that Volumio adds to the Raspberry Pi
kernel.

Volumio runs the kernel that Raspberry Pi ships. This repository builds a
set of drivers against exactly that kernel, with the compiler that built it,
and packs them, with the firmware they load, into one archive per kernel.

Its purpose is to keep Volumio ahead of that kernel: Wi-Fi and other
communication hardware that is on sale today should work, without waiting
for the kernel to catch up.

## What it provides

| Unit | 6.12.115 | 6.18.60 | Provides |
| --- | --- | --- | --- |
| `usb-audio` | yes | yes | `snd-usb-audio` with native DSD formats and device quirks for USB DACs |
| `ax88179` | yes | yes | ASIX AX88179/AX88178A USB 3.0 Gigabit Ethernet, vendor driver, for ASIX's own and the rebranded adapters |
| `rtw88` | yes | yes | Realtek rtw88 Wi-Fi 5 driver set, including USB and SDIO adapters, from its upstream project |
| `rtw89` | yes | yes | Realtek rtw89 Wi-Fi 6 and 7 driver set, including USB adapters, from its upstream project |
| `r8152` | yes | yes | Realtek USB Ethernet, the current mainline driver, including the 5 and 10 gigabit chips |
| `bluetooth-usb` | yes | yes | Bluetooth USB adapters: mainline's device entries for chips the kernel's driver already handles |
| `waveshare-dsi` | yes | yes | Waveshare DSI touchscreen panels |
| `i-sabre` | yes | yes | Audiophonics I-Sabre codec with transfer retries and device state read-back |
| `es9039q2m` | yes | yes | Device tree overlay for the ES9039Q2M I2S DAC |
| `i2c-designware` | on request | on request | DesignWare I2C controller with a 35 ms bus stuck-low timeout |

USB adapters served, counted as distinct USB device IDs:

| | Kernel 6.12.115 as shipped | Kernel 6.18.60 as shipped | With this repository |
| --- | --- | --- | --- |
| Realtek Wi-Fi 5 (rtw88) | 52 | 132 | 133 |
| Realtek Wi-Fi 6 and 7 (rtw89) | 0 | 33 | 77 |
| Realtek Ethernet (r8152) | 30 | 32 | 35 |
| Bluetooth (btusb), devices with their own entry | 350 | 361 | 385 and 386 |

## Kernels and boards

| Target | Kernel | Source |
| --- | --- | --- |
| `6.12.115-rpt` | 6.12.115, package `1:6.12.115-1+rpt1` | Raspberry Pi OS bookworm |
| `6.18.60-rpt` | 6.18.60, package `1:6.18.60-1+rpt1` | Raspberry Pi OS trixie |

A target is built for each flavour of its kernel. The kernel release is
`<kernel>+rpt-<flavour>`, for example `6.12.115+rpt-rpi-v8`.

| Flavour | Boards | 6.12.115-rpt | 6.18.60-rpt |
| --- | --- | --- | --- |
| `rpi-v6` | Pi 1, Zero, Zero W, CM1 | yes | yes |
| `rpi-v7` | Pi 2, Pi 3, Pi 3+, Zero 2 W, CM3, CM3+ with a 32-bit kernel | yes | yes |
| `rpi-v7l` | Pi 4, Pi 400, CM4 with a 32-bit kernel | yes | no such kernel |
| `rpi-v8` | Pi 3 and later, Pi 5, CM5 with a 64-bit kernel, 4K pages | yes | yes |
| `rpi-2712` | Pi 5, Pi 500, CM5 with the 64-bit kernel, 16K pages | no | yes |

On a 16K-page kernel, 32-bit programs run only when they were built for
that page size, so `rpi-2712` suits a 64-bit system.

## How it works

A driver is carried as complete files, placed into the kernel's source tree
at their kernel paths. Nothing is patched.

Each flavour is built inside a container of the distribution, architecture
and compiler package that built the shipped kernel of that flavour: Debian
on arm64 for the 64-bit kernel and Raspbian on armhf for the 32-bit kernels,
in the release the kernel was built on. The kernel's own configuration and
symbol versions are taken from Raspberry Pi's kernel packages, and only the
modules of the units are compiled.

A build stops, and writes no archive, unless:

- every replaced file was derived from the kernel's current version of that
  file,
- the builder reproduces the shipped kernel's configuration exactly, the
  compiler identification included,
- every module reports the exact release of the kernel it was built for,
- every module carries symbol versions taken from that kernel, and
- every module that replaces one of the kernel's serves at least the devices
  the kernel's own module serves.

## Requirements

- Docker.
- On a host that is not ARM: QEMU user emulation for `arm64` and `arm`,
  registered with the kernel. On Debian and Ubuntu that is the
  `qemu-user-static` package.
- Network access to `archive.raspberrypi.com`, `raspbian.raspberrypi.com`,
  `deb.debian.org` and `github.com`.

## Building

```sh
./build 6.12.115-rpt
```

This downloads and verifies the kernel source and, for each flavour, the
kernel's configuration and symbol versions; builds every unit for every
flavour; and writes:

```text
output/volumio-rpi-drivers-6.12.115-rpt.tar.gz
output/volumio-rpi-drivers-6.12.115-rpt.tar.gz.sha256
```

The first run of a target also builds its builder images. Under emulation a
flavour takes a few minutes.

Build one flavour only; the result stays in `work/` and no archive is written:

```sh
./build 6.12.115-rpt rpi-v8
```

Include the units that are built only on request; the result goes to
`work/<target>/pkg-all/` and is never packed:

```sh
./build -a 6.12.115-rpt rpi-v8
```

Downloads, kernel trees and intermediate files live in `work/`. Delete that
directory to start from nothing.

### What upstream offers beyond the build

Some units follow an upstream project. Every build ends by fetching each
such project's current state and reporting how far it has moved since the
unit was taken from it, which source files it has added, and which devices
it serves that the unit does not:

```text
==> rtw89 follows https://github.com/morrownr/rtw89 (main)
    unit taken from f2ce66eda627; upstream is at 1a2b3c4d5e6f (2026-12-01), 14 commits later
    devices upstream serves that the unit does not: 3
      usb 0x0bda:0xc832
```

This is a report and never fails a build. `./build -c <target>` prints it
without building.

## The archive

```text
volumio-rpi-drivers-<target>/
  boot/overlays/<name>.dtbo
  lib/modules/<kernel release>/updates/<kernel path>/<module>.ko.xz
  lib/firmware/<directory>/<firmware file>
```

Modules are installed under `updates/`, which takes precedence over the
kernel's own copy of a module with the same name. The kernel's files are not
overwritten.

### Installing on a running device

For the kernel the device is running:

```sh
tar -xzf volumio-rpi-drivers-6.12.115-rpt.tar.gz
sudo cp -r volumio-rpi-drivers-6.12.115-rpt/lib/modules/"$(uname -r)" /lib/modules/
sudo cp -r volumio-rpi-drivers-6.12.115-rpt/lib/firmware/. /lib/firmware/
sudo depmod -a
```

Copy the files under `boot/overlays/` to the overlays directory of the boot
partition, then reboot.

### Checking what is loaded

The module in use is the one from this archive when its path is under
`updates/` and its source version matches the loaded one:

```sh
modinfo -n snd-usb-audio
modinfo -F srcversion snd-usb-audio
cat /sys/module/snd_usb_audio/srcversion
```

A module the kernel refused is reported in the kernel log:

```sh
dmesg | grep -iE 'disagrees about version|version magic|Unknown symbol|Invalid module format'
```

These modules are built outside the kernel's own build, so the kernel logs
one "loading out-of-tree module" notice when the first of them loads.

## Layout

```text
build                 one-command entry point
targets/<name>.conf   a kernel: where its source and configuration come from,
                      its flavours and the builder of each, the set it uses
sets/<kernel>/        the units for one kernel version
  <unit>/unit.conf    what the unit builds
  <unit>/files/       files placed into the kernel tree, at their kernel paths
  <unit>/stock/       the kernel's own version of each replaced file
  <unit>/firmware.sha256
                      firmware files the unit's drivers load, with checksums
builder/<name>/       a build environment: container definition and compiler
tools/                fetch, check, build, pack and upstream steps
```

### Units

A unit replaces or adds files in one kernel directory, or switches on drivers
the kernel has but does not build, and names the modules to build from it.

| Key | Meaning |
| --- | --- |
| `SUMMARY` | One line describing the unit |
| `DIR` | Kernel directory the unit builds in |
| `MODULES` | Modules to build and pack, or `*` for every module in `DIR` |
| `OVERLAYS` | Device tree overlays to compile, without the `-overlay.dts` suffix |
| `KCONFIG` | Configuration symbols the unit needs beyond the kernel's own, as `NAME=m` or `NAME=y` |
| `WHOLE_DIR` | `yes` when `files/` is the complete directory rather than files laid over the kernel's |
| `RELEASE` | `no` to build the unit only with `-a` and keep it out of the archive |
| `ONLY_FLAVOURS` | Flavours the unit is built for, when the kernel has its driver only in some |
| `UPSTREAM` | Git URL of the project the unit follows |
| `UPSTREAM_BRANCH` | The branch of that project to watch |
| `UPSTREAM_PATH` | The file or directory followed, when it is one part of a large project |
| `UPSTREAM_REF` | The commit the unit's files were taken from |
| `FIRMWARE_URL` | Where the files listed in `firmware.sha256` are downloaded from |
| `FIRMWARE_DIR` | Directory under `lib/firmware/` the firmware is packed into |

Every file under `files/` that replaces a kernel file has the kernel's own
version of that file under `stock/`, at the same path. A file with no
counterpart under `stock/` is new to the kernel. A unit with no `files/` only
switches drivers on, with `KCONFIG`.

A unit that follows an upstream project carries that project's sources as
its complete directory and has no `stock/`: its reference is the upstream
commit, not the kernel's file. Its modules are named as the kernel names
them, so they take the place of the kernel's own.

A unit can also follow one file of the mainline kernel, as `r8152` does. It
then replaces the kernel's file with the newer one, keeps the `stock/` copy
like any replacement, and bridges the few kernel interfaces that differ in
one marked block at the top of the file.

## Updating a unit from its upstream

1. Replace the sources under the unit's `files/` with the upstream project's
   at the commit you are taking. Keep the module names in the `Makefile` as
   the kernel's.
2. Set `UPSTREAM_REF` to that commit, and the commit in `FIRMWARE_URL`.
3. Rewrite `firmware.sha256` from the project's firmware files:
   `sha256sum *.bin`.
4. Build. The build stops if any module would serve fewer devices than the
   kernel's own.

The `bluetooth-usb` unit is the kernel's own `btusb.c` with mainline's
missing device entries added, for chips that kernel's driver already
handles. Rebuild it from a newer mainline with:

```sh
tools/btusb-entries <mainline btusb_main.c> <unit>/stock/drivers/bluetooth/btusb.c \
    <unit>/files/drivers/bluetooth/btusb.c <mainline commit>
```

It prints the entries it leaves out because they need code the kernel does
not have.

## Moving to a new kernel

Every kernel version has its own set, point release included, also when its
files are identical to another kernel's.

1. Copy the set of the nearest kernel to `sets/<new kernel version>/`.
2. Add `targets/<name>.conf` for the kernel: the set it uses, the checksums
   of its inputs and the builder of each flavour.
3. Run the check:

   ```sh
   ./build -c <name>
   ```

   It lists every replaced file whose kernel version differs from the copy
   under `stock/`, and prints the difference.
4. For each file listed, carry the kernel's change into the file under
   `files/`, then replace the copy under `stock/` with the kernel's new
   version.
5. Run `./build <name>`. The build refuses to start while the check reports
   a difference.

A kernel built by a different distribution or compiler gets its own
directory under `builder/`. The build tells you when one is needed: it stops
and prints every configuration entry the builder fails to reproduce.

## Changing or adding a driver

Edit the file under `sets/<kernel>/<unit>/files/` and build.

To add a driver, create a unit directory with a `unit.conf`, put the files
under `files/` at their kernel paths, and copy the kernel's own version of
any file being replaced to the same path under `stock/`.

## Earlier kernels

Kernels before 6.12 are served by
[volumio-rpi-custom](https://github.com/volumio/volumio-rpi-custom).

## Credits

The driver changes for kernel 6.12 were first collected in volumio-rpi-custom
by its contributors. The ASIX driver is ASIX's own. The Realtek Wi-Fi units
are the work of the [rtw88](https://github.com/lwfinger/rtw88) and
[rtw89](https://github.com/morrownr/rtw89) projects and of the Linux wireless
developers they follow. Every file under `sets/` derives from the Linux
kernel, from one of those projects or from a vendor's driver, and keeps its
authors' copyright notices.

## Support this project

volumio-rpi-drivers-evo is free and open source, designed, built and
maintained by one developer in his own time. If it has earned a place in
your build, you can support its development:

- GitHub Sponsors: <https://github.com/sponsors/foonerd>
- Ko-fi: <https://ko-fi.com/justanerd>

Sponsorship is a thank-you, not a purchase: it buys no priority, no features
and no support agreement.

## Licence

GNU General Public License v2.0 or later; see [LICENSE](LICENSE). Files under
`sets/` keep the licence stated in their own headers. Firmware is not part of
this repository: it is downloaded when an archive is built and stays under
its vendor's licence.
