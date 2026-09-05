# Changelog

## 5.6.0-SNAPSHOT — Unreleased

In development on `dev`. Nothing here has shipped; the version carries the
`-SNAPSHOT` suffix so a board on a desk cannot be mistaken for the 5.5.0
release, and About's **This build** page names the branch and commit.
`release.yml` refuses to publish a tag whose version carries this suffix.

Two things 5.5.0 left open, both worth more than a feature:

The **E32R28T-1 and ESP32-2432S028R are silent** even though they compile the
DAC audio backend. IO4 is declared as the RGB green channel in their board
profiles while the vendor pin table calls it the amplifier enable, and both
cannot be true. Whatever drives IO4 as an LED holds the amplifier in shutdown,
exactly as it did on the 4-inch before that board was fixed. It needs a meter
or one test build, not an argument.

The **two ESP32-2432S028 variants have still never been run** on the hardware
they name, and are still offered from the installer page. That has been open
since 5.2.0.

Also outstanding, and recorded in `docs/RENDER_AUDIT.md`: nine quiz screens
carry only the mechanical half of the render split and still redraw their whole
body on a tap, and the Wi-Fi keyboard repaints thirty keys to change one
character of a password.

## 5.5.0 — 2026-09-05

Sound on a second family of boards, and a console that stops redrawing the
whole screen to change one thing on it.

The 4-inch E32R40T now makes noise. It has no codec -- GPIO26 is an ESP32
built-in DAC feeding an onboard power amplifier -- so the existing synthesiser
gained a second output backend rather than a second voice: every cue, and
Cinnamon's pitched pads, come from the same code the Freenove uses. The spoken
boot phrase does not survive 8-bit quantisation and is not expected to.

Every screen used to wipe and redraw all of itself for any change at all. A
full repaint is about 150 KB over SPI and 30 ms of blanking against a 20 ms
frame budget, and twice that on the 4-inch panel -- so three frame budgets to
recolour one button. Screens now paint what changed: one card of twenty-four in
Memory, one cell of eighty-one in Whack-a-Mole, the tiles but not the header
when the launcher pages.

The open question from 5.2.0 is still open: the two ESP32-2432S028 variants are
offered from the installer page and have never been run on the hardware they
name. A report from anybody who owns one is worth more than any feature here --
`docs/PORTING.md` is the checklist.

Known: the E32R28T-1 and ESP32-2432S028R compile the DAC backend but stay
silent, because IO4 is declared as the RGB green channel there and the vendor
pin table calls it the amplifier enable. The two cannot both be true, and it is
not resolved on hardware yet.

### Added

- **Screens repaint what changed, not the whole panel.** `Game` and `AppGame`
  gained a two-phase render: `renderStatic()` runs only when the layout changed,
  `renderDynamic()` runs on every repaint. `Ui::clear()` belongs in the first
  and only the first. The old `render()` is now a default that dispatches
  between them, so a screen that has not been converted behaves exactly as it
  did and conversion happens per screen.

  This was always the intent -- `markDirty()` and `markFullDirty()` have said
  which was needed since the beginning -- but a single `render()` opening with
  an unconditional `Ui::clear()` threw the distinction away one line later, on
  29 of the 40 screens that clear. A full repaint is ~150KB over SPI and about
  30ms of blanking against a 20ms budget; on the 480x320 board it is twice that,
  so three frame budgets to change one tile. The bigger panel did not introduce
  this, it removed the margin that was hiding it.

  **The launcher is converted and is the reference for the pattern.** Paging
  already called `markDirty()`, so the invalidation was correct and was being
  discarded. Next/previous now leaves the header alone, and does not redraw the
  tile chrome either: a tile's rect comes from its slot and its colour from
  `slot % 3`, so the button is pixel-identical between pages while its contents
  are not. `Ui::drawButton` pushes two full tile areas plus a bevel and an
  outline; on a content change the interior is erased with one plain fillRect
  instead.

  Converting it turned up the two things that do not erase themselves once the
  screen stops being wiped, which is what the per-screen work has to look for:
  an empty tile slot on a short last page used to `break` out of the loop and
  now has to be painted over, and the `"%u/%u"` pager text is drawn with an
  opaque background that only covers the glyphs it draws, so `10/12` -> `9/12`
  left the trailing character behind.

- **Sound on the 4-inch E32R40T, from the same synthesiser as the Freenove.**
  GPIO26 is ESP32 DAC channel 2, and the LCDWIKI schematic for this board runs
  it through an RC filter into an onboard 8002-series mono power amp whose
  shutdown input is GPIO4, active low. So the I2S peripheral drives the DAC
  directly via `I2S_DAC_BUILT_IN` and the amplifier does the rest -- no codec,
  no extra wiring, and no second sound vocabulary: every cue in `Sound::` and
  Cinnamon's four pitched pads come from the existing synthesiser, unchanged.
  Volume is linear amplitude scaling, not the dB curve the ES8311 register
  needs.

  **Validated by ear on the E32R40T only.** The E32R28T-1 and ESP32-2432S028R
  compile the same backend and are expected to work -- the vendor pin table is
  the same family -- but neither has been heard, and neither has its amplifier
  enable described yet. On the E32R28T-1 that is blocked on a real
  contradiction: the vendor table gives RGB as IO22/IO16/IO17 with IO4 as the
  audio enable, while this repo's profile claims IO4 is the green LED on
  measured grounds. Both cannot be true, and until it is resolved that board
  stays silent, because whatever drives IO4 as an LED holds the amplifier in
  shutdown.

  **The spoken boot phrase does not survive an 8-bit DAC.** The cue vocabulary
  does. The voice is formant synthesis, and it depends on quiet resonator
  ringing between excitation impulses; at 8 bits, scaled by a volume setting,
  that detail quantises away. The Freenove's codec is 16-bit -- 256 times the
  resolution -- so the two boards genuinely do not sound alike and no amount of
  tuning here will make them. This is a property of the hardware, not a defect
  to chase.

  The audio backend is now three-state rather than binary:
  `GUME_HAS_AUDIO_CODEC` for the codec path, `GUME_HAS_AUDIO_DAC` for the
  built-in DAC, and neither for boards with no speaker path. The two macros
  default to 0 in `BoardConfig.h`, so any file can test them.

  `AUDIO_VOLUME_MAX` moved from a hard-coded constant into `BoardProfile`
  (`audio.maxVolume`), so each board states its own ceiling: 85 for the
  Freenove (unchanged, set by listening), 75 for the DAC boards (not yet set
  by listening -- the vendor rates the amp at 1.5W into 8 ohms).

  `env:audiodiag` is a new standalone probe (same pattern as `batdiag`). It
  answers the three questions silence cannot distinguish, in one reset and
  without the panel or a button: a PWM burst that uses no DAC at all proves the
  speaker, the amp and the enable line; an I2S tone proves the DAC path; and a
  software-timed direct `dacWrite()` tone separates the I2S framing from the
  DAC peripheral itself.

## 5.4.0 — 2026-09-04

A fifth board, and the first one bigger than the canvas the games are drawn
on. Supporting it meant finding every place the firmware had quietly assumed
those two were the same size, which was most of the places that draw.

### Added

- **The 4-inch E32R40T is supported, measured on hardware.** An ST7796
  320x480 panel running 480x320 in landscape, with XPT2046 resistive touch
  sharing the display's SPI bus. It is offered by the web installer and all 31
  games run on it.

  This board is here for a particular audience: a physically bigger, plainer
  screen for players who need one. That is also why playable games reach it
  through `Ui::ScaledRenderer`, which stretches their fixed canvas to fill the
  panel, and why the launcher grows its tiles and the type inside them rather
  than leaving small writing in a large box.

  Every panel fact was measured with a new standalone probe rather than
  inherited: the display bus turns out to be identical to the 2.8-inch board's,
  pin for pin, and **exactly one pin differs -- the backlight is GPIO27.**
  GPIO21, the obvious assumption and the value the 2.8-inch board uses, was
  measured dark twice. That is the pin that fails silently: with the wrong one
  the panel is black while Wi-Fi, BLE, NVS and the screen saver all run
  perfectly, so the log looks healthy and it reads as a dead screen.

  Four peripherals are declared absent rather than guessed -- the SD slot,
  and until measured the LED, speaker and battery sense, which are inherited
  from the E32R28T-1 and annotated with what would disprove each. A wrong
  battery pin does not fail loudly; it reports a plausible fiction.

- **`env:diag4`, a bring-up probe for that board.** Eight pages over serial:
  who is on the SPI bus, a backlight sweep across candidate pins in both
  polarities, geometry and colour order, rotation, both touch wirings, ADC
  candidates, and Wi-Fi and BLE separately and then together. It defines
  neither `TFT_BL` nor `GUME_BOARD_HEADER` on purpose -- a probe must not
  depend on the answer it exists to find.

- **Braino has a voice and a sound vocabulary.** On a board whose profile
  describes a codec it says "Let's play Braino!" at boot and has a vocabulary
  beyond right and wrong. Nothing is a recording and nothing may become one:
  every sound is generated a sample at a time, because a second of 16-bit
  16kHz mono is 32 KB and the app partition is already three quarters full.
  The voice is formant synthesis -- the phrase spelled out as phonemes, three
  resonators driven by a buzz for vowels and by noise for consonants -- which
  is why it sounds like a 1980s home computer. Boards with a bare speaker pin
  are unchanged: the LED pulse is still the whole of the feedback.

- **A way back from a bad touch calibration, and BOOT as a Home key.** The
  wizard only ran when nothing was stored, so a calibration that was present
  but wrong reported as fine and never re-ran -- leaving reflashing over USB
  as the entire recovery path. Settings -> Admin -> Recalibrate touch is the
  control. The BOOT key returns to the launcher, wakes the panel and dismisses
  the saver; a board that wires no key behaves exactly as before.

### Fixed

- **Wi-Fi rebooted the console whenever the beacon was on.** `esp_wifi` will
  not share the radio with a Bluetooth controller unless Wi-Fi is
  power-saving, and it does not degrade or return an error -- it calls
  `abort()`. Scanning disabled modem sleep to get a complete network list, so
  opening the Wi-Fi screen with the beacon enabled rebooted the device, and
  rebooted it again on the retry. The beacon is off by default, which is why
  this survived: it only bit an owner who had turned it on.

- **Games were laid out against the panel instead of their own canvas.** A
  playable game draws into a fixed 320x240 canvas that is then stretched, so
  `SCREEN_WIDTH / 2` asked for the panel's midpoint in canvas units and landed
  half as far again across. Every centred element sat 120px right on the
  4-inch board. Corrected across 29 files.

- **The launcher tile grid never adapted.** Its gear, profile chip, lock and
  top bar all measured the live panel; the tiles alone were fixed at the
  320px geometry, so they stopped two-thirds of the way across on the first
  screen anyone sees.

- **Artwork tore into bands.** Image blits are one scanline per address
  window, so through a scaling renderer each 1px row became 1.33px and
  consecutive rows alternately overlapped and gapped. Flags and maps now blit
  1:1 to the panel and are centred in the space the layout gives them; they
  are never resampled, so an enlargement is a whole-pixel replication.

