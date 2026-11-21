# Ideas for Braino!

**A backlog, not a plan.** Every idea and future direction discussed for
Braino is written down here so that none of them lives only in somebody's
memory or an old conversation. Nothing in this file is agreed, scheduled or
promised unless the entry says so in as many words. Being listed is not
approval: several entries exist precisely so that they are argued before
anybody writes code.

Each entry says **what** it is, **why** it might be worth doing, its
**status** -- *idea*, *partly done*, or *blocked on the owner* -- and, where it
matters, what it costs against the two budgets that bind on a board: **flash**
(2,706,157 of 3,145,728 bytes of the app partition, 86.0%, on the 4 MB boards,
leaving about 429 KB) and the **20 ms frame budget**.

Three things are deliberately not repeated here: `docs/FRAMEWORK_PLAN.md` (the
framework design, summarised in section 3), `docs/RENDER_AUDIT.md` (an
in-progress checklist) and `CHANGELOG.md` (what has shipped).

**Before starting anything below, read the rule it has to honour.** Most of
these are cheap to write and expensive to write *correctly*, and the expensive
part is nearly always a standing rule in `CLAUDE.md`: the closed list of
outbound flows, no heap churn, no `delay()` on a render path, no fact restated
that the firmware already knows, and the board described in exactly two files.

Contents:

