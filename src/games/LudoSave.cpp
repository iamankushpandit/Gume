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
 * it is fixed are in LudoGame.h beside Saved.
 *
 * There are two saves. A table of consoles is the console's: it is kept in
 * the console's slot, whoever is holding the console, and only End game or
 * the game finishing does away with it. A game on this console alone, and the
 * lobby's seats and level, are the player's.
 *
 * The last section is the other side of the same thing: another console at
 * the table going quiet, the pause while it is, and ending the table. */

namespace {

/* How long a last word stays on the air before anything may replace it. The
 * scanner's longest stretch of hearing nothing is about four seconds, so this
 * is that and a margin. */
constexpr uint32_t LAST_WORD_HOLD_MS = 6000;

/* Where every nearby game used to keep its save, this one included, each
 * writing over the others'. Read once for a game left there; never written. */
constexpr char OLD_SHARED_KEY[] = "game";

}   // namespace

void LudoGame::begin(AppContext& host) {
    const uint32_t now = millis();
    confirmUntilMs_ = 0;
    confirmShown_ = false;
    backResumes_ = false;
    if (mode_ == Mode::Play && net_) {
        /* Still at a table from before the screen was left. The screen is one
         * instance for the life of the console, so the game is all here, and
         * it is kept exactly as it is -- finished or not: a finished one still
         * shows how it ended -- whoever is holding the console now. The
         * others at the table are playing this console, not a player. Only
         * what belongs to the visit that ended starts over. */
        watch_.reset();
        pausePainted_ = false;
        wordHeld_ = false;
        if (phase_ != Phase::Moving) {
            enterTurn(now, rollBonus_);   // the message, and timers that ran out
        }
    } else {
        if (mode_ == Mode::Table) {
            /* Reached without end() before it: the runtime starts a screen
             * over when going idle has ended an administrator's session. A
             * table still being gathered is not a game, and is left. */
            quitTable(host, now);
        }
        if (restoreTable(host)) {
            /* The console was restarted in the middle of a table. Say our
             * turn again: nothing of ours is on the air after a restart, and
             * the others are waiting on it. */
            host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
        } else if (!restorePlayer(host, true)) {
            leaveTable(host);
            mode_ = Mode::Lobby;
        }
        if (!(mode_ == Mode::Play && net_) && !wordHolding(now)) {
            /* In no game across consoles now, so nothing of this screen's
             * belongs on the air. An ending still being held is the one
             * exception, and the service takes that down by itself. */
            host.nearbyStop();
            wordHeld_ = false;
        }
        NearbySeat seat;
        if (mode_ == Mode::Play && !net_ && host.nearbyInviteForUs(seat) && seat.forThisGame) {
            /* Somebody is asking this console to their table, and the banner
             * that said so has sent the player here. Their own game would be
             * a screen with no way to say yes, so the table lobby comes up
             * over it. The game is not touched: it stays in the save, Back
             * returns to it, and so does opening the screen again. */
            setAside_ = true;
            openTable(host);
            backResumes_ = true;
        }
    }
    lobbyStale_ = true;
    markFullDirty();
}

void LudoGame::end(AppContext& host) {
    const uint32_t now = millis();
    if (mode_ == Mode::Table) {
        /* A table still being gathered is left: no game has started at it. */
        quitTable(host, now);
    }
    saveGame(host);
    if (mode_ == Mode::Play && net_) {
        /* Leaving the screen does not end a game played across consoles: only
         * End game does. So a table's last move stays on the air while this
         * console is elsewhere -- the others may not have heard it yet, and
         * taking it off would strand them waiting for a move that had already
         * been made. begin() picks the table up again.
         *
         * A table that is over has only its last move left to say, which is
         * how the others find out it finished. That is given a minute and
         * then taken down, or this console would look as if it were in a
         * game until the screen was opened again. */
        if (state_.over) {
            host.nearbyStopLater();
        }
        return;
    }
    /* Anything else of ours comes off the air, an invitation included. A word
     * still being held stays up instead: holdWord() has already asked the
     * service to take it down. */
    if (!wordHolding(now)) {
        host.nearbyStop();
        wordHeld_ = false;
    }
}

void LudoGame::holdWord(AppContext& host, uint32_t now) {
    wordAtMs_ = now;
    wordHeld_ = true;
    /* Bounded from the start. Whatever this screen does next -- sits in a
     * lobby, is left, is never opened again -- the word comes down in a
     * minute unless something has replaced it first. */
    host.nearbyStopLater();
}

bool LudoGame::wordHolding(uint32_t now) const {
    return wordHeld_ && now - wordAtMs_ < LAST_WORD_HOLD_MS;
}

