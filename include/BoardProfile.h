// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>

/* The contract every supported board has to fill in.
 *
 * Everything the firmware needs to know about a *specific* piece of hardware
 * lives in one `BoardProfile` constant, declared by one header in
 * `include/boards/`. Nothing outside that header may name a GPIO number, a
 * panel size or a divider ratio; the rest of the firmware reads `BOARD`.
 *
 * That is the whole point. Before this existed, supporting a second board
 * meant copying thirty `-D` flags into a second platformio.ini section and
 * hoping every `PIN_*` constant in `include/BoardConfig.h` still applied --
 * and the constants were global, so it could only ever describe one board at
 * a time. `docs/PORTING.md` is the checklist; this file is the shape.
 *
 * Not every board can be supported, and this file is where that is decided.
 * Braino! needs a colour panel of at least the size the games are drawn at,
 * touch as its only input, a controllable backlight, and a 4 MB part to hold
 * the 3 MB app. A board missing any of those cannot run this firmware, and
 * `include/BoardConfig.h` refuses to compile for it rather than producing
 * something that boots into a screen nobody can read or press. Saying no at
 * the compiler is the kindest place to say it.
 *
 * Everything else is genuinely optional -- SD slot, RGB LED, speaker, battery
 * sense -- and a board without one sets `PIN_NONE` and gets a firmware that
 * quietly does without. That distinction is the useful one: "degrades" and
 * "cannot work" are different answers and must not be blurred into each other.
 *
 * Rules for adding a field:
 *
 * - It must be a property of the *hardware*, not a product decision. Screen
 *   rotation belongs here (the USB socket is on a physical edge); the idle
 *   timeout does not.
 * - Decide, and write down, whether a board can be supported without it. If it
 *   can, `PIN_NONE` plus a `has...()` guard at every call site. If it cannot,
 *   a static_assert in `BoardConfig.h` with a message saying what is missing.
 * - Fill it in for every existing board in the same commit.
 *   `tools/check_boards.py` reads the field names out of this file, so an
 *   unfilled field fails the check rather than defaulting to zero.
 */

/* A line the board does not wire. Signed, because a GPIO number is not. */
constexpr int8_t PIN_NONE = -1;

/* The panel, as the hardware presents it -- native (portrait) dimensions, not
 * the landscape canvas the games draw on. `screenWidth()` derives that. */
struct PanelProfile {
    int16_t nativeWidth;          // TFT_WIDTH: the panel's own short edge
    int16_t nativeHeight;         // TFT_HEIGHT: its own long edge
    uint8_t landscapeRotation;    // rotation putting the USB edge at the bottom
    uint8_t portraitRotation;     // the quarter-turn from that
    int8_t  backlightPin;         // must equal TFT_BL; see BoardConfig.h
    bool    backlightActiveHigh;
    /* True when the panel's colours come out inverted under the driver this
     * board is built with. A hardware fact about the glass, not a preference:
     * an ILI9341 build on an ST7789-style panel draws everything perfectly
     * with every channel flipped, which reads as a theme bug rather than as a
     * wrong driver. Board::begin() acts on it -- TFT_eSPI's TFT_INVERT_COLORS
     * macro cannot, because only its ST7789/ST7735 init paths consult it. */
    bool    invertColours;
};

/* Which kind of touch controller the board wires. This is a property of the
 * hardware, and it decides more than a pin map: a resistive panel reports an
 * ADC reading that means nothing until three points have been fitted to it,
 * while a capacitive controller reports pixels and has nothing to calibrate.
 * Running the calibration wizard on one is not a degraded experience, it is a
 * screen the owner cannot get past. */
enum class TouchKind : uint8_t {
    ResistiveXpt2046,      // bit-banged SPI, needs the 3-point affine fit
    CapacitiveFt6336u,     // I2C, reports panel-native pixels directly
};

/* Touch. One struct covers both kinds because a board has exactly one, and a
 * discriminated flat record keeps `check_boards.py`'s field-by-label check
 * working -- a variant type would let a board fill in the wrong arm and still
 * compile.
 *
 * Lines the board's kind does not use are `PIN_NONE`, exactly as an absent
 * peripheral is elsewhere in this file. `BoardConfig.h` static_asserts the
 * lines each kind actually needs, so an unfilled one fails at the compiler
 * rather than at the first press.
 *
 * Resistive: `mosi`/`miso`/`sclk` may repeat the display's SPI pins on boards
 * that share the bus; the firmware bit-bangs either way.
 *
 * Capacitive: the controller reports coordinates in the panel's NATIVE
 * (portrait) frame and knows nothing about `setRotation()`. Rotating them to
 * match the display is the touch layer's job, and it is not optional -- the
 * FNK0104B bring-up probe tracked a finger correctly at rotation 0 and not at
 * any other, which is exactly this. */
