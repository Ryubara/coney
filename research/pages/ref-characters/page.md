# Character list

This reference page lists the game's 449 character entries: the 17 values each entry gives, the name stored for its
id and the changes each difficulty file makes. Every value was read from the PS2 disc (NTSC-U, `SLUS_212.15`), from
entries of `WARRIORS.WAD` located through `WARRIORS.DIR`. All the scripts named here are compiled Lua 4.0 chunks. The
speed and power classes that the entries number are on the page `ref-character-classes`.

## Disc sources

| Source | Directory entry (zero-based) | Size in bytes | Values taken from it |
| --- | ---: | ---: | --- |
| `config_preload2.lua` | 3,771 | 182,649 | The 449 character entries |
| `config_easy.lua` | 3,766 | 17,316 | 63 character entries set again for difficulty 0 |
| `config_fury.lua` | 3,767 | 16,884 | The same 63 ids for difficulty 3 |
| `config_hard.lua` | 3,768 | 17,253 | The same 63 ids for difficulty 2 |
| `config_normal.lua` | 3,769 | 17,181 | The same 63 ids for difficulty 1 |
| `config_rumble.lua` | 3,773 | 17,049 | The same 63 ids for difficulty 4 |
| `charnames.lua` | 3,720 | 9,715 | The name table: 366 names keyed by id |
| `warriors.glr` | 10,671 | 635,488 | The character list the model names are found in |

## Entry layout

Each character entry is a list of 17 values, always in this order. The column names A2 to A17 are this page's own and
give the value's position. Two values are the same in (nearly) every entry and are left out of the Characters table:
A9 is 1 in every entry except id 204, where it is 0.75, and A14 is `RangeNormal` in all 449 entries.

| Position | Column | Type | Values on the disc |
| ---: | --- | --- | --- |
| 1 | Id | integer | 449 distinct ids from 1 to 460; 10, 37, 39, 42, 44, 46, 47, 48, 188, 190 and 232 are absent |
| 2 | A2 | integer | 1 to 6: 2 in 249 entries, 4 in 116, 3 in 48, 1 in 20, 5 in 15, 6 in 1 (id 441) |
| 3 | A3 | integer | 13 values from 1 to 15 (3 and 7 do not occur); 11 in 120 entries and 4 in 118 |
| 4 | A4 | integer | A speed class number, 0 to 4 |
| 5 | A5 | integer | A power class number; 41 of the 51 classes occur, from 0 to 57 |
| 6 | A6 | integer | 34 values from 200 to 9999; 300 in 116 entries, 1800 in 50 |
| 7 | A7 | table name | One of 11 tables named `Damage...` that the same script defines (see Named tables) |
| 8 | A8 | table name | One of 50 tables named `Att_...` that the same script defines (see Named tables) |
| 9 | A9 | number | 1, or 0.75 for id 204 |
| 10 | Model | string | A model name; 404 distinct names, 43 of them given by more than one id |
| 11 | A11 | string | An object name beginning `dyn_` (87 distinct, 212 entries), or `none` (237 entries) |
| 12 | A12 | integer | 182 distinct values from 0 to 216 |
| 13 | A13 | integer | 0 in 388 entries, 1 in 61 |
| 14 | A14 | table name | `RangeNormal` in every entry |
| 15 | A15 | string | `grp_male_ped` 46, `dyn_revival` 37, `grp_female_ped` 25, `grp_bum` 12, `ngrp_male_ped` 1, `none` 328 |
| 16 | A16 | integer | 5 in 123 entries, 0 in 119, -1 in 84, 20 in 61, 50 in 36, 15 in 11, 8 in 11, 1 in 4 |
| 17 | A17 | string | `dyn_baton` 17, `dyn_hunter` 6, one each of `dyn_bowie`, `dyn_crowbar`, `dyn_lt_bat_blk_a`, `dyn_pipe_a`, `dyn_wrenchplumb`; `none` 421 |

## Named tables

The tables that A7 and A8 name, with the number of entries giving each.

| Column | Table | Entries |
| --- | --- | ---: |
| A7 | `DamageWeak` | 186 |
| A7 | `DamageNormal` | 69 |
| A7 | `DamageBoss` | 60 |
| A7 | `DamageTough` | 60 |
| A7 | `DamageWarriors` | 40 |
| A7 | `DamageCop` | 10 |
| A7 | `DamageFox` | 8 |
| A7 | `DamageRookieCop` | 8 |
| A7 | `DamageDestroyerLT` | 5 |
| A7 | `DamageGhost` | 2 |
| A7 | `DamageMoonLT` | 1 |
| A8 | `Att_Normal` | 79 |
| A8 | `Att_Weak` | 78 |
| A8 | `Att_Warrior` | 48 |
| A8 | `Att_Female` | 26 |
| A8 | `Att_SprayFemale` | 26 |
| A8 | `Att_Bum` | 18 |
| A8 | `Att_Dealer` | 15 |
| A8 | `Att_DestSol` | 13 |
| A8 | `Att_DestLT` | 12 |
| A8 | `Att_Brawler` | 11 |
| A8 | `Att_Riff` | 11 |
| A8 | `Att_Cop` | 9 |
| A8 | `Att_RailGoon` | 8 |
| A8 | `Att_SaMo` | 8 |
| A8 | `Att_Lizz` | 7 |
| A8 | `Att_HunSol` | 6 |
| A8 | `Att_HiHat` | 5 |
| A8 | `Att_Moon` | 5 |
| A8 | `Att_Drunk` | 4 |
| A8 | `Att_FurySold` | 4 |
| A8 | `Att_HurrSol` | 4 |
| A8 | `Att_JSBS` | 4 |
| A8 | `Att_OrphSol` | 4 |
| A8 | `Att_Pimp` | 4 |
| A8 | `Att_BoppSolA` | 3 |
| A8 | `Att_HunLT` | 3 |
| A8 | `Att_TurnSolA` | 3 |
| A8 | `Att_BoppSolB` | 2 |
| A8 | `Att_HurrLT` | 2 |
| A8 | `Att_KungFoo` | 2 |
| A8 | `Att_Luther` | 2 |
| A8 | `Att_Mercy` | 2 |
| A8 | `Att_OrphLT` | 2 |
| A8 | `Att_TurnSolB` | 2 |
| A8 | `Att_Virg` | 2 |
| A8 | `Att_BoppBoss` | 1 |
| A8 | `Att_BoppLT` | 1 |
| A8 | `Att_DiegoSwing` | 1 |
| A8 | `Att_FuryBoss` | 1 |
| A8 | `Att_FuryKill` | 1 |
| A8 | `Att_FuryLT` | 1 |
| A8 | `Att_Ghost` | 1 |
| A8 | `Att_HiHatBo` | 1 |
| A8 | `Att_HiHatLT` | 1 |
| A8 | `Att_JSBSLT` | 1 |
| A8 | `Att_MoonLT` | 1 |
| A8 | `Att_Riot_Cop` | 1 |
| A8 | `Att_SaMoLT` | 1 |
| A8 | `Att_TurnLT` | 1 |
| A8 | `Att_Vargas` | 1 |

## Names

Name is the string `charnames.lua` stores under the entry's id. 366 ids have a name (336 distinct strings, so some
names are shared, for example Cleon for ids 2, 3 and 189); the other 83 ids have none, shown as `-`. Every id in the
name table has a character entry.

## Characters

The 449 entries of `config_preload2.lua`, in id order. `-` in A11, A15 or A17 stands for `none`.

