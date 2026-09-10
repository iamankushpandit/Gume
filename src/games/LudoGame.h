#pragma once

#include "engine/Game.h"
#include "games/LudoRules.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& ludoAppMetadata();

/* Ludo for two to four, any mix of players and computers, on one console.
 *
 * The rules live in LudoRules and nothing here decides what is legal: this
 * file is the table the game is played on -- who sits where, the die, the
 * hopping token, and whose turn it is. See LudoRules.h for the rules as played.
 *
 * Seats are Player, Computer or Empty, chosen in a lobby before the game.
 * "At least two" means two seats in play, and at least one of them has to be a
 * person: four computers playing each other is a screensaver, not a game. The
 * default is one player against one computer at Easy, because the owner who
 * opens this for the first time is most often a child on their own, and Nearby
 * play -- which is where the other players would come from -- ships switched
 * off.
 *
 * Two players sit in opposite corners, Red against Yellow, as on a real board:
 * adjacent corners would put one player's start square six moves behind the
 * other's for the whole game.
 *
 * Fixed 320x240 landscape canvas, no followsLayout. The board is the whole
 * height and the panel beside it carries the die, whose turn it is and the
 * seats; the runtime upscales it on a bigger panel. A cell is 13px, which is
 * too small to aim a finger at on a resistive panel -- so a tap picks the
 * nearest token that can move rather than the one under the finger, and when
 * only one move is possible it plays itself.
 *
 * Each colour also has its own token shape (circle, square, diamond,
 * triangle). A game built from four colours is otherwise unplayable for the
 * roughly one boy in twelve who cannot tell red from green.
 *
 * Remembered, like Chess: saved after every roll and every move, so a game put
 * down comes back where it was, and End game is the way to abandon one.
 *
 * No score. A win is not a number.
 */
class LudoGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /** Saves the game on the way out, so nothing depends on the last move. */
    void end(AppContext& host) override;

    enum class SeatKind : uint8_t { Empty = 0, Player = 1, Computer = 2 };

