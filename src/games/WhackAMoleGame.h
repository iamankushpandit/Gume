#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& whackAMoleAppMetadata();

class WhackAMoleGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. The grid's 81 outlines never change; a spawn moves the smile
     * between two cells and a flash expiring touches one, out of eighty-one.
     * See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    Rect cellRect(uint8_t index) const;
    int8_t touchedCell(int16_t x, int16_t y) const;
    void spawnMole();
    void recordMiss();
    uint8_t level() const;
    uint16_t visibleMs() const;
    void drawSmile(Ui::Renderer& tft, const Rect& r) const;
    void drawCell(Ui::Renderer& tft, uint8_t index, uint8_t state);

    int8_t activeCell_ = -1;
    int8_t flashCell_ = -1;
    uint16_t score_ = 0;
    uint16_t bestScore_ = 0;
    uint8_t missStreak_ = 0;
    uint32_t expiresAt_ = 0;
    uint32_t nextSpawnAt_ = 0;
    uint32_t flashUntil_ = 0;
    bool flashSuccess_ = false;
    bool gameOver_ = false;
    /* Whether the about-to-vanish tick has already sounded for the mole that
     * is currently up. Cleared by spawnMole(), so it is once per mole rather
     * than once per frame for the last quarter-second of its life. */
    bool warned_ = false;

    /* How long before a mole vanishes the warning tick sounds. It has to be
     * shorter than the fastest mole's whole visible time (360ms at level 10)
     * by enough to still be a warning. */
    static constexpr uint16_t WARN_MS = 260;

    /* What is currently ON THE PANEL, one byte per cell: bit 0 is the mole,
     * bits 1-2 are the fill (normal / hit / miss). renderDynamic() repaints a
     * cell only where this disagrees with the live state.
     *
     * It matters more here than anywhere converted so far, because this screen
     * is driven by three timers rather than by the player -- the spawn, the
     * mole's expiry and flashUntil_ -- so it repaints several times a second
     * whether or not anyone touches it. Tracking rather than flagging is also
     * what keeps a missed invalidation from leaving a mole painted on a cell it
     * has already left. CELL_COUNT is checked against GRID * GRID in the .cpp,
     * where GRID lives. */
    static constexpr uint8_t CELL_COUNT = 81;
    static constexpr uint8_t CELL_NONE = 0xFF;   // nothing painted there yet
    uint8_t drawnCell_[CELL_COUNT] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnBest_ = 0xFFFF;
    uint8_t drawnLevel_ = 0xFF;
    uint8_t drawnMiss_ = 0xFF;
    bool drawnOver_ = false;
};