| Id | Name | A2 | A3 | A4 | A5 | A6 | A7 | A8 | Model | A11 | A12 | A13 | A15 | A16 | A17 |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | --- | ---: | ---: | --- | ---: | --- |
| 1 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_cl` | - | 0 | 0 | - | 0 | - |
| 2 | Cleon | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_cl_gen` | - | 0 | 0 | - | 0 | - |
| 3 | Cleon | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_cl_ghost` | - | 0 | 0 | - | 5 | - |
| 4 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_cl_chase` | - | 0 | 0 | - | 0 | - |
| 5 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_sw` | - | 1 | 0 | - | 0 | - |
| 6 | Swan | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_sw_gen` | - | 1 | 0 | - | 0 | - |
| 7 | Swan | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_sw_cv` | - | 1 | 0 | - | 0 | - |
| 8 | Swan | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_sw_ghost` | - | 1 | 0 | - | 0 | - |
| 9 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_sw_chase` | - | 1 | 0 | - | 0 | - |
| 11 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_aj` | - | 2 | 0 | - | 0 | - |
| 12 | Ajax | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_aj_gen` | - | 2 | 0 | - | 0 | - |
| 13 | Ajax | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_aj_cv` | - | 2 | 0 | - | 0 | - |
| 14 | Ajax | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_aj_ghost` | - | 2 | 0 | - | 0 | - |
| 15 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_co` | - | 3 | 0 | - | 0 | - |
| 16 | Cochise | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_co_gen` | - | 3 | 0 | - | 0 | - |
| 17 | Cochise | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_co_cv` | - | 3 | 0 | - | 0 | - |
| 18 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_cb` | `dyn_warr_cb` | 4 | 0 | - | 0 | - |
| 19 | Cowboy | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_cb_gen` | `dyn_warr_cb` | 4 | 0 | - | 0 | - |
| 20 | Cowboy | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_cb_cv` | `dyn_warr_cb` | 4 | 0 | - | 0 | - |
| 21 | - | 3 | 14 | 2 | 8 | 1800 | `DamageFox` | `Att_Warrior` | `warr_fo` | - | 5 | 0 | - | 0 | - |
| 22 | Fox | 3 | 14 | 2 | 8 | 1800 | `DamageFox` | `Att_Warrior` | `warr_fo_gen` | - | 5 | 0 | - | 0 | - |
| 23 | Fox | 3 | 14 | 2 | 8 | 1800 | `DamageFox` | `Att_Warrior` | `warr_fo_cv1` | - | 5 | 0 | - | 0 | - |
| 24 | Fox | 3 | 14 | 2 | 8 | 1800 | `DamageFox` | `Att_Warrior` | `warr_fo_cv2` | - | 5 | 0 | - | 0 | - |
| 25 | Fox | 3 | 14 | 2 | 8 | 1800 | `DamageFox` | `Att_Warrior` | `warr_fo_ghost` | - | 5 | 0 | - | 5 | - |
| 26 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_ve` | - | 6 | 0 | - | 0 | - |
| 27 | Vermin | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_ve_gen` | - | 6 | 0 | - | 0 | - |
| 28 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_ve` | - | 6 | 0 | - | 0 | - |
| 29 | Vermin | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_ve_cv` | - | 6 | 0 | - | 0 | - |
| 30 | - | 3 | 14 | 2 | 8 | 1800 | `DamageFox` | `Att_Warrior` | `warr_re` | - | 7 | 0 | - | 0 | - |
| 31 | Rembrandt | 3 | 14 | 2 | 8 | 1800 | `DamageFox` | `Att_Warrior` | `warr_re_gen` | - | 7 | 0 | - | 0 | - |
| 32 | Rembrandt | 3 | 14 | 2 | 8 | 1800 | `DamageFox` | `Att_Warrior` | `warr_re_cv` | - | 7 | 0 | - | 0 | - |
| 33 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_sn` | - | 8 | 0 | - | 0 | - |
| 34 | Snow | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_sn_gen` | - | 8 | 0 | - | 5 | - |
| 35 | Snow | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_sn_cv` | - | 8 | 0 | - | 0 | - |
| 36 | - | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_sn_chase` | - | 8 | 0 | - | 0 | - |
| 38 | Ash | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_ty` | - | 9 | 0 | - | 0 | - |
| 40 | Ash | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_ty_cv` | - | 9 | 0 | - | 0 | - |
| 41 | Jones | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_jn` | - | 13 | 0 | - | 0 | - |
| 43 | Malcolm | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_ma` | - | 10 | 0 | - | 0 | - |
| 45 | Lynx | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_ly` | - | 11 | 0 | - | 0 | - |
| 49 | Rhino | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_so_va1` | - | 10 | 0 | - | 0 | - |
| 50 | Cody | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_so_va2` | - | 14 | 0 | - | 0 | - |
| 51 | Royce | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_so_va3` | - | 13 | 0 | - | 0 | - |
| 52 | Ace | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_so_vb1` | - | 11 | 0 | - | 0 | - |
| 53 | West | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_so_vb2` | - | 12 | 0 | - | 0 | - |
| 54 | Terrance | 3 | 14 | 2 | 7 | 1800 | `DamageWarriors` | `Att_Warrior` | `warr_so_vb3` | - | 11 | 0 | - | 0 | - |
| 55 | - | 2 | 11 | 1 | 40 | 1400 | `DamageNormal` | `Att_Normal` | `warr_so_va1_hr` | - | 10 | 0 | - | 0 | - |
| 56 | - | 2 | 11 | 1 | 40 | 1400 | `DamageNormal` | `Att_Normal` | `warr_so_va2_hr` | - | 14 | 0 | - | 0 | - |
| 57 | - | 2 | 11 | 1 | 40 | 1400 | `DamageNormal` | `Att_Normal` | `warr_so_va3_hr` | - | 13 | 0 | - | 0 | - |
| 58 | - | 2 | 11 | 1 | 40 | 1400 | `DamageNormal` | `Att_Normal` | `warr_so_vb1_hr` | - | 11 | 0 | - | 0 | - |
| 59 | - | 2 | 11 | 1 | 40 | 1400 | `DamageNormal` | `Att_Normal` | `warr_so_vb2_hr` | - | 12 | 0 | - | 0 | - |
| 60 | - | 2 | 11 | 1 | 40 | 1400 | `DamageNormal` | `Att_Normal` | `warr_so_vb3_hr` | - | 11 | 0 | - | 0 | - |
| 61 | Lemmy | 2 | 12 | 1 | 1 | 650 | `DamageNormal` | `Att_DestLT` | `warr_dest_le` | - | 90 | 0 | `dyn_revival` | 50 | - |
| 62 | Cyrus | 2 | 13 | 1 | 4 | 800 | `DamageTough` | `Att_Riff` | `riff_cy` | - | 37 | 0 | - | 0 | - |
| 63 | Cyrus | 2 | 13 | 1 | 4 | 800 | `DamageTough` | `Att_Riff` | `riff_cy_ghost` | - | 37 | 0 | - | 5 | - |
| 64 | Masai | 2 | 13 | 1 | 4 | 800 | `DamageTough` | `Att_Riff` | `riff_bo_va1` | - | 38 | 0 | - | 0 | - |
| 65 | - | 2 | 13 | 1 | 4 | 800 | `DamageTough` | `Att_Riff` | `civl_hud` | - | 38 | 0 | - | 0 | - |
| 66 | Spazz | 2 | 12 | 1 | 5 | 600 | `DamageTough` | `Att_Riff` | `riff_lt` | - | 39 | 0 | `dyn_revival` | 50 | - |
| 67 | Seal | 2 | 12 | 1 | 5 | 600 | `DamageTough` | `Att_Riff` | `riff_em` | - | 39 | 0 | `dyn_revival` | 50 | - |
| 68 | Cleaver | 2 | 11 | 1 | 4 | 400 | `DamageTough` | `Att_Riff` | `riff_so_va1` | - | 40 | 0 | - | 5 | - |
| 69 | Calvin | 2 | 11 | 1 | 4 | 400 | `DamageTough` | `Att_Riff` | `riff_so_va2` | - | 41 | 0 | - | 5 | - |
| 70 | Stokely | 2 | 11 | 1 | 4 | 400 | `DamageTough` | `Att_Riff` | `riff_so_va1` | - | 40 | 0 | - | 5 | - |
| 71 | Jamal | 2 | 11 | 1 | 4 | 400 | `DamageTough` | `Att_Riff` | `riff_so_vb1` | - | 42 | 0 | - | 5 | - |
| 72 | Malachi | 2 | 11 | 1 | 4 | 400 | `DamageTough` | `Att_Riff` | `riff_so_vb2` | - | 43 | 0 | - | 5 | - |
| 73 | Jerome | 2 | 11 | 1 | 2 | 400 | `DamageNormal` | `Att_Normal` | `riff_so_vc1` | - | 40 | 0 | - | 5 | - |
| 74 | AJ | 2 | 11 | 1 | 2 | 400 | `DamageNormal` | `Att_Normal` | `riff_so_vc2` | `dyn_bm_va2` | 41 | 0 | - | 5 | - |
| 75 | Slim | 2 | 11 | 1 | 2 | 400 | `DamageNormal` | `Att_Normal` | `riff_so_vc3` | `dyn_pl_ma2` | 42 | 0 | - | 5 | - |
| 76 | Screetch | 2 | 11 | 1 | 2 | 400 | `DamageNormal` | `Att_Normal` | `riff_so_vc4` | `dyn_pl_ma3` | 43 | 0 | - | 5 | - |
| 77 | Luther | 2 | 13 | 1 | 4 | 1200 | `DamageBoss` | `Att_Luther` | `rogu_bo` | - | 44 | 0 | - | 0 | - |
| 78 | - | 2 | 13 | 1 | 15 | 1350 | `DamageBoss` | `Att_Luther` | `rogu_bo_vb` | - | 44 | 0 | - | 0 | - |
| 79 | - | 2 | 13 | 1 | 4 | 575 | `DamageBoss` | `Att_Normal` | `rogu_bo_gun` | - | 44 | 0 | - | 0 | - |
| 80 | Cropsy | 2 | 12 | 1 | 19 | 700 | `DamageNormal` | `Att_Normal` | `rogu_lt` | `dyn_rogu_lt` | 45 | 0 | `dyn_revival` | 50 | - |
| 81 | Johnny | 2 | 11 | 1 | 18 | 500 | `DamageNormal` | `Att_Normal` | `rogu_so_va1` | `dyn_rogu_so` | 46 | 0 | - | 5 | - |
| 82 | Dee Dee | 2 | 11 | 1 | 18 | 500 | `DamageNormal` | `Att_Normal` | `rogu_so_va2` | `dyn_rogu_so` | 47 | 0 | - | 5 | - |
| 83 | Tommy | 2 | 11 | 1 | 18 | 500 | `DamageNormal` | `Att_Normal` | `rogu_so_vb1` | `dyn_rogu_so` | 48 | 0 | - | 5 | - |
| 84 | Joey | 2 | 11 | 1 | 18 | 500 | `DamageNormal` | `Att_Normal` | `rogu_so_vb2` | `dyn_rogu_so` | 49 | 0 | - | 5 | - |
| 85 | - | 2 | 11 | 1 | 18 | 500 | `DamageNormal` | `Att_Normal` | `rogu_so_va1_hr` | `dyn_rogu_so` | 46 | 0 | - | 5 | - |
| 86 | - | 2 | 11 | 1 | 18 | 500 | `DamageNormal` | `Att_Normal` | `rogu_so_va2_hr` | `dyn_rogu_so` | 47 | 0 | - | 5 | - |
| 87 | - | 2 | 11 | 1 | 18 | 500 | `DamageNormal` | `Att_Normal` | `rogu_so_vb1_hr` | `dyn_rogu_so` | 48 | 0 | - | 5 | - |
| 88 | - | 2 | 11 | 1 | 18 | 500 | `DamageNormal` | `Att_Normal` | `rogu_so_vb2_hr` | `dyn_rogu_so` | 49 | 0 | - | 5 | - |
| 89 | Cobb | 2 | 13 | 1 | 27 | 2400 | `DamageGhost` | `Att_FuryBoss` | `fury_bo` | - | 50 | 0 | - | 0 | `dyn_lt_bat_blk_a` |
| 90 | Thurman | 2 | 12 | 1 | 5 | 800 | `DamageTough` | `Att_FuryLT` | `fury_lt` | - | 51 | 0 | `dyn_revival` | 50 | - |
| 91 | Ryan | 2 | 11 | 1 | 26 | 600 | `DamageTough` | `Att_FurySold` | `fury_so1` | - | 52 | 0 | - | 5 | - |
| 92 | Darien | 2 | 11 | 1 | 26 | 600 | `DamageTough` | `Att_FurySold` | `fury_so2` | - | 53 | 0 | - | 5 | - |
| 93 | Zack | 2 | 11 | 1 | 26 | 600 | `DamageTough` | `Att_FurySold` | `fury_so3` | - | 54 | 0 | - | 5 | - |
| 94 | BJ | 2 | 11 | 1 | 26 | 600 | `DamageTough` | `Att_FurySold` | `fury_so4` | - | 55 | 0 | - | 5 | - |
| 95 | - | 2 | 12 | 1 | 28 | 600 | `DamageTough` | `Att_FuryKill` | `fury_so2` | - | 51 | 0 | - | 0 | - |
| 96 | - | 2 | 13 | 1 | 24 | 2000 | `DamageBoss` | `Att_Brawler` | `turn_bd` | - | 56 | 0 | - | 0 | - |
| 97 | - | 2 | 13 | 1 | 24 | 1000 | `DamageBoss` | `Att_Brawler` | `char_wheels` | `dyn_mohawk` | 56 | 0 | - | 0 | - |
| 98 | - | 2 | 13 | 1 | 24 | 450 | `DamageBoss` | `Att_Brawler` | `turn_bd_gun` | - | 56 | 0 | - | 0 | - |
| 99 | Sid | 2 | 13 | 1 | 24 | 1000 | `DamageBoss` | `Att_Brawler` | `turn_bo` | - | 28 | 0 | - | 5 | - |
| 100 | Thrash | 2 | 12 | 1 | 25 | 550 | `DamageTough` | `Att_TurnLT` | `turn_lt` | - | 24 | 0 | `dyn_revival` | 50 | - |
| 101 | Nails | 2 | 11 | 1 | 24 | 550 | `DamageTough` | `Att_TurnSolA` | `turn_so1` | - | 16 | 0 | - | 5 | - |
| 102 | Hammer | 2 | 11 | 1 | 24 | 550 | `DamageTough` | `Att_TurnSolB` | `turn_so2` | - | 17 | 0 | - | 5 | - |
| 103 | Smash | 2 | 11 | 1 | 24 | 550 | `DamageTough` | `Att_TurnSolA` | `turn_so3` | - | 27 | 0 | - | 5 | - |
| 104 | Axel | 2 | 11 | 1 | 24 | 550 | `DamageTough` | `Att_TurnSolB` | `turn_so4` | `dyn_turn_so` | 26 | 0 | - | 5 | - |
| 105 | Skid | 2 | 11 | 1 | 24 | 550 | `DamageTough` | `Att_TurnSolA` | `turn_so5` | `dyn_turn_so` | 19 | 0 | - | 5 | - |
| 106 | Ghost | 2 | 13 | 1 | 53 | 4500 | `DamageGhost` | `Att_Ghost` | `sava_bo` | `dyn_ghost` | 51 | 0 | - | 0 | - |
| 107 | Jimmy Lo | 2 | 12 | 1 | 52 | 700 | `DamageNormal` | `Att_HunLT` | `sava_lt` | `dyn_huns_lt1` | 60 | 0 | `dyn_revival` | 50 | `dyn_hunter` |
| 108 | Tam-Tam | 2 | 12 | 1 | 52 | 700 | `DamageNormal` | `Att_HunLT` | `sava_lt1` | - | 60 | 0 | `dyn_revival` | 50 | `dyn_hunter` |
| 109 | Lingo | 2 | 12 | 1 | 52 | 700 | `DamageNormal` | `Att_HunLT` | `sava_lt2` | `dyn_huns_lt2` | 60 | 0 | `dyn_revival` | 50 | `dyn_hunter` |
| 110 | Blood Lotus | 2 | 11 | 1 | 51 | 300 | `DamageWeak` | `Att_HunSol` | `sava_so1a` | - | 61 | 0 | - | 5 | - |
| 111 | Shadow Blade | 2 | 11 | 1 | 51 | 300 | `DamageWeak` | `Att_HunSol` | `sava_so1b` | - | 62 | 0 | - | 5 | - |
| 112 | Iron Knee | 2 | 11 | 1 | 51 | 300 | `DamageWeak` | `Att_HunSol` | `sava_so2a` | - | 63 | 0 | - | 5 | - |
| 113 | Crimson Fist | 2 | 11 | 1 | 51 | 300 | `DamageWeak` | `Att_HunSol` | `sava_so2b` | - | 64 | 0 | - | 5 | - |
| 114 | Arthur | 4 | 4 | 1 | 0 | 200 | `DamageWeak` | `Att_HunSol` | `civl_monk1` | - | 58 | 0 | `dyn_revival` | 5 | - |
| 115 | Benny | 4 | 4 | 1 | 0 | 200 | `DamageWeak` | `Att_HunSol` | `civl_monk2` | - | 59 | 0 | - | 5 | - |
| 116 | Gabriel | 2 | 13 | 1 | 4 | 800 | `DamageBoss` | `Att_KungFoo` | `civl_monk_bo1` | - | 58 | 0 | - | 5 | - |
| 117 | Bruce | 2 | 13 | 1 | 4 | 800 | `DamageBoss` | `Att_KungFoo` | `civl_monk_bo2` | - | 59 | 0 | - | 5 | - |
| 118 | - | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_ct_ma1` | `dyn_tr_cw` | 168 | 0 | - | 20 | - |
| 119 | Vargas | 2 | 13 | 1 | 4 | 1400 | `DamageBoss` | `Att_Vargas` | `hurr_va` | - | 66 | 0 | - | 0 | - |
| 120 | - | 2 | 13 | 1 | 6 | 3000 | `DamageBoss` | `Att_DiegoSwing` | `hurr_de` | - | 65 | 0 | - | 0 | - |
| 121 | Apephibian | 2 | 12 | 1 | 4 | 1500 | `DamageTough` | `Att_HurrLT` | `civl_abe` | `dyn_abe` | 51 | 0 | - | 5 | - |
| 122 | Sanchez | 2 | 11 | 1 | 4 | 400 | `DamageWeak` | `Att_Normal` | `hurr_sa` | - | 67 | 0 | - | 5 | - |
| 123 | Rico | 2 | 12 | 1 | 3 | 800 | `DamageNormal` | `Att_HurrLT` | `hurr_lt` | `dyn_hurr_lt` | 68 | 0 | `dyn_revival` | 50 | `dyn_bowie` |
| 124 | Eusebio | 2 | 11 | 1 | 2 | 600 | `DamageNormal` | `Att_HurrSol` | `hurr_so1a` | `dyn_hurr_so` | 69 | 0 | - | 5 | - |
| 125 | Alejandro | 2 | 11 | 1 | 2 | 600 | `DamageNormal` | `Att_HurrSol` | `hurr_so1b` | `dyn_hurr_so` | 70 | 0 | - | 5 | - |
| 126 | Rodrigo | 2 | 11 | 1 | 2 | 600 | `DamageNormal` | `Att_HurrSol` | `hurr_so2a` | `dyn_hurr_so` | 71 | 0 | - | 5 | - |
| 127 | Benicio | 2 | 11 | 1 | 2 | 600 | `DamageNormal` | `Att_HurrSol` | `hurr_so2b` | `dyn_hurr_so` | 72 | 0 | - | 5 | - |
| 128 | Big Moe | 2 | 13 | 1 | 45 | 3500 | `DamageBoss` | `Att_BoppBoss` | `bopp_bo` | `dyn_bopp_bo` | 73 | 0 | - | 0 | - |
| 129 | Tubbz | 2 | 12 | 1 | 44 | 400 | `DamageNormal` | `Att_BoppLT` | `bopp_lt` | `dyn_bopp_lt` | 34 | 0 | `dyn_revival` | 50 | - |
| 130 | Boxcar | 2 | 11 | 1 | 43 | 250 | `DamageWeak` | `Att_BoppSolA` | `bopp_so1a` | `dyn_bopp_so` | 26 | 0 | - | 5 | - |
| 131 | TJ | 2 | 11 | 1 | 43 | 250 | `DamageWeak` | `Att_BoppSolB` | `bopp_so1b` | `dyn_bopp_so` | 27 | 0 | - | 5 | - |
| 132 | Greenback | 2 | 11 | 1 | 43 | 250 | `DamageWeak` | `Att_BoppSolA` | `bopp_so1c` | `dyn_bopp_so` | 30 | 0 | - | 5 | - |
| 133 | Nate | 2 | 11 | 1 | 43 | 250 | `DamageWeak` | `Att_BoppSolB` | `bopp_so1d` | `dyn_bopp_so` | 32 | 0 | - | 5 | - |
| 134 | Donovan | 2 | 11 | 1 | 43 | 250 | `DamageWeak` | `Att_BoppSolA` | `bopp_so2a` | `dyn_bopp_so` | 34 | 0 | - | 5 | - |
| 135 | Chatterbox | 2 | 13 | 1 | 45 | 2800 | `DamageBoss` | `Att_HiHatBo` | `hiha_bo` | - | 74 | 0 | - | 0 | - |
| 136 | Marcel | 2 | 12 | 1 | 50 | 650 | `DamageTough` | `Att_HiHatLT` | `hiha_lt` | `dyn_hiha_lt` | 76 | 0 | `dyn_revival` | 50 | `dyn_hunter` |
| 137 | Crackerjack | 2 | 12 | 1 | 3 | 650 | `DamageTough` | `Att_HiHat` | `hiha_lt_ft` | `dyn_hiha_lt_ft` | 75 | 0 | `dyn_revival` | 50 | - |
| 138 | Desmond | 2 | 11 | 1 | 49 | 500 | `DamageNormal` | `Att_HiHat` | `hiha_so_va1` | `dyn_hiha_so` | 77 | 0 | - | 5 | - |
| 139 | Jude | 2 | 11 | 1 | 49 | 500 | `DamageNormal` | `Att_HiHat` | `hiha_so_va2` | `dyn_hiha_so` | 78 | 0 | - | 5 | - |
| 140 | Etienne | 2 | 11 | 1 | 49 | 500 | `DamageNormal` | `Att_HiHat` | `hiha_so_vb1` | `dyn_hiha_so` | 79 | 0 | - | 5 | - |
| 141 | Jaques | 2 | 11 | 1 | 49 | 500 | `DamageNormal` | `Att_HiHat` | `hiha_so_vb2` | `dyn_hiha_so` | 80 | 0 | - | 5 | - |
| 142 | Edge | 2 | 13 | 1 | 4 | 800 | `DamageBoss` | `Att_Normal` | `sara_bo` | - | 81 | 0 | - | 0 | - |
| 143 | Mouse | 2 | 12 | 1 | 3 | 600 | `DamageTough` | `Att_Normal` | `sara_lt` | - | 25 | 0 | `dyn_revival` | 50 | - |
| 144 | Snakefingers | 2 | 11 | 1 | 2 | 400 | `DamageNormal` | `Att_Normal` | `sara_so1a` | - | 27 | 0 | - | 5 | - |
| 145 | Arson | 2 | 11 | 1 | 2 | 400 | `DamageNormal` | `Att_Normal` | `sara_so1b` | - | 17 | 0 | - | 5 | - |
| 146 | Petey | 2 | 11 | 1 | 2 | 400 | `DamageNormal` | `Att_Normal` | `sara_so2a` | - | 29 | 0 | - | 5 | - |
| 147 | Big Jer | 2 | 11 | 1 | 2 | 400 | `DamageNormal` | `Att_Normal` | `sara_so2b` | - | 18 | 0 | - | 5 | - |
| 148 | Jonah | 2 | 13 | 1 | 4 | 800 | `DamageBoss` | `Att_Normal` | `panz_bo` | - | 27 | 0 | - | 0 | - |
| 149 | Isaac | 2 | 12 | 1 | 3 | 800 | `DamageTough` | `Att_Normal` | `panz_lt` | `dyn_panz_lt` | 36 | 0 | `dyn_revival` | 50 | - |
| 150 | Abe | 2 | 11 | 1 | 2 | 600 | `DamageNormal` | `Att_Normal` | `panz_so_va1` | - | 30 | 0 | - | 5 | - |
| 151 | C-Style | 2 | 11 | 1 | 2 | 600 | `DamageNormal` | `Att_Normal` | `panz_so_va2` | - | 29 | 0 | - | 5 | - |
| 152 | Keenan | 2 | 11 | 1 | 2 | 600 | `DamageNormal` | `Att_Normal` | `panz_so_vb1` | `dyn_panz_so` | 28 | 0 | - | 5 | - |
| 153 | Theo | 2 | 11 | 1 | 2 | 600 | `DamageNormal` | `Att_Normal` | `panz_so_vb2` | `dyn_panz_so` | 31 | 0 | - | 5 | - |
| 154 | Knox | 2 | 13 | 1 | 4 | 800 | `DamageBoss` | `Att_Normal` | `jsbs_bo` | - | 83 | 0 | - | 0 | - |
| 155 | Harry | 2 | 12 | 1 | 3 | 600 | `DamageTough` | `Att_JSBSLT` | `jsbs_lt` | - | 23 | 0 | `dyn_revival` | 50 | `dyn_hunter` |
| 156 | Pilfer | 2 | 11 | 1 | 2 | 450 | `DamageNormal` | `Att_JSBS` | `jsbs_so1` | `dyn_pl_ma6` | 18 | 0 | - | 5 | `dyn_crowbar` |
| 157 | Royal | 2 | 11 | 1 | 2 | 450 | `DamageNormal` | `Att_JSBS` | `jsbs_so2` | `dyn_tr_ma5` | 19 | 0 | - | 5 | `dyn_pipe_a` |
| 158 | Fleece | 2 | 11 | 1 | 2 | 450 | `DamageNormal` | `Att_JSBS` | `jsbs_so3` | `dyn_sml_fro` | 16 | 0 | - | 5 | `dyn_wrenchplumb` |
| 159 | Boomstick | 2 | 11 | 1 | 2 | 450 | `DamageNormal` | `Att_JSBS` | `jsbs_so4` | - | 21 | 0 | - | 5 | `dyn_hunter` |
| 160 | Spider | 2 | 13 | 1 | 32 | 1600 | `DamageBoss` | `Att_Brawler` | `samo_bo` | `dyn_sata_bo` | 84 | 0 | - | 0 | - |
| 161 | Tiny | 2 | 13 | 1 | 32 | 2500 | `DamageTough` | `Att_SaMoLT` | `samo_lt` | - | 24 | 0 | `dyn_revival` | 50 | - |
| 162 | Goober | 2 | 11 | 1 | 31 | 500 | `DamageTough` | `Att_SaMo` | `samo_so_va1` | `dyn_samo1` | 15 | 0 | - | 5 | - |
| 163 | Kevin K | 2 | 11 | 1 | 31 | 500 | `DamageTough` | `Att_SaMo` | `samo_so_va2` | `dyn_tr_ma5` | 16 | 0 | - | 5 | - |
| 164 | Spice | 2 | 11 | 1 | 31 | 500 | `DamageTough` | `Att_SaMo` | `samo_so_va3` | `dyn_pl_ma5` | 17 | 0 | - | 5 | - |
| 165 | Ted | 2 | 11 | 1 | 31 | 500 | `DamageTough` | `Att_SaMo` | `samo_so_va4` | `dyn_samo2` | 18 | 0 | - | 5 | - |
| 166 | Pharaoh | 2 | 13 | 1 | 4 | 800 | `DamageBoss` | `Att_Normal` | `juda_bo` | - | 26 | 0 | - | 0 | - |
| 167 | Lucifer | 2 | 12 | 1 | 1 | 600 | `DamageTough` | `Att_Normal` | `juda_lt` | `dyn_pl_ma2` | 36 | 0 | `dyn_revival` | 50 | - |
| 168 | Jonah | 2 | 11 | 1 | 0 | 400 | `DamageNormal` | `Att_Normal` | `juda_so_va1` | - | 28 | 0 | - | 5 | - |
| 169 | Herod | 2 | 11 | 1 | 0 | 400 | `DamageNormal` | `Att_Normal` | `juda_so_va2` | `dyn_bm_va2` | 27 | 0 | - | 5 | - |
| 170 | Goliath | 2 | 11 | 1 | 0 | 400 | `DamageNormal` | `Att_Normal` | `juda_so_vb1` | `dyn_pl_ma2` | 33 | 0 | - | 5 | - |
| 171 | Caine | 2 | 11 | 1 | 0 | 400 | `DamageNormal` | `Att_Normal` | `juda_so_vb2` | `dyn_pl_ma3` | 32 | 0 | - | 5 | - |
| 172 | Tracer | 2 | 13 | 1 | 4 | 800 | `DamageBoss` | `Att_Normal` | `elec_bo` | - | 15 | 0 | - | 0 | - |
| 173 | Header | 2 | 12 | 1 | 3 | 750 | `DamageTough` | `Att_Normal` | `elec_lt` | - | 23 | 0 | `dyn_revival` | 50 | - |
| 174 | Beckman | 2 | 11 | 1 | 2 | 500 | `DamageNormal` | `Att_Normal` | `elec_so1` | - | 18 | 0 | - | 5 | - |
| 175 | Pelly | 2 | 11 | 1 | 2 | 500 | `DamageNormal` | `Att_Normal` | `elec_so2` | `dyn_sml_fro` | 19 | 0 | - | 5 | - |
| 176 | Charlton | 2 | 11 | 1 | 2 | 500 | `DamageNormal` | `Att_Normal` | `elec_so3` | `dyn_tr_ma5` | 21 | 0 | - | 5 | - |
| 177 | Zidane | 2 | 11 | 1 | 2 | 500 | `DamageNormal` | `Att_Normal` | `elec_so4` | `dyn_pl_ma6` | 22 | 0 | - | 5 | - |
| 178 | Virgil | 2 | 13 | 1 | 48 | 5000 | `DamageBoss` | `Att_Virg` | `dest_bo` | - | 87 | 0 | - | 0 | - |
| 179 | Virgil | 2 | 13 | 1 | 48 | 1000 | `DamageBoss` | `Att_Virg` | `dest_bo_tg` | - | 87 | 0 | - | 0 | - |
| 180 | Crusty | 2 | 12 | 1 | 47 | 550 | `DamageDestroyerLT` | `Att_DestLT` | `dest_lt` | `dyn_dest_lta` | 35 | 0 | `dyn_revival` | 50 | - |
| 181 | - | 2 | 12 | 1 | 47 | 550 | `DamageDestroyerLT` | `Att_DestLT` | `dest_lt_hr` | `dyn_dest_lta` | 35 | 0 | `dyn_revival` | 50 | - |
| 182 | Chase | 2 | 12 | 1 | 47 | 550 | `DamageDestroyerLT` | `Att_DestLT` | `dest_lt_v1` | `dyn_dest_lta` | 36 | 0 | `dyn_revival` | 50 | - |
| 183 | Skooly | 2 | 12 | 1 | 47 | 550 | `DamageDestroyerLT` | `Att_DestLT` | `dest_lt_v2` | `dyn_dest_lta` | 23 | 0 | `dyn_revival` | 50 | - |
| 184 | Devon | 2 | 12 | 1 | 47 | 550 | `DamageDestroyerLT` | `Att_DestLT` | `dest_lt_v3` | `dyn_dest_lta` | 25 | 0 | `dyn_revival` | 50 | - |
| 185 | Beansie | 2 | 12 | 1 | 1 | 650 | `DamageNormal` | `Att_DestLT` | `dest_lt_be` | `dyn_beansie` | 89 | 0 | `dyn_revival` | 50 | - |
| 186 | Lemmy | 2 | 12 | 1 | 1 | 650 | `DamageNormal` | `Att_DestLT` | `dest_lt_le` | - | 90 | 0 | `dyn_revival` | 50 | - |
| 187 | Lemmy | 2 | 12 | 1 | 1 | 650 | `DamageNormal` | `Att_DestLT` | `dest_le_bare` | - | 90 | 0 | `dyn_revival` | 50 | - |
| 189 | Cleon | 3 | 14 | 2 | 6 | 1800 | `DamageWarriors` | `Att_Warrior` | `dest_cl` | `dyn_dest_lt` | 0 | 0 | - | 5 | - |
| 191 | Vermin | 3 | 14 | 2 | 6 | 1800 | `DamageWarriors` | `Att_Warrior` | `dest_ve` | - | 6 | 0 | - | 5 | - |
| 192 | Bull | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_DestSol` | `dest_so1a` | `dyn_tr_ma5` | 15 | 0 | - | 5 | - |
| 193 | - | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_DestSol` | `dest_so1a_hr` | `dyn_tr_ma5` | 15 | 0 | - | 5 | - |
| 194 | Gnat | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_DestSol` | `dest_so1b` | `dyn_pl_ma6` | 19 | 0 | - | 5 | - |
| 195 | - | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_DestSol` | `dest_so1b_hr` | `dyn_pl_ma6` | 19 | 0 | - | 5 | - |
| 196 | LC | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_DestSol` | `dest_so1c` | - | 88 | 0 | - | 5 | - |
| 197 | Maggot | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_DestSol` | `dest_so2a` | - | 29 | 0 | - | 5 | - |
| 198 | - | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_DestSol` | `dest_so2a_hr` | - | 29 | 0 | - | 5 | - |
| 199 | Mainline | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_DestSol` | `dest_so2b` | `dyn_bm_va2` | 33 | 0 | - | 5 | - |
| 200 | - | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_DestSol` | `dest_so2b_hr` | `dyn_bm_va2` | 33 | 0 | - | 5 | - |
| 201 | - | 2 | 6 | 4 | 39 | 400 | `DamageWeak` | `Att_Drunk` | `dest_so1a_drnk` | `dyn_tr_ma5` | 102 | 0 | - | 5 | - |
| 202 | - | 2 | 6 | 4 | 39 | 400 | `DamageWeak` | `Att_Drunk` | `dest_so1b_drnk` | `dyn_pl_ma6` | 103 | 0 | - | 5 | - |
| 203 | - | 2 | 6 | 4 | 39 | 400 | `DamageWeak` | `Att_Drunk` | `dest_so2b_drnk` | `dyn_bm_va2` | 104 | 0 | - | 5 | - |
| 204 | - | 4 | 6 | 4 | 39 | 400 | `DamageWeak` | `Att_Drunk` | `dest_so2a_drnk` | - | 105 | 0 | - | 5 | - |
| 205 | LaFonda | 2 | 4 | 1 | 0 | 275 | `DamageWeak` | `Att_Female` | `char_lcgf` | - | 161 | 1 | - | 20 | - |
| 206 | LaFonda | 2 | 4 | 1 | 0 | 275 | `DamageWeak` | `Att_Female` | `char_lcgf_vest` | - | 161 | 1 | - | 20 | - |
| 207 | - | 2 | 12 | 1 | 55 | 650 | `DamageTough` | `Att_DestLT` | `dest_lt` | `dyn_dest_lta` | 35 | 0 | `dyn_revival` | 50 | - |
| 208 | - | 2 | 11 | 1 | 2 | 450 | `DamageNormal` | `Att_DestSol` | `dest_so1a` | `dyn_tr_ma5` | 15 | 0 | - | 5 | - |
| 209 | - | 2 | 11 | 1 | 2 | 450 | `DamageNormal` | `Att_DestSol` | `dest_so1b` | `dyn_pl_ma6` | 17 | 0 | - | 5 | - |
| 210 | - | 2 | 11 | 1 | 2 | 450 | `DamageNormal` | `Att_DestSol` | `dest_so2a` | - | 29 | 0 | - | 5 | - |
| 211 | - | 2 | 11 | 1 | 2 | 450 | `DamageNormal` | `Att_DestSol` | `dest_so2b` | `dyn_bm_va2` | 33 | 0 | - | 5 | - |
| 212 | - | 2 | 12 | 1 | 4 | 700 | `DamageTough` | `Att_DestLT` | `dest_lt_le` | - | 90 | 0 | `dyn_revival` | 50 | - |
| 213 | - | 2 | 12 | 1 | 4 | 700 | `DamageTough` | `Att_DestLT` | `dest_lt_be` | `dyn_beansie` | 89 | 0 | `dyn_revival` | 50 | - |
| 214 | Bullet | 2 | 13 | 1 | 4 | 800 | `DamageBoss` | `Att_Moon` | `moon_bo` | - | 20 | 0 | - | 0 | - |
| 215 | Deke | 2 | 12 | 1 | 1 | 600 | `DamageMoonLT` | `Att_MoonLT` | `moon_lt` | - | 36 | 0 | `dyn_revival` | 50 | - |
| 216 | Glenn | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_Moon` | `moon_so1a` | `dyn_pl_ma2` | 28 | 0 | - | 5 | - |
| 217 | Cooper | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_Moon` | `moon_so1b` | `dyn_pl_ma3` | 31 | 0 | - | 5 | - |
| 218 | Gus | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_Moon` | `moon_so2a` | - | 15 | 0 | - | 5 | - |
| 219 | Luna 51 | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_Moon` | `moon_so2b` | `dyn_bn_ma1` | 20 | 0 | - | 5 | - |
| 220 | Sully | 2 | 13 | 1 | 4 | 900 | `DamageBoss` | `Att_Weak` | `orph_bo_va1` | - | 92 | 0 | - | 0 | - |
| 221 | Mercy | 2 | 11 | 1 | 57 | 400 | `DamageWeak` | `Att_Mercy` | `orph_me` | - | 93 | 1 | - | 0 | - |
| 222 | Mercy | 2 | 11 | 1 | 57 | 400 | `DamageWeak` | `Att_Mercy` | `orph_me_vb` | - | 93 | 1 | - | 0 | - |
| 223 | Jesse | 2 | 12 | 1 | 39 | 600 | `DamageNormal` | `Att_OrphLT` | `orph_lt_va` | - | 94 | 0 | `dyn_revival` | 50 | - |
| 224 | Monk | 2 | 12 | 1 | 1 | 600 | `DamageNormal` | `Att_OrphLT` | `orph_lt_vb` | - | 25 | 0 | `dyn_revival` | 50 | - |
| 225 | Hutch | 2 | 11 | 1 | 16 | 500 | `DamageWeak` | `Att_OrphSol` | `orph_so_va1` | - | 19 | 0 | - | 1 | - |
| 226 | Pike | 2 | 11 | 1 | 16 | 500 | `DamageWeak` | `Att_OrphSol` | `orph_so_va2` | `dyn_pl_ma6` | 20 | 0 | - | 1 | - |
| 227 | Bugs | 2 | 11 | 1 | 16 | 500 | `DamageWeak` | `Att_OrphSol` | `orph_so_vb1` | `dyn_sml_fro` | 21 | 0 | - | 1 | - |
| 228 | McGee | 2 | 11 | 1 | 16 | 500 | `DamageWeak` | `Att_OrphSol` | `orph_so_vb2` | `dyn_tr_ma5` | 22 | 0 | - | 1 | - |
| 229 | Vance | 2 | 13 | 1 | 22 | 1800 | `DamageBoss` | `Att_Brawler` | `punk_bo` | - | 15 | 0 | - | 0 | - |
| 230 | - | 2 | 13 | 1 | 22 | 1800 | `DamageBoss` | `Att_Brawler` | `punk_bo_vb` | - | 15 | 0 | - | 0 | - |
| 231 | Ed | 2 | 12 | 1 | 23 | 1000 | `DamageTough` | `Att_Brawler` | `punk_lt` | `dyn_pl_ma6` | 25 | 0 | `dyn_revival` | 50 | - |
| 233 | Hog | 2 | 11 | 1 | 22 | 700 | `DamageTough` | `Att_SaMo` | `punk_so1a` | `dyn_tr_ma5` | 16 | 0 | - | 5 | - |
| 234 | Hornet | 2 | 11 | 1 | 22 | 700 | `DamageTough` | `Att_SaMo` | `punk_so1b` | `dyn_tr_ma2` | 18 | 0 | - | 5 | - |
| 235 | Maurice | 2 | 11 | 1 | 22 | 700 | `DamageTough` | `Att_SaMo` | `punk_so2a` | - | 32 | 0 | - | 5 | - |
| 236 | Lumpy | 2 | 11 | 1 | 22 | 700 | `DamageTough` | `Att_SaMo` | `punk_so2b` | `dyn_tr_ma5` | 22 | 0 | - | 5 | - |
| 237 | Starr | 2 | 13 | 1 | 4 | 600 | `DamageBoss` | `Att_Lizz` | `lizz_bo` | - | 96 | 1 | - | 0 | - |
| 238 | Sarah | 2 | 12 | 1 | 20 | 300 | `DamageNormal` | `Att_Lizz` | `lizz_lt` | - | 97 | 1 | `dyn_revival` | 50 | - |
| 239 | Roxanna | 2 | 11 | 1 | 20 | 300 | `DamageWeak` | `Att_Lizz` | `lizz_so_va1` | - | 98 | 1 | - | 5 | - |
| 240 | Dana | 2 | 11 | 1 | 20 | 300 | `DamageWeak` | `Att_Lizz` | `lizz_so_va2` | - | 99 | 1 | - | 5 | - |
| 241 | Sheera | 2 | 11 | 1 | 20 | 300 | `DamageWeak` | `Att_Lizz` | `lizz_so_va3` | - | 97 | 1 | - | 5 | - |
| 242 | Hollie | 2 | 11 | 1 | 20 | 300 | `DamageWeak` | `Att_Lizz` | `lizz_so_vb1` | - | 100 | 1 | - | 5 | - |
| 243 | Alice | 2 | 11 | 1 | 20 | 300 | `DamageWeak` | `Att_Lizz` | `lizz_so_vb2` | - | 101 | 1 | - | 5 | - |
| 244 | - | 2 | 12 | 1 | 1 | 600 | `DamageNormal` | `Att_Weak` | `civl_hud` | - | 20 | 0 | `dyn_revival` | 50 | - |
| 245 | Stoich | 1 | 10 | 3 | 9 | 700 | `DamageRookieCop` | `Att_RailGoon` | `cops_ty_va1` | `dyn_cops_vc` | 116 | 0 | - | 0 | `dyn_baton` |
| 246 | Cedric | 1 | 10 | 3 | 9 | 700 | `DamageRookieCop` | `Att_RailGoon` | `cops_ty_va2` | `dyn_cops_vc` | 117 | 0 | - | 0 | `dyn_baton` |
| 247 | Pucci | 1 | 10 | 3 | 9 | 700 | `DamageRookieCop` | `Att_RailGoon` | `cops_ty_vb1` | `dyn_cops_vc` | 118 | 0 | - | 0 | `dyn_baton` |
| 248 | Curt | 1 | 10 | 3 | 9 | 700 | `DamageRookieCop` | `Att_RailGoon` | `cops_ty_vb2` | `dyn_cops_vc` | 119 | 0 | - | 0 | `dyn_baton` |
| 249 | - | 1 | 10 | 3 | 9 | 1000 | `DamageBoss` | `Att_RailGoon` | `cops_ty_va1` | `dyn_cops_vc` | 116 | 0 | - | 0 | `dyn_baton` |
| 250 | - | 1 | 10 | 3 | 9 | 1000 | `DamageBoss` | `Att_RailGoon` | `cops_ty_va2` | `dyn_cops_vc` | 117 | 0 | - | 0 | `dyn_baton` |
| 251 | - | 1 | 10 | 3 | 9 | 1000 | `DamageBoss` | `Att_RailGoon` | `cops_ty_vb1` | `dyn_cops_vc` | 118 | 0 | - | 0 | `dyn_baton` |
| 252 | - | 1 | 10 | 3 | 9 | 1000 | `DamageBoss` | `Att_RailGoon` | `cops_ty_vb2` | `dyn_cops_vc` | 119 | 0 | - | 0 | `dyn_baton` |
| 253 | Dubrowski | 1 | 9 | 3 | 9 | 1000 | `DamageCop` | `Att_Cop` | `cops_va1` | `dyn_cops_va` | 120 | 0 | - | 0 | `dyn_baton` |
| 254 | Dawson | 1 | 9 | 3 | 9 | 1000 | `DamageCop` | `Att_Cop` | `cops_va2` | `dyn_cops_va` | 121 | 0 | - | 0 | `dyn_baton` |
| 255 | Austin | 1 | 9 | 3 | 9 | 1000 | `DamageCop` | `Att_Cop` | `cops_va3` | `dyn_cops_va` | 124 | 0 | - | 0 | `dyn_baton` |
| 256 | Riley | 1 | 9 | 3 | 9 | 1000 | `DamageCop` | `Att_Cop` | `cops_vb1` | `dyn_cops_va` | 122 | 0 | - | 0 | `dyn_baton` |
| 257 | Wyatt | 1 | 9 | 3 | 9 | 1000 | `DamageCop` | `Att_Cop` | `cops_vb2` | `dyn_cops_va` | 123 | 0 | - | 0 | `dyn_baton` |
| 258 | Jackson | 1 | 9 | 3 | 9 | 1000 | `DamageCop` | `Att_Cop` | `cops_vb3` | `dyn_cops_va` | 124 | 0 | - | 0 | `dyn_baton` |
| 259 | Nic | 1 | 9 | 3 | 13 | 1000 | `DamageCop` | `Att_Riot_Cop` | `cops_vc1` | `dyn_riot_visor` | 126 | 0 | - | 0 | `dyn_baton` |
| 260 | Chloe | 1 | 9 | 3 | 9 | 1000 | `DamageCop` | `Att_Cop` | `cops_uc1` | - | 127 | 1 | - | 0 | - |
| 261 | Garrison | 1 | 9 | 3 | 9 | 1000 | `DamageCop` | `Att_Normal` | `cops_ia1` | - | 128 | 0 | - | 0 | - |
| 262 | Harrison | 1 | 9 | 3 | 9 | 1000 | `DamageCop` | `Att_Normal` | `cops_ia2` | - | 129 | 0 | - | 0 | - |
| 263 | Gavin | 1 | 10 | 3 | 9 | 700 | `DamageRookieCop` | `Att_Cop` | `cops_vd1` | `dyn_cops_va` | 133 | 0 | - | 0 | `dyn_baton` |
| 264 | Willis | 1 | 10 | 3 | 9 | 700 | `DamageRookieCop` | `Att_Cop` | `cops_vd2` | `dyn_cops_va` | 134 | 0 | - | 0 | `dyn_baton` |
| 265 | - | 2 | 9 | 3 | 9 | 800 | `DamageBoss` | `Att_Normal` | `civl_sg1` | `dyn_cops_vb` | 120 | 0 | - | 0 | - |
| 266 | - | 2 | 9 | 3 | 9 | 800 | `DamageBoss` | `Att_Normal` | `cops_vb1` | `dyn_cops_va` | 121 | 0 | - | 0 | - |
| 267 | - | 2 | 9 | 3 | 9 | 800 | `DamageBoss` | `Att_Normal` | `cops_vc1` | `dyn_riot_visor` | 122 | 0 | - | 0 | - |
| 268 | Debbie | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Female` | `char_db1` | - | 176 | 1 | - | 20 | - |
| 269 | Debbie | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Female` | `char_db2` | - | 176 | 1 | - | 20 | - |
| 270 | Old Crazy | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_va1` | `dyn_bm_va1` | 143 | 0 | `grp_bum` | -1 | - |
| 271 | Rossco | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_va2` | `dyn_bm_va2` | 147 | 0 | `grp_bum` | -1 | - |
| 272 | Peaches | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb1` | `dyn_bm_va2` | 146 | 0 | `grp_bum` | -1 | - |
| 273 | Stinky | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb2` | `dyn_bm_vb2` | 144 | 0 | `grp_bum` | -1 | - |
| 274 | Rusty | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb3` | `dyn_bm_vb3` | 147 | 0 | `grp_bum` | -1 | - |
| 275 | Rudy | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb4` | `dyn_bm_vb4` | 142 | 0 | `grp_bum` | -1 | - |
| 276 | Speedy | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb5` | `dyn_bm_vb5` | 145 | 0 | `grp_bum` | -1 | - |
| 277 | Perry | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb6` | `dyn_bm_vb6` | 146 | 0 | `grp_bum` | -1 | - |
| 278 | Socks | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb7` | `dyn_bm_vb7` | 145 | 0 | `grp_bum` | -1 | - |
| 279 | Patrick | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb8` | `dyn_bm_vb8` | 145 | 0 | `grp_bum` | -1 | - |
| 280 | 8 Ball | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb9` | `dyn_bm_vb9` | 141 | 0 | `grp_bum` | -1 | - |
| 281 | Ron Weed | 4 | 6 | 4 | 39 | 300 | `DamageWeak` | `Att_Bum` | `civl_bm_vb11` | `dyn_bm_vb1` | 141 | 0 | `grp_bum` | -1 | - |
| 282 | - | 2 | 6 | 0 | 0 | 350 | `DamageWeak` | `Att_Bum` | `civl_bm_va2` | `dyn_bm_va2` | 147 | 0 | - | 5 | - |
| 283 | - | 2 | 6 | 0 | 0 | 350 | `DamageWeak` | `Att_Bum` | `civl_bm_vb5` | `dyn_bm_vb5` | 143 | 0 | - | 5 | - |
| 284 | - | 2 | 6 | 0 | 0 | 350 | `DamageWeak` | `Att_Bum` | `civl_bm_vb8` | `dyn_bm_vb8` | 146 | 0 | - | 5 | - |
| 285 | - | 2 | 6 | 0 | 0 | 350 | `DamageWeak` | `Att_Bum` | `civl_bm_va1` | `dyn_bm_va1` | 143 | 0 | - | 5 | - |
| 286 | - | 2 | 6 | 0 | 0 | 350 | `DamageWeak` | `Att_Bum` | `civl_bm_vb2` | `dyn_bm_vb2` | 141 | 0 | - | 5 | - |
| 287 | - | 2 | 6 | 0 | 0 | 350 | `DamageWeak` | `Att_Bum` | `civl_bm_vb11` | `dyn_bm_vb1` | 142 | 0 | - | 5 | - |
| 288 | Tuttle | 4 | 11 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_gk` | - | 180 | 0 | - | 20 | - |
| 289 | Rosie | 2 | 8 | 1 | 0 | 400 | `DamageWeak` | `Att_Female` | `civl_co_ic` | - | 154 | 1 | - | 8 | - |
| 290 | Omar | 2 | 8 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_co_sh` | `dyn_tr_ma2` | 159 | 0 | `grp_male_ped` | -1 | - |
| 291 | - | 2 | 8 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_co_di` | `dyn_butcher` | 180 | 0 | `grp_male_ped` | -1 | - |
| 292 | Donut Dan | 2 | 8 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_co_do` | `dyn_tr_ma2` | 152 | 0 | `grp_male_ped` | -1 | - |
| 293 | Roy | 4 | 4 | 1 | 4 | 700 | `DamageTough` | `Att_Normal` | `civl_hl_b1` | - | 158 | 0 | `grp_male_ped` | -1 | - |
| 294 | Booker | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_b2` | - | 159 | 0 | `grp_male_ped` | -1 | - |
| 295 | Delton | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_b3` | - | 160 | 0 | `grp_male_ped` | -1 | - |
| 296 | Luke | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_cw_ma1` | `dyn_cw_m2` | 151 | 0 | - | 20 | - |
| 297 | Sam | 4 | 5 | 1 | 2 | 600 | `DamageTough` | `Att_Normal` | `civl_cw_ma2` | `dyn_cw_m1` | 150 | 0 | - | 20 | - |
| 298 | Dylan | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_cw_mb1` | `dyn_cw_m2` | 183 | 0 | - | 20 | - |
| 299 | Archie | 4 | 5 | 1 | 2 | 600 | `DamageTough` | `Att_Normal` | `civl_cw_mb2` | `dyn_cw_m1` | 182 | 0 | - | 20 | - |
| 300 | Herb | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_co_lf1` | - | 150 | 0 | - | 20 | - |
| 301 | Clinton | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_co_lf2` | - | 150 | 0 | - | 20 | - |
| 302 | Erik | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_co_lf3` | - | 150 | 0 | - | 20 | - |
| 303 | Charlie | 2 | 4 | 1 | 0 | 1000 | `DamageWeak` | `Att_Weak` | `civl_ch` | `dyn_ch` | 186 | 0 | - | 20 | - |
| 304 | Skinny Pete | 2 | 4 | 1 | 0 | 1000 | `DamageWeak` | `Att_Weak` | `civl_sk_pe` | `dyn_sk_pe` | 187 | 0 | - | 20 | - |
| 305 | Twiggy | 2 | 4 | 1 | 0 | 1000 | `DamageWeak` | `Att_Weak` | `civl_tw` | `dyn_tw` | 189 | 0 | - | 20 | - |
| 306 | Mindy | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_csg` | `dyn_cgs` | 155 | 1 | - | 8 | - |
| 307 | Oates | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_sp_ju` | - | 185 | 0 | - | 20 | - |
| 308 | Steve | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_prom_ma` | `dyn_pl_ma3` | 152 | 0 | - | 20 | - |
| 309 | Mike | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_prom_mb` | `dyn_pl_ma5` | 151 | 0 | - | 20 | - |
| 310 | Candice | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_prom_fa` | `dyn_prom_fa` | 155 | 1 | - | 8 | - |
| 311 | Kimmy | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_prom_fb` | `dyn_tr_fa1` | 154 | 1 | - | 8 | - |
| 312 | Gypsy | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_gypsy` | `dyn_gypsy` | 176 | 1 | - | 8 | - |
| 313 | Sharla | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `bopp_fa1` | - | 155 | 1 | - | 8 | - |
| 314 | Shandra | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `bopp_fa1` | - | 154 | 1 | - | 8 | - |
| 315 | Hot Dog Jimmy | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hd` | `dyn_pl_ma9` | 171 | 0 | - | 20 | - |
| 316 | Dick | 2 | 11 | 0 | 4 | 800 | `DamageBoss` | `Att_Normal` | `civl_hl_br1` | - | 159 | 0 | - | 20 | - |
| 317 | Winston | 2 | 11 | 0 | 4 | 800 | `DamageBoss` | `Att_Normal` | `civl_hl_br2` | - | 156 | 0 | - | 20 | - |
| 318 | Colt | 2 | 11 | 0 | 4 | 800 | `DamageBoss` | `Att_Normal` | `civl_hl_br3` | - | 183 | 0 | - | 20 | - |
| 319 | Ali | 2 | 11 | 0 | 4 | 800 | `DamageBoss` | `Att_Normal` | `civl_hl_br4` | - | 158 | 0 | - | 20 | - |
| 320 | Barry | 2 | 11 | 0 | 4 | 800 | `DamageBoss` | `Att_Normal` | `civl_hl_br5` | - | 183 | 0 | - | 20 | - |
| 321 | Gideon | 2 | 11 | 0 | 4 | 800 | `DamageBoss` | `Att_Normal` | `civl_gc_bt` | - | 158 | 0 | - | 20 | - |
| 322 | Randy | 2 | 11 | 0 | 4 | 800 | `DamageBoss` | `Att_Normal` | `civl_hl_dm1` | - | 183 | 0 | - | 20 | - |
| 323 | Kip | 2 | 11 | 0 | 4 | 800 | `DamageBoss` | `Att_Normal` | `civl_hl_dm2` | - | 158 | 0 | - | 20 | - |
| 324 | Lyle | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_ma1` | - | 159 | 0 | `grp_male_ped` | -1 | - |
| 325 | King | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_ma2` | - | 160 | 0 | `grp_male_ped` | -1 | - |
| 326 | Eldon | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_ma3` | - | 158 | 0 | `grp_male_ped` | -1 | - |
| 327 | Denver | 4 | 5 | 1 | 2 | 550 | `DamageTough` | `Att_Normal` | `civl_hl_ma4` | - | 156 | 0 | `grp_male_ped` | -1 | - |
| 328 | Gil | 4 | 5 | 1 | 2 | 700 | `DamageTough` | `Att_Normal` | `civl_hl_ma5` | - | 183 | 0 | `grp_male_ped` | -1 | - |
| 329 | Connie | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_hl_fb1` | - | 163 | 1 | - | 8 | - |
| 330 | Lisa | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_hl_fb2` | - | 161 | 1 | - | 8 | - |
| 331 | Tonia | 4 | 2 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_hl_ho1` | - | 176 | 1 | - | 15 | - |
| 332 | Shandra | 4 | 2 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_hl_ho2` | `dyn_tr_fa4` | 177 | 1 | - | 15 | - |
| 333 | Mary Janny | 2 | 2 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `hook_01` | - | 179 | 1 | - | 15 | - |
| 334 | Tara | 2 | 2 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `hook_03` | - | 176 | 1 | - | 15 | - |
| 335 | - | 2 | 2 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_hl_ho2` | - | 176 | 1 | - | 15 | - |
| 336 | - | 2 | 2 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_hud` | - | 176 | 1 | - | 15 | - |
| 337 | Trina | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_hl_tr` | - | 161 | 1 | - | 8 | - |
| 338 | Diana | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_hl_fc1` | - | 161 | 1 | `grp_female_ped` | -1 | - |
| 339 | Carla | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_hl_fc2` | - | 176 | 1 | `grp_female_ped` | -1 | - |
| 340 | Beth | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_hl_fc3` | - | 162 | 1 | `grp_female_ped` | -1 | - |
| 341 | Yolanda | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_hl_fc4` | - | 177 | 1 | `grp_female_ped` | -1 | - |
| 342 | Sharla | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Female` | `civl_hl_fc5` | - | 163 | 1 | `grp_female_ped` | -1 | - |
| 343 | Pablo | 2 | 8 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_bt1` | - | 170 | 0 | - | 20 | - |
| 344 | Roger | 2 | 8 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_bt1` | - | 159 | 0 | - | 20 | - |
| 345 | Dallas | 2 | 8 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_bt2` | - | 159 | 0 | - | 20 | - |
| 346 | Leroy | 2 | 8 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_bt3` | - | 159 | 0 | - | 20 | - |
| 347 | Lester | 2 | 8 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_bt4` | - | 158 | 0 | - | 20 | - |
| 348 | Travis | 2 | 11 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_bt5` | - | 158 | 0 | - | 20 | - |
| 349 | - | 2 | 8 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_hl_bt5` | - | 158 | 0 | - | 20 | - |
| 350 | Saddler | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_hl_dj1` | `dyn_civl_a` | 96 | 0 | - | 20 | - |
| 351 | - | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Female` | `dj` | - | 96 | 1 | - | 8 | - |
| 352 | ? | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Female` | `civl_hud` | - | 96 | 0 | - | 20 | - |
| 353 | Sugar | 2 | 4 | 1 | 0 | 275 | `DamageWeak` | `Att_Female` | `civl_hl_st1a` | - | 177 | 1 | - | 15 | - |
| 354 | Simone | 2 | 4 | 1 | 0 | 275 | `DamageWeak` | `Att_Female` | `civl_hl_st1b` | - | 177 | 1 | - | 15 | - |
| 355 | Cleo | 2 | 4 | 1 | 0 | 275 | `DamageWeak` | `Att_Female` | `civl_hl_st2a` | - | 161 | 1 | - | 15 | - |
| 356 | Lilly | 2 | 4 | 1 | 0 | 275 | `DamageWeak` | `Att_Female` | `civl_hl_st2b` | - | 161 | 1 | - | 15 | - |
| 357 | Isis | 2 | 4 | 1 | 0 | 275 | `DamageWeak` | `Att_Female` | `civl_hl_st3a` | - | 162 | 1 | - | 15 | - |
| 358 | Eddie | 2 | 11 | 3 | 4 | 800 | `DamageRookieCop` | `Att_Normal` | `civl_sg1` | `dyn_cops_vb` | 180 | 0 | - | 20 | - |
| 359 | Henry | 2 | 11 | 3 | 4 | 800 | `DamageRookieCop` | `Att_Normal` | `civl_sg2` | `dyn_cops_vb` | 181 | 0 | - | 20 | - |
| 360 | Cameron | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_pl_ma1` | `dyn_pl_ma1` | 160 | 0 | `grp_male_ped` | -1 | - |
| 361 | Clifton | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_pl_ma2` | - | 183 | 0 | `grp_male_ped` | -1 | - |
| 362 | Nelson | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_pl_ma3` | `dyn_pl_ma3` | 158 | 0 | `grp_male_ped` | -1 | - |
| 363 | Heath | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_pl_ma5` | `dyn_pl_ma5` | 171 | 0 | `grp_male_ped` | -1 | - |
| 364 | Darryl | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_pl_ma6` | `dyn_pl_ma5` | 151 | 0 | `grp_male_ped` | -1 | - |
| 365 | Tomas | 4 | 5 | 1 | 2 | 700 | `DamageTough` | `Att_Normal` | `civl_pl_ma8` | `dyn_pl_ma7` | 150 | 0 | `grp_male_ped` | -1 | - |
| 366 | Nikki | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_pl_fa1` | - | 155 | 1 | `grp_female_ped` | -1 | - |
| 367 | Samantha | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_pl_fa2` | - | 154 | 1 | `grp_female_ped` | -1 | - |
| 368 | Julie | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_bn_fa1` | `dyn_bn_fa1` | 155 | 1 | `grp_female_ped` | -1 | - |
| 369 | Karen | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_bn_fa2` | `dyn_bn_fa2` | 154 | 1 | `grp_female_ped` | -1 | - |
| 370 | Jacklyn | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_bn_fa3` | - | 174 | 1 | `grp_female_ped` | -1 | - |
| 371 | Becky | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_bn_fa4` | - | 162 | 1 | `grp_female_ped` | -1 | - |
| 372 | Danny | 4 | 4 | 1 | 2 | 750 | `DamageTough` | `Att_Normal` | `civl_bn_ma1` | `dyn_bn_ma1` | 150 | 0 | `grp_male_ped` | -1 | - |
| 373 | Gabe | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_bn_ma2` | - | 151 | 0 | `grp_male_ped` | -1 | - |
| 374 | Taylor | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_bn_ma3` | `dyn_bn_ma1` | 171 | 0 | `grp_male_ped` | -1 | - |
| 375 | Arnold | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_bn_ma4` | `dyn_pl_ma2` | 160 | 0 | `grp_male_ped` | -1 | - |
| 376 | Donald | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_bn_mb1` | - | 152 | 0 | `grp_male_ped` | -1 | - |
| 377 | - | 2 | 4 | 1 | 4 | 1000 | `DamageTough` | `Att_Normal` | `civl_bn_ma1` | `dyn_bn_ma1` | 150 | 0 | - | 20 | - |
| 378 | - | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_bn_ma2` | - | 151 | 0 | - | 20 | - |
| 379 | - | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_bn_ma3` | `dyn_bn_ma1` | 171 | 0 | - | 20 | - |
| 380 | Maria | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_eh_fa1` | `dyn_eh_fa1` | 174 | 1 | `grp_female_ped` | -1 | - |
| 381 | Rosa | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_eh_fa2` | `dyn_eh_fa2` | 175 | 1 | `grp_female_ped` | -1 | - |
| 382 | Virginia | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_eh_fa3` | `dyn_eh_fa3` | 173 | 1 | `grp_female_ped` | -1 | - |
| 383 | Pablo | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_eh_ma1` | - | 171 | 0 | `grp_male_ped` | -1 | - |
| 384 | Jose | 4 | 4 | 1 | 0 | 301 | `DamageWeak` | `Att_Weak` | `civl_eh_ma2` | `dyn_eh_ma2` | 170 | 0 | `grp_male_ped` | -1 | - |
| 385 | Carloz | 4 | 5 | 1 | 2 | 600 | `DamageTough` | `Att_Normal` | `civl_eh_ma3` | `dyn_eh_ma3` | 170 | 0 | `grp_male_ped` | -1 | - |
| 386 | Chico | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_eh_ma4` | `dyn_eh_ma4` | 160 | 0 | `grp_male_ped` | -1 | - |
| 387 | Fernando | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_eh_ma5` | `dyn_eh_ma5` | 172 | 0 | `grp_male_ped` | -1 | - |
| 388 | Roderick | 4 | 4 | 1 | 0 | 200 | `DamageWeak` | `Att_Weak` | `civl_tr_cw` | `dyn_tr_cw` | 180 | 0 | - | 20 | - |
| 389 | Shauna | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_tr_fa1` | `dyn_tr_fa1` | 153 | 1 | `grp_female_ped` | -1 | - |
| 390 | Hanna | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_tr_fa2` | `dyn_tr_fa2` | 154 | 1 | `grp_female_ped` | -1 | - |
| 391 | Krissy | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_tr_fa3` | - | 163 | 1 | `grp_female_ped` | -1 | - |
| 392 | Avery | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_tr_fa4` | `dyn_tr_fa4` | 177 | 1 | `grp_female_ped` | -1 | - |
| 393 | Rodney | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_tr_ma1` | `dyn_pl_ma6` | 151 | 0 | `grp_male_ped` | -1 | - |
| 394 | Chris | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_tr_ma2` | `dyn_tr_ma2` | 171 | 0 | `grp_male_ped` | -1 | - |
| 395 | Bruce | 4 | 4 | 1 | 2 | 500 | `DamageTough` | `Att_Normal` | `civl_tr_ma3` | - | 158 | 0 | `grp_male_ped` | -1 | - |
| 396 | Alvin | 4 | 4 | 1 | 2 | 600 | `DamageTough` | `Att_Normal` | `civl_tr_ma4` | `dyn_pl_ma3` | 183 | 0 | `ngrp_male_ped` | -1 | - |
| 397 | Tony | 4 | 5 | 1 | 2 | 700 | `DamageTough` | `Att_Normal` | `civl_tr_ma5` | `dyn_tr_ma5` | 150 | 0 | `grp_male_ped` | -1 | - |
| 398 | - | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_tr_ma1` | `dyn_pl_ma6` | 151 | 0 | - | 20 | - |
| 399 | - | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_tr_ma3` | - | 158 | 0 | - | 20 | - |
| 400 | - | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_tr_ma4` | `dyn_pl_ma3` | 183 | 0 | - | 20 | - |
| 401 | - | 2 | 4 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_tr_ma5` | `dyn_tr_ma5` | 150 | 0 | - | 20 | - |
| 402 | Amy | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_ct_fa1` | - | 165 | 1 | `grp_female_ped` | -1 | - |
| 403 | Henry | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_ct_ma1` | `dyn_tr_cw` | 168 | 0 | `grp_male_ped` | -1 | - |
| 404 | Judy | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_ct_fa2` | - | 164 | 1 | `grp_female_ped` | -1 | - |
| 405 | Leif | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_ct_ma2` | `dyn_ct_ma2` | 168 | 0 | `grp_male_ped` | -1 | - |
| 406 | Jane | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_ct_fa3` | `dyn_ct_fa3` | 166 | 1 | `grp_female_ped` | -1 | - |
| 407 | Robert | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_ct_ma3` | - | 167 | 0 | `grp_male_ped` | -1 | - |
| 408 | Lee | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_ct_fa4` | `dyn_ct_fa4` | 178 | 1 | `grp_female_ped` | -1 | - |
| 409 | Charles | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_ct_ma4` | - | 169 | 0 | `grp_male_ped` | -1 | - |
| 410 | Bob | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_ct_ma5` | - | 167 | 0 | `grp_male_ped` | -1 | - |
| 411 | Jackie | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_ct_ck1` | - | 167 | 0 | - | 20 | - |
| 412 | Sammo | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_ct_ck2` | - | 168 | 0 | - | 20 | - |
| 413 | Yan | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_ct_ck3` | - | 169 | 0 | - | 20 | - |
| 414 | Sammy | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_Weak` | `civl_ct_eb` | `dyn_sun` | 62 | 0 | - | 20 | - |
| 415 | Mr. Pong | 2 | 11 | 1 | 0 | 450 | `DamageWeak` | `Att_Weak` | `civl_ct_ac` | `dyn_ct_act` | 64 | 0 | - | 20 | - |
| 416 | Stefano | 2 | 8 | 1 | 54 | 400 | `DamageWeak` | `Att_Weak` | `civl_st` | `dyn_butcher` | 188 | 0 | - | 20 | - |
| 417 | Lamont | 4 | 4 | 1 | 2 | 600 | `DamageTough` | `Att_Normal` | `civl_co_ma1` | - | 158 | 0 | `grp_male_ped` | -1 | - |
| 418 | Coney Kid | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_co_ma2` | `dyn_pl_ma6` | 152 | 0 | `grp_male_ped` | -1 | - |
| 419 | Floyd | 4 | 4 | 1 | 2 | 300 | `DamageTough` | `Att_Normal` | `civl_co_ma3` | `dyn_co_ma3` | 182 | 0 | `grp_male_ped` | -1 | - |
| 420 | Yuri | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_co_ma4` | `dyn_tr_ma2` | 149 | 0 | `grp_male_ped` | -1 | - |
| 421 | Grimes | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_co_ma5` | `dyn_co_ma5` | 21 | 0 | `grp_male_ped` | -1 | - |
| 422 | Eric | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_pl_st1` | - | 191 | 0 | - | 5 | - |
| 423 | Warren | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_pl_st2` | - | 192 | 0 | - | 5 | - |
| 424 | Casey | 4 | 4 | 1 | 0 | 303 | `DamageWeak` | `Att_Weak` | `civl_pl_st3` | - | 193 | 0 | - | 5 | - |
| 425 | Scopes | 4 | 4 | 1 | 0 | 303 | `DamageWeak` | `Att_Weak` | `civl_scps` | `dyn_scps` | 184 | 0 | - | 5 | - |
| 426 | Owen | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_dr_ma1` | `dyn_pl_ma5` | 200 | 0 | - | 0 | - |
| 427 | Preston | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_dr_ma2` | `dyn_bm_va2` | 203 | 0 | - | 0 | - |
| 428 | Stacey | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_dr_ma4` | `dyn_pl_ma6` | 212 | 0 | - | 0 | - |
| 429 | Dante | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_dr_ma3` | `dyn_bm_vb4` | 206 | 0 | - | 0 | - |
| 430 | Cody | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_dr_ma5` | `dyn_eh_ma2` | 209 | 0 | - | 0 | - |
| 431 | Skritz | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_sp_ma5` | `dyn_tr_ma5` | 202 | 0 | - | 0 | - |
| 432 | Cap-one | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_sp_ma1` | `dyn_civl_pl_sp` | 205 | 0 | - | 0 | - |
| 433 | Scotty-C | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_sp_ma3` | `dyn_pl_ma6` | 214 | 0 | - | 0 | - |
| 434 | Chuck | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_sp_ma2` | `dyn_eh_ma4` | 211 | 0 | - | 0 | - |
| 435 | Sniffles | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_sp_ma4` | - | 208 | 0 | - | 0 | - |
| 436 | Lamar | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_we_ma2` | `dyn_we_ma2` | 204 | 0 | - | 0 | - |
| 437 | Stanley | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_we_ma1` | `dyn_tr_ma5` | 201 | 0 | - | 0 | - |
| 438 | Cesar | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_we_ma3` | `dyn_bm_va2` | 207 | 0 | - | 0 | - |
| 439 | Seth | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_we_ma4` | `dyn_pl_ma9` | 216 | 0 | - | 0 | - |
| 440 | Julius | 5 | 15 | 2 | 56 | 800 | `DamageBoss` | `Att_Dealer` | `civl_we_ma5` | - | 210 | 0 | - | 0 | - |
| 441 | Trevor | 6 | 4 | 0 | 2 | 500 | `DamageNormal` | `Att_Normal` | `civl_co_di` | - | 180 | 0 | - | 0 | - |
| 442 | Fillmore | 2 | 1 | 1 | 2 | 600 | `DamageNormal` | `Att_Pimp` | `civl_pimp1` | `dyn_pimp_va` | 156 | 0 | - | 20 | - |
| 443 | Galaxy | 2 | 1 | 1 | 56 | 2000 | `DamageBoss` | `Att_Pimp` | `civl_pimp2` | `dyn_pimp_vb` | 157 | 0 | - | 20 | - |
| 444 | - | 2 | 1 | 1 | 2 | 600 | `DamageNormal` | `Att_Pimp` | `civl_pimp1` | - | 149 | 0 | - | 20 | - |
| 445 | Reggie Styles | 2 | 1 | 1 | 2 | 600 | `DamageNormal` | `Att_Pimp` | `civl_pl_pm` | `dyn_pl_pm` | 148 | 0 | - | 20 | - |
| 446 | Russel | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_rs_ma1` | - | 160 | 0 | `grp_male_ped` | -1 | - |
| 447 | Walter | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_rs_ma2` | - | 180 | 0 | `grp_male_ped` | -1 | - |
| 448 | Jason | 4 | 4 | 1 | 4 | 600 | `DamageTough` | `Att_Normal` | `civl_rs_ma3` | - | 182 | 0 | `grp_male_ped` | -1 | - |
| 449 | Dontrev | 4 | 4 | 1 | 4 | 700 | `DamageTough` | `Att_Normal` | `civl_rs_ma4` | - | 158 | 0 | `grp_male_ped` | -1 | - |
| 450 | Trevdon | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_Weak` | `civl_rs_ma5` | - | 159 | 0 | `grp_male_ped` | -1 | - |
| 451 | Kim | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_rs_fa1` | - | 155 | 1 | `grp_female_ped` | -1 | - |
| 452 | Melissa | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_rs_fa2` | - | 153 | 1 | `grp_female_ped` | -1 | - |
| 453 | Wanda | 4 | 4 | 1 | 0 | 300 | `DamageWeak` | `Att_SprayFemale` | `civl_rs_fa3` | - | 163 | 1 | `grp_female_ped` | -1 | - |
| 454 | - | 2 | 8 | 1 | 0 | 400 | `DamageWeak` | `Att_Weak` | `civl_st` | `dyn_butcher` | 188 | 0 | - | 20 | - |
| 455 | - | 2 | 4 | 1 | 0 | 1000 | `DamageWeak` | `Att_Weak` | `civl_ch` | `dyn_ch` | 186 | 0 | - | 20 | - |
| 456 | - | 2 | 4 | 1 | 0 | 1000 | `DamageWeak` | `Att_Weak` | `civl_sk_pe` | `dyn_sk_pe` | 187 | 0 | - | 20 | - |
| 457 | - | 2 | 5 | 1 | 2 | 1000 | `DamageNormal` | `Att_Normal` | `civl_tw` | `dyn_tw` | 189 | 0 | - | 20 | - |
| 458 | - | 2 | 13 | 1 | 24 | 1000 | `DamageBoss` | `Att_Brawler` | `turn_bd` | - | 56 | 0 | - | 5 | - |
| 459 | - | 2 | 13 | 1 | 24 | 1000 | `DamageBoss` | `Att_Brawler` | `char_wheels` | `dyn_mohawk` | 56 | 0 | - | 5 | - |
| 460 | - | 2 | 13 | 1 | 0 | 9999 | `DamageBoss` | `Att_Brawler` | `punchbag` | - | 56 | 0 | - | 5 | - |

