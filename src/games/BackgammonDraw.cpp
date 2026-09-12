#include "BackgammonGame.h"

/* Board geometry and every pixel of Backgammon.
 *
 * Nothing on the board repaints wholesale during play. Each point, the bar
 * and the tray is a place with a dirty bit, drawn by an idempotent function
 * that clears its own box and paints the triangle and the stack over it; a
 * move repaints the two or three places it touched. The panel is four parts,
 * each repainted only when what it shows changed. A full repaint happens on
 * entering the screen and starting a game. */

namespace {

// ---- the board: every rectangle below is derived from these ----------------
constexpr int16_t BX = 4;
constexpr int16_t BY = 34;
constexpr int16_t COL = 17;              // one point's width
constexpr int16_t HALF_W = 6 * COL;      // six points
constexpr int16_t BAR_W = 14;
constexpr int16_t BAR_X = BX + HALF_W;
constexpr int16_t RIGHT_X = BAR_X + BAR_W;
constexpr int16_t BOARD_R = RIGHT_X + HALF_W;
constexpr int16_t TRAY_X = BOARD_R + 2;
constexpr int16_t TRAY_W = 18;
constexpr int16_t BOARD_H = 202;
constexpr int16_t PT_H = 96;             // a point's column
constexpr int16_t TRI_H = 88;            // its triangle
constexpr int16_t R = 7;                 // checker radius
constexpr int16_t PITCH = 15;            // checker spacing in a stack
constexpr uint8_t STACK_SHOWN = 5;       // a taller stack shows its count

constexpr int16_t PANEL_X = TRAY_X + TRAY_W + 4;
constexpr int16_t PANEL_W = GAME_CANVAS_WIDTH - PANEL_X - 4;

static_assert(2 * R + 1 <= COL, "a checker is wider than its point");
static_assert(STACK_SHOWN * PITCH <= PT_H, "five checkers overflow a point");
static_assert(2 * PT_H < BOARD_H, "the two rows of points overlap");
static_assert(BY + BOARD_H <= GAME_CANVAS_HEIGHT, "the board falls off the canvas");
static_assert(PANEL_W >= 66, "the panel cannot hold two dice");

constexpr Rect TURN_RECT{PANEL_X, 36, PANEL_W, 16};
constexpr int16_t DIE = 28;
constexpr int16_t DICE_Y = 56;
constexpr Rect DICE_RECT{PANEL_X, DICE_Y, PANEL_W, DIE + 12};
constexpr Rect NEWS_RECT{PANEL_X, 98, PANEL_W, 50};
constexpr Rect ROLL_RECT{PANEL_X, 150, PANEL_W, 26};
constexpr Rect UNDO_RECT{PANEL_X, 180, PANEL_W, 24};
constexpr Rect ACTION_RECT{PANEL_X, 207, PANEL_W, 26};
static_assert(DICE_RECT.y + DICE_RECT.h <= NEWS_RECT.y, "dice run into the pips");
static_assert(NEWS_RECT.y + NEWS_RECT.h <= ROLL_RECT.y, "the message runs into Roll");
static_assert(ACTION_RECT.y + ACTION_RECT.h + Ui::BUTTON_SHADOW_DY <= GAME_CANVAS_HEIGHT,
              "End's shadow falls off the canvas");

// ---- colours: board art, fixed in every theme like Chess's squares ---------
uint16_t felt()      { return Ui::rgb(28, 96, 62); }
uint16_t wood()      { return Ui::rgb(96, 62, 38); }
uint16_t trayWood()  { return Ui::rgb(70, 45, 28); }
uint16_t triLight()  { return Ui::rgb(222, 190, 140); }
uint16_t triDark()   { return Ui::rgb(160, 70, 52); }
uint16_t whiteFill() { return Ui::rgb(246, 242, 232); }
uint16_t whiteEdge() { return Ui::rgb(120, 110, 95); }
uint16_t blackFill() { return Ui::rgb(44, 44, 54); }
uint16_t blackEdge() { return Ui::rgb(190, 190, 205); }
uint16_t dieFace()   { return Ui::rgb(252, 252, 250); }
uint16_t dieUsed()   { return Ui::rgb(150, 150, 150); }
uint16_t ink()       { return Ui::rgb(26, 34, 48); }
/* A source that is picked up, and where it can go: cyan, which is none of
 * the board's own colours. */
uint16_t hilite()    { return Ui::rgb(120, 230, 255); }

uint32_t fnv(uint32_t h, uint8_t b) { return (h ^ b) * 16777619U; }

}   // namespace

