// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of the chess rules and computer player, compiled against the
// real src/games/ChessRules.cpp and ChessAi.cpp. Both are pure C++ with no
// Arduino in them; this file is what proves it, since it will not compile
// otherwise -- and that mattered here more than usual, because ChessRules.cpp
// carried a comment claiming purity for a long time while including
// ChessGame.h, and therefore Game.h, Ui.h and TFT_eSPI. The bodies were
// honest; the translation unit was not; nothing could tell until something
// tried to build it on a host. This file is that something.
//
// Not run by CI; build it with any host C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src/games test/host/chess_rules_test.cpp src/games/ChessRules.cpp src/games/ChessAi.cpp -o chess_test && ./chess_test
//
// What it checks: the opening position, a pawn's single and double push and
// its captures, en passant including the fact that the right expires, castling
// both sides and every way it can be refused, promotion, that a pinned piece
// cannot move, mate and stalemate told apart, the four dead positions, and the
// fifty-move counter; then that evaluate() agrees about who is winning, that
// both levels only ever answer with a legal move, that a search seeded the
// same way twice gives the same move, that Medium's stepped search completes,
// and that Medium beats Easy over a run of games.
#include "ChessRules.h"

#include <cstdio>
#include <cstring>

using namespace Ch;

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

/* An empty board with nothing allowed and nobody to move but White. Castling
 * rights are off by default here so a test has to ask for them, which is the
 * safer default in a file full of hand-built positions. */
static Position bare() {
    Position p{};
    std::memset(p.sq, EMPTY, sizeof(p.sq));
    p.whiteToMove = true;
    for (bool& c : p.castle) c = false;
    p.epSquare = NO_SQ;
    p.halfmove = 0;
    return p;
}

/* Lay a position out from eight rows written the way a board is printed --
 * rank 8 first -- with uppercase for White and lowercase for Black. */
static Position fromRows(const char* const rows[8], bool whiteToMove) {
    Position p = bare();
    for (int8_t r = 0; r < 8; ++r) {
        const char* row = rows[7 - r];      // rows[0] is rank 8
        for (int8_t f = 0; f < 8; ++f) {
            int8_t piece = EMPTY;
            switch (row[f]) {
                case 'P': piece = PAWN; break;
                case 'N': piece = KNIGHT; break;
                case 'B': piece = BISHOP; break;
                case 'R': piece = ROOK; break;
                case 'Q': piece = QUEEN; break;
                case 'K': piece = KING; break;
                case 'p': piece = -PAWN; break;
                case 'n': piece = -KNIGHT; break;
                case 'b': piece = -BISHOP; break;
                case 'r': piece = -ROOK; break;
                case 'q': piece = -QUEEN; break;
                case 'k': piece = -KING; break;
                default: break;
            }
            p.sq[idx(f, r)] = piece;
        }
    }
    p.whiteToMove = whiteToMove;
    return p;
}

static Position opening() {
    static const char* const rows[8] = {
        "rnbqkbnr",
        "pppppppp",
        "........",
        "........",
        "........",
        "........",
        "PPPPPPPP",
        "RNBQKBNR",
    };
    Position p = fromRows(rows, true);
    for (bool& c : p.castle) c = true;
    return p;
}

static bool canMove(const Position& p, uint8_t from, uint8_t to) {
    uint8_t out[MAX_MOVES];
    const uint8_t n = legalMoves(p, from, out);
    for (uint8_t i = 0; i < n; ++i) {
        if (out[i] == to) return true;
    }
    return false;
}

static uint8_t moveCount(const Position& p, uint8_t from) {
    uint8_t out[MAX_MOVES];
    return legalMoves(p, from, out);
}

