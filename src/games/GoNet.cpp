// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "GoGame.h"

#include <string.h>

/* Go on two consoles, and the lobby that starts it.
 *
 * Everything on the air is the nearby service's two-player turn -- the same
 * invitation, the same numbered move and the same ending Chess, Sea Battle,
 * Ludo and Backgammon send -- so nothing new is transmitted. A Go turn is one
 * of four words spelled by Go::Net across the two six-bit fields: a stone, a
 * pass, a dead-group toggle in the marking phase, or accepting the marking.
 * Resigning is the service's own ending.
 *
 * THE LOBBY (drawn and answered in GoLobby.cpp) shows every console in the
 * room and what it is doing: asking us, asking somebody else, in a game, or
 * free. An invitation stands until it is answered or taken back, so nothing
 * here times one. The console that asks states the rules and the board in
 * its session number (SETTING_BITS), and the console that answers plays that
 * game: two consoles on different chips used to play different games at
 * each other.
 *
 * A GAME ONCE BEGUN belongs to the console and only End game ends it -- here,
 * or on the other console, or the game finishing by its rules. Leaving the
 * screen, sleeping, a change of player and the other console going quiet do
 * not: the last word stays on the air and the game is where it was.
 *
 * Every move received is played only if it is legal in THIS console's
 * position and it is the other side's turn, which is what stops a hostile or
 * confused advertiser corrupting a board. The marking phase is the one place
 * both sides speak out of turn: a toggle from either side is applied as it
 * arrives, any toggle unagrees both, and the game is scored once both have
 * accepted the same marking.
 *
 * It is a BROADCAST: anyone in range hears the moves. The lobby says so. */

namespace {

/* One console by its tag, as it is on this frame rather than as the lobby
 * last painted it. */
bool seatNow(AppContext& host, const char* deviceId, NearbySeat& out) {
    const uint8_t n = host.nearbySeatCount();
    for (uint8_t i = 0; i < n; ++i) {
        if (host.nearbySeatAt(i, out) && strcmp(out.deviceId, deviceId) == 0) {
            return true;
        }
    }
    return false;
}

}   // namespace

// ---- answering the lobby ----------------------------------------------------------

void GoGame::pressSeat(AppContext& host, uint8_t index) {
    /* A row that only says what that console is doing takes no press. */
    if (!rowTakesPress(rowFor(seats_[index]))) {
        return;
    }
    /* The row is up to a second old, and an invitation can have been taken
     * back in that second: accepted from the row as painted, this console
     * would sit in a game nobody is in. So the press is for what that
     * console says now -- which, whichever of us asks, is still a game with
     * the console the player chose. */
    NearbySeat seat;
    if (!seatNow(host, seats_[index].deviceId, seat)) {
        host.beepError();   // gone since the row was painted
        refreshSeats(host);
        return;
    }
    const Row row = rowFor(seat);
    if (row == Row::Invites) {
        if (acceptInvite(host, seat)) {
            host.playSound(Sound::Select);
        } else {
            host.beepError();
        }
    } else if (rowTakesPress(row)) {
        inviteSeat(host, seat);
    } else {
        host.beepError();
        refreshSeats(host);   // it has just become a row that takes none
    }
}

bool GoGame::acceptInvite(AppContext& host, const NearbySeat& seat) {
    if (rowFor(seat) != Row::Invites) {
        return false;
    }
    /* Their session, and the colour the coin gave us came with the
     * invitation. Our presence in their session is the whole answer. */
    return startRemote(host, seat, seat.session, seat.weMoveFirst, false);
}

