// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "OddOneOutGame.h"
#include "engine/AppRegistry.h"

namespace {
namespace OC = OddOneCourse;
constexpr uint16_t YELLOW = 0xFEC0;
constexpr uint32_t RIGHT_HOLD_MS = 700;
constexpr uint32_t WRONG_HOLD_MS = 1600;
/* A ring round a card sits 2 and 3 px outside it; the cards stand 20px
 * apart across and 10px down, so neither reaches the next card. */
constexpr int16_t RING = 3;

constexpr AppScoreInfo ODD_ONE_OUT_SCORE = {
    "oddone", "Odd One", "oddBest", "pts", false
};

constexpr AppMetadata ODD_ONE_OUT_METADATA = {
    "oddone",
    "Odd One",
    "Odd One Out",
    "find different",
    "Odd One",
    "Find the one that differs.",
    &ODD_ONE_OUT_SCORE,
    LauncherIcon::OddOneOut,
    20,
    true,
};
}

const AppMetadata& oddOneOutAppMetadata() {
    return ODD_ONE_OUT_METADATA;
}

const char* OddOneOutGame::title() const {
    return oddOneOutAppMetadata().screenTitle != nullptr
        ? oddOneOutAppMetadata().screenTitle
        : oddOneOutAppMetadata().title;
}

/* A new visit, or New game from the Carry on card: the score starts over,
 * the level does not. It used to be this visit's score, so every visit began
 * with colour again. */
void OddOneOutGame::begin(AppContext& host) {
    score_ = 0;
    rounds_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(oddOneOutAppMetadata().score->bestKey, 0));
    ladder_.start(OC::LEVEL_COUNT, host.courseLevel());
    recent_.reset();
    announce_ = false;
    newRound();
    markDirty();
}

/* An answered round is something to come back to. */
Game::Keep OddOneOutGame::keepsGame() const {
    return rounds_ > 0 ? Keep::Ask : Keep::No;
}

/* The answer stays shown for the rest of its time. */
void OddOneOutGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    movedOn(feedbackUntil_, awayMs);
}

Rect OddOneOutGame::itemRect(uint8_t index) const {
    const uint8_t col = index % 3;
    const uint8_t row = index / 3;
    return Rect{static_cast<int16_t>(36 + col * 86), static_cast<int16_t>(70 + row * 50), 66, 40};
}

void OddOneOutGame::newRound() {
    /* Nine new cards and a new odd one out. A layout change, and it lives here
     * so no caller can deal a round and forget. */
    markFullDirty();
    for (uint8_t tries = 0; tries < 20; ++tries) {
        OC::deal(ladder_.level(), esp_random(), round_);
        if (!recent_.recentlyUsed(OC::token(round_))) break;
    }
    recent_.remember(OC::token(round_));
    banner_ = announce_;
    announce_ = false;
    selected_ = -1;
    answered_ = false;
    feedbackUntil_ = 0;
}