- **Pie slices detached from their circles.** `fillCircle` uses the averaged
  axis scale so it stays a circle, while the wedge points were scaled per axis
  and landed on an ellipse -- bulging past the rim at the sides and falling
  short top and bottom.

- **Scores, Settings and Wi-Fi were drawn for one screen size.** Scores had
  not a single reference to the panel's size. Wi-Fi wrote seventeen of its
  rects out twice, once to hit-test and once to draw, so any change had two
  places to go wrong -- its geometry is stated once now. All three fill the
  panel, and portrait becomes possible for them for the first time.

- **A sync badge that had been wrong since before this board.** Its x was a
  design-space origin added to a measured text width. At 320 the two spaces
  coincide and it looked right; anywhere else the badge lands short of the
  text it belongs to.

### Changed

- `TouchProfile` gains `irqUsable`. The resistive gate accepts a press when
  the IRQ reads low **or** pressure is high, so an IRQ with no pull-up fitted
  does not degrade touch -- it defeats it, reporting a touch on every poll
  forever. Whether the resistor is fitted is a property of the board, so it is
  stated rather than inferred, and filled in for all five boards.

- `CLAUDE.md` and `AGENTS.md` now say that features and fixes start from `dev`
  and that the rule is not an agent's to overrule -- with the specific
  instruction to `git fetch` before forming an opinion about a branch, after
  an agent branched from `main` on the strength of local refs it had never
  fetched.

## 5.3.0 — 2026-09-02

A fourth board, and the first one this project can honestly say it has run.

The Freenove FNK0104B is an ESP32-S3 with a capacitive touch panel, and
supporting it meant the board contract had to stop assuming a resistive
controller — so `TouchProfile` now describes either kind, and `AudioProfile`
grew from a single speaker pin into something that can describe a codec.
Braino makes sounds for the first time.

Unlike the two ESP32-2432S028 variants, this port was not built from a
published pin map. Display, backlight, touch at all four rotations, battery
sense, the codec and the speaker were each confirmed on a device before the
values were written down, using a new bring-up probe that ships with it.

Three of that board's peripherals are switched off rather than half-wired,
each because the profile cannot yet describe the hardware, and each with an
issue carrying the pin map and the measurements needed to finish it. The
README says plainly what a user will notice.

The older open question is unchanged and still worth more than any feature
here: the two ESP32-2432S028 variants are offered from the installer page and
have never been run on the hardware they name. If you own one, telling us
whether it works is the most useful thing you can send — `docs/PORTING.md` is
the checklist, and the board-port issue template asks for the pin map and its
source.

### Added

- **The Freenove FNK0104B is supported, on real hardware.** A 2.8-inch
  ESP32-S3 board with the same ILI9341 240x320 panel, an FT6336U *capacitive*
  touch controller, 16 MB flash and 8 MB PSRAM. It is offered by the web
  installer and all 31 games run on it. Unlike the two CYD variants, this port
  was brought up on a device rather than from a published pin map: display,
  backlight, touch at all four rotations, battery sense and the audio path were
  each confirmed by measurement.

  Three peripherals are deliberately switched off rather than half-wired, each
  because the board profile cannot yet describe the hardware: **sound** (an
  ES8311 codec, not a speaker pin), the **status LED** (one WS2812, not three
  PWM channels) and the **SD card** (SDMMC, not SPI). Each degrades quietly and
  each has an issue with the pin map and the measurements needed to finish it.
  The battery percentage is correct; the charging verdict is not yet validated
  on this board's charger.

- **Braino makes sounds, on a board whose profile describes a codec.**
  `beepOk()` and `beepError()` play a rising two-tone and a low double buzz
  through the ES8311 on the Freenove FNK0104B, as well as pulsing the LED where
  one exists. `AudioProfile` grew from a single speaker pin to something that
  can describe a codec: an I2C control address, the five I2S lines and an
  amplifier enable with its polarity.

  A beep is *rendered* into a static buffer when it is asked for and *fed* to
  the I2S DMA a slice at a time by `tickAudio()`, which the runtime calls once
  per frame beside `tickRgb()`. It has to work that way: `beepOk()` is called
  from game code inside a 20ms frame budget, and playing a 200ms note the way
  the bring-up probe does would blow that budget on every correct answer in
  every game. Volume is capped at `AUDIO_VOLUME_MAX = 80` -- a ceiling in the
  same spirit as `BRIGHTNESS_MIN`, for a handheld held near a child's ears --
  and the amplifier is powered only while something is playing.

  Boards with a bare `speakerPin` are unchanged: `beep()` is still a stub and
  the LED pulse is still the whole of the feedback.

- **The board contract describes capacitive touch.** `TouchProfile` carried an
  XPT2046 and nothing else, so a board wiring an I2C controller could not be
  described at all. It now carries a `TouchKind` and both pin sets, and
  `BoardConfig.h` asserts per arm rather than demanding SPI lines of every
  board. A capacitive panel reports pixels, so it never enters the calibration
  wizard -- a screen its owner could not otherwise get past.

- **`pio run -e s3diag`**, a standalone bring-up probe for ESP32-S3 boards:
  panel, rotation, I2C scan, live touch mapping, battery, and the full audio
  path including a record-and-playback microphone test. Built alone, in the
  same spirit as `wifidiag` and `batdiag`.

### Fixed

- **Turning the BLE beacon on made the screen flash every couple of seconds.**
  Three things change the header on their own schedule rather than the screen's
  -- the clock, the battery badge and the notification banner -- and each was
  answered with a full repaint: ~150 KB over SPI and about 30 ms of visible
  blanking, to change something in the top 30 pixels of a 240-pixel panel.

  The battery is what made it constant. One percent is roughly 2 mV on the
  mid-discharge plateau, and with the divider halving the cell before the ADC
  sees it, a single ADC count is worth most of a percentage point -- so the
  mapped percentage crossed a boundary far more often than the pack actually
  discharged. Switching the beacon on widened the supply ripple enough to make
  that continuous, which is why it looked like a BLE fault rather than a gauge
  one.

  Both halves are fixed. `Game::renderChrome()` repaints the header strip
  alone, an eighth of the panel and comfortably inside the 20 ms frame budget
  where a full repaint is 150% of it; screens carrying their own header
  override it, and one that cannot repaint its chrome in isolation asks for the
  old behaviour. And the displayed percentage now carries a two-point deadband,
  so the number itself stops twitching. Both endpoints are exempt -- "100%" on
  the charger and "0%" about to die are the readings people act on. A side
  effect worth having: the low-battery banner can no longer flap on and off
  while the reading sits on its threshold.

- **The battery gauge was sampled from the render path.** Eight ADC reads and
  both filters ran inside whichever frame found the 2 s cache expired, and that
  frame was almost always a top bar being drawn -- so the filters advanced on
  the UI's cadence rather than on a clock. Sampling now runs on its own
  priority-1 task pinned to core 0, the same shape as the watchdog monitor, and
  every accessor does nothing but copy the published snapshot. Readers take the
  voltage, the charge verdict and the percentage from one sample rather than
  three, so a "battery low" can no longer be reported about a pack the same
  sample knows is charging.

- **Opening the Wi-Fi screen with the BLE beacon on rebooted the device.**
  `WifiGame::runScan()` disabled Wi-Fi modem sleep before scanning, because a
  sleeping radio misses probe responses and the network list came back short.
  That is true, and esp_wifi also refuses to share the radio with a Bluetooth
  controller unless Wi-Fi is power-saving -- and it does not degrade or return
  an error, it calls `abort()`:

  ```
  E wifi: Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled!!!!!!
  abort() was called
  ```

  From the outside this looked like "Wi-Fi is broken": the console restarted
  every time the owner tried to connect, and restarted again on the retry. The
  beacon is off by default, which is why this survived since it was introduced
  -- it only bites someone who turned the beacon on and then went to the Wi-Fi
  screen, and then it bites every single time.

  Modem sleep now stays on whenever the BT controller is up. The condition is
  asked of the controller directly rather than of `BleBeacon`, because that is
  the exact thing esp_wifi tests and the two cannot then drift; going through
  the beacon's own state would leave a gap wherever the controller is up for
  another reason, such as the Nearby scanner. The cost is a scan that may miss
  a distant access point while the beacon is on, which is the right trade
  against a reboot.

  `src/wifi_diag.cpp` has the same line and keeps it: that environment is built
  alone, with no BLE in the image, so there is no controller to conflict with.

- **The console was almost inaudible, and it was a unit bug.** ES8311 register
  0x32 is the DAC volume and it is linear in **decibels** -- half a decibel per
  step, `0xBF` unity, `0x00` silence. Both the firmware and `src/s3_diag.cpp`
  scaled the percentage straight onto the byte, which treats a logarithmic
  register as a linear one and is wrong by up to 25 dB. The default 60% was
  landing on `0x98`, which is **-19.5 dB** -- a tenth of the amplitude the
  number implied, and too quiet to hear across a room. The error was worst
  exactly where people leave a volume control, in the middle; at the far end it
  failed the other way, with 100% mapping to `0xFF`, **+32 dB**, which would
  have clipped every sound into a square wave.

  A percentage is now a fraction of amplitude -- 100% unity, 50% -6 dB, 10%
  -20 dB -- clamped at unity, since above 0 dB the register is digital gain on
  a signal already near full scale and buys only clipping. The probe was
  corrected in the same change: a bench instrument that disagrees with the
  product about what "80" means is worse than no instrument.

  Three other things were quiet independently, and all three are fixed:
  the synthesiser's output scale (26000 to 32000, with every cue's amplitude
  raised in proportion -- topping out at 55 on a 1-inch driver was leaving
  range unused); the formant bandwidth (90 Hz to 60 Hz, which is a *loudness*
  control here, because the excitation is one impulse every 143 samples and
  what fills the gap is the resonators ringing -- at 90 Hz the ring had decayed
  to a tenth before the next pulse, so most of every vowel was near-silence);
  and the fricative makeup gain, which is what the S in "Let's" was losing.

- **"Let's play Braino!" is slower, and now intelligible.** 1.45 to 2.4 seconds
  for four syllables, about half conversational rate. Synthetic speech with no
  pitch movement carries none of the prosody a listener leans on, so duration
  is the only cue left for where one sound ends and the next begins. The
  stressed vowels are now the long ones, and each diphthong is three steps
  rather than two -- F2 climbs around 550 Hz across an "EY" and the ear tracks
  that sweep, where a single jump was audible as a click at the join and read
  as two vowels.

- **The profile name entry has a way out.** The keyboard shared by **Add
  Player** and **Rename** could only be left by committing: `DEL` clears
  characters and `OK` commits, and `OK` on an empty field created a
  default-named profile -- so the give-up gesture a player reaches for made
  the very profile they were trying to avoid, and removal is admin-only. There
  is now a **Cancel** in the corner the admin PIN's Back button already owns,
  and `OK` on an empty field errors instead of writing. The name entry also
  gets its first mock-up in `docs/screens/`, now on the site with the rest of
  the Profiles screens.

## 5.2.0 — 2026-08-27

The release that stops assuming one board. The installer picker now offers
three, the build is pinned so the figures below mean something to somebody who
did not measure them, and the privacy claim says what the firmware actually
does rather than an absolute the same page then contradicts.

