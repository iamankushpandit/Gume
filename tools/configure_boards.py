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
      "settings": {"theme": "Midnight", "brightness": 70, "beacon": true,
                   "nearby": true, "timezone": "US Central"},
      "wifi": {"ssid": "YOUR-NETWORK", "password": "YOUR-PASSWORD"},
      "profiles": ["Player1", "Player2"],
      "rename_profiles": {"Player 3": "Player3"},
      "remove_profiles": ["Old player"],
      "games": {"all": {"chess": false}, "Player1": {"maze": true}}
    }

Every key except "pin" is optional; a key left out is left alone.

  settings         any key `get` lists on the board; true/false mean on/off
  wifi             {"ssid", "password"}, or "clear" to forget the network
  profiles         players that must exist; one already there is skipped
  rename_profiles  {"current name": "new name"}
  remove_profiles  players to delete, with their scores and progress. The
                   admin and the active player are refused by the board.
  games            {"all" or a player name: {"game-id": true/false}}

Players are addressed by NAME, never by slot: slots shift when a player is
removed, and a slot number that was right on one board is wrong on the next.
The tool reads `profiles` on each board and works the slots out.

How it works
------------
Boards are found by asking `identify`, never by resetting -- and each is
configured in one quiet serial session: the port is opened with DTR and RTS
already low, so opening it does not reset the board. The firmware side is the
command table in src/engine/AppRuntimeConsole*.cpp (`help` on the device lists
it). Anything that changes a board, and reading player names, needs `unlock
<admin PIN>` first, exactly as the Settings screen needs the admin profile.
Replies are one line each, `ok key="v" ...` or `err <code> <message>`. Three
wrong PINs lock the console out for 30 seconds, so a refused PIN stops that
board rather than retrying.

