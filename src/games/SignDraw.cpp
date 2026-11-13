// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "SignGame.h"
#include "map_n_flag.h"

/* Every pixel of Sign ABC.
 *
 * The hands are line art stored as an alpha ramp, painted in Ui::text() on a
 * Ui::panel() card -- the same pairing as every other card in the firmware, so
 * the contrast check already covers it and all nine themes get a hand in
 * their own ink.
 *
 * A full repaint happens only on a change of stage (SignGame::goTo()), which
 * is a change of screen. Inside one: Next in a lesson repaints the card, the
 * letter and the two buttons; a new question of the same kind repaints the
 * cards and labels; an answer repaints button states and the prompt line; a
 * question of the other kind clears the body below the header, and nothing
 * above it. */

namespace {
constexpr int16_t HEADER_Y = 31;
constexpr int16_t HEADER_H = 18;
constexpr int16_t CHIP_W = 64;
constexpr int16_t CHIP_X = GAME_CANVAS_WIDTH - 8 - CHIP_W;
constexpr int16_t HEADER_TEXT_W = CHIP_X - 8 - 4;

/* Everything below the header row. */
constexpr int16_t BODY_Y = HEADER_Y + HEADER_H + 3;

/* The big card holds a hand at 2x: 128x168 inside a 2px inset. */
constexpr int16_t BIG_X = 8;
constexpr int16_t BIG_Y = 54;
constexpr int16_t BIG_W = 2 * MNF_SIGN_WIDTH + 16;
constexpr int16_t BIG_H = 2 * MNF_SIGN_HEIGHT + 12;
static_assert(BIG_Y >= BODY_Y, "big card overlaps the header row");
static_assert(BIG_Y + BIG_H <= GAME_CANVAS_HEIGHT - 4, "big card runs off the canvas");

/* Everything to the right of the big card. */
constexpr int16_t SIDE_X = BIG_X + BIG_W + 8;
constexpr int16_t SIDE_W = GAME_CANVAS_WIDTH - 8 - SIDE_X;
constexpr int16_t SIDE_GAP = 8;
constexpr int16_t KEY_W = (SIDE_W - SIDE_GAP) / 2;

/* Read: the prompt line, then a 2x2 of letter buttons under it. */
constexpr int16_t PROMPT_H = 24;
constexpr int16_t KEY_Y0 = BIG_Y + PROMPT_H + 6;
constexpr int16_t KEY_H = (BIG_Y + BIG_H - KEY_Y0 - SIDE_GAP) / 2;

/* Find: a prompt line, the letter big under it, four hand cards in a row. */
constexpr int16_t CARD_W = MNF_SIGN_WIDTH + 10;
constexpr int16_t CARD_H = MNF_SIGN_HEIGHT + 14;
constexpr int16_t CARD_GAP = (GAME_CANVAS_WIDTH - 12 - 4 * CARD_W) / 3;
constexpr int16_t CARD_Y = GAME_CANVAS_HEIGHT - 6 - CARD_H;
constexpr int16_t FIND_LETTER_Y = BIG_Y + PROMPT_H;
constexpr int16_t FIND_LETTER_H = CARD_Y - 4 - FIND_LETTER_Y;
static_assert(CARD_GAP >= 2, "four hand cards do not fit across the canvas");
static_assert(FIND_LETTER_H >= 48, "no room for the letter above the hand cards");

/* Lessons: two rows of four tiles, a one-line status under each. */
constexpr int16_t TILE_COLS = 4;
constexpr int16_t TILE_GAP = 8;
constexpr int16_t TILE_W = (GAME_CANVAS_WIDTH - 16 - (TILE_COLS - 1) * TILE_GAP) / TILE_COLS;
constexpr int16_t TILE_H = 58;
constexpr int16_t TILE_STATUS_H = 14;
constexpr int16_t TILE_Y0 = BODY_Y + 8;
constexpr int16_t TILE_PITCH = TILE_H + TILE_STATUS_H + 10;
static_assert(TILE_Y0 + TILE_PITCH + TILE_H + TILE_STATUS_H <= GAME_CANVAS_HEIGHT,
              "the second row of lesson tiles runs off the canvas");

/* The frame round a hand card: one outline pixel normally, three pixels of
 * success or error once answered. Repainting it never touches the hand. */
constexpr int16_t FRAME = 3;

/* Font 4 doubled: 52px, the size of a letter worth asking about. */
constexpr uint8_t BIG_FONT = 4;
constexpr uint8_t BIG_SIZE = 2;

Rect inset(const Rect& r, int16_t by) {
    return Rect{static_cast<int16_t>(r.x + by), static_cast<int16_t>(r.y + by),
                static_cast<int16_t>(r.w - 2 * by), static_cast<int16_t>(r.h - 2 * by)};
}

void drawHandCard(Ui::Renderer& tft, const Rect& card, uint8_t letter, uint8_t scale) {
    tft.fillRect(card.x, card.y, card.w, card.h, Ui::panel());
    tft.drawRect(card.x, card.y, card.w, card.h, Ui::outline());
    Ui::drawCountryImageTintedScaled(tft, mnf_sign(static_cast<char>('A' + letter)),
                                     inset(card, FRAME), Ui::panel(), Ui::text(), scale);
}

void drawBigLetter(Ui::Renderer& tft, uint8_t letter, int16_t cx, int16_t cy) {
    const char buf[2] = { static_cast<char>('A' + letter), '\0' };
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextSize(BIG_SIZE);
    tft.drawString(buf, cx, cy, BIG_FONT);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
}

void centredLine(Ui::Renderer& tft, const char* text, int16_t cx, int16_t cy,
                 uint16_t color, uint8_t font) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(color, Ui::bg());
    tft.drawString(text, cx, cy, font);
    tft.setTextDatum(TL_DATUM);
}
}  // namespace

