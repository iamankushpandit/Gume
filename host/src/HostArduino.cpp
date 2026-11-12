// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The Arduino core and the small ESP-IDF calls, for the host. See
 * host/shim/Arduino.h. */

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <lwip/stats.h>

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

#include "HostBattery.h"
#include "HostLed.h"
#include "HostMain.h"
#include "HostPlatform.h"
#include "boards/host.h"

HardwareSerial Serial;
EspClass ESP;
SDFS SD;
SPIClass SPI;
stats_ lwip_stats{};

/* ------------------------------------------------------------- time */

namespace {
const auto bootTime = std::chrono::steady_clock::now();

std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}
std::mutex rngMutex;
}   // namespace

uint32_t millis() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - bootTime)
                                     .count());
}

uint32_t micros() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - bootTime)
                                     .count());
}

int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - bootTime)
        .count();
}

/* The runtime sleeps off the rest of each 20ms frame here, so this is where
 * the window gets its events pumped and its picture shown. Off the main
 * thread it is a plain sleep.
 *
 * Once a frame is enough for both, because the loop reads input once a frame
 * anyway. It used to wake every 4ms, about 250 times a second, and kept doing
 * so behind a dark screen: on a handheld that is 250 times a second a core is
 * pulled out of sleep for nothing. Asleep there is no picture, so the whole
 * wait is one sleep -- the runtime's own 100ms poll, the same latency the
 * firmware has always given a touch on a sleeping panel. A long delay() with
 * the screen on still services the window every frame, so it stays drawn. */
constexpr uint32_t SERVICE_EVERY_MS = 20;

void delay(uint32_t ms) {
    if (!HostPlatform::onMainThread()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return;
    }
    const uint32_t until = millis() + ms;
    for (;;) {
        const int32_t left = static_cast<int32_t>(until - millis());
        if (left > 0) {
            const uint32_t step = HostPlatform::screenAsleep()
                                      ? static_cast<uint32_t>(left)
                                      : std::min(static_cast<uint32_t>(left), SERVICE_EVERY_MS);
            std::this_thread::sleep_for(std::chrono::milliseconds(step));
        }
        if (!HostPlatform::service()) hostExit(0);
        if (static_cast<int32_t>(until - millis()) <= 0) break;
    }
}

