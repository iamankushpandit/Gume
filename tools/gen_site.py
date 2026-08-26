#!/usr/bin/env python3
"""Generate the GitHub Pages site, including the web installer.

The page is *derived*, for the same reason the About app is: a hand-written
copy of the game list fell six games behind once already, and a landing page is
even easier to forget than a screen you look at while holding the device. So
nothing here is typed twice -- the version comes from AppVersion.h, the games
and their blurbs from the playable app registry, the build figures from
README.md, the board name and the firmware environments from platformio.ini.

    python tools/gen_site.py [--out DIR] [--repo URL]

Writes, into DIR (default `site/_build`, which is not committed):

    index.html                             the page, from site/index.template.html
    firmware/<board>/<env>/manifest.json   an esp-web-tools manifest per build
    screens/*.png                          a copy of docs/screens

The .bin files each manifest points at are *not* produced here -- CI builds
them with `pio run` and drops them next to their manifest. Run this locally to
check the page renders; the flash button will 404 until the binaries exist.
"""

import argparse
import datetime
import json
import os
import re
import shutil
import sys

from app_registry_parser import playable_apps, system_apps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Offsets are the standard Arduino-ESP32 layout, and match what `pio run
# -t upload` sends. huge_app.csv moves the partition *contents*, not these.
PARTS = (
    ("bootloader.bin", 0x1000),
    ("partitions.bin", 0x8000),
    ("boot_app0.bin",  0xE000),
    ("firmware.bin",   0x10000),
)

# One entry per PlatformIO environment offered on the page. `label` is what the
# picker shows; `note` is the sentence under it, and has to be honest about
# what the diagnostic builds do -- they replace the games entirely.
# Only the console is offered here.
#
# The bringup and wifidiag environments still exist, and CI still builds them --
# they are how you tell a wiring fault from a software one on a fresh board.
# They are not on this page. Everyone arriving at it wants the games, and a
# picker whose other two entries erase the console and replace it with a serial
# test is a trap for exactly the audience least able to recover from it.
# Diagnostics belong with the people who build from source; README covers them.
#
# Adding an entry here is a commitment that .github/workflows/pages.yml builds
# that environment -- check_docs.py enforces it, because a manifest pointing at
# binaries CI never produced fails halfway through erasing somebody's board.
VARIANTS = (
    {
        "env": "app",
        "label": "Braino! (the games)",
        "name": "Braino!",
        "note": "The full console: {count} games, profiles, scores, settings, "
                "Wi-Fi clock and the BLE beacon.",
    },
)

# The pin map, screen rotation and touch controller are compile-time constants,
# so a build is only meaningful on the board it was compiled for -- the picker
# offers one firmware per supported board.
#
# The board LIST is not written here. It is read out of platformio.ini's
# [board_*] sections by boards(), because "supported" and "offered on the page"
# have to be the same set: a board someone can build is a board someone should
# be able to flash from the browser, and a hand-kept list here is exactly how
# the two drift. What cannot be derived -- the human label and the chip family
# the flasher needs -- is looked up below, and a board missing an entry stops
# the site from generating rather than being quietly dropped.
BOARD_DETAILS = {
    "e32r28t1": {
        "label": "E32R28T-1 / ESP32-32E -- 2.8 inch ILI9341 + XPT2046 (resistive)",
        "chip": "ESP32",
        "buy": "https://www.amazon.com/dp/B0D92C9MMH"
               "?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1",
    },
}

