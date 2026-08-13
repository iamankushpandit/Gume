#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class SystemInfoGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class RowKind : uint8_t {
        Section,
        Text,
        Meter,
        Action
    };

    struct Row {
        RowKind kind = RowKind::Text;
        String label;
        String value;
        uint16_t valueColor = 0;
        uint8_t meterPct = 0;
        uint16_t meterColor = 0;
        int16_t height = 16;
    };

    struct TrafficSnapshot {
        bool available = false;
        bool bytes = false;
        uint32_t rx = 0;
        uint32_t tx = 0;
    };

    static constexpr uint8_t TAB_COUNT = 5;
    static constexpr uint32_t REFRESH_MS = 1000;
    static constexpr uint8_t MAX_ROWS = 48;
    static const char* const TAB_LABELS[TAB_COUNT];

    Rect tabRect(uint8_t idx, int16_t screenW, int16_t stripY) const;
    Rect contentRect(int16_t screenW, int16_t screenH) const;

    bool refreshTelemetry(bool force);
    TrafficSnapshot readTrafficCounters() const;
    void beginRows();
    void addSection(const String& title);
    void addRow(const String& label, const String& value, uint16_t valueColor = 0, int16_t height = 16);
    void addMeter(uint8_t pct, uint16_t color);
    /* Tappable chip inside the scrolling list. drawRows() records where it
     * landed on screen so update() can hit-test it on the next frame. */
    void addAction(const String& label);
    int16_t rowsHeight() const;
    void drawRows(TFT_eSPI& tft, const Rect& r);
    void drawScrollBar(TFT_eSPI& tft, const Rect& r, int16_t totalHeight) const;
    void clampScroll(uint8_t tabIndex, int16_t viewportH);

    void drawTabStrip(TFT_eSPI& tft, int16_t screenW);
    void drawContent(GameHost& host);
    void buildBoardRows(GameHost& host);
    void buildMemoryRows();
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
    Row rows_[MAX_ROWS];
    uint8_t rowCount_ = 0;
    int16_t scrollOffset_[TAB_COUNT] = {0, 0, 0, 0, 0};
    Rect actionRect_{};          // on-screen position of the Action row, if drawn
    bool bleAdvanced_ = false;   // Advanced BLE diagnostics expanded
    bool scrolling_ = false;
    int16_t scrollAnchorY_ = 0;
    int16_t scrollStartOffset_ = 0;
};
