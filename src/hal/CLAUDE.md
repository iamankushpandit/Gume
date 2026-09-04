# src/hal

The only place that talks to hardware. Ordinary games should not reach this directly; system screens use `GameHost::board()` and should prefer the narrow access facets when they only need one concern.

## Which board, and how this code knows

**Nothing in here may name a GPIO number, a panel size or a divider ratio.** Every hardware fact comes from `BOARD`, the one `BoardProfile` constant selected at compile time by `GUME_BOARD_HEADER` and defined in `include/boards/<id>.h`. That is what lets a second board be a header rather than a patch, and it is the rule that stops this directory becoming board-specific again.

A peripheral the board does not wire is `PIN_NONE`, and the caller guards: `BOARD.hasSdSlot()`, `hasRgbLed()`, `hasSpeaker()`, `hasBatterySense()`, `hasBacklightControl()`. Degrade quietly rather than fail -- a board with no battery connector blanks the gauge's digits, which is the honest answer, and needs no code change here at all. Adding a field to the contract means filling it in for every existing board in the same commit; `tools/check_boards.py` is what notices when that did not happen. `docs/PORTING.md` has the rest.

## Board.* + BoardAccess.h + BoardStorage.cpp + BoardStorageMaintenance.cpp

`Board.h` remains the legacy aggregate for existing call sites, but it also exposes no-vtable concern facades: `displayAccess()`, `touchAccess()`, `storageAccess()`, `powerAccess()`, `networkAccess()` and `feedbackAccess()`. New subsystem code should take the narrowest facade it can. The implementation is split by concern:

- `Board.cpp` - bring-up, profiles, layout/idle settings, visibility cache
- `BoardAccess.h` - narrow display/touch/storage/power/network/feedback forwarding facades
- `BoardDisplay.cpp` - TFT access, rotation, BMP blitting
- `BoardTouch.cpp` - touch ADC, calibration, coordinate mapping
- `BoardPower.cpp` - battery telemetry, backlight brightness, panel sleep/wake
- `BoardNetwork.cpp` - Wi-Fi credentials, timezone, NTP sync, network activity log
- `BoardFeedback.cpp` - RGB LED, BLE and Nearby enable switches
- `BoardAudio.cpp` - the sound engine: codec bring-up, the cue tables, the
  phoneme table for the spoken boot phrase, and the per-sample synthesiser
- `Sound.h` - the cue vocabulary, shared with app code without pulling in
  `Board.h`
- `BoardStorage.cpp` - scoped persistence, schema versioning, migration
- `BoardStorageMaintenance.cpp` - profile-slot moves, NVS usage telemetry
- `TouchTypes.h` - `TouchPoint` shared with app code without pulling in `Board.h` or the TFT driver

### Persistence and profiles

NVS via `Preferences`, in the legacy application namespace.

`BoardStorage.cpp` owns profile-scoped persistence, the storage schema version and the legacy-key migrator. `BoardStorageMaintenance.cpp` owns NVS usage telemetry and profile-slot moves. `scopedKey()` is **private**. It prefixes `p{N}_` and translates plain game keys into compact app-scoped leaves inside `getScore` / `setScore` / `saveBestScore` / `worstScore` / `loadBlob` / `saveBlob`, so callers pass a plain key and get per-profile storage for free. Never construct a prefixed key by hand.

Five player slots plus a permanent Guest (`MAX_PLAYERS = 5`, `GUEST_INDEX = 5`, names capped at `PROFILE_NAME_MAX = 9` — `NAME_MAX` is a POSIX macro, hence the odd name). **Guest drops every write** through the scoped setters; that is deliberate, not a bug to fix.

Per-profile: scores, best/worst, mastery blobs, game visibility (`gameVisibleFor`). Global: theme, layout, brightness, Wi-Fi credentials, NTP, NTP resync interval, timezone.

Deleting a player is a storage operation, not a name-list edit. `removePlayer()` clears the deleted slot's `pN_` keys, moves every later slot down by enumerating NVS entries in the application namespace, copies supported NVS types with fixed static scratch buffers, and clears the old last slot. Deleting the active player switches to Guest; deleting a lower slot shifts the active index down so the same real player stays active.

`storageTelemetry()` reports default-partition NVS entries plus the application and watchdog namespace entry counts. System Info -> Memory shows that data with 80% warning and 92% critical soft quota thresholds. Do not call it from per-frame render paths; it enumerates flash-backed metadata.

