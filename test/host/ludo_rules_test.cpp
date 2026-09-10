// Host-side test of the Ludo rules engine, compiled against the real
// src/games/LudoRules.cpp. LudoRules is pure C++ with no Arduino in it -- that
// is a rule (see src/games/CLAUDE.md) and this file is what proves it still
// holds, since it will not compile otherwise.
//
// It is not run by CI and not picked up by pio test (the folder name does
// not start with test_). Build and run it with any host C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src/games test/host/ludo_rules_test.cpp src/games/LudoRules.cpp -o ludo_rules_test && ./ludo_rules_test
//
// What it checks: each rule on a hand-built position (yard needs a 6, exact
// roll home, capture and bonus, safe squares, blocks and their exceptions,
// three sixes, a six with no move), then 3,000 seeded games across every seat
// mix with the invariants tested after every move -- no two colours on an
// ordinary square, every illegal token refused with the state untouched, the
// computer never choosing an illegal move, every game finishing -- and last,
// that Normal clearly beats Easy.
#include "LudoRules.h"

#include <cstdio>
#include <cstring>

using namespace Ludo;

static int failures = 0;
static int checks = 0;
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

static State fresh(uint8_t mask, uint8_t first = 0) {
    State s;
    std::memset(&s, 0xAA, sizeof(s));   // garbage first: reset must set everything
    reset(s, mask, first);
    return s;
}

// A seed whose die at `index` is `want`.
static uint32_t seedFor(uint16_t index, uint8_t want) {
    for (uint32_t seed = 1;; ++seed) {
        if (die(seed, index) == want) return seed;
    }
}

static void testReset() {
    State s = fresh(0x05, 2);
    CHECK(s.turn == 2, "turn %u", s.turn);
    CHECK(!s.over, "over with two seats");
    CHECK(s.pending == 0 && s.sixes == 0 && s.rolls == 0 && s.finished == 0, "counters");
    for (int a = 0; a < 4; ++a)
        for (int t = 0; t < 4; ++t) CHECK(s.pos[a][t] == YARD, "pos");
    State one = fresh(0x01);
    CHECK(one.over, "one seat is not a game");
    State bad = fresh(0x0A, 0);   // first not playing -> lowest playing
    CHECK(bad.turn == 1, "fallback first %u", bad.turn);
}

static void testDie() {
    long counts[7] = {0};
    for (uint32_t seed = 0; seed < 200; ++seed)
        for (uint16_t i = 0; i < 3000; ++i) {
            const uint8_t d = die(seed * 2654435761U, i);
            CHECK(d >= 1 && d <= 6, "die %u", d);
            counts[d]++;
        }
    const double n = 200.0 * 3000.0;
    for (int f = 1; f <= 6; ++f) {
        const double p = counts[f] / n;
        CHECK(p > 0.160 && p < 0.173, "face %d freq %.4f", f, p);
    }
    CHECK(die(12345, 7) == die(12345, 7), "deterministic");
}

static void testYardNeedsSix() {
    State s = fresh(0x05);
    s.pending = 5;
    CHECK(movable(s) == 0, "5 from yard");
    s.pending = 6;
    CHECK(movable(s) == 0x0F, "6 from yard mask %x", movable(s));
    MoveInfo m;
    CHECK(move(s, 2, m), "move out");
    CHECK(s.pos[0][2] == 0 && m.from == YARD && m.to == 0, "on start");
    CHECK(m.bonus && s.turn == 0, "six gives bonus");
}

static void testExactHome() {
    State s = fresh(0x05);
    s.pos[0][0] = 54;
    CHECK(target(s, 0, 0, 3) == YARD, "overshoot home");
    CHECK(target(s, 0, 0, 2) == HOME, "exact home");
    s.pending = 2;
    MoveInfo m;
    CHECK(move(s, 0, m) && m.reachedHome && !m.bonus, "home, no bonus");
    CHECK(s.turn == 2, "turn passes to Yellow %u", s.turn);
    CHECK(target(s, 0, 0, 1) == YARD, "a home token never moves");
}

