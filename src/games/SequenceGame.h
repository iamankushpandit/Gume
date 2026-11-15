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

#include "CalendarCourse.h"

struct AppMetadata;

const AppMetadata& sequenceAppMetadata();

/* Calendar: a course in the week and the year. Each of the two buttons is a
 * track with its own level; what each level asks, and the wrong tiles, are
 * in CalendarCourse.h. */
class SequenceGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase render, and within renderDynamic() three parts each
     * repainted only when it changed: the track buttons with the level and
     * tally, the question, and the four tiles. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    Rect modeBtn(uint8_t m) const;
    Rect answerTile(uint8_t i) const;
    void startTrack(uint8_t track);
    void newQuestion();

    /* Days is track 0 and Months track 1, as AppContext stores them. */
    uint8_t track_ = CalendarCourse::DAYS;
    Course::Ladder ladders_[CalendarCourse::TRACKS];
    Course::QuestionDeck deck_;
    CalendarCourse::Question q_;
    uint16_t item_ = 0;
    bool    announce_  = false;
    bool    banner_    = false;
    int8_t  selected_  = -1;
    bool    answered_  = false;
    uint32_t feedbackUntil_ = 0;
    uint16_t score_  = 0;
    uint16_t rounds_ = 0;

    /* What is on the panel. */
    bool    drawnHeader_ = false;
    uint8_t drawnTrack_ = 0xFF;
    uint8_t drawnLevel_ = 0;
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnRounds_ = 0xFFFF;
    bool    drawnQuestion_ = false;
    /* Tiles: 0xFF not drawn, 0 waiting, 1 answered. */
    uint8_t drawnTiles_ = 0xFF;
};
