# src/games

One `.h`/`.cpp` pair per game or system screen. Settings, Wi-Fi, Profiles, Scores, System Info and About are `Game` subclasses (`../engine/Game.h`); the launcher is also a `Game`, but it lives in `src/engine` because it is part of the runtime shell.

## Lifecycle

```cpp
const char* title() const;                                // display name
void begin(AppContext& host);                             // ordinary game
void update(AppContext& host, const TouchPoint& touch);
void render(AppContext& host);
void end(AppContext& host);                               // optional
Keep keepsGame() const;                                   // what leaving keeps
void resume(AppContext& host, uint32_t awayMs);           // optional
```

System/UI screens that genuinely need privileged access still subclass `Game` directly and keep the `GameHost&` signatures. Right now that list is exactly: Launcher, Settings, Wi-Fi, Profiles, Scores, About and System Info. Guard protected actions with `requireCapability()`.

`main.cpp` calls `begin()` -> `render()` -> `clearDirty()` on launch, then each frame calls `update()` and re-renders only when `needsRender()` is true. On leaving, `end()` is called once via `leaveActiveGame()`, before the next screen's `begin()`.

The default `end()` does nothing, which is right for a game holding only its own members. Override it if your screen acquires anything that outlives a frame. Every transition also compares free heap against the value captured before `begin()` and logs `[heap] '<screen>' left N bytes short` if a screen does not hand it back: watch the serial log after adding one.

**What a game owes a player who left in the middle.** Players press Home by mistake. The runtime keeps the game -- the instance is static, so everything is still there -- and brings it back with `resume()` in place of `begin()`. The game says how, as the screen is left, from `keepsGame()`:

| Answer | When | What the player gets |
|---|---|---|
| `Keep::Ask` (the default) | a round under way, a score, a puzzle part done | *Carry on? / New game*, then `resume()` or `begin()` |
| `Keep::Quietly` | the screen holds a place, not a game -- the letter being traced, the open tab -- or the game has its own control to end one (every board game) | straight back, `resume()` |
| `Keep::No` | a result or game-over screen, a menu, nothing achieved yet, a tool | `begin()`, as before |

Decide it for every state the screen can be in. Two rules: **never Quietly for a state the player cannot leave by a control on the screen** -- with no question on the way in, that is a game they are shut in until the console is switched off -- and **never test for "nothing pressed yet"**, which the runtime already does.

`resume(host, awayMs)` is for a game with a clock. Every deadline and stopwatch is a `millis()` stamp, and the game was not running: move each on with `movedOn(stamp_, awayMs)`, or start it again where a fresh window is fairer (the mole that was up in Whack A Mole). Never reset progress, never draw -- a full repaint follows. **It is also called after the screen saver, sleep and the lock screen**, when nothing was lost, so it must leave a paused game exactly as it was, and it must be harmless on a result screen. A game with no `millis()` in it needs no `resume()` at all.

