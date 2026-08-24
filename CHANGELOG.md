# Changelog

## 5.0.0 — Unreleased

An admin profile behind a four-digit PIN, so device settings and one profile
are out of a child's reach.

The major bump is deliberate: this is the first release where an existing
device changes behaviour under its owner rather than only gaining features.
A console that upgrades to this gains an Admin profile it did not have, stops
booting into whatever profile was last used if that profile is the admin, and
starts refusing settings changes to everyone else in the house.

Flash 2,330,649 / 3,145,728 (74.1%), RAM 72,044 / 327,680 (22.0%).

### Added

- **An admin profile, guarded by a four-digit PIN.** One profile is the admin
  (`Board::adminProfileIndex()`, with the PIN beside it in NVS). A fresh device
  gets the profile **Admin** and the PIN **0000** created on first boot — there
  is no setup wizard to sit through, and the PIN is changeable afterwards. The
  admin row carries a padlock in the profile picker, so the lock is visible
  before the tap rather than after it.
- **The PIN is asked for on the two routes into the admin profile**: switching
  to it, and opening its Edit menu (which reaches rename and that profile's
  per-child game list). It is asked **every time**, including straight after a
  correct entry — "already admin" says nothing about who is holding the device
  now, which is exactly the case this is for.
- **Settings is readable by everyone, writable only by the admin.** No PIN to
  open it: there is nothing secret on those pages, and a child who cannot see
  why the screen dims is worse off than one who can read it. Every control
  greys out for a non-admin and, more to the point, is refused.
- **Settings → Admin → Change admin PIN.** The new PIN is entered twice and is
  written only if both entries match. A mistyped PIN stored anyway locks the
  owner out of their own device, and there is no recovery short of a factory
  reset. Settings grew a third tab for it, and its tab strip now divides the
  live width instead of assuming 160px halves.

### Fixed

- **Every greyed-out Settings control was still live.** Both tabs have drawn
  their controls greyed for a non-admin for some time, but nothing checked who
  was tapping: a child could change the theme, the layout, the brightness, the
  radios and the idle policy, and could trigger a factory reset. Settings now
  refuses the change as well as drawing it grey.
- **The device no longer boots into the admin profile.** First boot used to
  leave admin selected, and the picker's *Done* button goes home with whatever
  profile is already active — so the PIN could be skipped entirely by pressing
  Done. Boot now drops to Guest if it finds admin active.
- **Both PIN pads were drawn off the bottom of the screen.** Rows were
  hard-coded at y=220 and the DEL/OK buttons at y=270 on a 240px-tall panel:
  the bottom row and both actions were outside the display, so there was
  nothing to press and no way to submit a PIN. Both pads are now laid out
  against the live panel size, as a standard 3x4 pad with DEL / 0 / OK on the
  last row.
- **A PIN of `0000` could not be entered.** Digit count was inferred from the
  numeric value, and `0000` is zero — indistinguishable from an empty field. It
  is tracked separately now, and an entry is only judged once it is exactly
  four digits, so a short prefix cannot be tested against the stored PIN.
- **The Settings PIN pad printed the PIN in plain text** in a box above the
  keys, and labelled its keys `0`–`11`. It shows masked dots and the digits
  `0`–`9`.
- **The PIN screen had no way out.** Tapping the admin row left a child stuck
  on it; there is a Back button now.

## 4.2.0 — 2026-08-17

Battery status the user can act on: the console now works out whether it is
charging, and asks to be plugged in before it dies.

Flash 2,325,045 / 3,145,728 (73.9%), RAM 72,020 / 327,680 (22.0%).

### Added

- **Charging is detected, where it used to be reported as unknown.** This board
  brings no charge-status line out to a GPIO, so `ChargingState` had exactly one
  value and the System Info row said "Unknown" forever. It is now inferred from
  the cell voltage on GPIO34, which is the only thing the firmware can see:
  a step between consecutive samples catches the cable going in or out within
  about two seconds, a voltage held above 4.24V is a charger holding it there
  because no resting cell reaches that, and a 45-second trend covers everything
  in between. A window that comes out flat keeps the previous verdict rather
  than flapping — mid-discharge a LiPo plateau spans about 20mV across 40% of
  the capacity, so "no movement" is not evidence of anything. `FULL` is only
  ever reached from `CHARGING`, because a rested full pack and a finished charge
  look identical from one sample.
- **A charge-me warning at 15%.** `Board::isBatteryLow()` and
  `isBatteryCritical()` (15% and 5%) drive a strip across the top of whatever
  screen is open: *Battery low — time to charge*, escalating to *Battery empty —
  plug in the charger*. It shows for six seconds and repeats every two minutes,
  which is often enough to be a warning and rare enough not to become furniture
  a child learns to ignore. Crossing either threshold restarts the cycle, so low
  becoming critical says so at once. Both predicates are false while charging,
  so plugging in clears the warning immediately rather than waiting for the
  percentage to climb back over the line.