struct TouchProfile {
    TouchKind kind;
    int8_t   mosi;                // resistive only
    int8_t   miso;                // resistive only
    int8_t   sclk;                // resistive only
    int8_t   cs;                  // resistive only
    int8_t   irq;                 // both: pen-down / INT
    /* Whether that IRQ line can actually be believed.
     *
     * It is wired on every board here, and on one of them it is useless: the
     * 4-inch E32R40T brings it out on GPIO36, an input-only pin with no
     * internal pull, and populates no external pull-up. It therefore floats
     * LOW and reports a permanent, false pen-down. The resistive gate accepts
     * a press when the IRQ is low OR pressure is high, so an unusable IRQ does
     * not degrade touch -- it defeats it entirely, reporting a touch on every
     * poll forever.
     *
     * This is a property of the board (is the resistor fitted?), not of the
     * controller, which is why it lives here rather than being inferred. A
     * board setting this false is telling the firmware to gate on pressure
     * alone, which is a clean signal on these panels: idle noise reads 10-20
     * and a real press 2000+. */
    bool     irqUsable;
    int8_t   sda;                 // capacitive only
    int8_t   scl;                 // capacitive only
    int8_t   reset;               // capacitive only; held low then released
    /* Where the reset line is when it is NOT a GPIO.
     *
     * The Attaky Core routes its touch controller's reset through an AW9523
     * I2C GPIO expander rather than to the ESP32, so `reset` above has no pin
     * to name and stays PIN_NONE. These two say which expander and which of
     * its sixteen ports -- 0..7 for P0_0..P0_7, 8..15 for P1_0..P1_7 -- and
     * Board::begin() drives it exactly as it drives a GPIO reset: held low
     * through the panel's init, then released. Active low, like every reset
     * here. 0 for the address means there is no such expander. */
    uint8_t  resetExpanderAddress; // capacitive only; an AW9523, or 0
    uint8_t  resetExpanderPort;    // capacitive only; 0..15
    uint8_t  i2cAddress;          // capacitive only
    uint32_t i2cHz;               // capacitive only
    uint16_t pressureThreshold;   // resistive: below this, with IRQ high, noise
    int16_t  hitSlop;             // both: pixels of forgiveness on a hit test

    constexpr bool isCapacitive() const {
        return kind == TouchKind::CapacitiveFt6336u;
    }
    /* A capacitive panel has nothing to fit, so the wizard must not run and
     * `hasTouchCalibration()` must answer true without one having been done. */
    constexpr bool needsCalibration() const { return !isCapacitive(); }
};

/* The BOOT key, where a board brings one out as something a person can press.
 *
 * Every board here already has one, because the ROM needs it to enter serial
 * download mode. That is also the whole caveat: it is a strapping pin, and the
 * ROM samples it at reset. The firmware therefore only ever reads it at
 * runtime, long after boot has committed -- holding it while the device starts
 * is a request to the ROM, not to us, and nothing here can change that.
 *
 * It is optional in the same sense as the RGB LED: `PIN_NONE` for a board that
 * does not wire one, every caller guarded by `BOARD.hasBootButton()`, and a
 * board without one gets a firmware that is driven entirely by touch, exactly
 * as it was before this existed. Touch remains the input this console is
 * designed around; the button is a shortcut, never the only way to do
 * anything. */
struct ButtonProfile {
    int8_t bootPin;
    /* True when the line idles high through a pull-up and the switch shorts it
     * to ground -- which is what BOOT is on every board here. Stated rather
     * than assumed, because a board that wired it the other way would read as
     * a button held down forever. */
    bool   activeLow;
};

/* Navigation keys -- a D-pad, Enter and Back -- where a board has them.
 *
 * Only behind an AW9523 I2C expander for now, because that is the only way
 * any supported board wires them (the Attaky Core's face keys). The fields
 * are that expander's address and, for each key, one of its sixteen ports:
 * 0..7 for P0_0..P0_7, 8..15 for P1_0..P1_7, PIN_NONE for a key the board
 * does not have. The expander shares the touch controller's I2C bus, which
 * is why BoardConfig.h requires a capacitive board.
 *
 * The directions are PHYSICAL: Up is the key that points up when the board
 * is held at its landscape rotation. Board::pollNavKey() turns them into
 * screen directions for whatever rotation is current.
 *
 * `keyPower` is a power key's SHORT press, where the board reports one to
 * the firmware at all -- the runtime treats it as the Lock button. A long
 * press that switches the power in hardware is not the firmware's to see.
 *
 * Optional, like the BOOT key: keyExpander 0 is a board without them, every
 * caller is guarded by hasNavKeys(), and nothing may become reachable only
 * through a key. */
