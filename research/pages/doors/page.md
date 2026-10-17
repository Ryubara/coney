# Doors and gates

Doors, gates, shutters and breakable panels are objects that a level's script places when the level loads. This
page gives the door types and their records, the five values that place a door, what each kind of door is made of,
and the door commands that level scripts use, with what each one does in game. Every value was read from the PS2
disc (NTSC-U, `SLUS_212.15`) and compared with the Xbox disc. The full per-level door lists are on the reference page
`ref-doors`.

## Disc sources

All scripts named here are compiled Lua 4.0 chunks inside `WARRIORS.WAD`, located through `WARRIORS.DIR`.

| Source | Directory entry (zero-based) | Size in bytes | Values taken from it |
| --- | ---: | ---: | --- |
| `config_preload2.lua` | 3,771 | 182,649 | The object type constants (`OBJECT.TYPE_DOOR` and the others below) |
| `config_preload3.lua` | 3,772 | 308,938 | One object record per object type, including every door type and door leaf |
| `level<N>.lua` | 45 scripts | various | The door placements of each level |
| `level86_chapter1_reg.lua` | 9,162 | 23,860 | One more placement for `level86` |

## Door counts

| Fact | PS2 disc | Xbox disc |
| --- | ---: | ---: |
| Door placements in scripts | 484 | 493 |
| Scripts holding placements | 46 | 50 |
| Door types placed | 36 | 36 |
| Level records with a `.lev` file and no door | 19 | not compared |

The Xbox disc holds all 484 PS2 placements unchanged and nine more: three in `level117.lua` (Tenement Lobby), three in
`level125.lua` (Spookarama), two in `level73.lua` and one in a script whose name is not recovered. None of those
four scripts is on the PS2 disc. The door object records are the same on both discs.

All 23 story missions (orders 1 to 23 on `ref-levels`) have doors. The 19 levels with a `.lev` file but no
door are `level1`, `level63`, `level100` to `level102`, `level104`, `level105`, `level108`, `level110`, `level113`,
`level116`, `level118` to `level120`, `level122`, `level127`, `level129`, `level130` and `level132`.

## Object records

Each object type has one record in `config_preload3.lua`: a list of 22 values. The values this page uses are these.

| Position | Meaning on this page | Values for door types |
| ---: | --- | --- |
| 1 | Object type name | `dyn_door_...` for the placed door, `dyn_dr_...` for its leaves |
| 2 | Kind name | `dyn_door_swinging`, `dyn_door_sliding`, `dyn_door_fence` or a name of the type's own |
| 8 | Box size (x, y, z) | For example 1.64, 0.07, 1.89 for `dyn_door_cabin_a` |
| 13 | Material constant | For example `MATERIAL.GLASS`, `MATERIAL.WOODFENCE`, `MATERIAL.STOREDOOR_GLASS` |
| 19 | Object type constant | One of the constants below |

| Object type constant | Value | Door-related records using it |
| --- | ---: | ---: |
| `OBJECT.TYPE_GENERIC` | 2 | 24 |
| `OBJECT.TYPE_DRUM` | 5 | 2 |
| `OBJECT.TYPE_DOOR` | 15 | 69 |
| `OBJECT.TYPE_BREAKABLE_DOOR` | 25 | 16 |
| `OBJECT.TYPE_BREAKANDENTER_DOOR` | 30 | 1 |
| `OBJECT.TYPE_GLASS` | 32 | 19 |
| `OBJECT.TYPE_SLIDING_DOOR` | 33 | 9 |

Door-related records here are the 140 records whose name contains `door` or starts with `dyn_dr_`. Only
`dyn_door_store` has `OBJECT.TYPE_BREAKANDENTER_DOOR`. 19 door records carry `OBJECT.TYPE_GLASS`, all of them
`dyn_dr_` leaves such as `dyn_dr_templedoor`.

## Door kinds

Every door type record (`dyn_door_...`) in `config_preload3.lua` names its kind at position 2, and the kind decides
what the door does in play. Of the 140 door-related records, 59 are door types and 58 are leaves (`dyn_dr_...`); the
other 23, such as `dyn_old_door` and `dyn_hearse_door_a_l`, are never placed as doors.

