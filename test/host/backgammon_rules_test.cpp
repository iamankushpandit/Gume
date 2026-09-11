// Host-side test of the Backgammon rules and computer player, compiled against
// the real src/games/BackgammonRules.cpp and BackgammonAi.cpp. Both are pure
// C++ with no Arduino in them; this file is what proves it, since it will not
// compile otherwise. Not run by CI; build it with any host C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src/games test/host/backgammon_rules_test.cpp src/games/BackgammonRules.cpp src/games/BackgammonAi.cpp -o bg_test && ./bg_test
//
// What it checks: the start, bar entry, a closed board, the higher-die rule,
// the bear-off cases, a hit during bear-off, Gammon and Backgammon; then, on
// positions from hundreds of random games, that legalMoves() is exactly the
// set a brute-force search over every move order allows (which is what proves
// the canonical ordering for doubles loses nothing), and that the computer
// only ever plays legal turns and every game finishes with fifteen a side.
#include "BackgammonRules.h"

#include <cstdio>
#include <cstring>

using namespace Bg;

static int failures = 0;
static long checks = 0;
#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        ++checks;                                                         \
        if (!(cond)) {                                                    \
            ++failures;                                                   \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                     \
            std::printf("\n");                                            \
        }                                                                 \
    } while (0)

static Position empty() {
    Position p;
    std::memset(&p, 0, sizeof(p));
    return p;
}

static Dice dice(uint8_t a, uint8_t b) {
    Dice d;
    if (a == b) {
        d.count = 4;
        for (uint8_t& v : d.value) v = a;
    } else {
        d.count = 2;
        d.value[0] = a;
        d.value[1] = b;
    }
    return d;
}

static void testSetup() {
    Position p;
    setup(p);
    CHECK(checkerTotal(p, WHITE) == 15 && checkerTotal(p, BLACK) == 15, "fifteen a side");
    CHECK(pipCount(p, WHITE) == 167 && pipCount(p, BLACK) == 167, "167 pips each: %u %u",
          pipCount(p, WHITE), pipCount(p, BLACK));
    CHECK(pipsToGo(WHITE, 0) == 1 && pipsToGo(BLACK, 23) == 1 && pipsToGo(WHITE, BAR) == 25, "pips");
}

static void testBar() {
    Position p;
    setup(p);
    p.pt[23] = 1;
    p.bar[WHITE] = 1;
    Move moves[MAX_MOVES];
    const uint8_t n = legalMoves(p, WHITE, dice(3, 5), moves, MAX_MOVES);
    CHECK(n > 0, "can enter");
    for (uint8_t i = 0; i < n; ++i) CHECK(moves[i].from == BAR, "bar first, got %u", moves[i].from);
    // White enters on 24 - roll: a 5 lands on index 19.
    CHECK(target(p, WHITE, BAR, 5) == 19, "entry point");

    // A closed board: Black holds all six entry points.
    Position c = empty();
    for (uint8_t i = 18; i < 24; ++i) c.pt[i] = -2;
    c.off[BLACK] = 3;
    c.bar[WHITE] = 1;
    c.pt[5] = 14;
    CHECK(maxUsable(c, WHITE, dice(6, 1)) == 0, "closed board, nothing to play");
    CHECK(legalMoves(c, WHITE, dice(6, 1), moves, MAX_MOVES) == 0, "no legal move");
}

static void testHigherDie() {
    // One White checker on index 10; index 0 closed. A 6 or a 4 alone is
    // playable, but not both (10-6-4 and 10-4-6 both land on index 0).
    Position p = empty();
    p.pt[10] = 1;
    p.off[WHITE] = 14;
    p.pt[0] = -2;
    p.off[BLACK] = 13;
    Move moves[MAX_MOVES];
    const uint8_t n = legalMoves(p, WHITE, dice(4, 6), moves, MAX_MOVES);
    CHECK(maxUsable(p, WHITE, dice(4, 6)) == 1, "only one die usable");
    CHECK(n == 1 && moves[0].die == 6 && moves[0].to == 4, "must play the higher: n=%u", n);

    // Both dice must be used when some order allows it: from 10 with 3 and 5,
    // index 7 closed. 3 first is blocked outright; 5 then 3 uses both.
    Position q = empty();
    q.pt[10] = 1;
    q.pt[20] = 14;   // the rest of White, which can also move -- the check below is general
    q.pt[7] = -2;
    q.off[BLACK] = 13;
    const uint8_t total = maxUsable(q, WHITE, dice(3, 5));
    CHECK(total == 2, "both usable");
    const uint8_t k = legalMoves(q, WHITE, dice(3, 5), moves, MAX_MOVES);
    for (uint8_t i = 0; i < k; ++i) {
        Position c = q;
        apply(c, WHITE, moves[i]);
        Dice r = dice(3, 5);
        useDie(r, moves[i].die);
        CHECK(maxUsable(c, WHITE, r) == 1, "every legal first move leaves the other die usable");
    }
}

