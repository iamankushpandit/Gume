#include "WifiGame.h"
#include <WiFi.h>
#include <esp_sntp.h>
#include "hal/Board.h"
#include "hal/Clock.h"

const char WifiGame::KEYS_LOWER[4][11] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl-",
    "zxcvbnm.@_"
};
const char WifiGame::KEYS_UPPER[4][11] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL-",
    "ZXCVBNM.@_"
};
/* Symbol layer. The letter layers only carried  - . @ _  which is not
 * enough for a typical WPA passphrase. */
const char WifiGame::KEYS_SYMBOL[4][11] = {
    "1234567890",
    "!@#$%^&*()",
    "-_=+[]{};:",
    "'\",.<>/?~\\",
};

const char* WifiGame::title() const { return "Wi-Fi"; }

void WifiGame::begin(GameHost& host) {
    (void)host.requireCapability(APP_CAP_NETWORK, "open wifi");
    phase_ = Phase::Idle;
    password_ = "";
    capsLock_ = false;
    selectedNet_ = -1;
    listPage_ = 0;
    netCount_ = 0;
    connectOk_ = false;
    markDirty();
}

namespace {
constexpr int16_t NET_TOP    = 48;
constexpr int16_t NET_PITCH  = 31;   // keeps the rows clear of the pager row
constexpr int16_t NET_H      = 29;
constexpr int16_t ZONE_TOP   = 46;
constexpr int16_t ZONE_PITCH = 28;
constexpr int16_t ZONE_H     = 26;
constexpr uint8_t KEY_ROWS   = 4;
constexpr uint8_t KEY_COLS   = 10;
constexpr int16_t KEYS_H     = 112;
constexpr int16_t MENU_ROW_Y[3] = {64, 132, 172};
/* A small fixed chip; it fits every supported panel unchanged. */
constexpr Rect BACK_CHIP{8, 34, 60, 20};
}

/* The Idle menu is three rows of two, each row splitting the width. On a
 * 320px panel gridCell lands on the authored x=14 and x=166, 140 wide. */
Rect WifiGame::menuRow(const Ui::Frame& f, uint8_t row) const {
    return Rect{14, MENU_ROW_Y[row], static_cast<int16_t>(f.w - 28), 30};
}
Rect WifiGame::menuCell(const Ui::Frame& f, uint8_t row, uint8_t col) const {
    return Ui::gridCell(menuRow(f, row), 2, 1, col, 12);
}

Rect WifiGame::netRect(const Ui::Frame& f, uint8_t slot) const {
    return Rect{8, static_cast<int16_t>(NET_TOP + slot * NET_PITCH),
                static_cast<int16_t>(f.w - 16), NET_H};
}

Rect WifiGame::zoneRect(const Ui::Frame& f, uint8_t slot) const {
    return Rect{8, static_cast<int16_t>(ZONE_TOP + slot * ZONE_PITCH),
                static_cast<int16_t>(f.w - 16), ZONE_H};
}

/* Both lists fill the height between their header and the pager, so a taller
 * panel shows more networks rather than more blank space. Five rows on a
 * 320x240 panel, as authored. */
uint8_t WifiGame::listRowsPerPage(const Ui::Frame& f) const {
    const int16_t rows = static_cast<int16_t>((pagerBand(f).y - NET_TOP) / NET_PITCH);
    if (rows < 1) return 1;
    if (rows > MAX_NETS) return MAX_NETS;
    return static_cast<uint8_t>(rows);
}

uint8_t WifiGame::zoneRowsPerPage(const Ui::Frame& f) const {
    const int16_t rows = static_cast<int16_t>((pagerBand(f).y - ZONE_TOP) / ZONE_PITCH);
    if (rows < 1) return 1;
    return static_cast<uint8_t>(rows);
}

Rect WifiGame::pagerBand(const Ui::Frame& f) const {
    return Rect{8, static_cast<int16_t>(f.h - 34), static_cast<int16_t>(f.w - 16), 26};
}
Rect WifiGame::pagerCell(const Ui::Frame& f, uint8_t index) const {
    return Ui::gridCell(pagerBand(f), 3, 1, index, 8);
}