| Kind (position 2) | Door type records | Placed types | Placements | What the player sees | Opened or broken by | Lock |
| --- | ---: | ---: | ---: | --- | --- | --- |
| `dyn_door_bar_bani` | 1 | 1 | 51 | A boarded barrier that never opens; worn down by hits it breaks with dust, splinters and four loose boards, and the gap stops blocking | Hits | None |
| `dyn_door_barricade` | 1 | 1 | 3 | A blocker that draws nothing of its own; the open command clears the way and the close command blocks it again | Level scripts only | None |
| `dyn_door_bnstr` | 1 | 0 | 0 | Not placed; not examined | | |
| `dyn_door_chain_s` | 1 | 0 | 0 | Not placed; not examined | | |
| `dyn_door_fence` | 4 | 4 | 45 | A fence or wall section that never opens; worn down by hits it breaks (see the table below) and the gap stops blocking | Hits | None |
| `dyn_door_fence_o` | 1 | 0 | 0 | Not placed; not examined | | |
| `dyn_door_parapet` | 1 | 0 | 0 | Not placed; not examined | | |
| `dyn_door_sliding` | 9 | 5 | 29 | One or two leaves slide aside or up (see Sliding leaves) with an open or close sound; open, the doorway stops blocking | Level scripts, directly or through a character's use move | Scripts can show a lock-pick marker on it while it is closed |
| `dyn_door_swinging` | 40 | 25 | 356 | One or two leaves swing on their hinges, to 170 degrees when fully open, away from the character who opens it; once open the doorway stops blocking | A character's use move on it, a picked lock or level scripts; seven types also break under hits | Three placed types start locked (Pick yes in Placed door types); scripts can lock or unlock any |
| All kinds | 59 | 36 | 484 | | | |

Leaf records name their own kinds: 39 are `sub_swinging_door`, 10 `sub_sliding_door` and 9 `simple_object`. Three
swinging records, `dyn_door_dblwood`, `dyn_door_woodbrd` and `dyn_door_woodp`, get no leaves and are not placed.

| Swinging door outcome | Value |
| --- | --- |
| Hits on the 20 placed swinging types not listed below | No damage |
| Lock picked | The door unlocks and the player who picked it opens it |
| Locked door | Stays shut whatever opens it, until its lock is picked or a script unlocks it |
| Opened by a character | The leaves swing 170 degrees away from the character, with the type's open sound |
| Start of the level | The types with Pick yes are locked and show a lock-pick marker |
| Two-leaf doors | Both leaves turn by the same angle in mirror image |

Hits wear down only the door types below. A broken door no longer blocks; for all of them but `dyn_door_cabin_a`
the level's navigation link of the door's number also opens.

| Door type | Kind | Placed | What hits do |
| --- | --- | ---: | --- |
| `dyn_door_bar_bani` | `dyn_door_bar_bani` | 51 | Dust and splinters at each hit; broken, four boards fly off |
| `dyn_door_cabin_a` | swinging | 87 | The glass breaks; worn through, both leaves fly off as loose pieces |
| `dyn_door_dclub` | swinging | 2 | Dust and splinters at each hit; half worn, the leaves drop away and the frame shows damage; worn through, pieces fly off |
| `dyn_door_fence` | fence | 38 | Dust and splinters at each hit; broken, the section vanishes and loose wood pieces fly off |
| `dyn_door_liz` | swinging | 1 | Dust and splinters at each hit and four growing stages of damage; worn through, pieces fly off |
| `dyn_door_stall` | swinging | 8 | The first hit breaks it; both leaves fly off |
| `dyn_door_store` | swinging | 33 | Worn through, it swings open away from the character who hit it |
| `dyn_door_vargas` | fence | 2 | Dust and splinters at each hit; broken, it changes to a broken model |
| `dyn_door_wall_a` | fence | 3 | Dust and splinters at each hit; broken, it changes to a broken model |
| `dyn_door_wall_b` | fence | 2 | Dust and splinters at each hit; broken, it changes to a broken model |

