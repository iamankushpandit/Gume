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
            const uint8_t session = static_cast<uint8_t>((millis() >> 3) & 0x3F);
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
    if (waiting) {
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

void GoGame::leaveRemote(AppContext& host, const char* note) {
    if (note != nullptr) {
        snprintf(lobbyNote_, sizeof(lobbyNote_), "%s", note);
    }
    host.nearbyStop();
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
    host.nearbyEnd(session_, static_cast<uint8_t>((ourPly_ + 1) & 0x7F), theirPly_);
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
    (void)now;
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
        }
        return;
    }

    /* Republish every frame. Unchanged values do not touch the radio, and a
     * turn must stay on the air until it is replaced -- from ply 0, which is
     * how an accepted invitation is answered. */
    if (!ended_) publishOurs(host);
    if (!heard) return;

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
            agreed_ = static_cast<uint8_t>(agreed_ | agreedBit(Go::other(humanColour_)));
            if (agreed_ == agreedBoth()) {
                finishGame(host);
            } else {
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
        return false;
    }
    const NearbyWatch::State before = watch_.state();
    if (watch_.tick(host, opponent_, now)) {
        if (watch_.state() != before) {
            char line[26];
            snprintf(line, sizeof(line), "%.10s %s", sideName(Go::other(humanColour_)),
                     watch_.state() == NearbyWatch::State::Gone ? "out of range" : "gone quiet");
            setMessage(line);
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
            endRemoteByUs(host);
            break;
        default:
            break;
    }
    return touch.justPressed;
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
