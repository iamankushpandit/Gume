// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "engine/Game.h"
#include "games/NearbyWatch.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& seaBattleAppMetadata();

/* Sea Battle -- battleships, for two players on one console or two nearby.
 *
 * The second game to use the nearby two-player service, and the one that shows
 * whether it was actually a service or just chess with a wrapper. It needed no
 * new wire format and no new call: an invitation, a numbered turn each way, a
 * coin toss for who fires first, and an ending. See src/games/CLAUDE.md.
 *
 * LANDSCAPE ONLY, on the fixed 320x240 canvas, and that is a fit rather than a
 * shortcut. Battleships is two grids -- your waters and theirs -- and a screen
 * wider than it is tall is the shape that holds both. The board takes the
 * height and the column beside it carries a small picture of your own sea, so
 * you can watch shots land on you without changing screens. In portrait the
 * same two things would have to be stacked, and the one you look at most would
 * be the smaller. So `followsLayout` is left off, the runtime forces landscape
 * and scales the canvas up on a bigger panel, and there is no orientation
 * branching anywhere below.
 *
 * EIGHT BY EIGHT, not the usual ten, and that is the wire deciding: the nearby
 * service carries two six-bit values per turn, which is 0..63, which is exactly
 * a square on an 8x8 grid. Ten by ten would need seven bits and the payload is
 * already full at 31 bytes. Eight also gives 25-pixel cells on the smallest
 * panel, which is what a young player's finger on a resistive screen needs.
 *
 * WHAT TRAVELS. A turn says "I fire at square S" and "your last shot was a
 * miss / a hit / a hit that sank something". Nothing about the fleet is ever
 * transmitted: each console keeps its own ships and answers questions about
 * them one square at a time, which is exactly how the board game works and
 * happens to be the most private thing this device does. A win is derived on
 * both sides from counting -- eleven hits taken is a loss, eleven scored is a
 * win -- so there is no result message. The loser does send one more turn:
 * an answer otherwise travels on the next shot, and a sunk fleet has no next
 * shot, so the answer to the last one goes out on a turn of its own.
 *
 * SHIPS ARE PLACED FOR YOU, with a Shuffle button. Tap-to-place with a rotate
 * control is four more controls and a lot of mis-taps at 25 pixels a cell, and
 * the interesting half of battleships is the guessing. Shuffle until the fleet
 * looks lucky, then Ready.
 *
 * No score. Sinking a fleet is not a number, and a "best" would be
 * meaningless -- the same reason Chess and Piano leave it null.
 */
class SeaBattleGame : public AppGame {
public:
#if GUME_HAS_NAV_KEYS
    /* Puts back what the d-pad selector's ring covered (Game.h). */
    bool repaintUnderRing(AppContext& host, const Rect& box) override;
#endif
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /* Writes the game out. A game against a nearby console stays on the air,
     * finished or not: the other console may not have heard the last shot or
     * the ending yet, and this screen keeps the game in memory to come back
     * to. Anything else hands the radio back. */
    void end(AppContext& host) override;
    /* A sea being fired on comes back as it was left, with no question
     * asked: End game is this screen's own way out of one. A fleet still
     * being laid out has no End game beside it, so that one asks. In
     * SeaBattleSave.cpp. */
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

    /** Eight, because the wire carries six bits per value. See the class note. */
    static constexpr uint8_t GRID = 8;
    static constexpr uint8_t CELLS = GRID * GRID;
    static constexpr uint8_t SHIP_COUNT = 4;
    /** One four, one three, two twos: eleven cells of a sixty-four cell sea. */
    static constexpr uint8_t FLEET_CELLS = 11;

private:
    static constexpr uint8_t NO_SHIP = 0xFF;
    static constexpr uint8_t NO_CELL = 0xFF;

    /* What this player knows about one square of the ENEMY's sea. Unknown is
     * every square at the start; Pending is a shot fired over the air whose
     * answer has not come back yet, which exists only in a remote game and is
     * why a shot cannot simply be resolved where it is made. */
    enum class Mark : uint8_t { Unknown, Pending, Miss, Hit };

    /* One player's whole position: the fleet they are defending, what they
     * have learned about the other sea, and what has been fired at them.
     *
     * A local game uses both of these and a remote game uses only the first,
     * which is the same shape either way -- the alternative was two code paths
     * for firing, and firing is the one thing this game does. */
    struct Side {
        uint8_t shipAt[CELLS];   // ship index defending this square, or NO_SHIP
        Mark shot[CELLS];        // what we have learned about THEIR sea
        bool incoming[CELLS];    // squares that have been fired at us
        uint8_t hitsScored;      // our hits on them; FLEET_CELLS is a win
        uint8_t hitsTaken;       // their hits on us; FLEET_CELLS is a loss
        uint8_t sunkAgainstUs;   // our ships fully hit, for the tally line
    };

