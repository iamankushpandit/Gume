#pragma once

#include "engine/Game.h"
#include "games/BackgammonRules.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& backgammonAppMetadata();

/* Backgammon for two: passing one console, against the computer, or against
 * another console in the room.
 *
 * The rules and the computer player are in BackgammonRules / BackgammonAi and
 * nothing here decides what is legal. This file is the table: whose turn it
 * is, the dice, the tap-a-checker-then-a-point input, undo within a turn, and
 * the nearby session.
 *
 * Fixed 320x240 landscape canvas, no followsLayout -- a backgammon board is
 * landscape by nature -- with the board on the left and a narrow panel on the
 * right holding the dice, the pip counts, one line of news and the buttons.
 * The board never flips: White (bottom, home bottom right) is the person in
 * a game against the computer, and in a nearby game each console's owner is
 * whichever colour the invitation's coin toss gave them.
 *
 * A turn is: Roll, then tap a checker (or the bar) and tap one of the points
 * that light up, as many times as the dice allow, then Done. Done is not a
 * formality: it is what makes Undo usable for the last move, it is the moment
 * to pass the device, and in a nearby game it is when the turn goes on the air.
 *
 * Remembered like Chess, and End game asks twice.
 *
 * No score. A win -- even a Backgammon -- is not a number.
 */
class BackgammonGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /** Saves, and hands the radio back if a nearby game was on it. */
    void end(AppContext& host) override;

