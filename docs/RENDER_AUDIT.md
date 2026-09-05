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
| MemoryGame | **converted** | Below. |
| WhackAMoleGame | **converted** | Below. Worst offender found so far. |
| *quiz screens (19)* | analysed | Group A — same layout, one answer. |
| TicTacToe, SlidingPuzzle | analysed | Group B — boards. |
| ColorMix, Microku, Sort, OddOneOut, Maze, Trace, CoinFlip, Dice | analysed | Group C — timer-driven, higher priority. |
| About, Scores, GreWords, States, StateFlag, StateMap | **no change needed** | Group D — they only ever full-repaint. |
| Cinnamon | migrate only | Already partial by hand. |
| Settings, SystemInfo, Profiles, Nearby | analysed | Group E. |
| Wi-Fi | deferred | Group E — fix its blocking scan first. |

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

## MemoryGame — converted

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


---

## WhackAMoleGame — converted

**The worst case found so far, and the first one that is not paced by the
player.** The grid is `GRID * GRID` where `GRID = 9` — **81 cells** of 20x20 —
and every repaint redraws all of them: 81 `fillRect`s, 81 `drawRect`s, four
strings and a full clear.

What actually changes:

| Event | Cells that change |
|---|---|
| A mole spawns | **2** — the old cell loses its smile, the new one gains it |
| A hit or a miss | 1–2, plus the counters |
| The flash timer expiring | **1** |

So one or two of eighty-one. That is a 40–80x over-draw.

**And it repaints on timers, not on touches.** Memory only redraws when the
player does something; this screen has three independent clocks — the spawn
timer, the mole's expiry, and `flashUntil_` — each calling `markDirty()`. So the
full-screen cost is paid several times a second for the whole game, which is why
this one will feel worst on the 480x320 panel: ~60ms of blanking, repeatedly,
while the player is trying to react to a mole.

**Static**
- Background, top bar.
- **All 81 cell outlines.** `drawRect(r.x, r.y, r.w, r.h, Ui::outline())` —
  position-indexed, constant colour. Better than Memory's case: the interior
  fill is already **inset by one pixel** (`fillRect(r.x + 1, r.y + 1, r.w - 2,
  r.h - 2, …)`), so a cell repaint cannot touch its own outline. The grid can be
  drawn once and never again.

**Dynamic**
| Element | Changes when |
|---|---|
| A cell's interior (fill + smile) | a mole appears or leaves it, or it flashes hit/miss |
| `Score N` | a hit |
| `Level N Miss N/10` | a hit or a miss |
| `Best N` | a new best |
| `Speed Nms` | the level changes |
| Game-over panel | the tenth miss / a restart |

**Tracking:** `drawnCell_[81]`, one byte encoding what is painted there — fill
kind (normal / hit / miss) plus whether the smile is on it. Repaint only where
it disagrees with live state. Same reasoning as Memory's `drawnFace_[]`.

**Erase**
- **Cell interior** — self-erasing. The inset `fillRect` covers the whole
  interior including any smile that was on it, so a cell always repaints from a
  clean fill and the smile goes back on top.
- **The two left-aligned strings** (`Score`, `Level … Miss …`) — both shrink.
  `Score` resets to 0 on restart; `Miss 10/10` -> `Miss 0/10` loses a character.
  Fill first.
- **The two right-aligned strings** (`Best`, `Speed`) — these are `TR_DATUM`,
  and that is a trap of its own: a right-aligned string grows **leftwards**, so
  when it shrinks the stale characters are left at the LEFT end, not the right.
  `Speed 1000ms` -> `Speed 900ms` is the normal case, because the mole gets
  faster as the level rises. The erase rect has to cover the old extent measured
  leftward from the right margin, not a fixed box anchored at the text origin.
- **Game-over panel** — a thing that stops being drawn. Restart reshuffles
  everything and resets both counters, so it is a layout change: the restart
  path should `markFullDirty()`, exactly as Memory's `newRound()` now does.

**Invalidation changes**
- The restart branch → `markFullDirty()`.
- The three timer paths stay `markDirty()` — they are the whole point of the
  exercise.

**Risk:** low-moderate. No scrolling or viewport, fixed geometry, but 81 cells
means the tracking array is the largest so far and the timer-driven paths make
a missed repaint more visible than on a screen the player is stepping through.
This is the screen where `drawnCell_[]` being self-correcting rather than
flag-driven earns its keep.