    enum class Mode : uint8_t { Lobby, Local, Waiting, Remote };
    /* Placing  -- laying out a fleet, with Shuffle and Ready
     * Passing  -- a full-screen curtain, so one player cannot see the other's
     *             sea on a shared console. Never used in a remote game.
     * Firing   -- the game proper
     */
    enum class Phase : uint8_t { Placing, Passing, Firing };

    /* What happened to the last shot, for the status line and for the answer
     * a remote game owes its opponent. None means "nothing fired yet", which
     * is distinct from Miss. */
    enum class Shot : uint8_t { None, Miss, Hit, Sunk };

    // ---- rules ----------------------------------------------------------
    static uint8_t cellOf(uint8_t col, uint8_t row) {
        return static_cast<uint8_t>(row * GRID + col);
    }
    static uint8_t colOf(uint8_t cell) { return static_cast<uint8_t>(cell % GRID); }
    static uint8_t rowOf(uint8_t cell) { return static_cast<uint8_t>(cell / GRID); }
    /** Lay a fresh random fleet into `side`, clearing whatever was there. */
    static void placeFleet(Side& side);
    /** True when every square of the ship defending `cell` has been hit. */
    static bool shipSunk(const Side& side, uint8_t cell);
    /* Resolve a shot at `cell` against `target`, recording the damage on it.
     * Returns what the shooter should be told. */
    static Shot resolve(Side& target, uint8_t cell);

    // ---- layout, all against the fixed 320x240 canvas --------------------
    static Rect boardRect();
    static Rect cellRect(uint8_t cell);
    static Rect panelRect();
    /** The small picture of our own sea, in the column beside the board. */
    static Rect miniRect();
    static Rect miniCellRect(uint8_t cell);
    static Rect statusRect();
    static Rect tallyRect();
    /** Shuffle while placing, End game while playing, New game once over. */
    static Rect actionRect();
    /** Ready, only while placing. */
    static Rect readyRect();
    static uint8_t cellAt(int16_t x, int16_t y);

    void drawBoard(AppContext& host) const;
    void drawCell(AppContext& host, uint8_t cell) const;
    void drawMini(AppContext& host) const;
    void drawStatus(AppContext& host) const;
    void drawTally(AppContext& host) const;
    void drawButtons(AppContext& host) const;
    void renderPass(AppContext& host) const;
    void renderLobby(AppContext& host);
    void updateLobby(AppContext& host, const TouchPoint& touch);
    /* An invitation not answered yet: Cancel, the time limit, and the other
     * console asking us at the same moment. */
    void updateWaiting(AppContext& host, const TouchPoint& touch);
    /** Back to the lobby with a fresh sea. Does not touch the radio. */
    void toLobby();
    /** Tells the d-pad's selector whether the sea takes a press just now. */
    void noteStops() const;

    // ---- nearby ----------------------------------------------------------
    Rect lobbyRowRect(uint8_t row) const;
    void pollOpponent(AppContext& host);
    /* The other console going quiet: pause, the card, and its two buttons.
     * True when the press was on (or through) the card and must not reach
     * the sea. In SeaBattleNet.cpp, beside the poll it belongs with. */
    bool updatePause(AppContext& host, const TouchPoint& touch);
    /** The card over the board while paused, or nothing. End of both renders. */
    void drawPause(AppContext& host);
    NearbyWatch watch_;
    bool pausePainted_ = false;
    uint16_t pauseSecondsDrawn_ = 0;
    /* The last thing said in a game this screen has already left for the
     * lobby -- the answer to the shot that sank us, or our End game -- while
     * the other console may not have heard it. It stays on the air until they
     * acknowledge it or go out of range, or a new game takes its place. */
    bool lastWord_ = false;
    char lastWordPeer_[5] = {0};
    uint8_t lastWordSession_ = 0;
    uint8_t lastWordPly_ = 0;
    bool lastWordLanded(AppContext& host) const;
    void tickLastWord(AppContext& host);
    /* Saves are kept per player and this screen is not, so the save is how a
     * game kept in memory is told from one somebody else was playing. False
     * for Guest, whose saves are dropped: Guest's game is known by there
     * being no save at all. In SeaBattleSave.cpp. */
    bool savesKept_ = false;
    /** At the start of a nearby game: write it out and see whether it stuck. */
    void noteOwner(AppContext& host);
    /** Is the nearby game still in memory the active player's own? */
    bool keptGameIsOurs(AppContext& host) const;
    void startLocal();
    void startRemote(const NearbySeat& seat, uint8_t session, bool weFireFirst);
    bool ourTurn() const;
    const char* opponentLabel() const {
        return opponentName_[0] != 0 ? opponentName_ : opponent_;
    }

