// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "ChessRules.h"

#include "engine/Game.h"
#include "games/NearbyWatch.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& chessAppMetadata();

/* Chess: two players over one console, two consoles over the air, or one
 * player against the device.
 *
 * This used to say there was no computer opponent, and that it was a decision
 * rather than an omission, because a search worth playing "would have to run
 * across frames or on its own task". That turned out to be the answer rather
 * than the objection -- Go had already shipped exactly it -- so the engine is
 * in ChessRules.h / ChessAi.cpp and the search is stepped a few milliseconds
 * per frame. What was true in that paragraph and is still true is the reason
 * the engine is deliberately weak: two young players over one console is the thing
 * this device is actually good at, and a computer that always wins is a worse
 * product than one a young player beats half the time.
 *
 * Tap a piece and every square it may legally move to is marked. That is the
 * requested behaviour and it is not a convenience: at 26px a square on a
 * resistive panel, tap-to-select then tap-to-destination is far more forgiving
 * than a drag, because a mis-tap just selects something else and costs
 * nothing. It also happens to be how you teach the game.
 *
 * "Legally" is meant strictly. Moves are generated pseudo-legally and then
 * filtered by making each one and asking whether the mover's own king is
 * attacked -- so pins, discovered checks and the obligation to answer a check
 * all fall out of one rule rather than being special cases. That same filter
 * is what makes checkmate and stalemate detectable: no legal moves and in
 * check is mate, no legal moves and not in check is a draw.
 *
 * Orientation-adaptive (followsLayout), with a panel beside the board in
 * landscape and underneath it in portrait carrying the captured pieces, the
 * status and the one button. The board is square and takes the axis the panel
 * does not: 200px in landscape on a 320x240 console, where it used to be 176
 * with two empty gutters either side of it.
 *
 * The game is remembered. It is written to NVS after every move and again on
 * the way out, so pressing Lock, going Home or running the battery flat brings
 * the same position back -- which matters because the young players this is for put
 * the device down constantly, and a game that evaporated is a game they stop
 * starting. That in turn is why End game exists: once a board survives leaving
 * the screen, walking away is no longer a way to abandon one.
 *
 * No score. A win is not a number, and a "best" would be meaningless.
 */
class ChessGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /* Hands the radio back: a move stays advertised until replaced, so
     * leaving the screen without clearing it would leave this console
     * broadcasting a game it is no longer in. */
    void end(AppContext& host) override;

    /* Piece codes, aliased from ChessRules.h so ChessGame::PAWN still reads
     * the way the sprite table and the renderer have always spelled it. The
     * values live with the rules now, because the engine needs them and the
     * engine may not include a screen. */
    static constexpr int8_t EMPTY = Ch::EMPTY;
    static constexpr int8_t PAWN = Ch::PAWN;
    static constexpr int8_t KNIGHT = Ch::KNIGHT;
    static constexpr int8_t BISHOP = Ch::BISHOP;
    static constexpr int8_t ROOK = Ch::ROOK;
    static constexpr int8_t QUEEN = Ch::QUEEN;
    static constexpr int8_t KING = Ch::KING;