void GoGame::inviteSeat(AppContext& host, const NearbySeat& seat) {
    /* The game we offer, said in the session number: our chips. The number
     * itself is the service's, so it is never one an old game's last move is
     * still held under. */
    const bool bigBoard = boardSize_ > 9 && BIG_BOARD_AVAILABLE;
    const uint8_t settings = static_cast<uint8_t>((static_cast<uint8_t>(rules_) & SETTING_RULES) |
                                                  (bigBoard ? SETTING_BIG : 0));
    const uint8_t session = host.nearbyFreshSession(settings, SETTING_BITS);
    bool weAreBlack = false;
    if (!host.nearbyInvite(seat.deviceId, session, weAreBlack)) {
        host.beepError();
        return;
    }
    if (!startRemote(host, seat, session, weAreBlack, true)) {
        host.nearbyStop();   // nobody here is waiting on it
        host.beepError();
        return;
    }
    host.playSound(Sound::Select);
}

void GoGame::takeBackInvite(AppContext& host) {
    host.nearbyStop();
    confirmUntilMs_ = 0;
    mode_ = Mode::Lobby;
    lobbyStale_ = true;
    refreshSeats(host);   // the room as it is now, in the one repaint
    host.playSound(Sound::Select);
    markFullDirty();
}

// ---- a game against another console -------------------------------------------------

bool GoGame::startRemote(AppContext& host, const NearbySeat& seat, uint8_t session,
                         bool weAreBlack, bool waiting) {
    const char* self = host.nearbySelfId();
    if (self[0] == 0) {
        return false;   // the radio went down between the lobby and here
    }
    snprintf(opponent_, sizeof(opponent_), "%s", seat.deviceId);
    snprintf(opponentName_, sizeof(opponentName_), "%s", seat.name);
    session_ = static_cast<uint8_t>(session & 0x3F);
    /* The coin toss decides the colours, and Black moves first. The rules
     * and the board are the ones the invitation states, on both consoles:
     * read back out of the session number here whichever of us asked, so
     * there is one place they can come from. */
    humanColour_ = weAreBlack ? Go::BLACK : Go::WHITE;
    newGame(waiting ? Mode::Waiting : Mode::Remote, (session_ & SETTING_BIG) != 0 ? 19 : 9,
            static_cast<Go::Rules>(session_ & SETTING_RULES));
    ourPly_ = 0;
    theirPly_ = 0;
    ourFrom_ = 0;
    ourTo_ = 0;
    theirAck_ = 0;
    haveAck_ = false;
    lastWordAtMs_ = 0;   // whatever was left on the air is replaced by this game
    if (waiting) {
        char line[26];
        snprintf(line, sizeof(line), "Asking %.10s...", sideName(Go::other(humanColour_)));
        setMessage(line);
    } else {
        publishOurs(host);   // ply 0, 0,0: our presence is the acceptance
        setMessage(humanTurn() ? "Tap a point" : "Their move first");
        saveGame(host);
    }
    return true;
}

void GoGame::publishOurs(AppContext& host) {
    host.nearbyPublish(session_, ourPly_, ourFrom_, ourTo_, theirPly_);
}

void GoGame::sendWord(AppContext& host, Go::Net::Kind kind, uint16_t p) {
    ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
    Go::Net::encode(kind, p, ourFrom_, ourTo_);
    publishOurs(host);
}

bool GoGame::remoteMayMark(AppContext& host) {
    if (mode_ != Mode::Remote || peerCaughtUp()) return true;
    host.beepError();   // our last word is still on its way to them
    setMessage("Wait for them...");
    markDirty();
    return false;
}

void GoGame::pressEndInMarking(AppContext& host, uint32_t now) {
    /* The marking has no Resume across consoles, so that slot is End game,
     * asked twice as in play: a marking the other player never agrees to
     * must not be a screen with no way out. */
    if (now < confirmUntilMs_) {
        confirmUntilMs_ = 0;
        endRemoteByUs(host);
        return;
    }
    confirmUntilMs_ = now + CONFIRM_MS;
    buttonsStale_ = true;
    host.playSound(Sound::Tap);
    markDirty();
}

// ---- the air ------------------------------------------------------------------------

