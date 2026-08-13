# src/engine

Framework shared by every screen. No hardware access lives here except through `Board`.

## Game.h

Defines `Game` (the screen base class) and `GameHost` (the only system handle a game gets: `board()`, `content()`, `goHome()`, `relaunchActiveGame()`, `openSettings()`, `openWifi()`, `openProfiles()`).

Invalidation is two-level because a full 320×240 wipe is ~150 KB over SPI / ~30 ms of blanking:

| Member | Access | Use |
|---|---|---|
| `markDirty()` | protected | content changed — repaint moving parts |
| `markFullDirty()` | protected | layout changed — repaint chrome too |
| `needsFullRender()` | protected | guard static chrome inside `render()` |
| `needsRender()` / `clearDirty()` | public | driven by the main loop |
| `requestRender()` | public | force a full repaint when returning to a screen |

Both flags start true, so the first paint is always full.

## GameCatalog.{h,cpp}

Single source of truth for the **26 playable games** — `main.cpp`, `SettingsGame` and `AboutGame` all read from it, which is what stops the three lists drifting (About had silently fallen six games behind before this existed).

Fields: `id`, `title`, `subtitle`, `label`, `blurb`. Array order defines both launcher order and Settings order.

Two constraints: `id` is a persisted NVS visibility key and must never be renamed, and `blurb` renders at font 1 across ~292 px so it must stay under ~46 chars.

System screens (Scores, Settings, Wi-Fi, Profiles, About) are **not** in this catalog — `main.cpp` appends them after the catalog entries and they are always visible.

## Progress.{h,cpp}

Per-item spaced repetition, one signed byte per item (−6…+6) in a single NVS blob. Correct `+1`, miss `−2`. Selection weights: heavily missed 8×, unseen 3×, mastered 1×. `pickWeighted()` takes a filter callback; `masteryPercent()` feeds auto-difficulty. Used by the flag/state games, which track recognition and capitals separately.

## ScoreCatalog.{h,cpp}

Maps a game to its NVS score key, unit label, and sort direction (`lowerIsBetter`). Lets `ScoresGame` render best/worst without knowing anything about game internals. Add an entry here whenever a new game records a score.

## RecentQuestions.h

Header-only ring buffer of the last 10 question tokens. Rejects recent repeats while keeping selection genuinely random — deliberately not a shuffle bag. Procedural games hash their question into a token; table-driven games use `pickIndex()`.

## ContentLoader.{h,cpp}

Optional SD-card JSON overrides (Memory grid size/symbols, Counting range) parsed with ArduinoJson. Purely additive — every game ships compiled-in defaults and must work with no card present. Format is documented in `docs/SD_CONTENT_SPEC.md`.
