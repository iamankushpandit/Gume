#include "MemoryGame.h"
#include "engine/AppRegistry.h"
#include "engine/ContentLoader.h"

namespace {
constexpr uint16_t CARD_BACK = 0x24BD;
constexpr uint16_t CARD_FRONT = 0x39EF;
constexpr uint16_t MATCHED = 0x07F3;
constexpr uint16_t ACCENT = 0xE8E4;
constexpr int16_t GRID_TOP = TOP_BAR_HEIGHT + 30;
constexpr int16_t GRID_GAP = 4;

constexpr AppScoreInfo MEMORY_SCORE = {
    "memory", "Memory", "memBest", "moves", true
};

constexpr AppMetadata MEMORY_METADATA = {
    "memory",
    "Memory",
    "Memory Match",
    "match pairs",
    "Memory",
    "Flip cards to find matching pairs.",
    &MEMORY_SCORE,
    LauncherIcon::Memory,
    1,
    true,
};
}

const AppMetadata& memoryAppMetadata() {
    return MEMORY_METADATA;
}

const char* MemoryGame::title() const {
    return memoryAppMetadata().screenTitle != nullptr
        ? memoryAppMetadata().screenTitle
        : memoryAppMetadata().title;
}

void MemoryGame::begin(AppContext& host) {
    newRound(host);
    markDirty();
}

void MemoryGame::newRound(AppContext& host) {
    host.content().loadMemoryConfig(config_);
    bestMoves_ = static_cast<uint16_t>(host.getScore(memoryAppMetadata().score->bestKey, 0));
    const uint8_t total = config_.rows * config_.cols;
    config_.pairCount = min<uint8_t>(MAX_MEMORY_PAIRS, total / 2);

    for (uint8_t i = 0; i < total; ++i) {
        cards_[i] = i / 2;
        visible_[i] = false;
        matched_[i] = false;
    }
    for (int i = total - 1; i > 0; --i) {
        const uint8_t j = random(i + 1);
        const uint8_t tmp = cards_[i];
        cards_[i] = cards_[j];
        cards_[j] = tmp;
    }
    first_ = -1;
    second_ = -1;
    moves_ = 0;
    resolveAt_ = 0;
    resolving_ = false;
}

Rect MemoryGame::cardRect(uint8_t index) const {
    const uint8_t totalCols = config_.cols;
    const uint8_t totalRows = config_.rows;
    const int16_t maxCellW = (GAME_CANVAS_WIDTH - 20 - GRID_GAP * (totalCols - 1)) / totalCols;
    const int16_t maxCellH = (GAME_CANVAS_HEIGHT - GRID_TOP - 4 - GRID_GAP * (totalRows - 1)) / totalRows;
    const int16_t cell = min<int16_t>(maxCellW, maxCellH);
    const int16_t gridW = totalCols * cell + (totalCols - 1) * GRID_GAP;
    const int16_t gridH = totalRows * cell + (totalRows - 1) * GRID_GAP;
    const int16_t startX = (GAME_CANVAS_WIDTH - gridW) / 2;
    const int16_t startY = GRID_TOP + (GAME_CANVAS_HEIGHT - GRID_TOP - gridH) / 2;
    const uint8_t row = index / totalCols;
    const uint8_t col = index % totalCols;
    return Rect{static_cast<int16_t>(startX + col * (cell + GRID_GAP)), static_cast<int16_t>(startY + row * (cell + GRID_GAP)), cell, cell};
}