private:
    static constexpr uint8_t NO_SQ = Ch::NO_SQ;
    static constexpr uint8_t MAX_MOVES = Ch::MAX_MOVES;
    /* Most pieces a side can lose: sixteen, less the king, which is never
     * captured -- the rules end the game one move before that could happen. */
    static constexpr uint8_t MAX_TAKEN = 15;

    /* The position, and the rules that move it, live in ChessRules.h. */
    using Position = Ch::Position;

    /* How the game stands, including all the ways it can be over.
     *
     * The three draws matter more here than they would in a chess program for
     * adults, and DrawMaterial matters most of all: beginners trade everything
     * off, so two bare kings is the position they reach constantly. Leaving it
     * undetected meant the console sat there saying "White to move" for a game
     * that was already over by the rules, and the only way out was End game --
     * which then reported "no result" for what chess calls a draw. A young player
     * learning the game would take that as the truth, which is the worst kind
     * of bug this console can have.
     *
     * Ended is different from all of them: a game somebody stopped rather than
     * one the rules finished. Kept separate on purpose -- calling an abandoned
     * game a draw would teach the same wrong lesson in the other direction. */
    enum class Status : uint8_t {
        Playing,
        Check,
        Checkmate,
        Stalemate,
        DrawMaterial,   // neither side has the pieces to mate with
        DrawFifty,      // fifty moves each with no capture and no pawn moved
        Ended,          // a player stopped it
    };

    /* How this game is being played.
     *
     *   Lobby    choosing between passing the device, the computer and a peer
     *   Local    two players, one console -- the original, and the default
     *   Computer one player against this console. See ChessRules.h.
     *   Waiting  we invited somebody and are waiting for them to answer
     *   Remote   a live game against a peer
     *
     * Lost is not a mode, but it is a state the screen shows. A peer that
     * walks out of range simply stops being heard, and the board stays intact
     * -- the position is still correct if they come back, and the moves are
     * advertised state, so a returning console re-hears the current one and
     * play resumes with nothing re-sent. What the radio CAN observe is the
     * silence, and NearbyWatch turns it into a pause with a card: waiting for
     * whom, for how long, keep waiting or end. Before that the game sat on
     * "is thinking" for ever after a flat battery, saying nothing. */
    enum class Mode : uint8_t { Lobby, Local, Computer, Waiting, Remote };

    /* ---- rules -------------------------------------------------------
     *
     * Defined in ChessRules.h, in namespace Ch, with no Arduino anywhere near
     * them so that the engine and the host test can have them. These are thin
     * forwarders rather than a using-declaration on each, because ChessSave
     * and the rest call them unqualified from inside member functions and
     * should not have to care where they went. */
    static bool isWhite(int8_t piece) { return Ch::isWhite(piece); }
    static int8_t kind(int8_t piece) { return Ch::kind(piece); }
    static bool onBoard(int8_t file, int8_t rank) { return Ch::onBoard(file, rank); }
    static uint8_t pseudoMoves(const Position& p, uint8_t from, uint8_t* out) {
        return Ch::pseudoMoves(p, from, out);
    }
    static bool attacked(const Position& p, uint8_t square, bool bySideIsWhite) {
        return Ch::attacked(p, square, bySideIsWhite);
    }
    static uint8_t kingSquare(const Position& p, bool white) {
        return Ch::kingSquare(p, white);
    }
    static int8_t applyMove(Position& p, uint8_t from, uint8_t to) {
        return Ch::applyMove(p, from, to);
    }
    static uint8_t legalMoves(const Position& p, uint8_t from, uint8_t* out) {
        return Ch::legalMoves(p, from, out);
    }
    static bool hasAnyLegalMove(const Position& p) { return Ch::hasAnyLegalMove(p); }
    static bool deadPosition(const Position& p) { return Ch::deadPosition(p); }

    // ---- layout, all measured from the live panel ------------------------
    /* True when the panel is wider than it is tall, which is the only thing
     * the layout below actually branches on. In landscape the board is sized
     * from the full height and everything else goes in a column beside it; in
     * portrait the board is sized from the width and the column becomes a
     * band underneath. Same four rectangles either way. */
    bool sidePanel(AppContext& host) const;
    Rect boardRect(AppContext& host) const;
    /** Everything that is not the board: taken pieces, status, the button. */
    Rect panelRect(AppContext& host) const;
    /** Where the pieces of one colour that have been captured are shown. */
    Rect takenRect(AppContext& host, uint8_t side) const;
    Rect statusRect(AppContext& host) const;
    /** End game while playing, New game once it is over. */
    Rect actionRect(AppContext& host) const;
    Rect squareRect(AppContext& host, uint8_t square) const;
    uint8_t squareAt(AppContext& host, int16_t x, int16_t y) const;

    void drawSquare(AppContext& host, uint8_t square) const;
    void drawPiece(AppContext& host, const Rect& r, int8_t piece) const;
    void drawStatus(AppContext& host) const;
    void drawTaken(AppContext& host, uint8_t side) const;
    void drawAction(AppContext& host) const;
    void refreshStatus();
    /** Note a captured piece for the display. EMPTY is ignored. */
    void recordCapture(int8_t piece);

    // ---- ending, resetting and remembering -------------------------------
    /* Stop the game without a mate. `byUs` distinguishes this console's own
     * decision from the opponent's, which is worth saying on screen: "you
     * ended it" and "they ended it" are different pieces of news. */
    void declareEnd(AppContext& host, bool byUs);
    /** Fresh position, same mode. The board, not the session. */
    void newGame();
    void saveGame(AppContext& host) const;
    /** Restore a game left part-finished. False if there was nothing to take. */
    bool restoreGame(AppContext& host);

    // ---- nearby play -----------------------------------------------------
    /* Our colour in a remote game. Whoever moves first is White, and which
     * console that is comes from the nearby service's coin flip rather than
     * from anything decided here -- see AppContext::nearbyInvite(). Deciding
     * it in the game would mean the next two-player game decides it again,
     * probably differently, and the console that asked for the game would keep
     * on winning the toss. */
    bool remoteIsWhite_ = true;
    /* The peer's advertised tag, which is what the service matches on, and the
     * owner's own label for it, which is what a player is shown. The tag is
     * never displayed when a name exists and the name is never transmitted. */
    char opponent_[5] = {0};
    char opponentName_[11] = {0};
    uint8_t session_ = 0;
    /* Plies we have applied. ourPly_ is the last we published, theirPly_ the
     * last of theirs we accepted. Both count OUR view of the move number, so
     * either side can tell a repeat from a new move without a clock. */
    uint8_t ourPly_ = 0;
    uint8_t theirPly_ = 0;
    /* The move we are advertising, kept so it can be republished unchanged
     * every frame without touching the radio. */
    uint8_t ourFrom_ = 0;
    uint8_t ourTo_ = 0;

    Mode mode_ = Mode::Lobby;
    /* Lobby list, refreshed on a cadence rather than every frame -- reading
     * the peer table walks a small array under a lock, and a lobby that
     * rebuilds at 50Hz is 50Hz of lock contention for a list that changes
     * every few seconds. */
    NearbySeat seats_[6];
    uint8_t seatCount_ = 0;
    uint32_t seatsAtMs_ = 0;

    Rect lobbyRowRect(AppContext& host, uint8_t row) const;
    void renderLobby(AppContext& host);
    void updateLobby(AppContext& host, const TouchPoint& touch);
    /* Accept a peer's move if it is theirs, in this session, the ply we are
     * waiting for, and legal in our position. All four are required; the last
     * is what stops a hostile or confused advertiser corrupting the board. */
    void pollOpponent(AppContext& host);
    /* The other console going quiet: pause, the card, and its two buttons.
     * True when the press was on (or through) the card and must not reach
     * the board. In ChessNet.cpp, beside the poll it belongs with. */
    bool updatePause(AppContext& host, const TouchPoint& touch);
    /* The card over the board while paused, or nothing. Called at the end of
     * both render paths, so a full repaint underneath cannot lose it. */
    void drawPause(AppContext& host);
    NearbyWatch watch_;
    bool pausePainted_ = false;
    uint16_t pauseSecondsDrawn_ = 0;
    void startLocal();
    void startRemote(const NearbySeat& seat, uint8_t session, bool weAreWhite);
    /** True when it is this console's turn in a remote game. */
    bool ourTurn() const;

    /* ---- the computer -------------------------------------------------
     *
     * Chess said for a long time that it had no computer opponent and that
     * this was a decision rather than an omission, on the grounds that a
     * search worth playing would have to run across frames or on a task. The
     * first half of that turned out to be the answer rather than the
     * objection -- Go does exactly it -- so the paragraph is gone and this is
     * here. See ChessRules.h for the engine.
     *
     * Everything the computer needs is held between moves, because the search
     * outlives the frame that started it. */
    void updateComputer(AppContext& host, uint32_t now);
    /** Arm the computer's move: start the clock, and the search if there is one. */
    void beginThinking();
    /** Start a game against this console, resolving Random into a colour. */
    void startComputer();

    /* Lobby geometry. MAX_LOBBY_ROWS is a ceiling on the loop rather than a
     * claim about the panel -- lobbyRowCount() measures what actually fits. */
    static constexpr uint8_t MAX_LOBBY_ROWS = 8;
    Rect lobbyChipRect(AppContext& host, uint8_t index) const;
    uint8_t lobbyRowCount(AppContext& host) const;
    /** True while it is the person's move -- always, unless a computer owes one. */
    bool humanTurn() const;
    /* One move, applied the one way. Used by the person, the network and the
     * computer alike; before this the sequence was written out twice and the
     * third caller is what made that untenable. */
    void playMove(AppContext& host, uint8_t from, uint8_t to, bool publish);

    Ch::Level level_ = Ch::Level::Easy;
    /* Which side the person has in a Computer game. Resolved at the start of
     * the game, never re-rolled -- a restored game must not change colours
     * under the player. */
    bool humanIsWhite_ = true;
    /* What the lobby is set to ask for: White, Black, or a coin toss. Distinct
     * from humanIsWhite_ for exactly that reason. */
    enum class Side : uint8_t { White, Black, Random };
    Side sideChoice_ = Side::White;
    /* The medium search, carried across frames. ~1.3KB, a fixed member like
     * everything else here -- see CLAUDE.md's memory rule. */
    Ch::Search search_;
    /* Easy's own randomness. Seeded from the clock at the start of a game so
     * two games are not identical, and carried rather than re-seeded per move
     * so a move is not correlated with the millisecond it was made in. */
    Ch::Rng rng_;
    bool thinking_ = false;
    /* Not before this. A computer that answers instantly reads as a machine
     * having already known; a beat of delay reads as thought, and it is also
     * where the search actually happens. */
    uint32_t thinkUntilMs_ = 0;
    /* Nothing more can be played. One definition, because there are now five
     * ways to reach it and three places that ask. */
    bool gameOver() const {
        return status_ == Status::Checkmate || status_ == Status::Stalemate ||
               status_ == Status::DrawMaterial || status_ == Status::DrawFifty ||
               status_ == Status::Ended;
    }
    /** What to call the opponent on screen: their name, else their tag. */
    const char* opponentLabel() const {
        return opponentName_[0] != 0 ? opponentName_ : opponent_;
    }

    Position pos_{};
    Status status_ = Status::Playing;
    /* Pieces captured, in the order they went, indexed by the VICTIM's colour:
     * taken_[0] is what White has lost. Kept as a list rather than counts per
     * kind because the order is free information a player reads at a glance --
     * and because rebuilding it from counts to draw it would cost more than
     * storing it. Thirty bytes, static, like everything else here. */
    int8_t taken_[2][MAX_TAKEN] = {};
    uint8_t takenCount_[2] = {0, 0};
    /* Who stopped a declared game. Only meaningful when status_ is Ended. */
    bool endedByUs_ = false;
    /* End game asks twice. Not a modal dialog -- the button relabels itself to
     * "Sure?" and a second press inside this window confirms. A dialog here
     * would be a second screen to lay out in two orientations, and a new set
     * of clear rectangles over a board, for one yes/no question. */
    uint32_t confirmUntilMs_ = 0;
    static constexpr uint32_t CONFIRM_MS = 3000;
    /** The panel, unlike the board, is cheap enough to repaint whole. */
    bool panelStale_ = true;
    uint8_t selected_ = NO_SQ;
    uint8_t targets_[MAX_MOVES] = {};
    uint8_t targetCount_ = 0;
    /* Squares needing repaint next frame. A move changes at most a handful of
     * squares, and a full board is 64 fills plus 32 glyphs -- far more than a
     * frame wants to spend when two of them changed. */
    uint8_t dirtySq_[MAX_MOVES + 4] = {};
    uint8_t dirtyCount_ = 0;
    bool statusStale_ = true;

    void markSquare(uint8_t square);

    /* The whole game, flat, for NVS.
     *
     * Written on every move and again on the way out, so a console that is
     * locked, sent home or simply goes flat mid-game comes back to the same
     * position. That is not a nicety: the players this is for put the device
     * down constantly, and a game that evaporated because somebody pressed
     * Lock is a game they stop starting.
     *
     * A remote game is saved too, session and all. The moves are advertised
     * state rather than messages, so the opponent's board is still on the air
     * when we come back -- there is nothing to re-sync and no handshake to
     * repeat.
     *
     * Guest drops writes, silently, which is what makes Guest a guest. So a
     * guest's game does not survive leaving the screen, and that is the same
     * answer Guest gives to scores and mastery. Do not special-case it here.
     *
     * Fixed layout, and loadBlob() refuses a blob whose length has changed, so
     * altering this struct retires old saves rather than misreading them.
     * `version` covers the case where the size happens to stay the same. */
    struct Saved {
        uint16_t magic;
        uint8_t version;
        int8_t sq[64];
        uint8_t whiteToMove;
        uint8_t castle[4];
        uint8_t epSquare;
        uint8_t halfmove;
        uint8_t status;
        uint8_t mode;
        uint8_t remoteIsWhite;
        uint8_t endedByUs;
        char opponent[5];
        char opponentName[11];
        uint8_t session;
        uint8_t ourPly;
        uint8_t theirPly;
        uint8_t ourFrom;
        uint8_t ourTo;
        /* The computer's settings. Adding these grows the struct, so
         * loadBlob() refuses the old blob on length and games in progress at
         * the upgrade are retired rather than misread -- which is what the
         * fixed layout is for. The version goes up as well, for the case where
         * a future field happens to leave the size unchanged.
         *
         * humanIsWhite is the RESOLVED colour, never the request: a restored
         * game must not re-roll Random and hand the player the other side. */
        uint8_t level;
        uint8_t sideChoice;
        uint8_t humanIsWhite;
        uint8_t takenCount[2];
        int8_t taken[2][MAX_TAKEN];
    };
    static constexpr uint16_t SAVE_MAGIC = 0xC4E5;
    static constexpr uint8_t SAVE_VERSION = 4;
};
