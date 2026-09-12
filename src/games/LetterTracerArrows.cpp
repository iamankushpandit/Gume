#include "LetterTracer.h"
#include "LetterTracerLayout.h"

#include <math.h>

/* WHERE THE DIRECTION ARROWS GO.
 *
 * Every stroke gets one short arrow beside its start, carrying the stroke's
 * number, and -- in a set that asks for them -- one more beside each sharp
 * reversal part-way along. That is how a handwriting workbook marks a letter,
 * and it is what replaced the single arrow that used to sit ON the path and
 * jump from turn to turn: five-year-olds in testing could not tell it apart
 * from the dots it was covering.
 *
 * "Beside" means parallel to the stroke, a few pixels off it. "Outside" means
 * on whichever side leads away from the middle of the letter, so the arrow
 * reads as a label on the shape rather than a mark inside it. That is a
 * preference, not a rule: an arrow that would sit outside but touch another
 * stroke, run off the canvas or land on another arrow tries sliding a little
 * further along its stroke, then standing further off, and only then the
 * inside. Every candidate is measured, never assumed, against the strokes as
 * the dots draw them.
 *
 * All of it runs once per glyph, from loadGlyph(). The arrows then never move,
 * which is also what lets the repaint stay incremental: a stroke finishing
 * recolours two arrows in place and nothing has to be erased. */

using namespace LetterTracerLayout;

namespace {

/* How far along the stroke to look when working out which way it is going.
 * Long enough to see past the few pixels of jitter in a generated cursive
 * table, short enough not to see round the next bend. */
constexpr float REACH = 10.0f;
/* A stroke shorter than this is a mark -- the dot on an i -- and has no
 * direction worth pointing out. The start ring still says where it is. */
constexpr float MIN_STROKE = 12.0f;
/* How far along its stroke an arrow may slide to find room. 'a' needs most of
 * it: its bowl starts right against its own stem, and the outside of the
 * curve is only clear once the stroke has come over the top. */
constexpr uint8_t SLIDES[] = {0, 6, 12, 18, 24, 30};
/* The label is a font-1 digit, 6x8, drawn centred on its point. */
constexpr int16_t LABEL_HALF_W = 3;
constexpr int16_t LABEL_HALF_H = 4;
constexpr int16_t LABEL_R = 4;
/* Anything below this is an arrow drawn over a stroke. Better none at all. */
constexpr float LAST_RESORT = 2.0f;

struct Candidate {
    float tailX, tailY, tipX, tipY, labelX, labelY;
    float ux, uy, nx, ny;
    bool numbered;
};

/* Points along one arrow that must all be clear: the shaft every 3px, the tip
 * and both corners of the head. The label is tested on its own. */
uint8_t shaftSamples(const Candidate& c, float* xs, float* ys) {
    uint8_t n = 0;
    for (int16_t t = 0; t < ARROW_LEN; t += 3) {
        xs[n] = c.tailX + c.ux * t;
        ys[n] = c.tailY + c.uy * t;
        ++n;
    }
    xs[n] = c.tipX;
    ys[n] = c.tipY;
    ++n;
    const float hx = c.tipX - c.ux * ARROW_HEAD;
    const float hy = c.tipY - c.uy * ARROW_HEAD;
    xs[n] = hx + c.nx * ARROW_HALF;
    ys[n] = hy + c.ny * ARROW_HALF;
    ++n;
    xs[n] = hx - c.nx * ARROW_HALF;
    ys[n] = hy - c.ny * ARROW_HALF;
    ++n;
    return n;
}

constexpr uint8_t MAX_SAMPLES = ARROW_LEN / 3 + 4;

bool inBounds(float x, float y) {
    return x >= ARROW_MIN_X && x <= ARROW_MAX_X && y >= ARROW_MIN_Y && y <= ARROW_MAX_Y;
}

/* Squared distance from (px, py) to the segment a-b. */
float segDist2(float px, float py, float ax, float ay, float bx, float by) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float apx = px - ax;
    const float apy = py - ay;
    const float l2 = abx * abx + aby * aby;
    float t = l2 > 0.0f ? (apx * abx + apy * aby) / l2 : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const float ex = apx - abx * t;
    const float ey = apy - aby * t;
    return ex * ex + ey * ey;
}

}   // namespace