- System Info's Power section now reports the charge verdict and a plain-English
  charge level beside the voltage.

### Changed

- **The battery icon says something at a glance.** The lowest colour band began
  at 30%, so it said nothing new at the level the user is actually asked to act
  on; the bands are now red at or below 15%, amber to 40%, green above. At or
  below 15% the *outline* goes red as well as the fill, because a nearly empty
  battery and a nearly full one differ by 11 pixels of fill and that is not
  something anyone reads across a room. The emphasis drops the moment the
  charger is attached — by then the user has done the thing it was asking for.
- The bolt now means "the charger is attached", derived from the charge verdict
  rather than from a voltage above 4.35V — with a pack fitted that threshold was
  never crossed, so the bolt could not appear while charging a real battery. An
  empty bolt alone still means USB with no pack.
- `Ui::drawBatteryBadge()` takes a `Ui::PowerHint` rather than an
  `isExternalPower` bool. `Ui::powerHint(board)` is the single place the drawing
  layer and the HAL enum meet, which keeps `Ui.h` free of the HAL.
- The runtime's notification strip is now `drawHeaderBanner()`, and the battery
  warning outranks a Nearby event on it: one of them is a nicety, the other is
  the reason the console is about to switch off.

## 4.1.0 — 2026-08-15

A major console refresh with a new product name, redesigned launcher icons,
two new games (Dice, Coin Flip), the BLE beacon renamed to **Braino**, and an
opt-in anonymous score exchange with other consoles in the room.

Flash 2,323,937 / 3,145,728 (73.9%), RAM 71,980 / 327,680 (22.0%).

### Changed

- **The console is now Braino!** The product was GoodTime Kids; it is Braino!
  from this release. The owner did not change: every copyright line still reads
  GoodTime Micro Company, and the trademark notice records the old name so the
  two facts cannot be conflated later. The name and both copyright forms now
  live only in `include/AppVersion.h` -- they had been typed out in five places,
  which is the same shape of drift that once left About six games behind.
  `GOODTIME_KIDS_VERSION` is now `BRAINO_VERSION`.
- **The launcher icons are drawn to a design system now.** What made the old
  set look homemade was not any single icon but that they disagreed: some were
  outlines and some solids, strokes ran from one to three pixels, several sat
  timidly in the middle of a tile while others filled it, and a few spent four
  colours where one would do. Every icon is now a solid light silhouette with
  detail in one near-black and at most one accent hue, filling a 36x36 box, with
  a two-pixel minimum stroke and a single corner radius. Five games are exempt
  from the one-accent rule because colour is their subject.
- **Every launcher icon was reworked.** Three pairs of tiles were effectively
  the same picture -- Fractions and Percent were both a white circle with a
  yellow wedge, Flags and State Flags the same flag in two colours, Tic-Tac-Toe
  and Whack A Mole both a white grid -- and six more drew font 1 text that is a
  smudge at tile size. Icons now have a documented 36x36 budget, since the
  landscape tile is only 46px tall.
- **The beacon now advertises as `Braino-<id>`.** The family id is `Braino` and
  the two-letter manufacturer tag is `BR`; the device id is unchanged — the last
  two bytes of the factory Bluetooth MAC, rendered as four hex digits. The
  manufacturer-data layout version goes to `2`, so a device running the previous
  firmware is ignored rather than mis-decoded.
- The web installer offers only the console firmware. The bring-up and Wi-Fi
  diagnostics are still built by CI and still documented in the README, but they
  are no longer in the browser flasher's picker, where choosing one replaces a
  working console with a serial test.

### Added

- **Dice.** Pick one, two or three dice and throw them; the faces tumble for a
  second before they settle. No score is kept -- a best total here would be
  luck, reachable in a few taps and then frozen at 18 forever, so it would only
  add a dead row to the Scores app.
- **Coin Flip.** Best of one, three or five spins. Each coin squashes edge-on
  and back as it spins, then lands, and a pip row fills in as the round goes on.
  No score, for the same reason as Dice.
- **Nearby — an anonymous score exchange.** A new system app, **off by
  default**, that listens for other Braino devices and shows who is in range,
  which game they have open and their best score for it. When a peer's score
  beats the record on this device, the header says so.

  - Sharing is opt-in twice over: it does nothing unless the BLE beacon is on,
    and then only once *Settings → Nearby* (or the switch on the Nearby screen
    itself) is turned on. Turning the beacon off stands it down.
  - What travels is a game index and a best score, added to the existing
    beacon payload. **No child name, no profile name, no progress, nothing
    profile-scoped.** A peer is four hex digits of its own MAC and nothing
    else, which is the whole design: a leaderboard with no way to find out who
    is on it.
  - The scan is **passive** — it never transmits a scan request, so listening
    adds nothing to what the device puts on air.
  - Peers are decoded with the exact inverse of the function that builds our
    own payload, so there is one description of the wire format rather than a
    transmit copy and a receive copy that can drift.

