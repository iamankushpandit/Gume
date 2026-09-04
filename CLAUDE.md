# GUme â€” Braino!

These rules apply to everyone changing this repository, human or agent. `CONTRIBUTING.md` covers how a change reaches the protected `main` and `dev` branches; this file covers what the change itself has to honour.

## Two standing rules â€” do these without being asked

**No AI attribution in commits.** Never add `Co-Authored-By: Claude`,
`Co-Authored-By:` naming any AI, "Generated withâ€¦" footers, or any trailer that
credits a model or tool. This applies to commits, amends, squashes and PR
bodies. The history here records a human author.

**Docs are part of the change, not a follow-up.** If a change alters behaviour,
architecture, dependencies, screens, settings, the game list or the build, then
in the *same* commit update `README.md` (feature set, game count, the flash/RAM
figures from your own `pio run`, version), this file (architecture, invariants,
build), `AGENTS.md` (agent protocol) and the relevant directory `CLAUDE.md`.
A README claiming the wrong game count or a stale flash figure is a defect
belonging to whoever last changed the thing it describes.

**Run `python tools/check_docs.py` and `python tools/check_boards.py` before
you commit.** It fails if the version,
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

## The About app is user-facing documentation â€” keep it true

`AboutGame` is the only documentation most owners will ever read, and the only
one they read *while holding the device*. It is part of the deliverable, not a
credits screen. **Update it in the same commit as the change it describes.**

It was already six games out of date once, because it kept a hand-written list.
The fix, and the standing rule, is to **derive rather than restate**:

| About shows | Derived from |
|---|---|
| Version | `BRAINO_VERSION` |
| Game count and every game name/blurb | `AppRegistry` playable apps |
| Board name | `BOARD_NAME` |
| Wi-Fi status | `Board::hasWifiCredentials()` / `isWifiConnected()` |
| Beacon status and advertised name | `BleBeacon::active()` / `configured()` |
| Whether scores are being shared | `BleBeacon::configured().sharesActivity` |
| Branch, commit and build time | `BuildStamp::branch()` / `commit()` / `builtAt()` |

If you are about to type a fact into About that the firmware already knows, read
it from the firmware instead. Anything genuinely static â€” the credits, the
privacy statements â€” must be re-read whenever the thing it describes changes.

## No data collection - the rule that outranks the feature

Braino collects nothing about the player using it, and no change may alter
that. It is not a setting that ships switched off; it is what the product is.
Exactly three things leave the device: an NTP time query, one `ip-api.com`
lookup to guess the timezone on first connect, and the opt-in, non-connectable
BLE beacon. **That list is closed.** Do not add analytics, usage counters,
crash reporting, any other HTTP/UDP/DNS request, any dependency that phones
home at runtime, or anything transmitted that carries a player's name, profile
name, score, progress or typing. A fourth outbound flow needs the maintainer's
agreement in an issue *before* the code exists - it is a change to what the
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

## Responsiveness rule â€” know what a frame costs

The loop runs at a 20ms budget (`FRAME_BUDGET_MS`) and paces to a deadline: it
sleeps only the remainder, so a slow frame is not punished twice. That budget is
the whole allowance for touch, logic and drawing. Three things have each eaten
most of it at some point in this codebase, and all three look harmless at the
call site:

1. **NVS reads.** `Preferences` is flash-backed â€” every getter is a hash lookup,
   not a variable read. `screenSaverSeconds()` ran once per loop iteration and
   `gameVisible()` up to ~180 times per launcher repaint. **Anything read more
   than once per screen change gets a write-through RAM mirror in `Board`**:
   the setter updates the mirror and NVS together, so a stale read is not
   possible. Theme, layout, brightness, idle timeout, active profile, game
   visibility, Wi-Fi credentials and the NTP hot-path settings all work this
   way now. Add to that list rather than reaching for `prefs_` in a hot path.
2. **Blocking `delay()` inside a getter.** Battery sensing slept 10ms per call
   and `Ui::drawTopBar()` calls two battery getters, so every top bar cost
   ~20ms â€” a whole frame â€” before anything was drawn. **No `delay()` in
   anything a render path can reach.** Sample on a cadence and cache.
3. **Rebuilding content that did not change.** System Info reassembled every
   row on every frame while scrolling. Scrolling changes an offset, not
   content. Gate rebuilds behind a stale flag.

Before claiming a screen is fast: System Info's Memory tab shows loop load,
worst work, worst frame and NVS usage, and the watchdog logs a stall past
`STALL_WARN_MS`. A worst frame above ~40ms is a bug, not a heavy screen.

Related: full-screen repaints are ~150KB over SPI and ~30ms of visible blanking,
which is why `Game` has two levels of invalidation. Guard static chrome behind
`needsFullRender()` and repaint only what moved.

## Memory rule â€” there is no garbage collector

C++ gives you no GC, and FreeRTOS gives you a heap that can never be compacted.
So the thing that kills this device is not the leak you are picturing. There is
not one `new`, `delete`, `malloc` or `free` in this firmware â€” every screen is a
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
   is the worked example: it was 48 rows Ã— 2 `String`s rebuilt every frame â€”
   about 96 long-lived allocations churning at frame rate â€” and is now flat
   char buffers that allocate nothing, ever.
3. **Don't rebuild content on every frame.** Rebuild when the data changed and
   keep a `stale` flag. Scrolling changes an offset, not the content.
4. **A `String` member on a screen is a smell.** A few exist for genuinely
   user-entered text (`ProfileGame::draft_`, `WifiGame::password_`) â€” that is
   the bar. Anything derived from state belongs in a fixed buffer.
5. **Give back what you borrowed, in `end()`.** Every screen transition goes
   through `BrainoApp::leaveActiveGame()`, which compares free heap
   against the value captured before that screen's `begin()` and logs
   `[heap] '<screen>' left N bytes short` when a screen does not hand it back.
   Watch the serial log after adding a screen.
6. **Prefer fixed-size members over growth.** A statically sized array that is
   occasionally half empty is cheaper and safer here than anything that grows.
   Trading a few hundred bytes of static RAM for zero heap traffic is nearly
   always the right call on this device â€” `RowList` cost 864 bytes of RAM and
   *saved* 5.5 KB of flash.
7. **Check the numbers before you claim it is fine.** `pio run` reports RAM and
   flash; System Info's Memory tab reports free heap, minimum free heap,
   largest allocatable block, fragmentation and NVS pressure. Read them.

---

ESP32 firmware (Arduino / PlatformIO, C++17) for a handheld educational console for young players. 31 games, all baked into flash. Target hardware is the E32R28T-1 / ESP32-32E (2.8-inch 240Ã—320 resistive-touch board): ILI9341 320Ã—240 TFT + XPT2046 resistive touch + onboard single-cell Li-ion/LiPo charging circuitry. Wi-Fi is used for NTP only â€” no accounts, no telemetry, no SD card required.