/* Distance from (x, y) to the nearest part of any stroke, as the dots draw it.
 *
 * Measured against the resampled waypoints rather than the raw table: they are
 * in RAM and already in pixels, and on the tightest curve here a chord between
 * two of them strays from the true line by under two pixels.
 *
 * Returns as soon as it finds anything nearer than `floor`: a candidate that
 * close is rejected whatever the exact figure, and most candidates are
 * rejected, so this is where the planner's time goes. */
float LetterTracer::clearance(float x, float y, float floor) const {
    const float stop = floor > 0.0f ? floor * floor : -1.0f;
    float best = 1.0e9f;
    for (uint8_t s = 0; s < strokeCount_; ++s) {
        /* A stroke whose box is further than the floor cannot be the reason
         * a candidate is rejected, so with a floor it need not be measured:
         * the answer may then come back larger than the truth, but never
         * below the floor when the truth is above it, which is all a floor
         * asks. Without one, everything is measured. */
        if (floor > 0.0f) {
            const int16_t* b = strokeBox_[s];
            if (x < b[0] - floor || x > b[1] + floor ||
                y < b[2] - floor || y > b[3] + floor) {
                continue;
            }
        }
        const uint8_t start = strokeStart_[s];
        const uint8_t len = strokeLen_[s];
        for (uint8_t i = 0; i < len; ++i) {
            const Pt& a = pts_[start + i];
            const Pt& b = (i + 1 < len) ? pts_[start + i + 1] : a;
            const float d2 = segDist2(x, y, a.x, a.y, b.x, b.y);
            if (d2 < best) {
                best = d2;
                if (best < stop) return sqrtf(best);
            }
        }
    }
    return sqrtf(best);
}

/* WHERE THE MOVING GUIDE GOES.
 *
 * Beside the dot the finger is aiming at, pointing the way the stroke goes
 * next. Deliberately simpler than placeArrow(): it is recomputed every time a
 * dot is claimed, so it tries the two sides of the line and takes the clearer,
 * rather than searching slides and stand-offs. It also gives up rather than
 * draw over the letter, because an arrow that hides the dots is the thing this
 * whole design is fixing.
 *
 * The guide points from the target dot along the stroke, not from the finger:
 * the finger's own position is not sampled between dots, and the question a
 * child has at that moment is where the line goes AFTER the dot they are
 * reaching for. */
void LetterTracer::updateGuide() {
    guideShown_ = false;
    if (complete_ || activeStroke_ >= strokeCount_) return;
    const uint8_t start = strokeStart_[activeStroke_];
    const uint8_t len = strokeLen_[activeStroke_];
    if (nextPoint_ + 1 >= len) return;      // the last dot has nowhere to point
    const uint8_t at = static_cast<uint8_t>(start + nextPoint_);

    const float px = pts_[at].x;
    const float py = pts_[at].y;
    const float dx = static_cast<float>(pts_[at + 1].x - pts_[at].x);
    const float dy = static_cast<float>(pts_[at + 1].y - pts_[at].y);
    const float len2 = sqrtf(dx * dx + dy * dy);
    if (len2 < 0.5f) return;
    const float ux = dx / len2;
    const float uy = dy / len2;

    float bestClear = -1.0e9f;
    for (int8_t side = 1; side >= -1; side -= 2) {
        const float nx = -uy * side;
        const float ny = ux * side;
        const float tailX = px + nx * GUIDE_OFFSET;
        const float tailY = py + ny * GUIDE_OFFSET;
        const float tipX = tailX + ux * ARROW_LEN;
        const float tipY = tailY + uy * ARROW_LEN;
        float worst = 1.0e9f;
        bool inside = true;
        for (int16_t t = 0; t <= ARROW_LEN; t += 4) {
            const float x = tailX + ux * t;
            const float y = tailY + uy * t;
            if (!inBounds(x, y)) {
                inside = false;
                break;
            }
            const float c = clearance(x, y, 0.0f);
            if (c < worst) worst = c;
        }
        if (!inside || worst <= bestClear) continue;
        bestClear = worst;
        guide_.tailX = static_cast<int16_t>(lroundf(tailX));
        guide_.tailY = static_cast<int16_t>(lroundf(tailY));
        guide_.tipX = static_cast<int16_t>(lroundf(tipX));
        guide_.tipY = static_cast<int16_t>(lroundf(tipY));
        guide_.labelX = 0;
        guide_.labelY = 0;
        guide_.stroke = activeStroke_;
        guide_.numbered = false;
        guideShown_ = true;
    }
    /* Nowhere clear beside the line: better nothing than an arrow over the
     * dots. The dots themselves still say where to go. */
    if (bestClear < GUIDE_CLEAR) guideShown_ = false;
}

