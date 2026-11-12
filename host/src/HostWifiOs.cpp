// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* See HostWifiOs.h. */

#include "HostWifiOs.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

extern char** environ;

namespace HostWifiOs {
namespace {

enum class Backend : uint8_t { None, NetworkManager, WpaSupplicant };

std::mutex stateMutex;
Link current;
JoinState joinState_ = JoinState::Idle;
std::string target;
std::atomic<uint32_t> joinGeneration{0};
/* Written once, by the starter thread, before `ready` is set; read by everyone
 * else only after seeing it set. */
Backend backend = Backend::None;
std::string iface;
std::atomic<bool> ready{false};
std::once_flag started;
/* See sleepRadio(). The generation lets a wake that is still rejoining notice
 * that the panel went back to sleep under it. */
std::atomic<bool> radioAsleep{false};
std::atomic<uint32_t> radioGeneration{0};
constexpr const char* RADIO_MARK = "/run/braino/sleep-wifi-off";

constexpr int JOIN_TIMEOUT_S = 20;
constexpr int READY_WAIT_S = 30;
/* muOS's own network script, which knows how to load this board's Wi-Fi
 * driver; and the wpa_supplicant config muOS keeps. */
constexpr const char* MUOS_NETWORK = "/opt/muos/script/init/async/S02network.sh";
constexpr const char* WPA_CONF = "/etc/wpa_supplicant.conf";

/* ------------------------------------------------------------- processes */

bool onPath(const char* name) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::stringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        const std::string full = dir + "/" + name;
        if (::access(full.c_str(), X_OK) == 0) return true;
    }
    return false;
}

/* Run argv[0] with argv, no shell, stdout and stderr captured together.
 * Returns the exit status, or -1 when it could not start or ran past the
 * timeout (and was killed). */
int run(const std::vector<std::string>& args, int timeoutMs, std::string* out) {
    int pipeFds[2];
    if (::pipe(pipeFds) != 0) return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipeFds[1]);
    if (rc != 0) {
        ::close(pipeFds[0]);
        return -1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool timedOut = false;
    char buf[1024];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        if (left <= 0) {
            timedOut = true;
            break;
        }
        pollfd p{pipeFds[0], POLLIN, 0};
        const int ready = ::poll(&p, 1, static_cast<int>(left));
        if (ready <= 0) continue;
        const ssize_t n = ::read(pipeFds[0], buf, sizeof(buf));
        if (n <= 0) break;   // EOF: the child closed its end
        if (out) out->append(buf, static_cast<size_t>(n));
    }
    ::close(pipeFds[0]);
    if (timedOut) ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (timedOut || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

/* Start a program that daemonises (-b, -B) and wait only for the process we
 * started to exit, with its output sent to /dev/null.
 *
 * run() reads the child's output until EOF, and a daemon's forked copy
 * inherits that pipe and holds it open for as long as it lives -- so run()
 * would sit out its whole timeout every time. That is how the first join on
 * an RG40XXV outlasted the Wi-Fi screen's 15 s: dhcpcd had very likely got an
 * address in a second, and the join was still waiting on a pipe. */
int runQuiet(const std::vector<std::string>& args, int timeoutMs) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) return -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (::waitpid(pid, &status, WNOHANG) == pid) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &status, 0);
    return -1;
}

uint32_t sinceMs(std::chrono::steady_clock::time_point t0) {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t0)
                                     .count());
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

/* nmcli's terse mode separates fields with ':' and escapes literal ones as
 * "\:" -- which a BSSID is full of. */
std::vector<std::string> terseFields(const std::string& line) {
    std::vector<std::string> fields(1);
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            fields.back() += line[++i];
        } else if (line[i] == ':') {
            fields.emplace_back();
        } else {
            fields.back() += line[i];
        }
    }
    return fields;
}

int channelOf(int mhz) {
    if (mhz >= 2412 && mhz <= 2472) return (mhz - 2407) / 5;
    if (mhz == 2484) return 14;
    if (mhz >= 5000 && mhz <= 5900) return (mhz - 5000) / 5;
    return 0;
}

/* nmcli reports signal as 0-100. The usual linear mapping back to dBm puts
 * 100% at -50 and 0% at -100, which is what the firmware's bars expect. */
int percentToDbm(int pct) { return pct / 2 - 100; }

