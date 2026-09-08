#pragma once

#include "LetterTracer.h"
#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& traceAppMetadata();

/* Trace printed letters and digits: ABC, abc, 123.
 *
 * A shell over LetterTracer, which is where the tracing actually lives. This
 * class is now only three facts: which glyph table, which alphabets, and its
 * own name. Everything that used to be here moved when Cursive needed the
 * same engine -- see LetterTracer.h.
 */
class TraceGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

    /* The glyph data model, kept here as aliases so TraceGlyphData.cpp -- 260
     * lines of hand-authored letterforms -- did not have to be touched by a
     * refactor that changed no letter in it. */
    using Stroke = LetterTracer::Stroke;
    using Glyph = LetterTracer::Glyph;

private:
    LetterTracer tracer_;
};
