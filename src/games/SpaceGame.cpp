// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "SpaceGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;

constexpr AppScoreInfo SPACE_SCORE = {
    "space", "Space", "spaceBest", "pts", false
};

constexpr AppMetadata SPACE_METADATA = {
    "space",
    "Space",
    "Space Quiz",
    "planets & the air above",
    "Space",
    "The solar system, and the air above us.",
    &SPACE_SCORE,
    LauncherIcon::Space,
    38,
    true,
};

/* The questions, their levels and the four generators are in SpaceCourse.h,
 * where test/host/space_course_test.cpp checks every one fits the screen. */
}

const AppMetadata& spaceAppMetadata() {
    return SPACE_METADATA;
}

const char* SpaceGame::title() const {
    return spaceAppMetadata().screenTitle != nullptr
        ? spaceAppMetadata().screenTitle
        : spaceAppMetadata().title;
}

namespace SpC = SpaceCourse;

void SpaceGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    best_ = static_cast<uint16_t>(host.getScore(spaceAppMetadata().score->bestKey, 0));
    ladder_.start(SpC::LEVEL_COUNT, host.courseLevel());
    deck_.reset(SpC::poolSize(ladder_.level()));
    announce_ = false;
    newQuestion();
    markDirty();
}

uint8_t SpaceGame::level() const {
    return ladder_.level();
}

/* The same four rects Math uses. Two screens with the same shape should not
 * put their buttons in two nearly-identical places. */
Rect SpaceGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152),
                static_cast<int16_t>(144 + row * 46), 132, 38};
}

/* Three lines of font 2 plus a little daylight. The note strip below starts at
 * 125 and the top button at 144, so this panel may not grow downwards without
 * both of those moving -- which is why they are derived from it below rather
 * than typed in twice. */
Rect SpaceGame::questionRect() const {
    return Rect{20, 66, 280, 58};
}

Rect SpaceGame::noteRect() const {
    const Rect q = questionRect();
    const int16_t top = static_cast<int16_t>(q.y + q.h + 1);
    return Rect{14, top, static_cast<int16_t>(GAME_CANVAS_WIDTH - 28),
                static_cast<int16_t>(answerRect(0).y - top - 1)};
}

void SpaceGame::newQuestion() {
    item_ = deck_.next(esp_random());
    SpC::make(level(), item_, esp_random(), q_);

    for (uint8_t i = 0; i < 4; ++i) {
        order_[i] = i;
    }
    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const uint8_t tmp = order_[i];
        order_[i] = order_[j];
        order_[j] = tmp;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        if (order_[i] == 0) {
            correctButton_ = i;
            break;
        }
    }

    selected_ = -1;
    answered_ = false;

    /* Everything a new question changes, invalidated together -- and the
     * button trackers are in here for the reason docs/RENDER_AUDIT.md gives:
     * the two buttons that were never highlighted go from state 0 to state 0,
     * so without this they would keep the previous question's labels. */
    drawnQuestion_ = false;
    drawnHeader_ = false;
    drawnNote_ = 0xFF;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
}

void SpaceGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (answered_) {
        newQuestion();
        /* A new question, four new labels and the prompt back. All of it is
         * content in rects that have not moved, so this is markDirty(): the
         * panel, the strip and the buttons each paint over themselves
         * opaquely. */
        markDirty();
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        selected_ = static_cast<int8_t>(i);
        answered_ = true;
        const bool right = (i == correctButton_);
        announce_ = false;
        if (right) {
            ++score_;
            ++streak_;
            if (host.saveBestScore(spaceAppMetadata().score->bestKey, score_, false)) {
                best_ = score_;
            }
        } else {
            streak_ = 0;
            deck_.missed(item_);   // before the ladder moves
        }
        const Course::Ladder::Step step = ladder_.answer(right);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
            deck_.reset(SpC::poolSize(ladder_.level()));
        }
        if (step == Course::Ladder::Step::Up) {
            announce_ = true;   // named over the next question
            host.pulseRgb(0, 255, 40, 450);
            host.playSound(Sound::LevelUp);
        } else {
            right ? host.beepOk() : host.beepError();
        }
        markDirty();
        return;
    }
}

void SpaceGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    /* Nothing below here is painted yet, so every tracker says "unknown"
     * rather than "unchanged". */
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnStreak_ = 0xFFFF;
    drawnHeader_ = false;
    drawnQuestion_ = false;
    drawnNote_ = 0xFF;
}

/* The panel fills itself opaquely before the text goes down, so a short
 * question cannot leave the tail of a long one behind it. */
