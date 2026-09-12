#include "GoGame.h"

#include <string.h>

#include "engine/AppRegistry.h"
#include "engine/Entropy.h"

/* Go: the game's life, a tap on the board, the computer's pace, the marking
 * phase and the score. Five files against one header --
 *   GoGame.cpp   this
 *   GoDraw.cpp   geometry and every pixel
 *   GoNet.cpp    the lobby and a game against a nearby console
 *   GoSave.cpp   remembering a game in NVS
 * over GoRules (the rules) and GoAi (the computer), which are pure. */

namespace {

constexpr AppScoreInfo GO_SCORE = {
    "go", "Go", "goWins", "wins", false
};

constexpr AppMetadata GO_METADATA = {
    "go",
    "Go",
    nullptr,
    "vs friend, computer or nearby",
    "Go",
    "Surround to capture. 9x9, five rule sets.",
    &GO_SCORE,
    LauncherIcon::Go,
    37,
    true,
};

/* The computer waits before it plays, so a child can see whose turn it
 * became; the medium player thinks a few playouts per frame and plays when
 * both the playouts and the wait are done. */
constexpr uint32_t CPU_THINK_MS = 650;
/* Playouts per frame for the medium player and the dead-stone estimate: a
 * budget in microseconds, checked between playouts, well inside the 20ms
 * frame with the repaint that follows. */
constexpr uint32_t CPU_SLICE_US = 4000;
/** Playouts behind the computer's opinion of the dead stones. */
constexpr uint16_t DEAD_PLAYOUTS_SMALL = 40;
constexpr uint16_t DEAD_PLAYOUTS_BIG = 12;

}   // namespace

const AppMetadata& goAppMetadata() {
    return GO_METADATA;
}

const char* GoGame::title() const {
    return goAppMetadata().title;
}

void GoGame::begin(AppContext& host) {
    confirmUntilMs_ = 0;
    confirmShown_ = false;
    ghost_ = Go::NO_POINT;
    haveUndo_ = false;
    watch_.reset();
    pausePainted_ = false;
    rng_.x = Entropy::below(0xFFFFFFFFU) | 1U;
    if (!restoreGame(host)) {
        mode_ = Mode::Lobby;
    }
    lobbyStale_ = true;
    turnStale_ = capturesStale_ = infoStale_ = buttonsStale_ = resultStale_ = true;
    seatCount_ = 0;
    seatsAtMs_ = 0;
}

void GoGame::end(AppContext& host) {
    if (mode_ == Mode::Local || mode_ == Mode::Computer || mode_ == Mode::Remote) {
        saveGame(host);
    }
    host.nearbyStop();
}

// ---- the game --------------------------------------------------------------------

void GoGame::newGame(Mode mode) {
    mode_ = mode;
    const uint8_t n = (boardSize_ > 9 && BIG_BOARD_AVAILABLE) ? 19 : 9;
    Go::reset(state_, n, rules_);
    phase_ = Phase::Play;
    ghost_ = Go::NO_POINT;
    haveUndo_ = false;
    if (mode != Mode::Remote && mode != Mode::Waiting) humanColour_ = Go::BLACK;
    timerMs_ = 0;
    confirmUntilMs_ = 0;
    agreed_ = 0;
    deadPlayoutsLeft_ = 0;
    memset(dead_, 0, sizeof(dead_));
    memset(own_, 0, sizeof(own_));
    ended_ = false;
    watch_.reset();
    pausePainted_ = false;
    setMessage(mode == Mode::Waiting ? "Asking..." : "Tap a point");
    turnStale_ = capturesStale_ = infoStale_ = buttonsStale_ = resultStale_ = true;
    anyDirty_ = false;
    memset(dirty_, 0, sizeof(dirty_));
    markFullDirty();
}

bool GoGame::humanTurn() const {
    switch (mode_) {
        case Mode::Local: return true;
        case Mode::Computer:
        case Mode::Remote: return state_.toMove == humanColour_;
        default: return false;
    }
}

