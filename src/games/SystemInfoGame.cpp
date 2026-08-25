#include "SystemInfoGame.h"
#include "AppVersion.h"
#include "engine/AppRegistry.h"
#include "engine/NearbyPlay.h"
#include "hal/BleBeacon.h"
#include "hal/Board.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"

#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <lwip/opt.h>
#include <time.h>

#if MIB2_STATS
#include <lwip/netif.h>
#include <tcpip_adapter.h>
#elif LWIP_STATS && LINK_STATS
#include <lwip/stats.h>
#endif

namespace {
constexpr int16_t TAB_STRIP_H = 28;

int16_t tabStripY() {
    return static_cast<int16_t>(TOP_BAR_HEIGHT + 2);
}

String formatBytes(uint32_t bytes) {
    if (bytes >= 1024UL * 1024UL) return String(bytes / (1024.0f * 1024.0f), 1) + " MB";
    if (bytes >= 1024UL) return String(bytes / 1024.0f, 1) + " KB";
    return String(bytes) + " B";
}

String formatRate(uint32_t amount, bool bytes) {
    return bytes ? (formatBytes(amount) + "/s") : (String(amount) + " pkt/s");
}

uint8_t percentOf(uint32_t value, uint32_t total) {
    if (total == 0) return 0;
    const uint32_t pct = (value * 100UL + total / 2UL) / total;
    return static_cast<uint8_t>(min<uint32_t>(100, pct));
}

uint32_t deltaCounter(uint32_t current, uint32_t previous) {
    return current >= previous ? current - previous
                               : (0xFFFFFFFFUL - previous) + current + 1UL;
}

String powerText(Board::PowerState pwr) {
    if (pwr == Board::PowerState::BATTERY) return "Battery";
    if (pwr == Board::PowerState::EXTERNAL_POWER) return "External USB";
    return "Unknown";
}

String chargingText(Board::ChargingState state) {
    switch (state) {
        case Board::ChargingState::CHARGING:    return "Charging";
        case Board::ChargingState::FULL:        return "Charged (on USB)";
        case Board::ChargingState::DISCHARGING: return "On battery";
        default: break;
    }
    return "Unknown";
}

String uptimeText(uint32_t seconds) {
    const uint32_t hours = seconds / 3600UL;
    const uint32_t mins = (seconds % 3600UL) / 60UL;
    const uint32_t secs = seconds % 60UL;
    char buf[18];
    snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(mins),
             static_cast<unsigned long>(secs));
    return String(buf);
}

String syncAgeText(uint32_t msAgo) {
    return uptimeText(msAgo / 1000UL) + " ago";
}

String formatTimestamp(time_t epoch) {
    if (epoch <= 0) return "Never";
    struct tm tmv{};
    localtime_r(&epoch, &tmv);
    char buf[24] = {0};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return String(buf);
}

String wifiPhyText() {
    if (WiFi.status() != WL_CONNECTED) return "-";
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return "-";
    if (ap.phy_11n) return "802.11n";
    if (ap.phy_11g) return "802.11g";
    if (ap.phy_11b) return "802.11b";
    if (ap.phy_lr) return "LR";
    return "-";
}
}

const char* const SystemInfoGame::TAB_LABELS[TAB_COUNT] = {
    "Board", "Memory", "Network", "BLE", "App"
};

const char* SystemInfoGame::title() const {
    return "System Info";
}

void SystemInfoGame::begin(GameHost& host) {
    (void)host.requireCapability(APP_CAP_DIAGNOSTICS, "open diagnostics");
    tab_ = 0;
    lastRefreshMs_ = 0;
    lastTrafficSampleMs_ = 0;
    lastTrafficRx_ = 0;
    lastTrafficTx_ = 0;
    rxRate_ = 0;
    txRate_ = 0;
    trafficAvailable_ = false;
    trafficBytes_ = false;
    rows_.clear();
    rowsStale_ = true;
    scrolling_ = false;
    scrollAnchorY_ = 0;
    scrollStartOffset_ = 0;
    bleAdvanced_ = false;
    for (uint8_t i = 0; i < TAB_COUNT; ++i) {
        scrollOffset_[i] = 0;
    }
    refreshTelemetry(true);
    markFullDirty();
}

/* Nothing to release. rows_ is a flat, statically sized member holding char
 * buffers, so leaving this screen frees no heap because it never took any --
 * which is the point of RowList. clear() just resets the count. */