- **Header notifications.** Nearby events (a console arriving, changing game,
  or beating a record) paint a strip over the header for five seconds and are
  then removed — the screen underneath repaints itself. They are not collected
  into a list, because a notification nobody clears is furniture.

- **`Settings → Device` gains a Nearby switch.** Network moves to a full-width
  row to make space; the switch greys out and says *needs Beacon* while the
  radio is off, rather than flipping and doing nothing.

- The GitHub Pages installer now showcases every generated still for the games,
  launcher, settings, profiles, Wi-Fi/time, scores, About, System Info and the
  screen saver instead of showing only the launcher mock-ups.

### Fixed

- **Every game was drawing from a software PRNG, not the hardware RNG.**
  `AppRuntime::begin()` called `randomSeed(esp_random())`, which reads like an
  improvement and is the opposite on this core: arduino-esp32 defaults
  `random()` to `esp_random()`, and `randomSeed()` switches it to newlib `rand()`
  for the rest of the boot. One good number bought, then 123 draws across 25
  games spent on a once-seeded LCG. The call is gone. Dice and Coin Flip
  additionally draw through `engine/Entropy.h`, straight from `esp_random()`
  with the modulo bias rejected, because for those two the draw is the product.
- **The device could stop waking from a long sleep.** `applyTimeConfig()` handed
  lwIP's SNTP client `ntpServer().c_str()` -- the buffer of a `String` temporary
  destroyed at the end of that statement. `sntp_setservername()` keeps the
  pointer rather than copying the string, so the daemon was left holding freed
  heap and dereferenced it on its own poll cadence. That block survives while
  nothing reuses it, which is why the fault needed hours of idling to appear.
  The name now lives in a `Board` member. The same call was also being re-issued
  every five minutes forever, stopping and restarting the daemon each time for
  no benefit; it is idempotent now.
- The ILI9341 ignores Sleep Out within 120ms of a Sleep In. `displayWake()` now
  waits out the remainder, which a tap arriving just after the saver handed over
  to sleep could otherwise land inside.
- The Wi-Fi tile drew outside its own tile. It painted the bottom halves of two
  full circles out with a rectangle in the tile colour, and the mask was three
  pixels short, so the base of the outer ring survived at `cy + 24` -- past the
  bottom edge of a 46px landscape tile. It is drawn as real arcs now.
- The Coin Flip tile had the same overflow, from its edge-on sliver.
- Several icons set the text datum to `MC_DATUM` and returned without restoring
  it, re-aligning whatever the launcher drew next.
- **Profiles and System Info came back from the screen saver in the wrong
  orientation.** Both opt into `followsLayout`, and both were laid out correctly
  when opened -- but `wakeFromSleep()` and `exitScreenSaver()` tested
  `activeApp_ != nullptr`, which is true for *every* launched app, and forced
  landscape. Boot into Profiles in Vertical layout looked right; idle until the
  saver came on, touch to dismiss, and it came back landscape and stayed there.
  All three call sites now share one `rotationForActiveScreen()`; the rule was
  written out three times and one copy was wrong.
- **The launcher never drew the copyright in Vertical layout.** The landscape
  branch drew it and the portrait branch did not, so owners running the device
  in portrait never saw it on the home screen. It now shares the top row with
  the title, right-aligned, which costs portrait its centred title and makes the
  launcher and Profiles headers agree.
- **Profiles.** "Who is playing?" and the guest hint overlapped each other in
  both orientations, and in portrait the hint ran straight through the first
  profile row. In portrait the copyright line also collided with the title,
  which is too wide at 240px to share that line. "Braino!" is half the width, so
  both orientations went back to sharing one 30px header bar.

### Privacy surfaces updated in the same change

- *System Info → BLE* decodes the two new Nearby fields and reports **Open game** and
  **Best score** as `Broadcast` / `Not Broadcast` from `sharesActivity` — read
  off the same struct the controller was handed, not from UI copy.
- The **About** radio page and the README/installer privacy sections say what
  Nearby adds, and say it only while it is actually on.
- `docs/BLE_BEACON_SPEC.md` records the v2 payload and the anonymity argument.

### Other updates

- The installer page and README now call out the exact E32R28T-1 / ESP32-32E
  Amazon board needed for the ready-made flash image.

## 4.0.0 — 2026-08-15

Browser flashing, screen sleep, GRE Words, Percent Circle, Scores device bests,
app/runtime refactors, storage telemetry, safer profile deletion, and a pass
over touch/redraw bugs found on the physical board.

Flash 2,307,681 / 3,145,728 (73.4%), RAM 68,036 / 327,680 (20.8%).

### Added

