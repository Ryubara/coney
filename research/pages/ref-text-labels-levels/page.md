# Text labels: level tables

This reference page lists the keys of the string tables that belong to single levels, `LEVEL<N>` and `LEVEL95C`. The
shared tables (`GSTRING`, `LABEL`, `TSTRING`, `RUMBLE`), the language numbering and the level-name lists are on the
companion page `ref-text-labels-global`. Every value was read from the PS2 disc (NTSC-U, `SLUS_212.15`), from entries
of `WARRIORS.WAD` located through `WARRIORS.DIR`; all the files named here are compiled Lua 4.0 chunks. The page gives
keys, ids, counts and the files that hold them, not the text itself.

The number in a table's name is the number of the level record `level<N>` on `ref-levels`. There are 30 tables with
631 keys in all.

## Tables and levels

Level, Order and Title are those of the level record on `ref-levels`. Keys counts every key of the table; Lists counts
the keys that hold a numbered list rather than a single string. Entry and Size belong to the file `level<N>.lua`.

| Table | Level | Order | Title | Keys | Lists | Entry (zero-based) | Size in bytes | Other files with the table |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: | --- |
| `LEVEL99` | `level99` | 1 | New Blood | 69 | 0 | 9,606 | 108,385 | none |
| `LEVEL80` | `level80` | 2 | Real Live Bunch | 26 | 0 | 8,729 | 93,870 | none |
| `LEVEL87` | `level87` | 3 | Payback | 43 | 0 | 9,170 | 187,837 | none |
| `LEVEL34` | `level34` | 4 | Blackout | 24 | 0 | 7,919 | 113,064 | none |
| `LEVEL2` | `level2` | 5 | Real Heavy Rep | 22 | 0 | 7,707 | 134,521 | none |
| `LEVEL3` | `level3` | 6 | Writer's Block | 32 | 0 | 7,855 | 170,819 | `level3_strings_en.lua` |
| `LEVEL5` | `level5` | 7 | Adios Amigo | 16 | 0 | 8,028 | 161,204 | none |
| `LEVEL81` | `level81` | 8 | Encore | 9 | 0 | 8,779 | 85,086 | `level81_strings_en.lua` |
| `LEVEL86` | `level86` | 9 | Payin' The Cost | 30 | 0 | 9,116 | 179,846 | `level86_strings_en.lua` |
| `LEVEL93` | `level93` | 10 | Destroyed | 32 | 0 | 9,320 | 155,362 | none |
| `LEVEL31` | `level31` | 11 | Boys In Blue | 26 | 0 | 7,859 | 140,376 | none |
| `LEVEL14` | `level14` | 12 | Set Up | 18 | 0 | 7,654 | 153,175 | none |
| `LEVEL9` | `level9` | 13 | All-City | 16 | 0 | 9,228 | 132,548 | none |
| `LEVEL51` | `level51` | 14 | Desperate Dudes | 19 | 0 | 8,030 | 167,192 | none |
| `LEVEL52` | `level52` | 15 | No Permits, No Parley | 16 | 0 | 8,190 | 182,309 | none |
| `LEVEL54` | `level54` | 16 | Home Run | 17 | 0 | 8,261 | 171,812 | none |
| `LEVEL55` | `level55` | 17 | Friendly Faces | 15 | 0 | 8,356 | 80,178 | none |
| `LEVEL84` | `level84` | 18 | Come Out To Play | 6 | 0 | 9,029 | 63,868 | none |
| `LEVEL82` | `level82` | 19 | Roots | 27 | 0 | 8,845 | 187,495 | none |
| `LEVEL92` | `level92` | 20 | The Best | 25 | 0 | 9,257 | 136,768 | none |
| `LEVEL83` | `level83` | 21 | Heavy Muscle | 26 | 0 | 8,915 | 154,001 | none |
| `LEVEL20` | `level20` | 22 | Scout's Honor | 28 | 0 | 7,709 | 187,113 | none |
| `LEVEL11` | `level11` | 23 | Sharp Dressed Man | 25 | 0 | 7,072 | 170,160 | none |
| `LEVEL95` | `level95` | 24 | no title | 27 | 11 | 9,400 | 288,407 | the five `level95_strings_<lang>.lua` |
| `LEVEL95C` | `level95` | 24 | no title | 13 | 3 | none | none | the five `level95_coney_strings_<lang>.lua` |
| `LEVEL60` | `level60` | 25 | Stage 1 | 3 | 0 | 8,487 | 46,855 | `level60_strings.lua` |
| `LEVEL61` | `level61` | 25 | Stage 2 | 4 | 0 | 8,513 | 56,181 | `level61_strings.lua` |
| `LEVEL62` | `level62` | 25 | Stage 3 | 3 | 0 | 8,545 | 47,106 | `level62_strings.lua` |
| `LEVEL63` | `level63` | 25 | Stage 4 | 5 | 0 | 8,569 | 39,852 | `level63_strings.lua` |
| `LEVEL64` | `level64` | 25 | Stage 5 | 9 | 0 | 8,594 | 55,388 | `level64_strings.lua` |

