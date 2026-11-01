// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Print and Serial. The serial console writes its replies to a Print&, so
 * the formatting lives in Print and a sink only says where bytes go.
 *
 * Serial is stdout and stdin. Output is the boot log and the console's
 * replies, line for line what a board prints over USB; input is the same
 * console, so `identify` and `get` can be typed into the terminal that
 * launched the host build. */

#include <cstddef>
#include <cstdint>

class String;

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* data, size_t len);
    size_t write(const char* s);

    size_t print(const char* s);
    size_t print(const String& s);
    size_t print(char c);
    size_t print(int v, int base = 10);
    size_t print(unsigned int v, int base = 10);
    size_t print(long v, int base = 10);
    size_t print(unsigned long v, int base = 10);
    size_t print(long long v, int base = 10);
    size_t print(unsigned long long v, int base = 10);
    size_t print(double v, int digits = 2);

    size_t println();
    template <typename T>
    size_t println(const T& v) {
        const size_t n = print(v);
        return n + println();
    }
    template <typename T>
    size_t println(const T& v, int format) {
        const size_t n = print(v, format);
        return n + println();
    }

    size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    virtual void flush() {}
};

class HardwareSerial : public Print {
public:
    using Print::write;

    void begin(unsigned long baud) { (void)baud; }
    void end() {}
    void setTimeout(unsigned long ms) { (void)ms; }
    explicit operator bool() const { return true; }

    int available();
    int read();
    int peek();

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* data, size_t len) override;
    void flush() override;
};

extern HardwareSerial Serial;
