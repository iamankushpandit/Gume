# Hardware Reference: E32R28T-1 (ESP32-32E)

Complete hardware specification and driver configuration for the E32R28T-1 / ESP32-32E 2.8-inch resistive-touch CYD board. This file is the single source of truth for supporting this board and a reference for porting to other boards.

**Last verified:** 2026-08-17  
**Firmware version:** 4.2.0 and later

---

## 1. Board Overview

| Specification | Value |
|---|---|
| **MCU** | ESP32-32E (ESP32-WROOM-32E module) |
| **Flash** | 4 MB (3 MB for app via `huge_app.csv` partition) |
| **PSRAM** | None |
| **RAM** | 520 KB SRAM |
| **Display** | ILI9341 controller, 240×320 pixels (2.8-inch) |
| **Display Colors** | RGB565 (16-bit) |
| **Touch** | XPT2046 resistive controller |
| **Power** | Single-cell Li-ion/LiPo (3.7V nominal), USB charging |
| **Wi-Fi** | 802.11 b/g/n (2.4 GHz), used for NTP only |
| **Bluetooth** | BLE (NimBLE stack, ~192 KB flash) |
| **Peripheral Bus** | SPI (multiple devices, careful bus sharing) |

---

## 2. GPIO Pinout

All pins defined in `include/BoardConfig.h`.

### Display (ILI9341) — HSPI Bus

| Function | GPIO | Direction | Notes |
|---|---|---|---|
| MOSI (DIN) | 13 | Out | SPI data to display |
| MISO (READ) | 12 | In | SPI data from display; used by TFT_eSPI for reads |
| SCLK (CLK) | 14 | Out | SPI clock |
| CS (Chip Select) | 15 | Out | Display chip select (active LOW) |
| DC (Data/Command) | 2 | Out | Register select: HIGH=data, LOW=command |
| RST (Reset) | — | — | Not wired; TFT_eSPI gets `-D TFT_RST=-1` |
| BL (Backlight) | 21 | Out | PWM brightness control (active HIGH) |

**SPI Configuration (TFT_eSPI flags in `platformio.ini`):**
```
-D USER_SETUP_LOADED=1
-D ILI9341_2_DRIVER=1
-D TFT_WIDTH=240
-D TFT_HEIGHT=320
-D TFT_MISO=12
-D TFT_MOSI=13
-D TFT_SCLK=14
-D TFT_CS=15
-D TFT_DC=2
-D TFT_RST=-1
-D TFT_BL=21
-D TFT_BACKLIGHT_ON=HIGH
-D USE_HSPI_PORT=1                 # Use hardware SPI port 1 (the TFT owns it)
-D SPI_FREQUENCY=40000000         # 40 MHz write
-D SPI_READ_FREQUENCY=16000000    # 16 MHz read
-D SUPPORT_TRANSACTIONS=1
```

**Key constraint:** Touch uses bit-banged SPI on dedicated pins because HSPI is fully occupied by the display.

### Touch (XPT2046) — Bit-Banged SPI

| Function | GPIO | Direction | Notes |
|---|---|---|---|
| MOSI (DIN) | 32 | Out | SPI data to touch controller |
| MISO (DOUT) | 39 | In | SPI data from touch controller |
| SCLK (CLK) | 25 | Out | SPI clock |
| CS (Chip Select) | 33 | Out | Touch chip select (active LOW) |
| IRQ (Interrupt) | 36 | In | Pressure interrupt (not presently used; polling only) |

**Calibration:** 3-point affine transformation, persisted in NVS under key `touchCal` with magic `0x43594431UL`. Loaded at boot; `runTouchCalibration()` re-runs the wizard.

**Thresholds:**
```
TOUCH_PRESSURE_THRESHOLD = 350   // Z1/(Z1+Z2) > threshold = press detected
TOUCH_HIT_SLOP = 8               // ±8 pixels for hit detection
```

**ADC Commands (XPT2046):**
- `0xD0`: Read X (12-bit)
- `0x90`: Read Y (12-bit)
- `0xB0`: Read Z1 (pressure lower plate)
- `0xC0`: Read Z2 (pressure upper plate)

