// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "MathGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;

/* The vertical stack, written once and derived from rather than typed in
 * twice.
 *
 * This is not tidiness. The panel used to be 76..129 and the prompt strip was
 * cleared from y=125, so every answer erased the bottom five rows of the
 * question panel -- its rounded border included -- and nothing put them back
 * until the next question. It was there from the day the screen was split, on
 * every board, and it is invisible in docs/screens/ because a mock-up draws
 * elements in isolation with no clear rectangles at all. The static_asserts
 * below are what stop the next person reintroducing it by moving one number. */
constexpr int16_t PANEL_X = 26;
constexpr int16_t PANEL_Y = 70;
constexpr int16_t PANEL_W = 268;
constexpr int16_t PANEL_H = 56;
constexpr int16_t BUTTON_TOP = 144;
constexpr int16_t PROMPT_Y = PANEL_Y + PANEL_H + 1;
constexpr int16_t PROMPT_H = BUTTON_TOP - PROMPT_Y - 1;
static_assert(PROMPT_Y > PANEL_Y + PANEL_H - 1,
              "the prompt strip would erase the question panel's bottom border");
static_assert(PROMPT_H >= 16, "the prompt strip is too short for a font 2 line");

constexpr AppScoreInfo MATH_SCORE = {
    "math", "Math", "mathBest", "pts", false
};

constexpr AppMetadata MATH_METADATA = {
    "math",
    "Math",
    nullptr,
    "sums and word problems",
    "Math",
    "Sums and word problems, timed.",
    &MATH_SCORE,
    LauncherIcon::Math,
    9,
    true,
};

/* Names and things to count.
 *
 * Short, because the whole problem has to wrap into three lines of font 2, and
 * from as many places as a classroom has young players in it. No pronouns anywhere
 * in the templates: "Ava has 7 shells and finds 5 more" needs none, and a
 * pronoun would be a guess about a made-up young player that the sentence does not
 * need to make. */
const char* const NAMES[] = {
    "Ava", "Ben", "Chen", "Dara", "Eli", "Farah", "Gus", "Hana",
    "Ines", "Jai", "Kofi", "Lena", "Mo", "Nia", "Omar", "Priya",
    "Rosa", "Sam", "Tess", "Uma", "Wren", "Yusuf", "Zara", "Iris",
};
constexpr uint8_t NAME_COUNT = sizeof(NAMES) / sizeof(NAMES[0]);

const char* const THINGS[] = {
    "apples", "marbles", "stickers", "pencils", "shells", "buttons",
    "grapes", "blocks", "coins", "leaves", "beads", "crayons",
    "acorns", "stamps", "cherries", "pebbles",
};
constexpr uint8_t THING_COUNT = sizeof(THINGS) / sizeof(THINGS[0]);
}

const AppMetadata& mathAppMetadata() {
    return MATH_METADATA;
}

const char* MathGame::title() const {
    return mathAppMetadata().title;
}

void MathGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestCorrect_ = static_cast<uint16_t>(host.getScore(mathAppMetadata().score->bestKey, 0));
    bestSeconds_ = static_cast<uint16_t>(host.getScore("mathTime", 0));
    startedAt_ = millis();
    newQuestion();
    markDirty();
}

uint8_t MathGame::level() const {
    return min<uint8_t>(5, 1 + score_ / 5);
}

Rect MathGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152),
                static_cast<int16_t>(BUTTON_TOP + row * 46), 132, 38};
}

bool MathGame::optionExists(int16_t value, uint8_t upTo) const {
    for (uint8_t i = 0; i < upTo; ++i) {
        if (options_[i] == value) {
            return true;
        }
    }
    return false;
}

uint16_t MathGame::elapsedSeconds() const {
    return static_cast<uint16_t>((millis() - startedAt_) / 1000UL);
}

/* Into the caller's buffer rather than out of a String. It is called twice per
 * header repaint, and a header repaints on every answer -- which is the churn
 * the memory rule is about, and what docs/RENDER_AUDIT.md asked for here. */
void MathGame::formatSeconds(uint16_t seconds, char* out, size_t len) const {
    snprintf(out, len, "%u:%02u", seconds / 60U, seconds % 60U);
}

