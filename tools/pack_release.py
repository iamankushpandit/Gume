#!/usr/bin/env python3
"""Assemble the files a GitHub release publishes, from a finished `pio run`.

This is the whole of what `.github/workflows/release.yml` does between building
and uploading, kept here rather than in YAML for one reason: YAML in a workflow
is only ever executed by the workflow, on a tag, when it is far too late to
find out it was wrong. This can be run against a local build:

    pio run -e app -e bringup -e wifidiag -e batdiag
    python tools/pack_release.py --version 5.0.1 --out dist/

It produces, per environment:

    braino-<version>-<board>-<env>-firmware.bin     the four parts, at the
    braino-<version>-<board>-<env>-bootloader.bin   offsets FLASHING.txt lists
    braino-<version>-<board>-<env>-partitions.bin
    braino-<version>-<board>-<env>-boot_app0.bin
    braino-<version>-<board>-<env>-merged.bin       all four, written at 0x0

plus SHA256SUMS.txt, FLASHING.txt and NOTES.md.

## Two things are derived rather than restated, and both have bitten already

**The environment list comes from platformio.ini**, not from a list here or in
the workflow. `pages.yml` hard-coded "app bringup wifidiag" while gen_site.py
declared only ("app",), and every Pages deploy from 2026-08-15 died after a
clean build -- the installer silently stayed on an old version for two pushes.
A release that quietly omits an environment fails the same way, except the
evidence is a missing download rather than a red run.

**The flash mode, frequency and size are `keep`**, so esptool copies them out
of the bootloader image the build just produced. Writing `dio` / `40m` / `4MB`
here would be a fourth place the board is described, and the one nobody would
think to update when a board with a different flash arrives -- and the failure
is a merged image that bricks at boot rather than anything a check would see.
Verified byte-identical to the explicit flags on this board.

## Release notes come out of CHANGELOG.md

The section for the version being released is lifted verbatim. Hand-written
release notes are a second changelog that agrees with the first only on the day
it is written.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FLASH_PARTS = (
    ("bootloader.bin", "0x1000"),
    ("partitions.bin", "0x8000"),
    ("boot_app0.bin",  "0xe000"),
    ("firmware.bin",   "0x10000"),
)

# What each environment is for, in the words the README and CLAUDE.md use.
# An environment with no entry still ships -- it just gets no description, and
# `--strict` turns that into an error so a new environment cannot arrive
# unexplained.
ENV_BLURBS = {
    "app": "The console -- the games, profiles, scores, settings, Wi-Fi clock "
           "and the BLE beacon. This is the firmware for normal use.",
    "bringup": "Display/touch/SD check, for triaging a board that will not "
               "draw or will not respond to a press.",
    "wifidiag": "Radio test, built alone so no TFT, touch or game code can "
                "interfere with the result.",
    "batdiag": "Eight-page battery bring-up and calibration tool, with CSV "
               "over serial for capturing a full discharge.",
}


def die(message):
    sys.stderr.write("pack_release: %s\n" % message)
    raise SystemExit(1)


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as handle:
        return handle.read()


def firmware_version():
    """The version the firmware reports, which is the release's identity."""
    match = re.search(r'BRAINO_VERSION\s+"([^"]+)"', read("include", "AppVersion.h"))
    if not match:
        die("include/AppVersion.h: BRAINO_VERSION not found")
    return match.group(1)


def environments(ini):
    """Every [env:*] in platformio.ini, with the board each one composes.

    An environment that references no [board_*] section is board-agnostic --
    wifidiag builds src/wifi_diag.cpp alone and touches no board peripheral.
    With a single board defined it still carries that board's name, because the
    release is *for* that board; once there are several, a board-agnostic build
    cannot claim one and goes unlabelled.
    """
    boards = re.findall(r"^\[board_(\w+)\]", ini, re.M)
    found = []
    for env in re.findall(r"^\[env:(\w+)\]", ini, re.M):
        body = re.search(r"^\[env:%s\](.*?)(?=^\[|\Z)" % re.escape(env),
                         ini, re.M | re.S).group(1)
        used = [b for b in boards
                if re.search(r"\$\{board_%s\.build_flags\}" % re.escape(b), body)]
        if len(used) > 1:
            die("[env:%s] composes more than one board section (%s) -- one "
                "board per environment is the rule in CLAUDE.md"
                % (env, ", ".join(used)))
        if used:
            board = used[0]
        elif len(boards) == 1:
            board = boards[0]
        else:
            board = None
        found.append((env, board))
    if not found:
        die("platformio.ini declares no [env:*] sections")
    return found


