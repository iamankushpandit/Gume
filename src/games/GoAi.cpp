#include "GoRules.h"

#include <string.h>

/* The computer player. Pure, like the rules it plays by -- see GoRules.h.
 *
 * Two levels, one engine. Easy is a handful of heuristics -- take a capture,
 * save a group in atari, do not put yourself in one, prefer the third and
 * fourth lines, stay near the fight, never fill your own eye -- and answers
 * at once. Medium ranks the points by the same heuristics, keeps the best
 * dozen, and plays random games from each to see which wins most; those
 * playouts run a few at a time across frames (stepSearch), so a move takes
 * a moment and never a frame. The same playouts, run from a finished
 * position, are the computer's opinion of which groups are dead. */

namespace Go {

uint32_t Rng::next() {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

uint32_t Rng::below(uint32_t n) {
    return n == 0 ? 0 : next() % n;
}

namespace {

/** Which line from the nearest edge a point is on: 0 is the edge itself. */
uint8_t lineOf(uint8_t n, uint16_t p) {
    const uint8_t r = rowOf(n, p);
    const uint8_t c = colOf(n, p);
    uint8_t line = r;
    if (n - 1 - r < line) line = static_cast<uint8_t>(n - 1 - r);
    if (c < line) line = c;
    if (n - 1 - c < line) line = static_cast<uint8_t>(n - 1 - c);
    return line;
}

int32_t lineBonus(uint8_t n, uint8_t line) {
    if (n <= 9) {
        static const int16_t v[5] = {-30, 10, 40, 30, 20};
        return v[line > 4 ? 4 : line];
    }
    static const int16_t v[6] = {-30, 0, 40, 45, 25, 10};
    return v[line > 5 ? 5 : line];
}

uint8_t neighbours4(uint8_t n, uint16_t p, uint16_t* out) {
    const uint8_t r = rowOf(n, p);
    const uint8_t c = colOf(n, p);
    uint8_t k = 0;
    if (r > 0) out[k++] = static_cast<uint16_t>(p - n);
    if (r + 1 < n) out[k++] = static_cast<uint16_t>(p + n);
    if (c > 0) out[k++] = static_cast<uint16_t>(p - 1);
    if (c + 1 < n) out[k++] = static_cast<uint16_t>(p + 1);
    return k;
}

/* Is `p` worth looking at: within two of a stone, or the board is nearly
 * empty. On 19x19 this is what keeps a move under a frame's worth of work;
 * on 9x9 nearly everything qualifies once a few stones are down. */
bool near(const State& s, uint16_t p) {
    if (s.moves < 4) return true;
    const uint8_t r = rowOf(s.n, p);
    const uint8_t c = colOf(s.n, p);
    for (int8_t dr = -2; dr <= 2; ++dr) {
        for (int8_t dc = -2; dc <= 2; ++dc) {
            const int16_t rr = static_cast<int16_t>(r + dr);
            const int16_t cc = static_cast<int16_t>(c + dc);
            if (rr < 0 || cc < 0 || rr >= s.n || cc >= s.n) continue;
            if (s.at[point(s.n, static_cast<uint8_t>(rr), static_cast<uint8_t>(cc))] != EMPTY) {
                return true;
            }
        }
    }
    return false;
}

/* How good is `p` for s.toMove? Assumes legal(s, p). The result of playing
 * it is judged on a copy: liberties afterwards, whether it saved something,
 * whether it put something of the other side in atari. */
int32_t evaluatePoint(const State& s, uint16_t p, Rng& rng) {
    const uint8_t me = s.toMove;
    const uint8_t them = other(me);
    if (fillsOwnEye(s, p, me)) return -100000;

    int32_t v = 0;
    const uint16_t taken = captures(s, p);
    v += 1000 * taken;

    uint16_t nb[4];
    const uint8_t k = neighbours4(s.n, p, nb);
    /* Own groups in atari beside p, before the move. */
    uint16_t saveable = 0;
    for (uint8_t i = 0; i < k; ++i) {
        if (s.at[nb[i]] == me && liberties(s, nb[i]) == 1) {
            uint16_t stones[MAX_POINTS];
            uint16_t count = 0;
            group(s, nb[i], stones, count);
            saveable = static_cast<uint16_t>(saveable + count);
        }
    }

    State t = s;
    play(t, p);
    uint16_t stones[MAX_POINTS];
    uint16_t count = 0;
    const uint16_t libs = group(t, p, stones, count);
    if (libs == 1 && taken == 0) {
        v -= 400 + 60 * count;   // self-atari, and worse the more it risks
    } else if (saveable != 0 && libs >= 2) {
        v += 500 + 40 * saveable;
    }
    v += 30 * (libs > 4 ? 4 : libs);

    /* A threat: an enemy group left with one liberty. */
    for (uint8_t i = 0; i < k; ++i) {
        if (t.at[nb[i]] == them && liberties(t, nb[i]) == 1) {
            v += 120;
            break;
        }
    }

    v += lineBonus(s.n, lineOf(s.n, p));
    for (uint8_t i = 0; i < k; ++i) {
        if (s.at[nb[i]] == me) v += 8;
        else if (s.at[nb[i]] == them) v += 12;
    }
    if (s.last != NO_POINT) {
        const int16_t dr = static_cast<int16_t>(rowOf(s.n, p)) - rowOf(s.n, s.last);
        const int16_t dc = static_cast<int16_t>(colOf(s.n, p)) - colOf(s.n, s.last);
        const int16_t dist = static_cast<int16_t>((dr < 0 ? -dr : dr) + (dc < 0 ? -dc : dc));
        if (dist <= 2) v += 25;
        else if (dist <= 4) v += 8;
    }
    v += static_cast<int32_t>(rng.below(16));
    return v;
}

/* Every legal, sensible point, scored. Returns how many. */
uint16_t rankPoints(const State& s, Rng& rng, uint16_t* pts, int32_t* vals) {
    const uint16_t total = points(s.n);
    uint16_t n = 0;
    for (uint16_t p = 0; p < total; ++p) {
        if (s.at[p] != EMPTY || !near(s, p) || !legal(s, p)) continue;
        pts[n] = p;
        vals[n] = evaluatePoint(s, p, rng);
        ++n;
    }
    /* Insertion sort, best first. A few hundred entries at most and it runs
     * once per computer move. */
    for (uint16_t i = 1; i < n; ++i) {
        const uint16_t p = pts[i];
        const int32_t v = vals[i];
        uint16_t j = i;
        while (j > 0 && vals[j - 1] < v) {
            pts[j] = pts[j - 1];
            vals[j] = vals[j - 1];
            --j;
        }
        pts[j] = p;
        vals[j] = v;
    }
    return n;
}

/** A finished playout's winner by area, whatever the game's own rule set. */
uint8_t areaWinner(const State& s) {
    uint8_t own[MAX_POINTS];
    ownership(s, nullptr, own);
    int16_t black = 0;
    int16_t white = KOMI_HALF;
    const uint16_t total = points(s.n);
    for (uint16_t i = 0; i < total; ++i) {
        if (own[i] == BLACK) black = static_cast<int16_t>(black + 2);
        else if (own[i] == WHITE) white = static_cast<int16_t>(white + 2);
    }
    return black > white ? BLACK : WHITE;
}

/* A random legal move that is not an own eye, or PASS. Tries a few random
 * points first, then walks the board from a random start, so an early
 * position is cheap and a late one still finds the last move there is. */
uint16_t randomMove(const State& s, Rng& rng) {
    const uint16_t total = points(s.n);
    for (uint8_t tries = 0; tries < 10; ++tries) {
        const uint16_t p = static_cast<uint16_t>(rng.below(total));
        if (s.at[p] == EMPTY && !fillsOwnEye(s, p, s.toMove) && legal(s, p)) return p;
    }
    const uint16_t start = static_cast<uint16_t>(rng.below(total));
    for (uint16_t i = 0; i < total; ++i) {
        const uint16_t p = static_cast<uint16_t>((start + i) % total);
        if (s.at[p] == EMPTY && !fillsOwnEye(s, p, s.toMove) && legal(s, p)) return p;
    }
    return PASS;
}

}   // namespace

uint16_t chooseEasy(const State& s, Rng& rng) {
    if (s.over) return PASS;
    uint16_t pts[MAX_POINTS];
    int32_t vals[MAX_POINTS];
    const uint16_t n = rankPoints(s, rng, pts, vals);
    if (n == 0) return legal(s, PASS) ? PASS : NO_POINT;
    /* Nothing worth playing -- only eye-fills and self-ataris left -- is a
     * pass where passing is allowed. Under Capture rules there is no pass,
     * so the least bad move is played. */
    if (vals[0] < -50 && legal(s, PASS)) return PASS;
    return pts[0];
}

namespace {
/* Play `s` out with random moves; the finished board is left in `s`. */
void playOut(State& s, Rng& rng) {
    s.over = false;
    s.passes = 0;
    const uint16_t cap = static_cast<uint16_t>(2 * points(s.n));
    uint8_t passes = 0;
    for (uint16_t i = 0; i < cap && passes < 2; ++i) {
        const uint16_t p = randomMove(s, rng);
        if (p == PASS) {
            ++passes;
            if (!play(s, PASS)) s.toMove = other(s.toMove);
        } else {
            passes = 0;
            play(s, p);
        }
        if (s.over && s.winner != EMPTY) return;
    }
}
}   // namespace

void playoutOwnership(const State& s, Rng& rng, uint8_t* own) {
    State t = s;
    playOut(t, rng);
    ownership(t, nullptr, own);
}

uint8_t playout(State s, Rng& rng) {
    s.over = false;
    s.passes = 0;
    const uint16_t cap = static_cast<uint16_t>(2 * points(s.n));
    uint8_t passes = 0;
    for (uint16_t i = 0; i < cap && passes < 2; ++i) {
        const uint16_t p = randomMove(s, rng);
        if (p == PASS) {
            ++passes;
            /* Capture rules refuse a pass; the turn simply goes over. */
            if (!play(s, PASS)) s.toMove = other(s.toMove);
        } else {
            passes = 0;
            play(s, p);
        }
        if (s.over && s.winner != EMPTY) return s.winner;
    }
    return areaWinner(s);
}

uint16_t playoutsFor(uint8_t n) {
    return n <= 9 ? 300 : 40;
}

void beginSearch(Search& sr, const State& s, uint32_t seed) {
    sr.root = s;
    sr.rng.x = seed | 1U;
    sr.candidates = 0;
    sr.done = 0;
    uint16_t pts[MAX_POINTS];
    int32_t vals[MAX_POINTS];
    const uint16_t n = rankPoints(s, sr.rng, pts, vals);
    /* The best dozen by the heuristics, so three hundred playouts say
     * something about each rather than five about sixty. */
    const uint16_t keep = n < 12 ? n : 12;
    for (uint16_t i = 0; i < keep; ++i) {
        if (vals[i] < -50 && i > 0) break;   // the rest are eye-fills and self-ataris
        sr.candidate[sr.candidates] = pts[i];
        sr.wins[sr.candidates] = 0;
        sr.plays[sr.candidates] = 0;
        ++sr.candidates;
    }
    if (legal(s, PASS) && (sr.candidates == 0 || vals[0] < 0)) {
        sr.candidate[sr.candidates] = PASS;
        sr.wins[sr.candidates] = 0;
        sr.plays[sr.candidates] = 0;
        ++sr.candidates;
    }
    sr.target = sr.candidates == 0 ? 0 : playoutsFor(s.n);
}

uint16_t bestMove(const Search& sr) {
    uint16_t best = NO_POINT;
    int32_t bestScore = -1;
    for (uint16_t i = 0; i < sr.candidates; ++i) {
        if (sr.plays[i] == 0 || sr.plays[i] == 0xFFFF) continue;
        /* Win rate in thousandths, with a nudge for the heuristic order so
         * an untested tie goes to the move the heuristics liked. */
        const int32_t rate = (static_cast<int32_t>(sr.wins[i]) * 1000) / sr.plays[i] - i;
        if (rate > bestScore) {
            bestScore = rate;
            best = sr.candidate[i];
        }
    }
    if (best == NO_POINT && sr.candidates > 0) best = sr.candidate[0];
    return best == NO_POINT ? PASS : best;
}

void estimateDead(const State& s, uint8_t* dead, uint32_t seed, uint16_t playouts) {
    const uint16_t total = points(s.n);
    uint16_t alive[MAX_POINTS];
    memset(alive, 0, sizeof(alive));
    Rng rng;
    rng.x = seed | 1U;
    for (uint16_t k = 0; k < playouts; ++k) {
        uint8_t own[MAX_POINTS];
        playoutOwnership(s, rng, own);
        for (uint16_t i = 0; i < total; ++i) {
            if (s.at[i] != EMPTY && own[i] == s.at[i]) ++alive[i];
        }
    }
    for (uint16_t i = 0; i < total; ++i) {
        dead[i] = (s.at[i] != EMPTY && alive[i] * 2 < playouts) ? 1 : 0;
    }
}

}   // namespace Go
