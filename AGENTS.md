# Agent instructions

This project keeps its guidance in **`CLAUDE.md`** files, which apply to every agent working here regardless of which tool you are — read them as your instructions.

- [`CLAUDE.md`](CLAUDE.md) — start here: build commands, architecture, invariants, and the protocol for working alongside other agents
- [`src/engine/CLAUDE.md`](src/engine/CLAUDE.md) — screen lifecycle, catalogs, progress tracking
- [`src/games/CLAUDE.md`](src/games/CLAUDE.md) — how to add or change a game
- [`src/hal/CLAUDE.md`](src/hal/CLAUDE.md) — hardware, persistence, profiles, watchdog
- [`src/ui/CLAUDE.md`](src/ui/CLAUDE.md) — theming and drawing helpers

**Modularity rule:** If a file is becoming large (as a rule of thumb, `src/main.cpp` > ~400 lines of active logic in one function, or any `.cpp` > ~600 lines total), **refactor it into a more modular form first** before making the requested change. The refactor must not break existing functionality and must land as its own commit before the feature change that prompted it.

**System/UI app orientation rule:** Any screen that is a system utility (Settings, Wi-Fi, SystemInfo, Profiles, Scores, About, or any future app beyond the playable game catalog) must work in **both landscape and portrait**. Read `tft.width()` / `tft.height()` at render time, not `SCREEN_WIDTH`/`SCREEN_HEIGHT`. Use `Ui::drawTab()` for multi-section layouts; the strip adapts when you divide screen width at render time.

## Two standing rules — you should never have to be told these

**1. No AI attribution in commits, ever.** Do not add `Co-Authored-By: Claude`,
`Co-Authored-By: <any AI>`, "Generated with…" footers, or any other trailer or
sign-off naming an AI tool or model. The repository's history records the
author, and that is a human. Check your commit message before you run
`git commit` — this applies to amends, squashes and PR bodies too.

**2. Keep the docs in sync as part of the change, not as a follow-up.** If your
change alters behaviour, structure, dependencies, screens, settings, the game
list or the build, then in the *same* commit you also update whichever of these
it touched:

| You changed | Update |
|---|---|
| Anything user-visible or the feature set | `README.md` — including the games count, the flash/RAM figures from your own `pio run`, and the version |
| Architecture, invariants, layout, build flags, dependencies | `CLAUDE.md` |
| The agent protocol itself | `AGENTS.md` (this file) |
| A subsystem's rules | the `CLAUDE.md` in that directory |
| A new game | `src/games/CLAUDE.md` and the README game table |

Do not wait to be asked, and do not leave it for "a docs pass later". A stale
`README.md` that claims the wrong game count or the wrong flash figure is a
defect, and it is your defect if you shipped the change that made it wrong.

## Responsiveness rule — know what a frame costs

The loop runs at a 20ms budget (`FRAME_BUDGET_MS`) and paces to a deadline: it
sleeps only the remainder, so a slow frame is not punished twice. That budget is
the whole allowance for touch, logic and drawing. Three things have each eaten
most of it at some point in this codebase, and all three look harmless at the
call site:

1. **NVS reads.** `Preferences` is flash-backed — every getter is a hash lookup,
   not a variable read. `screenSaverSeconds()` ran once per loop iteration and
   `gameVisible()` up to ~180 times per launcher repaint. **Anything read more
   than once per screen change gets a write-through RAM mirror in `Board`**:
   the setter updates the mirror and NVS together, so a stale read is not
   possible. Theme, layout, brightness, idle timeout, active profile, game
   visibility and the Wi-Fi credentials all work this way now. Add to that list
   rather than reaching for `prefs_` in a hot path.
2. **Blocking `delay()` inside a getter.** Battery sensing slept 10ms per call
   and `Ui::drawTopBar()` calls two battery getters, so every top bar cost
   ~20ms — a whole frame — before anything was drawn. **No `delay()` in
   anything a render path can reach.** Sample on a cadence and cache.
3. **Rebuilding content that did not change.** System Info reassembled every
   row on every frame while scrolling. Scrolling changes an offset, not
   content. Gate rebuilds behind a stale flag.

