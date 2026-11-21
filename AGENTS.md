# Agent instructions

This project keeps its rules in **`CLAUDE.md`** files. They apply to everyone
who changes this repository -- every agent, whichever tool you are, and every
human contributor. There is one rulebook, not an agent track and a people
track: [CONTRIBUTING.md](CONTRIBUTING.md) covers how work reaches the protected
`main` and `dev` branches, and these files cover what the change has to
honour. Read them as your instructions.

This file is the door for tools that read `AGENTS.md` rather than `CLAUDE.md`.
It states the rules in brief and says where each is written in full; where the
two ever differ, `CLAUDE.md` is right and this file is the defect.
`tools/check_docs.py` fails if this file stops linking a rulebook.

## The rulebooks

- [`CLAUDE.md`](CLAUDE.md) -- start here: the product's rules, which hold on
  every platform; build, architecture, invariants; working alongside other
  agents; and where each platform's rules live
- [`src/engine/CLAUDE.md`](src/engine/CLAUDE.md) -- screen lifecycle, the app
  registry, courses, progress
- [`src/games/CLAUDE.md`](src/games/CLAUDE.md) -- how to add or change a game
  or a system screen
- [`src/hal/CLAUDE.md`](src/hal/CLAUDE.md) -- hardware, persistence, profiles,
  sound, battery, watchdog; the **ESP32 boards' hardware notes** and the
  **Attaky Core's standing rules**
- [`src/ui/CLAUDE.md`](src/ui/CLAUDE.md) -- theming and drawing helpers
- [`host/CLAUDE.md`](host/CLAUDE.md) -- the **Linux platform**: the same `src/`
  as a PC program and as the Anbernic RG40XX V's whole-device image
- [`host/android/CLAUDE.md`](host/android/CLAUDE.md) -- the same host build
  as an Android APK that is the device's home screen (the Meta Portal+;
  experimental)
- [`docs/PORTING.md`](docs/PORTING.md) -- adding an ESP32 board, or a platform

## Three kinds of platform

| Platform | Rules |
|---|---|
| ESP32 boards (the published product) | `src/hal/CLAUDE.md`, `docs/PORTING.md`, `docs/boards/` |
| Attaky Core (ESP32-S3, the Cyber Deck Plus; a product) | `src/hal/CLAUDE.md`, `docs/boards/Attaky-Core.md` |
| Linux: PC build and RG40XX V image (and, experimentally, the same host build on Android: the Portal+) | `host/CLAUDE.md`, `host/android/CLAUDE.md` |

One `src/` builds for all three. **`host/` adapts to `src/`, never the
reverse**: no platform `#if` under `src/`; a difference is an interface with
one implementation per platform. Code only one board carries compiles only
into that board's image. `tools/check_platforms.py` checks both.

## The rules in brief

Each is written in full in the root `CLAUDE.md` under the heading given.

