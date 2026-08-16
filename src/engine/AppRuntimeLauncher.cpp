#include "AppRuntime.h"

#include <string.h>
#include "AppVersion.h"
#include "engine/NearbyPlay.h"
#include "hal/BleBeacon.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"
#include "ui/LauncherIcons.h"
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

uint8_t KidsPlatformApp::launcherEntryCount() {
    return appVisibleCount(board_);
}

uint8_t KidsPlatformApp::launcherPageSize() {
    return LauncherLayout::pageSize(board_.layoutMode());
}

const AppDefinition& KidsPlatformApp::launcherEntry(uint8_t filteredIndex) {
    return appVisibleAt(board_, filteredIndex);
}

void KidsPlatformApp::openApp(const AppDefinition& app) {
    launch(app);
}

void KidsPlatformApp::launch(const AppDefinition& app) {
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
        markDirty();
        return;
    }
    if (Rect{static_cast<int16_t>(lW - 82), pY, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
        page_ + 1 < pages) {
        ++page_;
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
        if (LauncherLayout::tileRect(slot, mode).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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
        const int16_t bx = static_cast<int16_t>(8 + tft.textWidth(Clock::timeText(), 2) + 10);
        Ui::drawSyncBadge(tft, bx, 60, Clock::synced(), Ui::surface());
        Ui::drawWifiBadge(tft, static_cast<int16_t>(bx + 22), 60, Ui::surface());
        Ui::drawBatteryBadge(tft, static_cast<int16_t>(bx + 46), 60,
                             board.getBatteryPercent(),
                             board.getPowerSource() == Board::PowerState::EXTERNAL_POWER,
                             Ui::surface());
        if (BleBeacon::active()) {
            Ui::drawBleBadge(tft, static_cast<int16_t>(bx + 70), 60, Ui::surface());
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
        Ui::drawSyncBadge(tft, static_cast<int16_t>(lW - 92), 34, Clock::synced(), Ui::surface());
        Ui::drawWifiBadge(tft, static_cast<int16_t>(lW - 68), 34, Ui::surface());
        Ui::drawBatteryBadge(tft, static_cast<int16_t>(lW - 44), 34,
                             board.getBatteryPercent(),
                             board.getPowerSource() == Board::PowerState::EXTERNAL_POWER,
                             Ui::surface());
        tft.drawFastVLine(static_cast<int16_t>(lW - 110), 8, 32, Ui::outline());
    }
    Ui::drawGearIcon(tft, gearBtn, Ui::text());

    const uint8_t pageSize = host.launcherPageSize();
    const uint8_t start = page_ * pageSize;
    const uint8_t total = host.launcherEntryCount();
    for (uint8_t slot = 0; slot < pageSize; ++slot) {
        const uint8_t index = start + slot;
        if (index >= total) {
            break;
        }
        const AppDefinition& entry = host.launcherEntry(index);
        const Rect r = LauncherLayout::tileRect(slot, mode);
        const uint16_t fill = slot % 3 == 0 ? Ui::rgb(36, 132, 204)
                            : (slot % 3 == 1 ? Ui::rgb(45, 154, 96)
                                             : Ui::rgb(222, 83, 83));
        Ui::drawButton(tft, r, "", fill, TFT_DARKGREY, TFT_WHITE);

        char label[24];
        if (tall) {
            drawLauncherIcon(tft, entry.icon(), r, fill,
                             static_cast<int16_t>(r.x + r.w / 2),
                             static_cast<int16_t>(r.y + 30));
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(MC_DATUM);
            const int16_t cxT = static_cast<int16_t>(r.x + r.w / 2);
            copyFittedText(tft, entry.title(), label, sizeof(label),
                           static_cast<int16_t>(r.w - 8), 2);
            tft.drawString(label, cxT, static_cast<int16_t>(r.y + 68), 2);
            tft.setTextColor(Ui::rgb(235, 245, 255), fill);
            copyFittedText(tft, entry.subtitle(), label, sizeof(label),
                           static_cast<int16_t>(r.w - 8), 1);
            tft.drawString(label, cxT, static_cast<int16_t>(r.y + 87), 1);
        } else {
            drawLauncherIcon(tft, entry.icon(), r, fill,
                             static_cast<int16_t>(r.x + 24),
                             static_cast<int16_t>(r.y + 22));
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(ML_DATUM);
            copyFittedText(tft, entry.title(), label, sizeof(label),
                           static_cast<int16_t>(r.w - 54), 2);
            tft.drawString(label, r.x + 48, r.y + 17, 2);
            tft.setTextColor(Ui::rgb(235, 245, 255), fill);
            copyFittedText(tft, entry.subtitle(), label, sizeof(label),
                           static_cast<int16_t>(r.w - 54), 1);
            tft.drawString(label, r.x + 48, r.y + 34, 1);
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
