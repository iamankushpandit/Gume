# src/ui

`Ui` is a stateless namespace of themed drawing helpers plus the palette. Game code should draw through these rather than hardcoding colours, so that all nine themes work.

## Renderer

`Renderer.h` is the app-facing drawing interface. It declares the small RGB565 primitive set used by games and UI helpers without including `TFT_eSPI.h`, so host renderers can implement the same surface later. `TftRenderer.h` is the firmware adapter around the real panel driver and is owned by `BrainoApp`; ordinary games get only `Ui::Renderer&` from `AppContext::display()`.

## Theme

Nine palettes of RGB565 colours, one row each in `PALETTES` (`Ui.cpp`), swapped into live `COLOR_*` globals by `Ui::setTheme()`. Roles: `bg`, `bar`, `barText`, `surface`, `panel`, `text`, `muted`, `outline`, `success`, `error`, `warning`, three launcher `tile` fills and a corner `radius`. Helpers read the active palette automatically. `Ui::rgb(r, g, b)` packs a literal colour for icon art, which is the one place fixed colours are expected.

Three of those roles were constants until the period themes needed them, and each one had silently made a whole class of theme impossible:

- **`barText`** was `constexpr COLOR_BAR_TEXT = TFT_WHITE`. It is why the Light theme kept a dark bar ("keep dark header both themes") -- a light bar would have had white glyphs on it. Classic needs a white bar with black glyphs, and Paper wanted a cream one.
- **The three tile fills** were literals in `AppRuntimeLauncher.cpp`. Pocket is four shades of green; three bright RGB rectangles on the first screen anyone sees would have wrecked it.
- **`radius`** was a hardcoded `6` in `drawButton`. A rounded Windows 98 button is not a Windows 98 button.

The lesson generalises: when a theme "can't be done", check for a colour that is a constant rather than a role.

**A theme change is `markFullDirty()`, never `markDirty()`.** It repaints the ground and the chrome, not the content on top of them. Settings used `markDirty()` and left the light-theme tab strip sitting above a dark screen -- visible with two themes, and nine make it unmissable.

**Judge palettes on glass, not in `gen_screens.py`.** PIL renders every palette cleanly and cannot show a resistive overlay diffusing a marginal pairing. `muted` on `surface` is where they fail first, because About and System Info draw most of their body text that way.

## Widgets

`drawButton()` (3D bevel + shadow), `drawTopBar()` (home icon, gear, clock, Wi-Fi/sync badges), `drawSlider()`, `drawPagerButton()`, `drawLabel()`, `drawWrappedText()`, `drawHopArc()` (semicircular hop animation, used by Number Line).

Tabs have an ordering contract: draw all `drawTab()` calls **first**, then `drawTabBaseline()` — the baseline paints over the inactive tabs' bottom edge to produce the browser-tab look.

Shapes and icons: `drawTriangleShape()`, `drawStarShape()`, `drawGearIcon()`, `drawHomeIcon()`.

Badges: `drawSyncBadge()` (NTP state), `drawWifiBadge()` (bars derived from RSSI), `drawBatteryBadge()` (the percentage as numerals **inside** the shell, iOS/Android status-bar style, over a bordered two-pixel level gauge along the inside bottom; red shell *and* digits at or below `Board::BATTERY_LOW_PERCENT`, since 11 pixels of fill is not a signal a player reads across a room and a number is. There is no charging bolt and no charging state -- the board has no charge-status line, and 5.10.0 stopped showing a guess. **It is variable width** -- it grows with its digits, widest at `100` -- so callers lay out from `Ui::batteryBadgeWidth()` and never assume a size -- which is also how a board with **no battery sense** gets no badge at all: the width is 0 and the draw is a no-op; `cx` is the centre of the whole badge, terminal nub included), `drawBleBadge()` (the Bluetooth rune).

`drawBleBadge()` has **no "off" variant on purpose.** An icon that is always present but sometimes greyed turns "is it transmitting?" into a question of shade, and that is the one question the badge exists to answer at a glance — so callers draw it only while `BleBeacon::active()`. It is 10x16 centred on the given point; the launcher header has almost no slack in landscape, so position anything near it off measured text widths rather than fixed offsets.

`drawTopBar()` carries Home, Lock, the title and the status cluster, and the bar is full: the cluster is laid out from measured widths off the right edge, so Lock's 18px came out of the title (Home narrowed to 32px, the title moved from x=48 to x=62). `LauncherLayout::topBarHomeRect()` / `topBarLockRect()` / `topBarSettingsRect()` are read by both the drawing here and the runtime's hit testing, so a glyph can never end up somewhere its target is not. `drawLockIcon()` takes its proportions from the rect because the same padlock is drawn at 18px in the bar, 24px on the launcher header and 34px on the lock screen.