## Difficulty files

Each of the five difficulty files sets 63 character entries again, with all 17 values: ids 1 to 9, 11 to 36, 38, 40,
41, 43, 45, 49 to 61, 89 to 95, 189, 191 and 206. `config_normal.lua` gives these ids exactly the values of
`config_preload2.lua`. The other four files differ only in the values below; every value not listed is the same as
in `config_preload2.lua`.

| Difficulty | Ids | Value | In `config_preload2.lua` | In the difficulty file |
| --- | --- | --- | --- | --- |
| easy | 89 | A6 | 2400 | 2000 |
| hard | 89 | A6 | 2400 | 2600 |
| fury | 89 | A6 | 2400 | 2600 |
| fury | 47 ids (see Fury models) | Model | various | various |
| fury | 1, 2, 5, 6, 11, 12, 15, 16, 18, 19, 21, 22, 26, 27, 28, 30, 31, 33, 34, 38, 41, 43, 45, 49 to 61 (36 ids) | A17 | `none` | `dyn_bat` |
| fury | 18, 19 | A11 | `dyn_warr_cb` | `none` |
| rumble | 89 | A6 | 2400 | 2000 |
| rumble | 1 to 9, 11 to 36, 38, 40, 41, 43, 45, 49 to 54, 189, 191 (48 ids) | A6 | 1800 | 600 |
| rumble | 95 | A15 | `none` | `dyn_revival` |
| rumble | 95 | A16 | 0 | 50 |

