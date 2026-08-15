# GUme — GoodTime Kids

## Two standing rules — do these without being asked

**No AI attribution in commits.** Never add `Co-Authored-By: Claude`,
`Co-Authored-By:` naming any AI, "Generated with…" footers, or any trailer that
credits a model or tool. This applies to commits, amends, squashes and PR
bodies. The history here records a human author.

**Docs are part of the change, not a follow-up.** If a change alters behaviour,
architecture, dependencies, screens, settings, the game list or the build, then
in the *same* commit update `README.md` (feature set, game count, the flash/RAM
figures from your own `pio run`, version), this file (architecture, invariants,
build), `AGENTS.md` (agent protocol) and the relevant directory `CLAUDE.md`.
A README claiming the wrong game count or a stale flash figure is a defect
belonging to whoever last changed the thing it describes.

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

If you are about to type a fact into About that the firmware already knows, read
it from the firmware instead. Anything genuinely static — the credits, the
privacy statements — must be re-read whenever the thing it describes changes.

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

---

ESP32 firmware (Arduino / PlatformIO, C++17) for a handheld educational console for young children. 26 games, all baked into flash. Target hardware is the E32R28T-1 / ESP32-32E (2.8-inch 240×320 resistive-touch board): ILI9341 320×240 TFT + XPT2046 resistive touch + onboard single-cell Li-ion/LiPo charging circuitry. Wi-Fi is used for NTP only — no accounts, no telemetry, no SD card required.

## Build

```bash
pio run                  # build (env:app, the default)
pio run -t upload        # build + flash at 460800 baud
pio device monitor       # serial, 115200 baud
```

Two diagnostic environments exist for hardware triage:
- `pio run -e bringup` — full tree with `-D CYD_BRINGUP_ONLY`; `main.cpp` compiles a display/touch/SD check instead of the app.
- `pio run -e wifidiag` — builds `src/wifi_diag.cpp` **alone** (`build_src_filter = +<wifi_diag.cpp>`), so no TFT/touch/game code can interfere with the radio test.

### Build gotchas

- **`map-n-flag` points at a machine-local path.** `lib_deps` now carries `file://C:/Users/Ankus/CppEsp32Lib` on `main`, which resolves only on the author's machine — any other checkout must swap it back to `https://github.com/iamankushpandit/map-n-flag.git` to build. It is committed deliberately; don't "fix" it, and don't be surprised when a fresh clone fails to resolve it.
- **BLE pulls in NimBLE, not Bluedroid.** `h2zero/NimBLE-Arduino` costs ~192 KB of flash for host plus controller; the core's Bluedroid stack costs several times that and this partition cannot absorb it.
- `lib_ldf_mode = deep+` is required on `env:app` — transitive library headers do not resolve without it.
- **TFT_eSPI is configured entirely through `-D` flags in `platformio.ini`** (`USER_SETUP_LOADED=1`, pins, `USE_HSPI_PORT`, fonts, SPI speeds). There is no `User_Setup.h` — editing one would do nothing.
- Partition is `huge_app.csv` (3 MB app). Flash is the scarce resource; artwork and data tables dominate.
- `CYD_SCREEN_ROTATION=3` is landscape with the USB edge at the bottom. `Board::pollTouch()` compensates for every rotation, so don't hand-correct coordinates in game code.

## Working alongside other agents

Assume you are **not** the only one editing this tree. Several agents (Claude or otherwise) and the author may be working concurrently, and files can change underneath you mid-task.

**Start by reading `git status`.** Uncommitted work that isn't yours is normal here. If a file you need is already modified, someone is probably mid-change in it — re-read it immediately before editing, and don't assume your earlier read is still accurate.

### Branch per requirement — in your own worktree

When a new requirement arrives, don't start editing the shared tree. Take an isolated copy:

```bash
git worktree add ../GUme-<slug> -b feat/<slug>
```

