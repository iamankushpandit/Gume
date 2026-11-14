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

/* The table lobby: the consoles in the room and what each is doing, who is
 * asked to this console's table and who has sat down at it -- drawn here, and
 * answered here. (The lobby for a game on this console alone is in
 * LudoPanel.cpp.)
 *
 * A table is gathered by a host. It picks consoles and they are asked one at
 * a time, because only one invitation can be on the air at once; and while
 * that invitation is up every other console in the room can see that a table
 * is being gathered and sit down at it without having been asked. The host
 * seats whoever sits down while there is a chair, and can send anybody away.
 *
 * Everything is redrawn whole whenever what it says changes. It is a menu
 * tapped a few times, and the rows change every few seconds at most. */

namespace {

/* Up to four consoles as rows, then the computers and level the host is
 * offering, then Back and Start. */
constexpr int16_t ROW_H = 30;
constexpr int16_t ROW_PITCH = 34;
constexpr int16_t ROWS_TOP = 36;
constexpr uint8_t ROWS_SHOWN = 4;
constexpr Rect COMPUTERS_RECT{8, 172, 148, 26};
constexpr Rect TABLE_LEVEL_RECT{164, 172, 148, 26};
constexpr Rect BACK_RECT{8, 204, 110, 30};
constexpr Rect TABLE_START_RECT{202, 204, 110, 30};
static_assert(ROWS_TOP + (ROWS_SHOWN - 1) * ROW_PITCH + ROW_H + Ui::BUTTON_SHADOW_DY <=
                  COMPUTERS_RECT.y,
              "the console rows run into the computers chip");

/* How often the list of consoles is re-read. It changes every few seconds at
 * most, and reading it walks a table under a lock. */
constexpr uint32_t PEERS_REFRESH_MS = 1000;
/* How many looks in a row a guest must find its host offering no table
 * before it believes the table has closed. One advertisement caught between
 * two invitations is not that. */
constexpr uint8_t CLOSED_AFTER = 3;
/* How long "Started without you" stays up before the lobby takes over
 * saying it: as long as a notice in play. */
constexpr uint32_t LEFT_OUT_NOTICE_MS = 1200;

}   // namespace

Rect LudoGame::tableRowRect(uint8_t row) {
    return Rect{8, static_cast<int16_t>(ROWS_TOP + row * ROW_PITCH), 304, ROW_H};
}
Rect LudoGame::tableComputersRect() { return COMPUTERS_RECT; }
Rect LudoGame::tableLevelRect() { return TABLE_LEVEL_RECT; }
Rect LudoGame::tableBackRect() { return BACK_RECT; }
Rect LudoGame::tableStartRect() { return TABLE_START_RECT; }

// ---- what a row says ---------------------------------------------------------

LudoGame::TableRow LudoGame::tableRow(AppContext& host, const NearbySeat& p) {
    if (p.inviting && p.forThisGame) {
        return TableRow::InvitesUs;
    }
    /* Our own table next: whatever else a console we asked is doing, its row
     * is where that asking is taken back. */
    if (invitedIndex(p.deviceId) >= 0) {
        return joined(host, p.deviceId) ? TableRow::Joined : TableRow::Asked;
    }
    if (p.asking) {
        return TableRow::TheirTable;
    }
    if (p.inGame) {
        /* With nobody to name it may be waiting for THIS console to come
         * back to a game it has lost, so that row still asks. */
        return p.partner[0] != 0 ? TableRow::Busy : TableRow::AskBusy;
    }
    return TableRow::Ask;
}