static void testBearOff() {
    Position p = empty();
    p.pt[3] = 2;   // White 4-point
    p.pt[1] = 3;   // White 2-point
    p.off[WHITE] = 10;
    p.pt[20] = -15;
    CHECK(target(p, WHITE, 3, 4) == OFF, "exact");
    CHECK(target(p, WHITE, 3, 6) == OFF, "higher from the highest point");
    CHECK(target(p, WHITE, 1, 6) == NO_POINT, "higher, but something sits higher");
    CHECK(target(p, WHITE, 1, 2) == OFF, "exact from lower");
    CHECK(target(p, WHITE, 3, 1) == 2, "or move inside");
    // Using the smaller of two dice that both bear off.
    Move m;
    CHECK(findMove(p, WHITE, dice(5, 6), 3, OFF, m) && m.die == 5, "keeps the bigger die: %u", m.die);

    // Not all home: no bearing off.
    Position q = p;
    q.pt[3] = 1;
    q.pt[9] = 1;
    CHECK(target(q, WHITE, 1, 2) == NO_POINT, "a checker outside home");

    // Hit during bear-off: the hit checker goes to the bar and bearing off stops.
    Position h = empty();
    h.pt[2] = 1;    // White blot on the 3-point
    h.pt[4] = 2;
    h.off[WHITE] = 12;
    h.pt[1] = -2;   // Black on White's 2-point
    h.off[BLACK] = 13;
    Move hit;
    hit.from = 1;
    hit.to = 2;
    hit.die = 1;
    CHECK(apply(h, BLACK, hit), "a hit");
    CHECK(h.bar[WHITE] == 1 && h.pt[2] == -1, "hit to the bar");
    CHECK(target(h, WHITE, 4, 5) == NO_POINT, "on the bar: no bearing off");
    CHECK(target(h, WHITE, BAR, 3) == 21, "must enter");
}

static void testResults() {
    Position p = empty();
    p.off[WHITE] = 15;
    p.off[BLACK] = 2;
    p.pt[20] = -13;
    CHECK(result(p, WHITE) == Result::Single, "single");
    p.off[BLACK] = 0;
    p.pt[20] = -15;
    CHECK(result(p, WHITE) == Result::Gammon, "gammon");
    p.pt[20] = -14;
    p.pt[3] = -1;   // Black left in White's home board
    CHECK(result(p, WHITE) == Result::Backgammon, "backgammon, home board");
    p.pt[3] = 0;
    p.bar[BLACK] = 1;
    CHECK(result(p, WHITE) == Result::Backgammon, "backgammon, bar");
    CHECK(result(p, BLACK) == Result::None, "black has not won");
}

static void testDice() {
    long counts[7] = {0};
    for (uint32_t s = 0; s < 300; ++s)
        for (uint16_t i = 0; i < 2000; ++i) counts[die(mix(s + 1), i)]++;
    for (int f = 1; f <= 6; ++f) {
        const double q = counts[f] / 600000.0;
        CHECK(q > 0.160 && q < 0.173, "face %d %.4f", f, q);
    }
    uint16_t rolls = 0;
    uint8_t first = 9;
    Dice d;
    openingRoll(12345, rolls, first, d);
    CHECK(d.count == 2 && d.value[0] != d.value[1] && first <= BLACK, "opening is never a double");
    CHECK((first == WHITE) == (d.value[0] > d.value[1]), "higher die moves first");
    CHECK(tableSeed(7, "A4F2", "B1C3") == tableSeed(7, "B1C3", "A4F2"), "seed is symmetric");
    CHECK(tableSeed(7, "A4F2", "B1C3") != tableSeed(8, "A4F2", "B1C3"), "session matters");
}

// ---- brute force: every order, no canonical pruning -------------------------

static uint8_t bruteMax(const Position& p, uint8_t side, const Dice& d) {
    uint8_t best = 0;
    for (uint8_t i = 0; i < d.count; ++i) {
        if (i > 0 && d.value[i] == d.value[i - 1]) continue;
        for (uint8_t from = 0; from <= BAR; ++from) {
            const uint8_t t = target(p, side, from, d.value[i]);
            if (t == NO_POINT) continue;
            Position c = p;
            Move m;
            m.from = from;
            m.to = t;
            m.die = d.value[i];
            apply(c, side, m);
            Dice r = d;
            useDie(r, d.value[i]);
            const uint8_t n = static_cast<uint8_t>(1 + bruteMax(c, side, r));
            if (n > best) best = n;
        }
    }
    return best;
}