Then work entirely in `../GUme-<slug>`. Branch names: `feat/<game-id>` for a new game, `fix/<area>` for a repair, `docs/<topic>` otherwise.

**Use a worktree, not a bare `git switch`.** Switching branches inside a tree that holds another agent's uncommitted changes either drags their work onto your branch or refuses outright — and if it succeeds, it silently strands their edits somewhere they don't expect. Right now this tree has substantial uncommitted work in it from at least one other agent, so treat the main checkout as occupied. (In Claude Code, worktree isolation can also be requested when spawning the work.)

A separate worktree also gives you your own `.pio/` build directory, which removes the concurrent-build race described below.

Keep branches short-lived and rebase onto `main` often — a branch that sits for days turns into exactly the merge this is meant to avoid. Build before you merge, and don't merge to `main` or push unless the user asks.

**Branching moves the risk rather than removing it.** More parallel branches means more merges, and the failure below is a merge-time failure — so it gets *more* important, not less.

### The one that bites silently

`GAME_CATALOG[]` (`src/engine/GameCatalog.cpp`) and `CATALOG_KINDS[]` (`src/main.cpp`) are parallel arrays coupled **by index**, in two different files, with nothing enforcing agreement. Two agents each appending a game will both append at the end of both arrays. A merge — or a human resolving "keep both changes" — can easily order them differently, and the result **still compiles and still links**. The only symptom is a tile launching the wrong game.

After any merge, rebase, or conflict resolution touching either file, re-verify the two arrays line up entry-for-entry before doing anything else. Same applies to `ScoreCatalog.cpp`, which is keyed off the same list.

### Serialise on the spine, parallelise on the leaves

- **Leaves — safe in parallel:** a game's own `src/games/*.{h,cpp}` pair. One agent per game is fine.
- **Spine — expect collisions:** `src/main.cpp`, `src/engine/GameCatalog.*`, `src/engine/ScoreCatalog.cpp`, `src/hal/Board.*`, `src/ui/Ui.*`. Adding a single game touches five sites across three of these. Make spine edits tight and land them quickly rather than holding them open across a long task.

### The board is a singleton

There is one physical device and one serial port, and both are exclusive:

- `pio device monitor` holds the COM port; an `upload` from another agent will fail while it's open. Release it when you're not actively reading.
- **Flashing destroys shared physical state.** NVS holds touch calibration, profiles and scores that another agent may be mid-test against, and `factoryReset()` wipes all of it. Never flash or factory-reset on a shared board without saying so first.
- One `.pio/` build directory per working tree. Two concurrent `pio run` invocations in the same tree race on it — use a separate git worktree if you need to build in parallel.

### Take the lock before you build or flash

Worktrees isolate source and `.pio/`, but **not** the board, the serial port, or the toolchain's shared package cache. Those need an explicit lock, and it has to live somewhere every worktree can see — so it goes in the shared git common directory, not in `.pio/`:

```powershell
$lock = Join-Path (git rev-parse --git-common-dir) "gume-board.lock"
```

That path resolves to the same file from every worktree, and is never committed.

**Acquire** before any `pio run`, `pio run -t upload`, or `pio device monitor` — write your PID, the action, the worktree and a timestamp, so the next agent can tell whether you're alive:

```powershell
"$PID|flash|$(Get-Location)|$(Get-Date -Format o)" | Set-Content $lock -Encoding utf8
```

**Release it in all cases** when the build, flash or monitor session ends — including on failure. `Remove-Item $lock`.

### If the lock is already held

Do not just delete it, and do not build anyway. Work out whether it is live or stale:

1. Read the file and take the PID from the first field.
2. Check whether that process still exists:
   ```powershell
   Get-Process -Id <pid> -ErrorAction SilentlyContinue
   ```
3. Cross-check for any build or flash actually in flight, in case the PID was recycled:
   ```powershell
   Get-Process | Where-Object { $_.Name -match 'platformio|pio|esptool|python' }
   ```

