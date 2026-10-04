# The streamed world

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims: PCSX2 was
not running when this page was written. The disc-side checks (2026-10-04) read the NTSC-U disc's WAD with throwaway
scripts outside the repository and are reported as names, counts and sizes only; those marked "Coney's disc test" come
from `coney_tests "[world]"` (`tests/platform/disc_world_test.cpp`), which anyone can run on their own disc.

## Purpose

A level's buildings, streets and props that never move are not in the level file. They are a **streamed world**: a
RenderWare BSP world that holds only the layout (sectors with bounding boxes and no triangles) and its textures, plus
the geometry of each sector as one RenderWare atomic, packed into numbered **parts** that the game reads from the disc
and throws away again as the camera moves. This page is the format of those files, how the game picks what to load
and unload, the memory it gives them, and how one frame draws them. How a level as a whole is loaded, and in which
order, is on [Level loading](level-loading.md).

Every level has two streamed worlds, drawn at different points of the frame: `<level>s`, the solid scenery, and
`<level>d`, the detail layer (graffiti, fences, road markings, vegetation), drawn after the level's objects. The page
calls them the **`s` world** and the **`d` world**.

## Original structure

`c:/Warriors/Source/World/ps2/WorldPS2.cpp` holds the world class (path string at `0x00588dc0`; source map
`0x004101f0`-`0x004124f8`), `World/ps2/WorldManagerPS2.cpp` the world manager that owns the two worlds
([Source map](source-map.md#world)). The two RenderWare plugins are registered from
`Graphics/Devices/Renderware/DevRWGeneric.cpp` ([Graphics](graphics.md#plugins)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00410308` | `World::World` | constructor of the 0x2188-byte world object | confirmed (code) |
| `0x00410e08` | `World_ReadManifest(world, name)` | reads `<name>_sec.mem` | confirmed (code) |
| `0x00410648` | `World_LoadStream(world, name)` | reads `<name>_sec.wld` into its own clump | confirmed (code) |
| `0x00410a50` | `World_ReadStream(world, rwStream)` | part count, texture dictionary, RenderWare world, callbacks | confirmed (code) |
| `0x00411010` | `World_RegisterSector` | per-sector callback at load: index the sector by its plugin data | confirmed (code) |
| `0x00412310` | `World_RequestPart(world, sector)` | starts the asynchronous read of the sector's part | confirmed (code) |
| `0x004114d8` | `World_PartLoaded` (file-manager callback) | wraps the read buffer in a RenderWare stream and calls `0x004110c0` | confirmed (code) |
| `0x004110c0` | `World_ReadPart(world, rwStream)` | reads one `<name>_ms<i>.sec` | confirmed (code) |
| `0x004115d0` | `World_UnloadPart(world, part)` | destroys a part's atomics, dictionary and clump | confirmed (code) |
| `0x004118b8` | `WorldSector_ReleaseAtomic` | one sector's atomic | confirmed (code) |
| `0x004101f0` | `World_CameraDistanceSq(bbox)` | squared distance from the nearest camera to a sector box | confirmed (code) |
| `0x00411eb0` | `World_FindPartToLoad` | nearest sector without its atomic | confirmed (code) |
| `0x004120a8` | `World_FindPartToUnload` | farthest part that nobody sees | confirmed (code) |
| `0x00411880` | `World_PendingDistance` | distance to the sector found by `0x00411eb0`; `FLT_MAX` when there is none | confirmed (code) |
| `0x0040e100` | `WorldManager_NearestPendingDistance` | the smaller of the two worlds' `World_PendingDistance`; no new search | confirmed (code) |
| `0x00426c78` | `Atomic_AssignGamePipelines` | the game's PS2 pipelines for an atomic and its materials ([Pipelines](#pipelines)) | confirmed (code) |
| `0x0017d640` | `LightManager` constructor | the light manager (`0x0050cce4`, 0xc0 bytes) ([Lighting](#lighting)) | confirmed (code) |
| `0x0017de10` | `LightManager_SelectLights` | the lights for one atomic or sphere | confirmed (code) |
| `0x00124778` | `Cam_Follow::Cam_Follow` | the player camera: field of view, near and far clip ([The player camera](#player-camera)) | confirmed (code) |
| `0x00411d10` | `World_FindVisibleSectors(world, firstViewport)` | visibility pass for one camera | confirmed (code) |
| `0x00411b98` | sector callback during the visibility pass | PVS, occluders, mark visible | confirmed (code) |
| `0x00411b20` | `World_CollectSector` (the world's sector render callback) | queues a sector whose atomic is loaded | confirmed (code) |
| `0x00411990` | `World_RenderSectorAtomic(atomic, fadeEnd)` | fade-in, lights, default atomic render | confirmed (code) |
| `0x004123e8` | `World_ResetVisibility` | clears the visible bits, sets the PVS view point | confirmed (code); file inferred |
| `0x00410b70` | `World_Unload` | every part, the world, its dictionary and clump | confirmed (code) |
| `0x0040f8a0` | `WorldManager_Update` | the streaming decision, once a frame | confirmed (code); file inferred |
| `0x0040e8d8` | `WorldManager_Render(viewport)` | the world part of a viewport's frame | confirmed (code) |
| `0x00198e20` | sector plugin `0x3F1` stream reader | 20 bytes into the sector's plugin data | confirmed (code) |
| `0x00192688` | atomic plugin `0x3F0` stream reader | 12 bytes into the atomic's plugin data | confirmed (code) |

RenderWare function names (`RpWorldRender`, `RpWorldSetSectorRenderCallBack`, `RpPVSHook` and so on) are inferred from
their arguments and their place in the RenderWare block, as on [Graphics](graphics.md#original-structure).

## Data

All values little-endian. File names below are the WAD names; how a level's world name is chosen is on
[Level loading](level-loading.md#the-level-record).

### File names {#file-names}

| File | Format | Evidence |
| --- | --- | --- |
| `<world>_sec.mem` | the [manifest](#manifest) | confirmed (code) at `0x00410e08` (string `0x00588e00`) |
| `<world>_sec.wld` | the [world stream](#world-stream) | confirmed (code) at `0x00410648` (string `0x00588db0`) |
| `<world>_ms<i>.sec`, `i` = 1, 2, ... | [part](#part-file) `i` | confirmed (code) at `0x00412310` (string `0x00588df0`, `"%s_ms%i.sec"`) |

`<world>` is `<level>s` and `<level>d` when `<level>s_sec.wld` exists, and `<level>` otherwise
([Level loading](level-loading.md#worldmanager-loadlevel)).

**Disc check (corroboration), all 2,229 streamed-world entries now named:** the WAD holds 159 worlds: 79 levels with
an `s` and a `d` world, and `objarena` with a single world. Each has a `_sec.wld` and a `_sec.mem`, and parts
numbered from 1 with no gaps: 1,911 part files. So the 2,229 entries the [WAD survey](formats/wad-contents.md#names)
could not name are `159 + 159 + 1,911`.

### Manifest (`_sec.mem`) {#manifest}

Read by `0x00410e08` into the world object, confirmed (code):

```c
struct WorldManifest {
    uint32_t worldSize;       // bytes of <world>_sec.wld        -> world +0x216c
    uint32_t worldHeapSize;   // heap to create for the world    -> world +0x2170
    uint32_t partCount;       // n                               -> world +0x2164
    struct {
        uint32_t fileSize;    // bytes of <world>_ms<i>.sec      -> part record i, +0x10
        uint32_t heapSize;    // heap to create for part i       -> part record i, +0x14
    } parts[partCount];       // i = 1 .. n
};
```

**Disc check (corroboration):** in all 159 manifests `worldSize` is the world stream's size, `partCount` equals the
world stream's own count, and every `fileSize` is its part's size. `worldHeapSize` is 1.009 to 1.575 times
`worldSize`; `heapSize` is 0.988 to 1.518 times `fileSize`.

Without a manifest the loader measures the files instead (confirmed (code) at `0x00410648`): world heap = 135 % of
the file size, at least 716,800 bytes (`0xaf000`); part heap = 130 % of the file size, at least 256 KB. Every world on
the disc has a manifest, so this path is never taken on the retail disc (inferred).

### World stream (`_sec.wld`) {#world-stream}

Read by `0x00410a50`, confirmed (code):

```text
u32 partCount                          the same n as the manifest; stored at world +0x2164
RenderWare texture dictionary (0x16)   every world texture; made current, then current = none
RenderWare world (0x0B)                the BSP: plane sectors and atomic sectors
```

The world itself is ordinary RenderWare 3.7 (`0x1C02000A`), PS2 native (`0x510` native data on each atomic sector),
except that **no atomic sector holds triangles**: the sectors are only boxes in the BSP, each carrying the game's
[sector plugin](#sector-plugin). **Disc check (corroboration):** all 159 worlds have zero triangles and vertices in
the world header and in every one of their 16,074 atomic sectors. World format flags: `0x410200b9` (two texture
coordinate sets) in 74 `s` worlds and 12 `d` worlds, `0x4101003d` (one set) in 5 `s` worlds, 67 `d` worlds and
`objarena`. 18 worlds carry RenderWare PVS data (3,304 extension sections of plugin `0x12A`).

After reading, the loader (confirmed (code) at `0x00410a50`, `0x00410648`):

1. Records whether the world has PVS data (`0x004bef68`) at `+0x2160`.
2. Sets the world's sector render callback to `World_CollectSector` (`0x00411b20`).
3. Walks every world sector with `World_RegisterSector` (`0x00411010`), below.
4. Checks every atomic's textures with `0x00198190` (the "Potential Crash from Missing Texture" check of
   [Graphics](graphics.md#loading-textures)).
5. Sets the RpWorld's word `+0x0c` to 2. In RenderWare 3.7 that word is the world's render order and 2 is
   back-to-front (inferred from the RenderWare layout).

### Sector plugin `0x3F1` {#sector-plugin}

Every atomic sector of a streamed world carries 20 bytes of extension data under plugin id `0x3F1`. The plugin gives
each world sector 32 bytes (offset in `0x0050ced0`). Stream reader `0x00198e20`, setters `0x00198ff0`-`0x00199068`,
confirmed (code):

| Plugin offset | Stream bytes | Meaning | Evidence |
| --- | --- | --- | --- |
| `+0x00` | | the sector's atomic once its part is loaded, else null | confirmed (code) at `0x004110c0`, `0x004118b8` |
| `+0x04` | 0-3 | **streamed-sector index** `k`: the sector's slot in the world's sector table; `0xffffffff` = the sector has no atomic | confirmed (code) at `0x00411010` |
| `+0x08` | | state: 2 = atomic loaded, 3 = release pending, 4 = released | confirmed (code) at `0x004110c0`, `0x004118b8` |
| `+0x0c` | 4-7 | **part number** `i`: the atomic is in `<world>_ms<i>.sec`; 0 for a sector without an atomic | confirmed (code) at `0x00411010`, `0x00412310` |
| `+0x10` | | end of the fade-in: real-time milliseconds when the atomic was read, plus 1,000 | confirmed (code) at `0x004110c0` |
| `+0x14` | 8-19 | three floats: **where the atomic's frame is placed** (its translation, RenderWare axes) | confirmed (code) at `0x004110c0` |

**Disc check (corroboration):** 5,315 of the 16,074 sectors have an index; the indices of a world run 0 to `k - 1`
with no gap or repeat. Exactly the sectors without an index have part 0 and a zero position (10,759). Part numbers
run 1 to `partCount`. 4,810 of the 5,315 positions lie inside their sector's box, 4,891 within 5 % of its centre.

### Part file (`_ms<i>.sec`) {#part-file}

Read by `0x004110c0`, confirmed (code):

```text
16 bytes   header {u32 1, u32 0, u32 0, u32 nameHash}; read and ignored
RenderWare texture dictionary (0x16)   made current, then current = none; kept in the part record (+0x04)
u32 count
count times:
    u32 sectorIndex                    the streamed-sector index k (sector plugin +0x04)
    RenderWare atomic (0x14)           a standalone atomic with its geometry
```

`nameHash` is the CRC-32 of the file's own name, `<world>_ms<i>.sec`, not lowercased and without `./ee_files/`
(disc check: 1,911 of 1,911). The loader does not read it.

For each atomic the loader (confirmed (code)):

1. Creates a frame (`0x004841e8`), translates it to the sector plugin's position, and gives it to the atomic.
2. Stores the atomic in sector `k` (`+0x00`), sets the state to 2 and the fade end to now + 1,000 ms.
3. Saves the atomic's render callback in `0x005147e8` and replaces it with a do-nothing callback (`0x00411b18` returns
   its argument). RenderWare never draws these atomics itself; the world manager does ([A frame](#a-frame)).
4. Sets geometry flag `0x40` (modulate material colour, used for the fade) (`0x004102f0`).
5. Checks the materials' textures (`0x00198190`) and runs `0x00426c78`, which stamps the atomic with `0x005151c4` and
   walks its materials (meaning not traced).

**Disc check (corroboration):** every part's atomics belong to sectors whose part number is that part: in all 159
worlds each indexed sector gets exactly one atomic from exactly one part (5,315 atomics in the 1,564 parts numbered
`1..n`, no mismatch). 1,153 of those parts have a non-empty texture dictionary (10,776 textures in all); the world
streams hold 11,664. All atomics read with RenderWare's PS2 native geometry (`0x510`) and the game's own
right-to-render pipeline ids (plugin 3, extra data `0x30082`/`0x30083` on atomics, `0x30084`, `0x30086`, `0x30088` on
materials).

**Native data struct size:** inside each part atomic's native data section (`0x510`), the struct section's header
gives a size larger than the `0x510` section that holds it (for example 878 bytes in a 422-byte section). RenderWare's
reader, like librw's, skips that header without using the size; Coney does the same. Why it is larger is not known
(Coney's disc test reads every atomic this way).

**347 part files are never loaded:** parts numbered above `partCount` (31,046,766 bytes). The loader only asks for
parts that sectors name, and no sector names them; they look like leftovers of earlier builds (inferred).

### Atomic plugin `0x3F0` {#atomic-plugin}

Every atomic gets 16 bytes (offset in `0x0050cd98`; registered by `0x001927d8`); 12 of them are streamed, confirmed
(code) at `0x00192618` (defaults), `0x00192688` (reader), `0x00192740` (writer):

| Offset | Stream | Default | Meaning | Evidence |
| --- | --- | --- | --- | --- |
| `+0x00` | float | 1.0 | uploaded to VU memory by the game's four custom PS2 pipelines (`0x004252c8`, `0x00426430`, `0x004275d0`, `0x00428410`); **the scale of the packed 16-bit vertex positions** ([PS2 world geometry](#ps2-world-geometry)) | upload confirmed (code); scale confirmed (runtime) by Coney's disc test; that the microcode applies it this way inferred |
| `+0x04` | float | 1.0 | uploaded next to `+0x00` in the same quadword (`+0x00` as `x`, `+0x04` as `z`; `0x001928c0`, `0x001928f0`); **the scale of the packed 16-bit texture coordinates** | upload confirmed (code); scale confirmed (runtime) by Coney's viewer, visually; the microcode was not read |
| `+0x08` | u32 | 0 | read by `0x004290d8` | confirmed (code); meaning unknown |
| `+0x0c` | | 0 | not streamed: the game object that owns the atomic (getter `0x00192870`, used by the object renderers) | confirmed (code); meaning inferred |

**Disc check (corroboration):** in the streamed worlds' atomics the two floats are always powers of two from 2⁻⁸ to
2⁻¹⁵ (for example 2⁻¹⁰ and 2⁻¹¹), and `+0x08` is always 0.

**Coney's disc test, confirmed (runtime) for the disc data:** with each atomic's packed positions multiplied by
`+0x00` and placed at its sector's origin, all 7,563,801 vertices of the 5,315 atomics lie in their sector's box
(1 % margin), and for **all 5,315 atomics the vertices' own bounds are the sector's box**, every face within 1 % of the
box's largest side. Unscaled, 136 vertices lie in the box. In the 4,708 atomics whose two floats differ, scaling by
`+0x04` never gives the box (0 of 4,708; the floats are equal in the other 607). So `+0x00` is the position scale.
The atomic's own bounding sphere in the geometry header is in the same scaled units (seen on one atomic). `+0x04` is
the texture-coordinate scale (Coney's viewer, [below](#coneys-implementation); scaled by it, second-set coordinates
fall in 0 to 1 for 93 % of `level2s`'s vertices, first-set ones show tiling values).

### PS2 world geometry {#ps2-world-geometry}

Every part atomic's geometry is RenderWare PS2 native geometry: the mesh plugin (`0x50E`) gives each mesh's vertex
count and material, and the native data (`0x510`) holds, per mesh, a DMA chain for vector unit 1 (VU1). The chains are
built for the game's own pipelines, right to render plugin 3 with data `0x30083` (5,314 atomics) or `0x30082`
(1 atomic), and their layout is not RenderWare's default one. From Coney's disc test (confirmed (runtime) for the disc
data: all 5,315 atomics, 181,150 batches decode this way; what the microcode does with it is inferred):

- **Tags:** reference tags whose address counts 16-byte units from the chain's start (the vertex data sits after the
  chain's `ret` tag), and `cnt`/`ret` tags whose upper two words carry VIF commands.
- **Batches:** `STCYCL 4, 1`, then up to four `UNPACK`s to VU addresses 0 to 3, interleaved four quadwords a vertex:
  position `V4_16` (signed: x, y, z and an unused fourth word), texture coordinates `V4_16` (two sets) or `V2_16`
  (one set), prelighting colour `V4_8` unsigned, normal `V4_8` signed. Then `ITOP n` and a microprogram start
  (`MSCALF` for the first batch, `MSCNT` after); `FLUSH` at the end.
- **ITOP is the batch's vertex count.** Each `UNPACK` may write a few more vectors than that, as padding: positions to
  an even count, the byte formats to whole quadwords. With ITOP the batches add up to every mesh's count in the mesh
  plugin (all meshes).
- **Strips across batches:** the meshes are triangle strips, and every batch after the first starts with the last two
  vertices of the batch before (all batches on the disc).
- **Scale:** positions are the 16-bit integers times the atomic plugin's `+0x00`, in the atomic's frame, which the
  loader places at the sector plugin's origin ([Atomic plugin 0x3F0](#atomic-plugin)).

Triangles: decoded and joined, the strips give 2,869,406 non-degenerate triangles against 2,870,179 in the geometry
headers (5,155 of 5,315 atomics equal), and 4,887,946 distinct vertices against 4,943,282. The difference is inferred
to be vertices that became identical when packed to 16 bits, which merges them and turns their triangles degenerate.

### Pipelines and the second texture-coordinate set {#pipelines}

The pipeline ids in the files are not the whole story: when a part's atomic (and the level file's sky, cloud and
skyline models) is set up, `Atomic_AssignGamePipelines` (`0x00426c78`) gives the atomic the pipeline `0x30083`
(`0x005151c4`, made by `0x00426d58`) and chooses each material's pipeline from its **MatFX effect** (`0x00426cc8`,
reading the effect type through `0x004653d8`), confirmed (code):

| Material's MatFX effect | Pipeline | Made by | Texture coordinates unpacked as | Microcode |
| --- | --- | --- | --- | --- |
| none, geometry without flag `0x80` | `0x30084` (`0x005151b4`) | `0x00428f30` | `V2_16`, one set (`0x6500000d`) | table `0x005045a0` |
| none, geometry with flag `0x80` (two sets) | `0x30088` (`0x005151b8`) | `0x004298c0` | `V4_16`, two sets (`0x6d00000d`) | the same table `0x005045a0` |
| 4, dual texture | `0x30086` (`0x005151bc`) | `0x004273f8` | | `0x004fc870` |
| 2, environment map | `0x30087` (`0x005151c0`) | `0x00428080` | | `0x004ff1c0` |
| any other | unchanged | | | |

The set-up of these pipelines is `0x00426e28` / `0x00426c40`. So `0x30084` and `0x30088` run the **same microcode**
and differ only in unpacking one or two texture-coordinate sets: on a plain material the second set is unpacked and,
inferred from the shared microcode, not used. `0x30086` is the dual-texture pipeline with its own microcode.

**Disc check (corroboration):** in the streamed worlds' part files, 9,516 materials carry MatFX effect 4 (dual), all
with the blend `SRCALPHA` / `INVSRCALPHA` and a second texture; 2,467 carry effect 1 (bump map), which keeps its stored
pipeline. So the **second texture-coordinate set** is, inferred, the coordinates of a dual material's second texture,
alpha-blended over the first (decals, grime, painted markings); the `0x3F0 +0x04` scale presumably applies to it too.
How the microcode at `0x004fc870` draws the second pass was not read.

**Vertex colour range**, confirmed (code) at the uploads `0x004252c8` and `0x00426430`: the **material colour** is
scaled by `1/255` for an untextured material and by `0.0019700117` (about `0.5/255`) for a textured one; alpha
always by `0.00197`. A textured white material thus reaches the GS as 128, and the GS's texture modulate treats 128 as
1.0 (inferred from the GS's documented behaviour). The prelighting colours are unpacked unsigned (`V4_8`) and not
scaled by the CPU, so they too are on the GS's scale, where 0x80 is full brightness (inferred).

### The world object (0x2188 bytes) {#world-object}

Built by `0x00410308`, confirmed (code) for the offsets; the names are ours.

| Offset | Meaning |
| --- | --- |
| `+0x0000` | part being read, -1 when none (set by `0x00412310`, cleared by `0x004114d8`) |
| `+0x0004` | -1, set by the constructor and again when a part's unload completes (`0x004115d0`); the world manager treats `>= 0` as busy, but no other value is ever written (inferred, see [Open questions](#open-questions)) |
| `+0x0008` | pool the part clumps are created in (the `Sector Pool`) |
| `+0x000c` | sector whose part is being read |
| `+0x0010` | set when the last search fell back to sectors nobody sees |
| `+0x0014` | sector found by the last search (`0x00411eb0`), read by `0x00411880` |
| `+0x0018` | 32 bytes cleared at each first-viewport visibility pass; use not traced |
| `+0x0038` | sector table: 560 pointers, indexed by the streamed-sector index |
| `+0x08f8` | visible bits: 18 words, 576 bits, by streamed-sector index |
| `+0x0940` | per part: a vector of its sectors (140 entries of 16 bytes, part number as index) |
| `+0x1200` | per part: a 0x1c-byte record (140 entries, part number as index): `+0x04` texture dictionary, `+0x08` parent pool, `+0x0c` part clump, `+0x10` file size, `+0x14` heap size (after a load: the bytes the part really used) |
| `+0x2150` | result of the PVS view-point update |
| `+0x2154` | the world's clump |
| `+0x2158` | the world's texture dictionary |
| `+0x215c` | the `RpWorld` |
| `+0x2160` | world has PVS data |
| `+0x2164` | part count `n` |
| `+0x2168` | streamed-sector count (highest index + 1) |
| `+0x216c`, `+0x2170` | world stream size and heap size (manifest) |
| `+0x2174` | the world's name, 19 characters and a NUL |

The fixed tables limit a world to 560 streamed sectors and 139 parts. **`level70s` has 593 streamed sectors**: its
indices 560 to 592 would be stored over the visible bits and the first part vectors. Whether level 70 is ever played
(it has no `level70.lev`, see [Level loading](level-loading.md#the-level-record)) is open. Coney should size these
tables from the data.

## Behaviour

### Loading a world {#loading-a-world}

`World_LoadStream` (`0x00410648`), after `World_ReadManifest`, confirmed (code):

1. If no manifest was read, take the sizes from the files ([above](#manifest)).
2. Ask the resource manager for room: `0x00187d28(resourceManager, worldHeapSize, 1)` evicts the least recently used
   resources that nothing holds until the `Sector Pool`'s largest free block is big enough (confirmed (code) for
   the loop; "least recently used" inferred from the time stamps it compares). If it cannot make room, the world is
   not loaded.
3. Create a clump ([Memory](memory.md#clump-behaviour)) of `worldHeapSize` bytes named after the world in the
   `Sector Pool` (`0x006eb9c8`), make it current,
   and read the whole world stream synchronously through a RenderWare stream over the file ([World stream](#world-stream)).
4. With no manifest, also measure every part file.

### Choosing what to stream {#streaming}

Once a frame, while a level runs, the world manager's update (`0x0040f8a0`) makes at most one streaming decision. It
weighs the two worlds' parts against two other streamers, the resource manager (level props and characters) and the
water effect (`Graphics/WaterEffect.cpp`, world manager `+0x5c`). Confirmed (code) for the steps; the arbitration is
summarised:

1. **Busy?** If either world is reading a part (`+0x00 >= 0`) or has `+0x04 >= 0`, or the file manager has a read in
   flight, do nothing (return 2).
2. **What is wanted.** In each world, `World_FindPartToLoad` (`0x00411eb0`) looks for the nearest sector that has an
   index and no loaded atomic: first among the sectors marked visible in the last frame, and, if none of those is
   within reach, among all of them (then `+0x10` is set). "Nearest" is
   [the camera distance](#camera-distance); "within reach" compares that *squared* distance with the camera's draw
   distance, which is not squared (confirmed (code); whether that is intended is not known). The resource manager
   offers its own nearest wanted resource (`0x00189750`, scaled by `0x005147cc` = 0.001, set by `LoadLevel`), the water
   effect its own.
3. **Urgency.** `0x005147c8` = 1 when the nearest wanted thing is closer than 75.0 (read elsewhere; not traced).
4. **Load the nearest.** If the nearest is a world sector, `World_RequestPart` (`0x00412310`) asks the resource
   manager for room for the part's heap (`0x00187d28(rm, heapSize, 0)`); if it gets it, it records the part as being
   read and queues an asynchronous read of `<world>_ms<i>.sec` with `World_PartLoaded` as the callback (return 2).
   Otherwise the resource manager or the water effect starts its read.
5. **Make room.** When nothing could be started, `World_FindPartToUnload` (`0x004120a8`) offers, in each world, the
   part whose nearest sector is farthest from the cameras, among parts that are loaded and of which no sector was
   visible last frame and no atomic is still in use (`0x00198220`). The resource manager offers its farthest
   resource. The farthest of these is unloaded, but only if it is more than 5.0 farther than the nearest wanted
   thing (return 2). If there is nothing to unload, return 1 or 3. The two world searches give **squared** camera
   distances, so for scenery the 5.0 margin is added to a squared distance (`unload > load + 5.0`, both squared;
   confirmed (code) at `0x0040f8a0`); the resource manager's values (`0x00189750`, scaled by 0.001) are compared on
   the same scale, with a `- 5.0` on its own path.

`World_FindPartToUnload` looks at parts 1 to `n - 1` only: the last part of a world is never unloaded (confirmed
(code); an off-by-one in the original, inferred).

When the read finishes, the file manager calls `World_PartLoaded` (`0x004114d8`), which wraps the read buffer in a
RenderWare memory stream and reads the part into a new clump, `Sectors<i>`, of the part's heap size, created in the
`Sector Pool` (`0x004110c0`, [Part file](#part-file)). Afterwards the part record's heap size is replaced by what the
part really used. Then `+0x00` = -1.

**Unloading a part** (`0x004115d0`): for each sector of the part, `0x004118b8` sets the state to 3 and, unless the
atomic is still in use, detaches and destroys the atomic and its frame and sets the state to 4. If every sector
reached 4, the part's texture dictionary is destroyed, world `+0x04` is set to -1, and the part's clump is checked
empty, unregistered and given back to the `Sector Pool`. Otherwise the unload is retried on a later frame. While the
atomics are destroyed the clump counts frees, the only place where a clump's individual frees matter
([Memory](memory.md#clump-behaviour)); confirmed (code).

### Camera distance {#camera-distance}

`0x004101f0`, confirmed (code). For each player camera (count `0x0011eae0`, camera `i` from `0x0011f9b0`), its
position `p` in game axes becomes RenderWare axes as `q = (p.x, p.z, -p.y)`. For a sector box with corners `lo` and
`hi`:

```text
d2 = sum over the three axes a of  min((lo[a] - q[a])^2, (hi[a] - q[a])^2)
result = the smallest d2 over all cameras
```

This is a distance to the nearest face *planes*, not to the box: a camera inside a box still gets a positive value.
`World_PendingDistance` (`0x00411880`) recomputes this for the sector found by the last search (world `+0x14`),
against the current cameras, and returns its square root; with no sector found (`+0x14` null) it returns `FLT_MAX`
(3.4028235e38), not infinity. `0x0040e100` returns the smaller of the two worlds' values and does **not** search
again (confirmed (code)). So in the draw-distance step of [A frame](#a-frame), a fully loaded level gives `FLT_MAX`
and the draw distance grows to its ceiling.

### Preloading {#preloading}

When a level starts, `InitLevel` preloads the world around the camera before the first frame
([Level loading](level-loading.md#preload)): it runs the update above in a loop until it stops reporting work, the
time budget runs out or the nearest missing sector is beyond the camera's draw distance.

### A frame {#a-frame}

`WorldManager_Render(viewport)` (`0x0040e8d8`) is the "world" step of [a viewport's frame](graphics.md#a-frame).
Confirmed (code), in order. Render states are RenderWare's: `0x14` cull mode (1 none, 2 back faces), `6` Z test, `8`
Z write, `0x0e` fog. The world toggles `0x005e5380`-`0x005e5398` are all 1 (set by the static initialiser `0x00155a90`).

1. Nothing at all is drawn while a full-screen fade covers the screen (`0x005fdeb8 + 0x1d8 >= 1` and `+0x1d4` set).
2. Viewport camera; the level's occluders are set up for it (`0x0017a610`).
3. **Draw distance.** When `0x0050c698` is set (it is, in `.data`) and the byte `W_GameState + 0x410` is 0 (meaning
   not traced), the camera's
   draw distance moves each frame by at most `dt` (real time, capped at 100 ms):
    - frame rate (`0x0050c680`) above 29.5 (24.5 with device flag `0x02`): toward the [pending
      distance](#camera-distance) of the nearest missing sector of either world, shrinking by up to `40.5 dt` and
      growing by up to `10.5 dt`, never above 300;
    - frame rate below it: shrink by `(threshold - rate) × 10.5 dt`;
    - never below `60 - 10 × viewports` and never above the camera's own far clip (115 for the player camera,
      [The player camera](#player-camera)).

    So the view closes in on scenery that is not loaded yet rather than showing holes.
4. Far clip = draw distance; fog distance = draw distance × the device's fog start.
5. **The level world** (the light glows in the `.lev`, [Level loading](level-loading.md#the-level-object)): culling off,
   Z test, Z write and fog on, rendered with `RpWorldRender`.
6. **The `s` world:** add the camera to it; culling = `0x0050c69c` (2, back faces); the visibility pass
   ([below](#visibility)); then draw the collected sectors' atomics, last collected first, each with
   `World_RenderSectorAtomic` (`0x00411990`).
7. Fog distance halved: the objects the resource manager queued in its list `+0xc78` (`0x00174320`; what they are is
   not traced). Fog restored: the first pass of list `+0xc98` (`0x00172c70`) and the object instances of list
   `+0xc88` (`0x0017fd78`) except those of type `0x20`.
8. **The `d` world:** the same as the `s` world.
9. The water effect (`0x00191dd8`, a phase that grows by 0.16 a frame).
10. With Z write off: the instances of type `0x20`, then the second pass of list `+0xc98`. Then the remaining
    effects (`0x00419da0`, ground fog `0x001712c0`, garbage `0x00171f58`, `0x001795f8`).

**`World_RenderSectorAtomic`** (`0x00411990`): during the second after the atomic was read, set the alpha of every
material colour to `255 × (1 - (fadeEnd - now) / 1000)`, afterwards to 255 (only when it changes); light the atomic
from the `LightManager` with its world bounding sphere (`0x0017de10`, `0x0017e810`); then call the atomic's original
render callback (RenderWare's default, `0x00469ef8`, when none was saved). New scenery therefore **fades in over one
second**.

### Visibility {#visibility}

`World_FindVisibleSectors` (`0x00411d10`), confirmed (code):

1. For the first viewport, reset: clear the visible bits and the 32 bytes at `+0x18`, and, if the world has PVS data,
   set the PVS view point to player 1's camera position (`0x004123e8`).
2. Install `0x00411b98` as the sector render callback and call `RpWorldRender`: RenderWare walks the BSP and calls the
   callback for each sector inside the camera's frustum (no geometry is drawn: the sectors have none).
3. With PVS data and PVS on (`0x005e539c`, set to 1 at start-up), the walk runs with the PVS hooked in
   (`0x004beff8`, `0x004beec0`, `0x004befa0`). If that leaves no sector at all, the pass is run again without PVS.
4. The callback, for a sector with an index: skip it if the PVS says it is hidden; skip it if one of the level's
   occluders hides its box (`0x0017a738`); otherwise mark it visible in `+0x08f8` and call the world's own sector
   callback, `World_CollectSector` (`0x00411b20`), which appends the sector to the world manager's draw list
   (`+0x50`-`+0x58`) when its atomic is loaded.

Because the world's render order is back-to-front and the list is drawn from its end, sectors are drawn roughly
front to back (inferred).

### Lighting {#lighting}

The `LightManager` (constructor `0x0017d640`, 0xc0 bytes, pointer `0x0050cce4`) owns the lights of a level; the level
script adds lights, the world uses some of them. Confirmed (code) unless stated.

| Offset | What |
| --- | --- |
| `+0x40` | ambient light A: flags 2 (world), enabled; its colour is set each viewport from `+0x50` |
| `+0x44` | ambient light B: flags 1 (objects), disabled by default; colour a triangle wave between `+0x60` (black) and `+0x70` (0.25 grey) with period `+0x80` = 300 ms (what enables it is not traced) |
| `+0x50` | the world ambient colour |
| `+0x84` | a point light, radius 0.4, white, flags 1: a character's glow, used when the character's byte `+0x647` is above 10 |
| `+0x90` | the gamma offset (RGB) |
| `+0xa0` | a constant offset, 40/255 = 0.157 (set by the constructor through `0x0017ec38`) |

A light's flags say what it lights: bit 0 (1) objects, bit 1 (2) the world.

**Colour.** `0x0017c840` adds `+0x90 + +0xa0` to the colour of every ambient and directional light, clamped below at 0
(`0x0017ecc8`). So with no script call, the world's ambient is **0.157 grey**. The Lua bindings, confirmed (code):

- `SetWorldAmbient(r, g, b)` (`0x0036e4b8` → `0x0017f218`): `+0x50 = (r, g, b) + 0.07`, so the world ambient becomes
  `rgb + 0.07 + 0.157`. Used in 3 script files of 2 levels.
- `SetGammaOffset({r, g, b})` (`0x0037bd30` → `0x001b4908` → `0x0017ec80`): `+0x90`. Used in 1 file.
- `SetLight(handle, type, pos, dir, colour, radius, a7…a10, flags, a12, flicker, state)` (`0x0037bfb8`, `0x0037c348`,
  through `0x0017ef20`): type 0 point, 1 spot, 2 directional, 3 ambient; `flags` up to 3; `flicker` up to 6
  (`SetLightFlicker` sets it alone); state 0 off, 1 on, 2 delete. Used in 60 files of 52 levels; `SetLightFlicker`
  in 9 levels. The light descriptor `0x0017c508` (RenderWare type at `+0x00`: 0x80 point, 0x81 spot, 1 directional,
  2 ambient; position `+0x04`, direction `+0x10`, colour `+0x20`, radius `+0x30`, flags `+0x44`, flicker `+0x4c`,
  enabled `+0x4e`).

(File counts from a scan of the disc's compiled Lua files for the binding names; corroboration.)

**Per viewport** (`LightManager_BeginViewport`, `0x0017ea60`, and `0x0017d880`): ambient A takes its colour, and the
lights are sorted into lists: list A (`0x00715324`) holds the enabled ambient and directional lights with flag 2; lists
B and C those with flag 1; point and spot lights are culled by distance from the camera (`far × 0.75 + radius`) and by
the frustum planes into the viewport's lists `+0x20` (all) and `+0x30` (flag 2).

**Per atomic** (`LightManager_SelectLights`, `0x0017de10(…, sphere, flags, …)`, then the upload `0x0017e810`): for the
world (flags bit 0 clear), list A, up to 8 lights, then the point lights of `+0x30` whose sphere meets the atomic's,
keeping the nearest when full; objects use lists B or C, with `6 - LOD` lights. Streamed sectors are lit this way with
their world bounding sphere (`World_RenderSectorAtomic`), and the background with a sphere far away and no point
lights ([Level loading](level-loading.md#render-order)).

So the streamed world's light is the prelighting (on the GS's 0x80 scale) modulated by the lighting the microcode
computes from the 0.157 ambient, the script's world ambient and the script's world lights (how the microcode combines
prelighting and lights was not read).

### The player camera {#player-camera}

Confirmed (code) at the constructors and `0x00120a98`:

| Field | `Cam_ICamera` base (`0x00120868`) | `Cam_Follow` (`0x00124778`, vtable `0x00535d50`), the player camera | Getter slot |
| --- | --- | --- | --- |
| `+0x44` field of view, degrees | 60 | **65** | `0x1f4` |
| `+0x50` near clip | 0.3 | **0.1** | `0x204` |
| `+0x54` the camera's own far clip | 60 | **115.0** (`0x005d91c0`, set by `Cam_Follow`'s static initialiser `0x00134ab0`) | `0x20c` |
| `+0x58` draw distance | 60 | set by `0x00122128` (which also sets the main camera's far clip when `0x0050c698` is set) | `0x214` |
| `+0x5c`, `+0x60` view window | | from the field of view, below | `0x254` |

**View window** (`0x00120a98`): `half = (fov + 0x0050b178) × 0.5` degrees (`0x0050b178` is 0); `+0x5c = tan(half) ×
0.75 × aspect` with aspect `+0x4c` = `0x0050b204` = 4/3, so just `tan(half)`; `+0x60 = tan(half) × 0.75`. For the
player camera that is **(0.637, 0.478)**: a 65° horizontal field of view on a 4:3 picture. Split screen halves the
values. (`0x004b8d30` is inferred to be `tanf`.)

Each frame `0x001562c8` hands the view window, the near clip (`+0x50`) and the **draw distance** (`+0x58`) as the far
clip to device slot `+0x28`; fog starts at far clip × the device's fog start. In `WorldManager_Render` the draw
distance's ceiling is the camera's own far clip (`+0x54`, slot `0x20c`): **115** for the player camera.

### Fog and background colour {#fog}

One colour is both: device `+0x440`, set through device slot `+0x48` ([Graphics](graphics.md#device-object)). It is
white from start-up (`GraphicsDevice_Open`) and black after `UnloadLevel`; a level sets it from its script, confirmed
(code):

- `SetFogColor(r, g, b)` (`0x0036e558` → `Level_SetFogColour`, `0x0040c868`): floats in 0 to 1, times 255, alpha 255.
- `SetFogDistance(d)` (`0x0036e5f8` → `0x0040c908`): the device's fog start `+0x444` (0.5 by default), the fraction
  of the far clip where fog begins.

**Disc check (corroboration):** `SetFogColor` appears in 66 compiled Lua files, covering 62 of the 64 levels that have
a `.lev`; `SetFogDistance` in 14 levels. The colour values themselves are inside Lua bytecode and were not decoded.

## Coney's implementation

Reading, decoding, streaming and drawing exist (2026-10-04): `coney --view-world <level>` streams a level's two worlds
around a free-flying camera ([Building](../guides/building.md#the-world-viewer)). The level file, the level world,
objects, PVS and occluders do not.

**Reading and decoding** (2026-10-04, unchanged):

- **`src/world/world_streams.h`** reads the layout of a world stream (`inspectWorldStream`: part count, dictionary,
  every atomic sector with its box and `0x3F1` data) and of a part file (`inspectPartFile`), and checks each atomic
  section for librw (`inspectAtomicSection`). **librw has no RpWorld stream reader at all**, so the BSP is walked here;
  the sectors hold no geometry, so nothing else of the world is needed. The two plugin readers are `@orig`-tagged.
- **`src/graphics/ps2_world_mesh.h`** decodes a mesh's DMA chain ([PS2 world geometry](#ps2-world-geometry)).
- **`src/platform/world_atomic.h`**: `attachWorldPlugins()` (run by `RenderEngine::start` between librw's init and
  open) registers librw's mesh, native data and right-to-render plugins, the `0x3F0` atomic plugin (so the scales stay
  with the atomic), and **a rights callback for plugin id 3** that gives atomics with pipeline `0x30082`/`0x30083`
  Coney's PS2 `ObjPipeline`, whose uninstance step decodes the chains into plain librw geometry (positions scaled by
  `+0x00`, texture coordinates by `+0x04`, colours, normals, triangles from the strips). `WorldAtomic::read` reads one
  part atomic: librw's atomic reader is written for clumps (it takes the geometry from a clump's geometry list), but a
  part atomic carries its geometry inside, so the geometry is read on its own and the atomic is then read from its
  struct and extension. `WorldAtomic::unpack()` uninstances it and hands it to the default (GL3) pipeline.

What librw does with these atomics on its own (from librw's source): it **reads** the geometry and keeps each mesh's
DMA chain untouched, but it **cannot decode** it. Its PS2 uninstance code expects RenderWare's default layout,
positions as `V3_32` and texture coordinates as `V2_32` inline after their `UNPACK`; here they are `V4_16`, by
reference, interleaved and scaled. With its asserts off (Coney's build) it would print "unexpected unpack" and copy the
wrong bytes. And the GL3 renderer cannot draw PS2 native geometry at all. So the smallest fix is the pipeline above,
registered from Coney's code; librw needs no patch.

**Streaming** (platform-neutral, `src/world/`, unit-tested on synthetic worlds):

- `world_manifest.h`: `readWorldManifest` (`0x00410e08`) and the no-manifest heap rules.
- `streamed_world.h`: `StreamedWorld`, one world's bookkeeping, its tables sized from the data (so `level70s`'s 593
  sectors fit): `create` indexes the sectors (`World_RegisterSector`); `cameraDistanceSq` is
  [the face-plane metric](#camera-distance); `findSectorToLoad`, `pendingDistance`, `findPartToUnload`,
  `resetVisibility`, `findVisibleSectors` and `collectSectors` follow `0x00411eb0`, `0x00411880`, `0x004120a8`,
  `0x004123e8`, `0x00411d10` and `0x00411b20`. The two quirks are kept and marked in the code: the squared distance
  compared with the plain draw distance, and part `n` never unloaded. A consequence of the metric shows in the tests:
  a camera inside a sector is as near to the neighbour across the closest face, and the lower index wins the tie.
- `world_streamer.h`: `updateStreaming` (`WorldManager_Update`) makes one decision a frame across the `s` and `d`
  worlds through a `PartStore`; `requestPart` (`0x00412310`); `preloadWorlds` (`WorldManager_Preload`, radius = the
  draw distance); `adjustDrawDistance` (step 3 of [A frame](#a-frame)).
- `sector_budget.h`: `SectorBudget`, the `Sector Pool` as a number ([Memory](memory.md#coneys-implementation)).
- `view_frustum.h`, `debug_camera.h`: the visibility pass's frustum test (Coney tests each sector's box, having no BSP
  walk) and the viewer's free-fly camera.

**Loading and drawing** (`src/platform/`):

- `world_set.h`: `WorldSet` loads a name's worlds in LoadLevel's order (`<name>s` then `<name>d`, or `<name>` alone):
  manifest, room in the budget, world stream with its dictionary (`0x00410648`, `0x00410a50`); it is the `PartStore`
  that reads a part (`0x004110c0`: dictionary first, then each atomic placed at its sector's origin, unpacked and
  given geometry flag `0x40`) and frees one (`0x004115d0`: atomics, then dictionary); its destructor is `World_Unload`.
- `texture_lookup.h`: [the global lookup](graphics.md#texture-lookup). Every world and part dictionary is registered,
  newest first, and librw's find callback searches them all; librw's own default makes its start-up dictionary current
  and searches only that one, so Coney clears the current dictionary, as the game never leaves one set. No file is
  read and no stand-in made for a missing name.
- `world_renderer.h`: `WorldManager_Render` as far as it goes: the camera at the draw distance with fog from half of
  it in the background colour, then the `s` world's collected sectors and the `d` world's, Z test and write, back faces
  culled, each atomic through `World_RenderSectorAtomic` (material alpha for the one-second fade, then librw's render).
- `world_viewer_mode.h`: the mode behind `--view-world`. Per frame: camera, one streaming decision (from the last
  frame's visibility), draw distance, visibility pass, draw. Game time, so `--frames` and `--input-script` give the
  same run every time.

**Seen in the viewer** (screenshots of `level2`, `level14`, `level51`, `level83`, `level100` and `objarena`, checked by
eye; none kept):

- Sectors meet without cracks or overlaps, so placement at the sector origin with the `+0x00` scale is right.
- **`0x3F0 +0x04` is the texture-coordinate scale**: with it, road markings, crossings, tiled pavements and trees
  look right; with the `+0x00` scale instead, the same textures tile visibly wrong (trees become rows of repeated leaf
  patches, crossings lose their stripes). Coney's evidence: confirmed (runtime), visually.
- **The first texture-coordinate set** is the base texture's: drawn with set 1 only, everything looks right. What the
  second set (in the `s` worlds) is for is open.
- **Prelighting is dark**: over all of `level2s`'s vertices the colour channels average about 14 of 255 and rarely
  pass 128; alpha is always 255. Coney doubles red, green and blue (clamped) when it unpacks, reading 0x80 as full
  brightness as the GS does when it modulates a texel by a vertex colour (**Coney's choice**, inferred from the GS).
  Even so the scenery is dark without the game's LightManager, so the viewer adds one ambient light of 0.25
  (**Coney's choice**, a stand-in until the LightManager exists).
- **Winding and culling**: in `level2s`, `level2d` and `level51s` 99.6 % of the triangles face the way their vertex
  normals point (153,453 against 525); with back-face culling, the faces that go are the backs of one-sided backdrop
  façades seen from outside the play area, as expected.
- Billboards and signs read left to right: the image is not mirrored.

**Coney's choices** (marked in the code): the `Sector Pool`'s size is the memory page's upper bound, 23,181,864
bytes, charged with the `Global Data Pool` (101 % of `warriors.glr`), the `World Level Pool` (103 % of `<level>.lev`,
at least 256 KB) and then the worlds and parts by their manifest heap sizes; a part's recorded heap size stays the
manifest's (the original replaces it with what the part used); reads are synchronous; a part that fails to read is
marked failed and not asked for again; freeing happens only when a wanted part does not fit; the 5.0 margin compares
plain distances (**the original: squared**, [Choosing what to stream](#streaming)); `pendingDistance` is +infinity
when the last search found nothing (**the original: `FLT_MAX`**; the same in effect); the preload's "nearest missing
sector" (`0x0040e100`) is a fresh search (**the original reuses the last search**); the fade uses game time; the
camera's own far clip is 300, its near clip 0.5, its view window 0.5 high and as wide as the window's shape (**the
original: 115, 0.1 and a 65° view, (0.637, 0.478)**, [The player camera](#player-camera)); the background and fog
colour is a slate blue (**the original: the level script's `SetFogColor`, white by default**, [Fog](#fog)); collected
sectors are drawn nearest first, sorted by the camera metric (**the original: last collected first**,
[Visibility](#visibility)); the viewer starts above the middle of the first world's
part 1, looking along +z.

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[world_streaming]"` streams all 80 levels' worlds
(79 pairs and `objarena`) under a camera that visits the centre of every streamed sector, three frames each (15,945
frames): 1,564 parts read, every used part at least once, none failed, none freed: with the default budget every
level's worlds and parts fit at once (largest peak 17,428,516 bytes for the worlds and parts alone). Most atomics
resident at once: 598. With the budget cut to the worlds plus 2 MB, `level51`, `level83` and `level54` stream with
682 parts freed, 711 read and 533 frames short of room, peak 3,753,423 bytes; in both runs no frame breaks an
invariant (budget never exceeded and equal to what is loaded, every loaded part's atomics present, no freed part seen
last frame, the last part never freed, the margin always kept).

**The implementer's questions, answered** (2026-10-04, from code; the contradictions are marked in the paragraph
above):

- **`0x3F0 +0x04`**: the texture-coordinate scale. The code shows both floats uploaded in one quadword; the microcode
  that applies them was not read, so the evidence stays Coney's visual check ([Atomic plugin](#atomic-plugin)).
- **Vertex colour range**: consistent with 0x80 = 1.0, so Coney's doubling stays; the CPU halves textured material
  colours for the same reason ([Pipelines](#pipelines)). The 0.25 ambient stand-in should become the
  [LightManager](#lighting): 0.157 plus the script's world ambient (+ 0.07) and world lights; Coney's 0.25 is brighter
  than the default.
- **The 5.0 margin**: squared distances. **`0x0040e100`**: no new search. **`World_PendingDistance` with nothing
  found**: `FLT_MAX` ([Camera distance](#camera-distance)).
- **The player camera**: far clip 115, near clip 0.1, 65° ([The player camera](#player-camera)).
- **The second texture-coordinate set and the pipelines**: [Pipelines](#pipelines).
- **The background colour**: the level script's fog colour ([Fog](#fog)); **the level world**: the light glows
  ([Level loading](level-loading.md#the-level-object)).
- **The `Sector Pool`'s real size**: still open (runtime, [Memory](memory.md#open-questions)).

## Disc counts {#disc-counts}

NTSC-U disc, 2026-10-04, counts and sizes only. "Parts" are those numbered `1..n`, the ones the game can load;
"sectors" are sectors with a streamed atomic; sizes in bytes.

| | Count | Bytes |
| --- | ---: | ---: |
| Worlds (`_sec.wld`) | 159 | 70,177,584 (texture dictionaries 66,506,916) |
| Manifests (`_sec.mem`) | 159 | 14,420 |
| Part files used | 1,564 | 279,667,294 (texture dictionaries 41,411,116) |
| Part files never loaded | 347 | 31,046,766 |
| World sectors / with an atomic | 16,074 / 5,315 | |
| Atomics in used parts | 5,315 | 2,870,179 triangles, 4,943,282 vertices, 88,425 materials |
| Textures | 11,664 in worlds, 10,776 in used parts | |

Largest values: world stream 1,316,524 (`level61s`); world heap 1,594,404; part file 346,962; part heap 372,229;
parts in one world 46 (`level83s`, `level84s`); streamed sectors in one world 593 (`level70s`, then 144). The level
whose worlds and used parts are largest is `level51`: 15,538,318 bytes in all, far more than fits in memory at once,
which is why the parts stream.

Per level: world stream size, parts and streamed sectors of each world, and the bytes of all its used parts.
`objarena` has one world: 28,820 bytes, 1 part, 1 sector, 69,708 bytes of parts.

??? note "Every level (79)"

    | Level | `s` world | `s` parts | `s` sectors | `d` world | `d` parts | `d` sectors | Parts, both worlds |
    | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
    | `level1` | 26,800 | 1 | 1 | 18,112 | 1 | 1 | 2,076 |
    | `level2` | 811,128 | 24 | 48 | 275,956 | 16 | 57 | 6,940,964 |
    | `level3` | 1,154,234 | 30 | 112 | 283,156 | 10 | 35 | 6,457,894 |
    | `level5` | 1,098,068 | 23 | 67 | 297,416 | 20 | 72 | 8,297,320 |
    | `level9` | 553,380 | 22 | 57 | 268,772 | 5 | 20 | 5,915,296 |
    | `level11` | 1,080,879 | 30 | 71 | 269,740 | 20 | 62 | 9,437,256 |
    | `level14` | 807,126 | 21 | 54 | 269,756 | 9 | 34 | 5,743,904 |
    | `level20` | 1,081,816 | 36 | 94 | 278,056 | 12 | 52 | 9,784,458 |
    | `level31` | 801,358 | 20 | 40 | 284,636 | 12 | 48 | 6,426,536 |
    | `level34` | 1,085,436 | 23 | 68 | 265,808 | 10 | 29 | 5,535,774 |
    | `level51` | 1,127,936 | 39 | 95 | 306,644 | 30 | 101 | 14,103,738 |
    | `level52` | 1,117,420 | 26 | 75 | 283,492 | 16 | 55 | 8,769,378 |
    | `level54` | 1,214,140 | 35 | 137 | 300,496 | 28 | 102 | 11,784,862 |
    | `level55` | 1,095,196 | 19 | 46 | 271,504 | 11 | 28 | 4,941,500 |
    | `level60` | 1,233,152 | 7 | 28 | 517,868 | 4 | 8 | 1,900,100 |
    | `level61` | 1,316,524 | 10 | 25 | 267,664 | 5 | 14 | 2,876,126 |
    | `level62` | 1,062,240 | 7 | 32 | 258,168 | 4 | 5 | 1,684,622 |
    | `level63` | 723,600 | 5 | 21 | 254,880 | 3 | 8 | 1,316,814 |
    | `level64` | 1,049,612 | 8 | 20 | 269,336 | 5 | 20 | 2,306,562 |
    | `level70` | 1,289,380 | 31 | 593 | 214,696 | 5 | 5 | 2,732,096 |
    | `level71` | 511,908 | 3 | 12 | 198,764 | 2 | 3 | 507,526 |
    | `level72` | 201,076 | 1 | 1 | 48,536 | 1 | 1 | 123,322 |
    | `level73` | 1,003,424 | 21 | 140 | 276,664 | 13 | 34 | 5,259,224 |
    | `level74` | 351,256 | 4 | 24 | 221,172 | 2 | 5 | 1,177,106 |
    | `level80` | 1,063,451 | 19 | 48 | 278,940 | 12 | 42 | 6,692,176 |
    | `level81` | 845,680 | 27 | 68 | 274,316 | 10 | 31 | 5,342,364 |
    | `level82` | 825,154 | 29 | 82 | 291,940 | 17 | 74 | 9,017,116 |
    | `level83` | 1,104,452 | 46 | 103 | 303,164 | 33 | 104 | 13,074,120 |
    | `level84` | 871,416 | 46 | 144 | 281,532 | 17 | 45 | 7,676,910 |
    | `level86` | 1,051,788 | 14 | 31 | 259,300 | 5 | 20 | 3,716,592 |
    | `level87` | 793,823 | 24 | 56 | 276,424 | 11 | 39 | 7,157,260 |
    | `level90` | 346,412 | 1 | 2 | 221,776 | 1 | 3 | 326,300 |
    | `level91` | 254,848 | 8 | 18 | 276,048 | 5 | 33 | 2,895,146 |
    | `level92` | 1,089,181 | 30 | 73 | 278,396 | 14 | 40 | 7,767,202 |
    | `level93` | 804,528 | 23 | 63 | 270,528 | 14 | 46 | 6,603,378 |
    | `level94` | 784,504 | 8 | 20 | 254,860 | 5 | 21 | 2,753,064 |
    | `level95` | 1,102,042 | 32 | 78 | 286,748 | 17 | 48 | 8,542,984 |
    | `level96` | 1,132,436 | 26 | 100 | 280,264 | 17 | 38 | 9,255,666 |
    | `level97` | 254,848 | 8 | 18 | 276,048 | 5 | 33 | 2,895,146 |
    | `level98` | 184,508 | 1 | 2 | 98,164 | 1 | 1 | 152,946 |
    | `level99` | 811,848 | 20 | 47 | 274,856 | 11 | 27 | 5,712,274 |
    | `level100` | 50,756 | 19 | 51 | 30,056 | 1 | 1 | 95,700 |
    | `level101` | 494,044 | 4 | 12 | 190,816 | 2 | 9 | 1,205,364 |
    | `level102` | 474,088 | 3 | 9 | 257,736 | 3 | 13 | 1,213,532 |
    | `level103` | 710,368 | 5 | 17 | 257,796 | 2 | 5 | 1,364,454 |
    | `level104` | 413,756 | 5 | 29 | 264,276 | 5 | 21 | 1,458,342 |
    | `level105` | 159,720 | 1 | 2 | 51,272 | 1 | 2 | 201,474 |
    | `level106` | 630,084 | 6 | 13 | 224,600 | 2 | 8 | 1,456,838 |
    | `level107` | 787,800 | 5 | 13 | 204,476 | 1 | 5 | 1,317,476 |
    | `level108` | 778,096 | 8 | 18 | 269,836 | 5 | 22 | 2,501,606 |
    | `level109` | 736,060 | 9 | 21 | 263,720 | 3 | 13 | 2,565,624 |
    | `level110` | 86,588 | 2 | 5 | 12,948 | 1 | 1 | 360,938 |
    | `level111` | 668,100 | 6 | 14 | 250,488 | 3 | 8 | 1,593,614 |
    | `level112` | 1,056,084 | 15 | 48 | 268,032 | 6 | 22 | 4,524,650 |
    | `level113` | 616,952 | 4 | 9 | 265,956 | 3 | 14 | 1,358,088 |
    | `level114` | 788,220 | 10 | 20 | 208,556 | 2 | 9 | 2,297,158 |
    | `level115` | 741,720 | 6 | 13 | 270,132 | 4 | 18 | 1,909,206 |
    | `level116` | 117,344 | 1 | 2 | 91,676 | 1 | 1 | 190,452 |
    | `level117` | 124,500 | 1 | 3 | 37,632 | 1 | 1 | 252,000 |
    | `level118` | 468,940 | 4 | 8 | 128,004 | 1 | 5 | 935,422 |
    | `level119` | 530,872 | 19 | 51 | 290,812 | 10 | 55 | 6,335,822 |
    | `level120` | 530,872 | 19 | 51 | 290,812 | 10 | 55 | 6,335,822 |
    | `level121` | 408,860 | 2 | 5 | 226,836 | 2 | 9 | 754,820 |
    | `level122` | 367,208 | 3 | 6 | 220,292 | 1 | 5 | 554,400 |
    | `level123` | 134,472 | 1 | 1 | 23,348 | 1 | 1 | 116,210 |
    | `level124` | 135,412 | 2 | 4 | 58,020 | 1 | 1 | 349,650 |
    | `level125` | 128,864 | 2 | 3 | 22,696 | 1 | 1 | 275,460 |
    | `level126` | 755,760 | 7 | 27 | 261,628 | 2 | 7 | 1,684,436 |
    | `level127` | 456,524 | 3 | 32 | 98,704 | 2 | 12 | 916,258 |
    | `level128` | 396,944 | 4 | 8 | 231,468 | 2 | 8 | 946,568 |
    | `level129` | 635,228 | 7 | 13 | 238,724 | 2 | 8 | 1,610,290 |
    | `level130` | 117,788 | 2 | 6 | 204,748 | 2 | 13 | 746,400 |
    | `level131` | 353,100 | 4 | 62 | 46,384 | 1 | 7 | 671,722 |
    | `level132` | 260,628 | 2 | 4 | 81,780 | 1 | 4 | 512,874 |
    | `level133` | 265,656 | 6 | 13 | 109,732 | 2 | 7 | 1,543,244 |
    | `level134` | 643,996 | 6 | 14 | 267,224 | 3 | 16 | 1,783,600 |
    | `level135` | 783,448 | 7 | 13 | 265,152 | 4 | 13 | 2,143,394 |
    | `level136` | 338,544 | 4 | 10 | 211,940 | 3 | 13 | 1,112,152 |
    | `level137` | 317,176 | 3 | 5 | 183,280 | 2 | 8 | 797,428 |

Some pairs are byte-identical (`level91`/`level97`, `level119`/`level120`).

## Open questions

- **The microcode**: how the VU1 programs at `0x005045a0`, `0x004fc870` and `0x004ff1c0` use the `0x3F0` scales,
  the prelighting and the lights, and how the dual pass draws (a PCSX2 look at VU1 memory would settle `+0x04`).
- **MatFX effect 1** (bump map, 2,467 materials): which pipeline it ends up with, and whether it draws differently.
- **Light B** of the `LightManager` (the 300 ms pulse on objects): what turns it on.
- **The two atomic pipelines `0x30082` and `0x30083`:** only one atomic uses `0x30082`; how the two differ.
- **The native data struct size** that exceeds its section ([Part file](#part-file)).
- **`0x3F0` `+0x08`** (always 0 in the worlds) and `0x004290d8`, its only reader.
- **World `+0x04`** (answered): nothing sets it to a value `>= 0`. The only writes found are the constructor's and
  the finished unload's -1 (`0x00410308`, `0x004115d0`); every other function of `WorldPS2.cpp` and the world
  manager's update and loaders were searched for a store to `+0x04` (inferred: a write from outside `World/` is not
  ruled out, but the world objects are private to the world manager). The busy test on it is dead code in practice;
  it looks like the remains of an "unload in progress" index that the retrying unload no longer needs
  (speculative).
- **`0x005147c8`**, the "nearest wanted thing within 75" flag, and the 32 bytes at world `+0x18`: their readers.
- **`level70`**: 593 streamed sectors overflow the 560-entry table. Is the level reachable?
- **The water effect** (`Graphics/WaterEffect.cpp`), the third streamer: its file (named `%u` from a CRC) and drawing.
- **Runtime confirmation** with PCSX2: a breakpoint on `0x00412310` while walking through a level would show the
  order parts are requested in; a watch on world `+0x0000` the reads in flight.
