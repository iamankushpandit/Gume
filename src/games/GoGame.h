// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

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
 * to two bits a point so nineteen-by-nineteen is a hundred bytes. A game
 * passed between two people or played against the computer is the player's
 * own; a game against another console is the console's, whoever is holding
 * it, and only End game ends it (GoSave.cpp). End game asks twice. Undo
 * takes back your last move and, against the computer, its reply; never in
 * a nearby game, which would need protocol this has not got.
 *
 * Score: wins against the computer, per profile. */
class GoGame : public AppGame {
public:
#if GUME_HAS_NAV_KEYS
    /* Puts back what the d-pad selector's ring covered (Game.h). */
    bool repaintUnderRing(AppContext& host, const Rect& box) override;
#endif
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    /** Saves. A nearby game stays on the air, because leaving the screen
     *  does not end it; anything else hands the radio back. */
    void end(AppContext& host) override;
    /* A game on the board comes back as it was left, with no question asked:
     * End game is this screen's own way out of one. In GoSave.cpp. */
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

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
    /** A new game on the lobby's chips, or on a board and rules given. */
    void newGame(Mode mode);
    void newGame(Mode mode, uint8_t n, Go::Rules rules);
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

    // ---- the lobby and a nearby game (GoNet.cpp) --------------------------------
    /* The two things both consoles of a nearby game must agree before the
     * first stone. The console that asks states them in the low four bits of
     * the session number -- the rules in three, the 19x19 board in the
     * fourth -- and the console that answers plays what it was asked to,
     * whatever its own chips say. */
    static constexpr uint8_t SETTING_BITS = 4;
    static constexpr uint8_t SETTING_RULES = 0x07;
    static constexpr uint8_t SETTING_BIG = 0x08;
    /** Rows for consoles in the room, under the two for playing here. */
    static constexpr uint8_t LOBBY_SEATS = 3;

    /* What a lobby row says about one console in the room. Invites, PlayBusy
     * and Play take a press; the others only say what that console is
     * doing, and are drawn so that they do not look like buttons. */
    enum class Row : uint8_t {
        Invites,    // asking us, to a game this console can play
        TooBig,     // asking us to 19x19, on a panel too small for it
        NoRules,    // asking us under rules this build has not got
        Asking,     // waiting on somebody else's answer
        Playing,    // in a game with a console we can hear as well
        PlayBusy,   // in a game with somebody; can still be asked
        Play,       // free
    };
    static Row rowFor(const NearbySeat& seat);
    static bool rowTakesPress(Row row) {
        return row == Row::Invites || row == Row::PlayBusy || row == Row::Play;
    }
    static void rowLabel(const NearbySeat& seat, Row row, char* out, size_t len);
    void updateLobby(AppContext& host, const TouchPoint& touch);
    /** Who is in the room and what each is doing, read now; the lobby is
     *  repainted if any row would read differently. */
    void refreshSeats(AppContext& host);
    void pressSeat(AppContext& host, uint8_t index);
    /** Their game, on their rules and board: our presence is the answer. */
    bool acceptInvite(AppContext& host, const NearbySeat& seat);
    void inviteSeat(AppContext& host, const NearbySeat& seat);
    /** Back from Asking: the invitation comes off the air. */
    void takeBackInvite(AppContext& host);
    bool startRemote(AppContext& host, const NearbySeat& seat, uint8_t session, bool weAreBlack,
                     bool waiting);
    void pollRemote(AppContext& host);
    void publishOurs(AppContext& host);
    /** Our next numbered word: a stone, a pass, a toggle or an agreement. */
    void sendWord(AppContext& host, Go::Net::Kind kind, uint16_t p);
    /** Has the other console applied everything we have said so far? */
    bool peerCaughtUp() const { return haveAck_ && theirAck_ == ourPly_; }
    /** In the marking: may we say something now? Says "wait" when not. */
    bool remoteMayMark(AppContext& host);
    /** End game from the marking phase of a nearby game, asked twice. */
    void pressEndInMarking(AppContext& host, uint32_t now);
    bool updatePause(AppContext& host, const TouchPoint& touch, uint32_t now);
    void drawPause(Ui::Renderer& tft);
    /** Tells the d-pad's selector whether the board takes a press just now
     *  (GoGame.cpp, beside the input it describes). */
    void updateStops();

