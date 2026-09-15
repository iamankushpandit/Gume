// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ChessGame.h"

#include <string.h>

#include "ChessInternal.h"
#include "engine/AppRegistry.h"

/* Chess: the screen. Six files against one header --
 *   ChessGame.cpp   this: the game's life, a fresh board, a tap on it
 *   ChessRules.cpp  move generation, check, and the ways a game ends
 *   ChessDraw.cpp   geometry and every pixel
 *   ChessNet.cpp    the lobby and a game against a nearby console
 *   ChessSave.cpp   remembering a game in NVS
 * Split by line range from one 1450-line file; nothing moved changed. */

namespace {

/* How long the computer appears to think, and how much of each frame it may
 * actually spend doing it.
 *
 * The pause is deliberate and is not the search: Easy decides in well under a
 * millisecond, and a reply that lands the instant your finger leaves the glass
 * reads as a machine that had already decided rather than an opponent. 650ms
 * is copied from Go, where the same question was settled by playing it.
 *
 * The slice is the real constraint. 4000us leaves the rest of the 20ms frame
 * for touch and for the repaint that follows a move -- see CLAUDE.md's
 * responsiveness rule. Medium's search is stepped a slice at a time until it
 * finishes; it is never allowed to run to completion inside one frame. */
constexpr uint32_t CPU_THINK_MS = 650;
constexpr uint32_t CPU_SLICE_US = 4000;

constexpr AppMetadata CHESS_METADATA = {
    "chess",
    "Chess",
    nullptr,
    "friend or computer",
    "Chess",
    "A friend or the computer. Tap to see moves.",
    nullptr,
    LauncherIcon::Chess,
    32,
    true,
};

}   // namespace

const AppMetadata& chessAppMetadata() {
    return CHESS_METADATA;
}

const char* ChessGame::title() const {
    return chessAppMetadata().screenTitle != nullptr
        ? chessAppMetadata().screenTitle
        : chessAppMetadata().title;
}

// ---------------------------------------------------------------- screen

/* A fresh position in whatever mode is already running. The board, not the
 * session: a remote game resets to the starting position with the same
 * opponent, and only the lobby changes who is playing whom. */
void ChessGame::newGame() {
    watch_.reset();
    pausePainted_ = false;
    memset(pos_.sq, EMPTY, sizeof(pos_.sq));
    for (int8_t f = 0; f < 8; ++f) {
        pos_.sq[idx(f, 0)] = BACK_RANK[f];
        pos_.sq[idx(f, 1)] = PAWN;
        pos_.sq[idx(f, 6)] = -PAWN;
        pos_.sq[idx(f, 7)] = static_cast<int8_t>(-BACK_RANK[f]);
    }
    pos_.whiteToMove = true;
    for (bool& c : pos_.castle) c = true;
    pos_.epSquare = NO_SQ;
    pos_.halfmove = 0;

    selected_ = NO_SQ;
    targetCount_ = 0;
    dirtyCount_ = 0;
    status_ = Status::Playing;
    statusStale_ = true;
    takenCount_[0] = takenCount_[1] = 0;
    endedByUs_ = false;
    confirmUntilMs_ = 0;
    ourPly_ = 0;
    theirPly_ = 0;
    panelStale_ = true;
    markFullDirty();
}

void ChessGame::begin(AppContext& host) {
    newGame();
    mode_ = Mode::Lobby;
    seatCount_ = 0;
    seatsAtMs_ = 0;
    opponent_[0] = 0;
    opponentName_[0] = 0;
    session_ = 0;

    /* A game left part-finished comes back. Anything else -- no save, a save
     * from an older layout, a game that had already ended -- falls through to
     * the lobby, which is also what a first visit gets. */
    restoreGame(host);
    markFullDirty();
}


/* end() rather than begin() is where the radio is handed back. A move is a
 * state that stays on the air until it is replaced, so leaving the screen
 * without clearing it would leave this console advertising a game it is no
 * longer playing -- and the score field it displaces would stay missing.
 *
 * The board is written here as well as after each move. After each move is
 * what survives a flat battery; here is what survives everything else, and
 * costs one NVS write on a screen change rather than one per frame. */
void ChessGame::end(AppContext& host) {
    if (mode_ == Mode::Local || mode_ == Mode::Computer ||
        mode_ == Mode::Remote) {
        saveGame(host);
    }
    host.nearbyStop();
}

void ChessGame::recordCapture(int8_t piece) {
    if (piece == EMPTY) return;
    const uint8_t side = isWhite(piece) ? 0 : 1;
    if (takenCount_[side] < MAX_TAKEN) {
        taken_[side][takenCount_[side]++] = piece;
        panelStale_ = true;
    }
}

