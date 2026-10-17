# Physics bodies and props

This page lists the object setups the game carries on its disc: every prop, weapon, hat, door, pick-up and vehicle
type, with the body shape, size, material and type each one is given, and what a player sees when such objects are
hit, thrown or broken. Values come from the compiled Lua 4.0 config scripts in the PS2 archive (`WARRIORS.DIR` and
`WARRIORS.WAD`). World collision, gravity and step rate belong to the physics-world page.

## Source files

| File | Archive entry | Size (bytes) | Holds |
| --- | --- | --- | --- |
| `config_preload2.lua` | 3771 | 182,649 | 92 object records (all hats), 19 glass-type records, the time-to-live value, five enum tables |
| `config_preload3.lua` | 3772 | 308,938 | 1,279 object records, grouped in five blocks by first letter (A-C, D-F, G-M, N-S, T-Z) |
| `enum_preload.lua` | - | 12,501 | the `MATERIAL` table (190 names) |
| `config_preload.lua` | 3770 | 159,786 | strike-on-surface material pairs (sound and hit tables) |
| `global.lua` | - | 169,601 | a burn time of 3000 |
| `car.lua` | - | 1,676 | the car rumble: a hit target of 50 |

All are Lua 4.0 binary chunks: little-endian, 4-byte integers, sizes and instructions, 8-byte (double) numbers.

## Object records

Each object record is a list of 22 values, by position, in the compiled script. The two files hold 1,371 records
under 1,371 different names, and each record adds one object type that placed objects name.
1,152 of them give a placed object a physics body: the 1,116 `OBB` records, the 5 `SPHERE` records and the 31
`powerup_item` records, which all have shape `NONE` (see Body shapes). The Xbox disc carries all six source files
byte for byte, so its records are the same.

| File | Block (letter after `dyn_`) | Records | `OBB` | `SPHERE` | `NONE` | Of those `powerup_item` | With a body |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `config_preload2.lua` | one block, all `hat_object` | 92 | 43 | 0 | 49 | 0 | 43 |
| `config_preload3.lua` | A to C | 362 | 335 | 2 | 25 | 13 | 350 |
| `config_preload3.lua` | D to F | 248 | 144 | 0 | 104 | 0 | 144 |
| `config_preload3.lua` | G to M | 139 | 127 | 0 | 12 | 3 | 130 |
| `config_preload3.lua` | N to S | 406 | 346 | 3 | 57 | 14 | 363 |
| `config_preload3.lua` | T to Z | 124 | 121 | 0 | 3 | 1 | 122 |
| Both files | | 1,371 | 1,116 | 5 | 250 | 31 | 1,152 |

In the chunk, string arguments sit in the function's string constants, whole numbers are carried in the instruction
itself, and other numbers are 8-byte doubles in the function's number constants. A name such as `PHYS.OBB` is written
as a look-up in a global table, so the record holds the name and takes the value listed under Enum tables. Positions
7, 8, 21 and 22 are table constructors of 3 or 4 numbers.

