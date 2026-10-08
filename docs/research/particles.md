# Particles and the script type table

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), and a disc check (2026-10-06) of the sprite sheets named below, reported as counts and sizes. No runtime
claims.

## Purpose

What `SpawnParticle` makes and where effect types come from: the **script type table**, which names every particle
system and also the behaviours of world objects, lights and glass; how a particle system draws (a **sprite word**
into a sprite sheet); and how one type spawns others. The list of every type is
[Particle effects](../references/particles.md); the sprite sheets themselves are on [GUI](gui.md#particle-page).

In one paragraph: there is no particle data file. Each effect type is **compiled code**: a record of the table at
`0x00512f28` gives its name, three functions (initialise, update, handle a message) and a flags word. Spawning a
particle system by name allocates a particle task (1,400 in play, [Tasks](tasks.md#classes)), finds the record by
name and runs its initialiser, which reads the creation arguments and sets the task's sprite word (sheet and
rectangle), colour, size and update interval. The same table serves world objects: `CfgObj`'s class names
(`simple_object`, `melee_weapon`, `pickup_item` ...) are records of it too.

## Original structure

The type functions sit in `0x003a8698`-`0x00408ac8`, the stretch of `TaskEngine/` after `TaskManager.cpp`
([Source map](source-map.md#position), inferred). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00512f28` | script type table | 270 records of 20 bytes, sorted by name | confirmed (code) |
| `0x003c55e8` | `ScriptType_Find(name)` | binary search (`bsearch`, `0x00433a88`, 270 × 20), then a linear `strcmp` fallback; index or -1 | confirmed (code) |
| `0x003c56c8` | `ScriptType_Get(i)` | `0x00512f28 + 20 × i`, or null | confirmed (code) |
| `0x0039aef0` | particle task initialiser | `Task_Init`, a handle, the type record (vtable `+0x194`), defaults | confirmed (code) |
| `0x0039bfb0` | `Particle_Spawn` | pushes position, rotation and parent, creates by name, starts it | confirmed (code) |
| `0x003a4e88` | `PTank_New(depth, sprite, ...)` | a sprite batch over a sheet-table record; returns `batch << 16` | confirmed (code) |
| `0x003a5010` | `Task_SetRect(task, sprite)` | replaces the low 16 bits of a task's sprite word | confirmed (code) |

## Data

### Script type record {#type-record}

```c
struct ScriptType {          // 0x00512f28 + 20 * i, i < 270
    const char *name;        // +0x00
    void (*init)(Task *, Message *);         // +0x04
    int  (*update)(Task *);                  // +0x08
    void (*message)(Task *, int, Message *); // +0x0c
    uint32_t flags;          // +0x10
};
```

**Evidence:** confirmed (code) for the name and the search (`0x003c55e8`) and for `+0x04`, `+0x08`, `+0x0c` being
code (the radar dot's are `0x003e5bf8`, `0x003e6008`, `0x003e5f20`; the message handler switches on the message number
`0x11`, `0x15`, `0x19`, `0x29`, `0x2a`, `0x34`). Which call is init, update and message is inferred from what each does.
Flags seen: `0x10` on 183 particle systems, `0x08` on 64 object behaviours, `0x04` on 22 lights, `0x400` on
`glass_script` (counts from the table; the kinds are inferred from the names and from `0x0039bba0`, which accepts a
task as a particle system only when its type bits have `0x10`).

Several names share one set of functions: the ten fire types (`part_fire`, `part_fire_large`, `part_torch_flame` ...)
all use `0x003ca050`, and the narrow and square flames `0x003c84f0`, which then tells them apart by name (`strcmp`
against `0x00584420`, `0x00584438`, `0x00584450`).

### Particle task fields {#task-fields}

The particle task is `0xf0` bytes ([Tasks](tasks.md#classes)). Fields the type code writes, confirmed (code) in the
initialisers named:

| Offset | Meaning |
| --- | --- |
| `+0x10` | position (vec4) |
| `+0x20` | rotation (quaternion) |
| `+0x54` | flags (`0x04` hidden, inferred from messages `0x29` / `0x2a`) |
| `+0xb0`, `+0xb4` | colours, `0xRRGGBBAA` (the radar's `0x001b40f8` packs them so) |
| `+0xbc`, `+0xc0` | size or scale (the radar's icon scale multiplies both, `0x001c4768`) |
| `+0xc4` | **sprite word**: batch `<< 16` \| rectangle |
| `+0xd4` | the task's own handle (`0x0039aef0`) |
| `+0xdc` | what the object at `0x00512b04` returns for the name (its vtable `+0xcc`; not traced) |

### Sprite words {#sprite-words}

A sprite is named by one 32-bit word whose low 16 bits are a **rectangle** of a sprite sheet and whose high 16 bits
say which sheet:

- given to `PTank_New` (`0x003a4e88`) the high half is a **record of the sprite-sheet table** (chunk `0x4D`,
  [GUI](gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header)): `sra a1,a0,0x10` then `0x001828c0(rm, record)`;
- stored at a task's `+0xc4` it is a **batch** (resource instance) number: the one `PTank_New` returned, or one of the
  thirteen batches the resource manager makes at start (`0x00184918`; [GUI](gui.md#resource-instances)).

Both are confirmed (code). The start-up batches and the sheet records they draw:

| Batches | Sheet | Sheet record |
| --- | --- | --- |
| 0, 1, 4, 9, 10 | `part_page1` | 1 |
| 2, 3, 5 | `part_page0` | 0 |
| 6 | `big_font` | 13 |
| 7, 8 | `lighting` | 8 |
| 11 | `hud_minigames` | 10 |
| 12 | `part_fire` | 7 |

Types write their sprite as constants such as `0x80002` (`part_gun_flash`: `PTank_New(1.0, 0x80002, ...)` at
`0x003d8bd8`, sheet record 8 `lighting`, rectangle 2), `0x10000 | 0x2a` (`sub_shack_puff`, `0x00408860`: batch 1
`part_page1`, rectangle 42 plus a random 0-2, since `Random_Int(n)` at `0x003353b8` returns 0 to n inclusive) or
`0xc0000` (the fire types: batch 12 `part_fire`). Some take the rectangle from their creator instead (`spark`,
`0x003e0f50`, pops it as its first argument).

Sheet records 0-15 on the disc (counts of rectangles): 0 `part_page0` 371 (512 × 256), 1 `part_page1` 67
(512 × 256), 3 `menu_system` 6, 4 `part_tv` 4, 7 `part_fire` 36 (256 × 256), 8 `lighting` 6 (256 × 256),
10 `hud_minigames` 13, 13 `big_font` 262; records 2, 9, 27 (72 rectangles, the flames) and 409 are unnamed.

**The sprite of each type** in [Particle effects](../references/particles.md) is the first one its code sets, found by
following its three functions to depth 3 for constant sprite words passed to `PTank_New`, `Task_SetRect` or stored
at `+0xc4` (inferred; checked by hand for `part_gun_flash`, `sub_shack_puff` and the flames). A type may then animate
through further rectangles (the flames step through 36, `0x003c84f0`), and 212 of the 270 have no constant sprite
(their creator passes it, or they draw nothing of their own).

## Behaviour

### Spawning {#spawning}

`Particle_Spawn` (`0x0039bfb0`), behind `SpawnParticle`: resolve the parent handle, push the position (w = 1), the
rotation and the parent into the [message scratch](tasks.md#messages) (`0x003a2d60` a vec4, `0x003a2dc0` a handle,
`0x003a2da0` an int), create by name (`0x003a2e40` → `0x003a4160` → the particle manager's allocator `0x0039c698`), then
call the new task's vtable `+0x2c`, which gives its handle, and return that (inferred: `+0x2c` is the handle getter, as
for objects, [Tasks: handles](tasks.md#handles); nothing else is started there). The initialiser `0x0039aef0` runs
`Task_Init`, takes a handle, looks the type up by name (`ScriptType_Find`) and hands the record to vtable `+0x194`;
default colour `DAT_005fd268`, scale 1.0, 5.0 and 0.3 at `+0xc8`-`+0xd0`. The type's own `init` then **pops** its
arguments last-pushed first (`0x003a8518` an int, `0x003a84a8` a vec4). Confirmed (code).

A type spawns others the same way, by name: the 33 such links found are in the list's *Spawns* column (inferred:
calls to `0x003a2e40`, `0x003a4160` or `0x001e99f8` with a constant name). The car initialiser makes
`part_copcar_lights` for the police car ([Cars](cars.md)); the radar makes `hud_radar_dot` ([GUI](gui.md#radar-icons)).

A name that is not in the table finds no record: `part_ominoussmoke`, which one script spawns six times, is such a
name (the table has `part_ominous_smoke`). What the task then does is not traced.

**The list's images** (Coney, [Building](../guides/building.md#reference-images)): each traced type's rectangle, cut
from its sheet's texture and scaled to fit 64 × 64 (the `lighting` glows are 120 × 120), without the type's colour.
Three types traced to rectangle 54 of `part_page1` (`part_s_subway_sparks`, `subway_spark`, `urine_spray`) show a
grey box there, not a spark or a spray (checked on the images), so their trace is in doubt.

### Steam vents {#steam}

`part_steam` (init `0x003f69b8`, update `0x003f6bf0`, messages `0x003f6b40`) is a vent that `CfgSteam` configures
(message `0x27`, `0x003f6720`) and that spawns `sub_smoke` puffs (init `0x003f61d8`, update `0x003f6460`).
Confirmed (code) unless marked.

**The vent.** `CfgSteam` stores the puff colour, the drag vector (dragH, dragV, size, growth), the puff update
interval, the vent's own interval and the puff life in updates, `round(life × 60) / puffInterval` (60 Hz ticks, so
`life` is in seconds); the start velocity is the vent's −x axis (its rotation applied to (−1, 0, 0)) times `speed`,
with z replaced by `rise`. `still` makes the life count negative, which the puff reads as "no wind". Enabled vents
(message 10) run every `interval` ticks while the vent is within 30 m (`0x003a5280`) and 20 m (`0x003a51f8`) of the
tests' points and the [particle budget](objects.md#shatter) (`0x003a5a50`) allows, and every 60 ticks otherwise;
each run spawns one `sub_smoke` at the vent with the colour, the velocity, the drag vector, a spin of 0, the puff
interval and the life.

**A puff** (init): position the vent's; velocity the start velocity with z × 0.8-1.2, set once on the task and
integrated by `Task_Integrate` (position += velocity × elapsed seconds), so **`speed` and `rise` are metres per
second**; update interval `puffInterval`; size `size × 0.8-1.2`; colour the given one, alpha starting at 0; a wind
factor of 0.005-0.015 (0 when `still`). Sprite: `part_page1` (batch 1) rectangle 42, 43 or 44 (`0x1002a +
Random_Int(2)`, which returns 0-2); a puff given a spin would use batch 4 and turn at a random rate up to it, but the
vent passes 0.

**Each puff update** (every `puffInterval` ticks), with `age` counting updates up to `life` (then the puff ends) and
`f = age / life`:

- velocity = `v0 + wind − v0 × f × (dragH, dragH, dragV)`, then × a random 0.75-1.15, where `v0` is the start
  velocity and `wind` is the [wind vector](#garbage) at `0x006f31a0` × the wind factor × the current size. So
  **drag scales linearly with the life**: 0 keeps the speed, 1 brings the start velocity to 0 at the end of the life
  (more than 1 reverses it); the wind part is not dragged.
- size = current size + `growth × 0.8-1.2`: **growth is per puff update**, not per second.
- alpha = `start alpha × (life − age) / life`, 0 on the last update or when the particle budget is short.

The task keeps each value twice (`+0xb0`/`+0xb4` colour, `+0xbc`/`+0xc0` size): the update writes the second, and
the size it grows from is the first. The draw blends from the first to the second over the update interval
(`ParticleTask_Draw`, `0x0039b020`, [Drifting fog](#fog)); confirmed (code).
**Defaults.** A vent starts **on** (data `+0x2c` = 1) with built-in values, which `CfgSteam` (`0x003f6720`) replaces;
the vents in a level's [objects file](objects.md#objs-file) get no `CfgSteam` there, so they puff with these. Data
layout, from `CfgSteam`'s writes: `+0x00` the start velocity (x, y = the vent's −x axis × speed, z = rise), `+0x10`
dragH, dragV, size, growth, `+0x20` the colour word (alpha in the low byte, so `0xffffff40` is white at alpha 64),
`+0x24` the life in puff updates (negative = still), `+0x28` the puff interval and `+0x29` the vent interval (ticks).
The inits store the vent's −x axis unscaled (speed 1 m/s) and then the values below; every vent's first update
interval is 60 ticks. Confirmed (code) at the inits:

| Vent (init) | rise | dragH, dragV | size, growth | colour | life (updates) | puff, vent interval |
| --- | --- | --- | --- | --- | --- | --- |
| `part_steam` (`0x003f69b8`) | 0.25 | 0, 0 | 0.25, 0.125 | `0xffffff40` | 11 | 10, 17 |
| `part_steam_large` (`0x003f7138`) | 0.45 | 0, 0 | 0.75, 0.02 | `0xffffff54` | 11 | 10, 6 |
| `part_steam_huge` (`0x003f6d70`) | 0.75 | 0, −0.3 | 2.5, 0.32 | `0xffffff40` | 4 | 30, 7 |

The large and huge vents' updates (`0x003f7378`, `0x003f6fb8`) spawn the puffs with sizes of their own
([Steam family](script-types.md#steam-family)); how they differ from `part_steam`'s puffs was not read.

### Drifting fog {#fog}

`Start3DFog` (`Fog3D_Start`, `0x0018e148`) makes one `part_fog` emitter (init `0x003cadd8`) per screen-effects
manager; the emitter's sprite batch is `PTank_New(10000, sprite, ...)` with the script's sprite word, whose high half
is a [sheet-table record](gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header). 15 of the 17 calls pass
`0x2120000`: record **530 (`0x212`), the sheet `part_fog_00`** (CRC-32 `0x7513cd85`), rectangle 0; the other two
`0x2130000`, record 531 `part_fog_01` (`0x0214fd13`). Each sheet is one 64 × 64 texture of the same name with one
rectangle covering it. Confirmed (code) for the record; the names and sizes are a disc check (2026-10-06).

**A wisp's drift** (`sub_fog` init `0x003ca658`): its velocity is set once, at birth, and integrated like any task's
(metres per second; inferred, as for the [steam](#steam) puffs). The direction is from the wisp **toward the player's
camera** (the camera task's position, vtable `+0x21c`), plus a sideways offset: the camera's rotation (vtable `+0x224`)
applied to (r, 0, 0) with r a random −2 to 2 m, i.e. along the camera's own x axis. The sum is normalised and scaled
by `drift` × 1.75-2.25. So every wisp drifts toward the viewer, spread a little to either side, and rises or sinks
with the height difference. Confirmed (code).

**The wisp record** (`sub_fog` init `0x003ca658`, update `0x003ca9d8`; the arguments come from `PartFog_Update`
`0x003caed8`, which passes on the `Start3DFog` values): the task's sprite word `+0xc4` is the emitter's batch with
rectangle 0; colour `+0xb0` = the script colour with alpha 0 (bit 0 of the alpha kept) and `+0xb4` = the colour with
alpha 0; size `+0xbc` = 0 and `+0xc0` = a random 1-4 (`Random_FloatRange2`, the particles' stream). The type's own
record holds the age, the target colour, the fade length in steps (⌊9 / fadeSpeed⌋, at least 1), the viewport and
the alpha step (⌊alpha / steps⌋ × ⌊fadeRate⌋, one byte). The wisp updates every 2 ticks; when the step comes out 0 it
updates every 30 ticks instead, with step 1 and ⌊0.6 / fadeSpeed⌋ steps. Confirmed (code).

**Each update**, in order (`0x003ca9d8`). Before it, the shared particle step (`ParticleTask_Process`, `0x0039ba68`)
copies the current colour and size into the previous ones (`+0xb4` → `+0xb0`, `+0xc0` → `+0xbc`). Confirmed (code):

1. **Fade in**: while the age is below the fade length and the colour is not the target yet, `+0xb4` += the alpha
   step, capped at the target.
2. **Hide**: the alpha of `+0xb4` is set to 0, and the wisp updates every tick, when its camera (the viewport's
   player camera, vtable `+0x21c`) is more than 20 m away, or within 20 m when the point is more than 5 m outside the
   view (`Cameras_IsPointVisibleAny(5, position)`, a test against the six frustum planes, `0x003a50e0`), or within
   10 m when a camera flag (vtable `+0x29c`, not traced) is set. Within 4 m the alpha is also set to 0, and it updates
   every 30 ticks.
3. **End**: from the third update on, a wisp whose previous colour (`+0xb0`) had alpha 0 is done. It lowers the wisp
   count (`0x005144cc`), and the emitter spawns a new one elsewhere. It also ends when its viewport's camera is gone.

So a wisp that leaves the 20 m range or the view, or comes within 4 m, vanishes at once and is replaced one update
later (inferred from the order above).

**The draw** (`ParticleTask_Draw`, `0x0039b020`, the same for every particle task; confirmed (code) unless marked):

- **Sheet and rectangle**: the whole 64 × 64 texture. Rectangle 0 of `part_fog_00`, or of `part_fog_01` for the two
  calls that pass record 531; the rectangle covers the sheet ([above](#fog)). There is no animation through
  rectangles.
- **Colour**: the previous and current colours blended by t = time since the last update ÷ the update interval
  (`Colour_LerpRatio`, `0x00338240`, with the interval at task `+0x64`). The value is on RenderWare's 0-255 scale
  ([GUI: sprite colours](gui.md#sprite-colours)), so the script's alpha is the opacity the wisp fades in to.
- **Size**: 2 × the previous and current sizes blended the same way, as both width and height. So a wisp is a square
  of 2-8 units (metres, inferred) and grows from 0 over its first update. The width is stored negated, as for every
  particle task; its effect is not traced.
- **Orientation**: the batch is a format-0 PTank (position and size, no rotation or matrix; flags `0x10000087`), so
  each wisp is a **camera-facing square** with no spin. Its position is the task's, written as (x, z, −y) for the
  RenderWare frame of the 3D overlay world.
- **The batch** (`PTank_New(10000, sprite, origin, 10, 0, viewport)` at `0x003cae98`, then `ResourceMgr_CreateInstance`
  and `Instance_Init`, `0x001828e0`): capacity **10 sprites a frame** (`+0x18`), blend 5 / 6 (source alpha over
  inverse source alpha, `+0x30` / `+0x34`), Z test on (`+0x24` = 1, flag `0x100` clear), in the **3D overlay world**
  (`+0x3c` = 1), and drawn only in its own viewport (`+0x42`). Each frame a wisp's sprite is added in the pool's
  order. Sprites past the tenth are dropped, so with the default 20 wisps (or 15 after `MaxFogParticles`) at most 10
  show in a frame. Hidden wisps (alpha 0) still take a place (inferred from `Instance_AddSpriteFormat`,
  `0x00183038`).
- **Frame order**: `ResourceMgr_RenderOverlay` (`0x00185d20`) turns Z write off and draws the queued batches in
  ascending key. The fog batch's key is the squared distance from player 1's camera to the batch's atomic (at the
  world origin) minus 10,000², so it sorts with the other 3D batches before every 2D one (`Instance_QueueCb`,
  `0x00197000`; [GUI: draw order](gui.md#draw-order)). That is [pass 10](rendering.md#sprites) of the frame: after the
  world, the objects, the characters and the translucent objects; before the HUD and the level tint.

**Seen in a GS dump** of `level87` (scratch dump `lv_l87`; confirmed (runtime)): one run per frame of 5 `SPRITE`
primitives with a 64 × 64 `T8` texture, in context 2, blend `0x44`, alpha test NOTEQUAL 0, Z test GEQUAL with no Z
write, fog on (vertex fog 254). All five have the vertex colour (35, 40, 40), alpha 55 on the GS's scale. It is drawn
after the strips of pass 9 and before the HUD and the full-screen fans. That this run is the fog is inferred: it is
the only 64 × 64 sprite batch, and `level87` runs `Start3DFog` throughout. The state matches the [3D sprite
pass](rendering.md#sprites) of the steam.

### Blowing litter {#garbage}

`StartGarbage(kind)` (`0x00170330`, on the object at `0x005971a0`, which `0x0016fcc8` creates at start) arms 64
pieces of litter (`0x70` bytes each); `EndGarbage` (`0x00170528`) clears its active flag. Each piece is a
flat, textured card with its own position, velocity and orientation, stepped by `0x00170c88` from `Humans_Update`
at 30 Hz (fixed `dt` 1/30) and drawn by `0x001712c0` (nothing while a scene plays). Confirmed (code).

**Per kind** (the sprite is a rectangle of sprite batch 0, `part_page1`; the draw ignores the word's high half;
size in metres, inferred from the quad being 1 × 1 before scaling):

| Kind | Rectangles (texels) | Size byte → metres |
| --- | --- | --- |
| 0 | 54-58 (32 × 32, 55 is 34 × 29) | 96-102 → 0.38-0.40 |
| 1 | 30-32 (32 × 32 to 33 × 36) | 96-102 → 0.38-0.40 |
| 2 | 8-11 (20 × 20, 10 is 10 × 20, 11 is 8 × 20) | 16-32 → 0.06-0.13 |
| 3 | 18 (20 × 20) | 43-50 → 0.17-0.20 |

Other kinds do nothing. Each piece takes a random rectangle and size in its kind's range (`0x003353f0`, both ends
included), a grey of 128-190 in all three channels, a wind threshold of 0.5-10, a lifetime of 600-900 updates and an
orientation a quarter turn about a fixed axis (`0x00511720`). Breakables throw extra pieces
through `Garbage_Throw(litter, kind, position, direction)` (`0x00170b28`), described below. Confirmed (code).

**Thrown litter** (`Garbage_Throw`, confirmed (code); the table read from the executable). It takes `Random_IntRange`
(min, max) pieces, both ends included, from the 64 in index order, using **only pieces waiting to respawn** (piece
byte `+0x6d` bit `0x10`, set by the update when a piece's ground ray found nothing or it faded out); with none waiting,
nothing is thrown, and it stops early when it runs out. Each taken piece is re-made by `Garbage_SpawnPiece`
(`0x0016fd90`: looks, grey, wind threshold, 600-900 updates of life) with the entry's sprite and a size byte of
96-102 (0.38-0.40 m, whatever the entry says), then put at the given position with velocity **6 m/s along the
normalised direction** and handed to the wind step (`Garbage_ApplyWind`, `0x00170600`). The 12-byte entries at
`0x0050cc08`:

| Kind | `+0x00` sprite word (rectangle) | `+0x04` extra rectangles | `+0x05`, `+0x06` pieces | `+0x07`, `+0x08` size bytes (unused here) |
| --- | --- | --- | --- | --- |
| 0 | `0x10036` (54) | 1 (54-55) | 5-9 | 96-102 |
| 1 | `0x10036` (54) | 1 (54-55) | 5-9 | 96-102 |
| 2 | `0x1001e` (30) | 1 (30-31) | 5-9 | 96-102 |
| 3 | `0x10012` (18) | 0 (18) | 9-16 | 43-50 |

The rectangle is the word + `Random_IntRange(0, +0x04)`. Kind 0 is the trash props' ([Objects: trash
props](objects.md#trash-props)), kind 1 a few other `overhead_weapon` breaks; `DynMasks_OnHit` and `DynChicken_Break`
also call it. The trash props pass their contact vector `+0xb0` as the direction, which is zero for a prop that was
never touched (then the pieces start with no speed and only the wind moves them; inferred).

**Placement**: the pieces start on an 8 × 8 grid around the camera's position, 7 m apart (offsets −28 to +21 m), at
the camera's height −1 to +5 m. Every 20 updates (staggered) each piece casts a ray 25 m down: no ground makes it
respawn; a piece just placed drops onto the hit. A piece more than 28 m from the camera on x or y fades out (its
alpha is 4.25 × a 60-update countdown) and respawns; so does one whose lifetime ends after it has touched the ground.
The start (and, inferred, each respawn) gives a piece a wind push of at least 10 m/s horizontally. Confirmed (code).

**Motion, each update**:

- gravity: z velocity −0.327 × (1 − 0.5 × |tilt|) per update, i.e. 9.8 m/s² edge-on and half that flat (tilt from
  the piece's orientation matrix);
- wind, every 1-30 updates at random (`0x00170600`): w = the wind vector at `0x006f31a0` × 0.75-1.0; below the
  piece's threshold nothing; above it the piece tumbles (a random spin and a tilt kept within ±15°, slerped over
  30-60 updates) and its velocity becomes `0.7 × v + (w × (1 − threshold / |w|) + random × 0.4 × excess) / 30`;
- collision: a ray along the velocity from 0.5 m behind the piece; a hit within 0.7 m removes the velocity's part
  into the surface, lays the piece flat on it (slerped over up to 8 updates) and marks it grounded;
- position += velocity / 30.

The wind vector is written by `Wind_Manager`'s code (`0x00407318`, `0x00407758`; not read); steam puffs read the same
vector. Confirmed (code) for the reads; that it is wind is inferred from the name.

### Glints {#glints}

The small blinking stars on pickups and on lock-pickable doors. Confirmed (code) at each address; the constants were
read from the executable.

**Who makes them.** A `pickup_item` that is not held and has no glint creates a `sub_triglint` at its position
(`PickupItem_ShowGlint`, `0x003f16e8`) and removes it with message `0x15` (`PickupItem_RemoveGlint`, `0x003f1760`).
A pickable door makes one the same way ([World objects](objects.md)). That the jewellery in the store's cabinets
are `pickup_item`s is inferred.

**`sub_triglint`** (init `0x003eaf98`, update `0x003eb0d0`, message `0x003eb060`) draws nothing itself. It hides
itself (flags `+0x54` = 4), updates every 60 ticks and owns three `sub_glint`s. On each update it keeps them only while
its position is visible to a camera within 30 m (`Cameras_IsPointVisibleAny(30, position)`): it removes them when the
point is not visible and spawns them again when it is (`SubTriglint_SpawnGlints`, `0x003eade8`;
`SubTriglint_RemoveGlints`, `0x003eaf38`). Message `0x15` removes the glints and the triglint. The three glints sit
at the triglint's position plus fixed offsets (no rotation), with fixed sizes:

| Glint | Offset (m), `0x006f3160` (set by `0x003ee2a0`) | Size, `0x005146c0` | Drawn size (× 0.15) |
| --- | --- | --- | --- |
| 1 | (0, 0.043, 0.067) | 1.3 | 0.195 |
| 2 | (−0.071, −0.025, −0.027) | 0.75 | 0.1125 |
| 3 | (0.061, −0.032, −0.040) | 0.65 | 0.0975 |

**`sub_glint`** (init `0x003e9560`, update `0x003e96a0`, message `0x003e9780`): one sprite, sprite word `0x40029`
(batch 4, `part_page1`, rectangle 41), its size the given size × 0.15.

- **Unattached** (the triglint's): the first update comes after 30 ticks. Each update then toggles it, on for one
  interval and off for the next, and picks the next interval at random from 20 to 30 ticks (`Random_Int(10)` + 20:
  `0x003353b8` returns the raw number mod (n + 1), unsigned, so 0-10 inclusive, 11 values; the raw number is the next
  entry of the game's fixed 1,024-entry table `0x005117e0` on stream `0x006eb8b8`, the particles' stream,
  [Maths](maths.md); confirmed (code)). So each star blinks about once a second, out of step with the other two.
- **Attached** to an object (flags `0x80010`): it updates every 2 ticks and stays on.
- **On** means colour `+0xb4` = `0xffffff80` (white, alpha 128 on RenderWare's 0-255 scale, so half transparent) and
  size `+0xc0` = the drawn size. **Off** means alpha 0 and size 0.

## Coney's implementation {#coneys-implementation}

`effects::ParticleSystems` (`repo:src/effects/particles.h`) is the particle manager: a pool of 1,400 systems spawned
by name (`SpawnParticle`, `repo:src/scripting/effects_bindings.h`) with the position, rotation and parent above,
stepped on the fixed step with its own seeded generator, and drawn by `platform::ParticleRenderer`
(`repo:src/platform/particle_renderer.h`) as camera-facing squares cut from `part_page0`, `part_page1`, `part_fire`
and `lighting`. `effects::particleTypes()` (`repo:src/effects/particle_types.h`) lists the types Coney draws, with the
traced sprites of [Particle effects](../references/particles.md). Combat can call `spawnBlood` and `spawnSparks`; a
pane's [shatter](objects.md#shatter) makes its shards with `spawnShard`, and the objects' dust and bursts are
`sub_shack_puff`, through gameplay's object services (`repo:src/gamemodes/level_object_services.h`).

**Coney's stand-ins**, until the type code is traced: each type's motion is one of eight behaviours chosen by its name
(glow, flash, flame stream, puff, spray, sparks, shard, inert), with sizes, tints and lives of Coney's choosing; the
budget is 4,096 sprites; a name Coney does not draw, or the table lacks, makes an inert system that still answers to
its handle; an attached system keeps its spawn offset from its parent; glows, flashes, flames and sparks add to what
is behind them, the rest blend by alpha; a shard is an untextured quad; a shatter wants room for 158 sprites and a
camera near ([Objects](objects.md#coneys-implementation)). `StartParticle` and `EndParticle` switch a system's stream
on and off (sprites in flight live out their life; each type's own answer to messages `0x12` and `0x13` is not
traced), and show or hide a plain object.

**Explosions** (the molotov's `sub_explode`, `part_explosion`, `sub_fireball_emitter`, [Script
types](script-types.md)) follow the traced recipe: the flash's three stages (1, 8 and 40 ticks to sizes 0.2, 1.0 and
1.5, from white at alpha `0xf0` through `0xbf` to 0), the `part_explosion` it makes as its second stage begins, and
that explosion's six embers, six fireballs (seven stages of the table's ticks plus up to as many again, through the
table's colours from transparent black, 2.5 m/s outward) and 20 + 8 chips of debris. A sprite is drawn twice its size
wide. **Coney's stand-ins**: sizes and colours move linearly through each stage; the flash, fireballs and debris blend
by alpha and the embers add; an ember's random 0.6-1.5 is its size × 0.2 m; the embers' landing sparks, the
explosion's light and the debris lying flat on landing are not built.

**Steam vents** (`part_steam`, `part_steam_large`, `part_steam_huge`,
[`CfgSteam`](../references/bindings/config.md#cfgsteam)) follow [Steam vents](#steam): each starts with its init's
built-in values (`effects::defaultSteam`; the vents of a level's objects file get no `CfgSteam`), then while on one puff
every `interval` frames (60 a second) when the camera
is within 20 m, every 60 frames otherwise; a puff starts along the vent's −x axis at `speed` with z the `rise`
(× 0.8-1.2), its size × 0.8-1.2, and every `puffInterval` frames for round(life × 60) / puffInterval updates its
velocity is the start velocity less the drag's share of the life (× 0.75-1.15), its size grows by `growth`
× 0.8-1.2 and its alpha falls linearly to 0; it draws `part_page1` rectangle 42-44. **Coney's readings**: both near
tests are distances from the camera; the wind (`0x006f31a0`) is not traced, so no puff is blown; the particle budget's
short-alpha rule is left out.

**Glints** (`repo:src/effects/glints.h`, drawn by `PlayLevelMode` with the world objects) follow [Glints](#glints):
each step every `pickup_item` lying in the world (brought in round the camera, not in a hand, not hidden; not the
seven hobo foods that [never glint](script-types.md#pickup-item)) and every door whose lock-pick glint is up ([World
objects](objects.md#lock-pick)) has a triglint, removed with its glints when its owner goes; the triglint checks every
60 ticks whether a camera sees its position within 30 m (inside the view window) and drops or makes its three glints;
each glint toggles after 30 ticks and then every 20-30 ticks (`Random_Int(10)` from the game's table, read from the
disc's executable, on the triglints' own index), drawn while on at its offset, its size × 0.15, white at alpha 128,
`part_page1` rectangle 41. **Coney's stand-ins**: a new glint starts off; the particles' stream starts at index 0 when
the level starts and only the glints draw from it; the sprites are added to what is behind them, as Coney's other
flashes are.

**Light types** (`repo:src/effects/light_tasks.h`, lit by `GameplayMode` in the level's [light
manager](lighting.md#record)) follow [How a light type lights](script-types.md#light-type-lights): a point light with
no corona at its system, lighting objects and humans and, with flag `0x08`, the world, whose colour cross-fades from
the colour at the last update to the target over the update interval, a tick at a time. An alarm strobe
(`part_strobe_red`) makes a [strober](script-types.md#strober) when switched on (message `0x12`: red for 30 ticks,
then 10 ticks up to red, 10 down, 20 black) whose next update after `0x13` ends it, and an `alarm_emitter` ambient
loop of `alarmbell_loop` (mode 3, filter 2, 30 m), on while the strobe is. `part_orange_neon` and `part_pink_neon` place
their [neon light](script-types.md#neon-signs) at once (4 m, faded out over each second for its first 16 updates, then
steady for 16 updates of 30 ticks between off spells of 1 + `Random_Int(15)` steps a 15-tick blink). **Coney's
stand-ins**: the neon's draws come from the particles' generator, not the game's table; the sign's own task stays (it
ends after its first update in the original), holding the light.

**Fly piles** (`part_garbage_flies`, `part_garbage_flies_ns`, `part_light_bugs`) follow [Flies and
bugs](script-types.md#flies): 60 ticks after its spawn and then every 60 ticks while a view's frustum widened by 50 m
holds the pile, it keeps three flies, and otherwise drops them and looks again in 120 ticks. A fly is `part_page1`
rectangle 19, grey `0x808080ff`, 0.04 m across, faded and grown in from white at alpha 0 over its first update; it
jumps to `centre + 1.5 × (sin²a cos b, sin²a sin b, sin a cos a)` at each update, its angles stepping by 2π/10 and
2π/5, the next update in 20 ticks or, when `Random_Int(10)` is below 2, in 10. A `part_garbage_flies` also has a
`script_emitter` loop of sound `0x49d14d16` (mode 3, filter 0, 20 m). **Coney's readings**: `part_garbage_flies_ns`
is the pile itself without the sound (its child's emitter is skipped because it has a parent; inferred); the flies
are dropped at once rather than at their next update, and their draws come from the particles' generator.

## Open questions

- Whether the type functions were generated from the `Particle Source` / `Particle Asm Debug` chunk types (`0x19`-`0x21`,
  none of which is on the disc).
- What happens when `SpawnParticle` names a type the table does not have.
- The sprites of the 212 types with no constant sprite word, and what sheet records 2, 9, 27 and 409 are called.
- What `part_s_subway_sparks`, `subway_spark` and `urine_spray` really draw: rectangle 54 of `part_page1` does not look
  like either.
- Each type's update: the lives, speeds, counts, sizes and blending of its sprites (Coney's behaviours stand in).
- What `+0xc8`-`+0xd0` (1.0, 5.0, 0.3) scale in the particle draw, and what the negated sprite width does (the
  facing, blending and Z of a format-0 batch are in [Drifting fog](#fog)).
- `glasstest`'s sprite and fall, and which types the objects' dust (`0x003c57d8`) and bursts (`0x003c6038`) make.
- Which combat hits spawn which blood and spark types.