/* Stop a game nobody is going to finish.
 *
 * This is a result, not an escape hatch: it is stored like one, the status
 * line says so, and the button afterwards offers a new game. Two young players
 * abandon games constantly -- one of them loses interest, or the bell goes --
 * and before this the only exit was to leave the screen, which now brings the
 * same stuck position straight back.
 *
 * Over the radio it rides the move field with `from` equal to `to`. That is
 * never a legal chess move, so it cannot be confused with one, and it costs no
 * bytes on a payload that is already exactly 31. */
void ChessGame::declareEnd(AppContext& host, bool byUs) {
    status_ = Status::Ended;
    endedByUs_ = byUs;
    confirmUntilMs_ = 0;
    if (mode_ == Mode::Remote && byUs) {
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        /* Published through the service's own call rather than by writing a
         * reserved square pair, so what "ended" looks like on the wire stays
         * one fact in one place. ourFrom_/ourTo_ are left alone: the frame
         * loop republishes them, and it must not republish this as a move. */
        host.nearbyEnd(session_, ourPly_, theirPly_);
    }
    for (uint8_t t = 0; t < targetCount_; ++t) markSquare(targets_[t]);
    markSquare(selected_);
    selected_ = NO_SQ;
    targetCount_ = 0;
    statusStale_ = true;
    panelStale_ = true;
    saveGame(host);
    markDirty();
}

void ChessGame::startLocal() {
    mode_ = Mode::Local;
    opponent_[0] = 0;
    newGame();
}

void ChessGame::startComputer() {
    mode_ = Mode::Computer;
    opponent_[0] = 0;
    /* The coin toss happens ONCE, here, and what is stored afterwards is the
     * colour rather than the request. A restored game that re-rolled Random
     * would hand the player the other side of the board they had been
     * playing, which is the kind of bug that reads as the console cheating. */
    rng_.s = static_cast<uint32_t>(millis()) | 1u;
    humanIsWhite_ = sideChoice_ == Side::White   ? true
                    : sideChoice_ == Side::Black ? false
                                                 : (rng_.next() & 1u) != 0;
    newGame();
    /* White moves first, so a player who chose Black is owed a move before
     * they have done anything. */
    if (!humanTurn()) beginThinking();
}

void ChessGame::startRemote(const NearbySeat& seat, uint8_t session,
                            bool weAreWhite) {
    strncpy(opponent_, seat.deviceId, sizeof(opponent_) - 1);
    opponent_[sizeof(opponent_) - 1] = 0;
    /* The label is copied once, here, rather than resolved on every frame:
     * it is display text and the peer table is behind a lock. It is saved with
     * the game for the same reason -- coming back to "A4F2 is thinking" after
     * naming that console RAVI would look like the name had not taken. */
    strncpy(opponentName_, seat.name, sizeof(opponentName_) - 1);
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = static_cast<uint8_t>(session & 0x3F);
    remoteIsWhite_ = weAreWhite;
    mode_ = Mode::Remote;
    newGame();
}

bool ChessGame::ourTurn() const {
    if (mode_ == Mode::Computer) return humanTurn();
    if (mode_ != Mode::Remote) return true;
    return pos_.whiteToMove == remoteIsWhite_;
}

bool ChessGame::humanTurn() const {
    if (mode_ != Mode::Computer) return true;
    return pos_.whiteToMove == humanIsWhite_;
}

/* Every move goes through here: the person's tap, the opponent's broadcast and
 * the computer's choice alike.
 *
 * It exists because the third caller made the duplication untenable. The same
 * eight steps -- mark what moved, apply, note the capture, clear the
 * selection, recompute the status, save, sound, repaint -- were written out in
 * ChessGame.cpp and again in ChessNet.cpp, and they had already drifted: only
 * one of them repainted the rank a castling rook crosses. */
