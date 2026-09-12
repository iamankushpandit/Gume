#pragma once

#include <stdint.h>

/* The rules of Go, as played here, and nothing about a screen.
 *
 * Pure C++: no Arduino, no drawing, no millis(), no random(). Every function
 * is a function of a flat State, which is what lets a second console compute
 * the same game from the same moves, and what lets the rules be tested off
 * the device -- test/host/go_rules_test.cpp builds against this file with any
 * host compiler. The computer player (GoAi.cpp) is declared here too and is
 * held to the same standard.
 *
 * ------------------------------------------------------------------------
 * The rules as played
 * ------------------------------------------------------------------------
 * Two board sizes: 9x9 everywhere, 19x19 where the panel is large enough to
 * aim at (the screen decides that; the rules take any n up to MAX_N).
 *
 * Five rule sets, chosen before the game and fixed for its length:
 *
 *   Capture1/3/5  First to capture that many stones wins. No passing, no
 *                 scoring, no komi. The form taught in schools, and the
 *                 on-ramp: a whole game is a few minutes.
 *   Area          Two passes end the game. Score is stones on the board plus
 *                 the empty points only you surround, plus komi for White.
 *                 A dead group is captured by playing on -- there is no
 *                 negotiation, which is the clearer lesson for a beginner.
 *   Territory     Two passes end the game, then both players agree which
 *                 groups are dead (the screen's business, through `dead`
 *                 in score()). Score is the empty points you surround plus
 *                 prisoners -- stones captured, and dead stones removed.
 *
 * Komi is 5.5 to White under Area and Territory, kept as eleven half-points
 * so no score is ever a float and no game is ever drawn.
 *
 * Ko is SIMPLE ko: you may not immediately retake a single stone that has
 * just captured a single stone. Positional superko would need a hash of every
 * past position and exists to close loopholes beginners never reach.
 *
 * Suicide is illegal. A move that captures is never suicide, because the
 * captures are removed first.
 */
