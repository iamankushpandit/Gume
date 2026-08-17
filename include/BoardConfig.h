#pragma once

#include <Arduino.h>

/* Landscape canvas, derived from the panel the build is configured for.
 *
 * TFT_WIDTH/TFT_HEIGHT are the panel's native portrait dimensions, so landscape
 * swaps them: the 2.8-inch ILI9341 (240x320) gives 320x240, and the 4-inch
 * ST7796S (320x480) gives 480x320. These stay only for the screens that have
 * not been converted yet -- everything responsive reads tft.width()/height()
 * at render time, which is what lets one binary serve both panels. */
#ifndef TFT_WIDTH
#define TFT_WIDTH 240
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 320
#endif

constexpr int16_t SCREEN_WIDTH = TFT_HEIGHT;
constexpr int16_t SCREEN_HEIGHT = TFT_WIDTH;
constexpr int16_t TOP_BAR_HEIGHT = 30;

/* The backlight GPIO is board-specific: 21 on the E32R28T-1, 27 on the 4-inch
 * ST7796S board. Driving the wrong one leaves the panel dark while every other
 * part of the firmware runs perfectly, which is exactly how that presents. */
#ifndef TFT_BL
#define TFT_BL 21
#endif
constexpr uint8_t PIN_TFT_BACKLIGHT = TFT_BL;

constexpr uint8_t PIN_SD_CS = 5;
constexpr uint8_t PIN_SD_MOSI = 23;
constexpr uint8_t PIN_SD_MISO = 19;
constexpr uint8_t PIN_SD_SCLK = 18;

/* Touch wiring is board-specific. The E32R28T-1 gives the XPT2046 its own
 * three pins; the 4-inch ST7796S board instead shares the LCD's SPI bus
 * (SCLK 14, MOSI 13, MISO 12) and distinguishes the digitiser by chip select
 * alone. Bit-banging is what makes that safe -- the panel's CS is deasserted
 * between transactions, so borrowing the lines to clock a slow XPT2046 does
 * not disturb it.
 *
 * Getting these wrong does not look like broken wiring: the reads return a
 * constant, all three calibration targets produce the same raw point, and the
 * affine solve rejects them as degenerate. That presents as "Calibration
 * failed" with no hint that the pins are the problem. */
#ifndef PIN_TOUCH_MOSI_CFG
#define PIN_TOUCH_MOSI_CFG 32
#endif
#ifndef PIN_TOUCH_MISO_CFG
#define PIN_TOUCH_MISO_CFG 39
#endif
#ifndef PIN_TOUCH_SCLK_CFG
#define PIN_TOUCH_SCLK_CFG 25
#endif

constexpr uint8_t PIN_TOUCH_MOSI = PIN_TOUCH_MOSI_CFG;
constexpr uint8_t PIN_TOUCH_MISO = PIN_TOUCH_MISO_CFG;
constexpr uint8_t PIN_TOUCH_SCLK = PIN_TOUCH_SCLK_CFG;
constexpr uint8_t PIN_TOUCH_CS = 33;
constexpr uint8_t PIN_TOUCH_IRQ = 36;

/* RGB LED, common anode (drive LOW to light a channel).
 *
 * The red and green lines are crossed relative to the usual standard pinout on this
 * unit (E32R28T-1): driving GPIO4 lit GREEN, not red. Verified on hardware -- an orange
 * (R255 G110) mix came out green, and purple (R200 B255) came out cyan/blue,
 * which is exactly what swapping R and G produces. */
/* Common anode, so drive is inverted. The red and green lines are crossed on
 * the E32R28T-1 relative to the usual pinout -- verified on hardware, do not
 * "fix" it again. The 4-inch board uses a different set again (R 22, G 16,
 * B 17), which is why its LED showed green where the firmware meant red. */
#ifndef PIN_RGB_R_CFG
#define PIN_RGB_R_CFG 16
#endif
#ifndef PIN_RGB_G_CFG
#define PIN_RGB_G_CFG 4
#endif
#ifndef PIN_RGB_B_CFG
#define PIN_RGB_B_CFG 17
#endif

constexpr uint8_t PIN_RGB_R = PIN_RGB_R_CFG;
constexpr uint8_t PIN_RGB_G = PIN_RGB_G_CFG;
constexpr uint8_t PIN_RGB_B = PIN_RGB_B_CFG;
constexpr uint8_t PIN_SPEAKER = 26;

// TODO(HARDWARE-VALIDATION): Verify E32R28T-1 battery ADC calibration, divider ratio, battery-present thresholds, no-battery behavior, USB-power behavior, and charging-state detection using physical hardware.
constexpr uint8_t PIN_BAT_ADC = 34;

constexpr uint16_t TOUCH_PRESSURE_THRESHOLD = 350;
constexpr uint8_t TOUCH_HIT_SLOP = 8;

