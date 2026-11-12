// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The host's answer to PowerProbe: a Linux handheld, or a laptop.
 *
 * Everything is read from /proc and /sys, so it works on anything with a
 * kernel and simply reports less where a file is missing. A PMIC with
 * current_now is the prize: it answers what a change costs within seconds
 * rather than after an hour of watching a percentage.
 *
 * The busiest processes are here because this platform, unlike a board, runs
 * other people's software beside the firmware: a daemon left over from the OS
 * eating a core looks exactly like a battery defect.
 *
 * The [drain] lines are for finding where the power goes while that is still
 * an open question, and say things the Batt tab has no room for: once, what
 * the kernel offers (every power_supply value, the sleep states, what can
 * wake the device); then once a minute, how often the processor was woken
 * and by what, how deeply its cores slept, whether the audio device is
 * running, how many frames went to the display, and the radios' traffic.
 * Process, interrupt and supply names only: no address, serial or network. */

#include "hal/PowerProbe.h"

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "HostBattery.h"
#include "HostI2s.h"
#include "HostLed.h"
#include "HostNetTraffic.h"
#include "HostPlatform.h"
#include "HostProcFs.h"
#include "HostScreenPower.h"

/* The shim's clock, so [drain] and [power] lines share one t=. Declared here
 * rather than by including Arduino.h, whose String makes every std::string
 * concatenation in this file ambiguous. */
uint32_t millis();

namespace {

constexpr int SYNC_EVERY = 30;   // samples: a console is switched off by pulling power
constexpr int TOP_PROCESSES = 3;
constexpr int DRAIN_EVERY = 6;   // samples of 10 s: one [drain] line a minute
constexpr int TOP_IRQS = 4;
constexpr size_t LONGEST_VALUE = 32;
constexpr int MAX_CPUS = 8;
constexpr int MAX_IDLE_STATES = 8;

constexpr int SOCKETS_EVERY = 10;   // [drain] lines: which processes hold sockets

using HostProcFs::listDir;
using HostProcFs::readLine;

/* A value safe to print: short, and not a serial number or a name a vendor
 * may have made unique to the unit. */
bool printable(const std::string& file, const char* value) {
    if (file == "uevent" || file.find("serial") != std::string::npos) return false;
    return std::strlen(value) <= LONGEST_VALUE;
}

class HostPowerProbe : public PowerProbe {
public:
    /* Asked once: the Batt tab reads this from the loop, and a sensor does not
     * come and go. */
    const char* source() const override {
        static const bool hasCurrent = HostBattery::electrical().milliamps != PowerReading::UNKNOWN;
        return hasCurrent ? "Battery current sensor" : "Battery gauge only";
    }