- **Device-wide best scores on the Scores screen.** A new **Device** tab shows
  the best score across every child on the device for each game, and who holds
  it. The holder's name appears in gold when it is the current player. Scores
  are computed once at screen entry and cached, so scrolling and paging add no
  NVS reads. Ties go to the lowest profile index.

- **A GitHub Pages site with a browser-based flasher.**
  <https://iamankushpandit.github.io/Gume/> describes the firmware and installs
  it over Web Serial: pick the board and the firmware, plug in the USB cable,
  press the button. No PlatformIO, no toolchain, nothing to download by hand.
  Desktop Chrome, Edge and Opera only -- Firefox and Safari do not implement
  Web Serial, so the page falls back to `esptool` instructions and direct
  `.bin` links.

  The firmware picker offers all three environments: the console itself, the
  bring-up diagnostic for a board that shows nothing, and the isolated Wi-Fi
  radio test. Each one gets its own esp-web-tools manifest.

- **`tools/gen_site.py`.** The page is generated, not written: version from
  `AppVersion.h`, the game list and blurbs from `AppRegistry`, the build
  figures from `README.md`, the board name from `platformio.ini`. Same reason
  About derives its list -- a hand-written copy of the game list has fallen six
  games behind before. `site/index.template.html` holds wording and layout
  only.

- **Percent Circle.** Three modes: read the circle, make the circle, percent of
  a number. Teaches percentages as a portion of a whole, with circle-based
  visualization and adaptive difficulty.

- **GRE Words.** A vocabulary trainer with 250 GRE-level words in two modes:
  **Study** flips a card from the word to its meaning and an example sentence,
  and **Quiz** asks for the right gloss out of four. Both feed the same
  spaced-repetition data (`engine/Progress`), so a missed word comes back soon
  and a known one fades out. The odd screen out in this catalog -- it is aimed
  at whoever is sitting the test, not at a small child.

- **Screen sleep.** The device now blanks the panel when it has been left
  alone, which is what actually conserves the battery: the screen saver was
  keeping the backlight lit indefinitely. **Settings -> Power** carries three
  policies -- saver then sleep, sleep only, or saver only -- with both the idle
  delay and the sleep delay configurable. The main loop drops from 50Hz to 10Hz
  while asleep and any touch wakes it, returning to whatever screen was open.

  This is panel sleep, not `esp_deep_sleep`: the CPU stays up to poll the touch
  controller, because no wake source is wired for a true deep sleep on this
  board.

- **Settings has tabs.** Device and Power. The grid was already seven buttons
  and a slider, with nowhere to put the three idle controls.

- **`.github/workflows/pages.yml`.** Builds `app`, `bringup` and `wifidiag` on
  every push to `main`, generates the site around them, and copies the four
  flash images beside each manifest. It rewrites the machine-local `map-n-flag`
  path in its own working copy, because CI is just another checkout. A final
  step refuses to publish a manifest whose binaries are missing -- that failure
  would otherwise land in a user's browser, halfway through writing their
  board.

- **`check_docs.py` now checks the site.** It fails if a version number is
  typed into the template, if the template loses a placeholder the generator
  fills, or if the page offers a firmware that platformio.ini does not define
  or the workflow does not build.

- **NVS storage telemetry.** System Info now reports NVS entry use, free
  entries and namespace counts, with soft warning/critical thresholds so future
  apps cannot quietly exhaust the shared partition.

- **`CONTRIBUTING.md`.** Contributor workflow is now written down for humans
  and agents: branch/worktree use, board locking, adding games/apps, fixing
  bugs, checks, docs and flashing.

### Changed

- **Trace game completely redesigned.** The old version let a single tap
  anywhere complete an entire stroke; tap a few times and the letter was done,
  with no requirement to follow a path. The new version enforces ordered
  dot-to-dot tracing: strokes must be followed in order from a numbered start
  dot, with each dot claimed only by dragging through them in sequence. Added
  26 lowercase letters (a–z) alongside the existing uppercase and digits,
  with a three-way mode selector (ABC / abc / 123) at the top. Waypoints are
  now calculated at glyph load and displayed as pulsing dots that guide the
  child through the correct path, teaching both letter formation and stroke
  order before a pencil is involved. The final device-tested pass improved
  lowercase `f` and `g`, added Again/Next controls, and moved completion
  feedback out of the drawing field so the child can see the finished
  character.

- **Apps and games now launch through one registry.** Playable games declare
  their id, title, subtitle, blurb, score info, icon, launcher index and
  default visibility in one metadata block, while `AppRegistry` binds that
  metadata to the concrete instance.

- **Rendering now goes through `Ui::Renderer`.** Normal games draw against a
  driver-free RGB565 interface instead of directly taking `TFT_eSPI`, while the
  firmware runtime adapts it back to the panel.

- **`main.cpp`, runtime and `Board` were split by concern.** The app runtime,
  launcher, screen saver/sleep path, HAL access facades, display, touch,
  storage, power, network and feedback implementations now live in smaller
  units.

