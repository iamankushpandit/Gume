// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "SeaBattleGame.h"

#include <string.h>

#include "engine/NearbyPlay.h"

/* Sea Battle on two consoles: the lobby that starts it and the poll that
 * plays it. Everything on the air is the nearby service's two-player turn --
 * see src/games/CLAUDE.md. */

void SeaBattleGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    Ui::drawButton(tft, lobbyRowRect(0), "Pass and play", Ui::panel(),
                   Ui::outline(), Ui::text(), false, 2);

    char label[32];
    uint8_t row = 1;
    for (uint8_t i = 0; i < seatCount_ && row <= LOBBY_PEER_ROWS; ++i, ++row) {
        const char* who = seats_[i].name[0] != 0 ? seats_[i].name
                                                 : seats_[i].deviceId;
        if (seats_[i].inviting) {
            snprintf(label, sizeof(label), "%s invites you", who);
        } else {
            snprintf(label, sizeof(label), "Play %s", who);
        }
        Ui::drawButton(tft, lobbyRowRect(row), label,
                       seats_[i].inviting ? Ui::success() : Ui::panel(),
                       Ui::outline(), Ui::text(), false, 2);
    }

    tft.setTextDatum(BC_DATUM);
    /* A low battery outranks the usual note: a console that dies mid-game
     * cannot tell anyone, so this is the moment to say it. */
    const bool low = seatCount_ > 0 && host.batteryLow();
    tft.setTextColor(low ? Ui::warning() : Ui::muted(), Ui::bg());
    const char* note = low             ? "Battery low: a nearby game may not finish."
                       : seatCount_ > 0 ? "Shots travel by Bluetooth. Ships never do."
                                        : "An administrator can switch Beacon and Nearby on.";
    tft.drawString(note, GAME_CANVAS_WIDTH / 2,
                   static_cast<int16_t>(GAME_CANVAS_HEIGHT - 6), 1);
    tft.setTextDatum(TL_DATUM);
}

void SeaBattleGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (now - seatsAtMs_ > 1000) {
        seatsAtMs_ = now;
        NearbySeat fresh[6];
        uint8_t count = 0;
        const uint8_t n = host.nearbySeatCount();
        for (uint8_t i = 0; i < n && count < LOBBY_PEER_ROWS; ++i) {
            NearbySeat seat;
            if (host.nearbySeatAt(i, seat)) {
                /* An invitation to some other game is not one to accept here:
                 * answering it would leave two consoles playing different
                 * games at each other. The row reads "Play A4F2" instead. */
                seat.inviting = seat.inviting && seat.forThisGame;
                fresh[count++] = seat;
            }
        }
        /* Compare the rows and not just how many: a peer that starts inviting
         * changes what its row says without changing the count. */
        bool changed = count != seatCount_;
        for (uint8_t i = 0; !changed && i < count; ++i) {
            changed = fresh[i].inviting != seats_[i].inviting ||
                      strcmp(fresh[i].deviceId, seats_[i].deviceId) != 0;
        }
        for (uint8_t i = 0; i < count; ++i) seats_[i] = fresh[i];
        seatCount_ = count;
        if (changed) markFullDirty();
    }

    if (!touch.justPressed) return;

    if (lobbyRowRect(0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.playSound(Sound::Select);
        startLocal();
        return;
    }
    for (uint8_t i = 0; i < seatCount_; ++i) {
        if (!lobbyRowRect(static_cast<uint8_t>(i + 1))
                 .contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        if (seats_[i].inviting) {
            host.playSound(Sound::Select);
            startRemote(seats_[i], seats_[i].session, seats_[i].weMoveFirst);
            noteOwner(host);
        } else {
            /* Never a number either console still holds a turn for, or an
             * old game's last turn would be read as this one's first. */
            const uint8_t session = host.nearbyFreshSession();
            bool weFireFirst = false;
            if (host.nearbyInvite(seats_[i].deviceId, session, weFireFirst)) {
                host.playSound(Sound::Select);
                startRemote(seats_[i], session, weFireFirst);
                mode_ = Mode::Waiting;
                invitedAtMs_ = millis();
                seatsAtMs_ = invitedAtMs_;
                noteOwner(host);
                markFullDirty();
            } else {
                host.beepError();
            }
        }
        return;
    }
}

/* Asked, and not answered yet. Nothing on this screen is in play: the sea
 * and Ready belong to a game that has not started, so the one control is
 * Cancel.
 *
 * Two things end the wait besides their answer. The invitation is on the air
 * for a few seconds only, so a player who opens the game after that sees a
 * plain "Play" row and asks us back -- and two consoles each waiting on their
 * own invitation would wait for ever. So their invitation is taken up here:
 * at once by the console with the higher tag, so that when both ask in the
 * same moment exactly one gives way, and by either once its own invitation
 * has left the air. Ours is not sent again, because sending tosses the coin
 * for first shot again and a console that accepted the first toss would
 * disagree. And after WAITING_MS with no answer the lobby comes back.
 *
 * Only an invitation heard just now is taken up. What is remembered about a
 * console is the last thing it said, and one that asked a while ago and has
 * since taken up OUR invitation would otherwise be answered in its old
 * session while it plays in ours. */
void SeaBattleGame::updateWaiting(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (now - seatsAtMs_ > 500) {
        seatsAtMs_ = now;
        NearbySeat seat;
        if (host.nearbyInviteForUs(seat) && seat.forThisGame &&
            strcmp(seat.deviceId, opponent_) == 0 &&
            seat.silentMs < INVITE_HEARD_MS &&
            (now - invitedAtMs_ > INVITE_ON_AIR_MS ||
             strcmp(host.nearbySelfId(), opponent_) > 0)) {
            /* Nothing has been fired, so the sea laid out for our own
             * invitation serves. The turn published from the next frame on
             * is the answer they are waiting for. */
            session_ = static_cast<uint8_t>(seat.session & 0x3F);
            remoteFiresFirst_ = seat.weMoveFirst;
            mode_ = Mode::Remote;
            host.playSound(Sound::Select);
            fullPaint_ = true;
            saveGame(host);
            markFullDirty();
            return;
        }
    }

    const bool cancel = touch.justPressed &&
                        actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP);
    if (!cancel && now - invitedAtMs_ <= WAITING_MS) return;
    if (cancel) host.playSound(Sound::Tap);
    host.nearbyStop();
    toLobby();
    markFullDirty();
}