void LudoGame::renderTable(AppContext& host) {
    Ui::Renderer& tft = host.display();
    tft.fillRect(0, TOP_BAR_HEIGHT, GAME_CANVAS_WIDTH,
                 GAME_CANVAS_HEIGHT - TOP_BAR_HEIGHT, Ui::bg());
    char line[40];

    if (role_ == Role::Guest) {
        const char* who = hostId_;
        for (uint8_t i = 0; i < peerCount_; ++i) {
            if (strncmp(peers_[i].deviceId, hostId_, 4) == 0 && peers_[i].name[0] != 0) {
                who = peers_[i].name;
            }
        }
        const bool leftOut = leftOut_;
        if (leftOut) {
            snprintf(line, sizeof(line), "%s's game", who);
        } else {
            snprintf(line, sizeof(line), "You joined %s's game.", who);
        }
        Ui::drawLabel(tft, Rect{8, 64, 304, 18}, line, Ui::text(), 2, Align::Center);
        Ui::drawLabel(tft, Rect{8, 90, 304, 18},
                      leftOut ? "Started without you." : "Waiting for it to start...",
                      leftOut ? Ui::warning() : Ui::muted(), 2, Align::Center);
        Ui::drawLabel(tft, Rect{8, 150, 304, 12},
                      host.batteryLow() ? "Battery low: a nearby game may not finish."
                                        : "Moves travel by Bluetooth. Anyone near hears them.",
                      host.batteryLow() ? Ui::warning() : Ui::muted(), 1, Align::Center);
        Ui::drawButton(tft, BACK_RECT, "Back", Ui::panel(), Ui::outline(), Ui::text());
        return;
    }

    const uint8_t rows = peerCount_ < ROWS_SHOWN ? peerCount_ : ROWS_SHOWN;
    if (rows == 0) {
        /* The empty case names who can fix it: switching the radio on is an
         * admin's job, playing once it is on is not. */
        Ui::drawLabel(tft, Rect{8, 70, 304, 18}, "Nobody nearby yet.", Ui::text(), 2,
                      Align::Center);
        Ui::drawLabel(tft, Rect{8, 96, 304, 12},
                      "An administrator can switch Beacon and Nearby on.", Ui::muted(), 1,
                      Align::Center);
    }
    for (uint8_t r = 0; r < rows; ++r) {
        const NearbySeat& p = peers_[r];
        const char* who = p.name[0] != 0 ? p.name : p.deviceId;
        const Rect row = tableRowRect(r);
        uint16_t fill = Ui::surface();
        uint16_t ink = Ui::text();
        switch (tableRow(host, p)) {
            case TableRow::InvitesUs:
                snprintf(line, sizeof(line), "%s invites you", who);
                fill = Ui::success();
                ink = TFT_BLACK;
                break;
            case TableRow::Joined:
                snprintf(line, sizeof(line), "%s joined", who);
                fill = Ui::panel();
                ink = Ui::success();
                break;
            case TableRow::Asked:
                snprintf(line, sizeof(line), "Asking %s...", who);
                fill = Ui::panel();
                break;
            case TableRow::TheirTable:
                snprintf(line, sizeof(line), "%s's table", who);
                break;
            case TableRow::Busy: {
                /* Not a button, so not drawn as one. No outline: a fill and
                 * an outline of one rectangle is what the d-pad's selector
                 * stops on. No shadow: nothing here lifts off the page. */
                snprintf(line, sizeof(line), "%s is playing %s", who,
                         p.partnerName[0] != 0 ? p.partnerName : p.partner);
                tft.fillRoundRect(row.x, row.y, row.w, row.h, 6, Ui::surface());
                tft.setTextColor(Ui::muted(), Ui::surface());
                tft.setTextDatum(MC_DATUM);
                tft.drawString(Ui::fitted(tft, line, static_cast<int16_t>(row.w - 12), 2),
                               row.x + row.w / 2, row.y + row.h / 2, 2);
                tft.setTextDatum(TL_DATUM);
                continue;
            }
            case TableRow::AskBusy:
                snprintf(line, sizeof(line), "Invite %s (in a game)", who);
                break;
            case TableRow::Ask:
                snprintf(line, sizeof(line), "Invite %s", who);
                break;
        }
        Ui::drawButton(tft, row, line, fill, Ui::outline(), ink);
    }
    if (rows > 0 && rows < ROWS_SHOWN) {
        /* It is a broadcast, and the screen that starts it says so -- unless
         * the battery is low, which outranks it: a console that dies
         * mid-game cannot tell anyone. */
        const bool low = host.batteryLow();
        Ui::drawLabel(tft,
                      Rect{8, static_cast<int16_t>(ROWS_TOP + rows * ROW_PITCH + 4), 304, 12},
                      low ? "Battery low: a nearby game may not finish."
                          : "Moves travel by Bluetooth. Anyone near hears them.",
                      low ? Ui::warning() : Ui::muted(), 1, Align::Center);
    }

    snprintf(line, sizeof(line), "Computers: %u", static_cast<unsigned>(computers_));
    Ui::drawButton(tft, COMPUTERS_RECT, line, Ui::surface(), Ui::outline(), Ui::text());
    Ui::drawButton(tft, TABLE_LEVEL_RECT,
                   level_ == Ludo::Level::Easy ? "Level: Easy" : "Level: Normal",
                   Ui::surface(), Ui::outline(), Ui::text());
    Ui::drawButton(tft, BACK_RECT, "Back", Ui::panel(), Ui::outline(), Ui::text());
    Ui::drawPagerButton(tft, TABLE_START_RECT, "Start", hostCanStart(host));
}

// ---- opening and leaving -------------------------------------------------------

