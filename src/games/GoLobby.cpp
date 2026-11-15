// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "GoGame.h"

#include <string.h>

/* The Go lobby: every console in the room and what it is doing, drawn and
 * answered. What a press on a row does -- inviting, accepting, taking an
 * invitation back -- and the game that follows are GoNet.cpp's. */

namespace {

constexpr uint32_t SEATS_REFRESH_MS = 1000;

/* Would a lobby row for `a` read as the one painted for `b`? Everything a
 * row is built from, so a console that starts asking, sits down to a game or
 * is given a name repaints its row -- not only one that arrives or leaves. */
bool sameRow(const NearbySeat& a, const NearbySeat& b) {
    return strcmp(a.deviceId, b.deviceId) == 0 && strcmp(a.name, b.name) == 0 &&
           a.inviting == b.inviting && a.forThisGame == b.forThisGame &&
           (!a.inviting || a.session == b.session) && a.asking == b.asking &&
           a.inGame == b.inGame && strcmp(a.partner, b.partner) == 0 &&
           strcmp(a.partnerName, b.partnerName) == 0;
}

/* A row with nothing to press, so nothing that looks pressable: a plain fill
 * with no outline and no shadow, and muted words. A fill and an outline of
 * one rectangle is also what the d-pad's selector stops on, and it must not
 * stop here. */
void drawPlainRow(Ui::Renderer& tft, const Rect& r, const char* label) {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, Ui::cornerRadius(), Ui::surface());
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::muted(), Ui::surface());
    tft.drawString(Ui::fitted(tft, label, static_cast<int16_t>(r.w - 12), 2), r.x + r.w / 2,
                   r.y + r.h / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

}   // namespace

// ---- the lobby ----------------------------------------------------------------------

GoGame::Row GoGame::rowFor(const NearbySeat& seat) {
    if (seat.inviting && seat.forThisGame) {
        /* The game it offers is written in its session number. One this
         * console cannot play is shown and cannot be accepted. */
        if ((seat.session & SETTING_RULES) >= Go::RULES_COUNT) return Row::NoRules;
        if ((seat.session & SETTING_BIG) != 0 && !BIG_BOARD_AVAILABLE) return Row::TooBig;
        return Row::Invites;
    }
    if (seat.asking) return Row::Asking;
    if (seat.inGame && seat.partner[0] != 0) return Row::Playing;
    /* In a game, and we cannot tell whose -- it may be waiting on this very
     * console -- so it can still be asked: the banner on its screen says
     * somebody is. */
    if (seat.inGame) return Row::PlayBusy;
    return Row::Play;
}

void GoGame::rowLabel(const NearbySeat& seat, Row row, char* out, size_t len) {
    const char* who = seat.name[0] != 0 ? seat.name : seat.deviceId;
    const char* partner = seat.partnerName[0] != 0 ? seat.partnerName : seat.partner;
    switch (row) {
        case Row::Invites:
            /* With the game it would be: the answer plays their rules. */
            snprintf(out, len, "%s invites you: %s%s", who,
                     Go::rulesName(static_cast<Go::Rules>(seat.session & SETTING_RULES)),
                     (seat.session & SETTING_BIG) != 0 ? ", 19x19" : "");
            break;
        case Row::TooBig:
            snprintf(out, len, "%s: 19x19, needs a bigger screen", who);
            break;
        case Row::NoRules:
            snprintf(out, len, "%s: rules this console lacks", who);
            break;
        case Row::Asking:
            snprintf(out, len, "%s is asking %s", who, partner);
            break;
        case Row::Playing:
            snprintf(out, len, "%s is playing %s", who, partner);
            break;
        case Row::PlayBusy:
            snprintf(out, len, "Play %s (in a game)", who);
            break;
        case Row::Play:
            snprintf(out, len, "Play %s", who);
            break;
    }
}

void GoGame::refreshSeats(AppContext& host) {
    seatsAtMs_ = millis();
    NearbySeat fresh[LOBBY_SEATS];
    uint8_t count = 0;
    const uint8_t n = host.nearbySeatCount();
    /* Whoever is asking us goes first: with more consoles in the room than
     * rows, an invitation must not be the one left off. */
    for (uint8_t pass = 0; pass < 2; ++pass) {
        for (uint8_t i = 0; i < n && count < LOBBY_SEATS; ++i) {
            NearbySeat seat;
            if (!host.nearbySeatAt(i, seat)) continue;
            if ((seat.inviting && seat.forThisGame) != (pass == 0)) continue;
            fresh[count++] = seat;
        }
    }
    bool changed = count != seatCount_;
    for (uint8_t i = 0; !changed && i < count; ++i) {
        changed = !sameRow(fresh[i], seats_[i]);
    }
    for (uint8_t i = 0; i < count; ++i) seats_[i] = fresh[i];
    seatCount_ = count;
    if (changed) {
        lobbyStale_ = true;
        markDirty();
    }
}

void GoGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    if (millis() - seatsAtMs_ >= SEATS_REFRESH_MS) {
        refreshSeats(host);
    }

    if (!touch.justPressed) {
        return;
    }
    if (lobbyNote_[0] != 0) {
        lobbyNote_[0] = 0;   // "A4F2 ended the game" has been read
        lobbyStale_ = true;
        markDirty();
    }
    /* The chips. Rules cycles through all five; Level toggles; Board, where
     * it is shown, toggles. Saved so they come back. */
    const uint8_t chips = BIG_BOARD_AVAILABLE ? 3 : 2;
    for (uint8_t i = 0; i < chips; ++i) {
        if (!lobbyChipRect(i, chips).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        if (i == 0) {
            rules_ = static_cast<Go::Rules>((static_cast<uint8_t>(rules_) + 1) % Go::RULES_COUNT);
        } else if (i == 1) {
            level_ = level_ == Go::Level::Easy ? Go::Level::Medium : Go::Level::Easy;
        } else {
            boardSize_ = boardSize_ > 9 ? 9 : 19;
        }
        host.playSound(Sound::Tap);
        saveChips(host);
        lobbyStale_ = true;
        markDirty();
        return;
    }
    /* The two rows for playing here. Over a game still in the player's save
     * the row of its kind carries that game on, and the other is not drawn
     * as a button and takes no press (renderLobby says why). */
    for (uint8_t row = 0; row < 2; ++row) {
        if (!lobbyRowRect(row).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        const Mode kind = row == 0 ? Mode::Local : Mode::Computer;
        if (shelved_ == kind) {
            resumeShelved(host);
            return;
        }
        if (shelved_ != Mode::Lobby) {
            return;
        }
        host.playSound(Sound::Select);
        newGame(kind);
        saveGame(host);
        return;
    }
    for (uint8_t i = 0; i < seatCount_; ++i) {
        if (lobbyRowRect(static_cast<uint8_t>(i + 2)).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            pressSeat(host, i);
            return;
        }
    }
}

void GoGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    /* The chips: board size only where 19x19 is offered, rules, level. Each
     * is a label over a value with a small arrow, and a tap cycles it. */
    const uint8_t chips = BIG_BOARD_AVAILABLE ? 3 : 2;
    const char* labels[3] = {"Rules", "Level", "Board"};
    char values[3][16];
    snprintf(values[0], sizeof(values[0]), "%s", Go::rulesName(rules_));
    snprintf(values[1], sizeof(values[1]), "%s", level_ == Go::Level::Easy ? "Easy" : "Medium");
    snprintf(values[2], sizeof(values[2]), "%s", boardSize_ > 9 ? "19x19" : "9x9");
    for (uint8_t i = 0; i < chips; ++i) {
        const Rect c = lobbyChipRect(i, chips);
        tft.fillRoundRect(c.x, c.y, c.w, c.h, 6, Ui::surface());
        tft.drawRoundRect(c.x, c.y, c.w, c.h, 6, Ui::outline());
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(labels[i], c.x + 7, c.y + 3, 1);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(values[i], c.x + 7, c.y + 12, 2);
        tft.fillTriangle(c.x + c.w - 16, c.y + 11, c.x + c.w - 8, c.y + 11, c.x + c.w - 12,
                         c.y + 17, Ui::muted());
    }

    /* The two rows for playing here. Over a game still in the player's own
     * save, the row of its kind says it carries that game on, and the other
     * is not offered: starting one would write over the game being kept, and
     * a game is only ever given up with End game. */
    static const char* const HERE[2] = {"Pass and play", "Play the computer"};
    static const char* const CARRY_ON[2] = {"Carry on pass and play",
                                            "Carry on with the computer"};
    for (uint8_t row = 0; row < 2; ++row) {
        const Mode kind = row == 0 ? Mode::Local : Mode::Computer;
        if (shelved_ != Mode::Lobby && shelved_ != kind) {
            drawPlainRow(tft, lobbyRowRect(row), HERE[row]);
            continue;
        }
        Ui::drawButton(tft, lobbyRowRect(row), shelved_ == kind ? CARRY_ON[row] : HERE[row],
                       Ui::panel(), Ui::outline(), Ui::text());
    }
    char label[48];
    for (uint8_t i = 0; i < seatCount_; ++i) {
        const Rect r = lobbyRowRect(static_cast<uint8_t>(i + 2));
        const Row row = rowFor(seats_[i]);
        rowLabel(seats_[i], row, label, sizeof(label));
        if (rowTakesPress(row)) {
            const bool invites = row == Row::Invites;
            Ui::drawButton(tft, r, label, invites ? Ui::success() : Ui::surface(), Ui::outline(),
                           invites ? static_cast<uint16_t>(TFT_BLACK) : Ui::text());
            continue;
        }
        drawPlainRow(tft, r, label);   // only says what that console is doing
    }
    /* Why a game just vanished; else a low battery, which outranks the usual
     * note; else what nearby play is and who can switch it on. */
    const bool low = lobbyNote_[0] == 0 && seatCount_ > 0 && host.batteryLow();
    const char* note = lobbyNote_[0] != 0 ? lobbyNote_
                       : low              ? "Battery low: a nearby game may not finish."
                       : seatCount_ > 0   ? "Moves travel by Bluetooth. Anyone near hears them."
                                          : "An administrator can switch Beacon and Nearby on.";
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(lobbyNote_[0] != 0 || low ? Ui::warning() : Ui::muted(), Ui::bg());
    tft.drawString(note, GAME_CANVAS_WIDTH / 2, GAME_CANVAS_HEIGHT - 4, lobbyNote_[0] != 0 ? 2 : 1);
    tft.setTextDatum(TL_DATUM);
}
