#include "LudoGame.h"
#include "engine/AppRegistry.h"

/* Game flow, input and saving. The board geometry and every pixel of drawing
 * are in LudoBoard.cpp; the rules are in LudoRules.cpp. */

namespace {

constexpr AppMetadata LUDO_METADATA = {
    "ludo",
    "Ludo",
    nullptr,
    "race home",
    "Ludo",
    "Race four tokens home. Play friends or CPU.",
    nullptr,
    LauncherIcon::Ludo,
    35,
    true,
};

/* One square of a hop. Slow enough to count along with, which is how a
 * child checks that a 5 went five squares. */
constexpr uint32_t HOP_MS = 110;
/* A computer waits before it rolls, and again before it moves, so that
 * whoever is watching sees whose turn it became and what was rolled. Without
 * these a computer's whole turn is a single frame. */
constexpr uint32_t CPU_ROLL_MS = 700;
constexpr uint32_t CPU_MOVE_MS = 650;
/* A player's move that makes itself still waits long enough to see the die. */
constexpr uint32_t AUTO_MOVE_MS = 450;
/* How long "No move" or "Three 6s!" stays up before the turn moves on. */
constexpr uint32_t NOTICE_MS = 1200;
/* The YourTurn cue comes a beat after the hand-over, and never over the top
 * of the cue the move itself made: the synthesiser plays one script at a time,
 * so a turn cue played at once would cut a capture's Coin in half. These are
 * how long those cues run (BoardAudio.cpp), rounded up. */
constexpr uint32_t TURN_CUE_GAP_MS = 150;
constexpr uint32_t MOVE_CUE_MS = 300;     // Coin, LevelUp
constexpr uint32_t FINISH_CUE_MS = 750;   // Victory, GameOver
/* End game asks twice, the way Chess does: the button relabels itself and a
 * second press inside this window confirms. */
constexpr uint32_t CONFIRM_MS = 3000;
/* Half a period of the turn dot's and the die's blink: 1.25 flashes a
 * second, far below the 3Hz where flashing becomes a photosensitivity
 * concern. */
constexpr uint32_t BLINK_MS = 400;

}   // namespace

const AppMetadata& ludoAppMetadata() {
    return LUDO_METADATA;
}

const char* LudoGame::title() const {
    return ludoAppMetadata().title;
}

void LudoGame::begin(AppContext& host) {
    confirmUntilMs_ = 0;
    confirmShown_ = false;
    if (!restoreGame(host)) {
        mode_ = Mode::Lobby;
    }
    lobbyStale_ = true;
    markFullDirty();
}

void LudoGame::end(AppContext& host) {
    saveGame(host);
    /* Hands the radio back, as Chess does: a turn stays on the air until it is
     * replaced, so leaving without clearing it would leave this console
     * advertising a game it is no longer in. A game across consoles is saved
     * and says the same thing again when this screen comes back. */
    if (net_ || mode_ == Mode::Table) {
        host.nearbyStop();
    }
    if (mode_ == Mode::Table) {
        leaveTable(host);
        mode_ = Mode::Lobby;
    }
}

bool LudoGame::canStart() const {
    uint8_t seats = 0;
    bool player = false;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (kind_[s] != SeatKind::Empty) {
            ++seats;
        }
        if (kind_[s] == SeatKind::Player) {
            player = true;
        }
    }
    return seats >= 2 && player;
}

uint32_t LudoGame::pace(uint32_t ms) const {
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (Ludo::playing(state_, s) && state_.place[s] == 0 && !isComputer(s)) {
            return ms;
        }
    }
    return ms / 4;
}

void LudoGame::setMessage(const char* text) {
    snprintf(message_, sizeof(message_), "%s", text);
    messageSeat_ = Ludo::NO_SEAT;
}

void LudoGame::setWaiting(uint8_t seat) {
    snprintf(message_, sizeof(message_), "%s", "Waiting for");
    messageSeat_ = seat;
}