void SystemInfoGame::end(GameHost& host) {
    (void)host;
    rows_.clear();
    rowsStale_ = true;
    scrolling_ = false;
}

Rect SystemInfoGame::tabRect(uint8_t idx, int16_t screenW, int16_t stripY) const {
    const int16_t tabW = static_cast<int16_t>(screenW / TAB_COUNT);
    const int16_t x = static_cast<int16_t>(idx * tabW);
    const int16_t nextX = (idx + 1 == TAB_COUNT) ? screenW
        : static_cast<int16_t>((idx + 1) * tabW);
    return Rect{x, stripY, static_cast<int16_t>(nextX - x), TAB_STRIP_H};
}

Rect SystemInfoGame::contentRect(int16_t screenW, int16_t screenH) const {
    const int16_t top = static_cast<int16_t>(tabStripY() + TAB_STRIP_H);
    return Rect{0, top, screenW, static_cast<int16_t>(screenH - top)};
}

bool SystemInfoGame::refreshTelemetry(bool force) {
    const uint32_t now = millis();
    if (!force && lastRefreshMs_ != 0 && now - lastRefreshMs_ < REFRESH_MS) {
        return false;
    }

    const TrafficSnapshot snap = readTrafficCounters();
    if (snap.available && trafficAvailable_ && snap.bytes == trafficBytes_ &&
        lastTrafficSampleMs_ != 0 && now != lastTrafficSampleMs_) {
        const uint32_t dt = now - lastTrafficSampleMs_;
        rxRate_ = (deltaCounter(snap.rx, lastTrafficRx_) * 1000UL) / dt;
        txRate_ = (deltaCounter(snap.tx, lastTrafficTx_) * 1000UL) / dt;
    } else {
        rxRate_ = 0;
        txRate_ = 0;
    }

    trafficAvailable_ = snap.available;
    trafficBytes_ = snap.bytes;
    lastTrafficRx_ = snap.rx;
    lastTrafficTx_ = snap.tx;
    lastTrafficSampleMs_ = now;
    lastRefreshMs_ = now;
    return true;
}

SystemInfoGame::TrafficSnapshot SystemInfoGame::readTrafficCounters() const {
    TrafficSnapshot snap;
#if MIB2_STATS
    void* rawNetif = nullptr;
    if (tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_STA, &rawNetif) == ESP_OK && rawNetif != nullptr) {
        const struct netif* sta = static_cast<const struct netif*>(rawNetif);
        snap.available = true;
        snap.bytes = true;
        snap.rx = sta->mib2_counters.ifinoctets;
        snap.tx = sta->mib2_counters.ifoutoctets;
    }
#elif LWIP_STATS && LINK_STATS
    snap.available = true;
    snap.bytes = false;
    snap.rx = lwip_stats.link.recv;
    snap.tx = lwip_stats.link.xmit;
#endif
    return snap;
}

void SystemInfoGame::update(GameHost& host, const TouchPoint& touch) {
    if (refreshTelemetry(false)) {
        rowsStale_ = true;
        markDirty();
    }

    const Rect content = contentRect(host.display().width(), host.display().height());

    if (touch.justReleased) {
        scrolling_ = false;
    }

    if (touch.justPressed) {
        if (Rect{0, 0, 42, TOP_BAR_HEIGHT}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.goHome();
            return;
        }

        const int16_t W = host.display().width();
        for (uint8_t i = 0; i < TAB_COUNT; ++i) {
            if (tabRect(i, W, tabStripY()).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                if (tab_ != i) {
                    tab_ = i;
                    scrolling_ = false;
                    rowsStale_ = true;
                    markFullDirty();
                }
                return;
            }
        }

        /* The chip position comes from the previous frame's draw, which is
         * the frame the user was looking at when they touched it. */
        if (rows_.actionRect().w > 0 &&
            rows_.actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            bleAdvanced_ = !bleAdvanced_;
            rowsStale_ = true;
            host.board().beepOk();
            markDirty();
            return;
        }

        if (content.contains(touch.x, touch.y) && rows_.totalHeight() > content.h) {
            scrolling_ = true;
            scrollAnchorY_ = touch.y;
            scrollStartOffset_ = scrollOffset_[tab_];
        }
    } else if (touch.down && scrolling_) {
        scrollOffset_[tab_] = static_cast<int16_t>(scrollStartOffset_ - (touch.y - scrollAnchorY_));
        rows_.clampScroll(scrollOffset_[tab_], content.h);
        markDirty();
    }
}

