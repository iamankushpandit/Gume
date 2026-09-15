// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& mathAppMetadata();

class MathGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. The equation panel belongs to the question, so it is static:
     * answering recolours at most two of the four buttons and rewrites one
     * line, and the sum above them does not move. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    enum class Operation : uint8_t {
        Add,
        Subtract
    };

    Rect answerRect(uint8_t index) const;
    void newQuestion();
    void makeOptions();
    void makeWordProblem();
    uint8_t level() const;
    bool optionExists(int16_t value, uint8_t upTo) const;
    uint16_t elapsedSeconds() const;
    void formatSeconds(uint16_t seconds, char* out, size_t len) const;
    void updateBest(AppContext& host);
    void drawQuestion(Ui::Renderer& tft);

    int16_t left_ = 0;
    int16_t right_ = 0;
    int16_t answer_ = 0;
    int16_t options_[4] = {};

    /* The same sum, in words.
     *
     * A young player who can answer "12 - 7 = ?" often cannot answer "Nia has 12
     * shells and gives 7 away", and the second is the one that turns up in
     * school and in life. So from level 2 some questions are dressed as a
     * story: the arithmetic underneath is unchanged and so are the four
     * buttons, which is what keeps this a variation on the screen rather than
     * a second screen.
     *
     * A fixed buffer rather than a String, and filled once per question: the
     * memory rule in the root CLAUDE.md is about small allocations repeated
     * for hours, and a quiz question every few seconds is exactly that shape.
     * 128 is the longest template with three-digit numbers and the longest
     * name and noun in the tables, plus room. */
    char problem_[128] = {};
    bool wordProblem_ = false;

    /* What each button currently shows: 0 unanswered, 1 correct, 2 wrong.
     * Answering changes at most TWO of the four -- the right one turns green
     * and the chosen one turns red -- and only ONE when the player was right,
     * because then they are the same button. */
    uint8_t drawnButton_[4] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnStreak_ = 0xFFFF;
    bool drawnAnswered_ = false;
    bool drawnHeader_ = false;
    /* The equation panel is dynamic, not static: a new question changes it,
     * and a new question must not cost a full repaint. See renderDynamic(). */
    bool drawnQuestion_ = false;
    uint8_t correctButton_ = 0;
    Operation operation_ = Operation::Add;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestCorrect_ = 0;
    uint16_t bestSeconds_ = 0;
    uint32_t startedAt_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
