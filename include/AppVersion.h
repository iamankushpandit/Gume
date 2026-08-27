#pragma once

/* Product identity, spelled once.
 *
 * Three facts live here and they are not the same fact:
 *
 *   - The console is **Braino!**.
 *   - The brand that publishes it is **GoodTime Micro Company**, which owns
 *     that name and the game names as trademarks.
 *   - The **copyright** in this source is held by an individual,
 *     iamankushpandit, and that is what the licence grant runs from.
 *
 * The copyright line used to name the company. It was changed because a brand
 * that is not an incorporated entity cannot hold a copyright, and a GPL notice
 * naming a holder that does not legally exist is a notice nobody can rely on --
 * least of all the person forking it, who is being asked to trust exactly that
 * line. Trademark and copyright are separate regimes; NOTICE.md keeps them
 * apart deliberately and is the long version of this paragraph.
 *
 * Nothing else in the firmware may spell either of them. The product name was
 * typed out in five places before this existed -- launcher, profiles, screen
 * saver, About and the mock-up generator -- which is exactly the shape of the
 * drift that left About six games out of date. Same rule as the game list:
 * derive, do not restate.
 *
 * Two copyright forms exist because two of those screens are tight on width.
 * They are currently the same string: the old pair differed only by dropping
 * "Company", and the present holder's name has no suffix to drop. Both names
 * are kept rather than collapsed into one, because the reason for the split is
 * the pixel budget on the launcher and Profiles headers, and that budget did
 * not go away -- it is waiting for the next name that does not fit. If you
 * change either, re-measure: the portrait launcher has about 38px of air
 * between the product name and this string, and that is the whole allowance. */
#define BRAINO_VERSION          "5.3.0-SNAPSHOT"
#define BRAINO_PRODUCT_NAME     "Braino!"
/** The brand that publishes the console, and the owner of the marks. */
#define BRAINO_COMPANY          "GoodTime Micro Company"
/** The copyright holder in this source. See NOTICE.md. */
#define BRAINO_COPYRIGHT        "(C) iamankushpandit"
#define BRAINO_COPYRIGHT_SHORT  "(C) iamankushpandit"