Before claiming a screen is fast: System Info's Memory tab shows loop load,
worst work and worst frame, and the watchdog logs a stall past
`STALL_WARN_MS`. A worst frame above ~40ms is a bug, not a heavy screen.

Related: full-screen repaints are ~150KB over SPI and ~30ms of visible blanking,
which is why `Game` has two levels of invalidation. Guard static chrome behind
`needsFullRender()` and repaint only what moved.

## Memory rule — there is no garbage collector

C++ gives you no GC, and FreeRTOS gives you a heap that can never be compacted.
So the thing that kills this device is not the leak you are picturing. There is
not one `new`, `delete`, `malloc` or `free` in this firmware — every screen is a
`static` instance and everything else is stack or a fixed member. **Keep it that
way**, and the classic leak is impossible by construction.

What actually kills it is **fragmentation**: many small allocations of differing
sizes, made and freed over and over, chop the free space into pieces too small
to satisfy a later request. Free heap looks fine right up to the allocation that
fails, hours in. The only visible symptom beforehand is the fragmentation
percentage, which is why System Info shows it.

The offender is almost always Arduino `String`. Every concatenation allocates,
every assignment may reallocate, and a `String` member that is rewritten each
frame is a long-lived block being freed and re-made 27 times a second.

Rules, in the order they bite:

1. **No raw owning allocation.** No `new`/`delete`, no `malloc`/`free`, no
   owning raw pointers. If something genuinely must be dynamic, justify it in a
   comment and give it an owner with a destructor.
2. **No `String` in anything that runs per frame.** Build text with `snprintf`
   into a stack buffer, or store it in a fixed `char[]`. `RowList` (`src/ui/`)
   is the worked example: it was 48 rows × 2 `String`s rebuilt every frame —
   about 96 long-lived allocations churning at frame rate — and is now flat
   char buffers that allocate nothing, ever.
3. **Don't rebuild content on every frame.** Rebuild when the data changed and
   keep a `stale` flag. Scrolling changes an offset, not the content.
4. **A `String` member on a screen is a smell.** A few exist for genuinely
   user-entered text (`ProfileGame::draft_`, `WifiGame::password_`) — that is
   the bar. Anything derived from state belongs in a fixed buffer.
5. **Give back what you borrowed, in `end()`.** Every screen transition goes
   through `KidsPlatformApp::leaveActiveGame()`, which compares free heap
   against the value captured before that screen's `begin()` and logs
   `[heap] '<screen>' left N bytes short` when a screen does not hand it back.
   Watch the serial log after adding a screen.
6. **Prefer fixed-size members over growth.** A statically sized array that is
   occasionally half empty is cheaper and safer here than anything that grows.
   Trading a few hundred bytes of static RAM for zero heap traffic is nearly
   always the right call on this device — `RowList` cost 864 bytes of RAM and
   *saved* 5.5 KB of flash.
7. **Check the numbers before you claim it is fine.** `pio run` reports RAM and
   flash; System Info's Memory tab reports free heap, minimum free heap,
   largest allocatable block and fragmentation. Read them.

Four things that cause real damage here if you skip them:

1. **Branch into your own worktree before you start.** New requirement → `git worktree add ../GUme-<slug> -b feat/<slug>`, then work there. Do **not** `git switch` inside the main checkout: it holds other agents' uncommitted work and switching under them strands it. A worktree also gives you a private `.pio/` build dir.
2. **Multiple agents share this repo.** Read `git status` first; uncommitted work that isn't yours is normal. Stage explicit paths, never `git add -A`. Don't merge to `main` or push unless asked.
3. **`GAME_CATALOG[]` and `CATALOG_KINDS[]` are index-coupled across two files** and a bad merge misaligns them silently — it still compiles and links, and the only symptom is a tile launching the wrong game. Re-verify entry-for-entry after every merge.
4. **One physical board, one serial port** — shared across all branches and worktrees. Don't flash or factory-reset without saying so; it wipes calibration, profiles and scores another agent may be testing against.
5. **Take the board lock before building or flashing.** It lives at `(git rev-parse --git-common-dir)/gume-board.lock` so it's visible from every worktree. If it's held by a live process, wait and poll. If no build or flash process is actually running, the lock is stale — delete it and carry on, and say that you did. Release your own lock even when the build fails.
