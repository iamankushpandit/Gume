#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

"""Check the rules that keep one `src/` building for every platform.

Braino! runs on three kinds of platform -- the ESP32 boards, the Attaky Core
(an experimental ESP32-S3 board with plug-in modules), and Linux (the PC
build and the RG40XX V image, both from `host/`). They share one `src/`, and
CLAUDE.md states the rules that make that possible. These are the ones a
machine can check:

  1. No platform conditional under src/ or include/. `host/` adapts to
     `src/`, never the reverse: no `#if` on a host macro, an OS macro or SDL,
     and no SDL header. A platform difference is an interface (rule 2).
  2. Every platform interface has one implementation per platform. Each
     `src/**/<Name>Esp32.cpp` implements the interface declared in
     `<Name>.h` beside it; `host/src/` must implement the same namespace,
     and `host/CMakeLists.txt` must leave out exactly the ESP32
     implementations plus the two parts the host replaces outright (the
     hardware watchdog and the codec/DAC backend). A new interface whose
     ESP32 file the host still compiles fails to link on Linux; one the host
     leaves out but never implements does too -- this says so before CI.
  3. Attaky code compiles only into the Attaky image. The four macros that
     switch it on are non-zero only in `include/boards/attaky-core.h`, and
     capacitive touch only on the boards that have it.
  4. (--elf) The same rule, measured: given built firmware, no image but the
     Attaky Core's carries an Attaky-only symbol. Run after `pio run`:

         python tools/check_platforms.py --elf .pio/build

     Environments not built are skipped and named, so a run with no builds
     still checks rules 1-3.

    python tools/check_platforms.py
"""

import argparse
import glob
import io
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

# --- rule 1 ---------------------------------------------------------------

# Macros that would mean "which platform is this?" asked from inside src/.
PLATFORM_MACRO = re.compile(
    r"\b(GUME_HOST\w*|BRAINO_HOST\w*|HOST_BUILD|__linux__|__linux|linux|"
    r"_WIN32|_WIN64|__APPLE__|__unix__|SDL_\w+|__EMSCRIPTEN__)\b")
CONDITIONAL = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)$")
SDL_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]SDL')

# --- rule 2 ---------------------------------------------------------------

# Replaced on the host by a file of its own rather than through an interface
# with a *Esp32.cpp beside it. Each names the host file that replaces it.
REPLACED_OUTRIGHT = {
    "src/hal/Watchdog.cpp": "host/src/HostWatchdog.cpp",
    "src/hal/BoardAudioBackend.cpp": "host/src/HostAudio.cpp",
}

# --- rule 3 ---------------------------------------------------------------

ATTAKY_HEADER = "include/boards/attaky-core.h"
ATTAKY_ONLY_MACROS = ("GUME_HAS_IO_EXPANDER", "GUME_HAS_NAV_KEYS",
                      "GUME_HAS_KEYBOARD", "GUME_HAS_ATTAKY_MODULES")
# Capacitive touch is a fact about a panel, so more than one board has it.
CAPACITIVE_BOARDS = {ATTAKY_HEADER, "include/boards/fnk0104b.h",
                     "host/boards/host.h"}

# --- rule 4 ---------------------------------------------------------------

# Symbols that exist only behind the macros above. From the list CLAUDE.md
# states under "Attaky code compiles only into the Attaky image".
ATTAKY_SYMBOLS = ("pollControls", "pollTypedChar", "::typedChar(",
                  "selectorFrame", "Focus::", "BoardExpander",
                  "sampleModuleBattery", "Modules::", "i2sUp")


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def read(path):
    with io.open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as f:
        return f.read()


def code_lines(text):
    """(line number, line) with comments blanked, so a rule written in a
    comment is not mistaken for code that breaks it."""
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text,
                  flags=re.S)
    for n, line in enumerate(text.split("\n"), 1):
        yield n, line.split("//", 1)[0]


