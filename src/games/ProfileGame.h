#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

/*
 * Who is playing.
 *
 * Shown at boot before the launcher, and reachable any time from the launcher,
 * so switching child never means rebooting. Up to five children can be added
 * and removed here, plus a permanent Guest that plays without saving anything.
 *
 * Names are edited on a compact letter grid: a full keyboard would not fit
 * beside the list, and a child's name is short.
 */
class ProfileGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Phase : uint8_t { Pick, Menu, Rename };

    Rect slotRect(uint8_t i) const;
    Rect menuRect(uint8_t i) const;
    Rect addRect() const;
    Rect doneRect() const;
    Rect keyRect(uint8_t row, uint8_t col) const;
    Rect menuActionRect(uint8_t i) const;
    uint8_t rowCount(Board& board) const;
    uint8_t profileForRow(Board& board, uint8_t row) const;

    Phase phase_ = Phase::Pick;
    uint8_t editing_ = 0;      // 0xFF while creating a new child
    uint8_t menuFor_ = 0;
    String draft_;
};