static void testOpening() {
    const Position p = opening();
    CHECK(p.sq[idx(4, 0)] == KING, "white king on e1");
    CHECK(p.sq[idx(4, 7)] == -KING, "black king on e8");
    /* Twenty moves in the opening position: sixteen pawn moves and four
     * knight moves. The number is the classic one and is worth asserting
     * exactly, because almost any generator bug changes it. */
    Move all[MAX_ROOT_MOVES];
    CHECK(allMoves(p, all, MAX_ROOT_MOVES) == 20, "20 opening moves");
    CHECK(moveCount(p, idx(4, 1)) == 2, "e2 pawn has two moves");
    CHECK(moveCount(p, idx(1, 0)) == 2, "b1 knight has two moves");
    CHECK(!hasAnyLegalMove(p) == false, "the opening is not stalemate");
    CHECK(!deadPosition(p), "the opening is not dead");
}

static void testPawns() {
    Position p = bare();
    p.sq[idx(4, 1)] = PAWN;
    p.sq[idx(4, 0)] = KING;
    p.sq[idx(0, 7)] = -KING;
    CHECK(canMove(p, idx(4, 1), idx(4, 2)), "single push");
    CHECK(canMove(p, idx(4, 1), idx(4, 3)), "double push from the second rank");

    /* Blocked: a pawn captures diagonally and only diagonally, so a piece
     * directly in front stops both pushes rather than being taken. */
    p.sq[idx(4, 2)] = -PAWN;
    CHECK(!canMove(p, idx(4, 1), idx(4, 2)), "a pawn does not take forwards");
    CHECK(!canMove(p, idx(4, 1), idx(4, 3)), "a blocked pawn cannot double push");

    p.sq[idx(4, 2)] = EMPTY;
    p.sq[idx(3, 2)] = -PAWN;
    CHECK(canMove(p, idx(4, 1), idx(3, 2)), "diagonal capture");
}

static void testEnPassant() {
    Position p = bare();
    p.sq[idx(4, 4)] = PAWN;         // white pawn on e5
    p.sq[idx(4, 0)] = KING;
    p.sq[idx(0, 7)] = -KING;
    p.sq[idx(3, 6)] = -PAWN;        // black pawn on d7
    p.whiteToMove = false;
    applyMove(p, idx(3, 6), idx(3, 4));      // d7-d5, the double push
    CHECK(p.epSquare == idx(3, 5), "the double push sets the ep square");
    CHECK(canMove(p, idx(4, 4), idx(3, 5)), "en passant is available");

    Position q = p;
    const int8_t taken = applyMove(q, idx(4, 4), idx(3, 5));
    CHECK(taken == -PAWN, "en passant reports the pawn it took");
    CHECK(q.sq[idx(3, 4)] == EMPTY, "the captured pawn is removed from d5");

    /* The right lasts exactly one move. This is the case a naive flag on the
     * pawn gets wrong, which is why epSquare is part of the position. */
    Position r = p;
    applyMove(r, idx(0, 7), idx(0, 6));      // black king wanders
    CHECK(r.epSquare == NO_SQ, "the ep right expires after one move");
    CHECK(!canMove(r, idx(4, 4), idx(3, 5)), "en passant is gone");
}

static void testCastling() {
    static const char* const rows[8] = {
        "r...k..r",
        "........",
        "........",
        "........",
        "........",
        "........",
        "........",
        "R...K..R",
    };
    Position p = fromRows(rows, true);
    for (bool& c : p.castle) c = true;
    CHECK(canMove(p, idx(4, 0), idx(6, 0)), "white may castle short");
    CHECK(canMove(p, idx(4, 0), idx(2, 0)), "white may castle long");

    Position q = p;
    applyMove(q, idx(4, 0), idx(6, 0));
    CHECK(q.sq[idx(6, 0)] == KING && q.sq[idx(5, 0)] == ROOK,
          "castling moves the rook too");
    CHECK(!q.castle[0] && !q.castle[1], "castling spends both white rights");

    /* Blocked, attacked, and through check -- the three refusals. */
    Position blocked = p;
    blocked.sq[idx(5, 0)] = BISHOP;
    CHECK(!canMove(blocked, idx(4, 0), idx(6, 0)), "cannot castle through a piece");

    Position attacked_ = p;
    attacked_.sq[idx(5, 5)] = -ROOK;       // bears down the f-file
    CHECK(!canMove(attacked_, idx(4, 0), idx(6, 0)),
          "cannot castle through an attacked square");

    Position inCheck = p;
    inCheck.sq[idx(4, 5)] = -ROOK;         // bears down the e-file
    CHECK(!canMove(inCheck, idx(4, 0), idx(6, 0)), "cannot castle out of check");

    Position lost = p;
    lost.castle[0] = false;
    CHECK(!canMove(lost, idx(4, 0), idx(6, 0)), "cannot castle without the right");
}

