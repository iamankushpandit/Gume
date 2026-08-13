# src/hal

The only place that talks to hardware. Game code reaches all of this through `GameHost::board()`.

## Board.{h,cpp}

### Persistence and profiles

NVS via `Preferences`, namespace `cydkids`.

`scopedKey()` is **private**. It prefixes `p{N}_` inside `getScore` / `setScore` / `saveBestScore` / `worstScore` / `loadBlob` / `saveBlob`, so callers pass a plain key and get per-profile storage for free — that is how all games became per-profile without touching a single game file. Never construct a prefixed key by hand.

Five child slots plus a permanent Guest (`MAX_KIDS = 5`, `GUEST_INDEX = 5`, names capped at `PROFILE_NAME_MAX = 9` — `NAME_MAX` is a POSIX macro, hence the odd name). **Guest drops every write** through the scoped setters; that is deliberate, not a bug to fix.

Per-profile: scores, best/worst, mastery blobs, game visibility (`gameVisibleFor`). Global: theme, layout, brightness, Wi-Fi credentials, NTP, timezone.

`factoryReset()` wipes everything and reboots.

### Touch

Bit-banged SPI to the XPT2046 (the TFT owns HSPI). `pollTouch()` averages samples, applies `TOUCH_PRESSURE_THRESHOLD`, maps through a 3-point affine calibration, and compensates for the active rotation — so game code should never adjust coordinates itself. Calibration persists in NVS behind a magic number; `runTouchCalibration()` re-runs the wizard.

`TouchPoint` carries `down`, `justPressed`, `justReleased`, `x`, `y`, `pressure`; edge flags come from diffing against `lastTouch_`.

### Display

Owns the `TFT_eSPI` instance. Calibration is captured in rotation 1 and derived for other rotations by transform, so `setDisplayRotation()` is safe at runtime. Brightness is PWM on the backlight pin with a floor of `BRIGHTNESS_MIN = 25` — below that the panel is unreadable and a child could not find the slider to undo it.

### RGB LED

LEDC PWM, common anode (inverted drive). `setRgbColor()` holds a colour, `pulseRgb()` shows one briefly, `tickRgb()` fades it and must be called every frame from the main loop. `beepOk()`/`beepError()` are the game-facing wrappers — there is no audio despite the name; `PIN_SPEAKER` is stubbed.

The red and green GPIOs are physically crossed on this unit versus the standard pinout. `BoardConfig.h` already accounts for it (`PIN_RGB_R = 16`, `PIN_RGB_G = 4`) and it was verified on hardware — leave it alone.

### Wi-Fi and time

Wi-Fi exists only to set the clock. `tickTimeSync()` drives a non-blocking `Idle → Connecting → Syncing → Synced` state machine from the main loop, re-syncing every ~5 minutes. `ntpUdpProbe()` queries NTP over raw UDP and doubles as a diagnostic and a fallback that sets the clock directly when lwIP's SNTP never answers.

Credentials are cached in RAM (`wifiCacheLoaded_`) because the state machine polls `hasWifiCredentials()` at ~27 Hz and hitting NVS that often wasted cycles and flooded the log with `nvs_get_str NOT_FOUND`.

Timezone is stored in **minutes** to support :30 and :45 zones, and comes from a named POSIX zone or a public-IP lookup — DHCP options 100/101 exist but are essentially never implemented by routers.

## Clock.{h,cpp}

Time/date formatting and sync-state helpers over the ESP32 RTC.

## Battery sensing

`PIN_BAT_ADC` is GPIO34 = **ADC1**_CH6, and that matters: ADC2 is unusable while Wi-Fi is associated, and this radio comes up for NTP. Don't move battery sensing to an ADC2 pin.

`readBatteryTelemetry()` caches one sample set for `BATTERY_SAMPLE_MS` (2s) and every other accessor — `getBatteryVoltage()`, `getPowerSource()`, `isBatteryPresent()`, `getBatteryPercent()` — reads through it. **Keep it that way.** Before this, each accessor ran its own 10-sample conversion with a `delay(1)` between samples, and `Ui::drawTopBar()` calls two of them: every top bar cost ~20ms of blocking delay, and the System Info board tab ~30ms per repaint. That is most of a frame, and it is what made scrolling feel sluggish.

Conversion goes through `esp_adc_cal`, not `raw / 4095 * 3.3`. At 11dB the ESP32's ADC is nominally 0–3.1V but only linear to ~2.45V, and its reference varies 1000–1200mV part to part; the eFuse calibration corrects both. The old comment claiming "3.3V reference with 11dB attenuation" described two mutually exclusive things.

