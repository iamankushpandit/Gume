#!/usr/bin/env python3
"""Check that every supported board is described once, completely, and consistently.

A board is described in exactly two places, and they have to agree:

  * `include/boards/<id>.h` -- the `BoardProfile` the firmware reads. Pins,
    rotations, the battery divider, which peripherals exist at all.
  * a `[board_<id>]` section in `platformio.ini` -- only the handful of macros
    TFT_eSPI insists on being told at compile time, plus the board's name and
    the header naming.

`include/BoardConfig.h` static_asserts the overlap, so a mismatch cannot be
flashed. This check runs without a toolchain and catches the rest: a field
added to `BoardProfile` that an existing board never filled in, a header with
no section pointing at it, a board-specific macro that has drifted up into
`[common]` where it would silently apply to every board.

    python tools/check_boards.py

Exit 0 = consistent, 1 = something needs fixing.

Why the fields are checked by comment
-------------------------------------
`BOARD` is aggregate-initialised, so the initialisers are positional and carry
no field names of their own. Each board header therefore labels every value
with a `/* fieldName */` comment, and this check reads those labels. It is a
convention, not a language rule -- which is exactly why it needs a checker.
Adding a field to `BoardProfile` without filling it in on every board would
otherwise compile, and the board would boot with a zero for it.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOARDS_DIR = os.path.join(ROOT, "include", "boards")

# Macros that describe one specific board. In [common] they would be a claim
# about every board, which is how a second board ends up with the first one's
# backlight pin.
BOARD_SPECIFIC_MACRO = re.compile(
    r"-D\s*(BOARD_NAME|GUME_BOARD_HEADER|TFT_\w+|\w+_DRIVER|SPI_FREQUENCY"
    r"|SPI_READ_FREQUENCY|USE_HSPI_PORT)\b")

# Every [board_*] section has to state these; the firmware or the display
# driver reads each one, and a missing one fails late and confusingly.
REQUIRED_BOARD_MACROS = ("BOARD_NAME", "GUME_BOARD_HEADER",
                         "TFT_WIDTH", "TFT_HEIGHT", "TFT_BL")

# Environments that need no [board_*] section.
#
# wifidiag builds no board peripheral at all. s3diag is a different case worth
# stating: it drives a panel, but for a board this firmware cannot yet run on.
# A [board_*] section is a claim of support, and check_reachable() below is
# what that claim costs -- a web-installer label, an offered firmware, a CI
# build. Making it before the capacitive touch path exists would advertise a
# firmware that boots into a screen nobody can press, which is precisely the
# outcome BoardProfile.h refuses to allow. The section arrives with the port.
BOARDLESS_ENVS = ("wifidiag", "s3diag", "diag4")


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_release            # noqa: E402  (path set immediately above)


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as handle:
        return handle.read()


def profile_contract():
    """What include/BoardProfile.h requires of every board.

    Read out of the header rather than listed here, so adding a field to the
    contract automatically starts requiring it of every board.

    Returns (leaf field names, the peripheral struct types BoardProfile nests).
    """
    text = read("include", "BoardProfile.h")
    fields = []
    groups = []
    for name, body in re.findall(r"struct\s+(\w+)\s*\{(.*?)\n\};", text, re.S):
        for line in body.splitlines():
            line = line.split("//")[0].strip()
            match = re.match(r"^(?:const\s+)?([\w:]+)\*?\s+(\w+)\s*;$", line)
            if not match:
                continue
            kind, field = match.groups()
            if name == "BoardProfile":
                # A nested peripheral group. Its own leaves are required below;
                # the group itself is satisfied by naming the type.
                if kind.endswith("Profile"):
                    groups.append(kind)
                continue
            fields.append(field)
    return fields, groups


def ini_sections(text):
    """{section name: raw body} for every section in an ini file."""
    sections = {}
    current = None
    for line in text.splitlines():
        header = re.match(r"^\[([^\]]+)\]\s*$", line)
        if header:
            current = header.group(1)
            sections[current] = []
        elif current is not None:
            sections[current].append(line)
    return {name: "\n".join(body) for name, body in sections.items()}


def check_headers(problems, fields, groups):
    if not os.path.isdir(BOARDS_DIR):
        problems.append("include/boards/ does not exist")
        return []

    headers = sorted(name for name in os.listdir(BOARDS_DIR)
                     if name.endswith(".h"))
    if not headers:
        problems.append("include/boards/ contains no board profile")

    for name in headers:
        text = read("include", "boards", name)
        where = "include/boards/%s" % name

        if '#include "BoardProfile.h"' not in text:
            problems.append("%s does not include BoardProfile.h" % where)
        if not re.search(r"\binline\s+constexpr\s+BoardProfile\s+BOARD\s*=", text):
            problems.append(
                "%s does not define `inline constexpr BoardProfile BOARD`" % where)

        for group in groups:
            if not re.search(r"\b%s\s*\{" % group, text):
                problems.append("%s never initialises its %s" % (where, group))

        labelled = set(re.findall(r"/\*\s*(\w+)\s*\*/", text))
        for field in fields:
            if field not in labelled:
                problems.append(
                    "%s never fills in BoardProfile field '%s' -- add it, or the "
                    "board boots with a zero for it" % (where, field))

    return headers


def check_ini(problems, headers):
    text = read("platformio.ini")
    sections = ini_sections(text)

    common = sections.get("common", "")
    for macro in BOARD_SPECIFIC_MACRO.findall(common):
        problems.append(
            "platformio.ini [common] defines -D %s, which is true of one board "
            "rather than all of them; move it to a [board_*] section" % macro)

    boards = {name: body for name, body in sections.items()
              if name.startswith("board_")}
    if not boards:
        problems.append("platformio.ini declares no [board_*] section")

    referenced_headers = set()
    for name, body in sorted(boards.items()):
        for macro in REQUIRED_BOARD_MACROS:
            if not re.search(r"-D\s*%s\s*=" % macro, body):
                problems.append("platformio.ini [%s] does not define %s"
                                % (name, macro))

        header = re.search(r'-D\s*GUME_BOARD_HEADER\s*=\s*\\?"([^"\\]+)\\?"', body)
        if not header:
            continue
        relative = header.group(1)
        referenced_headers.add(os.path.basename(relative))
        if not os.path.isfile(os.path.join(ROOT, "include", *relative.split("/"))):
            problems.append("platformio.ini [%s] points GUME_BOARD_HEADER at "
                            "include/%s, which does not exist" % (name, relative))
            continue
        check_agreement(problems, name, body, relative)

    for header in headers:
        if header not in referenced_headers:
            problems.append(
                "include/boards/%s is not referenced by any [board_*] section, "
                "so nothing can build for it" % header)

    board_envs = {}
    for name, body in sorted(sections.items()):
        if not name.startswith("env:"):
            continue
        env = name[4:]
        if env in BOARDLESS_ENVS:
            continue
        used = re.findall(r"\$\{(board_\w+)\.build_flags\}", body)
        if len(used) != 1:
            problems.append(
                "platformio.ini [%s] pulls in %d board sections; an environment "
                "targets exactly one board" % (name, len(used)))
        elif used[0] not in boards:
            problems.append("platformio.ini [%s] extends [%s], which does not exist"
                            % (name, used[0]))
        else:
            board_envs.setdefault(used[0], []).append(env)

    check_reachable(problems, boards, board_envs)


def check_reachable(problems, boards, board_envs):
    """A board that can be supported has to be flashable from the web page.

    "Supported" is not a private fact about this repository. Someone who owns
    the board should be able to put the firmware on it from
    iamankushpandit.github.io/Gume without a toolchain -- so a [board_*]
    section that the site cannot offer, or that CI never builds, is an
    unfinished port rather than a supported board. gen_site.py derives its
    picker from these same sections; this check is what stops one being added
    without the label and the CI build that make the offer real.
    """
    site = read("tools", "gen_site.py")
    offered = set(re.findall(r'"env":\s*"(\w+)"', site))
    labelled = set(re.findall(r'^\s{4}"(\w+)":\s*\{', site, re.M))

    workflows = {}
    for name in ("ci.yml", "pages.yml"):
        try:
            workflows[name] = read(".github", "workflows", name)
        except OSError:
            problems.append(".github/workflows/%s is missing" % name)

    for board in sorted(boards):
        board_id = board[len("board_"):]
        envs = board_envs.get(board, [])

        if board_id not in labelled:
            problems.append(
                "board '%s' has no entry in gen_site.py's BOARD_DETAILS, so the "
                "web installer has no label for it and the site will not "
                "generate" % board_id)

        games = [env for env in envs if env in offered]
        if not games:
            problems.append(
                "board '%s' has no environment offered by the web installer; a "
                "board that can be supported must be flashable from the page, "
                "so add one to gen_site.py's VARIANTS or say why the board is "
                "not supported" % board_id)

        for env in games:
            for name, text in sorted(workflows.items()):
                if not re.search(r"pio run\b[^\n]*-e %s\b" % re.escape(env), text):
                    problems.append(
                        "board '%s' is offered as firmware '%s', but "
                        ".github/workflows/%s never builds it -- its manifest "
                        "would point at binaries that do not exist"
                        % (board_id, env, name))

        for env in envs:
            role = pack_release.env_role(env, board_id)
            if role not in pack_release.ENV_BLURBS:
                problems.append(
                    "board '%s' declares environment '%s', which "
                    "pack_release.py has no description for -- "
                    "`pack_release.py --strict` refuses to pack it, so the "
                    "release workflow would fail on the tag, after the tag "
                    "has already been pushed" % (board_id, env))


def check_agreement(problems, section, body, relative):
    """The panel is described to TFT_eSPI and to us; compare the two."""
    header = read("include", *relative.split("/"))
    labelled = {}
    for match in re.finditer(r"/\*\s*(\w+)\s*\*/\s*([^,\n]+),", header):
        labelled[match.group(1)] = match.group(2).strip()

    pairs = (("TFT_WIDTH", "nativeWidth"),
             ("TFT_HEIGHT", "nativeHeight"),
             ("TFT_BL", "backlightPin"))
    for macro, field in pairs:
        found = re.search(r"-D\s*%s\s*=\s*(-?\d+)" % macro, body)
        if not found or field not in labelled:
            continue
        stated = labelled[field].rstrip("f")
        if stated != found.group(1):
            problems.append(
                "platformio.ini [%s] says %s=%s but include/%s says %s=%s"
                % (section, macro, found.group(1), relative, field, stated))


def main():
    problems = []
    fields, groups = profile_contract()
    if not fields:
        problems.append("parsed no fields out of include/BoardProfile.h")
    headers = check_headers(problems, fields, groups)
    check_ini(problems, headers)

    if problems:
        print("Board descriptions have drifted:\n")
        for problem in problems:
            print("  - %s" % problem)
        print("\n%d problem(s). See docs/PORTING.md." % len(problems))
        return 1
    print("Board check: clean (%d board(s), %d profile field(s))."
          % (len(headers), len(fields)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