What `begin()` loads -- a best score, a config -- is not loaded again on the way back. That is right for the same player, which is the only player a game is kept for.

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
- **Everything a player presses is drawn as a stop for the d-pad.** A console
  with keys finds its controls as they are drawn: a fill followed by an
  outline of the same rectangle, square or rounded. A board drawn as one
  picture has nothing to stop on, so draw each square, key or cell as such a
  pair -- the outline in the fill's own colour changes nothing on the panel
  (Chess, Go, Backgammon, Tic-Tac-Toe, Elements, Piano's lower keys), or put
  the pair under what is drawn on it. A stop goes away when a fill wider than
  it covers its middle; a fill inside it is it repainting itself. Keep what
  is not pressed from being a pair (Maze's walls, Odd One Out's squares), and
  the stops under 160 and at least 12px a side. A screen that waits for a tap
  anywhere still needs one stop that is not the header (Sea Battle's
  hand-over). Check with the host: `BRAINO_FOCUS_LOG=1` logs every stop.
- Never block `update()` for more than a second or two. The loop is watchdogged (`Watchdog::TIMEOUT_SECONDS = 12`) and a long busy-wait reboots the device. Games do not feed or touch the watchdog themselves; if you genuinely must block, ask `Board` to do it behind a `Watchdog::Pause`.

## The four-button quiz screens

Math, Multiplication, Time, Flag, Percent, Space and Roman are one layout:
header counters, a question panel, four answer buttons at
`(18 + col * 152, 144 + row * 46, 132, 38)`, a strip between them. Math and
Multiplication share the panel too, `(26, 70, 268, 56)`. If you add another,
copy `MathGame` or `SpaceGame` rather than inventing the rects again -- two
screens with the same shape should not put their buttons in two
nearly-identical places. Counting is the cousin: the same header and strip
idea, but a picture panel and four buttons in one row at
`(15 + i * 76, 188, 62, 40)`.

Three things about that family are load-bearing, and all three are mistakes
somebody already made here:

- **The question panel is DYNAMIC, gated on a `drawn...` flag.** Classing it as
  static means only `markFullDirty()` can repaint it, which makes every new
  question a 320x240 wipe.
- **Invalidate the button trackers in `newQuestion()`.** A button repaints only
  when its *state* changes, and on a new question the two that were never
  highlighted go 0 -> 0 -- so they keep the previous question's labels.
- **Derive the strip below the panel from the panel's own bottom edge.** Math
  typed both in and they overlapped by five rows for two releases, erasing the
  panel's border on every answer. `docs/RENDER_AUDIT.md` has the full story.
  Multiplication and Counting carried the same overlap, five rows and six,
  until they became courses.

**Wrap text with `Ui::wrapLines()`, not `Ui::drawWrappedText()`.** The second
builds every line with `String`, which is right where it is used (a game
message changes when the game says something) and wrong for a question that is
rewritten every few seconds for as long as a young player keeps playing. `wrapLines()`
writes into the caller's fixed buffers, allocates nothing, and returns how many
lines the text *needed* -- which is what lets a panel say "wrap at font 2, and
if that wanted a fourth line, wrap it again at font 1" instead of discovering
the overflow on the panel. TFT_eSPI cuts an overlong string at the viewport
edge with no mark and mid-word, and `tools/gen_screens.py` cannot reproduce
that, so a label that overflows looks perfect in every mock-up.

## A quiz that is a course

Math, Multiplication, Counting, Shape Arith, Fingers, Time, Number Line, Calendar, Color Mix, Shapes, Odd One Out, Money and Fractions keep each player's level between visits and
deal every question of a level before any comes back. The pieces are
`../engine/Course.h` (`Course::Ladder`, `Course::QuestionDeck`) and, for each
game, a pure `<Game>Course.h` that knows its levels: how many questions each
has, how to make question number `item`, and which wrong answers a player at
that level actually gives. The game itself only wires the two together, in
three places. From `MathGame`:

```cpp
void MathGame::begin(AppContext& host) {
    score_ = 0;                                   // the visit's, not the level's
    ladder_.start(MathCourse::LEVEL_COUNT, host.courseLevel());
    startLevel();                                 // deck_.reset(poolSize(level)), recent_.reset()
    newQuestion();
}

void MathGame::newQuestion() {
    const uint8_t level = ladder_.level();
    if (MathCourse::numbered(level)) {            // levels 1-5: a few dozen questions each
        item_ = deck_.next(esp_random());
        MathCourse::makeNumbered(level, item_, question_);
    } else {                                      // levels 6-12: too many to number
        for (uint8_t tries = 0; tries < 20; ++tries) {
            MathCourse::makeDrawn(level, esp_random(), question_);
            if (!recent_.recentlyUsed(MathCourse::token(level, question_))) break;
        }
        recent_.remember(MathCourse::token(level, question_));
    }
    MathCourse::makeOptions(level, question_, esp_random());
}

// in update(), on the one press a question gets:
if (!right && MathCourse::numbered(ladder_.level())) {
    deck_.missed(item_);                          // before the ladder moves
}
const Course::Ladder::Step step = ladder_.answer(right);
if (step != Course::Ladder::Step::Up) right ? host.beepOk() : host.beepError();
if (step != Course::Ladder::Step::Stay) {         // climb()
    host.setCourseLevel(ladder_.level());
    startLevel();                                 // a new level is a new deck
    if (step == Course::Ladder::Step::Up) {
        announceNext_ = ladder_.level();          // "Level 4: Cross ten" in the strip
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::LevelUp);
    }
}
```

What each line is guarding against:

- **`courseLevel()` in `begin()` only.** Not in `resume()`: a game taken up
  again carries on with the ladder it has. And `begin()` is also New game on
  the *Carry on?* question, which starts the score again and must not start
  the level again.
- **`setCourseLevel()` only on Up or Down.** Storing every answer would be a
  flash write a question, and storing on the way out would write over a level
  the console set while the game was open.
- **`missed()` before `answer()`.** A step resets the deck, and an item number
  means something only on the level that dealt it.
- **One `answer()` per question.** Every screen in the family has one try; a
  game that offers a second must not let the second move the ladder.
- **The level's name, not a message about failing.** A step up names the new
  level in the strip over its first question (Counting, whose strip is under
  the picture, puts it on the question line and the question in the strip).
  A step down says nothing.
- **The wrong options are the course header's job, and its test's.** Each
  `<Game>Course.h` builds them from the level's own mistakes -- the carry
  forgotten, the smaller digit taken from the larger, the neighbouring fact --
  places the answer at random among them, and its host test checks the answer
  is not given away by where it ranks among the four (Math's, by its last
  digit as well).
  Noise around the answer does both of those, which is what these games had.

## Adding a game

The full checklist lives in the root `CLAUDE.md` under "Adding a game or an app": work through that, not this summary. It covers the docs, the screenshots and the verification steps, which is where things have actually been shipped broken. The code edits alone are:

1. In the game's own `.cpp`, add one `AppMetadata` declaration and a `...AppMetadata()` accessor. Keep `id` stable; blurb must stay under ~46 chars. The metadata block also owns launcher icon, launcher index and default visibility.
2. If the game records a score, declare one `AppScoreInfo` there too and point the metadata at it.
2b. If it asks questions at levels, make it a course (above): the level from `courseLevel()`, never from the score.
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

**An invitation names its game and its console, and stands until it is
answered.** A lobby accepts one only when `NearbySeat::forThisGame` is set --
otherwise a Sea Battle invitation answered from the Chess lobby is two
consoles playing different games at each other. And `NearbySeat::inviting` is
true only for an invitation aimed at THIS console: an invitation is a
broadcast, and a third console shown somebody else's could accept a game
nobody was offering it and sit in it for ever.

It used to leave the air after six seconds, like a poke. That left the console
that asked waiting in its lobby for an answer nobody could give any more, and
turned the other player's tap, a moment late, into a second invitation
crossing the first. It now stays up until the first turn replaces it,
`nearbyStop()` withdraws it, or another replaces it -- so **no game may time
an invitation, re-send one or give up on one**. The console that asked waits
until it is answered or the player takes it back. When two consoles ask each
other at the same moment, the one whose own tag (`nearbySelfId()`) compares
greater withdraws its invitation and accepts the other's; the one with the
smaller tag waits. No clock is involved, so the two cannot disagree.

**The lobby says who is there and what they are doing.** One row per console
in the room, re-read about once a second. First match wins:

| The seat says | The row reads | A press |
|---|---|---|
| `inviting && forThisGame` | "A4F2 invites you" (filled `success`) | accepts |
| `asking` | "A4F2 is asking B3C1" | nothing (Ludo: sits at that table) |
| `inGame`, `partner` set | "A4F2 is playing B3C1" | nothing |
| `inGame` | "Play A4F2 (in a game)" | invites |
| otherwise | "Play A4F2" | invites |

`inGame` is a move heard on that console's advertisement lately; a move does
not say which game it belongs to, so the row cannot either. `partner` is the
other console saying moves in the same session, when it is in the room too. A
console in a game whose partner cannot be seen may be waiting for THIS console
to come back to a game it has lost, which is why that row still invites: the
banner on their screen tells them somebody is asking. **A row that takes no
press is not drawn as a button** -- a fill and an outline of one rectangle is
what the d-pad's selector stops on -- and the press handler skips it. The
lobby repaints when any of those fields changes for any row, not only when a
console arrives or leaves.

**A game across consoles belongs to the console, and only End game ends it.**
The other console is playing *this console*, whoever is holding it. So:

- **Its state is saved in the console's slot** (`saveConsoleBlob()` /
  `loadConsoleBlob()` / `clearConsoleBlob()`), never in the player's. It
  survives a change of player, it is kept for Guest (whose own saves are
  dropped) through a power cut, and it never overwrites a player's own game
  against the computer -- that one stays in the player's slot (`saveBlob()`)
  and is there again when the nearby game is over.
