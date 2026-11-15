// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ColorMixGame.h"
#include "engine/AppRegistry.h"

namespace {
namespace CMC = ColorMixCourse;
constexpr uint16_t YELLOW = 0xFEC0;

/* The screen, top to bottom, each part cleared only inside its own strip. */
constexpr int16_t HEADER_Y = 33;
constexpr int16_t HEADER_H = 17;
constexpr Rect LINE_RECT = {10, 52, 300, 18};
constexpr Rect LEFT_SWATCH = {56, 78, 60, 44};
constexpr Rect RIGHT_SWATCH = {204, 78, 60, 44};
/* The swatches, the sign between them and the names hung 12px under them in
 * font 1. */
constexpr int16_t QUESTION_TOP = 74;
constexpr int16_t QUESTION_H = 64;
constexpr int16_t BUTTON_TOP = 142;
/* A ring round a button sits 2 and 3 px outside it. */
constexpr int16_t RING = 3;
static_assert(HEADER_Y + HEADER_H <= LINE_RECT.y, "header and line overlap");
static_assert(LINE_RECT.y + LINE_RECT.h <= QUESTION_TOP, "line and question overlap");
static_assert(QUESTION_TOP + QUESTION_H <= BUTTON_TOP - RING, "question and rings overlap");

constexpr uint32_t RIGHT_HOLD_MS = 900;
constexpr uint32_t WRONG_HOLD_MS = 1800;

constexpr AppScoreInfo COLOR_MIX_SCORE = {
    "colormix", "Color Mix", "mixBest", "pts", false
};

constexpr AppMetadata COLOR_MIX_METADATA = {
    "colormix",
    "Color Mix",
    nullptr,
    "mix colors",
    "Color Mix",
    "Mix two colours.",
    &COLOR_MIX_SCORE,
    LauncherIcon::ColorMix,
    18,
    true,
};
}

const AppMetadata& colorMixAppMetadata() {
    return COLOR_MIX_METADATA;
}

const char* ColorMixGame::title() const {
    return colorMixAppMetadata().screenTitle != nullptr
        ? colorMixAppMetadata().screenTitle
        : colorMixAppMetadata().title;
}

/* A new visit, or New game from the Carry on card: the score starts over,
 * the level does not. */
void ColorMixGame::begin(AppContext& host) {
    score_ = 0;
    attempts_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(colorMixAppMetadata().score->bestKey, 0));
    ladder_.start(CMC::LEVEL_COUNT, host.courseLevel());
    startLevel();
    announce_ = false;
    newQuestion();
    markDirty();
}

/* Worth coming back to once an answer has been given: the score line is the
 * whole of what this visit has built. */
Game::Keep ColorMixGame::keepsGame() const {
    return attempts_ > 0 ? Keep::Ask : Keep::No;
}

/* The answer stays shown for the rest of its time, so the player sees how
 * the last one went before the next question arrives. */
void ColorMixGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    movedOn(feedbackUntil_, awayMs);
}

Rect ColorMixGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(26 + col * 148), static_cast<int16_t>(BUTTON_TOP + row * 44), 120, 34};
}

void ColorMixGame::startLevel() {
    deck_.reset(CMC::poolSize(ladder_.level()));
}

void ColorMixGame::newQuestion() {
    questionLevel_ = ladder_.level();
    item_ = deck_.next(esp_random());
    CMC::make(questionLevel_, item_, esp_random(), q_);
    banner_ = announce_;
    announce_ = false;
    selected_ = -1;
    answered_ = false;
    feedbackUntil_ = 0;
    drawnQuestion_ = false;
    drawnLine_ = 0xFF;
}

