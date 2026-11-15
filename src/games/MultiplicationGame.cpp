// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "MultiplicationGame.h"
#include "engine/AppRegistry.h"

#include <esp_system.h>   // esp_random(), for dealing and for the options

namespace MC = MultiplicationCourse;

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;

/* The vertical stack, written once and derived from rather than typed in
 * twice -- the same fix MathGame records. The panel used to be drawn on rows
 * 76..129 and the prompt strip cleared from row 125, after it, so the bottom
 * five rows of the panel and its rounded border were erased on every paint
 * and every answer. The panel's own numbers live in MultiplicationCourse.h,
 * where the host test measures every question against them. */
constexpr int16_t PANEL_X = MC::Screen::PANEL_X;
constexpr int16_t PANEL_Y = MC::Screen::PANEL_Y;
constexpr int16_t PANEL_W = MC::Screen::PANEL_W;
constexpr int16_t PANEL_H = MC::Screen::PANEL_H;
constexpr int16_t HEADER_Y = 33;
constexpr int16_t HEADER_H = 36;
constexpr int16_t BUTTON_TOP = 144;
constexpr int16_t BUTTON_W = MC::Screen::BUTTON_W;
constexpr int16_t PROMPT_Y = PANEL_Y + PANEL_H + 1;
constexpr int16_t PROMPT_H = BUTTON_TOP - PROMPT_Y - 1;
static_assert(MC::Screen::CANVAS_W == GAME_CANVAS_WIDTH,
              "the course measures its questions against a different canvas");
static_assert(HEADER_Y + HEADER_H <= PANEL_Y, "the header clear would erase the panel's top");
static_assert(PROMPT_Y > PANEL_Y + PANEL_H - 1,
              "the prompt strip would erase the question panel's bottom border");
static_assert(PROMPT_H >= 16, "the prompt strip is too short for a font 2 line");

/* Font 4's figures are all 14px wide, its space 5 and its "=" 14. The host
 * test measures every real question with the whole width table; these say
 * the widest possible ones fit, beside the code that draws them. */
constexpr int16_t FONT4_FIGURE = 14;
constexpr int16_t FONT4_SPACE = 5;
constexpr int16_t FONT4_EQUALS = 14;
static_assert(MC::MAX_SHOWN <= 999, "the labels and the sides are sized for three figures");
static_assert(3 * FONT4_FIGURE <= MC::Screen::LEFT_MAX_W, "144 does not fit left of the sign");
static_assert(5 * FONT4_FIGURE + 2 * FONT4_SPACE + FONT4_EQUALS <= MC::Screen::RIGHT_MAX_W,
              "\"12 = 144\" does not fit right of the sign");
static_assert(3 * FONT4_FIGURE <= BUTTON_W - 16, "three figures do not fit on a button");
/* How far apart the division sign's three bars are drawn. The whole sign must
 * stay inside the gap the two sides leave it. */
constexpr int16_t DIVIDE_BAR_STEP = 3;
constexpr int16_t FONT4_MINUS = 8;           // font 4's advance for '-'
static_assert(2 * DIVIDE_BAR_STEP + FONT4_MINUS < 2 * MC::Screen::OP_GAP,
              "the division sign would run into the numbers either side");

/* The course checks every pool against MAX_POOL; this ties that to the deck. */
static_assert(MC::MAX_POOL <= Course::QuestionDeck::MAX_ITEMS,
              "the course numbers more questions than the deck can deal");

constexpr AppScoreInfo MULTIPLICATION_SCORE = {
    "multiply", "Multiply", "multBest", "pts", false
};

constexpr AppMetadata MULTIPLICATION_METADATA = {
    "multiply",
    "Multiply",
    "Multiplication",
    "times tables",
    "Multiply",
    "Times tables, then division.",
    &MULTIPLICATION_SCORE,
    LauncherIcon::Multiplication,
    10,
    true,
};
}

const AppMetadata& multiplicationAppMetadata() {
    return MULTIPLICATION_METADATA;
}

const char* MultiplicationGame::title() const {
    return multiplicationAppMetadata().screenTitle != nullptr
        ? multiplicationAppMetadata().screenTitle
        : multiplicationAppMetadata().title;
}

/* A new visit, or New game from the Carry on card: the score and the streak
 * start over, the level does not. It is the player's, kept by the runtime,
 * and it used to be worked out of this visit's score -- so every visit began
 * with the twos, however well the player knew their nines. */
void MultiplicationGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(multiplicationAppMetadata().score->bestKey, 0));
    ladder_.start(MC::LEVEL_COUNT, host.courseLevel());
    deck_.reset(MC::poolSize(ladder_.level()));
    announce_ = false;
    newQuestion();
    markDirty();
}

Game::Keep MultiplicationGame::keepsGame() const {
    return score_ > 0 ? Keep::Ask : Keep::No;
}

Rect MultiplicationGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152),
                static_cast<int16_t>(BUTTON_TOP + row * 46), BUTTON_W, 38};
}

void MultiplicationGame::updateBest(AppContext& host) {
    if (streak_ > bestStreak_) {
        bestStreak_ = streak_;
        host.setScore(multiplicationAppMetadata().score->bestKey, bestStreak_);
    }
}

void MultiplicationGame::newQuestion() {
    selected_ = -1;
    answered_ = false;
    questionLevel_ = ladder_.level();
    /* Every level is small enough to number, so the deck deals all of it
     * before anything comes back; nothing here needs RecentQuestions. */
    item_ = deck_.next(esp_random());
    question_ = MC::makeQuestion(questionLevel_, item_, esp_random());
    banner_ = announce_;
    announce_ = false;
    /* Everything a new question changes, together. The buttons are not
     * optional: the loop below repaints one only when its state changed, and
     * the two that were never highlighted go 0 -> 0, so without this they keep
     * the previous question's numbers. The header carries the level and is
     * cheap. */
    drawnQuestion_ = false;
    drawnHeader_ = false;
    drawnPrompt_ = false;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
}

void MultiplicationGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (answered_) {
        newQuestion();
        /* A new question, four new options and the prompt back to its first
         * wording. All of it is content in fixed places, drawn over itself
         * opaquely -- nothing about the layout changes -- so this is
         * markDirty(). It used to wipe and repaint the whole panel, top bar
         * and battery read included, between every question. */
        markDirty();
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        selected_ = static_cast<int8_t>(i);
        answered_ = true;
        const bool right = i == question_.correct;
        if (right) {
            ++score_;
            ++streak_;
            updateBest(host);
        } else {
            streak_ = 0;
            /* Before the ladder moves: a step down resets the deck, and the
             * item number belongs to the level it was dealt from. */
            deck_.missed(item_);
        }
        /* One answer, one move. There is no second try on this screen, so
         * the first press is the only one the ladder ever hears about. */
        const Course::Ladder::Step step = ladder_.answer(right);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
            deck_.reset(MC::poolSize(ladder_.level()));
        }
        if (step == Course::Ladder::Step::Up) {
            /* The level flourish in place of the plain tick, and the colour
             * by hand, since playSound() does not pulse the LED. A step down
             * says nothing at all: the questions get easier, and that is the
             * whole of it. */
            announce_ = true;
            host.pulseRgb(0, 255, 40, 450);
            host.playSound(Sound::LevelUp);
        } else if (right) {
            host.beepOk();
        } else {
            host.beepError();
        }
        markDirty();
        return;
    }
}

void MultiplicationGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnStreak_ = 0xFFFF;
    drawnLevel_ = 0;
    drawnPrompt_ = false;
    drawnHeader_ = false;
    drawnQuestion_ = false;
}

/* The equation, hung off a fixed operator (see MC::Screen). Font 4 has no
 * division sign, but it has a colon and a minus, and the minus already sits
 * between the colon's two dots: drawn one over the other, without a
 * background so the second does not wipe the first, they are one. */
void MultiplicationGame::drawEquation(Ui::Renderer& tft) const {
    char left[MC::LEFT_SIZE];
    char right[MC::RIGHT_SIZE];
    question_.sides(left, sizeof(left), right, sizeof(right));
    const int16_t cy = static_cast<int16_t>(PANEL_Y + PANEL_H / 2);

    tft.fillRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 8, Ui::panel());
    tft.drawRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 8, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MR_DATUM);
    tft.drawString(left, MC::Screen::OP_X - MC::Screen::OP_GAP, cy, 4);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(right, MC::Screen::OP_X + MC::Screen::OP_GAP, cy, 4);
    tft.setTextDatum(MC_DATUM);
    if (question_.op == MC::Op::Times) {
        tft.drawString("x", MC::Screen::OP_X, cy, 4);
    } else {
        /* The bar three times, a few pixels apart: font 4's minus alone is
         * barely wider than the colon's dots, and on the panel the sign
         * read as a colon with a smudge in it. */
        tft.setTextColor(Ui::text());
        tft.drawString(":", MC::Screen::OP_X, cy, 4);
        for (int16_t dx = -DIVIDE_BAR_STEP; dx <= DIVIDE_BAR_STEP; dx += DIVIDE_BAR_STEP) {
            tft.drawString("-", static_cast<int16_t>(MC::Screen::OP_X + dx), cy, 4);
        }
    }
    tft.setTextDatum(TL_DATUM);
}

void MultiplicationGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    /* The question. Gated because it changes only with a new question, but
     * dynamic rather than static because a new question must not cost a full
     * repaint. The panel is filled opaquely before the text goes down, so a
     * shorter question cannot leave a tail. */
    if (!drawnQuestion_) {
        drawEquation(tft);
        drawnQuestion_ = true;
    }

    /* Score and Streak move on an answer, and Streak resets to zero; the
     * level moves with the question after a climb. The right-hand pair is
     * TR_DATUM, so a shrinking string leaves its stale characters at the
     * LEFT end -- that rect is measured leftward from the margin rather than
     * sized around where the text starts. */
    if (!drawnHeader_ || score_ != drawnScore_ || streak_ != drawnStreak_ ||
        questionLevel_ != drawnLevel_) {
        tft.fillRect(10, HEADER_Y, 150, HEADER_H, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 10 - 160, HEADER_Y, 160, HEADER_H, Ui::bg());
        char buf[28];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(questionLevel_));
        tft.drawString(buf, 10, 35, 2);
        snprintf(buf, sizeof(buf), "Score %u", static_cast<unsigned>(score_));
        tft.drawString(buf, 10, 52, 1);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Streak %u", static_cast<unsigned>(streak_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 35, 2);
        if (bestStreak_ > 0) {
            snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(bestStreak_));
        } else {
            snprintf(buf, sizeof(buf), "Best --");
        }
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 52, 1);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnStreak_ = streak_;
        drawnLevel_ = questionLevel_;
        drawnHeader_ = true;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t state = 0;
        if (answered_) {
            if (i == question_.correct) state = 1;
            else if (i == selected_) state = 2;
        }
        if (state == drawnButton_[i]) {
            continue;
        }
        const uint16_t fill = state == 1 ? GREEN : (state == 2 ? RED : BLUE);
        const uint16_t text = state == 0 ? TFT_WHITE : TFT_BLACK;
        char label[12];
        snprintf(label, sizeof(label), "%u", static_cast<unsigned>(question_.options[i]));
        Ui::drawButton(tft, answerRect(i), label, fill, TFT_DARKGREY, text, false, 4);
        drawnButton_[i] = state;
    }

    /* The strip between the panel and the buttons, derived from both, so it
     * can reach neither. Before an answer it says what to tap -- or, on the
     * first question after a climb, which level this is now. */
    if (!drawnPrompt_ || answered_ != drawnAnswered_) {
        tft.fillRect(MC::Screen::PROMPT_X, PROMPT_Y, MC::Screen::PROMPT_W, PROMPT_H, Ui::bg());
        const int16_t cy = static_cast<int16_t>(PROMPT_Y + PROMPT_H / 2);
        tft.setTextDatum(MC_DATUM);
        if (answered_) {
            tft.setTextColor(selected_ == question_.correct ? GREEN : RED, Ui::bg());
            tft.drawString(selected_ == question_.correct ? "Correct - tap for next"
                                                          : "Green is correct - tap next",
                           GAME_CANVAS_WIDTH / 2, cy, 2);
        } else if (banner_) {
            char banner[MC::BANNER_SIZE];
            MC::levelBanner(banner, sizeof(banner), questionLevel_);
            tft.setTextColor(YELLOW, Ui::bg());
            tft.drawString(banner, GAME_CANVAS_WIDTH / 2, cy, 2);
        } else {
            tft.setTextColor(YELLOW, Ui::bg());
            tft.drawString(question_.prompt(), GAME_CANVAS_WIDTH / 2, cy, 2);
        }
        tft.setTextDatum(TL_DATUM);
        drawnAnswered_ = answered_;
        drawnPrompt_ = true;
    }
}
