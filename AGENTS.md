# Agent instructions

This project keeps its guidance in **`CLAUDE.md`** files. They apply to everyone who changes this repository — every agent regardless of which tool you are, and every human contributor too. There is one rulebook, not an agent track and a people track: [CONTRIBUTING.md](CONTRIBUTING.md) covers how work reaches the protected `main` and `dev` branches, and these files cover what the code itself has to honour. Read them as your instructions.

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

**Run `python tools/check_docs.py` before you commit.** It fails if the version,
the game count, the source-tree listing or the build figures have drifted, and
if the About app has started restating facts instead of deriving them. These
rules existed and the docs went stale anyway -- the README shipped claiming 23
games when there were 26, and the source listing missed three files that had
been added. Asking for vigilance does not work at the end of a long change; the
check does. It is not a substitute for reading the prose, only for the parts a
machine can catch.

If your change alters a screen's layout, or adds or removes one, regenerate the
mock-ups in the same commit: `python tools/gen_screens.py`. `docs/screens/` kept
images of the deleted Countries game for two releases, and the Settings picture
showed a grid that no longer existed. A mock-up of a screen that is not there
any more is a worse lie than a missing one.

## The About app is user-facing documentation — keep it true

`AboutGame` is the only documentation most owners will ever read, and the only
one they read *while holding the device*. It is part of the deliverable, not a
credits screen. **Update it in the same commit as the change it describes.**

It was already six games out of date once, because it kept a hand-written list.
The fix, and the standing rule, is to **derive rather than restate**:

| About shows | Derived from |
|---|---|
| Version | `GOODTIME_KIDS_VERSION` |
| Game count and every game name/blurb | `GAME_CATALOG` |
| Board name | `BOARD_NAME` |
| Wi-Fi status | `Board::hasWifiCredentials()` / `isWifiConnected()` |
| Beacon status and advertised name | `BleBeacon::active()` / `configured()` |
| Whether scores are being shared | `BleBeacon::configured().sharesActivity` |

If you are about to type a fact into About that the firmware already knows, read
it from the firmware instead. Anything genuinely static — the credits, the
privacy statements — must be re-read whenever the thing it describes changes.

## No data collection — the rule that outranks the feature

Braino collects nothing about the child using it, and no change may alter
that. It is not a setting that ships switched off; it is what the product is.
Exactly three things leave the device: an NTP time query, one `ip-api.com`
lookup to guess the timezone on first connect, and the opt-in, non-connectable
BLE beacon. **That list is closed.** Do not add analytics, usage counters,
crash reporting, any other HTTP/UDP/DNS request, any dependency that phones
home at runtime, or anything transmitted that carries a child's name, profile
name, score, progress or typing. A fourth outbound flow needs the maintainer's
agreement in an issue *before* the code exists — it is a change to what the
product promises, not a feature to be reviewed on merit.