## Build

```bash
pio run                  # build (env:app, the default)
pio run -t upload        # build + flash at 460800 baud
pio device monitor       # serial, 115200 baud
```

Five diagnostic environments exist for hardware triage:
- `pio run -e bringup` â€” full tree with `-D CYD_BRINGUP_ONLY`; `main.cpp` compiles a display/touch/SD check instead of the app.
- `pio run -e wifidiag` â€” builds `src/wifi_diag.cpp` **alone** (`build_src_filter = +<wifi_diag.cpp>`), so no TFT/touch/game code can interfere with the radio test.
- `pio run -e batdiag` — builds `src/battery_diag.cpp` **alone**, an eight-page
  battery bring-up and calibration tool. It exists because the questions the
  battery poses cannot be answered from inside the app: the frame budget, the
  watchdog and the 2s telemetry cache are all correct product decisions that
  get in the way of watching an ADC for an hour. BOOT cycles pages; CSV
  (`ms,raw,adc_mv,cell_mv,pct,state`) streams to serial for capturing a full
  discharge. It reads the divider ratio and the ADC fault ceiling from the same
  board profile the product does. Its charge-inference constants are still
  copied from `BoardPower.cpp` deliberately, so what it shows is what the
  product will do — **if you change one of those, change both.**
- `pio run -e s3diag` -- builds `src/s3_diag.cpp` **alone**, a bring-up probe
  for the Freenove FNK0104B. It skips the touch calibration wizard on purpose:
  `env:bringup` runs that on a board with no stored calibration, which on a
  capacitive panel the XPT2046 code cannot read strands the display check
  behind a dead crosshair.
- `pio run -e diag4` -- builds `src/diag4.cpp` **alone**, a eight-page bring-up
  probe for the 4-inch ST7796 board. It exists because every fact that board
  needs stated in a profile is still a guess, and the three ways of being wrong
  -- wrong SPI bus, wrong driver, wrong backlight pin -- all produce the same
  dark screen with a healthy serial log. Page ID reads the controller's ID
  register back over MISO, which separates them without anything being visible;
  page BL sweeps candidate backlight pins in both polarities. **It defines
  neither `TFT_BL` nor `GUME_BOARD_HEADER`, deliberately** -- TFT_eSPI owning
  the backlight would defeat the sweep, and a probe must not depend on the
  board profile it exists to produce. Both are boardless envs, listed in
  `check_boards.py`'s `BOARDLESS_ENVS`, because a `[board_*]` section is a
  claim of support and neither board is supported yet.

### Every build is stamped, and the time is not a `-D`

`tools/build_stamp.py` is a pre-build script wired in from `[esp32_common]`, so
every environment gets it. It injects the branch and the abbreviated commit as
`-D GUME_BUILD_BRANCH` / `-D GUME_BUILD_COMMIT`, and the firmware reads them
through `BuildStamp::` -- never the macros directly, outside
`include/BuildStamp.h`. About's last page, System Info's Device tab and the
`[boot] build=` serial line all read the same three accessors, so the answer to
"which firmware is on this board?" is one fact with three viewers.

`BRAINO_VERSION` cannot answer that question: it is identical across every
flash of a release, which is exactly the case where you need to know.

**Do not add the build time as a third `-D`.** PlatformIO folds build flags into
its build signature, so a flag whose value changes on every invocation -- which
a clock does by definition -- invalidates every object in the tree and turns
`pio run` into a permanent full rebuild: roughly 100 seconds instead of 25, for
everyone, forever. The time comes from the compiler's own `__DATE__` and
`__TIME__` inside `src/BuildStamp.cpp`, and the script deletes that one object
file so they are always current. One file recompiles per build, not the tree.
The consequence to know is that the stamp is the build machine's local clock in
C's format, not UTC and not ISO -- it identifies a build, it is not a timestamp
to compute with.

Branch and commit *do* cost a full rebuild when they change, which is correct:
that is when the tree needed rebuilding anyway. On GitHub Actions the checkout
is a detached HEAD, so the script prefers `GITHUB_HEAD_REF` / `GITHUB_REF_NAME`
over `git rev-parse --abbrev-ref`, which would otherwise say "HEAD". A tree with
no `.git` at all -- a source tarball -- is a supported way to build, and stamps
"unknown" rather than inventing something plausible.

### Build gotchas

- **BLE pulls in NimBLE, not Bluedroid.** `h2zero/NimBLE-Arduino` costs ~192 KB of flash for host plus controller; the core's Bluedroid stack costs several times that and this partition cannot absorb it.
- `lib_ldf_mode = deep+` is required on `env:app` â€” transitive library headers do not resolve without it.
- **TFT_eSPI is configured entirely through `-D` flags in `platformio.ini`** (`USER_SETUP_LOADED=1`, pins, `USE_HSPI_PORT`, fonts, SPI speeds). There is no `User_Setup.h` â€” editing one would do nothing.
- **One board = one `[board_*]` section plus one profile header.** `platformio.ini` splits into `[common]` (true of every board), `[esp32_common]` (the MCU), and a `[board_*]` section per board holding only the TFT_eSPI macros, `BOARD_NAME` and `GUME_BOARD_HEADER`. An environment composes `${common.build_flags}` with exactly one `${board_*.build_flags}`. Put a board-specific `-D` in `[common]` and it becomes a claim about every board â€” `check_boards.py` fails on that. The panel is described twice, to TFT_eSPI and to us, and `BoardConfig.h` static_asserts `TFT_WIDTH`, `TFT_HEIGHT` and `TFT_BL` against the profile so the two cannot disagree past the compiler.
- Partition is `huge_app.csv` (3 MB app). Flash is the scarce resource; artwork and data tables dominate.
- `CYD_SCREEN_ROTATION=3` is landscape with the USB edge at the bottom. `Board::pollTouch()` compensates for every rotation, so don't hand-correct coordinates in game code.

## Working alongside other agents

Assume you are **not** the only one editing this tree. Several agents (Claude or otherwise) and the author may be working concurrently, and files can change underneath you mid-task.

**Start by reading `git status`.** Uncommitted work that isn't yours is normal here. If a file you need is already modified, someone is probably mid-change in it â€” re-read it immediately before editing, and don't assume your earlier read is still accurate.

### Branch per requirement â€” in your own worktree

When a new requirement arrives, don't start editing the shared tree. Take an isolated copy:

```bash
git worktree add ../GUme-<slug> -b feat/<slug> dev
```

Then work entirely in `../GUme-<slug>`. Branch names: `feat/<game-id>` for a new game, `fix/<area>` for a repair, `docs/<topic>` otherwise.

**Use a worktree, not a bare `git switch`.** Switching branches inside a tree that holds another agent's uncommitted changes either drags their work onto your branch or refuses outright â€” and if it succeeds, it silently strands their edits somewhere they don't expect. Right now this tree has substantial uncommitted work in it from at least one other agent, so treat the main checkout as occupied. (In Claude Code, worktree isolation can also be requested when spawning the work.)