Rect WifiGame::passwordRect(const Ui::Frame& f) const {
    return Rect{8, 54, static_cast<int16_t>(f.w - 16), 34};
}

/* The keyboard hangs off its action row rather than off y=96, so it stays
 * above CAPS/SPACE/JOIN whatever the panel height. Ten columns will not fit a
 * 240px panel at the authored 32px pitch, so the keys share the width. */
Rect WifiGame::keyActionRow(const Ui::Frame& f) const {
    return Rect{2, static_cast<int16_t>(f.h - 32), static_cast<int16_t>(f.w - 4), 24};
}
Rect WifiGame::keyActionRect(const Ui::Frame& f, uint8_t index) const {
    return Ui::gridCell(keyActionRow(f), 5, 1, index, 4);
}
Rect WifiGame::keysBand(const Ui::Frame& f) const {
    return Rect{2, static_cast<int16_t>(keyActionRow(f).y - KEYS_H - 6),
                static_cast<int16_t>(f.w - 4), KEYS_H};
}
Rect WifiGame::keyRect(const Ui::Frame& f, uint8_t row, uint8_t col) const {
    return Ui::gridCell(keysBand(f), KEY_COLS, KEY_ROWS,
                        static_cast<uint8_t>(row * KEY_COLS + col), 4);
}

Rect WifiGame::backChipRect(const Ui::Frame& f) const {
    (void)f;
    return BACK_CHIP;
}

void WifiGame::startScan(GameHost& host) {
    if (!host.requireCapability(APP_CAP_NETWORK, "scan wifi")) {
        return;
    }
    /* Two-step: flip to the Scanning phase and let the host render the
     * "Scanning..." screen, then run the actual scan on the NEXT update tick.
     * The scan itself BLOCKS for a few seconds.
     *
     * The async form (scanNetworks(true, ...) polled via scanComplete()) is
     * what silently returned nothing on this board. An isolated radio test
     * found 58 access points with a blocking scan, so we use the call pattern
     * that demonstrably works rather than the tidier one. */
    phase_ = Phase::Scanning;
    scanPending_ = true;
    netCount_ = 0;
    listPage_ = 0;
    scanStart_ = millis();
    markDirty();
}

void WifiGame::runScan() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);       // a sleeping radio misses probe responses
    delay(200);
    WiFi.scanDelete();

    // blocking, show_hidden, active, 300ms per channel -> roughly 4 seconds
    const int16_t n = WiFi.scanNetworks(false, true, false, 300);
    lastScanCode_ = n;

    netCount_ = 0;
    if (n > 0) {
        /* Dedupe by SSID keeping the strongest signal. A mesh router advertises
         * the same name from every node, which otherwise fills the entire list
         * with repeats. Hidden APs are dropped: they cannot be joined here. */
        for (int16_t i = 0; i < n && netCount_ < static_cast<int8_t>(MAX_NETS); ++i) {
            const String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;

            int8_t existing = -1;
            for (int8_t k = 0; k < netCount_; ++k) {
                if (WiFi.SSID(netIdx_[k]) == ssid) { existing = k; break; }
            }
            if (existing >= 0) {
                if (WiFi.RSSI(i) > WiFi.RSSI(netIdx_[existing])) {
                    netIdx_[existing] = static_cast<int8_t>(i);
                }
                continue;
            }
            netIdx_[netCount_++] = static_cast<int8_t>(i);
        }

        // Strongest first.
        for (int8_t a = 0; a + 1 < netCount_; ++a) {
            for (int8_t b = static_cast<int8_t>(a + 1); b < netCount_; ++b) {
                if (WiFi.RSSI(netIdx_[b]) > WiFi.RSSI(netIdx_[a])) {
                    const int8_t t = netIdx_[a]; netIdx_[a] = netIdx_[b]; netIdx_[b] = t;
                }
            }
        }
    }

    listPage_ = 0;
    phase_ = Phase::List;
    markDirty();
}