/* Has the other console heard the last thing we said? It has once its own
 * turn acknowledges that ply, and it needs no telling if it has ended the
 * game itself. A console that has gone out of range will not hear it however
 * long it is left up, which is as good as heard. */
bool SeaBattleGame::lastWordLanded(AppContext& host) const {
    NearbyTurn turn;
    if (host.nearbyTurnFrom(lastWordPeer_, lastWordSession_, turn) &&
        (turn.ended || turn.ack == lastWordPly_)) {
        return true;
    }
    return host.nearbyPeerSilentMs(lastWordPeer_) == NearbyPlay::PEER_SILENT_UNKNOWN;
}

/* From the lobby or a game on this console: take a finished nearby game's
 * last word down once it has done its job. A new nearby game clears the flag
 * instead, in startRemote(), because its own turn has taken that place. */
void SeaBattleGame::tickLastWord(AppContext& host) {
    if (!lastWord_ || !lastWordLanded(host)) return;
    lastWord_ = false;
    host.nearbyStop();
}

/* Take the opponent's turn, if there is one we should act on.
 *
 * The same four tests Chess uses -- from this peer, in this session, the ply
 * we expect, and legal here -- with "legal" meaning a square inside the grid
 * that has not already been fired at. A peer's advertisement is a broadcast
 * and is not ours to trust.
 *
 * One thing is particular to this game: a turn carries TWO facts, their shot
 * and the answer to ours. The answer is applied first, because it belongs to
 * the shot we made before theirs.
 */