A separate worktree also gives you your own `.pio/` build directory, which removes the concurrent-build race described below.

**Every feature and every fix starts from `dev`.** Not `main`, not a release
branch, not whatever the last worktree happened to be sitting on. The only
exceptions are a change the maintainer has explicitly asked to be based
elsewhere, and a hotfix onto a release branch that has already been agreed --
in both cases said out loud, in the request, before the work starts. If nobody
said otherwise, the answer is `dev`.

**This rule is not yours to overrule, and in particular it is not overruled by
`dev` looking wrong.** It has already failed once exactly that way: an agent
saw `dev` sitting 55 commits behind `main`, concluded it was stale and
therefore the wrong base, branched from `main` instead, and wrote a paragraph
justifying it. The refs were local and had never been fetched. One
`git fetch origin` showed `dev` was five commits *ahead* with work `main` did
not have -- the reasoning was confident, articulate and built entirely on stale
data. So:

- **`git fetch origin` before you form any opinion about a branch.** A local
  ref is a memory of the remote, not the remote. Compare with
  `git rev-list --left-right --count origin/dev...origin/main`.
- **If `dev` still looks like the wrong base after fetching, stop and ask.**
  Say what you measured and why it looks wrong. Do not decide it yourself, and
  do not proceed while explaining the decision -- an explanation is not an
  approval.
- **Basing on `main` and rebasing onto `dev` later is not a shortcut, it is
  extra work with a hazard in it.** The release commits in `main`'s history
  come along for the ride, so the feature branch quietly carries the version
  bump that drops `-SNAPSHOT` into `dev`, which is meant to keep it. Recovering
  from that means replaying your own commits with `--onto`, and knowing you had
  to is not something the rebase tells you.

Keep branches short-lived and rebase onto `dev` often -- Keep branches short-lived and rebase onto `dev` often â€” a branch that sits for days turns into exactly the merge this is meant to avoid. Build before you merge, and don't merge or push unless the user asks. `main` and `dev` are protected on GitHub: no force-push, no deletion, and every merge arrives through a pull request with the `verify` CI job green, so a direct `git push origin main` is rejected by the server rather than by convention. Releases go `dev` -> `main` after hardware testing. The full rules are in CONTRIBUTING.md.

**Branching moves the risk rather than removing it.** More parallel branches means more merges, and the failure below is a merge-time failure â€” so it gets *more* important, not less.

### The one that bites silently

`APP_REGISTRY[]` (`src/engine/AppRegistry.cpp`) is the launcher spine. Each playable game's own `AppMetadata` now carries its launcher index, icon and default visibility, while the registry binds that metadata to a concrete `GameInstances` member. A bad merge can still duplicate or skip an index inside those metadata blocks, and the result still compiles; the symptom is a tile launching the wrong game or appearing in the wrong place.

After any merge, rebase, or conflict resolution touching `AppRegistry.cpp` or a game's local `AppMetadata`, re-run `python tools/check_catalog.py` before doing anything else.

### Serialise on the spine, parallelise on the leaves

- **Leaves â€” safe in parallel:** a game's own `src/games/*.{h,cpp}` pair. One agent per game is fine.
- **Spine â€” expect collisions:** `src/main.cpp`, `src/engine/AppRegistry.*`, `src/hal/Board.*`, `src/ui/Ui.*`. Adding a single game still touches the registry and `PLAYABLE_APP_COUNT`, even though title/icon/index/visibility metadata now lives with the game. Make spine edits tight and land them quickly rather than holding them open across a long task.

### The board is a singleton

There is one physical device and one serial port, and both are exclusive:

- `pio device monitor` holds the COM port; an `upload` from another agent will fail while it's open. Release it when you're not actively reading.
- **Flashing destroys shared physical state.** NVS holds touch calibration, profiles and scores that another agent may be mid-test against, and `factoryReset()` wipes all of it. Never flash or factory-reset on a shared board without saying so first.
- One `.pio/` build directory per working tree. Two concurrent `pio run` invocations in the same tree race on it â€” use a separate git worktree if you need to build in parallel.

### Take the lock before you build or flash

Worktrees isolate source and `.pio/`, but **not** the board, the serial port, or the toolchain's shared package cache. Those need an explicit lock, and it has to live somewhere every worktree can see â€” so it goes in the shared git common directory, not in `.pio/`:

```powershell
$lock = Join-Path (git rev-parse --git-common-dir) "gume-board.lock"
```

That path resolves to the same file from every worktree, and is never committed.

**Acquire** before any `pio run`, `pio run -t upload`, or `pio device monitor` â€” write your PID, the action, the worktree and a timestamp, so the next agent can tell whether you're alive:

```powershell
"$PID|flash|$(Get-Location)|$(Get-Date -Format o)" | Set-Content $lock -Encoding utf8
```

**Release it in all cases** when the build, flash or monitor session ends â€” including on failure. `Remove-Item $lock`.

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

**If a matching process is running, wait** â€” poll every ~10 s rather than busy-looping, and say what you're waiting on. Builds take a couple of minutes; a flash plus verify is under a minute. Keep waiting while the process lives.

**If no such process exists, the lock is stale â€” delete it and continue.** Stale locks are normal here: an agent that was interrupted, crashed, or had its command cancelled mid-build never got to release it. A leftover file must not be allowed to block every future agent, so clearing it is the correct action, not a workaround.

Say so plainly when you remove one â€” note the PID and timestamp you found â€” so it's visible if a build really was killed halfway and left `.pio/` in a bad state. A lock older than ~15 minutes with no live process is stale beyond doubt.

The same reasoning applies to any lock PlatformIO itself leaves in `~/.platformio` after an interrupted dependency install: confirm nothing is running, then clear it.

### Shared budgets

Flash is global and nearly the binding constraint (2,364,321 / 3,145,728 bytes,
**74.9%**; NimBLE plus the BT controller account for ~192 KB of that). RAM sits
at 72,628 / 327,680 (22.2%) -- higher than it was, deliberately: RowList traded
864 bytes of static RAM for zero heap traffic and storage diagnostics keep their
profile-move buffers static. On this device that is a good
trade every time. Two agents can each add artwork that fits locally and together overflow it. Read the size line from `pio run` and report it when you add data tables or images.

### Modularity rule

If a file is becoming large (as a rule of thumb, `src/main.cpp` > ~400 lines of active logic in a single function, or any `.cpp` > ~600 lines total), **refactor it into a more modular form first** before making the requested change. Split into helper files, break large functions into smaller ones, or extract a new class â€” whatever fits the existing architecture. The refactor must not break existing functionality (build must still succeed and behaviour must be unchanged), and it must land as its own commit before the feature change that prompted it.