**If a matching process is running, wait** — poll every ~10 s rather than busy-looping, and say what you're waiting on. Builds take a couple of minutes; a flash plus verify is under a minute. Keep waiting while the process lives.

**If no such process exists, the lock is stale — delete it and continue.** Stale locks are normal here: an agent that was interrupted, crashed, or had its command cancelled mid-build never got to release it. A leftover file must not be allowed to block every future agent, so clearing it is the correct action, not a workaround.

Say so plainly when you remove one — note the PID and timestamp you found — so it's visible if a build really was killed halfway and left `.pio/` in a bad state. A lock older than ~15 minutes with no live process is stale beyond doubt.

The same reasoning applies to any lock PlatformIO itself leaves in `~/.platformio` after an interrupted dependency install: confirm nothing is running, then clear it.

### Shared budgets

Flash is global and nearly the binding constraint (2,264,721 / 3,145,728 bytes,
**72.0%**; NimBLE plus the BT controller account for ~192 KB of that). RAM sits
at 65,316 / 327,680 (19.9%) -- higher than it was, deliberately: RowList traded
864 bytes of static RAM for zero heap traffic. On this device that is a good
trade every time. Two agents can each add artwork that fits locally and together overflow it. Read the size line from `pio run` and report it when you add data tables or images.

### Modularity rule

If a file is becoming large (as a rule of thumb, `src/main.cpp` > ~400 lines of active logic in a single function, or any `.cpp` > ~600 lines total), **refactor it into a more modular form first** before making the requested change. Split into helper files, break large functions into smaller ones, or extract a new class — whatever fits the existing architecture. The refactor must not break existing functionality (build must still succeed and behaviour must be unchanged), and it must land as its own commit before the feature change that prompted it.

This keeps diffs reviewable, conflicts locatable, and prevents any single file from becoming a merge hazard.

## Git hygiene

- Stage explicit paths. `git add -A` will sweep up another agent's half-finished work.
- Commit narrowly — one game or one concern per commit — so conflicts stay resolvable.
- Don't rebase or force-push shared branches, and don't revert changes you can't attribute; an unfamiliar edit is more likely someone's in-flight work than a mistake.
- Don't reformat, re-order includes, or opportunistically refactor files you aren't otherwise changing. Cosmetic churn in a spine file turns someone else's small diff into a merge conflict.
- `src/games/CountryDataTable.cpp` is generated. Regenerating it rewrites the whole file — announce it rather than folding it into an unrelated change.

## Architecture

`setup()`/`loop()` in `src/main.cpp` delegate to a `KidsPlatformApp` singleton that owns every screen as a `static` instance and implements `GameHost`.

Views: **Profiles** (shown at boot, picks whose scores are being written) → **Launcher** (paginated tile grid) → **Game** → **ScreenSaver** (self-playing Pong that mirrors rally colour onto the case LED).

| Layer | Where | Responsibility |
|---|---|---|
| `Game` / `GameHost` | `src/engine/Game.h` | Screen lifecycle + the only handle games get on the system |
| `Board` | `src/hal/Board.h` | All hardware: TFT, touch, NVS, Wi-Fi/NTP, RGB LED, profiles, settings |
| `Ui` | `src/ui/Ui.h` | Stateless themed drawing helpers; owns the colour palette |
| `GameCatalog` | `src/engine/GameCatalog.h` | Single source of truth for the game list |
| `Watchdog` | `src/hal/Watchdog.h` | Background supervisor: reboots a hung loop, logs stalls and heap, keeps a crash breadcrumb |
| `BleBeacon` | `src/hal/BleBeacon.h` | Opt-in non-connectable BLE presence beacon. Owns the one authoritative advertisement payload |

### Invariants worth knowing before editing

