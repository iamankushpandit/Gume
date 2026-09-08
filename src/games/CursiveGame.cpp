#include "CursiveGame.h"

#include "CursiveGlyphData.h"
#include "engine/AppRegistry.h"

namespace {

/* Higher is better, and it never resets. The number is how many letters and
 * words have been finished, so it grows for as long as a child keeps
 * practising -- which is the whole of what this game is for. There is no win
 * condition to score against and inventing one would turn handwriting practice
 * into a test. */
constexpr AppScoreInfo CURSIVE_SCORE = {
    "cursive", "Cursive", "curTrace", "traced", false
};

constexpr AppMetadata CURSIVE_METADATA = {
    "cursive",
    "Cursive",
    nullptr,
    "joined-up letters",
    "Cursive",
    "Trace joined-up letters and words.",
    &CURSIVE_SCORE,
    LauncherIcon::Cursive,
    34,
    true,
};

/* Two alphabets, no digits: there is no such thing as a cursive 7. The third
 * tab slot is simply left empty -- LetterTracer keeps Prev where it is rather
 * than sliding it up, because a control that moves depending on which game you
 * opened is a control you have to look for. */
/* Three modes, and the third is the point of cursive.
 *
 * Letters teach the shapes; words are where joining up actually happens, and a
 * child who can draw a lone 'c' still has to learn that 'cat' is one movement
 * across the page. Ten short words, all lowercase, all three letters.
 *
 * The word set traces at a tighter dot spacing. A word is a third of the
 * height of a single letter on the same canvas, so the default 20px would put
 * about two dots on each letter and the guide would stop guiding. */
constexpr LetterTracer::Set CURSIVE_SETS[] = {
    {"ABC", 0, 26, 0, nullptr, false},
    {"abc", 26, 26, 0, nullptr, false},
    {"Words", CURSIVE_WORD_FIRST, CURSIVE_WORD_COUNT, 12, CURSIVE_WORDS, true},
};

}   // namespace

const AppMetadata& cursiveAppMetadata() {
    return CURSIVE_METADATA;
}

const char* CursiveGame::title() const {
    return cursiveAppMetadata().screenTitle != nullptr
        ? cursiveAppMetadata().screenTitle
        : cursiveAppMetadata().title;
}

void CursiveGame::begin(AppContext& host) {
    tracer_.configure(CURSIVE_GLYPHS, CURSIVE_SETS,
                      sizeof(CURSIVE_SETS) / sizeof(CURSIVE_SETS[0]));
    tracer_.begin();
    practised_ = host.getScore(CURSIVE_SCORE.bestKey);
    markFullDirty();
}

void CursiveGame::update(AppContext& host, const TouchPoint& touch) {
    tracer_.update(host, touch);

    /* One more letter or word finished.
     *
     * Written through on every completion rather than batched on the way out:
     * a child who traces four letters and then has the console taken off them
     * should keep the four, and one NVS write per completed letter is nothing
     * -- a completion takes tens of seconds of finger-dragging, so this is
     * about as far from a hot path as this firmware has.
     *
     * saveBestScore as well as setScore, because the Scores app reads the best
     * and for this game the best and the total are the same number. */
    if (tracer_.takeCompleted()) {
        ++practised_;
        host.setScore(CURSIVE_SCORE.bestKey, practised_);
        host.saveBestScore(CURSIVE_SCORE.bestKey, practised_,
                           CURSIVE_SCORE.lowerIsBetter);
    }

    const bool full = tracer_.takeFullDirty();
    const bool any = tracer_.takeDirty();
    if (full) {
        markFullDirty();
    } else if (any) {
        markDirty();
    }
}

void CursiveGame::render(AppContext& host) {
    tracer_.render(host, title(), needsFullRender());
}