`factoryReset()` wipes everything in the application namespace and reboots. The watchdog crash namespace is deliberately separate.

### Touch

**Two kinds of controller, one coordinate.** `BOARD.touch.kind` says which the board wires, and `pollTouch()`'s rotation switch is shared by both — game code never adjusts coordinates itself either way.

*Resistive (XPT2046).* Bit-banged SPI (the TFT owns HSPI). `readResistiveTouch()` averages samples and applies `BOARD.touch.pressureThreshold`; `mapTouch()` runs the 3-point affine fit, which persists in NVS behind a magic number. `runTouchCalibration()` re-runs the wizard.

*Capacitive (FT6336U).* I²C. `readCapacitiveTouch()` does one 5-byte read from `TD_STATUS`, which is immediately followed by the first touch point — count and coordinates in a single transaction per frame. Two things about it are load-bearing:

- **It reports the panel's NATIVE portrait frame and has never heard of `setRotation()`.** `mapTouch()` rotates into the same landscape frame the affine fit produces, so everything downstream sees one kind of coordinate. **That quarter-turn is a property of the panel, not a convention** — it depends on which physical corner the controller calls its origin, and it was established on hardware, not derived: `src/s3_diag.cpp` drew a crosshair through this exact transform, tracked a finger at all four rotations, and carried the opposite handedness on a key to show it was wrong.
- **`hasTouchCalibration()` returns true unconditionally, and `runTouchCalibration()` returns immediately.** There is nothing to fit. This is not a convenience: every caller that sees `false` offers or forces the wizard, and on a capacitive panel that wizard **cannot be completed** — the first crosshair is a screen the owner never gets past. `pressure` is synthesised as `0xFFFF` because capacitive contact is binary and every caller only ever compares it to a threshold; reporting 0 would make a real press look like noise.

**Use `#if GUME_TOUCH_CAPACITIVE`, not `if constexpr`, to separate the two.** In a non-template function both arms of an `if constexpr` are still compiled, so the capacitive arm alone linked Wire into every resistive build — measured at **+4,476 bytes** of flash for code that can never run, on a budget already at 75%. The macro lives in the board header and the profile's `TouchKind` is *derived from it*, so there is still exactly one statement of the fact and the two cannot drift.

`TouchPoint` lives in `TouchTypes.h` and carries `down`, `justPressed`, `justReleased`, `x`, `y`, `pressure`; edge flags come from diffing against `lastTouch_`.

### Display

Owns the `TFT_eSPI` instance. Calibration is captured in rotation 1 and derived for other rotations by transform, so `setDisplayRotation()` is safe at runtime. Brightness is PWM on the backlight pin with a floor of `BRIGHTNESS_MIN = 25` — below that the panel is unreadable and a player could not find the slider to undo it.

Panel sleep/wake is observable: `displaySleepTelemetry()` reports sleep count, wake count, last sleep/wake times, last sleep duration and panel wake delay. `BoardPower.cpp` also logs `[display] sleep ...` and `[display] wake ...` lines over serial, and System Info shows the same counters under App -> Display sleep. This is instrumentation for soak testing, not a substitute for a physical long-duration run.

### Sound

`Sound.h` is the vocabulary; `BoardAudio.cpp` is everything else. Read the
header comment in that file before changing it -- three of its decisions look
arbitrary and are not.

**Nothing here is a recording and nothing here may become one.** Every sound is
generated a sample at a time from a script of `Segment`s: sine or square with
an optional linear sweep, band-passed noise, and three formant resonators
driven either by a monotone impulse train (voiced) or by noise (unvoiced).
That is a flash decision -- a second of 16-bit 16 kHz mono is 32 KB against a
budget already at 74.9% -- and it is also what makes a spoken phrase possible
at all. The whole vocabulary plus the phrase is under a kilobyte of const data.

**`playSound()` arms a script; it does not play it.** `tickAudio()` runs once
per frame from `BrainoApp::loop()` and writes only what the DMA will accept
with a zero timeout. The DMA is 6 x 256 frames, 96 ms at 16 kHz, so it stays
comfortably ahead of a 20 ms frame. Samples the DMA would not take are held in
`pending` and written first next frame rather than dropped: the synthesiser
cannot be run backwards, and dropping them is audible as a stutter on anything
long enough to fill the DMA -- which is exactly the boot phrase. Never make
this blocking. `src/s3_diag.cpp` does block, and is right to: a bring-up probe
has no frame budget.