- **Every screen is a `Game`.** Settings, Wi-Fi, Profiles, Scores, System Info and About are all `Game` subclasses with the same `begin`/`update`/`render`/`end` lifecycle.
- **`end()` is called on every screen change** via `KidsPlatformApp::leaveActiveGame()`, before the next screen's `begin()`. Add new transitions through that funnel, not by assigning `activeGame_` directly. Override `end()` for anything a screen holds that outlives a frame; nothing here runs off a task or timer, and the hook is what keeps that true.
- **Never sample the battery ADC more than once per frame.** `Board::readBatteryTelemetry()` caches for 2s and everything else reads through it. Each accessor used to run its own blocking 10ms conversion, and a top bar calls two of them. See `src/hal/CLAUDE.md`.
- **Games never touch hardware directly.** They receive `GameHost&` and go through `host.board()`. Keep it that way.
- **Profile scoping is automatic and invisible to games.** `Board::scopedKey()` is **private**; it prefixes `p{N}_` inside `getScore` / `setScore` / `saveBestScore` / `worstScore` / `loadBlob` / `saveBlob`. Just call those with a plain key and per-profile behaviour comes for free. Guest (`GUEST_INDEX == 5`) silently **drops all writes** — that is what makes it a guest rather than a sixth child.
- **Device settings are global, not per-profile**: theme, layout, brightness, Wi-Fi credentials, NTP, timezone. Per-profile: scores, mastery blobs, game visibility.
- **`GameCatalogEntry::id` is a persisted NVS visibility key.** Renaming one silently resets that game's visibility on existing devices.
- **`GAME_CATALOG` holds the 26 playable games only.** Scores / Settings / Wi-Fi / Profiles / About / System Info are appended by `KidsPlatformApp::allEntry()` at raw indices `>= GAME_CATALOG_COUNT`, are always visible, and always sort to the end of the launcher.
- **`CATALOG_KINDS[]` in `main.cpp` must stay index-aligned with `GAME_CATALOG[]`.** Nothing enforces this; a misalignment launches the wrong game from the right tile.
- **The launcher shows the profile name as plain text, not a button.** The framed chip is what overlapped the status badges; the name itself is wanted. `launcherProfileRect()` is both where it draws and the touch target, so the two cannot drift — in landscape it sits after the byline, not across it.
- **The launcher status badges are packed to the pixel.** Landscape runs from a hairline at `lW-110` to the gear at `lW-30` with about 8px spare, which is why the BLE badge sits on the clock's line and is positioned off the *measured* width of the clock string. Portrait has room to extend the badge row instead. Anything new in that header needs the same treatment — measure, don't guess.
- **The BLE advertisement has exactly one description.** `BleBeacon::Advertisement`
  is compiled into a raw AD buffer that is handed to the controller verbatim,
  and the System Info BLE tab reads that same buffer back. Never add a
  hand-written UI description of the payload -- see `docs/BLE_BEACON_SPEC.md`.
- **The loop is watchdogged.** `Watchdog::feed()` is the first statement in `KidsPlatformApp::loop()` and a frame over `TIMEOUT_SECONDS = 12` reboots the device. Anything that blocks the loop task for longer on purpose — a calibration wizard, a network round trip — must sit inside a `Watchdog::Pause` guard, or it will look exactly like a hang. See `src/hal/CLAUDE.md`.

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

## Rendering model

Direct TFT_eSPI primitives, RGB565, no framebuffer. A full 320×240 wipe pushes ~150 KB over SPI — roughly 30 ms of visible blanking — which is why `Game` carries two levels of invalidation:

- `markDirty()` — content changed; repaint moving parts only.
- `markFullDirty()` — layout changed; repaint background/chrome too.
- `render()` should guard static chrome behind `if (needsFullRender())` and draw dynamic parts unconditionally.

These three are `protected`; the public surface is `needsRender()`, `clearDirty()`, and `requestRender()` (which forces a full repaint, used when returning to a screen). First paint is always full.

