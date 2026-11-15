// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "FingerCountGame.h"
#include <esp_system.h>   // esp_random(), for the deck and the options
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t SKIN_UP   = 0xFDB6;   // warm skin tone, raised
constexpr uint16_t SKIN_DOWN = 0x9CD3;   // dimmed, folded down
constexpr uint16_t NAIL      = 0xFEDA;

/* Finger lengths, thumb -> little. The middle finger is tallest, which makes
 * the hand read as a hand and separates the tips much further apart than the
 * old flat row of circles ever did. */
constexpr int16_t FINGER_LEN[5] = {34, 46, 52, 46, 36};
constexpr int16_t STUB_LEN = 12;         // folded-down finger

constexpr AppMetadata FINGER_COUNT_METADATA = {
    "fingers",
    "Fingers",
    "Finger Counting",
    "count on hands",
    "Fingers",
    "Count on two hands.",
    nullptr,
    LauncherIcon::FingerCount,
    22,
    true,
};
}

const AppMetadata& fingerCountAppMetadata() {
    return FINGER_COUNT_METADATA;
}

const char* FingerCountGame::title() const {
    return fingerCountAppMetadata().screenTitle != nullptr
        ? fingerCountAppMetadata().screenTitle
        : fingerCountAppMetadata().title;
}

namespace {
using FingersCourse::Ask;

/* Rows of the screen, top to bottom, each cleared on its own. The hint ends
 * above the tallest fingertip's erase box, which starts at FINGER_CLEAR_TOP. */
constexpr int16_t HEADER_Y = TOP_BAR_HEIGHT;
constexpr int16_t HEADER_H = 18;
constexpr int16_t PROMPT_Y = HEADER_Y + HEADER_H;
constexpr int16_t PROMPT_H = 28;
constexpr int16_t HINT_Y = PROMPT_Y + PROMPT_H + 1;
constexpr int16_t HINT_H = 20;
constexpr int16_t TALLEST_FINGER = 52;
constexpr int16_t BUTTON_Y = 196;
constexpr int16_t BUTTON_H = 34;
}   // namespace

static_assert(HINT_Y + HINT_H < 152 - TALLEST_FINGER - 1, "the hint clears the fingertips");

/* Course finger i: 0..4 the left hand from the thumb, 5..9 the right hand
 * from the thumb. The left thumb is drawn at the left end of its hand and
 * the right thumb at the right end of its, so the two hands mirror. */
int16_t FingerCountGame::fingerX(uint8_t idx) const {
    if (idx < 5) return static_cast<int16_t>(LEFT_X0 + idx * FINGER_PITCH);
    return static_cast<int16_t>(RIGHT_X0 + (4 - (idx - 5)) * FINGER_PITCH);
}

int16_t FingerCountGame::fingerTop(uint8_t idx) const {
    const uint8_t shape = static_cast<uint8_t>(idx % 5);
    const int16_t len = isUp(idx) ? FINGER_LEN[shape] : STUB_LEN;
    return static_cast<int16_t>(PALM_TOP - len);
}

Rect FingerCountGame::fingerRect(uint8_t idx) const {
    // The touch target always spans the full possible finger height, so a
    // folded-down finger is just as easy to tap back up as a raised one.
    const int16_t top = static_cast<int16_t>(PALM_TOP - TALLEST_FINGER);
    return Rect{fingerX(idx), top, FINGER_W, static_cast<int16_t>(PALM_BOTTOM - top)};
}

Rect FingerCountGame::answerRect(uint8_t i) const {
    return Rect{static_cast<int16_t>(12 + i * 76), BUTTON_Y, 68, BUTTON_H};
}

Rect FingerCountGame::doneRect() const {
    return Rect{88, BUTTON_Y, 144, BUTTON_H};
}

/* A new game is a new score, not a new start: the level is the player's. */
void FingerCountGame::begin(AppContext& host) {
    score_ = 0;
    rounds_ = 0;
    ladder_.start(FingersCourse::LEVEL_COUNT, host.courseLevel());
    deck_.reset(FingersCourse::poolSize(ladder_.level()));
    announce_ = false;
    newQuestion();
    markDirty();
}

