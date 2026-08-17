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
| 10 | Money | `MoneyGame.cpp` | todo | 491 lines — largest game |
| 11 | Fractions | `FractionGame.cpp` | done | three trays, all width-derived; compare keeps its pair |
| 12 | Maze | `MazeGame.cpp` | done | 12x22px is wider than a 240px panel - cell shrinks to 19 |
| 13 | Sorting | `SortGame.cpp` | done | 3x2 tray, height capped so portrait tiles stay tile-sized |
| 14 | Color Mix | `ColorMixGame.cpp` | done | swatches at w/4; answers 2x2 / stacked |
| 15 | Slide | `SlidingPuzzleGame.cpp` | done | square board, cell size from the space left |
| 16 | Odd One | `OddOneOutGame.cpp` | done | 3x3 tray keeps 3 columns; banner bottom-anchored |
| 17 | Shape Arith | `ObjectAddGame.cpp` | done | trays split the width; object pitch follows the tray |
| 18 | Fingers | `FingerCountGame.cpp` | todo | 8 SCREEN_* refs |
| 19 | Calendar | `SequenceGame.cpp` | done | days/months; mode buttons split width less the score gutter |
| 20 | Number Line | `NumberLineGame.cpp` | done | line spans the panel; 4-up answers pair in portrait |
| 21 | Flags | `FlagGame.cpp` | todo | bitmap art |
| 22 | US States | `StatesGame.cpp` | todo | |
| 23 | Trace | `TraceGame.cpp` | todo | glyph paths, 8 constexpr rects |
| 24 | State Flags | `StateFlagGame.cpp` | todo | bitmap art |
| 25 | State Maps | `StateMapGame.cpp` | todo | bitmap art |
| 26 | Percent | `PercentCircleGame.cpp` | todo | |
| 27 | GRE Words | `GreWordsGame.cpp` | todo | 4 constexpr rects |
| 28 | Dice | `DiceGame.cpp` | done | Roll anchors to the bottom edge; dice size to the width |
| 29 | Coin Flip | `CoinFlipGame.cpp` | done | coin/pips/verdict placed as fractions of the free band |

### System apps (7)

| App | Source | Was responsive? | Status |
|-----|--------|-----------------|--------|
| Profiles | `ProfileGame.cpp` | yes | already `followsLayout` |
| System Info | `SystemInfoGame.cpp` | yes | already `followsLayout` |
| Nearby | `NearbyGame.cpp` | yes | already `followsLayout` |
| Scores | `ScoresGame.cpp` | no | todo |
| Settings | `SettingsGame.cpp` | no | todo |
| Wi-Fi | `WifiGame.cpp` | no | todo — 12 SCREEN_* refs, worst offender |
| About | `AboutGame.cpp` | no | todo |

### Shared plumbing

| Item | Status | Notes |
|------|--------|-------|
| `src/ui/GameLayout.{h,cpp}` | done | responsive primitives |
| Per-frame rotation re-evaluation | done | rotation is only applied on screen transitions today, so toggling Layout inside Settings does not re-rotate until you leave and come back |
| README "Layout of the code" block | done | `check_docs.py` fails if `GameLayout.cpp` is not listed |
| Flash/RAM figures in README + CLAUDE.md | todo | refresh from a real `pio run` |

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