const char* GoGame::sideName(uint8_t colour) const {
    if (mode_ == Mode::Computer) {
        return colour == humanColour_ ? "You" : "CPU";
    }
    if (mode_ == Mode::Remote || mode_ == Mode::Waiting) {
        if (colour == humanColour_) return "You";
        return opponentName_[0] != 0 ? opponentName_ : opponent_;
    }
    return colour == Go::BLACK ? "Black" : "White";
}

void GoGame::setMessage(const char* text) {
    snprintf(message_, sizeof(message_), "%s", text);
    infoStale_ = true;
}

void GoGame::markPoint(uint16_t p) {
    if (p >= Go::points(state_.n)) return;
    dirty_[p >> 3] = static_cast<uint8_t>(dirty_[p >> 3] | (1U << (p & 7)));
    anyDirty_ = true;
}

void GoGame::markChanged(const Go::State& before) {
    const uint16_t total = Go::points(state_.n);
    for (uint16_t p = 0; p < total; ++p) {
        if (before.at[p] != state_.at[p]) markPoint(p);
    }
    if (before.last != Go::NO_POINT) markPoint(before.last);
    if (state_.last != Go::NO_POINT) markPoint(state_.last);
}

void GoGame::setGhost(uint16_t p) {
    if (p == ghost_) return;
    if (ghost_ != Go::NO_POINT) markPoint(ghost_);
    ghost_ = p;
    if (ghost_ != Go::NO_POINT) markPoint(ghost_);
    infoStale_ = true;
    if (big()) buttonsStale_ = true;   // Place lights when a ghost is legal
    markDirty();
}

void GoGame::applyMove(AppContext& host, uint16_t p, bool byHuman) {
    const Go::State before = state_;
    if (!Go::play(state_, p)) {
        host.beepError();
        return;
    }
    if (byHuman && mode_ != Mode::Remote) {
        undo_ = before;   // and against the computer, its reply goes with it
        haveUndo_ = true;
    }
    setGhost(Go::NO_POINT);
    markChanged(before);
    const uint16_t taken = static_cast<uint16_t>(
        (state_.captured[Go::BLACK] + state_.captured[Go::WHITE]) -
        (before.captured[Go::BLACK] + before.captured[Go::WHITE]));
    if (p == Go::PASS) {
        host.playSound(Sound::Whoosh);
        setMessage(state_.passes == 1 ? "Passed. One more ends it" : "Passed");
    } else if (taken > 0) {
        host.playSound(Sound::Coin);
        char line[26];
        snprintf(line, sizeof(line), "%s took %u", sideName(before.toMove),
                 static_cast<unsigned>(taken));
        setMessage(line);
        capturesStale_ = true;
    } else {
        host.playSound(byHuman ? Sound::Tap : Sound::Pop);
        setMessage(humanTurn() || mode_ == Mode::Local ? "Tap a point" : "");
    }
    if (mode_ == Mode::Remote && byHuman) {
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        Go::Net::encode(p == Go::PASS ? Go::Net::Kind::Pass : Go::Net::Kind::Play, p, ourFrom_,
                        ourTo_);
        publishOurs(host);
    }
    turnStale_ = true;
    buttonsStale_ = true;
    if (big()) turnStale_ = true;
    if (state_.over) {
        if (state_.rules == Go::Rules::Territory && state_.winner == Go::EMPTY) {
            enterMarking(host);
        } else {
            finishGame(host);
        }
        return;
    }
    if (mode_ == Mode::Computer && !humanTurn()) {
        phase_ = Phase::Thinking;
        timerMs_ = millis() + CPU_THINK_MS;
        if (level_ == Go::Level::Medium) {
            Go::beginSearch(search_, state_, rng_.next());
        }
        setMessage("Thinking...");
    } else if (mode_ == Mode::Remote && !humanTurn()) {
        char line[26];
        snprintf(line, sizeof(line), "%.10s thinks", sideName(state_.toMove));
        setMessage(line);
    }
    saveGame(host);
    markDirty();
}

