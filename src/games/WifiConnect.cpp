// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "WifiApp.h"
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_sntp.h>
#include "hal/Board.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"

/* The Wi-Fi screen's radio work: scanning, joining a network and rejoining
 * the saved one. Split from WifiApp.cpp, which keeps the phases, the touch
 * handling and the drawing; nothing here draws. */

void WifiApp::startScan(GameHost& host) {
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

void WifiApp::runScan() {
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

void WifiApp::checkScan() {
    // Runs one tick after startScan(), so the "Scanning..." screen is visible
    // before we block.
    if (scanPending_) {
        scanPending_ = false;
        runScan();
    }
}

void WifiApp::startConnect(GameHost& host) {
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

    /* NOTHING IS SAVED HERE. It used to be: the new SSID and password went
     * into NVS before WiFi.begin() was called, so mistyping a password on a
     * different network destroyed the working one -- the owner came back to a
     * console that could not reach the network it had been using for weeks,
     * and had no way to find out what it used to be. The credentials are
     * written in checkConnect(), once the association has actually
     * succeeded; until then the old ones are the ones on the device. */
    if (ssid.length() == 0) {
        Serial.println("[wifi] REFUSING to join an empty SSID");
        phase_ = Phase::Idle;
        markFullDirty();
        return;
    }

    /* Off here too: the join path is the other place a password reaches
     * esp_wifi, and its own NVS copy is one this firmware cannot clear. See
     * the note in BoardNetwork.cpp. */
    WiFi.persistent(false);
    WiFi.begin(ssid.c_str(), password_.c_str());
    connectStart_ = millis();
    connectOk_ = false;
    phase_ = Phase::Connecting;
    markFullDirty();
}

void WifiApp::checkConnect(GameHost& host) {
    if (!host.requireCapability(APP_CAP_NETWORK, "finish wifi join")) {
        phase_ = Phase::Idle;
        markFullDirty();
        return;
    }
    Board& board = host.board();
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        connectOk_ = true;
        /* Now it is worth keeping. The readback checks that NVS round-tripped
         * what we wrote; the verdict is the diagnostic, and the name was only
         * ever how it was spelled. */
        String ssid = selectedSsid_;
        if (ssid.length() == 0 && selectedNet_ >= 0) ssid = WiFi.SSID(selectedNet_);
        if (ssid.length() == 0) ssid = WiFi.SSID();
        if (ssid.length() > 0) {
            board.setWifiCredentials(ssid, password_);
            saveReadback_ = board.wifiSsid();
            Serial.printf("[wifi] saved after connecting, readback %s\n",
                          saveReadback_ == ssid ? "matches" : "MISMATCH");
        }
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
        /* The attempt failed, so the device is sitting associated with
         * nothing while its saved network is still in NVS untouched. Ask for
         * it back: without this the console stays off the air until it is
         * rebooted, which is indistinguishable from having lost the network.
         * Harmless when nothing is saved -- beginTimeSync() returns at once. */
        rejoinSaved(host);
        phase_ = Phase::Done;
        markFullDirty();
    }
}

/* Put the console back on the network it already had.
 *
 * Every way out of the scan -- Back from the list, < back from the keyboard, a
 * failed join -- leaves the radio associated with nothing, because runScan()
 * cycles WIFI_OFF/WIFI_STA to scan. The credentials in NVS are untouched, so
 * this is only a reconnect; it is what stops "I looked at the other networks"
 * from reading as "my network is gone". */
void WifiApp::rejoinSaved(GameHost& host) {
    Board& board = host.board();
    if (!board.hasWifiCredentials() || Ui::wifiUp()) return;
    board.beginTimeSync();
}
