#include "LetterTracer.h"

#include <esp_system.h>   // esp_random(), for a set that opens at random
#include <math.h>

namespace {

/* EVERY CONTROL LIVES IN A SIDE COLUMN, AND NONE ABOVE OR BELOW THE CANVAS.
 *
 * They used to sit in a strip 4px above the tracing area and another 12px
 * below it. A child tracing the top of a letter runs a finger straight off the
 * top edge into the mode tabs and lands on a different alphabet mid-stroke;
 * the same happens at the bottom with Prev and Next. The buttons were sitting
 * in the natural overshoot of the gesture the game exists to teach.
 *
 * THE COLUMNS ARE AS NARROW AS THE LABELS ALLOW, because everything they do
 * not use belongs to the letter. They were 68px wide with 26px buttons, which
 * left a 164px canvas -- fine for a single letter and cramped for a joined
 * word, where the whole point is the run across the page. At 52px they still
 * hold "Words" at font 1 and the canvas grows to 200x162, a fifth more area
 * and most of it in the direction a word needs. Do not shrink them further
 * without checking "Words" still fits and the targets are still finger-sized:
 * 52x22 with TOUCH_HIT_SLOP is about the floor on a resistive panel. */
constexpr int16_t COL_W = 52;
constexpr int16_t BTN_H = 22;
constexpr int16_t COL_L_X = 4;
constexpr int16_t COL_R_X = 264;
constexpr int16_t SET_Y = 52;
constexpr int16_t SET_STEP = 26;
constexpr Rect PREV_BTN{COL_L_X, 142, COL_W, BTN_H};
constexpr Rect RETRY_BTN{COL_R_X, 52, COL_W, BTN_H};
constexpr Rect NEXT_BTN{COL_R_X, 78, COL_W, BTN_H};

/* The word or letter, spelled out in ordinary type above the canvas.
 *
 * There was no such thing before -- only a font-1 watermark behind the dots,
 * which at word sizes was illegible, so a child tracing 'quiz' had no way to
 * know that was the word. A label is not a decoration here: the whole task is
 * "write this", and the child has to be able to read what "this" is. */
constexpr int16_t CAPTION_Y = 31;
constexpr int16_t CAPTION_H = 19;

constexpr int16_t DRAW_X = 60;
constexpr int16_t DRAW_Y = 52;
constexpr int16_t DRAW_W = 200;
/* 156 and not 162: a glyph's coordinates run to COORD_MAX exactly, so its
 * lowest point lands on DRAW_Y + DRAW_H, and the numbered badge drawn on it
 * is a 7px circle. At 162 that circle touched the progress bar. */
constexpr int16_t DRAW_H = 156;
constexpr int16_t HIT_RADIUS = 16;
constexpr uint32_t PULSE_PERIOD_MS = 500;
constexpr int16_t BAR_Y = 220;

/* Dot sizes, and they are small on purpose.
 *
 * They were radius 3 for a waypoint and 4 or 6 for the next one, which at a
 * single letter's 20px spacing was fine and at a word's 12px spacing merged
 * the letters into a chain of blobs -- reported from the device as not being
 * able to tell that the word was 'quiz'. At radius 2 the shape shows through
 * between the dots.
 *
 * The next dot pulses by CHANGING COLOUR AND NOT SIZE, which is also what
 * makes the incremental repaint below possible: a dot that never grows never
 * has to be erased, so a frame can overdraw it and touch nothing else. */
constexpr int16_t DOT_R = 2;
constexpr int16_t NEXT_R = 3;

/* HOW SHARP A TURN HAS TO BE TO EARN AN ARROW.
 *
 * A waypoint on a gentle curve turns by roughly step/radius radians -- at 10px
 * spacing, a 50px radius bends 11 degrees per dot and wants no arrow, while
 * the 15px radius at the bottom of a cursive undercurve bends nearly 40. So
 * the threshold separates "keep going round" from "now go the other way",
 * which is exactly the distinction a child needs pointing out.
 *
 * Too low and a curve sprouts an arrow every few dots; too high and the sharp
 * turn inside a cursive 'k' gets nothing. */
constexpr float CORNER_COS = 0.70f;      // ~46 degrees
/* And a turn cannot be marked within this many dots of the last one.
 *
 * Without it a tight curve fires on three or four consecutive waypoints,
 * because each of them individually bends past the threshold. Measured on the
 * real tables: cursive 'o' produced seven arrows in eighteen dots and print
 * 'S' four in twenty-two, which marks "you are on a curve" rather than "now
 * turn". With a three-dot gap the same letters get two and two, and print 'A'
 * gets exactly the three that matter -- the start, the apex, and the
 * crossbar. */
constexpr uint8_t CORNER_GAP = 3;
constexpr int16_t ARROW_LEN = 11;        // tip, measured from the waypoint
constexpr int16_t ARROW_HALF = 4;        // half the base width

Rect setTabRect(uint8_t i) {
    return Rect{COL_L_X, static_cast<int16_t>(SET_Y + i * SET_STEP), COL_W, BTN_H};
}

}   // namespace

