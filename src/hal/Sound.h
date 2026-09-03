#pragma once

#include <stdint.h>

/* The console's sound vocabulary.
 *
 * Every noise Braino makes is one of these, and every one of them is
 * SYNTHESISED at play time from a handful of oscillator segments -- there is
 * not a single sample, WAV or audio table in this firmware, and there must
 * not be one. Flash is the binding constraint here (see CLAUDE.md's shared
 * budgets), and even a modest set of short 16kHz clips would cost more than
 * every game's artwork put together. A cue costs about forty bytes of const
 * data instead. See BoardAudio.cpp for the engine.
 *
 * WHY A NAMED VOCABULARY RATHER THAN board.beep(freq, ms).
 *
 * The two beeps were the whole of the language for a long time, so a game
 * with anything else to say -- a tile landing, a round ending, a personal
 * best -- either said nothing or said "correct" again. A fixed vocabulary
 * gives the console ONE voice: `Coin` sounds the same in Whack-a-Mole as it
 * does in Cinnamon, so a player learns it once. Games choosing their own
 * frequencies is how that stops being true, which is why `beep()` stays
 * private to Board.
 *
 * Adding a cue is adding a word to that language. Do it when a game has
 * something genuinely new to say, not when an existing cue is nearly right.
 *
 * On a board with no codec these are silent -- `Correct` and `Wrong` still
 * pulse the RGB LED, because that is what they have always done and it is the
 * whole of the feedback there. */
enum class Sound : uint8_t {
    Tap,        // a control was pressed
    Select,     // a choice was committed, or a page turned
    Correct,    // right answer -- what beepOk() plays
    Wrong,      // wrong answer -- what beepError() plays
    Reveal,     // a hidden thing is now showing
    Coin,       // a point scored
    LevelUp,    // a round or level was cleared
    Victory,    // the whole game was won
    GameOver,   // out of lives, time, or moves
    HighScore,  // a personal best was beaten
    Countdown,  // one tick of a timer running out
    Whoosh,     // something slid or moved
    Pop,        // something appeared

    /* Four pitched pads, for a colour-sequence round -- Cinnamon is the one
     * that needs them. They are a vocabulary entry rather than a game picking
     * its own frequencies because the point of that game is that the four
     * colours have four FIXED notes and the player learns the tune: a pad
     * whose pitch drifted between rounds would break it. They are the C major
     * triad plus its octave, the same set the Coin/LevelUp/Victory family is
     * built from, so a pad landing next to a cue is still in key. */
    Pad1,
    Pad2,
    Pad3,
    Pad4,

    Boot,       // the spoken startup phrase
};
