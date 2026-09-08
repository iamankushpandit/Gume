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
constexpr int16_t COORD_MAX = 200;
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

Rect setTabRect(uint8_t i) {
    return Rect{COL_L_X, static_cast<int16_t>(SET_Y + i * SET_STEP), COL_W, BTN_H};
}

}   // namespace

void LetterTracer::configure(const Glyph* glyphs, const Set* sets,
                             uint8_t setCount) {
    glyphs_ = glyphs;
    sets_ = sets;
    setCount_ = setCount > MAX_SETS ? MAX_SETS : setCount;
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
}

int16_t LetterTracer::scaleX(int16_t nx) const {
    return static_cast<int16_t>(DRAW_X + (int32_t)nx * DRAW_W / COORD_MAX);
}

int16_t LetterTracer::scaleY(int16_t ny) const {
    return static_cast<int16_t>(DRAW_Y + (int32_t)ny * DRAW_H / COORD_MAX);
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
        drawProgress(tft);
        if (complete_) drawCompleteStatus(tft);
        paintedStroke_ = activeStroke_;
        paintedPoint_ = nextPoint_;
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

    drawProgress(tft);
    tft.setTextDatum(TL_DATUM);
}