struct KeysProfile {
    uint8_t keyExpander;          // an AW9523's I2C address, or 0
    int8_t  keyUp;
    int8_t  keyDown;
    int8_t  keyLeft;
    int8_t  keyRight;
    int8_t  keyEnter;
    int8_t  keyBack;
    int8_t  keyPower;             // a power key's short press: Lock
    bool    keysActiveLow;        // pulled up, shorted to ground when pressed
};

/* A typing keyboard, where a board can carry one.
 *
 * Only as two AW9523 expanders scanning a 5x5 matrix each, because that is
 * the only keyboard any supported board takes: Attaky's Control: Keyboard
 * (Bottom) module, which clips onto the Attaky Core. Each expander reads its
 * five columns on P0_0..P0_4 and drives its five rows on P1_0..P1_4, low to
 * select; P1_5..P1_7 are that half's LED, which the firmware leaves off.
 *
 * The layouts say what is at each of the 25 positions, row by row, columns
 * left to right -- a fact about the keyboard module, so it lives here with
 * the addresses rather than in the driver. Letters, digits and the
 * punctuation printed on the keys stand for themselves; four characters are
 * codes, because the keys they stand for do not type anything:
 *
 *   ^  Shift      <  Delete      >  Enter      ~  no key, or one not used
 *
 * Optional twice over: a board without one says keyboardLeft 0, and a board
 * that can carry one may not have it attached -- the module is detachable,
 * so Board::begin() probes both addresses and the keyboard is off unless
 * both answer. Nothing may become reachable only by typing; every text field
 * keeps its on-screen keypad. */
struct KeyboardProfile {
    uint8_t     keyboardLeft;     // the left half's AW9523, or 0
    uint8_t     keyboardRight;    // the right half's AW9523
    const char* leftLayout;       // 25 characters, ROW0 COL0 first
    const char* rightLayout;
};

struct SdProfile {
    int8_t   cs;
    int8_t   mosi;
    int8_t   miso;
    int8_t   sclk;
    uint32_t spiHz;
};

/* Plug-in modules that would reuse the SD slot's lines, and how to notice
 * one before the slot is brought up.
 *
 * Only the Attaky Core needs it: its Expand modules share IO5-8, so the SD
 * card's SPI lines belong to Media I/O on one module and to a LoRa radio or
 * an FM tuner's GPIO on the others. Two of them can be found without driving
 * anything they own: an I2C device at `fmRadioAddress` on its own bus
 * (`fmSda`/`fmScl`), and a GPS that sends NMEA sentences at `gpsBaud` into
 * `gpsRx` without being asked, heard with that pin an input. Finding either
 * means the SD slot is left alone this boot.
 *
 * Optional: 0 and PIN_NONE throughout is a board with nothing to look for,
 * which is every other board. */
struct ExpandProbeProfile {
    uint8_t  fmRadioAddress;      // an I2C address on its own bus, or 0
    int8_t   fmSda;
    int8_t   fmScl;
    int8_t   gpsRx;               // a GPS's TX, as this board's RX; or PIN_NONE
    uint32_t gpsBaud;
};

/* Status LED. `commonAnode` boards sink current, so a channel lights when the
 * line is driven LOW -- getting this backwards is a lit LED that never goes
 * out, not a dark one.
 *
 * `expander` 0 means r/g/b are GPIOs, dimmed by LEDC PWM. Otherwise it is
 * the I2C address of the AW9523 the LED hangs off -- the Attaky Core's -- and
 * r/g/b are its ports, 0..15 as in KeysProfile. Those are driven in the
 * expander's GPIO mode, so each channel is simply on or off: a colour asked
 * for is shown as the channels at least half as bright as its brightest. */
struct RgbLedProfile {
    int8_t  r;
    int8_t  g;
    int8_t  b;
    bool    commonAnode;
    uint8_t expander;             // an AW9523's I2C address, or 0 for GPIOs
};

/* How a board makes a sound, if it can.
 *
 * `speakerPin` is a bare transducer on a GPIO -- what the CYD boards wire.
 * On those boards GPIO26 is ESP32 DAC channel 2, so the I2S peripheral can
 * drive it directly (I2S_DAC_BUILT_IN) without a codec. GUME_HAS_AUDIO_DAC
 * selects that backend; GUME_HAS_AUDIO_CODEC selects the ES8311 backend;
 * GUME_HAS_AUDIO_I2S selects a bare I2S amplifier with no codec (the Attaky
 * Core's NS4168), which needs i2sBclk, i2sWordSelect and i2sDataOut and
 * nothing else. All three pull in driver/i2s.h; the codec path additionally
 * needs Wire. None is present on a board with no audio path at all.
 *
 * `maxVolume` is the ceiling for the volume slider. It lives here because it
 * is set by listening on real hardware and differs by amplifier: the codec
 * board's ES8311 + class-D amp peaks at 85, the CYD DAC boards at 75, and the
 * unheard Attaky Core starts at a cautious 50 (both of those last two apply
 * it as linear gain -- there is no register to write). A board
 * without audio sets it to 0; that is guarded in setVolume() so nothing
 * crashes if the default NVS value exceeds 0 on a boardless build.
 *
 * Unused lines are PIN_NONE, as everywhere else here. `hasCodec()` and
 * `hasSpeaker()` are separate questions and a board may answer no to both. */
