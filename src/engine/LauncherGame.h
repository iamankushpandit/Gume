#pragma once

#include "engine/Game.h"

class LauncherGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    /* Two-phase: the header is static, the tile grid and pager are dynamic.
     * Paging calls markDirty(), so next/previous repaints the grid alone. */
    void renderStatic(GameHost& host) override;
    void renderDynamic(GameHost& host) override;

    /* The launcher draws no top bar -- it carries its own header, with the
     * wordmark, the profile name and the status badges -- so the base class's
     * strip would be wrong here. Repaints that header instead. */
    bool renderChrome(GameHost& host) override;

private:
    void clampPage(GameHost& host);

    /* Everything above the tile grid, in both orientations. Shared by render()
     * and renderChrome() so the two cannot drift; it paints its own background
     * and assumes nothing about what was underneath. */
    void drawHeader(GameHost& host);

    uint8_t page_ = 0;
};
