// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "WhackAMoleGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint8_t GRID = 9;
constexpr int16_t CELL = 20;
constexpr int16_t GRID_X = (GAME_CANVAS_WIDTH - GRID * CELL) / 2;
constexpr int16_t GRID_Y = TOP_BAR_HEIGHT + 28;
constexpr uint16_t CELL_FILL = 0x1085;
constexpr uint16_t SMILE = 0xFFE6;
constexpr uint16_t SMILE_FACE = 0x0843;
constexpr uint16_t HIT = 0x37F0;
constexpr uint16_t MISS = 0xF9EA;
constexpr uint16_t DECOY = 0xC81F;        // a purple frown, unlike the yellow smile

constexpr AppScoreInfo WHACK_A_MOLE_SCORE = {
    "whack", "Whack", "whackBest", "pts", false
};

constexpr AppMetadata WHACK_A_MOLE_METADATA = {
    "whack",
    "Whack",
    "Whack A Mole",
    "hit targets",
    "Whack",
    "Tap the mole before it escapes.",
    &WHACK_A_MOLE_SCORE,
    LauncherIcon::WhackAMole,
    12,
    true,
};
}

const AppMetadata& whackAMoleAppMetadata() {
    return WHACK_A_MOLE_METADATA;
}

const char* WhackAMoleGame::title() const {
    return whackAMoleAppMetadata().screenTitle != nullptr
        ? whackAMoleAppMetadata().screenTitle
        : whackAMoleAppMetadata().title;
}

namespace WC = WhackCourse;

void WhackAMoleGame::begin(AppContext& host) {
    ladder_.start(WC::LEVEL_COUNT, host.courseLevel());
    climbed_ = false;
    score_ = 0;
    bestScore_ = static_cast<uint16_t>(host.getScore(whackAMoleAppMetadata().score->bestKey, 0));
    activeCell_ = -1;
    flashCell_ = -1;
    missStreak_ = 0;
    flashUntil_ = 0;
    nextSpawnAt_ = 0;
    gameOver_ = false;
    spawnMole();
    /* A layout change, not a content one -- the game-over panel has to go, both
     * counters reset, and every cell returns to its idle fill. One full repaint
     * rather than a special case for each. */
    markFullDirty();
}

/* A run with a hit in it. Before the first hit there is no score to lose, and
 * after the tenth miss the run is over -- its best is already stored. */
Game::Keep WhackAMoleGame::keepsGame() const {
    return !gameOver_ && score_ > 0 ? Keep::Ask : Keep::No;
}

/* The mole that is up gets its whole window again rather than what was left
 * of it: at level 10 that could be a few milliseconds, gone before the player
 * has found the board, and left alone it would already have expired -- a miss
 * on the first frame back, the end of a run on a streak of nine. The gap
 * after a hit moves on by the time away, and any flash is over. */
void WhackAMoleGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    if (activeCell_ >= 0) {
        expiresAt_ = millis() + visibleMs();
        warned_ = false;
    }
    movedOn(nextSpawnAt_, awayMs);
    flashCell_ = -1;
}

/* The player's level, kept between visits; it used to be this run's hits
 * divided by five, so every run began slow. */
uint8_t WhackAMoleGame::level() const {
    return ladder_.level();
}

uint16_t WhackAMoleGame::visibleMs() const {
    return WC::visibleMs(level());
}

bool WhackAMoleGame::judge(AppContext& host, WC::Outcome outcome) {
    const Course::Ladder::Step step = ladder_.answer(WC::right(outcome));
    if (step != Course::Ladder::Step::Stay) host.setCourseLevel(ladder_.level());
    climbed_ = step == Course::Ladder::Step::Up;
    if (!climbed_) return false;
    host.pulseRgb(0, 255, 40, 450);
    host.playSound(Sound::LevelUp);
    return true;
}

Rect WhackAMoleGame::cellRect(uint8_t index) const {
    return Rect{
        static_cast<int16_t>(GRID_X + (index % GRID) * CELL),
        static_cast<int16_t>(GRID_Y + (index / GRID) * CELL),
        CELL,
        CELL
    };
}

