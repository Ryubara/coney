// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pad of level99's combat tutorial (checkpoint 1) through lessons 1-6, as mission 1's play-through script plays
// them (docs/research/scripting.md#level99-lessons): the disc tests that need the tutorial under way share it.

#include <cstdint>
#include <string_view>

namespace coney::test {

// Lessons 1-6 of checkpoint 1, frame by frame from the level's start: the intro skipped, the walk to the sparring
// ground, then each lesson's moves at partial stick deflections (mission 1's play-through script).
inline constexpr std::string_view kLessonsScript =
    "0 stick left 0 0\n100 tap cross\n135 stick left 19 67\n150 stick left 18 68\n153 stick left 17 68\n"
    "159 stick left 16 68\n165 stick left 15 68\n171 stick left 14 69\n180 stick left 13 69\n"
    "186 stick left 12 69\n195 stick left 9 69\n198 stick left 4 70\n201 stick left -2 70\n"
    "204 stick left -8 70\n207 stick left -10 69\n210 stick left -12 69\n225 stick left -11 69\n"
    "240 stick left -10 69\n255 stick left -9 69\n270 stick left -5 42\n294 stick left -4 42\n"
    "297 stick left 0 0\n317 stick left 9 -69\n329 stick left 10 -69\n341 stick left 11 -69\n"
    "347 stick left 12 -69\n356 stick left 13 -69\n362 stick left 14 -69\n368 stick left 15 -68\n"
    "374 stick left 16 -68\n380 stick left 17 -68\n383 stick left 18 -68\n389 stick left 19 -67\n"
    "395 stick left 20 -67\n398 stick left 21 -67\n401 stick left 22 -67\n404 stick left 22 -66\n"
    "407 stick left 23 -66\n410 stick left 14 -39\n413 stick left 15 -39\n419 stick left 16 -39\n"
    "422 stick left 17 -39\n425 stick left 17 -38\n428 stick left 20 -37\n431 stick left 0 0\n"
    "516 tap cross\n706 stick left 10 39\n712 stick left 11 39\n724 stick left 0 0\n724 tap square\n"
    "730 tap cross\n736 tap cross\n742 tap cross\n748 tap cross\n754 tap cross\n760 tap cross\n"
    "766 tap circle\n772 tap circle\n778 tap circle\n784 tap circle\n790 tap circle\n796 tap circle\n"
    "802 tap circle\n808 tap circle\n814 tap circle\n820 tap circle\n826 tap square\n832 tap cross\n"
    "838 tap cross\n844 tap cross\n850 tap cross\n856 press circle\n870 release circle\n"
    "966 press circle\n980 release circle\n1076 tap square\n1082 tap cross\n1088 tap cross\n"
    "1094 tap cross\n1100 tap cross\n1106 tap cross\n1112 tap cross\n1118 tap cross\n1124 tap cross\n"
    "1130 tap cross\n1136 tap cross\n1142 tap cross\n1148 tap l2\n1154 tap l2\n1160 tap l2\n1166 tap l2\n"
    "1172 tap l2\n1178 tap l2\n1184 press l1\n1259 release l1\n1355 press l1\n1430 release l1\n"
    "1526 tap square\n1536 tap square\n1592 tap square\n1602 tap square\n1612 tap square\n"
    "1668 stick left 29 28\n1674 stick left 0 0\n1674 tap square\n1684 tap cross\n1740 tap cross\n"
    "1750 tap cross\n1806 tap square\n1816 tap square\n1826 tap cross\n1882 stick left 4 40\n"
    "1888 stick left 0 0\n1888 tap square cross\n1894 stick left 9 39\n1900 stick left 12 38\n"
    "1912 stick left 10 39\n1918 stick left 8 39\n1942 stick left 7 39\n1954 stick left 6 39\n"
    "1960 stick left 6 40\n1972 stick left 0 0\n1972 tap cross circle\n1978 tap cross circle\n"
    "1984 tap cross circle\n1990 tap cross circle\n1996 tap cross circle\n2002 tap cross circle\n"
    "2008 tap cross circle\n2014 tap square cross\n2020 tap square cross\n2048 tap cross\n"
    "2076 tap cross\n2150 tap circle\n2156 tap circle\n2162 tap circle\n2168 tap circle\n"
    "2174 tap circle\n2180 tap square cross\n2208 tap cross\n2236 tap cross\n2310 tap square cross\n"
    "2338 tap cross\n2366 tap cross\n";

// The frame the lesson's moves end on: lesson 7's scene starts after it.
inline constexpr std::uint64_t kLessonsEnd = 2366;

} // namespace coney::test