- **Shape Arith subtraction is clearer.** Subtraction now shows a `left` box and
  a `take away` box, keeps the removed group visible, and asks the child to
  count what remains instead of showing an unused mystery box.

### Fixed

- **Profile deletion now moves persisted data, not only names.** Removing a
  child clears that slot's `pN_` keys, shifts later profile data down with the
  child, clears the old last slot and keeps the active child pointing at the
  same real profile where possible.

- **Percent, Maze, Trace, Shape & Color, Cinnamon and Profiles redraw bugs.**
  Percent clears the previous question before the next round, Maze now redraws
  only moved cells instead of flashing the whole screen, filled stars match
  their outline geometry, Cinnamon shows feedback for the final correct input,
  and the wide Profile picker no longer squeezes the helper text into the
  player rows.


## 3.0.0 — 2026-08-13

BLE beacon with full on-device transparency, a System Info app, an About
app that reports what the radios are actually doing, and a pass over
everything that was making the UI feel slow.

Flash 2,254,141 / 3,145,728 (71.7%), RAM 64,940 / 327,680 (19.8%).

### Added

- **BLE beacon.** An opt-in, non-connectable presence broadcast, off by
  default and switched from *Settings -> Beacon*. It advertises a device name
  (`LearnKey-<id>`) and a manufacturer-data block holding a family tag, a
  layout version and two bytes of the factory Bluetooth MAC. 27 of the 31
  legal payload bytes. Nothing profile-scoped is reachable from the radio path.

  NimBLE rather than the core's Bluedroid stack: advertise-only needs a
  fraction of the host, and flash is the scarce resource here. Host plus
  controller cost ~192 KB.

- **System Info BLE tab.** Shows whether advertising is currently active, the
  advertised name, every decoded field of the manufacturer data, a privacy
  list of what is *not* broadcast, and under *Show advanced* the interval, TX
  power, advertising type, controller address and a hex dump of the exact
  bytes on air.

  Everything on that screen is read back from the same structure the radio was
  configured from -- `BleBeacon::Advertisement`, compiled into a raw buffer and
  handed to the controller verbatim. There is no second, hand-written UI
  description of the payload, so the display and the radio cannot drift apart.
  Contract written down in `docs/BLE_BEACON_SPEC.md`.

- **Beacon badge in the launcher header.** The Bluetooth rune, drawn only
  while the radio is genuinely advertising -- there is no greyed-out variant,
  because "is it transmitting?" should not be a question of shade. Placed
  per layout mode: landscape puts it on the clock's line, positioned off the
  measured width of the clock string, because the badge row there has about
  8px of slack; portrait simply extends the badge row.

- **System Info screen** (five tabs: board, memory, network, BLE, app state)
  with live telemetry, scrolling rows and a scroll bar.

- **About gained a "What the radios do" page**, and every fact on it is read
  from the running system rather than typed in: Wi-Fi status from
  `Board::hasWifiCredentials()` / `isWifiConnected()`, beacon status and the
  advertised name from `BleBeacon::active()` / `configured()`. A privacy
  claim that has drifted from the hardware is worse than none at all, because
  it is believed -- so About now cannot drift. It also points at
  *System Info > BLE* for the exact bytes on air.

  About is also now orientation-aware, as the system-app rule already
  required: it laid out against `SCREEN_WIDTH` and would have overflowed a
  240px portrait screen.

### Performance

The board felt sluggish, and it was not one thing. Each of these looked
harmless at the call site and each was eating most of a 20ms frame.

- **NVS reads in hot paths.** `Preferences` is flash-backed -- every getter is
  a hash lookup, not a variable read. `screenSaverSeconds()` ran once per loop
  iteration, about 50 times a second. Worse, `gameVisible()` ran up to ~180
  times per launcher repaint and each call was two NVS reads plus three
  `String` temporaries, so a single launcher repaint could mean ~360 flash
  lookups and ~550 allocations.

  Theme, layout, brightness, idle timeout and active profile now have
  write-through RAM mirrors, and game visibility is a 32-bit mask loaded once
  per profile. `gameVisible()` went from two NVS reads and three allocations
  to a shift and a mask.

- **A blocking `delay()` reachable from a render path.** Battery sensing slept
  10ms per call and `Ui::drawTopBar()` calls two battery getters, so every top
  bar cost ~20ms of pure `delay()` before anything was drawn. Now one cached
  sample set with a 2s lifetime and no delays at all.

- **Frame pacing was a fixed nap, not a deadline.** The loop ended in an
  unconditional `delay(18)`, so the frame period was work + 18ms and a heavy
  frame was punished twice. It now sleeps only the remainder of a 20ms budget,
  so touch latency tracks how long the work actually took.

- **Content rebuilt every frame.** System Info reassembled every row on every
  frame while scrolling. Scrolling changes an offset, not content; rebuilds
  are now gated behind a stale flag.

### Fixed