void delayMicroseconds(uint32_t us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void yield() {}

/* ------------------------------------------------------------- random */

long random(long howBig) {
    if (howBig <= 0) return 0;
    std::lock_guard<std::mutex> lock(rngMutex);
    return static_cast<long>(rng()() % static_cast<uint32_t>(howBig));
}

long random(long howSmall, long howBig) {
    if (howSmall >= howBig) return howSmall;
    return howSmall + random(howBig - howSmall);
}

void randomSeed(unsigned long seed) {
    std::lock_guard<std::mutex> lock(rngMutex);
    if (seed != 0) rng().seed(static_cast<uint32_t>(seed));
}

uint32_t esp_random() {
    std::lock_guard<std::mutex> lock(rngMutex);
    return rng()();
}

void esp_fill_random(void* buf, uint32_t len) {
    auto* out = static_cast<uint8_t*>(buf);
    for (uint32_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>(esp_random());
}

/* ------------------------------------------------------------- GPIO
 *
 * The virtual pins of host/boards/host.h. The BOOT key is Home on the pad or
 * keyboard; the touch IRQ is low while a finger is down, as an FT6336U's
 * INT is. Everything else reads idle. */

void pinMode(uint8_t, uint8_t) {}
namespace {
/* Which LED channel a virtual pin is, or -1. */
int ledChannel(uint8_t pin) {
    if (pin == static_cast<uint8_t>(HostPin::LED_R)) return 0;
    if (pin == static_cast<uint8_t>(HostPin::LED_G)) return 1;
    if (pin == static_cast<uint8_t>(HostPin::LED_B)) return 2;
    return -1;
}
}   // namespace

void digitalWrite(uint8_t pin, uint8_t value) {
    const int ch = ledChannel(pin);
    if (ch < 0) return;
    const bool lit = BOARD.rgb.commonAnode ? value == LOW : value == HIGH;
    HostLed::setChannel(ch, lit ? 255 : 0);
}

int digitalRead(uint8_t pin) {
    if (pin == static_cast<uint8_t>(HostPin::HOME)) return HostPlatform::homeHeld() ? LOW : HIGH;
    if (pin == static_cast<uint8_t>(HostPin::TOUCH_IRQ)) {
        return HostPlatform::contact().down ? LOW : HIGH;
    }
    return HIGH;
}

/* The ADC answers only for the battery-sense pin, in millivolts at the pin --
 * the cell over the profile's divider -- which is what
 * esp_adc_cal_raw_to_voltage() expects back on the host (it is exact here,
 * see shim/esp_adc_cal.h). */
uint16_t analogRead(uint8_t pin) {
#if GUME_HOST_BATTERY
    if (pin == static_cast<uint8_t>(HostPin::BATTERY) && BOARD.battery.dividerRatio > 0.0f) {
        return static_cast<uint16_t>(HostBattery::cellMillivolts() / BOARD.battery.dividerRatio);
    }
#else
    (void)pin;
#endif
    return 0;
}

uint32_t analogReadMilliVolts(uint8_t pin) { return analogRead(pin); }
void analogReadResolution(uint8_t) {}
void analogSetAttenuation(int) {}
void analogSetPinAttenuation(uint8_t, int) {}

/* LEDC: only the backlight channel means anything, and it becomes the
 * window's brightness. */
namespace {
uint8_t ledcBits[16] = {0};
int8_t ledcPin[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
}   // namespace

double ledcSetup(uint8_t channel, double freq, uint8_t bits) {
    if (channel < 16) ledcBits[channel] = bits;
    return freq;
}

void ledcAttachPin(uint8_t pin, uint8_t channel) {
    if (channel < 16) ledcPin[channel] = static_cast<int8_t>(pin);
}

void ledcDetachPin(uint8_t pin) {
    for (int8_t& p : ledcPin) {
        if (p == static_cast<int8_t>(pin)) p = -1;
    }
}

void ledcWrite(uint8_t channel, uint32_t duty) {
    if (channel >= 16 || ledcPin[channel] < 0) return;
    const uint32_t full = (1u << (ledcBits[channel] ? ledcBits[channel] : 8)) - 1u;
    const int led = ledChannel(static_cast<uint8_t>(ledcPin[channel]));
    if (led >= 0) {
        uint32_t level = duty > full ? full : duty;
        if (BOARD.rgb.commonAnode) level = full - level;
        HostLed::setChannel(led, static_cast<uint8_t>(level * 255u / full));
        return;
    }
    if (ledcPin[channel] != HostPin::BACKLIGHT) return;
    float level = static_cast<float>(duty) / static_cast<float>(full);
    if (!BOARD.panel.backlightActiveHigh) level = 1.0f - level;
    HostPlatform::setBacklight(level);
}

/* ------------------------------------------------------------- clock
 *
 * The operating system keeps the time, so the clock is always "synced". A
 * zone the owner picks in Settings still applies -- applyTimeConfig() sets TZ
 * itself -- and until then localtime() follows the host's own zone. */

bool getLocalTime(struct tm* info, uint32_t) {
    const time_t now = time(nullptr);
    return localtime_r(&now, info) != nullptr;
}

void configTime(long, int, const char*, const char*, const char*) {}

void configTzTime(const char* tz, const char*, const char*, const char*) {
    if (tz && *tz) {
        setenv("TZ", tz, 1);
        tzset();
    }
}

/* ------------------------------------------------------------- ESP */

namespace {
/* Random, never the PC's own, and kept: see host/shim/esp_mac.h and
 * HostPlatform::radioId(). Marked locally administered, as a made-up MAC
 * must be. */
const uint8_t* hostMac() {
    static uint8_t mac[6];
    static bool made = false;
    if (!made) {
        HostPlatform::radioId(mac);
        mac[0] = static_cast<uint8_t>((mac[0] & 0xFC) | 0x02);
        made = true;
    }
    return mac;
}
}   // namespace

esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t) {
    std::memcpy(mac, hostMac(), 6);
    return ESP_OK;
}

esp_err_t esp_efuse_mac_get_default(uint8_t* mac) { return esp_read_mac(mac, ESP_MAC_WIFI_STA); }

uint64_t EspClass::getEfuseMac() {
    uint64_t v = 0;
    std::memcpy(&v, hostMac(), 6);
    return v;
}

void EspClass::restart() { HostPlatform::restart(); }
void esp_restart() { HostPlatform::restart(); }
esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
void esp_deep_sleep_start() {}

const char* esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK: return "ESP_OK";
        case ESP_FAIL: return "ESP_FAIL";
        default: return "ESP_ERR";
    }
}

