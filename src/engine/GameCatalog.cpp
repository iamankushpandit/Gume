#include "GameCatalog.h"

/* Order defines the launcher order AND the Settings list order. The `id`
 * strings are persisted visibility keys -- changing one resets that game's
 * visibility for existing devices, so leave them alone.
 *
 * Columns: id, launcher title, launcher subtitle, Settings label, About blurb.
 * Blurbs are rendered at font 1 across ~292px, so keep them under ~46 chars. */
const GameCatalogEntry GAME_CATALOG[GAME_CATALOG_COUNT] = {
    { "tictactoe",  "Tic-Tac-Toe", "2 player",          "Tic-Tac-Toe", "Two players take turns."         },
    { "memory",     "Memory",      "match pairs",       "Memory",      "Match the hidden pairs."         },
    { "math",       "Math",        "add & subtract",    "Math",        "Add and subtract."               },
    { "multiply",   "Multiply",    "times tables",      "Multiply",    "Times tables."                   },
    { "time",       "Time",        "read clock",        "Time",        "Read an analog clock."           },
    { "whack",      "Whack",       "tap smiles",        "Whack",       "Tap the smiles quickly."         },
    { "simon",      "Simon",       "repeat colors",     "Simon",       "Repeat the colour sequence."     },
    { "sudoku",     "Sudoku",      "2x2 to 6x6",        "Sudoku",      "2x2 up to 6x6 grids."            },
    { "shapecolor", "Shapes",      "match color",       "Shapes",      "Match shape and colour."         },
    { "counting",   "Counting",    "tap number",        "Counting",    "Count objects, tap the number."  },
    { "money",      "Money",       "count coins",       "Money",       "Count coins and make change."    },
    { "fractions",  "Fractions",   "pie slices",        "Fractions",   "Match the pie chart."            },
    { "maze",       "Maze",        "drag dot",          "Maze",        "Drag the dot to the exit."       },
    { "sort",       "Sorting",     "order nums",        "Sorting",     "Order numbers up or down."       },
    { "colormix",   "Color Mix",   "mix colors",        "Color Mix",   "Mix two colours."                },
    { "slide",      "Slide",       "number puzzle",     "Slide",       "Slide tiles into order."         },
    { "oddone",     "Odd One",     "find different",    "Odd One",     "Find the one that differs."      },
    { "shapearith", "Shape Arith", "add & subtract",    "Shape Arith", "Add and take away shapes."       },
    { "fingers",    "Fingers",     "count on hands",    "Fingers",     "Count on two hands."             },
    { "calendar",   "Calendar",    "days & months",     "Calendar",    "Days and months in order."       },
    { "numberline", "Number Line", "jump to number",    "Number Line", "Hop along a number line."        },
    { "flags",      "Flags",       "guess the flag",    "Flags",       "Name the flag, then its capital."},
    { "states",     "US States",   "states & capitals", "US States",   "US states and their capitals."   },
    { "trace",      "Trace",       "A-Z & 0-9",         "Trace",       "Trace letters and numbers."      },
    { "stateflags", "State Flags", "name the flag",     "State Flags", "Name the state flag, then capital."},
    { "statemaps",  "State Maps",  "name the outline",  "State Maps",  "Name the state outline, then capital."},
};
