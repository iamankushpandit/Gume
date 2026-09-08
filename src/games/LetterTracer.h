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
     * wrong.
     *
     * `spacing` is how far apart the dots a child chases are, in pixels. It
     * belongs to the set rather than to the engine because a single letter
     * fills the canvas and wants generous spacing, while a three-letter word
     * is a third of the height and would get two dots per letter at the same
     * number. Zero means the default. */
    struct Set {
        const char* label;
        uint8_t first;
        uint8_t count;
        uint8_t spacing;
        /* What to print under the canvas for each entry in this set, when a
         * single character is not enough to say what is being traced. Null for
         * an alphabet -- Glyph::label already carries the letter. A word set
         * points this at its own array of words. */
        const char* const* names;
        /* Open this set at a random entry rather than its first.
         *
         * Right for words and wrong for an alphabet: A B C is the order a
         * child is learning and shuffling it would be actively unhelpful,
         * while always being handed the same word first makes fifty words feel
         * like one. Prev and Next still walk in order from wherever it lands,
         * because a child who wants the word they had a moment ago has to be
         * able to get back to it. */
        bool randomStart;
    };

    /* Longest resampled run, across all of a glyph's strokes.
     *
     * Raised from 96 for word tracing: three letters at a tighter spacing than
     * a single letter needs is comfortably more dots than any one letter, and
     * running out silently truncates the last stroke -- a word whose final
     * letter cannot be finished, with nothing on screen to say why. 128 points
     * is 512 bytes of static RAM, which on this device is nothing; see
     * CLAUDE.md's memory rule on trading RAM for certainty. */
    static constexpr uint8_t MAX_POINTS = 128;
    /* Six, for a word: three letters plus the marks on an i, j or t. A single
     * letter never needs more than three. */
    static constexpr uint8_t MAX_STROKES = 6;
    static constexpr uint8_t DEFAULT_SPACING = 20;
    /** Three tabs is what the left column holds; see the layout note in .cpp. */
    static constexpr uint8_t MAX_SETS = 3;

    /* Point the tracer at a table. Called once, from the shell's constructor
     * or begin(); the pointers must outlive the tracer, which they do because
     * every glyph table in this firmware is `static const` in flash.
     *
     * `coordW` and `coordH` are the box the table's coordinates live in. They
     * exist because THE MAPPING MUST BE UNIFORM: a table authored in a square
     * box and drawn into a canvas that is not square has to be letterboxed,
     * not stretched. See scaleX(). Trace's letters are authored 200x200;
     * Cursive's are authored to the canvas's own shape so that a joined word
     * gets the full width. */
    void configure(const Glyph* glyphs, const Set* sets, uint8_t setCount,
                   int16_t coordW = 200, int16_t coordH = 200);

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
    /* True once per glyph finished. The shell counts these; the tracer does
     * not know what a score is and should not -- it also does not know
     * whether it just traced a letter or a whole word. */
    bool takeCompleted();

private:
    struct Pt {
        int16_t x, y;
    };

    /** Recompute the uniform scale and the letterbox offsets. */
    void fitBox();
    void loadGlyph();
    void resampleWaypoints();
    int16_t scaleX(int16_t nx) const;
    int16_t scaleY(int16_t ny) const;
    /** What is being traced, as text: the letter, or the whole word. */
    const char* caption(char* buf, size_t len) const;
    void drawCaption(Ui::Renderer& tft);
    /** The finished shape, faintly, as the thing to aim at. */
    void drawGhost(Ui::Renderer& tft);
    /* Work out which waypoints are turns rather than continuations. Called
     * once per glyph, from resampleWaypoints(). */
    void findCorners();
    /* The next waypoint at or after the finger that is a turn, or NO_CORNER.
     * One arrow at a time, always the one that matters next. */
    uint8_t nextCorner() const;
    /** A small arrow at `index`, pointing the way the stroke goes next. */
    void drawArrow(Ui::Renderer& tft, uint8_t index);
    void drawTracedSegment(Ui::Renderer& tft, uint8_t from, uint8_t to);
    void drawAllDots(Ui::Renderer& tft);
    void drawProgress(Ui::Renderer& tft);
    void drawCompleteStatus(Ui::Renderer& tft);
    void drawModeTabs(Ui::Renderer& tft);
    void updatePulsePhase();
    void previousGlyph();
    void nextGlyph();
    uint8_t setFirstIndex() const;
    uint8_t setLastIndex() const;
    uint8_t setStartIndex() const;
    int16_t waypointSpacing() const;
    void markDirty() { dirty_ = true; }
    void markFullDirty() { dirty_ = true; fullDirty_ = true; }

    const Glyph* glyphs_ = nullptr;
    const Set* sets_ = nullptr;
    int16_t coordW_ = 200;
    int16_t coordH_ = 200;
    /* One scale for both axes, and where the box lands inside the canvas.
     * Computed once per configure() rather than per point. */
    float boxScale_ = 1.0f;
    int16_t boxX_ = 0;
    int16_t boxY_ = 0;
    uint8_t setCount_ = 0;
    uint8_t setIndex_ = 0;

    static constexpr uint8_t NO_CORNER = 0xFF;

    Pt pts_[MAX_POINTS] = {};
    /* Which waypoints are turns. A byte each rather than a bitfield: 128
     * bytes of static RAM against the arithmetic and the off-by-one risk of
     * packing it, on a device with 250KB spare. */
    bool corner_[MAX_POINTS] = {};
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
    bool justCompleted_ = false;
    /* Where the arrow currently is. When it moves the picture changes shape,
     * so that earns a full repaint -- see render(). Corners are a handful per
     * glyph, so this is a handful of full repaints per letter. */
    uint8_t arrowAt_ = NO_CORNER;
    /* What the panel already shows, so a partial frame can draw only what has
     * appeared since. See the note above render() in the .cpp. */
    uint8_t paintedStroke_ = 0;
    uint8_t paintedPoint_ = 0;
};