**Read this before flashing a CYD.** The **E32R28T-1 / ESP32-32E** is the board
this firmware is developed and tested against, and it is the only one that has
been. The two **ESP32-2432S028** variants are ports built from published pin
maps and vendor board definitions; nobody has run this build on either. They
are offered rather than held back because a port that no owner can try never
gets tested — but "offered" is not "supported", and the page and the README both
say so. If you own one, a report either way is the most useful thing you can
send. `docs/PORTING.md` is the checklist.

Two boards sold under the same name can carry different display controllers, so
the picker asks which variant you have. Flashing the wrong image gives inverted
colours, dead touch or a blank screen — not an error message.

Flash 2,353,205 / 3,145,728 (74.8%), RAM 72,588 / 327,680 (22.2%), measured on
`env:app` for the E32R28T-1.

### Added

- **The ESP32-2432S028 joins the picker, as two entries rather than one.** The
  marketplace name covers at least three display revisions, and they differ in
  ways that fail silently: the original micro-USB board is an ILI9341 with the
  backlight on GPIO21, MISO on GPIO12, SPI mode 0 and BGR order, while the Rv3
  dual-USB board is an ST7789 with the backlight on GPIO27, no MISO wired, SPI
  mode 3 and RGB. One profile covering both would be wrong for whoever owns the
  other. Battery sense is GPIO35 on this family, not the GPIO34 the E32R28T-1
  uses — GPIO34 is the light sensor there, which is exactly the class of
  difference that produces a plausible wrong reading instead of a failure. The
  classic variant also has the RGB LED and speaker the Rv3 lacks. Each is a
  profile header under `include/boards/` plus a `[board_*]` section, per
  `docs/PORTING.md`; no file under `src/` learned a new pin number.

- **Ten environments, built and offered.** `app`, `bringup` and `batdiag` now
  exist for each of the three boards, alongside the board-independent
  `wifidiag`. CI and Pages build the set, and `gen_site.py` derives the picker
  from the `[board_*]` sections, so a board cannot be added to the tree without
  becoming flashable from the page — or failing the check that says it is not.

### Changed

- **CI builds what the change can break, not everything, on a pull request.**
  `app` always builds because it is the product; `bringup`, `batdiag` and
  `wifidiag` build when HAL, board, battery or Wi-Fi files are touched. Pushes
  to `main` and `dev` stay conservative and build the lot. Ten environments per
  documentation typo was the alternative, and it is the reason nobody wanted to
  wait for CI.

- **The build is reproducible.** `platform = espressif32` was bare and every
  library carried a caret range, so a checkout of a fixed commit resolved to
  whatever was newest that day -- a different Arduino core, a different Xtensa
  toolchain, a different ArduinoJson. `^7.0.4` had already floated to 7.4.3 in
  practice. Worse, `map-n-flag` tracked its default branch, so the flag and
  outline artwork could change under a release build without a byte changing
  here. Everything is now pinned to what the documented flash and RAM figures
  were actually measured on: `platformio/espressif32@7.0.1` (Arduino core
  3.20017.241212, toolchain-xtensa-esp32 8.4.0+2021r2-patch5), TFT_eSPI 2.5.43,
  ArduinoJson 7.4.3, NimBLE-Arduino 1.4.3, and `map-n-flag` at commit
  `3c412cb` -- the upstream carries no tags, so the commit is the only
  immutable handle. Nothing is upgraded; this pins the known-working build so
  the size figures in `README.md` and `CLAUDE.md` mean something to a
  contributor who did not measure them. A clean-slate resolution reproduces
  `env:app` at 2,353,205 bytes flash and 72,588 bytes RAM on the machine the
  documented figures were read from. It is not bit-identical across hosts: the
  same commit built on the Linux runner comes out 172 bytes smaller in flash
  and 48 smaller in RAM. Pinning fixes what the build is made of, not which
  toolchain binary assembles it, and `check_docs.py` compares the documents
  against your own `.pio` ELF -- so the figures here are a local reading, not a
  claim about the image CI publishes.

- **The privacy claim no longer overstates itself.** The README, the installer
  page and the About app's credits page each said some form of "the device
  sends nothing anywhere" -- while the same documents described NTP, the
  `ip-api.com` time-zone lookup and the BLE beacon a few lines below, and
  About's own radio page shows the beacon broadcasting live. Nothing about
  what the firmware does has changed, and the strong claims are all still
  made: no accounts, no analytics, no telemetry, no personal-data collection.
  The absolute one is gone, replaced by wording that names the optional
  exchanges it excepts. On the device, the credits page now points at *What
  the radios do* rather than restating a promise -- the same derive-rather-
  than-restate rule as the game list. `tools/check_privacy.py` now fails the
  build on the absolute forms, and equally on a public document that drops the
  strong claims instead of correcting them.

- **Contributor build instructions match the tree again.** `CONTRIBUTING.md`
  claimed `pio run -e app -e bringup -e wifidiag` built "every firmware
  environment". It omitted `batdiag` for most of the project's life, and once
  two more boards arrived it was missing seven of ten. The command is now
  derived from `platformio.ini` the same way `release.yml` derives it, so it
  cannot drift again, with the default board's four given separately for
  everyday work.

- **The site's board-porting guidance describes the current architecture.** It
  said Braino targets one board and pointed at `AGENTS.md`; it now says a board
  is a profile under `include/boards/<id>.h` plus a `[board_<id>]` section in
  `platformio.ini`, and links `docs/PORTING.md`. Stale references calling
  `include/BoardConfig.h` the place pins are defined are corrected in
  `BOARD_E32R28T-1.md` and `cases/README.md` -- that file selects a profile and
  static_asserts against it, and holds no pin of its own.

- **The installer's site generator now runs on every pull request.**
  `tools/gen_site.py` writes the landing page and the esp-web-tools manifests
  behind the flash button, and until now nothing but the Pages workflow ever
  executed it -- so a generator failure was found after merge to `main`, as a
  dead installer rather than a red pull request. It is now a step in the
  required `verify` job, and deliberately **ungated**: it runs even when the
  firmware build is skipped, because changes under `site/`, `docs/`, `tools/`
  and `.github/` are both the ones that gate skips and the ones most likely to
  break the installer. It needs no PlatformIO and adds seconds.

- **Pages fails fast.** The Pages workflow generated the site *after* building
  ten firmware environments. Site generation now runs first, so a missing
  placeholder or an orphaned still is caught in seconds instead of after
  twenty minutes of compiling. The firmware set it builds is unchanged.

### Fixed

- **The installer page generates again with more than one board.** Generalising
  `site/index.template.html` for multiple boards removed the `{{BOARD}}` and
  `{{BOARD_BUY_URL}}` placeholders but left `tools/gen_site.py` substituting
  them, so `check_docs.py` failed and `gen_site.py` itself refused to run --
  its single-board guard fired the moment a second `[board_*]` section
  appeared. Both halves are now consistent: the substitutions are gone, and so
  is the guard, whose stated precondition was exactly the generalisation that
  has since happened. The guards that matter are untouched -- a board still
  needs an environment that builds it and a `BOARD_DETAILS` entry naming it,
  so "supported" still means "flashable from the page".

## 5.1.0 — 2026-08-27

The release that opens the project up. Everything a stranger needs in order to
build it, port it, report a fault or send a change is now in the repository:
a security policy, a code of conduct, issue and pull-request templates, a
licence split that says plainly what the GPL grants and what the name does not,
and a release that is cut by pushing a tag rather than assembled by hand.

Also here: the lock screen says which device it is, the battery reports its
charging state honestly, and the firmware can say which build it is carrying.

**Supported hardware is the E32R28T-1 / ESP32-32E alone.** Support for further
CYD variants exists on `dev` and is deliberately held back from this release.
Supported means someone holding that board can flash it from the installer page
and have it work; no second board has cleared that bar yet, and shipping one
that has not is how a project acquires users it cannot help. Porting is the
single most useful thing a contributor can do here — see
[docs/PORTING.md](docs/PORTING.md).

Flash 2,353,221 / 3,145,728 (74.8%), RAM 72,588 / 327,680 (22.2%).

### Added

- **Releasing is a tag now.** `.github/workflows/release.yml` builds every
  environment `platformio.ini` declares, packs them with the new
  `tools/pack_release.py`, and publishes a GitHub release carrying all four
  flash parts plus a single `-merged.bin` per environment, `SHA256SUMS.txt`
  and `FLASHING.txt`. 5.0.0 was assembled by hand -- four builds, sixteen
  copies, four `esptool` invocations and notes typed out beside a changelog
  that already said the same thing.

  Two things are derived rather than restated, both because the hand-kept
  version of each has already failed here. The environment list comes out of
  `platformio.ini`, since `pages.yml` hard-coded its own and every Pages
  deploy died silently for two pushes. The flash mode, frequency and size are
  `keep`, so `esptool` reads them from the bootloader the build just produced
  rather than from a fourth copy of the board's description -- verified
  byte-identical to the explicit flags.

  The packing is a script rather than workflow YAML so it can be run against a
  local build: `python tools/pack_release.py --out dist`. YAML is only ever
  executed on a tag, which is the worst moment to learn it was wrong.

  The workflow refuses to publish if the tag disagrees with `BRAINO_VERSION`,
  or if the version is a `-SNAPSHOT`. `dev` carries a snapshot between
  releases by design, so without that guard the first mistaken tag would
  publish a "release" the firmware itself describes as unreleased -- and a
  published release cannot be quietly corrected.

- **The lock screen carries a header.** The **Braino!** wordmark and the
  battery level, with the copyright line beneath them. Somebody who finds the
  device locked in a bag can now see what it is and whether it is about to die
  without touching anything -- previously the only screen they could reach
  without unlocking said nothing at all. Nothing drifts the way the screen
  saver's wordmark does: the saver is up for hours and has to worry about the
  panel, this screen is up for `LOCK_TIMEOUT_MS`, so the header is painted once
  and the progress bar remains the only thing repainted per frame.

  The copyright gets its own row rather than sharing the wordmark's, because
  the battery badge is variable width -- 22px at 72%, 36px at 100% on the
  charger -- and on the 240px portrait panel the three do not fit across one
  line at the badge's widest. That collision would have appeared only on a
  charging device at full battery, which is exactly the kind that ships.

- **`SECURITY.md`**, with a private reporting route and, more usefully, an
  explicit out-of-scope list. The admin PIN is a parental control and not a
  security boundary; saying so in the policy is kinder than saying it in a
  reply to somebody's first report.
- **`CODE_OF_CONDUCT.md`** (Contributor Covenant 2.1), with one addition
  particular to this project: screenshots and issues here carry children's
  profile names, so redaction is treated as an ordinary part of review.
- **Issue and pull request templates.** The bug report asks which board and
  which build, because those are the two things a firmware report is useless
  without. The board-port template asks for a pin map and its *source*, since a
  confident wrong pin map costs more time than a partial one.
