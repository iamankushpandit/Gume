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
