# Box list

This reference page lists every box that the level scripts place in the game's levels: 974 boxes in 53 scripts.
Every value was read from the PS2 disc (NTSC-U, `SLUS_212.15`), from the compiled Lua 4.0 scripts stored in
`WARRIORS.WAD` and located through `WARRIORS.DIR`. The level scripts hold all of the game's boxes; no other file
on the disc places one. What a box does in play is on the boxes page; level names and titles are on ref-levels.

## Disc sources

| Source | Directory entries (zero-based) | Values taken from it |
| --- | --- | --- |
| `level<N>.lua` | one per story level, 29 with boxes | Boxes of the story levels and Stage 1 to Stage 5 |
| `level<N>_<mode>_init.lua` | 23 with boxes | Boxes of the Rumble arenas |
| Unnamed entry 7,105 | 7,105 | Three boxes for the Bensonhurst arena (`level111`); its file name is not recovered |
| The same scripts | as above | The pool sizes each script sets for the three box kinds |

Entry 7,105 lies between `level111_hifi_init.lua` (7,104) and `level111_mercy_init.lua` (7,106) in the directory,
sets the same pool sizes as `level111_mercy_init.lua` and places the same three store boxes as three other `level111`
scripts.

## Columns

Every box is placed with four values: a name, a kind, a corner and a size. The tables give them as stored, with
the turn values where the script turns the box.

| Column | Type | Meaning |
| --- | --- | --- |
| Box | text | The box's name as written in the script (3 to 26 characters; the game keeps the first 15) |
| Kind | integer | 0 any-character box, 2 player box, 3 turf box (see the boxes page) |
| Corner | three numbers | x, y, z of the corner with the lowest values |
| Size | three numbers | Extent along x, y and z from the corner; every size on the disc is above 0 |
| Turn | four numbers or none | The 2 x 2 matrix a, b, c, d that turns the box's footprint about its centre |

A turned box covers its footprint turned about the footprint's centre; the z range is never turned. 211 boxes are
turned. The four values have at most three decimals and are close to a rotation. For example 0, -1, 1, 0 turns the
footprint by a quarter turn; 10 boxes carry the plain 1, 0, 0, 1. Corners and sizes have at most one decimal.

## Kinds on the disc

Smallest and largest are by volume.

| Kind | Boxes | Turned | Smallest size | Largest size |
| ---: | ---: | ---: | --- | --- |
| 0 | 547 | 108 | 1.1, 1.1, 3 (`vStreetStall_03`) | 363.5, 85.3, 63.9 (`vChaseKill`) |
| 2 | 251 | 61 | 2, 2, 3.5 (`vPowerSwitch`) | 110.2, 163.5, 20 (`vbPlaySubway`) |
| 3 | 176 | 42 | 3.9, 4, 2.2 (`vFightTurf`) | 260, 460, 70 (`vbparkTurf`) |
| 1 | 0 | 0 | none | none |

No script places a kind-1 box, and no script gives a box more than the four values.

## Counts per script

Pools are the sizes the same script sets for kinds 0, 1, 2 and 3 before it places its boxes. In every script the
kind-0 pool equals the number of kind-0 boxes; the kind-2 and kind-3 pools are never smaller than their counts.
The kind-1 pool is 0 everywhere except in `level100.lua`, which sets 2, 1, 2, 3 and places no boxes.

| Script | Entry | Level title | Kind 0 | Kind 2 | Kind 3 | Boxes | Pools (0, 1, 2, 3) |
| --- | ---: | --- | ---: | ---: | ---: | ---: | --- |
| `level2.lua` | 7,707 | Real Heavy Rep | 15 | 5 | 8 | 28 | 15, 0, 9, 8 |
| `level3.lua` | 7,855 | Writer's Block | 25 | 29 | 4 | 58 | 25, 0, 37, 4 |
| `level5.lua` | 8,028 | Adios Amigo | 35 | 7 | 5 | 47 | 35, 0, 15, 6 |
| `level9.lua` | 9,228 | All-City | 3 | 24 | 7 | 34 | 3, 0, 30, 14 |
| `level11.lua` | 7,072 | Sharp Dressed Man | 10 | 5 | 19 | 34 | 10, 0, 13, 21 |
| `level14.lua` | 7,654 | Set Up | 9 | 10 | 9 | 28 | 9, 0, 14, 13 |
| `level20.lua` | 7,709 | Scout's Honor | 32 | 8 | 5 | 45 | 32, 0, 17, 5 |
| `level31.lua` | 7,859 | Boys In Blue | 19 | 0 | 13 | 32 | 19, 0, 10, 19 |
| `level34.lua` | 7,919 | Blackout | 20 | 0 | 6 | 26 | 20, 0, 5, 9 |
| `level51.lua` | 8,030 | Desperate Dudes | 59 | 12 | 10 | 81 | 59, 0, 22, 10 |
| `level52.lua` | 8,190 | No Permits, No Parley | 30 | 1 | 4 | 35 | 30, 0, 6, 10 |
| `level54.lua` | 8,261 | Home Run | 17 | 52 | 8 | 77 | 17, 0, 64, 12 |
| `level55.lua` | 8,356 | Friendly Faces | 14 | 0 | 1 | 15 | 14, 0, 7, 1 |
| `level60.lua` | 8,487 | Stage 1 | 4 | 0 | 0 | 4 | 4, 0, 1, 0 |
| `level61.lua` | 8,513 | Stage 2 | 6 | 0 | 2 | 8 | 6, 0, 1, 2 |
| `level62.lua` | 8,545 | Stage 3 | 6 | 0 | 0 | 6 | 6, 0, 1, 0 |
| `level63.lua` | 8,569 | Stage 4 | 1 | 0 | 0 | 1 | 1, 0, 1, 0 |
| `level64.lua` | 8,594 | Stage 5 | 2 | 0 | 2 | 4 | 2, 0, 4, 2 |
| `level80.lua` | 8,729 | Real Live Bunch | 18 | 4 | 3 | 25 | 18, 0, 12, 6 |
| `level81.lua` | 8,779 | Encore | 19 | 7 | 2 | 28 | 19, 0, 14, 2 |
| `level82.lua` | 8,845 | Roots | 30 | 8 | 13 | 51 | 30, 0, 14, 20 |
| `level83.lua` | 8,915 | Heavy Muscle | 4 | 18 | 8 | 30 | 4, 0, 28, 9 |
| `level84.lua` | 9,029 | Come Out To Play | 12 | 0 | 4 | 16 | 12, 0, 10, 4 |
| `level86.lua` | 9,116 | Payin' The Cost | 19 | 7 | 6 | 32 | 19, 0, 18, 8 |
| `level87.lua` | 9,170 | Payback | 12 | 27 | 5 | 44 | 12, 0, 34, 7 |
| `level92.lua` | 9,257 | The Best | 18 | 5 | 10 | 33 | 18, 0, 10, 10 |
| `level93.lua` | 9,320 | Destroyed | 30 | 12 | 17 | 59 | 30, 0, 28, 17 |
| `level95.lua` | 9,400 | none | 5 | 10 | 1 | 16 | 5, 0, 10, 1 |
| `level99.lua` | 9,606 | New Blood | 16 | 0 | 0 | 16 | 16, 0, 7, 0 |
| `level101_kinghill_init.lua` | 6,913 | Junkyard | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level103_wchair_init.lua` | 6,955 | Warrior Turf | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level104_wchair_init.lua` | 6,981 | The Rotunda | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level105_brawl_init.lua` | 6,991 | Red Devil | 4 | 0 | 0 | 4 | 4, 0, 0, 0 |
| `level107_tagbt_init.lua` | 7,023 | Courtyard | 0 | 0 | 1 | 1 | 0, 0, 0, 1 |
| `level108_mercy_init.lua` | 7,045 | Gunhill Road | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level108_wchair_init.lua` | 7,048 | Gunhill Road | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level111_car_init.lua` | 7,103 | Bensonhurst | 5 | 0 | 0 | 5 | 5, 0, 0, 0 |
| `level111_hifi_init.lua` | 7,104 | Bensonhurst | 3 | 0 | 0 | 3 | 3, 0, 0, 1 |
| `level111_mercy_init.lua` | 7,106 | Bensonhurst | 3 | 0 | 0 | 3 | 3, 0, 0, 0 |
| unnamed | 7,105 | Bensonhurst | 3 | 0 | 0 | 3 | 3, 0, 0, 0 |
| `level112_mercy_init.lua` | 7,135 | Coney Drag | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level112_wchair_init.lua` | 7,137 | Coney Drag | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level114_wchair_init.lua` | 7,179 | Chinatown | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level122_tagbt_init.lua` | 7,412 | Wrecked Apts | 0 | 0 | 1 | 1 | 0, 0, 0, 1 |
| `level129_kinghill_init.lua` | 7,514 | King o'Shanties | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level130_kinghill_init.lua` | 7,528 | Mausoleum Hill | 1 | 0 | 0 | 1 | 1, 0, 0, 0 |
| `level131_royal_init.lua` | 7,544 | Soho Rooftops | 9 | 0 | 0 | 9 | 9, 0, 0, 0 |
| `level132_royal_init.lua` | 7,557 | Tenement Rooftop | 9 | 0 | 0 | 9 | 9, 0, 0, 0 |
| `level133_royal_init.lua` | 7,584 | Machine Shop | 9 | 0 | 0 | 9 | 9, 0, 0, 0 |
| `level134_shoot_init.lua` | 7,603 | Tremont | 0 | 0 | 1 | 1 | 0, 0, 0, 1 |
| `level134_survival_init.lua` | 7,605 | Tremont | 0 | 0 | 1 | 1 | 0, 0, 0, 1 |
| `level136_run4life_init.lua` | 7,637 | Gunhill Chase | 1 | 0 | 0 | 1 | 1, 0, 0, 2 |
| `level137_run4life_init.lua` | 7,652 | Furies' Chase | 1 | 0 | 0 | 1 | 1, 0, 0, 2 |

## Story level boxes

One table per level script, boxes in the order the script places them.

### level2

Real Heavy Rep, entry 7,707, 28 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbIntroCam_01` | 2 | 453.2, 197, 12.8 | 5.9, 64, 2.9 | none |
| `vbIntroCam_02` | 2 | 458.1, 198.2, 12.3 | 4.7, 14, 12 | -0.768, -0.64, 0.64, -0.768 |
| `vbLeadRail_01` | 2 | 459.1, 185.2, 4.8 | 4.7, 14, 12 | none |
| `vbLeadRail_02` | 2 | 462.8, 196.3, 12.3 | 4.7, 14, 12 | -0.768, -0.64, 0.64, -0.768 |
| `vClinicTurf` | 3 | 463.9, 82.4, -0.5 | 58.5, 32, 12.2 | 1, 0.001, -0.001, 1 |
| `vMainTurf` | 3 | 312.9, -72.7, -1.8 | 167.8, 143.6, 60.8 | 0.866, -0.5, 0.5, 0.866 |
| `vEnterTenement` | 0 | 397.2, -33, 6.1 | 10, 8.4, 3.7 | 0.866, -0.5, 0.5, 0.866 |
| `vStartWander` | 0 | 466.4, 7.3, -0.1 | 6, 52, 15 | 0.866, -0.5, 0.5, 0.866 |
| `vStartClinicTalk` | 0 | 470.1, 128.1, -0.3 | 3.9, 2.5, 3.5 | 1, 0.003, -0.003, 1 |
| `vNearClubhouse` | 0 | 460.8, -65.9, -0.6 | 4.9, 2.7, 5 | 0.866, -0.5, 0.5, 0.866 |
| `vJunkyardTurf` | 3 | 357.9, 7.3, -187.4 | 37.7, 49.3, 10 | 1, -0.001, 0.001, 1 |
| `vTenementTurf` | 3 | 408.3, -64.9, 6.1 | 31.8, 35.7, 10 | 0.866, -0.5, 0.5, 0.866 |
| `vClubhouseTurf` | 3 | 465.4, -82.3, -1.1 | 39.5, 28, 10 | 0.866, -0.5, 0.5, 0.866 |
| `vEnterStreet` | 0 | 438, 166.9, -0.2 | 4, 4.9, 8 | -0.005, 1, -1, -0.005 |
| `vJunkyardGate` | 0 | 367.2, 13.4, 7.5 | 1.6, 1.6, 4 | none |
| `vClinicScene` | 0 | 486.7, 110.4, 4.2 | 9.2, 3.9, 7 | none |
| `vStartWander01` | 0 | 415.3, -13.3, -0.1 | 6, 20, 15 | none |
| `vStartPark` | 0 | 468.4, -0.6, -1.1 | 20.2, 8, 15 | 0.866, -0.5, 0.5, 0.866 |
| `vJesseScene` | 0 | 474, 91.8, -0.2 | 4, 4, 5 | none |
| `vStartFenceDialogue` | 0 | 437.8, 136.1, -0.4 | 27.3, 6.5, 5 | none |
| `vBumDoor` | 0 | 463.4, 120.6, -0.3 | 1.5, 2.3, 3.5 | none |
| `vPreloadParty` | 0 | 449, -43.1, 4.4 | 6, 5.4, 8 | 0.864, -0.504, 0.504, 0.864 |
| `vClinicBumTurf` | 3 | 464.8, 120.6, -0.7 | 12.9, 10.2, 5.2 | none |
| `vReachClubhouse` | 0 | 468.4, -46.8, -0.9 | 3.2, 5.4, 8 | 0.5, 0.866, -0.866, 0.5 |
| `vSpotDiceOrphans` | 0 | 458.9, 185, 4.6 | 3.8, 6.9, 9 | none |
| `vJesseTurf1` | 3 | 302.5, -40.8, -1.7 | 170, 110, 83 | 0.866, -0.5, 0.5, 0.866 |
| `vJesseTurf2` | 3 | 469.6, -14.3, -1.7 | 22, 27.4, 17.3 | 0.866, -0.5, 0.5, 0.866 |
| `vbSideRail` | 2 | 436.9, 180.4, 4.7 | 28, 4.8, 3 | none |

### level3

Writer's Block, entry 7,855, 58 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFightTurf_02` | 3 | -74.4, -173.3, -1 | 15, 5.9, 4.4 | none |
| `vFightTurf_01` | 3 | -57.2, -176.1, -1 | 15.1, 8.8, 4.4 | none |
| `vFightTurf_03` | 3 | -59.2, -190.9, -1 | 19.2, 11.5, 4.4 | none |
| `vFixCam_01` | 0 | -129.3, 358.7, 18.3 | 5, 14.5, 7.1 | none |
| `vFixCam_02` | 0 | -64, 357.6, 16 | 6.2, 16.6, 10 | none |
| `vFixCam_11` | 0 | -24.9, 357.9, 13.2 | 17.6, 16, 7.1 | none |
| `vFixCam_9` | 0 | 19.6, 355.5, 13.2 | 4.7, 20.8, 11.9 | none |
| `vFixCam_12` | 0 | 101.6, 313.7, 10.1 | 11.9, 20.6, 11.9 | none |
| `vFixCam_5` | 0 | 108.5, 322.6, 12 | 7.9, 25.9, 5.3 | none |
| `vFixCam_13` | 0 | 109.1, 354, 8.7 | 12.1, 23.8, 8.6 | none |
| `vFixCam_14` | 0 | -158.7, 358.7, 18.3 | 8.1, 14.5, 7.1 | none |
| `vbJumpWarn_1` | 2 | -185.2, 340.9, 24.4 | 5.4, 6, 9.3 | none |
| `vbJumpWarn_03` | 2 | -133.4, 359, 21 | 4.2, 6, 9.3 | none |
| `vbJumpWarn_04` | 2 | -69.4, 359.6, 15.8 | 5.4, 13, 9.3 | none |
| `vbJumpWarn_05` | 2 | 96.2, 357.5, 16.3 | 5.4, 16.8, 4 | none |
| `vbJumpWarn_06` | 2 | 15.4, 363.7, 18.6 | 4.2, 7.1, 4 | none |
| `vbJumpWarn_07` | 2 | 58.3, 332.6, 15.7 | 5.4, 18, 7 | none |
| `vbJumpWarn_08` | 2 | 146.5, 360, 13 | 4.6, 13.6, 7 | none |
| `vbJumpWarn_09` | 2 | 188.5, 359.4, 15.8 | 4.3, 12.9, 7 | none |
| `vbJumpWarn_10` | 2 | -151.9, 344.3, 19.9 | 5.4, 4.4, 5 | none |
| `vbJumpWarn_12` | 2 | -101.9, 332.9, 16 | 5.4, 11, 9.3 | none |
| `vbJumpWarn_13` | 2 | -90.4, 331.9, 16 | 5.4, 11, 9.3 | none |
| `vbJumpWarn_14` | 2 | -37.5, 337.5, 16 | 6, 11, 9.3 | none |
| `vbJumpWarn_02` | 2 | 120.3, 336.6, 8.8 | 5.4, 12.8, 7 | none |
| `vSideRail_01` | 2 | -202.7, 357.3, 18.3 | 49.8, 19, 9.3 | none |
| `vSideRail_02` | 2 | -62, 357.4, 12.1 | 38.5, 18.7, 16.4 | none |
| `vMainRail_05` | 2 | -152.7, 356.9, 18.3 | 23.1, 19.8, 9.3 | none |
| `vMainRail_06` | 2 | -126, 357.4, 18.3 | 18.4, 18.7, 9.3 | none |
| `vMainRail_02` | 2 | 25.7, 357.4, 15.8 | 71.9, 18.7, 9.3 | none |
| `vMainRail_03` | 2 | -8.3, 356.9, 12.9 | 26, 18.7, 16.2 | none |
| `vMainRail_04` | 2 | -256.2, 357.2, 18.3 | 53.4, 19.2, 9.3 | none |
| `vTopRail_02` | 2 | 8.1, 323.7, 13.2 | 9.2, 18.7, 9.3 | none |
| `vTopRail_19` | 0 | 20, 357, -41.5 | 5.5, 18.6, 57.1 | none |
| `vTopRail_11` | 0 | 98.5, 357.1, -47.1 | 14.5, 20.5, 57.1 | none |
| `vTopRail_12` | 0 | 144.8, 324.7, -47.6 | 14.5, 85.3, 57.1 | none |
| `vTopRail_13` | 0 | 186.2, 324.7, -43.8 | 14.5, 85.3, 57.1 | none |
| `vTopRail_14` | 0 | -132.2, 324.7, -42.9 | 14.5, 85.3, 57.1 | none |
| `vTopRail_15` | 0 | -66.4, 355.9, -42.9 | 14.5, 20.5, 57.1 | none |
| `vBigRail_01` | 2 | -107.5, 357.6, 13.2 | 44.1, 18.7, 9.3 | none |
| `vColKill_04` | 0 | 205.1, 385.6, 5.2 | 1.7, 1.2, 6.2 | none |
| `vColKill_02` | 0 | 213.8, 366.9, 8.3 | 2.1, 1.1, 6.2 | none |
| `vColKill_03` | 0 | 215.9, 366.9, 8.3 | 2.1, 1.1, 6.2 | none |
| `vColKill_01` | 0 | 220.7, 388.1, 5.2 | 1, 0.9, 6.2 | none |
| `vbAmbSound_01` | 2 | 200.9, 360.8, 5 | 30.1, 31.1, 10.7 | none |
| `vbAmbSound_02` | 2 | -33, -253, 20.7 | 32, 21, 10.8 | none |
| `vTagCompTurf` | 3 | -85, -197.6, -1 | 44.6, 36, 12.6 | none |
| `vComment` | 0 | -116.7, -169.5, -1 | 14.5, 21, 10.1 | none |
| `vGallery` | 0 | 195.9, 356.5, 2.4 | 32.8, 40.8, 17.6 | none |
| `vDeathGallery` | 0 | 185.7, 350.2, -20.4 | 53.1, 53.4, 17.6 | none |
| `vbEnterGallery` | 2 | 200.8, 355.8, 11.8 | 8, 24.9, 11 | none |
| `vbStartRail` | 2 | -260.5, 357.4, 18.3 | 4.4, 18.7, 9.3 | none |
| `vbSpawnMidCut` | 2 | 26.1, 317.8, 15.2 | 6, 16, 10 | none |
| `vbObjBox` | 2 | 57.8, 315.5, 13.9 | 10, 18, 10 | none |
| `vbWarrRegroup` | 2 | 26.1, 315.2, 13.5 | 45.6, 19.4, 11.3 | none |
| `vbStopRunning` | 0 | 202.8, 360.5, 13.7 | 5.4, 10, 7 | none |
| `vChaseKill` | 0 | -163.4, 324.7, -54.9 | 363.5, 85.3, 63.9 | none |
| `vLeaveCam` | 0 | -72.7, -187.5, -1 | 12.8, 20.9, 8.6 | none |
| `vChattersbox` | 0 | -82.1, -182.7, 4.1 | 2.7, 15.1, 8.6 | none |

