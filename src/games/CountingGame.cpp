// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "CountingGame.h"
#include <esp_system.h>   // esp_random(), for the deck and the arrangement
#include "engine/AppRegistry.h"

using CountingCourse::Picture;

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t DOT_COLORS[5] = {0xE8E4, 0x24BD, 0x05D1, 0xFEC0, 0xAA9F};

/* THE SCREEN, top to bottom. Written once, and every clear rectangle below
 * is derived from it: the strip under the panel used to be typed in at
 * y=168 while the panel ran to 174, so every answer erased the panel's
 * bottom edge and the bottom of the third row of objects with it. */
constexpr int16_t PROMPT_Y = 32;
constexpr int16_t PROMPT_H = 26;            // font 4
constexpr int16_t PROMPT_W = GAME_CANVAS_WIDTH - 32;
constexpr int16_t STATS_Y = 58;
constexpr int16_t STATS_H = 12;
constexpr int16_t PANEL_X = 18;
constexpr int16_t PANEL_Y = 76;
constexpr int16_t PANEL_W = 284;
constexpr int16_t BUTTON_X = 15;
constexpr int16_t BUTTON_PITCH = 76;
constexpr int16_t BUTTON_Y = 188;
constexpr int16_t BUTTON_W = 62;
constexpr int16_t BUTTON_H = 40;
constexpr int16_t GAP = 2;
constexpr int16_t STRIP_H = 18;             // font 2, with a row either side
constexpr int16_t STRIP_Y = BUTTON_Y - GAP - STRIP_H;
constexpr int16_t PANEL_H = STRIP_Y - GAP - PANEL_Y;
constexpr int16_t PANEL_CX = PANEL_X + PANEL_W / 2;
constexpr int16_t PANEL_CY = PANEL_Y + PANEL_H / 2;
/* Room inside the panel's rounded border for anything drawn in it. */
constexpr int16_t INSET = 6;
constexpr int16_t INNER_W = PANEL_W - 2 * INSET;
constexpr int16_t INNER_H = PANEL_H - 2 * INSET;

static_assert(PROMPT_Y >= TOP_BAR_HEIGHT, "the prompt line is below the top bar");
static_assert(PROMPT_Y + PROMPT_H <= STATS_Y, "clearing the prompt must not reach the stats");
static_assert(STATS_Y + STATS_H <= PANEL_Y, "the stats line sits above the panel");
static_assert(PANEL_H >= 80, "the strip has squeezed the panel below its pictures");
static_assert(3 * BUTTON_PITCH + BUTTON_X + BUTTON_W <= GAME_CANVAS_WIDTH, "four buttons fit");

/* Single dots: a row, a ten-frame and the scatter grid. */
constexpr int16_t DOT_R = 10;
constexpr int16_t DOT_PITCH = 39;
constexpr int16_t FRAME_PITCH_Y = 30;
constexpr int16_t SCATTER_PITCH_Y = 26;
constexpr int16_t SCATTER_STAGGER = 8;      // odd rows sit a little right
static_assert((CountingCourse::ROW_MAX - 1) * DOT_PITCH + 2 * DOT_R <= INNER_W, "row");
static_assert((CountingCourse::FIVES_PER_ROW - 1) * DOT_PITCH + 2 * DOT_R <= INNER_W, "fives");
static_assert((CountingCourse::FIVES_ROWS - 1) * FRAME_PITCH_Y + 2 * DOT_R <= INNER_H, "fives");
static_assert((CountingCourse::SCATTER_COLS - 1) * DOT_PITCH + SCATTER_STAGGER + 2 * DOT_R <=
                  INNER_W, "scatter");
static_assert((CountingCourse::SCATTER_ROWS - 1) * SCATTER_PITCH_Y + 2 * DOT_R <= INNER_H,
              "scatter");

