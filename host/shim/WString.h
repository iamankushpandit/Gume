// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Arduino's String, on std::string. The firmware keeps String out of anything
 * that runs per frame (CLAUDE.md, memory rule 2), so this is about meaning,
 * not speed: the same conversions, the same "-1 when not found", the same
 * truncating substring(). */

#include <cstdint>
#include <cstdlib>
#include <string>

class String {
public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    String(const char* s, unsigned int len) : s_(s ? std::string(s, len) : std::string()) {}
    String(const std::string& s) : s_(s) {}
    String(char c) : s_(1, c) {}
    String(unsigned char value, unsigned char base = 10);
    String(int value, unsigned char base = 10);
    String(unsigned int value, unsigned char base = 10);
    String(long value, unsigned char base = 10);
    String(unsigned long value, unsigned char base = 10);
    String(long long value, unsigned char base = 10);
    String(unsigned long long value, unsigned char base = 10);
    String(float value, unsigned int decimals = 2);
    String(double value, unsigned int decimals = 2);

    const char* c_str() const { return s_.c_str(); }
    unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
    bool isEmpty() const { return s_.empty(); }
    void reserve(unsigned int size) { s_.reserve(size); }
    char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : '\0'; }
    void setCharAt(unsigned int i, char c) {
        if (i < s_.size()) s_[i] = c;
    }
    char operator[](unsigned int i) const { return charAt(i); }
    char& operator[](unsigned int i) { return s_[i]; }

    String& operator=(const char* s) {
        s_ = s ? s : "";
        return *this;
    }

    bool concat(const String& o) { s_ += o.s_; return true; }
    bool concat(const char* s) { if (s) s_ += s; return true; }
    bool concat(char c) { s_ += c; return true; }
    template <typename T>
    bool concat(T value) { return concat(String(value)); }

    String& operator+=(const String& o) { concat(o); return *this; }
    String& operator+=(const char* s) { concat(s); return *this; }
    String& operator+=(char c) { concat(c); return *this; }
    template <typename T>
    String& operator+=(T value) { concat(String(value)); return *this; }

    bool equals(const String& o) const { return s_ == o.s_; }
    bool equals(const char* s) const { return s_ == (s ? s : ""); }
    bool equalsIgnoreCase(const String& o) const;
    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator==(const char* s) const { return equals(s); }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator!=(const char* s) const { return !equals(s); }
    bool operator<(const String& o) const { return s_ < o.s_; }
    int compareTo(const String& o) const { return s_.compare(o.s_); }

    bool startsWith(const String& prefix) const { return s_.rfind(prefix.s_, 0) == 0; }
    bool endsWith(const String& suffix) const {
        return s_.size() >= suffix.s_.size()
            && s_.compare(s_.size() - suffix.s_.size(), suffix.s_.size(), suffix.s_) == 0;
    }

    int indexOf(char c, unsigned int from = 0) const {
        const auto at = s_.find(c, from);
        return at == std::string::npos ? -1 : static_cast<int>(at);
    }
    int indexOf(const String& sub, unsigned int from = 0) const {
        const auto at = s_.find(sub.s_, from);
        return at == std::string::npos ? -1 : static_cast<int>(at);
    }
    int lastIndexOf(char c) const {
        const auto at = s_.rfind(c);
        return at == std::string::npos ? -1 : static_cast<int>(at);
    }
    String substring(unsigned int from) const {
        return from >= s_.size() ? String() : String(s_.substr(from));
    }
    String substring(unsigned int from, unsigned int to) const;

    void trim();
    void toUpperCase();
    void toLowerCase();
    void replace(char find, char with);
    void replace(const String& find, const String& with);
    void remove(unsigned int index);
    void remove(unsigned int index, unsigned int count);

    long toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }
    float toFloat() const { return std::strtof(s_.c_str(), nullptr); }
    double toDouble() const { return std::strtod(s_.c_str(), nullptr); }

    void toCharArray(char* buf, unsigned int size, unsigned int index = 0) const;
    void getBytes(unsigned char* buf, unsigned int size, unsigned int index = 0) const {
        toCharArray(reinterpret_cast<char*>(buf), size, index);
    }

    const std::string& str() const { return s_; }

private:
    std::string s_;
};

inline String operator+(const String& a, const String& b) {
    String out(a);
    out += b;
    return out;
}
inline String operator+(const String& a, const char* b) {
    String out(a);
    out += b;
    return out;
}
inline String operator+(const char* a, const String& b) {
    String out(a);
    out += b;
    return out;
}
inline String operator+(const String& a, char b) {
    String out(a);
    out += b;
    return out;
}
template <typename T>
inline String operator+(const String& a, T b) {
    String out(a);
    out += String(b);
    return out;
}
inline bool operator==(const char* a, const String& b) { return b == a; }
