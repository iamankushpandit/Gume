#include "RowList.h"

namespace {

void drawMeterBar(TFT_eSPI& tft, const Rect& r, uint8_t pct, uint16_t fill) {
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

void RowList::addAction(const char* label) {
    Row* row = next();
    if (row == nullptr) return;
    row->kind = Kind::Action;
    copyField(row->label, LABEL_MAX, label);
    row->valueColor = Ui::text();
    row->height = 22;
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

void RowList::drawScrollBar(TFT_eSPI& tft, const Rect& r, int16_t totalH, int16_t offset) const {
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

void RowList::draw(TFT_eSPI& tft, const Rect& r, int16_t offset) {
    const int16_t totalH = totalHeight();
    const bool needsScrollBar = totalH > r.h;
    const int16_t rightPad = needsScrollBar
        ? static_cast<int16_t>(PAD_X + SCROLLBAR_W + 6) : PAD_X;
    const int16_t labelX = static_cast<int16_t>(r.x + PAD_X);
    const int16_t valueX = static_cast<int16_t>(r.x + max<int16_t>(92, r.w / 2));
    const int16_t right = static_cast<int16_t>(r.x + r.w - rightPad);
    int16_t y = static_cast<int16_t>(r.y + PAD_Y - offset);

    tft.fillRect(r.x, r.y, r.w, r.h, Ui::surface());
    actionRect_ = Rect{};

    /* vpDatum = false keeps the coordinates below absolute, so the layout
     * maths is unchanged and only the clipping is added. */
    tft.setViewport(r.x, r.y, r.w, r.h, false);

    for (uint8_t i = 0; i < count_; ++i) {
        const Row& row = rows_[i];
        if (y + row.height < r.y) {
            y = static_cast<int16_t>(y + row.height);
            continue;
        }
        if (y > r.y + r.h) break;   // clipped anyway, but stop the work

        if (row.kind == Kind::Section) {
            tft.setTextDatum(TL_DATUM);
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.drawString(row.label, labelX, y, 2);
            tft.drawFastHLine(static_cast<int16_t>(labelX + 54),
                              static_cast<int16_t>(y + 7),
                              static_cast<int16_t>(max<int16_t>(10, right - labelX - 56)),
                              Ui::outline());
        } else if (row.kind == Kind::Text) {
            tft.setTextDatum(TL_DATUM);
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.drawString(row.label, labelX, y, 1);
            tft.setTextColor(row.valueColor, Ui::surface());
            tft.drawString(Ui::fitted(tft, row.value, static_cast<int16_t>(right - valueX), 1),
                           valueX, y, 1);
        } else if (row.kind == Kind::Action) {
            const Rect chip{labelX, y, static_cast<int16_t>(min<int16_t>(150, right - labelX)), 18};
            Ui::drawButton(tft, chip, row.label, Ui::panel(), Ui::outline(), Ui::text(), false, 1);
            actionRect_ = chip;
        } else {
            drawMeterBar(tft, Rect{labelX, y, static_cast<int16_t>(right - labelX), 8},
                         row.meterPct, row.meterColor);
        }
        y = static_cast<int16_t>(y + row.height);
    }

    drawScrollBar(tft, r, totalH, offset);
    tft.resetViewport();
}
