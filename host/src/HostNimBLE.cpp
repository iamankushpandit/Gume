// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* NimBLE's advertiser and scanner, on a Linux raw HCI socket. See
 * host/shim/NimBLEDevice.h for what is promised.
 *
 * Everything goes through the kernel's own Bluetooth socket API with the few
 * constants it needs written out here, so there is no BlueZ library to link
 * and nothing extra to ship. The commands are the Bluetooth 4.0 legacy LE set
 * -- every controller since speaks them, and they are what a Braino board's
 * NimBLE puts on the air too:
 *
 *   LE Set Random Address      the advertiser's address (random static)
 *   LE Set Advertising Params  interval, ADV_NONCONN_IND, random address
 *   LE Set Advertising Data    the firmware's 31-byte payload, verbatim
 *   LE Set Advertise Enable
 *   LE Set Scan Params/Enable  passive: no scan requests are ever sent
 *
 * One reader thread takes every event the controller sends: command replies
 * are matched to the command waiting for them, and LE advertising reports go
 * to the scanner's callback -- BleScanner.cpp's own, which locks its table
 * exactly as it does when NimBLE's host task calls it on a board.
 *
 * BlueZ's daemon may be running beside this. A raw socket shares the
 * controller with it rather than taking it over; nothing here asks bluetoothd
 * for anything or changes its pairings. */

#include <NimBLEDevice.h>

#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "HostPlatform.h"

namespace {

constexpr int AF_BT = 31;           // AF_BLUETOOTH
constexpr int BTPROTO_HCI_ = 1;
constexpr int SOL_HCI_ = 0;
constexpr int HCI_FILTER_ = 2;
constexpr unsigned short HCI_CHANNEL_RAW_ = 0;
constexpr unsigned long HCIDEVUP_ = 0x400448c9;   // _IOW('H', 201, int)

constexpr uint8_t PKT_COMMAND = 0x01;
constexpr uint8_t PKT_EVENT = 0x04;
constexpr uint8_t EVT_CMD_COMPLETE = 0x0E;
constexpr uint8_t EVT_CMD_STATUS = 0x0F;
constexpr uint8_t EVT_LE_META = 0x3E;
constexpr uint8_t LE_ADV_REPORT = 0x02;

constexpr uint16_t OP_LE_SET_RANDOM_ADDR = 0x2005;
constexpr uint16_t OP_LE_SET_ADV_PARAMS = 0x2006;
constexpr uint16_t OP_LE_SET_ADV_DATA = 0x2008;
constexpr uint16_t OP_LE_SET_ADV_ENABLE = 0x200A;
constexpr uint16_t OP_LE_SET_SCAN_PARAMS = 0x200B;
constexpr uint16_t OP_LE_SET_SCAN_ENABLE = 0x200C;

struct SockaddrHci {
    sa_family_t family;
    unsigned short dev;
    unsigned short channel;
};

struct HciFilter {
    uint32_t typeMask;
    uint32_t eventMask[2];
    uint16_t opcode;
};

int fd = -1;
bool initialised = false;
std::thread reader;
uint8_t address[6] = {0};   // most significant first, as printed

std::mutex commandMutex;   // one command in flight at a time
std::mutex replyMutex;
std::condition_variable replyCv;
uint16_t awaitedOpcode = 0;
bool replied = false;
uint8_t replyStatus = 0xFF;

NimBLEAdvertising advertising;
NimBLEScan scan;

int deviceIndex() {
    const char* env = std::getenv("BRAINO_HCI_DEV");
    return (env && *env) ? std::atoi(env) : 0;
}

void readerLoop() {
    ::pthread_setname_np(::pthread_self(), "ble-hci");
    uint8_t buf[300];
    NimBLEAdvertisedDevice device;
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return;   // the socket was closed by deinit()
        }
        if (n < 3 || buf[0] != PKT_EVENT) continue;
        const uint8_t event = buf[1];
        const uint8_t* p = buf + 3;
        const size_t plen = static_cast<size_t>(n) - 3;