Storing is not collecting: scores and profiles live in this device's own NVS
and never leave it. The full statement, including what to update if a
transmitted-data change is ever agreed, is in
[CONTRIBUTING.md](CONTRIBUTING.md#no-data-collection).

**A privacy claim that has drifted from the hardware is worse than none at all,
because it is believed.** If you add or change anything that transmits, stores
or shares data, the About radio page and the README Privacy section are part of
that change. Not a follow-up.

About is also a system app, so it follows the orientation rule: lay out against
`tft.width()` / `tft.height()`, never `SCREEN_WIDTH` / `SCREEN_HEIGHT`.

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

## Adding a game or an app — the whole checklist

The old version of this list had five items, all of them code. Everything that
has since been shipped broken or stale was *outside* those five: the README game
table, the screenshot gallery, `docs/screens/`, the changelog. A game that
launches correctly and is invisible in every document describing the product is
not finished.

Work through all four groups. Nothing here is optional for a screen that ships.

### 1. Code — miss one and it fails silently or won't link

1. `src/games/NewGame.{h,cpp}` — subclass `Game`.
2. `src/engine/GameCatalog.cpp` — append an entry. Blurbs render at font 1
   across ~292 px, so keep them under ~46 chars. **`id` is a persisted NVS
   visibility key**: renaming it later resets that game's visibility on every
   existing device.
3. `src/main.cpp` — add to `enum class EntryKind`.
4. `src/main.cpp` — add to `CATALOG_KINDS[]` **at the same index as the catalog
   entry**. Nothing enforces this; a misalignment compiles, links, and launches
   the wrong game from the right tile.
5. `src/main.cpp` — a `case` in `launchKind()` (wires the instance) and one in
   `drawLauncherIcon()` (draws the tile icon).
6. `src/engine/ScoreCatalog.cpp` — only if it records a score, so the Scores
   screen can show it.

### 2. Docs — the part that actually gets forgotten

7. `README.md` — a row in the right game table (what it is, what it builds,
   age), or a bullet under the system screens if it is an app.
8. `README.md` — an `<img>` in the matching screenshot gallery.
9. `CHANGELOG.md` — an entry under the unreleased heading.

### 3. Screens

10. `tools/gen_screens.py` — a render function plus an entry in `SCREENS` or
    `EXTRA_SCREENS`. Take the geometry from the game's own `Rect` helpers so
    the mock-up matches the device rather than approximating it.
11. Run `python tools/gen_screens.py` and **look at the PNG**. It is a
    generated image; nothing else will tell you it came out wrong.

### 4. Verify

12. `pio run` — and put the new flash/RAM figures in `README.md` **and**
    `CLAUDE.md`. They are the two places that disagree.
13. `python tools/check_docs.py` — must be clean.
14. Flash it and actually play it. Take the board lock first.

### What you do NOT hand-edit

These derive from `GAME_CATALOG` and update themselves. Editing them by hand is
how About fell six games behind in the first place:

- the **About** app's game list, count and blurbs
- the **Settings → Games** visibility list
- the **launcher** tiles and paging
- the **GitHub Pages site** — `tools/gen_site.py` reads the version from
  `AppVersion.h`, the game list and blurbs from `GAME_CATALOG`, the build
  figures from `README.md` and the board name from `platformio.ini`. Edit
  `site/index.template.html` for wording and layout only; `check_docs.py`
  fails if a version number is typed into it

### If it is a system app rather than a game

Everything above still applies, plus:

- It **must work in both orientations.** Read `tft.width()` / `tft.height()` at
  render time, never `SCREEN_WIDTH` / `SCREEN_HEIGHT`. Settings and Wi-Fi
  currently violate this and are the reason the rule is stated so bluntly.
- If it touches a radio, stores data, or changes what leaves the device, the
  **About radios page** and the **README privacy section** are part of the same
  change — and About must *read* the state, not restate it.

## Supporting a new board — the whole checklist

Before adding support for a new hardware variant, complete a comprehensive hardware reference and verification app. This prevents silent failures, driver misconfigurations, and port-specific quirks from reaching users.

### 1. Hardware documentation — must precede any game support

1. **Create `BOARD_<VARIANT>.md`** — complete hardware reference for the new board
   - Copy structure from [`BOARD_E32R28T-1.md`](BOARD_E32R28T-1.md) — the template
   - Sections: overview, GPIO pinout, display driver, touch, battery sensing, RGB LED, radios, memory, watchdog, known issues, verification procedures
   - Include all TFT_eSPI flags, SPI frequencies, ADC attenuation, power thresholds
   - Document every GPIO assignment and SPI bus topology (which devices share which buses?)
   - **List every known hardware bug, quirk and workaround** (crossed LED pins, inverted polarity, ADC2 unavailable, etc.)
   - Do NOT skip "known issues" section — it is where you prevent the next person from re-discovering the same bugs

2. **Update `AGENTS.md`** to reference the new board file in this section

3. **Update `platformio.ini`** with a new environment for the variant
   - Example: `[panel_st7789]` or `[env:app4]` for a 4-inch board
   - Include all build flags (`-D TFT_WIDTH=`, `-D TFT_HEIGHT=`, `-D PANEL_<VARIANT>=1`, etc.)
   - Partition table (3 MB app? 1.5 MB app?)
   - SPI speeds (some displays require lower frequencies)

### 2. Hardware verification app — exercises all subsystems before game support

1. **Create a bringup/test app** (can extend the existing `env:bringup` environment)
   - Exercises every hardware subsystem: display, touch, battery, BLE, Wi-Fi, RGB LED
   - Each subsystem has a simple test: fill colors, show coordinates, read voltage, scan networks
   - Display full-screen color test + rotation check + brightness slider
   - Touch: show crosshairs, tap them, verify coordinates match screen, check pressure threshold
   - Battery: show voltage, percentage, charging state; physically plug/unplug USB and verify state changes within 2s
   - BLE: turn beacon on/off, use phone scanner to verify name and payload format
   - Wi-Fi: connect to network, fetch NTP time, verify clock advances
   - RGB LED: show all colours, verify no crossed polarity or missing channels
   - Run tests from serial monitor or on-device UI menu

2. **All tests must pass before merging the variant into main:**
   - [ ] Display renders all colors correctly at native resolution
   - [ ] Touch calibration works; coordinates accurate to ±10 pixels
   - [ ] Battery readings within 0.1V of meter; charging/discharging detected within 2s of USB plug/unplug
   - [ ] BLE beacon visible on phone with correct device name and manufacturer data payload
   - [ ] Wi-Fi connects and fetches time; clock is correct
   - [ ] RGB LED shows correct colours (no crossed channels)
   - [ ] Watchdog reboot works (hang the loop; device reboots after 12s)

3. **Document test procedures in the board file** — section 11: Hardware Verification Procedures
   - Step-by-step instructions so future agents can verify a new board independently
   - Expected success criteria (e.g., "voltage within ±0.1V of meter")

### 3. Architecture changes for multi-board support (applies if adding non-playable-game code)

1. **`ScaledRenderer` or similar display scaling** — if supporting different resolutions (e.g., 240×320 vs 480×320)
   - Games author to fixed 320×240 logical canvas
   - Renderer stretches to physical panel size
   - Touch reverse-scales from physical space back to logical canvas

2. **Pin abstraction layer** — if multiple GPIO assignments across boards
   - Do NOT hardcode pins in driver code; use `BoardConfig.h` and `-D` flags
   - Use compile-time constants; no runtime conditionals for pins

3. **Driver selection flags** — if multiple display/touch controllers
   - Use `-D PANEL_<NAME>=1` and `#if PANEL_<NAME>` in driver init
   - TFT_eSPI already supports this via `-D <DRIVER>=1` flags

### 4. Verification before merge

1. `python tools/check_docs.py` — must pass (board list, build figures)
2. `pio run -e app` and `pio run -e app4` (or new variant) — both must build
3. Firmware flashes successfully on the new board
4. All hardware verification tests pass
5. At least one playable game runs and is responsive (no frames > 40ms)

### What NOT to do

- Do NOT add support for a new board without a `BOARD_<VARIANT>.md` file
- Do NOT merge to `main` unless all verification tests pass
- Do NOT ship a board driver without documenting known bugs and workarounds
- Do NOT change display/touch driver code without also updating the board reference file

## The web installer is part of the deliverable

`https://iamankushpandit.github.io/Gume/` flashes a board from the browser over
Web Serial. `.github/workflows/pages.yml` builds all three PlatformIO
environments on every push to `main`, runs `tools/gen_site.py`, and drops the
resulting `.bin` files beside the manifest that points at them. Nobody
regenerates it by hand, so it cannot go stale on its own — but three things
break it, and all three fail in someone else's browser rather than here:

1. **Adding or renaming a PlatformIO environment.** The page offers one
   firmware per entry in `gen_site.py`'s `VARIANTS`; if the workflow does not
   build that env, its manifest points at binaries that do not exist and the
   flash fails partway. `check_docs.py` cross-checks `VARIANTS` against
   `platformio.ini` and the workflow.
2. **Changing what the firmware transmits or stores.** The page carries a
   privacy section, and the same rule applies to it as to the About radio page
   and the README: a privacy claim that has drifted from the hardware is worse
   than none, because it is believed.
3. **Making CI unable to build.** `main` deliberately points `map-n-flag` at a
   machine-local path; the workflow rewrites that line in its own working copy.
   If `lib_deps` changes shape, fix the `sed` in the same commit.

`python tools/gen_site.py` writes `site/_build/` locally so you can look at the
page. The flash button will 404 there — the binaries only exist in CI.

Four things that cause real damage here if you skip them:

1. **Branch into your own worktree before you start, from `dev`.** New requirement → `git worktree add ../GUme-<slug> -b feat/<slug> dev`, then work there. Do **not** `git switch` inside the main checkout: it holds other agents' uncommitted work and switching under them strands it. A worktree also gives you a private `.pio/` build dir.
2. **Multiple agents share this repo.** Read `git status` first; uncommitted work that isn't yours is normal. Stage explicit paths, never `git add -A`. Don't merge or push unless asked. **`main` and `dev` are protected on GitHub** — no force-push, no deletion, and every merge goes through a pull request with the `verify` CI job green. Land work on `dev`; `main` takes releases from `dev` once they have been tested on hardware. See [CONTRIBUTING.md](CONTRIBUTING.md#branches-and-protection).
3. **`GAME_CATALOG[]` and `CATALOG_KINDS[]` are index-coupled across two files** and a bad merge misaligns them silently — it still compiles and links, and the only symptom is a tile launching the wrong game. Re-verify entry-for-entry after every merge.
4. **One physical board, one serial port** — shared across all branches and worktrees. Don't flash or factory-reset without saying so; it wipes calibration, profiles and scores another agent may be testing against.
5. **Take the board lock before building or flashing.** It lives at `(git rev-parse --git-common-dir)/gume-board.lock` so it's visible from every worktree. If it's held by a live process, wait and poll. If no build or flash process is actually running, the lock is stale — delete it and carry on, and say that you did. Release your own lock even when the build fails.
