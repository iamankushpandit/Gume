// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AppRuntime.h"

#include "engine/NearbyPlay.h"

/* The notification strip -- what is laid along the bottom of the screen, and
 * when.
 *
 * Three things can raise a notice and they share one strip: a low battery, a
 * newer firmware, and Nearby. They are gathered here rather than spread
 * through the runtime because the thing that actually matters about them is a
 * single decision -- which one wins, and how little of the panel is repainted
 * for it -- and that decision is unreadable when its three halves live in
 * three places.
 *
 * Nothing here repaints the screen, and since the strip moved to the bottom
 * nothing here repaints the header either. A notice arriving paints its own
 * strip. A notice leaving has the screen redraw that strip and no more. See
 * settleNotice().
 */
/* Painted when it first appears and when its words change -- not on every
 * frame, and no longer after every repaint of the screen either, because the
 * screen cannot draw on it. Repainting a strip at 50Hz for five seconds would
 * spend milliseconds a frame redrawing text that has not changed, and the
 * frame budget is 20ms for everything. */
/* The battery warning is driven by the percentage alone -- at or below 15%,
 * escalating at 5%. There is no charge verdict to consult any more, so on the
 * charger it clears once the reading climbs back over the threshold. */
/* Announce a newer firmware, once a day, to whoever is holding the device.
 *
 * Deliberately not admin-only. The person who can act on this is often not the
 * person playing, and a notice only the admin profile ever sees would be
 * invisible on a console that spends its life logged in as a young player -- which is
 * every console. So the wording carries the instruction instead: it names the
 * version and says who to ask, which is something a young player can act on
 * and an administrator can act on directly.
 *
 * The daily gate and the "have we already said this version" test both live in
 * Board, persisted, because both have to survive a power cycle -- see
 * Board::updateNoticeDue(). Nothing here decides when; it only draws.
 *
 * The string is composed once, when the banner is raised, and not rebuilt per
 * frame: the strip repaints from updateBanner_ for as long as it is up. It
 * measures 36 characters at the longest plausible version, inside the 40 the
 * strip fits at font 2 on a 320px panel, so it does not scroll -- and should
 * not be made to. Scrolling would mean repainting the strip on every frame
 * for five seconds, which is exactly what painting it once exists to avoid. */
void BrainoApp::tickUpdateNotice(uint32_t nowMs) {
    if (updateBannerActive_) {
        if (nowMs - updateBannerShownMs_ >= UPDATE_BANNER_MS) {
            updateBannerActive_ = false;
            requestBannerRepaint();
        }
        return;
    }
    if (!board_.updateNoticeDue()) return;

    snprintf(updateBanner_, sizeof(updateBanner_),
             "%s available - ask admin to update", board_.latestKnownVersion());
    updateBannerActive_ = true;
    updateBannerShownMs_ = nowMs;
    board_.markUpdateNoticeShown();
    requestBannerRepaint();
}

void BrainoApp::tickBatteryWarning(uint32_t nowMs) {
    if (batteryCheckMs_ != 0 && nowMs - batteryCheckMs_ < BATTERY_CHECK_MS) {
        return;
    }
    batteryCheckMs_ = nowMs;

    const uint8_t level = board_.isBatteryCritical() ? 2
                        : board_.isBatteryLow()      ? 1
                                                     : 0;

    if (level != batteryWarnLevel_) {
        batteryWarnLevel_ = level;
        /* Crossing a threshold -- in either direction -- restarts the cycle,
         * so going from low to critical says so at once instead of waiting out
         * the repeat interval left over from the milder warning. */
        batteryEverHidden_ = false;
        if (batteryBanner_ != nullptr) {
            batteryBanner_ = nullptr;
            requestBannerRepaint();
        }
    }

    if (level == 0) {
        if (batteryBanner_ != nullptr) {
            batteryBanner_ = nullptr;
            requestBannerRepaint();
        }
        return;
    }

    if (batteryBanner_ != nullptr) {
        if (nowMs - batteryShownMs_ >= BATTERY_BANNER_MS) {
            batteryBanner_ = nullptr;
            batteryHiddenMs_ = nowMs;
            batteryEverHidden_ = true;
            requestBannerRepaint();
        }
        return;
    }

    if (batteryEverHidden_ && nowMs - batteryHiddenMs_ < BATTERY_REPEAT_MS) {
        return;
    }
    batteryBanner_ = (level == 2) ? "Battery empty - plug in the charger"
                                  : "Battery low - time to charge";
    batteryShownMs_ = nowMs;
    requestBannerRepaint();
}

/* Asking for the strip to be looked at again: a notice has appeared, changed
 * its words, or gone. Nothing else is asked for.
 *
 * This has asked for less each time it was looked at. First it called
 * requestRender(), so the battery warning wiped the whole panel twice per
 * cycle, once to appear and once to leave, for as long as the cell stayed low.
 * Then it asked for the chrome, because the strip was painted over the header
 * and the header had to come back from underneath it. Along the bottom it is
 * over no chrome at all, and settleNotice() gives the screen back exactly the
 * strip that was covered. */
void BrainoApp::requestBannerRepaint() {
    bannerNeedsPaint_ = true;
}

/* Priority order, and it is not arbitrary. A flat battery is about to end the
 * session whatever else is true; an update is a standing condition that will
 * still be there in five seconds; a poke is somebody waiting. The console's
 * own notice goes first only because somebody at a cable has just asked to
 * see it. */
