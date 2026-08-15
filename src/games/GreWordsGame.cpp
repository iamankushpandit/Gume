#include "GreWordsGame.h"

// Agent C owns this file — see src/games/CLAUDE.md

const char* GreWordsGame::title() const {
    return "GRE Words";
}

void GreWordsGame::begin(GameHost& host) {
    markFullDirty();
}

void GreWordsGame::update(GameHost& host, const TouchPoint& touch) {
    (void)touch;
}

void GreWordsGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    Ui::drawLabel(tft, Rect{20, 100, 280, 40}, "Coming soon", Ui::muted(), 2, Align::Center);
    tft.setTextDatum(TL_DATUM);
}

void GreWordsGame::end(GameHost& host) {
    (void)host;
}
