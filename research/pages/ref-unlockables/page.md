# Unlockables table

This page lists every record of the unlockables table on the PS2 disc. The table is the list named `Unlockables` in
the compiled script `global.lua`, which sits in the PS2 archive (`WARRIORS.DIR` and `WARRIORS.WAD`). The models in
the type 4 table are the model names that `config_preload2.lua` gives to each character id. All values below were
read from the disc image; none come from the executable.

## Source files

| File | Archive entry | Size (bytes) | Holds |
| --- | ---: | ---: | --- |
| `global.lua` | 4,105 | 169,601 | The `Unlockables` list (610 records) and the `RM_` mode constants |
| `config_preload2.lua` | 3,771 | 182,649 | 449 character definitions: character id and model name |

- Both files are compiled Lua 4.0 chunks: they start with the bytes `1B 4C 75 61 40`.
- `WARRIORS.DIR` is 128,428 bytes and lists 10,701 entries; the entry numbers above count from 0.

## Record layout

The list holds 610 records of six numbers each. A record's number is its position in the list counting from 0.

| Position | Field | Values on the disc | Meaning |
| ---: | --- | --- | --- |
| 1 | Level | 0 to 99, 33 distinct values | Level number the record is tied to |
| 2 | Group | 0 or 1 | Second part of the record's key |
| 3 | Item | 0 to 92 | Third part of the record's key |
| 4 | Type | 0 to 11 | Kind of thing the record opens (see "Record types") |
| 5 | Extra | 0 to 365 | Per-type number (see "Record types") |
| 6 | Data | 0 to 453 | Id of the thing opened, within its type |

- Level, group and item together form a key that several records can share: the 610 records use 153 distinct keys.
- No two records of the same type share a data value: every (type, data) pair occurs once.
- In the type 1 records the data value is written as the name of a constant defined earlier in `global.lua`; the
  tables below give both the value and the constant's name.

## Record types

| Type | Records | Record numbers | Data values | Extra values | What the data value is |
| ---: | ---: | --- | --- | --- | --- |
| 0 | 18 | 0 to 22 | 18 level numbers, 2 to 99 | 0 | A level number; each has a `level<n>.lua` script on the disc |
| 1 | 9 | 161 to 169 | 1 to 24 | Odd numbers 35 to 51 | A mode number, written as an `RM_` constant |
| 2 | 29 | 581 to 609 | 29 numbers, 101 to 134 | 1 | A level number; each has a `level<n>.lua` script on the disc |
| 3 | 46 | 170 to 215 | 46 numbers, 1 to 49 | 1 to 46 in record order | Not identified |
| 4 | 365 | 216 to 580 | 365 numbers, 2 to 453 | 1 to 365 in record order | A character id from `config_preload2.lua` |
| 5 | 6 | 155 to 160 | 1 to 6 | 29 to 34 | Not identified |
| 6 | 28 | 28 to 49 and 56 to 61 | 1 to 28 | 1 to 28 in record order | Not identified |
| 7 | 5 | 5, 7, 10, 12, 16 | 11, 20, 82, 83, 92 | 0 | A level number; each has a `level<n>.lua` script on the disc |
| 8 | 28 | 50 to 55, 62, 73 to 81, 116 to 127 | 28 numbers, 1 to 92 | 22 (records 50 to 55) or 28 | Not identified |
| 9 | 5 | 23 to 27 | 60 to 64 | 0 | A level number; each has a `level<n>.lua` script on the disc |
| 10 | 44 | 63 to 72, 82 to 115 | 44 numbers, 11 to 74 | 28 | Not identified |
| 11 | 27 | 128 to 154 | 0 to 26 | 28 | Not identified |

- The type 0 records form one chain of level numbers: each record's data value is the level value of the next record.
  The chain runs 0, 99, 80, 87, 34, 2, 3, 5, 81, 86, 93, 31, 14, 9, 51, 52, 54, 55 and ends at 84.
- The type 9 records also form a chain: 84, 60, 61, 62, 63 and 64.
- Records 0 to 27 are all of types 0, 7 and 9, with group, item and extra 0.

## Type 0 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 0 | 0 | 0 | 99 |
| 1 | 99 | 0 | 0 | 0 | 80 |
| 2 | 80 | 0 | 0 | 0 | 87 |
| 3 | 87 | 0 | 0 | 0 | 34 |
| 4 | 34 | 0 | 0 | 0 | 2 |
| 6 | 2 | 0 | 0 | 0 | 3 |
| 8 | 3 | 0 | 0 | 0 | 5 |
| 9 | 5 | 0 | 0 | 0 | 81 |
| 11 | 81 | 0 | 0 | 0 | 86 |
| 13 | 86 | 0 | 0 | 0 | 93 |
| 14 | 93 | 0 | 0 | 0 | 31 |
| 15 | 31 | 0 | 0 | 0 | 14 |
| 17 | 14 | 0 | 0 | 0 | 9 |
| 18 | 9 | 0 | 0 | 0 | 51 |
| 19 | 51 | 0 | 0 | 0 | 52 |
| 20 | 52 | 0 | 0 | 0 | 54 |
| 21 | 54 | 0 | 0 | 0 | 55 |
| 22 | 55 | 0 | 0 | 0 | 84 |

## Type 1 records

| Record | Level | Group | Item | Extra | Data | Mode constant |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 161 | 0 | 0 | 0 | 35 | 12 | `RM_Brawl1` |
| 162 | 0 | 0 | 0 | 37 | 14 | `RM_Brawl5` |
| 163 | 87 | 0 | 0 | 39 | 1 | `RM_Brawl` |
| 164 | 34 | 0 | 0 | 41 | 9 | `RM_Surv` |
| 165 | 2 | 0 | 0 | 43 | 23 | `RM_Mercy` |
| 166 | 3 | 0 | 0 | 45 | 19 | `RM_TagBt` |
| 167 | 5 | 0 | 0 | 47 | 3 | `RM_Royal` |
| 168 | 31 | 0 | 0 | 49 | 24 | `RM_Wchair` |
| 169 | 92 | 0 | 0 | 51 | 2 | `RM_Koth` |

`global.lua` defines 18 `RM_` mode constants plus `RM_MaxGangs` (47). Nine of the modes appear in the table:

| Constant | Value | In the table |
| --- | ---: | --- |
| `RM_Brawl` | 1 | Record 163 |
| `RM_Koth` | 2 | Record 169 |
| `RM_Royal` | 3 | Record 167 |
| `RM_Caps` | 4 | No |
| `RM_Car` | 6 | No |
| `RM_Food` | 7 | No |
| `RM_Shoot` | 8 | No |
| `RM_Surv` | 9 | Record 164 |
| `RM_Run` | 10 | No |
| `RM_Hifi` | 11 | No |
| `RM_Brawl1` | 12 | Record 161 |
| `RM_Brawl5` | 14 | Record 162 |
| `RM_Snuff` | 15 | No |
| `RM_Last` | 16 | No |
| `RM_Muggr` | 18 | No |
| `RM_TagBt` | 19 | Record 166 |
| `RM_Mercy` | 23 | Record 165 |
| `RM_Wchair` | 24 | Record 168 |

