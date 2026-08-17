#include "AppRuntime.h"

#include <math.h>
#include "AppVersion.h"
#include "hal/Watchdog.h"

void KidsPlatformApp::enterScreenSaver() {
    board_.setRgbColor(0, 140, 255);
    ssavPrevView_ = view_;
    applyRotation(effectiveRotation(board_.layoutMode() != Board::LayoutMode::Vertical));
    Watchdog::setContext("ScreenSaver");
    view_ = View::ScreenSaver;
    screenSaverStartMs_ = millis();
    ssav_initialized_ = false;
}

void KidsPlatformApp::enterSleep() {
    if (view_ != View::ScreenSaver) {
        ssavPrevView_ = view_;
    }
    board_.setRgbColor(0, 0, 0);
    board_.displaySleep();
    Watchdog::setContext("Asleep");
    view_ = View::Asleep;
}

void KidsPlatformApp::wakeFromSleep() {
    {
        Watchdog::Pause guard;
        board_.displayWake();
    }

    lastActivityMs_ = millis();

    if (ssavPrevView_ == View::Game && activeGame_ != nullptr) {
        applyRotation(rotationForActiveScreen());
        Watchdog::setContext(activeAppTitle());
        view_ = View::Game;
        activeGame_->requestRender();
    } else {
        goHome();   // applies its own rotation
    }
}

void KidsPlatformApp::exitScreenSaver() {
    board_.setRgbColor(0, 0, 0);

    lastActivityMs_ = millis();

    if (ssavPrevView_ == View::Game && activeGame_ != nullptr) {
        applyRotation(rotationForActiveScreen());
        Watchdog::setContext(activeAppTitle());
        view_ = View::Game;
        activeGame_->requestRender();
    } else {
        goHome();
    }
}

void KidsPlatformApp::onScreenSaverHit() {
    static const uint16_t RALLY_COLORS[6] = {
        Ui::rgb(80, 180, 255),
        Ui::rgb(80, 240, 160),
        Ui::rgb(255, 226, 90),
        Ui::rgb(255, 160, 60),
        Ui::rgb(255, 96, 96),
        Ui::rgb(220, 120, 255),
    };
    if (ssav_hits_ < 255) ++ssav_hits_;
    ssav_color_ = RALLY_COLORS[ssav_hits_ % 6];

    static const uint8_t RALLY_RGB[6][3] = {
        {0, 140, 255}, {0, 255, 140}, {255, 210, 0},
        {255, 110, 0}, {255, 0, 40}, {200, 0, 255},
    };
    const uint8_t* c = RALLY_RGB[ssav_hits_ % 6];
    board_.setRgbColor(c[0], c[1], c[2]);
}

void KidsPlatformApp::resetScreenSaverRally(int16_t effW, int16_t effH) {
    ssav_bx_ = effW / 2.0f;
    ssav_by_ = effH / 2.0f;
    ssav_bvx_ = (random(2) ? 2.8f : -2.8f);
    ssav_bvy_ = 1.6f;
    ssav_hits_ = 0;
    ssav_color_ = Ui::rgb(80, 180, 255);
}