// ---- geometry ---------------------------------------------------------------

Rect SignGame::lessonRect(uint8_t i) {
    return Rect{static_cast<int16_t>(8 + (i % TILE_COLS) * (TILE_W + TILE_GAP)),
                static_cast<int16_t>(TILE_Y0 + (i / TILE_COLS) * TILE_PITCH),
                TILE_W, TILE_H};
}

Rect SignGame::lessonsChipRect() { return Rect{CHIP_X, HEADER_Y, CHIP_W, HEADER_H}; }

Rect SignGame::bigCardRect() { return Rect{BIG_X, BIG_Y, BIG_W, BIG_H}; }

Rect SignGame::letterRect(uint8_t i) {
    return Rect{static_cast<int16_t>(SIDE_X + (i % 2) * (KEY_W + SIDE_GAP)),
                static_cast<int16_t>(KEY_Y0 + (i / 2) * (KEY_H + SIDE_GAP)),
                KEY_W, KEY_H};
}

Rect SignGame::handCardRect(uint8_t i) {
    return Rect{static_cast<int16_t>(6 + i * (CARD_W + CARD_GAP)), CARD_Y, CARD_W, CARD_H};
}

/* A lesson's Back and Next take the bottom row of the letter grid, so they sit
 * where the quiz's buttons will be. */
Rect SignGame::backRect() { return letterRect(2); }
Rect SignGame::nextRect() { return letterRect(3); }

Rect SignGame::resultRect(uint8_t i) {
    constexpr int16_t W = 132;
    constexpr int16_t GAP = 16;
    constexpr int16_t X0 = (GAME_CANVAS_WIDTH - 2 * W - GAP) / 2;
    return Rect{static_cast<int16_t>(X0 + i * (W + GAP)), 166, W, 54};
}

uint8_t SignGame::optionState(uint8_t i) const {
    if (phase_ != Phase::Feedback) return 0;
    if (i == correctSlot_) return 1;
    if (static_cast<int8_t>(i) == chosen_) return 2;
    return 0;
}

/* 0 asking, 1 answered right, 2 answered wrong. */
uint8_t SignGame::promptState() const {
    if (phase_ != Phase::Feedback) return 0;
    return static_cast<uint8_t>(chosen_) == correctSlot_ ? 1 : 2;
}