void LudoGame::openTable(AppContext& host) {
    /* Whatever this console was advertising belongs to a game that is over.
     * An ending still being held is the exception: it stays up for the
     * consoles that have not heard it, and nothing of this table's goes on
     * the air until it has had its time (updateTable()). */
    if (!wordHeld_) {
        host.nearbyStop();
    }
    mode_ = Mode::Table;
    role_ = Role::None;
    net_ = false;
    ended_ = false;
    droppedChairs_ = 0;
    pendingDrop_ = Ludo::Net::MAX_HUMANS;
    watch_.reset();
    pausePainted_ = false;
    invitedCount_ = 0;
    inviteCursor_ = 0;
    nextInviteMs_ = 0;
    askedId_[0] = 0;
    turnedAwayCount_ = 0;
    closedCount_ = 0;
    startHeard_ = false;
    leftOut_ = false;
    peerCount_ = 0;
    peersAtMs_ = 0;
    tableSig_ = 0;
    refreshPeers(host);
    tableStale_ = true;
    markFullDirty();
}

void LudoGame::leaveTable(AppContext& host) {
    /* Our last word is not touched here: a console that has not yet heard the
     * final move, or our ending, can still pick it up. Whoever calls this
     * decides how long it stays -- see holdWord(). */
    (void)host;
    role_ = Role::None;
    invitedCount_ = 0;
    askedId_[0] = 0;
    turnedAwayCount_ = 0;
    backResumes_ = false;
    net_ = false;
    /* A pause belongs to the table it was measuring, and so does its card's
     * hold on the d-pad's selector. A table ended from another console while
     * the card was up left that hold in place, and in the lobby nothing but
     * what stood where the card had been could be reached with the keys. */
    watch_.reset();
    pausePainted_ = false;
}

void LudoGame::quitTable(AppContext& host, uint32_t now) {
    if (role_ == Role::Guest && !wordHeld_) {
        /* We had answered. Going silent would leave the host counting a
         * console that has gone, and able to start a game with it -- so say
         * that we left, the way a game is ended. */
        host.nearbyEnd(session_, 0, Ludo::Net::NOT_STARTED);
        holdWord(host, now);
    }
    leaveTable(host);
    mode_ = Mode::Lobby;
}

void LudoGame::sitAt(AppContext& host, const NearbySeat& seat) {
    /* Their session, their table. Anything of a table of our own is dropped
     * -- a console sits at one table -- and whoever had sat down at ours sees
     * it close. What goes on the air is our presence (guestWait()). */
    if (!wordHeld_) {
        host.nearbyStop();
    }
    role_ = Role::Guest;
    session_ = seat.session;
    copyId(hostId_, seat.deviceId);
    invitedCount_ = 0;
    askedId_[0] = 0;
    turnedAwayCount_ = 0;
    closedCount_ = 0;
    startHeard_ = false;
    leftOut_ = false;
    tableStale_ = true;
    host.playSound(Sound::Select);
    markDirty();
}

void LudoGame::tableGone(AppContext& host, uint32_t now, bool sayLeft, const char* note) {
    snprintf(lobbyNote_, sizeof(lobbyNote_), "%s", note);
    if (!sayLeft) {
        /* Nobody is counting this console, so there is nobody to tell. */
        if (!wordHeld_) {
            host.nearbyStop();
        }
        role_ = Role::None;
    }
    quitTable(host, now);
    lobbyStale_ = true;
    host.playSound(Sound::GameOver);
    markFullDirty();
}

// ---- the consoles in the room ---------------------------------------------------

