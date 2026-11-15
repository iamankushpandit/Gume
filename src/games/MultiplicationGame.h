// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Arduino.h's bit() macro would rewrite the deck's own bit() helper: set it
 * aside while Course.h is read (MathGame.h says more). */
#pragma push_macro("bit")
#undef bit
#include "engine/Course.h"
#pragma pop_macro("bit")
#include "engine/Game.h"
#include "MultiplicationCourse.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& multiplicationAppMetadata();

/* The times tables in the order they are taught, then the missing number,
 * then division as the same facts read backwards. What each level asks, and
 * the options each question offers, are in MultiplicationCourse.h; this is
 * the screen and the player's place on the ladder. */
class MultiplicationGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. Same shape as MathGame, which is the reference: only the top
     * bar is static, the equation panel is drawn once per question, and
     * answering recolours at most two of the four buttons. See
     * docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /* Endless, like MathGame: worth coming back to once something is right.
     * The level is not the reason -- that is stored for the player and comes
     * back with New game too -- the score and the streak of this visit are.
     * Nothing here runs on the clock, so there is nothing to resume. */
    Keep keepsGame() const override;

private:
    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void drawEquation(Ui::Renderer& tft) const;
    void updateBest(AppContext& host);

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    MultiplicationCourse::Question question_;
    /* Which of the level's items is on screen, for deck_.missed(). */
    uint16_t item_ = 0;
    /* The level the question on screen was dealt from. The ladder moves as
     * the answer lands, but the header and the banner follow the next
     * question, so the two never disagree about where the player is. */
    uint8_t questionLevel_ = 1;
    /* Set by a climb: the next question's prompt line names the new level. */
    bool announce_ = false;

    /* What each button shows: 0 unanswered, 1 correct, 2 wrong. */
    uint8_t drawnButton_[4] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnStreak_ = 0xFFFF;
    uint8_t drawnLevel_ = 0;
    bool drawnAnswered_ = false;
    bool drawnPrompt_ = false;
    bool drawnHeader_ = false;
    /* The product panel is dynamic, not static: a new question changes it, and
     * a new question must not cost a full repaint. See renderDynamic(). */
    bool drawnQuestion_ = false;
    bool banner_ = false;           // the prompt line is showing the level
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