The seven breakable swinging types are exactly the swinging records whose object type is `TYPE_BREAKABLE_DOOR` or
`TYPE_BREAKANDENTER_DOOR`, with `dyn_door_cabin_b` and `dyn_door_cabin_c` not placed. Sliding doors and
`dyn_door_barricade` take no damage.

### Sliding leaves

| Door type | Leaves | Record values 15 to 18 |
| --- | --- | --- |
| `dyn_door_ele` | one `dyn_dr_ele` | 0, 0.007, 0, 3 |
| `dyn_door_ele_c` | two `dyn_dr_ele_c` | 0, 0.007, 0, 1.6 |
| `dyn_door_fence_r` | two `dyn_dr_fence_r` | 3.37, 0.007, 2.518, 0 |
| `dyn_door_fence_s` | two `dyn_dr_fence_s` | 0, 0.005, 2.31, 0 |
| `dyn_door_pol` | one `dyn_dr_pol` | 1.88, 0.009, 1.88, 0 |
| `dyn_door_portcullis` | two `dyn_dr_portcullis` | 3.27, 0.01, 3, 0 |
| `dyn_door_rollup` | one `dyn_dr_rollup` | 0, 0.009, 0, 3.1 |
| `dyn_door_spook` | one `dyn_dr_spook_a` and one `dyn_dr_spook_b` | 2.49, 0.01, 2.4, 0 |
| `dyn_door_subway` | two `dyn_dr_subway` | 0.83, 0.012, 0.7, 0 |

In every sliding record exactly one of values 17 and 18 is nonzero.

## Placed door types

Placed counts the PS2 placements and Scripts the scripts that hold them. Leaves names the leaf objects a swinging
or sliding door is made of. Links is yes for the types whose placement marks the level's navigation links of the
door's number as link kind 0x40. Pick is yes for the types whose lock can be picked from the start.

