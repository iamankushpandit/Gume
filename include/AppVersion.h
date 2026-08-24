#pragma once

/* Product identity, spelled once.
 *
 * The console is **Braino!**. The company that owns it is **GoodTime Micro
 * Company**, and the copyright line stays in the company's name -- the product
 * was renamed, the owner did not change. Those are two different facts and
 * conflating them is how a rename ends up half-applied.
 *
 * Nothing else in the firmware may spell either of them. The product name was
 * typed out in five places before this existed -- launcher, profiles, screen
 * saver, About and the mock-up generator -- which is exactly the shape of the
 * drift that left About six games out of date. Same rule as the game list:
 * derive, do not restate.
 *
 * Two copyright forms exist because two of those screens are tight on width.
 * The short one drops "Company" and is for headers measured to the pixel; the
 * full one is for About and the screen saver, which have the room. */
#define BRAINO_VERSION          "5.0.0"
#define BRAINO_PRODUCT_NAME     "Braino!"
#define BRAINO_COMPANY          "GoodTime Micro Company"
#define BRAINO_COPYRIGHT        "(C) GoodTime Micro Company"
#define BRAINO_COPYRIGHT_SHORT  "(C) GoodTime Micro"
