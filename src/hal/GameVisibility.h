// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Which games a player sees, keyed by the game and not by its tile.
 *
 * Visibility used to be one NVS bool per game per player, named after the
 * game's launcher index ("p0_gv12"), mirrored into a 32-bit mask. Both halves
 * were wrong once the catalogue grew. The mask could not hold a 33rd game, so
 * the last eight of forty could not be hidden at all; and an index is a place
 * in the launcher, not a game: inserting one game moved every later game's
 * setting onto its neighbour, exactly the failure the course levels were
 * keyed by id to avoid.
 *
 * So a player's visibility is now one blob of SLOTS two-byte entries. Each
 * entry is the 14-bit key of a game's id -- the same FNV-1a fold the course
 * levels use for track 0 (BrainoApp::courseKey()), so the collision check in
 * tools/check_catalog.py covers both -- with bit 15 saying whether that game
 * is shown. A game with no entry is at its default. One blob rather than a
 * key per game because a key per game per player is up to two hundred of the
 * five hundred NVS entries there are, where this is a handful.
 *
 * Pure C++ with no Arduino, so test/host/game_visibility_test.cpp checks it on
 * a PC. Board keeps one Table as the RAM mirror of the active player's blob:
 * gameVisible() is asked for every tile on every launcher repaint and must not
 * reach NVS. */

#include <stddef.h>
#include <stdint.h>

namespace GameVisibility {

/* The bits of the key that name a game. Matches courseKey()'s ID_BITS with
 * two track bits; tools/check_catalog.py's course_key() is the Python copy. */
constexpr uint8_t ID_BITS = 14;
constexpr uint16_t ID_MASK = static_cast<uint16_t>((1U << ID_BITS) - 1U);
constexpr uint16_t SHOWN_BIT = 0x8000U;

/* Room for every game there is and a catalogue half as long again. A larger
 * number later is safe: an older, shorter blob reads into the front of the
 * table and the rest stays empty. */
constexpr uint8_t SLOTS = 64;
constexpr size_t BLOB_BYTES = SLOTS * sizeof(uint16_t);

/* The 32 launcher indices the old per-index keys could ever have named. */
constexpr uint8_t LEGACY_INDEX_LIMIT = 32;

/** The stored key of a game's id: never 0, which marks an empty entry. */
constexpr uint16_t keyOf(const char* id) {
    uint32_t h = 2166136261UL;                     // FNV-1a
    for (const char* p = id; *p != '\0'; ++p) {
        h ^= static_cast<uint8_t>(*p);
        h *= 16777619UL;
    }
    const uint16_t key = static_cast<uint16_t>(((h >> 16) ^ (h & 0xFFFFU)) & ID_MASK);
    return key == 0 ? 1 : key;
}

/* A game's key as Board takes it. A type of its own, not a bare integer, so a
 * caller still passing a launcher index fails to compile instead of hiding or
 * showing whichever game happens to hash to that number. */
struct Key {
    uint16_t value;
    constexpr explicit Key(uint16_t v) : value(v) {}
};

constexpr Key keyFor(const char* id) { return Key(keyOf(id)); }

struct Table {
    uint16_t entries[SLOTS] = {};

    void clear() {
        for (uint8_t i = 0; i < SLOTS; ++i) entries[i] = 0;
    }

    /** Whether the game has an entry at all. */
    bool has(uint16_t key) const {
        key &= ID_MASK;
        for (uint8_t i = 0; i < SLOTS; ++i) {
            const uint16_t e = entries[i];
            if (e != 0 && (e & ID_MASK) == key) return true;
        }
        return false;
    }

    /** One old per-index setting, for the schema 2 -> 3 migration: taken
     *  unless the table already holds the game, which would mean this
     *  firmware wrote it and it is the newer. True when the table changed. */
    bool foldLegacy(uint16_t key, bool shownNow) {
        if (has(key)) return false;
        return set(key, shownNow);
    }

    /** Shown or hidden as stored, or `fallback` when the game has no entry. */
    bool shown(uint16_t key, bool fallback) const {
        key &= ID_MASK;
        for (uint8_t i = 0; i < SLOTS; ++i) {
            const uint16_t e = entries[i];
            if (e != 0 && (e & ID_MASK) == key) return (e & SHOWN_BIT) != 0;
        }
        return fallback;
    }

    /** Record a game as shown or hidden. False only when the table is full of
     *  other games -- SLOTS distinct keys -- and nothing was written. */
    bool set(uint16_t key, bool shownNow) {
        key &= ID_MASK;
        if (key == 0) key = 1;
        const uint16_t value = static_cast<uint16_t>(key | (shownNow ? SHOWN_BIT : 0));
        int16_t empty = -1;
        for (uint8_t i = 0; i < SLOTS; ++i) {
            const uint16_t e = entries[i];
            if (e == 0) {
                if (empty < 0) empty = i;
            } else if ((e & ID_MASK) == key) {
                entries[i] = value;
                return true;
            }
        }
        if (empty < 0) return false;
        entries[empty] = value;
        return true;
    }

    /** Fill from a stored blob of any length up to BLOB_BYTES; the bytes are
     *  little-endian entries, as the ESP32 and every PC lay a uint16_t out. */
    void load(const uint8_t* bytes, size_t len) {
        clear();
        if (len > BLOB_BYTES) len = BLOB_BYTES;
        for (size_t i = 0; i + 1 < len; i += 2) {
            entries[i / 2] = static_cast<uint16_t>(bytes[i] | (bytes[i + 1] << 8));
        }
    }

    void store(uint8_t* out) const {
        for (uint8_t i = 0; i < SLOTS; ++i) {
            out[2 * i] = static_cast<uint8_t>(entries[i] & 0xFF);
            out[2 * i + 1] = static_cast<uint8_t>(entries[i] >> 8);
        }
    }

    bool empty() const {
        for (uint8_t i = 0; i < SLOTS; ++i) {
            if (entries[i] != 0) return false;
        }
        return true;
    }
};

}   // namespace GameVisibility
