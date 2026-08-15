#pragma once

#include <Arduino.h>

struct TouchPoint {
    bool down = false;
    bool justPressed = false;
    bool justReleased = false;
    int16_t x = 0;
    int16_t y = 0;
    uint16_t pressure = 0;
};
