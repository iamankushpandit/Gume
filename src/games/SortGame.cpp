// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "SortGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t TILE = 0x24BD;
constexpr uint16_t LOCKED = 0x37F0;
constexpr uint16_t WRONG = 0xF9EA;
constexpr uint16_t YELLOW = 0xFEC0;
namespace SC = SortCourse;

constexpr AppScoreInfo SORT_SCORE = {
    "sort", "Sorting", "sortBest", "pts", false
};

constexpr AppMetadata SORT_METADATA = {
    "sort",
    "Sorting",
    nullptr,
    "order nums",
    "Sorting",
    "Order numbers up or down.",
    &SORT_SCORE,
    LauncherIcon::Sort,
    17,
    true,
};
}

const AppMetadata& sortAppMetadata() {
    return SORT_METADATA;
}

const char* SortGame::title() const {
    return sortAppMetadata().screenTitle != nullptr
        ? sortAppMetadata().screenTitle
        : sortAppMetadata().title;
}

/* A new visit, or New game from the Carry on card: the tally starts over,
 * the level does not. It used to be the rounds finished this visit. */
void SortGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(sortAppMetadata().score->bestKey, 0));
    ladder_.start(SC::LEVEL_COUNT, host.courseLevel());
    recent_.reset();
    directions_ = 0;
    climbed_ = false;
    newRound();
    markDirty();
}

Rect SortGame::tileRect(uint8_t index) const {
    const uint8_t col = index % 3;
    const uint8_t row = index / 3;
    return Rect{static_cast<int16_t>(24 + col * 92), static_cast<int16_t>(82 + row * 58), 76, 44};
}

void SortGame::newRound() {
    /* New numbers, every tile unlocked, the direction possibly reversed and the
     * sorted panel off. A layout change, and it lives here so no caller can
     * deal a round and forget. */
    markFullDirty();
    for (uint8_t tries = 0; tries < 20; ++tries) {
        SC::deal(ladder_.level(), esp_random(), directions_, round_);
        if (!recent_.recentlyUsed(SC::token(round_))) break;
    }
    recent_.remember(SC::token(round_));
    directions_ = SC::remember(directions_, round_.ascending);
    for (uint8_t i = 0; i < SC::MAX_TILES; ++i) locked_[i] = false;
    next_ = 0;
    slipped_ = false;
    flashTile_ = -1;
    flashUntil_ = 0;
}

bool SortGame::allLocked() const {
    return next_ >= round_.count;
}

int8_t SortGame::touchedTile(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < round_.count; ++i) {
        if (!locked_[i] && tileRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

/* Rounds sorted, or tiles already locked in this one -- including a board
 * sorted and waiting for the tap that counts it -- are worth coming back to. */
Game::Keep SortGame::keepsGame() const {
    return (score_ > 0 || next_ > 0) ? Keep::Ask : Keep::No;
}

/* A tile's flash finishes its moment rather than being cut short. */
void SortGame::resume(AppContext&, uint32_t awayMs) {
    movedOn(flashUntil_, awayMs);
}

void SortGame::update(AppContext& host, const TouchPoint& touch) {
    if (flashUntil_ > 0 && millis() > flashUntil_) {
        flashUntil_ = 0;
        flashTile_ = -1;
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    if (allLocked()) {
        newRound();
        climbed_ = false;
        markDirty();
        return;
    }

    const int8_t tile = touchedTile(touch.x, touch.y);
    if (tile < 0) {
        return;
    }

    if (tile == round_.order[next_]) {
        locked_[tile] = true;
        flashTile_ = tile;
        flashError_ = false;
        flashUntil_ = millis() + 350UL;
        ++next_;
        if (!allLocked()) {
            host.beepOk();
        } else {
            /* The round is the answer: right with no slip in it. */
            ++score_;
            if (!slipped_) {
                ++streak_;
                if (host.saveBestScore(sortAppMetadata().score->bestKey, streak_, false)) {
                    bestStreak_ = streak_;
                }
            }
            const Course::Ladder::Step step = ladder_.answer(!slipped_);
            if (step != Course::Ladder::Step::Stay) {
                host.setCourseLevel(ladder_.level());
                recent_.reset();
            }
            if (step == Course::Ladder::Step::Up) {
                climbed_ = true;
                host.pulseRgb(0, 255, 40, 450);
                host.playSound(Sound::LevelUp);
            } else {
                host.beepOk();
            }
        }
    } else {
        /* Wrong: the streak goes, and the round now counts as a miss, but the
         * board stays to be finished. */
        streak_ = 0;
        slipped_ = true;
        flashTile_ = tile;
        flashError_ = true;
        flashUntil_ = millis() + 500UL;
        host.beepError();
    }
    markDirty();
}

void SortGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    /* The direction is fixed for the round, and a new round is a full repaint,
     * so this is static even though it is not constant. */
    Ui::drawLabel(tft, Rect{10, 55, 300, 18},
                  round_.ascending ? "Tap smallest to largest" : "Tap largest to smallest",
                  Ui::text(), 2, Align::Center);

    for (uint8_t i = 0; i < 6; ++i) {
        drawnTile_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnBest_ = 0xFFFF;
    drawnSorted_ = false;
}

void SortGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    for (uint8_t i = 0; i < round_.count; ++i) {
        const uint8_t state = static_cast<uint8_t>((locked_[i] ? 1 : 0) |
                              (flashTile_ == static_cast<int8_t>(i)
                                   ? (flashError_ ? 2 : 4) : 0));
        if (state == drawnTile_[i]) {
            continue;
        }
        uint16_t fill = locked_[i] ? LOCKED : TILE;
        uint16_t text = locked_[i] ? TFT_BLACK : TFT_WHITE;
        if (flashTile_ == static_cast<int8_t>(i)) {
            fill = flashError_ ? WRONG : LOCKED;
            text = TFT_BLACK;
        }
        char label[8];
        SC::text(round_.tiles[i], label, sizeof(label));
        /* drawButton fills its rect opaquely, so a tile erases the tile that
         * was there and the flash needs no clearing of its own. */
        Ui::drawButton(tft, tileRect(i), label, fill, Ui::outline(), text, false, 4);
        drawnTile_[i] = state;
    }

    if (score_ != drawnScore_ || bestStreak_ != drawnBest_) {
        tft.fillRect(10, 33, 150, 20, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 8 - 150, 33, 150, 20, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        char buf[32];
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u  Rounds %u", static_cast<unsigned>(round_.level),
                 static_cast<unsigned>(score_));
        tft.drawString(buf, 10, 35, 2);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Best streak %u", static_cast<unsigned>(bestStreak_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 35, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnBest_ = bestStreak_;
    }

    /* Painted once. It overlaps the bottom row of tiles, but it never has to be
     * taken off: the only way past a sorted board is a tap, which deals a new
     * round and asks for a full repaint. A step up names the new level on it. */
    if (allLocked() && !drawnSorted_) {
        tft.fillRoundRect(58, 178, 204, 42, 8, Ui::panel());
        tft.drawRoundRect(58, 178, 204, 42, 8, Ui::success());
        char line[SC::LEVEL_LINE_MAX + 1];
        uint16_t colour = Ui::success();
        if (climbed_) {
            SC::levelLine(ladder_.level(), line, sizeof(line));
            colour = YELLOW;
        } else {
            snprintf(line, sizeof(line), "%s", slipped_ ? "Sorted - tap next" : "Sorted first time!");
        }
        tft.setTextColor(colour, Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(line, GAME_CANVAS_WIDTH / 2, 199, 2);
        tft.setTextDatum(TL_DATUM);
        drawnSorted_ = true;
    }
}
