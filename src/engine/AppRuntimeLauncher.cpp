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

/* Everything above the tile grid. Extracted from render() so that a change
 * confined to the header -- the clock ticking over, the battery badge, a
 * notification arriving -- repaints the header alone instead of wiping the
 * panel and redrawing every tile with it. See Game::renderChrome().
 *
 * It paints its own background and takes nothing from the caller, so it is
 * correct over a live screen as well as over a freshly cleared one. */
void LauncherGame::drawHeader(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t lW = static_cast<int16_t>(tft.width());
    const Board::LayoutMode mode = board.layoutMode();
    const bool tall = (mode == Board::LayoutMode::Vertical);
    const int16_t headerH = LauncherLayout::headerHeight(mode);
    const Rect gearBtn = LauncherLayout::gearRect(mode, lW);
    const Rect profileBtn = LauncherLayout::profileRect(mode, lW);

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
}

bool LauncherGame::renderChrome(GameHost& host) {
    drawHeader(host);
    return true;
}

/* Background and header. Only on a full repaint -- paging does not touch it. */
void LauncherGame::renderStatic(GameHost& host) {
    Ui::clear(host.display());
    drawHeader(host);
    /* The panel has just been wiped, so no slot has a frame on it any more. */
    for (uint8_t i = 0; i < MAX_TILES; ++i) {
        slotHasButton_[i] = false;
    }
}

/* The tile grid and the pager. This is what next/previous changes, and it is
 * all next/previous should cost: the header above is left alone.
 *
 * Every tile repaints its own rect opaquely -- Ui::drawButton fills it and the
 * label is drawn with the tile colour as its text background -- so a tile
 * erases the tile that was there. The two things that do NOT self-erase are
 * handled explicitly below: a slot with no entry on it, and the pager. */
void LauncherGame::renderDynamic(GameHost& host) {
    clampPage(host);

    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t lW = static_cast<int16_t>(tft.width());
    const int16_t lH = static_cast<int16_t>(tft.height());
    const Board::LayoutMode mode = board.layoutMode();
    const bool tall = (mode == Board::LayoutMode::Vertical);

    /* Still true here: the runtime clears the dirty flags AFTER render, so
     * this distinguishes "the whole screen was just wiped" from "only the
     * content changed". */
    const bool fullPaint = needsFullRender();

    const uint8_t pageSize = host.launcherPageSize();
    const uint8_t start = page_ * pageSize;
    const uint8_t total = host.launcherEntryCount();

    /* One text size for the whole page, chosen so every label on it fits.
     *
     * Deciding per tile produced a grid of mixed sizes -- "Memory" large
     * beside "Tic-Tac-Toe" small -- which reads as a fault rather than as
     * fitting, because a grid implies its cells are alike. Worse, the test
     * only measured the title, so "Math" qualified for 2x and its subtitle
     * "addition & subtraction" was then quietly truncated to "addition &".
     *
     * So: measure BOTH strings of EVERY tile on the page, and take the larger
     * size only if all of them fit. One tile with a long name holds the page
     * to 1x, which is the right trade -- uniform and complete beats large and
     * clipped. */
    const Rect probe = LauncherLayout::tileRect(0, mode, lW, lH);
    const float probeS = [&]() {
        const float px = static_cast<float>(probe.w) / (tall ? 108.0f : 145.0f);
        const float py = static_cast<float>(probe.h) / (tall ? 96.0f : 46.0f);
        return px < py ? px : py;
    }();
    const int16_t titleRoom =
        static_cast<int16_t>(tall ? probe.w - 8 : probe.w * 0.66f - 8);

    uint8_t pageTextScale = probeS >= 1.45f ? 2 : 1;
    if (pageTextScale > 1) {
        tft.setTextSize(2);
        for (uint8_t slot = 0; slot < pageSize && pageTextScale > 1; ++slot) {
            const uint8_t index = start + slot;
            if (index >= total) {
                break;
            }
            const AppDefinition& e = host.launcherEntry(index);
            if (tft.textWidth(e.title(), 2) > titleRoom ||
                tft.textWidth(e.subtitle(), 1) > titleRoom) {
                pageTextScale = 1;
            }
        }
        tft.setTextSize(1);
    }

    for (uint8_t slot = 0; slot < pageSize; ++slot) {
        const uint8_t index = start + slot;
        if (index >= total) {
            /* Erase, do not skip. This used to `break`, which was correct only
             * because the whole screen had just been cleared. Paging from a
             * full page to a shorter last one now has to take the leftover
             * tiles off itself, or they stay on screen belonging to a page the
             * player is no longer on. */
            const Rect empty = LauncherLayout::tileRect(slot, mode, lW, lH);
            tft.fillRect(empty.x, empty.y, empty.w, empty.h, Ui::bg());
            if (slot < MAX_TILES) {
                slotHasButton_[slot] = false;
            }
            continue;
        }
        const AppDefinition& entry = host.launcherEntry(index);
        const Rect r = LauncherLayout::tileRect(slot, mode, lW, lH);
        const uint16_t fill = slot % 3 == 0 ? Ui::rgb(36, 132, 204)
                            : (slot % 3 == 1 ? Ui::rgb(45, 154, 96)
                                             : Ui::rgb(222, 83, 83));
        /* The button itself is invariant across pages -- its rect comes from
         * the slot and its colour from `slot % 3`, neither of which a page
         * change touches. Ui::drawButton pushes TWO full tile areas (a shadow
         * roundrect and the fill) plus a bevel and an outline, so redrawing it
         * to change what is written inside it is about half the cost of paging
         * thrown away.
         *
         * On a full repaint it still has to be drawn. On a content change only
         * the interior is erased -- one plain fillRect inside the border,
         * cheaper than either roundrect -- and the icon and label go back on
         * top. The erase is not optional: glyph widths and icon shapes differ
         * between entries, so drawing the new content over the old leaves
         * fragments of it behind. */
        /* The frame is only reusable if there IS one. A slot that was empty on
         * the page before this holds bare background, so skipping the chrome
         * would leave a square of tile colour with no rounded edge, no border
         * and no shadow -- which is what "the tiles overlap" looks like. The
         * last page is a partial one whenever the app count is not a multiple
         * of the page size, so paging to the end and back is all it takes. */
        const bool hasFrame = (slot < MAX_TILES) && slotHasButton_[slot];
        if (fullPaint || !hasFrame) {
            Ui::drawButton(tft, r, "", fill, TFT_DARKGREY, TFT_WHITE);
            if (slot < MAX_TILES) {
                slotHasButton_[slot] = true;
            }
        } else {
            tft.fillRect(static_cast<int16_t>(r.x + 2), static_cast<int16_t>(r.y + 2),
                         static_cast<int16_t>(r.w - 4), static_cast<int16_t>(r.h - 4),
                         fill);
        }

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

        const uint8_t textScale = pageTextScale;

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
        /* Cleared first, because the string can get NARROWER. drawString with
         * an opaque background only paints behind the glyphs it draws, so
         * "10/12" -> "9/12" would leave the last character of the old text on
         * screen. Harmless while a full clear preceded every paint; a real
         * artefact now that paging repaints this strip alone. */
        tft.fillRect(static_cast<int16_t>(lW / 2 - 40), pY,
                     80, 24, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(pageText, lW / 2, static_cast<int16_t>(pY + 12), 2);
    }
    tft.setTextDatum(TL_DATUM);
}