bool LudoGame::blinkPhase() {
    /* Slow enough to read as a signal rather than a flicker, and well clear
     * of the rates that matter for photosensitivity. */
    return (millis() / BLINK_MS) & 1U;
}

bool LudoGame::dieFlashing() const {
    return mode_ == Mode::Play && phase_ == Phase::Roll && !state_.over &&
           ownsSeat(state_.turn) && !isComputer(state_.turn);
}

void LudoGame::startGame(AppContext& host) {
    /* A local game: whatever a previous table left on the air goes. */
    host.nearbyStop();
    net_ = false;
    ended_ = false;
    uint8_t mask = 0;
    uint8_t seats[Ludo::SEATS];
    uint8_t count = 0;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (kind_[s] != SeatKind::Empty) {
            mask |= static_cast<uint8_t>(1U << s);
            seats[count++] = s;
        }
    }
    /* Arduino's random() is esp_random() underneath on this core, so two
     * draws are 32 bits nobody chose. The seed fixes every die of the game;
     * see LudoRules.h. */
    seed_ = (static_cast<uint32_t>(random(0x10000)) << 16) ^
            static_cast<uint32_t>(random(0x10000)) ^ millis();
    /* Who goes first is drawn too. Always starting with Red would hand the
     * first move to whoever sits in the top-left, every game. */
    const uint8_t first = seats[Ludo::mix(seed_) % count];
    Ludo::reset(state_, mask, first);
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            shown_[s][t] = state_.pos[s][t];
        }
    }
    face_ = 0;
    mode_ = Mode::Play;
    confirmUntilMs_ = 0;
    enterTurn(millis(), false);
    host.playSound(Sound::Select);
    saveGame(host);
    markFullDirty();
}

void LudoGame::enterTurn(uint32_t now, bool bonus) {
    autoToken_ = Ludo::NO_TOKEN;
    turnCueAtMs_ = 0;
    /* The seat list is NOT repainted for a new turn -- the turn dot moves
     * instead. It repaints when a place is decided (finishMove) and here. */
    actionStale_ = true;
    if (state_.over) {
        phase_ = Phase::Over;
        seatsStale_ = true;
        messageSeat_ = Ludo::NO_SEAT;
        for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
            if (Ludo::playing(state_, s) && state_.place[s] == 1) {
                snprintf(message_, sizeof(message_), "%s wins!", seatName(s));
            }
        }
        markDirty();
        return;
    }
    const uint8_t seat = state_.turn;
    /* Seats nobody here taps for: a computer, or a seat another console
     * decides. Both get the computer's pacing, so a move arriving over the air
     * is shown at the speed a computer's would be. */
    const bool autoSeat = isComputer(seat) || remoteSeat(seat);
    /* A restored game can be holding a roll. Pick up exactly there rather
     * than rolling again, which would be a free reroll for putting the
     * console down. */
    if (state_.pending != 0) {
        if (Ludo::movable(state_) != 0) {
            phase_ = Phase::Choose;
            if (autoSeat) {
                timerMs_ = now + pace(CPU_MOVE_MS);
                setMessage("");
            } else {
                autoToken_ = onlyChoice();
                timerMs_ = now + AUTO_MOVE_MS;
                setMessage(autoToken_ == Ludo::NO_TOKEN ? "Pick a token" : "");
            }
        } else {
            phase_ = Phase::Notice;
            timerMs_ = now + pace(NOTICE_MS);
            setMessage(state_.sixes >= 3 ? "Three 6s!" : "No move");
        }
        markDirty();
        return;
    }
    phase_ = Phase::Roll;
    timerMs_ = now + pace(CPU_ROLL_MS);
    /* A blank die for a new roll. Leaving the last number up reads as "you
     * have already rolled", which is exactly wrong for whoever is next. */
    face_ = 0;
    if (isComputer(seat)) {
        setMessage("Thinking...");
    } else if (remoteSeat(seat)) {
        setWaiting(seat);
    } else {
        setMessage(bonus ? "Roll again!" : "Tap to roll");
        /* A person here has the die. Not on a bonus roll: the turn did not
         * come round, it never left, and "Roll again!" already says so. */
        if (!bonus) {
            const uint32_t at = now + TURN_CUE_GAP_MS;
            turnCueAtMs_ = at > cueEndsMs_ ? at : cueEndsMs_;
        }
    }
    markDirty();
}

