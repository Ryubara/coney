# Character classes

This reference page lists the three sets of numbered classes that the game's character entries point to: 5 speed
classes, the warrior classes of each difficulty and the power classes of each difficulty. Every value was read from
the PS2 disc (NTSC-U, `SLUS_212.15`), from entries of `WARRIORS.WAD` located through `WARRIORS.DIR`. All the scripts
named here are compiled Lua 4.0 chunks. The character entries themselves are on the page `ref-characters`.

## Disc sources

| Source | Directory entry (zero-based) | Size in bytes | Values taken from it |
| --- | ---: | ---: | --- |
| `config_preload2.lua` | 3,771 | 182,649 | Speed classes 0 to 4, warrior classes 0 to 9, power classes, the character entries counted below |
| `config_easy.lua` | 3,766 | 17,316 | Warrior classes 0 to 8 and power classes for difficulty 0 |
| `config_fury.lua` | 3,767 | 16,884 | The same for difficulty 3 |
| `config_hard.lua` | 3,768 | 17,253 | The same for difficulty 2 |
| `config_normal.lua` | 3,769 | 17,181 | The same for difficulty 1 |
| `config_rumble.lua` | 3,773 | 17,049 | The same for difficulty 4 |

`WARRIORS.DIR` lists 10,701 entries. Each difficulty file states its own difficulty number (easy 0, normal 1, hard 2,
fury 3, rumble 4). `config_preload2.lua` states difficulty 1, and its warrior classes 0 to 8 and all of its power
classes hold exactly the values of `config_normal.lua`.

## Class sets

| Set | Values per class | Classes | Defined in |
| --- | ---: | --- | --- |
| Speed | 6 | 0 to 4 | `config_preload2.lua` only |
| Warrior | 13 | 0 to 8 in every difficulty file; 0 to 9 in `config_preload2.lua` | All six scripts |
| Power | 30 | 51 class numbers between 0 and 66 (50 in `config_rumble.lua`, which has no class 54) | All six scripts |

The column names S1 to S6, W1 to W13 and P1 to P30 are this page's own and number the values in the order the
script gives them. What each value controls in game is not established (see Open questions).

Power class 39 is set twice in every one of the six scripts, with different values. The power tables below give the
second set of values; the first is listed under Repeated power class.

## Speed classes

Entries is the number of character entries in `config_preload2.lua` (449 in all) that give this class number in
their fourth value (A4 on `ref-characters`).

| Class | S1 | S2 | S3 | S4 | S5 | S6 | Entries |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 3 | 0.8 | 1.6 | 4.1 | 6.5 | 8 | 15 |
| 1 | 3 | 0.8 | 1.6 | 4.1 | 7.5 | 9 | 330 |
| 2 | 3 | 0.8 | 1.6 | 1.6 | 1.6 | 9.75 | 63 |
| 3 | 3 | 0.8 | 1.6 | 4.1 | 7.5 | 7.5 | 25 |
| 4 | 3 | 0.964 | 0.964 | 0.965 | 2.723 | 2.723 | 16 |

## Warrior classes

Classes 0 to 8 for each difficulty file. Class 9 appears only in `config_preload2.lua` (shown as preload2).

