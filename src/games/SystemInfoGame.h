#pragma once

#include "engine/Game.h"
#include "ui/RowList.h"
#include "ui/Ui.h"

class SystemInfoGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    /* Two-phase, though this screen was already careful: its row rebuild is
     * throttled to REFRESH_MS and RowList wipes its own rect. Unlike Nearby the
     * data here genuinely ticks -- uptime, time since sync, event ages -- so
     * the repaint is earned. See docs/RENDER_AUDIT.md. */
    void renderStatic(GameHost& host) override;
    void renderDynamic(GameHost& host) override;
    void end(GameHost& host) override;

private:
    struct TrafficSnapshot {
        bool available = false;
        bool bytes = false;
        uint32_t rx = 0;
        uint32_t tx = 0;
    };

    static constexpr uint8_t TAB_COUNT = 5;
    static constexpr uint32_t REFRESH_MS = 1000;
    static const char* const TAB_LABELS[TAB_COUNT];

    Rect tabRect(uint8_t idx, int16_t screenW, int16_t stripY) const;
    Rect contentRect(int16_t screenW, int16_t screenH) const;

    bool refreshTelemetry(bool force);
    TrafficSnapshot readTrafficCounters() const;
    /* Refill rows_ for the active tab. Called only when something the rows
     * depend on actually changed -- not once per frame. */
    void rebuildRows(GameHost& host);

    void drawTabStrip(Ui::Renderer& tft, int16_t screenW);
    void drawContent(GameHost& host);
    void buildBoardRows(GameHost& host);
    void buildMemoryRows(GameHost& host);
    void buildNetworkRows(GameHost& host);
    void buildBleRows(GameHost& host);
    void buildAppStateRows(GameHost& host);

    uint8_t tab_ = 0;
    uint32_t lastRefreshMs_ = 0;
    uint32_t lastTrafficSampleMs_ = 0;
    uint32_t lastTrafficRx_ = 0;
    uint32_t lastTrafficTx_ = 0;
    uint32_t rxRate_ = 0;
    uint32_t txRate_ = 0;
    bool trafficAvailable_ = false;
    bool trafficBytes_ = false;
    RowList rows_;
    bool rowsStale_ = true;
    int16_t scrollOffset_[TAB_COUNT] = {0, 0, 0, 0, 0};
    bool bleAdvanced_ = false;   // Advanced BLE diagnostics expanded
    bool scrolling_ = false;
    int16_t scrollAnchorY_ = 0;
    int16_t scrollStartOffset_ = 0;
};