/* Rows of ten, with a gap after the fifth so ten reads as five and five. */
constexpr int16_t TEN_R = 9;
constexpr int16_t TEN_PITCH = 24;
constexpr int16_t TEN_FIVE_GAP = 10;
constexpr int16_t TEN_PITCH_Y = 26;
static_assert((CountingCourse::TENS_PER_ROW - 1) * TEN_PITCH + TEN_FIVE_GAP + 2 * TEN_R <=
                  INNER_W, "rows of ten");
static_assert((CountingCourse::TENS_ROWS - 1) * TEN_PITCH_Y + 2 * TEN_R <= INNER_H,
              "rows of ten");

/* Rods: ten small dots in an outline, then the loose ones three to a row. */
constexpr int16_t ROD_DOT_R = 3;
constexpr int16_t ROD_DOT_PITCH = 7;
constexpr int16_t ROD_W = 12;
constexpr int16_t ROD_H = 10 * ROD_DOT_PITCH + 4;
constexpr int16_t ROD_PITCH = 16;
constexpr int16_t ONES_R = 5;
constexpr int16_t ONES_PITCH = 14;
constexpr int16_t ONES_COLS = 3;
constexpr int16_t ONES_GAP = 18;
static_assert(ROD_H <= INNER_H, "a rod stands inside the panel");
static_assert((CountingCourse::RODS_MAX - 1) * ROD_PITCH + ROD_W + ONES_GAP +
                  (ONES_COLS - 1) * ONES_PITCH + 2 * ONES_R <= INNER_W, "nine rods and nine");
static_assert((CountingCourse::ONES_MAX + ONES_COLS - 1) / ONES_COLS * ONES_PITCH <= INNER_H,
              "the loose ones");

/* Equal groups. Pairs stand in one row; fives are dice faces, five a row. */
constexpr int16_t PAIR_W = 20;
constexpr int16_t PAIR_H = 36;
constexpr int16_t PAIR_PITCH = 26;
constexpr int16_t PAIR_DOT_R = 6;
constexpr int16_t PAIR_DOT_DY = 9;
constexpr int16_t FIVE_BOX = 34;
constexpr int16_t FIVE_PITCH_X = 44;
constexpr int16_t FIVE_PITCH_Y = 38;
constexpr int16_t FIVE_DOT_R = 5;
constexpr int16_t FIVE_DOT_D = 9;
constexpr uint8_t FIVES_A_ROW = 5;
static_assert((CountingCourse::GROUPS_MAX - 1) * PAIR_PITCH + PAIR_W <= INNER_W, "ten pairs");
static_assert(PAIR_H <= INNER_H && PAIR_DOT_DY + PAIR_DOT_R < PAIR_H / 2, "a pair");
static_assert((FIVES_A_ROW - 1) * FIVE_PITCH_X + FIVE_BOX <= INNER_W, "five fives a row");
static_assert((CountingCourse::GROUPS_MAX + FIVES_A_ROW - 1) / FIVES_A_ROW == 2 &&
                  FIVE_PITCH_Y + FIVE_BOX <= INNER_H, "two rows of fives");
static_assert(FIVE_DOT_D + FIVE_DOT_R < FIVE_BOX / 2, "a five");

/* The buttons print up to two digits at font 4. */
static_assert(CountingCourse::OPTION_MAX <= 99, "an option label is two digits");

constexpr AppScoreInfo COUNTING_SCORE = {
    "counting", "Counting", "countBest", "streak", false
};

constexpr AppMetadata COUNTING_METADATA = {
    "counting",
    "Counting",
    nullptr,
    "tap number",
    "Counting",
    "Count objects, tap the number.",
    &COUNTING_SCORE,
    LauncherIcon::Counting,
    7,
    true,
};

void dot(Ui::Renderer& tft, int16_t x, int16_t y, int16_t r, uint16_t colour) {
    tft.fillCircle(x, y, r, colour);
    tft.drawCircle(x, y, r, TFT_DARKGREY);
}

