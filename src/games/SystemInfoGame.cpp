#include "SystemInfoGame.h"
#include "AppVersion.h"
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
constexpr int16_t CONTENT_PAD_X = 6;
constexpr int16_t CONTENT_PAD_Y = 6;
constexpr int16_t SCROLLBAR_W = 6;

int16_t tabStripY() {
    return static_cast<int16_t>(TOP_BAR_HEIGHT + 2);
}

String fittedText(TFT_eSPI& tft, const String& text, int16_t maxW, uint8_t font) {
    String fitted = text;
    while (fitted.length() > 2 && tft.textWidth(fitted, font) > maxW) {
        fitted.remove(fitted.length() - 1);
    }
    if (fitted.length() < text.length() && fitted.length() > 1) {
        fitted.setCharAt(fitted.length() - 1, '.');
    }
    return fitted;
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

void drawMeter(TFT_eSPI& tft, const Rect& r, uint8_t pct, uint16_t fill) {
    if (pct > 100) pct = 100;
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 3, Ui::panel());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 3, Ui::outline());
    const int16_t innerW = static_cast<int16_t>(max<int16_t>(0, r.w - 2));
    const int16_t fillW = static_cast<int16_t>((innerW * pct) / 100);
    if (fillW > 0) {
        tft.fillRoundRect(static_cast<int16_t>(r.x + 1), static_cast<int16_t>(r.y + 1),
                          fillW, static_cast<int16_t>(r.h - 2), 2, fill);
    }
}

String powerText(Board::PowerState pwr) {
    if (pwr == Board::PowerState::BATTERY) return "Battery";
    if (pwr == Board::PowerState::EXTERNAL_POWER) return "External USB";
    return "Unknown";
}