---

# The remaining screens

Analysed by class, because the classes are real: nineteen of them are the same
screen with different questions in it, and the answer for one is the answer for
all nineteen.

A third trap turned up doing this, and it is now the one to watch for:
**right-aligned text**. `TR_DATUM` grows leftwards, so when the value shrinks
the stale characters are left at the LEFT end, where an erase rect anchored on
the text origin will not reach them. Nearly every screen below has at least one.

---

## Group A — the quiz screens (19)

`MathGame`, `MultiplicationGame`, `CountingGame`, `FractionGame`, `FlagGame`,
`TimeGame`, `MoneyGame`, `ObjectAddGame`, `FingerCountGame`, `NumberLineGame`,
`PercentCircleGame`, `SequenceGame`, `ShapeColorGame`, `ElementsGame`,
`ColorMixGame` (see also Group C), and the rest of the same shape.

They are all one layout: header counters, a question panel, four answer
buttons, a feedback line.

**Two transitions, and only one of them is a layout change.**

| Transition | What changes |
|---|---|
| **Answering** (`answered_ = true`) | the four buttons recolour (correct green, chosen red) and the feedback line changes. **The question panel does not.** |
| **Next question** | everything. Genuinely full. |

So the conversion is: question panel and its frame in `renderStatic()`, buttons
and feedback line in `renderDynamic()`, and the next-question path calls
`markFullDirty()`.

**This is a smaller win than a board game — perhaps a third of the panel — but
it is the one the player feels most.** Answering is the moment they are waiting
on: tap, and the button should go green now. Today that costs a full wipe of a
question that has not changed.

**Erase notes**

- The buttons are `Ui::drawButton`, which fills its rect opaquely, so a recolour
  self-erases.
- The feedback line alternates between two *different-length* strings, e.g.
  `"Tap the answer"` and `"Green is correct - tap next"`. That is the shrinking
  trap at its worst; it needs a fill across the longest of them.
- Header counters (`Score`, `Streak`, `Best`, `Level`) need the usual fills, and
  on most of these `Best` and `Streak` are `TR_DATUM`.

**Also worth fixing while in there:** several build their text with `String`
concatenation *inside render*. `MathGame` does it six times per repaint,
including `String(left_) + " " + symbol + …`. That is heap churn at repaint
rate, which the memory rule in `CLAUDE.md` is explicitly about. Converting to
`snprintf` pays the frame-rules ratchet down at the same time, as Memory's did.

**Special case, `MathGame` has a live clock.** `formatSeconds(elapsedSeconds())`
sits in the header but only repaints when something else triggers one, so the
displayed time already jumps rather than ticks. Decide that deliberately rather
than inheriting it: if it should tick, that is a per-second `markDirty()` and the
counter block needs its own erase.

---

## Group B — the board games (2)

### TicTacToeGame

Nine cells. Placing a mark changes **one**. The board panel and the four grid
bars are position-indexed constants, so they are static — the same shape as
Memory's card shadows.

- **Static:** background, top bar, Reset button, board panel, grid bars.
- **Dynamic:** the nine marks, the `X n  O n  D n` score line, the red
  game-over outline.
- **Erase:** `drawMark` draws onto the panel colour. A cell being *cleared* on
  reset would need its square filled back — but reset changes every cell anyway,
  so making reset `markFullDirty()` removes the question.
- **Trap:** the game-over `drawRoundRect` in red is a thing that stops being
  drawn; the full repaint on reset covers it.
- The score line is `TR_DATUM`, and `X 10  O 9  D 2` → `X 0  O 0  D 0` shrinks
  hard.

### SlidingPuzzleGame

A move swaps a tile with the blank: **exactly two tiles change**, out of 4, 9 or
16. Everything else on the board is identical.

- **Static:** background, top bar, the "Slide tiles into order" hint.
- **Dynamic:** the tiles, `%ux%u Moves %u`, `Best %u`, the solved panel.
- **Erase:** tiles are `Ui::drawButton` / `fillRoundRect`, opaque and same rect,
  so they self-erase. Track `drawnTile_[]` by tile value.
