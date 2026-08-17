# Portrait rollout — working tracker

**Purpose.** Every screen in this firmware must lay out correctly in *both*
orientations: landscape 320x240 (`CYD_SCREEN_ROTATION = 3`) and portrait
240x320 (`CYD_PORTRAIT_ROTATION = 0`).

This file exists so the work survives a handoff. It is a **working document**,
not product documentation — delete it in the commit that finishes the rollout.

## If you are picking this up cold

1. Read the "How a screen gets converted" section below. The convention is
   already set; do not invent a second one.
2. Find the first screen in the table below that is not `done`.
3. Convert it, tick it, commit it. One commit per batch of related screens.
4. `pio run` and `python tools/check_docs.py` must both be clean before the
   final commit of the branch, and the flash/RAM figures in `README.md` and
   `CLAUDE.md` must be refreshed from your own build.

## The problem this is solving

The *switching* mechanism already existed and worked before this branch:
`AppDefinition::followsLayout` plus `KidsPlatformApp::rotationForActiveScreen()`
(`src/engine/AppRuntime.cpp`) already honoured the owner's Layout setting, and
Settings already had the toggle.

What did not exist was the **geometry**. All 30 playable games were
`followsLayout = false` and were authored against a hardcoded 320x240 canvas —
`constexpr Rect BOARD_RECT{76, 68, 168, 168}` in Tic-Tac-Toe, `18 + col * 152`
in Math, `(SCREEN_WIDTH - MAZE_COLS * CELL) / 2` in Maze. Flipping the flag
alone clips every one of them. Four system apps (Settings, Wi-Fi, Scores,
About) were also still `followsLayout = false` despite `CLAUDE.md` requiring
system apps to be responsive.

## How a screen gets converted

**The rule: no screen reads `SCREEN_WIDTH` or `SCREEN_HEIGHT` at render time.**
Those two constants describe the landscape panel and nothing else. Read
`tft.width()` / `tft.height()`, or take them from `Ui::Frame`.

1. Add `#include "ui/GameLayout.h"`.
2. Replace hardcoded rects with functions that take the frame:
   `Rect answerRect(uint8_t i) const` becomes
   `Rect answerRect(const Ui::Frame& f, uint8_t i) const`.
   Layout helpers stay `const` and stay pure — they are also what
   `tools/gen_screens.py` mirrors, so keep them readable.
3. Build the frame once per `render()` / `update()` with `Ui::frame(tft)`.
   It is four integer reads; do not cache it in a member, because a member
   goes stale the moment the owner flips the Layout switch.
4. Flip the registry entry to `followsLayout = true` in
   `src/engine/AppRegistry.cpp`.
5. Look at it in both orientations before ticking the row.

Prefer **one formula that serves both orientations** over a landscape branch
and a portrait branch. Flash is the binding constraint here (see the budget
note at the bottom), and two hand-tuned layouts per screen across 34 screens is
how you overflow it. `Ui::gridCell`, `Ui::squareIn` and `Ui::Stack` exist so the
common shapes cost one copy of the arithmetic rather than 34.

Where a game is genuinely fixed-aspect (Maze's 22px cells, Trace's glyph
paths), the right answer is usually to keep the play area square, centre it in
the larger of the two axes, and re-band the chrome around it rather than
distorting the play field.

## Status

Legend: `todo` / `wip` / `done`

### Playable games (30)

