// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AppRuntime.h"

#include <string.h>
#include "ui/LauncherIcons.h"
#include "ui/ScaledRenderer.h"

/* What goes inside one launcher tile: its icon, title and subtitle, in either
 * orientation. Split out of AppRuntimeLauncher.cpp, which keeps the grid, the
 * paging and the input; this file only decides how one entry is laid out in
 * the rect it is given, and draws over a fill the caller has already put down. */

namespace {
void copyFittedText(Ui::Renderer& tft, const char* source, char* dest, size_t cap,
                    int16_t maxW, uint8_t font) {
    if (cap == 0) {
        return;
    }
    snprintf(dest, cap, "%s", source != nullptr ? source : "");
    while (dest[0] != '\0' && strlen(dest) > 2 && tft.textWidth(dest, font) > maxW) {
        dest[strlen(dest) - 1] = '\0';
    }
}

/* Break `source` at a space into two lines that each fit `maxW`, keeping the
 * first line as long as it can be. False when there is no such break -- a
 * single word too wide for the tile, or text that needs three lines. */
bool splitIntoTwoLines(Ui::Renderer& tft, const char* source, char* first, char* second,
                       size_t cap, int16_t maxW, uint8_t font) {
    if (cap == 0 || source == nullptr) {
        return false;
    }
    snprintf(first, cap, "%s", source);
    for (size_t i = strlen(first); i-- > 1;) {
        if (first[i] != ' ') {
            continue;
        }
        first[i] = '\0';
        if (tft.textWidth(first, font) > maxW) {
            first[i] = ' ';
            continue;
        }
        // The shortest second line there can be; if this misses, all do.
        snprintf(second, cap, "%s", first + i + 1);
        return tft.textWidth(second, font) <= maxW;
    }
    return false;
}
}

