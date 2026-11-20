#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Braino! on muOS. This file goes on the card as ROMS/Ports/Braino.sh and the
# program beside it in ports/braino/. Launch it once from the Ports list;
# from then on the console boots into Braino and nothing else.
#
# muOS runs it as root through its "External - Ports" launcher, which has
# already set the SDL environment (the controller mapping above all). Three
# things it does every time, so that they stay true even after somebody
# changes them in muOS's own menus:
#
#  - Startup becomes "Last Content". muOS relaunches whatever was played last
#    at every boot, and Braino is what was played last.
#
#  - The boot logo becomes Braino's splash, copied in again if a theme change
#    replaced it.
#
#  - muOS's safe-mode counter is cleared once Braino has run for ten seconds.
#    muOS counts every boot and clears the count only after its OWN menu has
#    run for eight; a console that never shows that menu never clears it, and
#    on the third boot muOS would skip the autostart "for safety" and show
#    the menu. Braino running is the evidence of a healthy boot here.
#
# AND BRAINO NEVER HANDS THE SCREEN BACK. To the player there is no other
# operating system: if Braino exits for any reason -- Select + Start, a crash,
# one of muOS's own kill hotkeys -- it is started again, and muOS's menu is
# never drawn, unless the console is shutting down. Two exceptions, both
# deliberate:
#
#  - A file named `braino-maintenance` at the top of the games partition
#    (create it from a PC) lets Select + Start leave for muOS's menu, for
#    updates and settings. Delete it and the console is sealed again.
#
#  - Five exits inside thirty seconds is a Braino that cannot start, not a
#    player. The loop gives up and muOS's menu appears, rather than leave a
#    black screen that looks like a dead device.

BRAINO_DIR=${BRAINO_DIR:-/mnt/mmc/ports/braino}
[ -x "$BRAINO_DIR/braino" ] || BRAINO_DIR=/mnt/sdcard/ports/braino

[ -f /opt/muos/script/var/func.sh ] && . /opt/muos/script/var/func.sh
HAVE_VARS=0
command -v SET_VAR >/dev/null 2>&1 && HAVE_VARS=1

[ "$HAVE_VARS" = 1 ] && SET_VAR config settings/general/startup last
# The light ring around the sticks is Braino's status LED now (a rainbow, and
# the firmware's colours over it). Mode 10 is muOS's "external": it keeps
# its own hands off the LEDs, low-battery red included.
[ "$HAVE_VARS" = 1 ] && SET_VAR config settings/rgb/mode 10

# BRAINO'S PRIVACY PROMISE COVERS THE WHOLE CONSOLE, OS INCLUDED. Exactly the
# firmware's own flows may leave this device (CLAUDE.md, "No data
# collection"), so muOS's network services are switched off: its web
# dashboard (which serves the card's files, saves and Wi-Fi password among
# them, to anyone on the network), mDNS, SSH, SFTP, the web terminal and
# Syncthing. Its time daemon is stopped too: it polls pool.ntp.org as often as
# every minute, and Braino sets the clock itself with the one NTP query it
# declares. The startup chime, the "waiting for network" screen and muOS's
# own two-minute dimming are Braino's business here as well.
if [ "$HAVE_VARS" = 1 ]; then
    for SERVICE in landing mdns sshd sftp ttyd syncthing; do
        SET_VAR config "web/$SERVICE" 0
    done
    SET_VAR config settings/general/chime 0
    SET_VAR config settings/advanced/retrowait 0
    SET_VAR config settings/power/idle_display 0
    # The power button turns the console off. muOS's default is its own sleep,
    # which also stops the network underneath Braino and shows nothing on the
    # screen that says why. 2 is muOS's "shut down at once"; maintenance mode
    # gets its default back.
    if [ -e "$(dirname "$(dirname "$BRAINO_DIR")")/braino-maintenance" ]; then
        SET_VAR config settings/power/shutdown 600
    else
        SET_VAR config settings/power/shutdown 2
    fi
fi
[ -e "$(dirname "$(dirname "$BRAINO_DIR")")/braino-maintenance" ] || killall chronyd 2>/dev/null

if [ -d /mnt/boot ] && [ -f "$BRAINO_DIR/bootlogo.bmp" ] &&
    ! cmp -s "$BRAINO_DIR/bootlogo.bmp" /mnt/boot/bootlogo.bmp; then
    cp -f "$BRAINO_DIR/bootlogo.bmp" /mnt/boot/bootlogo.bmp && sync
fi

export BRAINO_DATA_DIR="$BRAINO_DIR/data"
export BRAINO_FULLSCREEN=1
mkdir -p "$BRAINO_DATA_DIR"
cd "$BRAINO_DIR" || exit 1

ROM_ROOT="$(dirname "$(dirname "$BRAINO_DIR")")"
MAINTENANCE="$ROM_ROOT/braino-maintenance"
FIRST=1
EXITS=0
WINDOW_START=$(date +%s)

while :; do
    # The splash and its loading bar belong to the boot, not to a restart.
    if [ "$FIRST" = 1 ]; then export BRAINO_BOOT_SPLASH=1; else unset BRAINO_BOOT_SPLASH; fi

    # Keep the last run's log: after a crash it is the one that explains it.
    [ -f "$BRAINO_DIR/braino.log" ] && mv -f "$BRAINO_DIR/braino.log" "$BRAINO_DIR/braino.prev.log"
    "$BRAINO_DIR/braino" > "$BRAINO_DIR/braino.log" 2>&1 &
    PID=$!

    # Braino has its own screen saver and sleep; muOS's idle sleep must not
    # pull the device out from under it.
    [ -d /run/muos ] && echo "$PID" > /run/muos/idle_sleep_inhibit

    if [ "$FIRST" = 1 ]; then
        (
            sleep 10
            if kill -0 "$PID" 2>/dev/null; then
                # muOS's own "this boot was healthy", if it has one; the
                # counter it resets otherwise.
                [ -f /opt/muos/script/var/runtime.sh ] && . /opt/muos/script/var/runtime.sh
                if command -v BOOT_GUARD_CONFIRM >/dev/null 2>&1; then
                    BOOT_GUARD_CONFIRM
                elif [ "$HAVE_VARS" = 1 ]; then
                    SET_VAR system boot/attempt_count 0
                fi
            fi
        ) &
    fi
    FIRST=0

    wait "$PID"

    # muOS is shutting the console down and stopped Braino on the way. Starting
    # it again would draw over the shutdown and write to a card being unmounted.
    pgrep -f '(quit|halt)\.sh' >/dev/null 2>&1 && exit 0

    [ -e "$MAINTENANCE" ] && exit 0

    NOW=$(date +%s)
    if [ $((NOW - WINDOW_START)) -gt 30 ]; then
        WINDOW_START=$NOW
        EXITS=0
    fi
    EXITS=$((EXITS + 1))
    [ "$EXITS" -ge 5 ] && exit 1
done
