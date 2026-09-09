#pragma once

#include "BoardProfile.h"

/* Which touch controller this board wires, as a macro so the preprocessor can
 * act on it -- see e32r28t1.h for why this cannot just be the profile field. */
#define GUME_TOUCH_CAPACITIVE 0

/* No codec; DAC output on GPIO26 (ESP32 DAC2), as on the 2.8- and 4-inch
 * boards of this family. */
#define GUME_HAS_AUDIO_CODEC 0
#define GUME_HAS_AUDIO_DAC   1

/* LCDWIKI E32R32P (ESP32-32E) -- the 3.2-inch board. ST7789P3 240x320 IPS
 * panel, XPT2046 resistive touch sharing the display's SPI bus.
 *
 * This is the middle sibling of the family Braino already supports: the
 * E32R28T-1 (2.8in, ILI9341, touch on its OWN bit-banged bus) and the E32R40T
 * (4in, ST7796, touch on the SHARED bus). This board follows the 4-inch
 * board's wiring, not the 2.8-inch board's, which is the single most important
 * thing to know about it -- flashing it with a 2.8-inch profile gives a
 * working picture and completely dead touch, which is exactly how it arrived
 * here.
 *
 * WHAT IS MEASURED AND WHAT IS NOT
 *
 * Measured on hardware with env:diag32p (src/diag4.cpp), 2026-09-05:
 *   - The panel renders correctly at rotation 3 (320x240 landscape): all four
 *     corner labels land in their corners, unmirrored.
 *   - COLOUR ORDER IS BGR. With the driver's default RGB order the red block
 *     drew blue, blue drew red, green stayed green and yellow drew cyan --
 *     red and blue exchanged with green untouched, which is a BGR panel and
 *     nothing else. Rebuilt with TFT_RGB_ORDER=TFT_BGR and confirmed correct
 *     by eye. Inversion was ruled out by the same picture: an inverted panel
 *     would have drawn the black background white. The board had been flashed
 *     with the ESP32-2432S028Rv3 profile, whose TFT_INVERT_COLORS=1 and RGB
 *     order were BOTH wrong for it.
 *   - The backlight is GPIO27, active high: the panel is lit with that pin
 *     driven high and nothing else.
 *   - Battery sense is GPIO34. It read 1910mV at the pin against every other
 *     ADC1 input sitting railed or floored, which is ~3.82V behind the 2:1
 *     divider. Attaching the pack moved it by 11mV -- this board cannot tell a
 *     missing pack from a present one, same as the rest of the family.
 *   - The XPT2046 answers on the shared display bus with CS=33: idle reads are
 *     a stable x=0, y~3670, z=12-18, which is coherent digitiser output rather
 *     than a floating line, and sits an order of magnitude below the 350
 *     pressure threshold.
 *   - Wi-Fi and BLE coexist: a scan found 29 networks, NimBLE advertised
 *     throughout five combined rounds, and free heap returned to within 2KB
 *     with the largest allocatable block unchanged.
 *
 * Vendor-documented in the LCDWIKI E32R32P pin table, not inferred: the
 * display bus, the touch bus and CS, the backlight, the SD slot, the RGB LED,
 * the audio path and battery sense.
 *
 * NOT yet verified on this board, and flagged again at each field below:
 *   - the RGB LED channel order. The vendor pin table for the E32R28T-1 got
 *     precisely this wrong, so a second vendor table is not evidence.
 *   - maxVolume, which is a judgement made by listening, not a datasheet
 *     number.
 *
 * The display's own SPI pins are NOT here: TFT_eSPI is configured through -D
 * flags in platformio.ini, and BoardConfig.h cross-checks the two descriptions
 * so they cannot drift.
 */
