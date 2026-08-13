# Agent instructions

This project keeps its guidance in **`CLAUDE.md`** files, which apply to every agent working here regardless of which tool you are — read them as your instructions.

- [`CLAUDE.md`](CLAUDE.md) — start here: build commands, architecture, invariants, and the protocol for working alongside other agents
- [`src/engine/CLAUDE.md`](src/engine/CLAUDE.md) — screen lifecycle, catalogs, progress tracking
- [`src/games/CLAUDE.md`](src/games/CLAUDE.md) — how to add or change a game
- [`src/hal/CLAUDE.md`](src/hal/CLAUDE.md) — hardware, persistence, profiles, watchdog
- [`src/ui/CLAUDE.md`](src/ui/CLAUDE.md) — theming and drawing helpers

Four things that cause real damage here if you skip them:

1. **Branch into your own worktree before you start.** New requirement → `git worktree add ../GUme-<slug> -b feat/<slug>`, then work there. Do **not** `git switch` inside the main checkout: it holds other agents' uncommitted work and switching under them strands it. A worktree also gives you a private `.pio/` build dir.
2. **Multiple agents share this repo.** Read `git status` first; uncommitted work that isn't yours is normal. Stage explicit paths, never `git add -A`. Don't merge to `main` or push unless asked.
3. **`GAME_CATALOG[]` and `CATALOG_KINDS[]` are index-coupled across two files** and a bad merge misaligns them silently — it still compiles and links, and the only symptom is a tile launching the wrong game. Re-verify entry-for-entry after every merge.
4. **One physical board, one serial port** — shared across all branches and worktrees. Don't flash or factory-reset without saying so; it wipes calibration, profiles and scores another agent may be testing against.
5. **Take the board lock before building or flashing.** It lives at `(git rev-parse --git-common-dir)/gume-board.lock` so it's visible from every worktree. If it's held by a live process, wait and poll. If no build or flash process is actually running, the lock is stale — delete it and carry on, and say that you did. Release your own lock even when the build fails.
