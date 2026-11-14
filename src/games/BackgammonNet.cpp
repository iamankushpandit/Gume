// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "BackgammonGame.h"

#include "engine/NearbyPlay.h"

/* Backgammon on two consoles, and the lobby that starts it.
 *
 * Everything on the air is the nearby service's two-player turn -- the same
 * invitation, the same numbered move and the same ending Chess and Sea Battle
 * send -- so nothing new is transmitted. A move is one checker: `from` is a
 * point 0..23 or BAR (24), `to` a point or OFF (25), both inside the six bits
 * the turn gives them. None of that can be 63-and-63, the service's reserved
 * "I am stopping".
 *
 * ------------------------------------------------------------------------
 * The dice never go on the air
 * ------------------------------------------------------------------------
 * The seed is Bg::tableSeed(session, both tags), so both consoles know every
 * roll -- the opening, and each turn's -- and each move received is played
 * only if Bg::findMove() says it is legal with those dice in this console's
 * own position. A hostile or confused advertiser can therefore play nothing
 * the rules and the real dice would not have allowed.
 *
 * ------------------------------------------------------------------------
 * Plies
 * ------------------------------------------------------------------------
 * One global count over both players' checker moves, seven bits, wrapping.
 * `ack` is the last ply this console has applied. A turn goes on the air when
 * Done is pressed -- until then Undo is still allowed -- one move per ply,
 * and each replaces the one before only once the other console has
 * acknowledged it, because a move nobody is carrying any more is a move that
 * can never be caught up. The turn ends where the rules say it does: when the
 * receiver computes that nothing is left to play, which is also how a turn
 * with no moves at all passes without anything being sent.
 *
 * ------------------------------------------------------------------------
 * What ends a game, and what does not
 * ------------------------------------------------------------------------
 * End game pressed here, the other console's ending heard, or somebody
 * bearing off the last checker. Nothing else: leaving the screen, opening
 * another game, the console sleeping or restarting and a different player
 * picking it up all leave the game where it was, with its last move still on
 * the air (BackgammonGame::end) and its state in the console's own storage
 * (BackgammonSave.cpp).
 *
 * It is a BROADCAST: anyone in range hears the moves. The lobby says so. */

namespace {

constexpr uint32_t SEATS_REFRESH_MS = 1000;
/* Moves from the other console are shown at the computer's pace. */
constexpr uint32_t REMOTE_MOVE_MS = 450;
constexpr uint8_t PLY_MASK = 0x7F;

inline uint8_t nextPly(uint8_t p) { return static_cast<uint8_t>((p + 1) & PLY_MASK); }
inline bool atOrAfter(uint8_t a, uint8_t b) { return ((a - b) & PLY_MASK) < 64; }

/* Would the lobby's row for this console read differently? Everything a row
 * shows is here, so a console that starts or stops asking, begins a game or
 * is given a name repaints the lobby as surely as one that arrives. */
bool rowDiffers(const NearbySeat& a, const NearbySeat& b) {
    return strcmp(a.deviceId, b.deviceId) != 0 || strcmp(a.name, b.name) != 0 ||
           (a.inviting && a.forThisGame) != (b.inviting && b.forThisGame) ||
           a.asking != b.asking || a.inGame != b.inGame ||
           strcmp(a.partner, b.partner) != 0 || strcmp(a.partnerName, b.partnerName) != 0;
}

}   // namespace

// ---- the lobby ------------------------------------------------------------------

BackgammonGame::RowKind BackgammonGame::rowKind(const NearbySeat& seat) {
    if (seat.inviting && seat.forThisGame) return RowKind::Invites;
    if (seat.asking) return RowKind::Asking;
    if (seat.inGame && seat.partner[0] != 0) return RowKind::Playing;
    if (seat.inGame) return RowKind::PlayBusy;
    return RowKind::Play;
}