std::string hexOf(const std::string& s) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}

/* ------------------------------------------------------------- discovery */

std::string findWirelessInterface() {
    DIR* dir = ::opendir("/sys/class/net");
    if (!dir) return std::string();
    std::string found;
    while (dirent* e = ::readdir(dir)) {
        if (e->d_name[0] == '.') continue;
        const std::string base = std::string("/sys/class/net/") + e->d_name;
        struct stat st;
        if (::stat((base + "/wireless").c_str(), &st) == 0 ||
            ::stat((base + "/phy80211").c_str(), &st) == 0) {
            found = e->d_name;
            break;
        }
    }
    ::closedir(dir);
    return found;
}

bool networkManagerRunning() {
    if (!onPath("nmcli")) return false;
    std::string out;
    return run({"nmcli", "-t", "-f", "RUNNING", "general"}, 3000, &out) == 0 &&
           out.find("running") != std::string::npos;
}

bool wpaAnswers() {
    if (!onPath("wpa_cli")) return false;
    std::string out;
    return run({"wpa_cli", "-i", iface, "ping"}, 3000, &out) == 0 &&
           out.find("PONG") != std::string::npos;
}

/* On a handheld, nothing may have brought the radio up yet.
 *
 * muOS loads the Wi-Fi driver and starts wpa_supplicant only when ITS menu
 * connects a saved profile, or at boot when "connect on boot" is on -- so on a
 * console that has only ever run Braino there is no wireless interface at
 * all, and a scan would find nothing. So the radio is brought up the way the
 * OS would: the driver through muOS's own network script (it knows which
 * module this board needs), then wpa_supplicant on the interface with the
 * config file muOS keeps, created with muOS's own header if it is missing.
 * Where NetworkManager or a running wpa_supplicant already owns the radio,
 * nothing is started. This is configuration of the computer's own radio, the
 * same thing its Wi-Fi menu does; it sends nothing anywhere. */
/* Strip every network={...} block out of WPA_CONF, keeping the rest. Earlier
 * builds asked wpa_supplicant to save the network it joined, which put the
 * password there in plain text; this takes it back out, and nothing now
 * writes one in. Rewritten in place, after zeroing, so the old bytes do not
 * stay on the card behind a rename. */
void scrubSavedNetworks() {
    FILE* in = std::fopen(WPA_CONF, "r");
    if (!in) return;
    std::string kept;
    bool removed = false;
    int depth = 0;
    char line[1024];
    while (std::fgets(line, sizeof(line), in)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (depth == 0 && std::strncmp(p, "network=", 8) == 0) {
            depth = 1;
            removed = true;
            continue;
        }
        if (depth > 0) {
            if (std::strchr(line, '}')) depth = 0;
            continue;
        }
        kept += line;
    }
    const long oldSize = std::ftell(in);
    std::fclose(in);
    if (!removed) return;
    if (FILE* out = std::fopen(WPA_CONF, "r+")) {
        static const char zeros[512] = {0};
        for (long left = oldSize; left > 0; left -= static_cast<long>(sizeof(zeros))) {
            std::fwrite(zeros, 1, left < static_cast<long>(sizeof(zeros)) ? left : sizeof(zeros), out);
        }
        std::fflush(out);
        ::fsync(::fileno(out));
        std::fclose(out);
    }
    if (FILE* out = std::fopen(WPA_CONF, "w")) {
        std::fputs(kept.c_str(), out);
        std::fflush(out);
        ::fsync(::fileno(out));
        std::fclose(out);
        std::printf("[wifi] host: removed saved networks from %s\n", WPA_CONF);
        std::fflush(stdout);
    }
}

