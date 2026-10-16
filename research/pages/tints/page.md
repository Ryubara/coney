# Object and character tints

Colour tints in The Warriors (PS2, NTSC-U `SLUS_212.15`): the tint an object gets when a script spawns it, colours
scripts set on objects and characters later, and the per-level colour laid over the whole view. Values come from
the Lua scripts in `WARRIORS.WAD` on the PS2 disc (467 compiled Lua 4.0 chunks, numbered by their place in
`WARRIORS.DIR`) and from ten PCSX2 savestates taken in level 99. Full-screen effects are on the page
`screen-effects`.

## Tint colour word

| Property | Value |
| --- | --- |
| Size | 32 bits |
| Written as a number | `0xRRGGBBAA`: red in the top byte, alpha in the bottom byte |
| Bytes in memory (little-endian) | alpha, blue, green, red |
| No tint | `0xFFFFFFFF` (white, opaque) |
| Effect on the model | applied to the model's colour; white leaves the model unchanged |

A spawn without a colour gets `0xFFFFFFFF`. Script tables of four values are taken in the order red, green, blue,
alpha.

## Spawn tints

Object spawns in the scripts pass the tint as their seventh value. The disc holds 2,871 spawn uses; 2,506 pass
`0xFFFFFFFF`, one passes nothing (also white), and 364 pass one of the 33 other tints below.

| Tint | R, G, B, A | Uses | Objects | Scripts |
| --- | --- | --- | --- | --- |
| `0xA49E98FF` | 164, 158, 152, 255 | 60 | `dyn_subwaydoor_a` | 5 |
| `0x888888FF` | 136, 136, 136, 255 | 48 | `dyn_s_wwcart_simple_a`, `dyn_s_wwcart_simple_b`, `dyn_s_wwcart_simple_c` | 2 |
| `0xB1B1B1FF` | 177, 177, 177, 255 | 48 | `dyn_s_wwcart_simple_a`, `dyn_s_wwcart_simple_b`, `dyn_s_wwcart_simple_c` | 2 |
| `0x5C584DFF` | 92, 88, 77, 255 | 47 | `dyn_tomb_b` (40), `dyn_broom`, `dyn_porcelain_d`, `dyn_cash_roll` | 6 |
| `0x595459FF` | 89, 84, 89, 255 | 32 | `dyn_tomb_a` (31), `dyn_tomb_b` | 4 |
| `0xE8E3DEFF` | 232, 227, 222, 255 | 24 | `dyn_subwaydoor_a` | 1 |
| `0x757A7EFF` | 117, 122, 126, 255 | 18 | `dyn_s_train_med_full` | 3 |
| `0xE2DEDAFF` | 226, 222, 218, 255 | 16 | `dyn_clublite` | 2 |
| `0xF7F7F7FF` | 247, 247, 247, 255 | 12 | `dyn_fx_train` | 10 |
| `0x8A8682FF` | 138, 134, 130, 255 | 11 | `dyn_special_key`, `dyn_revival`, `dyn_spraycan`, `dyn_cuffs_b` and others | 3 |
| `0xF9F9F9FF` | 249, 249, 249, 255 | 9 | `dyn_barrel_b` | 1 |
| `0x787D81FF` | 120, 125, 129, 255 | 6 | `dyn_s_train_med_half` | 1 |
| `0xDAD9D8FF` | 218, 217, 216, 255 | 4 | `dyn_tag_warr`, `dyn_tag_warr_dest` | 3 |
| `0x111319FF` | 17, 19, 25, 255 | 3 | `dyn_gbin_small_open` | 1 |
| `0xEBEBEBFF` | 235, 235, 235, 255 | 3 | `dyn_boat` | 1 |
| `0x474542FF` | 71, 69, 66, 255 | 2 | `dyn_s_wwheel_a` | 2 |
| `0x99948EFF` | 153, 148, 142, 255 | 2 | `dyn_porcelain_d` | 2 |
| `0x838483FF` | 131, 132, 131, 255 | 2 | `dyn_s_train_p` | 1 |
| `0x433831FF` | 67, 56, 49, 255 | 2 | `dyn_fridge_b` | 1 |
| `0x868887FF` | 134, 136, 135, 255 | 2 | `dyn_s_dumbells` | 1 |
| `0xFCFCFCFF` | 252, 252, 252, 255 | 1 | `dyn_s_blaster` | 1 |
| `0x544F52FF` | 84, 79, 82, 255 | 1 | `dyn_tomb_a` | 1 |
| `0xF4F4F5FF` | 244, 244, 245, 255 | 1 | `dyn_oil_fire` | 1 |
| `0xFEFEFEFF` | 254, 254, 254, 255 | 1 | `dyn_s_train_b` | 1 |
| `0xE1E0E0FF` | 225, 224, 224, 255 | 1 | `dyn_pinball_fuzzballs_b` | 1 |
| `0x494341FF` | 73, 67, 65, 255 | 1 | `dyn_pinball_fuzzballs_a` | 1 |

