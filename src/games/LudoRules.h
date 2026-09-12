#pragma once

#include <stdint.h>

/* Ludo, as rules and nothing else.
 *
 * No drawing, no touch, no timing and no Arduino: everything here is a pure
 * function of a small flat State, so the screen, the computer player and --
 * later -- a console across the room all compute exactly the same game from
 * exactly the same inputs. That property is the design, not a convenience.
 *
 * ------------------------------------------------------------------------
 * The dice are a function, not a generator
 * ------------------------------------------------------------------------
 * Roll number k of a game is die(seed, k). Nothing is "rolled" in the sense of
 * drawing from a stream that only this console can see: tapping Roll reveals a
 * number that the seed had already fixed. A physical die is no more random than
 * that from the thrower's point of view, and it buys the one thing a networked
 * board game needs above all -- every console can check every move, because
 * every console knows what the die said. A move then only has to name a token.
 *
 * ------------------------------------------------------------------------
 * The rules, as played here
 * ------------------------------------------------------------------------
 *   - A token leaves the yard only on a 6, onto its own start square.
 *   - A 6 earns another roll. So does a capture. Reaching home does not.
 *   - Three 6s in a row forfeits the third roll and ends the turn. Moves
 *     already made with the first two stand. A 6 rolled as a capture bonus
 *     still counts towards the three: the rule is about sixes in a row, not
 *     about how the rolls were earned.
 *   - Tokens travel clockwise, the exact number shown, and need the exact
 *     number to reach home.
 *   - Landing on a lone opponent token on an ordinary square sends it home to
 *     its yard.
 *   - Eight squares are safe: the four start squares and the four stars. Tokens
 *     there cannot be captured and any number of colours may share one.
 *   - Two or more tokens of one colour on an ordinary square are a block. No
 *     opponent may land on it or pass it. Blocks do NOT form on safe squares,
 *     deliberately: a start square that could be walled off would let one
 *     player lock another out of the game entirely.
 *   - The first player home with all four wins; the rest play on for the
 *     places behind them.
 *
 * Positions are stored RELATIVE to each colour's own start, so "how far along
 * is this token" is the stored number itself and the same code serves all four
 * colours. Conversion to a square on the shared track happens only where two
 * colours have to be compared.
 */
