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

#include "TimeCourse.h"

struct AppMetadata;

const AppMetadata& timeGameAppMetadata();

/* A course in reading the clock: what each level asks, and the wrong
 * options each question offers, are in TimeCourse.h; this is the screen. */
class TimeGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. The clock face belongs to the question and is static. Note
     * this screen cannot do the two-button trick the others do -- its prompt
     * overlaps the lower buttons. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;

private:
    Rect answerRect(uint8_t index) const;
    void startLevel();
    void newQuestion();
    void drawClock(Ui::Renderer& tft) const;
    void drawPrompt(Ui::Renderer& tft) const;

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    /* Levels 6 and 9 are too many to number, and draw at random. */
    RecentQuestions recent_;
    TimeCourse::Question question_;
    /* Which of the level's items is on screen, for deck_.missed(). */
    uint16_t item_ = 0;
    /* The level the question on screen was dealt from: the ladder may have
     * moved since, on the answer that is still showing. */
    uint8_t questionLevel_ = 1;
    /* The next question is the first of a new level: name it in the strip. */
    bool announce_ = false;
    bool banner_ = false;

    /* What each button shows: 0 unanswered, 1 correct, 2 wrong. */
    uint8_t drawnButton_[4] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnStreak_ = 0xFFFF;
    bool drawnAnswered_ = false;
    bool drawnHeader_ = false;
    uint8_t drawnLevel_ = 0;
    /* The question line changes with the level -- "20 minutes later?" */
    bool drawnPrompt_ = false;
    /* The clock face is dynamic, not static: a new question moves the hands,
     * and a new question must not cost a full repaint. See renderDynamic(). */
    bool drawnClock_ = false;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