static void testCaptureAndSafe() {
    // Red at rel 10 (abs 10). Green at rel 49 -> abs (49+13)%52 = 10.
    State s = fresh(0x03);
    s.pos[0][0] = 5;
    s.pos[1][0] = 49;   // abs 10, not safe (10%13=10)
    s.pending = 5;
    MoveInfo m;
    CHECK(move(s, 0, m), "capture move");
    CHECK(m.capturedSeat == 1 && m.capturedToken == 0, "captured");
    CHECK(s.pos[1][0] == YARD, "sent home");
    CHECK(m.bonus && s.turn == 0, "capture bonus");

    // Safe star: abs 8. Green rel 47 -> abs 8.
    State t = fresh(0x03);
    t.pos[0][0] = 3;
    t.pos[1][0] = 47;
    t.pending = 5;
    CHECK(move(t, 0, m), "onto star");
    CHECK(m.capturedSeat == NO_SEAT && t.pos[1][0] == 47, "no capture on a star");
    CHECK(!m.bonus && t.turn == 1, "no bonus");

    // Start square abs 13 is Green's start: Red at rel 13 cannot capture there.
    State u = fresh(0x03);
    u.pos[0][0] = 9;
    u.pos[1][0] = 0;   // Green on its own start = abs 13
    u.pending = 4;
    CHECK(move(u, 0, m) && m.capturedSeat == NO_SEAT, "start squares are safe");
}

static void testBlocks() {
    // Green block at abs 11 (rel 50 for Green: (50+13)%52 = 11). 11%13 = 11, not safe.
    State s = fresh(0x03);
    s.pos[1][0] = 50;
    s.pos[1][1] = 50;
    s.pos[0][0] = 9;   // Red abs 9
    CHECK(target(s, 0, 0, 1) == 10, "short of the block");
    CHECK(target(s, 0, 0, 2) == YARD, "cannot land on block");
    CHECK(target(s, 0, 0, 5) == YARD, "cannot pass block");
    // Own tokens can pass their own block.
    State o = fresh(0x03);
    o.pos[0][1] = 11;
    o.pos[0][2] = 11;
    o.pos[0][0] = 9;
    CHECK(target(o, 0, 0, 5) == 14, "own block is passable");
    // Two Green on a safe square do not block: abs 21 (star) = Green rel 8.
    State q = fresh(0x03);
    q.pos[1][0] = 8;
    q.pos[1][1] = 8;
    q.pos[0][0] = 19;
    CHECK(target(q, 0, 0, 2) == 21, "may land beside a pair on a star");
    CHECK(target(q, 0, 0, 4) == 23, "may pass a pair on a star");
    // Yard exit never blocked, even with an opponent pair on our start square.
    State y = fresh(0x03);
    y.pos[1][0] = 39;   // Green rel 39 -> abs 0 (Red's start)
    y.pos[1][1] = 39;
    CHECK(target(y, 0, 0, 6) == 0, "start square cannot be walled off");
}

static void testThreeSixes() {
    State s = fresh(0x05);
    s.pos[0][0] = 10;
    s.sixes = 2;
    s.rolls = 5;
    const uint32_t seed = seedFor(5, 6);
    CHECK(roll(s, seed) == RollResult::Forfeit, "third six forfeits");
    CHECK(movable(s) == 0, "forfeited six is not playable");
    MoveInfo m;
    CHECK(!move(s, 0, m), "cannot move on a forfeit");
    skip(s);
    CHECK(s.turn == 2 && s.pending == 0 && s.sixes == 0, "turn passes, no bonus");
    CHECK(s.pos[0][0] == 10, "earlier moves stand");

    // A non-six resets the run.
    State r = fresh(0x05);
    r.sixes = 2;
    r.rolls = 0;
    const uint32_t seed2 = seedFor(0, 3);
    roll(r, seed2);
    CHECK(r.sixes == 0, "run broken by a 3");
}

static void testSixNoMoveStillRollsAgain() {
    // Red: no yard tokens, one token at 53 (needs 3), others home. Roll 6 -> no move.
    State s = fresh(0x05);
    s.pos[0][0] = 53;
    s.pos[0][1] = HOME;
    s.pos[0][2] = HOME;
    s.pos[0][3] = HOME;
    s.rolls = 0;
    const uint32_t seed = seedFor(0, 6);
    CHECK(roll(s, seed) == RollResult::NoMove, "six, no move");
    skip(s);
    CHECK(s.turn == 0, "still Red's turn after an unusable six");
}

