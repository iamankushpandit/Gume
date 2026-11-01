# host/

Braino! as a desktop program. The firmware under `src/` is compiled
**unchanged** against small stand-ins for the ESP32 libraries, and an SDL
window plays the part of the panel, the touch controller, the BOOT key and
the speaker. The first use is seeing and playing the real screens on a PC;
the target it is building towards is the Anbernic RG40XXV (Linux, 640x480,
gamepad), where it would ship as a PortMaster port.

```bash
# In Linux or WSL, with build-essential, cmake, pkg-config, libsdl2-dev
# and libcurl4-openssl-dev:
cmake -S host -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host -j
./build/host/braino
```

A mouse click is a finger on the glass. With a gamepad, the left stick moves a
crosshair, the d-pad (the arrow keys) moves a selector from control to control,
and A (Enter, Space) presses under whichever is showing; see "The d-pad's
selector" below. B and Start (Escape, Backspace) are the BOOT key, so they go
Home. Y or L1 (PageUp) and X or R1 (PageDown) press the screen's own Prev and
Next buttons, so they turn the launcher's pages and every other pager drawn with
`Ui::drawPagerButton()`; the panel shim learns where those are from the labels
it draws, and on a screen without one the keys do nothing rather than press
whatever sits in that corner. Pushing the cursor against the left or right side
of the screen does the same after a 250 ms dwell, and again every 700 ms while
it is held there; the dwell is so that a cursor swept across to a tile by the
edge does not turn the page under it. Pushing the cursor against the top or bottom of a
list that has a scroll bar scrolls it: the cursor stays at the edge and the host
drags the list's scroll track as a finger would, starting 20px inside the list
because a grab within `TOUCH_HIT_SLOP` of a tab strip presses the tab. The
panel shim recognises `RowList`'s scroll bar by how it is drawn, and its thumb
says when there is nothing more to scroll, so the cursor then carries on past
the edge. Not in portrait, where the list's up and down are the frame's left
and right. M (SDL's Guide button; the M key) moves to the next tab on a screen
with a tab strip, wrapping round -- the shim recognises `Ui::drawTab()` by its
radius-6 fill and 7px square-off, and the selected tab by its extra height --
and on a screen with none opens Settings. R2 (L) locks and L2
(K) mutes, by tapping the header's own gear, padlock and speaker: the panel
shim recognises each by the primitives `Ui` draws it with, inside the rect
`LauncherLayout` gives it in the top bar or the launcher header, so on a screen
that has not drawn one the button does nothing. A button SDL reports but
nothing maps is logged once (`[host] button N is not mapped`), which is how to
map a handheld whose M is not Guide. Holding Select + Start for three seconds quits -- long, so that nobody
leaves by accident on a handheld where Braino is all there is. F10 or closing
the window quits at once. `BRAINO_FULLSCREEN=1` fills
the screen. Settings and scores go in `$BRAINO_DATA_DIR`, or
`~/.local/share/braino`.

## The d-pad's selector

The firmware has no idea what a selected control is: every screen takes one
touch point. So the selector is the host's, and pressing A is an ordinary touch
at the middle of the selected control. No screen changed for it.

`src/HostTftFocus.cpp` reads the controls off the panel as they are drawn: a
round fill and an outline of the same rectangle, which is how `Ui::drawButton()`
ends and so every button, launcher tile and keyboard key. `src/HostFocus.h` is
where the selector goes next, and is pure arithmetic. It is drawn over the
window like the crosshair, never into the panel's frame.

- **A button has a shadow; a card does not, and that is the whole test for what
  gets a stop.** A fill and outline with no shadow might be a control (Memory's
  cards, Cinnamon's pads, Ludo's seats) or something to look at (a quiz's
  question box, Color Mix's two colours). Beside buttons, one to three cards
  were always the second kind, so they get no stop, and from the fourth they are
  a grid of controls. That is measured off every screen by opening each with a
  d-pad press and photographing it (`BRAINO_SHOTS` logs how many controls it
  found), so a new screen needs the same look.
- **A board drawn as one picture has nothing to select.** Tic-Tac-Toe, Whack A
  Mole, Piano, Maze, the Elements table, the squares in Chess, Go, Sea Battle
  and Backgammon, Microku's cells and the tracing games work the square out from
  where the finger landed, so nothing marks a cell, and the stick's crosshair is
  the way onto them. The buttons around them still take the selector.
- **The first press only brings it up**, on the control nearest the crosshair,
  because nobody has seen where it is yet. Pushing the stick puts it away.
- **Past the end of a row is the next page and past the end of a column is the
  rest of the list**, through the same Prev and Next buttons and scroll drag the
  crosshair uses. Holding down on a list with nothing to select, like System
  Info's, scrolls it too.
- **The header's Home, padlock, speaker and gear are stops as well**, found the
  way M, R2 and L2 find them (`iconTarget()`, Home added for this). Up from the
  top of a list with more above it scrolls the list before it reaches them, and
  going past the end of the header's row turns no page.

## The one rule: host/ adapts to src/, never the reverse

There is no `#ifdef` for the host anywhere under `src/`, and there must not
be. Everything the firmware does on a board it does here, through the same
code: storage goes through `Board` into Preferences, touch goes through
`BoardTouch.cpp`'s capacitive path and `mapTouch()`, sound goes through the
synthesiser and its `braino-audio` task. A host build that needed `src/` to
bend would be testing a different program from the one on the device.

What the host *replaces* is the short list in `CMakeLists.txt`'s exclusion
regex: the hardware watchdog and the codec/DAC backend. Their host versions are in `src/Host*.cpp`, and each says why.

## How it fits together

| File | Stands in for |
|---|---|
| `boards/host.h` | A board profile whose every pin is **virtual**. It is deliberately not in `include/boards/`, which would make it a claim of support that `check_boards.py` and the web installer would hold it to. |
| `shim/` | Arduino, TFT_eSPI, Preferences, NVS, FreeRTOS, Wire, I2S, Wi-Fi, SD and the ESP-IDF calls `src/` makes -- **only** what `src/` calls. A new platform call in `src/` breaks this build until a shim answers it, which is the intended way to find out. |
| `src/HostTft.cpp` | The panel: one 320x240 RGB565 frame in rotation 1's orientation, the four rotation transforms `Board::pollTouch()` undoes, and TFT_eSPI's text rules (datums, ASCII-only fonts 2 and 4, background cells, unloaded fonts drawing nothing). The font tables are TFT_eSPI's own, fetched at the pinned version and never copied into this tree. |
| `src/HostWire.cpp` | An emulated FT6336U answering BoardTouch's one transaction with the pointer, in the panel's native portrait frame. |
| `src/HostNvs.cpp` | One typed key-value store behind both `nvs_*` and Preferences, with IDF's type codes and its refusal of mismatched reads and 16-character keys. Saved whole to `nvs.txt` after every change. |
| `src/HostAudio.cpp`, `HostI2s.cpp` | The codec backend: an SDL queue kept about as deep as the codec board's DMA, so the audio task is paced the way it is on hardware. |
| `src/HostWifiOs.cpp`, `HostNet.cpp` | Wi-Fi through the OS's network manager; UDP sockets and libcurl for the firmware's own requests. |
| `src/HostPlatform.cpp` | The window, input, cursor and test aids. |

## Wi-Fi is the computer's radio; the requests are still the firmware's

`src/HostWifiOs.cpp` drives the real radio through whichever manager owns it:
NetworkManager (`nmcli`) on desktop Linux, wpa_supplicant (`wpa_cli`) on muOS
and most handheld images. A scan lists what the OS sees, `begin()` asks the
OS to join, and `status()` reports what the OS has. Link state comes from the
kernel and a once-a-second poller, so the per-frame `status()` never waits
on a subprocess. With no wireless interface (WSL, a wired desktop) the radio
is absent and the firmware sees `WL_NO_SHIELD`.

Three things about it are load-bearing:

- **The shims answer; they never originate.** `WiFiUDP` is a real socket and
  `HTTPClient` is libcurl, but the only traffic is the firmware's own three
  requests, behind its own `WL_CONNECTED` checks: NTP, the timezone lookup and
  the update manifest. Nothing goes out until a network has been joined from
  the firmware's Wi-Fi screen, because time sync needs saved credentials and
  the update check needs time sync. **A shim must not add a request, a header
  or a field.** libcurl sends no User-Agent unless told to, and it is told
  nothing. That keeps `check_privacy.py`'s promise about the manifest request
  true here.
- **While the firmware has asked for a network, `status()` answers for that
  network.** WifiApp saves the typed password once `status()` says
  connected, so reporting whatever else the computer is on would save the
  wrong password against the right name.
- **Every subprocess gets an argument vector and no shell.** An SSID is text
  a stranger chose. With wpa_supplicant, the SSID goes in as hex so a quote
  cannot end it. With nmcli, the password is an argument, which another local
  user could read from the process table for the moment nmcli runs: nobody,
  on a handheld.

`mode(WIFI_OFF)` does not turn the computer's radio off: the OS owns its
power. With NetworkManager, joining saves the network in the OS's own
configuration, as its Wi-Fi menu would. With wpa_supplicant -- the RG40XXV --
it does not: no `save_config`, and `scrubSavedNetworks()` zeroes and strips
any `network={}` block an older build left in `/etc/wpa_supplicant.conf`.
Braino joins from its own settings on every start instead.

**The Wi-Fi name and password and the admin PIN are sealed in `nvs.txt`**
(`HostSecret.h`, called from `HostNvs.cpp`'s `save()` and `load()`, so the
firmware reads and writes them exactly as on a board). On a handheld that file
is on a microSD card any laptop opens; a board's flash takes tools to read,
and this is what keeps the two comparable. The key is HMAC-SHA256 over the
processor serial (sunxi chip id, SID, device-tree serial or `/proc/cpuinfo`,
whichever answers first), read every start and never stored or logged -- the
log names only the source. Values are a random nonce, an HMAC keystream and an
encrypt-then-MAC tag bound to the value's name, so a value altered or copied
to another key fails to open and is forgotten. A file with readable values is
rewritten sealed on load, the old copy zeroed in place first. Handheld builds
(`BRAINO_HOST_STRICT_SECRETS`, on when cross-compiling) accept only a
processor-bound key; with none, the Wi-Fi network stays in memory for the run
and the PIN stays readable, because an unsaved PIN falls back to the public
default. A desktop build may fall back to `/etc/machine-id` and says so. The
limit, stated in the README: someone with the whole console and the source
can derive the key.

`esp_read_mac()` returns random bytes, made once and kept as
`<data dir>/radio-id`, and never the computer's own MAC, and `WiFi.macAddress()`
answers `--`: the identifier rule applies to the computer as much as to a
board. Kept, so the beacon's device id is stable and peers' names for this
console survive a reboot.

## Bluetooth: the firmware's beacon and scanner, on a raw HCI socket

`src/hal/BleBeacon.cpp` and `BleScanner.cpp` compile unchanged.
`shim/NimBLEDevice.h` answers the dozen NimBLE calls they make, and
`src/HostNimBLE.cpp` puts them on the controller through Linux's own
Bluetooth socket API. There is no BlueZ library, and nothing asks bluetoothd
for anything. Each call maps to one legacy LE command: random address,
advertising parameters (`ADV_NONCONN_IND`), the firmware's 31-byte payload
verbatim, advertise enable, and a passive scan whose reports go to the
scanner's own callback. So Nearby, pokes, finds and two-console games
interoperate with Braino boards byte for byte.

**It advertises from a random static address** made from the kept radio id,
never the controller's factory address, which a board does broadcast. It
still needs root (true under muOS) and a controller the OS has powered.
Without one, the beacon logs why and Nearby sees an empty room.

**The controller outlives the program.** It keeps advertising after a crash,
and it refuses new advertising parameters (`HCI 0x2006 refused (status 0x0c)`)
until told to stop, so `NimBLEDevice::init()` switches advertising and scanning
off before anything else. Without that, Nearby sits on "Radio starting" after
any restart that was not clean, with the old payload still on the air. A clean
exit calls `NimBLEDevice::deinit()`, which takes the beacon off the air and
joins the reader thread.

## The light ring

The host board profile declares three virtual RGB pins, so the firmware's
own LED code drives `src/HostLed.cpp` unchanged. On the RG40XXV that is the
ring around each stick, behind an MCU on `/dev/ttyS5`; the protocol comes
from muOS's `script/device/rgb.sh`. When the firmware has a colour lit, the
ring shows it: green for right, red for wrong, the saver's rally colours.
Otherwise it shows a rainbow, as the hardware does out of the box. It is
dark when the player switches the LED off in Settings, and while the screen
sleeps. `BRAINO_LED_RAINBOW="brightness,speed"` tunes the rainbow. The
launcher sets muOS's LED mode to "external", so muOS keeps its hands off,
low-battery red included. `BRAINO_WIFI_IFACE` names the interface
outright, for a device with two radios.

The ring is also a battery gauge (`src/HostRingGauge.h`), up for as long as
the charger is in and for ten seconds after it comes out or a stick is pushed,
screen off included. How far
round it is lit is the percentage, along red, orange at half and green at full.
With the charger in, the ring breathes twice in the percentage's colour first.
Then the charged part holds still and the part yet to charge flashes in its own
colours, under one flash a second. Pulling the charger
opens with a mood instead: red pulses below 50%, deliberately under three a
second because it is saturated red, one slow orange breath from 50%, and a
green light running round the ring at 100%. Pushing a stick
needs more than half its travel, once per push, because a stick drifts. With
the screen on only the right stick asks, since the left is the cursor. Without
the charger, a colour the firmware has lit comes before the gauge when the
screen is on. With it the gauge comes first, the saver's colours and the green
and red for answers included, because from here they all look the same. The LED
switch in Settings turns the gauge off too. Plugged in is read from the charger
supply's `online`, not the battery's `status`, which has said Full at 4067 mV.

## Sleep turns the lamp off

On a board, sleep drives the backlight PWM to zero. Here the PWM is only a
brightness multiplier on the window, so a sleeping Braino used to draw black
frames under a fully lit backlight, which is the largest drain on a handheld.
`src/HostScreenPower.cpp` now follows the panel shim's Sleep In and Sleep Out
and switches the real lamp: `/dev/disp`'s backlight ioctls on the H700, or
`/sys/class/backlight` elsewhere. The log names which one it found (`[display]
host: backlight through ...`). While asleep the window presents one black
frame and then nothing until it wakes.

Any button wakes it, and does nothing else: the press becomes a 200ms BOOT
press, which `View::Asleep` takes as it takes a touch, and nothing reaches the
screen underneath. The stick does not wake it, because a stick drifts. A run
that dies with the lamp off leaves `<data dir>/backlight-off` holding the level
to return to, and the next start restores it. Fatal signals restore it on the
way out, and SIGTERM and SIGINT go through SDL's quit, which restores it too.
The module is built only with `BRAINO_HOST_SCREEN_POWER` (cross-compiled builds
by default), so a desktop build never touches the computer's own backlight.

## The power button turns the console off

Braino does not read the power button: SDL's gamepad layer does not report it,
and muOS's `muhotkey` owns it. By default muOS answers with its own sleep
(`suspend.sh`), which stops the network underneath Braino without telling it.
So `muos/Braino.sh` sets `settings/power/shutdown` to 2, muOS's "shut down at
once", and stops its restart loop when it sees muOS shutting down. Maintenance
mode gets muOS's default of 600 back. `reviveRadio()` in `src/HostWifiOs.cpp`
restarts the radio if anything else stops it, so a scan cannot come back empty
without saying why (`[wifi] host: scan found N`).

## Seeing a screen without anyone at the window

Two environment variables, both off by default:

- `BRAINO_SHOTS="2500,5000"` saves the panel frame as
  `<data dir>/shot-<ms>.bmp` at those milliseconds after start. F12 saves one
  on demand.
- `BRAINO_TAPS="3000:237,223;4500:80,125,200"` presses the panel at frame
  coordinates `x,y` at that time, for 120 ms or the given hold. `4000:next`
  and `4000:prev` press the page keys instead, and `4000:down,3000`,
  `4000:up`, `4000:left` and `4000:right` hold the d-pad (for 1000 ms unless a
  hold is given), `4000:a` holds A, and `4000:menu`, `4000:mute` and
  `4000:lock` press M, L2 and R2.

Together they walk to a screen and photograph it. These are pictures of the
firmware's own drawing, so **unlike `tools/gen_screens.py` they do show
clipping, partial-repaint mistakes and overdraw**. They are not a substitute
for the panel: colours are exact RGB565 rather than what a given glass makes
of them, and nothing here models SPI time.

## The boot splash

`braino --splash <file.bmp> [width height [progress]]` writes the picture a
handheld shows from power-on until Braino's first screen, then exits without
opening a window. `src/HostSplash.cpp` draws it, and it is the one thing the
host draws at the panel's own resolution rather than as the 320x240 canvas
scaled up. The mark is vector artwork and gets every pixel:
`tools/braino-badge.svg` is compiled into the binary (`cmake/EmbedMark.cmake`)
and rasterised with 4x4 supersampling by a port of
`tools/gen_logo_mask.py`'s rasteriser, the one that makes the device's
bitmaps, so the two cannot disagree about the shape. Under it goes a glow,
which is the mark's own coverage blurred and added in the theme's accent.
Below sit a thin loading bar with its percentage, and `BRAINO_COPYRIGHT`. The
colours are the Dark theme's and the text is TFT_eSPI's font 2 at native
size. It is always Dark, so a console set to another theme shows a dark
splash for the moment before its first screen.

On a handheld the launcher sets `BRAINO_BOOT_SPLASH=1`, and Braino shows the
same picture across the window before its first screen (`runBootPhase()`).
The glow breathes there, between 35% and 115% of the static logo's
strength, once every 1.6 s: slow enough to read as light rather than
flicker. It starts at exactly the static logo's strength, so taking over from
the logo the OS showed has no step in it. **What the bar measures, honestly:**
Braino starts near the end of the boot, so the bar opens at the share of a
boot already gone. That is `/proc/uptime` against how long the previous boot
took to reach this point, which is written to `<data dir>/boot-seconds` each
time, and only when uptime says this really was a boot. It then fills over
about two seconds and reads 100% only when the first screen is next. The
opening number is an estimate; the 100% is not.

## The Anbernic RG40XXV under muOS

`cmake/aarch64-linux-gnu.cmake` cross-compiles against a Debian bullseye
arm64 sysroot (glibc 2.31) with clang. Do not use a distribution's
`aarch64-linux-gnu-gcc`: it carries its own newer glibc, searches it before
the sysroot, and links half the program against each. libstdc++ and libgcc
are linked statically. The binary needs glibc 2.30 or newer, plus SDL2 and
libcurl, which muOS ships. A sysroot unpacked by hand needs its absolute
symlinks made relative, or the linker resolves `libpthread.so` against the
build machine's own `/lib` and silently takes the static archive instead.

`muos/pack.sh` lays out what goes on the card's ROMS (exFAT) partition:
`ROMS/Ports/Braino.sh` (the launcher, from `muos/Braino.sh`, with its line
endings forced to Unix), `ports/braino/braino` and
`ports/braino/bootlogo.bmp`. Launch Braino once from muOS's Ports list.
Every run, the launcher then keeps three things true, even after someone
changes them in muOS's menus:

- **Startup is "Last Content",** so muOS relaunches Braino at every boot. Only
  content can be the startup target (a muOS *Application* cannot), which is
  why Braino ships as a port.
- **The boot logo is Braino's splash.** It is copied into `/mnt/boot/bootlogo.bmp`
  again whenever a theme change has replaced it; muOS regenerates that file
  from the active theme on the shutdown after a theme change.
- **muOS's safe-mode counter is cleared once Braino has run for ten
  seconds.** muOS counts every boot and clears the count only after its own
  menu has run for eight. A console that never shows that menu never clears
  it, and on every third boot muOS would skip the autostart and show the
  menu. Leave this out and the appliance works twice and fails once.

**To the player there is no other operating system.** If Braino exits for
any reason -- Select + Start, a crash, one of muOS's own kill hotkeys -- the
launcher starts it again, and muOS's menu is never drawn. The splash and its
loading bar run only on the first start of a boot, not on a restart. Two
deliberate exceptions:

- A file named `braino-maintenance` at the top of the card's games partition,
  created from a PC, lets Select + Start leave for muOS's menu, for updates
  and settings. Deleting it seals the console again.
- Five exits inside thirty seconds is a Braino that cannot start, not a
  player, so the loop gives up and muOS's menu appears. Otherwise a broken
  build would leave a black screen that looks like a dead device. The
  previous run's log is kept as `braino.prev.log` for exactly that case.

muOS keeps its own hotkeys running underneath: volume, brightness (Menu +
volume), suspend on the power key, and the L2 + R2 combos. Hiding muOS
completely is not finished: its first-boot setup, its shutdown splash and its
own network services are still to be dealt with, and a pre-built card image
is the plan for the first boot.

## Known differences from a board

- Portrait layout is drawn turned on a landscape window, as it would be on a
  device that cannot be turned round.
- Heap figures in System Info are a fixed healthy picture, not the PC's.
- The watchdog never reboots. Its frame statistics are real.
- No SD card. A handheld build (`BRAINO_HOST_BATTERY`, on whenever
  cross-compiling) reads its battery through a virtual sense pin that
  `src/HostBattery.cpp` answers, so the firmware's own curve, deadband,
  badge and 15% / 5% warnings run unchanged. Where the battery has a fuel
  gauge (`/sys/class/power_supply/*/capacity`, the AXP2202 on the RG40XXV),
  the pin answers with the voltage that curve maps back to the gauge's
  percentage. The cell voltage is a poor guess beside it, since it moves with
  the load and with charging. The curve is
  copied into that file to be run backwards, so a change to `LIPO_CURVE` in
  `src/hal/BoardPower.cpp` belongs there too. Without a gauge it reads
  `voltage_now`. A desktop build has no battery and shows no badge. The RGB LED is a handheld's light ring
  where it has one (`src/HostLed.cpp`), and nothing on a PC.
- GCC 13 warns about some firmware code the ESP32 toolchain is quiet about.
  Read those warnings: one already found a real truncated string.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