void SystemInfoGame::drawTabStrip(Ui::Renderer& tft, int16_t screenW) {
    tft.fillRect(0, tabStripY(), screenW, TAB_STRIP_H, Ui::panel());
    for (uint8_t i = 0; i < TAB_COUNT; ++i) {
        Ui::drawTab(tft, tabRect(i, screenW, tabStripY()), TAB_LABELS[i], i == tab_);
    }
    Ui::drawTabBaseline(tft, static_cast<int16_t>(tabStripY() + TAB_STRIP_H),
                        0, screenW, tabRect(tab_, screenW, tabStripY()));
}

void SystemInfoGame::buildBoardRows(GameHost& host) {
    Board& board = host.board();
    const Board::BatteryTelemetry battery = board.readBatteryTelemetry();
    const int8_t pct = board.getBatteryPercent();

    rows_.clear();
    rows_.addSection("Chip");
    rows_.addRow("Board", BOARD_NAME);
    rows_.addRow("Chip", String(ESP.getChipModel()) + " r" + ESP.getChipRevision());
    rows_.addRow("CPU", String(ESP.getChipCores()) + "x " + ESP.getCpuFreqMHz() + " MHz");
    rows_.addRow("SDK", ESP.getSdkVersion());
    rows_.addRow("Firmware", BRAINO_VERSION);
    rows_.addRow("Reset", Watchdog::resetReasonText(static_cast<int>(esp_reset_reason())));
    rows_.addRow("Uptime", uptimeText(millis() / 1000UL));

    rows_.addSection("Power");
    rows_.addRow("Source", powerText(board.getPowerSource()));
    rows_.addRow("Charging", chargingText(board.getChargingState()));
    /* A negative percentage means the ADC read outside a plausible range, not
     * that the pack is missing: this board cannot tell whether one is fitted
     * (see BoardPower.cpp), so it must not claim to. */
    rows_.addRow("Battery", String(battery.batteryVoltage, 2) + " V" +
           (pct >= 0 ? " (" + String(pct) + "%)" : " (sensor fault)"));
    rows_.addRow("Charge level", pct < 0 ? String("Sensor out of range")
                                : board.isBatteryCritical() ? String("Critical - charge now")
                                : board.isBatteryLow() ? String("Low - charge soon")
                                : String("OK"));
    rows_.addRow("BAT ADC", String(battery.rawAdc) + " raw");
    rows_.addRow("ADC pin", String(battery.adcVoltage, 2) + " V");

    rows_.addSection("Flash");
    rows_.addRow("Chip size", formatBytes(ESP.getFlashChipSize()));
    rows_.addRow("Clock", String(ESP.getFlashChipSpeed() / 1000000UL) + " MHz");
    rows_.addRow("Sketch", formatBytes(ESP.getSketchSize()));
    rows_.addRow("Free app", formatBytes(ESP.getFreeSketchSpace()));
}