This keeps diffs reviewable, conflicts locatable, and prevents any single file from becoming a merge hazard.

## Git hygiene

- Stage explicit paths. `git add -A` will sweep up another agent's half-finished work.
- Commit narrowly â€” one game or one concern per commit â€” so conflicts stay resolvable.
- Don't rebase or force-push shared branches, and don't revert changes you can't attribute; an unfamiliar edit is more likely someone's in-flight work than a mistake.
- Don't reformat, re-order includes, or opportunistically refactor files you aren't otherwise changing. Cosmetic churn in a spine file turns someone else's small diff into a merge conflict.
- `main` and `dev` are protected branches. Open a pull request against `dev`; never try to force-push or delete either, and do not use an admin bypass to skip a failing check.
- `src/games/CountryDataTable.cpp` is generated. Regenerating it rewrites the whole file â€” announce it rather than folding it into an unrelated change.

## Architecture

`setup()`/`loop()` in `src/main.cpp` delegate to a `BrainoApp` singleton defined in `src/engine/AppRuntime.*`, which owns every screen as a `static` instance and implements `GameHost`.

Runtime views are now only **Game** (including Launcher, Profiles, Settings and ordinary games), **ScreenSaver** (self-playing Pong that mirrors rally colour onto the case LED), **Asleep** (backlight off, panel in low-power state) and **Locked** (the hold-to-unlock guard between either of those and the screen underneath). Boot opens the Profiles app first; after a profile is chosen, `goHome()` activates `LauncherGame` through the same `begin`/`update`/`render` lifecycle as the rest of the screens.

The idle path is driven by `Board::idleAction()`: `SaverThenSleep` runs the
saver and then blanks after `sleepSeconds()`, `SleepOnly` blanks straight away
at `screenSaverSeconds()`, `SaverOnly` never blanks. **`View::Asleep` polls at
`SLEEP_POLL_MS` (100ms) rather than the 20ms frame budget** â€” there is nothing
to draw, and holding 50Hz behind a dark screen defeats the point. It is panel
sleep, not `esp_deep_sleep`: the CPU must stay up to poll touch, since no wake
source is wired. `Board::displayWake()` blocks ~120ms for the panel, so its
call site sits inside a `Watchdog::Pause` guard.

`View::Locked` sits between both idle views and the screen underneath, and is
gated on `Board::wakeLockEnabled()` (default on, RAM-mirrored, global like
every device setting). It is an **accidental-touch guard, not access control**:
it is disjoint from the admin PIN, neither granting nor revoking admin, and
`resumeUnderlyingScreen()` -- the single tail shared by `exitScreenSaver()`,
`wakeFromSleep()` and the unlock -- returns to exactly the screen, profile and
orientation that were up before. Three things about it are load-bearing:

- **The press that got you here is swallowed.** `enterLock()` sets
  `swallowTouch_`, so a press held through a bag can never complete the
  gesture however long it lasts.
