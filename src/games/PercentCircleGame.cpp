// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "PercentCircleGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t PERCENT_FILL = 0xFCA8;     // Ui::rgb(255, 202, 84) - golden/orange
constexpr uint16_t PERCENT_EMPTY = 0x39EF;    // similar to surface
constexpr uint16_t PERCENT_OUTLINE = 0x2478;  // Ui::rgb(36, 132, 204) - blue
constexpr int16_t CIRCLE_CX = 96;
constexpr int16_t CIRCLE_CY = 130;
constexpr int16_t CIRCLE_RADIUS = 62;
constexpr uint16_t YELLOW = 0xFEC0;
namespace PC = PercentCourse;
/* Under the buttons and the shading controls, right of the circle: how the
 * answer went, or a new level's name. It used to be a card drawn over the
 * circle and the buttons, which left pieces behind. */
constexpr Rect NOTE_RECT = {162, 190, 158, 30};

/* A selection that is not one of the four option buttons: the shading
 * round's OK. */
constexpr int8_t SUBMIT = 4;

constexpr AppScoreInfo PERCENT_SCORE = {
    "percent", "Percent", "pctBest", "pts", false
};

constexpr AppMetadata PERCENT_METADATA = {
    "percent",
    "Percent",
    nullptr,
    "circle parts",
    "Percent",
    "Percentages on a circle.",
    &PERCENT_SCORE,
    LauncherIcon::Percent,
    27,
    true,
};
}

const AppMetadata& percentCircleAppMetadata() {
    return PERCENT_METADATA;
}

const char* PercentCircleGame::title() const {
    return percentCircleAppMetadata().screenTitle != nullptr
        ? percentCircleAppMetadata().screenTitle
        : percentCircleAppMetadata().title;
}

/* A new visit, or New game from the Carry on card: the tally starts over,
 * the level does not. It used to follow the streak, so one wrong tap fell to
 * level 1, and every visit began there. */
void PercentCircleGame::begin(AppContext& host) {
    score_ = 0;
    rounds_ = 0;
    streak_ = 0;
    ladder_.start(PC::LEVEL_COUNT, host.courseLevel());
    startLevel();
    announce_ = false;
    newRound();
    markFullDirty();
}

/* One answered question is worth coming back to. Nothing here runs on a
 * clock. */
Game::Keep PercentCircleGame::keepsGame() const {
    return rounds_ > 0 ? Keep::Ask : Keep::No;
}

void PercentCircleGame::startLevel() {
    deck_.reset(PC::poolSize(ladder_.level()));
}

void PercentCircleGame::newRound() {
    item_ = deck_.next(esp_random());
    PC::make(ladder_.level(), item_, esp_random(), q_);
    banner_ = announce_;
    announce_ = false;
    selected_ = -1;
    roundComplete_ = false;
    currentPercent_ = 0;
}

/* One try: the right answer is shown either way, where wrong taps used to
 * be retried until right and still score. */
