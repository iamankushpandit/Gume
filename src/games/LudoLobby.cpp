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

/* The lobby: who sits in each seat, and how hard the computer plays -- drawn
 * here, and answered here too (updateLobby(), at the end).
 *
 * Four chips, one per colour, each cycling Empty -> Player -> Computer on a
 * tap; the level toggle; a hint that says why Start is greyed when it is; and
 * Start. Everything here is redrawn whole on any change -- the lobby is a
 * handful of fixed rectangles, each painted by an idempotent function, so
 * there is nothing to be gained by tracking which one changed.
 *
 * The table lobby -- the consoles in the room, who is asked and who has
 * answered -- is the second half of this file. */

namespace {

/* Chips sit in the same corners as the yards they stand for. */
constexpr int16_t CHIP_W = 148;
constexpr int16_t CHIP_H = 44;
/* drawLabel() puts left-aligned text at the rect's top, so this is the 16px
 * line centred on the level buttons' 30px row rather than the row itself. */
constexpr Rect LEVEL_LABEL{8, 145, 100, 16};
constexpr Rect HINT_RECT{8, 176, 304, 18};
/* Start plays on this console; Nearby opens the table of consoles. Side by
 * side and the same size, because they are the two ways to begin. */
constexpr Rect START_RECT{60, 198, 120, 34};
constexpr Rect NEARBY_RECT{192, 198, 120, 34};
static_assert(START_RECT.y + START_RECT.h + Ui::BUTTON_SHADOW_DY <= GAME_CANVAS_HEIGHT,
              "Start's shadow falls off the canvas");
static_assert(START_RECT.x + START_RECT.w + Ui::BUTTON_SHADOW_DX < NEARBY_RECT.x,
              "Start and Nearby overlap");

/* The table lobby: up to four consoles as rows, then the computers and level
 * the host is offering, then Back and Start. */
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
/* An invitation is on the air for the service's poke time (six seconds), and
 * only one can be at once -- a second replaces the first. So the host asks one
 * console at a time, and moves on when that one has had its turn. */
constexpr uint32_t INVITE_ROTATE_MS = 6500;
/* How often to look again for somebody still to invite, once all are asked. */
constexpr uint32_t INVITE_RECHECK_MS = 1000;

}   // namespace

Rect LudoGame::seatChipRect(uint8_t seat) {
    /* Laid out like the yards: Red top left, Green top right, Yellow bottom
     * right, Blue bottom left -- so a young player finds a colour where the board
     * keeps it. */
    const int16_t col = (seat == 1 || seat == 2) ? 1 : 0;
    const int16_t row = seat >= 2 ? 1 : 0;
    return Rect{static_cast<int16_t>(8 + col * (CHIP_W + 8)),
                static_cast<int16_t>(38 + row * (CHIP_H + 6)), CHIP_W, CHIP_H};
}

Rect LudoGame::levelRect(uint8_t level) {
    return Rect{static_cast<int16_t>(112 + level * 100), 138, 94, 30};
}

Rect LudoGame::startRect() { return START_RECT; }
Rect LudoGame::nearbyRect() { return NEARBY_RECT; }
Rect LudoGame::tableRowRect(uint8_t row) {
    return Rect{8, static_cast<int16_t>(ROWS_TOP + row * ROW_PITCH), 304, ROW_H};
}
Rect LudoGame::tableComputersRect() { return COMPUTERS_RECT; }
Rect LudoGame::tableLevelRect() { return TABLE_LEVEL_RECT; }
Rect LudoGame::tableBackRect() { return BACK_RECT; }
Rect LudoGame::tableStartRect() { return TABLE_START_RECT; }

void LudoGame::drawSeatChip(Ui::Renderer& tft, uint8_t seat) const {
    const Rect r = seatChipRect(seat);
    const bool empty = kind_[seat] == SeatKind::Empty;
    const uint16_t fill = empty ? Ui::surface() : seatColour(seat);
    const uint16_t text = empty ? Ui::muted() : seatText(seat);
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    const int16_t tx = static_cast<int16_t>(r.x + 20);
    const int16_t ty = static_cast<int16_t>(r.y + r.h / 2);
    tft.fillCircle(tx, ty, 12, paperColour());
    drawToken(tft, tx, ty, seat, 8, 1);
    static const char* const KINDS[3] = {"Empty", "Player", "Computer"};
    tft.setTextColor(text);
    tft.drawString(seatName(seat), r.x + 40, r.y + 5, 2);
    tft.drawString(KINDS[static_cast<uint8_t>(kind_[seat])], r.x + 40, r.y + 23, 2);
}