- **Every game saves under its own key** (`chess_game`, `go_game`, `bg_game`,
  `sea_game`, `ludo_game`). All five used to save under `"game"`, so leaving a
  Chess game and then playing Ludo destroyed the Chess game. An old save is
  read from `"game"` once, if it validates as this game's and is not a nearby
  game, and never written there again.
- **Opening the game**, in this order: the instance still holds a nearby game
  from this boot -> keep it as it is, finished or not; else the console's
  slot holds one still in play -> restore it and say its turn again; else the
  player's own save, or the lobby. If the result is not a nearby game,
  `nearbyStop()`. And if it is a local or computer game while an invitation
  for this app is waiting for this console, show the lobby instead, without
  touching the save -- otherwise the banner sends a player to a screen with no
  way to say yes.
- **Leaving the screen**: save; a nearby game still in play keeps its turn on
  the air and does nothing else; one that is over calls `nearbyStopLater()`,
  which gives its last word a minute to be heard and then takes it down;
  anything else calls `nearbyStop()`.
- **Nothing else ends it.** Not leaving the screen, the console sleeping, the
  saver, Keep waiting, a change of player or another game being opened. Only
  End game pressed here, the other console's ending heard, the game finishing
  by its rules, or (Ludo) the host playing on without a console.
- **`nearbyStop()` is safe to call from anywhere in a game.** It withdraws
  this console's invitation and takes the turn down only if THIS app put it
  there, so one game's lobby cannot remove another game's kept move. The air
  still holds one turn: a second nearby game that says a turn replaces the
  first game's, and the first says it again when its screen comes back.
