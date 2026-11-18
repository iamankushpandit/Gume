// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "FractionGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t PIE_EMPTY = 0x39EF;
constexpr uint16_t PIE_FILL = 0xFEC0;
constexpr uint16_t PIE_LINE = 0x0843;
constexpr uint16_t YELLOW = 0xFEC0;
namespace FC = FractionsCourse;

constexpr AppScoreInfo FRACTION_SCORE = {
    "fractions", "Fractions", "fracBest", "pts", false
};

constexpr AppMetadata FRACTION_METADATA = {
    "fractions",
    "Fractions",
    nullptr,
    "pie slices",
    "Fractions",
    "Match the pie chart.",
    &FRACTION_SCORE,
    LauncherIcon::Fractions,
    15,
    true,
};
}

const AppMetadata& fractionAppMetadata() {
    return FRACTION_METADATA;
}

const char* FractionGame::title() const {
    return fractionAppMetadata().screenTitle != nullptr
        ? fractionAppMetadata().screenTitle
        : fractionAppMetadata().title;
}

/* A new visit, or New game from the Carry on card: the tally starts over,
 * the level does not. It used to be the visit's score, and so did the kind of
 * question, so every visit began with halves in the same order. */
void FractionGame::begin(AppContext& host) {
    score_ = 0;
    rounds_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(fractionAppMetadata().score->bestKey, 0));
    ladder_.start(FC::LEVEL_COUNT, host.courseLevel());
    startLevel();
    announce_ = false;
    newRound();
    markDirty();
}

/* One answered question is worth coming back to. Nothing here runs on a
 * clock, so there is no resume() to write. */
Game::Keep FractionGame::keepsGame() const {
    return rounds_ > 0 ? Keep::Ask : Keep::No;
}

void FractionGame::startLevel() {
    deck_.reset(FC::poolSize(ladder_.level()));
    recent_.reset();
}

void FractionGame::newRound() {
    const uint8_t level = ladder_.level();
    if (FC::numbered(level)) {
        item_ = deck_.next(esp_random());
        FC::make(level, item_, esp_random(), q_);
    } else {
        for (uint8_t tries = 0; tries < 20; ++tries) {
            FC::makeDrawn(level, esp_random(), q_);
            if (!recent_.recentlyUsed(FC::token(q_))) break;
        }
        recent_.remember(FC::token(q_));
    }
    banner_ = announce_;
    announce_ = false;
    selected_ = -1;
    roundComplete_ = false;
}

Rect FractionGame::optionRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152), static_cast<int16_t>(164 + row * 34), 132, 30};
}

Rect FractionGame::pieOptionRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(34 + col * 152), static_cast<int16_t>(112 + row * 48), 100, 42};
}

Rect FractionGame::compareRect(uint8_t index) const {
    return index == 0 ? Rect{34, 176, 104, 34} : Rect{182, 176, 104, 34};
}

void FractionGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    if (roundComplete_) {
        newRound();
        markDirty();
        return;
    }
    const bool compare = q_.mode == FC::Mode::Compare;
    const uint8_t buttons = compare ? 2 : 4;
    for (uint8_t i = 0; i < buttons; ++i) {
        const Rect r = compare ? compareRect(i)
                               : (q_.mode == FC::Mode::FindThePie ? pieOptionRect(i) : optionRect(i));
        if (!r.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        /* One try: the right answer is shown either way. Tapping all four
         * used to score, and climb. */
        selected_ = static_cast<int8_t>(i);
        roundComplete_ = true;
        ++rounds_;
        const bool right = i == q_.correct;
        if (right) {
            ++score_;
            ++streak_;
            if (host.saveBestScore(fractionAppMetadata().score->bestKey, streak_, false)) {
                bestStreak_ = streak_;
            }
        } else {
            streak_ = 0;
            if (FC::numbered(q_.level)) deck_.missed(item_);
        }
        const Course::Ladder::Step step = ladder_.answer(right);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
            startLevel();
        }
        if (step == Course::Ladder::Step::Up) {
            announce_ = true;
            host.pulseRgb(0, 255, 40, 450);
            host.playSound(Sound::LevelUp);
        } else if (right) {
            host.beepOk();
        } else {
            host.beepError();
        }
        markDirty();
        return;
    }
}