void ChessGame::playMove(AppContext& host, uint8_t from, uint8_t to, bool publish) {
    markSquare(from);
    markSquare(to);
    /* Castling and en passant move or remove a piece on a square nobody
     * touched, so those are repainted too. Marking the whole home rank and the
     * captured pawn's square is cheaper than working out which case applied. */
    for (int8_t f = 0; f < 8; ++f) markSquare(idx(f, rankOf(from)));
    if (pos_.epSquare != NO_SQ) {
        markSquare(idx(fileOf(pos_.epSquare), rankOf(from)));
    }
    for (uint8_t t = 0; t < targetCount_; ++t) markSquare(targets_[t]);

    recordCapture(applyMove(pos_, from, to));
    if (publish && mode_ == Mode::Remote) {
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        ourFrom_ = from;
        ourTo_ = to;
        host.nearbyPublish(session_, ourPly_, ourFrom_, ourTo_, theirPly_);
    }
    selected_ = NO_SQ;
    targetCount_ = 0;
    refreshStatus();
    /* Written after every move, not just on the way out. That is what survives
     * a battery going flat mid-game; end() is what survives everything else. */
    saveGame(host);
    /* A draw is an ending and sounds like one, but it is not a win. On a board
     * with no speaker this changes nothing, which is why the status line has
     * to carry the same news in words. */
    host.playSound(status_ == Status::Checkmate ? Sound::Victory
                   : gameOver()                 ? Sound::GameOver
                   : status_ == Status::Check   ? Sound::Reveal
                                                : Sound::Tap);
    markDirty();

    /* Hand over to the computer if it is now its move. The search is started
     * here rather than in update() so that it is armed by the act that made it
     * the computer's turn, whatever that act was -- including a restored game,
     * which calls this path's tail in ChessSave. */
    if (mode_ == Mode::Computer && !gameOver() && !humanTurn()) {
        beginThinking();
    }
}

/* Arm the computer's move. Medium's search is begun now and stepped over the
 * frames that follow; Easy has nothing to prepare and only waits out the same
 * pause, so that the two feel like the same opponent thinking harder. */
void ChessGame::beginThinking() {
    thinking_ = true;
    thinkUntilMs_ = millis() + CPU_THINK_MS;
    if (level_ == Ch::Level::Medium) {
        Ch::beginSearch(search_, pos_, static_cast<uint32_t>(millis()) ^ 0x9E3779B9u);
    }
    panelStale_ = true;
    markDirty();
}

void ChessGame::updateComputer(AppContext& host, uint32_t now) {
    if (!thinking_ || mode_ != Mode::Computer || gameOver()) return;

    if (level_ == Ch::Level::Medium) {
        /* A slice of the search, bounded in microseconds rather than in nodes,
         * because what has to be protected is the frame and not the tree. The
         * budget is well inside the 20ms the loop allows, with the repaint
         * that follows still to pay for. */
        const uint32_t t0 = micros();
        const bool done = Ch::stepSearch(
            search_, [&]() { return micros() - t0 > CPU_SLICE_US; });
        if (!done || now < thinkUntilMs_) return;
    } else if (now < thinkUntilMs_) {
        return;
    }

    Ch::Move m = level_ == Ch::Level::Medium ? Ch::bestMove(search_)
                                             : Ch::chooseEasy(pos_, rng_);
    thinking_ = false;
    if (m.from == NO_SQ || m.to == NO_SQ) {
        /* No move: the position is over and refreshStatus() already knows it.
         * Saying so here rather than playing something illegal is the whole of
         * the contract between this screen and the engine. */
        return;
    }
    playMove(host, m.from, m.to, false);
}

void ChessGame::markSquare(uint8_t square) {
    if (square == NO_SQ) return;
    for (uint8_t i = 0; i < dirtyCount_; ++i) {
        if (dirtySq_[i] == square) return;
    }
    if (dirtyCount_ < sizeof(dirtySq_)) dirtySq_[dirtyCount_++] = square;
}

void ChessGame::refreshStatus() {
    const bool inCheck =
        attacked(pos_, kingSquare(pos_, pos_.whiteToMove), !pos_.whiteToMove);
    /* Order matters. Mate ends the game even in a position that is otherwise
     * dead -- you cannot be mated by pieces that cannot mate, so the two never
     * actually collide, but stating the precedence means nobody has to work
     * that out again. Having no legal move is asked first for the same reason:
     * stalemate is a draw arrived at by the rules of movement, not by counting
     * material or moves. */
    if (!hasAnyLegalMove(pos_)) {
        status_ = inCheck ? Status::Checkmate : Status::Stalemate;
    } else if (deadPosition(pos_)) {
        status_ = Status::DrawMaterial;
    } else if (pos_.halfmove >= 100) {
        status_ = Status::DrawFifty;
    } else {
        status_ = inCheck ? Status::Check : Status::Playing;
    }
    statusStale_ = true;
}


