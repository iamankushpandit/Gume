#pragma once

#include <stdint.h>

/* Backgammon, as rules and nothing else.
 *
 * Pure C++ -- no Arduino, no drawing, no clock -- for the same two reasons as
 * LudoRules: a second console must compute exactly the same game from the
 * same inputs, and the rules must be testable off the device
 * (test/host/backgammon_rules_test.cpp).
 *
 * ------------------------------------------------------------------------
 * The board
 * ------------------------------------------------------------------------
 * pt[i] is White's (i+1)-point: positive counts are White checkers, negative
 * are Black. White moves from 24 down to 1 and bears off below 1; Black moves
 * the other way and bears off above 24. White's home board is pt[0..5],
 * Black's is pt[18..23]. A move names its source (a point, or BAR) and its
 * destination (a point, or OFF), so it fits the nearby service's six-bit
 * from/to exactly.
 *
 * ------------------------------------------------------------------------
 * The rules, as played here
 * ------------------------------------------------------------------------
 *   - Standard start: 2 on the 24-point, 5 on the 13, 3 on the 8, 5 on the 6.
 *   - Opening roll: each side one die, ties rolled again, the higher moves
 *     first and plays both numbers.
 *   - Doubles are played four times.
 *   - A checker on the bar must enter before anything else moves.
 *   - A point with two or more opposing checkers is closed; landing on a lone
 *     opposing checker (a blot) hits it to the bar.
 *   - Bearing off needs all fifteen home. The exact number bears off from its
 *     point; a higher number bears off from the highest occupied point only
 *     when nothing sits higher.
 *   - As many dice as possible must be used, and when only one of two
 *     different dice can be used it must be the higher one if that one can be.
 *   - A single game, won as Single (1), Gammon (2: the loser bore nothing off)
 *     or Backgammon (3: ...and still has a checker on the bar or in the
 *     winner's home board). No doubling cube.
 *
 * ------------------------------------------------------------------------
 * The dice are a function of the seed
 * ------------------------------------------------------------------------
 * As in Ludo: roll k of a game is die(seed, k). A local game draws its seed
 * from the hardware; a game on two consoles derives it from the session and
 * both consoles' tags, so both know every roll and a move only has to say
 * which checker went where -- and every move received can be checked against
 * the dice the receiver computed.
 */
namespace Bg {

constexpr uint8_t POINTS = 24;
constexpr uint8_t CHECKERS = 15;
constexpr uint8_t WHITE = 0;
constexpr uint8_t BLACK = 1;
/** A source that is the bar, and a destination that is off the board. */
constexpr uint8_t BAR = 24;
constexpr uint8_t OFF = 25;
constexpr uint8_t NO_POINT = 0xFF;
constexpr uint8_t MAX_DICE = 4;
/** More than the single moves any position can offer: 15 sources x 2 dice. */
constexpr uint8_t MAX_MOVES = 32;

struct Position {
    int8_t pt[POINTS];
    uint8_t bar[2];
    uint8_t off[2];
};

/* One checker moved by one die. `die` is the value used, which the source and
 * destination imply except when bearing off with a higher number. */
struct Move {
    uint8_t from = NO_POINT;
    uint8_t to = NO_POINT;
    uint8_t die = 0;
};

/* The dice still to be played this turn: two values, or four of one. */
struct Dice {
    uint8_t value[MAX_DICE] = {0, 0, 0, 0};
    uint8_t count = 0;
};

enum class Result : uint8_t { None = 0, Single = 1, Gammon = 2, Backgammon = 3 };

inline uint8_t opponent(uint8_t side) { return side == WHITE ? BLACK : WHITE; }
inline int8_t sign(uint8_t side) { return side == WHITE ? 1 : -1; }
/** Checkers `side` has on point `i` (0 if the point is the other side's). */
inline uint8_t mine(const Position& p, uint8_t side, uint8_t i) {
    const int8_t v = static_cast<int8_t>(p.pt[i] * sign(side));
    return v > 0 ? static_cast<uint8_t>(v) : 0;
}
/** Pips from `from` to off the board, for `side`. The bar is 25. */
inline uint8_t pipsToGo(uint8_t side, uint8_t from) {
    if (from == BAR) return 25;
    return side == WHITE ? static_cast<uint8_t>(from + 1) : static_cast<uint8_t>(POINTS - from);
}

void setup(Position& p);

uint32_t mix(uint32_t x);
/** Roll number `index` of the game seeded with `seed`: 1..6. */
uint8_t die(uint32_t seed, uint16_t index);
/** The next turn's dice, advancing `rolls` by two. Doubles give four. */
void rollDice(uint32_t seed, uint16_t& rolls, Dice& out);
/* The opening roll: one die each, rolled again on a tie. Sets who moves
 * first and the two numbers they play, and advances `rolls` past all of it. */
void openingRoll(uint32_t seed, uint16_t& rolls, uint8_t& first, Dice& out);
/** Seed for a game between two consoles, the same on both. */
uint32_t tableSeed(uint8_t session, const char* idA, const char* idB);

/* Where a checker of `side` on `from` lands with `roll`, or NO_POINT. The
 * one-step rule only: the bar, closed points and bearing off. Whether the
 * move uses the dice as the rules require is legalMoves()' question. */
uint8_t target(const Position& p, uint8_t side, uint8_t from, uint8_t roll);
/** Make one move. Returns true when it hit a blot. */
bool apply(Position& p, uint8_t side, const Move& m);
/** Remove one die of this value from what is left to play. */
void useDie(Dice& d, uint8_t value);

/** The most dice `side` can use from here. */
uint8_t maxUsable(const Position& p, uint8_t side, const Dice& d);
/* Every move `side` may make now: each keeps the most dice usable, and
 * respects the higher-die rule. The single definition of a legal move --
 * the screen's highlights, the computer and moves from another console all
 * ask this. Returns how many were written to `out`. */
uint8_t legalMoves(const Position& p, uint8_t side, const Dice& d, Move* out, uint8_t cap);
/* The legal move from `from` to `to`, with the die it uses. When two dice
 * would both do (bearing off with a 5 or a 6 from the 4-point), the smaller
 * is used, so the bigger is kept -- and so two consoles always agree. */
bool findMove(const Position& p, uint8_t side, const Dice& d, uint8_t from, uint8_t to,
              Move& out);

uint16_t pipCount(const Position& p, uint8_t side);
/** All fifteen, wherever they are: points, bar and off. */
uint8_t checkerTotal(const Position& p, uint8_t side);
inline bool won(const Position& p, uint8_t side) { return p.off[side] == CHECKERS; }
/** How `winner` won, or None if they have not. */
Result result(const Position& p, uint8_t winner);

/* ---- the computer player ------------------------------------------------ */

/* A whole turn's moves, in order. */
struct Plan {
    Move moves[MAX_DICE];
    uint8_t count = 0;
};

/* The computer's turn. Enumerates every complete legal sequence -- doubles in
 * a canonical order that loses no final position -- and plays the one whose
 * resulting position scores best: pip lead, made points (home points and
 * runs of them more), opponents on the bar, minus blots weighted by how many
 * opposing checkers can reach them. No lookahead into the next roll. Fixed
 * memory, no heap. */
void choosePlan(const Position& p, uint8_t side, const Dice& d, Plan& out);

}   // namespace Bg
