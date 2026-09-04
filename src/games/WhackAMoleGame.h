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
    void render(AppContext& host) override;

private:
    Rect cellRect(uint8_t index) const;
    int8_t touchedCell(int16_t x, int16_t y) const;
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
    /* Whether the about-to-vanish tick has already sounded for the mole that
     * is currently up. Cleared by spawnMole(), so it is once per mole rather
     * than once per frame for the last quarter-second of its life. */
    bool warned_ = false;

    /* How long before a mole vanishes the warning tick sounds. It has to be
     * shorter than the fastest mole's whole visible time (360ms at level 10)
     * by enough to still be a warning. */
    static constexpr uint16_t WARN_MS = 260;
};