int8_t MemoryGame::cardAt(int16_t x, int16_t y) const {
    const uint8_t total = config_.rows * config_.cols;
    for (uint8_t i = 0; i < total; ++i) {
        if (cardRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

bool MemoryGame::allMatched() const {
    const uint8_t total = config_.rows * config_.cols;
    for (uint8_t i = 0; i < total; ++i) {
        if (!matched_[i]) {
            return false;
        }
    }
    return true;
}

void MemoryGame::update(AppContext& host, const TouchPoint& touch) {
    if (resolving_ && millis() >= resolveAt_) {
        visible_[first_] = false;
        visible_[second_] = false;
        first_ = -1;
        second_ = -1;
        resolving_ = false;
        markDirty();
    }

    if (!touch.justPressed || resolving_) {
        return;
    }

    if (allMatched()) {
        /* Tapping past the finished board deals the next one. That is a
         * choice being made, not an answer being marked -- Victory already
         * sounded when the last pair went down. */
        newRound(host);
        host.playSound(Sound::Select);
        /* A layout change, not a content one: every card is reshuffled and
         * turned back over, the counter resets, and the win panel has to go.
         * Asking for a full repaint is what makes all three correct without a
         * special case for any of them. */
        markFullDirty();
        return;
    }

    const int8_t index = cardAt(touch.x, touch.y);
    if (index < 0 || matched_[index] || visible_[index]) {
        return;
    }

    visible_[index] = true;
    if (first_ < 0) {
        first_ = index;
        /* Turning the first card of a pair over. The second one is silent
         * here because it is immediately followed by the match or the
         * mismatch, and two sounds 30ms apart read as one bad one. */
        host.playSound(Sound::Reveal);
    } else {
        second_ = index;
        ++moves_;
        if (cards_[first_] == cards_[second_]) {
            matched_[first_] = true;
            matched_[second_] = true;
            first_ = -1;
            second_ = -1;
            if (allMatched()) {
                /* The board is clear. HighScore if it took fewer moves than
                 * ever before, Victory otherwise -- and neither is the Coin
                 * that an ordinary pair gets, because finishing is not just
                 * another match. */
                const bool best =
                    host.saveBestScore(memoryAppMetadata().score->bestKey, moves_, true);
                if (best) {
                    bestMoves_ = moves_;
                }
                host.pulseRgb(0, 255, 40, 450);
                host.playSound(best ? Sound::HighScore : Sound::Victory);
            } else {
                host.pulseRgb(0, 255, 40, 220);
                host.playSound(Sound::Coin);
            }
        } else {
            resolving_ = true;
            resolveAt_ = millis() + 900UL;
            host.beepError();
        }
    }
    markDirty();
}

/* One card, face and all. The fill covers the whole card rect, so it erases
 * whatever face was there; the outline goes back on top because the fill would
 * otherwise paint over it. Only the shadow is left alone -- it is drawn once in
 * renderStatic() and never moves. */
void MemoryGame::drawCard(Ui::Renderer& tft, uint8_t index, uint8_t face) {
    const Rect r = cardRect(index);
    const uint16_t fill = face == FACE_MATCHED ? MATCHED
                        : (face == FACE_FRONT ? CARD_FRONT : CARD_BACK);
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, TFT_DARKGREY);
    if (face == FACE_BACK) {
        tft.fillCircle(r.x + r.w / 2, r.y + r.h / 2, min(r.w, r.h) / 7, ACCENT);
        return;
    }
    const String label = config_.symbols[cards_[index]];
    const uint8_t font = tft.textWidth(label, 4) <= r.w - 8 ? 4 : 2;
    tft.setTextColor(TFT_BLACK, fill);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, font);
    tft.setTextDatum(TL_DATUM);
}

void MemoryGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    /* The card shadows. Their rects come from cardRect(), which depends on the
     * board shape and not on any card's state, and their colour is a constant
     * -- so they are the same pixels for the whole round however many cards get
     * turned over. Drawing them here rather than per card halves what a card
     * costs to repaint. */
    const uint8_t total = config_.rows * config_.cols;
    for (uint8_t i = 0; i < total; ++i) {
        const Rect r = cardRect(i);
        tft.fillRoundRect(r.x + 2, r.y + 3, r.w, r.h, 6, 0xBDF7);
    }

    /* The panel has just been wiped, so nothing on it is what we last drew.
     * Resetting the trackers is what makes renderDynamic() repaint everything
     * on this pass and only the differences afterwards. */
    for (uint8_t i = 0; i < MAX_MEMORY_CARDS; ++i) {
        drawnFace_[i] = FACE_NONE;
    }
    drawnMoves_ = 0xFFFF;
    drawnBest_ = 0xFFFF;
    drawnPanel_ = false;
}

void MemoryGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    const uint8_t total = config_.rows * config_.cols;
    for (uint8_t i = 0; i < total; ++i) {
        const uint8_t face = matched_[i] ? FACE_MATCHED
                           : (visible_[i] ? FACE_FRONT : FACE_BACK);
        if (face == drawnFace_[i]) {
            continue;   // already on the panel
        }
        drawCard(tft, i, face);
        drawnFace_[i] = face;
    }

    /* Both counters are cleared before they are written. "Moves" grows harmlessly
     * (9 -> 10) but newRound() resets it, and "Best" only ever falls, so both can
     * get NARROWER -- and drawString paints only behind the glyphs it draws, so
     * the tail of the old number would stay. The grid starts at GRID_TOP, which
     * is 30px below the bar, so this rect cannot reach a card. */
    if (moves_ != drawnMoves_ || bestMoves_ != drawnBest_) {
        tft.fillRect(8, TOP_BAR_HEIGHT + 2, 130, 26, Ui::bg());
        char line[24];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(line, sizeof(line), "Moves %u", static_cast<unsigned>(moves_));
        tft.drawString(line, 8, TOP_BAR_HEIGHT + 2, 2);
        if (bestMoves_ > 0) {
            snprintf(line, sizeof(line), "Best %u", static_cast<unsigned>(bestMoves_));
        } else {
            snprintf(line, sizeof(line), "Best --");
        }
        tft.drawString(line, 8, TOP_BAR_HEIGHT + 18, 1);
        drawnMoves_ = moves_;
        drawnBest_ = bestMoves_;
    }

    /* Painted once when the board comes out. It never has to be erased here:
     * the only way off a finished board is newRound(), which is a layout change
     * and asks for a full repaint. */
    if (allMatched() && !drawnPanel_) {
        tft.fillRoundRect(50, 92, 220, 56, 8, Ui::panel());
        tft.drawRoundRect(50, 92, 220, 56, 8, Ui::success());
        tft.setTextColor(Ui::success(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("You matched all!", GAME_CANVAS_WIDTH / 2, 112, 4);
        tft.drawString("Tap to play again", GAME_CANVAS_WIDTH / 2, 136, 2);
        tft.setTextDatum(TL_DATUM);
        drawnPanel_ = true;
    }
}
