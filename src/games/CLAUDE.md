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
- Feedback via `beepOk()` / `beepError()` for a right and a wrong answer: each
  pulses the RGB LED and plays its cue.
- **Anything else a game wants to say goes through `playSound(Sound::...)`**,
  from the fixed vocabulary in `hal/Sound.h` -- `Coin` for a point scored,
  `Whoosh` for something sliding, `LevelUp` for a round cleared, `Victory` and
  `GameOver` for the end of one, `HighScore` for a personal best, `Countdown`
  for a timer running out, `Step` for a piece walking one square, `YourTurn`
  when play comes round to the person holding the console. Pick the cue that matches what actually happened,
  not the one that sounds nicest: the whole value of the vocabulary is that
  `Coin` means the same thing in every game. There is no way for a game to ask
  for a frequency, deliberately -- if none of the words fits, add one to
  `Sound.h` rather than working around it.
- `playSound()` does **not** pulse the LED. On a board with no codec that pulse
  is the entire feedback, so a cue replacing a `beepOk()` at a moment that
  deserves a colour must keep the `pulseRgb()` call by hand -- `MazeGame` and
  `MemoryGame` are the worked examples.
- Sound is silent on a board with no audio hardware, and silent again when the
  owner has muted it in
  **Settings -> Sound**. Nothing may *depend* on a cue having been heard, and
  no screen should consult `soundEnabled()` to decide what to do -- the gate is
  inside `playSound()` and a second check outside it is how the two get to
  disagree. The one exception is the Sound tab itself, which has to *describe*
  the state it is offering to change.

  Which boards those are has changed: the Freenove has a codec, and the
  E32R28T-1, the inverted-panel CYD, the E32R32P and the E32R40T drive the
  ESP32's built-in DAC. Every app is offered on
  every board regardless -- `playSound()` compiles to nothing where there is no
  audio path, so an app needs no guard of its own, and one that leans on sound
  should carry the same information visually. Piano draws the note name on the
  key for exactly that reason.
- Assume a fixed 320x240 landscape canvas **unless the app sets `followsLayout`**. That is the default and it is right for almost everything: the runtime forces landscape and scales the canvas up on a bigger panel, so a game gets free upscaling and never thinks about orientation. Piano and Chess are the exceptions.
- **`followsLayout` is both halves or neither.** An app that sets it honours the owner's orientation AND draws through the raw renderer at the panel's real size, with touch delivered unmapped. Read `tft.width()`/`tft.height()` at render time; never `SCREEN_WIDTH`, `SCREEN_HEIGHT` or `GAME_CANVAS_*`. The two are decided by the same flag on purpose -- a game with real pixels to draw on and canvas coordinates to hit-test against lays a third of itself off the edge of a portrait panel, and the serial log looks perfectly healthy while it does.
- System/UI apps (Settings, Wi-Fi, SystemInfo, Profiles, Scores, About, and any future app-style screens) must support both landscape and portrait orientations. Read `tft.width()` / `tft.height()` at render time rather than the compile-time constants `SCREEN_WIDTH` / `SCREEN_HEIGHT`. Use `Ui::drawTab()` + `Ui::drawTabBaseline()` for multi-section content; the tab strip adapts naturally when you divide `tft.width()` at render time.
- Never read a setting from `board` more than once per screen change in a hot path. `Preferences` is flash-backed, so every getter is an NVS lookup. The frequently-read settings have write-through RAM mirrors in `Board`; add to those rather than reaching for a fresh getter each frame. Never call anything containing a `delay()` from `render()` or `update()`.
- Don't rebuild content that didn't change. Scrolling changes an offset, not content. Gate expensive rebuilds behind a stale flag.
- **A full redraw is the exception, and that applies to every game here.**
  `markFullDirty()` costs ~150KB over SPI and ~30ms of visible blanking, so
  doing it to change a few pixels is a flash in the player's hand rather
  than a lost optimisation. Something appeared? Draw it. Something moved?
  Erase its own box, derived from the geometry that drew it, and repaint the
  guide over that box -- idempotent draw functions can be re-run whole, which
  is simpler than working out what overlapped. Something animating on its own
  clock? Change its colour rather than its size, so it never has to be
  erased. Repaint fully only when the scene genuinely changed. The worked
  example is in the root `CLAUDE.md`: the tracer's direction arrow cleared
  the screen eleven times per word before this was written down.
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

