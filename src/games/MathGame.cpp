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

#include <esp_system.h>   // esp_random(), for dealing the questions

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

/* The text the course writes, against the room this screen gives it.
 * MathCourse.h measures its widest equation, option and level name against
 * these widths; here they are held to the layout that actually draws them,
 * so neither side can move without the other noticing. */
constexpr int16_t BUTTON_W = 132;
constexpr int16_t PROMPT_X = 20;
constexpr int16_t PROMPT_W = GAME_CANVAS_WIDTH - 2 * PROMPT_X;
static_assert(PANEL_W - 16 >= MathCourse::QUESTION_TEXT_W, "the question panel is narrower than the course allows for");
static_assert(PROMPT_W >= MathCourse::PROMPT_W, "the prompt strip is narrower than the longest level name");
static_assert(BUTTON_W - 12 >= MathCourse::LABEL_W, "an answer button is narrower than the largest option");
static_assert(MathCourse::EQUATION_CHARS < 24, "the equation buffer in drawQuestion() is too small");

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
    /* The level is the player's, not the visit's: it comes back from the
     * runtime, and New game on the Carry on card starts the score again here
     * without moving it. The meter under it starts over every visit, which
     * is Course::Ladder's own rule. */
    ladder_.start(MathCourse::LEVEL_COUNT, host.courseLevel());
    startLevel();
    announceNext_ = 0;
    newQuestion();
    markDirty();
}

/* Unchanged by the level being kept: with no correct answer yet there is
 * still nothing here a player would want back -- the question on screen is
 * one of many, and the level is waiting for them on the way in either way. */
Game::Keep MathGame::keepsGame() const {
    return score_ > 0 ? Keep::Ask : Keep::No;
}

/* The clock is the visit's, and it is also half of the best record: time spent
 * away -- on the card, in another game, asleep -- must not be counted, or the
 * next correct answer writes a slower time than the player took. */
void MathGame::resume(AppContext&, uint32_t awayMs) {
    movedOn(startedAt_, awayMs);
}

/* A new level, or a new visit to one: the deck starts a fresh pass, and the
 * recent list forgets the questions of the level that was left. */
void MathGame::startLevel() {
    deck_.reset(MathCourse::poolSize(ladder_.level()));
    recent_.reset();
}

Rect MathGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152),
                static_cast<int16_t>(BUTTON_TOP + row * 46), BUTTON_W, 38};
}

/* Held at its largest rather than wrapped. A game kept to carry on can be
 * open for a day, and eighteen hours wrapping to 0:00 would read as the
 * fastest time ever and write it into the record. */