- **The open list moved to the issue tracker.**
  `code_review_remaining_things_to_fix.md` is gone, and its contents were
  re-checked against the tree and filed as #44-#51. Three of its twelve items
  were already tracked (#7, #8, #30), one had quietly become true and was
  dropped, and three others had partly landed and were narrowed to what is
  actually still open. A file whose name read like leftover scaffolding was
  holding the most contributor-facing content in the repository.

### Changed

- **The copyright holder is now stated as an individual**, in `AppVersion.h`,
  on the device, and in every document. It named the brand before, which cannot
  be right: a trading name that is not an incorporated entity cannot hold a
  copyright, and a GPL notice naming a holder that does not legally exist is
  precisely the notice a downstream user cannot rely on. `NOTICE.md` was
  rewritten to keep copyright and trademark apart, and to state the one thing a
  fork is asked to do before shipping -- rename, which is an edit to a single
  header.
- **CI builds `batdiag`.** All four PlatformIO environments now build in both
  workflows. `batdiag` was built by neither for its whole life: `check_docs.py`
  only validates environments the installer offers, and a bench tool is
  deliberately not offered, so it fell through every net. It builds today; that
  was luck, not a check.

### Fixed

- **Network names no longer reach the serial console.** The saved SSID was
  printed at boot, on save, on connect, on the Wi-Fi join screen and on its
  readback line, and the scan picker logged the *neighbours'* network names as
  well. A serial log is the one artifact of this device that routinely leaves
  the house -- pasted into a bug report, or read by whoever plugs in a cable --
  and the network's name belongs to the household, not the firmware.

  Every diagnostic those lines carried is preserved without the name: whether
  credentials exist, the byte counts NVS actually wrote, and a round-trip
  verdict on the readback that says strictly more than printing the name did.
  The name is still on the device, in System Info and the network activity
  list, where the person reading it is the person holding it. `env:wifidiag` is
  the one deliberate exception and now says so in a comment: a bench radio
  scanner that redacted its own results would answer nothing.


## 5.0.0 — 2026-08-26

An admin profile behind a four-digit PIN, so device settings and one profile
are out of a player's reach -- and Elements, the periodic table, as game 31.

The major bump is deliberate: this is the first release where an existing
device changes behaviour under its owner rather than only gaining features.
A console that upgrades to this gains an Admin profile it did not have, stops
booting into whatever profile was last used if that profile is the admin, and
starts refusing settings changes to everyone else in the house.

Flash 2,353,001 / 3,145,728 (74.8%), RAM 72,572 / 327,680 (22.1%).

### Added

- **Every build says which build it is.** `BRAINO_VERSION` is 5.0.0 across every
  flash of this release, so it cannot answer the question anyone actually asks
  of a board on a desk: *is this the release, or the branch I was part-way
  through an hour ago?* A pre-build script now stamps the branch and the
  abbreviated commit into the firmware, and the compiler supplies the build
  time. Three places read it and none of them restates it: About's new last
  page, **This build**; System Info's Device tab, as `Branch`, `Commit` and
  `Built` rows; and a `[boot] build=` line in the serial log, so a log pasted
  into an issue identifies its own firmware. A build from a source tarball with
  no git reads `unknown` rather than inventing a plausible commit.

  The build time is deliberately **not** a compiler flag like the other two.
  PlatformIO folds build flags into its build signature, so a flag that changes
  on every invocation would invalidate every object in the tree and make
  `pio run` a permanent full rebuild -- about 100 seconds instead of 25, for
  everyone. It comes from `__DATE__`/`__TIME__` in one small translation unit
  that the script deletes before each build, so exactly one file recompiles.
- **A Lock button.** A padlock beside Home on the top bar, and on the
  launcher's own header in both layouts, blanks the panel immediately; the next
  touch lands on the existing Hold to unlock screen and a completed hold
  resumes exactly what was open. It reuses the wake lock rather than adding a
  second lock state, and an explicit press produces the lock screen even for an
  owner who has switched Hold to unlock off -- that press is the request. The
  tap is consumed by the shell above the active screen, so nothing under the
  padlock is pressed as well. Making room for it narrowed the top bar's Home
  slot from 42px to 32px and moved the title's start from 48 to 62.

### Fixed

- **The lock screen's footer is no longer cut mid-word on the device.** It went
  through `Ui::fitted()`, which truncates and ends in a `.`; past that, a line
  wider than the panel loses its tail silently, because TFT_eSPI drops
  characters once x reaches the viewport's right edge. The footer now takes the
  longest wording that measures whole against the live panel, wraps at a word
  break if none fits, and clamps both lines inside the screen; `renderLock()`
  also resets the viewport, since the lock screen owns the panel and should not
  inherit another screen's clip. The mock-ups could not have caught this -- PIL
  has its own metrics and clips nothing -- so the generator now draws the
  portrait lock screen too, where the sentence is tightest, and the firmware
  logs the panel size and the measured footer width once per lock paint.
- **Profiles are players throughout the firmware, UI and docs.** The profile
  APIs are now `playerCount()`, `addPlayer()` and `removePlayer()`, the runtime
  singleton is `BrainoApp`, and on-device text talks about players. The
  persisted NVS namespace and profile-count key keep their old names so
  existing devices do not orphan profiles or scores.
- **Flag capital bonus answers no longer stay on the same button.** Flags and
  State Flags now reshuffle the fixed option array when entering the capital
  bonus phase, move `correctBtn_` with the answer, and force the answer off the
  just-pressed slot.
- **The battery badge has real clearance and a bordered level strip.** The
  shell is taller so the percentage digits no longer touch the outline, and the
  internal level strip is drawn inside its own border without changing the
  width contract used by the launcher header.
- **The hold-to-unlock footer now fits in both orientations.** Lock-screen
  strings are measured and fitted against the live panel width, and the footer
  is anchored to the bottom of the panel instead of inheriting all the stacked
  spacing above it.
- **Automatic NTP resync is now configurable.** Settings → Device shows a
  global `Sync: Nh` control, admin-writable from 1 to 24 hours with a 6-hour
  default. The value is mirrored in RAM for `tickTimeSync()`, while boot sync,
  manual **Sync now** and failure retries keep their existing paths.
- **`verify` was red on `dev` for every pull request**, whatever the request
  contained. `tools/check_frame_rules.py` is a ratchet over the memory and
  frame-budget rules, and its baseline had fallen behind four files, so the
  job failed before it could say anything about the change under review. A
  check that is always red teaches people to ignore it, so the counts are
  brought back under the baseline rather than the baseline raised to meet
  them:
  - `SettingsGame.cpp` was never in debt at all -- the `new` arm of the heap
    rule was matching *inside a string literal*, so `renderPinPad(host, "Enter
    new PIN")` read as two raw allocations. The checker now blanks string
    literals before matching, which also stops a `"http://..."` URL being
    mistaken for a trailing `//` comment. `GreWordTable.cpp` was a false
    positive of the same kind.
  - `Board::addPlayer()` grew a `const char*` form that builds the stored name in
    a stack buffer, so boot's default Admin profile allocates nothing and the
    empty-name fallback no longer builds `String("Player ") + n` only to throw
    it away. The `String` overload stays for `ProfileGame`, whose draft name
    genuinely is one.
  - `NearbyGame`'s `scoreText()` returned a `String` per row -- four heap
    blocks per rebuild with two peers on screen, churning while the scanner
    keeps the list moving. It writes into a caller-owned buffer now, which is
    what `RowList` itself was rewritten to do.
  - `SystemInfoGame`'s loop-load, worst-work, worst-frame, loops, boot-count,
    fragmentation, RSSI, channel, brightness, saver and stall rows build with
    `snprintf` into a reused stack buffer. These are the rows somebody watches
    *while* chasing a slow frame, so they were the worst possible place to be
    churning the heap.

  The ratchet has been lowered to the new counts, so none of it can climb
  back. Flash is 884 bytes smaller as a side effect.

### Added

- **Board profiles — a board is now two files, and nothing else.** The
  firmware was tied to the E32R28T-1 by construction: `include/BoardConfig.h`
  held one board's pins as global constants, `SCREEN_WIDTH`/`SCREEN_HEIGHT`
  were literal 320 and 240, and a second board meant copying thirty `-D` flags
  into a second `platformio.ini` section. Now `include/boards/<id>.h` holds the
  whole board — pins, rotations, the battery divider, which peripherals exist
  at all — and a `[board_<id>]` section carries only the macros TFT_eSPI
  insists on being told at compile time. **No file under `src/` names a GPIO
  number any more.** The landscape canvas is derived from the panel and its
  rotation rather than stated, so a differently sized board gets the right
  constants without an edit. A peripheral a board does not wire is `PIN_NONE`
  and the firmware degrades quietly, so a board with no SD slot, no LED or no
  battery needs no code change. `docs/PORTING.md` is the checklist.
- **A statement of which boards *cannot* be supported.** Optional hardware and
  missing hardware are different answers and used to be blurred together. Four
  things are now hard requirements — a panel of at least 320×240 in landscape,
  a touch controller, the backlight on a GPIO, and 4 MB of flash for the 3 MB
  app — and a board missing one fails to compile with a message naming it,
  rather than flashing into a screen nobody can read or press. The SD slot, the
  RGB LED, the speaker and battery sensing stay genuinely optional.
- **The web installer now derives its board list from `platformio.ini`.** A
  board that can be supported has to be flashable by the person who owns it,
  not only by someone with a toolchain, so "supported" and "offered on the
  page" are now the same set by construction. `gen_site.py` reads the
  `[board_*]` sections and refuses to generate until a new board has a label, a
  firmware entry and a CI build behind it; `check_boards.py` reports the same
  gaps without needing to build.
- **`tools/check_boards.py`, and a compile-time cross-check.** The panel is
  necessarily described twice — once to TFT_eSPI, once to us — so
  `BoardConfig.h` static_asserts `TFT_WIDTH`, `TFT_HEIGHT` and `TFT_BL` against
  the profile. The backlight one matters most: with the wrong `TFT_BL` the
  panel is completely dark while Wi-Fi, BLE, NVS, the LED and the screen saver
  all run perfectly, so it reads as a dead screen rather than a wrong pin. The
  script catches what the compiler cannot — a profile field a board never
  filled in, a header no section points at, a board-specific flag that has
  drifted into `[common]` where it would silently apply to every board.
- **Hold to unlock, coming out of the screen saver or panel sleep.** A single
  stray press used to dismiss the saver and land straight on whatever screen
  was underneath -- mid-game, or on Settings -- which in a bag or a coat
  pocket meant phantom taps on live UI. `swallowTouch_` suppressed the *next*
  press, which never helped: the press that dismissed the saver was itself the
  unintended one. A touch now lights the screen and nothing else, onto a lock
  screen whose unlock button has to be held for about a second, with a
  progress bar so it is obvious what the device is waiting for. It covers both
  the saver and panel sleep, works in both orientations, and returns you to
  exactly the screen, profile and orientation you left.

  It is **not** a PIN and is disjoint from the admin PIN: unlocking neither
  grants nor revokes admin. The hold forgives up to 150ms of lost contact,
  because a held press on a resistive panel drops out intermittently as a
  matter of course -- a detector that reset on the first gap would feel broken
  while looking correct in code. Nobody unlocking within 12 seconds sends the
  device back to sleep rather than leaving it lit. On by default; switch it off
  in **Settings -> Power -> Hold to unlock**, which like every device setting
  is global and admin-writable only.
- **`cases/` — printable enclosures, one folder per board.** The first is
  `cases/E32R28T-1/`: a two-part screwed shell for the 2.8-inch board, as STL
  and as a Bambu Studio project with the print profile intact, plus the notes a
  slicer cannot infer. A case is explicitly **not** required for a board to be
  supported; the folder exists because someone who has never opened a CAD tool
  should not be handed a bare board and told to improvise. Ports are encouraged
  to add their own folder.
- **A licence.** The repo had none, which meant default copyright -- all
  rights reserved -- so nobody could legally contribute to a project that was
  asking for contributions. It is now **GPL-3.0-or-later**, so ports and
  modified builds stay available to the people holding the hardware. The
  bundled libraries keep their own permissive, GPL-compatible licences.
- **Help wanted sections** in `README.md`, `CONTRIBUTING.md` and on the Pages
  site, asking for the two things the project actually needs: board ports, and
  people willing to say what is wrong with the code.
- **Elements, the periodic table (game 31).** Three tabs, and the order is the
  argument: *Explore* first, because a quiz about something you have never seen
  is a test rather than a lesson. Explore draws the real 118-cell wall chart --
  nine rows, including the lanthanide and actinide strips lifted out from under
  periods 6 and 7, coloured by family. Tapping a square selects it; tapping the
  strip above opens a card with the name, the atomic number, what kind of
  element it is, whether it is solid, liquid or gas, and one plain line about
  where the player has already met it: *Helium -- fills balloons and makes them
  float*. From the card, **Quiz me** starts a round about that element.
- **Six question shapes, all derived from the table.** Symbol to name, name to
  symbol, find it on the chart, how many protons, which element does this, and
  solid/liquid/gas. Over 118 elements that is upwards of seven hundred distinct
  questions, and -- the point -- a bank that cannot drift from the data behind
  it, the same reason About derives its game list instead of restating it.
  Wrong answers hold the right one on screen for 1.2s rather than just scoring
  the player. Selection is weighted by `Progress`, so a missed element comes
  back soon and a known one fades out.
- **A Level tab, because auto alone was not enough.** Auto follows measured
  mastery: the famous 26 until 80% of them are known, then the everyday 63,
  then all 118. *Easy / Common / All* pin it instead, persisted per profile, so
  a parent can set an older sibling to the whole table. The level only ever
  narrows the **quiz** -- Explore always shows and opens all 118, with
  out-of-level squares dimmed rather than hidden. A player who wants to go and
  read about Oganesson is doing exactly the thing the screen is for.
- **`tools/gen_elements.py`** generates `src/games/ElementDataTable.cpp`, and
  is where the checking lives: unique chart cells, every element covered once,
  facts inside the 46 characters the card can render, tier lists naming only
  real symbols. A hand-edited 118-row C++ table hides all four until it is on
  the device.
- **An admin profile, guarded by a four-digit PIN.** One profile is the admin
  (`Board::adminProfileIndex()`, with the PIN beside it in NVS). A fresh device
  gets the profile **Admin** and the PIN **0000** created on first boot — there
  is no setup wizard to sit through, and the PIN is changeable afterwards. The
  admin row carries a padlock in the profile picker, so the lock is visible
  before the tap rather than after it.
- **The PIN is asked for on the two routes into the admin profile**: switching
  to it, and opening its Edit menu (which reaches rename and that profile's
  per-player game list). It is asked **every time**, including straight after a
  correct entry — "already admin" says nothing about who is holding the device
  now, which is exactly the case this is for.
- **Per-player game visibility is the admin's to set, and now actually is.**
  The list at *Profiles → Edit → Games* is readable by anyone and changeable
  only by the admin. Previously it enforced nothing: a player opened their own
  row and switched back on every game that had been hidden from them, which
  made the whole per-player feature decorative.
- **Removing a player is admin-only.** It destroys that player's scores and
  mastery data permanently, and any player could previously delete a sibling in
  two taps with no confirmation. Renaming stays open to everyone: it is
  harmless and reversible.
- **Settings is readable by everyone, writable only by the admin.** No PIN to
  open it: there is nothing secret on those pages, and a player who cannot see
  why the screen dims is worse off than one who can read it. Every control
  greys out for a non-admin and, more to the point, is refused.
- **Settings → Admin → Change admin PIN.** The new PIN is entered twice and is
  written only if both entries match. A mistyped PIN stored anyway locks the
  owner out of their own device, and there is no recovery short of a factory
  reset. Settings grew a third tab for it, and its tab strip now divides the
  live width instead of assuming 160px halves.

### Changed

- **The battery icon shows the percentage.** It carries the number as numerals
  inside the shell, the way iOS and Android status bars do, over a two-pixel
  level gauge that keeps the analogue cue the fill used to give. Eleven pixels
  of fill is not a figure anybody can read, and "how long have I got?" is a
  number question. The bolt moved inside the shell while charging, so plugged-in
  and how-full are one glance rather than two. Red at or below 15% now colours
  the digits as well as the shell, and still nothing else colours the shell --
  that signal only works while it is rare.
- **The status rows are laid out from measured widths, not constants.** The
  badge is variable width now (22px at `72`, 36px at `100` on the charger), so
  the top bar and both launcher headers place the clock, sync, Wi-Fi, battery
  and beacon badges right-to-left off `Ui::batteryBadgeWidth()` and the measured
  clock string. The landscape launcher's status hairline moved from `lW-110` to
  `lW-116` to buy the pixels, and `LauncherLayout::profileRect()` gave them up;
  in the widest state that row has about 4px of slack left. The portrait top bar
  is measured now too, which happens to *reduce* an existing title/clock overlap
  at 240px rather than making it worse.

### Notes

- Neither Elements tab commits on a single tap. A chart cell is 16x14 px, well
  under the 30 px touch minimum, so the first tap aims and the second confirms
  -- in the quiz too, where the aimed square is ringed in amber and the strip
  says so. Moving the selection repaints two cells, not 118: a full chart is
  ~20 ms of SPI, which is a whole frame budget for a one-square move.

### Fixed

- **The Wi-Fi icon was drawn as mobile-signal bars.** Four ascending bars is
  the cellular convention, on a device with no cellular radio -- it read as
  data signal rather than Wi-Fi. It is now the usual fan: a dot with three
  arcs radiating from it, still driven by the real RSSI (the dot alone is the
  weakest level, each further arc one step up), still greyed and struck
  through when the radio is down. The arcs are a precomputed table of pixel
  offsets rather than trig in the render path, since the top bar is on every
  screen and `Ui::Renderer` has no arc primitive. `tools/gen_screens.py`
  carries the same table, so all 50 mock-ups with a top bar were regenerated.
- **The battery gauge claimed it could tell whether a pack was fitted. It
  cannot.** Measured on hardware with the `batdiag` wizard, 8s averaged: pack
  plus USB 4.224 V, USB with no pack 4.159 V, pack alone 4.066 V. The no-pack
  case sits *between* the other two, because the TP4054 holds its BAT output at
  float voltage whether or not a cell is attached, so no threshold separates
  them in either direction. `V_NO_BATTERY = 4.35 V` was above anything this
  board produces and never fired: `isBatteryPresent()` was a constant `true`,
  `getPowerSource()`'s no-pack branch was dead, `getBatteryPercent()` could not
  return -1, and the blank-digit rendering in the badge was unreachable.
  `isBatteryPresent()` has been removed rather than left as a function that
  structurally cannot be right; the ceiling is now `V_SENSOR_MAX = 4.50 V` and
  means *the ADC is faulty*, not *no pack*. With no pack fitted the gauge reads
  high, near full -- the README and System Info now say so instead of promising
  a state the hardware cannot reach.
- **A console booted on USB reported "on battery" after 45 seconds.** Three
  charge-inference faults, all proven on hardware and now shared by
  `BoardPower.cpp` and `battery_diag.cpp`: `V_CHARGER_HELD` was 4.24 V, above
  the 4.238 V maximum the board actually produces, so the held-high signal
  never fired; the flat-window fallback described itself as "flat and low" but
  never tested for low, so a full pack sitting at float voltage fell into it;
  and the held-high level was tested before the negative step, so a falling
  cell that was still above the trip could be called charging. The step test
  now runs first, the level is 4.21 V, and the fallback is guarded by
  `chargeSmoothV_ < CHARGE_FULL_V`. Plug/unplug detection was never at fault
  and is unchanged.
- **Every greyed-out Settings control was still live.** Both tabs have drawn
  their controls greyed for a non-admin for some time, but nothing checked who
  was tapping: a player could change the theme, the layout, the brightness, the
  radios and the idle policy, and could trigger a factory reset. Settings now
  refuses the change as well as drawing it grey.
- **The device no longer boots into the admin profile.** First boot used to
  leave admin selected, and the picker's *Done* button goes home with whatever
  profile is already active — so the PIN could be skipped entirely by pressing
  Done. Boot now drops to Guest if it finds admin active.
- **Both PIN pads were drawn off the bottom of the screen.** Rows were
  hard-coded at y=220 and the DEL/OK buttons at y=270 on a 240px-tall panel:
  the bottom row and both actions were outside the display, so there was
  nothing to press and no way to submit a PIN. Both pads are now laid out
  against the live panel size, as a standard 3x4 pad with DEL / 0 / OK on the
  last row.
- **A PIN of `0000` could not be entered.** Digit count was inferred from the
  numeric value, and `0000` is zero — indistinguishable from an empty field. It
  is tracked separately now, and an entry is only judged once it is exactly
  four digits, so a short prefix cannot be tested against the stored PIN.
- **The Settings PIN pad printed the PIN in plain text** in a box above the
  keys, and labelled its keys `0`–`11`. It shows masked dots and the digits
  `0`–`9`.
- **The PIN screen had no way out.** Tapping the admin row left a player stuck
  on it; there is a Back button now.

## 4.2.0 — 2026-08-17

Battery status the user can act on: the console now works out whether it is
charging, and asks to be plugged in before it dies.

Flash 2,325,045 / 3,145,728 (73.9%), RAM 72,020 / 327,680 (22.0%).

### Added

- **Charging is detected, where it used to be reported as unknown.** This board
  brings no charge-status line out to a GPIO, so `ChargingState` had exactly one
  value and the System Info row said "Unknown" forever. It is now inferred from
  the cell voltage on GPIO34, which is the only thing the firmware can see:
  a step between consecutive samples catches the cable going in or out within
  about two seconds, a voltage held above 4.24V is a charger holding it there
  because no resting cell reaches that, and a 45-second trend covers everything
  in between. A window that comes out flat keeps the previous verdict rather
  than flapping — mid-discharge a LiPo plateau spans about 20mV across 40% of
  the capacity, so "no movement" is not evidence of anything. `FULL` is only
  ever reached from `CHARGING`, because a rested full pack and a finished charge
  look identical from one sample.
- **A charge-me warning at 15%.** `Board::isBatteryLow()` and
  `isBatteryCritical()` (15% and 5%) drive a strip across the top of whatever
  screen is open: *Battery low — time to charge*, escalating to *Battery empty —
  plug in the charger*. It shows for six seconds and repeats every two minutes,
  which is often enough to be a warning and rare enough not to become furniture
  a player learns to ignore. Crossing either threshold restarts the cycle, so low
  becoming critical says so at once. Both predicates are false while charging,
  so plugging in clears the warning immediately rather than waiting for the
  percentage to climb back over the line.
- System Info's Power section now reports the charge verdict and a plain-English
  charge level beside the voltage.

### Changed

- **The battery icon says something at a glance.** The lowest colour band began
  at 30%, so it said nothing new at the level the user is actually asked to act
  on; the bands are now red at or below 15%, amber to 40%, green above. At or
  below 15% the *outline* goes red as well as the fill, because a nearly empty
  battery and a nearly full one differ by 11 pixels of fill and that is not
  something anyone reads across a room. The emphasis drops the moment the
  charger is attached — by then the user has done the thing it was asking for.
- The bolt now means "the charger is attached", derived from the charge verdict
  rather than from a voltage above 4.35V — with a pack fitted that threshold was
  never crossed, so the bolt could not appear while charging a real battery. An
  empty bolt alone still means USB with no pack.
- `Ui::drawBatteryBadge()` takes a `Ui::PowerHint` rather than an
  `isExternalPower` bool. `Ui::powerHint(board)` is the single place the drawing
  layer and the HAL enum meet, which keeps `Ui.h` free of the HAL.
- The runtime's notification strip is now `drawHeaderBanner()`, and the battery
  warning outranks a Nearby event on it: one of them is a nicety, the other is
  the reason the console is about to switch off.

## 4.1.0 — 2026-08-15

A major console refresh with a new product name, redesigned launcher icons,
two new games (Dice, Coin Flip), the BLE beacon renamed to **Braino**, and an
opt-in anonymous score exchange with other consoles in the room.

Flash 2,323,937 / 3,145,728 (73.9%), RAM 71,980 / 327,680 (22.0%).

### Changed

- **The console is now Braino!** The product was renamed; it is Braino!
  from this release. The owner did not change: every copyright line still reads
  GoodTime Micro Company, and the trademark notice states that continuity
  directly. The name and both copyright forms now live only in
  `include/AppVersion.h` -- they had been typed out in five places,
  which is the same shape of drift that once left About six games behind.
  The version macro is now `BRAINO_VERSION`.
- **The launcher icons are drawn to a design system now.** What made the old
  set look homemade was not any single icon but that they disagreed: some were
  outlines and some solids, strokes ran from one to three pixels, several sat
  timidly in the middle of a tile while others filled it, and a few spent four
  colours where one would do. Every icon is now a solid light silhouette with
  detail in one near-black and at most one accent hue, filling a 36x36 box, with
  a two-pixel minimum stroke and a single corner radius. Five games are exempt
  from the one-accent rule because colour is their subject.
- **Every launcher icon was reworked.** Three pairs of tiles were effectively
  the same picture -- Fractions and Percent were both a white circle with a
  yellow wedge, Flags and State Flags the same flag in two colours, Tic-Tac-Toe
  and Whack A Mole both a white grid -- and six more drew font 1 text that is a
  smudge at tile size. Icons now have a documented 36x36 budget, since the
  landscape tile is only 46px tall.
- **The beacon now advertises as `Braino-<id>`.** The family id is `Braino` and
  the two-letter manufacturer tag is `BR`; the device id is unchanged — the last
  two bytes of the factory Bluetooth MAC, rendered as four hex digits. The
  manufacturer-data layout version goes to `2`, so a device running the previous
  firmware is ignored rather than mis-decoded.
- The web installer offers only the console firmware. The bring-up and Wi-Fi
  diagnostics are still built by CI and still documented in the README, but they
  are no longer in the browser flasher's picker, where choosing one replaces a
  working console with a serial test.

### Added

- **Dice.** Pick one, two or three dice and throw them; the faces tumble for a
  second before they settle. No score is kept -- a best total here would be
  luck, reachable in a few taps and then frozen at 18 forever, so it would only
  add a dead row to the Scores app.
- **Coin Flip.** Best of one, three or five spins. Each coin squashes edge-on
  and back as it spins, then lands, and a pip row fills in as the round goes on.
  No score, for the same reason as Dice.
- **Nearby — an anonymous score exchange.** A new system app, **off by
  default**, that listens for other Braino devices and shows who is in range,
  which game they have open and their best score for it. When a peer's score
  beats the record on this device, the header says so.

  - Sharing is opt-in twice over: it does nothing unless the BLE beacon is on,
    and then only once *Settings → Nearby* (or the switch on the Nearby screen
    itself) is turned on. Turning the beacon off stands it down.
  - What travels is a game index and a best score, added to the existing
    beacon payload. **No player name, no profile name, no progress, nothing
    profile-scoped.** A peer is four hex digits of its own MAC and nothing
    else, which is the whole design: a leaderboard with no way to find out who
    is on it.
  - The scan is **passive** — it never transmits a scan request, so listening
    adds nothing to what the device puts on air.
  - Peers are decoded with the exact inverse of the function that builds our
    own payload, so there is one description of the wire format rather than a
    transmit copy and a receive copy that can drift.

- **Header notifications.** Nearby events (a console arriving, changing game,
  or beating a record) paint a strip over the header for five seconds and are
  then removed — the screen underneath repaints itself. They are not collected
  into a list, because a notification nobody clears is furniture.

- **`Settings → Device` gains a Nearby switch.** Network moves to a full-width
  row to make space; the switch greys out and says *needs Beacon* while the
  radio is off, rather than flipping and doing nothing.

- The GitHub Pages installer now showcases every generated still for the games,
  launcher, settings, profiles, Wi-Fi/time, scores, About, System Info and the
  screen saver instead of showing only the launcher mock-ups.

### Fixed

- **Every game was drawing from a software PRNG, not the hardware RNG.**
  `AppRuntime::begin()` called `randomSeed(esp_random())`, which reads like an
  improvement and is the opposite on this core: arduino-esp32 defaults
  `random()` to `esp_random()`, and `randomSeed()` switches it to newlib `rand()`
  for the rest of the boot. One good number bought, then 123 draws across 25
  games spent on a once-seeded LCG. The call is gone. Dice and Coin Flip
  additionally draw through `engine/Entropy.h`, straight from `esp_random()`
  with the modulo bias rejected, because for those two the draw is the product.
- **The device could stop waking from a long sleep.** `applyTimeConfig()` handed
  lwIP's SNTP client `ntpServer().c_str()` -- the buffer of a `String` temporary
  destroyed at the end of that statement. `sntp_setservername()` keeps the
  pointer rather than copying the string, so the daemon was left holding freed
  heap and dereferenced it on its own poll cadence. That block survives while
  nothing reuses it, which is why the fault needed hours of idling to appear.
  The name now lives in a `Board` member. The same call was also being re-issued
  every five minutes forever, stopping and restarting the daemon each time for
  no benefit; it is idempotent now.
- The ILI9341 ignores Sleep Out within 120ms of a Sleep In. `displayWake()` now
  waits out the remainder, which a tap arriving just after the saver handed over
  to sleep could otherwise land inside.
- The Wi-Fi tile drew outside its own tile. It painted the bottom halves of two
  full circles out with a rectangle in the tile colour, and the mask was three
  pixels short, so the base of the outer ring survived at `cy + 24` -- past the
  bottom edge of a 46px landscape tile. It is drawn as real arcs now.
- The Coin Flip tile had the same overflow, from its edge-on sliver.
- Several icons set the text datum to `MC_DATUM` and returned without restoring
  it, re-aligning whatever the launcher drew next.
- **Profiles and System Info came back from the screen saver in the wrong
  orientation.** Both opt into `followsLayout`, and both were laid out correctly
  when opened -- but `wakeFromSleep()` and `exitScreenSaver()` tested
  `activeApp_ != nullptr`, which is true for *every* launched app, and forced
  landscape. Boot into Profiles in Vertical layout looked right; idle until the
  saver came on, touch to dismiss, and it came back landscape and stayed there.
  All three call sites now share one `rotationForActiveScreen()`; the rule was
  written out three times and one copy was wrong.
- **The launcher never drew the copyright in Vertical layout.** The landscape
  branch drew it and the portrait branch did not, so owners running the device
  in portrait never saw it on the home screen. It now shares the top row with
  the title, right-aligned, which costs portrait its centred title and makes the
  launcher and Profiles headers agree.
- **Profiles.** "Who is playing?" and the guest hint overlapped each other in
  both orientations, and in portrait the hint ran straight through the first
  profile row. In portrait the copyright line also collided with the title,
  which is too wide at 240px to share that line. "Braino!" is half the width, so
  both orientations went back to sharing one 30px header bar.

### Privacy surfaces updated in the same change

- *System Info → BLE* decodes the two new Nearby fields and reports **Open game** and
  **Best score** as `Broadcast` / `Not Broadcast` from `sharesActivity` — read
  off the same struct the controller was handed, not from UI copy.
- The **About** radio page and the README/installer privacy sections say what
  Nearby adds, and say it only while it is actually on.
- `docs/BLE_BEACON_SPEC.md` records the v2 payload and the anonymity argument.

### Other updates

- The installer page and README now call out the exact E32R28T-1 / ESP32-32E
  Amazon board needed for the ready-made flash image.

## 4.0.0 — 2026-08-15

Browser flashing, screen sleep, GRE Words, Percent Circle, Scores device bests,
app/runtime refactors, storage telemetry, safer profile deletion, and a pass
over touch/redraw bugs found on the physical board.

Flash 2,307,681 / 3,145,728 (73.4%), RAM 68,036 / 327,680 (20.8%).

### Added

- **Device-wide best scores on the Scores screen.** A new **Device** tab shows
  the best score across every player on the device for each game, and who holds
  it. The holder's name appears in gold when it is the current player. Scores
  are computed once at screen entry and cached, so scrolling and paging add no
  NVS reads. Ties go to the lowest profile index.

- **A GitHub Pages site with a browser-based flasher.**
  <https://iamankushpandit.github.io/Gume/> describes the firmware and installs
  it over Web Serial: pick the board and the firmware, plug in the USB cable,
  press the button. No PlatformIO, no toolchain, nothing to download by hand.
  Desktop Chrome, Edge and Opera only -- Firefox and Safari do not implement
  Web Serial, so the page falls back to `esptool` instructions and direct
  `.bin` links.

  The firmware picker offers all three environments: the console itself, the
  bring-up diagnostic for a board that shows nothing, and the isolated Wi-Fi
  radio test. Each one gets its own esp-web-tools manifest.

- **`tools/gen_site.py`.** The page is generated, not written: version from
  `AppVersion.h`, the game list and blurbs from `AppRegistry`, the build
  figures from `README.md`, the board name from `platformio.ini`. Same reason
  About derives its list -- a hand-written copy of the game list has fallen six
  games behind before. `site/index.template.html` holds wording and layout
  only.

- **Percent Circle.** Three modes: read the circle, make the circle, percent of
  a number. Teaches percentages as a portion of a whole, with circle-based
  visualization and adaptive difficulty.

- **GRE Words.** A vocabulary trainer with 250 GRE-level words in two modes:
  **Study** flips a card from the word to its meaning and an example sentence,
  and **Quiz** asks for the right gloss out of four. Both feed the same
  spaced-repetition data (`engine/Progress`), so a missed word comes back soon
  and a known one fades out. The odd screen out in this catalog -- it is aimed
  at whoever is sitting the test, not at the youngest audience.

- **Screen sleep.** The device now blanks the panel when it has been left
  alone, which is what actually conserves the battery: the screen saver was
  keeping the backlight lit indefinitely. **Settings -> Power** carries three
  policies -- saver then sleep, sleep only, or saver only -- with both the idle
  delay and the sleep delay configurable. The main loop drops from 50Hz to 10Hz
  while asleep and any touch wakes it, returning to whatever screen was open.

  This is panel sleep, not `esp_deep_sleep`: the CPU stays up to poll the touch
  controller, because no wake source is wired for a true deep sleep on this
  board.

- **Settings has tabs.** Device and Power. The grid was already seven buttons
  and a slider, with nowhere to put the three idle controls.

- **`.github/workflows/pages.yml`.** Builds `app`, `bringup` and `wifidiag` on
  every push to `main`, generates the site around them, and copies the four
  flash images beside each manifest. It rewrites the machine-local `map-n-flag`
  path in its own working copy, because CI is just another checkout. A final
  step refuses to publish a manifest whose binaries are missing -- that failure
  would otherwise land in a user's browser, halfway through writing their
  board.

- **`check_docs.py` now checks the site.** It fails if a version number is
  typed into the template, if the template loses a placeholder the generator
  fills, or if the page offers a firmware that platformio.ini does not define
  or the workflow does not build.

- **NVS storage telemetry.** System Info now reports NVS entry use, free
  entries and namespace counts, with soft warning/critical thresholds so future
  apps cannot quietly exhaust the shared partition.

- **`CONTRIBUTING.md`.** Contributor workflow is now written down for humans
  and agents: branch/worktree use, board locking, adding games/apps, fixing
  bugs, checks, docs and flashing.

### Changed

- **Trace game completely redesigned.** The old version let a single tap
  anywhere complete an entire stroke; tap a few times and the letter was done,
  with no requirement to follow a path. The new version enforces ordered
  dot-to-dot tracing: strokes must be followed in order from a numbered start
  dot, with each dot claimed only by dragging through them in sequence. Added
  26 lowercase letters (a–z) alongside the existing uppercase and digits,
  with a three-way mode selector (ABC / abc / 123) at the top. Waypoints are
  now calculated at glyph load and displayed as pulsing dots that guide the
  player through the correct path, teaching both letter formation and stroke
  order before a pencil is involved. The final device-tested pass improved
  lowercase `f` and `g`, added Again/Next controls, and moved completion
  feedback out of the drawing field so the player can see the finished
  character.

- **Apps and games now launch through one registry.** Playable games declare
  their id, title, subtitle, blurb, score info, icon, launcher index and
  default visibility in one metadata block, while `AppRegistry` binds that
  metadata to the concrete instance.

- **Rendering now goes through `Ui::Renderer`.** Normal games draw against a
  driver-free RGB565 interface instead of directly taking `TFT_eSPI`, while the
  firmware runtime adapts it back to the panel.

- **`main.cpp`, runtime and `Board` were split by concern.** The app runtime,
  launcher, screen saver/sleep path, HAL access facades, display, touch,
  storage, power, network and feedback implementations now live in smaller
  units.

- **Shape Arith subtraction is clearer.** Subtraction now shows a `left` box and
  a `take away` box, keeps the removed group visible, and asks the player to
  count what remains instead of showing an unused mystery box.

### Fixed

- **Profile deletion now moves persisted data, not only names.** Removing a
  player clears that slot's `pN_` keys, shifts later profile data down with the
  player, clears the old last slot and keeps the active player pointing at the
  same real profile where possible.

- **Percent, Maze, Trace, Shape & Color, Cinnamon and Profiles redraw bugs.**
  Percent clears the previous question before the next round, Maze now redraws
  only moved cells instead of flashing the whole screen, filled stars match
  their outline geometry, Cinnamon shows feedback for the final correct input,
  and the wide Profile picker no longer squeezes the helper text into the
  player rows.


## 3.0.0 — 2026-08-13

BLE beacon with full on-device transparency, a System Info app, an About
app that reports what the radios are actually doing, and a pass over
everything that was making the UI feel slow.

Flash 2,254,141 / 3,145,728 (71.7%), RAM 64,940 / 327,680 (19.8%).

### Added

- **BLE beacon.** An opt-in, non-connectable presence broadcast, off by
  default and switched from *Settings -> Beacon*. It advertises a device name
  (`LearnKey-<id>`) and a manufacturer-data block holding a family tag, a
  layout version and two bytes of the factory Bluetooth MAC. 27 of the 31
  legal payload bytes. Nothing profile-scoped is reachable from the radio path.

  NimBLE rather than the core's Bluedroid stack: advertise-only needs a
  fraction of the host, and flash is the scarce resource here. Host plus
  controller cost ~192 KB.

- **System Info BLE tab.** Shows whether advertising is currently active, the
  advertised name, every decoded field of the manufacturer data, a privacy
  list of what is *not* broadcast, and under *Show advanced* the interval, TX
  power, advertising type, controller address and a hex dump of the exact
  bytes on air.

  Everything on that screen is read back from the same structure the radio was
  configured from -- `BleBeacon::Advertisement`, compiled into a raw buffer and
  handed to the controller verbatim. There is no second, hand-written UI
  description of the payload, so the display and the radio cannot drift apart.
  Contract written down in `docs/BLE_BEACON_SPEC.md`.

- **Beacon badge in the launcher header.** The Bluetooth rune, drawn only
  while the radio is genuinely advertising -- there is no greyed-out variant,
  because "is it transmitting?" should not be a question of shade. Placed
  per layout mode: landscape puts it on the clock's line, positioned off the
  measured width of the clock string, because the badge row there has about
  8px of slack; portrait simply extends the badge row.

- **System Info screen** (five tabs: board, memory, network, BLE, app state)
  with live telemetry, scrolling rows and a scroll bar.

- **About gained a "What the radios do" page**, and every fact on it is read
  from the running system rather than typed in: Wi-Fi status from
  `Board::hasWifiCredentials()` / `isWifiConnected()`, beacon status and the
  advertised name from `BleBeacon::active()` / `configured()`. A privacy
  claim that has drifted from the hardware is worse than none at all, because
  it is believed -- so About now cannot drift. It also points at
  *System Info > BLE* for the exact bytes on air.

  About is also now orientation-aware, as the system-app rule already
  required: it laid out against `SCREEN_WIDTH` and would have overflowed a
  240px portrait screen.

### Performance

The board felt sluggish, and it was not one thing. Each of these looked
harmless at the call site and each was eating most of a 20ms frame.

- **NVS reads in hot paths.** `Preferences` is flash-backed -- every getter is
  a hash lookup, not a variable read. `screenSaverSeconds()` ran once per loop
  iteration, about 50 times a second. Worse, `gameVisible()` ran up to ~180
  times per launcher repaint and each call was two NVS reads plus three
  `String` temporaries, so a single launcher repaint could mean ~360 flash
  lookups and ~550 allocations.

  Theme, layout, brightness, idle timeout and active profile now have
  write-through RAM mirrors, and game visibility is a 32-bit mask loaded once
  per profile. `gameVisible()` went from two NVS reads and three allocations
  to a shift and a mask.

- **A blocking `delay()` reachable from a render path.** Battery sensing slept
  10ms per call and `Ui::drawTopBar()` calls two battery getters, so every top
  bar cost ~20ms of pure `delay()` before anything was drawn. Now one cached
  sample set with a 2s lifetime and no delays at all.

- **Frame pacing was a fixed nap, not a deadline.** The loop ended in an
  unconditional `delay(18)`, so the frame period was work + 18ms and a heavy
  frame was punished twice. It now sleeps only the remainder of a 20ms budget,
  so touch latency tracks how long the work actually took.

- **Content rebuilt every frame.** System Info reassembled every row on every
  frame while scrolling. Scrolling changes an offset, not content; rebuilds
  are now gated behind a stale flag.

### Fixed

- **"Adding a game" became "Adding a game or an app", and grew from five items
  to fourteen.** The old list was five code edits, and everything that has
  since been shipped broken or stale was outside those five: the README game
  table, the screenshot gallery, `docs/screens/`, the changelog. Four games
  reached a release listed in the catalog and named nowhere in the README.

  The list now covers code, docs, screens and verification, states which
  surfaces derive from `GAME_CATALOG` and must *not* be hand-edited, and adds
  the extra obligations for a system app (both orientations; the About radios
  page if it touches a radio). `check_docs.py` enforces the two items a machine
  can see: every catalogued game is named in the README, and every catalogued
  game has a screenshot.

- **Regenerated the README mock-ups, which had drifted badly.**
  `docs/screens/` still held `countries-outline.png` and
  `countries-continent.png` for a game deleted two releases ago; the launcher
  images showed a profile chip that no longer exists, no battery or beacon
  badge, and a Countries tile; and the Settings picture showed a three-row
  grid that had become four rows with a beacon toggle.

  Added screens for the four games that had none -- US States, State Flags,
  State Maps and Trace -- plus System Info's BLE and memory tabs and the About
  radios page. State flags and outlines render from the real `map-n-flag`
  pixel data, so those are the actual pixels the device draws.

  `check_docs.py` now fails if a generated screen is missing, if a PNG has no
  generator entry (i.e. it outlived its screen), if README references an image
  that does not exist, or if a catalogued game has no screenshot at all.

- **Corrected the artwork licensing, which had been wrong since the Countries
  game was removed.** README carried a warning that the country outlines came
  from `djaiss/mapsicon` -- whose terms are *"don't resell them - I forbid
  it!"* -- and told readers that anyone wanting to monetise the project must
  first swap them for Natural Earth. About credited mapsicon too.

  That artwork went with the Countries game. The binary contains no mapsicon
  data and no reference to it: `mnf_map()` is never called, the library ships
  no country-outline arrays, and `nm` on the firmware finds zero matching
  symbols. The US state outlines that replaced them are Natural Earth, public
  domain. **Every asset now compiled in is MIT or public domain.**

  A credit that outlives its asset is not harmless -- this one misrepresented
  what the owner was allowed to do with their own project. `check_docs.py`
  now fails if mapsicon is credited while nothing calls `mnf_map()`.

- **The boot counter was stuck.** `"boots"` was persisted only inside the
  unclean-reset branch, so a cold boot -- which is every power cycle, since
  RTC memory does not survive one -- read back whatever the last crash had
  left and reported the same number forever. The device sat on "boot #4"
  across many power cycles, quietly undermining the crash report it appears
  next to. Now written on every boot.

- **The System Info row list no longer churns the heap.** It held 48 rows of
  two Arduino `String`s each and rebuilt every one on every frame -- roughly
  96 long-lived allocations freed and re-made at frame rate, interleaved with
  every transient `String` the row builders create. On a heap that cannot be
  compacted that is a fragmentation engine: free heap looks healthy right up
  to the allocation that fails.

  Extracted to `src/ui/RowList` with fixed `char` buffers, so it now allocates
  nothing at all, and rebuilds are gated on a stale flag rather than running
  per frame. Costs 864 bytes of static RAM and *saves* 5.5 KB of flash.

- **Heap accounting per screen.** Every transition compares free heap against
  the value captured before that screen's `begin()` and logs
  `[heap] '<screen>' left N bytes short` when a screen does not hand it back.
  Fragmentation is logged past 60% and shown on the System Info memory tab,
  since free heap alone will not reveal it.

- **Battery sensing made ~20-30ms cheaper per frame, and more honest.**
  `getBatteryPercent()` and `getPowerSource()` each ran their own 10-sample
  ADC conversion with a `delay(1)` between samples, and `Ui::drawTopBar()`
  calls both -- so every top bar cost ~20ms of blocking delay and the System
  Info board tab ~30ms per repaint. That is most of a frame, and it is what
  made scrolling that screen feel sluggish. One cached sample set, 2s
  lifetime, no delays, shared by every accessor.

  Three wrong assumptions went with it: the conversion claimed "3.3V reference
  with 11dB attenuation" (mutually exclusive -- at 11dB the ADC is linear only
  to ~2.45V and its reference varies 1000-1200mV per chip, so it now goes
  through `esp_adc_cal` and the eFuse calibration); percentage was linear from
  3.2-4.2V, which reads ~20 points high through a LiPo's flat middle, and is
  now a piecewise curve; and `isBatteryPresent()` returned false whenever the
  device was on external power, which reads as "no battery fitted".

  The 100k/100k divider ratio remains assumed and still wants a meter.