Game::Keep FingerCountGame::keepsGame() const {
    return score_ > 0 ? Keep::Ask : Keep::No;
}

void FingerCountGame::newQuestion() {
    item_ = deck_.next(esp_random());
    FingersCourse::make(ladder_.level(), item_, esp_random(), q_);
    raised_ = q_.raised;
    showLevel_ = announce_;
    announce_ = false;
    selected_ = -1;
    lastCorrect_ = false;
    drawnQuestion_ = false;
}

void FingerCountGame::judge(AppContext& host, bool right) {
    ++rounds_;
    lastCorrect_ = right;
    if (right) {
        ++score_;
    } else {
        deck_.missed(item_);            // before the ladder can reset the deck
    }
    const Course::Ladder::Step step = ladder_.answer(right);
    if (step != Course::Ladder::Step::Stay) {
        host.setCourseLevel(ladder_.level());
        deck_.reset(FingersCourse::poolSize(ladder_.level()));
    }
    if (step == Course::Ladder::Step::Up) {
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::LevelUp);
        announce_ = true;
    } else if (right) {
        host.beepOk();
    } else {
        host.beepError();
    }
    markDirty();
}

void FingerCountGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;
    if (answered()) {
        newQuestion();
        markDirty();
        return;
    }
    if (!FingersCourse::showsWithHands(q_.ask)) {
        for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
            if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
            selected_ = static_cast<int8_t>(i);
            judge(host, i == q_.correct);
            return;
        }
        return;
    }
    /* Showing with the hands: fingers go up and down until Done says this
     * is the answer. Only Done is judged, so a finger tapped past the number
     * and back costs nothing. */
    if (doneRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        selected_ = OPTION_COUNT;
        judge(host, FingersCourse::countUp(raised_) == q_.answer);
        return;
    }
    for (uint8_t i = 0; i < FINGER_COUNT; ++i) {
        if (!fingerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        raised_ = static_cast<uint16_t>(raised_ ^ (1U << i));
        markDirty();
        return;
    }
}

/* One hand whole: what was above its palm cleared, then the palm, the five
 * fingers and its label. Idempotent, so a finger tapped repaints its hand
 * and nothing else. */
void FingerCountGame::drawHand(Ui::Renderer& tft, uint8_t hand) const {
    const int16_t x0 = (hand == 0) ? LEFT_X0 : RIGHT_X0;
    const int16_t palmX = static_cast<int16_t>(x0 - 4);
    const int16_t palmW = static_cast<int16_t>(4 * FINGER_PITCH + FINGER_W + 8);
    const int16_t palmH = static_cast<int16_t>(PALM_BOTTOM - PALM_TOP + 6);
    tft.fillRect(palmX, PALM_TOP - TALLEST_FINGER - 1, palmW, TALLEST_FINGER - 5, Ui::bg());
    tft.fillRoundRect(palmX, static_cast<int16_t>(PALM_TOP - 6), palmW, palmH, 8, SKIN_UP);
    tft.drawRoundRect(palmX, static_cast<int16_t>(PALM_TOP - 6), palmW, palmH, 8, Ui::outline());

    for (uint8_t d = 0; d < 5; ++d) {
        const uint8_t idx = static_cast<uint8_t>(hand * 5 + d);
        const int16_t x = fingerX(idx);
        const int16_t top = fingerTop(idx);
        const int16_t h = static_cast<int16_t>(PALM_TOP + 4 - top);
        const uint16_t col = isUp(idx) ? SKIN_UP : SKIN_DOWN;
        tft.fillRoundRect(x, top, FINGER_W, h, 9, col);
        tft.drawRoundRect(x, top, FINGER_W, h, 9, Ui::outline());
        if (isUp(idx)) {
            // A nail on the tip reads as "up" at a glance.
            tft.fillRoundRect(static_cast<int16_t>(x + 5), static_cast<int16_t>(top + 5),
                              static_cast<int16_t>(FINGER_W - 10), 8, 3, NAIL);
        }
    }
    /* Labels sit ON the palms. Above the hands they collided with the middle
     * fingertip. */
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::rgb(150, 90, 88), SKIN_UP);
    tft.drawString(hand == 0 ? "Left" : "Right",
                   static_cast<int16_t>(x0 + 2 * FINGER_PITCH + FINGER_W / 2), 169, 2);
    tft.setTextDatum(TL_DATUM);
}