| Type | Placed | Scripts | Kind | Object type | Material | Leaves | Links | Pick |
| --- | ---: | ---: | --- | --- | --- | --- | --- | --- |
| `dyn_door_cabin_a` | 87 | 19 | swinging | `TYPE_BREAKABLE_DOOR` | `GLASS` | two `dyn_dr_cabin_a` | no | no |
| `dyn_door_chainlnk_a` | 69 | 20 | swinging | `TYPE_DOOR` | `CHAINLINK_NOCLIMB` | one `dyn_dr_chainlnk_a` | no | no |
| `dyn_door_bar_bani` | 51 | 8 | own | `TYPE_BREAKABLE_DOOR` | `WOODFURNITURE` | none | yes | no |
| `dyn_door_steel_b` | 41 | 16 | swinging | `TYPE_DOOR` | `CAR_HOOD` | two `dyn_dr_steel_b` | no | no |
| `dyn_door_fence` | 38 | 12 | fence | `TYPE_BREAKABLE_DOOR` | `WOODFENCE` | none | yes | no |
| `dyn_door_store` | 33 | 15 | swinging | `TYPE_BREAKANDENTER_DOOR` | `STOREDOOR_GLASS` | two `dyn_dr_store` | no | no |
| `dyn_door_storeb` | 28 | 13 | swinging | `TYPE_DOOR` | `STOREDOOR_GLASS` | two `dyn_dr_storeb` | no | yes |
| `dyn_door_stlslot` | 21 | 4 | swinging | `TYPE_DOOR` | `CAR_HOOD` | one `dyn_dr_steel_b` and one `dyn_dr_stlslot` | no | no |
| `dyn_door_temple` | 11 | 5 | swinging | `TYPE_DOOR` | `LAMPOST` | two `dyn_dr_temple` | no | no |
| `dyn_door_ornate` | 11 | 6 | swinging | `TYPE_DOOR` | `WOODFENCE` | two `dyn_dr_ornate` | no | no |
| `dyn_door_rollup` | 10 | 5 | sliding | `TYPE_SLIDING_DOOR` | `CAR_HOOD` | one `dyn_dr_rollup` | no | no |
| `dyn_door_dblgate` | 9 | 5 | swinging | `TYPE_DOOR` | `LAMPOST` | two `dyn_dr_dblgate` | no | no |
| `dyn_door_ele` | 8 | 3 | sliding | `TYPE_DOOR` | `WOODFENCE` | one `dyn_dr_ele` | no | no |
| `dyn_door_stall` | 8 | 2 | swinging | `TYPE_BREAKABLE_DOOR` | `WOODFENCE` | one `dyn_dr_stall` | no | no |
| `dyn_door_fence_s` | 6 | 5 | sliding | `TYPE_DOOR` | `CHAINLINK_NOCLIMB` | two `dyn_dr_fence_s` | no | no |
| `dyn_door_woodfnce_xl` | 5 | 2 | swinging | `TYPE_DOOR` | `WOODFENCE` | one `dyn_dr_woodfnce_xl` | no | no |
| `dyn_door_wood` | 5 | 4 | swinging | `TYPE_DOOR` | `WOODFENCE` | two `dyn_dr_wood` | no | no |
| `dyn_door_templedoor` | 4 | 1 | swinging | `TYPE_DOOR` | `WOODFENCE` | two `dyn_dr_templedoor` | no | yes |
| `dyn_door_subway` | 4 | 2 | sliding | `TYPE_DOOR` | `CAR_HOOD` | two `dyn_dr_subway` | no | no |
| `dyn_door_dblwoodfnce_xl` | 3 | 3 | swinging | `TYPE_GENERIC` | `WOODFENCE` | two `dyn_dr_woodfnce_xl` | no | no |
| `dyn_door_strip` | 3 | 2 | swinging | `TYPE_DOOR` | `CAR_HOOD` | two `dyn_dr_strip` | no | no |
| `dyn_door_wall_a` | 3 | 1 | fence | `TYPE_BREAKABLE_DOOR` | `PLASTERWALL` | none | yes | no |
| `dyn_door_dblstlwin` | 3 | 2 | swinging | `TYPE_DOOR` | `CAR_HOOD` | two `dyn_dr_stlwin` | no | no |
| `dyn_door_barricade` | 3 | 1 | own | `TYPE_DOOR` | `WOOD` | not established | no | no |
| `dyn_door_redgate` | 2 | 2 | swinging | `TYPE_DOOR` | `LAMPOST` | two `dyn_dr_redgate` | no | no |
| `dyn_door_chainlnk_ap` | 2 | 2 | swinging | `TYPE_DOOR` | `CHAINLINK_NOCLIMB` | one `dyn_dr_chainlnk_ap` | no | no |
| `dyn_door_subcan` | 2 | 2 | swinging | `TYPE_DOOR` | `CAR_HOOD` | two `dyn_dr_subcan` | no | no |
| `dyn_door_dclub` | 2 | 2 | swinging | `TYPE_BREAKABLE_DOOR` | `LIZZIE_DOOR` | two `dyn_dr_dclub` | yes | no |
| `dyn_door_vargas` | 2 | 2 | fence | `TYPE_GENERIC` | `PLASTERWALL` | none | yes | no |
| `dyn_door_ornate_single` | 2 | 2 | swinging | `TYPE_DOOR` | `WOODFENCE` | one `dyn_dr_ornate` | no | no |
| `dyn_door_chainlnk_pick` | 2 | 1 | swinging | `TYPE_DOOR` | `CHAINLINK_NOCLIMB` | one `dyn_dr_chainlnk_a` | no | yes |
| `dyn_door_wall_b` | 2 | 1 | fence | `TYPE_BREAKABLE_DOOR` | `MEDWOOD` | none | yes | no |
| `dyn_door_templeshutter` | 1 | 1 | swinging | `TYPE_DOOR` | `WOODFENCE` | two `dyn_dr_templeshutter` | no | no |
| `dyn_door_ele_c` | 1 | 1 | sliding | `TYPE_DOOR` | `WOODFENCE` | two `dyn_dr_ele_c` | no | no |
| `dyn_door_cemgate` | 1 | 1 | swinging | `TYPE_DOOR` | `LAMPOST` | two `dyn_dr_cemgate` | no | no |
| `dyn_door_liz` | 1 | 1 | swinging | `TYPE_BREAKABLE_DOOR` | `LIZZIE_DOOR` | two `dyn_dr_liz` | yes | no |

The seven Links types account for 99 placements. The swinging types that have leaves but are not placed:

