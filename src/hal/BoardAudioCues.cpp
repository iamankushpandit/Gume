// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "BoardAudioInternal.h"

/* Every sound the console can make.
 *
 * There are no audio files here and there must never be one. Each cue is a
 * const Segment array -- a script of oscillator, noise and formant steps --
 * and the synthesiser next door turns it into samples. One second of 16-bit
 * mono audio is 32 KB; the whole vocabulary as synthesis is under a kilobyte,
 * on a flash budget that is already past 80%.
 *
 * Adding a cue is adding a word to a language. Do it when a game has something
 * genuinely new to say, not when an existing cue is nearly right: `Coin` means
 * the same thing in Whack-a-Mole as in Memory, and a game picking its own
 * frequencies is exactly how that stops being true. That is also why
 * Board::beep() is private and every screen goes through playSound().
 */
#if GUME_HAS_AUDIO
namespace {
using namespace audio;

/* Shorthands, so the cue tables below read as music rather than as struct
 * initialisers. Durations are milliseconds, amplitudes are percentages. */
constexpr Segment hush(uint16_t ms) {
    return Segment{0, 0, 0, ms, 0, Wave::Silence};
}
constexpr Segment tone(uint16_t hz, uint16_t ms, uint8_t amp) {
    return Segment{hz, hz, 0, ms, amp, Wave::Tone};
}
constexpr Segment sweep(uint16_t from, uint16_t to, uint16_t ms, uint8_t amp) {
    return Segment{from, to, 0, ms, amp, Wave::Tone};
}
constexpr Segment air(uint16_t centreHz, uint16_t bandwidthHz, uint16_t ms, uint8_t amp) {
    return Segment{centreHz, bandwidthHz, 0, ms, amp, Wave::Noise};
}
constexpr Segment vox(uint16_t f1, uint16_t f2, uint16_t f3, uint16_t ms, uint8_t amp) {
    return Segment{f1, f2, f3, ms, amp, Wave::Voiced};
}
constexpr Segment hiss(uint16_t f1, uint16_t f2, uint16_t f3, uint16_t ms, uint8_t amp) {
    return Segment{f1, f2, f3, ms, amp, Wave::Unvoiced};
}

/* A note struck on a timbre, and one that glides from one pitch to another.
 * The segment's length is when the NEXT thing starts; the note itself rings
 * on until its timbre has let it die away. */
constexpr Segment note(uint16_t hz, uint16_t ms, uint8_t amp, Timbre timbre) {
    return Segment{hz, hz, timbre, ms, amp, Wave::Note};
}
constexpr Segment glide(uint16_t from, uint16_t to, uint16_t ms, uint8_t amp, Timbre timbre) {
    return Segment{from, to, timbre, ms, amp, Wave::Note};
}

/* ------------------------------------------------------------------ cues
 *
 * THE SMOOTH SET, chosen by the owner on 2026-11-15 from a listening page
 * that offered three: struck glass, wood, and smooth -- pure tones that glide.
 * Smooth won for the four heard most and as the direction for the rest, so
 * every cue below is a pure sine struck and left to fade, most of them
 * gliding into or between their notes. The Bell alone carries a soft octave
 * above its note, because it is the one cue that has to be heard through a
 * cushion. GameOver and the spoken phrase were not in question and are the
 * old engine's, unchanged.
 *
 * Pitches are named where they are notes, because the cues that are runs have
 * to agree with each other: Coin, LevelUp, Victory and HighScore are all
 * built from the same major set, so two of them landing close together
 * sounds like one instrument rather than like a fault -- and now that a note
 * rings on under the next, they bloom into the chord they spell.
 *
 * Three rules shape them, and they are the ones to hold to when adding more:
 *
 *   - A cue a player hears hundreds of times in a session (Tap, Coin) is
 *     SHORT and quiet. Past about 120ms it stops being feedback and starts
 *     being something the game is waiting for.
 *   - Cues that mean opposite things differ in DIRECTION, not only in pitch.
 *     Rising is good, falling is not. A player who is not listening carefully
 *     -- which is every player -- hears the contour before the notes. A glide
 *     makes the contour the whole of the sound.
 *   - The amplitudes are RELATIVE, and the top of the range is meant to be
 *     used. What matters is the spacing between them, not their level.
 *     A note's `amp` is its peak, and notes that ring under each other are
 *     summed through a soft ceiling, so a chord may name more than 100 between
 *     its notes without being clipped square.
 */

constexpr Segment CUE_TAP[] = {
    glide(700, 1100, 25, 90, PURE),
};

constexpr Segment CUE_SELECT[] = {
    glide(600, 900, 50, 92, SOFT),
};

constexpr Segment CUE_CORRECT[] = {
    glide(659, 988, 90, 88, SOFT),      // E5 up to B5
    glide(988, 1319, 150, 92, SOFT),    // and on to E6
};

/* One long fall, where the old Wrong was two low buzzes. Down a sixth, and
 * slow enough to hear as a sigh rather than a slip. */
constexpr Segment CUE_WRONG[] = {
    glide(392, 233, 240, 96, SOFT),     // G4 down to B-flat 3
};

constexpr Segment CUE_REVEAL[] = {
    glide(659, 1319, 80, 86, SOFT),     // E5 up an octave
};

constexpr Segment CUE_COIN[] = {
    note(1568, 45, 78, GLOW),           // G6
    glide(1976, 2093, 80, 84, GLOW),    // into C7
};

constexpr Segment CUE_LEVEL_UP[] = {
    note(784, 70, 72, GLOW),            // G5
    note(988, 70, 75, GLOW),            // B5
    note(1175, 70, 78, GLOW),           // D6
    note(1568, 150, 86, RING),          // G6
};

constexpr Segment CUE_VICTORY[] = {
    note(523, 100, 84, GLOW),           // C5
    note(659, 100, 86, GLOW),           // E5
    note(784, 100, 88, GLOW),           // G5
    glide(988, 1047, 300, 100, RING),   // into C6, and let it ring
};

/* The only cue that falls the whole way, and the longest -- losing is the one
 * moment in a game that is allowed to take a beat. Not part of the smooth
 * set: the owner kept it as it was. */
constexpr Segment CUE_GAME_OVER[] = {
    tone(392, 150, 92),     // G4
    tone(330, 150, 92),     // E4
    sweep(311, 110, 400, 96),
};

constexpr Segment CUE_HIGH_SCORE[] = {
    note(1047, 60, 66, GLOW),           // C6
    note(1319, 60, 68, GLOW),           // E6
    note(1568, 60, 70, GLOW),           // G6
    note(2093, 120, 76, RING),          // C7
    note(1568, 1, 62, RING),            // then G6 and C7 struck together
    note(2093, 200, 72, RING),
};

constexpr Segment CUE_COUNTDOWN[] = {
    note(1000, 30, 90, PURE),
};

/* A pure tone swelling in and falling an octave and more on its own clock,
 * with a breath of air under it. The old Whoosh was the noise alone, which
 * read as static as much as movement; the glide is what moves. SWISH glides
 * over 150ms whatever the segment's length, so the air begins while the tone
 * is still falling. */
constexpr Segment CUE_WHOOSH[] = {
    glide(1400, 520, 1, 80, SWISH),
    air(2400, 1400, 50, 16),
    air(1600, 1100, 50, 20),
    air(1000, 900, 50, 12),
};

constexpr Segment CUE_POP[] = {
    glide(420, 1250, 30, 92, PURE),
};

/* One square of a counter's walk -- Ludo plays one per hop, and a hop is
 * 110ms, so this has to be over well inside that or a walk becomes one long
 * buzz. */
constexpr Segment CUE_STEP[] = {
    note(988, 30, 80, TICK),            // B5
};

/* The same note twice, a doorbell: it asks for attention without saying
 * anything was right or wrong, which is why it neither rises like Correct nor
 * falls like Wrong. Heard after the other seats have been playing, so it is
 * allowed to be louder than a Tap. */
constexpr Segment CUE_YOUR_TURN[] = {
    note(784, 160, 92, RING),           // G5
    note(784, 200, 94, RING),
};

/* A struck bell, twice. The one cue meant to carry across a room, and the
 * one with an overtone: a pure sine on a 1-inch speaker is easy to lose
 * through a cushion, and an octave above it is not. Two strikes rather than
 * one because a single ring is easy to mistake for one of the shorter cues.
 *
 * The runtime re-arms this every ALERT_CADENCE_MS. A Note is struck again
 * whenever it is armed again, so a repeat always rings. See Sound::Bell. */
constexpr Segment CUE_BELL[] = {
    note(1319, 290, 125, BELL),         // E6
    note(1046, 290, 125, BELL),         // C6
};

/* One chromatic octave, equal temperament with A4 = 440. Indexed by
 * (cue - Sound::NoteC4), which is why Sound.h insists that run stays
 * contiguous.
 *
 * A table and a range test rather than thirteen more switch cases: every note
 * is the same one-segment script with a different number in it, and spelling
 * that out thirteen times would bury the cues that actually differ from each
 * other.
 *
 * KEYS holds at 40% while the key is down and falls in 60ms once it comes
 * up. The note's segment is SOUND_NOTE_MS; PianoGame sends Sound::NoteHold
 * every SOUND_NOTE_REARM_MS while a finger stays on the key, which gives it
 * its segment again without striking it. A new key takes a voice of its own,
 * so the one before it rings out underneath. */
constexpr uint16_t NOTE_HZ[SOUND_NOTE_COUNT] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, 523,
};
constexpr uint16_t NOTE_MS = SOUND_NOTE_MS;   // one statement of the fact
constexpr uint8_t NOTE_AMP = 92;

