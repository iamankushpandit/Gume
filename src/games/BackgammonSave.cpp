#include "BackgammonGame.h"

/* Remembering a game, like Chess: after every move, on Done and on the way
 * out, so a console put down comes back to the same position -- the dice
 * still to play, this turn's undo stack, and a nearby game's place in the
 * conversation included. Fixed layout: loadBlob() refuses a blob whose
 * length changed, and `version` covers one that kept its size. */

void BackgammonGame::saveGame(AppContext& host) const {
    Saved out{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    out.mode = static_cast<uint8_t>(mode_);
    out.phase = static_cast<uint8_t>(phase_);
    out.toMove = toMove_;
    out.winner = winner_;
    out.result = static_cast<uint8_t>(result_);
    out.pos = pos_;
    out.dice = dice_;
    out.rolled = rolled_;
    out.rolls = rolls_;
    out.seed = seed_;
    out.turnCount = turnCount_;
    for (uint8_t i = 0; i < Bg::MAX_DICE; ++i) {
        out.undoPos[i] = undoPos_[i];
        out.undoDice[i] = undoDice_[i];
        out.turnMoves[i] = turnMoves_[i];
        out.outbox[i] = outbox_[i];
    }
    out.ourWhite = ourWhite_ ? 1 : 0;
    memcpy(out.opponent, opponent_, sizeof(out.opponent));
    memcpy(out.opponentName, opponentName_, sizeof(out.opponentName));
    out.session = session_;
    out.applied = applied_;
    out.myPly = myPly_;
    out.myFrom = myFrom_;
    out.myTo = myTo_;
    out.outboxCount = outboxCount_;
    host.saveBlob("game", &out, sizeof(out));
}

bool BackgammonGame::restoreGame(AppContext& host) {
    Saved in{};
    host.loadBlob("game", &in, sizeof(in));
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) {
        return false;
    }
    /* Nothing to come back to from the lobby, and an invitation left waiting
     * has long since expired on the air. */
    const Mode mode = static_cast<Mode>(in.mode);
    if (mode != Mode::Local && mode != Mode::Computer && mode != Mode::Remote) {
        return false;
    }
    if (in.phase > static_cast<uint8_t>(Phase::Over) || in.toMove > Bg::BLACK ||
        in.winner > Bg::BLACK || in.result > static_cast<uint8_t>(Bg::Result::Backgammon) ||
        in.dice.count > Bg::MAX_DICE || in.rolled.count > Bg::MAX_DICE ||
        in.turnCount > Bg::MAX_DICE || in.outboxCount > Bg::MAX_DICE) {
        return false;
    }
    /* Refuse a position a game could not have reached rather than drawing
     * sixteen checkers. */
    if (Bg::checkerTotal(in.pos, Bg::WHITE) != Bg::CHECKERS ||
        Bg::checkerTotal(in.pos, Bg::BLACK) != Bg::CHECKERS) {
        return false;
    }
    if (static_cast<Phase>(in.phase) == Phase::Over) {
        return false;   // a finished game is not worth reopening
    }

    mode_ = mode;
    phase_ = static_cast<Phase>(in.phase);
    toMove_ = in.toMove;
    winner_ = in.winner;
    result_ = static_cast<Bg::Result>(in.result);
    pos_ = in.pos;
    dice_ = in.dice;
    rolled_ = in.rolled;
    rolls_ = in.rolls;
    seed_ = in.seed;
    turnCount_ = in.turnCount;
    for (uint8_t i = 0; i < Bg::MAX_DICE; ++i) {
        undoPos_[i] = in.undoPos[i];
        undoDice_[i] = in.undoDice[i];
        turnMoves_[i] = in.turnMoves[i];
        outbox_[i] = in.outbox[i];
    }
    ourWhite_ = in.ourWhite != 0;
    memcpy(opponent_, in.opponent, sizeof(opponent_));
    opponent_[sizeof(opponent_) - 1] = 0;
    memcpy(opponentName_, in.opponentName, sizeof(opponentName_));
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = in.session;
    applied_ = in.applied;
    myPly_ = in.myPly;
    myFrom_ = in.myFrom;
    myTo_ = in.myTo;
    outboxCount_ = in.outboxCount;
    ended_ = false;
    selected_ = Bg::NO_POINT;
    planned_ = false;   // the computer re-plans from the position it finds
    timerMs_ = millis();
    refreshLegal();

    switch (phase_) {
        case Phase::Opening: setMessage("Tap Roll to start"); break;
        case Phase::Roll: setMessage("Tap Roll"); break;
        case Phase::Moving: setMessage("Pick a checker"); break;
        case Phase::DoneReady: setMessage(legalCount_ == 0 && turnCount_ == 0 ? "No moves: Done"
                                                                             : "Tap Done"); break;
        case Phase::Computer: setMessage("Thinking..."); break;
        case Phase::Remote: setMessage("Their turn"); break;
        case Phase::Over: break;
    }
    if (mode_ == Mode::Remote) {
        /* Say the same thing again: our word went when the screen was left. */
        host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
    }
    return true;
}
