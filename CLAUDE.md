# GUme — GoodTime Kids

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

- **The `map-n-flag` dependency is in flux.** Committed `HEAD` uses `https://github.com/iamankushpandit/map-n-flag.git`; the working tree currently points at `file://C:/Users/Ankus/CppEsp32Lib`, a machine-local absolute path. Check `git diff platformio.ini` before assuming which is live. The `file://` form will not resolve on any other machine — don't commit it without confirming that's intended, and don't revert it either, since it may be deliberate in-progress work.
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

Flash is global and nearly the binding constraint (~73% of the 3 MB partition). Two agents can each add artwork that fits locally and together overflow it. Read the size line from `pio run` and report it when you add data tables or images.

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

### Invariants worth knowing before editing

- **Every screen is a `Game`.** Settings, Wi-Fi, Profiles, Scores and About are all `Game` subclasses with the same `begin`/`update`/`render` lifecycle.
- **Games never touch hardware directly.** They receive `GameHost&` and go through `host.board()`. Keep it that way.
- **Profile scoping is automatic and invisible to games.** `Board::scopedKey()` is **private**; it prefixes `p{N}_` inside `getScore` / `setScore` / `saveBestScore` / `worstScore` / `loadBlob` / `saveBlob`. Just call those with a plain key and per-profile behaviour comes for free. Guest (`GUEST_INDEX == 5`) silently **drops all writes** — that is what makes it a guest rather than a sixth child.
- **Device settings are global, not per-profile**: theme, layout, brightness, Wi-Fi credentials, NTP, timezone. Per-profile: scores, mastery blobs, game visibility.
- **`GameCatalogEntry::id` is a persisted NVS visibility key.** Renaming one silently resets that game's visibility on existing devices.
- **`GAME_CATALOG` holds the 26 playable games only.** Scores / Settings / Wi-Fi / Profiles / About are appended by `KidsPlatformApp::allEntry()` at raw indices `>= GAME_CATALOG_COUNT`, are always visible, and always sort to the end of the launcher.
- **`CATALOG_KINDS[]` in `main.cpp` must stay index-aligned with `GAME_CATALOG[]`.** Nothing enforces this; a misalignment launches the wrong game from the right tile.
- **The loop is watchdogged.** `Watchdog::feed()` is the first statement in `KidsPlatformApp::loop()` and a frame over `TIMEOUT_SECONDS = 12` reboots the device. Anything that blocks the loop task for longer on purpose — a calibration wizard, a network round trip — must sit inside a `Watchdog::Pause` guard, or it will look exactly like a hang. See `src/hal/CLAUDE.md`.

## Adding a game

Five coordinated edits — miss one and it fails silently or won't link:

1. `src/games/NewGame.{h,cpp}` — subclass `Game`.
2. `src/engine/GameCatalog.cpp` — append an entry. Blurbs render at font 1 across ~292 px, so keep them under ~46 chars.
3. `src/main.cpp` — add to `enum class EntryKind`.
4. `src/main.cpp` — add to `CATALOG_KINDS[]` **at the same index as the catalog entry**.
5. `src/main.cpp` — add a `case` to `launchKind()` (wires the instance) and to `drawLauncherIcon()` (draws the tile icon).

If the game records a score, also add it to `src/engine/ScoreCatalog.cpp` so the Scores screen can display it.

## Rendering model

Direct TFT_eSPI primitives, RGB565, no framebuffer. A full 320×240 wipe pushes ~150 KB over SPI — roughly 30 ms of visible blanking — which is why `Game` carries two levels of invalidation:

- `markDirty()` — content changed; repaint moving parts only.
- `markFullDirty()` — layout changed; repaint background/chrome too.
- `render()` should guard static chrome behind `if (needsFullRender())` and draw dynamic parts unconditionally.

These three are `protected`; the public surface is `needsRender()`, `clearDirty()`, and `requestRender()` (which forces a full repaint, used when returning to a screen). First paint is always full.

Most games still repaint wholesale. Simon is the reference for partial redraw — it was also a photosensitivity concern at full-flash rates, so prefer partial redraw for anything that updates rapidly.

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
tools/                    generators — gen_country_facts.py, gen_screens.py
docs/                     SD_CONTENT_SPEC.md, screens/
```

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
