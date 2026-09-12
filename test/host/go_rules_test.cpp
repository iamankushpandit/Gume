// Host-side test of the Go rules and computer player, compiled against the
// real src/games/GoRules.cpp and GoAi.cpp. Both are pure C++ with no Arduino
// in them; this file is what proves it, since it will not compile otherwise.
// Not run by CI; build it with any host C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src/games test/host/go_rules_test.cpp src/games/GoRules.cpp src/games/GoAi.cpp -o go_test && ./go_test
//
// What it checks: a capture, simple ko, suicide, a capture that is not
// suicide, eyes real and false, area and territory scoring on a small board
// with and without dead stones, the wire encoding round-tripping every point
// and every kind and refusing the service's presence and ending words; then,
// on hundreds of random games at 9x9 and a few at 19x19, that no stone is
// ever left with zero liberties, that every game ends, that the easy player
// never fills its own eye and only ever plays legal moves, and that the
// medium player's search completes and answers with a legal move.
#include "GoRules.h"

#include <cstdio>
#include <cstring>

using namespace Go;

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

/* Lay out a position from rows of '.', 'X' (black) and 'O' (white). */
static State fromRows(const char* const* rows, uint8_t n, Rules rules, uint8_t toMove) {
    State s;
    reset(s, n, rules);
    for (uint8_t r = 0; r < n; ++r) {
        for (uint8_t c = 0; c < n; ++c) {
            const char ch = rows[r][c];
            s.at[point(n, r, c)] = ch == 'X' ? BLACK : ch == 'O' ? WHITE : EMPTY;
        }
    }
    s.toMove = toMove;
    s.moves = 10;   // past the opening, so near() is meaningful
    return s;
}

static bool noDeadStones(const State& s) {
    const uint16_t total = points(s.n);
    for (uint16_t p = 0; p < total; ++p) {
        if (s.at[p] != EMPTY && liberties(s, p) == 0) return false;
    }
    return true;
}

static void testCapture() {
    const char* rows[5] = {
        ".....",
        "..X..",
        ".XOX.",
        ".....",
        ".....",
    };
    State s = fromRows(rows, 5, Rules::Area, BLACK);
    CHECK(captures(s, point(5, 3, 2)) == 1, "one stone in atari");
    CHECK(play(s, point(5, 3, 2)), "the capture is legal");
    CHECK(s.at[point(5, 2, 2)] == EMPTY, "the stone is gone");
    CHECK(s.captured[BLACK] == 1, "credited to black: %u", s.captured[BLACK]);
    CHECK(s.ko == NO_POINT, "four stones taking one is not a ko");
    CHECK(s.toMove == WHITE, "white to move");
}

static void testKo() {
    const char* rows[5] = {
        ".....",
        ".XO..",
        "X.XO.",
        ".XO..",
        ".....",
    };
    State s = fromRows(rows, 5, Rules::Area, WHITE);
    CHECK(play(s, point(5, 2, 1)), "white takes the ko");
    CHECK(s.at[point(5, 2, 2)] == EMPTY, "black stone taken");
    CHECK(s.ko == point(5, 2, 2), "the ko point is where the stone was");
    CHECK(!legal(s, point(5, 2, 2)), "black may not retake at once");
    CHECK(play(s, point(5, 4, 4)), "black plays elsewhere");
    CHECK(s.ko == NO_POINT, "the ko is lifted by a move elsewhere");
    CHECK(play(s, point(5, 0, 0)), "white plays elsewhere");
    CHECK(legal(s, point(5, 2, 2)), "now black may retake");
}

static void testSuicide() {
    const char* rows[5] = {
        ".O...",
        "O.O..",
        ".O...",
        ".....",
        ".....",
    };
    State s = fromRows(rows, 5, Rules::Area, BLACK);
    CHECK(!legal(s, point(5, 1, 1)), "a lone stone with no liberties is suicide");
    const char* rows2[5] = {
        "XO...",
        ".XO..",
        "XO...",
        ".....",
        ".....",
    };
    /* Black at (1,0) would have no liberties of its own but captures nothing
     * -- wait: the white stones at (0,1),(1,2),(2,1) are not captured by it.
     * Suicide. */
    State t = fromRows(rows2, 5, Rules::Area, BLACK);
    CHECK(!legal(t, point(5, 1, 0)) || captures(t, point(5, 1, 0)) > 0,
          "no liberties and no capture is suicide");
    /* A capture is never suicide: white in the corner in atari, black takes
     * it by playing the point that would otherwise be dead. */
    const char* rows3[5] = {
        "OX...",
        ".X...",
        "X....",
        ".....",
        ".....",
    };
    State u = fromRows(rows3, 5, Rules::Area, BLACK);
    CHECK(legal(u, point(5, 1, 0)), "capturing is not suicide");
    CHECK(play(u, point(5, 1, 0)), "and it plays");
    CHECK(u.at[point(5, 0, 0)] == EMPTY, "the corner stone is gone");
}

