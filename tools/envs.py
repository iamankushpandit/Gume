#!/usr/bin/env python3
"""Which PlatformIO environments are the product, and which are bench tools.

`platformio.ini` declares fifteen environments and only five of them are
Braino!. The other ten are hardware probes -- bringup, batdiag, audiodiag,
wifidiag, s3diag, diag4, diag32p and their per-board copies -- and they exist to
be flashed at a board on a desk when something is wrong with it. Nobody
downloads one from the installer, and nobody wants one in a release.

Every workflow used to name its own list. Three lists, hand-kept, and they had
already drifted: ci.yml and pages.yml both built eighteen environments and
neither of them built `audiodiag`, `diag32p` or `audiodiag_e32r32p`, which had
been added months earlier -- exactly the "an environment nobody builds is an
environment that is already broken and has not been told yet" failure pages.yml
warns about in its own comment. release.yml derived its list and so published
fourteen diagnostic images per release that nothing pointed at.

So the list lives in `platformio.ini`, beside the environments, as
`custom_env_kind`. Every environment must declare one -- an unclassified
environment is an error rather than a default, because the failure of a default
is silent in whichever direction it guesses.

    python tools/envs.py --product              app app_e32r32p ...
    python tools/envs.py --product --pio-args   -e app -e app_e32r32p ...
    python tools/envs.py --diagnostic
    python tools/envs.py --all
    python tools/envs.py --for-changes file [file ...]

`--for-changes` is what CI asks: given the paths a push or a pull request
touched, which environments actually need building? The product side answers
conservatively (a board header or platformio.ini means all of them); the
diagnostic side answers from each environment's own `build_src_filter`, so
touching `src/battery_diag.cpp` builds the four batdiag environments and
nothing else.
"""

import argparse
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PRODUCT = "product"
DIAGNOSTIC = "diagnostic"
KINDS = (PRODUCT, DIAGNOSTIC)

# A change to any of these changes every board's build, so every product
# environment is rebuilt rather than reasoned about. `src/hal/` is here because
# it is where the board profile is consumed; `include/boards/` because it is
# where the board is described.
PRODUCT_WIDE = (
    "platformio.ini",
    "include/boards/",
    "include/BoardProfile.h",
    "include/BoardConfig.h",
    "src/hal/",
)

# The diagnostics sweep on a narrower trigger, and deliberately so. Their build
# flags live in platformio.ini and nowhere else, so that file moving is the one
# change that can break all of them at once. A board header is NOT here: a
# probe that composes a board section compiles the same header the product
# environment for that board already compiles, so the product build is the test
# and building the probe again would only be slower at saying so.
DIAGNOSTIC_WIDE = ("platformio.ini",)

# The reference product environment. A change under src/ or include/ that is
# not board-wide still has to compile, and compiling it once is what a pull
# request needs; the other six boards differ only in flags and are covered on
# the push to dev.
REFERENCE_ENV = "app"

# A bringup environment builds the whole tree with -D CYD_BRINGUP_ONLY, so its
# build_src_filter names no file of its own. What is actually distinct about it
# is the branch in main.cpp that the macro selects -- everything else it
# compiles is compiled again by the app environment for the same board.
BRINGUP_SOURCE = "src/main.cpp"


def read_ini():
    with io.open(os.path.join(ROOT, "platformio.ini"), encoding="utf-8") as handle:
        return handle.read()


def die(message):
    sys.stderr.write("envs: %s\n" % message)
    raise SystemExit(1)


def sections(ini):
    """Every [env:*] with its body, in the order platformio.ini declares them."""
    found = []
    for match in re.finditer(r"^\[env:(\w+)\](.*?)(?=^\[|\Z)", ini, re.M | re.S):
        found.append((match.group(1), match.group(2)))
    return found


def classify(ini=None):
    """{env: kind}, and a hard error for anything that has not said which.

    Returned in declaration order so every list this module prints is stable --
    a set here would reorder the build command between Python versions and make
    two identical CI runs look different.
    """
    ini = read_ini() if ini is None else ini
    kinds, problems = {}, []
    for env, body in sections(ini):
        match = re.search(r"^custom_env_kind\s*=\s*(\w+)\s*$", body, re.M)
        if not match:
            problems.append(
                "[env:%s] does not declare `custom_env_kind`. Say `product` if "
                "it is Braino! for a board, or `diagnostic` if it is a bench "
                "probe -- nothing builds or releases an environment that has "
                "not said which it is." % env)
            continue
        if match.group(1) not in KINDS:
            problems.append("[env:%s] has custom_env_kind = %s; expected one of %s"
                            % (env, match.group(1), ", ".join(KINDS)))
            continue
        kinds[env] = match.group(1)
    if problems:
        die("\n  ".join(["platformio.ini:"] + problems))
    if not kinds:
        die("platformio.ini declares no [env:*] sections")
    return kinds


