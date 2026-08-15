# Contributing

GUme is a small firmware project with a large code surface: 28 built-in games,
system apps, hardware drivers, generated screenshots, a web installer and CI.
The code in this repo has largely been built with Claude or other coding agents
under human direction, so the repo includes explicit instructions and machine
checks for both people and agents. Read those instructions first; they are part
of the engineering process, not side notes.

Start here:

- `AGENTS.md`: repo-level rules that apply to every agent or assistant.
- `CLAUDE.md`: architecture, build commands, invariants and workflow.
- `src/engine/CLAUDE.md`: screen lifecycle, app registry, progress tracking.
- `src/games/CLAUDE.md`: how games and system screens are structured.
- `src/hal/CLAUDE.md`: board, storage, profiles, watchdog and hardware rules.
- `src/ui/CLAUDE.md`: renderer, theme, widgets and drawing helpers.

These files exist because multiple clients can work on this repo at the same
time. Follow them even when you are not using Claude. They explain how to keep
changes small, avoid stepping on another contributor's work and preserve the
firmware's performance and memory constraints.

## Workflow

1. Create or use a branch for one concern. Do not merge into `main` until the
   change has been tested and reviewed.
2. Prefer a separate worktree for new requirements:
   `git worktree add ../GUme-<slug> -b fix/<slug>`.
3. Check the worktree before editing: `git status --short --branch`.
4. Read the relevant `CLAUDE.md` file before changing a subsystem.
5. Keep the change scoped. Stage explicit paths; do not use `git add -A` in a
   mixed worktree.
6. Do not add AI attribution to commits. No `Co-Authored-By` lines naming an
   AI tool, and no generated-by footers.
7. Update docs in the same commit when behavior, architecture, screens,
   settings, dependencies, build figures or privacy claims change.

## Building And Flashing

Build the app:

```bash
pio run
```

Build every firmware environment:

```bash
pio run -e app -e bringup -e wifidiag
```

Before any build, upload or serial monitor session, take the shared board lock.
It lives in the common git directory so every worktree sees the same lock:

```powershell
$lock = Join-Path (git rev-parse --git-common-dir) "gume-board.lock"
"$PID|build|$(Get-Location)|$(Get-Date -Format o)" | Set-Content $lock -Encoding utf8
```

Release it when the command finishes. If a lock already exists, read
`CLAUDE.md` for the stale-lock protocol before touching it.

Flash the connected board only when you are ready to overwrite the device state:

```bash
pio run -e app -t upload
```

Flashing can disturb calibration, profiles and scores on the shared physical
board. Say what you are doing before flashing in a shared session.

## Checks

Run the checks that match your change. For normal firmware work, run:

```bash
python tools/check_catalog.py
python tools/check_frame_rules.py
python tools/check_docs.py
pio run -e app -e bringup -e wifidiag
```

If a screen layout changed, regenerate screenshots:

```bash
python tools/gen_screens.py
```

Then inspect the changed PNGs in `docs/screens/`.

## Adding A Game

Use `src/games/CLAUDE.md` and the full checklist in `CLAUDE.md`. The short path
is:

1. Add `src/games/NewGame.h` and `src/games/NewGame.cpp`.
2. Subclass `AppGame` unless the screen needs privileged system access.
3. In the game's `.cpp`, declare one `AppMetadata` block with the stable id,
   title, subtitle, blurb, launcher icon, launcher index and default visibility.
4. Add an `AppScoreInfo` only if the game records a score.
5. Add the game instance to `src/games/GameInstances.h`.
6. Bind metadata and instance in `src/engine/AppRegistry.cpp`.
7. Update `PLAYABLE_APP_COUNT` in `src/engine/AppRegistry.h`.
8. Add README game table text and screenshot references.
9. Add or update a renderer in `tools/gen_screens.py`, then run it.
10. Run catalog, frame, docs and PlatformIO checks.
11. Flash and play the game on the device.

Keep game code on the app-facing API: `AppContext`, `Ui::Renderer`, scoped
storage and feedback helpers. Normal games should not include `Board.h` or draw
directly through `TFT_eSPI`.

## Adding A System App

System apps are screens such as Settings, Wi-Fi, Profiles, Scores, About and
System Info. They can use `GameHost` only when they truly need privileged board
access.

System apps must:

- Work in both landscape and portrait.
- Read `tft.width()` and `tft.height()` at render time.
- Use `requireCapability()` before privileged actions.
- Keep privacy and radio claims in README and About accurate.
- Derive catalog facts from the registry rather than restating them.

## Fixing A Bug

Start from the symptom and keep the repair narrow:

1. Reproduce or inspect the affected game or screen.
2. Read the subsystem `CLAUDE.md` file.
3. Preserve the dirty/full-dirty rendering model. Do not replace it with a
   generic UI framework.
4. Avoid heap churn in hot paths. Prefer fixed buffers and `snprintf` over
   Arduino `String` in per-frame logic.
5. Add or update focused checks when the behavior can be tested on the host.
6. Regenerate screenshots if layout changed.
7. Run the relevant checks and build all environments.
8. Flash and verify on the board when the bug is visual, touch-related,
   storage-related or hardware-related.

Small fixes are preferred. If a file grows past the modularity thresholds in
`AGENTS.md` or `CLAUDE.md`, split it first and keep that refactor separate from
the behavior change when possible.

## Persistence And Profiles

Do not hand-build NVS keys. Use the storage helpers on `AppContext` or `Board`.
Profile prefixing, app scoping, guest write suppression and legacy migrations
are owned by the HAL. If you change persisted formats, update the schema
migration and test profile deletion and slot shifting.

## Pull Requests

A useful PR includes:

- What changed.
- Why it changed.
- How it was tested, including exact `pio` size figures when firmware changed.
- Screenshots or regenerated mockups for UI changes.
- Any hardware validation that was done or still remains.

Do not merge your own firmware branch into `main` until the device build has
been tested on hardware.
