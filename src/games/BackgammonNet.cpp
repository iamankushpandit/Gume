#include "BackgammonGame.h"

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
 * It is a BROADCAST: anyone in range hears the moves. The lobby says so. */

namespace {

constexpr uint32_t SEATS_REFRESH_MS = 1000;
/* Moves from the other console are shown at the computer's pace. */
constexpr uint32_t REMOTE_MOVE_MS = 450;
constexpr uint8_t PLY_MASK = 0x7F;

inline uint8_t nextPly(uint8_t p) { return static_cast<uint8_t>((p + 1) & PLY_MASK); }
inline bool atOrAfter(uint8_t a, uint8_t b) { return ((a - b) & PLY_MASK) < 64; }

}   // namespace

void BackgammonGame::updateLobby(AppContext& host, const TouchPoint& touch) {
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
            /* Accepting: their session, and the side the coin gave us came
             * with the invitation. Our presence in their session is the
             * whole answer. */
            host.playSound(Sound::Select);
            startRemote(host, seats_[i], seats_[i].session, seats_[i].weMoveFirst, false);
        } else {
            const uint8_t session = static_cast<uint8_t>((millis() >> 3) & 0x3F);
            bool weAreWhite = false;
            if (host.nearbyInvite(seats_[i].deviceId, session, weAreWhite)) {
                host.playSound(Sound::Select);
                startRemote(host, seats_[i], session, weAreWhite, true);
            } else {
                host.beepError();
            }
        }
        return;
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
    if (waiting) {
        setMessage("Asking...");
    } else {
        host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
        doOpening(host);
    }
    saveGame(host);
}

void BackgammonGame::leaveRemote(AppContext& host, const char* note) {
    if (note != nullptr) {
        snprintf(lobbyNote_, sizeof(lobbyNote_), "%s", note);
    }
    host.nearbyStop();
    ended_ = true;
    mode_ = Mode::Lobby;
    lobbyStale_ = true;
    host.playSound(Sound::GameOver);
    saveGame(host);
    markFullDirty();
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
        }
        return;
    }

    if (!ended_) {
        host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
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
