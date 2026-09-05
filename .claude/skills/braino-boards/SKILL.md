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

It prints one line per port: the board, and the PlatformIO environment to flash
it with. To flash every connected board correctly, in one step, taking and
releasing the board lock on its own:

```bash
python tools/identify_boards.py --flash
```

## Why the port is not the answer

A COM number is assigned by Windows in plug order, so the same four boards were
COM9/10/11/12 in the morning and COM9/10/12/13 in the afternoon. Anything keyed
on the port is stale by the next reboot.

The **MAC** is burned into eFuse: unique per chip, never moves, and readable
from a board with no firmware on it at all — which is exactly the situation
after a failed flash, and the situation where a boot banner tells you nothing.
`tools/board_registry.json` maps MAC → board → env, and is the memory that makes
the question answerable once instead of every time.

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

`[boot] board=<NAME>` is compiled in, so it reports which *firmware* is on the
board, not which *panel* is underneath it. It is right whenever the board was
last flashed correctly and confidently wrong when it was not. That is why the
registry keeps a `how` field recording what each identification rested on —
"boot banner" and "confirmed on the panel by the owner" are different levels of
confidence and the file should not hide which one it has.

If the registry and a live banner disagree, the tool prints `!! registry says X,
board says Y` rather than silently preferring either. Investigate; don't paper
over it.
