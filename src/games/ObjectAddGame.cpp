// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ObjectAddGame.h"
#include <esp_system.h>   // esp_random(), for the deck and the options
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t COL_FIRST   = 0x24BD; // blue
constexpr uint16_t COL_SECOND  = 0x37F0; // green
constexpr uint16_t COL_REMOVED = 0xF9EA; // red/orange for subtraction
constexpr uint16_t OBJ_BTN     = 0x24BD;
constexpr uint16_t OBJ_CORRECT = 0x37F0;
constexpr uint16_t OBJ_WRONG   = 0xF9EA;

constexpr AppMetadata OBJECT_ADD_METADATA = {
    "shapearith",
    "Shape Arith",
    nullptr,
    "add & subtract",
    "Shape Arith",
    "Add and take away shapes.",
    nullptr,
    LauncherIcon::ObjectAdd,
    21,
    true,
};
}

const AppMetadata& objectAddAppMetadata() {
    return OBJECT_ADD_METADATA;
}

const char* ObjectAddGame::title() const {
    return objectAddAppMetadata().screenTitle != nullptr
        ? objectAddAppMetadata().screenTitle
        : objectAddAppMetadata().title;
}

namespace {
using ShapeArithCourse::Op;

/* The two panels, the sign between them and the button row. Everything
 * repainted on its own is cleared from a box derived from these. */
constexpr Rect LEFT_PANEL = {8, 50, 132, 110};
constexpr Rect RIGHT_PANEL = {180, 50, 132, 110};
constexpr int16_t PANEL_BOTTOM = 50 + 110;
constexpr int16_t HEADER_Y = TOP_BAR_HEIGHT;
constexpr int16_t HEADER_H = 50 - TOP_BAR_HEIGHT;
constexpr int16_t SCORE_X = 196;                 // the problem line ends before this
constexpr int16_t BUTTON_Y = 172;
constexpr int16_t BUTTON_H = 42;
constexpr int16_t STRIP_Y = PANEL_BOTTOM + 1;
constexpr int16_t STRIP_H = BUTTON_Y - STRIP_Y;
/* Between the panels: the sign, and "= ?" while a question shows itself. */
constexpr int16_t GAP_X = LEFT_PANEL.x + LEFT_PANEL.w;
constexpr int16_t GAP_W = RIGHT_PANEL.x - GAP_X;

/* Rows of five, as on a ten-frame. Up to ten in two rows; past ten (the
 * start of "Back past ten") four rows of smaller shapes. */
constexpr int16_t PITCH_X = 24;
constexpr int16_t SMALL_R = 10;
constexpr int16_t SMALL_ROW0 = 50;
constexpr int16_t SMALL_PITCH_Y = 26;
constexpr int16_t BIG_R = 9;
constexpr int16_t BIG_ROW0 = 32;
constexpr int16_t BIG_PITCH_Y = 21;
constexpr int16_t GRID_X0 = 66 - 2 * PITCH_X;   // the middle column on the panel's centre
constexpr int16_t LABEL_BOTTOM = 16;

static_assert(ShapeArithCourse::PER_ROW == 5, "the grid is five across");
static_assert(GRID_X0 - SMALL_R - 1 >= 0 &&
              GRID_X0 + 4 * PITCH_X + SMALL_R + 1 <= LEFT_PANEL.w, "five across fit");
static_assert(SMALL_ROW0 - SMALL_R - 1 > LABEL_BOTTOM &&
              SMALL_ROW0 + SMALL_PITCH_Y + SMALL_R + 1 < LEFT_PANEL.h, "two rows fit");
static_assert(BIG_ROW0 - BIG_R - 1 > LABEL_BOTTOM &&
              BIG_ROW0 + 3 * BIG_PITCH_Y + BIG_R + 1 < LEFT_PANEL.h, "four rows fit");
static_assert(ShapeArithCourse::PANEL_MAX <= 20 && ShapeArithCourse::SMALL_GRID == 10,
              "the grids hold ten and twenty");
/* A shape's erase box is 2r+3 square; neighbours must not share a pixel. */
static_assert(2 * SMALL_R + 3 <= PITCH_X && 2 * SMALL_R + 3 <= SMALL_PITCH_Y, "small boxes");
static_assert(2 * BIG_R + 3 <= PITCH_X && 2 * BIG_R + 3 <= BIG_PITCH_Y, "big boxes");
static_assert(STRIP_H >= 9, "a font 1 line fits between the panels and the buttons");

void cell(const Rect& p, uint8_t count, uint8_t idx, int16_t& cx, int16_t& cy, int16_t& r) {
    const bool big = count > ShapeArithCourse::SMALL_GRID;
    r = big ? BIG_R : SMALL_R;
    cx = static_cast<int16_t>(p.x + GRID_X0 + (idx % 5) * PITCH_X);
    cy = static_cast<int16_t>(p.y + (big ? BIG_ROW0 + (idx / 5) * BIG_PITCH_Y
                                         : SMALL_ROW0 + (idx / 5) * SMALL_PITCH_Y));
}

/* What the left panel shows, as one number, so a change is one comparison. */
constexpr uint8_t LEFT_ALL = 0;        // every shape, in the first colour
constexpr uint8_t LEFT_FLASH_ON = 1;   // the ones going, red
constexpr uint8_t LEFT_FLASH_OFF = 2;  // the ones going, faded
constexpr uint8_t LEFT_GONE = 3;       // the ones going, gone
constexpr uint8_t LEFT_EMPTY = 4;      // a frame with empty places
constexpr uint8_t LEFT_FILLED = 5;     // the empty places filled in, the answer

const char* shapeName(uint8_t shape) {
    switch (shape % 4) {
        case 0: return "circles";
        case 1: return "squares";
        case 2: return "triangles";
        default: return "stars";
    }
}

bool reached(uint32_t now, uint32_t at) {
    return static_cast<int32_t>(now - at) >= 0;
}
}   // namespace

