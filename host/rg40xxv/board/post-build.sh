#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Runs on the finished root filesystem before it is packed into the kernel:
# keeps only the firmware this console uses, adds the panel's init sequence
# and the controller mapping, and drops the init scripts of services Braino
# does not have.
set -eu
TARGET=$1
EXT=$BR2_EXTERNAL_BRAINO_PATH

# Firmware: the RTL8821CS Wi-Fi and Bluetooth, and the regulatory database.
FW=$TARGET/lib/firmware
KEEP="rtw88/rtw8821c_fw.bin rtl_bt/rtl8821cs_fw.bin rtl_bt/rtl8821cs_config.bin regulatory.db regulatory.db.p7s"
if [ -d "$FW" ]; then
    find "$FW" \( -type f -o -type l \) | while read -r f; do
        case " $KEEP " in
            *" ${f#"$FW"/} "*) ;;
            *) rm -f "$f" ;;
        esac
    done
    find "$FW" -depth -type d -empty -delete
fi
# The Bluetooth half's config. linux-firmware's WHENCE makes it a link,
# "rtl_bt/rtl8821cs_config.bin -> rtl8761b_config.bin", which Buildroot's
# file list does not install; btrtl asks for it by that name.
install -D -m 0644 "$BUILD_DIR"/linux-firmware-*/rtl_bt/rtl8761b_config.bin "$FW/rtl_bt/rtl8821cs_config.bin"
mkdir -p "$FW/panels"
cp -f "$EXT"/rocknix/firmware/panels/*.panel "$FW/panels/"

# SDL's mapping for the joypad driver's "H700 Gamepad".
install -D -m 0644 "$EXT/rocknix/gamecontroller.txt" "$TARGET/etc/braino/gamecontroller.txt"

# The console profiles: which controls each console has. Braino picks one at
# start-up from the device tree (host/src/HostConsole.h). Every device tree
# on this card must have a profile of its own -- a console that boots here
# and then falls back to the family's profile has lost its stick, and nobody
# would have been told.
CONSOLES=$EXT/../consoles
mkdir -p "$TARGET/etc/braino/consoles"
install -m 0644 "$CONSOLES"/*.conf "$TARGET/etc/braino/consoles/"
for dts in "$EXT"/rocknix/dts/*.dts; do
    model=$(sed -n 's/^[[:space:]]*compatible = "\(anbernic,[^"]*\)", "allwinner,.*/\1/p' "$dts" | head -n 1)
    [ -n "$model" ] || continue     # a variant that includes another's tree keeps its name
    if ! grep -qs "^match = $model[[:space:]]*$" "$CONSOLES"/*.conf; then
        echo "post-build: no profile in host/consoles/ has \"match = $model\" (${dts##*/})" >&2
        exit 1
    fi
done

# No logging daemons, no network services: the overlay's scripts are the
# system, with S40network bringing up only the loopback interface.
for s in S01syslogd S01seedrng S02klogd S02sysctl S10mdev S11modules S15watchdog S41ifplugd S50crond S50telnet S90httpd; do
    rm -f "$TARGET/etc/init.d/$s"
done
