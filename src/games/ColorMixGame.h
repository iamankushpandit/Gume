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

struct ColorMixDefinition {
    const char* left;
    const char* right;
    const char* result;
    uint16_t leftColor;
    uint16_t rightColor;
    uint16_t resultColor;
};

struct AppMetadata;

const AppMetadata& colorMixAppMetadata();

class ColorMixGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase render. renderStatic() is the clear and the top bar;
     * renderDynamic() is everything else. Split mechanically by
     * tools/split_render.py -- see the note in the .cpp. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    Rect answerRect(uint8_t index) const;
    void newQuestion();
    int8_t touchedAnswer(int16_t x, int16_t y) const;

    const ColorMixDefinition* active_ = nullptr;
    const char* labels_[4] = {};
    uint16_t colors_[4] = {};
    uint8_t correct_ = 0;
    uint16_t score_ = 0;
    uint16_t attempts_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    bool correctFlash_ = false;
    bool wrongFlash_ = false;
    uint32_t flashUntil_ = 0;
};
