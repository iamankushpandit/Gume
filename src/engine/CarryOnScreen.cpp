#include "CarryOnScreen.h"

#include "BoardConfig.h"
#include "engine/AppRegistry.h"

namespace {
/* Laid out against the surface it is given, at the moment it is drawn: for
 * most games that is the 320x240 game canvas, for a game that follows the
 * owner's orientation it is the panel itself, either way up. Nothing here is
 * a position typed in -- the two buttons share a row under the middle of the
 * screen, which is where a quiz's answers are and so where a hand already
 * is, and the sentence wraps where the panel is narrow. */
constexpr int16_t MARGIN = 18;
constexpr int16_t GAP = 20;
constexpr int16_t BUTTON_H = 46;
constexpr int16_t HEADLINE_H = 30;
constexpr int16_t SENTENCE_H = 40;      // two lines of font 2, for a narrow panel

struct Layout {
    Rect headline;
    Rect sentence;
    Rect carryOn;
    Rect newGame;
};

Layout layoutFor(int16_t w, int16_t h) {
    const int16_t below = static_cast<int16_t>(h - TOP_BAR_HEIGHT);
    const int16_t buttonW = static_cast<int16_t>((w - 2 * MARGIN - GAP) / 2);
    const int16_t headlineY = static_cast<int16_t>(TOP_BAR_HEIGHT + below / 6);
    const int16_t buttonY = static_cast<int16_t>(TOP_BAR_HEIGHT + below / 2 + 8);
    Layout l;
    l.headline = Rect{0, headlineY, w, HEADLINE_H};
    l.sentence = Rect{MARGIN, static_cast<int16_t>(headlineY + HEADLINE_H + 4),
                      static_cast<int16_t>(w - 2 * MARGIN), SENTENCE_H};
    l.carryOn = Rect{MARGIN, buttonY, buttonW, BUTTON_H};
    l.newGame = Rect{static_cast<int16_t>(MARGIN + buttonW + GAP), buttonY, buttonW, BUTTON_H};
    return l;
}

/* The smallest surface this is ever drawn on is the game canvas. On it the
 * sentence must end above the buttons and the buttons above the bottom edge;
 * anything larger only has more room. */
constexpr int16_t CANVAS_BELOW = GAME_CANVAS_HEIGHT - TOP_BAR_HEIGHT;
static_assert(TOP_BAR_HEIGHT + CANVAS_BELOW / 6 + HEADLINE_H + 4 + SENTENCE_H <=
                  TOP_BAR_HEIGHT + CANVAS_BELOW / 2 + 8,
              "the sentence would run into the buttons");
static_assert(TOP_BAR_HEIGHT + CANVAS_BELOW / 2 + 8 + BUTTON_H + Ui::BUTTON_SHADOW_DY <
                  GAME_CANVAS_HEIGHT,
              "the buttons would leave the canvas");
}

const char* CarryOnScreen::title() const {
    return app_ != nullptr ? app_->title() : "";
}

void CarryOnScreen::begin(GameHost& host) {
    (void)host;
    choice_ = Choice::None;
    markFullDirty();
}

void CarryOnScreen::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }
    Ui::Renderer& tft = host.display();
    const Layout l = layoutFor(tft.width(), tft.height());
    if (l.carryOn.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        choice_ = Choice::CarryOn;
    } else if (l.newGame.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        choice_ = Choice::NewGame;
    }
}

void CarryOnScreen::renderStatic(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const Layout l = layoutFor(tft.width(), tft.height());
    Ui::clear(tft);
    host.drawTopBar(title());

    Ui::drawLabel(tft, l.headline, "Carry on?", Ui::text(), 4, Align::Center);
    Ui::drawWrappedText(tft, "Your game is where you left it.", l.sentence, Ui::muted(), 2,
                        Align::Center);

    /* Carry on is the one a player who got here by mistake wants, so it is
     * first, on the left, and the filled one. */
    const uint16_t fill = Ui::accent();
    Ui::drawButton(tft, l.carryOn, "Carry on", fill, Ui::outline(), Ui::onFill(fill), false, 2);
    Ui::drawButton(tft, l.newGame, "New game", Ui::panel(), Ui::outline(), Ui::text(), false, 2);
}