// ---- rendering --------------------------------------------------------------

void SignGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    if (stage_ != Stage::Lessons) {
        Ui::drawButton(tft, lessonsChipRect(), "Lessons", Ui::panel(), Ui::outline(),
                       Ui::text(), false, 2);
    }
    /* The lesson tiles and the result card change only by leaving and
     * re-entering their stage, so they are drawn once, here. */
    if (stage_ == Stage::Lessons) drawLessons(tft);
    if (stage_ == Stage::Result) drawResult(tft);

    drawnQuestion_ = 0xFFFF;
    /* The screen was just cleared, so the body is already empty for whichever
     * kind of question comes first. */
    drawnKind_ = static_cast<uint8_t>(kind_);
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) drawnOption_[i] = NOT_DRAWN;
    drawnPrompt_ = NOT_DRAWN;
    drawnHeader_[0] = '\0';
    drawnTeach_ = NOT_DRAWN;
}

void SignGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    drawHeader(tft);
    switch (stage_) {
        case Stage::Teach:    drawTeach(tft);    break;
        case Stage::Quiz:
        case Stage::Practice: drawQuestion(tft); break;
        case Stage::Lessons:
        case Stage::Result:   break;
    }
}

void SignGame::drawHeader(Ui::Renderer& tft) {
    char buf[sizeof(drawnHeader_)];
    const char first = static_cast<char>('A' + lessonStart(lesson_));
    const char last = static_cast<char>('A' + lessonEnd(lesson_) - 1);
    switch (stage_) {
        case Stage::Lessons:
            snprintf(buf, sizeof(buf), "%u of %u lessons done", passed_, LESSONS);
            break;
        case Stage::Teach:
            snprintf(buf, sizeof(buf), "Lesson %u: %c to %c", lesson_ + 1, first, last);
            break;
        case Stage::Quiz:
            snprintf(buf, sizeof(buf), "Lesson %u quiz: %u of %u", lesson_ + 1,
                     quizNum_ + 1, QUIZ_LEN);
            break;
        case Stage::Result:
            snprintf(buf, sizeof(buf), "Lesson %u", lesson_ + 1);
            break;
        case Stage::Practice:
            snprintf(buf, sizeof(buf), "Practice  %u/%u", score_, rounds_);
            break;
    }
    if (strcmp(buf, drawnHeader_) == 0) return;
    tft.fillRect(8, HEADER_Y, HEADER_TEXT_W, HEADER_H, Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(buf, 8, HEADER_Y + 2, 2);
    strncpy(drawnHeader_, buf, sizeof(drawnHeader_) - 1);
}

void SignGame::drawLessons(Ui::Renderer& tft) {
    for (uint8_t i = 0; i <= LESSONS; ++i) {
        const Rect r = lessonRect(i);
        const bool practice = i == LESSONS;
        const bool open = practice ? passed_ > 0 : i <= passed_;
        const bool done = !practice && i < passed_;
        const bool next = !practice && i == passed_;

        char label[12];
        const char* status = nullptr;
        if (practice) {
            snprintf(label, sizeof(label), "Practice");
            status = open ? "all you know" : "after lesson 1";
        } else {
            /* "A B C D": the letters themselves, spaced so each reads. */
            uint8_t n = 0;
            for (uint8_t c = lessonStart(i); c < lessonEnd(i) && n + 2 < sizeof(label); ++c) {
                if (n > 0) label[n++] = ' ';
                label[n++] = static_cast<char>('A' + c);
            }
            label[n] = '\0';
            status = done ? "done" : (next ? "next" : "locked");
        }

        const uint16_t fill = done ? Ui::success() : (next ? Ui::accent() : Ui::panel());
        const uint16_t ink = (done || next) ? Ui::onFill(fill) : (open ? Ui::text() : Ui::muted());
        Ui::drawButton(tft, r, label, fill, Ui::outline(), ink, false, 2);
        centredLine(tft, status, r.x + r.w / 2, r.y + r.h + TILE_STATUS_H / 2 + 2,
                    open ? Ui::text() : Ui::muted(), 1);
    }
}

void SignGame::drawTeach(Ui::Renderer& tft) {
    if (drawnTeach_ == teachIndex_) return;
    const uint8_t start = lessonStart(lesson_);
    const uint8_t count = static_cast<uint8_t>(lessonEnd(lesson_) - start);
    const uint8_t letter = static_cast<uint8_t>(start + teachIndex_);
    drawHandCard(tft, bigCardRect(), letter, 2);

    /* Everything above Back and Next, down to one row short of them. */
    const int16_t textH = static_cast<int16_t>(backRect().y - 1 - BIG_Y);
    static_assert(BIG_Y + 98 + 8 < KEY_Y0 + KEY_H + SIDE_GAP,
                  "the lesson text runs into Back and Next");
    tft.fillRect(SIDE_X, BIG_Y, SIDE_W, textH, Ui::bg());
    const int16_t cx = SIDE_X + SIDE_W / 2;
    drawBigLetter(tft, letter, cx, BIG_Y + 28);

    char line[24];
    snprintf(line, sizeof(line), "New hand %u of %u", teachIndex_ + 1, count);
    centredLine(tft, line, cx, BIG_Y + 64, Ui::muted(), 2);
    const mnf_sign_t* sign = mnf_sign_at(letter);
    if (sign != nullptr && sign->moves) {
        centredLine(tft, "This one moves:", cx, BIG_Y + 82, Ui::accent(), 2);
        centredLine(tft, "draw it in the air", cx, BIG_Y + 98, Ui::accent(), 2);
    } else {
        centredLine(tft, "Make it with", cx, BIG_Y + 82, Ui::text(), 2);
        centredLine(tft, "your own hand", cx, BIG_Y + 98, Ui::text(), 2);
    }

    /* Back does nothing on the first card, and looks it. */
    Ui::drawButton(tft, backRect(), "< Back", Ui::panel(), Ui::outline(),
                   teachIndex_ == 0 ? Ui::muted() : Ui::text(), false, 2);
    const bool last = teachIndex_ + 1 >= count;
    const uint16_t nextFill = last ? Ui::accent() : Ui::panel();
    Ui::drawButton(tft, nextRect(), last ? "Quiz me" : "Next >", nextFill, Ui::outline(),
                   last ? Ui::onFill(nextFill) : Ui::text(), false, 2);
    drawnTeach_ = teachIndex_;
}

void SignGame::drawQuestion(Ui::Renderer& tft) {
    bool fresh = drawnQuestion_ != question_;
    if (static_cast<uint8_t>(kind_) != drawnKind_) {
        /* The other kind of question has a different layout below the header;
         * clear the body, and only the body. */
        tft.fillRect(0, BODY_Y, GAME_CANVAS_WIDTH, GAME_CANVAS_HEIGHT - BODY_Y, Ui::bg());
        drawnKind_ = static_cast<uint8_t>(kind_);
        fresh = true;
    }
    if (fresh) {
        for (uint8_t i = 0; i < OPTION_COUNT; ++i) drawnOption_[i] = NOT_DRAWN;
        drawnPrompt_ = NOT_DRAWN;
    }
    if (kind_ == Kind::Read) drawRead(tft, fresh);
    else drawFind(tft, fresh);
    drawnQuestion_ = question_;
}

void SignGame::drawPrompt(Ui::Renderer& tft, const Rect& r, const char* line, uint16_t color) {
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    centredLine(tft, line, r.x + r.w / 2, r.y + r.h / 2, color, 2);
}

void SignGame::drawRead(Ui::Renderer& tft, bool fresh) {
    if (fresh) drawHandCard(tft, bigCardRect(), current_, 2);

    const uint8_t prompt = promptState();
    if (prompt != drawnPrompt_) {
        char line[24];
        uint16_t color = Ui::text();
        if (prompt == 0) {
            snprintf(line, sizeof(line), "Which letter?");
        } else {
            snprintf(line, sizeof(line), prompt == 1 ? "Yes! That's %c" : "That's %c",
                     'A' + current_);
            color = prompt == 1 ? Ui::success() : Ui::warning();
        }
        drawPrompt(tft, Rect{SIDE_X, BIG_Y, SIDE_W, PROMPT_H}, line, color);
        drawnPrompt_ = prompt;
    }

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const uint8_t state = optionState(i);
        if (state == drawnOption_[i]) continue;
        const uint16_t fill = state == 1 ? Ui::success() : (state == 2 ? Ui::error() : Ui::panel());
        const char label[2] = { static_cast<char>('A' + options_[i]), '\0' };
        tft.setTextSize(BIG_SIZE);
        Ui::drawButton(tft, letterRect(i), label, fill, Ui::outline(),
                       state == 0 ? Ui::text() : Ui::onFill(fill), false, BIG_FONT);
        tft.setTextSize(1);
        drawnOption_[i] = state;
    }
}