**The voice is a phoneme table, not text-to-speech.** `PHRASE_LETS_PLAY_BRAINO`
spells "Let's play Braino!" out as L EH T S / P L EY / B R EY N OW, each entry
being the first three formant frequencies of a vocal tract shaped to say it.
There is no dictionary and no grapheme rules, so a second phrase means writing
its phonemes by hand -- which is the intended cost, not an oversight. Three
things in that table are load-bearing and are commented as such: a diphthong is
two segments, a stop is a silence *and then* a burst, and the fricative has to
be longer than a vowel to be audible at all.

**Mute and volume are global device settings, and both are RAM-mirrored.**
`soundEnabled()` sits on the path of every cue in every game, so reading it
from flash-backed Preferences per call is exactly the hot-path NVS read the
responsiveness rule is about. The mute gate lives in `Board::playSound()` and
nowhere else, which is what makes it true of the beeps and the boot phrase as
well as of the cues; `setSoundEnabled(false)` also stops whatever is currently
sounding, because Mute is the control somebody reaches for *while* the boot
phrase is playing. `setVolume()` clamps to `AUDIO_VOLUME_MAX` (now read from
`BOARD.audio.maxVolume`).

**The audio backend is now three-state, not binary.**

- `GUME_HAS_AUDIO_CODEC 1` — ES8311 codec (Freenove FNK0104B). Requires
  `Wire` + `driver/i2s.h`. Volume via register 0x32 (logarithmic — see
  `applyCodecVolume()`). `maxVolume` = 85 on this board.
- `GUME_HAS_AUDIO_DAC 1` — ESP32 built-in DAC via `I2S_DAC_BUILT_IN` (CYD
  boards: E32R28T-1, E32R40T, ESP32-2432S028R). Requires `driver/i2s.h`
  only; no Wire. Volume is linear amplitude scaling applied at sample time in
  `tickAudio()` — do **not** copy the dB conversion from `applyCodecVolume()`,
  which is correct only for a logarithmic register. Samples must be converted
  from signed int16 to unsigned offset-binary: `(sample * vol/100) + 32768`.
  Only the high 8 bits reach the DAC, giving 8-bit resolution. `maxVolume` =
  75 on CYD boards (bare DAC, unamplifed 1-inch driver distorts above 75%).
- Neither defined — no audio (e.g. esp32-2432s028 ST7789 variant).

`AUDIO_VOLUME_MAX` is now `BOARD.audio.maxVolume`, set per-board in
`BoardProfile`. A board that gains a louder amplifier sets its own ceiling;
a board with no speaker path sets it to 0.

`Board::hasSound()` covers both codec and DAC: a settings screen can offer
the mute switch and volume slider on any audio-capable board, not just
codec boards.

**Register 0x32 is logarithmic and was being driven as if it were linear.**
Half a decibel per step, `0xBF` unity, `0x00` silence -- so scaling the
percentage onto the byte, which is what both this and `src/s3_diag.cpp` did,
is wrong by up to 25 dB. The default 60% was landing on -19.5 dB and the
console was nearly inaudible across a room; 100% would have been +32 dB and
clipped everything. `applyCodecVolume()` now converts a percentage of
amplitude (100% unity, 50% -6 dB, 10% -20 dB) and clamps at unity, and
`es8311VolumeReg()` in the probe is the same function -- **change one, change
both**, for the same reason the battery constants carry that rule: a bench
instrument that disagrees with the product about what "80" means is worse than
no instrument.

**IDF version dependency.** `I2S_DAC_BUILT_IN` and `i2s_set_dac_mode()` are
IDF 4.4 (Arduino core 2.0.17) APIs. They were removed in IDF 5.x. If the
platform is ever bumped, the DAC backend in `beginAudio()` and `tickAudio()`
and the whole of `src/audiodiag.cpp` need rewriting with the new driver.

The knobs for retuning by ear are the formant numbers, the segment lengths,
`VOICE_PITCH_HZ` (monotone, which is what makes it robotic) and `NOISE_MAKEUP`
(raise it if the fricatives vanish under the vowels). Nothing else.

The codec register values and the three findings that are easy to get wrong --
active-low amplifier enable, MCLK from the APLL, ADC volume resetting to
minimum -- came from `env:s3diag` on real hardware and are documented at the
top of the file.

**`env:audiodiag`** is a standalone DAC bring-up probe (same pattern as
`batdiag`): builds `src/audiodiag.cpp` alone, no frame budget, no watchdog.
Sine wave at a configurable frequency, frequency sweep, volume steps and tone
bursts approximating each cue. The three questions it answers: does GPIO26
reach the speaker? Is output level audible? Where does distortion begin?

