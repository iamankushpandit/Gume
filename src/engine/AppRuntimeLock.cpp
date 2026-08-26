#include "AppRuntime.h"

#include "hal/Watchdog.h"
#include "ui/LauncherLayout.h"

namespace {
void drawCenteredFitted(Ui::Renderer& tft, const char* text, int16_t cx,
                        int16_t y, int16_t maxW, uint8_t font) {
    tft.drawString(Ui::fitted(tft, text != nullptr ? text : "", maxW, font),
                   cx, y, font);
}

int16_t lockFooterY(Ui::Renderer& tft) {
    constexpr int16_t GLCD_FONT_H = 8;
    constexpr int16_t BOTTOM_PAD = 8;
    return static_cast<int16_t>(tft.height() - BOTTOM_PAD - GLCD_FONT_H);
}
}

/*
 * The wake lock: an accidental-touch guard between the screen saver or panel
 * sleep and whatever screen was underneath.
 *
 * The problem it solves is not security. A console in a bag or a coat pocket
 * gets pressed constantly, and a single stray press used to dismiss the saver
 * and land straight on a live screen -- mid-game, or on Settings.
 * `swallowTouch_` suppressed the *next* press after that, which never helped:
 * the press that dismissed the saver was itself the unintended one.
 *
 * It is deliberately NOT a PIN. The admin PIN guards the admin profile and is
 * asked every time; this screen neither grants nor revokes admin, and unlocking
 * returns to exactly the screen and profile that were up before.
 *
 * No rotation is applied on the way in. The lock lays itself out against the
 * live tft.width()/height(), so it is correct in whichever orientation the
 * panel already happens to be in, and resumeUnderlyingScreen() applies the
 * screen's own rotation on the way out. Rotating twice for a screen that is
 * up for a second would cost two full repaints and a visible flash.
 */

Rect BrainoApp::activeLockRect() {
    /* The launcher draws no top bar, so it carries the button on its own
     * header instead. Both rects come from LauncherLayout, which is also where
     * the drawing reads them from. */
    if (activeGame_ == &launcher_) {
        return LauncherLayout::lockRect(board_.layoutMode(), renderer_.width());
    }
    return LauncherLayout::topBarLockRect();
}

/* The deliberate way in. Everything else here is reached by a timeout; this is
 * reached by a child, a parent or a bag being packed. */
void BrainoApp::lockAndSleepNow() {
    board_.beepOk();
    lockOnWake_ = true;
    /* The finger that pressed Lock is still on the panel. Without this the
     * Asleep branch takes that same contact as the press that wakes it, and
     * the screen lights again before it has gone dark. Cleared on release. */
    swallowTouch_ = true;
    enterSleep();
}

Rect BrainoApp::lockButtonRect() {
    const int16_t w = static_cast<int16_t>(renderer_.width());
    const int16_t h = static_cast<int16_t>(renderer_.height());
    const int16_t bw = min<int16_t>(200, static_cast<int16_t>(w - 48));
    constexpr int16_t BH = 58;
    return Rect{static_cast<int16_t>((w - bw) / 2),
                static_cast<int16_t>((h - BH) / 2 + 12), bw, BH};
}

Rect BrainoApp::lockProgressRect() {
    const Rect b = lockButtonRect();
    return Rect{b.x, static_cast<int16_t>(b.y + b.h + 10), b.w, 10};
}

uint8_t BrainoApp::lockProgressPercent(uint32_t nowMs) const {
    if (!lockHolding_) return 0;
    const uint32_t held = nowMs - lockHoldStartMs_;
    if (held >= LOCK_HOLD_MS) return 100;
    return static_cast<uint8_t>(held * 100UL / LOCK_HOLD_MS);
}

void BrainoApp::enterLock() {
    board_.setRgbColor(0, 0, 0);
    Watchdog::setContext("Locked");
    view_ = View::Locked;
    lockHolding_ = false;
    lockHoldStartMs_ = 0;
    lockContactMs_ = 0;
    lockActivityMs_ = millis();
    lockFullPaint_ = true;
    lockPaintedPct_ = -1;
    /* The press that got us here is the one we do not trust. Swallowing it
     * means the hold cannot start until the finger has come off, so a press
     * held through a bag can never complete the gesture however long it
     * lasts. */
    swallowTouch_ = true;
    renderLock();
}

