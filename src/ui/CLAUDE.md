# src/ui

`Ui` is a stateless namespace of themed drawing helpers plus the palette. Game code should draw through these rather than hardcoding colours, so Dark/Light both work.

## Renderer

`Renderer.h` is the app-facing drawing interface. It declares the small RGB565 primitive set used by games and UI helpers without including `TFT_eSPI.h`, so host renderers can implement the same surface later. `TftRenderer.h` is the firmware adapter around the real panel driver and is owned by `KidsPlatformApp`; ordinary games get only `Ui::Renderer&` from `AppContext::display()`.

## Theme

Dark and Light palettes of RGB565 colours — `bg`, `surface`, `panel`, `text`, `muted`, `outline`, `success`, `error`, `warning` — swapped by `Ui::setTheme()`. Helpers read the active palette automatically. `Ui::rgb(r, g, b)` packs a literal colour for icon art, which is the one place fixed colours are expected.

## Widgets

`drawButton()` (3D bevel + shadow), `drawTopBar()` (home icon, gear, clock, Wi-Fi/sync badges), `drawSlider()`, `drawPagerButton()`, `drawLabel()`, `drawWrappedText()`, `drawHopArc()` (semicircular hop animation, used by Number Line).

Tabs have an ordering contract: draw all `drawTab()` calls **first**, then `drawTabBaseline()` — the baseline paints over the inactive tabs' bottom edge to produce the browser-tab look.

Shapes and icons: `drawTriangleShape()`, `drawStarShape()`, `drawGearIcon()`, `drawHomeIcon()`.

Badges: `drawSyncBadge()` (NTP state), `drawWifiBadge()` (bars derived from RSSI), `drawBatteryBadge()` (the percentage as numerals **inside** the shell, iOS/Android status-bar style, over a two-pixel level gauge along the inside bottom, plus a bolt inside the shell while charging; red shell *and* digits at or below `Board::BATTERY_LOW_PERCENT`, since 11 pixels of fill is not a signal a child reads across a room and a number is. Takes a `Ui::PowerHint`, not the HAL enum — `Ui::powerHint(board)` is the one place the two meet. **It is variable width** — 22px at `72`, 36px at `100` while charging — so callers lay out from `Ui::batteryBadgeWidth()` and never assume a size; `cx` is the centre of the whole badge, terminal nub included), `drawBleBadge()` (the Bluetooth rune).

`drawBleBadge()` has **no "off" variant on purpose.** An icon that is always present but sometimes greyed turns "is it transmitting?" into a question of shade, and that is the one question the badge exists to answer at a glance — so callers draw it only while `BleBeacon::active()`. It is 10x16 centred on the given point; the launcher header has almost no slack in landscape, so position anything near it off measured text widths rather than fixed offsets.

`drawNotification()` paints a transient strip over the top of whatever header is already there, full width at `TOP_BAR_HEIGHT`, so it works over the top bar and over the launcher's taller header alike. It is painted by the runtime **after** the screen has drawn itself, and the screen is asked for a full repaint when the notification goes away — that repaint is what actually removes it. Nothing accumulates: there is no notification list, because a list nobody clears is furniture.

## RowList

`RowList` is the scrolling label/value widget System Info is built from — sections, text rows, meters and a tappable action chip, with scrollbar and clamping.

It holds **fixed `char` buffers, not `String`**, and that is the whole point. It was 48 rows x 2 Arduino `String`s rebuilt on every frame: ~96 long-lived allocations freed and re-made at frame rate, interleaved with every transient `String` the row builders create — close to the worst thing you can do to a heap that cannot be compacted. It now allocates nothing, ever, so there is nothing for a caller to release and nothing to fragment. The cost is `LABEL_MAX` / `VALUE_MAX` caps, which is the right trade: the value column is ~140px and was truncating anyway.

`draw()` clips to the given rect with `setViewport(..., false)`. That is not optional — skipping rows wholly outside the rect still lets the row straddling the top edge draw in full, which smears text into whatever chrome sits above.

Callers keep their own scroll offset and pass it in, so one list can serve several tabs.

`Rect` is defined here — `{x, y, w, h}` with `contains(px, py, pad)`. Pass `TOUCH_HIT_SLOP` as the pad for touch targets.

## Country and state artwork

`drawCountryImage()`, `drawCountryImageCentred()`, `drawCountryImageTinted()`, `drawCountryImageScaled()` render flags and outlines from the `map-n-flag` library (supplied by the local `file://` path in `lib_deps`).

Images are 4-bit indexed data in flash, decoded one row at a time through `mnf_row_rgb565()` into a ~200-byte stack buffer and pushed with `pushPixels()` — so a full-screen flag costs no heap and no framebuffer. Keep that streaming pattern if you add new image helpers; buffering a whole image would not fit in RAM.

Note the byte-swap correction applied before `pushPixels()`; dropping it produces colour-garbled output.
