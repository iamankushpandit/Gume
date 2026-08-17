#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& traceAppMetadata();

class TraceGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

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

    /* Chrome bands, all hung off the live panel. On a 320x240 screen these
     * reproduce the authored rects exactly. */
    Rect tabRow(const Ui::Frame& f) const;
    Rect modeTabRect(const Ui::Frame& f, uint8_t index) const;
    Rect pagerRow(const Ui::Frame& f) const;
    Rect prevRect(const Ui::Frame& f) const;
    Rect nextRect(const Ui::Frame& f) const;
    Rect statusRect(const Ui::Frame& f) const;
    Rect progressRect(const Ui::Frame& f) const;

    /* The glyph box. Stroke data is in an abstract 0..COORD_MAX design space,
     * so the box can be any size; it keeps the authored 160:132 aspect so the
     * letters are not restretched on a differently shaped panel. */
    Rect drawBox(const Ui::Frame& f) const;

    void loadGlyph(const Rect& box);
    void resampleWaypoints(const Rect& box);
    int16_t scaleX(const Rect& box, int16_t nx) const;
    int16_t scaleY(const Rect& box, int16_t ny) const;
    void drawGuide(Ui::Renderer& tft);
    void drawProgress(Ui::Renderer& tft, const Ui::Frame& f);
    void drawCompleteStatus(Ui::Renderer& tft, const Ui::Frame& f);
    void drawModeTabs(Ui::Renderer& tft, const Ui::Frame& f);
    void updatePulsePhase();
    void previousGlyph(const Rect& box);
    void nextGlyph(const Rect& box);
    uint8_t getSetFirstIndex() const;
    uint8_t getSetLastIndex() const;

    /* The box pts_ was built against. Waypoints are baked into panel pixels,
     * so they have to be rebuilt when the panel changes shape -- the count is
     * unaffected (the polyline is walked in design units), so a rotation
     * remaps the dots without losing how far the child had got. */
    Rect box_ = {};

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