    void newGame();
    void fireAt(AppContext& host, uint8_t cell);
    void declareEnd(AppContext& host, bool byUs);
    void saveGame(AppContext& host) const;
    bool restoreGame(AppContext& host);
    bool gameOver() const {
        return won_ || lost_ || ended_;
    }
    /** Whose sea is on screen: player 1 or 2 locally, always us remotely. */
    uint8_t viewer() const { return mode_ == Mode::Local ? turnPlayer_ : 0; }

    Side side_[2] = {};
    Mode mode_ = Mode::Lobby;
    Phase phase_ = Phase::Placing;
    /* Whose turn it is in a local game, and which of the two Sides is being
     * placed or fired from. Always 0 in a remote game -- there is only us. */
    uint8_t turnPlayer_ = 0;
    bool placed_[2] = {false, false};

    bool won_ = false;
    bool lost_ = false;
    bool ended_ = false;
    bool endedByUs_ = false;
    Shot lastShot_ = Shot::None;
    /* The square we last fired at, so a remote game can mark it when the
     * answer finally arrives -- which is a turn later than the shot. */
    uint8_t pendingCell_ = NO_CELL;
    /* What we owe the opponent about THEIR last shot, carried on our next
     * turn. There is no separate answer message, deliberately: a separate
     * message is the one that goes missing. */
    Shot owedReply_ = Shot::None;

    // ---- nearby state ----------------------------------------------------
    bool remoteFiresFirst_ = true;
    char opponent_[5] = {0};
    char opponentName_[11] = {0};
    uint8_t session_ = 0;
    uint8_t ourPly_ = 0;
    uint8_t theirPly_ = 0;
    uint8_t ourShotCell_ = 0;
    uint8_t ourReplyCode_ = 0;

    /* As many consoles as the lobby has rows for. The list is cut to this
     * where it is gathered, so a row that is not drawn cannot be tapped. */
    static constexpr uint8_t LOBBY_PEER_ROWS = 4;
    NearbySeat seats_[6];
    uint8_t seatCount_ = 0;
    uint32_t seatsAtMs_ = 0;
    /* When our invitation went out. It is on the air for a few seconds only,
     * and nobody should sit on "Asking..." for ever. */
    uint32_t invitedAtMs_ = 0;
    static constexpr uint32_t INVITE_ON_AIR_MS = 6000;
    /** An invitation counts as on the air only if heard this recently. */
    static constexpr uint32_t INVITE_HEARD_MS = 3000;
    static constexpr uint32_t WAITING_MS = 30000;

    uint32_t confirmUntilMs_ = 0;
    static constexpr uint32_t CONFIRM_MS = 3000;

    bool fullPaint_ = true;
    bool panelStale_ = true;
    /* Squares needing a repaint. A shot changes one cell; the whole board is
     * 64 fills and is not worth spending a frame on. */
    uint8_t dirty_[4] = {};
    uint8_t dirtyCount_ = 0;
    void markCell(uint8_t cell);

    /* The whole game, flat, for NVS. Written after every shot and again on the
     * way out, for the same reason Chess does it: the players this is for put
     * the device down constantly, and a game that evaporated is one they stop
     * starting. Guest drops writes, which is what makes Guest a guest.
     *
     * loadBlob() refuses a blob whose length has changed, so altering this
     * struct retires old saves rather than misreading them; `version` covers
     * the case where the size happens to stay the same. */
    struct Saved {
        uint16_t magic;
        uint8_t version;
        uint8_t mode;
        uint8_t phase;
        uint8_t turnPlayer;
        uint8_t placed[2];
        uint8_t won;
        uint8_t lost;
        uint8_t ended;
        uint8_t endedByUs;
        uint8_t lastShot;
        uint8_t pendingCell;
        uint8_t owedReply;
        uint8_t remoteFiresFirst;
        char opponent[5];
        char opponentName[11];
        uint8_t session;
        uint8_t ourPly;
        uint8_t theirPly;
        uint8_t ourShotCell;
        uint8_t ourReplyCode;
        uint8_t shipAt[2][CELLS];
        uint8_t shot[2][CELLS];
        uint8_t incoming[2][CELLS];
        uint8_t hitsScored[2];
        uint8_t hitsTaken[2];
        uint8_t sunkAgainstUs[2];
    };
    static constexpr uint16_t SAVE_MAGIC = 0x5EA1;
    static constexpr uint8_t SAVE_VERSION = 1;
};