// ---- geometry ------------------------------------------------------------------

Rect BackgammonGame::pointRect(uint8_t i) {
    /* White's 1-point is bottom right and the points run anticlockwise to the
     * 24 at top right, so White travels right-to-left along the top and
     * left-to-right along the bottom into its home, bottom right. */
    int16_t x;
    if (i < 6) {
        x = static_cast<int16_t>(RIGHT_X + (5 - i) * COL);
    } else if (i < 12) {
        x = static_cast<int16_t>(BX + (11 - i) * COL);
    } else if (i < 18) {
        x = static_cast<int16_t>(BX + (i - 12) * COL);
    } else {
        x = static_cast<int16_t>(RIGHT_X + (i - 18) * COL);
    }
    const int16_t y = i >= 12 ? BY : static_cast<int16_t>(BY + BOARD_H - PT_H);
    return Rect{x, y, COL, PT_H};
}

Rect BackgammonGame::barRect() { return Rect{BAR_X, BY, BAR_W, BOARD_H}; }
Rect BackgammonGame::trayRect() { return Rect{TRAY_X, BY, TRAY_W, BOARD_H}; }
Rect BackgammonGame::rollRect() { return ROLL_RECT; }
Rect BackgammonGame::undoRect() { return UNDO_RECT; }
Rect BackgammonGame::actionRect() { return ACTION_RECT; }

Rect BackgammonGame::lobbyRowRect(uint8_t row) {
    return Rect{10, static_cast<int16_t>(38 + row * 34), 300, 30};
}

uint8_t BackgammonGame::placeAt(int16_t x, int16_t y) {
    if (y < BY || y >= BY + BOARD_H) {
        return Bg::NO_POINT;
    }
    /* By column, not by Rect slop: at 17px the columns are narrower than the
     * slop, so "nearest column" is the only honest reading of a tap. */
    if (x >= TRAY_X - 1 && x < TRAY_X + TRAY_W + 3) return Bg::OFF;
    if (x >= BAR_X && x < RIGHT_X) return Bg::BAR;
    const bool top = y < BY + BOARD_H / 2;
    int16_t k;
    bool left;
    if (x >= BX - 3 && x < BAR_X) {
        k = static_cast<int16_t>((x - BX) / COL);
        left = true;
    } else if (x >= RIGHT_X && x < BOARD_R) {
        k = static_cast<int16_t>((x - RIGHT_X) / COL);
        left = false;
    } else {
        return Bg::NO_POINT;
    }
    if (k < 0) k = 0;
    if (k > 5) k = 5;
    if (top) return static_cast<uint8_t>(left ? 12 + k : 18 + k);
    return static_cast<uint8_t>(left ? 11 - k : 5 - k);
}

void BackgammonGame::markPlace(uint8_t place) {
    if (place <= Bg::OFF) {
        dirtyPlaces_ |= (1UL << place);
    }
}

// ---- the board -------------------------------------------------------------------

void BackgammonGame::drawChecker(Ui::Renderer& tft, int16_t cx, int16_t cy, uint8_t side) {
    const bool white = side == Bg::WHITE;
    tft.fillCircle(cx, cy, R, white ? whiteFill() : blackFill());
    tft.drawCircle(cx, cy, R, white ? whiteEdge() : blackEdge());
    tft.drawCircle(cx, cy, R - 3, white ? whiteEdge() : blackEdge());
}