### level5

Adios Amigo, entry 8,028, 47 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `volCar_01` | 0 | -115.6, -182, -1 | 30, 20, 10 | none |
| `volCar_02` | 0 | -115.6, -190.8, -1 | 30, 50, 10 | none |
| `volSanRun1_01` | 0 | 30.1, 5.2, -1 | 20, 10, 6 | none |
| `volSanRun1_02` | 0 | 38.6, 41.4, -1 | 10, 4, 6 | none |
| `volSanRun1_03` | 0 | 37.9, 45.3, -1 | 16, 10, 10 | none |
| `volSanRun2_02` | 0 | 120, 55.4, -1 | 10, 30, 6 | none |
| `volSanRun2_03` | 0 | 180.3, 119.4, -1 | 10, 6, 6 | none |
| `volSanRun2_01` | 0 | 133.2, 23.8, -1 | 16, 18, 6 | none |
| `volFightPit_01` | 0 | 179.2, 141.7, -1.3 | 10, 20, 6 | none |
| `volFightPit_02` | 0 | 182.2, 160.4, -2.3 | 8, 10, 6 | none |
| `volFightPit_03` | 0 | 216.7, 153.1, 4.5 | 8, 8, 6 | none |
| `volSanEnd_01` | 0 | 106.1, 15.1, -1 | 6, 10, 6 | none |
| `volFightF1_01` | 0 | 75.1, 19.5, -1 | 10, 6, 6 | none |
| `volSanWarp_01` | 0 | 68.4, 43.2, -1 | 6, 6, 6 | none |
| `volSanWarp_02` | 0 | 128, 45.2, 2.5 | 12, 6, 6 | none |
| `volSanWarp_03` | 0 | 166.3, 119.6, -1 | 6, 6, 6 | none |
| `volSanWarp_04` | 0 | 180, 127.6, -1 | 6, 6, 6 | none |
| `volSanWarp_05` | 0 | 86.2, 170.2, -1 | 6, 6, 6 | none |
| `volSanWarp_08` | 0 | 111.9, 224.2, -0.5 | 12, 6, 6 | none |
| `volSanRun3_03` | 0 | 92.2, 172.1, -1 | 11, 8, 6 | none |
| `volSanRun3_02` | 0 | 122.7, 174.1, -0.9 | 16, 12, 12 | none |
| `volFixCam_01` | 0 | 111.9, 219.7, 3.4 | 12, 4, 13 | none |
| `volSanGo_01` | 0 | 155.8, 140, 4.1 | 16, 12, 6 | 1, 0, 0, 1 |
| `volSanGo_02` | 0 | 138.6, 146.6, -0.9 | 16, 16, 4 | 1, 0, 0, 1 |
| `volBackTimerOn_02` | 0 | 179, 125, -1 | 8, 8, 8 | none |
| `volBackTimerOn_01` | 0 | 58.3, 35.1, -1 | 8, 8, 8 | none |
| `volBackTimerOff_02` | 0 | 179.1, 140.3, -1.3 | 8, 8, 6 | none |
| `volBackTimerOff_01` | 0 | 67, 18.3, -1.3 | 8, 8, 6 | none |
| `vbAmbSound_01` | 2 | 130.8, 99.4, -0.1 | 11.3, 9.7, 3.5 | none |
| `vbAmbSound_07` | 2 | -36.8, -148.2, -200 | 32.8, 30.2, 10 | none |
| `vbAmbSound_03` | 2 | -9.7, -14.7, -0.1 | 10, 8, 3.5 | none |
| `vbAmbSound_04` | 2 | 161.8, 120, -0.1 | 26.9, 20, 3.5 | none |
| `vbAmbSound_05` | 2 | 32.5, 210.3, -0.1 | 36.4, 32.3, 16.5 | none |
| `vbAmbSound_06` | 2 | 25.2, 220.2, -0.1 | 10, 5, 7 | none |
| `vbAmbSound_02` | 2 | -9.8, -9.9, -0.1 | 29.1, 24.6, 3.5 | none |
| `turfBar` | 3 | -9.9, -29.3, -201 | 40, 50, 10 | none |
| `turfHurF1` | 3 | 22.8, -7.2, -1 | 100, 70, 10 | none |
| `turfFight04` | 3 | 153.1, 140.4, -2.4 | 40, 30, 12.1 | none |
| `turfCar01` | 3 | -138.2, -227.2, -1 | 120, 120, 10 | none |
| `volStartHouse` | 0 | 54.2, 190.4, -1 | 16, 12, 6 | none |
| `volSanDoor01` | 0 | 129, 13.3, -1 | 12, 24, 10 | none |
| `volStartSanRun2` | 0 | 104.1, 15, -0.5 | 10, 10, 6 | none |
| `turfCar02` | 3 | -139.5, -133, -1 | 50, 70, 10 | none |
| `volPhoneGuy2` | 0 | -89.3, -132.6, -0.7 | 12, 24, 6 | none |
| `volObjective01` | 0 | -61.3, -82, -1 | 6, 6, 6 | none |
| `vAdvCombat` | 0 | -89.6, -100.4, -1 | 6, 6, 6 | none |
| `volSpawnStreet2` | 0 | -64.6, -180.7, -2.5 | 10, 20, 10 | none |

### level9

