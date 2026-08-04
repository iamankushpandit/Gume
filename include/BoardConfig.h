#pragma once

#include <Arduino.h>

constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;
constexpr int16_t TOP_BAR_HEIGHT = 30;

constexpr uint8_t PIN_TFT_BACKLIGHT = 21;

constexpr uint8_t PIN_SD_CS = 5;
constexpr uint8_t PIN_SD_MOSI = 23;
constexpr uint8_t PIN_SD_MISO = 19;
constexpr uint8_t PIN_SD_SCLK = 18;

constexpr uint8_t PIN_TOUCH_MOSI = 32;
constexpr uint8_t PIN_TOUCH_MISO = 39;
constexpr uint8_t PIN_TOUCH_SCLK = 25;
constexpr uint8_t PIN_TOUCH_CS = 33;
constexpr uint8_t PIN_TOUCH_IRQ = 36;

constexpr uint8_t PIN_RGB_R = 4;
constexpr uint8_t PIN_RGB_G = 16;
constexpr uint8_t PIN_RGB_B = 17;
constexpr uint8_t PIN_SPEAKER = 26;

constexpr uint16_t TOUCH_PRESSURE_THRESHOLD = 350;
constexpr uint8_t TOUCH_HIT_SLOP = 8;