The remaining seven tints colour the pool ball model `dyn_poolball08_`, once each, in one level 11 script that
spawns eight balls, the eighth untinted:

| Tint | R, G, B, A | Looks |
| --- | --- | --- |
| `0x8080FFFF` | 128, 128, 255, 255 | blue |
| `0xAEAEFFFF` | 174, 174, 255, 255 | pale blue |
| `0x80BF80FF` | 128, 191, 128, 255 | green |
| `0xAED6AEFF` | 174, 214, 174, 255 | pale green |
| `0xFF8181FF` | 255, 129, 129, 255 | red |
| `0xFFADADFF` | 255, 173, 173, 255 | pale red |
| `0xA0A06EFF` | 160, 160, 110, 255 | olive |
| `0xFFFFFFFF` (white) | 255, 255, 255, 255 | the white ball |

Most spawn tints are greys a little below white that dim one copy of a shared model, for example the subway doors,
tombstones and Wonder Wheel cars. Every spawn tint on the disc is fully opaque.

## Object colour changes

Scripts can recolour a spawned object later. On the PS2 disc this appears three times, all in the wheelchair race
Rumble script (directory entry 10676): the finish-line markers cycle through three colours, one every 200 ms.

| Script values | Stored word | R, G, B, A | Looks |
| --- | --- | --- | --- |
| 255, 255, 255, 125 | `0xFFFFFF7D` | 255, 255, 255, 125 | white, half see-through |
| 255, 100, 100, 125 | `0xFF64647D` | 255, 100, 100, 125 | red, half see-through |
| 255, 255, 100, 125 | `0xFFFF647D` | 255, 255, 100, 125 | yellow, half see-through |

These values are whole numbers from 0 to 255; the stored byte equals the value given.

## Character colours

Scripts set a character's colour with four values from 0 to 1, stored as bytes of value times 255.

| Values | Stored R, G, B, A | Characters | Uses | Where |
| --- | --- | --- | --- | --- |
| 0.3, 0.3, 0.3, 1 | 76, 76, 76, 255 | Swan, Ajax | 40 | five Armies of the Night scripts |
| 1, 1, 1, 1 | 255, 255, 255, 255 | Swan, Ajax | 40 | five Armies of the Night scripts |
| 0.6, 0.6, 0.6, 0 | 153, 153, 153, 0 | Cyrus, Cleon (two handles), Fox | 4 | level 64 |

In the Armies of the Night levels, a Warrior who comes back after being knocked out blinks between the dark colour
(76, 76, 76) and normal in 250 ms steps, and is invulnerable for 2 s.

## Level colour

At each level start the global script (directory entry 4105) lays a colour over the whole view, from its per-level
table of four values from 0 to 1. The view shows value times 255, rounded down. Levels numbered above 200 use the
entry 200 lower, and a level without an entry uses the entry for level 1. The colour blends in over 0.25 s.