        if (event == EVT_CMD_COMPLETE && plen >= 4) {
            const uint16_t op = static_cast<uint16_t>(p[1] | (p[2] << 8));
            std::lock_guard<std::mutex> lock(replyMutex);
            if (op == awaitedOpcode) {
                replyStatus = p[3];
                replied = true;
                replyCv.notify_all();
            }
        } else if (event == EVT_CMD_STATUS && plen >= 4) {
            const uint16_t op = static_cast<uint16_t>(p[2] | (p[3] << 8));
            std::lock_guard<std::mutex> lock(replyMutex);
            if (op == awaitedOpcode && p[0] != 0) {   // a status reply only matters as a refusal
                replyStatus = p[0];
                replied = true;
                replyCv.notify_all();
            }
        } else if (event == EVT_LE_META && plen >= 2 && p[0] == LE_ADV_REPORT) {
            /* num_reports, then each: event type, address type, address (6),
             * data length, data, RSSI. Walked with bounds checks: this is
             * bytes from the air, from anyone. */
            uint8_t reports = p[1];
            size_t at = 2;
            while (reports-- > 0 && at + 9 <= plen) {
                const uint8_t dataLen = p[at + 8];
                if (at + 9 + dataLen + 1 > plen) break;
                const uint8_t* data = p + at + 9;
                const int8_t rssi = static_cast<int8_t>(p[at + 9 + dataLen]);
                NimBLEAdvertisedDeviceCallbacks* cb = scan.hostCallbacks();
                if (scan.isScanning() && cb != nullptr) {
                    device.hostSet(data, dataLen, rssi);
                    cb->onResult(&device);
                }
                at += 9 + dataLen + 1;
            }
        }
    }
}

/* Send one HCI command and wait for the controller's answer. True on
 * status 0. */
bool command(uint16_t opcode, const uint8_t* params, uint8_t len) {
    if (fd < 0) return false;
    std::lock_guard<std::mutex> one(commandMutex);
    uint8_t pkt[4 + 255];
    pkt[0] = PKT_COMMAND;
    pkt[1] = static_cast<uint8_t>(opcode & 0xFF);
    pkt[2] = static_cast<uint8_t>(opcode >> 8);
    pkt[3] = len;
    if (len) std::memcpy(pkt + 4, params, len);
    {
        std::lock_guard<std::mutex> lock(replyMutex);
        awaitedOpcode = opcode;
        replied = false;
        replyStatus = 0xFF;
    }
    if (::write(fd, pkt, 4u + len) != static_cast<ssize_t>(4u + len)) return false;
    std::unique_lock<std::mutex> lock(replyMutex);
    replyCv.wait_for(lock, std::chrono::milliseconds(1000), [] { return replied; });
    const bool ok = replied && replyStatus == 0;
    if (!ok) {
        std::printf("[ble] host: HCI 0x%04x %s (status 0x%02x)\n", opcode,
                    replied ? "refused" : "unanswered", replyStatus);
        std::fflush(stdout);
    }
    awaitedOpcode = 0;
    return ok;
}

uint16_t msToUnits(uint16_t ms) {
    uint32_t units = static_cast<uint32_t>(ms) * 1000u / 625u;
    if (units < 4) units = 4;
    if (units > 0x4000) units = 0x4000;
    return static_cast<uint16_t>(units);
}

}   // namespace

/* ------------------------------------------------------------- address */

NimBLEAddress::NimBLEAddress(const uint8_t* bytes) { std::memcpy(b_, bytes, 6); }

std::string NimBLEAddress::toString() const {
    char text[18];
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", b_[0], b_[1], b_[2], b_[3],
                  b_[4], b_[5]);
    return std::string(text);
}

/* ------------------------------------------------------------- device */

void NimBLEDevice::init(const std::string&) {
    if (initialised) return;
    const int dev = deviceIndex();

    /* Power the controller up if the OS left it down; harmless if it is up. */
    const int ctl = ::socket(AF_BT, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI_);
    if (ctl < 0) {
        std::printf("[ble] host: no Bluetooth in this kernel (%s)\n", std::strerror(errno));
        std::fflush(stdout);
        return;
    }
    if (::ioctl(ctl, HCIDEVUP_, dev) < 0 && errno != EALREADY) {
        std::printf("[ble] host: hci%d will not come up (%s)\n", dev, std::strerror(errno));
        std::fflush(stdout);
    }
    ::close(ctl);

    fd = ::socket(AF_BT, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI_);
    SockaddrHci addr{static_cast<sa_family_t>(AF_BT), static_cast<unsigned short>(dev), HCI_CHANNEL_RAW_};
    if (fd < 0 || ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::printf("[ble] host: cannot open hci%d (%s)\n", dev, std::strerror(errno));
        std::fflush(stdout);
        if (fd >= 0) ::close(fd);
        fd = -1;
        return;
    }
    HciFilter filter{};
    filter.typeMask = 1u << PKT_EVENT;
    for (uint8_t e : {EVT_CMD_COMPLETE, EVT_CMD_STATUS, EVT_LE_META}) {
        filter.eventMask[e >> 5] |= 1u << (e & 31);
    }
    ::setsockopt(fd, SOL_HCI_, HCI_FILTER_, &filter, sizeof(filter));
    reader = std::thread(readerLoop);

    /* The controller outlives this program. A run that crashed left it
     * advertising, and it refuses new parameters until told to stop, which
     * reads as a beacon that never starts while the old payload stays on the
     * air. So nothing is assumed about what it is doing. */
    const uint8_t advertiseOff = 0;
    const uint8_t scanOff[2] = {0, 0};
    command(OP_LE_SET_ADV_ENABLE, &advertiseOff, 1);
    command(OP_LE_SET_SCAN_ENABLE, scanOff, 2);

    /* A random STATIC address: the device's own random radio id with the
     * two top bits set, as the Core spec requires of one. Never the
     * controller's factory address. HCI takes it least significant first. */
    HostPlatform::radioId(address);
    address[0] = static_cast<uint8_t>(address[0] | 0xC0);
    uint8_t le[6];
    for (int i = 0; i < 6; ++i) le[i] = address[5 - i];
    command(OP_LE_SET_RANDOM_ADDR, le, 6);

    initialised = true;
    std::printf("[ble] host: hci%d up, advertising as random static %s\n", dev,
                NimBLEAddress(address).toString().c_str());
    std::fflush(stdout);
}