void SystemInfoGame::buildMemoryRows(GameHost& host) {
    Board& board = host.board();
    const Watchdog::Stats stats = Watchdog::stats();
    const Board::StorageTelemetry storage = board.storageTelemetry();
    const uint32_t heapTotal = ESP.getHeapSize();
    const uint32_t heapFree = ESP.getFreeHeap();
    const uint32_t heapUsed = heapTotal > heapFree ? heapTotal - heapFree : 0;
    const uint8_t heapPct = percentOf(heapUsed, heapTotal);
    const uint32_t psramTotal = ESP.getPsramSize();
    const uint32_t psramFree = ESP.getFreePsram();
    const uint32_t frameMs = max<uint32_t>(1, stats.lastFrameMs);
    const uint8_t loopPct = percentOf(stats.lastWorkMs, frameMs);

    rows_.clear();
    rows_.addSection("Heap");
    rows_.addRow("Used", formatBytes(heapUsed) + " / " + formatBytes(heapTotal));
    rows_.addMeter(heapPct, heapPct > 80 ? Ui::warning() : Ui::success());
    rows_.addRow("Free", formatBytes(heapFree));
    rows_.addRow("Min free", formatBytes(stats.minFreeHeap));
    rows_.addRow("Largest", formatBytes(stats.largestBlock));
    /* The number free heap alone will not tell you: plenty available, no
     * single piece big enough for the next allocation. */
    const uint8_t frag = Watchdog::heapFragmentation();
    rows_.addRow("Fragmented", String(frag) + "%",
                 frag >= 60 ? Ui::error() : (frag >= 35 ? Ui::warning() : Ui::success()));
    rows_.addMeter(frag, frag >= 60 ? Ui::error() : (frag >= 35 ? Ui::warning() : Ui::success()));
    rows_.addRow("PSRAM", psramTotal > 0 ? formatBytes(psramFree) + " free" : "Not present");

    rows_.addSection("NVS");
    if (storage.available) {
        const uint8_t nvsPct = percentOf(storage.usedEntries, storage.totalEntries);
        const uint16_t nvsColor = nvsPct >= Board::STORAGE_CRITICAL_PERCENT
            ? Ui::error()
            : (nvsPct >= Board::STORAGE_WARN_PERCENT ? Ui::warning() : Ui::success());
        char value[40];
        snprintf(value, sizeof(value), "%lu / %lu entries",
                 static_cast<unsigned long>(storage.usedEntries),
                 static_cast<unsigned long>(storage.totalEntries));
        rows_.addRow("Used", value, nvsColor);
        rows_.addMeter(nvsPct, nvsColor);
        snprintf(value, sizeof(value), "%lu entries",
                 static_cast<unsigned long>(storage.freeEntries));
        rows_.addRow("Free", value);
        snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(storage.namespaceCount));
        rows_.addRow("Namespaces", value);
        snprintf(value, sizeof(value), "%u%% warn, %u%% critical",
                 static_cast<unsigned>(Board::STORAGE_WARN_PERCENT),
                 static_cast<unsigned>(Board::STORAGE_CRITICAL_PERCENT));
        rows_.addRow("Quota", value);
    } else {
        rows_.addRow("Partition", "Stats unavailable", Ui::warning());
    }
    char namespaceValue[32];
    if (storage.appNamespaceAvailable) {
        snprintf(namespaceValue, sizeof(namespaceValue), "%lu data entries",
                 static_cast<unsigned long>(storage.appEntries));
    } else {
        snprintf(namespaceValue, sizeof(namespaceValue), "No namespace");
    }
    rows_.addRow("cydkids", namespaceValue);
    if (storage.watchdogNamespaceAvailable) {
        snprintf(namespaceValue, sizeof(namespaceValue), "%lu data entries",
                 static_cast<unsigned long>(storage.watchdogEntries));
    } else {
        snprintf(namespaceValue, sizeof(namespaceValue), "No namespace");
    }
    rows_.addRow("cydwdt", namespaceValue);

    rows_.addSection("CPU");
    rows_.addRow("Loop load", String(loopPct) + "% (" + stats.lastWorkMs + "/" + stats.lastFrameMs + " ms)");
    rows_.addMeter(loopPct, loopPct > 70 ? Ui::warning() : Ui::success());
    rows_.addRow("Worst work", String(stats.maxWorkMs) + " ms");
    rows_.addRow("Worst frame", String(stats.maxFrameMs) + " ms");
    rows_.addRow("Loops", String(stats.loops));
    rows_.addRow("Boot count", String(stats.bootCount));
}

