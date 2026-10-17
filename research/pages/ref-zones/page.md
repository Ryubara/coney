# Zone list

This reference page lists every zone of every level, with what the disc data does with it. A zone is a numbered group
of a level's objects; the page `zones` describes what zones are and what switching them does. Every value was read
from the PS2 disc (NTSC-U, `SLUS_212.15`), from entries of `WARRIORS.WAD` located through `WARRIORS.DIR`: the 63
object lists `<level>_objs.txt` and the compiled Lua 4.0 scripts.

## Disc sources

| Source | Values taken from it |
| --- | --- |
| `<level>_objs.txt` | Listed objects: the number of object lines carrying each zone number (the second integer after the rotation) |
| `<level>.lua` | Names: the globals `Zone<N>` the level script sets to N; script spawns made by the level script |
| `<level>_<part>.lua` | Which scripts switch each zone on or off, restore it or set handlers on it; table names ending in `_ZONE` |
| `global.lua` (entry 4,105) | The shared level set-up, which switches on each of zones 21 to 32 that the level script names |
| Entries 9,499 to 9,510 | Seven unnamed scripts among the `level95` scripts that spawn objects into zone 3 |

## Columns

| Column | Meaning |
| --- | --- |
| Level | The level's name, as in the level list |
| Zone | The zone number |
| Names in the scripts | Script names whose value is this zone number; empty when no script names it |
| Listed objects | Lines of the level's object list in this zone |
| Script spawns | Object-spawn commands in the named script that give this zone, counted where they stand in the script |
| Switched on by | Scripts that switch the zone on; `global` is the shared set-up in `global.lua` |
| Switched off by | Scripts that switch the zone off |
| Other uses | Scripts that restore the zone, set handlers on it (message numbers in brackets) or keep vandals off it; and, in Rumble arenas, the mode constant the zone answers to |

A row exists for every zone number that the level's object list, its scripts or its zone names use. Script names
are given without the `.lua` extension.

## Totals

| Fact | Value |
| --- | ---: |
| Levels with rows | 62 |
| Rows | 225 |
| Listed objects over all rows | 10,301 |
| Rows with listed objects | 128 |
| Script spawns giving a zone other than 0, in named level scripts | 453 |
| Script spawns into zone 3 from entries 9,499 to 9,510 | 237 |
| Rows with a name | 176 |

`level1` (an object list holding the count 0, no zone names) and `level100` (no object list) have no rows.

## Zones by level

