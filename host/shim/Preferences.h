// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Arduino-ESP32's Preferences, over the same store as nvs.h, with the same
 * type mapping the real class uses (Bool and UChar are U8, Long is I32, and
 * so on). Writes go through at once, as on the device; the store saves itself
 * to a file in the host's data directory. */

#include <cstddef>
#include <cstdint>
#include "WString.h"

typedef enum {
    PT_I8, PT_U8, PT_I16, PT_U16, PT_I32, PT_U32, PT_I64, PT_U64, PT_STR, PT_BLOB, PT_INVALID
} PreferenceType;

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false, const char* partition = nullptr);
    void end();
    bool clear();
    bool remove(const char* key);
    bool isKey(const char* key);
    PreferenceType getType(const char* key);
    size_t freeEntries();

    size_t putChar(const char* key, int8_t value);
    size_t putUChar(const char* key, uint8_t value);
    size_t putShort(const char* key, int16_t value);
    size_t putUShort(const char* key, uint16_t value);
    size_t putInt(const char* key, int32_t value);
    size_t putUInt(const char* key, uint32_t value);
    size_t putLong(const char* key, int32_t value);
    size_t putULong(const char* key, uint32_t value);
    size_t putLong64(const char* key, int64_t value);
    size_t putULong64(const char* key, uint64_t value);
    size_t putBool(const char* key, bool value);
    size_t putString(const char* key, const char* value);
    size_t putString(const char* key, const String& value) { return putString(key, value.c_str()); }
    size_t putBytes(const char* key, const void* value, size_t len);

    int8_t getChar(const char* key, int8_t fallback = 0);
    uint8_t getUChar(const char* key, uint8_t fallback = 0);
    int16_t getShort(const char* key, int16_t fallback = 0);
    uint16_t getUShort(const char* key, uint16_t fallback = 0);
    int32_t getInt(const char* key, int32_t fallback = 0);
    uint32_t getUInt(const char* key, uint32_t fallback = 0);
    int32_t getLong(const char* key, int32_t fallback = 0);
    uint32_t getULong(const char* key, uint32_t fallback = 0);
    int64_t getLong64(const char* key, int64_t fallback = 0);
    uint64_t getULong64(const char* key, uint64_t fallback = 0);
    bool getBool(const char* key, bool fallback = false);
    String getString(const char* key, const String& fallback = String());
    size_t getString(const char* key, char* out, size_t maxLen);
    size_t getBytesLength(const char* key);
    size_t getBytes(const char* key, void* out, size_t maxLen);

private:
    uint32_t handle_ = 0;
    bool started_ = false;
    bool readOnly_ = false;
};