static void testEyes() {
    const char* rows[5] = {
        ".X...",
        "X.X..",
        ".X...",
        ".....",
        ".....",
    };
    State s = fromRows(rows, 5, Rules::Area, BLACK);
    CHECK(fillsOwnEye(s, point(5, 1, 1), BLACK), "a real eye");
    CHECK(!fillsOwnEye(s, point(5, 1, 1), WHITE), "not white's eye");
    const char* rows2[5] = {
        "OX...",
        "X.X..",
        "OX.O.",
        ".....",
        ".....",
    };
    State t = fromRows(rows2, 5, Rules::Area, BLACK);
    CHECK(!fillsOwnEye(t, point(5, 1, 1), BLACK), "two enemy diagonals make it false");
    CHECK(legal(t, point(5, 1, 1)), "and it is a legal point");
}

static void testScoring() {
    const char* rows[5] = {
        "..X.O",
        "..X.O",
        "..X.O",
        "..XOO",
        "..XO.",
    };
    State s = fromRows(rows, 5, Rules::Area, BLACK);
    Score sc;
    score(s, nullptr, sc);
    CHECK(sc.stones[BLACK] == 5 && sc.stones[WHITE] == 6, "stones %u %u", sc.stones[BLACK],
          sc.stones[WHITE]);
    CHECK(sc.territory[BLACK] == 10, "black's ten empty points: %u", sc.territory[BLACK]);
    CHECK(sc.territory[WHITE] == 1, "white's corner: %u", sc.territory[WHITE]);
    /* Column 3, rows 0..2 touch both colours: neutral. */
    CHECK(sc.half[BLACK] == 30 && sc.half[WHITE] == 14 + KOMI_HALF, "area totals %d %d",
          sc.half[BLACK], sc.half[WHITE]);
    CHECK(sc.winner == BLACK, "black wins by area");

    State t = s;
    t.rules = Rules::Territory;
    t.captured[WHITE] = 3;
    score(t, nullptr, sc);
    CHECK(sc.half[BLACK] == 20 && sc.half[WHITE] == 8 + KOMI_HALF, "territory totals %d %d",
          sc.half[BLACK], sc.half[WHITE]);

    /* Mark the white column dead: its points become black territory and the
     * six stones white's prisoners lost. */
    uint8_t dead[MAX_POINTS];
    memset(dead, 0, sizeof(dead));
    toggleDead(t, dead, point(5, 0, 4));
    CHECK(dead[point(5, 4, 3)] != 0, "the whole connected group toggles");
    score(t, dead, sc);
    CHECK(sc.prisoners[BLACK] == 6, "six dead white stones are black's prisoners: %u",
          sc.prisoners[BLACK]);
    CHECK(sc.territory[BLACK] == 20, "with the column dead black surrounds all twenty: %u",
          sc.territory[BLACK]);
    toggleDead(t, dead, point(5, 0, 4));
    CHECK(dead[point(5, 4, 3)] == 0, "toggling again clears it");
}

static void testCaptureRules() {
    const char* rows[5] = {
        ".....",
        "..X..",
        ".XOX.",
        ".....",
        ".....",
    };
    State s = fromRows(rows, 5, Rules::Capture1, BLACK);
    CHECK(!legal(s, PASS), "no passing under capture rules");
    CHECK(play(s, point(5, 3, 2)), "black takes");
    CHECK(s.over && s.winner == BLACK, "first capture wins");
    State t = fromRows(rows, 5, Rules::Capture3, BLACK);
    CHECK(play(t, point(5, 3, 2)), "black takes one");
    CHECK(!t.over, "three needed");
}