void LetterTracer::configure(const Glyph* glyphs, const Set* sets,
                             uint8_t setCount, int16_t coordW, int16_t coordH) {
    glyphs_ = glyphs;
    sets_ = sets;
    setCount_ = setCount > MAX_SETS ? MAX_SETS : setCount;
    coordW_ = coordW > 0 ? coordW : 200;
    coordH_ = coordH > 0 ? coordH : 200;
    fitBox();
}

/* THE ONE SCALE, AND WHY THERE HAS TO BE ONE.
 *
 * This used to be two: x scaled by DRAW_W / 200 and y by DRAW_H / 200. When
 * the canvas was 164x160 those were 0.82 and 0.80 and nobody noticed. Widening
 * it to 200x156 made them 1.00 and 0.78 -- every letter drawn 22% shorter than
 * it is, which was reported from the device as words looking flat, and it was
 * not the word list's fault at all. A short word like 'six' has no ascender
 * and no descender, so the squash is all there is to see.
 *
 * So one scale for both axes, and the box is letterboxed inside the canvas
 * rather than stretched to it. A table authored square gets margins left and
 * right; a table authored to the canvas's own shape fills it. */
void LetterTracer::fitBox() {
    const float sx = static_cast<float>(DRAW_W) / static_cast<float>(coordW_);
    const float sy = static_cast<float>(DRAW_H) / static_cast<float>(coordH_);
    boxScale_ = sx < sy ? sx : sy;
    boxX_ = static_cast<int16_t>(
        DRAW_X + (DRAW_W - static_cast<int16_t>(coordW_ * boxScale_)) / 2);
    boxY_ = static_cast<int16_t>(
        DRAW_Y + (DRAW_H - static_cast<int16_t>(coordH_ * boxScale_)) / 2);
}

bool LetterTracer::takeDirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
}

bool LetterTracer::takeFullDirty() {
    const bool d = fullDirty_;
    fullDirty_ = false;
    return d;
}

bool LetterTracer::takeCompleted() {
    const bool d = justCompleted_;
    justCompleted_ = false;
    return d;
}

/* Where a set opens. Random for the word sets, its first entry otherwise --
 * see Set::randomStart. */
uint8_t LetterTracer::setStartIndex() const {
    const uint8_t first = setFirstIndex();
    if (sets_ == nullptr || setCount_ == 0) return first;
    const Set& s = sets_[setIndex_];
    if (!s.randomStart || s.count == 0) return first;
    return static_cast<uint8_t>(first + (esp_random() % s.count));
}

void LetterTracer::begin() {
    setIndex_ = 0;
    glyphIndex_ = setStartIndex();
    loadGlyph();
    markFullDirty();
}

/* What the child is being asked to write, as a string.
 *
 * A letter set can answer from Glyph::label, which is one char and is all a
 * letter needs. A word set cannot -- its glyphs each hold a whole word, and
 * Glyph::label carries only the first letter -- so it supplies its own names
 * and this reads them. Falling back to the label rather than to nothing means
 * a set that forgets its names shows something wrong rather than nothing at
 * all, which is easier to notice. */
