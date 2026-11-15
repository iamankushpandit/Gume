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
#include "ui/Ui.h"

#include "ColorMixCourse.h"

struct AppMetadata;

const AppMetadata& colorMixAppMetadata();

/* A course in mixing colours: what each level asks, and the wrong buttons,
 * are in ColorMixCourse.h; this is the screen. */
class ColorMixGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase render, and within renderDynamic() four parts each repainted
     * only when it changed: the header, the line under it, the two swatches
     * and the buttons. An answer repaints the line and rings two buttons. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    Rect answerRect(uint8_t index) const;
    void startLevel();
    void newQuestion();
    int8_t touchedAnswer(int16_t x, int16_t y) const;

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    ColorMixCourse::Question q_;
    uint16_t item_ = 0;
    uint8_t questionLevel_ = 1;
    bool announce_ = false;
    bool banner_ = false;

    uint16_t score_ = 0;
    uint16_t attempts_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
    uint32_t feedbackUntil_ = 0;

    bool drawnHeader_ = false;
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnAttempts_ = 0xFFFF;
    uint8_t drawnLevel_ = 0;
    bool drawnQuestion_ = false;
    /* The line: 0xFF stale, 0 the prompt or the level, 1 the answer. */
    uint8_t drawnLine_ = 0xFF;
    bool drawnRings_ = false;
};
