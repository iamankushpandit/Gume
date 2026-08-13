#pragma once

#include "engine/Game.h"
#include "engine/GameCatalog.h"
#include "ui/Ui.h"

class ProfileGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Phase : uint8_t { Pick, Menu, Rename, Games };

    Rect slotRect(uint8_t i) const;
    Rect menuRect(uint8_t i) const;
    Rect addRect() const;
    Rect doneRect() const;
    Rect keyRect(uint8_t row, uint8_t col) const;
    Rect menuActionRect(uint8_t i) const;
    Rect gameCheckRect(uint8_t row) const;
    Rect gamesBackRect() const;
    Rect gamesPrevRect() const;
    Rect gamesNextRect() const;
    uint8_t rowCount(Board& board) const;
    uint8_t profileForRow(Board& board, uint8_t row) const;

    Phase phase_ = Phase::Pick;
    uint8_t editing_ = 0;
    uint8_t menuFor_ = 0;
    uint8_t gameScroll_ = 0;
    String draft_;
};
