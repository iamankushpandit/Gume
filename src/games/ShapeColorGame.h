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

const AppMetadata& shapeColorAppMetadata();

/* Match four shapes to their outlines, getting harder each round.
 *
 * The ladder is the point. A round of circle / square / triangle / star is
 * solvable by colour alone, which is right for a younger player and is
 * nothing at all for an older player -- so the rounds after it take the easy
 * cues away one at a time:
 *
 *   1. the four everyone knows, named by colour and shape
 *   2. told apart by SIDE COUNT: pentagon, hexagon, heptagon, octagon
 *   3. lookalikes: oval, rectangle, diamond, trapezium -- the shapes a young player
 *      calls "circle" and "square" until somebody makes them look twice
 *   4. CONCAVE against CONVEX, with the word on the row, because the
 *      distinction has a name and this is where it is worth learning
 *
 * Level 4 is the only one that mixes families, and it always deals at least
 * one of each -- a round of four concave shapes would not be the lesson. */
class ShapeColorGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase, and properly so since 5.12. The instruction line is the only
     * thing that never changes while the screen is up; a tap used to repaint
     * all eight rows and the header with it. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

    /* Public only so the fact table in the .cpp can assert that it has one
     * row per shape. A count stated in two places is a count that will
     * eventually disagree with itself. */
    enum class Shape : uint8_t {
        Circle,
        Square,
        Triangle,
        Star5,
        Pentagon,
        Hexagon,
        Heptagon,
        Octagon,
        Oval,
        Rectangle,
        Diamond,
        Trapezium,
        Cross,
        Arrow,
        Star4,
        Count
    };

private:
    struct Item {
        Shape shape;
        uint16_t color;
        char name[16];
    };

    Rect choiceRect(uint8_t index) const;
    Rect targetRect(uint8_t index) const;
    Rect statsRect() const;
    void newRound();
    bool allMatched() const;
    uint8_t level() const;
    /** 0 normal, 1 selected, 2 matched -- what a left-hand row is showing. */
    uint8_t choiceState(uint8_t index) const;
    void drawChoice(Ui::Renderer& tft, uint8_t index);
    void drawTarget(Ui::Renderer& tft, uint8_t index);
    void drawStats(Ui::Renderer& tft);
    void drawWin(Ui::Renderer& tft);
    void drawShape(Ui::Renderer& tft, Shape shape, int16_t cx, int16_t cy, int16_t size, uint16_t color, bool filled) const;

    Item items_[4];
    uint8_t targetOrder_[4] = {};
    bool matched_[4] = {};
    int8_t selected_ = -1;
    uint16_t taps_ = 0;
    uint16_t bestTaps_ = 0;
    /* Rounds CLEARED, which is what the level is made of. It resets with the
     * screen rather than persisting: a level is where you are in this sitting,
     * not a rank, and a young player picking the console up should meet the easy
     * round first. */
    uint8_t rounds_ = 0;

    /* Painted state. 0xFF means "nothing is painted there", which is what
     * renderStatic() and a new round both reset to -- a row that goes from
     * state 0 to state 0 with a different shape on it would otherwise be
     * skipped. */
    uint8_t drawnChoice_[4] = {};
    uint8_t drawnTarget_[4] = {};
    uint16_t drawnTaps_ = 0xFFFF;
    bool drawnWin_ = false;
};
