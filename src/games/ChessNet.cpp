#include "ChessGame.h"

#include <string.h>

#include "ChessInternal.h"

/* Chess on two consoles: the lobby that starts it and the poll that plays it.
 * Everything on the air is the nearby service's two-player turn -- see
 * src/games/CLAUDE.md. */

void ChessGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    Ui::drawButton(tft, lobbyRowRect(host, 0), "Pass and play",
                   Ui::panel(), Ui::outline(), Ui::text(), false, 2);

    char label[32];
    uint8_t row = 1;
    for (uint8_t i = 0; i < seatCount_ && row < 5; ++i, ++row) {
        /* A peer offering us a game reads differently from one merely
         * present, and the wording has to say which -- "Play A4F2" and
         * "A4F2 invites you" are different offers. */
        /* The owner's own label for that console when they have given it
         * one. Naming a peer is the whole point of being able to name one, so
         * it has to change what every screen calls it, not just the Nearby
         * app's list. */
        const char* who = seats_[i].name[0] != 0 ? seats_[i].name
                                                 : seats_[i].deviceId;
        if (seats_[i].inviting) {
            snprintf(label, sizeof(label), "%s invites you", who);
        } else {
            snprintf(label, sizeof(label), "Play %s", who);
        }
        Ui::drawButton(tft, lobbyRowRect(host, row), label,
                       seats_[i].inviting ? Ui::success() : Ui::panel(),
                       Ui::outline(), Ui::text(), false, 2);
    }

    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    /* The empty case names who can fix it. Switching the radio on is an
     * admin job, so a player who reads "turn Beacon on" and cannot find the
     * switch has been sent to a door they have no key for. Playing, once it is
     * on, needs no admin at all. */
    const char* note = seatCount_ > 0
        ? "Moves travel by Bluetooth. Anyone near hears them."
        : "Nobody nearby. An adult can switch Beacon and Nearby on.";
    tft.drawString(note, static_cast<int16_t>(tft.width() / 2),
                   static_cast<int16_t>(tft.height() - 6), 1);
    tft.setTextDatum(TL_DATUM);
}

void ChessGame::updateLobby(AppContext& host, const TouchPoint& touch) {
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
                 * answering a Sea Battle invitation from this lobby leaves two
                 * consoles playing different games at each other. The row
                 * reads "Play A4F2" instead. */
                seat.inviting = seat.inviting && seat.forThisGame;
                fresh[count++] = seat;
            }
        }

        /* Compare the rows, not just how many there are. A peer that starts
         * inviting us does not change the count -- it changes what its row
         * says, from "Play A4F2" to "A4F2 invites you" -- so a count-only test
         * left an invitation sitting on the air with nothing on screen to
         * accept it, until some unrelated console wandered in or out of
         * range. */
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

    if (lobbyRowRect(host, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.playSound(Sound::Select);
        startLocal();
        return;
    }
    for (uint8_t i = 0; i < seatCount_; ++i) {
        if (!lobbyRowRect(host, static_cast<uint8_t>(i + 1))
                 .contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        if (seats_[i].inviting) {
            /* Accepting: they chose the session, and which of us moves first
             * came with the invitation -- the service flipped for it. We
             * answer by publishing ply 0 in their session, which is the whole
             * handshake; there is no second round trip to go missing. */
            host.playSound(Sound::Select);
            startRemote(seats_[i], seats_[i].session, seats_[i].weMoveFirst);
        } else {
            /* Inviting: our session id, derived from the clock rather than a
             * counter so two consoles inviting each other at once are unlikely
             * to pick the same number. Which of us is White is NOT ours to
             * decide -- nearbyInvite() tosses for it and tells us. */
            const uint8_t session = static_cast<uint8_t>((millis() >> 3) & 0x3F);
            bool weMoveFirst = false;
            if (host.nearbyInvite(seats_[i].deviceId, session, weMoveFirst)) {
                host.playSound(Sound::Select);
                startRemote(seats_[i], session, weMoveFirst);
                /* Back to Waiting: startRemote() sets up the game, but nobody
                 * has accepted yet and the board must not take a move until
                 * somebody has. */
                mode_ = Mode::Waiting;
                markFullDirty();
            } else {
                host.beepError();
            }
        }
        return;
    }
}

/* Take the opponent's move, if there is one we should act on.
 *
 * Four tests, and every one of them is load-bearing:
 *
 *   from this peer      -- another console's game must not leak into ours
 *   in this session     -- nor a previous game between the same two consoles
 *   the ply we expect   -- an advertisement repeats, so the same move arrives
 *                          many times; acting once is what makes it a move
 *                          rather than a stutter
 *   legal here          -- and this is the one that matters for safety. A
 *                          move is only applied if it is legal in OUR
 *                          position, so a confused or hostile advertiser
 *                          cannot force the board into a state that is not
 *                          reachable by playing chess.
 */
void ChessGame::pollOpponent(AppContext& host) {
    if (mode_ != Mode::Remote && mode_ != Mode::Waiting) return;
    if (opponent_[0] == 0) return;

    NearbyTurn turn;
    if (!host.nearbyTurnFrom(opponent_, session_, turn)) return;

    if (mode_ == Mode::Waiting) {
        /* They answered. Their first advertisement in our session is the
         * acceptance -- no separate message, because a separate message could
         * be the one that goes missing. Nothing is re-derived here: the seat,
         * the session and the side were all settled when the invitation went
         * out, and this only promotes the mode. */
        mode_ = Mode::Remote;
        markFullDirty();
    }

    const uint8_t expected = static_cast<uint8_t>((theirPly_ + 1) & 0x7F);
    if (turn.ply != expected) return;

    /* They stopped the game. This is tested BEFORE the whose-turn check on
     * purpose: a player gives up when they are stuck, which is usually while
     * they are waiting for us, and a declaration that only arrived on their
     * own turn would be one that mostly never arrived. The reserved encoding
     * belongs to the nearby service, not here -- this only reads the flag. */
    if (turn.ended) {
        theirPly_ = turn.ply;
        declareEnd(host, false);
        host.playSound(Sound::GameOver);
        return;
    }

    if (ourTurn()) return;              // not their move to make

    uint8_t legal[MAX_MOVES];
    const uint8_t n = legalMoves(pos_, turn.from, legal);
    bool ok = false;
    for (uint8_t i = 0; i < n; ++i) {
        if (legal[i] == turn.to) { ok = true; break; }
    }
    if (!ok) return;

    for (int8_t f = 0; f < 8; ++f) markSquare(idx(f, rankOf(turn.from)));
    markSquare(turn.from);
    markSquare(turn.to);
    recordCapture(applyMove(pos_, turn.from, turn.to));
    theirPly_ = turn.ply;
    selected_ = NO_SQ;
    targetCount_ = 0;
    refreshStatus();
    saveGame(host);
    host.playSound(gameOver()                 ? Sound::GameOver
                   : status_ == Status::Check ? Sound::Reveal
                                              : Sound::Tap);
    markDirty();
}