- **Text on this screen is measured against the live panel, never trusted.**
  Two different things chop a line and they look different on the device:
  `Ui::fitted()` truncates and ends in a `.`, while TFT_eSPI drops characters
  outright once x reaches the viewport's right edge (`drawChar`: `if (xd >=
  _vpW) return`) -- no mark, cut mid-word. **`tools/gen_screens.py` cannot
  reproduce either**: PIL has its own font metrics and clips nothing, so a
  mock-up showing a sentence whole is not evidence that the panel does. The
  footer therefore picks the longest wording that measures whole and
  `drawSentence()` wraps and clamps it, and `renderLock()` resets the viewport
  because this screen owns the panel and must not inherit anybody's clip.
- **The hold tolerates dropouts.** Resistive contact falls below
  `TOUCH_PRESSURE_THRESHOLD` mid-press as a matter of course, so gaps up to
  `LOCK_CONTACT_GRACE_MS` (150ms) do not restart the timer.
- **The header is fixed, and the vertical stack is measured against 240px.**
  The wordmark and battery badge answer what a person finding a locked
  device wants to know without touching it. Nothing drifts: the saver moves
  its wordmark because it is up for hours, this screen for
  `LOCK_TIMEOUT_MS`, so the header is painted once inside the
  `lockFullPaint_` branch and the progress bar stays the only thing
  repainted per frame. The whole stack hangs off `lockButtonRect()`, whose
  offset below centre went from +12 to +25 to clear the new hairline;
  landscape is the tight case and every gap is stated in the comment there.
  The battery badge is variable width, so it is placed off
  `Ui::batteryBadgeWidth()` rather than a constant -- same rule as the
  launcher header.
- **`Locked` is excluded from the idle-timeout block** alongside `ScreenSaver`
  and `Asleep`; it runs its own `LOCK_TIMEOUT_MS` and hands back to sleep (or
  to the saver under `SaverOnly`). Leaving it in that block re-arms the saver
  timer against an already-expired `lastActivityMs_`.

`BrainoApp::lockAndSleepNow()` is the deliberate way in -- the Lock button --
and it is the same guard, not a second one: it sleeps through the ordinary
`enterSleep()`, leaves `activeGame_` alone and comes back through
`resumeUnderlyingScreen()`. Two things go with it:

- **`lockOnWake_` overrides the setting, one time.** Both wake paths gate on
  `wakeLockEnabled() || lockOnWake_`, so pressing Lock produces the lock screen
  even for an owner who has switched Hold to unlock off -- that press *is* the
  request. `resumeUnderlyingScreen()` clears it, because that is already the
  one place that decides the lock is over.
- **The Lock tap is consumed by the runtime, above the active screen**, beside
  the Home and gear routing and for the same reason: a tap delivered to the
  screen as well would press whatever sat under the padlock, and the user would
  find it done when they unlocked. Its slot comes from
  `LauncherLayout::topBarLockRect()` (top bar) or `LauncherLayout::lockRect()`
  (the launcher, which draws no top bar), and both the glyph and the hit test
  read those same helpers.
- **The top bar has no spare pixels, and Lock was paid for.** Home narrowed
  from 42px to 32px and the title's start moved from 48 to 62, costing the
  title 14px. The right-hand cluster is measured, not padded, and cannot give;
  check the longest screen title ("Finger Counting") before spending any more.
- **The BOOT key is Home, and it is a shortcut rather than a route.** It is
  consumed in the runtime above the active screen's `update()`, beside the
  Home, gear and Lock routing and for the same reason. Three deliberate holes
  in it: the launcher does not consume it (you are already home, and taking
  the frame would drop a simultaneous touch), the lock screen ignores it
  entirely (a key pressed through the side of a bag is the accident that
  screen exists to catch), and `View::Asleep` and the saver treat it exactly
  as a touch. It counts as activity, or the saver would arrive a moment after
  you pressed Home. **Nothing may become reachable only through it** --
  `BOARD.hasBootButton()` can be false and the console has to remain complete.
  It fires on the press edge, which is what a hold gesture would have to
  change first; see `src/hal/BoardButton.cpp`.

| Layer | Where | Responsibility |
|---|---|---|
| `Game` / `AppGame` / `GameHost` | `src/engine/Game.h` | Screen lifecycle plus the split between ordinary app context and privileged system host |
| `Board` | `src/hal/Board.h` / `BoardAccess.h` | Hardware aggregate plus narrow display/touch/storage/power/network/feedback facades |
| `Ui::Renderer` | `src/ui/Renderer.h` / `TftRenderer.h` | Driver-free RGB565 drawing interface plus the firmware TFT adapter |
| `Ui` | `src/ui/Ui.h` | Stateless themed drawing helpers; owns the colour palette |
| GameCatalog | src/engine/GameCatalog.h | Derived compatibility view over playable-game metadata |
| AppRegistry | src/engine/AppRegistry.h | Single source of truth for launchable apps and instance bindings |
| `Sound` / `BoardAudio` | `src/hal/Sound.h` / `BoardAudio.cpp` | The console's sound vocabulary, and the synthesiser that generates every one of them a sample at a time |
| `Watchdog` | `src/hal/Watchdog.h` | Background supervisor: reboots a hung loop, logs stalls and heap, keeps a crash breadcrumb |
| `BleBeacon` | `src/hal/BleBeacon.h` | Opt-in non-connectable BLE presence beacon. Owns the one authoritative advertisement payload, and its inverse `decode()` |
| `BleScan` | `src/hal/BleScanner.h` | Passive observer for other Braino beacons. Radio only -- no opinion about scores |
| `NearbyPlay` | `src/engine/NearbyPlay.h` | Nearby play policy: peer scores, header notifications, the sharing switch |

### Invariants worth knowing before editing

- **The board is described in two files and nowhere else.** `include/boards/<id>.h` holds the whole board -- pins, rotations, the battery divider, which peripherals exist; the `[board_<id>]` section in `platformio.ini` holds only what TFT_eSPI must be told at compile time. **No file under `src/` may name a GPIO number, a panel size or a divider ratio.** If you need one, read it off `BOARD`; if `BOARD` does not have it, add the field to `include/BoardProfile.h` and fill it in for every existing board in the same commit. `tools/check_boards.py` enforces the completeness, and `BoardConfig.h` static_asserts the overlap with TFT_eSPI. See `docs/PORTING.md`.
- **Some boards cannot be supported, and that is a compile error, not a degradation.** Braino needs a panel of at least `GAME_CANVAS_WIDTH`x`GAME_CANVAS_HEIGHT` in landscape, a touch controller, the backlight on a GPIO, and 4 MB of flash. `BoardConfig.h` static_asserts each with a message naming what is missing. Optional hardware -- SD slot, RGB LED, speaker, battery sense -- is `PIN_NONE` plus a `has...()` guard, and the firmware does without it. **Do not blur the two:** turning a requirement into a quiet degradation ships a board that flashes and then cannot be read or pressed.
- **A supported board must be flashable from the web installer.** `tools/gen_site.py` derives the picker's board list from the `[board_*]` sections rather than a hand-kept list, and refuses to generate until a new board has a `BOARD_DETAILS` label, an offered environment and a CI build behind it. `tools/check_boards.py` reports the same gaps without a build. "Supported" means someone who owns the board can flash it from the page, not that it builds here.
- **`SCREEN_WIDTH` / `SCREEN_HEIGHT` are derived, not stated.** They come out of `BOARD.screenWidth()` / `screenHeight()`, which rotate the panel's native size by the profile's landscape rotation. Do not reintroduce a literal 320 or 240 next to them; a board that states its size twice will eventually state it two different ways.
- **Every screen is a `Game`.** Launcher, Settings, Wi-Fi, Profiles, Scores, System Info and About are all `Game` subclasses with the same `begin`/`update`/`render`/`end` lifecycle.
- **`end()` is called on every screen change** via `BrainoApp::leaveActiveGame()`, before the next screen's `begin()`. Add new transitions through that funnel, not by assigning `activeGame_` directly. Override `end()` for anything a screen holds that outlives a frame; nothing here runs off a task or timer, and the hook is what keeps that true.
- **Never sample the battery ADC more than once per frame.** `Board::readBatteryTelemetry()` caches for 2s and everything else reads through it. Each accessor used to run its own blocking 10ms conversion, and a top bar calls two of them. See `src/hal/CLAUDE.md`.
- **Ordinary games should not receive the full board anymore.** Use `AppGame` + `AppContext` for catalog games; that surface is limited to `Ui::Renderer` drawing, content, scoped persistence, feedback and basic navigation. The only screens still on `GameHost&` are Launcher, Settings, Wi-Fi, Profiles, Scores, About and System Info, and system screens must guard privileged actions with `requireCapability()`.
- **Profile scoping is automatic and invisible to games.** `Board::scopedKey()` is **private**; it prefixes `p{N}_` and translates plain game keys into compact app-scoped leaves inside `getScore` / `setScore` / `saveBestScore` / `worstScore` / `loadBlob` / `saveBlob`. `BoardStorage.cpp` owns the schema-versioned migrator from the older key format; `BoardStorageMaintenance.cpp` owns NVS usage telemetry and profile deletion: removing a player clears that slot's `pN_` keys, shifts later slots down with their own persisted data, and clears the old last slot. Just call the storage API with a plain key and per-profile behaviour comes for free. Guest (`GUEST_INDEX == 5`) silently **drops all writes** â€” that is what makes it a guest rather than a sixth player.
- **Device settings are global, not per-profile**: theme, layout, brightness,
  sound on/off, volume, Wi-Fi credentials, NTP, NTP resync interval, timezone.
  Sound belongs on that list for a reason worth stating: the speaker belongs to
  whoever is in the room, and a console that came back loud because a different
  player picked it up is a poor thing to hand a child in a quiet house. Per-profile: scores, mastery blobs, game visibility.
- **The admin PIN gates the two routes into the admin profile**: switching to
  it, and opening its Edit menu (rename plus its per-player game list). One
  profile is admin (`Board::adminProfileIndex()`, one `uint16_t` PIN beside it
  in NVS). Add a third way to become admin and it needs the same gate. It is
  asked **every time**, including when already admin: being admin is not
  evidence about who is holding the device, which is the whole threat model.
- **Per-player game visibility and profile removal are admin-only; renaming is
  not.** `ProfileGame` gates on `board.isAdminProfile(board.activeProfile())`
  — the *actor*, not the profile being edited. Those two are different
  questions and conflating them is exactly how Remove ended up available to
  every player. The Games list stays readable by anyone on purpose: a player who
  can see a game is switched off is better served than one facing a launcher
  that is short for unexplained reasons.
- **Settings is readable by everyone and writable only by the admin.** There is
  no lock screen on it. The enforcement is a single `if (!isAdmin(board))`
  early return in `SettingsGame::update()`, sitting *below* tab switching so a
  non-admin can still page through and read. The greyed-out controls are a
  drawing decision and enforce nothing on their own: for a while every greyed
  row was still live and a player could toggle the lot. If you add a control,
  it is covered by that early return automatically — do not add a path above
  it.
- **Boot must not leave the admin profile active.** `BrainoApp::begin()`
  drops to Guest if it finds admin selected. The picker's Done button goes home
  with whatever is already active, so a remembered admin selection is a PIN
  bypass, not a convenience. Do not "restore the last profile" here.
- **A PIN's digit count is not derivable from its value.** `0000` and an empty
  field are both zero, so every PIN entry point tracks digits separately from
  the number, and only judges an entry once it is exactly four long. Both
  screens got this wrong first time and silently accepted a three-digit prefix.
- **Both PIN pads are laid out against the live `tft.width()`/`height()`.** The
  first version hard-coded rows at y=220 and the action buttons at y=270 on a
  240px-tall panel, so the bottom row, DEL and OK were all drawn off the screen
  and there was physically nothing to press. Anything added to either pad must
  still end above `screenH`.
- **Each playable game declares its own metadata once.** `AppMetadata` owns id, title, screen title, subtitle, launcher label, blurb, score pointer, launcher icon, launcher index and default visibility. `APP_REGISTRY` only binds that metadata to the concrete static instance.
- **`APP_REGISTRY` holds the 31 playable games plus 7 launchable system apps.** The launcher itself is not a tile in that table; it is `LauncherGame`, activated by `goHome()`.
- **Metadata launcher indices must stay contiguous and index-aligned.** `check_catalog.py` enforces this now, but the failure mode is still the same: a misalignment launches the wrong game from the right tile.
- **The launcher shows the profile name as plain text, not a button.** The framed chip is what overlapped the status badges; the name itself is wanted. `launcherProfileRect()` is both where it draws and the touch target, so the two cannot drift â€” in landscape it sits after the byline, not across it.
- **The launcher status badges are packed to the pixel.** Landscape runs from a hairline at `lW-138` to the gear at `lW-30`, and the Lock badge sits at its left-hand end. The battery badge is **variable width** -- it carries its own percentage, so it is 22px at `72` and 36px at `100` on the charger -- and in that widest state the row has about 4px spare. Everything on it is therefore laid out right-to-left off `Ui::batteryBadgeWidth()` and the *measured* width of the clock string, never a constant offset; the hairline has moved out twice to buy those pixels -- `lW-110` to `lW-116` for the battery percentage, then to `lW-138` for the Lock badge -- and `LauncherLayout::profileRect()`'s right limit moved with it both times. Lock is a **badge, not a control**: it is drawn at 18px beside the battery and Wi-Fi glyphs rather than at the gear's 26px, because it belongs to that family and a gear-sized padlock read as the most important thing on the header. Portrait has room to extend the badge row instead. Anything new in that header needs the same treatment â€” measure, don't guess.
- **The BLE advertisement has exactly one description.** `BleBeacon::Advertisement`
  is compiled into a raw AD buffer that is handed to the controller verbatim,
  and the System Info BLE tab reads that same buffer back. `BleBeacon::decode()`
  is the exact inverse and is what the scanner reads peers with -- never write a
  second parser. With Nearby play on the payload is **exactly 31 bytes**, so
  there is no room for another AD structure or a longer name. See
  `docs/BLE_BEACON_SPEC.md`.
- **Nearby play is off by default and gated on the beacon.** `NearbyPlay::tick()`
  re-derives that gate every frame rather than trusting an ordering contract with
  Settings, so turning the radio off takes the feature with it. What it shares is
  a game index and a best score, never a name or anything profile-scoped.
- **There are no audio files, and there must never be one.** Every sound the
  console makes -- the cues in `hal/Sound.h`, the four Cinnamon pad notes, and
  the spoken "Let's play Braino!" at boot -- is *generated* by `BoardAudio.cpp`
  from a script of oscillator, noise and formant segments. No WAV, no PCM
  table, no sample bank, and nothing decoded at runtime. This is a flash rule
  before it is an aesthetic one: one second of 16-bit 16kHz mono is 32 KB, so
  the vocabulary as recordings would cost more than the whole game catalogue's
  artwork, on a budget already at 74.9%. As synthesis it is under a kilobyte.
  The spoken phrase is a phoneme table, not text-to-speech -- there is no
  dictionary and there is no second phrase; adding one means writing its
  phonemes out by hand, which is the intended cost.
- **Mute is gated in exactly one place, `Board::playSound()`.** Every sound in
  the firmware goes through that door -- both beeps and the boot phrase
  included -- so a switch labelled Mute cannot leave something still audible.
  The RGB pulse is deliberately *not* gated: `beepOk()`/`beepError()` pulse
  before they call it, so muting takes the sound and leaves the light, which is
  the whole of the feedback on a codec-less board anyway. `soundEnabled()` and
  `volume()` are RAM-mirrored write-through settings because the first is on
  the path of every cue in every game.
- **A screen makes a noise through `playSound(Sound::...)` and nothing else.**
  `Board::beep(freq, ms)` is private on purpose. A shared vocabulary is the
  point -- `Coin` means the same thing in Whack-a-Mole as in Memory, and a game
  picking its own frequencies is exactly how that stops being true. Cinnamon's
  four pads are the one pitched exception and they are *in* the vocabulary for
  that reason. Adding a cue is adding a word to a language: do it when a game
  has something genuinely new to say, not when an existing cue is nearly right.
- **A cue is armed, never played.** `playSound()` copies a script and returns
  in microseconds; `Board::tickAudio()`, called once per frame beside
  `tickRgb()`, generates only as many samples as the I2S DMA will take without
  blocking. Never write a blocking `i2s_write` on a render path -- a 300ms note
  is fifteen frame budgets and `Watchdog` will log the stall. `src/s3_diag.cpp`
  does block, correctly, because a bring-up probe has no frame budget.
- **The loop is watchdogged.** `Watchdog::feed()` is the first statement in `BrainoApp::loop()` and a frame over `TIMEOUT_SECONDS = 12` reboots the device. Anything that blocks the loop task for longer on purpose â€” a calibration wizard, a network round trip â€” must sit inside a `Watchdog::Pause` guard, or it will look exactly like a hang. See `src/hal/CLAUDE.md`.

## Adding a game or an app â€” the whole checklist

The old version of this list had five items, all of them code. Everything that
has since been shipped broken or stale was *outside* those five: the README game
table, the screenshot gallery, `docs/screens/`, the changelog. A game that
launches correctly and is invisible in every document describing the product is
not finished.

Work through all four groups. Nothing here is optional for a screen that ships.

### 1. Code â€” miss one and it fails silently or won't link

1. `src/games/NewGame.{h,cpp}` â€” subclass `AppGame` unless it is a privileged system screen.
2. In the game's `.cpp`, declare one `AppMetadata` block. Blurbs render at font 1
   across ~292 px, so keep them under ~46 chars. Put the launcher icon,
   launcher index and default visibility in that metadata block, not in the registry.
3. If it records a score, declare one local `AppScoreInfo` and point the metadata at it.
4. `src/engine/AppRegistry.cpp` — add a `metadataCatalogApp(...AppMetadata(), instance)` entry at the intended playable position.
5. `src/engine/AppRegistry.h` — update `PLAYABLE_APP_COUNT`.

### 2. Docs â€” the part that actually gets forgotten

6. `README.md` â€” a row in the right game table (what it is, what it builds,
   age), or a bullet under the system screens if it is an app.
7. `README.md` â€” an `<img>` in the matching screenshot gallery.
8. `CHANGELOG.md` â€” an entry under the unreleased heading.

### 3. Screens

9. `tools/gen_screens.py` â€” a render function plus an entry in `SCREENS` or
    `EXTRA_SCREENS`. Take the geometry from the game's own `Rect` helpers so
    the mock-up matches the device rather than approximating it.
10. Run `python tools/gen_screens.py` and **look at the PNG**. It is a
    generated image; nothing else will tell you it came out wrong.
11. `tools/gen_site.py` — add every new still to `PLAYABLE_STILLS` (or
    `SYSTEM_SHOWCASE` for a system app), keyed by app id, and give each one a
    `SCREEN_CAPTIONS` entry. **A still on disk that the site does not
    reference makes `gen_site.py` refuse to generate**, which fails the Pages
    deploy rather than the build. Elements shipped without this and the site
    went undeployed for two pushes.

### 4. Verify

12. `pio run` â€” and put the new flash/RAM figures in `README.md` **and**
    `CLAUDE.md`. They are the two places that disagree.
13. `python tools/check_docs.py` â€” must be clean.
14. Flash it and actually play it. Take the board lock first.

### What you do NOT hand-edit

These derive from `AppRegistry` and update themselves. Editing them by hand is
how About fell six games behind in the first place:

- the **About** app's game list, count and blurbs
- the **Settings â†’ Games** visibility list
- the **launcher** tiles and paging
- the **GitHub Pages site** â€” `tools/gen_site.py` reads the version from
  `AppVersion.h`, the game list and blurbs from `AppRegistry`, the build
  figures from `README.md` and the board name from `platformio.ini`. Edit
  `site/index.template.html` for wording and layout only; `check_docs.py`
  fails if a version number is typed into it. The gallery is generated too, so
  do not add an `<img>` to the template to show a new screenshot — add the
  still to `PLAYABLE_STILLS` per step 11 above

### If it is a system app rather than a game

Everything above still applies, plus:

- It **must work in both orientations.** Read `tft.width()` / `tft.height()` at
  render time, never `SCREEN_WIDTH` / `SCREEN_HEIGHT`. Settings and Wi-Fi
  currently violate this and are the reason the rule is stated so bluntly.
- If it touches a radio, stores data, or changes what leaves the device, the
  **About radios page** and the **README privacy section** are part of the same
  change â€” and About must *read* the state, not restate it.

## Rendering model

App-facing rendering uses `Ui::Renderer`: a driver-free RGB565 primitive
interface implemented on hardware by `Ui::TftRenderer` over `TFT_eSPI`. HAL
bring-up, calibration and BMP blitting may still use the raw panel driver, but
games and shared UI helpers should not. There is still no framebuffer. A full
320Ã—240 wipe pushes ~150 KB over SPI â€” roughly 30 ms of visible blanking â€”
which is why `Game` carries two levels of invalidation:

- `markDirty()` â€” content changed; repaint moving parts only.
- `markFullDirty()` â€” layout changed; repaint background/chrome too.
- `render()` should guard static chrome behind `if (needsFullRender())` and draw dynamic parts unconditionally.

These three are `protected`; the public surface is `needsRender()`, `clearDirty()`, and `requestRender()` (which forces a full repaint, used when returning to a screen). First paint is always full.

**Clip scrolling content with `tft.setViewport(x, y, w, h, false)`** and reset it after. Skipping rows that fall entirely outside the viewport is not enough â€” the row straddling the edge still draws in full and smears into the chrome above it, which is what System Info did into its own tab strip. `vpDatum=false` keeps drawing coordinates absolute, so nothing else in the draw loop changes.

Most games still repaint wholesale. Cinnamon is the reference for partial redraw â€” it was also a photosensitivity concern at full-flash rates, so prefer partial redraw for anything that updates rapidly.

Playable games are authored against a fixed 320Ã—240 landscape canvas. Launcher and system/UI apps support portrait (`LayoutMode::Vertical`; the launcher uses 4 tiles/page vs 6 in landscape).

**System/UI apps** (Settings, Wi-Fi, SystemInfo, Profiles, Scores, About, and any future app-style screens beyond the playable game catalog) must support **both landscape and portrait orientations**. They must read `tft.width()` / `tft.height()` at render time rather than the compile-time constants `SCREEN_WIDTH` / `SCREEN_HEIGHT`, and lay themselves out responsively. Use `Ui::drawTab()` + `Ui::drawTabBaseline()` for multi-section content; the tab strip width adapts by dividing `tft.width()` at render time.

## Layout

```
include/BoardProfile.h    the contract every board fills in
include/BoardConfig.h     selects one profile; derives SCREEN_* from it
include/BuildStamp.h      branch/commit/build-time accessors
include/boards/           one header per supported board -- the ONLY place
                          in the tree that names a GPIO      include/AppVersion.h