SCREEN_CAPTIONS = {
    "about-radios": "About: what the radios do",
    "calendar": "Calendar",
    "cinnamon": "Cinnamon Says",
    "colormix": "Color Mix",
    "counting": "Counting",
    "elements": "Elements: the periodic table",
    "elements-card": "Elements: one element up close",
    "elements-quiz": "Elements: find it in the table",
    "fingers-count": "Fingers: count them",
    "fingers-show": "Fingers: show me N",
    "flags-capital": "Flags: capital bonus",
    "flags-country": "Flags: name the country",
    "fractions": "Fractions",
    "grewords": "GRE Words: quiz",
    "grewords-study": "GRE Words: study",
    "launcher-tall": "Launcher: Tall layout",
    "launcher-wide": "Launcher: Wide layout",
    "math": "Math",
    "maze": "Maze",
    "memory": "Memory",
    "microku": "Microku",
    "money": "Money",
    "multiply": "Multiply",
    "network-time": "Wi-Fi and clock",
    "numberline": "Number Line",
    "nearby": "Nearby",
    "oddone": "Odd One",
    "percent": "Percent Circle",
    "profiles": "Profiles",
    "scores-device": "Scores: device best",
    "scores-mine": "Scores: this player",
    "screensaver": "Screen saver",
    "wakelock": "Hold to unlock",
    "settings-device": "Settings: device",
    "profiles-games": "Profiles: per-player games",
    "settings-power": "Settings: power",
    "shapearith": "Shape Arith",
    "shapes": "Shapes",
    "slide": "Slide",
    "sorting": "Sorting",
    "stateflags": "State Flags",
    "statemaps": "State Maps",
    "states": "US States",
    "systeminfo-ble": "System Info: BLE",
    "systeminfo-memory": "System Info: memory",
    "tictactoe": "Tic-Tac-Toe",
    "time": "Time",
    "timezone": "Time zone picker",
    "trace": "Trace: uppercase and digits",
    "trace-lower": "Trace: lowercase",
    "whack": "Whack",
}

PLAYABLE_STILLS = {
    "tictactoe": ("tictactoe",),
    "memory": ("memory",),
    "math": ("math",),
    "multiply": ("multiply",),
    "time": ("time",),
    "whack": ("whack",),
    "cinnamon": ("cinnamon",),
    "microku": ("microku",),
    "shapecolor": ("shapes",),
    "counting": ("counting",),
    "money": ("money",),
    "fractions": ("fractions",),
    "maze": ("maze",),
    "sort": ("sorting",),
    "colormix": ("colormix",),
    "slide": ("slide",),
    "oddone": ("oddone",),
    "shapearith": ("shapearith",),
    "fingers": ("fingers-count", "fingers-show"),
    "calendar": ("calendar",),
    "numberline": ("numberline",),
    "flags": ("flags-country", "flags-capital"),
    "states": ("states",),
    "trace": ("trace", "trace-lower"),
    "stateflags": ("stateflags",),
    "statemaps": ("statemaps",),
    "percent": ("percent",),
    "grewords": ("grewords", "grewords-study"),
    "dice": ("dice",),
    "coinflip": ("coinflip",),
    "elements": ("elements", "elements-card", "elements-quiz"),
}

SYSTEM_SHOWCASE = (
    {
        "id": "launcher",
        "title": "Launcher",
        "subtitle": "Wide and Tall home layouts with live profile, clock, Wi-Fi, BLE and battery status.",
        "stills": ("launcher-wide", "launcher-tall"),
    },
    {
        "id": "profiles",
        "subtitle": "Boot-time player choice, five players, Guest mode that deliberately saves nothing, "
                    "a PIN-guarded admin profile, and per-player game visibility only the admin can set.",
        "stills": ("profiles", "profiles-pin", "profiles-games"),
    },
    {
        "id": "settings",
        "subtitle": "Device controls, brightness, screen saver and sleep policy -- "
                    "readable by anyone, changeable only by the admin.",
        "stills": ("settings-device", "settings-power",
                   "settings-admin", "settings-pin"),
    },
    {
        "id": "wifi",
        "subtitle": "Optional Wi-Fi for NTP time only, with a timezone picker for daylight-saving rules.",
        "stills": ("network-time", "timezone"),
    },
    {
        "id": "scores",
        "subtitle": "Per-player best and worst scores, plus device-wide records and record holders.",
        "stills": ("scores-mine", "scores-device"),
    },
    {
        "id": "systeminfo",
        "subtitle": "Live board, memory, network, BLE, NVS and watchdog diagnostics for hardware triage.",
        "stills": ("systeminfo-memory", "systeminfo-ble"),
    },
    {
        "id": "about",
        "subtitle": "Parent-readable documentation on the device, including exactly what the radios broadcast.",
        "stills": ("about-radios",),
    },
    {
        "id": "nearby",
        "subtitle": "Opt-in, anonymous score sharing with other Brainos in range -- device tags only, never names.",
        "stills": ("nearby",),
    },
    {
        "id": "screensaver",
        "title": "Screen Saver",
        "subtitle": "Self-playing Pong before panel sleep, with rally colour mirrored on the case LED -- "
                    "and a press-and-hold unlock on the way back, so a stray press in a bag "
                    "cannot land on a live screen.",
        "stills": ("screensaver", "wakelock"),
    },
)