void FingerCountGame::drawPrompt(Ui::Renderer& tft) const {
    tft.fillRect(0, PROMPT_Y, GAME_CANVAS_WIDTH, PROMPT_H, Ui::bg());
    char text[32];
    switch (q_.ask) {
        case Ask::ShowMe:
            snprintf(text, sizeof(text), "Show me %u %s", static_cast<unsigned>(q_.answer),
                     q_.answer == 1 ? "finger" : "fingers");
            break;
        case Ask::HowManyDown: snprintf(text, sizeof(text), "How many are down?"); break;
        case Ask::InAll: snprintf(text, sizeof(text), "How many in all?"); break;
        case Ask::MakeIt:
            snprintf(text, sizeof(text), "Make it %u", static_cast<unsigned>(q_.answer));
            break;
        case Ask::HowMany:
        default: snprintf(text, sizeof(text), "How many fingers?"); break;
    }
    const uint8_t font = tft.textWidth(text, 4) <= GAME_CANVAS_WIDTH - 16 ? 4 : 2;
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, GAME_CANVAS_WIDTH / 2, PROMPT_Y + PROMPT_H / 2, font);
    tft.setTextDatum(TL_DATUM);
}

/* The line under the prompt: how to answer, the level just reached, the
 * running count on the first two levels, or whether the answer was right. */
