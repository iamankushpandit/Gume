// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* See HostScreenPower.h. */

#include "HostScreenPower.h"

#if defined(GUME_HOST_SCREEN_POWER)

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "HostPlatform.h"

namespace HostScreenPower {
namespace {

/* The Allwinner display engine's ioctls (sunxi_display2.h). Each takes an
 * array of unsigned long whose first entry is the screen, and the brightness
 * calls carry the level, 0-255, in the second. */
constexpr unsigned long DISP_LCD_SET_BRIGHTNESS = 0x102;
constexpr unsigned long DISP_LCD_GET_BRIGHTNESS = 0x103;
constexpr unsigned long DISP_LCD_BACKLIGHT_ENABLE = 0x104;
constexpr unsigned long DISP_LCD_BACKLIGHT_DISABLE = 0x105;
constexpr const char* DISP_DEVICE = "/dev/disp";
/* FB_BLANK_POWERDOWN and FB_BLANK_UNBLANK, as bl_power takes them. */
constexpr const char* BL_POWER_OFF = "4";
constexpr const char* BL_POWER_ON = "0";

enum Method : int { None = 0, Disp = 1, Sysfs = 2 };

/* Everything restoreLamp() reads is set up before the lamp goes out, in
 * fixed buffers, so that a signal handler can call it: it does nothing but
 * open, write, ioctl, close and unlink, which are all async-signal-safe. */
volatile std::sig_atomic_t lampOff = 0;
int method = None;
unsigned long savedLevel = 0;
char savedLevelText[16] = {0};
char sysBrightness[256] = {0};
char sysPower[256] = {0};
char restorePath[512] = {0};

bool writeText(const char* path, const char* text) {
    const int fd = ::open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) return false;
    const ssize_t n = ::write(fd, text, std::strlen(text));
    ::close(fd);
    return n == static_cast<ssize_t>(std::strlen(text));
}

bool readLong(const char* path, unsigned long& out) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    const bool ok = std::fscanf(f, "%lu", &out) == 1;
    std::fclose(f);
    return ok;
}

bool dispCall(unsigned long cmd, unsigned long level, long* result = nullptr) {
    const int fd = ::open(DISP_DEVICE, O_RDWR);
    if (fd < 0) return false;
    unsigned long args[4] = {0, level, 0, 0};
    const long r = ::ioctl(fd, cmd, args);
    ::close(fd);
    if (result) *result = r;
    return r >= 0;
}

void restoreLamp() {
    if (!lampOff) return;
    if (method == Disp) {
        dispCall(DISP_LCD_BACKLIGHT_ENABLE, 0);
        dispCall(DISP_LCD_SET_BRIGHTNESS, savedLevel);
    } else if (method == Sysfs) {
        writeText(sysBrightness, savedLevelText);
        if (sysPower[0]) writeText(sysPower, BL_POWER_ON);
    }
    if (restorePath[0]) ::unlink(restorePath);
    lampOff = 0;
}

void onFatalSignal(int sig) {
    restoreLamp();
    /* SA_RESETHAND has already put the default action back. */
    std::raise(sig);
}

/* The first backlight class device that reports a brightness. */
bool findSysfsBacklight() {
    DIR* dir = ::opendir("/sys/class/backlight");
    if (!dir) return false;
    bool found = false;
    while (dirent* e = ::readdir(dir)) {
        if (e->d_name[0] == '.') continue;
        const std::string base = std::string("/sys/class/backlight/") + e->d_name;
        unsigned long level = 0;
        if (!readLong((base + "/brightness").c_str(), level)) continue;
        std::snprintf(sysBrightness, sizeof(sysBrightness), "%s/brightness", base.c_str());
        std::snprintf(sysPower, sizeof(sysPower), "%s/bl_power", base.c_str());
        if (::access(sysPower, W_OK) != 0) sysPower[0] = 0;
        found = true;
        break;
    }
    ::closedir(dir);
    return found;
}

unsigned long currentLevel() {
    if (method == Disp) {
        long r = 0;
        return dispCall(DISP_LCD_GET_BRIGHTNESS, 0, &r) ? static_cast<unsigned long>(r) : 0;
    }
    unsigned long level = 0;
    if (method == Sysfs) readLong(sysBrightness, level);
    return level;
}

/* What the next start needs if this run dies with the lamp off. */
void writeRestoreFile() {
    const int fd = ::open(restorePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    char line[32];
    const int n = std::snprintf(line, sizeof(line), "%d %lu\n", method, savedLevel);
    if (n > 0 && ::write(fd, line, static_cast<size_t>(n)) == n) ::fsync(fd);
    ::close(fd);
}

}   // namespace

void begin() {
    std::snprintf(restorePath, sizeof(restorePath), "%s/backlight-off",
                  HostPlatform::dataDir().c_str());
    long level = 0;
    if (dispCall(DISP_LCD_GET_BRIGHTNESS, 0, &level)) {
        method = Disp;
    } else if (findSysfsBacklight()) {
        method = Sysfs;
    }

    /* A previous run went to sleep and never woke: SIGKILL, a power
     * button held through it, a crash the handlers could not catch. */
    FILE* f = std::fopen(restorePath, "r");
    if (f) {
        int was = None;
        unsigned long wasLevel = 0;
        const bool ok = std::fscanf(f, "%d %lu", &was, &wasLevel) == 2;
        std::fclose(f);
        if (ok && was == method && method != None) {
            savedLevel = wasLevel;
            std::snprintf(savedLevelText, sizeof(savedLevelText), "%lu", savedLevel);
            lampOff = 1;
            restoreLamp();
            std::printf("[display] host: backlight left off by the last run, restored to %lu\n",
                        savedLevel);
        } else {
            ::unlink(restorePath);
        }
    }

    if (method == Disp) {
        std::printf("[display] host: backlight through %s (level %lu)\n", DISP_DEVICE,
                    currentLevel());
    } else if (method == Sysfs) {
        std::printf("[display] host: backlight through %s (level %lu)\n", sysBrightness,
                    currentLevel());
    } else {
        std::printf("[display] host: no backlight control found; "
                    "the screen will go black in sleep but stay lit\n");
        return;
    }

    /* SIGINT and SIGTERM are SDL's: it turns them into a quit, and the way
     * out calls restore(). These are the ones that end the program without
     * passing through it. */
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onFatalSignal;
    sa.sa_flags = SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGHUP, SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) {
        ::sigaction(sig, &sa, nullptr);
    }
}

void setAwake(bool awake) {
    if (method == None) return;
    if (awake) {
        if (!lampOff) return;
        restoreLamp();
        std::printf("[display] host: backlight on (level %lu)\n", savedLevel);
        return;
    }
    if (lampOff) return;
    savedLevel = currentLevel();
    std::snprintf(savedLevelText, sizeof(savedLevelText), "%lu", savedLevel);
    writeRestoreFile();
    lampOff = 1;
    bool ok = false;
    if (method == Disp) {
        ok = dispCall(DISP_LCD_SET_BRIGHTNESS, 0);
        ok = dispCall(DISP_LCD_BACKLIGHT_DISABLE, 0) || ok;
    } else {
        ok = writeText(sysBrightness, "0");
        if (sysPower[0]) ok = writeText(sysPower, BL_POWER_OFF) || ok;
    }
    std::printf("[display] host: backlight off (was level %lu)%s\n", savedLevel,
                ok ? "" : " -- the driver refused");
}

void restore() { restoreLamp(); }

}   // namespace HostScreenPower

#else

namespace HostScreenPower {
void begin() {}
void setAwake(bool) {}
void restore() {}
}   // namespace HostScreenPower

#endif
