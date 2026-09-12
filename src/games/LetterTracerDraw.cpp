#include "LetterTracer.h"
#include "LetterTracerLayout.h"

#include <math.h>

/* Everything LetterTracer paints. The tracing logic -- resampling and hit
 * testing -- is in LetterTracer.cpp, where the arrows go is decided in
 * LetterTracerArrows.cpp, and printed words are spelled in
 * LetterTracerWords.cpp; this file only reads the state those leave behind.
 * Split out when the engine passed 700 lines, per CLAUDE.md's modularity
 * rule. */

using namespace LetterTracerLayout;

/* The numbered arrows, all of them, where planArrows() put them.
 *
 * They are the PLAN for the letter -- which stroke starts where and sets off
 * which way -- so a child can see the whole of it before starting, numbered,
 * the way a workbook shows it. They never move and they are all muted; what is
 * happening now is said by the two things in the highlight colour, the start
 * ring and the guide arrow that follows the finger.
 *
 * IDEMPOTENT, like drawGhost() and drawAllDots(): it paints each arrow's exact
 * pixels, so it can be re-run to repair whatever an erase took. */
void LetterTracer::drawArrows(Ui::Renderer& tft) {
    tft.setTextDatum(MC_DATUM);
    for (uint8_t i = 0; i < arrowCount_; ++i) {
        /* All of them muted, including the stroke being traced. They are the
         * plan for the letter; what is happening NOW is the guide arrow and
         * the ring, and those are the two things in the highlight colour. When
         * every arrow competed for that colour a child had no way to tell the
         * one that mattered from the four that did not. */
        drawArrow(tft, arrows_[i], Ui::muted());
    }
}

/* One arrow: a two-pixel shaft, a filled head, and -- for a stroke's own
 * arrow -- its number beside the tail. */
void LetterTracer::drawArrow(Ui::Renderer& tft, const Arrow& a, uint16_t col) {
    const float dx = static_cast<float>(a.tipX - a.tailX);
    const float dy = static_cast<float>(a.tipY - a.tailY);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;
    {
        const float ux = dx / len;
        const float uy = dy / len;
        const int16_t hx = static_cast<int16_t>(lroundf(a.tipX - ux * ARROW_HEAD));
        const int16_t hy = static_cast<int16_t>(lroundf(a.tipY - uy * ARROW_HEAD));
        /* A two-pixel shaft: a one-pixel line vanishes on a resistive panel
         * viewed at arm's length, which is how a five-year-old holds it. */
        tft.drawLine(a.tailX, a.tailY, hx, hy, col);
        tft.drawLine(a.tailX + 1, a.tailY, hx + 1, hy, col);
        tft.drawLine(a.tailX, a.tailY + 1, hx, hy + 1, col);
        const int16_t lx = static_cast<int16_t>(lroundf(hx - uy * ARROW_HALF));
        const int16_t ly = static_cast<int16_t>(lroundf(hy + ux * ARROW_HALF));
        const int16_t rx = static_cast<int16_t>(lroundf(hx + uy * ARROW_HALF));
        const int16_t ry = static_cast<int16_t>(lroundf(hy - ux * ARROW_HALF));
        tft.fillTriangle(a.tipX, a.tipY, lx, ly, rx, ry, col);
        if (a.numbered) {
            char num[2] = {static_cast<char>('1' + a.stroke), 0};
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(col, Ui::bg());
            tft.drawString(num, a.labelX, a.labelY, 1);
        }
    }
}

/* The box an arrow covers, derived from the arrow itself plus the head's
 * half-width and a pixel of daylight -- never a typed-in rectangle, which is
 * the mistake CLAUDE.md's rendering rule is most emphatic about. */
Rect LetterTracer::arrowBox(const Arrow& a) const {
    const int16_t pad = ARROW_HALF + 2;
    const int16_t x0 = (a.tailX < a.tipX ? a.tailX : a.tipX) - pad;
    const int16_t y0 = (a.tailY < a.tipY ? a.tailY : a.tipY) - pad;
    const int16_t x1 = (a.tailX > a.tipX ? a.tailX : a.tipX) + pad;
    const int16_t y1 = (a.tailY > a.tipY ? a.tailY : a.tipY) + pad;
    return Rect{x0, y0, static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0)};
}

/* Move the guide arrow WITHOUT clearing the screen.
 *
 * It moves every time a dot is claimed -- a couple of times a second while a
 * child is tracing -- so a full repaint here would be the screen flashing
 * continuously, which is the failure the rendering rule in CLAUDE.md was
 * written about. Instead its old box is painted out and the letter repainted
 * inside that box: drawGhost() and drawAllDots() paint exactly what is already
 * there, so running them clipped to the box restores what the erase took and
 * touches nothing else. */