- Every level of orders 1 to 25 has a table, and every table's number is a level record. No other level record has
  one.
- `level<N>.lua` holds its table in all five languages. `LEVEL95C` is held only by the `level95_coney_strings` files.

## Strings files

These files hold a level's table apart from `level<N>.lua`.

| File | Entry (zero-based) | Size in bytes | Languages | Table |
| --- | ---: | ---: | --- | --- |
| `level3_strings_en.lua` | 8,026 | 2,349 | English | `LEVEL3` |
| `level81_strings_en.lua` | 8,843 | 828 | English | `LEVEL81` |
| `level86_strings_en.lua` | 9,168 | 2,360 | English | `LEVEL86` |
| `level60_strings.lua` | 8,511 | 1,365 | all five | `LEVEL60` |
| `level61_strings.lua` | 8,543 | 1,641 | all five | `LEVEL61` |
| `level62_strings.lua` | 8,567 | 1,419 | all five | `LEVEL62` |
| `level63_strings.lua` | 8,592 | 1,917 | all five | `LEVEL63` |
| `level64_strings.lua` | 8,622 | 3,575 | all five | `LEVEL64` |
| `level95_strings_de.lua` | 9,526 | 8,027 | German | `LEVEL95` |
| `level95_strings_en.lua` | 9,527 | 7,487 | English | `LEVEL95` |
| `level95_strings_es.lua` | 9,528 | 8,943 | Spanish | `LEVEL95` |
| `level95_strings_fr.lua` | 9,529 | 7,899 | French | `LEVEL95` |
| `level95_strings_it.lua` | 9,530 | 7,821 | Italian | `LEVEL95` |
| `level95_coney_strings_de.lua` | 9,513 | 12,821 | German | `LEVEL95C` |
| `level95_coney_strings_en.lua` | 9,514 | 11,978 | English | `LEVEL95C` |
| `level95_coney_strings_es.lua` | 9,515 | 12,630 | Spanish | `LEVEL95C` |
| `level95_coney_strings_fr.lua` | 9,516 | 12,659 | French | `LEVEL95C` |
| `level95_coney_strings_it.lua` | 9,517 | 12,641 | Italian | `LEVEL95C` |

- Each strings file holds the same keys as the table in `level<N>.lua`, with the same value in each language it
  carries: 32 keys for `level3_strings_en.lua`, 9 for `level81_strings_en.lua`, 30 for `level86_strings_en.lua`, and
  85 strings and list items per language for the `level95_strings` files.
- No French, German, Spanish or Italian counterpart of the three English-only files exists under the same naming.

## Keys by table

Each table below lists its keys in the order of their first appearance in the file. Value is a single string or a
numbered list with its ids; Languages counts the languages with a value.

### LEVEL99

