# Level list

This reference page lists the game's 111 level records and the places where player 1 is put when a level starts.
Every value was read from the PS2 disc (NTSC-U, `SLUS_212.15`), from entries of `WARRIORS.WAD` located through
`WARRIORS.DIR`. All the scripts named here are compiled Lua 4.0 chunks.

## Disc sources

| Source | Directory entry (zero-based) | Size in bytes | Values taken from it |
| --- | ---: | ---: | --- |
| `config_preload3.lua` | 3,772 | 308,938 | The level list: 111 records, and the constants they use |
| `config_strings_en.lua` | 3,775 | 38,257 | English titles and location labels, keyed by level name |
| `level<N>.lua` | one per level | various | Story-level placements (mission tables and their characters) |
| `level<N>_<mode>_init.lua` | 56 scripts | various | Rumble arena flags, including the player-1 flag list |
| `WARRIORS.DIR` | disc file | 128,428 | Which `.lev`, `.pak` and script entries exist for each record |

The strings files for French, German, Spanish and Italian (`config_strings_fr.lua`, `_de`, `_es`, `_it`) are also on
the disc; this page gives the English strings only.

## Record layout

Each record in the level list is a list of 15 values, always in this order. The column names are this page's own,
except where a value is one of the script's named constants.

| Position | Column | Type | Values on the disc |
| ---: | --- | --- | --- |
| 1 | Name | string | 111 distinct names; 79 have the form `level<N>`, 32 do not |
| 2 | World | string | The same as the name in 89 records; the other 22 (all with names not of the form `level<N>`) name a different world |
| 3 | Number | integer | N for every `level<N>` record; 403 to 428 for the others, with 419, 421 and 425 each shared by several records |
| 4 | Sections | integer | 0 to 12 |
| 5 | Order | integer | 0 to 26 (see Groups by order) |
| 6 | LT 1 | constant | `LT_NONE` in all 111 records |
| 7 | LT 2 | constant | `LT_PLAY` in 7 records, `LT_NONE` in the rest |
| 8 | LT 3 | constant | `LT_PLAY` in 6 records, `LT_NONE` in the rest |
| 9 | Lock | constant | `UNLOCKED` in 70 records, `LOCKED` in 41 |
| 10 to 12 | Point | three numbers | x, y, z; 560.46 is the most common z |
| 13 | Value A | integer | Shown below as its upper and lower 16-bit halves: 25 and 0 to 11 for orders 1 to 23, 0 elsewhere |
| 14 | Subway | constant or 0 | `SUBWAY_ORANGE` 41, `SUBWAY_BLUE` 7, `SUBWAY_BROWN` 6, `SUBWAY_GREEN` 6, plain 0 in 51 |
| 15 | Value B | integer | Shown as halves: 22 or 23 and 0 to 11 for orders 1 to 25; 539 to 575 and 0 for the 37 records of order 26, one more per record; 0 elsewhere |

The title and the location label are not part of the record. They are the English strings stored under the
record's name in the strings file's mission-name and level-name tables. 65 records have a title.

## Script constants

| Name | Value |
| --- | ---: |
| `LT_NONE` | 0 |
| `LT_COMPLETED` | 1 |
| `LT_PLAY` | 1 |
| `LOCKED` | 0 |
| `UNLOCKED` | 1 |
| `SUBWAY_ORANGE` | 1 |
| `SUBWAY_GREEN` | 2 |
| `SUBWAY_BLUE` | 3 |
| `SUBWAY_BROWN` | 4 |

`LT_COMPLETED` and `LT_PLAY` share the value 1; no record uses `LT_COMPLETED` by name.

## Groups by order

| Order | Records | What the data shows |
| --- | ---: | --- |
| 1 to 18 | 18 | One record per order value, each titled; the location label starts with the order number (`1:` to `18:`) |
| 19 to 23 | 5 | Titled; the location label starts with a letter, A to E |
| 24 | 1 | `level95`: no title, label `:Coney`, 12 sections and packs 1 to 12 |
| 25 | 5 | `level60` to `level64`, titled Stage 1 to Stage 5 |
| 26 | 37 | `level101` to `level137`, all titled and `UNLOCKED`; 33 of them have Rumble flag scripts |
| 0 | 45 | `level100`, `level1`, 11 records with 1 section and no files of their own, and the 32 records not named `level<N>` |

Only order 1 (New Blood) is `UNLOCKED` among orders 1 to 25.

## Level table

`.lev` says whether an entry named `<name>.lev` exists in `WARRIORS.DIR`. Packs lists the numbers k for which an
entry named `<name>_<k>.pak` exists. Both are looked up by the record's name, not its world. 64 records have a
`.lev` entry.