void BackgammonGame::drawPoint(Ui::Renderer& tft, uint8_t i) const {
    const Rect r = pointRect(i);
    const bool top = i >= 12;
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
    tft.fillRect(r.x, r.y, r.w, r.h, felt());
    const uint16_t tri = (i & 1) ? triDark() : triLight();
    if (top) {
        tft.fillTriangle(r.x, r.y, r.x + r.w - 1, r.y, cx, r.y + TRI_H, tri);
    } else {
        tft.fillTriangle(r.x, r.y + r.h - 1, r.x + r.w - 1, r.y + r.h - 1, cx,
                         r.y + r.h - 1 - TRI_H, tri);
    }

    const int8_t v = pos_.pt[i];
    const uint8_t count = static_cast<uint8_t>(v < 0 ? -v : v);
    const uint8_t side = v > 0 ? Bg::WHITE : Bg::BLACK;
    const uint8_t shown = count < STACK_SHOWN ? count : STACK_SHOWN;
    auto slotY = [&](uint8_t s) {
        return top ? static_cast<int16_t>(r.y + R + 1 + s * PITCH)
                   : static_cast<int16_t>(r.y + r.h - 2 - R - s * PITCH);
    };
    for (uint8_t s = 0; s < shown; ++s) {
        drawChecker(tft, cx, slotY(s), side);
    }
    if (count > STACK_SHOWN) {
        char n[4];
        snprintf(n, sizeof(n), "%u", static_cast<unsigned>(count));
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(side == Bg::WHITE ? ink() : static_cast<uint16_t>(TFT_WHITE));
        tft.drawString(n, cx, slotY(STACK_SHOWN - 1) + 1, 1);
        tft.setTextDatum(TL_DATUM);
    }
    if (selected_ == i && shown > 0) {
        /* The checker that is picked up: a thick cyan ring, inside its own
         * circle, so putting it down needs nothing erased. */
        const int16_t cy = slotY(static_cast<uint8_t>(shown - 1));
        tft.drawCircle(cx, cy, R, hilite());
        tft.drawCircle(cx, cy, R - 1, hilite());
    }
    if (isTarget(i)) {
        /* Where it would land: a dot on the next slot -- over a blot, which a
         * landing would hit -- and the column outlined. */
        const uint8_t slot = (count == 1 && side == Bg::opponent(toMove_)) ? 0
                             : (shown < STACK_SHOWN ? shown : STACK_SHOWN - 1);
        tft.fillCircle(cx, slotY(slot), 4, hilite());
        tft.drawRect(r.x, r.y, r.w, r.h, hilite());
    }
}

void BackgammonGame::drawBar(Ui::Renderer& tft) const {
    tft.fillRect(BAR_X, BY, BAR_W, BOARD_H, wood());
    const int16_t cx = static_cast<int16_t>(BAR_X + BAR_W / 2);
    const int16_t mid = static_cast<int16_t>(BY + BOARD_H / 2);
    /* White waits above the middle, Black below: each nearer the side of the
     * board it re-enters on. */
    for (uint8_t side = 0; side < 2; ++side) {
        const uint8_t n = pos_.bar[side];
        const uint8_t shown = n < 4 ? n : 4;
        for (uint8_t s = 0; s < shown; ++s) {
            const int16_t cy = side == Bg::WHITE ? static_cast<int16_t>(mid - 10 - s * PITCH)
                                                 : static_cast<int16_t>(mid + 10 + s * PITCH);
            drawChecker(tft, cx, cy, side);
            if (s + 1 == shown && selected_ == Bg::BAR && side == toMove_) {
                tft.drawCircle(cx, cy, R, hilite());
                tft.drawCircle(cx, cy, R - 1, hilite());
            }
        }
        if (n > 4) {
            char t[4];
            snprintf(t, sizeof(t), "%u", static_cast<unsigned>(n));
            const int16_t cy = side == Bg::WHITE ? static_cast<int16_t>(mid - 10 - 3 * PITCH)
                                                 : static_cast<int16_t>(mid + 10 + 3 * PITCH);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(side == Bg::WHITE ? ink() : static_cast<uint16_t>(TFT_WHITE));
            tft.drawString(t, cx, cy + 1, 1);
            tft.setTextDatum(TL_DATUM);
        }
    }
}