static void testPromotion() {
    Position p = bare();
    p.sq[idx(0, 6)] = PAWN;
    p.sq[idx(4, 0)] = KING;
    p.sq[idx(7, 7)] = -KING;
    applyMove(p, idx(0, 6), idx(0, 7));
    CHECK(p.sq[idx(0, 7)] == QUEEN, "a pawn promotes to a queen");
}

static void testPins() {
    Position p = bare();
    p.sq[idx(4, 0)] = KING;       // e1
    p.sq[idx(4, 1)] = BISHOP;     // e2, pinned
    p.sq[idx(4, 7)] = -ROOK;      // e8
    p.sq[idx(0, 7)] = -KING;
    CHECK(moveCount(p, idx(4, 1)) == 0, "a pinned bishop cannot move");
}

static void testMateAndStalemate() {
    /* Back-rank mate: the rook gives check and the king's own pawns hem it. */
    static const char* const mate[8] = {
        "......k.",
        ".....ppp",
        "........",
        "........",
        "........",
        "........",
        "........",
        "R.....K.",
    };
    Position m = fromRows(mate, false);
    m.sq[idx(0, 7)] = ROOK;       // white rook lands on a8, giving check
    m.sq[idx(0, 0)] = EMPTY;
    const uint8_t k = kingSquare(m, false);
    CHECK(attacked(m, k, true), "the black king is in check");
    CHECK(!hasAnyLegalMove(m), "and has no move: mate");

    /* Stalemate: not in check, and nothing legal. */
    static const char* const stale[8] = {
        "k.......",
        "........",
        ".Q......",
        "........",
        "........",
        "........",
        "........",
        ".......K",
    };
    Position s = fromRows(stale, false);
    s.sq[idx(1, 5)] = EMPTY;
    s.sq[idx(2, 6)] = QUEEN;      // c7: covers a8's flight squares, no check
    const uint8_t sk = kingSquare(s, false);
    CHECK(!attacked(s, sk, true), "the black king is not in check");
    CHECK(!hasAnyLegalMove(s), "and has no move: stalemate");
}

static void testDeadPositions() {
    Position kk = bare();
    kk.sq[idx(0, 0)] = KING;
    kk.sq[idx(7, 7)] = -KING;
    CHECK(deadPosition(kk), "king against king is dead");

    Position kn = kk;
    kn.sq[idx(1, 0)] = KNIGHT;
    CHECK(deadPosition(kn), "king and knight is dead");

    Position kb = kk;
    kb.sq[idx(2, 0)] = BISHOP;
    CHECK(deadPosition(kb), "king and bishop is dead");

    Position kr = kk;
    kr.sq[idx(1, 0)] = ROOK;
    CHECK(!deadPosition(kr), "king and rook is not dead");

    Position kp = kk;
    kp.sq[idx(1, 1)] = PAWN;
    CHECK(!deadPosition(kp), "a pawn is never dead material");
}

static void testHalfmoveClock() {
    Position p = opening();
    applyMove(p, idx(1, 0), idx(2, 2));      // Nb1-c3, a quiet move
    CHECK(p.halfmove == 1, "a piece move advances the clock");
    applyMove(p, idx(4, 6), idx(4, 4));      // ...e7-e5, a pawn move
    CHECK(p.halfmove == 0, "a pawn move resets it");
}

