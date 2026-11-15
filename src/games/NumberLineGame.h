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

#include "NumberLineCourse.h"

struct AppMetadata;

const AppMetadata& numberLineAppMetadata();

/* A course in the number line: what each level asks, and the wrong options,
 * are in NumberLineCourse.h; this is the screen. */
class NumberLineGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase render, and within renderDynamic() four parts, each repainted
     * only when it changed: the header, the question line, the line with
     * its frog and hops, and the buttons. A hop repaints the line's strip
     * and nothing else. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    /* Watch: level 1 only, the frog hops before the question.
     * Ask: the buttons are up and the frog is at the start.
     * Show: answered; the frog hops to check it.
     * Feedback: the answer stays a moment, then the next question. */
    enum class Phase : uint8_t { Watch, Ask, Show, Feedback };

    Rect answerRect(uint8_t i) const;
    int16_t numToX(int16_t n) const;
    /* Where the frog stands after `step` hops of the question. */
    int16_t positionAfter(uint8_t step) const;
    /* How many hops the animation takes: two through ten, else the count. */
    uint8_t animSteps() const;
    void startLevel();
    void newQuestion();
    void startHops(uint32_t now);
    void drawLine(Ui::Renderer& tft) const;

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    NumberLineCourse::Question q_;
    uint16_t item_ = 0;
    uint8_t questionLevel_ = 1;
    bool announce_ = false;
    bool banner_ = false;

    Phase    phase_    = Phase::Ask;
    uint8_t  animStep_ = 0;
    int8_t   selected_ = -1;
    bool     lastCorrect_ = false;
    uint32_t nextAt_   = 0;
    uint32_t feedbackUntil_ = 0;
    uint16_t score_    = 0;
    uint16_t rounds_   = 0;

    /* What is on the panel, so each part repaints only when it changed. */
    bool    drawnHeader_ = false;
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnRounds_ = 0xFFFF;
    uint8_t drawnLevel_ = 0;
    bool    drawnQuestion_ = false;
    bool    drawnSolved_ = false;
    bool    drawnLine_ = false;
    uint8_t drawnStep_ = 0xFF;
    Phase   drawnPhase_ = Phase::Feedback;
    /* Buttons: 0xFF hidden, 0 waiting, 1 shown with the answer marked. */
    uint8_t drawnButtons_ = 0xFE;
};