void WifiGame::checkScan() {
    // Runs one tick after startScan(), so the "Scanning..." screen is visible
    // before we block.
    if (scanPending_) {
        scanPending_ = false;
        runScan();
    }
}

void WifiGame::startConnect(GameHost& host) {
    if (!host.requireCapability(APP_CAP_NETWORK, "join wifi")) {
        return;
    }
    Board& board = host.board();

    String ssid = selectedSsid_;
    if (ssid.length() == 0 && selectedNet_ >= 0) ssid = WiFi.SSID(selectedNet_);
    if (ssid.length() == 0) ssid = board.wifiSsid();

    Serial.printf("[wifi] JOIN ssid='%s' (len %u) pass len %u\n",
                  ssid.c_str(), (unsigned)ssid.length(), (unsigned)password_.length());

    if (ssid.length() > 0) {
        board.setWifiCredentials(ssid, password_);
    } else {
        Serial.println("[wifi] REFUSING to save an empty SSID");
    }
    saveReadback_ = board.wifiSsid();
    Serial.printf("[wifi] readback='%s'\n", saveReadback_.c_str());

    WiFi.begin(ssid.c_str(), password_.c_str());
    connectStart_ = millis();
    connectOk_ = false;
    phase_ = Phase::Connecting;
    markDirty();
}

void WifiGame::checkConnect(GameHost& host) {
    if (!host.requireCapability(APP_CAP_NETWORK, "finish wifi join")) {
        phase_ = Phase::Idle;
        markDirty();
        return;
    }
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        connectOk_ = true;
        // Delegate to the board so the stored timezone is applied. This used to
        // call configTime(0, 0, ...) directly, which pinned the clock to UTC.
        host.board().beginTimeSync();
        phase_ = Phase::Done;
        markDirty();
        return;
    }
    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL ||
        millis() - connectStart_ > 15000UL) {
        connectOk_ = false;
        phase_ = Phase::Done;
        markDirty();
    }
}