**Bit-banging protocol:** 8 bits command (MSB first), 16 bits result (MSB first), 4-bit discard, 12-bit ADC value. See `src/hal/BoardTouch.cpp` for full implementation.

### Battery Sensing

| Function | GPIO | Type | Notes |
|---|---|---|---|
| Battery ADC | 34 | ADC1_CH6 | Analog input from voltage divider |

**CRITICAL:** GPIO34 is **ADC1_CH6** — never use ADC2 for battery (ADC2 is unavailable while Wi-Fi is associated, and this radio is always up for NTP).

**Divider:** 100k / 100k (predicted; needs meter verification)
```
V_cell = ADC_millivolts × 2.0  (DIVIDER_RATIO = 2.0f)
```

**ADC Calibration:** eFuse-backed via `esp_adc_cal` with `ADC_ATTEN_DB_11` (0–3.1V range, linear to ~2.45V).
- Do NOT use naive `raw / 4095 × 3.3`; that is wrong twice over (reference varies 1000–1200 mV part-to-part, and 11dB linearity limit is 2.45V).
- `esp_adc_cal_characterize()` called once on first battery read; result cached in static.

**Sampling:** 8 samples averaged, result cached for `BATTERY_SAMPLE_MS = 2000` to avoid hammering NVS and loop load. One battery read costs ~10ms blocking delay in conversions. **Do not call battery getters more than once per frame update.**

### RGB LED — PWM (Common Anode, Inverted)

| Function | GPIO | Drive Logic | Notes |
|---|---|---|---|
| Red | 16 | LOW = light | ⚠️ **CROSSED on this board** |
| Green | 4 | LOW = light | ⚠️ **CROSSED on this board** |
| Blue | 17 | LOW = light | Standard wiring |

**⚠️ HARDWARE QUIRK — RED and GREEN are crossed:**
- **Verified on hardware:** Driving GPIO4 HIGH leaves red OFF and green ON.
- Expected: GPIO4 = red, GPIO16 = green.
- Actual: GPIO4 = green, GPIO16 = red.
- If you see orange (R255 G110) output as green and purple (R200 B255) as cyan, the swap is working correctly.
- **DO NOT "FIX" THIS.** It is intentional in `BoardConfig.h` and verified on device. Any future board must have its own quirk check.

**PWM:** LEDC channel 4, 5 kHz PWM, 8-bit depth (0–255), PWM_HIGH_MODE.

**Example code:**
```cpp
// Common anode: LOW = light, HIGH = off
ledcWrite(BL_CHANNEL, 255 - brightness);  // 0 = bright, 255 = off
```

### Speaker

| Function | GPIO | Status |
|---|---|---|
| Speaker | 26 | Stubbed (no audio) |

Firmware calls `beepOk()` / `beepError()`, which pulse the RGB LED instead of producing sound.

---

## 3. Display Driver: ILI9341

| Property | Value |
|---|---|
| **Controller** | ILI9341 |
| **Bus** | SPI (HSPI, 40 MHz write / 16 MHz read) |
| **Resolution** | 240×320 pixels |
| **Color Depth** | 16-bit RGB565 |
| **Rotation** | Software (TFT_eSPI) |
| **Frame Rate** | 50 Hz (20 ms per frame) |
| **Backlight** | PWM on GPIO21, floor 25% |

### Initialization (TFT_eSPI)

Handled by `TFT_eSPI` library; `Board::begin()` calls `tft_.init()` which:
1. Initializes HSPI (40 MHz write, 16 MHz read)
2. Sends ILI9341 init sequence
3. Sets rotation to `CYD_SCREEN_ROTATION=3` (landscape, USB edge bottom)

Reset is not wired; TFT_eSPI runs soft reset via command sequence.

### Rotation

Rotation is set at boot to value 3 (landscape, USB edge at bottom). `setDisplayRotation(rotation)` is safe at runtime and used only by system apps (game layouts are fixed landscape).

| Rotation | Canvas | Physical Orientation |
|---|---|---|
| 0 | 240×320 | Portrait, USB top |
| 1 | 240×320 | Portrait, USB bottom |
| 2 | 320×240 | Landscape, USB top |
| **3** | 320×240 | **Landscape, USB bottom** (default) |