| Type | Leaves |
| --- | --- |
| `dyn_door_big_gate` | two `dyn_dr_big_gate` |
| `dyn_door_cabin_b` | two `dyn_dr_cabin_b` |
| `dyn_door_cabin_c` | two `dyn_dr_cabin_c` |
| `dyn_door_chainlnk_ag` | one `dyn_dr_chainlnk_ag` |
| `dyn_door_chainlnk_ar` | one `dyn_dr_chainlnk_ar` |
| `dyn_door_chainlnk_aw` | one `dyn_dr_chainlnk_aw` |
| `dyn_door_corr` | one `dyn_dr_corr` |
| `dyn_door_dblgate_d` | two `dyn_dr_dblgate_d` |
| `dyn_door_red_fence` | one `dyn_dr_red_fence` |
| `dyn_door_shackdoor` | two `dyn_dr_shackdoor` |
| `dyn_door_sheetmtl` | one `dyn_dr_sheetmtl` |
| `dyn_door_stlwin` | two `dyn_dr_stlwin` |

## Placing a door

A level script places a door with five values. The script keeps the handle it gets back in a table under a short
key of its own, for example `Clinic` or `ChA01` in `level2.lua`.

| Value | Content | Values on the disc |
| ---: | --- | --- |
| 1 | Door type name | One of the 36 placed types above |
| 2 | Position x, y, z | Three numbers; z is height |
| 3 | Rotation | Four numbers; the first two are 0 in all 484, and the four always form a unit quaternion (rotation about the vertical axis only) |
| 4 | Collision triangles | Two triangle numbers n and n + 1 |
| 5 | Door number | 1 to N within the script |
| Result | Door handle | The handle that the door commands take |

| Outcome | Value |
| --- | --- |
| Door numbers | Each of the 45 level scripts numbers its N doors 1 to N with no gaps or repeats; `level86_chapter1_reg.lua` places one more door for `level86`, number 12 |
| Door triangles | The door's two collision triangles become two-sided |
| Lock picking | Types with Pick yes accept lock picking as soon as they are placed |
| Navigation links | For the seven Links types, the level's navigation links that carry the door's number become link kind 0x40 |
| Rotations | 31 distinct rotations; the four most common are (z, w) = (0, -1) 129 times, (0.707107, -0.707107) 122 times, (-0.707107, -0.707107) 87 times and (1, 0) 82 times |
| Triangle material | For swinging and sliding doors and `dyn_door_bar_bani` the two triangles take the material of the door type's record |
| Triangle numbers | No triangle number appears twice in a script; each script's triangles form one block of 2N consecutive numbers, for example 726 to 777 for the 26 doors of `level2` |

## Door commands

A level script acts on a placed door through the handle that placing it gave back. Uses counts the places in all 467
compiled Lua scripts on the PS2 disc (level, chapter and other scripts) where the command appears, and Scripts the
scripts holding them. Placing a door is described above (484 uses in 46 scripts).

