#pragma once

#include <Arduino.h>

namespace Clock {
void begin();
String timeText();
uint32_t minuteKey();
}