| Key | Value | Languages |
| --- | --- | --- |
| `V` | string | 5 |
| `MP_0` | string | 5 |
| `MP_1` | string | 5 |
| `MP_2` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C2_3` | string | 5 |
| `MS_C2_5` | string | 5 |
| `MS_C2_6` | string | 5 |
| `LBL_1` | string | 5 |
| `TT_1` | string | 5 |
| `TT_1a` | string | 5 |
| `TT_3` | string | 5 |
| `TT_3c` | string | 5 |
| `TT_3d` | string | 5 |
| `TT_5` | string | 5 |
| `TT_6` | string | 5 |
| `TT_7` | string | 5 |
| `TT_9` | string | 5 |
| `TT_9a` | string | 5 |
| `TT_10` | string | 5 |
| `TT_13` | string | 5 |
| `TT_14` | string | 5 |
| `TT_15` | string | 5 |
| `TT_16` | string | 5 |
| `TT_4a` | string | 5 |
| `TT_17` | string | 5 |
| `TT_18` | string | 5 |
| `TT_18a` | string | 5 |
| `TT_19a` | string | 5 |
| `TT_19b` | string | 5 |
| `TT_19c` | string | 5 |
| `TT_19d` | string | 5 |
| `TT_19e` | string | 5 |
| `TT_19f` | string | 5 |
| `TT_19g` | string | 5 |
| `TT_20a` | string | 5 |
| `TT_20c` | string | 5 |
| `TT_22a` | string | 5 |
| `TT_22b` | string | 5 |
| `TT_23a` | string | 5 |
| `TT_23b` | string | 5 |
| `TT_23d` | string | 5 |
| `TT_23e` | string | 5 |
| `TT_23f` | string | 5 |
| `TT_24a` | string | 5 |
| `TT_24b` | string | 5 |
| `TT_24d` | string | 5 |
| `TT_24e` | string | 5 |
| `TT_25a` | string | 5 |
| `TT_25b` | string | 5 |
| `TT_25c` | string | 5 |
| `TT_25d` | string | 5 |
| `TT_25e` | string | 5 |
| `TT_25h` | string | 5 |
| `TT_25i` | string | 5 |
| `TT_25j` | string | 5 |
| `TT_25k` | string | 5 |
| `TT_26a` | string | 5 |
| `TT_26b` | string | 5 |
| `TT_28a` | string | 5 |
| `TT_28b` | string | 5 |
| `TT_28d` | string | 5 |
| `TT_28e` | string | 5 |
| `TT_28f` | string | 5 |
| `TT_28h` | string | 5 |
| `TT_29` | string | 5 |
| `TT_30` | string | 5 |
| `TT_40` | string | 5 |

### LEVEL80

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_1a` | string | 5 |
| `MS_C3_2` | string | 5 |
| `MS_C3_3` | string | 5 |
| `MS_C3_4` | string | 5 |
| `MS_C4_1` | string | 5 |
| `MS_C4_2` | string | 5 |
| `TANDEM` | string | 5 |
| `TT_1` | string | 4: English, French, German, Spanish |
| `TT_4` | string | 5 |
| `TT_5` | string | 5 |
| `TT_7` | string | 5 |
| `TT_8` | string | 5 |
| `TT_9` | string | 5 |
| `TT_16` | string | 5 |
| `TT_20` | string | 5 |
| `TT_11` | string | 5 |
| `TT_25` | string | 5 |
| `TT_26` | string | 5 |
| `TT_27` | string | 5 |
| `TT_28` | string | 5 |
| `TT_29` | string | 5 |

### LEVEL87

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C2_3` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_2` | string | 5 |
| `MS_C3_3` | string | 5 |
| `MS_C4_1` | string | 5 |
| `MS_C4_3` | string | 5 |
| `MS_C4_2` | string | 5 |
| `MS_C5_1` | string | 5 |
| `MS_C5_2` | string | 5 |
| `MS_C5_3` | string | 5 |
| `MS_C5_3a` | string | 5 |
| `MS_C5_3b` | string | 5 |
| `MS_C5_3c` | string | 5 |
| `MB_1` | string | 5 |
| `MB_2_1` | string | 5 |
| `MB_2_2` | string | 5 |
| `RUNATTACK` | string | 5 |
| `SPRAYATTACK` | string | 5 |
| `CASH` | string | 5 |
| `TT_1` | string | 5 |
| `TT_2` | string | 5 |
| `TT_41` | string | 5 |
| `TT_3` | string | 5 |
| `RM_1` | string | 5 |
| `RM_2` | string | 5 |
| `MS_C5_4` | string | 5 |
| `MS_C5_5` | string | 5 |
| `MS_C5_6` | string | 5 |
| `MS_C5_7` | string | 5 |
| `MS_C5_8` | string | 5 |
| `MS_C5_9` | string | 5 |
| `MS_C5_10` | string | 5 |
| `MS_C5_11` | string | 5 |
| `MS_C5_12` | string | 5 |
| `MS_C5_13` | string | 5 |
| `MS_C5_14` | string | 5 |
| `MF_C5_1` | string | 5 |
| `MF_C5_2` | string | 5 |