All-City, entry 9,228, 34 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbTrainTurf_01` | 3 | -175.8, 198.8, -6.3 | 320, 155.2, 45 | none |
| `vbMainCam_1` | 2 | 79.2, 258.3, -0.5 | 6, 6, 7.5 | 0.4, 0.916, -0.916, 0.4 |
| `vbMainCam_2` | 2 | 56.1, 280.2, -0.5 | 6, 6, 7.5 | 0.847, 0.531, -0.531, 0.847 |
| `vbInTrain_1` | 2 | 76.5, 263.5, 0.6 | 2.7, 15.6, 1.8 | 0.914, -0.405, 0.405, 0.914 |
| `vbInTrain_02` | 2 | 66.1, 279.6, 0.6 | 2.7, 8, 1.8 | 0.538, -0.843, 0.843, 0.538 |
| `vbInTrain_03` | 2 | 61.4, 282.2, 0.6 | 2.7, 8.4, 1.8 | -0.511, 0.86, -0.859, -0.511 |
| `vbFirstMoonTurf_01` | 3 | 94.4, 251.1, -0.5 | 34, 48, 10 | 1, 0.003, -0.003, 1 |
| `vbFirstMoonTurf_02` | 3 | 48.6, 282, -0.5 | 34, 48, 10 | -0.001, 1, -1, -0.001 |
| `vbFirstMoonTurf_03` | 3 | 66, 273, -0.5 | 35, 48, 10 | -0.456, 0.89, -0.89, -0.456 |
| `vbFinalObj_01` | 0 | 65.2, 320.5, 2.1 | 27, 13.8, 6.9 | -0.781, 0.625, -0.625, -0.781 |
| `vbFinalObj_02` | 0 | 37.7, 360.3, 2.1 | 80, 20, 6.9 | -0.02, 1, -1, -0.02 |
| `vbPlatMoonSpawn` | 2 | -101.5, 284.2, -4.8 | 60, 40, 20 | none |
| `vbTriggerBeast` | 2 | 59.1, 285.4, -0.5 | 4.5, 3, 7.5 | 0.851, 0.525, -0.525, 0.851 |
| `vbTriggerArrest` | 2 | 51.1, 270.9, -0.7 | 5, 15, 7.5 | 0.683, -0.73, 0.73, 0.683 |
| `vbFirstMoon` | 2 | 85, 280.7, -1.3 | 16, 16, 6.9 | 1, 0.003, -0.003, 1 |
| `vbTrainShack` | 0 | -39.9, 262.4, 0 | 23.8, 5.5, 2.7 | none |
| `vbCutscene2` | 2 | 53, 306, 1.4 | 14, 11, 7 | 0.591, 0.807, -0.807, 0.591 |
| `vbG7Turf` | 3 | -1.9, 260.4, -2 | 23, 15, 6.7 | none |
| `vbWalkUp` | 2 | 58.9, 309.3, 1.4 | 9.5, 9.5, 7 | 0.591, 0.807, -0.807, 0.591 |
| `vbStopArrest` | 2 | 54.5, 240.9, -0.7 | 5, 26, 7.5 | 0.784, 0.621, -0.621, 0.784 |
| `vbBeastCam` | 2 | 62, 283.7, -0.5 | 6, 3, 7.5 | 0.851, 0.525, -0.525, 0.851 |
| `vbG7Toggle` | 2 | -1.9, 260.4, -2 | 23, 15, 6.7 | none |
| `vbSpawnRoofToss` | 2 | 18.6, 275.5, -0.5 | 2.4, 14, 4 | none |
| `vbArriveObj` | 2 | 62.7, 313.8, 1.4 | 6, 4, 7 | 0.591, 0.807, -0.807, 0.591 |
| `vbTagTutorial` | 2 | 62.6, 299.6, -0.2 | 7.8, 6.3, 3.2 | none |
| `vbRestOfYard` | 2 | 79.7, 263, 0.4 | 1.7, 4, 4 | -0.915, 0.404, -0.404, -0.915 |
| `vbRadioObj` | 2 | 6.2, 271.3, -1.6 | 4.7, 4, 4 | none |
| `vbTriggRoofDrop` | 2 | 29.4, 275.1, -0.7 | 2.4, 6, 4 | 0.01, 1, -1, 0.01 |
| `vbMoon1Turf` | 3 | -188.8, 295.7, -2.3 | 199.7, 60, 14 | none |
| `vbMoon3Turf` | 3 | -113.6, 243, -2.3 | 170, 44, 14 | none |
| `vbCopsOn` | 2 | -11.4, 235.2, -0.1 | 4.4, 90.3, 15 | none |
| `vbFoxOnRoof` | 2 | -134.2, 287.2, 5.9 | 20.5, 14, 7 | none |
| `vbCanRemSpeak` | 2 | 21.2, 260.7, -0.3 | 9.9, 9.9, 5 | none |
| `vbFogAmbient` | 2 | 63.9, 315.6, 0 | 24.2, 96, 10 | none |

### level11

Sharp Dressed Man, entry 7,072, 34 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `turfBopStreet_05` | 3 | -112.9, -74.4, -7 | 60, 70, 25 | none |
| `turfBopStreet_03` | 3 | -14.1, -130.4, -7 | 30, 70, 25 | none |
| `turfBopStreet_02` | 3 | -13.2, -8, -7 | 30, 90, 25 | none |
| `turfBopStreet_04` | 3 | 104.6, -52.7, -7 | 46, 50, 25 | none |
| `turfBopStreet_01` | 3 | 22.4, -5.3, -7 | 90, 40, 25 | none |
| `turfBopStreet_06` | 3 | 44.2, -95.7, -1.3 | 20, 20, 10 | none |
| `turfBopStreet_07` | 3 | -75.6, 4, -7 | 70, 52, 25 | none |
| `volBarBT_01` | 2 | -10.7, 48.6, -200 | 6, 8, 6 | none |
| `volBarBT_02` | 2 | -42.5, -55.7, -1 | 6, 10, 6 | 0, -1, 1, 0 |
| `volBarBT_03` | 2 | 122.3, -74.7, 1.1 | 4, 8, 6 | none |
| `volStriper_03` | 0 | 135.3, -78, 0 | 6, 6, 6 | none |
| `volStriper_02` | 0 | 128.4, -77, 0 | 6, 6, 6 | none |
| `volStriper_01` | 0 | 121.3, -96.3, 0 | 6, 8, 6 | none |
| `volStriper_04` | 0 | 128.4, -83.1, 0 | 6, 6, 6 | none |
| `turfBar2` | 3 | -72.5, -103.1, -7 | 54, 74, 25 | none |
| `volBar3Inside` | 2 | 131.7, -57.1, 0.2 | 4, 4, 6 | 0, 1, -1, 0 |
| `turfBar3` | 3 | 114.5, -103.1, 0.2 | 30, 50, 10 | none |
| `turfTenderB2` | 3 | -53, -51.4, -7 | 30, 22, 25 | none |
| `turfB2Final` | 3 | 12.2, -128.6, -7 | 40, 40.4, 25 | none |
| `turfBar1` | 3 | -32.7, 36.8, -200.7 | 30, 30, 25 | 1, -0.008, 0.008, 1 |
| `turfPit` | 3 | 56.4, -116.2, -7 | 50, 80, 25 | none |
| `volBopIntro` | 0 | 126.5, -12.5, -0.3 | 12, 16, 6 | none |
| `volRoman` | 0 | -3.2, 23.5, -0.9 | 20, 20, 6 | none |
| `volRVandal` | 0 | -32, 37.3, -200.8 | 28.6, 28.7, 20 | none |
| `turfC1Bops` | 3 | 67.2, -45.2, -7 | 90, 74, 25 | none |
| `volPoolHall` | 0 | -72.7, -29.2, -0.9 | 20, 20, 6 | none |
| `turfPoolBar` | 3 | -40.6, -58.1, -1 | 3, 10, 6 | 0, -1, 1, 0 |
| `turfRomanBar02` | 3 | -21.5, 42.5, -200.1 | 18, 12, 5 | none |
| `turfRomanBar03` | 3 | -32, 41.7, -200.1 | 10, 16, 5 | none |
| `turfRomanBar04` | 3 | -30.6, 36.5, -200.1 | 16, 6, 5 | none |
| `volRomanUpper` | 0 | -20.8, 56.7, -197.4 | 13.9, 8.7, 6 | none |
| `volObjStrip` | 2 | 131.8, -54, 0.2 | 4, 4, 6 | 0, 1, -1, 0 |
| `volSeeBoppers` | 0 | 48.1, -5.1, -0.9 | 20, 20, 6 | none |
| `turfPoolBar01` | 3 | -49.1, -55.2, -1 | 22, 30, 6 | 0, -1, 1, 0 |

### level14

Set Up, entry 7,654, 28 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `wStoreCam_Electronic` | 0 | -147.6, 33.8, -1 | 10, 11.5, 4.3 | none |
| `wStoreCam_Jewellry2` | 0 | -147.2, 3.9, -1 | 9.4, 11.1, 4.3 | none |
| `wStoreCam_Jewellry1` | 0 | -117.1, 9.1, -1 | 9.4, 11.1, 4.3 | none |
| `wStoreCam_Liquor` | 0 | -24.2, -22.8, -1 | 9.6, 11.2, 4.3 | none |
| `wStoreCam_Pharmacy` | 0 | 4.1, -7.7, -1 | 9.4, 11.1, 4.3 | none |
| `wStoreCam_Electronic2` | 0 | -153.6, 33.9, -1 | 6.7, 11.5, 4.3 | none |
| `vbAmbSound_01` | 2 | -147.5, 33.9, -0.1 | 9.9, 11.7, 3.5 | none |
| `vbAmbSound_02` | 2 | -147.5, 3.8, -0.1 | 9.9, 11.7, 3.5 | none |
| `vbAmbSound_03` | 2 | -117.4, 8.8, -0.1 | 9.9, 11.7, 3.5 | none |
| `vbAmbSound_04` | 2 | -25.6, 32, -0.1 | 9.9, 11.7, 3.5 | none |
| `vbAmbSound_05` | 2 | -24.3, -22.9, -0.1 | 9.9, 11.7, 3.5 | none |
| `vbAmbSound_06` | 2 | 3.9, -8, -0.1 | 9.9, 11.7, 3.5 | none |
| `vbAmbSound_07` | 2 | -50.2, -33.4, -198 | 60, 60, 20 | none |
| `vbAmbSound_08` | 2 | -72.4, 8.9, 2.4 | 13, 21, 3.5 | none |
| `vbAmbSound_09` | 2 | -74.8, 51.7, -0.6 | 12.2, 20, 7.5 | none |
| `vbAmbSound_10` | 2 | -69.9, 47.6, -0.6 | 7.4, 6, 7.5 | none |
| `vSecurityTurf` | 3 | -158.9, -51.8, -1 | 34.8, 30.8, 17.5 | 1, -0.004, 0.004, 1 |
| `vClubhouse` | 0 | -101.5, 48.5, -4 | 27.7, 27.1, 10.1 | none |
| `vJSBTurf` | 3 | -96, 47.9, -3.5 | 32.3, 27.3, 10.1 | none |
| `vBumTurf` | 3 | -95, 56.7, -3.5 | 20.5, 19.9, 10.1 | none |
| `vJSBTurf01` | 3 | -81.7, 40.8, -0.1 | 20.9, 11.3, 10.1 | none |
| `vEndMission` | 0 | -65.9, 37.9, -3 | 21.8, 11.2, 10.1 | 0.603, -0.798, 0.798, 0.603 |
| `vCourtyard` | 0 | -96.5, 56.8, -3.5 | 20.5, 19.9, 10.1 | none |
| `vTDTurf` | 3 | 9.5, 45, -1 | 32.3, 29.9, 14.7 | none |
| `vPartyTurf` | 3 | -172.7, 39, -1 | 28.8, 26.4, 19.9 | 0.791, -0.612, 0.612, 0.791 |
| `vAptTurf` | 3 | -80.9, 7.6, -4.1 | 24.1, 33, 15.1 | none |
| `vToolTurf02` | 3 | -116.6, 21, -1 | 24.1, 25.3, 10.6 | none |
| `vToolTurf01` | 3 | -80.3, -23.2, -3.9 | 24.1, 25.3, 10.6 | none |

### level20

Scout's Honor, entry 7,709, 45 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vStreetStall_03` | 0 | 352.8, -95.6, 0 | 1.1, 1.1, 3 | none |
| `vStreetStall_02` | 0 | 322, -178, -0.1 | 1.1, 1.1, 3 | 0.737, 0.676, -0.676, 0.737 |
| `vStreetStall_01` | 0 | 289.7, -159.3, -0.1 | 1.1, 1.1, 3 | 0.714, 0.7, -0.7, 0.714 |
| `vMark_01` | 3 | 516.3, 69.4, -0.4 | 14, 12, 5 | 0.673, 0.74, -0.74, 0.673 |
| `vMark_02` | 3 | 523.6, 55.4, -0.4 | 8, 14, 5 | 1, 0, 0, 1 |
| `vMark_03` | 3 | 522.6, 62.1, -0.4 | 9, 14, 5 | 0.995, 0.097, -0.097, 0.995 |
| `vbAmbSound_01` | 2 | 261.2, -127.6, -0.1 | 13, 11.4, 3.5 | none |
| `vbAmbSound_02` | 2 | 358.9, -110.2, -0.1 | 15.1, 11.4, 3.5 | none |
| `vbAmbSound_03` | 2 | 367.3, -39.2, -0.1 | 9.9, 11.4, 3.5 | 0.706, 0.708, -0.708, 0.706 |
| `vbAmbSound_04` | 2 | 407.2, 25.1, -0.1 | 9.9, 11.4, 3.5 | 0.001, 1, -1, 0.001 |
| `vbAmbSound_05` | 2 | 447.8, 46, -0.5 | 9.9, 11.4, 3.5 | 0.001, 1, -1, 0.001 |
| `vbAmbSound_06` | 2 | 287.4, -119.9, 2.2 | 10.2, 23.1, 3.5 | none |
| `vbAmbSound_07` | 2 | 304.5, -94.5, 2.2 | 12.3, 10.3, 3.5 | none |
| `vbAmbSound_08` | 2 | 306.5, -132.1, 2.2 | 19.4, 10.2, 3.5 | none |
| `vHunsApproach` | 0 | 347.5, -128.8, -0.1 | 11, 9.3, 4 | -1, 0.001, -0.001, -1 |
| `vRestaurant` | 0 | 348.8, -170.2, 0.1 | 13.1, 11.3, 6.1 | 0.703, 0.711, -0.711, 0.703 |
| `vRestaurantTurf` | 3 | 348.8, -170.2, 0.1 | 13.1, 11.3, 6.1 | 0.703, 0.711, -0.711, 0.703 |
| `vTakeoutStore` | 0 | 340.5, -168.3, 0 | 26, 11.3, 5.8 | 0.703, 0.711, -0.711, 0.703 |
| `vPharmacy` | 0 | 298.5, -35.8, 0 | 11.3, 9.8, 6 | 0.709, -0.706, 0.706, 0.709 |
| `vSection1` | 0 | 320.8, 44.2, -199.8 | 3.3, 3.8, 3 | none |
| `vSection1Cleanup` | 0 | 312.3, 30.2, -199.8 | 3, 5.1, 3 | none |
| `vbVandalsExit` | 0 | 329.2, -2.9, -200.2 | 7, 3, 4 | none |
| `vChaseHuns` | 3 | 490, 49.1, -3.4 | 54, 38, 15 | 0.736, -0.677, 0.677, 0.736 |
| `vMVandal` | 0 | 283.8, -23.1, -202.7 | 72, 80, 15 | none |
| `vUnsuspendStore` | 0 | 272.1, -82.7, -1.7 | 12, 6, 8 | none |
| `vChaseGate` | 0 | 285.5, -155.5, 0 | 4.2, 2.4, 3 | 0.705, -0.709, 0.709, 0.705 |
| `vPlaySubwaySound` | 0 | 351.5, -96.6, -6.9 | 13.2, 26, 6 | none |
| `vStreetPeds` | 0 | 354.3, -81.9, -8.2 | 5.7, 5.2, 7 | none |
| `vStall1` | 0 | 351.4, -95.9, 0.1 | 3.6, 3.6, 6 | none |
| `vStall2` | 0 | 321.5, -180, 0 | 3.6, 3.6, 6 | 0.641, -0.768, 0.768, 0.641 |
| `vStall3` | 0 | 287.8, -161.3, -0.3 | 3.6, 3.6, 6 | 0.691, 0.723, -0.723, 0.691 |
| `vWLTag` | 0 | 357.7, -170.1, -1 | 2, 1.5, 6 | 0.713, -0.701, 0.701, 0.713 |
| `vRunBackup` | 0 | 482.3, 14.7, -0.2 | 7.5, 3.8, 3 | none |
| `vAttGhostBackup` | 0 | 493.4, 49.4, 2.4 | 6, 8, 3.5 | -0.027, -1, 1, -0.027 |
| `vGetOutOfBackAlley` | 0 | 531.5, 55.3, -1 | 4, 8, 6 | 1, 0.003, -0.003, 1 |
| `vUpperCam02` | 0 | 500.8, 69.6, 2.9 | 5, 10, 3.5 | 0.916, -0.401, 0.401, 0.916 |
| `vUpperCam01` | 0 | 501.4, 56.4, 4 | 5, 5.9, 3.5 | -0.198, -0.98, 0.98, -0.198 |
| `vMainCam01` | 0 | 515.5, 71.1, 0 | 10, 10, 6 | -0.648, -0.762, 0.762, -0.648 |
| `vMainCam02` | 0 | 493.5, 47.9, 2.8 | 6, 6, 6 | 0.012, -1, 1, 0.012 |
| `vUpperCam03` | 0 | 507.9, 72.2, 2.9 | 5, 10, 3.5 | -0.419, -0.908, 0.908, -0.419 |
| `vUpperCam04` | 0 | 511.6, 58.1, 4 | 5, 10, 3.5 | -0.404, -0.915, 0.915, -0.404 |
| `vRailCam02` | 0 | 508.5, 66.3, -1.8 | 8, 8, 6 | -0.447, -0.895, 0.895, -0.447 |
| `vRailCam03` | 0 | 503.6, 63.9, -0.3 | 7, 6, 6 | -0.391, -0.92, 0.92, -0.391 |
| `vRailCam01` | 0 | 490.8, 57.1, -0.3 | 8, 18, 6 | -0.067, -0.998, 0.998, -0.067 |
| `vFinalScene` | 0 | 580.2, 65.5, -5.4 | 4.2, 4.1, 6 | 0.707, 0.707, -0.707, 0.707 |

### level31

Boys In Blue, entry 7,859, 32 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vTopsideCopTurf` | 3 | 49.4, 49.4, 4.9 | 128.1, 30.3, 20 | none |
| `vTopsideGangTurf` | 3 | 89.1, -7.5, 4.9 | 93.8, 94, 20 | none |
| `vGamblingClub` | 0 | 146.8, 85.4, 5.8 | 2.9, 1.7, 3 | none |
| `vBackalleyTurf` | 3 | 89, 85.8, 4.8 | 76.4, 13, 20 | none |
| `vBumGate` | 0 | 73.3, 155.6, -1.1 | 1.4, 1.6, 4 | none |
| `vSewerTurf` | 3 | 5.2, 145.4, -2.1 | 67.9, 69.3, 25 | none |
| `vNearDevil` | 0 | -62.5, 130.8, 5 | 10.5, 4, 5 | none |
| `vDevilWindow` | 0 | -58.4, 81.4, 5.1 | 30, 16, 4 | 0, 1, -1, 0 |
| `vBreakHiders2` | 0 | 153.7, 12.7, 5.6 | 3.1, 5.5, 4 | none |
| `vBreakHiders1` | 0 | 125.4, 35.6, 5.8 | 4.1, 4.7, 4 | none |
| `vStreetAmbient` | 0 | 69.3, 75.8, 4.1 | 3.8, 3.3, 5 | none |
| `vCopSetup1` | 3 | 111.1, 74.5, 5.8 | 8.9, 10.8, 7.2 | none |
| `vCopSetup2` | 3 | 63.2, 55.3, 5.8 | 8.9, 10.8, 7.2 | none |
| `vBreakDoor` | 0 | 5.6, 171.7, 7.9 | 1.4, 2.4, 4 | none |
| `vEnterBumFight` | 0 | 5.6, 168.5, 8 | 13.1, 16.1, 13.1 | none |
| `vApproachRenegades` | 0 | 29.8, 196, 2 | 4.8, 2.5, 4 | none |
| `vSpotDowned` | 0 | -56.3, 83.8, 8.8 | 4.3, 10.9, 5.9 | none |
| `vEnterBandArea` | 0 | -30.3, 102.9, 3.5 | 9, 4.4, 5 | none |
| `vPedsLeave` | 0 | -41.2, 107.1, 3.5 | 10, 4.4, 5 | none |
| `tbBandArea` | 3 | -16.3, 94.9, 4 | 3.7, 12.8, 6 | none |
| `tbDanceFloor` | 3 | -30.9, 94.8, 3.5 | 14.4, 12.6, 7 | none |
| `tbBarArea` | 3 | -44.5, 95.2, 4.2 | 13.2, 22, 7 | none |
| `tbRedDevil` | 3 | -44.5, 95.2, 3.9 | 32, 22, 6 | none |
| `vPoolConfront` | 0 | -31.6, 110.1, 3.5 | 10, 4.4, 5 | 0, 1, -1, 0 |
| `vBumBarf` | 0 | -55.2, 62.4, 3.5 | 3, 7.6, 8 | none |
| `vFindBirdie` | 0 | -52.9, 4.3, 9.2 | 4, 8, 8 | none |
| `tbACTurf` | 3 | -73, 30.2, 3.3 | 56, 40, 10 | none |
| `vRefineryEntrance` | 0 | -63.4, 29.9, 4.2 | 4, 4, 6 | none |
| `tbBackroom` | 3 | -47.6, -16.4, 9.3 | 11, 7.4, 5 | none |
| `vBarTurf` | 3 | -44.8, 96.8, 5.2 | 5.6, 6.8, 4 | none |
| `vCatWalk` | 0 | -53.7, -6.3, 9.2 | 5, 10.1, 2 | none |
| `vOnStage` | 0 | -16.5, 94.9, 4 | 3.7, 12.8, 6 | none |

### level34

Blackout, entry 7,919, 26 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vLevelVandal` | 0 | -142.9, -300.1, -0.8 | 138.9, 279.9, 30 | none |
| `vIntro` | 0 | -44.4, -77.5, -0.3 | 3.3, 6.4, 3.8 | none |
| `vTier2Turf` | 3 | -127.7, -224.4, -0.8 | 59.2, 107.3, 53.7 | none |
| `vSection2` | 0 | -131.2, -224.1, -0.8 | 76.7, 107.3, 53.7 | none |
| `vNearPanzerStore` | 0 | -99.4, -218.4, -0.7 | 9.9, 11.5, 6 | none |
| `vCarPoizo` | 0 | -75.3, -146.6, -0.4 | 2.2, 6.1, 3.8 | none |
| `vNearHunStore` | 0 | -99.2, -180.4, -0.7 | 24.8, 16, 6 | 1, 0, 0, 1 |
| `vTier3Turf` | 3 | -111.5, -279.5, -0.8 | 96.5, 55.2, 53.7 | none |
| `vSection3` | 0 | -111.5, -279.4, -1.9 | 96.5, 55.2, 53.7 | none |
| `vSection1` | 0 | -74.8, -154.2, -0.9 | 66.1, 102.9, 53.7 | none |
| `vNearFuries` | 0 | -175.7, -266.5, -9.5 | 5.1, 4, 9 | -0.003, -1, 1, -0.003 |
| `vFuryTurf` | 3 | -233.3, -258.1, -12.5 | 53.5, 55.1, 21.2 | 0.7, -0.714, 0.714, 0.7 |
| `vSubwayExit` | 0 | -256.6, -203.2, -12.7 | 2, 3.8, 3.5 | 0.696, -0.718, 0.718, 0.696 |
| `vNearHiFi` | 0 | -37.9, -251.8, -0.8 | 22.3, 20.6, 8 | none |
| `vEnterRiot` | 0 | -22.1, -72, -0.1 | 2, 2.2, 2.5 | none |
| `vHiFiTurf` | 3 | -77.5, -279.2, -0.4 | 15.8, 16.7, 9.5 | none |
| `vNearMolotovCar` | 0 | -89.2, -190.3, -0.8 | 14.8, 7.5, 4.5 | none |
| `vLoadTagger` | 0 | -26.7, -122.1, -0.8 | 5.6, 2.7, 4 | none |
| `vStartIntroLooters` | 0 | -8.9, -72.8, -6.2 | 3, 4, 4 | none |
| `vRiotCleanup` | 0 | -142.8, -271, -0.3 | 6.3, 8.4, 6 | none |
| `vRiotTurf` | 3 | -143, -299.8, -0.8 | 150.4, 300.1, 30 | none |
| `vPharmacyTurf` | 3 | -99.4, -218.4, -0.7 | 9.9, 11.5, 6 | none |
| `vFuryConfront` | 0 | -234.8, -273.4, -12.4 | 26.3, 8.2, 5.9 | 0.738, -0.675, 0.675, 0.738 |
| `vEnterDiamond` | 0 | -224.1, -266.8, -12.4 | 20, 46, 10 | 0.709, -0.705, 0.705, 0.709 |
| `vWBreakTutorial` | 0 | -45.1, -113.4, -0.2 | 2.9, 6.1, 2.7 | none |
| `vSection3Gate` | 0 | -109.3, -224, -0.3 | 3.6, 1.8, 5 | none |

