#pragma once

#include <Arduino.h>
#include "ui/Ui.h"

/* The on-screen keyboard, as one description instead of several.
 *
 * ------------------------------------------------------------------------
 * Why this is its own file
 * ------------------------------------------------------------------------
 * There were two hand-rolled keyboards in this firmware -- ProfileApp's
 * rename phase and WifiApp's password entry -- each with its own copy of the
 * grid maths, and a third was about to be written for naming a Nearby peer.
 * Three copies of "where is the key at row 3, column 4 on this panel" is three
 * chances to draw a key somewhere it cannot be pressed, which has already
 * happened here: both PIN pads hard-coded rows at y=220 on a 240px panel and
 * put DEL and OK off the bottom of the screen, where there was physically
 * nothing to press.
 *
 * So the layout is computed once, here, and the SAME function answers both
 * "where do I draw this key" and "what did the finger land on". They cannot
 * disagree, because they are one function.
 *
 * ------------------------------------------------------------------------
 * QWERTY, and what that costs
 * ------------------------------------------------------------------------
 * The rows are ragged -- 10, 10, 9, 7 -- because that is what QWERTY is; a
 * tidy rectangle would not be the layout anybody recognises. Each row is
 * centred on the panel and the action row is three wide buttons.
 *
 * The cost is key width, and it is worth knowing before adding a caller. Ten
 * columns across a 320px landscape panel gives 26px keys, which is
 * comfortable. Across a 240px portrait panel it gives 20px, which is narrow --
 * usable because TOUCH_HIT_SLOP widens every target by 8px on each side, but
 * it is the tightest thing this firmware asks a finger to hit. Do not add an
 * eleventh column.
 *
 * This is deliberately NOT a widget class. It owns no state -- no draft
 * buffer, no cursor, no dirty flag. The screen owns its text and decides what
 * a keystroke means; this only turns a grid position into a character and back
 * into a rectangle. A screen that wants a different meaning for OK (commit a
 * profile name, join a network, label a peer) writes that itself.
 *
 * Lays out against the live panel size passed in, never SCREEN_WIDTH /
 * SCREEN_HEIGHT, so it works in both orientations -- every caller is a system
 * app and system apps must.
 */
namespace Ui {
namespace Keypad {

/** Rows, including the action row at the bottom. */
constexpr uint8_t ROWS = 5;

/** Widest row, for callers sizing a loop. Rows are ragged: see columns(). */
constexpr uint8_t MAX_COLS = 10;

/* Three of the keys are actions rather than characters. They travel as
 * characters so the table stays a plain table; a caller compares against these
 * names rather than against punctuation scattered through its update(). */
constexpr char BACKSPACE = '<';
constexpr char ACCEPT = '>';
constexpr char SPACE = ' ';

/** How many keys are in this row. Ragged by design -- 10, 10, 9, 7, then 3. */
uint8_t columns(uint8_t row);

/** The character at a grid position, or 0 when the position is off the grid. */
char keyAt(uint8_t row, uint8_t col);

/* Space to leave below the keyboard for a 22px-tall button at screenH-30,
 * which is where every screen here puts its Cancel. */
constexpr int16_t FOOTER_BUTTON = 34;

/* Space to leave when there is nothing under the keyboard at all. */
constexpr int16_t FOOTER_NONE = 6;

/* The keyboard is ANCHORED TO THE BOTTOM of the panel, above `bottomReserve`
 * pixels kept clear for the caller's own footer.
 *
 * It used to be placed from the top with a hardcoded y0 and a per-orientation
 * fudge of -2 or -22. Those two numbers were correct for exactly two panels --
 * a 240px-tall landscape and a 320px-tall portrait -- and silently wrong for
 * the 4-inch board, where they left the keyboard floating with an 84px hole
 * above the Cancel button. Anchoring to the bottom needs no per-panel
 * constant: the gap above the footer is the same everywhere, and the space
 * that varies is the space above the keyboard, which is where the caller's
 * text field lives and where slack belongs.
 */
Rect keyRect(uint8_t row, uint8_t col, int16_t screenW, int16_t screenH,
             int16_t bottomReserve);

/* The character under a touch, or 0 when the press missed every key. Uses the
 * same keyRect() the drawing does, which is the whole point of this file. */
char hit(int16_t x, int16_t y, int16_t screenW, int16_t screenH,
         int16_t bottomReserve);

/** Total height the keyboard occupies, footer excluded. */
int16_t height(int16_t screenW, int16_t screenH);

/* The y of the keyboard's top edge. A caller lays its text field out ABOVE
 * this rather than guessing, which is what stops the two overlapping on a
 * panel nobody tested. Can be small on a short landscape panel -- check it
 * rather than assuming there is room. */
int16_t topY(int16_t screenW, int16_t screenH, int16_t bottomReserve);

/** Draw the whole keyboard. DEL and OK are coloured and labelled as actions. */
void draw(Renderer& tft, int16_t screenW, int16_t screenH,
          int16_t bottomReserve);

}   // namespace Keypad
}   // namespace Ui