static void testEvaluate() {
    Position even = opening();
    CHECK(evaluate(even) == 0, "the opening is level: %d", evaluate(even));

    Position up = opening();
    up.sq[idx(3, 7)] = EMPTY;                // black loses its queen
    CHECK(evaluate(up) > 500, "a queen up is a big plus for White: %d",
          evaluate(up));
    up.whiteToMove = false;
    CHECK(evaluate(up) < -500, "and the same position is bad for Black: %d",
          evaluate(up));
}

/* Play a whole game between two levels and report who won: +1 White, -1 Black,
 * 0 drawn or unfinished. Both sides are driven through the same public entry
 * points the firmware uses. */
static int playGame(Level white, Level black, uint32_t seed) {
    Position p = opening();
    Rng rng;
    rng.s = seed | 1u;
    for (int ply = 0; ply < 300; ++ply) {
        if (deadPosition(p) || p.halfmove >= 100) return 0;
        if (!hasAnyLegalMove(p)) {
            const uint8_t k = kingSquare(p, p.whiteToMove);
            if (k != NO_SQ && attacked(p, k, !p.whiteToMove)) {
                return p.whiteToMove ? -1 : 1;      // the side to move is mated
            }
            return 0;
        }
        const Level level = p.whiteToMove ? white : black;
        Move m;
        if (level == Level::Easy) {
            m = chooseEasy(p, rng);
        } else {
            Search s;
            beginSearch(s, p, rng.next());
            int steps = 0;
            while (!stepSearch(s, [&]() { return (++steps % 4) == 0; })) {
            }
            CHECK(s.done == s.count, "the search completed: %u of %u", s.done,
                  s.count);
            m = bestMove(s);
        }
        CHECK(m.from != NO_SQ && canMove(p, m.from, m.to),
              "the computer answered with a legal move");
        if (m.from == NO_SQ || !canMove(p, m.from, m.to)) return 0;
        applyMove(p, m.from, m.to);
    }
    return 0;
}

static void testLevelsPlayLegally() {
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        playGame(Level::Easy, Level::Easy, seed);
    }
    /* Determinism: the same seed must give the same move, or a reported game
     * cannot be replayed and the engine cannot be debugged from a photograph
     * of a board. */
    const Position p = opening();
    Search a;
    Search b;
    beginSearch(a, p, 99);
    beginSearch(b, p, 99);
    while (!stepSearch(a, []() { return false; })) {
    }
    while (!stepSearch(b, []() { return false; })) {
    }
    const Move ma = bestMove(a);
    const Move mb = bestMove(b);
    CHECK(ma.from == mb.from && ma.to == mb.to, "the same seed plays the same move");

    Rng r1;
    Rng r2;
    r1.s = 7;
    r2.s = 7;
    const Move e1 = chooseEasy(p, r1);
    const Move e2 = chooseEasy(p, r2);
    CHECK(e1.from == e2.from && e1.to == e2.to, "easy is deterministic too");
}

static void testMediumBeatsEasy() {
    /* The whole point of there being two levels. Medium plays White in half of
     * them and Black in the other half, so the result is not just an opening
     * advantage being counted twice. */
    int mediumScore = 0;
    const int games = 8;
    for (int i = 0; i < games; ++i) {
        const uint32_t seed = static_cast<uint32_t>(100 + i);
        if (i % 2 == 0) {
            mediumScore += playGame(Level::Medium, Level::Easy, seed);
        } else {
            mediumScore -= playGame(Level::Easy, Level::Medium, seed);
        }
    }
    std::printf("  Medium scored %+d over %d games against Easy\n", mediumScore,
                games);
    CHECK(mediumScore > 0, "Medium should come out ahead of Easy: %+d",
          mediumScore);
}

int main() {
    testOpening();
    testPawns();
    testEnPassant();
    testCastling();
    testPromotion();
    testPins();
    testMateAndStalemate();
    testDeadPositions();
    testHalfmoveClock();
    testEvaluate();
    testLevelsPlayLegally();
    testMediumBeatsEasy();
    std::printf("%ld checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