### RGB LED

LEDC PWM, common anode (inverted drive). `setRgbColor()` holds a colour, `pulseRgb()` shows one briefly, `tickRgb()` fades it and must be called every frame from the main loop. `beepOk()`/`beepError()` live in `BoardAudio.cpp` now and are the two cues that also pulse the LED; a screen wanting any other sound calls `playSound()` and pulses the LED itself if the moment deserves a colour. On a board with no audio path the pulse is the whole of the feedback, which is why the ones that replace a `beepOk()` keep it by hand. Whether the drive is inverted comes from `BOARD.rgb.commonAnode`, not from an assumption in the driver.

The red and green GPIOs are physically crossed on this unit versus the standard pinout. The E32R28T-1 profile already accounts for it (`rgb.r = 16`, `rgb.g = 4`) and it was verified on hardware — leave it alone.

### Wi-Fi and time

Wi-Fi exists only to set the clock. `tickTimeSync()` drives a non-blocking `Idle → Connecting → Syncing → Synced` state machine from the main loop. The success-path automatic resync interval is a write-through cached setting, 1–24 hours with a 6-hour default; boot sync, manual sync and failure retries are separate. `ntpUdpProbe()` queries NTP over raw UDP and doubles as a diagnostic and a fallback that sets the clock directly when lwIP's SNTP never answers.

**These are the only outbound flows the firmware has, together with the opt-in BLE beacon, and that list is closed.** No analytics, no usage or crash reporting, no other HTTP/UDP/DNS request, and nothing transmitted that carries a player's name, profile name, score, progress or typing. `BoardNetwork.cpp` is where such a thing would be added, so it is where it gets refused: a fourth flow needs agreement in an issue before the code exists. See [CONTRIBUTING.md](../../CONTRIBUTING.md#no-data-collection).

Credentials and the NTP hot-path settings are cached in RAM because the state machine polls them at ~27 Hz and hitting NVS that often wasted cycles and flooded the log with `nvs_get_str NOT_FOUND`.

Timezone is stored in **minutes** to support :30 and :45 zones, and comes from a named POSIX zone or a public-IP lookup — DHCP options 100/101 exist but are essentially never implemented by routers.

## BoardButton.cpp

The BOOT key — the only input on these boards that is not the touchscreen. A GPIO with an internal pull-up, sampled once per frame by the runtime, edge-detected in `pollBootButton()`. No interrupt and no task: the debounce *is* the sampling rate, and a tactile switch settles well inside a 20 ms frame. Poll it faster and it will need a real one.

Two things it deliberately does not do. It never reads the key during boot — BOOT is a strapping pin, the ROM samples it at reset to choose serial download mode, and that decision is made before `setup()` runs. And it does not time the press: a hold is not a gesture this firmware recognises, and a `heldMs` nobody reads would claim otherwise. The runtime acts on the press edge, so anyone adding a hold has to move that to the release edge first — you cannot both go home on the press and later decide the same press was the start of something longer.

`PIN_NONE` is a supported profile. The event is all-false on a board with no key, so callers need no `hasBootButton()` guard of their own.

## Clock.{h,cpp}

Time/date formatting and sync-state helpers over the ESP32 RTC.

## Battery sensing

`BOARD.battery.adcPin` is GPIO34 on this board = **ADC1**_CH6, and that matters: ADC2 is unusable while Wi-Fi is associated, and this radio comes up for NTP. Don't move battery sensing to an ADC2 pin.

`readBatteryTelemetry()` caches one sample set for `BATTERY_SAMPLE_MS` (2s) and every other accessor — `getBatteryVoltage()`, `getPowerSource()`, `getBatteryPercent()` — reads through it. **Keep it that way.** Before this, each accessor ran its own 10-sample conversion with a `delay(1)` between samples, and `Ui::drawTopBar()` calls two of them: every top bar cost ~20ms of blocking delay, and the System Info board tab ~30ms per repaint. That is most of a frame, and it is what made scrolling feel sluggish.

Conversion goes through `esp_adc_cal`, not `raw / 4095 * 3.3`. At 11dB the ESP32's ADC is nominally 0–3.1V but only linear to ~2.45V, and its reference varies 1000–1200mV part to part; the eFuse calibration corrects both. The old comment claiming "3.3V reference with 11dB attenuation" described two mutually exclusive things.