const char* LetterTracer::caption(char* buf, size_t len) const {
    if (sets_ != nullptr && setCount_ > 0 && sets_[setIndex_].names != nullptr) {
        const uint8_t n = static_cast<uint8_t>(glyphIndex_ - setFirstIndex());
        if (n < sets_[setIndex_].count) {
            return sets_[setIndex_].names[n];
        }
    }
    if (glyphs_ == nullptr || len < 2) return "";
    buf[0] = glyphs_[glyphIndex_].label;
    buf[1] = 0;
    return buf;
}

/* The active set's dot spacing, or the default. A word is a third of a
 * letter's height, so the same number would put two dots on each letter and
 * make the guide useless. */
int16_t LetterTracer::waypointSpacing() const {
    if (sets_ == nullptr || setCount_ == 0) return DEFAULT_SPACING;
    const uint8_t s = sets_[setIndex_].spacing;
    return s != 0 ? s : DEFAULT_SPACING;
}

uint8_t LetterTracer::setFirstIndex() const {
    if (sets_ == nullptr || setCount_ == 0) return 0;
    return sets_[setIndex_].first;
}

uint8_t LetterTracer::setLastIndex() const {
    if (sets_ == nullptr || setCount_ == 0) return 0;
    const Set& s = sets_[setIndex_];
    return static_cast<uint8_t>(s.first + (s.count > 0 ? s.count - 1 : 0));
}

void LetterTracer::loadGlyph() {
    glyphIndex_ = constrain(glyphIndex_, setFirstIndex(), setLastIndex());
    complete_ = false;
    completeAt_ = 0;
    activeStroke_ = 0;
    nextPoint_ = 0;
    lastPulseChange_ = millis();
    pulseState_ = false;
    resampleWaypoints();
    arrowAt_ = nextCorner();
    /* A new glyph repaints everything anyway, so the arrow on the panel is
     * whatever that paint draws -- but say so, or the first partial frame
     * would try to erase an arrow belonging to the previous letter. */
    arrowDrawnAt_ = NO_CORNER;
    markFullDirty();
}

void LetterTracer::previousGlyph() {
    glyphIndex_ = (glyphIndex_ == setFirstIndex())
        ? setLastIndex()
        : static_cast<uint8_t>(glyphIndex_ - 1);
    loadGlyph();
}

void LetterTracer::nextGlyph() {
    glyphIndex_ = (glyphIndex_ == setLastIndex())
        ? setFirstIndex()
        : static_cast<uint8_t>(glyphIndex_ + 1);
    loadGlyph();
}

/* Walk each stroke at a constant spacing, so the dots a child chases are
 * evenly spread however long or curved the stroke is. A cursive letter is one
 * long curve where a printed one is two straight lines, and this is the code
 * that makes both feel the same to follow. */