/* The four pads, struck and gone well inside Cinnamon's 600ms lit period, so
 * a pad has stopped sounding before it goes dark. */
constexpr Segment CUE_PAD_1[] = {note(392, 60, 110, PAD)};    // G4
constexpr Segment CUE_PAD_2[] = {note(523, 60, 110, PAD)};    // C5
constexpr Segment CUE_PAD_3[] = {note(659, 60, 110, PAD)};    // E5
constexpr Segment CUE_PAD_4[] = {note(784, 60, 110, PAD)};    // G5

/* -------------------------------------------------------------- the voice
 *
 * "Let's play Braino!", as twenty-one segments of formant synthesis.
 *
 * There is no text-to-speech here and no dictionary. The phrase is spelled
 * out as the phonemes it is made of, and each phoneme is the first three
 * formant frequencies of a vocal tract shaped to say it. Drive three
 * resonators tuned to those frequencies with a buzz and a vowel comes out;
 * drive the same three with noise and a consonant does. That is the whole
 * technique, and it is roughly forty years old -- it is what made a 1980s
 * home computer talk, which is exactly why this sounds like one. The ask was
 * for a robotic voice, and the honest way to get one is to build the thing
 * that is genuinely robotic rather than to compress a recording of a person.
 *
 *   L  EH T S     P L  EY     B R  EY N  OW
 *
 * Three things about the transcription are load-bearing:
 *
 *   - A DIPHTHONG IS TWO SEGMENTS. The "EY" in "play" is not one sound: the
 *     tongue moves from EH towards IY while it is being said and F2 climbs
 *     around 500Hz doing it. Written as a single steady segment it comes out
 *     as "pleh". Both EYs below are split, and so is the closing OW.
 *   - A STOP IS A SILENCE AND THEN A BURST. T, P and B are not sounds; they
 *     are the moment a closed mouth opens. The silence is not padding and
 *     cannot be trimmed -- without it the burst has nothing to contrast
 *     against and the word simply begins with a click.
 *   - THE FRICATIVE IS THE LONG ONE. The S is 120ms, longer than any vowel
 *     here. Fricatives carry little energy at these amplitudes, so an S of
 *     "correct" length is inaudible on a small driver and the phrase lands
 *     as "let play".
 *
 * FOUR THINGS ARE SET BY EAR ON HARDWARE, and all four moved after the first
 * listen on a real device, where the phrase came out too fast and too quiet to
 * make out:
 *
 *   - IT IS SLOW. 2.4 seconds for four syllables, roughly twice a person's
 *     conversational rate. Synthetic speech with no pitch movement carries
 *     none of the prosody a listener leans on, so the only cue left for where
 *     one sound ends and the next begins is duration. Speeding this up is the
 *     first thing that makes it unintelligible again.
 *   - THE STRESSED VOWELS ARE THE LONG ONES. "LET'S", "PLAY", "BRAY" get
 *     150-190ms; the unstressed N and the final OW taper off. Equal durations
 *     read as a robot spelling out letters rather than saying words.
 *   - THE DIPHTHONGS ARE THREE STEPS, NOT TWO. F2 climbs about 550 Hz across
 *     an "EY" and the ear tracks that sweep; in two steps the jump is audible
 *     as a click at the join and the vowel reads as two vowels.
 *   - THE AMPLITUDES ARE NEAR THE TOP. Speech is much quieter than a tone at
 *     the same nominal amplitude, because the excitation is impulses and most
 *     of a vowel is the decay between them -- which is also why
 *     FORMANT_BANDWIDTH matters here. See the note on it above.
 *
 * The remaining knob is VOICE_PITCH_HZ: lower is more menacing, higher is more
 * toy. Nothing else in the file needs to change. */
