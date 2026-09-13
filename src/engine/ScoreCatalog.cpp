// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ScoreCatalog.h"
#include "engine/AppRegistry.h"

uint8_t scoreCatalogCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < playableAppCount(); ++i) {
        if (playableAppAt(i).score() != nullptr) {
            ++count;
        }
    }
    return count;
}

const ScoreEntry* scoreCatalogAt(uint8_t index) {
    uint8_t seen = 0;
    for (uint8_t i = 0; i < playableAppCount(); ++i) {
        const ScoreEntry* score = playableAppAt(i).score();
        if (score == nullptr) {
            continue;
        }
        if (seen == index) {
            return score;
        }
        ++seen;
    }
    return nullptr;
}
