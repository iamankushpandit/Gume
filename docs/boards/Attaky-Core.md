# Attaky Core (Cyber Deck Plus) -- 2.0-inch, capacitive touch, one USB-C

**How to recognise it:** not a bare board but a finished handheld in a case,
built from Attaky's snap-together modules. The Core is the top half -- a
2.0-inch screen, a five-way D-pad and four keys along the top edge -- and on
the Cyber Deck Plus it sits above a gamepad and a 48-key keyboard, with a
Media I/O module (microSD, speaker, microphone) on the back and a 1000 mAh
battery module. One USB-C socket, on the Core, through a CH340X USB-serial
chip. The ESP32-S3 module inside is the same N16R8 part as the Freenove board.

**In the installer:** "2.0-inch, capacitive touch, one USB-C port -- Attaky
Core 1.0 in a Cyber Deck Plus".

**Which modules this build expects:** the Core, and Media I/O on the back for
the SD slot. The Expand modules share their pins, so a Core wearing the
LoRa/GPS or FM/AM module instead would have this firmware driving that
module's lines as an SD card. The gamepad is harmless either way: the
firmware does not talk to it. The keyboard is read when it is clipped on, and
the battery module's fuel gauge whenever it answers.

## What has been checked on this board

**Nothing yet.** The profile was written on 2026-10-23 from Attaky's published
documents, before a device was available, and it builds. Every pin agrees with
two firmwares Attaky ship for this hardware -- their Retro-Go target and their
MeshCore variant -- but agreement on paper is not a board that has been seen to
draw and respond. Until somebody has flashed it, treat every line below as a
claim, and the four things most likely to need changing as:

- **Which way up the picture is**, in all four positions of the Layout button.
  The panel is mounted half a turn round, and Attaky warn that the rotation
  number depends on the display library.
- **Whether touch lands where you tap** in each of those positions. The
  controller's reset is driven through an I2C expander, which no other board
  here does.
- **The colours.** Inversion on and RGB order follow Attaky's Retro-Go init. A
  photo-negative picture means inversion is wrong; red and blue exchanged means
  the order is.
- **The SD card**, which is wired in SPI mode on lines the Expand modules share.

**The Core's own keys drive the launcher:** the D-pad moves a ring between
the tiles, Enter opens one, and the top-left shoulder key (L1) is Back -- Home
from any other screen, as BOOT is. Worth checking that each direction moves
the way it points, in landscape and in portrait.

**The battery badge reads the Power module's fuel gauge**, a MAX17048 at 0x36
on the touch bus, rather than a voltage divider -- the Core has none. The
battery task reads its cell voltage and state of charge every two seconds and
the badge shows the state of charge as the gauge reports it, through the same
deadband and the same 15% / 5% warnings as every other board. A gauge that
does not answer -- the module unclipped -- is no reading: no badge and no
warning, never an empty battery. Worth checking on a device:

- the serial log says `[battery] fuel gauge at 0x36 answering` at boot, and
  System Info -> Board -> Power shows a voltage near the cell's and
  "Fuel gauge MAX17048";
- the percentage agrees roughly with Attaky's own firmware on the same charge;
- unclipping the Power module (on USB) takes the badge away within a couple of
  seconds and logs `not answering`, and clipping it back brings it back;
- touch is not slowed or disturbed while the gauge is being read -- the two
  share one I2C bus from two tasks, with Wire's own lock between them.

**The keyboard types into every text field:** a player's name, a Nearby
peer's name, both admin PIN pads and the Wi-Fi password, with Delete and Enter
doing what DEL and OK do. The name fields take capitals, digits and spaces, as
their on-screen keypad does; the password takes exactly what is typed, with
Shift for capitals. The on-screen keypad stays: the module clips off, and the
firmware checks for it at boot.

Left out on purpose, not broken: sound (the NS4168 amplifier needs a plain-I2S
backend the firmware does not have yet), the status LED (on the I2C expander), the
gamepad, and the D-pad on any screen but the launcher. Touch is the console's
input on every board.

## Sources

Checked 2026-10-23. Attaky states no licence for its pictures or documents, so
these are linked, not copied. Its firmware-builder repository is Apache-2.0
and its Retro-Go overlay GPL-2.0; only facts are taken from either.

| Source | Archived copy | What it gives |
|---|---|---|
| [Attaky shop: Cyber Deck Plus](https://shop.attaky.com/products/attaky-cyber-deck-plus) | -- | what is in the box |
| [Attaky manual: Cyber Deck Plus 1.0](https://manuals.attaky.com/docs/complete-builds/cyber/deck-plus) | -- | controls, buttons, power |
| [Attaky datasheets](https://docs.attaky.com/docs/datasheets) | -- | Core, Media I/O, Gamepad, Keyboard and Standard Cell modules |
| [Attaky I2C address map](https://docs.attaky.com/docs/frame-and-connections/i2c-address-map) | -- | every device on the shared bus |
| [Hardware catalog](https://github.com/Attaky-Module/Attaky-Firmware-Builder/blob/main/shared/hardware-catalog.md) | -- | the host pin map, including the display's SPI pins |
| [Retro-Go target](https://github.com/Attaky-Module/retro-go/blob/master/overlay/components/retro-go/targets/ATK-RetroSeries-V1/config.h) | -- | a working display, SD and audio pin set |
| [MeshCore variant](https://github.com/Attaky-Module/MeshCore/tree/main/variants/attaky_core) | -- | a working display, I2C and button set |

<!-- BEGIN GENERATED by tools/gen_board_docs.py -- do not edit by hand -->

### From the firmware profile

| | |
|---|---|
| Firmware name (`BOARD_NAME`) | `Attaky-Core` |
| Installer / PlatformIO environment | `app_attaky_core` |
| Device-id tag | `ATK-...` |
| Panel | 240 x 320, ST7789 driver |
| Touch | capacitive, I2C; reset on port P1_3 of the I2C expander at 0x59 |
| Sound | none |
| Battery sense | MAX17048 fuel gauge, I2C address 0x36 on the touch bus |
| Flash | 16 MB |

| GPIO | Used for |
|---|---|
| 1 | Touch: I2C SCL |
| 2 | Touch: I2C SDA |
| 4 | Touch: interrupt (not used) |
| 5 | SD card: SPI clock |
| 6 | SD card: SPI MOSI |
| 7 | SD card: SPI MISO |
| 8 | SD card: chip select |
| 10 | Display: reset |
| 11 | Display: SPI clock |
| 12 | Display: data/command |
| 13 | Display: SPI MOSI |
| 14 | Display: backlight |
| 38 | Key: BOOT, which the firmware uses as Home |
| 45 | Display: chip select |

![GPIO use on the Attaky-Core](img/Attaky-Core-pins.png)

### Photos

None yet. Add your own as `docs/boards/img/Attaky-Core-front.jpg` (and `-back.jpg`)
and re-run `python tools/gen_board_docs.py`.

<!-- END GENERATED -->

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