**Touch compensation:** `pollTouch()` applies affine calibration + rotation compensation automatically. Game code never adjusts coordinates.

### Backlight (GPIO21)

PWM brightness control on LEDC channel 4:
- Frequency: 5 kHz
- Depth: 8-bit (0–255)
- Floor: `BRIGHTNESS_MIN = 25` (11%) — below this the panel is unreadable and a player cannot see the slider to undo it
- Ceiling: 255 (100%)

**Drive logic:** Common anode (`HIGH` = off, `LOW` = on), but LEDC inverts for convenience:
```cpp
ledcWrite(BL_CHANNEL, brightness);  // 0 = off, 255 = full bright
```

**Panel sleep/wake:** Backlight PWM continues while asleep; the panel itself enters low-power state. Wake delay is ~120 ms (blocking). Guarded by `Watchdog::Pause`.

---

## 4. Touch Controller: XPT2046

| Property | Value |
|---|---|
| **Protocol** | SPI (bit-banged on dedicated pins) |
| **Sampling** | Pressure (Z1, Z2), X, Y coordinates |
| **Pressure Threshold** | 350 (Z1/(Z1+Z2)) |
| **Calibration** | 3-point affine, persisted in NVS |
| **Coordinate Mapping** | Automatic, per rotation |

### Calibration Procedure

1. Power on: check NVS key `touchCal` for magic `0x43594431UL`
2. If missing/invalid: show calibration wizard
3. Wizard: tap 3 cross-hairs at known screen locations
4. Compute affine transform from raw → screen coordinates
5. Persist to NVS with magic

**Affine transform (6 coefficients):**
```
screen_x = ax × raw_x + bx × raw_y + cx
screen_y = ay × raw_x + by × raw_y + cy
```

Matrix is computed from 3 raw/screen coordinate pairs and persisted. No recalculation needed until next calibration.

### Bit-Bang Implementation (BoardTouch.cpp)

Command structure: 8-bit command (MSB first), then 16-bit result (MSB first, last 12 bits used).

```cpp
// Example: Read X coordinate
uint16_t X = readTouchAdc(0xD0);  // Command 0xD0
// Returns 12-bit ADC value (bits 15–4 of 16-bit read)
```

Commands:
- `0xD0`: Read X (ADC channel)
- `0x90`: Read Y
- `0xB0`: Read Z1 (pressure, lower plate)
- `0xC0`: Read Z2 (pressure, upper plate)

**Sampling logic:**
1. Read Z1 (pressure lower)
2. Read Z2 (pressure upper)
3. Calculate touch pressure: `Z = Z1 / (Z1 + Z2)`
4. If `Z > TOUCH_PRESSURE_THRESHOLD` (350 / 1024 ≈ 34%), then:
   - Read X, Y
   - Apply affine calibration
   - Apply rotation compensation
   - Emit `TouchPoint` with coordinates

**Pressure averaging:** Each coordinate is averaged from multiple samples in `pollTouch()`. **Do not call polling more than once per frame.**

---

## 5. Battery and Power Management

### Power Sources

1. **Battery (Li-ion/LiPo):** 3.7V nominal, sensed on GPIO34 via voltage divider
2. **USB:** 5V, connected through charging circuitry

### Voltage Sensing

**Divider equation:**
```
V_cell = ADC_reading_mV × 2.0  (predicted from 100k/100k divider)
```

**ADC calibration (critical):**
```
esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                         ADC_DEFAULT_VREF_MV=1100, &s_adcChars);
voltage_mV = esp_adc_cal_raw_to_voltage(raw, &s_adcChars);
```

- Attenuation: 11 dB (0–3.1V nominal range, linear to ~2.45V)
- Reference: eFuse calibration per unit, ~1100 mV typical (±100 mV variation)
- Sample count: 8 samples averaged

**TODO (hardware validation needed):**
- Divider ratio verification (assumed 100k/100k, untested)
- No-battery threshold (assumed `V_NO_BATTERY = 4.35V`)
- USB-only voltage behavior

### Charging Inference

This board has **no CHRG pin**; charging state is inferred from cell voltage alone via three signals:

