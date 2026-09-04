# Render audit

One entry per screen, written **before** that screen is converted to
`renderStatic()` / `renderDynamic()`. The conversion is mechanical; deciding
what is actually static, and how the old pixels get removed, is not — and it is
a different answer for every screen, because every screen draws different
things.

A screen whose honest answer is "a full repaint is correct here" is a valid
result, recorded with its reason. What is not acceptable is converting a screen
without having asked.

## The method

Three questions, in this order.

**1. What is static?** Painted once and unchanged while the screen is up:
background, top bar, board outlines, grid lines, fixed labels. `Ui::clear()`
belongs in `renderStatic()` and nowhere else.

**2. What is pixel-identical between states?** This is the question that pays,
and it is easy to miss because the code redraws it every time. The launcher's
tile buttons were the whole win there: a tile's rect comes from its *slot* and
its colour from `slot % 3`, so paging changes what is written inside a button
and never the button. Look for chrome that is indexed by position rather than
by content.

**3. How does each changed thing erase its predecessor?** There is no
framebuffer. Today every screen relies on `Ui::clear()` to wipe the previous
frame; the moment it stops, nothing removes the old pixels but the screen
itself.

## The two traps

Both were found converting the launcher. Neither is visible in a build log.

**Shrinking text.** `drawString` with an opaque background colour paints only
behind the glyphs it actually draws. A value that gets *narrower* leaves its
tail on screen — the launcher's `"%u/%u"` pager going `10/12` → `9/12` kept the
trailing character. Every variable-width number needs a fill over its old
extent first, or a fixed width. **Counters that reset to zero are the worst
case**, because that is the largest shrink a screen ever does.

**Things that stop being drawn.** A tile slot with no entry, a panel that was
showing and now is not, a highlight that moved. The old code `break`s or skips;
with no clear behind it, skipping means leaving. Either erase explicitly, or
recognise the transition as a *layout* change and call `markFullDirty()` —
which is often the honest answer, because a thing appearing or disappearing
usually is one.

## Screens

| Screen | State | Notes |
|---|---|---|
| LauncherGame | **converted** | Reference for the pattern. |
| MemoryGame | analysed | Below. |
| *(29 others)* | not started | |

---

## LauncherGame — converted, reference

**Static:** background, header (wordmark, profile name, clock, status badges,
gear, lock). Paging never touches it.

**Pixel-identical between pages:** the tile buttons. Rect from the slot, colour
from `slot % 3`. `Ui::drawButton` pushes *two* full tile areas — a shadow
roundrect and the fill — plus a bevel and an outline; all of it was being
redrawn to change the icon and label inside. On a content change the interior is
erased with one plain `fillRect` inside the border instead.

**Dynamic:** tile contents (icon, title, subtitle), pager buttons, page text.

**Erase:**
- Tile contents — the interior fill covers them; not optional, because glyph
  widths and icon shapes differ between entries.
- Empty slot on a short last page — was `break`, now painted over. Correct
  before only because the screen had just been cleared.
- `"%u/%u"` page text — explicit fill first; it can shrink.

---

## MemoryGame — analysed, not yet converted

A 4×6 board: **24 cards**. Every state change currently redraws all 24, and
each card costs two full-card `fillRoundRect`s plus an outline plus its symbol.

**Static**
- Background, top bar.
- **Card shadow** — `fillRoundRect(r.x + 2, r.y + 3, …, 0xBDF7)`. Constant rect,
  constant colour, for the life of the round.
- **Card outline** — `drawRoundRect(…, TFT_DARKGREY)`. Constant.

Both are indexed by position, not by card state. This is the same win the
launcher had, and it halves the per-card cost.

**Dynamic**
| Element | Changes when |
|---|---|
| One card's face (fill + symbol or dot) | it is revealed, matched, or hidden again |
| `Moves N` | a second card completes a pair |
| `Best N` | a new best is saved (rare) |
| Win panel | the last pair goes down / a new round is dealt |

**The win here is bigger than the launcher's.** Look at what actually changes
per interaction:

- Revealing the first card of a pair: **1 card**.
- Completing or missing a pair: **2 cards**, plus `Moves`.
- The 900 ms resolve timer flipping a wrong pair back: **2 cards**.

So the common case is 1 of 24 cards, and the screen redraws all 24. Repainting
only what changed is roughly an order of magnitude, before counting the shadow
and outline that never needed redrawing at all.

**How to know which cards changed:** keep `drawnState_[MAX_MEMORY_CARDS]` — one
byte per card holding what is currently *on the panel* (back / front / matched)
— and in `renderDynamic()` repaint only cards whose live state differs, then
update it. That is `CinnamonGame::litDrawn_[]`'s pattern. Prefer it over a dirty
bitmask set by `update()`: it is self-correcting, so a state change someone
forgets to flag still paints, where a missed bitmask bit is a card stuck
showing the wrong face.

**Erase**
- **Card face** — self-erasing. `fillRoundRect` with the new colour covers the
  same rect exactly, and the symbol is drawn with that colour as its text
  background. Only the shadow can be skipped, not the fill.
- **`Moves N`** — needs an explicit fill. It grows `9` → `10`, but `newRound()`
  resets it, so `Moves 23` → `Moves 0` leaves `3` behind.
- **`Best N`** — needs a fill. Lower is better here, so it only ever shrinks.
- **Win panel** — nothing erases it when a new round is dealt. Do **not** patch
  this with a fill: dealing a new board reshuffles every card, resets the
  counter and removes the panel, which is a layout change. `newRound()` should
  call `markFullDirty()`.

**Invalidation changes**
- `newRound()` → `markFullDirty()` (currently `markDirty()`). This is what makes
  the panel and both text tails correct without special-casing any of them.
- Everything else stays `markDirty()`.

**Risk:** low. No scrolling, no viewport, no theme override, fixed geometry from
`cardRect()`.
