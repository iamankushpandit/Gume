// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include "IPAddress.h"

/* A UDP socket, for the one thing the firmware sends over UDP: its NTP
 * probe. Non-blocking, like lwIP's: parsePacket() answers 0 until a reply
 * has arrived, and the firmware polls it. */
class WiFiUDP {
public:
    ~WiFiUDP() { stop(); }
    uint8_t begin(uint16_t localPort);
    void stop();
    int beginPacket(IPAddress ip, uint16_t port);
    int beginPacket(const char* host, uint16_t port);
    size_t write(const uint8_t* data, size_t len);
    int endPacket();
    int parsePacket();
    int read(uint8_t* out, size_t len);
    int read(char* out, size_t len) { return read(reinterpret_cast<uint8_t*>(out), len); }

private:
    int fd_ = -1;
    uint32_t destIp_ = 0;     // network byte order
    uint16_t destPort_ = 0;
    uint8_t tx_[512] = {0};
    size_t txLen_ = 0;
    uint8_t rx_[512] = {0};
    size_t rxLen_ = 0;
    size_t rxAt_ = 0;
};
