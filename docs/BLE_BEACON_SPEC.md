# BLE Beacon — design contract

The device may broadcast a BLE presence beacon. It is **off by default** and
opt-in from *Settings → Beacon*.

The governing principle: **if the device transmits something wirelessly, the
owner must be able to see what it is transmitting, from the device itself.**

## One authoritative payload

`BleBeacon::Advertisement` (`src/hal/BleBeacon.h`) is the single description of
the outgoing advertisement. `buildPayload()` compiles it into a raw AD-structure
buffer; `startRadio()` hands the controller **that exact buffer** via
`NimBLEAdvertisementData::addData()` rather than the per-field helper setters,
and the System Info *BLE* tab reads the same struct back.

`BleBeacon::decode()` is the exact inverse of `buildPayload()`'s manufacturer
block, and the scanner in `src/hal/BleScanner.cpp` uses it to read peers. That
is deliberate: a transmit copy and a separate receive copy of a wire format
drift apart, and the first symptom is two consoles that cannot see each other
for no visible reason.

There is deliberately no second, hand-written UI description of the payload. To
change what goes on air, change `Advertisement` / `buildPayload()`; the screen
follows. This is the only arrangement in which the display and the radio cannot
silently diverge.

## What is broadcast

| AD type | Contents |
|---|---|
| `0x01` Flags | `0x06` — LE General Discoverable, BR/EDR not supported |
| `0x09` Complete Local Name | `Braino-<id>` |
| `0xFF` Manufacturer Data | see below |

### Manufacturer data, layout version 4

| Offset | Bytes | Field |
|---|---|---|
| 0 | 2 | Company id `0xFFFF`, little endian |
| 2 | 2 | Family tag `"BR"` |
| 4 | 1 | Layout version — `4` |
| 5 | 2 | Device id, the two MAC bytes |
| 7 | 1 | Flags. Bit 0 = shares Nearby activity, bit 1 = poking, bit 2 = chess invitation, bit 3 = chess move; the rest reserved, sent as zero |
| 8 | 1 | *(any flag set)* Open game, an index into the playable app registry |
| 9 | 4 | *(sharing, no poke/invite/move)* Best score for that game, little endian |
| 9 | 2 | *(poke or invite)* Target — the device id being poked or invited |
| 11 | 1 | *(poke or invite)* Nonce; for an invitation this is the session id |
| 9 | 4 | *(move only)* Packed move, little endian — see below |

**Version 4 exists because a move is the same length as a score.** A chess
block and an activity block are both thirteen bytes, so length alone cannot
tell them apart — only the flag can. A version-3 reader meeting a version-4
move would decode it as a best score and cheerfully show somebody several
million points. That is the identical failure that took version 2 to 3 when the
poke's *shorter* block arrived, and the lesson has now been learned twice:
**gate every field on its own length AND its own flag.**

#### The packed move

Thirty-two bits, little endian, in the four bytes the best score would
otherwise occupy:

| Bits | Field |
|---|---|
| 31–26 | Session id (6) — which game, so two boards in one room do not merge |
| 25–19 | Ply (7) — move number, modulo 128 |
| 18–13 | From square (6) — 0–63 |
| 12–7 | To square (6) — 0–63 |
| 6–0 | Ack (7) — the highest ply of *theirs* this device has applied |

There is nothing else, and there is nowhere to put anything else: the sharing
payload already uses all 31 legal bytes, so a move had to **displace** the
score rather than follow it. While a game is running the score is structurally
absent, exactly as it is during a poke.

Ply wraps at 128. That is safe because the two sides only ever advance one ply
at a time and each carries the other's ack, so they can never be more than a
move or two apart.

#### Why a move is a state and a poke is an event

A poke stops after `POKE_ADVERTISE_MS`. A move does **not**: it stays on the
air until the next move replaces it. That is the whole reliability story — an
opponent may have missed three scan windows, or only just walked back into
range, and it can pick the game up from any later window. The acknowledgement
rides the opponent's own advertisement, so there is no separate ack message
that could itself go missing.