struct AudioProfile {
    int8_t   speakerPin;
    int8_t   codecI2cAddress;   // 0 when there is no codec
    int8_t   i2sMclk;
    int8_t   i2sBclk;
    int8_t   i2sWordSelect;
    int8_t   i2sDataOut;        // ESP32 -> codec DAC (speaker)
    int8_t   i2sDataIn;         // codec ADC -> ESP32 (microphone)
    int8_t   ampEnablePin;
    bool     ampEnableActiveLow;
    uint8_t  maxVolume;         // ceiling for the volume slider (0–100)

    constexpr bool hasCodec() const { return codecI2cAddress != 0; }
};

/* What the part has, as distinct from what the PCB wires. `flashBytes` gates
 * support outright: the app partition is 3 MB (huge_app.csv), so a 4 MB part
 * is the floor and nothing smaller can hold this firmware. It is declared
 * rather than probed because the answer has to be known before the build, not
 * after it has failed to fit. */
struct MemoryProfile {
    uint32_t flashBytes;
};

/* Battery sense. `dividerRatio` is the multiplier from the voltage at the ADC
 * to the voltage at the cell; `sensorMaxVolts` is a plausibility ceiling on
 * the reading, above which the divider or the ADC is faulty. Neither is a
 * "is a pack fitted?" test -- see the comment block in hal/BoardPower.cpp. */
struct BatteryProfile {
    int8_t adcPin;
    float  dividerRatio;
    float  sensorMaxVolts;
};

struct BoardProfile {
    /* NO WHITESPACE IN THIS NAME. It is printed in the boot banner as
     * `[boot] board=<name>` and tools/ESP32_boardUtil.py reads that back as a
     * single token, so a name with a space in it is silently truncated and the
     * tool then reports a mismatch against its own registry. One board here
     * shipped as "ESP32-2432S028Rv3 (ST7789)" and did exactly that. Hyphens,
     * not spaces. */
    const char*   name;           // what About and System Info show the owner

    /* A SHORT TAG FOR THIS MODEL, up to 7 characters, no whitespace.
     *
     * It prefixes the firmware-generated device id -- `R28T-9F3A2C71` -- so
     * that an id tells you which build a board wants without telling anyone
     * which board it is. That distinction is the point: the tag names a MODEL,
     * of which many exist in the world, while the eight hex characters name
     * this unit and mean nothing off the device. Neither half is a MAC, and
     * the id cannot be turned back into one.
     *
     * Keep it terse and stable. Changing it changes every id on that model. */
    const char*   idTag;
    PanelProfile  panel;
    TouchProfile  touch;
    ButtonProfile button;         // beside touch: both are how a person acts
    KeysProfile   keys;           // and so are these, on the boards that have them
    KeyboardProfile keyboard;     // and this, when one is attached
    SdProfile     sd;
    ExpandProbeProfile expandProbe; // what else might be on the SD lines
    RgbLedProfile rgb;
    AudioProfile  audio;
    BatteryProfile battery;
    MemoryProfile memory;

    /* The landscape canvas the games are authored against. Derived, because a
     * board that states both its panel size and its screen size can state them
     * inconsistently, and one of the two would then be a lie. */
    constexpr int16_t screenWidth() const {
        return (panel.landscapeRotation & 1) ? panel.nativeHeight : panel.nativeWidth;
    }
    constexpr int16_t screenHeight() const {
        return (panel.landscapeRotation & 1) ? panel.nativeWidth : panel.nativeHeight;
    }

    constexpr bool hasBootButton() const { return button.bootPin != PIN_NONE; }
    constexpr bool hasNavKeys() const { return keys.keyExpander != 0; }
    constexpr bool hasKeyboard() const { return keyboard.keyboardLeft != 0; }
    constexpr bool hasBacklightControl() const { return panel.backlightPin != PIN_NONE; }
    constexpr bool hasSdSlot() const { return sd.cs != PIN_NONE; }
    constexpr bool hasExpandProbe() const {
        return expandProbe.fmRadioAddress != 0 || expandProbe.gpsRx != PIN_NONE;
    }
    constexpr bool hasRgbLed() const { return rgb.r != PIN_NONE || rgb.g != PIN_NONE || rgb.b != PIN_NONE; }
    constexpr bool hasSpeaker() const { return audio.speakerPin != PIN_NONE; }
    constexpr bool hasBatterySense() const { return battery.adcPin != PIN_NONE; }
};