| Command | Values | Uses | Scripts | What the player sees |
| --- | --- | ---: | ---: | --- |
| Allow or forbid lock picking | Door, yes or no; left out or nil counts as yes | 24 | 16 | Yes, on a closed swinging door: the door locks and a lock-pick marker appears; on an open swinging door nothing changes. Yes, on a closed sliding door: the marker appears. No: the marker goes and a swinging door unlocks; a player picking its lock at that moment leaves the lock-pick minigame without success |
| Ask whether a door is open | Door | 2 | 2 | Gives yes for a swinging door that has finished opening and for an open sliding door, and no for a shut one |
| Check value | Four numbers from 0 to 255 | 62 | 62 | No effect |
| Close a door | Door | 84 | 53 | A swinging door's leaves swing back shut with its close sound; a sliding door's leaves slide shut with its close sound; a barricade blocks again. The doorway blocks again and the door's navigation link closes. Fence-kind doors and `dyn_door_bar_bani` do not react |
| Get a door's left leaf | Door | 74 | 42 | Gives the handle of the door's first leaf; scripts hand leaves to cut scenes |
| Get a door's right leaf | Door | 47 | 29 | Gives the handle of the second leaf, or the empty handle for a one-leaf door |
| Mark a door for the camera | Door | 2 | 2 | The door's two collision triangles get triangle type bit 0x200 |
| Open a door | Door | 85 | 46 | An unlocked swinging door swings open to 170 degrees with its open sound; a locked one stays shut. A sliding door slides open with its open sound and its lock-pick marker goes. A barricade stops blocking. Open, the doorway no longer blocks and the door's navigation link opens. Fence-kind doors and `dyn_door_bar_bani` do not react |
| Open a door for a character | Door, character | 31 | 19 | An unlocked swinging door that stands shut swings 170 degrees away from the character, or to the angle kept from an earlier open-to-angle command; its lock-pick marker goes. A sliding door opens as for Open a door. Locked or open swinging doors and other kinds do not react |
| Open a door to an angle | Door, angle in degrees | 97 | 50 | An unlocked swinging door turns its leaves to that angle from the closed position (0), with its open sound; a door opened this way from shut stops blocking. A locked door stays shut and keeps the angle. Other kinds do not react. The disc's values run from -179 to 220 |
| Stop a door blocking | Door | 0 | 0 | A swinging or sliding door stops blocking without moving, and its navigation link opens |
| Use an object | Object, character | 7 | 6 | The character makes its use move on the object; on a swinging door, the door opens away from the character at the end of the move. Four of the seven uses target buttons, such as `dyn_button` in `level3_balcony.lua` |

The check command appears once in each of 62 level scripts: the 45 that place doors and 17 of the 19 doorless ones
(not `level1.lua` and `level100.lua`). The 45 give four nonzero numbers, for example 15, 40, 31 and 127 in
`level2.lua`, and so does the doorless `level132.lua` (43, 254, 204 and 154); the other 16 give 0, 0, 0 and 0.

Where a use names a door by its placing key in the same level's script (for example the `Doors` entry `ChA06` of
`level2.lua`), its kind can be traced. No traced use reaches a fence-kind door or `dyn_door_bar_bani`.

| Command | Swinging | Sliding | Barricade | Not traced |
| --- | ---: | ---: | ---: | ---: |
| Allow or forbid lock picking | 13 | 0 | 0 | 11 |
| Ask whether a door is open | 1 | 0 | 0 | 1 |
| Close a door | 43 | 5 | 2 | 34 |
| Get a door's left leaf | 53 | 8 | 0 | 13 |
| Get a door's right leaf | 36 | 1 | 0 | 10 |
| Mark a door for the camera | 1 | 0 | 0 | 1 |
| Open a door | 49 | 11 | 4 | 21 |
| Open a door for a character | 23 | 0 | 0 | 8 |
| Open a door to an angle | 55 | 0 | 0 | 42 |

Three more commands take a position (x, y, z) instead of a door and act on the level's navigation links near it.

| Command | Uses | Scripts | Outcome |
| --- | ---: | ---: | --- |
| Block a door link | 2 | 2 | The door link (kind 0x10) nearest the point, within 5, is marked blocked in both directions |
| Turn a jump link into a door link | 5 | 3 | The jump link (kind 4) nearest the point, within 5, becomes a door link (kind 0x10) in both directions |
| Unblock a door link | 2 | 2 | The door link nearest the point, within 5, loses its blocked mark in both directions |

With no link of the kind within 5 of the point, these commands change nothing.

## Differences between PS2 and Xbox

