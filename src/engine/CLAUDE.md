# src/engine

Framework shared by every screen. No hardware access lives here except through `Board`.

## Game.h

Defines `Game` (the screen base class), `AppContext` (the narrowed surface for ordinary launchable games), `AppGame` (a bridge from `Game` to `AppContext`) and `GameHost` (the privileged host for system screens: `board()`, capability checks, launcher entry access, `openSettings()`, `openWifi()`, `openProfiles()`, and `applyOrientation()`, which a screen calls after changing the layout or the screen flip so that the runtime turns what is up -- a screen never sets a rotation itself).

All catalog games run through `AppGame` and only get `Ui::Renderer` drawing, content, scoped persistence, feedback and basic navigation. `Game.h` does not include `Board.h` or `TFT_eSPI.h`; `Board` and `ContentLoader` are forward-declared there so ordinary game headers do not inherit hardware dependencies. The only screens still on full `GameHost` are Launcher, Settings, Wi-Fi, Profiles, Scores, About, System Info and Nearby, and system screens must call `requireCapability()` before protected device/profile/network/score actions.

Lifecycle is `begin()` -> `update()`/`render()` per frame -> `end()`. `end()` is called exactly once when the screen is replaced, before the next screen's `begin()`, and every transition in the `BrainoApp` runtime funnels through `leaveActiveGame()` so no path can skip it.

`BrainoApp` lives in `AppRuntime.cpp` plus `AppRuntimeContext.cpp` (the storage, sound, feedback and capability calls screens make, mostly hand-offs to `Board`), `AppRuntimeLauncher.cpp`, `AppRuntimeScreenSaver.cpp`, `AppRuntimeLock.cpp`, `AppRuntimeLights.cpp` (the console's `lights` test) and `AppRuntimeIdentity.cpp` (the boot banner, and the `identify?` reply -- the same facts, so a board can be identified without a reset, never the MAC), `AppRuntimeConsole.cpp` + `AppRuntimeConsoleSettings.cpp` + `AppRuntimeConsoleProfiles.cpp` (the serial console: one line reader, one command table, one reply grammar `ok key="v"` / `err <code> <message>` with word-character keys; `get`/`set` over one settings table mirroring the Settings and Wi-Fi screens, CRUD for players, per-player game visibility, and `level <id>[:<track>] [<level>]`, the active player's course level in a game by id, through the same `courseLevelOf()`/`storeCourseLevel()` the games' `courseLevel()`/`setCourseLevel()` call, never reaching into a game that is open; device reads open, changes and of player data (names, levels) reads behind `unlock <admin PIN>` keyed on each row's `AppCapability`; same `Board` setters and refusals as the screens, not counted as activity -- add a command or a setting as a row, never a string match; parsing in `ConsoleText.h`), rather than keeping launcher/runtime/saver logic embedded in `main.cpp`. `AppRuntimeSelector.cpp` is the d-pad's selector on a board with keys (`GUME_HAS_NAV_KEYS`; nothing elsewhere): it runs above the routing in `loop()`, presses by handing the routing a touch at the selected control, raises Home, Lock and Mute through the doors BOOT, the power key and the speaker already use, and draws its ring only where it can paint the colours back -- see the root CLAUDE.md. `AppRuntimeLauncher.cpp` implements `LauncherApp` (one tile's icon and labels are in `AppRuntimeLauncherTiles.cpp`), so home screen touch/render goes through the same lifecycle as the app screens; only ScreenSaver, Asleep and Locked remain runtime views. `AppRuntimeLock.cpp` owns the hold-to-unlock guard and `lockAndSleepNow()`, the Lock button's entry point -- the deliberate way into the same guard, which sleeps through the ordinary `enterSleep()` and leaves `activeGame_` alone so a completed hold resumes it. The guard itself: it lays itself out against the live `tft.width()`/`height()` and applies no rotation of its own, because `resumeUnderlyingScreen()` applies the returning screen's rotation and rotating twice for a screen that is up for a second costs two full repaints.

`AppRuntimeLock.cpp` also owns `toggleMute()` and `activeSpeakerRect()`, the
header's mute control. It is the same shape of thing as the Lock button -- a
device-wide state changed from a glyph in the chrome, with the tap consumed by
the runtime above the active screen so it cannot also press whatever sat under
it -- which is why the two live together rather than in `Ui`. **It is
deliberately not gated on the admin PIN**, unlike every other route to that
setting; the reasoning is written out at the definition and should be read
before anyone adds the gate.

The default `end()` does nothing, which is right for the games: they hold only their own members and `begin()` resets those. Override it if a screen acquires anything that outlives a frame: a cached buffer, a sampling cadence, a radio or a file handle. Nothing runs off a task or timer today, so no screen keeps burning cycles once you leave it; the hook exists so that stays true as screens grow. `SystemInfoApp` uses it to clear its fixed-buffer row list.