| Difficulty | Class | W1 | W2 | W3 | W4 | W5 | W6 | W7 | W8 | W9 | W10 | W11 | W12 | W13 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| easy | 0 | 105 | 190 | 150 | 212 | 110 | 170 | 1 | 1 | 2 | 2 | 2 | 2 | 2 |
| easy | 1 | 105 | 180 | 120 | 172 | 100 | 168 | 1 | 1 | 2 | 2 | 2 | 2 | 2 |
| easy | 2 | 75 | 170 | 120 | 172 | 100 | 168 | 2 | 1 | 2 | 2 | 2 | 2 | 2 |
| easy | 3 | 70 | 150 | 120 | 166 | 80 | 156 | 2 | 1 | 2 | 1 | 2 | 2 | 1 |
| easy | 4 | 70 | 150 | 150 | 154 | 100 | 240 | 2 | 1 | 2 | 1 | 2 | 2 | 2 |
| easy | 5 | 85 | 160 | 100 | 154 | 90 | 240 | 2 | 1 | 2 | 2 | 1 | 2 | 2 |
| easy | 6 | 70 | 150 | 78 | 154 | 200 | 240 | 2 | 1 | 1 | 1 | 2 | 2 | 2 |
| easy | 7 | 80 | 170 | 120 | 166 | 100 | 168 | 1 | 1 | 2 | 2 | 1 | 2 | 2 |
| easy | 8 | 95 | 180 | 96 | 154 | 70 | 132 | 1 | 1 | 2 | 2 | 2 | 2 | 2 |
| normal | 0 | 85 | 155 | 150 | 202 | 110 | 170 | 1 | 3 | 3 | 3 | 3 | 1 | 2 |
| normal | 1 | 85 | 145 | 120 | 162 | 110 | 168 | 1 | 3 | 2 | 2 | 3 | 2 | 2 |
| normal | 2 | 55 | 135 | 120 | 162 | 100 | 168 | 2 | 2 | 3 | 2 | 2 | 2 | 2 |
| normal | 3 | 50 | 115 | 120 | 160 | 110 | 156 | 3 | 1 | 2 | 1 | 2 | 2 | 1 |
| normal | 4 | 50 | 115 | 150 | 144 | 100 | 240 | 3 | 1 | 2 | 1 | 2 | 3 | 2 |
| normal | 5 | 65 | 125 | 100 | 144 | 100 | 240 | 2 | 3 | 3 | 3 | 1 | 2 | 2 |
| normal | 6 | 50 | 115 | 78 | 144 | 200 | 240 | 3 | 1 | 1 | 1 | 2 | 3 | 2 |
| normal | 7 | 60 | 135 | 120 | 156 | 100 | 168 | 1 | 2 | 3 | 2 | 1 | 1 | 2 |
| normal | 8 | 75 | 145 | 96 | 144 | 125 | 170 | 1 | 3 | 3 | 3 | 2 | 1 | 2 |
| hard | 0 | 45 | 119 | 150 | 202 | 140 | 195 | 1 | 3 | 3 | 3 | 3 | 1 | 2 |
| hard | 1 | 45 | 111 | 120 | 162 | 140 | 190 | 1 | 3 | 2 | 2 | 3 | 1 | 2 |
| hard | 2 | 35 | 102 | 120 | 162 | 135 | 190 | 2 | 2 | 3 | 2 | 2 | 1 | 2 |
| hard | 3 | 30 | 85 | 120 | 160 | 120 | 175 | 3 | 1 | 2 | 1 | 2 | 3 | 1 |
| hard | 4 | 30 | 85 | 150 | 144 | 130 | 240 | 3 | 1 | 2 | 1 | 2 | 3 | 2 |
| hard | 5 | 45 | 94 | 100 | 144 | 125 | 240 | 2 | 3 | 3 | 3 | 1 | 2 | 2 |
| hard | 6 | 30 | 85 | 78 | 144 | 200 | 240 | 3 | 1 | 1 | 1 | 2 | 3 | 2 |
| hard | 7 | 40 | 102 | 120 | 156 | 125 | 195 | 1 | 2 | 3 | 2 | 1 | 1 | 2 |
| hard | 8 | 45 | 111 | 96 | 144 | 145 | 200 | 1 | 3 | 3 | 3 | 2 | 1 | 2 |
| fury | 0 | 45 | 119 | 150 | 202 | 110 | 170 | 1 | 3 | 3 | 3 | 3 | 1 | 2 |
| fury | 1 | 45 | 111 | 120 | 162 | 100 | 168 | 1 | 3 | 2 | 2 | 3 | 1 | 2 |
| fury | 2 | 35 | 102 | 120 | 162 | 100 | 168 | 2 | 2 | 3 | 2 | 2 | 1 | 2 |
| fury | 3 | 30 | 85 | 120 | 160 | 80 | 156 | 3 | 1 | 2 | 1 | 2 | 3 | 1 |
| fury | 4 | 30 | 85 | 150 | 144 | 100 | 240 | 3 | 1 | 2 | 1 | 2 | 3 | 2 |
| fury | 5 | 45 | 94 | 100 | 144 | 90 | 240 | 2 | 3 | 3 | 3 | 1 | 2 | 2 |
| fury | 6 | 30 | 85 | 78 | 144 | 200 | 240 | 3 | 1 | 1 | 1 | 2 | 3 | 2 |
| fury | 7 | 40 | 102 | 120 | 156 | 100 | 168 | 1 | 2 | 3 | 2 | 1 | 1 | 2 |
| fury | 8 | 45 | 111 | 96 | 144 | 70 | 132 | 1 | 3 | 3 | 3 | 2 | 1 | 2 |
| rumble | 0 | 100 | 115 | 150 | 202 | 110 | 170 | 1 | 3 | 3 | 3 | 3 | 1 | 2 |
| rumble | 1 | 100 | 115 | 120 | 162 | 100 | 168 | 1 | 3 | 2 | 2 | 3 | 1 | 2 |
| rumble | 2 | 100 | 115 | 120 | 162 | 100 | 168 | 2 | 2 | 3 | 2 | 2 | 1 | 2 |
| rumble | 3 | 100 | 115 | 120 | 156 | 80 | 156 | 3 | 1 | 2 | 1 | 2 | 3 | 1 |
| rumble | 4 | 100 | 115 | 150 | 144 | 100 | 240 | 3 | 1 | 2 | 1 | 2 | 3 | 2 |
| rumble | 5 | 100 | 115 | 100 | 144 | 90 | 240 | 2 | 3 | 3 | 3 | 1 | 2 | 2 |
| rumble | 6 | 100 | 115 | 78 | 144 | 200 | 240 | 3 | 1 | 1 | 1 | 2 | 3 | 2 |
| rumble | 7 | 100 | 115 | 120 | 156 | 100 | 168 | 1 | 2 | 3 | 2 | 1 | 1 | 2 |
| rumble | 8 | 100 | 115 | 96 | 144 | 70 | 132 | 1 | 3 | 3 | 3 | 2 | 1 | 2 |
| preload2 | 9 | 85 | 135 | 120 | 162 | 100 | 168 | 2 | 2 | 2 | 2 | 2 | 2 | 2 |

