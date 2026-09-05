#pragma once

#include "engine/Game.h"
#include "ui/RowList.h"
#include "ui/Ui.h"

/*
 * Nearby -- who else has a Braino in the room, and how they are doing.
 *
 * The exchange is anonymous by construction: a peer is four hex digits of its
 * own Bluetooth MAC, and the only other things that travel are which game is
 * open and the best score recorded for it. No player name, no profile, nothing
 * a name could be reconstructed from. See docs/BLE_BEACON_SPEC.md.
 *
 * It is off by default and needs the BLE beacon on, so this screen has three
 * states rather than one, and says which it is in instead of showing an empty
 * list that could mean any of them:
 *
 *   beacon off      the radio is not on at all -- point at Settings
 *   sharing off     the radio is on, this feature is not -- offer the switch
 *   sharing on      the peer list, or "nobody yet"
 *
 * A system app, so it lays out against tft.width()/height() and works in both
 * orientations.
 */
class NearbyGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    /* Two-phase. The list wipes and redraws its own content rect, so the win
     * here is not repainting it at all when nothing on it can have changed.
     * See docs/RENDER_AUDIT.md. */
    void renderStatic(GameHost& host) override;
    void renderDynamic(GameHost& host) override;
    void end(GameHost& host) override;

private:
    static constexpr uint32_t REFRESH_MS = 1000;

    Rect toggleRect(int16_t screenW) const;
    Rect contentRect(int16_t screenW, int16_t screenH) const;

    /* Refill rows_ from the peer table. Reads NVS for each peer's local
     * record, so it runs when the table changed or on the one-second tick --
     * never per frame, and never while a scroll drag is in flight. */
    void rebuildRows(GameHost& host);

    RowList rows_;
    bool rowsStale_ = true;
    uint32_t lastPeerGeneration_ = 0;
    uint32_t lastRefreshMs_ = 0;
    int16_t scrollOffset_ = 0;
    bool scrolling_ = false;

    /* The toggle only changes when somebody presses it or the beacon is turned
     * off underneath us -- never on the one-second tick. */
    bool drawnRadio_ = false;
    bool drawnSharing_ = false;
    bool drawnToggle_ = false;
    int16_t scrollAnchorY_ = 0;
    int16_t scrollStartOffset_ = 0;
};
