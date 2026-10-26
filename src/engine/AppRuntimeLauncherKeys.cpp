// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AppRuntime.h"

/* The launcher's navigation keys: focus, the ring that shows it, and what
 * each key does. Apart from AppRuntimeLauncher.cpp, which owns the grid,
 * the paging and touch; this file only moves a ring around that grid and
 * asks it to turn a page. Built only where GUME_HAS_NAV_KEYS is set; on every
 * other board LauncherApp.h's hooks are empty inlines and this is nothing. */

#if GUME_HAS_NAV_KEYS
#include "ui/LauncherLayout.h"

namespace {
/* The key-focus ring: a RING_W outline in the gap round a tile.
 *
 * It sits in space nothing else uses, so moving focus erases one outline in
 * the background colour and draws another, and no tile is repainted. That
 * only holds while the ring stays clear of the tile it surrounds, that
 * tile's own shadow, and every neighbour and neighbour's shadow -- so the
 * offsets are stated from the gap and the shadow and checked here, rather
 * than typed in and trusted. Left and top: RING_OUT outside the tile. Right
 * and bottom: one pixel past the tile's shadow. */
constexpr int16_t RING_W = 2;
constexpr int16_t RING_OUT = 4;
constexpr int16_t RING_RIGHT = Ui::BUTTON_SHADOW_DX + 1;
constexpr int16_t RING_BOTTOM = Ui::BUTTON_SHADOW_DY + 1;
constexpr int16_t GAP = LauncherLayout::TILE_GAP;
static_assert(RING_OUT >= RING_W, "the ring would touch its tile's left or top edge");
static_assert(RING_OUT < GAP - Ui::BUTTON_SHADOW_DX + 1,
              "the ring's left edge would land on the shadow of the tile to its left");
static_assert(RING_OUT < GAP - Ui::BUTTON_SHADOW_DY + 1,
              "the ring's top edge would land on the shadow of the tile above");
static_assert(RING_RIGHT + RING_W - 1 < GAP && RING_BOTTOM + RING_W - 1 < GAP,
              "the ring would reach the next tile across or down");

void drawFocusRing(Ui::Renderer& tft, const Rect& t, uint16_t colour) {
    const int16_t left = static_cast<int16_t>(t.x - RING_OUT);
    const int16_t top = static_cast<int16_t>(t.y - RING_OUT);
    const int16_t right = static_cast<int16_t>(t.x + t.w + RING_RIGHT);
    const int16_t bottom = static_cast<int16_t>(t.y + t.h + RING_BOTTOM);
    const int16_t spanW = static_cast<int16_t>(right + RING_W - left);
    const int16_t spanH = static_cast<int16_t>(bottom + RING_W - top);
    tft.fillRect(left, top, spanW, RING_W, colour);
    tft.fillRect(left, bottom, spanW, RING_W, colour);
    tft.fillRect(left, top, RING_W, spanH, colour);
    tft.fillRect(right, top, RING_W, spanH, colour);
}
}  // namespace

/* ------------------------------------------------------------ the keys
 *
 * Directions walk the list in reading order, as the grid shows it: Right and
 * Left step one tile, Down and Up one row. Walking off the end of a page
 * turns it -- the list is one list, and the pages are how it fits -- with
 * the same sound and the same repaint as Next and Prev. Enter opens the
 * focused tile exactly as a tap would; Back here only puts the ring away,
 * since the runtime's Back is Home and this is home.
 *
 * The header -- profile, Lock, mute, the gear -- is not reachable by key
 * yet. Touch reaches all of it, as it does on every board. */
