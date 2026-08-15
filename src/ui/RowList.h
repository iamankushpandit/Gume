#pragma once

#include <Arduino.h>
#include "ui/Ui.h"

/* Scrolling label/value list, as used by System Info.
 *
 * ------------------------------------------------------------------------
 * Why this holds char buffers and not String
 * ------------------------------------------------------------------------
 * This device has no garbage collector and a heap it can never compact. The
 * failure mode is not a leak -- there is not one `new` or `malloc` in this
 * firmware -- it is fragmentation: many small allocations of differing sizes,
 * made and freed repeatedly, leave the free space chopped into pieces too
 * small to satisfy a later request. Free heap looks healthy right up until an
 * allocation fails.
 *
 * A list of 48 rows holding two Arduino Strings each, rebuilt on every frame,
 * is close to the worst thing you could do to such a heap: ~96 long-lived
 * allocations released and re-made 27 times a second, interleaved with every
 * transient String the row builders create. Fixed buffers make the whole
 * structure a flat, statically sized member. It allocates nothing, ever, so it
 * cannot fragment anything and there is nothing for a caller to release.
 *
 * The cost is a cap on text length, which is the right trade here: these
 * values are drawn into a ~140px column and were being truncated anyway.
 */
class RowList {
public:
    static constexpr uint8_t MAX_ROWS = 48;
    static constexpr uint8_t LABEL_MAX = 18;   // ~90px at font 1
    static constexpr uint8_t VALUE_MAX = 34;   // ~200px at font 1
    static constexpr int16_t PAD_X = 6;
    static constexpr int16_t PAD_Y = 6;
    static constexpr int16_t SCROLLBAR_W = 6;

    enum class Kind : uint8_t { Section, Text, Meter, Action };

    struct Row {
        Kind kind = Kind::Text;
        char label[LABEL_MAX] = {0};
        char value[VALUE_MAX] = {0};
        uint16_t valueColor = 0;
        uint8_t meterPct = 0;
        uint16_t meterColor = 0;
        int16_t height = 16;
    };

    /** Discard the current contents. O(1) -- no deallocation to do. */
    void clear() { count_ = 0; }

    void addSection(const char* title);
    void addRow(const char* label, const char* value, uint16_t valueColor = 0, int16_t height = 16);
    void addMeter(uint8_t pct, uint16_t color);
    /** Tappable chip. `actionRect()` reports where it landed after a draw. */
    void addAction(const char* label);

    /* String overloads for callers that must build text dynamically. The
     * String is consumed here and never stored, so nothing outlives the call.*/
    void addRow(const char* label, const String& value, uint16_t valueColor = 0, int16_t height = 16) {
        addRow(label, value.c_str(), valueColor, height);
    }
    void addRow(const String& label, const char* value, uint16_t valueColor = 0, int16_t height = 16) {
        addRow(label.c_str(), value, valueColor, height);
    }
    void addRow(const String& label, const String& value, uint16_t valueColor = 0, int16_t height = 16) {
        addRow(label.c_str(), value.c_str(), valueColor, height);
    }

    uint8_t count() const { return count_; }
    int16_t totalHeight() const;

    /** Clamp `offset` to the scrollable range for a viewport of height h. */
    void clampScroll(int16_t& offset, int16_t viewportH) const;

    /* Draw clipped to `r`. Clipping is not optional: skipping rows that fall
     * wholly outside still lets the row straddling the top edge draw in full,
     * which smears text into whatever chrome sits above. */
    void draw(Ui::Renderer& tft, const Rect& r, int16_t offset);

    /** Where the Action chip was last drawn; w == 0 when it is off screen. */
    const Rect& actionRect() const { return actionRect_; }

private:
    Row* next();
    void drawScrollBar(Ui::Renderer& tft, const Rect& r, int16_t totalH, int16_t offset) const;

    Row rows_[MAX_ROWS];
    uint8_t count_ = 0;
    Rect actionRect_{};
};