void LudoGame::drawLevel(Ui::Renderer& tft) const {
    static const char* const LEVELS[2] = {"Easy", "Normal"};
    Ui::drawLabel(tft, LEVEL_LABEL, "Computer", Ui::text(), 2, Align::Left);
    for (uint8_t lv = 0; lv < 2; ++lv) {
        const Rect r = levelRect(lv);
        const bool on = static_cast<uint8_t>(level_) == lv;
        tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
        /* The chosen one inverted, which has contrast in every theme without
         * borrowing a colour that means something else. */
        Ui::drawButton(tft, r, LEVELS[lv], on ? Ui::text() : Ui::surface(), Ui::outline(),
                       on ? Ui::bg() : Ui::text());
    }
}

void LudoGame::drawLobbyHint(Ui::Renderer& tft) const {
    tft.fillRect(HINT_RECT.x, HINT_RECT.y, HINT_RECT.w, HINT_RECT.h, Ui::bg());
    if (lobbyNote_[0] != 0) {
        /* Why the game just vanished. Outranks everything until tapped. */
        Ui::drawLabel(tft, HINT_RECT, lobbyNote_, Ui::warning(), 2, Align::Center);
        return;
    }
    if (inviteWaiting_) {
        /* The notice announced it and has gone; this is where it is still
         * answerable from. */
        Ui::drawLabel(tft, HINT_RECT, "Invitation waiting: tap Nearby", Ui::success(), 2,
                      Align::Center);
        return;
    }
    uint8_t seats = 0;
    bool player = false;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        seats = static_cast<uint8_t>(seats + (kind_[s] != SeatKind::Empty ? 1 : 0));
        player = player || kind_[s] == SeatKind::Player;
    }
    const char* hint = seats < 2 ? "Two seats are needed to play"
                       : !player ? "Someone has to be a Player"
                                 : "Tap a seat to change who sits there";
    Ui::drawLabel(tft, HINT_RECT, hint, canStart() ? Ui::muted() : Ui::warning(), 2,
                  Align::Center);
}

void LudoGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        drawSeatChip(tft, s);
    }
    drawLevel(tft);
    drawLobbyHint(tft);
    for (const Rect& r : {START_RECT, NEARBY_RECT}) {
        tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
    }
    Ui::drawPagerButton(tft, START_RECT, "Start", canStart());
    Ui::drawButton(tft, NEARBY_RECT, "Nearby", inviteWaiting_ ? Ui::success() : Ui::panel(),
                   Ui::outline(), inviteWaiting_ ? static_cast<uint16_t>(TFT_BLACK) : Ui::text());
}

/* The table lobby, drawn whole whenever what it says changes. It is a menu
 * tapped a few times, and the rows change every few seconds at most. */
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
        snprintf(line, sizeof(line), "You joined %s's game.", who);
        Ui::drawLabel(tft, Rect{8, 64, 304, 18}, line, Ui::text(), 2, Align::Center);
        Ui::drawLabel(tft, Rect{8, 90, 304, 18}, "Waiting for it to start...", Ui::muted(),
                      2, Align::Center);
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
        bool invited = false;
        for (uint8_t i = 0; i < invitedCount_; ++i) {
            invited = invited || strncmp(invited_[i], p.deviceId, 4) == 0;
        }
        uint16_t fill = Ui::surface();
        uint16_t ink = Ui::text();
        if (p.inviting && p.forThisGame) {
            snprintf(line, sizeof(line), "%s invites you", who);
            fill = Ui::success();
            ink = TFT_BLACK;
        } else if (invited && joined(host, p.deviceId)) {
            snprintf(line, sizeof(line), "%s joined", who);
            fill = Ui::panel();
            ink = Ui::success();
        } else if (invited) {
            snprintf(line, sizeof(line), "Asking %s...", who);
            fill = Ui::panel();
        } else {
            snprintf(line, sizeof(line), "Invite %s", who);
        }
        Ui::drawButton(tft, tableRowRect(r), line, fill, Ui::outline(), ink);
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

void LudoGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    /* An invitation to Ludo is announced in the header by the service; the
     * lobby says where to answer it. Re-read on the peer cadence, not every
     * frame. */
    const uint32_t now = millis();
    if (now - peersAtMs_ >= 1000) {
        peersAtMs_ = now;
        NearbySeat seat;
        const bool waiting = host.nearbyInviteForUs(seat) && seat.forThisGame;
        if (waiting != inviteWaiting_) {
            inviteWaiting_ = waiting;
            lobbyStale_ = true;
            markDirty();
        }
    }
    if (!touch.justPressed) {
        return;
    }
    if (lobbyNote_[0] != 0) {
        /* "A4F2 ended the game" has been seen; any tap retires it. */
        lobbyNote_[0] = 0;
        lobbyStale_ = true;
        markDirty();
    }
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        if (seatChipRect(s).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            /* Empty -> Player -> Computer -> Empty: one tap per step, and the
             * chip says what it is now rather than what a tap will do. */
            kind_[s] = static_cast<SeatKind>((static_cast<uint8_t>(kind_[s]) + 1) % 3);
            lobbyStale_ = true;
            host.playSound(Sound::Tap);
            saveGame(host);
            markDirty();
            return;
        }
    }
    for (uint8_t lv = 0; lv < 2; ++lv) {
        if (levelRect(lv).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            level_ = static_cast<Ludo::Level>(lv);
            lobbyStale_ = true;
            host.playSound(Sound::Tap);
            saveGame(host);
            markDirty();
            return;
        }
    }
    if (nearbyRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.playSound(Sound::Select);
        openTable(host);
        return;
    }
    if (startRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (canStart()) {
            startGame(host);
        } else {
            host.beepError();
        }
    }
}