| # | Game | Source | Status | Notes |
|---|------|--------|--------|-------|
| 0 | Tic-Tac-Toe | `TicTacToeGame.cpp` | done | square field: largest centred square under the header |
| 1 | Memory | `MemoryGame.cpp` | done | already fitted its grid - just fed the live panel |
| 2 | Math | `MathGame.cpp` | done | prompt + answer grid: Ui::answerColumns picks 2x2 or 1x4 |
| 3 | Multiply | `MultiplicationGame.cpp` | done | same grid as Math |
| 4 | Time | `TimeGame.cpp` | done | clock centred on the panel; answers 2x2 / stacked |
| 5 | Whack | `WhackAMoleGame.cpp` | done | 9x9 field sizes to whichever axis runs out first |
| 6 | Cinnamon | `CinnamonGame.cpp` | done | pads via gridCell; now honours requestRender for rotation |
| 7 | Microku | `MicrokuGame.cpp` | done | grid sized to free space - 3x3 no longer runs under the pad |
| 8 | Shapes | `ShapeColorGame.cpp` | done | two columns split the width; rows spread down the height |
| 9 | Counting | `CountingGame.cpp` | done | dots per row derived from panel width; answers 4-up / 2x2 |
| 10 | Money | `MoneyGame.cpp` | done | four modes share named bands; answers stay 2x2, tray keeps its coin size |
| 11 | Fractions | `FractionGame.cpp` | done | three trays, all width-derived; compare keeps its pair |
| 12 | Maze | `MazeGame.cpp` | done | 12x22px is wider than a 240px panel - cell shrinks to 19 |
| 13 | Sorting | `SortGame.cpp` | done | 3x2 tray, height capped so portrait tiles stay tile-sized |
| 14 | Color Mix | `ColorMixGame.cpp` | done | swatches at w/4; answers 2x2 / stacked |
| 15 | Slide | `SlidingPuzzleGame.cpp` | done | square board, cell size from the space left |
| 16 | Odd One | `OddOneOutGame.cpp` | done | 3x3 tray keeps 3 columns; banner bottom-anchored |
| 17 | Shape Arith | `ObjectAddGame.cpp` | done | trays split the width; object pitch follows the tray |
| 18 | Fingers | `FingerCountGame.cpp` | done | hand pitch/width derived; palms hang off the answer row |
| 19 | Calendar | `SequenceGame.cpp` | done | days/months; mode buttons split width less the score gutter |
| 20 | Number Line | `NumberLineGame.cpp` | done | line spans the panel; 4-up answers pair in portrait |
| 21 | Flags | `FlagGame.cpp` | done | picture-quiz family: centred art, choices stack in portrait |
| 22 | US States | `StatesGame.cpp` | done | picture-quiz family: centred art, choices stack in portrait |
| 23 | Trace | `TraceGame.cpp` | done | strokes are normalised 0-200 design coords; box keeps its 160:132 aspect and grows with the panel |
| 24 | State Flags | `StateFlagGame.cpp` | done | picture-quiz family: centred art, choices stack in portrait |
| 25 | State Maps | `StateMapGame.cpp` | done | picture-quiz family: centred art, choices stack in portrait |
| 26 | Percent | `PercentCircleGame.cpp` | done | stacked portrait branch as measured |
| 27 | GRE Words | `GreWordsGame.cpp` | done | card fills to the study row; the three buttons share the width |
| 28 | Dice | `DiceGame.cpp` | done | Roll anchors to the bottom edge; dice size to the width |
| 29 | Coin Flip | `CoinFlipGame.cpp` | done | coin/pips/verdict placed as fractions of the free band |

### System apps (7)

| App | Source | Was responsive? | Status |
|-----|--------|-----------------|--------|
| Profiles | `ProfileGame.cpp` | yes | already `followsLayout` |
| System Info | `SystemInfoGame.cpp` | yes | already `followsLayout` |
| Nearby | `NearbyGame.cpp` | yes | already `followsLayout` |
| Scores | `ScoresGame.cpp` | no | done — rows/page derived from height; pager bottom-anchored; switch takes its own row when narrow |
| Settings | `SettingsGame.cpp` | no | done — paired rows via gridCell; slider bottom-anchored |
| Wi-Fi | `WifiGame.cpp` | no | done — every rect literal became a named frame helper, so update() and render() share one layout |
| About | `AboutGame.cpp` | no | done — content flows to the panel; games/page and page count derived |

### Shared plumbing

| Item | Status | Notes |
|------|--------|-------|
| `src/ui/GameLayout.{h,cpp}` | done | responsive primitives |
| Per-frame rotation re-evaluation | done | rotation is only applied on screen transitions today, so toggling Layout inside Settings does not re-rotate until you leave and come back |
| README "Layout of the code" block | done | `check_docs.py` fails if `GameLayout.cpp` is not listed |
| Flash/RAM figures in README + CLAUDE.md | done | 2,331,865 / 74.1% as of the GRE Words commit; refresh again when the last 8 screens land |

## Budget

Baseline before this branch, from a real `pio run`: **2,323,933 / 3,145,728
flash (73.9%)**,
71,980 / 327,680 RAM (22.0%).

Record the figure after each batch. If flash climbs past ~90% stop and
consolidate geometry into `GameLayout` rather than pushing on.