void LetterTracer::resampleWaypoints() {
    if (glyphs_ == nullptr) {
        strokeCount_ = 0;
        return;
    }
    const Glyph& g = glyphs_[glyphIndex_];
    uint8_t totalPts = 0;

    for (uint8_t s = 0; s < g.strokeCount && s < MAX_STROKES; ++s) {
        const Stroke& st = g.strokes[s];
        strokeStart_[s] = totalPts;
        if (st.count == 0 || totalPts >= MAX_POINTS) {
            strokeLen_[s] = 0;
            continue;
        }

        pts_[totalPts].x = scaleX(st.pts[0]);
        pts_[totalPts].y = scaleY(st.pts[1]);
        ++totalPts;

        const int16_t spacing = waypointSpacing();
        float distanceToNext = spacing;
        for (uint8_t i = 0; i + 1 < st.count && totalPts < MAX_POINTS; ++i) {
            const int16_t x1 = st.pts[i * 2];
            const int16_t y1 = st.pts[i * 2 + 1];
            const int16_t x2 = st.pts[(i + 1) * 2];
            const int16_t y2 = st.pts[(i + 1) * 2 + 1];
            const float dx = x2 - x1;
            const float dy = y2 - y1;
            const float segLen = sqrtf(dx * dx + dy * dy);
            if (segLen < 0.0001f) {
                continue;
            }

            float walked = 0.0f;
            while (walked + distanceToNext <= segLen && totalPts < MAX_POINTS) {
                walked += distanceToNext;
                const float ratio = walked / segLen;
                pts_[totalPts].x = scaleX(static_cast<int16_t>(x1 + dx * ratio));
                pts_[totalPts].y = scaleY(static_cast<int16_t>(y1 + dy * ratio));
                ++totalPts;
                distanceToNext = spacing;
            }
            distanceToNext -= (segLen - walked);
        }

        const int16_t endX = scaleX(st.pts[(st.count - 1) * 2]);
        const int16_t endY = scaleY(st.pts[(st.count - 1) * 2 + 1]);
        const bool duplicateEnd = totalPts > strokeStart_[s] &&
            pts_[totalPts - 1].x == endX && pts_[totalPts - 1].y == endY;
        if (totalPts < MAX_POINTS && !duplicateEnd) {
            pts_[totalPts].x = endX;
            pts_[totalPts].y = endY;
            ++totalPts;
        }

        strokeLen_[s] = static_cast<uint8_t>(totalPts - strokeStart_[s]);
    }
    strokeCount_ = g.strokeCount > MAX_STROKES ? MAX_STROKES : g.strokeCount;
    findCorners();
}

/* Which waypoints are turns.
 *
 * The first point of every stroke counts, always: at the start there is no
 * previous direction to have changed from, and "which way do I set off?" is
 * the question a child actually has at that moment -- especially in cursive,
 * where a letter can begin by going up, down or sideways.
 *
 * After that it is the angle between arriving and leaving. The last point of a
 * stroke is never a corner: there is nowhere further to go. */
void LetterTracer::findCorners() {
    for (bool& c : corner_) c = false;

    for (uint8_t s = 0; s < strokeCount_; ++s) {
        const uint8_t start = strokeStart_[s];
        const uint8_t len = strokeLen_[s];
        if (len == 0) continue;
        corner_[start] = true;
        uint8_t lastMarked = 0;

        for (uint8_t i = 1; i + 1 < len && start + i + 1 < MAX_POINTS; ++i) {
            if (i - lastMarked < CORNER_GAP) continue;
            const uint8_t p = static_cast<uint8_t>(start + i);
            const float ax = static_cast<float>(pts_[p].x - pts_[p - 1].x);
            const float ay = static_cast<float>(pts_[p].y - pts_[p - 1].y);
            const float bx = static_cast<float>(pts_[p + 1].x - pts_[p].x);
            const float by = static_cast<float>(pts_[p + 1].y - pts_[p].y);
            const float la = sqrtf(ax * ax + ay * ay);
            const float lb = sqrtf(bx * bx + by * by);
            if (la < 0.5f || lb < 0.5f) continue;
            /* cos of the turn: 1 is straight on, 0 is a right angle. */
            if ((ax * bx + ay * by) / (la * lb) < CORNER_COS) {
                corner_[p] = true;
                lastMarked = i;
            }
        }
    }
}

uint8_t LetterTracer::nextCorner() const {
    if (activeStroke_ >= strokeCount_) return NO_CORNER;
    const uint8_t start = strokeStart_[activeStroke_];
    const uint8_t len = strokeLen_[activeStroke_];
    for (uint8_t i = nextPoint_; i + 1 < len && start + i < MAX_POINTS; ++i) {
        if (corner_[start + i]) return static_cast<uint8_t>(start + i);
    }
    return NO_CORNER;
}

/* A small arrow just past the waypoint, pointing where the stroke goes next.
 *
 * Past it rather than on it, so it does not bury the dot the finger is aiming
 * for -- the dot says WHERE and the arrow says WHICH WAY, and they are easier
 * to read as two things than as one. */
/* The box an arrow occupies. Derived from the same two points the arrow is
 * drawn from, plus a pixel of daylight, so it cannot be too small however the
 * arrow's proportions change -- CLAUDE.md's rule about clear rectangles being
 * derived and never typed in. */
