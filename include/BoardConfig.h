#pragma once

#include <Arduino.h>

#include "BoardProfile.h"

/* Selects the one board this build targets, and checks that its description
 * agrees with TFT_eSPI's.
 *
 * `GUME_BOARD_HEADER` comes from platformio.ini -- one flag per `[board_*]`
 * section, naming a header in `include/boards/`. Nothing else here is
 * board-specific: the constants below are *derived* from the selected profile
 * so that a new board changes one header and one ini section, never this file.
 */
#ifndef GUME_BOARD_HEADER
#error "No board selected. Define GUME_BOARD_HEADER (see platformio.ini and docs/PORTING.md)."
#endif

#include GUME_BOARD_HEADER

/* Audio backend defaults, so every translation unit can test these without
 * knowing which board it is compiling for. A board header states the backend
 * it has; a board with no speaker path states neither and gets zero for both.
 * Kept here rather than in BoardAudio.cpp because Board.cpp tests them too --
 * the DAC owns its pad and must not have it configured as a plain GPIO first. */
#ifndef GUME_HAS_AUDIO_CODEC
#define GUME_HAS_AUDIO_CODEC 0
#endif
#ifndef GUME_HAS_AUDIO_DAC
#define GUME_HAS_AUDIO_DAC 0
#endif

/* The canvas the playable games are drawn on. This is a property of the
 * *games*, not of any board -- they were authored against it and position
 * things by arithmetic on it. It is the floor a panel has to clear to be
 * supportable at all, which is why it is stated here rather than derived. */
constexpr int16_t GAME_CANVAS_WIDTH = 320;
constexpr int16_t GAME_CANVAS_HEIGHT = 240;

/* TFT_eSPI is configured through its own `-D` flags, so the panel is described
 * twice: once for the driver, once for us. These asserts are what stops the
 * two from drifting.
 *
 * The backlight one earns its keep on its own. With the wrong TFT_BL the panel
 * is completely dark while Wi-Fi, BLE, NVS, the LED and the screen saver all
 * run perfectly, so the serial log looks healthy and it reads as a dead
 * screen. Catching it at compile time costs nothing; catching it on the bench
 * costs an afternoon. */
#if defined(TFT_WIDTH) && defined(TFT_HEIGHT)
static_assert(BOARD.panel.nativeWidth == TFT_WIDTH,
              "board profile disagrees with TFT_WIDTH in platformio.ini");
static_assert(BOARD.panel.nativeHeight == TFT_HEIGHT,
              "board profile disagrees with TFT_HEIGHT in platformio.ini");
#endif
#if defined(TFT_BL)
static_assert(BOARD.panel.backlightPin == TFT_BL,
              "board profile disagrees with TFT_BL in platformio.ini");
#endif

/* ---- What a board must have to be supported at all --------------------
 *
 * Not every ESP32 display board can run this firmware, and the honest place to
 * say so is here, at the compiler, rather than in a build that flashes and
 * then cannot be read or pressed. Each assert below is a hard requirement;
 * a board failing one is out of scope, not a porting task.
 *
 * Everything NOT asserted here is optional: SD slot, RGB LED, speaker and
 * battery sense are all `PIN_NONE`-able and the firmware does without them.
 * Keep that line clear -- "degrades" and "cannot work" are different answers.
 */

/* A panel, and enough of one. The playable games are drawn on a fixed
 * landscape canvas; a smaller panel would clip controls off the edge, and a
 * player cannot press what is not there. */
static_assert(BOARD.panel.nativeWidth > 0 && BOARD.panel.nativeHeight > 0,
              "board profile has no panel size");
static_assert(BOARD.panel.landscapeRotation < 4 && BOARD.panel.portraitRotation < 4,
              "board rotations must be 0..3");
static_assert((BOARD.panel.landscapeRotation & 1) != (BOARD.panel.portraitRotation & 1),
              "landscape and portrait rotations must be a quarter-turn apart");
static_assert(BOARD.screenWidth() >= GAME_CANVAS_WIDTH
                  && BOARD.screenHeight() >= GAME_CANVAS_HEIGHT,
              "panel is smaller than the canvas the games are drawn on; this "
              "board cannot be supported");