void SystemInfoGame::buildNetworkRows(GameHost& host) {
    Board& board = host.board();
    const bool up = WiFi.status() == WL_CONNECTED;
    const time_t lastSyncEpoch = board.lastTimeSyncEpoch();
    const uint32_t lastSyncMs = board.lastTimeSyncMs();

    rows_.clear();
    rows_.addSection("Status");
    rows_.addRow("Wi-Fi", up ? "Connected" : "Disconnected",
           up ? Ui::success() : Ui::warning());
    rows_.addRow("Purpose", "NTP + TZ lookup if auto");
    rows_.addRow("NTP server", board.ntpServer());
    rows_.addRow("Fallbacks", "time.google.com, time.cloudflare.com");
    rows_.addRow("TZ lookup", board.tzZoneChosen() ? "Disabled (manual zone)" : "ip-api.com");
    rows_.addRow("Time sync", board.timeSynced() ? "Synced" : "Free-running",
           board.timeSynced() ? Ui::success() : Ui::warning());
    rows_.addRow("Last sync", formatTimestamp(lastSyncEpoch));
    rows_.addRow("Since sync", lastSyncMs != 0 ? syncAgeText(millis() - lastSyncMs) : "Never");

    rows_.addSection("Link");
    rows_.addRow("Hostname", String(WiFi.getHostname() ? WiFi.getHostname() : "-"));
    rows_.addRow("MAC", WiFi.macAddress());
    rows_.addRow("Saved SSID", board.wifiSsid().length() ? board.wifiSsid() : "None");
    if (up) {
        wifi_ap_record_t ap{};
        const bool haveAp = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
        rows_.addRow("SSID", WiFi.SSID());
        rows_.addRow("RSSI", String(WiFi.RSSI()) + " dBm");
        rows_.addRow("Channel", String(WiFi.channel()));
        rows_.addRow("PHY", wifiPhyText());
        rows_.addRow("BSSID", haveAp ? WiFi.BSSIDstr() : "-");
        rows_.addRow("IP", WiFi.localIP().toString());
        rows_.addRow("Gateway", WiFi.gatewayIP().toString());
        rows_.addRow("Subnet", WiFi.subnetMask().toString());
        rows_.addRow("DNS", WiFi.dnsIP().toString());
    }

    rows_.addSection("Traffic");
    rows_.addRow("RX", trafficAvailable_ ? formatRate(rxRate_, trafficBytes_) : "Counters off");
    rows_.addRow("TX", trafficAvailable_ ? formatRate(txRate_, trafficBytes_) : "Counters off");
    rows_.addRow("Counters", trafficAvailable_ ? (trafficBytes_ ? "lwIP bytes" : "lwIP packets")
                                         : "lwIP stats disabled");

    rows_.addSection("Recent calls");
    if (board.networkActivityCount() == 0) {
        rows_.addRow("Log", "No calls recorded yet");
    } else {
        for (uint8_t i = 0; i < board.networkActivityCount() && i < 8; ++i) {
            const Board::NetworkActivity item = board.networkActivity(i);
            const uint32_t ageMs = item.atMs > 0 ? millis() - item.atMs : 0;
            rows_.addRow(String("T-") + uptimeText(ageMs / 1000UL), item.detail);
        }
    }
}

/* What the device is putting on the air, read back from the one structure the
 * radio was configured from (hal/BleBeacon.h). Nothing here is a hand-written
 * description of the payload -- every value, including the raw hex, comes from
 * BleBeacon::configured()/broadcasting(), so the screen cannot drift away from
 * what the controller is actually transmitting.
 *
 * The distinction the screen has to keep honest: `live` is non-null only while
 * the controller is advertising. When it is null nothing is on air, and the
 * configured identity is labelled as configuration, never as broadcast. */
