#!/usr/bin/env python3
"""
Generate src/games/ElementDataTable.cpp for Braino!.

    python tools/gen_elements.py

Unlike gen_country_facts.py there is no upstream dataset to pull: the symbols,
names and atomic numbers are common knowledge and the one-line facts are
written for a six-year-old who has never met chemistry. So the data lives here,
and what the generator buys is the checking -- grid positions unique and inside
the table, facts short enough to render, every element covered exactly once,
and the tier lists naming only real symbols. Those are precisely the mistakes a
hand-edited 118-row C++ table hides until it is on the device.

The output file is generated. Edit this script and re-run it; do not hand-edit
src/games/ElementDataTable.cpp.
"""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "games" / "ElementDataTable.cpp"

# Facts render at font 1 inside the element card, which wraps at 44 columns
# over two lines. 46 is the hard cap so a fact can never push a third line.
MAX_FACT = 46

# Category ids, matching the enum in ElementData.h. Lanthanides are shown as
# "Rare earth" and actinides as "Radioactive metal": the real names are jargon
# for the audience, and these two are what the labels actually mean to them.
CATEGORIES = [
    "Alkali metal",
    "Alkaline earth",
    "Transition metal",
    "Metal",
    "Metalloid",
    "Nonmetal",
    "Halogen",
    "Noble gas",
    "Rare earth",
    "Radioactive metal",
]
ALKALI, ALKALINE, TRANSITION, METAL, METALLOID, NONMETAL, HALOGEN, NOBLE, RARE, RADIO = range(10)

STATES = ["Solid", "Liquid", "Gas"]
SOLID, LIQUID, GAS = range(3)

