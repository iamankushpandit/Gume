#pragma once

#include "engine/Game.h"
#include "engine/ContentLoader.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& memoryAppMetadata();

class MemoryGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase: the board's shadows are position-indexed and never change
     * while a round is up; only the card faces do, and only one or two of them
     * per interaction. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    void drawCard(Ui::Renderer& tft, uint8_t index, uint8_t face);
    void newRound(AppContext& host);
    int8_t cardAt(int16_t x, int16_t y) const;
    Rect cardRect(uint8_t index) const;
    bool allMatched() const;

    MemoryConfig config_;
    uint8_t cards_[MAX_MEMORY_CARDS] = {};
    bool visible_[MAX_MEMORY_CARDS] = {};
    bool matched_[MAX_MEMORY_CARDS] = {};
    int8_t first_ = -1;
    int8_t second_ = -1;
    uint16_t moves_ = 0;
    uint16_t bestMoves_ = 0;
    uint32_t resolveAt_ = 0;
    bool resolving_ = false;

    /* What is currently ON THE PANEL, as distinct from what the game state
     * says. renderDynamic() repaints a card only where the two disagree, which
     * turns "turn one card over" from 24 card redraws into one.
     *
     * Tracked rather than flagged from update() on purpose: this is
     * self-correcting, so a state change nobody remembered to mark still
     * paints. A missed bit in a dirty mask leaves a card showing the wrong
     * face, which is a far worse failure than a redundant repaint.
     * CinnamonGame::litDrawn_[] is the same idea. */
    static constexpr uint8_t FACE_NONE = 0xFF;   // nothing painted there yet
    static constexpr uint8_t FACE_BACK = 0;
    static constexpr uint8_t FACE_FRONT = 1;
    static constexpr uint8_t FACE_MATCHED = 2;
    uint8_t drawnFace_[MAX_MEMORY_CARDS] = {};
    uint16_t drawnMoves_ = 0xFFFF;
    uint16_t drawnBest_ = 0xFFFF;
    bool drawnPanel_ = false;
};