### LEVEL34

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MP_2` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_1a` | string | 5 |
| `MS_C1_3` | string | 5 |
| `MS_C1_5` | string | 5 |
| `MS_C1_6` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C3_1` | string | 5 |
| `TT_1` | string | 5 |
| `BMUG` | string | 5 |
| `BTAG` | string | 5 |
| `BRADIO` | string | 5 |
| `BBEAT` | string | 5 |
| `BBEAT2` | string | 5 |
| `BPIMP` | string | 5 |
| `WEAPONBREAK` | string | 5 |
| `WEAPONTHROW` | string | 5 |
| `SCATTER` | string | 5 |
| `LBL_1` | string | 5 |
| `LBL_2` | string | 5 |
| `LBL_3` | string | 5 |
| `LBL_4` | string | 5 |

### LEVEL2

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C1_3` | string | 5 |
| `MS_C1_4` | string | 5 |
| `MS_C1_5` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C4_1` | string | 5 |
| `MS_C4_2` | string | 5 |
| `MB_1` | string | 5 |
| `MB_2` | string | 5 |
| `MB_3` | string | 5 |
| `MB_4` | string | 5 |
| `BTAG` | string | 5 |
| `TT_1` | string | 5 |
| `TT_2` | string | 5 |
| `LBL_1` | string | 5 |
| `SPRINT` | string | 5 |
| `COUNTER` | string | 5 |
| `RAGETHROW` | string | 5 |

### LEVEL3

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MP_2` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C1_3` | string | 5 |
| `MS_C1_4` | string | 5 |
| `MS_C1_7` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_2` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C4_3` | string | 5 |
| `MS_C5_1` | string | 5 |
| `MB_1` | string | 5 |
| `MB_3` | string | 5 |
| `BTAG` | string | 5 |
| `LT_1` | string | 5 |
| `BROKETRUCE` | string | 5 |
| `OUTOFTIME` | string | 5 |
| `MF_1` | string | 5 |
| `MF_2` | string | 5 |
| `MF_3` | string | 5 |
| `MF_4` | string | 5 |
| `MS_C1_5` | string | 5 |
| `MS_C1_6` | string | 5 |
| `PULLEY` | string | 5 |
| `GALLERY` | string | 5 |
| `ELEVATOR` | string | 5 |
| `WARRIORS` | string | 5 |
| `MOON` | string | 5 |
| `ELEC` | string | 5 |
| `PANZERS` | string | 5 |
| `MIMESARRIVE` | string | 5 |

### LEVEL5

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MB_1` | string | 5 |
| `MBBAR_1` | string | 5 |
| `MB_2` | string | 5 |
| `SS` | string | 5 |
| `SAN` | string | 5 |
| `DIE` | string | 5 |
| `VAR` | string | 5 |
| `MF` | string | 5 |
| `BTAG` | string | 5 |
| `WALLSMASH` | string | 5 |
| `RAGEBUILD` | string | 5 |

