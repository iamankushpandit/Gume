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

    /* Whether each slot currently has a tile BUTTON painted in it, as opposed
     * to bare background. renderDynamic() skips the button chrome on a content
     * change because a tile's frame is identical from page to page -- but that
     * is only true of a slot that already has one. A slot that was empty on the
     * previous page has no frame to reuse, and drawing only its interior leaves
     * a bare rectangle with no rounded edge, border or shadow.
     *
     * Six tiles in landscape, four in portrait; eight is room to spare. */
    static constexpr uint8_t MAX_TILES = 8;
    bool slotHasButton_[MAX_TILES] = {};
};