static void compareWithBrute(const Position& p, uint8_t side, const Dice& d) {
    const uint8_t total = bruteMax(p, side, d);
    CHECK(maxUsable(p, side, d) == total, "canonical max %u vs brute %u", maxUsable(p, side, d), total);
    // The brute-force legal set: first moves after which the rest reach the max,
    // then the higher-die rule.
    Move brute[MAX_MOVES];
    uint8_t nb = 0;
    if (total > 0) {
        for (uint8_t i = 0; i < d.count; ++i) {
            if (i > 0 && d.value[i] == d.value[i - 1]) continue;
            for (uint8_t from = 0; from <= BAR; ++from) {
                const uint8_t t = target(p, side, from, d.value[i]);
                if (t == NO_POINT) continue;
                Position c = p;
                Move m;
                m.from = from;
                m.to = t;
                m.die = d.value[i];
                apply(c, side, m);
                Dice r = d;
                useDie(r, d.value[i]);
                if (1 + bruteMax(c, side, r) == total && nb < MAX_MOVES) brute[nb++] = m;
            }
        }
        if (total == 1 && d.count == 2 && d.value[0] != d.value[1]) {
            const uint8_t hi = d.value[0] > d.value[1] ? d.value[0] : d.value[1];
            bool anyHi = false;
            for (uint8_t i = 0; i < nb; ++i) anyHi = anyHi || brute[i].die == hi;
            if (anyHi) {
                uint8_t k = 0;
                for (uint8_t i = 0; i < nb; ++i)
                    if (brute[i].die == hi) brute[k++] = brute[i];
                nb = k;
            }
        }
    }
    Move got[MAX_MOVES];
    const uint8_t ng = legalMoves(p, side, d, got, MAX_MOVES);
    CHECK(ng == nb, "legal move count %u vs brute %u", ng, nb);
    for (uint8_t i = 0; i < nb; ++i) {
        bool found = false;
        for (uint8_t j = 0; j < ng; ++j)
            found = found || (got[j].from == brute[i].from && got[j].to == brute[i].to &&
                              got[j].die == brute[i].die);
        CHECK(found, "brute move %u->%u (%u) missing", brute[i].from, brute[i].to, brute[i].die);
    }
}

// ---- whole games: the computer against itself --------------------------------

static void testGames() {
    long turns = 0, games = 0, compared = 0;
    int results[4] = {0, 0, 0, 0};
    for (uint32_t g = 0; g < 400; ++g) {
        const uint32_t seed = mix(g * 7919U + 1U);
        Position p;
        setup(p);
        uint16_t rolls = 0;
        uint8_t side = WHITE;
        Dice d;
        openingRoll(seed, rolls, side, d);
        int guard = 0;
        while (!won(p, WHITE) && !won(p, BLACK) && ++guard < 2000) {
            if (g % 8 == 0 && compared < 3000) {
                compareWithBrute(p, side, d);
                ++compared;
            }
            Plan plan;
            choosePlan(p, side, d, plan);
            CHECK(plan.count == maxUsable(p, side, d), "the computer uses as many dice as possible");
            for (uint8_t i = 0; i < plan.count; ++i) {
                Move m;
                const bool legal = findMove(p, side, d, plan.moves[i].from, plan.moves[i].to, m);
                CHECK(legal, "planned move %u->%u legal", plan.moves[i].from, plan.moves[i].to);
                if (!legal) break;
                apply(p, side, m);
                useDie(d, m.die);
            }
            Move rest[MAX_MOVES];
            CHECK(legalMoves(p, side, d, rest, MAX_MOVES) == 0, "nothing left to play after the plan");
            CHECK(checkerTotal(p, WHITE) == 15 && checkerTotal(p, BLACK) == 15, "fifteen each");
            if (won(p, side)) break;
            side = opponent(side);
            rollDice(seed, rolls, d);
            ++turns;
        }
        const uint8_t winner = won(p, WHITE) ? WHITE : BLACK;
        CHECK(won(p, winner), "game %u finished", g);
        results[static_cast<int>(result(p, winner))]++;
        ++games;
    }
    std::printf("  %ld games, %ld turns, %ld positions compared with brute force; "
                "single %d, gammon %d, backgammon %d\n",
                games, turns, compared, results[1], results[2], results[3]);
}

int main() {
    testSetup();
    testBar();
    testHigherDie();
    testBearOff();
    testResults();
    testDice();
    testGames();
    std::printf("%ld checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
