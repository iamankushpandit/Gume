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
    "Trace big and small letters.",
    nullptr,
    LauncherIcon::Trace,
    6,
    true,
};

/* Where each alphabet starts in TRACE_GLYPHS and how many it has. Counts
 * rather than end indices: an off-by-one in a boundary silently offers the
 * wrong letter, and a count that is wrong is obviously wrong. */
constexpr LetterTracer::Set TRACE_SETS[] = {
    {"ABC", 0, 26},
    {"abc", 26, 26},
    {"123", 52, 10},
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
