// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The host's real Wi-Fi radio, through the operating system that owns it.
 *
 * Linux keeps the radio: a network manager associates, a DHCP client gets the
 * address. So the firmware's WiFi calls are answered by asking that manager,
 * never by driving the interface underneath it. Two are understood --
 * NetworkManager through `nmcli` (desktop Linux) and wpa_supplicant through
 * `wpa_cli` (muOS and most handheld images) -- and the first one present wins.
 * With neither, or with no wireless interface at all (WSL, a wired desktop),
 * the radio is absent and the firmware sees WL_NO_SHIELD, exactly as a board
 * without Wi-Fi would.
 *
 * Link state (associated SSID, signal, address) is read straight from the
 * kernel rather than from either manager, so it is the same code whichever one
 * is in charge. A background thread refreshes it once a second; the firmware
 * polls status() every frame and must never wait on a subprocess to get it.
 *
 * Every subprocess is started with an argument vector and no shell, because an
 * SSID is text a stranger chose. */

#include <cstdint>
#include <string>

namespace HostWifiOs {

struct Link {
    bool radio = false;        // a wireless interface exists and a manager answers
    bool associated = false;
    bool hasAddress = false;   // an IPv4 address on the wireless interface
    std::string ssid;
    int rssi = 0;              // dBm
    int channel = 0;
    std::string bssid;
    uint32_t ip = 0;           // network byte order, as in_addr
    uint32_t netmask = 0;
    uint32_t gateway = 0;
    uint32_t dns = 0;
};

enum class JoinState : uint8_t { Idle, Joining, Failed, NoSuchNetwork };

struct ScanEntry {
    std::string ssid;
    int rssi = -100;
    bool secured = false;
    int channel = 0;
    std::string bssid;
};

/* Start the link poller. Cheap and idempotent; called on first use. */
void begin();

Link link();
JoinState joinState();
/* The SSID the firmware last asked to join, empty when it has not. */
std::string joinTarget();

/* Ask the manager to join `ssid`. Returns at once; the work runs on its own
 * thread and joinState()/link() report how it went. A join to the network
 * already up is a no-op rather than a reconnect. */
void join(const std::string& ssid, const std::string& password);

/* A fresh scan, blocking -- the firmware's own scan blocks for ~4 s on a
 * board and shows a "Scanning..." screen first for that reason. */
int scan(ScanEntry* out, int capacity);

/* Which manager is in charge, for System Info and the boot log. */
const char* backendName();

}   // namespace HostWifiOs
