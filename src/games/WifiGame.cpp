#include "WifiGame.h"
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_sntp.h>
#include "hal/Board.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"

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
    markFullDirty();
}

namespace {
/* The size this screen's geometry was authored against. */
constexpr int16_t WIFI_BASE_W = 320;
constexpr int16_t WIFI_BASE_H = 240;
}  // namespace

void WifiGame::syncPanel(GameHost& host) {
    panelW_ = static_cast<int16_t>(host.display().width());
    panelH_ = static_cast<int16_t>(host.display().height());
}

/* Map a rect from the design size onto the panel. Exact identity at 320x240. */
Rect WifiGame::baseRect(int16_t x, int16_t y, int16_t w, int16_t h) const {
    return Rect{static_cast<int16_t>(static_cast<int32_t>(x) * panelW_ / WIFI_BASE_W),
                static_cast<int16_t>(static_cast<int32_t>(y) * panelH_ / WIFI_BASE_H),
                static_cast<int16_t>(static_cast<int32_t>(w) * panelW_ / WIFI_BASE_W),
                static_cast<int16_t>(static_cast<int32_t>(h) * panelH_ / WIFI_BASE_H)};
}

int16_t WifiGame::baseX(int16_t x) const {
    return static_cast<int16_t>(static_cast<int32_t>(x) * panelW_ / WIFI_BASE_W);
}

int16_t WifiGame::baseY(int16_t y) const {
    return static_cast<int16_t>(static_cast<int32_t>(y) * panelH_ / WIFI_BASE_H);
}

Rect WifiGame::netRect(uint8_t slot) const {
    // 31px pitch keeps all five rows clear of the Prev/Back/Next row at y=206.
    return baseRect(8, static_cast<int16_t>(48 + slot * 31), 304, 29);
}

Rect WifiGame::zoneRect(uint8_t slot) const {
    return baseRect(8, static_cast<int16_t>(46 + slot * 28), 304, 26);
}

Rect WifiGame::keyRect(uint8_t row, uint8_t col) const {
    return baseRect(static_cast<int16_t>(2 + col * 32),
                    static_cast<int16_t>(96 + row * 28), 28, 24);
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
    markFullDirty();
}

void WifiGame::runScan() {
    /* THIS BLOCKS FOR ABOUT FOUR AND A HALF SECONDS, ON PURPOSE.
     *
     * A 300ms-per-channel active scan is roughly four seconds, and the mode
     * changes below add another 300ms of settling. That is a deliberate
     * trade -- the network list is something the owner picks from once, and a
     * scan that misses their access point is worse than a screen that pauses.
     *
     * But it is called straight from update(), on the loop task, with the
     * watchdog armed at TIMEOUT_SECONDS = 12 and the monitor logging a stall
     * past STALL_WARN_MS = 3000. So every single Wi-Fi scan this firmware has
     * ever done has logged a stall, and a slower scan -- a busy band, a retry
     * -- moves it towards a reboot that would look to the owner like the Wi-Fi
     * screen crashing the console.
     *
     * The guard is what every other blocking call in the tree already has:
     * Board::runTouchCalibration(), ntpUdpProbe() and detectTimezone() all
     * take one. This is the last place that blocked for seconds without it. */
    const Watchdog::Pause wdtPause;

    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);

    /* MODEM SLEEP MUST STAY ON WHILE THE BLUETOOTH CONTROLLER IS UP.
     *
     * This line used to be an unconditional `setSleep(false)`, because a
     * sleeping radio misses probe responses and the scan came back short.
     * That is true, and on a board with the BLE beacon switched on it also
     * PANICS THE DEVICE: esp_wifi refuses to share the radio with a
     * Bluetooth controller unless Wi-Fi is power-saving, and it does not
     * degrade or return an error --
     *
     *   E wifi: Should enable WiFi modem sleep when both WiFi and Bluetooth
     *           are enabled!!!!!!
     *   abort() was called
     *
     * The console reboots, which from the outside looks like "Wi-Fi is
     * broken": the beacon is off by default, so this only bites an owner who
     * turned it on and then opened this screen -- and then it bites every
     * single time, in a loop, because the first thing they do on the way back
     * is try again.
     *
     * The condition is asked of the BT controller directly rather than of
     * BleBeacon, deliberately. It is the exact thing esp_wifi tests, so the
     * two cannot drift; going through the beacon's own state would leave a
     * gap wherever the controller is up for some other reason -- the Nearby
     * scanner today, anything else later.
     *
     * The cost is a scan that may miss a distant access point while the
     * beacon is on. That is the right trade against a reboot, and the scan is
     * a list the owner picks from rather than a measurement. */
    const bool btControllerUp =
        esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
    WiFi.setSleep(btControllerUp);
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
    markFullDirty();
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

    /* Lengths, not names. The network's name is the household's, and a
     * serial log is the one thing here that gets pasted into bug reports --
     * see the note in BrainoApp::begin(). A zero length is the failure this
     * line exists to catch, and it survives the redaction intact. */
    Serial.printf("[wifi] JOIN ssid len %u, pass len %u\n",
                  (unsigned)ssid.length(), (unsigned)password_.length());

    if (ssid.length() > 0) {
        board.setWifiCredentials(ssid, password_);
    } else {
        Serial.println("[wifi] REFUSING to save an empty SSID");
    }
    saveReadback_ = board.wifiSsid();
    /* What this checks is that NVS round-tripped what we wrote, so the
     * verdict is the diagnostic and the name was only ever how it was
     * spelled. Comparing here says strictly more than printing it did. */
    Serial.printf("[wifi] readback %s\n",
                  saveReadback_ == ssid ? "matches" : "MISMATCH");

    WiFi.begin(ssid.c_str(), password_.c_str());
    connectStart_ = millis();
    connectOk_ = false;
    phase_ = Phase::Connecting;
    markFullDirty();
}