void LetterTracer::planArrows() {
    arrowCount_ = 0;
    if (glyphs_ == nullptr) return;
    const Glyph& g = glyph();
    const bool turns = setCount_ > 0 && set().turnArrows;

    /* Every stroke's box, and the whole glyph's middle, once per glyph. */
    int16_t minX = INT16_MAX, maxX = INT16_MIN, minY = INT16_MAX, maxY = INT16_MIN;
    for (uint8_t k = 0; k < strokeCount_; ++k) {
        int16_t* b = strokeBox_[k];
        b[0] = INT16_MAX;
        b[1] = INT16_MIN;
        b[2] = INT16_MAX;
        b[3] = INT16_MIN;
        for (uint8_t i = 0; i < strokeLen_[k]; ++i) {
            const Pt& p = pts_[strokeStart_[k] + i];
            if (p.x < b[0]) b[0] = p.x;
            if (p.x > b[1]) b[1] = p.x;
            if (p.y < b[2]) b[2] = p.y;
            if (p.y > b[3]) b[3] = p.y;
        }
        if (b[0] < minX) minX = b[0];
        if (b[1] > maxX) maxX = b[1];
        if (b[2] < minY) minY = b[2];
        if (b[3] > maxY) maxY = b[3];
    }
    const float cx = (minX + maxX) / 2.0f;
    const float cy = (minY + maxY) / 2.0f;

    for (uint8_t s = 0; s < g.strokeCount && s < MAX_STROKES; ++s) {
        const Stroke& st = g.strokes[s];
        if (st.count < 2) continue;

        /* Walk the stroke once, in pixels, noting its length so far at each
         * authored vertex -- a turn's arrow starts from that distance. */
        float arc = 0.0f;
        float total = 0.0f;
        for (uint8_t i = 0; i + 1 < st.count; ++i) {
            const float dx = (st.pts[(i + 1) * 2] - st.pts[i * 2]) * boxScale_;
            const float dy = (st.pts[(i + 1) * 2 + 1] - st.pts[i * 2 + 1]) * boxScale_;
            total += sqrtf(dx * dx + dy * dy);
        }
        if (total < MIN_STROKE) continue;

        placeArrow(s, 0.0f, true, cx, cy);
        if (!turns) continue;

        for (uint8_t i = 1; i + 1 < st.count; ++i) {
            const float ax = static_cast<float>(st.pts[i * 2] - st.pts[(i - 1) * 2]);
            const float ay = static_cast<float>(st.pts[i * 2 + 1] - st.pts[(i - 1) * 2 + 1]);
            const float bx = static_cast<float>(st.pts[(i + 1) * 2] - st.pts[i * 2]);
            const float by = static_cast<float>(st.pts[(i + 1) * 2 + 1] - st.pts[i * 2 + 1]);
            const float la = sqrtf(ax * ax + ay * ay);
            const float lb = sqrtf(bx * bx + by * by);
            arc += la * boxScale_;
            if (la < 0.5f || lb < 0.5f) continue;
            /* cos of the turn: 1 is straight on, -1 is straight back. */
            if ((ax * bx + ay * by) / (la * lb) < TURN_COS) {
                placeArrow(s, arc, false, cx, cy);
            }
        }
    }
}

