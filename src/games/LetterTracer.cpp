#include "LetterTracer.h"

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
 * Side columns put them where the hand is not. That also frees the strips
 * above and below, so the canvas grows from 160x132 to 164x160 -- worth having
 * on its own, because the glyph coordinate space is COORD_MAX square. Mapping
 * a 200x200 design into 160x132 squashed every letter vertically; 164x160 is
 * nearly square, so the letters a child copies are the shape they should be. */
constexpr int16_t DRAW_X = 78;
constexpr int16_t DRAW_Y = 36;
constexpr int16_t DRAW_W = 164;
constexpr int16_t DRAW_H = 160;
constexpr int16_t COORD_MAX = 200;
constexpr int16_t WAYPOINT_SPACING = 20;
constexpr int16_t HIT_RADIUS = 16;
constexpr uint32_t PULSE_PERIOD_MS = 500;

/* Left column: the alphabet tabs, then Prev. Right column: Again and Next.
 * The set tabs step down from SET_Y, so a game with two alphabets leaves the
 * third slot empty rather than moving Prev -- a control that shifts position
 * depending on which game you opened is a control you have to look for. */
constexpr int16_t COL_W = 68;
constexpr int16_t SET_Y = 40;
constexpr int16_t SET_STEP = 32;
constexpr Rect PREV_BTN{4, 160, COL_W, 30};
constexpr Rect NEXT_BTN{248, 72, COL_W, 26};
constexpr Rect RETRY_BTN{248, 40, COL_W, 26};

Rect setTabRect(uint8_t i) {
    return Rect{4, static_cast<int16_t>(SET_Y + i * SET_STEP), COL_W, 26};
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

void LetterTracer::begin() {
    setIndex_ = 0;
    glyphIndex_ = setFirstIndex();
    loadGlyph();
    markFullDirty();
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

        float distanceToNext = WAYPOINT_SPACING;
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
                distanceToNext = WAYPOINT_SPACING;
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
            glyphIndex_ = setFirstIndex();
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

                    if (activeStroke_ >= strokeCount_) {
                        complete_ = true;
                        completeAt_ = millis();
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

void LetterTracer::drawGuide(Ui::Renderer& tft) {
    // Completed strokes, in the success colour.
    for (uint8_t s = 0; s < activeStroke_ && s < strokeCount_; ++s) {
        const uint8_t start = strokeStart_[s];
        for (uint8_t i = 0; i + 1 < strokeLen_[s] && start + i < MAX_POINTS; ++i) {
            const uint8_t a = static_cast<uint8_t>(start + i);
            const uint8_t b = static_cast<uint8_t>(start + i + 1);
            tft.drawLine(pts_[a].x, pts_[a].y, pts_[b].x, pts_[b].y, Ui::success());
            tft.drawLine(pts_[a].x + 1, pts_[a].y, pts_[b].x + 1, pts_[b].y,
                         Ui::success());
        }
    }

    if (activeStroke_ < strokeCount_) {
        const uint8_t start = strokeStart_[activeStroke_];
        const uint16_t inked = min(nextPoint_, (uint8_t)strokeLen_[activeStroke_]);

        for (uint8_t i = 0; i + 1 < inked && start + i < MAX_POINTS; ++i) {
            const uint8_t a = static_cast<uint8_t>(start + i);
            const uint8_t b = static_cast<uint8_t>(start + i + 1);
            tft.drawLine(pts_[a].x, pts_[a].y, pts_[b].x, pts_[b].y, Ui::success());
            tft.drawLine(pts_[a].x + 1, pts_[a].y, pts_[b].x + 1, pts_[b].y,
                         Ui::success());
        }

        for (uint8_t i = 0; i < strokeLen_[activeStroke_] && start + i < MAX_POINTS; ++i) {
            const uint8_t idx = static_cast<uint8_t>(start + i);
            uint16_t colour;

            if (i < inked) {
                colour = Ui::success();
            } else if (i == inked && pulseState_) {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, 6, Ui::warning());
                continue;
            } else if (i == inked) {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, 4, Ui::warning());
                continue;
            } else {
                colour = Ui::muted();
            }

            tft.fillCircle(pts_[idx].x, pts_[idx].y, 3, colour);
        }

        // Which stroke this is, on its first dot: cursive letters are one
        // stroke, printed ones are up to four, and the number is how a child
        // knows there is more to come.
        tft.fillCircle(pts_[start].x, pts_[start].y, 7, Ui::warning());
        tft.setTextColor(Ui::panel(), Ui::warning());
        tft.setTextDatum(MC_DATUM);
        char badge[2];
        badge[0] = static_cast<char>('1' + activeStroke_);
        badge[1] = 0;
        tft.drawString(badge, pts_[start].x, pts_[start].y, 1);
    }

    // Strokes not started yet, as faint dots.
    for (uint8_t s = static_cast<uint8_t>(activeStroke_ + 1); s < strokeCount_; ++s) {
        const uint8_t start = strokeStart_[s];
        for (uint8_t i = 0; i < strokeLen_[s] && start + i < MAX_POINTS; ++i) {
            tft.fillCircle(pts_[start + i].x, pts_[start + i].y, 2, Ui::muted());
        }
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
    const int16_t barY = 222;

    tft.fillRoundRect(barX, barY, barW, 10, 4, Ui::panel());
    tft.drawRoundRect(barX, barY, barW, 10, 4, Ui::outline());

    const int16_t fillW = static_cast<int16_t>((int32_t)barW * pct / 100);
    if (fillW > 0) {
        tft.fillRoundRect(barX, barY, fillW, 10, 4, Ui::success());
    }
}

void LetterTracer::drawCompleteStatus(Ui::Renderer& tft) {
    /* Below the canvas, which ends at DRAW_Y + DRAW_H, and above the progress
     * bar at y=222. */
    constexpr Rect STATUS{82, 198, 156, 21};
    tft.fillRoundRect(STATUS.x, STATUS.y, STATUS.w, STATUS.h, 6, Ui::success());
    tft.drawRoundRect(STATUS.x, STATUS.y, STATUS.w, STATUS.h, 6, Ui::outline());
    tft.setTextColor(TFT_BLACK, Ui::success());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Great job", STATUS.x + STATUS.w / 2,
                   STATUS.y + STATUS.h / 2, 2);
}

void LetterTracer::render(AppContext& host, const char* title, bool fullRender) {
    Ui::Renderer& tft = host.display();
    if (glyphs_ == nullptr) return;
    const Glyph& g = glyphs_[glyphIndex_];

    if (fullRender) {
        Ui::clear(tft);
        host.drawTopBar(title);
        drawModeTabs(tft);
    } else {
        tft.fillRect(76, 197, 168, 26, Ui::bg());
    }

    tft.fillRect(DRAW_X - 2, DRAW_Y - 2, DRAW_W + 4, DRAW_H + 4, Ui::bg());
    tft.setTextColor(Ui::panel(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    char label[2] = {g.label, 0};
    tft.drawString(label, DRAW_X + DRAW_W / 2, DRAW_Y + DRAW_H / 2, 1);

    drawGuide(tft);
    drawProgress(tft);

    if (complete_) {
        drawCompleteStatus(tft);
    }

    tft.setTextDatum(TL_DATUM);
}