void GoGame::tapBoard(AppContext& host, const TouchPoint& touch) {
    const uint16_t p = pointAt(touch.x, touch.y);
    if (p == Go::NO_POINT) return;
    if (touch.justPressed && p == ghost_) {
        /* The second tap, on the same point: play it, if it can be. */
        if (Go::legal(state_, p)) {
            applyMove(host, p, true);
        } else {
            host.beepError();
        }
        return;
    }
    if (state_.at[p] != Go::EMPTY) {
        if (touch.justPressed) setGhost(Go::NO_POINT);
        return;
    }
    setGhost(p);
}

void GoGame::pressPlace(AppContext& host) {
    if (ghost_ == Go::NO_POINT || !Go::legal(state_, ghost_) || !humanTurn() ||
        phase_ != Phase::Play) {
        host.beepError();
        return;
    }
    applyMove(host, ghost_, true);
}

void GoGame::pressPass(AppContext& host) {
    if (phase_ != Phase::Play || !humanTurn() || !Go::legal(state_, Go::PASS)) {
        host.beepError();
        return;
    }
    applyMove(host, Go::PASS, true);
}

void GoGame::pressUndo(AppContext& host) {
    if (!haveUndo_ || phase_ != Phase::Play || mode_ == Mode::Remote || mode_ == Mode::Waiting ||
        !humanTurn()) {
        host.beepError();
        return;
    }
    state_ = undo_;
    haveUndo_ = false;
    ghost_ = Go::NO_POINT;
    host.playSound(Sound::Whoosh);
    setMessage("Taken back");
    turnStale_ = capturesStale_ = buttonsStale_ = true;
    saveGame(host);
    markFullDirty();   // many points may have changed: a capture undone
}

void GoGame::pressAction(AppContext& host, uint32_t now) {
    if (phase_ == Phase::Marking) {
        /* Agree. One console with two people needs both to say so; against
         * the computer the person's word is final, because a computer that
         * argued about dead stones would be a worse thing to hand a child
         * than one that is occasionally wrong. Across consoles both must
         * agree, and ours goes on the air (GoNet). */
        if (mode_ == Mode::Computer) {
            finishGame(host);
            return;
        }
        /* Whose agreement this press is. Locally the console is passed and
         * nothing can tell the two people apart, so the first press counts
         * for the player to move and the second for the other; remotely it
         * is always ours. One encoding either way -- see agreedBit(). */
        const uint8_t who =
            mode_ == Mode::Local
                ? ((agreed_ & agreedBit(state_.toMove)) != 0 ? Go::other(state_.toMove)
                                                            : state_.toMove)
                : humanColour_;
        agreed_ = static_cast<uint8_t>(agreed_ | agreedBit(who));
        if (mode_ == Mode::Remote) {
            ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
            Go::Net::encode(Go::Net::Kind::Accept, Go::PASS, ourFrom_, ourTo_);
            publishOurs(host);
        }
        host.playSound(Sound::Select);
        if (agreed_ == agreedBoth()) {
            finishGame(host);
        } else {
            setMessage(mode_ == Mode::Local ? "Pass to the other player"
                                            : "Waiting for them to agree");
            buttonsStale_ = true;
            markDirty();
        }
        return;
    }
    if (phase_ == Phase::Over) {
        mode_ = Mode::Lobby;
        lobbyStale_ = true;
        host.playSound(Sound::Select);
        saveGame(host);
        markFullDirty();
        return;
    }
    if (now < confirmUntilMs_) {
        confirmUntilMs_ = 0;
        if (mode_ == Mode::Remote || mode_ == Mode::Waiting) {
            endRemoteByUs(host);
            return;
        }
        mode_ = Mode::Lobby;
        lobbyStale_ = true;
        host.playSound(Sound::Select);
        saveGame(host);
        markFullDirty();
        return;
    }
    confirmUntilMs_ = now + CONFIRM_MS;
    buttonsStale_ = true;
    host.playSound(Sound::Tap);
    markDirty();
}

