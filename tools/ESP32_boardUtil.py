#!/usr/bin/env python3
"""Say which board is on which serial port, without anyone being asked.

    python tools/ESP32_boardUtil.py            # identify everything attached
    python tools/ESP32_boardUtil.py --learn    # ...and record what it found
    python tools/ESP32_boardUtil.py --json     # machine-readable, for scripting
    python tools/ESP32_boardUtil.py --flash    # build each model once, flash all at once
    python tools/ESP32_boardUtil.py --flash --board E32R40T   # ...only that board
    python tools/ESP32_boardUtil.py --no-reset # ask only; never restart a board

Why this exists
---------------
A COM port number is not a property of a board. Windows hands them out in the
order things were plugged in, so the same four boards were COM9/10/11/12 in the
morning and COM9/10/12/13 in the afternoon, and each shuffle turned "flash all
the boards" back into a question for the owner. Asked three times in one
session, which is two times too many.

So the port is treated as an address, not an identity. At each address:

1. **Ask.** Current firmware answers `identify?` with one `ok v="1" ...` line
   -- device id, board, version, build -- and keeps running. Nothing is reset, so
   a game in progress or another agent's test is not disturbed. This is the
   normal path.
2. **Reset and listen**, only if nothing answered: older firmware, a diag
   build, or a board stuck before its app. esptool proves an ESP32 is there
   (it reads the MAC to do so, and that MAC is discarded -- see read_chip()),
   then the boot banner supplies the device id.

Either way the device id is looked up in tools/board_registry.json, and the
board is known.

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
EXAMPLE_REGISTRY = os.path.join(ROOT, "tools", "board_registry.example.json")

BANNER_RE = re.compile(r"\[boot\] board=(\S+)")
# The firmware's own id for itself -- `[boot] device=R28T-9F3A2C71`. THIS, not
# the MAC, is what the registry is keyed on. A MAC is burned into eFuse and
# cannot be changed, so a file mapping MACs to firmware is a permanent list of
# specific devices; this repository shipped exactly that for two releases. See
# CLAUDE.md, "No identifiers in this repository".
DEVICE_RE = re.compile(r"\[boot\] device=(\S+)")
VERSION_RE = re.compile(r"\[boot\] build=.*\bversion=(\S+)")
CHIP_RE = re.compile(r"^Chip is (.+?)(?:\s*\(|\s*$)", re.I | re.M)
# The reply to `identify?` -- one line, every value quoted. Current firmware
# answers in the console's grammar, `ok v="1" device=...`; 5.10.0-SNAPSHOT
# builds from before the console said `[ident] v="1" ...`. Both are accepted,
# because a bench is never all on one build. See
# src/engine/AppRuntimeConsole.cpp.
IDENT_LINE_RE = re.compile(r'^(?:\[ident\]|ok) (v="1".*)$', re.M)
IDENT_FIELD_RE = re.compile(r'(\w+)="([^"]*)"')


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
    """Load the local registry, seeding it from the template on first run.

    tools/board_registry.json is GITIGNORED and must stay that way: it names
    the boards on one person's desk. board_registry.example.json ships in its
    place with placeholder ids, and --learn fills the real one in here.
    """
    if not os.path.exists(REGISTRY):
        with open(EXAMPLE_REGISTRY, encoding="utf-8") as f:
            seed = json.load(f)
        seed["boards"] = {}          # the template's entries are placeholders
        seed["_comment"] = [
            "Local, gitignored, and specific to this machine. Populated by",
            "`python tools/ESP32_boardUtil.py --learn`.",
        ]
        with open(REGISTRY, "w", encoding="utf-8", newline="\n") as f:
            json.dump(seed, f, indent=2, ensure_ascii=False)
            f.write("\n")
        print("Created %s from the template -- run --learn to populate it."
              % os.path.relpath(REGISTRY, ROOT))
    with open(REGISTRY, encoding="utf-8") as f:
        reg = json.load(f)
    # A registry written before 5.10.0 was keyed by MAC. Those keys are exactly
    # what must not be kept, so they are dropped rather than migrated: the
    # boards re-introduce themselves by device id on the next run.
    stale = [k for k in reg.get("boards", {}) if ":" in k]
    if stale:
        for key in stale:
            del reg["boards"][key]
        print("Dropped %d MAC-keyed entr%s from the local registry; run "
              "--learn to re-record them by device id."
              % (len(stale), "y" if len(stale) == 1 else "ies"))
    return reg


def list_ports(py):
    """Serial ports, via pyserial in PlatformIO's own venv."""
    code = ("import json,serial.tools.list_ports as p;"
            "print(json.dumps([[x.device, x.description or '', x.hwid or ''] "
            "for x in p.comports()]))")
    out = subprocess.run([py, "-c", code], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.strip():
        return []
    return json.loads(out.stdout.strip().splitlines()[-1])


def query_board(py, port, seconds=1.5):
    """Ask a running board what it is. A dict of its reply, or None.

    This is the first thing tried on every port because it does not reset
    the board: whatever it was doing -- a game, a test, the screen saver --
    carries on. The port is opened with DTR and RTS already low, because on
    the auto-reset circuit these boards use, opening a port the ordinary way
    pulls EN and resets the board anyway, which would defeat the point.

    None means nobody answered: firmware older than the query, a diag build,
    a blank flash, or a board stuck before its app. The reset path below is
    the fallback for all of those.
    """
    script = (
        "import serial,sys,time\n"
        "s=serial.Serial()\n"
        "s.port=%r; s.baudrate=115200; s.timeout=0.1\n"
        "s.dtr=False; s.rts=False\n"      # set BEFORE open: opening must not reset
        "s.open()\n"
        "s.reset_input_buffer()\n"
        "s.write(b'\\nidentify?\\n'); s.flush()\n"   # leading \\n ends any partial line
        "d=b''; t=time.time()\n"
        "while time.time()-t < %f:\n"
        "    d += s.read(512)\n"
        "    i = max(d.find(b'[ident] '), d.find(b'ok v=\"1\"'))\n"
        "    if i >= 0 and b'\\n' in d[i:]: break\n"
        "s.close(); sys.stdout.write(d.decode('utf-8','replace'))\n"
    ) % (port, seconds)
    out = subprocess.run([py, "-c", script], capture_output=True, text=True)
    line = IDENT_LINE_RE.search((out.stdout or "").replace("\r", ""))
    if not line:
        return None
    fields = dict(IDENT_FIELD_RE.findall(line.group(1)))
    return fields if fields.get("board") else None


def read_banner(py, port, seconds=4.0):
    """Reset the board and catch its banner: (board, device id, version).

    The fallback when query_board() got no answer. A board that is asleep,
    mid-game or running a diag build will not print it, and that is not an
    error. Waits for the build= line, which follows board=, so the version is
    caught too.
    """
    script = (
        "import serial,sys,time\n"
        "s=serial.Serial(%r,115200,timeout=0.2)\n"
        "s.setDTR(False); s.setRTS(True); time.sleep(0.12)\n"
        "s.setRTS(False); time.sleep(0.05)\n"
        "d=b''; t=time.time()\n"
        "while time.time()-t < %f:\n"
        "    d += s.read(512)\n"
        "    i = d.find(b' version=')\n"
        "    if i >= 0 and b'\\n' in d[i:]: break\n"
        "s.close(); sys.stdout.write(d.decode('utf-8','replace'))\n"
    ) % (port, seconds)
    out = subprocess.run([py, "-c", script], capture_output=True, text=True)
    text = out.stdout or ""
    board = BANNER_RE.search(text)
    device = DEVICE_RE.search(text)
    version = VERSION_RE.search(text)
    return (board.group(1) if board else None,
            device.group(1) if device else None,
            version.group(1) if version else None)


def read_chip(py, esptool, port):
    """The chip model, and whether an ESP answered at all.

    THE MAC IS READ BY esptool AND DELIBERATELY DISCARDED HERE. It is the one
    identifier that answers on a board with no firmware, so it stays useful for
    proving something is alive -- but it is never returned, stored or printed,
    because a file or a log carrying it publishes a permanent identifier for
    that board. Identity comes from `[boot] device=`, which the firmware owns.
    """
    if not esptool:
        return None, False
    out = subprocess.run(
        [py, esptool, "--port", port, "--before", "default_reset",
         "--after", "hard_reset", "chip_id"],
        capture_output=True, text=True)
    text = (out.stdout or "") + (out.stderr or "")
    chip = CHIP_RE.search(text)
    alive = "Chip is" in text
    return (chip.group(1).strip() if chip else None, alive)


# What the ROM prints when it cannot boot the app it was just given, over and
# over, every ~350 ms. See recover_rom_loop().
ROM_LOOP_RE = re.compile(r"invalid header|flash read err|RTCWDT_RTC_RESET")
# How long a freshly flashed board gets to boot before it is asked anything.
BOOT_WAIT_S = 5
RECOVERY_ATTEMPTS = 3


def listen(py, port, seconds=2.5):
    """Whatever a board prints on its own, without resetting it."""
    script = (
        "import serial,sys,time\n"
        "s=serial.Serial()\n"
        "s.port=%r; s.baudrate=115200; s.timeout=0.2\n"
        "s.dtr=False; s.rts=False\n"
        "s.open()\n"
        "d=b''; t=time.time()\n"
        "while time.time()-t < %f: d += s.read(512)\n"
        "s.close(); sys.stdout.write(d.decode('utf-8','replace'))\n"
    ) % (port, seconds)
    out = subprocess.run([py, "-c", script], capture_output=True, text=True)
    return out.stdout or ""


def recover_rom_loop(py, esptool, port):
    """Bring a board out of the ROM boot loop without anyone pulling a battery.

    Seen repeatedly on the E32R40T straight after an upload: the reset at the
    end of the flash lands it in `invalid header` / `flash read err` with the
    panel dark, over and over, although the image in flash is intact -- read
    back byte for byte while it looped. What reliably brings it back is a
    reset that comes FROM download mode rather than from a running chip: one
    esptool command that enters the bootloader and stays there, then a second
    that starts from that state and hard-resets. Measured on the bench:

      - the pair works; the second step alone, retried after it once failed,
        never does -- so a failed attempt repeats the whole pair;
      - it writes nothing and erases nothing: flash_id and read_mac only read.

    esptool prints the chip's MAC on every connect. That output is captured
    and dropped unread, like read_chip()'s: a MAC names a board for the life
    of the silicon and must not reach a log.

    True once the board answers `identify?` again.
    """
    if not esptool:
        return False
    for _attempt in range(RECOVERY_ATTEMPTS):
        subprocess.run([py, esptool, "--port", port, "--after", "no_reset",
                        "flash_id"], capture_output=True, text=True)
        subprocess.run([py, esptool, "--port", port, "--before", "no_reset",
                        "--after", "hard_reset", "read_mac"],
                       capture_output=True, text=True)
        time.sleep(BOOT_WAIT_S)
        if query_board(py, port, 3.0):
            return True
    return False


def head_commit():
    out = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"],
                         cwd=ROOT, capture_output=True, text=True)
    return out.stdout.strip() if out.returncode == 0 else ""


