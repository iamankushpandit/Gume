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

**HDMI.** With a television plugged into the console's mini HDMI port
(engraved "HD"), Braino moves to it and the built-in panel goes dark, lamp
and all; unplugged, the picture comes back to the panel. The TV gets the
panel's own 640x480 picture at a whole scale, centred -- 2x on a 1080p TV --
so nothing is laid out again and nothing is stretched. Why instead of
alongside: the room is looking at the TV and the console in the player's
hands is the controller, and a second picture would cost a second
conversion of every frame and the panel's lamp for something nobody watches.

- **The layout is always the panel's.** A TV plugged in before power-on is
  shown, but Braino still lays out on the panel's mode, so pulling the TV out
  never leaves a frame too big for the panel.
- **The TV's preferred mode is used up to 1920x1080**; a 4K TV is driven at
  its largest progressive mode up to that (`host/src/HostTvMode.h`, tested by
  `test/host/tv_mode_test.cpp`). Every buffer the display engine scans has to
  be physically contiguous, and two at 4K would be 66 MB; the boot line asks
  for 64 MB of contiguous memory (`cma=64M` in `board/extlinux.conf`, against
  the kernel's 32 MB) for two at 1080p beside the kernel's own. A TV whose best
  mode is smaller than the panel is ignored.
- **Sleep turns off whichever output is showing**, and a TV unplugged while
  the console sleeps finds the panel on waking.
- **Nothing else draws on the TV.** The kernel has no text console, U-Boot
  has no video output on this chip, and the kernel's fbdev emulation, which
  has no console to show, holds a black frame until Braino starts.
- **The sound stays on the console** (see below). The kernel's hotplug event
  is read straight from its netlink socket, so no udev is needed.

**Headphones.** With headphones in the 3.5 mm jack the speaker is silent and
its amplifier is powered down; pulled out, the speaker is back. The kernel
already reads the jack -- the device tree gives the codec its detect line
(`hp-det-gpios`, PI3), and `sun4i-codec` reports it as a `Headphone Jack`
control and an input switch -- but both outputs hang off the codec's one line
output and the kernel never takes the speaker away. `acpid` hears the switch
and runs `/usr/bin/braino-headphones in|out`, which turns the codec's
`Speaker` pin off or on; `S40keys` runs it once at boot with `sync` for a
plug that was already in. Volume and Mute are the same on both: Braino sets
its own level in software, and the volume keys move the line output that
feeds both.

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
  the codec's mixer, and powers the light ring's controller. It rebinds the
  Bluetooth driver once the Wi-Fi chip is powered, because the driver probes
  before the chip has power and no `hci0` appears otherwise (not yet
  confirmed on the device). It also writes
  `hardware.txt` (what the kernel named every LED, serial port, input, sound
  card and display connector, each connector's modes, and what the headphone
  jack read at boot) and `dmesg.txt` next to Braino's own log in
  `BRAINODATA/braino/`, so a card brought back to a PC explains itself. They
  stay on the card; nothing is sent anywhere.
- `S40keys` runs busybox's `acpid` for the keys that are not on the gamepad:
  power (turns the console off) and the volume keys (the codec's mixer). It
  also hears the headphone jack, which moves the sound between the speaker
  and the jack (`/usr/bin/braino-headphones`), and sets the speaker from the
  jack as it is at boot.
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
when a pull request or a push to `dev` or `main` changes something the image
is built from -- `host/` (not its Markdown, not the muOS packer), `src/`,
`include/`, the product mark, the licences -- and uploads two artifacts:
`braino-rg40xxv` (the `.img.gz`, its checksums and `write-card.ps1`) and
`braino-rg40xxv-sources` (the source bundle below). A change to the README, to
an ESP32 board or to the site starts nothing. Those paths are
`TRIGGER_PATHS` in `tools/card_inputs.py`, and `tools/check_card_inputs.py`
fails if the workflow's copy of them drifts.

**Most runs build only Braino.** A full build -- toolchain, glibc, kernel,
U-Boot, every library -- takes 50 to 80 minutes on a runner (measured on
every run from 12 to 13 November 2026), and a change to a game touches none of
it. So a full build on `dev` or `main` stores its output as an *OS base*:
`os-base.sh trim` cuts the 17 GB build directory to the 1.3 GB (compressed) a
later build reads -- the toolchain, the root filesystem, the kernel's tree, the
Arduino libraries Braino compiles, and every other package's `.stamp_*` files
-- and it is kept as a 90-day artifact named after `os-base.sh key`, a hash of
everything the OS is built from. A later run with the same hash unpacks it,
runs `os-base.sh settle`, and runs the same `build.sh`: Braino's build
directory is named after its commit, so Buildroot builds it afresh, then
repacks the root filesystem and relinks the kernel around it, and nothing
else. On a runner that whole job is about two and a half minutes, the build
itself 78 seconds, against an hour; in WSL, from a trimmed base to the
finished image, 2 minutes 53 seconds, with `braino` the only package built. `settle` dates the OS's own files in the
checkout before the base's stamps, because a fresh checkout dates them now,
and make takes a fragment newer than its stamp as a change to rebuild from
sources the base no longer has.

**What counts as the OS is decided in one place, `tools/card_inputs.py`**,
which `os-base.sh` asks for the key and for the files to settle;
`python3 tools/card_inputs.py explain` prints every file here with its kind,
and `python3 tools/card_inputs.py needs <path>...` says whether a change to
those paths is a full build. Everything in this directory is OS, and its
contents are hashed, except:

- **the overlay**, whose file *names* are hashed and whose contents are not.
  Buildroot copies the overlay into the root filesystem on every build, so
  editing an init script, `acpid`'s map or `braino-run` takes minutes. Adding
  or removing a file is a full build: a removed file would otherwise survive
  in the base's root filesystem, and a new one usually comes with an OS change
  beside it.
- **what every build applies afresh**: Braino's package, `post-image.sh`, the
  boot menu and the card layout, which write only into `images/`.
- **this README and `write-card.ps1`**, which are not in the image.

`post-build.sh` is OS on purpose: it deletes from the root filesystem
(firmware blobs, init scripts), and a base's root filesystem has already lost
whatever it deleted. The defconfig, the packages, the kernel and busybox
fragments, ROCKNIX's patches and device trees, `fetch.sh`'s pinned commit and
`build.sh` (which names the Buildroot release) are all OS. Change one and the
hash moves, no base matches, and the run builds everything; on `dev` or
`main` it then stores the new base. Things to know:

- **Only a base stored by a push to `dev` or `main` of this repository is
  used.** A base goes into published images, and a pull request runs code
  nobody has reviewed yet. Pull requests build on bases and never store one,
  so a pull request that changes the OS builds everything on each push, and
  its merge builds everything once more to store the base.
- **A push to `dev` or `main` is never cancelled by the next one**, only
  queued behind it. The full build after an OS change is the one that stores
  the base; cancelled, the next push would find none and build in full again.
  Pull request runs are cancelled by a newer push.
- **A base does not expire while it is used.** A push that finds one over 60
  days old stores the same bytes again, which starts its 90 days over. The
  weekly full build (Mondays, from the default branch's copy of the workflow,
  so only once `card-image.yml` is on `main`) proves the OS still builds from
  source and stores a fresh base too.
- **A file Braino's package stops installing stays on a card built on an old
  base.** Bump `BASE_EPOCH` in `tools/card_inputs.py` when that matters, and
  the next run builds everything. A manual run of the workflow with *Build
  everything from source* ticked does the same once.
- **A post-build step that reads a package's build directory needs that file
  kept by `os-base.sh trim`.** `rtl8761b_config.bin` is the one there is; a
  build on a base fails at once, not quietly, when one is missing.

The run's summary says which it did and why: "Braino on a stored OS base" with
the base's run and age, or "Full build" with the reason.

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

- Braino: GPL-3.0-or-later (this repository). Compiled into its binary: GNU
  FreeFont's FreeSans, cut to Latin (`host/fonts/`, GPL-3.0-or-later with the
  font exception; its `COPYING` is in Braino's legal-info), and
  stb_truetype (`host/third_party/stb/`, public domain or MIT, its text at the
  end of the header). Both are in Braino's source, so the source offer covers
  them.
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
- **HDMI and the headphone jack are untested on the device.** Both are
  wired as described above, from the kernel and device tree that are built,
  but no television or headphones have been plugged into a console running
  this image yet. One thing only a console can answer: the board routes the
  line output to the speaker and the jack through a 74HC4052 analog switch,
  and if the speaker's amplifier enable also selects that switch, headphones
  would go quiet with the speaker. `hardware.txt`'s `== headphone jack` says
  whether the detect line reads at all.
- **No sound over HDMI.** ROCKNIX's `0204-dts-Enable-hdmi-sound.patch` turns
  on device-tree nodes for the H700's audio hub (`ahub1_plat`,
  `ahub1_mach_hdmi`), but their compatibles (`allwinner,sunxi-snd-plat-ahub`,
  `allwinner,sunxi-snd-mach`) belong to Allwinner's vendor kernel, and
  mainline Linux 7.2 has no driver for either, so nothing binds and there is
  no HDMI sound card. The HDMI controller's side is there
  (`CONFIG_DRM_DW_HDMI_I2S_AUDIO`, `CONFIG_SND_SOC_HDMI_CODEC`). What it would
  take is a mainline ASoC driver for the H616/H700 audio hub -- its I2S/TDM
  block and the routing that feeds the HDMI controller's I2S input -- as a
  kernel patch of our own, plus a device-tree overlay pointing those nodes at
  it, and then a choice in Braino of which card to open. That is a driver
  port, not configuration; until someone writes or upstreams one, the sound
  stays on the console's speaker or headphones.
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