def boot_app0():
    """The stock boot_app0.bin from the Arduino framework package.

    Not a build artifact -- it ships with the framework, and pio's own upload
    sends it at 0xe000. A merged image without it boots the wrong slot.
    """
    root = os.path.expanduser("~/.platformio/packages")
    for base, _dirs, files in os.walk(root):
        if "boot_app0.bin" in files and "partitions" in base.replace("\\", "/"):
            return os.path.join(base, "boot_app0.bin")
    die("boot_app0.bin not found under %s -- has the framework been installed?"
        % root)


def esptool_command():
    """esptool, preferring the copy PlatformIO installed for this project."""
    candidate = os.path.expanduser(
        "~/.platformio/packages/tool-esptoolpy/esptool.py")
    if os.path.exists(candidate):
        return [sys.executable, candidate]
    if shutil.which("esptool.py"):
        return ["esptool.py"]
    die("esptool not found -- install it, or run a `pio run` first so "
        "PlatformIO fetches tool-esptoolpy")


def merge(env_dir, parts, out_path):
    """Flatten the four parts into one image to be written at 0x0.

    flash_mode/freq/size are `keep`: esptool reads them from the bootloader
    header the build produced, so this cannot contradict the board.
    """
    command = esptool_command() + [
        "--chip", "esp32", "merge_bin", "-o", out_path,
        "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
    ]
    for name, offset in FLASH_PARTS:
        command += [offset, parts[name]]
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    if result.returncode != 0:
        die("esptool merge_bin failed for %s:\n%s"
            % (env_dir, result.stdout.decode("utf-8", "replace")))


def changelog_section(version):
    """The CHANGELOG entry for this version, verbatim, without its heading."""
    changelog = read("CHANGELOG.md")
    match = re.search(
        r"^##\s+%s\b[^\n]*\n(.*?)(?=^##\s|\Z)" % re.escape(version),
        changelog, re.M | re.S)
    if not match:
        die("CHANGELOG.md has no '## %s' section -- release notes are lifted "
            "from it rather than written twice" % version)
    return match.group(1).strip()


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def flashing_notes(version, built, entries):
    lines = [
        "Braino! %s -- firmware images" % version,
        "",
    ]
    lines += textwrap.wrap(
        "Built from %s. Every image reports its own branch and commit on "
        "About > This build, so a board on a desk can be identified without "
        "guessing which flash it is carrying." % built,
        width=70, break_on_hyphens=False)
    lines += [
        "",
        "Easiest route is the web installer:",
        "  https://iamankushpandit.github.io/Gume/",
        "It flashes over Web Serial from a Chromium browser and needs none of",
        "these files.",
        "",
        "-" * 70,
        "Which image do I want?",
        "-" * 70,
    ]
    # The board is named once when every image is for the same one, and per
    # image only when they differ. Repeating an identical line under each
    # environment is how a list stops being read.
    boards = {board for _env, board, _parts in entries if board}
    if len(boards) == 1:
        lines += ["  Board: %s" % boards.pop(), ""]
    for env, board, _parts in entries:
        blurb = ENV_BLURBS.get(env, "(no description -- see platformio.ini)")
        if len(boards) > 1 and board:
            blurb = "%s [board: %s]" % (blurb, board)
        # break_on_hyphens=False, or "Wi-Fi" and "bring-up" get split
        # across lines at the hyphen.
        wrapped = textwrap.wrap(blurb, width=58,
                                break_on_hyphens=False) or [""]
        lines.append("  %-9s %s" % (env, wrapped[0]))
        for extra in wrapped[1:]:
            lines.append("  %-9s %s" % ("", extra))
    lines += [
        "",
        "The diagnostics are for hardware triage. Flashing one replaces the",
        "console until you flash the app image back.",
        "",
        "-" * 70,
        "Flashing the merged image (one command)",
        "-" * 70,
        "  esptool.py --chip esp32 --port COM5 --baud 460800 \\",
        "      write_flash 0x0 %s" % entries[0][2]["merged_name"],
        "",
        "The merged file already contains the four parts below at their",
        "offsets, and carries the flash mode, frequency and size the firmware",
        "was built with.",
        "",
        "-" * 70,
        "Flashing the parts separately",
        "-" * 70,
        "  esptool.py --chip esp32 --port COM5 --baud 460800 write_flash \\",
    ]
    for name, offset in FLASH_PARTS:
        lines.append("      %-7s %s \\" % (offset, entries[0][2][name + "_name"]))
    lines[-1] = lines[-1].rstrip(" \\")
    lines += [
        "",
        "Substitute another environment's name to flash a diagnostic.",
        "",
        "-" * 70,
        "What flashing destroys",
        "-" * 70,
        "NVS holds touch calibration, player profiles and scores. A plain",
        "write_flash of these images leaves NVS alone; `esptool.py",
        "erase_flash` does not, and takes the touch calibration with it, so",
        "the panel needs recalibrating on first boot.",
        "",
        "-" * 70,
        "Verifying a download",
        "-" * 70,
        "  sha256sum -c SHA256SUMS.txt",
        "",
    ]
    return "\n".join(lines)