## Type 2 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 581 | 92 | 0 | 0 | 1 | 101 |
| 582 | 0 | 0 | 0 | 1 | 102 |
| 583 | 54 | 0 | 0 | 1 | 104 |
| 584 | 31 | 0 | 0 | 1 | 105 |
| 585 | 3 | 0 | 0 | 1 | 107 |
| 586 | 31 | 0 | 0 | 1 | 108 |
| 587 | 20 | 0 | 0 | 1 | 109 |
| 588 | 11 | 0 | 0 | 1 | 110 |
| 589 | 14 | 0 | 15 | 1 | 111 |
| 590 | 83 | 0 | 0 | 1 | 113 |
| 591 | 20 | 0 | 0 | 1 | 114 |
| 592 | 31 | 0 | 9 | 1 | 115 |
| 593 | 93 | 0 | 0 | 1 | 116 |
| 594 | 82 | 0 | 0 | 1 | 118 |
| 595 | 64 | 0 | 0 | 1 | 119 |
| 596 | 64 | 0 | 0 | 1 | 120 |
| 597 | 2 | 0 | 0 | 1 | 121 |
| 598 | 5 | 0 | 7 | 1 | 122 |
| 599 | 55 | 0 | 0 | 1 | 123 |
| 600 | 5 | 0 | 0 | 1 | 124 |
| 601 | 81 | 0 | 0 | 1 | 126 |
| 602 | 34 | 0 | 0 | 1 | 127 |
| 603 | 51 | 0 | 0 | 1 | 128 |
| 604 | 86 | 0 | 0 | 1 | 129 |
| 605 | 64 | 0 | 0 | 1 | 130 |
| 606 | 5 | 0 | 0 | 1 | 131 |
| 607 | 5 | 0 | 0 | 1 | 132 |
| 608 | 9 | 0 | 0 | 1 | 133 |
| 609 | 2 | 0 | 7 | 1 | 134 |

## Type 3 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 170 | 0 | 0 | 0 | 1 | 5 |
| 171 | 11 | 0 | 0 | 2 | 13 |
| 172 | 14 | 1 | 3 | 3 | 21 |
| 173 | 87 | 0 | 0 | 4 | 2 |
| 174 | 3 | 0 | 8 | 5 | 19 |
| 175 | 3 | 0 | 0 | 6 | 10 |
| 176 | 5 | 0 | 0 | 7 | 14 |
| 177 | 14 | 0 | 0 | 8 | 16 |
| 178 | 55 | 0 | 0 | 9 | 7 |
| 179 | 84 | 0 | 0 | 10 | 9 |
| 180 | 55 | 0 | 0 | 11 | 6 |
| 181 | 9 | 0 | 0 | 12 | 20 |
| 182 | 0 | 0 | 0 | 13 | 3 |
| 183 | 3 | 0 | 8 | 14 | 17 |
| 184 | 84 | 0 | 0 | 15 | 8 |
| 185 | 14 | 1 | 1 | 16 | 15 |
| 186 | 82 | 0 | 0 | 17 | 12 |
| 187 | 20 | 0 | 0 | 18 | 11 |
| 188 | 0 | 0 | 0 | 19 | 4 |
| 189 | 34 | 0 | 0 | 20 | 18 |
| 190 | 0 | 0 | 0 | 21 | 1 |
| 191 | 80 | 0 | 2 | 22 | 35 |
| 192 | 84 | 0 | 2 | 23 | 37 |
| 193 | 51 | 0 | 0 | 24 | 32 |
| 194 | 51 | 1 | 1 | 25 | 43 |
| 195 | 9 | 0 | 15 | 26 | 49 |
| 196 | 52 | 0 | 8 | 27 | 39 |
| 197 | 14 | 0 | 0 | 28 | 27 |
| 198 | 83 | 0 | 0 | 29 | 40 |
| 199 | 87 | 1 | 2 | 30 | 22 |
| 200 | 51 | 0 | 7 | 31 | 47 |
| 201 | 20 | 0 | 0 | 32 | 30 |
| 202 | 20 | 0 | 0 | 33 | 44 |
| 203 | 34 | 0 | 12 | 34 | 41 |
| 204 | 86 | 0 | 1 | 35 | 23 |
| 205 | 86 | 0 | 0 | 36 | 31 |
| 206 | 11 | 0 | 0 | 37 | 25 |
| 207 | 11 | 0 | 0 | 38 | 48 |
| 208 | 31 | 0 | 0 | 39 | 26 |
| 209 | 87 | 1 | 3 | 40 | 33 |
| 210 | 34 | 1 | 2 | 41 | 34 |
| 211 | 9 | 0 | 0 | 42 | 45 |
| 212 | 5 | 0 | 0 | 43 | 28 |
| 213 | 11 | 0 | 11 | 44 | 24 |
| 214 | 2 | 0 | 0 | 45 | 29 |
| 215 | 82 | 0 | 0 | 46 | 46 |

## Type 4 records