/* ------------------------------------------------------------- Serial
 *
 * stdout out, stdin in. A reader thread turns stdin into a byte queue, so the
 * console's non-blocking available()/read() polling works from a terminal
 * and costs nothing when the host was started without one. */

namespace {
std::mutex rxMutex;
std::deque<uint8_t> rxQueue;
std::once_flag rxStarted;

void startReader() {
    std::thread([] {
        int c;
        while ((c = std::getchar()) != EOF) {
            std::lock_guard<std::mutex> lock(rxMutex);
            rxQueue.push_back(static_cast<uint8_t>(c));
        }
    }).detach();
}
}   // namespace

int HardwareSerial::available() {
    std::call_once(rxStarted, startReader);
    std::lock_guard<std::mutex> lock(rxMutex);
    return static_cast<int>(rxQueue.size());
}

int HardwareSerial::read() {
    std::call_once(rxStarted, startReader);
    std::lock_guard<std::mutex> lock(rxMutex);
    if (rxQueue.empty()) return -1;
    const int c = rxQueue.front();
    rxQueue.pop_front();
    return c;
}

int HardwareSerial::peek() {
    std::lock_guard<std::mutex> lock(rxMutex);
    return rxQueue.empty() ? -1 : rxQueue.front();
}

void HardwareSerial::flush() { std::fflush(stdout); }

size_t HardwareSerial::write(uint8_t c) {
    std::fputc(c, stdout);
    if (c == '\n') std::fflush(stdout);
    return 1;
}

size_t HardwareSerial::write(const uint8_t* data, size_t len) {
    std::fwrite(data, 1, len, stdout);
    if (std::memchr(data, '\n', len)) std::fflush(stdout);
    return len;
}

/* ------------------------------------------------------------- Print */

size_t Print::write(const uint8_t* data, size_t len) {
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) n += write(data[i]);
    return n;
}

size_t Print::write(const char* s) { return print(s); }

