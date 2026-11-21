// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AppRuntime.h"

#include <string.h>
#include "AppVersion.h"
#include "engine/NearbyPlay.h"
#include "hal/BleBeacon.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"
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

/* The player's name in the space profileRect() gives it, left-aligned and
 * centred on the rect's height: font 2 when the whole name fits, font 1 when
 * it does not. profileRect() is sized so that font 1 always holds a full
 * PROFILE_NAME_MAX name (6px a character), so a short name reads large and a
 * long one is made smaller rather than cut. The trim is only a backstop for a
 * panel too narrow for even that. */
void drawProfileName(Ui::Renderer& tft, const char* name, const Rect& r) {
    char text[Board::PROFILE_NAME_MAX + 1];
    const uint8_t font = tft.textWidth(name, 2) <= r.w ? 2 : 1;
    copyFittedText(tft, name, text, sizeof(text), r.w, font);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(text, r.x, static_cast<int16_t>(r.y + r.h / 2), font);
}

/* The header's status group, in both orientations, from LauncherLayout's one
 * packing of it: the clock with its sync check (and the gear, for the admin)
 * on the top row; the rune, padlock, speaker, Wi-Fi and battery on the badge
 * row; and the hairline at the group's left edge. Only what is there is
 * drawn, and LauncherLayout has already closed up the space of what is not.
 * The clock's place is kept for renderClock()'s minute repaint. */
void drawStatusGroup(Ui::Renderer& tft, Board& board, Board::LayoutMode mode, int16_t lW,
                     int16_t& clockX, int16_t& clockY, int16_t& clockW) {
    const int16_t topCy = LauncherLayout::statusTopCy(mode);
    const int16_t badgeCy = LauncherLayout::statusBadgeCy(mode);

    Ui::drawSyncBadge(tft, LauncherLayout::syncCx(mode, lW), topCy, Clock::synced(), Ui::surface());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(MR_DATUM);
    clockX = LauncherLayout::clockRight(mode, lW);
    clockY = topCy;
    clockW = tft.textWidth(Clock::timeText(), 2);
    tft.drawString(Clock::timeText(), clockX, topCy, 2);
    if (Ui::settingsShown()) Ui::drawGearIcon(tft, LauncherLayout::gearRect(mode, lW), Ui::text());

    if (BleBeacon::active()) {
        Ui::drawBleBadge(tft, LauncherLayout::bleCx(mode, lW), badgeCy, Ui::surface());
    }
    /* Padlock and speaker: drawn here, but their taps are taken by the runtime
     * before update() ever sees them. The speaker is absent on a board with
     * none, where it would be a control for nothing. */
    Ui::drawLockIcon(tft, LauncherLayout::lockRect(mode, lW), Ui::text(), Ui::surface());
    if (Board::hasSound()) {
        Ui::drawSpeakerIcon(tft, LauncherLayout::speakerRect(mode, lW), !board.soundEnabled(),
                            Ui::text());
    }
    Ui::drawWifiBadge(tft, LauncherLayout::wifiCx(mode, lW), badgeCy, Ui::surface());
    /* Left-aligned in its slot: a narrower reading gives its room back at the
     * panel's edge, not as a gap beside Wi-Fi. Width 0 -- no battery sense, or
     * no usable reading -- draws nothing. */
    const int8_t battPct = board.getBatteryPercent();
    const int16_t battW = Ui::batteryBadgeWidth(tft, battPct);
    if (battW > 0) {
        Ui::drawBatteryBadge(tft, static_cast<int16_t>(LauncherLayout::batteryLeft(mode, lW) + battW / 2),
                             badgeCy, battPct, Ui::surface());
    }

    const int16_t dividerTop = LauncherLayout::headerDividerTop(mode);
    tft.drawFastVLine(LauncherLayout::headerDividerX(mode, lW), dividerTop,
                      static_cast<int16_t>(LauncherLayout::headerDividerBottom(mode) - dividerTop),
                      Ui::outline());
}
}

uint8_t BrainoApp::launcherEntryCount() {
    return appVisibleCount(board_);
}

/* Against the live panel, because the portrait grid depends on its size. The
 * launcher's rotation is applied before its begin(), so this is always the
 * orientation the page is about to be drawn in. */
