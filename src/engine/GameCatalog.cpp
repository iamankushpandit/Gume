#include "GameCatalog.h"

/* Order defines the launcher order AND the Settings list order. The `id`
 * strings are persisted visibility keys -- changing one resets that game's
 * visibility for existing devices, so leave them alone. */
const GameCatalogEntry GAME_CATALOG[GAME_CATALOG_COUNT] = {
    { "tictactoe",  "Tic-Tac-Toe", "2 player",          "Tic-Tac-Toe" },
    { "memory",     "Memory",      "match pairs",       "Memory"      },
    { "math",       "Math",        "add & subtract",    "Math"        },
    { "multiply",   "Multiply",    "times tables",      "Multiply"    },
    { "time",       "Time",        "read clock",        "Time"        },
    { "whack",      "Whack",       "tap smiles",        "Whack"       },
    { "simon",      "Simon",       "repeat colors",     "Simon"       },
    { "sudoku",     "Sudoku",      "2x2 to 6x6",        "Sudoku"      },
    { "shapecolor", "Shapes",      "match color",       "Shapes"      },
    { "counting",   "Counting",    "tap number",        "Counting"    },
    { "money",      "Money",       "count coins",       "Money"       },
    { "fractions",  "Fractions",   "pie slices",        "Fractions"   },
    { "maze",       "Maze",        "drag dot",          "Maze"        },
    { "sort",       "Sorting",     "order nums",        "Sorting"     },
    { "colormix",   "Color Mix",   "mix colors",        "Color Mix"   },
    { "slide",      "Slide",       "number puzzle",     "Slide"       },
    { "oddone",     "Odd One",     "find different",    "Odd One"     },
    { "shapearith", "Shape Arith", "add & subtract",    "Shape Arith" },
    { "fingers",    "Fingers",     "count on hands",    "Fingers"     },
    { "calendar",   "Calendar",    "days & months",     "Calendar"    },
    { "numberline", "Number Line", "jump to number",    "Number Line" },
    { "geo",        "Countries",   "maps & continents", "Countries"   },
    { "flags",      "Flags",       "guess the flag",    "Flags"       },
};