uint16_t MathGame::elapsedSeconds() const {
    const uint32_t seconds = (millis() - startedAt_) / 1000UL;
    return seconds > 0xFFFFUL ? static_cast<uint16_t>(0xFFFF) : static_cast<uint16_t>(seconds);
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

/* Deals the next question from the player's level, and everything that goes
 * with it, once: the options, the story if it is one, and the invalidation.
 * Nothing about the question is worked out again per frame. */
void MathGame::newQuestion() {
    const uint8_t level = ladder_.level();
    selected_ = -1;
    answered_ = false;
    announce_ = announceNext_;
    announceNext_ = 0;

    if (MathCourse::numbered(level)) {
        item_ = deck_.next(esp_random());
        MathCourse::makeNumbered(level, item_, question_);
    } else {
        /* Hundreds of questions or more, so a repeat is unlikely rather than
         * impossible; drawing again while the question is one of the last
         * ten makes it vanishingly rare, and twenty tries cannot spin. */
        uint32_t token = 0;
        for (uint8_t tries = 0; tries < 20; ++tries) {
            MathCourse::makeDrawn(level, esp_random(), question_);
            token = MathCourse::token(level, question_);
            if (!recent_.recentlyUsed(token)) break;
        }
        recent_.remember(token);
    }
    MathCourse::makeOptions(level, question_, esp_random());

    /* About a third of the questions a story can tell, so a run of them
     * never becomes a reading test. Which questions those are is the
     * course's to say: never before level 4, never a missing number, and
     * never with one of anything, which is where the grammar went wrong. */
    wordProblem_ = MathCourse::storyFits(level, question_) && esp_random() % 3 == 0;
    if (wordProblem_) {
        MathCourse::tellStory(question_, esp_random(), problem_, sizeof(problem_));
    }

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
     * clock would sit still for longer than it used to, and a step down
     * would not show until the next answer. Repainting two small rects per
     * question keeps that behaviour as it was. */
    drawnQuestion_ = false;
    drawnHeader_ = false;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
}

/* One answer has moved the ladder. Up is said out loud -- the flourish now,
 * and the new level's name in the strip over its first question. Down says
 * nothing at all: the questions get easier, and that is the whole of it. */
void MathGame::climb(AppContext& host, Course::Ladder::Step step) {
    if (step == Course::Ladder::Step::Stay) return;
    host.setCourseLevel(ladder_.level());
    startLevel();
    if (step == Course::Ladder::Step::Up) {
        announceNext_ = ladder_.level();
        /* The colour by hand, since playSound() does not pulse the LED. */
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::LevelUp);
    } else {
        announceNext_ = 0;
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
            selected_ = static_cast<int8_t>(i);
            answered_ = true;
            const bool right = i == question_.correct;
            if (right) {
                ++score_;
                ++streak_;
                updateBest(host);
            } else {
                streak_ = 0;
                /* Before the ladder moves: a step down resets the deck, and
                 * the item number belongs to the level it was dealt from.
                 * A drawn level has no numbers to bring back. */
                if (MathCourse::numbered(ladder_.level())) {
                    deck_.missed(item_);
                }
            }
            /* One answer, one move: there is no second try on this screen,
             * so the first press is the only one the ladder hears. */
            const Course::Ladder::Step step = ladder_.answer(right);
            if (step != Course::Ladder::Step::Up) {
                if (right) host.beepOk();
                else host.beepError();
            }
            climb(host, step);
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
        MathCourse::equation(question_, equation, sizeof(equation));
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
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(ladder_.level()));
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
            if (i == question_.correct) state = 1;
            else if (i == selected_) state = 2;
        }
        if (state == drawnButton_[i]) {
            continue;
        }
        const uint16_t fill = state == 1 ? GREEN : (state == 2 ? RED : BLUE);
        const uint16_t text = state == 0 ? TFT_WHITE : TFT_BLACK;
        char label[12];
        snprintf(label, sizeof(label), "%d", static_cast<int>(question_.options[i]));
        Ui::drawButton(tft, answerRect(i), label, fill, TFT_DARKGREY, text, false, 4);
        drawnButton_[i] = state;
    }

    /* "Tap the answer" and "Green is correct - tap next" are different lengths,
     * so the line is cleared before either is written. The strip starts one row
     * below the panel and stops one row above the buttons -- both derived, so
     * neither edge can eat into its neighbour the way this one used to.
     *
     * The first question of a level just reached names the level here in
     * place of "Tap the answer": the place a player is already looking, and
     * gone with the next question. The longest name is measured against this
     * strip by the course's own test. */
    if (answered_ != drawnAnswered_) {
        tft.fillRect(PROMPT_X, PROMPT_Y, PROMPT_W, PROMPT_H, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        const int16_t cy = static_cast<int16_t>(PROMPT_Y + PROMPT_H / 2);
        const bool right = selected_ == static_cast<int8_t>(question_.correct);
        if (answered_) {
            tft.setTextColor(right ? GREEN : RED, Ui::bg());
            tft.drawString(right ? "Correct - tap for next" : "Green is correct - tap next",
                           GAME_CANVAS_WIDTH / 2, cy, 2);
        } else if (announce_ != 0) {
            char line[MathCourse::LEVEL_LINE_MAX];
            MathCourse::levelLine(announce_, line, sizeof(line));
            tft.setTextColor(YELLOW, Ui::bg());
            tft.drawString(line, GAME_CANVAS_WIDTH / 2, cy, 2);
        } else {
            tft.setTextColor(YELLOW, Ui::bg());
            tft.drawString("Tap the answer", GAME_CANVAS_WIDTH / 2, cy, 2);
        }
        tft.setTextDatum(TL_DATUM);
        drawnAnswered_ = answered_;
    }
}
