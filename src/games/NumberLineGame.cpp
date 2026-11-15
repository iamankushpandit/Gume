// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "NumberLineGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr AppMetadata NUMBER_LINE_METADATA = {
    "numberline",
    "Number Line",
    nullptr,
    "jump to number",
    "Number Line",
    "Hop along a number line.",
    nullptr,
    LauncherIcon::NumberLine,
    4,
    true,
};
}

const AppMetadata& numberLineAppMetadata() {
    return NUMBER_LINE_METADATA;
}

const char* NumberLineGame::title() const {
    return numberLineAppMetadata().screenTitle != nullptr
        ? numberLineAppMetadata().screenTitle
        : numberLineAppMetadata().title;
}

namespace {
namespace NLC = NumberLineCourse;

uint16_t blueFill() { return Ui::rgb(36, 132, 204); }
uint16_t frogFill() { return Ui::rgb(80, 230, 80); }
uint16_t hopColour() { return Ui::rgb(255, 200, 0); }
constexpr uint16_t YELLOW = 0xFEC0;

/* The screen, top to bottom. Each part is cleared only inside its own strip,
 * and the strips are stated once here so they cannot overlap: the header
 * under the top bar, the question, a line of instruction, the number line
 * with the frog and its hops, and the buttons. */
constexpr int16_t HEADER_Y = 34;
constexpr int16_t HEADER_H = 16;
constexpr int16_t QUESTION_CY = 62;
constexpr int16_t QUESTION_TOP = 50;
constexpr int16_t QUESTION_H = 26;
constexpr int16_t NOTE_CY = 90;
constexpr int16_t NOTE_TOP = 81;
constexpr int16_t NOTE_H = 18;
constexpr int16_t LINE_Y = 162;
/* The hop arcs rise 20px and the frog sits 14px up with a radius of 9; the
 * tick labels hang 9px below in font 1. */
constexpr int16_t LINE_TOP = LINE_Y - 34;
constexpr int16_t LINE_H = 54;
constexpr int16_t BUTTON_Y = 194;
static_assert(HEADER_Y + HEADER_H <= QUESTION_TOP, "header and question overlap");
static_assert(QUESTION_TOP + QUESTION_H <= NOTE_TOP, "question and note overlap");
static_assert(NOTE_TOP + NOTE_H <= LINE_TOP, "note and line overlap");
static_assert(LINE_TOP + LINE_H <= BUTTON_Y, "line and buttons overlap");

constexpr uint32_t WATCH_PAUSE_MS = 1100;
constexpr uint32_t HOP_MS = 650;
constexpr uint32_t RIGHT_HOLD_MS = 1300;
constexpr uint32_t WRONG_HOLD_MS = 2400;
}

int16_t NumberLineGame::numToX(int16_t n) const {
    const int16_t span = static_cast<int16_t>(q_.hi - q_.lo);
    return static_cast<int16_t>(22 + (static_cast<int32_t>(n - q_.lo) * 276) / span);
}

Rect NumberLineGame::answerRect(uint8_t i) const {
    return Rect{static_cast<int16_t>(14 + i * 74), BUTTON_Y, 64, 38};
}

int16_t NumberLineGame::positionAfter(uint8_t step) const {
    if (q_.viaTen) {
        if (step == 0) return q_.start;
        if (step == 1) return 10;
        return q_.landing();
    }
    const int16_t d = static_cast<int16_t>(q_.hop * step);
    return static_cast<int16_t>(q_.add ? q_.start + d : q_.start - d);
}

uint8_t NumberLineGame::animSteps() const {
    if (q_.ask == NLC::Ask::Nearest) return 0;
    return q_.viaTen ? 2 : q_.hops;
}

void NumberLineGame::startLevel() {
    deck_.reset(NLC::poolSize(ladder_.level()));
}

void NumberLineGame::newQuestion() {
    questionLevel_ = ladder_.level();
    item_ = deck_.next(esp_random());
    NLC::make(questionLevel_, item_, esp_random(), q_);
    banner_ = announce_;
    announce_ = false;
    animStep_ = 0;
    selected_ = -1;
    feedbackUntil_ = 0;
    if (q_.watchFirst) {
        phase_ = Phase::Watch;
        nextAt_ = millis() + WATCH_PAUSE_MS;
    } else {
        phase_ = Phase::Ask;
        nextAt_ = 0;
    }
    drawnQuestion_ = false;
    drawnLine_ = false;
    drawnPhase_ = Phase::Feedback;
    drawnButtons_ = 0xFE;
    if (phase_ == Phase::Ask) drawnPhase_ = Phase::Watch;
}

/* A new visit, or New game from the Carry on card: the tally starts over,
 * the level does not. It used to start at the same thirty sums every time. */
void NumberLineGame::begin(AppContext& host) {
    score_ = 0;
    rounds_ = 0;
    ladder_.start(NLC::LEVEL_COUNT, host.courseLevel());
    startLevel();
    announce_ = false;
    newQuestion();
    markDirty();
}

/* A score is worth coming back to; before the first answer there is only a
 * question, and a new one is as good. */