## Fury models

The model each of the 47 ids takes in `config_fury.lua`. Twelve of these model names appear in no entry of
`config_preload2.lua`.

| Id | Model in `config_preload2.lua` | Model in `config_fury.lua` |
| ---: | --- | --- |
| 1 | `warr_cl` | `warr_cl_fury` |
| 2 | `warr_cl_gen` | `warr_cl_fury` |
| 4 | `warr_cl_chase` | `warr_cl_chase_fury` |
| 5 | `warr_sw` | `warr_sw_fury` |
| 6 | `warr_sw_gen` | `warr_sw_fury` |
| 9 | `warr_sw_chase` | `warr_sw_chase_fury` |
| 11 | `warr_aj` | `warr_aj_fury` |
| 12 | `warr_aj_gen` | `warr_aj_fury` |
| 15 | `warr_co` | `warr_co_fury` |
| 16 | `warr_co_gen` | `warr_co_fury` |
| 18 | `warr_cb` | `warr_cb_fury` |
| 19 | `warr_cb_gen` | `warr_cb_fury` |
| 21 | `warr_fo` | `warr_fo_fury` |
| 22 | `warr_fo_gen` | `warr_fo_fury` |
| 26 | `warr_ve` | `warr_ve_fury` |
| 27 | `warr_ve_gen` | `warr_ve_fury` |
| 28 | `warr_ve` | `warr_ve_fury` |
| 30 | `warr_re` | `warr_re_fury` |
| 31 | `warr_re_gen` | `warr_re_fury` |
| 33 | `warr_sn` | `warr_sn_fury` |
| 34 | `warr_sn_gen` | `warr_sn_fury` |
| 36 | `warr_sn_chase` | `warr_sn_chase_fury` |
| 38 | `warr_ty` | `fury_so4` |
| 41 | `warr_jn` | `fury_so4` |
| 43 | `warr_ma` | `fury_so4` |
| 45 | `warr_ly` | `fury_so4` |
| 49 | `warr_so_va1` | `fury_so4` |
| 50 | `warr_so_va2` | `fury_so4` |
| 51 | `warr_so_va3` | `fury_so4` |
| 52 | `warr_so_vb1` | `fury_so4` |
| 53 | `warr_so_vb2` | `fury_so4` |
| 54 | `warr_so_vb3` | `fury_so4` |
| 55 | `warr_so_va1_hr` | `fury_so4` |
| 56 | `warr_so_va2_hr` | `fury_so4` |
| 57 | `warr_so_va3_hr` | `fury_so4` |
| 58 | `warr_so_vb1_hr` | `fury_so4` |
| 59 | `warr_so_vb2_hr` | `fury_so4` |
| 60 | `warr_so_vb3_hr` | `fury_so4` |
| 61 | `warr_dest_le` | `warr_sn_fury` |
| 89 | `fury_bo` | `warr_cl` |
| 90 | `fury_lt` | `warr_sw` |
| 91 | `fury_so1` | `warr_sn` |
| 92 | `fury_so2` | `warr_aj` |
| 93 | `fury_so3` | `warr_cb` |
| 94 | `fury_so4` | `warr_fo` |
| 95 | `fury_so2` | `warr_re` |
| 206 | `char_lcgf_vest` | `warr_aj_fury` |