- **A new game takes its session number from `nearbyFreshSession()`**, never
  from the clock: it steps past every session this console still holds a turn
  for, so an old game's last move or ending cannot be read as the new game's.

**More than two seats is the same service.** Ludo reads every other console's
turn by tag, invites them one at a time (only one invitation is on the air at
once), and orders the table with `nearbySelfId()`. Who moves first there comes
from the table's seed rather than the coin toss -- a toss between two cannot
seat four -- and a console replaces its turn only once every other console has
acknowledged it. See the Ludo section below and `LudoRules.h`'s `Net`.

**A peer that stops talking pauses the game, and every game pauses the same
way.** The second way a two-player game ends never arrives as a message: a
flat battery, a young player walking into the next room, a console sat on. Until
5.11 every nearby game sat on "is thinking" for ever when that happened. The
service now measures the silence (`nearbyPeerSilentMs()`, `NearbySeat::silentMs`)
and `NearbyWatch` (`src/games/NearbyWatch.{h,cpp}`) turns it into four states
and one card, so the games cannot each decide what "too long" means:

- *Present* -- heard within `NearbyPlay::PEER_QUIET_MS` (15s: missing a
  few advertisements in a row is ordinary). Play on.
- *Away* -- heard, but saying no move in this game for
  `NearbyPlay::PEER_AWAY_MS` (20s, `nearbyPeerAwayMs()`): it lost the game,
  started another, or switched Nearby off. A console that only left the
  screen keeps its move on the air and stays Present. Same card, "Not in this
  game for 34s". Without it the only sign was a turn that never came, with a
  console that looked perfectly present.
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
nearby game does the same at the same places; do not invent a fifth state.
While the card is up only its buttons are stops for the d-pad's selector, and
the card itself is not one: `NearbyWatch` says so through `ui/Stops.h`.