- **System Info no longer smears text into its tab strip while scrolling.**
  Rows entirely outside the content rect were skipped, but the row straddling
  the top edge was still drawn in full. The row loop now clips to a viewport.

### Changed

- **Profiles screen now carries the product mark.** A branded
  header at the top shows the product name and "(C) GoodTime Micro" in the same
  visual language as the launcher and screen saver. The Pick phase geometry is
  recalculated to accommodate the header without collision.

- **Screens now have an `end()` lifecycle hook**, called on every transition
  through a single `leaveActiveGame()` funnel so no path can skip it. Nothing
  in the firmware runs off a task or timer, so no screen keeps consuming
  cycles after you leave it -- the hook is what keeps that true as screens
  grow. System Info uses it to release its row list.

- **The launcher header shows the profile name as plain text, not a button.**
  The framed chip was what overlapped the clock and status badges on a 240px
  portrait header; the name itself was wanted. In landscape it now sits after
  the byline rather than across it. The rect is both where it draws and the
  touch target, so the two cannot drift apart.

- Settings moved to a four-row grid to fit the beacon toggle, with Reset
  spanning both columns.

- `README.md`, `CLAUDE.md`, `AGENTS.md` and the per-directory `CLAUDE.md`
  files brought back in sync: the game count (23 -> 26), the removed Countries
  game, the added US States / State Flags / State Maps / Trace games,
  profiles, the watchdog, and current flash and RAM figures.