| Record | Level | Group | Item | Extra | Data | Model |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 216 | 0 | 0 | 0 | 1 | 2 | `warr_cl_gen` |
| 217 | 0 | 0 | 0 | 2 | 6 | `warr_sw_gen` |
| 218 | 0 | 0 | 0 | 3 | 27 | `warr_ve_gen` |
| 219 | 0 | 0 | 0 | 4 | 34 | `warr_sn_gen` |
| 220 | 0 | 0 | 0 | 5 | 16 | `warr_co_gen` |
| 221 | 0 | 0 | 0 | 6 | 22 | `warr_fo_gen` |
| 222 | 0 | 0 | 0 | 7 | 12 | `warr_aj_gen` |
| 223 | 0 | 0 | 0 | 8 | 31 | `warr_re_gen` |
| 224 | 0 | 0 | 0 | 9 | 19 | `warr_cb_gen` |
| 225 | 2 | 1 | 1 | 10 | 220 | `orph_bo_va1` |
| 226 | 2 | 1 | 2 | 11 | 223 | `orph_lt_va` |
| 227 | 2 | 0 | 0 | 12 | 394 | `civl_tr_ma2` |
| 228 | 2 | 0 | 0 | 13 | 396 | `civl_tr_ma4` |
| 229 | 2 | 0 | 0 | 14 | 391 | `civl_tr_fa3` |
| 230 | 2 | 0 | 0 | 15 | 389 | `civl_tr_fa1` |
| 231 | 2 | 0 | 0 | 16 | 397 | `civl_tr_ma5` |
| 232 | 2 | 0 | 0 | 17 | 395 | `civl_tr_ma3` |
| 233 | 2 | 0 | 0 | 18 | 390 | `civl_tr_fa2` |
| 234 | 2 | 0 | 0 | 19 | 393 | `civl_tr_ma1` |
| 235 | 2 | 0 | 0 | 20 | 392 | `civl_tr_fa4` |
| 236 | 2 | 0 | 0 | 21 | 40 | `warr_ty_cv` |
| 237 | 3 | 0 | 8 | 22 | 172 | `elec_bo` |
| 238 | 3 | 0 | 8 | 23 | 175 | `elec_so2` |
| 239 | 3 | 0 | 8 | 24 | 177 | `elec_so4` |
| 240 | 3 | 0 | 8 | 25 | 174 | `elec_so1` |
| 241 | 3 | 0 | 8 | 26 | 176 | `elec_so3` |
| 242 | 3 | 0 | 8 | 27 | 173 | `elec_lt` |
| 243 | 3 | 0 | 0 | 28 | 141 | `hiha_so_vb2` |
| 244 | 3 | 0 | 0 | 29 | 138 | `hiha_so_va1` |
| 245 | 3 | 0 | 0 | 30 | 140 | `hiha_so_vb1` |
| 246 | 3 | 0 | 0 | 31 | 139 | `hiha_so_va2` |
| 247 | 3 | 0 | 0 | 32 | 136 | `hiha_lt` |
| 248 | 3 | 0 | 8 | 33 | 148 | `panz_bo` |
| 249 | 3 | 0 | 8 | 34 | 151 | `panz_so_va2` |
| 250 | 3 | 0 | 8 | 35 | 152 | `panz_so_vb1` |
| 251 | 3 | 0 | 8 | 36 | 153 | `panz_so_vb2` |
| 252 | 3 | 0 | 8 | 37 | 150 | `panz_so_va1` |
| 253 | 3 | 0 | 8 | 38 | 149 | `panz_lt` |
| 254 | 3 | 0 | 8 | 39 | 307 | `civl_sp_ju` |
| 255 | 5 | 1 | 1 | 40 | 119 | `hurr_va` |
| 256 | 5 | 0 | 0 | 41 | 124 | `hurr_so1a` |
| 257 | 5 | 0 | 0 | 42 | 127 | `hurr_so2b` |
| 258 | 5 | 0 | 0 | 43 | 126 | `hurr_so2a` |
| 259 | 5 | 0 | 0 | 44 | 125 | `hurr_so1b` |
| 260 | 5 | 1 | 2 | 45 | 123 | `hurr_lt` |
| 261 | 5 | 1 | 2 | 46 | 122 | `hurr_sa` |
| 262 | 5 | 0 | 0 | 47 | 386 | `civl_eh_ma4` |
| 263 | 5 | 0 | 0 | 48 | 384 | `civl_eh_ma2` |
| 264 | 5 | 0 | 0 | 49 | 385 | `civl_eh_ma3` |
| 265 | 5 | 0 | 0 | 50 | 380 | `civl_eh_fa1` |
| 266 | 5 | 0 | 0 | 51 | 381 | `civl_eh_fa2` |
| 267 | 5 | 0 | 0 | 52 | 382 | `civl_eh_fa3` |
| 268 | 5 | 0 | 0 | 53 | 387 | `civl_eh_ma5` |
| 269 | 5 | 0 | 0 | 54 | 383 | `civl_eh_ma1` |
| 270 | 8 | 0 | 0 | 55 | 91 | `fury_so1` |
| 271 | 8 | 0 | 0 | 56 | 93 | `fury_so3` |
| 272 | 8 | 0 | 0 | 57 | 92 | `fury_so2` |
| 273 | 8 | 0 | 0 | 58 | 94 | `fury_so4` |
| 274 | 8 | 0 | 0 | 59 | 226 | `orph_so_va2` |
| 275 | 8 | 0 | 0 | 60 | 228 | `orph_so_vb2` |
| 276 | 8 | 0 | 0 | 61 | 227 | `orph_so_vb1` |
| 277 | 8 | 0 | 0 | 62 | 225 | `orph_so_va1` |
| 278 | 8 | 0 | 0 | 63 | 224 | `orph_lt_vb` |
| 279 | 8 | 0 | 0 | 64 | 103 | `turn_so3` |
| 280 | 8 | 0 | 0 | 65 | 105 | `turn_so5` |
| 281 | 8 | 0 | 0 | 66 | 104 | `turn_so4` |
| 282 | 8 | 0 | 0 | 67 | 101 | `turn_so1` |
| 283 | 8 | 0 | 0 | 68 | 102 | `turn_so2` |
| 284 | 9 | 0 | 0 | 69 | 214 | `moon_bo` |
| 285 | 9 | 0 | 0 | 70 | 219 | `moon_so2b` |
| 286 | 9 | 0 | 0 | 71 | 217 | `moon_so1b` |
| 287 | 9 | 0 | 0 | 72 | 218 | `moon_so2a` |
| 288 | 9 | 0 | 0 | 73 | 216 | `moon_so1a` |
| 289 | 9 | 0 | 0 | 74 | 215 | `moon_lt` |
| 290 | 9 | 0 | 0 | 75 | 247 | `cops_ty_vb1` |
| 291 | 9 | 0 | 0 | 76 | 248 | `cops_ty_vb2` |
| 292 | 9 | 0 | 0 | 77 | 245 | `cops_ty_va1` |
| 293 | 9 | 0 | 0 | 78 | 358 | `civl_sg1` |
| 294 | 9 | 0 | 0 | 79 | 246 | `cops_ty_va2` |
| 295 | 9 | 0 | 0 | 80 | 359 | `civl_sg2` |
| 296 | 9 | 1 | 1 | 81 | 425 | `civl_scps` |
| 297 | 9 | 0 | 15 | 82 | 432 | `civl_sp_ma1` |
| 298 | 9 | 0 | 15 | 83 | 431 | `civl_sp_ma5` |
| 299 | 9 | 0 | 15 | 84 | 434 | `civl_sp_ma2` |
| 300 | 9 | 0 | 15 | 85 | 435 | `civl_sp_ma4` |
| 301 | 9 | 0 | 15 | 86 | 433 | `civl_sp_ma3` |
| 302 | 11 | 1 | 1 | 87 | 128 | `bopp_bo` |
| 303 | 11 | 0 | 0 | 88 | 133 | `bopp_so1d` |
| 304 | 11 | 0 | 0 | 89 | 130 | `bopp_so1a` |
| 305 | 11 | 0 | 0 | 90 | 132 | `bopp_so1c` |
| 306 | 11 | 0 | 0 | 91 | 134 | `bopp_so2a` |
| 307 | 11 | 0 | 0 | 92 | 131 | `bopp_so1b` |
| 308 | 11 | 0 | 0 | 93 | 129 | `bopp_lt` |
| 309 | 11 | 0 | 0 | 94 | 313 | `bopp_fa1` |
| 310 | 11 | 0 | 0 | 95 | 314 | `bopp_fa1` |
| 311 | 11 | 0 | 0 | 96 | 337 | `civl_hl_tr` |
| 312 | 11 | 0 | 0 | 97 | 342 | `civl_hl_fc5` |
| 313 | 11 | 0 | 0 | 98 | 338 | `civl_hl_fc1` |
| 314 | 11 | 0 | 0 | 99 | 339 | `civl_hl_fc2` |
| 315 | 11 | 0 | 0 | 100 | 340 | `civl_hl_fc3` |
| 316 | 11 | 0 | 0 | 101 | 341 | `civl_hl_fc4` |
| 317 | 11 | 0 | 0 | 102 | 324 | `civl_hl_ma1` |
| 318 | 11 | 0 | 0 | 103 | 326 | `civl_hl_ma3` |
| 319 | 11 | 0 | 0 | 104 | 328 | `civl_hl_ma5` |
| 320 | 11 | 0 | 0 | 105 | 327 | `civl_hl_ma4` |
| 321 | 11 | 0 | 0 | 106 | 325 | `civl_hl_ma2` |
| 322 | 11 | 0 | 0 | 107 | 17 | `warr_co_cv` |
| 323 | 11 | 0 | 11 | 108 | 268 | `char_db1` |
| 324 | 11 | 0 | 11 | 109 | 269 | `char_db2` |
| 325 | 11 | 0 | 11 | 110 | 357 | `civl_hl_st3a` |
| 326 | 11 | 0 | 11 | 111 | 354 | `civl_hl_st1b` |
| 327 | 11 | 0 | 11 | 112 | 353 | `civl_hl_st1a` |
| 328 | 11 | 0 | 11 | 113 | 355 | `civl_hl_st2a` |
| 329 | 11 | 0 | 11 | 114 | 356 | `civl_hl_st2b` |
| 330 | 14 | 0 | 0 | 115 | 373 | `civl_bn_ma2` |
| 331 | 14 | 0 | 0 | 116 | 370 | `civl_bn_fa3` |
| 332 | 14 | 0 | 0 | 117 | 371 | `civl_bn_fa4` |
| 333 | 14 | 0 | 0 | 118 | 372 | `civl_bn_ma1` |
| 334 | 14 | 0 | 0 | 119 | 376 | `civl_bn_mb1` |
| 335 | 14 | 0 | 0 | 120 | 375 | `civl_bn_ma4` |
| 336 | 14 | 0 | 0 | 121 | 374 | `civl_bn_ma3` |
| 337 | 14 | 0 | 0 | 122 | 369 | `civl_bn_fa2` |
| 338 | 14 | 0 | 0 | 123 | 368 | `civl_bn_fa1` |
| 339 | 14 | 1 | 3 | 124 | 253 | `cops_va1` |
| 340 | 14 | 1 | 3 | 125 | 255 | `cops_va3` |
| 341 | 14 | 1 | 3 | 126 | 254 | `cops_va2` |
| 342 | 14 | 1 | 3 | 127 | 264 | `cops_vd2` |
| 343 | 14 | 1 | 3 | 128 | 256 | `cops_vb1` |
| 344 | 14 | 1 | 3 | 129 | 263 | `cops_vd1` |
| 345 | 14 | 1 | 3 | 130 | 258 | `cops_vb3` |
| 346 | 14 | 1 | 3 | 131 | 257 | `cops_vb2` |
| 347 | 14 | 1 | 2 | 132 | 154 | `jsbs_bo` |
| 348 | 14 | 0 | 0 | 133 | 158 | `jsbs_so3` |
| 349 | 14 | 0 | 0 | 134 | 156 | `jsbs_so1` |
| 350 | 14 | 0 | 0 | 135 | 159 | `jsbs_so4` |
| 351 | 14 | 0 | 0 | 136 | 157 | `jsbs_so2` |
| 352 | 14 | 0 | 0 | 137 | 155 | `jsbs_lt` |
| 353 | 14 | 1 | 1 | 138 | 142 | `sara_bo` |
| 354 | 14 | 1 | 1 | 139 | 147 | `sara_so2b` |
| 355 | 14 | 1 | 1 | 140 | 145 | `sara_so1b` |
| 356 | 14 | 1 | 1 | 141 | 144 | `sara_so1a` |
| 357 | 14 | 1 | 1 | 142 | 146 | `sara_so2a` |
| 358 | 14 | 1 | 1 | 143 | 143 | `sara_lt` |
| 359 | 20 | 0 | 0 | 144 | 405 | `civl_ct_ma2` |
| 360 | 20 | 0 | 0 | 145 | 406 | `civl_ct_fa3` |
| 361 | 20 | 0 | 0 | 146 | 407 | `civl_ct_ma3` |
| 362 | 20 | 0 | 0 | 147 | 404 | `civl_ct_fa2` |
| 363 | 20 | 0 | 0 | 148 | 408 | `civl_ct_fa4` |
| 364 | 20 | 0 | 0 | 149 | 410 | `civl_ct_ma5` |
| 365 | 20 | 0 | 0 | 150 | 402 | `civl_ct_fa1` |
| 366 | 20 | 0 | 0 | 151 | 403 | `civl_ct_ma1` |
| 367 | 20 | 0 | 0 | 152 | 409 | `civl_ct_ma4` |
| 368 | 20 | 0 | 0 | 153 | 415 | `civl_ct_ac` |
| 369 | 20 | 0 | 0 | 154 | 115 | `civl_monk2` |
| 370 | 20 | 0 | 0 | 155 | 117 | `civl_monk_bo2` |
| 371 | 20 | 0 | 0 | 156 | 116 | `civl_monk_bo1` |
| 372 | 20 | 0 | 0 | 157 | 414 | `civl_ct_eb` |
| 373 | 20 | 0 | 0 | 158 | 114 | `civl_monk1` |
| 374 | 20 | 0 | 0 | 159 | 411 | `civl_ct_ck1` |
| 375 | 20 | 0 | 0 | 160 | 412 | `civl_ct_ck2` |
| 376 | 20 | 0 | 0 | 161 | 413 | `civl_ct_ck3` |
| 377 | 20 | 0 | 9 | 162 | 24 | `warr_fo_cv2` |
| 378 | 20 | 1 | 1 | 163 | 106 | `sava_bo` |
| 379 | 20 | 0 | 0 | 164 | 111 | `sava_so1b` |
| 380 | 20 | 0 | 0 | 165 | 112 | `sava_so2a` |
| 381 | 20 | 0 | 0 | 166 | 110 | `sava_so1a` |
| 382 | 20 | 0 | 0 | 167 | 113 | `sava_so2b` |
| 383 | 20 | 0 | 0 | 168 | 107 | `sava_lt` |
| 384 | 20 | 0 | 0 | 169 | 108 | `sava_lt1` |
| 385 | 20 | 0 | 0 | 170 | 109 | `sava_lt2` |
| 386 | 20 | 0 | 9 | 171 | 29 | `warr_ve_cv` |
| 387 | 20 | 1 | 1 | 172 | 23 | `warr_fo_cv1` |
| 388 | 31 | 1 | 1 | 173 | 259 | `cops_vc1` |
| 389 | 31 | 0 | 0 | 174 | 360 | `civl_pl_ma1` |
| 390 | 31 | 0 | 0 | 175 | 362 | `civl_pl_ma3` |
| 391 | 31 | 0 | 0 | 176 | 364 | `civl_pl_ma6` |
| 392 | 31 | 0 | 0 | 177 | 361 | `civl_pl_ma2` |
| 393 | 31 | 0 | 0 | 178 | 363 | `civl_pl_ma5` |
| 394 | 31 | 0 | 0 | 179 | 330 | `civl_hl_fb2` |
| 395 | 31 | 0 | 0 | 180 | 329 | `civl_hl_fb1` |
| 396 | 31 | 0 | 0 | 181 | 365 | `civl_pl_ma8` |
| 397 | 31 | 0 | 0 | 182 | 367 | `civl_pl_fa2` |
| 398 | 82 | 0 | 0 | 183 | 288 | `civl_gk` |
| 399 | 82 | 0 | 0 | 184 | 296 | `civl_cw_ma1` |
| 400 | 82 | 0 | 0 | 185 | 299 | `civl_cw_mb2` |
| 401 | 82 | 0 | 0 | 186 | 298 | `civl_cw_mb1` |
| 402 | 82 | 0 | 0 | 187 | 297 | `civl_cw_ma2` |
| 403 | 34 | 1 | 1 | 188 | 90 | `fury_lt` |
| 404 | 34 | 0 | 0 | 189 | 166 | `juda_bo` |
| 405 | 34 | 0 | 0 | 190 | 167 | `juda_lt` |
| 406 | 34 | 0 | 0 | 191 | 171 | `juda_so_vb2` |
| 407 | 34 | 0 | 0 | 192 | 170 | `juda_so_vb1` |
| 408 | 34 | 0 | 0 | 193 | 168 | `juda_so_va1` |
| 409 | 34 | 0 | 0 | 194 | 169 | `juda_so_va2` |
| 410 | 34 | 1 | 2 | 195 | 452 | `civl_rs_fa2` |
| 411 | 34 | 1 | 2 | 196 | 450 | `civl_rs_ma5` |
| 412 | 34 | 1 | 2 | 197 | 449 | `civl_rs_ma4` |
| 413 | 34 | 1 | 2 | 198 | 448 | `civl_rs_ma3` |
| 414 | 34 | 1 | 2 | 199 | 446 | `civl_rs_ma1` |
| 415 | 34 | 1 | 2 | 200 | 453 | `civl_rs_fa3` |
| 416 | 34 | 1 | 2 | 201 | 451 | `civl_rs_fa1` |
| 417 | 34 | 1 | 2 | 202 | 447 | `civl_rs_ma2` |
| 418 | 34 | 0 | 12 | 203 | 294 | `civl_hl_b2` |
| 419 | 34 | 0 | 12 | 204 | 295 | `civl_hl_b3` |
| 420 | 34 | 0 | 12 | 205 | 424 | `civl_pl_st3` |
| 421 | 34 | 0 | 12 | 206 | 423 | `civl_pl_st2` |
| 422 | 34 | 0 | 12 | 207 | 422 | `civl_pl_st1` |
| 423 | 34 | 0 | 12 | 208 | 293 | `civl_hl_b1` |
| 424 | 34 | 0 | 12 | 209 | 366 | `civl_pl_fa1` |
| 425 | 34 | 0 | 12 | 210 | 420 | `civl_co_ma4` |
| 426 | 34 | 0 | 12 | 211 | 321 | `civl_gc_bt` |
| 427 | 51 | 0 | 7 | 212 | 275 | `civl_bm_vb4` |
| 428 | 51 | 0 | 7 | 213 | 273 | `civl_bm_vb2` |
| 429 | 51 | 0 | 7 | 214 | 280 | `civl_bm_vb9` |
| 430 | 51 | 0 | 7 | 215 | 279 | `civl_bm_vb8` |
| 431 | 51 | 0 | 7 | 216 | 277 | `civl_bm_vb6` |
| 432 | 51 | 0 | 7 | 217 | 271 | `civl_bm_va2` |
| 433 | 51 | 0 | 0 | 218 | 428 | `civl_dr_ma4` |
| 434 | 51 | 0 | 0 | 219 | 427 | `civl_dr_ma2` |
| 435 | 51 | 0 | 0 | 220 | 426 | `civl_dr_ma1` |
| 436 | 51 | 0 | 0 | 221 | 430 | `civl_dr_ma5` |
| 437 | 51 | 0 | 0 | 222 | 429 | `civl_dr_ma3` |
| 438 | 51 | 1 | 1 | 223 | 437 | `civl_we_ma1` |
| 439 | 51 | 1 | 1 | 224 | 436 | `civl_we_ma2` |
| 440 | 51 | 1 | 1 | 225 | 439 | `civl_we_ma4` |
| 441 | 51 | 1 | 1 | 226 | 440 | `civl_we_ma5` |
| 442 | 51 | 1 | 1 | 227 | 438 | `civl_we_ma3` |
| 443 | 51 | 1 | 2 | 228 | 99 | `turn_bo` |
| 444 | 51 | 1 | 2 | 229 | 100 | `turn_lt` |
| 445 | 52 | 0 | 8 | 230 | 344 | `civl_hl_bt1` |
| 446 | 52 | 0 | 8 | 231 | 346 | `civl_hl_bt3` |
| 447 | 52 | 0 | 8 | 232 | 348 | `civl_hl_bt5` |
| 448 | 52 | 0 | 8 | 233 | 347 | `civl_hl_bt4` |
| 449 | 52 | 0 | 8 | 234 | 345 | `civl_hl_bt2` |
| 450 | 52 | 0 | 8 | 235 | 343 | `civl_bt1` |
| 451 | 52 | 1 | 1 | 236 | 221 | `orph_me` |
| 452 | 54 | 0 | 2 | 237 | 89 | `fury_bo` |
| 453 | 55 | 0 | 2 | 238 | 237 | `lizz_bo` |
| 454 | 55 | 0 | 0 | 239 | 243 | `lizz_so_vb2` |
| 455 | 55 | 0 | 0 | 240 | 240 | `lizz_so_va2` |
| 456 | 55 | 0 | 0 | 241 | 239 | `lizz_so_va1` |
| 457 | 55 | 0 | 0 | 242 | 241 | `lizz_so_va3` |
| 458 | 55 | 0 | 0 | 243 | 242 | `lizz_so_vb1` |
| 459 | 55 | 0 | 0 | 244 | 238 | `lizz_lt` |
| 460 | 55 | 0 | 0 | 245 | 235 | `punk_so2a` |
| 461 | 55 | 0 | 0 | 246 | 234 | `punk_so1b` |
| 462 | 55 | 0 | 0 | 247 | 233 | `punk_so1a` |
| 463 | 55 | 0 | 0 | 248 | 236 | `punk_so2b` |
| 464 | 55 | 0 | 0 | 249 | 231 | `punk_lt` |
| 465 | 55 | 0 | 0 | 250 | 229 | `punk_bo` |
| 466 | 64 | 0 | 0 | 251 | 73 | `riff_so_vc1` |
| 467 | 64 | 0 | 0 | 252 | 74 | `riff_so_vc2` |
| 468 | 64 | 0 | 0 | 253 | 75 | `riff_so_vc3` |
| 469 | 64 | 0 | 0 | 254 | 76 | `riff_so_vc4` |
| 470 | 64 | 0 | 0 | 255 | 66 | `riff_lt` |
| 471 | 64 | 0 | 0 | 256 | 67 | `riff_em` |
| 472 | 64 | 0 | 0 | 257 | 350 | `civl_hl_dj1` |
| 473 | 64 | 0 | 0 | 258 | 63 | `riff_cy_ghost` |
| 474 | 64 | 0 | 0 | 259 | 25 | `warr_fo_ghost` |
| 475 | 64 | 0 | 0 | 260 | 3 | `warr_cl_ghost` |
| 476 | 64 | 0 | 0 | 261 | 8 | `warr_sw_ghost` |
| 477 | 64 | 0 | 0 | 262 | 14 | `warr_aj_ghost` |
| 478 | 80 | 0 | 2 | 263 | 51 | `warr_so_va3` |
| 479 | 80 | 0 | 2 | 264 | 50 | `warr_so_va2` |
| 480 | 80 | 0 | 2 | 265 | 52 | `warr_so_vb1` |
| 481 | 80 | 0 | 2 | 266 | 49 | `warr_so_va1` |
| 482 | 80 | 0 | 2 | 267 | 53 | `warr_so_vb2` |
| 483 | 80 | 0 | 2 | 268 | 54 | `warr_so_vb3` |
| 484 | 80 | 0 | 2 | 269 | 43 | `warr_ma` |
| 485 | 80 | 0 | 2 | 270 | 41 | `warr_jn` |
| 486 | 80 | 0 | 2 | 271 | 38 | `warr_ty` |
| 487 | 80 | 0 | 2 | 272 | 45 | `warr_ly` |
| 488 | 81 | 0 | 0 | 273 | 135 | `hiha_bo` |
| 489 | 81 | 0 | 2 | 274 | 137 | `hiha_lt_ft` |
| 490 | 82 | 0 | 0 | 275 | 261 | `cops_ia1` |
| 491 | 82 | 0 | 0 | 276 | 262 | `cops_ia2` |
| 492 | 82 | 0 | 0 | 277 | 189 | `dest_cl` |
| 493 | 82 | 0 | 0 | 278 | 191 | `dest_ve` |
| 494 | 82 | 0 | 9 | 279 | 161 | `samo_lt` |
| 495 | 82 | 0 | 9 | 280 | 160 | `samo_bo` |
| 496 | 82 | 0 | 0 | 281 | 162 | `samo_so_va1` |
| 497 | 82 | 0 | 0 | 282 | 164 | `samo_so_va3` |
| 498 | 82 | 0 | 0 | 283 | 163 | `samo_so_va2` |
| 499 | 82 | 0 | 0 | 284 | 165 | `samo_so_va4` |
| 500 | 82 | 0 | 0 | 285 | 300 | `civl_co_lf1` |
| 501 | 82 | 0 | 0 | 286 | 301 | `civl_co_lf2` |
| 502 | 82 | 0 | 0 | 287 | 302 | `civl_co_lf3` |
| 503 | 83 | 0 | 0 | 288 | 317 | `civl_hl_br2` |
| 504 | 83 | 0 | 0 | 289 | 319 | `civl_hl_br4` |
| 505 | 83 | 0 | 0 | 290 | 320 | `civl_hl_br5` |
| 506 | 83 | 0 | 0 | 291 | 322 | `civl_hl_dm1` |
| 507 | 83 | 0 | 0 | 292 | 318 | `civl_hl_br3` |
| 508 | 83 | 0 | 0 | 293 | 323 | `civl_hl_dm2` |
| 509 | 83 | 0 | 0 | 294 | 316 | `civl_hl_br1` |
| 510 | 83 | 1 | 3 | 295 | 13 | `warr_aj_cv` |
| 511 | 83 | 1 | 2 | 296 | 35 | `warr_sn_cv` |
| 512 | 83 | 0 | 0 | 297 | 187 | `dest_le_bare` |
| 513 | 83 | 0 | 7 | 298 | 61 | `warr_dest_le` |
| 514 | 83 | 0 | 0 | 299 | 206 | `char_lcgf_vest` |
| 515 | 83 | 0 | 7 | 300 | 205 | `char_lcgf` |
| 516 | 84 | 0 | 0 | 301 | 62 | `riff_cy` |
| 517 | 84 | 0 | 0 | 302 | 64 | `riff_bo_va1` |
| 518 | 84 | 0 | 0 | 303 | 68 | `riff_so_va1` |
| 519 | 84 | 0 | 0 | 304 | 69 | `riff_so_va2` |
| 520 | 84 | 0 | 0 | 305 | 70 | `riff_so_va1` |
| 521 | 84 | 0 | 0 | 306 | 71 | `riff_so_vb1` |
| 522 | 84 | 0 | 0 | 307 | 72 | `riff_so_vb2` |
| 523 | 84 | 0 | 0 | 308 | 77 | `rogu_bo` |
| 524 | 84 | 0 | 0 | 309 | 83 | `rogu_so_vb1` |
| 525 | 84 | 0 | 0 | 310 | 82 | `rogu_so_va2` |
| 526 | 84 | 0 | 0 | 311 | 84 | `rogu_so_vb2` |
| 527 | 84 | 0 | 0 | 312 | 81 | `rogu_so_va1` |
| 528 | 84 | 0 | 0 | 313 | 80 | `rogu_lt` |
| 529 | 84 | 0 | 2 | 314 | 260 | `cops_uc1` |
| 530 | 84 | 0 | 2 | 315 | 310 | `civl_prom_fa` |
| 531 | 84 | 0 | 2 | 316 | 311 | `civl_prom_fb` |
| 532 | 84 | 0 | 2 | 317 | 222 | `orph_me_vb` |
| 533 | 84 | 0 | 2 | 318 | 308 | `civl_prom_ma` |
| 534 | 84 | 0 | 2 | 319 | 309 | `civl_prom_mb` |
| 535 | 84 | 0 | 2 | 320 | 306 | `civl_csg` |
| 536 | 86 | 0 | 1 | 321 | 290 | `civl_co_sh` |
| 537 | 86 | 0 | 1 | 322 | 292 | `civl_co_do` |
| 538 | 86 | 0 | 1 | 323 | 388 | `civl_tr_cw` |
| 539 | 86 | 0 | 1 | 324 | 289 | `civl_co_ic` |
| 540 | 86 | 0 | 1 | 325 | 441 | `civl_co_di` |
| 541 | 86 | 0 | 1 | 326 | 419 | `civl_co_ma3` |
| 542 | 86 | 0 | 0 | 327 | 416 | `civl_st` |
| 543 | 86 | 0 | 0 | 328 | 417 | `civl_co_ma1` |
| 544 | 86 | 0 | 0 | 329 | 418 | `civl_co_ma2` |
| 545 | 86 | 0 | 0 | 330 | 315 | `civl_hd` |
| 546 | 86 | 0 | 0 | 331 | 304 | `civl_sk_pe` |
| 547 | 86 | 0 | 0 | 332 | 303 | `civl_ch` |
| 548 | 86 | 0 | 0 | 333 | 305 | `civl_tw` |
| 549 | 86 | 0 | 0 | 334 | 312 | `civl_gypsy` |
| 550 | 87 | 1 | 2 | 335 | 281 | `civl_bm_vb11` |
| 551 | 87 | 1 | 2 | 336 | 276 | `civl_bm_vb5` |
| 552 | 87 | 1 | 2 | 337 | 274 | `civl_bm_vb3` |
| 553 | 87 | 1 | 2 | 338 | 278 | `civl_bm_vb7` |
| 554 | 87 | 1 | 2 | 339 | 272 | `civl_bm_vb1` |
| 555 | 87 | 1 | 2 | 340 | 270 | `civl_bm_va1` |
| 556 | 87 | 0 | 0 | 341 | 192 | `dest_so1a` |
| 557 | 87 | 0 | 0 | 342 | 194 | `dest_so1b` |
| 558 | 87 | 0 | 0 | 343 | 197 | `dest_so2a` |
| 559 | 87 | 0 | 0 | 344 | 199 | `dest_so2b` |
| 560 | 87 | 0 | 17 | 345 | 186 | `dest_lt_le` |
| 561 | 87 | 1 | 3 | 346 | 331 | `civl_hl_ho1` |
| 562 | 87 | 1 | 3 | 347 | 333 | `hook_01` |
| 563 | 87 | 1 | 3 | 348 | 334 | `hook_03` |
| 564 | 87 | 1 | 3 | 349 | 332 | `civl_hl_ho2` |
| 565 | 87 | 1 | 3 | 350 | 443 | `civl_pimp2` |
| 566 | 87 | 1 | 3 | 351 | 445 | `civl_pl_pm` |
| 567 | 87 | 1 | 3 | 352 | 442 | `civl_pimp1` |
| 568 | 87 | 1 | 4 | 353 | 179 | `dest_bo_tg` |
| 569 | 87 | 0 | 17 | 354 | 182 | `dest_lt_v1` |
| 570 | 87 | 0 | 17 | 355 | 183 | `dest_lt_v2` |
| 571 | 87 | 0 | 17 | 356 | 184 | `dest_lt_v3` |
| 572 | 92 | 0 | 6 | 357 | 20 | `warr_cb_cv` |
| 573 | 92 | 1 | 1 | 358 | 7 | `warr_sw_cv` |
| 574 | 93 | 0 | 8 | 359 | 178 | `dest_bo` |
| 575 | 93 | 1 | 2 | 360 | 185 | `dest_lt_be` |
| 576 | 93 | 0 | 0 | 361 | 180 | `dest_lt` |
| 577 | 93 | 0 | 0 | 362 | 196 | `dest_so1c` |
| 578 | 95 | 1 | 27 | 363 | 421 | `civl_co_ma5` |
| 579 | 95 | 1 | 7 | 364 | 121 | `civl_abe` |
| 580 | 99 | 0 | 5 | 365 | 32 | `warr_re_cv` |