def check_boot(py, esptool, flashed):
    """Ask every board just flashed whether it is running the new build.

    An upload that esptool verified says the bytes are in flash; it does not
    say the board booted them. So each one is asked, unreset, after
    BOOT_WAIT_S. A silent board is listened to: if the ROM is looping, it is
    recovered (recover_rom_loop) rather than left for somebody to find dark.
    Returns the ports that are still stuck -- those count as failures. A board
    that is silent for any other reason is reported and not failed: one
    2.8-inch board's USB is known to drop off the bus as its app starts.
    """
    commit = head_commit()
    stuck = []
    print("\n=== checking each board booted it")
    time.sleep(BOOT_WAIT_S)
    for r in flashed:
        port, how = r["port"], ""
        reply = query_board(py, port, 3.0) or query_board(py, port, 3.0)
        if not reply and ROM_LOOP_RE.search(listen(py, port)):
            print("  ... %-6s is in the ROM boot loop -- recovering it" % port)
            if recover_rom_loop(py, esptool, port):
                reply = query_board(py, port, 3.0)
                how = "  (recovered from the ROM boot loop)"
            else:
                stuck.append(port)
                print("  ERR %-6s still looping after %d recoveries -- pull its "
                      "battery and press reset" % (port, RECOVERY_ATTEMPTS))
                continue
        if not reply:
            print("  ??  %-6s did not answer (asleep, or its USB dropped as the "
                  "app started)" % port)
            continue
        build = reply.get("build", "?")
        print("  %s %-6s %-22s %s%s" % ("ok " if commit in build else "!! ",
                                        port, r["board"], build, how))
        if commit and commit not in build:
            print("       expected a build of %s" % commit)
    return stuck


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