bool BackgammonGame::seatNow(AppContext& host, const char* id, NearbySeat& out) {
    const uint8_t n = host.nearbySeatCount();
    for (uint8_t i = 0; i < n; ++i) {
        if (host.nearbySeatAt(i, out) && strcmp(out.deviceId, id) == 0) {
            return true;
        }
    }
    return false;
}

bool BackgammonGame::invitationHere(AppContext& host, const char* from, NearbySeat& out) {
    /* Nobody is asking this console anything: the usual answer, and the
     * cheap one. Otherwise every console is looked at, because more than one
     * can be asking and the service names only the first. */
    if (!host.nearbyInviteForUs(out)) {
        return false;
    }
    const uint8_t n = host.nearbySeatCount();
    for (uint8_t i = 0; i < n; ++i) {
        if (!host.nearbySeatAt(i, out) || !out.inviting || !out.forThisGame) continue;
        if (from == nullptr || strcmp(out.deviceId, from) == 0) return true;
    }
    return false;
}

void BackgammonGame::refreshSeats(AppContext& host) {
    seatsAtMs_ = millis();
    NearbySeat fresh[SEAT_ROWS];
    uint8_t count = 0;
    const uint8_t n = host.nearbySeatCount();
    /* Consoles asking this one first: there are only so many rows, and an
     * invitation must not be the row that did not fit. */
    for (uint8_t pass = 0; pass < 2; ++pass) {
        for (uint8_t i = 0; i < n && count < SEAT_ROWS; ++i) {
            NearbySeat seat;
            if (!host.nearbySeatAt(i, seat)) continue;
            if ((rowKind(seat) == RowKind::Invites) != (pass == 0)) continue;
            fresh[count++] = seat;
        }
    }
    bool changed = count != seatCount_;
    for (uint8_t i = 0; !changed && i < count; ++i) {
        changed = rowDiffers(fresh[i], seats_[i]);
    }
    for (uint8_t i = 0; i < count; ++i) seats_[i] = fresh[i];
    seatCount_ = count;
    if (changed) {
        lobbyStale_ = true;
        markDirty();
    }
}

void BackgammonGame::pressSeat(AppContext& host, const NearbySeat& shown) {
    if (!rowPressable(rowKind(shown))) {
        return;   // busy with somebody else: the row says so and is no button
    }
    /* The row is up to a second old and the press is judged on what that
     * console is saying now: an invitation taken back in that second is not
     * accepted, and a console that has just become busy is not asked. */
    NearbySeat seat;
    if (!seatNow(host, shown.deviceId, seat)) {
        host.beepError();     // it has left the room
        refreshSeats(host);
        return;
    }
    const RowKind kind = rowKind(seat);
    if (kind == RowKind::Invites) {
        /* Accepting: their session, and the side the coin gave us came with
         * the invitation. Our presence in their session is the whole
         * answer. */
        host.playSound(Sound::Select);
        startRemote(host, seat, seat.session, seat.weMoveFirst, false);
        return;
    }
    if (rowPressable(kind)) {
        /* Never a number either console still holds a turn for: an old
         * game's last move, or its ending, would be read as this one's. */
        const uint8_t session = host.nearbyFreshSession();
        bool weAreWhite = false;
        if (host.nearbyInvite(seat.deviceId, session, weAreWhite)) {
            host.playSound(Sound::Select);
            startRemote(host, seat, session, weAreWhite, true);
            return;
        }
    }
    host.beepError();
    refreshSeats(host);   // show what changed under the press
}

void BackgammonGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    if (millis() - seatsAtMs_ >= SEATS_REFRESH_MS) {
        refreshSeats(host);
    }

    if (!touch.justPressed) {
        return;
    }
    if (lobbyNote_[0] != 0) {
        lobbyNote_[0] = 0;   // "A4F2 ended the game" has been read
        lobbyStale_ = true;
        markDirty();
    }
    for (uint8_t row = 0; row < 2; ++row) {
        if (!lobbyRowRect(row).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        const Mode mode = row == 0 ? Mode::Local : Mode::Computer;
        host.playSound(Sound::Select);
        /* The game this lobby came up in front of is carried on, not started
         * over; the other row is a new game, and takes its place. */
        const bool carryOn = savedBehind_ == mode;
        savedBehind_ = Mode::Lobby;
        if (carryOn && loadPlayerGame(host)) {
            panelStale_ = true;
            markFullDirty();
            return;
        }
        newGame(mode);
        saveGame(host);
        return;
    }
    for (uint8_t i = 0; i < seatCount_ && i < SEAT_ROWS; ++i) {
        if (lobbyRowRect(static_cast<uint8_t>(i + 2)).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            pressSeat(host, seats_[i]);
            return;
        }
    }
}

void BackgammonGame::startRemote(AppContext& host, const NearbySeat& seat, uint8_t session,
                                 bool weAreWhite, bool waiting) {
    const char* self = host.nearbySelfId();
    if (self[0] == 0) {
        host.beepError();   // the radio went down between the lobby and here
        return;
    }
    snprintf(opponent_, sizeof(opponent_), "%s", seat.deviceId);
    snprintf(opponentName_, sizeof(opponentName_), "%s", seat.name);
    session_ = static_cast<uint8_t>(session & 0x3F);
    /* The coin toss decides the colours. Who moves first is the opening
     * roll's business, from the shared seed, exactly as on one board. */
    ourWhite_ = weAreWhite;
    newGame(waiting ? Mode::Waiting : Mode::Remote);
    seed_ = Bg::tableSeed(session_, self, opponent_);
    applied_ = 0;
    myPly_ = 0;
    myFrom_ = 0;
    myTo_ = 0;
    lastAcked_ = true;    // nothing of ours to acknowledge yet
    lastWordOnAir_ = false;    // the invitation, or our first word, replaces it
    if (waiting) {
        /* Nothing is saved for an invitation, and nothing moves until it is
         * answered. It stays on the air until then, or until it is taken
         * back: Cancel, or leaving the screen. */
        setMessage("Asking...");
    } else {
        host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
        doOpening(host);   // which saves: the game is the console's from here
    }
}

void BackgammonGame::cancelAsk(AppContext& host, const char* note) {
    if (note != nullptr) {
        snprintf(lobbyNote_, sizeof(lobbyNote_), "%s", note);
    }
    host.nearbyStop();
    host.playSound(Sound::Select);
    enterLobby(host);
}

void BackgammonGame::holdLastWord(AppContext& host) {
    host.nearbyStopLater();
    lastWordOnAir_ = true;
    lastWordAtMs_ = millis();
}

bool BackgammonGame::lastWordStanding() const {
    return lastWordOnAir_ && millis() - lastWordAtMs_ < NearbyPlay::LAST_WORD_MS;
}

void BackgammonGame::leaveRemote(AppContext& host, const char* note) {
    if (note != nullptr) {
        snprintf(lobbyNote_, sizeof(lobbyNote_), "%s", note);
    }
    host.nearbyStop();
    forgetGame(host);
    ended_ = true;
    lastWordOnAir_ = false;
    watch_.reset();
    pausePainted_ = false;
    host.playSound(Sound::GameOver);
    enterLobby(host);
}

