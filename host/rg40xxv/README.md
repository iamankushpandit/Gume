# Braino! for the Anbernic RG40XX V — the whole-device image

This directory builds a microSD card image on which the RG40XX V is Braino and
nothing else: no menu, no other OS behind it, no login. It replaces the muOS
card (`host/muos/`), which could not be published because it carried
Anbernic's vendor kernel (no source released) and the closed `libmali` GPU
driver. Everything in this image is built from source here, except one
redistributable firmware blob for the Wi-Fi/Bluetooth chip.

It is a [Buildroot](https://buildroot.org) external tree.

## What is on the card

| Part | Where it comes from |
|---|---|
| U-Boot 2026.01 at 8 KB, with TF-A 2.12 BL31 | upstream, plus ROCKNIX's LPDDR4 board config and two patches |
| Linux 7.2 (mainline) | kernel.org, plus ROCKNIX's H700 patches and device trees |
| The whole root filesystem, built into the kernel | Buildroot: glibc, busybox, SDL2, ALSA, libcurl + mbedTLS, wpa_supplicant, iw |
| Braino | this repository, `host/` built with `BRAINO_HOST_DRM` |
| `rtw8821c_fw.bin`, `rtl8821cs_fw.bin`, `rtl8821cs_config.bin` | `linux-firmware` (Realtek's redistributable licence) |
| The panel's init sequence | ROCKNIX (`rocknix/firmware/panels/`) |

The card has three regions: U-Boot at 8 KB, a 64 MB FAT partition (`BRAINO`)
holding the kernel and both device trees, and a 32 MB ext4 partition
(`BRAINODATA`) for Braino's settings, profiles and scores. The system runs
from RAM and only `BRAINODATA` is ever written.

### No GPU, no window system

Braino draws a 320×240 picture. `host/src/HostDrm.cpp` puts it on the panel
through the kernel's DRM driver with two dumb buffers, scaled by a whole
number, with no libdrm, no Mesa and no X or Wayland. SDL2 is still used, but
only for the gamepad, events and sound. The PC build is unchanged: the option
is off unless `BRAINO_HOST_DRM=ON`.

**HDMI.** With a screen plugged into the console's HDMI port, Braino moves
to it at the screen's own resolution (4× on a 1080p TV, centred) and the
built-in panel switches off; unplugged, it comes back. The kernel's hotplug
event is read straight from its netlink socket, so no udev is needed.

### Nothing on the screen but Braino

The kernel and init print only to the serial port (`console=ttyS0`, `quiet`)
and the kernel has no text console on the panel at all, so the screen stays
dark from power-on until Braino's splash. U-Boot draws nothing on this panel.

### What runs

`/etc/inittab` runs four scripts and Braino, and restarts Braino if it ever
exits (Buildroot's own `S40network` also runs, and brings up only the
loopback interface):

- `S01data` checks and mounts `BRAINODATA`. If it cannot be repaired it is
  formatted, because a console that keeps no setting is worse than one that
  starts over.
- `S05hardware` loads the joypad driver, sets the clock from the RTC, opens
  the codec's mixer, and powers the light ring's controller. It also writes
  `hardware.txt` (what the kernel named every LED, serial port, input, sound
  card and display connector) and `dmesg.txt` next to Braino's own log in
  `BRAINODATA/braino/`, so a card brought back to a PC explains itself. They
  stay on the card; nothing is sent anywhere.
- `S40keys` runs busybox's `acpid` for the keys that are not on the gamepad:
  power (turns the console off) and the volume keys (the codec's mixer).
- `braino-run` sets Braino's environment, including the gamepad mapping, and
  shows the splash only on the first start of a boot.

There is no getty, no shell on any console, no SSH, no telnet, no syslog and
no network service. The only things that ever leave the device are the four
outbound flows Braino itself declares (CLAUDE.md, "No data collection"):
wpa_supplicant and DHCP run only when Braino starts Wi-Fi, and nothing else
uses the network.

## Building

In Linux or WSL (Ubuntu 24.04 works), with Buildroot 2026.08.1 unpacked in
`~/braino-rg40xxv/`:

```sh
host/rg40xxv/build.sh
```

The first build downloads about 1.5 GB of sources and takes 1–2 hours. The
result is in `~/braino-rg40xxv/out/images/`: `braino-rg40xxv.img`, the same
compressed as `.img.xz`, and `braino-rg40xxv.sha256`. Keep the build
directory on a Linux filesystem; building under `/mnt/c` is many times
slower.

`rocknix/fetch.sh` refreshes the ROCKNIX parts from a pinned commit, and
`rocknix/PROVENANCE.txt` records where every file came from and its hash.

## Writing a card on Windows — read this first

With this PC's Realtek card reader, **Windows overwrites sector 0 of a card
whenever it mounts it**, replacing the partition table with a copy of a FAT
boot sector. A card that has gone through that boots U-Boot and then cannot
find its partitions. Write the image with a tool that keeps every volume on
the card locked until the card is removed, and pull the card out before the
lock is released. Rufus and Raspberry Pi Imager release it afterwards.

## Licences

Every part keeps its own licence. Buildroot's `make legal-info` collects the
licence texts and the exact sources of everything in the image, and that
bundle is what a release must offer beside the image.

- Braino: GPL-3.0-or-later (this repository).
- Linux, U-Boot, the joypad driver and ROCKNIX's patches: GPL-2.0. The patches
  and device trees in `rocknix/` are ROCKNIX's and their authors', not
  Braino's.
- TF-A: BSD-3-Clause. Busybox: GPL-2.0. glibc: LGPL-2.1. SDL2: zlib. ALSA
  lib: LGPL-2.1. libcurl: curl. mbedTLS: Apache-2.0. wpa_supplicant: BSD.
- Realtek firmware: `LICENCE.rtlwifi_firmware.txt` in `linux-firmware`
  (redistributable, binary only).

### Before the first public release

- **HDMI is untested on the device**, and HDMI sound is not routed: ROCKNIX
  ships the H700's HDMI audio (its `ahub` sound card) disabled in this
  device tree.
- **The Wi-Fi password's Show checkbox is not reached by the d-pad** on the
  device, though it is drawn as a button. `braino-run` sets
  `BRAINO_FOCUS_LOG=1` for now, which logs the selector's list on every
  screen; remove it once that is understood.
- **The GPL source offer.** Publish Buildroot's `make legal-info` output
  beside every image: every source, patch and configuration that built it,
  and the licence texts, Realtek's firmware licence among them.

- **The panel files' licence.** `anbernic,rg40xx*-panel.panel` come from
  ROCKNIX's tree, which states no licence for them. Confirm it with ROCKNIX.
- **Which panel.** Newer RG40XX V units have a second panel revision with its
  own device tree (`...-v2-panel.dtb`). The card boots the first; detecting
  the panel is not done yet.
- **DDR3 units.** U-Boot is built for LPDDR4, as the RG40XX V is documented.
  ROCKNIX also ships an LPDDR3 build for H700 units that need it.
- **SD cards.** One 128 GB card tested here fails writes under the mainline
  kernel while working under muOS; another works. Test the cards owners use.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
