#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

/* The finger-tracing engine, shared by every game that asks a child to draw a
 * shape by following dots.
 *
 * It was TraceGame, entirely: the waypoint resampling, the hit testing, the
 * pulsing next-dot, the progress bar, the side columns of controls and the
 * "Great job" badge. None of that is specific to printed letters -- it is
 * specific to *tracing* -- and the second tracing game would have had to copy
 * four hundred lines to get it. So it moved here first, unchanged in
 * behaviour, and the games became shells that supply a glyph table and the
 * names of their alphabets. See src/games/CLAUDE.md's modularity rule.
 *
 * A game owns one of these as a member and forwards begin/update/render to it.
 * The tracer never touches the Game's dirty flags -- it raises its own and the
 * shell drains them with takeDirty()/takeFullDirty(). Reaching into a
 * protected base from a helper would have made the helper part of the class it
 * is trying to be independent of.
 */
class LetterTracer {
public:
    /* One stroke of a glyph: a polyline in the design coordinate box, as
     * x,y pairs. Curves are approximated by enough points that resampling
     * cannot see the corners -- which is why the cursive tables are generated
     * rather than typed. */
    struct Stroke {
        const int16_t* pts;
        uint8_t count;
    };

    struct Glyph {
        char label;
        const Stroke* strokes;
        uint8_t strokeCount;
    };

    /* One selectable alphabet, and the tab that switches to it. `count` rather
     * than a last index: an off-by-one in a boundary is silent and picks the
     * wrong letter, and a count cannot be off by one without being obviously
     * wrong. */
    struct Set {
        const char* label;
        uint8_t first;
        uint8_t count;
    };

    /** Longest resampled stroke run. A queen's-worth of dots is plenty. */
    static constexpr uint8_t MAX_POINTS = 96;
    static constexpr uint8_t MAX_STROKES = 4;
    /** Three tabs is what the left column holds; see the layout note in .cpp. */
    static constexpr uint8_t MAX_SETS = 3;

    /* Point the tracer at a table. Called once, from the shell's constructor
     * or begin(); the pointers must outlive the tracer, which they do because
     * every glyph table in this firmware is `static const` in flash. */
    void configure(const Glyph* glyphs, const Set* sets, uint8_t setCount);

    /** Back to the first letter of the first alphabet. */
    void begin();
    void update(AppContext& host, const TouchPoint& touch);
    /* `fullRender` comes from the shell's needsFullRender(), because whether
     * the chrome needs repainting is the Game's business and not the
     * tracer's. */
    void render(AppContext& host, const char* title, bool fullRender);

    /** True once, if something changed that needs a repaint. */
    bool takeDirty();
    /** True once, if the chrome changed too. */
    bool takeFullDirty();

private:
    struct Pt {
        int16_t x, y;
    };

    void loadGlyph();
    void resampleWaypoints();
    int16_t scaleX(int16_t nx) const;
    int16_t scaleY(int16_t ny) const;
    void drawGuide(Ui::Renderer& tft);
    void drawProgress(Ui::Renderer& tft);
    void drawCompleteStatus(Ui::Renderer& tft);
    void drawModeTabs(Ui::Renderer& tft);
    void updatePulsePhase();
    void previousGlyph();
    void nextGlyph();
    uint8_t setFirstIndex() const;
    uint8_t setLastIndex() const;
    void markDirty() { dirty_ = true; }
    void markFullDirty() { dirty_ = true; fullDirty_ = true; }

    const Glyph* glyphs_ = nullptr;
    const Set* sets_ = nullptr;
    uint8_t setCount_ = 0;
    uint8_t setIndex_ = 0;

    Pt pts_[MAX_POINTS] = {};
    uint8_t strokeStart_[MAX_STROKES] = {};
    uint8_t strokeLen_[MAX_STROKES] = {};
    uint8_t strokeCount_ = 0;
    uint8_t activeStroke_ = 0;
    uint8_t nextPoint_ = 0;

    uint8_t glyphIndex_ = 0;
    bool complete_ = false;
    uint32_t completeAt_ = 0;

    uint32_t lastPulseChange_ = 0;
    bool pulseState_ = false;

    bool dirty_ = true;
    bool fullDirty_ = true;
};
