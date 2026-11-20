// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* See HostNetTraffic.h. Everything comes from counters the kernel keeps
 * anyway: the interface's own statistics, the protocol tables in
 * /proc/net/snmp and snmp6, ARP's cache statistics, and the socket tables
 * matched to processes through /proc/<pid>/fd. */

#include "HostNetTraffic.h"

#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "HostProcFs.h"

using HostProcFs::commandLine;
using HostProcFs::listDir;
using HostProcFs::readLine;

namespace {

/* The handhelds' radio is wlan0. BRAINO_WIFI_IFACE overrides it, as it does
 * for HostWifiOs, which also lets these counters be read off a wired
 * interface on a desktop. Letters and digits only, because it goes into the
 * iw command line. */
std::string interfaceName() {
    const char* forced = std::getenv("BRAINO_WIFI_IFACE");
    std::string name = (forced && *forced) ? forced : "wlan0";
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return "wlan0";
    }
    return name;
}

using Counters = std::map<std::string, unsigned long long>;
Counters last;

unsigned long long ifaceStat(const char* iface, const char* name) {
    char line[32];
    const std::string path = std::string("/sys/class/net/") + iface + "/statistics/" + name;
    return readLine(path, line, sizeof(line)) ? std::strtoull(line, nullptr, 10) : 0;
}

/* /proc/net/snmp is pairs of lines, a header of names and then their values,
 * each starting with the table's name: "Udp: InDatagrams NoPorts ...". Kept as
 * "Udp.NoPorts". */
void readSnmp(Counters& out) {
    std::ifstream f("/proc/net/snmp");
    std::string names, values;
    while (std::getline(f, names) && std::getline(f, values)) {
        std::istringstream n(names), v(values);
        std::string table, name, value;
        n >> table;
        v >> value;   // the same table name again
        table.pop_back();   // the ':'
        while (n >> name && v >> value) out[table + "." + name] = std::strtoull(value.c_str(), nullptr, 10);
    }
}

/* snmp6 is one counter per line: "Udp6NoPorts   12". */
void readSnmp6(Counters& out) {
    std::ifstream f("/proc/net/snmp6");
    std::string name;
    unsigned long long value;
    while (f >> name >> value) out[name] = value;
}

/* ARP requests heard, broadcast and addressed. /proc/net/stat/arp_cache is a
 * header and one line of hex per processor. */
void readArp(Counters& out) {
    std::ifstream f("/proc/net/stat/arp_cache");
    std::string header;
    if (!std::getline(f, header)) return;
    std::istringstream h(header);
    std::vector<std::string> names;
    for (std::string w; h >> w;) names.push_back(w);
    for (std::string l; std::getline(f, l);) {
        std::istringstream v(l);
        std::string hex;
        for (size_t i = 0; i < names.size() && v >> hex; ++i) {
            out["arp." + names[i]] += std::strtoull(hex.c_str(), nullptr, 16);
        }
    }
}

}   // namespace

