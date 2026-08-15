#include "AppRuntime.h"

#include "hal/BleBeacon.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"
#include "ui/LauncherIcons.h"
#include "ui/LauncherLayout.h"

uint8_t KidsPlatformApp::launcherEntryCount() {
    return appVisibleCount(board_);
}

void KidsPlatformApp::clampLauncherPage() {
    const uint8_t pageSize = launcherPageSize();
    const uint8_t pages = max<uint8_t>(1, (launcherEntryCount() + pageSize - 1) / pageSize);
    if (launcherPage_ >= pages) launcherPage_ = static_cast<uint8_t>(pages - 1);
}

uint8_t KidsPlatformApp::launcherPageSize() {
    return LauncherLayout::pageSize(board_.layoutMode());
}

const AppDefinition& KidsPlatformApp::launcherEntry(uint8_t filteredIndex) {
    return appVisibleAt(board_, filteredIndex);
}

void KidsPlatformApp::handleLauncherTouch(const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    const int16_t lW = static_cast<int16_t>(board_.display().width());
    const int16_t lH = static_cast<int16_t>(board_.display().height());
    const Board::LayoutMode mode = board_.layoutMode();
    const Rect profileBtn = LauncherLayout::profileRect(mode, lW);
    if (profileBtn.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        openProfiles();
        return;
    }
    const Rect gearBtn = LauncherLayout::gearRect(mode, lW);
    if (gearBtn.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        openSettings();
        return;
    }
    const uint8_t pageSize = launcherPageSize();
    const uint8_t pages = max<uint8_t>(1, (launcherEntryCount() + pageSize - 1) / pageSize);
    const int16_t pY = static_cast<int16_t>(lH - 28);
    if (Rect{8, pY, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) && launcherPage_ > 0) {
        --launcherPage_;
        launcherDirty_ = true;
        return;
    }
    if (Rect{static_cast<int16_t>(lW - 82), pY, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
        launcherPage_ + 1 < pages) {
        ++launcherPage_;
        launcherDirty_ = true;
        return;
    }
    const uint8_t start = launcherPage_ * pageSize;
    const uint8_t count = launcherEntryCount();
    for (uint8_t slot = 0; slot < pageSize; ++slot) {
        const uint8_t index = start + slot;
        if (index >= count) {
            break;
        }
        if (LauncherLayout::tileRect(slot, mode).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            launch(launcherEntry(index));
            return;
        }
    }
}

void KidsPlatformApp::launch(const AppDefinition& app) {
    leaveActiveGame();
    if (strcmp(app.id(), "profiles") == 0) {
        openProfiles();
        return;
    }

    activeGame_ = &app.game(games_);
    view_ = View::Game;
    activeApp_ = &app;
    Watchdog::setContext(app.title());
    applyRotation(effectiveRotation(!app.followsLayout ||
                                    board_.layoutMode() != Board::LayoutMode::Vertical));
    heapAtLaunch_ = ESP.getFreeHeap();
    activeGame_->begin(*this);
    activeGame_->render(*this);
    activeGame_->clearDirty();
}

void KidsPlatformApp::renderLauncher() {
    TFT_eSPI& tft = board_.display();
    const int16_t lW = static_cast<int16_t>(tft.width());
    const int16_t lH = static_cast<int16_t>(tft.height());
    const Board::LayoutMode mode = board_.layoutMode();
    const bool tall = (mode == Board::LayoutMode::Vertical);
    const int16_t headerH = LauncherLayout::headerHeight(mode);
    const Rect gearBtn = LauncherLayout::gearRect(mode, lW);
    const Rect profileBtn = LauncherLayout::profileRect(mode, lW);

    Ui::clear(tft);
    tft.fillRect(0, 0, lW, headerH, Ui::surface());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(ML_DATUM);

    if (tall) {
        tft.setTextDatum(MC_DATUM);
        tft.drawString("GoodTime Kids!", static_cast<int16_t>(lW / 2), 17, 4);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(Ui::fitted(tft, board_.profileName(board_.activeProfile()),
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
                             board_.getBatteryPercent(),
                             board_.getPowerSource() == Board::PowerState::EXTERNAL_POWER,
                             Ui::surface());
        if (BleBeacon::active()) {
            Ui::drawBleBadge(tft, static_cast<int16_t>(bx + 70), 60, Ui::surface());
        }
        tft.drawFastHLine(8, 30, static_cast<int16_t>(lW - 16), Ui::shade(Ui::surface(), 150));
    } else {
        tft.drawString("GoodTime Kids!", 10, 16, 4);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString("(C) GoodTime Micro", 10, 38, 1);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(Ui::fitted(tft, board_.profileName(board_.activeProfile()),
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
                             board_.getBatteryPercent(),
                             board_.getPowerSource() == Board::PowerState::EXTERNAL_POWER,
                             Ui::surface());
        tft.drawFastVLine(static_cast<int16_t>(lW - 110), 8, 32, Ui::outline());
    }
    Ui::drawGearIcon(tft, gearBtn, Ui::text());

    const uint8_t pageSize = launcherPageSize();
    const uint8_t start = launcherPage_ * pageSize;
    const uint8_t total = launcherEntryCount();
    for (uint8_t slot = 0; slot < pageSize; ++slot) {
        const uint8_t index = start + slot;
        if (index >= total) {
            break;
        }
        const AppDefinition& entry = launcherEntry(index);
        const Rect r = LauncherLayout::tileRect(slot, mode);
        const uint16_t fill = slot % 3 == 0 ? Ui::rgb(36, 132, 204)
                            : (slot % 3 == 1 ? Ui::rgb(45, 154, 96)
                                             : Ui::rgb(222, 83, 83));
        Ui::drawButton(tft, r, "", fill, TFT_DARKGREY, TFT_WHITE);

        if (tall) {
            drawLauncherIcon(tft, entry.icon, r, fill,
                             static_cast<int16_t>(r.x + r.w / 2),
                             static_cast<int16_t>(r.y + 30));
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(MC_DATUM);
            const int16_t cxT = static_cast<int16_t>(r.x + r.w / 2);
            String titleText = entry.title();
            while (titleText.length() > 2 && tft.textWidth(titleText, 2) > r.w - 8) {
                titleText.remove(titleText.length() - 1);
            }
            tft.drawString(titleText, cxT, static_cast<int16_t>(r.y + 68), 2);
            tft.setTextColor(Ui::rgb(235, 245, 255), fill);
            String subText = entry.subtitle();
            while (subText.length() > 2 && tft.textWidth(subText, 1) > r.w - 8) {
                subText.remove(subText.length() - 1);
            }
            tft.drawString(subText, cxT, static_cast<int16_t>(r.y + 87), 1);
        } else {
            drawLauncherIcon(tft, entry.icon, r, fill,
                             static_cast<int16_t>(r.x + 24),
                             static_cast<int16_t>(r.y + 22));
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(ML_DATUM);
            String titleText = entry.title();
            while (titleText.length() > 2 && tft.textWidth(titleText, 2) > r.w - 54) {
                titleText.remove(titleText.length() - 1);
            }
            tft.drawString(titleText, r.x + 48, r.y + 17, 2);
            tft.setTextColor(Ui::rgb(235, 245, 255), fill);
            String subText = entry.subtitle();
            while (subText.length() > 2 && tft.textWidth(subText, 1) > r.w - 54) {
                subText.remove(subText.length() - 1);
            }
            tft.drawString(subText, r.x + 48, r.y + 34, 1);
        }
    }

    const uint8_t pages = max<uint8_t>(1, (total + pageSize - 1) / pageSize);
    if (pages > 1) {
        const int16_t pY = static_cast<int16_t>(lH - 28);
        Ui::drawPagerButton(tft, Rect{8, pY, 74, 24}, "Prev", launcherPage_ > 0);
        Ui::drawPagerButton(tft, Rect{static_cast<int16_t>(lW - 82), pY, 74, 24},
                            "Next", launcherPage_ + 1 < pages);
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(String(launcherPage_ + 1) + "/" + pages, lW / 2,
                       static_cast<int16_t>(pY + 12), 2);
    }
    tft.setTextDatum(TL_DATUM);
}