### level51

Desperate Dudes, entry 8,030, 81 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `volMeetWar_01` | 0 | -85.1, 499.4, -1.7 | 40, 30, 10 | 0, -1, 1, 0 |
| `volMeetWar_02` | 0 | -93.7, 503.1, -1.7 | 70, 36, 10 | 0, -1, 1, 0 |
| `vPitRange_04` | 0 | 202.3, 245.5, -3.4 | 20, 30, 10 | none |
| `vPitRange_02` | 0 | 169.6, 263.6, -3.4 | 16, 40, 10 | none |
| `vPitRange_03` | 0 | 193.6, 257.6, -3.4 | 24, 46, 10 | none |
| `vPitRange_01` | 0 | 167.6, 251, -3.4 | 26, 30, 10 | none |
| `vPitRange_05` | 0 | 178.3, 277.1, -3.4 | 16, 40, 10 | none |
| `vPitRange_06` | 0 | 150, 303.7, -3.4 | 40, 24, 10 | none |
| `vPitRange_07` | 0 | 170, 282.2, -3.4 | 16, 20, 10 | none |
| `vCHRange_01` | 0 | 80.2, 304.1, -3.4 | 20, 30, 10 | none |
| `vCHRange_02` | 0 | 89.6, 273.9, -3.4 | 32, 30, 10 | none |
| `vCHRange_03` | 0 | 69.3, 284.6, -3.4 | 20, 30, 10 | none |
| `vCHRange_04` | 0 | 70.4, 271.5, -3.4 | 30, 26, 10 | none |
| `vCHRange_05` | 0 | 65.9, 318.4, -3.4 | 24, 14, 10 | none |
| `vCHRange_07` | 0 | 65.7, 301.9, -3.4 | 20, 30, 10 | none |
| `volAYRange_01` | 0 | -108.2, 426.4, -1 | 44, 24, 6 | none |
| `volAYRange_02` | 0 | -107.9, 447, -1 | 40, 20, 6 | none |
| `volAYRange_03` | 0 | -77.9, 427.8, -1 | 30, 20, 6 | none |
| `volAYRange_04` | 0 | -109.4, 462.4, -1 | 30, 24, 6 | none |
| `volAYRange_05` | 0 | -54.7, 427.6, -1 | 34, 20, 6 | none |
| `volAYRange_06` | 0 | -105, 428, -1 | 20, 30, 6 | none |
| `vLook_01` | 0 | -10.8, 432.2, -1.2 | 12, 12, 6 | none |
| `vLook_02` | 0 | -52.8, 423, -1.2 | 24, 12, 6 | none |
| `Trig_09` | 2 | -421.8, 573.5, 15.9 | 2, 6, 10 | none |
| `Trig_01` | 2 | -270.9, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_03` | 2 | -297.4, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_05` | 2 | -311.4, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_07` | 2 | -329.9, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_10` | 2 | -441.4, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_12` | 2 | -455, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_14` | 2 | -472.7, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_16` | 2 | -487.8, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_18` | 2 | -501.8, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_20` | 2 | -516.1, 573.5, 15.9 | 6.4, 6, 10 | none |
| `Trig_22` | 2 | -529.9, 574.1, 15.9 | 4.4, 6, 10 | none |
| `Chase2Trig_01` | 0 | -617, 515.1, 10.2 | 40, 8, 20 | none |
| `Chase2Trig_02` | 0 | -617, 483, 10.2 | 40, 8, 20 | none |
| `Chase2Trig_03` | 0 | -617, 450.3, 10.2 | 40, 8, 20 | none |
| `Chase2Trig_04` | 0 | -617, 417.8, 10.2 | 40, 8, 20 | none |
| `Chase2Trig_05` | 0 | -617, 384.7, 10.2 | 40, 8, 20 | none |
| `Chase2Trig_06` | 0 | -617, 352, 10.2 | 40, 8, 20 | none |
| `Chase2Trig_10` | 0 | -617, 567, 10.2 | 40, 8, 20 | none |
| `Chase2Trig_07` | 0 | -617, 331.9, 10.2 | 40, 8, 20 | none |
| `trufTBST` | 3 | -132, 413.7, -5.1 | 160, 150, 25 | none |
| `volTBCh02` | 0 | 105.1, 250.8, -2 | 16, 16, 20 | none |
| `volTBStart01` | 0 | 136, 234.2, -3.6 | 6, 12, 10 | none |
| `turfOpenTB` | 3 | 32.7, 163, -1 | 70, 110, 10 | none |
| `volFinal` | 0 | -605.9, 315.5, 12.6 | 16, 16, 10 | none |
| `volMoveTB` | 0 | 194.9, 221.5, -1 | 8, 8, 6 | none |
| `volKillSeeks` | 0 | 171.1, 245.3, -2.9 | 50, 12, 6 | none |
| `turfC01` | 3 | 145.4, 216.9, -5 | 80, 110, 25 | none |
| `volPoizoFire` | 0 | 175.4, 233.8, -1 | 14, 8, 6 | 0.94, 0.342, -0.342, 0.94 |
| `volBusE` | 0 | -86.9, 464.7, 2.4 | 10, 10, 6 | none |
| `volBusC` | 0 | -12.9, 427.1, -0.8 | 24, 30, 6 | none |
| `volBusF` | 0 | -120.5, 422.1, -0.7 | 60, 20, 6 | none |
| `turfC` | 3 | 121.6, 290.8, -5 | 20, 20, 25 | none |
| `TurfFinal` | 3 | -83.4, 514, -0.9 | 40, 40, 6 | none |
| `volLoadStreetC2` | 0 | 167.2, 318.9, -0.9 | 6, 6, 6 | none |
| `volC2SeeObj` | 0 | 168.8, 296.5, -0.5 | 10, 5.7, 6 | none |
| `volStreet01` | 0 | 128.8, 293.7, 0 | 14, 14, 6 | none |
| `volHideObjC2` | 0 | 167.6, 321.1, -0.7 | 2, 2, 6 | none |
| `volBusD` | 0 | -56.7, 427.1, -0.8 | 24, 24, 6 | none |
| `volSeeMeetWar` | 0 | -79.3, 486.3, -1.7 | 6, 6, 10 | 0, -1, 1, 0 |
| `volCH3Go` | 0 | 112, 315.4, -0.5 | 4, 4, 6 | none |
| `volCh6Go` | 0 | -181.2, 570, 15.3 | 4, 4, 6 | 0, -1, 1, 0 |
| `turfCH` | 3 | 11.3, 272.2, -5 | 110, 90, 20 | none |
| `volClubHouse` | 0 | 64.6, 332.5, 5.8 | 23, 20, 10 | none |
| `volCH4Guys` | 0 | 86.3, 329.1, 2.6 | 4, 8, 6 | none |
| `volStartCHRail` | 0 | 65.3, 329.1, 5.7 | 22, 3.3, 3.5 | none |
| `volCHKill` | 0 | 78.7, 336, -0.4 | 8, 8, 4 | none |
| `volBusEnd` | 0 | -538, 572.7, 13.5 | 8, 8, 6 | none |
| `volWarpToEnd` | 0 | -250.1, 572.5, 15.6 | 8, 8, 6 | none |
| `volCH4Go` | 0 | 29.3, 335.9, 5.8 | 10, 20, 6 | none |
| `turfTBCH06` | 3 | 9.2, 320.7, -1.3 | 30, 42.3, 20 | none |
| `volCH4Go01` | 0 | 36.3, 335.3, 5.8 | 12, 22, 6 | none |
| `volVerminCheck` | 0 | 9.3, 319.3, -1.3 | 30, 45.1, 16 | none |
| `volTBCh022` | 0 | 88.6, 242.4, -2 | 16, 16, 20 | none |
| `trufTBSTRspond01` | 3 | -119.4, 436.2, -5.1 | 50, 50, 25 | none |
| `trufTBSTRspond02` | 3 | -114.9, 417.5, -5.1 | 130, 30, 25 | none |
| `trufTBSTRspond03` | 3 | -14.9, 443.4, -5.1 | 30, 50, 25 | none |
| `volCH3Go01` | 0 | 100, 303.8, 4.3 | 12.4, 13.1, 6 | none |

### level52

No Permits, No Parley, entry 8,190, 35 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbAmbSound_01` | 2 | 481.6, -113.2, -0.1 | 9.8, 11.3, 3.5 | none |
| `vPark` | 0 | 464, -91.1, -0.3 | 17.2, 31, 6.5 | none |
| `vTurfChapter2` | 3 | 405.1, -257.4, -1 | 140, 165.6, 25 | none |
| `vAjaxTrigger` | 0 | 414.8, -131.5, -0.3 | 36, 41, 20 | none |
| `vHomeFree` | 0 | 440.5, -69, -0.3 | 23.9, 20, 3.5 | none |
| `vRembrandtTrigger` | 0 | 407.1, -175.2, -0.1 | 5.4, 5.4, 3.5 | none |
| `vCowboyTrigger` | 0 | 492.3, -151.3, -0.1 | 8, 8, 3.5 | none |
| `vFoxTrigger` | 0 | 444.5, -183.2, -0.1 | 4, 4, 3.5 | none |
| `vAlleyMothersTrigger` | 0 | 488.5, -130, -0.1 | 4, 36, 3.5 | none |
| `vHideEliminators2Trigger1` | 0 | 484, -194.6, -0.1 | 18.6, 26, 3.5 | none |
| `vHideEliminators1Trigger1` | 0 | 414.8, -131.5, -0.3 | 36, 41, 20 | none |
| `vHideEliminators1Trigger2` | 0 | 447.3, -108.5, -0.1 | 16, 7.4, 2.5 | 0, -1, 1, 0 |
| `vHideEliminators2Trigger2` | 0 | 499.7, -167.2, -0.1 | 8, 8, 3.5 | 1, 0, 0, 1 |
| `vCochiseTrigger` | 0 | 463.9, -219.3, -0.1 | 10, 10, 3.5 | none |
| `vVerminTrigger` | 0 | 477.3, -136.6, -0.2 | 4, 4, 3.5 | none |
| `vRembrandtTalkTrigger` | 0 | 406.9, -175.2, -0.1 | 10, 10, 3.5 | none |
| `vHideEliminators1Trigger3` | 0 | 451.6, -103, -0.1 | 6.3, 6, 2.5 | 0, -1, 1, 0 |
| `vHideEliminators2Trigger3` | 0 | 502.7, -167.2, -0.1 | 5, 5, 3.5 | none |
| `vTurfChapter2a` | 3 | 431, -94.2, -1 | 33, 8, 25 | none |
| `vPenTrigger` | 0 | 516.7, -217.3, -0.1 | 5.5, 13.8, 3.5 | none |
| `vRunnerEliminators2Trigger` | 0 | 414.7, -125.2, -0.2 | 66.9, 5.3, 3.5 | none |
| `vRunnerMothers2Trigger` | 0 | 456.7, -137.5, -0.2 | 57.5, 5.3, 3.5 | none |
| `vRunnerMothers1Trigger` | 0 | 406.9, -179.1, -0.2 | 52.6, 8, 3.5 | none |
| `vRunnerEliminators1Trigger` | 0 | 439.8, -207.3, -0.2 | 44.3, 5, 12.5 | none |
| `vOrphanGateTrigger` | 0 | 504.1, -214.3, -0.1 | 1.6, 1.6, 3.5 | none |
| `vParkExit` | 0 | 467.2, -63.3, 0.9 | 1.6, 1.6, 3.5 | none |
| `vPharmacyStore` | 0 | 481.5, -113.4, -0.1 | 10, 11.5, 3.5 | none |
| `vLiquorStore` | 0 | 449.6, -186.8, -0.1 | 9.8, 11.5, 3.5 | none |
| `vLeavePark` | 0 | 467.2, -63.3, 0.9 | 1.6, 1.6, 3.5 | none |
| `vTurfParkGang` | 3 | 440.2, -91.1, -1 | 41.2, 42, 10 | none |
| `vTurfChapter3` | 3 | 487, 139.7, -0.1 | 10, 10, 3.5 | none |
| `vOrphanTrigger` | 0 | 487.2, 129.1, -0.1 | 8, 4, 3.5 | 1, -0.001, 0.001, 1 |
| `vHideMothersTrigger2` | 0 | 442.2, -185.7, -0.1 | 4, 4, 3.5 | none |
| `vHideMothersTrigger1` | 0 | 442.2, -185.8, -0.1 | 7.4, 8, 3.5 | none |
| `vParkMusic` | 0 | 464.1, -91.3, -0.3 | 17.2, 31.4, 6.5 | none |

### level54

