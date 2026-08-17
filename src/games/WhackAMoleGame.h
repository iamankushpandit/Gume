#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& whackAMoleAppMetadata();

class WhackAMoleGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    /* A 9x9 field of holes. The cell size comes from whichever axis runs out
      * first, so the field stays square and fills the panel it is given. */
    Rect fieldRect(const Ui::Frame& f) const;
    Rect cellRect(const Ui::Frame& f, uint8_t index) const;
    int8_t touchedCell(const Ui::Frame& f, int16_t x, int16_t y) const;
    void spawnMole();
    void recordMiss();
    uint8_t level() const;
    uint16_t visibleMs() const;
    void drawSmile(Ui::Renderer& tft, const Rect& r) const;

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
};
