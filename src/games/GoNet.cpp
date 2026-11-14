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

#include "ui/Stops.h"

/* Go on two consoles, and the lobby that starts it.
 *
 * Everything on the air is the nearby service's two-player turn -- the same
 * invitation, the same numbered move and the same ending Chess, Sea Battle,
 * Ludo and Backgammon send -- so nothing new is transmitted. A Go turn is one
 * of four words spelled by Go::Net across the two six-bit fields: a stone, a
 * pass, a dead-group toggle in the marking phase, or accepting the marking.
 * Resigning is the service's own ending.
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

constexpr uint32_t SEATS_REFRESH_MS = 1000;
/* How long the service keeps an invitation on the air. Past it, the console
 * we asked can no longer be accepting ours. */
constexpr uint32_t INVITE_ON_AIR_MS = 6000;

}   // namespace

void GoGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (now - seatsAtMs_ >= SEATS_REFRESH_MS) {
        seatsAtMs_ = now;
        NearbySeat fresh[6];
        uint8_t count = 0;
        const uint8_t n = host.nearbySeatCount();
        for (uint8_t i = 0; i < n && count < 6; ++i) {
            NearbySeat seat;
            if (host.nearbySeatAt(i, seat)) {
                /* An invitation to another game is not one to accept here. */
                seat.inviting = seat.inviting && seat.forThisGame;
                fresh[count++] = seat;
            }
        }
        bool changed = count != seatCount_;
        for (uint8_t i = 0; !changed && i < count; ++i) {
            changed = fresh[i].inviting != seats_[i].inviting ||
                      strcmp(fresh[i].deviceId, seats_[i].deviceId) != 0;
        }
        for (uint8_t i = 0; i < count; ++i) seats_[i] = fresh[i];
        seatCount_ = count;
        if (changed) {
            lobbyStale_ = true;
            markDirty();
        }
    }

    if (!touch.justPressed) {
        return;
    }
    if (lobbyNote_[0] != 0) {
        lobbyNote_[0] = 0;   // "A4F2 ended the game" has been read
        lobbyStale_ = true;
        markDirty();
    }
    /* The chips. Rules cycles through all five; Level toggles; Board, where
     * it is shown, toggles. Saved with the game so they come back. */
    const uint8_t chips = BIG_BOARD_AVAILABLE ? 3 : 2;
    for (uint8_t i = 0; i < chips; ++i) {
        if (!lobbyChipRect(i, chips).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        if (i == 0) {
            rules_ = static_cast<Go::Rules>((static_cast<uint8_t>(rules_) + 1) % Go::RULES_COUNT);
        } else if (i == 1) {
            level_ = level_ == Go::Level::Easy ? Go::Level::Medium : Go::Level::Easy;
        } else {
            boardSize_ = boardSize_ > 9 ? 9 : 19;
        }
        host.playSound(Sound::Tap);
        saveGame(host);
        lobbyStale_ = true;
        markDirty();
        return;
    }
    if (lobbyRowRect(0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.playSound(Sound::Select);
        newGame(Mode::Local);
        saveGame(host);
        return;
    }
    if (lobbyRowRect(1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.playSound(Sound::Select);
        newGame(Mode::Computer);
        saveGame(host);
        return;
    }
    for (uint8_t i = 0; i < seatCount_ && i < 3; ++i) {
        if (!lobbyRowRect(static_cast<uint8_t>(i + 2)).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        if (seats_[i].inviting) {
            /* Accepting: their session, and the colour the coin gave us came
             * with the invitation. Our presence in their session is the
             * whole answer. */
            host.playSound(Sound::Select);
            startRemote(host, seats_[i], seats_[i].session, seats_[i].weMoveFirst, false);
        } else {
            /* Never a number an old game's last move is still held under. */
            const uint8_t session = host.nearbyFreshSession();
            bool weAreBlack = false;
            if (host.nearbyInvite(seats_[i].deviceId, session, weAreBlack)) {
                host.playSound(Sound::Select);
                startRemote(host, seats_[i], session, weAreBlack, true);
            } else {
                host.beepError();
            }
        }
        return;
    }
}

void GoGame::startRemote(AppContext& host, const NearbySeat& seat, uint8_t session,
                         bool weAreBlack, bool waiting) {
    const char* self = host.nearbySelfId();
    if (self[0] == 0) {
        host.beepError();   // the radio went down between the lobby and here
        return;
    }
    snprintf(opponent_, sizeof(opponent_), "%s", seat.deviceId);
    snprintf(opponentName_, sizeof(opponentName_), "%s", seat.name);
    session_ = static_cast<uint8_t>(session & 0x3F);
    /* The coin toss decides the colours, and Black moves first. The board
     * size is the inviter's chip: an invitation cannot carry it, so both
     * consoles play the size each has chosen -- the lobby says 9x9 is the
     * size to agree on, and a 19x19 console meeting a 9x9 one is refused by
     * the move validation rather than merged. */
    humanColour_ = weAreBlack ? Go::BLACK : Go::WHITE;
    newGame(waiting ? Mode::Waiting : Mode::Remote);
    ourPly_ = 0;
    theirPly_ = 0;
    ourFrom_ = 0;
    ourTo_ = 0;
    theirAck_ = 0;
    haveAck_ = false;
    lastWordOurs_ = false;
    settling_ = false;   // whatever was left on the air is replaced by this game
    if (waiting) {
        invitedAtMs_ = millis();
        setMessage("Asking...");
    } else {
        publishOurs(host);   // ply 0, 0,0: our presence is the acceptance
        setMessage(humanTurn() ? "Tap a point" : "Their move first");
    }
    saveGame(host);
}

void GoGame::publishOurs(AppContext& host) {
    host.nearbyPublish(session_, ourPly_, ourFrom_, ourTo_, theirPly_);
}

void GoGame::sendWord(AppContext& host, Go::Net::Kind kind, uint16_t p) {
    ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
    Go::Net::encode(kind, p, ourFrom_, ourTo_);
    lastWordOurs_ = true;
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

void GoGame::settleAir(AppContext& host) {
    /* A finished game's last move, or our ending, is still on the air. It
     * comes down when the other console has it: it acked the word, it ended
     * the game itself, or it has stopped advertising the game -- which is
     * what a console does once it has heard, and what one out of range looks
     * like too. When the last word was theirs there is nothing of ours to be
     * acked; ours stays up, carrying our ack, until theirs has gone. */
    NearbyTurn t;
    const bool heard = host.nearbyTurnFrom(opponent_, session_, t);
    if (heard && !t.ended && !(lastWordOurs_ && t.ack == settlePly_)) return;
    host.nearbyStop();
    settling_ = false;
}

void GoGame::updateStops() {
    /* The result card is over the board with the two buttons that matter:
     * while it is up, only what is inside it is a stop. */
    const bool card = mode_ != Mode::Lobby && phase_ == Phase::Over;
    if (card) {
        Ui::setOverlayStops(resultRect());
    } else if (resultOverlay_) {
        Ui::clearOverlayStops();
    }
    resultOverlay_ = card;
    if (mode_ == Mode::Lobby) {
        Ui::clearBoardStops();
        return;
    }
    /* The points stay drawn whoever is to move. They are stops only while a
     * press on one does something for the player holding this console: their
     * own turn, or the marking -- and never while an invitation is
     * unanswered or the other console has gone quiet. */
    bool live = false;
    if (mode_ != Mode::Waiting && !(mode_ == Mode::Remote && watch_.paused())) {
        live = phase_ == Phase::Marking || (phase_ == Phase::Play && humanTurn());
    }
    Ui::setBoardStops(boardRect(), live);
}

void GoGame::leaveRemote(AppContext& host, const char* note) {
    if (note != nullptr) {
        snprintf(lobbyNote_, sizeof(lobbyNote_), "%s", note);
    }
    host.nearbyStop();
    settling_ = false;
    ended_ = true;
    watch_.reset();
    pausePainted_ = false;
    mode_ = Mode::Lobby;
    lobbyStale_ = true;
    host.playSound(Sound::GameOver);
    saveGame(host);
    markFullDirty();
}

void GoGame::endRemoteByUs(AppContext& host) {
    /* The service's own ending, which stays on the air after we leave so the
     * other console still hears it; it goes back to its lobby too
     * (pollRemote). Used by End game and by the pause card alike. */
    settlePly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
    host.nearbyEnd(session_, settlePly_, theirPly_);
    settling_ = true;
    lastWordOurs_ = true;
    ended_ = true;
    watch_.reset();
    pausePainted_ = false;
    mode_ = Mode::Lobby;
    lobbyStale_ = true;
    host.playSound(Sound::Select);
    saveGame(host);
    markFullDirty();
}

void GoGame::pollRemote(AppContext& host, uint32_t now) {
    NearbyTurn t;
    const bool heard = host.nearbyTurnFrom(opponent_, session_, t);
    const char* who = opponentName_[0] != 0 ? opponentName_ : opponent_;

    if (mode_ == Mode::Waiting) {
        /* Their first word in our session is the acceptance. */
        if (heard) {
            mode_ = Mode::Remote;
            setMessage(humanTurn() ? "Tap a point" : "Their move first");
            turnStale_ = buttonsStale_ = true;
            markFullDirty();
            return;
        }
        /* Our invitation leaves the air after a few seconds, and by the time
         * they have opened Go their lobby offers "Play ... nearby" instead:
         * tapping that invites US, in a session of theirs. Left alone, both
         * consoles sat on "Asking..." in different sessions for ever. So an
         * invitation from the console we asked is answered from here. When
         * both invitations are live only one side may give way, or each
         * would join the other's session: the larger tag does. */
        NearbySeat seat;
        if (host.nearbyInviteForUs(seat) && seat.forThisGame &&
            strcmp(seat.deviceId, opponent_) == 0) {
            const bool oursExpired = now - invitedAtMs_ >= INVITE_ON_AIR_MS;
            if (oursExpired || strcmp(host.nearbySelfId(), opponent_) > 0) {
                host.nearbyStop();   // ours comes down; theirs is the game
                startRemote(host, seat, seat.session, seat.weMoveFirst, false);
            }
        }
        return;
    }

    /* Republish every frame. Unchanged values do not touch the radio, and a
     * turn must stay on the air until it is replaced -- from ply 0, which is
     * how an accepted invitation is answered. */
    /* A finished game says its last word only until it has been heard. */
    if (!ended_ && (phase_ != Phase::Over || settling_)) publishOurs(host);
    if (!heard) return;
    /* Before the ply test: they repeat an unchanged turn with a newer ack,
     * so this mostly arrives on a ply already taken. */
    theirAck_ = t.ack;
    haveAck_ = true;

    const uint8_t expected = static_cast<uint8_t>((theirPly_ + 1) & 0x7F);
    if (t.ply != expected) return;

    /* They stopped. Tested BEFORE the whose-turn check: a player gives up
     * while waiting for us. */
    if (t.ended) {
        char note[32];
        snprintf(note, sizeof(note), "%s ended the game", who);
        leaveRemote(host, note);
        return;
    }

    Go::Net::Kind kind;
    uint16_t p = Go::PASS;
    if (!Go::Net::decode(t.from, t.to, kind, p)) return;

    switch (kind) {
        case Go::Net::Kind::Play:
        case Go::Net::Kind::Pass:
            if (phase_ != Phase::Play || humanTurn()) return;   // not their move to make
            if (!Go::legal(state_, p)) return;   // not legal here: never played
            theirPly_ = t.ply;
            lastWordOurs_ = false;
            applyMove(host, p, false);
            return;
        case Go::Net::Kind::ToggleDead:
            if (phase_ != Phase::Marking || p >= Go::points(state_.n) || state_.at[p] == Go::EMPTY) {
                return;
            }
            theirPly_ = t.ply;
            lastWordOurs_ = false;
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
            lastWordOurs_ = false;
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
            char line[26];
            snprintf(line, sizeof(line), "%.10s %s", sideName(Go::other(humanColour_)),
                     watch_.state() == NearbyWatch::State::Gone ? "out of range" : "gone quiet");
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