void bringUp() {
    scrubSavedNetworks();
    if (iface.empty() && ::access(MUOS_NETWORK, R_OK) == 0) {
        std::printf("[wifi] host: loading the Wi-Fi driver through muOS\n");
        std::fflush(stdout);
        runQuiet({"sh", MUOS_NETWORK, "load"}, 20000);
        for (int i = 0; i < 10 && iface.empty(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            iface = findWirelessInterface();
        }
    }
    if (iface.empty() || networkManagerRunning() || wpaAnswers()) return;
    if (!onPath("wpa_supplicant")) return;

    struct stat st;
    if (::stat(WPA_CONF, &st) != 0) {
        if (FILE* f = std::fopen(WPA_CONF, "w")) {
            std::fputs("ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=0\nupdate_config=1\n", f);
            std::fclose(f);
            ::chmod(WPA_CONF, 0600);
        }
    }
    std::printf("[wifi] host: starting wpa_supplicant on the radio\n");
    std::fflush(stdout);
    run({"ip", "link", "set", iface, "up"}, 3000, nullptr);
    /* muOS's own script turns power saving off on every wireless interface it
     * brings up; on these SDIO Realtek parts it is known to drop handshakes
     * and DHCP replies. */
    if (onPath("iw")) run({"iw", "dev", iface, "set", "power_save", "off"}, 3000, nullptr);
    runQuiet({"wpa_supplicant", "-B", "-i", iface, "-c", WPA_CONF, "-D", "nl80211"}, 10000);
    for (int i = 0; i < 10 && !wpaAnswers(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

/* An address once associated. wpa_supplicant joins the network; getting an
 * IP from it is the DHCP client's job, which the OS's own connect script
 * would have done. dhcpcd when the system has it, BusyBox udhcpc otherwise.
 *
 * udhcpc is the card image's client, and it is told to say nothing about the
 * console: -C sends no client identifier (the default is the radio's MAC,
 * which would be recorded with the lease), -V "" sends no vendor string (the
 * default is "udhcp" and its version), and with no -F or -x it sends no
 * hostname. What is left is what DHCP cannot work without: the hardware
 * address in the request itself, which every frame on the network carries
 * anyway and which never leaves it. tools/check_privacy.py holds these. */
void requestAddress() {
    std::printf("[wifi] host: DHCP with %s\n",
                onPath("dhcpcd") ? "dhcpcd" : (onPath("udhcpc") ? "udhcpc" : "nothing (no client found)"));
    std::fflush(stdout);
    if (onPath("dhcpcd")) {
        runQuiet({"dhcpcd", "-4", "-q", "-b", iface}, 15000);
    } else if (onPath("udhcpc")) {
        runQuiet({"udhcpc", "-i", iface, "-b", "-q", "-C", "-V", ""}, 15000);
    }
}

/* The radio can be taken away while Braino runs: muOS's own sleep stops the
 * network, and nothing tells this program. Scans then come back empty with no
 * error, and a join has nothing to talk to, until the next start. So anything
 * about to use wpa_supplicant checks that it still answers, and brings the
 * radio back the way the first start did. */
void reviveRadio() {
    static std::mutex reviving;
    std::lock_guard<std::mutex> lock(reviving);
    if (backend != Backend::WpaSupplicant || wpaAnswers()) return;
    const char* forced = std::getenv("BRAINO_WIFI_IFACE");
    if (forced && *forced) return;
    std::printf("[wifi] host: the radio was stopped underneath Braino, bringing it back\n");
    std::fflush(stdout);
    const std::string device = "/sys/class/net/" + iface;
    if (::access(device.c_str(), F_OK) != 0 && ::access(MUOS_NETWORK, R_OK) == 0) {
        runQuiet({"sh", MUOS_NETWORK, "load"}, 20000);
        for (int i = 0; i < 10 && ::access(device.c_str(), F_OK) != 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    bringUp();
    std::printf("[wifi] host: radio %s\n", wpaAnswers() ? "back" : "did not come back");
    std::fflush(stdout);
}

void waitReady() {
    for (int i = 0; i < READY_WAIT_S * 10 && !ready; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

Backend detectBackend() {
    if (iface.empty()) return Backend::None;
    if (onPath("nmcli")) {
        std::string out;
        if (run({"nmcli", "-t", "-f", "RUNNING", "general"}, 3000, &out) == 0 &&
            out.find("running") != std::string::npos) {
            return Backend::NetworkManager;
        }
    }
    if (onPath("wpa_cli")) {
        std::string out;
        if (run({"wpa_cli", "-i", iface, "ping"}, 3000, &out) == 0 &&
            out.find("PONG") != std::string::npos) {
            return Backend::WpaSupplicant;
        }
    }
    return Backend::None;
}

/* ------------------------------------------------------------- link state */

void readAddress(Link& l) {
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) return;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (iface != a->ifa_name) continue;
        l.ip = reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr;
        if (a->ifa_netmask) {
            l.netmask = reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr;
        }
        l.hasAddress = l.ip != 0;
    }
    ::freeifaddrs(list);

    /* /proc/net/route prints each address as the raw in_addr word in hex,
     * so parsing it back gives network byte order directly. */
    if (FILE* f = std::fopen("/proc/net/route", "r")) {
        char name[64];
        unsigned long dest = 0, gw = 0;
        char line[256];
        bool header = true;
        while (std::fgets(line, sizeof(line), f)) {
            if (header) {
                header = false;
                continue;
            }
            if (std::sscanf(line, "%63s %lx %lx", name, &dest, &gw) == 3 && iface == name &&
                dest == 0) {
                l.gateway = static_cast<uint32_t>(gw);
                break;
            }
        }
        std::fclose(f);
    }
    if (FILE* f = std::fopen("/etc/resolv.conf", "r")) {
        char line[256];
        char addr[64];
        while (std::fgets(line, sizeof(line), f)) {
            if (std::sscanf(line, "nameserver %63s", addr) == 1) {
                in_addr a{};
                if (::inet_pton(AF_INET, addr, &a) == 1) {
                    l.dns = a.s_addr;
                    break;
                }
            }
        }
        std::fclose(f);
    }
}

Link readLink() {
    Link l;
    if (backend == Backend::None) return l;
    l.radio = true;

    if (backend == Backend::NetworkManager) {
        std::string out;
        if (run({"nmcli", "-t", "-f", "ACTIVE,SSID,SIGNAL,CHAN,BSSID", "device", "wifi", "list",
                 "ifname", iface, "--rescan", "no"},
                3000, &out) == 0) {
            for (const std::string& line : lines(out)) {
                const auto f = terseFields(line);
                if (f.size() >= 5 && f[0] == "yes") {
                    l.associated = true;
                    l.ssid = f[1];
                    l.rssi = percentToDbm(std::atoi(f[2].c_str()));
                    l.channel = std::atoi(f[3].c_str());
                    l.bssid = f[4];
                    break;
                }
            }
        }
    } else {
        std::string out;
        if (run({"wpa_cli", "-i", iface, "status"}, 3000, &out) == 0) {
            int freq = 0;
            bool completed = false;
            for (const std::string& line : lines(out)) {
                if (line == "wpa_state=COMPLETED") completed = true;
                if (line.rfind("ssid=", 0) == 0) l.ssid = line.substr(5);
                if (line.rfind("bssid=", 0) == 0) l.bssid = line.substr(6);
                if (line.rfind("freq=", 0) == 0) freq = std::atoi(line.c_str() + 5);
            }
            l.associated = completed;
            l.channel = channelOf(freq);
        }
        std::string sig;
        if (l.associated && run({"wpa_cli", "-i", iface, "signal_poll"}, 3000, &sig) == 0) {
            for (const std::string& line : lines(sig)) {
                if (line.rfind("RSSI=", 0) == 0) l.rssi = std::atoi(line.c_str() + 5);
            }
        }
    }
    if (!l.associated) l.ssid.clear();
    readAddress(l);
    return l;
}

void poller() {
    for (;;) {
        /* Two subprocesses a second is a processor woken twice a second;
         * with the radio off there is nothing for them to find. */
        if (radioAsleep) {
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                current = Link{};
                current.radio = true;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        const Link l = readLink();
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            current = l;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

/* Everything slow -- loading a driver, starting a supplicant, asking a
 * manager -- happens on this thread, never on the frame loop. Until it is
 * done the radio reads as absent, which on a board is what a radio still
 * coming up looks like too. */
void start() {
    std::thread([] {
        /* BRAINO_WIFI_IFACE names the interface outright: for a device with
         * more than one radio, and for exercising the managers' parsing on a
         * machine whose only interface is not wireless. */
        const char* forced = std::getenv("BRAINO_WIFI_IFACE");
        iface = (forced && *forced) ? std::string(forced) : findWirelessInterface();
        if (!forced || !*forced) bringUp();
        backend = detectBackend();
        std::printf("[wifi] host radio: %s%s%s\n", backendName(), iface.empty() ? "" : " on ",
                    iface.c_str());
        std::fflush(stdout);
        if (backend != Backend::None) {
            const Link l = readLink();
            std::lock_guard<std::mutex> lock(stateMutex);
            current = l;
        }
        ready = true;
        if (backend == Backend::WpaSupplicant && ::access(RADIO_MARK, F_OK) == 0) {
            std::printf("[wifi] host: a previous run left the radio off for sleep; waking it\n");
            std::fflush(stdout);
            radioAsleep = true;
            sleepRadio(false);
        }
        if (backend != Backend::None) poller();
    }).detach();
}

/* ------------------------------------------------------------- joining */

bool connectedTo(const std::string& ssid) {
    std::lock_guard<std::mutex> lock(stateMutex);
    return current.associated && current.hasAddress && current.ssid == ssid;
}

void finish(uint32_t generation, JoinState state) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (generation == joinGeneration) joinState_ = state;
}

void joinWithNetworkManager(uint32_t gen, const std::string& ssid, const std::string& password) {
    /* The password is an argument, so another local user could read it from
     * the process table for the second nmcli runs. On a single-user handheld
     * that is nobody; on a shared desktop, join from the desktop's own Wi-Fi
     * settings instead -- the firmware will see the result either way. */
    std::vector<std::string> args{"nmcli", "--wait", std::to_string(JOIN_TIMEOUT_S), "device",
                                  "wifi", "connect", ssid};
    if (!password.empty()) {
        args.push_back("password");
        args.push_back(password);
    }
    args.push_back("ifname");
    args.push_back(iface);
    std::string out;
    const int rc = run(args, (JOIN_TIMEOUT_S + 5) * 1000, &out);
    if (rc == 0) {
        finish(gen, JoinState::Idle);
    } else if (out.find("No network with SSID") != std::string::npos) {
        finish(gen, JoinState::NoSuchNetwork);
    } else {
        finish(gen, JoinState::Failed);
    }
}

void joinWithWpaSupplicant(uint32_t gen, const std::string& ssid, const std::string& password) {
    reviveRadio();
    auto cli = [](std::vector<std::string> args, std::string* out = nullptr) {
        args.insert(args.begin(), {"wpa_cli", "-i", iface});
        return run(args, 5000, out);
    };

    /* Replace any saved entry for this SSID rather than editing it, so a
     * changed password cannot leave a half-updated network behind. */
    std::string list;
    if (cli({"list_networks"}, &list) == 0) {
        for (const std::string& line : lines(list)) {
            std::stringstream fields(line);
            std::string id, name;
            if (std::getline(fields, id, '\t') && std::getline(fields, name, '\t') &&
                name == ssid && !id.empty() && std::isdigit(static_cast<unsigned char>(id[0]))) {
                cli({"remove_network", id});
            }
        }
    }

    std::string added;
    if (cli({"add_network"}, &added) != 0) {
        finish(gen, JoinState::Failed);
        return;
    }
    const auto addedLines = lines(added);
    const std::string id = addedLines.empty() ? std::string() : addedLines.back();
    if (id.empty() || !std::isdigit(static_cast<unsigned char>(id[0]))) {
        finish(gen, JoinState::Failed);
        return;
    }
    /* The SSID goes in as hex, which wpa_supplicant accepts unquoted, so a
     * name containing a quote cannot end the string early. */
    bool ok = cli({"set_network", id, "ssid", hexOf(ssid)}) == 0;
    if (password.empty()) {
        ok = ok && cli({"set_network", id, "key_mgmt", "NONE"}) == 0;
    } else {
        ok = ok && cli({"set_network", id, "psk", "\"" + password + "\""}) == 0;
    }
    ok = ok && cli({"enable_network", id}) == 0 && cli({"select_network", id}) == 0;
    if (!ok) {
        cli({"remove_network", id});
        finish(gen, JoinState::Failed);
        return;
    }
    /* Not saved to WPA_CONF. Braino's own settings hold the network, sealed
     * (HostSecret.h), and it joins from them on every start, so a second
     * copy in a file any Linux machine can read off the card would only be a
     * leak. scrubSavedNetworks() removes any an older build left there. */

    /* Every step is logged with its time, and never the network's name or
     * password -- the firmware's own rule for its serial log, which is what
     * gets pasted into bug reports. wpa_state shows where a join stops:
     * SCANNING (not found), ASSOCIATING, 4WAY_HANDSHAKE (a wrong password
     * stops here), COMPLETED. */
    const auto t0 = std::chrono::steady_clock::now();
    std::printf("[wifi] host: join network %s queued (pass len %u)\n", id.c_str(),
                static_cast<unsigned>(password.size()));
    std::fflush(stdout);
    std::string lastState;
    bool askedForAddress = false;
    for (int i = 0; i < JOIN_TIMEOUT_S * 4; ++i) {
        if (gen != joinGeneration) return;   // superseded by a newer join
        std::string status;
        cli({"status"}, &status);
        std::string state;
        for (const std::string& line : lines(status)) {
            if (line.rfind("wpa_state=", 0) == 0) state = line.substr(10);
        }
        if (state != lastState) {
            std::printf("[wifi] host: +%u ms wpa_state=%s\n", sinceMs(t0), state.c_str());
            std::fflush(stdout);
            lastState = state;
        }
        Link l = readLink();
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            current = l;
        }
        if (connectedTo(ssid)) {
            std::printf("[wifi] host: +%u ms address up, joined\n", sinceMs(t0));
            std::fflush(stdout);
            /* Power saving was off for the join, which is where it drops
             * handshakes and DHCP replies (startWpaSupplicant()). Joined, it
             * goes back on: Braino talks to the network every few hours, and
             * with it off the radio stays awake for every broadcast on the
             * network in between, hundreds a minute on a busy one. */
            if (onPath("iw")) run({"iw", "dev", iface, "set", "power_save", "on"}, 3000, nullptr);
            finish(gen, JoinState::Idle);
            return;
        }
        /* Associated with no address: nothing on this system runs DHCP for
         * us, so ask, once. */
        if (l.associated && l.ssid == ssid && !l.hasAddress && !askedForAddress) {
            askedForAddress = true;
            std::printf("[wifi] host: +%u ms associated, asking for an address\n", sinceMs(t0));
            std::fflush(stdout);
            requestAddress();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    std::printf("[wifi] host: +%u ms gave up at wpa_state=%s%s\n", sinceMs(t0), lastState.c_str(),
                askedForAddress ? " (no address from DHCP)" : "");
    std::fflush(stdout);
    finish(gen, JoinState::Failed);
}

}   // namespace

void begin() { std::call_once(started, start); }

Link link() {
    begin();
    std::lock_guard<std::mutex> lock(stateMutex);
    return current;
}

JoinState joinState() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return joinState_;
}

std::string joinTarget() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return target;
}

void join(const std::string& ssid, const std::string& password) {
    begin();
    if (radioAsleep) return;   // waking rejoins; see sleepRadio()
    if (!ready || backend == Backend::None || ssid.empty()) {
        /* Still coming up: join from a thread once it is, rather than drop
         * the request or hold the frame loop. */
        if (!ready && !ssid.empty()) {
            std::thread([ssid, password] {
                waitReady();
                if (ready && backend != Backend::None) join(ssid, password);
            }).detach();
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (joinState_ == JoinState::Joining && target == ssid) return;
        target = ssid;
    }
    if (connectedTo(ssid)) {
        finish(joinGeneration, JoinState::Idle);
        return;
    }
    const uint32_t gen = ++joinGeneration;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        joinState_ = JoinState::Joining;
    }
    std::printf("[wifi] host: joining through %s\n", backendName());
    std::fflush(stdout);
    if (backend == Backend::NetworkManager) {
        std::thread(joinWithNetworkManager, gen, ssid, password).detach();
    } else {
        std::thread(joinWithWpaSupplicant, gen, ssid, password).detach();
    }
}

int scan(ScanEntry* out, int capacity) {
    begin();
    waitReady();   // a scan blocks anyway; the firmware shows "Scanning..." first
    if (backend == Backend::None || capacity <= 0) return 0;
    int n = 0;
    if (backend == Backend::NetworkManager) {
        std::string text;
        if (run({"nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY,CHAN,BSSID", "device", "wifi", "list",
                 "ifname", iface, "--rescan", "yes"},
                15000, &text) != 0) {
            return 0;
        }
        for (const std::string& line : lines(text)) {
            const auto f = terseFields(line);
            if (f.size() < 5 || n >= capacity) continue;
            ScanEntry& e = out[n++];
            e.ssid = f[0];
            e.rssi = percentToDbm(std::atoi(f[1].c_str()));
            e.secured = !f[2].empty() && f[2] != "--";
            e.channel = std::atoi(f[3].c_str());
            e.bssid = f[4];
        }
        return n;
    }

    reviveRadio();
    run({"wpa_cli", "-i", iface, "scan"}, 5000, nullptr);
    std::string text;
    /* Results arrive a few seconds after the request; poll rather than
     * guess, and stop as soon as there is something to show. */
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        text.clear();
        run({"wpa_cli", "-i", iface, "scan_results"}, 5000, &text);
        if (lines(text).size() > 1 && i >= 5) break;
    }
    bool header = true;
    for (const std::string& line : lines(text)) {
        if (header) {   // "bssid / frequency / signal level / flags / ssid"
            header = false;
            continue;
        }
        std::stringstream fields(line);
        std::string bssid, freq, signal, flags, ssid;
        if (!std::getline(fields, bssid, '\t') || !std::getline(fields, freq, '\t') ||
            !std::getline(fields, signal, '\t') || !std::getline(fields, flags, '\t')) {
            continue;
        }
        std::getline(fields, ssid);
        if (n >= capacity) break;
        ScanEntry& e = out[n++];
        e.ssid = ssid;
        e.rssi = std::atoi(signal.c_str());
        e.secured = flags.find("WPA") != std::string::npos ||
                    flags.find("RSN") != std::string::npos ||
                    flags.find("WEP") != std::string::npos ||
                    flags.find("SAE") != std::string::npos;
        e.channel = channelOf(std::atoi(freq.c_str()));
        e.bssid = bssid;
    }
    std::printf("[wifi] host: scan found %d\n", n);
    std::fflush(stdout);
    return n;
}

const char* backendName() {
    switch (backend) {
        case Backend::NetworkManager: return "NetworkManager";
        case Backend::WpaSupplicant: return "wpa_supplicant";
        default: return iface.empty() ? "none (no wireless interface)" : "none (no manager answers)";
    }
}

void sleepRadio(bool asleep) {
    if (!ready || backend != Backend::WpaSupplicant || iface.empty()) return;
    if (radioAsleep == asleep) return;
    const uint32_t generation = ++radioGeneration;
    radioAsleep = asleep;
    /* A run that ends asleep leaves the interface down, and nothing at the
     * next start brings it up: start() finds wpa_supplicant answering and
     * skips bringUp(). This marker is how start() knows to wake it. */
    if (asleep) {
        ::mkdir("/run/braino", 0755);
        if (FILE* f = std::fopen(RADIO_MARK, "w")) std::fclose(f);
    } else {
        ::unlink(RADIO_MARK);
    }
    std::thread([asleep, generation] {
        auto cli = [](const char* command) { run({"wpa_cli", "-i", iface, command}, 3000, nullptr); };
        if (asleep) {
            /* disconnect first, so wpa_supplicant does not try to rejoin a
             * network on an interface that is going away. */
            cli("disconnect");
            run({"ip", "link", "set", iface, "down"}, 3000, nullptr);
            std::printf("[wifi] host: radio off for sleep\n");
            std::fflush(stdout);
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        run({"ip", "link", "set", iface, "up"}, 3000, nullptr);
        if (onPath("iw")) run({"iw", "dev", iface, "set", "power_save", "off"}, 3000, nullptr);
        cli("reconnect");
        bool askedForAddress = false;
        for (int i = 0; i < JOIN_TIMEOUT_S * 4; ++i) {
            if (generation != radioGeneration) return;   // asleep again already
            const Link l = readLink();
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                current = l;
            }
            if (l.associated && l.hasAddress) {
                if (onPath("iw")) run({"iw", "dev", iface, "set", "power_save", "on"}, 3000, nullptr);
                std::printf("[wifi] host: +%u ms back on the network after sleep\n", sinceMs(t0));
                std::fflush(stdout);
                return;
            }
            if (l.associated && !askedForAddress) {
                askedForAddress = true;
                requestAddress();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        std::printf("[wifi] host: +%u ms not back on the network after sleep%s\n", sinceMs(t0),
                    askedForAddress ? " (no address from DHCP)" : "");
        std::fflush(stdout);
    }).detach();
}

}   // namespace HostWifiOs