String chargingText(Board::ChargingState state) {
    (void)state;
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
    (void)host;
    tab_ = 0;
    lastRefreshMs_ = 0;
    lastTrafficSampleMs_ = 0;
    lastTrafficRx_ = 0;
    lastTrafficTx_ = 0;
    rxRate_ = 0;
    txRate_ = 0;
    trafficAvailable_ = false;
    trafficBytes_ = false;
    rowCount_ = 0;
    scrolling_ = false;
    scrollAnchorY_ = 0;
    scrollStartOffset_ = 0;
    bleAdvanced_ = false;
    actionRect_ = Rect{};
    for (uint8_t i = 0; i < TAB_COUNT; ++i) {
        scrollOffset_[i] = 0;
    }
    refreshTelemetry(true);
    markFullDirty();
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

void SystemInfoGame::beginRows() {
    rowCount_ = 0;
}

void SystemInfoGame::addSection(const String& title) {
    if (rowCount_ >= MAX_ROWS) return;
    rows_[rowCount_].kind = RowKind::Section;
    rows_[rowCount_].label = title;
    rows_[rowCount_].value = "";
    rows_[rowCount_].valueColor = Ui::muted();
    rows_[rowCount_].meterPct = 0;
    rows_[rowCount_].meterColor = 0;
    rows_[rowCount_].height = 18;
    rowCount_++;
}

void SystemInfoGame::addRow(const String& label, const String& value, uint16_t valueColor, int16_t height) {
    if (rowCount_ >= MAX_ROWS) return;
    rows_[rowCount_].kind = RowKind::Text;
    rows_[rowCount_].label = label;
    rows_[rowCount_].value = value;
    rows_[rowCount_].valueColor = valueColor == 0 ? Ui::text() : valueColor;
    rows_[rowCount_].meterPct = 0;
    rows_[rowCount_].meterColor = 0;
    rows_[rowCount_].height = height;
    rowCount_++;
}

void SystemInfoGame::addAction(const String& label) {
    if (rowCount_ >= MAX_ROWS) return;
    rows_[rowCount_].kind = RowKind::Action;
    rows_[rowCount_].label = label;
    rows_[rowCount_].value = "";
    rows_[rowCount_].valueColor = Ui::text();
    rows_[rowCount_].meterPct = 0;
    rows_[rowCount_].meterColor = 0;
    rows_[rowCount_].height = 22;
    rowCount_++;
}

void SystemInfoGame::addMeter(uint8_t pct, uint16_t color) {
    if (rowCount_ >= MAX_ROWS) return;
    rows_[rowCount_].kind = RowKind::Meter;
    rows_[rowCount_].label = "";
    rows_[rowCount_].value = "";
    rows_[rowCount_].valueColor = Ui::text();
    rows_[rowCount_].meterPct = pct;
    rows_[rowCount_].meterColor = color;
    rows_[rowCount_].height = 12;
    rowCount_++;
}

int16_t SystemInfoGame::rowsHeight() const {
    int16_t total = static_cast<int16_t>(CONTENT_PAD_Y * 2);
    for (uint8_t i = 0; i < rowCount_; ++i) {
        total = static_cast<int16_t>(total + rows_[i].height);
    }
    return total;
}

void SystemInfoGame::drawScrollBar(TFT_eSPI& tft, const Rect& r, int16_t totalHeight) const {
    if (totalHeight <= r.h) return;
    const int16_t trackX = static_cast<int16_t>(r.x + r.w - SCROLLBAR_W - 2);
    const int16_t trackY = static_cast<int16_t>(r.y + 3);
    const int16_t trackH = static_cast<int16_t>(r.h - 6);
    tft.fillRoundRect(trackX, trackY, SCROLLBAR_W, trackH, 3, Ui::panel());
    tft.drawRoundRect(trackX, trackY, SCROLLBAR_W, trackH, 3, Ui::outline());

    const int16_t thumbH = static_cast<int16_t>(max<int16_t>(18, (trackH * r.h) / totalHeight));
    const int16_t maxScroll = static_cast<int16_t>(totalHeight - r.h);
    const int16_t travel = static_cast<int16_t>(max<int16_t>(1, trackH - thumbH));
    const int16_t thumbY = static_cast<int16_t>(trackY +
        (static_cast<int32_t>(scrollOffset_[tab_]) * travel) / max<int16_t>(1, maxScroll));
    tft.fillRoundRect(static_cast<int16_t>(trackX + 1), static_cast<int16_t>(thumbY + 1),
                      static_cast<int16_t>(SCROLLBAR_W - 2), static_cast<int16_t>(thumbH - 2),
                      2, Ui::rgb(88, 164, 224));
}

void SystemInfoGame::clampScroll(uint8_t tabIndex, int16_t viewportH) {
    const int16_t totalHeight = rowsHeight();
    const int16_t maxScroll = static_cast<int16_t>(max<int16_t>(0, totalHeight - viewportH));
    if (scrollOffset_[tabIndex] < 0) scrollOffset_[tabIndex] = 0;
    if (scrollOffset_[tabIndex] > maxScroll) scrollOffset_[tabIndex] = maxScroll;
}

void SystemInfoGame::drawRows(TFT_eSPI& tft, const Rect& r) {
    const int16_t totalHeight = rowsHeight();
    clampScroll(tab_, r.h);
    const bool needsScrollBar = totalHeight > r.h;
    const int16_t rightPad = needsScrollBar ? static_cast<int16_t>(CONTENT_PAD_X + SCROLLBAR_W + 6) : CONTENT_PAD_X;
    const int16_t labelX = static_cast<int16_t>(r.x + CONTENT_PAD_X);
    const int16_t valueX = static_cast<int16_t>(r.x + max<int16_t>(92, r.w / 2));
    const int16_t right = static_cast<int16_t>(r.x + r.w - rightPad);
    int16_t y = static_cast<int16_t>(r.y + CONTENT_PAD_Y - scrollOffset_[tab_]);

    tft.fillRect(r.x, r.y, r.w, r.h, Ui::surface());
    actionRect_ = Rect{};   // repopulated below if an Action row is on screen

    for (uint8_t i = 0; i < rowCount_; ++i) {
        const Row& row = rows_[i];
        if (y + row.height < r.y) {
            y = static_cast<int16_t>(y + row.height);
            continue;
        }
        if (y > r.y + r.h) break;

        if (row.kind == RowKind::Section) {
            tft.setTextDatum(TL_DATUM);
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.drawString(row.label, labelX, y, 2);
            tft.drawFastHLine(static_cast<int16_t>(labelX + 54),
                              static_cast<int16_t>(y + 7),
                              static_cast<int16_t>(max<int16_t>(10, right - labelX - 56)),
                              Ui::outline());
        } else if (row.kind == RowKind::Text) {
            tft.setTextDatum(TL_DATUM);
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.drawString(row.label, labelX, y, 1);
            tft.setTextColor(row.valueColor, Ui::surface());
            tft.drawString(fittedText(tft, row.value, static_cast<int16_t>(right - valueX), 1),
                           valueX, y, 1);
        } else if (row.kind == RowKind::Action) {
            const Rect chip{labelX, y, static_cast<int16_t>(min<int16_t>(150, right - labelX)), 18};
            Ui::drawButton(tft, chip, row.label, Ui::panel(), Ui::outline(), Ui::text(), false, 1);
            actionRect_ = chip;
        } else {
            drawMeter(tft, Rect{labelX, y, static_cast<int16_t>(right - labelX), 8},
                      row.meterPct, row.meterColor);
        }
        y = static_cast<int16_t>(y + row.height);
    }

    drawScrollBar(tft, r, totalHeight);
}

void SystemInfoGame::update(GameHost& host, const TouchPoint& touch) {
    if (refreshTelemetry(false)) {
        markDirty();
    }

    const Rect content = contentRect(static_cast<int16_t>(host.board().display().width()),
                                     static_cast<int16_t>(host.board().display().height()));

    if (touch.justReleased) {
        scrolling_ = false;
    }

    if (touch.justPressed) {
        if (Rect{0, 0, 42, TOP_BAR_HEIGHT}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.goHome();
            return;
        }

        const int16_t W = static_cast<int16_t>(host.board().display().width());
        for (uint8_t i = 0; i < TAB_COUNT; ++i) {
            if (tabRect(i, W, tabStripY()).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                if (tab_ != i) {
                    tab_ = i;
                    scrolling_ = false;
                    markFullDirty();
                }
                return;
            }
        }

        /* The chip position comes from the previous frame's draw, which is
         * the frame the user was looking at when they touched it. */
        if (actionRect_.w > 0 && actionRect_.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            bleAdvanced_ = !bleAdvanced_;
            host.board().beepOk();
            markDirty();
            return;
        }

        if (content.contains(touch.x, touch.y) && rowsHeight() > content.h) {
            scrolling_ = true;
            scrollAnchorY_ = touch.y;
            scrollStartOffset_ = scrollOffset_[tab_];
        }
    } else if (touch.down && scrolling_) {
        scrollOffset_[tab_] = static_cast<int16_t>(scrollStartOffset_ - (touch.y - scrollAnchorY_));
        clampScroll(tab_, content.h);
        markDirty();
    }
}

void SystemInfoGame::drawTabStrip(TFT_eSPI& tft, int16_t screenW) {
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

    beginRows();
    addSection("Chip");
    addRow("Board", BOARD_NAME);
    addRow("Chip", String(ESP.getChipModel()) + " r" + ESP.getChipRevision());
    addRow("CPU", String(ESP.getChipCores()) + "x " + ESP.getCpuFreqMHz() + " MHz");
    addRow("SDK", ESP.getSdkVersion());
    addRow("Firmware", GOODTIME_KIDS_VERSION);
    addRow("Reset", Watchdog::resetReasonText(static_cast<int>(esp_reset_reason())));
    addRow("Uptime", uptimeText(millis() / 1000UL));

    addSection("Power");
    addRow("Source", powerText(board.getPowerSource()));
    addRow("Charging", chargingText(board.getChargingState()));
    addRow("Battery", String(battery.batteryVoltage, 2) + " V" +
           (pct >= 0 ? " (" + String(pct) + "%)" : " (no batt)"));
    addRow("BAT ADC", String(battery.rawAdc) + " raw");
    addRow("ADC pin", String(battery.adcVoltage, 2) + " V");

    addSection("Flash");
    addRow("Chip size", formatBytes(ESP.getFlashChipSize()));
    addRow("Clock", String(ESP.getFlashChipSpeed() / 1000000UL) + " MHz");
    addRow("Sketch", formatBytes(ESP.getSketchSize()));
    addRow("Free app", formatBytes(ESP.getFreeSketchSpace()));
}

void SystemInfoGame::buildMemoryRows() {
    const Watchdog::Stats stats = Watchdog::stats();
    const uint32_t heapTotal = ESP.getHeapSize();
    const uint32_t heapFree = ESP.getFreeHeap();
    const uint32_t heapUsed = heapTotal > heapFree ? heapTotal - heapFree : 0;
    const uint8_t heapPct = percentOf(heapUsed, heapTotal);
    const uint32_t psramTotal = ESP.getPsramSize();
    const uint32_t psramFree = ESP.getFreePsram();
    const uint32_t frameMs = max<uint32_t>(1, stats.lastFrameMs);
    const uint8_t loopPct = percentOf(stats.lastWorkMs, frameMs);

    beginRows();
    addSection("Heap");
    addRow("Used", formatBytes(heapUsed) + " / " + formatBytes(heapTotal));
    addMeter(heapPct, heapPct > 80 ? Ui::warning() : Ui::success());
    addRow("Free", formatBytes(heapFree));
    addRow("Min free", formatBytes(stats.minFreeHeap));
    addRow("Largest", formatBytes(stats.largestBlock));
    addRow("PSRAM", psramTotal > 0 ? formatBytes(psramFree) + " free" : "Not present");

    addSection("CPU");
    addRow("Loop load", String(loopPct) + "% (" + stats.lastWorkMs + "/" + stats.lastFrameMs + " ms)");
    addMeter(loopPct, loopPct > 70 ? Ui::warning() : Ui::success());
    addRow("Worst work", String(stats.maxWorkMs) + " ms");
    addRow("Worst frame", String(stats.maxFrameMs) + " ms");
    addRow("Loops", String(stats.loops));
    addRow("Boot count", String(stats.bootCount));
}

void SystemInfoGame::buildNetworkRows(GameHost& host) {
    Board& board = host.board();
    const bool up = WiFi.status() == WL_CONNECTED;
    const time_t lastSyncEpoch = board.lastTimeSyncEpoch();
    const uint32_t lastSyncMs = board.lastTimeSyncMs();

    beginRows();
    addSection("Status");
    addRow("Wi-Fi", up ? "Connected" : "Disconnected",
           up ? Ui::success() : Ui::warning());
    addRow("Purpose", "NTP + TZ lookup if auto");
    addRow("NTP server", board.ntpServer());
    addRow("Fallbacks", "time.google.com, time.cloudflare.com");
    addRow("TZ lookup", board.tzZoneChosen() ? "Disabled (manual zone)" : "ip-api.com");
    addRow("Time sync", board.timeSynced() ? "Synced" : "Free-running",
           board.timeSynced() ? Ui::success() : Ui::warning());
    addRow("Last sync", formatTimestamp(lastSyncEpoch));
    addRow("Since sync", lastSyncMs != 0 ? syncAgeText(millis() - lastSyncMs) : "Never");

    addSection("Link");
    addRow("Hostname", String(WiFi.getHostname() ? WiFi.getHostname() : "-"));
    addRow("MAC", WiFi.macAddress());
    addRow("Saved SSID", board.wifiSsid().length() ? board.wifiSsid() : "None");
    if (up) {
        wifi_ap_record_t ap{};
        const bool haveAp = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
        addRow("SSID", WiFi.SSID());
        addRow("RSSI", String(WiFi.RSSI()) + " dBm");
        addRow("Channel", String(WiFi.channel()));
        addRow("PHY", wifiPhyText());
        addRow("BSSID", haveAp ? WiFi.BSSIDstr() : "-");
        addRow("IP", WiFi.localIP().toString());
        addRow("Gateway", WiFi.gatewayIP().toString());
        addRow("Subnet", WiFi.subnetMask().toString());
        addRow("DNS", WiFi.dnsIP().toString());
    }

    addSection("Traffic");
    addRow("RX", trafficAvailable_ ? formatRate(rxRate_, trafficBytes_) : "Counters off");
    addRow("TX", trafficAvailable_ ? formatRate(txRate_, trafficBytes_) : "Counters off");
    addRow("Counters", trafficAvailable_ ? (trafficBytes_ ? "lwIP bytes" : "lwIP packets")
                                         : "lwIP stats disabled");

    addSection("Recent calls");
    if (board.networkActivityCount() == 0) {
        addRow("Log", "No calls recorded yet");
    } else {
        for (uint8_t i = 0; i < board.networkActivityCount() && i < 8; ++i) {
            const Board::NetworkActivity item = board.networkActivity(i);
            const uint32_t ageMs = item.atMs > 0 ? millis() - item.atMs : 0;
            addRow(String("T-") + uptimeText(ageMs / 1000UL), item.detail);
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

    beginRows();

    addSection("Beacon");
    if (onAir) {
        addRow("Status", "Advertising", Ui::success());
        addRow("Mode", BleBeacon::modeText());
    } else {
        addRow("BLE Beacon", board.bleBeaconEnabled() ? "On (radio down)" : "Off",
               board.bleBeaconEnabled() ? Ui::warning() : Ui::muted());
        addRow("Broadcasting", "Nothing", Ui::success());
        addRow("Turn on in", "Settings > Beacon", Ui::muted());
    }

    /* Section title doubles as the configured/broadcast label, so no row below
     * it can be misread as being on air when it is not. */
    addSection(onAir ? "On Air" : "Config");
    addRow("Name", src.deviceName);
    addRow("Family ID", src.familyId);
    addRow("Device ID", src.deviceId);

    addSection(onAir ? "Mfr Data" : "Mfr (cfg)");
    addRow("Company", "0x" + String(src.companyId, HEX) + " unassigned");
    addRow("Family", String(src.familyId) + " (" + BleBeacon::FAMILY_TAG + ")");
    addRow("Version", String(BleBeacon::PAYLOAD_VERSION));
    addRow("Device ID", src.deviceId);
    addRow("Raw", BleBeacon::toHex(src.manufacturerData, src.manufacturerLen));

    if (src.serviceUuid16 != 0) {
        addRow("Service UUID", "0x" + String(src.serviceUuid16, HEX));
        if (src.serviceDataLen > 0) {
            addRow("Service Data", BleBeacon::toHex(src.serviceData, src.serviceDataLen));
        }
    }

    /* The privacy list is not a promise typed into the UI -- it is the direct
     * consequence of BleBeacon::buildPayload(), which emits a name AD and a
     * manufacturer AD and nothing else. Anything a game or profile stores is
     * structurally unreachable from there. */
    addSection("Privacy");
    const uint16_t safe = Ui::success();
    addRow("Child info", "Not Broadcast", safe);
    addRow("Child name", "Not Broadcast", safe);
    addRow("Location", "Not Broadcast", safe);
    addRow("Wi-Fi password", "Not Broadcast", safe);
    addRow("Wi-Fi SSID", "Not Broadcast", safe);
    addRow("IP address", "Not Broadcast", safe);
    addRow("Game progress", "Not Broadcast", safe);
    addRow("Scores", "Not Broadcast", safe);
    addRow("Usage history", "Not Broadcast", safe);

    addAction(bleAdvanced_ ? "Hide advanced" : "Show advanced");

    if (!bleAdvanced_) {
        return;
    }

    addSection("Advanced");
    addRow("Interval", String(src.advIntervalMs) + " ms");
    addRow("TX power", src.txPowerConfigured ? (String(src.txPowerDbm) + " dBm")
                                             : String("Controller default"));
    addRow("Adv type", src.connectable ? "Connectable undirected"
                                       : "Non-connectable");
    const String addr = BleBeacon::address();
    addRow("BLE address", addr.length() ? addr : "Stack down");
    addRow("Payload", String(src.payloadLen) + " of " +
                      String(BleBeacon::PAYLOAD_MAX) + " bytes");

    /* The full AD-structure dump, chunked so each line fits the value column
     * rather than being truncated to an ellipsis. Byte offsets are labelled so
     * a reader can line them up against a scanner capture. */
    constexpr uint8_t PER_ROW = 7;
    for (uint8_t off = 0; off < src.payloadLen; off = static_cast<uint8_t>(off + PER_ROW)) {
        const uint8_t n = static_cast<uint8_t>(min<int>(PER_ROW, src.payloadLen - off));
        addRow(String(off) + "-" + String(off + n - 1),
               BleBeacon::toHex(src.payload + off, n));
    }
}

void SystemInfoGame::buildAppStateRows(GameHost& host) {
    Board& board = host.board();
    const Watchdog::Stats stats = Watchdog::stats();

    beginRows();
    addSection("Device");
    addRow("Theme", board.themeMode() == Board::ThemeMode::Light ? "Light" : "Dark");
    addRow("Layout", board.layoutMode() == Board::LayoutMode::Vertical ? "Portrait" : "Landscape");
    addRow("Brightness", String(board.brightness()) + "%");
    addRow("Saver", String(board.screenSaverSeconds()) + " s");
    addRow("Profile", board.isGuest() ? "Guest" : board.profileName(board.activeProfile()));
    addRow("Touch cal", board.hasTouchCalibration() ? "Calibrated" : "Uncalibrated");
    addRow("SD card", board.sdReady() ? "Ready" : "Not found");
    addRow("Wi-Fi creds", board.hasWifiCredentials() ? "Stored" : "Not set");
    addRow("NTP enabled", board.ntpEnabled() ? "Yes" : "No");

    addSection("Watchdog");
    addRow("State", stats.armed ? "Armed" : "Not armed");
    addRow("Stalls", String(stats.stalls));
    addRow("Uptime", uptimeText(stats.uptimeSeconds));

    addSection("Claims");
    addRow("Internet use", board.tzZoneChosen() ? "NTP only" : "NTP + ip-api.com");
    addRow("BLE beacon", BleBeacon::active() ? "Advertising -- see BLE tab"
                                             : "Off; nothing broadcast");
    addRow("Temp", "Not shown; ESP32 reading not trusted");
}

void SystemInfoGame::drawContent(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());
    const Rect cr = contentRect(W, H);

    switch (tab_) {
        case 0: buildBoardRows(host); break;
        case 1: buildMemoryRows(); break;
        case 2: buildNetworkRows(host); break;
        case 3: buildBleRows(host); break;
        case 4: buildAppStateRows(host); break;
    }
    drawRows(tft, cr);
}

void SystemInfoGame::render(GameHost& host) {
    Board& board = host.board();
    TFT_eSPI& tft = board.display();
    const int16_t W = static_cast<int16_t>(tft.width());

    if (needsFullRender()) {
        Ui::clear(tft);
        Ui::drawTopBar(board, title());
        drawTabStrip(tft, W);
    }
    drawContent(host);
}