Rect ObjectAddGame::answerRect(uint8_t i) const {
    return Rect{static_cast<int16_t>(15 + i * 76), BUTTON_Y, 62, BUTTON_H};
}

void ObjectAddGame::drawShape(Ui::Renderer& tft, const Rect& panel, uint8_t count, uint8_t idx,
                              uint16_t color, bool filled) const {
    int16_t cx, cy, r;
    cell(panel, count, idx, cx, cy, r);
    switch (shape_ % 4) {
        case 0:
            if (filled) tft.fillCircle(cx, cy, r, color);
            tft.drawCircle(cx, cy, r, filled ? TFT_DARKGREY : color);
            break;
        case 1:
            if (filled) tft.fillRoundRect(cx - r, cy - r, 2 * r, 2 * r, 3, color);
            tft.drawRoundRect(cx - r, cy - r, 2 * r, 2 * r, 3, filled ? TFT_DARKGREY : color);
            break;
        case 2:
            Ui::drawTriangleShape(tft, cx, cy, r + 1, color, filled);
            break;
        default:
            Ui::drawStarShape(tft, cx, cy, r, color, filled);
            break;
    }
}

void ObjectAddGame::eraseShape(Ui::Renderer& tft, const Rect& panel, uint8_t count,
                               uint8_t idx) const {
    int16_t cx, cy, r;
    cell(panel, count, idx, cx, cy, r);
    tft.fillRect(cx - r - 1, cy - r - 1, 2 * r + 3, 2 * r + 3, Ui::panel());
}

/* A new game is a new score, not a new start: the level is the player's,
 * kept between visits, and Carry on's New game comes through here too. */
void ObjectAddGame::begin(AppContext& host) {
    score_ = 0;
    rounds_ = 0;
    ladder_.start(ShapeArithCourse::LEVEL_COUNT, host.courseLevel());
    deck_.reset(ShapeArithCourse::poolSize(ladder_.level()));
    announce_ = false;
    newQuestion();
    markDirty();
}

Game::Keep ObjectAddGame::keepsGame() const {
    return score_ > 0 ? Keep::Ask : Keep::No;
}

/* The same question at the same level. A demonstration cut off part way is
 * played again from the start, because the animation IS the question. An
 * answer being shown waits for a tap, so it needs nothing. */
void ObjectAddGame::resume(AppContext&, uint32_t) {
    if (phase_ == Phase::Showing || phase_ == Phase::AnimIn || phase_ == Phase::Flashing) {
        phase_ = Phase::Showing;
        animCount_ = 0;
        flashOn_ = false;
        animNextAt_ = millis() + 900UL;
    }
}

void ObjectAddGame::newQuestion() {
    item_ = deck_.next(esp_random());
    ShapeArithCourse::make(ladder_.level(), item_, esp_random(), q_);
    shape_ = static_cast<uint8_t>(esp_random() % 4);
    showLevel_ = announce_;
    announce_ = false;
    phase_ = Phase::Showing;
    animCount_ = 0;
    flashOn_ = false;
    animNextAt_ = millis() + 900UL;
    selected_ = -1;
    /* A new question is new panels; the buttons are not optional, since one
     * repaints only when its state changed. */
    drawnQuestion_ = false;
}

