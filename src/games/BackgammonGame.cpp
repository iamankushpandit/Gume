#include "BackgammonGame.h"
#include "engine/AppRegistry.h"
#include "engine/Entropy.h"

/* Turn flow, input and the computer's pacing. Drawing is in BackgammonDraw,
 * the nearby session in BackgammonNet, saving in BackgammonSave; the rules
 * and the computer's choice of moves in BackgammonRules / BackgammonAi. */

namespace {

constexpr AppMetadata BACKGAMMON_METADATA = {
    "backgammon",
    "Backgammon",
    nullptr,
    "vs friend or computer",
    "Backgammon",
    "Race your 15 checkers home and off.",
    nullptr,
    LauncherIcon::Backgammon,
    36,
    true,
};

/* The computer waits before it plays, then shows its checkers moving one at
 * a time, so a child can follow what it did rather than find the board
 * changed. */
constexpr uint32_t CPU_THINK_MS = 700;
constexpr uint32_t CPU_MOVE_MS = 450;
/* How long "No moves" stays up before the turn passes on its own. */
constexpr uint32_t NOTICE_MS = 1400;
constexpr uint32_t CONFIRM_MS = 3000;

}   // namespace

const AppMetadata& backgammonAppMetadata() {
    return BACKGAMMON_METADATA;
}

const char* BackgammonGame::title() const {
    return backgammonAppMetadata().title;
}

void BackgammonGame::begin(AppContext& host) {
    confirmUntilMs_ = 0;
    confirmShown_ = false;
    if (!restoreGame(host)) {
        mode_ = Mode::Lobby;
    }
    lobbyStale_ = true;
    panelStale_ = true;
    markFullDirty();
}

void BackgammonGame::end(AppContext& host) {
    saveGame(host);
    /* A turn stays on the air until it is replaced; leaving without clearing
     * it would leave this console advertising a game it is no longer in --
     * including an ending we sent and walked away from. Unconditional, as in
     * Chess. A nearby game is saved and says the same thing again when this
     * screen returns. */
    host.nearbyStop();
}

void BackgammonGame::setMessage(const char* text) {
    snprintf(message_, sizeof(message_), "%s", text);
    panelStale_ = true;
}

bool BackgammonGame::humanTurn() const {
    switch (mode_) {
        case Mode::Local: return true;
        case Mode::Computer: return toMove_ == Bg::WHITE;
        case Mode::Remote: return toMove_ == (ourWhite_ ? Bg::WHITE : Bg::BLACK);
        default: return false;
    }
}

const char* BackgammonGame::sideName(uint8_t side) const {
    if (mode_ == Mode::Computer) {
        return side == Bg::WHITE ? "You" : "CPU";
    }
    if (mode_ == Mode::Remote || mode_ == Mode::Waiting) {
        if (side == (ourWhite_ ? Bg::WHITE : Bg::BLACK)) return "You";
        return opponentName_[0] != 0 ? opponentName_ : opponent_;
    }
    return side == Bg::WHITE ? "White" : "Black";
}

void BackgammonGame::newGame(Mode mode) {
    mode_ = mode;
    Bg::setup(pos_);
    rolls_ = 0;
    if (mode != Mode::Remote && mode != Mode::Waiting) {
        /* Hardware entropy, as Dice uses. A nearby game sets its own seed from
         * the session so both consoles roll the same dice. */
        seed_ = Entropy::below(0xFFFFFFFFU);
    }
    dice_ = Bg::Dice{};
    rolled_ = Bg::Dice{};
    result_ = Bg::Result::None;
    turnCount_ = 0;
    selected_ = Bg::NO_POINT;
    legalCount_ = 0;
    planned_ = false;
    outboxCount_ = 0;
    ended_ = false;
    toMove_ = Bg::WHITE;
    phase_ = Phase::Opening;
    confirmUntilMs_ = 0;
    setMessage("Tap Roll to start");
    markFullDirty();
}

void BackgammonGame::refreshLegal() {
    legalCount_ = Bg::legalMoves(pos_, toMove_, dice_, legal_, Bg::MAX_MOVES);
}

void BackgammonGame::doOpening(AppContext& host) {
    uint8_t first = Bg::WHITE;
    Bg::openingRoll(seed_, rolls_, first, dice_);
    rolled_ = dice_;
    toMove_ = first;
    turnCount_ = 0;
    refreshLegal();
    host.playSound(Sound::Select);
    if (humanTurn()) {
        phase_ = Phase::Moving;
        char line[24];
        snprintf(line, sizeof(line), "%s first", sideName(first));
        setMessage(line);
    } else if (mode_ == Mode::Computer) {
        phase_ = Phase::Computer;
        planned_ = false;
        timerMs_ = millis() + CPU_THINK_MS;
        setMessage("CPU first");
    } else {
        phase_ = Phase::Remote;
        setMessage("They go first");
    }
    panelStale_ = true;
    markDirty();
    saveGame(host);
}

