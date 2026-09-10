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
/* End game asks twice, the way Chess does: the button relabels itself and a
 * second press inside this window confirms. */
constexpr uint32_t CONFIRM_MS = 3000;

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
        if (kind_[s] == SeatKind::Player && Ludo::playing(state_, s) && state_.place[s] == 0) {
            return ms;
        }
    }
    return ms / 4;
}

void LudoGame::setMessage(const char* text) {
    snprintf(message_, sizeof(message_), "%s", text);
}

void LudoGame::startGame(AppContext& host) {
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
    seatsStale_ = true;
    actionStale_ = true;
    if (state_.over) {
        phase_ = Phase::Over;
        for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
            if (Ludo::playing(state_, s) && state_.place[s] == 1) {
                snprintf(message_, sizeof(message_), "%s wins!", seatName(s));
            }
        }
        markDirty();
        return;
    }
    const uint8_t seat = state_.turn;
    /* A restored game can be holding a roll. Pick up exactly there rather
     * than rolling again, which would be a free reroll for putting the
     * console down. */
    if (state_.pending != 0) {
        if (Ludo::movable(state_) != 0) {
            phase_ = Phase::Choose;
            if (isComputer(seat)) {
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
    } else {
        setMessage(bonus ? "Roll again!" : "Tap to roll");
    }
    markDirty();
}

void LudoGame::doRoll(AppContext& host, uint32_t now) {
    const Ludo::RollResult result = Ludo::roll(state_, seed_);
    face_ = state_.pending;
    host.playSound(Sound::Tap);
    switch (result) {
        case Ludo::RollResult::Choose:
            phase_ = Phase::Choose;
            if (isComputer(state_.turn)) {
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
    if (!Ludo::move(state_, token, anim_)) {
        return;
    }
    animAt_ = anim_.from;
    phase_ = Phase::Moving;
    timerMs_ = now;
    autoToken_ = Ludo::NO_TOKEN;
    setMessage("");
    host.playSound(anim_.from == Ludo::YARD ? Sound::Pop : Sound::Whoosh);
    /* The rules have already applied the move, so save now: a Lock pressed
     * mid-hop must not bring the token back where it started. */
    saveGame(host);
    markDirty();
}

void LudoGame::stepMove(AppContext& host, uint32_t now) {
    if (now < timerMs_) {
        return;
    }
    animAt_ = animAt_ == Ludo::YARD ? 0 : static_cast<uint8_t>(animAt_ + 1);
    shown_[anim_.seat][anim_.token] = animAt_;
    markDirty();
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
        bool anyPlayer = false;
        for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
            anyPlayer = anyPlayer || kind_[s] == SeatKind::Player;
        }
        if (!isComputer(anim_.seat)) {
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
    } else if (anim_.reachedHome) {
        host.playSound(Sound::LevelUp);
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
    if (phase_ == Phase::Over || now < confirmUntilMs_) {
        /* New game after a finish, or the second press of End game: back to
         * the lobby with the same seats, which is what "again" usually means. */
        mode_ = Mode::Lobby;
        confirmUntilMs_ = 0;
        confirmShown_ = false;
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

    const bool computer = isComputer(state_.turn);
    switch (phase_) {
        case Phase::Roll:
            if (computer) {
                if (now >= timerMs_) {
                    doRoll(host, now);
                }
            } else if (touch.justPressed &&
                       (dieRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) ||
                        boardRect().contains(touch.x, touch.y))) {
                doRoll(host, now);
            }
            break;
        case Phase::Choose:
            if (computer) {
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

void LudoGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (seatChipRect(s).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            /* Empty -> Player -> Computer -> Empty: one tap per step, and the
             * chip says what it is now rather than what a tap will do. */
            kind_[s] = static_cast<SeatKind>((static_cast<uint8_t>(kind_[s]) + 1) % 3);
            lobbyStale_ = true;
            host.playSound(Sound::Tap);
            saveGame(host);
            markDirty();
            return;
        }
    }
    for (uint8_t lv = 0; lv < 2; ++lv) {
        if (levelRect(lv).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            level_ = static_cast<Ludo::Level>(lv);
            lobbyStale_ = true;
            host.playSound(Sound::Tap);
            saveGame(host);
            markDirty();
            return;
        }
    }
    if (startRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (canStart()) {
            startGame(host);
        } else {
            host.beepError();
        }
    }
}

void LudoGame::update(AppContext& host, const TouchPoint& touch) {
    if (mode_ == Mode::Lobby) {
        updateLobby(host, touch);
    } else {
        updatePlay(host, touch, millis());
    }
}

void LudoGame::saveGame(AppContext& host) const {
    Saved out{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    out.inGame = (mode_ == Mode::Play && !state_.over) ? 1 : 0;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        out.kind[s] = static_cast<uint8_t>(kind_[s]);
    }
    out.level = static_cast<uint8_t>(level_);
    out.seed = seed_;
    out.state = state_;
    host.saveBlob("game", &out, sizeof(out));
}

bool LudoGame::restoreGame(AppContext& host) {
    Saved in{};
    host.loadBlob("game", &in, sizeof(in));
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) {
        return false;
    }
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (in.kind[s] > static_cast<uint8_t>(SeatKind::Computer)) {
            return false;
        }
    }
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        kind_[s] = static_cast<SeatKind>(in.kind[s]);
    }
    level_ = in.level != 0 ? Ludo::Level::Normal : Ludo::Level::Easy;
    if (!in.inGame) {
        return false;
    }

    /* Refuse anything a game could not have reached rather than drawing a
     * token off the edge of the board. */
    const Ludo::State& st = in.state;
    if (st.over || st.turn >= Ludo::SEATS || st.pending > 6 || st.sixes > 3 ||
        !Ludo::playing(st, st.turn)) {
        return false;
    }
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            const uint8_t p = st.pos[s][t];
            if (p != Ludo::YARD && p > Ludo::HOME) {
                return false;
            }
        }
    }
    /* A seat that was playing has to still be filled. The kinds are saved
     * beside the state, so this only fails on a damaged blob. */
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (Ludo::playing(st, s) && kind_[s] == SeatKind::Empty) {
            return false;
        }
    }

    state_ = st;
    seed_ = in.seed;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            shown_[s][t] = state_.pos[s][t];
        }
    }
    face_ = state_.pending;
    mode_ = Mode::Play;
    enterTurn(millis(), false);
    return true;
}
