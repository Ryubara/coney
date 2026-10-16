# Statistics values

This page lists the score values that the PS2 disc's scripts give to the six statistic categories: the points each
counted event is worth, the default maximum of each category, and the per-level maxima. The values come from two
compiled Lua 4.0 scripts in the PS2 archive (`WARRIORS.DIR` and `WARRIORS.WAD`): `config_preload2.lua` and
`global.lua`.

## Source files

| File | Archive entry | Size (bytes) | Holds |
| --- | ---: | ---: | --- |
| `config_preload2.lua` | 3,771 | 182,649 | 45 event point values in six groups; the default maxima of five categories |
| `global.lua` | 4,105 | 169,601 | The `Pts` tier values (14) and the `PointsMatrix` per-level table (24 levels) |
| `level99.lua` | 9,606 | 108,385 | One further event point value, equal to the one in `config_preload2.lua` |

- Category names on this page are the field names used in `PointsMatrix`: `mission`, `bonus`, `style`, `combat`,
  `crime` and `harmony`.
- Which group number belongs to which category was confirmed against the executable, read-only.

## Categories

| Group | Category | Default maximum | Event values set |
| ---: | --- | ---: | ---: |
| 0 | `mission` | 10000 | 3 |
| 1 | `bonus` | none set | 4 |
| 2 | `style` | 2000 | 13 |
| 3 | `combat` | 5000 | 8 |
| 4 | `crime` | 3000 | 12 |
| 5 | `harmony` | 4000 | 5 |

- `config_preload2.lua` gives five default maxima, in the order combat 5000, crime 3000, harmony 4000, style 2000
  and mission 10000. It gives none for `bonus`.
- The per-level maxima in "Level maxima" carry a value for all six categories, `bonus` included.

## Event points

Points per counted event, by group and index, as set in `config_preload2.lua`.

| Group | Category | Index | Points |
| ---: | --- | ---: | ---: |
| 0 | `mission` | 0 | 1000 |
| 0 | `mission` | 1 | 1000 |
| 0 | `mission` | 2 | 500 |
| 1 | `bonus` | 0 | 100 |
| 1 | `bonus` | 1 | 250 |
| 1 | `bonus` | 2 | 500 |
| 1 | `bonus` | 3 | 1000 |
| 2 | `style` | 0 | 75 |
| 2 | `style` | 1 | 200 |
| 2 | `style` | 2 | 150 |
| 2 | `style` | 3 | 60 |
| 2 | `style` | 4 | 150 |
| 2 | `style` | 5 | 150 |
| 2 | `style` | 6 | 100 |
| 2 | `style` | 7 | 150 |
| 2 | `style` | 8 | 100 |
| 2 | `style` | 9 | 150 |
| 2 | `style` | 10 | 100 |
| 2 | `style` | 11 | 75 |
| 2 | `style` | 12 | 30 |
| 3 | `combat` | 0 | 6 |
| 3 | `combat` | 1 | 4 |
| 3 | `combat` | 2 | 1 |
| 3 | `combat` | 3 | 7 |
| 3 | `combat` | 4 | 20 |
| 3 | `combat` | 5 | 3 |
| 3 | `combat` | 6 | 5 |
| 3 | `combat` | 7 | 5 |
| 4 | `crime` | 0 | 50 |
| 4 | `crime` | 1 | 300 |
| 4 | `crime` | 2 | 5 |
| 4 | `crime` | 3 | 25 |
| 4 | `crime` | 4 | 250 |
| 4 | `crime` | 5 | 100 |
| 4 | `crime` | 6 | 300 |
| 4 | `crime` | 7 | 200 |
| 4 | `crime` | 8 | 100 |
| 4 | `crime` | 9 | 5 |
| 4 | `crime` | 10 | 50 |
| 4 | `crime` | 11 | 10 |
| 5 | `harmony` | 0 | 100 |
| 5 | `harmony` | 1 | 5 |
| 5 | `harmony` | 2 | 200 |
| 5 | `harmony` | 3 | 10 |
| 5 | `harmony` | 4 | 50 |

- `level99.lua` also sets group 0, index 0 to 1000, the same value as above.

## Maximum tiers

The `Pts` table in `global.lua` names the values that the per-level maxima of four categories are drawn from.

| Category | `LOW` | `MED` | `HI` | `ULTRA` |
| --- | ---: | ---: | ---: | ---: |
| `style` (`STYLE_`) | 2000 | 5000 | 10000 | 20000 |
| `combat` (`COMBAT_`) | 5000 | 10000 | 15000 | 20000 |
| `crime` (`CRIME_`) | 2000 | 5000 | 10000 | none |
| `harmony` (`HARM_`) | 2000 | 5000 | 10000 | none |