private:
    enum class Mode : uint8_t { Lobby, Play };

    /* Where a turn is.
     *
     *   Roll     the seat to move has to roll: a player taps, a computer waits
     *            a moment first so a child can see whose turn it became
     *   Choose   a roll is held and at least one token can use it
     *   Moving   a token is hopping square by square
     *   Notice   the roll could not be used -- no move, or a third six -- and
     *            the screen is saying so before the turn moves on
     *   Over     every place is decided */
    enum class Phase : uint8_t { Roll, Choose, Moving, Notice, Over };

    // ---- game flow (LudoGame.cpp) ----------------------------------------
    void startGame(AppContext& host);
    /* Pick up wherever the rules state says the game is. Used after every
     * move and skip, and on restore -- the phase is derived, never saved, so
     * a restored game cannot disagree with its own position. `bonus` only
     * changes what the message says: the rules already decided whose turn. */
    void enterTurn(uint32_t now, bool bonus);
    void doRoll(AppContext& host, uint32_t now);
    void startMove(AppContext& host, uint8_t token, uint32_t now);
    void stepMove(AppContext& host, uint32_t now);
    void finishMove(AppContext& host, uint32_t now);
    void updatePlay(AppContext& host, const TouchPoint& touch, uint32_t now);
    void updateLobby(AppContext& host, const TouchPoint& touch);
    void pressAction(AppContext& host, uint32_t now);
    /* The token a player's tap means: the nearest one that can move, within
     * reach of the finger. NO_TOKEN when the tap was nowhere near one. */
    uint8_t tokenAt(int16_t x, int16_t y) const;
    /* The single move a player would make anyway -- one movable token, or
     * several standing on one square, which are the same move. */
    uint8_t onlyChoice() const;
    bool isComputer(uint8_t seat) const {
        return kind_[seat] == SeatKind::Computer;
    }
    bool canStart() const;
    /* A delay, or a quarter of it once every person at the table has
     * finished. The rules still play the computers out for the remaining
     * places, but a child who came first should not have to sit through two
     * computers doing it at a pace chosen for somebody watching to learn. */
    uint32_t pace(uint32_t ms) const;
    void setMessage(const char* text);
    void saveGame(AppContext& host) const;
    bool restoreGame(AppContext& host);

    // ---- geometry and drawing (LudoBoard.cpp; the lobby in LudoLobby.cpp) --
    /* The board is the classic 15x15 cross: four 6x6 yards in the corners,
     * three-wide arms carrying the 52-square track and the four home columns,
     * and a 3x3 centre. At 13px a cell it is 195px square, which is the
     * 210px under the top bar less a margin -- and leaves 105px beside it for
     * the panel. */
    static constexpr int16_t GRID = 15;
    static constexpr int16_t CELL = 13;
    static constexpr int16_t BOARD_X = 6;
    static constexpr int16_t BOARD_Y = 37;
    static constexpr int16_t BOARD_PX = GRID * CELL;
    static Rect boardRect() { return Rect{BOARD_X, BOARD_Y, BOARD_PX, BOARD_PX}; }
    static const char* seatName(uint8_t seat);
    static uint16_t seatColour(uint8_t seat);
    /** Text drawn on a seat's colour: white, except ink on yellow. */
    static uint16_t seatText(uint8_t seat);
    /** The board's paper, behind the track squares and a chip's token. */
    static uint16_t paperColour();

    struct Cell { int8_t col; int8_t row; };
    static Cell trackCell(uint8_t abs);
    static Cell homeCell(uint8_t seat, uint8_t step);
    static Rect cellRect(Cell c);
    static Rect yardRect(uint8_t seat);
    static void yardSpot(uint8_t seat, uint8_t token, int16_t& x, int16_t& y);
    /** Where a seat's home count sits, inside its triangle in the centre. */
    static void homeSpot(uint8_t seat, int16_t& x, int16_t& y);
    /* Every token is somewhere that can be repainted on its own: a grid cell,
     * one of sixteen yard spots, or the centre. Numbering them lets the
     * renderer keep one dirty bit per place and repaint only those. */
    static uint8_t placeOf(uint8_t seat, uint8_t token, uint8_t rel);
    void tokenCentre(uint8_t seat, uint8_t token, int16_t& x, int16_t& y) const;

    void drawBoard(Ui::Renderer& tft) const;
    void drawPlace(Ui::Renderer& tft, uint8_t place) const;
    void drawCellAt(Ui::Renderer& tft, Cell c) const;
    void drawCentre(Ui::Renderer& tft) const;
    void drawYardSpot(Ui::Renderer& tft, uint8_t seat, uint8_t token) const;
    static void drawToken(Ui::Renderer& tft, int16_t cx, int16_t cy, uint8_t seat,
                          int16_t radius, uint8_t count);
    void drawDie(Ui::Renderer& tft) const;
    void drawTurn(Ui::Renderer& tft) const;
    void drawMessage(Ui::Renderer& tft) const;
    void drawSeats(Ui::Renderer& tft) const;
    /* `sure` is End game's second-press state, decided by the caller from the
     * clock so that this stays a pure drawing of what it is told. */
    void drawAction(Ui::Renderer& tft, bool sure) const;
    void renderLobby(AppContext& host);
    void drawSeatChip(Ui::Renderer& tft, uint8_t seat) const;
    void drawLevel(Ui::Renderer& tft) const;
    void drawLobbyHint(Ui::Renderer& tft) const;

    static Rect dieRect();
    static Rect actionRect();
    static Rect seatChipRect(uint8_t seat);
    static Rect levelRect(uint8_t level);
    static Rect startRect();

    /** Set the dirty bit for one place. */
    void markPlace(uint8_t place);
    /** Compare what is shown with what was drawn and mark what changed. */
    void diffPlaces();
    /* Tokens a player may tap right now, as bit (seat * 4 + token). Only a
     * player's own choice is lit: lighting a computer's options would show a
     * child a decision that is not theirs to make. */
    uint16_t highlights() const;

    // ---- state ------------------------------------------------------------
    Ludo::State state_{};
    uint32_t seed_ = 0;
    SeatKind kind_[Ludo::SEATS] = {SeatKind::Player, SeatKind::Empty,
                                   SeatKind::Computer, SeatKind::Empty};
    Ludo::Level level_ = Ludo::Level::Easy;
    Mode mode_ = Mode::Lobby;
    Phase phase_ = Phase::Roll;
    uint32_t timerMs_ = 0;

    /* Where each token is DRAWN, which differs from the rules state only
     * while a token is hopping: the rules apply a move at once, the screen
     * catches up a square at a time. */
    uint8_t shown_[Ludo::SEATS][Ludo::TOKENS] = {};
    Ludo::MoveInfo anim_{};
    uint8_t animAt_ = Ludo::YARD;
    /* The token to move when the choice makes itself, or NO_TOKEN. */
    uint8_t autoToken_ = Ludo::NO_TOKEN;
    /* The face on the die: the roll just made, 0 for a blank die. */
    uint8_t face_ = 0;
    char message_[20] = {0};
    uint32_t confirmUntilMs_ = 0;

    // ---- what is on the panel, so only changes are repainted ---------------
    static constexpr uint8_t PLACE_CENTRE = 241;
    static constexpr uint8_t PLACE_COUNT = 242;
    uint8_t drawnPlace_[Ludo::SEATS][Ludo::TOKENS] = {};
    /* The highlight set that the places being drawn should show. Worked out
     * once per render rather than once per cell: it costs a legality check
     * per token, and a full board is 72 cells. */
    uint16_t hi_ = 0;
    uint16_t drawnHi_ = 0;
    uint8_t dirty_[(PLACE_COUNT + 7) / 8] = {};
    bool anyDirty_ = false;
    uint8_t drawnFace_ = 0xFF;
    uint8_t drawnTurn_ = 0xFF;
    char drawnMessage_[20] = {0};
    bool seatsStale_ = true;
    bool actionStale_ = true;
    bool confirmShown_ = false;
    bool lobbyStale_ = true;

    /* The whole game, flat, for NVS. Fixed layout: loadBlob() refuses a blob
     * whose length changed, and `version` covers one that kept its size.
     * Seat kinds and level are kept with no game in progress too, so the
     * lobby comes back as it was left. */
    struct Saved {
        uint16_t magic;
        uint8_t version;
        uint8_t inGame;
        uint8_t kind[Ludo::SEATS];
        uint8_t level;
        uint32_t seed;
        Ludo::State state;
    };
    static constexpr uint16_t SAVE_MAGIC = 0x1D05;
    static constexpr uint8_t SAVE_VERSION = 1;
};