HERO_STILLS = (
    "launcher-wide", "counting", "flags-country", "shapearith", "trace",
    "grewords", "maze", "settings-power", "profiles", "scores-device",
    "systeminfo-memory", "screensaver",
)


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as handle:
        return handle.read()


def die(message):
    sys.stderr.write("gen_site.py: %s\n" % message)
    raise SystemExit(1)


def find(pattern, text, what, flags=0):
    match = re.search(pattern, text, flags)
    if not match:
        die("could not read %s -- the generator needs updating" % what)
    return match


def version():
    return find(r'BRAINO_VERSION\s+"([^"]+)"',
                read("include", "AppVersion.h"), "the version").group(1)


def boards():
    """Every supported board, read out of platformio.ini's [board_*] sections.

    A board is supported when it has a section and an environment building the
    games firmware for it. Both are facts about platformio.ini, so both are
    read from it -- the page cannot offer a board that does not build, and
    cannot omit one that does.
    """
    ini = read("platformio.ini")
    offered = {entry["env"] for entry in VARIANTS}

    found = []
    for section in re.findall(r"^\[board_(\w+)\]", ini, re.M):
        body = re.search(r"^\[board_%s\](.*?)(?=^\[|\Z)" % re.escape(section),
                         ini, re.M | re.S).group(1)
        name = find(r'BOARD_NAME=\\"([^\\"]+)\\"', body,
                    "BOARD_NAME in [board_%s]" % section).group(1)

        envs = tuple(env for env in re.findall(
            r"^\[env:(\w+)\](?:(?!^\[).)*?\$\{board_%s\.build_flags\}"
            % re.escape(section), ini, re.M | re.S) if env in offered)
        if not envs:
            die("platformio.ini declares [board_%s] but no environment builds "
                "the games firmware for it, so the site cannot offer it. A "
                "board that can be supported must be flashable from the page "
                "-- see docs/PORTING.md." % section)

        details = BOARD_DETAILS.get(section)
        if not details:
            die("board '%s' has no BOARD_DETAILS entry in tools/gen_site.py, "
                "so the picker has no label for it. Add one." % section)

        found.append({"id": section, "name": name, "envs": envs, **details})

    if not found:
        die("platformio.ini declares no [board_*] section")
    if len(found) > 1:
        die("more than one board is supported, but site/index.template.html "
            "still describes a single board in prose ({{BOARD}}, "
            "{{BOARD_BUY_URL}}). Generalise that copy before adding the "
            "second board -- offering a firmware the page then mislabels is "
            "worse than not offering it.")
    return found


def games():
    """(title, blurb) for every playable game, in launcher order."""
    apps = playable_apps()
    if not apps:
        die("parsed no playable apps out of AppRegistry.cpp")
    return [(app.title, app.blurb) for app in apps]


def build_figures():
    """The flash and RAM lines from README's headline table.

    They cannot be derived without running a build, and check_docs.py already
    fails if README and CLAUDE.md disagree, so README is the one source.
    """
    readme = read("README.md")
    flash = find(r"\|\s*Flash\s*\|\s*([^|]+?)\s*\|", readme, "the flash figure")
    ram = find(r"\|\s*RAM\s*\|\s*([^|]+?)\s*\|", readme, "the RAM figure")

    def tidy(text):
        text = text.replace("**", "").replace(" bytes", "")
        return re.sub(r"\s+", " ", text).strip()

    return tidy(flash.group(1)), tidy(ram.group(1))