namespace HostNetTraffic {

void appendMinute(std::ostream& out, double seconds) {
    char line[64];
    const std::string iface = interfaceName();
    if (!readLine("/sys/class/net/" + iface + "/operstate", line, sizeof(line))) return;

    Counters now;
    now["rx"] = ifaceStat(iface.c_str(), "rx_packets");
    now["tx"] = ifaceStat(iface.c_str(), "tx_packets");
    now["mcast"] = ifaceStat(iface.c_str(), "multicast");
    now["lo"] = ifaceStat("lo", "tx_packets");
    readSnmp(now);
    readSnmp6(now);
    readArp(now);

    if (seconds > 0 && !last.empty()) {
        auto d = [&](const char* key) -> unsigned long long {
            const auto n = now.find(key);
            const auto l = last.find(key);
            if (n == now.end() || l == last.end() || n->second < l->second) return 0;
            return n->second - l->second;
        };
        const unsigned long long rx = d("rx"), mcast = d("mcast");
        out << " wifi_pkts/min=" << rx << "/" << d("tx");
        /* Arriving: broadcast or multicast against addressed to us; then what
         * the protocols made of all of it. The protocol counters include the
         * loopback interface, whose traffic is lo= under the outgoing side. */
        out << " wifi_rx=bcast:" << mcast << ",ucast:" << (rx > mcast ? rx - mcast : 0)
            << ",arp_req:" << d("arp.rcv_probes_mcast") + d("arp.rcv_probes_ucast");
        out << " net_in=udp:" << d("Udp.InDatagrams") + d("Udp6InDatagrams")
            << ",udp_noport:" << d("Udp.NoPorts") + d("Udp6NoPorts")
            << ",tcp:" << d("Tcp.InSegs") << ",icmp:" << d("Icmp.InMsgs")
            << ",icmp6:" << d("Icmp6InMsgs") << ",ip6:" << d("Ip6InReceives");
        out << " net_out=udp:" << d("Udp.OutDatagrams") + d("Udp6OutDatagrams")
            << ",tcp:" << d("Tcp.OutSegs") << ",icmp:" << d("Icmp.OutMsgs")
            << ",icmp6:" << d("Icmp6OutMsgs") << ",lo:" << d("lo");
    }
    last.swap(now);

    const std::string ps = commandLine(("iw dev " + iface + " get power_save 2>/dev/null").c_str());
    const size_t colon = ps.find(':');
    if (colon != std::string::npos) out << " wifi_ps=" << ps.substr(colon + 2);
}

void logSockets() {
    /* inode -> "udp:68", from every socket table. Only the LOCAL port: the
     * far end is someone else's address and is never read into this. */
    std::map<std::string, std::string> byInode;
    for (const char* table : {"tcp", "tcp6", "udp", "udp6", "raw", "raw6"}) {
        std::ifstream f(std::string("/proc/net/") + table);
        std::string l;
        std::getline(f, l);   // header
        while (std::getline(f, l)) {
            std::istringstream w(l);
            std::string slot, local, remote, state, skip, inode;
            w >> slot >> local >> remote >> state;
            for (int i = 0; i < 5; ++i) w >> skip;   // tx:rx, tr:when, retrnsmt, uid, timeout
            w >> inode;
            const size_t colon = local.rfind(':');
            if (colon == std::string::npos || inode.empty() || inode == "0") continue;
            std::string proto(table);
            if (proto.back() == '6') proto.pop_back();
            std::string text = proto + ":" + std::to_string(std::strtoul(local.c_str() + colon + 1, nullptr, 16));
            /* Bound to loopback only: it cannot be what the radio carries. */
            /* 127.x.x.x: the kernel writes the address least significant
             * byte first, so its first octet is the fourth hex pair. */
            if ((colon == 8 && local.compare(6, 2, "7F") == 0) ||
                local.compare(0, 32, "00000000000000000000000001000000") == 0) {
                text += "lo";
            }
            if (proto == "tcp" && state == "0A") text += "L";   // listening
            byInode[inode] = text;
        }
    }
    {
        std::ifstream f("/proc/net/packet");
        std::string l;
        std::getline(f, l);
        while (std::getline(f, l)) {
            std::istringstream w(l);
            std::string word, inode;
            while (w >> word) inode = word;   // the last column
            if (!inode.empty()) byInode[inode] = "packet";
        }
    }

    std::map<std::string, std::set<std::string>> byProcess;
    for (const std::string& pid : listDir("/proc")) {
        if (!std::isdigit(static_cast<unsigned char>(pid[0]))) continue;
        char comm[32];
        if (!readLine("/proc/" + pid + "/comm", comm, sizeof(comm))) continue;
        const std::string fdDir = "/proc/" + pid + "/fd";
        for (const std::string& fd : listDir(fdDir)) {
            char target[64];
            const ssize_t n = ::readlink((fdDir + "/" + fd).c_str(), target, sizeof(target) - 1);
            if (n <= 8) continue;
            target[n] = '\0';
            if (std::strncmp(target, "socket:[", 8) != 0) continue;
            std::string inode(target + 8);
            if (!inode.empty() && inode.back() == ']') inode.pop_back();
            const auto s = byInode.find(inode);
            if (s != byInode.end()) byProcess[comm].insert(s->second);
        }
    }

    std::string line;
    for (const auto& [name, sockets] : byProcess) {
        std::string clean = name;
        for (char& c : clean) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '.';
        }
        line += " " + clean + "=";
        bool first = true;
        for (const std::string& s : sockets) {
            line += (first ? "" : ",") + s;
            first = false;
        }
    }
    std::printf("[drain] sockets:%s\n", line.empty() ? " (none visible)" : line.c_str());
}

}   // namespace HostNetTraffic
