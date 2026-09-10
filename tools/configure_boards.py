#!/usr/bin/env python3
"""Set up every connected board from one config file, over the serial console.

    python tools/configure_boards.py                      # every board that answers
    python tools/configure_boards.py --board E32R40T      # only boards of one model
    python tools/configure_boards.py --port COM12         # only one port
    python tools/configure_boards.py --config other.json  # a different config
    python tools/configure_boards.py --dry-run            # show, send nothing

The config is tools/bench_config.json -- GITIGNORED, because it holds a Wi-Fi
password and player names. Copy tools/bench_config.example.json to start:

    {
      "pin": "0000",
      "theme": "Midnight",
      "brightness": 70,
      "beacon": true,
      "nearby": true,
      "wifi": {"ssid": "YOUR-NETWORK", "password": "YOUR-PASSWORD"},
      "profiles": ["Player1", "Player2"]
    }

Every key except "pin" is optional; leave one out and that setting is not
touched. "wifi": "clear" forgets the network. Profiles that already exist are
skipped, so running this twice is harmless.

How it works
------------
Boards are found the way identify_boards.py finds them -- by asking
`identify?`, never by resetting -- and each is configured in one quiet serial
session: the port is opened with DTR and RTS already low, so opening it does
not reset the board. The firmware side is the command table in
src/engine/AppRuntimeConsole.cpp (`help` on the device lists it); every
command that changes something needs `unlock <admin PIN>` first, exactly as
the Settings screen needs the admin profile. Replies are one line each,
`ok key="v" ...` or `err <code> <message>`. Three wrong PINs lock the console
out for 30 seconds.

Nothing personal is printed here: the password is never shown, even with
--dry-run, and the firmware's replies carry counts and flags, not names.
"""

import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import identify_boards as ib  # noqa: E402  (reuse discovery, registry, lock)

DEFAULT_CONFIG = os.path.join(HERE, "bench_config.json")
EXAMPLE_CONFIG = os.path.join(HERE, "bench_config.example.json")
REPLY_TIMEOUT_S = 3.0
KNOWN_KEYS = {"pin", "theme", "brightness", "beacon", "nearby", "wifi", "profiles"}


def ensure_pyserial():
    """Re-run under PlatformIO's Python if this one has no pyserial."""
    try:
        import serial  # noqa: F401
        return
    except ImportError:
        pass
    py = ib.pio_python()
    if os.path.abspath(py) == os.path.abspath(sys.executable):
        sys.exit("pyserial is not installed (pip install pyserial)")
    os.execv(py, [py] + sys.argv)


def quote(value):
    if '"' in value:
        raise ValueError("a value cannot contain a double quote: %r" % value)
    return '"%s"' % value


def onoff(v):
    return "on" if v else "off"


def build_commands(cfg):
    """(command, label) pairs, in the order they must be sent."""
    unknown = {k for k in cfg if not k.startswith("_")} - KNOWN_KEYS
    if unknown:
        raise ValueError("unknown config key(s): %s" % ", ".join(sorted(unknown)))
    pin = str(cfg.get("pin", ""))
    if len(pin) != 4 or not pin.isdigit():
        raise ValueError('"pin" must be the four-digit admin PIN, as a string')

    cmds = [("unlock %s" % pin, "unlock")]
    if "theme" in cfg:
        cmds.append(("theme %s" % cfg["theme"], "theme"))
    if "brightness" in cfg:
        cmds.append(("brightness %d" % int(cfg["brightness"]), "brightness"))
    # Beacon before Nearby: the firmware refuses Nearby while the beacon is off.
    if "beacon" in cfg:
        cmds.append(("beacon %s" % onoff(cfg["beacon"]), "beacon"))
    if "nearby" in cfg:
        cmds.append(("nearby %s" % onoff(cfg["nearby"]), "nearby"))
    if "wifi" in cfg:
        w = cfg["wifi"]
        if w == "clear" or w is None:
            cmds.append(("wifi clear", "wifi"))
        else:
            cmds.append(("wifi %s %s" % (quote(w["ssid"]), quote(w.get("password", ""))),
                         "wifi"))
    for name in cfg.get("profiles", []):
        cmds.append(("profile-add %s" % quote(name), "profile"))
    cmds.append(("settings", "settings"))
    cmds.append(("lock", "lock"))
    return cmds