/* Find room for one arrow and record it, or record nothing.
 *
 * Candidates are tried in order of preference and the first that is clear
 * wins: outside before inside, then the least slide along the stroke, then the
 * nearer stand-off. Only if none is clear is the least-bad one taken, and only
 * if it still does not actually touch a stroke. */
void LetterTracer::placeArrow(uint8_t s, float arc, bool numbered, float cx, float cy) {
    if (arrowCount_ >= MAX_ARROWS) return;
    const Stroke& st = glyph().strokes[s];

    /* The point `dist` pixels along the stroke, by walking its segments. */
    auto pointAt = [&](float dist, float& px, float& py) {
        float walked = 0.0f;
        for (uint8_t i = 0; i + 1 < st.count; ++i) {
            const float ax = boxX_ + st.pts[i * 2] * boxScale_;
            const float ay = boxY_ + st.pts[i * 2 + 1] * boxScale_;
            const float bx = boxX_ + st.pts[(i + 1) * 2] * boxScale_;
            const float by = boxY_ + st.pts[(i + 1) * 2 + 1] * boxScale_;
            const float len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
            if (len > 0.0f && walked + len >= dist) {
                const float t = (dist - walked) / len;
                px = ax + (bx - ax) * t;
                py = ay + (by - ay) * t;
                return true;
            }
            walked += len;
        }
        px = boxX_ + st.pts[(st.count - 1) * 2] * boxScale_;
        py = boxY_ + st.pts[(st.count - 1) * 2 + 1] * boxScale_;
        return false;    // ran off the end
    };

    float xs[MAX_SAMPLES];
    float ys[MAX_SAMPLES];

    /* How clear a candidate is: the smallest gap, over everything it draws,
     * to a stroke, to a start ring, or to an arrow already placed. Stops early
     * once it is below `need`, because then the answer is simply "no". */
    auto measure = [&](const Candidate& c, float need) {
        const uint8_t n = shaftSamples(c, xs, ys);
        /* Off the canvas is a straight no, and costs nothing to find out. */
        for (uint8_t i = 0; i < n; ++i) {
            if (!inBounds(xs[i], ys[i])) return -1.0e9f;
        }
        if (c.numbered &&
            (!inBounds(c.labelX - LABEL_HALF_W, c.labelY - LABEL_HALF_H) ||
             !inBounds(c.labelX + LABEL_HALF_W, c.labelY + LABEL_HALF_H))) {
            return -1.0e9f;
        }
        float worst = 1.0e9f;
        auto consider = [&](float x, float y, float pad) {
            if (worst < need) return;
            for (uint8_t k = 0; k < strokeCount_; ++k) {
                const Pt& r = pts_[strokeStart_[k]];
                const float d = sqrtf((x - r.x) * (x - r.x) + (y - r.y) * (y - r.y)) -
                                START_RING_R - 1 - pad;
                if (d < worst) worst = d;
            }
            /* An arrow already placed is its shaft plus half its head's
             * width, and its label a small disc. */
            for (uint8_t a = 0; a < arrowCount_; ++a) {
                const Arrow& o = arrows_[a];
                float d = sqrtf(segDist2(x, y, o.tailX, o.tailY, o.tipX, o.tipY)) -
                          ARROW_HALF - 2 - pad;
                if (d < worst) worst = d;
                if (o.numbered) {
                    d = sqrtf((x - o.labelX) * (x - o.labelX) +
                              (y - o.labelY) * (y - o.labelY)) - LABEL_R - 2 - pad;
                    if (d < worst) worst = d;
                }
            }
            if (worst < need) return;
            const float d = clearance(x, y, need + pad) - pad;
            if (d < worst) worst = d;
        };
        for (uint8_t i = 0; i < n; ++i) consider(xs[i], ys[i], 0.0f);
        if (c.numbered) consider(c.labelX, c.labelY, LABEL_R);
        return worst;
    };

    /* Candidate `k`, in preference order within a pass: least slide first,
     * then the nearer stand-off, then either side. False if `k` slides off
     * the end of the stroke or the stroke has no direction there. */
    constexpr uint8_t OFFSET_COUNT = sizeof(ARROW_OFFSETS) / sizeof(ARROW_OFFSETS[0]);
    constexpr uint8_t PER_SLIDE = OFFSET_COUNT * 2;
    constexpr uint8_t CANDIDATES = sizeof(SLIDES) * PER_SLIDE;
    auto candidate = [&](uint8_t k, Candidate& c, bool& outside) {
        const uint8_t slide = SLIDES[k / PER_SLIDE];
        const int16_t off = ARROW_OFFSETS[(k / 2) % OFFSET_COUNT];
        const float side = (k % 2) == 0 ? 1.0f : -1.0f;
        float px, py, qx, qy;
        pointAt(arc + slide, px, py);
        const bool reached = pointAt(arc + slide + REACH, qx, qy);
        if (slide > 0 && !reached) return false;
        const float dx = qx - px;
        const float dy = qy - py;
        const float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.5f) return false;
        c.ux = dx / len;
        c.uy = dy / len;
        c.nx = -c.uy * side;
        c.ny = c.ux * side;
        c.numbered = numbered;
        c.tailX = px + c.ux * 2 + c.nx * off;
        c.tailY = py + c.uy * 2 + c.ny * off;
        c.tipX = c.tailX + c.ux * ARROW_LEN;
        c.tipY = c.tailY + c.uy * ARROW_LEN;
        c.labelX = c.tailX - c.ux * ARROW_LABEL_BACK + c.nx * ARROW_LABEL_OUT;
        c.labelY = c.tailY - c.uy * ARROW_LABEL_BACK + c.ny * ARROW_LABEL_OUT;
        const float mx = (c.tailX + c.tipX) / 2.0f;
        const float my = (c.tailY + c.tipY) / 2.0f;
        outside = (mx - cx) * (mx - cx) + (my - cy) * (my - cy) >
                  (px - cx) * (px - cx) + (py - cy) * (py - cy);
        return true;
    };

    Candidate best{};
    bool found = false;
    /* Outside first, then inside; the first clear candidate wins. */
    for (uint8_t pass = 0; pass < 2 && !found; ++pass) {
        for (uint8_t k = 0; k < CANDIDATES && !found; ++k) {
            Candidate c{};
            bool outside = false;
            if (!candidate(k, c, outside) || outside != (pass == 0)) continue;
            if (measure(c, ARROW_CLEAR) >= ARROW_CLEAR) {
                best = c;
                found = true;
            }
        }
    }
    /* Nothing clear anywhere: the least-bad, if it does not touch a stroke.
     * Rare, and the only case that pays for measuring every candidate fully. */
    if (!found) {
        float bestClear = -1.0e9f;
        for (uint8_t k = 0; k < CANDIDATES; ++k) {
            Candidate c{};
            bool outside = false;
            if (!candidate(k, c, outside)) continue;
            const float clear = measure(c, -1.0e9f);
            if (clear > bestClear) {
                best = c;
                bestClear = clear;
            }
        }
        if (bestClear < LAST_RESORT) return;
    }

    Arrow& a = arrows_[arrowCount_++];
    a.tailX = static_cast<int16_t>(lroundf(best.tailX));
    a.tailY = static_cast<int16_t>(lroundf(best.tailY));
    a.tipX = static_cast<int16_t>(lroundf(best.tipX));
    a.tipY = static_cast<int16_t>(lroundf(best.tipY));
    a.labelX = static_cast<int16_t>(lroundf(best.labelX));
    a.labelY = static_cast<int16_t>(lroundf(best.labelY));
    a.stroke = s;
    a.numbered = numbered;
}