- **"Adding a game" became "Adding a game or an app", and grew from five items
  to fourteen.** The old list was five code edits, and everything that has
  since been shipped broken or stale was outside those five: the README game
  table, the screenshot gallery, `docs/screens/`, the changelog. Four games
  reached a release listed in the catalog and named nowhere in the README.

  The list now covers code, docs, screens and verification, states which
  surfaces derive from `GAME_CATALOG` and must *not* be hand-edited, and adds
  the extra obligations for a system app (both orientations; the About radios
  page if it touches a radio). `check_docs.py` enforces the two items a machine
  can see: every catalogued game is named in the README, and every catalogued
  game has a screenshot.

- **Regenerated the README mock-ups, which had drifted badly.**
  `docs/screens/` still held `countries-outline.png` and
  `countries-continent.png` for a game deleted two releases ago; the launcher
  images showed a profile chip that no longer exists, no battery or beacon
  badge, and a Countries tile; and the Settings picture showed a three-row
  grid that had become four rows with a beacon toggle.

  Added screens for the four games that had none -- US States, State Flags,
  State Maps and Trace -- plus System Info's BLE and memory tabs and the About
  radios page. State flags and outlines render from the real `map-n-flag`
  pixel data, so those are the actual pixels the device draws.

  `check_docs.py` now fails if a generated screen is missing, if a PNG has no
  generator entry (i.e. it outlived its screen), if README references an image
  that does not exist, or if a catalogued game has no screenshot at all.

- **Corrected the artwork licensing, which had been wrong since the Countries
  game was removed.** README carried a warning that the country outlines came
  from `djaiss/mapsicon` -- whose terms are *"don't resell them - I forbid
  it!"* -- and told readers that anyone wanting to monetise the project must
  first swap them for Natural Earth. About credited mapsicon too.

  That artwork went with the Countries game. The binary contains no mapsicon
  data and no reference to it: `mnf_map()` is never called, the library ships
  no country-outline arrays, and `nm` on the firmware finds zero matching
  symbols. The US state outlines that replaced them are Natural Earth, public
  domain. **Every asset now compiled in is MIT or public domain.**

  A credit that outlives its asset is not harmless -- this one misrepresented
  what the owner was allowed to do with their own project. `check_docs.py`
  now fails if mapsicon is credited while nothing calls `mnf_map()`.

- **The boot counter was stuck.** `"boots"` was persisted only inside the
  unclean-reset branch, so a cold boot -- which is every power cycle, since
  RTC memory does not survive one -- read back whatever the last crash had
  left and reported the same number forever. The device sat on "boot #4"
  across many power cycles, quietly undermining the crash report it appears
  next to. Now written on every boot.

- **The System Info row list no longer churns the heap.** It held 48 rows of
  two Arduino `String`s each and rebuilt every one on every frame -- roughly
  96 long-lived allocations freed and re-made at frame rate, interleaved with
  every transient `String` the row builders create. On a heap that cannot be
  compacted that is a fragmentation engine: free heap looks healthy right up
  to the allocation that fails.

  Extracted to `src/ui/RowList` with fixed `char` buffers, so it now allocates
  nothing at all, and rebuilds are gated on a stale flag rather than running
  per frame. Costs 864 bytes of static RAM and *saves* 5.5 KB of flash.

- **Heap accounting per screen.** Every transition compares free heap against
  the value captured before that screen's `begin()` and logs
  `[heap] '<screen>' left N bytes short` when a screen does not hand it back.
  Fragmentation is logged past 60% and shown on the System Info memory tab,
  since free heap alone will not reveal it.

- **Battery sensing made ~20-30ms cheaper per frame, and more honest.**
  `getBatteryPercent()` and `getPowerSource()` each ran their own 10-sample
  ADC conversion with a `delay(1)` between samples, and `Ui::drawTopBar()`
  calls both -- so every top bar cost ~20ms of blocking delay and the System
  Info board tab ~30ms per repaint. That is most of a frame, and it is what
  made scrolling that screen feel sluggish. One cached sample set, 2s
  lifetime, no delays, shared by every accessor.

  Three wrong assumptions went with it: the conversion claimed "3.3V reference
  with 11dB attenuation" (mutually exclusive -- at 11dB the ADC is linear only
  to ~2.45V and its reference varies 1000-1200mV per chip, so it now goes
  through `esp_adc_cal` and the eFuse calibration); percentage was linear from
  3.2-4.2V, which reads ~20 points high through a LiPo's flat middle, and is
  now a piecewise curve; and `isBatteryPresent()` returned false whenever the
  device was on external power, which reads as "no battery fitted".

  The 100k/100k divider ratio remains assumed and still wants a meter.

- **System Info no longer smears text into its tab strip while scrolling.**
  Rows entirely outside the content rect were skipped, but the row straddling
  the top edge was still drawn in full. The row loop now clips to a viewport.

### Changed

