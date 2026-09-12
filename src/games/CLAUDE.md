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
- `RowList` section headings are struck through by a rule that starts a fixed 54px in, so keep them to about six characters. `NearbyApp` puts the peer's tag in the heading and everything else about it in rows for exactly this reason.
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

**A peer that stops talking pauses the game, and every game pauses the same
way.** The second way a two-player game ends never arrives as a message: a
flat battery, a child walking into the next room, a console sat on. Until
5.11 every nearby game sat on "is thinking" for ever when that happened. The
service now measures the silence (`nearbyPeerSilentMs()`, `NearbySeat::silentMs`)
and `NearbyWatch` (`src/games/NearbyWatch.{h,cpp}`) turns it into three states
and one card, so the games cannot each decide what "too long" means:

- *Present* -- heard within `NearbyPlay::PEER_QUIET_MS` (6s: several missed
  scan windows, because missing one is ordinary). Play on.
- *Quiet* -- the game pauses. It cannot proceed anyway. The card over the
  board says who it is waiting for and for how long, with **Keep waiting**
  and **End game**. Keep waiting takes the card away and leaves the game
  paused with its own status line saying why; the card comes back once if
  the peer then goes Gone, because that is a new fact.
- *Gone* -- the scanner has dropped the peer (`SIGHTING_TTL_MS`, 45s). Same
  card, "out of range". Waiting has no time limit: the game is saved after
  every move, so waiting costs nothing. A game with more than two seats can
  offer something here that a two-player game cannot: Ludo's host gets a
  third button, *Play without* (see the Ludo section).

Coming back needs nothing. A turn is state that stays on the air, so a
console that reappears in the same session re-hears the current move and
the game resumes where it stopped -- `NearbyWatch::resumed()` is one frame,
the game repaints whole (the card was over the board) and plays `Pop`. End
from the card is the same ending End game sends, so a console that does come
back goes to its lobby rather than to a board nobody is playing.

Every game hooks it the same way: `updatePause()` after the poll and the
republish (so a move that did arrive is applied before the silence is
measured) and before any tap (so a press through the card is never a move);
`drawPause()` at the end of BOTH render paths, with `pausePainted_` cleared
whenever anything under the card is repainted. Only a live remote game is
watched -- not the lobby, not an unanswered invitation (which has its own
"Asking..." and its own End), not a game the rules have finished. A new
nearby game does the same at the same places; do not invent a fourth state.

Nothing about this transmits. It is derived from the ABSENCE of the beacon
that is already there, which is why it needed no payload change and no
agreement about what goes on the air.

**And the one case that can be anticipated is.** Your own battery dying is
the one silence you can see coming, so every nearby lobby's footer line
becomes "Battery low: a nearby game may not finish", in the warning colour,
when `AppContext::batteryLow()` says so -- the board's own
`BATTERY_LOW_PERCENT`, from the published snapshot, never the ADC. It
replaces the broadcast note rather than adding a line, because the lobbies
have one line to give. A new nearby lobby should do the same in the same
slot.

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

## Chess and Sea Battle

Both were one `.cpp` each until they passed the size the modularity rule
allows, and both are now split the way Backgammon is: `ChessGame.cpp` (the
game's life, a fresh board, a tap on it), `ChessRules.cpp` (move generation,
check, the endings), `ChessDraw.cpp` (geometry and every pixel, the lobby's
row geometry included), `ChessNet.cpp` (the lobby and the poll that plays a
nearby game) and `ChessSave.cpp`, sharing `ChessInternal.h` for the three
square helpers and the back rank that more than one of them needs. Sea Battle
is `SeaBattleGame.cpp` (the fleet and a shot as well as the screen -- its
rules are eighty lines), `SeaBattleDraw.cpp`, `SeaBattleNet.cpp` and
`SeaBattleSave.cpp`. The split was by line range and nothing moved changed;
`ChessInternal.h` is for the Chess files and nobody else, like
`NearbyPlayState.h` in the engine.

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
  that has not heard it yet still will.
