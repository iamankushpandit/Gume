# Changelog

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
  unreadable, and a child who dragged it to the bottom would have no way to
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

- **Spaced repetition.** Questions were drawn uniformly at random, so a child
  saw Brazil as often as Bhutan and got no extra practice on misses. Flags and
  Countries now keep a mastery score per country; a miss costs **twice** what a
  correct answer earns, and selection weights a recently-missed country **8×**
  against **1×** for a mastered one. The two games track separately.
- **Adaptive difficulty.** Easy (30 countries) / Medium (62) / Hard (195), with
  six correct in a row promoting a tier automatically.
- **Finger Counting rebuilt.** It previously showed "3 + 4 = ?" and expected the
  child to tap 7 fingers — which requires already knowing the answer, so it
  tested arithmetic rather than teaching counting. It now alternates *"How many
  fingers?"* and *"Show me 7 fingers"*, drawn as real hands.
- **Subtraction animation loops** in Shape Arith, so a child who looks away can
  re-watch it.

### Accessibility

- **Simon no longer flashes the screen.** It repainted the entire display on
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
  redraw. Simon is converted; the other games still clear the whole screen.
- **`pio run -e wifidiag`** — an isolated radio test that builds with no
  display, touch or game code.

### Known limitations

- 22 of 23 games still repaint the whole screen on every change, which is
  visible as flicker. Only Simon has been converted.
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
