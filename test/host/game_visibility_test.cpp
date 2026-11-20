// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of per-player game visibility (src/hal/GameVisibility.h):
// the id-keyed table Board mirrors each player's "p<N>_gvis" blob into, and
// the schema 2 -> 3 fold of the old per-index keys into it.
//
// "A reboot" here is what Board does across one: the table is stored as the
// blob's bytes and a fresh table is loaded from them, exactly as
// Board::setGameVisibleFor() and Board::loadVisibility() do around NVS.
//
// Not run by CI and not picked up by pio test. From the repository root:
//   g++ -std=c++17 -Wall -Wextra -Isrc test/host/game_visibility_test.cpp -o /tmp/game_visibility && /tmp/game_visibility

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hal/GameVisibility.h"

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond);       \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

using GameVisibility::Table;
using GameVisibility::keyOf;

static int checks = 0;
#define EXPECT(cond) do { CHECK(cond); ++checks; } while (0)

// The catalogue's last eight, launcher indices 32..39, the ones the old 32-bit
// mask could not hold. Index 35 is Ludo.
static const char* const LATE_IDS[] = {
    "chess", "seabattle", "cursive", "ludo", "backgammon", "go", "space", "roman",
};

static Table rebooted(const Table& before) {
    uint8_t blob[GameVisibility::BLOB_BYTES];
    before.store(blob);
    Table after;
    after.load(blob, sizeof(blob));
    return after;
}

static void keysMatchTheCatalogueChecker() {
    // Values from tools/check_catalog.py's course_key(), which is the Python
    // copy of the same fold: the collision check there covers these keys.
    EXPECT(keyOf("memory") == 13639);
    EXPECT(keyOf("money") == 10271);
    EXPECT(keyOf("ludo") == 16051);
    EXPECT(keyOf("backgammon") == 14320);
    EXPECT(keyOf("roman") == 1045);
    for (const char* id : LATE_IDS) {
        EXPECT(keyOf(id) != 0);
        EXPECT(keyOf(id) <= GameVisibility::ID_MASK);
    }
    static_assert(keyOf("ludo") == 16051, "keyOf must be usable at compile time");
}

static void indexThirtyFiveHidesAndStaysHidden() {
    Table t;
    const uint16_t ludo = keyOf("ludo");                 // launcher index 35
    EXPECT(t.shown(ludo, true));                         // default until set
    EXPECT(t.set(ludo, false));
    EXPECT(!t.shown(ludo, true));

    const Table after = rebooted(t);
    EXPECT(!after.shown(ludo, true));
    EXPECT(!after.shown(ludo, false));
    // Nothing else moved with it.
    for (const char* id : LATE_IDS) {
        if (std::strcmp(id, "ludo") == 0) continue;
        EXPECT(after.shown(keyOf(id), true));
        EXPECT(!after.shown(keyOf(id), false));
    }

    // Every one of the last eight can be hidden, and shown again.
    Table all;
    for (const char* id : LATE_IDS) EXPECT(all.set(keyOf(id), false));
    Table back = rebooted(all);
    for (const char* id : LATE_IDS) EXPECT(!back.shown(keyOf(id), true));
    for (const char* id : LATE_IDS) EXPECT(back.set(keyOf(id), true));
    back = rebooted(back);
    for (const char* id : LATE_IDS) EXPECT(back.shown(keyOf(id), false));
}

static void defaultHiddenGameCanBeShown() {
    Table t;
    const uint16_t space = keyOf("space");
    EXPECT(!t.shown(space, false));
    EXPECT(t.set(space, true));
    EXPECT(rebooted(t).shown(space, false));
}

static void setTwiceUsesOneEntry() {
    Table t;
    const uint16_t go = keyOf("go");
    for (int i = 0; i < 100; ++i) EXPECT(t.set(go, (i & 1) != 0));
    int used = 0;
    for (uint8_t i = 0; i < GameVisibility::SLOTS; ++i) used += t.entries[i] != 0;
    EXPECT(used == 1);
}

static void fullTableRefusesNewGamesOnly() {
    Table t;
    for (uint16_t k = 1; k <= GameVisibility::SLOTS; ++k) EXPECT(t.set(k, false));
    EXPECT(!t.set(GameVisibility::SLOTS + 1, false));    // a new game: no room
    EXPECT(t.shown(GameVisibility::SLOTS + 1, true));    // and nothing changed
    EXPECT(t.set(5, true));                              // an existing one: fine
    EXPECT(t.shown(5, false));
}

static void shorterBlobReadsIntoTheFront() {
    // A blob from a firmware with fewer slots, or a truncated one.
    Table t;
    t.set(keyOf("chess"), false);
    uint8_t blob[GameVisibility::BLOB_BYTES];
    t.store(blob);
    Table small;
    small.load(blob, 8);                                 // four entries' worth
    EXPECT(!small.shown(keyOf("chess"), true));
    Table none;
    none.load(blob, 0);
    EXPECT(none.empty());
}

// The schema 2 -> 3 fold, as Board::migrateGameVisibility() does it: each old
// "p<N>_gv<I>" read against the game at index I today.
static void legacyKeysFoldByTodaysIndex() {
    const char* const catalogue[] = {"memory", "money", "flags", "chess", "ludo"};
    const bool oldKeyPresent[] = {true, false, true, true, false};
    const bool oldKeyValue[]   = {false, false, true, false, false};

    Table t;
    for (unsigned i = 0; i < 5; ++i) {
        if (oldKeyPresent[i]) t.foldLegacy(keyOf(catalogue[i]), oldKeyValue[i]);
    }
    const Table after = rebooted(t);
    EXPECT(!after.shown(keyOf("memory"), true));         // was hidden: still is
    EXPECT(after.shown(keyOf("money"), true));           // no key: default
    EXPECT(!after.shown(keyOf("money"), false));
    EXPECT(after.shown(keyOf("flags"), false));          // was shown: still is
    EXPECT(!after.shown(keyOf("chess"), true));

    // Once keyed by id, a game inserted in the middle of the launcher moves
    // nobody's setting: the lookup never mentions an index.
    EXPECT(!after.shown(keyOf("memory"), true));

    // An entry already in the blob is newer than an old key and is kept.
    Table newer;
    newer.set(keyOf("memory"), true);
    EXPECT(!newer.foldLegacy(keyOf("memory"), false));
    EXPECT(newer.shown(keyOf("memory"), false));
}

int main() {
    keysMatchTheCatalogueChecker();
    indexThirtyFiveHidesAndStaysHidden();
    defaultHiddenGameCanBeShown();
    setTwiceUsesOneEntry();
    fullTableRefusesNewGamesOnly();
    shorterBlobReadsIntoTheFront();
    legacyKeysFoldByTodaysIndex();
    std::printf("game_visibility_test: %d checks passed\n", checks);
    return 0;
}
