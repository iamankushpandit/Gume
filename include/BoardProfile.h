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
    int8_t   sda;                 // capacitive only
    int8_t   scl;                 // capacitive only
    int8_t   reset;               // capacitive only; held low then released
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

struct SdProfile {
    int8_t   cs;
    int8_t   mosi;
    int8_t   miso;
    int8_t   sclk;
    uint32_t spiHz;
};

/* Status LED. `commonAnode` boards sink current, so a channel lights when the
 * line is driven LOW -- getting this backwards is a lit LED that never goes
 * out, not a dark one. */
struct RgbLedProfile {
    int8_t r;
    int8_t g;
    int8_t b;
    bool   commonAnode;
};

/* How a board makes a sound, if it can.
 *
 * `speakerPin` is a bare transducer on a GPIO -- what the CYD boards wire, and
 * what this struct used to be able to describe on its own. It is stubbed on
 * every board that has one.
 *
 * A codec is a different animal: an I2C control channel plus a five-wire I2S
 * data path plus a separate amplifier enable, none of which is a "speaker
 * pin". Describing one as a pin would have meant the firmware toggling a GPIO
 * that does nothing, which is worse than admitting there is no speaker.
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
    const char*   name;           // what About and System Info show the owner
    PanelProfile  panel;
    TouchProfile  touch;
    ButtonProfile button;         // beside touch: both are how a person acts
    SdProfile     sd;
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
    constexpr bool hasBacklightControl() const { return panel.backlightPin != PIN_NONE; }
    constexpr bool hasSdSlot() const { return sd.cs != PIN_NONE; }
    constexpr bool hasRgbLed() const { return rgb.r != PIN_NONE || rgb.g != PIN_NONE || rgb.b != PIN_NONE; }
    constexpr bool hasSpeaker() const { return audio.speakerPin != PIN_NONE; }
    constexpr bool hasBatterySense() const { return battery.adcPin != PIN_NONE; }
};
