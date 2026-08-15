# src/engine

Framework shared by every screen. No hardware access lives here except through `Board`.

## Game.h

Defines `Game` (the screen base class), `AppContext` (the narrowed surface for ordinary launchable games), `AppGame` (a bridge from `Game` to `AppContext`) and `GameHost` (the privileged host for system screens: `board()`, `openSettings()`, `openWifi()`, `openProfiles()`).

All catalog games now run through `AppGame` and only get drawing, content, scoped persistence, feedback and basic navigation. The only screens still on full `GameHost` are Settings, Wi-Fi, Profiles, Scores, About and System Info.

Lifecycle is `begin()` -> `update()`/`render()` per frame -> `end()`. `end()` is called exactly once when the screen is replaced, before the next screen's `begin()`, and every transition in the `KidsPlatformApp` runtime funnels through `leaveActiveGame()` so no path can skip it.

`KidsPlatformApp` now lives in `AppRuntime.cpp` plus `AppRuntimeLauncher.cpp` and `AppRuntimeScreenSaver.cpp`, rather than keeping launcher/runtime/saver logic embedded in `main.cpp`.

The default `end()` does nothing, which is right for the games: they hold only their own members and `begin()` resets those. Override it if a screen acquires anything that outlives a frame: a cached buffer, a sampling cadence, a radio or a file handle. Nothing runs off a task or timer today, so no screen keeps burning cycles once you leave it; the hook exists so that stays true as screens grow. `SystemInfoGame` uses it to release its row list, which holds up to 48 pairs of Arduino `String`s.

Invalidation is two-level because a full 320x240 wipe is ~150 KB over SPI / ~30 ms of blanking:

| Member | Access | Use |
|---|---|---|
| `markDirty()` | protected | content changed -> repaint moving parts |
| `markFullDirty()` | protected | layout changed -> repaint chrome too |
| `needsFullRender()` | protected | guard static chrome inside `render()` |
| `needsRender()` / `clearDirty()` | public | driven by the main loop |
| `requestRender()` | public | force a full repaint when returning to a screen |

Both flags start true, so the first paint is always full.

## GameCatalog.{h,cpp}

Single source of truth for the 28 playable games. `AppRegistry`, `SettingsGame` and `AboutGame` all read from it, which is what stops the three lists drifting (About had silently fallen six games behind before this existed).

Fields: `id`, `title`, `subtitle`, `label`, `blurb`. Array order defines both launcher order and Settings order.

Two constraints: `id` is a persisted NVS visibility key and must never be renamed, and `blurb` renders at font 1 across ~292 px so it must stay under ~46 chars.

System screens are not in this catalog. `AppRegistry.cpp` appends them after the catalog entries and keeps them always visible.

## AppRegistry.{h,cpp}

Single source of truth for the launchable app list. Each playable game binds one `GAME_CATALOG` slot to its concrete `GameInstances` member and launcher icon via `catalogApp(index, LauncherIcon::..., ...)`; the system screens live in the same table via `systemApp(...)`.

If you add a playable game, the registry line must use the same catalog index as the entry you appended in `GameCatalog.cpp`. `tools/check_catalog.py` checks that the two still line up and that `ScoreCatalog.cpp` only names real catalog ids.

## Progress.{h,cpp}

Per-item spaced repetition, one signed byte per item (-6..+6) in a single NVS blob. Correct `+1`, miss `-2`. Selection weights: heavily missed 8x, unseen 3x, mastered 1x. `pickWeighted()` takes a filter callback; `masteryPercent()` feeds auto-difficulty. Used by the flag/state games, which track recognition and capitals separately.

## ScoreCatalog.{h,cpp}

Maps a game to its NVS score key, unit label, and sort direction (`lowerIsBetter`). Lets `ScoresGame` render best/worst without knowing anything about game internals. Add an entry here whenever a new game records a score.

## RecentQuestions.h

Header-only ring buffer of the last 10 question tokens. Rejects recent repeats while keeping selection genuinely random: deliberately not a shuffle bag. Procedural games hash their question into a token; table-driven games use `pickIndex()`.

## ContentLoader.{h,cpp}

Optional SD-card JSON overrides (Memory grid size/symbols, Counting range) parsed with ArduinoJson. Purely additive: every game ships compiled-in defaults and must work with no card present. Format is documented in `docs/SD_CONTENT_SPEC.md`.
