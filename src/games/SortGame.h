#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& sortAppMetadata();

class SortGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    /* Up to six number tiles in a 3x2 tray. Three columns in both
      * orientations -- the ordering task reads across the row. */
    Rect tileBand(const Ui::Frame& f) const;
    Rect tileRect(const Ui::Frame& f, uint8_t index) const;
    void newRound();
    int8_t touchedTile(const Ui::Frame& f, int16_t x, int16_t y) const;
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
    bool flashError_ = false;
    uint32_t flashUntil_ = 0;
};

