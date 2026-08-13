# src/games

One `.h`/`.cpp` pair per screen. Every screen — including Settings, Wi-Fi, Profiles, Scores, System Info and About — is a `Game` subclass (`../engine/Game.h`).

## Lifecycle

```cpp
const char* title() const;                              // display name
void begin(GameHost& host);                             // (re)initialise on entry
void update(GameHost& host, const TouchPoint& touch);   // input, once per frame
void render(GameHost& host);                            // draw current state
void end(GameHost& host);                               // optional; on leaving
```

`main.cpp` calls `begin()` → `render()` → `clearDirty()` on launch, then each frame calls `update()` and re-renders only when `needsRender()` is true. On leaving, `end()` is called once via `leaveActiveGame()`, before the next screen's `begin()`.

The default `end()` does nothing, which is right for a game holding only its own members. Override it if your screen acquires anything that outlives a frame. Every transition also compares free heap against the value captured before `begin()` and logs `[heap] '<screen>' left N bytes short` if a screen does not hand it back — watch the serial log after adding one.

Inside `render()`, guard static chrome behind `if (needsFullRender())` and draw dynamic parts unconditionally. Call `markDirty()` for content changes and `markFullDirty()` for layout changes. A full-screen repaint costs ~30 ms of visible blanking, so rapidly-updating games should do partial redraws — `SimonGame` is the reference implementation.

## House rules

- Reach hardware only through `host.board()`. Never include board internals or talk to TFT_eSPI outside `Ui::` helpers and direct primitives on `board.display()`.
- Draw with `Ui::` functions so the active Dark/Light theme applies.
- Persist with `board.getScore()` / `setScore()` / `saveBestScore()` / `loadBlob()` / `saveBlob()` using a plain key — profile prefixing happens inside `Board`, and Guest writes are dropped automatically. Do not build profile-prefixed keys yourself.
- Hit test with `Rect{...}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)`.
- Feedback via `board.beepOk()` / `board.beepError()` (pulses the RGB LED; there is no audio).
- Assume a fixed 320×240 landscape canvas — games do not run in portrait.
- **System/UI apps (Settings, Wi-Fi, SystemInfo, Profiles, Scores, About, and any future app-style screens) MUST support both landscape and portrait orientations.** Read `tft.width()` / `tft.height()` at render time rather than the compile-time constants `SCREEN_WIDTH` / `SCREEN_HEIGHT`. Use `Ui::drawTab()` + `Ui::drawTabBaseline()` for multi-section content; the tab strip adapts naturally when you divide `tft.width()` at render time.
- **Never read a setting from `board` more than once per screen change in a hot path.** `Preferences` is flash-backed, so every getter is an NVS lookup. The frequently-read settings have write-through RAM mirrors in `Board`; add to those rather than reaching for a fresh getter each frame. Never call anything containing a `delay()` from `render()` or `update()`.
- **Don't rebuild content that didn't change.** Scrolling changes an offset, not content. Gate expensive rebuilds behind a stale flag.
- Never block `update()` for more than a second or two. The loop is watchdogged (`Watchdog::TIMEOUT_SECONDS = 12`) and a long busy-wait reboots the device. Games do not feed or touch the watchdog themselves; if you genuinely must block, ask `Board` to do it behind a `Watchdog::Pause`.

## Adding a game

Beyond the new file pair, four edits elsewhere, all required:

1. `../engine/GameCatalog.cpp` — append an entry. `id` becomes a permanent NVS key; blurb must stay under ~46 chars.
2. `../main.cpp` — new `EntryKind` enumerator.
3. `../main.cpp` — new slot in `CATALOG_KINDS[]`, **at the same index as the catalog entry**. Misalignment is not caught at compile time and launches the wrong game.
4. `../main.cpp` — `case` in `launchKind()` and in `drawLauncherIcon()`.

Scoring games also need an entry in `../engine/ScoreCatalog.cpp`.

### If other agents are working in parallel

Do this on its own branch in its own worktree (`git worktree add ../GUme-<game-id> -b feat/<game-id>`) rather than in the shared checkout — see the root `CLAUDE.md`.

Steps 2–4 all land in shared files, so two agents adding games at once will collide there. The dangerous case is not the visible conflict — it's that `GAME_CATALOG[]` and `CATALOG_KINDS[]` live in different files and are coupled only by position, so a merge that appends both entries in a different order **compiles cleanly and launches the wrong game**. Re-check the two arrays entry-for-entry after any merge.

Pick an `id` that nobody else is likely to be using concurrently, and re-read the catalog right before appending — it may have grown since you last looked.

## Shared data

- `CountryData.{h,cpp}` — 195 countries: ISO2, capital, continent, difficulty tier.
- `CountryDataTable.cpp` — **generated**; edit `tools/gen_country_facts.py` and regenerate rather than hand-editing.
- `StateData.{h,cpp}` — 50 US states: code, name, capital, tier.

Flag and outline artwork comes from the `map-n-flag` library (local `lib_deps` path) and is blitted via `Ui::drawCountryImage*()`, which streams 4-bit indexed rows straight out of flash.
