// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "LudoGame.h"

#include "engine/NearbyPlay.h"

/* Ludo across consoles: forming the table, and carrying each console's turns
 * in order. The table lobby itself is in LudoLobby.cpp, and the pause for a
 * console that has gone quiet is in LudoSave.cpp.
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

// ---- who is at the host's table -----------------------------------------------

namespace {

/* An invitation stays on the air until it is answered or replaced, and only
 * one can be up at once. So the host asks the consoles it picked in turn:
 * this is how long each is asked before the next one is. One that has
 * answered is passed over. */
constexpr uint32_t INVITE_ROTATE_MS = 6500;
/* How often to look again for somebody still to ask, once all are asked. */
constexpr uint32_t INVITE_RECHECK_MS = 1000;

}   // namespace

int8_t LudoGame::invitedIndex(const char* id) const {
    for (uint8_t i = 0; i < invitedCount_; ++i) {
        if (sameId(invited_[i], id)) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

void LudoGame::unseat(uint8_t index) {
    for (uint8_t j = index; j + 1 < invitedCount_; ++j) {
        copyId(invited_[j], invited_[j + 1]);
        invitedWalkIn_[j] = invitedWalkIn_[j + 1];
    }
    --invitedCount_;
}

bool LudoGame::turnedAway(const char* id) const {
    for (uint8_t i = 0; i < turnedAwayCount_; ++i) {
        if (sameId(turnedAway_[i], id)) {
            return true;
        }
    }
    return false;
}

bool LudoGame::joined(AppContext& host, const char* id) {
    NearbyTurn t;
    /* Answered, and still being heard. An answer is remembered for as long
     * as its console is in the room, so without the second half a console
     * switched off after answering would go on counting as at the table. */
    return host.nearbyTurnFrom(id, session_, t) && !t.ended && t.ply == 0 &&
           t.from == Ludo::Net::PRESENCE && t.to == Ludo::Net::PRESENCE &&
           host.nearbyPeerSilentMs(id) < NearbyPlay::PEER_QUIET_MS;
}

uint8_t LudoGame::joinedCount(AppContext& host) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < invitedCount_; ++i) {
        n = static_cast<uint8_t>(n + (joined(host, invited_[i]) ? 1 : 0));
    }
    return n;
}

bool LudoGame::hostCanStart(AppContext& host) {
    if (role_ != Role::Host) {
        return false;
    }
    const uint8_t people = static_cast<uint8_t>(1 + joinedCount(host));
    return people >= 2 && people + computers_ <= Ludo::SEATS;
}

void LudoGame::seatWalkIns(AppContext& host) {
    /* A console that sat down by itself and has got up again is forgotten,
     * not asked back: asking was never this console's idea. */
    for (uint8_t i = invitedCount_; i-- > 0;) {
        if (invitedWalkIn_[i] && !joined(host, invited_[i])) {
            unseat(i);
        }
    }
    /* Anybody saying they are at this table is seated, while there is a
     * chair -- except a console the host has sent away, which is not seated
     * again just for still saying so. */
    for (uint8_t i = 0; i < peerCount_ && invitedCount_ < Ludo::Net::MAX_HUMANS - 1; ++i) {
        const char* id = peers_[i].deviceId;
        if (invitedIndex(id) >= 0 || turnedAway(id) || !joined(host, id)) {
            continue;
        }
        copyId(invited_[invitedCount_], id);
        invitedWalkIn_[invitedCount_++] = true;
    }
}

void LudoGame::askNext(AppContext& host, uint32_t now) {
    if (host.nearbySelfId()[0] == 0) {
        askedId_[0] = 0;   // Nearby is off: nothing of ours is on the air
        return;
    }
    if (now < nextInviteMs_) {
        return;
    }
    nextInviteMs_ = now + INVITE_RECHECK_MS;
    if (invitedCount_ == 0) {
        /* Nobody is at this table or asked to it. An invitation left up
         * would be to a console the table has no chair for. */
        if (askedId_[0] != 0) {
            host.nearbyStop();
            askedId_[0] = 0;
        }
        return;
    }
    /* The next console the host picked that has not answered. One that sat
     * down by itself is never asked: if it has got up, it is forgotten the
     * next time the room is read. Nothing else of ours may be on the air
     * meanwhile: a turn would replace the invitation. */
    int8_t pick = -1;
    for (uint8_t k = 0; k < invitedCount_ && pick < 0; ++k) {
        const uint8_t i = static_cast<uint8_t>((inviteCursor_ + k) % invitedCount_);
        if (!invitedWalkIn_[i] && !joined(host, invited_[i])) {
            pick = static_cast<int8_t>(i);
        }
    }
    if (pick < 0) {
        /* Everybody has answered. The invitation stays up all the same,
         * naming one of them: it is how a console that was not asked sees
         * there is a table here to sit at. */
        if (askedId_[0] != 0 && invitedIndex(askedId_) >= 0) {
            return;
        }
        pick = 0;
    }
    inviteCursor_ = static_cast<uint8_t>(pick + 1);
    nextInviteMs_ = now + INVITE_ROTATE_MS;
    if (sameId(askedId_, invited_[pick])) {
        return;   // it is the one being asked already, and that stands
    }
    bool unused = false;   // who moves first comes from the seed, not the toss
    if (host.nearbyInvite(invited_[pick], session_, unused)) {
        copyId(askedId_, invited_[pick]);
    }
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
        /* Not a chair the table has played on without: that console is at no
         * table of ours any more, and its giving up on the one it remembers
         * must not end the one that went on. */
        if (c == selfChair_ || chairDropped(c)) {
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
            closeTable(host);
            host.playSound(Sound::GameOver);
            return;
        }
    }

    /* A table that has gone on without this console. Nobody at a table can
     * be more than a ply for each other console ahead of one that is still
     * in it -- each waits for every ack before it speaks again -- so a host
     * further ahead than that is not waiting for us: it played on without us
     * while we were out of range and has since replaced the word that said
     * so, or this console formed its table from a roster the host never
     * dealt. Either way no move for us will ever come. */
    NearbyTurn hostWord;
    if (role_ == Role::Guest && host.nearbyTurnFrom(hostId_, session_, hostWord) &&
        !hostWord.ended && hostWord.ack != Ludo::Net::NOT_STARTED &&
        Ludo::Net::atOrAfter(hostWord.ack, static_cast<uint8_t>(
                                               (applied_ + Ludo::Net::MAX_HUMANS) &
                                               Ludo::Net::PLY_MASK))) {
        snprintf(lobbyNote_, sizeof(lobbyNote_), "Table went on without you");
        host.nearbyStop();
        ended_ = true;
        closeTable(host);
        host.playSound(Sound::GameOver);
        return;
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
            snprintf(lobbyNote_, sizeof(lobbyNote_), "%.9s played on without you",
                     chairs_[c].name[0] != 0 ? chairs_[c].name : chairs_[c].id);
            host.nearbyStop();
            ended_ = true;
            closeTable(host);
            host.playSound(Sound::GameOver);
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
