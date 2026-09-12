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
#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
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
constexpr Segment blip(uint16_t hz, uint16_t ms, uint8_t amp) {
    return Segment{hz, hz, 0, ms, amp, Wave::Square};
}
constexpr Segment slide(uint16_t from, uint16_t to, uint16_t ms, uint8_t amp) {
    return Segment{from, to, 0, ms, amp, Wave::Square};
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

/* ------------------------------------------------------------------ cues
 *
 * Pitches are named where they are notes, because the cues that are runs have
 * to agree with each other: Coin, LevelUp, Victory and HighScore are all
 * built from the same C major set, so two of them landing close together
 * sounds like one instrument rather than like a fault.
 *
 * Three rules shape them, and they are the ones to hold to when adding more:
 *
 *   - A cue a player hears hundreds of times in a session (Tap, Coin) is
 *     SHORT and quiet. Past about 120ms it stops being feedback and starts
 *     being something the game is waiting for.
 *   - Cues that mean opposite things differ in DIRECTION, not only in pitch.
 *     Rising is good, falling is not. A player who is not listening carefully
 *     -- which is every player -- hears the contour before the notes.
 *   - The amplitudes are RELATIVE, and the top of the range is meant to be
 *     used. The first set of these topped out at 55 and the console was
 *     inaudible across a room -- partly because of the volume-curve bug in
 *     applyCodecVolume(), but partly because leaving 45% of the range unused
 *     on a 1-inch driver is just quietness. What matters is the spacing: Tap
 *     at 55 against Victory at 92 is what makes one feel incidental and the
 *     other final. Raising them all together changes nothing about that.
 */

constexpr Segment CUE_TAP[] = {
    blip(1400, 30, 55),
};

constexpr Segment CUE_SELECT[] = {
    slide(900, 1500, 50, 62),
};

constexpr Segment CUE_CORRECT[] = {
    tone(880, 95, 88),
    tone(1320, 120, 88),
};

constexpr Segment CUE_WRONG[] = {
    tone(160, 130, 100),
    hush(40),
    tone(160, 150, 100),
};

constexpr Segment CUE_REVEAL[] = {
    sweep(500, 1100, 75, 72),
};

constexpr Segment CUE_COIN[] = {
    blip(1568, 50, 72),     // G6
    blip(2093, 95, 72),     // C7
};

constexpr Segment CUE_LEVEL_UP[] = {
    blip(784, 75, 80),      // G5
    blip(988, 75, 80),      // B5
    blip(1175, 140, 80),    // D6
};

constexpr Segment CUE_VICTORY[] = {
    blip(523, 95, 85),      // C5
    blip(659, 95, 85),      // E5
    blip(784, 95, 85),      // G5
    blip(1047, 250, 92),    // C6
};

/* The only cue that falls the whole way, and the longest -- losing is the one
 * moment in a game that is allowed to take a beat. */
constexpr Segment CUE_GAME_OVER[] = {
    tone(392, 150, 92),     // G4
    tone(330, 150, 92),     // E4
    sweep(311, 110, 400, 96),
};

constexpr Segment CUE_HIGH_SCORE[] = {
    blip(1047, 65, 80),     // C6
    blip(1319, 65, 80),     // E6
    blip(1568, 65, 80),     // G6
    blip(2093, 75, 88),     // C7
    hush(40),
    blip(2093, 170, 88),
};

constexpr Segment CUE_COUNTDOWN[] = {
    tone(1000, 45, 68),
};

/* Three noise bands in sequence, sliding down: the cheapest thing that reads
 * as movement. One band on its own does not -- it just sounds like static. */
constexpr Segment CUE_WHOOSH[] = {
    air(2600, 1200, 32, 60),
    air(1700, 1000, 32, 68),
    air(950, 800, 42, 55),
};

constexpr Segment CUE_POP[] = {
    slide(300, 1300, 45, 76),
};

/* One square of a counter's walk -- Ludo plays one per hop, and a hop is
 * 110ms, so this has to be over well inside that or a walk becomes one long
 * buzz. Sine and quiet, because a six is six of them in a row. */
constexpr Segment CUE_STEP[] = {
    tone(988, 35, 58),      // B5
};

/* The same note twice, a doorbell: it asks for attention without saying
 * anything was right or wrong, which is why it neither rises like Correct nor
 * falls like Wrong. Heard after the other seats have been playing, so it is
 * allowed to be louder than a Tap. */
constexpr Segment CUE_YOUR_TURN[] = {
    tone(784, 70, 80),      // G5
    hush(45),
    tone(784, 120, 80),
};

/* A struck bell, twice. The one cue meant to carry across a room.
 *
 * There is no per-segment envelope -- gain slews between segments and that is
 * all -- so the decay of a strike is spelled out as the same pitch at falling
 * amplitude. Two strikes rather than one because a single ring is easy to
 * mistake for one of the shorter cues; a pair is unmistakably a bell asking
 * for attention.
 *
 * 580ms total, and the ceiling matters: the runtime re-arms this every
 * ALERT_CADENCE_MS, and arm() would read a re-arm inside its own duration as
 * a held note and never restart it. See Sound::Bell. */
constexpr Segment CUE_BELL[] = {
    tone(1319, 60, 95),     // E6, struck
    tone(1319, 90, 62),
    tone(1319, 110, 30),    // ringing down
    hush(40),
    tone(1046, 60, 95),     // C6
    tone(1046, 90, 62),
    tone(1046, 130, 28),
};

/* The four pads. Sine rather than square: they are the only cue that plays
 * repeatedly at a steady pulse, and a square wave becomes wearing after two
 * dozen of them. 260ms fits inside Cinnamon's 600ms lit period with room, so
 * the note has stopped before the pad goes dark rather than being cut off by
 * the next one. */
/* One chromatic octave, equal temperament with A4 = 440. Indexed by
 * (cue - Sound::NoteC4), which is why Sound.h insists that run stays
 * contiguous.
 *
 * A table and a range test rather than thirteen more switch cases: every note
 * is the same one-segment script with a different number in it, and spelling
 * that out thirteen times would bury the cues that actually differ from each
 * other.
 *
 * Sine, like the pads and for the same reason -- a piano is played fast and
 * repeatedly, and a square wave becomes wearing within a minute. 320ms is long
 * enough to ring after the finger lifts and short enough that a quick run does
 * not turn into one continuous note; the synthesiser is monophonic, so a new
 * key replaces the one before it rather than sounding with it. */
constexpr uint16_t NOTE_HZ[SOUND_NOTE_COUNT] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, 523,
};
constexpr uint16_t NOTE_MS = SOUND_NOTE_MS;   // one statement of the fact
constexpr uint8_t NOTE_AMP = 85;

constexpr Segment CUE_PAD_1[] = {tone(392, 260, 85)};    // G4
constexpr Segment CUE_PAD_2[] = {tone(523, 260, 85)};    // C5
constexpr Segment CUE_PAD_3[] = {tone(659, 260, 85)};    // E5
constexpr Segment CUE_PAD_4[] = {tone(784, 260, 85)};    // G5

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
#endif  /* GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC */

/* The Board:: members below are the public face of all this; they reach the
 * synthesiser by name, as they did when it was one file. */
using namespace audio;

void Board::beep(uint16_t frequency, uint16_t ms) {
#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
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
#if GUME_HAS_AUDIO_CODEC || GUME_HAS_AUDIO_DAC
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
        const Segment note[] = {tone(NOTE_HZ[i], NOTE_MS, NOTE_AMP)};
        armCue(note);
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