constexpr Segment PHRASE_LETS_PLAY_BRAINO[] = {
    vox(360, 1050, 2600, 110, 92),     // L
    vox(560, 1750, 2500, 190, 100),    // EH  -- the stressed vowel of "Let's"
    hush(45),                          // T   -- the closure
    hiss(3000, 4200, 5200, 28, 95),    // T   -- the burst
    hiss(4800, 6200, 7000, 200, 100),  // S
    hush(110),                         //     -- between words

    hush(45),                          // P   -- the closure
    hiss(800, 1600, 2400, 22, 85),     // P   -- the burst
    vox(360, 1050, 2600, 100, 92),     // L
    vox(600, 1700, 2450, 150, 100),    // EY  -- open, where the stress sits
    vox(450, 1980, 2650, 110, 96),     // EY  -- mid-glide
    vox(330, 2250, 2900, 100, 88),     // EY  -- closed
    hush(120),                         //     -- between words

    hush(35),                          // B   -- the closure
    vox(250, 900, 2200, 50, 90),       // B   -- the burst, and it is voiced
    vox(350, 1050, 1500, 110, 96),     // R   -- a low F3 is the whole of an R
    vox(600, 1700, 2450, 150, 100),    // EY
    vox(450, 1980, 2650, 110, 96),     // EY  -- mid-glide
    vox(330, 2250, 2900, 100, 88),     // EY  -- closed
    vox(250, 1400, 2600, 130, 78),     // N   -- nasal, so quieter
    vox(500, 950, 2400, 150, 100),     // OW
    vox(430, 820, 2400, 130, 92),      // OW  -- rounding
    vox(370, 730, 2400, 120, 78),      // OW  -- and closing
    hush(60),
};