- Two pairs of the character ids in this table share a model name: 313 and 314 are both `bopp_fa1`, and 68 and 70
  are both `riff_so_va1`.

## Type 5 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 155 | 34 | 0 | 0 | 29 | 1 |
| 156 | 2 | 0 | 0 | 30 | 2 |
| 157 | 5 | 0 | 0 | 31 | 3 |
| 158 | 81 | 0 | 0 | 32 | 4 |
| 159 | 31 | 0 | 0 | 33 | 5 |
| 160 | 84 | 0 | 0 | 34 | 6 |

## Type 6 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 28 | 1 | 0 | 2 | 1 | 1 |
| 29 | 3 | 0 | 0 | 2 | 2 |
| 30 | 1 | 0 | 0 | 3 | 3 |
| 31 | 1 | 0 | 1 | 4 | 4 |
| 32 | 95 | 1 | 3 | 5 | 5 |
| 33 | 95 | 1 | 6 | 6 | 6 |
| 34 | 95 | 1 | 26 | 7 | 7 |
| 35 | 95 | 1 | 28 | 8 | 8 |
| 36 | 95 | 1 | 23 | 9 | 9 |
| 37 | 95 | 1 | 25 | 10 | 10 |
| 38 | 95 | 1 | 9 | 11 | 11 |
| 39 | 95 | 1 | 10 | 12 | 12 |
| 40 | 95 | 1 | 22 | 13 | 13 |
| 41 | 95 | 1 | 27 | 14 | 14 |
| 42 | 95 | 1 | 21 | 15 | 15 |
| 43 | 95 | 1 | 30 | 16 | 16 |
| 44 | 95 | 1 | 7 | 17 | 18 |
| 45 | 4 | 0 | 1 | 18 | 21 |
| 46 | 4 | 0 | 2 | 19 | 22 |
| 47 | 4 | 0 | 3 | 20 | 20 |
| 48 | 4 | 0 | 4 | 21 | 17 |
| 49 | 4 | 0 | 5 | 22 | 19 |
| 56 | 95 | 1 | 1 | 23 | 23 |
| 57 | 95 | 1 | 2 | 24 | 24 |
| 58 | 95 | 1 | 3 | 25 | 25 |
| 59 | 95 | 1 | 4 | 26 | 26 |
| 60 | 95 | 1 | 5 | 27 | 27 |
| 61 | 95 | 1 | 6 | 28 | 28 |