size_t Print::print(const char* s) {
    if (!s) return 0;
    return write(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

size_t Print::print(const String& s) { return print(s.c_str()); }
size_t Print::print(char c) { return write(static_cast<uint8_t>(c)); }
size_t Print::print(int v, int base) { return print(String(v, static_cast<unsigned char>(base))); }
size_t Print::print(unsigned int v, int base) { return print(String(v, static_cast<unsigned char>(base))); }
size_t Print::print(long v, int base) { return print(String(v, static_cast<unsigned char>(base))); }
size_t Print::print(unsigned long v, int base) { return print(String(v, static_cast<unsigned char>(base))); }
size_t Print::print(long long v, int base) { return print(String(v, static_cast<unsigned char>(base))); }
size_t Print::print(unsigned long long v, int base) { return print(String(v, static_cast<unsigned char>(base))); }
size_t Print::print(double v, int digits) { return print(String(v, static_cast<unsigned int>(digits))); }

size_t Print::println() { return print("\r\n"); }

size_t Print::printf(const char* fmt, ...) {
    char small[256];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(small, sizeof(small), fmt, args);
    va_end(args);
    if (n < 0) return 0;
    if (static_cast<size_t>(n) < sizeof(small)) return write(reinterpret_cast<const uint8_t*>(small), n);
    std::string big(static_cast<size_t>(n) + 1, '\0');
    va_start(args, fmt);
    std::vsnprintf(&big[0], big.size(), fmt, args);
    va_end(args);
    return write(reinterpret_cast<const uint8_t*>(big.data()), static_cast<size_t>(n));
}

/* ------------------------------------------------------------- String */

namespace {
std::string inBase(unsigned long long v, unsigned char base, bool negative) {
    if (base < 2 || base > 36) base = 10;
    char buf[72];
    int at = sizeof(buf) - 1;
    buf[at] = '\0';
    do {
        const unsigned digit = static_cast<unsigned>(v % base);
        buf[--at] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
        v /= base;
    } while (v != 0 && at > 1);
    if (negative) buf[--at] = '-';
    return std::string(buf + at);
}

std::string signedInBase(long long v, unsigned char base) {
    /* Arduino prints negative numbers in decimal with a sign, and in any
     * other base as the two's-complement bit pattern. */
    if (base == 10) {
        const bool negative = v < 0;
        const unsigned long long mag = negative ? 0ULL - static_cast<unsigned long long>(v)
                                                : static_cast<unsigned long long>(v);
        return inBase(mag, base, negative);
    }
    return inBase(static_cast<unsigned long>(v), base, false);
}
}   // namespace

String::String(unsigned char value, unsigned char base) : s_(inBase(value, base, false)) {}
String::String(int value, unsigned char base) : s_(signedInBase(value, base)) {}
String::String(unsigned int value, unsigned char base) : s_(inBase(value, base, false)) {}
String::String(long value, unsigned char base) : s_(signedInBase(value, base)) {}
String::String(unsigned long value, unsigned char base) : s_(inBase(value, base, false)) {}
String::String(long long value, unsigned char base) : s_(signedInBase(value, base)) {}
String::String(unsigned long long value, unsigned char base) : s_(inBase(value, base, false)) {}

String::String(float value, unsigned int decimals) : String(static_cast<double>(value), decimals) {}

String::String(double value, unsigned int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(decimals), value);
    s_ = buf;
}

bool String::equalsIgnoreCase(const String& o) const {
    return s_.size() == o.s_.size() && strcasecmp(s_.c_str(), o.s_.c_str()) == 0;
}

String String::substring(unsigned int from, unsigned int to) const {
    if (from > to) std::swap(from, to);
    if (from >= s_.size()) return String();
    if (to > s_.size()) to = static_cast<unsigned int>(s_.size());
    return String(s_.substr(from, to - from));
}

void String::trim() {
    size_t a = 0;
    while (a < s_.size() && std::isspace(static_cast<unsigned char>(s_[a]))) ++a;
    size_t b = s_.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s_[b - 1]))) --b;
    s_ = s_.substr(a, b - a);
}

void String::toUpperCase() {
    for (char& c : s_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

void String::toLowerCase() {
    for (char& c : s_) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

void String::replace(char find, char with) {
    for (char& c : s_) {
        if (c == find) c = with;
    }
}

void String::replace(const String& find, const String& with) {
    if (find.s_.empty()) return;
    size_t at = 0;
    while ((at = s_.find(find.s_, at)) != std::string::npos) {
        s_.replace(at, find.s_.size(), with.s_);
        at += with.s_.size();
    }
}

void String::remove(unsigned int index) {
    if (index < s_.size()) s_.erase(index);
}

void String::remove(unsigned int index, unsigned int count) {
    if (index < s_.size()) s_.erase(index, count);
}

void String::toCharArray(char* buf, unsigned int size, unsigned int index) const {
    if (!buf || size == 0) return;
    if (index >= s_.size()) {
        buf[0] = '\0';
        return;
    }
    const size_t n = std::min<size_t>(size - 1, s_.size() - index);
    std::memcpy(buf, s_.data() + index, n);
    buf[n] = '\0';
}