void FractionGame::fillSlice(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const {
    /* Pre-correct the radius per axis so the wedge lands on the circle that
     * fillCircle actually drew.
     *
     * The renderer may scale x and y by different amounts -- on a 480x320
     * panel the game canvas stretches 1.5 across and 1.333 down. fillCircle
     * takes a single radius and uses the average of the two, so it stays a
     * circle. These wedge points are computed here in canvas space and then
     * scaled independently, so without this they land on an ellipse instead:
     * the fill bulges past the rim at the sides and falls short at the top and
     * bottom, which is what made the arcs look wrong rather than merely
     * mis-sized. Dividing by each axis and multiplying by the average puts
     * both back on the same circle. On a board where the scales are equal --
     * every board today -- avg/sx and avg/sy are 1 and this is the old code. */
    const float avg = tft.imageScale();
    const float sxAxis = tft.imageScaleX();
    const float syAxis = tft.imageScaleY();
    const float rx = (sxAxis > 0.0f) ? radius * avg / sxAxis : static_cast<float>(radius);
    const float ry = (syAxis > 0.0f) ? radius * avg / syAxis : static_cast<float>(radius);

    const float step = PI / 24.0f;
    float angle = startAngle;
    while (angle < endAngle) {
        const float next = min(endAngle, angle + step);
        const int16_t x1 = cx + static_cast<int16_t>(cosf(angle) * rx);
        const int16_t y1 = cy + static_cast<int16_t>(sinf(angle) * ry);
        const int16_t x2 = cx + static_cast<int16_t>(cosf(next) * rx);
        const int16_t y2 = cy + static_cast<int16_t>(sinf(next) * ry);
        tft.fillTriangle(cx, cy, x1, y1, x2, y2, color);
        angle = next;
    }
}

void FractionGame::drawPie(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, const Frac& fraction, bool selected) const {
    tft.fillCircle(cx, cy, radius + 3, selected ? Ui::warning() : Ui::surface());
    tft.fillCircle(cx, cy, radius, PIE_EMPTY);
    for (uint8_t i = 0; i < fraction.n; ++i) {
        const float start = -PI / 2.0f + i * TWO_PI / fraction.d;
        const float end = -PI / 2.0f + (i + 1) * TWO_PI / fraction.d;
        fillSlice(tft, cx, cy, radius, start, end, PIE_FILL);
    }
    tft.drawCircle(cx, cy, radius, PIE_LINE);
    for (uint8_t i = 0; i < fraction.d; ++i) {
        const float angle = -PI / 2.0f + i * TWO_PI / fraction.d;
        const int16_t x = cx + static_cast<int16_t>(cosf(angle) * radius);
        const int16_t y = cy + static_cast<int16_t>(sinf(angle) * radius);
        tft.drawLine(cx, cy, x, y, PIE_LINE);
    }
}

void FractionGame::drawTextOptions(Ui::Renderer& tft) const {
    char label[12];
    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t fill = BLUE;
        uint16_t text = TFT_WHITE;
        if (roundComplete_ && i == q_.correct) {
            fill = GREEN;
            text = TFT_BLACK;
        } else if (roundComplete_ && i == selected_) {
            fill = RED;
            text = TFT_BLACK;
        }
        FC::text(q_.options[i], label, sizeof(label));
        Ui::drawButton(tft, optionRect(i), label, fill, Ui::outline(), text, false, 2);
    }
}

void FractionGame::drawPieOptions(Ui::Renderer& tft) const {
    char label[12];
    FC::text(q_.a, label, sizeof(label));
    tft.fillRoundRect(96, 74, 128, 30, 6, Ui::panel());
    tft.drawRoundRect(96, 74, 128, 30, 6, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, GAME_CANVAS_WIDTH / 2, 89, 4);
    for (uint8_t i = 0; i < 4; ++i) {
        const Rect r = pieOptionRect(i);
        if (roundComplete_ && i == selected_ && i != q_.correct) {
            tft.fillRoundRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, 6, RED);
        }
        drawPie(tft, r.x + r.w / 2, r.y + r.h / 2, 18, q_.options[i], roundComplete_ && i == q_.correct);
    }
}

/* Two fractions, each with its pie -- or on Compare without pies, the
 * fractions alone in large type until answered, and then the pies, as the
 * explanation. */