void LudoGame::doRoll(AppContext& host, uint32_t now) {
    const uint8_t seat = state_.turn;
    const Ludo::RollResult result = Ludo::roll(state_, seed_);
    face_ = state_.pending;
    turnCueAtMs_ = 0;   // rolled before the cue came: it has nothing to say
    host.playSound(Sound::Tap);
    switch (result) {
        case Ludo::RollResult::Choose:
            phase_ = Phase::Choose;
            if (isComputer(seat) || remoteSeat(seat)) {
                timerMs_ = now + pace(CPU_MOVE_MS);
                setMessage("");
            } else {
                autoToken_ = onlyChoice();
                timerMs_ = now + AUTO_MOVE_MS;
                setMessage(autoToken_ == Ludo::NO_TOKEN ? "Pick a token" : "");
            }
            break;
        case Ludo::RollResult::NoMove: {
            bool anyOut = false;
            for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
                const uint8_t p = state_.pos[state_.turn][t];
                anyOut = anyOut || (p != Ludo::YARD && p != Ludo::HOME);
            }
            phase_ = Phase::Notice;
            timerMs_ = now + pace(NOTICE_MS);
            setMessage(anyOut ? "No move" : "Need a 6");
            break;
        }
        case Ludo::RollResult::Forfeit:
            phase_ = Phase::Notice;
            timerMs_ = now + pace(NOTICE_MS);
            setMessage("Three 6s!");
            break;
    }
    /* A roll nobody could use is decided the moment it is made, so a seat
     * this console plays says so now. A usable one waits for its move. */
    if (net_ && ownsSeat(seat) && result != Ludo::RollResult::Choose) {
        publishPly(host, seat, Ludo::Net::SKIP);
    }
    saveGame(host);
    markDirty();
}

uint8_t LudoGame::onlyChoice() const {
    const uint8_t mask = Ludo::movable(state_);
    uint8_t first = Ludo::NO_TOKEN;
    for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
        if (!(mask & (1U << t))) {
            continue;
        }
        if (first == Ludo::NO_TOKEN) {
            first = t;
        } else if (state_.pos[state_.turn][t] != state_.pos[state_.turn][first]) {
            return Ludo::NO_TOKEN;   // a real choice
        }
    }
    return first;
}

void LudoGame::startMove(AppContext& host, uint8_t token, uint32_t now) {
    const uint8_t seat = state_.turn;
    if (!Ludo::move(state_, token, anim_)) {
        return;
    }
    if (net_ && ownsSeat(seat)) {
        publishPly(host, seat, token);
    }
    animAt_ = anim_.from;
    phase_ = Phase::Moving;
    timerMs_ = now;
    autoToken_ = Ludo::NO_TOKEN;
    setMessage("");
    /* Leaving the yard pops onto the board. A walk says nothing here: every
     * square of it plays a Step as the token lands (stepMove), starting with
     * the first hop on the next frame. */
    if (anim_.from == Ludo::YARD) {
        host.playSound(Sound::Pop);
    }
    /* The rules have already applied the move, so save now: a Lock pressed
     * mid-hop must not bring the token back where it started. */
    saveGame(host);
    markDirty();
}

void LudoGame::stepMove(AppContext& host, uint32_t now) {
    if (now < timerMs_) {
        return;
    }
    const bool fromYard = animAt_ == Ludo::YARD;
    animAt_ = fromYard ? 0 : static_cast<uint8_t>(animAt_ + 1);
    shown_[anim_.seat][anim_.token] = animAt_;
    markDirty();
    /* One Step per square landed on, so a player can count a move by ear.
     * The hop out of the yard had its Pop already. Played before finishMove()
     * on the last square so that a capture's or a home's cue replaces it. */
    if (!fromYard) {
        host.playSound(Sound::Step);
    }
    if (animAt_ >= anim_.to) {
        finishMove(host, now);
        return;
    }
    timerMs_ = now + pace(HOP_MS);
}

