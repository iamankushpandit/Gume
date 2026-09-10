#include "LudoGame.h"

/* Remembering a game: the flat blob in NVS, and refusing one that a game
 * could not have reached. Split from LudoGame.cpp for size; the layout and
 * why it is fixed are in LudoGame.h beside Saved. */

void LudoGame::saveGame(AppContext& host) const {
    Saved out{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    out.inGame = (mode_ == Mode::Play && !state_.over && !ended_) ? 1 : 0;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        out.kind[s] = static_cast<uint8_t>(kind_[s]);
        out.owner[s] = owner_[s];
    }
    out.level = static_cast<uint8_t>(level_);
    out.seed = seed_;
    out.state = state_;
    out.net = net_ ? 1 : 0;
    out.role = static_cast<uint8_t>(role_);
    out.session = session_;
    out.chairCount = chairCount_;
    out.selfChair = selfChair_;
    out.applied = applied_;
    out.myPly = myPly_;
    out.myFrom = myFrom_;
    out.myTo = myTo_;
    out.netCode = netCode_;
    out.ended = ended_ ? 1 : 0;
    memcpy(out.hostId, hostId_, sizeof(out.hostId));
    memcpy(out.chairs, chairs_, sizeof(out.chairs));
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
    if (in.net) {
        /* A table: every playing colour has somebody in it, and every chair
         * index points at a chair. */
        if (in.chairCount < 2 || in.chairCount > Ludo::Net::MAX_HUMANS ||
            in.selfChair >= in.chairCount || in.role > static_cast<uint8_t>(Role::Guest)) {
            return false;
        }
        for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
            if (Ludo::playing(st, s) &&
                (in.owner[s] == Ludo::NO_SEAT ||
                 in.owner[s] >= in.chairCount + Ludo::Net::MAX_COMPUTERS)) {
                return false;
            }
        }
    } else {
        /* A seat that was playing has to still be filled. The kinds are saved
         * beside the state, so this only fails on a damaged blob. */
        for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
            if (Ludo::playing(st, s) && kind_[s] == SeatKind::Empty) {
                return false;
            }
        }
    }

    state_ = st;
    seed_ = in.seed;
    net_ = in.net != 0;
    role_ = static_cast<Role>(in.role);
    session_ = in.session;
    chairCount_ = in.chairCount;
    selfChair_ = in.selfChair;
    applied_ = in.applied;
    myPly_ = in.myPly;
    myFrom_ = in.myFrom;
    myTo_ = in.myTo;
    netCode_ = in.netCode;
    ended_ = in.ended != 0;
    memcpy(owner_, in.owner, sizeof(owner_));
    memcpy(hostId_, in.hostId, sizeof(hostId_));
    hostId_[4] = 0;
    memcpy(chairs_, in.chairs, sizeof(chairs_));
    for (Chair& c : chairs_) {
        c.id[4] = 0;
        c.name[sizeof(c.name) - 1] = 0;
    }
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
