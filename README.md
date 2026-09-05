# Braino!

[![CI](https://github.com/iamankushpandit/Gume/actions/workflows/ci.yml/badge.svg)](https://github.com/iamankushpandit/Gume/actions/workflows/ci.yml)
[![Pages](https://github.com/iamankushpandit/Gume/actions/workflows/pages.yml/badge.svg)](https://github.com/iamankushpandit/Gume/actions/workflows/pages.yml)
[![Flash in browser](https://img.shields.io/badge/flash%20in%20browser-Web%20Serial-6f42c1)](https://iamankushpandit.github.io/Gume/)
[![Version](https://img.shields.io/badge/version-5.5.0--SNAPSHOT-9a6700)](CHANGELOG.md)
[![Games](https://img.shields.io/badge/games-31-2d7d9a)](#the-games)
[![Platform](https://img.shields.io/badge/platform-ESP32--32E-e25822)](#build-and-flash)
[![Framework](https://img.shields.io/badge/framework-Arduino%20%7C%20PlatformIO-orange)](https://platformio.org/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599c)](platformio.ini)
[![Flash](https://img.shields.io/badge/flash-75.9%25%20of%203%20MB-yellow)](#build-and-flash)
[![No telemetry](https://img.shields.io/badge/telemetry-none-brightgreen)](#privacy)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue)](LICENSE)

A 31-game educational console for young players, running on an **ESP32-32E
board** (E32R28T-1 — ILI9341 320×240 resistive
touchscreen, 4 MB flash, no PSRAM).

Everything is baked into the firmware: **no SD card, no accounts, no telemetry,
no data collection.** Two radios exist and both are narrow by design:

- **Wi-Fi** is used for exactly one thing — fetching the time from an NTP
  server (plus a one-off public-IP lookup to guess the time zone).
- **Bluetooth LE** is **off by default**. Turned on, it broadcasts a device
  name (`Braino-<id>`) and a hardware id. Turn on **Nearby** as well and it also
  broadcasts which game is open and the best score for it — anonymously, with no
  name or profile attached. The device shows you the exact bytes it is
  transmitting either way. See [Privacy](#privacy).

| | |
|---|---|
| Games | 31 |
| Flash | 2,388,105 / 3,145,728 bytes (**75.9%**) |
| RAM | 73,860 / 327,680 bytes (**22.5%**) |
| Artwork | 195 country flags, 50 state flags, 50 state outlines — 763 KB (34% of the image) |

Contribution workflow lives in [CONTRIBUTING.md](CONTRIBUTING.md), alongside
a [code of conduct](CODE_OF_CONDUCT.md), a
[security policy](SECURITY.md) and [NOTICE.md](NOTICE.md) on copyright and
the one thing a fork is asked to do. What the firmware does *not* do yet is
written down too, as [open issues](https://github.com/iamankushpandit/Gume/issues).

## Help wanted

**Board ports.** Braino is developed and tested against the E32R28T-1. Two
ESP32-2432S028 CYD variants ship as ports built from published pin maps and
have not been verified on real hardware; the Freenove FNK0104B *has* been
verified on hardware but ships with three peripherals switched off (see
[Freenove FNK0104B](#freenove-fnk0104b-esp32-s3)); the 4-inch **E32R40T**
has had its panel, backlight and touch confirmed on hardware but ships with four
peripherals not yet characterised (see [E32R40T](#e32r40t-4-inch-st7796))
— if you own any of those, telling us whether it works is the single
most useful thing you can send. The CYD family has many variants whose
differences fail silently — backlight on GPIO21 versus GPIO27, GPIO34 as a
battery sense here but a light sensor on the ESP32-2432S028R. A board is now described in two
files and nowhere else: a profile header in `include/boards/` and a
`[board_*]` section in `platformio.ini`. No file under `src/` names a GPIO,
and the panel size the games draw on is derived from the profile rather than
stated.

Not every board can be supported. Braino needs a panel of at least 320×240
in landscape, a touch controller (the only input it has), the backlight on a
GPIO, and 4 MB of flash for the 3 MB app; a board missing one of those fails
to compile with a message saying which, rather than flashing into something
unreadable. An SD slot, an RGB LED, a speaker and battery sensing are all
genuinely optional — the firmware does without them.

And a board that *can* be supported has to be flashable from
[the web installer](https://iamankushpandit.github.io/Gume/), not just from a
toolchain: the page derives its board list from the same `[board_*]` sections,
and the checks fail a port that CI does not build. [docs/PORTING.md](docs/PORTING.md)
is the checklist, and [AGENTS.md](AGENTS.md) covers the hardware reference and
bring-up app a port needs before it ships.

**Cases.** A printable shell for the E32R28T-1 is in
[`cases/`](cases/README.md), one folder per board. A case is not required for a
board to be supported — but a builder who has never opened a CAD tool should
not be left with a bare board and a battery taped to the back, so ports are
encouraged to bring one. Print notes and fastener sizes are as valuable as the
mesh.

**Tell us what's wrong with the code.** Bug reports and code critique are
genuinely valuable — please
[open an issue](https://github.com/iamankushpandit/Gume/issues), and if you
can, say how you'd fix it. Hardware assumptions that were never measured are
the most valuable of all.

**Something that needs no hardware at all.** There are no host-side tests
yet, and a good deal of this firmware is pure logic that never touches a
board — progress tracking, scoped storage, question selection, touch
rotation. Rendering already goes through an interface, so a recording
backend is possible without a panel. That is
[issue #44](https://github.com/iamankushpandit/Gume/issues/44), and the rest of what this firmware does not
do yet is [on the tracker](https://github.com/iamankushpandit/Gume/issues) beside it.

Found a security problem instead? Don't open an issue —
[SECURITY.md](SECURITY.md) has the private route.

<p align="center">
  <img src="docs/screens/launcher-wide.png" width="420" alt="Home screen, Wide layout">
  <img src="docs/screens/launcher-tall.png" width="315" alt="Home screen, Tall layout">
</p>

## Install it without a toolchain

**<https://iamankushpandit.github.io/Gume/>** flashes the board from a browser
over Web Serial — pick the board and the firmware, plug the device into USB, and
press the button. Nothing to install; desktop Chrome, Edge or Opera only, since
Firefox and Safari do not implement Web Serial.

The picker offers one image per board, and you have to choose the one that
matches yours: two boards sold under the same name can carry different display
controllers, and the wrong image gives inverted colours, dead touch or a blank
screen rather than an error.

### Freenove FNK0104B (ESP32-S3)

**The Freenove FNK0104B** is a 2.8-inch ESP32-S3 board with the same ILI9341
240×320 panel, an **FT6336U capacitive** touch controller, 16 MB flash and
8 MB PSRAM. It is flashable from the web installer and every one of the 31
games works on it. It was brought up on real hardware rather than from a
published pin map: display, backlight, touch at all four rotations, battery
sense and the audio path were each confirmed on a device.

Its USB-C is on a short edge, so unlike the other boards no landscape rotation
puts the socket at the bottom — Braino uses rotation 3, which puts the cable on
the left.

**Sound works**, and on this board it is the whole of the
[sound vocabulary](#sound) rather than two beeps — including the spoken
"Let's play Braino!" at boot. It plays through the ES8311 codec, capped at 80%
volume. **Two things do not work yet, and they are switched off rather than
broken:**

| Not working | Why | Issue |
|---|---|---|
| Status LED | One **WS2812** addressable pixel on GPIO42; `RgbLedProfile` describes three PWM channels. The colour half of the feedback is a no-op — the sounds still play — and the screen saver loses its rally colour | [#72](https://github.com/iamankushpandit/Gume/issues/72) |
| SD card | The slot is **SDMMC 4-bit**; `SdProfile` describes an SPI card. Optional SD content is simply not loaded, which everything has defaults for | [#73](https://github.com/iamankushpandit/Gume/issues/73) |

The battery **percentage** is correct, but the **charging/discharging verdict**
is not yet validated on this board's charger — those constants were measured
against the E32R28T-1's TP4054 ([#74](https://github.com/iamankushpandit/Gume/issues/74)).
Partition sizing on the 16 MB part and a first-frame time worth checking are
tracked in [#75](https://github.com/iamankushpandit/Gume/issues/75).

`pio run -e s3diag` is a standalone bring-up probe for this board: panel,
rotation, I²C scan, live touch, battery, and the full audio path including a
record-and-playback microphone test.

### E32R40T (4-inch ST7796)

**The E32R40T** is the 4-inch board, and it is here for a reason the other
ports are not: a physically bigger, plainer screen for players who need one.
Braino stretches the games onto it rather than leaving them small in a corner
— playable games draw through `Ui::ScaledRenderer`, which maps the fixed
320×240 game canvas onto the panel's 480×320 and renders text at double glyph
scale, so text gains on its boxes rather than merely keeping pace.

It is the same reference PCB family as the E32R28T-1 and shares its display bus
exactly — MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, HSPI at 40 MHz. **One pin
differs, and it is the one that fails silently: the backlight is GPIO27, not
GPIO21.** With the wrong value the panel is black while Wi-Fi, BLE, NVS and the
screen saver all run perfectly, so the serial log looks healthy and it reads as
a dead screen. Both values were measured rather than assumed — 21 was confirmed
dark from a clean reset — and `BoardConfig.h` static_asserts the profile
against `TFT_BL` so they cannot drift apart again.

Touch is an XPT2046 **sharing the display's SPI bus** with its own CS on
GPIO33, driven through TFT_eSPI's touch extension rather than a bit-banged bus
of its own. Its IRQ on GPIO36 is wired but **unusable** — an input-only pin
with no internal pull and no external pull-up fitted, so it floats low and
reports a permanent false pen-down. The profile says so (`irqUsable`) and the
firmware gates on pressure alone, which is clean here: idle noise reads 10–20
and a real press 2000+.

**Four peripherals are switched off rather than broken**, because nobody has
measured them on this board yet: the SD slot, the RGB LED, the speaker and
battery sense. They are plausibly wired like the 2.8-inch board's — and
"plausibly" is exactly the reasoning that produced a backlight pin nobody had
checked. `PIN_NONE` costs a feature the firmware already does without; a wrong
pin costs a fictional battery percentage. If you own one and can measure them,
that is the most useful thing you can send.

`pio run -e diag4` is the standalone bring-up probe for this board: panel
identity over SPI, a backlight sweep, geometry and colour, rotation, both touch
wirings, ADC candidates, and a Wi-Fi/BLE coexistence test.

**The E32R28T-1 / ESP32-32E** 2.8-inch resistive-touch board is the one this
firmware is developed and tested against — use
[this Amazon board](https://www.amazon.com/dp/B0D92C9MMH?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1).
The two **ESP32-2432S028** entries (classic ILI9341 and Rv3 ST7789) are ports
built from published pin maps and have **not been verified on hardware**; they
are offered so somebody who owns one can try them and report back. Other ESP32
boards may accept a binary, but their display and touch pins will not match.

That page is generated by `tools/gen_site.py` and published by
`.github/workflows/pages.yml`, which builds every PlatformIO environment `platformio.ini` declares on
every push to `main` and puts the real binaries behind the button. The page
derives its version, game list and blurbs from the firmware for the same reason
the About app does — see [Layout of the code](#layout-of-the-code).

Flashing replaces the firmware and leaves NVS alone; *erasing* throws away the
touch calibration, the profiles, the scores and the mastery data.

---

> **About the images in this README.** These are **rendered mock-ups**, not
> device photos. They are redrawn on a PC by `tools/gen_screens.py` using the
> exact rectangles, fonts and colours from the C++ source, so the layout is
> accurate. The flag and outline artwork *is* the real pixel data, decoded from
> the generated arrays the device draws from. True screenshots aren't possible
> because the target board doesn't wire the panel's MISO line for read-back.

---

## The games

Ages are a guide, not a gate. Every game can be hidden from a player's launcher
in **Profiles → Edit → Games**, so you can pare the list down to what one player
needs without touching anyone else's. Only the admin can change it.

### Numbers and early maths

| Game | What it is | What it builds | Age |
|---|---|---|---|
| **Counting** | "How many objects?" — tap the matching number | One-to-one correspondence: the idea that counting means one number per object | 3–5 |
| **Fingers** | Alternates *"How many fingers?"* (count what's raised) and *"Show me 7 fingers"* (raise that many) | Counting on hands in **both** directions — recognising a quantity and producing one | 3–6 |
| **Shape Arith** | Objects appear one by one to add; subtraction splits left vs. take-away boxes | Makes arithmetic concrete before it's symbolic. The subtraction display keeps the removed group visible while the player counts what is left | 4–6 |
| **Number Line** | A marker hops along a number line to reach the answer | Turns addition and subtraction into *movement* — the mental model behind mental arithmetic | 5–7 |
| **Math** | "Tap the answer" — addition and subtraction, difficulty rises with level | Recall speed once the concept is solid | 5–8 |
| **Multiply** | "Tap the product" — times tables | Multiplication facts | 7–10 |
| **Fractions** | "Pick the matching fraction" against a pie chart | Connects the written fraction to the amount it represents | 6–9 |
| **Money** | "How much is this?" — count coins, compare amounts, make change | Coin values and everyday arithmetic | 5–9 |
| **Percent** | Read, make and calculate percentages on a circle | Percentage as a portion of a whole, before it is an algorithm | 7–11 |
| **Sorting** | "Tap smallest to largest" (or the reverse) | Ordering and magnitude comparison | 4–7 |

### World knowledge

| Game | What it is | What it builds | Age |
|---|---|---|---|
| **Flags** | A real flag, name the country. Correct answers unlock a **capital-city bonus** | 195 flags with a genuine reward loop; capitals arrive as a bonus rather than a chore | 5–12 |
| **US States** | Name the state, then its capital | US geography as a name-then-capital pair, the same loop as Flags | 6–12 |
| **State Flags** | A real state flag, name the state, then its capital | Fifty more flags with the same reward loop | 6–12 |
| **State Maps** | A real state outline, name the state, then its capital | Map-shape recognition | 7–12 |
| **Calendar** | "What comes after Wednesday?" — days and months | Sequence and cyclical time | 4–7 |
| **Time** | "Which time is shown?" on an analogue clock | Reading a clock face | 5–8 |
| **Elements** | **Explore** the real 118-cell periodic table, tap any square to read what it is and where you have met it; **Quiz** asks six kinds of question about it; **Level** decides how much of the table it may ask about | The periodic table as a place rather than a list — a player who has never taken chemistry can find Oxygen on the chart, learn that Helium is what makes balloons float, and never be asked about an element they have not seen | 5–12 |

Flags, Elements and the three US States games all use **spaced repetition**; Flags also
uses **adaptive difficulty** — see below.

### Logic, memory and attention

| Game | What it is | What it builds | Age |
|---|---|---|---|
| **Cinnamon** | Repeat the colour sequence | Working memory and sustained attention | 4–10 |
| **Memory** | Match pairs face-down | Visual working memory | 3–8 |
| **Odd One** | "Tap the one that is different" | Categorisation — spotting the attribute that doesn't fit | 3–6 |
| **Shapes** | Match a named shape *and* colour, e.g. "red circle" | Holding two attributes in mind at once | 3–6 |
| **Color Mix** | "What do you get?" mixing two colours | Colour theory, and that mixing is predictable | 4–8 |
| **Microku** | 2×2 up to 6×6 grids | Constraint reasoning, scaled to a player's level | 6–12 |
| **Slide** | Slide numbered tiles into order | Planning several moves ahead | 6–12 |
| **Maze** | Drag a dot to the exit | Fine motor control and route planning | 3–7 |
| **Whack** | Tap the smiles before they vanish | Reaction time and visual scanning | 3–8 |
| **Tic-Tac-Toe** | Two players | Turn-taking and blocking — best played with a grown-up | 4+ |
| **Trace** | Trace uppercase, lowercase, and digits following numbered waypoints | Letter formation and stroke order, with Again and Next controls so a player can repeat any character | 3–6 |
| **GRE Words** | **Study** flips a card to its meaning and an example; **Quiz** picks the right gloss from four | 250 GRE-level words, weighted by spaced repetition so a missed word returns soon. Aimed at an older student, not a preschooler | 15+ |
| **Dice** | Pick one, two or three dice and throw them | A physical randomiser to play board games with when the real dice are lost. Keeps no score, because a best total would be luck | 3+ |
| **Coin Flip** | Spin a coin, best of one, three or five | Settling an argument fairly, and seeing that best-of-five is not the same as one toss. Keeps no score, for the same reason | 4+ |

---

## What makes it more than a quiz

### Spaced repetition

Questions used to be drawn uniformly at random, so a player saw Brazil exactly as
often as Bhutan and got no extra practice on the ones they missed.

Flags and the US States games now keep a mastery score per item
(`src/engine/Progress.cpp`). A miss costs **twice** what a correct answer earns,
so a missed country returns quickly and only leaves the rotation after repeated
success. Weighting runs from **8×** for a recently-missed country down to **1×**
for a mastered one — mastered items still appear, just rarely, so the mix stays
varied.

Each game tracks **separately**: recognising Italy's flag says little about
recognising Texas's outline. Mastery is also stored **per player profile**, so
one player's progress never moves another's.

### Adaptive difficulty

Three tiers — **Easy** (30 familiar countries), **Medium** (62), **Hard** (all
195). Six correct in a row promotes a tier automatically. The tier button still
overrides by hand, and the choice persists.

### Profiles

Up to **five players plus a permanent Guest**, chosen at boot. Scores, mastery
data and per-game visibility are all scoped to the active profile automatically
— games never do anything to opt in.

**Guest deliberately persists nothing.** Every score and mastery write is
dropped while Guest is active. That is what makes it a guest rather than a sixth
player: a visitor can play without leaving results behind or disturbing anyone
else's records.

Deleting a player deletes that player's profile-scoped NVS data and shifts later
players down with their own scores, mastery and visibility intact; deleting the
active player switches the device back to Guest.

Device settings — theme, layout, brightness, Wi-Fi, time zone, the BLE beacon —
stay global.

### Accessibility

Cinnamon originally repainted the **whole screen** on every step, producing a
full-screen flash roughly once a second — uncomfortable generally and a genuine
risk for photosensitive players. It now repaints only the pads that changed, so
there is **no full-area luminance change at all**, and mirrors each colour on
the case LED so the cue doesn't depend on the screen flashing. On a board with
a codec each pad also has its own fixed note, so the sequence can be followed
by ear alone — which is the point of the game and, for a player who cannot
easily tell the four colours apart, the difference between playable and not.

<p align="center">
  <img src="docs/screens/cinnamon.png" width="360" alt="Cinnamon Says">
</p>

---

### Sound

On a board with an audio codec — today that is the
[Freenove FNK0104B](#freenove-fnk0104b-esp32-s3) — the console has a small
fixed vocabulary of sounds rather than a beep for yes and a buzz for no. A
point scored, a tile sliding, a level cleared, a personal best beaten, a mole
about to vanish and a game lost each sound different, and they sound the same
in every game that uses them, so a player learns them once.

**None of it is a recording.** There is no WAV, no MP3 and no sample bank
anywhere in this firmware, and there is not going to be one: a single second of
audio would cost more flash than several games. Every sound is generated as it
plays, from a few oscillators, a noise source and three formant filters. The
whole vocabulary — fourteen cues, four pad notes and a spoken phrase — costs
under a kilobyte.

The formant filters are there because the console says **"Let's play Braino!"**
when it starts up, in a deliberately robotic voice. That phrase is not a clip
either. It is written down as the phonemes it is made of, and each phoneme is
the first three resonances of a mouth shaped to say it; drive those with a
steady buzz and you get a vowel, drive them with noise and you get a
consonant. It is the same technique that made 1980s home computers talk, which
is exactly why it sounds like one.

All of it can be turned off. **Settings → Sound** holds a mute switch and a
volume slider capped at 85% for a device held near a child's ears, plus two
buttons to hear the level while you set it — because setting a volume you
cannot hear while you set it is guesswork.

A board with no codec is unchanged: the RGB case LED still flashes green for
right and red for wrong, and every call to play a sound is silently a no-op.
The Sound tab says which of those two situations you are in rather than showing
a muted switch for a speaker that was never there.

---

## Every game

One screen per game, in launcher order.

### Numbers and early maths

<p align="center">
  <img src="docs/screens/counting.png" width="300" alt="Counting">
  <img src="docs/screens/fingers-count.png" width="300" alt="Finger Counting: count them">
  <img src="docs/screens/fingers-show.png" width="300" alt="Finger Counting: show me N">
  <img src="docs/screens/shapearith.png" width="300" alt="Shape Arith">
</p>
<p align="center">
  <img src="docs/screens/numberline.png" width="300" alt="Number Line">
  <img src="docs/screens/math.png" width="300" alt="Math">
  <img src="docs/screens/multiply.png" width="300" alt="Multiplication">
</p>
<p align="center">
  <img src="docs/screens/fractions.png" width="300" alt="Fractions">
  <img src="docs/screens/money.png" width="300" alt="Money">
  <img src="docs/screens/percent.png" width="300" alt="Percent">
</p>
<p align="center">
  <img src="docs/screens/sorting.png" width="300" alt="Sorting">
</p>

### World knowledge

<p align="center">
  <img src="docs/screens/flags-country.png" width="300" alt="Flags: name the country">
  <img src="docs/screens/flags-capital.png" width="300" alt="Flags: capital bonus">
  <img src="docs/screens/states.png" width="300" alt="US States: name the capital">
</p>
<p align="center">
  <img src="docs/screens/stateflags.png" width="300" alt="State Flags: name the state">
  <img src="docs/screens/statemaps.png" width="300" alt="State Maps: name the outline">
  <img src="docs/screens/calendar.png" width="300" alt="Calendar">
</p>
<p align="center">
  <img src="docs/screens/time.png" width="300" alt="Time">
  <img src="docs/screens/elements.png" width="300" alt="Elements: the periodic table">
  <img src="docs/screens/elements-card.png" width="300" alt="Elements: one element up close">
</p>
<p align="center">
  <img src="docs/screens/elements-quiz.png" width="300" alt="Elements: find it in the table">
</p>

### Logic, memory and attention

<p align="center">
  <img src="docs/screens/cinnamon.png" width="300" alt="Cinnamon Says">
  <img src="docs/screens/memory.png" width="300" alt="Memory Match">
  <img src="docs/screens/oddone.png" width="300" alt="Odd One Out">
</p>
<p align="center">
  <img src="docs/screens/shapes.png" width="300" alt="Shape & Color">
  <img src="docs/screens/colormix.png" width="300" alt="Color Mix">
  <img src="docs/screens/microku.png" width="300" alt="Microku">
</p>
<p align="center">
  <img src="docs/screens/slide.png" width="300" alt="Slide Puzzle">
  <img src="docs/screens/maze.png" width="300" alt="Maze">
  <img src="docs/screens/whack.png" width="300" alt="Whack A Mole">
</p>
<p align="center">
  <img src="docs/screens/tictactoe.png" width="300" alt="Tic-Tac-Toe">
  <img src="docs/screens/trace.png" width="300" alt="Trace: uppercase and digits">
  <img src="docs/screens/trace-lower.png" width="300" alt="Trace: lowercase letters">
  <img src="docs/screens/grewords.png" width="300" alt="GRE Words: pick the meaning">
  <img src="docs/screens/grewords-study.png" width="300" alt="GRE Words: study card">
  <img src="docs/screens/dice.png" width="300" alt="Dice: three dice thrown">
  <img src="docs/screens/coinflip.png" width="300" alt="Coin Flip: best of five">
</p>

---

## System screens

### Settings

<p align="center">
  <img src="docs/screens/settings-device.png" width="360" alt="Settings: device">
  <img src="docs/screens/settings-power.png" width="360" alt="Settings: power and sleep">
  <img src="docs/screens/settings-sound.png" width="360" alt="Settings: sound and volume">
  <img src="docs/screens/settings-admin.png" width="360" alt="Settings: the admin PIN">
</p>

Four tabs. **Device** holds theme, menu layout, the case light, the BLE beacon,
the Network button, the NTP resync interval, the Nearby switch, screen
brightness, and a factory reset behind a two-tap confirm. Nearby greys out and
reads *needs Beacon* while the radio is off — it rides on that radio, and a
switch that flips without doing anything is worse than one that says why.
Automatic NTP resync is **1–24 hours**, default **6 hours**; boot-time sync and
the Network screen's **Sync now** action stay immediate. **Power** holds
the idle policy, its two delays and the hold-to-unlock guard — see [Screen
saver and sleep](#screen-saver-and-sleep). They were one screen until the sleep settings
arrived and there was nowhere left to put them.

**Sound** holds the mute switch and the volume. Mute is a switch of its own
rather than volume zero, so the level survives being silenced and comes back
where it was; muting takes *everything*, including the startup phrase, and
leaves the case LED still flashing green and red. The volume slider is capped
at **85%** and says 85 — a control that relabelled its ceiling as 100% would
read better and lie. That ceiling is a hearing-safety limit for a device held
near a young player's ears, the counterpart of the 25% brightness floor, and it
was set by listening on the Freenove's own driver rather than picked as a round
number. Two test buttons sit under it, because setting a volume
you cannot hear while you set it is guesswork: one plays a cue, the other says
"Let's play Braino!". Dragging the slider plays a note at the new level as you
go. On a board with no audio codec the tab says so plainly rather than offering
controls that do nothing — "this board cannot" and "you have muted it" are
different things to tell an owner. See [Sound](#sound).

**Admin** holds the PIN and **Recalibrate touch**. The calibration wizard runs
on its own only when nothing is stored, which leaves one hole it cannot fill: a
calibration that is *present but wrong* — drifted, or captured by a child
tapping past the three targets — reports as fine and never re-runs, and the
only cure was a factory reset behind a touch target nobody could hit. This is
the way back. It is safe to press by mistake, because the wizard reads the
panel raw and replaces what is stored only if the new three-point fit succeeds;
time it out and the old calibration is still there. Boards with capacitive
panels have nothing to fit and say so instead of offering the button.

Game visibility is **not** here — it is per player, so it lives with the player,
under *Profiles → Edit → Games*. See [The admin profile](#the-admin-profile).

Brightness floors at **25%, not 0**, deliberately: at lower duty the panel is
unreadable and a player who dragged the slider to the bottom could not see the
control needed to undo it.

**Menu layout is launcher-only.** Every game is authored against the fixed
320×240 landscape canvas, so Tall changes the home screen and nothing else.

**Admin** holds the PIN that guards the admin profile. See below.

Like everything else in Settings, the sound controls are **readable by anyone
and changeable only by the admin** — the speaker belongs to whoever is in the
room, so volume is a device setting rather than a per-player one.

### The admin profile

<p align="center">
  <img src="docs/screens/profiles-pin.png" width="360" alt="The admin PIN prompt">
  <img src="docs/screens/settings-pin.png" width="360" alt="Setting a new admin PIN">
</p>

One profile is the **admin**, and it is guarded by a four-digit PIN. It carries
a small padlock in the profile picker, so the lock is visible before the tap
rather than after it.

The PIN is asked for on the two routes *into* the admin profile:

- switching to it,
- opening its **Edit** menu — which reaches rename and that profile's
  per-player game list.

It is asked **every time**, including immediately after a correct entry.
"Already admin" is not evidence that the person now holding the device is the
one who typed the PIN — which is the entire situation this guards against, a
device handed to a player mid-session.

**The admin decides who plays what.** Game visibility is per player, set from
*Profiles → Edit → Games*, and only the admin can change it. Anyone can open
the list and see which games are on or off — a player seeing that something is
switched off is fine, and better than a launcher that is mysteriously short —
but the checkboxes only respond to the admin. Before this the feature enforced
nothing: a player opened their own row and turned back on everything that had
been hidden from them.

**Removing a player is also the admin's call**, because it destroys that
player's scores and mastery data permanently. Any player could previously delete
a sibling in two taps. Renaming is deliberately left open — it is harmless, and
a player wanting their own name spelled properly is not a threat.

**Settings is not behind the PIN.** Anyone can open it and read every page —
there is nothing secret there, and a player being unable to see why the screen
dims is worse than one who can look. What a non-admin cannot do is *change*
anything: every control renders greyed, and every one of them is inert. The
greying and the refusal are separate things, and it is the refusal that
enforces it — the controls were grey and fully live for a while, which is
exactly the bug this arrangement is written down to prevent.

The device also refuses to **boot** into the admin profile: if admin was active
at power-off, the next boot drops to Guest. The picker's *Done* button goes home
with whatever profile is already active, so a remembered admin selection would
have handed out admin with no PIN at all.

The admin profile **cannot be removed** — the Remove button greys out on that
row and says why.

A fresh device ships with the admin profile **Admin** and the PIN **0000**.
Change it from *Settings → Admin → Change admin PIN*: the new PIN is entered
twice and is only written if both entries match. A mistyped PIN stored anyway
would lock the owner out of their own device with no recovery short of a
factory reset.

What the PIN is **not**: it is a parental control, not a security boundary. It
is four digits, there is no attempt limiting, and anyone who can reflash the
board can clear it. It is sized to stop a player changing settings, not an
adversary.

### Nearby

<p align="center">
  <img src="docs/screens/nearby.png" width="420" alt="Nearby: who else is playing">
</p>

**Off by default.** Turned on, the console listens for other Brainos in range
and shows who is there, which game they have open, and their best score for it.
When somebody's score beats the record on this device, a strip appears over the
header for a few seconds and is then removed.

It is anonymous by construction. A peer is **four hex digits of its own
Bluetooth MAC** — the same id already used to name the device — and the only
other things that travel are a game index and a score. No player name, no
profile name, nothing profile-scoped is read by the feature at all. Two
players learn that *someone with a Braino nearby has 9 on Maze* and nothing
whatever about each other.

Two switches guard it, in this order: the **BLE beacon** must be on, and then
**Nearby** must be on. Turning the beacon off stands Nearby down with it. The
scan is passive, so listening never transmits anything.

### Network & Time

<p align="center">
  <img src="docs/screens/network-time.png" width="360" alt="Network and Time">
  <img src="docs/screens/timezone.png" width="360" alt="Time zone picker">
</p>

Wi-Fi exists **only** to set the clock. The time zone is detected from the
public IP on first connect; the picker overrides it with named zones carrying
POSIX rules (`CST6CDT,M3.2.0,M11.1.0`), so daylight saving is handled without
anyone touching it in March and November.

### Screen saver and sleep

<p align="center">
  <img src="docs/screens/screensaver.png" width="360" alt="Pong screen saver">
  <img src="docs/screens/wakelock.png" width="360" alt="Hold to unlock">
  <img src="docs/screens/wakelock-tall.png" width="200" alt="Hold to unlock, Tall layout">
</p>

Pong that plays itself. Paddles sweep opposite ways; every rally speeds the ball
up and advances the colour, mirrored on the case LED. Touching it returns you to
**whatever you were doing** — not the home screen.

After the saver, the device **sleeps**: the backlight goes off and the panel
drops into its low-power state, which is what actually saves the battery — a
lit screen playing Pong to an empty room does not. A touch anywhere wakes it,
and you land back on the screen you left.

Three policies, set in **Settings → Power**:

| Policy | What happens after the idle delay |
|---|---|
| **Saver then sleep** | Pong runs, then the screen blanks after the sleep delay |
| **Sleep only** | The screen blanks straight away — no Pong |
| **Saver only** | Pong runs and the screen never blanks |

Both delays are configurable: **Idle after** (30s / 1m / 2m / 5m) is the wait
from the last touch, and **Sleep after** (15s / 30s / 1m / 2m / 5m) is how long
the saver runs before the screen goes dark.

**Sleep is not `esp_deep_sleep`.** The CPU stays up so it can poll the touch
panel — there is no wake source wired for a true deep sleep on this board. The
main loop drops from 50 Hz to 10 Hz while asleep, and the battery still drains,
just far more slowly than with the backlight on.

#### Hold to unlock

A console in a bag or a coat pocket gets pressed constantly. A touch therefore
**lights the screen and nothing else**: it lands on a lock screen, and only a
deliberate **press-and-hold of about a second** on the unlock button hands the
device back to the screen underneath. A progress bar fills while you hold, so
it is obvious what the device is waiting for. The screen carries the
**Braino!** wordmark and the **battery level** in a header, so somebody who
finds the device locked can see what it is and whether it is about to die
without touching anything. Neither drifts around the panel the way the
screen saver's wordmark does — this screen is up for seconds, not hours.

This is an accidental-touch guard, not a password. It is **nothing to do with
the admin PIN** — unlocking neither grants nor revokes admin, and you come back
to exactly the profile and screen you left, in the orientation you left them in.
It guards the screen saver and panel sleep alike, and it is deliberately
tolerant of the way a resistive panel drops contact mid-press: gaps of up to
150 ms do not restart the hold. If nobody unlocks within 12 seconds the device
goes back to sleep on its own.

Turn it off in **Settings → Power → Hold to unlock** if you would rather any
touch went straight through. It is on by default, and like every device
setting it is global and admin-writable only.

#### Lock now

The padlock beside **Home** — and on the launcher's own header, in both
layouts — blanks the screen at once, without waiting for an idle timeout. It
is the same guard, reached deliberately: for a console being carried, handed
across a car seat, or dropped into a bag with a game still open.

The next touch lands on the same **Hold to unlock** screen above, and a
completed hold puts back the exact screen that was open. Pressing Lock
yourself is a request, so it produces the lock screen even if **Hold to
unlock** is switched off. Nothing is stored, no profile or score is touched,
and the tap is consumed by the shell — whatever sat under the padlock is not
pressed as well.

### The BOOT key is a Home button

The small **BOOT** key on the board — the one the ROM uses for serial download
mode — goes back to the launcher from any screen. It also wakes the panel and
dismisses the screen saver, exactly as a touch does. Useful when a small player
has wandered into a game and cannot find the way out, or when the top bar's
Home glyph is simply harder to hit than a physical key.

It is a shortcut and never the only way to do anything. Everything it reaches
is reachable by touch, so a board that does not wire the key behaves as it
always did. Two places ignore it on purpose: the **Hold to unlock** screen,
because a key pressed through the side of a bag is the accident that screen
exists to catch, and the launcher, where you are already home.

Holding it while the console starts is unchanged and has nothing to do with
this — that is a message to the chip's own ROM, which reads the pin at reset,
long before this firmware runs. **RESET**, beside it, cannot be given a job at
all: it pulls the chip's enable line, so there is no software on either side of
it to notice.

### Scores, Profiles, About and System Info

Four more screens, all of them ordinary `Game` subclasses like everything else:

- **Profiles** -- shown at boot; picks whose scores are being written. Carries
  the Braino! product mark in a branded header, with the copyright line
  beside it.
- **Scores** -- two tabs:
  - **Mine** shows bests and worsts for the active player, per game.
  - **Device** shows the device-wide best and its holder across every player on
    the device. The holder's name appears in gold when it is the current player
    — a small recognition for being on top.
- **About** -- what each game is for, in a parent's words, plus a **What the
  radios do** page. Every fact on that page is read from the running system
  rather than typed in: the playable app list comes from `AppRegistry`, the
  version from the firmware constant, Wi-Fi state from `Board`, and beacon
  state and the advertised name from `BleBeacon`. It cannot drift out of date.
  The last page is **This build** -- the branch, the abbreviated commit and
  the build time, stamped in at compile time by `tools/build_stamp.py`. The
  version number cannot answer "which firmware is on this board?", because
  it is identical across every flash of a release; this can. A build made
  from a source tarball with no git says `unknown` rather than guessing.
- **System Info** -- five tabs of live telemetry: board, memory, network, BLE
  and app state. This is a diagnostics screen, not a toy: chip and reset
  reason, heap with a fragmentation meter, NVS usage and namespace counts,
  Wi-Fi throughput, watchdog stalls, and the branch, commit and build time
  this firmware was built from.

<p align="center">
  <img src="docs/screens/scores-mine.png" width="360" alt="Scores: this player">
  <img src="docs/screens/scores-device.png" width="360" alt="Scores: device best">
</p>

<p align="center">
  <img src="docs/screens/profiles.png" width="360" alt="Profiles: who is playing">
  <img src="docs/screens/about-radios.png" width="360" alt="About: what the radios do">
  <img src="docs/screens/about-build.png" width="360" alt="About: which build is on the device">
  <img src="docs/screens/systeminfo-memory.png" width="360" alt="System Info: heap and CPU">
</p>

### Battery

The gauge reads GPIO34 through the board's divider, converted with the ESP32's
own eFuse ADC calibration rather than a nominal 3.3V reference — at 11dB
attenuation the converter is only linear to about 2.45V and its reference varies
part to part, so the naive `raw / 4095 × 3.3` is wrong twice over.

Percentage comes off a piecewise LiPo discharge curve, not a straight line: a
cell sits near 3.7V for most of its life, so a linear map reads about 20 points
high through the middle.

**Charging is inferred, because this board has no charge-status line.** The
charger's CHRG pin never reaches a GPIO, so the cell voltage on GPIO34 is the
only thing the firmware can see. Three signals are read from it: a step between
consecutive samples (plugging the cable in lifts the terminal voltage well
beyond ADC noise within a couple of seconds, and unplugging drops it back under
load), a voltage held above 4.21V that no resting cell reaches, and — for
everything in between — the direction the voltage has moved over the last 45
seconds. Mid-discharge a LiPo sits on a plateau where 40% of the capacity spans
about 20mV, which is why the slow window has to be that long, and why a window
that comes out genuinely flat leaves the previous verdict standing rather than
flapping between charging and not.

"Charged" is only ever reached from "charging". A pack resting at 4.15V off the
cable and one that has just finished charging read identically from a single
sample, so the firmware will not claim a battery is full unless it watched it
get there.

The **battery icon shows the percentage as a number**, inside the shell, the
way an iPhone or an Android status bar does it — because eleven pixels of fill
is not a figure anyone can read, and "how long have I got?" is a number
question. Underneath the digits a two-pixel gauge still runs along the inside
of the shell, so the analogue cue is there too: green above 40%, amber down to
16%, and at or below **15%** the shell and the digits both go red. That red
outline is what makes *charge me* visible across a room, which is why nothing
else colours the shell. A lightning bolt appears inside the icon whenever the
charger is attached.

**This board cannot tell whether a battery is fitted, and the gauge does not
claim to.** The charger holds its BAT output at float voltage whether or not a
cell is attached, so running on USB with no pack reads *between* the two
states a real pack produces — measured here as 4.159 V with no pack, against
4.066 V on a pack alone and 4.224 V on a pack plus USB. No threshold can
separate them in either direction. With no pack fitted the gauge therefore
reads **high, close to full**, rather than showing an empty or absent battery.
The digits blank only when the ADC reads outside a plausible range, which means
a sensor fault, not a missing pack.

The icon is deliberately **not** a fixed size: it is 22px wide showing `72`
and 36px showing `100` on the charger, and the status rows around it are laid
out from its measured width rather than a constant offset.

At **15% or less** a strip also appears across the top of whatever screen is
open: *Battery low — time to charge*, escalating to *Battery empty — plug in the
charger* at 5%. It shows for six seconds and repeats every two minutes, so it
stays a warning rather than becoming furniture. Plugging in clears it within a
couple of seconds — the warning is driven off the charge verdict, not the
percentage, so it goes away when the user does the thing it asked for rather
than waiting for the reading to climb.

The divider ratio itself is still an assumption pending a meter on the board.

### Reliability

A background **watchdog** supervises the main loop (`src/hal/Watchdog.cpp`). The
ESP32 hardware task watchdog reboots the device if any frame takes longer than
12 seconds, so a hung game cannot leave a player staring at a frozen screen, and
a low-priority monitor task samples frame times and heap once a second, logging
a stall long before that. After an unclean reset the breadcrumb -- which screen
was up, uptime, heap low-water mark -- survives the reboot and is printed on the
next boot.

---

## Privacy

No accounts, analytics, telemetry, or personal-data collection. The optional
Wi-Fi and Bluetooth LE features make only the limited exchanges described
below, and each one is listed. Scores and mastery data live in the ESP32's own
flash and never leave it.

Wi-Fi connects only to reach an NTP server, plus one lookup to `ip-api.com` to
guess the time zone on first connect (the picker overrides it, and you can skip
Wi-Fi entirely). After the first clock set, automatic NTP resync is configurable
from 1 to 24 hours and defaults to 6 hours.

### The BLE beacon

The beacon is **off by default** and opt-in from *Settings -> Beacon*. When it is
on, the launcher header shows a Bluetooth badge -- drawn **only** while the radio
is genuinely advertising, so it is never a question of whether an icon looks
greyed out. It is placed per layout: landscape puts it on the clock's line,
positioned off the measured width of the clock string, because the badge row
there has about 8px of slack; portrait simply extends the badge row.

With Nearby off, what goes on air is a device name and a hardware id. That is
all:

| AD type | Contents |
|---|---|
| Flags | LE General Discoverable, BR/EDR not supported |
| Complete Local Name | `Braino-A4F2` |
| Manufacturer Data | company `0xFFFF`, `"BR"`, layout version, two MAC bytes, flags |

The id is the last two bytes of the factory Bluetooth MAC -- a hardware serial,
stable so you can recognise your own device in a scanner. Nobody types it and it
is not derived from anything a player entered. Advertising is **non-connectable**:
there is no GATT server, so there is nothing to connect to.

**Not broadcast:** player information, player name, profile name, location, Wi-Fi
credentials, Wi-Fi SSID, IP address, game progress, usage history.

That list is structural rather than a promise. `buildPayload()` emits a name AD
and a manufacturer AD and nothing else, and nothing profile-scoped is reachable
from the radio path at all.

### What Nearby adds, and only while it is on

[Nearby](#nearby) is a second opt-in on top of the beacon. While it is on, two
more fields join the manufacturer data:

| Field | Contents |
|---|---|
| Open game | An index into the playable-game list — `12` is Maze |
| Best score | This device's best score for that game |

While it is off, those fields are **absent from the payload** rather than
present and zeroed: the manufacturer block is five bytes shorter and the flag
bit is clear. "Not transmitted" has to be structural to be worth claiming.

Neither field says who is playing. There is no name, no profile, and no way to
get from a score back to a player — the exchange is a leaderboard with nobody's
name on it. Listening is passive, so a console that is only watching transmits
nothing extra.

*System Info -> BLE* reports **Open game** and **Best score** as `Broadcast` or
`Not Broadcast` read from the same structure the controller was handed, so that
row cannot disagree with the radio.

### You can check all of this on the device

<p align="center">
  <img src="docs/screens/systeminfo-ble.png" width="420" alt="System Info: what BLE is broadcasting">
</p>

*System Info -> BLE* shows whether advertising is currently active, the actual
name being advertised, every decoded field of the manufacturer data, the privacy
list above, and -- under *Show advanced* -- the interval, TX power, advertising
type, controller address and a **hex dump of the bytes actually on air** (27 of
the 31 legal bytes with Nearby off, all 31 with it on).

Turn the beacon off and the same screen says *Broadcasting: Nothing*, relabelling
the identity block as configuration so nothing reads as being transmitted when it
is not.

None of that is UI copy describing the payload from memory. There is one
authoritative structure -- `BleBeacon::Advertisement` -- which is compiled into a
raw byte buffer, handed to the controller verbatim, and read back by the screen.
The display and the radio cannot drift apart. The contract is written down in
[`docs/BLE_BEACON_SPEC.md`](docs/BLE_BEACON_SPEC.md).

The governing principle: **if the device transmits something wirelessly, the
owner should be able to see what it is transmitting, from the device itself.**

---

## Version

In development: **5.5.0-SNAPSHOT** — the last release was **5.4.0**. See
[CHANGELOG.md](CHANGELOG.md) for what has changed since.

---

## Build and flash

```bash
pio run -e app -t upload
```

Requires **PlatformIO**. The `huge_app.csv` partition (3 MB) is already set in
`platformio.ini` — the default 4 MB scheme gives the app only 1.31 MB, which is
less than the country artwork alone.

Dependencies resolve automatically:

| Library | Purpose |
|---|---|
| `bodmer/TFT_eSPI` | Display and touch |
| `bblanchon/ArduinoJson` | Optional SD content configs |
| `h2zero/NimBLE-Arduino` | BLE beacon -- ~192 KB for host plus controller, against several times that for the core's Bluedroid stack |
| [`map-n-flag`](https://github.com/iamankushpandit/map-n-flag) | Flag and outline artwork |

Both diagnostic builds below, and the app itself, are also on the
[web installer](#install-it-without-a-toolchain) — useful when the board is not
on the machine that has PlatformIO.

To check the page itself before pushing:

```bash
python tools/gen_site.py
```

That writes `site/_build/` (git-ignored) with the page and one manifest per
firmware. The binaries are *not* built locally; the flash button only works
against the published site, where CI has put them there.

### Diagnostics

An isolated Wi-Fi radio test, built with **no** display, touch or game code:

```bash
pio run -e wifidiag -t upload
```

It scans and prints every network with RSSI, channel and encryption. This is
what proved the radio was fine when the app's scan was returning nothing — the
async `scanNetworks()`/`scanComplete()` pair was silently failing on this board,
while a blocking scan found 58 access points.

The main firmware also traces the clock over serial at 115200:

```
[boot] ntp=1 creds=1 ssid='MyNetwork' tzmin=-360
[time] wifi up, ip=192.168.1.142 rssi=-57
[time] configTzTime US Central (CST6CDT,M3.2.0,M11.1.0)
[time] UDP NTP OK, clock set from pool.ntp.org
[time] SYNCED 2026-08-11 01:00:16 (US Central)
```

---

## Layout of the code

```
include/
  BoardProfile.h        the contract every supported board fills in
  BuildStamp.h          which build this is, and why the time is not a -D
  BoardConfig.h         selects one board profile; derives the screen constants
  boards/
    e32r28t1.h          the 2.8-inch board: pins, rotations, battery divider
src/
  main.cpp              bringup entrypoint + normal app setup/loop
  BuildStamp.cpp        branch/commit/build time; rebuilt every build
  wifi_diag.cpp         standalone radio test (env:wifidiag only)
  battery_diag.cpp      standalone battery/ADC calibration tool (env:batdiag only)
  s3_diag.cpp           standalone ESP32-S3 bring-up probe (env:s3diag only)
  diag4.cpp             standalone 4-inch ST7796 bring-up probe (env:diag4 only)
  audiodiag.cpp         standalone DAC audio bring-up probe (env:audiodiag only)
  engine/
    AppCapabilities.h   system-app capability flags
    AppRegistry.cpp     authoritative app registry + instance bindings
    AppRuntime.cpp      runtime loop, transitions, view state
    AppRuntimeLauncher.cpp  LauncherGame paging, tiles, header UI
    AppRuntimeScreenSaver.cpp  screen saver and panel sleep/wake
    AppRuntimeLock.cpp  hold-to-unlock guard on the way back
    Game.h              base class; lifecycle + full vs partial invalidation
    LauncherGame.h      home screen lifecycle object
    GameCatalog.cpp     derived playable-game catalog view
    ScoreCatalog.cpp    derived scored-app catalog view
    NearbyPlay.cpp      anonymous peer scores, notifications, sharing switch
    Progress.cpp        per-item mastery, spaced repetition
    ContentLoader.cpp   optional SD-card config (everything has defaults)
  games/                one .cpp/.h pair per game and per system app
    SettingsGame.cpp    Settings: lifecycle, the four tabs, touch routing
    SettingsPanels.cpp  the Device, Power and Sound tab bodies
    SettingsPin.cpp     the admin PIN pad and the Admin tab
    CountryData.cpp     capitals, continents, difficulty tiers
    CountryDataTable.cpp  generated -- see tools/gen_country_facts.py
    ElementData.cpp     lookups over the 118-element table
    ElementDataTable.cpp  generated -- see tools/gen_elements.py
    MazeData.cpp        maze layouts kept out of the redraw logic
    StateData.cpp       50 US states: code, name, capital, tier
    TraceGlyphData.cpp  letter/number stroke guides
  hal/
    Board.cpp           board bring-up, profiles, layout/idle settings
    BoardAccess.h       narrow display/touch/storage/power/network/feedback facades
    BoardDisplay.cpp    TFT access, rotation, BMP blitting
    BoardTouch.cpp      touch ADC, calibration, coordinate mapping
    BoardButton.cpp     the BOOT key, debounced by the frame rate
    BoardPower.cpp      battery telemetry, backlight, panel sleep/wake
    BoardNetwork.cpp    Wi-Fi credentials, timezone, NTP sync
    BoardFeedback.cpp   RGB LED, BLE and Nearby toggles
    BoardAudio.cpp      the whole sound engine: cues and the spoken phrase
    Sound.h             the console's sound vocabulary, as an enum
    BoardStorage.cpp    schema migration + app-scoped NVS keys
    BoardStorageMaintenance.cpp  profile moves + NVS telemetry
    TouchTypes.h        TouchPoint event type shared without display deps
    BleBeacon.cpp       the one authoritative BLE advertisement payload
    BleScanner.cpp      passive observer for other Braino beacons
    Clock.cpp           time and date formatting
    Watchdog.cpp        loop supervisor, stall logging, crash breadcrumb
  ui/
    Renderer.h          app-facing drawing interface, no TFT driver include
    TftRenderer.h       TFT_eSPI adapter used by firmware runtime
    LauncherIcons.cpp   launcher tile icon drawing
    LauncherLayout.cpp  launcher header, profile and tile geometry
    Ui.cpp              theme, widgets, badges, map-n-flag blitting
    RowList.cpp         scrolling label/value list; fixed buffers, no heap
tools/
  gen_country_facts.py  regenerates the capital/continent table
  gen_elements.py       regenerates the 118-element table, checking it
  gen_screens.py        regenerates the images in this README
  gen_site.py           builds the GitHub Pages site and its flash manifests
  check_docs.py         fails if these docs have drifted from the code
  check_boards.py       fails if a board is described inconsistently
  check_catalog.py      fails if launcher metadata indices misalign
  check_frame_rules.py  ratchet on heap/String/delay in render paths
site/
  index.template.html   the landing page, with {{PLACEHOLDERS}} gen_site fills
.github/
  workflows/ci.yml      runs checks and builds every firmware on a clean runner
  workflows/pages.yml   publishes the site with the same built firmware
  ISSUE_TEMPLATE/       bug report, board port, game or feature idea
  pull_request_template.md
docs/
  BLE_BEACON_SPEC.md    what the beacon broadcasts, and why that is checkable
  PORTING.md            adding a board: the two files, and the bring-up order
  SD_CONTENT_SPEC.md    optional SD content format
cases/
  <BOARD_NAME>/         printable enclosure for that board -- STL, 3MF, print notes
```

`AppRegistry` matters more than it looks: each playable game declares its own
metadata once, including launcher icon, order and default visibility, and the
registry binds that metadata to its concrete game instance alongside the
launchable system apps. That replaced the old split between `CATALOG_KINDS[]`,
`launchKind()` and the icon switch in `main.cpp`, which could drift silently.

---

## Credits and licensing

Code in this repository is licensed **GPL-3.0-or-later** — see
[LICENSE](LICENSE). Copyright © 2026 iamankushpandit.

The copyright and the brand are held by different parties on purpose, and
[NOTICE.md](NOTICE.md) is the short statement of which is which: the code is
the individual's and comes to you under the GPL, while **Braino!**, the game
names and **GoodTime Micro Company™** are trademarks. The licence does not
grant a name — no licence does — so NOTICE.md also spells out the one thing a
fork is asked to do before shipping: rename. That is a single header,
[`include/AppVersion.h`](include/AppVersion.h), because the firmware spells
the product exactly once.

In short: you may use, study, modify and redistribute it, but if you
distribute a modified version or a device running one, you must offer the
corresponding source under the same terms. That is deliberate — this is a
project for players' hardware that people are invited to port, and the ports
should stay available to the people who own the devices.

The bundled artwork and libraries keep their own, more permissive licences, all
of which are compatible with GPL-3.0: `TFT_eSPI` (FreeBSD), `ArduinoJson`
(MIT), `NimBLE-Arduino` (Apache-2.0, which is GPLv3-compatible) and
[map-n-flag](https://github.com/iamankushpandit/map-n-flag) (MIT). Combining
them under GPL-3.0 does not relicense them; it licenses *this* work.

| Asset | Source | Licence |
|---|---|---|
| Country flags | [lipis/flag-icons](https://github.com/lipis/flag-icons) | MIT |
| US state flags | [fonttools/region-flags](https://github.com/fonttools/region-flags) | Public domain |
| US state outlines | [Natural Earth](https://www.naturalearthdata.com/) | Public domain (ODC PDDL) |
| Capitals / regions | [mledoze/countries](https://github.com/mledoze/countries) | ODbL |

**Every asset compiled into this firmware is MIT or public domain.**

This used to carry a warning that the country outlines came from
[djaiss/mapsicon](https://github.com/djaiss/mapsicon), whose terms are *"don't
resell them - I forbid it!"*, making the build unsuitable for commercial use.
That artwork went when the Countries game was removed. The current binary
contains **no mapsicon data and no reference to it** — `mnf_map()` is never
called and the library ships no country-outline arrays. The US state outlines
that replaced them are Natural Earth, which is public domain.

The remaining obligation is attribution, not restriction: `mledoze/countries`
is ODbL, so the capital/region data must keep its credit. Note also that the
Arduino-ESP32 core is **LGPL** — distributing a statically-linked binary
carries relinking obligations.