A chess *invitation*, by contrast, is an event and uses the poke's exact wire
shape and timer. That reuse is deliberate: an invitation is the same kind of
thing — aimed at one peer, repeated because scan windows have gaps, acted on
once per (device id, nonce) — and sharing the layout means sharing the
idempotence argument rather than writing a second one slightly differently.

#### What a receiver must do with a move

Four tests, all required:

1. **From the peer we are playing.** Another console's game must not leak in.
2. **In this session.** Nor an earlier game between the same two consoles.
3. **The ply we are expecting.** An advertisement repeats; acting once is what
   makes it a move rather than a stutter.
4. **Legal in the receiver's own position.** This is the safety property: a
   move is applied only if it is legal on the board the receiver already has,
   so a confused or hostile advertiser cannot force a position that is not
   reachable by playing chess. At worst — and only by guessing both the session
   and the exact ply — it can play a legal move.

Everything from offset 8 on is present **only** when the flag that names it is
set. With Nearby off the block is eight bytes and stops at the flag byte — the
fields are absent from the air, not present and zeroed, because "not
transmitted" has to be structural to be worth claiming.

**A poke displaces the score; it is not appended to it.** The sharing payload is
already exactly 31 bytes, so there is nowhere to put three more. While a poke is
on air the block is twelve bytes: the game stays visible and the four score
bytes are simply not sent. Peers keep the last score they heard rather than
reading a zero, because absent is not nought.

That is why the version went to 3 and why **every field is now gated on its own
length**. Version 2 decided "is the game here?" and "is the score here?" with
one test against a single length, which a poke's shorter block answers wrongly
for both. `decode()` refuses to read any field unless its flag is set *and* the
block is long enough to have carried it, so a truncated advertisement can never
be read as a game, a score or a poke target that was never sent.

### The poke, and what it actually is

A poke is a nudge one console sends another: the target raises a notification
and makes a sound. Three properties are part of the contract.

- **It is a broadcast, not a message.** Non-connectable advertising has no
  addressing. Every Braino in range hears that `A4F2` poked `B1C3`; only `B1C3`
  reacts. The API says so and the UI must not imply a private channel, because
  there is not one.
- **The only identifier it carries is one already on air.** The target's device
  id is the same four hex digits that device broadcasts about itself, every
  second, as its own name. A poke adds no new *kind* of data to the radio — it
  adds an event, and names a party using that party's own public id.
- **It is an event, so it ends.** The poke is transmitted for
  `POKE_ADVERTISE_MS` (6 s) because a peer's scan windows have gaps and a
  single-shot poke can be sent perfectly and never heard. The **nonce** is what
  makes that repetition safe: a receiver acts on a (device id, nonce) pair
  exactly once, however many copies it hears. The nonce increases across pokes
  and is never reset, so a second poke to the same peer is a new event rather
  than a repeat of the last one.

Poking requires Nearby play to be on, which requires the beacon to be on. There
is no path that pokes while the radio setting says the device is quiet.

- **Device ID** is the last two bytes of the factory Bluetooth MAC, rendered as
  four uppercase hex digits. It is a hardware serial, stable across reboots so a
  parent can recognise their own unit in a scanner. Nobody types it and it is
  not derived from anything a player entered.
- **Company ID `0xFFFF`** is the SIG's reserved "no company / testing" value. We
  are not a member company; claiming another company's identifier would be worse
  than honestly using the reserved one.
- **Layout version** is checked by peers before anything after it is read, so a
  future layout is ignored rather than mis-decoded by an older device.
- **Open game** is an index into `APP_REGISTRY`'s playable entries. Two devices
  agree on what index 12 means because they compile the same table; the version
  byte is what stops devices that do *not* from guessing.
- No service UUID and no service data are advertised. Those fields exist in the
  struct at zero length, and the UI omits their rows rather than printing a
  value that is not on air.
- Advertising is **non-connectable** (`ADV_NONCONN_IND`) at a 1000 ms interval,
  +3 dBm. There is no GATT server; there is nothing to connect to.