`drawNotification()` paints a transient strip over the top of whatever header is already there, full width at `TOP_BAR_HEIGHT`, so it works over the top bar and over the launcher's taller header alike. It is painted by the runtime **after** the screen has drawn itself, and the screen's header is repainted when the notification goes away (`Game::renderChrome()`) — that repaint is what actually removes it. A screen that cannot repaint its chrome alone falls back to a full repaint. Nothing accumulates: there is no notification list, because a list nobody clears is furniture.

## Keypad

`Ui::Keypad` is the on-screen QWERTY keyboard, and it is the ONLY one. There
were two hand-rolled copies before it -- ProfileGame's rename and WifiGame's
password entry -- each with its own grid maths, and a third was about to be
written for naming a Nearby peer.

It is stateless: no draft buffer, no cursor, no dirty flag. The screen owns its
text and decides what a keystroke means; this turns a grid position into a
character and back into a rectangle. `keyRect()` answers both "where do I draw
this key" and "what did the finger land on", so those two cannot disagree --
which is the failure both PIN pads had, drawing DEL and OK below the bottom of
a 240px panel where there was physically nothing to press.

Rows are ragged (10/10/9/7 plus a three-key action row) and centred, because
that is what QWERTY is. **It is anchored to the BOTTOM of the panel**, above a
`bottomReserve` the caller names -- `FOOTER_BUTTON` for a Cancel at
`screenH-30`, `FOOTER_NONE` otherwise. It used to be placed from the top with a
per-orientation fudge of -2 or -22, which was right for exactly the two
320x240-class panels and left the 4-inch board's keyboard floating 84px above
its own button. Anchoring needs no per-panel constant: the slack lands above
the keyboard, where the caller's text field is.

`topY()` is exposed so a caller places that field against the keyboard's real
top edge instead of guessing. Key width is the thing to know before adding a
caller: 26px on a 320px landscape panel, 42px on the 4-inch, and **20px in
240px portrait**, which is the narrowest target in the firmware and is only
usable because `TOUCH_HIT_SLOP` widens it. Do not add an eleventh column.

## RowList

`RowList` is the scrolling label/value widget System Info is built from — sections, text rows, meters and a tappable action chip, with scrollbar and clamping.

It holds **fixed `char` buffers, not `String`**, and that is the whole point. It was 48 rows x 2 Arduino `String`s rebuilt on every frame: ~96 long-lived allocations freed and re-made at frame rate, interleaved with every transient `String` the row builders create — close to the worst thing you can do to a heap that cannot be compacted. It now allocates nothing, ever, so there is nothing for a caller to release and nothing to fragment. The cost is `LABEL_MAX` / `VALUE_MAX` caps, which is the right trade: the value column is ~140px and was truncating anyway.

`draw()` clips to the given rect with `setViewport(..., false)`. That is not optional — skipping rows wholly outside the rect still lets the row straddling the top edge draw in full, which smears text into whatever chrome sits above.

Callers keep their own scroll offset and pass it in, so one list can serve several tabs.

`draw()` wipes the rect and paints every row; `drawChanged()` repaints only the rows whose content changed since the last paint. It keeps a 32-bit hash per row (192 bytes) plus the layout, rect and offset they were drawn at, and falls back to a full paint when any of those differ. Changed text rows are overdrawn, not cleared -- the glyphs carry their background and only the tail of an old, longer string is erased -- so a value changing does not blink its row. **After the screen under the list has been cleared, call `draw()`**: the hashes describe pixels that are gone. Nearby is the caller; it rebuilds from live beacon data, where nearly every rebuild is identical to the last.

`Rect` is defined here — `{x, y, w, h}` with `contains(px, py, pad)`. Pass `TOUCH_HIT_SLOP` as the pad for touch targets.

## Country and state artwork

`drawCountryImage()`, `drawCountryImageCentred()`, `drawCountryImageTinted()`, `drawCountryImageScaled()` render flags and outlines from the `map-n-flag` library (supplied by the local `file://` path in `lib_deps`).

Images are 4-bit indexed data in flash, decoded one row at a time through `mnf_row_rgb565()` into a ~200-byte stack buffer and pushed with `pushPixels()` — so a full-screen flag costs no heap and no framebuffer. Keep that streaming pattern if you add new image helpers; buffering a whole image would not fit in RAM.

Note the byte-swap correction applied before `pushPixels()`; dropping it produces colour-garbled output.
