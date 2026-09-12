#include "AppRuntime.h"

#include "engine/NearbyPlay.h"

/* The header notification strip -- what interrupts the top bar, and when.
 *
 * Three things can raise a banner and they share one 320x30 strip: a low
 * battery, a newer firmware, and Nearby. They are gathered here rather than
 * spread through the runtime because the thing that actually matters about
 * them is a single decision -- which one wins, and how rarely the strip is
 * allowed to repaint -- and that decision is unreadable when its three halves
 * live in three places.
 *
 * Nothing here repaints the screen. Every path ends at requestChromeRender(),
 * which repaints the top 30 rows; a full 240-row repaint to change a line of
 * text is what this whole mechanism exists to avoid.
 */
/* Painted when it first appears and again whenever the screen underneath has
 * just redrawn over it -- not on every frame. Repainting a 320x30 strip at
 * 50Hz for five seconds would spend milliseconds a frame redrawing text that
 * has not changed, and the frame budget is 20ms for everything. */
/* The battery warning is driven by the percentage alone -- at or below 15%,
 * escalating at 5%. There is no charge verdict to consult any more, so on the
 * charger it clears once the reading climbs back over the threshold. */
/* Announce a newer firmware, once a day, to whoever is holding the device.
 *
 * Deliberately not admin-only. The person who can act on this is often not the
 * person playing, and a notice only the admin profile ever sees would be
 * invisible on a console that spends its life logged in as a child -- which is
 * every console. So the wording carries the instruction instead: it names the
 * version and says who to ask, which is something a seven-year-old can act on
 * and an adult can act on directly.
 *
 * The daily gate and the "have we already said this version" test both live in
 * Board, persisted, because both have to survive a power cycle -- see
 * Board::updateNoticeDue(). Nothing here decides when; it only draws.
 *
 * The string is composed once, when the banner is raised, and not rebuilt per
 * frame: the strip repaints from updateBanner_ for as long as it is up. It
 * measures 36 characters at the longest plausible version, inside the 40 the
 * top bar fits at font 2, so it does not scroll -- and should not be made to.
 * Scrolling would mean repainting the chrome strip on every frame for five
 * seconds, which is exactly what Game::renderChrome() exists to avoid. */
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

/* The strip is painted over the screen's own header, so the header underneath
 * has to redraw before it can genuinely go away again -- the header, not the
 * screen. This used to call requestRender(), so the battery warning wiped the
 * whole panel twice per cycle, once to appear and once to leave, and repeated
 * that for as long as the cell stayed low. Nearby's banner had already moved
 * to the chrome path; this is the same fix for the battery and update notices.
 * A screen that cannot repaint its chrome alone still gets a full repaint,
 * through the fallback in loop(). */
void BrainoApp::requestBannerRepaint() {
    bannerNeedsPaint_ = true;
    requestChromeRender();
}

void BrainoApp::drawHeaderBanner(bool screenRepainted) {
    /* Priority order, and it is not arbitrary. A flat battery is about to end
     * the session whatever else is true; an update is a standing condition that
     * will still be there in five seconds; a poke is somebody waiting. */
    const char* text = (batteryBanner_ != nullptr)  ? batteryBanner_
                     : updateBannerActive_          ? updateBanner_
                                                    : NearbyPlay::banner();
    if (text == nullptr) {
        bannerNeedsPaint_ = false;
        return;
    }
    if (!bannerNeedsPaint_ && !screenRepainted) {
        return;
    }
    bannerNeedsPaint_ = false;
    Ui::drawNotification(renderer_, text);
}