    // ---- a nearby game ending (GoSave.cpp) -----------------------------------
    /** A finished game's last word, said once more and left for a minute. */
    void sayLastWord(AppContext& host);
    void holdLastWord(AppContext& host);
    bool lastWordUp() const;
    /** A nearby game is finished with: forgotten, and the lobby shown. */
    void lobbyAfterNearby(AppContext& host);
    void leaveRemote(AppContext& host, const char* note);
    void endRemoteByUs(AppContext& host);

    // ---- save (GoSave.cpp) ----------------------------------------------------
    struct Saved;
    /** The game on screen, to the console's slot or the player's. */
    void saveGame(AppContext& host) const;
    /** The lobby's chips alone, leaving a game in the player's save as it is. */
    void saveChips(AppContext& host) const;
    /** The player's own save, or false when there is none to read. */
    bool loadOwn(AppContext& host, Saved& out) const;
    void fillSaved(Saved& out) const;
    void takeChips(const Saved& in);
    /** Take the game out of a blob, if it is one a game could have reached. */
    bool applySaved(const Saved& in, bool nearby);
    /** The player's own saved game, picked up again from the lobby. */
    void resumeShelved(AppContext& host);

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
    void renderLobby(AppContext& host);   // GoNet.cpp, with the rest of the lobby
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
    /* The highest ply of ours the other console says it has applied. A turn
     * is one state on the air, so a second word sent before the first was
     * heard replaces it and the first is lost for good: in the marking phase,
     * where both sides speak when they like, nothing more is sent until this
     * has caught up with ourPly_. */
    uint8_t theirAck_ = 0;
    bool haveAck_ = false;
    /* Our last dead-group toggle in this marking. An agreement from the other
     * console counts only if it had seen that toggle when it agreed. */
    uint8_t lastTogglePly_ = 0;
    bool toggledThisMarking_ = false;
    /* A game that finished, or that we ended, leaves its last word on the air
     * for the other console to hear -- taken down at once, they would wait
     * for ever on a move that had been made -- and the service takes it down
     * a minute later. This is when it went up, or 0: while it stands, leaving
     * this screen or opening it again does not take it down early. */
    uint32_t lastWordAtMs_ = 0;
    /* The lobby is up over a game in the player's own save: an invitation
     * was waiting when the screen opened, or a nearby game has just ended.
     * This is the kind of game that is, so the lobby can offer to carry it
     * on; Lobby for none. */
    Mode shelved_ = Mode::Lobby;
    NearbySeat seats_[LOBBY_SEATS] = {};
    uint8_t seatCount_ = 0;
    uint32_t seatsAtMs_ = 0;
    char lobbyNote_[32] = {0};
    NearbyWatch watch_;
    bool pausePainted_ = false;
    uint16_t pauseSecondsDrawn_ = 0;
    /* A press that began on the pause card is the card's until the finger
     * lifts: let through, the rest of it put a ghost on the board. */
    bool pauseHold_ = false;
    /* The result card is the selector's overlay while it is up. */
    bool resultOverlay_ = false;

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
     * `version` covers one that kept its size. boardSize, rules and level
     * are the lobby's chips; n and gameRules are the game's own, which in a
     * nearby game are whatever the invitation said. */
    struct Saved {
        uint16_t magic;
        uint8_t version;
        uint8_t mode;
        uint8_t phase;
        uint8_t boardSize;
        uint8_t rules;
        uint8_t level;
        uint8_t n;
        uint8_t gameRules;
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
    };
    static constexpr uint16_t SAVE_MAGIC = 0x60A0;
    /* 2 changed what `agreed` means: version 1 counted local agreements 1/3
     * while using colour bits remotely, so a version-1 blob restored under
     * these rules would read one local press as White having agreed. The
     * struct is the same size, which is exactly the case `version` exists
     * for -- loadBlob() cannot catch it by length.
     *
     * 3 gave the game its own rules beside the lobby's chip, and came with
     * the key below. */
    static constexpr uint8_t SAVE_VERSION = 3;
    /* Go's own key. Every nearby game once saved under "game", so saving one
     * destroyed another's; that key is read once more, for a game left under
     * it (GoSave.cpp), and never written. */
    static constexpr const char* SAVE_KEY = "go_game";
    static constexpr uint32_t CONFIRM_MS = 3000;
};
