// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* Finding this console's profile on the card. See HostConsole.h. */

#include "HostConsole.h"

#include "HostControls.h"

#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace HostConsole {
namespace {

Profile chosen;
Source chosenFrom = Source::Computer;
bool detected = false;

bool readFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

/* A PC, or a package that brought no profiles: a gamepad with everything. */
Profile everything() {
    Profile p;
    p.name = "This computer";
    p.controls = HostControls::ALL;
    return p;
}

/* Profiles are on the card and none is for this console. The d-pad, a button
 * to press with and one to go Home are the least any console here has, and
 * with those every screen is still reached through the selector. */
Profile least() {
    using namespace HostControls;
    Profile p;
    p.name = "Unknown console";
    p.controls = bit(DPad) | bit(A) | bit(B);
    return p;
}

std::vector<Profile> loadAll(const std::string& dir) {
    std::vector<std::string> files;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            const size_t len = std::strlen(e->d_name);
            if (len > 5 && std::strcmp(e->d_name + len - 5, ".conf") == 0) files.push_back(e->d_name);
        }
        closedir(d);
    }
    /* readdir's order is the filesystem's. Sorted, the same card always
     * chooses the same profile should two ever claim one console. */
    std::sort(files.begin(), files.end());
    std::vector<Profile> profiles;
    for (const std::string& name : files) {
        std::string text, error;
        Profile p;
        if (!readFile(dir + "/" + name, text)) continue;
        if (!parse(text, p, error)) {
            std::printf("[console] %s is not used: %s\n", name.c_str(), error.c_str());
            continue;
        }
        profiles.push_back(p);
    }
    return profiles;
}

void decide() {
    if (const char* forced = std::getenv("BRAINO_CONSOLE_PROFILE")) {
        std::string text, error;
        if (readFile(forced, text) && parse(text, chosen, error)) {
            chosenFrom = Source::Matched;
            std::printf("[console] %s, from %s\n", chosen.name.c_str(), forced);
            return;
        }
        std::printf("[console] %s is not used: %s\n", forced,
                    error.empty() ? "it cannot be read" : error.c_str());
    }

    const char* dtEnv = std::getenv("BRAINO_DT_COMPATIBLE");
    const char* dirEnv = std::getenv("BRAINO_CONSOLE_DIR");
    std::string blob;
    const bool hasTree = readFile(dtEnv ? dtEnv : "/proc/device-tree/compatible", blob);
    const std::vector<Profile> profiles = loadAll(dirEnv ? dirEnv : "/etc/braino/consoles");
    if (!hasTree || profiles.empty()) {
        chosen = everything();
        chosenFrom = Source::Computer;
        return;
    }

    const std::vector<std::string> compatible = splitCompatible(blob);
    const std::string model = compatible.empty() ? std::string("?") : compatible.front();
    const int index = choose(profiles, compatible);
    if (index < 0) {
        chosen = least();
        chosenFrom = Source::Unknown;
        std::printf("[console] no profile for \"%s\": only the d-pad, A and B are used\n",
                    model.c_str());
        return;
    }
    chosen = profiles[static_cast<size_t>(index)];
    const std::vector<std::string>& m = chosen.match;
    const bool own = std::find(m.begin(), m.end(), model) != m.end();
    chosenFrom = own ? Source::Matched : Source::Family;
    if (own) {
        std::printf("[console] %s (%s)\n", chosen.name.c_str(), model.c_str());
    } else {
        std::printf("[console] no profile for \"%s\": using %s\n", model.c_str(), chosen.name.c_str());
    }
}

}   // namespace

const Profile& detect() {
    if (!detected) {
        detected = true;
        decide();
    }
    return chosen;
}

Source source() {
    detect();
    return chosenFrom;
}

}   // namespace HostConsole
