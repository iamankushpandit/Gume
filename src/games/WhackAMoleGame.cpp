#include "WhackAMoleGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint8_t GRID = 9;
constexpr int16_t GRID_Y = TOP_BAR_HEIGHT + 28;
constexpr int16_t CELL_MAX = 26;
constexpr uint16_t CELL_FILL = 0x1085;
constexpr uint16_t SMILE = 0xFFE6;
constexpr uint16_t SMILE_FACE = 0x0843;
constexpr uint16_t HIT = 0x37F0;
constexpr uint16_t MISS = 0xF9EA;

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
    5,
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

void WhackAMoleGame::begin(AppContext& host) {
    score_ = 0;
    bestScore_ = static_cast<uint16_t>(host.getScore(whackAMoleAppMetadata().score->bestKey, 0));
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

Rect WhackAMoleGame::fieldRect(const Ui::Frame& f) const {
    /* 20px cells came from 180px of field on a 240px-tall panel. Derive it:
     * landscape is height-bound and lands back on 20, portrait is
     * width-bound and gets 24. */
    int16_t cell = static_cast<int16_t>((f.w - 16) / GRID);
    const int16_t byHeight = static_cast<int16_t>((f.h - GRID_Y - 2) / GRID);
    if (byHeight < cell) {
        cell = byHeight;
    }
    if (cell > CELL_MAX) {
        cell = CELL_MAX;
    }
    const int16_t side = static_cast<int16_t>(cell * GRID);
    return Rect{static_cast<int16_t>((f.w - side) / 2), GRID_Y, side, side};
}

Rect WhackAMoleGame::cellRect(const Ui::Frame& f, uint8_t index) const {
    const Rect field = fieldRect(f);
    const int16_t cell = static_cast<int16_t>(field.w / GRID);
    return Rect{
        static_cast<int16_t>(field.x + (index % GRID) * cell),
        static_cast<int16_t>(field.y + (index / GRID) * cell),
        cell,
        cell
    };
}

int8_t WhackAMoleGame::touchedCell(const Ui::Frame& f, int16_t x, int16_t y) const {
    const Rect field = fieldRect(f);
    if (x < field.x || y < field.y ||
        x >= field.x + field.w || y >= field.y + field.h) {
        return -1;
    }
    const int16_t cell = static_cast<int16_t>(field.w / GRID);
    const uint8_t col = static_cast<uint8_t>((x - field.x) / cell);
    const uint8_t row = static_cast<uint8_t>((y - field.y) / cell);
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
        recordMiss();
        if (!gameOver_) {
            spawnMole();
        }
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    const int8_t cell = touchedCell(Ui::frame(host.display()), touch.x, touch.y);
    if (cell < 0) {
        return;
    }

    if (cell == activeCell_) {
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
        host.beepOk();
    } else {
        flashCell_ = cell;
        flashSuccess_ = false;
        flashUntil_ = millis() + 180UL;
        recordMiss();
        host.beepError();
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

void WhackAMoleGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    const int16_t bannerY = static_cast<int16_t>(
        TOP_BAR_HEIGHT + (f.h - TOP_BAR_HEIGHT) * 38 / 100);
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    char leftBuf[28];
    snprintf(leftBuf, sizeof(leftBuf), "Score %u", score_);
    tft.drawString(leftBuf, 8, TOP_BAR_HEIGHT + 5, 2);
    snprintf(leftBuf, sizeof(leftBuf), "Level %u Miss %u/10", level(), missStreak_);
    tft.drawString(leftBuf, 8, TOP_BAR_HEIGHT + 21, 1);
    tft.setTextDatum(TR_DATUM);
    char rightBuf[24];
    snprintf(rightBuf, sizeof(rightBuf), "Best %u", bestScore_);
    tft.drawString(rightBuf, f.w - 8, TOP_BAR_HEIGHT + 5, 2);
    snprintf(rightBuf, sizeof(rightBuf), "Speed %ums", visibleMs());
    tft.drawString(rightBuf, f.w - 8, TOP_BAR_HEIGHT + 21, 1);

    for (uint8_t i = 0; i < GRID * GRID; ++i) {
        const Rect r = cellRect(f, i);
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
        tft.drawString("Game over", f.cx(), bannerY, 4);
        tft.setTextColor(Ui::text(), Ui::panel());
        tft.drawString("Tap to restart", f.cx(), bannerY + 30, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