Every one of the 9 classes differs from normal in each other difficulty. Cells that differ from normal: easy 56,
hard 35, fury 26, rumble 24. In rumble, W1 is 100 and W2 is 115 for every class.

## Power classes

The values of `config_normal.lua` (and of `config_preload2.lua`, which are the same). Entries is the number of
character entries in `config_preload2.lua` that give this class number in their fifth value (A5 on
`ref-characters`). 41 classes are used by at least one entry; classes 10 and 58 to 66 by none.

| Class | P1 | P2 | P3 | P4 | P5 | P6 | P7 | P8 | P9 | P10 | P11 | P12 | P13 | P14 | P15 | P16 | P17 | P18 | P19 | P20 | P21 | P22 | P23 | P24 | P25 | P26 | P27 | P28 | P29 | P30 | Entries |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 180 | 30 | 80 | 10 | 2500 | 750 | 750 | 0.45 | 0.55 | 0.025 | 0 | 3.5 | 1.5 | 0.5 | 10 | 10 | 6 | 0 | 0 | 4 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 30 | 0 | 0 | 153 |
| 1 | 200 | 30 | 95 | 20 | 2250 | 750 | 750 | 0.6 | 0.2 | 0.1 | 0.05 | 3 | 1.25 | 0.5 | 8 | 8 | 5 | 0 | 0.04 | 4 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 45 | 0 | 0 | 8 |
| 2 | 200 | 32 | 95 | 20 | 2000 | 750 | 750 | 0.65 | 0.35 | 0.35 | 0.15 | 3 | 1.25 | 0.55 | 7 | 7 | 4 | 0 | 0.25 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 50 | 0 | 0 | 45 |
| 3 | 240 | 42 | 95 | 25 | 2000 | 750 | 750 | 0.7 | 0.25 | 0.6 | 0.3 | 3 | 1.5 | 0.6 | 6 | 6 | 3 | 0 | 0.32 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 75 | 0 | 0 | 6 |
| 4 | 240 | 50 | 95 | 25 | 2000 | 500 | 750 | 0.7 | 0.3 | 0.75 | 0.4 | 4.5 | 1.5 | 0.6 | 5.5 | 5.5 | 2 | 2 | 0.48 | 4 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 75 | 0 | 0 | 40 |
| 5 | 300 | 60 | 95 | 25 | 1750 | 500 | 750 | 0.8 | 0.35 | 0.9 | 0.5 | 4 | 1.5 | 0.65 | 5 | 5 | 2 | 3 | 0.48 | 4 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 90 | 0 | 0 | 3 |
| 6 | 300 | 60 | 95 | 25 | 1500 | 200 | 0 | 1.25 | 0.1 | 0.01 | 0.01 | 1 | 1.5 | 1 | 1 | 1 | 2 | 0 | 0.72 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 100 | 0 | 0 | 3 |
| 7 | 400 | 100 | 120 | 30 | 100 | 200 | 300 | 1.4 | 0.35 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 3 | 2 | 0.4 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 50 | 0 | 0 | 38 |
| 8 | 400 | 100 | 120 | 30 | 100 | 200 | 300 | 1.3 | 0.35 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 3 | 1.5 | 0.4 | 4 | 1 | 0 | 0 | 1 | 0 | 0 | 0 | 50 | 0 | 0 | 8 |
| 9 | 600 | 90 | 45 | 5 | 1200 | 100 | 50 | 1 | 0.2 | 0.25 | 0.01 | 5 | 1.5 | 0.65 | 3 | 4 | 1.5 | 8 | 0.9 | 2 | 1 | 0 | 1 | 0 | 1 | 0 | 0 | 75 | 0 | 0 | 22 |
| 10 | 80 | 10 | 80 | 10 | 3000 | 2000 | 0 | 0.75 | 0.95 | 1 | 1 | 2 | 1.5 | 0.5 | 3 | 3 | 5 | 0 | 0.16 | 4 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 25 | 0 | 0 | 0 |
| 13 | 600 | 90 | 15 | 1 | 3750 | 100 | 100 | 1 | 0.4 | 0.3 | 0.01 | 5 | 1.25 | 0.75 | 4 | 4 | 2 | 5 | 0.6 | 2 | 1 | 0 | 1 | 0 | 1 | 0 | 0 | 75 | 0 | 0 | 1 |
| 15 | 600 | 100 | 95 | 25 | 1000 | 10 | 10 | 0.7 | 0.05 | 1.67 | 0.4 | 4.5 | 1.5 | 0.6 | 0.5 | 6.5 | 2 | 15 | 1 | 2 | 1 | 1 | 1 | 0 | 1 | -1 | 1 | 75 | 0 | 0 | 1 |
| 16 | 180 | 30 | 80 | 10 | 2500 | 750 | 750 | 0.45 | 0.2 | 0.025 | 0 | 3.5 | 1.5 | 0.5 | 8 | 0.01 | 6 | 0 | 0 | 4 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 35 | 0 | 0 | 4 |
| 18 | 200 | 32 | 95 | 20 | 2000 | 750 | 750 | 0.65 | 0.25 | 0.4 | 0.3 | 3 | 1.25 | 0.55 | 7 | 7 | 4 | 0 | 0.32 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 60 | 0 | 0 | 8 |
| 19 | 240 | 42 | 95 | 25 | 2000 | 750 | 750 | 0.7 | 0.25 | 0.6 | 0.3 | 3 | 1.5 | 0.6 | 6 | 6 | 3 | 0 | 0.32 | 4 | 1 | 0 | 1 | 0 | 1 | 0 | 0 | 75 | 0 | 0 | 1 |
| 20 | 60 | 30 | 80 | 10 | 2500 | 750 | 750 | 0.45 | 1 | 0.1 | 0.1 | 5.5 | 1.5 | 0.5 | 25 | 25 | 6 | 0 | 0.16 | 4 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 40 | 0 | 0 | 6 |
| 22 | 240 | 50 | 95 | 25 | 2000 | 500 | 750 | 0.9 | 0.3 | 0.85 | 0.4 | 3.5 | 1.25 | 0.6 | 3.5 | 1.5 | 2 | 2 | 0.58 | 3 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 75 | 0 | 0 | 6 |
| 23 | 300 | 60 | 95 | 25 | 1750 | 500 | 750 | 1 | 0.35 | 0.95 | 0.5 | 3 | 1.25 | 0.65 | 2 | 1 | 2 | 3 | 0.58 | 2 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 90 | 0 | 0 | 1 |
| 24 | 240 | 50 | 55 | 15 | 2000 | 500 | 750 | 1 | 0.35 | 0.65 | 0.35 | 4.5 | 1.5 | 0.6 | 5.5 | 5.5 | 3 | 2 | 0.16 | 4 | 1 | 1 | 1 | 0 | 0 | -1 | 0 | 75 | 0 | 0 | 11 |
| 25 | 300 | 60 | 55 | 15 | 1750 | 500 | 750 | 1.2 | 0.38 | 0.8 | 0.4 | 4 | 1.5 | 0.65 | 5 | 5 | 3 | 3 | 0.16 | 4 | 1 | 1 | 1 | 0 | 0 | -1 | 1 | 90 | 0 | 0 | 1 |
| 26 | 240 | 50 | 95 | 25 | 2000 | 500 | 750 | 1 | 0.3 | 0.75 | 0.4 | 4.5 | 1.5 | 0.6 | 2.5 | 2.5 | 2 | 2 | 0.48 | 4 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 95 | 0 | 0 | 4 |
| 27 | 500 | 100 | 95 | 25 | 2000 | 10 | 10 | 1 | 0.1 | 1.27 | 1 | 1 | 1.15 | 0.8 | 0.5 | 0.5 | 2 | 15 | 1 | 2 | 0 | 1 | 1 | 0 | 1 | -1 | 1 | 100 | 0 | 0 | 1 |
| 28 | 240 | 50 | 95 | 25 | 2000 | 500 | 750 | 5 | 0.3 | 0.9 | 0.5 | 4.5 | 1.5 | 0.6 | 5.5 | 5.5 | 2 | 2 | 0.48 | 4 | 1 | 0 | 1 | 0 | 0 | 0 | 0 | 95 | 0 | 0 | 1 |
| 31 | 240 | 32 | 55 | 15 | 2000 | 500 | 750 | 0.7 | 0.3 | 0.9 | 0.5 | 4.5 | 1.5 | 0.6 | 5.5 | 5.5 | 3 | 2 | 0.16 | 4 | 1 | 1 | 1 | 0 | 0 | 0 | 1 | 70 | 0 | 0 | 4 |
| 32 | 400 | 42 | 65 | 15 | 1750 | 500 | 100 | 0.8 | 0.25 | 1.2 | 0.5 | 3 | 1.5 | 0.65 | 1 | 1 | 2 | 4 | 0.48 | 4 | 1 | 1 | 1 | 0 | 1 | 0 | 0 | 85 | 0 | 0 | 2 |
| 39 | 100 | 10 | 35 | 5 | 2250 | 750 | 750 | 0.6 | 0.2 | 0.1 | 0.05 | 3 | 1.25 | 0.5 | 8 | 8 | 5 | 0 | 0.04 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 45 | 0 | 0 | 17 |
| 40 | 180 | 30 | 80 | 10 | 5000 | 750 | 750 | 0.45 | 0.2 | 0.2 | 0.1 | 3.5 | 1.5 | 0.5 | 20 | 20 | 5 | 0 | 0.08 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 50 | 0 | 0 | 6 |
| 43 | 200 | 30 | 95 | 20 | 2000 | 750 | 750 | 0.5 | 0.35 | 0.3 | 0.1 | 3.5 | 1.25 | 0.5 | 8.5 | 8.5 | 3 | 0 | 0.16 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 60 | 0 | 0 | 5 |
| 44 | 200 | 30 | 95 | 20 | 2000 | 750 | 750 | 0.5 | 0.2 | 0.3 | 0.1 | 3.5 | 1.25 | 0.5 | 8.5 | 8.5 | 3 | 0 | 0.16 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 75 | 0 | 1 | 1 |
| 45 | 500 | 100 | 95 | 25 | 500 | 10 | 10 | 1 | 0.1 | 1.27 | 1 | 1 | 1.15 | 0.8 | 1.5 | 1.5 | 2 | 25 | 1 | 2 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 100 | 0 | 0 | 2 |
| 47 | 200 | 30 | 95 | 20 | 2250 | 300 | 300 | 0.6 | 0.2 | 0.45 | 0.35 | 1 | 1.25 | 0.5 | 7 | 7 | 5 | 5 | 0.04 | 4 | 1 | 0 | 1 | 0 | 1 | 0 | 0 | 60 | 0 | 0 | 5 |
| 48 | 500 | 100 | 95 | 25 | 1500 | 50 | 50 | 1 | 0.1 | 1.58 | 1 | 1 | 1.15 | 0.8 | 2 | 2 | 2 | 25 | 1 | 2 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 100 | 0 | 0 | 2 |
| 49 | 800 | 500 | 95 | 20 | 2000 | 750 | 750 | 0.65 | 0.45 | 0.1 | 0.05 | 3 | 1.25 | 0.15 | 9 | 9 | 3 | 0 | 0.2 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 60 | 0 | 0 | 4 |
| 50 | 240 | 50 | 95 | 25 | 2000 | 500 | 750 | 1 | 0.3 | 0.75 | 0.4 | 4.5 | 1.5 | 0.6 | 5.5 | 5.5 | 2 | 2 | 0.48 | 4 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 75 | 1 | 0 | 1 |
| 51 | 200 | 32 | 60 | 15 | 2000 | 750 | 750 | 0.5 | 0.5 | 0.3 | 0.1 | 4 | 1.25 | 0.55 | 25 | 25 | 4 | 0 | 0.16 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 35 | 0 | 0 | 4 |
| 52 | 240 | 42 | 60 | 15 | 2000 | 0 | 750 | 0.5 | 0.5 | 0.6 | 0.2 | 4 | 1.25 | 0.6 | 8 | 8 | 3 | 0 | 0.16 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 70 | 1 | 0 | 3 |
| 53 | 500 | 100 | 95 | 25 | 1500 | 50 | 50 | 1 | 0.1 | 1.58 | 1 | 1 | 1.15 | 0.8 | 0.5 | 0.5 | 2 | 25 | 1 | 2 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 100 | 0 | 0 | 1 |
| 54 | 300 | 60 | 55 | 15 | 1750 | 500 | 750 | 3.5 | 0.38 | 0.8 | 0.4 | 4 | 1.5 | 0.65 | 5 | 5 | 3 | 3 | 0.16 | 4 | 1 | 1 | 1 | 0 | 0 | -1 | 1 | 90 | 0 | 0 | 1 |
| 55 | 240 | 50 | 95 | 25 | 2000 | 500 | 750 | 0.7 | 0.3 | 0.75 | 0.4 | 4.5 | 1.5 | 0.6 | 5.5 | 5.5 | 2 | 2 | 0.48 | 4 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 75 | 0 | 1 | 1 |
| 56 | 400 | 60 | 100 | 30 | 50 | 0 | 0 | 1 | 0.05 | 0.3 | 0.5 | 4 | 1.5 | 0.85 | 0.5 | 0.5 | 2 | 5 | 1 | 2 | 1 | 1 | 1 | 0 | 1 | -1 | 1 | 75 | 0 | 0 | 16 |
| 57 | 200 | 32 | 95 | 20 | 2000 | 750 | 750 | 0.65 | 0.01 | 0.4 | 0.3 | 3 | 1.25 | 0.55 | 7 | 7 | 5 | 0 | 0.16 | 4 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 30 | 0 | 0 | 2 |
| 58 | 550 | 110 | 80 | 20 | 2000 | 200 | 300 | 1.2 | 0.25 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 3.5 | 2 | 0.45 | 4 | 1 | 0 | 0 | 0 | 0 | -1 | 1 | 85 | 0 | 0 | 0 |
| 59 | 500 | 100 | 100 | 30 | 2000 | 200 | 300 | 1 | 0.25 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 2.75 | 2 | 0.5 | 4 | 1 | 0 | 0 | 0 | 0 | -1 | 1 | 85 | 0 | 0 | 0 |
| 60 | 400 | 100 | 120 | 30 | 2500 | 200 | 300 | 1.1 | 0.3 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 2 | 2 | 0.6 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 50 | 0 | 0 | 0 |
| 61 | 400 | 60 | 135 | 40 | 2750 | 200 | 300 | 1.5 | 0.3 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 3 | 2 | 0.35 | 4 | 1 | 0 | 0 | 0 | 0 | 1 | -1 | 40 | 0 | 0 | 0 |
| 62 | 400 | 75 | 135 | 40 | 2750 | 200 | 300 | 1.3 | 0.3 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 2.7 | 2 | 0.35 | 2 | 1 | 0 | 0 | 0 | 0 | 1 | -1 | 40 | 0 | 0 | 0 |
| 63 | 400 | 100 | 120 | 30 | 2750 | 200 | 300 | 1.2 | 0.3 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 2 | 2 | 0.6 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 65 | 0 | 0 | 0 |
| 64 | 400 | 60 | 135 | 40 | 2750 | 200 | 300 | 1.3 | 0.3 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 3 | 2 | 0.35 | 3 | 1 | 0 | 0 | 0 | 0 | 1 | -1 | 35 | 0 | 0 | 0 |
| 65 | 400 | 100 | 120 | 30 | 2500 | 200 | 300 | 1.2 | 0.3 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 2 | 2 | 0.5 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 65 | 0 | 0 | 0 |
| 66 | 500 | 100 | 100 | 30 | 2000 | 200 | 300 | 1 | 0.25 | 1 | 1 | 1 | 1 | 0.75 | 3 | 3 | 2.75 | 2 | 0.5 | 4 | 1 | 0 | 0 | 0 | 0 | -1 | 1 | 75 | 0 | 0 | 0 |

