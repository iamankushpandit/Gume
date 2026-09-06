#!/usr/bin/env python3
"""Say which board is on which serial port, without anyone being asked.

    python tools/identify_boards.py            # identify everything attached
    python tools/identify_boards.py --learn    # ...and record what it found
    python tools/identify_boards.py --json     # machine-readable, for scripting
    python tools/identify_boards.py --flash    # build and flash each one correctly

Why this exists
---------------
A COM port number is not a property of a board. Windows hands them out in the
order things were plugged in, so the same four boards were COM9/10/11/12 in the
morning and COM9/10/12/13 in the afternoon, and each shuffle turned "flash all
the boards" back into a question for the owner. Asked three times in one
session, which is two times too many.

The MAC is burned into eFuse. It is unique per chip, it never moves, and
esptool reads it in about two seconds from a board with *no firmware on it at
all* -- which is the one case where reading the boot banner cannot help, and
also the case you are in immediately after a failed flash.

So the port is treated as an address, not an identity: read the MAC at the
address, look the MAC up in tools/board_registry.json, and the board is known.

How a board gets into the registry in the first place
-----------------------------------------------------
Two sources, in order of confidence:

1. **The boot banner.** A board already running Braino prints
   `[boot] board=<BOARD_NAME>`, which is the board's own answer. This is what
   --learn records. Note the honest limit: BOARD_NAME is compiled in, so the
   banner says which firmware is on the board, not which panel is under it. It
   is right whenever the board was last flashed correctly, and confidently
   wrong if it was not -- which is why the registry keeps a `how` field.
2. **The chip type**, which separates families for free: the Freenove is the
   only ESP32-S3 in this set, so an S3 needs no further evidence.

What this deliberately does not do
----------------------------------
It does not guess. A MAC that is not in the registry is reported as UNKNOWN
with the chip type and whatever the banner said, and no environment is
suggested for it. Flashing a 4-inch ST7796 with the ILI9341 build produces a
dark panel and a perfectly healthy serial log, so a wrong guess here is both
easy to make and slow to notice. An honest "I don't know this one" costs one
question; a confident wrong answer costs a debugging session.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGISTRY = os.path.join(ROOT, "tools", "board_registry.json")

BANNER_RE = re.compile(r"\[boot\] board=(\S+)")
MAC_RE = re.compile(r"^MAC:\s*([0-9a-f:]{17})", re.I | re.M)
CHIP_RE = re.compile(r"^Chip is (.+?)(?:\s*\(|\s*$)", re.I | re.M)


def pio_python():
    for c in (os.path.expanduser("~/.platformio/penv/Scripts/python.exe"),
              os.path.expanduser("~/.platformio/penv/bin/python")):
        if os.path.exists(c):
            return c
    return sys.executable


def esptool_py():
    hits = sorted(glob.glob(os.path.expanduser(
        "~/.platformio/packages/tool-esptoolpy*/esptool.py")))
    return hits[-1] if hits else None


def load_registry():
    with open(REGISTRY, encoding="utf-8") as f:
        return json.load(f)


def list_ports(py):
    """Serial ports, via pyserial in PlatformIO's own venv."""
    code = ("import json,serial.tools.list_ports as p;"
            "print(json.dumps([[x.device, x.description or '', x.hwid or ''] "
            "for x in p.comports()]))")
    out = subprocess.run([py, "-c", code], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.strip():
        return []
    return json.loads(out.stdout.strip().splitlines()[-1])


def read_banner(py, port, seconds=4.0):
    """Reset the board and catch its [boot] board= line. None if it says nothing.

    A board that is asleep, mid-game or running a diag build will not print it,
    and that is not an error -- the MAC path does not depend on this.
    """
    script = (
        "import serial,sys,time\n"
        "s=serial.Serial(%r,115200,timeout=0.2)\n"
        "s.setDTR(False); s.setRTS(True); time.sleep(0.12)\n"
        "s.setRTS(False); time.sleep(0.05)\n"
        "d=b''; t=time.time()\n"
        "while time.time()-t < %f:\n"
        "    d += s.read(512)\n"
        "    if b'[boot] board=' in d: break\n"
        "s.close(); sys.stdout.write(d.decode('utf-8','replace'))\n"
    ) % (port, seconds)
    out = subprocess.run([py, "-c", script], capture_output=True, text=True)
    m = BANNER_RE.search(out.stdout or "")
    return m.group(1) if m else None


def read_chip(py, esptool, port):
    """(chip, mac) from eFuse. Works on a blank board; that is the whole point."""
    if not esptool:
        return None, None
    out = subprocess.run(
        [py, esptool, "--port", port, "--before", "default_reset",
         "--after", "hard_reset", "chip_id"],
        capture_output=True, text=True)
    text = (out.stdout or "") + (out.stderr or "")
    mac = MAC_RE.search(text)
    chip = CHIP_RE.search(text)
    return (chip.group(1).strip() if chip else None,
            mac.group(1).lower() if mac else None)


def lock_path():
    """The board lock, in the shared git common dir so every worktree sees it.

    Worktrees isolate source and .pio/. They do not isolate the boards or the
    serial ports, which is what this lock is actually about -- see CLAUDE.md.
    """
    out = subprocess.run(["git", "rev-parse", "--git-common-dir"],
                         cwd=ROOT, capture_output=True, text=True)
    if out.returncode != 0:
        return None
    common = out.stdout.strip()
    if not os.path.isabs(common):
        common = os.path.join(ROOT, common)
    return os.path.join(common, "gume-board.lock")


def flash_all(results):
    """Flash every identified board with its own environment.

    Refuses outright if anything is UNKNOWN. Flashing a board with the wrong
    panel's build is the failure this whole file exists to prevent, and
    "most of them were right" is not a state anybody can act on afterwards.
    """
    unknown = [r for r in results if r["status"] == "unknown"]
    if unknown:
        print("Refusing to flash: %d port(s) unidentified (%s)."
              % (len(unknown), ", ".join(r["port"] for r in unknown)))
        return 1

    targets = [r for r in results if r["status"] in ("known", "new") and r.get("env")]
    if not targets:
        print("Nothing to flash.")
        return 1

    lock = lock_path()
    if lock and os.path.exists(lock):
        print("Board lock held: %s\n  %s\nCheck whether that PID is alive "
              "before clearing it -- see CLAUDE.md."
              % (lock, open(lock, encoding="utf-8").read().strip()))
        return 1
    if lock:
        with open(lock, "w", encoding="utf-8") as f:
            f.write("%d|flash-all|%s|%s\n"
                    % (os.getpid(), ROOT, time.strftime("%Y-%m-%dT%H:%M:%S")))

    failed = []
    try:
        for r in targets:
            print("\n=== %s  %s  env:%s" % (r["port"], r["board"], r["env"]))
            rc = subprocess.run(
                ["pio", "run", "-e", r["env"], "-t", "upload",
                 "--upload-port", r["port"]], cwd=ROOT, shell=True).returncode
            if rc != 0:
                failed.append(r["port"])
    finally:
        # Released on every path, including a failed flash and a Ctrl-C. A
        # lock left behind blocks every later agent, and stale locks here are
        # normal precisely because this step gets interrupted.
        if lock and os.path.exists(lock):
            os.remove(lock)

    if failed:
        print("\nFAILED: %s" % ", ".join(failed))
        return 1
    print("\nAll %d board(s) flashed." % len(targets))
    return 0


def env_for_board_name(reg, name):
    for entry in reg["boards"].values():
        if entry["board"] == name:
            return entry["env"]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--learn", action="store_true",
                    help="record any new MAC whose board the banner identified")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--no-banner", action="store_true",
                    help="MAC lookup only; skip the reset-and-listen step")
    ap.add_argument("--flash", action="store_true",
                    help="flash every identified board with its own env")
    args = ap.parse_args()

    py, esptool, reg = pio_python(), esptool_py(), load_registry()
    skip = reg.get("skip_ports", {})
    results, learned = [], 0

    for device, desc, hwid in list_ports(py):
        if device in skip:
            results.append({"port": device, "status": "skipped",
                            "why": skip[device]})
            continue

        chip, mac = read_chip(py, esptool, device)
        if mac is None:
            results.append({"port": device, "status": "no-esp",
                            "why": "no ESP32 answered here", "desc": desc})
            continue

        known = reg["boards"].get(mac)
        banner = None if args.no_banner else read_banner(py, device)

        if known:
            row = {"port": device, "status": "known", "mac": mac, "chip": chip,
                   "board": known["board"], "env": known["env"]}
            # The registry is a record, not an authority. If the board itself
            # now says something different, say so rather than papering over it.
            if banner and banner != known["board"]:
                row["conflict"] = ("registry says %s, board says %s"
                                   % (known["board"], banner))
            results.append(row)
        elif banner:
            env = env_for_board_name(reg, banner)
            results.append({"port": device, "status": "new", "mac": mac,
                            "chip": chip, "board": banner, "env": env})
            if args.learn and env:
                reg["boards"][mac] = {
                    "board": banner, "env": env,
                    "note": "learned from the boot banner on %s"
                            % time.strftime("%Y-%m-%d"),
                    "how": "boot banner",
                }
                learned += 1
        else:
            results.append({"port": device, "status": "unknown", "mac": mac,
                            "chip": chip})

    if learned:
        with open(REGISTRY, "w", encoding="utf-8") as f:
            json.dump(reg, f, indent=2)
            f.write("\n")

    if args.json:
        print(json.dumps(results, indent=2))
        return 0

    width = max([len(r["port"]) for r in results] + [4])
    for r in results:
        p = r["port"].ljust(width)
        s = r["status"]
        if s == "known":
            print("  %s  %-20s env:%s" % (p, r["board"], r["env"]))
            if "conflict" in r:
                print("  %s  !! %s" % (" " * width, r["conflict"]))
        elif s == "new":
            print("  %s  %-20s env:%s   (new -- re-run with --learn)"
                  % (p, r["board"], r["env"] or "?"))
        elif s == "unknown":
            print("  %s  UNKNOWN  chip=%s mac=%s" % (p, r["chip"], r["mac"]))
            print("  %s  Not guessing. Flash a build, read the banner, "
                  "then --learn." % (" " * width))
        else:
            print("  %s  -- %s" % (p, r["why"]))

    if learned:
        print("\nRecorded %d new board(s) in tools/board_registry.json." % learned)

    if args.flash:
        return flash_all(results)

    unknown = sum(1 for r in results if r["status"] == "unknown")
    return 1 if unknown else 0


if __name__ == "__main__":
    sys.exit(main())