int8_t ColorMixGame::touchedAnswer(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < 4; ++i) {
        if (answerRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

void ColorMixGame::update(AppContext& host, const TouchPoint& touch) {
    if (answered_) {
        if (millis() >= feedbackUntil_) {
            newQuestion();
            markDirty();
        }
        return;
    }
    if (!touch.justPressed) {
        return;
    }
    const int8_t answer = touchedAnswer(touch.x, touch.y);
    if (answer < 0) {
        return;
    }
    /* One try: the answer is shown either way. Four taps used to find any
     * answer, and a wrong tap said nothing about what the pair makes. */
    ++attempts_;
    selected_ = answer;
    answered_ = true;
    const bool right = answer == q_.correct;
    if (right) {
        ++score_;
        ++streak_;
        if (host.saveBestScore(colorMixAppMetadata().score->bestKey, streak_, false)) {
            bestStreak_ = streak_;
        }
    } else {
        streak_ = 0;
        /* Before the ladder moves: a step resets the deck. */
        deck_.missed(item_);
    }
    const Course::Ladder::Step step = ladder_.answer(right);
    if (step != Course::Ladder::Step::Stay) {
        host.setCourseLevel(ladder_.level());
        startLevel();
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

void ColorMixGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    drawnHeader_ = false;
    drawnQuestion_ = false;
    drawnLine_ = 0xFF;
}

void ColorMixGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    char buf[40];

    if (!drawnHeader_ || score_ != drawnScore_ || attempts_ != drawnAttempts_ ||
        questionLevel_ != drawnLevel_) {
        tft.fillRect(0, HEADER_Y, GAME_CANVAS_WIDTH, HEADER_H, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(questionLevel_));
        tft.drawString(buf, 10, 35, 2);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "%u/%u  Best %u", static_cast<unsigned>(score_),
                 static_cast<unsigned>(attempts_), static_cast<unsigned>(bestStreak_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 35, 2);
        tft.setTextDatum(TL_DATUM);
        drawnHeader_ = true;
        drawnScore_ = score_;
        drawnAttempts_ = attempts_;
        drawnLevel_ = questionLevel_;
    }

    /* The line under the header: the question, or the new level's name over
     * its first question, or once answered what the pair makes. */
    const uint8_t line = answered_ ? 1 : 0;
    if (line != drawnLine_) {
        const bool right = selected_ == static_cast<int8_t>(q_.correct);
        uint16_t colour = Ui::text();
        const char* text = q_.prompt();
        if (answered_) {
            colour = right ? Ui::success() : Ui::error();
            if (right) {
                text = "Correct!";
            } else {
                snprintf(buf, sizeof(buf), q_.backwards ? "It was %s" : "It makes %s",
                         CMC::SWATCHES[q_.answer].name);
                text = buf;
            }
        } else if (banner_) {
            CMC::levelLine(questionLevel_, buf, sizeof(buf));
            text = buf;
            colour = YELLOW;
        }
        tft.fillRect(LINE_RECT.x, LINE_RECT.y, LINE_RECT.w, LINE_RECT.h, Ui::bg());
        tft.setTextColor(colour, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(text, LINE_RECT.x + LINE_RECT.w / 2, LINE_RECT.y + LINE_RECT.h / 2, 2);
        tft.setTextDatum(TL_DATUM);
        drawnLine_ = line;
    }

    /* The two swatches: forwards the two colours and a plus, backwards the
     * colour, "+ ? =" and what it made. And the four buttons, unringed. */
    if (!drawnQuestion_) {
        tft.fillRect(0, QUESTION_TOP, GAME_CANVAS_WIDTH, QUESTION_H, Ui::bg());
        const uint8_t shown[2] = {q_.left, q_.right};
        const Rect swatch[2] = {LEFT_SWATCH, RIGHT_SWATCH};
        tft.setTextDatum(MC_DATUM);
        for (uint8_t k = 0; k < 2; ++k) {
            const Rect& r = swatch[k];
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, CMC::SWATCHES[shown[k]].fill);
            tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
            tft.setTextColor(Ui::text(), Ui::bg());
            tft.drawString(CMC::SWATCHES[shown[k]].name, r.x + r.w / 2, r.y + r.h + 12, 1);
        }
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(q_.backwards ? "+ ? =" : "+", GAME_CANVAS_WIDTH / 2,
                       LEFT_SWATCH.y + LEFT_SWATCH.h / 2, 4);

        const Rect first = answerRect(0);
        const Rect last = answerRect(3);
        tft.fillRect(first.x - RING, first.y - RING, last.x + last.w - first.x + 2 * RING,
                     last.y + last.h - first.y + 2 * RING, Ui::bg());
        for (uint8_t i = 0; i < 4; ++i) {
            const Rect r = answerRect(i);
            const uint16_t fill = CMC::SWATCHES[q_.options[i]].fill;
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
            tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
            tft.setTextColor(Ui::onFill(fill), fill);
            tft.drawString(CMC::SWATCHES[q_.options[i]].name, r.x + r.w / 2, r.y + r.h / 2, 2);
        }
        tft.setTextDatum(TL_DATUM);
        drawnQuestion_ = true;
        drawnRings_ = false;
    }

    /* Answered: the buttons are their own colours, so the answer is marked
     * with a ring round it rather than a fill -- green round the right one,
     * red round the one tapped if that was not it. */
    if (answered_ && !drawnRings_) {
        for (uint8_t i = 0; i < 4; ++i) {
            const bool isRight = i == q_.correct;
            const bool isTapped = i == static_cast<uint8_t>(selected_);
            if (!isRight && !isTapped) continue;
            const Rect r = answerRect(i);
            const uint16_t colour = isRight ? Ui::success() : Ui::error();
            for (int16_t k = 2; k <= RING; ++k) {
                tft.drawRoundRect(r.x - k, r.y - k, r.w + 2 * k, r.h + 2 * k, 6 + k, colour);
            }
        }
        drawnRings_ = true;
    }
}