void LudoGame::tickWord(AppContext& host, uint32_t now) {
    if (!wordHeld_ || wordHolding(now)) {
        return;
    }
    /* It has had its time, so the next thing may replace it. In the lobbies
     * it stays up until something does or the service's minute is over, so a
     * console that has still not heard it gets that much longer to. A local
     * game takes it down now, since nothing in one would replace it. */
    wordHeld_ = false;
    if (mode_ == Mode::Play) {
        host.nearbyStop();
    }
}

// ---- the two saves ------------------------------------------------------------

void LudoGame::saveGame(AppContext& host) const {
    const bool table = mode_ == Mode::Play && net_;
    if (!table && setAside_) {
        /* The player's save is holding a game this screen is not showing.
         * Writing the lobby over it would lose that game for a seat chip. */
        return;
    }
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
    if (table) {
        /* The console's, and only there: a player's own game is never written
         * over by a table, and Guest, whose saves are dropped, keeps a table
         * through a power cut like anybody else. */
        host.saveConsoleBlob(SAVE_KEY, &out, sizeof(out));
    } else {
        host.saveBlob(SAVE_KEY, &out, sizeof(out));
    }
}

bool LudoGame::readable(const Saved& in) {
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) {
        return false;   // never written, as Guest's never is
    }
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (in.kind[s] > static_cast<uint8_t>(SeatKind::Computer)) {
            return false;
        }
    }
    return true;
}

bool LudoGame::playable(const Saved& in) {
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
            if (Ludo::playing(st, s) &&
                in.kind[s] == static_cast<uint8_t>(SeatKind::Empty)) {
                return false;
            }
        }
    }
    return true;
}

void LudoGame::adopt(const Saved& in) {
    /* A table's level is the one its host offered, and the host's computer
     * seats play at it, so it comes with the game. */
    level_ = in.level != 0 ? Ludo::Level::Normal : Ludo::Level::Easy;
    state_ = in.state;
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
}

bool LudoGame::restoreTable(AppContext& host) {
    Saved in{};
    host.loadConsoleBlob(SAVE_KEY, &in, sizeof(in));
    if (!readable(in)) {
        return false;   // no table was kept
    }
    if (in.net == 0 || !playable(in)) {
        /* A table that finished, or was ended, before the console was
         * restarted: there is nothing to carry on with, so it is done with. */
        host.clearConsoleBlob(SAVE_KEY);
        return false;
    }
    adopt(in);
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

bool LudoGame::restorePlayer(AppContext& host, bool resume) {
    setAside_ = false;
    Saved in{};
    host.loadBlob(SAVE_KEY, &in, sizeof(in));
    if (!readable(in)) {
        /* Nothing of this player's under our own key yet. Before the key was
         * ours a game was saved under one every nearby game shared: take a
         * game on this console from there if it is Ludo's. A table is not
         * taken -- tables are the console's now -- and nothing is ever
         * written back there. */
        in = Saved{};
        host.loadBlob(OLD_SHARED_KEY, &in, sizeof(in));
        if (!readable(in) || in.net != 0) {
            return false;
        }
    }
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        kind_[s] = static_cast<SeatKind>(in.kind[s]);
    }
    level_ = in.level != 0 ? Ludo::Level::Normal : Ludo::Level::Easy;
    if (in.net != 0 || !playable(in)) {
        return false;
    }
    if (!resume) {
        setAside_ = true;
        return false;
    }
    adopt(in);
    return true;
}

void LudoGame::closeTable(AppContext& host) {
    host.clearConsoleBlob(SAVE_KEY);
    leaveTable(host);
    mode_ = Mode::Lobby;
    confirmUntilMs_ = 0;
    confirmShown_ = false;
    /* The seats and the level in memory are the table's, and whoever sat down
     * at it may not be who is holding the console now. The lobby is the
     * active player's, so it is read back from their save -- and a game of
     * their own that the table took the place of stays there, untouched, for
     * the next time the screen is opened. */
    restorePlayer(host, false);
    lobbyStale_ = true;
    markFullDirty();
}

// ---- a console going quiet ----------------------------------------------------

void LudoGame::endTableByUs(AppContext& host) {
    /* Tell the others first, through the service's own ending, and every
     * console at the table leaves to its lobby too (pollTable). The ending
     * stays on the air after we leave, so a console that has not heard it
     * yet still will -- held, so that the next thing pressed in the lobby
     * cannot take it straight off. Used by End game and by the pause card
     * alike. */
    host.nearbyEnd(session_, Ludo::Net::nextPly(applied_), applied_);
    holdWord(host, millis());
    ended_ = true;
    closeTable(host);
    host.playSound(Sound::Select);
}

void LudoGame::dropChair(AppContext& host, uint8_t chair) {
    (void)host;
    if (role_ != Role::Host || chair >= chairCount_ || chair == selfChair_ ||
        chairDropped(chair)) {
        return;
    }
    /* Chosen now, said when the table can hear it -- see pollTable. From this
     * frame the chair is not waited for, so the pause lifts. */
    pendingDrop_ = chair;
}