#### Signal 1: Voltage Step (≥0.060V between samples)
Attaching USB lifts terminal voltage >0.060V within ~2 seconds. Unplugging drops it the same way. **Response time: ~2 seconds.**

#### Signal 2: Held High Voltage (≥4.24V)
No resting cell reaches 4.24V off a charger; that threshold is specific to charger output holding it there.

#### Signal 3: 45-Second Trend (±0.012V movement)
LiPo discharge plateau: 40% capacity spans ~20mV, so the window must be long enough that real movement clears ADC noise. A flat trend keeps the previous verdict (no flapping).

**Critical invariant:** `FULL` is only reachable from `CHARGING` state. A rested full pack (4.13–4.15V off cable) and a just-finished charge read identically; claiming "charged" without watching it charge is a lie the user will act on.

#### State Machine (BoardPower.cpp)

| State | Entered when | Meaning |
|---|---|---|
| UNKNOWN | No pack or implausible voltage | Cannot determine |
| CHARGING | Step ≥0.060V, or voltage ≥4.24V, or trend ≥0.012V | Charger active |
| DISCHARGING | Step ≤-0.060V, or trend ≤-0.012V | Running on battery |
| FULL | CHARGING → flat + ≥4.13V | Charge complete (taper detected) |

**Charging inference thresholds:**
```cpp
constexpr float CHARGE_STEP_V = 0.060f;        // plug/unplug
constexpr float V_CHARGER_HELD = 4.24f;        // charger floor
constexpr float CHARGE_FULL_V = 4.13f;         // taper detection
constexpr uint32_t CHARGE_WINDOW_MS = 45000;   // slow trend window
constexpr float CHARGE_TREND_V = 0.012f;       // trend threshold
```

### Percentage Mapping

Piecewise LiPo discharge curve (NOT linear):
```cpp
constexpr CurvePoint LIPO_CURVE[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.93f, 70}, {3.87f, 60},
    {3.82f, 50},  {3.79f, 40}, {3.77f, 30}, {3.74f, 20}, {3.68f, 10},
    {3.55f, 5},   {3.20f, 0},
};
```

Linear mapping (3.2–4.2V) reads ~20% high through the middle because a cell sits near 3.7V for 60% of its life.

### Battery Warnings

Two thresholds, both disabled while charging (plugging in clears warning immediately):

| Level | Voltage | Message | Frequency |
|---|---|---|---|
| Low | ≤15% | "Battery low — time to charge" | 6s on, repeat every 2min |
| Critical | ≤5% | "Battery empty — plug in the charger" | 6s on, repeat every 2min |

**Critical rule:** Both predicates (`isBatteryLow()`, `isBatteryCritical()`) return `false` while `chargeState_ == CHARGING || FULL`. Plugging in clears the warning at once, not after voltage climbs.

---

## 6. Radio: Wi-Fi (lwIP + SNTP)

### Purpose
NTP time synchronization only. No telemetry, no data logging.

### State Machine (Non-blocking)
- **Idle** → (credentials available) → **Connecting**
- **Connecting** → (joined network) → **Syncing**
- **Syncing** → (NTP reply) → **Synced**
- **Synced** → (re-sync timer ~5 min) → **Idle**

Driven by `Board::tickTimeSync()` from the main loop (20 ms budget).

### NTP Configuration

- **Primary server:** `pool.ntp.org` (via lwIP SNTP)
- **Fallback:** `ntpUdpProbe()` raw UDP query (2s timeout) when SNTP times out
- **Cache:** Credentials loaded into RAM on boot to avoid NVS reads at 27 Hz

### Timezone

Stored in **minutes** (supports :30 / :45 zones):
```cpp
constexpr int16_t TZ_UNSET = -32768;  // sentinel
int16_t tzOffsetMin;  // offset in minutes (e.g., -300 = UTC-5)
```

- **Detection:** Public IP lookup on first connect (returns named POSIX zone)
- **Override:** Manual timezone picker in Settings
- **POSIX zone example:** `CST6CDT,M3.2.0,M11.1.0` (US Central with DST rules)

### Credentials
Stored in the application NVS namespace:
- SSID
- Password
- Cached flag (`hasWifiCredentials()` reads RAM mirror after boot load)

---

## 7. Bluetooth: BLE Beacon (NimBLE)