- **Traps:** `Moves` resets to 0 on a new board; `Best` is `TR_DATUM` and only
  falls; the solved panel disappears. **The board also changes size** (2x2 → 3x3
  on solving), so `setupSolved()` / `shuffle()` must be `markFullDirty()`. It is
  the one screen so far whose static geometry is not constant for the life of
  the screen.

---

## Group C — timer-driven (8)

`ColorMixGame`, `MicrokuGame`, `SortGame`, `OddOneOutGame`, `MazeGame`,
`TraceGame`, `CoinFlipGame`, `DiceGame`.

These repaint **without the player doing anything**, which is what made
Whack-a-Mole the worst case: the cost is paid several times a second for the
length of the game rather than once per tap. Treat this group as higher priority
than Group A despite being smaller, for exactly that reason.

The common pattern is a **flash**: `correctFlash_` / `wrongFlash_` against a
`flashUntil_` deadline, with a panel drawn over the middle of the screen while
it is set. Two consequences:

- The flash panel is a **disappearing thing**. When the timer clears it, nothing
  erases it. This is the most common defect waiting in this group.
- The flash expiring is a *content* change, not a layout one, so it must repaint
  the region the panel covered rather than the screen. In `ColorMixGame` that
  panel sits at `96,96,128,34`, over the two colour swatches and the `+`.

**`CoinFlipGame` and `DiceGame` are animations** and already carry partial
guards — check whether the guard is the base class's or a private flag, and
migrate rather than rewrite.

**`MazeGame` and `TraceGame` draw a persistent trail.** The player's path
accumulates, so they are the best possible case for partial repaint — append
only, nothing ever needs erasing — and should be near the top of the list.

---

## Group D — no change needed (6)

`AboutGame`, `ScoresGame`, `GreWordsGame`, `StatesGame`, `StateFlagGame`,
`StateMapGame`.

**Every one of these has zero `markDirty()` calls.** They only ever
`markFullDirty()`, because they change everything when they change anything: a
new question, a new page, a new tab. A full repaint is the correct and honest
answer here, and converting them would add tracking state for no benefit.

Recorded so nobody audits them twice. They would still need the
`renderStatic()` / `renderDynamic()` signatures if the base `render()` is ever
removed, but that is a mechanical rename, not a redraw change.

---

## Group E — system screens (5)

### SettingsGame

Toggling one row repaints the whole tab. Rows are cheap and this is not
latency-critical, so convert it, but late. The tab strip and baseline are
static, the rows are dynamic. Its invalidation is already more considered than
most — 10 `markDirty` against 10 `markFullDirty`.

### SystemInfoGame — the careful one

It sets a viewport for scrolling and must reset it. Two different changes are
conflated today: **scrolling**, where every row moves and a full repaint is
correct, and **the live values ticking** on four timers, where one row's number
changes. The second is what should become dynamic. Do not split the viewport
across the two halves — set and reset it inside whichever function draws the
scrolling region, or an early return leaks the clip onto the next screen.

### ProfileGame

Its own header, plus a PIN-entry phase that owns the whole panel — which is why
it already returns `false` from `renderChrome()`. Phase changes are layout
changes; within the picker, only the selected row changes. Convert after the
games.

### WifiGame — leave until last

229 lines of render, 30 `markDirty()` sites, five timers, and a ~4.3 second
blocking scan inside `update()` with no `Watchdog::Pause`. **That blocking scan
is a separate, already-diagnosed bug and should be fixed before anyone touches
this screen's rendering.** Recorded as not yet analysed rather than analysed
badly.

### NearbyGame

Small, two timers, and a peer list that changes as beacons come and go. A list
whose entries appear and disappear is the disappearing-thing trap in list form:
rows that vanish need erasing, or the list needs a full repaint whenever its
*length* changes.

---

## Suggested order

1. **Maze, Trace** — append-only trails, nothing to erase, easiest real win.
2. **TicTacToe, SlidingPuzzle** — small boards, 1–2 of n cells, pattern already
   proven by Memory.
3. **The rest of Group C** — timer-driven, so their cost is continuous.
4. **Group A** — nineteen screens, one pattern, mechanical once the first is
   done. Pay off the `String` churn at the same time.
5. **Settings, SystemInfo, Profiles, Nearby.**
6. **Wi-Fi**, after its blocking scan is fixed.
