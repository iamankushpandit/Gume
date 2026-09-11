#include "LetterTracer.h"

#include <esp_system.h>   // esp_random(), for a set that opens at random
#include <math.h>
#include "LetterTracerLayout.h"

using namespace LetterTracerLayout;

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
    selectSet(0);
}

/* Switch alphabet. A spelled set works out its one word scale here, once,
 * rather than per word -- see fitSpelledSet(). */
void LetterTracer::selectSet(uint8_t i) {
    setIndex_ = i < setCount_ ? i : 0;
    if (spelled()) fitSpelledSet();
    glyphIndex_ = setStartIndex();
    loadGlyph();
}

bool LetterTracer::spelled() const {
    return sets_ != nullptr && setCount_ > 0 && glyphs_ != nullptr &&
           set().alphabet != NO_ALPHABET && set().names != nullptr;
}

const LetterTracer::Glyph& LetterTracer::glyph() const {
    return spelled() ? spelled_ : glyphs_[glyphIndex_];
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
    buf[0] = glyph().label;
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
    /* A spelled word is laid out straight into canvas pixels, so it is drawn
     * with the identity mapping; everything else goes through the letterbox
     * its table was authored for. */
    if (spelled()) {
        spellWord();
        boxScale_ = 1.0f;
        boxX_ = DRAW_X;
        boxY_ = DRAW_Y;
    } else {
        fitBox();
    }
    resampleWaypoints();
    planArrows();
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
    const Glyph& g = glyph();
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
            selectSet(i);
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
                    /* NOT a full repaint, though the picture does change:
                     * the start ring moves to the next stroke and two sets
                     * of arrows swap colour. A printed word is up to eight
                     * strokes, and clearing the screen between each is the
                     * flashing CLAUDE.md's rendering rule is about. render()
                     * moves the ring in place instead. */

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
