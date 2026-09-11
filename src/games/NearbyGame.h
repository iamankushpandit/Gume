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
    /* Two-phase. The list repaints only the rows whose text changed
     * (RowList::drawChanged), and wipes its own rect only when rows arrive,
     * leave or scroll. See docs/RENDER_AUDIT.md. */
    void renderStatic(GameHost& host) override;
    void renderDynamic(GameHost& host) override;
    void end(GameHost& host) override;

private:
    static constexpr uint32_t REFRESH_MS = 1000;
    /* The soonest a heard advertisement rebuilds the list -- see update(). */
    static constexpr uint32_t PEER_REFRESH_MS = 250;

    /* Two phases rather than two screens: naming a peer borrows the whole
     * panel for a keyboard and hands it straight back. A separate app would
     * need a launcher tile and a way in of its own, for something that only
     * ever makes sense while looking at the list it edits. */
    enum class Phase : uint8_t { List, Name };

    void updateName(GameHost& host, const TouchPoint& touch);
    void clearDraft();
    void renderName(GameHost& host);
    Rect nameCancelRect(int16_t screenW, int16_t screenH) const;

public:
    /** For the static_assert in the .cpp; see draft_. */
    static constexpr uint8_t draftCapacity() { return 10; }

private:

    Rect toggleRect(int16_t screenW) const;
    Rect contentRect(int16_t screenW, int16_t screenH) const;

    /* Refill rows_ from the peer table. Reads NVS for each peer's local
     * record, so it runs at most every PEER_REFRESH_MS when the table changed,
     * or on the one-second tick -- never per frame. */
    void rebuildRows(GameHost& host);

    /* Device ids of the peers a Poke chip was built for, indexed by the chip's
     * action id. Held rather than re-derived at press time because the peer
     * table is rebuilt by the scanner on its own schedule: index 2 at the
     * moment of the press is not necessarily the row the finger was aiming at,
     * and poking whoever has since moved up the list is the kind of bug nobody
     * reports because it looks like a mis-tap. Fixed size, no allocation. */
    static constexpr uint8_t MAX_POKE_TARGETS = 8;
    char pokeTargets_[MAX_POKE_TARGETS][5] = {};
    uint8_t pokeTargetCount_ = 0;

    /* The peer poked most recently, and when -- so its chip can say "Poked"
     * for a moment instead of leaving the press unacknowledged. */
    char pokedId_[5] = {0};
    uint32_t pokedAtMs_ = 0;

    Phase phase_ = Phase::List;

    /* Which device the naming phase is editing, and the text so far.
     *
     * A fixed buffer, NOT a String. ProfileGame and WifiGame keep Strings for
     * their user-entered text and CLAUDE.md calls that "the bar" -- but the
     * bar is for text of unbounded length. A peer label is capped at ten
     * characters by the storage it goes into, so the String would buy nothing
     * and cost a live allocation on a screen that is a static instance for the
     * life of the device. DRAFT_MAX is static_asserted against
     * Board::PEER_NAME_MAX in the .cpp so the two cannot drift. */
    static constexpr uint8_t DRAFT_MAX = 10;
    char namingId_[5] = {0};
    char draft_[DRAFT_MAX + 1] = {0};
    uint8_t draftLen_ = 0;

    /* Action-chip ids. Poke chips take 0..MAX_POKE_TARGETS-1 and Name chips
     * take that plus NAME_ACTION_BASE, so one hit test resolves both without a
     * second array to keep in step. */
    static constexpr int8_t NAME_ACTION_BASE = 16;

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
