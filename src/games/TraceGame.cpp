#include "TraceGame.h"
#include "TraceGlyphData.h"
#include "engine/AppRegistry.h"

namespace {

constexpr int16_t DRAW_X = 80;
constexpr int16_t DRAW_Y = 58;
constexpr int16_t DRAW_W = 160;
constexpr int16_t DRAW_H = 132;
constexpr int16_t COORD_MAX = 200;
constexpr int16_t WAYPOINT_SPACING = 20;
constexpr int16_t HIT_RADIUS = 16;
constexpr uint32_t PULSE_PERIOD_MS = 500;

constexpr Rect PREV_BTN{8,   202, 60, 30};
constexpr Rect NEXT_BTN{252, 202, 60, 30};

constexpr Rect MODE_ABC{38,  32, 50, 22};
constexpr Rect MODE_abc{92,  32, 50, 22};
constexpr Rect MODE_123{146, 32, 50, 22};
constexpr Rect RETRY_BTN{204, 32, 52, 22};
constexpr Rect TOP_NEXT_BTN{260, 32, 52, 22};

constexpr AppMetadata TRACE_METADATA = {
    "trace",
    "Trace",
    nullptr,
    "ABC abc 123",
    "Trace",
    "Trace big and small letters.",
    nullptr,
    LauncherIcon::Trace,
    23,
    true,
};
}

const AppMetadata& traceAppMetadata() {
    return TRACE_METADATA;
}

const char* TraceGame::title() const {
    return traceAppMetadata().screenTitle != nullptr
        ? traceAppMetadata().screenTitle
        : traceAppMetadata().title;
}

void TraceGame::begin(AppContext& host) {
    (void)host;
    glyphIndex_ = 0;
    glyphSet_ = GlyphSet::Upper;
    loadGlyph();
    markFullDirty();
}

uint8_t TraceGame::getSetFirstIndex() const {
    switch (glyphSet_) {
        case GlyphSet::Upper: return UPPER_FIRST;
        case GlyphSet::Lower: return LOWER_FIRST;
        case GlyphSet::Digit: return DIGIT_FIRST;
    }
    return 0;
}

uint8_t TraceGame::getSetLastIndex() const {
    switch (glyphSet_) {
        case GlyphSet::Upper: return LOWER_FIRST - 1;
        case GlyphSet::Lower: return DIGIT_FIRST - 1;
        case GlyphSet::Digit: return GLYPH_COUNT_TOTAL - 1;
    }
    return 0;
}

void TraceGame::loadGlyph() {
    glyphIndex_ = constrain(glyphIndex_, getSetFirstIndex(), getSetLastIndex());
    complete_ = false;
    completeAt_ = 0;
    activeStroke_ = 0;
    nextPoint_ = 0;
    lastPulseChange_ = millis();
    pulseState_ = false;
    resampleWaypoints();
    markFullDirty();
}

void TraceGame::previousGlyph() {
    glyphIndex_ = (glyphIndex_ == getSetFirstIndex())
        ? getSetLastIndex()
        : glyphIndex_ - 1;
    loadGlyph();
}

void TraceGame::nextGlyph() {
    glyphIndex_ = (glyphIndex_ == getSetLastIndex())
        ? getSetFirstIndex()
        : glyphIndex_ + 1;
    loadGlyph();
}

void TraceGame::resampleWaypoints() {
    const Glyph& g = TRACE_GLYPHS[glyphIndex_];
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

        strokeLen_[s] = totalPts - strokeStart_[s];
    }
    strokeCount_ = g.strokeCount;
}

int16_t TraceGame::scaleX(int16_t nx) const {
    return DRAW_X + (int16_t)((int32_t)nx * DRAW_W / COORD_MAX);
}

int16_t TraceGame::scaleY(int16_t ny) const {
    return DRAW_Y + (int16_t)((int32_t)ny * DRAW_H / COORD_MAX);
}