src/main.cpp              bringup entrypoint + normal app setup/loop
src/BuildStamp.cpp        which build this is; recompiled every build
src/wifi_diag.cpp         standalone radio test (env:wifidiag only)
src/s3_diag.cpp           standalone ESP32-S3 bring-up probe (env:s3diag only)
src/diag4.cpp             standalone 4-inch ST7796 bring-up probe (env:diag4 only)
src/engine/               Game, LauncherGame, GameCatalog, AppRegistry, NearbyPlay,
                          AppRuntime, AppRuntimeLock, ScoreCatalog, Progress,
                          RecentQuestions, ContentLoader
src/games/                one .h/.cpp pair per game + GameInstances.h +
                          Country/State, Maze and Trace data.
                          Settings is three .cpp against one header --
                          SettingsGame (tabs + routing), SettingsPanels
                          (the tab bodies), SettingsPin (the PIN pad)
src/hal/                  Board bring-up, BleBeacon, BleScanner, BoardAccess facades,
                          per-concern HAL units, BoardAudio (the synthesiser),
                          Sound.h (the cue vocabulary), BoardButton (the BOOT
                          key), BoardStorage, storage
                          maintenance, TouchTypes,
                          Clock, Watchdog
src/ui/                   Renderer, TftRenderer, Ui, LauncherIcons,
                          LauncherLayout
