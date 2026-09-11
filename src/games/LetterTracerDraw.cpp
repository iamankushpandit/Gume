#include "LetterTracer.h"
#include "LetterTracerLayout.h"

#include <math.h>

/* Everything LetterTracer paints. The tracing logic -- resampling, hit
 * testing, turn finding -- is in LetterTracer.cpp; this file only reads the
 * state that logic leaves behind. Split out when the engine passed 700
 * lines, per CLAUDE.md's modularity rule. */

using namespace LetterTracerLayout;

/* A small arrow just past the waypoint, pointing where the stroke goes next.
 *
 * Past it rather than on it, so it does not bury the dot the finger is aiming
 * for -- the dot says WHERE and the arrow says WHICH WAY, and they are easier
 * to read as two things than as one. */
/* The box an arrow occupies. Derived from the same two points the arrow is
 * drawn from, plus a pixel of daylight, so it cannot be too small however the
 * arrow's proportions change -- CLAUDE.md's rule about clear rectangles being
 * derived and never typed in. */
Rect LetterTracer::arrowRect(uint8_t index) const {
    if (index == NO_CORNER || index + 1 >= MAX_POINTS) return Rect{0, 0, 0, 0};
    const int16_t reach = static_cast<int16_t>(NEXT_R + ARROW_LEN + ARROW_HALF + 2);
    return Rect{static_cast<int16_t>(pts_[index].x - reach),
                static_cast<int16_t>(pts_[index].y - reach),
                static_cast<int16_t>(reach * 2),
                static_cast<int16_t>(reach * 2)};
}

/* Move the arrow, in place, WITHOUT a full repaint.
 *
 * The first version marked the whole screen dirty whenever the arrow moved, on
 * the reasoning that a moving arrow changes the picture's shape and corners are
 * rare. Corners are not rare: a three-letter joined word has about eleven, so
 * that was eleven screen clears while tracing one word, and it was reported
 * from the device as the screen flashing. It was my misjudgement, not a
 * surprise -- a full repaint wipes 200x156 pixels plus the chrome to move
 * fifteen.
 *
 * So: erase the old arrow's own box, then repaint the guide over it. Both
 * drawGhost() and drawAllDots() are IDEMPOTENT -- they paint exactly what is
 * already there in exactly the same colours -- so running them whole is
 * visually a no-op everywhere except inside the box that was just cleared, and
 * costs no clear of its own. That is what makes this flicker-free without
 * having to work out which ghost segments and which dots the arrow overlapped.
 */
void LetterTracer::moveArrow(Ui::Renderer& tft) {
    if (arrowDrawnAt_ == arrowAt_) return;
    if (arrowDrawnAt_ != NO_CORNER) {
        const Rect r = arrowRect(arrowDrawnAt_);
        if (r.w > 0) {
            tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
            drawGhost(tft);
            drawAllDots(tft);
        }
    }
    drawArrow(tft, arrowAt_);
    arrowDrawnAt_ = arrowAt_;
}

void LetterTracer::drawArrow(Ui::Renderer& tft, uint8_t index) {
    if (index == NO_CORNER || index + 1 >= MAX_POINTS) return;
    const float dx = static_cast<float>(pts_[index + 1].x - pts_[index].x);
    const float dy = static_cast<float>(pts_[index + 1].y - pts_[index].y);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const float ux = dx / len;
    const float uy = dy / len;

    const float bx = static_cast<float>(pts_[index].x) + ux * NEXT_R;
    const float by = static_cast<float>(pts_[index].y) + uy * NEXT_R;
    const int16_t tipX = static_cast<int16_t>(bx + ux * ARROW_LEN);
    const int16_t tipY = static_cast<int16_t>(by + uy * ARROW_LEN);
    const int16_t leftX = static_cast<int16_t>(bx - uy * ARROW_HALF);
    const int16_t leftY = static_cast<int16_t>(by + ux * ARROW_HALF);
    const int16_t rightX = static_cast<int16_t>(bx + uy * ARROW_HALF);
    const int16_t rightY = static_cast<int16_t>(by - ux * ARROW_HALF);
    tft.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, Ui::warning());
}

void LetterTracer::drawModeTabs(Ui::Renderer& tft) {
    const uint16_t activeBg = Ui::warning();
    const uint16_t activeTxt = Ui::panel();
    const uint16_t inactiveBg = Ui::panel();
    const uint16_t inactiveTxt = Ui::text();

    auto tab = [&](Rect r, const char* label, bool active) {
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, active ? activeBg : inactiveBg);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
        tft.setTextColor(active ? activeTxt : inactiveTxt,
                         active ? activeBg : inactiveBg);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 1);
    };

    for (uint8_t i = 0; i < setCount_; ++i) {
        tab(setTabRect(i), sets_[i].label, i == setIndex_);
    }
    tab(RETRY_BTN, "Again", false);
    tab(NEXT_BTN, "Next", false);
    tab(PREV_BTN, "Prev", false);
}