void SeaBattleGame::pollOpponent(AppContext& host) {
    if (mode_ != Mode::Remote && mode_ != Mode::Waiting) return;
    if (opponent_[0] == 0) return;

    NearbyTurn turn;
    if (!host.nearbyTurnFrom(opponent_, session_, turn)) return;

    if (mode_ == Mode::Waiting) {
        mode_ = Mode::Remote;
        fullPaint_ = true;
        markFullDirty();
    }

    /* Nothing they send changes a game that is over. */
    if (gameOver()) return;

    /* An ending is taken one or two plies ahead, before the test for the
     * ply we expect. It replaces their last shot on the air, so a console
     * that was elsewhere while both went out only ever sees the ending --
     * and would otherwise wait for ever for the shot in between. */
    const uint8_t ahead = static_cast<uint8_t>((turn.ply - theirPly_) & 0x7F);
    if (turn.ended) {
        if (ahead < 1 || ahead > 2) return;
        theirPly_ = turn.ply;
        declareEnd(host, false);
        host.playSound(Sound::GameOver);
        return;
    }
    if (ahead != 1) return;

    /* Not yet ready to be shot at. The turn stays on the air -- it is state,
     * not a message -- so it will still be here when this fleet is placed.
     * Without this the opponent's opening shot would be dropped by whichever
     * console took longer over Shuffle. */
    if (phase_ == Phase::Placing) return;
    if (turn.from >= CELLS) return;

    theirPly_ = turn.ply;

    // Their answer to our shot, which we have been showing as Pending.
    if (pendingCell_ != NO_CELL) {
        const Shot answer = static_cast<Shot>(turn.to);
        Side& us = side_[0];
        if (answer == Shot::Hit || answer == Shot::Sunk) {
            us.shot[pendingCell_] = Mark::Hit;
            ++us.hitsScored;
        } else {
            us.shot[pendingCell_] = Mark::Miss;
        }
        lastShot_ = answer;
        markCell(pendingCell_);
        pendingCell_ = NO_CELL;
        if (us.hitsScored >= FLEET_CELLS) {
            won_ = true;
            host.playSound(Sound::Victory);
            panelStale_ = true;
            markDirty();
            saveGame(host);
            return;
        }
        host.playSound(answer == Shot::Sunk  ? Sound::LevelUp
                       : answer == Shot::Hit ? Sound::Coin
                                             : Sound::Reveal);
    }

    // Their shot at us, and the answer we will owe them on our next turn.
    owedReply_ = resolve(side_[0], turn.from);
    if (side_[0].hitsTaken >= FLEET_CELLS) {
        lost_ = true;
        /* There is no next turn of ours to carry this answer, and without it
         * the winner never learns the last shot landed. So it goes out on a
         * turn of its own. The square repeats our last shot; they read the
         * answer first, see they have won, and never play it. */
        ourReplyCode_ = static_cast<uint8_t>(owedReply_);
        owedReply_ = Shot::None;
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        host.nearbyPublish(session_, ourPly_, ourShotCell_, ourReplyCode_,
                           theirPly_);
        host.playSound(Sound::GameOver);
    }
    panelStale_ = true;
    markDirty();
    saveGame(host);
}


/* ---- the other console going quiet -----------------------------------------
 *
 * NearbyWatch decides what quiet means; this is what Sea Battle does about it.
 * Only a live remote game is watched -- not the lobby, not an invitation still
 * unanswered, and not a game that is already won, lost or ended. A fleet still
 * being placed IS watched: the opponent can go flat while you shuffle. */
bool SeaBattleGame::updatePause(AppContext& host, const TouchPoint& touch) {
    if (mode_ != Mode::Remote || gameOver()) {
        if (watch_.paused()) {
            watch_.reset();
            pausePainted_ = false;
            markFullDirty();
        }
        return false;
    }
    const NearbyWatch::State before = watch_.state();
    if (watch_.tick(host, opponent_, millis())) {
        if (watch_.state() != before) panelStale_ = true;   // the status line names it
        markDirty();
    }
    if (watch_.resumed()) {
        host.playSound(Sound::Pop);
        pausePainted_ = false;
        panelStale_ = true;
        markFullDirty();
        return false;
    }
    if (!watch_.cardShown()) return false;
    switch (watch_.press(boardRect(), touch, false)) {
        case NearbyWatch::Press::Wait:
            watch_.dismiss();
            pausePainted_ = false;
            host.playSound(Sound::Tap);
            markFullDirty();
            break;
        case NearbyWatch::Press::End:
            declareEnd(host, true);   // the same ending End game sends
            host.playSound(Sound::GameOver);
            pausePainted_ = false;
            break;
        default:
            break;
    }
    /* Only the sea is under the card. The buttons beside it are not, and
     * each is safe while paused, so a press there goes on to them. */
    return touch.justPressed && boardRect().contains(touch.x, touch.y, 0);
}

void SeaBattleGame::drawPause(AppContext& host) {
    if (mode_ != Mode::Remote || gameOver() || !watch_.cardShown()) {
        pausePainted_ = false;
        return;
    }
    Ui::Renderer& tft = host.display();
    const Rect area = boardRect();
    if (!pausePainted_) {
        watch_.draw(tft, area, opponentLabel(), nullptr);
        pausePainted_ = true;
        pauseSecondsDrawn_ = watch_.silentSeconds();
    } else if (pauseSecondsDrawn_ != watch_.silentSeconds()) {
        watch_.drawSeconds(tft, area, false);
        pauseSecondsDrawn_ = watch_.silentSeconds();
    }
}
