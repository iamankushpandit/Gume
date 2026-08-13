#!/usr/bin/env python3
"""Generate the GitHub Pages site, including the web installer.

The page is *derived*, for the same reason the About app is: a hand-written
copy of the game list fell six games behind once already, and a landing page is
even easier to forget than a screen you look at while holding the device. So
nothing here is typed twice -- the version comes from AppVersion.h, the games
and their blurbs from GAME_CATALOG, the build figures from README.md.

The board and firmware matrix lives in BOARDS and VARIANTS below, and CI asks
for it with --print-envs rather than keeping a second copy, so adding a board
in one place is enough to get it built, published and offered on the page.

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
# the sentence under it, and has to be honest about what the diagnostic builds
# do -- they replace the games entirely.
VARIANTS = (
    {
        "id": "app",
        "label": "GoodTime Kids (the games)",
        "name": "GoodTime Kids",
        "note": "The full console: {count} games, profiles, scores, settings, "
                "Wi-Fi clock and the BLE beacon. This is the one you want.",
    },
    {
        "id": "bringup",
        "label": "Bring-up diagnostic (display + touch check)",
        "name": "GoodTime Kids bring-up diagnostic",
        "note": "Diagnostic build, not the console. Draws a display and touch "
                "test instead of the app -- flash it when a fresh board shows "
                "nothing, or touch lands in the wrong place, to tell a wiring "
                "fault from a software one.",
    },
    {
        "id": "wifidiag",
        "label": "Wi-Fi radio diagnostic",
        "name": "GoodTime Kids Wi-Fi diagnostic",
        "note": "Diagnostic build, not the console. The radio test compiled "
                "alone, with no display, touch or game code that could "
                "interfere; it scans for networks and reports over serial at "
                "115200 baud. No display code, so one build serves every panel.",
    },
)

# The panel controller and the pin map are compile-time constants, so a build is
# only meaningful on the board it was compiled for. `envs` maps a firmware id to
# the PlatformIO environment that produces it for this board -- wifidiag has no
# display code, so every board points at the same one.
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
        "envs": {"app": "app", "bringup": "bringup", "wifidiag": "wifidiag"},
    },
    {
        "id": "cyd2usb",
        "label": "ESP32-2432S028R, micro-USB + USB-C (ST7789) -- 2.8 inch, resistive",
        "chip": "ESP32",
        "tested": False,
        "aka": "The later two-USB revision, often called v2/v3 or CYD2USB. Same "
               "board, same GPIOs, different panel controller: ST7789 with BGR "
               "colour order and inversion off.",
        "envs": {"app": "app_st7789", "bringup": "bringup_st7789",
                 "wifidiag": "wifidiag"},
    },
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
    return find(r'GOODTIME_KIDS_VERSION\s+"([^"]+)"',
                read("include", "AppVersion.h"), "the version").group(1)


def games():
    """(title, blurb) for every catalogued game, in launcher order."""
    catalog = read("src", "engine", "GameCatalog.cpp")
    entries = re.findall(
        r'^\s*\{\s*"[a-z0-9]+",\s*"([^"]+)",\s*"[^"]*",\s*"[^"]*",\s*"([^"]+)"',
        catalog, re.M)
    if not entries:
        die("parsed no games out of GameCatalog.cpp")

    declared = int(find(r"GAME_CATALOG_COUNT\s*=\s*(\d+)",
                        read("src", "engine", "GameCatalog.h"),
                        "GAME_CATALOG_COUNT").group(1))
    if len(entries) != declared:
        die("parsed %d games but GAME_CATALOG_COUNT is %d"
            % (len(entries), declared))
    return entries


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

    CI asks for this rather than keeping its own list, so adding a board here
    is enough to get it built and published.
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
                             "one per line, and exit. CI builds exactly these.")
    args = parser.parse_args()

    if args.print_envs:
        print("\n".join(environments()))
        return 0

    release = version()
    catalog = games()
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
    game_rows = "\n".join(
        "    <div><b>%s</b> &mdash; <span>%s</span></div>"
        % (escape(title), escape(blurb)) for title, blurb in catalog)
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
        ("{{BOARD_OPTIONS}}", board_options),
        ("{{VARIANT_OPTIONS}}", variant_options),
        ("{{GAME_ROWS}}", game_rows),
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