tools/                    gen_screens.py, gen_site.py, check_docs.py,
                          check_boards.py, check_catalog.py,
                          check_frame_rules.py, build_stamp.py,
                          pack_release.py
site/                     index.template.html â€” the GitHub Pages landing page
.github/workflows/        ci.yml validates checks + builds; pages.yml publishes
                          the site from the same firmware set;
                          release.yml publishes a tagged release with
                          every firmware image attached
docs/                     SD_CONTENT_SPEC.md, PORTING.md, screens/
cases/                    printable enclosures, one folder per BOARD_NAME;
                          optional -- a board is supported without one
```

## Cutting a release

A release is a tag. Everything else is automatic:

```bash
git tag -a v5.0.1 -m "Braino! 5.0.1" && git push origin v5.0.1
```

`.github/workflows/release.yml` then builds every environment
`platformio.ini` declares, packs them with `tools/pack_release.py`, and
publishes a GitHub release with all four parts plus a single `-merged.bin`
per environment, `SHA256SUMS.txt` and `FLASHING.txt`.

Before tagging, on `main`:

1. `include/AppVersion.h` carries the real version -- **not** a `-SNAPSHOT`.
   The workflow refuses to publish one, because `dev` carries a snapshot
   between releases by design and the first mistaken tag would otherwise
   publish a "release" the firmware itself calls unreleased.
2. `CHANGELOG.md` has a `## <version>` section with that day's date. The
   release notes are lifted from it verbatim -- notes written by hand are a
   second changelog that agrees with the first only on the day it is written.