int8_t WhackAMoleGame::touchedCell(int16_t x, int16_t y) const {
    if (x < GRID_X || y < GRID_Y || x >= GRID_X + GRID * CELL || y >= GRID_Y + GRID * CELL) {
        return -1;
    }
    const uint8_t col = static_cast<uint8_t>((x - GRID_X) / CELL);
    const uint8_t row = static_cast<uint8_t>((y - GRID_Y) / CELL);
    return static_cast<int8_t>(row * GRID + col);
}

void WhackAMoleGame::spawnMole() {
    int8_t next = activeCell_;
    while (next == activeCell_) {
        next = static_cast<int8_t>(random(GRID * GRID));
    }
    activeCell_ = next;
    activeDecoy_ = WC::decoy(level(), esp_random());
    expiresAt_ = millis() + visibleMs();
    nextSpawnAt_ = 0;
    warned_ = false;
}

void WhackAMoleGame::recordMiss() {
    if (missStreak_ < 10) {
        ++missStreak_;
    }
    if (missStreak_ >= 10) {
        gameOver_ = true;
        activeCell_ = -1;
        nextSpawnAt_ = 0;
    }
}

void WhackAMoleGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (flashCell_ >= 0 && now >= flashUntil_) {
        flashCell_ = -1;
        markDirty();
    }

    if (gameOver_) {
        if (touch.justPressed) {
            begin(host);
        }
        return;
    }

    if (activeCell_ < 0 && nextSpawnAt_ > 0 && now >= nextSpawnAt_) {
        spawnMole();
        markDirty();
    } else if (activeCell_ >= 0 && now >= expiresAt_) {
        if (activeDecoy_) {
            /* A decoy let go is the right answer. */
            judge(host, WC::Outcome::DecoyLeft);
            spawnMole();
        } else {
            judge(host, WC::Outcome::Missed);
            recordMiss();
            if (gameOver_) {
                host.pulseRgb(255, 0, 0, 450);
                host.playSound(Sound::GameOver);
            } else {
                spawnMole();
            }
        }
        markDirty();
    } else if (activeCell_ >= 0 && !activeDecoy_ && !warned_ && now + WARN_MS >= expiresAt_) {
        /* The mole is about to leave. At level 10 it is only visible for
         * 360ms, and the miss that ends the run is otherwise the first thing
         * the player hears about it -- a tick that says "now" is the
         * difference between a game of reflexes and a game of luck. Once per
         * mole: `warned_` is cleared by spawnMole(). */
        warned_ = true;
        host.playSound(Sound::Countdown);
    }

    if (!touch.justPressed) {
        return;
    }

    const int8_t cell = touchedCell(touch.x, touch.y);
    if (cell < 0) {
        return;
    }

    if (cell == activeCell_ && activeDecoy_) {
        /* Pressing the frown is the wrong answer, and a miss. */
        judge(host, WC::Outcome::DecoyPressed);
        flashCell_ = cell;
        flashSuccess_ = false;
        flashUntil_ = millis() + 180UL;
        activeCell_ = -1;
        nextSpawnAt_ = millis() + 180UL;
        recordMiss();
        if (gameOver_) {
            host.pulseRgb(255, 0, 0, 450);
            host.playSound(Sound::GameOver);
        } else {
            host.beepError();
        }
    } else if (cell == activeCell_) {
        ++score_;
        missStreak_ = 0;
        if (host.saveBestScore(whackAMoleAppMetadata().score->bestKey, score_, false)) {
            bestScore_ = score_;
        }
        flashCell_ = cell;
        flashSuccess_ = true;
        flashUntil_ = millis() + 160UL;
        activeCell_ = -1;
        nextSpawnAt_ = millis() + 180UL;
        /* Coin, not the "correct answer" beep. A hit here is a point scored
         * rather than a question marked, and a player racking up thirty of
         * them a minute should not be hearing the same two notes that mean
         * "yes, 7 x 8 is 56". The green pulse is beepOk()'s and is kept by
         * hand: on a board with no codec the LED is the whole of the
         * feedback. */
        if (!judge(host, WC::Outcome::Hit)) {
            host.pulseRgb(0, 255, 40, 220);
            host.playSound(Sound::Coin);
        }
    } else {
        judge(host, WC::Outcome::WrongCell);
        flashCell_ = cell;
        flashSuccess_ = false;
        flashUntil_ = millis() + 180UL;
        recordMiss();
        if (gameOver_) {
            host.pulseRgb(255, 0, 0, 450);
            host.playSound(Sound::GameOver);
        } else {
            host.beepError();
        }
    }
    markDirty();
}