// ---- the table lobby ---------------------------------------------------------

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
        holdWord(now);
    }
    leaveTable(host);
    mode_ = Mode::Lobby;
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

    /* What the rows would say, folded into one number, so the lobby repaints
     * when a row's meaning changes -- somebody answering, or starting to
     * invite us -- and not merely when somebody arrives or leaves. */
    uint32_t sig = 2166136261U ^ count;
    for (uint8_t i = 0; i < count; ++i) {
        for (uint8_t c = 0; c < 4; ++c) {
            sig = (sig ^ static_cast<uint8_t>(peers_[i].deviceId[c])) * 16777619U;
        }
        const bool mine = role_ == Role::Host && joined(host, peers_[i].deviceId);
        sig = (sig ^ ((peers_[i].inviting && peers_[i].forThisGame) ? 1U : 0U) ^
               (mine ? 2U : 0U)) * 16777619U;
    }
    if (sig != tableSig_) {
        tableSig_ = sig;
        tableStale_ = true;
        markDirty();
    }
}

void LudoGame::updateTable(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (now - peersAtMs_ >= PEERS_REFRESH_MS) {
        peersAtMs_ = now;
        refreshPeers(host);
    }

    /* Neither an invitation nor an answer goes out while the ending of the
     * last table is still being held: either would replace it. */
    if (role_ == Role::Host && !wordHeld_ && now >= nextInviteMs_) {
        /* Ask the next console that has not answered. Nothing else of ours may
         * be on the air meanwhile: a turn would replace the invitation. */
        nextInviteMs_ = now + INVITE_RECHECK_MS;
        for (uint8_t k = 0; k < invitedCount_; ++k) {
            const uint8_t i = static_cast<uint8_t>((inviteCursor_ + k) % invitedCount_);
            if (joined(host, invited_[i])) {
                continue;
            }
            bool unused = false;   // who moves first comes from the seed, not the toss
            host.nearbyInvite(invited_[i], session_, unused);
            inviteCursor_ = static_cast<uint8_t>(i + 1);
            nextInviteMs_ = now + INVITE_ROTATE_MS;
            break;
        }
    } else if (role_ == Role::Guest && !wordHeld_) {
        /* Our answer: presence in the host's session, not yet started. */
        host.nearbyPublish(session_, 0, Ludo::Net::PRESENCE, Ludo::Net::PRESENCE,
                           Ludo::Net::NOT_STARTED);
        if (guestTryStart(host)) {
            return;
        }
    }

    if (!touch.justPressed) {
        return;
    }
    if (tableBackRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        quitTable(host, now);
        if (!wordHeld_) {
            host.nearbyStop();   // an invitation of ours, when we were asking
        }
        lobbyStale_ = true;
        host.playSound(Sound::Select);
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
    const uint8_t rows = peerCount_ < 4 ? peerCount_ : 4;
    for (uint8_t r = 0; r < rows; ++r) {
        if (!tableRowRect(r).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        const NearbySeat& p = peers_[r];
        if (p.inviting && p.forThisGame) {
            /* Joining: their session, their table. Any invitations of our own
             * are dropped -- a console sits at one table. */
            if (!wordHeld_) {
                host.nearbyStop();
            }
            role_ = Role::Guest;
            session_ = p.session;
            copyId(hostId_, p.deviceId);
            invitedCount_ = 0;
            tableStale_ = true;
            host.playSound(Sound::Select);
            markDirty();
            return;
        }
        for (uint8_t i = 0; i < invitedCount_; ++i) {
            if (sameId(invited_[i], p.deviceId)) {
                /* Tapped again: not invited after all. */
                for (uint8_t j = i; j + 1 < invitedCount_; ++j) {
                    copyId(invited_[j], invited_[j + 1]);
                }
                --invitedCount_;
                tableStale_ = true;
                host.playSound(Sound::Tap);
                markDirty();
                return;
            }
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
        copyId(invited_[invitedCount_++], p.deviceId);
        nextInviteMs_ = now;   // ask straight away
        tableStale_ = true;
        host.playSound(Sound::Select);
        markDirty();
        return;
    }
}
