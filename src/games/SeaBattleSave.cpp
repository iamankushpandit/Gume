#include "SeaBattleGame.h"

#include <string.h>

/* Remembering a game. See the Saved struct in SeaBattleGame.h. */

// ------------------------------------------------------------- persistence

void SeaBattleGame::saveGame(AppContext& host) const {
    Saved out{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    out.mode = static_cast<uint8_t>(mode_);
    out.phase = static_cast<uint8_t>(phase_);
    out.turnPlayer = turnPlayer_;
    out.placed[0] = placed_[0] ? 1 : 0;
    out.placed[1] = placed_[1] ? 1 : 0;
    out.won = won_ ? 1 : 0;
    out.lost = lost_ ? 1 : 0;
    out.ended = ended_ ? 1 : 0;
    out.endedByUs = endedByUs_ ? 1 : 0;
    out.lastShot = static_cast<uint8_t>(lastShot_);
    out.pendingCell = pendingCell_;
    out.owedReply = static_cast<uint8_t>(owedReply_);
    out.remoteFiresFirst = remoteFiresFirst_ ? 1 : 0;
    memcpy(out.opponent, opponent_, sizeof(out.opponent));
    memcpy(out.opponentName, opponentName_, sizeof(out.opponentName));
    out.session = session_;
    out.ourPly = ourPly_;
    out.theirPly = theirPly_;
    out.ourShotCell = ourShotCell_;
    out.ourReplyCode = ourReplyCode_;
    for (uint8_t s = 0; s < 2; ++s) {
        memcpy(out.shipAt[s], side_[s].shipAt, CELLS);
        for (uint8_t c = 0; c < CELLS; ++c) {
            out.shot[s][c] = static_cast<uint8_t>(side_[s].shot[c]);
            out.incoming[s][c] = side_[s].incoming[c] ? 1 : 0;
        }
        out.hitsScored[s] = side_[s].hitsScored;
        out.hitsTaken[s] = side_[s].hitsTaken;
        out.sunkAgainstUs[s] = side_[s].sunkAgainstUs;
    }
    host.saveBlob("game", &out, sizeof(out));
}

bool SeaBattleGame::restoreGame(AppContext& host) {
    Saved in{};
    host.loadBlob("game", &in, sizeof(in));
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) return false;

    const Mode m = static_cast<Mode>(in.mode);
    if (m != Mode::Local && m != Mode::Remote) return false;
    /* A finished game is restored as nothing, so the screen opens at the lobby
     * rather than at a result somebody has already read. */
    if (in.won || in.lost || in.ended) return false;
    if (in.turnPlayer > 1) return false;

    mode_ = m;
    phase_ = static_cast<Phase>(in.phase);
    turnPlayer_ = in.turnPlayer;
    placed_[0] = in.placed[0] != 0;
    placed_[1] = in.placed[1] != 0;
    won_ = lost_ = ended_ = false;
    endedByUs_ = in.endedByUs != 0;
    lastShot_ = static_cast<Shot>(in.lastShot);
    pendingCell_ = in.pendingCell;
    owedReply_ = static_cast<Shot>(in.owedReply);
    remoteFiresFirst_ = in.remoteFiresFirst != 0;
    memcpy(opponent_, in.opponent, sizeof(opponent_));
    opponent_[sizeof(opponent_) - 1] = 0;
    memcpy(opponentName_, in.opponentName, sizeof(opponentName_));
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = in.session;
    ourPly_ = in.ourPly;
    theirPly_ = in.theirPly;
    ourShotCell_ = in.ourShotCell;
    ourReplyCode_ = in.ourReplyCode;
    for (uint8_t s = 0; s < 2; ++s) {
        memcpy(side_[s].shipAt, in.shipAt[s], CELLS);
        for (uint8_t c = 0; c < CELLS; ++c) {
            side_[s].shot[c] = static_cast<Mark>(in.shot[s][c]);
            side_[s].incoming[c] = in.incoming[s][c] != 0;
        }
        side_[s].hitsScored = in.hitsScored[s];
        side_[s].hitsTaken = in.hitsTaken[s];
        side_[s].sunkAgainstUs = in.sunkAgainstUs[s];
    }
    return true;
}

