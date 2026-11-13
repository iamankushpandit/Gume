// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class ProfileApp : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    /* Two-phase, but deliberately NOT a partial-repaint screen -- see the
     * note above renderStatic() in the .cpp, and its entry in
     * docs/RENDER_AUDIT.md. */
    void renderStatic(GameHost& host) override;
    void renderDynamic(GameHost& host) override;

    /* This screen draws no top bar -- it carries its own wordmark header --
     * but the picker shows the battery, between Add Player and Done, and the
     * speaker, so three chrome events reach it: the percentage changing, mute
     * being toggled, and a notification coming or going over the header. Each
     * repaints exactly its own piece -- the badge, the speaker, or the 30px
     * header strip -- and never the screen: a full repaint for a battery tick
     * is the flash the invalidation rule exists to stop. The other phases
     * show none of those and return false, as the picker used to. */
    bool renderChrome(GameHost& host) override;
    /* No clock on this screen, so the minute has nothing to change. Without
     * this the default would draw a top bar's clock it does not have. */
    bool renderClock(GameHost& host) override {
        (void)host;
        return true;
    }

    /* The picker carries its own padlock and speaker (LauncherLayout's
     * pickerLockRect/pickerSpeakerRect), and only while picking: the menu,
     * rename, games list and PIN pad have their own layouts, and the runtime
     * takes nothing on them. */
    bool headerControls(int16_t screenW, int16_t screenH, Rect& lock,
                        Rect& speaker) const override;

private:
    enum class Phase : uint8_t { Pick, Menu, Rename, Games, PinEntry };

    /* What a successful PIN entry should do. Both routes into the admin
     * profile are gated: switching to it, and opening its Edit menu (which
     * leads to rename and to the per-profile game list). */
    enum class PinPurpose : uint8_t { Switch, OpenMenu };

    Rect headerRect(int16_t screenW, int16_t screenH) const;
    Rect slotRect(uint8_t i, int16_t screenW, int16_t screenH) const;
    Rect menuRect(uint8_t i, int16_t screenW, int16_t screenH) const;
    Rect pinKeyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const;
    Rect pinDeleteRect(int16_t screenW, int16_t screenH) const;
    Rect pinConfirmRect(int16_t screenW, int16_t screenH) const;
    Rect pinCancelRect(int16_t screenW, int16_t screenH) const;
    Rect renameCancelRect(int16_t screenW, int16_t screenH) const;
    /* The gap between Add Player and Done, which holds the battery badge: its
     * measured width plus air either side, or 0 on a board with no battery
     * sense, whose buttons keep their old width. Hit tests and drawing both
     * ask, so the badge and the buttons cannot come to overlap. */
    static int16_t batterySlot(Ui::Renderer& tft);
    Rect addRect(int16_t screenW, int16_t screenH, int16_t slot) const;
    Rect doneRect(int16_t screenW, int16_t screenH, int16_t slot) const;
    void drawHeaderStrip(Ui::Renderer& tft, int16_t W, int16_t H);
    void drawBatterySlot(Ui::Renderer& tft, int16_t W, int16_t H, int8_t percent);
    Rect keyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH) const;
    Rect menuActionRect(uint8_t i, int16_t screenW, int16_t screenH) const;
    Rect gameCheckRect(uint8_t row, int16_t screenW) const;
    Rect gamesBackRect(int16_t screenW) const;
    Rect gamesPrevRect(int16_t screenH) const;
    Rect gamesNextRect(int16_t screenW, int16_t screenH) const;
    uint8_t visibleGameRows(int16_t screenH) const;
    uint8_t rowCount(Board& board) const;
    uint8_t profileForRow(Board& board, uint8_t row) const;

    void beginPinEntry(uint8_t profile, PinPurpose purpose);
    void appendPinDigit(uint8_t digit);
    void deletePinDigit();
    /* Split by "does a digit change it?": the keys, heading and Back are
     * chrome and painted once by renderStatic(); the four dots are the only
     * thing a keypress touches. See the note in ProfileApp.cpp. */
    void renderPinPadChrome(GameHost& host);
    void renderPinDots(GameHost& host);
    void updatePinEntry(GameHost& host, const TouchPoint& touch);
    /* A physical keyboard's character, for the PIN pad and the name
     * field; both reach the same functions a touch does. */
    void typePinChar(GameHost& host, char typed);
    void confirmPin(GameHost& host);
    void typeRenameChar(GameHost& host, char typed);
    void applyRenameKey(GameHost& host, char ch);
    void updateRename(GameHost& host, const TouchPoint& touch);
    void renderRename(GameHost& host);

    Phase phase_ = Phase::Pick;
    /* What the picker last drew in its chrome, so renderChrome() repaints
     * only what changed. */
    int8_t shownPct_ = -2;
    bool shownMuted_ = false;
    uint8_t editing_ = 0;
    uint8_t menuFor_ = 0;
    uint8_t gameScroll_ = 0;
    uint16_t adminPinAttempt_ = 0;
    uint8_t adminPinDigitCount_ = 0;
    uint8_t profileToSwitchTo_ = 0;
    PinPurpose pinPurpose_ = PinPurpose::Switch;
    String draft_;
};