- The data value equals the extra value in every type 6 record except records 44, 45, 46, 48 and 49.

## Type 7 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | 34 | 0 | 0 | 0 | 82 |
| 7 | 2 | 0 | 0 | 0 | 92 |
| 10 | 5 | 0 | 0 | 0 | 83 |
| 12 | 81 | 0 | 0 | 0 | 20 |
| 16 | 31 | 0 | 0 | 0 | 11 |

## Type 8 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 50 | 0 | 0 | 0 | 22 | 1 |
| 51 | 34 | 0 | 0 | 22 | 2 |
| 52 | 2 | 0 | 0 | 22 | 3 |
| 53 | 93 | 0 | 0 | 22 | 4 |
| 54 | 31 | 0 | 0 | 22 | 5 |
| 55 | 9 | 0 | 0 | 22 | 6 |
| 62 | 81 | 0 | 0 | 28 | 7 |
| 73 | 84 | 0 | 0 | 28 | 21 |
| 74 | 34 | 0 | 0 | 28 | 22 |
| 75 | 87 | 0 | 0 | 28 | 23 |
| 76 | 31 | 0 | 0 | 28 | 25 |
| 77 | 3 | 0 | 0 | 28 | 26 |
| 78 | 81 | 0 | 0 | 28 | 27 |
| 79 | 84 | 0 | 0 | 28 | 28 |
| 80 | 81 | 0 | 0 | 28 | 29 |
| 81 | 3 | 0 | 0 | 28 | 30 |
| 116 | 95 | 1 | 81 | 28 | 81 |
| 117 | 95 | 1 | 84 | 28 | 84 |
| 118 | 95 | 1 | 87 | 28 | 87 |
| 119 | 95 | 1 | 90 | 28 | 90 |
| 120 | 95 | 1 | 82 | 28 | 82 |
| 121 | 95 | 1 | 85 | 28 | 85 |
| 122 | 95 | 1 | 88 | 28 | 88 |
| 123 | 95 | 1 | 91 | 28 | 91 |
| 124 | 95 | 1 | 83 | 28 | 83 |
| 125 | 95 | 1 | 86 | 28 | 86 |
| 126 | 95 | 1 | 89 | 28 | 89 |
| 127 | 95 | 1 | 92 | 28 | 92 |