/* What to write, in ordinary type, above the canvas. */
void LetterTracer::drawCaption(Ui::Renderer& tft) {
    char buf[2];
    tft.fillRect(DRAW_X, CAPTION_Y, DRAW_W, CAPTION_H, Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(caption(buf, sizeof(buf)),
                   static_cast<int16_t>(DRAW_X + DRAW_W / 2),
                   static_cast<int16_t>(CAPTION_Y + CAPTION_H / 2), 2);
}

/* The finished shape, faintly, under everything else.
 *
 * This is the answer to "what am I aiming at?" -- a child can see the whole
 * letter or word before starting and check their own line against it as they
 * go, which is what a handwriting workbook's grey letter does. It costs no
 * screen space, which is why it went here rather than into a thumbnail beside
 * the canvas: there is no room for a second copy of a joined word.
 *
 * Drawn from the RAW glyph polyline rather than from the resampled waypoints,
 * because the waypoints are 12 to 20 pixels apart and a polyline through them
 * visibly corners on the tight curves -- which is exactly where a child needs
 * the target to be accurate. */
void LetterTracer::drawGhost(Ui::Renderer& tft) {
    if (glyphs_ == nullptr) return;
    const Glyph& g = glyphs_[glyphIndex_];
    for (uint8_t st = 0; st < g.strokeCount && st < MAX_STROKES; ++st) {
        const Stroke& s = g.strokes[st];
        for (uint8_t i = 0; i + 1 < s.count; ++i) {
            tft.drawLine(scaleX(s.pts[i * 2]), scaleY(s.pts[i * 2 + 1]),
                         scaleX(s.pts[(i + 1) * 2]), scaleY(s.pts[(i + 1) * 2 + 1]),
                         Ui::outline());
        }
    }
}

void LetterTracer::drawTracedSegment(Ui::Renderer& tft, uint8_t from, uint8_t to) {
    tft.drawLine(pts_[from].x, pts_[from].y, pts_[to].x, pts_[to].y, Ui::success());
    tft.drawLine(pts_[from].x + 1, pts_[from].y, pts_[to].x + 1, pts_[to].y,
                 Ui::success());
}

/* Every dot of every stroke. Only on a full repaint -- see render(). */
void LetterTracer::drawAllDots(Ui::Renderer& tft) {
    for (uint8_t st = 0; st < strokeCount_; ++st) {
        const uint8_t start = strokeStart_[st];
        const uint8_t len = strokeLen_[st];
        const bool done = st < activeStroke_;
        const bool active = st == activeStroke_;
        const uint8_t inked = active ? min(nextPoint_, (uint8_t)len)
                                     : (done ? len : 0);

        for (uint8_t i = 0; i + 1 < inked && start + i + 1 < MAX_POINTS; ++i) {
            drawTracedSegment(tft, static_cast<uint8_t>(start + i),
                              static_cast<uint8_t>(start + i + 1));
        }
        for (uint8_t i = 0; i < len && start + i < MAX_POINTS; ++i) {
            const uint8_t idx = static_cast<uint8_t>(start + i);
            if (i < inked) {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, DOT_R, Ui::success());
            } else if (active && i == inked) {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, NEXT_R,
                               pulseState_ ? Ui::text() : Ui::warning());
            } else {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, DOT_R, Ui::muted());
            }
        }
    }

    /* Which stroke this is, on its first dot. A cursive word is one stroke and
     * a printed letter is up to four, and the number is how a child knows
     * there is more to come after this one. */
    if (activeStroke_ < strokeCount_) {
        const uint8_t start = strokeStart_[activeStroke_];
        tft.fillCircle(pts_[start].x, pts_[start].y, 7, Ui::warning());
        tft.setTextColor(Ui::panel(), Ui::warning());
        tft.setTextDatum(MC_DATUM);
        char badge[2];
        badge[0] = static_cast<char>('1' + activeStroke_);
        badge[1] = 0;
        tft.drawString(badge, pts_[start].x, pts_[start].y, 1);
    }
}