- **Profiles screen now carries the GoodTime Kids product mark.** A branded
  header at the top shows "GoodTime Kids!" and "(C) GoodTime Micro" in the same
  visual language as the launcher and screen saver. The Pick phase geometry is
  recalculated to accommodate the header without collision.

- **Screens now have an `end()` lifecycle hook**, called on every transition
  through a single `leaveActiveGame()` funnel so no path can skip it. Nothing
  in the firmware runs off a task or timer, so no screen keeps consuming
  cycles after you leave it -- the hook is what keeps that true as screens
  grow. System Info uses it to release its row list.

- **The launcher header shows the profile name as plain text, not a button.**
  The framed chip was what overlapped the clock and status badges on a 240px
  portrait header; the name itself was wanted. In landscape it now sits after
  the byline rather than across it. The rect is both where it draws and the
  touch target, so the two cannot drift apart.

- Settings moved to a four-row grid to fit the beacon toggle, with Reset
  spanning both columns.

- `README.md`, `CLAUDE.md`, `AGENTS.md` and the per-directory `CLAUDE.md`
  files brought back in sync: the game count (23 -> 26), the removed Countries
  game, the added US States / State Flags / State Maps / Trace games,
  profiles, the watchdog, and current flash and RAM figures.

## 2.0.1 — 2026-08-11

Display and theming fixes, all reported from the device.

### Added

- **Screen brightness.** The backlight moves from a plain `digitalWrite` to
  LEDC PWM, with the level persisted and a web-style slider in Settings
  (rounded track, filled portion, round handle).

  The floor is **25%, not 0** — deliberately. At very low duty the panel is
  unreadable, and a child who dragged it to the bottom would have no way to
  *see* the control needed to undo it. The slider's full travel maps to
  25–100, so the unusable range cannot be reached at all.

- **Screen flip.** A Settings toggle turns the display 180° for whichever
  layout is active — landscape 1↔3, portrait 0↔2, i.e. base XOR 2. All five
  rotation call sites now go through a single `effectiveRotation()` helper, so
  they cannot drift apart. Rotation and layout are logged at boot *after*
  `goHome()`, which is what actually applies them.

### Fixed

- **Light theme was partly invisible.** The launcher header is
  `Ui::surface()` — near-white on the light theme — but the gear icon defaulted
  to **white**, so it could not be seen at all. Status colours were also
  theme-*independent* brights: pale yellow on white barely registered and the
  green washed out. Each now has a darker light-theme counterpart, badge glyphs
  flip to white over those darker fills, and unlit Wi-Fi bars lighten so they
  still read. The top bar stays dark in both themes, so its gear is explicitly
  white.

- **Wi-Fi badge overlapped the gear** in the wide launcher header by 2px. The
  header is rebuilt as two rows: branding on the left across two lines, the
  status block on the right behind a hairline divider. A font-4 title is about
  190px wide, so it was never going to share one line with a clock, two badges
  and a gear without crowding.

- **Counting overlapped itself.** The font-4 question spans roughly 60–260px
  when centred, while the right-aligned score started around 213px — both drawn
  at the same y. Score and streak now share one small line beneath the question.

### Changed

- **Settings tabs look like tabs.** `Ui::drawTab` draws browser-style tabs: the
  active one is page-coloured with rounded top corners only and breaks the
  baseline beneath itself so it merges into the content area. Inactive tabs sit
  lower and darker. They previously rendered as ordinary buttons.

- Settings rows tightened from 40px to 36 to make room for the slider; verified
  clear down to the slider ending at y=232 of 240.

- README now pictures **every one of the 23 games** — 33 generated screens in
  total. `tools/gen_screens.py` takes each game's geometry from its own `Rect`
  helpers in `src/games/`.

## 2.0.0 — 2026-08-11

The geography release. Three new games, real country artwork, spaced
repetition, and a working clock.

Flash 2,302,437 / 3,145,728 bytes (73.2%) · RAM 50,312 / 327,680 (15.4%)

### New games

- **Flags** — a real flag, name the country; correct answers unlock a
  **capital-city bonus** round. 195 flags.
- **Countries** — a real country outline, alternating *"which country?"* and
  *"which continent is it in?"*. 191 outlines.
- **Number Line** — a marker hops along a number line to reach the answer.

All three existed as source files but were **never registered** in `main.cpp` —
no include, no enum entry, no member — so they were unreachable. The launcher
went from 20 to 23 games.