def shown(cmd):
    """The command as it may be printed: never a password or a PIN."""
    if cmd.startswith("wifi ") and cmd != "wifi clear":
        return 'wifi "<name>" "<password>"'
    if cmd.startswith("unlock "):
        return "unlock ****"
    return cmd


def send(port_handle, cmd):
    """Send one command; return its reply line, skipping unrelated log lines.

    Every command answers with exactly one `ok key="v" ...` or
    `err <code> <message>` line; log lines never start with either."""
    port_handle.reset_input_buffer()
    port_handle.write(b"\n" + cmd.encode("ascii") + b"\n")
    port_handle.flush()
    want = ("ok ", "err ")
    buf = b""
    deadline = time.time() + REPLY_TIMEOUT_S
    while time.time() < deadline:
        buf += port_handle.read(256)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").strip()
            if text.startswith(want):
                return text
    return None


def configure(port, cmds):
    import serial
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.1
    s.dtr = False          # set BEFORE open: opening must not reset the board
    s.rts = False
    s.open()
    results = []
    try:
        for cmd, label in cmds:
            reply = send(s, cmd)
            results.append((label, cmd, reply))
            if label == "unlock" and not (reply or "").startswith("ok "):
                break      # nothing else will be accepted
    finally:
        try:
            if results and results[-1][0] != "lock":
                send(s, "lock")
        finally:
            s.close()
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--config", default=DEFAULT_CONFIG)
    ap.add_argument("--port", action="append", help="only this port (repeatable)")
    ap.add_argument("--board", help="only boards reporting this board name")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.config):
        sys.exit("No config at %s -- copy %s and fill it in. It is gitignored."
                 % (os.path.relpath(args.config), os.path.relpath(EXAMPLE_CONFIG)))
    with open(args.config, encoding="utf-8") as f:
        cfg = json.load(f)
    try:
        cmds = build_commands(cfg)
    except (ValueError, KeyError, TypeError) as e:
        sys.exit("Bad config: %s" % e)

    if args.dry_run:
        print("Would send, to each board:")
        for cmd, _ in cmds:
            print("  " + shown(cmd))

    ensure_pyserial()
    py = ib.pio_python()
    reg = ib.load_registry()
    skip = reg.get("skip_ports", {})

    targets = []
    for port, _desc, _hwid in ib.list_ports(py):
        if port in skip or (args.port and port not in args.port):
            continue
        reply = ib.query_board(py, port) or ib.query_board(py, port)
        if not reply:
            print("  %-6s -- did not answer identify? (older firmware?) -- skipped" % port)
            continue
        if args.board and reply.get("board") != args.board:
            continue
        targets.append((port, reply))

    if not targets:
        print("No boards to configure.")
        return 1
    if args.dry_run:
        for port, reply in targets:
            print("  %-6s %s %s" % (port, reply.get("board"), reply.get("version")))
        return 0

    lock = ib.lock_path()
    if lock and os.path.exists(lock):
        print("Board lock held: %s\n  %s\nCheck whether that PID is alive before "
              "clearing it -- see CLAUDE.md." % (lock, open(lock, encoding="utf-8").read().strip()))
        return 1
    if lock:
        with open(lock, "w", encoding="utf-8") as f:
            f.write("%d|configure|%s|%s\n" % (os.getpid(), ib.ROOT,
                                               time.strftime("%Y-%m-%dT%H:%M:%S")))
    failures = 0
    try:
        for port, reply in targets:
            print("\n=== %s  %s  %s" % (port, reply.get("board"), reply.get("device")))
            for label, cmd, answer in configure(port, cmds):
                ok = answer is not None and answer.startswith("ok ")
                failures += 0 if ok else 1
                print("  %s %-24s %s" % ("ok " if ok else "ERR", shown(cmd),
                                         answer or "(no reply)"))
    finally:
        if lock and os.path.exists(lock):
            os.remove(lock)

    print("\n%s" % ("All boards configured." if not failures
                    else "%d command(s) failed." % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
