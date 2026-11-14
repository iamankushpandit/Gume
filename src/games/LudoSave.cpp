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
 * could not have reached; and leaving the screen and coming back to it, which
 * is the same subject. Split from LudoGame.cpp for size; the layout and why
 * it is fixed are in LudoGame.h beside Saved. */

namespace {

/* How long a last word stays on the air before anything may replace it. The
 * scanner's longest stretch of hearing nothing is about four seconds, so this
 * is that and a margin. */
constexpr uint32_t LAST_WORD_HOLD_MS = 6000;

}   // namespace

void LudoGame::begin(AppContext& host) {
    const uint32_t now = millis();
    confirmUntilMs_ = 0;
    confirmShown_ = false;
    const bool atTable = mode_ == Mode::Play && net_;
    const SaveSays says = atTable ? saveSays(host) : SaveSays::Nothing;
    if (atTable && (says == SaveSays::ThisTable || says == tableSaid_)) {
        /* Still at a table from before the screen was left. The screen is one
         * instance for the life of the console, so the game is all here, and
         * it is kept rather than read back: the Guest profile's saves are
         * dropped, and for anybody a saved table is at best the same thing.
         * Finished or not -- a finished one still shows how it ended. Only
         * what belongs to the visit that ended starts over. */
        tableSaid_ = says;
        watch_.reset();
        pausePainted_ = false;
        wordHeld_ = false;
        if (phase_ != Phase::Moving) {
            enterTurn(now, rollBonus_);   // the message, and timers that ran out
        }
    } else {
        /* Anything else comes back from the save -- and so does a table in
         * memory once the save has stopped answering for it. Another player
         * has picked the console up since (going idle hands an
         * administrator's console to Guest), and they get their own game
         * rather than somebody else's, which would be written over theirs on
         * the way out. A table its player had saved is still in that save, to
         * come back to. One that was in nobody's save is gone for good: the
         * others at it are told, the way End game tells them, rather than
         * left waiting for a console that will not move. */
        const bool lost = atTable && tableSaid_ != SaveSays::ThisTable && !state_.over;
        const uint8_t lostSession = session_;
        const uint8_t lostPly = applied_;
        if (mode_ == Mode::Table) {
            /* Reached without end() before it: the runtime starts a screen
             * over when going idle has ended an administrator's session. */
            quitTable(host, now);
        }
        if (!restoreGame(host)) {
            leaveTable(host);
            mode_ = Mode::Lobby;
        }
        if (!(mode_ == Mode::Play && net_)) {
            if (lost) {
                host.nearbyEnd(lostSession, Ludo::Net::nextPly(lostPly), lostPly);
                holdWord(now);
            } else if (!wordHolding(now)) {
                /* In no game across consoles now: a word this screen left on
                 * the air on its way out has nobody to carry on with. */
                host.nearbyStop();
                wordHeld_ = false;
            }
        }
    }
    lobbyStale_ = true;
    markFullDirty();
}

void LudoGame::end(AppContext& host) {
    saveGame(host);
    /* Leaving the screen does not end a game played across consoles: only End
     * game does. So a table's last move stays on the air while this console
     * is elsewhere -- the others may not have heard it yet, and taking it off
     * would strand them waiting for a move that had already been made. That
     * holds for a finished game too: its last move is how the others find out
     * it finished. begin() picks the table up again. */
    if (mode_ == Mode::Play && net_) {
        return;
    }
    const uint32_t now = millis();
    if (mode_ == Mode::Table) {
        /* A table still being gathered is left: no game has started at it. */
        quitTable(host, now);
    }
    /* Anything else of ours comes off the air, an invitation included. A word
     * still being held stays up instead; begin() takes it down. */
    if (!wordHolding(now)) {
        host.nearbyStop();
        wordHeld_ = false;
    }
}

void LudoGame::holdWord(uint32_t now) {
    wordAtMs_ = now;
    wordHeld_ = true;
}

bool LudoGame::wordHolding(uint32_t now) const {
    return wordHeld_ && now - wordAtMs_ < LAST_WORD_HOLD_MS;
}

void LudoGame::tickWord(AppContext& host, uint32_t now) {
    if (!wordHeld_ || wordHolding(now)) {
        return;
    }
    /* It has had its time, so the next thing may replace it. In the lobbies
     * it stays up until something does, or the screen is left: Ludo has no
     * score it could be keeping off the air, and a console that has still not
     * heard it gets that much longer to. A local game takes it down, since
     * nothing in one would. */
    wordHeld_ = false;
    if (mode_ == Mode::Play) {
        host.nearbyStop();
    }
}

LudoGame::SaveSays LudoGame::saveSays(AppContext& host) const {
    Saved in{};
    host.loadBlob("game", &in, sizeof(in));
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) {
        return SaveSays::Nothing;   // never written, as Guest's never is
    }
    /* Every save made at a table names it, finished or not, so the player who
     * was at it has exactly this in theirs. */
    return in.net != 0 && in.session == session_ && sameId(in.hostId, hostId_)
               ? SaveSays::ThisTable
               : SaveSays::Other;
}

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
    tableSaid_ = SaveSays::ThisTable;   // it came out of this player's save
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
