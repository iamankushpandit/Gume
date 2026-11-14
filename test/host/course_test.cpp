// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of engine/Course.h: the ladder a player climbs in a game, and
// the deck that deals a level's questions. Course.h is pure C++ with no
// Arduino in it, and this file is what proves that still holds, since it will
// not compile otherwise.
//
// It is not run by CI and not picked up by pio test (the folder name does not
// start with test_). Build and run it with any host C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src test/host/course_test.cpp -o course_test && ./course_test
//
// What it checks. The ladder: where it starts, what moves it and by how much,
// that it cannot leave its ends. The deck, for every pool size from 1 to 256
// and many random streams: no question twice running, every question of a
// level dealt before the pass moves on, nothing left waiting longer than two
// passes, a missed question back after exactly three others, no fixed cycle
// on a small pool, and two players never getting the same order.
#include "engine/Course.h"

#include <cstdio>
#include <cstring>
#include <vector>

using Course::Ladder;
using Course::QuestionDeck;

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

// xorshift32: any stream will do, as long as two seeds differ.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

static void testLadder() {
    Ladder l;
    l.start(5, 0);
    CHECK(l.level() == 1, "never set starts at 1, got %u", l.level());
    l.start(5, 9);
    CHECK(l.level() == 5, "a stored level above the top is the top, got %u", l.level());
    l.start(0, 3);
    CHECK(l.level() == 1 && l.levelCount() == 1, "no levels is one level");

    // Six right in a row: up one, and the meter starts over.
    l.start(5, 2);
    for (int i = 0; i < 5; ++i) {
        CHECK(l.answer(true) == Ladder::Step::Stay, "answer %d moved early", i);
    }
    CHECK(l.percentToNext() == 83, "five of six is 83%%, got %u", l.percentToNext());
    CHECK(l.answer(true) == Ladder::Step::Up, "the sixth right answer moves up");
    CHECK(l.level() == 3 && l.meter() == 0, "level 3 with an empty meter");

    // A miss costs two: 4 right, 1 wrong, then 4 more right to make six.
    l.start(5, 1);
    for (int i = 0; i < 4; ++i) l.answer(true);
    CHECK(l.answer(false) == Ladder::Step::Stay && l.meter() == 2, "4 - 2 = 2");
    for (int i = 0; i < 3; ++i) CHECK(l.answer(true) == Ladder::Step::Stay, "not yet");
    CHECK(l.answer(true) == Ladder::Step::Up, "eight right and one wrong moves up");

    // Three misses from level: down one.
    l.start(5, 3);
    CHECK(l.answer(false) == Ladder::Step::Stay, "one miss stays");
    CHECK(l.answer(false) == Ladder::Step::Stay, "two misses stay");
    CHECK(l.answer(false) == Ladder::Step::Down, "three misses running go down");
    CHECK(l.level() == 2 && l.meter() == 0, "level 2 with an empty meter");
    CHECK(l.percentToNext() == 0, "nothing towards the next level");

    // The ends hold.
    l.start(3, 1);
    for (int i = 0; i < 20; ++i) CHECK(l.answer(false) == Ladder::Step::Stay, "below 1");
    CHECK(l.level() == 1 && l.meter() == Ladder::DOWN_AT, "floor");
    l.start(3, 3);
    for (int i = 0; i < 20; ++i) CHECK(l.answer(true) == Ladder::Step::Stay, "above top");
    CHECK(l.level() == 3 && l.atTop() && l.percentToNext() == 100, "ceiling");

    // Chosen by hand.
    l.setLevel(2);
    CHECK(l.level() == 2 && l.meter() == 0, "set by hand");
    l.setLevel(0);
    CHECK(l.level() == 1, "0 is 1");

    // The band: nine in ten climbs, two in three holds, half sinks.
    struct Case { int right; int of; int wantSign; };
    const Case cases[] = {{9, 10, 1}, {2, 3, 0}, {1, 2, -1}};
    for (const Case& c : cases) {
        Rng rng(77);
        Ladder lad;
        lad.start(40, 20);
        for (int i = 0; i < 4000; ++i) {
            lad.answer(static_cast<int>(rng.next() % c.of) < c.right);
        }
        const int moved = static_cast<int>(lad.level()) - 20;
        if (c.wantSign > 0) CHECK(moved >= 15, "%d/%d should climb, moved %d", c.right, c.of, moved);
        if (c.wantSign < 0) CHECK(moved <= -15, "%d/%d should sink, moved %d", c.right, c.of, moved);
        if (c.wantSign == 0) CHECK(moved > -19 && moved < 19, "%d/%d should hold, moved %d", c.right, c.of, moved);
    }
}

