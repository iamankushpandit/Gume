// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "hal/Board.h"
#include "ui/Ui.h"

namespace LauncherLayout {

Rect topBarSettingsRect(int16_t screenW);
/* Mute/unmute, right of the padlock and the same size. See Ui::CONTROL_H. */
Rect topBarSpeakerRect(int16_t screenW);

/* The shared top bar's two left-hand controls. Home has always been there;
 * Lock sits beside it, which is why the home slot narrowed from 42px to 32px
 * -- the title's right edge is pinned by the status cluster, so every pixel
 * the lock takes comes out of the title. Drawing and hit testing both read
 * these, so the glyph and its target cannot drift apart. */
Rect topBarHomeRect();
Rect topBarLockRect(int16_t screenW);
/** Where the top bar's title may start: clear of Home and the padlock. */
int16_t topBarTitleLeft(int16_t screenW);

/* THE LAUNCHER HEADER'S STATUS GROUP, the same shape in both orientations: a
 * top row of the clock, its sync check and the gear, and under it a badge row
 * of the Bluetooth rune, the padlock, the speaker, Wi-Fi and the battery. It
 * is packed from the right edge out of only what is there -- no gear for a
 * player who is not the admin (Ui::settingsShown()), no battery on a board
 * with no battery sense, no rune while the beacon is off -- so a missing
 * thing leaves no hole. A hairline (headerDivider*) stands at the group's
 * left edge in both orientations, and the player's name runs up to it.
 *
 * The padlock and the speaker are the only things in the group anyone taps
 * besides the gear. While the gear is drawn they keep clear of its corner, so
 * a tap on the gear's lower edge cannot mute the console; without it they
 * close up too. They move only when the gear comes or goes -- a change of
 * player, which repaints the header -- never under a finger. The drawing,
 * every hit test and the d-pad's selector read these same functions. */
Rect lockRect(Board::LayoutMode mode, int16_t screenW);
int16_t headerHeight(Board::LayoutMode mode);
Rect gearRect(Board::LayoutMode mode, int16_t screenW);
Rect profileRect(Board::LayoutMode mode, int16_t screenW);
/* The two rows' centre lines. */
int16_t statusTopCy(Board::LayoutMode mode);
int16_t statusBadgeCy(Board::LayoutMode mode);
/* The clock's right edge (it is drawn MR_DATUM, so a longer time grows
 * leftwards, away from its check) and the sync check's centre beside it. */
int16_t clockRight(Board::LayoutMode mode, int16_t screenW);
int16_t syncCx(Board::LayoutMode mode, int16_t screenW);
/* The badge row's passive glyphs: Wi-Fi's centre, the battery badge's left
 * edge (it is drawn left-aligned there, so a narrower reading gives its room
 * back at the panel's edge rather than opening a gap), and the rune's centre. */
int16_t wifiCx(Board::LayoutMode mode, int16_t screenW);
int16_t batteryLeft(Board::LayoutMode mode, int16_t screenW);
int16_t bleCx(Board::LayoutMode mode, int16_t screenW);
/* The hairline between the mark, name and copyright and the status group --
 * its x, and the rows it spans -- in both orientations. */
int16_t headerDividerX(Board::LayoutMode mode, int16_t screenW);
int16_t headerDividerTop(Board::LayoutMode mode);
int16_t headerDividerBottom(Board::LayoutMode mode);
/* The launcher's tile grid, measured against the live panel.
 *
 * screenW/screenH are required rather than defaulted. Everything else on this
 * screen -- gear, profile, lock, the top bar -- already takes a width and
 * adapts; the tiles alone were fixed at the 320x240 geometry, so on a 480x320
 * panel they stopped two-thirds of the way across and left a dead band while
 * the header and pager correctly reached the edges. A default of 320/240 here
 * would be that same literal, just hidden where nobody would look for it. */
Rect tileRect(uint8_t slot, Board::LayoutMode mode, int16_t screenW, int16_t screenH);
/* The space between tiles, and between the outer tiles and the panel edge.
 * Named because the key-focus ring is drawn in it and has to be proved to
 * fit there; see LauncherApp's focus ring. */
constexpr int16_t TILE_GAP = 8;

/* How many columns and rows of tiles the launcher shows. One answer, read by
 * tileRect(), pageSize() and the tile colouring, so the grid that is drawn,
 * the grid that is hit-tested and the number of apps per page cannot disagree.
 *
 * Landscape is 2x3 on every panel. Portrait is 2x2, except on a panel whose
 * short side is at least DENSE_PORTRAIT_MIN_W -- today only the 4-inch board
 * -- where it is 3x3: 96x112 tiles there, still wider than the 2.8-inch
 * board's portrait tiles are tall, and nine apps a page instead of four. It is
 * decided from the glass, not from which board this is, because nothing under
 * src/ may name a board. */
struct Grid {
    uint8_t cols;
    uint8_t rows;
};
constexpr int16_t DENSE_PORTRAIT_MIN_W = 320;
/** The most tiles any grid puts on one page. */
constexpr uint8_t MAX_PAGE_SIZE = 9;
/* Mute/unmute on the launcher's own header, beside the padlock. */
Rect speakerRect(Board::LayoutMode mode, int16_t screenW);

/* The player picker's padlock and speaker. It draws its own header and no top
 * bar, so like the launcher it carries the pair itself: at the right-hand end
 * of the "Who is playing?" row, in both orientations. Here rather than in
 * ProfileApp because the runtime and the handheld's d-pad read them too. */
Rect pickerLockRect(int16_t screenW);
Rect pickerSpeakerRect(int16_t screenW);

Grid grid(Board::LayoutMode mode, int16_t screenW, int16_t screenH);
uint8_t pageSize(Board::LayoutMode mode, int16_t screenW, int16_t screenH);

/* Which of the palette's three tile fills a slot gets. slot % 3 on a
 * two-column grid, as it always was; on a three-column grid that would paint
 * every column one colour, so there it steps by row as well. */
uint8_t tileFillIndex(uint8_t slot, const Grid& g);

}  // namespace LauncherLayout
