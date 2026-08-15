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
    static constexpr uint8_t MAX_POINTS  = 96;
    static constexpr uint8_t MAX_STROKES = 4;
    static constexpr uint8_t UPPER_FIRST = 0;
    static constexpr uint8_t LOWER_FIRST = 26;
    static constexpr uint8_t DIGIT_FIRST = 52;
    static constexpr uint8_t GLYPH_COUNT_TOTAL = 62;

    enum class GlyphSet { Upper, Lower, Digit };

    struct Pt {
        int16_t x, y;
    };

    void loadGlyph();
    void resampleWaypoints();
    int16_t scaleX(int16_t nx) const;
    int16_t scaleY(int16_t ny) const;
    void drawGuide(TFT_eSPI& tft);
    void drawProgress(TFT_eSPI& tft);
    void drawModeTabs(TFT_eSPI& tft);
    void updatePulsePhase();
    uint8_t getSetFirstIndex() const;
    uint8_t getSetLastIndex() const;

    Pt pts_[MAX_POINTS] = {};
    uint8_t strokeStart_[MAX_STROKES] = {};
    uint8_t strokeLen_[MAX_STROKES] = {};
    uint8_t strokeCount_ = 0;
    uint8_t activeStroke_ = 0;
    uint8_t nextPoint_ = 0;

    uint8_t glyphIndex_ = 0;
    GlyphSet glyphSet_ = GlyphSet::Upper;
    bool complete_ = false;
    uint32_t completeAt_ = 0;

    uint32_t lastPulseChange_ = 0;
    bool pulseState_ = false;
};
