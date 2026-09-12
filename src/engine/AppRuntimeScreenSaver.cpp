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
    /* lockOnWake_ is the Lock button asking for this one: an owner who has
     * switched the wake lock off still gets the lock screen when they press
     * Lock themselves, because that press is the request. */
    const bool lock = board_.wakeLockEnabled() || lockOnWake_;
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

    /* Same gate as the wake path. It matters under SaverOnly: a deliberate
     * lock that nobody unlocks hands over to the saver rather than blanking,
     * and coming back out of that must still ask for the hold. */
    if (board_.wakeLockEnabled() || lockOnWake_) {
        enterLock();
        return;
    }

    lastActivityMs_ = millis();
    resumeUnderlyingScreen();
}

/* One place that decides what you come back to. It was two, and they agreed
 * only by accident; the lock screen would have made it three. */
void BrainoApp::resumeUnderlyingScreen() {
    /* One place decides what you come back to, so it is also the one place
     * that can say the deliberate lock is over. */
    lockOnWake_ = false;
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
        ssav_textDrawn_ = false;
        ssav_batPctDrawn_ = -2;
        ssav_batBoxW_ = 0;
        ssav_lyDrawn_ = -1;
        ssav_ryDrawn_ = -1;
        ssav_initialized_ = true;
    }

    constexpr int16_t PAD_H = 40;
    constexpr int16_t PAD_W = 6;
    constexpr int16_t PAD_R = 3;
    constexpr int16_t BALL = 6;
    constexpr int16_t LX = 10;
    const int16_t RX = static_cast<int16_t>(effW - 10);

    /* Kept, because everything below has to know where the hole is. The ball is
     * the only thing that moves across the whole panel, so it is the only
     * thing that can punch through the text, the net or the badge -- and those
     * three are now repainted only when it has. */
    const int16_t ballEraseX = static_cast<int16_t>(ssav_bx_ - BALL);
    const int16_t ballEraseY = static_cast<int16_t>(ssav_by_ - BALL);
    tft.fillRect(ballEraseX, ballEraseY, BALL * 2, BALL * 2, TFT_BLACK);

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

    auto hitsBall = [&](int16_t x, int16_t y, int16_t w, int16_t h) {
        return ballEraseX < x + w && ballEraseX + BALL * 2 > x &&
               ballEraseY < y + h && ballEraseY + BALL * 2 > y;
    };

    /* THE MARK STANDS STILL, CENTRED ON WHICHEVER WAY THE PANEL IS HELD.
     *
     * It used to bob up and down on an eighteen-second sine, which meant
     * erasing and redrawing a band of the screen whenever the integer position
     * changed -- a visible shimmer for no information. effW and effH are the
     * live panel, so the centre is right in landscape and portrait alike.
     *
     * It is the BADGE now rather than the product name in font 4: the brain
     * with the wordmark under it, from the same artwork the case badge is cut
     * from, blitted from a one-bit mask (`Ui::drawLogo`). The name is inside
     * the artwork, so nothing draws it as text here any more.
     *
     * What changes instead is colour: the mark takes a dim shade of the rally
     * colour, so it shifts with every paddle hit. A colour change is an
     * overdraw -- the same silhouette, in the same place -- so nothing is
     * erased and nothing flashes. The only other thing that repaints it is the
     * ball passing through, which has already punched its own square out of
     * it; the erase inside that square is why the mark is redrawn whole rather
     * than only when its colour moves.
     *
     * The block is the mark with the copyright line under it, and its box is
     * derived from the mask's own size -- the net skips that box, and the ball
     * test uses it, so a typed-in rectangle here would show up as a net drawn
     * through the logo. */
    const int16_t midX = static_cast<int16_t>(effW / 2);
    const int16_t midY = static_cast<int16_t>(effH / 2);
    constexpr int16_t COPY_GAP = 8;
    const int16_t logoW = Ui::logoWidth();
    const int16_t logoH = Ui::logoHeight();
    const int16_t blockH = static_cast<int16_t>(logoH + COPY_GAP + 8);
    const int16_t textY = static_cast<int16_t>(midY - blockH / 2);
    const int16_t logoCy = static_cast<int16_t>(textY + logoH / 2);
    const int16_t copyY = static_cast<int16_t>(textY + logoH + COPY_GAP);
    const int16_t textW = static_cast<int16_t>(
        max<int16_t>(logoW, static_cast<int16_t>(tft.textWidth(BRAINO_COPYRIGHT, 1))) + 8);
    const int16_t textX = static_cast<int16_t>(midX - textW / 2);
    const int16_t TEXT_H = blockH;
    const uint16_t nameColour = Ui::shade(ssav_color_, 60);
    if (!ssav_textDrawn_ || nameColour != ssav_textColorDrawn_ ||
        hitsBall(textX, textY, textW, TEXT_H)) {
        /* No erase, deliberately: the silhouette never changes shape, so the
         * new colour lands on exactly the pixels the old one occupied, and the
         * ball has already blacked out whatever it flew through. Clearing the
         * block first would be a 72x104 flash on every paddle hit. */
        Ui::drawLogo(tft, midX, logoCy, nameColour);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Ui::rgb(70, 76, 92), TFT_BLACK);
        tft.drawString(BRAINO_COPYRIGHT, midX, copyY, 1);
        tft.setTextDatum(TL_DATUM);
        ssav_textDrawn_ = true;
        ssav_textColorDrawn_ = nameColour;
    }

    /* THE BATTERY IS REPAINTED WHEN ITS NUMBER CHANGES, NOT EVERY FRAME.
     *
     * It was erased and redrawn on every frame, which is a badge visibly
     * flickering at 25Hz. Its percentage is a RAM read and moves rarely, so
     * it now repaints on a change -- erasing the old box first, because the
     * badge's width follows its digits -- or when the ball has cut into it,
     * in which case an overdraw is enough. */
    const int16_t batCx = midX;
    const int16_t batCy = 14;
    const int8_t batPct = board_.getBatteryPercent();
    const int16_t batW = static_cast<int16_t>(Ui::batteryBadgeWidth(tft, batPct) + 6);
    const int16_t batY = static_cast<int16_t>(batCy - 8);
    constexpr int16_t BAT_H = 16;
    const bool batChanged = batPct != ssav_batPctDrawn_;
    const bool batHit = ssav_batBoxW_ > 0 &&
                        hitsBall(static_cast<int16_t>(batCx - ssav_batBoxW_ / 2), batY,
                                 ssav_batBoxW_, BAT_H);
    if (batChanged || batHit) {
        if (batChanged && ssav_batBoxW_ > 0) {
            tft.fillRect(static_cast<int16_t>(batCx - ssav_batBoxW_ / 2), batY,
                         ssav_batBoxW_, BAT_H, TFT_BLACK);
        }
        Ui::drawBatteryBadge(tft, batCx, batCy, batPct, TFT_BLACK);
        ssav_batPctDrawn_ = batPct;
        ssav_batBoxW_ = batW;
    }

    /* The net is redrawn every frame -- it is twenty-odd 2x8 fills in a colour
     * that never changes, so overdrawing it costs nothing visible and repairs
     * wherever the ball went through. It skips the stretches behind the
     * wordmark and the battery, so it no longer cuts through either. */
    for (int16_t y = 0; y < effH; y += 14) {
        const bool behindText = y + 8 > textY && y < textY + TEXT_H;
        const bool behindBattery = y + 8 > batY && y < batY + BAT_H;
        if (!behindText && !behindBattery) {
            tft.fillRect(static_cast<int16_t>(midX - 1), y, 2, 8, Ui::rgb(40, 40, 40));
        }
    }

    /* THE PADDLES ARE REPAINTED WHEN THEY MOVE, NOT WHEN A FRAME HAPPENS.
     *
     * Only one paddle tracks the ball at a time, so the other stands still for
     * half of every rally -- and both used to be erased to black and redrawn
     * every frame regardless, which on the panel is a paddle flickering while
     * it does nothing. Now a paddle is touched only when its integer position
     * or its colour changes, or the ball's erase has cut into it.
     *
     * A moving paddle erases just the rows it has left: the old rect minus the
     * new one's straight-sided middle. The overlap is repainted in the colour
     * it already had, so it does not blink on its way past. */
    const bool recolour = (ssav_color_ != ssav_padColorDrawn_);
    auto paintPaddle = [&](int16_t cx, float yCentre, int16_t& drawnTop) {
        const int16_t left = static_cast<int16_t>(cx - PAD_W / 2);
        const int16_t top = static_cast<int16_t>(yCentre - PAD_H / 2);
        const bool painted = drawnTop >= 0;
        const bool damaged = painted && hitsBall(left, drawnTop, PAD_W, PAD_H);
        if (painted && top == drawnTop && !recolour && !damaged) {
            return;
        }
        if (painted && top != drawnTop) {
            const int16_t oldBottom = static_cast<int16_t>(drawnTop + PAD_H);
            const int16_t keepTop = static_cast<int16_t>(top + PAD_R);
            const int16_t keepBottom = static_cast<int16_t>(top + PAD_H - PAD_R);
            const int16_t upperEnd = min(oldBottom, keepTop);
            if (upperEnd > drawnTop) {
                tft.fillRect(left, drawnTop, PAD_W, static_cast<int16_t>(upperEnd - drawnTop),
                             TFT_BLACK);
            }
            const int16_t lowerStart = max(drawnTop, keepBottom);
            if (oldBottom > lowerStart) {
                tft.fillRect(left, lowerStart, PAD_W, static_cast<int16_t>(oldBottom - lowerStart),
                             TFT_BLACK);
            }
        }
        tft.fillRoundRect(left, top, PAD_W, PAD_H, PAD_R, ssav_color_);
        drawnTop = top;
    };
    paintPaddle(LX, ssav_ly_, ssav_lyDrawn_);
    paintPaddle(RX, ssav_ry_, ssav_ryDrawn_);
    ssav_padColorDrawn_ = ssav_color_;

    tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL), static_cast<int16_t>(ssav_by_ - BALL), BALL * 2, BALL * 2, 2, ssav_color_);
    tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL / 2), static_cast<int16_t>(ssav_by_ - BALL / 2), BALL, BALL, 1, TFT_WHITE);
}