**A commit trailer names who is accountable; a branch name says what
changed.** ("Two standing rules") This project is built with AI coding tools
and says so openly -- in the README, in NOTICE.md and on the website. Nothing
in this rule hides that, and nothing in it may be read as asking you to. It is
about what a git trailer *means*. `Co-Authored-By:` is a claim of authorship
and, under the GPL, of copyright; a tool can hold neither, cannot accept the
licence and cannot answer for a defect. The author of record is the person who
directed the change, reviewed it, tested it and licenses it. So do not add
`Co-Authored-By:` naming a model, a "Generated with..." footer, or any
sign-off crediting a tool -- in commits, amends, squashes or PR bodies. If a
change was substantially drafted by a tool and that is worth recording, write
it as a plain sentence in the commit body ("drafted with an AI coding
assistant; reviewed and tested by hand"). Branches are `feat/<game-id>`,
`fix/<area>`, `docs/<topic>`, never named after a tool; rename one before
opening the PR (`git branch -m`). The `CLAUDE.md` filename is the one
exception.

**Docs are part of the change, not a follow-up.** ("Two standing rules")
README, the rulebooks, About, the mock-ups, the changelog -- in the same
commit as the change they describe. Run every `tools/check_*.py` before you
commit; "Which rules a machine checks" in `CLAUDE.md` says what each covers
and which rules only review can catch.

**Every file says who wrote it, and under what licence.** Run
`python tools/check_licenses.py --fix` on a new file; never hand-type a notice.
The holder is `iamankushpandit` and nothing else.

**No identifiers in this repository.** ("No identifiers in this repository")
Nothing that identifies one physical device, one network or one person: no
MAC, no SSID, no PIN, no player's name -- not in a tool file, a comment or a
fixture. Identify a board by `Board::deviceId()`; the boot banner prints
`device=`, never `mac=`. `check_identifiers.py` catches MACs and public IPs
and nothing else, so a clean run is not permission.

**No data collection.** ("No data collection") Exactly four things leave the
device: an NTP time query, one `ip-api.com` lookup to guess the timezone on
first connect, the opt-in, non-connectable BLE beacon, and the daily
update-availability check. **That list is closed**, on every platform. A fifth
outbound flow needs the maintainer's agreement in an issue *before* the code
exists. Anything that transmits, stores or shares data changes the About radio
page and the README privacy section in the same commit.

**Players.** The people holding the console are players, young players and the
administrator -- never a word for an age group. `check_docs.py` checks it.

**Know what a frame costs.** ("Responsiveness rule") A 20ms budget. No NVS
read in a hot path (RAM mirror in `Board`), no `delay()` on a render path, no
sampling a sensor from a getter, no rebuilding content that did not change.

**A full redraw is the exception.** ("Rendering model") Draw what appeared;
erase only what moved, with a clear rectangle derived from the geometry; route
header changes through `requestChromeRender()`. A screen transition is always a
full repaint.

**There is no garbage collector.** ("Memory rule") No `new`/`malloc`, no
`String` in anything that runs per frame, fixed buffers, give back what you
borrowed in `end()`.

**Refactor a large file first.** ("Modularity rule") A `.cpp` past ~600 lines,
or a function in `src/main.cpp` past ~400 lines of logic, is split before the
requested change, in a commit of its own that changes no behaviour.

**Lay out against the panel you are on.** System screens read
`tft.width()`/`tft.height()`, never `SCREEN_WIDTH`/`SCREEN_HEIGHT`. Portrait is
decided screen by screen (issue 22); the launcher, lock screen and saver
support it and must keep doing so.

**Adding a game or an app** has a whole checklist -- code, docs, screens,
verification -- under "Adding a game or an app" in `CLAUDE.md`. Games declare
their own `AppMetadata`; `APP_REGISTRY` binds it; nothing derived from the
registry (About, the launcher, Settings' games list, the site) is hand-edited.

**Adding a board** is [docs/PORTING.md](docs/PORTING.md): a profile header and
a `[board_*]` section, bring-up checked on the hardware, a page in
`docs/boards/`, and on the web installer -- a board people cannot flash from
the page is not supported.

## Working alongside other agents

("Working alongside other agents" in `CLAUDE.md` has the whole protocol.)

1. **Start in your own worktree, from `dev`**:
   `git worktree add ../GUme-<slug> -b feat/<slug> dev`. Never `git switch`
   inside the main checkout. `git fetch origin` before forming any opinion
   about a branch; if `dev` still looks like the wrong base, say what you
   measured and ask -- an explanation is not an approval.
2. **Read `git status` first.** Uncommitted work that is not yours is normal.
   Stage explicit paths, never `git add -A`. Don't merge or push unless asked.
   `main` and `dev` are protected: every merge is a pull request with `verify`
   green.
3. **After touching `AppRegistry.cpp` or a game's `AppMetadata`**, run
   `python tools/check_catalog.py` before anything else.
4. **One board, one serial port, shared by everyone.** Take the lock at
   `$(git rev-parse --git-common-dir)/gume-board.lock` before any build, flash
   or monitor; release it even on failure; a lock with no live process behind
   it is stale -- delete it and say so. Never flash or factory-reset without
   saying so first.
5. **A release is a `v*` tag on `main`.** `release.yml` does the rest and
   refuses a `-SNAPSHOT` or a tag that disagrees with `BRAINO_VERSION`.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