| Level | Zone | Names in the scripts | Listed objects | Script spawns | Switched on by | Switched off by | Other uses |
| --- | ---: | --- | ---: | --- | --- | --- | --- |
| `level2` | 0 | `Zone0` | 485 | | | | |
| `level2` | 32 | `Zone32` | 13 | | `global` | | |
| `level3` | 0 | `Zone0` | 110 | | | | |
| `level3` | 2 | `Zone2` | 18 | | `level3_balcony` | | |
| `level3` | 4 | `Zone4` | | 163 (`level3`) | `level3_chase`, `level3_gallery` | | |
| `level5` | 0 | `Zone0` | 48 | | | | |
| `level5` | 1 | `Zone1` | 209 | | `level5_chapter1` | `level5_chapter2` | |
| `level5` | 2 | `Zone2` | 73 | 5 (`level5`) | `level5_chapter2`, `level5_scenetest` | | |
| `level5` | 3 | `Zone3` | 184 | 9 (`level5`) | `level5_chapter2` | `level5_chapter3` | |
| `level5` | 5 | `Zone5` | 50 | 22 (`level5`) | `level5_chapter3`, `level5_scenetest` | | restored by `level5_chapter3` |
| `level5` | 6 | `Zone6` | 31 | 1 (`level5`) | `level5_chapter1a`, `level5_scenetest` | `level5_chapter1a` | |
| `level5` | 10 | `Zone10` | 51 | | `level5_chapter1a` | `level5_chapter1a` | |
| `level9` | 0 | `Zone0` | 322 | | | | |
| `level9` | 1 | `Zone1` | | 1 (`level9`) | `level9_chap1_radio`, `level9_scenetest` | | |
| `level9` | 41 | `Zone41` | | 1 (`level9`) | `level9_chap1_radio` | `level9_chap1_radio` | |
| `level11` | 0 | `Zone0` | 346 | | | | |
| `level11` | 4 | `Zone4` | 31 | 11 (`level11`) | `level11_chapter4`, `level11_scenetest` | `level11_chapter4` | |
| `level11` | 5 | `Zone5` | 62 | | `level11_chapter4`, `level11_chapter5` | `level11_chapter5` | |
| `level11` | 7 | `Zone7` | | 1 (`level11`) | `level11_chapter6`, `level11_scenetest` | | |
| `level11` | 11 | `Zone11` | | 1 (`level11`) | `level11_chapter5` | | |
| `level11` | 24 | `Zone24` | 21 | | `global` | | |
| `level11` | 80 | `Zone80` | | 1 (`level11`) | `level11_chapter6` | `level11_chapter6` | |
| `level11` | 81 | `Zone81` | | 1 (`level11`) | `level11_chapter6` | `level11_chapter6` | |
| `level14` | 0 | `Zone0` | 350 | | | | |
| `level14` | 24 | `Zone24` | 24 | | `global` | | |
| `level14` | 26 | `Zone26` | 28 | | `global` | | |
| `level14` | 27 | `Zone27` | 17 | | `global` | | |
| `level14` | 30 | `Zone30` | 34 | | `global` | | |
| `level14` | 32 | `Zone32` | 24 | | `global` | | |
| `level20` | 0 | `Zone0` | 602 | | | | |
| `level20` | 5 | `Zone5` | | 5 (`level20`) | `level20_marketplace` | | |
| `level20` | 31 | `Zone31` | 20 | | `global` | | |
| `level31` | 0 | `Zone0` | 410 | | | | |
| `level31` | 15 | `Zone15` | | 1 (`level31`) | `level31_gclub` | | |
| `level31` | 29 | `Zone29` | 23 | | `global` | | |
| `level31` | 87 | `Zone87` | 41 | | `level31_reddevil` | | |
| `level31` | 111 | | 55 | | `level31_reddevil` | `level31_reddevil` | |
| `level34` | 0 | `Zone0` | 182 | | | | |
| `level34` | 2 | `Zone2` | 2 | | `level34_riot` | | |
| `level34` | 3 | `Zone3` | 5 | | `level34_riot` | | |
| `level34` | 32 | `Zone32` | 22 | | `global`, `level34_gate`, `level34_riot` | | |
| `level34` | 33 | `Zone33` | 21 | | `level34_riot` | | |
| `level34` | 34 | `Zone34` | 24 | | `level34_gate`, `level34_riot` | | |
| `level34` | 35 | `Zone35` | 22 | | `level34_riot` | | |
| `level34` | 36 | `Zone36` | 13 | | `level34_gate`, `level34_riot` | | |
| `level34` | 37 | `Zone37` | 21 | | `level34_riot` | | |
| `level34` | 38 | `Zone38` | 16 | | `level34_gate`, `level34_riot` | | |
| `level34` | 40 | `Zone40` | 8 | | `level34_gate`, `level34_riot` | | |
| `level34` | 41 | `Zone41` | 8 | | `level34_riot` | | |
| `level34` | 42 | `Zone42` | 6 | | `level34_gate`, `level34_riot` | | |
| `level34` | 43 | `Zone43` | 10 | | `level34_riot` | | |
| `level34` | 44 | `Zone44` | 10 | | `level34_gate`, `level34_riot` | | |
| `level34` | 45 | `Zone45` | 8 | | `level34_riot` | | |
| `level34` | 46 | | | | `level34_gate` | | |
| `level34` | 48 | | | | `level34_gate` | | |
| `level34` | 50 | `Zone50` | | 14 (`level34`) | `level34_park`, `level34_riot`, `level34_scenetest` | | |
| `level34` | 51 | `Zone51` | 9 | | `level34_riot` | | |
| `level51` | 0 | `Zone0` | 12 | | | | |
| `level51` | 1 | `Zone1` | 134 | | `level51_chapter1`, `level51_scenetest` | `level51_chapter2` | |
| `level51` | 2 | `Zone2` | 286 | | `level51_chapter1`, `level51_chapter2`, `level51_chapter2a` | `level51_chapter2a` | |
| `level51` | 3 | `Zone3` | 198 | 2 (`level51`) | `level51_chapter3` | `level51_chapter3` | |
| `level51` | 4 | `Zone4` | 2 | | | | |
| `level51` | 5 | `Zone5` | 220 | | `level51_chapter3`, `level51_chapter5` | `level51_chapter8` | |
| `level51` | 6 | `Zone6` | 64 | | `level51_chapter5` | `level51_chapter8` | |
| `level51` | 7 | `Zone7` | 85 | 56 (`level51`) | `level51_chapter8` | `level51_chapter8` | restored by `level51_chapter8` |
| `level51` | 9 | | | | `level51_chapter9`, `level51_scenetest` | `level51_chapter9` | restored by `level51_chapter9` |
| `level51` | 12 | `Zone12` | | 1 (`level51`) | `level51_chapter9`, `level51_scenetest` | `level51_chapter9` | |
| `level51` | 13 | | | | `level51_scenetest` | | |
| `level51` | 20 | `Zone20` | 26 | | `level51_chapter3` | | handlers by `level51_chapter3` (2) |
| `level52` | 0 | `Zone0` | 218 | | | | |
| `level52` | 3 | | | | `level52_chapter4` | | |
| `level52` | 31 | `Zone31` | 23 | | `global` | | |
| `level54` | 0 | `Zone0` | 272 | | | | |
| `level54` | 3 | | | | `level54_boss`, `level54_scenetest` | | |
| `level54` | 4 | `Zone4` | | 3 (`level54`) | `level54_boss`, `level54_scenetest` | `level54`, `level54_boss` | |
| `level54` | 9 | `Zone9` | | 2 (`level54`) | `level54_boss`, `level54_scenetest` | `level54_boss` | |
| `level55` | 0 | `Zone0` | 78 | | | | |
| `level55` | 2 | `Zone2` | 21 | | `level55` | | kept from vandals by `level55` |
| `level60` | 0 | `Zone0` | | | | | |
| `level61` | 0 | `Zone0` | | | | | |
| `level62` | 0 | `Zone0` | | | | | |
| `level63` | 0 | `Zone0` | | | | | |
| `level64` | 0 | `Zone0` | | | | | |
| `level80` | 0 | `Zone0` | 215 | | | | |
| `level80` | 24 | `Zone24` | 27 | | `global` | | |
| `level81` | 0 | `Zone0` | 212 | | | | |
| `level82` | 0 | `Zone0` | 12 | | | | |
| `level82` | 1 | `Zone1` | 11 | | `level82_chapter4` | `level82_chapter5` | |
| `level82` | 2 | `Zone2` | 48 | | `level82_chapter2` | `level82_chapter1`, `level82_chapter2` | |
| `level82` | 3 | `Zone3` | 778 | | `level82_chapter1`, `level82_chapter2` | | |
| `level82` | 5 | `Zone5` | 30 | | `level82_chapter5` | `level82_chapter5` | restored by `level82_chapter5` |
| `level82` | 6 | `Zone6` | 1 | | | `level82_chapter5` | restored by `level82_chapter5` |
| `level83` | 0 | `Zone0` | 390 | | | | |
| `level83` | 24 | `Zone24` | 29 | | `global` | | |
| `level83` | 30 | `Zone30` | 38 | | `global` | | handlers by `level83_c5_apartment` (2) |
| `level84` | 0 | `Zone0` | 4 | | | | |
| `level84` | 1 | `Zone1` | | 5 (`level84`) | `level84_intro`, `level84_scenetest` | `level84_chase` | |
| `level84` | 2 | `Zone2` | 12 | | `level84_chase`, `level84_final` | `level84_chase` | restored by `level84_chase` |
| `level84` | 3 | `Zone3` | 9 | | `level84_final` | `level84_final` | restored by `level84_final` |
| `level84` | 4 | `Zone4` | 24 | | `level84_final`, `level84_scenetest` | `level84_final` | restored by `level84_final` |
| `level86` | 0 | `Zone0` | 92 | | | | |
| `level86` | 1 | `Zone1` | 34 | | `level86`, `level86_chapter2_reg` | `level86_chapter3_reg` | |
| `level86` | 2 | `Zone2` | 11 | | `level86` | `level86_chapter3_reg` | |
| `level86` | 3 | `Zone3` | 12 | | `level86` | `level86_chapter3_reg` | |
| `level86` | 4 | `Zone4` | 29 | 2 (`level86`) | `level86` | `level86_chapter3_reg` | |
| `level86` | 5 | | | | `level86` | `level86_chapter3_reg` | |
| `level86` | 6 | `Zone6` | 39 | | `level86` | `level86_chapter3_reg` | |
| `level86` | 7 | `Zone7` | 44 | | `level86` | `level86_chapter3_reg` | |
| `level86` | 9 | | | | `level86_chapter4` | | |
| `level86` | 10 | `Zone10` | 20 | | `level86` | `level86_chapter3_reg`, `level86_chapter4` | |
| `level87` | 0 | `Zone0` | 604 | | | | |
| `level87` | 1 | `Zone1` | | 2 (`level87`) | `level87_chap1_tutorial`, `level87_chap5_club`, `level87_scenetest` | | |
| `level87` | 2 | | | | | `level87` | |
| `level87` | 5 | `Zone5` | 75 | 2 (`level87`) | `level87_chap5_club`, `level87_scenetest` | | |
| `level87` | 6 | `Zone6` | | 1 (`level87`) | `level87_chap5_club`, `level87_scenetest` | | |
| `level87` | 27 | `Zone27` | 21 | | `global` | | |
| `level87` | 31 | `Zone31` | 20 | | `global` | | |
| `level92` | 0 | `Zone0` | 157 | | | | |
| `level92` | 1 | `VD.VOODOO_ZONE`, `Zone1` | 5 | | `level92_chapter2` | `level92_chapter2` | handlers by `level92_chapter2` (1, 2) |
| `level92` | 2 | `MT.MEAT_ZONE`, `Zone2` | 46 | | `level92_chapter2` | `level92_chapter2` | handlers by `level92_chapter2` (2) |
| `level92` | 3 | `PB.PLUMB_ZONE`, `Zone3` | 35 | | `level92_chapter2` | `level92_chapter2` | handlers by `level92_chapter2` (1, 2) |
| `level92` | 4 | `HD.HEAD_ZONE`, `Zone4` | 44 | | `level92_chapter2` | `level92_chapter2` | handlers by `level92_chapter2` (1, 2) |
| `level92` | 6 | | | | | `level92_chapter1` | |
| `level93` | 0 | `Zone0` | 206 | | | | |
| `level93` | 2 | `PH.PHARMACY_ZONE`, `Zone2` | 16 | | `level93_chapter1` | `level93_chapter1`, `level93_chapter2` | handlers by `level93_chapter1` (1, 2) |
| `level93` | 3 | `LQ.LIQUOR_ZONE`, `Zone3` | 20 | | `level93_chapter1` | `level93_chapter1`, `level93_chapter2` | handlers by `level93_chapter1` (1, 2) |
| `level93` | 4 | `WH.WAREHOUSE_ZONE`, `Zone4` | 17 | | `level93_chapter2` | `level93_chapter2` | handlers by `level93_chapter2` (1, 2) |
| `level93` | 5 | `GH.GAMBLE_ZONE`, `Zone5` | 18 | 4 (`level93`) | `level93_chapter2` | `level93_chapter2` | handlers by `level93_chapter2` (1, 2) |
| `level93` | 6 | `C4.PATH_BLOCK_ZONE`, `Zone6` | 10 | | `level93_chapter4` | `level93_chapter4` | |
| `level93` | 66 | `Zone66` | 55 | 11 (`level93`); 1 (`level93_chapter6`) | `level93_chapter6` | `level93_chapter6` | restored by `level93_chapter6` |
| `level95` | 0 | `Zone0` | 35 | | | | |
| `level95` | 1 | `Zone1` | 6 | 16 (`level95`) | `level95`, `level95_clubhouse` | `level95`, `level95_clubhouse` | restored by `level95`, `level95_clubhouse` |
| `level95` | 2 | `Zone2` | 7 | 1 (`level95`) | | `level95`, `level95_clubhouse` | |
| `level95` | 3 | `Zone3` | 7 | 237 (entries 9,499 to 9,510) | | `level95`, `level95_clubhouse` | |
| `level95` | 4 | | | | | `level95`, `level95_clubhouse` | |
| `level95` | 5 | `Zone5` | 2 | 5 (`level95`) | `level95`, `level95_clubhouse` | `level95`, `level95_clubhouse` | restored by `level95`, `level95_clubhouse` |
| `level95` | 6 | `Zone6` | 7 | | | | |
| `level95` | 7 | | | 1 (`level95`) | `level95` | | |
| `level95` | 8 | `Zone8` | | 1 (`level95`) | `level95`, `level95_clubhouse` | | restored by `level95`, `level95_clubhouse` |
| `level95` | 9 | | | 1 (`level95`) | | `level95`, `level95_clubhouse` | |
| `level95` | 11 | `Zone11` | 3 | | `level95`, `level95_clubhouse` | | |
| `level95` | 13 | `Zone13` | 120 | | `level95_coney` | `level95_coney` | |
| `level95` | 14 | | | | `level95_coney` | `level95_coney` | |
| `level95` | 15 | `Zone15`, `homeless.MEAT_ZONE` | 38 | | `level95_coney` | `level95_coney` | restored by `level95_homeless` |
| `level95` | 16 | `Zone16` | 25 | | `level95_coney` | `level95_coney` | restored by `level95_flashhead` |
| `level95` | 17 | `Zone17` | 4 | | `level95_coney` | `level95_coney` | |
| `level95` | 19 | `Zone19` | | 2 (`level95`) | | | |
| `level95` | 32 | `Zone32`, `roam.JEWELERY_ZONE` | 15 | | `global`, `level95_coney` | `level95_coney` | restored by `level95_coney` |
| `level99` | 0 | `Zone0` | 89 | | | | |
| `level99` | 1 | `Zone1` | | 3 (`level99`) | `level99_combat` | | |
| `level99` | 26 | `Zone26` | 23 | | `global` | | |
| `level101` | 21 | | | | | `level101` | |
| `level102` | 0 | `Zone0` | | | | | |
| `level102` | 1 | `Zone1` | 18 | | | | mode zone for `RM_Brawl` |
| `level102` | 21 | | | | | `level102` | |
| `level103` | 0 | `Zone0` | 1 | | | | |
| `level103` | 21 | | | | | `level103` | |
| `level103` | 24 | `Zone24` | | 4 (`level103`) | `global` | `wchair` | mode zone for `RM_Wchair` |
| `level104` | 0 | `Zone0` | | | | | |
| `level104` | 21 | | | | | `level104` | |
| `level104` | 24 | `Zone24` | | 3 (`level104`) | `global` | `wchair` | mode zone for `RM_Wchair` |
| `level105` | 0 | `Zone0` | 104 | | | | |
| `level105` | 21 | | | | | `level105` | |
| `level107` | 0 | `Zone0` | | | | | |
| `level107` | 19 | `Zone19` | 25 | | | | mode zone for `RM_TagBt` |
| `level107` | 21 | | | | | `level107` | |
| `level108` | 21 | | | | | `level108` | |
| `level108` | 24 | `Zone24` | | 9 (`level108`) | `global` | `wchair` | mode zone for `RM_Wchair` |
| `level109` | 0 | `Zone0` | 45 | | | | |
| `level109` | 21 | | | | | `level109` | |
| `level110` | 0 | `Zone0` | 57 | | | | |
| `level110` | 21 | | | | | `level110` | |
| `level111` | 0 | `Zone0` | 54 | | | | |
| `level111` | 11 | `Zone11` | | 31 (`level111`) | | | mode zone for `RM_Hifi` |
| `level111` | 21 | | | | | `level111` | |
| `level111` | 30 | `Zone30` | 3 | | `global` | | |
| `level112` | 0 | `Zone0` | 46 | | | | |
| `level112` | 21 | | | | | `level112` | |
| `level112` | 24 | `Zone24` | | 16 (`level112`) | `global` | `wchair` | mode zone for `RM_Wchair` |
| `level113` | 0 | `Zone0` | | | | | |
| `level113` | 21 | | | | | `level113` | |
| `level114` | 0 | `Zone0` | 2 | | | | |
| `level114` | 18 | `Zone18` | 175 | | | | mode zone for `RM_Muggr` |
| `level114` | 21 | | | | | `level114` | |
| `level114` | 24 | `Zone24` | | 9 (`level114`) | `global` | `wchair` | mode zone for `RM_Wchair` |
| `level115` | 0 | `Zone0` | | | | | |
| `level115` | 21 | | | | | `level115` | |
| `level116` | 0 | `Zone0` | | | | | |
| `level116` | 21 | | | | | `level116` | |
| `level118` | 0 | `Zone0` | | | | | |
| `level118` | 21 | | | | | `level118` | |
| `level119` | 0 | `Zone0` | | | | | |
| `level119` | 21 | | | | | `level119` | |
| `level120` | 0 | `Zone0` | | | | | |
| `level120` | 21 | | | | | `level120` | |
| `level121` | 0 | `Zone0` | 54 | | | | |
| `level121` | 21 | | | | | `level121` | |
| `level122` | 0 | `Zone0` | | | | | |
| `level122` | 21 | | | | | `level122` | |
| `level123` | 0 | `Zone0` | 47 | | | | |
| `level123` | 21 | | | | | `level123` | |
| `level124` | 0 | `Zone0` | 78 | | | | |
| `level124` | 21 | | | | | `level124` | |
| `level126` | 0 | `Zone0` | 33 | | | | |
| `level126` | 21 | | | | | `level126` | |
| `level127` | 0 | `Zone0` | | | | | |
| `level127` | 21 | | | | | `level127` | |
| `level128` | 0 | `Zone0` | | | | | |
| `level128` | 21 | | | | | `level128` | |
| `level129` | 21 | | | | | `level129` | |
| `level130` | 2 | `Zone2` | | 18 (`level130`) | | | mode zone for `RM_Koth` |
| `level130` | 21 | | | | | `level130` | |
| `level131` | 0 | `Zone0` | | | | | |
| `level131` | 21 | | | | | `level131` | |
| `level132` | 0 | `Zone0` | | | | | |
| `level132` | 21 | | | | | `level132` | |
| `level133` | 0 | `Zone0` | | | | | |
| `level133` | 21 | | | | | `level133` | |
| `level134` | 0 | `Zone0` | | | | | |
| `level134` | 21 | | | | | `level134` | |
| `level134` | 30 | | | 4 (`level134`) | | | |
| `level136` | 0 | `Zone0` | | | | | |
| `level136` | 21 | | | | | `level136` | |
| `level137` | 0 | `Zone0` | | | | | |
| `level137` | 21 | | | | | `level137` | |