void BrainoApp::updateLock(const TouchPoint& touch, uint32_t nowMs) {
    if (touch.down || touch.justPressed || touch.justReleased) {
        lockActivityMs_ = nowMs;
    }

    const Rect target = lockButtonRect();
    const bool onTarget = touch.down && target.contains(touch.x, touch.y, TOUCH_HIT_SLOP);

    if (onTarget) {
        if (!lockHolding_) {
            lockHolding_ = true;
            lockHoldStartMs_ = nowMs;
        }
        lockContactMs_ = nowMs;
    } else if (lockHolding_) {
        /* A held press on a resistive panel drops below
         * TOUCH_PRESSURE_THRESHOLD intermittently -- that is how the hardware
         * behaves, not a fault. Resetting on the first gap made the gesture
         * feel broken while looking perfectly correct in code, so short gaps
         * are forgiven. A press that moves off the target is a different
         * thing and is not: it cancels once the grace has run out too. */
        if (nowMs - lockContactMs_ > LOCK_CONTACT_GRACE_MS) {
            lockHolding_ = false;
            lockHoldStartMs_ = 0;
        }
    }

    if (lockHolding_ && nowMs - lockHoldStartMs_ >= LOCK_HOLD_MS) {
        board_.beepOk();
        lockHolding_ = false;
        lastActivityMs_ = nowMs;
        resumeUnderlyingScreen();
        return;
    }

    /* Nobody unlocked. Go back the way we came rather than sitting lit: a
     * lock screen burning the battery for the rest of the afternoon is the
     * failure the saver and sleep exist to prevent. SaverOnly says the panel
     * must never blank, so that policy gets the saver back instead. */
    if (nowMs - lockActivityMs_ > LOCK_TIMEOUT_MS) {
        if (board_.idleAction() == Board::IdleAction::SaverOnly) {
            enterScreenSaver();
        } else {
            enterSleep();
        }
        return;
    }

    renderLock();
}

void BrainoApp::renderLock() {
    Ui::Renderer& tft = renderer_;
    const int16_t W = static_cast<int16_t>(tft.width());
    const Rect btn = lockButtonRect();
    const Rect bar = lockProgressRect();
    const uint8_t pct = lockProgressPercent(millis());
    const int16_t textMaxW = static_cast<int16_t>(W - 16);

    if (lockFullPaint_) {
        lockFullPaint_ = false;
        Ui::clear(tft);

        /* The same padlock the Lock button shows, drawn large. It was a
         * ring-and-body built inline here; there are three of them now -- top
         * bar, launcher header and this screen -- so the glyph lives in Ui and
         * takes its proportions from the rect it is given. */
        constexpr int16_t ICON = 34;
        Ui::drawLockIcon(tft, Rect{static_cast<int16_t>(W / 2 - ICON / 2),
                                   static_cast<int16_t>(btn.y - 74 - ICON / 2),
                                   ICON, ICON}, Ui::muted(), Ui::bg());

        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(Ui::text(), Ui::bg());
        drawCenteredFitted(tft, "Locked", W / 2,
                           static_cast<int16_t>(btn.y - 48), textMaxW, 4);
        tft.setTextColor(Ui::muted(), Ui::bg());
        drawCenteredFitted(tft, "Press and hold the button", W / 2,
                           static_cast<int16_t>(btn.y - 22), textMaxW, 1);

        Ui::drawButton(tft, btn, "Hold to unlock", Ui::rgb(36, 132, 204),
                       Ui::outline(), TFT_WHITE, false, 2);

        tft.drawRoundRect(bar.x, bar.y, bar.w, bar.h, 4, Ui::outline());
        tft.setTextColor(Ui::muted(), Ui::bg());
        drawCenteredFitted(tft, "Nothing under here can be touched yet", W / 2,
                           lockFooterY(tft), textMaxW, 1);
        tft.setTextDatum(TL_DATUM);
        lockPaintedPct_ = -1;
    }

    /* The bar is the only thing that moves, and it is repainted only when its
     * width actually changes. Repainting the button and its label at frame
     * rate would spend milliseconds a frame redrawing text that has not moved
     * -- and the whole frame allowance is 20ms. */
    if (lockPaintedPct_ == static_cast<int16_t>(pct)) {
        return;
    }
    lockPaintedPct_ = static_cast<int16_t>(pct);

    const int16_t innerX = static_cast<int16_t>(bar.x + 2);
    const int16_t innerW = static_cast<int16_t>(bar.w - 4);
    const int16_t fillW = static_cast<int16_t>(innerW * pct / 100);
    tft.fillRect(innerX, static_cast<int16_t>(bar.y + 2), innerW,
                 static_cast<int16_t>(bar.h - 4), Ui::surface());
    if (fillW > 0) {
        tft.fillRect(innerX, static_cast<int16_t>(bar.y + 2), fillW,
                     static_cast<int16_t>(bar.h - 4), Ui::success());
    }
}