## Type 9 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 23 | 84 | 0 | 0 | 0 | 60 |
| 24 | 60 | 0 | 0 | 0 | 61 |
| 25 | 61 | 0 | 0 | 0 | 62 |
| 26 | 62 | 0 | 0 | 0 | 63 |
| 27 | 63 | 0 | 0 | 0 | 64 |

## Type 10 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 63 | 0 | 0 | 0 | 28 | 11 |
| 64 | 95 | 1 | 11 | 28 | 12 |
| 65 | 95 | 1 | 12 | 28 | 13 |
| 66 | 95 | 1 | 13 | 28 | 14 |
| 67 | 95 | 1 | 14 | 28 | 15 |
| 68 | 95 | 1 | 15 | 28 | 16 |
| 69 | 95 | 1 | 16 | 28 | 17 |
| 70 | 95 | 1 | 17 | 28 | 18 |
| 71 | 95 | 1 | 18 | 28 | 19 |
| 72 | 95 | 1 | 19 | 28 | 20 |
| 82 | 0 | 0 | 0 | 28 | 31 |
| 83 | 95 | 1 | 31 | 28 | 32 |
| 84 | 95 | 1 | 32 | 28 | 33 |
| 85 | 95 | 1 | 33 | 28 | 34 |
| 86 | 95 | 1 | 34 | 28 | 35 |
| 87 | 95 | 1 | 35 | 28 | 36 |
| 88 | 95 | 1 | 36 | 28 | 37 |
| 89 | 95 | 1 | 37 | 28 | 38 |
| 90 | 95 | 1 | 38 | 28 | 39 |
| 91 | 95 | 1 | 39 | 28 | 40 |
| 92 | 0 | 0 | 0 | 28 | 41 |
| 93 | 95 | 1 | 41 | 28 | 42 |
| 94 | 95 | 1 | 42 | 28 | 43 |
| 95 | 95 | 1 | 43 | 28 | 44 |
| 96 | 95 | 1 | 44 | 28 | 45 |
| 97 | 95 | 1 | 45 | 28 | 46 |
| 98 | 95 | 1 | 46 | 28 | 47 |
| 99 | 95 | 1 | 47 | 28 | 48 |
| 100 | 95 | 1 | 48 | 28 | 49 |
| 101 | 95 | 1 | 49 | 28 | 50 |
| 102 | 0 | 0 | 0 | 28 | 51 |
| 103 | 95 | 1 | 51 | 28 | 52 |
| 104 | 95 | 1 | 52 | 28 | 53 |
| 105 | 95 | 1 | 53 | 28 | 54 |
| 106 | 95 | 1 | 54 | 28 | 55 |
| 107 | 95 | 1 | 55 | 28 | 56 |
| 108 | 95 | 1 | 56 | 28 | 57 |
| 109 | 95 | 1 | 57 | 28 | 58 |
| 110 | 95 | 1 | 58 | 28 | 59 |
| 111 | 95 | 1 | 59 | 28 | 60 |
| 112 | 95 | 1 | 60 | 28 | 71 |
| 113 | 95 | 1 | 40 | 28 | 72 |
| 114 | 95 | 1 | 50 | 28 | 73 |
| 115 | 95 | 1 | 20 | 28 | 74 |

