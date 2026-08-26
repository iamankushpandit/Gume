# Contributing

GUme is the repository behind **Braino!**, a small firmware project with a
large code surface: 31 built-in games, 7
system apps, hardware drivers, generated screenshots, a web installer and CI.
The repo includes explicit instructions and machine checks to keep that
surface consistent. Read those instructions first; they are part of the
engineering process, not side notes.

Start here:

- `AGENTS.md`: repo-level rules that apply to every contributor.
- `CLAUDE.md`: architecture, build commands, invariants and workflow.
- `src/engine/CLAUDE.md`: screen lifecycle, app registry, progress tracking.
- `src/games/CLAUDE.md`: how games and system screens are structured.
- `src/hal/CLAUDE.md`: board, storage, profiles, watchdog and hardware rules.
- `src/ui/CLAUDE.md`: renderer, theme, widgets and drawing helpers.

These files exist because multiple contributors can work on this repo at the
same time. They are the rules for every contributor, and a pull request is
reviewed against them. They explain how to keep changes small, avoid stepping
on another contributor's work and preserve the firmware's performance and
memory constraints.

## No Data Collection

Braino collects nothing about the player using it, and no contribution may
change that. It is not a default, a setting or an opt-in that happens to be
switched off — it is what the product is. This is the one rule here that a
maintainer cannot wave through in review.

Three things leave the device over the air. This is the complete list:

| Flow | What goes out | Controlled by |
|---|---|---|
| NTP | A time query to `pool.ntp.org`, or whichever server is configured | Only once Wi-Fi is set up, and Wi-Fi can be skipped entirely |
| Timezone guess | One request to `ip-api.com` on first connect, which necessarily shows that host the device's public IP | The same Wi-Fi switch; overridden by picking a zone by hand |
| BLE beacon | A device name and two bytes of the factory Bluetooth MAC, non-connectable. With Nearby play on, also a game index and a best score | Off by default, opt-in from *Settings → Beacon* |

Adding a fourth changes what the product promises its owners. Raise it in an
issue and get agreement before writing the code, because a pull request that
adds one gets closed on principle rather than on quality.

Concretely, do not add:

- analytics, usage counters, or crash and error reporting to any server;
- any HTTP, UDP or DNS request beyond the table above;
- a player's name, profile name, score, progress or usage history inside
  anything that is transmitted;
- an identifier derived from something a player typed;
- a dependency that phones home, checks for updates, or fetches remote content
  at runtime;
- a field in the BLE advertisement that identifies a person rather than a
  device — and there is no room for one regardless, the payload with Nearby
  play on is already exactly 31 bytes.

Storing is not collecting. Scores, mastery data and profiles live in the
ESP32's own NVS and never leave it, which is the design rather than a
compromise. Nothing needs an account, and there is nothing to sign in to.

If a change to what is transmitted or stored is agreed, four documents are part
of that same commit: the About app's radio page — which must *read* the state
from the firmware, not restate it — the README Privacy section, the privacy
section of `site/index.template.html`, and this file. **A privacy claim that
has drifted from the hardware is worse than no claim at all, because it is
believed.**

The project's own infrastructure holds the same line. No analytics on the
GitHub Pages site, and the web installer flashes over Web Serial from inside
the browser — no firmware, serial number or board detail is uploaded anywhere.
The one third-party request that page makes today is the `esp-web-tools` module
it loads from unpkg.com, which sees a visitor's IP the way any CDN does. Do not
add a second.

## What would help most

