#pragma once

#include "LetterTracer.h"
#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& cursiveAppMetadata();

/* Trace joined-up letters: cursive ABC and abc.
 *
 * The same finger-tracing engine as Trace, with different letters -- which is
 * the whole reason LetterTracer exists as its own thing. This class is three
 * facts: which glyph table, which alphabets, and its own name.
 *
 * Cursive is a genuinely different skill from print, not a decoration on it.
 * Most of these letters are ONE unbroken stroke, so the dots run from the
 * entry stroke on the baseline all the way to the exit stroke without lifting
 * -- which is the thing a child has to feel to learn it. The printed 'a' in
 * Trace is a circle and a line; the cursive 'a' is one movement.
 *
 * The letterforms are generated: tools/gen_cursive_glyphs.py holds them as
 * Bezier chains and writes both the table and a preview sheet. A curve typed
 * out as a polyline is forty numbers nobody can check by eye, and a malformed
 * cursive 'q' reads as a perfectly good 9 right up until a child copies it.
 */
class CursiveGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

    using Stroke = LetterTracer::Stroke;
    using Glyph = LetterTracer::Glyph;

private:
    LetterTracer tracer_;
    /* How many letters and words this player has finished tracing, ever.
     *
     * A count rather than a best, and it only ever goes up: there is nothing
     * to win here and nothing to lose, so the only honest number is how much
     * practice has been done. Held in RAM and written through to the profile's
     * own storage on each completion -- see CursiveGame.cpp on why that write
     * is affordable. */
    uint32_t practised_ = 0;
};
