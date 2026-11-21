// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The two system calls an Android app may not make, answered the way a
 * desktop answers them.
 *
 * Android runs every app under a seccomp filter, and a call it forbids is not
 * refused -- the kernel kills the process with SIGSYS. Braino is the device's
 * home activity, so Android starts it again at once, it makes the same call,
 * and the device sits in a loop of boot greetings. Two calls the firmware and
 * the host make are on that list:
 *
 *   settimeofday()  Board::ntpUdpProbe() sets the clock from an NTP reply,
 *                   as a board must. It ran the first time a saved Wi-Fi
 *                   network let the clock sync, and every time after.
 *   mount()         host/src/HostSleep.cpp mounts debugfs to reach the
 *                   Allwinner display's debug interface when the panel
 *                   sleeps -- the RG40XX V's, which the Portal does not have.
 *
 * A desktop Linux program that is not root gets -1 and EPERM from both, and
 * the firmware already carries on from that: the clock it reads is the
 * system's, which the system keeps right. So the Android build links both
 * names to these (CMakeLists.txt: -Wl,--wrap=...), which give that same
 * answer, and the call never reaches the kernel. Android keeps the Portal's
 * clock synced itself.
 *
 * A call added to src/ or host/ that Android also forbids would end the same
 * way; adb logcat -b crash names it ("seccomp prevented call to disallowed
 * arm64 system call N"), and it belongs here beside these. */

#include <cerrno>
#include <cstdio>

#include <sys/time.h>

extern "C" {

int __wrap_settimeofday(const struct timeval*, const struct timezone*) {
    static bool said = false;
    if (!said) {
        std::printf("[time] Android keeps the clock: Braino reads it, and does not set it\n");
        said = true;
    }
    errno = EPERM;
    return -1;
}

int __wrap_mount(const char*, const char*, const char*, unsigned long, const void*) {
    errno = EPERM;
    return -1;
}

}   // extern "C"