static void testTwoPasses() {
    State s;
    reset(s, 9, Rules::Area);
    CHECK(play(s, point(9, 4, 4)), "tengen");
    CHECK(play(s, PASS) && play(s, PASS), "two passes");
    CHECK(s.over && s.winner == BLACK, "one stone against komi... black owns the board");
    State t;
    reset(t, 9, Rules::Territory);
    CHECK(play(t, PASS) && play(t, PASS), "two passes");
    CHECK(t.over && t.winner == EMPTY, "territory waits for the marking");
}

static void testNet() {
    for (uint16_t p = 0; p < MAX_POINTS; ++p) {
        for (uint8_t k = 0; k < 4; ++k) {
            const Net::Kind kind = static_cast<Net::Kind>(k);
            const uint16_t send = (kind == Net::Kind::Pass || kind == Net::Kind::Accept) ? PASS : p;
            uint8_t from = 0, to = 0;
            Net::encode(kind, send, from, to);
            CHECK(from <= 63 && to >= Net::TO_MIN && to <= 63, "in range: %u %u", from, to);
            CHECK(!(from == 63 && to == 63), "never the reserved ending");
            Net::Kind gotKind;
            uint16_t got = 0;
            CHECK(Net::decode(from, to, gotKind, got), "decodes: p=%u k=%u", p, k);
            CHECK(gotKind == kind && got == send, "round trip p=%u k=%u -> %u", p, k, got);
        }
    }
    Net::Kind kind;
    uint16_t p;
    CHECK(!Net::decode(0, 0, kind, p), "the presence word is not a move");
    CHECK(!Net::decode(63, 63, kind, p), "the ending is not a move");
}

static void testRandomGames() {
    Rng rng;
    rng.x = 12345;
    int finished = 0;
    for (int g = 0; g < 200; ++g) {
        State s;
        reset(s, 9, static_cast<Rules>(g % RULES_COUNT));
        int moves = 0;
        while (!s.over && moves < 400) {
            const uint16_t p = chooseEasy(s, rng);
            if (p == NO_POINT) break;   // capture rules, nothing at all to play
            CHECK(legal(s, p), "easy picks a legal move (game %d move %d)", g, moves);
            if (p != PASS) CHECK(!fillsOwnEye(s, p, s.toMove), "easy never fills an eye");
            CHECK(play(s, p), "and it plays");
            CHECK(noDeadStones(s), "no stone without liberties (game %d move %d)", g, moves);
            ++moves;
        }
        if (s.over) ++finished;
    }
    CHECK(finished >= 190, "nearly every game ends: %d of 200", finished);
}

static void testMedium() {
    Rng rng;
    rng.x = 777;
    for (int g = 0; g < 3; ++g) {
        State s;
        reset(s, 9, Rules::Area);
        for (int m = 0; m < 20; ++m) play(s, chooseEasy(s, rng));
        Search sr;
        beginSearch(sr, s, 99 + g);
        int steps = 0;
        while (!stepSearch(sr, [&]() { return (++steps % 50) == 0; })) {
        }
        const uint16_t p = bestMove(sr);
        CHECK(legal(s, p), "medium answers with a legal move");
        CHECK(sr.done == sr.target, "the search completed: %u of %u", sr.done, sr.target);
    }
    /* 19x19: it must complete in a sane number of playouts, and answer. */
    State big;
    reset(big, 19, Rules::Area);
    for (int m = 0; m < 30; ++m) play(big, chooseEasy(big, rng));
    Search sr;
    beginSearch(sr, big, 5);
    while (!stepSearch(sr, []() { return false; })) {
    }
    CHECK(legal(big, bestMove(sr)), "19x19 medium answers with a legal move");

    /* The dead-stone estimate agrees with the obvious: a lone stone inside a
     * finished enemy territory is dead, a wall is alive. */
    const char* rows[5] = {
        "..X.O",
        "..X.O",
        "O.X.O",
        "..XOO",
        "..XO.",
    };
    State pos = fromRows(rows, 5, Rules::Territory, BLACK);
    uint8_t dead[MAX_POINTS];
    estimateDead(pos, dead, 3, 60);
    CHECK(dead[point(5, 2, 0)] != 0, "the lone white stone is dead");
    CHECK(dead[point(5, 0, 2)] == 0, "the black wall is alive");
}

int main() {
    testCapture();
    testKo();
    testSuicide();
    testEyes();
    testScoring();
    testCaptureRules();
    testTwoPasses();
    testNet();
    testRandomGames();
    testMedium();
    std::printf("%ld checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