void PercentCircleGame::answer(AppContext& host, bool right, int8_t pressed) {
    selected_ = pressed;
    roundComplete_ = true;
    ++rounds_;
    if (right) {
        ++score_;
        ++streak_;
        host.saveBestScore(percentCircleAppMetadata().score->bestKey, score_, false);
    } else {
        streak_ = 0;
        deck_.missed(item_);
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
}

void PercentCircleGame::fillSlice(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const {
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

void PercentCircleGame::drawCircle(Ui::Renderer& tft, uint8_t percent, bool highlight) const {
    tft.fillCircle(CIRCLE_CX, CIRCLE_CY, CIRCLE_RADIUS + 3, highlight ? Ui::warning() : Ui::surface());
    tft.fillCircle(CIRCLE_CX, CIRCLE_CY, CIRCLE_RADIUS, PERCENT_EMPTY);

    if (percent > 0) {
        const float fillAngle = (percent / 100.0f) * TWO_PI;
        fillSlice(tft, CIRCLE_CX, CIRCLE_CY, CIRCLE_RADIUS, -PI / 2.0f, -PI / 2.0f + fillAngle, PERCENT_FILL);
    }

    tft.drawCircle(CIRCLE_CX, CIRCLE_CY, CIRCLE_RADIUS, PERCENT_OUTLINE);

    for (uint8_t i = 0; i < 10; ++i) {
        const float angle = -PI / 2.0f + i * TWO_PI / 10.0f;
        const int16_t x = CIRCLE_CX + static_cast<int16_t>(cosf(angle) * CIRCLE_RADIUS);
        const int16_t y = CIRCLE_CY + static_cast<int16_t>(sinf(angle) * CIRCLE_RADIUS);
        tft.drawLine(CIRCLE_CX, CIRCLE_CY, x, y, Ui::rgb(100, 150, 200));
    }
}

void PercentCircleGame::drawOptions(Ui::Renderer& tft) const {
    char label[12];
    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t fill = Ui::panel();
        uint16_t text = Ui::text();
        uint16_t outline = Ui::outline();
        if (roundComplete_ && i == q_.correct) {
            fill = Ui::success();
            text = Ui::onFill(fill);
            outline = Ui::success();
        } else if (roundComplete_ && i == selected_) {
            fill = Ui::error();
            text = Ui::onFill(fill);
            outline = Ui::error();
        }
        PC::optionText(q_, q_.options[i], label, sizeof(label));
        Ui::drawButton(tft, optionRect(i), label, fill, outline, text, false, 2);
    }
}

/* - the box + and OK. From Fives on the box says "?": the circle is what is
 * read, not the number beside it. */
void PercentCircleGame::drawShadeControls(Ui::Renderer& tft) const {
    tft.fillRoundRect(200, 152, 38, 34, 4, Ui::panel());
    tft.drawRoundRect(200, 152, 38, 34, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    char valueStr[8];
    if (PC::showShadedValue(q_.level) || roundComplete_) {
        snprintf(valueStr, sizeof(valueStr), "%u%%", currentPercent_);
    } else {
        snprintf(valueStr, sizeof(valueStr), "?");
    }
    tft.drawString(valueStr, 219, 169, 1);
    Ui::drawPagerButton(tft, minusRect(), "-", currentPercent_ > 0 && !roundComplete_);
    Ui::drawPagerButton(tft, plusRect(), "+", currentPercent_ < 100 && !roundComplete_);
    /* OK looks the same whatever the circle shows: it changes only once
     * pressed, green or red. */
    uint16_t okFill = Ui::accent();
    if (roundComplete_) okFill = currentPercent_ == q_.answer ? Ui::success() : Ui::error();
    Ui::drawButton(tft, okRect(), "OK", okFill, Ui::outline(), Ui::onFill(okFill), false, 2);
    tft.setTextDatum(TL_DATUM);
}

void PercentCircleGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    if (roundComplete_) {
        /* A new round of another kind paints other regions, so it clears
         * the screen; one of the same kind is content in the same places. */
        const PC::Mode before = q_.mode;
        newRound();
        if (q_.mode != before) {
            markFullDirty();
        } else {
            markDirty();
        }
        return;
    }
    if (q_.mode == PC::Mode::Shade) {
        const uint8_t step = PC::shadeStep(q_.level);
        if (minusRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (currentPercent_ >= step) {
                currentPercent_ = static_cast<uint8_t>(currentPercent_ - step);
                markDirty();
            }
            return;
        }
        if (plusRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (currentPercent_ + step <= 100) {
                currentPercent_ = static_cast<uint8_t>(currentPercent_ + step);
                markDirty();
            }
            return;
        }
        if (okRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            answer(host, currentPercent_ == q_.answer, SUBMIT);
            markDirty();
        }
        return;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        if (optionRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            answer(host, i == q_.correct, static_cast<int8_t>(i));
            markDirty();
            return;
        }
    }
}

void PercentCircleGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
}

void PercentCircleGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    char buf[48];

    /* The question, in font 4 where it fits beside nothing. */
    PC::prompt(q_, buf, sizeof(buf));
    tft.fillRect(0, 48, GAME_CANVAS_WIDTH, 34, Ui::bg());
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(buf, 160, 60, tft.textWidth(buf, 4) <= 300 ? 4 : 2);

    /* The circle: what was asked about -- and for a fraction or "what
     * percent", nothing until answered, then the answer. While shading, what
     * the player has shaded. */
    uint8_t shaded = q_.percent;
    if (q_.mode == PC::Mode::Shade) {
        shaded = currentPercent_;
    } else if (q_.mode == PC::Mode::Fraction || q_.mode == PC::Mode::WhatPercent) {
        shaded = roundComplete_ ? PC::shownPercent(q_) : 0;
    }
    drawCircle(tft, shaded, false);

    if (q_.mode == PC::Mode::Shade) {
        drawShadeControls(tft);
    } else {
        drawOptions(tft);
    }

    tft.fillRect(NOTE_RECT.x, NOTE_RECT.y, NOTE_RECT.w, NOTE_RECT.h, Ui::bg());
    tft.setTextDatum(MC_DATUM);
    const int16_t cx = static_cast<int16_t>(NOTE_RECT.x + NOTE_RECT.w / 2);
    if (roundComplete_) {
        const bool right = q_.mode == PC::Mode::Shade ? currentPercent_ == q_.answer
                                                       : selected_ == static_cast<int8_t>(q_.correct);
        tft.setTextColor(right ? Ui::success() : Ui::error(), Ui::bg());
        if (right) {
            tft.drawString("Correct!", cx, NOTE_RECT.y + 8, 2);
        } else {
            char answerText[12];
            PC::optionText(q_, q_.answer, answerText, sizeof(answerText));
            if (q_.mode == PC::Mode::Shade) snprintf(answerText, sizeof(answerText), "%u%%", q_.answer);
            snprintf(buf, sizeof(buf), "It is %s", answerText);
            tft.drawString(buf, cx, NOTE_RECT.y + 8, 2);
        }
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Tap for the next", cx, NOTE_RECT.y + 24, 1);
    } else if (banner_) {
        PC::levelLine(q_.level, buf, sizeof(buf));
        tft.setTextColor(YELLOW, Ui::bg());
        tft.drawString(buf, cx, NOTE_RECT.y + 14, 1);
    }

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.fillRect(0, 222, GAME_CANVAS_WIDTH, 18, Ui::bg());
    snprintf(buf, sizeof(buf), "Level %u  Score %u  Streak %u", static_cast<unsigned>(q_.level),
             static_cast<unsigned>(score_), static_cast<unsigned>(streak_));
    tft.drawString(buf, 8, 226, 1);
}
