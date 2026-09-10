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
 * ## They arrive in a generated header, not as `-D`
 *
 * The script writes `GumeBuildStamp.h` into the build directory and
 * `src/BuildStamp.cpp` is the only file that includes it. They used to be
 * appended to `CPPDEFINES`, which reaches the Arduino core and NimBLE as well
 * as our own sources -- so changing the commit recompiled 336 objects and took
 * 333 s, against 66 s and one object for a build where nothing had changed.
 * The include path is a flag and is constant; the header's contents are not a
 * flag and are free to change. The full reasoning is in the script.
 *
 * ## Why the time is not in that header either
 *
 * It comes from the compiler's own `__DATE__` and `__TIME__` in
 * `src/BuildStamp.cpp`, which needs no mechanism at all, and the script
 * deletes that one object file so they are always this build's.
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

#ifndef GUME_BUILD_TIME
#define GUME_BUILD_TIME "unknown"
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
