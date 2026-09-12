#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

/* Is the other console still there? -- the part of a nearby game that every
 * one of them needs and none of them had.
 *
 * A two-player game can end two ways and only one arrives as a message.
 * nearbyEnd() is somebody pressing End game. The other is a flat battery, a
 * child walking into the next room, a console sat on: none of those can send
 * anything, so until this existed a game whose opponent vanished sat on "their
 * turn" for ever, saying nothing. The service now measures the silence
 * (AppContext::nearbyPeerSilentMs); this turns it into three states and one
 * card, so five games cannot each answer "how long is too long" differently.
 *
 *   Present   heard within NearbyPlay::PEER_QUIET_MS. Play on.
 *   Quiet     not heard for longer than that. Missing one scan window is
 *             ordinary; six seconds is several. The game PAUSES -- it cannot
 *             proceed anyway, nothing of theirs is arriving -- and the card
 *             says who it is waiting for and for how long.
 *   Gone      the scanner has dropped the peer (BleScan::SIGHTING_TTL_MS).
 *             Still paused; the card now says out of range, and offers the
 *             one thing a game with more than two seats can do about it.
 *
 * Coming back needs nothing: a turn is a state that stays on the air until it
 * is replaced, so a console that reappears in the same session re-hears the
 * current move and the game picks up where it stopped. tick() reports the
 * return and the game repaints.
 *
 * The card is a question -- "Keep waiting" or "End game" -- and Keep waiting
 * means what it says: the card goes away, the game stays paused with its own
 * "Waiting for..." line, and the card comes back once if the peer then goes
 * from Quiet to Gone, because that is a new fact. Waiting has no time limit:
 * the game is saved after every move, so waiting costs nothing and the console
 * can be put down.
 *
 * Nothing here transmits. It is all derived from the ABSENCE of a beacon that
 * is already there. */
class NearbyWatch {
public:
    enum class State : uint8_t { Present, Quiet, Gone };
    enum class Press : uint8_t { None, Wait, End, Extra };

    /* Forget everything: a game just started, or this console left one. */
    void reset();

    /* One peer, every frame. Returns true when something the screen shows
     * changed -- the state, the seconds line, or the card appearing. */
    bool tick(AppContext& host, const char* peerId, uint32_t now);
    /* A table of peers: the state is the worst of them and `who()` names it.
     * `skip` is this console's own index, or 0xFF. `dropped` marks chairs no
     * longer at the table (bit per index), which are not waited for. */
    bool tickTable(AppContext& host, const char (*ids)[5], uint8_t count, uint8_t skip,
                   uint8_t dropped, uint32_t now);

    State state() const { return state_; }
    bool paused() const { return state_ != State::Present; }
    /* True on the frame the peer came back from Quiet or Gone. The game
     * repaints whole (the card was over its board) and plays a cue. */
    bool resumed() const { return resumed_; }
    /** Index into the table passed to tickTable(); 0 for tick(). */
    uint8_t who() const { return who_; }
    /** Whole seconds of silence, for a message line of the game's own. */
    uint16_t silentSeconds() const { return seconds_; }

    /* Should the card be on screen? False while Present, and false after
     * Keep waiting until the state gets worse. */
    bool cardShown() const { return paused() && dismissed_ != state_; }
    void dismiss() { dismissed_ = state_; }

    /* The card, centred on `area` -- the board, since that is what a pause
     * is over. `who` is the peer's label. `extra` is a third button for the
     * game that has something more to offer than waiting (Ludo's "Play
     * without"), or nullptr. Draws everything; the caller decides when. */
    void draw(Ui::Renderer& tft, const Rect& area, const char* who, const char* extra) const;
    /* Repaint only the seconds line, when the card is already up. */
    void drawSeconds(Ui::Renderer& tft, const Rect& area, bool hasExtra) const;
    /** Which button a press landed on, if the card is shown. */
    Press press(const Rect& area, const TouchPoint& touch, bool hasExtra) const;

    static Rect cardRect(const Rect& area, bool hasExtra);

private:
    static State stateFor(uint32_t silentMs);
    Rect waitRect(const Rect& area, bool hasExtra) const;
    Rect endRect(const Rect& area, bool hasExtra) const;
    Rect extraRect(const Rect& area) const;
    Rect secondsRect(const Rect& area, bool hasExtra) const;

    State state_ = State::Present;
    State dismissed_ = State::Present;
    uint8_t who_ = 0;
    uint16_t seconds_ = 0;
    bool resumed_ = false;
};
