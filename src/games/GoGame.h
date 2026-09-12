#pragma once

#include "engine/Game.h"
#include "games/GoRules.h"
#include "games/NearbyWatch.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& goAppMetadata();

/* Go: passing one console, against the computer, or against another console
 * in the room.
 *
 * The rules and the computer player are in GoRules / GoAi and nothing here
 * decides what is legal. This file is the board: whose turn it is, a finger
 * choosing a point, the pause while the computer thinks, the marking phase
 * under Territory rules, the score, and the nearby session.
 *
 * Fixed 320x240 landscape canvas, no followsLayout: the board on the left
 * takes the whole height and a panel beside it carries the turn, the
 * captures, one line of news and the buttons. 9x9 everywhere; 19x19 only on
 * a panel wide enough that a point is something a finger can aim at
 * (BIG_BOARD_AVAILABLE, from the physical width -- a hardware fact, not a
 * layout one).
 *
 * A STONE IS PLACED IN TWO TAPS. The first puts a ghost on the nearest
 * point and the panel says what it would do; the second, on the same point
 * (or Place, on 19x19, where the panel also shows the five-by-five around
 * the ghost magnified), plays it. Not a nicety: a misplaced stone in Go is
 * for ever, a resistive panel misplaces, and on 19x19 the points are closer
 * together than a fingertip. The ghost follows a finger that is held down.
 *
 * REPAINT IS PER POINT. A move changes the point it went on, the points it
 * captured and where the last-move marker was and is: two to four
 * intersections out of eighty-one or three hundred and sixty-one. Each has
 * a dirty bit and repaintPoint() paints its whole cell idempotently -- wood,
 * the grid through it, a star, the stone, the marker, the ghost. The panel is
 * parts, each repainted when what it shows changed. markFullDirty() is for
 * entering the screen, a new game, undo, the marking phase and the result
 * card, and nothing else.
 *
 * Remembered, like Chess: saved after every move and on the way out, packed
 * to two bits a point so nineteen-by-nineteen is a hundred bytes. End game
 * asks twice. Undo takes back your last move and, against the computer, its
 * reply; never in a nearby game, which would need protocol this has not got.
 *
 * Score: wins against the computer, per profile. */
class GoGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /** Saves, and hands the radio back if a nearby game was on it. */
    void end(AppContext& host) override;

    /** 19x19 is offered only where a point is wide enough to aim at. */
    static constexpr bool BIG_BOARD_AVAILABLE = SCREEN_WIDTH >= 480;

