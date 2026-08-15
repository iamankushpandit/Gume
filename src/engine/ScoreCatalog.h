#pragma once

#include <Arduino.h>

#include "engine/AppRegistry.h"

using ScoreEntry = AppScoreInfo;

uint8_t scoreCatalogCount();
const ScoreEntry* scoreCatalogAt(uint8_t index);
