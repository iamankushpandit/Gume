#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Copies the parts of ROCKNIX's H700 support that the RG40XXV image needs into
# this directory, from one pinned ROCKNIX commit. Run it again with a new
# commit to move forward; the diff then shows exactly what ROCKNIX changed.
#
# Everything this fetches keeps its own upstream licence and authorship (the
# kernel and U-Boot patches are GPL-2.0, TF-A's are BSD-3-Clause); none of it
# is Braino's, and PROVENANCE.txt says where each file came from.
set -eu

COMMIT=2e37b9a5e235765d2b470a8d163195c552bcbf9f
RAW=https://raw.githubusercontent.com/ROCKNIX/distribution/$COMMIT
HERE=$(cd "$(dirname "$0")" && pwd)

get() { # get <path in ROCKNIX> <destination relative to here>
    mkdir -p "$HERE/$(dirname "$2")"
    curl -fsSL "$RAW/$1" -o "$HERE/$2"
    printf '%s  %s  <- %s\n' "$(sha256sum "$HERE/$2" | cut -c1-64)" "$2" "$1" >>"$HERE/PROVENANCE.txt.new"
}

: >"$HERE/PROVENANCE.txt.new"

# Linux 7.2: the version-wide fixes ROCKNIX carries, minus the Qualcomm ones.
for p in 9998-silence-initramfs-unpack-warn.patch 9999-fix-rust-build-error.patch; do
    get projects/ROCKNIX/packages/linux/patches/7.2/$p linux/1-7.2/$p
done
# ROCKNIX's "mainline" set: PWM period helper and the joypad keycode redirect.
for p in 0003-pwm-add-pwm_set_period.patch 0004-input-adc-keys-redirect-keycode-316-to-rocknix-joypa.patch; do
    get projects/ROCKNIX/packages/linux/patches/mainline/$p linux/2-mainline/$p
done
# The H700 device set, in ROCKNIX's order.
for p in \
    0002-rg35xx-enable-HDMI-LCD.patch \
    0003-Update-sun8i_tcon_top.c.patch \
    0007-rg35xx-add-GPU-opp.patch \
    0008-v8_20260804_james_hilliard1_introduce_allwinner_h616_pwm_controller.patch \
    0010-rg35xx-enable-pwm-backlight.patch \
    0040-Revert-usb-musb-Fix-hardware-lockup-on-first-Rx-endp.patch \
    0110-v2_20250226_kikuchan98_drm_panel_add_generic_mipi_panel_driver.patch \
    0111-rg35xx-2024-use-panel-mipi-dpi-spi-driver.patch \
    0124-battery-name.patch \
    0126-20241018_macroalpha82_rg35xx_add_additional_hardware_support.patch \
    0127-enable-mmc1-for-RG35XX-2024.patch \
    0140-rg35xx-2024-use-rocknix-joypad-driver.patch \
    0144-Update-sun50i-h700-anbernic-rg35xx-h.dts.patch \
    0150-add-forcefeedback.patch \
    0151-phy-fix-OTG-host-mode.patch \
    0152-rg35xx-2024-enable-usb-otg.patch \
    0153-enable-rgb-leds.patch \
    0154-rocknix-dt-id.patch \
    0155-sun4i-set-rgb-connector-as-DSI.patch \
    0156-rg35xx-plus-add-pcf8563-rtc.patch \
    0157-input-axp20x-pek-do-not-report-the-key-press-that-woke-the-system.patch \
    0158-mmc-sunxi-mmc-mask-data0-when-updating-the-clock-on-h616-emmc.patch \
    0159-arm64-dts-rg35xx-2024-keep-the-second-sd-card-powered.patch \
    0160-phy-sun4i-usb-disable-the-clock-that-was-enabled.patch \
    0161-drm-sun4i-gate-the-display-pipeline-across-system-suspend.patch \
    0162-pinctrl-sunxi-release-the-lcd0-pads-while-the-panel-is-off.patch \
    0163-asoc-sun4i-codec-suspend-the-codec-and-switch-the-amplifier-supply.patch \
    0200-h700-update-opps.patch \
    0204-dts-Enable-hdmi-sound.patch \
    0210-h616-cpu-opp-mark-480MHz-as-suspend-opp.patch \
    0901-dts-rg35xx-2024-fix-led-1-color.patch; do
    get projects/ROCKNIX/devices/H700/patches/linux/$p linux/3-h700/$p
done

# ROCKNIX's own device trees for this console (they replace the in-tree ones).
for d in sun50i-h700-anbernic-rg40xx-v.dts sun50i-h700-anbernic-rg40xx-v-v2-panel.dts; do
    get projects/ROCKNIX/devices/H700/linux/dts/allwinner/$d dts/$d
done

# The panel's init sequence, loaded by the generic MIPI panel driver.
for f in 'anbernic,rg40xx-panel.panel' 'anbernic,rg40xx-v2-panel.panel'; do
    get "projects/ROCKNIX/packages/linux-firmware/kernel-firmware/extra-firmware/panels/$f" "firmware/panels/$f"
done

# ROCKNIX's whole H700 kernel config, the starting point for ours.
get projects/ROCKNIX/devices/H700/linux/linux.aarch64.conf linux/rocknix-h700.config

# U-Boot v2026.01: the LPDDR4 board config and its two patches.
get projects/ROCKNIX/devices/H700/packages/u-boot-DDR4/sources/configs/anbernic_rg35xx_h700_lpddr4_defconfig uboot/anbernic_rg35xx_h700_lpddr4_defconfig
for p in 0001-Update-dram_sun50i_h616.c.patch 0002-sunxi-axp717-charge-only-boot.patch; do
    get projects/ROCKNIX/devices/H700/packages/u-boot-DDR4/patches/$p patches/uboot/$p
done

# TF-A v2.12.0 for sun50i_h616: system suspend, off unless built with
# SUNXI_SYSTEM_SUSPEND=1. ROCKNIX's 000-use-gcc-for-asm.patch is left out on
# purpose: it assembles with $(CC), which ROCKNIX's build sets and Buildroot's
# does not, so under Buildroot every .S file became the command "-x ...".
for p in 001-allwinner-h616-psci-system-suspend.patch; do
    get projects/ROCKNIX/packages/tools/atf/patches/H700/$p patches/arm-trusted-firmware/$p
done

# The controller mapping ROCKNIX ships for the joypad driver's "H700 Gamepad".
curl -fsSL "$RAW/projects/ROCKNIX/packages/apps/gamecontrollerdb/config/gamecontrollerdb.txt" |
    grep '^1900f6a24b480000df14000000010000,H700 Gamepad,' >"$HERE/gamecontroller.txt"
printf '%s  %s  <- %s\n' "$(sha256sum "$HERE/gamecontroller.txt" | cut -c1-64)" gamecontroller.txt \
    "projects/ROCKNIX/packages/apps/gamecontrollerdb/config/gamecontrollerdb.txt (the H700 Gamepad line)" >>"$HERE/PROVENANCE.txt.new"

{
    echo "Fetched by fetch.sh from https://github.com/ROCKNIX/distribution at commit $COMMIT."
    echo "Each file keeps its upstream licence and authorship; none of it is Braino's."
    echo
    cat "$HERE/PROVENANCE.txt.new"
} >"$HERE/PROVENANCE.txt"
rm -f "$HERE/PROVENANCE.txt.new"
echo "fetched $(grep -c '<-' "$HERE/PROVENANCE.txt") files"