# (symbol, name, fact)
ELEMENTS = [
    ("H",  "Hydrogen",      "The lightest gas. Stars burn it."),
    ("He", "Helium",        "Fills balloons and makes them float."),
    ("Li", "Lithium",       "Powers phone and laptop batteries."),
    ("Be", "Beryllium",     "Light stiff metal used in spacecraft."),
    ("B",  "Boron",         "Goes into strong glass and soap."),
    ("C",  "Carbon",        "In pencils, diamonds and every animal."),
    ("N",  "Nitrogen",      "Most of the air you breathe is this."),
    ("O",  "Oxygen",        "The part of air that keeps you alive."),
    ("F",  "Fluorine",      "Added to toothpaste to guard teeth."),
    ("Ne", "Neon",          "Glows bright orange-red in signs."),
    ("Na", "Sodium",        "Half of the salt on your chips."),
    ("Mg", "Magnesium",     "Burns with a blinding white flame."),
    ("Al", "Aluminum",      "Light metal in cans and cooking foil."),
    ("Si", "Silicon",       "Sand, glass, and computer chips."),
    ("P",  "Phosphorus",    "Strikes the flame on a matchbox."),
    ("S",  "Sulfur",        "Yellow rock that smells like eggs."),
    ("Cl", "Chlorine",      "Keeps swimming pools clean."),
    ("Ar", "Argon",         "Quiet gas that fills light bulbs."),
    ("K",  "Potassium",     "Bananas are full of it."),
    ("Ca", "Calcium",       "Builds your bones and your teeth."),
    ("Sc", "Scandium",      "Makes very light bicycle frames."),
    ("Ti", "Titanium",      "Strong light metal used in jet planes."),
    ("V",  "Vanadium",      "Added to steel to make tools tough."),
    ("Cr", "Chromium",      "Gives shiny bumpers their mirror shine."),
    ("Mn", "Manganese",     "Makes steel harder. Also in batteries."),
    ("Fe", "Iron",          "Makes steel. Your blood is red from it."),
    ("Co", "Cobalt",        "Colors glass a deep royal blue."),
    ("Ni", "Nickel",        "Coins and rechargeable batteries."),
    ("Cu", "Copper",        "Orange metal inside every wire."),
    ("Zn", "Zinc",          "Coats nails so they do not rust."),
    ("Ga", "Gallium",       "A metal that melts in your hand."),
    ("Ge", "Germanium",     "Used in old radios and fiber cables."),
    ("As", "Arsenic",       "Poisonous. Once used in green paint."),
    ("Se", "Selenium",      "Tiny amounts keep your body healthy."),
    ("Br", "Bromine",       "Red liquid with a very sharp smell."),
    ("Kr", "Krypton",       "A rare gas used in bright lamps."),
    ("Rb", "Rubidium",      "Used in very accurate atomic clocks."),
    ("Sr", "Strontium",     "Makes fireworks burn bright red."),
    ("Y",  "Yttrium",       "Helps make white LEDs glow."),
    ("Zr", "Zirconium",     "Fake diamonds are made from it."),
    ("Nb", "Niobium",       "Used in magnets for MRI scanners."),
    ("Mo", "Molybdenum",    "Keeps engine steel strong when hot."),
    ("Tc", "Technetium",    "The first element made by people."),
    ("Ru", "Ruthenium",     "A hard metal that plates jewelry."),
    ("Rh", "Rhodium",       "Cleans the exhaust from car engines."),
    ("Pd", "Palladium",     "Soaks up hydrogen gas like a sponge."),
    ("Ag", "Silver",        "Shiniest metal. Used for jewelry."),
    ("Cd", "Cadmium",       "Made yellow paint. It is poisonous."),
    ("In", "Indium",        "Makes touch screens feel your finger."),
    ("Sn", "Tin",           "Tin cans, and solder that joins wires."),
    ("Sb", "Antimony",      "Stops plastics from catching fire."),
    ("Te", "Tellurium",     "Used in solar panels on rooftops."),
    ("I",  "Iodine",        "Keeps a gland in your neck working."),
    ("Xe", "Xenon",         "Makes car headlights very bright."),
    ("Cs", "Cesium",        "The clocks of the world are set by it."),
    ("Ba", "Barium",        "Swallowed so a tummy shows on X-rays."),
    ("La", "Lanthanum",     "Helps camera lenses see clearly."),
    ("Ce", "Cerium",        "The spark in a lighter flint."),
    ("Pr", "Praseodymium",  "Colors the goggles welders wear."),
    ("Nd", "Neodymium",     "Makes the strongest fridge magnets."),
    ("Pm", "Promethium",    "Very rare. Once lit watch dials."),
    ("Sm", "Samarium",      "Magnets that work when red hot."),
    ("Eu", "Europium",      "Makes the red in old TV screens."),
    ("Gd", "Gadolinium",    "Helps doctors read MRI pictures."),
    ("Tb", "Terbium",       "Glows green in energy-saving lamps."),
    ("Dy", "Dysprosium",    "Magnets inside electric car motors."),
    ("Ho", "Holmium",       "Makes the strongest magnetic fields."),
    ("Er", "Erbium",        "Boosts light in internet cables."),
    ("Tm", "Thulium",       "The rarest metal in its whole row."),
    ("Yb", "Ytterbium",     "Used in the most exact clocks."),
    ("Lu", "Lutetium",      "Helps hospital body scanners work."),
    ("Hf", "Hafnium",       "Sits inside computer chips."),
    ("Ta", "Tantalum",      "Every phone holds a little of it."),
    ("W",  "Tungsten",      "Melts hotter than any other metal."),
    ("Re", "Rhenium",       "Lets jet engines run scorching hot."),
    ("Os", "Osmium",        "The heaviest element for its size."),
    ("Ir", "Iridium",       "Rare here. Common in asteroids."),
    ("Pt", "Platinum",      "Precious metal, rarer than gold."),
    ("Au", "Gold",          "Never rusts. That is why it lasts."),
    ("Hg", "Mercury",       "The only metal that is a liquid."),
    ("Tl", "Thallium",      "Very poisonous. Used in detectors."),
    ("Pb", "Lead",          "Heavy metal. It blocks X-rays."),
    ("Bi", "Bismuth",       "Grows rainbow staircase crystals."),
    ("Po", "Polonium",      "Radioactive. Found by Marie Curie."),
    ("At", "Astatine",      "The rarest element in the ground."),
    ("Rn", "Radon",         "A gas that seeps up out of rocks."),
    ("Fr", "Francium",      "Vanishes minutes after it is made."),
    ("Ra", "Radium",        "Glows. Marie Curie discovered it."),
    ("Ac", "Actinium",      "Glows pale blue in the dark."),
    ("Th", "Thorium",       "A fuel that future reactors may use."),
    ("Pa", "Protactinium",  "Very rare and highly radioactive."),
    ("U",  "Uranium",       "Fuel for nuclear power stations."),
    ("Np", "Neptunium",     "Named after the planet Neptune."),
    ("Pu", "Plutonium",     "Powers spacecraft far from the Sun."),
    ("Am", "Americium",     "Sits inside smoke alarms."),
    ("Cm", "Curium",        "Named after Marie and Pierre Curie."),
    ("Bk", "Berkelium",     "Named after Berkeley in California."),
    ("Cf", "Californium",   "Used to start up nuclear reactors."),
    ("Es", "Einsteinium",   "Named after Albert Einstein."),
    ("Fm", "Fermium",       "Named after Enrico Fermi."),
    ("Md", "Mendelevium",   "Named for the inventor of the table."),
    ("No", "Nobelium",      "Named after Alfred Nobel."),
    ("Lr", "Lawrencium",    "Named after Ernest Lawrence."),
    ("Rf", "Rutherfordium", "Made atom by atom inside a lab."),
    ("Db", "Dubnium",       "Named after Dubna in Russia."),
    ("Sg", "Seaborgium",    "Named after Glenn Seaborg."),
    ("Bh", "Bohrium",       "Named after Niels Bohr."),
    ("Hs", "Hassium",       "Named after Hesse in Germany."),
    ("Mt", "Meitnerium",    "Named after Lise Meitner."),
    ("Ds", "Darmstadtium",  "Named after Darmstadt, Germany."),
    ("Rg", "Roentgenium",   "Named after the X-ray discoverer."),
    ("Cn", "Copernicium",   "Named after Nicolaus Copernicus."),
    ("Nh", "Nihonium",      "Made in Japan. Nihon means Japan."),
    ("Fl", "Flerovium",     "Named after Georgy Flerov."),
    ("Mc", "Moscovium",     "Named after the Moscow region."),
    ("Lv", "Livermorium",   "Named after a lab in California."),
    ("Ts", "Tennessine",    "Named after Tennessee, USA."),
    ("Og", "Oganesson",     "The last one. Heaviest of them all."),
]