private:
    /*   Lobby      choosing how to play
     *   Local      two people, one console
     *   Computer   the person is White, the computer Black
     *   Waiting    we invited a console and it has not answered yet
     *   Remote     a game against another console */
    enum class Mode : uint8_t { Lobby, Local, Computer, Waiting, Remote };

    /*   Opening    before the opening roll: "Tap Roll to start"
     *   Roll       a person here must roll
     *   Moving     a person here is moving checkers
     *   DoneReady  nothing left to play: Done hands the turn over
     *   Computer   the computer is thinking, then playing its moves one by one
     *   Remote     the other console's turn: its moves arrive over the air
     *   Over       somebody has borne off all fifteen */
    enum class Phase : uint8_t { Opening, Roll, Moving, DoneReady, Computer, Remote, Over };

    // ---- flow (BackgammonGame.cpp) -----------------------------------------
    void newGame(Mode mode);
    void doOpening(AppContext& host);
    /** Hand the turn to `side` and set up what its turn needs. */
    void startTurn(AppContext& host, uint8_t side);
    void doRoll(AppContext& host);
    /** Recompute the legal moves and what the screen says about them. */
    void refreshLegal();
    void playMove(AppContext& host, const Bg::Move& m, bool byHuman);
    void pressDone(AppContext& host);
    void pressUndo(AppContext& host);
    void pressAction(AppContext& host, uint32_t now);
    void finishGame(AppContext& host);
    void updateBoard(AppContext& host, const TouchPoint& touch, uint32_t now);
    void updateComputer(AppContext& host, uint32_t now);
    void tapBoard(AppContext& host, int16_t x, int16_t y);
    void select(uint8_t from);
    /** Is it a person on this console who moves now? */
    bool humanTurn() const;
    const char* sideName(uint8_t side) const;
    void setMessage(const char* text);

    // ---- nearby (BackgammonNet.cpp) ------------------------------------------
    void updateLobby(AppContext& host, const TouchPoint& touch);
    void startRemote(AppContext& host, const NearbySeat& seat, uint8_t session, bool weAreWhite,
                     bool waiting);
    /* Every frame of a nearby game: our word on the air, the other console's
     * moves off it in order, its ending, and our outbox drained one move per
     * ply as fast as it acknowledges them. */
    void pollRemote(AppContext& host, uint32_t now);
    /** Leave a nearby game for the lobby, saying why when it was the other side. */
    void leaveRemote(AppContext& host, const char* note);

    // ---- save (BackgammonSave.cpp) --------------------------------------------
    void saveGame(AppContext& host) const;
    bool restoreGame(AppContext& host);

    // ---- geometry and drawing (BackgammonDraw.cpp) -----------------------------
    static Rect pointRect(uint8_t index);
    static Rect barRect();
    static Rect trayRect();
    static Rect lobbyRowRect(uint8_t row);
    static Rect rollRect();
    static Rect undoRect();
    static Rect actionRect();
    /** The point, BAR or OFF a tap means, or NO_POINT. */
    static uint8_t placeAt(int16_t x, int16_t y);
    void drawPoint(Ui::Renderer& tft, uint8_t index) const;
    void drawBar(Ui::Renderer& tft) const;
    void drawTray(Ui::Renderer& tft) const;
    /* The panel in four parts -- whose turn, the dice, the pip counts and
     * message, the buttons -- each repainted only when what it shows changed.
     * A move changes the dice and the pips; it must not blink the buttons. */
    void drawPanel(Ui::Renderer& tft);
    void drawTurnLine(Ui::Renderer& tft) const;
    void drawDice(Ui::Renderer& tft) const;
    void drawNews(Ui::Renderer& tft) const;
    void drawButtons(Ui::Renderer& tft, bool sure) const;
    uint8_t rollButtonState() const;
    bool undoEnabled() const;
    void renderLobby(AppContext& host);
    static void drawChecker(Ui::Renderer& tft, int16_t cx, int16_t cy, uint8_t side);
    /* Mark a board place for repaint: a point 0..23, BAR or OFF. Moves repaint
     * the two or three places they touch; nothing else on the board redraws. */
    void markPlace(uint8_t place);
    bool isTarget(uint8_t place) const;

    // ---- the game ----------------------------------------------------------------
    Mode mode_ = Mode::Lobby;
    Phase phase_ = Phase::Opening;
    Bg::Position pos_{};
    uint8_t toMove_ = Bg::WHITE;
    Bg::Dice dice_{};            // still to play
    Bg::Dice rolled_{};          // as rolled, for the dice drawn in the panel
    uint16_t rolls_ = 0;
    uint32_t seed_ = 0;
    Bg::Result result_ = Bg::Result::None;
    uint8_t winner_ = Bg::WHITE;

    Bg::Move legal_[Bg::MAX_MOVES] = {};
    uint8_t legalCount_ = 0;
    uint8_t selected_ = Bg::NO_POINT;

    /* This turn's moves, to take back one at a time and, in a nearby game, to
     * put on the air when Done is pressed. Four is a doubles turn. */
    Bg::Position undoPos_[Bg::MAX_DICE] = {};
    Bg::Dice undoDice_[Bg::MAX_DICE] = {};
    Bg::Move turnMoves_[Bg::MAX_DICE] = {};
    uint8_t turnCount_ = 0;

    Bg::Plan plan_{};
    uint8_t planIndex_ = 0;
    bool planned_ = false;
    uint32_t timerMs_ = 0;

    char message_[24] = {0};
    uint32_t confirmUntilMs_ = 0;
    char lobbyNote_[32] = {0};

    // ---- a nearby game ------------------------------------------------------------
    bool ourWhite_ = true;
    char opponent_[5] = {0};
    char opponentName_[11] = {0};
    uint8_t session_ = 0;
    /* The last ply this console has applied (its own and the other's), and the
     * turn it is advertising. See the protocol note in BackgammonNet.cpp. */
    uint8_t applied_ = 0;
    uint8_t myPly_ = 0;
    uint8_t myFrom_ = 0;
    uint8_t myTo_ = 0;
    Bg::Move outbox_[Bg::MAX_DICE] = {};
    uint8_t outboxCount_ = 0;
    bool ended_ = false;
    NearbySeat seats_[6] = {};
    uint8_t seatCount_ = 0;
    uint32_t seatsAtMs_ = 0;

    // ---- what is on the panel -------------------------------------------------------
    uint32_t dirtyPlaces_ = 0;   // bit per point, then BAR, then OFF
    bool panelStale_ = true;
    uint16_t drawnTurn_ = 0xFFFF;
    uint32_t drawnDice_ = 0xFFFFFFFFU;
    uint32_t drawnNews_ = 0xFFFFFFFFU;
    uint16_t drawnButtons_ = 0xFFFF;
    bool confirmShown_ = false;
    bool lobbyStale_ = true;

    struct Saved {
        uint16_t magic;
        uint8_t version;
        uint8_t mode;
        uint8_t phase;
        uint8_t toMove;
        uint8_t winner;
        uint8_t result;
        Bg::Position pos;
        Bg::Dice dice;
        Bg::Dice rolled;
        uint16_t rolls;
        uint32_t seed;
        uint8_t turnCount;
        Bg::Position undoPos[Bg::MAX_DICE];
        Bg::Dice undoDice[Bg::MAX_DICE];
        Bg::Move turnMoves[Bg::MAX_DICE];
        uint8_t ourWhite;
        char opponent[5];
        char opponentName[11];
        uint8_t session;
        uint8_t applied;
        uint8_t myPly;
        uint8_t myFrom;
        uint8_t myTo;
        uint8_t outboxCount;
        Bg::Move outbox[Bg::MAX_DICE];
    };
    static constexpr uint16_t SAVE_MAGIC = 0xBA6A;
    static constexpr uint8_t SAVE_VERSION = 1;
};
