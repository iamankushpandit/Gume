#!/usr/bin/env python3
"""Split a screen's render() into renderStatic() / renderDynamic().

    python tools/split_render.py                 # dry run, says what it would do
    python tools/split_render.py --apply         # write the changes
    python tools/split_render.py --apply MathGame FlagGame

Why a script at all
-------------------
Nineteen of the playable screens are the same layout with a different question
in it: header counters, a question panel, four answer buttons, a feedback line.
Splitting each one by hand is the same edit nineteen times, and doing the same
edit nineteen times by hand is how one of them ends up subtly different from
the other eighteen.

What it does, and what it deliberately does NOT do
--------------------------------------------------
It performs only the part of the conversion that is mechanically SAFE:

    renderStatic()   Ui::clear() + the top bar. Nothing else.
    renderDynamic()  a fill of the body region, then the whole of the old body.

That is behaviour-preserving. The body is still redrawn in full and still
painted onto cleared ground, so nothing can be left behind; what is saved is
the top bar -- which costs a battery read and five glyphs -- and the strip of
clear above the content, on every repaint that is not a full one.

It does NOT try to work out which parts of the body are static. That is the
question the audit in docs/RENDER_AUDIT.md exists to answer, it has a different
answer for every screen, and getting it wrong leaves stale pixels that no build
and no checker will catch. Each converted file gets a comment pointing at the
audit so the second, larger half of the job is discoverable rather than
forgotten.

How it refuses
--------------
It rewrites a file only when the render() body opens with EXACTLY:

    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

and the header declares exactly `void render(AppContext& host) override;`.
Anything else -- a screen that already guards on needsFullRender(), one that
reads the panel size first, one that branches before it clears -- is skipped
with a reason and left alone. A screen that is skipped is a screen that needs a
person, which is the point.
"""

import argparse
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMES = os.path.join(ROOT, "src", "games")

PROLOGUE = (
    "    Ui::Renderer& tft = host.display();\n"
    "    Ui::clear(tft);\n"
    "    host.drawTopBar(title());\n"
)

HEADER_DECL = "    void render(AppContext& host) override;"

HEADER_NEW = """    /* Two-phase render. renderStatic() is the clear and the top bar;
     * renderDynamic() is everything else. Split mechanically by
     * tools/split_render.py -- see the note in the .cpp. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;"""

BODY_NOTE = """    /* Split mechanically, and only half done.
     *
     * What this saves is the top bar -- a battery read and five glyphs -- and
     * the strip of clear above the content, on every repaint that is not a
     * full one. What it does NOT yet do is leave the question panel alone when
     * only the answer buttons change, which is the win this screen actually
     * has: answering recolours four buttons and rewrites one line, and the
     * question above them does not move.
     *
     * Doing that needs the region cleared below to shrink to the parts that
     * change, and the next-question path to call markFullDirty(). See this
     * screen's entry in docs/RENDER_AUDIT.md before attempting it: the traps
     * are text that gets narrower, things that stop being drawn, and
     * right-aligned text whose stale characters are left at the LEFT end. */
    tft.fillRect(0, TOP_BAR_HEIGHT, GAME_CANVAS_WIDTH,
                 GAME_CANVAS_HEIGHT - TOP_BAR_HEIGHT, Ui::bg());
"""


def split_one(name, apply_changes):
    cpp_path = os.path.join(GAMES, name + ".cpp")
    h_path = os.path.join(GAMES, name + ".h")
    if not os.path.exists(cpp_path) or not os.path.exists(h_path):
        return "no such screen"

    cpp = io.open(cpp_path, encoding="utf-8").read()
    hdr = io.open(h_path, encoding="utf-8").read()

    if "::renderStatic" in cpp:
        return "already split"
    if HEADER_DECL not in hdr:
        return "header does not declare render(AppContext&) exactly"

    open_re = re.compile(r"void %s::render\(AppContext& host\) \{\n" % re.escape(name))
    m = open_re.search(cpp)
    if not m:
        return "no render(AppContext&) definition"

    # Find the closing brace of the function: the first line that is exactly "}".
    end = cpp.find("\n}\n", m.end())
    if end < 0:
        return "could not find the end of render()"
    body = cpp[m.end():end + 1]

    if not body.startswith(PROLOGUE):
        first = body.split("\n")[0].strip()
        return "prologue is not the standard three lines (starts %r)" % first[:48]

    rest = body[len(PROLOGUE):]
    if "needsFullRender" in rest:
        return "already reasons about full vs partial"

    new_fns = (
        "void %s::renderStatic(AppContext& host) {\n"
        "    Ui::Renderer& tft = host.display();\n"
        "    Ui::clear(tft);\n"
        "    host.drawTopBar(title());\n"
        "}\n"
        "\n"
        "void %s::renderDynamic(AppContext& host) {\n"
        "    Ui::Renderer& tft = host.display();\n"
        "%s"
        "%s"
        "}\n"
    ) % (name, name, BODY_NOTE, rest)

    if apply_changes:
        io.open(cpp_path, "w", encoding="utf-8").write(
            cpp[:m.start()] + new_fns + cpp[end + 3:])
        io.open(h_path, "w", encoding="utf-8").write(
            hdr.replace(HEADER_DECL, HEADER_NEW, 1))
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("screens", nargs="*", help="screen names, or all of src/games")
    ap.add_argument("--apply", action="store_true", help="write the changes")
    args = ap.parse_args()

    names = args.screens
    if not names:
        names = sorted(
            f[:-4] for f in os.listdir(GAMES)
            if f.endswith(".cpp") and os.path.exists(os.path.join(GAMES, f[:-4] + ".h")))

    done, skipped = [], []
    for n in names:
        why = split_one(n, args.apply)
        (skipped if why else done).append((n, why))

    for n, why in skipped:
        print("  skip  %-22s %s" % (n, why))
    for n, _ in done:
        print("  %s  %s" % ("SPLIT" if args.apply else "would", n))
    print("\n%d %s, %d left alone."
          % (len(done), "split" if args.apply else "would be split", len(skipped)))
    if not args.apply and done:
        print("Re-run with --apply to write. Then BUILD, and look at each screen "
              "on a panel: a partial-repaint fault compiles cleanly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