bool NimBLEDevice::getInitialized() { return initialised; }

void NimBLEDevice::deinit(bool) {
    if (!initialised) return;
    advertising.stop();
    scan.stop();
    initialised = false;
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
    fd = -1;
    if (reader.joinable()) reader.join();
}

NimBLEAdvertising* NimBLEDevice::getAdvertising() { return &advertising; }
NimBLEScan* NimBLEDevice::getScan() { return &scan; }
NimBLEAddress NimBLEDevice::getAddress() { return NimBLEAddress(address); }

/* ------------------------------------------------------------- advertising */

bool NimBLEAdvertising::start() {
    if (!initialised) return false;
    if (advertising_) stop();
    uint8_t params[15] = {0};
    params[0] = static_cast<uint8_t>(minUnits_ & 0xFF);
    params[1] = static_cast<uint8_t>(minUnits_ >> 8);
    params[2] = static_cast<uint8_t>(maxUnits_ & 0xFF);
    params[3] = static_cast<uint8_t>(maxUnits_ >> 8);
    params[4] = type_ == BLE_GAP_CONN_MODE_NON ? 0x03 : 0x00;   // ADV_NONCONN_IND : ADV_IND
    params[5] = 0x01;                                           // own address: random
    params[13] = 0x07;                                          // all three channels
    uint8_t data[32] = {0};
    const size_t len = payload_.size() > 31 ? 31 : payload_.size();
    data[0] = static_cast<uint8_t>(len);
    std::memcpy(data + 1, payload_.data(), len);
    const uint8_t on = 1;
    advertising_ = command(OP_LE_SET_ADV_PARAMS, params, sizeof(params)) &&
                   command(OP_LE_SET_ADV_DATA, data, sizeof(data)) &&
                   command(OP_LE_SET_ADV_ENABLE, &on, 1);
    return advertising_;
}

bool NimBLEAdvertising::stop() {
    const uint8_t off = 0;
    if (initialised) command(OP_LE_SET_ADV_ENABLE, &off, 1);
    advertising_ = false;
    return true;
}

/* ------------------------------------------------------------- scanning */

bool NimBLEScan::start(uint32_t, void (*)(NimBLEScanResults), bool) {
    if (!initialised) return false;
    const uint16_t interval = msToUnits(intervalMs_);
    const uint16_t window = msToUnits(windowMs_ > intervalMs_ ? intervalMs_ : windowMs_);
    uint8_t params[7] = {0};
    params[0] = active_ ? 0x01 : 0x00;   // passive unless asked; the firmware never asks
    params[1] = static_cast<uint8_t>(interval & 0xFF);
    params[2] = static_cast<uint8_t>(interval >> 8);
    params[3] = static_cast<uint8_t>(window & 0xFF);
    params[4] = static_cast<uint8_t>(window >> 8);
    params[5] = 0x01;                   // own address: random
    const uint8_t off[2] = {0, 0};
    command(OP_LE_SET_SCAN_ENABLE, off, 2);   // a controller refuses new params while scanning
    const uint8_t on[2] = {1, static_cast<uint8_t>(duplicateFilter_ ? 1 : 0)};
    scanning_ = command(OP_LE_SET_SCAN_PARAMS, params, sizeof(params)) &&
                command(OP_LE_SET_SCAN_ENABLE, on, 2);
    std::printf("[ble] host: passive scan %s\n", scanning_ ? "running" : "FAILED");
    std::fflush(stdout);
    return scanning_;
}

bool NimBLEScan::stop() {
    const uint8_t off[2] = {0, 0};
    if (initialised) command(OP_LE_SET_SCAN_ENABLE, off, 2);
    scanning_ = false;
    return true;
}
