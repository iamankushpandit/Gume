// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "ShapeArithCourse.h"
#include "engine/Course.h"
#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& objectAddAppMetadata();

/* Shape Arith: adding and taking away with shapes the player can count, as
 * a course (ShapeArithCourse.h). Each question shows itself -- the second
 * group arrives a shape at a time, or the group taken away blinks and goes
 * -- and then waits for an answer, and after the answer for a tap. */
class ObjectAddGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* renderStatic() is the clear and the top bar; renderDynamic() repaints
     * only what changed since it last ran, tracked by the drawn* members. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /* Endless: worth coming back to once something is right. */
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    enum class Phase : uint8_t { Showing, AnimIn, Flashing, Question, Feedback };

    Rect answerRect(uint8_t i) const;
    void newQuestion();
    void drawQuestion(Ui::Renderer& tft);
    void drawLeftPanel(Ui::Renderer& tft, uint8_t state) const;
    void drawLeftLabel(Ui::Renderer& tft, uint8_t state) const;
    void drawShape(Ui::Renderer& tft, const Rect& panel, uint8_t count, uint8_t idx,
                   uint16_t color, bool filled) const;
    void eraseShape(Ui::Renderer& tft, const Rect& panel, uint8_t count, uint8_t idx) const;
    uint8_t leftState() const;
    uint8_t arrived() const;
    uint8_t stripState() const;

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    ShapeArithCourse::Question q_;
    uint16_t item_ = 0;
    Phase phase_ = Phase::Showing;
    uint8_t shape_ = 0;
    uint8_t animCount_ = 0;
    bool flashOn_ = false;
    bool announce_ = false;    // the next question is a level's first
    bool showLevel_ = false;   // this one is, so the strip names the level
    uint32_t animNextAt_ = 0;
    int8_t selected_ = -1;
    uint16_t score_ = 0;
    uint16_t rounds_ = 0;

    /* What the panel shows, so a repaint draws only the difference. 0xFF
     * means "not drawn"; renderStatic() and newQuestion() set them so. */
    bool drawnQuestion_ = false;
    uint8_t drawnLeft_ = 0xFF;
    uint8_t drawnArrived_ = 0;
    uint8_t drawnBottom_ = 0xFF;
    uint8_t drawnStrip_ = 0xFF;
    uint8_t drawnButton_[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnRounds_ = 0xFFFF;
    uint8_t drawnLevel_ = 0xFF;
};
