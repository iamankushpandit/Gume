// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "LudoGame.h"

/* Ludo across consoles: forming the table, and carrying each console's turns
 * in order. The table lobby itself is in LudoLobby.cpp.
 *
 * Everything on the air goes through AppContext's nearby calls -- the same
 * invitation, the same numbered turn and the same ending Chess and Sea Battle
 * use -- and is spelled by Ludo::Net in LudoRules.h. Nothing here can put
 * anything else on the air, and nothing here carries a name: a console is its
 * four-hex-digit tag, and the owner's own label for it is looked up locally
 * and only ever drawn.
 *
 * It is a BROADCAST. Every console in range hears every move; only the ones at
 * the table act on them. The table lobby says so. */

// ---- who decides for a seat ---------------------------------------------

bool LudoGame::ownsSeat(uint8_t seat) const {
    if (!net_) {
        return true;
    }
    const uint8_t p = owner_[seat];
    if (p == Ludo::NO_SEAT) {
        return false;
    }
    /* A dropped chair's seat is a computer seat: the host's, like the rest of
     * them. Only once the takeover has been announced -- until then nobody
     * plays it, which is what keeps the host from moving for a seat the
     * others still think belongs to somebody. */
    if (p < chairCount_ && !chairDropped(p)) {
        return p == selfChair_;
    }
    return role_ == Role::Host;   // computer seats are played by the host
}

const char* LudoGame::ownerId(uint8_t seat) const {
    const uint8_t p = owner_[seat];
    return p < chairCount_ && !chairDropped(p) ? chairs_[p].id : hostId_;
}

const char* LudoGame::seatLabel(uint8_t seat) const {
    if (!net_) {
        return nullptr;
    }
    const uint8_t p = owner_[seat];
    if (p == Ludo::NO_SEAT) {
        return "";
    }
    if (p >= chairCount_ || chairDropped(p)) {
        return "CPU";
    }
    if (p == selfChair_) {
        return "You";
    }
    return chairs_[p].name[0] != 0 ? chairs_[p].name : chairs_[p].id;
}

// ---- forming the table -------------------------------------------------------

void LudoGame::hostStart(AppContext& host) {
    const char* self = host.nearbySelfId();
    if (self[0] == 0) {
        host.beepError();
        return;
    }
    char ids[Ludo::Net::MAX_HUMANS][5];
    uint8_t n = 0;
    copyId(ids[n++], self);
    for (uint8_t i = 0; i < invitedCount_ && n < Ludo::Net::MAX_HUMANS; ++i) {
        if (joined(host, invited_[i])) {
            copyId(ids[n++], invited_[i]);
        }
    }
    if (n < 2) {
        /* Whoever had answered went quiet between Start being offered and
         * its being pressed. A table of one is not a table. */
        host.beepError();
        return;
    }
    Ludo::Net::sortIds(ids, n);

    Ludo::Net::Start st;
    st.humans = n;
    st.computers = computers_ + n <= Ludo::SEATS ? computers_
                                                  : static_cast<uint8_t>(Ludo::SEATS - n);
    st.level = static_cast<uint8_t>(level_);
    st.rosterCheck = Ludo::Net::rosterCheck(ids, n);

    chairCount_ = n;
    for (uint8_t i = 0; i < n; ++i) {
        copyId(chairs_[i].id, ids[i]);
        chairs_[i].name[0] = 0;
        for (uint8_t k = 0; k < peerCount_; ++k) {
            if (sameId(peers_[k].deviceId, ids[i])) {
                snprintf(chairs_[i].name, sizeof(chairs_[i].name), "%s", peers_[k].name);
            }
        }
        if (sameId(ids[i], self)) {
            selfChair_ = i;
        }
    }
    copyId(hostId_, self);
    Ludo::Net::encodeStart(st, myFrom_, myTo_);
    myPly_ = 0;
    applied_ = 0;
    startNetGame(host, st);
}