static void testDeckCoverage() {
    for (uint16_t size = 1; size <= 256; ++size) {
        for (uint32_t seed = 1; seed <= 6; ++seed) {
            Rng rng(seed * 2654435761U + size);
            QuestionDeck deck;
            deck.reset(size);
            const uint32_t deals = static_cast<uint32_t>(size) * 12 + 8;
            std::vector<uint32_t> lastSeen(size, 0);     // deal number, 1-based; 0 never
            std::vector<uint32_t> times(size, 0);
            uint16_t prev = QuestionDeck::NONE;
            uint32_t worstGap = 0;
            for (uint32_t n = 1; n <= deals; ++n) {
                const uint16_t item = deck.next(rng.next());
                CHECK(item < size, "size %u: dealt %u", size, item);
                if (item >= size) return;
                if (size > 1) {
                    CHECK(item != prev, "size %u seed %u: %u twice running at deal %u",
                          size, seed, item, n);
                }
                if (lastSeen[item] != 0) {
                    const uint32_t gap = n - lastSeen[item];
                    if (gap > worstGap) worstGap = gap;
                }
                lastSeen[item] = n;
                ++times[item];
                prev = item;
                /* Every item has come up once the first pass and the items
                 * it held back are through: size + size/8 deals at most. */
                if (n == static_cast<uint32_t>(size) + size / 8) {
                    for (uint16_t i = 0; i < size; ++i) {
                        CHECK(times[i] >= 1, "size %u seed %u: item %u unseen after %u deals",
                              size, seed, i, n);
                    }
                }
            }
            /* Nothing waits longer than two passes. */
            CHECK(worstGap <= static_cast<uint32_t>(size) * 2,
                  "size %u seed %u: an item waited %u deals", size, seed, worstGap);
            /* None starved, none favoured: an item can be held back from
             * every other pass at worst, so half its share is the floor. */
            const uint32_t lo = deals / (2U * size);
            const uint32_t hi = (deals * 3U) / (2U * size) + 2U;
            for (uint16_t i = 0; i < size; ++i) {
                CHECK(times[i] >= lo && times[i] <= hi,
                      "size %u seed %u: item %u dealt %u times in %u", size, seed, i,
                      times[i], deals);
            }
        }
    }
}

static void testDeckMissed() {
    for (uint16_t size : {5, 9, 12, 40, 81, 144, 256}) {
        Rng rng(1234 + size);
        QuestionDeck deck;
        deck.reset(size);
        for (int round = 0; round < 200; ++round) {
            const uint16_t item = deck.next(rng.next());
            deck.missed(item);
            /* Exactly RETRY_AFTER others, none of them the missed one. */
            for (uint8_t k = 0; k < QuestionDeck::RETRY_AFTER; ++k) {
                const uint16_t other = deck.next(rng.next());
                CHECK(other != item, "size %u: the missed item came back after %u", size, k);
            }
            const uint16_t back = deck.next(rng.next());
            CHECK(back == item, "size %u: expected %u back, got %u", size, item, back);
        }
    }

    /* A pool smaller than the wait still gives the item back, and never
     * twice running. */
    for (uint16_t size : {2, 3, 4}) {
        Rng rng(99 + size);
        QuestionDeck deck;
        deck.reset(size);
        uint16_t prev = QuestionDeck::NONE;
        std::vector<uint32_t> times(size, 0);
        for (int n = 0; n < 400; ++n) {
            const uint16_t item = deck.next(rng.next());
            CHECK(item < size, "size %u: dealt %u", size, item);
            if (item >= size) return;
            ++times[item];
            if ((rng.next() & 3) == 0) deck.missed(item);
            (void)prev;
            prev = item;
        }
        for (uint16_t i = 0; i < size; ++i) {
            CHECK(times[i] >= 400 / (size * 3), "size %u: item %u starved (%u)", size, i, times[i]);
        }
    }

    /* Everything missed, on a pool the queue can swallow whole. */
    QuestionDeck all;
    all.reset(3);
    Rng rng(5);
    for (int n = 0; n < 60; ++n) {
        const uint16_t item = all.next(rng.next());
        CHECK(item < 3, "dealt %u from three", item);
        all.missed(item);
    }
}

static void testDeckNotACycle() {
    /* On three items the order of one pass must not fix the order of the
     * next: all six orders have to turn up. */
    QuestionDeck deck;
    deck.reset(3);
    Rng rng(4242);
    bool seen[27] = {};
    int orders = 0;
    for (int pass = 0; pass < 300; ++pass) {
        const uint16_t a = deck.next(rng.next());
        const uint16_t b = deck.next(rng.next());
        const uint16_t c = deck.next(rng.next());
        CHECK(a != b && b != c && a != c, "a pass of three repeated an item: %u %u %u", a, b, c);
        const int key = a * 9 + b * 3 + c;
        if (!seen[key]) {
            seen[key] = true;
            ++orders;
        }
    }
    CHECK(orders == 6, "three items came in %d orders, not 6", orders);

    /* Two players, same level: different order. */
    QuestionDeck one, two;
    one.reset(81);
    two.reset(81);
    Rng r1(1), r2(2);
    int same = 0;
    for (int n = 0; n < 81; ++n) {
        if (one.next(r1.next()) == two.next(r2.next())) ++same;
    }
    CHECK(same < 12, "two streams agreed on %d of 81 deals", same);

    /* And the same player on two visits: reset() keeps nothing. */
    QuestionDeck again;
    again.reset(81);
    Rng r3(3);
    std::vector<uint16_t> first;
    for (int n = 0; n < 40; ++n) first.push_back(again.next(r3.next()));
    again.reset(81);
    int repeat = 0;
    for (int n = 0; n < 40; ++n) {
        if (again.next(r3.next()) == first[n]) ++repeat;
    }
    CHECK(repeat < 8, "a second visit repeated %d of 40 deals in place", repeat);
}

static void testDeckEdges() {
    QuestionDeck deck;
    deck.reset(0);
    CHECK(deck.next(123) == 0, "an empty pool deals 0");
    deck.reset(1);
    for (int n = 0; n < 5; ++n) CHECK(deck.next(n * 7919U) == 0, "a pool of one deals it");
    deck.reset(1000);
    CHECK(deck.poolSize() == QuestionDeck::MAX_ITEMS, "a pool is capped at %u",
          QuestionDeck::MAX_ITEMS);
    deck.missed(5000);      // out of range: ignored
    CHECK(deck.next(1) < QuestionDeck::MAX_ITEMS, "still deals");
}

int main() {
    testLadder();
    testDeckCoverage();
    testDeckMissed();
    testDeckNotACycle();
    testDeckEdges();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