void KidsPlatformApp::renderScreenSaver() {
    // Always draw the screen saver on the raw physical renderer -- it fills
    // the real panel dimensions and must not go through the game-canvas scale.
    Ui::Renderer& tft = renderer_;
    const int16_t effW = static_cast<int16_t>(tft.width());
    const int16_t effH = static_cast<int16_t>(tft.height());

    if (!ssav_initialized_) {
        tft.fillScreen(TFT_BLACK);
        ssav_bx_ = effW / 2.0f;
        ssav_by_ = effH / 2.0f;
        ssav_bvx_ = (random(2) ? 2.8f : -2.8f);
        ssav_bvy_ = 1.6f + random(100) * 0.02f;
        ssav_ly_ = effH / 2.0f;
        ssav_ry_ = effH / 2.0f;
        ssav_hits_ = 0;
        ssav_color_ = Ui::rgb(80, 180, 255);
        ssav_textCy_ = -1;
        ssav_initialized_ = true;
    }

    constexpr int16_t PAD_H = 40;
    constexpr int16_t PAD_W = 6;
    constexpr int16_t BALL = 6;
    constexpr int16_t LX = 10;
    const int16_t RX = static_cast<int16_t>(effW - 10);

    tft.fillRect(LX - PAD_W / 2, static_cast<int16_t>(ssav_ly_ - PAD_H / 2 - 2), PAD_W, PAD_H + 4, TFT_BLACK);
    tft.fillRect(RX - PAD_W / 2, static_cast<int16_t>(ssav_ry_ - PAD_H / 2 - 2), PAD_W, PAD_H + 4, TFT_BLACK);
    tft.fillRect(static_cast<int16_t>(ssav_bx_ - BALL), static_cast<int16_t>(ssav_by_ - BALL), BALL * 2, BALL * 2, TFT_BLACK);

    ssav_bx_ += ssav_bvx_;
    ssav_by_ += ssav_bvy_;

    if (ssav_by_ < BALL) { ssav_by_ = BALL; ssav_bvy_ = fabsf(ssav_bvy_); }
    if (ssav_by_ > effH - BALL) { ssav_by_ = effH - BALL; ssav_bvy_ = -fabsf(ssav_bvy_); }

    constexpr uint8_t MAX_HITS = 30;
    constexpr float BASE_SPEED = 3.0f;
    constexpr float FAST_SPEED = 22.0f;
    constexpr float BASE_PAD_SPEED = 3.2f;
    constexpr float FAST_PAD_SPEED = 7.5f;
    constexpr float MAX_KICK_BVY = 3.0f;
    const float easedT = sqrtf(static_cast<float>(min<uint8_t>(ssav_hits_, MAX_HITS)) / MAX_HITS);

    const float padSpeed = BASE_PAD_SPEED + (FAST_PAD_SPEED - BASE_PAD_SPEED) * easedT;
    const bool ballGoingLeft = ssav_bvx_ < 0.0f;
    if (ballGoingLeft) {
        if (ssav_ly_ < ssav_by_ - padSpeed) ssav_ly_ += padSpeed;
        else if (ssav_ly_ > ssav_by_ + padSpeed) ssav_ly_ -= padSpeed;
    } else {
        if (ssav_ry_ < ssav_by_ - padSpeed) ssav_ry_ += padSpeed;
        else if (ssav_ry_ > ssav_by_ + padSpeed) ssav_ry_ -= padSpeed;
    }
    if (ssav_ly_ < PAD_H / 2) ssav_ly_ = PAD_H / 2;
    if (ssav_ly_ > effH - PAD_H / 2) ssav_ly_ = effH - PAD_H / 2;
    if (ssav_ry_ < PAD_H / 2) ssav_ry_ = PAD_H / 2;
    if (ssav_ry_ > effH - PAD_H / 2) ssav_ry_ = effH - PAD_H / 2;

    if (ssav_bvx_ < 0 && ssav_bx_ <= LX + PAD_W / 2 + BALL) {
        if (ssav_by_ >= ssav_ly_ - PAD_H / 2 && ssav_by_ <= ssav_ly_ + PAD_H / 2) {
            onScreenSaverHit();
            const float t = sqrtf(static_cast<float>(min<uint8_t>(ssav_hits_, MAX_HITS)) / MAX_HITS);
            ssav_bvx_ = BASE_SPEED + (FAST_SPEED - BASE_SPEED) * t;
            ssav_bvy_ = constrain(ssav_bvy_ + (ssav_by_ - ssav_ly_) * 0.05f, -MAX_KICK_BVY, MAX_KICK_BVY);
            ssav_bx_ = LX + PAD_W / 2 + BALL + 1;
            if (ssav_hits_ >= MAX_HITS) resetScreenSaverRally(effW, effH);
        }
    }
    if (ssav_bvx_ > 0 && ssav_bx_ >= RX - PAD_W / 2 - BALL) {
        if (ssav_by_ >= ssav_ry_ - PAD_H / 2 && ssav_by_ <= ssav_ry_ + PAD_H / 2) {
            onScreenSaverHit();
            const float t = sqrtf(static_cast<float>(min<uint8_t>(ssav_hits_, MAX_HITS)) / MAX_HITS);
            ssav_bvx_ = -(BASE_SPEED + (FAST_SPEED - BASE_SPEED) * t);
            ssav_bvy_ = constrain(ssav_bvy_ + (ssav_by_ - ssav_ry_) * 0.05f, -MAX_KICK_BVY, MAX_KICK_BVY);
            ssav_bx_ = RX - PAD_W / 2 - BALL - 1;
            if (ssav_hits_ >= MAX_HITS) resetScreenSaverRally(effW, effH);
        }
    }
    if (ssav_bx_ < 0 || ssav_bx_ > effW) {
        resetScreenSaverRally(effW, effH);
    }

    const float bobAmplitude = fmaxf(10.0f, effH / 2.0f - 40.0f);
    const float bob = sinf(static_cast<float>(millis()) * 0.00035f) * bobAmplitude;
    const int16_t cy = static_cast<int16_t>(effH / 2.0f + bob);
    constexpr int16_t TEXT_BAND_TOP = 26;
    constexpr int16_t TEXT_BAND_BOTTOM = 22;
    if (ssav_textCy_ >= 0) {
        tft.fillRect(0, static_cast<int16_t>(ssav_textCy_ - TEXT_BAND_TOP), effW,
                     TEXT_BAND_TOP + TEXT_BAND_BOTTOM, TFT_BLACK);
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::rgb(120, 128, 150), TFT_BLACK);
    tft.drawString(BRAINO_PRODUCT_NAME, effW / 2, static_cast<int16_t>(cy - 10), 4);
    tft.setTextColor(Ui::rgb(70, 76, 92), TFT_BLACK);
    tft.drawString(BRAINO_COPYRIGHT, effW / 2, static_cast<int16_t>(cy + 14), 1);
    tft.setTextDatum(TL_DATUM);
    ssav_textCy_ = cy;

    for (int16_t y = 0; y < effH; y += 14) {
        tft.fillRect(effW / 2 - 1, y, 2, 8, Ui::rgb(40, 40, 40));
    }

    const int16_t batCx = effW / 2;
    const int16_t batCy = 14;
    tft.fillRect(static_cast<int16_t>(batCx - 12), static_cast<int16_t>(batCy - 6), 24, 12, TFT_BLACK);
    Ui::drawBatteryBadge(tft, batCx, batCy,
                         board_.getBatteryPercent(),
                         board_.getPowerSource() == Board::PowerState::EXTERNAL_POWER,
                         TFT_BLACK);

    tft.fillRoundRect(LX - PAD_W / 2, static_cast<int16_t>(ssav_ly_ - PAD_H / 2), PAD_W, PAD_H, 3, ssav_color_);
    tft.fillRoundRect(RX - PAD_W / 2, static_cast<int16_t>(ssav_ry_ - PAD_H / 2), PAD_W, PAD_H, 3, ssav_color_);
    tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL), static_cast<int16_t>(ssav_by_ - BALL), BALL * 2, BALL * 2, 2, ssav_color_);
    tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL / 2), static_cast<int16_t>(ssav_by_ - BALL / 2), BALL, BALL, 1, TFT_WHITE);
}
