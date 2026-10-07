# World objects: spawning, models, tint, glass and doors

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), a disc check (2026-10-06) of what the scripts pass and of the Object List and the `WonderWheel_100`
scene, reported as counts and values, and, for the [objective markers](#objective-markers) and
[held objects](#held) only, PCSX2 2.9.94 over PINE in `level99` (2026-10-06).

## Purpose

What level scripts set on the world they build and what the player breaks or opens in it: how an object a script
spawns finds its **model**, where it stands and what moves it ([Dynamic objects](#dynamic-objects), with the front
end's Wonder Wheel as the worked case), an object's **tint**, the **objective markers** (the "W" a mission asks
the player to walk into), the **breakable glass** panes, the **doors** and the **breakable barriers**. The lists are
[Object tints](../references/tints.md), [Glass types](../references/glass-types.md) and [Doors](../references/doors.md);
the classes and pools of world objects and glass panes are on [Tasks](tasks.md#classes), the crimes a pane can raise on
[AI: crimes](ai.md#crimes), and what the player's attacks do to them on [Combat: breakables](combat.md#breakables).

## Original structure

The code sits in `TaskEngine/`, in the stretches placed by position
([Source map](source-map.md#position)). Names are ours. The script types
(the table at `0x00512f28`, [Particles](particles.md#type-record)) involved: `glass_script` (61), `sub_glass` (210),
`sub_stained_glass` (247), `glasstest` (62, the shards), `dyn_door_swinging` (32), `sub_swinging_door` (249, a leaf),
`dyn_door_sliding` (30), `sub_triglint` (254), and the barriers `dyn_door_fence` (27), `dyn_door_bar_bani` (23),
`dyn_door_bnstr` (25), `dyn_door_chain_s` (26), `dyn_door_fence_o` (28), `dyn_door_parapet` (29).

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00390f18` | `Cfg_AddObjectType` | `CfgObj`: one 0x90-byte object type ([Object types](#object-types)) | confirmed (code) |
| `0x003913d8` | `ObjectDb_FindByName` | a type's index by name (a hash table over the names) | confirmed (code) |
| `0x00396858` | `Obj_Spawn` | `ObjSpawn`: the unlockable checks, then a spawn record | confirmed (code) |
| `0x00398940` | `ObjRecord_Add` | `ObjSpawn`'s spawn record ([Spawn records](#spawn-records)); returns its handle | confirmed (code) |
| `0x00398fe0` | `ObjRecord_GetHandle` | a record's object, spawning it first when not live | confirmed (code) |
| `0x00399080` | `ObjRecord_Spawn` | makes the object from a record; copies the tint | confirmed (code) |
| `0x00399428` / `0x003996a0` | `ObjRecord_Store` / `ObjRecord_Remove` | an object back into its record / gone for good | confirmed (code) |
| `0x00398df8` | `ObjRecord_SetPinned` | record bit `0x10000` and object `+0x10f`: never stored | confirmed (code) |
| `0x00399d88` | `ObjectTaskManager_UpdateSpawns` | streams records in and out around the cameras ([Streaming](#streaming)) | confirmed (code) |
| `0x003918b8` | `WorldObject_Init` | the world object's initialiser: type, script type, defaults, body | confirmed (code) |
| `0x003a44c8` | `Obj_SetModel(object, hash)` | the model by Object List hash (0: the type's) | confirmed (code) |
| `0x001811b0` | `ObjectList_FindByHash` | an Object List record by name hash | confirmed (code) |
| `0x001808b8` / `0x001809c0` | `ObjectModel_IsLoaded` / `ObjectModel_Request` | a record's three resources resident / requested | confirmed (code) |
| `0x00180f58` | `ObjectModel_MakeInstance` | a model instance over the record's clump and dictionaries | confirmed (code) |
| `0x001897a8` | `ResourceManager_LoadNearestModel` | per frame: loads one object's model that is missing | confirmed (code) |
| `0x003eeea0` / `0x003ef840` / `0x003ef188` | `simple_object` init / update / message | the plain prop ([`simple_object`](#simple-object)) | confirmed (code) |
| `0x0017fd78` | `ObjectRender_Draw` | draws one object instance: lights, size cull, fade | confirmed (code) |
| `0x0017fa80` | `ObjectRender_ApplyFadeDistance` | the alpha of an object near its `ObjShow` distance | confirmed (code) |
| `0x00395b70` | `WorldObject_Update` | integrates, copies `+0xcc` to `+0xc8`, runs the class's update | confirmed (code) |
| `0x003e98e8` / `0x003e9b08` / `0x003e9be0` | `dyn_objective` init / update / message | an objective marker's disc ([Objective markers](#objective-markers)) | confirmed (code) |
| `0x003e9828` | `ObjectiveMarker_SetShown` | a marker's shown state, passed on to its column | confirmed (code) |
| `0x003e9d60` / `0x003e9fd8` / `0x003e9ea0` | `sub_objective_column` init / update / message | the marker's column | confirmed (code) |
| `0x003a1810` / `0x003a1858` | `Obj_Attach` / `Obj_Detach` | parent, bone and local pose; detaching keeps the world pose ([Held objects](#held)) | confirmed (code) |
| `0x003a19b0` / `0x003a1ad8` | `Obj_GetWorldPose` / `Obj_GetPoseNow` | vtable `+0xa4` / `+0xac`: an attached object's pose through its parent's bone | confirmed (code), runtime |
| `0x003fe490` / `0x003fe6b8` | `MeleeWeapon_Take` / `MeleeWeapon_Detach` | messages `0x1b` / `0x1c` of `melee_weapon` | confirmed (code) |
| `0x00257f38` / `0x002586d8` | `Human_DropHeld` / `Human_ReleaseThrow` | a held object let go: dropped / thrown | confirmed (code) |
| `0x003a53f0` | `Obj_SpawnChildByName` | creates an object by name at an object's pose, attached to it | confirmed (code) |
| `0x00396bd0` | `Obj_SetColour` | `ObjColor`: packs `{r, g, b, a}` into the tint word | confirmed (code) |
| `0x0038fab8` | `GlassTypes_Set` | `CfgSetGlassProperties`: one entry of the glass type table | confirmed (code) |
| `0x0039c0e0` | `Glass_Spawn` | pushes the pane's arguments, creates it by type name | confirmed (code) |
| `0x0038f8a8` | `GlassManager_Create` | allocates the pane, runs its initialiser, the window link | confirmed (code) |
| `0x003e29e8` / `0x003e3058` / `0x003e2d90` | `glass_script` init / update / message | the pane | confirmed (code) |
| `0x0038f378` | `Glass_Break(pane, breaker, object)` | the alarm, the window link, the window flags | confirmed (code) |
| `0x003e4be0` / `0x003e4cb8` | `sub_glass` init / update | the shatter: sound and shards | confirmed (code) |
| `0x003e44e0` / `0x003e4838` | `GlassTest_Init` / `GlassTest_Update` | a shard (`glasstest`) | confirmed (code) |
| `0x0038f1c8` | `GlassPane_UpdateBodyByDistance` | per frame (vtable `+0x13c`): the pane's body within 50 m | confirmed (code) |
| `0x0038ed68` / `0x0038eec0` | `GlassPane_CreateBody` / `GlassPane_RemoveBody` | the pane's collision body (`+0xe8`) | confirmed (code) |
| `0x0033d070` | `Physics_CreateBoxBody` | a static box body in the 255-body pool, flags `0x8000007a` (the pane's) | confirmed (code) |
| `0x0033f110` | `Human_TestStrikes` | per update, after the move: the human's switched-on strike shapes against nearby bodies | confirmed (code), runtime |
| `0x00219d50` | `Human_OnContact` | the human body's contact handler; an airborne body touching a pane breaks it ([Moving into a pane](#pane-break)) | confirmed (code), runtime |
| `0x0038fa50` → `0x0038ef60` | `GlassManager_QueueDraws` → `GlassPane_QueueDraw` | per frame: each pane into its sprite batch or the near list | confirmed (code) |
| `0x0038f090` → `0x001831c0` | `GlassPane_DrawNear` → `Instance_DrawOneSpriteIm3D` | a pane near a camera, drawn on its own | confirmed (code) |
| `0x00185b38` | `ResourceMgr_Render3DSprites` | the 3D sprite pass ([Drawing a pane](#pane-draw)) | confirmed (code) |
| `0x003966c8` → `0x003963b8` | `World_BreakGlassInRadius` | `BreakGlassInRadius` | confirmed (code) |
| `0x00396390` → `0x003961d0` | `World_BreakObjectsInRadius` | `BreakObjectsInRadius` | confirmed (code) |
| `0x00397230` | `Door_Spawn` | pushes the door's arguments, creates it by type name | confirmed (code) |
| `0x003fb5f8` / `0x003fbba0` / `0x003fb8d0` | `dyn_door_swinging` init / update / message | the swinging door | confirmed (code) |
| `0x003f80f0` | `DoorSwing_SetUpType` | per type name: leaf models, sounds, flags | confirmed (code) |
| `0x003f8fd0` | `DoorSwing_SwingTo` | turns the leaves to the angle at data `+0x00` | confirmed (code) |
| `0x003f9368` | `DoorSwing_ResetLeaves` | puts the leaves back in the closed pose | confirmed (code) |
| `0x003f9770` | `DoorSwing_StateCommand` | message `0x22`: open, close, collision, pickable | confirmed (code) |
| `0x003f9910` | `DoorSwing_OpenBy` | message `0x0b`: a human opens it, away from them | confirmed (code) |
| `0x003f9588` | `DoorSwing_SetPickable` | the lock-pick glint | confirmed (code) |
| `0x003f9ad0` / `0x003fa438` | `DoorSwing_Hit` / `DoorSwing_HitBreakable` | message 1 | confirmed (code) |
| `0x003fb330` / `0x003fb5a0` / `0x003fb430` | `sub_swinging_door` init / update / message | a leaf | confirmed (code) |
| `0x003a2310` / `0x00336a00` | `Task_Integrate` / `Quat_Slerp` | a leaf's rotation step and its in-between pose ([Leaves](#leaves)) | confirmed (code) |
| `0x003b2f40` / `0x003b3220` / `0x003b3158` / `0x003b2180` | `dyn_door_fence` init / update / message / hit | a breakable barrier | confirmed (code), runtime |
| `0x00391c10` | `WorldObject_Remove` | vtable `+0x4c`: message 2 to the object's handlers, out of its record, deleted ([Barriers](#barriers)) | confirmed (code), runtime |
| `0x00250c00` | `NavLinks_SetKindByNumber(number, kind)` | retags every navigation link carrying a number | confirmed (code) |
| `0x00250c50` / `0x00250d00` | `NavLinks_OpenByNumber` / `NavLinks_CloseByNumber` | bit 31 of every link with a number, path flag 8 of its door hole | confirmed (code) |
| `0x002508b8` | `NavLink_DoorPolygon(link)` | the hole at the middle of a door link | confirmed (code) |
| `0x00250100` | `PathPolygon_FindAtPoint(pos)` | the flag-4 hole whose box holds a point | confirmed (code) |
| `0x00250960` | `NavLink_FindNearest(pos, kinds, &link, &back)` | the nearest link of a kind within 5 m, and its reverse | confirmed (code) |
| `0x00417480` | `Flags_DisableNear(radius, pos, activity, 0)` | disables flags of one activity near a point | confirmed (code) |
| `0x003a5340` / `0x003a52c8` | `Obj_RemoveBody` / `Obj_AddBody` | an object's body in the collision world (`0x00597198`) | confirmed (code); names inferred |
| `0x00117280` | `Sound_PlayMaterialPair(a, b, pos)` | the sound matrix entry of two materials ([Sound](sound.md#play)) | confirmed (code) |
| `0x003a6ee8` | `Sound_PlayHashAt(hash, pos)` | a 3D sound by name hash | confirmed (code) |

## Data

### Object types {#object-types}

`CfgObj` fills one 0x90-byte record of the object database (`0x00512c04`, records from its start, a count at
`+0x34bc0`; the binding's fields are on [`CfgObj`](../references/bindings/config.md#cfgobj)). Confirmed (code) at
`0x00390f18`, `0x00391778`, `0x003917a0`, `0x00391340`:

| Offset | Meaning |
| --- | --- |
| `+0x28` | **name**, up to 26 characters (`dyn_s_wwheel_a`): the name scripts spawn and the task is created by |
| `+0x43` | **class** name, up to 20 characters (`simple_object`) |
| `+0x5c` | the class's index in the script type table ([Particles](particles.md#type-record)), or −1 |
| `+0x60` | the record's own index |
| `+0x8c` | **CRC-32 of the name** (the standard table at `0x005d91e0`, as typed: the names are lower case): the Object List key of its model |

The other fields (`+0x58`-`+0x88`) are the binding's arguments. A name is found by `ObjectDb_FindByName`
(`0x003913d8`): a hash table at `+0x34dfc` keyed by `h = 5h + c` over the characters.

### Spawn records {#spawn-records}

`ObjSpawn` does not make an object: it adds a **spawn record** (0x28 bytes) to the `ObjectTaskManager`'s array (`+0x14`,
count `+0x18`; `CfgSetDatabaseSizes` objects + 500 of them) and returns a handle naming the record: its index `<< 16`
with serial 0 ([Tasks: handles](tasks.md#handles)). Confirmed (code) at `0x00398940`, `0x00399080`, `0x00399428`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | rotation: four s16, the quaternion × 4,096 (`0x004f3c70` packs, `vitof12` unpacks) |
| `+0x08` | position `x, y, z` |
| `+0x14` | tint word ([The tint](#tint)) |
| `+0x18` | u16 the flag (marker) named by `flagName`, `0xffff` for none |
| `+0x1c` | parent handle (none: `NilHandle`) |
| `+0x20` | the type's index in its low 14 bits, the **object zone** in its top 10 (`<< 22`) |
| `+0x24` | low 16 bits: the live object's handle index (`0xffff` none); bit `0x10000` **pinned**, `0x20000` **live**, `0x40000` **removed** (never spawned again), `0x80000` `ObjSpawn` flag 2, `0x100000` stored hidden, `0x1000000` `ObjSpawn` flag 64, `0x400000` / `0x800000` a door's left / right leaf; bits 26-29 a state handed to the object on spawn (6: none, the value a new record gets) |

When a record is stored ([Streaming](#streaming)) the object's current pose and tint are written back, so it comes
back where it was left.

### The tint {#tint}

A world object's tint is a colour word at `+0xc8`, copied at `+0xcc`: `0xRRGGBBAA` read as a little-endian word
(bytes a, b, g, r in memory). Confirmed (code):

- `ObjSpawn`'s seventh argument is stored in the spawn record (`+0x14`, `0x00398940`) and copied to the object when it
  spawns (`0x003992a4`). The default, `0xFFFFFFFF`, is white: no tint. The scripts pass 33 others, all with alpha
  `0xFF`.
- `ObjColor(object, {r, g, b, a})` multiplies each component by 255, and the packer (`0x0017aca8`) multiplies by 255
  again; the soft-float conversion (`0x0042c718`) keeps the low 8 bits. Since 65,025 is 1 modulo 256, a whole
  component 0-255 comes out as itself and a 0-1 component does not (1.0 gives 1). The one script that calls it passes
  0-255 values.
- `HuSetSpinningIconColor(human, colour, colour2)` sends message `0x34` with the two words to the human's spinning
  icon (`0x00238b18`); no script calls it.
- The breakable doors write the word too: a broken `dyn_door_liz`, `dyn_door_dclub` or `dyn_door_stall` clears its
  alpha byte (`+0xcc` = `+0xc8` & `0xffffff00`, `0x003fa438`).

**Drawing** (`ObjectRender_Draw`, `0x0017fd78`, confirmed (code); the copy also confirmed (runtime)): each
update `WorldObject_Update` (`0x00395b70`) copies `+0xcc` into `+0xc8`, so a class that writes `+0xcc` sees it take
effect one update later. The model instance holds the word as bytes r, g, b, a (instance `+0x24`-`+0x27`, equal to
`+0xc8` in PCSX2), and the draw sets them as the colour of the instance's geometry before rendering it: the tint
multiplies the model's own colours and its alpha the model's opacity. The alpha is first scaled by the size fade
([The model](#models)), the `ObjShow` distance (below), a fade-in over the first second after the instance appears
(instance `+0x2c`; skipped while object flags `0x800010` has a bit set) and the camera fade (×0.3 for an object
between the camera and the player, instance `+0x34`, eased over 200 ms); **an alpha under 10 is not drawn**. While
the camera fade is below 1 the draw turns z-writing off (RenderWare render state 8) for that object.

**The `ObjShow` distance** (`+0x138`, `ObjectRender_ApplyFadeDistance` `0x0017fa80`): when it is above 0 and the
camera is farther than the distance − 2 m, the alpha is multiplied by (distance − camera distance) / 2, fading the
object out over its last 2 m. An attached object (flag `0x10`) uses its parent's distance and is not drawn beyond
it. Confirmed (code).

### Glass types {#glass}

The `GlassTaskManager` (world object `0x00512c7c` `+0x84c`) keeps a table of 16-byte entries at `+0x14 + 16 × type`,
confirmed (code) at `0x0038fab8`, `0x0038fb00`-`0x0038fb10`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | **window link** (`CfgSetGlassProperties`' second argument) |
| `+0x04` | **alarm** (third) |
| `+0x08` | the whole pane's sprite word (fourth): the pane's sprite rectangle is its low 16 bits (`Task_SetRect`, [Particles](particles.md#sprite-words)) |
| `+0x0c` | the broken pane's sprite word (fifth); 0 hides the pane once broken |

`config_preload2.lua` configures types 0-18.

**The pane task** (`0x100` bytes), confirmed (code) at `0x003e29e8`:

| Offset | Meaning |
| --- | --- |
| `+0x10` | centre: the first corner + half of each edge |
| `+0x54` | flags: `0x8001` at spawn; `4` hidden (also set while no camera is within 50 m), `0x800000` broken |
| `+0x70` | **W**, the width edge: third corner − first corner (`SpawnBreakableGlass`' `cornerV` − `corner`) |
| `+0x80` | the unit normal, normalise(**H** × **W**) |
| `+0x90` | **H**, the height edge: second corner − first corner (`cornerU` − `corner`) |
| `+0xa0` | centre (copy); `+0x70`-`+0xaf` is the matrix the pane is drawn with ([Drawing a pane](#pane-draw)) |
| `+0xb0` | sprite word: batch 0 in the high half, the type's rectangle in the low half (type 14: its own batch, rectangle 0) |
| `+0xb4` | colour `0xRRGGBBAA`: `0x808080e0` whole (0 for type 15), `0xffffff70` once broken |
| `+0xb8`-`+0xc4` | the script's two texture-coordinate pairs, `u0, v0, u1, v1` in push order; nothing reads them back (inferred: no reader among the pane's methods or the draw) |
| `+0xc8` | size word: whole metres of width, height `<< 16` (truncated) |
| `+0xcc` / `+0xd4` | width / height in metres (floats), \|W\| / \|H\| |
| `+0xd0` | squared distance from the centre to the nearest player camera, refreshed each frame (`0x0038ef30`) |
| `+0xd8` / `+0xdc` | the two collision triangles |
| `+0xe8` | its collision body, or 0 (vtable `+0xf4` returns it) |
| `+0xf8` | alarm bits (3 with the alarm, `0x0038ec30`) |
| `+0xfa` | type |

The order of the pops (`0x003e29e8`) against the pushes (`0x0039c0e0`) gives the corner names. The normal's order
(`vopmula vf1, vf2` then `vopmsub vf2, vf2, vf1` with `vf1` = H, `vf2` = W, at `0x003e2b6c`) is H × W; the draw does
not use it. Confirmed (code).

The pane updates every 180 ticks (3 s, [Tasks](tasks.md#wheel)); the update only calls an empty hook for an
unbroken type 1 within 100 m (`0x003e3058`).

### Swinging doors {#doors}

`dyn_door_swinging` is a frame object with one or two **leaves**, each a separate `sub_swinging_door` object. The door
task: position `+0x10`, rotation `+0x20` (`SpawnDoor`'s), flags `+0x54` = `0x405`, triangles `+0xd8` / `+0xdc`, door
number `+0xe0`, hitpoints mirror `+0x128` (what `GetHitpoints` reads). Its **data block** (vtable `+0x18c`), confirmed
(code) at `0x003fb5f8`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | the angle it swings to, degrees (0 closed) |
| `+0x04` | an angle `DoorOpenDegree` left while the door was pickable |
| `+0x08` | **hitpoints**: the type's `CfgObj` third argument (`+0x58`); message `0x19` sets it |
| `+0x0c` / `+0x10` | open and close sound (name hashes) |
| `+0x14` / `+0x18` | left / right leaf handle (`GetLeftDoorHandle`, `GetRightDoorHandle`; message `0x10`) |
| `+0x1c` | 1 once the leaves are registered with the object manager |
| `+0x20` | **pickable** (the lock-pick door) |
| `+0x24` | the pickable glint's handle |
| `+0x28` | **state** byte, below |

**Per type** (`0x003f80f0`, by name; leaves are the `dyn_dr_*` objects). A second leaf stands at
`position + rotation × (−2 × w, 0, 0)` turned 180° about the vertical, where `w` is one leaf's width, `CfgObj`
argument 15 ([Leaves](#leaves)); the leaf objects get `+0x124` = 2 (left) and 4 (right). Confirmed (code):

| Door types | Leaves | Open / close sound |
| --- | --- | --- |
| `dyn_door_big_gate`, `_cemgate`, `_dblgate`, `_dblgate_d`, `_redgate` | 2 | `0x43e4b743` / `0x2d88622b` |
| `dyn_door_chainlnk_a`, `_chainlnk_pick`, `_chainlnk_ag`, `_ap`, `_ar`, `_aw` | 1 | `0xca9bdea4` / `0x53928f1e` |
| `dyn_door_dblstlwin`, `_steel_b`, `_stlslot` (second leaf `dyn_dr_stlslot`), `_stlwin` | 2 | `0xc09823be` / `0x62b026b4` |
| `dyn_door_strip` | 2 | `0xc09823be` / `0xd8831bf0` |
| `dyn_door_dblwoodfnce_xl` (2), `dyn_door_woodfnce_xl` (1) | | `0xa75ac8f5` / `0x4954a9d9` |
| `dyn_door_red_fence`, `_sheetmtl` | 1 | `0x34062c42` / `0xb0443866` |
| `dyn_door_corr`, `_ornate_single` (`dyn_dr_ornate`), `_stall` | 1 | default |
| `dyn_door_cabin_a`/`_b`/`_c`, `_dclub`, `_liz`, `_ornate`, `_shackdoor`, `_store`, `_storeb`, `_subcan`, `_temple`, `_templedoor`, `_templeshutter`, `_wood` | 2 | default |

The default sounds are `0xecc3ab3c` / `0xb70e3ee2`. Other `dyn_door_swinging` types (`_dblwood`, `_woodbrd`,
`_woodp`, `_steel`...) get no leaves. Also per type: `_chainlnk_pick`, `_storeb` and `_templedoor` start **pickable**;
`_dclub` and `_liz` retag their number's links to `0x40`; the three cabin doors start in state 10 with collision
triangle bit `0x800` and a 180-tick update.

**Collision**: both triangles get triangle flag bit 1 (two-sided), bit 10 (`0x400`), bit 6 (`0x40`) when data
`+0x08` is non-zero at spawn, and the type's material (`CfgObj` 13th argument, type `+0x64`) as their material byte
([Collision](collision.md#triangles)). Confirmed (code) at `0x003fb5f8`.

**Hitpoints** (`CfgObj` third argument; disc values): `dyn_door_store` 1, `dyn_door_wall_a`/`_b` 3, `dyn_door_fence`
and `dyn_door_stall` 10, the cabin doors 12, `dyn_door_dclub` 20, `dyn_door_vargas` 1, the rest 100.

### Navigation links {#nav-links}

The level's navigation links are the path data's **D records** (edges, [Level loading](level-loading.md#path-data)):
8 bytes, `+0x00` the node, `+0x04` the edge flags (u16, the **kind**) and `+0x06` a u16 whose low 13 bits are a door
number and whose top bit is the word's bit 31, the "avoid" bit: route planning adds 1,600 to the edge's cost
([AI: path planning](ai.md#path-planning)), so a closed door makes a detour preferred, not mandatory. `0x00510590` is
the array, `0x00510594` its count. Confirmed (code) at `0x00250960`, `0x00250c00`-`0x00250d00`.

**A door's hole**: opening and closing by number (`0x00250c50` / `0x00250d00`, from the door's state commands with
the second argument 1, `0x003a4b00`) change the avoid bit of every link with the number, walking the D records from
the last, and the path flags of one polygon, found from the first link they meet (the highest index):

1. `0x002508b8`: the link's node (D `+0x00`), then that node's last link of kind `0x10` or `0x40` (searching its D
   records from the end), and the middle of those two nodes, so the middle of the doorway;
2. `PathPolygon_FindAtPoint` (`0x00250100`) at that point: of all polygons, only those with path flag 4 and at least
   one vertex whose box holds the point; one such polygon is the answer, several are narrowed by the inside test
   (`0x0024eef0`) to the one whose vertex average is nearest, none gives 0 (the original then writes through a null
   pointer: do nothing);
3. opening sets bit 3 (8) of that polygon's `u16` flags at `+0x48`, closing clears it.

The polygon is a **hole** of an area ([Path data](level-loading.md#path-data)), never the area's outline, which
has no flag 4. Opening it lets lines and routes cross the doorway ([AI: path planning](ai.md#path-planning)). Disc
check (level87, door 15): links 1530 and 1537 (kind `0x10`, nodes 219 and 220, 1.4 m apart); the middle lies in the
boxes of the street's outline (polygon 0) and of polygon 33, a clockwise 4-vertex hole of flags 7, about
0.8 × 2.8 m, the only flag-4 candidate: polygon 33 gets flag 8.

| Kind | Who sets it | How a follower takes it ([Following a route](ai.md#route-follow)) |
| --- | --- | --- |
| 4 | the data | `0x0029baa8`: faces the waypoint, then either runs or starts a jump (`Human_BeginJump`) |
| `0x10` | the data; `ConvertJumpToDoor` (4 → `0x10`) | walked while the door is open; with the avoid bit set the move fails (brain `+0x284` = 4) |
| `0x40` | breakable doors (by number) and window-linked panes | `0x0029bca0`: faces the waypoint, sets the move action to 4, and issues the **charge** (attack kind 19, command `0x20`) |

So an AI routed through a breakable door or a pane charges it. Confirmed (code) at `0x0029ad04`-`0x0029adb8`; every
human searches with the mask `0xff`, which admits them ([AI](ai.md#path-planning)).

## Behaviour

### Dynamic objects {#dynamic-objects}

A **dynamic object** is a world object a script places with `ObjSpawn(typeName, pos, rot, -1, zone, flags, tint,
flagName)`: props, weapons, pick-ups and the front end's Wonder Wheel (`dyn_s_wwheel_a`, its carts
`dyn_s_wwcart_simple_*` and the signs `dyn_s_neon_*`, [Scripts](scripting.md#level100lua-the-front-end)). Its life:
a spawn record, then an object made from the record when it is wanted, then a model attached when the model's
resources are in memory, then whatever moves it.

#### Spawning {#spawning}

1. **`Obj_Spawn`** (`0x00396858`): a name starting `dyn_key` (7 characters, `0x00581010`) is skipped unless
   unlockable 13 is set, and `dyn_powercuffs` (`0x00581018`) unless unlockable 14 is set and game state `0x0041d160`
   is 0; then `ObjRecord_Add` with the position, rotation, zone, flags, tint and flag name, the parent `NilHandle`.
   Confirmed (code).
2. **`ObjRecord_Add`** (`0x00398940`) fills the [record](#spawn-records) and returns its handle. Only in level 84, a
   `TYPE_GLASS` type is spawned at once. Confirmed (code).
3. **The object appears** when something wants it, by either path. Confirmed (code):
    - **resolving the record's handle** (`Handle_Resolve` → `ObjRecord_GetHandle`, `0x00398fe0`) spawns it now when
      it is not live. Any binding taking the handle does this: `SceneAddObject` (`Scene_BindObject`, `0x00354280`),
      `ObjColor`, `ObjDestroy` ...;
    - **streaming** ([below](#streaming)) spawns it when a camera comes within 70 m.
4. **`ObjRecord_Spawn`** (`0x00399080`): when the pool is full it first frees a slot (`0x003998f0`); it pushes the
   position (w = 1), the rotation (unpacked) and the parent into the message scratch and creates a task **by the
   type's name** (record `+0x28`) in the `ObjectTaskManager` ([Tasks: classes](tasks.md#classes)); the record gets the
   object's handle index and the live bit; the object gets the tint (`+0xc8`, `+0xcc`) and the zone (`+0x114`);
   record bits then send message `0x12` (`0x200000`), mark a door leaf (`+0x124` = 2 or 4), hide it (`0x100000`:
   message `0x3c` and object flag `0x800000`); a state other than 6 goes to the object's vtable `+0x124`.
5. **`WorldObject_Init`** (`0x003918b8`): `Task_Init`; the type found by name (object `+0x112` its index, `+0xc0`
   the type's name); the type's **class** record of the script type table handed to vtable `+0x194`; colour words
   `+0xc8`/`+0xcc` = `DAT_005fd268`; hitpoints `+0x128` from type `+0x58`; the collision body by shape
   (`0x00391d48`: shape 1 a box of the type's size, 2 a sphere of radius half its x); a handle (`+0xe4`). Then the
   class's own `init` runs with the pushed arguments (for `simple_object`, [below](#simple-object)). Confirmed (code).

`ObjSpawn` therefore returns at once, and nothing is drawn until the record is resolved or streamed in. In
`level100.lua` the 29 Wonder Wheel objects are spawned when `WonderWheelAnim:startScene` binds them to the scene
(`SceneAddObject` resolves each handle). Inferred from the paths above.

#### The level's placed objects (`_objs.txt`) {#objs-file}

`InitLevel`'s step 7 ([Level loading](level-loading.md#initlevel)), after the level script's main chunk, reads
`<level>_objs.txt` (`0x00398598`): a count (`%d`), then one line per object, `{name {x,y,z}, {qx,qy,qz,qw}, -1, zone,
flags, tint, flagName )` (format at `0x005811f0`), the arguments `ObjSpawn` takes. A name starting `part` (`0x00581228`)
makes a particle emitter task by that name at the pose instead; every other line goes to `ObjRecord_Add` directly (no
unlockable checks). Confirmed (code); that the fields mean what `ObjSpawn`'s do is inferred from their order.
`level99_objs.txt` holds 112 lines: 14 emitters and 98 objects; 89 lines name zone 0 (the emitters among them) and 23
zone 26 (the two stores' jewellery and a cash register, flags 2), a zone `BNESetup` (`global.lua`, called by
`level99.lua`'s `RegisterObjects`) enables, as it does each of zones 21-32 the level defines. Disc check (NTSC-U,
2026-10-06, counts only); the store's items at runtime on [Combat: breakables](combat.md#breakables).

#### Streaming {#streaming}

`ObjectTaskManager_UpdateSpawns` (`0x00399d88`, called from `TaskManager_TickGame`) walks up to 512 records a call,
round-robin from where it stopped (`+0x44`), and skips everything while the task manager's `+0x82c` is set.
Confirmed (code):

- **In**: the nearest record that is not live, not removed, whose zone is enabled (the bit mask at manager `+0x20`,
  [Tasks](tasks.md#classes)) and whose squared distance to the nearest camera (`0x00120230`) is under 4,900 (70 m) is
  spawned, at most one a call; when the pool is full, the farthest storable live object beyond it is stored first.
- **Out**: a live record not **pinned**, whose object can be stored (`0x00395078`: not pinned, flag `0x40000` clear,
  among checks not traced) and which is farther than the first camera's distance (its vtable `+0x214`) + 10 m, or whose
  zone is off, is **stored** (`0x00399428`: pose, tint and state written back, the object freed), or removed for
  good when its own check (`0x00399718`, objects that fell out of the world) says so.
- **Pinned** records stay: `SceneAddObject` pins its object (`0x00398df8`, record bit `0x10000`, object
  `+0x10f` = 1), so a scene's objects are never streamed out while bound. Confirmed (code).

What the first camera's vtable `+0x214` returns (the draw distance, inferred) is not traced.

#### The model {#models}

An object's **model** is the Object List record whose name hash is the type's (`CRC-32` of the type name,
[WAD contents](formats/wad-contents.md#object-list)); the class's initialiser asks for it. Confirmed (code) at
`0x003a44c8`, `0x00180f58`, `0x001897a8`:

1. **`Obj_SetModel(object, hash)`** (`0x003a44c8`): releases a model the object had (`+0x104`); hash 0 means the
   type's own (CRC-32 of type `+0x28`); `ObjectList_FindByHash` (`0x001811b0`, a linear search of resource manager
   `+0x94`, count `+0x90`). With no record the object has no model and the call returns 0. Otherwise it returns the
   hash, which the class stores at object `+0xc4`, and, when the record's resources are resident
   (`ObjectModel_IsLoaded`, `0x001808b8`), makes the instance at once.
2. **The resources** of a record: the **model** (`+0x08`, a `0x47` clump resource; sizes `+0x14` and `+0x18` are what
   the memory pool is asked for), the **texture dictionary** (`+0x0c`, size `+0x1c`) and an optional **second
   dictionary** (`+0x10`, size `+0x20`), each looked up by hash in the resource manager's maps (`+0x10`, `+0x30`)
   and reference-counted. That packs, the level file and standalone WAD entries all feed those maps is inferred.
3. **The instance** (`ObjectModel_MakeInstance`, `0x00180f58`): a 0x40-byte object (`0x0017faf8`) over the clump
   and the one or two dictionaries; `0x00395498` puts it at object `+0x104`, sets `+0x108` = 3 and points the
   instance back at the object (`+0x3c`).
4. **When not resident**: the object stays without a model. `ResourceManager_LoadNearestModel` (`0x001897a8`,
   from the world manager's service `0x0040f8a0`, which the game modes' updates call) takes one waiting human,
   object or car (handles at resource manager `+0xbc4`, `+0xbd4`, `+0xbf4`, the nearest by `0x00189750`,
   inferred) and, for an object with no instance, requests its record's three resources (`ObjectModel_Request`,
   `0x001809c0`, which first checks they fit), then attaches the instance once they are in. How the
   three handles are chosen is not traced.

Disc check (counts and hashes): the eight Wonder Wheel types have records whose model is `<name>_geo`, no second
dictionary and no variant; the three cart types share one dictionary (`0x4a9e1bf5`) and the four neon types another
(`0xf2a65bf6`), the wheel has its own (`0xad06c88d`). 33 of the 1,406 records have a second dictionary. Whether
`level100`'s packs hold them (resident at once) or they stream in is not checked.
The models' own frames are the z-up to y-up turn with an authored translation (the wheel none, the neons
(−29.32, 19.94, 0.07), the carts (−78.2, 0, 0)); drawn at their scene poses without that translation, the neons ring
the hub and the carts ride the rim as at runtime ([Front end](frontend.md#background)), so the instance is taken to
replace the frame's translation. Inferred.

**Drawing** (`ObjectRender_Draw`, `0x0017fd78`, confirmed (code) for what is cited): the lights are chosen per object
(`LightManager_SelectLights`, [Lighting](lighting.md)); an object whose `+0x124` is 0 and whose bounding radius over
its squared camera distance is under 0.0004 is **not drawn**, and fades out between 0.0004 and 0.0005. A
`dyn_s_wwcart_simple_*` (radius about 1.5 m) at the front end's 80 m would be culled by that rule; `simple_object`
gives the Wonder Wheel types `+0x124` = 5, which exempts them ([`simple_object`](#simple-object)).

#### `simple_object` {#simple-object}

The class of most props (`dyn_s_*` and others; record `0x00583a30` of the script type table: init `0x003eeea0`,
update `0x003ef840`, message `0x003ef188`, flags 8). Confirmed (code):

- **Init**: update interval 240 ticks (4 s); pops the parent handle, the rotation and the position (`+0x20`,
  `+0x10`); flags `+0x54` = 1, or `0x11` with a parent (attached to it, `0x003a18b8`); `Obj_SetModel(object, 0)`,
  the hash kept at `+0xc4`. Then by model hash: 14 hashes (the eight Wonder Wheel types among them) set `+0x124` = 5;
  others set flags `0x80`, `0x8080` or `0x20`; two (`0x14e8682c`, `0x014d30ce`) get an interval of 60 ticks and a
  looping sound (`0x326071de`, `0x26ac304b`).
- **Update**: nothing but the looping sound of those two: within the sound's range + 10 m it plays every 2 ticks,
  otherwise it stops and checks every 60. A plain prop does not move itself.
- **Messages**: `0x12` **shows** it (clears flag 4); `0x13` **hides** it (sets flag 4; three model hashes set
  `0x14000000` instead); `0x3c` hides it; `8` puts it back at its attached pose; `0x0a` sets whether it can be hit;
  `0x1b`, `0x1c`, `0x32` attach it to or detach it from a human; `0x20` stops its sound.

That flag 4 stops the draw is inferred (it is a glass pane's hidden bit too, [Glass types](#glass)).

#### Objective markers (`dyn_objective`) {#objective-markers}

The **marker** a mission asks the player to walk into is two world objects: a disc with a "W" on it that turns
about the vertical, at the top of a translucent coloured column standing on the ground. Scripts place it with
`ObjSpawn` of a `dyn_objective` type, show it with `ObjShow` and remove it with `ObjDestroy`; the volume box the
player walks into is a separate object ([Scripting: triggers](scripting.md#triggers)). The marker has no radar part:
a script that wants one adds a radar objective itself ([HUD](hud.md#the-radar-on-screen)).

**In `level99`** (inferred from the script's calls): `RegisterObjects` spawns four `dyn_w_mission` (the script's
`Objects.dyn_w_mission01`-`04`), each standing in a volume box. `P1.SetupCam` sets the first hint, calls
`ObjShow(Objects.dyn_w_mission02)` and waits for message 3 of the box `vMark01`; `P1.FirstGlow` clears that handler,
`ObjDestroy`s marker 02, sets the next hint, waits on `vMark03` and shows marker 03; `P1.DoneCamera` destroys marker
03. Confirmed (runtime), PCSX2 2.9.94 with the first hint on screen: marker 02 stands at (−283.27, 127.07, 0.3) in
`vMark01`, marker 03 at (−287.32, 121.55, 0.3) in `vMark03` with alpha 0 until shown.

**The disc** (`dyn_objective`, script type 45, record `0x005132ac`: init `0x003e98e8`, update `0x003e9b08`, message
`0x003e9be0`, flags 8). Confirmed (code) unless marked:

- **Init**: pops the position and rotation; flags `+0x54` = 1; `Obj_SetModel(object, 0)` (the type's own model,
  hash at `+0xc4`); **angular velocity** (`+0x40`, set through vtable `+0x7c`) = (0, 0, π/2), which
  `Task_Integrate` applies each update: it turns about the world z axis at **90° per second**, one turn in 4 s
  (confirmed (runtime): 180° in 120 ticks); update interval 2 ticks (30 Hz); `+0x124` = 1, which exempts it from
  the size cull ([The model](#models)).
- **The column**: creates an object of type `dyn_objective_a` (`Obj_SpawnChildByName` `0x003a53f0`, the name at
  `0x00585a70`) at the disc's position, attached to the disc, and sends it message `0x34` with a colour chosen by
  the disc's model hash (below). Then it sets itself, and so the column, **hidden**.
- **Shown state** (`ObjectiveMarker_SetShown` `0x003e9828`): message `0x0a` with 1 (`ObjShow`) or message `0x3c` sets
  it shown and object flag `0x800000`; `0x0a` with 0 (`ObjHide`) sets it hidden and clears the flag. Either way the
  disc sends the same `0x0a` to its column.
- **Update** (every 2 ticks): the tint's alpha byte steps **8** towards 255 while shown and towards 0 while hidden
  (written to `+0xcc`, so `+0xc8` follows an update later). A full fade takes 32 updates, **about 1.07 s**
  (confirmed (runtime): 0 → 255 and 255 → 0 each in 64 ticks, disc and column in step). Once the disc is dying
  (below) and its alpha is 0, the update returns 1 and `WorldObject_Update` removes it.
- **Messages**: `0x15` (destroy by message, `ObjDestroy(object, true)`) marks it dying, sends `0x15` to the column
  and hides both, so they fade out and go; `0x19` and `0x22` change its model (`Obj_SetModel` with a popped hash);
  `0x20` removes the column at once.

The column's colour, a word `0xRRGGBBAA` with alpha 0 (the fade supplies the alpha), by the disc's type:

| Disc types | Column colour | |
| --- | --- | --- |
| `dyn_w_mission`, `dyn_objective_yellow`, `dyn_throwtarget` | `0xC1A04700` (193, 160, 71) | yellow-gold |
| `dyn_w_goto`, `dyn_objective_w`, `dyn_objective_red` | `0x99121300` (153, 18, 19) | red |
| `dyn_w_bonus`, `dyn_objective_green` | `0x5F447000` (95, 68, 112) | purple |
| any other (`dyn_w_cinematics`, `dyn_w_mission_b`) | `0xFFFFFF00` | white |

The code compares model hashes (`0x27af4fe0`, `0x39cbfb46`, `0x646520ba`; `0xa83a74da`, `0xebef30bb`, `0x34af4687`;
`0x14dc9db8`, `0x7c280227`); the type names are those whose CRC-32 they are (disc check). The disc's own tint stays
the spawn record's, white for every marker `level99` places, so the disc shows its model's colours.

**The column** (`sub_objective_column`, script type 222, record `0x00514080`: init `0x003e9d60`, update `0x003e9fd8`,
message `0x003e9ea0`), type `dyn_objective_a` (`TYPE_GLASS`, no collision shape, box 0.68 × 0.68 × 3.68 m,
[Objects](../references/objects.md#obj-dyn-objective-a)). Confirmed (code) unless marked:

- **Init**: pops the parent, rotation and position; flags `0x11` (attached; parent at `+0x58`); its own model (hash
  `0x1f3b85ea`); local position zero; angular velocity (0, 0, −π/2), the disc's turn backwards, so the column keeps
  its world orientation while the disc turns (inferred: an attached object's pose is relative to its parent's);
  update interval 2 ticks; shown.
- **Messages**: `0x34` the colour (`+0xcc` = the word, `+0xc8` = the word with alpha 1); `0x0a` shown or hidden;
  `0x15` dying and hidden. Its update is the disc's fade.
- Its model is a cylinder, white at the foot and fading out towards the top (the reference image); tinted gold it is
  the translucent yellow column of the runtime picture.

**How it looks** (confirmed (runtime), PCSX2 2.9.94, the tutorial's first hint): a translucent gold column on the
ground, a little taller than a human, with the opaque "W" disc at its top, its face turning past the camera; the
column is see-through and fades upwards. The disc's model (`dyn_w_mission`, hash `0x27af4fe0`) is a gold coin with a
black "W" ([Objects](../references/objects.md#obj-dyn-w-mission)), its `CfgObj` box 0.52 × 0.52 × 2 m.

**Drawing.** Both are ordinary world objects drawn by `ObjectRender_Draw` with their tint as the geometry colour
([The tint](#tint)): no separate pass, the scene's lights selected as for any object, nothing drawn while the alpha
is under 10. The disc is never size-culled (`+0x124` = 1). `ObjShow`'s distance fades both over its last 2 m when a
script passes one; `level99` passes none. Confirmed (code). How the column's translucency is blended (its material
and vertex alpha, the PS2 pipeline's blend) is not traced.

**When it goes**: the script's `ObjDestroy(handle)` (no second argument) removes the disc at once (its vtable
`+0x4c`) and the attached column with it, without a fade. Confirmed (code) for the disc; confirmed (runtime) for
both: their handles are empty three updates later and nothing is drawn. Only `ObjHide` or a destroy by message fades
a marker out.

**Other marker kinds**, for later:

- `dyn_w_goto` and `dyn_w_mission` as the Rumble race's carrots and the box glows ([Rumble](rumble.md));
  `dyn_objective_w` (red, a larger "W") at `level99` checkpoint 2 for Vermin
  ([Scripting](scripting.md#level99-checkpoints)).
- `sub_objective_glow` (script type 223, init `0x003ea0b0`, update `0x003ea260`, message `0x003ea198`): a 120 × 120
  sprite of the `lighting` sheet, rectangle 3 (a corona), alpha 255, pulled 0.32 m towards the camera every 2 ticks;
  message `0x0a` shows or hides it. Who spawns it is not traced.
- `dyn_throwtarget` (gold), `dyn_w_bonus` and `dyn_objective_green` (purple), `dyn_w_cinematics` (white): the same
  class; their uses are not traced.

#### What moves them: the Wonder Wheel {#wonder-wheel}

No object code turns the wheel, swings a cart or flickers a neon: `simple_object` does not move, and the 29 types
have no class of their own. A **scene** moves them. `level100.lua` binds them to `WonderWheel_100` (scene id 34; slots
0-7 `_a` carts, 8-15 `_b`, 16 the wheel, 17-24 `_c`, 25-28 `neon_a`-`d`, [Scripts](scripting.md#level100lua-the-front-end))
and plays it looping in world coordinates; `level99.lua` does the same with `WonderWheel_99`, the same tracks without
a camera ([Scenes](scenes.md#superrunscene)). Each update (every 2 ticks, 30 a second) the scene's runner sets every
bound object's transform from its track: positions linear, rotations slerped between keys
([Scenes: objects](scenes.md#camera)). Confirmed (code) for the mechanism; the motion below is read from the disc
(counts and values, 2026-10-06):

- **The loop**: 600 frames (20 s). Every object ends where another starts, so the loop is seamless: the wheel and the
  four neons each end turned **45°** from their start (one rim car's spacing), every rim car ends on the next one's
  start, and the sliding cars end on another sliding car's start.
- **The wheel** (slot 16) and **the neons** (25-28): fixed at the hub (515.51, −68.89, −188.67), four rotation keys
  (frames 0, 255, 508/510, 600) turning them 45° about the axle: about **2.25° a second**, a turn in 160 s. The neons
  turn with the wheel (their own start angles differ).
- **The rim cars** (`_c`, slots 17-24): on a ring 21.55 m from the hub, 20 position keys and about 45 rotation keys
  each; they keep hanging level, swinging at most 6.6° about it.
- **The sliding cars** (`_a`, `_b`, slots 0-15): between 12.6 m and 22.2 m from the hub, 16-372 position keys each:
  the cars that run along curved tracks between the rim and the inner ring, tipping up to about 60° on the way.
- **The neon flicker**: scene events on the neon tracks, type **24** (message `0x12`, **show**) and **25** (message
  `0x13`, **hide**), confirmed (code) at `0x00354d98` and `0x003ef188`. Frames (30 a second), from the start of the
  loop:

    | Neon | Hidden | Shown |
    | --- | --- | --- |
    | `neon_a` | 0-60, 120-210, 270-360, 420-510, 570-600 | 60-120, 210-270, 360-420, 510-570 |
    | `neon_b` | 0-30, 60-180, 210-330, 360-480, 510-600 | 30-60, 180-210, 330-360, 480-510 |
    | `neon_c` | 30-150, 180-300, 330-450, 480-600 | 0-30, 150-180, 300-330, 450-480 |
    | `neon_d` | never | always |

    So the signs take turns (c, b, a every 5 s, each lit for 1-2 s) over the always-lit `neon_d`.
- **The camera**: the scene's own, fixed at (462.60, −122.35, −187.93) for the whole loop ([Front end](frontend.md#background)).

When the scene stops (`SceneStop`, a movie, a level start) the objects are released where they stand; playing it
again restarts every track from frame 0. Inferred from [Scenes: ending](scenes.md#ending).

### Pickable objects {#pickable}

Triangle's pick-up search ([Breakables](combat.md#breakables)) and `Human_PickUpObject` take only objects with flag
`0x8000` (object `+0x54`). Each class's `init` sets the flags, confirmed (code):

| Class | Init (`+0x54`) | Pickable | Exceptions |
| --- | --- | --- | --- |
| `melee_weapon` (`0x003fd420`) | `0x218081` | yes | bit `0x80` cleared for model hash `0x13ff8ee4`; its message `0x19` sets or clears `0x8000` |
| `thrown_weapon` (`0x00403090`) | `0x228081` | yes | |
| `overhead_weapon` (`0x003ff6c8`) | `0x228001` | yes | not for model hash `0x8fc6ac30` |
| `pickup_item` (`0x003f17e0`) | `0x208081` | yes | not for model hash `0x2fd690d6` (also cleared in `0x003f23f8`) |
| `powerup_item` (`0x003f2700`) | `0x808081` | yes, but the search skips the class: it is [walked over](player-state.md#walk-over) | |
| `simple_object` | 1 (or `0x11`) | no | `0x8080` added for model hash `0xfcbe9fbb` |

The search also refuses bit `0x10`, bit `0x4000000`, an object whose type value (`CfgObj` field `+0x62`, read through
vtable `+0xdc`) is 75 or more unless the human's record `+0x11b` is 13, the model hash `0xd2cfcd44` for a non-player,
and anything out of sight. At runtime (slot 1) `dyn_masks`, the cash register and the swinging doors had no `0x8000`.

### Objects in a human's hand {#held}

One mechanism carries every object a human holds (a bat, a bottle, a brick, a stolen stereo, a hat being picked
up): the object is **attached to one bone** of the human's skeleton at a **fixed local offset**, and both the bone
and the offset come from an **event in the clip that puts it in the hand**, not from `CfgObj` or a weapon table. The
bat is the worked case ([Combat: a bat in hand](combat.md#bat)); what the human does with it (anim sets, strikes,
`+0x338`) is there too.

**The attachment** (confirmed (code) at `0x003a1810`, `0x003a1858`, `0x003a19b0`): world object fields

| Field | Meaning |
| --- | --- |
| `+0x54` bit `0x10` | attached |
| `+0x58` | the parent task (here the human) |
| `+0x6d` | u8, the parent's **pose bone** ([Animation: bone transforms](formats/animation.md#bone-transforms)); 0 means the parent's own transform |
| `+0x10` / `+0x20` | while attached: the **local** position (vec4) and rotation (quaternion `x, y, z, w`) in the bone's frame |

`Obj_Attach` (`0x003a1810`) sets the bit, the parent and the bone. `Obj_Detach` (`0x003a1858`) first writes the
object's current world pose into `+0x10` / `+0x20`, then clears the bit and the parent, so a detached object starts
exactly where it was drawn.

**The world pose** (`Obj_GetWorldPose`, `0x003a19b0`, slot `+0xa4` of the world-object vtables, e.g. `0x00544c00`):
unattached, `+0x10` / `+0x20`; attached, three transforms composed with `0x003359b0` (rotation `qa · qb`, position
`pa + qa · pb`, no scale):

```text
world = parent's world transform (its vtable +0x9c: for a human, its position and rotation)
      ∘ the bone's transform, when the bone is not 0 and the parent is a human with a model instance
      ∘ the object's local (+0x10, +0x20)
```

A human's bone transform (`0x0023bde8`, its vtable `+0xbc`) is the bone's entry of the per-update bone cache
([Combat: the bone cache](combat.md#grab-posing)'s `0x006b6880` + index × `0x470`, built once per update from the playing
pose, model space) with the **position multiplied by the human's scale** (`+0x65c`, 0.97 for Rembrandt) and the
rotation as is. The object itself is never scaled. `0x003a1ad8` (vtable `+0xac`, the pose extrapolated to now) does
the same for an attached object, the local rotation advanced by its angular velocity (`+0x40`, zero while held).

**At runtime** (confirmed (runtime), PCSX2 2.9.94, slot 1 copy, the player 1 m behind a `dyn_bat_tuff`, triangle,
then stick 60 % up for 35 updates, square, R1 held 30 updates, stick 100 % up for 30 updates, triangle): the bat's
RenderWare frame matrix (its instance `+0x1c` atomic → frame, LTM at `+0x50`, axes as `(x, z, −y)`) equalled
human transform ∘ (scaled bone 25) ∘ local to within 0.005 m and 0.005 in every axis component on 256 of 257 held
updates, read from the values of the update before the draw; the one exception was one update's run distance
(0.26 m at 7.8 m/s). So the draw uses the pose of the latest update, with no smoothing or lag of its own.

**Clip events that place a held object** (confirmed (code) at `0x00101dd8`, which sends the human message
`0x80 + type` with the event's bone (`+6`), position and rotation ([Animation: events](formats/animation.md)); the
human's handler is `0x00245920`):

| Event | Message | What the human does |
| --- | --- | --- |
| 9 | `0x89` | the object being picked up (human `+0x33c`) is sent message `0x1b` **take**, with the human, the bone, the event's position × the human's scale, its rotation, and whether the human is in a scene |
| `0x36` | `0xb6` | the same (the left-hand clips 463 / 464 use it, bone 19) |
| `0x22` | `0xa2` | with a pick target and nothing in hand, as 9; otherwise the held object (`+0x338`) gets message `0x32`: **re-attach** at this bone and offset, nothing else. `GhettoPickUp`'s clips 549 / 550 carry one per frame from 13 to 39, animating the object in the hand |
| `0x34` / `0x35` | `0xb4` / `0xb5` | as 9 / `0x22`, only for object types 24 and 27 |
| `0x37` | `0xb7` | writes bone and offset straight into the object at human `+0x348` (or `+0x34c`); what those hold is not traced |
| 10 | `0x8a` | **release** with something in hand: a throw (`0x002586d8`) under state flags `0x1000010`, else a drop (`0x00257f38`) under `0x4000` or in a scene |

**Take** (message `0x1b`; `melee_weapon` `0x003fe490`, `simple_object` `0x003ef188`, confirmed (code)): velocity and
angular velocity zeroed, attached to the human at the bone, flags `0x6000000` cleared, the local pose set to the
event's. When the human is **not** in a scene, the local position is then moved along the object's own `y` axis by the
type's float at `CfgObj` `+0x70` (the local rotation applied to `(0, v, 0)`), so a model whose origin is not at its
grip is slid to it (0.39 m for `dyn_bat_tuff`). The object then sends the human message 3 (applies its anim set and
puts it in `+0x338`, [Combat: a bat in hand](combat.md#bat)) and message `0x17` (with `CfgObj` `+0x68`; not traced).
There is **no blend**: the object stays where it lay until the event's update and is in the hand on the next draw.
`melee_weapon` keeps the event's position in its data block too, and moves the local position again along `y` by
`CfgObj` `+0x70` / `+0x74` on its messages `0x12` / `0x13` (senders not traced).

**Scripted placement** (`HuPlaceItemInHand`, `0x00238540` → `0x0024c280` → `0x0024c6d8` → `0x00101360`, confirmed
(code)): drops what the human holds, creates the object by name at the human's position, and reads the **first type-9
event** of the low pick-up clip its `pickup_anim` selects ([Combat: the pick-up clip](combat.md#bat)) without playing
it, sending the object message `0x1b` at once; the `+0x70` slide applies when object `+0x110` is −1. Unlike the clip
path the event's position is not multiplied by the human's scale. A left-hand or hat pick-up clip has no type-9 event,
so nothing is attached.

**Drop** (`0x00257f38`, triangle with nothing to take, a pick-up of a two-handed or hat object, a scene's release;
confirmed (code)): pops the anim set the object pushed, records the human as its last holder (object `+0x11c`) unless
the type is 24 or 27, makes its physics body (`Obj_CreatePhysicsBody`), reschedules its task, and sends it message
`0x1c` **detach** with a zero velocity. If the object (flags `0x30000` without `0x400`) now lies farther than
0.35 × scale − 0.01 m from the human across the ground, it is pulled back to that distance. A removal time is set
(object `+0x120` = now + 120 s for type 11, 1 ms for type 39, a game setting at `0x0051489c` `+0x26c` for most
weapons, none for model hash `0x03c5256c`). It then falls under physics from the hand's pose. At runtime the bat
dropped from the hand came to rest on the ground (`z` 0.3, the ground's height there), flags `0xc321a081`
(confirmed (runtime)).

**Throw** (`0x002586d8` on event 10, confirmed (code)): as the drop, but the physics body is made per the type
(`CfgObj` `+0x84` = 2 or `+0x5a` ≠ 1, else `0x003923b8`), the velocity comes from `Human_ComputeThrowVelocity`, and
the human's target is told (`0x0021d5c0`). **Detach** (`melee_weapon` `0x003fe6b8`): the velocity, given in the
holder's frame, is turned by the holder's world rotation. A thrown object (velocity not zero) of anim set 1 or 2 takes
the holder's rotation and the angular velocity (−4π, 0, 0) rad/s (its vtable `+0x7c`); five model hashes get the
holder's rotation and (−8π, 0, 0); anything else keeps its own rotation and gets (0, 0, −kπ), `k` random in 5-7. A
drop keeps its rotation and gets no spin. Which frame the angular velocity is in is not traced.
`simple_object`'s detach only detaches.

**Holstering.** No code moves a weapon to another bone (back or belt): a human holds one object (`+0x338`), in the
hand its clip event named, until it is dropped, thrown or broken; a weapon not in a hand is a free object. Inferred
from the senders of messages `0x1b` and `0x32` above (all clip events or the scripted placement).

`HuRender` (`0x00237e98`) draws a human and then every object attached to it that is not hidden (flag 4), giving each
the human's alpha (confirmed (code)). In the normal draw an attached object skips the small-size fade and is drawn
only within its parent's `ObjShow` distance ([Drawing](#tint)).

### How they get into a level {#placement}

Glass and doors come from level scripts: `SpawnBreakableGlass` (929 calls in 32 chunks) and `SpawnDoor` (484 in
46), in the level's set-up after `CfgSetDatabaseSizes`; nothing else creates a `glass_script` or door task as far as
their creators' callers show (inferred). A pane's geometry is its three corners; a door's is its position, rotation
and type, and its model is the type name's (`CfgObj` `+0x8c`); both name two triangles of the level's static
collision mesh that stand for them. Confirmed (code) for the spawns, counts from the disc.

### A pane's life {#pane}

1. **Spawn** (`0x0039c0e0`): the three corners, the type, the texture coordinates, the flag and the two triangles
   are pushed, and a pane task named by the type in decimal is made. The initialiser (`0x003e29e8`) pops them in
   reverse and drops the flag. Confirmed (code).
2. **Setup**: both triangles are made two-sided (`0x003a4768`) and given material 2 (`GLASS`, `0x0038f8a8`); the
   sprite rectangle is the type's whole-pane word, except type 14, which makes a sprite batch of its own
   (`0x00020000`); type 15 is hidden (colour 0); types 17 and 18 start broken, their triangles disabled, and 18 also
   sets the path polygon's flag 8 (`0x003a47c0`). With the alarm, pane bits `+0xf8` = 3 (`0x0038ec30`). Confirmed
   (code).
3. **Window link** (`0x0038f8a8`): with the window link, the kind-4 link nearest the pane gets the avoid bit
   (`0x00250b68`) and, when the pane lies in a path polygon, it and its reverse are retagged kind `0x40`. Confirmed
   (code).
4. **What breaks it**: anything that sends message 1. There are no hitpoints: the first hit breaks a pane. The
   senders are `Strike_Contact` (`0x0021b290`), a thrown object (`0x00393538`) and `BreakGlassInRadius`
   (`0x003963b8`: every pane whose centre is within the radius, and every `TYPE_GLASS` object). `Strike_Contact`
   runs for a human's strike shape touching the pane's body, which covers a landed hit and a **jump**, and for an
   **airborne** human body touching it ([Moving into a pane](#pane-break)). Confirmed (code).
5. **Hit** (message 1, `0x003e2d90`), once, while not broken: the broken sprite (or the pane hidden when it is 0),
   a `sub_glass` shatter at the pane's centre with the pane's normal (`+0x80`), shard size 0.2 and size word
   (`sub_stained_glass` for type 14), the triangles disabled, its collision body removed (`0x003a5340` →
   `0x0038eec0`, `+0xe8` = 0), colour `0xffffff70`, the pane marked broken. Type 12 also frees every
   `dyn_carstereo` within 2 m (`0x003a5870`), for a pane a script places at a car; a parked car's own windows are
   its parts, not panes ([Cars: windows](cars.md#windows)). Message 0 is a shatter without the broken flag (the
   effect and the triangles only). Confirmed (code).
6. **Break** (`0x0038f378`, called by the human and thrown-object paths, not by `BreakGlassInRadius`):
    - with the alarm (`+0xf8` both bits): the `CrimeScene` flag moves to the pane and a break-in (crime type 1) is
      reported ([AI: crimes](ai.md#crimes));
    - with the window link: when the pane is in a path polygon, the polygon gets flag 8 and its `0x40` link loses the
      avoid bit; otherwise the kind-4 link does;
    - every enabled flag of activity 9 (`_xwWindowLook`, [Flags](flags.md#activities)) within max(width, height) of
      the pane is disabled (`0x00417480`), so nobody stands looking through a broken window.

    Confirmed (code).
7. **Statistics**: a player's (or a player's gang member's) pane counts crime 10 ([Statistics](../references/statistics.md#stat-4-10)),
   at `0x0021b290` and `0x00393538`. Confirmed (code).
8. **No respawn**: nothing clears the broken flag; a pane comes back only when its level's script spawns it again.
   Inferred from the handlers (messages 0 and 1 only).
9. **`ObjDestroy(pane)` is not a break**: `Obj_Destroy` (`0x00396c58`) without its second argument calls the
   object's vtable `+0x4c`, which for a pane (vtable `0x00544ec8`) is `GlassPane_Release` (`0x0038ed08`): body
   removed, script handler and handle freed, the task given back to the glass manager. No shatter, no sound, no
   broken sprite: the pane simply vanishes. `BreakObjectsInRadius` never reaches a pane either (it searches the
   object manager, and panes live in the glass manager; [below](#break-objects-in-radius)). Only
   `BreakGlassInRadius`, a strike, a thrown object or an airborne body breaks one visibly. Confirmed (code).

#### `BreakObjectsInRadius` {#break-objects-in-radius}

`BreakObjectsInRadius(object, radius)` (`World_BreakObjectsInRadius` `0x00396390` →
`World_BreakObjectsAround` `0x003961d0`): takes the object's position, reports a crime there when the object's type
has class byte `+0x86` = 30, finds up to 384 tasks of the **object manager** within `radius` of that point
(`ObjectManager_FindObjects`) and sends each **message `0x15`**, **the given object included** (the "skip myself"
argument is 0 on this path). What `0x15` does is up to each type: a door takes a hit two updates later
([Doors](#doors)), a `dyn_molotv` breaks itself ([Script types: the Molotov](script-types.md#molotov)). So the
scripts' `ObjSpawn("dyn_molotv", flag position)` then `BreakObjectsInRadius(molotov, 0.5)` is a way to set off a
molotov explosion at a point. Confirmed (code).

### Moving into a pane {#pane-break}

A whole pane is two things to a moving human: its two triangles in the level's collision mesh and its **body**
(`+0xe8`), a box in the physics world. The triangles are an ordinary wall; the body is what breaks. Nothing here
depends on the pane's type, its alarm or its window link: every pane gets the same body (`0x0038ed68`), and the only
type tests in the pane's code are the spawn and hit cases of [A pane's life](#pane). Confirmed (code) unless marked.

**The triangles.** Material 2 (`GLASS`) has no rule of its own in the human's wall sweep (`0x00347c08` never reads a
triangle's material) or in its contact handler (`Human_OnContact`, `0x00219d50`, whose level-triangle branch tests
only the fence materials 30, 31 and 122 while climbing, [Characters](characters.md#walls)). Both triangles are
two-sided, so while they are enabled a pane stops a human from either side like any other wall face 0.25 m or taller.
The hit disables them (message 1, [A pane's life](#pane)), and the sweep's resolution skips a contact on a disabled
triangle (`0x0033d9d8` checks the triangle's enabled bit), so a pane broken during a sweep stops blocking at once.

**The body.** `GlassPane_CreateBody` makes a box of **half-extents** width / 2, **0.25** and height / 2
(`0x00342dc0` halves `width, 0.5, height`), posed by the pane's matrix, so it stands 0.25 m proud of the glass on
each side and covers the whole pane. Its body flags (`0x0033d070`) are `0x8000007a`: bits `0x2`, `0x8`, `0x10`,
`0x20` and `0x40`. Two paths reach it.

1. **A strike shape** (the jump, a punch or kick). A shape of the human's body is a strike shape while its flag
   `0x2` is set. The human's strike-on and strike-off messages (`0x00247fc0` / `0x00248110`, from
   `Human_HandleMessage`) go through `0x003428d0`, which keeps a bit mask in body `+0xc0` (a bit per bone byte
   `+0x31`, from bone 2) and sets or clears flag `0x2` on the shape of that bone (shape vtable `+0x1c`). In the jump
   it is the body's own capsule: its flags read `0x11` on the ground and `0x13` from the launch to the landing
   (runtime, below). `Human_TestStrikes` (`0x0033f110`) runs once per update, after the move (`0x0023fea8` calls
   the move `0x0023d8c8`, then it), while body `+0xc0` (or `+0xd0`) is non-zero. It gathers bodies near the human
   whose flags hold `0x18`; for each, a ray from 1 m above the feet to the body's centre must hit nothing in the
   level mesh, the ray excluding material 2 (its exclusion list is `{2, 1}`) and triangles with flag `0x8` (and
   `0x40`, unless the target is a human's body, body `+0x44` bit `0x40`), so glass never hides a pane from a strike.
   A body with any of `0x30` that the human may strike (human vtable `+0x10c`, not traced) is then tested against
   each strike shape (shape flags `0x1` and `0x2`), and the first that overlaps calls `Strike_Contact`
   (`0x0021b290`). For a pane that is: the noise event (`0x002936a8`, 30 m), `Glass_Break` (`0x0038f378`), a
   player's crime-10 statistic and then message 1 ([A pane's life](#pane), steps 5-7).
2. **The airborne body**. The move's sweep asks the physics world for bodies whose flags meet a mask the human
   builds (`0x0033d498`, for a body with `+0x44` bit `0x40`, a human's): `0x4` on the ground; `0x44` while airborne
   (object flag `0x4000000`) or while record `+0x08` holds `0x800`; plus `0x20` while the sweeping shape is a strike
   shape and `0x40000` above gait 3. A pane's body has `0x20` and `0x40` but not `0x4` or `0x40000`, so **only an
   airborne or striking human's sweep meets it**: walking and running on the ground (capsule flags `0x11`) never
   touch the body, only the triangles. On the contact, `Human_OnContact` (object branch) goes on when the body has
   `0x40` and the human is airborne (or record `+0x08` `0x800`, or the landing state flag `0x2000000000`), or the
   body has `0x20` and the touching shape is a strike shape; it calls `Strike_Contact` (vtable `+0x104`) once per
   body (the body's contact list, vtable `+0x34` / `+0x44`) and, because the pane's type flags hold `0x400`
   (`0x0038f588`), returns 0: the contact is ignored and the sweep goes on as if the box were not there.

So a pane breaks for a human who is in the air, or whose strike shapes are on, as soon as his body or a strike shape
reaches the box 0.25 m in front of the glass; there is no minimum speed and nothing slows him. On the ground with no
strike, the glass is a wall. Thrown objects use `0x00393538` ([A pane's life](#pane)).

**At runtime** (confirmed (runtime), PCSX2 2.9.94, a copy of slot 9, 2026-10-07; scenario
[`glass_jump`](repo:research/traces/scenarios/glass_jump.toml), stick at 100 % straight ahead, hooks on
`Strike_Contact` and `Glass_Break`). Slot 9 stands Rembrandt at (63.6, −1.9, 4.20), heading 88°, on the roof of
`level99` checkpoint 3.4, facing the window: two type-11 panes in the plane x = 47.4, side by side (centres y 0.245
and −2.77, a 4 cm mullion between them at y −1.27 to −1.23), from z 4.36 to 7.54.

| Run | What happened |
| --- | --- |
| triangle at 51.4 m (update 60), launch at 9.5 m/s | the jump clip 434 from update 60; from the launch (update 61) to the landing (update 84) body `+0xc0` is non-zero and the capsule's flags are `0x13` (`0x11` before and after); on update 71, feet ending at x 48.33 z 5.26, `Strike_Contact` called from `Human_TestStrikes` (return `0x0033fddc`) for **both** panes, each then `Glass_Break` (return `0x0021bb7c`); x kept falling by 0.26 m per update (7.8 m/s) through the window, landing at x 44.95 on update 84 (clip 435) and stopping at 44.38 |
| no triangle: he runs off the roof edge at 49.6 m | falling (clip 428), feet at z 3.84 on update 72: `Strike_Contact` from `Human_OnContact` (return `0x0021a0e0`) for both panes and `Glass_Break` for each; the panes broke, but his body was below the sill and stopped at x 47.95 against the wall under the window (0.55 m from it) and fell to the street (z 0.22) |

So the original's jump goes through the window on the jump's strike shapes, not on the airborne body path; both
break the pane, and the airborne path alone is enough when the body reaches the box. In `level99` this is the only
thing that opens the window: the script's checkpoint 3.4 code breaks no glass (inferred: no `BreakGlassInRadius` or
pane message in `level99_lesson2.lua`'s `P3` functions).

### Drawing a pane {#pane-draw}

A pane has no model: it is one textured quad, a **sprite** of the resource manager's sprite batches
([GUI: sprite batches](gui.md#resource-instances)), drawn in the translucent 3D sprite pass after the world. Confirmed
(code) unless marked.

**Every frame, before the draw** (`0x0038fbe8` → `GlassPane_UpdateBodyByDistance`, `0x0038f1c8`, for each pane):
`+0xd0` = the squared distance from the centre to the nearest player camera (`0x00120230`); then

- within 50 m (`+0xd0` < 2500): a hidden pane (flag 4) is shown again; a shown pane whose colour is still
  `0x808080e0` (whole) gets its collision body back if it has none (`0x0038ed68`: a box of the pane's width and
  height in the collision world `0x00597198`, posed by the pane's matrix);
- beyond 50 m: a shown pane is hidden; a hidden whole pane loses its body (`0x0038eec0`).

So only whole panes within 50 m of a camera have a body. (One game state, `0x0051489c` `+0x33a` = 4 under a mode
check not traced, skips the distance test and keeps every whole pane's body.)

**Queueing** (`GlassManager_QueueDraws`, `0x0038fa50`, from `TaskManager_UpdateManagers`, once a frame): for each
pane, `GlassPane_QueueDraw` (`0x0038ef60`):

- **near**, `+0xd0` < 12 (within √12 ≈ 3.46 m): the pane goes on the resource manager's near-glass list
  (`0x0018b008`, `+0xc54`) and is drawn on its own after the batches (below);
- **otherwise**: when its batch (`+0xb0` high half) is resident and the pane **has a body** (vtable `+0xf4`, which
  returns `+0xe8`), one sprite is added to the batch (`0x00183038`): the rectangle's `u0, v0, u1, v1` from the
  batch's sheet (`0x00181e38`, rectangle = `+0xb0` low half), the colour `+0xb4` as RenderWare `{R, G, B, A}`
  (`0x808080e0` → 128, 128, 128, 224, [GUI: sprite colours](gui.md#sprite-colours): RenderWare's 0-255, so half
  brightness, 88 % opaque), and the matrix built from `+0x70`-`+0xaf` (`0x003368d8`).

**The quad.** The matrix's rows are right = **W**, up = **H**, at = the normal, position = the centre, each turned
into RenderWare's axes as `(x, z, −y)`. The near path (`Instance_DrawOneSpriteIm3D`, `0x001831c0`) makes the four
corners as matrix × (±0.5, ±0.5, 0, 1) (the constants at `0x00552850`), so in game space the quad is

```text
centre − W/2 − H/2 = corner     → (u1, v1)
centre + W/2 − H/2 = cornerV    → (u0, v1)
centre − W/2 + H/2 = cornerU    → (u1, v0)
centre + W/2 + H/2              → (u0, v0)
```

drawn as one 4-vertex triangle strip (`0x004a4f28` with 4 vertices, `0x004a49e8(4)`), every vertex in the pane's
colour. The script's own texture coordinates (`+0xb8`-`+0xc4`) take no part. The batched path hands the same matrix,
rectangle and colour to the PTank, which builds its quad itself; that it gives the same corners is inferred.

**Render states.** Near path, set in `0x001831c0`: the batch's texture raster, vertex alpha on, **Z test on, Z write
off**, source blend source alpha, destination blend inverse source alpha, fog off (RenderWare states 1, 12, 6, 8, 10,
11, 14). Batches (`ResourceMgr_Render3DSprites`, `0x00185b38`, per viewport after the world and before the ground
rings): **Z write off, culling off** (states 8 and 20), Z test left on; each batch blends source alpha / inverse
source alpha with vertex alpha (`0x001972b0` writes 5, 6 and 1 into the PTank's data). Back faces are drawn either
way: no cull state is set on the near path, and the batches turn culling off.

**Order.** The batches queued this frame are sorted by their key, **farthest first** (comparator `0x00184850`: the
camera distance² − radius²; `0x00197000`), and drawn; then `0x0018b058` draws the near list in pane order, unsorted
(after a list of other near sprites, `0x0039b6b8`). Panes inside one batch keep the pane manager's order. So glass
is drawn after the opaque world, back to front between batches, not sorted among themselves.

**Which sheet.** Every pane but type 14 draws from **batch 0**, one of the thirteen batches the resource manager
makes at start (`0x00184918`): sheet `part_page1` (sheet-table record 1, [Particles](particles.md#sprite-words)),
matrix format (format 2), 640 sprites, the 3D sprite pass. The init clears `+0xb0` and sets only the low half from
the type's table word (`Task_SetRect`), so the high half of `CfgSetGlassProperties`' words is ignored. The disc's
words for types 0-18 are rectangles 20 (whole) / 21 (broken) of `part_page1`, or 22 / 23 for types 10, 11 and 16;
types 17 and 18 use 21 for both ([Glass types](../references/glass-types.md)). Type 14 makes a one-sprite matrix
batch of its own (`PTank_New(max(width, height), 0x20000, centre, …)`, `0x003e2c84`) over sheet-table record 2,
rectangle 0, whose frame is placed at the pane's centre; no level places a type-14 pane.

**A broken pane is not drawn.** The hit sets the broken rectangle and colour `0xffffff70` (white, alpha 112) but
also removes the body, and both draw paths need the body, while `0x0038f1c8` never gives a broken pane (colour no
longer `0x808080e0`) a body again. What the player sees is the shatter's shards and then an empty frame. Confirmed
(code) as a chain of the reads above; not checked at runtime. The broken words matter only for types 17 and 18,
which start with the broken rectangle as their whole sprite, keep the colour `0x808080e0` and so get a body (and
are drawn) within 50 m although their triangles are off. Type 15 (colour 0, made with a body at spawn) is drawn,
invisibly.

### The shatter {#shatter}

`sub_glass` (`0x003e4cb8`) runs once and ends. Confirmed (code):

1. **Count** = 10 × width × height (whole metres from the size word); a pane under 2, or exactly 1 × 1, uses 10
   shards of size 0.06 and the sound of materials `GLASS_SMALL` × `GLASS_SMALL` (88); others the sound of `GLASS` ×
   `GLASS` (2). The sound is the material pair's entry in the sound matrix ([Sound](sound.md#play)).
2. **Culling**: no shards unless **some active camera is within 15 m** of the pane's centre
   (`Cameras_IsWithinRange(15, p)`, `0x003a5280`: the smallest squared distance to any view's camera,
   `Cameras_MinDistanceSq`), **and the centre is in some player view with a 10 m margin**
   (`Cameras_IsPointVisibleAny(10, p)`, `0x003a51f8` → `Camera_IsPointInPlayerView` `0x003a50e0`: the point fails only
   when it lies more than 10 m outside one of the view's six frustum planes), and the particle budget (`0x003a5a50`)
   allows; game state bit `0x20` (`0x0041cf30`) forces them. Both tests are about **cameras, never player 1's
   position**: in a scene the scene camera is the view's camera, so panes near the scene camera shatter wherever
   the player stands (inferred: the scene camera is what the view's camera answers while a scene plays). Confirmed
   (code) at `0x003e4cb8` for the arguments 15 and 10.
3. **Shards**: for each of the count, two tries, each taken at 2 in 3 (`Random_Int(2) < 2`, `Random_Int(n)` giving
   0 to n): a `glasstest` particle at a random point within ±4/7 of the width (whole metres) and ±4/7 of the height
   on the pane's plane (`0.5714286`), turned by the shatter's rotation. Each gets the shard size (0.2 from a hit,
   0.06 for a small pane), a count of `miniglass` pieces (2-4; 0-2 when the count is over 40; 0-1 over 79) and the
   shatter's sprite word, which is 0 (batch 0, the particle default, `0x0039aef0`). Over 79 the code writes `0x4f`
   into the data's size word, but the loop count is already taken, so every shard is still made.

**A shard** (`glasstest`, script type 62: `GlassTest_Init` `0x003e44e0`, `GlassTest_Update` `0x003e4838`), confirmed
(code) unless marked:

- **Sprite**: rectangle 24 + `Random_Int(4)`, so 24-28, of the batch in the passed word's high half: batch 0,
  `part_page1`.
- **Size**: the shard size × a random 0.1-1.5 (`+0xc0`). **Colour** `+0xb0`: white with alpha 128-224
  (`Random_Int(96) − 128` as `0xRRGGBBAA`).
- **Motion**: flags `0x14000001` (airborne; bit `0x10000000` cleared under game state bit 1); gravity −9.8 in its data
  block; a random rotation; an initial velocity with a random 1-2 m/s upward part (the rest, from the shatter's
  rotation, not traced).
- **Each update** (every 2 ticks): ended at once when it fails the 10 m and 15 m tests or the particle budget (as the
  shatter's culling, also when its data word `+0x24`, not traced, is over 119), unless game state bit `0x20`;
  otherwise a random ±1 m/s per axis is added to its velocity and its second colour `+0xb4` becomes a random grey
  64-255 in all four bytes (a glitter; which colour the particle draw uses is not traced).
- **Landing**: when its flags gain `0x20000000` (inferred: set by the particle's ground contact) it makes its count of
  `miniglass` particles around its position (their random speeds and sizes are not summarised here) and ends. A
  shard therefore lives from the hit until it lands, with no timer of its own.

How the particle draw blends a shard is on [Particles](particles.md); `miniglass` is not traced.

### Swinging doors: states and commands {#door-states}

The state byte (data `+0x28`), confirmed (code) at `0x003fbba0`, `0x003f9770`:

| State | Meaning | Next |
| --- | --- | --- |
| 0, 1 | closing; the update adds 1 | at 2 the leaves are reset to the closed pose |
| 2 | **closed** | |
| 3 | opening (the leaves swinging) | 4 on the next update |
| 4 | | collision off (state command 5), then 5 |
| 5 | **open**; `IsDoorOpen` (message `0x0c`) answers true only here | |
| 6 | **broken**: the update returns 1, so the task ends | |
| 10 | the cabin doors at rest | |

The door updates every 28 ticks (0.47 s); opening reschedules it for the next tick, so collision goes about 29 ticks
after the swing starts (inferred from the scheduling, [Tasks](tasks.md#wheel)).

**State commands** (message `0x22`, `ObjectChangeState`; the bindings below send them):

| Command | Effect |
| --- | --- |
| 2, 8 (`OpenDoor`) | when not pickable: reset the leaves, angle 170°, state 3, the open sound |
| 3, 7 (`CloseDoor`) | angle 0; from state 2 only a reset to state 0, otherwise swing back with the close sound and state 0; then as 6 |
| 6 | triangles enabled, the object's collision body back (`0x003a52c8`), the number's links get the avoid bit and its hole loses path flag 8 (`0x00250d00`, [A door's hole](#nav-links)) |
| 5 (`DisableDoorCollision`) | the number's links lose the avoid bit and its hole gets path flag 8 (`0x00250c50`), triangles disabled, the collision body removed (`0x003a5340`) |
| 10 / 0, 11 (`SetDoorPickable`) | pickable on / off ([Lock picking](#lock-pick)) |

**Messages**, confirmed (code) at `0x003fb8d0`:

- `0x42` (`DoorOpenDegree`): when not pickable, angle = the argument, swing, state 2 → 3, the open sound; when
  pickable, the angle is only kept at data `+0x04`.
- `0x0b` (`DoorOpen(door, human)`), only when not pickable and closed (state 2): the leaves reset, the angle is
  **±170°, away from the human** (the sign of the dot product of the door's turned axis with the human-to-door
  direction), or the kept data `+0x04` angle after a state 5; state 3, the open sound; pickable cleared.
- `0` with a human: `Door_OpenAnimated(door, human)`, the human's object target becomes the door
  (`0x00227080`) and its state code 26 (`0x1a`, `0x002266a8`). What state 26 plays (anim 666
  `ANIM_SPECIAL_OPEN_DOOR`, `gen_dooropen`, is the likely clip) is not traced.
- `0x15` (destroy, what `BreakObjectsInRadius` sends): flag `0x40` and update interval 2; the update then raises the
  interval by one each time and, on reaching 3, sends the door a hit (message 1) from itself.
- `0x19`: hitpoints = the argument. `0x20`: calls each leaf's vtable `+0x4c` (not `dyn_door_stall`). `0x3e`: answers
  the angle.

**Swinging**: `DoorSwing_SwingTo` (`0x003f8fd0`) sends each leaf message `0x35` with its target rotation: the door's
rotation turned by the angle about the vertical axis (half-angle quaternion), the right leaf by the negated angle
from its turned base. How a leaf turns there is under [Leaves](#leaves).

### Leaves: model, hinge and swing {#leaves}

**The frame draws nothing.** The `dyn_door_swinging` object never calls `Obj_SetModel` (its init leaves the model
word `+0xc4` at 0 and `DoorSwing_SetUpType` only spawns leaves) and its flags `0x405` include `4`, hidden; what is
seen of a door is its leaves, and of a type with no leaves (`_dblwood`, `_woodbrd`, `_woodp`, `_steel`...) nothing
but the level's own geometry around the doorway. Confirmed (code) at `0x003fb5f8`.

**A leaf** (`sub_swinging_door`, `0x003fb330`) is made by `Obj_SpawnChildByName(name, position, rotation, door)`,
which pushes the pose and the door's handle; it is not attached to the door (no flag `0x10`). Its init pops the
door's handle (data `+0x10`), the rotation (into `+0x20` and data `+0x00`) and the position (`+0x10`), sets flags
`0x1021`, an update every 28 ticks, and **its own type's model** (`Obj_SetModel(leaf, 0)`, the `dyn_dr_*` record's
model, word at `+0xc4`; the model hash `0xb14109ba` also sets flag `0x80`). Confirmed (code).

**The hinge is the model's origin.** A leaf only ever changes its rotation, about the vertical axis through its
position; the position stays where `DoorSwing_SetUpType` (`0x003f80f0`) and `DoorSwing_ResetLeaves`
(`0x003f9368`) put it:

| Leaf | Position (the hinge) | Rotation when closed |
| --- | --- | --- |
| first (left, `+0x124` = 2) | the door's position | the door's rotation |
| second (right, `+0x124` = 4) | door position + door rotation × (−2w, 0, 0) | the door's rotation × 180° about z |

where `w` is the door type's `CfgObj` argument 15 (`f15`, type `+0x68`, read by `0x003a3898` property 5). Confirmed
(code). On the disc `w` is one leaf's width: `dyn_door_store` 1.09 with a collision box 2.18 wide (two leaves),
`dyn_door_chainlnk_a` 2.29 for its one 2.31 m leaf, `dyn_door_big_gate` 4.43 with an 8.86 m box and 4.43 m leaves.
So the two hinges are a whole doorway apart, the second leaf faces back toward the first, and each leaf's model
extends from its origin along its local −x to about −w, meeting the other at the middle: inferred from the spacing,
from the lock-pick glint at door position + door rotation × (−w, 0, 0) + 1.3 m up (the seam of a two-leaf door, the
latch edge of a one-leaf one, `0x003f9588`), and from
the `dyn_dr_store` collision centre at x = −1.09; the model bounds themselves were not checked. A one-leaf door
(`_chainlnk_*`, `_red_fence`, `_sheetmtl`, `_corr`, `_ornate_single`, `_stall`, `_woodfnce_xl`) has only the first
leaf, hinged at the door's position.

**The swing: a linear slerp over one leaf update (28 ticks, 0.467 s).** Confirmed (code):

1. Message `0x35` (`0x003fb430`) stores the target in data `+0x00`-`+0x0c`, sets data `+0x14` = 1 and reschedules the
   leaf for the next tick.
2. On that update the integrate step runs first (`Task_Integrate`, `0x003a2310`): with flag `0x1000` it copies the
   **previous target** `+0x40` into the rotation `+0x20` and stamps the update time `+0x50`. Then the leaf's update
   (`0x003fb5a0`) copies the new target into `+0x40` and clears data `+0x14`.
3. Between updates the object's pose "now" (`Obj_GetPoseNow`, `0x003a1ad8`, flag `0x1000`) is
   `Quat_Slerp(t, +0x20, +0x40)` with t = (time since `+0x50`) / the update interval in seconds (`+0x64`, 28/60),
   clamped to 0-1. `Quat_Slerp` (`0x00336a00`) takes the shorter arc (it negates the second quaternion's weight when
   their dot product is negative) and falls back to a plain lerp when they are within 1e-6.
4. The next update, 28 ticks later, makes the target the rotation, and the leaf rests.

So a leaf turns at a **constant angular rate** from where it was to the target in 0.467 s: 170° at about 364° per
second, no ease-in or ease-out, whatever the angle. A new target that arrives mid-swing starts from the previous
target, not from the in-between pose (step 2), so the leaf jumps to the old target first. That the drawn model takes
this pose "now" is inferred: it is the only place the slerp happens. The door's own state steps (opening, state 3 →
4 → 5, [states](#door-states)) run on the door's 28-tick schedule independently of the leaves.

### How a human opens a door {#opening}

- **Scripts**: `OpenDoor` / `CloseDoor` / `DoorOpenDegree` (no human), `DoorOpen(door, human)` (away from the human),
  `OpenDoorAnimated(door, human)` (the human's state 26 with the door as target).
- **Use**: a player whose current interaction record (human `+0x660`) names the door and has a kind other than 0, 2
  or 3 sends the door message 0 on triangle (`0x0024d530`, the context action of [Characters: jumping](characters.md#jump)),
  so the same animated open. Confirmed (code) for the dispatch; what puts a door in the record is not traced.
- **The AI**: a door's number marks its links ([Doors and their numbers](#door-numbers)); a closed door's links carry
  the avoid bit, an open one's do not. AI humans do not open swinging doors by themselves as far as the route
  follower shows (its `0x10` and `0x40` handlers jump or charge). Inferred.

### Breaking a door {#door-break}

A **hit** is message 1, the same as a pane's: a direction, a point, an int, the hit's **kind** and two handles (the
attacker). The human path (`0x0021b290`) sends kind 0 for a plain hit, 2 while the attacker's record `+0x08` has any of
`0x1400000` (`0x1000000`: the run attack, the charge and, at runtime, the walk attack 23; `0x400000`: the dive) and 3
while airborne (object flag `0x4000000`); a thrown object (`0x00393538`) sends 1 (3 for `TYPE_MISSIONTV`, 0 for an
object of animation set 5 other than a molotov). Every door and barrier takes **4 + 6 × kind** hitpoints: 4, 10, 16
or 22. Confirmed (code) at `0x003f9ad0`, `0x003fa438`, `0x003b2180`; the kinds of the moving attacks confirmed
(runtime) on a fence ([Barriers](#barriers)).

- **`dyn_door_store`** (`TYPE_BREAKANDENTER_DOOR`, 1 hitpoint): the first hit plays sound `0xcf6586b2` and the
  `GLASS` × `GLASS` pair, triangles off, and the door sends itself `0x0b` (the human pushed is inferred to be the
  attacker): it **swings open away from them**. Hitting any `TYPE_BREAKANDENTER_DOOR` also reports a break-in
  (crime type 1) at the attacker (`0x0021b290`). Confirmed (code).
- **Cabin doors** (12): each hit plays `0xcf6586b2`; below 7 both leaves are flagged broken and one, at random, takes
  its next model (message `0x19`) with a small burst (`0x003c6038`); at 0 or below: triangles off, two loose pieces
  (`dyn_cabin_aa`, `_bb` or `_cc`), state 6. Two plain hits break the first leaf, three the door. Confirmed (code).
- **`dyn_door_liz`** (100), **`dyn_door_dclub`** (20), **`dyn_door_stall`** (10): every hit makes dust (`0x003c57d8`,
  radii 2.75 and 3.75) and 20 splinters (`0x003c5b00`), and plays the type's material against `CONCRETE`. `liz` swaps
  its model at 80, 60, 40 and 20 % (with material sounds 35, 36, 106 and its own); `dclub` at 50 %. At 0 or below
  (`stall` on its first hit): triangles off, the number's links opened, alpha cleared, more dust and splinters, two
  wreck pieces (`dyn_dre_liz_e`/`_f`, `dyn_dre_stall_a`/`_b`, `dyn_dre_dclub_c`/`_d`), state 6. Confirmed (code).
- Other swinging doors ignore hits.

### Breakable barriers {#barriers}

`dyn_door_fence` and the classes beside it are single objects, not leaves: `SpawnDoor` with the same arguments; the
triangles get two-sided, `0x40` and `0x400`, the number's links are retagged `0x40` at spawn, and the update interval is
60 ticks. Hitpoints from the type, written to data `+0x04` and object `+0x128`. A hit (`0x003b2180`) takes 4 + 6 ×
kind and makes dust and 20 splinters; one that leaves hitpoints does nothing more. At 0 or below: both triangle sets
off, the number's links opened (`0x00250c50`), then by class name:

- `dyn_door_wall_a`: a sound, flag `0x800000`, the damaged model `0x7f97c350` and its collision body removed
  (`0x003a5340`);
- `dyn_door_wall_b`: sounds only;
- the others (`dyn_door_fence`, `dyn_door_vargas`): more dust (unless bit 2 of game state word 0, `0x0041cf30`), a
  sound, and three `dyn_wooddmg_a`/`_b` boards (unless bit 2 of word 3), each sent messages `0x19` and `0x30`.

Then the type's material pair sounds (unless bit 4 of word 3), and the barrier either marks itself broken (data
`+0x00` = 1) and hides (flag 4), or, for `dyn_door_vargas`, which makes a second object at spawn (data `+0x08`),
destroys that object (message `0x15`), takes model `0x3b4fefad` and loses its body. Message 10 sets whether it can be
hit (`0x003a17e8`), `0x19` its hitpoints. Confirmed (code) at `0x003b2180`, `0x003b2f40`, `0x003b3158` for
`dyn_door_fence`; the other five classes are alike by their shared calls, not read in full.

**Message 2 is the removal.** The class's update (`0x003b3220`, every 60 ticks) only answers whether data `+0x00` is
set. When it is, `WorldObject_Update` (`0x00395b70`) calls the object's vtable `+0x4c`, `WorldObject_Remove`
(`0x00391c10`), which sends the object's own handlers message **2** with `+0x00` = 0 (a script's callback gets
`(self, NilHandle)`, [Scripts](scripting.md#message-handlers)), takes it out of its spawn record for good
(`0x00398cb0`, record bit `0x40000`) and deletes it (`0x00391b08`, which frees its collision body). So a broken
fence's script hears of it on the fence's next update, up to 1 s after the hit, and until then the hidden fence keeps
its body. Nothing in the barrier's hit sends a message itself. Confirmed (code). Every world object removed this way
sends message 2, which is why the [script event](../references/script-events.md) list reads it as "finished or
broke".

At runtime (confirmed (runtime), PCSX2 2.9.94, slot 10, `level99` checkpoint 3.5; scenario
[`charge_fence`](repo:research/traces/scenarios/charge_fence.toml)): the fence (type index 520, centre (23.6, −6.67))
had 10 hitpoints and broke on one charge, dive or walk-attack hit (kind 2). Its three boards were removed on their
own first update, the update after the hit (type indices 1368 and 1369, along the fence at x 22.7, 23.7 and 24.6); why
is not traced. The fence was removed 4, 11 and 18 updates after the hit in three runs (its 60-tick phase); in the same
update the objective marker went, and 15 updates later `P3.FenceBroken`'s placement put the player at (18.5, −16.3),
beyond the fence. Breaking in by charge: [Combat](combat.md#moving-strikes).

### Lock picking {#lock-pick}

The door side, confirmed (code) at `0x003f9588`, `0x00397078`:

- **Pickable on** (state command 10, `SetDoorPickable(door, true)`, and at spawn for `dyn_door_chainlnk_pick`,
  `dyn_door_storeb` and `dyn_door_templedoor`), only while its angle (data `+0x00`) is under 2: a `sub_triglint`
  glint at door position + door rotation × (−w, 0, 0) + 1.3 m up ([Leaves](#leaves)), drawn while within 30 m
  (`0x003eb0d0`); door object `+0x124` = 10; its collision body removed (`0x003a5340`).
- **Pickable off** (commands 0, 11; `SetDoorPickable(door, false)`): the glint destroyed (message `0x15`), `+0x124` = 0,
  the collision body back (`0x003a52c8`). `SetDoorPickable(false)` first stops every human whose object target is
  the door (`0x0022e400`, state flag `0x20000000`).
- While pickable the door refuses `OpenDoor`, `DoorOpen` and `DoorOpenDegree` (which keeps its angle for later).

The minigame (`CfgSetLockPickHandler`'s callbacks, `pick_lock.anm`) is not traced here.

### Doors and their numbers {#door-numbers}

`SpawnDoor`'s last value is the door's number in its level (1-27; 484 doors in 45 levels, unique per level). It ties
the door to the navigation links carrying it ([Navigation links](#nav-links)):

- the breakable doors retag them kind `0x40` at spawn (`0x00250c00`): the classes `dyn_door_fence`,
  `dyn_door_bar_bani`, `dyn_door_bnstr`, `dyn_door_fence_o` and `dyn_door_parapet` (initialisers `0x003b2f40`,
  `0x003b3250`, `0x003b4128`, `0x003b57d0`, `0x003b6220`), and the swinging types `dyn_door_dclub` and `dyn_door_liz`;
- opening (state command 5) or breaking clears their avoid bit and sets path flag 8 on the door's hole; closing sets
  it and clears the flag ([A door's hole](#nav-links)).

`DisableDoorLink` / `EnableDoorLink` do the same by position for the nearest `0x10` link within 5 m and its
reverse. Confirmed (code).

### Breakable props {#breakable-props}

The street props `level34`'s riot meter counts ([Scripts](scripting.md#level34)) are world objects (run-time type
`0x08`; a glass pane is `0x400`, [Tasks](tasks.md)) of two classes. Values from the `CfgObj` lines of
`config_preload3`; behaviour confirmed (code) at the cited addresses unless marked.

| Type | Class | Hit points `+0x58` | Hits `+0x5a` | Model hash |
| --- | --- | --- | --- | --- |
| `dyn_newsstand_a` | `dyn_masks` | 0 | 1 | `0x18f31e91` |
| `dyn_newsstand_b` | `dyn_masks` | 0 | 1 | `0x81fa4f2b` |
| `dyn_crate_stack` | `dyn_masks` | 0 | 1 | `0x1a0686f7` |
| `dyn_parkbench_a` | `dyn_masks` | 10 | 2 | `0x7852cedb` |
| `dyn_trashcan` | `overhead_weapon` | 150 | 1 | `0xfbf3e3ae` |
| `dyn_gbags` | `overhead_weapon` | 80 | 1 | `0x62502b03` |

`ObjType_GetIntField` (`0x003a37b8`) reads these fields (0: `+0x58`, 1: `+0x5a`, 3: the material `+0x64`).

- **Two counters on every world object**: `+0x10d` and `+0x10e`, from the type's `+0x5a` and `+0x5b` (a 0 becomes
  `0xff`, unbreakable; 0 means broken).
  `WorldObject_TakeHit` (`0x00393450`, kind *k*) takes one from `+0x10e` first while the object is airborne
  (`0x4000000`) or a held `0x3c4e590b`; otherwise `+0x10d` loses 1 when *k* is −1 or `+0x128` is 1, else 4 + 6*k*
  (floored at 0).
- **A strike** (`Strike_Contact`, `0x0021b290`) on a world object: kind 3 when the attacker has state `0x4000000`
  ([Combat](combat.md#state-flags)), 2 with something in hand (held flags `0x1400000`), else 0. In order: notes whether
  either counter was already 0, `WorldObject_TakeHit`, the impact sound, burns the attacker within 1.5 m of a
  `0x1c21bdc3`, message 6 to the boxes **only if the object was intact before the blow**
  ([Scripts](scripting.md#triggers)), `Glass_Break` if it is a pane, a crime report for class code `0x1e`, then
  task message **1** to the object with (attacker, attacker, *k*, 0, contact point `+0x90`, direction `+0x10`).
- **`dyn_masks`** keeps its own data: `+0x00` broken, `+0x04` path flag cleared, `+0x08` update interval, `+0x0c` hit
  points (`+0x58`, −1 when 0), `+0x10` hits (`+0x5a`, −1 when 0; `+0x128` = 1 when set and `+0x0c` is not),
  `+0x14` a child. `DynMasks_Init` (`0x003b7538`) sets flags `0x200001`, interval 20, path-polygon flag 8 under it,
  and copies `+0x0c` into `+0x10d`. Its message 1, `DynMasks_OnHit` (`0x003b7a88`): both counters −10 → ignored;
  `+0x0c` not −1 → `+0x0c −= 4 + 6k`, mirrored to `+0x10d`; else `+0x10` loses 1; below 1 → broken (also kind 3
  with object-flag bit 4, or model `0xf21e6a91` with argument −1). Every hit sets object flag `0x800000`, plays the
  material sound (survive: the material with 5; break: the material pair) and, when a visibility test passes (40 m
  and 10 m arguments; inferred to be distance and screen), dust; then a per-model effect. No velocity is given to
  the prop itself.
- **What each breaks into**: the newsstands a sound and five paper debris particles, no piece; the crate stack a sound
  and one piece object at −0.604 m, turned at random; the bench 25 splinters, a dust cloud and one piece at −0.395 m.
  The trash can and the bags have no case. A broken prop then looks for a damaged model (`Model_FindByHash`): if one
  exists it takes it, re-reads both counters from the new type and keeps its body; if not, it loses its collision body
  (`Obj_RemoveBody`), the low byte of `+0xcc` is cleared and path flag 8 under it is cleared once.
- **Removal**: `DynMasks_Update` (`0x003bf548`) answers "done" while `+0x00` is set, so `WorldObject_Update` removes it
  on its next update (up to 20 ticks): message 2, spawn record bit `0x40000`, never spawned again
  ([Barriers](#barriers) has the path). Messages 4 and `0x15` set flag `0x40` instead; the update then counts data
  `+0x08` up, using it as its interval, and when it reaches 4 sends the prop a kind-0 hit of its own (message 1, from
  itself; where the count restarts is not traced). Message `0x30` knocks a piece: velocity a random
  4-6 × its horizontal part (vtable `+0x74`), a spin (`+0x7c`), flags `|= 0x4200000` (airborne;
  [Physics](physics.md#movers) flies it).
- **Counting** (inferred from the code above): a newsstand or crate stack breaks on the first blow of any kind, so it
  sends message 6 once. The bench takes three bare-handed blows (10 → 6 → 2 → broken) or one armed blow, and each blow
  while intact sends message 6. A strike on a trash can or bags takes `+0x10d` from 1 to 0, so only the first counts.
- **`overhead_weapon`** objects are the throwables: `OverheadWeapon_ContactDamage` (`0x003932c8`) takes one point
  (`WorldObject_TakeHit(-1)`) per damaging contact while flying or held. What the class does with message 1 and how
  it breaks is not traced.
- **`F.Vandalize`** (`level34.lua`) tells the newsstand apart by name, not by type bits: `GetRTTI` 8 and
  `GetObjectName` `dyn_newsstand_a` or `_b` give 3 points, other 8 give 1, 1024 (a pane) gives 2.

### Trains (moving hazards) {#trains}

A **train** is a line of up to 12 objects (a subway train's cars) that AI humans step out of the way of; the scripts
build one with [`ObjSetTrainPoint`](../references/bindings/world.md#objsettrainpoint) and run it with
`ObjStartTrain` / `ObjStopTrain`. There are four records of 0x150 bytes at `0x006f3a10`, built at boot
(`0x00414220`, from the static initialiser stub `0x00414280`) and stopped by `InitLevel` and `UnloadLevel`
(`0x004136f0`); the task manager updates all four each frame (`0x00413728`). Confirmed (code).

| Offset | Field | Evidence |
| --- | --- | --- |
| `+0x00` | 12 point positions (0x10 each), re-read from the objects every update | confirmed (code) |
| `+0xc0` | a box around the points, grown by the radius | confirmed (code) |
| `+0xe0` | 12 object pointers (`Train_SetPoint`, `0x00413770` → `TrainRecord_SetPoint`, `0x004138a8`) | confirmed (code) |
| `+0x110` | 12 gap flags: non-zero means no segment starts at that point | confirmed (code) |
| `+0x140` | point count (the highest index set, plus one) | confirmed (code) |
| `+0x144` | the radius in metres (`ObjStartTrain`'s first argument) | confirmed (code) |
| `+0x148` | running (`Train_Start`, `0x004137d8`, after `TrainRecord_Reset`, `0x00413890`; `Train_Stop`, `0x00413828`) | confirmed (code) |
| `+0x149` | the last point index set with gap 0 | confirmed (code) |

`TrainRecord_Update` (`0x00413d90`), while running: up to 60 humans in the box (`0x002274a8`); each one with an AI
brain that is not in state 1 and not cuffed, and within the radius of a segment, gets the time stamped in its brain
(`+0x2e0`); if its brain has no goal of type 7 it drops a type-`0xf` goal on top and pushes the dodge goal
(`0x002f9358`, given the record and the nearest point). The dodge goal uses `0x004138f0` (still within the radius of
a segment, and whether it is more than 0.5 m inside and within 10 m of the front point) and `0x00413a90` (the
point's projection on the first segment long enough to hold it, and that segment's two indices). `Trains_IsPathClear`
(`0x00414188`) asks each running train (`TrainRecord_IsPathClear`, `0x00413c48`) whether a move from one point to
another passes within its radius of a segment; [AI](ai.md) movement checks it. The record's constructor is
`0x00413848`. Confirmed (code).

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0041d4f8` | `Cfg_SetGlobalTimeToLive` | `CfgSetGlobalTimeToLive`: `+0x26c` | confirmed (code) |

## Coney's implementation

**Object types and spawn records** (`src/world_objects/object_types.h`, `src/world_objects/spawn_records.h`,
the bindings in `src/scripting/spawn_bindings.h`): `CfgObj` adds a type (name, class, hitpoints and the model hash, the
CRC-32 of the name) and still records its arguments for what reads the rest; `ObjSpawn` adds a spawn record (type,
position, rotation, zone, flags, tint, flag name) and returns its handle; `CfgSetDatabaseSizes` makes the pool (its
object count plus 500). Resolving a handle marks its record live and pinning keeps it. A fresh script state starts
with no records but keeps the types, which the legal screen's preloads configure once
([Scripting](scripting.md#life-of-the-lua-state)).
**Coney's choices:** a record's handle comes from the counter every world object's handle comes from, not the record
index << 16 (record 0 would be `NilHandle`); the rotation is kept as given rather than packed into s16 × 4,096; a
record a binding resolved is "live"; and the keys and the power cuffs are always
suppressed, as a fresh profile has neither unlockable. `ObjEnableZone` sets or clears a zone's bit of the records'
zone mask (zone 0 on at a level's start, the rest off); `ObjShow` and `ObjHide` resolve the handle and mark the record
shown or hidden and keep the show message (`ObjShow`'s distance kept); `ObjDestroy` makes a human holding the object
let go, then removes the record for good, except that with its second argument an objective marker is marked dying and
fades out first (`src/scripting/world_bindings.h`). Disc check (NTSC-U,
2026-10-06, counts only): at the front end
`level100.lua` leaves 29 Wonder Wheel records, each of a configured type, the wheel tinted `0x474542FF`.

**Placed objects** (`src/world_objects/placed_objects_file.h`, from [the objects file](#objs-file)): after the level
script's main chunk, `runLevelScript` reads `<level>_objs.txt` through the script source and adds a spawn record per
line, each with a handle from the world objects' counter; the `part` lines are skipped (no particle emitters from it
yet). A missing file adds nothing. Disc check (NTSC-U, 2026-10-06, counts only): `level99` places 98 records of its
112 lines.

**Pickable objects and objects in a scene** (`src/world_objects/pickups.h`, `src/gamemodes/level_pickups.h`): triangle
takes the [pickable](#pickable) classes (`world_objects::pickable()`, the model-hash exceptions included); `CfgObj`'s
20th argument is kept as the type's anim set (`+0x87`; inferred from the field order and a bat's 3). A scene's moves
of a bound object (position and rotation) go to its spawn record (`SceneStage::setObjectMover`): the record's pose
stands for the object's, so the bats `l99_c8` lays down are where the pick-up finds them. **Coney's reading**; the original
writes the record only when the object is stored.

**Models** (`src/platform/object_models.h`, `src/platform/placed_objects.h`): an object's model is its type's Object
List record (`ObjectList::findByHash`, the type's model hash), its `0x47` model read as the level file's and its
dictionary's first texture on the first material, loaded once per type and shared. An object is drawn at its pose
with the model's vertices taken in the game's axes (z up) and carried into RenderWare's as positions are, `(x, y, z) →
(x, z, −y)`; the model's own atomic frame is left out. **Coney's reading**: the frames are either the identity or the
turn of z up into RenderWare's y up, some with a stray offset (the column's 2.69 m, the Wonder Wheel neons' 35 m), and
only without the frame does the objective disc (an identity frame, its centre 1.4 m up) stand upright on its column
as the runtime sees it. The draw (`PlacedObjects::draw`) follows [The tint](#tint): the tint word multiplies the
materials' colours and alpha, then the size cull (the model's bounding sphere), the `ObjShow` distance and the alpha
floor of 10; opaque objects first, then the translucent ones. Not modelled: the camera fade and the second dictionary.
The front end draws the live records' objects this way, without a camera (no cull), each lit as a world object
([Front end](frontend.md#coneys-implementation)); the eight Wonder Wheel types' frames are all the z-up turn, so
leaving the frame out draws them as turning by it would.

**World objects in play** (`repo:src/world_objects/object_tasks.h`, `repo:src/platform/play_level_world.cpp`): each
step, after the scripts and the scenes, `ObjectTasks` brings records in as [streaming](#streaming) does (zone enabled,
within 70 m of the camera), keeps them until they are beyond the draw distance + 10 m, and makes each object's draw;
the play mode draws them between the `s` and `d` worlds with the objects' lights. **Objective markers** follow
[the page](#objective-markers): the disc turns 90° a second about z; the alpha steps 8 an update towards 255 or 0 by
the last show message and is drawn one update late; the disc is never size-culled; the column (model `0x1f3b85ea`)
stands at the disc's position turning back, coloured by the disc's type; a dying marker is removed at alpha 0, and
`ObjDestroy` without its second argument removes it at once. **Held objects** ([In a human's hand](#held)): player 1's
object hangs from the bone of the first take event of its type's low pick-up clip, at the event's position × his scale
slid along the object's y by `CfgObj` `+0x70` (`ObjectType::grip`), composed with this frame's bone and his
placement. Disc check (NTSC-U, counts only): `coney_tests "[disc][objects]"` at `level99` checkpoint 1 after the
intro: one marker disc and one gold column at alpha 255; the three bats, laid on the ground as `l99_c8` leaves them,
drawn there.

Coney's stand-ins for the world objects, where this page is silent:

- The column's blend: alpha blended over what is drawn, Z tested without Z writes, after the opaque objects.
- A new marker starts at alpha 0. Every record within reach comes in on the same step; a record a binding resolved
  or a scene pinned is drawn whatever its distance and zone. `ObjHide` hides a plain object (`simple_object`'s
  `0x0a` is not a hide). The fade-in over the first second is linear.
- A held object: the bone's position is not scaled by the human's scale (Coney draws the body unscaled), the lean is
  left out, only player 1's object is drawn in a hand, and a dropped object lands where the pick-up's drop puts it
  (no fall from the hand).

**Glass and doors**, written from this page and [Crimes](crimes.md#lockpick) (2026-10-06), in `repo:src/world_objects/`:

- **`LevelObjects`** (`level_objects.h`) holds a level's panes and doors and the world they change (`ObjectWorld`: the
  collision mesh's triangles, the path data's links, and `ObjectServices` for sounds, shards, crimes, flags,
  statistics, bodies, loose objects and models). Play mode ticks it at 60 Hz and sends hits to `humanHit` /
  `thrownHit`, finding the object by the struck triangle (`objectOfTriangle`); `humanHitKind` / `thrownHitKind` give
  the hit's kind.
- **Panes** (`glass.h`): the type table, the spawn (geometry, `GLASS` two-sided triangles, sprites, types 14, 15, 17,
  18, alarm bits, window link), message 1 and 0, `Glass_Break` and `BreakGlassInRadius`; the shatter's count, sound
  pair and shards drawn from the game's random numbers.
- **Doors** (`doors.h`): the per-type set-up, leaves, triangle flags, the state machine and its timing in ticks, the
  state commands, `DoorOpen` away from the human, `DoorOpenDegree`, pickable doors, message `0x15`, the breakable
  types' hits and the barriers. **Links** (`nav_links.h`): retag, open and close by number (the door's hole found as
  [A door's hole](#nav-links) gives, `NavLinks::holeOf()`, `world::PathMap::holeAt()`), nearest by position. The
  edge's door number is decoded into `world::PathEdge::door`.
- **Bindings** (`repo:src/scripting/object_bindings.h`): the 22 glass, door, link and lock-pick bindings; `SpawnDoor`
  reads its type from the recorded `CfgObj` calls.
- **In play** (`repo:src/gamemodes/gameplay_mode.h`, `repo:src/platform/play_level_objects.cpp`): gameplay owns the
  level's `LevelObjects`; the boot scripts' recorded `CfgSetGlassProperties` calls are applied before the level script
  spawns into them. The play mode gives them the level's collision mesh and path data, ticks them twice a step, and
  sends player 1's landed hit to the pane or door the strike meets. A broken barrier is removed at its next 60-tick
  update: its handlers get message 2 (itself, `NilHandle`) and its spawn record goes for good. It draws each swinging
  door's leaves (hinged at
  their positions, `w` the type's `CfgObj` argument 15, swinging by the 28-tick slerp of [Leaves](#leaves)) and each
  barrier as its type's model (gone once broken; `world_objects::doorDraws()`) with the
  world objects, and the panes as [Drawing a pane](#pane-draw) gives (`world_objects::glassDraws()`, drawn from
  `part_page1` after the humans and before the rings). Their lock-pick callbacks call the scripts, a break-in moves
  the `CrimeScene` flag, and their sounds go to `repo:src/audio/object_sounds.h`, which plays a name hash through the
  `SoundPlayer`.
- **Disc check (NTSC-U, counts only):** `coney_tests "[disc][objects]"`: `level2` places its 26 doors (14 swinging, 12
  barriers, 21 leaves), every type configured by a `CfgObj`, and 25 panes; 19 glass types are set. `coney
  --play-level level2` hands all of them to the play mode; three plain punches (4 damage each) break a 10-hitpoint
  `dyn_door_fence` barrier, whose triangles then let the strike through.

Coney's stand-ins, where this page is silent:

- A leaf model is `dyn_dr_` and the type name after `dyn_door_`; a door model the Object List lacks draws `dyn_dr_`
  and the name less a leading `dbl` (`dyn_door_fence` draws `dyn_dr_fence`). A model stage a splintering door or a
  cabin leaf takes is not drawn. Panes are not sorted by their distance within the frame (all share batch 0), a
  pane's body is a box only an airborne human meets ([Moving into a pane](#pane-break): the push-out sphere of
  [Characters](characters.md#falling) stands for both the airborne body and the jump's strike shapes, and every whole
  pane has its body whatever its distance from a camera), and the leaf's pose steps per 60 Hz tick.
- `DoorOpen`'s "away" uses the door's turned y axis. `OpenDoorAnimated` and a lock pick's success open at once
  (`DoorOpen`), without human state 26.
- A shard's offset is a random step of 1/1000 in ±1. A link's distance
  is to its middle. Of several holes whose boxes hold a doorway's middle and none of which takes it in, the
  nearest vertex average wins. Wreck pieces and boards spawn at the door; a cabin door keeps
  its leaves once broken. Game state bits 2 and 4 are not read. An
  object type no `CfgObj` names is a swinging door of 100 hitpoints.
- Square with no human in front aims at a whole pane (its centre) as `Player_PickTarget`'s object pass does
  ([Combat](combat.md#targets)); the object attack's hit breaks that pane. Doors are not object targets yet. Any other
  hit meets a pane or door along a ray 1 m above the feet, along the facing, as long as the attack's reach. A moving
  attack (the charge, dive, run and walk attacks) casts that ray every update of its strike window
  ([Combat](combat.md#moving-strikes)) instead of its strike shapes, 0.8 m long (1.05 m for the dive: where the shapes
  first met the fence), striking each object once per window; the broken fence does not stop the charge, whose
  triangles are off at once. Thrown
  objects do not reach the objects yet: nothing is thrown in play.
- Sounds: a name hash plays on the effects bus at its recorded volume, with no 3D attenuation or pan; a material
  pair's sound and the lock pick's click are counted, not played (no sound matrix or interface cues yet). Shards,
  crimes beyond the `CrimeScene` flag, statistics, loose objects and models do nothing yet.
- Shards (`gamemodes/level_object_services.h`, with [Particles](particles.md)): the culling step's two distance tests
  are one in Coney, player 1 within 10 m of the pane, and the budget is 158 particles left in the pool; dust and bursts
  are a `sub_shack_puff`. A broken pane also frees the stereo of a parked car within 2 m of it
  ([Cars](cars.md#coneys-implementation)).

## Open questions

- Which search mask admits `0x40` links.
- What human state 26 (the animated open) plays and when it sends the door `0x0b`; what fills a player's interaction
  record (`+0x660`) for a door.
- Check at runtime that a broken pane is no longer drawn ([Drawing a pane](#pane-draw)), and that a leaf model
  spans its local x from 0 to −w ([Leaves](#leaves)).
- How the PTank builds a matrix sprite's corners and texture coordinates (inferred equal to the near path's), and
  which of a shard's two colours the particle draw uses.
- How the draw blends a translucent object (the column's material and vertex alpha, the PS2 blend), and where the
  instance's colour bytes are set from `+0xc8`.
- How the instance places the model: whether its clump's atomic frame (identity, or the z-up turn with an offset) is
  used; Coney leaves it out.
- Who spawns `sub_objective_glow`; which levels use the purple and white marker kinds.
- How the resource manager picks the object whose model it loads next (`+0xbd4`), and whether `level100`'s packs
  hold the Wonder Wheel's models.
- What the first camera's vtable `+0x214` returns (the streaming-out distance).
- Held objects: what fills human `+0x348` / `+0x34c` (event `0x37`), what message `0x17` to the holder does, who
  sends `melee_weapon` messages `0x12` / `0x13`, and the frame of a thrown object's angular velocity.
- `dyn_door_vargas`' second object, and the leaf models of `dyn_door_chainlnk_pick` (no `dyn_dr_chainlnk_pick` record).
- What a cabin door's leaves do once it breaks, and where the wreck pieces and boards appear.
- Moving into a pane ([Moving into a pane](#pane-break)): which clip events switch the strike shapes on in a jump.
  (Answered on [Combat](combat.md#moving-strikes): the strike shapes' sizes and events, record `+0x08` bit `0x800`,
  and a knock-back flight, which is not airborne. Human vtable `+0x10c`, `0x00227180`: any non-human body may be
  struck, a human unless its state word has `0x100000000`.)
