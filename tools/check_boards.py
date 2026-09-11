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
BOARDLESS_ENVS = ("wifidiag", "s3diag", "diag4", "diag32p", "audiodiag")


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_release            # noqa: E402  (path set immediately above)
import envs as env_kinds       # noqa: E402


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

    check_reachable(problems, boards, board_envs, text)
    check_boardless_blurbs(problems, text)
    check_workflow_envs(problems, text)


def check_workflow_envs(problems, ini):
    """Every environment a workflow names has to exist, and every workflow that
    builds firmware has to ask tools/envs.py which environments those are.

    THE FIRST HALF EXISTS BECAUSE A DANGLING `-e` SHIPPED AND BROKE THE SITE.
    Removing a board from `.github/workflows/pages.yml` left

        -e app_e32r40t -e -e app_e32r32p

    which PlatformIO rejects with "Got unexpected extra argument". CI went
    green anyway, because a pull request built a SELECTIVE list through a shell
    variable and only the full-build path carried that flag list -- so the
    fault was latent in ci.yml and fatal in pages.yml, which always built
    everything. The Pages deploy failed 49 seconds in, the site silently stayed
    on the previous release, and the first anybody knew was noticing the game
    count was wrong on the published page.

    THE SECOND HALF EXISTS BECAUSE THE LISTS THEMSELVES WERE WRONG. ci.yml and
    pages.yml each named eighteen environments and neither named `audiodiag`,
    `diag32p` or `audiodiag_e32r32p`; all three had been in platformio.ini for
    months, and pages.yml's own comment argued that an environment nobody
    builds is one that is already broken and has not been told yet. Checking
    that a hand-kept list parses does not check that it is complete, and no
    check can: the list has to be derived. So the workflows call
    `tools/envs.py`, and this is what notices if one of them stops.
    """
    envs = set(re.findall(r"^\[env:([\w.-]+)\]", ini, re.M))
    if not envs:
        problems.append("parsed no [env:*] sections out of platformio.ini")
        return

    # An environment that has not said whether it is product or diagnostic is
    # an environment nothing can decide to build. envs.py exits on that, which
    # would take every consumer down with it, so ask here where the message
    # lands with the other problems.
    try:
        env_kinds.classify(ini)
    except SystemExit:
        problems.append(
            "platformio.ini has an [env:*] with no `custom_env_kind` -- see "
            "tools/envs.py. Run `python tools/envs.py --all` for the detail.")

    for name in ("ci.yml", "pages.yml", "release.yml"):
        try:
            text = read(".github", "workflows", name)
        except OSError:
            continue                     # check_reachable() reports the absence

        builds = [line for line in text.splitlines() if "pio run" in line]
        if builds and "tools/envs.py" not in text:
            problems.append(
                ".github/workflows/%s runs `pio run` without asking "
                "tools/envs.py which environments to build. A list of "
                "environments written into a workflow is a list that goes "
                "stale silently -- three of them did." % name)

        for line in builds:
            # A -e with no environment after it. PlatformIO fails the whole
            # command, so this is a build that never happens.
            if re.search(r"-e\s+(?=-e)", line) or re.search(r"-e\s*$", line):
                problems.append(
                    ".github/workflows/%s has a `-e` with no environment after "
                    "it: PlatformIO rejects the whole command, so nothing in it "
                    "gets built. Usually left behind by deleting an env from "
                    "the list." % name)
                continue
            for env in re.findall(r"-e\s+([\w.-]+)", line):
                if env.startswith("$"):
                    continue             # derived, expanded at run time
                if env not in envs:
                    problems.append(
                        ".github/workflows/%s builds `-e %s`, which is not an "
                        "[env:*] in platformio.ini -- that build fails the "
                        "whole command." % (name, env))

        # Any surviving hand-kept list is still checked for typos.
        for listing in re.findall(r'ENVS="([^"$]+)"', text):
            for env in listing.split():
                if env not in envs:
                    problems.append(
                        ".github/workflows/%s lists env `%s` in ENVS, which is "
                        "not in platformio.ini." % (name, env))
        for listing in re.findall(r"for env in ([^;]+);", text):
            if "$(" in listing:
                continue                 # derived, expanded at run time
            for env in listing.split():
                if env.startswith("$"):
                    continue             # expanded at run time
                if env not in envs:
                    problems.append(
                        ".github/workflows/%s loops over env `%s`, which is "
                        "not in platformio.ini." % (name, env))


