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

#include "PercentCourse.h"

struct AppMetadata;

const AppMetadata& percentCircleAppMetadata();

class PercentCircleGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. This screen already reasoned about full versus partial by
     * hand and already filled its own bands before writing into them; the
     * migration only moves the two halves into the methods the base class
     * offers. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    Keep keepsGame() const override;

private:
    void startLevel();
    void newRound();
    /* The one answer a question gets, to the ladder. */
    void answer(AppContext& host, bool right, int8_t pressed);

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    PercentCourse::Question q_;
    uint16_t item_ = 0;
    bool announce_ = false;
    bool banner_ = false;
    uint16_t score_ = 0;
    uint16_t rounds_ = 0;
    uint16_t streak_ = 0;
    int8_t selected_ = -1;
    uint8_t currentPercent_ = 0;
    bool roundComplete_ = false;

    Rect optionRect(uint8_t index) const {
        const int16_t col = index % 2;
        const int16_t row = index / 2;
        return Rect{static_cast<int16_t>(164 + col * 76), static_cast<int16_t>(96 + row * 44), 70, 38};
    }

    Rect minusRect() const {
        return Rect{164, 152, 34, 34};
    }

    Rect valueRect() const {
        return Rect{200, 152, 38, 34};
    }

    Rect plusRect() const {
        return Rect{240, 152, 34, 34};
    }

    Rect okRect() const {
        return Rect{276, 152, 34, 34};
    }

    void fillSlice(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const;
    void drawCircle(Ui::Renderer& tft, uint8_t percent, bool highlight) const;
    void drawOptions(Ui::Renderer& tft) const;
    void drawShadeControls(Ui::Renderer& tft) const;
};