def board_name(env, ini=None):
    """The BOARD_NAME an environment builds for, or None for a boardless probe.

    Followed through the `${board_<id>.build_flags}` the environment composes,
    to the `BOARD_NAME` that board section states -- the same fact the
    firmware prints as `[boot] board=`, read from the one place it is written.
    Used to name CI jobs after the board, so a red job says which board broke.
    """
    ini = read_ini() if ini is None else ini
    body = dict(sections(ini)).get(env)
    if body is None:
        return None
    ref = re.search(r"\$\{board_(\w+)\.build_flags\}", body)
    if not ref:
        return None
    section = re.search(r"^\[board_%s\](.*?)(?=^\[|\Z)" % re.escape(ref.group(1)),
                        ini, re.M | re.S)
    if not section:
        return None
    name = re.search(r'BOARD_NAME=\\"([^\\"]+)\\"', section.group(1))
    return name.group(1) if name else None


def of_kind(kind, ini=None):
    kinds = classify(ini)
    return [env for env, value in kinds.items() if value == kind]


def diagnostic_sources(ini=None):
    """{env: [paths it is the only build of]} for the diagnostic environments.

    Derived from `build_src_filter`, so a probe that grows a second file is
    picked up without a second list to keep. An environment whose filter is the
    whole tree is a bringup build, and what is distinct about it is main.cpp.
    """
    ini = read_ini() if ini is None else ini
    kinds = classify(ini)
    sources = {}
    for env, body in sections(ini):
        if kinds.get(env) != DIAGNOSTIC:
            continue
        match = re.search(r"^build_src_filter\s*=\s*(.+)$", body, re.M)
        included = re.findall(r"\+<([^>]+)>", match.group(1)) if match else []
        named = [name for name in included if name != "*"]
        sources[env] = (["src/" + name for name in named] if named
                        else [BRINGUP_SOURCE])
    return sources


def for_changes(paths, ini=None, with_product=False):
    """Which environments a set of changed paths makes it worth building.

    `with_product` unions in every product environment regardless of the diff.
    That is what a push to main or dev asks for: those are the branches the
    installer and the release are cut from, so every product board is proven
    there whatever the diff looked like. The diagnostic half is still selected
    by the change, which is the whole saving.
    """
    ini = read_ini() if ini is None else ini
    kinds = classify(ini)
    paths = [path.replace("\\", "/") for path in paths]
    wanted = []

    def add(env):
        if env in kinds and env not in wanted:
            wanted.append(env)

    sources = diagnostic_sources(ini)
    # A file that ONLY a probe compiles is not firmware source as far as the
    # console is concerned. Without this, editing src/battery_diag.cpp built
    # env:app to prove that a file it does not contain still compiles.
    probe_only = {path for env, files in sources.items() for path in files
                  if path != BRINGUP_SOURCE}

    def touched(prefixes):
        return any(path == prefix or path.startswith(prefix)
                   for path in paths for prefix in prefixes)

    product_wide = touched(PRODUCT_WIDE)
    code = any(path not in probe_only
               and not re.match(r"^(docs/|cases/|site/|tools/|\.github/|LICENSE$)", path)
               and not path.endswith(".md") for path in paths)

    for env, kind in kinds.items():
        if kind == PRODUCT and (with_product or product_wide
                                or (code and env == REFERENCE_ENV)):
            add(env)

    # A probe is built when its own source moves, or when the file its flags
    # live in does. Not otherwise: that is the whole point.
    diagnostic_wide = touched(DIAGNOSTIC_WIDE)
    for env, files in sources.items():
        if diagnostic_wide or any(path in files for path in paths):
            add(env)

    return [env for env in kinds if env in wanted]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--product", action="store_const", const=PRODUCT,
                       dest="kind", help="the environments that are Braino!")
    group.add_argument("--diagnostic", action="store_const", const=DIAGNOSTIC,
                       dest="kind", help="the bench probes")
    group.add_argument("--all", action="store_true", help="every environment")
    group.add_argument("--for-changes", nargs="*", metavar="PATH",
                       help="what these changed paths make worth building; "
                            "reads one path per line from stdin if none given")
    parser.add_argument("--with-product", action="store_true",
                        help="with --for-changes: union in every product "
                             "environment, whatever the change was")
    parser.add_argument("--pio-args", action="store_true",
                        help="print as `-e name` arguments for `pio run`")
    parser.add_argument("--matrix", action="store_true",
                        help="print a JSON list of {env, board, name} for a CI "
                             "matrix; name is what the job is called")
    args = parser.parse_args()

    ini = read_ini()
    if args.all:
        envs = list(classify(ini))
    elif args.for_changes is not None:
        paths = args.for_changes or [line.strip() for line in sys.stdin
                                     if line.strip()]
        envs = for_changes(paths, ini, with_product=args.with_product)
    else:
        envs = of_kind(args.kind, ini)

    if args.matrix:
        # "E32R28T-1 (app)": the board first, because that is what a person
        # reading a red job needs; the environment beside it, because that is
        # what they type to reproduce it. A boardless probe is its env alone.
        rows = []
        for env in envs:
            board = board_name(env, ini)
            rows.append({"env": env, "board": board or "",
                         "name": "%s (%s)" % (board, env) if board else env})
        print(json.dumps(rows))
    elif args.pio_args:
        # Nothing to build is not an empty `pio run` -- that builds default_envs.
        # The caller has to be able to see the difference, so print nothing and
        # let it test for an empty string.
        print(" ".join("-e %s" % env for env in envs))
    else:
        print(" ".join(envs))


if __name__ == "__main__":
    main()
