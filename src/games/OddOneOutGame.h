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

#include "OddOneCourse.h"

struct AppMetadata;

const AppMetadata& oddOneOutAppMetadata();

/* Odd One Out: a course in seeing a difference. What each level deals, and
 * the guarantee that exactly one card is alone, are in OddOneCourse.h; this
 * is the screen. */
class OddOneOutGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. Within a round the nine cards never change -- a new round is
     * a full repaint -- so what moves is the header and, once answered, a
     * ring round the odd card and round the one tapped. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    Rect itemRect(uint8_t index) const;
    int8_t touchedItem(int16_t x, int16_t y) const;
    void newRound();
    void drawItem(Ui::Renderer& tft, uint8_t index) const;

    Course::Ladder ladder_;
    /* Rounds are generated, too many to number: no grid comes back within
     * ten. */
    RecentQuestions recent_;
    OddOneCourse::Round round_;
    bool announce_ = false;
    bool banner_ = false;
    int8_t selected_ = -1;
    bool answered_ = false;
    uint32_t feedbackUntil_ = 0;
    uint16_t score_ = 0;
    uint16_t rounds_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;

    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnBest_ = 0xFFFF;
    bool drawnRings_ = false;
};
