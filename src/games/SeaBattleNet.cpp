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

namespace {

/* Does this console's row read differently now? Everything a row is drawn
 * from, and not only who is in the room: a console that starts asking, or
 * whose game finds its other half, changes its row without coming or going. */
bool rowDiffers(const NearbySeat& a, const NearbySeat& b) {
    return a.inviting != b.inviting || a.asking != b.asking || a.inGame != b.inGame ||
           strcmp(a.deviceId, b.deviceId) != 0 || strcmp(a.name, b.name) != 0 ||
           strcmp(a.partner, b.partner) != 0 ||
           strcmp(a.partnerName, b.partnerName) != 0;
}

}   // namespace

SeaBattleGame::Row SeaBattleGame::rowFor(const NearbySeat& seat) {
    if (seat.inviting && seat.forThisGame) return Row::Invites;
    if (seat.asking) return Row::Asking;
    if (seat.inGame && seat.partner[0] != 0) return Row::Playing;
    if (seat.inGame) return Row::Busy;
    return Row::Free;
}

/* Asked of the room each time rather than read off the lobby's rows, which
 * are up to a second old: an invitation is taken up in the session it names
 * now, or not at all. */
bool SeaBattleGame::invitedBy(AppContext& host, const char* id, NearbySeat& out) {
    const uint8_t n = host.nearbySeatCount();
    for (uint8_t i = 0; i < n; ++i) {
        NearbySeat seat;
        if (!host.nearbySeatAt(i, seat)) continue;
        if (!seat.inviting || !seat.forThisGame) continue;
        if (id != nullptr && strcmp(seat.deviceId, id) != 0) continue;
        out = seat;
        return true;
    }
    return false;
}

void SeaBattleGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    Ui::drawButton(tft, lobbyRowRect(0),
                   keptLocal_ ? "Continue your game" : "Pass and play", Ui::panel(),
                   Ui::outline(), Ui::text(), false, 2);

    /* One row a console, saying what it is doing: a player can see who is
     * there, who is already in a game and with whom, and why a console they
     * asked is not answering. */
    char label[48];
    uint8_t row = 1;
    for (uint8_t i = 0; i < seatCount_ && row <= LOBBY_PEER_ROWS; ++i, ++row) {
        const NearbySeat& seat = seats_[i];
        const Rect r = lobbyRowRect(row);
        const char* who = seat.name[0] != 0 ? seat.name : seat.deviceId;
        const char* partner = seat.partnerName[0] != 0 ? seat.partnerName
                                                       : seat.partner;
        const Row kind = rowFor(seat);
        label[0] = 0;
        switch (kind) {
            case Row::Invites:
                snprintf(label, sizeof(label), "%s invites you", who);
                break;
            case Row::Asking:
                snprintf(label, sizeof(label), "%s is asking %s", who, partner);
                break;
            case Row::Playing:
                snprintf(label, sizeof(label), "%s is playing %s", who, partner);
                break;
            case Row::Busy:
                /* In a game, and its other console is not in this room, so
                 * whose game is not known. Asking is still allowed: the
                 * banner on their console tells them somebody is. */
                snprintf(label, sizeof(label), "Play %s (in a game)", who);
                break;
            case Row::Free:
                snprintf(label, sizeof(label), "Play %s", who);
                break;
        }
        if (!rowTakesPress(kind)) {
            /* News, not a button, so it is not drawn as one: a flat fill
             * with no shadow and no outline. An outline round the same
             * rectangle is also what the d-pad's selector stops on, and
             * there is nothing here to press. */
            tft.fillRoundRect(r.x, r.y, r.w, r.h, Ui::cornerRadius(), Ui::surface());
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.setTextDatum(MC_DATUM);
            tft.drawString(Ui::fitted(tft, label, static_cast<int16_t>(r.w - 12), 2),
                           r.x + r.w / 2, r.y + r.h / 2, 2);
            tft.setTextDatum(TL_DATUM);
            continue;
        }
        Ui::drawButton(tft, r, label,
                       kind == Row::Invites ? Ui::success() : Ui::panel(),
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
        bool changed = count != seatCount_;
        for (uint8_t i = 0; !changed && i < count; ++i) {
            changed = rowDiffers(fresh[i], seats_[i]);
        }
        for (uint8_t i = 0; i < count; ++i) seats_[i] = fresh[i];
        seatCount_ = count;
        if (changed) markFullDirty();
    }

    if (!touch.justPressed) return;

    if (lobbyRowRect(0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.playSound(Sound::Select);
        if (keptLocal_ && restoreOwn(host)) {
            /* The game this lobby came up over, as it was left. */
            keptLocal_ = false;
            fullPaint_ = true;
            panelStale_ = true;
            markFullDirty();
        } else {
            startLocal();
        }
        return;
    }
    for (uint8_t i = 0; i < seatCount_; ++i) {
        if (!lobbyRowRect(static_cast<uint8_t>(i + 1))
                 .contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        const Row kind = rowFor(seats_[i]);
        /* A row about somebody else's game says so and does nothing. */
        if (!rowTakesPress(kind)) return;
        if (kind == Row::Invites) {
            NearbySeat seat;
            if (invitedBy(host, seats_[i].deviceId, seat)) {
                acceptInvite(host, seat);
            } else {
                /* Taken back in the second since the row was drawn. */
                host.beepError();
                seatsAtMs_ = 0;
            }
            return;
        }
        /* Never a number either console still holds a turn for, or an old
         * game's last turn would be read as this one's first. */
        const uint8_t session = host.nearbyFreshSession();
        bool weFireFirst = false;
        if (host.nearbyInvite(seats_[i].deviceId, session, weFireFirst)) {
            host.playSound(Sound::Select);
            startRemote(seats_[i], session, weFireFirst);
            mode_ = Mode::Waiting;
            seatsAtMs_ = millis();
            markFullDirty();
        } else {
            host.beepError();
        }
        return;
    }
}

/* An invitation taken up, from a lobby row or from the wait below. The game
 * is the console's from this moment and is written down as such at once, so
 * a console that restarts before the first shot is still in it. The turn
 * published from the next frame on is the answer they are waiting for. */
void SeaBattleGame::acceptInvite(AppContext& host, const NearbySeat& seat) {
    host.playSound(Sound::Select);
    startRemote(seat, seat.session, seat.weMoveFirst);
    saveGame(host);
}

/* Asked, and not answered yet. Nothing on this screen is in play: the sea
 * and Ready belong to a game that has not started, so the one control is
 * Cancel.
 *
 * The invitation stands until it is answered or taken back here. Nothing
 * re-sends it and nothing gives up on it: sending tosses the coin for first
 * shot again, and a console that accepted the first toss would disagree.
 *
 * One thing besides their answer ends the wait. Two consoles that ask each
 * other in the same moment would each sit on their own invitation for ever,
 * so exactly one gives way: the one with the greater tag takes its own
 * invitation back and accepts theirs, and the other does nothing and is
 * answered. The tags decide it, not a clock, so the two cannot both give way
 * or both hold out. */
void SeaBattleGame::updateWaiting(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (now - seatsAtMs_ > 500) {
        seatsAtMs_ = now;
        NearbySeat seat;
        if (strcmp(host.nearbySelfId(), opponent_) > 0 &&
            invitedBy(host, opponent_, seat)) {
            host.nearbyStop();
            acceptInvite(host, seat);
            return;
        }
    }

    if (!touch.justPressed ||
        !actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        return;
    }
    host.playSound(Sound::Tap);
    host.nearbyStop();
    toLobby();
    keptLocal_ = ownGameKept(host);
    markFullDirty();
}

/* A nearby game's turn, each frame. Unchanged values do not touch the radio,
 * and a turn must stay on the air until it is replaced -- from ply 0, which
 * is how an accepted invitation is answered.
 *
 * A game that is over says one thing more and then stops: our ending if we
 * ended it, otherwise our last turn, whose ack is how they know theirs
 * arrived. It is said once and given a minute to be heard in. Said every
 * frame for as long as the result was on screen, it kept a console that went
 * to sleep there looking busy, and sharing no score, until somebody woke it. */
void SeaBattleGame::sayTurn(AppContext& host) {
    if (gameOver() && overSaid_) return;
    if (ended_ && endedByUs_) {
        host.nearbyEnd(session_, ourPly_, theirPly_);
    } else {
        host.nearbyPublish(session_, ourPly_, ourShotCell_, ourReplyCode_,
                           theirPly_);
    }
    if (!gameOver()) return;
    host.nearbyStopLater();
    overSaid_ = true;
    lastWordAtMs_ = millis();
}

/* New game, pressed on a nearby game that is over: the game is finished
 * with, so the console's record of it goes. Its last word stays on its timer
 * only if the other console still needs it -- the loser's answer to the last
 * shot and our own End game are their only way of knowing. The winner, and a
 * console that was told the game ended, have nothing left that must be
 * heard. */
void SeaBattleGame::finishNearby(AppContext& host) {
    memcpy(lastWordPeer_, opponent_, sizeof(lastWordPeer_));
    lastWordSession_ = session_;
    lastWordPly_ = ourPly_;
    lastWord_ = overSaid_ && (lost_ || (ended_ && endedByUs_)) &&
                !lastWordLanded(host);
    if (!lastWord_) host.nearbyStop();
    forgetNearby(host);
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
 * last word down as soon as it has done its job, rather than leave it its
 * whole minute. Once the minute is up the service has taken it down itself,
 * and there is nothing left here to protect. A new nearby game clears the
 * flag instead, in startRemote(), because its own invitation or turn has
 * taken that place. */
void SeaBattleGame::tickLastWord(AppContext& host) {
    if (!lastWord_) return;
    if (!lastWordLanded(host) &&
        millis() - lastWordAtMs_ <= NearbyPlay::LAST_WORD_MS) {
        return;
    }
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
        /* Answered. There is a game from here on, and it is the console's:
         * written down now, before a shot has been fired in it. */
        mode_ = Mode::Remote;
        fullPaint_ = true;
        saveGame(host);
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
 * being placed IS watched: the opponent can go flat while you shuffle.
 *
 * Waiting has no end of its own. Keep waiting keeps the game for as long as
 * it takes, here and in the console's storage; only End game, on the card or
 * beside the sea, ends it. */
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