/* A frame of `cols` x `rows` slots: the first `filled` are dots, the rest
 * empty rings, so seven on a ten-frame is also three short of ten. Each five
 * is one colour. */
void drawFrame(Ui::Renderer& tft, uint8_t filled, uint8_t cols, uint8_t rows, int16_t pitch,
               int16_t fiveGap, int16_t pitchY, int16_t r) {
    const int16_t width = (cols - 1) * pitch + (cols > 5 ? fiveGap : 0);
    const int16_t x0 = PANEL_CX - width / 2;
    const int16_t y0 = PANEL_CY - (rows - 1) * pitchY / 2;
    for (uint8_t i = 0; i < cols * rows; ++i) {
        const uint8_t col = i % cols;
        const int16_t x = x0 + col * pitch + (col >= 5 ? fiveGap : 0);
        const int16_t y = y0 + (i / cols) * pitchY;
        if (i < filled) {
            dot(tft, x, y, r, DOT_COLORS[(i / 5) % 5]);
        } else {
            tft.drawCircle(x, y, r, Ui::outline());
        }
    }
}

void drawRow(Ui::Renderer& tft, uint8_t count) {
    const int16_t x0 = PANEL_CX - (count - 1) * DOT_PITCH / 2;
    for (uint8_t i = 0; i < count; ++i) {
        dot(tft, x0 + i * DOT_PITCH, PANEL_CY, DOT_R, DOT_COLORS[i % 5]);
    }
}

void drawScatter(Ui::Renderer& tft, uint32_t places) {
    constexpr uint8_t COLS = CountingCourse::SCATTER_COLS;
    constexpr uint8_t ROWS = CountingCourse::SCATTER_ROWS;
    const int16_t x0 = PANEL_CX - ((COLS - 1) * DOT_PITCH + SCATTER_STAGGER) / 2;
    const int16_t y0 = PANEL_CY - (ROWS - 1) * SCATTER_PITCH_Y / 2;
    uint8_t drawn = 0;
    for (uint8_t cell = 0; cell < CountingCourse::SCATTER_CELLS; ++cell) {
        if ((places & (1UL << cell)) == 0) continue;
        const uint8_t row = cell / COLS;
        const int16_t x = x0 + (cell % COLS) * DOT_PITCH + (row % 2) * SCATTER_STAGGER;
        dot(tft, x, y0 + row * SCATTER_PITCH_Y, DOT_R, DOT_COLORS[drawn % 5]);
        ++drawn;
    }
}

void drawRods(Ui::Renderer& tft, uint8_t number) {
    const uint8_t tens = number / 10;
    const uint8_t ones = number % 10;
    const uint8_t onesCols = ones < ONES_COLS ? ones : ONES_COLS;
    int16_t width = (tens - 1) * ROD_PITCH + ROD_W;
    if (ones > 0) width += ONES_GAP + (onesCols - 1) * ONES_PITCH + 2 * ONES_R;
    const int16_t x0 = PANEL_CX - width / 2;
    const int16_t top = PANEL_CY - ROD_H / 2;
    for (uint8_t t = 0; t < tens; ++t) {
        const int16_t x = x0 + t * ROD_PITCH;
        /* An outline alone, never a fill and an outline of one rectangle:
         * that pair is what the d-pad selector takes for a control. */
        tft.drawRoundRect(x, top, ROD_W, ROD_H, 3, Ui::outline());
        for (uint8_t d = 0; d < 10; ++d) {
            tft.fillCircle(x + ROD_W / 2, top + 2 + ROD_DOT_PITCH / 2 + d * ROD_DOT_PITCH,
                           ROD_DOT_R, DOT_COLORS[1]);
        }
    }
    if (ones == 0) return;
    const uint8_t rows = (ones + ONES_COLS - 1) / ONES_COLS;
    const int16_t ox = x0 + (tens - 1) * ROD_PITCH + ROD_W + ONES_GAP + ONES_R;
    const int16_t oy = PANEL_CY - (rows - 1) * ONES_PITCH / 2;
    for (uint8_t i = 0; i < ones; ++i) {
        dot(tft, ox + (i % ONES_COLS) * ONES_PITCH, oy + (i / ONES_COLS) * ONES_PITCH, ONES_R,
            DOT_COLORS[0]);
    }
}