- **A console that goes quiet pauses the whole table, and the host may play
  on without it.** The same `NearbyWatch` as the two-player games, over every
  chair but our own (`tickTable()`), because a seat that cannot ack the last
  ply stalls `canPublish()` for everyone anyway. Once a chair is *Gone* --
  not merely Quiet: six seconds is a scan gap, forty-five is somebody who
  left -- the host's card carries a third button, *Play without X*. That
  makes the chair's seat a computer seat the host plays, and it has to be
  SAID, because every other console reads that seat's turns from the chair
  it came with: `Ludo::Net::takeoverFrom(seat)` is a numbered ply like a
  move, from 16..19 where nothing already on the air can be mistaken for
  it, published once the remaining chairs have acked the last ply
  (`pendingDrop_` until then, and until then the seat is nobody's, so no
  move for it can get ahead of the word). Only the host's takeover counts.
  A guest that hears its own seat taken goes back to its lobby, told
  "<host> played on without you" -- there is no way back into a seat a
  computer now holds. `droppedChairs_` is saved with the game; a chosen
  but unsaid takeover is not. No payload change and no new flag: it is a
  new meaning for the field the moves already use.
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

## Go

Split like Backgammon: `GoRules` (the rules, scoring and the wire encoding)
and `GoAi.cpp` (both computer levels and the dead-stone estimate, declared
in the same header), both pure and host-tested by
`test/host/go_rules_test.cpp`; `GoGame.cpp` (flow and input), `GoDraw.cpp`,
`GoNet.cpp` (the lobby and the nearby game) and `GoSave.cpp`. `GoRules.h`
states the rules as played.

- **Legality has one definition: `Go::legal()`**, and one way to change the
  board: `Go::play()`, which refuses an illegal move and changes nothing.
  The ghost, the computer, and every move off the air go through them.
  Simple ko, not superko; suicide illegal; a capture is never suicide.
- **Five rule sets are one enum**, chosen in the lobby and fixed for the
  game. Capture 1/3/5 forbid passing (`legal(PASS)` is false), so the Pass
  button greys and the computer plays its least bad move rather than
  passing. Territory sets `over` on the second pass with `winner` still
  EMPTY: the marking phase decides, through `score(dead)`.
- **The computer never fills its own eye** -- `fillsOwnEye()` is the one
  veto in the move generator and in the random playouts, because a playout
  that fills its own eyes kills its own groups and judges every position
  wrong. Easy is `chooseEasy()`, now. Medium is `beginSearch()` then
  `stepSearch()` with a microsecond budget each frame (`CPU_SLICE_US`)
  until `bestMove()`; the `Search` is a fixed 2.6 KB member. The dead-stone
  opinion under Territory is `playoutOwnership()` two per frame from
  `updateMarking()`, so neither ever costs a frame.
- **Two taps place a stone.** `tapBoard()` sets a ghost on the nearest point
  (`pointAt()` never answers "nowhere" on the board); the same point tapped
  again, or Place on 19x19, plays it. The ghost follows a held finger. Do
  not add a one-tap path: a misplaced stone is for ever.
- **Repaint is per point.** `dirty_` is a bit per point, `markChanged()`
  compares the board before and after a move, and `repaintPoint()` paints
  one cell whole -- wood, the grid through it clipped to the outer lines, a
  star, the stone, the marker, the ghost, a dead cross, a territory mark.
  The panel is four parts with stale flags. `markFullDirty()` is for entering
  the screen, a new game, Undo, the marking phase (territory can change
  across the board on one toggle) and the result card. The 19x19 magnifier
  is the info box and redraws whenever a point does.
- **19x19 is a compile-time fact about the panel**, `SCREEN_WIDTH >= 480`,
  read once as `BIG_BOARD_AVAILABLE`. Playable games always draw a 320x240
  canvas and get their touch scaled to it, so the 4-inch board buys pixels,
  not coordinates; the magnifier is what makes ten logical pixels a target.
  On a small board the Board chip does not exist rather than being refused.
