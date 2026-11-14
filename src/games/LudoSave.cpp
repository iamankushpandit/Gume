// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

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
    /* A takeover chosen but not yet said is not saved: if the console is
     * restarted first, the chair is simply quiet again and can be dropped
     * again. What IS said is part of the game. */
    out.dropped = droppedChairs_;
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
    droppedChairs_ = static_cast<uint8_t>(in.dropped & ((1U << chairCount_) - 1));
    pendingDrop_ = Ludo::Net::MAX_HUMANS;
    watch_.reset();
    pausePainted_ = false;
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

/* Leaving a game in memory as well as in NVS, a hop in flight included.
 * Guest's saves are dropped, so for Guest this is the only way a game
 * survives Home at all.
 *
 * Kept quietly, because End game is right there on the panel. Not kept: the
 * lobby, a table still being gathered (end() takes this console away from
 * it), a game whose places are all decided or that somebody ended, and a
 * local game nobody has rolled in yet. A game across consoles is kept from
 * the start, because the others are already at the table. */
Game::Keep LudoGame::keepsGame() const {
    if (mode_ != Mode::Play || phase_ == Phase::Over || state_.over || ended_) {
        return Keep::No;
    }
    if (!net_ && state_.rolls == 0) return Keep::No;
    return Keep::Quietly;
}

/* The game is exactly as end() left it, which for a game in play is only
 * written out; a table's last move was left on the air and the poll goes on
 * republishing it.
 *
 * One clock paces a computer's roll and move, each hop, an automatic move
 * and a roll nobody could use; it starts again where it was rather than
 * having run out while nobody was looking, and so does the "your turn" cue
 * that was waiting for it. cueEndsMs_ is left alone: that cue played out
 * while the screen was away. A half-pressed End game is closed: a "Sure?"
 * waiting for the first tap after a break is a game ended by accident. */
void LudoGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    if (mode_ != Mode::Play) return;
    movedOn(timerMs_, awayMs);
    movedOn(turnCueAtMs_, awayMs);
    confirmUntilMs_ = 0;
}
