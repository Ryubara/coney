# World objects: tint, glass and doors

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), and a disc check (2026-10-06) of what the scripts pass, reported as counts and values. No runtime claims.

## Purpose

What level scripts set on the world they build and what the player breaks or opens in it: an object's **tint**, the
**breakable glass** panes, the **doors** and the **breakable barriers**. The lists are
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
| `0x00396bd0` | `Obj_SetColour` | `ObjColor`: packs `{r, g, b, a}` into the tint word | confirmed (code) |
| `0x00398940` | `ObjRecord_Add` | `ObjSpawn`'s spawn record; the tint at `+0x14` | confirmed (code) |
| `0x00399080` | `ObjRecord_Spawn` | makes the object from a record; copies the tint | confirmed (code) |
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
| `0x00250c50` / `0x00250d00` | `NavLinks_OpenByNumber` / `NavLinks_CloseByNumber` | bit 31 of every link with a number | confirmed (code) |
| `0x00250960` | `NavLink_FindNearest(pos, kinds, &link, &back)` | the nearest link of a kind within 5 m, and its reverse | confirmed (code) |
| `0x00417480` | `Flags_DisableNear(radius, pos, activity, 0)` | disables flags of one activity near a point | confirmed (code) |
| `0x003a5340` / `0x003a52c8` | `Obj_RemoveBody` / `Obj_AddBody` | an object's body in the collision world (`0x00597198`) | confirmed (code); names inferred |
| `0x00117280` | `Sound_PlayMaterialPair(a, b, pos)` | the sound matrix entry of two materials ([Sound](sound.md#play)) | confirmed (code) |
| `0x003a6ee8` | `Sound_PlayHashAt(hash, pos)` | a 3D sound by name hash | confirmed (code) |

## Data

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

| Kind | Who sets it | How a follower takes it ([Following a route](ai.md#route-follow)) |
| --- | --- | --- |
| 4 | the data | the choke point |
| `0x10` | the data; `ConvertJumpToDoor` (4 → `0x10`) | `0x0029baa8`: faces the waypoint, then either walks or starts a jump (`Human_BeginJump`) |
| `0x40` | breakable doors (by number) and window-linked panes | `0x0029bca0`: faces the waypoint, sets the move action to 4, and issues the **charge** (attack kind 19, command `0x20`) |

So an AI routed through a breakable door or a pane charges it. Confirmed (code); the edge mask that admits `0x40`
edges to a search is open ([AI](ai.md#path-planning)).

## Behaviour

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
| 6 | triangles enabled, the object's collision body back (`0x003a52c8`), the number's links get the avoid bit and their polygon loses flag 8 (`0x00250d00`) |
| 5 (`DisableDoorCollision`) | the number's links lose the avoid bit and their polygon gets flag 8 (`0x00250c50`), triangles disabled, the collision body removed (`0x003a5340`) |
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
- opening (state command 5) or breaking clears their avoid bit and sets their polygon's flag 8; closing sets it and
  clears the flag.

`DisableDoorLink` / `EnableDoorLink` do the same by position for the nearest `0x10` link within 5 m and its
reverse. Confirmed (code).

## Coney's implementation

None yet.

## Open questions

- Which search mask admits `0x40` links, and what path polygon flag 8 does to the walkable-line test.
- What human state 26 (the animated open) plays and when it sends the door `0x0b`; what fills a player's interaction
  record (`+0x660`) for a door.
- How a leaf eases to its target rotation (`+0x40`), and the type's float property 5 (half a leaf's width?).
- Which sheet the glass sprite batch and the `glasstest` shards draw from.
- How the renderer applies an object's tint word.