private:
    /*   Lobby      choosing how to play, and the three chips
     *   Local      two people, one console
     *   Computer   the person is Black, the computer White
     *   Waiting    we invited a console and it has not answered yet
     *   Remote     a game against another console */
    enum class Mode : uint8_t { Lobby, Local, Computer, Waiting, Remote };

    /*   Play       somebody here, or the air, has to move
     *   Thinking   the computer is choosing, a few playouts per frame
     *   Marking    two passes under Territory: which groups are dead
     *   Over       scored, the card is up */
    enum class Phase : uint8_t { Play, Thinking, Marking, Over };

    // ---- flow (GoGame.cpp) ---------------------------------------------------
    void newGame(Mode mode);
    /** Apply a move for whoever is to move: the board, the sounds, the save. */
    void applyMove(AppContext& host, uint16_t p, bool byHuman);
    void tapBoard(AppContext& host, const TouchPoint& touch);
    void pressPlace(AppContext& host);
    void pressPass(AppContext& host);
    void pressUndo(AppContext& host);
    void pressAction(AppContext& host, uint32_t now);
    void updateComputer(AppContext& host, uint32_t now);
    void updateMarking(AppContext& host, const TouchPoint& touch, uint32_t now);
    void enterMarking(AppContext& host);
    void resumeFromMarking(AppContext& host);
    void finishGame(AppContext& host);
    /** Is it a person on this console who moves now? */
    bool humanTurn() const;
    void setGhost(uint16_t p);
    void setMessage(const char* text);
    const char* sideName(uint8_t colour) const;
    /** Every point that differs between `before` and state_, marked dirty. */
    void markChanged(const Go::State& before);
    void markPoint(uint16_t p);

    // ---- nearby (GoNet.cpp) ---------------------------------------------------
    void updateLobby(AppContext& host, const TouchPoint& touch);
    void startRemote(AppContext& host, const NearbySeat& seat, uint8_t session, bool weAreBlack,
                     bool waiting);
    void pollRemote(AppContext& host, uint32_t now);
    void publishOurs(AppContext& host);
    void leaveRemote(AppContext& host, const char* note);
    void endRemoteByUs(AppContext& host);
    bool updatePause(AppContext& host, const TouchPoint& touch, uint32_t now);
    void drawPause(Ui::Renderer& tft);

    // ---- save (GoSave.cpp) ----------------------------------------------------
    void saveGame(AppContext& host) const;
    bool restoreGame(AppContext& host);

    // ---- geometry and drawing (GoDraw.cpp) ------------------------------------
    bool big() const { return state_.n > 9; }
    int16_t step() const;
    int16_t margin() const;
    Rect boardRect() const;
    /** The cell around a point: what repaintPoint() clears. */
    Rect pointRect(uint16_t p) const;
    int16_t pointX(uint16_t p) const;
    int16_t pointY(uint16_t p) const;
    /** The nearest point to a touch on the board, or NO_POINT off it. */
    uint16_t pointAt(int16_t x, int16_t y) const;
    bool isStar(uint16_t p) const;
    int16_t panelX() const;
    int16_t panelW() const;
    Rect turnRect() const;
    Rect capturesRect() const;
    Rect infoRect() const;
    Rect placeRect() const;
    Rect passRect() const;
    Rect undoRect() const;
    Rect actionRect() const;
    static Rect lobbyChipRect(uint8_t index, uint8_t count);
    static Rect lobbyRowRect(uint8_t row);
    Rect resultRect() const;
    Rect resultButtonRect(uint8_t index) const;

    void drawBoard(Ui::Renderer& tft) const;
    void repaintPoint(Ui::Renderer& tft, uint16_t p) const;
    void drawStone(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t r, uint8_t colour,
                   bool faded) const;
    void drawPanel(Ui::Renderer& tft);
    void drawTurn(Ui::Renderer& tft) const;
    void drawCaptures(Ui::Renderer& tft) const;
    void drawInfo(Ui::Renderer& tft) const;
    void drawMagnifier(Ui::Renderer& tft) const;
    void drawButtons(Ui::Renderer& tft, bool sure) const;
    void drawResult(Ui::Renderer& tft) const;
    void renderLobby(AppContext& host);
    /** "E5": the column letter (no I, as on a real board) and the row from the bottom. */
    void coordinate(uint16_t p, char* out, size_t len) const;

    // ---- the game ---------------------------------------------------------------
    Mode mode_ = Mode::Lobby;
    Phase phase_ = Phase::Play;
    Go::State state_{};
    /* Before the person's last move, so Undo can take it back -- and against
     * the computer, the reply with it. Not saved: a game put down loses its
     * undo, which is the lesser thing to lose. */
    Go::State undo_{};
    bool haveUndo_ = false;
    Go::Search search_{};
    Go::Rng rng_{};
    uint32_t timerMs_ = 0;
    uint16_t ghost_ = Go::NO_POINT;
    uint8_t humanColour_ = Go::BLACK;   // in a Computer or Remote game

    /* The lobby's three chips, kept between games and saved. */
    uint8_t boardSize_ = 9;
    Go::Rules rules_ = Go::Rules::Capture1;
    Go::Level level_ = Go::Level::Easy;

    /* The marking phase. dead_ is one byte per point; own_ is the ownership
     * it implies, repainted as territory.
     *
     * agreed_ is who has accepted the marking, as ONE bit per colour in every
     * mode. It was two encodings for a while -- a count locally and a bit mask
     * across consoles -- which was self-consistent in each and exactly the
     * shape a later bug takes, so both now go through agreedBit(). Locally the
     * console is passed between two people and nothing can tell which of them
     * is pressing, so the first press is taken as the player to move and the
     * second as the other: the same two bits rather than a second way of
     * counting to two. Against the computer neither bit is set, because the
     * person's word is final and finishGame() is called at once. */
    static constexpr uint8_t agreedBit(uint8_t colour) {
        return static_cast<uint8_t>(1U << colour);
    }
    /* A function, not a `static constexpr` value: a static data member's
     * initialiser is evaluated while the class is still incomplete, so it
     * cannot call agreedBit() above it. A constexpr member function can,
     * because bodies are compiled once the class is closed -- and that keeps
     * the shift written down exactly once. */
    static constexpr uint8_t agreedBoth() {
        return static_cast<uint8_t>(agreedBit(Go::BLACK) | agreedBit(Go::WHITE));
    }
    uint8_t dead_[Go::MAX_POINTS] = {};
    uint8_t own_[Go::MAX_POINTS] = {};
    uint8_t agreed_ = 0;
    /* The computer's opinion of the dead stones is playouts, a few per frame
     * like a move: this is how many are still to run. */
    uint16_t deadPlayoutsLeft_ = 0;
    uint16_t alive_[Go::MAX_POINTS] = {};
    Go::Score score_{};

    char message_[26] = {0};
    uint32_t confirmUntilMs_ = 0;

    // ---- a nearby game ------------------------------------------------------------
    char opponent_[5] = {0};
    char opponentName_[11] = {0};
    uint8_t session_ = 0;
    uint8_t ourPly_ = 0;
    uint8_t theirPly_ = 0;
    uint8_t ourFrom_ = 0;
    uint8_t ourTo_ = 0;
    bool ended_ = false;
    NearbySeat seats_[6] = {};
    uint8_t seatCount_ = 0;
    uint32_t seatsAtMs_ = 0;
    char lobbyNote_[32] = {0};
    NearbyWatch watch_;
    bool pausePainted_ = false;
    uint16_t pauseSecondsDrawn_ = 0;

    // ---- what is on the screen ----------------------------------------------------
    uint8_t dirty_[(Go::MAX_POINTS + 7) / 8] = {};
    bool anyDirty_ = false;
    bool turnStale_ = true;
    bool capturesStale_ = true;
    bool infoStale_ = true;
    bool buttonsStale_ = true;
    bool confirmShown_ = false;
    bool lobbyStale_ = true;
    bool resultStale_ = true;

    /* The whole game, flat, for NVS: the board packed to two bits a point.
     * Fixed layout: loadBlob() refuses a blob whose length changed, and
     * `version` covers one that kept its size. */
    struct Saved {
        uint16_t magic;
        uint8_t version;
        uint8_t mode;
        uint8_t phase;
        uint8_t boardSize;
        uint8_t rules;
        uint8_t level;
        uint8_t n;
        uint8_t at[(Go::MAX_POINTS + 3) / 4];
        uint8_t toMove;
        uint16_t ko;
        uint16_t captured[2];
        uint8_t passes;
        uint16_t moves;
        uint16_t last;
        uint8_t over;
        uint8_t winner;
        uint8_t humanColour;
        uint8_t agreed;
        uint8_t dead[(Go::MAX_POINTS + 7) / 8];
        char opponent[5];
        char opponentName[11];
        uint8_t session;
        uint8_t ourPly;
        uint8_t theirPly;
        uint8_t ourFrom;
        uint8_t ourTo;
        uint8_t ended;
    };
    static constexpr uint16_t SAVE_MAGIC = 0x60A0;
    /* 2 changed what `agreed` means: version 1 counted local agreements 1/3
     * while using colour bits remotely, so a version-1 blob restored under
     * these rules would read one local press as White having agreed. The
     * struct is the same size, which is exactly the case `version` exists
     * for -- loadBlob() cannot catch it by length. */
    static constexpr uint8_t SAVE_VERSION = 2;
    static constexpr uint32_t CONFIRM_MS = 3000;
};