def sources(*dirs):
    for d in dirs:
        for path in glob.glob(os.path.join(ROOT, d, "**", "*"), recursive=True):
            if path.endswith((".h", ".hpp", ".cpp", ".c")):
                yield path


def check_no_platform_conditionals(problems):
    for path in sources("src", "include"):
        for n, line in code_lines(read(rel(path))):
            m = CONDITIONAL.match(line)
            if m and PLATFORM_MACRO.search(m.group(1)):
                problems.append(
                    "%s:%d asks which platform it is on (%s). host/ adapts to "
                    "src/, never the reverse: put the difference behind an "
                    "interface with one implementation per platform."
                    % (rel(path), n, PLATFORM_MACRO.search(m.group(1)).group(1)))
            if SDL_INCLUDE.match(line):
                problems.append("%s:%d includes SDL; only host/ may."
                                % (rel(path), n))


def interface_namespace(header):
    m = re.search(r"^\s*namespace\s+(\w+)\s*\{", read(header), re.M)
    return m.group(1) if m else None


def cmake_exclusions():
    """The src/ files host/CMakeLists.txt filters out of the firmware."""
    text = read("host/CMakeLists.txt")
    excluded = set()
    for block in re.findall(r"list\(FILTER\s+FIRMWARE_SOURCES\s+EXCLUDE\s+REGEX\s+"
                            r"\"([^\"]+)\"", text, re.S):
        pattern = block.replace("\\\\", "\\")
        for path in sources("src"):
            if re.search(pattern, "/" + rel(path)):
                excluded.add(rel(path))
    return excluded


def check_platform_interfaces(problems):
    host_text = {rel(p): read(rel(p)) for p in sources("host/src")}
    esp32_impls = sorted(rel(p) for p in sources("src")
                         if p.endswith("Esp32.cpp"))
    if not esp32_impls:
        problems.append("no src/**/*Esp32.cpp found -- has the naming "
                        "convention for platform implementations changed?")
    for impl in esp32_impls:
        header = impl[:-len("Esp32.cpp")] + ".h"
        if not os.path.exists(os.path.join(ROOT, header)):
            problems.append("%s has no interface header %s beside it" % (impl, header))
            continue
        ns = interface_namespace(header)
        if ns:
            what = ns
            defines = re.compile(r"\bnamespace\s+%s\s*\{|\b%s::\w+\s*\(" % (ns, ns))
        else:
            # A class with a factory: the free functions the header declares
            # at file scope (PowerProbe's platformPowerProbe()).
            names = re.findall(r"^[A-Za-z_][\w:<>&\* ]*?[\s&\*](\w+)\s*\([^;{]*\)\s*;",
                               read(header), re.M)
            if not names:
                problems.append("%s declares neither a namespace nor a free "
                                "function for a platform to implement" % header)
                continue
            what = ", ".join(n + "()" for n in names)
            defines = re.compile(r"\b(?:%s)\s*\([^;]*\)\s*\{" % "|".join(names))
        if not any(defines.search(t) for p, t in host_text.items()
                   if p.endswith(".cpp")):
            problems.append("%s (%s) is implemented for the ESP32 boards in %s "
                            "but nowhere in host/src/" % (header, what, impl))

    expected = set(esp32_impls) | set(REPLACED_OUTRIGHT)
    for src, replacement in REPLACED_OUTRIGHT.items():
        if not os.path.exists(os.path.join(ROOT, replacement)):
            problems.append("%s is replaced on the host by %s, which does not "
                            "exist" % (src, replacement))
    excluded = cmake_exclusions()
    for path in sorted(expected - excluded):
        problems.append("host/CMakeLists.txt compiles %s, an ESP32-only "
                        "implementation; add it to the FIRMWARE_SOURCES "
                        "exclusion" % path)
    for path in sorted(excluded - expected):
        problems.append("host/CMakeLists.txt leaves %s out of the host build, "
                        "but it is not an ESP32 implementation of an interface. "
                        "If the host really replaces it, name it in "
                        "REPLACED_OUTRIGHT in this checker with the file that "
                        "does" % path)


