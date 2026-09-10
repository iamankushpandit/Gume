---
name: braino-boards
description: Identify which Braino board is on which serial port, and flash the connected boards with the right firmware for each. Use whenever a task involves flashing, uploading, `pio run -t upload`, `pio device monitor`, testing on hardware, or any question of the form "which board is on COMn" / "flash all the connected boards".
---

# Which board is on which port — never ask

**Do not ask the owner to identify a port.** It has been asked three times in a
single session and it is answerable in about ten seconds. Run this instead:

```bash
python tools/identify_boards.py
```

It prints one line per port: the board, the PlatformIO environment to flash it
with, the firmware version, and `asked` or `reset` -- how it found out. To flash
every connected board correctly, in one step, taking and releasing the board
lock on its own:

```bash
python tools/identify_boards.py --flash
```

If other agents may be testing on the boards, never restart any of them:

```bash
python tools/identify_boards.py --no-reset
```

## Configure the bench — never by hand

To give every connected board the same settings, Wi-Fi, players and per-player
game lists (after a flash, or to put a bench into a known state):

```bash
python tools/configure_boards.py --dry-run
python tools/configure_boards.py
```

It reads `tools/bench_config.json`. That file is **gitignored** because it holds
the owner's Wi-Fi password and player names. If it does not exist, copy
`tools/bench_config.example.json` and ask the owner for the values rather than
inventing them. Never commit it, print it, or paste it anywhere. `--board
E32R40T` or `--port COM12` narrows the run to one model or one port.

It finds boards with `identify`, unlocks each with the admin PIN from the
config, applies `settings` (any key `get` lists), `wifi`, `profiles`,
`rename_profiles`, `remove_profiles` and `games`, reads back `get`, and locks
again. Players are addressed by **name**, never slot -- slots shift when a
player is removed. Boards on firmware older than the console are reported and
skipped. Nothing is reset. A board refusing `unlock` has a different PIN, and
three wrong PINs lock its console out for 30 seconds, so don't retry in a loop.
`remove_profiles` deletes a player's scores and progress for good -- only put a
name there when the owner asked for it.

Taking the board lock is done for you. To send one command by hand, open the
port with DTR/RTS low and type `help`: the board lists its own commands, and
`help <command>` gives usage and whether it needs the PIN. Every reply is one
line, `ok key="v" ...` or `err <code> <message>`.

## Why the port is not the answer

A COM number is assigned by Windows in plug order, so the same four boards were
COM9/10/11/12 in the morning and COM9/10/12/13 in the afternoon. Anything keyed
on the port is stale by the next reboot.

A board identifies itself by its **device id** (`[boot] device=R28T-…`), which
the firmware generates and owns -- never by its MAC, which must not be stored
or printed (see "No identifiers in this repository" in `CLAUDE.md`).
`tools/board_registry.json` (gitignored, local) maps device id → board → env.

## Ask, don't reset

Current firmware answers `identify` (or `identify?`) on the serial port with one
`ok v="1" device=...` line and keeps running, so the tool asks first. Only a board that stays silent --
older firmware, a diag build, a blank flash -- gets reset so its boot banner can
be read. Resetting is not free: it discards whatever the board was doing, one
2.8-inch board's USB drops off the bus as its app starts (so the banner is
lost), and an E32R40T has been seen stuck in a `flash read err` boot loop
after a reset until its battery was pulled.

If you write your own serial script, open the port with DTR and RTS already
low (`s = serial.Serial(); s.dtr = False; s.rts = False; s.port = ...;
s.open()`), or opening it resets the board.

## Rules this must not break

- **COM3 is not a board.** It is a component of the laptop. Never open it, never
  flash it. The registry's `skip_ports` enforces this; keep it there.
- **Take the board lock** before any `pio run`, upload or monitor, and release it
  on every path including failure. `--flash` does both itself; if you flash by
  hand, follow the protocol in `CLAUDE.md` — the lock lives at
  `$(git rev-parse --git-common-dir)/gume-board.lock` so every worktree sees it.
- **Say before you flash a shared board.** Flashing wipes NVS-held touch
  calibration, profiles and scores that another agent may be testing against.
- **Never guess an unidentified board.** The tool refuses to, and so should you.
  Flashing a 4-inch ST7796 with the ILI9341 build gives a dark panel and a
  completely healthy serial log, so a wrong guess is easy to make and slow to
  notice. Flash a candidate build, read `[boot] board=` back, then record it:

  ```bash
  python tools/identify_boards.py --learn
  ```

## The banner's honest limit

`[boot] board=<NAME>` (and the `identify` reply's `board=`) is compiled in, so it
reports which *firmware* is on the
board, not which *panel* is underneath it. It is right whenever the board was
last flashed correctly and confidently wrong when it was not. That is why the
registry keeps a `how` field recording what each identification rested on —
"boot banner" and "confirmed on the panel by the owner" are different levels of
confidence and the file should not hide which one it has.

If the registry and a live banner disagree, the tool prints `!! registry says X,
board says Y` rather than silently preferring either. Investigate; don't paper
over it.
