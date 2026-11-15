// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "CountingCourse.h"
#include "engine/Course.h"
#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& countingAppMetadata();

class CountingGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. The objects to be counted belong to the question, so the
     * panel and every dot in it are drawn once per question; answering
     * recolours at most two of the four buttons. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /* Endless: worth coming back to once something is right. The level is
     * kept for the player whatever happens, so this is only about the score
     * of the visit. Nothing here runs on the clock, so there is nothing to
     * resume. */
    Keep keepsGame() const override;

private:
    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void drawPrompt(Ui::Renderer& tft) const;

    /* The player's place in the course, kept between visits, and the deck
     * that deals every question of that level before any comes back. */
    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    /* The question on screen, scattered arrangement included -- chosen once,
     * so a repaint after the saver or Carry on shows the same picture. */
    CountingCourse::Question q_;
    uint16_t item_ = 0;
    /* The last answer moved the player up: the next question's prompt line
     * names the level they reached. */
    bool announce_ = false;
    bool showLevel_ = false;

    /* What each button shows: 0 unanswered, 1 correct, 2 wrong. */
    uint8_t drawnButton_[4] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnStreak_ = 0xFFFF;
    uint16_t drawnRounds_ = 0xFFFF;
    /* The strip under the panel: see renderDynamic(). */
    uint8_t drawnStrip_ = 0xFF;
    bool drawnStats_ = false;
    bool drawnPrompt_ = false;
    /* The object panel is dynamic, not static: a new question changes what
     * is in it, and a new question must not cost a full repaint. */
    bool drawnObjects_ = false;
    /* Sixteen bits, not eight: with the game kept to carry on, a visit can
     * run past 255 questions, and "Score 255/255" must not turn into 0/0. */
    uint16_t score_ = 0;
    uint16_t rounds_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
