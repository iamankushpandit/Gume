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

#include "SortCourse.h"

struct AppMetadata;

const AppMetadata& sortAppMetadata();

class SortGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. Tapping a tile locks one of up to six; the flash timer
     * touches one. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    Rect tileRect(uint8_t index) const;
    void newRound();
    int8_t touchedTile(int16_t x, int16_t y) const;
    bool allLocked() const;

    Course::Ladder ladder_;
    /* Boards are generated, too many to number: none comes back within ten. */
    RecentQuestions recent_;
    SortCourse::Round round_;
    /* Which way the last two rounds ran, so no three run the same way. */
    uint8_t directions_ = 0;
    bool locked_[SortCourse::MAX_TILES] = {};
    uint8_t next_ = 0;
    /* A wrong tap this round: the round is then a wrong answer to the
     * ladder, which hears about rounds rather than taps. */
    bool slipped_ = false;
    bool climbed_ = false;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t flashTile_ = -1;

    /* What is on the panel: locked state and flash state per tile, packed. */
    uint8_t drawnTile_[6] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnBest_ = 0xFFFF;
    bool drawnSorted_ = false;
    bool flashError_ = false;
    uint32_t flashUntil_ = 0;
};

