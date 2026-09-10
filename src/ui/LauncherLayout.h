#pragma once

#include "hal/Board.h"
#include "ui/Ui.h"

namespace LauncherLayout {

Rect topBarSettingsRect(int16_t screenW);

/* The shared top bar's two left-hand controls. Home has always been there;
 * Lock sits beside it, which is why the home slot narrowed from 42px to 32px
 * -- the title's right edge is pinned by the status cluster, so every pixel
 * the lock takes comes out of the title. Drawing and hit testing both read
 * these, so the glyph and its target cannot drift apart. */
Rect topBarHomeRect();
Rect topBarLockRect(int16_t screenW);
/** Where the top bar's title may start: clear of Home and the padlock. */
int16_t topBarTitleLeft(int16_t screenW);

/* Where the launcher draws its own Lock button. The launcher has no top bar,
 * so it needs its own slot: the air between the title and the profile name in
 * Wide, and the badge row's right-hand end in Tall. */
Rect lockRect(Board::LayoutMode mode, int16_t screenW);
int16_t headerHeight(Board::LayoutMode mode);
Rect gearRect(Board::LayoutMode mode, int16_t screenW);
Rect profileRect(Board::LayoutMode mode, int16_t screenW);
/* The launcher's tile grid, measured against the live panel.
 *
 * screenW/screenH are required rather than defaulted. Everything else on this
 * screen -- gear, profile, lock, the top bar -- already takes a width and
 * adapts; the tiles alone were fixed at the 320x240 geometry, so on a 480x320
 * panel they stopped two-thirds of the way across and left a dead band while
 * the header and pager correctly reached the edges. A default of 320/240 here
 * would be that same literal, just hidden where nobody would look for it. */
Rect tileRect(uint8_t slot, Board::LayoutMode mode, int16_t screenW, int16_t screenH);

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
Grid grid(Board::LayoutMode mode, int16_t screenW, int16_t screenH);
uint8_t pageSize(Board::LayoutMode mode, int16_t screenW, int16_t screenH);

/* Which of the palette's three tile fills a slot gets. slot % 3 on a
 * two-column grid, as it always was; on a three-column grid that would paint
 * every column one colour, so there it steps by row as well. */
uint8_t tileFillIndex(uint8_t slot, const Grid& g);

}  // namespace LauncherLayout