**The board says when it takes a press.** The d-pad's selector finds its
stops in what is drawn, and a board stays drawn while the other player
thinks. So every frame a game with a board calls
`Ui::setBoardStops(boardRect, live)` (`src/ui/Stops.h`): *live* exactly when
a touch on the board would be acted on now, and not on the other console's
turn, while the computer thinks, while an invitation is unanswered, when the
game is over, or while an animation owns the board. The lobby calls
`Ui::clearBoardStops()`. It is two assignments, nothing is drawn for it, and
the runtime clears it on every screen change. *Live* also makes every square
a stop however few are left -- beside buttons the selector otherwise takes one
to three plain squares for something to look at, which left a Ludo endgame
with two tokens and a die unplayable from the keys. The second rectangle is
for a control that stands apart from the board and is live with it (Ludo's
die).

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
  then *Nearby*. That is a privacy decision and belongs to an administrator.
- **Playing is not.** Nothing in `NearbyPlay` or `ChessGame` checks the active
  profile, so once an administrator has switched it on, any player on the console can
  invite and be invited. Do not add a profile check to a play path; the gate is
  the switch.
- **A saved game against the computer, or pass-and-play, is per-profile**,
  because `saveBlob()` is transparently profile-scoped; Guest drops those
  writes, the same answer Guest gives to scores. **A game across consoles is
  the console's** and is kept for whoever picks the console up, Guest
  included -- see above.
- **Naming a peer stays admin-only.** It is a device-wide label, not a personal
  one, and it is what every screen then calls that console.

## Chess and Sea Battle

Both were one `.cpp` each until they passed the size the modularity rule
allows, and both are now split the way Backgammon is: `ChessGame.cpp` (the
game's life, a fresh board, a tap on it), `ChessDraw.cpp` (geometry and every
pixel, the lobby's row geometry included), `ChessNet.cpp` (the lobby and the
poll that plays a nearby game) and `ChessSave.cpp`, over `ChessRules.h` /
`ChessRules.cpp` (move generation, check, the endings) and `ChessAi.cpp` (both
computer levels).

**Those last two are pure and must stay pure.** They are `namespace Ch`, they
include `<stdint.h>` and `<string.h>` and nothing else, and
`test/host/chess_rules_test.cpp` compiles them with a host g++ -- which is what
proves it, since the test will not build if an Arduino header ever leaks in.
That matters here more than the equivalent rule elsewhere, because
`ChessRules.cpp` carried a comment claiming purity for a long time while
including `ChessGame.h`, and therefore `Game.h`, `Ui.h` and TFT_eSPI. The
bodies were honest and the translation unit was not, and nothing could tell the
difference until something tried to build it off the device. A comment is not a
mechanism; the test is.

`ChessInternal.h` survives for the square helpers, but it now takes them from
`ChessRules.h` with four using-declarations rather than defining them --
deliberately four names rather than `using namespace Ch`, so a file has to say
what it is taking.

**The computer runs across frames, never on a task and never to completion in
one.** `ChessAi` splits its alpha-beta at the root: `stepSearch()` scores one
root move per call and the screen stops when the frame's microsecond budget is
spent, so a partial result can never be mistaken for a finished one. Easy is
one ply plus a recapture check. Both are deliberately weak -- a console that
beats a young player every time is a worse product than one they beat half
the time -- and both are deterministic in (position, seed), which is what makes
a bad move reportable rather than a story about one. Sea Battle
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
  its move are each delayed (`CPU_ROLL_MS`, `CPU_MOVE_MS`) so a young player can see
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
  not merely Quiet: fifteen seconds is a scan gap, forty-five is somebody who
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
small to follow. Before making the guide cleverer, ask whether a young player
who has never held a pencil to joined writing would read it.

- **The controls are in side columns and must stay there.** A young player tracing the
  top of a letter runs a finger off the top edge, and buttons above or below the
  canvas sit in the natural overshoot of the gesture the game teaches.
- **A `Set` carries its own dot spacing.** A single letter fills the canvas and
  wants 20px; a three-letter word is a third of the height and gets two dots
  per letter at that number. Zero means the default.
- **A `Set` may carry `names`** when one character cannot say what is being
  traced -- that is how the word sets caption themselves, since `Glyph::label`
  is a single char.
- **A `Set` may open at a random entry** (`randomStart`). Right for words and
  wrong for an alphabet: ABC is the order a young player is learning, while always
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
  to turn, which young players in testing could not tell apart from the dots.
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
  perfectly good 9 until a young player copies it, and nothing else will tell you.
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

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
