#pragma once

#include "BoardConfig.h"
#include "engine/Game.h"
#include "ui/Ui.h"

class WifiGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    /* Two-phase, but deliberately a FULL-repaint screen -- see the note above
     * renderStatic() in the .cpp and its entry in docs/RENDER_AUDIT.md. */
    void renderStatic(GameHost& host) override;
    void renderDynamic(GameHost& host) override;

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

    /* This screen was drawn against 320x240 and says so in every rect. Rather
     * than restate ~17 literals -- which are duplicated between the hit tests
     * and the drawing, so each would have had to be found twice and could be
     * fixed in one place and not the other -- everything goes through one
     * mapping from that design size onto the live panel.
     *
     * At 320x240 it is the identity, so the board this was drawn for keeps the
     * exact numbers it had. On a larger panel the layout fills it and the
     * targets grow, while the text stays its own size: this is a system app,
     * so it gets more room rather than bigger type. */
    int16_t panelW_ = SCREEN_WIDTH;
    int16_t panelH_ = SCREEN_HEIGHT;
    void syncPanel(GameHost& host);
    Rect baseRect(int16_t x, int16_t y, int16_t w, int16_t h) const;
    int16_t baseX(int16_t x) const;
    int16_t baseY(int16_t y) const;

    Rect netRect(uint8_t slot) const;
    Rect zoneRect(uint8_t slot) const;
    Rect keyRect(uint8_t row, uint8_t col) const;
};