void BackgammonGame::drawTray(Ui::Renderer& tft) const {
    tft.fillRect(TRAY_X, BY, TRAY_W, BOARD_H, trayWood());
    /* Borne-off checkers as edge-on slabs: Black's at the top, beside its
     * home; White's at the bottom, beside its. Fifteen fit either half. */
    for (uint8_t n = 0; n < pos_.off[Bg::BLACK]; ++n) {
        tft.fillRect(TRAY_X + 2, BY + 3 + n * 6, TRAY_W - 4, 4, blackFill());
    }
    for (uint8_t n = 0; n < pos_.off[Bg::WHITE]; ++n) {
        tft.fillRect(TRAY_X + 2, BY + BOARD_H - 7 - n * 6, TRAY_W - 4, 4, whiteFill());
    }
    if (isTarget(Bg::OFF)) {
        const int16_t y = toMove_ == Bg::WHITE ? static_cast<int16_t>(BY + BOARD_H / 2) : BY;
        tft.drawRect(TRAY_X, y, TRAY_W, BOARD_H / 2, hilite());
        tft.drawRect(TRAY_X + 1, y + 1, TRAY_W - 2, BOARD_H / 2 - 2, hilite());
    }
}

// ---- the panel -------------------------------------------------------------------

uint8_t BackgammonGame::rollButtonState() const {
    if (phase_ == Phase::DoneReady) return 2;                          // Done
    if (phase_ == Phase::Opening && mode_ != Mode::Remote && mode_ != Mode::Waiting) return 1;
    if (phase_ == Phase::Roll && humanTurn()) return 1;                // Roll
    return 0;                                                          // Roll, greyed
}

bool BackgammonGame::undoEnabled() const {
    return turnCount_ > 0 && humanTurn() &&
           (phase_ == Phase::Moving || phase_ == Phase::DoneReady);
}

void BackgammonGame::drawTurnLine(Ui::Renderer& tft) const {
    tft.fillRect(TURN_RECT.x, TURN_RECT.y, TURN_RECT.w, TURN_RECT.h, Ui::bg());
    const uint8_t side = phase_ == Phase::Over ? winner_ : toMove_;
    tft.fillCircle(TURN_RECT.x + 6, TURN_RECT.y + 8, 6,
                   side == Bg::WHITE ? whiteFill() : blackFill());
    tft.drawCircle(TURN_RECT.x + 6, TURN_RECT.y + 8, 6,
                   side == Bg::WHITE ? whiteEdge() : blackEdge());
    char name[8];
    snprintf(name, sizeof(name), "%.7s", sideName(side));   // seven characters fit
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(name, TURN_RECT.x + 16, TURN_RECT.y, 2);
}

void BackgammonGame::drawDice(Ui::Renderer& tft) const {
    tft.fillRect(DICE_RECT.x, DICE_RECT.y, DICE_RECT.w, DICE_RECT.h, Ui::bg());
    if (rolled_.count == 0) {
        return;
    }
    const bool doubles = rolled_.count == 4;
    for (uint8_t k = 0; k < 2; ++k) {
        const uint8_t face = rolled_.value[doubles ? 0 : k];
        /* A die is used once its value is no longer among the dice to play;
         * on doubles, the second die greys when two of the four are gone. */
        bool live = false;
        if (doubles) {
            live = dice_.count > (k == 0 ? 0 : 2);
        } else {
            for (uint8_t i = 0; i < dice_.count; ++i) live = live || dice_.value[i] == face;
        }
        const int16_t x = static_cast<int16_t>(DICE_RECT.x + 2 + k * (DIE + 8));
        tft.fillRoundRect(x, DICE_Y, DIE, DIE, 5, live ? dieFace() : dieUsed());
        tft.drawRoundRect(x, DICE_Y, DIE, DIE, 5, ink());
        const int16_t cx = static_cast<int16_t>(x + DIE / 2);
        const int16_t cy = static_cast<int16_t>(DICE_Y + DIE / 2);
        constexpr int16_t D = 8;
        const bool centre = face & 1;
        if (centre) tft.fillCircle(cx, cy, 3, ink());
        if (face >= 2) {
            tft.fillCircle(cx - D, cy - D, 3, ink());
            tft.fillCircle(cx + D, cy + D, 3, ink());
        }
        if (face >= 4) {
            tft.fillCircle(cx + D, cy - D, 3, ink());
            tft.fillCircle(cx - D, cy + D, 3, ink());
        }
        if (face == 6) {
            tft.fillCircle(cx - D, cy, 3, ink());
            tft.fillCircle(cx + D, cy, 3, ink());
        }
    }
    if (doubles) {
        char line[20];
        snprintf(line, sizeof(line), "double, %u left", static_cast<unsigned>(dice_.count));
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString(line, DICE_RECT.x + 2, DICE_Y + DIE + 3, 1);
    }
}

