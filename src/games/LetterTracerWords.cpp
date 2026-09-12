#include "LetterTracer.h"
#include "LetterTracerLayout.h"

/* PRINTED WORDS, SPELLED OUT OF THE ALPHABET RATHER THAN STORED.
 *
 * A printed word is its letters side by side, each written with its own
 * strokes and a pen lift between them -- which is exactly what the glyph table
 * already holds. So a word set in print stores only its words, as strings, and
 * this file lays each one out from the lowercase letters when it is chosen.
 * The letterforms a child sees in "cat" are therefore the very ones they
 * practised on the abc tab, and a fix to a letter fixes every word it is in.
 *
 * Cursive cannot work this way and does not try: joining the letters is the
 * whole skill there, so its words are generated as single strokes by
 * tools/gen_cursive_glyphs.py.
 *
 * Everything is laid out in canvas pixels, relative to the canvas's corner,
 * and loadGlyph() then draws it through the identity mapping. */

using namespace LetterTracerLayout;

namespace {

/* A narrow letter still takes up room. An 'i' or an 'l' is a single vertical
 * line with no width at all, and laid out by its ink alone it would stand on
 * the shoulder of the next letter. */
constexpr int16_t MIN_LETTER_W = 36;     // letter units, the table's own box
/* The space between one letter's ink and the next's. About a quarter of the
 * printed x-height, which is roughly what a handwriting sheet leaves. */
constexpr int16_t LETTER_GAP = 26;
/* Keeps the first and last letters off the canvas edge, in pixels. There must
 * be room for the first stroke's numbered arrow beside it. */
constexpr int16_t WORD_MARGIN = 10;

struct Extent {
    int16_t minX, maxX, minY, maxY;
};

Extent glyphExtent(const LetterTracer::Glyph& g) {
    Extent e{INT16_MAX, INT16_MIN, INT16_MAX, INT16_MIN};
    for (uint8_t s = 0; s < g.strokeCount; ++s) {
        const LetterTracer::Stroke& st = g.strokes[s];
        for (uint8_t i = 0; i < st.count; ++i) {
            const int16_t x = st.pts[i * 2];
            const int16_t y = st.pts[i * 2 + 1];
            if (x < e.minX) e.minX = x;
            if (x > e.maxX) e.maxX = x;
            if (y < e.minY) e.minY = y;
            if (y > e.maxY) e.maxY = y;
        }
    }
    return e;
}

/* How wide a letter is once it has been given its minimum, in letter units. */
int16_t letterWidth(const Extent& e) {
    const int16_t w = static_cast<int16_t>(e.maxX - e.minX);
    return w < MIN_LETTER_W ? MIN_LETTER_W : w;
}

bool isLower(char c) {
    return c >= 'a' && c <= 'z';
}

}   // namespace

/* ONE SCALE FOR THE WHOLE SET, decided by its widest word -- the same rule
 * Cursive's generator follows, for the same reason: a handwriting sheet
 * writes every word at the same size on the same lines, and "cat" shrinking
 * because the word before it was "go" would look like a fault.
 *
 * Capped at the scale a lone letter is drawn at, so no word is ever written
 * larger than the letters it is made of. And the height is measured from the
 * alphabet itself -- the top of its tallest ascender to the bottom of its
 * deepest descender -- rather than stated, so every word shares one baseline
 * whether or not it has an ascender in it. */
void LetterTracer::fitSpelledSet() {
    const Set& s = set();
    int16_t top = INT16_MAX;
    int16_t bottom = INT16_MIN;
    for (uint8_t c = 0; c < 26; ++c) {
        const Extent e = glyphExtent(glyphs_[s.alphabet + c]);
        if (e.minY < top) top = e.minY;
        if (e.maxY > bottom) bottom = e.maxY;
    }

    int32_t widest = 1;
    for (uint8_t n = 0; n < s.count; ++n) {
        int32_t w = 0;
        for (const char* p = s.names[n]; *p; ++p) {
            if (!isLower(*p)) continue;
            if (w > 0) w += LETTER_GAP;
            w += letterWidth(glyphExtent(glyphs_[s.alphabet + (*p - 'a')]));
        }
        if (w > widest) widest = w;
    }

    const float letter = min(static_cast<float>(DRAW_W) / coordW_,
                             static_cast<float>(DRAW_H) / coordH_);
    const float byWidth = static_cast<float>(DRAW_W - 2 * WORD_MARGIN) / widest;
    const float byHeight = static_cast<float>(DRAW_H) / (bottom - top);
    wordScale_ = min(letter, min(byWidth, byHeight));
    wordTop_ = static_cast<int16_t>((DRAW_H - (bottom - top) * wordScale_) / 2 -
                                    top * wordScale_);
}

/* Lay the current word out, letter by letter, into spelledPts_.
 *
 * Each letter keeps its own strokes in its own order -- the bowl of an 'a'
 * before its stem, the stem of a 't' before its crossbar -- so the numbers a
 * child follows in a word are the numbers they learned on the letter. A stroke
 * that would overflow the buffer is dropped whole, never cut short: a word
 * missing its last letter is visibly wrong, a letter missing half a stroke
 * looks like a different letter. */
void LetterTracer::spellWord() {
    const Set& s = set();
    const uint8_t n = static_cast<uint8_t>(glyphIndex_ - s.first);
    const char* word = n < s.count ? s.names[n] : "";

    int32_t width = 0;
    for (const char* p = word; *p; ++p) {
        if (!isLower(*p)) continue;
        if (width > 0) width += LETTER_GAP;
        width += letterWidth(glyphExtent(glyphs_[s.alphabet + (*p - 'a')]));
    }
    const float left = (DRAW_W - width * wordScale_) / 2.0f;

    uint16_t used = 0;
    uint8_t strokes = 0;
    int32_t pen = 0;
    bool full = false;
    for (const char* p = word; *p && !full; ++p) {
        if (!isLower(*p)) continue;
        const Glyph& letter = glyphs_[s.alphabet + (*p - 'a')];
        const Extent e = glyphExtent(letter);
        /* Centre a narrow letter in the room it was given. */
        const int16_t pad = static_cast<int16_t>((letterWidth(e) - (e.maxX - e.minX)) / 2);
        for (uint8_t k = 0; k < letter.strokeCount; ++k) {
            const Stroke& st = letter.strokes[k];
            if (strokes >= MAX_STROKES || used + st.count * 2u > MAX_WORD_COORDS) {
                full = true;
                break;
            }
            int16_t* out = &spelledPts_[used];
            for (uint8_t i = 0; i < st.count; ++i) {
                const int32_t ux = pen + pad + (st.pts[i * 2] - e.minX);
                out[i * 2] = static_cast<int16_t>(left + ux * wordScale_ + 0.5f);
                out[i * 2 + 1] = static_cast<int16_t>(
                    wordTop_ + st.pts[i * 2 + 1] * wordScale_ + 0.5f);
            }
            spelledStrokes_[strokes] = Stroke{out, st.count};
            ++strokes;
            used = static_cast<uint16_t>(used + st.count * 2u);
        }
        pen += letterWidth(e) + LETTER_GAP;
    }

    spelled_.label = word[0];
    spelled_.strokes = spelledStrokes_;
    spelled_.strokeCount = strokes;
}
