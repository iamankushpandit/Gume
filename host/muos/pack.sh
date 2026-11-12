#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Assemble Braino for a muOS card: what goes on its ROMS (exFAT) partition,
# laid out as it is there.
#
#   host/muos/pack.sh <arm64-build-dir> <native-build-dir> <out-dir>
#
# <arm64-build-dir> holds the cross-compiled braino (cmake/aarch64-linux-gnu
# .cmake); <native-build-dir> a braino that runs here, used only to render the
# boot logo, so the logo is drawn by the same code as the splash the program
# shows. The result:
#
#   <out>/ROMS/Ports/Braino.sh      the launcher (see host/muos/Braino.sh)
#   <out>/ports/braino/braino       the program
#   <out>/ports/braino/bootlogo.bmp the boot logo, 640x480 24-bit
#
# Copy the contents of <out> onto the card's ROMS partition, merging folders.
set -eu

ARM="${1:?arm64 build dir}"
NATIVE="${2:?native build dir}"
OUT="${3:?output dir}"
HERE="${BRAINO_MUOS_DIR:-$(cd "$(dirname "$0")" && pwd)}"

[ -x "$ARM/braino" ] || { echo "no arm64 braino in $ARM" >&2; exit 1; }
[ -x "$NATIVE/braino" ] || { echo "no native braino in $NATIVE" >&2; exit 1; }
case "$(file -b "$ARM/braino")" in
    *aarch64*) ;;
    *) echo "$ARM/braino is not an aarch64 binary" >&2; exit 1 ;;
esac

mkdir -p "$OUT/ROMS/Ports" "$OUT/ports/braino"

# A carriage return after the shebang is "sh\r: not found" on the device, so
# the launcher is written with Unix line endings whatever the checkout has.
tr -d '\r' < "$HERE/Braino.sh" > "$OUT/ROMS/Ports/Braino.sh"
chmod 755 "$OUT/ROMS/Ports/Braino.sh"

STRIP=llvm-strip
command -v "$STRIP" >/dev/null 2>&1 || STRIP=$(ls /usr/bin/llvm-strip-* 2>/dev/null | head -1)
cp -f "$ARM/braino" "$OUT/ports/braino/braino"
[ -n "$STRIP" ] && "$STRIP" "$OUT/ports/braino/braino"
chmod 755 "$OUT/ports/braino/braino"

"$NATIVE/braino" --splash "$OUT/ports/braino/bootlogo.bmp" 640 480 > /dev/null

echo "packed into $OUT:"
find "$OUT" -type f -printf '  %P  %s bytes\n'
