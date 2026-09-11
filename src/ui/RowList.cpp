#include "RowList.h"

namespace {

void drawMeterBar(Ui::Renderer& tft, const Rect& r, uint8_t pct, uint16_t fill) {
    if (pct > 100) pct = 100;
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 3, Ui::panel());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 3, Ui::outline());
    const int16_t innerW = static_cast<int16_t>(max<int16_t>(0, r.w - 2));
    const int16_t fillW = static_cast<int16_t>((innerW * pct) / 100);
    if (fillW > 0) {
        tft.fillRoundRect(static_cast<int16_t>(r.x + 1), static_cast<int16_t>(r.y + 1),
                          fillW, static_cast<int16_t>(r.h - 2), 2, fill);
    }
}

/* Truncating copy. snprintf already bounds the write; this just makes the
 * truncation explicit so an over-long value is never silently half-shown. */
void copyField(char* dst, size_t cap, const char* src) {
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

/* FNV-1a. A collision leaves one row stale until it next changes, which at
 * 32 bits is not a thing that will be seen. */
constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

uint32_t mix(uint32_t h, uint32_t v) {
    for (uint8_t i = 0; i < 4; ++i) {
        h = (h ^ ((v >> (i * 8)) & 0xFFu)) * FNV_PRIME;
    }
    return h;
}

uint32_t mixText(uint32_t h, const char* s) {
    for (; *s != '\0'; ++s) {
        h = (h ^ static_cast<uint8_t>(*s)) * FNV_PRIME;
    }
    return (h ^ 0xFFu) * FNV_PRIME;   // terminator, so "ab"+"c" != "a"+"bc"
}

/* Everything a row draws. Its position is the layout's business, not this. */
uint32_t rowHash(const RowList::Row& row) {
    uint32_t h = mix(FNV_OFFSET, static_cast<uint32_t>(row.kind));
    h = mix(h, static_cast<uint32_t>(static_cast<uint8_t>(row.actionId)));
    h = mixText(h, row.label);
    h = mixText(h, row.value);
    h = mix(h, row.valueColor);
    h = mix(h, row.meterPct);
    h = mix(h, row.meterColor);
    return h;
}

}   // namespace

RowList::Row* RowList::next() {
    if (count_ >= MAX_ROWS) {
        return nullptr;
    }
    Row* row = &rows_[count_++];
    *row = Row{};   // reset in place; no allocation involved
    return row;
}

void RowList::addSection(const char* title) {
    Row* row = next();
    if (row == nullptr) return;
    row->kind = Kind::Section;
    copyField(row->label, LABEL_MAX, title);
    row->valueColor = Ui::muted();
    row->height = 18;
}

void RowList::addRow(const char* label, const char* value, uint16_t valueColor, int16_t height) {
    Row* row = next();
    if (row == nullptr) return;
    row->kind = Kind::Text;
    copyField(row->label, LABEL_MAX, label);
    copyField(row->value, VALUE_MAX, value);
    row->valueColor = valueColor == 0 ? Ui::text() : valueColor;
    row->height = height;
}

void RowList::addMeter(uint8_t pct, uint16_t color) {
    Row* row = next();
    if (row == nullptr) return;
    row->kind = Kind::Meter;
    row->valueColor = Ui::text();
    row->meterPct = pct;
    row->meterColor = color;
    row->height = 12;
}

void RowList::addAction(const char* label, int8_t id) {
    Row* row = next();
    if (row == nullptr) return;
    row->kind = Kind::Action;
    copyField(row->label, LABEL_MAX, label);
    row->valueColor = Ui::text();
    row->actionId = id;
    row->height = 22;
}

int8_t RowList::actionAt(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < actionHitCount_; ++i) {
        if (actionHits_[i].rect.contains(x, y, TOUCH_HIT_SLOP)) {
            return actionHits_[i].id;
        }
    }
    return -1;
}

int16_t RowList::totalHeight() const {
    int16_t total = static_cast<int16_t>(PAD_Y * 2);
    for (uint8_t i = 0; i < count_; ++i) {
        total = static_cast<int16_t>(total + rows_[i].height);
    }
    return total;
}

