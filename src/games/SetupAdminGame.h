#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

/* First-boot admin profile setup wizard. Creates the Admin profile and sets
 * a 4-digit PIN for protecting device settings. Only runs on first device boot
 * or when upgrading with no admin set. */
class SetupAdminGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Phase : uint8_t { Welcome, CreateProfile, SetPin, ConfirmPin, Complete };

    Rect nextButtonRect() const;
    Rect keyRect(uint8_t row, uint8_t col) const;
    Rect deleteRect() const;
    Rect confirmRect() const;
    Rect pinDisplayRect() const;

    void renderWelcome(GameHost& host);
    void renderCreateProfile(GameHost& host);
    void renderSetPin(GameHost& host);
    void renderConfirmPin(GameHost& host);
    void renderComplete(GameHost& host);

    void appendPinDigit(uint8_t digit);
    void deletePinDigit();

    Phase phase_ = Phase::Welcome;
    String draftName_;
    uint16_t draftPin_ = 0;
    uint16_t confirmPin_ = 0;
    uint8_t adminProfileIdx_ = 0;
};