void ChessGame::update(AppContext& host, const TouchPoint& touch) {
    if (mode_ == Mode::Lobby) {
        updateLobby(host, touch);
        return;
    }

    pollOpponent(host);
    /* Beside pollOpponent() and for the same reason: both are an opponent
     * making a move without anybody touching the screen, both self-gate on the
     * mode, and both must run before the tap handling below so a move that has
     * just arrived is on the board before a finger lands on it. */
    updateComputer(host, millis());

    /* Republish every frame. Unchanged values do not touch the radio, and a
     * move must stay on the air until it is replaced: the opponent may be
     * anywhere in its scan cycle, or may only just have come back into range.
     *
     * From ply 0, which is not a move but a presence: it is how an accepted
     * invitation is answered. The inviter sits in Waiting until it hears
     * ANYTHING from us in its session, and the acceptor plays Black -- so if
     * this only published once we had moved, neither side could ever start.
     * The receiver's expected-ply test discards ply 0 as a move, so saying it
     * costs nothing and means the handshake needs no second message, which is
     * the message that would have gone missing. */
    if (mode_ == Mode::Remote && status_ != Status::Ended) {
        host.nearbyPublish(session_, ourPly_, ourFrom_, ourTo_, theirPly_);
    }

    /* Are they still there? Tested after the poll and the republish, so a
     * move that did arrive is applied before the silence is measured, and
     * before any tap, so a press through the card is never a move. */
    if (updatePause(host, touch)) return;

    /* The confirm window closes on its own, so a half-pressed End game does
     * not lie in wait to be completed by an unrelated tap minutes later. */
    if (confirmUntilMs_ != 0 && millis() > confirmUntilMs_) {
        confirmUntilMs_ = 0;
        panelStale_ = true;
        markDirty();
    }

    if (!touch.justPressed) return;

    const bool over = gameOver();

    /* The one button. New game once the game is over, End game while it is
     * running -- and End game asks twice, because it is the only control here
     * that destroys something. Tested before the board, so a button drawn
     * over the panel is never also a tap on a square. */
    if (actionRect(host).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (over) {
            /* Always back to the lobby, whichever way the game was being
             * played. Two reasons, and the second one is the important one.
             *
             * A new remote game needs a new session and a fresh invitation, so
             * pretending the old session can be reused would be wrong. And now
             * that an unfinished game is restored on the way in, the lobby is
             * no longer somewhere you arrive by leaving and coming back -- so
             * if a finished LOCAL game started another local game directly,
             * there would be no route from pass-and-play to playing a peer at
             * all, short of ending a game you did not want to end. Persistence
             * quietly took that route away; this is where it comes back. */
            host.playSound(Sound::Select);
            if (mode_ == Mode::Remote) host.nearbyStop();
            mode_ = Mode::Lobby;
            opponent_[0] = 0;
            opponentName_[0] = 0;
            session_ = 0;
            seatCount_ = 0;
            seatsAtMs_ = 0;
            newGame();
            saveGame(host);
        } else if (confirmUntilMs_ != 0) {
            declareEnd(host, true);
            host.playSound(Sound::GameOver);
        } else {
            confirmUntilMs_ = millis() + CONFIRM_MS;
            host.playSound(Sound::Tap);
            panelStale_ = true;
            markDirty();
        }
        return;
    }

    if (over) return;
    /* In a remote game the board is read-only while it is their turn. The
     * legality filter would catch an out-of-turn move anyway -- it generates
     * for the side to move -- but stopping it here means the pieces simply do
     * not respond, which reads as "not your turn" rather than as a bug. */
    /* Not your move: the board is not tappable. ourTurn() answers for the
     * computer as well as the network now -- without that, a player could move
     * the computer's pieces for it while it was thinking. */
    if (!ourTurn()) return;

    const uint8_t hit = squareAt(host, touch.x, touch.y);
    if (hit == NO_SQ) return;

    // A tap on one of the marked squares plays the move.
    for (uint8_t i = 0; i < targetCount_; ++i) {
        if (targets_[i] != hit) continue;
        playMove(host, selected_, hit, true);
        return;
    }

    // Otherwise it is a selection: only ever of a piece belonging to the mover.
    for (uint8_t t = 0; t < targetCount_; ++t) markSquare(targets_[t]);
    markSquare(selected_);
    selected_ = NO_SQ;
    targetCount_ = 0;

    const int8_t piece = pos_.sq[hit];
    if (piece != EMPTY && isWhite(piece) == pos_.whiteToMove) {
        selected_ = hit;
        targetCount_ = legalMoves(pos_, hit, targets_);
        markSquare(hit);
        for (uint8_t t = 0; t < targetCount_; ++t) markSquare(targets_[t]);
        host.playSound(Sound::Tap);
    }
    markDirty();
}

