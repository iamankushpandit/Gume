#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# The "OS base": a finished Buildroot output directory, cut down to what a
# build that changes only Braino still reads, so CI can keep it between runs
# and rebuild only Braino on top of it.
#
#   host/rg40xxv/os-base.sh key    # what the base depends on, as one hash
#   host/rg40xxv/os-base.sh trim   # cut $OUT down to the base, in place
#   host/rg40xxv/os-base.sh settle # date the OS inputs before an unpacked base
#
# Why this works: Braino's package version is its commit (package/braino/),
# so a new commit is a new build directory that Buildroot builds from
# scratch, and with the root filesystem built into the kernel, every `make`
# repacks it and relinks the kernel (linux-rebuild-with-initramfs). Nothing
# else needs to rebuild, because nothing else changed -- and the key below is
# what says nothing else changed.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HOME/braino-rg40xxv/out}

# Everything the OS is built from, apart from the Buildroot release: the
# configuration, every package but Braino's own, the kernel and busybox
# fragments, ROCKNIX's patches and device trees, and this script. Left out,
# because every build applies them afresh: Braino's package, the overlay and
# the post-build and post-image scripts, the boot menu and the card layout.
# Paths are relative to this directory, which is where both callers are.
os_inputs() {
    find . -type f \
        ! -path './package/braino/*' \
        ! -path './board/overlay/*' \
        ! -name post-build.sh ! -name post-image.sh \
        ! -name extlinux.conf ! -name genimage.cfg \
        ! -name README.md ! -name write-card.ps1 \
        | LC_ALL=C sort
}

case "${1:-}" in
key)
    # Bump BASE_EPOCH to force a full build.
    BASE_EPOCH=1
    cd "$HERE"
    {
        echo "epoch $BASE_EPOCH"
        echo "buildroot ${BUILDROOT:-buildroot-2026.08.1}"
        os_inputs | xargs sha256sum
    } | sha256sum | cut -c1-16
    ;;
settle)
    # A checkout dates every file now, after every stamp in the base, and
    # make reads that as a change: busybox.fragment newer than busybox's
    # .stamp_dotconfig reconfigures busybox, whose source the trim removed,
    # and the build stops. The key has already said these files are the
    # ones the base was built from, so they are dated before it instead.
    cd "$HERE"
    os_inputs | xargs touch -d '2000-01-01 00:00:00 UTC'
    ;;
trim)
    # What stays whole:
    #   host/, staging, target/   the toolchain, the libraries and the root
    #                             filesystem every later step builds on
    #   images/                   U-Boot and the rest of what post-image packs
    #   build/linux-*             the kernel's own tree: it is relinked with
    #                             the new root filesystem on every build
    #   build/braino-<library>-*  the three Arduino libraries Braino's CMake
    #                             compiles from their extracted sources
    #   build/linux-firmware-*/rtl_bt/rtl8761b_config.bin
    #                             the one file board/post-build.sh copies out
    #                             of a build directory on every build; a new
    #                             one of those has to be kept here too
    # Every other package keeps only its hidden files -- the .stamp_* files
    # that tell Buildroot it is done, and its file lists -- which is all
    # Buildroot reads of a package that is not rebuilt. Braino's own build
    # directories go entirely: the next commit builds its own.
    cd "$OUT/build"
    for dir in */; do
        dir=${dir%/}
        case "$dir" in
        linux-[0-9]*|braino-tft-espi-*|braino-arduinojson-*|braino-map-n-flag-*) ;;
        braino-*) rm -rf "$dir" ;;
        linux-firmware-*)
            find "$dir" -mindepth 1 ! -name '.*' ! -path "$dir/rtl_bt" \
                ! -path "$dir/rtl_bt/rtl8761b_config.bin" -prune -exec rm -rf {} +
            ;;
        *) find "$dir" -mindepth 1 -maxdepth 1 ! -name '.*' -exec rm -rf {} + ;;
        esac
    done
    # Hidden directories inside a package (a .git, a .libs) are not stamps.
    find . -mindepth 2 -maxdepth 2 -type d -name '.*' ! -path './linux-[0-9]*' \
        ! -path './braino-*' -exec rm -rf {} +
    rm -rf "$OUT/legal-info"
    du -sh "$OUT"
    ;;
*)
    echo "usage: $0 key|trim|settle" >&2
    exit 2
    ;;
esac