void GoGame::updateComputer(AppContext& host, uint32_t now) {
    if (level_ == Go::Level::Medium) {
        const uint32_t t0 = micros();
        const bool done = Go::stepSearch(search_, [&]() { return micros() - t0 > CPU_SLICE_US; });
        if (!done || now < timerMs_) return;
        uint16_t p = Go::bestMove(search_);
        if (p == Go::PASS && !Go::legal(state_, Go::PASS)) p = Go::chooseEasy(state_, rng_);
        phase_ = Phase::Play;
        if (p == Go::NO_POINT) {
            /* Capture rules and not one point to play: the person moves. */
            state_.toMove = Go::other(state_.toMove);
            turnStale_ = true;
            markDirty();
            return;
        }
        applyMove(host, p, false);
        return;
    }
    if (now < timerMs_) return;
    const uint16_t p = Go::chooseEasy(state_, rng_);
    phase_ = Phase::Play;
    if (p == Go::NO_POINT) {
        state_.toMove = Go::other(state_.toMove);
        turnStale_ = true;
        markDirty();
        return;
    }
    applyMove(host, p, false);
}

// ---- the marking phase -------------------------------------------------------------

void GoGame::enterMarking(AppContext& host) {
    phase_ = Phase::Marking;
    memset(dead_, 0, sizeof(dead_));
    memset(alive_, 0, sizeof(alive_));
    agreed_ = 0;
    ghost_ = Go::NO_POINT;
    haveUndo_ = false;
    /* The computer's opinion arrives a few playouts per frame, like a move;
     * until then nothing is marked and the person can start. */
    deadPlayoutsLeft_ = mode_ == Mode::Computer ? (big() ? DEAD_PLAYOUTS_BIG : DEAD_PLAYOUTS_SMALL) : 0;
    Go::ownership(state_, dead_, own_);
    setMessage("Tap groups that cannot live");
    host.playSound(Sound::Reveal);
    turnStale_ = capturesStale_ = buttonsStale_ = true;
    saveGame(host);
    markFullDirty();
}

void GoGame::resumeFromMarking(AppContext& host) {
    phase_ = Phase::Play;
    state_.over = false;
    state_.passes = 0;
    state_.winner = Go::EMPTY;
    deadPlayoutsLeft_ = 0;
    agreed_ = 0;
    setMessage("Play on");
    host.playSound(Sound::Select);
    turnStale_ = capturesStale_ = buttonsStale_ = true;
    saveGame(host);
    markFullDirty();
}

void GoGame::updateMarking(AppContext& host, const TouchPoint& touch, uint32_t now) {
    (void)now;
    if (deadPlayoutsLeft_ > 0) {
        /* Two playouts a frame, judged against the person's own stones as
         * well as the computer's: a group is dead when its points mostly end
         * up the other colour. Copied from estimateDead(), a slice at a time. */
        const uint16_t total = Go::points(state_.n);
        const uint16_t all = big() ? DEAD_PLAYOUTS_BIG : DEAD_PLAYOUTS_SMALL;
        for (uint8_t k = 0; k < 2 && deadPlayoutsLeft_ > 0; ++k) {
            uint8_t ownNow[Go::MAX_POINTS];
            Go::playoutOwnership(state_, rng_, ownNow);
            for (uint16_t i = 0; i < total; ++i) {
                if (state_.at[i] != Go::EMPTY && ownNow[i] == state_.at[i]) ++alive_[i];
            }
            --deadPlayoutsLeft_;
        }
        if (deadPlayoutsLeft_ == 0) {
            for (uint16_t i = 0; i < total; ++i) {
                dead_[i] = (state_.at[i] != Go::EMPTY && alive_[i] * 2 < all) ? 1 : 0;
            }
            Go::ownership(state_, dead_, own_);
            setMessage("CPU marked. Fix, then Agree");
            markFullDirty();
        }
    }

    if (!touch.justPressed) return;
    if (actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        pressAction(host, millis());
        return;
    }
    if (passRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (mode_ == Mode::Remote) {
            host.beepError();   // no resuming across consoles: agree or end
            return;
        }
        resumeFromMarking(host);
        return;
    }
    const uint16_t p = pointAt(touch.x, touch.y);
    if (p == Go::NO_POINT || state_.at[p] == Go::EMPTY) return;
    Go::toggleDead(state_, dead_, p);
    Go::ownership(state_, dead_, own_);
    agreed_ = 0;   // a change unagrees everybody
    host.playSound(Sound::Tap);
    if (mode_ == Mode::Remote) {
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        Go::Net::encode(Go::Net::Kind::ToggleDead, p, ourFrom_, ourTo_);
        publishOurs(host);
    }
    buttonsStale_ = true;
    saveGame(host);
    markFullDirty();   // territory can change across the board
}

