#include "Clock.h"

namespace {
uint32_t baseSeconds_ = 0;
uint32_t bootMillis_ = 0;
bool ready_ = false;

uint8_t twoDigit(const char* text) {
    return static_cast<uint8_t>((text[0] - '0') * 10 + (text[1] - '0'));
}

void ensureReady() {
    if (ready_) {
        return;
    }
    const char* buildTime = __TIME__;
    const uint8_t hour = twoDigit(buildTime);
    const uint8_t minute = twoDigit(buildTime + 3);
    const uint8_t second = twoDigit(buildTime + 6);
    baseSeconds_ = static_cast<uint32_t>(hour) * 3600UL + static_cast<uint32_t>(minute) * 60UL + second;
    bootMillis_ = millis();
    ready_ = true;
}

uint32_t secondsToday() {
    ensureReady();
    return (baseSeconds_ + (millis() - bootMillis_) / 1000UL) % 86400UL;
}
}

namespace Clock {

void begin() {
    ensureReady();
}

String timeText() {
    const uint32_t seconds = secondsToday();
    uint8_t hour24 = static_cast<uint8_t>(seconds / 3600UL);
    const uint8_t minute = static_cast<uint8_t>((seconds / 60UL) % 60UL);
    const bool pm = hour24 >= 12;
    uint8_t hour12 = hour24 % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }

    char buffer[9];
    snprintf(buffer, sizeof(buffer), "%u:%02u %s", hour12, minute, pm ? "PM" : "AM");
    return String(buffer);
}

uint32_t minuteKey() {
    return secondsToday() / 60UL;
}

}