Home Run, entry 8,261, 77 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbStreetTurf_1` | 3 | -623.4, -104.7, -12.4 | 600, 182.5, 50 | none |
| `vbStreetTurf_2` | 3 | -652, -223.7, -12.4 | 30, 300, 50 | none |
| `camswitch_01` | 2 | -85, -82.5, 0 | 6, 30, 15 | none |
| `camswitch_02` | 2 | -269.2, -80.7, 0 | 6, 30, 15 | none |
| `camswitch_03` | 2 | -284.9, -48.7, -1.1 | 6, 20, 15 | none |
| `camswitch_04` | 2 | -438.6, -38.6, -1.1 | 6, 4, 15 | none |
| `camswitch_05` | 2 | -449.3, -42.1, -1.1 | 6, 12, 15 | none |
| `camswitch_06` | 2 | -488.7, -46.1, -1.6 | 8, 16, 20 | none |
| `camswitch_07` | 2 | -588.6, -29.9, -1.2 | 8, 6, 15 | none |
| `camswitch_08` | 2 | -591.3, 48.7, -1.2 | 6, 20, 15 | none |
| `camswitch_10` | 2 | -643.5, 8.9, -1.2 | 4, 20, 15 | 0, -1, 1, 0 |
| `camswitch_12` | 2 | -645, -176.3, -1.2 | 6, 24, 15 | 0, -1, 1, 0 |
| `camswitch_11` | 2 | -652.5, -18.8, -1.2 | 16, 10, 15 | 0, -1, 1, 0 |
| `camswitch_14` | 2 | -650.1, -218.7, -1.2 | 6, 10, 15 | 0, -1, 1, 0 |
| `camswitch_13` | 2 | -653.5, -189.2, -1.2 | 6, 24, 15 | 0, -1, 1, 0 |
| `camswitch_09` | 2 | -645.6, 51.4, -1.2 | 10, 4, 15 | 0, -1, 1, 0 |
| `vbLook_01` | 2 | -334.2, -39.2, -0.6 | 4, 6, 4.3 | none |
| `vbLook_02` | 2 | -406.9, -38.9, -0.6 | 4, 6, 4.3 | none |
| `vbLook_03` | 2 | -585.9, -9, -0.6 | 4, 6, 4.3 | -0.001, -1, 1, -0.001 |
| `vbLook_04` | 2 | -648.4, -60.5, -0.6 | 4, 6, 4.3 | 0, -1, 1, 0 |
| `vbLook_05` | 2 | -648.6, -114.1, -0.6 | 4, 6, 4.3 | 0, -1, 1, 0 |
| `vbLook_06` | 2 | -648.6, -152.9, -0.6 | 4, 6, 4.3 | 0, -1, 1, 0 |
| `vbLeadOut_1` | 2 | -339, -42.2, -0.7 | 7.6, 10.1, 5 | none |
| `vbLeadOut_02` | 2 | -387.8, -42.2, -0.7 | 7.6, 10.1, 5 | none |
| `vbLeadOut_03` | 2 | -417.8, -42.2, -0.7 | 7.6, 10.1, 5 | none |
| `vbLeadIn_1` | 2 | -355.1, -42.2, -0.7 | 7.6, 10.1, 5 | none |
| `vbLeadIn_02` | 2 | -403.9, -42.2, -0.7 | 7.6, 10.1, 5 | none |
| `vbLeadIn_03` | 2 | -433.9, -42.2, -0.7 | 7.6, 10.1, 5 | none |
| `vbHint_01` | 2 | -344.8, -39.8, -0.4 | 5.4, 5.9, 2.9 | none |
| `vbHint_02` | 2 | -393.7, -39.8, -0.4 | 5.4, 5.9, 2.9 | none |
| `vbHint_03` | 2 | -430.1, -39.8, -0.4 | 5.4, 5.9, 2.9 | none |
| `vbHint_9` | 2 | -649.1, -80.8, -0.4 | 5.4, 5.9, 2.9 | 0, -1, 1, 0 |
| `vbHint_10` | 2 | -649.8, -129.3, -0.4 | 5.4, 5.9, 2.9 | 0, -1, 1, 0 |
| `vbHint_11` | 2 | -649.8, -159.4, -0.4 | 5.4, 5.9, 2.9 | 0, -1, 1, 0 |
| `vbHint_4` | 2 | -476.4, -38.8, 5.6 | 6, 3, 2.9 | 0, -1, 1, 0 |
| `vbHint_5` | 2 | -502.9, -32.5, 3.9 | 6, 3, 2.9 | 0, -1, 1, 0 |
| `vbHint_6` | 2 | -519.7, -32.5, 4 | 6, 3, 2.9 | 0, -1, 1, 0 |
| `vbHint_7` | 2 | -545.3, -32.5, 7.5 | 6, 3, 2.9 | 0, -1, 1, 0 |
| `vbHint_8` | 2 | -560.8, -25.9, 7.5 | 6, 3, 2.9 | 0, -1, 1, 0 |
| `vbUberHack_02` | 0 | -247.7, -74.5, 1.8 | 7.5, 20.2, 10.1 | none |
| `vbUberHack_03` | 0 | -329.7, -47, -1.4 | 7.5, 20.2, 10.1 | none |
| `vbUberHack_04` | 0 | -398.4, -47, -1.2 | 7.5, 20.2, 10.1 | none |
| `vbUberHack_05` | 0 | -498.3, -47, -0.8 | 7.5, 20.2, 15 | none |
| `vbUberHack_06` | 0 | -578.3, -43.9, 3.8 | 7.5, 20.2, 15 | none |
| `vbTransition_01` | 2 | -571, -33.4, 0.2 | 4, 8, 15 | 0.002, -1, 1, 0.002 |
| `vbTransition_02` | 2 | -565.9, -36.7, 0.2 | 4, 8, 15 | 1, 0.007, -0.007, 1 |
| `vbparkTurf` | 3 | -1114.5, -372.7, -38.4 | 260, 460, 70 | none |
| `pitTurf1` | 3 | -491.1, -39.6, -0.5 | 32, 5, 10 | none |
| `pitTurf2` | 3 | -513.2, -40.1, -0.5 | 24, 12, 10 | none |
| `pitTurf3` | 3 | -552.6, -40.1, -0.5 | 40, 12, 15 | none |
| `vbKill` | 0 | -644.8, -246.6, -12.4 | 10, 10, 50 | none |
| `vbStreetPursuitTurf` | 3 | -908.5, -313.4, -28.6 | 300, 60, 70 | none |
| `vbReverse` | 2 | -276.1, -50.3, -1.1 | 6, 10, 15 | none |
| `vbBreakBridge` | 2 | -593, 55.8, -1.2 | 8.8, 8.8, 15 | none |
| `vbCheckpoint1` | 2 | -591.6, 18.3, -0.9 | 7.3, 11.9, 6 | 0, -1, 1, 0 |
| `vbSuspendAlt` | 0 | -278.2, -57.6, -2.2 | 10, 4, 23.9 | none |
| `vbGoGoGo` | 0 | -308, -48.1, -1.1 | 6, 20, 15 | none |
| `vbBridgeBar` | 2 | -593, 47.7, -1.2 | 8.8, 8.8, 15 | none |
| `vbFuryGate` | 2 | -630.3, 57.4, 1.8 | 4, 10, 5 | none |
| `vbStopPath2` | 0 | -576.9, -41.6, 4.5 | 2.9, 12, 5.9 | none |
| `vbDestructor` | 2 | -590.4, 52.9, 2.5 | 5, 12.4, 7.7 | none |
| `vbFlusher` | 0 | -651.7, -213.8, 4.7 | 9.9, 9.9, 4.9 | none |
| `vbSwitchSideLead` | 0 | -537.5, -34.4, 7.1 | 2.8, 7.7, 3.8 | none |
| `vbTriggerPopsicle` | 2 | -1023.5, -65.5, -17.3 | 15, 6.2, 3.1 | 1, 0.015, -0.015, 1 |
| `vbTriggerSplit` | 2 | -844.9, -275.3, -8.3 | 20, 3.7, 10 | -0.001, 1, -1, -0.001 |
| `vbOutToStreet` | 2 | -662.5, -123.7, -0.7 | 13.5, 11.7, 5.8 | none |
| `vbSlowDown` | 0 | -652.6, -115.1, -0.7 | 13.5, 11.7, 5.8 | none |
| `vbRoofTurf` | 3 | -976.5, -86.5, -7.7 | 60, 60, 10 | none |
| `vbStartAjaxScrap` | 2 | -1013.5, -118.1, -18.8 | 29, 16.2, 8.1 | 0.4, -0.916, 0.916, 0.4 |
| `vbStragglerArrives` | 0 | -1020.7, -60.3, -17.3 | 18, 6.2, 3.1 | 1, 0.015, -0.015, 1 |
| `vbChangePath` | 0 | -570.6, -35.6, 4.5 | 2.9, 6, 5.9 | none |
| `vbSlowFuries` | 0 | -248.2, -80.7, 0 | 6, 30, 15 | none |
| `vbTightenPlatformer` | 2 | -563.5, -37.3, -1.6 | 8, 16, 20 | none |
| `vbStartFury` | 2 | -62.7, -52.8, 0 | 6, 30, 15 | 0.585, -0.811, 0.811, 0.585 |
| `vbCutToFury` | 0 | -97.7, -82.8, 0 | 6, 30, 15 | none |
| `vbCutBack` | 0 | -149, -82.8, 0 | 6, 30, 15 | none |
| `vbCheckForFight` | 2 | -1023.4, -69.6, -17.3 | 15, 6.2, 3.1 | 1, 0.015, -0.015, 1 |

### level55

Friendly Faces, entry 8,356, 15 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vStall_01` | 0 | 378.6, 19.2, -197.3 | 4.3, 2, 5 | none |
| `vStall_02` | 0 | 378.6, 16.8, -197.3 | 4.3, 2, 5 | none |
| `vStall_03` | 0 | 378.6, 14.4, -197.3 | 4.3, 2, 5 | none |
| `vStall_04` | 0 | 378.6, 12, -197.3 | 4.3, 2, 5 | none |
| `vCam_01` | 0 | 367.7, 24, 3.9 | 18.9, 8.1, 4.5 | none |
| `vCam_02` | 0 | 360.2, 8.6, 3.9 | 7.2, 8.1, 4.5 | none |
| `vLizCam_WideDeep` | 0 | 374.1, 2.1, 3.5 | 6, 9.5, 1.6 | none |
| `vEndLizScene` | 0 | 374.3, -7.1, 1.7 | 5.9, 12.6, 4.6 | -0.009, -1, 1, -0.009 |
| `vRailCam` | 0 | 376.5, -5, -197.3 | 10.4, 35.6, 11.7 | none |
| `vPunkTurf` | 3 | 361.6, -3.4, -197.3 | 21.4, 33.7, 11.7 | none |
| `vBathroom` | 0 | 361.6, -16.6, -197.3 | 21.4, 33.7, 11.7 | none |
| `vVandal` | 0 | 360.3, -6.8, 0 | 36.6, 44.2, 21.8 | none |
| `vClubhouse` | 0 | 367.2, 7.8, 4 | 20.7, 16.4, 7.8 | none |
| `vWindowBox` | 0 | 386.8, 3.9, -5 | 20.7, 24, 7.8 | none |
| `vOutsideCam` | 0 | 374.3, -1.9, 1.7 | 5.9, 12.6, 4.6 | -0.009, -1, 1, -0.009 |

### level60

Stage 1, entry 8,487, 4 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `VDestTrig_01` | 0 | -195.4, 14.8, -0.1 | 6, 2, 7 | none |
| `VSMTrig_01` | 0 | -688.5, 18.1, -0.1 | 5, 2, 10 | none |
| `VSMTrig_03` | 0 | -795.6, 17.7, -0.1 | 5, 2, 7 | none |
| `VSMTrig_02` | 0 | -782.5, 11.8, -1.1 | 24, 8, 5.3 | none |

### level61

Stage 2, entry 8,513, 8 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `VHunTrig_12` | 0 | -254.8, 17.6, -0.1 | 4, 2, 10 | none |
| `VHatsTrig_06` | 0 | -604.7, 19.9, -0.2 | 2, 4, 7 | none |
| `VHatsTrig_07` | 0 | -550.5, 17.3, -0.1 | 4, 2, 7 | none |
| `VHatsTrig_08` | 0 | -596, 13.8, -5.1 | 10, 6, 5.5 | none |
| `VEETrig_05` | 0 | -435.7, 17.3, -0.1 | 6, 2, 7 | none |
| `VPunksTrig_07` | 0 | -780.8, 17.4, -0.1 | 4, 2, 7 | none |
| `Turf_14` | 3 | -765.4, 18.4, -0.9 | 6.8, 7.4, 7 | none |
| `Turf_01` | 3 | -435.6, 20.1, -0.4 | 5.2, 5.6, 2 | none |

### level62

Stage 3, entry 8,545, 6 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `VLizTrig_09` | 0 | -162, 17.3, -0.1 | 4, 2, 7 | none |
| `VLizTrig_10` | 0 | -239.4, 17.5, -0.1 | 4, 2, 7 | none |
| `VBoppTrig_09` | 0 | -426.3, 15.7, -0.1 | 4, 4, 7 | none |
| `VBoppTrig_10` | 0 | -467.7, 15.7, -0.1 | 4, 4, 7 | none |
| `VBoppTrig_11` | 0 | -508.1, 15.7, -0.1 | 4, 4, 7 | none |
| `VHurrTrig_07` | 0 | -806.5, 16.9, -0.1 | 4, 2, 7 | none |

### level63

Stage 4, entry 8,569, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `VOrphTrig_11` | 0 | -138.1, 18.7, -0.1 | 2, 8, 7 | none |

### level64

Stage 5, entry 8,594, 4 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `Turf_01` | 3 | -593, 19.9, -0.9 | 6, 6, 7 | none |
| `Turf_02` | 3 | -443.5, 14.3, -0.9 | 6.4, 11.4, 7 | none |
| `VRiffsTrig_02` | 0 | -310.3, 17.4, -0.9 | 2, 8, 7 | 0.94, -0.342, 0.342, 0.94 |
| `VRogueTrig_09` | 0 | -621.1, 13.7, -3.2 | 16, 6, 3 | none |

### level80

Real Live Bunch, entry 8,729, 25 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbAmbSound_01` | 2 | -249.3, 217.9, 1.1 | 11.8, 10, 3.5 | none |
| `vbAmbSound_02` | 2 | -183.8, 81.8, 1.1 | 28.2, 30, 10 | none |
| `vbAmbSound_03` | 2 | -187.7, 81.8, 1.1 | 6, 16.9, 10 | none |
| `vbAmbSound_04` | 2 | -217, 89, -197 | 33, 46, 10 | none |
| `vAjaxTrigger` | 0 | -195.7, 154.4, 1.1 | 8, 11.9, 3.5 | 0.972, -0.236, 0.236, 0.972 |
| `vSnowCowboyTrigger` | 0 | -195.7, 210.8, 1.1 | 11.5, 7.2, 3.5 | none |
| `vTurfSnowCowboy` | 3 | -202.6, 229.2, 1.1 | 10.8, 14.9, 3.5 | none |
| `vFoxTrigger` | 0 | -237.3, 256.2, 1.1 | 10, 6, 3.5 | none |
| `vVandalBox` | 0 | -265, 262.2, 1 | 34.3, 19, 6 | none |
| `vBottleBox` | 0 | -264.7, 274.7, 1.1 | 8.2, 6.5, 3.5 | none |
| `vTurfFoxCops` | 3 | -264.6, 247.2, 1.1 | 15, 34, 3.5 | none |
| `vCochiseSwanTrigger` | 0 | -244.4, 167.3, 4.2 | 9.5, 10.4, 3.5 | none |
| `vSnowCowboyScene` | 0 | -197.8, 226, 1.1 | 4, 3, 3.5 | none |
| `vPharmacyStore` | 0 | -249.4, 217.9, 1.1 | 12.4, 10.2, 3.5 | none |
| `vStoreFrontTrigger` | 0 | -242.7, 228, 1.1 | 1.6, 1.6, 3.5 | none |
| `vStoreBackTrigger` | 0 | -246.7, 218.1, 1.1 | 1.6, 1.6, 3.5 | none |
| `vWarriorsTrigger` | 0 | -198.4, 124.1, 1.1 | 10, 10, 3.5 | none |
| `vDestroyerCopTrigger` | 0 | -220.7, 218.5, 1.1 | 7.6, 4, 3.5 | none |
| `vArrestDestroyerTrigger` | 0 | -207.4, 250.8, 1.1 | 7.2, 2, 3.5 | 1, -0.002, 0.002, 1 |
| `vThrowTutorialTrigger` | 0 | -246.4, 265.8, 5.5 | 5, 6, 3.5 | none |
| `vFoxRescueTrigger` | 0 | -240, 252.1, 1.1 | 4, 4, 3.5 | none |
| `vStealTrigger` | 0 | -249.4, 217.9, 1.1 | 12.4, 10.2, 3.5 | none |
| `vDestroyerTalkTrigger` | 0 | -248.1, 234.6, 1.1 | 8, 4, 3.5 | none |
| `vTurfDestroyers` | 3 | -254.1, 147.4, 1.1 | 10, 10, 3.5 | none |
| `vGarbageBumTrigger` | 0 | -199.7, 282.7, 1.1 | 2, 2, 3.5 | none |

### level81

Encore, entry 8,779, 28 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vChaseCam_01` | 2 | 100.3, -94, -216 | 4, 4, 3.5 | none |
| `vChaseCam_02` | 2 | 128.7, -117.9, -216 | 4, 4, 3.5 | none |
| `vChaseCam_04` | 2 | 156.4, -118.1, -216.6 | 4, 4, 3.5 | none |
| `vChaseCam_05` | 2 | 177.7, -117.9, -216.6 | 4, 4, 3.5 | none |
| `vChaseCam_03` | 2 | 147.9, -118.1, -214.7 | 4, 4, 3.5 | none |
| `vChaseCam_06` | 2 | 198.6, -81.5, -214.8 | 4, 4, 3.5 | none |
| `vTurfBrawl` | 3 | 4.9, -50.3, -5.5 | 53.8, 31.4, 31.2 | -1, -0.001, 0.001, -1 |
| `vThrowersBox` | 0 | 38.8, -33.9, 3 | 12, 6, 1.7 | 0, -1, 1, 0 |
| `vChatterTrigger` | 0 | 207.7, -32.2, -212.9 | 3, 3, 5 | none |
| `vAmbushTrigger` | 0 | 81.6, -41.3, -210.7 | 15.9, 4, 6.8 | none |
| `vHorrorTurf` | 3 | 72.3, -63.9, -210.7 | 32.7, 43.3, 6.8 | none |
| `vPowerSwitch` | 2 | 103, -31.9, -209.6 | 2, 2, 3.5 | none |
| `vChaseTrigger` | 0 | 98.8, -59.4, -216.8 | 9, 12, 6 | none |
| `vSmoke1` | 0 | 155.4, -117.6, -216.6 | 4, 3, 3.5 | none |
| `vSmoke2` | 0 | 163.9, -117.6, -216.6 | 4, 3, 3.5 | none |
| `vSmoke3` | 0 | 172.9, -117.6, -216.6 | 4, 3, 3.5 | none |
| `vTunnelExit` | 0 | 197.6, -60.1, -210 | 6, 8, 6 | 1, 0.003, -0.003, 1 |
| `vTera` | 0 | 128.7, -117.6, -215.9 | 4, 3, 3.5 | none |
| `vSpider` | 0 | 100.1, -87.6, -215.9 | 4, 3, 3.5 | 0.042, 0.999, -0.999, 0.042 |
| `vStrobe2` | 0 | 160.2, -117.6, -216.6 | 4, 3, 3.5 | none |
| `vStrobe1` | 0 | 102.6, -107.6, -215.9 | 4, 3, 3.5 | 0.427, 0.904, -0.904, 0.427 |
| `vSpook` | 0 | 182.3, -117.2, -215.9 | 4, 3, 3.5 | 0.989, -0.148, 0.148, 0.989 |
| `vStrobe3` | 0 | 198.7, -95.7, -216.6 | 4, 3, 3.5 | 0, -1, 1, 0 |
| `vStrobe4` | 0 | 198.6, -72.3, -213.2 | 4, 3, 3.5 | 0, -1, 1, 0 |
| `vSteamKill` | 0 | 37.5, -47.6, -0.6 | 5.8, 1.9, 2.5 | 0.967, -0.254, 0.254, 0.967 |
| `vbLightOn` | 0 | 219.1, -52.9, -213.2 | 4.5, 8, 8 | none |
| `vbLightOff` | 0 | 202.4, -33.7, -211.3 | 8, 6, 5 | none |
| `vbNoZoom` | 0 | 207.5, -30.2, -212.9 | 21.1, 7.3, 3.6 | none |

