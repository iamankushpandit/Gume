// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* See HostLed.h.
 *
 * The RG40XXV's light MCU speaks raw serial at 115200 8N1, once its supply is
 * switched on through the PMIC's mcu_pwr attribute. Each frame is a mode, a
 * brightness, the mode's arguments and a checksum -- the low byte of the sum
 * of everything before it:
 *
 *   solid   1, bri, (R,G,B) x 8 right ring, (R,G,B) x 8 left ring, sum
 *   rainbow 6, bri, 1, 1, speed, sum                 (multi-colour)
 *
 * The solid frame sets every LED on its own, which is what makes the battery
 * gauge possible. The battery is polled from this thread too, so that neither
 * the frame loop nor a sleeping panel's slower one has any part in it.
 *
 * Writes happen on a thread of their own and only the newest wish is sent:
 * the firmware may change the colour every frame in the saver, and 51 bytes
 * at 115200 baud is 4.4 ms the frame loop should never wait for. */

#include "HostLed.h"

#include <Preferences.h>

#include "HostBattery.h"
#include "HostRingGauge.h"

#include <fcntl.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {

constexpr const char* SERIAL_PATH = "/dev/ttyS5";
constexpr const char* MCU_POWER = "/sys/class/power_supply/axp2202-battery/mcu_pwr";

/* The rainbow's look. BRAINO_LED_RAINBOW="brightness,speed" overrides both
 * (0-255 each) while the defaults are tuned by eye on the device. */
uint8_t rainbowBrightness = 200;
/* 128 looked hurried on the device; half of it was asked for. The MCU's
 * speed byte is taken as "higher is faster", the controller's usual sense --
 * muOS 2606 animates its own LEDs and never sends this mode, so nothing in
 * its source settles the direction. */
uint8_t rainbowSpeed = 64;
constexpr uint8_t SOLID_BRIGHTNESS = 255;

constexpr auto GAUGE_SHOW = std::chrono::milliseconds(10000);
constexpr auto GAUGE_FRAME = std::chrono::milliseconds(33);   // also how soon a change lands
constexpr auto IDLE_FRAME = std::chrono::milliseconds(250);   // asleep, nothing showing
constexpr auto BATTERY_POLL = std::chrono::milliseconds(1000);

std::mutex mutex;
uint8_t want[3] = {0, 0, 0};
bool awake = true;
bool dirty = true;
bool gaugeAsked = false;
std::once_flag started;
std::atomic<int> lit{-1};   // what the ring last showed; -1 until it is driven
int fd = -1;

bool openDevice() {
    if (::access(SERIAL_PATH, W_OK) != 0) return false;
    if (FILE* p = std::fopen(MCU_POWER, "w")) {
        std::fputs("1\n", p);
        std::fclose(p);
    }
    fd = ::open(SERIAL_PATH, O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return false;
    termios tio{};
    if (::tcgetattr(fd, &tio) == 0) {
        ::cfmakeraw(&tio);
        ::cfsetospeed(&tio, B115200);
        ::cfsetispeed(&tio, B115200);
        tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
        tio.c_cflag |= CS8 | CLOCAL;
        ::tcsetattr(fd, TCSANOW, &tio);
    }
    /* The MCU misses the first frame after its supply comes up. */
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
}

void send(const uint8_t* frame, size_t len) {
    uint8_t buf[64];
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        buf[i] = frame[i];
        sum = static_cast<uint8_t>(sum + frame[i]);
    }
    buf[len] = sum;
    if (::write(fd, buf, len + 1) < 0) return;
    ::tcdrain(fd);
}

void sendSolid(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t frame[2 + 48];
    frame[0] = 1;
    frame[1] = SOLID_BRIGHTNESS;
    for (int i = 0; i < 16; ++i) {
        frame[2 + i * 3] = r;
        frame[3 + i * 3] = g;
        frame[4 + i * 3] = b;
    }
    send(frame, sizeof(frame));
}

/* Both rings show the same gauge. */
void sendGauge(const HostRingGauge::Colour ring[HostRingGauge::LEDS]) {
    uint8_t frame[2 + 48];
    frame[0] = 1;
    frame[1] = SOLID_BRIGHTNESS;
    for (int i = 0; i < 2 * HostRingGauge::LEDS; ++i) {
        const HostRingGauge::Colour& c = ring[i % HostRingGauge::LEDS];
        frame[2 + i * 3] = c.r;
        frame[3 + i * 3] = c.g;
        frame[4 + i * 3] = c.b;
    }
    send(frame, sizeof(frame));
}

void sendRainbow() {
    const uint8_t frame[] = {6, rainbowBrightness, 1, 1, rainbowSpeed};
    send(frame, sizeof(frame));
}

/* Braino's own LED switch (Settings), read from the same store the firmware
 * writes it to. Read when the light changes, not per frame. */
bool ledEnabled() {
    Preferences prefs;
    if (!prefs.begin("cydkids", true)) return true;
    const bool on = prefs.getBool("rgbOn", true);
    prefs.end();
    return on;
}

