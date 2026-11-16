// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* WHAT SPACE TEACHES, LEVEL BY LEVEL.
 *
 * Space had 41 questions in three levels, worked out from the right answers
 * of this visit -- so every visit began again with the same twelve. This
 * course has eight: the Sun, Earth and Moon; the eight planets, in order;
 * rocky worlds and giants; moons and small worlds; the air above us; days,
 * years and seasons; exploring space; stars and beyond. Four of them also
 * GENERATE questions from the planets and the layers of air in order
 * ("Which planet is fifth from the Sun?", "Which layer is just below the
 * mesosphere?", "Which of these is an ice giant?", "Which of these has the
 * longest year?"), so a player learns the order rather than a list.
 *
 * Every question is option 0 right, written that way so the table cannot
 * say a wrong thing by a transposed index; the game shuffles the buttons.
 * Every sentence has to fit the screen: a question within three lines of
 * about 36 characters, an option within 20, a fact within two lines of 48.
 * test/host/space_course_test.cpp checks all three, and that no question
 * offers the same answer twice.
 *
 * Pure C++, no Arduino. Randomness comes in as a number. */
namespace SpaceCourse {

constexpr uint8_t LEVEL_COUNT = 8;

struct Level {
    const char* name;
};

constexpr Level LEVELS[LEVEL_COUNT] = {
    {"Sun, Earth and Moon"}, {"The eight planets"}, {"Rocky worlds, giants"},
    {"Moons, small worlds"}, {"The air above us"},  {"Days, years, seasons"},
    {"Exploring space"},     {"Stars and beyond"},
};

inline uint8_t clampLevel(uint8_t level) {
    return level == 0 ? 1 : (level > LEVEL_COUNT ? LEVEL_COUNT : level);
}
inline const char* levelName(uint8_t level) { return LEVELS[clampLevel(level) - 1].name; }

constexpr uint8_t LEVEL_LINE_MAX = 30;
inline int levelLine(uint8_t level, char* out, size_t len) {
    return snprintf(out, len, "Level %u: %s", static_cast<unsigned>(clampLevel(level)),
                    levelName(level));
}

/* One written question. options[0] is the answer. */
struct Row {
    uint8_t level;
    const char* question;
    const char* options[4];
    const char* fact;
};

constexpr Row ROWS[] = {
    // ---- 1. Sun, Earth and Moon ----
    {1, "Which planet do we live on?", {"Earth", "Mars", "Venus", "Jupiter"},
     "Earth is the only world we know of with liquid water on its surface."},
    {1, "What is the Sun?", {"A star", "A planet", "A moon", "A comet"},
     "The Sun is a star, like the ones you see at night, only very much nearer."},
    {1, "What goes round the Earth?", {"The Moon", "The Sun", "Mars", "Venus"},
     "The Moon takes about 27 days to go all the way round us."},
    {1, "What makes day and night?", {"Earth spinning", "The Moon", "Clouds", "The Sun moving"},
     "Earth turns once a day, so your side faces the Sun and then faces away."},
    {1, "How long does Earth take to circle the Sun?", {"One year", "One day", "One month", "One week"},
     "That trip is what a year is: one lap of the Sun."},
    {1, "Where does the Moon's light come from?", {"The Sun", "Its own fire", "The Earth", "The stars"},
     "The Moon makes no light at all. It is a mirror for sunlight."},
    {1, "What covers most of the Moon?", {"Craters and dust", "Water", "Forest", "Ice caps"},
     "With no air to wear them away, the Moon's craters stay for billions of years."},
    {1, "Where does the Sun rise?", {"In the east", "In the west", "In the north", "In the south"},
     "Earth spins towards the east, so the Sun comes up on that side."},
    {1, "Which star is nearest to us?", {"The Sun", "The North Star", "Sirius", "Vega"},
     "The Sun is a star, and no other star comes anywhere near as close."},
    {1, "How many moons does Earth have?", {"One", "Two", "None", "Four"},
     "Earth has just one moon, which is why we simply call it the Moon."},
    {1, "Why do we always see the same side of the Moon?",
     {"It turns once a lap", "It never turns", "Clouds hide it", "Earth turns it"},
     "The Moon spins once for every trip round us, so one face always looks our way."},
    {1, "What are the Moon's changing shapes called?", {"Phases", "Seasons", "Tides", "Eclipses"},
     "From new moon to full moon and back, the phases repeat about once a month."},
    {1, "About how far away is the Moon?", {"384,000 km", "3,840 km", "38 km", "38 million km"},
     "A car driving day and night would take about five months to get there."},
    {1, "When did people first walk on the Moon?", {"1969", "1909", "1999", "2019"},
     "Neil Armstrong and Buzz Aldrin stepped onto the Moon in July 1969."},
    {1, "On the Moon, how heavy would you feel?",
     {"About a sixth", "Twice as heavy", "Just the same", "Ten times as heavy"},
     "The Moon's pull is about a sixth of Earth's, so you could jump much higher."},
    {1, "About how many Earths would fit across the Sun?",
     {"About 109", "About 10", "About 2", "About 1,000"},
     "The Sun is about 109 times as wide as the Earth."},

    // ---- 2. The eight planets ----
    {2, "Which planet is closest to the Sun?", {"Mercury", "Venus", "Earth", "Mars"},
     "Mercury is so close that it races all the way round the Sun in 88 days."},
    {2, "Which planet is called the Red Planet?", {"Mars", "Saturn", "Neptune", "Mercury"},
     "Mars looks red because its dust is full of rust, the same as an old nail."},
    {2, "How many planets go round our Sun?", {"Eight", "Nine", "Seven", "Ten"},
     "Eight since 2006, when Pluto was moved to a new group: the dwarf planets."},
    {2, "Which planet has bright rings round it?", {"Saturn", "Earth", "Mars", "Mercury"},
     "Saturn's rings are countless pieces of ice and rock, most smaller than a car."},
    {2, "Which planet is the largest?", {"Jupiter", "Saturn", "Earth", "Neptune"},
     "Jupiter is so big that every other planet would fit inside it together."},
    {2, "Which planet has a Great Red Spot?", {"Jupiter", "Mars", "Venus", "Uranus"},
     "The Great Red Spot is a storm on Jupiter, wider than the whole Earth."},
    {2, "Which planet is furthest from the Sun?", {"Neptune", "Uranus", "Saturn", "Jupiter"},
     "Neptune takes 165 Earth years to go round once."},
    {2, "Which planet is the hottest?", {"Venus", "Mercury", "Mars", "Jupiter"},
     "Venus beats Mercury: its thick clouds hold the heat in like a blanket."},
    {2, "Which planet is the smallest?", {"Mercury", "Mars", "Venus", "Earth"},
     "Mercury is only a little bigger than our Moon."},
    {2, "Which planet shines brightest in our sky?", {"Venus", "Mars", "Jupiter", "Saturn"},
     "Venus is called the morning or evening star, though it is a planet."},

    // ---- 3. Rocky worlds and giants ----
    {3, "Which planet spins on its side?", {"Uranus", "Neptune", "Saturn", "Mars"},
     "Uranus is tipped right over, so it rolls round the Sun rather than spins."},
    {3, "Which planet has the biggest volcano?", {"Mars", "Earth", "Venus", "Jupiter"},
     "Olympus Mons on Mars stands about three times as tall as Everest."},
    {3, "Which planet is nearly Earth's size?", {"Venus", "Mars", "Mercury", "Jupiter"},
     "Venus is almost our twin in size, and nothing like us in any other way."},
    {3, "On which planet is a day longer than its year?", {"Venus", "Mercury", "Mars", "Jupiter"},
     "Venus turns so slowly that it goes round the Sun before it spins once."},
    {3, "Which planet spins backwards?", {"Venus", "Earth", "Mars", "Saturn"},
     "On Venus the Sun rises in the west, because it spins the other way."},
    {3, "Which planet has the shortest day?", {"Jupiter", "Earth", "Mercury", "Venus"},
     "Jupiter spins round in about 10 hours, though it is the biggest planet."},
    {3, "Which planet is less dense than water?", {"Saturn", "Jupiter", "Earth", "Mars"},
     "Saturn is so light for its size it would float, if a bath were big enough."},
    {3, "Which planet has the fastest winds?", {"Neptune", "Earth", "Mars", "Venus"},
     "Winds on Neptune blow faster than 2,000 km an hour."},
    {3, "Why are Uranus and Neptune blue?", {"Methane gas", "Water", "Ice caps", "Blue rock"},
     "Methane in their air soaks up red light, so blue light bounces back."},
    {3, "Which giant planets have rings?",
     {"All four", "Only Saturn", "Saturn and Jupiter", "None of them"},
     "Saturn's are brightest, but Jupiter, Uranus and Neptune have faint rings."},
    {3, "What are Jupiter and Saturn mostly made of?",
     {"Hydrogen and helium", "Rock", "Iron", "Water"},
     "They are giant balls of gas with no solid ground to stand on."},
    {3, "What is the air on Venus mostly?", {"Carbon dioxide", "Oxygen", "Nitrogen", "Water vapour"},
     "Its thick carbon dioxide air traps heat, so Venus is hotter than Mercury."},
    {3, "Which planet has white ice caps at its poles?", {"Mars", "Venus", "Mercury", "Jupiter"},
     "Mars has caps of frozen water and frozen carbon dioxide."},
    {3, "Which planet has the longest canyon?", {"Mars", "Earth", "Venus", "Mercury"},
     "Valles Marineris on Mars would stretch nearly across the United States."},
    {3, "Which planet has almost no air?", {"Mercury", "Venus", "Earth", "Mars"},
     "Mercury is too small and too hot to hold on to air."},

    // ---- 4. Moons and small worlds ----
    {4, "What lies between Mars and Jupiter?", {"Asteroid belt", "A ring", "A comet", "A moon"},
     "Millions of rocks circle there, left over from when the planets formed."},
    {4, "What is Pluto called now?", {"A dwarf planet", "A planet", "A moon", "A star"},
     "Pluto is too small to have swept its orbit clear, so it got its own group."},
    {4, "What is a comet mostly made of?", {"Ice and dust", "Iron", "Gas only", "Rock only"},
     "A comet grows its tail when the Sun warms it and the ice turns to gas."},
    {4, "Which planet has no moon at all?", {"Venus", "Earth", "Mars", "Jupiter"},
     "Venus and Mercury are the only planets with no moon of their own."},
    {4, "What is Saturn's largest moon?", {"Titan", "Europa", "Io", "Callisto"},
     "Titan has a thick orange atmosphere and lakes, though of methane not water."},
    {4, "How many moons does Mars have?", {"Two", "One", "None", "Four"},
     "Mars has two small moons, shaped more like potatoes than balls."},
    {4, "What are Mars's two moons called?",
     {"Phobos and Deimos", "Io and Europa", "Titan and Rhea", "Luna and Sol"},
     "Phobos and Deimos may be asteroids that Mars caught long ago."},
    {4, "What is the largest moon in the solar system?", {"Ganymede", "Titan", "Our Moon", "Io"},
     "Jupiter's moon Ganymede is bigger than the planet Mercury."},
    {4, "Which moon has the most volcanoes?", {"Io", "Europa", "Titan", "Our Moon"},
     "Jupiter's moon Io is the most volcanic world we know of."},
    {4, "Which moon may hide an ocean under its ice?", {"Europa", "Io", "Phobos", "Our Moon"},
     "Europa is thought to have a salty ocean beneath its icy crust."},
    {4, "Who found Jupiter's four big moons?", {"Galileo", "Newton", "Copernicus", "Einstein"},
     "Galileo saw them through his telescope in 1610."},
    {4, "Which planet has the most known moons?", {"Saturn", "Earth", "Mars", "Neptune"},
     "Saturn has the most known moons, and new ones are still being found."},
    {4, "What is the largest thing in the asteroid belt?", {"Ceres", "Pluto", "Phobos", "Halley"},
     "Ceres is so big that it counts as a dwarf planet, like Pluto."},
    {4, "What is Pluto's largest moon?", {"Charon", "Titan", "Triton", "Io"},
     "Charon is half as wide as Pluto, so the two circle each other."},
    {4, "What lies beyond Neptune?", {"The Kuiper belt", "The asteroid belt", "The Sun", "Mars"},
     "The Kuiper belt is a ring of icy worlds, and Pluto is one of them."},
    {4, "Which way does a comet's tail point?",
     {"Away from the Sun", "Towards the Sun", "Towards Earth", "Straight down"},
     "Sunlight and the solar wind push the tail away from the Sun."},
    {4, "How often does Halley's Comet come back?",
     {"About every 76 years", "Every year", "Every 10 years", "Every 1,000 years"},
     "Halley's Comet was last seen in 1986, and returns in 2061."},
    {4, "What is a meteor?", {"A streak of light", "A planet", "A moon", "A star"},
     "A meteor is the glow of a grain of space dust burning up in our air."},
    {4, "What is a meteorite?", {"A rock that landed", "A shooting star", "A comet's tail", "A small moon"},
     "A meteorite is a piece of space rock that reaches the ground."},
    {4, "Which moon circles its planet backwards?", {"Triton", "Titan", "Our Moon", "Io"},
     "Neptune's moon Triton goes round the opposite way to Neptune's spin."},
    {4, "Which moon shoots jets of water into space?", {"Enceladus", "Our Moon", "Phobos", "Ganymede"},
     "Enceladus, a moon of Saturn, sprays water from cracks near its south pole."},

    // ---- 5. The air above us ----
    {5, "Which layer of air do we breathe in?",
     {"Troposphere", "Stratosphere", "Mesosphere", "Thermosphere"},
     "The troposphere is the bottom layer, and all our weather happens in it."},
    {5, "Which layer sits just above the troposphere?",
     {"Stratosphere", "Mesosphere", "Exosphere", "Thermosphere"},
     "The stratosphere is calm and dry, and it gets warmer as you go up."},
    {5, "Where is the ozone layer?", {"Stratosphere", "Troposphere", "Mesosphere", "Exosphere"},
     "Ozone up there soaks up the Sun's ultraviolet light before it reaches us."},
    {5, "Where do airliners fly?", {"Stratosphere", "Mesosphere", "Thermosphere", "Exosphere"},
     "Jets cruise at the very bottom of the stratosphere, or just below it."},
    {5, "Where do most meteors burn up?", {"Mesosphere", "Troposphere", "Stratosphere", "Exosphere"},
     "Shooting stars are grains of dust burning up about 80 km above you."},
    {5, "Which layer of air is the outermost?",
     {"Exosphere", "Thermosphere", "Mesosphere", "Stratosphere"},
     "The exosphere is so thin that its air slowly leaks away into space."},
    {5, "Which layer does the Space Station orbit in?",
     {"Thermosphere", "Stratosphere", "Mesosphere", "Troposphere"},
     "The station circles about 400 km up, inside the very thin thermosphere."},
    {5, "Which layer are the auroras seen in?",
     {"Thermosphere", "Troposphere", "Stratosphere", "Exosphere"},
     "The northern lights glow where the Sun's particles hit that thin air."},
    {5, "What is the line where space begins called?",
     {"The Karman line", "The equator", "The horizon", "The tropic"},
     "By agreement space starts 100 km up, at the Karman line."},
    {5, "Which gas is most of the air?", {"Nitrogen", "Oxygen", "Carbon dioxide", "Hydrogen"},
     "Air is about 78% nitrogen. Only a fifth of it is the oxygen you use."},
    {5, "Which layer of air is the coldest?",
     {"Mesosphere", "Troposphere", "Thermosphere", "Stratosphere"},
     "The top of the mesosphere reaches about -90C, the coldest air there is."},
    {5, "Why is the sky blue?", {"Air scatters it", "The sea", "Oxygen is blue", "Ice crystals"},
     "Air scatters blue light in every direction, so it reaches you from all over."},
    {5, "How much of the air is oxygen?", {"About a fifth", "About half", "Nearly all", "None of it"},
     "Most air is nitrogen; about one part in five is the oxygen we breathe."},
    {5, "Which layer of air is the hottest?",
     {"Thermosphere", "Troposphere", "Mesosphere", "Stratosphere"},
     "The thin thermosphere soaks up the Sun's strongest rays and gets very hot."},
    {5, "What happens to air as you go higher?",
     {"It gets thinner", "It gets thicker", "It stays the same", "It turns blue"},
     "High on a mountain each breath holds less air, so climbers carry oxygen."},
    {5, "What turns the solar wind aside from Earth?",
     {"Its magnetic field", "The Moon", "Clouds", "The ozone layer"},
     "Earth's magnetic field steers most of the Sun's particles around us."},

    // ---- 6. Days, years and seasons ----
    {6, "What holds the planets round the Sun?", {"Gravity", "Magnets", "Wind", "Air"},
     "The same pull that brings a dropped ball down holds Earth on its path."},
    {6, "How long does sunlight take to reach us?",
     {"About 8 minutes", "One second", "One hour", "One day"},
     "Sunlight you feel now left the Sun about eight minutes ago."},
    {6, "What gives us the seasons?", {"Earth's tilt", "Being nearer", "The Moon", "Clouds"},
     "Earth leans over, so each half gets more sunlight for part of the year."},
    {6, "Which planet has the shortest year?", {"Mercury", "Venus", "Earth", "Mars"},
     "Mercury goes round the Sun in just 88 days."},
    {6, "How long is a year on Mars?", {"687 Earth days", "365 days", "88 days", "12 years"},
     "Mars is further out, so its trip round the Sun takes almost two of ours."},
    {6, "About how long is a year on Jupiter?",
     {"About 12 years", "One year", "About 2 years", "About 100 years"},
     "Jupiter takes about 12 Earth years to go round the Sun once."},
    {6, "How far is Earth tilted?",
     {"About 23.5 degrees", "Not at all", "About 90 degrees", "About 45 degrees"},
     "That tilt is why summer and winter take turns in the two halves of Earth."},
    {6, "Why do we have leap years?",
     {"An extra 1/4 day", "The Moon is late", "Earth slows down", "To fit a holiday"},
     "A year is about 365 and a quarter days, so every fourth year gets a day more."},
    {6, "What causes most of the tides?", {"The Moon", "The wind", "Earthquakes", "Rivers"},
     "The Moon's pull lifts the oceans a little as Earth turns underneath it."},
    {6, "What is a solar eclipse?",
     {"Moon hides the Sun", "Earth hides the Moon", "The Sun goes out", "A cloudy day"},
     "The Moon passes between us and the Sun. Never look straight at it."},
    {6, "What darkens the Moon in a lunar eclipse?",
     {"Earth's shadow", "The Sun's shadow", "Clouds", "Night"},
     "Earth passes between the Sun and the Moon, and its shadow falls on the Moon."},
    {6, "How often do the Moon's phases repeat?",
     {"About 29.5 days", "Every day", "Every year", "Every week"},
     "From one full moon to the next is about 29 and a half days."},
    {6, "Which planets take longest to go round?",
     {"The furthest ones", "The biggest ones", "The nearest ones", "The smallest ones"},
     "The further a planet is from the Sun, the longer its path and its year."},
    {6, "What shape are the planets' orbits?",
     {"Stretched circles", "Squares", "Straight lines", "Zigzags"},
     "Orbits are ellipses: circles stretched a little out of shape."},
    {6, "Why do astronauts float in the Space Station?",
     {"They are falling", "There is no gravity", "They are light", "Magnets"},
     "They are falling round the Earth all the time, so they never hit it."},

    // ---- 7. Exploring space ----
    {7, "What was the first satellite in space?", {"Sputnik 1", "Hubble", "Apollo 11", "Voyager 1"},
     "Sputnik 1 went into orbit in 1957 and beeped as it circled Earth."},
    {7, "Who was the first person in space?",
     {"Yuri Gagarin", "Neil Armstrong", "Buzz Aldrin", "Sally Ride"},
     "Yuri Gagarin circled the Earth once in 1961."},
    {7, "Which mission first landed people on the Moon?",
     {"Apollo 11", "Apollo 1", "Sputnik 1", "Gemini 4"},
     "Apollo 11 landed on the Moon in July 1969."},
    {7, "Who first stepped onto the Moon?",
     {"Neil Armstrong", "Yuri Gagarin", "Buzz Aldrin", "John Glenn"},
     "Neil Armstrong stepped out first, and Buzz Aldrin followed him."},
    {7, "How many people have walked on the Moon?", {"Twelve", "Two", "Fifty", "None"},
     "Twelve astronauts walked on the Moon, all between 1969 and 1972."},
    {7, "How long does the Space Station take to circle Earth?",
     {"About 90 minutes", "About a day", "About a week", "About a year"},
     "The crew sees about sixteen sunrises every day."},
    {7, "What is Hubble?", {"A space telescope", "A planet", "A rocket", "A moon"},
     "Hubble has circled Earth since 1990, taking pictures above the blurring air."},
    {7, "What light does the James Webb telescope see?",
     {"Infrared", "X-rays", "Only red light", "Ultraviolet"},
     "Infrared light shines through dust that hides stars from other cameras."},
    {7, "What is the most distant thing people have made?",
     {"Voyager 1", "Hubble", "The Space Station", "Apollo 11"},
     "Voyager 1, launched in 1977, is now beyond the planets, between the stars."},
    {7, "Which spacecraft flew past Pluto in 2015?",
     {"New Horizons", "Voyager 1", "Apollo 11", "Hubble"},
     "New Horizons sent back the first close pictures of Pluto, heart and all."},
    {7, "What made the first flight on another planet?",
     {"Ingenuity", "Sputnik 1", "Voyager 1", "Apollo 11"},
     "The little helicopter Ingenuity first flew on Mars in 2021."},
    {7, "Which planet was first found with a telescope?", {"Uranus", "Mars", "Venus", "Saturn"},
     "William Herschel found Uranus in 1781."},
    {7, "Which planet was predicted before it was seen?", {"Neptune", "Uranus", "Jupiter", "Mercury"},
     "Astronomers worked out where Neptune must be, and found it there in 1846."},
    {7, "When was Pluto discovered?", {"1930", "1830", "1730", "1990"},
     "Clyde Tombaugh spotted Pluto in 1930 by comparing photos of the sky."},

    // ---- 8. Stars and beyond ----
    {8, "What is the Sun mostly made of?", {"Hydrogen", "Oxygen", "Iron", "Carbon"},
     "The Sun squeezes hydrogen into helium, and that is where its light comes from."},
    {8, "What does a light year measure?", {"Distance", "Time", "Weight", "Heat"},
     "It is how far light travels in a year: about 9.5 trillion kilometres."},
    {8, "What is our galaxy called?", {"The Milky Way", "Andromeda", "Orion", "Cassiopeia"},
     "Our galaxy holds a few hundred billion stars, and the Sun is one of them."},
    {8, "Which star is nearest beyond the Sun?",
     {"Proxima Centauri", "Sirius", "Polaris", "Betelgeuse"},
     "Proxima Centauri is about 4.2 light years away."},
    {8, "Which is the brightest star in the night sky?", {"Sirius", "Polaris", "Vega", "Rigel"},
     "Sirius, the Dog Star, outshines every other star at night."},
    {8, "Which star is the North Star?", {"Polaris", "Sirius", "The Sun", "Vega"},
     "Polaris sits almost straight above Earth's North Pole, so it barely moves."},
    {8, "What is the nearest large galaxy?",
     {"Andromeda", "The Milky Way", "Orion", "The Big Dipper"},
     "The Andromeda galaxy is about 2.5 million light years away."},
    {8, "Which stars are hottest?", {"Blue stars", "Red stars", "Yellow stars", "Orange stars"},
     "A star's colour tells its heat: blue is hottest, red is coolest."},
    {8, "About how old is the Sun?",
     {"4.6 billion years", "4,600 years", "46 years", "460 years"},
     "The Sun formed about 4.6 billion years ago, with the planets."},
    {8, "What will the Sun become in the far future?",
     {"A red giant", "A planet", "A comet", "A black hole"},
     "In about five billion years the Sun will swell up into a red giant."},
    {8, "Can light escape a black hole?", {"No", "Yes", "Only red light", "Only by day"},
     "A black hole pulls so hard that not even light gets out."},
    {8, "How many official constellations are there?", {"88", "12", "50", "200"},
     "Astronomers split the whole sky into 88 official constellations."},
    {8, "How far does light travel in a second?",
     {"About 300,000 km", "About 300 km", "About 3 km", "About 30 km"},
     "Light could go round the Earth more than seven times in one second."},
    {8, "How much of the solar system's mass is the Sun?",
     {"Over 99%", "About half", "About 10%", "About 1%"},
     "Everything else, planets included, is less than one part in a hundred."},
    {8, "About how many Earths would fit inside the Sun?",
     {"About 1.3 million", "About 100", "About 1,000", "About 10"},
     "The Sun is so big that over a million Earths would fit inside it."},
    {8, "How far away is the Sun?",
     {"150 million km", "150 km", "15,000 km", "1.5 billion km"},
     "About 150 million km: sunlight takes about eight minutes to cross it."},
};

constexpr uint16_t ROW_COUNT = sizeof(ROWS) / sizeof(ROWS[0]);

constexpr const char* PLANETS[8] = {"Mercury", "Venus", "Earth", "Mars",
                                    "Jupiter", "Saturn", "Uranus", "Neptune"};
constexpr const char* ORDINALS[8] = {"first", "second", "third", "fourth",
                                     "fifth", "sixth", "seventh", "eighth"};
constexpr const char* LAYERS[5] = {"Troposphere", "Stratosphere", "Mesosphere",
                                   "Thermosphere", "Exosphere"};
constexpr const char* LAYERS_LOWER[5] = {"troposphere", "stratosphere", "mesosphere",
                                         "thermosphere", "exosphere"};
/* 0 rocky, 1 gas giant, 2 ice giant. */
constexpr uint8_t PLANET_GROUP[8] = {0, 0, 0, 0, 1, 1, 2, 2};

/* Generated questions of a level, after its written rows:
 *  2: Nth from the Sun (8), next after X (7), just nearer the Sun than X
 *     (7), between X and Z (6) = 28
 *  3: which of these is rocky / a gas giant / an ice giant, one per planet = 8
 *  5: just above X (3: the troposphere's is written), just below X (4) = 7
 *  6: which of these four has the longest year (5) or the shortest (5) = 10 */
inline uint8_t generatedCount(uint8_t level) {
    switch (clampLevel(level)) {
        case 2: return 28;
        case 3: return 8;
        case 5: return 7;
        case 6: return 10;
        default: return 0;
    }
}

inline uint8_t rowCount(uint8_t level) {
    uint8_t n = 0;
    for (uint16_t i = 0; i < ROW_COUNT; ++i) n = static_cast<uint8_t>(n + (ROWS[i].level == level));
    return n;
}

inline uint16_t poolSize(uint8_t level) {
    level = clampLevel(level);
    return static_cast<uint16_t>(rowCount(level) + generatedCount(level));
}

constexpr uint8_t TEXT_MAX = 112;
constexpr uint8_t OPTION_MAX = 24;
constexpr uint8_t FACT_MAX = 104;

/* A question as the screen shows it. options[0] is the answer. */
struct Question {
    char text[TEXT_MAX];
    char options[4][OPTION_MAX];
    char fact[FACT_MAX];
};

namespace detail {
inline uint32_t stir(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* Three wrong options from `names` (count of them), none equal to `answer`
 * and allowed by `ok`. */
template <typename Ok>
inline void fillWrong(Question& q, const char* const* names, uint8_t count, uint8_t answer,
                      uint32_t& s, Ok ok) {
    uint8_t used[8] = {answer};
    uint8_t n = 1;
    for (uint8_t slot = 1; slot < 4; ++slot) {
        uint8_t pick = answer;
        for (uint8_t tries = 0; tries < 64; ++tries) {
            const uint8_t c = static_cast<uint8_t>(stir(s) % count);
            bool taken = false;
            for (uint8_t k = 0; k < n; ++k) taken = taken || used[k] == c;
            if (taken || !ok(c)) continue;
            pick = c;
            break;
        }
        if (pick == answer) {   // never in practice; scan in order
            for (uint8_t c = 0; c < count && pick == answer; ++c) {
                bool taken = false;
                for (uint8_t k = 0; k < n; ++k) taken = taken || used[k] == c;
                if (!taken && ok(c)) pick = c;
            }
        }
        used[n++] = pick;
        snprintf(q.options[slot], OPTION_MAX, "%s", names[pick]);
    }
}

inline int dist(uint8_t a, uint8_t b) { return a > b ? a - b : b - a; }
}  // namespace detail

/* Question `item` of `level` (item < poolSize(level)). */
inline void make(uint8_t level, uint16_t item, uint32_t seed, Question& q) {
    level = clampLevel(level);
    uint32_t s = seed == 0 ? 0x2545F491u : seed;
    const uint8_t rows = rowCount(level);
    if (item < rows) {
        uint16_t k = item;
        for (uint16_t i = 0; i < ROW_COUNT; ++i) {
            if (ROWS[i].level != level) continue;
            if (k-- == 0) {
                snprintf(q.text, TEXT_MAX, "%s", ROWS[i].question);
                for (uint8_t o = 0; o < 4; ++o) snprintf(q.options[o], OPTION_MAX, "%s", ROWS[i].options[o]);
                snprintf(q.fact, FACT_MAX, "%s", ROWS[i].fact);
                return;
            }
        }
    }
    uint8_t g = static_cast<uint8_t>((item - rows) % (generatedCount(level) ? generatedCount(level) : 1));
    if (level == 2) {
        uint8_t ans;
        if (g < 8) {
            ans = g;
            snprintf(q.text, TEXT_MAX, "Which planet is %s from the Sun?", ORDINALS[g]);
        } else if (g < 15) {
            const uint8_t x = static_cast<uint8_t>(g - 8);
            ans = static_cast<uint8_t>(x + 1);
            snprintf(q.text, TEXT_MAX, "Which planet comes next after %s?", PLANETS[x]);
        } else if (g < 22) {
            const uint8_t x = static_cast<uint8_t>(g - 15 + 1);
            ans = static_cast<uint8_t>(x - 1);
            snprintf(q.text, TEXT_MAX, "Which planet is just nearer the Sun than %s?", PLANETS[x]);
        } else {
            const uint8_t x = static_cast<uint8_t>(g - 22);
            ans = static_cast<uint8_t>(x + 1);
            snprintf(q.text, TEXT_MAX, "Which planet lies between %s and %s?", PLANETS[x],
                     PLANETS[x + 2]);
        }
        snprintf(q.options[0], OPTION_MAX, "%s", PLANETS[ans]);
        /* The neighbours are the mistakes: three planets within three places. */
        detail::fillWrong(q, PLANETS, 8, ans, s, [&](uint8_t c) { return detail::dist(c, ans) <= 3; });
        snprintf(q.fact, FACT_MAX, "%s",
                 "In order: Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune.");
    } else if (level == 3) {
        const uint8_t ans = g;
        const uint8_t group = PLANET_GROUP[ans];
        static constexpr const char* WHAT[3] = {"a rocky planet", "a gas giant", "an ice giant"};
        snprintf(q.text, TEXT_MAX, "Which of these is %s?", WHAT[group]);
        snprintf(q.options[0], OPTION_MAX, "%s", PLANETS[ans]);
        detail::fillWrong(q, PLANETS, 8, ans, s, [&](uint8_t c) { return PLANET_GROUP[c] != group; });
        snprintf(q.fact, FACT_MAX, "%s",
                 group == 0 ? "Mercury, Venus, Earth and Mars are rocky. The four beyond are giants."
                 : group == 1 ? "Jupiter and Saturn are gas giants, mostly hydrogen and helium."
                              : "Uranus and Neptune are ice giants, with icy insides under their air.");
    } else if (level == 5) {
        uint8_t ans;
        if (g < 3) {
            const uint8_t x = static_cast<uint8_t>(g + 1);   // stratosphere..thermosphere
            ans = static_cast<uint8_t>(x + 1);
            snprintf(q.text, TEXT_MAX, "Which layer of air is just above the %s?", LAYERS_LOWER[x]);
        } else {
            const uint8_t x = static_cast<uint8_t>(g - 3 + 1);   // stratosphere..exosphere
            ans = static_cast<uint8_t>(x - 1);
            snprintf(q.text, TEXT_MAX, "Which layer of air is just below the %s?", LAYERS_LOWER[x]);
        }
        snprintf(q.options[0], OPTION_MAX, "%s", LAYERS[ans]);
        detail::fillWrong(q, LAYERS, 5, ans, s, [](uint8_t) { return true; });
        snprintf(q.fact, FACT_MAX, "%s",
                 "From the ground up: troposphere, stratosphere, mesosphere, thermosphere, exosphere.");
    } else if (level == 6) {
        const bool longest = g < 5;
        const uint8_t ans = longest ? static_cast<uint8_t>(g + 3) : static_cast<uint8_t>(g - 5);
        snprintf(q.text, TEXT_MAX, "Which of these has the %s year?", longest ? "longest" : "shortest");
        snprintf(q.options[0], OPTION_MAX, "%s", PLANETS[ans]);
        detail::fillWrong(q, PLANETS, 8, ans, s,
                          [&](uint8_t c) { return longest ? c < ans : c > ans; });
        snprintf(q.fact, FACT_MAX, "%s",
                 "The further a planet is from the Sun, the longer its year: Neptune's is 165 of ours.");
    } else {
        make(level, 0, seed, q);
    }
}

}  // namespace SpaceCourse