void RowList::clampScroll(int16_t& offset, int16_t viewportH) const {
    const int16_t maxScroll = static_cast<int16_t>(max<int16_t>(0, totalHeight() - viewportH));
    if (offset < 0) offset = 0;
    if (offset > maxScroll) offset = maxScroll;
}

void RowList::drawScrollBar(Ui::Renderer& tft, const Rect& r, int16_t totalH, int16_t offset) const {
    if (totalH <= r.h) return;
    const int16_t trackX = static_cast<int16_t>(r.x + r.w - SCROLLBAR_W - 2);
    const int16_t trackY = static_cast<int16_t>(r.y + 3);
    const int16_t trackH = static_cast<int16_t>(r.h - 6);
    tft.fillRoundRect(trackX, trackY, SCROLLBAR_W, trackH, 3, Ui::panel());
    tft.drawRoundRect(trackX, trackY, SCROLLBAR_W, trackH, 3, Ui::outline());

    const int16_t thumbH = static_cast<int16_t>(max<int16_t>(18, (trackH * r.h) / totalH));
    const int16_t maxScroll = static_cast<int16_t>(totalH - r.h);
    const int16_t travel = static_cast<int16_t>(max<int16_t>(1, trackH - thumbH));
    const int16_t thumbY = static_cast<int16_t>(trackY +
        (static_cast<int32_t>(offset) * travel) / max<int16_t>(1, maxScroll));
    tft.fillRoundRect(static_cast<int16_t>(trackX + 1), static_cast<int16_t>(thumbY + 1),
                      static_cast<int16_t>(SCROLLBAR_W - 2), static_cast<int16_t>(thumbH - 2),
                      2, Ui::rgb(88, 164, 224));
}

uint32_t RowList::layoutHash() const {
    uint32_t h = mix(FNV_OFFSET, count_);
    for (uint8_t i = 0; i < count_; ++i) {
        h = mix(h, static_cast<uint32_t>(rows_[i].kind));
        h = mix(h, static_cast<uint32_t>(static_cast<uint16_t>(rows_[i].height)));
    }
    return h;
}

void RowList::draw(Ui::Renderer& tft, const Rect& r, int16_t offset) {
    paint(tft, r, offset, true);
}

void RowList::drawChanged(Ui::Renderer& tft, const Rect& r, int16_t offset) {
    const bool sameFrame = drawnValid_ && offset == drawnOffset_ &&
        r.x == drawnRect_.x && r.y == drawnRect_.y &&
        r.w == drawnRect_.w && r.h == drawnRect_.h &&
        layoutHash() == drawnLayout_;
    paint(tft, r, offset, !sameFrame);
}

