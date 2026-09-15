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
#include "engine/RecentQuestions.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& spaceAppMetadata();

/* A quiz about the solar system and the layers of air above us.
 *
 * It is a TEACHING screen before it is a scoring one: every answer, right or
 * wrong, is followed by one plain sentence saying why. A young player who guesses
 * Mercury for "which planet is hottest" and is told only that they were wrong
 * has learned nothing; being told that Venus traps its heat under thick cloud
 * is the whole point of the screen.
 *
 * The layers of the atmosphere are in here with the planets deliberately.
 * "Where does space start?" is the same question as "how far up does the air
 * go", and a quiz that answers one and not the other leaves the join out. */
class SpaceGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase, like Math. renderStatic() is the clear and the top bar and
     * nothing else -- the question panel is DYNAMIC, because a new question
     * must not cost a 320x240 wipe. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    Rect answerRect(uint8_t index) const;
    Rect questionRect() const;
    Rect noteRect() const;
    void newQuestion();
    uint8_t level() const;
    void drawQuestion(Ui::Renderer& tft);
    void drawNote(Ui::Renderer& tft);

    RecentQuestions recent_;

    uint16_t question_ = 0;
    /* Which table option each button shows. The correct answer is always
     * options[0] in the table, so this is what stops it being the same button
     * every time. */
    uint8_t order_[4] = {0, 1, 2, 3};
    uint8_t correctButton_ = 0;

    /* Painted state, so a repaint can skip what has not moved. 0 unanswered,
     * 1 correct, 2 wrong -- answering changes at most two of the four. */
    uint8_t drawnButton_[4] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnStreak_ = 0xFFFF;
    bool drawnHeader_ = false;
    bool drawnQuestion_ = false;
    /* The strip under the panel carries two different things: one line of
     * prompt before an answer, up to two lines of fact after one. Tracked as
     * a state rather than a bool so the first paint cannot be skipped. */
    uint8_t drawnNote_ = 0xFF;

    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t best_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
