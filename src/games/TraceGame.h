#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class TraceGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

    struct Stroke {
        const int16_t* pts;
        uint8_t count;
    };

    struct Glyph {
        char label;
        const Stroke* strokes;
        uint8_t strokeCount;
    };

private:
    static constexpr uint8_t MAX_SEGMENTS = 64;

    void loadGlyph();
    void buildSegments();
    int16_t scaleX(int16_t nx) const;
    int16_t scaleY(int16_t ny) const;
    void drawGuide(TFT_eSPI& tft);
    void drawProgress(TFT_eSPI& tft);
    uint8_t tracedCount() const;

    uint8_t glyphIndex_ = 0;
    bool traced_[MAX_SEGMENTS] = {};
    uint8_t totalSegments_ = 0;
    bool complete_ = false;
    uint32_t completeAt_ = 0;

    struct Seg {
        int16_t x1, y1, x2, y2;
    };
    Seg segs_[MAX_SEGMENTS] = {};
};