void SystemInfoGame::buildBleRows(GameHost& host) {
    Board& board = host.board();
    const BleBeacon::Advertisement& cfg = BleBeacon::configured();
    const BleBeacon::Advertisement* live = BleBeacon::broadcasting();
    const bool onAir = (live != nullptr);
    const BleBeacon::Advertisement& src = onAir ? *live : cfg;

    rows_.clear();

    rows_.addSection("Beacon");
    if (onAir) {
        rows_.addRow("Status", "Advertising", Ui::success());
        rows_.addRow("Mode", BleBeacon::modeText());
    } else {
        rows_.addRow("BLE Beacon", board.bleBeaconEnabled() ? "On (radio down)" : "Off",
               board.bleBeaconEnabled() ? Ui::warning() : Ui::muted());
        rows_.addRow("Broadcasting", "Nothing", Ui::success());
        rows_.addRow("Turn on in", "Settings > Beacon", Ui::muted());
    }

    /* Section title doubles as the configured/broadcast label, so no row below
     * it can be misread as being on air when it is not. */
    rows_.addSection(onAir ? "On Air" : "Config");
    rows_.addRow("Name", src.deviceName);
    rows_.addRow("Family ID", src.familyId);
    rows_.addRow("Device ID", src.deviceId);

    rows_.addSection(onAir ? "Mfr Data" : "Mfr (cfg)");
    rows_.addRow("Company", "0x" + String(src.companyId, HEX) + " unassigned");
    rows_.addRow("Family", String(src.familyId) + " (" + BleBeacon::FAMILY_TAG + ")");
    rows_.addRow("Version", String(BleBeacon::PAYLOAD_VERSION));
    rows_.addRow("Device ID", src.deviceId);
    /* Nearby play adds two fields to the manufacturer block. They are listed
     * from the same struct as everything else, and they are absent from the
     * list when they are absent from the air -- which is the only way the row
     * and the radio cannot disagree. */
    rows_.addRow("Nearby play", src.sharesActivity ? "Sharing" : "Not sharing",
                 src.sharesActivity ? Ui::warning() : Ui::success());
    if (src.sharesActivity) {
        const char* openTitle = src.gameIndex < playableAppCount()
            ? playableAppAt(src.gameIndex).title() : nullptr;
        rows_.addRow("Game index", openTitle != nullptr
                         ? (String(src.gameIndex) + " " + openTitle)
                         : String(src.gameIndex));
        rows_.addRow("Best score", String(src.bestScore));
    }
    rows_.addRow("Raw", BleBeacon::toHex(src.manufacturerData, src.manufacturerLen));

    if (src.serviceUuid16 != 0) {
        rows_.addRow("Service UUID", "0x" + String(src.serviceUuid16, HEX));
        if (src.serviceDataLen > 0) {
            rows_.addRow("Service Data", BleBeacon::toHex(src.serviceData, src.serviceDataLen));
        }
    }

    /* The privacy list is not a promise typed into the UI -- it is read off
     * the same struct the controller was handed. buildPayload() emits a name
     * AD and a manufacturer AD and nothing else, so everything below is
     * structurally unreachable from the radio path; the two rows that Nearby
     * play does put on air read from src.sharesActivity rather than from a
     * claim, because a privacy row that has drifted is worse than none. */
    rows_.addSection("Privacy");
    const uint16_t safe = Ui::success();
    rows_.addRow("Child info", "Not Broadcast", safe);
    rows_.addRow("Child name", "Not Broadcast", safe);
    rows_.addRow("Profile name", "Not Broadcast", safe);
    rows_.addRow("Location", "Not Broadcast", safe);
    rows_.addRow("Wi-Fi password", "Not Broadcast", safe);
    rows_.addRow("Wi-Fi SSID", "Not Broadcast", safe);
    rows_.addRow("IP address", "Not Broadcast", safe);
    rows_.addRow("Game progress", "Not Broadcast", safe);
    rows_.addRow("Open game", src.sharesActivity ? "Broadcast" : "Not Broadcast",
                 src.sharesActivity ? Ui::warning() : safe);
    rows_.addRow("Best score", src.sharesActivity ? "Broadcast" : "Not Broadcast",
                 src.sharesActivity ? Ui::warning() : safe);
    rows_.addRow("Usage history", "Not Broadcast", safe);

    rows_.addAction(bleAdvanced_ ? "Hide advanced" : "Show advanced");

    if (!bleAdvanced_) {
        return;
    }

    rows_.addSection("Advanced");
    rows_.addRow("Interval", String(src.advIntervalMs) + " ms");
    rows_.addRow("TX power", src.txPowerConfigured ? (String(src.txPowerDbm) + " dBm")
                                             : String("Controller default"));
    rows_.addRow("Adv type", src.connectable ? "Connectable undirected"
                                       : "Non-connectable");
    const String addr = BleBeacon::address();
    rows_.addRow("BLE address", addr.length() ? addr : "Stack down");
    rows_.addRow("Payload", String(src.payloadLen) + " of " +
                      String(BleBeacon::PAYLOAD_MAX) + " bytes");

    /* The full AD-structure dump, chunked so each line fits the value column
     * rather than being truncated to an ellipsis. Byte offsets are labelled so
     * a reader can line them up against a scanner capture. */
    constexpr uint8_t PER_ROW = 7;
    for (uint8_t off = 0; off < src.payloadLen; off = static_cast<uint8_t>(off + PER_ROW)) {
        const uint8_t n = static_cast<uint8_t>(min<int>(PER_ROW, src.payloadLen - off));
        rows_.addRow(String(off) + "-" + String(off + n - 1),
               BleBeacon::toHex(src.payload + off, n));
    }
}

