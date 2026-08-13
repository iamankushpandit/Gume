# src/ui

`Ui` is a stateless namespace of themed drawing helpers plus the palette. Game code should draw through these rather than hardcoding colours, so Dark/Light both work.

## Theme

Dark and Light palettes of RGB565 colours — `bg`, `surface`, `panel`, `text`, `muted`, `outline`, `success`, `error`, `warning` — swapped by `Ui::setTheme()`. Helpers read the active palette automatically. `Ui::rgb(r, g, b)` packs a literal colour for icon art, which is the one place fixed colours are expected.

## Widgets

`drawButton()` (3D bevel + shadow), `drawTopBar()` (home icon, gear, clock, Wi-Fi/sync badges), `drawSlider()`, `drawPagerButton()`, `drawLabel()`, `drawWrappedText()`, `drawHopArc()` (semicircular hop animation, used by Number Line).

Tabs have an ordering contract: draw all `drawTab()` calls **first**, then `drawTabBaseline()` — the baseline paints over the inactive tabs' bottom edge to produce the browser-tab look.

Shapes and icons: `drawTriangleShape()`, `drawStarShape()`, `drawGearIcon()`, `drawHomeIcon()`.

Badges: `drawSyncBadge()` (NTP state), `drawWifiBadge()` (bars derived from RSSI).

`Rect` is defined here — `{x, y, w, h}` with `contains(px, py, pad)`. Pass `TOUCH_HIT_SLOP` as the pad for touch targets.

## Country and state artwork

`drawCountryImage()`, `drawCountryImageCentred()`, `drawCountryImageTinted()`, `drawCountryImageScaled()` render flags and outlines from the `map-n-flag` library (supplied by the local `file://` path in `lib_deps`).

Images are 4-bit indexed data in flash, decoded one row at a time through `mnf_row_rgb565()` into a ~200-byte stack buffer and pushed with `pushPixels()` — so a full-screen flag costs no heap and no framebuffer. Keep that streaming pattern if you add new image helpers; buffering a whole image would not fit in RAM.

Note the byte-swap correction applied before `pushPixels()`; dropping it produces colour-garbled output.
