#pragma once

#include <Arduino.h>

constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;
constexpr int16_t TOP_BAR_HEIGHT = 30;

// Single source of truth with TFT_eSPI's own TFT_BL build flag, so the raw
// GPIO drive in Board::begin() and the panel driver never disagree about
// which pin the backlight is on.
constexpr uint8_t PIN_TFT_BACKLIGHT = TFT_BL;

constexpr uint8_t PIN_SD_CS = 5;
constexpr uint8_t PIN_SD_MOSI = 23;
constexpr uint8_t PIN_SD_MISO = 19;
constexpr uint8_t PIN_SD_SCLK = 18;

// Derived from build flags (see platformio.ini) rather than hardcoded, so a
// board variant with a differently-wired touch bus doesn't need a second copy
// of this file.
constexpr uint8_t PIN_TOUCH_MOSI = CYD_TOUCH_MOSI;
constexpr uint8_t PIN_TOUCH_MISO = CYD_TOUCH_MISO;
constexpr uint8_t PIN_TOUCH_SCLK = CYD_TOUCH_SCLK;
constexpr uint8_t PIN_TOUCH_CS = 33;
constexpr uint8_t PIN_TOUCH_IRQ = 36;

/* RGB LED, common anode (drive LOW to light a channel).
 *
 * The red and green lines are crossed relative to the usual standard pinout on this
 * unit (E32R28T-1): driving GPIO4 lit GREEN, not red. Verified on hardware -- an orange
 * (R255 G110) mix came out green, and purple (R200 B255) came out cyan/blue,
 * which is exactly what swapping R and G produces. */
constexpr uint8_t PIN_RGB_R = 16;
constexpr uint8_t PIN_RGB_G = 4;
constexpr uint8_t PIN_RGB_B = 17;
constexpr uint8_t PIN_SPEAKER = 26;

// TODO(HARDWARE-VALIDATION): Verify E32R28T-1 battery ADC calibration, divider ratio, battery-present thresholds, no-battery behavior, USB-power behavior, and charging-state detection using physical hardware.
constexpr uint8_t PIN_BAT_ADC = 34;

constexpr uint16_t TOUCH_PRESSURE_THRESHOLD = 350;
constexpr uint8_t TOUCH_HIT_SLOP = 8;