### LEVEL81

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_1a` | string | 5 |
| `MS_C3_4` | string | 5 |
| `MS_C3_5` | string | 5 |
| `MS_C3_3` | string | 5 |
| `HINT` | string | 5 |
| `CHAT` | string | 5 |

### LEVEL86

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1A` | string | 5 |
| `MS_PH_HUD` | string | 5 |
| `MS_C1_1B` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MF_C1_1` | string | 5 |
| `MF_C1_PETE` | string | 5 |
| `MF_C1_CHUCK` | string | 5 |
| `MF_C1_MILDRED` | string | 5 |
| `MF_C1_STEPHANO` | string | 5 |
| `MF_C1_PLAYERTRASH` | string | 5 |
| `CLEONTRASH` | string | 5 |
| `BUYHOTDOG` | string | 5 |
| `VOODOO_MOTORS` | string | 5 |
| `LITTLE_BUDDHA` | string | 5 |
| `PLUMBING_STORE` | string | 5 |
| `MEAT_MARKET` | string | 5 |
| `LOCK` | string | 5 |
| `ONE_OF_FOUR` | string | 5 |
| `TWO_OF_FOUR` | string | 5 |
| `THREE_OF_FOUR` | string | 5 |
| `FOUR_OF_FOUR` | string | 5 |
| `MF_VOODOO` | string | 5 |
| `MF_BUDDHA` | string | 5 |
| `MF_PLUMB` | string | 5 |
| `MF_MEATMARKET` | string | 5 |
| `FORMAT_OWNER_STRING` | string | 5 |
| `FORMAT_SPACE` | string | 5 |

### LEVEL93

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_1A` | string | 5 |
| `MS_C1_1B` | string | 5 |
| `MS_C1_1C` | string | 5 |
| `DAMAGE_BAR` | string | 5 |
| `PHARMACY_BAR` | string | 5 |
| `LIQUOR_BAR` | string | 5 |
| `PH_STORES` | string | 5 |
| `KEEP_MOVING` | string | 5 |
| `MS_C2_1B` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2B_1` | string | 5 |
| `MS_C2_2C` | string | 5 |
| `MS_C2_2D` | string | 5 |
| `MS_C2_2E` | string | 5 |
| `MS_C2_3` | string | 5 |
| `WAREHOUSE_BAR` | string | 5 |
| `MS_C3_1` | string | 5 |
| `PH_FIGHTS` | string | 5 |
| `MS_C4_1A` | string | 5 |
| `MS_C4_1C` | string | 5 |
| `MS_C4_1D` | string | 5 |
| `RAGEWALL` | string | 5 |
| `MS_C5_1` | string | 5 |
| `MS_C5_1_FAIL_1` | string | 5 |
| `MS_C5_1_FAIL_2` | string | 5 |
| `MS_C5_CHASEBAR` | string | 5 |
| `MS_C6_1` | string | 5 |
| `MS_C6_2` | string | 5 |
| `BTAG` | string | 5 |
| `VIRG` | string | 5 |

### LEVEL31

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MP_2` | string | 5 |
| `MP_3` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C1_3` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_1a` | string | 5 |
| `MS_C3_2` | string | 5 |
| `MS_C3_3` | string | 5 |
| `MS_C3_4` | string | 5 |
| `MS_C3_5` | string | 5 |
| `MS_C4_1` | string | 5 |
| `MS_C4_2` | string | 5 |
| `MB_1` | string | 5 |
| `BTAG` | string | 5 |
| `SCATTER` | string | 5 |
| `TT_1` | string | 5 |
| `TT_2` | string | 5 |
| `TT_3` | string | 5 |
| `TT_4` | string | 5 |
| `LBL_1` | string | 5 |
| `BIRDI` | string | 5 |
| `FAIL` | string | 5 |

### LEVEL14

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_4` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_3` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C2_3` | string | 5 |
| `MS_C2_5` | string | 5 |
| `MS_C2_6` | string | 5 |
| `LOOTLABEL` | string | 5 |
| `BTAG` | string | 5 |
| `BCAR` | string | 5 |
| `BBRN` | string | 5 |
| `BHAT` | string | 5 |
| `CARDESTROYED` | string | 5 |
| `JSBSAW` | string | 5 |
| `COPSTAKELOOT` | string | 5 |
| `LOOTGONETUT` | string | 5 |
| `REARATTACK` | string | 5 |

### LEVEL9

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_3` | string | 5 |
| `MS_C1_4` | string | 5 |
| `MS_C1_5` | string | 5 |
| `MS_C1_6` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_2` | string | 5 |
| `MS_C4_1` | string | 5 |
| `MB_1` | string | 5 |
| `BTAG` | string | 5 |
| `WAR` | string | 5 |
| `REMINDER` | string | 5 |
| `INFO` | string | 5 |

### LEVEL51

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C1_2A` | string | 5 |
| `MS_C1_3` | string | 5 |
| `MS_C1_3A` | string | 5 |
| `MS_C1_4` | string | 5 |
| `MS_C1_5` | string | 5 |
| `MS_C1_6` | string | 5 |
| `MS_C1_7` | string | 5 |
| `MS_C1_8` | string | 5 |
| `MS_C1_10` | string | 5 |
| `MS_C1_12` | string | 5 |
| `MB_1` | string | 5 |
| `BTAG` | string | 5 |
| `CLUB` | string | 5 |
| `KO` | string | 5 |
| `MS_C1_9` | string | 5 |
| `MS_C1_11` | string | 5 |