## Two-player games on nearby consoles

`AppContext`'s `nearby*` calls are a **service**, not a chess feature. A game
gets seats (who is in the room, with the owner's own label for each), an
invitation, a numbered turn each way, and an ending. It never gets the radio:
the surface is move-shaped, so a game can say "I played X to Y" and cannot say
"put these bytes on the air".

Two questions are answered by the service so that the next game cannot answer
them differently. **Who moves first** is a coin toss inside `nearbyInvite()` --
the console doing the asking must not also claim first move. **"I am stopping"**
is a reserved turn encoding the service owns, delivered as `NearbyTurn::ended`;
never invent a second one.

**An invitation names its game.** A lobby accepts one only when
`NearbySeat::forThisGame` is set -- otherwise a Sea Battle invitation answered
from the Chess lobby is two consoles playing different games at each other.
Every lobby checks it: Ludo's table lobby directly, and Chess and Sea Battle by
folding it into `inviting` as the seat list is copied, so an invitation to
another game reads as an ordinary "Play A4F2" row. A new two-player lobby
should do the same at the same place.

**More than two seats is the same service.** Ludo reads every other console's
turn by tag, invites them one at a time (only one invitation is on the air at
once), and orders the table with `nearbySelfId()`. Who moves first there comes
from the table's seed rather than the coin toss -- a toss between two cannot
seat four -- and a console replaces its turn only once every other console has
acknowledged it. See the Ludo section below and `LudoRules.h`'s `Net`.

Who can use it:

- **Switching the radio on is admin-only** -- *Settings -> Device -> Beacon*,
  then *Nearby*. That is a privacy decision and belongs to an adult.
- **Playing is not.** Nothing in `NearbyPlay` or `ChessGame` checks the active
  profile, so once an adult has switched it on, any player on the console can
  invite and be invited. Do not add a profile check to a play path; the gate is
  the switch.
- **Saved games are per-profile**, because `saveBlob()` is transparently
  profile-scoped. Each player has their own game in progress. Guest drops
  writes, so a guest's game does not survive leaving the screen -- the same
  answer Guest gives to scores.
- **Naming a peer stays admin-only.** It is a device-wide label, not a personal
  one, and it is what every screen then calls that console.

## Ludo

Split on purpose: `LudoRules` (rules, the computer player and the table
protocol), `LudoGame.cpp` (flow and input), `LudoBoard.cpp` (the board),
`LudoPanel.cpp` (the side panel), `LudoLobby.cpp` (the seat picker, drawn and
answered, and the table lobby), `LudoTable.cpp` (play across consoles) and `LudoSave.cpp`. The
header comment in `LudoRules.h` states the rules as played; read it before
changing one, because several are decisions rather than the only reading of the
board game.

- **`LudoRules` is pure C++ and must stay that way.** No Arduino, no drawing,
  no `millis()`, no `random()`. Every function is a function of a flat `State`
  and a seed. That is what will let a second console compute the same game from
  the same inputs, and what lets the rules be tested off the device:
  `test/host/ludo_rules_test.cpp` builds against the real `LudoRules.cpp` with
  any host compiler (the command is at its top) and plays 3,000 seeded games
  checking invariants after every move. Run it after touching a rule. CI does
  not, yet.
- **The dice are a function, not a generator.** Roll *k* of a game is
  `Ludo::die(seed, k)`. Nothing may draw a die any other way, and the computer
  player's tie-breaks come from a separately salted `mix()` of the same seed so
  that asking it for a move can never change what the die says next.
- **Legality has one definition: `Ludo::target()`.** `move()`, the highlights,
  the computer player and `tokenAt()` all ask it. Never test a move any other
  way, and never apply one without going through `move()`, which refuses an
  illegal one and changes nothing.
- **The phase is derived, never saved.** `enterTurn()` works out whether a
  seat must roll, is holding a roll, or is looking at a roll it cannot use,
  from the rules state alone. A restored game therefore cannot disagree with
  its own position -- and cannot grant a free reroll for putting the device
  down, because the roll it was holding is part of the state.
- **The screen draws `shown_`, not the state.** A move is applied to the rules
  at once and saved at once; the hopping token is `shown_` catching up a square
  at a time. Keep those two apart, or a Lock pressed mid-hop loses the move.
- **Repaint is per place.** 225 grid cells, 16 yard spots and the centre each
  have a dirty bit, and each is drawn by an idempotent function that paints its
  whole box. A move repaints two or three places. `markFullDirty()` is for
  entering the screen and starting a game, nothing else.
- **Computer seats are a seat kind, not a mode.** Any mix of Player and
  Computer, at least two seats and at least one Player. The computer's roll and
  its move are each delayed (`CPU_ROLL_MS`, `CPU_MOVE_MS`) so a child can see
  what it did; without them a computer's whole turn is one frame.
- **Across consoles, every seat has one decider.** `ownsSeat()` is true for
  the person holding this console and, on the host, for the computer seats;
  every other seat is `remoteSeat()` and its turns come only off the air, in
  ply order, through `Ludo::Net::accept()` -- which checks them against the die
  this console computed, so nothing is applied that the rules did not allow.
  A seat this console owns publishes its ply the moment the roll is resolved
  (`publishPly()` from `doRoll()` or `startMove()`), and may only roll once
  `canPublish()` says everyone has the previous one. The test plays 600 tables
  of separate state copies and requires them identical after every roll.
- **One console stopping ends the table.** A seat nobody plays stops everyone,
  so End game sends the service's ending and every console at the table --
  the one that ended it included -- goes straight back to its lobby, where
  the others read "A4F2 ended the game" until they tap. The ender's word stays
  on the air until its screen closes or it starts another game, so a console
  that has not heard it yet still will. A console that simply walks away
  stalls the game instead -- the radio cannot tell away from slow -- and
  anyone can then End it.
- **Whose turn it is blinks; nothing else does.** A dot beside that seat's row
  in the panel, and -- on the console whose person must roll -- the die's
  frame. Both change colour on one 400ms clock (`blinkPhase()`), never size,
  and each repaints only itself: the dot is its own column in the row, and
  the seat list is not repainted for a new turn at all, only when a place is
  decided. A remote seat's turn reads "Waiting for" and that seat's token.

## Backgammon

Split like Ludo: `BackgammonRules` (the rules) and `BackgammonAi.cpp` (the
computer, declared in the same header), both pure and host-tested by
`test/host/backgammon_rules_test.cpp`; `BackgammonGame.cpp` (flow and input),
`BackgammonDraw.cpp`, `BackgammonNet.cpp` (the lobby and the nearby game) and
`BackgammonSave.cpp`. `BackgammonRules.h` states the rules as played.

- **Legality has one definition: `Bg::legalMoves()`.** A move is legal only if
  the rest of the dice can still reach the most that can be used, and with two
  different dice of which only one can be played, it must be the higher if
  that one can be. The highlights, the computer, `findMove()` and every move
  from another console go through it. The test compares it with a brute-force
  search over every move order on thousands of positions from real games.
- **Doubles are searched in a canonical order** (each source no further from
  home than the last). It reaches every final position the full search does,
  which the same brute-force comparison proves, and cuts the worst case from
  tens of thousands of nodes to a few thousand.
- **The dice are a function of the seed**, as in Ludo. Locally the seed comes
  from `Entropy`; across consoles from `Bg::tableSeed()`. Roll is still a tap:
  it reveals a number that was fixed, which is all a physical die does too.
- **Done is part of the turn.** It makes Undo usable for the last move, it is
  when the device is passed, and in a nearby game it is when the turn goes on
  the air -- a move taken back before Done was never transmitted.
- **The computer's moves are checked before they are played.** Its search has
  a node ceiling so no position can hold the frame; the game re-validates each
  planned move with `findMove()` and falls back to the first legal one, so a
  cut-short search plays a weaker move, never an illegal one.
- **Repaint is per place**: 24 points, the bar and the tray, each an
  idempotent draw of its whole box, plus four panel parts each redrawn only
  when what it shows changed. A move repaints two or three places.
- **Nearby play is the lobby's third way to play**, beside Two players and
  Play the computer: consoles in the room are listed, an invitation to
  Backgammon reads "A4F2 invites you", and one to another game does not. Colour
  comes from the invitation's coin toss, who moves first from the opening roll.
  Either side ending the game sends both back to the lobby.

## Tracing games

`LetterTracer` is the finger-tracing engine: waypoint resampling, hit testing,
the pulsing next-dot, the side columns of controls, the progress bar. Trace
(print) and Cursive are shells over it -- a glyph table, a list of alphabets,
and a name. Add a third tracing game the same way; do not copy the engine.

- **The controls are in side columns and must stay there.** A child tracing the
  top of a letter runs a finger off the top edge, and buttons above or below the
  canvas sit in the natural overshoot of the gesture the game teaches.
- **A `Set` carries its own dot spacing.** A single letter fills the canvas and
  wants 20px; a three-letter word is a third of the height and gets two dots
  per letter at that number. Zero means the default.
- **A `Set` may carry `names`** when one character cannot say what is being
  traced -- that is how the word sets caption themselves, since `Glyph::label`
  is a single char.
- **A `Set` may open at a random entry** (`randomStart`). Right for words and
  wrong for an alphabet: ABC is the order a child is learning, while always
  being handed the same word first makes fifty words feel like one.
- **A glyph table declares the box it was authored in.** `configure()` takes
  `coordW`/`coordH` and the tracer scales BOTH axes by one number, letterboxing
  the remainder. Two scales is how every glyph came to be drawn 22% short:
  x by canvas-width/200 and y by canvas-height/200 are only equal when the
  canvas is square. If you author a new table, give it the canvas's shape when
  width matters (words) and a square when it does not (single letters).
- **Direction arrows come from the geometry, not from the data.** A waypoint is
  a turn when the angle between arriving and leaving exceeds `CORNER_COS`, and
  no turn is marked within `CORNER_GAP` dots of the last -- without that gap a
  tight curve marks every dot. The arrow moving is a change of shape, so it
  takes a full repaint; corners are a handful per glyph, so that is rare.
- **Cursive's letterforms are generated** by `tools/gen_cursive_glyphs.py` from
  a GPLv3 dotted teaching font. Both `CursiveGlyphData.h` and `.cpp` are
  generated, including the counts, which are `constexpr` because the game's
  `Set` table is. Edit the script, never the output, and **look at
  `docs/cursive-sheet.png`** afterwards: a malformed cursive `q` reads as a
  perfectly good 9 until a child copies it, and nothing else will tell you.

## Shared data

- `CountryData.{h,cpp}`: 195 countries: ISO2, capital, continent, difficulty tier.
- `CountryDataTable.cpp`: generated; edit `tools/gen_country_facts.py` and regenerate rather than hand-editing.
- `ElementData.{h,cpp}`: all 118 elements: symbol, name, atomic number, chart cell, category, room-temperature state, difficulty tier, and one player-facing line. `.cpp` holds only the lookups.
- `ElementDataTable.cpp`: generated; edit `tools/gen_elements.py` and regenerate. The generator is where the real checking lives — unique chart cells, fact length, tier lists naming real symbols.
- `MazeData.{h,cpp}`: static maze layouts used by Maze; keep path data out of the redraw logic.
- `StateData.{h,cpp}`: 50 US states: code, name, capital, tier.
- `TraceGlyphData.{h,cpp}`: static trace stroke geometry used by Trace; keep glyph data out of the touch/render logic.

Flag and outline artwork comes from the `map-n-flag` library and is blitted via `Ui::drawCountryImage*()`, which streams 4-bit indexed rows straight out of flash.