template <size_t N>
void armCue(const Segment (&segments)[N]) {
    arm(segments, static_cast<uint8_t>(N));
}

}  // namespace
#endif  /* GUME_HAS_AUDIO */

/* The Board:: members below are the public face of all this; they reach the
 * synthesiser by name, as they did when it was one file. Guarded like the
 * namespace itself: on a board with no audio path there is no `audio` to
 * name, and every member below compiles to its silent arm. */
#if GUME_HAS_AUDIO
using namespace audio;
#endif

void Board::beep(uint16_t frequency, uint16_t ms) {
#if GUME_HAS_AUDIO
    if (!soundEnabled()) return;
    const Segment one[] = {tone(frequency, ms, 90)};
    armCue(one);
#else
    (void)frequency;
    (void)ms;
#endif
}

/* The whole vocabulary, in one place. A switch rather than a table indexed by
 * the enum because the arrays have different lengths and a table of
 * {pointer, count} pairs is the same size with one more thing to get wrong;
 * the compiler folds this into a jump table either way. There is no default
 * case on purpose -- adding a Sound without a cue is then a build warning
 * rather than a screen that silently makes no noise. */
void Board::playSound(Sound cue) {
#if GUME_HAS_AUDIO
    /* The mute gate is here, at the one door every sound goes through --
     * including the boot phrase and both beeps. A switch labelled Mute that
     * left something still audible would be exactly the kind of half-truth
     * the About-page rules are written against. The RGB pulse is deliberately
     * NOT gated: beepOk() and beepError() pulse before they call this, so
     * muting takes the sound and leaves the light. */
    if (!soundEnabled()) return;

    /* The piano's octave, handled as a range before the switch. Still behind
     * the mute gate above -- there is one door, and notes come through it like
     * everything else. */
    if (cue >= Sound::NoteC4 && cue <= Sound::NoteC5) {
        const uint8_t i = static_cast<uint8_t>(
            static_cast<uint8_t>(cue) - static_cast<uint8_t>(Sound::NoteC4));
        const Segment key[] = {note(NOTE_HZ[i], NOTE_MS, NOTE_AMP, KEYS)};
        armCue(key);
        return;
    }

    switch (cue) {
        case Sound::Tap:       armCue(CUE_TAP); break;
        case Sound::Select:    armCue(CUE_SELECT); break;
        case Sound::Correct:   armCue(CUE_CORRECT); break;
        case Sound::Wrong:     armCue(CUE_WRONG); break;
        case Sound::Reveal:    armCue(CUE_REVEAL); break;
        case Sound::Coin:      armCue(CUE_COIN); break;
        case Sound::LevelUp:   armCue(CUE_LEVEL_UP); break;
        case Sound::Victory:   armCue(CUE_VICTORY); break;
        case Sound::GameOver:  armCue(CUE_GAME_OVER); break;
        case Sound::HighScore: armCue(CUE_HIGH_SCORE); break;
        case Sound::Countdown: armCue(CUE_COUNTDOWN); break;
        case Sound::Whoosh:    armCue(CUE_WHOOSH); break;
        case Sound::Pop:       armCue(CUE_POP); break;
        case Sound::Step:      armCue(CUE_STEP); break;
        case Sound::YourTurn:  armCue(CUE_YOUR_TURN); break;
        case Sound::NoteHold:  holdNote(); break;
        case Sound::Bell:      armCue(CUE_BELL); break;
        case Sound::Pad1:      armCue(CUE_PAD_1); break;
        case Sound::Pad2:      armCue(CUE_PAD_2); break;
        case Sound::Pad3:      armCue(CUE_PAD_3); break;
        case Sound::Pad4:      armCue(CUE_PAD_4); break;
        case Sound::Boot:      armCue(PHRASE_LETS_PLAY_BRAINO); break;
        /* Handled by the range test above, before this switch. Listed so the
         * compiler still checks the enum is covered -- an unlisted case here
         * would be a warning we would rather have than not. */
        case Sound::NoteC4:  case Sound::NoteCs4: case Sound::NoteD4:
        case Sound::NoteDs4: case Sound::NoteE4:  case Sound::NoteF4:
        case Sound::NoteFs4: case Sound::NoteG4:  case Sound::NoteGs4:
        case Sound::NoteA4:  case Sound::NoteAs4: case Sound::NoteB4:
        case Sound::NoteC5:
            break;
    }
#else
    (void)cue;
#endif
}

/* The two beeps are the ones bring-up settled on in src/s3_diag.cpp: a rising
 * two-tone for yes, a low double buzz for no. They differ in pitch, length AND
 * shape, so they stay distinguishable to a player who is not listening
 * carefully -- which is every player.
 *
 * They keep their own names rather than becoming playSound(Sound::Correct) at
 * the call site: they are called from a few hundred places, they pulse the
 * RGB LED as well, and on a board with no codec that pulse is all they are. */
void Board::beepOk() {
    pulseRgb(0, 255, 40, 450);
    playSound(Sound::Correct);
}

void Board::beepError() {
    pulseRgb(255, 0, 0, 450);
    playSound(Sound::Wrong);
}
