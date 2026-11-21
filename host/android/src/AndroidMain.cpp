// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* Where Android starts Braino: android_main(), on the thread
 * android_native_app_glue makes for it. It waits for the window, then runs
 * hostMain() -- the same start-up every host build has (host/src/HostMain.h)
 * -- which runs the firmware's setup() and loop() until the process ends.
 *
 * Braino is the device's home activity, so the process ending is not the
 * end: the system starts it again, which is what ESP.restart() and a crash
 * both come to. */

#include "HostAndroid.h"
#include "HostMain.h"

#include <android/log.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <unistd.h>

#include <cstdio>
#include <thread>

namespace {

/* Everything the host writes to stdout and stderr -- every [host], [boot]
 * and [time] line -- goes to logcat under the tag "braino", where a
 * desktop build would print it to the terminal. Read it with
 *   adb logcat -s braino */
void logToLogcat() {
    static int fds[2];
    if (::pipe(fds) != 0) return;
    ::dup2(fds[1], STDOUT_FILENO);
    ::dup2(fds[1], STDERR_FILENO);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::thread([] {
        char line[1024];
        size_t used = 0;
        for (;;) {
            const ssize_t n = ::read(fds[0], line + used, sizeof(line) - 1 - used);
            if (n <= 0) return;
            used += static_cast<size_t>(n);
            size_t start = 0;
            for (size_t i = 0; i < used; ++i) {
                if (line[i] != '\n') continue;
                line[i] = 0;
                __android_log_write(ANDROID_LOG_INFO, "braino", line + start);
                start = i + 1;
            }
            if (start == 0 && used == sizeof(line) - 1) {   // a line longer than the buffer
                line[used] = 0;
                __android_log_write(ANDROID_LOG_INFO, "braino", line);
                used = 0;
            } else if (start > 0) {
                for (size_t i = start; i < used; ++i) line[i - start] = line[i];
                used -= start;
            }
        }
    }).detach();
}

}   // namespace

void android_main(android_app* app) {
    logToLogcat();
    HostAndroid::setApp(app);
    /* The firmware decides when the screen goes dark (its saver and panel
     * sleep, Settings -> Power), so the system's own screen timeout must not
     * decide it first; and the frame is the whole screen, with no status
     * bar over its top row. */
    ANativeActivity_setWindowFlags(app->activity,
                                   AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);
    /* Which way up the device is held, for Auto in Settings (DevicePose). */
    HostAndroidPose::begin();
    /* Nothing to draw on until the system hands over the window. */
    while (app->window == nullptr) {
        if (!HostAndroid::pump(-1)) return;
    }
    char name[] = "braino";
    char* argv[] = {name, nullptr};
    hostMain(1, argv);
}