- **On the air, a Go turn is an eleven-bit word** across the service's two
  six-bit fields (`Go::Net`): two bits of kind -- stone, pass, dead-group
  toggle, accept -- and nine of point. `to` carries a fixed high bit so the
  presence word cannot decode, and the reserved ending decodes to a point
  off every board; both static_assert'd. The marking phase is the one place
  both sides speak out of turn: toggles apply as they arrive from either
  side, any toggle unagrees both, and the game is scored when both have
  accepted. Undo and Resume do not exist across consoles.
- **Board size does not travel.** An invitation cannot carry it, so each
  console plays the size its own chip says; a 19x19 console meeting a 9x9
  one is refused by move validation rather than merged. The lobby only
  offers 19x19 where it can be played, which is one board, so in practice
  the question does not arise; if a second big board is ever supported, the
  size needs to go on the air, and that is a change to what is transmitted.
- **Saved packed**: two bits a point, so 19x19 is a hundred bytes. Undo is
  not saved. The lobby's chips are, and come back whatever else does.

## Tracing games

`LetterTracer` is the finger-tracing engine: waypoint resampling, hit testing,
the pulsing next-dot, the side columns of controls, the progress bar. Trace
(print) and Cursive are shells over it -- a glyph table, a list of alphabets,
and a name. Add a third tracing game the same way; do not copy the engine.
It is four files: `LetterTracer.cpp` (logic), `LetterTracerDraw.cpp`
(painting), `LetterTracerArrows.cpp` (where the arrows go) and
`LetterTracerWords.cpp` (printed words), sharing `LetterTracerLayout.h`.

**The players are as young as five, and user testing has overruled the
engine twice.** Arrows on the path confused them and cursive words were too
small to follow. Before making the guide cleverer, ask whether a five-year-old
who has never held a pencil to joined writing would read it.

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
- **TWO KINDS OF ARROW, and the difference is what each answers.** The
  numbered ones are the PLAN -- which stroke starts where and sets off which
  way -- so they are placed once, never move, and are all muted. The GUIDE is
  one arrow that follows the dot being aimed at, in the highlight colour, and
  answers "which way now": players said an arrow that only appeared at the
  start was an arrow that does not move. `updateGuide()` picks the clearer
  side of the line and draws nothing when neither side is clear, and
  `moveGuide()` repaints it inside its own box -- it moves a couple of times a
  second, so a full repaint there would be a permanent flash.
- **A `Set` carries its dot radius**, because Cursive's loops pass within a few
  pixels of themselves and print's do not: 1 for cursive, 2 for print. The dot
  being aimed at keeps `NEXT_R` either way, so "where next" stays the biggest
  thing on the canvas.
- **Numbered arrows sit BESIDE the stroke, outside the letter, and never
  move.** Every stroke gets a numbered arrow beside its start; a set with
  `turnArrows` also gets one at each sharp reversal (`TURN_COS`, measured on
  the authored vertices, never the resampled dots). `planArrows()` places them
  once per glyph by measuring candidates against the strokes: outside the
  letter first, then sliding along the stroke, standing further off, and only
  then inside. They replaced a single arrow on the path that jumped from turn
  to turn, which five-year-olds in testing could not tell apart from the dots.
  Cursive and all word sets have `turnArrows` off: loops everywhere make an
  arrow at each one noise.
- **A stroke finishing is a partial repaint.** The start ring is painted out
  in the background colour, the ghost and dots (both idempotent) are painted
  back over it, and the arrows recolour in place. A printed word is up to eight
  strokes, and a screen clear between each was exactly the flashing the root
  rendering rule forbids. `tools/gen_screens.py` restates the placement
  candidate for candidate; change both or the stills lie.
- **A `Set` may be spelled rather than stored** (`alphabet` names the table
  index of 'a'). Trace's Words tab is strings only: each word is laid out from
  the lowercase letters in `LetterTracerWords.cpp`, all at one scale set by the
  widest word. A wide word shrinks every other word, which is why the list has
  no `quiz`, `mud` or `web`. Cursive cannot do this -- joining is the skill --
  so its words are generated.
