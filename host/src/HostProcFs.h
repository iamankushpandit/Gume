// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Small readers for /proc and /sys, shared by the battery diagnostic's files.
 * Each answers false or empty where the file is missing, so a kernel that
 * lacks something reports less rather than failing. */

#include <dirent.h>
#include <stdio.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace HostProcFs {

/* The first line of a file, without its line ending. */
inline bool readLine(const std::string& path, char* out, size_t size) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    const bool ok = std::fgets(out, static_cast<int>(size), f) != nullptr;
    std::fclose(f);
    if (ok) out[std::strcspn(out, "\r\n")] = '\0';
    return ok;
}

/* A directory's entries, sorted, without the dot entries. */
inline std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> names;
    if (DIR* dir = ::opendir(path.c_str())) {
        while (dirent* e = ::readdir(dir)) {
            if (e->d_name[0] != '.') names.push_back(e->d_name);
        }
        ::closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}

/* One short line of output from a command, or empty. */
inline std::string commandLine(const char* command) {
    std::string out;
    if (FILE* p = ::popen(command, "r")) {
        char line[128];
        if (std::fgets(line, sizeof(line), p)) {
            line[std::strcspn(line, "\r\n")] = '\0';
            out = line;
        }
        ::pclose(p);
    }
    return out;
}

}   // namespace HostProcFs