def pack(version, out_dir, built, strict):
    ini = read("platformio.ini")
    stock_boot_app0 = boot_app0()
    os.makedirs(out_dir, exist_ok=True)

    entries = []
    missing_blurb = []
    for env, board in environments(ini):
        env_dir = os.path.join(ROOT, ".pio", "build", env)
        if not os.path.isdir(env_dir):
            die("no build for [env:%s] at %s -- every environment in "
                "platformio.ini has to be built before packing, or the "
                "release quietly ships without it" % (env, env_dir))

        label = "-".join(filter(None, ("braino", version, board, env)))
        parts = {}
        for name, _offset in FLASH_PARTS:
            source = (stock_boot_app0 if name == "boot_app0.bin"
                      else os.path.join(env_dir, name))
            if not os.path.exists(source):
                die("[env:%s] built without %s -- expected at %s"
                    % (env, name, source))
            target_name = "%s-%s" % (label, name)
            shutil.copyfile(source, os.path.join(out_dir, target_name))
            parts[name] = source
            parts[name + "_name"] = target_name

        parts["merged_name"] = "%s-merged.bin" % label
        merge(env, parts, os.path.join(out_dir, parts["merged_name"]))

        if env not in ENV_BLURBS:
            missing_blurb.append(env)
        entries.append((env, board, parts))

    if missing_blurb and strict:
        die("no description in ENV_BLURBS for: %s -- a release that offers a "
            "firmware nobody can identify is worse than one that omits it"
            % ", ".join(missing_blurb))

    binaries = sorted(name for name in os.listdir(out_dir)
                      if name.endswith(".bin"))
    with open(os.path.join(out_dir, "SHA256SUMS.txt"), "w",
              encoding="utf-8", newline="\n") as handle:
        for name in binaries:
            handle.write("%s *%s\n" % (sha256(os.path.join(out_dir, name)), name))

    with open(os.path.join(out_dir, "FLASHING.txt"), "w",
              encoding="utf-8", newline="\n") as handle:
        handle.write(flashing_notes(version, built, entries))

    with open(os.path.join(out_dir, "NOTES.md"), "w",
              encoding="utf-8", newline="\n") as handle:
        handle.write(changelog_section(version) + "\n")

    return entries, binaries


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", help="release version; defaults to "
                                          "BRAINO_VERSION")
    parser.add_argument("--out", default="dist", help="output directory")
    parser.add_argument("--built", default="a local build",
                        help="how the binaries were built, for FLASHING.txt")
    parser.add_argument("--strict", action="store_true",
                        help="fail if an environment has no description")
    args = parser.parse_args()

    version = args.version or firmware_version()
    stated = firmware_version()
    if version != stated:
        die("asked to pack %s but include/AppVersion.h says %s -- the "
            "firmware's own version is the release's identity"
            % (version, stated))

    out_dir = args.out if os.path.isabs(args.out) else os.path.join(ROOT, args.out)
    entries, binaries = pack(version, out_dir, args.built, args.strict)

    print("Packed %s: %d environment(s), %d binaries -> %s"
          % (version, len(entries), len(binaries), out_dir))
    for env, board, parts in entries:
        size = os.path.getsize(os.path.join(out_dir, parts["merged_name"]))
        print("  %-9s %-10s %9s bytes merged"
              % (env, board or "(any board)", format(size, ",")))


if __name__ == "__main__":
    main()