void BackgammonGame::startTurn(AppContext& host, uint8_t side) {
    toMove_ = side;
    dice_ = Bg::Dice{};
    rolled_ = Bg::Dice{};
    turnCount_ = 0;
    select(Bg::NO_POINT);
    legalCount_ = 0;
    planned_ = false;
    panelStale_ = true;
    if (humanTurn()) {
        phase_ = Phase::Roll;
        setMessage("Tap Roll");
    } else {
        /* The dice are a function of the seed, so the computer's -- and the
         * other console's -- are known the moment the turn begins. Showing
         * them now is what lets a child follow the moves that come. */
        Bg::rollDice(seed_, rolls_, dice_);
        rolled_ = dice_;
        refreshLegal();
        timerMs_ = millis() + (legalCount_ == 0 ? NOTICE_MS : CPU_THINK_MS);
        if (mode_ == Mode::Computer) {
            phase_ = Phase::Computer;
            setMessage(legalCount_ == 0 ? "CPU: no moves" : "Thinking...");
        } else {
            phase_ = Phase::Remote;
            setMessage(legalCount_ == 0 ? "They can't move" : "Their turn");
        }
    }
    markDirty();
    saveGame(host);
}

void BackgammonGame::doRoll(AppContext& host) {
    Bg::rollDice(seed_, rolls_, dice_);
    rolled_ = dice_;
    host.playSound(Sound::Tap);
    refreshLegal();
    if (legalCount_ == 0) {
        phase_ = Phase::DoneReady;
        setMessage("No moves: Done");
    } else {
        phase_ = Phase::Moving;
        setMessage("Pick a checker");
    }
    markDirty();
    saveGame(host);
}

void BackgammonGame::select(uint8_t from) {
    if (from == selected_) {
        return;
    }
    /* The old source and its targets lose their marks, the new ones gain
     * them: exactly those places repaint. */
    for (uint8_t pass = 0; pass < 2; ++pass) {
        const uint8_t src = pass == 0 ? selected_ : from;
        if (src == Bg::NO_POINT) continue;
        markPlace(src);
        for (uint8_t i = 0; i < legalCount_; ++i) {
            if (legal_[i].from == src) markPlace(legal_[i].to);
        }
    }
    selected_ = from;
    markDirty();
}

bool BackgammonGame::isTarget(uint8_t place) const {
    if (selected_ == Bg::NO_POINT) return false;
    for (uint8_t i = 0; i < legalCount_; ++i) {
        if (legal_[i].from == selected_ && legal_[i].to == place) return true;
    }
    return false;
}

void BackgammonGame::playMove(AppContext& host, const Bg::Move& m, bool byHuman) {
    select(Bg::NO_POINT);
    if (byHuman && turnCount_ < Bg::MAX_DICE) {
        undoPos_[turnCount_] = pos_;
        undoDice_[turnCount_] = dice_;
        turnMoves_[turnCount_] = m;
        ++turnCount_;
    }
    const bool hit = Bg::apply(pos_, toMove_, m);
    Bg::useDie(dice_, m.die);
    markPlace(m.from);
    markPlace(m.to);
    if (hit) markPlace(Bg::BAR);
    host.playSound(hit ? Sound::Whoosh : m.to == Bg::OFF ? Sound::Coin : Sound::Tap);
    panelStale_ = true;

    if (Bg::won(pos_, toMove_)) {
        finishGame(host);
        return;
    }
    refreshLegal();
    if (byHuman) {
        if (legalCount_ == 0) {
            phase_ = Phase::DoneReady;
            setMessage("Tap Done");
        } else {
            phase_ = Phase::Moving;
            setMessage("Pick a checker");
        }
    }
    markDirty();
    saveGame(host);
}

void BackgammonGame::pressDone(AppContext& host) {
    host.playSound(Sound::Select);
    if (mode_ == Mode::Remote) {
        /* Done is when the turn goes on the air: the moves queue here and
         * pollRemote() sends them one per ply, each once the other console
         * has the one before. */
        for (uint8_t i = 0; i < turnCount_ && outboxCount_ < Bg::MAX_DICE; ++i) {
            outbox_[outboxCount_++] = turnMoves_[i];
        }
    }
    startTurn(host, Bg::opponent(toMove_));
}

void BackgammonGame::pressUndo(AppContext& host) {
    if (turnCount_ == 0 || !humanTurn() ||
        (phase_ != Phase::Moving && phase_ != Phase::DoneReady)) {
        host.beepError();
        return;
    }
    select(Bg::NO_POINT);
    --turnCount_;
    const Bg::Move& m = turnMoves_[turnCount_];
    pos_ = undoPos_[turnCount_];
    dice_ = undoDice_[turnCount_];
    markPlace(m.from);
    markPlace(m.to);
    markPlace(Bg::BAR);
    markPlace(Bg::OFF);
    refreshLegal();
    phase_ = Phase::Moving;
    setMessage("Pick a checker");
    host.playSound(Sound::Tap);
    markDirty();
    saveGame(host);
}

