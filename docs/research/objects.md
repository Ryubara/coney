# World objects: spawning, models, tint, glass and doors

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), and a disc check (2026-10-06) of what the scripts pass and of the Object List and the `WonderWheel_100`
scene, reported as counts and values. No runtime claims.

## Purpose

What level scripts set on the world they build and what the player breaks or opens in it: how an object a script
spawns finds its **model**, where it stands and what moves it ([Dynamic objects](#dynamic-objects), with the front
end's Wonder Wheel as the worked case), an object's **tint**, the **breakable glass** panes, the **doors** and the
**breakable barriers**. The lists are
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
| `0x00396bd0` | `Obj_SetColour` | `ObjColor`: packs `{r, g, b, a}` into the tint word | confirmed (code) |
| `0x0038fab8` | `GlassTypes_Set` | `CfgSetGlassProperties`: one entry of the glass type table | confirmed (code) |
| `0x0039c0e0` | `Glass_Spawn` | pushes the pane's arguments, creates it by type name | confirmed (code) |
| `0x0038f8a8` | `GlassManager_Create` | allocates the pane, runs its initialiser, the window link | confirmed (code) |
| `0x003e29e8` / `0x003e3058` / `0x003e2d90` | `glass_script` init / update / message | the pane | confirmed (code) |
| `0x0038f378` | `Glass_Break(pane, breaker, object)` | the alarm, the window link, the window flags | confirmed (code) |
| `0x003e4be0` / `0x003e4cb8` | `sub_glass` init / update | the shatter: sound and shards | confirmed (code) |
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
| `0x003b2f40` / `0x003b3158` / `0x003b2180` | `dyn_door_fence` init / message / hit | a breakable barrier | confirmed (code) |
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

How the renderer uses the word is not traced.

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
| `+0x54` | flags: `0x8001` at spawn; `4` hidden, `0x800000` broken |
| `+0x70` / `+0x80` / `+0x90` | the edge along the width, the unit normal (width × height edge), the edge along the height |
| `+0xa0` | centre (copy) |
| `+0xb0` | sprite word (type 14: its own sprite batch, high 16 bits only) |
| `+0xb4` | colour, `0x808080e0` (0 for type 15) |
| `+0xb8`-`+0xc4` | the two texture-coordinate pairs |
| `+0xc8` | size word: whole metres of width, height `<< 16` (truncated) |
| `+0xcc` / `+0xd4` | width / height in metres (floats) |
| `+0xd8` / `+0xdc` | the two collision triangles |
| `+0xf8` | alarm bits (3 with the alarm, `0x0038ec30`) |
| `+0xfa` | type |

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
`position + rotation × (−2 × w, 0, 0)` turned 180° about the vertical, where `w` is the type's float property 5
(`0x003a3898`); the leaf objects get `+0x124` = 2 (left) and 4 (right). Confirmed (code):

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
   senders are a human's landed hit (`0x0021b290`), a thrown object (`0x00393538`) and `BreakGlassInRadius`
   (`0x003963b8`: every pane whose centre is within the radius, and every `TYPE_GLASS` object). Confirmed (code).
5. **Hit** (message 1, `0x003e2d90`), once, while not broken: the broken sprite (or the pane hidden when it is 0),
   a `sub_glass` shatter at the pane's centre with the pane's rotation and size (`sub_stained_glass` for type 14), the
   triangles disabled, its collision body removed (`0x003a5340`), the pane marked broken. Type 12 also frees every
   `dyn_carstereo` within 2 m (`0x003a5870`): a car window. Message 0 is a shatter without the broken flag (the
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

### The shatter {#shatter}

`sub_glass` (`0x003e4cb8`) runs once and ends. Confirmed (code):

1. **Count** = 10 × width × height (whole metres from the size word); a pane under 2, or exactly 1 × 1, uses 10
   shards of size 0.06 and the sound of materials `GLASS_SMALL` × `GLASS_SMALL` (88); others the sound of `GLASS` ×
   `GLASS` (2). The sound is the material pair's entry in the sound matrix ([Sound](sound.md#play)).
2. **Culling**: no shards unless the pane is within 15 m (`0x003a5280`) and 10 m (`0x003a51f8`) of the tests' points
   and the particle budget (`0x003a5a50`) allows; game state bit `0x20` (`0x0041cf30`) forces them.
3. **Shards**: for each of the count, two tries, each taken at 2 in 3 (`0x003353b8(rng, 2) < 2`): a `glasstest`
   particle at a random point within ±0.571 of the half-width and half-height on the pane's plane, with the pane's
   colour word. The count is capped by writing `0x4f` into the size word when it exceeds 79.

The shard sheet and how `glasstest` falls are not traced ([Particles](particles.md)).

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
from its turned base. The leaf stores it and copies it to `+0x40` on its next update (`0x003fb5a0`); how the leaf
eases there is not traced.

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
`0x1400000` (the run attack, charge or dive) and 3 while airborne (object flag `0x4000000`); a thrown object
(`0x00393538`) sends 1 (3 for `TYPE_MISSIONTV`, 0 for an object of animation set 5 other than a molotov). Every door
and barrier takes **4 + 6 × kind** hitpoints: 4, 10, 16 or 22. Confirmed (code) at `0x003f9ad0`, `0x003fa438`,
`0x003b2180`.

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
60 ticks. Hitpoints from the type, written to `+0x128`. A hit (`0x003b2180`) takes 4 + 6 × kind and makes dust and 20
splinters. One that leaves hitpoints swaps to the damaged model (`0x7f97c350`). At 0 or below: triangles off, the
number's links opened; all but `dyn_door_wall_a`/`_b` throw three `dyn_wooddmg_a`/`_b` boards (unless game state bit
2); then the barrier hides itself, or, for `dyn_door_vargas`, which makes a second object at spawn (data `+0x08`),
destroys that object and takes model `0x3b4fefad`. The type's material pair sounds unless game state bit 4.
Message 10 sets whether it can be hit (`0x003a17e8`), `0x19` its hitpoints. Confirmed (code) for `dyn_door_fence`; the
other five classes are alike by their shared calls, not read in full.

The player charging one: [Combat, slot 10](combat.md#breakables) (a charge is kind 2: 16 hitpoints, so a fence's 10
go in one).

### Lock picking {#lock-pick}

The door side, confirmed (code) at `0x003f9588`, `0x00397078`:

- **Pickable on** (state command 10, `SetDoorPickable(door, true)`, and at spawn for `dyn_door_chainlnk_pick`,
  `dyn_door_storeb` and `dyn_door_templedoor`), only while its angle (data `+0x00`) is under 2: a `sub_triglint`
  glint 1.3 m up at half the leaf width, drawn while within 30 m (`0x003eb0d0`); door object `+0x124` = 10; its
  collision body removed (`0x003a5340`).
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

## Coney's implementation

**Object types and spawn records** (`src/world_objects/object_types.h`, `src/world_objects/spawn_records.h`,
the bindings in `src/scripting/spawn_bindings.h`): `CfgObj` adds a type (name, class, hitpoints and the model hash, the
CRC-32 of the name) and still records its arguments for what reads the rest; `ObjSpawn` adds a spawn record (type,
position, rotation, zone, flags, tint, flag name) and returns its handle; `CfgSetDatabaseSizes` makes the pool (its
object count plus 500). Resolving a handle marks its record live and pinning keeps it. A fresh script state starts
with no records but keeps the types, which the legal screen's preloads configure once
([Scripting](scripting.md#life-of-the-lua-state)).
**Coney's choices:** a record's handle comes from the counter every world object's handle comes from, not the record
index << 16 (record 0 would be `NilHandle`); the rotation is kept as given rather than packed into s16 × 4,096; there
are no object tasks yet, so "live" marks a record a consumer draws; and the keys and the power cuffs are always
suppressed, as a fresh profile has neither unlockable. `ObjEnableZone` sets or clears a zone's bit of the records'
zone mask (zone 0 on at a level's start, the rest off); `ObjShow` and `ObjHide` resolve the handle and mark the record
shown or hidden (`ObjShow`'s distance kept); `ObjDestroy` makes a human holding the object let go, then removes the
record for good, by either of its paths (`src/scripting/world_bindings.h`). Nothing streams by zone or draws the
hidden mark in play yet: Coney has no object tasks there. Disc check (NTSC-U, 2026-10-06, counts only): at the front end
`level100.lua` leaves 29 Wonder Wheel records, each of a configured type, the wheel tinted `0x474542FF`.

**Placed objects** (`src/world_objects/placed_objects_file.h`, from [the objects file](#objs-file)): after the level
script's main chunk, `runLevelScript` reads `<level>_objs.txt` through the script source and adds a spawn record per
line, each with a handle from the world objects' counter; the `part` lines are skipped (no particle emitters from it
yet). A missing file adds nothing. Disc check (NTSC-U, 2026-10-06, counts only): `level99` places 98 records of its
112 lines.

**Models** (`src/platform/object_models.h`, `src/platform/placed_objects.h`): an object's model is its type's Object
List record (`ObjectList::findByHash`, the type's model hash), its `0x47` model read as the level file's and its
dictionary's first texture on the first material, loaded once per type and shared. An object is drawn at its pose
in the game's axes carried into RenderWare's as positions are, `(x, y, z) → (x, z, −y)`, the model's y up being the
game's z up (inferred; with it the Wonder Wheel stands where the runtime picture has it). Not modelled: the size cull
and fade, per-object lights, the tint and the second dictionary. The front end draws the live records' objects this
way ([Front end](frontend.md#coneys-implementation)).

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
  sends player 1's landed hit to the pane or door the strike meets. Their lock-pick callbacks call the scripts, a
  break-in moves the `CrimeScene` flag, and their sounds go to `repo:src/audio/object_sounds.h`, which plays a name
  hash through the `SoundPlayer`.
- **Disc check (NTSC-U, counts only):** `coney_tests "[disc][objects]"`: `level2` places its 26 doors (14 swinging, 12
  barriers, 21 leaves), every type configured by a `CfgObj`, and 25 panes; 19 glass types are set. `coney
  --play-level level2` hands all of them to the play mode; three plain punches (4 damage each) break a 10-hitpoint
  `dyn_door_fence` barrier, whose triangles then let the strike through.

Coney's stand-ins, where this page is silent:

- `w` (the type's float property 5) is half the `CfgObj` box's width; the glint stands at `(−w/2, 0, 1.3)` in the door's
  frame; a leaf model is `dyn_dr_` and the type name after `dyn_door_`.
- A leaf takes its target rotation on the next tick (no easing). `DoorOpen`'s "away" uses the door's turned y axis.
  `OpenDoorAnimated` and a lock pick's success open at once (`DoorOpen`), without human state 26.
- A large pane's shards are 0.06 like a small one's; a shard's offset is a random step of 1/1000 in ±1. A link's distance
  is to its middle. Of several holes whose boxes hold a doorway's middle and none of which takes it in, the
  nearest vertex average wins. Wreck pieces and boards spawn at the door; a cabin door keeps
  its leaves once broken. A barrier's material pair sounds on every hit; game state bits 2 and 4 are not read. An
  object type no `CfgObj` names is a swinging door of 100 hitpoints.
- Square with no human in front aims at a whole pane (its centre) as `Player_PickTarget`'s object pass does
  ([Combat](combat.md#targets)); the object attack's hit breaks that pane. Doors are not object targets yet. Any other
  hit meets a pane or door along a ray 1 m above the feet, along the facing, as long as the attack's reach. Thrown
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
- How a leaf eases to its target rotation (`+0x40`), and the type's float property 5 (half a leaf's width?).
- Which sheet the glass sprite batch and the `glasstest` shards draw from.
- How the renderer applies an object's tint word.
- How the resource manager picks the object whose model it loads next (`+0xbd4`), and whether `level100`'s packs
  hold the Wonder Wheel's models.
- What the first camera's vtable `+0x214` returns (the streaming-out distance).
- The shard size of a large shatter; whether `0x003353b8(rng, 2) < 2` is 2 in 3 (as read here) or always.
- `dyn_door_vargas`' second object, and the leaf models of `dyn_door_chainlnk_pick` (no `dyn_dr_chainlnk_pick` record).
- What a cabin door's leaves do once it breaks, and where the wreck pieces and boards appear.