void BackgammonGame::pollRemote(AppContext& host, uint32_t now) {
    NearbyTurn t;
    const bool heard = host.nearbyTurnFrom(opponent_, session_, t);
    const char* who = opponentName_[0] != 0 ? opponentName_ : opponent_;

    if (heard && t.ended) {
        char note[32];
        snprintf(note, sizeof(note), "%s ended the game", who);
        leaveRemote(host, note);
        return;
    }

    if (mode_ == Mode::Waiting) {
        /* Their first word in our session is the acceptance. Nothing of ours
         * went on the air while we waited: a turn would have replaced the
         * invitation. */
        if (heard) {
            mode_ = Mode::Remote;
            host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
            doOpening(host);
            markFullDirty();
            return;
        }
        const char* self = host.nearbySelfId();
        if (self[0] == 0) {
            /* Nearby was switched off under the invitation and the service
             * took it down; it would not come back by itself. */
            cancelAsk(host, "Nearby was switched off");
            return;
        }
        /* Both consoles asked each other at the same moment, and each is
         * waiting for the other to answer. One gives way, and which one is
         * decided by something both can see and neither chose: the console
         * with the greater tag takes its invitation back and accepts the
         * other's. The other does nothing and is answered. */
        NearbySeat seat;
        if (strcmp(self, opponent_) > 0 && invitationHere(host, opponent_, seat)) {
            host.nearbyStop();
            host.playSound(Sound::Select);
            startRemote(host, seat, seat.session, seat.weMoveFirst, false);
        }
        return;
    }

    if (!ended_) {
        host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
    }
    if (heard) {
        lastAcked_ = atOrAfter(t.ack, myPly_);
    }

    /* Our turn, one move per ply, each once they have the one before. */
    if (outboxCount_ > 0 && heard && atOrAfter(t.ack, myPly_)) {
        const Bg::Move m = outbox_[0];
        for (uint8_t i = 0; i + 1 < outboxCount_; ++i) outbox_[i] = outbox_[i + 1];
        --outboxCount_;
        myPly_ = nextPly(applied_);
        myFrom_ = m.from;
        myTo_ = m.to;
        applied_ = myPly_;
        lastAcked_ = false;
        host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
        saveGame(host);
    }

    /* Their turn. Nothing of theirs can arrive before our outbox is empty:
     * they cannot have moved without all of ours. */
    if (phase_ != Phase::Remote || outboxCount_ > 0 || now < timerMs_) {
        return;
    }
    const uint8_t us = ourWhite_ ? Bg::WHITE : Bg::BLACK;
    if (legalCount_ == 0) {
        startTurn(host, us);   // they had nothing to play; nothing was sent
        return;
    }
    if (!heard || t.ply != nextPly(applied_)) {
        return;
    }
    Bg::Move m;
    if (!Bg::findMove(pos_, toMove_, dice_, t.from, t.to, m)) {
        return;   // not legal with the dice we computed: never played
    }
    applied_ = t.ply;
    playMove(host, m, false);
    timerMs_ = now + REMOTE_MOVE_MS;
    if (phase_ == Phase::Over) {
        return;
    }
    if (legalCount_ == 0) {
        startTurn(host, us);   // their turn is complete
    }
}

void BackgammonGame::endRemoteByUs(AppContext& host) {
    /* The service's own ending. It stays on the air after we leave -- for
     * the lobby, or the screen altogether -- so the other console still
     * hears it and goes back to its lobby too (pollRemote), and the service
     * takes it down after a minute. Used by End game and by the pause card
     * alike, and by nothing else: this is the one way this console ends a
     * nearby game that is still being played. */
    host.nearbyEnd(session_, nextPly(applied_), applied_);
    holdLastWord(host);
    forgetGame(host);
    ended_ = true;
    outboxCount_ = 0;
    watch_.reset();
    pausePainted_ = false;
    host.playSound(Sound::Select);
    enterLobby(host);
}