void LauncherApp::drawTileContents(Ui::Renderer& tft, const AppDefinition& entry,
                                   const Rect& r, uint16_t fill, bool tall,
                                   uint8_t pageTextScale, bool wrapSubtitles) {
    /* Place the contents as fractions of the tile, and scale them with it.
     *
     * These offsets used to be constants -- icon at r.x + 24, title at
     * r.y + 17 -- which described the old 145x46 tile and nothing else.
     * Once the grid started dividing the real panel, the tiles grew and
     * their contents stayed put in the top-left corner of them, which
     * looked worse than the un-scaled grid had: a big empty box with small
     * writing in it. The fractions below are the old constants divided by
     * the old tile size, so the 2.8-inch board gets what it always got.
     *
     * The text scale is the whole reason this board is here. A larger
     * panel that renders the same small label is no easier for the players
     * this is meant to serve; the tile has to carry bigger writing, not
     * just more space around it. */
    const float sx = static_cast<float>(r.w) / (tall ? 108.0f : 145.0f);
    const float sy = static_cast<float>(r.h) / (tall ? 96.0f : 46.0f);
    const float s = sx < sy ? sx : sy;

    const uint8_t textScale = pageTextScale;

    /* Icons scale by WHOLE numbers only, or not at all.
     *
     * The tile grows by a fraction -- about 1.37 in portrait here -- and
     * feeding that to the icon art was wrong. These are drawn shapes, not
     * bitmaps, but they are built from small integers: 1px strokes, a 17px
     * radius, triangle vertices a dozen pixels from centre. Multiply those
     * by 1.37 and each rounds independently: strokes land on 1px in one
     * place and 2px in another, outlines lose corners, and any lettering
     * inside stays 1x while its box grows. Tic-Tac-Toe came through it
     * intact because its features are coarse -- a 34px plate and 30px
     * strokes have room to absorb a rounding error -- which is exactly why
     * it looked fixed while the detailed icons did not.
     *
     * So: an integer multiple when one fits, otherwise native size, and
     * centred in the tile either way. The same rule the flags follow. A
     * crisp icon in a roomy tile reads better than a smeared one that
     * fills it. */
    const uint8_t iconScale = (s >= 2.0f) ? 2 : 1;
    Ui::ScaledRenderer icon{tft};
    icon.setScale(static_cast<float>(iconScale), static_cast<float>(iconScale));

    char label[24];
    if (tall) {
        const int16_t cxT = static_cast<int16_t>(r.x + r.w / 2);
        const int16_t iconY = static_cast<int16_t>(r.y + r.h * 0.3125f);
        drawLauncherIcon(icon, entry.icon(), r, fill,
                         static_cast<int16_t>(cxT / iconScale),
                         static_cast<int16_t>(iconY / iconScale));
        /* With wrapping on, the title sits one subtitle line higher on
         * EVERY tile, whether its own subtitle wraps or not. A grid
         * implies its cells are alike -- titles at two heights on one page
         * would read as a fault, and would jump as pages turn. */
        const int16_t textW = static_cast<int16_t>(r.w - 8);
        const int16_t subPitch = static_cast<int16_t>(10 * textScale);
        const int16_t subY = static_cast<int16_t>(r.y + r.h * 0.906f);
        const int16_t titleY = static_cast<int16_t>(
            r.y + r.h * 0.708f - (wrapSubtitles ? subPitch : 0));
        /* THE INK FOLLOWS THE FILL, which the theme chose. A fixed
         * white here is how Classic drew white labels on light grey tiles
         * at 1.3:1 and Pocket on pale green at 2.6:1 -- unreadable, and
         * unreadable only on the themes nobody screenshots. */
        tft.setTextColor(Ui::onFill(fill), fill);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(textScale);
        copyFittedText(tft, entry.title(), label, sizeof(label), textW, 2);
        tft.drawString(label, cxT, titleY, 2);
        tft.setTextColor(Ui::onFillSoft(fill), fill);
        char second[sizeof(label)];
        if (!wrapSubtitles) {
            copyFittedText(tft, entry.subtitle(), label, sizeof(label), textW, 1);
            tft.drawString(label, cxT, subY, 1);
        } else if (tft.textWidth(entry.subtitle(), 1) > textW &&
                   splitIntoTwoLines(tft, entry.subtitle(), label, second,
                                     sizeof(label), textW, 1)) {
            tft.drawString(label, cxT, static_cast<int16_t>(subY - subPitch), 1);
            tft.drawString(second, cxT, subY, 1);
        } else {
            /* One line, centred in the two-line block. Cut with a dot if
             * even wrapping could not save it, so a clipped word never
             * passes for a whole one. */
            copyFittedText(tft, entry.subtitle(), label, sizeof(label), textW, 1);
            const size_t kept = strlen(label);
            if (kept > 0 && kept < strlen(entry.subtitle())) {
                label[kept - 1] = '.';
            }
            tft.drawString(label, cxT, static_cast<int16_t>(subY - subPitch / 2), 1);
        }
        tft.setTextSize(1);
    } else {
        /* Everything hangs off the tile's own centre line.
         *
         * The first version kept the old proportions -- icon at 0.478 of
         * the height, title at 0.37, subtitle at 0.74 -- which were fine
         * in a 46px tile and drift visibly in a 69px one: the pair sits
         * low, and the icon a shade above it, so nothing lines up with
         * anything. Measuring the text block from the middle outwards
         * keeps it centred at any tile height, and puts the icon on the
         * same line rather than near it. */
        const int16_t midY = static_cast<int16_t>(r.y + r.h / 2);
        const int16_t iconX = static_cast<int16_t>(r.x + r.w * 0.18f);
        drawLauncherIcon(icon, entry.icon(), r, fill,
                         static_cast<int16_t>(iconX / iconScale),
                         static_cast<int16_t>(midY / iconScale));

        /* Title above the line and subtitle below it, each by half its own
         * height, so the gap between them does not grow with the tile. */
        const int16_t titleH = static_cast<int16_t>(16 * textScale);
        const int16_t subH = static_cast<int16_t>(8 * textScale);
        const int16_t textX = static_cast<int16_t>(r.x + r.w * 0.34f);
        const int16_t textW = static_cast<int16_t>(r.w - (r.x + r.w * 0.34f - r.x) - 8);

        /* THE INK FOLLOWS THE FILL, which the theme chose. A fixed
         * white here is how Classic drew white labels on light grey tiles
         * at 1.3:1 and Pocket on pale green at 2.6:1 -- unreadable, and
         * unreadable only on the themes nobody screenshots. */
        tft.setTextColor(Ui::onFill(fill), fill);
        tft.setTextDatum(ML_DATUM);
        tft.setTextSize(textScale);
        copyFittedText(tft, entry.title(), label, sizeof(label), textW, 2);
        tft.drawString(label, textX, static_cast<int16_t>(midY - subH / 2 - 2), 2);
        tft.setTextColor(Ui::onFillSoft(fill), fill);
        copyFittedText(tft, entry.subtitle(), label, sizeof(label), textW, 1);
        tft.drawString(label, textX, static_cast<int16_t>(midY + titleH / 2 + 2), 1);
        tft.setTextSize(1);
    }
}
