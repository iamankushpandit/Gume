# src/engine

Framework shared by every screen. No hardware access lives here except through `Board`.

## Game.h

Defines `Game` (the screen base class), `AppContext` (the narrowed surface for ordinary launchable games), `AppGame` (a bridge from `Game` to `AppContext`) and `GameHost` (the privileged host for system screens: `board()`, capability checks, launcher entry access, `openSettings()`, `openWifi()`, `openProfiles()`).

All catalog games run through `AppGame` and only get `Ui::Renderer` drawing, content, scoped persistence, feedback and basic navigation. `Game.h` does not include `Board.h` or `TFT_eSPI.h`; `Board` and `ContentLoader` are forward-declared there so ordinary game headers do not inherit hardware dependencies. The only screens still on full `GameHost` are Launcher, Settings, Wi-Fi, Profiles, Scores, About and System Info, and system screens must call `requireCapability()` before protected device/profile/network/score actions.

Lifecycle is `begin()` -> `update()`/`render()` per frame -> `end()`. `end()` is called exactly once when the screen is replaced, before the next screen's `begin()`, and every transition in the `BrainoApp` runtime funnels through `leaveActiveGame()` so no path can skip it.

`BrainoApp` lives in `AppRuntime.cpp` plus `AppRuntimeLauncher.cpp`, `AppRuntimeScreenSaver.cpp`, `AppRuntimeLock.cpp` and `AppRuntimeIdentity.cpp` (the boot banner, and the `identify?` reply -- the same facts, so a board can be identified without a reset, never the MAC), `AppRuntimeConsole.cpp` + `AppRuntimeConsoleSettings.cpp` + `AppRuntimeConsoleProfiles.cpp` (the serial console: one line reader, one command table, one reply grammar `ok key="v"` / `err <code> <message>` with word-character keys; `get`/`set` over one settings table mirroring the Settings and Wi-Fi screens, CRUD for players, per-player game visibility; device reads open, changes and player-name reads behind `unlock <admin PIN>` keyed on each row's `AppCapability`; same `Board` setters and refusals as the screens, not counted as activity -- add a command or a setting as a row, never a string match; parsing in `ConsoleText.h`), rather than keeping launcher/runtime/saver logic embedded in `main.cpp`. `AppRuntimeLauncher.cpp` implements `LauncherApp`, so home screen touch/render goes through the same lifecycle as the app screens; only ScreenSaver, Asleep and Locked remain runtime views. `AppRuntimeLock.cpp` owns the hold-to-unlock guard and `lockAndSleepNow()`, the Lock button's entry point -- the deliberate way into the same guard, which sleeps through the ordinary `enterSleep()` and leaves `activeGame_` alone so a completed hold resumes it. The guard itself: it lays itself out against the live `tft.width()`/`height()` and applies no rotation of its own, because `resumeUnderlyingScreen()` applies the returning screen's rotation and rotating twice for a screen that is up for a second costs two full repaints.

The default `end()` does nothing, which is right for the games: they hold only their own members and `begin()` resets those. Override it if a screen acquires anything that outlives a frame: a cached buffer, a sampling cadence, a radio or a file handle. Nothing runs off a task or timer today, so no screen keeps burning cycles once you leave it; the hook exists so that stays true as screens grow. `SystemInfoApp` uses it to clear its fixed-buffer row list.

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

## NearbyPlay.{h,cpp} + NearbySession.cpp + NearbyPlayState.h

`NearbyPlay.cpp` is the score exchange and the notifications; `NearbySession.cpp`
is the two-player session half -- seats, invitations, turns and the reserved
ending. Both read one peer table, which lives in `NearbyPlayState.h` in
`NearbyPlay::detail` rather than in an anonymous namespace; nothing outside
those three files may include that header. The app-facing forwarders
(`BrainoApp::nearby*`) are in `AppRuntimeNearby.cpp`.

Two calls exist for games with more than two seats, and neither transmits
anything: `NearbySeat::forThisGame` compares the game an invitation names
(the peer's advertised game index, which `invitePeer()` sets) with the app open
here, and `selfId()` returns this console's own advertised tag so every console
at a table can order it alike.

Policy half of Nearby play: resolves a peer's game index against `AppRegistry`, compares its score against this profile's record, and raises the header notifications. `hal/BleScanner` listens and `hal/BleBeacon` transmits; neither knows what a game is.

- **Off by default, and gated on the beacon.** `tick()` re-derives `board.nearbyEnabled() && BleBeacon::enabled()` every frame rather than relying on an ordering contract with Settings, so the feature follows the radio in both directions. Both reads are from RAM.
- **NVS reads are change-driven.** A peer's record is looked up only when its game or score actually changed. A beacon repeats every second; looking a record up at that rate would put a flash lookup inside the frame budget for no new information.
- **One notification per thing that changed.** `Known[]` remembers what was already announced, so a repeating beacon does not repeat its banner.
- **Nothing identifying.** Profile names are never read here. A peer is four hex digits of its own MAC; the only numbers that travel are a game index and a best score.

## Progress.{h,cpp}

Per-item spaced repetition, one signed byte per item (-6..+6) in a single NVS blob. Correct `+1`, miss `-2`. Selection weights: heavily missed 8x, unseen 3x, mastered 1x. `pickWeighted()` takes a filter callback; `masteryPercent()` feeds auto-difficulty. Used by the flag/state games, which track recognition and capitals separately.

## ScoreCatalog.{h,cpp}

Derived compatibility view over the playable apps that actually expose score metadata. `ScoresApp` reads scores directly from `AppRegistry`; keep this wrapper derived for any helper code that still wants a score-catalog-shaped accessor.

## RecentQuestions.h

Header-only ring buffer of the last 10 question tokens. Rejects recent repeats while keeping selection genuinely random: deliberately not a shuffle bag. Procedural games hash their question into a token; table-driven games use `pickIndex()`.

## ContentLoader.{h,cpp}

Optional SD-card JSON overrides (Memory grid size/symbols, Counting range) parsed with ArduinoJson. Purely additive: every game ships compiled-in defaults and must work with no card present. Format is documented in `docs/SD_CONTENT_SPEC.md`.
