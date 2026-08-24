#include "SetupAdminGame.h"
#include "hal/Board.h"

const char* SetupAdminGame::title() const { return "Setup Admin"; }

void SetupAdminGame::begin(GameHost& host) {
    draftName_ = String();
    draftPin_ = 0;
    confirmPin_ = 0;
    adminProfileIdx_ = 0;
    phase_ = Phase::Welcome;
    markFullDirty();
}

Rect SetupAdminGame::nextButtonRect() const { return Rect{180, 280, 140, 40}; }
Rect SetupAdminGame::keyRect(uint8_t row, uint8_t col) const {
    return Rect{12 + col * 96, 120 + row * 40, 88, 36};
}
Rect SetupAdminGame::deleteRect() const { return Rect{12, 200, 88, 36}; }
Rect SetupAdminGame::confirmRect() const { return Rect{232, 200, 88, 36}; }
Rect SetupAdminGame::pinDisplayRect() const { return Rect{80, 70, 160, 30}; }

void SetupAdminGame::appendPinDigit(uint8_t digit) {
    if (phase_ == Phase::SetPin && draftPin_ < 9999) {
        draftPin_ = draftPin_ * 10 + digit;
        markDirty();
    } else if (phase_ == Phase::ConfirmPin && confirmPin_ < 9999) {
        confirmPin_ = confirmPin_ * 10 + digit;
        markDirty();
    }
}

void SetupAdminGame::deletePinDigit() {
    if (phase_ == Phase::SetPin && draftPin_ > 0) {
        draftPin_ /= 10;
        markDirty();
    } else if (phase_ == Phase::ConfirmPin && confirmPin_ > 0) {
        confirmPin_ /= 10;
        markDirty();
    }
}

void SetupAdminGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;

    if (phase_ == Phase::Welcome) {
        if (nextButtonRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::CreateProfile;
            markFullDirty();
        }
        return;
    }

    if (phase_ == Phase::CreateProfile) {
        if (nextButtonRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (draftName_.length() > 0) {
                adminProfileIdx_ = host.board().addKid(draftName_);
                phase_ = Phase::SetPin;
                markFullDirty();
            }
            return;
        }
        /* Keyboard input handled by renderCreateProfile would go here in full impl.
         * For now, assume draftName_ is set via some input mechanism. */
        return;
    }

    if (phase_ == Phase::SetPin) {
        if (confirmRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (draftPin_ >= 1000 && draftPin_ <= 9999) {
                phase_ = Phase::ConfirmPin;
                confirmPin_ = 0;
                markFullDirty();
            }
            return;
        }
        if (deleteRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            deletePinDigit();
            return;
        }
        for (uint8_t row = 0; row < 4; ++row) {
            for (uint8_t col = 0; col < 3; ++col) {
                if (keyRect(row, col).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                    appendPinDigit(static_cast<uint8_t>(row * 3 + col));
                    return;
                }
            }
        }
        return;
    }

    if (phase_ == Phase::ConfirmPin) {
        if (confirmRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (confirmPin_ == draftPin_) {
                host.board().setAdminProfileIndex(adminProfileIdx_);
                host.board().setAdminPin(draftPin_);
                phase_ = Phase::Complete;
                markFullDirty();
            } else {
                host.beepError();
                confirmPin_ = 0;
                markDirty();
            }
            return;
        }
        if (deleteRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            deletePinDigit();
            return;
        }
        for (uint8_t row = 0; row < 4; ++row) {
            for (uint8_t col = 0; col < 3; ++col) {
                if (keyRect(row, col).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                    appendPinDigit(static_cast<uint8_t>(row * 3 + col));
                    return;
                }
            }
        }
        return;
    }

    if (phase_ == Phase::Complete) {
        if (nextButtonRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.goHome();
        }
        return;
    }
}

