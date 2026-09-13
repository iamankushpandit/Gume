// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <Arduino.h>

/* Compatibility view over the playable `AppRegistry` entries.
 *
 * `AppRegistry` is the authored source now: each playable game declares its
 * own metadata once, and the registry orders those metadata declarations into
 * launcher order. Keep this wrapper derived so there is no second copy to
 * drift.
 */
struct GameCatalogEntry {
    const char* id;
    const char* title;
    const char* subtitle;
    const char* label;
    const char* blurb;
};

constexpr uint8_t GAME_CATALOG_COUNT = 28;
GameCatalogEntry gameCatalogAt(uint8_t index);