static void testFinishAndPlaces() {
    State s = fresh(0x07);   // Red, Green, Yellow
    for (int t = 0; t < 4; ++t) s.pos[0][t] = HOME;
    s.pos[0][3] = 55;
    s.pending = 1;
    MoveInfo m;
    CHECK(move(s, 3, m) && m.seatFinished, "Red finishes");
    CHECK(s.place[0] == 1 && !s.over, "1st, game continues with two");
    CHECK(s.turn == 1, "turn to Green %u", s.turn);
    for (int t = 0; t < 4; ++t) s.pos[1][t] = HOME;
    s.pos[1][0] = 50;
    s.pending = 6;
    CHECK(move(s, 0, m) && m.seatFinished && s.over, "Green finishes, game over");
    CHECK(s.place[1] == 2 && s.place[2] == 3, "places %u %u", s.place[1], s.place[2]);
    CHECK(!m.bonus, "no bonus when over");
}

static bool invariants(const State& s, const char* where) {
    bool ok = true;
    // No two colours on one ordinary square.
    for (uint8_t a = 0; a < TRACK_LEN; ++a) {
        if (safeSquare(a)) continue;
        int colours = 0;
        for (uint8_t seat = 0; seat < SEATS; ++seat) {
            if (!playing(s, seat)) continue;
            for (uint8_t t = 0; t < TOKENS; ++t) {
                const uint8_t p = s.pos[seat][t];
                if (onTrack(p) && absolute(seat, p) == a) {
                    ++colours;
                    break;
                }
            }
        }
        if (colours > 1) {
            std::printf("  [%s] two colours on square %u\n", where, a);
            ok = false;
        }
    }
    for (uint8_t seat = 0; seat < SEATS; ++seat) {
        for (uint8_t t = 0; t < TOKENS; ++t) {
            const uint8_t p = s.pos[seat][t];
            if (p != YARD && p > HOME) {
                std::printf("  [%s] bad pos %u\n", where, p);
                ok = false;
            }
            if (!playing(s, seat) && p != YARD) {
                std::printf("  [%s] empty seat has a token out\n", where);
                ok = false;
            }
        }
        if (s.place[seat] != 0 && homeCount(s, seat) != TOKENS && !s.over) {
            std::printf("  [%s] placed with tokens out\n", where);
            ok = false;
        }
    }
    return ok;
}

// Play one whole game. levels[seat]: 0 Easy, 1 Normal, 2 random-legal.
static int playGame(uint32_t seed, uint8_t mask, const int levels[4], uint8_t first,
                    long& rollsOut) {
    State s = fresh(mask, first);
    int steps = 0;
    while (!s.over) {
        if (++steps > 20000) {
            std::printf("  game %u did not finish\n", seed);
            ++failures;
            return -1;
        }
        const uint8_t seat = s.turn;
        if (!playing(s, seat) || s.place[seat] != 0) {
            std::printf("  turn given to a seat that cannot play\n");
            ++failures;
            return -1;
        }
        const RollResult r = roll(s, seed);
        if (r != RollResult::Choose) {
            if (movable(s) != 0) {
                std::printf("  NoMove/Forfeit but movable\n");
                ++failures;
            }
            skip(s);
            continue;
        }
        const uint8_t mask4 = movable(s);
        uint8_t tok;
        if (levels[seat] == 2) {
            uint8_t legal[4], n = 0;
            for (uint8_t t = 0; t < 4; ++t)
                if (mask4 & (1u << t)) legal[n++] = t;
            tok = legal[mix(seed ^ s.rolls ^ 0x5151) % n];
        } else {
            tok = botChoose(s, levels[seat] ? Level::Normal : Level::Easy, seed);
        }
        ++checks;
        if (tok >= 4 || !(mask4 & (1u << tok))) {
            std::printf("  bot chose illegal token %u (mask %x)\n", tok, mask4);
            ++failures;
            return -1;
        }
        // Every token outside the mask must be refused, with nothing changed.
        for (uint8_t t = 0; t < 4; ++t) {
            if (mask4 & (1u << t)) continue;
            State copy = s;
            MoveInfo dummy;
            ++checks;
            if (move(copy, t, dummy) || std::memcmp(&copy, &s, sizeof(s)) != 0) {
                std::printf("  illegal move accepted or state changed\n");
                ++failures;
            }
        }
        MoveInfo m;
        if (!move(s, tok, m)) {
            std::printf("  legal move refused\n");
            ++failures;
            return -1;
        }
        ++checks;
        if (!invariants(s, "game")) ++failures;
    }
    rollsOut += s.rolls;
    for (uint8_t seat = 0; seat < 4; ++seat)
        if (playing(s, seat) && s.place[seat] == 1) return seat;
    return -1;
}