| Levels | Table values | Shown R, G, B, A |
| --- | --- | --- |
| 2 | 0.15, 0.18, 0.2, 0.22 | 38, 45, 51, 56 |
| 3, 107 | 0, 0, 0.02, 0.1 | 0, 0, 5, 25 |
| 5 | 0.22, 0.24, 0.2, 0.22 | 56, 61, 51, 56 |
| 7 | 0.27, 0.35, 0.35, 0.17 | 68, 89, 89, 43 |
| 9 | 0.17, 0.16, 0.17, 0.12 | 43, 40, 43, 30 |
| 11 | 0.04, 0.035, 0.01, 0.2 | 10, 8, 2, 51 |
| 51 | 0.18, 0.2, 0.26, 0.05 | 45, 51, 66, 12 |
| 55 | 0.03, 0.035, 0.04, 0.4 | 7, 8, 10, 102 |
| 80, 102 | 0.03, 0.08, 0.12, 0.2 | 7, 20, 30, 51 |
| 87 | 0, 0.1, 0.18, 0.12 | 0, 25, 45, 30 |
| 91, 97 | 0.18, 0.2, 0.26, 0.17 | 45, 51, 66, 43 |
| 95 | 0.075, 0.075, 0.15, 0.25 | 19, 19, 38, 63 |
| 99, 100 | 0.03, 0.08, 0.12, 0.17 | 7, 20, 30, 43 |
| all other table entries (33 levels) | 0, 0, 0, 0 | no colour |

- The savestates in level 99 hold 7, 20, 30, 43 as the shown colour, matching the table.
- One level 55 script (directory entry 8403) sets 0.065, 0.045, 0.023, 0.56 instead, shown as 16, 11, 5, 142: a
  dark brown cast over most of the picture.
- The store, the stats screen and one other front-end mode clear the level colour (all zero) over 0.25 s; leaving
  the store brings the level colour back.
- While a look such as Rage is on, a level colour change shows only when that look ends (see `screen-effects`).

## Spinning icon colour

Characters can carry a spinning icon over the head. The icon models named by the scripts are `dyn_rockstar`,
`dyn_w_bonus_b`, `dyn_crown`, `dyn_flashdeal`, `dyn_member_p1` and `dyn_member_p2`, and one script passes a named
constant instead. No script on the PS2 disc sets an icon colour, so icons keep their own colours.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00396bd0` | recolour a spawned object | Object colour changes |
| `0x00239100` | set a character's colour | Character colours |
| `0x0018e5f0` | lay the level colour over every view | Level colour |
| `0x0018e6b8` | lay the store colour over every view | Level colour |

## Acceptance checks

- The PS2 Lua scripts hold 2,871 object spawn uses: 2,507 untinted and 364 with one of 33 other tints.
- The most used spawn tint is `0xA49E98FF` (164, 158, 152), on the subway door model in 60 uses.
- Every spawn tint on the PS2 disc has alpha 255.
- In the wheelchair race Rumble, the finish-line markers flash white, red and yellow, half see-through, changing
  every 200 ms.
- In an Armies of the Night level, a Warrior who returns after being knocked out blinks dark and normal in 250 ms
  steps.
- In level 99 the whole view carries a blue-grey cast of 7, 20, 30 at alpha 43.
- Opening the store removes the level's colour cast within 0.25 s, and leaving brings it back.
- The level colour table in the PS2 global script has 50 entries, 17 of them coloured, in 13 distinct colours.

## Open questions

- The exact way a tint mixes with the model's own colours and lighting was not measured on screen.
- The meaning of the alpha byte on objects (whether 125 gives half transparency on every object type) was not tested.
- Characters set to alpha 0 (Cyrus, Cleon, Fox in level 64) were not looked at in play.
- Levels for most spawn-tint scripts could not be named, because script names are only stored as hashes.
- Car colours and spray-tag colours are not covered here.
- The Xbox version was not checked.