| Position | Values | Written as | Meaning | Values seen |
| --- | --- | --- | --- | --- |
| 1 | 1 | string | object type name, used by placed objects | 1,371 distinct names, 6 to 26 characters, all starting `dyn_` |
| 2 | 1 | string | class: placed objects of the type behave as this class | 64 class names; `simple_object` 474, `dyn_masks` 201, `fade_object` 115, `hat_object` 92 |
| 3 | 1 | whole number | not confirmed | 0 to 300; 100 in 695 records, 0 in 326 |
| 4 | 1 | whole number | not confirmed | 0 to 100; 0 in 902 records, 1 in 326 |
| 5 | 1 | whole number | not confirmed | 0 to 5; 0 in 1,326 records |
| 6 | 1 | whole number | not confirmed | 1 to 100; 10 in 818 records, 50 in 366, 1 in all 92 hats |
| 7 | 3 | numbers x, y, z | shape offset (m): where the body sits from the object's origin | x -6.88 to 7.3, y -2.6 to 3.5, z -4 to 4.22; 0, 0, 0 in 1,261 records |
| 8 | 3 | numbers x, y, z | size (m) | x 0 to 656.25, y 0 to 29.61, z 0 to 44.89; 0, 0, 0 in 21 records |
| 9 | 1 | `PHYS` name | body shape | `OBB` 1,116, `NONE` 250, `SPHERE` 5 |
| 10 | 1 | `AXIS` name or `ANIM.na` | axis setting; its effect is not confirmed | an `AXIS` name in 516 records, `ANIM.na` (0) in 855 |
| 11 | 1 | number | not confirmed | 0 to 0.4; 0.1 in 1,359 records |
| 12 | 1 | 0, one `PHYFLAG` name or a sum of 2 to 6 names | body flags, the `PHYFLAG` values added together | 0 to 703, 28 different values; plain 0 in 918 records, one name in 82, a sum in 371 |
| 13 | 1 | `MATERIAL` name | material: picks the strike-on-surface sounds when the object hits something | a `MATERIAL` name in 1,364 records (78 names; `NONE` 429, `BAT` 280, `CHAIR` 130), `ANIM.na` in 6, `OBJECT.TYPE_GLASS` in 1 |
| 14 | 1 | `ANIM` name | pick-up animation, as the names say | `ANIM.na` 1,069, `OneHandPickUp` 113, `LeftHandPickUp` 78, `TwoHandPickUp` 69, `LeftHandHatPickUp` 40, `GhettoPickUp` 2 |
| 15 to 18 | 1 each | number | not confirmed | 15: 0 to 4.43, 16: 0 to 3.4, 17: -0.13 to 3, 18: 0 to 3.1; all four 0 in 1,092 records |
| 19 | 1 | `OBJECT` name | object type | 39 types, values 2 to 49; `TYPE_GENERIC` 846 |
| 20 | 1 | `ANIM` name | animation set while held, as the names say | `NONE_SET` 1,190, `OVERHEAD_WEAPON_SET` 68, `CLUB_WEAPON_SET` 46, `SINGLE_HAND_THROW_WEAPON_SET` 46, `BATON_WEAPON_SET` 12, `KNIFE_WEAPON_SET` 7, `GHETTO_SET` 2 |
| 21 | 3 | numbers x, y, z | not confirmed | not all 0 in 180 records (all 92 hats, 60 overhead weapons, 28 others); x -0.02 to 0.51, y -0.15 to 0.38, z -0.15 to 0.98 |
| 22 | 4 | numbers | not confirmed | not all 0 in 179 records (all 92 hats, 60 overhead weapons, 27 others); 177 of them have squares that add up to 1; each value -0.99 to 1 |

- All 92 records of `config_preload2.lua` hold 0, 0, 0 and 1 in positions 3 to 6, 0.1 in position 11, 0 in
  position 12, `MATERIAL.NONE` and `ANIM.NONE_SET`; their types are `TYPE_RIOT_HAT` (52) and `TYPE_HAT` (40).
- Positions 7 and 15 to 18 are not all 0 only in `config_preload3.lua` (110 and 279 records).
- The only negative value in positions 15 to 18 is the -0.13 of `dyn_bong_f`.
- Two `OBB` records, `dyn_s_swhbld_a` and `dyn_s_swhbld_b`, have size 0, 0, 0.
- The `ANIM` table gives the pick-up names values 0 to 7 and the sets values 0 to 6; no record uses
  `OneHandKnifePickUp` (3) or `OneHandBatPickUp` (4).

## Enum tables

The scripts define these tables by name; the numbers are the table values.

| `PHYS` | Value | Records |
| --- | --- | --- |
| `NONE` | 0 | 250 |
| `OBB` | 1 | 1,116 |
| `SPHERE` | 2 | 5 |
| `UCYLINDER` | 3 | 0 |
| `CAPSULE` | 4 | 0 |