## Models on the disc

The 404 model names of `config_preload2.lua` and the 12 that only `config_fury.lua` gives, 416 in all, each appear in
the character list of `warriors.glr`: the chunk of type `0x44`, which holds 543 records (541 distinct name hashes).
Each record's hash is the CRC-32 of the lower-case model name.

## Story level types

The page `ref-levels` lists the character type of each story-level start (slot 1 and slot 2). Every type it gives
has an entry here, and where `charnames.lua` names the id, the name is the one `ref-levels` gives.

| Type on `ref-levels` | Character named there | Name here | Model here |
| ---: | --- | --- | --- |
| 1 | Cleon | - | `warr_cl` |
| 5 | Swan | - | `warr_sw` |
| 11 | Ajax | - | `warr_aj` |
| 15 | Cochise | - | `warr_co` |
| 21 | Fox | - | `warr_fo` |
| 24 | Fox | Fox | `warr_fo_cv2` |
| 26 | Vermin | - | `warr_ve` |
| 29 | Vermin | Vermin | `warr_ve_cv` |
| 30 | Rembrandt | - | `warr_re` |
| 32 | Rembrandt | Rembrandt | `warr_re_cv` |
| 33 | Snow | - | `warr_sn` |
| 38 | Ash | Ash | `warr_ty` |
| 40 | Ash | Ash | `warr_ty_cv` |

