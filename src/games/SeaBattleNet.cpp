#include "SeaBattleGame.h"

#include <string.h>

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
    for (uint8_t i = 0; i < seatCount_ && row < 5; ++i, ++row) {
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
                                        : "Nobody nearby. An adult can switch Beacon and Nearby on.";
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
        for (uint8_t i = 0; i < n && count < 6; ++i) {
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
        } else {
            const uint8_t session = static_cast<uint8_t>((millis() >> 3) & 0x3F);
            bool weFireFirst = false;
            if (host.nearbyInvite(seats_[i].deviceId, session, weFireFirst)) {
                host.playSound(Sound::Select);
                startRemote(seats_[i], session, weFireFirst);
                mode_ = Mode::Waiting;
                markFullDirty();
            } else {
                host.beepError();
            }
        }
        return;
    }
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
        markFullDirty();
    }

    const uint8_t expected = static_cast<uint8_t>((theirPly_ + 1) & 0x7F);
    if (turn.ply != expected) return;

    if (turn.ended) {
        theirPly_ = turn.ply;
        declareEnd(host, false);
        host.playSound(Sound::GameOver);
        return;
    }

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
    return touch.justPressed;
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