**Clip scrolling content with `tft.setViewport(x, y, w, h, false)`** and reset it after. Skipping rows that fall entirely outside the viewport is not enough — the row straddling the edge still draws in full and smears into the chrome above it, which is what System Info did into its own tab strip. `vpDatum=false` keeps drawing coordinates absolute, so nothing else in the draw loop changes.

Most games still repaint wholesale. Cinnamon is the reference for partial redraw — it was also a photosensitivity concern at full-flash rates, so prefer partial redraw for anything that updates rapidly.

Games are authored against a fixed 320×240 landscape canvas. Only the launcher supports portrait (`LayoutMode::Vertical`, 4 tiles/page vs 6 in landscape).

**System/UI apps** (Settings, Wi-Fi, SystemInfo, Profiles, Scores, About, and any future app-style screens beyond the playable game catalog) must support **both landscape and portrait orientations**. They must read `tft.width()` / `tft.height()` at render time rather than the compile-time constants `SCREEN_WIDTH` / `SCREEN_HEIGHT`, and lay themselves out responsively. Use `Ui::drawTab()` + `Ui::drawTabBaseline()` for multi-section content; the tab strip width adapts by dividing `tft.width()` at render time.

## Layout

```
include/BoardConfig.h     pins + screen constants   include/AppVersion.h
src/main.cpp              app, launcher, screen saver, dispatch
src/wifi_diag.cpp         standalone radio test (env:wifidiag only)
src/engine/               Game, GameCatalog, ScoreCatalog, Progress,
                          RecentQuestions, ContentLoader
src/games/                one .h/.cpp pair per game + Country/State data
src/hal/                  Board (HAL), Clock, Watchdog
src/ui/                   Ui (theme, widgets, image blitting)
tools/                    generators — gen_country_facts.py, gen_screens.py,
                          gen_site.py, check_docs.py
site/                     index.template.html — the GitHub Pages landing page
.github/workflows/        pages.yml — builds every firmware, publishes the site
docs/                     SD_CONTENT_SPEC.md, screens/
```

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

## Hardware notes

Pins live in `include/BoardConfig.h`; read it rather than trusting generic ESP32 pinouts online.

- **The RGB LED's red and green lines are crossed on this unit** relative to the usual standard pinout — `PIN_RGB_R = 16`, `PIN_RGB_G = 4`, `PIN_RGB_B = 17`. This is already corrected in `BoardConfig.h` and verified on hardware; do not "fix" it again. Common anode, so drive is inverted.
- Touch is bit-banged SPI (the TFT owns HSPI), 3-point affine calibration persisted in NVS behind a magic number. `TOUCH_PRESSURE_THRESHOLD = 350`, `TOUCH_HIT_SLOP = 8`.
- Backlight brightness floors at `Board::BRIGHTNESS_MIN = 25` — at lower duty the panel is unreadable and a child could not see the slider to undo it.
- `PIN_SPEAKER = 26` exists but audio is stubbed; `beepOk()`/`beepError()` pulse the RGB LED instead.
- Wi-Fi/NTP is a non-blocking state machine driven by `tickTimeSync()` each frame, with a raw-UDP `ntpUdpProbe()` fallback for when lwIP's SNTP never answers. Timezone comes from a named POSIX zone or public-IP lookup — routers don't advertise one in practice.

## Conventions

- Hit testing: `Rect{...}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)`. `Rect` is in `Ui.h`, `TouchPoint` in `Board.h`.
- Feedback: `board.beepOk()` / `board.beepError()`.
- Draw through `Ui::` helpers so the Dark/Light theme is respected; avoid hardcoded colours outside icon art.
- `src/games/CountryDataTable.cpp` is generated — edit `tools/gen_country_facts.py` and regenerate.
- `swallowTouch_` in `main.cpp` suppresses the first press after a rotation change or screen-saver dismissal, preventing a phantom tap on freshly drawn UI.