The Rumble arena scripts also switch on the zone numbered after the current Rumble mode; that use is shown only in
the rows where the arena has objects or a name for that zone. `wchair` is the Rumble mode script for `RM_Wchair`.

## Acceptance checks

- The 63 object lists on the PS2 disc hold 10,301 object lines, and the Listed objects column sums to 10,301.
- `level82_objs.txt` places 778 of its 880 lines in zone 3.
- `level114_objs.txt` places 175 of its 177 lines in zone 18 and 2 in zone 0.
- `level31_objs.txt` places 55 lines in zone 111, the highest zone number in any object list.
- `level34` names 18 zones, from `Zone0` to `Zone51`, and `level34_riot` switches on 17 zones.
- `level92_chapter2` sets `VD.VOODOO_ZONE` 1, `MT.MEAT_ZONE` 2, `PB.PLUMB_ZONE` 3 and `HD.HEAD_ZONE` 4.
- `level93_chapter1` sets `PH.PHARMACY_ZONE` 2 and `LQ.LIQUOR_ZONE` 3; `level93_chapter2` sets `WH.WAREHOUSE_ZONE`
  4 and `GH.GAMBLE_ZONE` 5; `level93_chapter4` sets `C4.PATH_BLOCK_ZONE` 6.
- Each of the 33 Rumble arena scripts from `level101` to `level137` switches off zone 21.
- `level60` to `level64` name only `Zone0` and their object lists hold the count 0.

## Open questions

- The names of the seven unnamed scripts at entries 9,499 to 9,510; they are counted with `level95` here because they
  sit among its scripts and spawn into its zone 3.
- `level95` also switches on zones held in a script table that was not read; those zones are not in the Switched on
  by column.
- Script spawns are counted where they stand in a script; a spawn command inside a repeated part of a script may
  place more objects in game.
- Zone 30 of `level134` and zone 21 of the arenas have no script that switches them on.
- The Xbox disc was not examined.