| `AXIS` | Value | Records |
| --- | --- | --- |
| `NONE` | 0 | 0 by name; 855 records hold an animation constant worth 0 here |
| `X` | 1 | 76 |
| `Y` | 2 | 28 |
| `XY` | 3 | 30 |
| `Z` | 4 | 197 |
| `XZ` | 5 | 54 |
| `YZ` | 6 | 4 |
| `XYZ` | 7 | 119 |
| `XZ_ROUND` | 13 | 8 |
| `YZ_ROUND` | 14 | 0 |

| `PHYFLAG` | Value |
| --- | --- |
| `BLOCKOBJECTS` | 1 |
| `BLOCKHUMANS` | 2 |
| `MELEETARGET` | 4 |
| `WEAPONTARGET` | 8 |
| `JUMPTARGET` | 16 |
| `CHARGETARGET` | 32 |
| `COMBATTARGET` | 60 (the sum of the four target flags 4, 8, 16, 32) |
| `RAYCASTTARGET` | 64 |
| `COMBATPASSTHROUGH` | 128 |
| `THROWNWEAPONTARGET` | 256 |
| `RUNTARGET` | 512 |

The most common flag sums are 0 (918 records), 191 (130: blocks objects and humans, combat target, combat
pass-through), 63 (83), 1 (69), 61 (36) and 3 (36).

`OBJECT` holds 50 types numbered 0 to 49 (`TYPE_NOTHING` 0, `TYPE_HUMAN` 1, `TYPE_GENERIC` 2 up to
`TYPE_BOLTCUTTERBOX` 49). `MATERIAL` holds 190 names numbered 1 to 190 (`NONE` 1, `GLASS` 2, `CAR_HOOD` 13,
`WOOD` 16, `BAT` 29, `CHAIR` 86, `BRICK` 102, `GLASS_FRAGILE` 104, `MEAT` 109, ... `SALAMI` 190).

## Records by kind

| Kind | Classes | Records | Usual shape | Size range (m) | Main types |
| --- | --- | --- | --- | --- | --- |
| Plain scenery | `simple_object` | 474 | `OBB` (46 `NONE`) | up to 612.5 long | `TYPE_GENERIC` 424 |
| Breakable props | `dyn_masks`, `dyn_table`, `dyn_lizzies` | 220 | `OBB` | up to 6.24 | `TYPE_GENERIC` |
| Weapons | `melee_weapon`, `overhead_weapon`, `thrown_weapon`, `dyn_molotv` | 173 | `OBB` | up to 2.63 | `TYPE_BAT`, `TYPE_DRUM`, `TYPE_BRICK` |
| Debris pieces | `fade_object` | 115 | `OBB` | up to 4.98 | `TYPE_GENERIC` 107 |
| Doors and gates | 11 classes | 108 | mostly `NONE` | 0.11 to 9.26 wide | `TYPE_DOOR`, `TYPE_BREAKABLE_DOOR` |
| Hats and masks | `hat_object` | 92 | `NONE` 49, `OBB` 43 | 0.03 to 0.52 | `TYPE_RIOT_HAT` 52, `TYPE_HAT` 40 |
| Markers and effects | `dyn_icon`, `dyn_objective`, `rotating_object` | 71 | `OBB` | up to 29.61 | `TYPE_GENERIC` |
| Pick-ups | `powerup_item`, `pickup_item`, `dyn_pile`, `float_item` | 68 | `NONE` or `OBB` | up to 2.11 | `TYPE_SPECIAL` 51, piles |
| Single-purpose props | 36 other classes | 50 | mixed | mixed | mixed |

The 220 breakable props mostly use material `CHAIR` (93 of the `dyn_masks` records), with `TINBOX`, `WOODFURNITURE`
and `PORCELAIN` next. Records whose names share a stem with added letters describe one prop and its pieces: the
chair `dyn_chair_k` (an overhead weapon) has pieces `dyn_chair_ka` and `dyn_chair_kb` (debris), `dyn_chair_kc`
(debris) and `dyn_chair_kd` (a melee weapon of type `TYPE_BATON`).

## Body shapes