def flash_all(py, esptool, results, boards=None):
    """Flash every identified board with its own environment -- in parallel.

    Refuses outright if anything is UNKNOWN. Flashing a board with the wrong
    panel's build is the failure this whole file exists to prevent, and
    "most of them were right" is not a state anybody can act on afterwards.

    `boards` narrows it to the board being worked on: a list of BOARD_NAMEs
    (`E32R40T`) or environments (`app_e32r40t`), case-insensitive. Without it
    a change meant for one board rebuilt and reflashed the whole bench -- four
    full builds side by side, since a new commit changes the build stamp and
    so every object -- to test something only one panel could show. With it,
    an UNKNOWN port elsewhere on the bench is reported rather than refused:
    it cannot be matched, so it cannot be flashed by mistake.

    Two phases, under ONE hold of the board lock:

    1. BUILD each distinct environment once, all at the same time. Boards of
       the same model share a build. Measured after a new commit, four
       environments one after another took 210 s and in parallel 97 s: each
       rebuild is one changed object plus dependency scanning and linking,
       which is mostly single-core, so side by side they barely compete. Each
       environment has its own build directory and its own generated stamp
       header, and the object cache is safe to share between processes.
    2. UPLOAD to every port at once, one process per port, with `-t nobuild`
       so nothing is rebuilt. Each board is its own USB device on its own
       port, so this is safe; one board failing does not stop the others.
    3. CHECK each board booted the new build, and bring any that the upload's
       reset left in the ROM boot loop back out of it -- see check_boot().

    The lock is still global. Parallel flashing is for ONE agent's boards --
    two agents flashing the bench at the same time is exactly what the lock
    exists to stop, and nothing here changes that.
    """
    unknown = [r for r in results if r["status"] in ("unknown", "silent")]
    if unknown and not boards:
        print("Refusing to flash: %d port(s) unidentified (%s)."
              % (len(unknown), ", ".join(r["port"] for r in unknown)))
        return 1
    if unknown:
        print("Not flashing %d unidentified port(s): %s."
              % (len(unknown), ", ".join(r["port"] for r in unknown)))

    targets = [r for r in results
               if r["status"] in ("known", "new", "learned") and r.get("env")]
    if boards:
        wanted = {b.lower() for b in boards}
        targets = [r for r in targets
                   if r["board"].lower() in wanted or r["env"].lower() in wanted]
        if not targets:
            print("No connected board matches --board %s." % ", ".join(boards))
            return 1
    if not targets:
        print("Nothing to flash.")
        return 1

    lock = lock_path()
    if lock and os.path.exists(lock):
        print("Board lock held: %s\n  %s\nCheck whether that PID is alive "
              "before clearing it -- see CLAUDE.md."
              # utf-8-sig: PowerShell's Set-Content writes a byte-order mark,
              # and printing that to a cp1252 console crashed the tool here
              # instead of saying who holds the lock.
              % (lock, open(lock, encoding="utf-8-sig").read().strip()))
        return 1
    if lock:
        with open(lock, "w", encoding="utf-8") as f:
            f.write("%d|flash-all|%s|%s\n"
                    % (os.getpid(), ROOT, time.strftime("%Y-%m-%dT%H:%M:%S")))

    failed = []
    try:
        # A fresh worktree has no .pio/ until its first build, and the logs
        # below go there before any build has run.
        os.makedirs(os.path.join(ROOT, ".pio"), exist_ok=True)
        envs = sorted({r["env"] for r in targets})
        built = set()
        print("\n=== building %d environment(s) at once: %s"
              % (len(envs), ", ".join(envs)))
        t0 = time.time()
        builds = []
        for env in envs:
            log = open(os.path.join(ROOT, ".pio", "build-%s.log" % env),
                       "w", encoding="utf-8", errors="replace")
            builds.append((env, log, subprocess.Popen(
                ["pio", "run", "-e", env], cwd=ROOT, shell=True,
                stdout=log, stderr=subprocess.STDOUT), time.time()))
        for env, log, proc, started in builds:
            rc = proc.wait()
            log.close()
            print("  %s env:%-24s %3ds" % ("ok " if rc == 0 else "ERR", env,
                                           time.time() - started))
            if rc == 0:
                built.add(env)
            else:
                failed += [r["port"] for r in targets if r["env"] == env]
                print("       see %s" % os.path.relpath(log.name, ROOT))
        print("=== builds finished in %ds" % (time.time() - t0))

        uploads = [r for r in targets if r["env"] in built]
        if uploads:
            print("\n=== uploading to %d board(s) at once: %s"
                  % (len(uploads), ", ".join("%s(%s)" % (r["port"], r["env"])
                                             for r in uploads)))
            t0 = time.time()
            procs = []
            for r in uploads:
                log = open(os.path.join(ROOT, ".pio", "upload-%s.log" % r["port"]),
                           "w", encoding="utf-8", errors="replace")
                p = subprocess.Popen(
                    ["pio", "run", "-e", r["env"], "-t", "nobuild", "-t", "upload",
                     "--upload-port", r["port"]],
                    cwd=ROOT, shell=True, stdout=log, stderr=subprocess.STDOUT)
                procs.append((r, p, log))
            for r, p, log in procs:
                rc = p.wait()
                log.close()
                ok = rc == 0 and "Hash of data verified" in open(
                    log.name, encoding="utf-8", errors="replace").read()
                print("  %s %-6s %-22s env:%s" % ("ok " if ok else "ERR", r["port"],
                                                r["board"], r["env"]))
                if not ok:
                    failed.append(r["port"])
                    print("       see %s" % os.path.relpath(log.name, ROOT))
            print("=== uploads finished in %ds" % (time.time() - t0))
            # Still under the lock: recovery resets a board, which is exactly
            # the kind of thing another agent must not be doing at the time.
            flashed = [r for r in uploads if r["port"] not in failed]
            if flashed:
                failed += check_boot(py, esptool, flashed)
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
    """Which environment flashes the board that calls itself `name`.

    The registry is asked first, because a board someone has already identified
    carries whatever correction they made. But the registry alone cannot answer
    for the FIRST board of a model: it could only ever copy an env from another
    entry with the same BOARD_NAME, so a new model stayed permanently
    unlearnable and --flash skipped it every time. That is exactly the case this
    tool exists to remove.

    So fall back to platformio.ini, which is where the answer actually lives:
    find the [board_*] section whose BOARD_NAME matches, then the games
    environment that composes it. Derived rather than restated -- the same rule
    the site generator follows for the same reason.
    """
    for entry in reg["boards"].values():
        if entry["board"] == name:
            return entry["env"]

    ini_path = os.path.join(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), "platformio.ini")
    try:
        with open(ini_path, encoding="utf-8") as handle:
            ini = handle.read()
    except OSError:
        return None

    for section in re.findall(r"^\[board_(\w+)\]", ini, re.M):
        body = re.search(r"^\[board_%s\](.*?)(?=^\[|\Z)" % re.escape(section),
                         ini, re.M | re.S)
        if not body:
            continue
        found = re.search(r'BOARD_NAME=\\"([^\\"]+)\\"', body.group(1))
        if not found or found.group(1) != name:
            continue
        # The games firmware for that board, not a diagnostic env.
        envs = re.findall(
            r"^\[env:(\w+)\](?:(?!^\[).)*?\$\{board_%s\.build_flags\}"
            % re.escape(section), ini, re.M | re.S)
        for env in envs:
            if env == "app" or env.startswith("app_"):
                return env
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--learn", action="store_true",
                    help="record any new device id whose board the banner "
                         "identified")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--flash", action="store_true",
                    help="flash every identified board with its own env")
    ap.add_argument("--board", action="append", metavar="NAME",
                    help="with --flash, flash only boards whose BOARD_NAME or "
                         "env is NAME (e.g. E32R40T or app_e32r40t); repeat "
                         "for more than one")
    ap.add_argument("--no-reset", action="store_true",
                    help="only ask; never reset a board that does not answer")
    args = ap.parse_args()

    py, esptool, reg = pio_python(), esptool_py(), load_registry()
    skip = reg.get("skip_ports", {})
    results, learned = [], 0

    for device, desc, hwid in list_ports(py):
        if device in skip:
            results.append({"port": device, "status": "skipped",
                            "why": skip[device]})
            continue

        # Ask first. A board on current firmware answers without being reset.
        # Twice, because the reply shares the UART with every other task's
        # logging: on the bench a NimBLE scan line landed in the middle of one
        # and it did not parse. A second ask is cheap; a reset is not.
        reply = query_board(py, device) or query_board(py, device)
        if reply:
            banner, device_id = reply.get("board"), reply.get("device")
            version, chip, via = reply.get("version"), reply.get("chip"), "asked"
        elif args.no_reset:
            results.append({"port": device, "status": "silent",
                            "why": "did not answer, and --no-reset says not to "
                                   "reset it", "desc": desc})
            continue
        else:
            chip, alive = read_chip(py, esptool, device)
            if not alive:
                results.append({"port": device, "status": "no-esp",
                                "why": "no ESP32 answered here", "desc": desc})
                continue
            banner, device_id, version = read_banner(py, device)
            via = "reset"
        known = reg["boards"].get(device_id) if device_id else None

        if known:
            row = {"port": device, "status": "known", "device": device_id,
                   "chip": chip, "board": known["board"], "env": known["env"],
                   "version": version, "via": via}
            # The registry is a record, not an authority. If the board itself
            # now says something different, say so rather than papering over it.
            if banner and banner != known["board"]:
                row["conflict"] = ("registry says %s, board says %s"
                                   % (known["board"], banner))
            results.append(row)
        elif banner:
            env = env_for_board_name(reg, banner)
            row = {"port": device, "status": "new", "device": device_id,
                   "chip": chip, "board": banner, "env": env,
                   "version": version, "via": via}
            results.append(row)
            if args.learn and env and device_id:
                how = "asked over serial" if via == "asked" else "boot banner"
                reg["boards"][device_id] = {
                    "board": banner, "env": env,
                    "note": "learned (%s) on %s" % (how, time.strftime("%Y-%m-%d")),
                    "how": how,
                }
                learned += 1
                row["status"] = "learned"
        else:
            # No banner means no device id, so there is nothing to look up.
            # A blank board, a diag build or a sleeping one all land here: the
            # answer is to flash a candidate build and read the banner back,
            # which is what --learn then records.
            results.append({"port": device, "status": "unknown", "chip": chip,
                            "why": "said nothing on reset -- no firmware, a "
                                   "diag build, or asleep"})

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
        # "asked" means the board answered and was left running; "reset"
        # means it was restarted to make it print its banner.
        tail = "%-18s %s" % (r.get("version") or "version ?", r.get("via", ""))
        if s == "known":
            print("  %s  %-20s env:%-24s %s" % (p, r["board"], r["env"], tail))
            if "conflict" in r:
                print("  %s  !! %s" % (" " * width, r["conflict"]))
        elif s == "new":
            print("  %s  %-20s env:%-24s %s   (new -- re-run with --learn)"
                  % (p, r["board"], r["env"] or "?", tail))
        elif s == "learned":
            print("  %s  %-20s env:%-24s %s   (recorded)"
                  % (p, r["board"], r["env"], tail))
        elif s == "unknown":
            print("  %s  UNKNOWN  chip=%s -- %s"
                  % (p, r["chip"] or "?", r.get("why", "did not identify")))
            print("  %s  Not guessing. Flash a candidate build, read the "
                  "banner, then --learn." % (" " * width))
        else:
            print("  %s  -- %s" % (p, r["why"]))

    if learned:
        print("\nRecorded %d new board(s) in tools/board_registry.json." % learned)

    if args.flash:
        return flash_all(py, esptool, results, args.board)
    if args.board:
        print("--board only narrows --flash; nothing was flashed.")

    unknown = sum(1 for r in results if r["status"] in ("unknown", "silent"))
    return 1 if unknown else 0


if __name__ == "__main__":
    sys.exit(main())