### Configuration
- **Stack:** NimBLE-Arduino (~192 KB flash, far less than Bluedroid)
- **Mode:** Advertiser only (non-connectable)
- **Default:** OFF (opt-in from Settings)

### Advertisement Payload (≤31 bytes, **exactly 31 with Nearby on**)

| AD Type | Contents | Bytes |
|---|---|---|
| Flags | LE General Discoverable, BR/EDR not supported | 3 |
| Complete Local Name | `Braino-<id>` (e.g., `Braino-A4F2`) | 13 |
| Manufacturer Data | Company 0xFFFF, `"BR"`, layout v2, MAC[1:0], flags | 9–12 |
| (Nearby only) Game index + best score | 4 bytes | 4 |

**Total: 27 bytes (no Nearby) or 31 bytes (Nearby on).**

**Manufacturer data structure:**
```
Bytes 0–1:  Company ID (0xFFFF, little-endian)
Bytes 2–3:  Manufacturer tag ("BR", ASCII)
Byte 4:     Layout version (currently 2)
Bytes 5–6:  Last 2 bytes of BT MAC (device ID)
Byte 7:     Flags (nearby flag, etc.)
[Bytes 8–11 if Nearby: game index (2 bytes) + best score (2 bytes)]
```

**Device ID:** Last 2 bytes of factory BT MAC (4 hex digits), stable across boots, not derived from any player input.

### Nearby Play (Optional)

**Off by default.** Requires two opt-ins:
1. BLE beacon on (Settings → Beacon)
2. Nearby on (Settings → Device → Nearby, or Nearby screen)

When on, appends to manufacturer data:
- Open game index (1–30)
- Best score for that game (uint16_t)

**No name, no profile, no progress, nothing profile-scoped.**

---

## 8. Memory and Storage

### NVS (Non-Volatile Storage)
- **Namespace:** application profile/settings store
- **Secondary namespace:** `cydwdt` (watchdog crash breadcrumbs, separate to persist across factory reset)
- **Partition:** Default NVS (not app-scoped)

### Profile Storage

Five player slots (`p0_` through `p4_`) plus permanent Guest (`p5_`, writes dropped):
- Per-profile: scores, best/worst, mastery blobs, game visibility
- Global: theme, layout, brightness, Wi-Fi credentials, timezone, BLE beacon state

**Profile operations:**
- `removePlayer(index)` → clears `pN_` keys, shifts later slots down, clears old last slot
- Deleting active player → switches to Guest
- Deleting lower slot → active index shifts down (same real player stays active)

---

## 9. Watchdog and Reliability

### Hardware Watchdog
- **Supervises:** Main loop (core 1, Arduino task)
- **Timeout:** 12 seconds
- **Action on timeout:** Hard reboot (cannot escape a hung game)

### Software Monitor
- **Task:** Low-priority FreeRTOS, core 0
- **Frequency:** Once per second
- **Logs:** Frame times, heap, stalls at `STALL_WARN_MS = 3000`
- **Breadcrumb:** Active screen, uptime, heap low-water mark → RTC memory (survives reboot)
- **Persistence:** Unclean reset → NVS namespace `cydwdt` (survives power cycle)

**Reference counting guards for long operations:**
```cpp
{
    Watchdog::Pause guard;  // increments pause counter
    runTouchCalibration();  // can block for minutes
    // destructor decrements counter and resumes if zero
}
```

---

## 10. Known Issues and Workarounds

### Red/Green LED Crossing (HARDWARE QUIRK)
- **Symptom:** Colors appear wrong (e.g., red looks green)
- **Root cause:** Board schematic swapped GPIO16 (R) and GPIO4 (G)
- **Fix:** Already applied in `BoardConfig.h` (`PIN_RGB_R = 16`, `PIN_RGB_G = 4`)
- **Verification:** Tested on hardware; orange (R255 G110) output confirmed green
- **Action:** DO NOT "FIX" this. Verify with hardware if porting.

### ADC Reference Calibration (CRITICAL)
- **Symptom:** Battery percentage jumps or reads incorrectly across units
- **Root cause:** ESP32 ADC reference varies 1000–1200 mV part-to-part; naive 3.3V assumption is wrong
- **Fix:** Use eFuse calibration via `esp_adc_cal` (already done in `BoardPower.cpp`)
- **Verification:** Meter each unit's divider ratio and ADC linearity range

