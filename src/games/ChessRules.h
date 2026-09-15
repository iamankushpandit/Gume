// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>

/* The rules of chess, and the computer that plays by them. No Arduino, no
 * screen, no Board -- this header and ChessRules.cpp / ChessAi.cpp include
 * nothing but <stdint.h> and <string.h>, exactly like GoRules and
 * BackgammonRules, and for the same two reasons.
 *
 * The first is that a search can then be tuned off the device. An engine is
 * the one thing here whose quality is a question of numbers -- piece values,
 * depth, how often Medium actually beats Easy -- and answering that by
 * flashing a board and playing games by hand is not answering it.
 *
 * The second is that test/host/chess_rules_test.cpp proves the purity by
 * existing: it is compiled with a host g++ against these two files alone, so
 * if an Arduino header ever leaks in, the test stops compiling. That is the
 * whole mechanism, and it is why the test is worth more than the assertions in
 * it. A comment claiming purity is not a mechanism -- ChessRules.cpp carried
 * exactly such a comment for a long time while including ChessGame.h, and
 * therefore Game.h, Ui.h and TFT_eSPI.
 *
 *   g++ -std=c++17 -O2 -Wall -Wextra -I src/games test/host/chess_rules_test.cpp \
 *       src/games/ChessRules.cpp src/games/ChessAi.cpp -o chess_test && ./chess_test
 */
