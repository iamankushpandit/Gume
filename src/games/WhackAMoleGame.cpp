#include "WhackAMoleGame.h"

namespace {
constexpr uint8_t GRID = 9;
constexpr int16_t CELL = 20;
constexpr int16_t GRID_X = (SCREEN_WIDTH - GRID * CELL) / 2;
constexpr int16_t GRID_Y = TOP_BAR_HEIGHT + 28;
constexpr uint16_t CELL_FILL = 0x1085;
constexpr uint16_t SMILE = 0xFFE6;
constexpr uint16_t SMILE_FACE = 0x0843;
constexpr uint16_t HIT = 0x37F0;
constexpr uint16_t MISS = 0xF9EA;
}

const char* WhackAMoleGame::title() const {
    return "Whack A Mole";
}

void WhackAMoleGame::begin(GameHost& host) {
    score_ = 0;
    bestScore_ = static_cast<uint16_t>(host.board().getScore("whackBest", 0));
    activeCell_ = -1;
    flashCell_ = -1;
    missStreak_ = 0;
    flashUntil_ = 0;
    nextSpawnAt_ = 0;
    gameOver_ = false;
    spawnMole();
    markDirty();
}

uint8_t WhackAMoleGame::level() const {
    return min<uint8_t>(10, 1 + score_ / 5);
}

uint16_t WhackAMoleGame::visibleMs() const {
    const uint16_t speed = static_cast<uint16_t>(1180 - (level() - 1) * 90);
    return max<uint16_t>(360, speed);
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
    expiresAt_ = millis() + visibleMs();
    nextSpawnAt_ = 0;
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

void WhackAMoleGame::update(GameHost& host, const TouchPoint& touch) {
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
        recordMiss();
        if (!gameOver_) {
            spawnMole();
        }
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    const int8_t cell = touchedCell(touch.x, touch.y);
    if (cell < 0) {
        return;
    }

    if (cell == activeCell_) {
        ++score_;
        missStreak_ = 0;
        if (host.board().saveBestScore("whackBest", score_, false)) {
            bestScore_ = score_;
        }
        flashCell_ = cell;
        flashSuccess_ = true;
        flashUntil_ = millis() + 160UL;
        activeCell_ = -1;
        nextSpawnAt_ = millis() + 180UL;
        host.board().beepOk();
    } else {
        flashCell_ = cell;
        flashSuccess_ = false;
        flashUntil_ = millis() + 180UL;
        recordMiss();
        host.board().beepError();
    }
    markDirty();
}

void WhackAMoleGame::drawSmile(TFT_eSPI& tft, const Rect& r) const {
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

void WhackAMoleGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Score ") + score_, 8, 35, 2);
    tft.drawString(String("Level ") + level() + " Miss " + missStreak_ + "/10", 8, 51, 1);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Best ") + bestScore_, SCREEN_WIDTH - 8, 35, 2);
    tft.drawString(String("Speed ") + visibleMs() + "ms", SCREEN_WIDTH - 8, 51, 1);

    for (uint8_t i = 0; i < GRID * GRID; ++i) {
        const Rect r = cellRect(i);
        uint16_t fill = CELL_FILL;
        if (flashCell_ == i) {
            fill = flashSuccess_ ? HIT : MISS;
        }
        tft.fillRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, fill);
        tft.drawRect(r.x, r.y, r.w, r.h, Ui::outline());
        if (activeCell_ == i) {
            drawSmile(tft, r);
        }
    }

    if (gameOver_) {
        tft.fillRoundRect(46, 88, 228, 72, 8, Ui::panel());
        tft.drawRoundRect(46, 88, 228, 72, 8, Ui::error());
        tft.setTextColor(Ui::error(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Game over", SCREEN_WIDTH / 2, 110, 4);
        tft.setTextColor(Ui::text(), Ui::panel());
        tft.drawString("Tap to restart", SCREEN_WIDTH / 2, 140, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
