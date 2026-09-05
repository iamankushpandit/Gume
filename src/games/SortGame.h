#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& sortAppMetadata();

class SortGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. Tapping a tile locks one of up to six; the flash timer
     * touches one. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    Rect tileRect(uint8_t index) const;
    void newRound();
    int8_t touchedTile(int16_t x, int16_t y) const;
    int16_t expectedValue() const;
    bool allLocked() const;

    int16_t numbers_[6] = {};
    int16_t ordered_[6] = {};
    bool locked_[6] = {};
    uint8_t count_ = 4;
    uint8_t next_ = 0;
    bool ascending_ = true;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t flashTile_ = -1;

    /* What is on the panel: locked state and flash state per tile, packed. */
    uint8_t drawnTile_[6] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnBest_ = 0xFFFF;
    bool drawnSorted_ = false;
    bool flashError_ = false;
    uint32_t flashUntil_ = 0;
};