void TraceGame::updatePulsePhase() {
    uint32_t now = millis();
    if (now - lastPulseChange_ >= PULSE_PERIOD_MS / 2) {
        pulseState_ = !pulseState_;
        lastPulseChange_ = now;
        markDirty();
    }
}

void TraceGame::update(AppContext& host, const TouchPoint& touch) {
    if (touch.justPressed) {
        if (MODE_ABC.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphSet_ = GlyphSet::Upper;
            glyphIndex_ = UPPER_FIRST;
            loadGlyph();
            return;
        }
        if (MODE_abc.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphSet_ = GlyphSet::Lower;
            glyphIndex_ = LOWER_FIRST;
            loadGlyph();
            return;
        }
        if (MODE_123.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphSet_ = GlyphSet::Digit;
            glyphIndex_ = DIGIT_FIRST;
            loadGlyph();
            return;
        }
        if (RETRY_BTN.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            loadGlyph();
            return;
        }
        if (TOP_NEXT_BTN.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            nextGlyph();
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
        uint8_t startIdx = strokeStart_[activeStroke_];
        uint8_t nextPtIdx = startIdx + nextPoint_;

        if (nextPtIdx < MAX_POINTS && nextPoint_ < strokeLen_[activeStroke_]) {
            int16_t dx = touch.x - pts_[nextPtIdx].x;
            int16_t dy = touch.y - pts_[nextPtIdx].y;
            int32_t d2 = (int32_t)dx*dx + (int32_t)dy*dy;

            if (d2 < (int32_t)HIT_RADIUS * HIT_RADIUS) {
                nextPoint_++;
                markDirty();

                if (nextPoint_ >= strokeLen_[activeStroke_]) {
                    host.beepOk();
                    activeStroke_++;
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

void TraceGame::drawModeTabs(Ui::Renderer& tft) {
    const uint16_t activeBg = Ui::warning();
    const uint16_t activeTxt = Ui::panel();
    const uint16_t inactiveBg = Ui::panel();
    const uint16_t inactiveTxt = Ui::text();

    auto drawTab = [&](Rect r, const char* label, bool active) {
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, active ? activeBg : inactiveBg);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
        tft.setTextColor(active ? activeTxt : inactiveTxt, active ? activeBg : inactiveBg);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(label, r.x + r.w/2, r.y + r.h/2, 1);
    };

    drawTab(MODE_ABC, "ABC", glyphSet_ == TraceGame::GlyphSet::Upper);
    drawTab(MODE_abc, "abc", glyphSet_ == TraceGame::GlyphSet::Lower);
    drawTab(MODE_123, "123", glyphSet_ == TraceGame::GlyphSet::Digit);
    drawTab(RETRY_BTN, "Again", false);
    drawTab(TOP_NEXT_BTN, "Next", false);
}

void TraceGame::drawGuide(Ui::Renderer& tft) {
    // Draw completed strokes in success color
    for (uint8_t s = 0; s < activeStroke_ && s < strokeCount_; ++s) {
        uint8_t start = strokeStart_[s];
        for (uint8_t i = 0; i + 1 < strokeLen_[s] && start + i < MAX_POINTS; ++i) {
            uint8_t idx1 = start + i;
            uint8_t idx2 = start + i + 1;
            tft.drawLine(pts_[idx1].x, pts_[idx1].y, pts_[idx2].x, pts_[idx2].y, Ui::success());
            tft.drawLine(pts_[idx1].x+1, pts_[idx1].y, pts_[idx2].x+1, pts_[idx2].y, Ui::success());
        }
    }

    // Draw active stroke
    if (activeStroke_ < strokeCount_) {
        uint8_t start = strokeStart_[activeStroke_];
        uint16_t inked = min(nextPoint_, (uint8_t)strokeLen_[activeStroke_]);

        // Draw inked portion
        for (uint8_t i = 0; i + 1 < inked && start + i < MAX_POINTS; ++i) {
            uint8_t idx1 = start + i;
            uint8_t idx2 = start + i + 1;
            tft.drawLine(pts_[idx1].x, pts_[idx1].y, pts_[idx2].x, pts_[idx2].y, Ui::success());
            tft.drawLine(pts_[idx1].x+1, pts_[idx1].y, pts_[idx2].x+1, pts_[idx2].y, Ui::success());
        }

        // Draw all waypoints
        for (uint8_t i = 0; i < strokeLen_[activeStroke_] && start + i < MAX_POINTS; ++i) {
            uint8_t idx = start + i;
            uint16_t color;

            if (i < inked) {
                // Already claimed
                color = Ui::success();
            } else if (i == inked && pulseState_) {
                // Next dot, pulsing
                tft.fillCircle(pts_[idx].x, pts_[idx].y, 6, Ui::warning());
                continue;
            } else if (i == inked) {
                // Next dot, not pulsing
                tft.fillCircle(pts_[idx].x, pts_[idx].y, 4, Ui::warning());
                continue;
            } else {
                // Unclaimed
                color = Ui::muted();
            }

            tft.fillCircle(pts_[idx].x, pts_[idx].y, 3, color);
        }

        // Draw numbered badge on first point
        uint8_t badgeNum = activeStroke_ + 1;
        tft.fillCircle(pts_[start].x, pts_[start].y, 7, Ui::warning());
        tft.setTextColor(Ui::panel(), Ui::warning());
        tft.setTextDatum(MC_DATUM);
        char badge[2];
        badge[0] = '0' + badgeNum;
        badge[1] = 0;
        tft.drawString(badge, pts_[start].x, pts_[start].y, 1);
    }

    // Draw unclaimed strokes as faint dots
    for (uint8_t s = activeStroke_ + 1; s < strokeCount_; ++s) {
        uint8_t start = strokeStart_[s];
        for (uint8_t i = 0; i < strokeLen_[s] && start + i < MAX_POINTS; ++i) {
            tft.fillCircle(pts_[start + i].x, pts_[start + i].y, 2, Ui::muted());
        }
    }
}

void TraceGame::drawProgress(Ui::Renderer& tft) {
    uint16_t totalPoints = 0;
    uint16_t claimedPoints = 0;

    for (uint8_t s = 0; s < strokeCount_; ++s) {
        totalPoints += strokeLen_[s];
        if (s < activeStroke_) {
            claimedPoints += strokeLen_[s];
        } else if (s == activeStroke_) {
            claimedPoints += nextPoint_;
        }
    }

    uint8_t pct = totalPoints > 0 ? (uint8_t)((uint32_t)claimedPoints * 100 / totalPoints) : 0;
    int16_t barW = 120;
    int16_t barX = (SCREEN_WIDTH - barW) / 2;
    int16_t barY = 222;

    tft.fillRoundRect(barX, barY, barW, 10, 4, Ui::panel());
    tft.drawRoundRect(barX, barY, barW, 10, 4, Ui::outline());

    int16_t fillW = (int16_t)((int32_t)barW * pct / 100);
    if (fillW > 0) {
        tft.fillRoundRect(barX, barY, fillW, 10, 4, Ui::success());
    }
}

void TraceGame::drawCompleteStatus(Ui::Renderer& tft) {
    constexpr Rect STATUS{82, 196, 156, 21};
    tft.fillRoundRect(STATUS.x, STATUS.y, STATUS.w, STATUS.h, 6, Ui::success());
    tft.drawRoundRect(STATUS.x, STATUS.y, STATUS.w, STATUS.h, 6, Ui::outline());
    tft.setTextColor(TFT_BLACK, Ui::success());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Great job", STATUS.x + STATUS.w / 2,
                   STATUS.y + STATUS.h / 2, 2);
}

void TraceGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Glyph& g = TRACE_GLYPHS[glyphIndex_];

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());
        drawModeTabs(tft);

        Ui::drawPagerButton(tft, PREV_BTN, "Prev", true);
        Ui::drawPagerButton(tft, NEXT_BTN, "Next", true);
    } else {
        tft.fillRect(76, 194, 168, 44, Ui::bg());
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
