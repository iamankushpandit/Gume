// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>

#include "BoardConfig.h"
#include "ui/Ui.h"

/* What the d-pad's selector can land on, read off the panel as it is drawn.
 *
 * No screen knows about it, exactly as on the Linux host
 * (host/src/HostTftFocus.cpp, whose rules this follows): every control worth
 * selecting is drawn as a fill and an outline of one rectangle, which is how
 * Ui::drawButton() ends, and so every button, launcher tile and keyboard key.
 * Ui::TftRenderer reports each fill and outline here as it passes them to the
 * panel. What drawing alone cannot say is said explicitly by the three helpers
 * that know it -- a tab (Ui::drawTab), a Prev or Next pager
 * (Ui::drawPagerButton) and a list's scroll bar (RowList::drawScrollBar) --
 * and the header's Home, padlock, speaker and gear are the runtime's own
 * rects, so they are not recorded at all.
 *
 * Coordinates are the panel's, at the rotation in force: the frame touch is
 * reported in, which is the frame the selector moves and presses in.
 *
 * The other half -- picking a control, moving between them, pressing one and
 * drawing the ring -- is engine/AppRuntimeSelector.cpp.
 *
 * Only on a board with GUME_HAS_NAV_KEYS. Elsewhere every call below is an
 * empty inline and the image carries none of it, the recorder's RAM
 * included. */
namespace Focus {

/* A control the selector can stop at, with what it takes to draw the ring on
 * it and take it off again. The ring is drawn INSIDE the control -- its
 * outline recoloured, and a second line two pixels in -- because inside is
 * the only place whose colours are known exactly: the outline's and the
 * fill's, recorded as it was drawn. Outside are shadows and neighbours, and
 * this panel cannot be read back. */
/* Where a target's ring goes, so that it can always be taken off by drawing
 * known colours back over exactly the pixels it used -- never by asking a
 * screen to repaint, which on a header is a visible refresh. */
enum class Ring : uint8_t {
    Inside,   // a control: its outline, and a line two pixels in
    TabBar,   // a tab: a bar across the inside of its foot
    Around,   // a header glyph that fills its rect: two lines just outside
    Inset,    // a header rect with room round its glyph (Home): two lines in
    Under,    // the player's name: a bar on the rect's last two rows
};

struct Target {
    Rect box;               // what the selector moves between and presses
    int16_t radius = 0;     // the outline's corner radius
    uint16_t outline = 0;   // its colour, restored when the ring leaves
    uint16_t fill = 0;      // the colour two pixels in
    bool header = false;    // a header control (Home, padlock, speaker, gear)
    bool tab = false;
    Ring ring = Ring::Inside;
};

/* A list with a scroll bar: where a drag scrolls it, and which ways it can. */
struct ScrollArea {
    Rect list;
    bool moreAbove = false;
    bool moreBelow = false;
};

constexpr int MAX_TARGETS = 64;

#if GUME_HAS_NAV_KEYS
/* From Ui::TftRenderer, for every primitive it draws. */
void noteFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint16_t color);
void noteOutline(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint16_t color);
void noteScreenClear();
void noteViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool datum);
void noteViewportReset();

/* From the helpers that know what drawing cannot say. */
void noteTab(const Rect& r, bool active, uint16_t fill);
void notePager(const Rect& r, bool next, bool enabled);
void noteScroll(const Rect& list, bool moreAbove, bool moreBelow);

/* While false, nothing is recorded: the selector's own ring is drawn through
 * the same renderer and must not become a control. */
void setRecording(bool on);

/* The controls on the panel now, in the order recorded, at most `max`. */
int targets(Target* out, int max);
/* The tab after the selected one in its strip, wrapping round. */
bool nextTab(Rect& out);
/* The enabled Prev (next false) or Next pager. */
bool pager(bool next, Rect& out);
bool scrollArea(ScrollArea& out);
/* Four buttons of one size in two columns and two rows -- a quiz's answers
 * -- in reading order: top left, top right, bottom left, bottom right. False
 * unless the screen shows exactly one such set. `fills`, if given, gets each
 * one's fill as last drawn. */
bool answerGrid(Rect out[4], uint16_t* fills = nullptr);
/* Whether `t` is still on the panel exactly as it was recorded: the same
 * rectangle in the same colours. Only then may its ring be taken off by
 * painting those colours back; anywhere else the screen has already drawn
 * over the ring, and the old colours would be a mark of their own. */
bool stillDrawn(const Target& t);
#else
inline void noteFill(int32_t, int32_t, int32_t, int32_t, int32_t, uint16_t) {}
inline void noteOutline(int32_t, int32_t, int32_t, int32_t, int32_t, uint16_t) {}
inline void noteScreenClear() {}
inline void noteViewport(int32_t, int32_t, int32_t, int32_t, bool) {}
inline void noteViewportReset() {}
inline void noteTab(const Rect&, bool, uint16_t) {}
inline void notePager(const Rect&, bool, bool) {}
inline void noteScroll(const Rect&, bool, bool) {}
#endif

}   // namespace Focus
