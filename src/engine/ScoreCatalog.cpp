#include "ScoreCatalog.h"

/* Keys mirror the ones each game passes to saveBestScore(). Board prefixes
 * them with the active profile, so these are shared across children. */
const ScoreEntry SCORE_CATALOG[] = {
    { "tictactoe",  "Tic-Tac-Toe", "tttX",       "wins", false },
    { "memory",     "Memory",      "memBest",    "s",    true  },
    { "math",       "Math",        "mathBest",   "pts",  false },
    { "multiply",   "Multiply",    "multBest",   "pts",  false },
    { "time",       "Time",        "timeBest",   "pts",  false },
    { "whack",      "Whack",       "whackBest",  "pts",  false },
    { "simon",      "Simon",       "simonBest",  "steps",false },
    { "sudoku",     "Sudoku",      "sudokuBest", "s",    true  },
    { "shapecolor", "Shapes",      "shapeBest",  "pts",  false },
    { "counting",   "Counting",    "countBest",  "streak",false},
    { "money",      "Money",       "moneyBest",  "pts",  false },
    { "fractions",  "Fractions",   "fracBest",   "pts",  false },
    { "maze",       "Maze",        "mazeLevel",  "lvl",  false },
    { "sort",       "Sorting",     "sortBest",   "pts",  false },
    { "colormix",   "Color Mix",   "mixBest",    "pts",  false },
    { "slide",      "Slide",       "slideBest",  "moves",true  },
    { "oddone",     "Odd One",     "oddBest",    "pts",  false },
    { "shapearith", "Shape Arith", "shapeaBest", "pts",  false },
    { "fingers",    "Fingers",     "fingBest",   "pts",  false },
    { "calendar",   "Calendar",    "calBest",    "pts",  false },
    { "numberline", "Number Line", "nlineBest",  "pts",  false },
    { "flags",      "Flags",       "flagBest",   "pts",  false },
    { "states",     "US States",   "stateBest",  "pts",  false },
    { "stateflags", "State Flags", "sflagBest",  "pts",  false },
    { "statemaps",  "State Maps",  "smapBest",   "pts",  false },
};

const uint8_t SCORE_CATALOG_COUNT = sizeof(SCORE_CATALOG) / sizeof(SCORE_CATALOG[0]);