Game::Keep NumberLineGame::keepsGame() const {
    return rounds_ > 0 ? Keep::Ask : Keep::No;
}

/* Both deadlines carry on from where they stopped, so the frog does not leap
 * the moment the game is back and the last answer's colours are still shown
 * for as long as they would have been. */
void NumberLineGame::resume(AppContext&, uint32_t awayMs) {
    movedOn(nextAt_, awayMs);
    movedOn(feedbackUntil_, awayMs);
}

void NumberLineGame::startHops(uint32_t now) {
    animStep_ = 0;
    if (animSteps() == 0) {
        phase_ = Phase::Feedback;
        feedbackUntil_ = now + (lastCorrect_ ? RIGHT_HOLD_MS : WRONG_HOLD_MS);
        return;
    }
    phase_ = Phase::Show;
    nextAt_ = now + HOP_MS / 2;
}

void NumberLineGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if ((phase_ == Phase::Watch || phase_ == Phase::Show) && now >= nextAt_) {
        if (animStep_ < animSteps()) {
            ++animStep_;
            nextAt_ = now + HOP_MS;
        } else if (phase_ == Phase::Watch) {
            phase_ = Phase::Ask;
        } else {
            phase_ = Phase::Feedback;
            feedbackUntil_ = now + (lastCorrect_ ? RIGHT_HOLD_MS : WRONG_HOLD_MS);
        }
        markDirty();
        return;
    }
    if (phase_ == Phase::Feedback && now >= feedbackUntil_) {
        newQuestion();
        markDirty();
        return;
    }
    if (!touch.justPressed || phase_ != Phase::Ask) return;
    for (uint8_t i = 0; i < 4; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        selected_ = static_cast<int8_t>(i);
        ++rounds_;
        lastCorrect_ = (i == q_.correct);
        if (lastCorrect_) {
            ++score_;
        } else {
            /* Before the ladder moves: a step resets the deck. */
            deck_.missed(item_);
        }
        /* One try, so the first press is the only one the ladder hears. */
        const Course::Ladder::Step step = ladder_.answer(lastCorrect_);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
            startLevel();
        }
        if (step == Course::Ladder::Step::Up) {
            announce_ = true;
            host.pulseRgb(0, 255, 40, 450);
            host.playSound(Sound::LevelUp);
        } else if (lastCorrect_) {
            host.beepOk();
        } else {
            host.beepError();
        }
        /* From level 2 the frog hops now, to check the answer it was given.
         * On level 1 it already has. */
        if (q_.watchFirst) {
            phase_ = Phase::Feedback;
            feedbackUntil_ = now + (lastCorrect_ ? RIGHT_HOLD_MS : WRONG_HOLD_MS);
        } else {
            startHops(now);
        }
        markDirty();
        return;
    }
}

void NumberLineGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    drawnHeader_ = false;
    drawnQuestion_ = false;
    drawnLine_ = false;
    drawnPhase_ = phase_ == Phase::Watch ? Phase::Ask : Phase::Watch;
    drawnButtons_ = 0xFE;
}

/* The line, its ticks and labels, the hops so far and the frog, inside the
 * line's own strip. Every part is drawn opaquely over a filled strip, so it
 * can be called whole for any change in it. */
void NumberLineGame::drawLine(Ui::Renderer& tft) const {
    tft.fillRect(0, LINE_TOP, GAME_CANVAS_WIDTH, LINE_H, Ui::bg());

    for (uint8_t step = 0; step < animStep_; ++step) {
        Ui::drawHopArc(tft, numToX(positionAfter(step)), numToX(positionAfter(step + 1)),
                       LINE_Y, 20, hopColour());
    }

    tft.drawLine(18, LINE_Y, 302, LINE_Y, Ui::text());
    tft.drawLine(300, static_cast<int16_t>(LINE_Y - 5), 302, LINE_Y, Ui::text());
    tft.drawLine(300, static_cast<int16_t>(LINE_Y + 5), 302, LINE_Y, Ui::text());
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    uint8_t index = 0;
    for (int16_t v = q_.lo; v <= q_.hi; v = static_cast<int16_t>(v + q_.tick), ++index) {
        const int16_t tx = numToX(v);
        const bool labelled = index % q_.labelEvery == 0;
        tft.drawLine(tx, static_cast<int16_t>(LINE_Y - (labelled ? 6 : 4)), tx,
                     static_cast<int16_t>(LINE_Y + (labelled ? 6 : 4)), Ui::text());
        if (labelled) {
            char label[6];
            snprintf(label, sizeof(label), "%d", v);
            tft.drawString(label, tx, static_cast<int16_t>(LINE_Y + 9), 1);
        }
    }

    /* How many hops: the mark to hop to is ringed. Nearest ten, once
     * answered: the ten it is nearest is ringed. */
    if (q_.ask == NLC::Ask::Hops) {
        const int16_t rx = numToX(q_.landing());
        tft.drawCircle(rx, LINE_Y, 6, YELLOW);
        tft.drawCircle(rx, LINE_Y, 7, YELLOW);
    } else if (q_.ask == NLC::Ask::Nearest && phase_ == Phase::Feedback) {
        const int16_t rx = numToX(q_.answer);
        tft.drawCircle(rx, LINE_Y, 6, Ui::success());
        tft.drawCircle(rx, LINE_Y, 7, Ui::success());
    }

    const int16_t at = positionAfter(animStep_);
    const int16_t dx = numToX(at);
    const int16_t fy = static_cast<int16_t>(LINE_Y - 14);
    tft.fillCircle(dx, fy, 9, frogFill());
    tft.drawCircle(dx, fy, 9, Ui::text());
    tft.setTextColor(TFT_BLACK, frogFill());
    tft.setTextDatum(MC_DATUM);
    char label[6];
    snprintf(label, sizeof(label), "%d", at);
    tft.drawString(label, dx, fy, 1);
    tft.setTextDatum(TL_DATUM);
}

void NumberLineGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    char buf[32];

    if (!drawnHeader_ || score_ != drawnScore_ || rounds_ != drawnRounds_ ||
        questionLevel_ != drawnLevel_) {
        tft.fillRect(0, HEADER_Y, GAME_CANVAS_WIDTH, HEADER_H, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(questionLevel_));
        tft.drawString(buf, 8, HEADER_Y, 2);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(score_),
                 static_cast<unsigned>(rounds_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, HEADER_Y, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnRounds_ = rounds_;
        drawnLevel_ = questionLevel_;
        drawnHeader_ = true;
    }

    /* The question, and once answered the same line solved. */
    const bool solved = phase_ == Phase::Feedback;
    if (!drawnQuestion_ || solved != drawnSolved_) {
        tft.fillRect(0, QUESTION_TOP, GAME_CANVAS_WIDTH, QUESTION_H, Ui::bg());
        if (solved) {
            q_.solved(buf, sizeof(buf));
        } else {
            q_.text(buf, sizeof(buf));
        }
        tft.setTextColor(solved && lastCorrect_ ? Ui::success() : Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(buf, GAME_CANVAS_WIDTH / 2, QUESTION_CY, 4);
        tft.setTextDatum(TL_DATUM);
        drawnQuestion_ = true;
        drawnSolved_ = solved;
    }

    if (!drawnLine_ || animStep_ != drawnStep_ || phase_ != drawnPhase_) {
        drawLine(tft);
        drawnLine_ = true;
        drawnStep_ = animStep_;
    }

    /* One line of instruction, or the new level's name over its first
     * question, or what the answer was. */
    if (phase_ != drawnPhase_) {
        tft.fillRect(0, NOTE_TOP, GAME_CANVAS_WIDTH, NOTE_H, Ui::bg());
        const char* note = nullptr;
        uint16_t colour = Ui::muted();
        switch (phase_) {
            case Phase::Watch:
                note = q_.add ? "Watch the frog hop right!" : "Watch the frog hop left!";
                break;
            case Phase::Ask:
                if (banner_) {
                    NLC::levelLine(questionLevel_, buf, sizeof(buf));
                    note = buf;
                    colour = YELLOW;
                } else if (q_.watchFirst) {
                    note = "Where did the frog land?";
                } else if (q_.ask == NLC::Ask::Hops) {
                    note = "How many hops to the ring?";
                } else if (q_.ask == NLC::Ask::Nearest) {
                    note = "Which ten is it nearest?";
                } else if (q_.viaTen) {
                    note = "Hop to 10 first, then the rest";
                } else {
                    note = "Where will the frog land?";
                }
                break;
            case Phase::Show:
                note = "Watch the frog check it";
                break;
            case Phase::Feedback:
                if (lastCorrect_) {
                    note = "Correct!";
                    colour = Ui::success();
                } else {
                    snprintf(buf, sizeof(buf), "Not quite - it is %d", q_.answer);
                    note = buf;
                    colour = Ui::error();
                }
                break;
        }
        tft.setTextColor(colour, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(note, GAME_CANVAS_WIDTH / 2, NOTE_CY, 2);
        tft.setTextDatum(TL_DATUM);
        drawnPhase_ = phase_;
    }

    /* The buttons: none while the frog shows the hops first, waiting while
     * the question is asked, marked once it is answered. */
    const uint8_t buttons = phase_ == Phase::Watch ? 0xFF : (phase_ == Phase::Ask ? 0 : 1);
    if (buttons != drawnButtons_) {
        if (buttons == 0xFF) {
            tft.fillRect(0, BUTTON_Y, GAME_CANVAS_WIDTH, 38, Ui::bg());
        } else {
            for (uint8_t i = 0; i < 4; ++i) {
                uint16_t fill = blueFill();
                uint16_t tc = TFT_WHITE;
                if (buttons == 1) {
                    if (i == q_.correct) {
                        fill = Ui::success();
                        tc = TFT_BLACK;
                    } else if (i == static_cast<uint8_t>(selected_)) {
                        fill = Ui::error();
                        tc = TFT_BLACK;
                    }
                }
                snprintf(buf, sizeof(buf), "%d", q_.options[i]);
                Ui::drawButton(tft, answerRect(i), buf, fill, TFT_DARKGREY, tc, false, 4);
            }
        }
        drawnButtons_ = buttons;
    }
}
