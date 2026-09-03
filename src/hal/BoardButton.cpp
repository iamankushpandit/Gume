#include "Board.h"

/* The BOOT key: the one input on these boards that is not the touchscreen.
 *
 * There is very little to it, and that is deliberate. A GPIO with an internal
 * pull-up, sampled once per frame by the runtime, edge-detected here. No
 * interrupt, no timer, no task -- consistent with the rest of this firmware,
 * where the only thread besides the loop is the watchdog monitor.
 *
 * What this file does NOT do is as much the point as what it does:
 *
 *  - It does not read the key during boot. BOOT is a strapping pin and the
 *    ROM samples it at reset to decide whether to enter serial download mode.
 *    Whatever a held key means at that moment has already been decided before
 *    `setup()` runs, and nothing here can change it. Reading the key at
 *    runtime only -- which is all `pollBootButton()` is ever called for -- has
 *    no interaction with that at all.
 *
 *  - It does not time the press. A hold is not a gesture this firmware
 *    recognises yet, and a `heldMs` nobody reads would be a claim that it
 *    does. The press edge is enough for what the button is currently wired to,
 *    and whoever adds a hold should note that acting on the press edge is what
 *    would have to change first: you cannot both go home on the press and
 *    decide later that the same press was the start of something longer.
 */

Board::ButtonEvent Board::pollBootButton() {
    ButtonEvent event;
    if (!BOARD.hasBootButton()) {
        /* Not an error and not a degradation worth reporting -- touch drives
         * everything, and always did. Leaving bootButtonDown_ alone here means
         * a board with no key cannot manufacture an edge. */
        return event;
    }

    const int level = digitalRead(BOARD.button.bootPin);
    const bool down = BOARD.button.activeLow ? (level == LOW) : (level == HIGH);

    event.down = down;
    event.justPressed = down && !bootButtonDown_;
    event.justReleased = !down && bootButtonDown_;
    bootButtonDown_ = down;
    return event;
}