namespace Ludo {

constexpr uint8_t SEATS = 4;
constexpr uint8_t TOKENS = 4;

/* The shared track, and each colour's start on it. Seat s starts at 13*s. */
constexpr uint8_t TRACK_LEN = 52;
constexpr uint8_t SEAT_OFFSET = 13;
/** Last relative square on the shared track; the next step enters the home column. */
constexpr uint8_t LAST_TRACK = 50;
/** First of five squares in a colour's own home column. */
constexpr uint8_t HOME_COLUMN = 51;
/** The centre. A token here is finished. */
constexpr uint8_t HOME = 56;
/** In the yard, waiting for a 6. */
constexpr uint8_t YARD = 0xFF;

constexpr uint8_t NO_SEAT = 0xFF;
constexpr uint8_t NO_TOKEN = 0xFF;

/* One game. Flat, fixed-size and copyable -- small enough for the computer
 * player to try a move on a scratch copy, and for the screen to save to NVS
 * as it stands. */
struct State {
    uint8_t pos[SEATS][TOKENS];
    /** Seats in this game, one bit each. Empty seats never take a turn. */
    uint8_t playing;
    /** 0 while still playing, else the place finished in (1 = first). */
    uint8_t place[SEATS];
    uint8_t finished;          // how many seats have a place
    uint8_t turn;              // the seat whose turn it is
    /* The die this seat is holding and has not yet used, 0 when it must roll.
     * Held in the state rather than by the screen so that a game saved with a
     * roll outstanding comes back with the same roll outstanding. */
    uint8_t pending;
    uint8_t sixes;             // sixes in a row this turn
    uint16_t rolls;            // dice drawn so far: the index of the next one
    bool over;
};

/* What the seat to move may do with the roll it just made. */
enum class RollResult : uint8_t {
    Choose,     // at least one token can move: call move()
    NoMove,     // nothing can use it: call skip()
    Forfeit,    // third six in a row: call skip()
};

/* What a move did, for the screen to animate and announce. */
struct MoveInfo {
    uint8_t seat = NO_SEAT;
    uint8_t token = NO_TOKEN;
    uint8_t from = YARD;          // relative, before
    uint8_t to = YARD;            // relative, after
    uint8_t capturedSeat = NO_SEAT;
    uint8_t capturedToken = NO_TOKEN;
    bool reachedHome = false;
    bool seatFinished = false;    // that was this seat's fourth token home
    bool bonus = false;           // the same seat rolls again
};

/* A fresh game for the seats in `playingMask` (bit s = seat s), with `first`
 * to move. Who goes first is the caller's decision -- a local game draws it
 * from the seed, a table would take it from whoever dealt the seats -- and a
 * `first` that is not playing falls back to the lowest seat that is. */
void reset(State& s, uint8_t playingMask, uint8_t first);

/** The die for roll number `index` of the game seeded with `seed`: 1..6. */
uint8_t die(uint32_t seed, uint16_t index);

/* A 32-bit mixer, exposed because the computer player draws its own
 * tie-breaks from the same seed and must never disturb the dice by doing so. */
uint32_t mix(uint32_t x);

/* Draw the next die for the seat to move and say what it may do with it.
 * Only valid when s.pending == 0 and the game is not over. */
RollResult roll(State& s, uint32_t seed);

/* Consume a roll that could not be used -- NoMove or Forfeit. A six with no
 * move still earns its bonus; a forfeit never does. */
void skip(State& s);

/* Move `token` of the seat to move by s.pending. False, and nothing changed,
 * if that move is not legal. Always check legality this way rather than
 * trusting a caller: it is the one test every source of moves has to pass. */
bool move(State& s, uint8_t token, MoveInfo& out);

/* Where `token` of the seat to move would land with `roll`, or YARD when it
 * cannot move. The single legality rule, used by move(), the highlights and
 * the computer player alike. */
uint8_t target(const State& s, uint8_t seat, uint8_t token, uint8_t roll);

/** Bit t set when token t of the seat to move can use s.pending. */
uint8_t movable(const State& s);

/** A seat's relative square as a square on the shared track, 0..51. */
inline uint8_t absolute(uint8_t seat, uint8_t rel) {
    return static_cast<uint8_t>((rel + SEAT_OFFSET * seat) % TRACK_LEN);
}
inline bool onTrack(uint8_t rel) { return rel <= LAST_TRACK; }
/** The start squares and the stars. Nothing is captured there. */
inline bool safeSquare(uint8_t abs) {
    const uint8_t q = abs % SEAT_OFFSET;
    return q == 0 || q == 8;
}
inline bool playing(const State& s, uint8_t seat) {
    return (s.playing >> seat) & 1;
}
/** How many of this seat's tokens are home. */
uint8_t homeCount(const State& s, uint8_t seat);

/* ---- the computer player --------------------------------------------- */

enum class Level : uint8_t {
    /* A random legal move, except that it always brings a token out when it
     * can -- a computer that sits in its yard on a 6 looks broken rather than
     * easy. It captures when the move it happened to pick captures, never on
     * purpose. A young child should win most games. */
    Easy = 0,
    /* One step of lookahead and a fixed order of priorities: capture, get
     * home, come out, reach safety, escape a threat, advance the leader. No
     * search -- a roll offers at most four choices and Ludo rewards judgement
     * about each far more than it rewards looking ahead. */
    Normal = 1,
};

/* The token the seat to move should play with s.pending. Only valid when
 * movable(s) != 0. Deterministic in (s, seed): the same position and seed
 * always give the same choice, which is what makes a game replayable. */
uint8_t botChoose(const State& s, Level level, uint32_t seed);

/* ---- a table of consoles ------------------------------------------------
 *
 * How a game on several consoles is spelled in the nearby service's turn --
 * (session, ply, from, to, ack), six bits each for from and to, seven for ply
 * and ack. Nothing new goes on the air for Ludo: this is the same turn Chess
 * and Sea Battle send, read with Ludo's meaning. Kept here, beside the rules,
 * so it is pure and testable like them.
 *
 *   presence  ply 0, from 0, to 0      a console that accepted an invitation
 *   start     ply 0, from 32..61       the host's word: how many people, how
 *             to = roster check          many computers, what level, and a
 *                                       check on who the people are
 *   move      ply 1.., from 8 + seat   one roll: `to` is the token moved, or
 *             to = token or SKIP        SKIP when the roll could not be used
 *   ack       the last ply this console has applied; NOT_STARTED before the
 *             start word was
 *
 * The die never goes on the air. Every console knows the seed, so it knows
 * what each roll was, and accept() checks a move against that before anything
 * is applied: a console can only ever play a move that is legal with the die
 * it actually got. None of `from = to = 63` is producible, which is the
 * service's reserved "I am stopping". */
namespace Net {

constexpr uint8_t PRESENCE = 0;
constexpr uint8_t START_BASE = 32;
constexpr uint8_t MOVE_BASE = 8;
constexpr uint8_t SKIP = 4;
constexpr uint8_t PLY_MASK = 0x7F;
constexpr uint8_t NOT_STARTED = 0x7F;
/** A table is two to four consoles; computers fill up to four seats. */
constexpr uint8_t MAX_HUMANS = SEATS;
constexpr uint8_t MAX_COMPUTERS = 2;

struct Start {
    uint8_t humans = 2;        // consoles at the table, host included
    uint8_t computers = 0;     // computer seats, played by the host
    uint8_t level = 0;         // Level, for every computer seat
    uint8_t rosterCheck = 0;   // of the sorted console tags
};

void encodeStart(const Start& st, uint8_t& from, uint8_t& to);
/** False for anything that is not a start word a host could have sent. */
bool decodeStart(uint8_t from, uint8_t to, Start& out);

inline uint8_t moveFrom(uint8_t seat) { return static_cast<uint8_t>(MOVE_BASE + seat); }
inline bool isMove(uint8_t from) { return from >= MOVE_BASE && from < MOVE_BASE + SEATS; }

/* The host has taken over `seat`: the console that held it went out of range
 * and the host chose to play on without it, so from this ply the seat is a
 * computer seat played by the host, and every console reads its turns from
 * the host's word. A numbered ply like a move, so it is ordered and acked
 * like one; `to` is unused. 16..19, between the moves and the start word,
 * so nothing already on the air can be mistaken for it. Only the host may
 * say it, and a guest that hears its own seat taken leaves the table. */
constexpr uint8_t TAKEOVER_BASE = 16;
inline uint8_t takeoverFrom(uint8_t seat) { return static_cast<uint8_t>(TAKEOVER_BASE + seat); }
inline bool isTakeover(uint8_t from) {
    return from >= TAKEOVER_BASE && from < TAKEOVER_BASE + SEATS;
}
inline uint8_t nextPly(uint8_t ply) { return static_cast<uint8_t>((ply + 1) & PLY_MASK); }
/* Seven-bit sequence order: is `a` at or after `b`? Plies wrap at 128, and
 * the consoles at a table are never more than a handful apart, so half the
 * circle either way is unambiguous. */
inline bool atOrAfter(uint8_t a, uint8_t b) { return ((a - b) & PLY_MASK) < 64; }

/** Sort four-character tags ascending, in place. Every console sorts alike. */
void sortIds(char (*ids)[5], uint8_t n);
/** Six bits over sorted tags: whether two consoles agree who is at the table. */
uint8_t rosterCheck(const char (*ids)[5], uint8_t n);
/** The game's seed, from the session and the sorted tags. */
uint32_t tableSeed(uint8_t session, const char (*ids)[5], uint8_t n);

/* Deal the colours. `out[colour]` is the participant sitting there -- 0 to
 * humans-1 for the sorted tags, humans onwards for the computers -- or
 * NO_SEAT for an empty colour. Returns the playing mask. Two seats sit in
 * opposite corners and three leave Blue empty, as in a local game.
 *
 * Shuffled from the seed, so nobody chooses where they sit and every console
 * deals the same table. The host picks the session and so could, in
 * principle, try sessions until it liked the deal; a child with a Ludo board
 * is not the threat model, and the alternative is a second round trip. */
uint8_t deal(uint32_t seed, uint8_t humans, uint8_t computers, uint8_t out[SEATS]);
/** Which playing seat moves first, from the seed. */
uint8_t firstSeat(uint32_t seed, uint8_t playingMask);

/* Would roll, then `code`, be a correct account of the next turn? Checked on
 * a copy before anything is applied, so a move from the air that does not fit
 * the die this console computed changes nothing at all. */
bool accept(const State& s, uint32_t seed, uint8_t code);

}   // namespace Net

}   // namespace Ludo