| Record shape | Body the object gets | Example |
| --- | --- | --- |
| `OBB` | a box with the record's size triple as its x, y and z | `dyn_crate_bat`: 1 x 1 x 1.55 m |
| `SPHERE` | a sphere whose radius is half the size's x value | `dyn_cueball`: x 0.11, radius 0.055 m |
| `NONE` | no body from the record | 49 riot hats, 72 swinging doors |
| any, class `powerup_item` | a sphere of radius 0.75 m | `dyn_carstereo`, `dyn_copradio` |

- The body sits at the record's shape offset (position 7) from the object's origin; `dyn_bat` uses 0, -0.16, 0.
- The five sphere records are `dyn_bare_light`, `dyn_cueball`, `dyn_poolball08_`, `dyn_steak` and `dyn_steak_fade`.
- 72 records have a zero in at least one size value (flat signs, glows, icons).
- The largest records are 12 shoreline wave strips (`dyn_o_wavewash_b` 656.25 m long, 11 others 612.5 m) and the
  disco rays
  `dyn_discoray_gold_b` and `dyn_discoray_silver_b` (29.6 x 29.61 x 25.1 m).

## Weapons and thrown objects

| Name | Class | Pos 3 | Pos 4 | Pos 5 | Pos 6 | Size (m) | Shape, axis | Pos 11 | Material | Type |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `dyn_bat` | melee | 90 | 6 | 3 | 50 | 0.09 x 0.89 x 0.08 | `OBB`, `XZ_ROUND` | 0.1 | `BAT` | `TYPE_BAT` |
| `dyn_bat_fury` | melee | 90 | 10 | 4 | 50 | 0.09 x 0.89 x 0.08 | `OBB`, `XZ_ROUND` | 0.1 | `BAT` | `TYPE_BAT` |
| `dyn_bat_tuff` | melee | 50 | 0 | 0 | 50 | 0.09 x 0.89 x 0.08 | `OBB`, `XZ_ROUND` | 0.1 | `BAT` | `TYPE_BAT` |
| `dyn_baton` | melee | 50 | 0 | 1 | 50 | 0.04 x 0.6 x 0.05 | `OBB`, `XZ` | 0.1 | `BATON` | `TYPE_BATON` |
| `dyn_tknife` | melee | 200 | 4 | 1 | 20 | 0.03 x 0.46 x 0.08 | `OBB`, `X` | 0.2 | `SWITCHBLD` | `TYPE_KNIFE` |
| `dyn_mace` | melee | 10 | 3 | 0 | 50 | 0.07 x 0.2 x 0.05 | `OBB`, `X` | 0.2 | `NONE` | `TYPE_MACE` |
| `dyn_chair_k` | overhead | 100 | 1 | 0 | 50 | 0.57 x 0.52 x 1.1 | `OBB`, `XY` | 0.1 | `CHAIR` | `TYPE_DRUM` |
| `dyn_amp_a` | overhead | 100 | 1 | 0 | 70 | 1.02 x 0.35 x 0.48 | `OBB`, `XYZ` | 0.1 | `GEAR` | `TYPE_DRUM` |
| `dyn_brick` | thrown | 75 | 1 | 0 | 22 | 0.07 x 0.21 x 0.1 | `OBB`, `XZ` | 0.1 | `BRICK` | `TYPE_BRICK` |
| `dyn_beerbottle` | thrown | 60 | 1 | 0 | 20 | 0.1 x 0.3 x 0.09 | `OBB`, `XZ` | 0.1 | `BOTTLE` | `TYPE_BOTTLE` |
| `dyn_cueball` | thrown | 130 | 0 | 0 | 20 | 0.11 x 0.11 x 0.12 | `SPHERE`, `XYZ` | 0.2 | `POOLBALL` | `TYPE_BRICK` |
| `dyn_steak` | thrown | 80 | 3 | 0 | 20 | 0.22 x 0.31 x 0.04 | `SPHERE`, `X` | 0.15 | `MEAT` | `TYPE_BRICK` |
| `dyn_molotv` | `dyn_molotv` | 240 | 1 | 0 | 25 | 0.15 x 0.35 x 0.12 | `OBB`, `XZ` | 0.1 | `MOLOTOV` | `TYPE_MOLOTOV` |

