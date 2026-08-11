#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class SettingsGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Tab : uint8_t { Device, Games };

    static const char* GAME_IDS[];
    static const char* GAME_LABELS[];
    static constexpr uint8_t GAME_COUNT = 23;

    Rect tabRect(Tab t) const;
    Rect themeRect() const;
    Rect layoutRect() const;
    Rect saverRect() const;
    Rect ntpRect() const;
    Rect wifiRect() const;
    Rect gameCheckRect(uint8_t row) const;
    Rect gamesPrevRect() const;
    Rect gamesNextRect() const;
    void cycleScreenSaver(Board& board);
    void renderDeviceTab(GameHost& host);
    void renderGamesTab(GameHost& host);

    Tab  tab_  = Tab::Device;
    uint8_t gameScroll_ = 0; // scroll offset for game list
};