uint8_t BrainoApp::launcherPageSize() {
    return LauncherLayout::pageSize(board_.layoutMode(),
                                    static_cast<int16_t>(renderer_.width()),
                                    static_cast<int16_t>(renderer_.height()));
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
    /* Every way into an app comes through here -- a tile, the gear, a button
     * on another screen, the d-pad's selector, a return from the lock -- so
     * this is the one place an admin-only app is refused to anyone else.
     * Hiding the tile and the gear is what the player sees; this is what
     * holds if some route was missed. Nothing changes: the screen that was
     * up stays up. */
    if (!app.availableTo(board_)) {
        Serial.printf("[apps] '%s' is the admin's: not opened for this player\n", app.id());
        return;
    }
    leaveActiveGame();

    /* A game this player left with something in it is still in memory:
     * bring it back, or ask, before begin() throws it away. See
     * Game::keepsGame() and AppRuntimeKeep.cpp. */
    switch (keptHow(app)) {
        case Game::Keep::Ask:
            askCarryOn(app);
            return;
        case Game::Keep::Quietly:
            resumeKept(app);
            return;
        case Game::Keep::No:
            break;
    }
    enterApp(app, false, 0);
}

const char* LauncherApp::title() const {
    return "Launcher";
}

void LauncherApp::begin(GameHost& host) {
    host.content().scan();
    clampPage(host);
    markFullDirty();
}

void LauncherApp::clampPage(GameHost& host) {
    const uint8_t pageSize = host.launcherPageSize();
    const uint8_t pages = max<uint8_t>(1, (host.launcherEntryCount() + pageSize - 1) / pageSize);
    if (page_ >= pages) {
        page_ = static_cast<uint8_t>(pages - 1);
    }
}

