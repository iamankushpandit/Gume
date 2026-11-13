// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* Which console this is, and what is on its case.
 *
 * One card image serves a family of consoles that share a processor and
 * differ in what surrounds it: how many sticks, mostly. The kernel already
 * knows which one it booted on -- the device tree says so -- and each console
 * the image supports has a profile, a few lines of text in host/consoles/
 * that the image installs under /etc/braino/consoles/. This reads the one and
 * finds the other. HostControls.h then decides what each control is for.
 *
 * It is the handheld's counterpart of a board profile under include/boards/,
 * with one difference that matters: a board profile is chosen when the
 * firmware is compiled, and this is chosen when the console is switched on,
 * because the same image is on every card.
 *
 * Nothing here identifies a console beyond its model. The device tree's
 * `compatible` is the same on every unit of that model ever made, which is
 * what makes it fit to print in a log that may be pasted into an issue. */
namespace HostConsole {

struct Profile {
    std::string name;                   // "Anbernic RG40XX V"
    std::vector<std::string> match;     // device-tree compatible strings
    uint32_t controls = 0;              // HostControls::bit() of each control on the case
};

enum class Source {
    Computer,   // no device tree, or a package that ships no profiles: every control
    Matched,    // this console's own profile
    Family,     // only its processor family's: the buttons the family shares, no stick
    Unknown,    // profiles exist and none fits: the least a console can have
};

/* Parse one profile. False, with `error` saying why, if it names a control
 * that does not exist, has a line that is not `key = value`, or lacks a name,
 * a match or any controls. A profile that is wrong must fail loudly: a typo
 * that quietly dropped a button would look like a console with fewer
 * buttons. */
bool parse(const std::string& text, Profile& out, std::string& error);

/* Choose among `profiles` for a console whose device tree lists
 * `compatible`, most specific first. The first of the console's strings that
 * any profile matches wins, as the kernel itself reads that list. Returns the
 * index, or -1. */
int choose(const std::vector<Profile>& profiles, const std::vector<std::string>& compatible);

/* The NUL-separated strings of /proc/device-tree/compatible. */
std::vector<std::string> splitCompatible(const std::string& blob);

/* Find this console's profile, once, and say which in the log.
 *   BRAINO_CONSOLE_PROFILE  a profile file to use outright, for trying one
 *                           console's controls on another or on a PC
 *   BRAINO_CONSOLE_DIR      where profiles are (default /etc/braino/consoles)
 *   BRAINO_DT_COMPATIBLE    the file to read in place of
 *                           /proc/device-tree/compatible */
const Profile& detect();
Source source();

}   // namespace HostConsole