void MathGame::updateBest(AppContext& host) {
    const uint16_t elapsed = elapsedSeconds();
    if (score_ > bestCorrect_ || (score_ == bestCorrect_ && (bestSeconds_ == 0 || elapsed < bestSeconds_))) {
        bestCorrect_ = score_;
        bestSeconds_ = elapsed;
        host.setScore(mathAppMetadata().score->bestKey, bestCorrect_);
        host.setScore("mathTime", bestSeconds_);
    }
}

/* The sum already exists by the time this runs -- left_, right_, operation_
 * and answer_ are decided first and are not touched here. A word problem is a
 * way of ASKING the question, not a different question, which is what keeps
 * the four answer buttons and the whole scoring path identical. */
void MathGame::makeWordProblem() {
    const char* who = NAMES[random(NAME_COUNT)];
    const char* thing = THINGS[random(THING_COUNT)];
    const unsigned a = static_cast<unsigned>(left_);
    const unsigned b = static_cast<unsigned>(right_);

    if (operation_ == Operation::Add) {
        switch (random(3)) {
            case 0:
                snprintf(problem_, sizeof(problem_),
                         "%s has %u %s and finds %u more. How many now?",
                         who, a, thing, b);
                break;
            case 1:
                snprintf(problem_, sizeof(problem_),
                         "There are %u %s in a box. %u more go in. How many in all?",
                         a, thing, b);
                break;
            default:
                snprintf(problem_, sizeof(problem_),
                         "%s picks %u %s on Monday and %u on Tuesday. How many?",
                         who, a, thing, b);
                break;
        }
        return;
    }

    switch (random(3)) {
        case 0:
            snprintf(problem_, sizeof(problem_),
                     "%s has %u %s and gives %u away. How many are left?",
                     who, a, thing, b);
            break;
        case 1:
            snprintf(problem_, sizeof(problem_),
                     "%u %s are on the table. %u are taken away. How many stay?",
                     a, thing, b);
            break;
        default: {
            /* Two names, and they must differ or the question reads as
             * nonsense. Walking forward one slot is cheaper than a retry loop
             * and cannot spin. */
            uint8_t first = static_cast<uint8_t>(random(NAME_COUNT));
            uint8_t second = static_cast<uint8_t>((first + 1 + random(NAME_COUNT - 1)) % NAME_COUNT);
            snprintf(problem_, sizeof(problem_),
                     "%s has %u %s. %s has %u. How many more has %s?",
                     NAMES[first], a, thing, NAMES[second], b, NAMES[first]);
            break;
        }
    }
}

void MathGame::newQuestion() {
    const uint8_t currentLevel = level();
    selected_ = -1;
    answered_ = false;

    switch (currentLevel) {
        case 1:
            operation_ = Operation::Add;
            left_ = random(1, 10);
            right_ = random(1, 10);
            break;
        case 2:
            operation_ = random(2) == 0 ? Operation::Add : Operation::Subtract;
            left_ = random(2, 13);
            right_ = random(1, 10);
            break;
        case 3:
            operation_ = random(2) == 0 ? Operation::Add : Operation::Subtract;
            left_ = random(10, 30);
            right_ = random(1, 10);
            break;
        case 4:
            operation_ = random(2) == 0 ? Operation::Add : Operation::Subtract;
            left_ = random(10, 50);
            right_ = random(10, 50);
            break;
        default:
            operation_ = random(2) == 0 ? Operation::Add : Operation::Subtract;
            left_ = random(10, 100);
            right_ = random(10, 100);
            break;
    }

    if (operation_ == Operation::Subtract && right_ > left_) {
        const int16_t tmp = left_;
        left_ = right_;
        right_ = tmp;
    }
    answer_ = operation_ == Operation::Add ? left_ + right_ : left_ - right_;

    /* Level 1 is never a story. A player still learning that 3 + 4 is 7 is
     * being asked to read a sentence as well, and the reading is the harder
     * half at that stage -- so the words arrive once the arithmetic is steady,
     * and then about a third of the time, so a run of them never becomes a
     * reading test. */
    wordProblem_ = currentLevel >= 2 && random(3) == 0;
    if (wordProblem_) {
        makeWordProblem();
    }

    makeOptions();
    /* Everything a new question changes, invalidated together.
     *
     * The buttons matter more than they look. The loop below repaints a button
     * only when its *state* changed, and on a new question the two that were
     * never highlighted go from state 0 to state 0 -- so without this they
     * keep the previous question's numbers on them. The full repaint this
     * replaces hid that by resetting the trackers in renderStatic().
     *
     * The header is invalidated because it carries the elapsed clock and the
     * level. Its own condition only fires when the score or streak moves, and
     * a wrong answer with the streak already at 0 moves neither -- so the
     * clock would sit still for longer than it used to. Repainting two small
     * rects per question keeps that behaviour as it was. */
    drawnQuestion_ = false;
    drawnHeader_ = false;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
}