Total payload is 27 of the 31 legal bytes with Nearby off, **exactly 31 with it
on**, and 30 while a poke is being transmitted. There is no slack left: a longer device name or another AD structure
would push the manufacturer block off the air. `buildPayload()` logs and drops
the block rather than transmitting a half-advertisement if that ever happens.

## Listening

`src/hal/BleScanner.cpp` observes other beacons while Nearby play is on. Three
properties are part of the contract, not incidental:

1. **The scan is passive.** It never transmits a scan request, so a console
   that is listening puts nothing extra on air.
2. **A peer must match both the name prefix `Braino-` and the manufacturer
   block**, at a version we know. Either alone is not enough.
3. **Nothing allocates.** The sighting table is a fixed array of eight and the
   advertisement is parsed straight out of the controller's buffer — the
   callback runs at whatever rate the air is busy, which is exactly the churn
   that fragments this heap.

## What is never broadcast

Structural, not a promise typed into the UI: `buildPayload()` emits a name AD
and a manufacturer AD and nothing else, so none of the following is reachable
from the radio path.

Player information · player name · profile name · location · Wi-Fi credentials ·
Wi-Fi SSID · IP address · game progress · usage history.

A poke does not change this list. It transmits an event and one device id that
the addressed device is itself already broadcasting; no field naming a person is
reachable from `buildPayload()` whether a poke is live or not.

Nothing profile-scoped (`Board::scopedKey()`) is read by `BleBeacon` at all.
`engine/NearbyPlay` does read the active profile's best score in order to
publish it — that is the one number the owner opted in to sharing, and it
travels with no name attached.

### The anonymity argument, in one line

A peer is four hex digits of its own hardware MAC. There is no name, no
profile, and no path from a score back to a player. Two players learn that
*someone nearby has 9 on Maze*; neither learns anything about the other. They
can now nudge each other, and still learn nothing: a poke says *that tag wants
your attention*, which is the same anonymity with a doorbell attached.

### Local names, and why they do not weaken any of that

The owner of a device may label a tag they recognise -- "RAVI" against `A4F2`
-- so that a poke says who rather than what. Three properties keep this on the
right side of the line, and all three are structural:

- **The label never leaves the device.** `BleBeacon` does not read it and must
  never be given a reason to: `buildPayload()` composes the advertised name
  from the family id and the hardware id, and a label reaching the payload
  would be a privacy defect rather than a bug. What goes on air is unchanged,
  byte for byte, whether every peer is named or none is.
- **It is the owner's word, not the peer's.** The named device does not know it
  has been named, is not asked, and cannot see or set the label. This is the
  model a phone's contact list uses, not the model a social network uses.
- **Storing is not collecting.** The labels live in this device's own NVS
  beside the scores and the profiles, exactly as `CONTRIBUTING.md` describes,
  and leave it by no route at all.

They are **global to the device rather than per player**, because the consoles
in the room are the same consoles whoever is holding this one; and **only the
admin profile may set one**, so a label every player sees cannot be written by
any player. The tag itself stays visible beside the name in the Nearby list --
without it nobody could work out which console "RAVI" actually is when the
label turns out to be on the wrong one.

## What System Info must show

The *BLE* tab is part of the feature, not a nicety. It must show:

1. whether advertising is **currently active**;
2. the actual device name being advertised;
3. every application-defined field in the manufacturer data, decoded —
   including the Nearby fields when, and only when, they are on air;
4. under *Show advanced*: interval, TX power, advertising type, controller
   address, payload length, and the **raw advertising bytes in hex**;
5. the privacy list above, with **Open game** and **Best score** reported as
   `Broadcast` / `Not Broadcast` derived from `Advertisement::sharesActivity`
   rather than from typed-in copy;
6. when the beacon is off — *Broadcasting: Nothing*, with the identity block
   relabelled `Config` so no row reads as being on air.

`BleBeacon::broadcasting()` returns `nullptr` when the controller is not
advertising. That null is what separates "configured" from "on air"; the UI
must key off it rather than off the stored setting, which can be `On` while the
radio failed to come up.
