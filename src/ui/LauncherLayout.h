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
Rect topBarLockRect();

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
uint8_t pageSize(Board::LayoutMode mode);

}  // namespace LauncherLayout