void WhackAMoleGame::drawSmile(Ui::Renderer& tft, const Rect& r) const {
    const int16_t cx = r.x + r.w / 2;
    const int16_t cy = r.y + r.h / 2;
    tft.fillCircle(cx, cy, 8, SMILE);
    tft.drawCircle(cx, cy, 8, SMILE_FACE);
    tft.fillCircle(cx - 3, cy - 2, 1, SMILE_FACE);
    tft.fillCircle(cx + 3, cy - 2, 1, SMILE_FACE);
    tft.drawLine(cx - 4, cy + 3, cx - 2, cy + 5, SMILE_FACE);
    tft.drawLine(cx - 2, cy + 5, cx + 2, cy + 5, SMILE_FACE);
    tft.drawLine(cx + 2, cy + 5, cx + 4, cy + 3, SMILE_FACE);
}

/* One cell's interior. The fill is inset by a pixel, so this can never touch
 * the cell's own outline -- which is why the outline is static and this is not.
 * The fill also covers the whole interior, so it erases whatever smile or flash
 * colour was there and the mole simply goes back on top. */
void WhackAMoleGame::drawCell(Ui::Renderer& tft, uint8_t index, uint8_t state) {
    const Rect r = cellRect(index);
    const uint8_t kind = static_cast<uint8_t>((state >> 1) & 0x3);
    const uint16_t fill = kind == 1 ? HIT : (kind == 2 ? MISS : CELL_FILL);
    tft.fillRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, fill);
    if (state & 0x8) {
        /* The decoy: a purple face with the mouth turned down. */
        const int16_t cx = r.x + r.w / 2;
        const int16_t cy = r.y + r.h / 2;
        tft.fillCircle(cx, cy, 8, DECOY);
        tft.drawCircle(cx, cy, 8, SMILE_FACE);
        tft.fillCircle(cx - 3, cy - 2, 1, SMILE_FACE);
        tft.fillCircle(cx + 3, cy - 2, 1, SMILE_FACE);
        tft.drawLine(cx - 4, cy + 5, cx - 2, cy + 3, SMILE_FACE);
        tft.drawLine(cx - 2, cy + 3, cx + 2, cy + 3, SMILE_FACE);
        tft.drawLine(cx + 2, cy + 3, cx + 4, cy + 5, SMILE_FACE);
    } else if (state & 0x1) {
        drawSmile(tft, r);
    }
}

void WhackAMoleGame::renderStatic(AppContext& host) {
    /* Inside a member, because GRID is local to this file and CELL_COUNT is
     * private to the class -- neither can see the other at file scope. */
    static_assert(CELL_COUNT == GRID * GRID,
                  "drawnCell_ must have one byte per cell");
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    /* All eighty-one outlines, once. Their rects come from cellRect() and their
     * colour is the theme's -- neither depends on where the mole is. */
    for (uint8_t i = 0; i < GRID * GRID; ++i) {
        const Rect r = cellRect(i);
        /* Filled first, so each cell is a fill and an outline of one
         * rectangle: that pair is what the d-pad's selector stops at. */
        tft.fillRect(r.x, r.y, r.w, r.h, CELL_FILL);
        tft.drawRect(r.x, r.y, r.w, r.h, Ui::outline());
    }

    for (uint8_t i = 0; i < CELL_COUNT; ++i) {
        drawnCell_[i] = CELL_NONE;
    }
    drawnScore_ = 0xFFFF;
    drawnBest_ = 0xFFFF;
    drawnLevel_ = 0xFF;
    drawnMiss_ = 0xFF;
    drawnOver_ = false;
}

void WhackAMoleGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    for (uint8_t i = 0; i < GRID * GRID; ++i) {
        uint8_t state = 0;
        if (flashCell_ == static_cast<int8_t>(i)) {
            state |= static_cast<uint8_t>((flashSuccess_ ? 1 : 2) << 1);
        }
        if (activeCell_ == static_cast<int8_t>(i)) {
            state |= activeDecoy_ ? 0x8 : 0x1;
        }
        if (state == drawnCell_[i]) {
            continue;
        }
        drawCell(tft, i, state);
        drawnCell_[i] = state;
    }

    /* Both blocks are cleared before they are written, and both erase rects
     * stop above GRID_Y so they can never reach the grid.
     *
     * The right-hand pair needs more care than the left. They are TR_DATUM, so
     * they grow LEFTWARDS -- when one shrinks the stale characters are left at
     * the left end, not the right. "Speed" shrinks every time the level rises,
     * because the mole gets faster: "Speed 1000ms" -> "Speed 900ms". So the
     * rect is measured leftward from the right margin rather than sized around
     * where the text starts. */
    char buf[28];
    if (score_ != drawnScore_ || level() != drawnLevel_ || missStreak_ != drawnMiss_ ||
        climbed_ != drawnClimbed_) {
        tft.fillRect(8, 33, 150, GRID_Y - 33, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Score %u", score_);
        tft.drawString(buf, 8, 35, 2);
        if (climbed_) {
            WC::levelLine(level(), buf, sizeof(buf));   // a step up, until the next mole is judged
        } else {
            snprintf(buf, sizeof(buf), "Level %u Miss %u/10", level(), missStreak_);
        }
        tft.drawString(buf, 8, 51, 1);
        drawnScore_ = score_;
        drawnLevel_ = level();
        drawnMiss_ = missStreak_;
        drawnClimbed_ = climbed_;
    }

    if (bestScore_ != drawnBest_ || visibleMs() != drawnSpeed_) {
        tft.fillRect(GAME_CANVAS_WIDTH - 8 - 150, 33, 150, GRID_Y - 33, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Best %u", bestScore_);
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 35, 2);
        snprintf(buf, sizeof(buf), "Speed %ums", visibleMs());
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 51, 1);
        tft.setTextDatum(TL_DATUM);
        drawnBest_ = bestScore_;
        drawnSpeed_ = visibleMs();
    }

    /* Painted once when the run ends. It never needs erasing here: the only way
     * off it is a tap, which calls begin(), which asks for a full repaint. */
    if (gameOver_ && !drawnOver_) {
        tft.fillRoundRect(46, 88, 228, 72, 8, Ui::panel());
        tft.drawRoundRect(46, 88, 228, 72, 8, Ui::error());
        tft.setTextColor(Ui::error(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Game over", GAME_CANVAS_WIDTH / 2, 110, 4);
        tft.setTextColor(Ui::text(), Ui::panel());
        tft.drawString("Tap to restart", GAME_CANVAS_WIDTH / 2, 140, 2);
        tft.setTextDatum(TL_DATUM);
        drawnOver_ = true;
    }
}

#if GUME_HAS_NAV_KEYS
/* The ring's inner line crosses the mole, and a hit or a miss is not the
 * resting fill: the cell's outline and inside are drawn again. */
bool WhackAMoleGame::repaintUnderRing(AppContext& host, const Rect& box) {
    for (uint8_t i = 0; i < CELL_COUNT; ++i) {
        const Rect r = cellRect(i);
        if (r.x != box.x || r.y != box.y) continue;
        Ui::Renderer& tft = host.display();
        tft.drawRect(r.x, r.y, r.w, r.h, Ui::outline());
        drawCell(tft, i, drawnCell_[i]);
        return true;
    }
    return false;
}
#endif
