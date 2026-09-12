# src/ui

## LogoMask.{h,cpp} + Ui::drawLogo()

The product mark -- the brain over the wordmark -- as a one-bit silhouette,
**generated** by `tools/gen_logo_mask.py` from `tools/braino-badge.svg`. Edit
the SVG or the script, never the output, and look at `docs/logo-mask.png`
afterwards: a mark that has lost a stroke still compiles and still draws.

One bit rather than a colour depth, because the caller picks the colour --
the screen saver paints it in a shade of the rally colour that moves with
every paddle hit, which an anti-aliased image could not do without knowing
the background. `drawLogo()` paints only the ink, as horizontal runs, so
redrawing it in a new colour is an overdraw of the same shape and needs no
erase. Ask `logoWidth()`/`logoHeight()` for the variant you are drawing rather
than assuming a size.

Six marks are generated, not one: `BADGE` (the whole artwork, 118px, the screen
saver) and `BADGE_MID` (two thirds of it, the lock screen); `WORD` /
`WORD_SMALL` (the wordmark alone, at the heights the product name used to be
drawn as text in a font-4 header and a font-2 row); and `ICON` / `ICON_BIG`
(the brain on its own, for a row that has no room for the name). `Ui::Logo`
picks between them. **The product name is not drawn as text anywhere in the UI**
-- the launcher, Profiles, the lock screen and About all draw the mark.

Every mark but the icon carries a **trade mark sign**, stamped by the generator
at a proportional height rather than drawn into the SVG, so one artwork serves
every size. That is what makes the next paragraph necessary.

**`drawLogo(cx, ...)` places the mark's OPTICAL centre at `cx`, not the middle
of its bitmap.** The sign hangs off the right-hand end, so centring the image
sits the brain and the name visibly left of centre -- which is exactly how the
lock screen looked when it was first drawn. `<NAME>_CENTRE` is emitted beside
the bits (the centre of the ink *without* the sign) and `Ui::logoCentre()`
exposes it, so a caller placing a mark against a left edge passes
`x + logoCentre(which)` rather than `x + logoWidth(which) / 2`.

## Contrast is checked, not judged

`tools/check_contrast.py` measures every pairing the palette can produce
against the WCAG floors: 4.5:1 for body text, 3:1 for large text and for
graphics that must be seen but not read, 1.6:1 for a hairline. Run it after
touching `PALETTES`. It found sixty-five failures the first time, on colours
that had all been chosen by looking at them.

Two things it knows that are not in the table:

- **A card on a ground may be subtle**, because both are drawn with an outline
  round them. The rule is that the *edge* is findable: either the fills differ
  or the outline differs from both.
- **Text on a themed fill takes its ink from that fill** -- `Ui::onFill()` and
  `Ui::onFillSoft()`, restated in the checker so the launcher tiles are
  measured the way they are drawn. If either goes back to being a constant,
  the check keeps passing while the panel stops being readable.

The check is a floor, not a design: passing it does not make a palette good, it
only means nothing in it is unreadable. Judge the look on glass -- and before
that, in `python tools/gen_screens.py --themes`, which renders a representative
set of screens in all nine palettes to `docs/theme-sheets/`. Every defect
reported off the device -- an invisible Home button on Classic, a grey battery
badge on Silver and Pocket, one hard-coded blue for every theme's primary
button -- was visible in those sheets the moment they existed, and invisible
before, because the stills were all Dark.

`Ui::tileFill()` is cycled by `LauncherLayout::tileFillIndex()`, not by the raw
slot: `slot % 3` paints every column one colour on a three-column grid, so
there it steps by row as well and the fills run diagonally. A mock-up that
cycles by slot on such a grid is drawing something the firmware does not.

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

Badges: `drawSyncBadge()` (NTP state), `drawWifiBadge()` (bars derived from RSSI), `drawBatteryBadge()` (the percentage as numerals **inside** the shell, iOS/Android status-bar style, over a bordered two-pixel level gauge along the inside bottom; red shell *and* digits at or below `Board::BATTERY_LOW_PERCENT`, since 11 pixels of fill is not a signal a player reads across a room and a number is. There is no charging bolt and no charging state -- the board has no charge-status line, and 5.10.0 stopped showing a guess. **It is variable width** -- it grows with its digits, widest at `100` -- so callers lay out from `Ui::batteryBadgeWidth()` and never assume a size; `cx` is the centre of the whole badge, terminal nub included), `drawBleBadge()` (the Bluetooth rune).

**Two sizes, and everything in a header is one of them.** `Ui::BADGE_H` (13)
is a status glyph -- the sync dot, the Wi-Fi fan, the Bluetooth rune.
`Ui::CONTROL_H` (18) is something you can tap -- the padlock, the mute speaker,
the gear. Each was chosen per icon before this, so the rune was 17 among 13s
and the gear was 26x24 four pixels from an 18px padlock: a row of icons that do
not share a height reads as a mistake however good each one is alone. The
battery badge is the one exception at 15, because it is a shell holding digits
rather than a glyph, and it says so where it is defined.

`drawSpeakerIcon()` is a **control, not an indicator**, and it always shows the
state it is in -- waves for on, a slash for muted. A toggle that looks the same
either way makes "is it muted?" answerable only by making a noise, which is the
one thing you cannot do in the room where somebody just muted it. The tap is
consumed by the runtime above the active screen, like Home, the gear and Lock,
and it is **not** behind the admin PIN -- see `BrainoApp::toggleMute()` for why.

`drawBleBadge()` has **no "off" variant on purpose.** An icon that is always present but sometimes greyed turns "is it transmitting?" into a question of shade, and that is the one question the badge exists to answer at a glance — so callers draw it only while `BleBeacon::active()`. It is 10x16 centred on the given point; the launcher header has almost no slack in landscape, so position anything near it off measured text widths rather than fixed offsets.

**The top bar is now Home, Lock, MUTE, the title and the status cluster, and
the title paid for the speaker.** Its left edge is derived from
`topBarSpeakerRect()` rather than from the padlock, and it moved from x=62 to
x=86; eight of those pixels came back when the gear shrank to `CONTROL_H`, and
the status cluster is derived from `topBarSettingsRect()` so it followed the
gear rather than staying on an old constant. What is left is about ten
characters at font 2 on a 320px panel, so a long screen title -- "Cinnamon
Says", "Finger Counting" -- is cut, without an ellipsis. **Anything else that
wants space up here has to say what it is taking it from.**

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