## 2.0.1 — 2026-08-11

Display and theming fixes, all reported from the device.

### Added

- **Screen brightness.** The backlight moves from a plain `digitalWrite` to
  LEDC PWM, with the level persisted and a web-style slider in Settings
  (rounded track, filled portion, round handle).

  The floor is **25%, not 0** — deliberately. At very low duty the panel is
  unreadable, and a player who dragged it to the bottom would have no way to
  *see* the control needed to undo it. The slider's full travel maps to
  25–100, so the unusable range cannot be reached at all.

- **Screen flip.** A Settings toggle turns the display 180° for whichever
  layout is active — landscape 1↔3, portrait 0↔2, i.e. base XOR 2. All five
  rotation call sites now go through a single `effectiveRotation()` helper, so
  they cannot drift apart. Rotation and layout are logged at boot *after*
  `goHome()`, which is what actually applies them.

### Fixed

- **Light theme was partly invisible.** The launcher header is
  `Ui::surface()` — near-white on the light theme — but the gear icon defaulted
  to **white**, so it could not be seen at all. Status colours were also
  theme-*independent* brights: pale yellow on white barely registered and the
  green washed out. Each now has a darker light-theme counterpart, badge glyphs
  flip to white over those darker fills, and unlit Wi-Fi bars lighten so they
  still read. The top bar stays dark in both themes, so its gear is explicitly
  white.