const char* BrainoApp::noticeText() const {
    return benchNoticeActive_           ? benchNotice_
         : (batteryBanner_ != nullptr)  ? batteryBanner_
         : updateBannerActive_          ? updateBanner_
                                        : NearbyPlay::banner();
}

/* Put the notice on the panel, change its words, or take it away. Called once
 * a frame, after the screen has drawn.
 *
 * WHILE A NOTICE IS UP, ITS STRIP IS ITS OWN. There is no framebuffer, so a
 * strip painted over part of a screen is not on top of anything: the next
 * thing the screen draws there replaces it. The answer used to be to paint
 * the notice again after every repaint of the screen, which flickers on any
 * screen that draws often and still loses to a frame drawn outside the loop
 * (launch() draws a new screen's first one itself). So the renderer is given
 * a paint clip that leaves the strip out (Ui::TftRenderer::setPaintClip) for
 * as long as the notice is on the panel. The screen, its chrome, the first
 * frame of a screen just launched and the d-pad's selector all draw as they
 * always do, and none of it lands on the notice. It is painted once, and
 * again only when its words change.
 *
 * TAKING IT AWAY REPAINTS THE STRIP AND NOTHING ELSE. What was under it has to
 * come back, and only the screen knows what that was. So the screen is asked
 * for a full render with the clip turned round -- the strip, and only the
 * strip. The rows that were covered are redrawn and the rest of the panel is
 * not touched, where a plain full repaint would blank all of it every time a
 * notice left. CLAUDE.md's rule is that a full repaint is earned, and a strip
 * going away has not earned one.
 *
 * Returns true when the strip changed hands, in either direction. Whatever is
 * drawn over the screen afterwards -- the d-pad's selector, on a board that
 * draws its own -- has to look again: the control it was on may be under a
 * notice now, or a ring that was cut off at the strip's edge may have the
 * rest of its control back. */
bool BrainoApp::settleNotice() {
    const char* text = noticeText();
    const Rect strip = Ui::notificationRect(renderer_.width(), renderer_.height());
    if (text == nullptr) {
        bannerNeedsPaint_ = false;
        if (!noticeOnPanel_) {
            return false;
        }
        noticeOnPanel_ = false;
        if (activeGame_ == nullptr) {
            renderer_.clearPaintClip();
            return false;
        }
        renderer_.setPaintClip(strip.x, strip.y, strip.w, strip.h);
        /* The screen's own render has already run this frame and cleared its
         * flags, so nothing it had pending is lost by asking for this one. The
         * header is outside the clip and is not redrawn, which is why
         * chromeNeedsPaint_ is left exactly as it was. */
        activeGame_->requestRender();
        beginScreenPaint();
        activeGame_->render(*this);
        activeGame_->clearDirty();
        renderer_.clearPaintClip();
        return true;
    }
    if (noticeOnPanel_ && !bannerNeedsPaint_) {
        return false;
    }
    bannerNeedsPaint_ = false;
    renderer_.clearPaintClip();
    beginScreenPaint();
    Ui::drawNotification(renderer_, text);
    renderer_.setPaintClip(0, 0, strip.w, strip.y);
    noticeOnPanel_ = true;
    return true;
}

/* Whatever was on the panel is about to stop being there: the saver, sleep or
 * the lock screen is taking all of it, or the panel is turning. The notice
 * goes with it, and its clip has to, because a clip left set would keep the
 * saver or the lock screen out of a strip that is nobody's any more. If the
 * notice is still due when a screen is back, settleNotice() paints it again,
 * since it is no longer on the panel. */
void BrainoApp::noticeOffPanel() {
    renderer_.clearPaintClip();
    noticeOnPanel_ = false;
}

/* A press on the notice. Whichever one is showing is put away as if its time
 * had run out, so the battery warning still comes back after its interval for
 * as long as the cell stays low: pressing it is "I have seen this", not "stop
 * telling me". */
void BrainoApp::dismissNotice(uint32_t nowMs) {
    if (benchNoticeActive_) {
        benchNoticeActive_ = false;
    } else if (batteryBanner_ != nullptr) {
        batteryBanner_ = nullptr;
        batteryHiddenMs_ = nowMs;
        batteryEverHidden_ = true;
    } else if (updateBannerActive_) {
        updateBannerActive_ = false;
    } else {
        NearbyPlay::dismissBanner(nowMs);
    }
    requestBannerRepaint();
}

/* `notice "<words>"` on the serial console: those words as a notice, for as
 * long as any other. There is no other way to see one when it is wanted -- a
 * battery has to run down, a newer firmware has to exist, another console has
 * to be in the room -- and where the strip lands, what it covers and how the
 * screen comes back from under it are things to look at on a panel, on every
 * board. Empty words take it down again. */
void BrainoApp::cmdNotice(int, char** argv, Print& out) {
    snprintf(benchNotice_, sizeof(benchNotice_), "%s", argv[0]);
    benchNoticeActive_ = benchNotice_[0] != '\0';
    benchNoticeShownMs_ = millis();
    requestBannerRepaint();
    out.println(benchNoticeActive_ ? "ok notice=\"shown\"" : "ok notice=\"cleared\"");
}

void BrainoApp::tickBenchNotice(uint32_t nowMs) {
    if (benchNoticeActive_ && nowMs - benchNoticeShownMs_ >= BENCH_NOTICE_MS) {
        benchNoticeActive_ = false;
        requestBannerRepaint();
    }
}
