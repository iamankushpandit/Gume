#include "AppRuntime.h"

#include <math.h>
#include "AppVersion.h"
#include "hal/Watchdog.h"

void BrainoApp::enterScreenSaver() {
    board_.setRgbColor(0, 140, 255);
    /* Only a live screen is worth remembering. Coming back here from the lock
     * screen -- which happens when nobody unlocks in time -- must not
     * overwrite what we were doing with View::Locked. */
    if (view_ == View::Game) {
        ssavPrevView_ = view_;
    }
    applyRotation(effectiveRotation(board_.layoutMode() != Board::LayoutMode::Vertical));
    Watchdog::setContext("ScreenSaver");
    view_ = View::ScreenSaver;
    screenSaverStartMs_ = millis();
    ssav_initialized_ = false;
}

void BrainoApp::enterSleep() {
    if (view_ == View::Game) {
        ssavPrevView_ = view_;
    }
    board_.setRgbColor(0, 0, 0);
    board_.displaySleep();
    Watchdog::setContext("Asleep");
    view_ = View::Asleep;
}

/* Both wake paths keep the panel wake inside the Watchdog::Pause guard:
 * Board::displayWake() blocks ~120ms for the ILI9341 guard time, which is
 * otherwise indistinguishable from a stalled loop. */
void BrainoApp::wakeFromSleep() {
    const bool lock = board_.wakeLockEnabled();
    {
        Watchdog::Pause guard;
        board_.displayWake();
        /* The lock screen's first paint goes inside the guard as well. The
         * panel has to be painted in the same frame it lights -- otherwise it
         * shows the live screen underneath for a frame, which is the exact
         * leak this feature exists to close -- and a full repaint on top of
         * the 120ms panel wake is a known-slow section, not a hang.
         *
         * The press that woke the panel is the one we do not trust, so it
         * buys a lit lock screen and nothing else. */
        if (lock) {
            enterLock();
        }
    }
    if (lock) {
        return;
    }

    lastActivityMs_ = millis();
    resumeUnderlyingScreen();
}

void BrainoApp::exitScreenSaver() {
    board_.setRgbColor(0, 0, 0);

    if (board_.wakeLockEnabled()) {
        enterLock();
        return;
    }

    lastActivityMs_ = millis();
    resumeUnderlyingScreen();
}

/* One place that decides what you come back to. It was two, and they agreed
 * only by accident; the lock screen would have made it three. */
void BrainoApp::resumeUnderlyingScreen() {
    if (ssavPrevView_ == View::Game && activeGame_ != nullptr) {
        applyRotation(rotationForActiveScreen());
        Watchdog::setContext(activeAppTitle());
        view_ = View::Game;
        activeGame_->requestRender();
    } else {
        goHome();   // applies its own rotation
    }
}

void BrainoApp::onScreenSaverHit() {
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

void BrainoApp::resetScreenSaverRally(int16_t effW, int16_t effH) {
    ssav_bx_ = effW / 2.0f;
    ssav_by_ = effH / 2.0f;
    ssav_bvx_ = (random(2) ? 2.8f : -2.8f);
    ssav_bvy_ = 1.6f;
    ssav_hits_ = 0;
    ssav_color_ = Ui::rgb(80, 180, 255);
}

void BrainoApp::renderScreenSaver() {
    Ui::Renderer& tft = display();
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
    const int8_t batPct = board_.getBatteryPercent();
    const Ui::PowerHint batPower = Ui::powerHint(board_);
    /* Clear to the badge's own width: it is variable now, and a fixed 24px
     * wipe would leave the tail of a wider one behind as the ball goes past. */
    const int16_t batW = Ui::batteryBadgeWidth(tft, batPct, batPower);
    tft.fillRect(static_cast<int16_t>(batCx - batW / 2 - 2),
                 static_cast<int16_t>(batCy - 8),
                 static_cast<int16_t>(batW + 6), 16, TFT_BLACK);
    Ui::drawBatteryBadge(tft, batCx, batCy, batPct, batPower, TFT_BLACK);

    tft.fillRoundRect(LX - PAD_W / 2, static_cast<int16_t>(ssav_ly_ - PAD_H / 2), PAD_W, PAD_H, 3, ssav_color_);
    tft.fillRoundRect(RX - PAD_W / 2, static_cast<int16_t>(ssav_ry_ - PAD_H / 2), PAD_W, PAD_H, 3, ssav_color_);
    tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL), static_cast<int16_t>(ssav_by_ - BALL), BALL * 2, BALL * 2, 2, ssav_color_);
    tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL / 2), static_cast<int16_t>(ssav_by_ - BALL / 2), BALL, BALL, 1, TFT_WHITE);
}
