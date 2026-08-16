#!/usr/bin/env python3
"""Generate the GitHub Pages site, including the web installer.

The page is *derived*, for the same reason the About app is: a hand-written
copy of the game list fell six games behind once already, and a landing page is
even easier to forget than a screen you look at while holding the device. So
nothing here is typed twice -- the version comes from AppVersion.h, the games
and their blurbs from the playable app registry, and the build figures from
README.md.

The board and console-firmware matrix lives in BOARDS and VARIANTS below, and
the Pages workflow asks for it with --print-envs rather than keeping a second
copy, so adding a board in one place is enough to get it built, published and
offered on the page.

    python tools/gen_site.py [--out DIR] [--repo URL]
    python tools/gen_site.py --print-envs

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

# One entry per firmware the picker offers. `label` is what it shows; `note` is
# the sentence under it. Only the console is offered here.
#
# The bringup and wifidiag environments still exist, and normal CI still builds
# them -- they are how you tell a wiring fault from a software one on a fresh
# board. They are not on this page. Everyone arriving at it wants the games, and a
# picker whose other two entries erase the console and replace it with a serial
# test is a trap for exactly the audience least able to recover from it.
# Diagnostics belong with the people who build from source; README covers them.
#
# Adding an entry here is a commitment that .github/workflows/pages.yml builds
# that environment -- check_docs.py enforces it, because a manifest pointing at
# binaries CI never produced fails halfway through erasing somebody's board.
VARIANTS = (
    {
        "id": "app",
        "label": "Braino! (the games)",
        "name": "Braino!",
        "note": "The full console: {count} games, profiles, scores, settings, "
                "Wi-Fi clock and the BLE beacon.",
    },
)

# The panel controller and the pin map are compile-time constants, so a build is
# only meaningful on the board it was compiled for. `envs` maps a firmware id to
# the PlatformIO environment that produces it for this board. Diagnostics stay
# in platformio.ini and README; the public installer offers only the console.
#
# `tested` is not decoration. Only the E32R28T-1 has ever been run; everything
# else is compiled against a published pin map and never powered on here, and
# the page says so per board rather than in fine print.
BOARDS = (
    {
        "id": "e32r28t1",
        "label": "ESP32-2432S028R, single micro-USB (ILI9341) -- 2.8 inch, resistive",
        "chip": "ESP32",
        "tested": True,
        "aka": "Sold as E32R28T-1 / ESP32-32E. The board this firmware was "
               "written on and the only one it has been run on.",
        "envs": {"app": "app"},
    },
    {
        "id": "cyd2usb",
        "label": "ESP32-2432S028R, micro-USB + USB-C (ST7789) -- 2.8 inch, resistive",
        "chip": "ESP32",
        "tested": False,
        "aka": "The later two-USB revision, often called v2/v3 or CYD2USB. Same "
               "board, same GPIOs, different panel controller: ST7789 with BGR "
               "colour order and inversion off.",
        "envs": {"app": "app_st7789"},
    },
)

BOARD_BUY_URL = (
    "https://www.amazon.com/dp/B0D92C9MMH"
    "?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1"
)

SCREEN_CAPTIONS = {
    "about-radios": "About: what the radios do",
    "calendar": "Calendar",
    "cinnamon": "Cinnamon Says",
    "colormix": "Color Mix",
    "counting": "Counting",
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
    "oddone": "Odd One",
    "percent": "Percent Circle",
    "profiles": "Profiles",
    "scores-device": "Scores: device best",
    "scores-mine": "Scores: this player",
    "screensaver": "Screen saver",
    "settings-device": "Settings: device",
    "settings-games": "Settings: games",
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
        "subtitle": "Boot-time player choice, five children, and Guest mode that deliberately saves nothing.",
        "stills": ("profiles",),
    },
    {
        "id": "settings",
        "subtitle": "Device controls, per-child game visibility, brightness, screen saver and sleep policy.",
        "stills": ("settings-device", "settings-games", "settings-power"),
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
        "id": "screensaver",
        "title": "Screen Saver",
        "subtitle": "Self-playing Pong before panel sleep, with rally colour mirrored on the case LED.",
        "stills": ("screensaver",),
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


def environments():
    """Every PlatformIO environment the site needs, deduplicated.

    The Pages workflow asks for this rather than keeping its own list, so adding
    a board here is enough to get it built and published.
    """
    seen = []
    for target in BOARDS:
        for env in target["envs"].values():
            if env not in seen:
                seen.append(env)
    return seen


def plan(out, release, game_count):
    """Write one manifest per (board, firmware) and describe them for the page."""
    variants = {entry["id"]: entry for entry in VARIANTS}
    builds = []
    for target in BOARDS:
        for variant_id, env in target["envs"].items():
            if variant_id not in variants:
                die("board %s offers firmware '%s', which is not in VARIANTS"
                    % (target["id"], variant_id))
            variant = variants[variant_id]
            directory = "firmware/%s/%s" % (target["id"], variant_id)
            os.makedirs(os.path.join(out, *directory.split("/")))
            with open(os.path.join(out, *(directory.split("/") + ["manifest.json"])),
                      "w", encoding="utf-8") as handle:
                json.dump(manifest_for(target, variant, release), handle, indent=2)
            builds.append({
                "board": target["id"],
                "variant": variant_id,
                "env": env,
                "dir": directory,
                "manifest": directory + "/manifest.json",
                "note": variant["note"].format(count=game_count),
                "boardNote": target["aka"],
                "tested": bool(target["tested"]),
                "parts": [name for name, _ in PARTS],
            })
    return builds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=os.path.join(ROOT, "site", "_build"),
                        help="output directory (default: site/_build)")
    parser.add_argument("--repo", default="https://github.com/iamankushpandit/Gume",
                        help="repository URL used in the footer links")
    parser.add_argument("--print-envs", action="store_true",
                        help="print the PlatformIO environments the site needs, "
                             "one per line, and exit. Pages builds these.")
    args = parser.parse_args()

    if args.print_envs:
        print("\n".join(environments()))
        return 0

    release = version()
    catalog = playable_apps()
    validate_site_screens(catalog)
    flash, ram = build_figures()

    out = args.out
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)

    builds = plan(out, release, len(catalog))
    # CI reads this to know which build directory each environment feeds, so
    # the workflow never keeps a second copy of the board list.
    with open(os.path.join(out, "builds.json"), "w", encoding="utf-8") as handle:
        json.dump(builds, handle, indent=2)

    board_options = "\n".join(
        '          <option value="%s">%s%s</option>'
        % (t["id"], escape(t["label"]), "" if t["tested"] else " [untested]")
        for t in BOARDS)
    variant_options = "\n".join(
        '          <option value="%s">%s</option>' % (v["id"], escape(v["label"]))
        for v in VARIANTS)
    game_cards = render_game_cards(catalog)
    system_cards = render_system_cards()
    still_wall = render_still_wall()
    screen_count = len(referenced_site_screens(catalog))
    board_table = "\n".join(
        "          <br><b>%s</b>%s<br><span class=\"note\">%s</span>"
        % (escape(t["label"]),
           "" if t["tested"] else " &mdash; <b>untested</b>",
           escape(t["aka"]))
        for t in BOARDS)

    page = read("site", "index.template.html")
    for key, value in (
        ("{{VERSION}}", release),
        ("{{GAME_COUNT}}", str(len(catalog))),
        ("{{BOARD_TABLE}}", board_table),
        ("{{FLASH}}", escape(flash)),
        ("{{RAM}}", escape(ram)),
        ("{{BOARD_BUY_URL}}", BOARD_BUY_URL),
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