void LauncherApp::update(GameHost& host, const TouchPoint& touch) {
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
    if (Ui::settingsShown() && gearBtn.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.openSettings();
        return;
    }

    const uint8_t pageSize = host.launcherPageSize();
    const uint8_t pages = max<uint8_t>(1, (host.launcherEntryCount() + pageSize - 1) / pageSize);
    const int16_t pY = static_cast<int16_t>(lH - 28);
    if (Rect{8, pY, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ > 0) {
        --page_;
        host.playSound(Sound::Tap);
        requestGridPaint();
        return;
    }
    if (Rect{static_cast<int16_t>(lW - 82), pY, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) &&
        page_ + 1 < pages) {
        ++page_;
        host.playSound(Sound::Tap);
        requestGridPaint();
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
/* The launcher's clock alone, at the minute -- see Game::renderClock(). Only
 * at the same width: in portrait the sync check sits after the time, and a
 * wider or narrower time would have to move it, which is the header's job. */
bool LauncherApp::renderClock(GameHost& host) {
    Ui::Renderer& tft = host.display();
    if (static_cast<int16_t>(tft.width()) != clockScreenW_ ||
        tft.textWidth(Clock::timeText(), 2) != clockW_) {
        return false;
    }
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(clockDatum_);
    tft.drawString(Clock::timeText(), clockX_, clockY_, 2);
    tft.setTextDatum(TL_DATUM);
    return true;
}

void LauncherApp::drawHeader(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t lW = static_cast<int16_t>(tft.width());
    const Board::LayoutMode mode = board.layoutMode();
    const bool tall = (mode == Board::LayoutMode::Vertical);
    const int16_t headerH = LauncherLayout::headerHeight(mode);
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
        /* THE MARK, WITH ITS TRADE MARK SIGN -- not the name in font 4.
         *
         * The wordmark variant is cut to a font-4 heading's height, so the row
         * keeps its geometry, but it is 107px wide against the text's ~84.
         * At 240 that leaves about 7px of air before the copyright, which is
         * right-aligned and about 108px wide. That is the whole budget for
         * this row: re-measure it before putting anything else on it. */
        Ui::drawLogo(tft,
                     static_cast<int16_t>(10 + Ui::logoCentre(Ui::Logo::Word)),
                     16, Ui::text(), Ui::Logo::Word);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.setTextDatum(MR_DATUM);
        tft.drawString(BRAINO_COPYRIGHT_SHORT, static_cast<int16_t>(lW - 8), 17, 1);
        tft.setTextColor(Ui::text(), Ui::surface());
        drawProfileName(tft, board.profileName(board.activeProfile()).c_str(), profileBtn);

        /* The rule under the mark's row; the status group and its hairline
         * take the two rows beneath it, as they take the two in landscape. */
        tft.drawFastHLine(8, 30, static_cast<int16_t>(lW - 16), Ui::shade(Ui::surface(), 150));
    } else {
        /* The same mark in landscape, where the copyright sits on its own row
         * underneath. The player's name shares the mark's row (see
         * profileRect()), and the clock with its sync check and the gear close
         * it at the right, as in the top bar. */
        Ui::drawLogo(tft,
                     static_cast<int16_t>(10 + Ui::logoCentre(Ui::Logo::Word)),
                     16, Ui::text(), Ui::Logo::Word);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(BRAINO_COPYRIGHT_SHORT, 10, 38, 1);
        tft.setTextColor(Ui::text(), Ui::surface());
        drawProfileName(tft, board.profileName(board.activeProfile()).c_str(), profileBtn);

    }
    drawStatusGroup(tft, board, mode, lW, clockX_, clockY_, clockW_);
    clockScreenW_ = lW;
    clockDatum_ = MR_DATUM;
}

bool LauncherApp::renderChrome(GameHost& host) {
    drawHeader(host);
    return true;
}

/* Background and header. Only on a full repaint -- paging does not touch it. */
void LauncherApp::renderStatic(GameHost& host) {
    static_assert(MAX_TILES >= LauncherLayout::MAX_PAGE_SIZE,
                  "slotHasButton_ cannot cover every tile on a page");
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
void LauncherApp::renderDynamic(GameHost& host) {
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
    const LauncherLayout::Grid grid = LauncherLayout::grid(mode, lW, lH);
    /* Three columns in portrait -- the 4-inch panel -- leaves 88px for a
     * subtitle, and five of them are wider than that at font 1, which is
     * already the smallest font the firmware carries. So there, and only
     * there, a subtitle that does not fit goes onto two lines. */
    const bool wrapSubtitles = tall && grid.cols >= 3;

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
            /* The erase has to cover the SHADOW as well as the tile.
             * Ui::drawButton draws its shadow at the same size, offset down
             * and right, so it sticks out past the tile's right and bottom
             * edges. Erasing exactly the tile rect left a two-pixel vertical
             * sliver and a three-pixel horizontal one -- a ghost L in every
             * empty slot on a short last page, belonging to a tile from the
             * page before. Derived from the offsets rather than typed, so the
             * two cannot drift. */
            const Rect empty = LauncherLayout::tileRect(slot, mode, lW, lH);
            tft.fillRect(empty.x, empty.y,
                         static_cast<int16_t>(empty.w + Ui::BUTTON_SHADOW_DX),
                         static_cast<int16_t>(empty.h + Ui::BUTTON_SHADOW_DY),
                         Ui::bg());
            if (slot < MAX_TILES) {
                slotHasButton_[slot] = false;
            }
            continue;
        }
        const AppDefinition& entry = host.launcherEntry(index);
        const Rect r = LauncherLayout::tileRect(slot, mode, lW, lH);
        /* From the palette, not from three literals. Every theme that existed
         * before this lists the same three colours, so nothing changed for
         * them -- but a theme made of four shades of green cannot have bright
         * blue, green and red rectangles as the first thing anyone sees. */
        const uint16_t fill = Ui::tileFill(LauncherLayout::tileFillIndex(slot, grid));
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
            /* Rounded, and concentric with the frame: the frame's outline is
             * a rounded rect of Ui::cornerRadius(), and a square erase two
             * pixels inside it took the outline's four corner pixels with
             * it on every page turn. Inset by two with a radius two less, it
             * stays two pixels inside the outline all the way round. What
             * the icon or label drew BEYOND this area stays: that is why
             * LauncherIcons.cpp keeps every icon inside its box (rule 3). */
            const int16_t inner = static_cast<int16_t>(
                Ui::cornerRadius() > 2 ? Ui::cornerRadius() - 2 : 0);
            tft.fillRoundRect(static_cast<int16_t>(r.x + 2), static_cast<int16_t>(r.y + 2),
                              static_cast<int16_t>(r.w - 4), static_cast<int16_t>(r.h - 4),
                              inner, fill);
        }

        drawTileContents(tft, entry, r, fill, tall, pageTextScale, wrapSubtitles);
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
