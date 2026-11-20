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

#include "ShapesCourse.h"

struct AppMetadata;

const AppMetadata& shapeColorAppMetadata();

/* Shapes: a course in what a shape is. Each round is four shapes on the left
 * and four targets on the right, and the level decides what a target is --
 * the shape's outline, turned or not, how many sides it has, or its name.
 * The levels, the deal and how a round moves the ladder are in
 * ShapesCourse.h; this is the screen. */
class ShapeColorGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. The instruction line belongs to the round and is drawn with
     * the full repaint a new round makes; a tap repaints the rows it changed. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;

    using Shape = ShapesCourse::Shape;

private:
    Rect choiceRect(uint8_t index) const;
    Rect targetRect(uint8_t index) const;
    Rect statsRect() const;
    void newRound();
    bool allMatched() const;
    /* The round is done: tell the ladder how it went. */
    void finishRound(AppContext& host);
    /** 0 normal, 1 selected, 2 matched -- what a left-hand row is showing. */
    uint8_t choiceState(uint8_t index) const;
    void drawChoice(Ui::Renderer& tft, uint8_t index);
    void drawTarget(Ui::Renderer& tft, uint8_t index);
    void drawStats(Ui::Renderer& tft);
    void drawWin(Ui::Renderer& tft);
    void drawShape(Ui::Renderer& tft, Shape shape, int16_t cx, int16_t cy, int16_t size,
                   uint16_t color, bool filled, uint8_t turns = 0) const;

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    ShapesCourse::Round round_;
    bool matched_[4] = {};
    int8_t selected_ = -1;
    uint16_t taps_ = 0;
    /* Wrong matches this round: none is a right answer for the ladder, two
     * or more a wrong one, one says nothing. */
    uint8_t wrongs_ = 0;
    uint16_t bestTaps_ = 0;
    /* Rounds finished this visit, for the Carry on card. */
    uint8_t rounds_ = 0;
    /* The round just finished moved the player up: the card names the level. */
    bool climbed_ = false;

    /* Painted state. 0xFF means "nothing is painted there", which is what
     * renderStatic() and a new round both reset to -- a row that goes from
     * state 0 to state 0 with a different shape on it would otherwise be
     * skipped. */
    uint8_t drawnChoice_[4] = {};
    uint8_t drawnTarget_[4] = {};
    uint16_t drawnTaps_ = 0xFFFF;
    bool drawnWin_ = false;
};
