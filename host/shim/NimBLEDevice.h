// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The slice of NimBLE-Arduino that src/hal/BleBeacon.cpp and BleScanner.cpp
 * use, implemented over Linux's raw HCI socket (host/src/HostNimBLE.cpp), so
 * the firmware's beacon and scanner run unchanged on a Linux handheld and can
 * see, and be seen by, Braino boards.
 *
 * Only what those two files call exists: start and stop advertising one
 * non-connectable payload, and a passive scan that hands every advertising
 * report to a callback. No GATT, no connections, no pairing -- the beacon
 * never accepts a connection on a board either.
 *
 * One deliberate difference, in the device's favour: the advertisement goes
 * out from a RANDOM STATIC address, generated once and kept on this device,
 * never the controller's factory address. A board advertises from its
 * burned-in BT MAC; a handheld has no reason to put a hardware identifier on
 * the air, and the repository's identifier rule says not to. */

#include <cstddef>
#include <cstdint>
#include <string>

typedef enum {
    ESP_PWR_LVL_N12 = 0,
    ESP_PWR_LVL_N9,
    ESP_PWR_LVL_N6,
    ESP_PWR_LVL_N3,
    ESP_PWR_LVL_N0,
    ESP_PWR_LVL_P3,
    ESP_PWR_LVL_P6,
    ESP_PWR_LVL_P9,
} esp_power_level_t;

#define BLE_GAP_CONN_MODE_NON 0
#define BLE_GAP_CONN_MODE_DIR 1
#define BLE_GAP_CONN_MODE_UND 2

class NimBLEAddress {
public:
    NimBLEAddress() = default;
    explicit NimBLEAddress(const uint8_t* bytes);
    std::string toString() const;

private:
    uint8_t b_[6] = {0, 0, 0, 0, 0, 0};   // most significant first
};

class NimBLEAdvertisementData {
public:
    void addData(const std::string& raw) { payload_ += raw; }
    const std::string& getPayload() const { return payload_; }

private:
    std::string payload_;
};

class NimBLEAdvertising {
public:
    void setAdvertisementData(const NimBLEAdvertisementData& data) { payload_ = data.getPayload(); }
    void setScanResponse(bool) {}
    void setAdvertisementType(uint8_t type) { type_ = type; }
    void setMinInterval(uint16_t units) { minUnits_ = units; }
    void setMaxInterval(uint16_t units) { maxUnits_ = units; }
    bool start();
    bool stop();
    bool isAdvertising() const { return advertising_; }

private:
    std::string payload_;
    uint8_t type_ = BLE_GAP_CONN_MODE_NON;
    uint16_t minUnits_ = 1600;
    uint16_t maxUnits_ = 1600;
    bool advertising_ = false;
};

class NimBLEAdvertisedDevice {
public:
    const uint8_t* getPayload() const { return payload_; }
    size_t getPayloadLength() const { return length_; }
    int getRSSI() const { return rssi_; }

    void hostSet(const uint8_t* payload, size_t length, int rssi) {
        payload_ = payload;
        length_ = length;
        rssi_ = rssi;
    }

private:
    const uint8_t* payload_ = nullptr;
    size_t length_ = 0;
    int rssi_ = 0;
};

class NimBLEAdvertisedDeviceCallbacks {
public:
    virtual ~NimBLEAdvertisedDeviceCallbacks() = default;
    virtual void onResult(NimBLEAdvertisedDevice* device) = 0;
};

class NimBLEScanResults {};

class NimBLEScan {
public:
    void setAdvertisedDeviceCallbacks(NimBLEAdvertisedDeviceCallbacks* cb, bool wantDuplicates = false) {
        (void)wantDuplicates;
        callbacks_ = cb;
    }
    void setActiveScan(bool active) { active_ = active; }
    void setInterval(uint16_t ms) { intervalMs_ = ms; }
    void setWindow(uint16_t ms) { windowMs_ = ms; }
    void setDuplicateFilter(bool on) { duplicateFilter_ = on; }
    void setMaxResults(int) {}
    bool start(uint32_t durationSeconds, void (*complete)(NimBLEScanResults) = nullptr,
               bool isContinue = false);
    bool stop();
    bool isScanning() const { return scanning_; }
    void clearResults() {}

    NimBLEAdvertisedDeviceCallbacks* hostCallbacks() const { return callbacks_; }

private:
    NimBLEAdvertisedDeviceCallbacks* callbacks_ = nullptr;
    bool active_ = false;
    uint16_t intervalMs_ = 100;
    uint16_t windowMs_ = 100;
    bool duplicateFilter_ = false;
    bool scanning_ = false;
};

class NimBLEDevice {
public:
    static void init(const std::string& deviceName);
    static bool getInitialized();
    static void deinit(bool clearAll = false);
    static void setPower(esp_power_level_t level) { (void)level; }
    static NimBLEAdvertising* getAdvertising();
    static NimBLEScan* getScan();
    static NimBLEAddress getAddress();
};