void LudoGame::refreshPeers(AppContext& host) {
    NearbySeat fresh[MAX_PEERS];
    uint8_t count = 0;
    const uint8_t n = host.nearbySeatCount();
    for (uint8_t i = 0; i < n && count < MAX_PEERS; ++i) {
        if (host.nearbySeatAt(i, fresh[count])) {
            ++count;
        }
    }
    for (uint8_t i = 0; i < count; ++i) {
        peers_[i] = fresh[i];
    }
    peerCount_ = count;

    if (role_ == Role::Host) {
        seatWalkIns(host);
    } else if (role_ == Role::Guest && !leftOut_) {
        /* Is the table we sat down at still there? Its host says so for as
         * long as it is gathering -- its invitation, in our session, naming
         * us or somebody else -- and after that by its own word in the
         * session. A host that is heard saying neither has closed the table
         * or gone to another, and one not heard at all has gone. */
        bool heard = false;
        bool open = false;
        for (uint8_t i = 0; i < count; ++i) {
            const NearbySeat& p = peers_[i];
            if (sameId(p.deviceId, hostId_)) {
                heard = true;
                open = ((p.inviting && p.forThisGame) || p.asking) && p.session == session_;
            }
        }
        NearbyTurn t;
        if (open || host.nearbyTurnFrom(hostId_, session_, t)) {
            closedCount_ = 0;
        } else if ((heard || host.nearbyPeerSilentMs(hostId_) ==
                                 NearbyPlay::PEER_SILENT_UNKNOWN) &&
                   closedCount_ < CLOSED_AFTER) {
            ++closedCount_;
        }
    }

    /* What the rows would say, folded into one number, so the lobby repaints
     * when any row's meaning changes -- somebody answering, asking, starting
     * a game or being given a name -- and not merely when somebody arrives
     * or leaves. */
    uint32_t sig = 2166136261U ^ count;
    const auto fold = [&sig](const char* text) {
        for (; *text != 0; ++text) {
            sig = (sig ^ static_cast<uint8_t>(*text)) * 16777619U;
        }
        sig = (sig ^ 0xFFU) * 16777619U;
    };
    for (uint8_t i = 0; i < count; ++i) {
        const NearbySeat& p = peers_[i];
        fold(p.deviceId);
        fold(p.name);
        fold(p.partner);
        fold(p.partnerName);
        sig = (sig ^ static_cast<uint8_t>(tableRow(host, p))) * 16777619U;
    }
    if (sig != tableSig_) {
        tableSig_ = sig;
        tableStale_ = true;
        markDirty();
    }
}

// ---- a guest waiting for Start ---------------------------------------------------

bool LudoGame::guestWait(AppContext& host, uint32_t now) {
    if (leftOut_) {
        if (now - leftOutAtMs_ < LEFT_OUT_NOTICE_MS) {
            return false;
        }
        /* Said as a leaving, in case a table is counting this console after
         * all: one it could not make out the roster of is as stuck with us
         * as without. A table we were never at does not hear it. */
        tableGone(host, now, true, "Table started without you");
        return true;
    }
    if (closedCount_ >= CLOSED_AFTER) {
        char note[sizeof(lobbyNote_)];
        const char* who = hostId_;
        for (uint8_t i = 0; i < peerCount_; ++i) {
            if (sameId(peers_[i].deviceId, hostId_) && peers_[i].name[0] != 0) {
                who = peers_[i].name;
            }
        }
        snprintf(note, sizeof(note), "%s closed the table", who);
        tableGone(host, now, false, note);
        return true;
    }
    if (wordHeld_) {
        return false;   // our answer would replace an ending still being held
    }
    /* Our answer: presence in the host's session, not yet started. */
    host.nearbyPublish(session_, 0, Ludo::Net::PRESENCE, Ludo::Net::PRESENCE,
                       Ludo::Net::NOT_STARTED);
    if (guestTryStart(host)) {
        return true;
    }
    NearbyTurn t;
    if (!host.nearbyTurnFrom(hostId_, session_, t)) {
        return false;   // the host has said nothing in the session yet
    }
    Ludo::Net::Start st;
    if (!t.ended && t.ply == 0 && t.ack == 0 && Ludo::Net::decodeStart(t.from, t.to, st)) {
        /* Started, and nobody has moved. A console across the room may not
         * have been heard yet, so go on trying rosters -- for as long as a
         * console can say nothing at a table before it counts as away. */
        if (!startHeard_) {
            startHeard_ = true;
            startHeardMs_ = now;
        }
        if (now - startHeardMs_ < NearbyPlay::PEER_AWAY_MS) {
            return false;
        }
    }
    /* The table has moved on, or has been started long enough, and no roster
     * with this console in it fits the host's word. A table cannot play its
     * first roll until every console at it has acknowledged the start, so one
     * that has is one we are not at: the host sent us away, had no chair
     * left, or pressed Start before it had seen us sit down. */
    leftOut_ = true;
    leftOutAtMs_ = now;
    tableStale_ = true;
    markDirty();
    return false;
}

// ---- input -----------------------------------------------------------------------