inline constexpr BoardProfile BOARD = {
    "E32R32P",
    "R32P",

    /* panel: 3 is landscape, confirmed by drawing into it. */
    PanelProfile{
        /* nativeWidth          */ 240,
        /* nativeHeight         */ 320,
        /* landscapeRotation    */ 3,
        /* portraitRotation     */ 0,
        /* backlightPin         */ 27,
        /* backlightActiveHigh  */ true,
        /* invertColours        */ false,
    },

    /* touch: the XPT2046 rides the display's own hardware SPI bus, arbitrated
     * by its own CS, rather than the separate bit-banged bus the 2.8-inch
     * board uses. The mosi/miso/sclk below therefore repeat the display's
     * pins, which TouchProfile explicitly allows. The vendor pin table states
     * this outright -- "SPI bus clock signal (shared by LCD and touch
     * screen)" -- and page TOUCH-S of the probe confirmed the controller
     * answers there.
     *
     * irqUsable is false for the same reason as on the 4-inch board: GPIO36 is
     * input-only, has no internal pull, and this board fits no external
     * pull-up, so the line cannot be trusted as a pen-down signal. Gating on
     * pressure alone is clean here -- measured idle noise is 12-18 against a
     * 350 threshold. */
    TouchProfile{
        /* kind               */ (GUME_TOUCH_CAPACITIVE
                                     ? TouchKind::CapacitiveFt6336u
                                     : TouchKind::ResistiveXpt2046),
        /* mosi               */ 13,
        /* miso               */ 12,
        /* sclk               */ 14,
        /* cs                 */ 33,
        /* irq                */ 36,
        /* irqUsable          */ false,
        /* sda                */ PIN_NONE,
        /* scl                */ PIN_NONE,
        /* reset              */ PIN_NONE,
        /* i2cAddress         */ 0,
        /* i2cHz              */ 0,
        /* pressureThreshold  */ 350,
        /* hitSlop            */ 8,
    },

    /* BOOT (IO0), the key the ROM uses to enter serial download mode; read at
     * runtime as an ordinary input with an internal pull-up. */
    ButtonProfile{
        /* bootPin   */ 0,
        /* activeLow */ true,
    },

    /* Vendor-documented: the MicroSD slot shares its bus with the SPI
     * peripheral header, which is why IO18/19/23 appear in both places in the
     * pin table. Only the card select is the slot's alone. */
    SdProfile{
        /* cs    */ 5,
        /* mosi  */ 23,
        /* miso  */ 19,
        /* sclk  */ 18,
        /* spiHz */ 16000000,
    },

    /* Vendor pin table: IO22 red, IO16 green, IO17 blue, common anode (low
     * level on). UNVERIFIED ON HARDWARE -- and this is the one field in this
     * file where a vendor table has already been wrong once. The E32R28T-1's
     * table crosses red and green relative to what that board actually does,
     * which is corrected in its own profile and must not be "fixed" back.
     *
     * The consequence here is mild and self-evident: a wrong channel shows the
     * wrong colour and nothing else, because unlike the 4-inch board none of
     * these three pins is the amplifier enable. IO4 is AUDIO_EN on this board
     * and is declared in AudioProfile below, where it belongs, so the failure
     * that silenced the 4-inch board's speaker cannot happen here. */
    RgbLedProfile{
        /* r           */ 22,
        /* g           */ 16,
        /* b           */ 17,
        /* commonAnode */ true,
    },

    /* Vendor-documented: IO26 is "Audio signal DAC output" and IO4 is "Audio
     * enable signal, low level enable, high level disable". GPIO26 is ESP32
     * DAC channel 2, so I2S_DAC_BUILT_IN drives the onboard amplifier's analog
     * input directly. There is no I2S codec here and no BCLK/LRCLK/DOUT path
     * to name.
     *
     * Two hazards this board is clear of, both of which have cost a release:
     *   - The 5.5.0 defect cannot recur here. That was the DAC subsystem
     *     claiming GPIO25 as well as 26 and taking the bit-banged touch clock
     *     with it. This board's touch clock is GPIO14, on the shared display
     *     bus, and nothing this firmware drives sits on GPIO25 at all.
     *   - The amplifier cannot be held in shutdown by the LED driver. On the
     *     4-inch board IO4 was declared as the green LED while being the
     *     active-low amp enable, so switching the LED off silenced the
     *     speaker. Here the green LED is IO16 and IO4 appears only below.
     *
     * maxVolume 75 is inherited from the rest of the family and is NOT yet set
     * by listening on this board. Confirm it by ear before trusting it. */
    AudioProfile{
        /* speakerPin          */ 26,
        /* codecI2cAddress     */ 0,
        /* i2sMclk             */ PIN_NONE,
        /* i2sBclk             */ PIN_NONE,
        /* i2sWordSelect       */ PIN_NONE,
        /* i2sDataOut          */ PIN_NONE,
        /* i2sDataIn           */ PIN_NONE,
        /* ampEnablePin        */ 4,
        /* ampEnableActiveLow  */ true,
        /* maxVolume           */ 75,
    },

    /* Measured: IO34 (ADC1_CH6, input-only) behind the 2:1 divider the vendor
     * manual states plainly. sensorMaxVolts is a fault ceiling on the ADC, NOT
     * a pack-present test -- attaching the pack moved this reading by 11mV, so
     * like the rest of the family this board cannot detect a missing one.
     *
     * The divider ratio is the vendor's, not a meter's. Check it against a
     * multimeter before trusting the badge: a wrong ratio does not fail
     * loudly, it reports a plausible percentage that is fiction. */
    BatteryProfile{
        /* adcPin        */ 34,
        /* dividerRatio  */ 2.0f,
        /* sensorMaxVolts*/ 4.50f,
    },

    /* 4 MB part, partitioned huge_app.csv: 3 MB for the app. */
    MemoryProfile{
        /* flashBytes */ 4u * 1024u * 1024u,
    },
};