def escape(text):
    return (text.replace("&", "&amp;").replace("<", "&lt;")
                .replace(">", "&gt;").replace('"', "&quot;"))


def screen_file(name):
    return "screens/%s.png" % name


def available_screen_names():
    directory = os.path.join(ROOT, "docs", "screens")
    return {
        name[:-4]
        for name in os.listdir(directory)
        if name.endswith(".png") and os.path.isfile(os.path.join(directory, name))
    }


def still_figure(name, class_name="still", loading="eager"):
    caption = SCREEN_CAPTIONS.get(name, name.replace("-", " ").title())
    return (
        '<figure class="%s">'
        '<img src="%s" alt="%s" loading="%s">'
        '<figcaption>%s</figcaption>'
        '</figure>'
    ) % (
        class_name,
        screen_file(name),
        escape(caption),
        loading,
        escape(caption),
    )


def media_grid(stills, loading="eager"):
    count = len(stills)
    classes = "media-grid media-count-%d" % count
    return '<div class="%s">\n%s\n      </div>' % (
        classes,
        "\n".join(
            "        " + still_figure(name, "screen-shot", loading)
            for name in stills
        ),
    )


def render_still_wall():
    return "\n".join(
        "    " + still_figure(name, "wall-still", "eager")
        for name in HERO_STILLS
    )


def render_game_cards(apps):
    cards = []
    for app in apps:
        if app.id not in PLAYABLE_STILLS:
            die("no site still mapping for playable app '%s'" % app.id)
        score = ""
        if app.score is not None:
            direction = (
                "lower score is better"
                if app.score.lower_is_better else
                "higher score is better"
            )
            score = '<span>%s</span>' % direction
        meta = "".join((
            '<span>Game %02d</span>' % (app.index + 1),
            '<span>%s</span>' % escape(app.subtitle),
            score,
        ))
        cards.append(
            '<article class="show-card game-card" id="game-%s">\n'
            '      %s\n'
            '      <div class="show-copy">\n'
            '        <p class="eyebrow">%s</p>\n'
            '        <h3>%s</h3>\n'
            '        <p>%s</p>\n'
            '        <p class="meta-row">%s</p>\n'
            '      </div>\n'
            '    </article>'
            % (
                escape(app.id),
                media_grid(PLAYABLE_STILLS[app.id]),
                escape(app.label),
                escape(app.title),
                escape(app.blurb),
                meta,
            )
        )
    return "\n".join("    " + card for card in cards)


def render_system_cards():
    registry = {app.id: app for app in system_apps()}
    cards = []
    for item in SYSTEM_SHOWCASE:
        entry = registry.get(item["id"])
        title = item.get("title") or (entry.title if entry else item["id"].title())
        subtitle = item.get("subtitle") or (entry.subtitle if entry else "")
        kind = "System app" if entry else "Runtime screen"
        cards.append(
            '<article class="show-card system-card" id="screen-%s">\n'
            '      %s\n'
            '      <div class="show-copy">\n'
            '        <p class="eyebrow">%s</p>\n'
            '        <h3>%s</h3>\n'
            '        <p>%s</p>\n'
            '      </div>\n'
            '    </article>'
            % (
                escape(item["id"]),
                media_grid(item["stills"]),
                kind,
                escape(title),
                escape(subtitle),
            )
        )
    return "\n".join("    " + card for card in cards)


def referenced_site_screens(apps):
    names = set(HERO_STILLS)
    for app in apps:
        names.update(PLAYABLE_STILLS.get(app.id, ()))
    for item in SYSTEM_SHOWCASE:
        names.update(item["stills"])
    return names


