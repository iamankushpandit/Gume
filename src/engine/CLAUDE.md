# src/engine

Framework shared by every screen. No hardware access lives here except through `Board`.

## Game.h

Defines `Game` (the screen base class), `AppContext` (the narrowed surface for ordinary launchable games), `AppGame` (a bridge from `Game` to `AppContext`) and `GameHost` (the privileged host for system screens: `board()`, capability checks, launcher entry access, `openSettings()`, `openWifi()`, `openProfiles()`).

All catalog games run through `AppGame` and only get `Ui::Renderer` drawing, content, scoped persistence, feedback and basic navigation. `Game.h` does not include `Board.h` or `TFT_eSPI.h`; `Board` and `ContentLoader` are forward-declared there so ordinary game headers do not inherit hardware dependencies. The only screens still on full `GameHost` are Launcher, Settings, Wi-Fi, Profiles, Scores, About and System Info, and system screens must call `requireCapability()` before protected device/profile/network/score actions.

Lifecycle is `begin()` -> `update()`/`render()` per frame -> `end()`. `end()` is called exactly once when the screen is replaced, before the next screen's `begin()`, and every transition in the `KidsPlatformApp` runtime funnels through `leaveActiveGame()` so no path can skip it.

`KidsPlatformApp` lives in `AppRuntime.cpp` plus `AppRuntimeLauncher.cpp` and `AppRuntimeScreenSaver.cpp`, rather than keeping launcher/runtime/saver logic embedded in `main.cpp`. `AppRuntimeLauncher.cpp` implements `LauncherGame`, so home screen touch/render goes through the same lifecycle as the app screens; only ScreenSaver and Asleep remain runtime views.

The default `end()` does nothing, which is right for the games: they hold only their own members and `begin()` resets those. Override it if a screen acquires anything that outlives a frame: a cached buffer, a sampling cadence, a radio or a file handle. Nothing runs off a task or timer today, so no screen keeps burning cycles once you leave it; the hook exists so that stays true as screens grow. `SystemInfoGame` uses it to clear its fixed-buffer row list.

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

Derived compatibility view over the playable slice of `AppRegistry`. Keep it that way: the authored source is each game's local `AppMetadata`.

Fields: `id`, `title`, `subtitle`, `label`, `blurb`. The wrapper exists for code that still wants a lightweight catalog-shaped record without carrying a second hand-maintained table.

The same constraints still apply to the underlying metadata: keep `id` stable, and `blurb` renders at font 1 across ~292 px so it must stay under ~46 chars.

## AppRegistry.{h,cpp}

Single source of truth for the launchable app list. Each playable game declares one local `AppMetadata` (and optional `AppScoreInfo`) in its own `.cpp`; that metadata owns id, title, screen title, subtitle, launcher label, blurb, score pointer, launcher icon, launcher index and default visibility. `AppRegistry.cpp` binds that metadata to its concrete `GameInstances` member via `metadataCatalogApp(...)`. The launchable system screens live in the same table via `systemApp(...)` with explicit capability flags.

If you add a playable game, update `PLAYABLE_APP_COUNT`, put the launcher index/icon/default visibility in the game's own `AppMetadata`, and add the new `metadataCatalogApp(...AppMetadata(), instance)` line at the matching registry position. `tools/check_catalog.py` checks that indices stay contiguous, icon pairing still matches the app id, playable apps subclass `AppGame`, and system apps declare capabilities.

## Progress.{h,cpp}

Per-item spaced repetition, one signed byte per item (-6..+6) in a single NVS blob. Correct `+1`, miss `-2`. Selection weights: heavily missed 8x, unseen 3x, mastered 1x. `pickWeighted()` takes a filter callback; `masteryPercent()` feeds auto-difficulty. Used by the flag/state games, which track recognition and capitals separately.

## ScoreCatalog.{h,cpp}

Derived compatibility view over the playable apps that actually expose score metadata. `ScoresGame` reads scores directly from `AppRegistry`; keep this wrapper derived for any helper code that still wants a score-catalog-shaped accessor.

## RecentQuestions.h

Header-only ring buffer of the last 10 question tokens. Rejects recent repeats while keeping selection genuinely random: deliberately not a shuffle bag. Procedural games hash their question into a token; table-driven games use `pickIndex()`.

## ContentLoader.{h,cpp}

Optional SD-card JSON overrides (Memory grid size/symbols, Counting range) parsed with ArduinoJson. Purely additive: every game ships compiled-in defaults and must work with no card present. Format is documented in `docs/SD_CONTENT_SPEC.md`.