void LetterTracer::moveGuide(Ui::Renderer& tft) {
    const bool same = guideOnPanel_ && guideShown_ &&
                      guideDrawn_.tailX == guide_.tailX &&
                      guideDrawn_.tailY == guide_.tailY &&
                      guideDrawn_.tipX == guide_.tipX &&
                      guideDrawn_.tipY == guide_.tipY;
    if (same) return;

    if (guideOnPanel_) {
        const Rect r = arrowBox(guideDrawn_);
        tft.setViewport(r.x, r.y, r.w, r.h, false);
        tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
        drawGhost(tft);
        drawAllDots(tft);
        drawArrows(tft);
        drawStartRing(tft);
        tft.resetViewport();
        guideOnPanel_ = false;
    }
    if (guideShown_) {
        drawArrow(tft, guide_, Ui::warning());
        guideDrawn_ = guide_;
        guideOnPanel_ = true;
    }
}

/* "Start here": a ring on the first dot of the stroke being traced.
 *
 * A ring rather than the filled, numbered badge that used to sit there. The
 * number moved to the arrow beside the stroke, where the workbook puts it, and
 * a filled disc covered the very dot the child is meant to put a finger on. */
void LetterTracer::drawStartRing(Ui::Renderer& tft) {
    if (complete_ || activeStroke_ >= strokeCount_) return;
    const Pt& p = pts_[strokeStart_[activeStroke_]];
    tft.drawCircle(p.x, p.y, START_RING_R, Ui::warning());
    tft.drawCircle(p.x, p.y, START_RING_R - 1, Ui::warning());
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
    const Glyph& g = glyph();
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

/* Every dot of every stroke. On a full repaint, and when a stroke finishes --
 * see render(). Idempotent, which is what makes the second use safe. */
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
        const int16_t dotR = dotRadius();
        for (uint8_t i = 0; i < len && start + i < MAX_POINTS; ++i) {
            const uint8_t idx = static_cast<uint8_t>(start + i);
            if (i < inked) {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, dotR, Ui::success());
            } else if (active && i == inked) {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, NEXT_R,
                               pulseState_ ? Ui::text() : Ui::warning());
            } else {
                tft.fillCircle(pts_[idx].x, pts_[idx].y, dotR, Ui::muted());
            }
        }
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
    /* Black is right on a bright green and wrong on Pocket's dark one, so the
     * ink comes from the fill rather than from an assumption about it. */
    tft.setTextColor(Ui::onFill(Ui::success()), Ui::success());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Great job", static_cast<int16_t>(x + w / 2),
                   static_cast<int16_t>(y + h / 2), 2);
}

/* WHY THIS IS NOT ONE FULL REPAINT PER FRAME.
 *
 * It used to be: every dirty frame wiped the whole canvas and redrew the
 * ghost, every dot and the stroke badge. Claiming one dot changed about forty pixels
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
 * A full repaint is kept for the cases where the picture genuinely changes:
 * a new glyph, a new alphabet, and completion. Those are events, not frames.
 *
 * A stroke finishing is NOT one of them any more, though it used to be. It
 * moves the start ring and recolours two strokes' arrows, and a printed word
 * has up to eight strokes -- eight screen clears to trace "kit" would be the
 * flashing the rendering rule in CLAUDE.md describes. So the old ring is
 * painted out in the background colour and the ghost and dots are painted
 * back over it, both idempotent, then the arrows recolour in place.
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
        drawArrows(tft);
        drawStartRing(tft);
        guideOnPanel_ = false;
        moveGuide(tft);
        drawProgress(tft);
        if (complete_) drawCompleteStatus(tft);
        paintedStroke_ = activeStroke_;
        paintedPoint_ = nextPoint_;
        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (paintedStroke_ != activeStroke_) {
        if (paintedStroke_ < strokeCount_) {
            const Pt& old = pts_[strokeStart_[paintedStroke_]];
            tft.drawCircle(old.x, old.y, START_RING_R, Ui::bg());
            tft.drawCircle(old.x, old.y, START_RING_R - 1, Ui::bg());
        }
        drawGhost(tft);
        drawAllDots(tft);
        drawArrows(tft);
        drawStartRing(tft);
        paintedStroke_ = activeStroke_;
        paintedPoint_ = nextPoint_;
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
            tft.fillCircle(pts_[idx].x, pts_[idx].y, dotRadius(), Ui::success());
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

    moveGuide(tft);
    drawProgress(tft);
    tft.setTextDatum(TL_DATUM);
}