Percentage uses a piecewise LiPo discharge curve. A linear 3.2–4.2V map reads roughly 20 points high through the middle, because a cell sits near 3.7V for most of its life.

`isBatteryPresent()` is deliberately **not** `getPowerSource() == BATTERY` — the old version returned false whenever the device was on external power, which reads as "no battery fitted" and is not the question being asked.

Still assumed and still needing a meter: `DIVIDER_RATIO` (100k/100k) and the no-battery behaviour of the pin. The board exposes no charge-status line, so `ChargingState` stays `UNKNOWN`.

## BleBeacon.{h,cpp}

Opt-in, non-connectable BLE presence beacon in namespace `BleBeacon`. Off by default; `Board::bleBeaconEnabled()` / `setBleBeaconEnabled()` hold the switch in the `cydkids` NVS namespace, so a factory reset clears it. `Board::begin()` calls `BleBeacon::begin()` which always builds the advertisement and only powers the radio when opted in.

**The invariant that matters here:** there is exactly one description of the outgoing advertisement. `Advertisement` is compiled by `buildPayload()` into a raw AD-structure buffer, `startRadio()` hands the controller *that buffer* via `NimBLEAdvertisementData::addData()` rather than the per-field helper setters, and the System Info BLE tab reads the same buffer back to display it. Never add a second, hand-written description of the payload to the UI — that is the only way the screen and the radio can drift apart. See `docs/BLE_BEACON_SPEC.md`.

`broadcasting()` returns `nullptr` unless the controller is actually advertising. UI must key off that rather than off the stored setting, which can read On while the radio failed to come up; nothing may be labelled as on air unless it is.

On air: Flags, Complete Local Name (`LearnKey-<id>`) and manufacturer data (company `0xFFFF`, `"LK"`, version, two MAC bytes) — 27 of the 31 legal bytes. The device id is the last two bytes of the factory BT MAC. Nothing profile-scoped is read by this module at all.

`startRadio()` and `stopRadio()` both take a `Watchdog::Pause` — bringing the controller up blocks for a couple of hundred milliseconds and looks exactly like a hang from the loop task. `pause()` no-ops while the watchdog is unarmed, so calling this from `Board::begin()` is safe.

Stopping deinitialises the whole stack (`NimBLEDevice::deinit(true)`) rather than just halting advertising: leaving it up holds ~30 KB of heap the games would rather have, and "off" should mean off.

## Watchdog.{h,cpp}

Background supervisor for the main loop, in namespace `Watchdog`. Games never interact with it.

Two independent layers: the ESP32 hardware task watchdog subscribed to the Arduino loop task (reboots after `TIMEOUT_SECONDS = 12`, so a hung game can't leave a child at a frozen screen), and a low-priority FreeRTOS monitor task pinned to core 0 that samples a heartbeat, frame times and heap once a second. The monitor logs a stall at `STALL_WARN_MS = 3000` — well before the hardware watchdog fires — and keeps a breadcrumb in RTC memory recording the active screen, uptime and heap low-water mark. On the next boot the breadcrumb is printed, and if the reset was unclean it's persisted to NVS so it survives a power cycle too. `lastRun()` exposes it.

Two contracts worth respecting:

- The hardware watchdog is armed lazily on the **first `feed()`**, not in `begin()`, because boot can legitimately block for minutes inside the touch-calibration wizard waiting for a finger.
- Anything that blocks for a long time on purpose must sit between `pause()` and `resume()` — calibration and factory reset already do.

Use `setContext()` to label the current screen so a post-crash report says where it happened. `main.cpp` sets it on every view change, taking game names from `GAME_CATALOG` via `kindTitle()` so the label cannot drift from the launcher.

`pause()`/`resume()` are reference counted and `Watchdog::Pause` is the RAII form — prefer it, because several of these call sites have early returns. Three places in `Board` already use it: `runTouchCalibration()` (waits for a finger), `ntpUdpProbe()` (DNS plus a 4s reply wait) and `detectTimezone()` (DNS plus a 5s HTTP round trip). Add the guard to anything new that blocks the loop task for seconds.

The crash record uses its **own** NVS namespace, `cydwdt`, not `cydkids`, and `factoryReset()` deliberately leaves it alone — a parent resetting a device that keeps crashing is exactly when that history matters. It is written only after an unclean reset, so flash wear is a non-issue; the per-second breadcrumb goes to RTC memory, which costs nothing.

`begin()` is called from `KidsPlatformApp::begin()` right after `Board::begin()` (so the crash report is the first thing in the log) and `feed()` is the first statement in `KidsPlatformApp::loop()`. The bringup env is wired the same way.
