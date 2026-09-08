#include "CursiveGame.h"

#include "CursiveGlyphData.h"
#include "engine/AppRegistry.h"

namespace {

constexpr AppMetadata CURSIVE_METADATA = {
    "cursive",
    "Cursive",
    nullptr,
    "joined-up letters",
    "Cursive",
    "Trace joined-up letters, big and small.",
    nullptr,
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
    {"ABC", 0, 26, 0, nullptr},
    {"abc", 26, 26, 0, nullptr},
    {"Words", CURSIVE_WORD_FIRST, CURSIVE_WORD_COUNT, 12, CURSIVE_WORDS},
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
    (void)host;
    tracer_.configure(CURSIVE_GLYPHS, CURSIVE_SETS,
                      sizeof(CURSIVE_SETS) / sizeof(CURSIVE_SETS[0]));
    tracer_.begin();
    markFullDirty();
}

void CursiveGame::update(AppContext& host, const TouchPoint& touch) {
    tracer_.update(host, touch);
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