## Acceptance checks

- Entry 3,771 (zero-based) of the PS2 disc's `WARRIORS.DIR` is `config_preload2.lua`, 182,649 bytes, beginning with
  the Lua 4.0 signature; entry 3,720 is `charnames.lua`, 9,715 bytes.
- `config_preload2.lua` holds 449 character entries of 17 values each, with ids from 1 to 460.
- `charnames.lua` names 366 ids, and every one of them has a character entry.
- Id 32 has the model `warr_re_cv` and the name Rembrandt.
- Id 460 has the model `punchbag` and 9999 as A6, the largest A6 of any entry.
- Each of the five difficulty files sets the same 63 ids again, and `config_normal.lua` changes none of their values.
- In `config_fury.lua` the 16 ids 38, 41, 43, 45 and 49 to 60 all take the model `fury_so4`.
- In `config_rumble.lua` A6 is 600 for ids 1 to 9 and 1800 is no longer given by any of the 63 ids.
- All 416 model names appear in the character list of `warriors.glr`.
- In the first section of New Blood the player controls Rembrandt, whose type there (32) has the model `warr_re_cv`.

## Open questions

- What A2, A3, A6, A9, A12, A13 and A16 mean, and what A11, A15 and A17 do with the object names they hold.
- That A4 and A5 pick the speed and power class is inferred from their values matching the class numbers; it was not
  seen in game.
- A character's faction, the model used in the Armies mode, per-character speeds, the script names and levels a
  character appears in, and an image of each character: no data file read for this page gives them.
- The names of the 83 ids that `charnames.lua` leaves out; where the game shows a name, and whether it shows these.
- Whether the difficulty file in force replaces the entries of `config_preload2.lua` in game, and which file each
  game mode loads.
- What the two `warriors.glr` records that share a hash with another are.
- The Xbox disc's copies of these scripts were not compared.