def check_reachable(problems, boards, board_envs, ini):
    """A board that can be supported has to be flashable from the web page.

    "Supported" is not a private fact about this repository. Someone who owns
    the board should be able to put the firmware on it from
    iamankushpandit.github.io/Gume without a toolchain -- so a [board_*]
    section that the site cannot offer, or that CI never builds, is an
    unfinished port rather than a supported board. gen_site.py derives its
    picker from these same sections; this check is what stops one being added
    without the label and the CI build that make the offer real.

    "CI builds it" is now a question about platformio.ini rather than about the
    text of a workflow: ci.yml and pages.yml ask tools/envs.py which
    environments are the product, so an environment is built exactly when it
    declares `custom_env_kind = product`. That is the fact this checks; that
    the workflows still ask is checked in check_workflow_envs().
    """
    site = read("tools", "gen_site.py")
    product_envs = set(env_kinds.of_kind(env_kinds.PRODUCT, ini))
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
            if env not in product_envs:
                problems.append(
                    "board '%s' is offered as firmware '%s', but [env:%s] is "
                    "not `custom_env_kind = product` -- CI and the Pages "
                    "workflow build the product environments, so its manifest "
                    "would point at binaries that do not exist"
                    % (board_id, env, env))

        for env in envs:
            if env not in product_envs:
                continue          # not published, so nothing has to describe it
            role = pack_release.env_role(env, board_id)
            if role not in pack_release.ENV_BLURBS:
                problems.append(
                    "board '%s' declares environment '%s', which "
                    "pack_release.py has no description for -- "
                    "`pack_release.py --strict` refuses to pack it, so the "
                    "release workflow would fail on the tag, after the tag "
                    "has already been pushed" % (board_id, env))


def check_boardless_blurbs(problems, ini):
    """Every PUBLISHED environment needs a description, boardless ones included.

    check_reachable() below walks the environments that compose a [board_*]
    section, which misses exactly the envs in BOARDLESS_ENVS -- and
    `pack_release.py --strict` does not miss them. That gap is not theoretical:
    env:diag32p shipped without a blurb, every check here reported clean, and
    the release workflow failed on `pack_release` AFTER the v5.6.0 tag had been
    pushed. Finding it at the tag is the worst possible moment, because the tag
    is the thing that is awkward to take back.

    It asks only of the `product` environments now, because those are the only
    ones a release attaches. A diagnostic that nothing publishes owes nobody a
    download description -- what it owes is a paragraph in CLAUDE.md saying what
    it is for, which is a different check and a different reader.
    """
    for env in env_kinds.of_kind(env_kinds.PRODUCT, ini):
        role = pack_release.env_role(env, "")
        if role in pack_release.ENV_BLURBS:
            continue
        # Board-attached envs are covered by check_reachable(), which knows the
        # board id and can strip the suffix. Only report what nothing else will.
        if any(env.endswith("_%s" % b) for b in board_ids(ini)):
            continue
        problems.append(
            "environment '%s' has no description in pack_release.py's "
            "ENV_BLURBS -- `pack_release.py --strict` refuses to pack it, so "
            "the release workflow would fail on the tag, after the tag has "
            "already been pushed" % env)


def board_ids(ini):
    """The board ids declared by [board_*] sections."""
    return re.findall(r"^\[board_(\w+)\]", ini, re.M)


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
