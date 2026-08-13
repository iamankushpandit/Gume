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

There is deliberately no second, hand-written UI description of the payload. To
change what goes on air, change `Advertisement` / `buildPayload()`; the screen
follows. This is the only arrangement in which the display and the radio cannot
silently diverge.

## What is broadcast

| AD type | Contents |
|---|---|
| `0x01` Flags | `0x06` — LE General Discoverable, BR/EDR not supported |
| `0x09` Complete Local Name | `LearnKey-<id>` |
| `0xFF` Manufacturer Data | company `0xFFFF`, `"LK"`, layout version, 2 id bytes |

- **Device ID** is the last two bytes of the factory Bluetooth MAC, rendered as
  four uppercase hex digits. It is a hardware serial, stable across reboots so a
  parent can recognise their own unit in a scanner. Nobody types it and it is
  not derived from anything a child entered.
- **Company ID `0xFFFF`** is the SIG's reserved "no company / testing" value. We
  are not a member company; claiming another company's identifier would be worse
  than honestly using the reserved one.
- No service UUID and no service data are advertised. Those fields exist in the
  struct at zero length, and the UI omits their rows rather than printing a
  value that is not on air.
- Advertising is **non-connectable** (`ADV_NONCONN_IND`) at a 1000 ms interval,
  +3 dBm. There is no GATT server; there is nothing to connect to.

Total payload is 27 of the 31 legal bytes.

## What is never broadcast

Structural, not a promise typed into the UI: `buildPayload()` emits a name AD
and a manufacturer AD and nothing else, so none of the following is reachable
from the radio path.

Child information · child name · location · Wi-Fi credentials · Wi-Fi SSID ·
IP address · game progress · scores · usage history.

Nothing profile-scoped (`Board::scopedKey()`) is read by `BleBeacon` at all.

## What System Info must show

The *BLE* tab is part of the feature, not a nicety. It must show:

1. whether advertising is **currently active**;
2. the actual device name being advertised;
3. every application-defined field in the manufacturer data, decoded;
4. under *Show advanced*: interval, TX power, advertising type, controller
   address, payload length, and the **raw advertising bytes in hex**;
5. the privacy list above;
6. when the beacon is off — *Broadcasting: Nothing*, with the identity block
   relabelled `Config` so no row reads as being on air.

`BleBeacon::broadcasting()` returns `nullptr` when the controller is not
advertising. That null is what separates "configured" from "on air"; the UI
must key off it rather than off the stored setting, which can be `On` while the
radio failed to come up.