Rect LetterTracer::arrowRect(uint8_t index) const {
    if (index == NO_CORNER || index + 1 >= MAX_POINTS) return Rect{0, 0, 0, 0};
    const int16_t reach = static_cast<int16_t>(NEXT_R + ARROW_LEN + ARROW_HALF + 2);
    return Rect{static_cast<int16_t>(pts_[index].x - reach),
                static_cast<int16_t>(pts_[index].y - reach),
                static_cast<int16_t>(reach * 2),
                static_cast<int16_t>(reach * 2)};
}

/* Move the arrow, in place, WITHOUT a full repaint.
 *
 * The first version marked the whole screen dirty whenever the arrow moved, on
 * the reasoning that a moving arrow changes the picture's shape and corners are
 * rare. Corners are not rare: a three-letter joined word has about eleven, so
 * that was eleven screen clears while tracing one word, and it was reported
 * from the device as the screen flashing. It was my misjudgement, not a
 * surprise -- a full repaint wipes 200x156 pixels plus the chrome to move
 * fifteen.
 *
 * So: erase the old arrow's own box, then repaint the guide over it. Both
 * drawGhost() and drawAllDots() are IDEMPOTENT -- they paint exactly what is
 * already there in exactly the same colours -- so running them whole is
 * visually a no-op everywhere except inside the box that was just cleared, and
 * costs no clear of its own. That is what makes this flicker-free without
 * having to work out which ghost segments and which dots the arrow overlapped.
 */
void LetterTracer::moveArrow(Ui::Renderer& tft) {
    if (arrowDrawnAt_ == arrowAt_) return;
    if (arrowDrawnAt_ != NO_CORNER) {
        const Rect r = arrowRect(arrowDrawnAt_);
        if (r.w > 0) {
            tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
            drawGhost(tft);
            drawAllDots(tft);
        }
    }
    drawArrow(tft, arrowAt_);
    arrowDrawnAt_ = arrowAt_;
}

void LetterTracer::drawArrow(Ui::Renderer& tft, uint8_t index) {
    if (index == NO_CORNER || index + 1 >= MAX_POINTS) return;
    const float dx = static_cast<float>(pts_[index + 1].x - pts_[index].x);
    const float dy = static_cast<float>(pts_[index + 1].y - pts_[index].y);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const float ux = dx / len;
    const float uy = dy / len;

    const float bx = static_cast<float>(pts_[index].x) + ux * NEXT_R;
    const float by = static_cast<float>(pts_[index].y) + uy * NEXT_R;
    const int16_t tipX = static_cast<int16_t>(bx + ux * ARROW_LEN);
    const int16_t tipY = static_cast<int16_t>(by + uy * ARROW_LEN);
    const int16_t leftX = static_cast<int16_t>(bx - uy * ARROW_HALF);
    const int16_t leftY = static_cast<int16_t>(by + ux * ARROW_HALF);
    const int16_t rightX = static_cast<int16_t>(bx + uy * ARROW_HALF);
    const int16_t rightY = static_cast<int16_t>(by - ux * ARROW_HALF);
    tft.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, Ui::warning());
}

int16_t LetterTracer::scaleX(int16_t nx) const {
    return static_cast<int16_t>(boxX_ + nx * boxScale_);
}

int16_t LetterTracer::scaleY(int16_t ny) const {
    return static_cast<int16_t>(boxY_ + ny * boxScale_);
}

void LetterTracer::updatePulsePhase() {
    const uint32_t now = millis();
    if (now - lastPulseChange_ >= PULSE_PERIOD_MS / 2) {
        pulseState_ = !pulseState_;
        lastPulseChange_ = now;
        markDirty();
    }
}