/* Touch is the input this firmware is designed around, and the only one it
 * requires. A board may also wire the BOOT key (`ButtonProfile`), which the
 * runtime reads as a shortcut to Home -- but it is optional, guarded by
 * `BOARD.hasBootButton()`, and nothing can be reached only through it. That
 * is why there is no assert for it below and why the touch asserts stay
 * unconditional: a board with no key is supported, a board with no touch
 * controller is not.
 *
 * Which lines a board must bring out depends on its controller kind, so these
 * assert per arm rather than demanding every line of every board. Asserting
 * the union would have forced a capacitive board to invent an SPI chip select
 * -- and a made-up pin that satisfies a checker is worse than a missing one,
 * because it stops anybody noticing. */
static_assert(BOARD.touch.kind == TouchKind::ResistiveXpt2046
                  || BOARD.touch.kind == TouchKind::CapacitiveFt6336u,
              "board profile does not say which touch controller it wires");

static_assert(!BOARD.touch.isCapacitive()
                  ? (BOARD.touch.cs != PIN_NONE && BOARD.touch.mosi != PIN_NONE
                     && BOARD.touch.miso != PIN_NONE && BOARD.touch.sclk != PIN_NONE)
                  : true,
              "resistive touch board is missing an XPT2046 bus line");
static_assert(!BOARD.touch.isCapacitive() ? BOARD.touch.pressureThreshold > 0 : true,
              "resistive touch board needs a positive pressure threshold");

static_assert(BOARD.touch.isCapacitive()
                  ? (BOARD.touch.sda != PIN_NONE && BOARD.touch.scl != PIN_NONE
                     && BOARD.touch.i2cAddress != 0 && BOARD.touch.i2cHz > 0)
                  : true,
              "capacitive touch board is missing an I2C line, address or rate");

/* The one thing both kinds owe, and the thing that decides supportability: a
 * controller at all. `irq` is common to both -- pen-down on the XPT2046, INT
 * on the FT6336U. */
static_assert(BOARD.touch.irq != PIN_NONE,
              "board has no touch controller; it is the only input the "
              "firmware has, so this board cannot be supported");
static_assert(BOARD.touch.hitSlop >= 0, "touch hit slop cannot be negative");

/* A backlight the firmware can drive. Brightness is a setting with a
 * readability floor, and the idle path blanks the screen rather than only
 * dimming it; neither works on a backlight wired permanently on. */
static_assert(BOARD.hasBacklightControl(),
              "board does not bring the backlight out to a GPIO; brightness "
              "and screen blanking both need it, so this board cannot be "
              "supported");

/* Flash. The app partition is 3 MB (huge_app.csv), and the firmware is most of
 * it -- a 2 MB part cannot hold this build however it is partitioned. */
static_assert(BOARD.memory.flashBytes >= 4u * 1024u * 1024u,
              "board has less than 4 MB of flash; the 3 MB app partition does "
              "not fit, so this board cannot be supported");

/* ---- Optional hardware: stated so the guards cannot be forgotten ------- */
static_assert(!BOARD.hasBatterySense() || BOARD.battery.dividerRatio > 0.0f,
              "a board with battery sense must state its divider ratio");
static_assert(!BOARD.hasSdSlot()
                  || (BOARD.sd.mosi != PIN_NONE && BOARD.sd.miso != PIN_NONE
                      && BOARD.sd.sclk != PIN_NONE),
              "a board with an SD slot must state its whole SD bus");

/* The landscape canvas the playable games are authored against, derived from
 * the panel and its rotation rather than stated. A 320x480 board gets 480x320
 * here without touching a line of this file. */
constexpr int16_t SCREEN_WIDTH = BOARD.screenWidth();
constexpr int16_t SCREEN_HEIGHT = BOARD.screenHeight();

/* Product chrome, not hardware: the same everywhere. */
constexpr int16_t TOP_BAR_HEIGHT = 30;

/* Named here because every game hit-tests with it; it is the profile's value,
 * not a second opinion about it. */
constexpr int16_t TOUCH_HIT_SLOP = BOARD.touch.hitSlop;