def validate_site_screens(apps):
    available = available_screen_names()
    referenced = referenced_site_screens(apps)
    missing = referenced - available
    if missing:
        die("site references missing screen still(s): %s" % ", ".join(sorted(missing)))
    unused = available - referenced
    if unused:
        die("docs/screens still(s) not represented on the site: %s" % ", ".join(sorted(unused)))


def manifest_for(board, variant, release):
    return {
        "name": variant["name"],
        "version": release,
        "new_install_prompt_erase": False,
        "builds": [{
            "chipFamily": board["chip"],
            "parts": [{"path": name, "offset": offset} for name, offset in PARTS],
        }],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=os.path.join(ROOT, "site", "_build"),
                        help="output directory (default: site/_build)")
    parser.add_argument("--repo", default="https://github.com/iamankushpandit/Gume",
                        help="repository URL used in the footer links")
    args = parser.parse_args()

    release = version()
    supported = boards()
    catalog = playable_apps()
    validate_site_screens(catalog)
    flash, ram = build_figures()

    out = args.out
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)

    variants = {entry["env"]: entry for entry in VARIANTS}
    builds = []
    for target in supported:
        for env in target["envs"]:
            if env not in variants:
                die("board %s offers env '%s', which is not in VARIANTS"
                    % (target["id"], env))
            variant = variants[env]
            directory = "firmware/%s/%s" % (target["id"], env)
            os.makedirs(os.path.join(out, *directory.split("/")))
            with open(os.path.join(out, *(directory.split("/") + ["manifest.json"])),
                      "w", encoding="utf-8") as handle:
                json.dump(manifest_for(target, variant, release), handle, indent=2)
            builds.append({
                "board": target["id"],
                "env": env,
                "dir": directory,
                "manifest": directory + "/manifest.json",
                "note": variant["note"].format(count=len(catalog)),
                "parts": [name for name, _ in PARTS],
            })

    board_options = "\n".join(
        '          <option value="%s">%s</option>' % (t["id"], escape(t["label"]))
        for t in supported)
    variant_options = "\n".join(
        '          <option value="%s">%s</option>' % (v["env"], escape(v["label"]))
        for v in VARIANTS)
    game_cards = render_game_cards(catalog)
    system_cards = render_system_cards()
    still_wall = render_still_wall()
    screen_count = len(referenced_site_screens(catalog))

    page = read("site", "index.template.html")
    for key, value in (
        ("{{VERSION}}", release),
        ("{{GAME_COUNT}}", str(len(catalog))),
        ("{{BOARD}}", escape(supported[0]["name"])),
        ("{{FLASH}}", escape(flash)),
        ("{{RAM}}", escape(ram)),
        ("{{BOARD_BUY_URL}}", supported[0]["buy"]),
        ("{{BOARD_OPTIONS}}", board_options),
        ("{{VARIANT_OPTIONS}}", variant_options),
        ("{{SCREEN_COUNT}}", str(screen_count)),
        ("{{STILL_WALL}}", still_wall),
        ("{{GAME_CARDS}}", game_cards),
        ("{{SYSTEM_CARDS}}", system_cards),
        ("{{BUILDS_JSON}}", json.dumps(builds, indent=2)),
        ("{{BUILT}}", datetime.date.today().isoformat()),
        ("{{REPO}}", args.repo),
    ):
        page = page.replace(key, value)

    leftover = re.findall(r"\{\{[A-Z_]+\}\}", page)
    if leftover:
        die("template placeholder(s) not filled: %s" % ", ".join(sorted(set(leftover))))

    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as handle:
        handle.write(page)

    shutil.copytree(os.path.join(ROOT, "docs", "screens"),
                    os.path.join(out, "screens"))
    # Jekyll would otherwise skip anything starting with an underscore.
    open(os.path.join(out, ".nojekyll"), "w").close()

    print("Site written to %s -- %d games, %d build(s), v%s."
          % (out, len(catalog), len(builds), release))
    print("Firmware binaries are not included; CI adds them per manifest.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
