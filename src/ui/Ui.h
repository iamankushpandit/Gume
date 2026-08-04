#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "BoardConfig.h"

struct Rect {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;

    bool contains(int16_t px, int16_t py, int16_t pad = 0) const {
        return px >= x - pad && px < x + w + pad && py >= y - pad && py < y + h + pad;
    }
};

enum class Align {
    Left,
    Center
};

namespace Ui {
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
uint16_t bg();
uint16_t surface();
uint16_t panel();
uint16_t text();
uint16_t muted();
uint16_t outline();
uint16_t success();
uint16_t error();
uint16_t warning();
void clear(TFT_eSPI& tft);
void drawTopBar(TFT_eSPI& tft, const String& title);
void drawHomeIcon(TFT_eSPI& tft, const Rect& r);
void drawButton(TFT_eSPI& tft, const Rect& r, const String& label, uint16_t fill, uint16_t outline, uint16_t text, bool pressed = false, uint8_t font = 2);
void drawLabel(TFT_eSPI& tft, const Rect& r, const String& text, uint16_t color, uint8_t font = 2, Align align = Align::Left);
int16_t drawWrappedText(TFT_eSPI& tft, const String& text, const Rect& r, uint16_t color, uint8_t font = 2, Align align = Align::Left);
void drawTriangleShape(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color, bool filled);
void drawStarShape(TFT_eSPI& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color, bool filled);
}
