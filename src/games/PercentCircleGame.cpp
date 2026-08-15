#include "PercentCircleGame.h"

// Agent B owns this file — see src/games/CLAUDE.md

const char* PercentCircleGame::title() const {
    return "Percent";
}

void PercentCircleGame::begin(GameHost& host) {
    markFullDirty();
}

void PercentCircleGame::update(GameHost& host, const TouchPoint& touch) {
    (void)touch;
}

void PercentCircleGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    Ui::drawLabel(tft, Rect{20, 100, 280, 40}, "Coming soon", Ui::muted(), 2, Align::Center);
    tft.setTextDatum(TL_DATUM);
}
