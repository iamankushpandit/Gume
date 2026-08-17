#include "TraceGame.h"
#include "TraceGlyphData.h"
#include "engine/AppRegistry.h"
#include "ui/GameLayout.h"

namespace {

/* The glyph box was authored 160x132; that ratio is kept when the box is
 * resized so the letters keep their proportions. */
constexpr int16_t BOX_ASPECT_W = 160;
constexpr int16_t BOX_ASPECT_H = 132;
constexpr int16_t COORD_MAX = 200;
constexpr int16_t WAYPOINT_SPACING = 20;
constexpr int16_t HIT_RADIUS = 16;
constexpr uint32_t PULSE_PERIOD_MS = 500;

constexpr int16_t TAB_ROW_Y = 32;
constexpr int16_t TAB_ROW_H = 22;
constexpr int16_t MODE_TAB_COUNT = 5;   // ABC abc 123 Again Next
constexpr int16_t PAGER_W = 60;
constexpr int16_t PAGER_H = 30;
constexpr int16_t BAR_H = 10;
constexpr int16_t STATUS_H = 21;

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
    glyphIndex_ = 0;
    glyphSet_ = GlyphSet::Upper;
    loadGlyph(drawBox(Ui::frame(host.display())));
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

Rect TraceGame::tabRow(const Ui::Frame& f) const {
    return Rect{8, TAB_ROW_Y, static_cast<int16_t>(f.w - 16), TAB_ROW_H};
}

/* Five equal chips across the row rather than the authored 38/92/146/204/260,
 * which ran to x=312 and fell off a 240px panel. */
Rect TraceGame::modeTabRect(const Ui::Frame& f, uint8_t index) const {
    return Ui::gridCell(tabRow(f), MODE_TAB_COUNT, 1, index, 4);
}

Rect TraceGame::pagerRow(const Ui::Frame& f) const {
    return Rect{8, static_cast<int16_t>(f.h - 38), static_cast<int16_t>(f.w - 16), PAGER_H};
}
Rect TraceGame::prevRect(const Ui::Frame& f) const {
    const Rect p = pagerRow(f);
    return Rect{p.x, p.y, PAGER_W, p.h};
}
Rect TraceGame::nextRect(const Ui::Frame& f) const {
    const Rect p = pagerRow(f);
    return Rect{static_cast<int16_t>(p.x + p.w - PAGER_W), p.y, PAGER_W, p.h};
}

/* The bar sits between the two pagers, so its width is what is left there. */
Rect TraceGame::progressRect(const Ui::Frame& f) const {
    int16_t w = static_cast<int16_t>(f.w - 2 * (PAGER_W + 16));
    if (w > 120) w = 120;
    return Rect{static_cast<int16_t>(f.cx() - w / 2),
                static_cast<int16_t>(f.h - 18), w, BAR_H};
}

Rect TraceGame::statusRect(const Ui::Frame& f) const {
    int16_t w = static_cast<int16_t>(f.w - 40);
    if (w > 156) w = 156;
    return Rect{static_cast<int16_t>(f.cx() - w / 2),
                static_cast<int16_t>(progressRect(f).y - 26), w, STATUS_H};
}

Rect TraceGame::drawBox(const Ui::Frame& f) const {
    const Rect tabs = tabRow(f);
    const int16_t top = static_cast<int16_t>(tabs.y + tabs.h + 4);
    const int16_t bottom = static_cast<int16_t>(statusRect(f).y - 6);
    int16_t h = static_cast<int16_t>(bottom - top);
    if (h < 40) h = 40;
    int16_t w = static_cast<int16_t>((static_cast<int32_t>(h) * BOX_ASPECT_W) / BOX_ASPECT_H);
    const int16_t maxW = static_cast<int16_t>(f.w - 20);
    if (w > maxW) {
        w = maxW;
        h = static_cast<int16_t>((static_cast<int32_t>(w) * BOX_ASPECT_H) / BOX_ASPECT_W);
    }
    return Rect{static_cast<int16_t>(f.cx() - w / 2), top, w, h};
}

void TraceGame::loadGlyph(const Rect& box) {
    glyphIndex_ = constrain(glyphIndex_, getSetFirstIndex(), getSetLastIndex());
    complete_ = false;
    completeAt_ = 0;
    activeStroke_ = 0;
    nextPoint_ = 0;
    lastPulseChange_ = millis();
    pulseState_ = false;
    resampleWaypoints(box);
    markFullDirty();
}

void TraceGame::previousGlyph(const Rect& box) {
    glyphIndex_ = (glyphIndex_ == getSetFirstIndex())
        ? getSetLastIndex()
        : glyphIndex_ - 1;
    loadGlyph(box);
}

void TraceGame::nextGlyph(const Rect& box) {
    glyphIndex_ = (glyphIndex_ == getSetLastIndex())
        ? getSetFirstIndex()
        : glyphIndex_ + 1;
    loadGlyph(box);
}

void TraceGame::resampleWaypoints(const Rect& box) {
    const Glyph& g = TRACE_GLYPHS[glyphIndex_];
    uint8_t totalPts = 0;
    box_ = box;

    for (uint8_t s = 0; s < g.strokeCount && s < MAX_STROKES; ++s) {
        const Stroke& st = g.strokes[s];
        strokeStart_[s] = totalPts;
        if (st.count == 0 || totalPts >= MAX_POINTS) {
            strokeLen_[s] = 0;
            continue;
        }

        pts_[totalPts].x = scaleX(box, st.pts[0]);
        pts_[totalPts].y = scaleY(box, st.pts[1]);
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
                pts_[totalPts].x = scaleX(box, static_cast<int16_t>(x1 + dx * ratio));
                pts_[totalPts].y = scaleY(box, static_cast<int16_t>(y1 + dy * ratio));
                ++totalPts;
                distanceToNext = WAYPOINT_SPACING;
            }
            distanceToNext -= (segLen - walked);
        }