void SystemInfoGame::buildAppStateRows(GameHost& host) {
    Board& board = host.board();
    const Watchdog::Stats stats = Watchdog::stats();

    rows_.clear();
    rows_.addSection("Device");
    rows_.addRow("Theme", board.themeMode() == Board::ThemeMode::Light ? "Light" : "Dark");
    rows_.addRow("Layout", board.layoutMode() == Board::LayoutMode::Vertical ? "Portrait" : "Landscape");
    rows_.addRow("Brightness", String(board.brightness()) + "%");
    rows_.addRow("Saver", String(board.screenSaverSeconds()) + " s");
    rows_.addRow("Profile", board.isGuest() ? "Guest" : board.profileName(board.activeProfile()));
    rows_.addRow("Touch cal", board.hasTouchCalibration() ? "Calibrated" : "Uncalibrated");
    rows_.addRow("SD card", board.sdReady() ? "Ready" : "Not found");
    rows_.addRow("Wi-Fi creds", board.hasWifiCredentials() ? "Stored" : "Not set");
    rows_.addRow("NTP enabled", board.ntpEnabled() ? "Yes" : "No");

    const Board::DisplaySleepTelemetry sleep = board.displaySleepTelemetry();
    rows_.addSection("Display sleep");
    rows_.addRow("Panel", board.displayAsleep() ? "Asleep" : "Awake",
                 board.displayAsleep() ? Ui::warning() : Ui::success());
    char value[24];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(sleep.sleepCount));
    rows_.addRow("Sleeps", value);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(sleep.wakeCount));
    rows_.addRow("Wakes", value);
    rows_.addRow("Last sleep", sleep.lastSleepMs ? uptimeText(sleep.lastSleepMs / 1000UL) : "Never");
    rows_.addRow("Last wake", sleep.lastWakeMs ? uptimeText(sleep.lastWakeMs / 1000UL) : "Never");
    rows_.addRow("Last slept", sleep.lastSleepDurationMs
                 ? uptimeText(sleep.lastSleepDurationMs / 1000UL) : "-");
    if (sleep.lastWakeDelayMs) {
        snprintf(value, sizeof(value), "%lu ms", static_cast<unsigned long>(sleep.lastWakeDelayMs));
        rows_.addRow("Wake delay", value);
    } else {
        rows_.addRow("Wake delay", "-");
    }

    rows_.addSection("Watchdog");
    rows_.addRow("State", stats.armed ? "Armed" : "Not armed");
    rows_.addRow("Stalls", String(stats.stalls));
    rows_.addRow("Uptime", uptimeText(stats.uptimeSeconds));

    rows_.addSection("Claims");
    rows_.addRow("Internet use", board.tzZoneChosen() ? "NTP only" : "NTP + ip-api.com");
    rows_.addRow("BLE beacon", BleBeacon::active() ? "Advertising -- see BLE tab"
                                             : "Off; nothing broadcast");
    rows_.addRow("Nearby play", NearbyPlay::active()
                     ? (String(NearbyPlay::peerCount()) + " nearby")
                     : String(NearbyPlay::enabled() ? "On, radio starting" : "Off"));
    rows_.addRow("Temp", "Not shown; ESP32 reading not trusted");
}

void SystemInfoGame::rebuildRows(GameHost& host) {
    switch (tab_) {
        case 0: buildBoardRows(host); break;
        case 1: buildMemoryRows(host); break;
        case 2: buildNetworkRows(host); break;
        case 3: buildBleRows(host); break;
        case 4: buildAppStateRows(host); break;
    }
    rowsStale_ = false;
}

void SystemInfoGame::drawContent(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const Rect cr = contentRect(static_cast<int16_t>(tft.width()),
                                static_cast<int16_t>(tft.height()));

    /* Rebuilt on a telemetry tick, a tab change or a toggle -- not per frame.
     * The row builders assemble their values with Arduino String, so doing
     * this on every frame of a scroll drag meant a few hundred transient
     * allocations a second churning through a heap that cannot be compacted.
     * Scrolling changes only the offset, and the offset is a draw argument. */
    if (rowsStale_) {
        rebuildRows(host);
    }
    rows_.clampScroll(scrollOffset_[tab_], cr.h);
    rows_.draw(tft, cr, scrollOffset_[tab_]);
}

void SystemInfoGame::render(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());

    if (needsFullRender()) {
        Ui::clear(tft);
        Ui::drawTopBar(board, title());
        drawTabStrip(tft, W);
    }
    drawContent(host);
}