uint8_t ObjectAddGame::leftState() const {
    switch (q_.op) {
        case Op::Add: return LEFT_ALL;
        case Op::MakeUp: return phase_ == Phase::Feedback ? LEFT_FILLED : LEFT_EMPTY;
        case Op::TakeAway: break;
    }
    if (phase_ == Phase::Flashing) return flashOn_ ? LEFT_FLASH_ON : LEFT_FLASH_OFF;
    if (phase_ == Phase::Question || phase_ == Phase::Feedback) return LEFT_GONE;
    return LEFT_ALL;
}

/* Shapes of the second group on the panel so far. */
uint8_t ObjectAddGame::arrived() const {
    if (q_.op != Op::Add) return 0;
    if (phase_ == Phase::Showing) return 0;
    if (phase_ == Phase::AnimIn) return animCount_;
    return q_.b;
}

/* 0 the question's hint, 1 the level's name, 2 right, 3 wrong. */
uint8_t ObjectAddGame::stripState() const {
    if (phase_ == Phase::Feedback) return selected_ == static_cast<int8_t>(q_.correct) ? 2 : 3;
    return showLevel_ ? 1 : 0;
}

void ObjectAddGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (phase_ == Phase::Showing && reached(now, animNextAt_)) {
        if (q_.op == Op::Add) {
            phase_ = Phase::AnimIn;
            animNextAt_ = now + 450UL;
        } else if (q_.op == Op::TakeAway && q_.b > 0) {
            phase_ = Phase::Flashing;
            animNextAt_ = now + 300UL;
        } else {
            phase_ = Phase::Question;   // a frame to fill, or nothing taken
        }
        markDirty();
        return;
    }
    if (phase_ == Phase::AnimIn && reached(now, animNextAt_)) {
        ++animCount_;
        if (animCount_ >= q_.b) {
            phase_ = Phase::Question;
        } else {
            animNextAt_ = now + 450UL;
        }
        markDirty();
        return;
    }
    /* The group taken away blinks three times and goes. Afterwards it stays
     * on the right-hand panel, so what went is never off the screen. */
    if (phase_ == Phase::Flashing && reached(now, animNextAt_)) {
        flashOn_ = !flashOn_;
        ++animCount_;
        if (animCount_ >= 6) {
            phase_ = Phase::Question;
            flashOn_ = false;
        } else {
            animNextAt_ = now + 300UL;
        }
        markDirty();
        return;
    }

    if (!touch.justPressed) return;

    if (phase_ == Phase::Feedback) {
        newQuestion();
        markDirty();
        return;
    }
    if (phase_ != Phase::Question) return;
    for (uint8_t i = 0; i < 4; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        selected_ = static_cast<int8_t>(i);
        ++rounds_;
        const bool right = i == q_.correct;
        if (right) {
            ++score_;
        } else {
            /* Back after three others. Before the ladder moves: a level change
             * resets the deck, and an item means something only on its level. */
            deck_.missed(item_);
        }
        /* One try a question, so every answer is a first try. */
        const Course::Ladder::Step step = ladder_.answer(right);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
            deck_.reset(ShapeArithCourse::poolSize(ladder_.level()));
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
        phase_ = Phase::Feedback;
        markDirty();
        return;
    }
}

void ObjectAddGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    drawnQuestion_ = false;
    drawnScore_ = 0xFFFF;
    drawnRounds_ = 0xFFFF;
    drawnLevel_ = 0xFF;
}

void ObjectAddGame::drawLeftLabel(Ui::Renderer& tft, uint8_t state) const {
    const Rect& lp = LEFT_PANEL;
    tft.fillRect(lp.x + 6, lp.y + 4, lp.w - 12, LABEL_BOTTOM - 3, Ui::panel());
    char label[16];
    if (q_.op == Op::MakeUp) {
        snprintf(label, sizeof(label), "fill to %u", static_cast<unsigned>(q_.whole));
    } else {
        snprintf(label, sizeof(label), "%s", state == LEFT_GONE ? "left" : "start");
    }
    tft.setTextColor(Ui::muted(), Ui::panel());
    tft.setTextDatum(TC_DATUM);
    tft.drawString(label, lp.x + lp.w / 2, lp.y + 7, 1);
}

/* The left panel whole, in `state`. Idempotent: the same state paints the
 * same pixels, which is what lets a change repaint only the shapes in it. */
