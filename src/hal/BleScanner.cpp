#include "BleScanner.h"
#include "Watchdog.h"

#include <NimBLEDevice.h>
#include <string.h>

namespace BleScan {
namespace {

/* Scan window/interval, in milliseconds. 300ms of listening every 900ms is
 * enough to catch a 1000ms beacon within a couple of cycles while leaving the
 * radio idle two thirds of the time -- this shares an antenna with Wi-Fi. */
constexpr uint16_t SCAN_INTERVAL_MS = 900;
constexpr uint16_t SCAN_WINDOW_MS = 300;

/* AD types we care about (Core Supplement, Part A). */
constexpr uint8_t AD_NAME_SHORT = 0x08;
constexpr uint8_t AD_NAME_COMPLETE = 0x09;
constexpr uint8_t AD_MANUFACTURER = 0xFF;

Sighting table_[MAX_SIGHTINGS];
uint8_t used_ = 0;
uint32_t generation_ = 0;
bool enabled_ = false;

/* The scan callback runs on the NimBLE host task, not the loop task, so every
 * touch of the table above is bracketed by this. The guarded regions are a
 * handful of field writes over at most eight entries -- short enough that a
 * spinlock is the right primitive and a mutex would be the wrong one. */
portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;

/** Case-sensitive prefix test over a non-terminated buffer. */
bool hasPrefix(const uint8_t* text, uint8_t len, const char* prefix) {
    const size_t n = strlen(prefix);
    if (len < n) {
        return false;
    }
    return memcmp(text, prefix, n) == 0;
}

/* Record one decoded peer. Called from the host task with the lock held by the
 * caller. Updating in place keeps a device that is merely reporting a new
 * score from looking like a new arrival. */
void record(const BleBeacon::Observation& obs, int8_t rssi, uint32_t nowMs) {
    for (uint8_t i = 0; i < used_; ++i) {
        if (strncmp(table_[i].deviceId, obs.deviceId, sizeof(table_[i].deviceId)) == 0) {
            table_[i].sharesActivity = obs.sharesActivity;
            table_[i].gameIndex = obs.gameIndex;
            table_[i].bestScore = obs.bestScore;
            table_[i].rssi = rssi;
            table_[i].lastSeenMs = nowMs;
            ++generation_;
            return;
        }
    }

    uint8_t slot = used_;
    if (used_ >= MAX_SIGHTINGS) {
        /* Full: give the place to the nearer device. A weak sighting is the
         * one most likely to be somebody walking past outside. */
        uint8_t weakest = 0;
        for (uint8_t i = 1; i < MAX_SIGHTINGS; ++i) {
            if (table_[i].rssi < table_[weakest].rssi) {
                weakest = i;
            }
        }
        if (table_[weakest].rssi >= rssi) {
            return;
        }
        slot = weakest;
    } else {
        ++used_;
    }

    Sighting& s = table_[slot];
    memset(&s, 0, sizeof(s));
    snprintf(s.deviceId, sizeof(s.deviceId), "%s", obs.deviceId);
    s.sharesActivity = obs.sharesActivity;
    s.gameIndex = obs.gameIndex;
    s.bestScore = obs.bestScore;
    s.rssi = rssi;
    s.lastSeenMs = nowMs;
    ++generation_;
}

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice* device) override {
        if (device == nullptr) {
            return;
        }
        /* Walk the raw AD structures rather than the library's std::string
         * accessors: this runs at whatever rate the air is busy, and a pair of
         * heap allocations per advertisement is precisely the churn that
         * fragments this heap over an afternoon. */
        const uint8_t* payload = device->getPayload();
        const size_t total = device->getPayloadLength();
        if (payload == nullptr || total == 0) {
            return;
        }

        bool nameMatched = false;
        BleBeacon::Observation obs;
        bool decoded = false;

        size_t i = 0;
        while (i < total) {
            const uint8_t len = payload[i];
            if (len == 0 || i + 1 + len > total) {
                break;
            }
            const uint8_t type = payload[i + 1];
            const uint8_t* data = payload + i + 2;
            const uint8_t dataLen = static_cast<uint8_t>(len - 1);

            if (type == AD_NAME_COMPLETE || type == AD_NAME_SHORT) {
                nameMatched = hasPrefix(data, dataLen, BleBeacon::NAME_PREFIX);
            } else if (type == AD_MANUFACTURER) {
                decoded = BleBeacon::decode(data, dataLen, obs);
            }
            i += static_cast<size_t>(len) + 1;
        }

        /* Both tests, not either: the name is what a human recognises in a
         * scanner and the manufacturer block is what carries the fields, and
         * anything advertising one without the other is not a Braino. */
        if (!nameMatched || !decoded) {
            return;
        }

        const uint32_t now = millis();
        const int8_t rssi = static_cast<int8_t>(constrain(device->getRSSI(), -127, 20));
        portENTER_CRITICAL(&lock_);
        record(obs, rssi, now);
        portEXIT_CRITICAL(&lock_);
    }
};

