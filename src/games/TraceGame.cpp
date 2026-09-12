#include "TraceGame.h"

#include "TraceGlyphData.h"
#include "engine/AppRegistry.h"

namespace {

constexpr AppMetadata TRACE_METADATA = {
    "trace",
    "Trace",
    nullptr,
    "ABC abc 123",
    "Trace",
    "Trace letters, numbers and words.",
    nullptr,
    LauncherIcon::Trace,
    6,
    true,
};

/* PRINTED WORDS, AND WHY THESE.
 *
 * Short words a five-year-old reads or is about to: the CVC words a phonics
 * scheme starts with (cat, dog, sun), a few sight words (the, and, you, see),
 * and every letter of the alphabet but one, so the tab practises nearly all of
 * them. Two and three letters only.
 *
 * Short AND NARROW on purpose. Every word in the set is drawn at the one scale
 * the widest of them allows (see LetterTracer::fitSpelledSet()), so a single
 * wide word shrinks every other word with it. Measured, in the letters' own
 * units: 'quiz' is 434 and drew the whole set at an x-height of 46 pixels;
 * 'mud' and 'web', with their wide m and w, are 402. Without those three the
 * widest is 372 and every word gains 17%. That costs q, which has no word
 * shorter than 'quiz' -- the same letter Cursive's words cannot reach -- and
 * q is practised on the abc tab. Measure before adding a word:
 * tools/gen_screens.py restates the layout, so _spell() there has the sums.
 *
 * Each costs only its letters in flash: the words are spelled out of the abc
 * glyphs at runtime, so the strokes a child follows in "cat" are the ones they
 * practised on the abc tab. */
const char* const TRACE_WORDS[] = {
    "at", "and", "ant", "bat", "bed", "big", "box", "bus", "cat", "cup",
    "dad", "dog", "egg", "fox", "fun", "go", "hat", "hen", "hop", "ink",
    "is", "it", "jam", "jet", "kid", "kit", "leg", "log", "me", "net",
    "no", "nut", "on", "owl", "pen", "pig", "red", "run", "see", "sit",
    "six", "sun", "ten", "the", "top", "up", "van", "vet", "we", "wet",
    "yes", "you", "zip", "zoo",
};
constexpr uint8_t TRACE_WORD_COUNT = sizeof(TRACE_WORDS) / sizeof(TRACE_WORDS[0]);

/* Where the lowercase alphabet starts in TRACE_GLYPHS; the words are spelled
 * out of it. */
constexpr uint8_t TRACE_LOWER_FIRST = 26;

/* Where each alphabet starts in TRACE_GLYPHS and how many it has. Counts
 * rather than end indices: an off-by-one in a boundary silently offers the
 * wrong letter, and a count that is wrong is obviously wrong.
 *
 * Printed letters get an arrow at every sharp reversal as well as the numbered
 * one at each stroke's start: the apex of an A is where a child lifts off in
 * the wrong direction. Words do not -- a word is up to eight strokes, and the
 * numbered arrows are already a lot to read. */
constexpr LetterTracer::Set TRACE_SETS[] = {
    {"ABC", 0, 26, 0, nullptr, false, true, LetterTracer::NO_ALPHABET, 0},
    {"abc", 26, 26, 0, nullptr, false, true, LetterTracer::NO_ALPHABET, 0},
    {"123", 52, 10, 0, nullptr, false, true, LetterTracer::NO_ALPHABET, 0},
    {"Words", 0, TRACE_WORD_COUNT, 14, TRACE_WORDS, true, false, TRACE_LOWER_FIRST, 0},
};

}   // namespace

const AppMetadata& traceAppMetadata() {
    return TRACE_METADATA;
}

const char* TraceGame::title() const {
    return traceAppMetadata().screenTitle != nullptr
        ? traceAppMetadata().screenTitle
        : traceAppMetadata().title;
}

void TraceGame::begin(AppContext& host) {
    (void)host;
    tracer_.configure(TRACE_GLYPHS, TRACE_SETS,
                      sizeof(TRACE_SETS) / sizeof(TRACE_SETS[0]));
    tracer_.begin();
    markFullDirty();
}

void TraceGame::update(AppContext& host, const TouchPoint& touch) {
    tracer_.update(host, touch);
    /* BOTH are drained, every frame, before either is acted on. markFullDirty()
     * in the tracer raises both flags, so a short-circuit would leave the
     * plain one set and spend the next frame on a repaint nothing asked for. */
    const bool full = tracer_.takeFullDirty();
    const bool any = tracer_.takeDirty();
    if (full) {
        markFullDirty();
    } else if (any) {
        markDirty();
    }
}

void TraceGame::render(AppContext& host) {
    tracer_.render(host, title(), needsFullRender());
}
