// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

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
 * On a board with no audio path -- neither a codec nor the built-in DAC --
 * these are silent. `Correct` and `Wrong` still pulse the RGB LED, because
 * that is what they have always done and it is the whole of the feedback
 * there. No supported board is in that state today. */
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
    Step,       // a piece moved one square along a track
    YourTurn,   // play has come round to the person holding the console

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

    /* One chromatic octave, C4 to C5, for the Piano.
     *
     * These are in the vocabulary for the same reason the four pads are, and
     * the reason is worth restating because it is the rule this file exists to
     * enforce: `Board::beep(hz, ms)` is private, and a game that picks its own
     * frequencies is exactly how a shared sound vocabulary stops being shared.
     * A piano needs pitches, so the pitches are declared here, once, and the
     * game chooses among them. It cannot invent a note that is not on this
     * list -- which is the whole point.
     *
     * They must stay contiguous and in this order: playSound() maps the range
     * onto a frequency table by subtracting NoteC4, rather than carrying
     * thirteen more cases in a switch that is already the length of the
     * vocabulary. Insert a note in the middle and every note above it changes
     * pitch. */
    NoteC4,
    NoteCs4,
    NoteD4,
    NoteDs4,
    NoteE4,
    NoteF4,
    NoteFs4,
    NoteG4,
    NoteGs4,
    NoteA4,
    NoteAs4,
    NoteB4,
    NoteC5,

    /* Keep the note just struck sounding: a piano key still held down.
     *
     * Not a sound of its own. A note is struck, so asking for the same note
     * again strikes it again -- right for a key pressed twice, wrong for a
     * key that never came up, which would re-strike every few hundred
     * milliseconds. The two look identical from the synthesiser, so the game
     * that knows which it is says so: PianoGame sends this instead of the
     * note while a finger stays down, every SOUND_NOTE_REARM_MS. With nothing
     * held it does nothing. */
    NoteHold,

    /* THE ONE CUE MEANT TO BE HEARD FROM ANOTHER ROOM.
     *
     * Every other sound here is feedback for somebody already holding the
     * console, so it is short and quiet on purpose. This one answers "find my
     * Braino": it is a struck bell, loud, with a long enough tail to carry
     * through a cushion, and the runtime repeats it on a cadence for as long
     * as the alert lasts. Each repeat strikes it again: a script of Notes is
     * never taken for a continuation of itself. */
    Bell,

    Boot,       // the spoken startup phrase
};

/* How long a note is held without being asked again. A key that stays down
 * sends Sound::NoteHold within this long of the strike, and again within this
 * long of each hold, or the note is let go and falls away. Stated here rather
 * than inside the synthesiser because the caller has to know when to ask. */
constexpr uint16_t SOUND_NOTE_MS = 320;

/* How often a caller asks for a held note again. NOT "a little before
 * SOUND_NOTE_MS", which is what it was, and why a held piano key fell silent
 * once, about a third of a second in, and then held perfectly.
 *
 * The note is generated ahead of the speaker, not with it. When a sound is
 * armed on a quiet console the generator fills the whole DMA at once -- 96ms
 * on the codec, 64ms on the built-in DAC, plus a block -- and from then on
 * stays that far ahead. So at the first re-arm, 280ms of wall time after the
 * press, the engine had already made 384ms of a 320ms note, faded it out,
 * and stopped; the re-arm started a fresh note after a gap. Later re-arms
 * found the generator paced by the DMA and arrived in time, so it happened
 * exactly once per held key.
 *
 * The re-arm therefore has to land before the note ends by the DMA's lead
 * and a slow frame or two. BoardAudio.cpp static_asserts exactly that against
 * the real DMA size, and test/host/audio_synth_test.cpp replays a held key on
 * that timeline. Asking more often costs nothing audible: a hold strikes
 * nothing and changes no sample. */
constexpr uint16_t SOUND_NOTE_REARM_MS = 120;

/** Number of entries in the NoteC4..NoteC5 run. */
constexpr uint8_t SOUND_NOTE_COUNT =
    static_cast<uint8_t>(Sound::NoteC5) - static_cast<uint8_t>(Sound::NoteC4) + 1;
