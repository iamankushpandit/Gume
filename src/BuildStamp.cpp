#include "BuildStamp.h"

#include <stdio.h>

/* This translation unit exists to hold `__DATE__` and `__TIME__`, and it is
 * deleted from the build directory before every build by
 * `tools/build_stamp.py` so those two are always this build's. Keep it small:
 * it is recompiled every single time, unlike the rest of the tree.
 *
 * Nothing here allocates. The strings are literals or one fixed buffer, per the
 * memory rule -- these are read by About and by System Info's Device tab, and a
 * screen that rebuilds a `String` to say what it is would be an odd place to
 * start churning the heap. */

namespace BuildStamp {

const char* branch() {
    return GUME_BUILD_BRANCH;
}

const char* commit() {
    return GUME_BUILD_COMMIT;
}

const char* describe() {
    /* Adjacent string literals, so this is one constant in flash rather than a
     * concatenation done at runtime. */
    return GUME_BUILD_BRANCH " @ " GUME_BUILD_COMMIT;
}

const char* builtAt() {
    /* __DATE__ is "Aug 26 2026" and __TIME__ is "14:28:33". The seconds are
     * noise for identifying a build and cost width on a 240px panel, so they
     * are dropped -- which is the only reason this needs a buffer at all.
     *
     * Built once on first call and kept. 18 bytes of static RAM against a
     * rebuild on every render, which is the trade the memory rule asks for. */
    static char text[18];
    if (text[0] == '\0') {
        snprintf(text, sizeof(text), "%s %.5s", __DATE__, __TIME__);
    }
    return text;
}

}  // namespace BuildStamp
