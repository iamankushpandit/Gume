// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* WiFi on the host is the computer's own radio, through the operating system
 * that owns it -- see host/src/HostWifiOs.h. A scan lists the networks the
 * OS can see, begin() asks the OS to join one, and status() reports what the
 * OS has, so the firmware's Wi-Fi screen, time sync, timezone lookup and
 * update check run their real code against a real connection.
 *
 * What it does NOT do is add anything. These calls only answer the firmware;
 * every packet that leaves is one the firmware's own code chose to send,
 * gated on its own WL_CONNECTED checks, so the closed list of outbound flows
 * in CLAUDE.md is exactly as true here as on a board. mode(WIFI_OFF) does not
 * switch the computer's radio off: the OS owns its power, and other programs
 * share it.
 *
 * With no wireless interface (WSL, a wired desktop) status() is WL_NO_SHIELD
 * and a scan finds nothing, as on a board with no radio. */

#include <cstddef>
#include <cstdint>
#include <string>
#include "IPAddress.h"
#include "WString.h"

typedef enum {
    WL_NO_SHIELD = 255,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6,
} wl_status_t;

typedef enum {
    WIFI_OFF = 0,
    WIFI_STA = 1,
    WIFI_AP = 2,
    WIFI_AP_STA = 3,
    WIFI_MODE_NULL = WIFI_OFF,
    WIFI_MODE_STA = WIFI_STA,
} wifi_mode_t;
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 } wifi_auth_mode_t;

class WiFiClass {
public:
    wl_status_t begin(const char* ssid, const char* password = nullptr);
    bool disconnect(bool = false, bool = false) { return true; }
    bool mode(wifi_mode_t m) { mode_ = m; return true; }
    wifi_mode_t getMode() { return mode_; }
    void persistent(bool) {}
    bool setSleep(bool) { return true; }
    bool setAutoReconnect(bool) { return true; }
    wl_status_t status();
    bool isConnected() { return status() == WL_CONNECTED; }

    int16_t scanNetworks(bool async = false, bool showHidden = false, bool passive = false,
                         uint32_t msPerChannel = 300);
    void scanDelete();
    String SSID();
    String SSID(int i);
    int32_t RSSI();
    int32_t RSSI(int i);
    wifi_auth_mode_t encryptionType(int i);
    int32_t channel();
    String BSSIDstr();
    /* Never the computer's own MAC: a hardware identifier stays off the
     * screen, where it would end up in a screenshot. */
    String macAddress() { return String("--"); }
    const char* getHostname() { return "braino-host"; }
    bool setHostname(const char*) { return true; }

    IPAddress localIP();
    IPAddress gatewayIP();
    IPAddress subnetMask();
    IPAddress dnsIP(uint8_t = 0);
    int hostByName(const char* host, IPAddress& out);

private:
    wifi_mode_t mode_ = WIFI_STA;
};

extern WiFiClass WiFi;

/* The body of an HTTP response, read the way the firmware reads a stream. */
class WiFiClient {
public:
    virtual ~WiFiClient() = default;
    void setTimeout(uint32_t) {}
    int available() { return static_cast<int>(data_.size() - at_); }
    int read() { return at_ < data_.size() ? static_cast<unsigned char>(data_[at_++]) : -1; }
    size_t readBytes(char* out, size_t len);
    size_t readBytes(uint8_t* out, size_t len) { return readBytes(reinterpret_cast<char*>(out), len); }
    bool connected() { return at_ < data_.size(); }
    void stop() { data_.clear(); at_ = 0; }

    void hostLoad(const std::string& body) { data_ = body; at_ = 0; }

private:
    std::string data_;
    size_t at_ = 0;
};