bool BackgammonGame::closeRemote(AppContext& host, uint32_t now) {
    const bool won = winner_ == (ourWhite_ ? Bg::WHITE : Bg::BLACK);
    const bool owed = undelivered();
    /* The winning moves are still on their way, and the other console is
     * there to take them, so the first press only says so. It must not be the
     * only thing this button can do, though: a console left on another screen
     * still looks present and takes nothing, and this game stays in the
     * instance however often the screen is left. A second press leaves. Once
     * the other console has gone -- quiet, or off to another game -- nobody
     * is left to wait for. */
    if (owed && !watch_.paused() && now >= confirmUntilMs_) {
        confirmUntilMs_ = now + CONFIRM_MS;
        setMessage("Again to leave");
        host.beepError();
        markDirty();
        return false;
    }
    confirmUntilMs_ = 0;
    if (won && !owed) {
        host.nearbyStop();   // every move arrived
    } else {
        /* Moves that never went out cannot be caught up, so the other
         * console is told the game is over rather than left waiting for
         * them. Otherwise what is on the air stays as our last word: the
         * move that won, not yet acknowledged, or -- having lost -- the
         * acknowledgement the winner is waiting for before it can leave. */
        if (outboxCount_ > 0) {
            host.nearbyEnd(session_, nextPly(applied_), applied_);
        }
        holdLastWord(host);
    }
    outboxCount_ = 0;
    watch_.reset();
    pausePainted_ = false;
    return true;
}

/* ---- the other console going quiet -----------------------------------------
 *
 * NearbyWatch decides what quiet means -- not heard at all, or heard but no
 * longer saying anything in this game -- and this is what Backgammon does
 * about it. Only a live remote game is watched -- not an invitation still
 * unanswered, which has its own "Asking..." and its own Cancel, and not a game
 * that is over, unless this console won it and the winning moves have not all
 * arrived: the other console still has to be there for those. It is watched
 * on OUR turn as much as theirs: a move of ours cannot be acknowledged by a
 * console that is not there, so the outbox would simply stall.
 *
 * Nothing here ends the game. Keep waiting leaves it exactly as it is, for as
 * long as the player likes; only End game on the card ends it. */
bool BackgammonGame::updatePause(AppContext& host, const TouchPoint& touch, uint32_t now) {
    if (mode_ != Mode::Remote || (phase_ == Phase::Over && !undelivered())) {
        if (watch_.paused()) {
            watch_.reset();
            pausePainted_ = false;
            markFullDirty();
        }
        return false;
    }
    const NearbyWatch::State before = watch_.state();
    if (watch_.tick(host, opponent_, now)) {
        if (watch_.state() != before) {
            /* The panel's own line, for once the card has been sent away. */
            const NearbyWatch::State state = watch_.state();
            char line[24];
            snprintf(line, sizeof(line), "%.10s %s", sideName(ourWhite_ ? Bg::BLACK : Bg::WHITE),
                     state == NearbyWatch::State::Gone   ? "out of range"
                     : state == NearbyWatch::State::Away ? "not in game"
                                                         : "gone quiet");
            setMessage(line);
        }
        markDirty();
    }
    if (watch_.resumed()) {
        host.playSound(Sound::Pop);
        /* From the game as it stands now, not the line the pause displaced:
         * after Keep waiting the turn may have been played and handed over. */
        setPhaseMessage();
        pausePainted_ = false;
        markFullDirty();
        return false;
    }
    if (!watch_.cardShown()) return false;
    switch (watch_.press(boardArea(), touch, false)) {
        case NearbyWatch::Press::Wait:
            watch_.dismiss();
            pausePainted_ = false;
            host.playSound(Sound::Tap);
            markFullDirty();
            break;
        case NearbyWatch::Press::End:
            endRemoteByUs(host);
            break;
        default:
            break;
    }
    return touch.justPressed;
}

void BackgammonGame::drawPause(Ui::Renderer& tft) {
    if (mode_ != Mode::Remote || (phase_ == Phase::Over && !undelivered()) ||
        !watch_.cardShown()) {
        pausePainted_ = false;
        return;
    }
    const Rect area = boardArea();
    if (!pausePainted_) {
        watch_.draw(tft, area, sideName(ourWhite_ ? Bg::BLACK : Bg::WHITE), nullptr);
        pausePainted_ = true;
        pauseSecondsDrawn_ = watch_.silentSeconds();
    } else if (pauseSecondsDrawn_ != watch_.silentSeconds()) {
        watch_.drawSeconds(tft, area, false);
        pauseSecondsDrawn_ = watch_.silentSeconds();
    }
}
