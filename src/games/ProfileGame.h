#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class ProfileGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

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
    Rect addRect(int16_t screenW, int16_t screenH) const;
    Rect doneRect(int16_t screenW, int16_t screenH) const;
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
    void renderPinEntry(GameHost& host);

    Phase phase_ = Phase::Pick;
    uint8_t editing_ = 0;
    uint8_t menuFor_ = 0;
    uint8_t gameScroll_ = 0;
    uint16_t adminPinAttempt_ = 0;
    uint8_t adminPinDigitCount_ = 0;
    uint8_t profileToSwitchTo_ = 0;
    PinPurpose pinPurpose_ = PinPurpose::Switch;
    String draft_;
};