- Position 6 groups by class: 51 of 64 melee weapons hold 50, 35 of 67 overhead weapons hold 50, and 36 of 41
  thrown weapons hold 20.
- Position 11 differs from 0.1 in 12 records only: 0.2 for the knives `dyn_bowie`, `dyn_hunter` and `dyn_tknife`,
  `dyn_mace`, the broken bottle and both pool balls; 0.15 and 0.4 for the two steaks; 0 for two debris pieces and
  `dyn_doghouse_ab`.

## Breaking

- A broken object stops colliding: it has no body from the moment it breaks.
- Objects of type `TYPE_MOLOTOV` (8) burst when they break and hurt people within 1.5 m.
- Breakable door types are `TYPE_BREAKABLE_DOOR` (16 records) and `TYPE_BREAKANDENTER_DOOR` (1 record).
- `dyn_bare_light` (class `dyn_breakable_light`) is a sphere 0.5 m across, of material `GLASS_FRAGILE`.
- Debris pieces (class `fade_object`, 115 records) are separate object types with their own sizes and materials.

## Vehicles

| Name | Class | Type | Size (m) | Material | Pos 15 to 18 |
| --- | --- | --- | --- | --- | --- |
| `dyn_copcar_whole` | `simple_object` | `TYPE_COPCAR` | 2.65 x 5.45 x 1.8 | `CAR_HOOD` | 0.8, 0.8, 0.55, 0.55 |
| `dyn_copcar_wrecked` | `simple_object` | `TYPE_CAR` | 2.54 x 5.35 x 1.68 | `CAR_HOOD` | 0.8, 0.8, 0.55, 0.55 |
| `dyn_ocopcar` | `dyn_exploding_car` | `TYPE_COPCAR` | 2.41 x 5.76 x 1.8 | `NONE` | 0.5, 0.5, 0.29, 0.3 |
| `dyn_trans` | `dyn_exploding_car` | `TYPE_CAR` | 1.73 x 1.67 x 2.74 | `BAT` | 0, 0, 0, 0 |
| `dyn_osedan` | `simple_object` | `TYPE_CAR` | 2.42 x 5.94 x 1.54 | `NONE` | 0.5, 0.5, 0.29, 0.3 |
| `dyn_ocoupe`, `dyn_ocube`, `dyn_ovan`, `dyn_owagon` | `simple_object` | `TYPE_COPCAR` | 2.33 to 2.54 x 5.28 to 6.18 x 1.49 to 2.98 | `NONE` | 0.5, 0.5, 0.29, 0.3 |
| `dyn_hearse` | `simple_object` | `TYPE_MOVINGVEHICLE` | 2.61 x 6.3 x 1.69 | `BAT` | 0, 0, 0, 0 |
| `dyn_s_train_kill_p` | `simple_object` | `TYPE_MOVINGVEHICLE` | 3.18 x 16.69 x 2.91 | `TRAINCAR` | 0, 0, 0, 0 |

- There are 15 vehicle records: 3 `TYPE_CAR`, 6 `TYPE_COPCAR` and 6 `TYPE_MOVINGVEHICLE` (the others are
  `dyn_coney_car_a`, `dyn_ridecart`, `dyn_rogues_car` and `dyn_turnbul_bus`).
- The car rumble has two cars on the map. Each gang's bar shows the hits that gang has landed on the cars as a
  share of 50, and the first gang to land 50 hits wins.

## Glass types

`config_preload2.lua` sets 19 glass types, numbered 0 to 18, each with two true/false values and two numbers.

| Glass types | Value A | Value B | Number C | Number D |
| --- | --- | --- | --- | --- |
| 0, 1, 2, 3, 12, 15 | false | false | 65556 | 65557 |
| 4, 5, 6, 7 | false | true | 65556 | 65557 |
| 8, 9 | true | false | 65556 | 65557 |
| 10, 11 | true | false | 65558 | 65559 |
| 13 | true | true | 65556 | 65557 |
| 14 | true | false | 131072 | 65557 |
| 16 | false | false | 65558 | 65559 |
| 17 | false | false | 65557 | 65557 |
| 18 | true | false | 65557 | 65557 |

