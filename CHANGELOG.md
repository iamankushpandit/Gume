# Changelog

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