/* The whole table pauses for one quiet console. Playing on around somebody
 * is how you get a game nobody can finish; and a seat that cannot ack the
 * last ply stalls canPublish for everyone anyway. The host alone is offered
 * "Play without", and only once the chair is Gone rather than merely Quiet:
 * fifteen seconds is a scan gap, forty-five is somebody who has left. Or
 * once it is Away -- heard, but no longer saying anything at this table: that
 * console has left it as surely as one out of range has, and will never ack
 * a ply again. A guest whose host has gone can only wait or end. */
bool LudoGame::offerPlayWithout() const {
    return role_ == Role::Host && (watch_.state() == NearbyWatch::State::Gone ||
                                   watch_.state() == NearbyWatch::State::Away);
}

bool LudoGame::updatePause(AppContext& host, const TouchPoint& touch, uint32_t now) {
    if (!net_ || mode_ != Mode::Play || phase_ == Phase::Over || ended_) {
        if (watch_.paused()) {
            watch_.reset();
            pausePainted_ = false;
            markFullDirty();
        }
        return false;
    }
    char ids[Ludo::Net::MAX_HUMANS][5];
    for (uint8_t c = 0; c < chairCount_; ++c) {
        copyId(ids[c], chairs_[c].id);
    }
    const NearbyWatch::State before = watch_.state();
    if (watch_.tickTable(host, ids, chairCount_, selfChair_, chairsNotWaitedFor(), now)) {
        if (watch_.state() != before && watch_.paused()) {
            sayWhoIsQuiet();
            /* A card already up says something else now, so it is painted
             * again -- and with "Play without" coming or going it is a
             * different size, so the board under it is too. */
            if (pausePainted_) {
                pausePainted_ = false;
                if (pauseExtraDrawn_ != offerPlayWithout()) {
                    markFullDirty();
                }
            }
        }
        markDirty();
    }
    if (watch_.resumed()) {
        host.playSound(Sound::Pop);
        pausePainted_ = false;
        /* Puts the message back to whose turn it is. Not during a hop, which
         * it would abandon: the hop does the same when it lands. */
        if (phase_ != Phase::Moving) {
            enterTurn(now, rollBonus_);
        } else {
            setMessage("");   // as a hop leaves it; nobody is quiet any more
        }
        markFullDirty();
        return false;
    }
    if (!watch_.cardShown()) return false;
    const bool offerDrop = offerPlayWithout();
    switch (watch_.press(boardRect(), touch, offerDrop)) {
        case NearbyWatch::Press::Wait:
            watch_.dismiss();
            pausePainted_ = false;
            host.playSound(Sound::Tap);
            markFullDirty();
            break;
        case NearbyWatch::Press::End:
            endTableByUs(host);
            break;
        case NearbyWatch::Press::Extra:
            dropChair(host, watch_.who());
            host.playSound(Sound::Select);
            pausePainted_ = false;
            markFullDirty();
            break;
        default:
            break;
    }
    return touch.justPressed;
}

void LudoGame::sayWhoIsQuiet() {
    const Chair& who = chairs_[watch_.who()];
    char line[sizeof(message_)];
    const char* what = watch_.state() == NearbyWatch::State::Gone   ? "is away"
                       : watch_.state() == NearbyWatch::State::Away ? "has left"
                                                                    : "is quiet";
    snprintf(line, sizeof(line), "%.9s %s", who.name[0] != 0 ? who.name : who.id, what);
    /* Asked on every frame of a pause, so only a change is a repaint. */
    if (messageSeat_ == Ludo::NO_SEAT && strcmp(line, message_) == 0) {
        return;
    }
    snprintf(message_, sizeof(message_), "%s", line);
    messageSeat_ = Ludo::NO_SEAT;
    markDirty();
}

void LudoGame::drawPause(Ui::Renderer& tft) {
    if (!net_ || mode_ != Mode::Play || phase_ == Phase::Over || ended_ || !watch_.cardShown()) {
        pausePainted_ = false;
        return;
    }
    const Rect area = boardRect();
    const Chair& who = chairs_[watch_.who()];
    const char* label = who.name[0] != 0 ? who.name : who.id;
    const bool offerDrop = offerPlayWithout();
    if (!pausePainted_) {
        char extra[24];
        snprintf(extra, sizeof(extra), "Play without %.9s", label);
        watch_.draw(tft, area, label, offerDrop ? extra : nullptr);
        pausePainted_ = true;
        pauseExtraDrawn_ = offerDrop;
        pauseSecondsDrawn_ = watch_.silentSeconds();
    } else if (pauseSecondsDrawn_ != watch_.silentSeconds()) {
        watch_.drawSeconds(tft, area, offerDrop);
        pauseSecondsDrawn_ = watch_.silentSeconds();
    }
}