void ObjectAddGame::drawLeftPanel(Ui::Renderer& tft, uint8_t state) const {
    const Rect& lp = LEFT_PANEL;
    tft.fillRoundRect(lp.x, lp.y, lp.w, lp.h, 6, Ui::panel());
    tft.drawRoundRect(lp.x, lp.y, lp.w, lp.h, 6, Ui::outline());
    drawLeftLabel(tft, state);
    const uint8_t count = q_.leftCount();
    for (uint8_t i = 0; i < count; ++i) {
        if (q_.op == Op::MakeUp && i >= q_.a) {
            if (state == LEFT_FILLED) drawShape(tft, lp, count, i, COL_SECOND, true);
            else drawShape(tft, lp, count, i, Ui::muted(), false);
            continue;
        }
        const bool going = q_.op == Op::TakeAway && i >= q_.a - q_.b;
        if (!going || state == LEFT_ALL) drawShape(tft, lp, count, i, COL_FIRST, true);
        else if (state == LEFT_FLASH_ON) drawShape(tft, lp, count, i, COL_REMOVED, true);
        else if (state == LEFT_FLASH_OFF) drawShape(tft, lp, count, i, Ui::surface(), true);
    }
}

/* Everything a new question changes: the problem line, both panels, the
 * sign, and the row under them. */
