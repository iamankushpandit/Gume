// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include "WString.h"

/* No SD card on the host board, so no file is ever open. ContentLoader and
 * Board::drawBmp() both test for that and take the built-in content. */

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {
class File {
public:
    explicit operator bool() const { return false; }
    int available() { return 0; }
    int read() { return -1; }
    size_t read(uint8_t*, size_t) { return 0; }
    size_t readBytes(char*, size_t) { return 0; }
    bool seek(uint32_t) { return false; }
    size_t size() const { return 0; }
    size_t position() const { return 0; }
    void close() {}
    const char* name() const { return ""; }
    bool isDirectory() const { return false; }
    File openNextFile() { return File(); }
    size_t write(const uint8_t*, size_t) { return 0; }
    int peek() { return -1; }
};

class FS {
public:
    File open(const char*, const char* = FILE_READ) { return File(); }
    File open(const String&, const char* = FILE_READ) { return File(); }
    bool exists(const char*) { return false; }
    bool exists(const String&) { return false; }
    bool mkdir(const char*) { return false; }
    bool remove(const char*) { return false; }
};
}   // namespace fs

using fs::File;
using fs::FS;