## Power class changes

Every class of the normal table above whose values differ in another difficulty file, with only the values that
differ. A class not listed for a difficulty holds the normal values there, except class 54, which
`config_rumble.lua` does not set at all. Classes listed: easy 14 (25 values), hard 49 (106 values), fury 50
(122 values), rumble 4 (14 values).

| Difficulty | Class | Values that differ from normal |
| --- | ---: | --- |
| easy | 8 | P18 2 |
| easy | 9 | P3 40, P4 4, P5 1600, P7 100, P8 0.75, P9 0.4, P13 2, P17 2, P18 6 |
| easy | 26 | P9 0.5 |
| easy | 28 | P9 0.5 |
| easy | 54 | P8 2.5 |
| easy | 58 | P1 650 |
| easy | 59 | P1 600 |
| easy | 60 | P1 500 |
| easy | 61 | P1 500, P2 100 |
| easy | 62 | P1 500, P2 100 |
| easy | 63 | P1 500 |
| easy | 64 | P1 500, P2 100 |
| easy | 65 | P1 500 |
| easy | 66 | P1 600 |
| hard | 0 | P8 0.54, P9 0.45, P15 7, P16 7, P17 5 |
| hard | 1 | P8 0.72, P9 0.1, P15 6, P16 6, P17 4 |
| hard | 2 | P8 0.78, P9 0.25, P15 6, P16 6 |
| hard | 3 | P8 0.84, P9 0.15, P15 5, P16 5 |
| hard | 4 | P8 0.84, P9 0.2 |
| hard | 5 | P8 0.96, P9 0.25 |
| hard | 6 | P8 1.5 |
| hard | 8 | P18 2 |
| hard | 9 | P3 50, P8 1.3 |
| hard | 10 | P8 0.9, P9 0.85 |
| hard | 13 | P8 1.2, P9 0.3 |
| hard | 15 | P8 0.84 |
| hard | 16 | P8 0.54, P9 0.1, P15 6, P17 4 |
| hard | 18 | P8 0.78, P9 0.15, P15 6, P16 6 |
| hard | 19 | P8 0.84, P9 0.15, P15 5, P16 5 |
| hard | 20 | P8 0.54, P17 5 |
| hard | 22 | P8 1.08, P9 0.2 |
| hard | 23 | P8 1.2, P9 0.25 |
| hard | 24 | P8 1.2, P9 0.15 |
| hard | 25 | P8 1.44, P9 0.18 |
| hard | 26 | P8 1.2 |
| hard | 27 | P8 1.2 |
| hard | 28 | P8 3.6 |
| hard | 31 | P8 0.84, P9 0.2 |
| hard | 32 | P8 0.96, P9 0.15 |
| hard | 39 | P8 0.72, P9 0.1 |
| hard | 40 | P8 0.54, P9 0.1 |
| hard | 43 | P8 0.6, P9 0.25, P15 7.5, P16 7.5 |
| hard | 44 | P8 0.6, P9 0.1, P15 7.5, P16 7.5 |
| hard | 45 | P8 1.2, P9 0.05 |
| hard | 47 | P8 0.72, P9 0.1 |
| hard | 48 | P8 1.2, P9 0.05 |
| hard | 49 | P8 0.78, P9 0.35, P15 8, P16 6 |
| hard | 50 | P8 1.2, P9 0.2 |
| hard | 51 | P8 0.6, P9 0.4 |
| hard | 52 | P8 0.6, P9 0.4, P15 7, P16 7 |
| hard | 53 | P8 1.2 |
| hard | 54 | P8 2, P15 3, P16 3 |
| hard | 55 | P8 0.84, P9 0.2 |
| hard | 56 | P8 1.2 |
| hard | 58 | P8 1 |
| hard | 59 | P8 0.85 |
| hard | 60 | P8 0.9 |
| hard | 61 | P8 1.1 |
| hard | 62 | P8 1.05 |
| hard | 63 | P8 1 |
| hard | 64 | P8 1.1 |
| hard | 65 | P8 1 |
| hard | 66 | P8 0.8 |
| fury | 0 | P8 0.54, P9 0.45, P15 6, P16 6, P17 5 |
| fury | 1 | P8 0.72, P9 0.1, P15 5, P16 5, P17 4 |
| fury | 2 | P8 0.78, P9 0.25, P15 5, P16 5 |
| fury | 3 | P8 0.84, P9 0.15, P15 4, P16 4 |
| fury | 4 | P8 0.84, P9 0.2, P15 4.5, P16 4.5 |
| fury | 5 | P8 0.96, P9 0.25, P15 4, P16 4 |
| fury | 6 | P8 1.5 |
| fury | 7 | P8 2 |
| fury | 8 | P18 2 |
| fury | 9 | P3 50, P8 1.5 |
| fury | 10 | P8 0.9, P9 0.85 |
| fury | 13 | P8 1.2, P9 0.3 |
| fury | 15 | P8 0.84 |
| fury | 16 | P8 0.54, P9 0.1, P15 6 |
| fury | 18 | P8 0.78, P9 0.15, P15 6, P16 6, P17 3 |
| fury | 19 | P8 0.84, P9 0.15, P15 5, P16 5 |
| fury | 20 | P8 0.54 |
| fury | 22 | P8 1.08, P9 0.2 |
| fury | 23 | P8 1.2, P9 0.25 |
| fury | 24 | P8 1.2, P9 0.15, P15 5 |
| fury | 25 | P8 1.44, P9 0.18, P15 4.5 |
| fury | 26 | P8 1.2 |
| fury | 27 | P8 1.2 |
| fury | 28 | P8 3.6 |
| fury | 31 | P8 0.84, P9 0.2, P15 4.5, P16 4.5 |
| fury | 32 | P8 0.96, P9 0.15 |
| fury | 39 | P8 0.72, P9 0.1, P15 7, P16 7 |
| fury | 40 | P8 0.54, P9 0.1 |
| fury | 43 | P8 0.6, P9 0.25, P15 7.5, P16 7.5 |
| fury | 44 | P8 0.6, P9 0.1, P15 7.5, P16 7.5 |
| fury | 45 | P8 1.2, P9 0.05 |
| fury | 47 | P8 0.72, P9 0.1, P15 6, P16 6 |
| fury | 48 | P8 1.2, P9 0.05 |
| fury | 49 | P8 0.78, P9 0.35, P15 8, P16 6 |
| fury | 50 | P8 1.2, P9 0.2, P15 4.5, P16 3.5 |
| fury | 51 | P8 0.6, P9 0.4 |
| fury | 52 | P8 0.6, P9 0.4, P15 7, P16 7 |
| fury | 53 | P8 1.2 |
| fury | 54 | P8 2, P15 3, P16 3 |
| fury | 55 | P8 0.84, P9 0.2, P15 3.5, P16 3.5 |
| fury | 56 | P8 1.2 |
| fury | 58 | P8 2 |
| fury | 59 | P8 2 |
| fury | 60 | P8 2 |
| fury | 61 | P8 2 |
| fury | 62 | P8 2 |
| fury | 63 | P8 2 |
| fury | 64 | P8 2 |
| fury | 65 | P8 2 |
| fury | 66 | P8 2 |
| rumble | 8 | P18 2 |
| rumble | 9 | P5 2500, P7 100, P8 1.5, P9 0.35, P15 4, P17 2, P18 1, P19 0.4, P20 3, P25 0 |
| rumble | 26 | P9 0.5 |
| rumble | 28 | P8 3, P9 0.5 |