**Board ports.** Braino targets the E32R28T-1 properly, with a 4-inch ST7796
variant in progress. The CYD family has many variants whose differences fail
silently — backlight on GPIO21 versus GPIO27, GPIO34 as a battery sense here
but a light sensor on the ESP32-2432S028R. A board is described in two files
and nowhere else — a profile header in `include/boards/` and a `[board_*]`
section in `platformio.ini` — and no file under `src/` names a GPIO. The
mechanics are in [docs/PORTING.md](docs/PORTING.md); everything a port needs
around the code is in
[AGENTS.md → "Supporting a new board"](AGENTS.md#supporting-a-new-board--the-whole-checklist):
hardware reference first, then the profile and the PlatformIO env, then a
verification app, then games. The "known issues" section of the board doc is
not optional.

**Cases.** [`cases/`](cases/README.md) holds a printable enclosure per board,
in a folder named after the `BOARD_NAME` the firmware reports. A case is
optional — nothing about board support depends on one — but most people
building this have never designed an enclosure, and a port that ships a shell
is a complete package rather than a bare board. Print notes, fastener sizes and
"here is what I got wrong on the first print" are worth as much as the mesh
itself. `cases/README.md` has the layout to follow.

**Tell us what's wrong with the code.** Bug reports and code critique are
genuinely valuable — please
[open an issue](https://github.com/iamankushpandit/Gume/issues), and if you
can, say how you'd fix it. Hardware assumptions that were never measured are
the most valuable of all.

## Branches And Protection

`main` and `dev` are both protected on GitHub. Neither can be force-pushed or
deleted, and changes reach them through a pull request whose CI has passed.

| Branch | What it holds | How work lands |
|---|---|---|
| `main` | Released firmware — what the web installer flashes | Pull request from `dev`, after the build has been tested on hardware |
| `dev` | Integration branch, and the base for everything below | Pull request from a topic branch or a fork |
| `feat/*`, `fix/*`, `docs/*` | One concern each, short-lived | Rebased on `dev` and opened as a pull request |

In force on both branches: a pull request is required, the `verify` job in
`.github/workflows/ci.yml` must pass, the branch must be current with its base
before it can merge, review conversations must be resolved, and force-push and
deletion are blocked. `main` additionally requires linear history, so rebase or
squash rather than merging a stale branch into it. Repository admins can bypass
these rules — that exists for an emergency such as a broken release, not as a
normal route, and using it skips the CI that would have caught the problem.

`verify` also runs on pushes to `main` and `dev`. That is deliberate
belt-and-braces: the pull-request trigger alone left `main` unchecked, and it
stayed red for weeks without anyone seeing it. If an admin bypass ever does
land something on `main`, the push run is what says so. It is scoped to those
two branches rather than to every branch because a topic branch with a pull
request open matches both triggers, and ran the whole job twice for every
commit.

The job always runs on a pull request, whatever the change touches, because
`verify` is the required check: a required check that never runs does not read
as passed, it reads as still expected, and the pull request waits for a status
that will never arrive. So the skipping happens a level down. A pull request
touching only `docs/`, `cases/`, `site/`, `LICENSE` or Markdown skips the
firmware build and reports on the repository checks alone; anything else
builds all three environments as before. `paths-ignore:` on the trigger is the
obvious way to do this and is the trap -- it would make every
documentation-only pull request permanently unmergeable.

`git push origin main` will be rejected. That is the protection working; open a
pull request instead.

## Contributing From A Fork

Forking is the normal path, and the only one available if you are not a
collaborator on this repository. Base your work on `dev`, not `main`:

```bash
gh repo fork iamankushpandit/Gume --clone
cd Gume
git remote add upstream https://github.com/iamankushpandit/Gume.git
git fetch upstream
git switch -c fix/<slug> upstream/dev
```

Work, commit, then push to your fork and open the pull request against `dev`:

```bash
git push -u origin fix/<slug>
gh pr create --repo iamankushpandit/Gume --base dev
```

Keep the branch current — `git fetch upstream && git rebase upstream/dev` —
because the protection rules will not let a stale branch merge. Leave "allow
edits by maintainers" ticked so a maintainer can rebase or fix a check for you.

CI runs on pull requests from forks with a read-only token, so `verify` (the
repository checks, plus a build of every firmware environment when your change
touches code) will report on your PR without any secret being exposed to it. What CI cannot do is flash a board:
anything touching hardware, touch, storage or a screen still needs a human with
the device, so say in the PR what you tested and on which board.

Maintainers with push access can branch inside the repository instead of
forking — the worktree flow below — but the pull request and CI requirements
are identical either way. There is no path that merges to `main` or `dev`
without one.

## Working On A Change

1. One concern per branch. Branch from `dev`.
2. Prefer a separate worktree for a new requirement:
   `git worktree add ../GUme-<slug> -b fix/<slug> dev`.
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

## Cutting a release

A release is a tag, pushed on `main`. Everything else is automatic:

```bash
git tag -a v5.0.1 -m "Braino! 5.0.1" && git push origin v5.0.1
```

`.github/workflows/release.yml` builds every environment `platformio.ini`
declares, packs them with `tools/pack_release.py`, and publishes a GitHub
release carrying, per environment, all four flash parts and a single
`-merged.bin` to write at `0x0` -- plus `SHA256SUMS.txt` and `FLASHING.txt`.
Release notes are lifted from the `CHANGELOG.md` section for that version, so
there is no second set to keep in step.

Check three things before tagging:

1. `include/AppVersion.h` carries the real version, not a `-SNAPSHOT`. The
   workflow refuses to publish a snapshot: `dev` carries one between releases
   by design, and the first mistaken tag would otherwise publish a "release"
   that the firmware itself calls unreleased.
2. `CHANGELOG.md` has a `## <version>` section, dated.
3. The build figures in `README.md` and `CLAUDE.md` came from a build of the
   branch being released. `tools/build_stamp.py` compiles the branch name into
   the image, so a figure measured on a topic branch is a few bytes out on
   `main` -- measure with `GITHUB_REF_NAME=main pio run -e app`.

The tag and `BRAINO_VERSION` must agree, and the workflow fails if they do
not. A published release cannot be quietly corrected, because people have
already downloaded it.

To rehearse the packing without tagging anything, run it against a local
build:

```bash
pio run -e app -e bringup -e wifidiag -e batdiag
python tools/pack_release.py --out dist --strict
```

Afterwards, open the next version on `dev` as a `-SNAPSHOT`, so a board
flashed from `dev` cannot be mistaken for the release it is ahead of.

## Pull Requests

Open it against `dev` unless you are a maintainer cutting a release, which is
the one case that targets `main`.

A useful PR includes:

- What changed.
- Why it changed.
- How it was tested, including exact `pio` size figures when firmware changed.
- Screenshots or regenerated mockups for UI changes.
- Any hardware validation that was done or still remains.

Before asking for a merge: `verify` green, the branch rebased on its base, and
every review conversation resolved. The protection rules enforce all three, so
a PR that is not ready simply will not offer the merge button.

Firmware that has only been built, never run, does not go to `main`. CI proves
it links and fits in flash; it cannot prove a screen draws, a touch target is
reachable or a battery reading is sane. `dev` is where a change waits for
somebody to hold the board.

## Licensing

This project is licensed **GPL-3.0-or-later** — see [LICENSE](LICENSE).

By opening a pull request you agree that your contribution is licensed under
those same terms. There is no separate CLA. If you port Braino to another board
and distribute it, or ship a device running a modified build, GPL-3.0 requires
you to make the corresponding source available under the same licence — which
is the point: ports should stay available to the people holding the hardware.

The bundled libraries and artwork keep their own permissive licences
(`TFT_eSPI` FreeBSD, `ArduinoJson` MIT, `NimBLE-Arduino` Apache-2.0,
`map-n-flag` MIT), all GPL-3.0-compatible. Do not add a dependency under a
licence that is not — anything GPL-incompatible, or "non-commercial" and
similar source-available terms, cannot ship in this firmware. If you are
unsure, raise it in the issue before writing the code.
