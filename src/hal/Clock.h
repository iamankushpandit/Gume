#pragma once

#include <Arduino.h>

namespace Clock {
void begin();
String timeText();
/** "Tue Aug 11 2026", or "--" while the clock is unsynced. */
String dateText();
uint32_t minuteKey();
/** True when the displayed time came from NTP rather than the build clock. */
bool synced();
}