void LetterTracer::drawProgress(Ui::Renderer& tft) {
    uint16_t totalPoints = 0;
    uint16_t claimedPoints = 0;

    for (uint8_t s = 0; s < strokeCount_; ++s) {
        totalPoints = static_cast<uint16_t>(totalPoints + strokeLen_[s]);
        if (s < activeStroke_) {
            claimedPoints = static_cast<uint16_t>(claimedPoints + strokeLen_[s]);
        } else if (s == activeStroke_) {
            claimedPoints = static_cast<uint16_t>(claimedPoints + nextPoint_);
        }
    }

    const uint8_t pct = totalPoints > 0
        ? static_cast<uint8_t>((uint32_t)claimedPoints * 100 / totalPoints)
        : 0;
    const int16_t barW = 120;
    const int16_t barX = (GAME_CANVAS_WIDTH - barW) / 2;

    tft.fillRoundRect(barX, BAR_Y, barW, 10, 4, Ui::panel());
    tft.drawRoundRect(barX, BAR_Y, barW, 10, 4, Ui::outline());

    const int16_t fillW = static_cast<int16_t>((int32_t)barW * pct / 100);
    if (fillW > 0) {
        tft.fillRoundRect(barX, BAR_Y, fillW, 10, 4, Ui::success());
    }
}

void LetterTracer::drawCompleteStatus(Ui::Renderer& tft) {
    /* Over the bottom of the canvas, which is the one place a badge can go
     * without stealing room from the letter for the whole game to pay for a
     * message that shows for a moment. Painted only on the repaint completion
     * triggers, and wiped by the next one. */
    const int16_t w = 156;
    const int16_t h = 21;
    const int16_t x = static_cast<int16_t>(DRAW_X + (DRAW_W - w) / 2);
    const int16_t y = static_cast<int16_t>(DRAW_Y + DRAW_H - h - 2);
    tft.fillRoundRect(x, y, w, h, 6, Ui::success());
    tft.drawRoundRect(x, y, w, h, 6, Ui::outline());
    tft.setTextColor(TFT_BLACK, Ui::success());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Great job", static_cast<int16_t>(x + w / 2),
                   static_cast<int16_t>(y + h / 2), 2);
}

/* WHY THIS IS NOT ONE FULL REPAINT PER FRAME.
 *
 * It used to be: every dirty frame wiped the whole canvas and redrew the
 * ghost, every dot and the badge. Claiming one dot changed about forty pixels
 * and cost a 200x162 wipe plus a hundred and fifty circles, and the pulse made
 * that happen twice a second whether or not anybody was tracing. On a panel
 * where a full-screen repaint is 30ms of visible blanking that is both a waste
 * and a flicker.
 *
 * What actually changes between frames is small and additive: a dot goes from
 * grey to green, a short line appears behind it, and the next dot changes
 * colour. None of that needs anything erased -- which is the whole reason the
 * pulse changes colour instead of size. So a partial frame overdraws the few
 * dots that moved and the progress bar, and touches nothing else.
 *
 * A full repaint is kept for the cases where the picture genuinely changes
 * shape: a new glyph, a new alphabet, a stroke finishing (the badge moves to
 * the next stroke's first dot), and completion. Those are events, not frames.
 */
void LetterTracer::render(AppContext& host, const char* title, bool fullRender) {
    Ui::Renderer& tft = host.display();
    if (glyphs_ == nullptr) return;

    if (fullRender) {
        Ui::clear(tft);
        host.drawTopBar(title);
        drawModeTabs(tft);
        drawCaption(tft);
        tft.fillRect(DRAW_X, DRAW_Y, DRAW_W, DRAW_H, Ui::bg());
        drawGhost(tft);
        drawAllDots(tft);
        drawArrow(tft, arrowAt_);
        drawProgress(tft);
        if (complete_) drawCompleteStatus(tft);
        paintedStroke_ = activeStroke_;
        paintedPoint_ = nextPoint_;
        arrowDrawnAt_ = arrowAt_;
        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (activeStroke_ < strokeCount_) {
        const uint8_t start = strokeStart_[activeStroke_];
        const uint8_t len = strokeLen_[activeStroke_];

        /* Dots claimed since the last paint, and the line into each. */
        for (uint8_t i = paintedPoint_; i < nextPoint_ && i < len; ++i) {
            const uint8_t idx = static_cast<uint8_t>(start + i);
            if (i > 0) {
                drawTracedSegment(tft, static_cast<uint8_t>(idx - 1), idx);
            }
            tft.fillCircle(pts_[idx].x, pts_[idx].y, DOT_R, Ui::success());
        }
        /* The next dot, in whichever half of the pulse we are in. Same radius
         * every time, so this is an overdraw and never an erase. */
        if (nextPoint_ < len) {
            const uint8_t idx = static_cast<uint8_t>(start + nextPoint_);
            tft.fillCircle(pts_[idx].x, pts_[idx].y, NEXT_R,
                           pulseState_ ? Ui::text() : Ui::warning());
        }
        paintedPoint_ = nextPoint_;
    }

    moveArrow(tft);
    drawProgress(tft);
    tft.setTextDatum(TL_DATUM);
}