### LEVEL52

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_0` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C2_1A` | string | 5 |
| `MS_C2_1B` | string | 5 |
| `MS_C2_1C` | string | 5 |
| `MS_C2_1D` | string | 5 |
| `MS_C2_1E` | string | 5 |
| `MS_C2_1F` | string | 5 |
| `MS_C2_1G` | string | 5 |
| `MS_C2_1H` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_2` | string | 5 |
| `MS_C3_FAILED` | string | 5 |
| `BTAG` | string | 5 |

### LEVEL54

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C2_0` | string | 5 |
| `MS_C2_4` | string | 5 |
| `MS_C2_5` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_2` | string | 5 |
| `TIMER` | string | 5 |
| `LP` | string | 5 |
| `LABEL` | string | 5 |
| `COBB` | string | 5 |
| `CLEON` | string | 5 |
| `FAIL_1` | string | 5 |
| `FAIL_2` | string | 5 |
| `FAIL_3` | string | 5 |

### LEVEL55

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MP_2` | string | 5 |
| `MP_3` | string | 5 |
| `MP_4` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C1_3` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C2_3` | string | 5 |
| `MS_C3_1` | string | 5 |
| `TT_1` | string | 5 |
| `LIZZIEDOOR` | string | 5 |
| `MERCYHEALTH` | string | 5 |
| `MERCYDIED` | string | 5 |

### LEVEL84

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C3_1` | string | 5 |
| `LUTHER` | string | 5 |
| `KO` | string | 5 |
| `THROW` | string | 5 |

### LEVEL82

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MP_2` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_2` | string | 5 |
| `MS_C1_4` | string | 5 |
| `MS_C2_1a` | string | 5 |
| `MS_C2_1b` | string | 5 |
| `MS_C2_1c` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MB_1` | string | 5 |
| `MT_1` | string | 5 |
| `MT_2` | string | 5 |
| `MT_3` | string | 5 |
| `MT_4` | string | 5 |
| `BARLABEL` | string | 5 |
| `CRIME1` | string | 5 |
| `CRIME2` | string | 5 |
| `CRIME3` | string | 5 |
| `BUMRIOT` | string | 5 |
| `STAKEOUT` | string | 5 |
| `ASSAULT` | string | 5 |
| `MUGGING` | string | 5 |
| `VANDALISM` | string | 5 |
| `CAR` | string | 5 |
| `CRIMEWAVEPLUS` | string | 5 |
| `TINY` | string | 5 |
| `SPIDER` | string | 5 |

### LEVEL92

| Key | Value | Languages |
| --- | --- | --- |
| `MP_2a` | string | 5 |
| `MP_2b` | string | 5 |
| `MP_3` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_1A` | string | 5 |
| `MS_C2_1B` | string | 5 |
| `MS_C2_1C_1` | string | 5 |
| `MS_C2_1C_2` | string | 5 |
| `MS_C2_1D` | string | 5 |
| `MS_C2_E` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_2` | string | 5 |
| `BTAG` | string | 5 |
| `KING_FAILED_MSG` | string | 5 |
| `KING_RULES_1` | string | 5 |
| `KING_RULES_2` | string | 5 |
| `KING_RULES_3` | string | 5 |
| `HIT_STORES` | string | 5 |
| `HIT_LAST_STORE` | string | 5 |
| `HUD_TEXT_STORES` | string | 5 |
| `PLUMB_BAR` | string | 5 |
| `VOODOO_BAR` | string | 5 |
| `MEAT_BAR` | string | 5 |
| `HEAD_BAR` | string | 5 |