void ObjectAddGame::drawQuestion(Ui::Renderer& tft) {
    tft.fillRect(0, HEADER_Y, SCORE_X, HEADER_H, Ui::bg());
    char problem[40];
    const char* shapes = shapeName(shape_);
    if (q_.op == Op::Add) {
        snprintf(problem, sizeof(problem), "%u + %u %s", static_cast<unsigned>(q_.a),
                 static_cast<unsigned>(q_.b), shapes);
    } else if (q_.op == Op::TakeAway) {
        snprintf(problem, sizeof(problem), "%u take away %u %s", static_cast<unsigned>(q_.a),
                 static_cast<unsigned>(q_.b), shapes);
    } else {
        snprintf(problem, sizeof(problem), "%u + ? = %u %s", static_cast<unsigned>(q_.a),
                 static_cast<unsigned>(q_.whole), shapes);
    }
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(problem, 8, 34, 2);

    drawnLeft_ = leftState();
    drawLeftPanel(tft, drawnLeft_);

    const Rect& rp = RIGHT_PANEL;
    tft.fillRoundRect(rp.x, rp.y, rp.w, rp.h, 6, Ui::panel());
    tft.drawRoundRect(rp.x, rp.y, rp.w, rp.h, 6, Ui::outline());
    tft.setTextColor(Ui::muted(), Ui::panel());
    tft.setTextDatum(TC_DATUM);
    tft.drawString(q_.op == Op::Add ? "add" : (q_.op == Op::TakeAway ? "take away" : "how many more"),
                   rp.x + rp.w / 2, rp.y + 7, 1);
    if (q_.op == Op::TakeAway) {
        for (uint8_t i = 0; i < q_.b; ++i) drawShape(tft, rp, q_.b, i, COL_REMOVED, true);
    } else if (q_.op == Op::MakeUp) {
        tft.setTextColor(Ui::text(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("?", rp.x + rp.w / 2, rp.y + rp.h / 2 + 6, 4);
    }
    drawnArrived_ = 0;

    tft.fillRect(GAP_X, 90, GAP_W, 32, Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    if (q_.op == Op::TakeAway) {
        tft.drawLine(148, 106, 172, 106, COL_REMOVED);
        tft.fillTriangle(172, 106, 164, 101, 164, 111, COL_REMOVED);
    } else {
        tft.drawString("+", GAP_X + GAP_W / 2, 106, 4);
    }
    tft.setTextDatum(TL_DATUM);

    drawnBottom_ = 0xFF;
    drawnStrip_ = 0xFF;
    for (uint8_t i = 0; i < 4; ++i) drawnButton_[i] = 0xFF;
    drawnQuestion_ = true;
}

void ObjectAddGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (!drawnQuestion_) drawQuestion(tft);

    /* The level and the tally, right-aligned, in their own part of the
     * header row so the problem line is not touched. */
    if (score_ != drawnScore_ || rounds_ != drawnRounds_ || ladder_.level() != drawnLevel_) {
        tft.fillRect(SCORE_X, HEADER_Y, GAME_CANVAS_WIDTH - SCORE_X, HEADER_H, Ui::bg());
        char tally[28];
        snprintf(tally, sizeof(tally), "Level %u  %u/%u", static_cast<unsigned>(ladder_.level()),
                 static_cast<unsigned>(score_), static_cast<unsigned>(rounds_));
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TR_DATUM);
        tft.drawString(tally, GAME_CANVAS_WIDTH - 8, 34, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnRounds_ = rounds_;
        drawnLevel_ = ladder_.level();
    }

    /* The shapes that change colour or go: only those, over their own box. */
    const uint8_t left = leftState();
    if (left != drawnLeft_) {
        const uint8_t count = q_.leftCount();
        for (uint8_t i = 0; i < count; ++i) {
            if (q_.op == Op::MakeUp && i >= q_.a) {
                drawShape(tft, LEFT_PANEL, count, i, COL_SECOND, true);
            } else if (q_.op == Op::TakeAway && i >= q_.a - q_.b) {
                if (left == LEFT_GONE) eraseShape(tft, LEFT_PANEL, count, i);
                else drawShape(tft, LEFT_PANEL, count, i,
                               left == LEFT_FLASH_ON ? COL_REMOVED : Ui::surface(), true);
            }
        }
        drawLeftLabel(tft, left);
        drawnLeft_ = left;
    }

    /* The second group arriving: each shape drawn once, nothing erased. */
    const uint8_t in = arrived();
    for (; drawnArrived_ < in; ++drawnArrived_) {
        drawShape(tft, RIGHT_PANEL, q_.b, drawnArrived_, COL_SECOND, true);
    }

    /* The row under the panels: "= ?" while the question shows itself, then
     * a line of words and the four answers. */
    const uint8_t bottom = (phase_ == Phase::Question || phase_ == Phase::Feedback) ? 1 : 0;
    if (bottom != drawnBottom_) {
        if (bottom == 0) {
            tft.fillRect(0, STRIP_Y, GAME_CANVAS_WIDTH, STRIP_H, Ui::bg());
            tft.fillRect(0, BUTTON_Y, GAME_CANVAS_WIDTH, BUTTON_H + 2, Ui::bg());
            tft.setTextColor(Ui::text(), Ui::bg());
            tft.setTextDatum(MC_DATUM);
            tft.drawString("= ?", GAME_CANVAS_WIDTH / 2, 162, 4);
            tft.setTextDatum(TL_DATUM);
            drawnStrip_ = 0xFF;
        } else {
            /* Font 4 reaches a few rows into the button row; the buttons
             * are drawn after this, so they paint over the rest. */
            tft.fillRect(GAP_X, STRIP_Y - 13, GAP_W, STRIP_H + 13 + 4, Ui::bg());
            for (uint8_t i = 0; i < 4; ++i) drawnButton_[i] = 0xFF;
        }
        drawnBottom_ = bottom;
    }
    if (bottom == 0) return;

    const uint8_t strip = stripState();
    if (strip != drawnStrip_) {
        tft.fillRect(0, STRIP_Y, GAME_CANVAS_WIDTH, STRIP_H, Ui::bg());
        char line[ShapeArithCourse::LEVEL_LINE_MAX + 1];
        const char* text;
        uint16_t colour = Ui::muted();
        if (strip == 1) {
            ShapeArithCourse::levelLine(ladder_.level(), line, sizeof(line));
            text = line;
            colour = Ui::accent();
        } else if (strip == 2) {
            text = "Correct - tap for next";
            colour = OBJ_CORRECT;
        } else if (strip == 3) {
            text = "Green is the answer - tap for next";
            colour = OBJ_WRONG;
        } else if (q_.op == Op::Add) {
            text = "How many in all?";
        } else if (q_.op == Op::TakeAway) {
            text = "How many are left?";
        } else {
            snprintf(line, sizeof(line), "How many more make %u?", static_cast<unsigned>(q_.whole));
            text = line;
        }
        tft.setTextColor(colour, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(text, GAME_CANVAS_WIDTH / 2, STRIP_Y + STRIP_H / 2, 1);
        tft.setTextDatum(TL_DATUM);
        drawnStrip_ = strip;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t state = 0;
        if (phase_ == Phase::Feedback) {
            if (i == q_.correct) state = 1;
            else if (i == static_cast<uint8_t>(selected_)) state = 2;
        }
        if (state == drawnButton_[i]) continue;
        const uint16_t fill = state == 1 ? OBJ_CORRECT : (state == 2 ? OBJ_WRONG : OBJ_BTN);
        const uint16_t tc = state == 0 ? TFT_WHITE : TFT_BLACK;
        char label[4];
        snprintf(label, sizeof(label), "%u", static_cast<unsigned>(q_.options[i]));
        Ui::drawButton(tft, answerRect(i), label, fill, TFT_DARKGREY, tc, false, 4);
        drawnButton_[i] = state;
    }
}
