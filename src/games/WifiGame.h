#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

class WifiGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Phase : uint8_t { Idle, Scanning, List, Keyboard, Connecting, Done, TimeZone };

    static const char KEYS_LOWER[4][11];
    static const char KEYS_UPPER[4][11];
    static const char KEYS_SYMBOL[4][11];

    Phase phase_ = Phase::Idle;
    int8_t netCount_ = 0;
    int8_t selectedNet_ = -1;
    uint8_t listPage_ = 0;
    String password_;
    bool connectOk_ = false;
    bool capsLock_ = false;
    bool symbols_ = false;   // symbol layer: WPA keys are full of these
    uint32_t connectStart_ = 0;
    uint32_t scanStart_ = 0;
    bool scanPending_ = false;    // draw "Scanning..." first, then block
    /* The SSID is captured as a string the moment the row is tapped. Looking it
     * up again at JOIN time via WiFi.SSID(index) depends on the driver's scan
     * table still being allocated, and if it is not the lookup silently yields
     * an empty string -- which is exactly how an empty SSID got saved while the
     * connection itself still succeeded from the ESP32's own stored config. */
    String selectedSsid_;
    String saveReadback_;    // what NVS actually returned after the write
    uint8_t zonePage_ = 0;
    bool lastUpShown_ = false;
    bool lastSyncShown_ = false;
    int16_t lastScanCode_ = 0;    // raw scan result, shown on screen
    static constexpr uint8_t MAX_NETS = 20;
    int8_t netIdx_[MAX_NETS] = {}; // indices into the scan result, deduped

    void startScan(GameHost& host);
    void checkScan();
    void runScan();
    void startConnect(GameHost& host);
    void checkConnect(GameHost& host);

    /* Every rect on this screen was a literal, and most were written out twice
     * -- once in update() to hit-test and once in render() to draw. That is
     * why this was the last screen still pinned to a 320x240 canvas: there
     * were twelve places to change and no single place that owned the layout.
     * They are named helpers now, so a rect exists once and both callers ask
     * for it. On a 320x240 panel they reproduce the authored geometry. */
    Rect menuRow(const Ui::Frame& f, uint8_t row) const;
    Rect menuCell(const Ui::Frame& f, uint8_t row, uint8_t col) const;

    Rect netRect(const Ui::Frame& f, uint8_t slot) const;
    Rect zoneRect(const Ui::Frame& f, uint8_t slot) const;
    uint8_t listRowsPerPage(const Ui::Frame& f) const;
    uint8_t zoneRowsPerPage(const Ui::Frame& f) const;

    /* Prev / middle / Next, bottom-anchored and shared by the network list
     * and the time-zone list. */
    Rect pagerBand(const Ui::Frame& f) const;
    Rect pagerCell(const Ui::Frame& f, uint8_t index) const;

    Rect passwordRect(const Ui::Frame& f) const;
    Rect keysBand(const Ui::Frame& f) const;
    Rect keyRect(const Ui::Frame& f, uint8_t row, uint8_t col) const;
    Rect keyActionRow(const Ui::Frame& f) const;
    Rect keyActionRect(const Ui::Frame& f, uint8_t index) const;
    Rect backChipRect(const Ui::Frame& f) const;
};