# Gases and the two liquids at room temperature; everything else is a solid.
# Oganesson is predicted to be a solid despite the -on ending, and the quiz
# never asks about tier 3, so nothing here teaches a guess as a fact.
GASES = "H He N O F Ne Cl Ar Kr Xe Rn".split()
LIQUIDS = "Br Hg".split()

# Tier 1: the ones a young player has plausibly already heard the name of.
TIER1 = "H He Li C N O Ne Na Mg Al Si P S Cl K Ca Fe Ni Cu Zn Ag Sn I Au Hg Pb".split()
# Tier 2 adds the rest of the everyday table (cumulative).
TIER2 = TIER1 + ("Be B F Ar Sc Ti V Cr Mn Co Ga Ge As Se Br Kr Sr Y Zr Mo Pd Cd "
                 "In Sb Te Xe Cs Ba W Pt Tl Bi Rn Ra Th U Pu").split()


def position(z: int) -> tuple[int, int]:
    """Column (1-18) and row (1-9) in the folded 18-wide table.

    Rows 8 and 9 are the lanthanide and actinide strips lifted out from under
    periods 6 and 7, which is the layout every wall chart uses.
    """
    if z == 1:
        return 1, 1
    if z == 2:
        return 18, 1
    if z <= 4:
        return z - 2, 2
    if z <= 10:
        return z + 8, 2
    if z <= 12:
        return z - 10, 3
    if z <= 18:
        return z, 3
    if z <= 36:
        return z - 18, 4
    if z <= 54:
        return z - 36, 5
    if z <= 56:
        return z - 54, 6
    if z <= 71:
        return z - 54, 8        # La(57) .. Lu(71) -> columns 3..17
    if z <= 86:
        return z - 68, 6        # Hf(72) .. Rn(86) -> columns 4..18
    if z <= 88:
        return z - 86, 7
    if z <= 103:
        return z - 86, 9        # Ac(89) .. Lr(103) -> columns 3..17
    return z - 100, 7           # Rf(104) .. Og(118) -> columns 4..18


