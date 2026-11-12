// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* Reading a console profile and choosing one: the half of HostConsole.h that
 * touches no file, so test/host/host_controls_test.cpp builds it anywhere.
 * Finding the files is HostConsoleDetect.cpp. */

#include "HostConsole.h"

#include "HostControls.h"

namespace HostConsole {
namespace {

std::string trimmed(const std::string& s) {
    const char* blank = " \t\r";
    const size_t first = s.find_first_not_of(blank);
    if (first == std::string::npos) return std::string();
    return s.substr(first, s.find_last_not_of(blank) - first + 1);
}

bool readControls(const std::string& value, uint32_t& controls, std::string& error) {
    size_t at = 0;
    while (at < value.size()) {
        const size_t start = value.find_first_not_of(" \t", at);
        if (start == std::string::npos) break;
        size_t end = value.find_first_of(" \t", start);
        if (end == std::string::npos) end = value.size();
        HostControls::Control c;
        if (!HostControls::parseControl(value.data() + start, end - start, c)) {
            error = "no control is called \"" + value.substr(start, end - start) + "\"";
            return false;
        }
        controls |= HostControls::bit(c);
        at = end;
    }
    return true;
}

}   // namespace

bool parse(const std::string& text, Profile& out, std::string& error) {
    out = Profile();
    size_t at = 0;
    int lineNo = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        ++lineNo;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = trimmed(line);
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = "line " + std::to_string(lineNo) + " is not \"key = value\"";
            return false;
        }
        const std::string key = trimmed(line.substr(0, eq));
        const std::string value = trimmed(line.substr(eq + 1));
        if (key == "name") {
            out.name = value;
        } else if (key == "match") {
            if (!value.empty()) out.match.push_back(value);
        } else if (key == "controls") {
            if (!readControls(value, out.controls, error)) {
                error = "line " + std::to_string(lineNo) + ": " + error;
                return false;
            }
        } else {
            error = "line " + std::to_string(lineNo) + ": no setting is called \"" + key + "\"";
            return false;
        }
    }
    if (out.name.empty()) { error = "it has no name"; return false; }
    if (out.match.empty()) { error = "it has no match"; return false; }
    if (out.controls == 0) { error = "it lists no controls"; return false; }
    return true;
}

int choose(const std::vector<Profile>& profiles, const std::vector<std::string>& compatible) {
    for (const std::string& wanted : compatible) {
        for (size_t i = 0; i < profiles.size(); ++i) {
            for (const std::string& m : profiles[i].match) {
                if (m == wanted) return static_cast<int>(i);
            }
        }
    }
    return -1;
}

std::vector<std::string> splitCompatible(const std::string& blob) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < blob.size()) {
        size_t end = blob.find('\0', at);
        if (end == std::string::npos) end = blob.size();
        if (end > at) out.push_back(blob.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

}   // namespace HostConsole