void drawGroups(Ui::Renderer& tft, uint8_t groups, uint8_t size) {
    if (size == 2) {
        const int16_t x0 = PANEL_CX - ((groups - 1) * PAIR_PITCH + PAIR_W) / 2;
        for (uint8_t g = 0; g < groups; ++g) {
            const int16_t x = x0 + g * PAIR_PITCH;
            tft.drawRoundRect(x, PANEL_CY - PAIR_H / 2, PAIR_W, PAIR_H, 4, Ui::outline());
            dot(tft, x + PAIR_W / 2, PANEL_CY - PAIR_DOT_DY, PAIR_DOT_R, DOT_COLORS[g % 5]);
            dot(tft, x + PAIR_W / 2, PANEL_CY + PAIR_DOT_DY, PAIR_DOT_R, DOT_COLORS[g % 5]);
        }
        return;
    }
    /* Fives, left-aligned in rows so seven fives read as five and two. */
    static constexpr int8_t FACE[5][2] = {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}};
    const uint8_t cols = groups < FIVES_A_ROW ? groups : FIVES_A_ROW;
    const uint8_t rows = (groups + FIVES_A_ROW - 1) / FIVES_A_ROW;
    const int16_t x0 = PANEL_CX - ((cols - 1) * FIVE_PITCH_X + FIVE_BOX) / 2;
    const int16_t y0 = PANEL_CY - ((rows - 1) * FIVE_PITCH_Y + FIVE_BOX) / 2;
    for (uint8_t g = 0; g < groups; ++g) {
        const int16_t x = x0 + (g % FIVES_A_ROW) * FIVE_PITCH_X;
        const int16_t y = y0 + (g / FIVES_A_ROW) * FIVE_PITCH_Y;
        tft.drawRoundRect(x, y, FIVE_BOX, FIVE_BOX, 4, Ui::outline());
        for (uint8_t d = 0; d < 5; ++d) {
            dot(tft, x + FIVE_BOX / 2 + FACE[d][0] * FIVE_DOT_D,
                y + FIVE_BOX / 2 + FACE[d][1] * FIVE_DOT_D, FIVE_DOT_R, DOT_COLORS[g % 5]);
        }
    }
}

void drawPicture(Ui::Renderer& tft, const CountingCourse::Question& q) {
    switch (q.picture) {
        case Picture::Row:
            drawRow(tft, q.shown);
            break;
        case Picture::Fives:
            drawFrame(tft, q.shown, CountingCourse::FIVES_PER_ROW, CountingCourse::FIVES_ROWS,
                      DOT_PITCH, 0, FRAME_PITCH_Y, DOT_R);
            break;
        case Picture::Scatter:
            drawScatter(tft, q.places);
            break;
        case Picture::Tens:
            drawFrame(tft, q.shown, CountingCourse::TENS_PER_ROW, CountingCourse::TENS_ROWS,
                      TEN_PITCH, TEN_FIVE_GAP, TEN_PITCH_Y, TEN_R);
            break;
        case Picture::Rods:
            drawRods(tft, q.shown);
            break;
        case Picture::Groups:
            drawGroups(tft, q.shown / q.groupSize, q.groupSize);
            break;
    }
}
}   // namespace

const AppMetadata& countingAppMetadata() {
    return COUNTING_METADATA;
}

const char* CountingGame::title() const {
    return countingAppMetadata().title;
}

/* A new game is a new score, not a new start: the level is the player's,
 * kept between visits, and Carry on's New game comes through here too. */
