#include "BackgammonRules.h"

namespace Bg {

namespace {

bool allHome(const Position& p, uint8_t side) {
    if (p.bar[side] != 0) {
        return false;
    }
    const uint8_t lo = side == WHITE ? 6 : 0;
    const uint8_t hi = side == WHITE ? POINTS : 18;
    for (uint8_t i = lo; i < hi; ++i) {
        if (mine(p, side, i) != 0) {
            return false;
        }
    }
    return true;
}

/* Every place `side` could move a checker from right now: the bar alone while
 * anything is on it, otherwise each occupied point. */
uint8_t sources(const Position& p, uint8_t side, uint8_t* out) {
    if (p.bar[side] != 0) {
        out[0] = BAR;
        return 1;
    }
    uint8_t n = 0;
    for (uint8_t i = 0; i < POINTS; ++i) {
        if (mine(p, side, i) != 0) {
            out[n++] = i;
        }
    }
    return n;
}

bool allSame(const Dice& d) {
    for (uint8_t i = 1; i < d.count; ++i) {
        if (d.value[i] != d.value[0]) return false;
    }
    return d.count > 1;
}

/* The distinct values still to play, so a doubles turn is tried once per
 * source rather than four times over. */
uint8_t distinctValues(const Dice& d, uint8_t* out) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < d.count; ++i) {
        bool seen = false;
        for (uint8_t j = 0; j < n; ++j) seen = seen || out[j] == d.value[i];
        if (!seen) out[n++] = d.value[i];
    }
    return n;
}

/* The most dice usable, searched depth first with an early exit once every
 * die is used.
 *
 * Doubles are searched in a canonical order -- each move's source no further
 * from home than the one before -- which reaches every final position the full
 * search would. Moving one of your own checkers never closes a point against
 * another of yours, and entering from the bar and moving the back checkers
 * first is exactly the order that can only enable, never prevent, what comes
 * after. It takes the worst case from tens of thousands of nodes to a few
 * thousand; the host test checks the two searches agree. */
uint8_t search(const Position& p, uint8_t side, const Dice& d, uint8_t lastKey, uint8_t goal) {
    if (d.count == 0 || goal == 0) {
        return 0;
    }
    const bool doubles = allSame(d);
    uint8_t values[MAX_DICE];
    const uint8_t nv = distinctValues(d, values);
    uint8_t from[POINTS + 1];
    const uint8_t ns = sources(p, side, from);
    uint8_t best = 0;
    for (uint8_t v = 0; v < nv; ++v) {
        for (uint8_t s = 0; s < ns; ++s) {
            const uint8_t key = pipsToGo(side, from[s]);
            if (doubles && key > lastKey) {
                continue;
            }
            const uint8_t t = target(p, side, from[s], values[v]);
            if (t == NO_POINT) {
                continue;
            }
            Position c = p;
            Move m;
            m.from = from[s];
            m.to = t;
            m.die = values[v];
            apply(c, side, m);
            Dice r = d;
            useDie(r, values[v]);
            const uint8_t n = static_cast<uint8_t>(
                1 + search(c, side, r, doubles ? key : 255, static_cast<uint8_t>(goal - 1)));
            if (n > best) {
                best = n;
                if (best == goal) {
                    return best;
                }
            }
        }
    }
    return best;
}

}   // namespace

void setup(Position& p) {
    for (int8_t& v : p.pt) v = 0;
    /* White's 24, 13, 8 and 6 points; Black's are the mirror image. */
    p.pt[23] = 2;   p.pt[0] = -2;
    p.pt[12] = 5;   p.pt[11] = -5;
    p.pt[7] = 3;    p.pt[16] = -3;
    p.pt[5] = 5;    p.pt[18] = -5;
    p.bar[WHITE] = p.bar[BLACK] = 0;
    p.off[WHITE] = p.off[BLACK] = 0;
}