void SignGame::drawFind(Ui::Renderer& tft, bool fresh) {
    const Rect promptRect{6, BIG_Y, GAME_CANVAS_WIDTH - 12, PROMPT_H};
    if (fresh) {
        tft.fillRect(6, FIND_LETTER_Y, GAME_CANVAS_WIDTH - 12, FIND_LETTER_H, Ui::bg());
        drawBigLetter(tft, current_, GAME_CANVAS_WIDTH / 2, FIND_LETTER_Y + FIND_LETTER_H / 2);
        for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
            drawHandCard(tft, handCardRect(i), options_[i], 1);
            drawnOption_[i] = 0;
        }
    }

    const uint8_t prompt = promptState();
    if (prompt != drawnPrompt_) {
        if (prompt == 0) {
            drawPrompt(tft, promptRect, "Which hand is this letter?", Ui::text());
        } else {
            drawPrompt(tft, promptRect, prompt == 1 ? "Yes!" : "This one -- look at it",
                       prompt == 1 ? Ui::success() : Ui::warning());
        }
        drawnPrompt_ = prompt;
    }

    /* Only the frame changes with an answer. Its three rings are painted
     * whole every time, so lit and unlit overwrite each other exactly. */
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const uint8_t state = optionState(i);
        if (state == drawnOption_[i]) continue;
        const Rect r = handCardRect(i);
        for (int16_t k = 0; k < FRAME; ++k) {
            uint16_t c = state == 1 ? Ui::success() : Ui::error();
            if (state == 0) c = k == 0 ? Ui::outline() : Ui::panel();
            tft.drawRect(r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k, c);
        }
        drawnOption_[i] = state;
    }
}

