# Porting Braino! to another board

Braino! describes each board it supports in **two files, and nowhere else**:

| Where | What it holds |
|---|---|
| `include/boards/<id>.h` | the whole board: pins, rotations, the battery divider, which peripherals exist at all |
| `platformio.ini` `[board_<id>]` | only the macros TFT_eSPI insists on being told at compile time, plus the board's name and the header naming it |

No file under `src/` names a GPIO number, a panel size or a divider ratio. If
you find yourself adding one, the board profile is missing a field — add it to
`include/BoardProfile.h` instead, and fill it in for every existing board in the
same commit.

## First: can this board be supported at all?

**Not every ESP32 display board can run Braino!, and one that cannot is out of
scope rather than a porting task.** Four things are hard requirements. A board
missing any of them fails to compile, with a message saying which — that is
deliberate, because the alternative is firmware that flashes and then cannot be
read or pressed.

| Requirement | Why | If it is missing |
|---|---|---|
| A panel at least 320×240 in landscape | The playable games are drawn on a fixed canvas and position controls by arithmetic on it. A smaller panel puts buttons off the edge, and a player cannot press what is not there | Not supportable |
| A touch controller | It is the only input this firmware has. There are no buttons | Not supportable |
| The backlight on a GPIO | Brightness is a setting with a readability floor, and the idle path blanks the screen rather than only dimming it | Not supportable |
| 4 MB of flash | The app partition is 3 MB (`huge_app.csv`) and the firmware is most of it. No partitioning makes a 2 MB part fit | Not supportable |

Everything else is optional, and a board without it gets a firmware that
quietly does without — no code change, just `PIN_NONE`:

| Optional | What the firmware does without it |
|---|---|
| SD card slot | `sdReady()` is false; optional SD content simply is not loaded. Everything has defaults |
| RGB LED | `beepOk()`/`beepError()` become no-ops. There is no audio either way |
| Speaker | Already stubbed on every board |
| Battery sense | The gauge blanks its digits, which is the honest reading for a board that cannot see a cell |