void worker() {
    ::pthread_setname_np(::pthread_self(), "led-ring");
    if (!openDevice()) return;
    std::printf("[led] host: light ring on %s\n", SERIAL_PATH);
    std::fflush(stdout);
    lit = 0;
    using Clock = std::chrono::steady_clock;
    Clock::time_point gaugeStart{}, gaugeUntil{}, nextPoll{};
    HostRingGauge::Opening opening = HostRingGauge::Opening::None;
    HostBattery::Reading battery{-1, false, false};
    bool wasPlugged = false;
    bool gaugeShown = false;
    for (;;) {
        uint8_t r, g, b;
        bool isAwake, lightChanged, asked;
        {
            std::lock_guard<std::mutex> lock(mutex);
            lightChanged = dirty;
            dirty = false;
            asked = gaugeAsked;
            gaugeAsked = false;
            r = want[0];
            g = want[1];
            b = want[2];
            isAwake = awake;
        }

        const Clock::time_point now = Clock::now();
        if (now >= nextPoll) {
            nextPoll = now + BATTERY_POLL;
            battery = HostBattery::read();
            /* Held up from here for as long as the charger is in. The switch is
             * asked again each time, so it can still turn a held gauge off. */
            if (battery.plugged && battery.percent >= 0 && ledEnabled()) {
                gaugeUntil = now + GAUGE_SHOW;
            }
        }
        const bool cableMoved = battery.plugged != wasPlugged;
        wasPlugged = battery.plugged;
        if (cableMoved || asked) {
            /* Synced, because this is the line that says why the ring did or
             * did not answer, and a console is switched off by pulling power. */
            std::printf("[led] %s: battery %d%%, plugged %d, charging %d\n",
                        cableMoved ? "charger moved" : "stick", battery.percent,
                        battery.plugged ? 1 : 0, battery.charging ? 1 : 0);
            std::fflush(stdout);
            ::fsync(STDOUT_FILENO);
        }
        if ((cableMoved || asked) && battery.percent >= 0 && ledEnabled()) {
            /* Asking again while it is up only keeps it up: a wiggled stick
             * must not start the picture over. The cable moving does. */
            if (cableMoved || now >= gaugeUntil) {
                gaugeStart = now;
                using HostRingGauge::Opening;
                opening = battery.plugged ? Opening::Glow
                          : cableMoved    ? HostRingGauge::openingForUnplug(battery.percent)
                                          : Opening::None;
            }
            gaugeUntil = now + GAUGE_SHOW;
        }

        const bool firmwareLit = isAwake && (r != 0 || g != 0 || b != 0);
        if (now < gaugeUntil && (!firmwareLit || battery.plugged)) {
            using std::chrono::duration_cast;
            using std::chrono::milliseconds;
            HostRingGauge::Colour ring[HostRingGauge::LEDS];
            HostRingGauge::render(
                battery.percent, opening, battery.plugged,
                static_cast<uint32_t>(duration_cast<milliseconds>(now - gaugeStart).count()),
                static_cast<uint32_t>(duration_cast<milliseconds>(gaugeUntil - now).count()), ring);
            sendGauge(ring);
            lit = 1;
            gaugeShown = true;
        } else if (lightChanged || gaugeShown) {
            gaugeShown = false;
            if (!isAwake || !ledEnabled()) {
                sendSolid(0, 0, 0);
                lit = 0;
            } else if (r == 0 && g == 0 && b == 0) {
                sendRainbow();
                lit = 1;
            } else {
                sendSolid(r, g, b);
                lit = 1;
            }
        }
        /* Slept, not waited on the condition variable: without
         * pthread_cond_clockwait that wait is against the wall clock, and a
         * first NTP answer can step that back by hours, which would leave this
         * thread stuck there until something else woke it. Asleep with nothing
         * on the ring there is nothing to animate and no firmware colour to
         * follow, only a charger or a stick to notice, so it wakes a few times
         * a second instead of thirty. */
        const bool idle = !isAwake && now >= gaugeUntil;
        std::this_thread::sleep_for(idle ? IDLE_FRAME : GAUGE_FRAME);
    }
}

void start() {
    if (const char* env = std::getenv("BRAINO_LED_RAINBOW")) {
        unsigned bri = 0, speed = 0;
        if (std::sscanf(env, "%u,%u", &bri, &speed) == 2) {
            rainbowBrightness = static_cast<uint8_t>(bri > 255 ? 255 : bri);
            rainbowSpeed = static_cast<uint8_t>(speed > 255 ? 255 : speed);
        }
    }
    std::thread(worker).detach();
}

void changed() {
    std::call_once(started, start);
    dirty = true;
}

}   // namespace

namespace HostLed {

void setChannel(int channel, uint8_t level) {
    if (channel < 0 || channel > 2) return;
    static bool kicked = false;   // the boot's first "all off" must still start the rainbow
    std::lock_guard<std::mutex> lock(mutex);
    if (want[channel] == level && kicked) return;
    kicked = true;
    want[channel] = level;
    changed();
}

void setDisplayAwake(bool isAwake) {
    std::lock_guard<std::mutex> lock(mutex);
    if (awake == isAwake) return;
    awake = isAwake;
    changed();
}

int8_t lightOn() { return static_cast<int8_t>(lit.load()); }

void showBattery() {
    std::lock_guard<std::mutex> lock(mutex);
    gaugeAsked = true;
    changed();
}

}   // namespace HostLed
