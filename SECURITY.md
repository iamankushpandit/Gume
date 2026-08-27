# Security Policy

Braino! runs on hardware that children hold, so a few of the things below are
stated more bluntly than a firmware project usually bothers with.

## Reporting a vulnerability

**Please do not open a public issue for a security problem.** Use either:

1. **GitHub private vulnerability reporting** — the *Report a vulnerability*
   button under this repository's **Security** tab. This is the preferred
   route: it keeps the report private until there is a fix, and it keeps the
   whole exchange attached to the repository.
2. **Email** — esp32.gume@proton.me. Use this if you would rather not have
   a GitHub account involved.

A useful report says what you did, what happened, and what you expected. If it
is board-specific, say which board and which firmware build — the `[boot]`
lines on the serial console name the branch, commit and build time, and pasting
them saves a round trip. Proof-of-concept code is welcome and never required.

This is a single-maintainer hobby project, so set your expectations
accordingly: you should get an acknowledgement within about a week. There is no
bounty, and there is no embargo clock being run against you — if a fix takes a
while, that is capacity, not stalling. You are welcome to disclose publicly
once a fix ships or after 90 days, whichever comes first.

## Supported versions

Only the latest release on `main` is supported. Braino! has no update
mechanism: fixes reach a device when somebody reflashes it, from
[the web installer](https://iamankushpandit.github.io/Gume/) or a toolchain.
There is no way to push a fix to a board in the field, which is worth knowing
before you weigh how serious something is.

## What is in scope

- Anything that lets a remote party reach the device. The attack surface is
  small and enumerated: Wi-Fi association, an NTP exchange, one `ip-api.com`
  HTTP request, and the BLE advertisement. See
  [docs/BLE_BEACON_SPEC.md](docs/BLE_BEACON_SPEC.md).
- Anything that causes the device to transmit data it is not supposed to. The
  complete list of what leaves the device is in
  [CONTRIBUTING.md](CONTRIBUTING.md#no-data-collection), and a way to make the
  firmware exceed that list is a security bug in this project even when it is
  harmless by ordinary standards. That list is the product's central promise.
- Memory-safety bugs reachable from any of the above, or from data on an SD
  card.
- Anything that extracts stored Wi-Fi credentials, profile names or scores off
  the device by a route other than physically holding it.

## What is not in scope

Stated plainly, because two of these look like vulnerabilities and are not:

- **The admin PIN is a parental control, not a security boundary.** It is four
  digits, it is stored in plaintext NVS, and anyone with the board and a USB
  cable can read or erase it. It exists to stop a seven-year-old unhiding a
  game, and it is documented that way in the README. A PIN bypass is a bug
  report, not a security report.
- **The lock screen is an accidental-touch guard**, not access control. It is
  deliberately disjoint from the PIN and grants nothing.
- **Physical access is total access.** Anyone holding the board can reflash it,
  dump NVS, or read the serial console. There is no secure boot and no flash
  encryption, and enabling them is out of scope for a device with no secrets
  worth that complexity.
- Anything that requires modifying the firmware first.
- Denial of service by holding the device, jamming its radios, or unplugging
  it.

## Hardening that already exists, so you know what you are looking at

- **No dynamic allocation.** There is no `new`, `delete`, `malloc` or `free` in
  the firmware; a check (`tools/check_frame_rules.py`) fails the build if one
  appears. Every screen is a static instance.
- **A watchdog** reboots the device if the loop stalls past 12 seconds.
- **The BLE advertisement has exactly one encoder and one decoder**, and the
  device shows the raw bytes it is transmitting on its own System Info screen.
- **Network names never reach the serial console.** The saved SSID is
  deliberately absent from every `Serial` write in the product firmware,
  because a serial log is the artifact of this device most likely to be pasted
  into a public bug report.