void CountingGame::begin(AppContext& host) {
    score_ = 0;
    rounds_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(countingAppMetadata().score->bestKey, 0));
    ladder_.start(CountingCourse::LEVEL_COUNT, host.courseLevel());
    deck_.reset(CountingCourse::poolSize(ladder_.level()));
    announce_ = false;
    newQuestion();
    markDirty();
}

Game::Keep CountingGame::keepsGame() const {
    return score_ > 0 ? Keep::Ask : Keep::No;
}

Rect CountingGame::answerRect(uint8_t index) const {
    return Rect{static_cast<int16_t>(BUTTON_X + index * BUTTON_PITCH), BUTTON_Y, BUTTON_W,
                BUTTON_H};
}

void CountingGame::newQuestion() {
    item_ = deck_.next(esp_random());
    CountingCourse::make(ladder_.level(), item_, esp_random(), q_);
    showLevel_ = announce_;
    announce_ = false;
    selected_ = -1;
    answered_ = false;
    /* Everything a new question changes. The buttons are not optional: the
     * loop repaints one only when its state changed, so the two that were
     * never highlighted would keep the previous question's numbers. */
    drawnObjects_ = false;
    drawnStats_ = false;
    drawnPrompt_ = false;
    drawnStrip_ = 0xFF;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
}

void CountingGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    if (answered_) {
        newQuestion();
        /* A new question means a different picture, drawn into the same
         * fixed panel over an opaque fill -- content, not layout. */
        markDirty();
        return;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        selected_ = i;
        answered_ = true;
        ++rounds_;
        const bool right = i == q_.correct;
        if (right) {
            ++score_;
            ++streak_;
            if (streak_ > bestStreak_ &&
                host.saveBestScore(countingAppMetadata().score->bestKey, streak_, false)) {
                bestStreak_ = streak_;
            }
        } else {
            streak_ = 0;
            /* Back after three others, until it is answered right. Before
             * the ladder moves: a level change resets the deck, and a miss
             * from the old level must not be dealt on the new one. */
            deck_.missed(item_);
        }
        /* One try a question, so every answer is a first try. */
        const Course::Ladder::Step step = ladder_.answer(right);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
            deck_.reset(CountingCourse::poolSize(ladder_.level()));
        }
        /* Going up is worth a word and a sound: the flourish in place of the
         * plain tick, as Math and Multiply do -- the tick armed first would
         * be cut off by it -- and the colour by hand, since playSound() does
         * not pulse the LED. Going down is not worth either: the questions
         * get easier and that is all a player needs to notice. */
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
        return;
    }
}

/* The question, or on the first question of a level just reached, the level's
 * name -- and then the question itself goes in the strip under the panel, so
 * it is never off the screen. Measured against the live panel: the longest
 * name does not fit font 4 across, and TFT_eSPI would cut it mid-word with no
 * mark. */
void CountingGame::drawPrompt(Ui::Renderer& tft) const {
    tft.fillRect(0, PROMPT_Y, GAME_CANVAS_WIDTH, PROMPT_H, Ui::bg());
    char line[CountingCourse::LEVEL_LINE_MAX + 1];
    const char* text = CountingCourse::askText(q_.ask);
    uint16_t colour = Ui::text();
    if (showLevel_) {
        CountingCourse::levelLine(ladder_.level(), line, sizeof(line));
        text = line;
        colour = Ui::accent();
    }
    const uint8_t font = tft.textWidth(text, 4) <= PROMPT_W ? 4 : 2;
    tft.setTextColor(colour, Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, GAME_CANVAS_WIDTH / 2, PROMPT_Y + PROMPT_H / 2, font);
    tft.setTextDatum(TL_DATUM);
}

void CountingGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    tft.setTextDatum(TL_DATUM);

    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnStreak_ = 0xFFFF;
    drawnRounds_ = 0xFFFF;
    drawnStrip_ = 0xFF;
    drawnStats_ = false;
    drawnPrompt_ = false;
    drawnObjects_ = false;
}

void CountingGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    /* The prompt changes with the level, so it is drawn per question rather
     * than once as chrome. */
    if (!drawnPrompt_) {
        drawPrompt(tft);
        drawnPrompt_ = true;
    }

    /* The objects. None moves while the player is choosing -- so it is gated
     * -- but a new question changes them, so it is dynamic rather than
     * static. A new question must not cost a wipe of the whole screen.
     *
     * fillRoundRect covers the entire area before anything is drawn, so
     * going from nine rods to three dots leaves nothing behind. That opaque
     * fill is the reason this is safe; drawing onto whatever was there would
     * not be. */
    if (!drawnObjects_) {
        tft.fillRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 8, Ui::panel());
        tft.drawRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 8, Ui::outline());
        drawPicture(tft, q_);
        tft.setTextDatum(TL_DATUM);
        drawnObjects_ = true;
    }

    /* One centred line, so it grows both ways from the middle and a shorter
     * one leaves tails at BOTH ends. Cleared across its whole width first;
     * the static_asserts above keep it clear of the prompt and the panel.
     * Rounds is in the gate too: a miss with the streak already at 0 moves
     * nothing else, and the count of questions would sit one behind. */
    if (!drawnStats_ || score_ != drawnScore_ || streak_ != drawnStreak_ ||
        rounds_ != drawnRounds_) {
        tft.fillRect(20, STATS_Y, GAME_CANVAS_WIDTH - 40, STATS_H, Ui::bg());
        char stats[48];
        snprintf(stats, sizeof(stats), "Score %u/%u   Streak %u   Best %u",
                 static_cast<unsigned>(score_), static_cast<unsigned>(rounds_),
                 static_cast<unsigned>(streak_), static_cast<unsigned>(bestStreak_));
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString(stats, GAME_CANVAS_WIDTH / 2, STATS_Y + 2, 1);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnStreak_ = streak_;
        drawnRounds_ = rounds_;
        drawnStats_ = true;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t state = 0;
        if (answered_) {
            if (i == q_.correct) state = 1;
            else if (i == selected_) state = 2;
        }
        if (state == drawnButton_[i]) {
            continue;
        }
        const uint16_t fill = state == 1 ? GREEN : (state == 2 ? RED : BLUE);
        const uint16_t text = state == 0 ? TFT_WHITE : TFT_BLACK;
        char label[4];
        snprintf(label, sizeof(label), "%u", q_.options[i]);
        Ui::drawButton(tft, answerRect(i), label, fill, TFT_DARKGREY, text, false, 4);
        drawnButton_[i] = state;
    }

    /* The strip between the panel and the buttons: the question, while the
     * prompt line is naming a level; then whether the answer was right. Its
     * rows are derived from both neighbours, so clearing it touches neither.
     * The wordings differ in length, hence the clear. */
    uint8_t strip = 0;                      // empty
    if (answered_) {
        strip = selected_ == q_.correct ? 2 : 3;
    } else if (showLevel_) {
        strip = 1;
    }
    if (strip != drawnStrip_) {
        tft.fillRect(20, STRIP_Y, GAME_CANVAS_WIDTH - 40, STRIP_H, Ui::bg());
        if (strip != 0) {
            const char* text = strip == 1 ? CountingCourse::askText(q_.ask)
                             : strip == 2 ? "Correct - tap for next"
                                          : "Green is the answer";
            const uint16_t colour = strip == 1 ? Ui::text() : (strip == 2 ? GREEN : RED);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(colour, Ui::bg());
            tft.drawString(text, GAME_CANVAS_WIDTH / 2, STRIP_Y + STRIP_H / 2, 2);
            tft.setTextDatum(TL_DATUM);
        }
        drawnStrip_ = strip;
    }
}
