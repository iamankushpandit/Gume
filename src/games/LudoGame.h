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
    /*   Lobby   who sits in each seat on this console
     *   Table   inviting consoles in the room, or waiting to be started by one
     *   Play    a game, local or across consoles */
    enum class Mode : uint8_t { Lobby, Table, Play };

    /* Our part in a game across consoles. The host invited the others, deals
     * nothing -- the seed does that -- and plays any computer seats; a guest
     * accepted an invitation. Once play starts the two are equal except for
     * the computer seats. */
    enum class Role : uint8_t { None, Host, Guest };

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
    /* A computer plays this seat. At a table of consoles that comes from the
     * deal, not from the lobby's seat kinds, which stay as the owner left them
     * for the next local game. */
    bool isComputer(uint8_t seat) const {
        if (net_) {
            return owner_[seat] != Ludo::NO_SEAT && owner_[seat] >= chairCount_;
        }
        return kind_[seat] == SeatKind::Computer;
    }
    bool canStart() const;
    /* A delay, or a quarter of it once every person at the table has
     * finished. The rules still play the computers out for the remaining
     * places, but a child who came first should not have to sit through two
     * computers doing it at a pace chosen for somebody watching to learn. */
    uint32_t pace(uint32_t ms) const;
    void setMessage(const char* text);
    /* "Waiting for" and the waiting seat's own token, drawn after the words:
     * the colour answers "for whom?" faster than a name would, and at a table
     * the name could be ten characters the panel has no room for. */
    void setWaiting(uint8_t seat);
    /* One clock for everything that blinks, so the dot and the die flash
     * together rather than drifting apart. */
    static bool blinkPhase();
    /* The die's frame flashes on the console whose person has to roll -- the
     * one screen where "it is your go" is the whole message. */
    bool dieFlashing() const;
    void saveGame(AppContext& host) const;
    bool restoreGame(AppContext& host);

    // ---- a table of consoles (LudoTable.cpp) ------------------------------
    /* The nearby service carries it; LudoRules' Net namespace spells it. What
     * is here is the part only a screen can do: who was invited, who has
     * answered, when to start, and taking each console's turn off the air in
     * order. See LudoRules.h for the encoding. */
    void openTable(AppContext& host);
    void leaveTable(AppContext& host);
    void updateTable(AppContext& host, const TouchPoint& touch);
    void refreshPeers(AppContext& host);
    /** A peer that answered our invitation with its presence. */
    bool joined(AppContext& host, const char* id);
    uint8_t joinedCount(AppContext& host);
    bool hostCanStart(AppContext& host);
    void hostStart(AppContext& host);
    /** A guest seeing the host's start word, with everyone it names present. */
    bool guestTryStart(AppContext& host);
    void startNetGame(AppContext& host, const Ludo::Net::Start& st);
    /** Every frame of a game across consoles: our word, endings, others' turns. */
    void pollTable(AppContext& host, uint32_t now);
    /* May we replace the ply we are advertising? Only once every other
     * console has applied it -- otherwise a console that missed it could never
     * catch up, because nothing would be carrying it any more. In plain turn
     * order this is already true by the time our turn comes round; it is what
     * makes a bonus roll, or the host playing a computer seat straight after
     * its own, safe. */
    bool canPublish(AppContext& host);
    /** Put the roll just resolved for `seat` on the air as the next ply. */
    void publishPly(AppContext& host, uint8_t seat, uint8_t code);
    /** This console decides for `seat`: a person here, or a computer we host. */
    bool ownsSeat(uint8_t seat) const;
    /** A seat decided by the air, not by this console. */
    bool remoteSeat(uint8_t seat) const { return net_ && !ownsSeat(seat); }
    /** The console whose word carries `seat`'s turns. */
    const char* ownerId(uint8_t seat) const;
    /** "You", a console's label or tag, or "CPU". nullptr in a local game. */
    const char* seatLabel(uint8_t seat) const;
    void renderTable(AppContext& host);
    static Rect tableRowRect(uint8_t row);
    static Rect tableComputersRect();
    static Rect tableLevelRect();
    static Rect tableBackRect();
    static Rect tableStartRect();
    static Rect nearbyRect();

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
    /** The near-black every outline, pip and digit is drawn in. */
    static uint16_t inkColour();
    /** A token on a square, and in the panel's seat list. */
    static constexpr int16_t TOKEN_R = 5;

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
    /* `frameOn` false paints the frame in the ground colour: the flash. The
     * die is 44px, so it is repainted whole -- same pixels in the same colours
     * everywhere but the frame, which is what makes that invisible. */
    void drawDie(Ui::Renderer& tft, bool frameOn) const;
    /* The dot beside `seat`'s row in the seat list, lit or cleared. Its own
     * few pixels and nothing else: it blinks, so it must be cheap. */
    void drawTurnDot(Ui::Renderer& tft, uint8_t seat, bool on) const;
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
    /* A seat whose token follows the message, or NO_SEAT. See setWaiting(). */
    uint8_t messageSeat_ = Ludo::NO_SEAT;
    uint32_t confirmUntilMs_ = 0;
    /* When a person here gets the die, YourTurn plays at this time -- held
     * back until the last move's own cue, which ends at cueEndsMs_, is over.
     * 0 when none is due; a roll cancels it. */
    uint32_t turnCueAtMs_ = 0;
    uint32_t cueEndsMs_ = 0;
    /* Said in the lobby after a table ended -- "A4F2 ended the game" -- until
     * the next tap, so a child whose game vanished is told why. */
    char lobbyNote_[32] = {0};
    bool lastBlink_ = false;

    // ---- a table of consoles ------------------------------------------------
    /* One console at the table. The label is the owner's own name for that
     * console, read from local NVS when the table formed; it is shown and
     * never sent. */
    struct Chair {
        char id[5];
        char name[11];
    };
    bool net_ = false;
    Role role_ = Role::None;
    uint8_t session_ = 0;
    char hostId_[5] = {0};
    Chair chairs_[Ludo::Net::MAX_HUMANS] = {};   // sorted by tag
    uint8_t chairCount_ = 0;
    uint8_t selfChair_ = 0;
    /* Colour -> participant: a chair index, chairCount_ onwards for the
     * computers, NO_SEAT for an empty colour. From Ludo::Net::deal(). */
    uint8_t owner_[Ludo::SEATS] = {Ludo::NO_SEAT, Ludo::NO_SEAT, Ludo::NO_SEAT,
                                   Ludo::NO_SEAT};
    /* The last ply this console has applied, and the word it is advertising.
     * Both survive a save, so a console that was put down comes back saying
     * exactly what it said before. */
    uint8_t applied_ = Ludo::Net::NOT_STARTED;
    uint8_t myPly_ = 0;
    uint8_t myFrom_ = 0;
    uint8_t myTo_ = 0;
    /* The move another console made with the roll in hand, accepted and
     * waiting for the screen to animate it. */
    uint8_t netCode_ = 0;
    /* Somebody ended the game. Our own word stays on the air saying so, and
     * nothing may overwrite it with a move. */
    bool ended_ = false;

    // The table lobby.
    static constexpr uint8_t MAX_PEERS = 6;
    NearbySeat peers_[MAX_PEERS] = {};
    uint8_t peerCount_ = 0;
    uint32_t peersAtMs_ = 0;
    char invited_[Ludo::Net::MAX_HUMANS - 1][5] = {};
    uint8_t invitedCount_ = 0;
    uint8_t inviteCursor_ = 0;
    uint32_t nextInviteMs_ = 0;
    uint8_t computers_ = 0;
    bool tableStale_ = true;
    uint32_t tableSig_ = 0;
    /* The local lobby says so when an invitation to Ludo is waiting. */
    bool inviteWaiting_ = false;

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
    uint8_t drawnMessageSeat_ = 0xFE;
    uint8_t drawnDotSeat_ = Ludo::NO_SEAT;
    bool drawnDotOn_ = false;
    bool drawnFrameOn_ = true;
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
        // A game across consoles, when net is set.
        uint8_t net;
        uint8_t role;
        uint8_t session;
        uint8_t chairCount;
        uint8_t selfChair;
        uint8_t applied;
        uint8_t myPly;
        uint8_t myFrom;
        uint8_t myTo;
        uint8_t netCode;
        uint8_t ended;
        uint8_t owner[Ludo::SEATS];
        char hostId[5];
        Chair chairs[Ludo::Net::MAX_HUMANS];
    };
    static constexpr uint16_t SAVE_MAGIC = 0x1D05;
    /* 2 added the table fields. A version-1 blob is a different length, so
     * loadBlob() refuses it and the lobby comes back with its defaults. */
    static constexpr uint8_t SAVE_VERSION = 2;
};