3. The build figures in `README.md` and `CLAUDE.md` are from a build of the
   branch being released. **`tools/build_stamp.py` compiles the branch name
   into the image**, so a figure measured on a feature branch is a few bytes
   out on `main`; measure with `GITHUB_REF_NAME=main pio run -e app`, which is
   what CI does.

   **`BRAINO_VERSION` is in the image too, so measure after the version bump,
   not before.** Dropping `-SNAPSHOT` is nine characters and moved the 5.2.0
   figure by 16 bytes, which shipped to `main` wrong because the build was run
   on the tree as it stood before the release commit. The consequence is that
   `dev` and `main` legitimately carry different numbers between releases --
   2,364,321 on `5.3.0-SNAPSHOT` against 2,353,205 on `5.2.0` -- and that is
   not drift to be reconciled. `check_docs.py` compares each document against
   whatever `.pio/build/app/firmware.elf` is sitting in *your* tree, so each
   branch has to state its own figure or the checks fail for anyone who builds
   it.

   The figure is also not portable across hosts: the same commit is 172 bytes
   smaller in flash and 48 smaller in RAM on the Linux runner than on the
   Windows machine these numbers were read from. Pinning the platform and the
   libraries fixes what the build is made of, not which toolchain binary
   assembles it. Read the number from your own `pio run`; do not copy one out
   of a CI log.

The tag and `BRAINO_VERSION` must agree, and the workflow fails if they do
not. That check exists because a published release cannot be quietly
corrected: people have already downloaded it.

Afterwards, open the next version on `dev` as a `-SNAPSHOT`, so a board
flashed from `dev` cannot be mistaken for the release it is ahead of.

## The web installer is part of the deliverable

`https://iamankushpandit.github.io/Gume/` flashes a board from the browser over
Web Serial. `.github/workflows/pages.yml` builds all four PlatformIO
environments on every push to `main`, runs `tools/gen_site.py`, and drops the
resulting `.bin` files beside the manifest that points at them. Nobody
regenerates it by hand, so it cannot go stale on its own â€” but three things
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
3. **Making CI unable to build.** `platformio.ini` is expected to build on a
   clean checkout, locally and in GitHub Actions. If `lib_deps` changes shape,
   keep both `.github/workflows/ci.yml` and `.github/workflows/pages.yml`
   building all four environments in the same commit.

`python tools/gen_site.py` writes `site/_build/` locally so you can look at the
page. The flash button will 404 there â€” the binaries only exist in CI.

## Hardware notes

Pins live in `include/boards/<board>.h` -- one profile header per supported
board, and the only place in the tree that names a GPIO. Read the profile
rather than trusting generic ESP32 pinouts online, and never copy a pin map
between CYD variants: GPIO34 is battery sense here and the light sensor on the
ESP32-2432S028R. `docs/PORTING.md` is the checklist for adding a board.

- **The RGB LED's red and green lines are crossed on this unit** relative to the usual standard pinout â€” `rgb.r = 16`, `rgb.g = 4`, `rgb.b = 17` in the E32R28T-1 profile. This is already corrected there and verified on hardware; do not "fix" it again. Common anode, so drive is inverted â€” which the profile states rather than the driver assuming.
- Touch is bit-banged SPI (the TFT owns HSPI), 3-point affine calibration persisted in NVS behind a magic number. `touch.pressureThreshold = 350`, `touch.hitSlop = 8` in the profile.
- Backlight brightness floors at `Board::BRIGHTNESS_MIN = 25` â€” at lower duty the panel is unreadable and a player could not see the slider to undo it.
- `audio.speakerPin = 26` on the E32R28T-1 is a bare pin with no codec behind
  it, so `GUME_HAS_AUDIO_CODEC` is 0 there and the whole synthesiser compiles
  out: `playSound()` is a no-op and `beepOk()`/`beepError()` are the RGB pulse
  and nothing else. Sound is real only on a board whose profile describes a
  codec -- today the Freenove FNK0104B.
- Wi-Fi/NTP is a non-blocking state machine driven by `tickTimeSync()` each frame, with a raw-UDP `ntpUdpProbe()` fallback for when lwIP's SNTP never answers. The success-path automatic resync interval is a cached global setting, 1–24 hours with a 6-hour default; boot sync, manual sync and failure retries are separate. Timezone comes from a named POSIX zone or public-IP lookup â€” routers don't advertise one in practice.

## Conventions

- Hit testing: `Rect{...}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)`. `Rect` is in `Ui.h`, `TouchPoint` in `hal/TouchTypes.h`.
- Board facts come from `BOARD` (`include/BoardProfile.h`), never from a literal. A peripheral a board does not wire is `PIN_NONE`, and the caller guards with `BOARD.hasSdSlot()`, `hasRgbLed()`, `hasSpeaker()`, `hasBatterySense()` or `hasBacklightControl()`.
- Feedback: `board.beepOk()` / `board.beepError()`.
- Draw through `Ui::` helpers so the Dark/Light theme is respected; avoid hardcoded colours outside icon art.
- `src/games/CountryDataTable.cpp` is generated â€” edit `tools/gen_country_facts.py` and regenerate.
- `swallowTouch_` in `main.cpp` suppresses the first press after a rotation change or screen-saver dismissal, preventing a phantom tap on freshly drawn UI.