void GoGame::finishGame(AppContext& host) {
    Go::score(state_, phase_ == Phase::Marking ? dead_ : nullptr, score_);
    if (state_.winner == Go::EMPTY) state_.winner = score_.winner;
    state_.over = true;
    phase_ = Phase::Over;
    ghost_ = Go::NO_POINT;
    haveUndo_ = false;
    deadPlayoutsLeft_ = 0;
    if (mode_ == Mode::Computer) {
        if (state_.winner == humanColour_) {
            const uint32_t wins = host.getScore(GO_SCORE.bestKey, 0) + 1;
            host.saveBestScore(GO_SCORE.bestKey, wins, false);
            host.playSound(Sound::Victory);
        } else {
            host.playSound(Sound::GameOver);
        }
    } else if (mode_ == Mode::Remote) {
        host.playSound(state_.winner == humanColour_ ? Sound::Victory : Sound::GameOver);
    } else {
        host.playSound(Sound::Victory);
    }
    Go::ownership(state_, phase_ == Phase::Over && state_.rules == Go::Rules::Territory ? dead_ : nullptr,
                  own_);
    turnStale_ = capturesStale_ = buttonsStale_ = resultStale_ = true;
    saveGame(host);
    markFullDirty();
}

// ---- input ------------------------------------------------------------------------------

void GoGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (mode_ == Mode::Lobby) {
        updateLobby(host, touch);
        return;
    }
    if (confirmShown_ && now >= confirmUntilMs_) {
        buttonsStale_ = true;
        markDirty();
    }
    if (mode_ == Mode::Remote || mode_ == Mode::Waiting) {
        pollRemote(host, now);
        if (mode_ == Mode::Lobby) return;   // the other console ended it
        if (updatePause(host, touch, now)) return;
    }
    if (phase_ == Phase::Thinking) {
        updateComputer(host, now);
        if (phase_ != Phase::Thinking && phase_ != Phase::Play) return;
    }
    if (phase_ == Phase::Marking) {
        updateMarking(host, touch, now);
        return;
    }
    if (phase_ == Phase::Over) {
        if (!touch.justPressed) return;
        if (resultButtonRect(0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.playSound(Sound::Select);
            if (mode_ == Mode::Remote) {
                /* Again across consoles is a new invitation. */
                host.nearbyStop();
                mode_ = Mode::Lobby;
                lobbyStale_ = true;
                markFullDirty();
            } else {
                newGame(mode_);
                saveGame(host);
            }
            return;
        }
        if (resultButtonRect(1).contains(touch.x, touch.y, TOUCH_HIT_SLOP) ||
            actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            pressAction(host, now);
        }
        return;
    }
    if (!touch.justPressed && !touch.down) return;
    if (touch.justPressed && actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        pressAction(host, now);
        return;
    }
    if (mode_ == Mode::Waiting) return;   // nothing to play until they answer
    if (touch.justPressed) {
        if (passRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            pressPass(host);
            return;
        }
        if (undoRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            pressUndo(host);
            return;
        }
        if (big() && placeRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            pressPlace(host);
            return;
        }
    }
    if (phase_ == Phase::Play && humanTurn()) {
        tapBoard(host, touch);
    }
}
