// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "FingersCourse.h"
#include "engine/Course.h"
#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& fingerCountAppMetadata();

/*
 * Finger Counting, as a course (FingersCourse.h).
 *
 * Trains the numbers to ten on two hands in BOTH directions: reading how many
 * fingers are up (or down, or on both hands together) and tapping the number,
 * and raising a number -- or changing the number already up -- and then
 * tapping Done. From level 3 it is Done that answers, so a player tapping
 * fingers without counting can be wrong; on levels 1 and 2 the count is said
 * as it goes and the question ends when it is reached.
 */
class FingerCountGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* renderStatic() is the clear and the top bar; renderDynamic() repaints
     * only what changed, tracked by the drawn* members: a finger tapped
     * repaints its own hand, nothing else. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /* Endless: worth coming back to once a point is scored. */
    Keep keepsGame() const override;

private:
    static constexpr uint8_t FINGER_COUNT = FingersCourse::FINGERS;
    static constexpr uint8_t OPTION_COUNT = 4;

    // Hand geometry (landscape 320x240).
    static constexpr int16_t PALM_TOP    = 152;
    static constexpr int16_t PALM_BOTTOM = 182;
    static constexpr int16_t FINGER_W    = 22;
    static constexpr int16_t FINGER_PITCH = 26;
    static constexpr int16_t LEFT_X0     = 22;
    static constexpr int16_t RIGHT_X0    = 172;

    Rect fingerRect(uint8_t idx) const;
    Rect answerRect(uint8_t i) const;
    Rect doneRect() const;
    int16_t fingerX(uint8_t idx) const;
    int16_t fingerTop(uint8_t idx) const;
    bool isUp(uint8_t idx) const { return ((raised_ >> idx) & 1U) != 0; }
    void drawHand(Ui::Renderer& tft, uint8_t hand) const;
    void drawPrompt(Ui::Renderer& tft) const;
    void drawHint(Ui::Renderer& tft) const;
    void newQuestion();
    void judge(AppContext& host, bool right);
    bool answered() const { return selected_ >= 0; }

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    FingersCourse::Question q_;
    uint16_t item_ = 0;
    uint16_t raised_ = 0;          // the hands as they are now
    int8_t selected_ = -1;         // the button pressed, or 4 for Done; -1 not yet
    bool lastCorrect_ = false;
    bool announce_ = false;
    bool showLevel_ = false;
    uint16_t score_  = 0;
    uint16_t rounds_ = 0;

    bool drawnQuestion_ = false;
    uint16_t drawnRaised_ = 0xFFFF;
    uint8_t drawnHint_ = 0xFF;     // 0xFF: not drawn; otherwise a state number
    uint8_t drawnUp_ = 0xFF;
    uint8_t drawnButton_[OPTION_COUNT + 1] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnRounds_ = 0xFFFF;
    uint8_t drawnLevel_ = 0xFF;
};