1. [Product direction](#1-product-direction)
2. [Platforms and releases](#2-platforms-and-releases)
3. [Framework and architecture](#3-framework-and-architecture)
4. [Content and curriculum](#4-content-and-curriculum)
5. [Multiplayer](#5-multiplayer)
6. [Site and community](#6-site-and-community)
7. [Hardware and cases](#7-hardware-and-cases)
8. [Accessibility](#8-accessibility)
9. [Documentation and tooling](#9-documentation-and-tooling)
10. [Done since the September backlog](#10-done-since-the-september-backlog)
11. [Dropped, and deliberately not on this list](#11-dropped-and-deliberately-not-on-this-list)

---

## 1. Product direction

### 1.1 Braino as a platform, with the games decoupled from it

**What.** The owner's long-range direction: Braino "becoming an OS where we
can decouple the games". A platform repository (the HAL, the runtime, the
shell, the system screens) and a Braino repository (the games) built on top of
it; a command-line tool first; then an SDK with example apps so someone else
can write a game without forking the console; and eventually games shipped as
libraries on the SD card, with educational packs possibly sold separately from
a free core.

**Why.** It is the one change that answers several entries below at once: the
flash ceiling on 4 MB boards (1.6), per-platform catalogues (2.8), content
written by people who do not build firmware (4.7), and a product that is
more than one repository's worth of games.

**Status: idea.** `docs/FRAMEWORK_PLAN.md` holds the design and its first
line still says nothing in it is implemented. Pieces that exist already and
point the same way: `AppGame`/`AppContext` (the surface an ordinary game sees),
`Ui::Renderer` (driver-free drawing), `PanelSize` and the per-platform
interfaces (`PowerProbe`, `SystemSuspend`, `SleepPower`), and the PC build
proving that `src/` runs unchanged off the ESP32.

**Cost.** None in flash until code moves; a large diff when it does, across
every spine file. Sequence it after a release, never alongside one.

### 1.2 Configure a whole class at once, and flash in batches

**What.** A teacher or a reseller sets up many consoles identically: which
games each player sees, the settings, the players themselves -- "flash this
and sell in batch".

**Status: partly done.** The serial console's `game all <id> on|off` shows or
hides a game for every player, and `tools/configure_boards.py` applies one
gitignored config to every attached board over the console.
`tools/ESP32_boardUtil.py --flash` flashes every attached board in parallel.
What is missing: a saved "class profile" that can be applied without the
toolchain (see 6.3, the browser batch flasher), and a way to do the same on
the Linux handhelds, which have no serial console a teacher would reach.

### 1.3 Appliance-only: Braino takes over the whole console

**What.** Write the rule down in the docs: Braino ships only on devices it
takes over entirely -- no Android or iOS app, no port that runs inside
another operating system. A desktop build for players is out for the same
reason (the PC build stays a developer tool).

**Why.** The privacy promise is a promise about the whole device. Inside
another OS, the closed list of outbound flows describes Braino's own packets
and nothing else the device does.

**Status: decided by the owner, not yet written into `README.md`,
`docs/PORTING.md` or the site.** A docs change.

### 1.4 Native resolution is the rule for every platform

**What.** State in `docs/PORTING.md` that every platform draws at its own
resolution, found at run time through `PanelSize`, and that nothing draws a
320x240 picture for somebody else to stretch.

**Status: partly done.** The host and the RG40XX V already work this way
(`host/src/HostPanelSize.cpp`, FreeSans text, smooth shapes), and the root
`CLAUDE.md` describes it. `docs/PORTING.md` does not yet say it is the rule
for a new platform.

### 1.5 Game sets: a core set and a full set

**What.** A core catalogue for the 4 MB boards and the full one elsewhere
(16 MB S3 boards, the Linux handhelds). On Linux, where the catalogue can
grow, a way to find a game: categories, recently played, search.

**Status: idea.** It depends on stable game ids in the advertisement (5.3),
or two consoles with different catalogues would name different games with the
same byte. Per-player visibility is already keyed by game id
(`src/hal/GameVisibility.h`), so that half is ready. The owner asked that the
issue be filed only once the experimental image is on the main site.

### 1.6 The flash budget: measure before cutting

**What.** Flash on the 4 MB boards is 86.0% full and rose about ten points in
two months. Before any of the larger answers (game sets, a selective-build
flasher, moving artwork to the card), add **per-app flash accounting**: a tool
that reports what each game, data table and piece of artwork contributes.

**Why.** Without it, "which games would a 4 MB board drop" cannot be answered,
and the cheaper wins -- an oversized table, artwork that could be drawn, the
~192 KB of NimBLE plus controller that two features use -- cannot be priced.

**Status: idea.** Tools only, no flash. `tools/elf_size.py` already reads
section sums and is the natural starting point.

### 1.7 A flasher that builds an image from the games you picked

**What.** The installer asks which games you want and offers an image with
only those.

**Why it is listed.** It is open decision 4 in `docs/FRAMEWORK_PLAN.md` (ship
everything and enable, pre-built packs, or on-demand builds), arriving because
flash binds.

**The honest reading.** On-demand builds need a queue, a builder and someone
paying for it; a `pio run` takes minutes, and public build minutes are an
abuse surface. Pre-built packs ("early years", "maths", "everything") need no
backend but multiply the CI matrix by the boards. Both break "one version
identifies one image": About, System Info and the update check would have to
describe a configuration. And a game not picked is gone rather than hidden,
which is worse than the visibility switch that exists. **Do 1.6 first**; it
decides whether this is worth building.

**Status: idea.**

### 1.8 Braino in Attaky's firmware store

**What.** List Braino in the firmware store Attaky offers for its devices.

**Status: blocked on the owner**, until it is settled whether the work with
Attaky is public. Until then nothing public describes how that work came about.

---

## 2. Platforms and releases

### 2.1 Release cadence

**What.** Settle a release habit -- a release at a regular interval, or
whenever a hardware-tested batch is ready. 5.14.0 (2026-11-20) came five
weeks after 5.12.4, with the Attaky port, the RG40XX V image, courses, kept
games, the notice strip and the lobby rework all in one, and `main` had
gathered site commits `dev` never got back; a habit keeps that gap from
opening again.

**Status: blocked on the owner** (hardware testing of the consolidated branch).

### 2.2 A security-update habit for the Linux image

**What.** The RG40XX V card image is a whole Linux system. Track Buildroot's
point releases (and the pinned kernel's stable updates) and rebuild on a
schedule, with a line in the release notes saying which Buildroot it is.

**Why.** A board image is firmware; a Linux image ages like any other Linux
system, and the weekly from-source build in `card-image.yml` only proves it
still builds.

**Status: idea.**

### 2.3 The H700 family, not one console

**What.** Rename `host/rg40xxv/` for the family (`h700`); choose the device
tree per console in the bootloader rather than building one image per model;
settle whether the RG40XX V is a one-stick or two-stick console and what the
other H700 consoles need.

**Status: idea.** The version tag is already `+h700`, so the release side has
named the family.

### 2.4 Card updates that keep profiles and scores

**What.** Updating the RG40XX V today means writing a new card image, which
loses the settings file with the players, scores and levels on it. Proposed,
not chosen: a second release file holding only the boot region, written over
the old one, leaving the data partition alone.

**Status: idea.** Whatever is chosen must keep the sealed secrets
(`host/src/HostSecret.h`) readable after the update, since their key comes from
the processor serial, not the card.

### 2.5 RG40XX V: what stands between the image and a public release

From `host/rg40xxv/README.md`, plus the owner's own list:

- **The ROCKNIX panel files' licence** -- they state none; confirm with
  ROCKNIX. *Blocked on an outside answer.*
- **Permission to use Anbernic's product photo** on the site. *Blocked on the
  owner.*
- **The second panel revision** (`...-v2-panel.dtb`) is not detected.
- **LPDDR3 units**: U-Boot is built for LPDDR4 only.
- **One 128 GB card fails writes** under the mainline kernel.
- **Untested on a console:** the display switching off in sleep, HDMI, the
  headphone jack, the Bluetooth rebind, and Nearby (which showed "Radio
  starting" before that fix).
- **Wi-Fi scans use the real hardware address**; random-address scanning is
  available in the driver and untried.
- **No HDMI sound**: needs a mainline driver for the H700 audio hub, which
  is a kernel driver port, not configuration.
- **The first Wi-Fi scan can miss networks.** A fix was written and shelved at
  the owner's request; reapply only if it comes back.

### 2.6 `envs.py` as a list of targets, not only of ESP32 environments

**What.** `tools/envs.py` is the only list of what CI builds and what a release
publishes, but it knows only PlatformIO environments. The PC build and the card
image are separate workflows with their own conditions. Make it a list of
targets that also covers the host and the card, so a release, the site and CI
learn every target from one place.

**Status: idea.**

### 2.7 The Attaky Core before it is offered

**Status: the move is done.** The Attaky Core became a *product* in 5.15.0,
by the maintainer's decision: its firmware is on Attaky's firmware store. The
device checks below are still worth making, and none of them blocks it:

- flash the consolidated build and check on the device: the round-key symbols
  on quiz answers from the first question; that the selector never matches a
  button's colour in any theme; the d-pad stop rule beside a notice;
- run the `lights` console test and record which physical light answers each
  step;
- set the volume ceiling by ear (it is a provisional 50%);
- make the Get started page's "bought and runs" statement true for it.

**Status: blocked on the owner** (hardware, and the publishing decision).

### 2.8 OTA updates, or a firm decision never to have them

**What.** `BoardUpdate` says a newer firmware exists and stops there, on
purpose: the address shown is compiled in, so a hostile answer can only be
wrong about a number. Installing the update itself would make the answer
executable -- a different threat model, needing signed images and verified
boot whatever else is decided.

**There is nowhere to put a second image today.** Every ESP32 environment
uses `huge_app.csv`, which has one 3 MB app slot (`app0`, labelled `ota_0`)
and no `ota_1`, so `esp_ota_get_next_update_partition()` has nothing to return.

- **The FNK0104B could have it nearly free.** It declares 16 MB of flash but
  still uses the 4 MB `huge_app.csv`, stranding 12 MB. The stock
  `app3M_spiffs9M_fact512k_16MB.csv` gives two 3 MB slots -- the size the
  firmware already targets -- plus a 512 KB factory slot for a recovery image.
- **The 4 MB boards cannot.** The best stock OTA layout, `min_spiffs.csv`, has
  two slots of 1,966,080 bytes. The firmware is 2,706,157: **740,077 bytes
  (about 723 KB, 38%) over**. Removing NimBLE and the BT controller saves about
  192 KB, not a third of the gap.
- **Over Wi-Fi, never Bluetooth.** A ~2.7 MB image over a BLE connection at a
  realistic 50-100 kbps is four to seven minutes of holding still; it needs a
  connection the beacon deliberately does not accept, and a phone app or Web
  Bluetooth page to drive it.
- **The Linux handhelds** are the other half of the question: there an update
  is a card write (2.4), and an in-place update would be a new outbound
  download.

**The decision has three options:** OTA on the FNK0104B only (a two-tier
product the docs would have to explain); no OTA anywhere, said plainly in
`CONTRIBUTING.md` so nobody opens it as an obvious gap; or OTA everywhere,
which commits every future feature to an image under ~1.9 MB. Any of them is
also a change to what leaves the device and needs the owner's agreement first.

**Status: blocked on the owner.**

### 2.9 Releasing on more than one platform: the remaining pieces

- Organise the site's Experimental section by platform (H700, ESP32, the
  Attaky Core), each build with its own page saying what is being tried. A
  draft exists on a local branch.
- A local build of the whole site for the owner to read end to end before it
  goes up.
- Check what the CI work already covers (host on every pull request, the card
  image on tags, image changes and weekly) and drop what is duplicated.

**Status: partly done.**

---

## 3. Framework and architecture

### 3.1 The framework plan

`docs/FRAMEWORK_PLAN.md` is the design for 1.1 and is not restated here. Its
status line still says nothing is implemented. When it starts, two entries
below belong inside it rather than beside it: 3.2 (the panel at run time) and
the base-class rename (`Game` to `Screen`, `GameHost` to a shell host), which
the plan renames anyway -- doing it twice would be worse than doing it late.

### 3.2 ESP-IDF and `esp_lcd`: the panel as a run-time fact

**What.** TFT_eSPI's compile-time configuration is why one product needs one
image per board: everything else about a board is read at run time through
`BOARD`, but the panel driver, size, pins and SPI settings are `-D` flags.
`esp_lcd` describes the panel at run time, so one image could serve several
boards, `BoardConfig.h`'s static_asserts against TFT_eSPI would go, and the
board profile would be complete.

**The cost is drawing and fonts.** TFT_eSPI provides `drawString`,
`textWidth`, rounded rectangles, triangles, seven fonts and the viewport
clipping the scrolling screens rely on, and the launcher header, the lock
screen footer and the top-bar title are laid out against measured text widths.
`Ui::Renderer` already insulates games from the driver, and the host build has
since proved a second renderer with its own fonts can stand behind it.

**How to find out cheaply:** count what `Ui::Renderer` exposes that TFT_eSPI
implements rather than passes through; write one `esp_lcd` spike that brings
up one panel and fills a rectangle; only then decide about fonts.

**Status: idea**, scoped with 3.1 or not at all.

### 3.3 Everything that varies by platform gets an interface

**What.** The working rule: anything that differs by platform is an interface
in `src/` with one implementation per platform chosen at build time, as
`PanelSize`, `PowerProbe`, `SystemSuspend` and `SleepPower` already are.

**Status: partly done.** Practised, not yet written into `docs/PORTING.md` as
the rule a new platform follows. The move of each platform's rules beside
that platform (the root `CLAUDE.md` keeping only product-wide ones) is the
same tidy-up.

### 3.4 Nearby advertises a game index, not a stable id

**What.** Offset 8 of the beacon's manufacturer block is an index into the
playable registry. With per-platform catalogues (1.5) or a game inserted
mid-list, two consoles name different games with the same byte: a wrong score
notice, and an invitation accepted for the wrong game. A stable key -- the
14-bit FNV-1a fold of the game id that visibility and course levels already
use -- would fix it.

**Status: blocked on the owner.** It changes what goes on air, so it needs the
owner's agreement first, and probably a `PAYLOAD_VERSION` bump, which makes
consoles either side of it invisible to each other in Nearby.

### 3.5 Move the beacon's id off the Bluetooth MAC

**What.** `BleBeacon` builds its advertised id from the last two bytes of the
BT MAC. `Board::deviceId()` is a hash that cannot be turned back into the MAC
and changes on a factory reset; using it would be strictly better.

**Status: blocked on the owner.** It changes what the device transmits and what
peers key their saved names on.

### 3.6 Bluetooth headphones

**What.** Sound through Bluetooth headphones, for a console used in a shared
room.

**Status: idea.** On the ESP32 boards it needs A2DP, which NimBLE does not
provide (Classic Bluetooth, the Bluedroid stack this partition cannot
absorb), so it is realistic only on the Linux handhelds. It is a new radio
link, so it is the owner's call even there; it carries audio out, not data
about the player.

### 3.7 Sleep that actually sleeps, on more boards

**What.** True deep sleep needs a wake source: a touch interrupt line, the
BOOT key as an `ext0` source, or a charger status pin.

**Status: partly done.** Panel sleep now also drops the clock to 80 MHz, puts
modem sleep back, and rests the Wi-Fi station after five minutes
(`src/hal/SleepPower.h`); the RG40XX V suspends to RAM (`SystemSuspend`). Still
to do: measure the drain asleep per board on the bench (promised to the tester
whose E32R40T went flat overnight), and check each ESP32 board with a meter
for a wakeable line before assuming none exists.

### 3.8 Wake when the charger goes in

**What.** Plug a sleeping console in and nothing happens. Waking briefly to
show that it is charging, then sleeping again, answers "did it start?".

**Status: partly done, uncommitted.** Work sits uncommitted in the worktree
`wake-on-usb-connect-7d702e`, on a base from before 5.10.0, which removed the
charging display and its charge-inference constants from the product -- so it
needs re-basing and re-thinking rather than merging. Three things it must
honour whatever the detection:

- **Going to sleep looks like plugging in.** The backlight turning off is a
  load drop, and the cell's terminal voltage steps up. Detection by a voltage
  step must re-prime on the sleep transition, or prefer an absolute threshold
  above what a resting cell holds, and require the verdict to persist.
- **Wake, show the charge, sleep again** -- never leave a screen lit all night.
- **Never unlock.** A cable pushed in inside a bag must land on the lock
  screen or a charge display, never on the screen underneath.

On the Attaky Core the fuel gauge and on the RG40XX V the kernel's power
supply class report charging directly, so those platforms need no inference.
A future ESP32 board that wires the charger's status pin to a GPIO would get
both a clean signal and a real deep-sleep wake source.

### 3.9 Battery: a runtime estimate, and logging on battery

**What.** "About two hours left" rather than a percentage, stated as a range,
from a discharge curve captured with `env:batdiag` on the reference pack and
the measured draw (`PowerMonitor` already measures where the battery goes).
And on-battery logging (issue 14): a way to capture a discharge without a
cable attached.

**Status: idea.** The issue of the percentage bouncing (issue 8) is the same
physics -- voltage sags under load -- and is still open.

### 3.10 An on-device look at why it restarted

**What.** System Info already shows the reset reason. `Watchdog`'s crash
breadcrumb (which screen, what it was doing) is still serial-only. A "Last
restart" row showing it, **display only**: anything that transmits it would be
a new outbound flow and is out of scope permanently.

**Status: partly done.** A few hundred bytes of static RAM.

### 3.11 The heap report in the UI

**What.** `leaveActiveGame()` logs `[heap] '<screen>' left N bytes short`,
which nobody without a serial cable sees. A row on System Info's Memory tab --
the worst offender since boot, and by how much -- turns it into something a
tester can report.

**Status: idea.** A couple of hundred bytes of static RAM.

---

## 4. Content and curriculum

### 4.1 New games proposed or waiting

| Game | Where it stands |
|---|---|
| **Sign ABC** -- the manual alphabet | Open pull request 228 against `dev`. |
| **NumberNest** | Open pull request 197 against `dev`. |
| **Chinese checkers** | Idea. It would ride the nearby turn the way Ludo does (a seat and a hole fit the existing fields), and two to six players is exactly what Ludo's table protocol already solves. |
| **An offline Dino-style runner** (issue 25) | Idea. A running game is new ground: it repaints every frame, so it must be built on partial redraw from the start (Cinnamon is the reference). |
| **Electronics schematic symbols** -- a quiz on resistor, capacitor, diode, ground and friends | Idea, from a tester. Symbols are lines and arcs, so they can be drawn rather than stored, like the shapes. |
| **Proof of work** -- see 4.6 | Idea. |

Every one of them follows the four-group checklist in `CLAUDE.md`, and every
one costs flash on boards at 86% (1.6).

### 4.2 Courses: what is left

Twenty-seven games are courses (`src/engine/Course.h`): a level kept per
player, a deck that deals every question once per pass, and real mistakes as
the wrong options. What is left:

- **Seen live on a device:** a level-up, and *Carry on* in the middle of a
  question.
- **For the owner to judge:** Shape Arith level 6 wording, and the blink it
  dropped; Fingers levels 1 and 2 are nearly free; Fingers level 2's right
  answer is never the smallest option; Fingers level 5's hint names both
  counts; Fingers level 6 has 110 questions.
- **Games that stay as they were, by decision:** Slide, Tic-Tac-Toe, Microku,
  Maze, Piano and Whack A Mole (Whack's course was tried and reverted).

**Status: partly done.**

### 4.3 Money: notes as well as coins

**What.** Money is a course of eight levels and already asks for change from
two prices and the fewest coins -- but the largest thing in it is a 50-cent
coin. The September ask was change from $10 for a $5.75 price, given in notes
and coins:

- a denomination table with $1, $5, $10 and $20 notes beside the coins, drawn
  as rounded rectangles rather than circles;
- *build the change* with the existing Make tray, accepting any correct total
  and then showing the fewest-count answer ("here is how a shop would count
  it");
- *counting up*, the way change is actually given (from $5.75: 85, 95, $6, $7
  ... $10), as its own mode.

**Decide first:** the course labels coins by value because coin names belong
to one country. Notes make that harder -- either Money states that it is
US-only, or the table becomes a profile, decided now rather than after a
second set is hard-coded.

**Status: partly done.**

### 4.4 Shapes: the rest of the run

**What.** Shapes is now a course over seventeen shapes (`ShapesCourse.h`):
the polygons to the octagon, oval, rectangle, diamond, trapezium,
parallelogram, kite, cross, arrow and two stars. Not yet in it: the
semicircle, the nonagon and decagon (which need one large shape to be legible
at all on this panel), the heart and the crescent; and the follow-on lessons --
kinds of triangle (right, isosceles, scalene) and spotting shapes in a drawn
scene.

**Status: partly done.** Near-zero flash: regular shapes are one generator.

### 4.5 Piano: more than one octave, and something to play

**What.** Piano is one octave, C4 to C5. An octave shift (two chips), and
*follow the light*: the console lights a key, the player presses it, it lights
the next -- a handful of tunes as note tables is under a kilobyte. Notes stay
generated; **there is no audio file, ever**.

**Status: idea.** The one-octave width was chosen so the keys stay pressable
in portrait; a shift keeps that.

### 4.6 Proof of work as a teaching game -- and why a miner cannot ship

**Asked:** could the console also mine Bitcoin? **No**, and the privacy rule
is the least of the reasons:

- **It is a new outbound flow.** A pool connection submitting shares for as
  long as it runs, carrying a payout address that ties to the console's IP
  over time. A persistent financial identifier on a young player's console is
  the opposite of what the product promises.
- **The arithmetic.** Software double-SHA-256 on an ESP32 manages roughly
  50-80 kH/s with both cores; with the frame loop, BLE and Wi-Fi running, call
  it 30 kH/s. Against a network of the order of 10^21 H/s finding a block
  every ten minutes (about 6x10^23 hashes a block), that is one block in about
  2x10^19 seconds -- some 600 billion years.
- **The battery.** Both cores pinned with the radio up is the fastest way to
  flatten the console, heat it inside a printed case and wreck the frame
  budget.

**The version worth building:** a *Proof of Work* game that hashes locally at a
difficulty a player can beat. Change a short text and watch the hash change
completely; then set a target ("a hash starting with three zeros"), let the
console search for a second or two, and celebrate the hit. No network, a small
SHA-256 (the ESP32 has a hardware accelerator), no data table, and bounded
batches per frame so it never blocks.

**Status: idea.**

### 4.7 Content packs on the SD card

**What.** `docs/SD_CONTENT_SPEC.md` describes a `/games` tree, but the card is
used only for Memory and Counting settings. A quiz whose question bank lives on
the card would let content be written by someone who does not build firmware,
and is 1.1's "games as libraries" reached from the other end.

**Decide first:** with no card in the slot the answer must still be a complete
console, not a missing tile. And parsing must allocate nothing per frame.

**Status: idea.** Saves flash rather than costing it, for the data.

---

## 5. Multiplayer

Two consoles (four, for Ludo) already play Chess, Go, Backgammon, Sea Battle
and Ludo over the opt-in beacon, with lobbies, standing invitations, kept
games and a pause when a console goes quiet.

### 5.1 Lobbies: what is left before a release

- **Walk all five games on two or more real consoles.** Everything so far was
  on the PC build; nothing is proven on a lossy radio.
- **The second review pass never ran.**
- **Confirm** that the selector stays enabled on the other player's turn in
  all five games.
- **Service follow-ups:** `invite()` clears `stopAtMs_`; reuse the coin for
  the same session; `nearbyInviteForUs()` prefers an invitation for this app;
  a grace period in `NearbyWatch` after `reset()`.
- **Known gaps:** a Ludo run where a third console that had hosted its own
  table joined another after Start and left the two playing consoles stuck
  (seen once, not reproduced); Ludo's ending has no ply window; the Go lobby
  repaints the whole panel for a chip tap; `freshSession()` does not know
  sessions held only in a console-slot save.
- **Decide:** a standing invitation survives sleep, the saver and the lock
  screen. That matches "stands until answered", but maybe not for hours.
- **Docs:** the BLE spec, the changelog and lobby mock-ups.

**Status: partly done.**

### 5.2 Stable game ids on air, and the beacon id off the MAC

See 3.4 and 3.5. Both are changes to what the console transmits and wait on
the owner.

### 5.3 What a connection would cost

The beacon is non-connectable on purpose and the advertisement is exactly
31 bytes. Suggestions to widen it with a NimBLE data pipe would replace a
broadcast nobody has to accept with a link between two devices -- a change to
what the product promises, needing agreement before any code. Kept here so the
choice is argued rather than drifted into: keep a payload that fits in a
beacon, or accept connections.

---

## 6. Site and community

### 6.1 A site that is not ESP32-only

**What.** The landing page still describes firmware for the ESP32 "CYD". Rework
it for a product on several platforms:

- Home and Get started that start from "which console do you have";
- the installer grouped by **install method** (flash in the browser; write a
  card) rather than by board;
- privacy text for the handheld (the Linux image's own list of what leaves
  it, and where its secrets are kept);
- a "needs a stick" or "needs a card reader" mark where a platform needs one;
- an Experimental section organised by platform (2.9).

**Status: idea**, with a draft branch for the Experimental part.

### 6.2 A board decision table: best for, and what you give up

**What.** The README's "Which board do you have?" table tells boards apart; it
does not say which to buy. A buying table answers "which board, and what am I
giving up", and belongs first under the installer's board picker, where
somebody is choosing right now.

**Say what is common first:** whichever board, the same games, the same
screens and the same privacy promise, because `BoardConfig.h` will not compile
a board without a 320x240 landscape panel, touch, backlight on a GPIO and
4 MB of flash. Everything else -- speaker, SD slot, battery sense, a case --
is optional, and the table is about those.

**The rows, as the facts stand now** (re-derive before publishing):

| Board | Best for | What you give up |
|---|---|---|
| Freenove FNK0104B (ESP32-S3) | Arrives complete: codec and speaker fitted, the clearest boot phrase, capacitive touch with nothing to calibrate or carry, a microphone, 16 MB of flash. | Tracing is less precise with a fingertip than a stylus. No case. 12 MB of its flash unused (2.8). |
| E32R40T (4-inch) | The biggest screen, and the best for a player who struggles to read: 320x480, with larger launcher text. Sound. | No SD slot. No case. Needs a battery-drain check (3.7). |
| E32R32P (3.2-inch) | The same 240x320 as the 2.8-inch on a bigger panel: every letter physically larger. Sound. | No case. Volume ceiling inherited rather than tuned by ear. |
| E32R28T-1 (2.8-inch, reference) | The board everything is tested on, with the only printable case, an SD slot and sound since 5.10.0. | Smallest panel of the resistive boards. |
| ESP32-2432S028, dual-USB | Only if you already have one. | No battery hardware: run it from USB power. No case. |
| Attaky Core | The Cyber Deck Plus: keys, a clip-on keyboard and gamepad, swappable modules. Also on Attaky's firmware store. | 2.0-inch panel; no battery gauge yet. |

**Generate the fact columns, hand-write only the judgement.** Audio backend,
SD, resolution, touch, flash and whether `cases/<name>/` exists come from the
profiles; the "best for" sentence should be one more field in each profile,
which `check_boards.py` requires. A price is the one fact that cannot be
generated or checked: publish a **band** with the date it was set, and compare
**total** cost (board, battery, speaker where one is needed), not board cost.
Prices noted on 2026-09-08 (Amazon US, per board from multipacks) put a
complete console between about $24 and $32, the Freenove dearest; they need
re-checking before use. Next to purchase links, keep the plain
**not sponsored** statement directly above the table, and publish bare product
links with no referral or order-history parameters.

**And say the table is meant to change:** a row is where the work has got to,
and a correction to a board is usually a small diff in its one profile header.

**Status: idea.**

### 6.3 Flash several boards at once from the browser

**What.** The installer flashes one board per visit. A `/batch` page built on
`esptool-js` -- one loader per port, running in parallel -- would serve a
classroom or a reseller.

**Constraints that shape it:**

- Web Serial grants **one port per user gesture**, so the page is *Add device*
  per board and then one *Flash all*; a return visit gets the granted ports
  back with `getPorts()`.
- **The chip cannot tell you the board** -- two CYD variants report
  identically -- so a batch is **one board type**, chosen by the person, never
  guessed. A wrong image gives a board that draws and does not respond to
  touch.
- Bounded concurrency (a hub of lit backlights is a real load; one laptop's
  USB has already tripped its surge protection with four boards), one failure
  never takes the batch down, and a line saying the browser holds the ports
  while the tab is open.
- It reads the same generated manifests as the main installer, never a second
  description of them.

**Status: idea.** The local half exists: `ESP32_boardUtil.py --flash`.

### 6.4 Housekeeping on the tracker

- **Close the issues that are done:** 5, 27, 44 and 50.
- **Open pull requests** to decide: 228 Sign ABC, 207 the CYD2USB board
  (waits for its reporter's check on the device), 197 NumberNest, 196 snapshot,
  227 an "In the works" page, 161 the lineup, 176 pounds to dollars.
- **Open defect:** 8, the battery percentage bouncing.
- **CI over-building** (issue 89): check what the reworked CI already answers.

**Status: idea.**

### 6.5 A "start here" path through the code

**What.** `CONTRIBUTING.md` explains how to submit before how to understand. A
short guide that traces one small screen end to end -- Tic-Tac-Toe: metadata,
`AppRegistry`, `begin`/`update`/`render`/`end`, `markDirty()` against
`markFullDirty()`, where its score is stored -- with file references, as the
way in for someone who does not own a board (the PC build makes that
realistic now).

**Status: idea.**

---

## 7. Hardware and cases

### 7.1 The 4-inch case

**What.** Feedback from a tester who printed it: the tabs and the button
extensions are too thin, the C-clips too thick for the posts, the BOOT/RESET
lettering is mirrored, and the edges are sharp. The case has no source CAD in
the repository and its README sections are empty.

**Status: idea.** Cases are designed outside this repository; a fix needs the
source, and the README should at least say where it lives.

### 7.2 More cases

**What.** A case for the FNK0104B, and variants with room for a battery or a
speaker (issues 13 and 79).

**Status: idea, low priority.** A board is supported without one.

### 7.3 A diagnostic that identifies an unknown board

**What.** Issue 55: a probe, or an installer page, that says which supported
board is attached. Picking the wrong CYD profile does not fail loudly -- it
draws a stable picture in wrong colours with dead touch.

**The caution:** reading the panel's ID register is what broke two boards'
displays in 5.9.0 (`readcommand8()` leaves a panel mid-command when MISO is not
wired back), and on seven boards it never returned a real ID. A probe must be
its own environment that never runs before the product, and must not rely on
reading the panel back.

**Status: idea.**

---

## 8. Accessibility

The ground is strong and should not be broken by accident: nine themes
including High Contrast, every pairing checked against WCAG floors
(`check_contrast.py`) and every screen seen in every theme; brightness floored
so a player cannot dim themselves out of the setting; nothing that depends on
colour alone.

### 8.1 Controls act on touch-down (WCAG 2.5.2)

**What.** 67 files reference `justPressed` against 13 that reference
`justReleased`. For Whack A Mole a press *is* the game, and the criterion
exempts that. For a launcher tile, a Settings switch or Remove player it is
not: there is no sliding off a wrong tap. Act on release for controls, keeping
press for the pressed look; destructive and admin actions first.

**Status: idea.** A wide, shallow change -- land it in one pass while nobody
else has work open in `src/games/`.

### 8.2 Text size

**What.** No setting makes text larger, and layouts are absolute pixels
against measured widths, so a global scale would overrun the packed headers.
Narrower versions first: scale only prose (About, blurbs, rules if 9.1
happens, System Info rows), or a large-text theme. The 3.2- and 4-inch boards
already give larger letters with no software.

**Status: idea.**

### 8.3 The smallest control

The smallest control is the 18x18 padlock; the 8px hit slop lifts it to about
6 mm on the 2.8-inch panel, against roughly 6.4 mm for WCAG 2.5.8. Marginal, and
the header has been squeezed twice. Measure the next thing added to that row
against this. **Status: a note, not work.**

---

## 9. Documentation and tooling

### 9.1 A launch card with the rules, and an (i) for them

**What.** The owner asked for a splash before an app and an (i) on every app
explaining the game. One answers the other: a card showing the icon, title and
a few lines of rules is a splash worth its moment.

- **Not in the top bar**, which has no pixels left; a whole card instead.
- **One new `rules` field in `AppMetadata`**, beside the blurb (which is
  capped near 46 characters). About and the site could then show the rules
  too, derived rather than written twice. About 300 characters for each of 40
  games is roughly **12 KB of flash** -- affordable, but at 86% not free.
- **It must not block** (a timed state, never a `delay()`), it costs a
  second full repaint per launch (~30 ms) so it must not animate, and **the
  tap that dismisses it must be swallowed** or it presses the game's first
  answer.
- Auto-advance plus tap to go now, and a global setting to turn it off -- the
  twentieth launch of a game does not need it. Lead with the icon: many
  players cannot read the rules yet, so the text is for whoever helps them.

**Status: idea.**

### 9.2 Settings switches asked for

- **Turn off "Let's play Braino!" at boot** without muting everything; today
  only Mute silences it. A global device setting with a RAM mirror like the
  others; a few bytes.
- **Hide the Settings gear from players who are not admin.** Deferred on
  2026-09-07; **the owner decided it on 2026-11-20 and it is done in 5.15.0**:
  Settings, Wi-Fi and System Info are admin-only (`APP_CAP_ADMIN_ONLY`), with
  no tile and no gear for anyone else. See "Settings, Wi-Fi and System Info
  are the admin's alone" in `CLAUDE.md`.

**Status: idea.**

### 9.3 Portrait is a per-screen decision, and `CLAUDE.md` still says otherwise

**What.** Portrait for every system app (issue 22) was closed as not planned on
2026-09-08: no board has a gyro, and some screens are genuinely better in
landscape. The root `CLAUDE.md` still states the old rule in two places and
says Settings and Wi-Fi "violate" it. The wording to propose: system apps *may*
support both orientations, and those that do must read `tft.width()` /
`tft.height()`; drop the accusation.

**Status: blocked on the owner** -- it rewrites a standing rule.

### 9.4 Wider static checks

**What.** `check_frame_rules.py` checks allocation, `String` per frame and
`delay()` on a render path. Widen it to an NVS read reachable from a
`render()`, a sensor sampled from a getter, and a `String` member on a screen
other than the sanctioned user-text ones. And extend the game-count check to
every prose file: the September backlog found `CLAUDE.md` claiming a count
that `check_docs.py` did not look at.

**Status: idea.** Tools only.

### 9.5 Mock-ups that can catch an erase-over

**What.** `gen_screens.py` draws with no clear rectangles, so an over-erase is
invisible in it. **Partly answered:** the PC build's `BRAINO_SHOTS` photographs
the firmware's own frames, clears included. What remains is to use it for the
README stills, or to say plainly in `gen_screens.py`'s output that its
pictures cannot show clipping.

**Status: partly done.**

---

## 10. Done since the September backlog

Kept so that nobody re-opens them as ideas.

| September idea | Where it lives now |
|---|---|
| A Space quiz | `SpaceGame` over `SpaceCourse.h`: eight levels, generated questions on the planets and the layers of air. |
| Roman numerals | `RomanGame` over `RomanCourse.h`. |
| Chess against the console | `ChessAi.cpp`: two computer levels, searched across frames. |
| Two-console play over the beacon | Nearby play: Chess, Go, Backgammon, Sea Battle, Ludo (section 5). |
| A drill that adapts | 27 courses on `src/engine/Course.h`. |
| Seven system screens named `*Game` | Renamed `*App` (`AboutApp`, `WifiApp`, ...). |
| Launcher tile labels failing contrast | `Ui::onFill()` / `onFillSoft()` choose the ink per fill; `check_contrast.py`. |
| A board matrix and printed pin maps | `docs/boards/`, generated by `tools/gen_board_docs.py`, with `--check`. |
| Flash several boards at once, locally | `tools/ESP32_boardUtil.py --flash`. |
| Visibility for more than 32 games | Keyed by game id, 64 slots (`src/hal/GameVisibility.h`). |
| Money and Shapes as real lessons | Courses with change, fewest coins and seventeen shapes (4.3 and 4.4 hold what is left). |
| Silent 2.8-inch boards | Sound on the E32R28T-1 and the inverted CYD since 5.10.0. |
| One version on every platform | `FIRMWARE_VERSION`, with a `+device` tag (`include/FirmwareVersion.h`). |

---

## 11. Dropped, and deliberately not on this list

**Dropped by the owner:**

- Trademark registration, signed tags and attested builds ("do not worry about
  this").
- A desktop build for players (1.3).

**Never, by rule:**

- Analytics, usage counters, crash reporting, or any outbound flow beyond the
  closed list. Not a feature waiting for a good design; a change to what the
  product is.
- Cryptocurrency mining or any pool connection (4.6).
- Audio files of any kind: one second of 16-bit 16 kHz mono is 32 KB.
- A heap: every idea here has to fit in fixed members and the stack.
- Anything reachable only by a key: a board may have no BOOT key and no
  gamepad, and the console must stay complete.
- Android, iOS, or any port that runs inside another operating system (1.3).

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit -->

---

*Part of [Braino!](https://github.com/iamankushpandit/Gume) by [iamankushpandit](https://github.com/iamankushpandit). Copyright © 2026 iamankushpandit, licensed [GPL-3.0-or-later](https://github.com/iamankushpandit/Gume/blob/main/LICENSE) alongside the code — reuse of this document, in whole or in part, must keep this attribution and stay under the same licence. See [NOTICE.md](https://github.com/iamankushpandit/Gume/blob/main/NOTICE.md).*