### LEVEL83

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MP_2` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_3` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_3` | string | 5 |
| `MS_C3_4` | string | 5 |
| `MS_C3_5` | string | 5 |
| `MS_C4_1` | string | 5 |
| `MS_C4_4` | string | 5 |
| `MS_C4_2` | string | 5 |
| `MS_C4_5` | string | 5 |
| `MS_C4_6` | string | 5 |
| `MS_C4_3` | string | 5 |
| `MS_C5_1` | string | 5 |
| `MS_C5_2` | string | 5 |
| `MB_1` | string | 5 |
| `MB_2` | string | 5 |
| `MB_4` | string | 5 |
| `MB_5` | string | 5 |
| `MF_1` | string | 5 |
| `MF_2` | string | 5 |
| `PRESSTOPAY1FORICECREAM` | string | 5 |
| `FIGHTPEN_TIMER_NAME` | string | 5 |
| `CHASE_TIMER_NAME` | string | 5 |

### LEVEL20

| Key | Value | Languages |
| --- | --- | --- |
| `ACTIONFORMAT` | string | 5 |
| `MP_1` | string | 5 |
| `MS_C1_0` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_C1_3` | string | 5 |
| `MS_C1_7` | string | 5 |
| `MS_C1_8` | string | 5 |
| `MS_C1_9` | string | 5 |
| `MS_C2_1` | string | 5 |
| `MS_C2_2` | string | 5 |
| `MS_C2_3` | string | 5 |
| `MS_C2_4` | string | 5 |
| `MS_C2_5` | string | 5 |
| `MS_C2_6` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_1a` | string | 5 |
| `MS_C3_2` | string | 5 |
| `MS_C3_2a` | string | 5 |
| `MS_C3_3` | string | 5 |
| `MB_4` | string | 5 |
| `BTAG` | string | 5 |
| `LBL_1` | string | 5 |
| `LBL_2` | string | 5 |
| `LBL_3` | string | 5 |
| `FAILED_1` | string | 5 |
| `FAILED_2` | string | 5 |
| `FAILED_3` | string | 5 |
| `GHOST` | string | 5 |

### LEVEL11

| Key | Value | Languages |
| --- | --- | --- |
| `MP_1` | string | 5 |
| `MP_2` | string | 5 |
| `MS_C1_1` | string | 5 |
| `MS_HINT_1` | string | 5 |
| `MS_HINT_2` | string | 5 |
| `MS_C2_1A` | string | 5 |
| `MS_C2_1D` | string | 5 |
| `MS_C2_1E` | string | 5 |
| `MS_C2_1B` | string | 5 |
| `MS_C2_1C` | string | 5 |
| `MS_C2_2A` | string | 5 |
| `MS_C2_2B` | string | 5 |
| `MS_C2_2C` | string | 5 |
| `MS_C2_2D` | string | 5 |
| `MS_C3_1` | string | 5 |
| `MS_C3_1A` | string | 5 |
| `MS_C3_1B` | string | 5 |
| `MS_C3_2` | string | 5 |
| `MS_C3_3` | string | 5 |
| `MS_C4_1` | string | 5 |
| `MS_C5_1` | string | 5 |
| `BTAG` | string | 5 |
| `BHAT` | string | 5 |
| `MF_C1` | string | 5 |
| `MOE` | string | 5 |

### LEVEL95

| Key | Value | Languages |
| --- | --- | --- |
| `INT` | list of 5, ids 1-5 | 5 |
| `ACT` | list of 11, ids 1-11 | 5 |
| `NEW` | list of 5, ids 1-5 | 5 |
| `NEWC` | list of 4, ids 2-5 | 5 |
| `ACTC` | list of 9, ids 2-7, 10-12 | 5 |
| `ACTTUT` | list of 9, ids 2-7, 10-12 | 5 |
| `WORKTITLE` | string | 5 |
| `WORKOUT` | list of 4, ids 1-4 | 5 |
| `WLABEL` | list of 5, ids 1-5 | 5 |
| `PLAY` | list of 7, ids 1-7 | 5 |
| `INC` | list of 7, ids 1-7 | 5 |
| `RANK` | string | 5 |
| `WORKTIRED` | string | 5 |
| `WORKGOOD` | string | 5 |
| `WORKFAIL` | string | 5 |
| `FEATEXP` | list of 3, ids 1, 15, 20 | 5 |
| `MS_C4_1` | string | 5 |
| `MS_C9_1` | string | 5 |
| `MS_C9_2` | string | 5 |
| `MS_C9_3` | string | 5 |
| `CS_1` | string | 5 |
| `CS_2` | string | 5 |
| `CS_3` | string | 5 |
| `CS_4` | string | 5 |
| `CS_5` | string | 5 |
| `CS_6` | string | 5 |
| `LOCKPICK` | string | 5 |

The `LEVEL95` lists hold 69 items and the table 16 single strings, 85 values per language in all.

### LEVEL95C

| Key | Value | Languages |
| --- | --- | --- |
| `JOIN` | string | 5 |
| `BUSTEDLABEL` | string | 5 |
| `HOMELESSLABEL` | string | 5 |
| `DEALWITHITLABEL` | string | 5 |
| `FLASHRAGELABEL` | string | 5 |
| `ROBINLABEL` | string | 5 |
| `INVGANG` | list of 6, ids 1-6 | 5 |
| `GIVEFINGER` | string | 5 |
| `FINISHGAME` | string | 5 |
| `PATROL` | string | 5 |
| `MINI` | string | 5 |
| `ACTTUT` | list of 3, ids 1-2, 6 | 5 |
| `CUFFTUT` | list of 1, ids 1 | 5 |

### LEVEL60

| Key | Value | Languages |
| --- | --- | --- |
| `MS_1` | string | 5 |
| `MS_2` | string | 5 |
| `MS_3` | string | 5 |

### LEVEL61

| Key | Value | Languages |
| --- | --- | --- |
| `MS_1` | string | 5 |
| `MS_2` | string | 5 |
| `MS_3` | string | 5 |
| `MS_4` | string | 5 |

### LEVEL62

| Key | Value | Languages |
| --- | --- | --- |
| `MS_1` | string | 5 |
| `MS_2` | string | 5 |
| `MS_3` | string | 5 |

### LEVEL63

| Key | Value | Languages |
| --- | --- | --- |
| `MS_1` | string | 5 |
| `MS_2` | string | 5 |
| `MS_3` | string | 5 |
| `MS_4` | string | 5 |
| `MS_5` | string | 5 |

### LEVEL64

| Key | Value | Languages |
| --- | --- | --- |
| `MS_1` | string | 5 |
| `MS_2` | string | 5 |
| `MS_3` | string | 5 |
| `MS_9` | string | 5 |
| `MS_10` | string | 5 |
| `MS_11` | string | 5 |
| `MS_12` | string | 5 |
| `MS_14` | string | 5 |
| `MS_15` | string | 5 |

## Acceptance checks

- Entry 9,606 (zero-based) of the PS2 disc's `WARRIORS.DIR` is `level99.lua`, 108,385 bytes, beginning with the Lua
  4.0 signature; it holds `LEVEL99` with 69 keys in five languages.
- The 30 level tables hold 631 keys in all; the largest is `LEVEL99` (69 keys), the smallest `LEVEL60` and `LEVEL62`
  (3 keys each).
- Each of the 23 story levels of orders 1 to 23, the five stages `level60` to `level64` and `level95` has a table of
  its own number; `LEVEL95C` is the thirtieth.
- `LEVEL80.TT_1` has no Italian value; every other key on this page has a value in all five languages.
- `level3_strings_en.lua` (entry 8,026, 2,349 bytes) holds the 32 keys of `LEVEL3` with the same English values as
  `level3.lua`.
- `level95_strings_en.lua` (entry 9,527, 7,487 bytes) holds the same 85 English values of `LEVEL95` as `level95.lua`.
- `LEVEL95.NEWC` holds ids 2 to 5 and no id 1; `LEVEL95.ACT` holds ids 1 to 11.
- `LEVEL95C.ACTTUT` holds ids 1, 2 and 6 in each of the five `level95_coney_strings_<lang>.lua` files.

## Open questions

- Seventeen keys are named in level scripts but have no value in any table on the disc, so they are not listed above.
  `LEVEL3.GALLERYSTRING` is named in `level3_gallery.lua`, `LEVEL14.MP_0` in `level14_radiosandstores.lua`,
  `LEVEL55.B_1` in `level55_subway.lua`, `LEVEL55.QUIT` in `level55_party.lua`, and `LEVEL99.MS_C3_1` to
  `LEVEL99.MS_C3_12` and `LEVEL99.TT_W` in `level99_combat.lua`. What the game shows in their place was not checked.
- What the game shows in Italian for `LEVEL80.TT_1`.
- Why the lists of `LEVEL95` and `LEVEL95C` skip ids (`NEWC` 1; `ACTC` and `ACTTUT` 1, 8 and 9; `FEATEXP` all but 1,
  15 and 20; `LEVEL95C.ACTTUT` 3 to 5).
- Whether the game reads the three English-only strings files or the `level60` to `level64` strings files; their
  values duplicate those in the level scripts.
- What the key prefixes (`MP_`, `MS_C<k>_`, `MB_`, `MF_`, `TT_`, `LBL_`, `CS_` and others) stand for was not confirmed
  in game.
- The Xbox disc's copy of these scripts was not compared.
