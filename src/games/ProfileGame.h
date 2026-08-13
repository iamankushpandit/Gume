#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

/*
 * Who is playing.
 *
 * Shown at boot before the launcher, so each child's scores land in their own
 * slot. Names are editable from a compact letter grid -- a full keyboard would
 * not fit alongside the picker, and a child's name is short.
 */
class ProfileGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Phase : uint8_t { Pick, Rename };

    Rect slotRect(uint8_t i) const;
    Rect renameRect(uint8_t i) const;
    Rect keyRect(uint8_t row, uint8_t col) const;

    Phase phase_ = Phase::Pick;
    uint8_t editing_ = 0;
    String draft_;
};
