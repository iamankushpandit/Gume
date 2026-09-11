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
        /* Whether a sharp reversal inside a stroke -- the top of an A, the
         * points of an M -- gets an arrow of its own, as well as the numbered
         * arrow every stroke gets at its start.
         *
         * On for printed letters, where a stroke really does change direction
         * abruptly and the dots alone do not say which way the finger goes
         * next. Off for cursive and for words: a joined word is one long
         * stroke full of loops, and arrows at every loop were exactly what
         * five-year-olds in testing found confusing. */
        bool turnArrows;
        /* Build each entry from its name, letter by letter, instead of
         * reading it from the glyph table.
         *
         * NO_ALPHABET for a set whose glyphs are in the table. Otherwise the
         * index in the table of the glyph for 'a'; every entry's name is then
         * spelled out of that alphabet at load time. That is how Trace has
         * printed words without a single extra byte of letterform in flash --
         * a printed word is just its letters side by side, each with its own
         * strokes, which is exactly what print is. Cursive cannot do this,
         * because joining the letters is the whole skill, so its words are
         * generated. `first` is unused for such a set. */
        uint8_t alphabet;
    };

    static constexpr uint8_t NO_ALPHABET = 0xFF;

    /* Longest resampled run, across all of a glyph's strokes.
     *
     * Raised from 96 for word tracing: three letters at a tighter spacing than
     * a single letter needs is comfortably more dots than any one letter, and
     * running out silently truncates the last stroke -- a word whose final
     * letter cannot be finished, with nothing on screen to say why. 128 points
     * is 512 bytes of static RAM, which on this device is nothing; see
     * CLAUDE.md's memory rule on trading RAM for certainty. */
    static constexpr uint8_t MAX_POINTS = 128;
    /* Eight. A single letter never needs more than three, and a cursive word
     * is one stroke plus the marks on its i, j or t -- but a PRINTED word is
     * every stroke of every letter, and 'kit' is already six. */
    static constexpr uint8_t MAX_STROKES = 8;
    static constexpr uint8_t DEFAULT_SPACING = 20;
    /** Four tabs is what the left column holds; see LetterTracerLayout.h. */
    static constexpr uint8_t MAX_SETS = 4;
    /* One numbered arrow per stroke, plus one per sharp reversal in a set
     * that asks for those. Print 'M' is the most: one stroke, three turns. */
    static constexpr uint8_t MAX_ARROWS = 12;
    /* Raw points in a word spelled out of an alphabet, x and y counted
     * separately: 96 points. The printed lowercase letters are sparse
     * polylines -- the longest, 'g' and 's', are fourteen and twelve points --
     * so a three-letter word is under fifty. A word that would not fit loses
     * its last strokes, visibly; keep word lists to short words. */
    static constexpr uint16_t MAX_WORD_COORDS = 192;

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

    /* One direction arrow, placed once per glyph and never moved. Canvas
     * pixels throughout, so drawing it is arithmetic-free. */
    struct Arrow {
        int16_t tailX, tailY;
        int16_t tipX, tipY;
        /* Where the stroke number goes; unused when `numbered` is false. */
        int16_t labelX, labelY;
        uint8_t stroke;
        /* True for the arrow at a stroke's start, which carries its number;
         * false for one at a turn part-way along. */
        bool numbered;
    };

    /** The set currently selected. */
    const Set& set() const { return sets_[setIndex_]; }
    /** Switch to set `i` and open it at its starting entry. */
    void selectSet(uint8_t i);
    /** True when the current set spells its entries out of an alphabet. */
    bool spelled() const;
    /** The glyph being traced: from the table, or spelled into RAM. */
    const Glyph& glyph() const;
    /** Recompute the uniform scale and the letterbox offsets. */
    void fitBox();
    /* Lay the current word out of its letters, into spelled_. See
     * LetterTracerWords.cpp. */
    void spellWord();
    /* The one scale every word of a spelled set is drawn at: the widest word
     * decides it, once, when the set is chosen. */
    void fitSpelledSet();
    void loadGlyph();
    void resampleWaypoints();
    int16_t scaleX(int16_t nx) const;
    int16_t scaleY(int16_t ny) const;
    /** What is being traced, as text: the letter, or the whole word. */
    const char* caption(char* buf, size_t len) const;
    void drawCaption(Ui::Renderer& tft);
    /** The finished shape, faintly, as the thing to aim at. */
    void drawGhost(Ui::Renderer& tft);
    /* Decide where every arrow for this glyph goes. Called once per glyph,
     * from loadGlyph(); see LetterTracerArrows.cpp. */
    void planArrows();
    /* Place one arrow for the stroke `s`, starting `arc` pixels along it.
     * (cx, cy) is the middle of the glyph, which "outside" leads away from. */
    void placeArrow(uint8_t s, float arc, bool numbered, float cx, float cy);
    /* Distance from (x, y) to the nearest stroke, as the dots draw it. Stops
     * early, with some distance under `floor`, once the answer is "too near";
     * a floor of zero or less always measures fully. */
    float clearance(float x, float y, float floor) const;
    void drawArrows(Ui::Renderer& tft);
    void drawStartRing(Ui::Renderer& tft);
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

    /* A word spelled out of an alphabet, laid out in canvas pixels relative
     * to the canvas's corner. Fixed arrays, filled in place: the tracer never
     * allocates, and a word is rebuilt on every Next, which is exactly the
     * churn CLAUDE.md's memory rule is about. */
    int16_t spelledPts_[MAX_WORD_COORDS] = {};
    Stroke spelledStrokes_[MAX_STROKES] = {};
    Glyph spelled_ = {0, spelledStrokes_, 0};
    /* The scale for every word in the spelled set, and how far down the
     * canvas the letters' top guide sits. Set by fitSpelledSet(). */
    float wordScale_ = 1.0f;
    int16_t wordTop_ = 0;

    Pt pts_[MAX_POINTS] = {};
    /* Each stroke's waypoints' bounding box, as min x, max x, min y, max y.
     * Lets the arrow planner skip a stroke nowhere near a candidate instead
     * of measuring every segment of it -- a printed word is eight strokes and
     * any one candidate is near one or two. */
    int16_t strokeBox_[MAX_STROKES][4] = {};
    Arrow arrows_[MAX_ARROWS] = {};
    uint8_t arrowCount_ = 0;
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
    /* What the panel already shows, so a partial frame can draw only what has
     * appeared since. See the note above render() in the .cpp. */
    uint8_t paintedStroke_ = 0;
    uint8_t paintedPoint_ = 0;
};
