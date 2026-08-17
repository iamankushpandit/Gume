#pragma once

#include <Arduino.h>
#include "BoardConfig.h"
#include "ui/Renderer.h"
#include "ui/Ui.h"

namespace Ui {

/* The panel geometry as it is *right now*, rather than as it was authored.
 *
 * Every screen here used to measure against SCREEN_WIDTH and SCREEN_HEIGHT.
 * Those describe the landscape panel and stop being true the moment the owner
 * flips Settings -> Layout to Vertical, which is why 30 games were pinned to
 * landscape: the switching mechanism worked, the geometry did not.
 *
 * Build one per render()/update() with Ui::frame(tft). Do not cache it in a
 * member -- it is two virtual calls and some integer maths, and a cached copy
 * goes stale at exactly the moment that matters. */
struct Frame {
    int16_t w = SCREEN_WIDTH;
    int16_t h = SCREEN_HEIGHT;

    bool tall() const { return h > w; }
    int16_t cx() const { return static_cast<int16_t>(w / 2); }

    /* The area a screen owns: everything below the top bar, inset by `pad`. */
    Rect content(int16_t pad = 0) const {
        return Rect{pad,
                    static_cast<int16_t>(TOP_BAR_HEIGHT + pad),
                    static_cast<int16_t>(w - 2 * pad),
                    static_cast<int16_t>(h - TOP_BAR_HEIGHT - 2 * pad)};
    }
};

Frame frame(Renderer& tft);

/* Hands out horizontal bands down a rect, so a screen states its stacking
 * order once instead of carrying running Y offsets by hand -- the arithmetic
 * that baked 320x240 into every game in the first place.
 *
 * Pure integer bookkeeping, no allocation; make one on the stack per render. */
class Stack {
public:
    explicit Stack(const Rect& area, int16_t gap = 0)
        : area_(area), y_(area.y), gap_(gap) {}

    /* Take `h` pixels off the top, inset horizontally by `pad` each side. */
    Rect next(int16_t h, int16_t pad = 0);

    /* Give the next band a share of what is left, so a screen can say "the
     * play field gets half the remaining height" without knowing the panel. */
    Rect nextFraction(uint8_t numerator, uint8_t denominator, int16_t pad = 0);

    /* Move the cursor without drawing in the gap. */
    void skip(int16_t h);

    /* Everything not yet handed out. */
    Rect rest(int16_t pad = 0) const;
    int16_t remaining() const;
    int16_t cursor() const { return y_; }

private:
    Rect area_;
    int16_t y_;
    int16_t gap_;
};

/* Cell `index` of a cols x rows grid packed into `band`, row-major, with `gap`
 * between cells. Rounding is absorbed by the last row and column rather than
 * accumulating, so the grid always ends flush with the band. */
Rect gridCell(const Rect& band, uint8_t cols, uint8_t rows, uint8_t index, int16_t gap = 6);

/* The largest centred square that fits in `band`, capped at `maxSide` when
 * that is non-zero. Play fields want this: letterboxing a 3x3 board inside a
 * 240x320 panel reads better than stretching it into rectangles. */
Rect squareIn(const Rect& band, int16_t maxSide = 0);

/* Centre a w x h box inside `band`. */
Rect centreIn(const Rect& band, int16_t w, int16_t h);

/* How many columns `count` answer buttons should use.
 *
 * "A prompt, then N choices" is the catalog's most common shape by a wide
 * margin, and this is the one decision every such screen has to make. A 320px
 * landscape panel fits two comfortable columns; a 240px portrait one is
 * narrower but has ~80 more pixels of height to spend, so small answer sets
 * read better stacked in a single column. */
uint8_t answerColumns(const Frame& f, uint8_t count);

}