## Type 11 records

| Record | Level | Group | Item | Extra | Data |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 128 | 0 | 0 | 0 | 28 | 0 |
| 129 | 34 | 0 | 0 | 28 | 1 |
| 130 | 80 | 0 | 0 | 28 | 2 |
| 131 | 80 | 0 | 0 | 28 | 3 |
| 132 | 0 | 0 | 0 | 28 | 4 |
| 133 | 0 | 0 | 0 | 28 | 5 |
| 134 | 80 | 0 | 0 | 28 | 6 |
| 135 | 34 | 0 | 0 | 28 | 7 |
| 136 | 80 | 0 | 0 | 28 | 8 |
| 137 | 34 | 0 | 0 | 28 | 9 |
| 138 | 87 | 0 | 0 | 28 | 10 |
| 139 | 93 | 0 | 0 | 28 | 11 |
| 140 | 80 | 0 | 0 | 28 | 12 |
| 141 | 80 | 0 | 0 | 28 | 13 |
| 142 | 34 | 0 | 0 | 28 | 14 |
| 143 | 5 | 0 | 0 | 28 | 15 |
| 144 | 80 | 0 | 0 | 28 | 16 |
| 145 | 80 | 0 | 0 | 28 | 17 |
| 146 | 0 | 0 | 0 | 28 | 18 |
| 147 | 99 | 0 | 0 | 28 | 19 |
| 148 | 99 | 0 | 0 | 28 | 20 |
| 149 | 87 | 0 | 0 | 28 | 21 |
| 150 | 80 | 0 | 0 | 28 | 22 |
| 151 | 3 | 0 | 0 | 28 | 23 |
| 152 | 80 | 0 | 0 | 28 | 24 |
| 153 | 87 | 0 | 0 | 28 | 25 |
| 154 | 2 | 0 | 0 | 28 | 26 |