namespace Ch {

/* Piece codes. Sign carries colour, magnitude carries kind, so an empty square
 * is 0 and `-p` is the same piece in the other colour. That makes "is this
 * mine" a sign test and "what is it" an abs(), which is most of what move
 * generation asks. */
constexpr int8_t EMPTY = 0;
constexpr int8_t PAWN = 1;
constexpr int8_t KNIGHT = 2;
constexpr int8_t BISHOP = 3;
constexpr int8_t ROOK = 4;
constexpr int8_t QUEEN = 5;
constexpr int8_t KING = 6;

/** Not a square. */
constexpr uint8_t NO_SQ = 0xFF;

/** Longest legal move list from one square: a queen on an open board. */
constexpr uint8_t MAX_MOVES = 28;

/* Named idx() and not the obvious sq(): Arduino already defines sq(x) as "x
 * squared", so a two-argument call to it is a preprocessor error rather than a
 * shadowing warning, and the message names a macro you never wrote. The
 * Position member stays .sq -- member access never reaches the preprocessor.
 * Nothing here includes Arduino any more, but everything that includes this
 * does, so the hazard is unchanged. */
constexpr uint8_t idx(int8_t file, int8_t rank) {
    return static_cast<uint8_t>(rank * 8 + file);
}
constexpr int8_t fileOf(uint8_t s) { return static_cast<int8_t>(s % 8); }
constexpr int8_t rankOf(uint8_t s) { return static_cast<int8_t>(s / 8); }

/** The back rank, used to set up and to spell a promotion. */
constexpr int8_t BACK_RANK[8] = {
    ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK,
};

/* One position. Small enough to copy for the legality filter, which is why
 * make/unmake is a struct assignment here rather than an undo stack. Seventy
 * bytes or so, and the search below copies one per node -- it is sized against
 * that cost rather than in ignorance of it. */
struct Position {
    int8_t sq[64];        // +white, -black, 0 empty
    bool whiteToMove;
    /* Castling rights, in the order KQkq. Cleared when the king or the rook in
     * question moves or is captured -- rights are lost forever, not just while
     * something sits in the way. */
    bool castle[4];
    /* The square a pawn may capture onto en passant, or NO_SQ. Set only for
     * the one move immediately after a double push, which is why it is part of
     * the position rather than a flag on the pawn. */
    uint8_t epSquare;
    /* Plies since the last capture or pawn move. The fifty-move rule is a
     * hundred of these. */
    uint8_t halfmove;
};

inline bool isWhite(int8_t piece) { return piece > 0; }
inline int8_t kind(int8_t piece) { return piece < 0 ? -piece : piece; }
inline bool onBoard(int8_t file, int8_t rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

/** Every move `from` can make without regard to leaving the king exposed. */
uint8_t pseudoMoves(const Position& p, uint8_t from, uint8_t* out);
/** True when `bySideIsWhite` attacks `square` in `p`. */
bool attacked(const Position& p, uint8_t square, bool bySideIsWhite);
uint8_t kingSquare(const Position& p, bool white);
/* Apply a move, including castling, en passant and auto-promotion, and return
 * the piece it captured (EMPTY if none).
 *
 * The return value exists for the captured-piece display and is ignored by the
 * legality filter, which makes moves on a throwaway copy. Reporting it from
 * here rather than having the caller read the destination square first is what
 * keeps en passant correct: that is the one move in chess where the captured
 * piece is not standing on the square being moved to. */
int8_t applyMove(Position& p, uint8_t from, uint8_t to);
/** Legal moves: pseudo-legal, minus any that leave the mover in check. */
uint8_t legalMoves(const Position& p, uint8_t from, uint8_t* out);
bool hasAnyLegalMove(const Position& p);
/* A dead position: neither side could mate even with the other's help.
 *
 * The four standard cases and no more -- king alone against king, king and one
 * knight, king and one bishop, and two lone bishops on same-coloured squares.
 * Anything with a pawn, rook or queen still on the board can be mated with, so
 * it is not dead however hopeless it looks. Deliberately NOT "can the side to
 * move force a win", which is a search. */
bool deadPosition(const Position& p);

/* ---- the computer player (ChessAi.cpp) ------------------------------------
 *
 * Two levels, named for what they feel like to play against rather than for
 * their depth, and both deterministic in (position, seed) so a game can be
 * replayed exactly -- which is what makes a bad move reportable rather than a
 * story about one.
 *
 * The costs this is designed against are in the rules above and are not
 * incidental: there is no make/unmake, so every node copies a Position, and
 * kingSquare() scans all 64 squares for every legality test. MEDIUM_DEPTH is
 * chosen with that in mind. Measure before reaching for one more ply. */
enum class Level : uint8_t { Easy = 0, Medium = 1 };

/** One move, in the from/to the rest of the firmware already speaks. */
struct Move {
    uint8_t from = NO_SQ;
    uint8_t to = NO_SQ;
};

/* xorshift32, declared rather than hidden in the .cpp so a caller can seed it
 * and get the same game twice. */
struct Rng {
    uint32_t s = 0x2545F491u;
    uint32_t next();
    uint16_t below(uint16_t n);
};

/* The most root moves this will consider. The true maximum for a composed
 * position is 218 and nothing reachable in a real game comes near it, but
 * allMoves() is bounded rather than trusting that. */
constexpr uint8_t MAX_ROOT_MOVES = 128;

/** Every legal move for the side to move, up to `cap`. */
uint8_t allMoves(const Position& p, Move* out, uint8_t cap);

/* What a position is worth to the side to move, in centipawns. Exposed so the
 * host test can assert the obvious things about it -- that a queen up is
 * better than a queen down -- rather than only observing it through a search. */
int16_t evaluate(const Position& p);

/* Easy: one ply, plus a look at what can be taken straight back.
 *
 * It is meant to be beaten by a young player who is concentrating, and to lose
 * material in ways a young player can see and copy. What it must NOT do is hang its
 * queen on move four for nothing, because that ends the game and teaches
 * nothing -- so it scores the recapture, and it chooses at random among moves
 * that score alike rather than always playing the first one generated. */
Move chooseEasy(const Position& p, Rng& rng);

/* Medium: alpha-beta to MEDIUM_DEPTH, split at the root so it runs across
 * frames.
 *
 * Resumption is at the root and nowhere else: each step searches one root move
 * to completion, so the stop predicate is only consulted between whole
 * subtrees and a partial result can never be mistaken for a finished one.
 * Go's stepSearch() has the same shape for the same reason -- a 20ms frame
 * cannot hold a whole search, and blocking until it can is a stall the
 * watchdog will log.
 *
 * A subtree that runs past NODE_LIMIT returns what it has. That yields a
 * weaker move, never an illegal one, which is the bargain BackgammonAi makes
 * with its own node ceiling. */
constexpr uint8_t MEDIUM_DEPTH = 3;
constexpr uint32_t NODE_LIMIT = 12000;

struct Search {
    Position root;
    Move move[MAX_ROOT_MOVES];
    int16_t score[MAX_ROOT_MOVES];
    uint8_t count = 0;      // root moves to examine
    uint8_t done = 0;       // how many have been scored so far
    Rng rng;
};

void beginSearch(Search& s, const Position& p, uint32_t seed);

/** The best move found so far. Meaningful once stepSearch() returns true. */
Move bestMove(const Search& s);

/* Score exactly one root move. Exposed only because stepSearch() is a template
 * and therefore has to live in this header. */
void stepOne(Search& s);

/* Advance the search, stopping when `stop()` says this frame's budget is
 * spent. Returns true once every root move has been scored.
 *
 * The predicate is a template parameter so this header stays free of Arduino:
 * the firmware passes a micros() budget, the host test passes a counter. */
template <typename Stop>
bool stepSearch(Search& s, Stop stop) {
    while (s.done < s.count) {
        stepOne(s);
        if (stop()) {
            break;
        }
    }
    return s.done >= s.count;
}

}   // namespace Ch