void WifiGame::checkConnect(GameHost& host) {
    if (!host.requireCapability(APP_CAP_NETWORK, "finish wifi join")) {
        phase_ = Phase::Idle;
        markFullDirty();
        return;
    }
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        connectOk_ = true;
        // Delegate to the board so the stored timezone is applied. This used to
        // call configTime(0, 0, ...) directly, which pinned the clock to UTC.
        host.board().beginTimeSync();
        phase_ = Phase::Done;
        markFullDirty();
        return;
    }
    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL ||
        millis() - connectStart_ > 15000UL) {
        connectOk_ = false;
        phase_ = Phase::Done;
        markFullDirty();
    }
}

void WifiGame::update(GameHost& host, const TouchPoint& touch) {
    syncPanel(host);
    if (phase_ == Phase::Idle) {
        // Repaint when the link or sync state changes, so the badges and the
        // status line do not sit stale while the user watches them.
        const bool up = Ui::wifiUp();
        const bool sy = host.board().timeSynced();
        if (up != lastUpShown_ || sy != lastSyncShown_) {
            lastUpShown_ = up;
            lastSyncShown_ = sy;
            markFullDirty();
        }
    }
    if (phase_ == Phase::Scanning) {
        if (!host.requireCapability(APP_CAP_NETWORK, "run wifi scan")) {
            phase_ = Phase::Idle;
            markFullDirty();
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
        if (baseRect(14, 64, 140, 30).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { startScan(host); return; }
        if (baseRect(166, 64, 140, 30).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && board.hasWifiCredentials()) {
            board.clearWifiCredentials(); markFullDirty(); return;
        }
        // --- Time section ---
        if (baseRect(14, 132, 140, 30).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.setNtpEnabled(!board.ntpEnabled()); markFullDirty(); return;
        }
        if (baseRect(166, 132, 140, 30).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            zonePage_ = static_cast<uint8_t>(board.tzZoneIndex() / 5);
            phase_ = Phase::TimeZone; markFullDirty(); return;
        }
        if (baseRect(14, 172, 140, 30).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (Ui::wifiUp()) { board.syncTimeNow(); markFullDirty(); }
            return;
        }
        if (baseRect(166, 172, 140, 30).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { host.openSettings(); return; }

    } else if (phase_ == Phase::List) {
        const uint8_t pages = max<uint8_t>(1, (netCount_ + 4) / 5);
        if (baseRect(8, 206, 84, 26).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && listPage_ > 0) {
            --listPage_; markFullDirty(); return;
        }
        if (baseRect(228, 206, 84, 26).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && listPage_ + 1 < pages) {
            ++listPage_; markFullDirty(); return;
        }
        if (baseRect(104, 206, 112, 26).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::Idle; markFullDirty(); return;
        }
        for (uint8_t slot = 0; slot < 5; ++slot) {
            const uint8_t idx = listPage_ * 5 + slot;
            if (idx >= netCount_) break;
            if (netRect(slot).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                selectedNet_ = netIdx_[idx];
                selectedSsid_ = WiFi.SSID(netIdx_[idx]);   // capture now, not at JOIN
                Serial.printf("[wifi] selected network %d\n", (int)selectedNet_);
                password_ = "";
                capsLock_ = false;
                phase_ = Phase::Keyboard;
                markFullDirty();
                return;
            }
        }

    } else if (phase_ == Phase::Keyboard) {
        const char (*keys)[11] = symbols_ ? KEYS_SYMBOL
                                             : (capsLock_ ? KEYS_UPPER : KEYS_LOWER);
        for (uint8_t row = 0; row < 4; ++row) {
            for (uint8_t col = 0; col < 10; ++col) {
                if (keyRect(row, col).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                    if (password_.length() < 63) password_ += keys[row][col];
                    markFullDirty(); return;
                }
            }
        }
        if (baseRect(2, 208, 52, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            capsLock_ = !capsLock_; symbols_ = false; markFullDirty(); return;
        }
        if (baseRect(58, 208, 52, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            symbols_ = !symbols_; markFullDirty(); return;
        }
        if (baseRect(114, 208, 74, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (password_.length() < 63) password_ += ' ';
            markFullDirty(); return;
        }
        if (baseRect(192, 208, 50, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (password_.length() > 0) password_.remove(password_.length() - 1);
            markFullDirty(); return;
        }
        if (baseRect(246, 208, 72, 24).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { startConnect(host); return; }
        if (baseRect(8, 34, 60, 20).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) { phase_ = Phase::List; markFullDirty(); }

    } else if (phase_ == Phase::TimeZone) {
        const uint8_t n = Board::tzZoneCount();
        const uint8_t pages = static_cast<uint8_t>((n + 4) / 5);
        if (baseRect(8, 208, 90, 26).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && zonePage_ > 0) {
            --zonePage_; markFullDirty(); return;
        }
        if (baseRect(222, 208, 90, 26).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && zonePage_ + 1 < pages) {
            ++zonePage_; markFullDirty(); return;
        }
        if (baseRect(106, 208, 108, 26).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            phase_ = Phase::Idle; markFullDirty(); return;
        }
        for (uint8_t slot = 0; slot < 5; ++slot) {
            const uint8_t idx = static_cast<uint8_t>(zonePage_ * 5 + slot);
            if (idx >= n) break;
            if (zoneRect(slot).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                board.setTzZoneIndex(idx);
                phase_ = Phase::Idle;
                markFullDirty();
                return;
            }
        }

    } else if (phase_ == Phase::Done) {
        phase_ = Phase::Idle; markFullDirty();
    }
}

/* WI-FI IS A FULL-REPAINT SCREEN, ON PURPOSE.
 *
 * Seven phases, each a different layout -- an idle summary, a scan, a network
 * list, a full on-screen keyboard, a connecting spinner, a result and a
 * timezone picker -- and thirty places that ask for a repaint between them.
 * Almost every one of those is a phase change, where everything on the panel
 * is replaced anyway.
 *
 * The one candidate for a partial repaint is typing: a character changes the
 * password field and nothing else, while the keyboard beneath it is thirty-odd
 * keys that do not move. That is a real win and it is deliberately not taken
 * here. This screen has just had a four-and-a-half second blocking scan put
 * behind a watchdog guard, it is the least exercised screen in the firmware,
 * and it is the one where a stale row costs an owner their network. The
 * keyboard is recorded in the audit as the thing to do next, on its own, with
 * a device in hand.
 *
 * So the split below is a migration onto the base class's methods. Every
 * invalidation is now markFullDirty(), which is exactly what render() gave
 * them before by opening with Ui::clear(). */
void WifiGame::renderStatic(GameHost& host) {
    syncPanel(host);
    Ui::clear(host.display());
    Ui::drawTopBar(host.board(), title());
}

void WifiGame::renderDynamic(GameHost& host) {
    syncPanel(host);
    Board& board = host.board();
    Ui::Renderer& tft = host.display();

    if (phase_ == Phase::Idle) {
        const String ssid = board.wifiSsid();
        const bool up = Ui::wifiUp();
        const bool hasCreds = ssid.length() > 0;

        // ---------- Wi-Fi ----------
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("WI-FI", baseX(14), baseY(36), 1);
        tft.drawFastHLine(baseX(52), baseY(41), static_cast<int16_t>(baseX(52 + 254) - baseX(52)),
                          Ui::outline());

        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(hasCreds ? ssid : String("No saved network"), baseX(14), baseY(48), 2);
        Ui::drawWifiBadge(tft, baseX(296), baseY(54), Ui::bg());

        Ui::drawButton(tft, baseRect(14, 64, 140, 30), "Scan Wi-Fi", Ui::rgb(36, 132, 204), Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawButton(tft, baseRect(166, 64, 140, 30), hasCreds ? "Forget" : "---",
                       hasCreds ? Ui::panel() : Ui::surface(), Ui::outline(),
                       hasCreds ? Ui::text() : Ui::muted(), false, 2);

        // ---------- Time ----------
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("TIME", baseX(14), baseY(104), 1);
        tft.drawFastHLine(baseX(48), baseY(109), static_cast<int16_t>(baseX(48 + 258) - baseX(48)),
                          Ui::outline());

        const bool synced = Clock::synced();
        // Date alongside the time: NTP delivers both, and seeing the date is
        // the quickest way to spot a clock that has not actually synced.
        const String stamp = synced ? (Clock::dateText() + "  " + Clock::timeText())
                                    : Clock::timeText();
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(stamp, baseX(14), baseY(116), 2);

        /* Measured, so it lives in panel pixels -- the 14 it starts from is a
         * design-space origin and has to be mapped before the two are added,
         * or the badge lands short of the text it belongs to and the caption
         * lands on top of it. */
        const int16_t badgeX =
            static_cast<int16_t>(baseX(14) + tft.textWidth(stamp, 2) + 12);
        Ui::drawSyncBadge(tft, badgeX, baseY(122), synced, Ui::bg());
        tft.setTextColor(synced ? Ui::success() : Ui::warning(), Ui::bg());
        tft.drawString(synced ? "" : (up ? "not synced" : "needs Wi-Fi"),
                       static_cast<int16_t>(badgeX + 12), baseY(116), 2);

        Ui::drawButton(tft, baseRect(14, 132, 140, 30),
                       String("Auto time: ") + (board.ntpEnabled() ? "On" : "Off"),
                       board.ntpEnabled() ? Ui::rgb(45, 154, 96) : Ui::panel(),
                       Ui::outline(), board.ntpEnabled() ? TFT_WHITE : Ui::text(), false, 2);
        // Say "Auto" until a zone is picked by hand, rather than showing the
        // index-0 default ("UTC") as though it were a deliberate choice.
        Ui::drawButton(tft, baseRect(166, 132, 140, 30),
                       board.tzZoneChosen() ? Board::tzZoneName(board.tzZoneIndex())
                                            : String("Zone: Auto"),
                       Ui::panel(), Ui::outline(), Ui::text(), false, 2);

        Ui::drawButton(tft, baseRect(14, 172, 140, 30), "Sync now",
                       up ? Ui::rgb(45, 154, 96) : Ui::surface(), Ui::outline(),
                       up ? TFT_WHITE : Ui::muted(), false, 2);
        Ui::drawButton(tft, baseRect(166, 172, 140, 30), "Back", Ui::panel(), Ui::outline(), Ui::text(), false, 2);

        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString(board.tzAutoDetected() ? "Zone detected automatically"
                                              : "Tap the zone to change it",
                       baseX(WIFI_BASE_W / 2), baseY(210), 1);
        tft.setTextDatum(TL_DATUM);

    } else if (phase_ == Phase::TimeZone) {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        tft.drawString("Choose your time zone", baseX(8), baseY(34), 2);

        const uint8_t n = Board::tzZoneCount();
        const uint8_t current = board.tzZoneChosen() ? board.tzZoneIndex() : 0xFF;
        for (uint8_t slot = 0; slot < 5; ++slot) {
            const uint8_t idx = static_cast<uint8_t>(zonePage_ * 5 + slot);
            if (idx >= n) break;
            const Rect r = zoneRect(slot);
            const bool sel = (idx == current);
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, sel ? Ui::rgb(36, 132, 204) : Ui::surface());
            tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
            tft.setTextColor(sel ? TFT_WHITE : Ui::text(), sel ? Ui::rgb(36, 132, 204) : Ui::surface());
            tft.setTextDatum(ML_DATUM);
            tft.drawString(Board::tzZoneName(idx), r.x + 10, r.y + r.h / 2, 2);
        }

        const uint8_t pages = static_cast<uint8_t>((n + 4) / 5);
        Ui::drawPagerButton(tft, baseRect(8, 208, 90, 26), "Prev", zonePage_ > 0);
        Ui::drawButton(tft, baseRect(106, 208, 108, 26), "Cancel", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
        Ui::drawPagerButton(tft, baseRect(222, 208, 90, 26), "Next", zonePage_ + 1 < pages);

    } else if (phase_ == Phase::Scanning) {

        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        const uint32_t dots = (millis() / 450) % 4;
        String s = "Scanning";
        for (uint32_t i = 0; i < dots; ++i) s += '.';
        tft.drawString(s, baseX(WIFI_BASE_W / 2), baseY(90), 4);
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Looking for networks", baseX(WIFI_BASE_W / 2), baseY(130), 2);
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString("Please wait...", baseX(WIFI_BASE_W / 2), baseY(160), 2);
        markFullDirty(); // keep refreshing for animation

    } else if (phase_ == Phase::List) {
        // Header above list
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        tft.drawString(netCount_ == 0 ? "No networks found" : String(netCount_) + " found - tap to connect", baseX(8), baseY(34), 2);
        if (netCount_ == 0) {
            // -1 = still running, -2 = scan failed, 0 = radio saw nothing.
            tft.setTextColor(Ui::muted(), Ui::bg());
            tft.setTextDatum(TR_DATUM);
            tft.drawString(String("code ") + lastScanCode_, baseX(WIFI_BASE_W - 8), baseY(34), 1);
            tft.setTextDatum(TL_DATUM);
            tft.setTextColor(Ui::text(), Ui::bg());
            Ui::drawLabel(tft, baseRect(20, 96, 280, 20),
                          "2.4GHz only - a 5GHz-only network will not appear",
                          Ui::muted(), 2, Align::Center);
        }
        // Network rows starting below header
        for (uint8_t slot = 0; slot < 5; ++slot) {
            const uint8_t idx = listPage_ * 5 + slot;
            if (idx >= static_cast<uint8_t>(netCount_)) break;
            const Rect r = netRect(slot);
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, Ui::surface());
            tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
            tft.setTextColor(Ui::text(), Ui::surface());
            tft.setTextDatum(ML_DATUM);
            String name = WiFi.SSID(netIdx_[idx]);
            while (name.length() > 2 && tft.textWidth(name, 2) > 220) name.remove(name.length() - 1);
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
        const uint8_t pages = max<uint8_t>(1, (netCount_ + 4) / 5);
        Ui::drawPagerButton(tft, baseRect(8, 206, 84, 26), "Prev", listPage_ > 0);
        Ui::drawButton(tft, baseRect(104, 206, 112, 26), "Back", Ui::panel(), Ui::outline(), Ui::text());
        Ui::drawPagerButton(tft, baseRect(228, 206, 84, 26), "Next", listPage_ + 1 < pages);

    } else if (phase_ == Phase::Keyboard) {
        const char (*keys)[11] = symbols_ ? KEYS_SYMBOL
                                             : (capsLock_ ? KEYS_UPPER : KEYS_LOWER);
        const String ssid = selectedSsid_.length() ? selectedSsid_ : board.wifiSsid();
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        tft.drawString(String("< back   ") + ssid, baseX(8), baseY(36), 2);
        /* The one shape on this screen that is not built from a Rect, and so
         * the one the mapping pass missed: it was still drawn at the design
         * size while the text inside it had moved, which put the caption
         * below the box it belongs in. */
        const Rect field = baseRect(8, 54, 304, 34);
        tft.fillRoundRect(field.x, field.y, field.w, field.h, 4, Ui::surface());
        tft.drawRoundRect(field.x, field.y, field.w, field.h, 4, Ui::outline());
        tft.setTextColor(password_.length() > 0 ? Ui::text() : Ui::muted(), Ui::surface());
        tft.setTextDatum(ML_DATUM);
        String shown = password_.length() > 0 ? password_ : "Password";
        const int16_t fieldTextMax = static_cast<int16_t>(field.w - 16);
        while (shown.length() > 2 && tft.textWidth(shown, 2) > fieldTextMax) {
            shown.remove(shown.length() - 1);
        }
        /* Centred in the field rather than at a mapped y of its own, so the
         * two cannot drift apart again. */
        tft.drawString(shown, static_cast<int16_t>(field.x + 8),
                       static_cast<int16_t>(field.y + field.h / 2), 2);
        for (uint8_t row = 0; row < 4; ++row) {
            for (uint8_t col = 0; col < 10; ++col) {
                char buf[2] = {keys[row][col], 0};
                Ui::drawButton(tft, keyRect(row, col), String(buf), Ui::surface(), Ui::outline(), Ui::text(), false, 2);
            }
        }
        Ui::drawButton(tft, baseRect(2, 208, 52, 24), "CAPS",
                       capsLock_ ? Ui::rgb(36, 132, 204) : Ui::surface(), Ui::outline(),
                       capsLock_ ? TFT_WHITE : Ui::text(), false, 1);
        Ui::drawButton(tft, baseRect(58, 208, 52, 24), symbols_ ? "abc" : "!#$",
                       symbols_ ? Ui::rgb(36, 132, 204) : Ui::surface(), Ui::outline(),
                       symbols_ ? TFT_WHITE : Ui::text(), false, 1);
        Ui::drawButton(tft, baseRect(114, 208, 74, 24), "SPACE", Ui::surface(), Ui::outline(), Ui::text(), false, 1);
        Ui::drawButton(tft, baseRect(192, 208, 50, 24), "DEL", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
        Ui::drawButton(tft, baseRect(246, 208, 72, 24), "JOIN", Ui::rgb(36, 132, 204), Ui::outline(), TFT_WHITE, false, 2);

    } else if (phase_ == Phase::Connecting) {
        const String ssid = selectedSsid_.length() ? selectedSsid_ : board.wifiSsid();
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Connecting to", baseX(WIFI_BASE_W / 2), baseY(88), 2);
        tft.drawString(ssid, baseX(WIFI_BASE_W / 2), baseY(112), 4);
        const uint32_t dots = (millis() - connectStart_) / 500 % 4;
        String prog;
        for (uint32_t i = 0; i < dots + 1; ++i) prog += "...";
        tft.drawString(prog, baseX(WIFI_BASE_W / 2), baseY(152), 2);
        markFullDirty();

    } else if (phase_ == Phase::Done) {
        tft.setTextColor(connectOk_ ? Ui::success() : Ui::error(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(connectOk_ ? "Connected!" : "Connection failed", baseX(WIFI_BASE_W / 2), baseY(96), 4);
        tft.setTextColor(saveReadback_.length() ? Ui::success() : Ui::error(), Ui::bg());
        tft.drawString(saveReadback_.length() ? String("Saved: ") + saveReadback_
                                              : String("NOT saved - empty SSID"),
                       baseX(WIFI_BASE_W / 2), baseY(122), 2);
        if (connectOk_ && board.ntpEnabled()) {
            tft.setTextColor(Ui::text(), Ui::bg());
            tft.drawString("Clock sync started", baseX(WIFI_BASE_W / 2), baseY(136), 2);
        }
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Tap to return", baseX(WIFI_BASE_W / 2), baseY(200), 2);
    }
    tft.setTextDatum(TL_DATUM);
}