void LetterTracer::update(AppContext& host, const TouchPoint& touch) {
    if (touch.justPressed) {
        for (uint8_t i = 0; i < setCount_; ++i) {
            if (!setTabRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                continue;
            }
            setIndex_ = i;
            glyphIndex_ = setStartIndex();
            loadGlyph();
            return;
        }
        if (RETRY_BTN.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            loadGlyph();
            return;
        }
        if (PREV_BTN.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            previousGlyph();
            return;
        }
        if (NEXT_BTN.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            nextGlyph();
            return;
        }
    }

    if (complete_) {
        return;
    }

    updatePulsePhase();

    if (!touch.down) {
        lastPulseChange_ = millis();
        pulseState_ = false;
    }

    if (touch.down && activeStroke_ < strokeCount_) {
        const uint8_t startIdx = strokeStart_[activeStroke_];
        const uint8_t nextPtIdx = static_cast<uint8_t>(startIdx + nextPoint_);

        if (nextPtIdx < MAX_POINTS && nextPoint_ < strokeLen_[activeStroke_]) {
            const int16_t dx = static_cast<int16_t>(touch.x - pts_[nextPtIdx].x);
            const int16_t dy = static_cast<int16_t>(touch.y - pts_[nextPtIdx].y);
            const int32_t d2 = (int32_t)dx * dx + (int32_t)dy * dy;

            if (d2 < (int32_t)HIT_RADIUS * HIT_RADIUS) {
                ++nextPoint_;
                markDirty();
                /* The arrow moving is a change of shape, not an addition, so
                 * it earns a full repaint -- the old one has to go. Corners
                 * are a handful per glyph, so this is rare. */
                /* The arrow moving is NOT a full repaint. See moveArrow():
                 * a word has about eleven turns and clearing the screen at
                 * each one is the flashing this replaced. */
                arrowAt_ = nextCorner();

                if (nextPoint_ >= strokeLen_[activeStroke_]) {
                    host.beepOk();
                    ++activeStroke_;
                    nextPoint_ = 0;
                    /* A finished stroke changes the picture's shape rather
                     * than adding to it -- the numbered badge moves to the
                     * next stroke's first dot -- so this is one of the events
                     * that earns a full repaint. See render(). */
                    markFullDirty();

                    if (activeStroke_ >= strokeCount_) {
                        complete_ = true;
                        completeAt_ = millis();
                        justCompleted_ = true;
                        host.beepOk();
                        markFullDirty();
                    }
                }
            }
        }
    }
}

void LetterTracer::drawModeTabs(Ui::Renderer& tft) {
    const uint16_t activeBg = Ui::warning();
    const uint16_t activeTxt = Ui::panel();
    const uint16_t inactiveBg = Ui::panel();
    const uint16_t inactiveTxt = Ui::text();

    auto tab = [&](Rect r, const char* label, bool active) {
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, active ? activeBg : inactiveBg);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
        tft.setTextColor(active ? activeTxt : inactiveTxt,
                         active ? activeBg : inactiveBg);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 1);
    };

    for (uint8_t i = 0; i < setCount_; ++i) {
        tab(setTabRect(i), sets_[i].label, i == setIndex_);
    }
    tab(RETRY_BTN, "Again", false);
    tab(NEXT_BTN, "Next", false);
    tab(PREV_BTN, "Prev", false);
}

