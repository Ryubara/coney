# World objects: tint, glass and doors

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), and a disc check (2026-10-06) of what the scripts pass, reported as counts. No runtime claims.

## Purpose

Three things level scripts set on the world they build: an object's **tint**, the **breakable glass** panes and the
**doors**. The lists are [Object tints](../references/tints.md), [Glass types](../references/glass-types.md) and
[Doors](../references/doors.md); the classes and pools of world objects and glass panes are on
[Tasks](tasks.md#classes), and the crimes a pane can raise on [AI: crimes](ai.md#crimes).

## Original structure

The code sits in the unattributed `TaskEngine` stretch ([Source map](source-map.md)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00396bd0` | `Obj_SetColour` | `ObjColor`: packs `{r, g, b, a}` into the tint word | confirmed (code) |
| `0x00398940` | `ObjRecord_Add` | `ObjSpawn`'s spawn record; the tint at `+0x14` | confirmed (code) |
| `0x00399080` | `ObjRecord_Spawn` | makes the object from a record; copies the tint | confirmed (code) |
| `0x0038fab8` | `GlassTypes_Set` | `CfgSetGlassProperties`: one entry of the glass type table | confirmed (code) |
| `0x0039c0e0` | `Glass_Spawn` | pushes the pane's arguments, creates it by type name | confirmed (code) |
| `0x0038f8a8` | `GlassManager_Create` | allocates the pane, runs its initialiser, the window link | confirmed (code) |
| `0x003e29e8` | `glass_script` init | reads the arguments, per-type cases | confirmed (code) |
| `0x003e2d90` | `glass_script` message | 0 shatter, 1 hit | confirmed (code) |
| `0x0038f378` | `Glass_Break(pane, breaker, object)` | the alarm and the window link | confirmed (code) |
| `0x00397230` | `Door_Spawn` | pushes the door's arguments, creates it by type name | confirmed (code) |
| `0x003fb5f8` | `dyn_door_swinging` init | reads the arguments, sets up the leaves | confirmed (code) |
| `0x00250c00` | `NavLinks_SetKindByNumber(number, kind)` | retags every navigation link carrying a number | confirmed (code) |

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

`config_preload2.lua` configures types 0-18. The pane task: triangles `+0xd8` and `+0xdc`, type `+0xfa`, alarm bits
`+0xf8`, sprite word `+0xb0`, colour `+0xb4` (`0x808080e0`), size `+0xc8` (width, height `<< 16`), flags `+0x54`
(`0x800000` broken, `4` hidden).

### Doors {#doors}

A door's initialiser (`0x003fb5f8` for the class `dyn_door_swinging`; the other door classes alike) reads
`SpawnDoor`'s last three values back: the number into `+0xe0`, the two triangles into `+0xd8` and `+0xdc`.
Confirmed (code). On the disc each level's numbers run from 1 and never repeat (484 doors in 45 levels).

The level's **navigation links** are 8-byte records (`0x00510590`, count `0x00510594`): a kind at `+4` and, in the low
13 bits of `+6`, a door number. Kinds seen: 4 a jump link and `0x10` a door link (`ConvertJumpToDoor`, `0x00250db0`),
and `0x40`, which breakable doors and windows set. Bit 31 of a link's first word disables it (`DisableDoorLink`,
`0x00250b68`). Confirmed (code) for the fields; the kinds' names are inferred from the bindings.

## Behaviour

### A pane's life {#pane}

1. **Spawn** (`0x0039c0e0`): the three corners, the type, the texture coordinates, the flag and the two triangles
   are pushed, and a pane task named by the type in decimal is made. The initialiser (`0x003e29e8`) pops them in
   reverse and drops the flag. Confirmed (code).
2. **Setup**: both triangles are made two-sided (`0x003a4768`); the sprite rectangle is the type's whole-pane word,
   except type 14, which makes a sprite batch of its own (`0x00020000`); type 15 is hidden (colour 0); types 17 and
   18 start broken, their triangles disabled. With the alarm, pane bits `+0xf8` = 3 (`0x0038ec30`). Confirmed (code).
3. **Window link** (`0x0038f8a8`): with the window link, the jump link through the pane is disabled and, when the
   pane is in a navigation sector, retagged kind `0x40`. Confirmed (code); "the AI climbs through once broken" is
   inferred.
4. **Hit** (message 1, `0x003e2d90`): the broken sprite (or the pane hidden when it is 0), a `sub_glass` shatter
   (`sub_stained_glass` for type 14), the triangles disabled, the pane marked broken. Type 12 also frees every
   `dyn_carstereo` within 2 m (`0x00396090`): a car window. Confirmed (code).
5. **Break** (`0x0038f378`, from a human's hit `0x0021b290` or a thrown object `0x00393538`): with the alarm, the
   `CrimeScene` flag moves to the pane and a break-in (crime type 1) is reported; with the window link, its link is
   enabled again. Confirmed (code).

### Doors and their numbers {#door-numbers}

The breakable doors set every navigation link carrying their number to kind `0x40` (`0x00250c00`): the classes
`dyn_door_fence`, `dyn_door_bar_bani`, `dyn_door_bnstr`, `dyn_door_fence_o` and `dyn_door_parapet` (initialisers
`0x003b2f40`, `0x003b3250`, `0x003b4128`, `0x003b57d0`, `0x003b6220`), and the swinging types `dyn_door_dclub` and
`dyn_door_liz` (`0x003f80f0`). Confirmed (code). Other readers of a door's number are not traced.

## Coney's implementation

None yet.

## Open questions

- What path finding does with links of kind `0x40`, and whether breaking a door reopens them.
- Which sheet the glass sprite batch draws from.
- How the renderer applies an object's tint word.
