#pragma once

/* Where the console looks to find out whether a newer firmware exists, and
 * where it tells the owner to go to install it.
 *
 * ------------------------------------------------------------------------
 * This is a notice, not an update
 * ------------------------------------------------------------------------
 * Braino does not update itself. There is no OTA path, no downloaded image
 * and no second app partition to put one in -- huge_app.csv has a single
 * `app0` slot, and on a 4MB board the firmware does not fit twice with room
 * for anything else. What this does is smaller and, on a device handed to
 * children, better: it reads one small file, compares a version string
 * locally, and if there is something newer it says so and points at the web
 * installer. A person decides. Nothing is downloaded and nothing is written.
 *
 * That distinction is what lets the rest of this be simple, and it is load
 * bearing rather than a limitation to be engineered away later.
 *
 * ------------------------------------------------------------------------
 * What leaves the device: nothing about the device
 * ------------------------------------------------------------------------
 * The request is a bare GET of the URL below. There is no query string, no
 * custom header, no board id and -- crucially -- no version. The console does
 * not tell the server what it is running; it downloads what exists and does
 * the comparison itself.
 *
 * The manifest lists EVERY board rather than being fetched per board, and
 * that is the reason it is shaped that way. A per-board path would have been
 * a smaller download and would have turned this into a model census in
 * somebody's access log: identical bytes are requested by every Braino in the
 * world, so the request distinguishes nobody from nobody.
 *
 * What it does reveal is an IP address and the fact that something asked --
 * the same exposure as the ip-api.com timezone lookup that already exists.
 * About says exactly that and no more. Do not let it grow a claim it cannot
 * keep; a privacy statement the firmware contradicts is worse than none.
 *
 * ------------------------------------------------------------------------
 * Why the destination is compiled in
 * ------------------------------------------------------------------------
 * BRAINO_UPDATE_PAGE_URL is what About displays. It is NEVER read out of the
 * manifest, and the manifest has no field for it.
 *
 * That single rule is what bounds the damage a hostile or spoofed response
 * can do to exactly one thing: displaying a wrong version number. It cannot
 * send a child to a URL of an attacker's choosing, because the console will
 * only ever show the address baked into its own firmware, and it cannot
 * trigger an install, because nothing here installs anything. If a future
 * change ever lets the response supply the destination, that reasoning
 * collapses entirely and this file needs a signature scheme instead.
 *
 * The transport is HTTPS because GitHub Pages redirects plain HTTP and there
 * is no way to opt out of that; certificate verification is deliberately not
 * performed (see BoardUpdate.cpp) since, given the rule above, the response
 * has no authority over anything.
 */

/* The manifest. One static file, all boards, no parameters -- see above. */
#define BRAINO_UPDATE_MANIFEST_URL \
    "https://iamankushpandit.github.io/Gume/firmware/latest.txt"

/* What About tells the owner to type. Shown, never fetched. Kept without a
 * scheme because it has to fit a 320px panel next to a label, and a person
 * typing it into a browser does not need one. */
#define BRAINO_UPDATE_PAGE_URL "iamankushpandit.github.io/Gume"
