// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The slice of the Arduino-ESP32 core that the firmware under src/ uses,
 * rebuilt on the C++ standard library so that src/ compiles on a PC without
 * a single #ifdef of its own.
 *
 * It is a shim, not a port of the core: every function here exists because a
 * file under src/ calls it, and each one does the least that keeps that
 * caller's meaning. GPIO is the one place with behaviour behind it -- the
 * virtual pins in host/boards/host.h are answered by host/src/HostPlatform. */

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "esp_system.h"

using std::max;
using std::min;
using std::abs;

typedef uint8_t byte;
typedef bool boolean;
typedef uint16_t word;

#define PROGMEM
#define PGM_P const char*
#define PSTR(s) (s)
#define F(s) (s)
#define IRAM_ATTR
#define RTC_NOINIT_ATTR
#define RTC_DATA_ATTR
#define ARDUINO_RUNNING_CORE 1
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))
#define pgm_read_word(addr) (*reinterpret_cast<const uint16_t*>(addr))
#define pgm_read_dword(addr) (*reinterpret_cast<const uint32_t*>(addr))
#define pgm_read_ptr(addr) (*reinterpret_cast<const void* const*>(addr))
#define memcpy_P memcpy
#define strcpy_P strcpy
#define strlen_P strlen

#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))
#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bit(b) (1UL << (b))

inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
    const long span = inMax - inMin;
    if (span == 0) return outMin;
    return (x - inMin) * (outMax - outMin) / span + outMin;
}

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield();

long random(long howBig);
long random(long howSmall, long howBig);
void randomSeed(unsigned long seed);

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
uint16_t analogRead(uint8_t pin);
uint32_t analogReadMilliVolts(uint8_t pin);
void analogReadResolution(uint8_t bits);
void analogSetAttenuation(int attenuation);
void analogSetPinAttenuation(uint8_t pin, int attenuation);
#define ADC_0db 0
#define ADC_2_5db 1
#define ADC_6db 2
#define ADC_11db 3

double ledcSetup(uint8_t channel, double freq, uint8_t resolutionBits);
void ledcAttachPin(uint8_t pin, uint8_t channel);
void ledcDetachPin(uint8_t pin);
void ledcWrite(uint8_t channel, uint32_t duty);

bool getLocalTime(struct tm* info, uint32_t ms = 5000);
void configTime(long gmtOffsetSec, int daylightOffsetSec, const char* server1,
                const char* server2 = nullptr, const char* server3 = nullptr);
void configTzTime(const char* tz, const char* server1,
                  const char* server2 = nullptr, const char* server3 = nullptr);

#include "WString.h"
#include "HardwareSerial.h"
#include "Esp.h"
