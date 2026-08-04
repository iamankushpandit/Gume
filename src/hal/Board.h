#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "BoardConfig.h"

struct TouchPoint {
    bool down = false;
    bool justPressed = false;
    bool justReleased = false;
    int16_t x = 0;
    int16_t y = 0;
    uint16_t pressure = 0;
};

class Board {
public:
    struct TouchCalibration {
        uint32_t magic = 0;
        float ax = 0.0f;
        float bx = 0.0f;
        float cx = 0.0f;
        float ay = 0.0f;
        float by = 0.0f;
        float cy = 0.0f;
    };

    Board();

    void begin();
    TFT_eSPI& display();

    bool sdReady() const;
    bool mountSd();

    bool hasTouchCalibration() const;
    void runTouchCalibration();
    TouchPoint pollTouch();
    TouchPoint touch() const;

    void beepOk();
    void beepError();
    uint32_t getScore(const char* key, uint32_t fallback = 0);
    void setScore(const char* key, uint32_t value);
    bool saveBestScore(const char* key, uint32_t value, bool lowerIsBetter);

    void setRgb(bool red, bool green, bool blue);
    bool drawBmp(const char* path, int16_t x, int16_t y, int16_t maxW, int16_t maxH);

private:
    struct RawTouch {
        bool down = false;
        int16_t x = 0;
        int16_t y = 0;
        uint16_t pressure = 0;
    };

    uint16_t readTouchAdc(uint8_t command);
    RawTouch readRawTouch();
    bool waitForStableRaw(int16_t& rawX, int16_t& rawY);
    bool loadTouchCalibration();
    void saveTouchCalibration();
    bool mapTouch(const RawTouch& raw, int16_t& x, int16_t& y) const;
    void beep(uint16_t frequency, uint16_t ms);

    TFT_eSPI tft_;
    SPIClass sdSpi_;
    Preferences prefs_;
    TouchCalibration cal_;
    TouchPoint lastTouch_;
    bool sdMounted_ = false;
};