- **Cursive's letterforms are generated** by `tools/gen_cursive_glyphs.py` from
  a GPLv3 dotted teaching font. Both `CursiveGlyphData.h` and `.cpp` are
  generated, including the counts, which are `constexpr` because the game's
  `Set` table is. Edit the script, never the output, and **look at
  `docs/cursive-sheet.png`** afterwards: a malformed cursive `q` reads as a
  perfectly good 9 until a child copies it, and nothing else will tell you.
  Its word list is `KID_WORDS`, two and three letters, under
  `WORD_WIDTH_CAP` -- the cap is what sets how big every word is drawn, and a
  listed word over it fails the script rather than quietly vanishing.

## Cinnamon

The reference for partial redraw, and -- since 5.10.0 -- no longer the one
screen that ignores the owner's theme. It used to call `Ui::setTheme(Light)` at
the top of each render half and restore the palette at the bottom, which is why
entering it from a dark launcher flashed white.

**Do not reintroduce that.** The theme is global state in `Ui`, and the two
halves are two calls: on a partial repaint `renderStatic()` does not run at
all, so a force in one and a restore in the other leaves the whole firmware
drawing in this screen's palette. What made the forcing look necessary was two
hard-coded colours inside `drawPad()` -- a black ring and a grey outline, both
invisible on a dark ground -- and those are `Ui::text()` and `Ui::outline()`
now. The four pad hues stay fixed on purpose: they are the game, the way a
traffic light is not themeable, and they are fills rather than text, so nothing
has to be read off them.

## Settings repaints one control, not the tab

`SettingsGame` keeps a `dirtyRect_`. A handler calls `markControl(rect)` with
**the same Rect the control was drawn from**, and `renderDynamic()` clips the
repaint to it with `setViewport`, then re-runs the tab renderer whole. The
renderers are idempotent, so everything outside the box is drawn and discarded
by the clip -- which is what lets a control be repainted without any renderer
knowing it is being repainted alone.

An empty rect means the whole body, and that is right for a tab change, a
rotation, the first paint, and for the few changes that genuinely alter the
tab: muting greys every other control on the Sound tab, and the Power tab's
rows restate themselves in the footnote below. Those use plain `markDirty()`
rather than `markFullDirty()`, because the top bar and the tab strip above the
body did not change and repainting them costs a battery read.

Two things to know before adding a control:

- **Never type in a rectangle.** Derive it from the control's own helper, or
  the clear box and the drawing will drift apart -- the failure the root
  `CLAUDE.md` describes at length.
- **A change that alters something OTHER than the control you touched has to
  say so.** Disarming the factory-reset confirmation changes the reset row's
  label from a tap somewhere else entirely; the old whole-body clear covered
  that by accident and a clipped repaint does not.

## Shared data

- `CountryData.{h,cpp}`: 195 countries: ISO2, capital, continent, difficulty tier.
- `CountryDataTable.cpp`: generated; edit `tools/gen_country_facts.py` and regenerate rather than hand-editing.
- `ElementData.{h,cpp}`: all 118 elements: symbol, name, atomic number, chart cell, category, room-temperature state, difficulty tier, and one player-facing line. `.cpp` holds only the lookups.
- `ElementDataTable.cpp`: generated; edit `tools/gen_elements.py` and regenerate. The generator is where the real checking lives — unique chart cells, fact length, tier lists naming real symbols.
- `MazeData.{h,cpp}`: static maze layouts used by Maze; keep path data out of the redraw logic.
- `StateData.{h,cpp}`: 50 US states: code, name, capital, tier.
- `TraceGlyphData.{h,cpp}`: static trace stroke geometry used by Trace; keep glyph data out of the touch/render logic.

Flag and outline artwork comes from the `map-n-flag` library and is blitted via `Ui::drawCountryImage*()`, which streams 4-bit indexed rows straight out of flash.