void GoGame::pollRemote(AppContext& host) {
    NearbyTurn t;
    const bool heard = host.nearbyTurnFrom(opponent_, session_, t);
    const char* who = opponentName_[0] != 0 ? opponentName_ : opponent_;

    if (mode_ == Mode::Waiting) {
        /* Their first word in our session is the acceptance. From here it is
         * a game, and the console's: our own first word takes the
         * invitation's place on the air. */
        if (heard && !t.ended) {
            mode_ = Mode::Remote;
            confirmUntilMs_ = 0;
            publishOurs(host);
            saveGame(host);
            setMessage(humanTurn() ? "Tap a point" : "Their move first");
            turnStale_ = buttonsStale_ = true;
            markFullDirty();
            return;
        }
        /* The invitation stands until it is answered or taken back, so there
         * is nothing to time. One thing can still cross it: the console we
         * asked asking us, at the same moment. Each would wait on the other
         * for ever, or each join the other's session, unless exactly one
         * gives way -- the one with the greater tag, on both consoles, so
         * they cannot disagree. It takes its invitation back and answers
         * theirs; the other does nothing and is answered. */
        NearbySeat seat;
        if (strcmp(host.nearbySelfId(), opponent_) > 0 && seatNow(host, opponent_, seat) &&
            seat.inviting && seat.forThisGame) {
            host.nearbyStop();
            if (acceptInvite(host, seat)) {
                host.playSound(Sound::Select);
            } else {
                /* Theirs is a game this console cannot play. Back to the
                 * lobby, whose row for them says why. */
                snprintf(lobbyNote_, sizeof(lobbyNote_), "That game cannot be played here");
                mode_ = Mode::Lobby;
                lobbyStale_ = true;
                refreshSeats(host);
                host.beepError();
                markFullDirty();
            }
        }
        return;
    }

    /* A finished game has said its last word and listens to nothing more:
     * the result stands whatever the other console does next. */
    if (phase_ == Phase::Over) return;
    /* Republish every frame. Unchanged values do not touch the radio, and a
     * turn must stay on the air until it is replaced -- from ply 0, which is
     * how an accepted invitation is answered, and again whenever this screen
     * comes back to a game it was away from. */
    publishOurs(host);
    if (!heard) return;
    /* Before the ply test: they repeat an unchanged turn with a newer ack,
     * so this mostly arrives on a ply already taken. */
    theirAck_ = t.ack;
    haveAck_ = true;

    /* They stopped. Tested BEFORE the whose-turn check -- a player gives up
     * while waiting for us -- and before the exact-ply test, taken up to one
     * ply late as Chess does: a turn is one state on the air, so an ending
     * sent straight after a move this console never heard (it was off the
     * screen) has replaced that move for good. */
    if (t.ended) {
        const uint8_t ahead = static_cast<uint8_t>((t.ply - theirPly_) & 0x7F);
        if (ahead != 1 && ahead != 2) return;
        theirPly_ = t.ply;
        char note[32];
        snprintf(note, sizeof(note), "%s ended the game", who);
        leaveRemote(host, note);
        return;
    }

    const uint8_t expected = static_cast<uint8_t>((theirPly_ + 1) & 0x7F);
    if (t.ply != expected) return;

    Go::Net::Kind kind;
    uint16_t p = Go::PASS;
    if (!Go::Net::decode(t.from, t.to, kind, p)) return;

    switch (kind) {
        case Go::Net::Kind::Play:
        case Go::Net::Kind::Pass:
            if (phase_ != Phase::Play || humanTurn()) return;   // not their move to make
            if (!Go::legal(state_, p)) return;   // not legal here: never played
            theirPly_ = t.ply;
            applyMove(host, p, false);
            return;
        case Go::Net::Kind::ToggleDead:
            if (phase_ != Phase::Marking || p >= Go::points(state_.n) || state_.at[p] == Go::EMPTY) {
                return;
            }
            theirPly_ = t.ply;
            Go::toggleDead(state_, dead_, p);
            Go::ownership(state_, dead_, own_);
            agreed_ = 0;
            host.playSound(Sound::Tap);
            buttonsStale_ = true;
            saveGame(host);
            markFullDirty();
            return;
        case Go::Net::Kind::Accept:
            if (phase_ != Phase::Marking) return;
            theirPly_ = t.ply;
            /* An agreement made before they had seen our latest toggle is an
             * agreement to a marking that no longer exists. It is used up,
             * not kept: they repeat it with a newer ack every frame, and a
             * kept one would pass later, after our toggle had already
             * cleared it on their console. Plies wrap at seven bits. */
            if (toggledThisMarking_ && ((t.ack - lastTogglePly_) & 0x7F) >= 64) {
                saveGame(host);
                return;
            }
            agreed_ = static_cast<uint8_t>(agreed_ | agreedBit(Go::other(humanColour_)));
            if (agreed_ == agreedBoth()) {
                finishGame(host);
            } else {
                saveGame(host);
                char line[26];
                snprintf(line, sizeof(line), "%.10s agrees. You?", who);
                setMessage(line);
                host.playSound(Sound::Reveal);
                markDirty();
            }
            return;
    }
}