void FractionGame::drawCompare(Ui::Renderer& tft) const {
    char left[12];
    char right[12];
    FC::text(q_.a, left, sizeof(left));
    FC::text(q_.b, right, sizeof(right));
    const bool pies = !q_.hidePies || roundComplete_;
    tft.setTextDatum(MC_DATUM);
    if (pies) {
        drawPie(tft, 92, 112, 38, q_.a, roundComplete_ && q_.correct == 0);
        drawPie(tft, 228, 112, 38, q_.b, roundComplete_ && q_.correct == 1);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString(left, 92, 160, 2);
        tft.drawString(right, 228, 160, 2);
    } else {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(left, 92, 118, 4);
        tft.drawString(right, 228, 118, 4);
    }
    for (uint8_t i = 0; i < 2; ++i) {
        uint16_t fill = BLUE;
        uint16_t text = TFT_WHITE;
        if (roundComplete_ && i == q_.correct) {
            fill = GREEN;
            text = TFT_BLACK;
        } else if (roundComplete_ && i == selected_) {
            fill = RED;
            text = TFT_BLACK;
        }
        Ui::drawButton(tft, compareRect(i), i == 0 ? "Left" : "Right", fill, Ui::outline(), text, false, 2);
    }
}

/* A question written out -- "3/4 of 12", "3/8 + 2/8", "7/4 = ?" -- and four
 * written answers. */
void FractionGame::drawWritten(Ui::Renderer& tft) const {
    char line[24];
    FC::questionText(q_, line, sizeof(line));
    tft.fillRoundRect(40, 84, 240, 56, 8, Ui::panel());
    tft.drawRoundRect(40, 84, 240, 56, 8, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    const uint8_t font = tft.textWidth(line, 4) <= 220 ? 4 : 2;
    tft.drawString(line, GAME_CANVAS_WIDTH / 2, 112, font);
    if (q_.op >= 3) {
        tft.setTextColor(Ui::muted(), Ui::panel());
        tft.drawString(q_.op == 3 ? "as a mixed number" : "as a fraction", GAME_CANVAS_WIDTH / 2, 132, 1);
    }
    drawTextOptions(tft);
}

void FractionGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
}

void FractionGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    /* The whole body on every change, as before: an answer recolours the
     * buttons and rewrites the line, and a new question changes the rest. */
    tft.fillRect(0, TOP_BAR_HEIGHT, GAME_CANVAS_WIDTH,
                 GAME_CANVAS_HEIGHT - TOP_BAR_HEIGHT, Ui::bg());

    char buf[48];
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(q_.level));
    tft.drawString(buf, 8, 35, 2);
    snprintf(buf, sizeof(buf), "Score %u", static_cast<unsigned>(score_));
    tft.drawString(buf, 8, 51, 1);
    tft.setTextDatum(TR_DATUM);
    snprintf(buf, sizeof(buf), "Streak %u", static_cast<unsigned>(streak_));
    tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 35, 2);
    snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(bestStreak_));
    tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 51, 1);

    /* The line: what to do, a new level's name, or how the answer went --
     * where a panel used to be drawn over the answer it confirmed. */
    uint16_t colour = Ui::muted();
    const char* line = FC::prompt(q_);
    if (roundComplete_) {
        const bool right = selected_ == static_cast<int8_t>(q_.correct);
        colour = right ? GREEN : RED;
        if (right) {
            line = "Correct - tap for the next";
        } else {
            char answer[12];
            FC::text(q_.answer, answer, sizeof(answer));
            snprintf(buf, sizeof(buf), "It is %s - tap for the next", answer);
            line = buf;
        }
    } else if (banner_) {
        FC::levelLine(q_.level, buf, sizeof(buf));
        line = buf;
        colour = YELLOW;
    }
    tft.setTextColor(colour, Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(line, GAME_CANVAS_WIDTH / 2, 70, 1);

    switch (q_.mode) {
        case FC::Mode::NameThePie:
            drawPie(tft, GAME_CANVAS_WIDTH / 2, 120, 38, q_.a);
            drawTextOptions(tft);
            break;
        case FC::Mode::FindThePie:
            drawPieOptions(tft);
            break;
        case FC::Mode::Compare:
            drawCompare(tft);
            break;
        case FC::Mode::Written:
        default:
            drawWritten(tft);
            break;
    }
    tft.setTextDatum(TL_DATUM);
}