    void read(PowerReading& out) override {
        const HostBattery::Reading battery = HostBattery::read();
        const HostBattery::Electrical electrical = HostBattery::electrical();
        out.percent = static_cast<int16_t>(battery.percent);
        out.onCharger = battery.plugged ? 1 : 0;
        out.milliamps = electrical.milliamps;
        out.cellMillivolts = electrical.millivolts;
        out.lightOn = HostLed::lightOn();
        out.lampPercent = HostScreenPower::lampPercent();

        readCpu(out);
        readProcesses(out);
        readRadios(out);

        char line[64];
        if (readLine("/sys/class/thermal/thermal_zone0/temp", line, sizeof(line))) {
            char text[16];
            std::snprintf(text, sizeof(text), "%ld C", std::atol(line) / 1000);
            out.addDetail("Temperature", text);
        }
        if (readLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", line, sizeof(line))) {
            out.addDetail("Governor", line);
        }

        if (samples_ == 0) logInventory();
        if (samples_ % DRAIN_EVERY == 0) logDrain();

        if (++samples_ % SYNC_EVERY == 0) {
            std::fflush(stdout);
            ::fsync(STDOUT_FILENO);
        }
    }

private:
    void readCpu(PowerReading& out) {
        char line[160];
        if (readLine("/proc/stat", line, sizeof(line))) {
            unsigned long long v[8] = {0};
            std::sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2],
                        &v[3], &v[4], &v[5], &v[6], &v[7]);
            unsigned long long total = 0;
            for (unsigned long long x : v) total += x;
            const unsigned long long idle = v[3] + v[4];
            totalDelta_ = total - lastTotal_;
            if (lastTotal_ != 0 && totalDelta_ > 0) {
                out.cpuPercent = static_cast<int16_t>(100 - (idle - lastIdle_) * 100 / totalDelta_);
            }
            lastTotal_ = total;
            lastIdle_ = idle;
        }
        if (readLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", line, sizeof(line))) {
            out.cpuMhz = static_cast<int32_t>(std::atol(line) / 1000);
        }
    }

    void readProcesses(PowerReading& out) {
        struct Busy {
            unsigned long long ticks;
            std::string name;
        };
        std::vector<Busy> busy;
        std::map<int, unsigned long long> seen;
        DIR* dir = ::opendir("/proc");
        if (!dir) return;
        while (dirent* e = ::readdir(dir)) {
            if (!std::isdigit(static_cast<unsigned char>(e->d_name[0]))) continue;
            const int pid = std::atoi(e->d_name);
            char line[512];
            if (!readLine(std::string("/proc/") + e->d_name + "/stat", line, sizeof(line))) continue;
            /* The name is in parentheses and may itself contain spaces or a
             * parenthesis, so the fields are counted from the LAST ')'. */
            const char* open = std::strchr(line, '(');
            const char* close = std::strrchr(line, ')');
            if (!open || !close) continue;
            unsigned long long utime = 0, stime = 0;
            if (std::sscanf(close + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu",
                            &utime, &stime) != 2) {
                continue;
            }
            const unsigned long long ticks = utime + stime;
            seen[pid] = ticks;
            const auto was = lastTicks_.find(pid);
            if (was != lastTicks_.end() && ticks > was->second) {
                /* Reduced to harmless characters: this goes into a console
                 * reply and a log, and a process may be named anything. */
                std::string name(open + 1, close);
                for (char& c : name) {
                    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '.';
                }
                busy.push_back({ticks - was->second, name});
            }
        }
        ::closedir(dir);
        lastTicks_.swap(seen);

        if (totalDelta_ == 0) return;
        std::sort(busy.begin(), busy.end(),
                  [](const Busy& a, const Busy& b) { return a.ticks > b.ticks; });
        for (int i = 0; i < TOP_PROCESSES && i < static_cast<int>(busy.size()); ++i) {
            char label[12];
            char value[PowerReading::DETAIL_VALUE];
            std::snprintf(label, sizeof(label), "Busiest %d", i + 1);
            std::snprintf(value, sizeof(value), "%llu%% %.15s", busy[i].ticks * 100 / totalDelta_,
                          busy[i].name.c_str());
            out.addDetail(label, value);
        }
    }

    void readRadios(PowerReading& out) {
        char line[32];
        if (readLine("/sys/class/net/wlan0/operstate", line, sizeof(line))) {
            out.addDetail("Wi-Fi radio", line);
        }
        DIR* dir = ::opendir("/sys/class/rfkill");
        if (!dir) return;
        while (dirent* e = ::readdir(dir)) {
            if (e->d_name[0] == '.') continue;
            const std::string base = std::string("/sys/class/rfkill/") + e->d_name;
            char type[16], state[8];
            if (!readLine(base + "/type", type, sizeof(type)) || std::strcmp(type, "bluetooth") != 0) {
                continue;
            }
            if (readLine(base + "/state", state, sizeof(state))) {
                out.addDetail("BT radio", state[0] == '1' ? "On" : "Blocked");
            }
            break;
        }
        ::closedir(dir);
    }

    /* Once: what there is to read, and what the kernel could do about sleep. */
    void logInventory() {
        char line[160];
        for (const std::string& supply : listDir("/sys/class/power_supply")) {
            const std::string base = "/sys/class/power_supply/" + supply + "/";
            std::string values;
            for (const std::string& file : listDir(base)) {
                if (!readLine(base + file, line, sizeof(line)) || !printable(file, line)) continue;
                values += " " + file + "=" + line;
            }
            std::printf("[drain] supply %s:%s\n", supply.c_str(), values.c_str());
        }
        const char* single[][2] = {
            {"sleep states", "/sys/power/state"},
            {"mem sleep", "/sys/power/mem_sleep"},
            {"cpus online", "/sys/devices/system/cpu/online"},
            {"cpu freqs", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies"},
            {"cpu min", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq"},
            {"cpu max", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"},
            {"governors", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors"},
        };
        for (const auto& s : single) {
            if (readLine(s[1], line, sizeof(line))) std::printf("[drain] %s: %s\n", s[0], line);
        }
        std::string idle;
        for (const std::string& state : listDir("/sys/devices/system/cpu/cpu0/cpuidle")) {
            if (readLine("/sys/devices/system/cpu/cpu0/cpuidle/" + state + "/name", line, sizeof(line))) {
                idle += " " + state + "=" + line;
            }
        }
        std::printf("[drain] cpu idle states:%s\n", idle.empty() ? " none" : idle.c_str());
        std::string wake;
        for (const std::string& w : listDir("/sys/class/wakeup")) {
            if (readLine("/sys/class/wakeup/" + w + "/name", line, sizeof(line))) wake += " " + std::string(line);
        }
        std::printf("[drain] wakeup sources:%s\n", wake.empty() ? " (not listed)" : wake.c_str());
        for (const std::string& d : listDir("/sys/class/devfreq")) {
            const std::string base = "/sys/class/devfreq/" + d + "/";
            std::string values;
            for (const char* f : {"governor", "min_freq", "max_freq", "available_frequencies"}) {
                if (readLine(base + f, line, sizeof(line))) values += std::string(" ") + f + "=" + line;
            }
            std::printf("[drain] devfreq %s:%s\n", d.c_str(), values.c_str());
        }
        std::string zones;
        for (const std::string& z : listDir("/sys/class/thermal")) {
            if (z.compare(0, 12, "thermal_zone") != 0) continue;
            if (readLine("/sys/class/thermal/" + z + "/type", line, sizeof(line))) {
                zones += " " + z.substr(12) + "=" + line;
            }
        }
        std::printf("[drain] thermal zones:%s\n", zones.c_str());
        /* What sits on each I2C bus, so a busy twiN interrupt has a name. */
        std::string i2c;
        for (const std::string& d : listDir("/sys/bus/i2c/devices")) {
            if (readLine("/sys/bus/i2c/devices/" + d + "/name", line, sizeof(line))) i2c += " " + d + "=" + line;
        }
        std::printf("[drain] i2c devices:%s\n", i2c.empty() ? " none" : i2c.c_str());
        std::ifstream cards("/proc/asound/cards");
        for (std::string l; std::getline(cards, l);) {
            if (!l.empty() && l[0] != ' ') std::printf("[drain] sound card: %s\n", l.c_str());
        }
        std::fflush(stdout);
    }

    /* Once a minute: what woke the processor, how deeply it slept, and what
     * was left running. */
    void logDrain() {
        const uint32_t nowMs = millis();
        const double seconds = lastDrainMs_ ? (nowMs - lastDrainMs_) / 1000.0 : 0.0;
        lastDrainMs_ = nowMs;
        std::ostringstream out;
        out << "[drain] t=" << nowMs / 1000;

        /* Wakeups: interrupts and context switches per second. */
        unsigned long long intr = 0, ctxt = 0;
        {
            std::ifstream stat("/proc/stat");
            for (std::string l; std::getline(stat, l);) {
                if (l.compare(0, 5, "intr ") == 0) intr = std::strtoull(l.c_str() + 5, nullptr, 10);
                if (l.compare(0, 5, "ctxt ") == 0) ctxt = std::strtoull(l.c_str() + 5, nullptr, 10);
            }
        }
        if (seconds > 0) {
            out << " irq/s=" << static_cast<long>((intr - lastIntr_) / seconds)
                << " ctxt/s=" << static_cast<long>((ctxt - lastCtxt_) / seconds);
        }
        lastIntr_ = intr;
        lastCtxt_ = ctxt;

        /* Which interrupts: the busiest few, by name. */
        std::map<std::string, unsigned long long> irqs;
        {
            std::ifstream f("/proc/interrupts");
            std::string l;
            std::getline(f, l);   // the CPU header
            while (std::getline(f, l)) {
                std::istringstream words(l);
                std::string id, word, name;
                words >> id;
                unsigned long long total = 0;
                while (words >> word) {
                    if (std::isdigit(static_cast<unsigned char>(word[0]))) total += std::strtoull(word.c_str(), nullptr, 10);
                    name = word;   // the last word is the device's name
                }
                irqs[id + name] = total;
            }
        }
        if (seconds > 0) {
            std::vector<std::pair<unsigned long long, std::string>> busy;
            for (const auto& [key, total] : irqs) {
                const auto was = lastIrqs_.find(key);
                if (was != lastIrqs_.end() && total > was->second) busy.push_back({total - was->second, key});
            }
            std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            out << " top_irq=";
            for (int i = 0; i < TOP_IRQS && i < static_cast<int>(busy.size()); ++i) {
                out << (i ? "," : "") << busy[i].second << ":" << static_cast<long>(busy[i].first / seconds);
            }
        }
        lastIrqs_.swap(irqs);

        /* How deeply the cores slept: share of the minute in each idle state. */
        int online = 0;
        unsigned long long idleUs[MAX_IDLE_STATES] = {0};
        std::string idleNames[MAX_IDLE_STATES];
        char line[64];
        for (int cpu = 0; cpu < MAX_CPUS; ++cpu) {
            const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
            if (::access(base.c_str(), F_OK) != 0) break;
            if (readLine(base + "/online", line, sizeof(line)) && line[0] == '0') continue;
            ++online;
            for (int s = 0; s < MAX_IDLE_STATES; ++s) {
                const std::string st = base + "/cpuidle/state" + std::to_string(s);
                if (!readLine(st + "/time", line, sizeof(line))) break;
                idleUs[s] += std::strtoull(line, nullptr, 10);
                if (idleNames[s].empty() && readLine(st + "/name", line, sizeof(line))) idleNames[s] = line;
            }
        }
        out << " cpus=" << online;
        if (seconds > 0 && online > 0 && !idleNames[0].empty()) {
            out << " idle=";
            for (int s = 0; s < MAX_IDLE_STATES && !idleNames[s].empty(); ++s) {
                const double share = (idleUs[s] - lastIdleUs_[s]) / (seconds * 1e6 * online) * 100.0;
                out << (s ? "," : "") << idleNames[s] << ":" << static_cast<int>(share + 0.5) << "%";
            }
        }
        std::copy(std::begin(idleUs), std::end(idleUs), std::begin(lastIdleUs_));

        /* Each GPU or bus clock that scales, as it is now -- and whether the
         * device behind it is powered at all. A clock is only the rate it
         * would run at: an idle GPU is power-gated by runtime PM whatever its
         * clock says, so the share of the minute it was active is the number
         * that costs battery. */
        for (const std::string& d : listDir("/sys/class/devfreq")) {
            const std::string base = "/sys/class/devfreq/" + d;
            if (readLine(base + "/cur_freq", line, sizeof(line))) {
                out << " " << d << "_mhz=" << std::strtoull(line, nullptr, 10) / 1000000;
            }
            if (readLine(base + "/device/power/runtime_status", line, sizeof(line))) {
                out << " " << d << "_pm=" << line;
                if (readLine(base + "/device/power/runtime_active_time", line, sizeof(line))) {
                    const unsigned long long active = std::strtoull(line, nullptr, 10);
                    const auto was = lastGpuActiveMs_.find(d);
                    if (seconds > 0 && was != lastGpuActiveMs_.end() && active >= was->second) {
                        out << "," << static_cast<int>((active - was->second) / (seconds * 10.0) + 0.5) << "%";
                    }
                    lastGpuActiveMs_[d] = active;
                }
            }
        }

        /* The audio device: running means the codec and its amplifier are on. */
        std::string audio = "none";
        for (const std::string& card : listDir("/proc/asound")) {
            if (card.compare(0, 4, "card") != 0) continue;
            for (const std::string& pcm : listDir("/proc/asound/" + card)) {
                if (pcm.compare(0, 3, "pcm") != 0 || pcm.back() != 'p') continue;
                if (readLine("/proc/asound/" + card + "/" + pcm + "/sub0/status", line, sizeof(line))) {
                    const char* state = std::strncmp(line, "state:", 6) == 0 ? line + 7 : line;
                    if (audio == "none" || std::strcmp(state, "RUNNING") == 0) audio = state;
                }
            }
        }
        out << " audio=" << audio;

        /* Braino's own wakeups, by thread name: the part of the processor's
         * wakeups this program can do something about. */
        std::map<std::string, unsigned long long> switches;
        for (const std::string& tid : listDir("/proc/self/task")) {
            const std::string base = "/proc/self/task/" + tid + "/";
            char name[32];
            if (!readLine(base + "comm", name, sizeof(name))) continue;
            std::ifstream status(base + "status");
            unsigned long long n = 0;
            for (std::string l; std::getline(status, l);) {
                if (l.find("ctxt_switches:") != std::string::npos) n += std::strtoull(l.c_str() + l.find(':') + 1, nullptr, 10);
            }
            switches[tid + "/" + name] = n;
        }
        if (seconds > 0) {
            std::map<std::string, double> byName;
            for (const auto& [key, n] : switches) {
                const auto was = lastSwitches_.find(key);
                if (was != lastSwitches_.end() && n > was->second) {
                    byName[key.substr(key.find('/') + 1)] += (n - was->second) / seconds;
                }
            }
            std::vector<std::pair<double, std::string>> busy;
            for (const auto& [name, rate] : byName) busy.push_back({rate, name});
            std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            out << " braino_wake/s=";
            for (int i = 0; i < TOP_IRQS && i < static_cast<int>(busy.size()); ++i) {
                out << (i ? "," : "") << busy[i].second << ":" << static_cast<long>(busy[i].first + 0.5);
            }
        }
        lastSwitches_.swap(switches);

        /* The fuel gauge's own estimate of time to empty, as a current: the
         * charge it has left over the time it says that will last. Only
         * meaningful off the charger. */
        for (const std::string& supply : listDir("/sys/class/power_supply")) {
            const std::string base = "/sys/class/power_supply/" + supply + "/";
            char type[16], status[16], tte[24], charge[24];
            if (!readLine(base + "type", type, sizeof(type)) || std::strcmp(type, "Battery") != 0) continue;
            if (readLine(base + "spk_state", line, sizeof(line))) out << " spk_state=" << line;
            if (readLine(base + "status", status, sizeof(status)) && std::strcmp(status, "Discharging") == 0 &&
                readLine(base + "time_to_empty_now", tte, sizeof(tte)) &&
                readLine(base + "charge_counter", charge, sizeof(charge))) {
                const double hours = std::atof(tte) / 3600.0;
                if (hours > 0) {
                    out << " tte_min=" << static_cast<long>(hours * 60)
                        << " gauge_ma=" << static_cast<long>(std::atof(charge) / 1000.0 / hours);
                }
            }
            break;
        }

        /* Frames sent to the display. */
        const uint32_t frames = HostPlatform::presentCount();
        if (seconds > 0) out << " frames/min=" << (frames - lastFrames_);
        lastFrames_ = frames;

        /* How often the audio device was opened for a sound: each one is the
         * codec and the amplifier coming up, and going down two seconds after. */
        const uint32_t opens = HostI2s::openCount();
        if (seconds > 0) out << " audio_opens/min=" << (opens - lastOpens_);
        lastOpens_ = opens;

        /* Wi-Fi traffic, what kind it is, and whether the radio may doze
         * between beacons. */
        HostNetTraffic::appendMinute(out, seconds);

        /* Every temperature the kernel reports, not just the first zone's. */
        for (const std::string& z : listDir("/sys/class/thermal")) {
            if (z.compare(0, 12, "thermal_zone") != 0) continue;
            if (readLine("/sys/class/thermal/" + z + "/temp", line, sizeof(line))) {
                out << " " << z.substr(12) << "_c=" << std::atol(line) / 1000;
            }
        }
        std::printf("%s\n", out.str().c_str());
        if (drains_++ % SOCKETS_EVERY == 0) HostNetTraffic::logSockets();
    }

    uint32_t lastDrainMs_ = 0;
    int drains_ = 0;
    unsigned long long lastIntr_ = 0, lastCtxt_ = 0;
    std::map<std::string, unsigned long long> lastGpuActiveMs_;
    unsigned long long lastIdleUs_[MAX_IDLE_STATES] = {0};
    std::map<std::string, unsigned long long> lastIrqs_;
    std::map<std::string, unsigned long long> lastSwitches_;
    uint32_t lastFrames_ = 0;
    uint32_t lastOpens_ = 0;

    unsigned long long lastTotal_ = 0;
    unsigned long long lastIdle_ = 0;
    unsigned long long totalDelta_ = 0;
    std::map<int, unsigned long long> lastTicks_;
    int samples_ = 0;
};

}   // namespace

PowerProbe& platformPowerProbe() {
    static HostPowerProbe probe;
    return probe;
}