// ---- the other console going quiet ---------------------------------------------

bool GoGame::updatePause(AppContext& host, const TouchPoint& touch, uint32_t now) {
    if (mode_ != Mode::Remote || phase_ == Phase::Over) {
        if (watch_.paused()) {
            watch_.reset();
            pausePainted_ = false;
            markFullDirty();
        }
        pauseHold_ = false;
        return false;
    }
    const NearbyWatch::State before = watch_.state();
    if (watch_.tick(host, opponent_, now)) {
        if (watch_.state() != before) {
            /* Away is heard but in no game of ours: it lost the game or
             * began another, which silence is not. */
            const char* what = watch_.state() == NearbyWatch::State::Gone   ? "out of range"
                               : watch_.state() == NearbyWatch::State::Away ? "not in game"
                                                                            : "gone quiet";
            char line[26];
            snprintf(line, sizeof(line), "%.10s %s", sideName(Go::other(humanColour_)), what);
            setMessage(line);
            /* A ghost left from before the pause would make the first tap
             * afterwards play a stone. */
            setGhost(Go::NO_POINT);
        }
        markDirty();
    }
    if (watch_.resumed()) {
        host.playSound(Sound::Pop);
        setMessage(phase_ == Phase::Marking ? "Tap groups that cannot live"
                   : humanTurn()             ? "Tap a point"
                                             : "Their move");
        pausePainted_ = false;
        markFullDirty();
    }
    if (!watch_.cardShown()) {
        /* The rest of a press that began on the card -- the one that pressed
         * Keep waiting included -- reaches nothing until the finger lifts. */
        if (!touch.down && !touch.justPressed) pauseHold_ = false;
        return pauseHold_;
    }
    if (touch.justPressed || touch.down) pauseHold_ = true;
    switch (watch_.press(boardRect(), touch, false)) {
        case NearbyWatch::Press::Wait:
            /* Keep waiting means that and nothing else: the game is where it
             * was, and nothing is taken off the air. */
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
    return touch.justPressed || touch.down;
}

void GoGame::drawPause(Ui::Renderer& tft) {
    if (mode_ != Mode::Remote || phase_ == Phase::Over || !watch_.cardShown()) {
        pausePainted_ = false;
        return;
    }
    const Rect area = boardRect();
    if (!pausePainted_) {
        watch_.draw(tft, area, sideName(Go::other(humanColour_)), nullptr);
        pausePainted_ = true;
        pauseSecondsDrawn_ = watch_.silentSeconds();
    } else if (pauseSecondsDrawn_ != watch_.silentSeconds()) {
        watch_.drawSeconds(tft, area, false);
        pauseSecondsDrawn_ = watch_.silentSeconds();
    }
}