**A game that was left is kept (`AppRuntimeKeep.cpp`).** `leaveActiveGame()` calls `noteGameLeft()` before `end()`: for a playable app that has had a press since `begin()` (`pressedInGame_`, set where the loop hands a press to the screen) it records what `keepsGame()` answered, the active player, `Board::playersEpoch()` and the time, in `kept_[]`, one entry per playable app, RAM only. `launch()` reads it back through `keptHow()`, which forgets an entry made by another player or before a player was removed: **Ask** goes to `askCarryOn()`, **Quietly** to `resumeKept()`, **No** to `enterApp(app, false, 0)`, which is the old body of `launch()` and still the one way onto a screen. `enterApp(app, true, awayMs)` calls `resume()` where it would have called `begin()`.

`CarryOnScreen` is the question. It is a `Game` the runtime owns, like the launcher; it is made the active game with the app it asks about as the active app, so the top bar, the canvas, the rotation and the touch mapping are that game's, and it lays itself out against `display().width()`/`height()` so that it is right for a fixed-canvas game and for one that follows the owner's orientation. It decides nothing: `settleCarryOn()` runs after every `update()`, reads `take()`, leaves the question through `leaveActiveGame()` like any screen and enters the game. `noteGameLeft()` ignores the question itself, so leaving it by Home keeps the game waiting.

`resumeUnderlyingScreen()` -- the tail of the screen saver, sleep and the lock screen -- also calls `resume()`, with the time since `idleSinceMs_`. The screen stayed up there and nothing was lost, but nothing updated it either, and a game's deadlines are in `millis()`.

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

**A peer that stops talking is the service's business too.** `peerSilentMs()`
answers "how long since we heard this console", and `NearbySeat::silentMs`
carries the same number to a lobby. It is `Known::lastSeenMs`, copied off the
scanner's sighting in `recordSession()` -- nothing goes on the air for it. It
exists because the second way a two-player game ends never arrives as a
message: a flat battery or a young player walking off cannot send `nearbyEnd()`, and
before this every nearby game sat on "their turn" forever, saying nothing.
`PEER_QUIET_MS` (15s, many missed advertisements rather than a few) is the shared
threshold past which a game pauses and says so; `PEER_SILENT_UNKNOWN` is
returned once the scanner has dropped the peer at `SIGHTING_TTL_MS`, which is
where "a short pause" becomes "a choice". Read it every frame -- it is a walk
over at most eight entries. Never lower the threshold below a few scan
intervals: the scanner listens 260ms in 780, so silence of a second or two is
ordinary.

**And so is a peer that is heard but has left the game.** `peerAwayMs()`
answers "how long has this console, though heard, said no move in the session
our own turn is in" -- from `Known::lastTurnMs`, which the scanner stamps
whenever an advertisement carries a move, and from `sessionSinceMs_`, the
moment our own turn last changed session (the console that accepts an
invitation says its first turn before the other has said any, and must not be
called away for that). Past `PEER_AWAY_MS` (20s) the peer has lost the game,
started another or switched Nearby off; a console that only left the *screen*
keeps its move on the air and is not this. `NearbyWatch` turns it into the
same card as silence. 20s is longer than a poke takes a move's place for (6s)
plus the scan's misses.