A larger panel clears the size requirement, but see
[What is *not* decoupled yet](#what-is-not-decoupled-yet) before assuming the
games look right on it.

## Second: a supported board is one people can flash

If a board can be supported, **the web installer has to offer it**. Someone who
owns the board should be able to put the firmware on it from
`https://iamankushpandit.github.io/Gume/` without installing a toolchain;
"supported" that only means "builds on the maintainer's machine" is not
support.

That is enforced rather than remembered. `tools/gen_site.py` derives the
picker's board list from the same `[board_*]` sections in `platformio.ini`, so
a new board appears on the page automatically — and refuses to generate the
site until it has a label, an environment offered in `VARIANTS`, and a CI build
producing the binaries its manifest points at. `tools/check_boards.py` reports
the same gaps without needing to build. Both are in the checklist below.

## The short version

0. Check the board clears the four hard requirements above.
1. Write `include/boards/<id>.h`.
2. Add `[board_<id>]` to `platformio.ini`.
3. Add `[env:<something>]` pointing at it.
4. `python tools/check_boards.py`
5. `pio run -e <something>`
6. Flash it and check the six things in [Bring-up](#bring-up).
7. Put it on the web installer and in CI, and update the docs —
   [Landing a port](#landing-a-port).

## 1. The board profile

Copy `include/boards/e32r28t1.h` and work through it. Two rules:

**Label every value with its field name in a `/* comment */`.** `BOARD` is
aggregate-initialised, so the initialisers are positional and carry no names of
their own. `tools/check_boards.py` reads those labels to prove the board filled
in every field the contract declares — without them, a field added later would
compile on your board with a silent zero in it.

**An optional peripheral the board does not wire gets `PIN_NONE`.** The
firmware checks `BOARD.hasSdSlot()`, `hasRgbLed()`, `hasSpeaker()` and
`hasBatterySense()` and degrades quietly. A board with no battery connector
needs no code change; the gauge blanks its digits, which is the honest answer.
`PIN_NONE` is not a way past the four hard requirements above — those are
asserted, and a board that trips one is telling you it cannot run this
firmware.

Things worth getting right the first time:

- **`landscapeRotation`** is the quarter-turn that puts the USB socket at the
  bottom. `portraitRotation` is a quarter-turn from it, and `BoardConfig.h`
  static_asserts that the two are not the same axis. `Board::pollTouch()`
  compensates for every rotation, so nothing in game code needs adjusting.
- **The panel size is native (portrait)**, matching `TFT_WIDTH`/`TFT_HEIGHT`.
  The landscape canvas the games draw on is *derived* — `SCREEN_WIDTH` and
  `SCREEN_HEIGHT` come out of `BoardProfile::screenWidth()`. Do not state it
  twice; the second statement would eventually be the wrong one.
- **`commonAnode`** on the LED means a channel lights when its line is driven
  LOW. Get it backwards and the LED is on and stays on.
- **`dividerRatio`** is the multiplier from the voltage at the ADC to the
  voltage at the cell. Read it off the vendor's own documentation, then confirm
  it against a meter with `pio run -e batdiag` — that tool reads the same
  profile, so what it measures is what the product will report.
- **Use the vendor's pin table, not a generic ESP32 pinout, and never another
  CYD variant's.** GPIO34 is battery sense on the E32R28T-1 and the light
  sensor on the classic ESP32-2432S028R. They look like the same board.

## 2. The platformio.ini section

```ini
[board_<id>]
build_flags =
    -D BOARD_NAME=\"<what the owner should see>\"
    -D GUME_BOARD_HEADER=\"boards/<id>.h\"
    -D <DRIVER>_DRIVER=1
    -D TFT_WIDTH=...
    -D TFT_HEIGHT=...
    -D TFT_MISO=...  -D TFT_MOSI=...  -D TFT_SCLK=...
    -D TFT_CS=...    -D TFT_DC=...    -D TFT_RST=...
    -D TFT_BL=...
    -D TFT_BACKLIGHT_ON=HIGH
    -D SPI_FREQUENCY=...
    -D SPI_READ_FREQUENCY=...
```

Only what is *specific to this board* goes here. Everything shared —
`USER_SETUP_LOADED`, the fonts, the language standard — is already in
`[common]`, and `check_boards.py` fails if a board-specific macro drifts up
into it, because in `[common]` it would be a claim about every board.

The panel is now described twice: once for TFT_eSPI, once for us.
`include/BoardConfig.h` static_asserts `TFT_WIDTH`, `TFT_HEIGHT` and `TFT_BL`
against the profile, so the two cannot disagree past the compiler.

**`TFT_BL` deserves its own paragraph.** With the wrong backlight pin the panel
is completely dark while Wi-Fi, BLE, NVS, the LED and the screen saver all run
perfectly — the serial log looks healthy and it reads as a dead screen or a
wrong driver. The static_assert exists because that afternoon has already been
spent once.

## 3. The environment

```ini
[env:<something>]
extends = esp32_common
build_src_filter = +<*> -<wifi_diag.cpp> -<battery_diag.cpp>
lib_deps = ${common.lib_deps}
lib_ldf_mode = deep+
build_unflags = ${common.build_unflags}
build_flags =
    ${common.build_flags}
    ${board_<id>.build_flags}
```

One environment targets exactly one board; `check_boards.py` enforces that too.

## Bring-up

Flash it, open `pio device monitor`, and check these in order. Each failure
mode below has actually happened.

1. **Serial says `[boot] board=<name> rot=…`.** If nothing appears, it is the
   partition or the upload, not the board profile.
2. **The backlight comes on.** If the log is healthy and the screen is black,
   it is `TFT_BL` — and the static_assert means the profile and the ini agree,
   so *both* are wrong. Try the alternates for your panel.
3. **The image is the right way round and not garbled.** ST7796S accepts most
   ILI9341 commands, so the wrong driver gives you a real but mirrored,
   partly-corrupt picture rather than nothing — easy to misread as "sort of
   working".
4. **Touch lands where you tap**, after the calibration wizard. If it is
   mirrored or transposed, that is `landscapeRotation`, not the touch pins.
5. **The battery percentage matches a meter.** `pio run -e batdiag` is the tool
   for this; page CALIBRATE resolves the real divider ratio.
6. **The LED shows the colour asked for.** Wrong colour, not no colour, means
   the channels are crossed; LED permanently on means `commonAnode` is wrong.

If the board has no battery, no SD slot or no LED, set those to `PIN_NONE` and
skip the corresponding step — that is a supported configuration, not a
limitation.

## Landing a port

A board that boots is not a board that ships. In the same commit:

- `python tools/check_boards.py` and `python tools/check_docs.py`, both clean.
  Between them they check the board is complete, that its two descriptions
  agree, and that the web installer can actually offer it.
- `README.md` — say which boards are supported, and record the flash/RAM
  figures from your own `pio run` for the environment you added.
- `CLAUDE.md` — the same figures, and the environment in the build section.
- `tools/gen_site.py` — a `BOARD_DETAILS` entry (label, chip family, where to
  buy one), and a `VARIANTS` entry for any new environment. The board list
  itself is derived from `platformio.ini`, so this is the part that cannot be.
  A picker entry whose firmware CI never built fails halfway through erasing
  somebody's board, so `.github/workflows/pages.yml` and `ci.yml` must build it
  too. `check_boards.py` and `check_docs.py` cross-check all of this.
- `site/index.template.html` — the copy still describes **one** board in
  prose. `gen_site.py` refuses to generate with a second board until that is
  generalised, on purpose: offering a firmware the page then mislabels is worse
  than not offering it.
- `CHANGELOG.md`.

## What is *not* decoupled yet

The playable games are authored against a fixed landscape canvas and lay
themselves out against `SCREEN_WIDTH`/`SCREEN_HEIGHT`. Those constants now
follow the board, so a differently sized panel gets the right numbers — but a
game that positions things by arithmetic on them has not been checked at any
other size. The launcher and the system apps read `tft.width()`/`tft.height()`
at render time and are responsive by rule (see `CLAUDE.md`); Settings and Wi-Fi
are known violators.

A port to a board with the same 320×240 landscape canvas needs nothing beyond
this document. A port to a different panel size needs the games looked at, one
at a time, and that is the work — not this layer.