void BackgammonGame::drawNews(Ui::Renderer& tft) const {
    tft.fillRect(NEWS_RECT.x, NEWS_RECT.y, NEWS_RECT.w, NEWS_RECT.h, Ui::bg());
    /* The pip count: how far each side still has to go. Watching it fall is
     * most of what a child learns about racing. */
    char pips[20];
    snprintf(pips, sizeof(pips), "W%u B%u", static_cast<unsigned>(Bg::pipCount(pos_, Bg::WHITE)),
             static_cast<unsigned>(Bg::pipCount(pos_, Bg::BLACK)));
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(pips, NEWS_RECT.x + 2, NEWS_RECT.y, 1);
    Ui::drawWrappedText(tft, message_,
                        Rect{NEWS_RECT.x, static_cast<int16_t>(NEWS_RECT.y + 12), NEWS_RECT.w, 38},
                        Ui::text(), 2, Align::Left);
}

void BackgammonGame::drawButtons(Ui::Renderer& tft, bool sure) const {
    for (const Rect& r : {ROLL_RECT, UNDO_RECT, ACTION_RECT}) {
        tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
    }
    const uint8_t roll = rollButtonState();
    if (roll == 2) {
        Ui::drawButton(tft, ROLL_RECT, "Done", Ui::success(), Ui::outline(), TFT_BLACK);
    } else {
        Ui::drawPagerButton(tft, ROLL_RECT, "Roll", roll == 1);
    }
    Ui::drawPagerButton(tft, UNDO_RECT, "Undo", undoEnabled());
    if (phase_ == Phase::Over) {
        Ui::drawButton(tft, ACTION_RECT, "New game", Ui::success(), Ui::outline(), TFT_BLACK);
    } else if (sure) {
        Ui::drawButton(tft, ACTION_RECT, "Sure?", Ui::warning(), Ui::outline(), TFT_BLACK);
    } else {
        Ui::drawButton(tft, ACTION_RECT, "End", Ui::panel(), Ui::outline(), Ui::text());
    }
}

void BackgammonGame::drawPanel(Ui::Renderer& tft) {
    const uint16_t turn = static_cast<uint16_t>(
        toMove_ | (phase_ == Phase::Over ? 2 : 0) | (winner_ << 2) |
        (static_cast<uint8_t>(mode_) << 3));
    if (turn != drawnTurn_) {
        drawTurnLine(tft);
        drawnTurn_ = turn;
    }
    uint32_t dice = 2166136261U;
    for (uint8_t i = 0; i < Bg::MAX_DICE; ++i) {
        dice = fnv(fnv(dice, rolled_.value[i]), dice_.value[i]);
    }
    dice = fnv(fnv(dice, rolled_.count), dice_.count);
    if (dice != drawnDice_) {
        drawDice(tft);
        drawnDice_ = dice;
    }
    uint32_t news = fnv(fnv(2166136261U, static_cast<uint8_t>(Bg::pipCount(pos_, Bg::WHITE))),
                        static_cast<uint8_t>(Bg::pipCount(pos_, Bg::BLACK)));
    for (const char* c = message_; *c != 0; ++c) news = fnv(news, static_cast<uint8_t>(*c));
    if (news != drawnNews_) {
        drawNews(tft);
        drawnNews_ = news;
    }
    const bool sure = phase_ != Phase::Over && millis() < confirmUntilMs_;
    const uint16_t buttons = static_cast<uint16_t>(rollButtonState() | (undoEnabled() ? 4 : 0) |
                                                   (sure ? 8 : 0) | (phase_ == Phase::Over ? 16 : 0));
    if (buttons != drawnButtons_) {
        drawButtons(tft, sure);
        drawnButtons_ = buttons;
        confirmShown_ = sure;
    }
}

