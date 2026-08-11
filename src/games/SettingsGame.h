#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"
#include "engine/GameCatalog.h"

class SettingsGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Tab : uint8_t { Device, Games };

    // Ids and labels come from engine/GameCatalog.h -- single source of truth.
    static constexpr uint8_t GAME_COUNT = GAME_CATALOG_COUNT;

    Rect tabRect(Tab t) const;
    Rect themeRect() const;
    Rect layoutRect() const;
    Rect saverRect() const;
    Rect ntpRect() const;
    Rect wifiRect() const;
    Rect flipRect() const;
    Rect brightRect() const;
    Rect gameCheckRect(uint8_t row) const;
    Rect gamesPrevRect() const;
    Rect gamesNextRect() const;
    void cycleScreenSaver(Board& board);
    void renderDeviceTab(GameHost& host);
    void renderGamesTab(GameHost& host);

    Tab  tab_  = Tab::Device;
    uint8_t gameScroll_ = 0; // scroll offset for game list
};