/* What to write, in ordinary type, above the canvas. */
void LetterTracer::drawCaption(Ui::Renderer& tft) {
    char buf[2];
    tft.fillRect(DRAW_X, CAPTION_Y, DRAW_W, CAPTION_H, Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(caption(buf, sizeof(buf)),
                   static_cast<int16_t>(DRAW_X + DRAW_W / 2),
                   static_cast<int16_t>(CAPTION_Y + CAPTION_H / 2), 2);
}

/* The finished shape, faintly, under everything else.
 *
 * This is the answer to "what am I aiming at?" -- a child can see the whole
 * letter or word before starting and check their own line against it as they
 * go, which is what a handwriting workbook's grey letter does. It costs no
 * screen space, which is why it went here rather than into a thumbnail beside
 * the canvas: there is no room for a second copy of a joined word.
 *
 * Drawn from the RAW glyph polyline rather than from the resampled waypoints,
 * because the waypoints are 12 to 20 pixels apart and a polyline through them
 * visibly corners on the tight curves -- which is exactly where a child needs
 * the target to be accurate. */
void LetterTracer::drawGhost(Ui::Renderer& tft) {
    if (glyphs_ == nullptr) return;
    const Glyph& g = glyphs_[glyphIndex_];
    for (uint8_t st = 0; st < g.strokeCount && st < MAX_STROKES; ++st) {
        const Stroke& s = g.strokes[st];
        for (uint8_t i = 0; i + 1 < s.count; ++i) {
            tft.drawLine(scaleX(s.pts[i * 2]), scaleY(s.pts[i * 2 + 1]),
                         scaleX(s.pts[(i + 1) * 2]), scaleY(s.pts[(i + 1) * 2 + 1]),
                         Ui::outline());
        }
    }
}

void LetterTracer::drawTracedSegment(Ui::Renderer& tft, uint8_t from, uint8_t to) {
    tft.drawLine(pts_[from].x, pts_[from].y, pts_[to].x, pts_[to].y, Ui::success());
    tft.drawLine(pts_[from].x + 1, pts_[from].y, pts_[to].x + 1, pts_[to].y,
                 Ui::success());
}

/* Every dot of every stroke. Only on a full repaint -- see render(). */
void LetterTracer::drawAllDots(Ui::Renderer& tft) {
    for (uint8_t st = 0; st < strokeCount_; ++st) {
        const uint8_t start = strokeStart_[st];
        const uint8_t len = strokeLen_[st];
        const bool done = st < activeStroke_;
        const bool active = st == activeStroke_;
        const uint8_t inked = active ? min(nextPoint_, (uint8_t)len)
                                     : (done ? len : 0);

        for (uint8_t i = 0; i + 1 < inked && start + i + 1 < MAX_POINTS; ++i) {
            drawTracedSegment(tft, static_cast<uint8_t>(start + i),
                              static_cast<uint8_t>(start + i + 1));
        }
        for (uint8_t i = 0; i < len && start + i < MAX_POINTS; ++i) {
            const uint8_t idx = static_cast<uint8_t>(start + i);
            if (i < inked) {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, DOT_R, Ui::success());
            } else if (active && i == inked) {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, NEXT_R,
                               pulseState_ ? Ui::text() : Ui::warning());
            } else {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, DOT_R, Ui::muted());
            }
        }
    }

    /* Which stroke this is, on its first dot. A cursive word is one stroke and
     * a printed letter is up to four, and the number is how a child knows
     * there is more to come after this one. */
    if (activeStroke_ < strokeCount_) {
        const uint8_t start = strokeStart_[activeStroke_];
        tft.fillCircle(pts_[start].x, pts_[start].y, 7, Ui::warning());
        tft.setTextColor(Ui::panel(), Ui::warning());
        tft.setTextDatum(MC_DATUM);
        char badge[2];
        badge[0] = static_cast<char>('1' + activeStroke_);
        badge[1] = 0;
        tft.drawString(badge, pts_[start].x, pts_[start].y, 1);
    }
}

void LetterTracer::drawProgress(Ui::Renderer& tft) {
    uint16_t totalPoints = 0;
    uint16_t claimedPoints = 0;

    for (uint8_t s = 0; s < strokeCount_; ++s) {
        totalPoints = static_cast<uint16_t>(totalPoints + strokeLen_[s]);
        if (s < activeStroke_) {
            claimedPoints = static_cast<uint16_t>(claimedPoints + strokeLen_[s]);
        } else if (s == activeStroke_) {
            claimedPoints = static_cast<uint16_t>(claimedPoints + nextPoint_);
        }
    }

    const uint8_t pct = totalPoints > 0
        ? static_cast<uint8_t>((uint32_t)claimedPoints * 100 / totalPoints)
        : 0;
    const int16_t barW = 120;
    const int16_t barX = (GAME_CANVAS_WIDTH - barW) / 2;

    tft.fillRoundRect(barX, BAR_Y, barW, 10, 4, Ui::panel());
    tft.drawRoundRect(barX, BAR_Y, barW, 10, 4, Ui::outline());

    const int16_t fillW = static_cast<int16_t>((int32_t)barW * pct / 100);
    if (fillW > 0) {
        tft.fillRoundRect(barX, BAR_Y, fillW, 10, 4, Ui::success());
    }
}