Percentage uses a piecewise LiPo discharge curve. A linear 3.2–4.2V map reads roughly 20 points high through the middle, because a cell sits near 3.7V for most of its life.

**There is no `isBatteryPresent()`, and there must not be one.** It used to exist and was a constant `true`. Measured on hardware 2026-08-24 with `batdiag` (8s averaged): pack + USB **4.224 V**, USB with **no pack 4.159 V**, pack alone **4.066 V**. The no-pack case sits *between* the other two, because the TP4054 holds BAT at float voltage with or without a cell — so no threshold separates them in either direction. This is measured, not inferred; do not reintroduce the question in a new form or retune a constant to answer it.

`V_SENSOR_MAX` (4.50V) is the plausibility ceiling that replaced `V_NO_BATTERY` (4.35V). It means **the ADC is faulty**, not "no pack": 4.35V was above anything this board produces, so it never fired, which left `getBatteryPercent()` unable to return -1 and the blank-digit rendering in `Ui::drawBatteryBadge` unreachable. With no pack the gauge reads *high*, near full.

Still assumed and still needing a meter: `DIVIDER_RATIO` (100k/100k).

## Charge detection

The board exposes no charge-status line — the charger's CHRG pin is not wired to the ESP32 — so `getChargingState()` **infers** the answer from the cell voltage alone. `updateChargeState()` runs once per *fresh* telemetry sample (so at `BATTERY_SAMPLE_MS`, not per frame, however often a render path asks) and reads three signals: a step of `CHARGE_STEP_V` between consecutive samples, which is what makes the icon respond to a cable within ~2s; a voltage held above `V_CHARGER_HELD` (4.21V), which no resting cell reaches; and the trend of a low-passed average over `CHARGE_WINDOW_MS` (45s) for everything in between.

Two things are deliberate and worth not undoing. **A flat window keeps the previous verdict** rather than resetting to unknown — mid-discharge a LiPo plateau spans 20mV across 40% of the capacity, so "no movement" is not evidence of anything. And **`FULL` is only reachable from `CHARGING`**, because a rested full pack and a finished charge are indistinguishable from one sample; claiming "charged" for a battery nobody watched charge is a lie the user would act on.

`getPowerSource()` returns `EXTERNAL_POWER` when the charge verdict says the cable is in, and that is the *only* way it can be reached. The voltage sits in the same range whether the cable is in, out, or there is no pack at all, so the verdict is the only thing that distinguishes them. The old "no pack at all (above `V_NO_BATTERY`)" branch was dead code and has been removed.

Three fixes proven on hardware and now in both `BoardPower.cpp` and `battery_diag.cpp` — **change one, change both**: `V_CHARGER_HELD` is 4.21V (4.24V sat above the 4.238V maximum the board actually produces, so it never fired); the negative-step test runs **before** the held-high level, so a cell that is falling while still above the trip is never called charging; and the flat-window fallback is guarded by `chargeSmoothV_ < CHARGE_FULL_V`, because it claimed to mean "flat and low" while never testing for low — a board booted on USB reported `DISCHARGING` after 45s.

`isBatteryLow()` / `isBatteryCritical()` (≤15% / ≤5%) are both false while charging, so plugging in silences the warning at once instead of waiting for the reading to climb.

## BleBeacon.{h,cpp}

Opt-in, non-connectable BLE presence beacon in namespace `BleBeacon`. Off by default; `Board::bleBeaconEnabled()` / `setBleBeaconEnabled()` hold the switch in the application NVS namespace, so a factory reset clears it. `Board::begin()` calls `BleBeacon::begin()` which always builds the advertisement and only powers the radio when opted in.

**The invariant that matters here:** there is exactly one description of the outgoing advertisement. `Advertisement` is compiled by `buildPayload()` into a raw AD-structure buffer, `startRadio()` hands the controller *that buffer* via `NimBLEAdvertisementData::addData()` rather than the per-field helper setters, and the System Info BLE tab reads the same buffer back to display it. Never add a second, hand-written description of the payload to the UI — that is the only way the screen and the radio can drift apart. See `docs/BLE_BEACON_SPEC.md`.

`broadcasting()` returns `nullptr` unless the controller is actually advertising. UI must key off that rather than off the stored setting, which can read On while the radio failed to come up; nothing may be labelled as on air unless it is.