| Item | PS2 | Xbox |
| --- | --- | --- |
| Door placements | 484 | 493: the same 484 plus 9 |
| `level117.lua` | not on the disc | 3 doors: `dyn_door_vargas`, `dyn_door_steel_b`, `dyn_door_wood` |
| `level125.lua` | not on the disc | 3 `dyn_door_ornate` |
| `level73.lua` | not on the disc | 2 `dyn_door_fence_s` |
| Script whose name is not recovered | not on the disc | 1 `dyn_door_storeb` |
| Door object records | 140 | the same 140, value for value |

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x003796b8` | Place a door for a level script | Placing a door |
| `0x003fb5f8` | Set up a swinging door and its leaves | Placed door types |
| `0x003f5d10` | Set up a sliding door and its leaves | Sliding leaves |
| `0x00379ac8` | Open a door by its handle | Door commands |
| `0x00379b78` | Close a door by its handle | Door commands |
| `0x00379e40` | Allow or forbid picking a door's lock | Door commands |

## Acceptance checks

| Outcome |
| --- |
| The PS2 level scripts place 484 doors in 46 scripts, using 36 door types. |
| `level2.lua` places 26 doors numbered 1 to 26, whose collision triangles are 726 to 777. |
| `dyn_door_cabin_a` is the most placed door type: 87 doors in 19 scripts. |
| The record of `dyn_door_store` in `config_preload3.lua` carries `OBJECT.TYPE_BREAKANDENTER_DOOR`, value 30. |
| In `config_preload2.lua`, `OBJECT.TYPE_DOOR` is 15, `OBJECT.TYPE_BREAKABLE_DOOR` 25 and `OBJECT.TYPE_SLIDING_DOOR` 33. |
| Every placement's rotation has 0 as its first two numbers and unit length. |
| A chain-link door (`dyn_door_chainlnk_a`) opens as a single leaf; a cabin door (`dyn_door_cabin_a`) as two. |
| A roll-up door (`dyn_door_rollup`) is one sliding leaf; a subway door (`dyn_door_subway`) is two. |
| The record of `dyn_door_fence` names `dyn_door_fence` as its kind, and four door types share that kind. |
| Forbidding lock picking on a door while the player picks its lock ends the lock-pick minigame without success. |
| The Xbox level scripts place 493 doors, including 3 in `level117.lua` and 3 in `level125.lua`, which the PS2 disc lacks. |
| `config_preload3.lua` holds 59 door type records: 40 of kind `dyn_door_swinging`, 9 `dyn_door_sliding`, 4 `dyn_door_fence` and one each of six kinds named after their own type. |
| Of the 58 leaf records, 39 name `sub_swinging_door` as kind, 10 `sub_sliding_door` and 9 `simple_object`. |
| A stall door (`dyn_door_stall`) breaks at the first hit, and a fence section (`dyn_door_fence`) never swings open but breaks after enough hits. |
| A script's open command swings a closed, unlocked swinging door open to 170 degrees; a locked one stays shut. |
| The PS2 scripts use the open command 85 times in 46 scripts and the close command 84 times in 53 scripts. |
| The 45 level scripts that place doors and `level132.lua` give the check command four nonzero numbers; the other 16 level scripts that use it give four zeros. |

## Open questions

- What values 15 to 18 of a sliding record measure. The data fits a slide distance sideways (value 17) or upward
  (value 18) at a speed (value 16), with roll-up and lift doors moving up, but this was not seen in game.
- Which side a positive angle of the open-to-angle command swings a leaf toward, and what the player sees at values
  past 180 (one use on the disc gives 220).
- What the door check value's four numbers were meant to hold; the command has no effect on this disc.
- Whether the triangle numbers index the collision triangles of the level's `.lev` file; the triangle blocks were
  not matched against the `.lev` collision data.
- Which leaf counts as left and which as right for a two-leaf door.
- How the own-name types `dyn_door_barricade` and `dyn_door_bar_bani` are built (pieces and their models), and
  whether the level itself draws anything where a barricade stands.
- What link kind 0x40 does to characters' route finding through a broken or unbroken door.
- Hit points per door type were not established, so neither was how many hits each breakable type takes; record
  values 3 to 6 differ between types (for example 12 for `dyn_door_cabin_a`, 10 for `dyn_door_fence`, 1 for
  `dyn_door_store`) and may be hit points.
- How the player starts the lock-pick minigame at a locked door (prompt, button) was not examined.
- Which player actions start a character's use move on a swinging door, and whether gang members and enemies open
  doors on their own.
- Which hits (blows, thrown objects, vehicles) wear down a breakable door.
- What triangle type bit 0x200, set by the camera command, changes in play.
- What the four kinds that no level places (`dyn_door_bnstr`, `dyn_door_chain_s`, `dyn_door_fence_o`,
  `dyn_door_parapet`) do in play.
- Whether a lock-pick marker on a sliding door can be picked; no traced use puts one there.
- Whether every door is closed when its level starts; a sliding door receives a start-up state that was not tied to
  an in-game observation.