- `STYLE_ULTRA` is defined but no level uses it. `COMBAT_ULTRA` is used by level 51 only.

## Level maxima

The `PointsMatrix` table in `global.lua` has one row for each of 24 level numbers. The `mission` and `bonus` values
are in thousands of points (level 0's `mission` value 10 stands for 10,000 points, the same as the default maximum);
the other four are points, each written as one of the `Pts` tier names.

| Level | `mission` | `bonus` | `style` | `combat` | `crime` | `harmony` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 10 | 0 | 5000 (MED) | 10000 (MED) | 5000 (MED) | 5000 (MED) |
| 2 | 11 | 3.5 | 2000 (LOW) | 15000 (HI) | 2000 (LOW) | 10000 (HI) |
| 3 | 9 | 0 | 5000 (MED) | 15000 (HI) | 2000 (LOW) | 5000 (MED) |
| 5 | 15 | 4 | 10000 (HI) | 10000 (MED) | 2000 (LOW) | 5000 (MED) |
| 9 | 10 | 0 | 10000 (HI) | 5000 (LOW) | 5000 (MED) | 5000 (MED) |
| 11 | 13 | 7 | 10000 (HI) | 15000 (HI) | 5000 (MED) | 2000 (LOW) |
| 14 | 6 | 5.5 | 10000 (HI) | 5000 (LOW) | 10000 (HI) | 2000 (LOW) |
| 20 | 18 | 3.5 | 10000 (HI) | 10000 (MED) | 5000 (MED) | 2000 (LOW) |
| 31 | 17 | 1 | 10000 (HI) | 10000 (MED) | 2000 (LOW) | 5000 (MED) |
| 34 | 6 | 3.5 | 2000 (LOW) | 5000 (LOW) | 10000 (HI) | 5000 (MED) |
| 51 | 20 | 6 | 10000 (HI) | 20000 (ULTRA) | 2000 (LOW) | 10000 (HI) |
| 52 | 9 | 3 | 10000 (HI) | 15000 (HI) | 2000 (LOW) | 10000 (HI) |
| 54 | 10 | 0 | 2000 (LOW) | 5000 (LOW) | 2000 (LOW) | 10000 (HI) |
| 55 | 5 | 0 | 2000 (LOW) | 5000 (LOW) | 2000 (LOW) | 10000 (HI) |
| 80 | 5 | 0 | 2000 (LOW) | 5000 (LOW) | 2000 (LOW) | 2000 (LOW) |
| 81 | 5 | 0 | 5000 (MED) | 15000 (HI) | 2000 (LOW) | 10000 (HI) |
| 82 | 11 | 0 | 5000 (MED) | 10000 (MED) | 10000 (HI) | 2000 (LOW) |
| 83 | 10 | 0 | 5000 (MED) | 10000 (MED) | 5000 (MED) | 2000 (LOW) |
| 84 | 6 | 0 | 10000 (HI) | 10000 (MED) | 2000 (LOW) | 10000 (HI) |
| 86 | 10 | 0 | 5000 (MED) | 15000 (HI) | 2000 (LOW) | 5000 (MED) |
| 87 | 8 | 0 | 2000 (LOW) | 10000 (MED) | 5000 (MED) | 5000 (MED) |
| 92 | 7 | 2.5 | 5000 (MED) | 15000 (HI) | 10000 (HI) | 2000 (LOW) |
| 93 | 11 | 0 | 5000 (MED) | 15000 (HI) | 10000 (HI) | 10000 (HI) |
| 99 | 6 | 0 | 2000 (LOW) | 5000 (LOW) | 2000 (LOW) | 2000 (LOW) |

- Ten levels have a non-zero `bonus` value: 2, 5, 11, 14, 20, 31, 34, 51, 52 and 92.

## Acceptance checks

| Outcome |
| --- |
| `config_preload2.lua` on the PS2 disc sets 45 event point values |
| The six groups hold 3, 4, 13, 8, 12 and 5 event values, for groups 0 to 5 |
| The default maxima are 10000 for `mission`, 2000 for `style`, 5000 for `combat`, 3000 for `crime` and 4000 for `harmony` |
| No default maximum is given for `bonus` |
| `global.lua`'s `PointsMatrix` holds 24 levels, from level 0 to level 99 |
| Level 51 has the highest `mission` value, 20, and the only `combat` maximum of 20000 |
| Level 80 and level 99 have the lowest tier in all four tiered categories |
| Combat event index 4 is worth 20 points, the most in its group |
| Crime event indices 1 and 6 are each worth 300 points |

## Open questions

- Which in-game event each group and index stands for is not identified from the data files.
- The `bonus` maximum for a level without a `PointsMatrix` row is not known, since the defaults give none.
- The Xbox disc's copies of these scripts were not compared; differences between the versions are not known.
