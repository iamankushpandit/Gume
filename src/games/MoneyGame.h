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

#include "MoneyCourse.h"

struct AppMetadata;

const AppMetadata& moneyAppMetadata();

/* Money: a course in counting coins and making change. The levels, the
 * piles and the wrong amounts are in MoneyCourse.h; this is the screen. */
class MoneyGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase render. renderStatic() is the clear and the top bar;
     * renderDynamic() is everything else. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    /* What the line under the header says. */
    enum class Line : uint8_t { Prompt, Right, Wrong, MakeShort, MakeFewer };

    void startLevel();
    void newRound();
    /* The first answer of a round, to the ladder; later ones say nothing. */
    void record(AppContext& host, bool right);

    Rect optionRect(uint8_t index) const;
    Rect trayRect(uint8_t index) const;
    Rect clearRect() const;
    Rect doneRect() const;
    Rect moreRect(uint8_t index) const;

    void drawCoin(Ui::Renderer& tft, int16_t cx, int16_t cy, uint8_t value) const;
    void drawCoinGroup(Ui::Renderer& tft, const Rect& r, const uint8_t* coins, uint8_t count,
                       int16_t rowPitch) const;
    void drawOptions(Ui::Renderer& tft) const;
    void drawMake(Ui::Renderer& tft) const;
    void drawMore(Ui::Renderer& tft) const;

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    RecentQuestions recent_;
    MoneyCourse::Round round_;
    uint16_t item_ = 0;
    bool announce_ = false;
    bool banner_ = false;
    uint8_t makeCoins_[MoneyCourse::MAX_BUILT] = {};
    uint8_t makeCount_ = 0;
    uint16_t built_ = 0;
    uint16_t score_ = 0;
    uint16_t rounds_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool roundComplete_ = false;
    bool recorded_ = false;
    Line line_ = Line::Prompt;
};