def macro_values(path):
    values = {}
    for _, line in code_lines(read(path)):
        m = re.match(r"^\s*#\s*define\s+(GUME_\w+)\s+(\S+)", line)
        if m:
            values[m.group(1)] = m.group(2)
    return values


def check_attaky_macros(problems):
    headers = [rel(p) for p in glob.glob(os.path.join(ROOT, "include", "boards", "*.h"))]
    headers.append("host/boards/host.h")
    if ATTAKY_HEADER not in headers:
        problems.append("%s is missing; update ATTAKY_HEADER" % ATTAKY_HEADER)
        return
    for path in sorted(headers):
        values = macro_values(path)
        for macro in ATTAKY_ONLY_MACROS:
            on = values.get(macro, "0") not in ("0", "false")
            if on and path != ATTAKY_HEADER:
                problems.append("%s turns on %s; Attaky code compiles only into "
                                "the Attaky image" % (path, macro))
            if not on and path == ATTAKY_HEADER:
                problems.append("%s does not turn on %s" % (path, macro))
        cap = values.get("GUME_TOUCH_CAPACITIVE", "0") not in ("0", "false")
        if cap != (path in CAPACITIVE_BOARDS):
            problems.append("%s: GUME_TOUCH_CAPACITIVE is %s, but this checker's "
                            "CAPACITIVE_BOARDS says %s; a new capacitive board is "
                            "added there" % (path, "on" if cap else "off",
                                             "on" if path in CAPACITIVE_BOARDS
                                             else "off"))


def find_nm():
    pkgs = os.path.join(os.path.expanduser("~"), ".platformio", "packages")
    for name in ("xtensa-esp32s3-elf-nm", "xtensa-esp32-elf-nm"):
        found = shutil.which(name)
        if found:
            return found
        for path in glob.glob(os.path.join(pkgs, "toolchain-xtensa-*", "bin", name + "*")):
            return path
    return None


def check_elves(problems, build_dir):
    import envs  # tools/envs.py: the one place environments are classified
    ini = envs.read_ini()
    bodies = dict(envs.sections(ini))
    kinds = envs.classify(ini)
    nm = find_nm()
    checked, skipped = [], []
    for env, kind in kinds.items():
        if kind == "diagnostic":
            continue
        elf = os.path.join(build_dir, env, "firmware.elf")
        if not os.path.exists(elf):
            skipped.append(env)
            continue
        if "board_attaky_core" in bodies[env]:
            continue
        if nm is None:
            problems.append("no xtensa nm found to read %s" % elf)
            return checked, skipped
        out = subprocess.run([nm, "-C", elf], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, check=False)
        if out.returncode != 0:
            problems.append("%s: nm failed: %s" % (elf, out.stderr.decode()[:200]))
            continue
        found = set()
        for line in out.stdout.decode("utf-8", "replace").splitlines():
            for sym in ATTAKY_SYMBOLS:
                if sym in line:
                    found.add(sym)
        for sym in sorted(found):
            problems.append("[env:%s] firmware carries Attaky-only `%s`; it must "
                            "preprocess out of every other image" % (env, sym))
        checked.append(env)
    return checked, skipped


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--elf", metavar="BUILD_DIR",
                        help="also read built images under BUILD_DIR/<env>/firmware.elf")
    args = parser.parse_args()

    problems = []
    check_no_platform_conditionals(problems)
    check_platform_interfaces(problems)
    check_attaky_macros(problems)
    note = ""
    if args.elf:
        checked, skipped = check_elves(problems, args.elf)
        note = " Images read: %s." % (", ".join(checked) or "none")
        if skipped:
            note += " Not built, so not read: %s." % ", ".join(skipped)

    if problems:
        print("Platform check: %d problem(s)" % len(problems))
        for p in problems:
            print("  - " + p)
        return 1
    print("Platform check: clean." + note)
    return 0


if __name__ == "__main__":
    sys.exit(main())
