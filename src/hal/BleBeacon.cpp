#include "BleBeacon.h"
#include "Watchdog.h"

#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <string>

namespace BleBeacon {
namespace {

Advertisement adv_;
bool built_ = false;
bool enabled_ = false;
bool active_ = false;
uint8_t idBytes_[2] = {0, 0};   // the two MAC bytes behind adv_.deviceId

/* GAP AD types (Core Supplement, Part A). */
constexpr uint8_t AD_FLAGS = 0x01;
constexpr uint8_t AD_UUID16_COMPLETE = 0x03;
constexpr uint8_t AD_NAME_COMPLETE = 0x09;
constexpr uint8_t AD_SERVICE_DATA_16 = 0x16;
constexpr uint8_t AD_MANUFACTURER = 0xFF;

/* LE General Discoverable + BR/EDR Not Supported. Set even though we are
 * non-connectable, so ordinary phone scanners will list the device at all. */
constexpr uint8_t FLAGS_VALUE = 0x06;

/** Append one AD structure. Returns false (and appends nothing) if it won't fit. */
bool appendAd(Advertisement& a, uint8_t type, const uint8_t* data, uint8_t len) {
    const uint16_t need = static_cast<uint16_t>(len) + 2;
    if (a.payloadLen + need > PAYLOAD_MAX) {
        return false;
    }
    a.payload[a.payloadLen++] = static_cast<uint8_t>(len + 1);   // type + data
    a.payload[a.payloadLen++] = type;
    for (uint8_t i = 0; i < len; ++i) {
        a.payload[a.payloadLen++] = data[i];
    }
    return true;
}

/* Derive the short device id from the factory MAC. It is a hardware serial,
 * not anything the child or parent typed, and it is stable across reboots so a
 * parent can recognise their own device in a scanner. */
void deriveIdentity(Advertisement& a) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    idBytes_[0] = mac[4];
    idBytes_[1] = mac[5];
    snprintf(a.deviceId, sizeof(a.deviceId), "%02X%02X", idBytes_[0], idBytes_[1]);
    snprintf(a.familyId, sizeof(a.familyId), "%s", FAMILY_ID);
    snprintf(a.deviceName, sizeof(a.deviceName), "%s-%s", FAMILY_ID, a.deviceId);
}

/* Compile the struct into AD structures. This buffer is what start() gives the
 * controller and what System Info reads back -- keep it that way. */
void buildPayload(Advertisement& a) {
    a.payloadLen = 0;

    const uint8_t flags = FLAGS_VALUE;
    appendAd(a, AD_FLAGS, &flags, 1);

    appendAd(a, AD_NAME_COMPLETE,
             reinterpret_cast<const uint8_t*>(a.deviceName),
             static_cast<uint8_t>(strlen(a.deviceName)));

    /* Manufacturer data: company id (little endian), family tag, layout
     * version, device id. Five application-defined bytes, all of which the
     * System Info screen decodes field by field. */
    uint8_t mfg[8];
    uint8_t n = 0;
    mfg[n++] = static_cast<uint8_t>(a.companyId & 0xFF);
    mfg[n++] = static_cast<uint8_t>(a.companyId >> 8);
    mfg[n++] = static_cast<uint8_t>(FAMILY_TAG[0]);
    mfg[n++] = static_cast<uint8_t>(FAMILY_TAG[1]);
    mfg[n++] = PAYLOAD_VERSION;
    mfg[n++] = idBytes_[0];
    mfg[n++] = idBytes_[1];
    a.manufacturerLen = n;
    memcpy(a.manufacturerData, mfg, n);
    appendAd(a, AD_MANUFACTURER, a.manufacturerData, a.manufacturerLen);

    /* No service UUID or service data is advertised. Left at zero length so
     * the UI omits the rows entirely rather than showing an empty value. */
    if (a.serviceUuid16 != 0) {
        const uint8_t uuid[2] = {static_cast<uint8_t>(a.serviceUuid16 & 0xFF),
                                 static_cast<uint8_t>(a.serviceUuid16 >> 8)};
        appendAd(a, AD_UUID16_COMPLETE, uuid, 2);
        if (a.serviceDataLen > 0) {
            uint8_t sd[10];
            sd[0] = uuid[0];
            sd[1] = uuid[1];
            memcpy(sd + 2, a.serviceData, a.serviceDataLen);
            appendAd(a, AD_SERVICE_DATA_16, sd, static_cast<uint8_t>(a.serviceDataLen + 2));
        }
    }
}

/* NimBLE takes the interval in 0.625ms units. */
uint16_t intervalUnits(uint16_t ms) {
    const uint32_t units = (static_cast<uint32_t>(ms) * 1000UL) / 625UL;
    return static_cast<uint16_t>(constrain(units, 32UL, 16384UL));
}

void startRadio() {
    if (active_) {
        return;
    }
    /* Bringing the controller up blocks for a couple of hundred milliseconds,
     * which from the loop task looks exactly like a hang. */
    Watchdog::Pause guard;

    if (!NimBLEDevice::getInitialized()) {
        NimBLEDevice::init(adv_.deviceName);
    }
    NimBLEDevice::setPower(static_cast<esp_power_level_t>(ESP_PWR_LVL_P3));

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData data;
    /* Raw bytes, not the helper setters: the controller must receive exactly
     * the buffer System Info will display, byte for byte. */
    data.addData(std::string(reinterpret_cast<const char*>(adv_.payload), adv_.payloadLen));
    advertising->setAdvertisementData(data);
    advertising->setScanResponse(false);
    advertising->setAdvertisementType(adv_.connectable ? BLE_GAP_CONN_MODE_UND
                                                       : BLE_GAP_CONN_MODE_NON);
    const uint16_t units = intervalUnits(adv_.advIntervalMs);
    advertising->setMinInterval(units);
    advertising->setMaxInterval(units);

    active_ = advertising->start();
    Serial.printf("[ble] advertising %s as %s (%u byte payload)\n",
                  active_ ? "started" : "FAILED", adv_.deviceName, adv_.payloadLen);
}

void stopRadio() {
    if (!NimBLEDevice::getInitialized()) {
        active_ = false;
        return;
    }
    Watchdog::Pause guard;
    NimBLEDevice::getAdvertising()->stop();
    /* Fully tear the stack down: leaving it initialised holds ~30KB of heap
     * that the games would rather have, and "off" should mean off. */
    NimBLEDevice::deinit(true);
    active_ = false;
    Serial.println("[ble] advertising stopped");
}

}   // namespace

void begin(bool startEnabled) {
    if (!built_) {
        deriveIdentity(adv_);
        buildPayload(adv_);
        built_ = true;
    }
    enabled_ = startEnabled;
    if (enabled_) {
        startRadio();
    }
}

void setEnabled(bool on) {
    if (!built_) {
        begin(on);
        return;
    }
    if (on == enabled_) {
        return;
    }
    enabled_ = on;
    if (on) {
        startRadio();
    } else {
        stopRadio();
    }
}

bool enabled() { return enabled_; }
bool active() { return active_; }
const Advertisement& configured() { return adv_; }
const Advertisement* broadcasting() { return active_ ? &adv_ : nullptr; }

String address() {
    if (!NimBLEDevice::getInitialized()) {
        return String();
    }
    return String(NimBLEDevice::getAddress().toString().c_str());
}

const char* modeText() {
    return adv_.connectable ? "BLE Connectable" : "BLE Advertise Only";
}

String toHex(const uint8_t* data, uint8_t len) {
    String out;
    out.reserve(static_cast<unsigned>(len) * 3);
    char buf[4];
    for (uint8_t i = 0; i < len; ++i) {
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        if (i > 0) out += ' ';
        out += buf;
    }
    return out;
}

}   // namespace BleBeacon
