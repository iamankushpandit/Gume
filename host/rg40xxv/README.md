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
- `/etc/braino/consoles/` holds one profile per console, copied from
  `host/consoles/`: the controls on its case. Braino reads the model from the
  device tree and picks one, then decides what each control is for
  (`host/CLAUDE.md`, "Which console, and which button does what"). The build
  stops if a device tree on the card has no profile of its own.
- `braino-run` sets Braino's environment, including the gamepad mapping, and
  shows the splash only on the first start of a boot.

There is no getty, no shell on any console, no SSH, no telnet, no syslog and
no network service. The only things that ever leave the device are the four
outbound flows Braino itself declares (CLAUDE.md, "No data collection"):
wpa_supplicant and DHCP run only when Braino starts Wi-Fi, and nothing else
uses the network.

### What the network sees of the console

- **No IPv6.** The kernel is built without it (`board/linux.fragment`), so the
  console has no IPv6 address of any kind. Linux's default one is built from
  the Wi-Fi chip's hardware address, and would have travelled with Braino's
  requests to the servers they reach. `board/post-build.sh` fails the build if
  the kernel that was built has IPv6.
- **DHCP sends no client identifier, no vendor string and no hostname.** The
  request still carries the radio's hardware address, as every Wi-Fi frame
  does; that address stays on the local network.
- **wpa_supplicant knows no network of its own.** The image's
  `/etc/wpa_supplicant.conf` replaces Buildroot's, which joins any open
  network in range. Nothing is saved to it.
- **`hardware.txt` and `dmesg.txt` have anything shaped like a hardware
  address blanked**, since they are the files a bug report attaches.

To check a card after one boot, on a PC: `hardware.txt` has `== ipv6` followed
by `off: not in this kernel`, and `dmesg.txt` has no line containing
`PF_INET6`. A card from an image built before this has that line.

### Asleep

With the screen asleep Braino turns off the backlight, the display output,
Wi-Fi and three of the four cores, cuts the light ring controller's supply,
and reads the joypad every 50 ms instead of every 10 ms (the image lets the
joypad driver be polled that slowly). After 15 minutes asleep, with Deep sleep
on, it suspends to RAM and the power key wakes it. SDL is built without its
sound-card hotplug thread. Measured before the last four of those, a night on
battery lost about 6.9% an hour asleep and 4.4% suspended; muOS loses about 9%
asleep. The battery's current reading is flat under this kernel, so measure by
the percentage per hour.

## Building

In Linux or WSL (Ubuntu 24.04 works), with Buildroot 2026.08.1 unpacked in
`~/braino-rg40xxv/`:

```sh
host/rg40xxv/build.sh
```

The first build downloads about 1.5 GB of sources and takes 1–2 hours. The
result is in `~/braino-rg40xxv/out/images/`: `braino-rg40xxv.img`, the same
compressed as `.img.gz`, and `braino-rg40xxv.sha256`. Keep the build
directory on a Linux filesystem; building under `/mnt/c` is many times
slower.

`rocknix/fetch.sh` refreshes the ROCKNIX parts from a pinned commit, and
`rocknix/PROVENANCE.txt` records where every file came from and its hash.

### In CI

`.github/workflows/card-image.yml` builds the same image on GitHub's runners
whenever a pull request or a push to `dev` or `main` changes `host/`, `src/`
or `include/`, and uploads two artifacts: `braino-rg40xxv` (the `.img.gz`,
its checksums and `write-card.ps1`) and `braino-rg40xxv-sources` (the source
bundle below). The first build on a runner takes a few hours; the downloads
are cached after that.

To publish an image for testers, tag the commit and push the tag:

```sh
git tag card-rg40xxv-2026-11-12 && git push origin card-rg40xxv-2026-11-12
```

That builds the tagged commit and publishes both artifacts as a GitHub
pre-release, which the site's Experimental page links to. It is a
pre-release so that nothing takes it for the newest Braino: the old-release
installer skips pre-releases, and `release.yml` answers only to `v*` tags.

## Writing a card

Writing the image replaces everything on the card, the players' profiles and
scores included. Any microSD card of 1 GB or more will do.

**Windows.** Use `write-card.ps1`, beside this file, not an ordinary image
writer. With some card readers -- the Realtek reader built into the laptop
this image was made on is one -- **Windows writes over the start of a card
the moment it mounts it**, replacing the partition table with a copy of a FAT
boot sector, and a card that has gone through that boots U-Boot and then
cannot find its partitions. Rufus and Raspberry Pi Imager let go of the card
when they finish, which is exactly when Windows does it. The script locks
every volume on the card for the whole write, writes the first 4 MB last,
reads everything back, and keeps the locks until the card is pulled out.

```powershell
# PowerShell opened as administrator, in the folder with the download:
powershell -ExecutionPolicy Bypass -File write-card.ps1          # lists the cards
powershell -ExecutionPolicy Bypass -File write-card.ps1 -Image braino-rg40xxv.img.gz -Disk 2
```

It offers only a disk on an SD or MMC bus, or one Windows calls removable,
that is not the disk Windows runs from and is no bigger than 512 GB, and it
asks for the disk number a second time before it writes. When it says so,
take the card out without ejecting it. `-Backup <file>` saves what was on the
card first.

**Linux.** Find the card with `lsblk` (it is the disk that appears when the
card goes in), then, with `sdX` replaced by it:

```sh
gunzip -c braino-rg40xxv.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
```

**macOS.** Find the card with `diskutil list`, then, with `N` replaced by its
number:

```sh
diskutil unmountDisk /dev/diskN
gunzip -c braino-rg40xxv.img.gz | sudo dd of=/dev/rdiskN bs=4m
diskutil eject /dev/diskN
```

Check the download first: its SHA-256 is in `braino-rg40xxv.sha256`
(`Get-FileHash` on Windows, `sha256sum` on Linux, `shasum -a 256` on macOS).

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

- **The display output switching off in sleep is untested on the device.**
  If the panel ever wakes wrong, set `BRAINO_SLEEP_LCD=0` in `braino-run`.
- **HDMI is untested on the device**, and HDMI sound is not routed: ROCKNIX
  ships the H700's HDMI audio (its `ahub` sound card) disabled in this
  device tree.
- **The Wi-Fi password's Show checkbox is not reached by the d-pad** on the
  device, though it is drawn as a button. `braino-run` sets
  `BRAINO_FOCUS_LOG=1` for now, which logs the selector's list on every
  screen; remove it once that is understood.
- **Wi-Fi scans use the radio's real hardware address**, as a board's do, so
  anyone in radio range can hear it while the Wi-Fi screen scans or the
  console joins. The driver supports scanning from a random address
  (`wpa_cli mac_rand_scan all enable=1`); it is not switched on because it
  has not been tried on the device.
- **The GPL source offer is the source bundle.** Every published image has
  `braino-rg40xxv-sources.tar` beside it: Buildroot's `make legal-info` --
  every package's source, patches and licence texts, Realtek's firmware
  licence among them -- plus the Buildroot that built it. Two parts are left
  out and the bundle's `BRAINO-SOURCES.txt` says why: `linux-firmware`, of
  which the card carries three binary files with no source to offer, and the
  build machine's own tools, none of which is on the card. That leaves about
  550 MB of the 1.4 GB legal-info writes.

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