void SignGame::drawResult(Ui::Renderer& tft) {
    const int16_t cx = GAME_CANVAS_WIDTH / 2;
    char line[40];
    centredLine(tft, lastPassed_ ? "Well done!" : "Nearly there!", cx, 84,
                lastPassed_ ? Ui::success() : Ui::warning(), 4);
    snprintf(line, sizeof(line), "%u of %u right", quizRight_, QUIZ_LEN);
    centredLine(tft, line, cx, 116, Ui::text(), 2);

    const char* first = nullptr;
    const char* second = nullptr;
    if (lastPassed_) {
        if (lesson_ + 1 < LESSONS) {
            snprintf(line, sizeof(line), "Lesson %u is open", lesson_ + 2);
            first = "Next lesson";
        } else {
            snprintf(line, sizeof(line), "You know the whole alphabet!");
            first = "Practice";
        }
        second = "Lessons";
    } else {
        snprintf(line, sizeof(line), "%u right opens the next lesson", PASS_MARK);
        first = "Look again";
        second = "Try again";
    }
    centredLine(tft, line, cx, 140, Ui::muted(), 2);

    const uint16_t fill = Ui::accent();
    Ui::drawButton(tft, resultRect(0), first, fill, Ui::outline(), Ui::onFill(fill), false, 2);
    Ui::drawButton(tft, resultRect(1), second, Ui::panel(), Ui::outline(), Ui::text(), false, 2);
}
