#include "AppRuntime.h"

#include <string.h>
#include "AppVersion.h"
#include "engine/NearbyPlay.h"
#include "hal/BleBeacon.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"
#include "ui/LauncherIcons.h"
#include "ui/ScaledRenderer.h"
#include "ui/LauncherLayout.h"

namespace {
void copyFittedText(Ui::Renderer& tft, const char* source, char* dest, size_t cap,
                    int16_t maxW, uint8_t font) {
    if (cap == 0) {
        return;
    }
    snprintf(dest, cap, "%s", source != nullptr ? source : "");
    while (dest[0] != '\0' && strlen(dest) > 2 && tft.textWidth(dest, font) > maxW) {
        dest[strlen(dest) - 1] = '\0';
    }
}
}

uint8_t BrainoApp::launcherEntryCount() {
    return appVisibleCount(board_);
}

uint8_t BrainoApp::launcherPageSize() {
    return LauncherLayout::pageSize(board_.layoutMode());
}

const AppDefinition& BrainoApp::launcherEntry(uint8_t filteredIndex) {
    return appVisibleAt(board_, filteredIndex);
}

void BrainoApp::openApp(const AppDefinition& app) {
    /* Opening a tile. This is the console's one confirmation noise and it is
     * deliberately here rather than in launch(): launch() is also how the
     * runtime returns to a screen after a lock or a wake, and a chirp on
     * waking a device that has been in a bag is not a confirmation of
     * anything. */
    board_.playSound(Sound::Select);
    launch(app);
}

void BrainoApp::launch(const AppDefinition& app) {
    leaveActiveGame();

    activeGame_ = &app.game(games_);
    view_ = View::Game;
    activeApp_ = &app;
    /* Only playable games are announced. Opening Settings or System Info is
     * nobody else's business, and a system screen has no score to share. */
    NearbyPlay::setActiveApp(board_, app.isCatalogApp() ? &app : nullptr);
    Watchdog::setContext(app.title());
    applyRotation(rotationForActiveScreen());
    heapAtLaunch_ = ESP.getFreeHeap();
    activeGame_->begin(*this);
    activeGame_->render(*this);
    activeGame_->clearDirty();
}

const char* LauncherGame::title() const {
    return "Launcher";
}

void LauncherGame::begin(GameHost& host) {
    host.content().scan();
    clampPage(host);
    markFullDirty();
}

void LauncherGame::clampPage(GameHost& host) {
    const uint8_t pageSize = host.launcherPageSize();
    const uint8_t pages = max<uint8_t>(1, (host.launcherEntryCount() + pageSize - 1) / pageSize);
    if (page_ >= pages) {
        page_ = static_cast<uint8_t>(pages - 1);
    }
}

void LauncherGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    Board& board = host.board();
    const int16_t lW = host.display().width();
    const int16_t lH = host.display().height();
    const Board::LayoutMode mode = board.layoutMode();

    const Rect profileBtn = LauncherLayout::profileRect(mode, lW);
    if (profileBtn.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.openProfiles();
        return;
    }

    const Rect gearBtn = LauncherLayout::gearRect(mode, lW);
    if (gearBtn.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.openSettings();
        return;
    }

    const uint8_t pageSize = host.launcherPageSize();
    const uint8_t pages = max<uint8_t>(1, (host.launcherEntryCount() + pageSize - 1) / pageSize);
    const int16_t pY = static_cast<int16_t>(lH - 28);
    if (Rect{8, pY, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ > 0) {
        --page_;
        host.playSound(Sound::Tap);
        markDirty();
        return;
    }
    if (Rect{static_cast<int16_t>(lW - 82), pY, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
        page_ + 1 < pages) {
        ++page_;
        host.playSound(Sound::Tap);
        markDirty();
        return;
    }

    const uint8_t start = page_ * pageSize;
    const uint8_t count = host.launcherEntryCount();
    for (uint8_t slot = 0; slot < pageSize; ++slot) {
        const uint8_t index = start + slot;
        if (index >= count) {
            break;
        }
        if (LauncherLayout::tileRect(slot, mode, lW, lH).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.openApp(host.launcherEntry(index));
            return;
        }
    }
}

void LauncherGame::render(GameHost& host) {
    clampPage(host);

    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t lW = static_cast<int16_t>(tft.width());
    const int16_t lH = static_cast<int16_t>(tft.height());
    const Board::LayoutMode mode = board.layoutMode();
    const bool tall = (mode == Board::LayoutMode::Vertical);
    const int16_t headerH = LauncherLayout::headerHeight(mode);
    const Rect gearBtn = LauncherLayout::gearRect(mode, lW);
    const Rect profileBtn = LauncherLayout::profileRect(mode, lW);

    Ui::clear(tft);
    tft.fillRect(0, 0, lW, headerH, Ui::surface());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(ML_DATUM);

    if (tall) {
        /* Title left, copyright right, on one row.
         *
         * The title used to be centred and the copyright was not drawn at all;
         * squeezed into the 68..78 gap under the badges it was legible but
         * cramped. Sharing the title row is the only placement with real air
         * around it, and it costs the centred title -- worth it, and it now
         * matches both the landscape launcher and the Profiles header.
         *
         * At 240px: "Braino!" at font 4 runs from x=10 to about x=80, the
         * copyright at font 1 is ~108px right-aligned to x=232, so it starts
         * near x=124. Roughly 44px of air between them. That slack is the whole
         * budget -- a longer product name eats it, so re-measure this row before
         * changing BRAINO_PRODUCT_NAME. Nothing else uses y=4..30 in portrait;
         * the gear sits at y=48..72. */
        tft.setTextDatum(ML_DATUM);
        tft.drawString(BRAINO_PRODUCT_NAME, 10, 17, 4);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.setTextDatum(MR_DATUM);
        tft.drawString(BRAINO_COPYRIGHT_SHORT, static_cast<int16_t>(lW - 8), 17, 1);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(Ui::fitted(tft, board.profileName(board.activeProfile()),
                                  profileBtn.w, 2),
                       static_cast<int16_t>(profileBtn.x + 2),
                       static_cast<int16_t>(profileBtn.y + profileBtn.h / 2), 2);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(ML_DATUM);
        tft.drawString(Clock::timeText(), 8, 60, 2);
        /* Left to right off the clock. The battery badge is variable width now,
         * so the BLE badge after it is placed off the measured size rather than
         * a fixed +70 that assumed a 15px battery. */
        const int16_t bx = static_cast<int16_t>(8 + tft.textWidth(Clock::timeText(), 2) + 10);
        const int8_t battPct = board.getBatteryPercent();
        const Ui::PowerHint battPower = Ui::powerHint(board);
        const int16_t battW = Ui::batteryBadgeWidth(tft, battPct, battPower);
        const int16_t battLeft = static_cast<int16_t>(bx + 40);
        Ui::drawSyncBadge(tft, static_cast<int16_t>(bx + 6), 60, Clock::synced(), Ui::surface());
        Ui::drawWifiBadge(tft, static_cast<int16_t>(bx + 26), 60, Ui::surface());
        Ui::drawBatteryBadge(tft, static_cast<int16_t>(battLeft + battW / 2), 60,
                             battPct, battPower, Ui::surface());
        if (BleBeacon::active()) {
            Ui::drawBleBadge(tft, static_cast<int16_t>(battLeft + battW + 11), 60,
                             Ui::surface());
        }
        tft.drawFastHLine(8, 30, static_cast<int16_t>(lW - 16), Ui::shade(Ui::surface(), 150));

    } else {
        tft.drawString(BRAINO_PRODUCT_NAME, 10, 16, 4);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(BRAINO_COPYRIGHT_SHORT, 10, 38, 1);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(Ui::fitted(tft, board.profileName(board.activeProfile()),
                                  profileBtn.w, 1),
                       static_cast<int16_t>(profileBtn.x),
                       static_cast<int16_t>(profileBtn.y + profileBtn.h / 2), 1);

        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(MR_DATUM);
        tft.drawString(Clock::timeText(), static_cast<int16_t>(lW - 40), 14, 1);
        if (BleBeacon::active()) {
            const int16_t clockLeft =
                static_cast<int16_t>(lW - 40 - tft.textWidth(Clock::timeText(), 1));
            Ui::drawBleBadge(tft, max<int16_t>(static_cast<int16_t>(lW - 104),
                                               static_cast<int16_t>(clockLeft - 14)),
                             14, Ui::surface());
        }
        /* Packed to the pixel, and now measured rather than assumed. The row
         * runs from the hairline at lW-138 to the gear at lW-30, and carries
         * the Lock badge at its left-hand end. In the widest state -- "100"
         * while charging, 35px -- the three status badges plus their gaps come
         * to 81px, the padlock and its gap take another 25, and what is left
         * is a few pixels. Anything else that wants to live on this row has to
         * earn it. The hairline has moved out twice, lW-110 to lW-116 to lW-138,
         * and profileRect()'s right limit moved with it both times. */
        const int8_t battPct = board.getBatteryPercent();
        const Ui::PowerHint battPower = Ui::powerHint(board);
        const int16_t battW = Ui::batteryBadgeWidth(tft, battPct, battPower);
        const int16_t battRight = static_cast<int16_t>(lW - 36);
        const int16_t wifiCx = static_cast<int16_t>(battRight - battW - 6 - 8);
        const int16_t syncCx = static_cast<int16_t>(wifiCx - 8 - 6 - 6);
        Ui::drawSyncBadge(tft, syncCx, 34, Clock::synced(), Ui::surface());
        Ui::drawWifiBadge(tft, wifiCx, 34, Ui::surface());
        Ui::drawBatteryBadge(tft, static_cast<int16_t>(battRight - battW / 2), 34,
                             battPct, battPower, Ui::surface());
        tft.drawFastVLine(static_cast<int16_t>(lW - 138), 8, 32, Ui::outline());
    }
    Ui::drawGearIcon(tft, gearBtn, Ui::text());
    /* The launcher draws no top bar, so it carries its own Lock button. The
     * tap is consumed by the runtime before update() ever sees it, which is
     * why nothing here hit-tests it. */
    Ui::drawLockIcon(tft, LauncherLayout::lockRect(mode, lW), Ui::text(),
                     Ui::surface());

    const uint8_t pageSize = host.launcherPageSize();
    const uint8_t start = page_ * pageSize;
    const uint8_t total = host.launcherEntryCount();
    for (uint8_t slot = 0; slot < pageSize; ++slot) {
        const uint8_t index = start + slot;
        if (index >= total) {
            break;
        }
        const AppDefinition& entry = host.launcherEntry(index);
        const Rect r = LauncherLayout::tileRect(slot, mode, lW, lH);
        const uint16_t fill = slot % 3 == 0 ? Ui::rgb(36, 132, 204)
                            : (slot % 3 == 1 ? Ui::rgb(45, 154, 96)
                                             : Ui::rgb(222, 83, 83));
        Ui::drawButton(tft, r, "", fill, TFT_DARKGREY, TFT_WHITE);

        /* Place the contents as fractions of the tile, and scale them with it.
         *
         * These offsets used to be constants -- icon at r.x + 24, title at
         * r.y + 17 -- which described the old 145x46 tile and nothing else.
         * Once the grid started dividing the real panel, the tiles grew and
         * their contents stayed put in the top-left corner of them, which
         * looked worse than the un-scaled grid had: a big empty box with small
         * writing in it. The fractions below are the old constants divided by
         * the old tile size, so the 2.8-inch board gets what it always got.
         *
         * The text scale is the whole reason this board is here. A larger
         * panel that renders the same small label is no easier for the players
         * this is meant to serve; the tile has to carry bigger writing, not
         * just more space around it. */
        const float sx = static_cast<float>(r.w) / (tall ? 108.0f : 145.0f);
        const float sy = static_cast<float>(r.h) / (tall ? 96.0f : 46.0f);
        const float s = sx < sy ? sx : sy;

        /* Ask for the larger font, then check it actually fits -- and take the
         * smaller one when it does not.
         *
         * Deciding on the tile's growth alone was wrong in the wide layout.
         * A tall tile gives the label the full tile width, but a wide one
         * spends a third of it on the icon, so the same 2x that reads well
         * in portrait truncates "Multiplication" to a few characters in
         * landscape. The tile being bigger says the font may be bigger; only
         * measuring the actual string says whether it is. Long names fall back
         * on their own, per tile, with no list of exceptions to maintain. */
        const int16_t titleRoom = static_cast<int16_t>(tall ? r.w - 8 : r.w * 0.66f - 8);
        uint8_t textScale = s >= 1.45f ? 2 : 1;
        if (textScale > 1) {
            tft.setTextSize(2);
            if (tft.textWidth(entry.title(), 2) > titleRoom) {
                textScale = 1;
            }
            tft.setTextSize(1);
        }

        /* Icons scale by WHOLE numbers only, or not at all.
         *
         * The tile grows by a fraction -- about 1.37 in portrait here -- and
         * feeding that to the icon art was wrong. These are drawn shapes, not
         * bitmaps, but they are built from small integers: 1px strokes, a 17px
         * radius, triangle vertices a dozen pixels from centre. Multiply those
         * by 1.37 and each rounds independently: strokes land on 1px in one
         * place and 2px in another, outlines lose corners, and any lettering
         * inside stays 1x while its box grows. Tic-Tac-Toe came through it
         * intact because its features are coarse -- a 34px plate and 30px
         * strokes have room to absorb a rounding error -- which is exactly why
         * it looked fixed while the detailed icons did not.
         *
         * So: an integer multiple when one fits, otherwise native size, and
         * centred in the tile either way. The same rule the flags follow. A
         * crisp icon in a roomy tile reads better than a smeared one that
         * fills it. */
        const uint8_t iconScale = (s >= 2.0f) ? 2 : 1;
        Ui::ScaledRenderer icon{tft};
        icon.setScale(static_cast<float>(iconScale), static_cast<float>(iconScale));

        char label[24];
        if (tall) {
            const int16_t cxT = static_cast<int16_t>(r.x + r.w / 2);
            const int16_t iconY = static_cast<int16_t>(r.y + r.h * 0.3125f);
            drawLauncherIcon(icon, entry.icon(), r, fill,
                             static_cast<int16_t>(cxT / iconScale),
                             static_cast<int16_t>(iconY / iconScale));
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(MC_DATUM);
            tft.setTextSize(textScale);
            copyFittedText(tft, entry.title(), label, sizeof(label),
                           static_cast<int16_t>(r.w - 8), 2);
            tft.drawString(label, cxT, static_cast<int16_t>(r.y + r.h * 0.708f), 2);
            tft.setTextColor(Ui::rgb(235, 245, 255), fill);
            copyFittedText(tft, entry.subtitle(), label, sizeof(label),
                           static_cast<int16_t>(r.w - 8), 1);
            tft.drawString(label, cxT, static_cast<int16_t>(r.y + r.h * 0.906f), 1);
            tft.setTextSize(1);
        } else {
            /* Everything hangs off the tile's own centre line.
             *
             * The first version kept the old proportions -- icon at 0.478 of
             * the height, title at 0.37, subtitle at 0.74 -- which were fine
             * in a 46px tile and drift visibly in a 69px one: the pair sits
             * low, and the icon a shade above it, so nothing lines up with
             * anything. Measuring the text block from the middle outwards
             * keeps it centred at any tile height, and puts the icon on the
             * same line rather than near it. */
            const int16_t midY = static_cast<int16_t>(r.y + r.h / 2);
            const int16_t iconX = static_cast<int16_t>(r.x + r.w * 0.18f);
            drawLauncherIcon(icon, entry.icon(), r, fill,
                             static_cast<int16_t>(iconX / iconScale),
                             static_cast<int16_t>(midY / iconScale));

            /* Title above the line and subtitle below it, each by half its own
             * height, so the gap between them does not grow with the tile. */
            const int16_t titleH = static_cast<int16_t>(16 * textScale);
            const int16_t subH = static_cast<int16_t>(8 * textScale);
            const int16_t textX = static_cast<int16_t>(r.x + r.w * 0.34f);
            const int16_t textW = static_cast<int16_t>(r.w - (r.x + r.w * 0.34f - r.x) - 8);

            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(ML_DATUM);
            tft.setTextSize(textScale);
            copyFittedText(tft, entry.title(), label, sizeof(label), textW, 2);
            tft.drawString(label, textX, static_cast<int16_t>(midY - subH / 2 - 2), 2);
            tft.setTextColor(Ui::rgb(235, 245, 255), fill);
            copyFittedText(tft, entry.subtitle(), label, sizeof(label), textW, 1);
            tft.drawString(label, textX, static_cast<int16_t>(midY + titleH / 2 + 2), 1);
            tft.setTextSize(1);
        }
    }

    const uint8_t pages = max<uint8_t>(1, (total + pageSize - 1) / pageSize);
    if (pages > 1) {
        const int16_t pY = static_cast<int16_t>(lH - 28);
        Ui::drawPagerButton(tft, Rect{8, pY, 74, 24}, "Prev", page_ > 0);
        Ui::drawPagerButton(tft, Rect{static_cast<int16_t>(lW - 82), pY, 74, 24},
                            "Next", page_ + 1 < pages);
        char pageText[8];
        snprintf(pageText, sizeof(pageText), "%u/%u", page_ + 1, pages);
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(pageText, lW / 2, static_cast<int16_t>(pY + 12), 2);
    }
    tft.setTextDatum(TL_DATUM);
}