void LudoGame::finishMove(AppContext& host, uint32_t now) {
    shown_[anim_.seat][anim_.token] = anim_.to;
    const bool captured = anim_.capturedSeat != Ludo::NO_SEAT;
    if (captured) {
        shown_[anim_.capturedSeat][anim_.capturedToken] = Ludo::YARD;
    }

    /* One cue per move, the one that says the most. */
    if (anim_.seatFinished) {
        seatsStale_ = true;   // a place to write beside the name
        cueEndsMs_ = now + FINISH_CUE_MS;
        /* Whether somebody holding THIS console is playing, and whether it
         * was them: a console across the room finishing is news, not a win. */
        bool anyPlayer = false;
        for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
            anyPlayer = anyPlayer ||
                        (Ludo::playing(state_, s) && ownsSeat(s) && !isComputer(s));
        }
        if (!isComputer(anim_.seat) && ownsSeat(anim_.seat)) {
            host.pulseRgb(0, 255, 40, 450);
            host.playSound(Sound::Victory);
        } else if (state_.place[anim_.seat] == 1 && anyPlayer) {
            host.playSound(Sound::GameOver);   // a computer got there first
        } else {
            host.playSound(Sound::LevelUp);
        }
    } else if (captured) {
        const uint16_t c = seatColour(anim_.seat);
        /* The capturer's own colour, unpacked from RGB565. */
        host.pulseRgb(static_cast<uint8_t>((c >> 11) << 3),
                      static_cast<uint8_t>(((c >> 5) & 0x3F) << 2),
                      static_cast<uint8_t>((c & 0x1F) << 3), 300);
        host.playSound(Sound::Coin);
        cueEndsMs_ = now + MOVE_CUE_MS;
    } else if (anim_.reachedHome) {
        host.playSound(Sound::LevelUp);
        cueEndsMs_ = now + MOVE_CUE_MS;
    }

    saveGame(host);
    enterTurn(now, anim_.bonus);
}

uint8_t LudoGame::tokenAt(int16_t x, int16_t y) const {
    const uint8_t mask = Ludo::movable(state_);
    const uint8_t seat = state_.turn;
    /* A tap anywhere in the yard means a token in it: the spots are 16px
     * apart and the yard is the natural thing to aim at. */
    if (yardRect(seat).contains(x, y, TOUCH_HIT_SLOP)) {
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            if ((mask & (1U << t)) && shown_[seat][t] == Ludo::YARD) {
                return t;
            }
        }
    }
    /* Otherwise the nearest movable token within about a cell and a half.
     * Cells are 13px on a resistive panel; asking for the exact one would
     * make most taps miss. */
    constexpr int32_t REACH = CELL + TOUCH_HIT_SLOP;
    uint8_t best = Ludo::NO_TOKEN;
    int32_t bestD = REACH * REACH + 1;
    for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
        if (!(mask & (1U << t))) {
            continue;
        }
        int16_t cx = 0;
        int16_t cy = 0;
        tokenCentre(seat, t, cx, cy);
        const int32_t dx = x - cx;
        const int32_t dy = y - cy;
        const int32_t d = dx * dx + dy * dy;
        if (d < bestD) {
            bestD = d;
            best = t;
        }
    }
    return best;
}

