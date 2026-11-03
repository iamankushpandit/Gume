#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Builds the RG40XX V card image with Buildroot, in Linux or WSL:
#
#   host/rg40xxv/build.sh               # everything, ending in the .img
#   host/rg40xxv/build.sh linux-rebuild # any Buildroot target
#
# BR is an unpacked Buildroot 2026.08.1, OUT the build directory and
# BR2_DL_DIR the download cache; all three default to ~/braino-rg40xxv. Keep
# OUT on a Linux filesystem: building under /mnt/c is many times slower.
set -eu
# WSL appends the Windows PATH, whose "Program Files" Buildroot refuses.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
HERE=$(cd "$(dirname "$0")" && pwd)
BR=${BR:-$HOME/braino-rg40xxv/buildroot-2026.08.1}
OUT=${OUT:-$HOME/braino-rg40xxv/out}
export BR2_DL_DIR="${BR2_DL_DIR:-$HOME/braino-rg40xxv/dl}"

# Which commit this is, for Braino's build stamp. A worktree checked out by
# Windows has a .git file naming its git directory as "C:/...", which git in
# WSL cannot follow, so that path is translated as host/cmake/BuildStamp.cmake
# does.
REPO=$(cd "$HERE/../.." && pwd)
GITDIR=
if [ -f "$REPO/.git" ]; then
    GITDIR=$(sed -n 's/^gitdir: //p' "$REPO/.git" | tr -d '\r')
    case "$GITDIR" in
        [A-Za-z]:*)
            DRIVE=$(printf '%s' "$GITDIR" | cut -c1 | tr 'A-Z' 'a-z')
            GITDIR="/mnt/$DRIVE/$(printf '%s' "$GITDIR" | cut -c4-)"
            ;;
    esac
fi
git_() { if [ -n "$GITDIR" ]; then git --git-dir="$GITDIR" --work-tree="$REPO" "$@"; else git -C "$REPO" "$@"; fi; }
BRAINO_GIT_COMMIT=$(git_ rev-parse --short=7 HEAD 2>/dev/null || true)
BRAINO_GIT_BRANCH=$(git_ rev-parse --abbrev-ref HEAD 2>/dev/null || true)
export BRAINO_GIT_COMMIT BRAINO_GIT_BRANCH

# The defconfig is the whole configuration: read it again every time, so a
# change to it can never be shadowed by an older .config.
make -C "$BR" O="$OUT" BR2_EXTERNAL="$HERE" braino_rg40xxv_defconfig >/dev/null
make -C "$OUT" "$@"