ScanCallbacks callbacks_;

void startScan() {
    if (!NimBLEDevice::getInitialized()) {
        return;
    }
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan->isScanning()) {
        return;
    }
    /* Bringing the scan up talks to the controller and blocks briefly, which
     * from the loop task looks exactly like a stall. */
    Watchdog::Pause guard;
    scan->setAdvertisedDeviceCallbacks(&callbacks_, /*wantDuplicates=*/true);
    /* Passive: never transmit a scan request. Listening must not change what
     * this device puts on air. */
    scan->setActiveScan(false);
    scan->setInterval(SCAN_INTERVAL_MS);
    scan->setWindow(SCAN_WINDOW_MS);
    scan->setDuplicateFilter(false);
    /* Zero results kept: the callback is the only consumer, and the library's
     * results vector would otherwise grow one heap-allocated device per peer
     * for nobody to read. */
    scan->setMaxResults(0);
    scan->start(0, nullptr, false);   // 0 == until stopped
}

void stopScan() {
    if (!NimBLEDevice::getInitialized()) {
        return;
    }
    Watchdog::Pause guard;
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->stop();
    scan->setAdvertisedDeviceCallbacks(nullptr);
}

}   // namespace

void setEnabled(bool on) {
    if (on == enabled_) {
        /* Still worth a start attempt: the caller may be re-enabling after the
         * controller came up underneath us. */
        if (on) {
            startScan();
        }
        return;
    }
    enabled_ = on;
    if (on) {
        startScan();
    } else {
        stopScan();
        clear();
    }
    Serial.printf("[ble] nearby scan %s\n", on ? "started" : "stopped");
}

bool enabled() { return enabled_; }

bool scanning() {
    return enabled_ && NimBLEDevice::getInitialized() && NimBLEDevice::getScan()->isScanning();
}

void tick() {
    if (!enabled_) {
        return;
    }
    /* The library restarts a duration-0 scan itself, but a host reset or a
     * beacon toggle can still leave it down. Re-arming here is idempotent. */
    if (NimBLEDevice::getInitialized() && !NimBLEDevice::getScan()->isScanning()) {
        startScan();
    }

    const uint32_t now = millis();
    portENTER_CRITICAL(&lock_);
    uint8_t out = 0;
    for (uint8_t i = 0; i < used_; ++i) {
        if (now - table_[i].lastSeenMs < SIGHTING_TTL_MS) {
            if (out != i) {
                table_[out] = table_[i];
            }
            ++out;
        }
    }
    if (out != used_) {
        used_ = out;
        ++generation_;
    }
    portEXIT_CRITICAL(&lock_);
}

void clear() {
    portENTER_CRITICAL(&lock_);
    used_ = 0;
    ++generation_;
    portEXIT_CRITICAL(&lock_);
}

uint8_t count() {
    portENTER_CRITICAL(&lock_);
    const uint8_t n = used_;
    portEXIT_CRITICAL(&lock_);
    return n;
}

/* Returned by value: the table is written from the host task, so handing out a
 * reference would let a caller read a row while it is being rewritten. */
Sighting at(uint8_t index) {
    Sighting copy;
    portENTER_CRITICAL(&lock_);
    if (index < used_) {
        /* Strongest first, resolved here rather than by keeping the table
         * sorted, so the host-task write path stays a straight field update. */
        uint8_t order[MAX_SIGHTINGS];
        for (uint8_t i = 0; i < used_; ++i) {
            order[i] = i;
        }
        for (uint8_t i = 1; i < used_; ++i) {
            const uint8_t key = order[i];
            int8_t j = static_cast<int8_t>(i - 1);
            while (j >= 0 && table_[order[j]].rssi < table_[key].rssi) {
                order[j + 1] = order[j];
                --j;
            }
            order[j + 1] = key;
        }
        copy = table_[order[index]];
    }
    portEXIT_CRITICAL(&lock_);
    return copy;
}

uint32_t generation() {
    portENTER_CRITICAL(&lock_);
    const uint32_t g = generation_;
    portEXIT_CRITICAL(&lock_);
    return g;
}

}   // namespace BleScan