void LudoGame::pressAction(AppContext& host, uint32_t now) {
    if (phase_ == Phase::Over) {
        /* New game after a finish: back to the lobby with the same seats,
         * which is what "again" usually means. */
        leaveTable(host);
        mode_ = Mode::Lobby;
        confirmUntilMs_ = 0;
        confirmShown_ = false;
        lobbyStale_ = true;
        host.playSound(Sound::Select);
        saveGame(host);
        markFullDirty();
        return;
    }
    if (now < confirmUntilMs_) {
        /* The second press of End game: back to the lobby. At a table, tell
         * the others first, through the service's own ending, and every
         * console at it leaves to its lobby too (see pollTable()). The ending
         * stays on the air after we leave -- nothing here clears it until the
         * screen closes or another game starts -- so a console that has not
         * heard it yet still will. */
        confirmUntilMs_ = 0;
        confirmShown_ = false;
        if (net_) {
            host.nearbyEnd(session_, Ludo::Net::nextPly(applied_), applied_);
            ended_ = true;
            leaveTable(host);
        }
        mode_ = Mode::Lobby;
        lobbyStale_ = true;
        host.playSound(Sound::Select);
        saveGame(host);
        markFullDirty();
        return;
    }
    confirmUntilMs_ = now + CONFIRM_MS;
    actionStale_ = true;
    host.playSound(Sound::Tap);
    markDirty();
}

void LudoGame::updatePlay(AppContext& host, const TouchPoint& touch, uint32_t now) {
    if (confirmShown_ && now >= confirmUntilMs_) {
        actionStale_ = true;   // "Sure?" timed out: back to "End game"
        markDirty();
    }
    if (touch.justPressed && actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        pressAction(host, now);
        return;
    }
    if (phase_ != Phase::Over) {
        /* The turn dot and the die flash on one clock. A change of phase is
         * the only thing that needs a render; the render itself decides what
         * few pixels that is. */
        const bool blink = blinkPhase();
        if (blink != lastBlink_) {
            lastBlink_ = blink;
            markDirty();
        }
    }
    if (net_) {
        pollTable(host, now);   // may take another console's roll
        if (mode_ != Mode::Play) {
            return;             // or may have ended the game for everyone
        }
    }

    const uint8_t seat = state_.turn;
    const bool computer = isComputer(seat);
    const bool remote = remoteSeat(seat);
    switch (phase_) {
        case Phase::Roll:
            if (remote) {
                break;   // pollTable() rolls for it when its turn arrives
            }
            if (!computer && turnCueAtMs_ != 0 && now >= turnCueAtMs_) {
                turnCueAtMs_ = 0;
                host.playSound(Sound::YourTurn);
            }
            if (computer) {
                if (now >= timerMs_ && (!net_ || canPublish(host))) {
                    doRoll(host, now);
                }
            } else if (touch.justPressed &&
                       (dieRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) ||
                        boardRect().contains(touch.x, touch.y))) {
                if (net_ && !canPublish(host)) {
                    /* Somebody has not caught up with our last move yet --
                     * no one seat to name, so not "Waiting for". */
                    setMessage("One moment...");
                    markDirty();
                } else {
                    doRoll(host, now);
                }
            }
            break;
        case Phase::Choose:
            if (remote) {
                if (now >= timerMs_) {
                    startMove(host, netCode_, now);
                }
            } else if (computer) {
                if (now >= timerMs_) {
                    startMove(host, Ludo::botChoose(state_, level_, seed_), now);
                }
            } else if (touch.justPressed) {
                const uint8_t t = tokenAt(touch.x, touch.y);
                if (t != Ludo::NO_TOKEN) {
                    startMove(host, t, now);
                }
            } else if (autoToken_ != Ludo::NO_TOKEN && now >= timerMs_) {
                startMove(host, autoToken_, now);
            }
            break;
        case Phase::Moving:
            stepMove(host, now);
            break;
        case Phase::Notice:
            if (now >= timerMs_) {
                /* A six nobody could use still rolls again; a third six never
                 * does. skip() decides, this only needs to know for the
                 * message. */
                const bool bonus = state_.pending == 6 && state_.sixes < 3;
                Ludo::skip(state_);
                saveGame(host);
                enterTurn(now, bonus);
            }
            break;
        case Phase::Over:
            break;
    }
}

void LudoGame::update(AppContext& host, const TouchPoint& touch) {
    switch (mode_) {
        case Mode::Lobby: updateLobby(host, touch); break;
        case Mode::Table: updateTable(host, touch); break;
        case Mode::Play: updatePlay(host, touch, millis()); break;
    }
}