void SetupAdminGame::renderWelcome(GameHost& host) {
    Ui::Renderer& tft = host.display();

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Welcome to Braino!", 20, 60, 2);
    tft.drawString("Let's set up an Admin profile", 20, 100, 1);
    tft.drawString("to protect device settings.", 20, 120, 1);
    tft.drawString("Only the Admin can change:", 20, 160, 1);
    tft.drawString("- Theme & Layout", 40, 180, 1);
    tft.drawString("- Wi-Fi & Bluetooth", 40, 200, 1);
    tft.drawString("- Available Games", 40, 220, 1);

    Ui::drawButton(tft, nextButtonRect(), "Next", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
}

void SetupAdminGame::renderCreateProfile(GameHost& host) {
    Ui::Renderer& tft = host.display();

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Create Admin Profile", 20, 60, 2);
    tft.drawString("Profile name:", 20, 110, 1);

    Rect nameBox{20, 130, 280, 30};
    tft.drawRect(nameBox.x, nameBox.y, nameBox.w, nameBox.h, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(draftName_.c_str(), nameBox.x + 5, nameBox.y + 7, 1);

    tft.drawString("(Tap to edit name)", 20, 175, 1);
    Ui::drawButton(tft, nextButtonRect(), "Next", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
}

void SetupAdminGame::renderSetPin(GameHost& host) {
    Ui::Renderer& tft = host.display();

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Set 4-digit PIN", 20, 50, 2);
    tft.drawString("PIN: ", 20, 75, 1);

    Rect pinBox = pinDisplayRect();
    tft.fillRect(pinBox.x, pinBox.y, pinBox.w, pinBox.h, Ui::surface());
    tft.drawRect(pinBox.x, pinBox.y, pinBox.w, pinBox.h, Ui::outline());

    char pinStr[8];
    snprintf(pinStr, sizeof(pinStr), "%04u", draftPin_);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(pinStr, pinBox.x + pinBox.w / 2, pinBox.y + pinBox.h / 2, 2);

    for (uint8_t row = 0; row < 4; ++row) {
        for (uint8_t col = 0; col < 3; ++col) {
            char digit[2];
            snprintf(digit, sizeof(digit), "%u", row * 3 + col);
            Ui::drawButton(tft, keyRect(row, col), digit, Ui::panel(), Ui::outline(), Ui::text(), false, 1);
        }
    }

    Ui::drawButton(tft, deleteRect(), "DEL", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
    Ui::drawButton(tft, confirmRect(), "OK", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
}

void SetupAdminGame::renderConfirmPin(GameHost& host) {
    Ui::Renderer& tft = host.display();

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Confirm PIN", 20, 50, 2);
    tft.drawString("Re-enter PIN:", 20, 75, 1);

    Rect pinBox = pinDisplayRect();
    tft.fillRect(pinBox.x, pinBox.y, pinBox.w, pinBox.h, Ui::surface());
    tft.drawRect(pinBox.x, pinBox.y, pinBox.w, pinBox.h, Ui::outline());

    char pinStr[8];
    snprintf(pinStr, sizeof(pinStr), "%04u", confirmPin_);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(pinStr, pinBox.x + pinBox.w / 2, pinBox.y + pinBox.h / 2, 2);

    for (uint8_t row = 0; row < 4; ++row) {
        for (uint8_t col = 0; col < 3; ++col) {
            char digit[2];
            snprintf(digit, sizeof(digit), "%u", row * 3 + col);
            Ui::drawButton(tft, keyRect(row, col), digit, Ui::panel(), Ui::outline(), Ui::text(), false, 1);
        }
    }

    Ui::drawButton(tft, deleteRect(), "DEL", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
    Ui::drawButton(tft, confirmRect(), "OK", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
}

void SetupAdminGame::renderComplete(GameHost& host) {
    Ui::Renderer& tft = host.display();

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Admin Setup Complete!", 20, 60, 2);
    tft.drawString("Profile: " + draftName_, 20, 110, 1);
    tft.drawString("PIN: ****", 20, 135, 1);
    tft.drawString("", 20, 160, 1);
    tft.drawString("You can now launch games.", 20, 180, 1);
    tft.drawString("Only the Admin can change", 20, 200, 1);
    tft.drawString("device settings.", 20, 220, 1);

    Ui::drawButton(tft, nextButtonRect(), "Start", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
}

void SetupAdminGame::render(GameHost& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    switch (phase_) {
        case Phase::Welcome:
            renderWelcome(host); break;
        case Phase::CreateProfile:
            renderCreateProfile(host); break;
        case Phase::SetPin:
            renderSetPin(host); break;
        case Phase::ConfirmPin:
            renderConfirmPin(host); break;
        case Phase::Complete:
            renderComplete(host); break;
    }

    tft.setTextDatum(TL_DATUM);
}
