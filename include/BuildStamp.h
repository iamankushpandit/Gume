#pragma once

/* Which build is this, and when was it made.
 *
 * `BRAINO_VERSION` answers "which release"; it stays 5.0.0 across dozens of
 * flashes and cannot tell you whether the firmware in your hand is the release
 * or the branch you were part-way through an hour ago. These three answer that.
 *
 * The values are read at build time by `tools/build_stamp.py`, and this is the
 * only place in the firmware that names their macros. Same rule as the game
 * list and the product name: derive, do not restate. Nothing may type a commit
 * or a branch into a screen.
 *
 * ## Why the time is not a build flag like the other two
 *
 * PlatformIO folds the build flags into its build signature, so a `-D` whose
 * value changes on every invocation -- which a timestamp does, by definition --
 * invalidates every object in the tree and makes `pio run` a full rebuild every
 * time, for everyone. So the clock comes from the compiler's own `__DATE__`
 * and `__TIME__` in `src/BuildStamp.cpp`, and the script deletes that one
 * object file to keep them honest. The full reasoning is in the script.
 *
 * The consequence worth knowing: the time is the build machine's local clock in
 * C's own format ("Aug 26 2026 14:28"), not ISO and not UTC. It identifies a
 * build; it is not a timestamp to compute with.
 *
 * ## When git is not there
 *
 * Building from a source tarball with no `.git` is legitimate and stays
 * supported: the stamp reads "unknown" and the About page says so rather than
 * inventing a plausible-looking commit. */

#ifndef GUME_BUILD_BRANCH
/* Reached when the firmware is built by something other than the PlatformIO
 * script -- an IDE indexer, a unit-test harness, someone's own makefile. A
 * wrong answer here would be worse than no answer. */
#define GUME_BUILD_BRANCH "unknown"
#endif

#ifndef GUME_BUILD_COMMIT
#define GUME_BUILD_COMMIT "unknown"
#endif

namespace BuildStamp {

/* The branch the build came from, or "unknown". */
const char* branch();

/* Seven-character abbreviated commit, or "unknown". */
const char* commit();

/* Build machine's local clock, C format: "Aug 26 2026 14:28". */
const char* builtAt();

/* "<branch> @ <commit>" -- the one-line form the screens and the boot log use,
 * assembled once into a fixed buffer rather than concatenated per render. */
const char* describe();

}  // namespace BuildStamp