void BackgammonGame::finishGame(AppContext& host) {
    winner_ = toMove_;
    result_ = Bg::result(pos_, winner_);
    phase_ = Phase::Over;
    if (mode_ == Mode::Remote && humanTurn()) {
        /* The winning moves still have to reach the other console: no Done is
         * needed to finish, so they go to the outbox now. */
        for (uint8_t i = 0; i < turnCount_ && outboxCount_ < Bg::MAX_DICE; ++i) {
            outbox_[outboxCount_++] = turnMoves_[i];
        }
        turnCount_ = 0;
    }
    static const char* const HOW[4] = {"", "wins", "wins a gammon", "wins a backgammon"};
    char line[24];
    snprintf(line, sizeof(line), "%s %s", sideName(winner_), HOW[static_cast<uint8_t>(result_)]);
    setMessage(line);
    const bool ours = mode_ == Mode::Local || humanTurn();
    if (ours) {
        host.pulseRgb(0, 255, 40, 450);
    }
    host.playSound(ours ? Sound::Victory : Sound::GameOver);
    markDirty();
    saveGame(host);
}

void BackgammonGame::pressAction(AppContext& host, uint32_t now) {
    if (phase_ == Phase::Over) {
        if (outboxCount_ > 0) {
            host.beepError();   // the winning moves are still on their way
            return;
        }
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
            /* The service's own ending, which stays on the air after we leave
             * so the other console still hears it; it goes back to its lobby
             * too (pollRemote). */
            host.nearbyEnd(session_, static_cast<uint8_t>((applied_ + 1) & 0x7F), applied_);
            ended_ = true;
        }
        mode_ = Mode::Lobby;
        lobbyStale_ = true;
        host.playSound(Sound::Select);
        saveGame(host);
        markFullDirty();
        return;
    }
    confirmUntilMs_ = now + CONFIRM_MS;
    panelStale_ = true;
    host.playSound(Sound::Tap);
    markDirty();
}

void BackgammonGame::tapBoard(AppContext& host, int16_t x, int16_t y) {
    const uint8_t place = placeAt(x, y);
    if (place == Bg::NO_POINT) {
        return;
    }
    if (selected_ != Bg::NO_POINT && isTarget(place)) {
        Bg::Move m;
        if (Bg::findMove(pos_, toMove_, dice_, selected_, place, m)) {
            playMove(host, m, true);
        }
        return;
    }
    if (place == selected_) {
        select(Bg::NO_POINT);   // tapped again: put it down
        return;
    }
    for (uint8_t i = 0; i < legalCount_; ++i) {
        if (legal_[i].from == place) {
            select(place);
            setMessage("Pick a point");
            host.playSound(Sound::Tap);
            return;
        }
    }
    host.beepError();   // nothing there that can move
}

void BackgammonGame::updateComputer(AppContext& host, uint32_t now) {
    if (now < timerMs_) {
        return;
    }
    if (!planned_) {
        Bg::choosePlan(pos_, toMove_, dice_, plan_);
        planIndex_ = 0;
        planned_ = true;
        timerMs_ = now + CPU_MOVE_MS;
        return;
    }
    if (planIndex_ < plan_.count) {
        /* Checked, even though the plan came from the same rules: a search cut
         * short by its node limit could in principle plan a weaker sequence,
         * and a move that is not legal must never be played. */
        Bg::Move m;
        const Bg::Move& want = plan_.moves[planIndex_];
        if (!Bg::findMove(pos_, toMove_, dice_, want.from, want.to, m)) {
            if (legalCount_ == 0) {
                planIndex_ = plan_.count;
                return;
            }
            m = legal_[0];
        }
        ++planIndex_;
        playMove(host, m, false);
        timerMs_ = now + CPU_MOVE_MS;
        return;
    }
    if (phase_ == Phase::Computer) {
        startTurn(host, Bg::opponent(toMove_));
    }
}

void BackgammonGame::updateBoard(AppContext& host, const TouchPoint& touch, uint32_t now) {
    if (confirmShown_ && now >= confirmUntilMs_) {
        panelStale_ = true;
        markDirty();
    }
    if (mode_ == Mode::Remote || mode_ == Mode::Waiting) {
        pollRemote(host, now);
        if (mode_ == Mode::Lobby) {
            return;   // the other console ended it
        }
    }
    if (phase_ == Phase::Computer) {
        updateComputer(host, now);
    }
    if (!touch.justPressed) {
        return;
    }
    if (actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        pressAction(host, now);
        return;
    }
    if (mode_ == Mode::Waiting) {
        return;   // nothing to play until they answer
    }
    if (rollRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (phase_ == Phase::Opening && mode_ != Mode::Remote) {
            doOpening(host);
        } else if (phase_ == Phase::Roll && humanTurn()) {
            doRoll(host);
        } else if (phase_ == Phase::DoneReady) {
            pressDone(host);
        } else {
            host.beepError();
        }
        return;
    }
    if (undoRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        pressUndo(host);
        return;
    }
    if (phase_ == Phase::Moving && humanTurn()) {
        tapBoard(host, touch.x, touch.y);
    }
}

void BackgammonGame::update(AppContext& host, const TouchPoint& touch) {
    if (mode_ == Mode::Lobby) {
        updateLobby(host, touch);
        return;
    }
    updateBoard(host, touch, millis());
}