61 object records have type `TYPE_GLASS`. Across all records, 19 use material `GLASS`, 3 `GLASS_FRAGILE` and 2
`GLASS_SMALL`.

## Other settings

| Setting | Value | File |
| --- | --- | --- |
| global time-to-live | 30000 | `config_preload2.lua` |
| burn time | 3000 | `global.lua` |
| car rumble hit target | 50 | `car.lua` |
| strike-on-surface material pairs | 1,059 (109 striking and 113 struck materials) | `config_preload.lua` |
| pairs for a knocked-down torso (`TORSO_PRONE`) | 30 | `config_preload.lua` |

## Data quirks

- 855 records put an animation constant worth 0 in the axis slot, the same number as `AXIS.NONE`.
- Six records put an animation constant worth 0 in the material slot; 0 is not a `MATERIAL` value.
- `dyn_chandelier_glows` puts `OBJECT.TYPE_GLASS` (32) in the material slot; material 32 is `MAILBOX`.
- No record uses `UCYLINDER` or `CAPSULE`, and no record uses `AXIS.YZ_ROUND`.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0036a1a0` | register an object type from one object record | Object records |
| `0x00391d48` | give a placed object its physics body | Body shapes |
| `0x00393e20` | break a placed object | Breaking |
| `0x0036ad80` | record the values of one glass type | Glass types |

## Acceptance checks

| Check |
| --- |
| The PS2 config scripts hold 1,371 object records: 92 in `config_preload2.lua` and 1,279 in `config_preload3.lua`. |
| Every object record has 22 values, and no two records share a name. |
| The five blocks of `config_preload3.lua` hold 362, 248, 139, 406 and 124 records. |
| 1,152 object records give a placed object a physics body: 43 in `config_preload2.lua` and 1,109 in `config_preload3.lua`. |
| The 31 `powerup_item` records all have shape `NONE`. |
| The six source files are byte-identical on the PS2 and Xbox discs. |
| 1,116 records use shape `OBB`, 250 use `NONE` and 5 use `SPHERE`. |
| The records use 64 classes and 39 object types; `simple_object` has 474 records. |
| `dyn_bat` has size 0.09 x 0.89 x 0.08 m, material `BAT` and type `TYPE_BAT`. |
| `dyn_ocopcar` has class `dyn_exploding_car`, size 2.41 x 5.76 x 1.8 m and type `TYPE_COPCAR`. |
| A pool ball collides as a sphere about 0.11 m across. |
| A car stereo or police radio pick-up collides as a sphere about 1.5 m across. |
| A molotov that breaks hurts people standing within 1.5 m of it. |
| A broken prop no longer blocks people or objects. |
| `config_preload2.lua` sets 19 glass types, numbered 0 to 18. |
| `PHYFLAG.COMBATTARGET` is 60 and `MATERIAL` holds 190 names. |
| In the car rumble, the first gang to land 50 hits on the cars wins. |

## Open questions

- Meanings of positions 3, 4, 5, 6, 11 and 15 to 18 of the object records are not confirmed. Patterns: position 4 is
  higher for the Fury bat (10) than the normal bat (6); position 11 looks like a mass.
- What the glass types' two true/false values and two numbers (65556 to 65559, 131072) control.
- What the global time-to-live of 30000 and the burn time of 3000 govern, and in what unit.
- How knocked-down characters behave as bodies; no object record covers them.
- Whether a weapon's holder keeps or loses the weapon when it breaks in hand.
- What separates a `TYPE_COPCAR` from a `TYPE_CAR` record in play, and how `dyn_exploding_car` objects explode.
- Which code breaks a glass pane and which builds a car's body; candidates were found but not confirmed, so they are
  left out of the entry points.
- What position 10 (the axis setting) changes for an object.
- What positions 21 and 22 control; they are set in all hats and in 60 overhead weapons, and the four numbers of
  position 22 look like a rotation.