void LetterTracer::drawCompleteStatus(Ui::Renderer& tft) {
    /* Over the bottom of the canvas, which is the one place a badge can go
     * without stealing room from the letter for the whole game to pay for a
     * message that shows for a moment. Painted only on the repaint completion
     * triggers, and wiped by the next one. */
    const int16_t w = 156;
    const int16_t h = 21;
    const int16_t x = static_cast<int16_t>(DRAW_X + (DRAW_W - w) / 2);
    const int16_t y = static_cast<int16_t>(DRAW_Y + DRAW_H - h - 2);
    tft.fillRoundRect(x, y, w, h, 6, Ui::success());
    tft.drawRoundRect(x, y, w, h, 6, Ui::outline());
    tft.setTextColor(TFT_BLACK, Ui::success());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Great job", static_cast<int16_t>(x + w / 2),
                   static_cast<int16_t>(y + h / 2), 2);
}

/* WHY THIS IS NOT ONE FULL REPAINT PER FRAME.
 *
 * It used to be: every dirty frame wiped the whole canvas and redrew the
 * ghost, every dot and the badge. Claiming one dot changed about forty pixels
 * and cost a 200x162 wipe plus a hundred and fifty circles, and the pulse made
 * that happen twice a second whether or not anybody was tracing. On a panel
 * where a full-screen repaint is 30ms of visible blanking that is both a waste
 * and a flicker.
 *
 * What actually changes between frames is small and additive: a dot goes from
 * grey to green, a short line appears behind it, and the next dot changes
 * colour. None of that needs anything erased -- which is the whole reason the
 * pulse changes colour instead of size. So a partial frame overdraws the few
 * dots that moved and the progress bar, and touches nothing else.
 *
 * A full repaint is kept for the cases where the picture genuinely changes
 * shape: a new glyph, a new alphabet, a stroke finishing (the badge moves to
 * the next stroke's first dot), and completion. Those are events, not frames.
 */
void LetterTracer::render(AppContext& host, const char* title, bool fullRender) {
    Ui::Renderer& tft = host.display();
    if (glyphs_ == nullptr) return;

    if (fullRender) {
        Ui::clear(tft);
        host.drawTopBar(title);
        drawModeTabs(tft);
        drawCaption(tft);
        tft.fillRect(DRAW_X, DRAW_Y, DRAW_W, DRAW_H, Ui::bg());
        drawGhost(tft);
        drawAllDots(tft);
        drawArrow(tft, arrowAt_);
        drawProgress(tft);
        if (complete_) drawCompleteStatus(tft);
        paintedStroke_ = activeStroke_;
        paintedPoint_ = nextPoint_;
        arrowDrawnAt_ = arrowAt_;
        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (activeStroke_ < strokeCount_) {
        const uint8_t start = strokeStart_[activeStroke_];
        const uint8_t len = strokeLen_[activeStroke_];

        /* Dots claimed since the last paint, and the line into each. */
        for (uint8_t i = paintedPoint_; i < nextPoint_ && i < len; ++i) {
            const uint8_t idx = static_cast<uint8_t>(start + i);
            if (i > 0) {
                drawTracedSegment(tft, static_cast<uint8_t>(idx - 1), idx);
            }
            tft.fillCircle(pts_[idx].x, pts_[idx].y, DOT_R, Ui::success());
        }
        /* The next dot, in whichever half of the pulse we are in. Same radius
         * every time, so this is an overdraw and never an erase. */
        if (nextPoint_ < len) {
            const uint8_t idx = static_cast<uint8_t>(start + nextPoint_);
            tft.fillCircle(pts_[idx].x, pts_[idx].y, NEXT_R,
                           pulseState_ ? Ui::text() : Ui::warning());
        }
        paintedPoint_ = nextPoint_;
    }

    moveArrow(tft);
    drawProgress(tft);
    tft.setTextDatum(TL_DATUM);
}