- **Wi-Fi badge overlapped the gear** in the wide launcher header by 2px. The
  header is rebuilt as two rows: branding on the left across two lines, the
  status block on the right behind a hairline divider. A font-4 title is about
  190px wide, so it was never going to share one line with a clock, two badges
  and a gear without crowding.

- **Counting overlapped itself.** The font-4 question spans roughly 60–260px
  when centred, while the right-aligned score started around 213px — both drawn
  at the same y. Score and streak now share one small line beneath the question.

### Changed

- **Settings tabs look like tabs.** `Ui::drawTab` draws browser-style tabs: the
  active one is page-coloured with rounded top corners only and breaks the
  baseline beneath itself so it merges into the content area. Inactive tabs sit
  lower and darker. They previously rendered as ordinary buttons.

- Settings rows tightened from 40px to 36 to make room for the slider; verified
  clear down to the slider ending at y=232 of 240.

- README now pictures **every one of the 23 games** — 33 generated screens in
  total. `tools/gen_screens.py` takes each game's geometry from its own `Rect`
  helpers in `src/games/`.

## 2.0.0 — 2026-08-11

The geography release. Three new games, real country artwork, spaced
repetition, and a working clock.

Flash 2,302,437 / 3,145,728 bytes (73.2%) · RAM 50,312 / 327,680 (15.4%)

