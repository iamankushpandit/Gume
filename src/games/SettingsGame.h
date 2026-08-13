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
    Rect themeRect() const;
    Rect layoutRect() const;
    Rect saverRect() const;
    Rect ntpRect() const;
    Rect bleRect() const;
    Rect wifiRect() const;
    Rect resetRect() const;
    Rect brightRect() const;
    void cycleScreenSaver(Board& board);
    void renderDeviceTab(GameHost& host);

    bool confirmReset_ = false;
};