**The air holds one turn, and the service knows whose it is.** A game left in
the middle keeps its last move on the air, so the turn can belong to an app
that is not the one on screen. `turnOwner_` is the app that last published;
`stopTurns()` takes the turn down only for its owner (or when it is nobody's),
so one game's lobby calling `nearbyStop()` on its way in or out cannot remove
another game's kept move. It always withdraws an invitation, which can only be
the caller's. Four more things follow from a turn outliving its screen:

- **`freshSession()` is where a session number comes from.** Six bits from
  the clock, stepped past every session this console still holds a turn for
  (its own on the air, every peer's last) and every number a peer is offering
  now. A peer's last turn is kept for as long as the peer is in the room, so
  without this an old game's last move, or its ending, is read as the new
  game's one time in 64. A game can fix the number's low bits (up to four) to
  state a setting both consoles must agree before the first move; Go puts its
  rules and board size there, and the console that accepts reads them back out
  of `NearbySeat::session`.
- **`stopTurnsLater()` bounds a finished game's last word.** A game that is
  over is left with its last move or its ending on the air, because the other
  console may not have heard it; it goes after `LAST_WORD_MS` (a minute), or
  the console would look busy and share no score until that game was opened
  again. Saying a turn again cancels it. `tickSessions()` runs it from
  `tick()`, in every view.
- **`dropTurnsForOff()` clears everything when Nearby or the beacon goes
  off.** A turn left in the payload kept its flag bit on the air with sharing
  off, and came back, stale, when Nearby was switched on again. A game still
  in play says its turn again once it may.
- **A score belongs to the game it was heard with.** A peer that changes app
  while a move has its score's place on the air has sent no number for the new
  app: `BleScan` forgets the old one, `evaluateScore()` ignores a sighting
  with no score, and `PeerView::haveTheirScore` lets the Nearby screen print a
  dash instead of a nought that "beats" every lower-is-better record.

**One toss per session.** `invite()` remembers the last coin with its game
and session, and asking again under the same session -- Ludo putting its
invitation back up for each console it seats -- reuses it: a second toss
could tell a console that had read the first that the other side moves first
now. The toss is spent once a turn goes up or the invitation is withdrawn,
because `freshSession()` can hand a finished game's number out again. It also
cancels a pending `stopTurnsLater()`, whose turn the invitation
has just replaced, and `stopTurnsLater()` withdraws an invitation exactly as
`stopTurns()` does. `inviteForUs()` returns an invitation for the app open here
before one for another app, since that is the one the screen can answer.

**What a lobby is told about a peer** is `fillSeat()`, the one place a
`Known` becomes a `NearbySeat`: `inviting` (aimed at this console only),
`forThisGame`, `asking` (inviting somebody else to the app open here),
`inGame` (a move heard from it within `PEER_IN_GAME_MS` that is not an
ending), `partner` (the other console saying moves in that session, or who it
asked) and `withUs`. All of it is read from what peers already broadcast;
nothing is transmitted to make a lobby.

Policy half of Nearby play: resolves a peer's game index against `AppRegistry`, compares its score against this profile's record, and raises the notices (one line along the bottom of the screen; `AppRuntimeNotify.cpp` decides which of battery, update and Nearby is up and keeps the strip to itself while it is). `hal/BleScanner` listens and `hal/BleBeacon` transmits; neither knows what a game is.

- **Off by default, and gated on the beacon.** `tick()` re-derives `board.nearbyEnabled() && BleBeacon::enabled()` every frame rather than relying on an ordering contract with Settings, so the feature follows the radio in both directions. Both reads are from RAM.
- **NVS reads are change-driven.** A peer's record is looked up only when its game or score actually changed. A beacon repeats every second; looking a record up at that rate would put a flash lookup inside the frame budget for no new information.
- **One notification per thing that changed.** `Known[]` remembers what was already announced, so a repeating beacon does not repeat its banner.
- **Nothing identifying.** Profile names are never read here. A peer is four hex digits of its own MAC; the only numbers that travel are a game index and a best score.

## Progress.{h,cpp}

Per-item spaced repetition, one signed byte per item (-6..+6) in a single NVS blob. Correct `+1`, miss `-2`. Selection weights: heavily missed 8x, unseen 3x, mastered 1x. `pickWeighted()` takes a filter callback; `masteryPercent()` feeds auto-difficulty. Used by the flag/state games, which track recognition and capitals separately.

## Course.h

What turns a quiz into a course: a level that is kept, and a way of dealing that covers the whole of a level. Header-only, pure C++ with no Arduino; randomness is handed in as a number, so `test/host/course_test.cpp` compiles it as it is. Two pieces:

- **`Course::Ladder`** -- the player's level in one game. A meter, `+1` for a right answer and `-2` for a miss (the weighting `Progress` uses for one item), a step up at `UP_AT` (+6) and down at `DOWN_AT` (-6). `start(levelCount, stored)` takes what `AppContext::courseLevel()` gave, 0 meaning never set, and brings a stored level past the top down to it. The meter starts again every visit; only the level is kept. `answer()` returns `Step::Up`, `Down` or `Stay`, and the caller stores the level when it is not `Stay`.
- **`Course::QuestionDeck`** -- every item of a numbered level, up to 256, dealt once per pass in a new order: one bit per item, nothing stored but the bits. A pass stops short of its last eighth, which is owed to the next pass and dealt first in it, so the end of a pass cannot be counted down to and nothing waits longer than two passes. `missed(item)` brings an item back after `RETRY_AFTER` (three) others, until it is answered right. Under a hundred bytes, no heap.

Where the level lives is the runtime's business, not the course's: `AppRuntimeKeep.cpp` holds one blob per player of `COURSE_SLOTS` entries keyed by a hash of the game's id and a track, reads it once per change of player, and writes it only when a level changes. `courseLevel()`/`setCourseLevel()` answer for the open game; `courseLevelOf()`/`storeCourseLevel()` for any game by id, which is what the console's `level` command calls. `tools/check_catalog.py` mirrors the hash and refuses two ids with one key.

## ScoreCatalog.{h,cpp}

Derived compatibility view over the playable apps that actually expose score metadata. `ScoresApp` reads scores directly from `AppRegistry`; keep this wrapper derived for any helper code that still wants a score-catalog-shaped accessor.

## RecentQuestions.h

Header-only ring buffer of the last 10 question tokens. Rejects recent repeats while keeping selection genuinely random: deliberately not a shuffle bag. Procedural games hash their question into a token; table-driven games use `pickIndex()`. It keeps a question from coming back too soon and does nothing to make sure every one is seen; where a level can be numbered, `Course::QuestionDeck` does both. Math uses this one from level 6, where a level is hundreds of questions or more.

## ContentLoader.{h,cpp}

Optional SD-card JSON overrides (Memory grid size/symbols) parsed with ArduinoJson. Counting's range used to be one too; it went when Counting became a course, because a range would change the size of a level under its deck. Purely additive: every game ships compiled-in defaults and must work with no card present. Format is documented in `docs/SD_CONTENT_SPEC.md`.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