| Point | Flash | % | RAM |
|-------|-------|---|-----|
| baseline | 2,323,933 | 73.9% | 71,980 |
| shared plumbing | 2,324,041 | 73.9% | 71,980 |
| +4 games (ttt, math, dice, coinflip) | 2,325,761 | 73.9% | 71,980 |
| +3 games (multiply, counting, oddone) | 2,326,613 | 74.0% | 71,980 |
| +3 games (shapes, colormix, sequence) | 2,327,553 | 74.0% | 71,980 |
| +3 games (memory, slide, sort) | 2,328,029 | 74.0% | 71,980 |
| +3 games (whack, maze, cinnamon) | 2,328,857 | 74.0% | 71,980 |
| +3 games (time, fractions, numberline) | 2,329,529 | 74.1% | 71,980 |
| +2 games (shapearith, microku) | 2,330,101 | 74.1% | 71,980 |
| +4 games (flags, states, stateflags, statemaps) | 2,331,305 | 74.1% | 71,980 |
| +1 game (grewords) | 2,331,865 | 74.1% | 71,980 |


## Measurements already taken for the screens still `todo`

Two of the four remaining games were analysed but not converted. Recording
the numbers so the work is not repeated.

### Percent (`PercentCircleGame`) — needs a genuine portrait branch

This is the only screen in the catalog where one formula will not serve both
orientations, so do not force it. It is laid out as two columns:

- circle: `CIRCLE_CX = 96`, `CIRCLE_CY = 130`, `CIRCLE_RADIUS = 62`
- controls: everything from `x = 164` rightwards — `optionRect` is
  `{164 + col*76, 96 + row*44, 70, 38}`, and the stepper row is
  minus/value/plus/ok at x = 164/200/240/276, y = 152

At 240px wide the right-hand column has nowhere to go: `164 + 76 + 70 = 310`.
Narrowing it does not help either, because the stepper alone needs ~146px.

The shape that works is **side by side in landscape, stacked in portrait**:

```
Rect controlsRect(const Ui::Frame& f) const {
    if (f.tall()) {      // circle on top, controls beneath it
        return Rect{10, CIRCLE_CY + CIRCLE_RADIUS + 16, f.w - 20, 88};
    }
    return Rect{164, TOP_BAR_HEIGHT + 66, f.w - 174, 88};
}
```

Then `optionRect` is `Ui::gridCell(controlsRect(f), 2, 2, i, 6)` — which
gives 70x41 cells in landscape, essentially the authored 70x38 — and the
stepper is a 4-wide `gridCell` over
`{controls.x, controls.y + 56, controls.w, 34}`, which lands on y=152 in
landscape. The circle centre becomes `f.tall() ? f.cx() : 96`; note
`drawCircle()` and `fillSlice()` both read `CIRCLE_CX` directly and will
need the frame threaded through them.

Also in that file: `fillRect(0, 48, ...)` and `fillRect(0, 222, ...)` are the
two repaint strips (top prompt, bottom score) — the second should be
`f.h - 18`, and the score baseline `f.h - 14`.

### Fingers (`FingerCountGame`) — measured

Ten fingers on two hands, `FINGER_PITCH = 26`, `FINGER_W = 22`,
`LEFT_X0 = 22`, `RIGHT_X0 = 172`. Each hand spans `4*pitch + width = 126px`,
so two hands plus a gap need ~268px — the right hand currently runs to x=298
and would fall off a 240px panel.

Derive the pitch instead. With a 10px margin either side and a 16px gap
between the hands the budget is `8*pitch + 44 + 36 <= f.w`, giving pitch 26
in landscape (unchanged) and 20 in portrait; take `FINGER_W = pitch - 4`.
Centre the pair: `handSpan = 4*pitch + width`, left `x0 = f.cx() - 8 - span`,
right `x0 = f.cx() + 8`. That reproduces x=26/168 in landscape against the
authored 22/172.

`PALM_TOP = 152` / `PALM_BOTTOM = 182` should hang off the answer row rather
than off y=240: `palmBottom = answerBand(f).y - 14`, `palmTop = palmBottom - 30`
reproduces 182/152 exactly in landscape. The four answer buttons are
`{12 + i*76, 196, 68, 34}` — 304px in a row, so pair them in portrait the way
Counting and Number Line already do.

### Still unmeasured

`MoneyGame` (the largest game in the tree at 491 lines) and `TraceGame`
(whose stroke data lives in `TraceGlyphData.cpp` — check whether those
coordinates are normalised or in panel pixels **before** scaling anything).

### The four system apps

Scores, Settings, Wi-Fi and About are all still `followsLayout = false`.
`WifiGame` is the worst of them at 12 `SCREEN_*` references. `CLAUDE.md`
already requires these to be responsive, so this is pre-existing debt that
this branch was meant to clear, not new work.