def category(z: int, symbol: str) -> int:
    if symbol in ("Li", "Na", "K", "Rb", "Cs", "Fr"):
        return ALKALI
    if symbol in ("Be", "Mg", "Ca", "Sr", "Ba", "Ra"):
        return ALKALINE
    if symbol in ("F", "Cl", "Br", "I", "At", "Ts"):
        return HALOGEN
    if symbol in ("He", "Ne", "Ar", "Kr", "Xe", "Rn", "Og"):
        return NOBLE
    if symbol in ("H", "C", "N", "O", "P", "S", "Se"):
        return NONMETAL
    if symbol in ("B", "Si", "Ge", "As", "Sb", "Te"):
        return METALLOID
    if 57 <= z <= 71:
        return RARE
    if 89 <= z <= 103:
        return RADIO
    if symbol in ("Al", "Ga", "In", "Sn", "Tl", "Pb", "Bi", "Po",
                  "Nh", "Fl", "Mc", "Lv"):
        return METAL
    return TRANSITION


def main() -> None:
    symbols = [s for s, _, _ in ELEMENTS]
    assert len(ELEMENTS) == 118, "expected 118 elements, got %d" % len(ELEMENTS)
    assert len(set(symbols)) == 118, "duplicate symbol"
    for name in set(TIER1) | set(TIER2):
        assert name in symbols, "tier list names unknown symbol %r" % name

    rows = []
    seen_cells = {}
    for index, (symbol, name, fact) in enumerate(ELEMENTS):
        z = index + 1
        assert len(fact) <= MAX_FACT, \
            "%s: fact is %d chars (max %d)" % (name, len(fact), MAX_FACT)
        assert len(symbol) <= 2, "%s: symbol %r will not fit a cell" % (name, symbol)
        col, row = position(z)
        assert 1 <= col <= 18 and 1 <= row <= 9, "%s: bad cell %d,%d" % (name, col, row)
        assert (col, row) not in seen_cells, \
            "%s collides with %s at %d,%d" % (name, seen_cells[(col, row)], col, row)
        seen_cells[(col, row)] = name

        state = GAS if symbol in GASES else LIQUID if symbol in LIQUIDS else SOLID
        tier = 1 if symbol in TIER1 else 2 if symbol in TIER2 else 3
        rows.append((symbol, name, fact, z, col, row, category(z, symbol), state, tier))

    t1 = sum(1 for r in rows if r[8] == 1)
    t2 = sum(1 for r in rows if r[8] <= 2)
    print("%d elements | tier1=%d tier2(cumulative)=%d tier3=%d"
          % (len(rows), t1, t2, len(rows)))
    for i, label in enumerate(CATEGORIES):
        print("  %-18s %d" % (label, sum(1 for r in rows if r[6] == i)))
    print("  %-18s %d / %d chars" % ("longest fact", max(len(r[2]) for r in rows), MAX_FACT))

    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write('''/*
 * Generated by tools/gen_elements.py -- DO NOT EDIT BY HAND.
 *
 * All 118 elements: symbol, name, atomic number, the cell they occupy in the
 * folded 18-wide periodic table, a category, their state at room temperature,
 * a difficulty tier, and one plain-English line about where a player has
 * already met them.
 */
#include "ElementData.h"

const char* const ELEMENT_CATEGORY_NAMES[ELEMENT_CATEGORY_COUNT] = {
''')
        for label in CATEGORIES:
            f.write('    "%s",\n' % label)
        f.write("};\n\nconst char* const ELEMENT_STATE_NAMES[ELEMENT_STATE_COUNT] = {\n")
        for label in STATES:
            f.write('    "%s",\n' % label)
        f.write("};\n\n")

        f.write("/* Ordered by atomic number, so ELEMENTS[i].z == i + 1. */\n")
        f.write("const ElementFact ELEMENTS[ELEMENT_COUNT] = {\n")
        for symbol, name, fact, z, col, row, cat, state, tier in rows:
            f.write('    { "%s", "%s", "%s", %d, %d, %d, %d, %d, %d },\n'
                    % (symbol, name, fact, z, col, row, cat, state, tier))
        f.write("};\n")

    print("wrote %s" % OUT)


if __name__ == "__main__":
    main()