### Battery Sensing on ADC1 Only
- **Symptom:** Battery reads stale or Wi-Fi fails while reading battery
- **Root cause:** ADC2 is unavailable while Wi-Fi is associated; must use ADC1
- **Fix:** GPIO34 is ADC1_CH6 (already selected)
- **Action:** Never move battery sensing to an ADC2 pin (GPIO17, GPIO18, GPIO19, GPIO23, GPIO25, GPIO26, GPIO27)

### Charge Detection Plateaus
- **Symptom:** Warning state flaps when on a discharge plateau
- **Root cause:** LiPo discharge plateau spans 20mV across 40% capacity
- **Fix:** 45-second trend window with flat-window preservation (already done)
- **Action:** Do not reduce `CHARGE_WINDOW_MS` below ~45 seconds

### No CHRG Pin
- **Symptom:** Cannot detect charging directly
- **Root cause:** Charger CHRG pin not wired to ESP32
- **Fix:** Infer from cell voltage (3-signal method, already done)
- **Verification:** Meter unit's charge behavior; adjust thresholds if needed

### Touch XPT2046 Sharing MISO
- **Symptom:** Touch is unresponsive while display is active
- **Root cause:** Would happen if touch used HSPI; HSPI is dedicated to display
- **Fix:** Touch uses bit-banged SPI on separate pins (GPIO32, 39, 25, 33)
- **Action:** Never move touch to HSPI

### Panel Sleep Wake Delay
- **Symptom:** Screen blanks/wakes slowly (~120 ms visible delay)
- **Root cause:** ILI9341 panel controller requires time to power-cycle
- **Fix:** Guarded by `Watchdog::Pause` to prevent timeout reboot during wake
- **Action:** Sleep/wake always from `Watchdog::Pause` context

### Touch Calibration Persistence
- **Symptom:** Touch calibration lost after factory reset
- **Root cause:** `factoryReset()` clears the application NVS namespace where calibration is stored
- **Fix:** Expected behavior (clean slate for re-sale/hand-off)
- **Action:** Wizard re-runs automatically if calibration magic is missing

---

## 11. Hardware Verification Procedures

Before porting to a new board, create a hardware test app that exercises:

### Display Test
1. Fill screen solid red, green, blue
2. Draw gradient rectangles
3. Verify rotation (text orientation)
4. Verify backlight PWM (brightness slider)
5. Verify sleep/wake (backlight off/on)

**Test code:** Adapt from `src/games/SystemInfoGame.cpp` or create `bringup` environment variant.

### Touch Test
1. Show crosshairs at calibration points
2. Tap each; verify coordinates within ±10 pixels
3. Save calibration; reboot; tap again
4. Verify pressure threshold (Z1/(Z1+Z2) threshold works)
5. Verify coordinate reversal after rotation

**Test code:** `src/main.cpp` bringup mode includes touch calibration wizard.

### Battery Test
1. Power on battery alone (no USB)
2. Read voltage, percentage, charging state (should be DISCHARGING)
3. Plug in USB with battery; read voltage, charging state (should be CHARGING within 2s)
4. Unplug USB; read voltage, charging state (should be DISCHARGING within 2s)
5. Meter actual cell voltage; verify ADC reads within 0.1V
6. Verify divider ratio with meter

**Test code:** System Info → Power tab shows all telemetry.

### BLE Test
1. Turn beacon ON (Settings → Beacon)
2. Use phone BLE scanner to see device name (`Braino-<id>`)
3. Decode manufacturer data (should be 9 bytes without Nearby)
4. Turn Nearby ON; re-scan (should be 13 bytes with Nearby, containing game index + score)
5. Turn beacon OFF; re-scan (should disappear)

**Test code:** System Info → BLE tab shows live payload dump.

### Wi-Fi Test
1. Enter SSID and password (Settings → Network & Time)
2. Wait for "Synced" (System Info → Network tab)
3. Verify time is set (clock shows correct time, not boot time)
4. Turn off Wi-Fi, reboot; verify clock time persists (RTC keeps time)