void LauncherApp::handleNavKey(GameHost& host, NavKey key) {
    const uint8_t total = host.launcherEntryCount();
    if (total == 0) {
        return;
    }
    if (focus_ < 0) {
        /* The first key shows where you are rather than moving you somewhere
         * you could not see coming. */
        if (key != NavKey::Back) {
            focus_ = 0;
            requestFocusPaint();
        }
        return;
    }

    const uint8_t pageSize = host.launcherPageSize();
    const int16_t here = static_cast<int16_t>(page_ * pageSize + focus_);
    if (key == NavKey::Enter) {
        if (here < total) {
            host.openApp(host.launcherEntry(static_cast<uint8_t>(here)));
        }
        return;
    }
    if (key == NavKey::Back) {
        focus_ = -1;
        requestFocusPaint();
        return;
    }

    const int16_t cols = LauncherLayout::grid(host.board().layoutMode(),
                                              static_cast<int16_t>(host.display().width()),
                                              static_cast<int16_t>(host.display().height())).cols;
    int16_t to = here;
    switch (key) {
        case NavKey::Right: to = static_cast<int16_t>(here + 1); break;
        case NavKey::Left:  to = static_cast<int16_t>(here - 1); break;
        case NavKey::Up:    to = static_cast<int16_t>(here - cols); break;
        case NavKey::Down:
            to = static_cast<int16_t>(here + cols);
            /* Into a short last row from beside it: land on its last tile
             * rather than nowhere, so Down always reaches the end. */
            if (to >= total && (total - 1) / cols > here / cols) {
                to = static_cast<int16_t>(total - 1);
            }
            break;
        default:
            return;
    }
    if (to < 0 || to >= total) {
        return;
    }
    const uint8_t toPage = static_cast<uint8_t>(to / pageSize);
    focus_ = static_cast<int8_t>(to % pageSize);
    if (toPage != page_) {
        page_ = toPage;
        host.playSound(Sound::Tap);
        requestGridPaint();
    } else {
        requestFocusPaint();
    }
}

/* Ask for the ring alone -- unless a fuller repaint is already on its way,
 * which will paint the ring anyway and must not be downgraded to this. */
void LauncherApp::requestFocusPaint() {
    if (!(needsRender() && !focusOnly_)) {
        focusOnly_ = true;
    }
    markDirty();
}

void LauncherApp::paintFocus(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const int16_t lW = static_cast<int16_t>(tft.width());
    const int16_t lH = static_cast<int16_t>(tft.height());
    const Board::LayoutMode mode = host.board().layoutMode();

    /* A slot with no tile in it cannot hold focus: the page may have
     * shortened (a game hidden, the orientation turned) since focus was set. */
    if (focus_ >= 0) {
        const int16_t pageSize = host.launcherPageSize();
        const int16_t onPage = static_cast<int16_t>(host.launcherEntryCount() - page_ * pageSize);
        const int16_t last = static_cast<int16_t>((onPage < pageSize ? onPage : pageSize) - 1);
        if (focus_ > last) {
            focus_ = static_cast<int8_t>(last);
        }
    }
    if (ringShown_ == focus_) {
        return;
    }
    if (ringShown_ >= 0) {
        drawFocusRing(tft, LauncherLayout::tileRect(static_cast<uint8_t>(ringShown_), mode, lW, lH),
                      Ui::bg());
    }
    if (focus_ >= 0) {
        drawFocusRing(tft, LauncherLayout::tileRect(static_cast<uint8_t>(focus_), mode, lW, lH),
                      Ui::accent());
    }
    ringShown_ = focus_;
}

/* ------------------------------------------------------------ the hooks
 *
 * What AppRuntimeLauncher.cpp calls, at the places keys touch the grid. */
bool LauncherApp::takeNavKey(GameHost& host) {
    const NavKey key = hostNavKey(host);
    if (key == NavKey::None) {
        return false;
    }
    handleNavKey(host, key);
    return true;
}

void LauncherApp::dropFocusForTouch() {
    if (focus_ >= 0) {
        focus_ = -1;
        requestFocusPaint();
    }
}

bool LauncherApp::paintFocusOnly(GameHost& host) {
    const bool only = focusOnly_ && !needsFullRender();
    focusOnly_ = false;
    if (only) {
        paintFocus(host);
    }
    return only;
}

void LauncherApp::paintFocusAfterGrid(GameHost& host) {
    paintFocus(host);
}

#endif  /* GUME_HAS_NAV_KEYS */