| Index | Name | World | Number | Order | Title | Location label | Sections | LT 2 | LT 3 | Lock | Point (x, y, z) | Value A | Subway | Value B | `.lev` | Packs |
| ---: | --- | --- | ---: | ---: | --- | --- | ---: | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `level100` | `level100` | 100 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `LOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | yes | 1, 2 |
| 1 | `level99` | `level99` | 99 | 1 | New Blood | 1:Coney | 3 | `LT_PLAY` | `LT_NONE` | `UNLOCKED` | -1.33, 61.55, 95 | 25, 11 | `SUBWAY_ORANGE` | 22, 11 | yes | 1-3 |
| 2 | `level80` | `level80` | 80 | 2 | Real Live Bunch | 2:Coney | 4 | `LT_NONE` | `LT_NONE` | `LOCKED` | 320.88, 291.59, 215 | 25, 11 | `SUBWAY_ORANGE` | 22, 2 | yes | 1-4 |
| 3 | `level87` | `level87` | 87 | 3 | Payback | 3:East Coney | 5 | `LT_NONE` | `LT_PLAY` | `LOCKED` | -202.28, 212.19, 370 | 25, 1 | `SUBWAY_BROWN` | 22, 8 | yes | 1-5 |
| 4 | `level34` | `level34` | 34 | 4 | Blackout | 4:Riverside | 5 | `LT_NONE` | `LT_PLAY` | `LOCKED` | 272.04, -0.4, 280 | 25, 2 | `SUBWAY_BLUE` | 23, 4 | yes | 1-5 |
| 5 | `level2` | `level2` | 2 | 5 | Real Heavy Rep | 5:Tremont | 4 | `LT_NONE` | `LT_NONE` | `LOCKED` | -310.2, 209, 297.77 | 25, 8 | `SUBWAY_ORANGE` | 23, 2 | yes | 1-4 |
| 6 | `level3` | `level3` | 3 | 6 | Writer's Block | 6:Soho | 5 | `LT_NONE` | `LT_NONE` | `LOCKED` | 351.48, 439.9, 682.89 | 25, 3 | `SUBWAY_BLUE` | 23, 5 | yes | 1-5 |
| 7 | `level5` | `level5` | 5 | 7 | Adios Amigo | 7:East Harlem | 4 | `LT_NONE` | `LT_NONE` | `LOCKED` | 213.25, 241.22, 470 | 25, 5 | `SUBWAY_ORANGE` | 23, 10 | yes | 1-4 |
| 8 | `level81` | `level81` | 81 | 8 | Encore | 8:Coney | 5 | `LT_NONE` | `LT_PLAY` | `LOCKED` | -1.86, 48.42, 225 | 25, 11 | `SUBWAY_ORANGE` | 22, 3 | yes | 1-5 |
| 9 | `level86` | `level86` | 86 | 9 | Payin' The Cost | 9:Coney | 4 | `LT_NONE` | `LT_NONE` | `LOCKED` | 209.09, 196.58, 280 | 25, 11 | `SUBWAY_ORANGE` | 22, 7 | yes | 1-4 |
| 10 | `level93` | `level93` | 93 | 10 | Destroyed | 10:East Coney | 6 | `LT_NONE` | `LT_NONE` | `LOCKED` | -202.28, 212.19, 370 | 25, 1 | `SUBWAY_BROWN` | 22, 10 | yes | 1-6 |
| 11 | `level31` | `level31` | 31 | 11 | Boys In Blue | 11:Pelham | 6 | `LT_PLAY` | `LT_PLAY` | `LOCKED` | 106.72, 234.19, 279 | 25, 10 | `SUBWAY_GREEN` | 23, 3 | yes | 1-6 |
| 12 | `level14` | `level14` | 14 | 12 | Set Up | 12:Bensonhurst | 3 | `LT_NONE` | `LT_NONE` | `LOCKED` | 168.86, 120.5, 208 | 25, 6 | `SUBWAY_ORANGE` | 22, 0 | yes | 1-3 |
| 13 | `level9` | `level9` | 9 | 13 | All-City | 13:Trainyard | 3 | `LT_PLAY` | `LT_PLAY` | `LOCKED` | 171.17, 441.25, 300 | 25, 7 | `SUBWAY_GREEN` | 23, 11 | yes | 1-3 |
| 14 | `level51` | `level51` | 51 | 14 | Desperate Dudes | 14:Gunhill | 7 | `LT_PLAY` | `LT_NONE` | `LOCKED` | 237.58, 624.3, 503.22 | 25, 10 | `SUBWAY_GREEN` | 23, 6 | yes | 1-7 |
| 15 | `level52` | `level52` | 52 | 15 | No Permits, No Parley | 15:Tremont | 4 | `LT_PLAY` | `LT_NONE` | `LOCKED` | -400.28, -47.76, 230 | 25, 8 | `SUBWAY_ORANGE` | 23, 7 | yes | 1-4 |
| 16 | `level54` | `level54` | 54 | 16 | Home Run | 16:Riverside | 5 | `LT_PLAY` | `LT_NONE` | `LOCKED` | 1081.84, 499.41, 1130 | 25, 2 | `SUBWAY_BLUE` | 23, 8 | yes | 1-5 |
| 17 | `level55` | `level55` | 55 | 17 | Friendly Faces | 17:Bowery | 3 | `LT_NONE` | `LT_NONE` | `LOCKED` | 3.93, 289.53, 560.46 | 25, 9 | `SUBWAY_GREEN` | 23, 9 | yes | 1-3 |
| 18 | `level84` | `level84` | 84 | 18 | Come Out To Play | 18:Coney | 3 | `LT_NONE` | `LT_PLAY` | `LOCKED` | 332.45, 200.88, 560.46 | 25, 11 | `SUBWAY_ORANGE` | 22, 6 | yes | 1-4 |
| 19 | `level82` | `level82` | 82 | 19 | Roots | A:East Coney | 4 | `LT_NONE` | `LT_NONE` | `LOCKED` | -157.24, 83.44, 279.09 | 25, 1 | `SUBWAY_BROWN` | 22, 4 | yes | 1-4 |
| 20 | `level92` | `level92` | 92 | 20 | The Best | B:Coney | 3 | `LT_NONE` | `LT_NONE` | `LOCKED` | 266.15, 185.57, 230 | 25, 11 | `SUBWAY_ORANGE` | 22, 9 | yes | 1-3 |
| 21 | `level83` | `level83` | 83 | 21 | Heavy Muscle | C:East Coney | 6 | `LT_NONE` | `LT_NONE` | `LOCKED` | 128.24, 213.95, 468 | 25, 1 | `SUBWAY_BROWN` | 22, 5 | yes | 1-6 |
| 22 | `level20` | `level20` | 20 | 22 | Scout's Honor | D:Chinatown | 6 | `LT_NONE` | `LT_NONE` | `LOCKED` | -244.59, 145.07, 364 | 25, 4 | `SUBWAY_BLUE` | 23, 1 | yes | 1-6 |
| 23 | `level11` | `level11` | 11 | 23 | Sharp Dressed Man | E:Harlem | 6 | `LT_NONE` | `LT_NONE` | `LOCKED` | 137.7, 124.23, 340 | 25, 0 | `SUBWAY_BLUE` | 23, 0 | yes | 1-6 |
| 24 | `level60` | `level60` | 60 | 25 | Stage 1 | :Brooklyn | 3 | `LT_NONE` | `LT_NONE` | `LOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 22, 11 | yes | 1, 3 |
| 25 | `level61` | `level61` | 61 | 25 | Stage 2 | :Lower Manhattan | 3 | `LT_NONE` | `LT_NONE` | `LOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 22, 11 | yes | 1 |
| 26 | `level62` | `level62` | 62 | 25 | Stage 3 | :Harlem | 3 | `LT_NONE` | `LT_NONE` | `LOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 22, 11 | yes | 1 |
| 27 | `level63` | `level63` | 63 | 25 | Stage 4 | :The Bronx | 3 | `LT_NONE` | `LT_NONE` | `LOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 22, 11 | yes | 1 |
| 28 | `level64` | `level64` | 64 | 25 | Stage 5 | :Upper West Side | 4 | `LT_NONE` | `LT_NONE` | `LOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 22, 11 | yes | 1, 3, 4 |
| 29 | `level95` | `level95` | 95 | 24 | none | :Coney | 12 | `LT_NONE` | `LT_NONE` | `LOCKED` | 266.15, 185.57, 230 | 0, 0 | 0 | 22, 11 | yes | 1-12 |
| 30 | `level90` | `level90` | 90 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 31 | `level91` | `level91` | 91 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 32 | `level94` | `level94` | 94 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 33 | `level96` | `level96` | 96 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 34 | `level97` | `level97` | 97 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 35 | `level98` | `level98` | 98 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 36 | `level70` | `level70` | 70 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 37 | `level71` | `level71` | 71 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 38 | `level72` | `level72` | 72 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 39 | `level73` | `level73` | 73 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 40 | `level74` | `level74` | 74 | 0 | none | none | 1 | `LT_NONE` | `LT_NONE` | `LOCKED` | 362.25, 339.48, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 41 | `level1` | `level1` | 1 | 0 | none | none | 0 | `LT_PLAY` | `LT_NONE` | `LOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | yes | 1 |
| 42 | `level101` | `level101` | 101 | 26 | Junkyard | 30:Gunhill | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_GREEN` | 539, 0 | yes | 1 |
| 43 | `level102` | `level102` | 102 | 26 | Fight Pen | 1:Coney Island | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 540, 0 | yes | 1 |
| 44 | `level103` | `level103` | 103 | 26 | Warrior Turf | 7:Coney Island | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 80.93, 51.98, 54.6 | 0, 0 | `SUBWAY_BROWN` | 541, 0 | yes | 1 |
| 45 | `level104` | `level104` | 104 | 26 | The Rotunda | 17:Riverside | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 18.25, 18.46, 37 | 0, 0 | `SUBWAY_BLUE` | 542, 0 | yes | 1 |
| 46 | `level105` | `level105` | 105 | 26 | Red Devil | 28:Pelham | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_GREEN` | 543, 0 | yes | 1 |
| 47 | `level106` | `level106` | 106 | 26 | The Shanties | 8:Coney Island | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 11.68, 1.55, 50 | 0, 0 | `SUBWAY_BROWN` | 544, 0 | no | none |
| 48 | `level107` | `level107` | 107 | 26 | Courtyard | 13:Soho | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0.53, -5.14, 45 | 0, 0 | `SUBWAY_BLUE` | 545, 0 | yes | 1 |
| 49 | `level108` | `level108` | 108 | 26 | Gunhill Road | 21:Gunhill Road | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 103.3, 551.6, 125 | 0, 0 | `SUBWAY_ORANGE` | 546, 0 | yes | 1 |
| 50 | `level109` | `level109` | 109 | 26 | Shaolin Temple | 11:Chinatown | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 547, 0 | yes | 1 |
| 51 | `level110` | `level110` | 110 | 26 | Club 45 | 19:Harlem | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 548, 0 | yes | 1 |
| 52 | `level111` | `level111` | 111 | 26 | Bensonhurst | 10:Bensonhurst | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 76.75, 60.38, 132.06 | 0, 0 | `SUBWAY_ORANGE` | 549, 0 | yes | 1 |
| 53 | `level112` | `level112` | 112 | 26 | Coney Drag | 9:Coney Island | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 209.09, 196.58, 280 | 0, 0 | `SUBWAY_ORANGE` | 550, 0 | yes | none |
| 54 | `level113` | `level113` | 113 | 26 | Swimming Pool | 22:East Harlem | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 551, 0 | yes | 1 |
| 55 | `level114` | `level114` | 114 | 26 | Chinatown | 12:Chinatown | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 60.83, 54.34, 120 | 0, 0 | `SUBWAY_ORANGE` | 552, 0 | yes | 1 |
| 56 | `level115` | `level115` | 115 | 26 | Subway Platform | 29:Pelham | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 553, 0 | yes | 1 |
| 57 | `level116` | `level116` | 116 | 26 | The Shack | 2:Coney Island | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 554, 0 | yes | 1 |
| 58 | `level117` | `level117` | 117 | 26 | Tenement Lobby | 23:East Harlem | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 555, 0 | no | none |
| 59 | `level118` | `level118` | 118 | 26 | Tack's Warehouse | 3:Coney Island | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 556, 0 | yes | 1 |
| 60 | `level119` | `level119` | 119 | 26 | The Graveyard | 33:Prospect Park | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 557, 0 | yes | 1 |
| 61 | `level120` | `level120` | 120 | 26 | The Bridge | 34:Prospect Park | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 204.74, 209.08, 250 | 0, 0 | `SUBWAY_ORANGE` | 558, 0 | yes | 1 |
| 62 | `level121` | `level121` | 121 | 26 | The Orphanage | 26:Tremont | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 12.67, 5.21, 40 | 0, 0 | `SUBWAY_ORANGE` | 559, 0 | yes | 1 |
| 63 | `level122` | `level122` | 122 | 26 | Wrecked Apts | 20:Harlem | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 17.68, 18.9, 36.04 | 0, 0 | `SUBWAY_ORANGE` | 560, 0 | yes | 1 |
| 64 | `level123` | `level123` | 123 | 26 | Subway Bathroom | 16:Union Square | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 561, 0 | yes | 1 |
| 65 | `level124` | `level124` | 124 | 26 | Stripes and Solids | 24:East Harlem | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 562, 0 | yes | 1 |
| 66 | `level125` | `level125` | 125 | 26 | Spookarama | 4:Coney Island | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 563, 0 | no | none |
| 67 | `level126` | `level126` | 126 | 26 | Coney Amusement | 5:Coney Island | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 564, 0 | yes | 1 |
| 68 | `level127` | `level127` | 127 | 26 | The Park | 18:Riverside Park | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 565, 0 | yes | 1 |
| 69 | `level128` | `level128` | 128 | 26 | The Old Junkyard | 31:Gunhill Road | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 566, 0 | yes | 1 |
| 70 | `level129` | `level129` | 129 | 26 | King o'Shanties | 6:Coney Island | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 567, 0 | yes | 1 |
| 71 | `level130` | `level130` | 130 | 26 | Mausoleum Hill | 35:Prospect Park | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 568, 0 | yes | 1 |
| 72 | `level131` | `level131` | 131 | 26 | Soho Rooftops | 14:Soho | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 569, 0 | yes | 1 |
| 73 | `level132` | `level132` | 132 | 26 | Tenement Rooftop | 25:East Harlem | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 570, 0 | yes | 1 |
| 74 | `level133` | `level133` | 133 | 26 | Machine Shop | 15:Trainyard | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 571, 0 | yes | 1 |
| 75 | `level134` | `level134` | 134 | 26 | Tremont | 27:Tremont | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 572, 0 | yes | 1 |
| 76 | `level135` | `level135` | 135 | 26 | Harlem | 21:Harlem | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 573, 0 | no | none |
| 77 | `level136` | `level136` | 136 | 26 | Gunhill Chase | 36:Gunhill Road | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 574, 0 | yes | 1 |
| 78 | `level137` | `level137` | 137 | 26 | Furies' Chase | 37:Riverside Park | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | `SUBWAY_ORANGE` | 575, 0 | yes | 1 |
| 79 | `combatselect` | `level5` | 422 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 191.78, 326.61, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 80 | `test_car` | `test_combat2` | 420 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 81 | `test_combat3` | `test_combat3` | 403 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 82 | `test_combat4` | `test_combat3` | 404 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 83 | `test_combat5` | `test_combat5` | 405 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 84 | `test_combat_ben` | `test_combat_ben` | 406 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 192.58, 359.23, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 85 | `test_lipsynch` | `test_lipsync` | 407 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 86 | `test_combat_cop` | `test_combat_ben` | 408 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 87 | `test_combat_pit` | `test_combat2` | 409 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 88 | `test_avoidance` | `test_combat_ben` | 410 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 89 | `combatslow` | `level4` | 411 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 345.87, 303.46, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 90 | `test_rumble` | `level13` | 412 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 263.09, 305.36, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 91 | `test_variable` | `test_variable` | 413 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 197.29, 331.98, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 92 | `test_combat2` | `test_combat2` | 421 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 93 | `test_jump` | `level23` | 414 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | -58.95, 265.25, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 94 | `dynarena` | `test_combat3` | 415 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 95 | `test_train` | `level31` | 416 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 245.52, 365.86, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 96 | `test_door` | `test_door` | 417 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 97 | `test_sergei` | `test_sergei` | 418 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 98 | `buschase` | `test_buschase` | 419 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 99 | `buschase2` | `test_buschase` | 419 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 100 | `carchase` | `level84` | 419 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 101 | `armies` | `level60` | 419 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 102 | `cichase` | `level84` | 419 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 103 | `testflags` | `test_flags` | 421 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 104 | `test_sound` | `test_combat2` | 424 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 105 | `objarena` | `objarena` | 423 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 106 | `test4special` | `test_combat2` | 425 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 107 | `test` | `level3` | 425 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 108 | `combatselect2` | `level2` | 426 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 109 | `testclimb` | `testclimb` | 427 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |
| 110 | `testcamera` | `testcamera` | 428 | 0 | none | none | 0 | `LT_NONE` | `LT_NONE` | `UNLOCKED` | 0, 0, 560.46 | 0, 0 | 0 | 0, 0 | no | none |

## Story level starts

Ten of the level scripts hold a mission table with one entry per section of the level. Each entry names a
chapter script (which exists on the disc as `<chapter>.lua`) and a set of characters, each with a name, type
number, position, heading and slot number. In all ten scripts the slot 1 character is the one the script also
assigns as the player; in the five scripts of orders 1 to 23 the slot 2 character is the one assigned as the
second player. The table gives the slot 1 character of every entry. Entry is the position in the mission table,
counting from 1. Positions are the stored x, y and z; headings are the stored values, all between 0 and 359.

| Level | Title | Entry | Chapter script | Slot 1 character | Type | Position (x, y, z) | Heading | Slot 2 character |
| --- | --- | ---: | --- | --- | ---: | --- | ---: | --- |
| `level99` | New Blood | 1 | `level99_combat` | Rembrandt | 32 | -284.4, 120.4, 0.3 | 0 | Ash (type 40) |
| `level99` | New Blood | 2 | `level99_lesson1` | Rembrandt | 30 | 46, 34.2, 0.3 | 154 | Ash (type 38) |
| `level99` | New Blood | 3 | `level99_lesson2` | Rembrandt | 30 | 46.2, 34.6, 0.3 | 154 | Ash (type 38) |
| `level34` | Blackout | 1 | `level34_riot` | Vermin | 26 | -4.3, -25.1, -6.2 | 199 | Snow (type 33) |
| `level34` | Blackout | 2 | `level34_riot` | Vermin | 26 | -60.6, -143.7, 0 | 84 | Snow (type 33) |
| `level34` | Blackout | 3 | `level34_riot` | Vermin | 26 | -107.4, -227.1, 0 | 177 | Snow (type 33) |
| `level34` | Blackout | 4 | `level34_gate` | Vermin | 26 | -51.3, -251.3, 0 | 86 | Snow (type 33) |
| `level34` | Blackout | 5 | `level34_park` | Vermin | 26 | -116, -254.7, 0 | 86 | Snow (type 33) |
| `level2` | Real Heavy Rep | 1 | `level2_clinic` | Cleon | 1 | 454.7, 182.5, 5 | 90 | Fox (type 21) |
| `level2` | Real Heavy Rep | 2 | `level2_tenement` | Cleon | 1 | 471.5, 104.7, 0 | 177 | Fox (type 21) |
| `level2` | Real Heavy Rep | 3 | `level2_clubhouse` | Cleon | 1 | 445.3, -45.1, 8 | 297 | Fox (type 21) |
| `level2` | Real Heavy Rep | 4 | `level2_junkyard` | Cleon | 1 | 468, -66.7, -0.2 | 68 | Fox (type 21) |
| `level31` | Boys In Blue | 1 | `level31_Platform` | Cochise | 15 | 76.5, 134.4, 0 | 174 | Vermin (type 26) |
| `level31` | Boys In Blue | 2 | `level31_Streets` | Cochise | 15 | 78.3, 64.2, 0 | 358 | Vermin (type 26) |
| `level31` | Boys In Blue | 3 | `level31_gclub` | Cochise | 15 | 151.5, 90.5, 6 | 115 | Vermin (type 26) |
| `level31` | Boys In Blue | 4 | `level31_Sewers` | Cochise | 15 | 69.9, 156.5, -0.5 | 89 | Vermin (type 26) |
| `level31` | Boys In Blue | 5 | `level31_RedDevil` | Cochise | 15 | -36.7, 99.1, 5.3 | 0 | Vermin (type 26) |
| `level31` | Boys In Blue | 6 | `level31_Birdie` | Cochise | 15 | -51, 6.8, 10.3 | 268 | Vermin (type 26) |
| `level20` | Scout's Honor | 1 | `level20_mainstreet` | Fox | 24 | 333.4, -183.7, -213.4 | 180 | Vermin (type 29) |
| `level20` | Scout's Honor | 2 | `level20_temple` | Fox | 24 | 282.8, -150, 0.3 | 58 | Vermin (type 29) |
| `level20` | Scout's Honor | 3 | `level20_marketplace` | Fox | 24 | 327, -17.3, -199.7 | 326 | Vermin (type 29) |
| `level20` | Scout's Honor | 4 | `level20_backalley` | Fox | 24 | 414.9, 86.5, 0.3 | 311 | Vermin (type 29) |
| `level20` | Scout's Honor | 5 | `level20_bossfight` | Fox | 24 | 495.8, 59, 4.3 | 356 | Vermin (type 29) |
| `level20` | Scout's Honor | 6 | `level20_endtrain` | Fox | 24 | 495.8, 59, 4.3 | 356 | Vermin (type 29) |
| `level60` | Stage 1 | 1 | `level60_destroyers` | Swan | 5 | -93.5, 18.8, 0 | 359 | Ajax (type 11) |
| `level60` | Stage 1 | 2 | `level60_jsb` | Swan | 5 | -93.5, 18.8, 0 | 359 | Ajax (type 11) |
| `level60` | Stage 1 | 3 | `level60_mothers` | Swan | 5 | -93.5, 18.8, 0 | 359 | Ajax (type 11) |
| `level61` | Stage 2 | 1 | `level61_huns` | Swan | 5 | -108.2, 22, 0 | 89 | Ajax (type 11) |
| `level61` | Stage 2 | 2 | `level61_hihats` | Swan | 5 | -108.2, 22, 0 | 89 | Ajax (type 11) |
| `level61` | Stage 2 | 3 | `level61_punks` | Swan | 5 | -108.2, 22, 0 | 89 | Ajax (type 11) |
| `level62` | Stage 3 | 1 | `level62_lizzies` | Swan | 5 | -116.8, 22.9, 0 | 89 | Ajax (type 11) |
| `level62` | Stage 3 | 2 | `level62_boppers` | Swan | 5 | -116.8, 22.9, 0 | 89 | Ajax (type 11) |
| `level62` | Stage 3 | 3 | `level62_hurricanes` | Swan | 5 | -116.8, 22.9, 0 | 89 | Ajax (type 11) |
| `level63` | Stage 4 | 1 | `LEVEL63_orphans` | Swan | 5 | -70.7, 21.7, 0 | 89 | Ajax (type 11) |
| `level63` | Stage 4 | 2 | `LEVEL63_moonrunners` | Swan | 5 | -70.7, 21.7, 0 | 89 | Ajax (type 11) |
| `level64` | Stage 5 | 1 | `LEVEL64_rogues` | Swan | 5 | -542.3, 23, 0 | 89 | Ajax (type 11) |
| `level64` | Stage 5 | 2 | `LEVEL64_furies` | Swan | 5 | -542.3, 23, 0 | 89 | Ajax (type 11) |
| `level64` | Stage 5 | 3 | `LEVEL64_riffs` | Swan | 5 | -542.3, 23, 0 | 89 | Ajax (type 11) |
| `level64` | Stage 5 | 4 | `LEVEL64_cyrus` | Swan | 5 | -542.3, 23, 0 | 89 | Ajax (type 11) |

## Rumble arena starts

Each Rumble flag script `level<N>_<mode>_init.lua` places a list of flags for player 1 and another for player 2,
each flag with a name, position and heading. The table gives the first flag of the player-1 list for all 56 scripts
on the disc (33 arenas, 13 mode names). Mode is the middle part of the script's file name. Every player-1 list holds
9 or 10 flags. The arenas `level106`, `level117`, `level125` and `level135` have no flag script, no `.lev` and no
level script.

| Arena | Title | Mode | Flags in list | First flag | Position (x, y, z) | Heading |
| --- | --- | --- | ---: | --- | --- | ---: |
| `level101` | Junkyard | `kinghill` | 10 | `fP1_01` | -10.3, -5.5, 0 | 45 |
| `level102` | Fight Pen | `brawl` | 10 | `fP1_1` | -9.1, 8.9, -11.1 | 128 |
| `level103` | Warrior Turf | `brawl` | 10 | `fP1_01` | -66.4, 23.2, 0.3 | 95 |
| `level103` | Warrior Turf | `mercy` | 10 | `fP1_01` | -66.4, 23.2, 0.3 | 95 |
| `level103` | Warrior Turf | `wchair` | 10 | `fP1_01` | -73.9, 23.2, 0.3 | 95 |
| `level104` | The Rotunda | `brawl` | 9 | `fP1_1` | -5.3, 2.1, -8.5 | 88 |
| `level104` | The Rotunda | `mercy` | 10 | `fP1_01` | -1.2, 4, -8.5 | 183 |
| `level104` | The Rotunda | `wchair` | 10 | `fP1_01` | -1.2, 4, -8.5 | 183 |
| `level105` | Red Devil | `brawl` | 9 | `fP1_1` | 1.8, -5.8, -3.3 | 88 |
| `level107` | Courtyard | `brawl` | 9 | `fP1_1` | 10.4, -24.6, 0 | 90 |
| `level107` | Courtyard | `caps` | 9 | `fP1_1` | 10.4, -24.6, 0 | 90 |
| `level107` | Courtyard | `tagbt` | 9 | `fP1_1` | 19, -27.3, 0 | 216 |
| `level108` | Gunhill Road | `brawl` | 10 | `fP1_1` | -63.5, 461.6, 0.1 | 174 |
| `level108` | Gunhill Road | `mercy` | 9 | `fP1_01` | -60.6, 474.7, -0.6 | 0 |
| `level108` | Gunhill Road | `wchair` | 9 | `fP1_01` | -60.6, 474.7, -0.6 | 0 |
| `level109` | Shaolin Temple | `brawl` | 10 | `fP1_1` | 325.4, -105.5, 1.1 | 174 |
| `level110` | Club 45 | `brawl` | 10 | `fP1_1` | 29.6, -74.6, -201.6 | 221 |
| `level111` | Bensonhurst | `mercy` | 9 | `fP1_01` | -1.3, -7.5, 0 | 0 |
| `level111` | Bensonhurst | `car` | 9 | `fP1_01` | -5.7, -47.8, 0 | 179 |
| `level111` | Bensonhurst | `hifi` | 9 | `fP1_01` | -2.2, 14, 0 | 270 |
| `level112` | Coney Drag | `mercy` | 10 | `fP1_01` | -140, 39.5, 0.3 | 89 |
| `level112` | Coney Drag | `wchair` | 10 | `fP1_01` | -140, 39.5, 0.3 | 89 |
| `level113` | Swimming Pool | `brawl` | 10 | `fP1_1` | 220.5, 55.5, -2.9 | 358 |
| `level114` | Chinatown | `wchair` | 10 | `fP1_1` | -3.7, -46.2, 0.2 | 50 |
| `level114` | Chinatown | `muggr` | 10 | `fP1_1` | -3.7, -46.2, 0.2 | 50 |
| `level115` | Subway Platform | `brawl` | 10 | `fP1_1` | 75.4, 97.1, -0.1 | 178 |
| `level116` | The Shack | `brawl` | 9 | `fP1_1` | 12.5, -0.9, 0.1 | 180 |
| `level118` | Tack's Warehouse | `brawl` | 10 | `fP1_1` | -3.7, -1, 7.3 | 174 |
| `level119` | The Graveyard | `brawl` | 10 | `fP1_1` | -42.1, 93.6, 3.2 | 219 |
| `level120` | The Bridge | `brawl` | 10 | `fP1_1` | -65.7, 48.8, 0.6 | 257 |
| `level120` | The Bridge | `mercy` | 10 | `fP1_01` | -73.8, 56.4, 0.6 | 148 |
| `level121` | The Orphanage | `brawl` | 10 | `fP1_01` | 5.5, -7.6, -0.2 | 241 |
| `level121` | The Orphanage | `mercy` | 10 | `fP1_01` | 7.7, -25.9, -0.2 | 20 |
| `level121` | The Orphanage | `caps` | 10 | `fP1_01` | -0.9, -11.4, -0.2 | 60 |
| `level122` | Wrecked Apts | `brawl` | 9 | `fP1_1` | -1.6, 6.3, -1.9 | 166 |
| `level122` | Wrecked Apts | `tagbt` | 9 | `fP1_1` | 15.5, 6.3, 5 | 183 |
| `level122` | Wrecked Apts | `survival` | 9 | `fP1_1` | -1.6, 6.3, -1.9 | 166 |
| `level123` | Subway Bathroom | `brawl` | 10 | `fP1_1` | -3, 4.3, 0.2 | 177 |
| `level124` | Stripes and Solids | `brawl` | 10 | `fP1_1` | -3.6, -4.6, -200 | 89 |
| `level126` | Coney Amusement | `brawl` | 10 | `fP1_1` | 17.8, -28.1, 0.5 | 88 |
| `level127` | The Park | `brawl` | 10 | `fP1_01` | -219.3, -230.7, -11.8 | 0 |
| `level127` | The Park | `survival` | 10 | `fP1_01` | -219.3, -230.7, -11.8 | 0 |
| `level128` | The Old Junkyard | `brawl` | 10 | `fP1_1` | -175.3, 544.5, 16.2 | 88 |
| `level128` | The Old Junkyard | `survival` | 10 | `fP1_1` | -175.3, 544.5, 16.2 | 88 |
| `level129` | King o'Shanties | `kinghill` | 10 | `fP1_01` | 19.7, -8.7, 0.3 | 197 |
| `level130` | Mausoleum Hill | `brawl` | 10 | `fP1_1` | -3.2, -11.8, 14.1 | 88 |
| `level130` | Mausoleum Hill | `kinghill` | 10 | `fP1_01` | -6.8, -28.3, 11.3 | 21 |
| `level130` | Mausoleum Hill | `survival` | 10 | `fP1_1` | -3.2, -11.8, 14.1 | 88 |
| `level131` | Soho Rooftops | `royal` | 10 | `fP1_01` | -2.4, 0.8, 17 | 89 |
| `level132` | Tenement Rooftop | `royal` | 10 | `fP1_01` | 10.1, 229.1, 8.4 | 183 |
| `level133` | Machine Shop | `royal` | 10 | `fP1_01` | -56.9, 6.9, 13.8 | 89 |
| `level134` | Tremont | `brawl` | 10 | `fP1_1` | 11.7, 1.2, 0.1 | 332 |
| `level134` | Tremont | `survival` | 10 | `fP1_01` | 3.9, -6.2, 0 | 228 |
| `level134` | Tremont | `shoot` | 10 | `fP1_01` | 3.9, -6.2, 0 | 228 |
| `level136` | Gunhill Chase | `run4life` | 9 | `fP1_01` | -492.5, 579.2, 16.2 | 89 |
| `level137` | Furies' Chase | `run4life` | 10 | `fP1_01` | 28.3, 83.4, 0.1 | 183 |

## Acceptance checks

- Entry 3,772 (zero-based) of the PS2 disc's `WARRIORS.DIR` is `config_preload3.lua`, 308,938 bytes, beginning with
  the Lua 4.0 signature.
- The level list in `config_preload3.lua` holds 111 records of 15 values each.
- The records with orders 1 to 23 are titled, in order: New Blood; Real Live Bunch; Payback; Blackout; Real Heavy
  Rep; Writer's Block; Adios Amigo; Encore; Payin' The Cost; Destroyed; Boys In Blue; Set Up; All-City; Desperate
  Dudes; No Permits, No Parley; Home Run; Friendly Faces; Come Out To Play; Roots; The Best; Heavy Muscle; Scout's
  Honor; Sharp Dressed Man.
- In game the story missions follow that order: Real Live Bunch follows New Blood, Payback follows Real Live Bunch,
  and Come Out To Play is the 18th.
- The mission titled Blackout is `level34`, location label `4:Riverside`; the mission titled Roots is `level82`,
  label `A:East Coney`.
- 64 records have a `.lev` entry on the disc; the titled arenas The Shanties, Tenement Lobby, Spookarama and Harlem
  (`level106`, `level117`, `level125`, `level135`) have none.
- In Blackout the player controls Vermin, in Real Heavy Rep Cleon, in Boys In Blue Cochise and in Scout's Honor Fox.
- In Stage 1 to Stage 5 the player controls Swan.
- Of the 13 mode names on this page, the Rumble arena Junkyard (`level101`) has a flag script for `kinghill` only,
  and Warrior Turf (`level103`) for `brawl`, `mercy` and `wchair`.
- Every Rumble flag script's player-1 list holds 9 or 10 flags.

## Open questions

- What LT 1, LT 2 and LT 3 select in game; what Point, Value A and Value B are; what the Subway constant shows.
- Sections equals the number of mission-table entries for nine of the ten levels in the story table; `level63` has
  3 sections and 2 entries. What Sections counts is unconfirmed.
- How a mission-table entry number relates to the checkpoint a level is resumed from.
- Starts for the other 18 titled levels of orders 1 to 23 and for `level95`: their scripts have no mission table of
  the shape above, and their placements are not tabulated.
- Whether player 1 starts exactly at the first flag of the player-1 list in every Rumble mode (not seen in game).
- The unit of the positions and the direction a heading of 0 faces.
- The strings file has a title (Graveyard Shift) and a label (`17:Prospect Park`) for `level7`, which has no record.
  Label number 21 belongs to two arenas (`level108`, `level135`); 17 belongs to `level104` and to `level7`.
- Whether the four arenas without files appear anywhere in game.
- The Xbox disc's copy of these scripts was not compared.