uint32_t mix(uint32_t x) {
    /* lowbias32, as in LudoRules: consecutive indices give unrelated dice. */
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

uint8_t die(uint32_t seed, uint16_t index) {
    const uint32_t h = mix(seed ^ mix(index * 0x9E3779B9U + 0x3C6EF372U));
    return static_cast<uint8_t>(h % 6 + 1);
}

void rollDice(uint32_t seed, uint16_t& rolls, Dice& out) {
    const uint8_t a = die(seed, rolls++);
    const uint8_t b = die(seed, rolls++);
    if (a == b) {
        out.count = 4;
        for (uint8_t& v : out.value) v = a;
    } else {
        out.count = 2;
        out.value[0] = a;
        out.value[1] = b;
        out.value[2] = out.value[3] = 0;
    }
}

void openingRoll(uint32_t seed, uint16_t& rolls, uint8_t& first, Dice& out) {
    for (uint8_t attempt = 0; attempt < 64; ++attempt) {
        const uint8_t w = die(seed, rolls++);
        const uint8_t b = die(seed, rolls++);
        if (w != b) {
            first = w > b ? WHITE : BLACK;
            out.count = 2;
            out.value[0] = w;
            out.value[1] = b;
            out.value[2] = out.value[3] = 0;
            return;
        }
    }
    /* Sixty-four ties in a row is one chance in 10^49; White and a 2-1. */
    first = WHITE;
    out.count = 2;
    out.value[0] = 2;
    out.value[1] = 1;
}

uint32_t tableSeed(uint8_t session, const char* idA, const char* idB) {
    /* Sorted, so both consoles hash the same bytes whichever of them asks. */
    bool aFirst = true;
    for (uint8_t i = 0; i < 4; ++i) {
        if (idA[i] != idB[i]) {
            aFirst = static_cast<uint8_t>(idA[i]) < static_cast<uint8_t>(idB[i]);
            break;
        }
    }
    const char* ids[2] = {aFirst ? idA : idB, aFirst ? idB : idA};
    uint32_t h = 2166136261U ^ (0xBAC6U * ((session & 0x3FU) + 1U));
    for (const char* id : ids) {
        for (uint8_t i = 0; i < 4 && id[i] != 0; ++i) {
            h = (h ^ static_cast<uint8_t>(id[i])) * 16777619U;   // FNV-1a
        }
    }
    return mix(h);
}

uint8_t target(const Position& p, uint8_t side, uint8_t from, uint8_t roll) {
    if (roll < 1 || roll > 6) {
        return NO_POINT;
    }
    const uint8_t opp = opponent(side);
    int16_t dest;
    if (from == BAR) {
        if (p.bar[side] == 0) return NO_POINT;
        dest = side == WHITE ? static_cast<int16_t>(POINTS - roll) : static_cast<int16_t>(roll - 1);
    } else {
        if (p.bar[side] != 0 || from >= POINTS || mine(p, side, from) == 0) return NO_POINT;
        dest = side == WHITE ? static_cast<int16_t>(from - roll) : static_cast<int16_t>(from + roll);
    }
    if (dest >= 0 && dest < POINTS) {
        return mine(p, opp, static_cast<uint8_t>(dest)) >= 2 ? NO_POINT
                                                             : static_cast<uint8_t>(dest);
    }
    /* Off the board: only with everything home, and a higher number only
     * from the highest occupied point. */
    if (!allHome(p, side)) {
        return NO_POINT;
    }
    const uint8_t dist = pipsToGo(side, from);
    if (roll == dist) {
        return OFF;
    }
    if (roll > dist) {
        if (side == WHITE) {
            for (uint8_t i = static_cast<uint8_t>(from + 1); i < 6; ++i) {
                if (mine(p, side, i) != 0) return NO_POINT;
            }
        } else {
            for (uint8_t i = 18; i < from; ++i) {
                if (mine(p, side, i) != 0) return NO_POINT;
            }
        }
        return OFF;
    }
    return NO_POINT;
}

bool apply(Position& p, uint8_t side, const Move& m) {
    const uint8_t opp = opponent(side);
    if (m.from == BAR) {
        --p.bar[side];
    } else {
        p.pt[m.from] = static_cast<int8_t>(p.pt[m.from] - sign(side));
    }
    if (m.to == OFF) {
        ++p.off[side];
        return false;
    }
    const bool hit = mine(p, opp, m.to) == 1;
    if (hit) {
        p.pt[m.to] = 0;
        ++p.bar[opp];
    }
    p.pt[m.to] = static_cast<int8_t>(p.pt[m.to] + sign(side));
    return hit;
}

void useDie(Dice& d, uint8_t value) {
    for (uint8_t i = 0; i < d.count; ++i) {
        if (d.value[i] == value) {
            for (uint8_t j = i; j + 1 < d.count; ++j) d.value[j] = d.value[j + 1];
            d.value[--d.count] = 0;
            return;
        }
    }
}

uint8_t maxUsable(const Position& p, uint8_t side, const Dice& d) {
    return search(p, side, d, 255, d.count);
}

uint8_t legalMoves(const Position& p, uint8_t side, const Dice& d, Move* out, uint8_t cap) {
    const uint8_t total = maxUsable(p, side, d);
    if (total == 0) {
        return 0;
    }
    uint8_t values[MAX_DICE];
    const uint8_t nv = distinctValues(d, values);
    uint8_t from[POINTS + 1];
    const uint8_t ns = sources(p, side, from);
    uint8_t n = 0;
    for (uint8_t v = 0; v < nv; ++v) {
        for (uint8_t s = 0; s < ns && n < cap; ++s) {
            const uint8_t t = target(p, side, from[s], values[v]);
            if (t == NO_POINT) continue;
            Move m;
            m.from = from[s];
            m.to = t;
            m.die = values[v];
            Position c = p;
            apply(c, side, m);
            Dice r = d;
            useDie(r, values[v]);
            if (1 + maxUsable(c, side, r) == total) {
                out[n++] = m;
            }
        }
    }
    /* Two different dice, only one of them usable in any sequence: the higher
     * if the higher can be played at all. */
    if (total == 1 && d.count == 2 && d.value[0] != d.value[1]) {
        const uint8_t hi = d.value[0] > d.value[1] ? d.value[0] : d.value[1];
        bool anyHi = false;
        for (uint8_t i = 0; i < n; ++i) anyHi = anyHi || out[i].die == hi;
        if (anyHi) {
            uint8_t k = 0;
            for (uint8_t i = 0; i < n; ++i) {
                if (out[i].die == hi) out[k++] = out[i];
            }
            n = k;
        }
    }
    return n;
}

bool findMove(const Position& p, uint8_t side, const Dice& d, uint8_t from, uint8_t to,
              Move& out) {
    Move moves[MAX_MOVES];
    const uint8_t n = legalMoves(p, side, d, moves, MAX_MOVES);
    bool found = false;
    for (uint8_t i = 0; i < n; ++i) {
        if (moves[i].from == from && moves[i].to == to && (!found || moves[i].die < out.die)) {
            out = moves[i];
            found = true;
        }
    }
    return found;
}

uint16_t pipCount(const Position& p, uint8_t side) {
    uint16_t total = static_cast<uint16_t>(p.bar[side] * 25);
    for (uint8_t i = 0; i < POINTS; ++i) {
        total = static_cast<uint16_t>(total + mine(p, side, i) * pipsToGo(side, i));
    }
    return total;
}

uint8_t checkerTotal(const Position& p, uint8_t side) {
    uint16_t total = static_cast<uint16_t>(p.bar[side] + p.off[side]);
    for (uint8_t i = 0; i < POINTS; ++i) total = static_cast<uint16_t>(total + mine(p, side, i));
    return static_cast<uint8_t>(total > 255 ? 255 : total);
}

Result result(const Position& p, uint8_t winner) {
    if (!won(p, winner)) {
        return Result::None;
    }
    const uint8_t loser = opponent(winner);
    if (p.off[loser] > 0) {
        return Result::Single;
    }
    if (p.bar[loser] > 0) {
        return Result::Backgammon;
    }
    const uint8_t lo = winner == WHITE ? 0 : 18;
    for (uint8_t i = lo; i < lo + 6; ++i) {
        if (mine(p, loser, i) != 0) return Result::Backgammon;
    }
    return Result::Gammon;
}

}   // namespace Bg
