// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "SequenceGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr AppMetadata SEQUENCE_METADATA = {
    "calendar",
    "Calendar",
    nullptr,
    "days & months",
    "Calendar",
    "Days and months in order.",
    nullptr,
    LauncherIcon::Sequence,
    23,
    true,
};
}

const AppMetadata& sequenceAppMetadata() {
    return SEQUENCE_METADATA;
}

namespace {
namespace CC = CalendarCourse;
constexpr uint16_t YELLOW = 0xFEC0;

/* The question, top to bottom: the prompt line, the subject's box, and under
 * the box a line that names a new level over its first question. */
constexpr int16_t PROMPT_CY = 70;
constexpr int16_t PROMPT_TOP = 62;
constexpr int16_t PROMPT_H = 16;
constexpr int16_t BOX_X = 60;
constexpr int16_t BOX_Y = 84;
constexpr int16_t BOX_W = 200;
constexpr int16_t BOX_H = 52;
constexpr int16_t BANNER_TOP = BOX_Y + BOX_H;
constexpr int16_t BANNER_H = 16;
constexpr int16_t TILE_TOP = 152;
static_assert(PROMPT_TOP + PROMPT_H <= BOX_Y, "prompt and box overlap");
static_assert(BANNER_TOP + BANNER_H <= TILE_TOP, "banner and tiles overlap");
/* The level and the tally, right of the two track buttons. */
constexpr int16_t HEADER_X = 256;
}

const char* SequenceGame::title() const {
    return sequenceAppMetadata().screenTitle != nullptr
        ? sequenceAppMetadata().screenTitle
        : sequenceAppMetadata().title;
}

Rect SequenceGame::modeBtn(uint8_t m) const {
    return Rect{static_cast<int16_t>(8 + m * 124), 32, 120, 26};
}

Rect SequenceGame::answerTile(uint8_t i) const {
    const uint8_t col = i % 2;
    const uint8_t row = i / 2;
    return Rect{static_cast<int16_t>(8 + col * 159), static_cast<int16_t>(152 + row * 48), 152, 40};
}

void SequenceGame::startTrack(uint8_t track) {
    track_ = track;
    deck_.reset(CC::poolSize(track_, ladders_[track_].level()));
    announce_ = false;
    newQuestion();
}

void SequenceGame::newQuestion() {
    const uint8_t level = ladders_[track_].level();
    item_ = deck_.next(esp_random());
    CC::make(track_, level, item_, esp_random(), q_);
    banner_ = announce_;
    announce_ = false;
    selected_ = -1;
    answered_ = false;
    feedbackUntil_ = 0;
    drawnQuestion_ = false;
    drawnTiles_ = 0xFF;
}

/* A new visit, or New game from the Carry on card: the tally starts over,
 * and each track's level is the player's, kept by the runtime. */
void SequenceGame::begin(AppContext& host) {
    score_ = 0;
    rounds_ = 0;
    ladders_[CC::DAYS].start(CC::DAY_LEVELS, host.courseLevel(CC::DAYS));
    ladders_[CC::MONTHS].start(CC::MONTH_LEVELS, host.courseLevel(CC::MONTHS));
    startTrack(CC::DAYS);
    markDirty();
}

/* A score is worth coming back to. With no answer given yet there is nothing
 * to lose but a choice of Days or Months, which is one tap to make again. */
Game::Keep SequenceGame::keepsGame() const {
    return rounds_ > 0 ? Keep::Ask : Keep::No;
}

/* The last answer stays coloured for the rest of its moment rather than
 * vanishing the instant the game is back. */
void SequenceGame::resume(AppContext&, uint32_t awayMs) {
    movedOn(feedbackUntil_, awayMs);
}

void SequenceGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (answered_ && now >= feedbackUntil_) {
        newQuestion();
        markDirty();
        return;
    }

    if (!touch.justPressed) return;

    // Track buttons (always live)
    for (uint8_t m = 0; m < CC::TRACKS; ++m) {
        if (modeBtn(m).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (track_ != m) {
                startTrack(m);
                markDirty();
            }
            return;
        }
    }

    if (answered_) return;

    for (uint8_t i = 0; i < 4; ++i) {
        if (!answerTile(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        selected_ = static_cast<int8_t>(i);
        answered_ = true;
        ++rounds_;
        const bool right = i == q_.correct;
        if (right) {
            ++score_;
        } else {
            /* Before the ladder moves: a step resets the deck. */
            deck_.missed(item_);
        }
        /* One try, so the first press is the only one the ladder hears. */
        Course::Ladder& ladder = ladders_[track_];
        const Course::Ladder::Step step = ladder.answer(right);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder.level(), track_);
            deck_.reset(CC::poolSize(track_, ladder.level()));
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
        feedbackUntil_ = now + 1400UL;
        markDirty();
        return;
    }
}

void SequenceGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    drawnHeader_ = false;
    drawnQuestion_ = false;
    drawnTiles_ = 0xFF;
}

void SequenceGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    char buf[32];
    const uint8_t level = q_.level;

    /* The two track buttons, and right of them the level and the tally. */
    if (!drawnHeader_ || track_ != drawnTrack_ || level != drawnLevel_ ||
        score_ != drawnScore_ || rounds_ != drawnRounds_) {
        for (uint8_t m = 0; m < CC::TRACKS; ++m) {
            const bool active = track_ == m;
            Ui::drawButton(tft, modeBtn(m), m == CC::DAYS ? "Days" : "Months",
                           active ? Ui::rgb(36, 132, 204) : Ui::panel(), Ui::outline(),
                           active ? TFT_WHITE : Ui::text());
        }
        tft.fillRect(HEADER_X, 32, GAME_CANVAS_WIDTH - HEADER_X, 26, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(score_),
                 static_cast<unsigned>(rounds_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 33, 2);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(level));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 50, 1);
        tft.setTextDatum(TL_DATUM);
        drawnHeader_ = true;
        drawnTrack_ = track_;
        drawnLevel_ = level;
        drawnScore_ = score_;
        drawnRounds_ = rounds_;
    }

    if (!drawnQuestion_) {
        tft.fillRect(0, PROMPT_TOP, GAME_CANVAS_WIDTH, PROMPT_H, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(q_.prompt, GAME_CANVAS_WIDTH / 2, PROMPT_CY, 2);

        tft.fillRoundRect(BOX_X, BOX_Y, BOX_W, BOX_H, 8, Ui::surface());
        tft.drawRoundRect(BOX_X, BOX_Y, BOX_W, BOX_H, 8, Ui::rgb(36, 132, 204));
        tft.setTextColor(Ui::text(), Ui::surface());
        const uint8_t subjectFont = tft.textWidth(q_.subject, 4) > BOX_W - 20 ? 2 : 4;
        tft.drawString(q_.subject, GAME_CANVAS_WIDTH / 2, BOX_Y + BOX_H / 2, subjectFont);

        tft.fillRect(0, BANNER_TOP, GAME_CANVAS_WIDTH, BANNER_H, Ui::bg());
        if (banner_) {
            CC::levelLine(track_, level, buf, sizeof(buf));
            tft.setTextColor(YELLOW, Ui::bg());
            tft.drawString(buf, GAME_CANVAS_WIDTH / 2, BANNER_TOP + BANNER_H / 2, 2);
        }
        tft.setTextDatum(TL_DATUM);
        drawnQuestion_ = true;
    }

    const uint8_t tiles = answered_ ? 1 : 0;
    if (tiles != drawnTiles_) {
        for (uint8_t i = 0; i < 4; ++i) {
            uint16_t fill = Ui::panel();
            uint16_t tc   = Ui::text();
            if (answered_) {
                if (i == q_.correct)                           { fill = Ui::success(); tc = TFT_BLACK; }
                else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error();   tc = TFT_BLACK; }
            }
            CC::format(q_.label, q_.options[i], buf, sizeof(buf));
            Ui::drawButton(tft, answerTile(i), buf, fill, Ui::outline(), tc, false, 2);
        }
        drawnTiles_ = tiles;
    }
}
