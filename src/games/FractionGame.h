// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "engine/Course.h"
#include "engine/Game.h"
#include "engine/RecentQuestions.h"
#include "ui/Ui.h"

#include "FractionsCourse.h"

struct AppMetadata;

const AppMetadata& fractionAppMetadata();

/* Fractions: a course in naming, finding, comparing and working with
 * fractions. The levels and the wrong answers are in FractionsCourse.h;
 * this is the screen. */
class FractionGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase render. renderStatic() is the clear and the top bar;
     * renderDynamic() is everything else. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;

private:
    using Frac = FractionsCourse::Frac;

    void startLevel();
    void newRound();

    Rect optionRect(uint8_t index) const;
    Rect pieOptionRect(uint8_t index) const;
    Rect compareRect(uint8_t index) const;

    void drawPie(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, const Frac& fraction, bool selected = false) const;
    void fillSlice(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const;
    void drawTextOptions(Ui::Renderer& tft) const;
    void drawPieOptions(Ui::Renderer& tft) const;
    void drawCompare(Ui::Renderer& tft) const;
    void drawWritten(Ui::Renderer& tft) const;

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    RecentQuestions recent_;
    FractionsCourse::Question q_;
    uint16_t item_ = 0;
    bool announce_ = false;
    bool banner_ = false;
    uint16_t score_ = 0;
    uint16_t rounds_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool roundComplete_ = false;
};