void WifiGame::update(GameHost& host, const TouchPoint& touch) {
    const Ui::Frame f = Ui::frame(host.display());

    if (phase_ == Phase::Idle) {
        // Repaint when the link or sync state changes, so the badges and the
        // status line do not sit stale while the user watches them.
        const bool up = Ui::wifiUp();
        const bool sy = host.board().timeSynced();
        if (up != lastUpShown_ || sy != lastSyncShown_) {
            lastUpShown_ = up;
            lastSyncShown_ = sy;
            markDirty();
        }
    }
    if (phase_ == Phase::Scanning) {
        if (!host.requireCapability(APP_CAP_NETWORK, "run wifi scan")) {
            phase_ = Phase::Idle;
            markDirty();
            return;
        }
        checkScan();
        return;
    }
    if (phase_ == Phase::Connecting) { checkConnect(host); return; }

    if (!touch.justPressed) return;
    if (!host.requireCapability(APP_CAP_NETWORK, "change wifi settings")) {
        return;
    }

    Board& board = host.board();

    if (phase_ == Phase::Idle) {
        // --- Wi-Fi section ---
        if (menuCell(f, 0, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { startScan(host); return; }
        if (menuCell(f, 0, 1).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && board.hasWifiCredentials()) {
            board.clearWifiCredentials(); markDirty(); return;
        }
        // --- Time section ---
        if (menuCell(f, 1, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.setNtpEnabled(!board.ntpEnabled()); markDirty(); return;
        }
        if (menuCell(f, 1, 1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            zonePage_ = static_cast<uint8_t>(board.tzZoneIndex() / zoneRowsPerPage(f));
            phase_ = Phase::TimeZone; markDirty(); return;
        }
        if (menuCell(f, 2, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (Ui::wifiUp()) { board.syncTimeNow(); markDirty(); }
            return;
        }
        if (menuCell(f, 2, 1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { host.openSettings(); return; }

    } else if (phase_ == Phase::List) {
        const uint8_t pages = max<uint8_t>(1, (netCount_ + listRowsPerPage(f) - 1) / listRowsPerPage(f));
        if (pagerCell(f, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && listPage_ > 0) {
            --listPage_; markDirty(); return;
        }
        if (pagerCell(f, 2).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && listPage_ + 1 < pages) {
            ++listPage_; markDirty(); return;
        }
        if (pagerCell(f, 1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::Idle; markDirty(); return;
        }
        for (uint8_t slot = 0; slot < listRowsPerPage(f); ++slot) {
            const uint8_t idx = static_cast<uint8_t>(listPage_ * listRowsPerPage(f) + slot);
            if (idx >= netCount_) break;
            if (netRect(f, slot).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                selectedNet_ = netIdx_[idx];
                selectedSsid_ = WiFi.SSID(netIdx_[idx]);   // capture now, not at JOIN
                Serial.printf("[wifi] selected '%s'\n", selectedSsid_.c_str());
                password_ = "";
                capsLock_ = false;
                phase_ = Phase::Keyboard;
                markDirty();
                return;
            }
        }

    } else if (phase_ == Phase::Keyboard) {
        const char (*keys)[11] = symbols_ ? KEYS_SYMBOL
                                             : (capsLock_ ? KEYS_UPPER : KEYS_LOWER);
        for (uint8_t row = 0; row < 4; ++row) {
            for (uint8_t col = 0; col < 10; ++col) {
                if (keyRect(f, row, col).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                    if (password_.length() < 63) password_ += keys[row][col];
                    markDirty(); return;
                }
            }
        }
        if (keyActionRect(f, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            capsLock_ = !capsLock_; symbols_ = false; markDirty(); return;
        }
        if (keyActionRect(f, 1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            symbols_ = !symbols_; markDirty(); return;
        }
        if (keyActionRect(f, 2).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (password_.length() < 63) password_ += ' ';
            markDirty(); return;
        }
        if (keyActionRect(f, 3).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (password_.length() > 0) password_.remove(password_.length() - 1);
            markDirty(); return;
        }
        if (keyActionRect(f, 4).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { startConnect(host); return; }
        if (backChipRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { phase_ = Phase::List; markDirty(); }

    } else if (phase_ == Phase::TimeZone) {
        const uint8_t n = Board::tzZoneCount();
        const uint8_t pages = static_cast<uint8_t>((n + zoneRowsPerPage(f) - 1) / zoneRowsPerPage(f));
        if (pagerCell(f, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && zonePage_ > 0) {
            --zonePage_; markDirty(); return;
        }
        if (pagerCell(f, 2).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && zonePage_ + 1 < pages) {
            ++zonePage_; markDirty(); return;
        }
        if (pagerCell(f, 1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::Idle; markDirty(); return;
        }
        for (uint8_t slot = 0; slot < zoneRowsPerPage(f); ++slot) {
            const uint8_t idx = static_cast<uint8_t>(zonePage_ * zoneRowsPerPage(f) + slot);
            if (idx >= n) break;
            if (zoneRect(f, slot).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                board.setTzZoneIndex(idx);
                phase_ = Phase::Idle;
                markDirty();
                return;
            }
        }

    } else if (phase_ == Phase::Done) {
        phase_ = Phase::Idle; markDirty();
    }
}

void WifiGame::render(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    /* The three full-screen message phases centre their block of text in the
     * content area instead of sitting at y=88..200 of a 240px canvas. On a
     * 320x240 panel this lands on the authored baselines exactly. */
    const Rect content = f.content();
    const int16_t cy = static_cast<int16_t>(content.y + content.h / 2);
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    if (phase_ == Phase::Idle) {
        const String ssid = board.wifiSsid();
        const bool up = Ui::wifiUp();
        const bool hasCreds = ssid.length() > 0;

        // ---------- Wi-Fi ----------
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("WI-FI", 14, 36, 1);
        tft.drawFastHLine(52, 41, static_cast<int16_t>(f.w - 66), Ui::outline());

        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(hasCreds ? ssid : String("No saved network"), 14, 48, 2);
        Ui::drawWifiBadge(tft, static_cast<int16_t>(f.w - 24), 54, Ui::bg());

        Ui::drawButton(tft, menuCell(f, 0, 0), "Scan Wi-Fi", Ui::rgb(36, 132, 204), Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawButton(tft, menuCell(f, 0, 1), hasCreds ? "Forget" : "---",
                       hasCreds ? Ui::panel() : Ui::surface(), Ui::outline(),
                       hasCreds ? Ui::text() : Ui::muted(), false, 2);

        // ---------- Time ----------
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("TIME", 14, 104, 1);
        tft.drawFastHLine(48, 109, static_cast<int16_t>(f.w - 62), Ui::outline());

        const bool synced = Clock::synced();
        // Date alongside the time: NTP delivers both, and seeing the date is
        // the quickest way to spot a clock that has not actually synced.
        const String stamp = synced ? (Clock::dateText() + "  " + Clock::timeText())
                                    : Clock::timeText();
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(stamp, 14, 116, 2);

        const int16_t badgeX = static_cast<int16_t>(14 + tft.textWidth(stamp, 2) + 12);
        Ui::drawSyncBadge(tft, badgeX, 122, synced, Ui::bg());
        tft.setTextColor(synced ? Ui::success() : Ui::warning(), Ui::bg());
        tft.drawString(synced ? "" : (up ? "not synced" : "needs Wi-Fi"),
                       static_cast<int16_t>(badgeX + 12), 116, 2);

        Ui::drawButton(tft, menuCell(f, 1, 0),
                       String("Auto time: ") + (board.ntpEnabled() ? "On" : "Off"),
                       board.ntpEnabled() ? Ui::rgb(45, 154, 96) : Ui::panel(),
                       Ui::outline(), board.ntpEnabled() ? TFT_WHITE : Ui::text(), false, 2);
        // Say "Auto" until a zone is picked by hand, rather than showing the
        // index-0 default ("UTC") as though it were a deliberate choice.
        Ui::drawButton(tft, menuCell(f, 1, 1),
                       board.tzZoneChosen() ? Board::tzZoneName(board.tzZoneIndex())
                                            : String("Zone: Auto"),
                       Ui::panel(), Ui::outline(), Ui::text(), false, 2);

        Ui::drawButton(tft, menuCell(f, 2, 0), "Sync now",
                       up ? Ui::rgb(45, 154, 96) : Ui::surface(), Ui::outline(),
                       up ? TFT_WHITE : Ui::muted(), false, 2);
        Ui::drawButton(tft, menuCell(f, 2, 1), "Back", Ui::panel(), Ui::outline(), Ui::text(), false, 2);

        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString(board.tzAutoDetected() ? "Zone detected automatically"
                                              : "Tap the zone to change it",
                       f.cx(), static_cast<int16_t>(f.h - 30), 1);
        tft.setTextDatum(TL_DATUM);

    } else if (phase_ == Phase::TimeZone) {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        tft.drawString("Choose your time zone", 8, 34, 2);

        const uint8_t n = Board::tzZoneCount();
        const uint8_t current = board.tzZoneChosen() ? board.tzZoneIndex() : 0xFF;
        for (uint8_t slot = 0; slot < zoneRowsPerPage(f); ++slot) {
            const uint8_t idx = static_cast<uint8_t>(zonePage_ * zoneRowsPerPage(f) + slot);
            if (idx >= n) break;
            const Rect r = zoneRect(f, slot);
            const bool sel = (idx == current);
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, sel ? Ui::rgb(36, 132, 204) : Ui::surface());
            tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
            tft.setTextColor(sel ? TFT_WHITE : Ui::text(), sel ? Ui::rgb(36, 132, 204) : Ui::surface());
            tft.setTextDatum(ML_DATUM);
            tft.drawString(Board::tzZoneName(idx), r.x + 10, r.y + r.h / 2, 2);
        }

        const uint8_t pages = static_cast<uint8_t>((n + zoneRowsPerPage(f) - 1) / zoneRowsPerPage(f));
        Ui::drawPagerButton(tft, pagerCell(f, 0), "Prev", zonePage_ > 0);
        Ui::drawButton(tft, pagerCell(f, 1), "Cancel", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        Ui::drawPagerButton(tft, pagerCell(f, 2), "Next", zonePage_ + 1 < pages);

    } else if (phase_ == Phase::Scanning) {

        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        const uint32_t dots = (millis() / 450) % 4;
        String s = "Scanning";
        for (uint32_t i = 0; i < dots; ++i) s += '.';
        tft.drawString(s, f.cx(), static_cast<int16_t>(cy - 45), 4);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Looking for networks", f.cx(), static_cast<int16_t>(cy - 5), 2);
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString("Please wait...", f.cx(), static_cast<int16_t>(cy + 25), 2);
        markDirty(); // keep refreshing for animation

    } else if (phase_ == Phase::List) {
        // Header above list
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        tft.drawString(netCount_ == 0 ? "No networks found" : String(netCount_) + " found - tap to connect", 8, 34, 2);
        if (netCount_ == 0) {
            // -1 = still running, -2 = scan failed, 0 = radio saw nothing.
            tft.setTextColor(Ui::muted(), Ui::bg());
            tft.setTextDatum(TR_DATUM);
            tft.drawString(String("code ") + lastScanCode_, static_cast<int16_t>(f.w - 8), 34, 1);
            tft.setTextDatum(TL_DATUM);
            tft.setTextColor(Ui::text(), Ui::bg());
            Ui::drawLabel(tft, Rect{20, 96, static_cast<int16_t>(f.w - 40), 20},
                          "2.4GHz only - a 5GHz-only network will not appear",
                          Ui::muted(), 2, Align::Center);
        }
        // Network rows starting below header
        for (uint8_t slot = 0; slot < listRowsPerPage(f); ++slot) {
            const uint8_t idx = static_cast<uint8_t>(listPage_ * listRowsPerPage(f) + slot);
            if (idx >= static_cast<uint8_t>(netCount_)) break;
            const Rect r = netRect(f, slot);
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, Ui::surface());
            tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
            tft.setTextColor(Ui::text(), Ui::surface());
            tft.setTextDatum(ML_DATUM);
            String name = WiFi.SSID(netIdx_[idx]);
            while (name.length() > 2 && tft.textWidth(name, 2) > r.w - 84) name.remove(name.length() - 1);
            tft.drawString(name, r.x + 8, r.y + r.h / 2, 2);
            const int32_t rssi = WiFi.RSSI(netIdx_[idx]);
            const uint8_t bars = rssi > -55 ? 4 : (rssi > -70 ? 3 : (rssi > -80 ? 2 : 1));
            for (uint8_t b = 0; b < 4; ++b) {
                const int16_t bx = static_cast<int16_t>(r.x + r.w - 36 + b * 8);
                const int16_t bh = static_cast<int16_t>(4 + b * 4);
                tft.fillRect(bx, r.y + r.h - 2 - bh, 6, bh, b < bars ? Ui::success() : Ui::outline());
            }
            if (WiFi.encryptionType(netIdx_[idx]) != WIFI_AUTH_OPEN) {
                tft.setTextDatum(MR_DATUM);
                tft.drawString("*", static_cast<int16_t>(r.x + r.w - 42), r.y + r.h / 2, 2);
            }
        }
        const uint8_t pages = max<uint8_t>(1, (netCount_ + listRowsPerPage(f) - 1) / listRowsPerPage(f));
        Ui::drawPagerButton(tft, pagerCell(f, 0), "Prev", listPage_ > 0);
        Ui::drawButton(tft, pagerCell(f, 1), "Back", Ui::panel(), Ui::outline(), Ui::text());
        Ui::drawPagerButton(tft, pagerCell(f, 2), "Next", listPage_ + 1 < pages);

    } else if (phase_ == Phase::Keyboard) {
        const char (*keys)[11] = symbols_ ? KEYS_SYMBOL
                                             : (capsLock_ ? KEYS_UPPER : KEYS_LOWER);
        const String ssid = selectedSsid_.length() ? selectedSsid_ : board.wifiSsid();
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        tft.drawString(String("< back   ") + ssid, 8, 36, 2);
        const Rect pw = passwordRect(f);
        tft.fillRoundRect(pw.x, pw.y, pw.w, pw.h, 4, Ui::surface());
        tft.drawRoundRect(pw.x, pw.y, pw.w, pw.h, 4, Ui::outline());
        tft.setTextColor(password_.length() > 0 ? Ui::text() : Ui::muted(), Ui::surface());
        tft.setTextDatum(ML_DATUM);
        String shown = password_.length() > 0 ? password_ : "Password";
        while (shown.length() > 2 && tft.textWidth(shown, 2) > pw.w - 24) shown.remove(shown.length() - 1);
        tft.drawString(shown, static_cast<int16_t>(pw.x + 8),
                       static_cast<int16_t>(pw.y + pw.h / 2), 2);
        for (uint8_t row = 0; row < 4; ++row) {
            for (uint8_t col = 0; col < 10; ++col) {
                char buf[2] = {keys[row][col], 0};
                Ui::drawButton(tft, keyRect(f, row, col), String(buf), Ui::surface(), Ui::outline(), Ui::text(), false, 2);
            }
        }
        Ui::drawButton(tft, keyActionRect(f, 0), "CAPS",
                       capsLock_ ? Ui::rgb(36, 132, 204) : Ui::surface(), Ui::outline(),
                       capsLock_ ? TFT_WHITE : Ui::text(), false, 1);
        Ui::drawButton(tft, keyActionRect(f, 1), symbols_ ? "abc" : "!#$",
                       symbols_ ? Ui::rgb(36, 132, 204) : Ui::surface(), Ui::outline(),
                       symbols_ ? TFT_WHITE : Ui::text(), false, 1);
        Ui::drawButton(tft, keyActionRect(f, 2), "SPACE", Ui::surface(), Ui::outline(), Ui::text(), false, 1);
        Ui::drawButton(tft, keyActionRect(f, 3), "DEL", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
        Ui::drawButton(tft, keyActionRect(f, 4), "JOIN", Ui::rgb(36, 132, 204), Ui::outline(), TFT_WHITE, false, 2);

    } else if (phase_ == Phase::Connecting) {
        const String ssid = selectedSsid_.length() ? selectedSsid_ : board.wifiSsid();
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Connecting to", f.cx(), static_cast<int16_t>(cy - 47), 2);
        tft.drawString(ssid, f.cx(), static_cast<int16_t>(cy - 23), 4);
        const uint32_t dots = (millis() - connectStart_) / 500 % 4;
        String prog;
        for (uint32_t i = 0; i < dots + 1; ++i) prog += "...";
        tft.drawString(prog, f.cx(), static_cast<int16_t>(cy + 17), 2);
        markDirty();

    } else if (phase_ == Phase::Done) {
        tft.setTextColor(connectOk_ ? Ui::success() : Ui::error(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(connectOk_ ? "Connected!" : "Connection failed",
                       f.cx(), static_cast<int16_t>(cy - 39), 4);
        tft.setTextColor(saveReadback_.length() ? Ui::success() : Ui::error(), Ui::bg());
        tft.drawString(saveReadback_.length() ? String("Saved: ") + saveReadback_
                                              : String("NOT saved - empty SSID"),
                       f.cx(), static_cast<int16_t>(cy - 13), 2);
        if (connectOk_ && board.ntpEnabled()) {
            tft.setTextColor(Ui::text(), Ui::bg());
            tft.drawString("Clock sync started", f.cx(), static_cast<int16_t>(cy + 1), 2);
        }
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Tap to return", f.cx(), static_cast<int16_t>(f.h - 40), 2);
    }
    tft.setTextDatum(TL_DATUM);
}