### New games

- **Flags** — a real flag, name the country; correct answers unlock a
  **capital-city bonus** round. 195 flags.
- **Countries** — a real country outline, alternating *"which country?"* and
  *"which continent is it in?"*. 191 outlines.
- **Number Line** — a marker hops along a number line to reach the answer.

All three existed as source files but were **never registered** in `main.cpp` —
no include, no enum entry, no member — so they were unreachable. The launcher
went from 20 to 23 games.

Artwork comes from a new companion library,
[**map-n-flag**](https://github.com/iamankushpandit/map-n-flag): 4-bit indexed
images totalling 1.11 MiB, costing **zero RAM** because they stream from flash.

### Learning behaviour

- **Spaced repetition.** Questions were drawn uniformly at random, so a player
  saw Brazil as often as Bhutan and got no extra practice on misses. Flags and
  Countries now keep a mastery score per country; a miss costs **twice** what a
  correct answer earns, and selection weights a recently-missed country **8×**
  against **1×** for a mastered one. The two games track separately.
- **Adaptive difficulty.** Easy (30 countries) / Medium (62) / Hard (195), with
  six correct in a row promoting a tier automatically.
- **Finger Counting rebuilt.** It previously showed "3 + 4 = ?" and expected the
  player to tap 7 fingers — which requires already knowing the answer, so it
  tested arithmetic rather than teaching counting. It now alternates *"How many
  fingers?"* and *"Show me 7 fingers"*, drawn as real hands.
- **Subtraction animation loops** in Shape Arith, so a player who looks away can
  re-watch it.

### Accessibility

- **Cinnamon no longer flashes the screen.** It repainted the entire display on
  every step, producing a full-screen flash about once a second —
  uncomfortable generally and a genuine risk for photosensitive players. It now
  repaints only the pads that changed, so there is **no full-area luminance
  change at all**, and mirrors each colour on the case LED.

### Wi-Fi and clock

Every one of these was found by instrumenting the device over serial.

- **Scanning worked, then silently returned nothing.** The async
  `scanNetworks()` / `scanComplete()` pair was failing on this board. An
  isolated radio test (`pio run -e wifidiag`) found **58 access points** with a
  blocking scan, proving the hardware was fine. The app now uses the blocking
  form, dedupes by SSID keeping the strongest signal, and sorts strongest first.
- **Credentials were never saved.** The SSID was re-derived from
  `WiFi.SSID(index)` at JOIN time; once the driver's scan table was freed that
  returned an empty string, so an empty SSID was stored while the connection
  still succeeded from the ESP32's own copy. The name is now captured when the
  row is tapped.
- **NTP never answered.** lwIP's SNTP stays silent on some networks, so the
  clock is now set by a direct UDP query, with SNTP kept configured across three
  servers as a backup.
- **The clock was pinned to UTC.** `configTime(0, 0, ...)` hardcoded a zero
  offset. A single `applyTimeConfig()` now programs it, using **POSIX TZ rules**
  (`CST6CDT,M3.2.0,M11.1.0`) so daylight saving is handled automatically.
- **"Sync now" was blocked by the Auto-time switch** — an explicit request now
  ignores it.
- Time zone is auto-detected from the public IP, overridable from a paged
  picker of named zones.

### Interface

- **Wi-Fi and NTP consolidated** into one **Network & Time** screen.
- **Clock-sync and Wi-Fi badges** in the top bar and both launcher headers; the
  Wi-Fi one shows real signal strength (4/3/2/1 bars from RSSI).
- **Date shown** alongside the time.
- **Portrait launcher**: 2×2 tiles, four per page, USB edge at the bottom.
  Landscape flipped to match.
- **3D bevel** added centrally in `Ui::drawButton`, so every button in every
  game gained it from one change.
- **Screen saver** returns to whatever you were doing rather than the home
  screen, and swallows the wake-up touch — which previously landed as a phantom
  press and launched a random game. Paddles sweep opposite ways, and each rally
  speeds the ball up and changes colour, mirrored on the case LED.
- **RGB LED** driven by PWM and hooked into `beepOk`/`beepError`, so all 23
  games signal right and wrong without any per-game change.
- **About** is generated from the game catalogue; it had fallen six games behind.

### Fixes

- **The build was broken.** `ObjectAddGame.cpp` referenced a `Phase` enumerator
  that did not exist in its header.
- **Finger Counting could ask for 11 fingers** — `1 + random(6)` allowed totals
  above ten.
- **Blank launcher.** Switching Wide → Tall shrinks the page size, which could
  leave the page index past the end.
- Overlapping rectangles in Settings, the Wi-Fi list and Finger Counting.
- Duplicate answer options were possible in Flags and Countries.
- Continent names truncated to "North Americ." in the answer buttons.

### Internal

- **`engine/GameCatalog`** is the single source of truth for the game list. Ids,
  titles and labels previously lived in **two arrays coupled by index**, with
  nothing enforcing agreement.
- **`engine/Progress`** — reusable per-item mastery tracking.
- **`Game::markFullDirty()` / `needsFullRender()`** — the groundwork for partial
  redraw. Cinnamon is converted; the other games still clear the whole screen.
- **`pio run -e wifidiag`** — an isolated radio test that builds with no
  display, touch or game code.

### Known limitations

- 22 of 23 games still repaint the whole screen on every change, which is
  visible as flicker. Only Cinnamon has been converted.
- The country outlines (mapsicon) are **not licensed for resale**. Anything
  commercial needs them swapped for Natural Earth first.
- Tall layout applies to the launcher only; every game is authored against the
  fixed 320×240 landscape canvas.

---

## 1.0.1

Launcher company line layout fix.

## 1.0.0

Initial firmware: 20 games, theme and layout settings, touch calibration,
optional SD-card content, Pong screen saver.