## Acceptance checks

| Outcome |
| --- |
| The PS2 disc's `global.lua` (archive entry 4,105, 169,601 bytes) holds an `Unlockables` list of 610 records |
| Every record has exactly six numbers |
| The record counts per type, 0 to 11, are 18, 9, 29, 46, 365, 6, 28, 5, 28, 5, 44 and 27 |
| Record 0 is (level 0, group 0, item 0, type 0, extra 0, data 99) |
| Record 609, the last, is (level 2, group 0, item 7, type 2, extra 1, data 134) |
| No two records of one type share a data value |
| The 610 records use 153 distinct (level, group, item) keys |
| Every type 4 data value is a character id defined in `config_preload2.lua`, which defines 449 characters |
| Record 216 opens character 2, whose model is `warr_cl_gen` |
| Every type 0, 2, 7 and 9 data value has a matching `level<n>.lua` script in the archive |
| The type 0 records chain from level 0 to level 84 through 18 records |
| The type 1 data of record 169 is written as `RM_Koth`, a constant whose value is 2 |

## Open questions

- What the data values of types 3, 5, 6, 8, 10 and 11 stand for is not identified from the disc's data files. The
  type 6 records (28 to 49 and 56 to 61) are on the disc but were missing from the old research.
- The Xbox disc's copy of `global.lua` and `config_preload2.lua` was not compared; differences between the versions
  are not known.
- What levels 1, 4, 8 and 95 stand for as record levels is not confirmed: level 4 and level 8 have no `level<n>.lua`
  script on the PS2 disc.
- Which records are open on a new save, and which scripts open or test which keys, is not given here.
- The meaning of the extra field beyond the per-type pattern shown in "Record types" is not identified.