### level82

Roots, entry 8,845, 51 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vEvents_04` | 0 | 143.1, -103.9, -3 | 72.9, 81.7, 10.2 | none |
| `vCopTurf_01` | 3 | 202.9, -133.7, -1 | 177.2, 197.3, 91.7 | 1, 0.003, -0.003, 1 |
| `vCopTurf_02` | 3 | 137.4, -147.8, -1 | 138.5, 126.3, 91.7 | none |
| `vChangeRange_07` | 0 | 345.9, -160.9, -199.8 | 20, 28, 4 | none |
| `vChangeRange_04` | 0 | 365.9, -160.9, -199.8 | 10, 28, 4 | none |
| `vChangeRange_05` | 0 | 345.9, -123.7, -199.8 | 30, 20, 4 | none |
| `vChangeRange_01` | 0 | 306, -129.6, -199.8 | 30, 26, 4 | none |
| `vChangeRange_02` | 0 | 305.9, -159.8, -199.8 | 30, 36, 4 | none |
| `vChangeRange_06` | 0 | 345.8, -134.9, -195.3 | 30, 20, 4 | none |
| `vbAmbSound_01` | 2 | 506.6, -85.1, -0.1 | 26.6, 26.7, 13.5 | none |
| `vbAmbSound_02` | 2 | 346, -158.6, -199.6 | 30, 55, 11 | none |
| `vbAmbSound_03` | 2 | 306, -158.6, -199.6 | 30, 55, 11 | none |
| `vbAmbSound_04` | 2 | 61.1, 16.9, -0.1 | 26.7, 25, 12.5 | none |
| `vbAmbSound_05` | 2 | 61.3, 7, -0.1 | 6.6, 14, 12.5 | none |
| `vbAmbSound_06` | 2 | 96.8, -59.5, -0.1 | 28.8, 29.4, 3.5 | none |
| `vbAmbSound_07` | 2 | 106.7, -69.4, -0.1 | 29.4, 18.8, 3.5 | none |
| `vbAmbSound_08` | 2 | 46.7, -141.4, -0.1 | 59.3, 59.5, 3.5 | none |
| `vT2` | 0 | 149.8, -102.9, -1 | 50.7, 93.6, 3.9 | none |
| `vSceneC3` | 0 | 276.4, -87.2, 8.5 | 11, 14.9, 6.9 | none |
| `vSM09Turf` | 3 | 249.5, -125.2, -1 | 56.5, 58, 28.4 | none |
| `vCopArea` | 0 | 194.7, -19, -1 | 28.7, 51.7, 38.8 | none |
| `vStakeCam01` | 0 | 170.1, -22.4, -1 | 50.3, 18.2, 10.8 | none |
| `vLastCheckpoint` | 0 | 296.2, -132.8, 4 | 4.1, 4.6, 2.2 | none |
| `vViewCops` | 0 | 273.9, -104.3, 8 | 26.8, 5.1, 2.5 | 0.941, 0.339, -0.339, 0.941 |
| `vT21` | 0 | 265.4, -118.1, 8 | 31.1, 9.9, 2.5 | none |
| `vT3` | 0 | 205, -11.7, -1 | 15.9, 24.5, 8.6 | none |
| `vLCScene` | 0 | 196.6, -16.2, -1 | 6.9, 12.9, 5.3 | none |
| `vRoofTurf` | 3 | 265.6, -163.7, 4.3 | 54.5, 67.5, 13.9 | none |
| `vRoofDest` | 0 | 292, -142.1, 8.8 | 4, 3.9, 2.6 | none |
| `vBumTurf03` | 3 | 157.5, -107.4, -1 | 44.4, 127.3, 10.2 | none |
| `vFactoryTurf` | 3 | 237.8, -38.9, -1 | 28.9, 47, 11.2 | 0, -1, 1, 0 |
| `vCopsTurf` | 3 | 292.9, -32.6, -1 | 28.6, 22.9, 11.2 | none |
| `vFightTurf` | 3 | 178.6, -47.1, -1 | 3.9, 4, 2.2 | none |
| `vDonutStore` | 0 | 194, -113.5, -1 | 12.6, 13.2, 10.1 | none |
| `vDestTurf` | 3 | 401, -42.3, -4.9 | 32.8, 39.6, 25.7 | none |
| `vStationTurf` | 3 | 192, -120.4, -1 | 29.2, 31.4, 10.1 | none |
| `vDestWalk` | 0 | 390.9, -45.3, -7.4 | 17.4, 7, 9.4 | 0.696, 0.718, -0.718, 0.696 |
| `tWareHouse01` | 3 | 306, -161, -200.9 | 30, 60, 22.2 | none |
| `vOpenDS03` | 0 | 352, -149.8, -199.8 | 24, 8, 6 | none |
| `vWHPit` | 0 | 336.1, -158.8, -199.8 | 9.7, 52.6, 4 | none |
| `tWareHouse02` | 3 | 345.9, -161, -200.9 | 30, 60, 22.2 | none |
| `tWareHouse03` | 3 | 305.8, -161, -200.9 | 70, 60, 22.2 | none |
| `volPhase02` | 0 | 336, -149, -195.7 | 8, 6, 6 | none |
| `volPhase03` | 0 | 337.9, -119.5, -195.7 | 8, 6, 6 | none |
| `volWHGoal01` | 0 | 323.6, -147.6, -199.1 | 3, 3, 6 | none |
| `volWHGoal02` | 0 | 346.2, -120.2, -195.5 | 3, 4, 6 | none |
| `volWHGoal04` | 0 | 301.1, -133.2, -195.4 | 12, 4, 6 | none |
| `vKillWall` | 0 | 278.4, -132.4, -193.7 | 6, 6, 5.5 | none |
| `volWHOutSide` | 0 | 336, -149.8, -196.3 | 9.9, 37.9, 5 | none |
| `volWHGoal03` | 0 | 355.3, -118.1, -199.2 | 3, 4, 6 | none |
| `vRoofCam2` | 0 | 279.8, -131.7, -192.8 | 4.4, 10.2, 4 | none |

### level83

Heavy Muscle, entry 8,915, 30 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbSwitchCam_01` | 2 | 0.2, -85.6, -1.9 | 10.6, 4, 5.1 | -0.001, 1, -1, -0.001 |
| `vbSwitchCam_02` | 2 | -6.5, -77.3, -0.1 | 10.6, 4, 5.1 | -1, 0, 0, -1 |
| `vbSwitchCam_03` | 2 | -4.9, -34.4, -0.1 | 10.6, 4, 5.1 | -1, 0, 0, -1 |
| `vbAmbSound_01` | 2 | 220.8, -1.3, -0.1 | 26.9, 24.9, 12.6 | none |
| `vbAmbSound_02` | 2 | 220.7, -11.8, -0.1 | 6.9, 11.8, 12.6 | none |
| `vbAmbSound_03` | 2 | 261.9, 16.5, -0.1 | 13.5, 13.5, 3.5 | none |
| `vbAmbSound_04` | 2 | 263.7, 52.8, -0.1 | 17.3, 19.1, 3.5 | none |
| `vbAmbSound_05` | 2 | 272.5, 111.3, -0.1 | 28.5, 50, 12.5 | none |
| `vBoxDonut` | 0 | 261.6, 20.3, 0 | 14.1, 13.5, 8.1 | none |
| `vStoreDiner` | 0 | 304.6, 47.2, -1 | 13.7, 20.1, 8.8 | none |
| `vNorthStreet` | 0 | 127.4, 12.6, -1 | 247, 94.8, 57.6 | 0.968, 0.251, -0.251, 0.968 |
| `vSpotBeansie` | 0 | 257.2, 48.5, -1 | 9.1, 6.8, 7.4 | none |
| `vAlley` | 2 | 229.5, 65.1, -1 | 25.3, 12, 10 | -0.601, -0.799, 0.799, -0.601 |
| `vDinerTurf` | 3 | 248.1, 9.3, -1 | 40.6, 36, 12.5 | none |
| `vAlleyTurf` | 3 | 227.1, 61.7, -1 | 40.6, 36, 12.5 | none |
| `vFightPenTurf` | 3 | 162.4, 20.3, -203 | 38.7, 27.2, 23.3 | none |
| `vbBoardwalk` | 2 | -119.3, -14.4, 0.2 | 11.5, 8, 5.1 | 0, -1, 1, 0 |
| `vbAlleyTurf` | 3 | 203.3, -16.4, -0.7 | 88, 40, 20 | none |
| `vbPlaySubway` | 2 | 127, -100.3, -0.4 | 110.2, 163.5, 20 | none |
| `vbLeaveAptObj` | 2 | 14.3, -86, -0.1 | 5, 5, 6 | none |
| `vbLemScene` | 2 | 162.5, -82.8, -3.9 | 20, 20, 24.9 | none |
| `vbApartmentTurf` | 3 | 19, -136.5, -4.5 | 120, 60, 20 | none |
| `vbShantyTurf` | 3 | 159.3, -92.5, -3 | 43.7, 116, 15 | none |
| `vbSpawnShanty` | 2 | 247.2, -5.6, -2.4 | 6.6, 8, 12 | 0.008, -1, 1, 0.008 |
| `vbTurnOnCops` | 2 | 108.2, -107.4, -0.4 | 9.9, 9.7, 4.8 | none |
| `vbChaseTurf` | 3 | -143.5, -160.7, -4.5 | 260, 200, 30 | none |
| `vbSwitchAmbient` | 2 | 34.5, -98.2, -3 | 49.3, 19.9, 20 | none |
| `vbDonutRegion` | 2 | 261.3, 22.8, -0.1 | 14.1, 14, 8.1 | none |
| `vbLeaveArea` | 2 | 253.9, 12.5, 0 | 4.4, 7.3, 3.7 | none |
| `vbEscapeTurf` | 3 | 220.2, 12.2, -2.4 | 120, 90, 12.5 | none |

### level84

Come Out To Play, entry 9,029, 16 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `VCarChaseTrig_01` | 0 | -52, -105.5, -0.8 | 6, 6, 10 | none |
| `VCarChaseTrig_05` | 0 | 64.5, -104.8, -0.8 | 4, 4, 10 | none |
| `VCarChaseTrig_03` | 0 | 26, -105.5, -0.8 | 6, 6, 10 | none |
| `VCarChaseTrig_02` | 0 | -13, -105.5, -0.8 | 6, 6, 10 | none |
| `VCarChaseTrig_08` | 0 | 54.5, -107.5, -0.8 | 10, 10, 10 | none |
| `VCarChaseTrig_09` | 0 | 67.6, -118.2, -0.8 | 10, 10, 10 | none |
| `VCarChaseTrig_10` | 0 | -52.5, -105.5, -0.8 | 6, 6, 10 | none |
| `VCarChaseTrig_11` | 0 | -13.5, -105.5, -0.8 | 6, 6, 10 | none |
| `VCarChaseTrig_12` | 0 | 25.5, -105.5, -0.8 | 6, 6, 10 | none |
| `VCarChaseTrig_13` | 0 | 64.3, -113, -0.8 | 10, 14, 10 | none |
| `TurfFight02` | 3 | -479.6, -240.8, -1.4 | 16, 10, 6 | none |
| `vFOVZoom` | 0 | -471.1, -229.9, -0.9 | 14, 24, 6 | none |
| `vFOVZoom01` | 0 | -472.7, -229.9, -0.9 | 16, 24, 6 | none |
| `TurfF01` | 3 | -598.2, -226.9, -0.9 | 4, 4, 6 | -0.996, 0.087, -0.087, -0.996 |
| `TurfF02` | 3 | -600.1, -224.8, -0.9 | 4, 4, 6 | -0.707, 0.707, -0.707, -0.707 |
| `TurfF03` | 3 | -594, -227.2, -0.9 | 4, 4, 6 | none |

### level86

Payin' The Cost, entry 9,116, 32 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbAmbSound_01` | 2 | -138, 51.5, -0.1 | 29.5, 17.3, 3.5 | none |
| `vbAmbSound_02` | 2 | -148.6, -19.6, -0.1 | 13.2, 25.4, 3.5 | none |
| `vbAmbSound_03` | 2 | -38, 6.4, -0.1 | 19.6, 14.9, 3.5 | none |
| `vbAmbSound_04` | 2 | -77.8, 11.3, -0.1 | 19.6, 19.6, 3.5 | none |
| `vbAmbSound_05` | 2 | -174.8, 11, -0.1 | 29.8, 20.6, 3.5 | none |
| `vbAmbSound_06` | 2 | -187.7, 81.6, -0.1 | 32.6, 24.5, 12.5 | none |
| `vbAmbSound_07` | 2 | -187.7, 103.7, -0.1 | 15.6, 8, 12.5 | none |
| `vPerimeterMildred` | 0 | -91.1, 4.6, -3 | 60, 16, 6.5 | none |
| `vPerimeterTwiggy` | 0 | -108.7, 23, -0.1 | 64, 48, 6.5 | none |
| `vPerimeterChuck` | 0 | -150.4, 15.7, -0.1 | 64, 60, 6.5 | none |
| `vTurfTwiggy` | 3 | -95.1, 48, -0.1 | 25.2, 20, 6.5 | none |
| `vTurfMarcello` | 3 | -143.3, -17.2, -0.1 | 30.9, 28, 6.5 | none |
| `vTurfMildred` | 3 | -54.7, 1, -0.6 | 20.6, 20, 6.5 | none |
| `vTurfChuck` | 3 | -138.6, 46, -0.1 | 29.9, 22.9, 6.5 | none |
| `vChuck` | 0 | -138.4, 47.1, -0.7 | 29.5, 17.4, 6.5 | none |
| `vMarcello` | 0 | -136.1, -5.9, -0.1 | 19, 13, 6.5 | none |
| `vTwiggy` | 0 | -88.3, 48.1, -0.1 | 22, 18, 6.5 | none |
| `vMildred` | 0 | -54.6, 0.3, -0.1 | 20.2, 20.6, 6.5 | none |
| `vTurfWanderWalk` | 3 | -207.7, -44.8, -0.1 | 208.5, 140, 20 | none |
| `vVandalChuck` | 0 | -139.9, 45.3, -0.1 | 32, 26, 6.5 | none |
| `vVandalMarcello` | 0 | -144.9, -4.5, -0.1 | 34, 14, 6.5 | none |
| `vVandalMildred` | 0 | -58.7, 2.5, -0.1 | 34, 24, 6.5 | none |
| `vVandalTwiggy` | 0 | -86.5, 46.2, -0.1 | 34, 24, 6.5 | none |
| `vStartStoreOwners` | 0 | -138.3, 51.2, -0.1 | 29.1, 8.7, 6.5 | none |
| `vTurfHotDogGuy` | 3 | -97.3, 36, -0.1 | 12, 12, 6.5 | none |
| `vSoundChuck` | 0 | -141.4, 47.5, -0.1 | 32, 26, 6.5 | none |
| `vSoundMarcello` | 0 | -136.6, -5.4, -0.1 | 26, 13.2, 6.5 | none |
| `vSoundMildred` | 0 | -54.8, -2.6, -0.1 | 40, 24, 6.5 | none |
| `vHammerBox` | 0 | 44.3, 44.8, -0.1 | 10, 6, 6.5 | none |
| `vSecondVanBox` | 0 | 50.5, 62.5, -1.7 | 14, 12, 6.5 | none |
| `vPerimeterMarcello` | 0 | -156.6, -33.5, -3.1 | 70, 70, 6.5 | none |
| `vClipBox` | 0 | 39.1, 51.5, -0.1 | 16, 14, 6.5 | none |

