// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "TimeGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t CLOCK_FACE = 0xF7BE;
constexpr uint16_t HAND = 0x0843;
constexpr uint16_t MINUTE_HAND = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;

/* The dial, at file scope because two functions need to agree about where it
 * is. drawClock() draws it; renderDynamic() has to clear the score header
 * without erasing it, and for a long time it did not -- see HEADER_W. */
constexpr int16_t CLOCK_CX = GAME_CANVAS_WIDTH / 2;
constexpr int16_t CLOCK_CY = 86;
constexpr int16_t CLOCK_R = 43;
/* drawClock() fills one ring wider than the face, to cover the previous
 * hands. That ring is what the header has to stay clear of, not the face. */
constexpr int16_t CLOCK_OUTER = CLOCK_R + 4;

/* The score header: "Level/Score" top left, "Streak/Best" top right, each
 * cleared to background before being rewritten.
 *
 * The width is derived, and this is the whole bug. Both strips were a
 * hand-typed 140px wide -- far wider than the text needs -- so they reached
 * x=150 and x=170 while the dial spans 113..207. Every time the score changed,
 * 37px was erased off each shoulder of the clock, leaving the face as a narrow
 * vertical strip with two square bites out of it. renderDynamic() draws the
 * clock first and the header second, so this happened on the very first frame
 * and on every board; the 4-inch panel made it 48px a side and unmissable.
 *
 * Deriving the width from the dial means the two cannot drift apart again. If
 * a longer score ever needs more room, the dial has to give it up explicitly
 * rather than by being quietly painted over. */
constexpr int16_t HEADER_MARGIN = 10;
constexpr int16_t HEADER_Y = 33;
constexpr int16_t HEADER_H = 36;
/* A few pixels of daylight rather than exact adjacency. The two are scaled
 * independently on a panel that is not the canvas size -- positions by the
 * axis scale, the dial's radius by the smaller of the two -- and each rounds
 * on its own. Touching exactly would be correct arithmetic that one rounding
 * change turns back into the bug above. */
constexpr int16_t HEADER_GAP = 3;
constexpr int16_t HEADER_W = CLOCK_CX - CLOCK_OUTER - HEADER_MARGIN - HEADER_GAP;
static_assert(HEADER_W > 90,
              "The score header needs room for 'Streak 0' at font 2. If the "
              "dial has grown enough to squeeze it, move the dial down or "
              "shrink it -- do not let the header clear over it again.");

/* The question line, between the dial's outer ring (133) and the top of the
 * buttons (152). */
constexpr Rect PROMPT_RECT = {8, 136, 304, 16};
static_assert(CLOCK_CY + CLOCK_OUTER < 136, "the question line must clear the dial");

constexpr AppScoreInfo TIME_GAME_SCORE = {
    "time", "Time", "timeBest", "pts", false
};

constexpr AppMetadata TIME_GAME_METADATA = {
    "time",
    "Time",
    nullptr,
    "read clocks",
    "Time",
    "Read the clock and pick the time.",
    &TIME_GAME_SCORE,
    LauncherIcon::Time,
    11,
    true,
};
}

const AppMetadata& timeGameAppMetadata() {
    return TIME_GAME_METADATA;
}

const char* TimeGame::title() const {
    return timeGameAppMetadata().title;
}

/* A new visit, or New game from the Carry on card: the score and the streak
 * start over, the level does not. It is the player's, kept by the runtime;
 * it used to be worked out of this visit's score, so every visit began at
 * o'clock however well the player read a clock. */
void TimeGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(timeGameAppMetadata().score->bestKey, 0));
    ladder_.start(TimeCourse::LEVEL_COUNT, host.courseLevel());
    startLevel();
    announce_ = false;
    newQuestion();
    markDirty();
}

/* One right answer is a score worth coming back to. Nothing here runs on
 * the clock, and the full repaint on the way back resets every record
 * of what is drawn in renderStatic(), so there is no resume() to write. */
Game::Keep TimeGame::keepsGame() const {
    return score_ > 0 ? Keep::Ask : Keep::No;
}

Rect TimeGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152), static_cast<int16_t>(152 + row * 40), 132, 34};
}

/* A new level is a new deck, and a new history for the drawn ones. */
void TimeGame::startLevel() {
    deck_.reset(TimeCourse::poolSize(ladder_.level()));
    recent_.reset();
}

void TimeGame::newQuestion() {
    /* New hands on the clock. drawClock() opens with a fillCircle covering the
     * whole face, so the old hands cannot survive it -- which is what lets the
     * clock be dynamic and a new question cost a repaint of the face rather
     * than of the screen.
     *
     * The buttons are not optional here: the loop repaints one only when its
     * state changed, and the two that were never highlighted go 0 -> 0, so
     * without this they would keep the previous question's times on them. */
    markDirty();
    drawnClock_ = false;
    drawnHeader_ = false;
    drawnPrompt_ = false;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    selected_ = -1;
    answered_ = false;
    banner_ = announce_;
    announce_ = false;

    questionLevel_ = ladder_.level();
    if (TimeCourse::numbered(questionLevel_)) {
        item_ = deck_.next(esp_random());
        TimeCourse::makeNumbered(questionLevel_, item_, esp_random(), question_);
    } else {
        for (uint8_t tries = 0; tries < 20; ++tries) {
            TimeCourse::makeDrawn(questionLevel_, esp_random(), question_);
            if (!recent_.recentlyUsed(TimeCourse::token(question_))) break;
        }
        recent_.remember(TimeCourse::token(question_));
    }
}

void TimeGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (answered_) {
        newQuestion();
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
            if (host.saveBestScore(timeGameAppMetadata().score->bestKey, streak_, false)) {
                bestStreak_ = streak_;
            }
        } else {
            streak_ = 0;
            /* Before the ladder moves: a step resets the deck, and the item
             * number belongs to the level that dealt it. */
            if (TimeCourse::numbered(questionLevel_)) deck_.missed(item_);
        }
        /* One try, so the first press is the only one the ladder hears. */
        const Course::Ladder::Step step = ladder_.answer(right);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
            startLevel();
        }
        if (step == Course::Ladder::Step::Up) {
            /* The flourish in place of the tick, and the colour by hand,
             * since playSound() does not pulse the LED. A step down says
             * nothing: the questions get easier, and that is the whole of it. */
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

void TimeGame::drawClock(Ui::Renderer& tft) const {
    /* The vertical budget on this screen is tight and was overdrawn.
     *
     * The answer buttons start at y=152 and the question label is 16px tall, so
     * everything the clock occupies has to end by 136. At cy=91 with radius 49
     * the outer fill reached y=144 and printed the top of "Which time is
     * shown?" out of existence -- worst at the horizontal centre, where a
     * circle reaches lowest and the centred sentence has its middle. On the
     * 4-inch panel it swallowed the line almost whole.
     *
     * cy=86 with radius 43 puts the outer fill at 39..133, clear of the label
     * at 136 with three rows to spare. Everything drawn against the face --
     * numerals, tick lengths, hands -- is proportional to the radius and moved
     * with it; if you change one, change them all, or the hands leave the
     * dial. */
    constexpr int16_t cx = CLOCK_CX;
    constexpr int16_t cy = CLOCK_CY;
    constexpr int16_t radius = CLOCK_R;
    constexpr int16_t numeralInset = 32;   // was 36 at radius 49

    tft.fillCircle(cx, cy, CLOCK_OUTER, Ui::surface());
    tft.fillCircle(cx, cy, radius, CLOCK_FACE);
    tft.drawCircle(cx, cy, radius, Ui::outline());

    tft.setTextColor(HAND, CLOCK_FACE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("12", cx, cy - numeralInset, 2);
    tft.drawString("3", cx + numeralInset, cy, 2);
    tft.drawString("6", cx, cy + numeralInset, 2);
    tft.drawString("9", cx - numeralInset, cy, 2);

    for (uint8_t i = 0; i < 12; ++i) {
        const float angle = -PI / 2.0f + i * PI / 6.0f;
        const int16_t x1 = cx + static_cast<int16_t>(cosf(angle) * (radius - 8));
        const int16_t y1 = cy + static_cast<int16_t>(sinf(angle) * (radius - 8));
        const int16_t x2 = cx + static_cast<int16_t>(cosf(angle) * (radius - 3));
        const int16_t y2 = cy + static_cast<int16_t>(sinf(angle) * (radius - 3));
        tft.drawLine(x1, y1, x2, y2, HAND);
    }
    /* From "Any minute" on, the sixty minutes are marked round the rim:
     * counting on from a numeral needs something to count. Short, and
     * outside the numerals, so the face still reads at a glance. */
    if (question_.minuteMarks) {
        for (uint8_t i = 0; i < 60; ++i) {
            if (i % 5 == 0) continue;
            const float angle = -PI / 2.0f + i * PI / 30.0f;
            tft.drawLine(cx + static_cast<int16_t>(cosf(angle) * (radius - 5)),
                         cy + static_cast<int16_t>(sinf(angle) * (radius - 5)),
                         cx + static_cast<int16_t>(cosf(angle) * (radius - 2)),
                         cy + static_cast<int16_t>(sinf(angle) * (radius - 2)), HAND);
        }
    }

    const uint8_t hour = question_.shown / 60;
    const uint8_t minute = question_.shown % 60;
    const float hourAngle = -PI / 2.0f + (hour + minute / 60.0f) * PI / 6.0f;
    const float minuteAngle = -PI / 2.0f + minute * PI / 30.0f;
    const int16_t hx = cx + static_cast<int16_t>(cosf(hourAngle) * 22);
    const int16_t hy = cy + static_cast<int16_t>(sinf(hourAngle) * 22);
    const int16_t mx = cx + static_cast<int16_t>(cosf(minuteAngle) * 32);
    const int16_t my = cy + static_cast<int16_t>(sinf(minuteAngle) * 32);
    tft.drawLine(cx, cy, hx, hy, HAND);
    tft.drawLine(cx + 1, cy, hx + 1, hy, HAND);
    tft.drawLine(cx, cy, mx, my, MINUTE_HAND);
    tft.fillCircle(cx, cy, 4, HAND);
    tft.setTextDatum(TL_DATUM);
}

void TimeGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnStreak_ = 0xFFFF;
    drawnAnswered_ = !answered_;
    drawnHeader_ = false;
    drawnLevel_ = 0;
    drawnPrompt_ = false;
    drawnClock_ = false;
}

/* The question, between the clock and the buttons. It changes with the
 * level, so it is drawn with the question rather than once with the screen;
 * the strip is filled first, so a shorter question leaves no tail. */
void TimeGame::drawPrompt(Ui::Renderer& tft) const {
    char line[TimeCourse::PROMPT_MAX + 8];
    TimeCourse::prompt(question_, line, sizeof(line));
    tft.fillRect(PROMPT_RECT.x, PROMPT_RECT.y, PROMPT_RECT.w, PROMPT_RECT.h, Ui::bg());
    /* Font 2 where it fits -- "What time will it be 45 minutes later?" is
     * measured, not trusted -- and font 1 where it does not. */
    const uint8_t font = tft.textWidth(line, 2) <= PROMPT_RECT.w ? 2 : 1;
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(line, PROMPT_RECT.x + PROMPT_RECT.w / 2, PROMPT_RECT.y + PROMPT_RECT.h / 2, font);
    tft.setTextDatum(TL_DATUM);
}

void TimeGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    /* The clock face is the question: a circle, twelve ticks and two hands.
     * None of it moves while the player is choosing, so it is gated -- but a
     * new question moves the hands, and that must not cost a full repaint.
     * drawClock() fills the whole face before drawing anything on it, so the
     * previous hands are covered rather than left behind. */
    if (!drawnClock_) {
        drawClock(tft);
        drawnClock_ = true;
    }
    if (!drawnPrompt_) {
        drawPrompt(tft);
        drawnPrompt_ = true;
    }

    /* THIS SCREEN CANNOT REPAINT JUST THE TWO BUTTONS THAT CHANGED.
     *
     * Its prompt is drawn at y=226 and answerRect() puts the lower row at
     * 192..226, so the two overlap by eight rows. Clearing the prompt strip
     * therefore takes the bottom off the lower buttons -- including the two
     * whose colour did not change and which nothing would put back.
     *
     * So when the prompt changes, every button is invalidated and the order is
     * fixed: clear the strip, repaint all four buttons over it, then write the
     * prompt on top, which is the layering the screen already had. It still
     * leaves the clock face alone, which is the win worth having here. */
    /* The strip also names a new level over its first question, until that
     * question is answered. */
    const bool answerChanged = (answered_ != drawnAnswered_);
    if (answerChanged) {
        tft.fillRect(20, 216, GAME_CANVAS_WIDTH - 40, 20, Ui::bg());
        for (uint8_t i = 0; i < 4; ++i) {
            drawnButton_[i] = 0xFF;
        }
    }

    if (!drawnHeader_ || score_ != drawnScore_ || streak_ != drawnStreak_ ||
        questionLevel_ != drawnLevel_) {
        tft.fillRect(HEADER_MARGIN, HEADER_Y, HEADER_W, HEADER_H, Ui::bg());
        tft.fillRect(static_cast<int16_t>(GAME_CANVAS_WIDTH - HEADER_MARGIN - HEADER_W),
                     HEADER_Y, HEADER_W, HEADER_H, Ui::bg());
        char buf[20];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(questionLevel_));
        tft.drawString(buf, 10, 35, 2);
        snprintf(buf, sizeof(buf), "Score %u", score_);
        tft.drawString(buf, 10, 52, 2);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Streak %u", streak_);
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 35, 2);
        snprintf(buf, sizeof(buf), "Best %u", bestStreak_);
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 52, 2);
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
        char label[TimeCourse::LABEL_MAX];
        TimeCourse::format(question_.label, question_.options[i], label, sizeof(label));
        Ui::drawButton(tft, answerRect(i), label, fill, Ui::outline(), text, false, 2);
        drawnButton_[i] = state;
    }

    if (answerChanged) {
        tft.setTextDatum(MC_DATUM);
        if (answered_) {
            tft.setTextColor(selected_ == question_.correct ? GREEN : RED, Ui::bg());
            tft.drawString(selected_ == question_.correct ? "Correct - tap for next"
                                                          : "Green is correct - tap next",
                           GAME_CANVAS_WIDTH / 2, 226, 2);
        } else if (banner_) {
            char banner[TimeCourse::LEVEL_LINE_MAX + 1];
            TimeCourse::levelLine(questionLevel_, banner, sizeof(banner));
            tft.setTextColor(YELLOW, Ui::bg());
            tft.drawString(banner, GAME_CANVAS_WIDTH / 2, 226, 2);
        }
        tft.setTextDatum(TL_DATUM);
        drawnAnswered_ = answered_;
    }
}