void FingerCountGame::drawHint(Ui::Renderer& tft) const {
    tft.fillRect(0, HINT_Y, GAME_CANVAS_WIDTH, HINT_H, Ui::bg());
    char text[48];
    uint16_t colour = Ui::muted();
    const uint8_t up = FingersCourse::countUp(raised_);
    if (answered()) {
        colour = lastCorrect_ ? Ui::success() : Ui::error();
        if (lastCorrect_) {
            snprintf(text, sizeof(text), "Yes! %u - tap for next", static_cast<unsigned>(q_.answer));
        } else if (FingersCourse::showsWithHands(q_.ask)) {
            snprintf(text, sizeof(text), "That is %u. It was %u - tap for next",
                     static_cast<unsigned>(up), static_cast<unsigned>(q_.answer));
        } else {
            snprintf(text, sizeof(text), "It was %u - tap for next", static_cast<unsigned>(q_.answer));
        }
    } else if (showLevel_) {
        FingersCourse::levelLine(ladder_.level(), text, sizeof(text));
        colour = Ui::accent();
    } else {
        switch (q_.ask) {
            case Ask::ShowMe:
                if (q_.countsAloud) {
                    snprintf(text, sizeof(text), "Up: %u, then tap Done", static_cast<unsigned>(up));
                    if (up == q_.answer) colour = Ui::success();
                } else {
                    snprintf(text, sizeof(text), "Raise the fingers, then tap Done");
                }
                break;
            case Ask::HowManyDown: snprintf(text, sizeof(text), "Count the fingers folded down"); break;
            case Ask::InAll:
                snprintf(text, sizeof(text), "%u on the left, %u on the right",
                         static_cast<unsigned>(q_.left), static_cast<unsigned>(q_.right));
                break;
            case Ask::MakeIt: snprintf(text, sizeof(text), "Change the fingers, then tap Done"); break;
            case Ask::HowMany:
            default: snprintf(text, sizeof(text), "Count them, then tap the number"); break;
        }
    }
    tft.setTextColor(colour, Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, GAME_CANVAS_WIDTH / 2, HINT_Y + HINT_H / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

void FingerCountGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    drawnQuestion_ = false;
    drawnScore_ = 0xFFFF;
    drawnRounds_ = 0xFFFF;
    drawnLevel_ = 0xFF;
}

void FingerCountGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const bool hands = FingersCourse::showsWithHands(q_.ask);

    if (!drawnQuestion_) {
        drawPrompt(tft);
        /* Both hands, whatever they show: comparing against a "nothing
         * drawn" value would miss a hand that starts with all five up. */
        drawHand(tft, 0);
        drawHand(tft, 1);
        drawnRaised_ = raised_;
        drawnHint_ = 0xFF;
        drawnUp_ = 0xFF;
        /* The button row: four numbers, or one Done. Cleared whole, since a
         * question of the other kind leaves the other row behind. */
        tft.fillRect(0, BUTTON_Y, GAME_CANVAS_WIDTH, BUTTON_H + 2, Ui::bg());
        for (uint8_t i = 0; i <= OPTION_COUNT; ++i) drawnButton_[i] = 0xFF;
        drawnQuestion_ = true;
    }

    if (score_ != drawnScore_ || rounds_ != drawnRounds_ || ladder_.level() != drawnLevel_) {
        tft.fillRect(0, HEADER_Y, GAME_CANVAS_WIDTH, HEADER_H, Ui::bg());
        char text[16];
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(text, sizeof(text), "Level %u", static_cast<unsigned>(ladder_.level()));
        tft.drawString(text, 8, HEADER_Y + 2, 2);
        snprintf(text, sizeof(text), "%u/%u", static_cast<unsigned>(score_),
                 static_cast<unsigned>(rounds_));
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TR_DATUM);
        tft.drawString(text, GAME_CANVAS_WIDTH - 8, HEADER_Y + 2, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnRounds_ = rounds_;
        drawnLevel_ = ladder_.level();
    }

    /* A hand is repainted when any of its five fingers changed. */
    if (raised_ != drawnRaised_) {
        const uint16_t changed = static_cast<uint16_t>(raised_ ^ drawnRaised_);
        if ((changed & 0x1F) != 0) drawHand(tft, 0);
        if ((changed & 0x3E0) != 0) drawHand(tft, 1);
        drawnRaised_ = raised_;
    }

    /* The hint changes with the answer, and on the first two levels with
     * every finger. */
    const uint8_t up = FingersCourse::countUp(raised_);
    const uint8_t hint = answered() ? (lastCorrect_ ? 1 : 2) : (showLevel_ ? 3 : 0);
    if (hint != drawnHint_ || (q_.countsAloud && up != drawnUp_)) {
        drawHint(tft);
        drawnHint_ = hint;
        drawnUp_ = up;
    }

    if (hands) {
        uint8_t state = 0;
        if (answered()) state = lastCorrect_ ? 1 : 2;
        if (state != drawnButton_[OPTION_COUNT]) {
            const uint16_t fill = state == 1 ? Ui::success() : (state == 2 ? Ui::error() : Ui::panel());
            const uint16_t tc = state == 0 ? Ui::text() : TFT_BLACK;
            Ui::drawButton(tft, doneRect(), "Done", fill, Ui::outline(), tc, false, 4);
            drawnButton_[OPTION_COUNT] = state;
        }
        return;
    }
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        uint8_t state = 0;
        if (answered()) {
            if (i == q_.correct) state = 1;
            else if (i == static_cast<uint8_t>(selected_)) state = 2;
        }
        if (state == drawnButton_[i]) continue;
        const uint16_t fill = state == 1 ? Ui::success() : (state == 2 ? Ui::error() : Ui::panel());
        const uint16_t tc = state == 0 ? Ui::text() : TFT_BLACK;
        char label[4];
        snprintf(label, sizeof(label), "%u", static_cast<unsigned>(q_.options[i]));
        Ui::drawButton(tft, answerRect(i), label, fill, Ui::outline(), tc, false, 4);
        drawnButton_[i] = state;
    }
}