// ---- the lobby -----------------------------------------------------------------------

void BackgammonGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    tft.fillRect(0, TOP_BAR_HEIGHT, GAME_CANVAS_WIDTH, GAME_CANVAS_HEIGHT - TOP_BAR_HEIGHT, Ui::bg());
    Ui::drawButton(tft, lobbyRowRect(0), "Two players", Ui::panel(), Ui::outline(), Ui::text());
    Ui::drawButton(tft, lobbyRowRect(1), "Play the computer", Ui::panel(), Ui::outline(), Ui::text());
    char label[32];
    for (uint8_t i = 0; i < seatCount_ && i < 3; ++i) {
        const char* who = seats_[i].name[0] != 0 ? seats_[i].name : seats_[i].deviceId;
        if (seats_[i].inviting) {
            snprintf(label, sizeof(label), "%s invites you", who);
        } else {
            snprintf(label, sizeof(label), "Play %s nearby", who);
        }
        Ui::drawButton(tft, lobbyRowRect(static_cast<uint8_t>(i + 2)), label,
                       seats_[i].inviting ? Ui::success() : Ui::surface(), Ui::outline(),
                       seats_[i].inviting ? static_cast<uint16_t>(TFT_BLACK) : Ui::text());
    }
    /* Why a game just vanished, else what nearby play is and who can switch
     * it on -- an adult, since the radio is admin-only; playing is not. */
    /* A low battery outranks the usual note, below a note about a game that
     * just ended: a console that dies mid-game cannot tell anyone. */
    const bool low = lobbyNote_[0] == 0 && seatCount_ > 0 && host.batteryLow();
    const char* note = lobbyNote_[0] != 0 ? lobbyNote_
                       : low              ? "Battery low: a nearby game may not finish."
                       : seatCount_ > 0   ? "Moves travel by Bluetooth. Anyone near hears them."
                                          : "Nearby play: an adult can switch Beacon and Nearby on.";
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(lobbyNote_[0] != 0 || low ? Ui::warning() : Ui::muted(), Ui::bg());
    tft.drawString(note, GAME_CANVAS_WIDTH / 2, GAME_CANVAS_HEIGHT - 4, lobbyNote_[0] != 0 ? 2 : 1);
    tft.setTextDatum(TL_DATUM);
}

// ---- the render passes ------------------------------------------------------------------

void BackgammonGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    if (mode_ == Mode::Lobby) {
        lobbyStale_ = true;
        return;
    }
    tft.fillRect(BX - 2, BY - 2, BOARD_R - BX + 4, BOARD_H + 4, wood());
    for (uint8_t i = 0; i < Bg::POINTS; ++i) {
        drawPoint(tft, i);
    }
    drawBar(tft);
    drawTray(tft);
    dirtyPlaces_ = 0;
    drawnTurn_ = 0xFFFF;
    drawnDice_ = 0xFFFFFFFFU;
    drawnNews_ = 0xFFFFFFFFU;
    drawnButtons_ = 0xFFFF;
    pausePainted_ = false;   // the board was just repainted under it
    drawPause(tft);
}

void BackgammonGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    if (mode_ == Mode::Lobby) {
        if (lobbyStale_) {
            renderLobby(host);
            lobbyStale_ = false;
        }
        return;
    }
    if (dirtyPlaces_ != 0) {
        pausePainted_ = false;   // a place under the card
        for (uint8_t i = 0; i < Bg::POINTS; ++i) {
            if (dirtyPlaces_ & (1UL << i)) drawPoint(tft, i);
        }
        if (dirtyPlaces_ & (1UL << Bg::BAR)) drawBar(tft);
        if (dirtyPlaces_ & (1UL << Bg::OFF)) drawTray(tft);
        dirtyPlaces_ = 0;
    }
    drawPanel(tft);
    drawPause(tft);
}

Rect BackgammonGame::boardArea() {
    return Rect{static_cast<int16_t>(BX - 2), static_cast<int16_t>(BY - 2),
                static_cast<int16_t>(BOARD_R - BX + 4), static_cast<int16_t>(BOARD_H + 4)};
}
