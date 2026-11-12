// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* What each control is for, on consoles with every control, with fewer and
 * with more -- and reading the profile that says which a console is.
 *
 * Built against host/src/HostControls.h (pure, header-only) and
 * host/src/HostConsole.cpp (parsing and choosing; no file is opened). The
 * shipped profiles under host/consoles/ are read too, so a typo in one fails
 * here rather than on a console:
 *
 *   g++ -std=c++17 -O2 -Wall -Wextra -Ihost/src test/host/host_controls_test.cpp host/src/HostConsole.cpp -o controls_test && ./controls_test
 *
 * Run it from the top of the repository: it opens host/consoles/. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "HostConsole.h"
#include "HostControls.h"

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond);       \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

using namespace HostControls;

namespace {

uint32_t bits(std::initializer_list<Control> list) {
    uint32_t out = 0;
    for (Control c : list) out |= bit(c);
    return out;
}

const uint32_t BUTTONS = bits({DPad, A, B, X, Y, L1, R1, L2, R2, Select, Start, Menu});

std::string slurp(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::printf("FAILED: cannot open %s (run from the top of the repository)\n", path);
        std::exit(1);
    }
    std::string out;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

/* No control ever does two jobs, and every control the console has either
 * has a job or is reported as having none. */
void checkConsistent(uint32_t present, const Assignment& a) {
    uint32_t used = 0;
    for (uint8_t t = 0; t < TaskCount; ++t) {
        if (a.control[t] == ControlCount) continue;
        const uint32_t b = bit(static_cast<Control>(a.control[t]));
        CHECK((present & b) != 0);      // never a control the console lacks
        CHECK((used & b) == 0);         // never the same control twice
        used |= b;
    }
    CHECK((used | a.unassigned) == (present & ALL));
    CHECK((used & a.unassigned) == 0);
}

void everyControl() {
    const Assignment a = assign(ALL);
    checkConsistent(ALL, a);
    CHECK(a.is(Move, DPad));
    CHECK(a.is(Press, A));
    CHECK(a.is(Home, B));
    CHECK(a.is(PagePrev, L1) && a.is(PageNext, R1));
    CHECK(a.is(Tab, Menu));
    CHECK(a.is(Lock, R2) && a.is(Mute, L2));
    CHECK(a.is(Cursor, LStick) && a.is(CursorPress, L3));
    CHECK(a.is(AskBattery, RStick));
    CHECK(a.is(RestartA, Select) && a.is(RestartB, Start));
    /* More controls than jobs: the spare ones do nothing. */
    CHECK(a.unassigned == bits({X, Y, R3}));
    CHECK(a.taskOf(X) == TaskCount);
    CHECK(a.taskOf(B) == Home);
}

/* The buttons of the family and no stick: the crosshair has nowhere to go,
 * and that is the only thing lost. */
void noStick() {
    const Assignment a = assign(BUTTONS);
    checkConsistent(BUTTONS, a);
    CHECK(!a.has(Cursor) && !a.has(CursorPress) && !a.has(AskBattery));
    CHECK(a.is(Move, DPad) && a.is(Press, A) && a.is(Home, B));
    CHECK(a.is(PagePrev, L1) && a.is(Lock, R2) && a.is(Tab, Menu));
    CHECK(a.unassigned == bits({X, Y}));
}

/* One stick and no way to click it. */
void oneStick() {
    const uint32_t present = BUTTONS | bit(LStick);
    const Assignment a = assign(present);
    checkConsistent(present, a);
    CHECK(a.is(Cursor, LStick));
    CHECK(!a.has(CursorPress) && !a.has(AskBattery));
}

/* Fewer buttons, most missed first: each job moves to the next button that
 * can do it, and the jobs that are missed least are the ones that go. */
void fewerButtons() {
    /* No shoulder buttons: the triggers turn the pages, and Lock and Mute,
     * which wanted the triggers, take the two spare face buttons. */
    uint32_t present = bits({DPad, A, B, X, Y, L2, R2, Select, Start, Menu});
    Assignment a = assign(present);
    checkConsistent(present, a);
    CHECK(a.is(PagePrev, L2) && a.is(PageNext, R2));
    CHECK(a.is(Lock, X) && a.is(Mute, Y));
    CHECK(a.unassigned == 0);

    /* No shoulders and no triggers: the face buttons turn the pages, and
     * Lock and Mute are left to the selector. */
    present = bits({DPad, A, B, X, Y, Select, Start, Menu});
    a = assign(present);
    checkConsistent(present, a);
    CHECK(a.is(PagePrev, Y) && a.is(PageNext, X));
    CHECK(!a.has(Lock) && !a.has(Mute));

    /* A page button with no partner is not half a pager. */
    present = bits({DPad, A, B, L1});
    a = assign(present);
    checkConsistent(present, a);
    CHECK(!a.has(PagePrev) && !a.has(PageNext));
    CHECK(a.unassigned == bit(L1));

    /* No small button: Start is the tab key, so the restart hold, which
     * needs Start as well, is gone rather than doubled up. */
    present = bits({DPad, A, B, Select, Start});
    a = assign(present);
    checkConsistent(present, a);
    CHECK(a.is(Tab, Start));
    CHECK(!a.has(RestartA) && !a.has(RestartB));
    CHECK(a.unassigned == bit(Select));

    /* No A: B presses, and Home moves along to Start. */
    present = bits({DPad, B, Start, Select});
    a = assign(present);
    checkConsistent(present, a);
    CHECK(a.is(Press, B) && a.is(Home, Start) && a.is(Tab, Select));

    /* The least a console can have. Everything else is through the
     * selector, which is why this is enough. */
    present = bits({DPad, A});
    a = assign(present);
    checkConsistent(present, a);
    CHECK(a.is(Move, DPad) && a.is(Press, A));
    CHECK(!a.has(Home) && a.unassigned == 0);

    /* Only a right stick: it is the crosshair, and its own click presses. */
    present = bits({A, RStick, R3});
    a = assign(present);
    checkConsistent(present, a);
    CHECK(a.is(Cursor, RStick) && a.is(CursorPress, R3));
    CHECK(!a.has(Move) && !a.has(AskBattery));
}

/* Every subset of the controls, for the two properties that must hold
 * whatever a console turns out to have. */
void everySubset() {
    for (uint32_t present = 0; present <= ALL; ++present) {
        const Assignment a = assign(present);
        checkConsistent(present, a);
        /* The priority is real: a job is never left without a control while
         * its first choice sits idle. */
        if (present & bit(DPad)) CHECK(a.is(Move, DPad));
        if (present & bit(A)) CHECK(a.is(Press, A));
        if ((present & bit(LStick))) CHECK(a.is(Cursor, LStick));
        CHECK(a.has(PagePrev) == a.has(PageNext));
        CHECK(a.has(RestartA) == a.has(RestartB));
    }
}

void profiles() {
    HostConsole::Profile p;
    std::string error;

    CHECK(HostConsole::parse("# a comment\nname = Test  \nmatch = a,b\n\ncontrols = dpad a  b # tail\n",
                             p, error));
    CHECK(p.name == "Test");
    CHECK(p.match.size() == 1 && p.match[0] == "a,b");
    CHECK(p.controls == bits({DPad, A, B}));

    /* Windows line endings, as a profile edited on a PC has. */
    CHECK(HostConsole::parse("name = T\r\nmatch = m\r\ncontrols = dpad a\r\n", p, error));
    CHECK(p.controls == bits({DPad, A}));

    /* A typo must fail, not quietly drop a button. */
    CHECK(!HostConsole::parse("name = T\nmatch = m\ncontrols = dpad aa\n", p, error));
    CHECK(error.find("aa") != std::string::npos);
    CHECK(!HostConsole::parse("name = T\nmatch = m\ncontrol = dpad a\n", p, error));
    CHECK(!HostConsole::parse("name = T\nmatch = m\ncontrols dpad\n", p, error));
    CHECK(!HostConsole::parse("match = m\ncontrols = dpad a\n", p, error));
    CHECK(!HostConsole::parse("name = T\ncontrols = dpad a\n", p, error));
    CHECK(!HostConsole::parse("name = T\nmatch = m\n", p, error));

    /* The device tree's list, most specific first. */
    const std::string blob("anbernic,rg40xx-V\0allwinner,sun50i-h700\0", 41);
    const std::vector<std::string> compatible = HostConsole::splitCompatible(blob);
    CHECK(compatible.size() == 2);
    CHECK(compatible[0] == "anbernic,rg40xx-V" && compatible[1] == "allwinner,sun50i-h700");

    /* The profiles the image ships. */
    HostConsole::Profile v, family;
    CHECK(HostConsole::parse(slurp("host/consoles/anbernic-rg40xx-v.conf"), v, error));
    CHECK(HostConsole::parse(slurp("host/consoles/allwinner-h700.conf"), family, error));
    CHECK(v.controls == ALL);
    CHECK(family.controls == BUTTONS);

    /* The console's own profile wins whichever order the files come in, and
     * a console the image has not been taught gets the family's. */
    CHECK(HostConsole::choose({family, v}, compatible) == 1);
    CHECK(HostConsole::choose({v, family}, compatible) == 0);
    CHECK(HostConsole::choose({v, family}, {"anbernic,rg35xx-plus", "allwinner,sun50i-h700"}) == 1);
    CHECK(HostConsole::choose({v, family}, {"some,other-console"}) == -1);
    /* The match is exact: the device tree spells this one with a capital. */
    CHECK(HostConsole::choose({v}, {"anbernic,rg40xx-v"}) == -1);
}

}   // namespace

int main() {
    everyControl();
    noStick();
    oneStick();
    fewerButtons();
    everySubset();
    profiles();
    std::printf("host controls: all checks passed\n");
    return 0;
}