        const int16_t endX = scaleX(box, st.pts[(st.count - 1) * 2]);
        const int16_t endY = scaleY(box, st.pts[(st.count - 1) * 2 + 1]);
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

int16_t TraceGame::scaleX(const Rect& box, int16_t nx) const {
    return box.x + (int16_t)((int32_t)nx * box.w / COORD_MAX);
}

int16_t TraceGame::scaleY(const Rect& box, int16_t ny) const {
    return box.y + (int16_t)((int32_t)ny * box.h / COORD_MAX);
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
    const Ui::Frame f = Ui::frame(host.display());
    const Rect box = drawBox(f);

    if (touch.justPressed) {
        if (modeTabRect(f, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphSet_ = GlyphSet::Upper;
            glyphIndex_ = UPPER_FIRST;
            loadGlyph(box);
            return;
        }
        if (modeTabRect(f, 1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphSet_ = GlyphSet::Lower;
            glyphIndex_ = LOWER_FIRST;
            loadGlyph(box);
            return;
        }
        if (modeTabRect(f, 2).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphSet_ = GlyphSet::Digit;
            glyphIndex_ = DIGIT_FIRST;
            loadGlyph(box);
            return;
        }
        if (modeTabRect(f, 3).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            loadGlyph(box);
            return;
        }
        if (modeTabRect(f, 4).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            nextGlyph(box);
            return;
        }
        if (prevRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            previousGlyph(box);
            return;
        }
        if (nextRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            nextGlyph(box);
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

void TraceGame::drawModeTabs(Ui::Renderer& tft, const Ui::Frame& f) {
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

    drawTab(modeTabRect(f, 0), "ABC", glyphSet_ == TraceGame::GlyphSet::Upper);
    drawTab(modeTabRect(f, 1), "abc", glyphSet_ == TraceGame::GlyphSet::Lower);
    drawTab(modeTabRect(f, 2), "123", glyphSet_ == TraceGame::GlyphSet::Digit);
    drawTab(modeTabRect(f, 3), "Again", false);
    drawTab(modeTabRect(f, 4), "Next", false);
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

void TraceGame::drawProgress(Ui::Renderer& tft, const Ui::Frame& f) {
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
    const Rect bar = progressRect(f);

    tft.fillRoundRect(bar.x, bar.y, bar.w, bar.h, 4, Ui::panel());
    tft.drawRoundRect(bar.x, bar.y, bar.w, bar.h, 4, Ui::outline());

    int16_t fillW = (int16_t)((int32_t)bar.w * pct / 100);
    if (fillW > 0) {
        tft.fillRoundRect(bar.x, bar.y, fillW, bar.h, 4, Ui::success());
    }
}

void TraceGame::drawCompleteStatus(Ui::Renderer& tft, const Ui::Frame& f) {
    const Rect STATUS = statusRect(f);
    tft.fillRoundRect(STATUS.x, STATUS.y, STATUS.w, STATUS.h, 6, Ui::success());
    tft.drawRoundRect(STATUS.x, STATUS.y, STATUS.w, STATUS.h, 6, Ui::outline());
    tft.setTextColor(TFT_BLACK, Ui::success());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Great job", STATUS.x + STATUS.w / 2,
                   STATUS.y + STATUS.h / 2, 2);
}

void TraceGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    const Rect box = drawBox(f);
    const Glyph& g = TRACE_GLYPHS[glyphIndex_];

    /* The waypoints are baked into panel pixels. If the panel changed shape
     * under us, rebuild them against the new box -- the count is the same, so
     * how far the child had got survives the rotation. */
    if (box.x != box_.x || box.y != box_.y || box.w != box_.w || box.h != box_.h) {
        resampleWaypoints(box);
        markFullDirty();
    }

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());
        drawModeTabs(tft, f);

        Ui::drawPagerButton(tft, prevRect(f), "Prev", true);
        Ui::drawPagerButton(tft, nextRect(f), "Next", true);
    } else {
        /* Clear only the status/progress strip between the two pagers, so the
         * pagers themselves survive a partial repaint. */
        const Rect status = statusRect(f);
        const Rect prev = prevRect(f);
        const int16_t stripX = static_cast<int16_t>(prev.x + prev.w + 8);
        tft.fillRect(stripX, static_cast<int16_t>(status.y - 2),
                     static_cast<int16_t>(nextRect(f).x - stripX - 8),
                     static_cast<int16_t>(f.h - status.y), Ui::bg());
    }

    tft.fillRect(box.x - 2, box.y - 2, box.w + 4, box.h + 4, Ui::bg());
    tft.setTextColor(Ui::panel(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    char label[2] = {g.label, 0};
    tft.drawString(label, box.x + box.w / 2, box.y + box.h / 2, 1);

    drawGuide(tft);
    drawProgress(tft, f);

    if (complete_) {
        drawCompleteStatus(tft, f);
    }

    tft.setTextDatum(TL_DATUM);
}
