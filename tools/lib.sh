# Shared helpers for the tools in this directory. Sourced, not executed.
# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck shell=bash

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
WORK=${WORK:-$ROOT/work}
# Physical paths throughout: the compiler sees them, and the build maps them
# away so that no module records where it was built.
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd -P)

die() {
	echo "error: $*" >&2
	exit 1
}

note() {
	echo "==> $*"
}

# load_target <name>: read targets/<name>.conf and derive the paths used
# by every tool.
load_target() {
	[ $# -ge 1 ] && [ -n "$1" ] || die "no target given; see targets/"
	TARGET=$1
	local conf="$ROOT/targets/$TARGET.conf"
	[ -f "$conf" ] || die "no such target: $TARGET"
	# shellcheck source=/dev/null
	. "$conf"
	SETDIR="$ROOT/sets/$TARGET_SET"
	[ -d "$SETDIR" ] || die "target $TARGET names a missing set: $TARGET_SET"
	TWORK="$WORK/$TARGET"
	SRC="$TWORK/src"
	NAME="volumio-rpi-drivers-$TARGET"
}

# flavour_fields <flavour>: print "<arch> <builder> <headers deb> <sha256>".
flavour_fields() {
	local var="FLAVOUR_${1//-/_}"
	[ -n "${!var:-}" ] || die "target $TARGET has no flavour $1"
	echo "${!var}"
}

# load_builder <name>: read builder/<name>/env into BUILDER_PLATFORM,
# BUILDER_CROSS_COMPILE, BUILDER_CC and, where the shipped kernel was
# configured with one, BUILDER_CROSS_COMPILE_COMPAT.
load_builder() {
	BUILDER=$1
	BUILDER_CROSS_COMPILE_COMPAT=""
	local env="$ROOT/builder/$BUILDER/env"
	[ -f "$env" ] || die "no such builder: $BUILDER"
	# shellcheck source=/dev/null
	. "$env"
}

# units: names of the units in the target's set, sorted.
units() {
	local d
	for d in "$SETDIR"/*/; do
		[ -f "$d/unit.conf" ] && basename "$d"
	done
	return 0
}

# load_unit <name>: read a unit description into SUMMARY, DIR, MODULES,
# OVERLAYS, KCONFIG, WHOLE_DIR, RELEASE, ONLY_FLAVOURS, the UPSTREAM and
# FIRMWARE keys, and UNITDIR.
load_unit() {
	SUMMARY="" DIR="" MODULES="" OVERLAYS="" KCONFIG="" WHOLE_DIR="no" RELEASE="yes"
	ONLY_FLAVOURS=""
	UPSTREAM="" UPSTREAM_BRANCH="" UPSTREAM_REF="" UPSTREAM_PATH=""
	FIRMWARE_URL="" FIRMWARE_DIR=""
	UNITDIR="$SETDIR/$1"
	[ -f "$UNITDIR/unit.conf" ] || die "no such unit in set $TARGET_SET: $1"
	# shellcheck source=/dev/null
	. "$UNITDIR/unit.conf"
}

# verify_sha256 <file> <expected>
verify_sha256() {
	local got
	got=$(sha256sum "$1" | cut -d' ' -f1)
	[ "$got" = "$2" ] || die "checksum mismatch for $(basename "$1"): expected $2, got $got"
}