### level87

Payback, entry 9,170, 44 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbStreetTurf_01` | 3 | 315.8, 27.8, -3.8 | 220, 158, 30 | none |
| `vbStoreCam_Laundry` | 2 | 330.7, 136.4, -0.4 | 11.2, 10, 5.1 | none |
| `vbStoreCam_Pharmacy` | 2 | 371.6, 148.4, -1.2 | 10, 11.4, 5.8 | none |
| `vbStoreCam_Liquor` | 2 | 440, 114.1, -1.4 | 10.2, 11.6, 5.8 | none |
| `vbStoreCam_Hardware` | 2 | 410.9, 89.1, -1.2 | 11.7, 11.5, 5.7 | none |
| `vbAmbSound_01` | 2 | 371.9, 148.4, -0.8 | 9.7, 11.3, 3.5 | none |
| `vbAmbSound_02` | 2 | 440.3, 114.2, -0.1 | 9.7, 11.3, 3.5 | none |
| `vbAmbSound_03` | 2 | 412.8, 89, -0.1 | 9.7, 11.3, 3.5 | none |
| `vbAmbSound_12` | 2 | 330.7, 136.6, -0.9 | 11.3, 9.8, 3.5 | none |
| `vbPornTrigger` | 2 | 435.8, 82.7, 8.4 | 4, 4, 3.7 | none |
| `vbStartroofChat` | 2 | 451.2, 128.1, -2.2 | 18, 44, 14.1 | none |
| `vbBumScrap` | 2 | 385.8, 145.7, -1.6 | 30, 7.5, 20 | none |
| `vbRPGo` | 2 | 448.7, 107.8, 8.2 | 6, 4, 4 | none |
| `vbSide2Turf` | 3 | 414.9, 42.8, -6 | 86.1, 146.5, 20 | none |
| `vbBackAlleyTurf` | 3 | 372.8, 54.1, -2.1 | 46, 72, 15 | none |
| `vbLosBox` | 2 | 455.2, 100.2, -0.7 | 14.4, 13.1, 6.6 | none |
| `vbHearParty` | 2 | 440.1, 99.6, -2.3 | 40, 38.9, 19.5 | none |
| `vbStartRoofRadio` | 2 | 415.6, 93.8, -2 | 88.3, 10, 27.5 | none |
| `vbHomeInvasion` | 2 | 489, 146.5, 0 | 9.9, 11.1, 5.9 | none |
| `vbSpawnHome` | 2 | 489.5, 141.5, 0 | 3.5, 4.7, 2.3 | none |
| `vbRailDistSwitch` | 2 | 489, 149.8, 0 | 11, 8, 3.3 | none |
| `vbRembrandtGo` | 0 | 351.5, 49.2, -0.8 | 2.5, 7.1, 3.6 | none |
| `vbTriggerPuker` | 0 | 345.4, 72.7, -0.6 | 30, 20, 4 | 0.002, 1, -1, 0.002 |
| `vbTrainTrigger` | 2 | 385.2, 167.3, 5 | 4.1, 4.1, 2.1 | none |
| `vbTurnOffSystemSound` | 2 | 433.5, 64, -2 | 50, 80, 27.5 | none |
| `vbExitFour` | 2 | 419.3, 78.2, -0.1 | 12, 11, 2.8 | none |
| `vbStartRoofBanter` | 2 | 475.7, 71.6, 5.3 | 6.1, 6.5, 3.2 | none |
| `vbPukeScoutTurf` | 3 | 437.2, -96.8, -0.8 | 16, 16, 6 | none |
| `volObjTutorial05` | 0 | 415.1, -104.1, -0.8 | 4, 10, 6 | 1, 0, 0, 1 |
| `volTag9Objective` | 0 | 428.6, -115.8, 1.9 | 4.4, 8, 4 | none |
| `volDistract` | 0 | 424.7, -94.7, -0.2 | 4.5, 14, 8 | none |
| `volObjTutorial03` | 0 | 425, -101.6, -0.2 | 12, 20, 3 | none |
| `volObjTutorial04` | 0 | 416.9, -104.2, -0.8 | 8, 10, 6 | 1, 0, 0, 1 |
| `volObjTutorial02` | 0 | 429.9, -102, -0.8 | 8, 10, 6 | 0.895, -0.445, 0.445, 0.895 |
| `volPuker` | 0 | 444.9, -80.7, -0.8 | 8, 10, 6 | none |
| `volCh5HB01` | 0 | 405.2, -69.4, -0.8 | 12, 12, 6 | 1, 0, 0, 1 |
| `volObjTutorial01` | 0 | 410.4, -72.6, -0.8 | 8, 14, 6 | 0.695, 0.719, -0.719, 0.695 |
| `vbDestClubTurf` | 3 | 363.2, -166.5, -6.5 | 100, 120, 29.4 | none |
| `vbGotBack` | 2 | 413.7, -103.7, -0.4 | 4, 6, 6 | 1, 0, 0, 1 |
| `vbFinalGlow` | 2 | 417.4, -110.3, -2.7 | 4.4, 8, 4 | none |
| `vbStealth5` | 0 | 406.3, -108.2, -2.4 | 2.5, 2.5, 3 | none |
| `vbGoVoltron` | 2 | 343.1, 56.3, -0.1 | 6.5, 4.8, 5 | none |
| `vbForceScoutTurn` | 2 | 425.3, -69.2, -0.1 | 6, 11.8, 5.9 | none |
| `vbCheckTeleport` | 2 | 433.5, -99, 0 | 1.2, 5.7, 2.9 | none |

### level92

The Best, entry 9,257, 33 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbAmbSound_01` | 2 | -138, 47.6, -0.1 | 29.5, 17.3, 3.5 | none |
| `vbAmbSound_02` | 2 | -136.3, -5.3, -0.1 | 25.4, 13.1, 3.5 | none |
| `vbAmbSound_03` | 2 | -54.7, 6.4, -0.1 | 19.6, 14.9, 3.5 | none |
| `vbAmbSound_06` | 2 | -187.7, 81.6, -194.3 | 32.6, 24.5, 12.5 | none |
| `vbAmbSound_05` | 2 | -187.7, 103.7, -197.6 | 15.6, 8, 12.5 | none |
| `vTopTier` | 0 | -86.9, 119.5, -311.9 | 5.1, 7.4, 2.5 | none |
| `vHeadStore` | 0 | -54.7, 6.5, 0 | 15.7, 14.8, 9.5 | none |
| `vVoodooStore` | 0 | -88.9, 47.3, -0.6 | 24.2, 19.3, 9.7 | none |
| `vPlumbStore` | 0 | -138, 47.8, -1.7 | 29.5, 17.5, 8.8 | 0.99, 0, 0, 0.967 |
| `vPlumbFront` | 0 | -125, 46.1, 0.1 | 6.2, 1.6, 3.5 | 0.99, 0, 0, 0.967 |
| `vVoodooFront` | 0 | -76.2, 47.5, -0.6 | 6, 6, 6.5 | 0.99, 0, 0, 0.967 |
| `vHeadFront` | 0 | -58, 9.9, 0.1 | 3.1, 2.6, 3.5 | 0.99, 0, 0, 0.967 |
| `vMeatFront` | 0 | -132.5, 7.9, 0.1 | 2.4, 1.6, 3.5 | 0.99, 0.001, -0.001, 0.967 |
| `vTurfPlumbBackup` | 3 | -158.1, 32, 0.1 | 55.6, 40, 3.5 | 0.99, 0, 0, 0.967 |
| `vTurfVoodooBackup` | 3 | -102.2, 17.6, 0.1 | 37.4, 48.7, 3.5 | 0.99, 0, 0, 0.967 |
| `vTurfHeadBackup` | 3 | -58.9, 3.8, 0.1 | 20, 20, 3.5 | 0.99, 0, 0, 0.967 |
| `vTurfMeat` | 3 | -130.2, -11, 0.1 | 13.2, 24.5, 3.5 | 0, -0.967, 0.99, 0 |
| `vVoodooTalk` | 0 | -97.9, 37.7, -0.6 | 33, 28.8, 6.5 | 0.99, 0, 0, 0.967 |
| `vPlumbTalk` | 0 | -145.7, 31.8, 0.1 | 43.3, 28.4, 3.5 | 0.99, 0, 0, 0.967 |
| `vHeadTalk` | 0 | -70.7, 3.8, 0.1 | 16, 20, 3.5 | 0.99, 0, 0, 0.967 |
| `vTurfHead` | 3 | -54.7, 6.5, 0 | 15.7, 14.8, 9.5 | none |
| `vTurfPlumb` | 3 | -138, 47.8, -1.7 | 29.5, 17.5, 8.8 | 0.99, 0, 0, 0.967 |
| `vTurfVoodoo` | 3 | -88.9, 47.4, -0.6 | 24.2, 19.3, 9.7 | none |
| `vPlumbStoreOwner` | 0 | -134.8, 42.4, -1.7 | 6, 6, 8.8 | 0.99, 0, 0, 0.967 |
| `vRoofTop` | 0 | -190.4, 105.3, -194 | 10, 10, 8.8 | 0.99, 0, 0, 0.967 |
| `vTurfPlumbOwner` | 3 | -133.8, 43.4, -1.7 | 4, 4, 8.8 | 0.99, 0, 0, 0.967 |
| `vTurfCarny` | 3 | -78.1, -17.1, 0.1 | 23.2, 43.5, 3.5 | 0.99, 0, 0, 0.967 |
| `vMeatTalk` | 0 | -144.8, -5.2, 0.1 | 40.8, 21.9, 3.5 | 0.99, 0, 0, 0.967 |
| `vPlumbPerimeter` | 0 | -138.1, 45.1, -1.7 | 32.9, 19.7, 8.8 | 0.99, 0, 0, 0.967 |
| `vMeatPerimeter` | 0 | -138.8, -5.2, 0.1 | 27.8, 15.1, 3.5 | 0.99, 0, 0, 0.967 |
| `vHeadPerimeter` | 0 | -58.2, 6.5, 0 | 19.4, 14.8, 9.5 | none |
| `vVoodooPerimeter` | 0 | -88.9, 45.1, -0.6 | 24.2, 21.7, 9.7 | none |
| `vTurfMeatDestroyers` | 3 | -139.2, -8.1, 0.1 | 18, 23.3, 3.5 | 0, -0.967, 0.99, 0 |

### level93

Destroyed, entry 9,320, 59 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vLCTrigger_01` | 2 | 427.1, -103.7, 0.1 | 6, 6, 3.5 | -1, 0, 0, -1 |
| `vLCTrigger_02` | 2 | 444.2, -72.9, 0.1 | 8.5, 9.4, 3.5 | -1, 0, 0, -1 |
| `vLCTrigger_03` | 2 | 405.6, -63.8, 0.1 | 10, 11.6, 3.5 | -1, 0, 0, -1 |
| `vLCTrigger_04` | 2 | 365.8, -68.6, 0.1 | 9, 8, 3.5 | -1, 0, 0, -1 |
| `vbAmbSound_01` | 2 | 371.9, 148.4, -0.1 | 9.8, 11.3, 3.5 | none |
| `vbAmbSound_04` | 2 | 370.4, -31.9, -2.5 | 30.9, 24.3, 11.5 | none |
| `vbAmbSound_06` | 2 | 370.3, -45.4, -2.5 | 17.6, 17.4, 11.5 | none |
| `vbAmbSound_05` | 2 | 380.7, -40.2, -2.5 | 19.2, 12, 11.5 | 0.707, -0.707, 0.707, 0.707 |
| `vbAmbSound_07` | 2 | 324.4, 40.6, -199.9 | 18.7, 36, 3.5 | none |
| `vbAmbSound_02` | 2 | 340.3, 103.1, -0.1 | 9.8, 11.3, 3.5 | none |
| `vbAmbSound_03` | 2 | 402.9, 183.7, -0.1 | 12.5, 25.2, 3.5 | none |
| `vPharmacyStore` | 0 | 371.8, 148.4, 0.1 | 9.8, 11.3, 3.5 | none |
| `vLiquorStore` | 0 | 340.3, 103.1, 0.1 | 9.8, 11.3, 3.5 | none |
| `vPharmacyFront` | 0 | 369.8, 154.3, 0.1 | 2, 2.4, 3.5 | none |
| `vLiquorFront` | 0 | 350.2, 109, 0.1 | 2, 2.4, 3.5 | none |
| `vAlleyTrigger` | 0 | 371.1, 92.5, 0.1 | 1.6, 2.4, 3.5 | none |
| `vTurfPimp` | 3 | 380.1, 61.2, 0.1 | 23.7, 35.4, 3.5 | -1, 0, 0, -1 |
| `vTurfRiot` | 3 | 308.3, 67.3, -1 | 110, 142, 10 | none |
| `vTurfStore` | 3 | 330.2, 124.1, 0.1 | 8, 10, 3.5 | none |
| `vHouseFight` | 0 | 388.3, 150, 0.1 | 17.8, 16.5, 3.5 | none |
| `vTurfCorner` | 3 | 385.9, 61.4, 0.1 | 12, 10, 3.5 | none |
| `vRoofFight` | 0 | 369.2, 131, 5.8 | 10.7, 10.1, 3.5 | none |
| `vStoreFight` | 0 | 328.7, 121.6, 0.1 | 11, 14.8, 3.5 | none |
| `vTurfHouse` | 3 | 380.9, 162.5, 3.1 | 10.5, 9, 6 | none |
| `vCornerFight` | 0 | 383.1, 61.3, 0.1 | 17.6, 12.4, 3.5 | none |
| `vTurfRoof` | 3 | 371.6, 133.2, 5.8 | 6, 6, 3.5 | none |
| `vGarbageBumTrigger` | 0 | 374.6, 129.3, 0.1 | 2, 1.6, 3.5 | none |
| `vTurfLiquorDestroyers` | 3 | 332.7, 98.2, 0.1 | 37.7, 38.4, 3.5 | none |
| `vTurfWarehouseDestroyers2` | 3 | 383.7, 142.2, 0.1 | 23.1, 20.6, 3.5 | none |
| `vGambleHouseFront` | 0 | 408.5, 183.6, 0.1 | 4, 2, 3.5 | none |
| `vWarehouse` | 0 | 383.7, 151.9, 0.1 | 14, 11, 3.5 | none |
| `vPharmacyPerimeter` | 0 | 369.5, 148.4, 0.1 | 12.1, 11.3, 3.5 | none |
| `vLiquorPerimeter` | 0 | 340.2, 103.1, 0.1 | 12.1, 11.3, 3.5 | none |
| `vPimp` | 0 | 394.1, 74.9, 0.1 | 8.6, 18, 3.5 | -1, 0, 0, -1 |
| `vGambleHousePerimeter` | 0 | 402.8, 183.7, 0.1 | 12.6, 25.3, 3.5 | none |
| `vTurfGambleHouse` | 3 | 402.8, 186.3, 0.1 | 12.6, 22.6, 3.5 | none |
| `vLCInsideGate` | 0 | 350.2, -49.1, 0.1 | 4, 4, 3.5 | -1, 0, 0, -1 |
| `vYardTrigger` | 0 | 346, 67, 0.1 | 2, 2.4, 3.5 | -0.001, 1, -1, -0.001 |
| `vTurfYard1` | 3 | 401.6, -138.7, -2.3 | 31.5, 34.8, 3.5 | -1, 0, 0, -1 |
| `vTurfYard2` | 3 | 405.5, -107.9, -2.3 | 27.1, 39.2, 7 | -1, 0, 0, -1 |
| `vTurfYard3` | 3 | 399.7, -141.5, -2.3 | 10.4, 11.4, 4.8 | -0.707, -0.707, 0.707, -0.707 |
| `vTurfYard4` | 3 | 369.7, -156.2, -1 | 19.3, 10.5, 3.5 | -1, 0, 0, -1 |
| `vClubhouseTrigger` | 0 | 392.5, -137.8, 3.7 | 4, 4, 3.5 | -0.707, -0.707, 0.707, -0.707 |
| `vHookerTalk` | 0 | 394.3, 81.9, 0.1 | 6, 6, 3.5 | -1, 0, 0, -1 |
| `vTurfBalcony1` | 3 | 428.7, -131.9, 2.4 | 4.5, 28, 3.5 | -1, 0, 0, -1 |
| `vTurfBalcony3` | 3 | 405.7, -147.7, 2.4 | 4.5, 20.9, 3.5 | -0.707, -0.707, 0.707, -0.707 |
| `vRallyWarriorTrigger` | 0 | 409.2, -105.1, -2 | 6, 8, 3.5 | -1, 0, 0, -1 |
| `vTurfBalcony2` | 3 | 414, -131.8, 2.4 | 19, 4.2, 3.5 | -1, 0, 0, -1 |
| `vTurfWarehouseDestroyers1` | 3 | 383.6, 153.8, 0.1 | 11.8, 9, 3.5 | none |
| `vWarehouseTrigger` | 0 | 383.7, 147.8, 0.1 | 23.1, 20.6, 3.5 | none |
| `vGambleHouse` | 0 | 402.8, 186.3, 0.1 | 12.6, 22.6, 3.5 | none |
| `vDestroyerTagger` | 0 | 322.5, 134, 0.1 | 8, 10, 3.5 | none |
| `vKillWall` | 0 | 335.4, -46.3, -201.1 | 16.4, 0.2, 8 | none |
| `vStreetTrigger` | 0 | 411.5, 119.4, 0.1 | 7, 7, 3.5 | none |
| `vWarpVergil01` | 0 | 332.7, -35.6, -200.2 | 4, 4, 7.2 | none |
| `vbFirstLine` | 2 | 344.9, 119.9, 0.1 | 8, 18, 3.5 | none |
| `vKillWall01` | 0 | 317.9, -34, -201.1 | 6.3, 1, 8 | 0, -1, 1, 0 |
| `vHideoutGateTrigger` | 0 | 351.6, -49.1, 0.1 | 4, 4, 3.5 | -1, 0, 0, -1 |
| `vZoombox` | 0 | 314.5, -49.8, -202.2 | 9.5, 5.6, 4.1 | none |

### level95

No title, entry 9,400, 16 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vbStore_3` | 0 | -54.6, 6.2, -0.3 | 16.7, 14.9, 3 | none |
| `vbStore_2` | 0 | -88.9, 47, -0.3 | 24, 20, 3 | none |
| `vbStore_1` | 0 | -136.5, -5.3, -0.3 | 25.5, 13, 3 | none |
| `vbWorkout_01` | 0 | -187.7, 99.2, -197.9 | 16, 13, 4 | none |
| `vbConeyAmb_1` | 2 | -270.9, 61.3, 0 | 60, 80, 10 | none |
| `vbConeyAmb_2` | 2 | -211.1, 14.3, 0 | 54, 140, 10 | none |
| `vbConeyAmb_3` | 2 | -157.4, 52.3, 0 | 120, 40, 10 | none |
| `vbConeyAmb_4` | 2 | -157.2, 28.4, 0 | 100, 24, 10 | none |
| `vbConeyAmb_5` | 2 | -157, -21.6, 0 | 120, 50, 10 | none |
| `vbAmbSound_1` | 2 | -138, 49.1, -0.1 | 29.5, 17.3, 3.5 | none |
| `vbAmbSound_2` | 2 | -103.5, -16.2, -0.1 | 13.2, 25.4, 3.5 | none |
| `vbAmbSound_3` | 2 | -54.9, 6.4, -0.1 | 19.6, 14.9, 3.5 | none |
| `vbAmbSound_4` | 2 | -86.7, 46.3, -0.1 | 19.6, 19.6, 3.5 | none |
| `vbAmbSound_5` | 2 | -141.3, -12.1, -0.1 | 29.8, 20.6, 3.5 | none |
| `vbTutorialStart` | 0 | -187.8, 98, -194.1 | 6, 4, 4 | none |
| `vTurfCarny` | 3 | -230, 29.5, -1.3 | 60, 50, 20 | none |