int8_t OddOneOutGame::touchedItem(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < 9; ++i) {
        if (itemRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

/* One card, from its own colour, shape, size and turn. */
void OddOneOutGame::drawItem(Ui::Renderer& tft, uint8_t index) const {
    const Rect r = itemRect(index);
    const OC::Card& card = round_.cards[index];
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
    const int16_t cy = static_cast<int16_t>(r.y + r.h / 2);
    const int16_t size = card.size;
    switch (card.shape) {
        case OC::Shape::Circle:
            tft.fillCircle(cx, cy, size, card.colour);
            tft.drawCircle(cx, cy, size, Ui::outline());
            break;
        case OC::Shape::Square:
            /* The fill a pixel inside its outline, which covers that pixel
             * anyway: a fill and an outline of one rectangle is what the
             * d-pad's selector stops at, and a square in a card would make the
             * card a panel with a control in it rather than the control. */
            tft.fillRoundRect(cx - size + 1, cy - size + 1, size * 2 - 2, size * 2 - 2, 2, card.colour);
            tft.drawRoundRect(cx - size, cy - size, size * 2, size * 2, 3, Ui::outline());
            break;
        case OC::Shape::Triangle:
            if (card.upsideDown) {
                tft.fillTriangle(cx, cy + size, cx - size, cy - size, cx + size, cy - size, card.colour);
                tft.drawTriangle(cx, cy + size, cx - size, cy - size, cx + size, cy - size, Ui::outline());
            } else {
                Ui::drawTriangleShape(tft, cx, cy, size, card.colour, true);
                Ui::drawTriangleShape(tft, cx, cy, size, Ui::outline(), false);
            }
            break;
    }
}

void OddOneOutGame::update(AppContext& host, const TouchPoint& touch) {
    if (answered_) {
        if (millis() >= feedbackUntil_) {
            newRound();
        }
        /* Nothing else is live while the answer shows: the next grid is
         * dealt before any tap is judged against it. */
        return;
    }
    if (!touch.justPressed) {
        return;
    }
    const int8_t item = touchedItem(touch.x, touch.y);
    if (item < 0) {
        return;
    }
    /* One try: the odd card is shown either way. Tapping round the grid
     * used to find it every time, and climbed with it. */
    selected_ = item;
    answered_ = true;
    ++rounds_;
    const bool right = item == round_.odd;
    if (right) {
        ++score_;
        ++streak_;
        if (host.saveBestScore(oddOneOutAppMetadata().score->bestKey, streak_, false)) {
            bestStreak_ = streak_;
        }
    } else {
        streak_ = 0;
    }
    const Course::Ladder::Step step = ladder_.answer(right);
    if (step != Course::Ladder::Step::Stay) {
        host.setCourseLevel(ladder_.level());
        recent_.reset();
    }
    if (step == Course::Ladder::Step::Up) {
        announce_ = true;
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::LevelUp);
    } else if (right) {
        host.beepOk();
    } else {
        host.beepError();
    }
    feedbackUntil_ = millis() + (right ? RIGHT_HOLD_MS : WRONG_HOLD_MS);
    markDirty();
}

void OddOneOutGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    /* The line under the header: what to do, or over the first grid of a new
     * level, its name. */
    if (banner_) {
        char line[OC::LEVEL_LINE_MAX + 1];
        OC::levelLine(round_.level, line, sizeof(line));
        tft.setTextColor(YELLOW, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(line, GAME_CANVAS_WIDTH / 2, 60, 2);
        tft.setTextDatum(TL_DATUM);
    } else {
        Ui::drawLabel(tft, Rect{8, 52, 304, 16}, "Tap the one that is different",
                      Ui::muted(), 1, Align::Center);
    }

    /* The nine cards are fixed for the round. */
    for (uint8_t i = 0; i < OC::CARDS; ++i) {
        drawItem(tft, i);
    }

    drawnRings_ = false;
    drawnScore_ = 0xFFFF;
    drawnBest_ = 0xFFFF;
}

void OddOneOutGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (score_ != drawnScore_ || bestStreak_ != drawnBest_) {
        tft.fillRect(10, 33, 150, 18, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 8 - 140, 33, 140, 18, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        char buf[32];
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u  Score %u", static_cast<unsigned>(round_.level),
                 static_cast<unsigned>(score_));
        tft.drawString(buf, 10, 35, 2);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(bestStreak_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 35, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnBest_ = bestStreak_;
    }

    /* Answered: a green ring round the odd card, and a red one round the card
     * tapped if that was not it. Rings are drawn outside the cards, so the
     * cards are not touched; a new round repaints the whole screen. */
    if (answered_ && !drawnRings_) {
        for (uint8_t i = 0; i < OC::CARDS; ++i) {
            const bool odd = i == round_.odd;
            const bool tapped = i == static_cast<uint8_t>(selected_);
            if (!odd && !tapped) continue;
            const Rect r = itemRect(i);
            const uint16_t colour = odd ? Ui::success() : Ui::error();
            for (int16_t k = 2; k <= RING; ++k) {
                tft.drawRoundRect(r.x - k, r.y - k, r.w + 2 * k, r.h + 2 * k, 6 + k, colour);
            }
        }
        drawnRings_ = true;
    }
}