namespace Go {

constexpr uint8_t MAX_N = 19;
constexpr uint16_t MAX_POINTS = MAX_N * MAX_N;

enum : uint8_t { EMPTY = 0, BLACK = 1, WHITE = 2 };

/** A move that is not a point. */
constexpr uint16_t PASS = 0xFFFF;
constexpr uint16_t NO_POINT = 0xFFFE;

enum class Rules : uint8_t { Capture1 = 0, Capture3 = 1, Capture5 = 2, Area = 3, Territory = 4 };
constexpr uint8_t RULES_COUNT = 5;
const char* rulesName(Rules r);
/** Stones to capture under a Capture rule set; 0 for Area and Territory. */
uint8_t captureTarget(Rules r);
inline bool isCapture(Rules r) { return r <= Rules::Capture5; }

/** Komi to White, in half-points. */
constexpr int16_t KOMI_HALF = 11;

inline uint8_t other(uint8_t colour) { return colour == BLACK ? WHITE : BLACK; }

/* The whole game, flat. Sized for 19x19 and used for 9x9, because one State
 * is 400 bytes and a screen holds one plus an undo copy plus whatever the
 * computer copies to think with. */
struct State {
    uint8_t n = 9;
    uint8_t at[MAX_POINTS] = {};   // EMPTY, BLACK or WHITE
    uint8_t toMove = BLACK;
    Rules rules = Rules::Capture1;
    /* The point the next move may not take, or NO_POINT. Set only by a
     * single-stone capture of a single stone, for one move. */
    uint16_t ko = NO_POINT;
    /* Stones captured BY each colour, indexed by colour. [0] unused. */
    uint16_t captured[3] = {0, 0, 0};
    uint8_t passes = 0;       // consecutive
    uint16_t moves = 0;
    uint16_t last = NO_POINT; // the last stone played, for the marker
    bool over = false;
    /* EMPTY until the game is over. Under Territory the rules cannot know
     * the winner until the dead stones are agreed, so over is set by the
     * second pass and winner stays EMPTY until the screen calls score(). */
    uint8_t winner = EMPTY;
};

inline uint16_t point(uint8_t n, uint8_t row, uint8_t col) {
    return static_cast<uint16_t>(row * n + col);
}
inline uint8_t rowOf(uint8_t n, uint16_t p) { return static_cast<uint8_t>(p / n); }
inline uint8_t colOf(uint8_t n, uint16_t p) { return static_cast<uint8_t>(p % n); }
inline uint16_t points(uint8_t n) { return static_cast<uint16_t>(n * n); }

void reset(State& s, uint8_t n, Rules rules);

/* The group containing `p` and its liberties. `stones` receives the group's
 * points (up to MAX_POINTS) and returns their count; the liberty count is
 * the return value. `p` must be a stone. */
uint16_t group(const State& s, uint16_t p, uint16_t* stones, uint16_t& count);
/** Liberties of the group at `p` alone, cheaper when the stones are not needed. */
uint16_t liberties(const State& s, uint16_t p);

/* May `s.toMove` play at `p`? Empty, not the ko point, and not suicide. PASS
 * is legal except under a Capture rule set. */
bool legal(const State& s, uint16_t p);
/* How many stones `s.toMove` would capture by playing `p`. Zero for an
 * illegal point. Cheap enough for a move generator to ask of every point. */
uint16_t captures(const State& s, uint16_t p);

/* Play `p` or PASS for `s.toMove`. Refuses an illegal move and changes
 * nothing. Applies captures, sets ko, counts passes, and ends the game where
 * the rules do: a Capture target reached, or two passes under Area (scored
 * here) or Territory (over, unscored -- see score()). */
bool play(State& s, uint16_t p);

/* Would `p` fill one of `colour`'s own eyes? A point whose four neighbours
 * are all `colour` or the edge, and whose diagonals are not mostly the other
 * colour. The one move a computer must never make, and a move a random
 * playout must never make either, or the playout kills its own groups. */
bool fillsOwnEye(const State& s, uint16_t p, uint8_t colour);

// ---- scoring -----------------------------------------------------------------

struct Score {
    uint16_t stones[3] = {0, 0, 0};      // on the board, by colour
    uint16_t territory[3] = {0, 0, 0};   // empty points surrounded, by colour
    uint16_t prisoners[3] = {0, 0, 0};   // captured plus dead removed, by colour
    int16_t half[3] = {0, 0, 0};         // the total, in half-points
    uint8_t winner = EMPTY;              // never EMPTY once computed: komi is a half
};

/* Score the position. `dead`, if not nullptr, is one byte per point, nonzero
 * for a stone both players agree is dead: it is removed before territory is
 * counted and, under Territory, added to the other side's prisoners. Under
 * Area the dead stones simply stop being stones. Under a Capture rule set
 * the score is the capture counts and nothing else. */
void score(const State& s, const uint8_t* dead, Score& out);

/* Owner of every point for a finished position, by area: BLACK, WHITE, or
 * EMPTY for a neutral point. Used by the screen to paint territory and by
 * the computer to judge playouts. `dead` as for score(). */
void ownership(const State& s, const uint8_t* dead, uint8_t* out);

/* Toggle the dead mark on the whole group at `p`: every stone of that group
 * takes the opposite of the mark at `p`. A no-op for an empty point. */
void toggleDead(const State& s, uint8_t* dead, uint16_t p);

// ---- the computer player (GoAi.cpp) ----------------------------------------------

enum class Level : uint8_t { Easy = 0, Medium = 1 };

/* A tiny deterministic generator, so the computer's choices are a function
 * of a seed and can be replayed on the host. */
struct Rng {
    uint32_t x = 0x9E3779B9U;
    uint32_t next();
    /** Uniform in [0, n). */
    uint32_t below(uint32_t n);
};

/* The easy player: one move, now. Heuristics only -- captures, escapes,
 * shape, never an own eye -- and PASS when nothing is worth playing. */
uint16_t chooseEasy(const State& s, Rng& rng);

/* The medium player: random playouts, spread across frames. begin() picks
 * the candidates; step() runs playouts until `playouts` have been made or
 * the caller's budget is spent (it checks `stop` between playouts, so the
 * caller decides what a budget is -- micros on the device, a count on the
 * host); best() is the answer. A Search is about 2.6 KB and is a fixed
 * member of the screen, never allocated. */
struct Search {
    State root;
    uint16_t candidate[MAX_POINTS + 1] = {};
    uint16_t wins[MAX_POINTS + 1] = {};
    uint16_t plays[MAX_POINTS + 1] = {};
    uint16_t candidates = 0;
    uint16_t target = 0;   // playouts wanted in all
    uint16_t done = 0;
    Rng rng;
};

/** How many playouts a medium move gets on a board of `n`. */
uint16_t playoutsFor(uint8_t n);
void beginSearch(Search& sr, const State& s, uint32_t seed);
/* Run playouts. Calls `stop()` between playouts and returns early when it
 * says so; returns true once the search is complete. */
template <typename Stop>
bool stepSearch(Search& sr, Stop stop);
/** The candidate with the best record, or PASS. Valid once begun. */
uint16_t bestMove(const Search& sr);

/* One random playout from `s` to the end, returning the winner by area score.
 * Both sides play random legal non-eye-filling moves; at most 2n^2 moves. */
uint8_t playout(State s, Rng& rng);
/* Which stones a playout thinks are dead: from the finished position, run
 * `playouts` playouts and mark a stone dead when its point ends up owned by
 * the other colour in more than half of them. The computer's opinion for the
 * marking phase under Territory, and a fair one -- it is also what the
 * medium player uses to judge a game. */
void estimateDead(const State& s, uint8_t* dead, uint32_t seed, uint16_t playouts);

/* ---- on the air (Go::Net) ------------------------------------------------------
 *
 * The nearby service carries a turn as two six-bit values, `from` and `to`.
 * A point on a 19x19 board is nine bits and what kind of turn it is two
 * more, so a Go turn is an eleven-bit word spread across both:
 *
 *   word = kind<<9 | point            point 0..360
 *   from = word >> 5                  0..63
 *   to   = 32 | (word & 31)           32..63
 *
 * Two things the service already puts on the air must not read as a move,
 * and neither can. Its presence word is 0,0 at ply 0: `to` below 32 is
 * refused. Its reserved ending is 63,63: that decodes to point 511, which is
 * off every board and refused -- static_assert'd below rather than remarked
 * on. `kind` says what the word is: a stone, a pass, a dead-group toggle in
 * the marking phase, or accepting the marking. Resigning is the service's
 * own ending, not a kind. */
namespace Net {

enum class Kind : uint8_t { Play = 0, Pass = 1, ToggleDead = 2, Accept = 3 };

void encode(Kind kind, uint16_t point, uint8_t& from, uint8_t& to);
/** False for anything that is not a Go turn. `point` is PASS for a pass. */
bool decode(uint8_t from, uint8_t to, Kind& kind, uint16_t& point);

constexpr uint8_t TO_MIN = 32;
constexpr uint16_t POINT_MASK = 0x1FF;
static_assert((((63u << 5) | 31u) & POINT_MASK) >= MAX_POINTS,
              "the service's reserved ending would decode as a point on the board");

}   // namespace Net

// ---- template body -----------------------------------------------------------------

template <typename Stop>
bool stepSearch(Search& sr, Stop stop) {
    if (sr.candidates == 0) return true;
    while (sr.done < sr.target) {
        if (stop()) return false;
        const uint16_t i = static_cast<uint16_t>(sr.done % sr.candidates);
        State s = sr.root;
        const uint8_t me = s.toMove;
        if (!play(s, sr.candidate[i])) {
            sr.plays[i] = 0xFFFF;   // never chosen: legality changed underneath
            ++sr.done;
            continue;
        }
        const uint8_t w = s.over ? s.winner : playout(s, sr.rng);
        if (sr.plays[i] != 0xFFFF) {
            ++sr.plays[i];
            if (w == me) ++sr.wins[i];
        }
        ++sr.done;
    }
    return true;
}

}   // namespace Go