## Repeated power class

The first of the two sets of values for class 39 in each script. `config_preload2.lua` holds the same first set as
`config_normal.lua`. Easy and rumble hold the normal values; hard and fury differ in P8 and P9 only.

| Script | P1 | P2 | P3 | P4 | P5 | P6 | P7 | P8 | P9 | P10 | P11 | P12 | P13 | P14 | P15 | P16 | P17 | P18 | P19 | P20 | P21 | P22 | P23 | P24 | P25 | P26 | P27 | P28 | P29 | P30 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| normal, easy, rumble, preload2 | 100 | 10 | 80 | 10 | 2500 | 100 | 500 | 0.35 | 0.85 | 0.2 | 0.1 | 1 | 1.5 | 0.5 | 4 | 4 | 3 | 0 | 0.16 | 4 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 30 | 0 | 0 |
| hard, fury | 100 | 10 | 80 | 10 | 2500 | 100 | 500 | 0.42 | 0.75 | 0.2 | 0.1 | 1 | 1.5 | 0.5 | 4 | 4 | 3 | 0 | 0.16 | 4 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 30 | 0 | 0 |

## Acceptance checks

- Entry 3,771 (zero-based) of the PS2 disc's `WARRIORS.DIR` is `config_preload2.lua`, 182,649 bytes, beginning with
  the Lua 4.0 signature.