bool LudoGame::guestTryStart(AppContext& host) {
    NearbyTurn t;
    if (!host.nearbyTurnFrom(hostId_, session_, t) || t.ended || t.ply != 0) {
        return false;
    }
    Ludo::Net::Start st;
    if (!Ludo::Net::decodeStart(t.from, t.to, st)) {
        return false;   // the host has not pressed Start yet
    }
    const char* self = host.nearbySelfId();
    if (self[0] == 0) {
        return false;
    }
    /* Who is at the table: the host, us, and consoles answering in this
     * session. The start word says how many and carries a check of which;
     * until what we can hear matches both, keep listening -- a console across
     * the room may simply not have been heard yet.
     *
     * More may be answering than the host seated: one it un-invited, one
     * that left, one it had stopped hearing. So the others are candidates,
     * and every choice of the right number of them is tried against the
     * check. The check is six bits, which a wrong choice passes one time in
     * sixty-four -- and a wrong roster is a different seed and a different
     * game -- so the table forms only when exactly one choice passes. */
    if (st.humans < 2 || st.humans > Ludo::Net::MAX_HUMANS) {
        return false;
    }
    char others[MAX_PEERS][5];
    uint8_t candidates = 0;
    for (uint8_t i = 0; i < peerCount_ && candidates < MAX_PEERS; ++i) {
        const char* id = peers_[i].deviceId;
        if (sameId(id, hostId_) || sameId(id, self)) {
            continue;
        }
        NearbyTurn pt;
        if (host.nearbyTurnFrom(id, session_, pt) && pt.ply == 0 && !pt.ended) {
            copyId(others[candidates++], id);
        }
    }
    const uint8_t need = static_cast<uint8_t>(st.humans - 2);
    const uint8_t n = st.humans;
    char ids[Ludo::Net::MAX_HUMANS][5] = {};
    uint8_t passed = 0;
    for (uint8_t pick = 0; pick < (1U << candidates); ++pick) {
        char roster[Ludo::Net::MAX_HUMANS][5] = {};
        uint8_t count = 0;
        copyId(roster[count++], hostId_);
        copyId(roster[count++], self);
        for (uint8_t i = 0; i < candidates; ++i) {
            if ((pick & (1U << i)) == 0) {
                continue;
            }
            if (count < Ludo::Net::MAX_HUMANS) {
                copyId(roster[count], others[i]);
            }
            ++count;   // counted past the table's size too: that is not a roster
        }
        if (count != static_cast<uint8_t>(need + 2)) {
            continue;
        }
        Ludo::Net::sortIds(roster, n);
        if (Ludo::Net::rosterCheck(roster, n) != st.rosterCheck) {
            continue;
        }
        if (++passed > 1) {
            return false;   // two rosters fit: wait until only one does
        }
        memcpy(ids, roster, sizeof(ids));
    }
    if (passed != 1) {
        return false;
    }

    chairCount_ = n;
    for (uint8_t i = 0; i < n; ++i) {
        copyId(chairs_[i].id, ids[i]);
        chairs_[i].name[0] = 0;
        for (uint8_t k = 0; k < peerCount_; ++k) {
            if (sameId(peers_[k].deviceId, ids[i])) {
                snprintf(chairs_[i].name, sizeof(chairs_[i].name), "%s", peers_[k].name);
            }
        }
        if (sameId(ids[i], self)) {
            selfChair_ = i;
        }
    }
    myPly_ = 0;
    myFrom_ = Ludo::Net::PRESENCE;
    myTo_ = Ludo::Net::PRESENCE;
    applied_ = 0;   // the start word, applied
    startNetGame(host, st);
    return true;
}

void LudoGame::startNetGame(AppContext& host, const Ludo::Net::Start& st) {
    char ids[Ludo::Net::MAX_HUMANS][5];
    for (uint8_t i = 0; i < chairCount_; ++i) {
        copyId(ids[i], chairs_[i].id);
    }
    seed_ = Ludo::Net::tableSeed(session_, ids, chairCount_);
    const uint8_t mask = Ludo::Net::deal(seed_, st.humans, st.computers, owner_);
    level_ = st.level != 0 ? Ludo::Level::Normal : Ludo::Level::Easy;
    Ludo::reset(state_, mask, Ludo::Net::firstSeat(seed_, mask));
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        for (uint8_t t = 0; t < Ludo::TOKENS; ++t) {
            shown_[s][t] = state_.pos[s][t];
        }
    }
    net_ = true;
    ended_ = false;
    droppedChairs_ = 0;
    pendingDrop_ = Ludo::Net::MAX_HUMANS;
    watch_.reset();
    pausePainted_ = false;
    face_ = 0;
    mode_ = Mode::Play;
    confirmUntilMs_ = 0;
    wordHeld_ = false;   // this table's word is the one on the air now
    host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
    enterTurn(millis(), false);
    host.playSound(Sound::Select);
    saveGame(host);
    /* Whether that was kept is how begin() later knows whose table this is. */
    tableSaid_ = saveSays(host);
    markFullDirty();
}

