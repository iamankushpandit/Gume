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

### Manufacturer data, layout version 2

| Offset | Bytes | Field |
|---|---|---|
| 0 | 2 | Company id `0xFFFF`, little endian |
| 2 | 2 | Family tag `"BR"` |
| 4 | 1 | Layout version — `2` |
| 5 | 2 | Device id, the two MAC bytes |
| 7 | 1 | Flags. Bit 0 = shares Nearby activity; the rest reserved, sent as zero |
| 8 | 1 | *(sharing only)* Open game, an index into the playable app registry |
| 9 | 4 | *(sharing only)* Best score for that game, little endian |

The last two fields are present **only** while Nearby play is on. When it is
off the block is eight bytes and stops at the flag byte — the fields are absent
from the air, not present and zeroed, because "not transmitted" has to be
structural to be worth claiming. `decode()` refuses to read them unless both the
flag bit is set and the block is long enough, so a truncated advertisement can
never be read as a game and score that were never sent.

- **Device ID** is the last two bytes of the factory Bluetooth MAC, rendered as
  four uppercase hex digits. It is a hardware serial, stable across reboots so a
  parent can recognise their own unit in a scanner. Nobody types it and it is
  not derived from anything a child entered.
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

Total payload is 27 of the 31 legal bytes with Nearby off, and **exactly 31 with
it on**. There is no slack left: a longer device name or another AD structure
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

Child information · child name · profile name · location · Wi-Fi credentials ·
Wi-Fi SSID · IP address · game progress · usage history.

Nothing profile-scoped (`Board::scopedKey()`) is read by `BleBeacon` at all.
`engine/NearbyPlay` does read the active profile's best score in order to
publish it — that is the one number the owner opted in to sharing, and it
travels with no name attached.

### The anonymity argument, in one line

A peer is four hex digits of its own hardware MAC. There is no name, no
profile, and no path from a score back to a child. Two children learn that
*someone nearby has 9 on Maze*; neither learns anything about the other.

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
