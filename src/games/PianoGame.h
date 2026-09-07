#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& pianoAppMetadata();

/* A one-octave keyboard, C4 to C5.
 *
 * There is no score, no round and no right answer. That is deliberate and it
 * is the first thing in this catalog that is a toy rather than a drill -- you
 * cannot lose at it and it never asks you a question. A "best" here would be
 * meaningless, so the metadata leaves score null, as Dice and Trace do.
 *
 * It is also the first playable app to set followsLayout, so it draws through
 * the raw renderer at the panel's real size and honours the owner's
 * orientation. Everything here is measured from tft.width()/height() at render
 * time. Nothing may use SCREEN_WIDTH, SCREEN_HEIGHT or GAME_CANVAS_*: on a
 * 240px-wide portrait panel a layout built against the 320px canvas draws a
 * third of itself off the edge, and the serial log looks perfectly healthy
 * while it happens.
 *
 * On a board with no speaker the keys still light and still name the note.
 * playSound() compiles to nothing there, so this needs no guard of its own --
 * the app is offered on every board either way.
 */
class PianoGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

    /* Seven white keys plus the octave, and five black. A single octave rather
     * than two: at 240px portrait, fifteen white keys would be 16px each,
     * which is below what a child's finger can hit on a resistive panel. One
     * octave gives 30px in portrait and 40px in landscape.
     *
     * Public because the note, name and layout tables in the .cpp are sized
     * from them. They describe the shape of a keyboard, which is not a secret. */
    static constexpr uint8_t WHITE_KEYS = 8;
    static constexpr uint8_t BLACK_KEYS = 5;
    static constexpr uint8_t KEY_COUNT = WHITE_KEYS + BLACK_KEYS;

private:
    /** No key held. */
    static constexpr uint8_t NO_KEY = 0xFF;

    /* How long a pressed key stays lit after the finger lifts. Matched to the
     * note length so the light goes out roughly when the sound does; a key
     * that unlit instantly looked like a missed press. */
    static constexpr uint32_t KEY_LIT_MS = 320;

    Rect keyboardRect(AppContext& host) const;
    Rect whiteKeyRect(AppContext& host, uint8_t index) const;
    Rect blackKeyRect(AppContext& host, uint8_t index) const;
    /* Which key is under a point, black keys first because they are drawn on
     * top of the white ones and overlap them. Returns NO_KEY for a miss. */
    uint8_t keyAt(AppContext& host, int16_t x, int16_t y) const;
    void drawKey(AppContext& host, uint8_t key, bool lit) const;

    uint8_t held_ = NO_KEY;      // key under the finger right now
    uint8_t lit_ = NO_KEY;       // key drawn lit, which outlives the press
    uint8_t drawnLit_ = NO_KEY;  // what is actually on the panel
    uint32_t litSinceMs_ = 0;
    /* When the held note was last (re-)armed. A cue is a fixed script, not a
     * note-on: the synthesiser has no sustain, so holding a key only keeps
     * sounding if the note is asked for again before the last one runs out. */
    uint32_t lastArmMs_ = 0;
    Sound soundFor(uint8_t key) const;
};