void RowList::paint(Ui::Renderer& tft, const Rect& r, int16_t offset, bool full) {
    const int16_t totalH = totalHeight();
    const bool needsScrollBar = totalH > r.h;
    const int16_t rightPad = needsScrollBar
        ? static_cast<int16_t>(PAD_X + SCROLLBAR_W + 6) : PAD_X;
    const int16_t labelX = static_cast<int16_t>(r.x + PAD_X);
    const int16_t valueX = static_cast<int16_t>(r.x + max<int16_t>(92, r.w / 2));
    const int16_t right = static_cast<int16_t>(r.x + r.w - rightPad);
    /* A row repainted on its own may clear up to here and no further: the
     * scrollbar track starts at r.x + r.w - SCROLLBAR_W - 2 and is not redrawn
     * on a partial paint. */
    const int16_t stripRight = needsScrollBar
        ? static_cast<int16_t>(r.x + r.w - SCROLLBAR_W - 3)
        : static_cast<int16_t>(r.x + r.w);
    int16_t y = static_cast<int16_t>(r.y + PAD_Y - offset);

    if (full) {
        tft.fillRect(r.x, r.y, r.w, r.h, Ui::surface());
    }
    actionRect_ = Rect{};
    /* Rebuilt from scratch every draw. A chip that scrolled off screen this
     * frame must stop being pressable this frame, or the list accepts touches
     * for rows nobody can see. */
    actionHitCount_ = 0;

    /* vpDatum = false keeps the coordinates below absolute, so the layout
     * maths is unchanged and only the clipping is added. */
    tft.setViewport(r.x, r.y, r.w, r.h, false);

    for (uint8_t i = 0; i < count_; ++i) {
        const Row& row = rows_[i];
        /* Recorded for every row, visible or not. One that changed while
         * scrolled out of view is repainted by the full draw that scrolling
         * to it causes. */
        const uint32_t hash = rowHash(row);
        const bool changed = full || hash != drawnHash_[i];
        drawnHash_[i] = hash;
        if (y + row.height < r.y || y > r.y + r.h) {
            y = static_cast<int16_t>(y + row.height);
            continue;
        }

        if (row.kind == Kind::Section) {
            if (changed) {
                if (!full) {
                    tft.fillRect(r.x, y, static_cast<int16_t>(stripRight - r.x), row.height,
                                 Ui::surface());
                }
                tft.setTextDatum(TL_DATUM);
                tft.setTextColor(Ui::muted(), Ui::surface());
                tft.drawString(row.label, labelX, y, 2);
                tft.drawFastHLine(static_cast<int16_t>(labelX + 54),
                                  static_cast<int16_t>(y + 7),
                                  static_cast<int16_t>(max<int16_t>(10, right - labelX - 56)),
                                  Ui::outline());
            }
        } else if (row.kind == Kind::Text) {
            if (changed) {
                /* Overdrawn, not cleared: the glyphs carry their own
                 * background, so the new text replaces the old in one pass and
                 * only the tail an old, longer string left is erased. Clearing
                 * the strip first would blink the row for every change. */
                tft.setTextDatum(TL_DATUM);
                tft.setTextColor(Ui::muted(), Ui::surface());
                const int16_t labelEnd =
                    static_cast<int16_t>(labelX + tft.drawString(row.label, labelX, y, 1));
                tft.setTextColor(row.valueColor, Ui::surface());
                const int16_t valueEnd = static_cast<int16_t>(valueX + tft.drawString(
                    Ui::fitted(tft, row.value, static_cast<int16_t>(right - valueX), 1),
                    valueX, y, 1));
                if (!full) {
                    if (labelEnd < valueX) {
                        tft.fillRect(labelEnd, y, static_cast<int16_t>(valueX - labelEnd),
                                     row.height, Ui::surface());
                    }
                    if (valueEnd < stripRight) {
                        tft.fillRect(valueEnd, y, static_cast<int16_t>(stripRight - valueEnd),
                                     row.height, Ui::surface());
                    }
                }
            }
        } else if (row.kind == Kind::Action) {
            const Rect chip{labelX, y, static_cast<int16_t>(min<int16_t>(150, right - labelX)), 18};
            /* The button fills its own rect, so a changed label needs no
             * clearing first. The hit rect below is recorded either way. */
            if (changed) {
                Ui::drawButton(tft, chip, row.label, Ui::panel(), Ui::outline(), Ui::text(),
                               false, 1);
            }
            actionRect_ = chip;
            /* Only chips that landed inside the viewport are recorded. The row
             * loop above already skips rows wholly above or below it, but the
             * one straddling an edge is drawn clipped -- and half a button is
             * not a button you can aim at. */
            if (actionHitCount_ < MAX_ACTIONS && chip.y >= r.y &&
                chip.y + chip.h <= r.y + r.h) {
                actionHits_[actionHitCount_].rect = chip;
                actionHits_[actionHitCount_].id = row.actionId;
                ++actionHitCount_;
            }
        } else if (changed) {
            drawMeterBar(tft, Rect{labelX, y, static_cast<int16_t>(right - labelX), 8},
                         row.meterPct, row.meterColor);
        }
        y = static_cast<int16_t>(y + row.height);
    }

    /* The scrollbar depends only on the total height and the offset, and a
     * change to either is a full paint. */
    if (full) {
        drawScrollBar(tft, r, totalH, offset);
    }
    tft.resetViewport();

    drawnLayout_ = layoutHash();
    drawnRect_ = r;
    drawnOffset_ = offset;
    drawnValid_ = true;
}
