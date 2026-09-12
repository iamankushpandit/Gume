#include "GoGame.h"

#include <string.h>

/* Remembering a game, like Chess: after every move and on the way out, so a
 * console put down comes back to the same position. The board is packed to
 * two bits a point, so a 19x19 game is a hundred bytes rather than four
 * hundred; the undo copy and the computer's working state are not saved --
 * a game put down loses its undo, which is the lesser thing to lose. The
 * lobby's chips are saved beside the game so they come back as they were
 * left. Fixed layout: loadBlob() refuses a blob whose length changed, and
 * `version` covers one that kept its size. */

void GoGame::saveGame(AppContext& host) const {
    Saved out{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    out.mode = static_cast<uint8_t>(mode_);
    out.phase = static_cast<uint8_t>(phase_ == Phase::Thinking ? Phase::Play : phase_);
    out.boardSize = boardSize_;
    out.rules = static_cast<uint8_t>(rules_);
    out.level = static_cast<uint8_t>(level_);
    out.n = state_.n;
    const uint16_t total = Go::points(state_.n);
    for (uint16_t p = 0; p < total; ++p) {
        out.at[p >> 2] = static_cast<uint8_t>(out.at[p >> 2] | ((state_.at[p] & 3) << ((p & 3) * 2)));
        if (dead_[p] != 0) out.dead[p >> 3] = static_cast<uint8_t>(out.dead[p >> 3] | (1U << (p & 7)));
    }
    out.toMove = state_.toMove;
    out.ko = state_.ko;
    out.captured[0] = state_.captured[Go::BLACK];
    out.captured[1] = state_.captured[Go::WHITE];
    out.passes = state_.passes;
    out.moves = state_.moves;
    out.last = state_.last;
    out.over = state_.over ? 1 : 0;
    out.winner = state_.winner;
    out.humanColour = humanColour_;
    out.agreed = agreed_;
    memcpy(out.opponent, opponent_, sizeof(out.opponent));
    memcpy(out.opponentName, opponentName_, sizeof(out.opponentName));
    out.session = session_;
    out.ourPly = ourPly_;
    out.theirPly = theirPly_;
    out.ourFrom = ourFrom_;
    out.ourTo = ourTo_;
    out.ended = ended_ ? 1 : 0;
    host.saveBlob("game", &out, sizeof(out));
}

bool GoGame::restoreGame(AppContext& host) {
    Saved in{};
    host.loadBlob("game", &in, sizeof(in));
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) {
        return false;
    }
    /* The chips come back whatever else does. */
    if (in.rules < Go::RULES_COUNT) rules_ = static_cast<Go::Rules>(in.rules);
    level_ = in.level != 0 ? Go::Level::Medium : Go::Level::Easy;
    boardSize_ = (in.boardSize > 9 && BIG_BOARD_AVAILABLE) ? 19 : 9;

    const Mode mode = static_cast<Mode>(in.mode);
    if (mode != Mode::Local && mode != Mode::Computer && mode != Mode::Remote) {
        return false;   // nothing to come back to from the lobby or an invitation
    }
    if (in.phase > static_cast<uint8_t>(Phase::Over) || (in.n != 9 && in.n != 19) ||
        (in.n == 19 && !BIG_BOARD_AVAILABLE) || in.toMove < Go::BLACK || in.toMove > Go::WHITE ||
        in.winner > Go::WHITE || in.passes > 2 || in.humanColour < Go::BLACK ||
        in.humanColour > Go::WHITE) {
        return false;
    }
    Go::reset(state_, in.n, rules_);
    const uint16_t total = Go::points(state_.n);
    for (uint16_t p = 0; p < total; ++p) {
        const uint8_t v = static_cast<uint8_t>((in.at[p >> 2] >> ((p & 3) * 2)) & 3);
        if (v > Go::WHITE) return false;
        state_.at[p] = v;
        dead_[p] = (in.dead[p >> 3] >> (p & 7)) & 1;
    }
    if (in.ko != Go::NO_POINT && in.ko >= total) return false;
    if (in.last != Go::NO_POINT && in.last >= total) return false;
    state_.toMove = in.toMove;
    state_.ko = in.ko;
    state_.captured[Go::BLACK] = in.captured[0];
    state_.captured[Go::WHITE] = in.captured[1];
    state_.passes = in.passes;
    state_.moves = in.moves;
    state_.last = in.last;
    state_.over = in.over != 0;
    state_.winner = in.winner;
    /* Refuse a board a game could not have reached rather than drawing it:
     * a stone with no liberties is not a position. */
    for (uint16_t p = 0; p < total; ++p) {
        if (state_.at[p] != Go::EMPTY && Go::liberties(state_, p) == 0) return false;
    }
    mode_ = mode;
    phase_ = static_cast<Phase>(in.phase);
    humanColour_ = in.humanColour;
    agreed_ = in.agreed;
    memcpy(opponent_, in.opponent, sizeof(opponent_));
    opponent_[sizeof(opponent_) - 1] = 0;
    memcpy(opponentName_, in.opponentName, sizeof(opponentName_));
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = in.session;
    ourPly_ = in.ourPly;
    theirPly_ = in.theirPly;
    ourFrom_ = in.ourFrom;
    ourTo_ = in.ourTo;
    ended_ = in.ended != 0;
    ghost_ = Go::NO_POINT;
    haveUndo_ = false;
    deadPlayoutsLeft_ = 0;
    if (phase_ == Phase::Over) {
        Go::score(state_, state_.rules == Go::Rules::Territory ? dead_ : nullptr, score_);
    }
    if (phase_ == Phase::Marking || phase_ == Phase::Over) {
        Go::ownership(state_, dead_, own_);
    } else {
        memset(own_, 0, sizeof(own_));
    }
    /* A computer to move comes back thinking. */
    if (mode_ == Mode::Computer && phase_ == Phase::Play && !state_.over && !humanTurn()) {
        phase_ = Phase::Thinking;
        timerMs_ = millis() + 400;
        if (level_ == Go::Level::Medium) Go::beginSearch(search_, state_, rng_.next());
    }
    setMessage(phase_ == Phase::Marking ? "Tap groups that cannot live"
               : humanTurn()             ? "Tap a point"
                                         : "");
    return true;
}