// ---- playing ---------------------------------------------------------------------

bool LudoGame::canPublish(AppContext& host) {
    const uint8_t skip = chairsNotWaitedFor();
    for (uint8_t c = 0; c < chairCount_; ++c) {
        if (c == selfChair_ || (skip & (1U << c)) != 0) {
            continue;   // a console played on without is not waited for
        }
        NearbyTurn t;
        if (!host.nearbyTurnFrom(chairs_[c].id, session_, t) ||
            !Ludo::Net::atOrAfter(t.ack, myPly_)) {
            return false;
        }
    }
    return true;
}

void LudoGame::publishPly(AppContext& host, uint8_t seat, uint8_t code) {
    if (ended_) {
        return;
    }
    myPly_ = Ludo::Net::nextPly(applied_);
    myFrom_ = Ludo::Net::moveFrom(seat);
    myTo_ = code;
    applied_ = myPly_;
    host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
}

void LudoGame::pollTable(AppContext& host, uint32_t now) {
    if (ended_) {
        return;   // our word says we stopped; nothing may replace it
    }
    /* Every frame: unchanged values do not touch the radio, and our word has
     * to stay on the air until everyone has it. */
    host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);

    for (uint8_t c = 0; c < chairCount_; ++c) {
        if (c == selfChair_) {
            continue;
        }
        NearbyTurn t;
        if (host.nearbyTurnFrom(chairs_[c].id, session_, t) && t.ended) {
            /* Somebody stopped. The service owns what that looks like on the
             * air; this only reads the flag. One console leaving ends the game
             * for the table, because a seat nobody plays stops everyone -- so
             * every console goes back to its lobby, and the lobby says who
             * ended it rather than leaving a young player to wonder where the game
             * went. Our own turn comes off the air: this console is in no game
             * now, and the ending we heard is carried by the one that sent it. */
            snprintf(lobbyNote_, sizeof(lobbyNote_), "%s ended the game",
                     chairs_[c].name[0] != 0 ? chairs_[c].name : chairs_[c].id);
            host.nearbyStop();
            ended_ = true;
            leaveTable(host);
            mode_ = Mode::Lobby;
            confirmUntilMs_ = 0;
            lobbyStale_ = true;
            host.playSound(Sound::GameOver);
            saveGame(host);
            markFullDirty();
            return;
        }
    }

    /* A takeover we have chosen but not yet said: it goes on the air as the
     * next ply once the consoles still at the table have acked the last one.
     * Until it is said the seat is nobody's (ownsSeat), so no move for it can
     * get ahead of the word that makes it ours.
     *
     * Only between turns. Said during a hop it would abandon the hop, and
     * the token would stay drawn part-way; said with a roll of our own in
     * hand, the move that follows would replace it before anyone had it. */
    if (pendingDrop_ < Ludo::Net::MAX_HUMANS && phase_ == Phase::Roll && canPublish(host)) {
        const uint8_t chair = pendingDrop_;
        pendingDrop_ = Ludo::Net::MAX_HUMANS;
        droppedChairs_ = static_cast<uint8_t>(droppedChairs_ | (1U << chair));
        for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
            if (owner_[s] == chair) {
                myPly_ = Ludo::Net::nextPly(applied_);
                myFrom_ = Ludo::Net::takeoverFrom(s);
                myTo_ = 0;
                applied_ = myPly_;
                host.nearbyPublish(session_, myPly_, myFrom_, myTo_, applied_);
            }
        }
        seatsStale_ = true;
        enterTurn(now, false);   // the seat may be the one to move
        saveGame(host);
        markDirty();
        return;
    }

    /* The host playing on without somebody. Only the host's word counts, and
     * only as the next ply -- it is ordered like a move so that a move for the
     * taken seat cannot be read before the word that changed whose it is.
     * Not during a hop, for the same reason as above: the word stays on the
     * air, and is still the next ply when the token has landed. */
    for (uint8_t c = 0; phase_ != Phase::Moving && c < chairCount_; ++c) {
        if (c == selfChair_ || !sameId(chairs_[c].id, hostId_)) {
            continue;
        }
        NearbyTurn t;
        if (!host.nearbyTurnFrom(chairs_[c].id, session_, t) || t.ended ||
            !Ludo::Net::isTakeover(t.from) || t.ply != Ludo::Net::nextPly(applied_)) {
            continue;
        }
        const uint8_t seat = static_cast<uint8_t>(t.from - Ludo::Net::TAKEOVER_BASE);
        const uint8_t chair = seat < Ludo::SEATS ? owner_[seat] : Ludo::NO_SEAT;
        if (chair >= chairCount_) {
            continue;   // not a chair: nothing to take
        }
        if (chair == selfChair_) {
            /* Our own seat. We were out of range long enough for the host to
             * play on without us, and there is no way back into a seat a
             * computer now holds -- so back to the lobby, told why, the same
             * way an ending is. Our word comes off the air: we are in no
             * game now. */
            snprintf(lobbyNote_, sizeof(lobbyNote_), "%s played on without you",
                     chairs_[c].name[0] != 0 ? chairs_[c].name : chairs_[c].id);
            host.nearbyStop();
            ended_ = true;
            leaveTable(host);
            mode_ = Mode::Lobby;
            confirmUntilMs_ = 0;
            lobbyStale_ = true;
            host.playSound(Sound::GameOver);
            saveGame(host);
            markFullDirty();
            return;
        }
        droppedChairs_ = static_cast<uint8_t>(droppedChairs_ | (1U << chair));
        applied_ = t.ply;
        seatsStale_ = true;
        enterTurn(now, false);
        saveGame(host);
        markDirty();
        return;
    }

    if (phase_ != Phase::Roll || state_.over) {
        return;
    }
    const uint8_t seat = state_.turn;
    if (ownsSeat(seat)) {
        return;
    }
    NearbyTurn t;
    if (!host.nearbyTurnFrom(ownerId(seat), session_, t)) {
        return;
    }
    /* The ply we expect, for the seat whose turn it is, and consistent with
     * the die this console computed. All three, or nothing happens -- see
     * Ludo::Net::accept(). */
    if (t.ply != Ludo::Net::nextPly(applied_) || t.from != Ludo::Net::moveFrom(seat) ||
        !Ludo::Net::accept(state_, seed_, t.to)) {
        return;
    }
    netCode_ = t.to;
    applied_ = t.ply;
    doRoll(host, now);
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
    holdWord(millis());
    ended_ = true;
    leaveTable(host);
    watch_.reset();
    pausePainted_ = false;
    mode_ = Mode::Lobby;
    confirmUntilMs_ = 0;
    confirmShown_ = false;
    lobbyStale_ = true;
    host.playSound(Sound::Select);
    saveGame(host);
    markFullDirty();
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
 * fifteen seconds is a scan gap, forty-five is somebody who has left. A guest
 * whose host has gone can only wait or end. */
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
    const bool offerDrop = role_ == Role::Host && watch_.state() == NearbyWatch::State::Gone;
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
    snprintf(line, sizeof(line), "%.9s %s", who.name[0] != 0 ? who.name : who.id,
             watch_.state() == NearbyWatch::State::Gone ? "is away" : "is quiet");
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
    const bool offerDrop = role_ == Role::Host && watch_.state() == NearbyWatch::State::Gone;
    if (!pausePainted_) {
        char extra[24];
        snprintf(extra, sizeof(extra), "Play without %.9s", label);
        watch_.draw(tft, area, label, offerDrop ? extra : nullptr);
        pausePainted_ = true;
        pauseSecondsDrawn_ = watch_.silentSeconds();
    } else if (pauseSecondsDrawn_ != watch_.silentSeconds()) {
        watch_.drawSeconds(tft, area, offerDrop);
        pauseSecondsDrawn_ = watch_.silentSeconds();
    }
}
