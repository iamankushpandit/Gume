#include "GoRules.h"

#include <string.h>

/* The rules. See GoRules.h for the rules as played; this file only makes
 * them true. Nothing here allocates: every flood fill works in a stack
 * array sized for the largest board, which is 361 bytes of marks and 722
 * of stack -- a kilobyte of the loop task's stack for the duration of one
 * call, and gone before the next. */

namespace Go {

namespace {

/* Neighbours of `p`, up to four, into `out`. Written out rather than derived
 * from offsets because the board is not a torus: a point on the left edge
 * must not wrap to the right, and offset arithmetic on a flat array does
 * exactly that. */
uint8_t neighbours(uint8_t n, uint16_t p, uint16_t* out) {
    const uint8_t r = rowOf(n, p);
    const uint8_t c = colOf(n, p);
    uint8_t k = 0;
    if (r > 0) out[k++] = static_cast<uint16_t>(p - n);
    if (r + 1 < n) out[k++] = static_cast<uint16_t>(p + n);
    if (c > 0) out[k++] = static_cast<uint16_t>(p - 1);
    if (c + 1 < n) out[k++] = static_cast<uint16_t>(p + 1);
    return k;
}

/* Flood the group at `p` on `at`, marking each stone in `seen`. Returns the
 * liberty count; `stones`/`count` receive the group when wanted. `seen` is
 * shared across calls by the callers that walk every group once, which is
 * what makes counting every group on the board linear. */
uint16_t flood(uint8_t n, const uint8_t* at, uint16_t p, uint8_t* seen, uint16_t* stones,
               uint16_t* count) {
    const uint8_t colour = at[p];
    uint16_t stack[MAX_POINTS];
    uint16_t top = 0;
    uint16_t libs = 0;
    uint16_t found = 0;
    /* Liberties are counted once each: an empty point next to two stones of
     * the group is one liberty. `seen` doubles as the mark for those, with a
     * different value, so one array serves both. */
    stack[top++] = p;
    seen[p] = 1;
    uint16_t nb[4];
    while (top > 0) {
        const uint16_t q = stack[--top];
        if (stones != nullptr) stones[found] = q;
        ++found;
        const uint8_t k = neighbours(n, q, nb);
        for (uint8_t i = 0; i < k; ++i) {
            const uint16_t m = nb[i];
            if (seen[m] != 0) continue;
            if (at[m] == EMPTY) {
                seen[m] = 2;
                ++libs;
            } else if (at[m] == colour) {
                seen[m] = 1;
                stack[top++] = m;
            }
        }
    }
    if (count != nullptr) *count = found;
    return libs;
}

/* Clear only the liberty marks (2) that flood() left, so a caller walking
 * every group can reuse `seen` without a memset per group. */
void clearLibertyMarks(uint8_t n, uint8_t* seen) {
    const uint16_t total = points(n);
    for (uint16_t i = 0; i < total; ++i) {
        if (seen[i] == 2) seen[i] = 0;
    }
}

/* Remove the group at `p` from `at`, crediting `s.captured`. Returns how many. */
uint16_t removeGroup(State& s, uint16_t p) {
    uint8_t seen[MAX_POINTS];
    memset(seen, 0, points(s.n));
    uint16_t stones[MAX_POINTS];
    uint16_t count = 0;
    flood(s.n, s.at, p, seen, stones, &count);
    const uint8_t taker = other(s.at[p]);
    for (uint16_t i = 0; i < count; ++i) s.at[stones[i]] = EMPTY;
    s.captured[taker] = static_cast<uint16_t>(s.captured[taker] + count);
    return count;
}

void finishByScore(State& s) {
    Score sc;
    score(s, nullptr, sc);
    s.over = true;
    s.winner = sc.winner;
}

}   // namespace

const char* rulesName(Rules r) {
    switch (r) {
        case Rules::Capture1: return "Capture 1";
        case Rules::Capture3: return "Capture 3";
        case Rules::Capture5: return "Capture 5";
        case Rules::Area: return "Area";
        default: return "Territory";
    }
}

uint8_t captureTarget(Rules r) {
    switch (r) {
        case Rules::Capture1: return 1;
        case Rules::Capture3: return 3;
        case Rules::Capture5: return 5;
        default: return 0;
    }
}

void reset(State& s, uint8_t n, Rules rules) {
    s = State{};
    s.n = n > MAX_N ? MAX_N : (n < 5 ? 5 : n);
    s.rules = rules;
}

uint16_t group(const State& s, uint16_t p, uint16_t* stones, uint16_t& count) {
    count = 0;
    if (p >= points(s.n) || s.at[p] == EMPTY) return 0;
    uint8_t seen[MAX_POINTS];
    memset(seen, 0, points(s.n));
    return flood(s.n, s.at, p, seen, stones, &count);
}

uint16_t liberties(const State& s, uint16_t p) {
    if (p >= points(s.n) || s.at[p] == EMPTY) return 0;
    uint8_t seen[MAX_POINTS];
    memset(seen, 0, points(s.n));
    return flood(s.n, s.at, p, seen, nullptr, nullptr);
}

uint16_t captures(const State& s, uint16_t p) {
    if (p >= points(s.n) || s.at[p] != EMPTY) return 0;
    const uint8_t me = s.toMove;
    const uint8_t them = other(me);
    uint16_t nb[4];
    const uint8_t k = neighbours(s.n, p, nb);
    /* Each enemy group in atari beside p, counted once even when it touches
     * p on two sides: `seen` carries over between the four floods. */
    uint8_t seen[MAX_POINTS];
    memset(seen, 0, points(s.n));
    uint16_t taken = 0;
    for (uint8_t i = 0; i < k; ++i) {
        const uint16_t m = nb[i];
        if (s.at[m] != them || seen[m] != 0) continue;
        uint16_t count = 0;
        const uint16_t libs = flood(s.n, s.at, m, seen, nullptr, &count);
        clearLibertyMarks(s.n, seen);
        if (libs == 1) taken = static_cast<uint16_t>(taken + count);
    }
    return taken;
}

bool legal(const State& s, uint16_t p) {
    if (s.over) return false;
    if (p == PASS) return !isCapture(s.rules);
    if (p >= points(s.n) || s.at[p] != EMPTY || p == s.ko) return false;
    /* Not suicide: the stone has a liberty, or joins a group that keeps one,
     * or captures something. Cheapest test first. */
    uint16_t nb[4];
    const uint8_t k = neighbours(s.n, p, nb);
    for (uint8_t i = 0; i < k; ++i) {
        if (s.at[nb[i]] == EMPTY) return true;
    }
    if (captures(s, p) > 0) return true;
    /* Every neighbour is a stone and nothing is captured: legal only if an
     * own group beside p has a second liberty to share. */
    for (uint8_t i = 0; i < k; ++i) {
        if (s.at[nb[i]] == s.toMove && liberties(s, nb[i]) >= 2) return true;
    }
    return false;
}

bool play(State& s, uint16_t p) {
    if (!legal(s, p)) return false;
    const uint8_t me = s.toMove;
    const uint8_t them = other(me);
    ++s.moves;

    if (p == PASS) {
        ++s.passes;
        s.ko = NO_POINT;
        s.last = NO_POINT;
        s.toMove = them;
        if (s.passes >= 2) {
            if (s.rules == Rules::Area) {
                finishByScore(s);
            } else {
                s.over = true;   // Territory: the marking phase decides
            }
        }
        return true;
    }

    s.passes = 0;
    s.at[p] = me;
    /* Captures first, so the stone's own liberties are judged afterwards --
     * that is what makes a capturing move never suicide. */
    uint16_t nb[4];
    const uint8_t k = neighbours(s.n, p, nb);
    uint16_t taken = 0;
    uint16_t lastTaken = NO_POINT;
    for (uint8_t i = 0; i < k; ++i) {
        const uint16_t m = nb[i];
        if (s.at[m] != them) continue;
        if (liberties(s, m) == 0) {
            lastTaken = m;
            taken = static_cast<uint16_t>(taken + removeGroup(s, m));
        }
    }
    /* Simple ko: one stone took exactly one stone, and the taker now has a
     * single liberty, which is the point it took. */
    s.ko = NO_POINT;
    if (taken == 1 && liberties(s, p) == 1) {
        uint16_t stones[MAX_POINTS];
        uint16_t count = 0;
        group(s, p, stones, count);
        if (count == 1) s.ko = lastTaken;
    }
    s.last = p;
    s.toMove = them;

    const uint8_t target = captureTarget(s.rules);
    if (target != 0 && s.captured[me] >= target) {
        s.over = true;
        s.winner = me;
    }
    return true;
}

bool fillsOwnEye(const State& s, uint16_t p, uint8_t colour) {
    if (p >= points(s.n) || s.at[p] != EMPTY) return false;
    uint16_t nb[4];
    const uint8_t k = neighbours(s.n, p, nb);
    for (uint8_t i = 0; i < k; ++i) {
        if (s.at[nb[i]] != colour) return false;
    }
    /* The diagonals decide whether it is a real eye or a false one: on the
     * edge one enemy diagonal makes it false, in the middle two do. */
    const uint8_t r = rowOf(s.n, p);
    const uint8_t c = colOf(s.n, p);
    uint8_t enemy = 0;
    uint8_t offBoard = 0;
    const int8_t dr[4] = {-1, -1, 1, 1};
    const int8_t dc[4] = {-1, 1, -1, 1};
    for (uint8_t i = 0; i < 4; ++i) {
        const int16_t rr = static_cast<int16_t>(r + dr[i]);
        const int16_t cc = static_cast<int16_t>(c + dc[i]);
        if (rr < 0 || cc < 0 || rr >= s.n || cc >= s.n) {
            ++offBoard;
            continue;
        }
        if (s.at[point(s.n, static_cast<uint8_t>(rr), static_cast<uint8_t>(cc))] == other(colour)) {
            ++enemy;
        }
    }
    const uint8_t allowed = offBoard > 0 ? 0 : 1;
    return enemy <= allowed;
}

// ---- scoring --------------------------------------------------------------------

void ownership(const State& s, const uint8_t* dead, uint8_t* out) {
    const uint16_t total = points(s.n);
    /* The board with the dead removed. */
    uint8_t at[MAX_POINTS];
    for (uint16_t i = 0; i < total; ++i) {
        at[i] = (dead != nullptr && dead[i] != 0) ? EMPTY : s.at[i];
        out[i] = at[i];
    }
    /* Every empty region: flood it, note which colours it touches. One
     * colour makes it that colour's; two, or none, make it neutral. */
    uint8_t seen[MAX_POINTS];
    memset(seen, 0, total);
    uint16_t stack[MAX_POINTS];
    uint16_t region[MAX_POINTS];
    uint16_t nb[4];
    for (uint16_t start = 0; start < total; ++start) {
        if (at[start] != EMPTY || seen[start] != 0) continue;
        uint16_t top = 0;
        uint16_t size = 0;
        bool black = false;
        bool white = false;
        stack[top++] = start;
        seen[start] = 1;
        while (top > 0) {
            const uint16_t q = stack[--top];
            region[size++] = q;
            const uint8_t k = neighbours(s.n, q, nb);
            for (uint8_t i = 0; i < k; ++i) {
                const uint16_t m = nb[i];
                if (at[m] == BLACK) black = true;
                else if (at[m] == WHITE) white = true;
                else if (seen[m] == 0) {
                    seen[m] = 1;
                    stack[top++] = m;
                }
            }
        }
        const uint8_t owner = (black && !white) ? BLACK : (white && !black) ? WHITE : EMPTY;
        for (uint16_t i = 0; i < size; ++i) out[region[i]] = owner;
    }
}

void score(const State& s, const uint8_t* dead, Score& out) {
    out = Score{};
    const uint16_t total = points(s.n);
    if (isCapture(s.rules)) {
        out.half[BLACK] = static_cast<int16_t>(s.captured[BLACK] * 2);
        out.half[WHITE] = static_cast<int16_t>(s.captured[WHITE] * 2);
        out.prisoners[BLACK] = s.captured[BLACK];
        out.prisoners[WHITE] = s.captured[WHITE];
        out.winner = s.winner;
        return;
    }
    uint8_t own[MAX_POINTS];
    ownership(s, dead, own);
    for (uint16_t i = 0; i < total; ++i) {
        const bool isDead = dead != nullptr && dead[i] != 0;
        if (s.at[i] != EMPTY && !isDead) {
            ++out.stones[s.at[i]];
        } else if (own[i] != EMPTY) {
            ++out.territory[own[i]];
        }
        if (s.at[i] != EMPTY && isDead) ++out.prisoners[other(s.at[i])];
    }
    out.prisoners[BLACK] = static_cast<uint16_t>(out.prisoners[BLACK] + s.captured[BLACK]);
    out.prisoners[WHITE] = static_cast<uint16_t>(out.prisoners[WHITE] + s.captured[WHITE]);
    if (s.rules == Rules::Area) {
        out.half[BLACK] = static_cast<int16_t>((out.stones[BLACK] + out.territory[BLACK]) * 2);
        out.half[WHITE] = static_cast<int16_t>((out.stones[WHITE] + out.territory[WHITE]) * 2 + KOMI_HALF);
    } else {
        out.half[BLACK] = static_cast<int16_t>((out.territory[BLACK] + out.prisoners[BLACK]) * 2);
        out.half[WHITE] = static_cast<int16_t>((out.territory[WHITE] + out.prisoners[WHITE]) * 2 + KOMI_HALF);
    }
    out.winner = out.half[BLACK] > out.half[WHITE] ? BLACK : WHITE;
}

void toggleDead(const State& s, uint8_t* dead, uint16_t p) {
    if (p >= points(s.n) || s.at[p] == EMPTY) return;
    uint16_t stones[MAX_POINTS];
    uint16_t count = 0;
    group(s, p, stones, count);
    const uint8_t mark = dead[p] != 0 ? 0 : 1;
    for (uint16_t i = 0; i < count; ++i) dead[stones[i]] = mark;
}

// ---- on the air ------------------------------------------------------------------

namespace Net {

void encode(Kind kind, uint16_t point, uint8_t& from, uint8_t& to) {
    const uint16_t p = point == PASS ? 0 : static_cast<uint16_t>(point & POINT_MASK);
    const uint16_t word = static_cast<uint16_t>((static_cast<uint16_t>(kind) << 9) | p);
    from = static_cast<uint8_t>(word >> 5);
    to = static_cast<uint8_t>(TO_MIN | (word & 0x1F));
}

bool decode(uint8_t from, uint8_t to, Kind& kind, uint16_t& point) {
    if (from > 63 || to < TO_MIN || to > 63) return false;
    const uint16_t word = static_cast<uint16_t>((from << 5) | (to & 0x1F));
    const uint8_t k = static_cast<uint8_t>(word >> 9);
    if (k > static_cast<uint8_t>(Kind::Accept)) return false;
    kind = static_cast<Kind>(k);
    const uint16_t p = static_cast<uint16_t>(word & POINT_MASK);
    if (kind == Kind::Pass || kind == Kind::Accept) {
        point = PASS;
        return p == 0;
    }
    if (p >= MAX_POINTS) return false;
    point = p;
    return true;
}

}   // namespace Net

}   // namespace Go