- `config_preload2.lua` sets 5 speed classes (0 to 4), 10 warrior classes (0 to 9) and 52 power class entries for
  51 class numbers.
- `config_easy.lua`, `config_normal.lua`, `config_hard.lua` and `config_fury.lua` each set warrior classes 0 to 8 and
  52 power class entries; `config_rumble.lua` sets warrior classes 0 to 8 and 51 power class entries, with no class
  54.
- The difficulty files state the numbers easy 0, normal 1, hard 2, fury 3 and rumble 4.
- Power class 39 is set twice in each of the six scripts.
- Speed class 1 is given by 330 of the 449 character entries, speed class 4 by 16.
- Warrior class 0 has W1 105 in easy, 85 in normal and 45 in hard and fury; every rumble warrior class has W1 100.
- Power classes 10 and 58 to 66 are given by no character entry; class 0 is given by 153.
- Power class 9 has P1 600 and P5 1200 in normal; rumble changes P5 to 2500.

## Open questions

- What each speed value (S1 to S6), warrior value (W1 to W13) and power value (P1 to P30) controls in game, and their
  units.
- Which of the two class 39 entries is in force in game.
- Which warrior, and which character entries, each warrior class belongs to: no character entry holds a warrior
  class number.
- Whether warrior class N goes with power class 58 + N (the nine power classes 58 to 66 that no character entry
  gives) is unconfirmed.
- Whether the difficulty file in force replaces the values of `config_preload2.lua` in game, and which file each game
  mode loads.
- What warrior class 9, set only in `config_preload2.lua`, is used for.
- The Xbox disc's copies of these scripts were not compared.
