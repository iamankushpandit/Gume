#include "BackgammonRules.h"

/* The computer player. Pure, like the rules it plays by -- see
 * BackgammonRules.h for choosePlan()'s contract. */

namespace Bg {

namespace {

/* A hard ceiling on nodes, so no position however strange can hold the loop
 * past its frame budget. The canonical ordering keeps real positions to a few
 * thousand; the game still checks every planned move with findMove() and
 * falls back to the first legal one, so a truncated search can never play an
 * illegal move -- only a weaker one. */
constexpr uint32_t NODE_LIMIT = 20000;

inline bool inHome(uint8_t side, uint8_t i) {
    return side == WHITE ? i < 6 : i >= 18;
}

/* Opposing checkers that could land on point `i` with one die: those 1..6
 * pips behind it in their own direction, and those on the bar if `i` is where
 * they enter. Blocking in between is ignored -- it is a threat count, not a
 * move generator. */
uint8_t hitters(const Position& p, uint8_t side, uint8_t i) {
    const uint8_t opp = opponent(side);
    uint8_t n = 0;
    for (uint8_t d = 1; d <= 6; ++d) {
        /* The opponent reaches i from i + d if it is White (moving down), or
         * from i - d if it is Black. */
        const int16_t from = opp == WHITE ? static_cast<int16_t>(i + d) : static_cast<int16_t>(i - d);
        if (from >= 0 && from < POINTS) {
            n = static_cast<uint8_t>(n + mine(p, opp, static_cast<uint8_t>(from)));
        }
    }
    const bool entry = opp == WHITE ? i >= 18 : i < 6;
    if (entry) {
        n = static_cast<uint8_t>(n + p.bar[opp]);
    }
    return n;
}

int32_t evaluate(const Position& p, uint8_t side) {
    const uint8_t opp = opponent(side);
    int32_t v = (static_cast<int32_t>(pipCount(p, opp)) - pipCount(p, side)) * 2;
    uint8_t run = 0;
    uint8_t bestRun = 0;
    for (uint8_t i = 0; i < POINTS; ++i) {
        const uint8_t m = mine(p, side, i);
        if (m >= 2) {
            v += 6;
            if (inHome(side, i)) v += 8;
            run = static_cast<uint8_t>(run + 1);
            if (run > bestRun) bestRun = run;
        } else {
            run = 0;
            if (m == 1) {
                const uint8_t threats = hitters(p, side, i);
                if (threats > 0) {
                    /* A blot deep in our own home costs more to lose: it goes
                     * back the whole board. */
                    v -= 4 + 6 * threats + pipsToGo(side, i) / 4;
                }
            }
        }
    }
    v += bestRun * bestRun * 2;
    v += 12 * p.bar[opp];
    v -= 12 * p.bar[side];
    v += 4 * p.off[side];
    return v;
}

struct Search {
    uint8_t side;
    uint8_t hi;              // the higher of two different dice, else 0
    uint8_t bestDepth = 0;
    bool bestUsedHi = false;
    int32_t bestScore = 0;
    bool haveBest = false;
    uint32_t nodes = 0;
    Move path[MAX_DICE];
    Plan best;
};

void dfs(Search& s, const Position& p, const Dice& d, uint8_t depth, uint8_t lastKey, bool usedHi) {
    if (++s.nodes > NODE_LIMIT) {
        return;
    }
    bool moved = false;
    if (d.count > 0) {
        bool doubles = d.count > 1;
        for (uint8_t i = 1; i < d.count; ++i) doubles = doubles && d.value[i] == d.value[0];
        uint8_t values[MAX_DICE];
        uint8_t nv = 0;
        for (uint8_t i = 0; i < d.count; ++i) {
            bool seen = false;
            for (uint8_t j = 0; j < nv; ++j) seen = seen || values[j] == d.value[i];
            if (!seen) values[nv++] = d.value[i];
        }
        for (uint8_t v = 0; v < nv; ++v) {
            for (uint8_t from = 0; from <= BAR; ++from) {
                if (from < POINTS && mine(p, s.side, from) == 0) continue;
                const uint8_t key = pipsToGo(s.side, from);
                if (doubles && key > lastKey) continue;
                const uint8_t t = target(p, s.side, from, values[v]);
                if (t == NO_POINT) continue;
                moved = true;
                Move m;
                m.from = from;
                m.to = t;
                m.die = values[v];
                Position c = p;
                apply(c, s.side, m);
                Dice r = d;
                useDie(r, values[v]);
                s.path[depth] = m;
                dfs(s, c, r, static_cast<uint8_t>(depth + 1), doubles ? key : 255,
                    usedHi || values[v] == s.hi);
            }
        }
    }
    if (moved) {
        return;
    }
    /* A complete sequence. Using more dice beats fewer, which is the rule;
     * with one die of two usable, the higher beats the lower, which is also
     * the rule; only then does the position's score decide. */
    const int32_t score = evaluate(p, s.side);
    const bool hiRanks = s.hi != 0 && depth == 1;
    bool better = !s.haveBest || depth > s.bestDepth;
    if (!better && depth == s.bestDepth) {
        if (hiRanks && usedHi != s.bestUsedHi) {
            better = usedHi;
        } else {
            better = score > s.bestScore;
        }
    }
    if (better) {
        s.haveBest = true;
        s.bestDepth = depth;
        s.bestUsedHi = usedHi;
        s.bestScore = score;
        s.best.count = depth;
        for (uint8_t i = 0; i < depth; ++i) s.best.moves[i] = s.path[i];
    }
}

}   // namespace

void choosePlan(const Position& p, uint8_t side, const Dice& d, Plan& out) {
    Search s;
    s.side = side;
    s.hi = (d.count == 2 && d.value[0] != d.value[1])
               ? (d.value[0] > d.value[1] ? d.value[0] : d.value[1])
               : 0;
    dfs(s, p, d, 0, 255, false);
    out = s.best;
}

}   // namespace Bg
