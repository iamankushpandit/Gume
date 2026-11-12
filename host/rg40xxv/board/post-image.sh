#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Assembles the card image from what Buildroot produced, then a gzip copy for
# downloading and the hashes of both. gzip rather than xz, though xz is a
# fifth smaller: every image writer reads gzip, and so does Windows itself,
# which is what host/rg40xxv/write-card.ps1 unpacks it with.
set -eu
EXT=$BR2_EXTERNAL_BRAINO_PATH

install -m 0644 "$EXT/board/extlinux.conf" "$BINARIES_DIR/extlinux.conf"
support/scripts/genimage.sh -c "$EXT/board/genimage.cfg"

cd "$BINARIES_DIR"
gzip -9 -n -k -f braino-rg40xxv.img
sha256sum braino-rg40xxv.img braino-rg40xxv.img.gz >braino-rg40xxv.sha256
ls -l braino-rg40xxv.img braino-rg40xxv.img.gz Image