Artwork comes from a new companion library,
[**map-n-flag**](https://github.com/iamankushpandit/map-n-flag): 4-bit indexed
images totalling 1.11 MiB, costing **zero RAM** because they stream from flash.

### Learning behaviour

- **Spaced repetition.** Questions were drawn uniformly at random, so a child
  saw Brazil as often as Bhutan and got no extra practice on misses. Flags and
  Countries now keep a mastery score per country; a miss costs **twice** what a
  correct answer earns, and selection weights a recently-missed country **8×**
  against **1×** for a mastered one. The two games track separately.
- **Adaptive difficulty.** Easy (30 countries) / Medium (62) / Hard (195), with
  six correct in a row promoting a tier automatically.
- **Finger Counting rebuilt.** It previously showed "3 + 4 = ?" and expected the
  child to tap 7 fingers — which requires already knowing the answer, so it
  tested arithmetic rather than teaching counting. It now alternates *"How many
  fingers?"* and *"Show me 7 fingers"*, drawn as real hands.
- **Subtraction animation loops** in Shape Arith, so a child who looks away can
  re-watch it.

### Accessibility

- **Cinnamon no longer flashes the screen.** It repainted the entire display on
  every step, producing a full-screen flash about once a second —
  uncomfortable generally and a genuine risk for photosensitive players. It now
  repaints only the pads that changed, so there is **no full-area luminance
  change at all**, and mirrors each colour on the case LED.

### Wi-Fi and clock

Every one of these was found by instrumenting the device over serial.

- **Scanning worked, then silently returned nothing.** The async
  `scanNetworks()` / `scanComplete()` pair was failing on this board. An
  isolated radio test (`pio run -e wifidiag`) found **58 access points** with a
  blocking scan, proving the hardware was fine. The app now uses the blocking
  form, dedupes by SSID keeping the strongest signal, and sorts strongest first.
- **Credentials were never saved.** The SSID was re-derived from
  `WiFi.SSID(index)` at JOIN time; once the driver's scan table was freed that
  returned an empty string, so an empty SSID was stored while the connection
  still succeeded from the ESP32's own copy. The name is now captured when the
  row is tapped.
- **NTP never answered.** lwIP's SNTP stays silent on some networks, so the
  clock is now set by a direct UDP query, with SNTP kept configured across three
  servers as a backup.
- **The clock was pinned to UTC.** `configTime(0, 0, ...)` hardcoded a zero
  offset. A single `applyTimeConfig()` now programs it, using **POSIX TZ rules**
  (`CST6CDT,M3.2.0,M11.1.0`) so daylight saving is handled automatically.
- **"Sync now" was blocked by the Auto-time switch** — an explicit request now
  ignores it.
- Time zone is auto-detected from the public IP, overridable from a paged
  picker of named zones.

### Interface

- **Wi-Fi and NTP consolidated** into one **Network & Time** screen.
- **Clock-sync and Wi-Fi badges** in the top bar and both launcher headers; the
  Wi-Fi one shows real signal strength (4/3/2/1 bars from RSSI).
- **Date shown** alongside the time.
- **Portrait launcher**: 2×2 tiles, four per page, USB edge at the bottom.
  Landscape flipped to match.
- **3D bevel** added centrally in `Ui::drawButton`, so every button in every
  game gained it from one change.
- **Screen saver** returns to whatever you were doing rather than the home
  screen, and swallows the wake-up touch — which previously landed as a phantom
  press and launched a random game. Paddles sweep opposite ways, and each rally
  speeds the ball up and changes colour, mirrored on the case LED.
- **RGB LED** driven by PWM and hooked into `beepOk`/`beepError`, so all 23
  games signal right and wrong without any per-game change.
- **About** is generated from the game catalogue; it had fallen six games behind.

### Fixes

- **The build was broken.** `ObjectAddGame.cpp` referenced a `Phase` enumerator
  that did not exist in its header.
- **Finger Counting could ask for 11 fingers** — `1 + random(6)` allowed totals
  above ten.
- **Blank launcher.** Switching Wide → Tall shrinks the page size, which could
  leave the page index past the end.
- Overlapping rectangles in Settings, the Wi-Fi list and Finger Counting.
- Duplicate answer options were possible in Flags and Countries.
- Continent names truncated to "North Americ." in the answer buttons.

### Internal

- **`engine/GameCatalog`** is the single source of truth for the game list. Ids,
  titles and labels previously lived in **two arrays coupled by index**, with
  nothing enforcing agreement.
- **`engine/Progress`** — reusable per-item mastery tracking.
- **`Game::markFullDirty()` / `needsFullRender()`** — the groundwork for partial
  redraw. Cinnamon is converted; the other games still clear the whole screen.
- **`pio run -e wifidiag`** — an isolated radio test that builds with no
  display, touch or game code.

### Known limitations

- 22 of 23 games still repaint the whole screen on every change, which is
  visible as flicker. Only Cinnamon has been converted.
- The country outlines (mapsicon) are **not licensed for resale**. Anything
  commercial needs them swapped for Natural Earth first.
- Tall layout applies to the launcher only; every game is authored against the
  fixed 320×240 landscape canvas.

---

## 1.0.1

Launcher company line layout fix.

## 1.0.0

Initial firmware: 20 games, theme and layout settings, touch calibration,
optional SD-card content, Pong screen saver.