void MathGame::makeOptions() {
    correctButton_ = random(4);
    for (uint8_t i = 0; i < 4; ++i) {
        options_[i] = 0;
    }
    options_[correctButton_] = answer_;

    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctButton_) {
            continue;
        }
        int16_t candidate = answer_;
        uint8_t attempts = 0;
        while ((candidate == answer_ || optionExists(candidate, i)) && attempts < 40) {
            const int16_t spread = level() <= 2 ? 5 : (level() <= 4 ? 12 : 20);
            candidate = answer_ + random(-spread, spread + 1);
            if (candidate < 0) {
                candidate = abs(candidate);
            }
            ++attempts;
        }
        while (candidate == answer_ || optionExists(candidate, i)) {
            ++candidate;
        }
        options_[i] = candidate;
    }
}

void MathGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (answered_) {
        newQuestion();
        /* A new sum, four new options and the prompt back to "Tap the answer".
         * All of that is CONTENT: the equation panel, the four buttons and the
         * prompt line are in the same places, the same sizes, drawn over
         * themselves opaquely. Nothing about the layout changed, so this is
         * markDirty() -- it used to be markFullDirty(), which wiped and
         * repainted the whole 320x240 panel, top bar and battery read
         * included, between every question. */
        markDirty();
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            selected_ = i;
            answered_ = true;
            if (i == correctButton_) {
                ++score_;
                ++streak_;
                updateBest(host);
                host.beepOk();
            } else {
                streak_ = 0;
                host.beepError();
            }
            markDirty();
            return;
        }
    }
}

void MathGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;   // nothing painted there yet
    }
    drawnScore_ = 0xFFFF;
    drawnStreak_ = 0xFFFF;
    drawnAnswered_ = !answered_;   // force the feedback line on the first pass
    drawnHeader_ = false;
    drawnQuestion_ = false;        // and the equation
}

/* The question, either as a sum or as a story.
 *
 * It needs no erase of its own: the rect is fixed, and fillRoundRect covers it
 * opaquely before the text goes down, so a one-line sum cannot leave the third
 * line of a word problem behind it. If this panel is ever made to grow with
 * its content, that stops being true -- and PROMPT_Y is derived from its
 * bottom edge, so growing it moves the strip below rather than silently
 * overlapping it. */
void MathGame::drawQuestion(Ui::Renderer& tft) {
    tft.fillRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 8, Ui::panel());
    tft.drawRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 8, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());

    if (!wordProblem_) {
        char equation[24];
        const char symbol = operation_ == Operation::Add ? '+' : '-';
        snprintf(equation, sizeof(equation), "%d %c %d = ?",
                 static_cast<int>(left_), symbol, static_cast<int>(right_));
        tft.setTextDatum(MC_DATUM);
        tft.drawString(equation, GAME_CANVAS_WIDTH / 2,
                       static_cast<int16_t>(PANEL_Y + PANEL_H / 2), 4);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    /* Wrapped against the live font rather than a character count: the panel's
     * width is pixels, and "Ines" and "Yusuf" are not the same width. Three
     * lines of font 2 is the budget; a sentence that needs a fourth is asked
     * again at font 1 rather than losing its last few words, because the words
     * a word problem drops are usually the question. */
    const int16_t textW = PANEL_W - 16;
    char lines[4][Ui::WRAP_LINE_MAX];
    uint8_t font = 2;
    uint8_t needed = Ui::wrapLines(tft, problem_, textW, font, lines, 3);
    uint8_t cap = 3;
    if (needed > 3) {
        font = 1;
        cap = 4;
        needed = Ui::wrapLines(tft, problem_, textW, font, lines, 4);
    }
    const uint8_t shown = min<uint8_t>(needed, cap);
    const int16_t lineH = font == 2 ? 18 : 11;
    int16_t y = static_cast<int16_t>(PANEL_Y + (PANEL_H - shown * lineH) / 2);

    tft.setTextDatum(TC_DATUM);
    for (uint8_t i = 0; i < shown; ++i) {
        tft.drawString(lines[i], GAME_CANVAS_WIDTH / 2, y, font);
        y = static_cast<int16_t>(y + lineH);
    }
    tft.setTextDatum(TL_DATUM);
}

void MathGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    /* The question. It changes only with a new question, which is why it is
     * gated rather than drawn every frame -- but it is dynamic rather than
     * static, because a new question must not cost a full repaint. */
    if (!drawnQuestion_) {
        drawQuestion(tft);
        drawnQuestion_ = true;
    }

    /* Four counters, two of them TR_DATUM. Level and the clock only move with
     * a new question, but Correct and Streak change on an answer, and Streak
     * resets to 0 -- so the pair is cleared before either is written and the
     * right-hand rect is measured leftward from the margin, where a shrinking
     * right-aligned string leaves its stale characters. */
    if (!drawnHeader_ || score_ != drawnScore_ || streak_ != drawnStreak_) {
        tft.fillRect(10, 33, 150, 36, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 10 - 170, 33, 170, 36, Ui::bg());
        char buf[32];
        char clock[8];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(level()));
        tft.drawString(buf, 10, 35, 2);
        formatSeconds(elapsedSeconds(), clock, sizeof(clock));
        snprintf(buf, sizeof(buf), "Correct %u  %s", static_cast<unsigned>(score_), clock);
        tft.drawString(buf, 10, 52, 1);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Streak %u", static_cast<unsigned>(streak_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 35, 2);
        if (bestCorrect_ > 0) {
            formatSeconds(bestSeconds_, clock, sizeof(clock));
            snprintf(buf, sizeof(buf), "Best %u / %s", static_cast<unsigned>(bestCorrect_), clock);
        } else {
            snprintf(buf, sizeof(buf), "Best --");
        }
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 52, 1);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnStreak_ = streak_;
        drawnHeader_ = true;
    }

    /* Only the buttons whose colour changed. drawButton fills its rect
     * opaquely, so a recolour erases what was there and none of this needs a
     * clear of its own. */
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
        const uint16_t text = state == 0 ? TFT_WHITE : TFT_BLACK;
        char label[12];
        snprintf(label, sizeof(label), "%d", static_cast<int>(options_[i]));
        Ui::drawButton(tft, answerRect(i), label, fill, TFT_DARKGREY, text, false, 4);
        drawnButton_[i] = state;
    }

    /* "Tap the answer" and "Green is correct - tap next" are different lengths,
     * so the line is cleared before either is written. The strip starts one row
     * below the panel and stops one row above the buttons -- both derived, so
     * neither edge can eat into its neighbour the way this one used to. */
    if (answered_ != drawnAnswered_) {
        tft.fillRect(20, PROMPT_Y, GAME_CANVAS_WIDTH - 40, PROMPT_H, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        const int16_t cy = static_cast<int16_t>(PROMPT_Y + PROMPT_H / 2);
        if (answered_) {
            tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
            tft.drawString(selected_ == correctButton_ ? "Correct - tap for next"
                                                       : "Green is correct - tap next",
                           GAME_CANVAS_WIDTH / 2, cy, 2);
        } else {
            tft.setTextColor(YELLOW, Ui::bg());
            tft.drawString("Tap the answer", GAME_CANVAS_WIDTH / 2, cy, 2);
        }
        tft.setTextDatum(TL_DATUM);
        drawnAnswered_ = answered_;
    }
}
