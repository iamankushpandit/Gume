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
        recordMiss();
        if (gameOver_) {
            host.pulseRgb(255, 0, 0, 450);
            host.playSound(Sound::GameOver);
        } else {
            spawnMole();
        }
        markDirty();
    } else if (activeCell_ >= 0 && !warned_ && now + WARN_MS >= expiresAt_) {
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
        /* Coin, not the "correct answer" beep. A hit here is a point scored
         * rather than a question marked, and a player racking up thirty of
         * them a minute should not be hearing the same two notes that mean
         * "yes, 7 x 8 is 56". The green pulse is beepOk()'s and is kept by
         * hand: on a board with no codec the LED is the whole of the
         * feedback. */
        host.pulseRgb(0, 255, 40, 220);
        host.playSound(Sound::Coin);
    } else {
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

void WhackAMoleGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    char leftBuf[28];
    snprintf(leftBuf, sizeof(leftBuf), "Score %u", score_);
    tft.drawString(leftBuf, 8, 35, 2);
    snprintf(leftBuf, sizeof(leftBuf), "Level %u Miss %u/10", level(), missStreak_);
    tft.drawString(leftBuf, 8, 51, 1);
    tft.setTextDatum(TR_DATUM);
    char rightBuf[24];
    snprintf(rightBuf, sizeof(rightBuf), "Best %u", bestScore_);
    tft.drawString(rightBuf, GAME_CANVAS_WIDTH - 8, 35, 2);
    snprintf(rightBuf, sizeof(rightBuf), "Speed %ums", visibleMs());
    tft.drawString(rightBuf, GAME_CANVAS_WIDTH - 8, 51, 1);

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
        tft.drawString("Game over", GAME_CANVAS_WIDTH / 2, 110, 4);
        tft.setTextColor(Ui::text(), Ui::panel());
        tft.drawString("Tap to restart", GAME_CANVAS_WIDTH / 2, 140, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