void LudoGame::updateTable(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (now - peersAtMs_ >= PEERS_REFRESH_MS) {
        peersAtMs_ = now;
        refreshPeers(host);
        /* Two consoles that asked each other to their tables would each wait
         * on the other for ever. The one whose own tag is the greater gives
         * way and sits at the other's; the other does nothing. No clock is
         * involved, so the two cannot both give way or both hold out. */
        const char* self = host.nearbySelfId();
        for (uint8_t i = 0; role_ == Role::Host && i < peerCount_; ++i) {
            const NearbySeat& p = peers_[i];
            const int8_t at = invitedIndex(p.deviceId);
            if (p.inviting && p.forThisGame && at >= 0 && !invitedWalkIn_[at] &&
                strcmp(self, p.deviceId) > 0) {
                sitAt(host, p);
                break;
            }
        }
    }

    /* Neither an invitation nor an answer goes out while the ending of the
     * last table is still being held: either would replace it. */
    if (role_ == Role::Host && !wordHeld_) {
        askNext(host, now);
    } else if (role_ == Role::Guest && guestWait(host, now)) {
        return;
    }

    if (!touch.justPressed) {
        return;
    }
    if (tableBackRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        /* The way back takes back whatever this console was asking. */
        const bool resume = backResumes_;
        quitTable(host, now);
        if (!wordHeld_) {
            host.nearbyStop();   // an invitation of ours, when we were asking
        }
        host.playSound(Sound::Select);
        /* A table lobby that came up over the player's own game goes back to
         * that game, which is still in their save. */
        if (!(resume && restorePlayer(host, true))) {
            lobbyStale_ = true;
        }
        markFullDirty();
        return;
    }
    if (role_ == Role::Guest) {
        return;   // waiting for the host; Back is the only thing to press
    }
    if (tableComputersRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        computers_ = static_cast<uint8_t>((computers_ + 1) % (Ludo::Net::MAX_COMPUTERS + 1));
        tableStale_ = true;
        host.playSound(Sound::Tap);
        markDirty();
        return;
    }
    if (tableLevelRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        level_ = level_ == Ludo::Level::Easy ? Ludo::Level::Normal : Ludo::Level::Easy;
        tableStale_ = true;
        host.playSound(Sound::Tap);
        markDirty();
        return;
    }
    if (tableStartRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (hostCanStart(host)) {
            hostStart(host);
        } else {
            host.beepError();
        }
        return;
    }
    const uint8_t rows = peerCount_ < ROWS_SHOWN ? peerCount_ : ROWS_SHOWN;
    for (uint8_t r = 0; r < rows; ++r) {
        if (!tableRowRect(r).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        const NearbySeat& p = peers_[r];
        switch (tableRow(host, p)) {
            case TableRow::InvitesUs:
            case TableRow::TheirTable:
                /* Asked, or sitting down unasked: the same thing from here.
                 * The host seats whoever says they are at its table. */
                sitAt(host, p);
                return;
            case TableRow::Busy:
                return;   // says what that console is doing; takes no press
            case TableRow::Joined:
            case TableRow::Asked: {
                /* Tapped again: not at this table after all. It is noted, so
                 * that a console still saying it is here is not seated again
                 * for that; the oldest note goes when there are too many. */
                unseat(static_cast<uint8_t>(invitedIndex(p.deviceId)));
                if (!turnedAway(p.deviceId)) {
                    if (turnedAwayCount_ >= MAX_TURNED_AWAY) {
                        for (uint8_t j = 0; j + 1 < MAX_TURNED_AWAY; ++j) {
                            copyId(turnedAway_[j], turnedAway_[j + 1]);
                        }
                        --turnedAwayCount_;
                    }
                    copyId(turnedAway_[turnedAwayCount_++], p.deviceId);
                }
                if (sameId(askedId_, p.deviceId) && !wordHeld_) {
                    /* The invitation on the air names it: taken back now,
                     * and askNext() names whoever is still at the table. */
                    host.nearbyStop();
                    askedId_[0] = 0;
                }
                nextInviteMs_ = now;
                tableStale_ = true;
                host.playSound(Sound::Tap);
                markDirty();
                return;
            }
            case TableRow::AskBusy:
            case TableRow::Ask:
                break;
        }
        if (invitedCount_ >= Ludo::Net::MAX_HUMANS - 1) {
            host.beepError();   // four consoles is a full table
            return;
        }
        if (role_ == Role::None) {
            role_ = Role::Host;
            /* From the service: off the clock, so two consoles setting up
             * tables at once are unlikely to pick the same number, and never
             * the number of a table whose last word is still about. */
            session_ = host.nearbyFreshSession();
        }
        /* Picked by the host, which outranks having been sent away. */
        for (uint8_t j = 0; j < turnedAwayCount_; ++j) {
            if (sameId(turnedAway_[j], p.deviceId)) {
                for (uint8_t k = j; k + 1 < turnedAwayCount_; ++k) {
                    copyId(turnedAway_[k], turnedAway_[k + 1]);
                }
                --turnedAwayCount_;
                break;
            }
        }
        copyId(invited_[invitedCount_], p.deviceId);
        invitedWalkIn_[invitedCount_++] = false;
        nextInviteMs_ = now;   // ask straight away
        tableStale_ = true;
        host.playSound(Sound::Select);
        markDirty();
        return;
    }
}