On air: Flags, Complete Local Name (`Braino-<id>`) and manufacturer data (company `0xFFFF`, `"BR"`, layout version 2, two MAC bytes, a flag byte) — 27 of the 31 legal bytes. The device id is the last two bytes of the factory BT MAC. Nothing profile-scoped is read by this module at all.

With Nearby play on, `setActivity()` appends a game index and a best score, taking the payload to **exactly 31 bytes**. There is no slack left: a longer device name or a third AD structure pushes the manufacturer block off the air, and `buildPayload()` logs and drops it rather than transmitting a half-advertisement. Sharing off removes both fields rather than zeroing them — "not transmitted" has to be structural to be worth claiming.

`decode()` is the exact inverse of the manufacturer block and is what `BleScanner` reads peers with. Do not write a second parser: a transmit copy and a receive copy of a wire format drift, and the symptom is two consoles that silently cannot see each other.

`startRadio()` and `stopRadio()` both take a `Watchdog::Pause` — bringing the controller up blocks for a couple of hundred milliseconds and looks exactly like a hang from the loop task. `pause()` no-ops while the watchdog is unarmed, so calling this from `Board::begin()` is safe.

Stopping deinitialises the whole stack (`NimBLEDevice::deinit(true)`) rather than just halting advertising: leaving it up holds ~30 KB of heap the games would rather have, and "off" should mean off.

## BleScanner.{h,cpp}

Passive observer for other Braino beacons, in namespace `BleScan`. The radio half of Nearby play and nothing else — it holds no opinion about scores or notifications; that policy is `engine/NearbyPlay`, which is where the score catalog lives. The hal never has to know what a game is and the policy never has to know what an AD structure is.

Three properties are contract, not incidental:

- **Passive scan.** Never transmits a scan request, so listening adds nothing to what the device puts on air.
- **Both tests.** A peer must match the `Braino-` name prefix *and* decode as our manufacturer block at a known version.
- **Zero allocation.** Fixed eight-entry table; the advertisement is parsed straight out of the controller's buffer rather than through the library's `std::string` accessors, and `setMaxResults(0)` stops NimBLE keeping its own results vector. The callback runs on the NimBLE host task at whatever rate the air is busy — exactly the churn that fragments this heap.

The table is written from the host task and read from the loop task, so every touch of it sits inside a `portMUX` critical section, and `at()` returns **by value**. `startScan()`/`stopScan()` take a `Watchdog::Pause` for the same reason `BleBeacon` does.

## Watchdog.{h,cpp}

Background supervisor for the main loop, in namespace `Watchdog`. Games never interact with it.

Two independent layers: the ESP32 hardware task watchdog subscribed to the Arduino loop task (reboots after `TIMEOUT_SECONDS = 12`, so a hung game can't leave a player at a frozen screen), and a low-priority FreeRTOS monitor task pinned to core 0 that samples a heartbeat, frame times and heap once a second. The monitor logs a stall at `STALL_WARN_MS = 3000` — well before the hardware watchdog fires — and keeps a breadcrumb in RTC memory recording the active screen, uptime and heap low-water mark. On the next boot the breadcrumb is printed, and if the reset was unclean it's persisted to NVS so it survives a power cycle too. `lastRun()` exposes it.

Two contracts worth respecting:

- The hardware watchdog is armed lazily on the **first `feed()`**, not in `begin()`, because boot can legitimately block for minutes inside the touch-calibration wizard waiting for a finger.
- Anything that blocks for a long time on purpose must sit between `pause()` and `resume()` — calibration and factory reset already do.

Use `setContext()` to label the current screen so a post-crash report says where it happened. The runtime sets it on every screen change from the active app definition, or to `Launcher` for the home screen.

`pause()`/`resume()` are reference counted and `Watchdog::Pause` is the RAII form — prefer it, because several of these call sites have early returns. Three places in `Board` already use it: `runTouchCalibration()` (waits for a finger), `ntpUdpProbe()` (DNS plus a 4s reply wait) and `detectTimezone()` (DNS plus a 5s HTTP round trip). Add the guard to anything new that blocks the loop task for seconds.

The crash record uses its **own** NVS namespace, separate from the application data, and `factoryReset()` deliberately leaves it alone — a parent resetting a device that keeps crashing is exactly when that history matters. It is written only after an unclean reset, so flash wear is a non-issue; the per-second breadcrumb goes to RTC memory, which costs nothing.

`begin()` is called from `BrainoApp::begin()` right after `Board::begin()` (so the crash report is the first thing in the log) and `feed()` is the first statement in `BrainoApp::loop()`. The bringup env is wired the same way.