static void testManyGames() {
    long rolls = 0;
    int games = 0;
    const uint8_t masks[] = {0x05, 0x0F, 0x07, 0x0A, 0x03, 0x0E};
    for (uint32_t i = 0; i < 3000; ++i) {
        const uint32_t seed = mix(i + 1);
        const uint8_t mask = masks[i % 6];
        int levels[4] = {(int)(i % 3), (int)((i / 3) % 3), (int)((i / 9) % 3), (int)((i / 27) % 3)};
        playGame(seed, mask, levels, (uint8_t)(i % 4), rolls);
        ++games;
    }
    std::printf("  %d games, mean %.0f rolls per game\n", games, (double)rolls / games);
}

static void testNormalBeatsEasy() {
    long rolls = 0;
    int normalWins = 0, n = 2000;
    for (int i = 0; i < n; ++i) {
        const uint32_t seed = mix(0xABCD0000u + i);
        // Swap which seat is Normal each game, and who starts.
        const bool normalRed = (i & 1) == 0;
        int levels[4] = {normalRed ? 1 : 0, 0, normalRed ? 0 : 1, 0};
        const int w = playGame(seed, 0x05, levels, (uint8_t)((i & 2) ? 2 : 0), rolls);
        if ((w == 0 && normalRed) || (w == 2 && !normalRed)) ++normalWins;
    }
    const double rate = (double)normalWins / n;
    std::printf("  Normal beat Easy in %.1f%% of %d games\n", rate * 100, n);
    CHECK(rate > 0.60, "Normal should clearly beat Easy: %.3f", rate);

    int easyVsRandom = 0;
    for (int i = 0; i < n; ++i) {
        const uint32_t seed = mix(0x0E0E0000u + i);
        const bool easyRed = (i & 1) == 0;
        int levels[4] = {easyRed ? 0 : 2, 0, easyRed ? 2 : 0, 0};
        const int w = playGame(seed, 0x05, levels, (uint8_t)((i & 2) ? 2 : 0), rolls);
        if ((w == 0 && easyRed) || (w == 2 && !easyRed)) ++easyVsRandom;
    }
    std::printf("  Easy beat random-legal in %.1f%% of %d games\n", 100.0 * easyVsRandom / n, n);
}

static void testBotDeterministic() {
    State s = fresh(0x05);
    s.pos[0][0] = 4;
    s.pos[0][1] = 20;
    s.pending = 6;
    const uint8_t a = botChoose(s, Level::Normal, 99);
    const uint8_t b = botChoose(s, Level::Normal, 99);
    CHECK(a == b, "same input same choice");
    const uint8_t e = botChoose(s, Level::Easy, 99);
    CHECK(s.pos[0][e] == YARD, "Easy brings a token out on a 6 (%u)", e);
    // Normal takes a capture when one is there.
    State c = fresh(0x05);
    c.pos[0][0] = 4;
    c.pos[0][1] = 30;
    c.pos[2][0] = 9;    // Yellow rel 9 -> abs 35; Red rel 35 -> abs 35
    c.pending = 5;
    CHECK(botChoose(c, Level::Normal, 7) == 1, "Normal captures");
}

int main() {
    testReset();
    testDie();
    testYardNeedsSix();
    testExactHome();
    testCaptureAndSafe();
    testBlocks();
    testThreeSixes();
    testSixNoMoveStillRollsAgain();
    testFinishAndPlaces();
    testBotDeterministic();
    testManyGames();
    testNormalBeatsEasy();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