### level99

New Blood, entry 9,606, 16 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vNearStore` | 0 | 45, 48.4, 0 | 16, 2.6, 7 | none |
| `vInsideStore` | 0 | 47.4, 51.2, 0 | 11.4, 9.8, 8 | none |
| `vFenceJump` | 0 | 44.4, 31.3, 0 | 6, 2.4, 4 | none |
| `vClimb` | 0 | 51.6, 6, 0.1 | 8, 8.2, 4 | none |
| `vReachWindow` | 0 | 43.2, -4.3, 4.1 | 4, 6, 4 | none |
| `vAlmostDone` | 0 | 22.8, 2.6, -0.1 | 7.6, 7.2, 5 | 1, 0, 0, 1 |
| `vFenceJump2` | 0 | 44.4, 22.3, 0 | 6, 2.4, 1 | none |
| `vVerminJumps` | 0 | 36.3, 14.5, 3.9 | 8.5, 6, 4 | none |
| `vMark01` | 0 | -284.1, 126.2, 0.2 | 1.6, 1.6, 3 | 0.803, 0.596, -0.596, 0.803 |
| `vMark03` | 0 | -288.2, 120.8, 0.2 | 1.6, 1.6, 3 | 0.76, -0.649, 0.649, 0.76 |
| `vFenceSection` | 0 | 45.9, 48.6, 0 | 1.5, 1.4, 3 | none |
| `vFenceJump3` | 0 | 44.4, 12.8, 0 | 6, 2.4, 1 | none |
| `vReachRooftop` | 0 | 62.8, -2.5, 4 | 1.2, 1.2, 3 | none |
| `vDoneClimb` | 0 | 56.2, 4.5, 4.1 | 2.2, 1.6, 2 | none |
| `vReachRooftop2` | 0 | 50.4, -5.2, 3.3 | 4, 11.3, 5 | none |
| `vFirstClimb` | 0 | 56.2, 6, 1.3 | 2.2, 1.6, 2 | none |

## Arena boxes

One table per Rumble init script.

### level101_kinghill_init

Junkyard, entry 6,913, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vTopTier` | 0 | 15.6, -4.8, 8.3 | 5.6, 7.4, 3.5 | none |

### level103_wchair_init

Warrior Turf, entry 6,955, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFinishLine` | 0 | -100.6, -5.8, 0 | 40.3, 24.4, 31.6 | 1, 0.02, -0.02, 1 |

### level104_wchair_init

The Rotunda, entry 6,981, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFinishLine` | 0 | 1.2, -24.5, -11.5 | 10, 28, 18 | 0.994, -0.111, 0.111, 0.994 |

### level105_brawl_init

Red Devil, entry 6,991, 4 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vPOS_2` | 0 | 1.1, 1.5, -3.9 | 15.6, 9, 10 | none |
| `vPOS_4` | 0 | -14.7, -11.9, -3.9 | 12.8, 13.3, 10 | none |
| `vPOS_1` | 0 | -1.9, -11.9, -3.9 | 18.6, 13.3, 10 | none |
| `vPOS_3` | 0 | -14.5, 1.5, -3.9 | 15.6, 9, 10 | none |

### level107_tagbt_init

Courtyard, entry 7,023, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vSohoArena` | 3 | 0.9, -46.6, -0.7 | 46, 32, 15 | none |

### level108_mercy_init

Gunhill Road, entry 7,045, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFinishLine` | 0 | -84.3, 540.6, 0 | 40.3, 24.4, 31.6 | none |

### level108_wchair_init

Gunhill Road, entry 7,048, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFinishLine` | 0 | -84.3, 540.6, 0 | 40.3, 24.4, 31.6 | none |

### level111_car_init

Bensonhurst, entry 7,103, 5 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vStore01` | 0 | 9.2, -13.3, -0.9 | 11.8, 12, 11.8 | none |
| `vStore02` | 0 | -20.9, -17.8, -0.9 | 11.8, 12, 11.8 | none |
| `vStore03` | 0 | -26.1, 11.4, -0.8 | 16, 12, 11.8 | none |
| `vCar01` | 0 | -16.2, -52.2, -2.2 | 9, 8, 11.8 | none |
| `vCar02` | 0 | -2.6, -52.2, -2.2 | 7, 9, 11.8 | none |

### level111_hifi_init

Bensonhurst, entry 7,104, 3 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vStore01` | 0 | 9.2, -13.3, -0.9 | 11.8, 12, 11.8 | none |
| `vStore02` | 0 | -20.9, -17.8, -0.9 | 11.8, 12, 11.8 | none |
| `vStore03` | 0 | -26.1, 11.4, -0.8 | 16, 12, 11.8 | none |

### level111_mercy_init

Bensonhurst, entry 7,106, 3 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vStore01` | 0 | 9.2, -13.3, -0.9 | 11.8, 12, 11.8 | none |
| `vStore02` | 0 | -20.9, -17.8, -0.9 | 11.8, 12, 11.8 | none |
| `vStore03` | 0 | -26.1, 11.4, -0.8 | 16, 12, 11.8 | none |

### Entry 7105

Bensonhurst, entry 7,105, 3 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vStore01` | 0 | 9.2, -13.3, -0.9 | 11.8, 12, 11.8 | none |
| `vStore02` | 0 | -20.9, -17.8, -0.9 | 11.8, 12, 11.8 | none |
| `vStore03` | 0 | -26.1, 11.4, -0.8 | 16, 12, 11.8 | none |

### level112_mercy_init

Coney Drag, entry 7,135, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFinishLine` | 0 | -65.9, 30.3, 0 | 14.7, 20, 9.3 | 0, 1, -1, 0 |

### level112_wchair_init

Coney Drag, entry 7,137, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFinishLine` | 0 | -162.6, -3.5, 0 | 14.7, 20, 9.3 | 0, -1, 1, 0 |

### level114_wchair_init

Chinatown, entry 7,179, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFinishLine` | 0 | -69.8, -38.5, 0 | 40.3, 24.4, 31.6 | 0.707, -0.707, 0.707, 0.707 |

### level122_tagbt_init

Wrecked Apts, entry 7,412, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vSohoArena` | 3 | -22.8, -19.9, -3.1 | 48.8, 41.3, 15 | none |

### level129_kinghill_init

King o'Shanties, entry 7,514, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vTopTier` | 0 | 9.4, -27.8, 8.3 | 5, 7.4, 3.5 | -0.019, 1, -1, -0.019 |

### level130_kinghill_init

Mausoleum Hill, entry 7,528, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vTopTier` | 0 | -2.3, -9.1, 20.1 | 5, 10, 3.5 | -1, 0.009, -0.009, -1 |

### level131_royal_init

Soho Rooftops, entry 7,544, 9 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vRing` | 0 | -10.3, -7.8, 15.3 | 22, 17.5, 9 | none |
| `vRight` | 0 | 5.5, -18.5, 10.9 | 22, 40, 9 | none |
| `vLeft` | 0 | -24.7, -18.8, 10.9 | 22, 40, 9 | none |
| `vFront` | 0 | -19.3, -19.4, 10.9 | 40, 17.5, 9 | none |
| `vBack` | 0 | -19.6, 3.3, 10.9 | 40, 17.5, 9 | none |
| `vBackSpecial` | 0 | -10, 4.4, 10.9 | 6, 17.5, 9 | none |
| `vLeftSpecial` | 0 | -15.9, 2.4, 10.9 | 6, 6, 9 | none |
| `vFrontSpecial` | 0 | -6.6, -12.5, 10.9 | 6, 6, 9 | none |
| `vRightSpecial` | 0 | 4.1, 8.4, 10.9 | 6, 6, 9 | none |

### level132_royal_init

Tenement Rooftop, entry 7,557, 9 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vRing` | 0 | -4.9, 217.3, 7.1 | 24, 22, 12.3 | none |
| `vLeft` | 0 | -15.8, 213.8, 1.1 | 12.6, 30, 12.3 | none |
| `vBack` | 0 | -15.7, 237.4, 1.1 | 40, 10, 12.3 | none |
| `vRight` | 0 | 8.4, 211.1, 1.1 | 12.6, 34, 12.3 | none |
| `vFront` | 0 | -15.9, 210, 1.1 | 42, 10, 12.3 | none |
| `vBackSpecial` | 0 | 0.7, 239, 8.1 | 6, 6, 6 | none |
| `vLeftSpecial` | 0 | -10.7, 230, 7.1 | 6, 6, 8 | none |
| `vFrontSpecial` | 0 | -3.9, 211.3, 8.1 | 6, 6, 6 | none |
| `vRightSpecial` | 0 | 9.4, 211.8, 8.1 | 6, 6, 6 | none |

### level133_royal_init

Machine Shop, entry 7,584, 9 boxes.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vRing` | 0 | -63.4, 0.4, 12.1 | 16, 12, 9.3 | none |
| `vLeft` | 0 | -73.6, -8.6, 10.7 | 12.6, 30, 9.3 | none |
| `vBack` | 0 | -70.7, 11.1, 10.7 | 30, 10, 9.3 | none |
| `vRight` | 0 | -50, -8.6, 10.7 | 12.6, 30, 9.3 | none |
| `vFront` | 0 | -70.7, -8.1, 10.7 | 30, 10, 9.3 | none |
| `vBackSpecial` | 0 | -62.4, 10.7, 12.1 | 6, 6, 9.3 | none |
| `vLeftSpecial` | 0 | -68.2, 3, 12.1 | 6, 6, 9.3 | none |
| `vRightSpecial` | 0 | -48.8, 1.5, 12.1 | 4, 6, 9.3 | 0, 1, -1, 0 |
| `vFrontSpecial` | 0 | -61, -3.1, 12.1 | 4, 6, 9.3 | 0, 1, -1, 0 |

### level134_shoot_init

Tremont, entry 7,603, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vTurf` | 3 | -5.3, -16.8, -1 | 30, 40, 23.8 | none |

### level134_survival_init

Tremont, entry 7,605, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vTurf` | 3 | -5.3, -16.8, -1 | 30, 40, 23.8 | none |

### level136_run4life_init

Gunhill Chase, entry 7,637, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFinish` | 0 | -211.2, 565.5, 0 | 108.1, 23.2, 34.5 | none |

### level137_run4life_init

Furies' Chase, entry 7,652, 1 box.

| Box | Kind | Corner | Size | Turn |
| --- | ---: | --- | --- | --- |
| `vFinish` | 0 | -27.2, -98.5, 0 | 108.1, 23.2, 34.5 | 0, 1, -1, 0 |

## Acceptance checks

- The PS2 disc's level scripts place 974 boxes: 547 of kind 0, 251 of kind 2 and 176 of kind 3, and none of kind 1.
- `level51.lua` (Desperate Dudes) places the most boxes of any script, 81; `level63.lua` (Stage 4) places one,
  `VOrphTrig_11`.
- `level2.lua` (entry 7,707) places 28 boxes; its first is `vbIntroCam_01`, kind 2, corner 453.2, 197, 12.8, size
  5.9, 64, 2.9.
- By volume, the largest box on the disc is `vbparkTurf` in `level54.lua`, size 260, 460, 70; the smallest are the
  three `vStreetStall` boxes in `level20.lua`, size 1.1, 1.1, 3.
- 211 boxes carry turn values; every other box has its footprint aligned with the x and y axes.
- In every script with boxes, the kind-0 pool it sets equals its number of kind-0 boxes.
- Every box size on the disc is above 0, and no box name is shorter than 3 or longer than 26 characters.
- Each of the three royal init scripts (`level131` to `level133`) places 9 kind-0 boxes.

## Open questions

- The file name of entry 7,105 is not recovered; its link to `level111` rests on its place in the directory and
  its contents.
- The Xbox disc's copies of these scripts were not compared.
- The unit of the positions and sizes is taken from ref-levels and is not confirmed here.
- Which gangs own each turf box is set by the chapter scripts during play and changes as a level runs; it is not
  tabulated here.
- Why some kind-2 and kind-3 pools are larger than the number of boxes the script places (for example `level9`:
  24 player boxes in a pool of 30, 7 turf boxes in a pool of 14).
