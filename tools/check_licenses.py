#!/usr/bin/env python3
"""Every file that can carry a licence notice carries one.

The repository has been GPL-3.0-or-later since it was published, and LICENSE,
NOTICE.md and the README all say so. None of that travels. A licence at the
root of a repository is a claim about the repository; the thing that actually
reaches a stranger is a file -- one .cpp pasted into a forum answer, one tool
script copied into another tree, one page of documentation lifted into a wiki.
Split from its repository, an unmarked file carries no author, no terms and no
way back to either, and whoever took it is not being dishonest -- they simply
have nothing to go on.

So every text file that supports a comment states four things itself: the
licence (as an SPDX identifier, which tooling can read), the copyright holder,
where the work came from, and what reuse requires. That is the whole of it.
It grants nothing new and takes nothing back: it is the licence the project
already had, written where it can be seen.

Run it as a check:

    python tools/check_licenses.py

and as its own fix, which is the point of writing it as a tool rather than as
a rule in CLAUDE.md -- a rule that has to be remembered 250 times is a rule
that will be missed:

    python tools/check_licenses.py --fix

WHAT IS EXEMPT, AND WHY. Three kinds of file, each for a reason rather than
for convenience:

  - LICENSE itself. It is the FSF's text and may not be modified.
  - Anything with no comment syntax: JSON, and binary assets (PNG, STL).
    A notice cannot be put in a file with nowhere to put it. The JSON here is
    configuration examples and one generated baseline, so nothing original is
    at risk in them.
  - PlatformIO's stub READMEs in include/, lib/ and test/, which are the
    toolchain's boilerplate rather than this project's writing.

WHAT GENERATED FILES DO. A generated source file carries the same header as a
handwritten one, emitted by its generator, so that regenerating it cannot
quietly strip the notice. The generators are checked here too -- they are
original work in their own right.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

HOLDER = "iamankushpandit"
YEAR = "2026"
PROJECT = "Braino!"
REPO = "https://github.com/iamankushpandit/Gume"

# The notice, as plain lines, rendered into whichever comment syntax the file
# speaks. An empty string is a blank comment line.
NOTICE = [
    "SPDX-License-Identifier: GPL-3.0-or-later",
    "SPDX-FileCopyrightText: Copyright (C) {year} {holder} "
    "<https://github.com/{holder}>".format(year=YEAR, holder=HOLDER),
    "",
    "Part of {project} -- {repo}".format(project=PROJECT, repo=REPO),
    "Free software under GPL-3.0-or-later. Reusing any part of this file, in",
    "any work, must keep this notice, credit {holder} as the".format(holder=HOLDER),
    "author, and stay under the same licence with corresponding source",
    "offered. See LICENSE and NOTICE.md.",
]

# The same four facts for a document, where they have to be readable rather
# than merely present: a reader lifting a paragraph out of a rendered page
# never sees an HTML comment.
MARKDOWN_FOOTER = (
    "<!-- SPDX-License-Identifier: GPL-3.0-or-later -->\n"
    "<!-- SPDX-FileCopyrightText: Copyright (C) {year} {holder} -->\n"
    "\n"
    "---\n"
    "\n"
    "*Part of [{project}]({repo}) by [{holder}](https://github.com/{holder}). "
    "Copyright © {year} {holder}, licensed "
    "[GPL-3.0-or-later]({repo}/blob/main/LICENSE) alongside the code — "
    "reuse of this document, in whole or in part, must keep this attribution "
    "and stay under the same licence. See "
    "[NOTICE.md]({repo}/blob/main/NOTICE.md).*\n"
).format(year=YEAR, holder=HOLDER, project=PROJECT, repo=REPO)

MARKER = "SPDX-License-Identifier"

# extension -> (line prefix, block open, block close)
SLASH = ("// ", None, None)
HASH = ("# ", None, None)
SEMI = ("; ", None, None)
XML = ("     ", "<!--", "-->")

STYLES = {
    ".c": SLASH, ".cpp": SLASH, ".cc": SLASH, ".h": SLASH, ".hpp": SLASH,
    ".py": HASH, ".yml": HASH, ".yaml": HASH, ".sh": HASH,
    ".ini": SEMI,
    ".html": XML, ".svg": XML,
}

# Handled by a footer rather than a header: a comment block before the first
# heading pushes the title down in some renderers, and YAML frontmatter has to
# stay on line 1.
MARKDOWN = {".md"}

EXEMPT_PATHS = {
    "LICENSE",
    "include/README",
    "lib/README",
    "test/README",
    ".gitignore",
}

# No comment syntax exists in these; see the module docstring.
EXEMPT_EXTS = {".json", ".png", ".stl", ".jpg", ".jpeg", ".gif", ".bin", ".ttf"}


def prologue_len(lines, ext):
    """How many lines must stay first in their file. A notice goes after them."""
    n = 0
    if lines and lines[0].startswith("#!"):
        n = 1
    if ext in (".html", ".svg"):
        while n < len(lines) and (
            lines[n].lstrip().lower().startswith("<?xml")
            or lines[n].lstrip().lower().startswith("<!doctype")
        ):
            n += 1
    return n


def render(style):
    prefix, opener, closer = style
    out = []
    if opener:
        out.append(opener)
    for line in NOTICE:
        out.append((prefix + line).rstrip())
    if closer:
        out.append(closer)
    return out


def header_for(ext):
    """The notice as a comment block, for a generator to emit into its output.

    Generators call this rather than carrying their own copy of the wording,
    so the notice a regenerated file gets is the notice this checker looks
    for. Two copies of a licence header is one copy that will fall behind.
    """
    return "\n".join(render(STYLES[ext])) + "\n"


def markdown_footer():
    """The document notice, for a generator that writes Markdown."""
    return MARKDOWN_FOOTER


def tracked_files():
    out = subprocess.run(
        ["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout
    return [p for p in out.splitlines() if p]


def classify(path):
    """Return 'header', 'markdown' or None (exempt)."""
    if path in EXEMPT_PATHS:
        return None
    ext = os.path.splitext(path)[1].lower()
    if ext in EXEMPT_EXTS:
        return None
    if ext in MARKDOWN:
        return "markdown"
    if ext in STYLES:
        return "header"
    return None


def fix_header(text, ext):
    lines = text.split("\n")
    n = prologue_len(lines, ext)
    head = lines[:n]
    rest = lines[n:]
    while rest and rest[0].strip() == "":
        rest.pop(0)
    return "\n".join(head + render(STYLES[ext]) + [""] + rest)


def fix_markdown(text):
    return text.rstrip("\n") + "\n\n" + MARKDOWN_FOOTER


def main():
    ap = argparse.ArgumentParser(description="Check every file carries a licence notice.")
    ap.add_argument("--fix", action="store_true",
                    help="write the missing notices instead of listing them")
    args = ap.parse_args()

    missing = []
    fixed = 0
    for rel in tracked_files():
        kind = classify(rel)
        if not kind:
            continue
        full = os.path.join(ROOT, rel)
        if not os.path.isfile(full):
            continue
        with open(full, "r", encoding="utf-8") as handle:
            text = handle.read()
        if MARKER in text:
            continue
        if not args.fix:
            missing.append(rel)
            continue
        ext = os.path.splitext(rel)[1].lower()
        new = fix_markdown(text) if kind == "markdown" else fix_header(text, ext)
        with open(full, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(new)
        fixed += 1

    if args.fix:
        print("check_licenses: added a notice to {0} file(s).".format(fixed))
        return 0

    if missing:
        sys.stderr.write(
            "check_licenses: {0} file(s) carry no licence notice:\n\n".format(
                len(missing)))
        for rel in missing:
            sys.stderr.write("  {0}\n".format(rel))
        sys.stderr.write("\nRun: python tools/check_licenses.py --fix\n")
        return 1

    print("check_licenses: every eligible file carries a notice.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