void SpaceGame::drawQuestion(Ui::Renderer& tft) {
    const Rect r = questionRect();
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, Ui::panel());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, Ui::outline());

    const int16_t textW = static_cast<int16_t>(r.w - 16);
    char lines[3][Ui::WRAP_LINE_MAX];
    uint8_t font = 2;
    uint8_t needed = Ui::wrapLines(tft, q_.text, textW, font, lines, 3);
    if (needed > 3) {
        /* Font 2 could not say it in three lines. Nothing is cut: it is asked
         * again at font 1, which fits four lines in the same panel. */
        font = 1;
        needed = Ui::wrapLines(tft, q_.text, textW, font, lines, 3);
    }
    const uint8_t shown = min<uint8_t>(needed, 3);
    const int16_t lineH = font == 2 ? 18 : 11;
    int16_t y = static_cast<int16_t>(r.y + (r.h - shown * lineH) / 2);

    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(TC_DATUM);
    for (uint8_t i = 0; i < shown; ++i) {
        tft.drawString(lines[i], static_cast<int16_t>(r.x + r.w / 2), y, font);
        y = static_cast<int16_t>(y + lineH);
    }
    tft.setTextDatum(TL_DATUM);
}

/* One line of prompt before an answer, up to two of fact after one. The two
 * are different heights as well as different lengths, so the strip is cleared
 * whole before either is written rather than relying on an opaque background
 * colour behind the glyphs. */
void SpaceGame::drawNote(Ui::Renderer& tft) {
    const Rect r = noteRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    tft.setTextDatum(TC_DATUM);
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);

    if (!answered_) {
        /* A step up names the new level over its first question. */
        char prompt[SpC::LEVEL_LINE_MAX + 1];
        if (announce_) {
            SpC::levelLine(level(), prompt, sizeof(prompt));
        } else {
            snprintf(prompt, sizeof(prompt), "Tap the answer");
        }
        tft.setTextColor(YELLOW, Ui::bg());
        tft.drawString(prompt, cx, static_cast<int16_t>(r.y + 1), 2);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    char lines[2][Ui::WRAP_LINE_MAX];
    const uint8_t needed = Ui::wrapLines(tft, q_.fact, r.w, 1, lines, 2);
    const uint8_t shown = min<uint8_t>(needed, 2);
    tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
    for (uint8_t i = 0; i < shown; ++i) {
        tft.drawString(lines[i], cx, static_cast<int16_t>(r.y + i * 10), 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void SpaceGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (!drawnQuestion_) {
        drawQuestion(tft);
        drawnQuestion_ = true;
    }

    /* Level and Best only move with a new question or a new record, but Score
     * and Streak move on every answer and Streak RESETS -- the largest shrink
     * this screen does -- so both ends are cleared before either is written,
     * and the right-hand rect is measured leftward from the margin. */
    if (!drawnHeader_ || score_ != drawnScore_ || streak_ != drawnStreak_) {
        tft.fillRect(10, 33, 150, 36, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 10 - 150, 33, 150, 36, Ui::bg());
        char buf[32];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(level()));
        tft.drawString(buf, 10, 35, 2);
        snprintf(buf, sizeof(buf), "Correct %u", static_cast<unsigned>(score_));
        tft.drawString(buf, 10, 52, 1);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Streak %u", static_cast<unsigned>(streak_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 35, 2);
        if (best_ > 0) {
            snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(best_));
        } else {
            snprintf(buf, sizeof(buf), "Best --");
        }
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 52, 1);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnStreak_ = streak_;
        drawnHeader_ = true;
    }

    /* Only the buttons whose colour changed. Ui::drawButton fills its rect
     * opaquely, so a recolour erases what was under it. */
    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t state = 0;
        if (answered_) {
            if (i == correctButton_) state = 1;
            else if (i == selected_) state = 2;
        }
        if (state == drawnButton_[i]) {
            continue;
        }
        const uint16_t fill = state == 1 ? GREEN : (state == 2 ? RED : BLUE);
        const uint16_t ink = state == 0 ? TFT_WHITE : TFT_BLACK;
        const Rect r = answerRect(i);
        const char* label = q_.options[order_[i]];
        /* An option like "Carbon dioxide" is wider than the button at font 2.
         * Measured rather than guessed: TFT_eSPI cuts a string at the viewport
         * edge with no mark, mid-word, and gen_screens.py cannot reproduce
         * that -- so a label that overflows looks fine in every mock-up. */
        const uint8_t font = tft.textWidth(label, 2) <= r.w - 10 ? 2 : 1;
        Ui::drawButton(tft, r, label, fill, TFT_DARKGREY, ink, false, font);
        drawnButton_[i] = state;
    }

    const uint8_t note = answered_ ? (selected_ == correctButton_ ? 1 : 2) : 0;
    if (note != drawnNote_) {
        drawNote(tft);
        drawnNote_ = note;
    }
}
