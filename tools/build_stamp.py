"""Stamp the branch and commit a build came from into the firmware.

A PlatformIO pre-build script, wired in from `[esp32_common]` so every
environment gets it. It answers one question that the version number cannot:
*which* build is on this board. `BRAINO_VERSION` is 5.0.0 across dozens of
flashes; the branch and the commit are what tell you whether the thing in your
hand is the release, or the branch you were mid-way through an hour ago.

Read `include/BuildStamp.h` before changing anything here -- the two halves of
this are deliberately split, and the reason is not obvious.

## Why only the branch and the commit are injected, and not the time

The obvious implementation stamps the clock in here too, as a third `-D`. Do
not: it makes every build a full rebuild, permanently.

PlatformIO hashes the build flags into the build signature, so changing any
`-D` invalidates every object file in the tree. A timestamp changes on every
single invocation by definition, so `pio run` would stop being incremental --
about 100 seconds a time on this project instead of a few, for everybody,
forever. The clock is therefore taken from the compiler's own `__DATE__` and
`__TIME__` inside `src/BuildStamp.cpp`, and this script simply deletes that one
object file so it is always recompiled. One file, not nine hundred.

The branch and commit *are* injected as flags, and they do cost a full rebuild
when they change. That is the right trade: they change when you move to a
different commit, which is when the tree needed rebuilding anyway.

## Where the values come from

`git` in the project directory, except on GitHub Actions, which checks out a
detached HEAD -- `git rev-parse --abbrev-ref HEAD` there says "HEAD" rather
than the branch anyone means. `GITHUB_HEAD_REF` (set on pull requests) and
`GITHUB_REF_NAME` (set on pushes) carry the real name, so they win when set.

A build from a source tarball has no `.git` at all. That is a legitimate way to
build this firmware, so a missing git is not an error: the stamp reads
"unknown", the About page says so plainly, and the build proceeds.
"""

import os
import subprocess

Import("env")  # noqa: F821 -- injected by PlatformIO/SCons

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821

UNKNOWN = "unknown"


def git(*args):
    """Run a git command in the project dir, or return None if it cannot."""
    try:
        out = subprocess.run(
            ("git",) + args,
            cwd=PROJECT_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    text = out.stdout.decode("utf-8", "replace").strip()
    return text or None


def branch_name():
    """The branch, preferring CI's environment over a detached HEAD."""
    for var in ("GITHUB_HEAD_REF", "GITHUB_REF_NAME"):
        value = os.environ.get(var, "").strip()
        if value:
            return value
    name = git("rev-parse", "--abbrev-ref", "HEAD")
    if not name or name == "HEAD":
        # Detached outside CI -- a bisect, or a `git switch --detach`. The tag
        # or the commit is more use than the word "HEAD".
        return git("describe", "--tags", "--exact-match") or UNKNOWN
    return name


def short_commit():
    return git("rev-parse", "--short=7", "HEAD") or UNKNOWN


def stamp():
    branch = branch_name()
    commit = short_commit()

    env.Append(  # noqa: F821
        CPPDEFINES=[
            ("GUME_BUILD_BRANCH", env.StringifyMacro(branch)),  # noqa: F821
            ("GUME_BUILD_COMMIT", env.StringifyMacro(commit)),  # noqa: F821
        ]
    )

    # Force src/BuildStamp.cpp to recompile so its __DATE__/__TIME__ are this
    # build's, not whenever it last happened to be built. Removing the object
    # is enough; SCons rebuilds what is missing.
    obj = os.path.join(env.subst("$BUILD_DIR"), "src", "BuildStamp.cpp.o")  # noqa: F821
    try:
        os.remove(obj)
    except OSError:
        pass  # First build, or a variant that does not compile it.

    print("Build stamp: %s @ %s" % (branch, commit))


stamp()
