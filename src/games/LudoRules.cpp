#include "LudoRules.h"

namespace Ludo {

namespace {

/* Tokens of `seat` standing on shared-track square `abs`. Home column and
 * yard tokens are nowhere on the shared track, so they never count. */
uint8_t countAt(const State& s, uint8_t seat, uint8_t abs) {
    uint8_t n = 0;
    for (uint8_t t = 0; t < TOKENS; ++t) {
        const uint8_t p = s.pos[seat][t];
        if (onTrack(p) && absolute(seat, p) == abs) {
            ++n;
        }
    }
    return n;
}

/* An opponent's block on `abs`: two or more of one other colour. On a safe
 * square this is never asked -- see the header for why blocks do not form
 * there. */
bool opponentBlock(const State& s, uint8_t seat, uint8_t abs) {
    for (uint8_t o = 0; o < SEATS; ++o) {
        if (o != seat && playing(s, o) && countAt(s, o, abs) >= 2) {
            return true;
        }
    }
    return false;
}

void passTurn(State& s) {
    s.sixes = 0;
    for (uint8_t i = 1; i <= SEATS; ++i) {
        const uint8_t next = static_cast<uint8_t>((s.turn + i) % SEATS);
        if (playing(s, next) && s.place[next] == 0) {
            s.turn = next;
            return;
        }
    }
}

uint8_t seatsStillPlaying(const State& s) {
    uint8_t n = 0;
    for (uint8_t seat = 0; seat < SEATS; ++seat) {
        if (playing(s, seat) && s.place[seat] == 0) {
            ++n;
        }
    }
    return n;
}

}   // namespace

uint32_t mix(uint32_t x) {
    /* lowbias32 (Wellons). Avalanches well enough that consecutive roll
     * indices give unrelated dice, in two multiplies. */
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

uint8_t die(uint32_t seed, uint16_t index) {
    /* The modulo bias of 2^32 against 6 is four values in four billion. */
    const uint32_t h = mix(seed ^ mix(index * 0x9E3779B9U + 0x7F4A7C15U));
    return static_cast<uint8_t>(h % 6 + 1);
}

void reset(State& s, uint8_t playingMask, uint8_t first) {
    for (uint8_t seat = 0; seat < SEATS; ++seat) {
        for (uint8_t t = 0; t < TOKENS; ++t) {
            s.pos[seat][t] = YARD;
        }
        s.place[seat] = 0;
    }
    s.playing = static_cast<uint8_t>(playingMask & 0x0F);
    s.finished = 0;
    s.pending = 0;
    s.sixes = 0;
    s.rolls = 0;
    s.turn = 0;
    if (first < SEATS && playing(s, first)) {
        s.turn = first;
    } else {
        for (uint8_t seat = 0; seat < SEATS; ++seat) {
            if (playing(s, seat)) {
                s.turn = seat;
                break;
            }
        }
    }
    s.over = seatsStillPlaying(s) < 2;
}

uint8_t homeCount(const State& s, uint8_t seat) {
    uint8_t n = 0;
    for (uint8_t t = 0; t < TOKENS; ++t) {
        if (s.pos[seat][t] == HOME) {
            ++n;
        }
    }
    return n;
}

uint8_t target(const State& s, uint8_t seat, uint8_t token, uint8_t roll) {
    if (roll < 1 || roll > 6 || token >= TOKENS) {
        return YARD;
    }
    const uint8_t p = s.pos[seat][token];
    if (p == HOME) {
        return YARD;
    }
    if (p == YARD) {
        /* The start square is safe, so no block can stand on it: a 6 always
         * brings a token out. */
        return roll == 6 ? 0 : YARD;
    }
    const uint8_t dest = static_cast<uint8_t>(p + roll);
    if (dest > HOME) {
        return YARD;   // home needs the exact number
    }
    /* Every square passed over AND the one landed on: a block can be neither
     * jumped nor landed on. The home column is private, so only shared-track
     * squares are asked. */
    for (uint8_t step = static_cast<uint8_t>(p + 1); step <= dest; ++step) {
        if (!onTrack(step)) {
            break;
        }
        const uint8_t abs = absolute(seat, step);
        if (!safeSquare(abs) && opponentBlock(s, seat, abs)) {
            return YARD;
        }
    }
    return dest;
}

uint8_t movable(const State& s) {
    /* A third six is held in `pending` until skip() consumes it, and it must
     * never be playable in the meantime -- a game saved at exactly that moment
     * would otherwise come back offering the forfeited roll as a move. */
    if (s.over || s.pending == 0 || s.sixes >= 3) {
        return 0;
    }
    uint8_t mask = 0;
    for (uint8_t t = 0; t < TOKENS; ++t) {
        if (target(s, s.turn, t, s.pending) != YARD) {
            mask |= static_cast<uint8_t>(1U << t);
        }
    }
    return mask;
}

RollResult roll(State& s, uint32_t seed) {
    const uint8_t value = die(seed, s.rolls);
    ++s.rolls;
    s.pending = value;
    if (value == 6) {
        if (++s.sixes >= 3) {
            return RollResult::Forfeit;
        }
    } else {
        s.sixes = 0;
    }
    return movable(s) != 0 ? RollResult::Choose : RollResult::NoMove;
}

void skip(State& s) {
    /* A six that could not be used still earns its bonus roll; the third six
     * in a row never does, and is the only way `sixes` reaches three. */
    const bool bonus = s.pending == 6 && s.sixes < 3;
    s.pending = 0;
    if (!bonus) {
        passTurn(s);
    }
}

bool move(State& s, uint8_t token, MoveInfo& out) {
    if (s.over || s.pending == 0 || s.sixes >= 3) {
        return false;
    }
    const uint8_t seat = s.turn;
    const uint8_t dest = target(s, seat, token, s.pending);
    if (dest == YARD) {
        return false;
    }

    out = MoveInfo{};
    out.seat = seat;
    out.token = token;
    out.from = s.pos[seat][token];
    out.to = dest;
    s.pos[seat][token] = dest;

    bool captured = false;
    if (onTrack(dest)) {
        const uint8_t abs = absolute(seat, dest);
        if (!safeSquare(abs)) {
            /* target() has already refused an opponent's block here, so at
             * most one opponent token can be standing on this square. */
            for (uint8_t o = 0; o < SEATS && !captured; ++o) {
                if (o == seat || !playing(s, o)) {
                    continue;
                }
                for (uint8_t t = 0; t < TOKENS; ++t) {
                    const uint8_t p = s.pos[o][t];
                    if (onTrack(p) && absolute(o, p) == abs) {
                        s.pos[o][t] = YARD;
                        out.capturedSeat = o;
                        out.capturedToken = t;
                        captured = true;
                        break;
                    }
                }
            }
        }
    }

    if (dest == HOME) {
        out.reachedHome = true;
        if (homeCount(s, seat) == TOKENS) {
            s.place[seat] = ++s.finished;
            out.seatFinished = true;
        }
    }

    /* One seat left is not a game: it takes the last place and play stops. */
    if (seatsStillPlaying(s) <= 1) {
        for (uint8_t o = 0; o < SEATS; ++o) {
            if (playing(s, o) && s.place[o] == 0) {
                s.place[o] = ++s.finished;
            }
        }
        s.over = true;
    }

    out.bonus = !s.over && !out.seatFinished && (s.pending == 6 || captured);
    s.pending = 0;
    if (!s.over && !out.bonus) {
        passTurn(s);
    }
    return true;
}

/* ---- the computer player ---------------------------------------------- */

namespace {

/* Could an opponent land on shared square `abs` with its next roll? Only a
 * token on the track counts, and only one that would still BE on the track
 * after the distance: a token that turns off into its own home column first
 * is no threat to anything past the turning. */
bool threatened(const State& s, uint8_t seat, uint8_t abs) {
    if (safeSquare(abs)) {
        return false;
    }
    for (uint8_t o = 0; o < SEATS; ++o) {
        if (o == seat || !playing(s, o) || s.place[o] != 0) {
            continue;
        }
        for (uint8_t t = 0; t < TOKENS; ++t) {
            const uint8_t p = s.pos[o][t];
            if (!onTrack(p)) {
                continue;
            }
            const uint8_t d = static_cast<uint8_t>(
                (abs + TRACK_LEN - absolute(o, p)) % TRACK_LEN);
            if (d >= 1 && d <= 6 && p + d <= LAST_TRACK) {
                return true;
            }
        }
    }
    return false;
}

int16_t score(const State& s, uint8_t token) {
    const uint8_t seat = s.turn;
    const uint8_t from = s.pos[seat][token];
    State after = s;
    MoveInfo info;
    if (!move(after, token, info)) {
        return -32000;
    }
    const uint8_t to = info.to;
    int16_t v = 0;

    if (info.capturedSeat != NO_SEAT) v += 100;
    if (to == HOME) v += 80;
    if (from == YARD) v += 60;
    if (to >= HOME_COLUMN && to < HOME && from < HOME_COLUMN) v += 25;

    if (onTrack(to)) {
        const uint8_t abs = absolute(seat, to);
        if (safeSquare(abs)) {
            v += 20;
        } else if (countAt(after, seat, abs) >= 2) {
            v += 15;   // a block: nobody lands here now
        } else if (threatened(after, seat, abs)) {
            v -= 45;
        }
    }
    if (from != YARD && onTrack(from)) {
        const uint8_t abs = absolute(seat, from);
        if (!safeSquare(abs)) {
            const uint8_t together = countAt(s, seat, abs);
            if (together == 1 && threatened(s, seat, abs)) {
                v += 30;   // running from a token that could take this one
            } else if (together == 2 && threatened(after, seat, abs)) {
                v -= 20;   // breaking a block leaves the other one exposed
            }
        }
    }
    /* The tie-breaker among otherwise equal moves: the token further along. A
     * token nearer home has more invested in it and fewer squares left to be
     * caught on. */
    v += static_cast<int16_t>(to / 4);
    return v;
}

}   // namespace

uint8_t botChoose(const State& s, Level level, uint32_t seed) {
    const uint8_t mask = movable(s);
    if (mask == 0) {
        return NO_TOKEN;
    }
    /* Its own stream, salted away from the dice, so asking the computer what
     * it would do can never change what the die says next. */
    const uint32_t noise = mix(seed ^ 0xB0775EEDU ^ mix(s.rolls));

    uint8_t legal[TOKENS];
    uint8_t count = 0;
    for (uint8_t t = 0; t < TOKENS; ++t) {
        if (mask & (1U << t)) {
            legal[count++] = t;
        }
    }

    if (level == Level::Easy) {
        for (uint8_t i = 0; i < count; ++i) {
            if (s.pos[s.turn][legal[i]] == YARD) {
                return legal[i];
            }
        }
        return legal[noise % count];
    }

    uint8_t best = legal[0];
    int16_t bestScore = -32767;
    uint32_t bestTie = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const int16_t v = score(s, legal[i]);
        const uint32_t tie = mix(noise + legal[i]);
        if (v > bestScore || (v == bestScore && tie > bestTie)) {
            best = legal[i];
            bestScore = v;
            bestTie = tie;
        }
    }
    return best;
}

}   // namespace Ludo
