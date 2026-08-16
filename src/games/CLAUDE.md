# src/games

One `.h`/`.cpp` pair per game or system screen. Settings, Wi-Fi, Profiles, Scores, System Info and About are `Game` subclasses (`../engine/Game.h`); the launcher is also a `Game`, but it lives in `src/engine` because it is part of the runtime shell.

## Lifecycle

```cpp
const char* title() const;                                // display name
void begin(AppContext& host);                             // ordinary game
void update(AppContext& host, const TouchPoint& touch);
void render(AppContext& host);
void end(AppContext& host);                               // optional
```

System/UI screens that genuinely need privileged access still subclass `Game` directly and keep the `GameHost&` signatures. Right now that list is exactly: Launcher, Settings, Wi-Fi, Profiles, Scores, About and System Info. Guard protected actions with `requireCapability()`.

`main.cpp` calls `begin()` -> `render()` -> `clearDirty()` on launch, then each frame calls `update()` and re-renders only when `needsRender()` is true. On leaving, `end()` is called once via `leaveActiveGame()`, before the next screen's `begin()`.

The default `end()` does nothing, which is right for a game holding only its own members. Override it if your screen acquires anything that outlives a frame. Every transition also compares free heap against the value captured before `begin()` and logs `[heap] '<screen>' left N bytes short` if a screen does not hand it back: watch the serial log after adding one.

Inside `render()`, guard static chrome behind `if (needsFullRender())` and draw dynamic parts unconditionally. Call `markDirty()` for content changes and `markFullDirty()` for layout changes. A full-screen repaint costs ~30 ms of visible blanking, so rapidly-updating games should do partial redraws: `CinnamonGame` is the reference implementation.

## House rules

- Prefer `AppGame` for ordinary games. Reach the system through `AppContext`: `display()` returns `Ui::Renderer&`, plus `content()`, scoped score/blob helpers, feedback helpers, `drawTopBar()`, `goHome()`, `relaunchActiveGame()`. Only system screens should still depend on `GameHost` / `Board`; `tools/check_catalog.py` fails if a playable app does.
- Draw with `Ui::` functions so the active Dark/Light theme applies.
- Persist with `getScore()` / `setScore()` / `saveBestScore()` / `loadBlob()` / `saveBlob()` using a plain key: profile prefixing, app-scoped key translation and legacy-key migration all happen inside `Board`, and Guest writes are dropped automatically. Do not build storage keys yourself.
- Hit test with `Rect{...}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)`.
- Feedback via `beepOk()` / `beepError()` (pulses the RGB LED; there is no audio).
- Assume a fixed 320x240 landscape canvas: games do not run in portrait.
- System/UI apps (Settings, Wi-Fi, SystemInfo, Profiles, Scores, About, and any future app-style screens) must support both landscape and portrait orientations. Read `tft.width()` / `tft.height()` at render time rather than the compile-time constants `SCREEN_WIDTH` / `SCREEN_HEIGHT`. Use `Ui::drawTab()` + `Ui::drawTabBaseline()` for multi-section content; the tab strip adapts naturally when you divide `tft.width()` at render time.
- Never read a setting from `board` more than once per screen change in a hot path. `Preferences` is flash-backed, so every getter is an NVS lookup. The frequently-read settings have write-through RAM mirrors in `Board`; add to those rather than reaching for a fresh getter each frame. Never call anything containing a `delay()` from `render()` or `update()`.
- Don't rebuild content that didn't change. Scrolling changes an offset, not content. Gate expensive rebuilds behind a stale flag.
- `RowList` section headings are struck through by a rule that starts a fixed 54px in, so keep them to about six characters. `NearbyGame` puts the peer's tag in the heading and everything else about it in rows for exactly this reason.
- Never block `update()` for more than a second or two. The loop is watchdogged (`Watchdog::TIMEOUT_SECONDS = 12`) and a long busy-wait reboots the device. Games do not feed or touch the watchdog themselves; if you genuinely must block, ask `Board` to do it behind a `Watchdog::Pause`.

## Adding a game

The full checklist lives in the root `CLAUDE.md` under "Adding a game or an app": work through that, not this summary. It covers the docs, the screenshots and the verification steps, which is where things have actually been shipped broken. The code edits alone are:

1. In the game's own `.cpp`, add one `AppMetadata` declaration and a `...AppMetadata()` accessor. Keep `id` stable; blurb must stay under ~46 chars. The metadata block also owns launcher icon, launcher index and default visibility.
2. If the game records a score, declare one `AppScoreInfo` there too and point the metadata at it.
3. `../engine/AppRegistry.cpp`: add a `metadataCatalogApp(...AppMetadata(), instance)` line at the same playable position as the metadata launcher index. `tools/check_catalog.py` verifies the index, icon binding and `AppGame` subclass.
4. `../engine/AppRegistry.h`: update `PLAYABLE_APP_COUNT`.

Then the part that gets forgotten: a README table row and gallery image, a `tools/gen_screens.py` render function, regenerated screens, a changelog entry, and `python tools/check_docs.py` clean. A game that launches correctly and is invisible in every document describing the product is not finished.

### If other agents are working in parallel

Do this on its own branch in its own worktree (`git worktree add ../GUme-<game-id> -b feat/<game-id>`) rather than in the shared checkout: see the root `CLAUDE.md`.

The registry line still lands in a shared file, so two agents adding games at once will still collide there. The dangerous case is not the visible conflict: it's that the local `AppMetadata::launcherIndex` is launcher-order data, so a merge that duplicates or skips an index compiles cleanly and launches the wrong game. Re-run `python tools/check_catalog.py` after any merge touching the registry or a game's metadata.

Pick an `id` that nobody else is likely to be using concurrently, and re-read the catalog right before appending: it may have grown since you last looked.

## Shared data

- `CountryData.{h,cpp}`: 195 countries: ISO2, capital, continent, difficulty tier.
- `CountryDataTable.cpp`: generated; edit `tools/gen_country_facts.py` and regenerate rather than hand-editing.
- `MazeData.{h,cpp}`: static maze layouts used by Maze; keep path data out of the redraw logic.
- `StateData.{h,cpp}`: 50 US states: code, name, capital, tier.
- `TraceGlyphData.{h,cpp}`: static trace stroke geometry used by Trace; keep glyph data out of the touch/render logic.

Flag and outline artwork comes from the `map-n-flag` library and is blitted via `Ui::drawCountryImage*()`, which streams 4-bit indexed rows straight out of flash.