**Test code:** `pio run -e wifidiag -t upload` for isolated Wi-Fi test (display/touch/game code disabled).

### Watchdog Test
1. Load firmware
2. Check serial for boot breadcrumb: `[watchdog] boot...`
3. Trigger hang: add `while(1) delay(1000);` in a game, launch it
4. Wait 12 seconds; verify device reboots
5. Check serial for crash breadcrumb and last screen name

**Test code:** Hardware task watchdog is automatic; monitor serial output.

---

## 12. Porting Checklist for New Boards

When adding support for a new board (e.g., ST7789 variant, 4-inch ST7796), verify:

- [ ] **Pinout:** Collect all GPIO assignments (display, touch, battery, RGB, speaker)
- [ ] **Display driver:** Identify controller (ILI9341, ST7789, ST7796), resolution, SPI frequency
- [ ] **Touch controller:** Identify part (XPT2046, other), SPI bus (shared or dedicated)
- [ ] **Battery:** ADC pin, divider ratio, thresholds (ADC1 or ADC2? Do not use ADC2 if Wi-Fi is on!)
- [ ] **RGB LED:** Common anode or cathode? Polarity? Any GPIO crossings?
- [ ] **Power:** Charging circuit? CHRG pin wired? Fallback to inference?
- [ ] **BLE:** NimBLE or Bluedroid? Flash budget?
- [ ] **Quirks:** Any hardware bugs, reversed polarity, or non-standard wiring?
- [ ] **Create board hardware file:** `BOARD_<VARIANT>.md` with same structure as this file
- [ ] **Create hardware test app:** Exercises display, touch, battery, BLE, Wi-Fi
- [ ] **Run verification procedures:** All tests pass before game support
- [ ] **Document in AGENTS.md:** Reference new board file and test procedures

---

## 13. Key Dependencies and Libraries

| Library | Version | Purpose | Flash Impact |
|---|---|---|---|
| `bodmer/TFT_eSPI` | ^2.5.43 | Display driver (ILI9341, etc.) | ~100 KB |
| `h2zero/NimBLE-Arduino` | ^1.4.2 | BLE stack (advertiser + scanner) | ~192 KB |
| `bblanchon/ArduinoJson` | ^7.0.4 | Optional JSON config (SD content) | ~50 KB |
| `iamankushpandit/map-n-flag` | (git) | Flags and state outlines | ~763 KB (images) |
| Arduino-ESP32 core | (implicit) | SDK, lwIP, SNTP, eFuse cal | ~1.5 MB |

**NimBLE vs Bluedroid:** NimBLE is ~192 KB; Bluedroid is several times that. Flash is the binding constraint on `huge_app.csv` (3 MB partition). Do not switch to Bluedroid.

---

## 14. Serial Logging and Diagnostics

### Monitor Serial Output
```bash
pio device monitor --baud 115200
```

### Key Log Tags
- `[boot]` — Power-on, Wi-Fi credentials, NTP config
- `[time]` — NTP sync, time zone, clock updates
- `[touchcal]` — Touch calibration wizard
- `[battery]` — Battery telemetry (if debug enabled)
- `[watchdog]` — Stalls, frame times, crash breadcrumb
- `[display]` — Sleep/wake counts and timings
- `[ble]` — Radio init, advertisement data

### Crash Breadcrumb Format
```
[watchdog] CRASH: screen=Launcher uptime=1234567 ms heap=12345 bytes
```

Last three data points survive reboot in RTC memory, printed at next boot.

---

## References

- **TFT_eSPI docs:** https://github.com/Bodmer/TFT_eSPI
- **ILI9341 datasheet:** (typical: 3.6 MB SPI display controller spec)
- **XPT2046 datasheet:** (resistive touch controller protocol)
- **ESP32 ADC calibration:** `esp_adc_cal.h` (use eFuse, not nominal 3.3V)
- **NimBLE-Arduino:** https://github.com/h2zero/NimBLE-Arduino
- **BLE Beacon Spec:** `docs/BLE_BEACON_SPEC.md`

---

**Document history:**
- 2026-08-17: Initial version, E32R28T-1 baseline (firmware 4.2.0)