Nothing secret is printed: the PIN and the password are masked even with
--dry-run, and player names are shown only as a count.
"""

import argparse
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ESP32_boardUtil as ib  # noqa: E402  (reuse discovery, registry, lock)

DEFAULT_CONFIG = os.path.join(HERE, "bench_config.json")
EXAMPLE_CONFIG = os.path.join(HERE, "bench_config.example.json")
REPLY_TIMEOUT_S = 3.0
KNOWN_KEYS = {"pin", "settings", "wifi", "profiles", "rename_profiles",
              "remove_profiles", "games"}
FIELD_RE = re.compile(r'(\w+)="([^"]*)"')


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
    value = str(value)
    if '"' in value:
        raise ValueError("a value cannot contain a double quote: %r" % value)
    return '"%s"' % value


def setting_value(v):
    if isinstance(v, bool):
        return "on" if v else "off"
    return quote(v) if " " in str(v) else str(v)


def validate(cfg):
    unknown = {k for k in cfg if not k.startswith("_")} - KNOWN_KEYS
    if unknown:
        raise ValueError("unknown config key(s): %s" % ", ".join(sorted(unknown)))
    pin = str(cfg.get("pin", ""))
    if len(pin) != 4 or not pin.isdigit():
        raise ValueError('"pin" must be the four-digit admin PIN, as a string')
    for name in list(cfg.get("profiles", [])) + list(cfg.get("remove_profiles", [])):
        quote(name)
    for old, new in cfg.get("rename_profiles", {}).items():
        quote(old), quote(new)
    return pin


def shown(cmd):
    """The command as it may be printed: never a password or a PIN."""
    if cmd.startswith("wifi ") and cmd != "wifi clear":
        return 'wifi "<name>" "<password>"'
    if cmd.startswith("unlock "):
        return "unlock ****"
    if cmd.startswith("profile-") or cmd.startswith("game "):
        return re.sub(r'"[^"]*"', '"<name>"', cmd)
    return cmd


def shown_reply(cmd, reply):
    """Replies carrying names are summarised rather than printed."""
    if reply and cmd == "profiles" and reply.startswith("ok "):
        return 'ok (%s players)' % FIELD_RE.search(reply).group(2)
    if reply and cmd.startswith("profile-rename") and reply.startswith("ok "):
        return re.sub(r'name="[^"]*"', 'name="<name>"', reply)
    return reply or "(no reply)"


class Session:
    """One quiet serial session on one board."""

    def __init__(self, port):
        import serial
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = 115200
        self.s.timeout = 0.1
        self.s.dtr = False     # set BEFORE open: opening must not reset the board
        self.s.rts = False
        self.s.open()
        self.failures = 0

    def send(self, cmd, quiet=False):
        """Send one command; return its reply, skipping unrelated log lines.
        Every command answers with exactly one `ok ...` or `err ...` line."""
        self.s.reset_input_buffer()
        self.s.write(b"\n" + cmd.encode("ascii") + b"\n")
        self.s.flush()
        reply, buf = None, b""
        deadline = time.time() + REPLY_TIMEOUT_S
        while reply is None and time.time() < deadline:
            buf += self.s.read(256)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").strip()
                if text.startswith(("ok ", "err ")):
                    reply = text
                    break
        ok = reply is not None and reply.startswith("ok ")
        if not ok:
            self.failures += 1
        if not quiet or not ok:
            print("  %s %-34s %s" % ("ok " if ok else "ERR", shown(cmd),
                                      shown_reply(cmd, reply)))
        return reply if ok else None

    def players(self):
        """{lowercase name: slot} from `profiles`."""
        reply = self.send("profiles", quiet=True)
        found = {}
        for key, val in FIELD_RE.findall(reply or ""):
            if re.fullmatch(r"p\d+", key):
                found[val.lower()] = int(key[1:])
        return found

    def close(self):
        try:
            self.send("lock", quiet=True)
        finally:
            self.s.close()


def configure(port, cfg, pin):
    sess = Session(port)
    try:
        if not sess.send("unlock " + pin):
            return sess.failures          # nothing else will be accepted
        for key, val in cfg.get("settings", {}).items():
            sess.send("set %s %s" % (key, setting_value(val)))
        if "wifi" in cfg:
            w = cfg["wifi"]
            if w in ("clear", None):
                sess.send("wifi clear")
            else:
                sess.send("wifi %s %s" % (quote(w["ssid"]), quote(w.get("password", ""))))
        for name in cfg.get("profiles", []):
            sess.send("profile-add %s" % quote(name))
        for old, new in cfg.get("rename_profiles", {}).items():
            slot = sess.players().get(str(old).lower())
            if slot is None:
                print("  --  rename: no player by that name here -- skipped")
                continue
            sess.send("profile-rename %d %s" % (slot, quote(new)))
        for name in cfg.get("remove_profiles", []):
            # Re-read every time: removing a player shifts the later slots.
            slot = sess.players().get(str(name).lower())
            if slot is None:
                print("  --  remove: no player by that name here -- skipped")
                continue
            sess.send("profile-remove %d" % slot)
        for who, games in cfg.get("games", {}).items():
            if str(who).lower() == "all":
                target = "all"
            else:
                slot = sess.players().get(str(who).lower())
                if slot is None:
                    print("  --  games: no player by that name here -- skipped")
                    continue
                target = str(slot)
            for game_id, visible in games.items():
                sess.send("game %s %s %s" % (target, game_id, "on" if visible else "off"))
        sess.send("get")
    finally:
        sess.close()
    return sess.failures


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
        pin = validate(cfg)
    except (ValueError, KeyError, TypeError) as e:
        sys.exit("Bad config: %s" % e)

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
            print("  %-6s -- did not answer identify (older firmware?) -- skipped" % port)
            continue
        if args.board and reply.get("board") != args.board:
            continue
        targets.append((port, reply))

    if not targets:
        print("No boards to configure.")
        return 1
    if args.dry_run:
        print("Would configure (settings: %s; %d player(s) to ensure, %d to rename, "
              "%d to remove; wifi: %s):"
              % (", ".join(cfg.get("settings", {})) or "none",
                 len(cfg.get("profiles", [])), len(cfg.get("rename_profiles", {})),
                 len(cfg.get("remove_profiles", [])),
                 "clear" if cfg.get("wifi") == "clear" else ("set" if "wifi" in cfg else "unchanged")))
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
            failures += configure(port, cfg, pin)
    finally:
        if lock and os.path.exists(lock):
            os.remove(lock)

    print("\n%s" % ("All boards configured." if not failures
                    else "%d command(s) failed." % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
