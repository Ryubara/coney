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
| `0x0017d640` | `LightManager` constructor | the light manager (`0x0050cce4`, 0xc0 bytes) ([Lighting](lighting.md#manager)) | confirmed (code) |
| `0x0017de10` | `LightManager_SelectLights` | the lights for one atomic or sphere | confirmed (code) |
| `0x00124778` | `Cam_Follow::Cam_Follow` | the player camera: field of view, near and far clip ([The player camera](#player-camera)) | confirmed (code) |
| `0x00411d10` | `World_FindVisibleSectors(world, firstViewport)` | visibility pass for one camera | confirmed (code) |
| `0x00411b98` | `World_VisibilitySectorCallback` | PVS, occluders, mark visible | confirmed (code) |
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

### Small functions of `World/` {#small-functions}

The rest of `World/` (`0x0040c5e0`-`0x004124f8`) besides the level loader's functions on
[Level loading](level-loading.md#original-structure). Names are ours and match the local Ghidra project; all
confirmed (code) at the address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0040c628` / `0x0040c648` / `0x0040c668` | `Wad_FindEntry` / `Wad_GetEntrySize` / `Wad_HasEntry` | the WAD object's lookups (its opener is `Wad_Open`, [File I/O](file-io.md#the-wad-index)): the index entry for a name, its size field (`+0x04`, no null check), whether it exists | confirmed (code) |
| `0x0040c938` | `WorldManager_SetLoadPriority(scale)` | sets `0x005147cc`, the factor the [streaming update](#streaming) applies to the resource manager's distances (`LoadLevel` sets 0.001, the Rumble menu 10.0) | confirmed (code) |
| `0x0040c948` | `World_Precache(name, radius, budget)` | pauses the frame pacer (`0x0050b734`) if it runs, then `WorldManager_Preload` around the named position when a level object exists, and resumes | confirmed (code) |
| `0x0040ca18` | `Water_Set` (anchor of `World/WorldManagerLua.cpp`) | first call: allocates the level's `WaterEffect` (0x100 bytes, [the water surface](graphics.md#code-water)) into world manager `+0x5c`; later calls set its frame, wave height and speed, width, length and colour | confirmed (code) |
| `0x0040cc40` | `World_QueuePackToPrecache(name)` | `WorldManager_QueuePack` (`0x0040e1a0`) on the world manager: appends a pack name to the queue at `+0x0c`-`+0x24` that the preload loads at its end ([The world manager](level-loading.md#world-manager)) | confirmed (code) |
| `0x0040cc70` | `ScreenFx_SetMotionAlpha(a)` | unless the screen effect `0x0051489c` is running, sets the motion-blur colour (white, alpha `a`) of both screen managers (`0x005fdeb8`, `0x005fdebc`, `+0x1a4` and `+0x1a8`) | confirmed (code) |
| `0x0040cce8` / `0x0040cd28` | `ScreenFx_QueueMotionBlurAlpha` / `ScreenFx_QueueMotionBlurColour(time, rgba)` | queue a blur colour change on the screen manager `0x005fdeb8` (`0x0018c8c8`) | confirmed (code) |
| `0x0040cd78` / `0x0040cda8` | `ResourceManager_SetDynamicAnimation` / `_SetCharacterModel` | forward to the resource manager `0x0050cd4c` (`0x0018abd0`, `0x0018ad40`) | confirmed (code) |
| `0x0040cc68` | `PVS_CaptureSamplePoint` | empty: the PVS sampler was removed ([Debug](debug.md)) | confirmed (code) |
| `0x0040cf18` | `LevelHeader_RegisterChunkHandler` | installs `0x0040ce30` as chunk `0x17`'s handler ([Chunk system](chunk-system.md#handlers-registered-at-run-time)) | confirmed (code) |
| `0x0040e0b8` | `WorldManager_ResetVisibility` | clears `+0x34` and calls `World_ResetVisibility` on the `s` world (`+0x44`) and the `d` world (`+0x48`) when present | confirmed (code) |
| `0x0040e8d0` | `WorldManager_GetSolidWorld` | returns the `s` world (`+0x44`) | confirmed (code) |
| `0x004102f0` | `Atomic_SetModulateMaterialColour` | geometry flag `0x40` on an atomic's geometry ([Part file](#part-file), step 4) | confirmed (code) |
| `0x00410558` | `World_Destroy` | the world's destructor: empties the 140 16-byte vectors at `+0x940`-`+0x1200` and frees their buffers from the STL pool | confirmed (code) |
| `0x00411958` | `Material_SetAlphaCallback` | a material callback that sets the alpha byte of the material colour (`+0x04`); `World_RenderSectorAtomic` runs it over the materials for the fade-in | confirmed (code) |

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
| `+0x04` | float | 1.0 | uploaded next to `+0x00` in the same quadword (`+0x00` as `x`, `+0x04` as `z`; `0x001928c0`, `0x001928f0`); **the scale of the packed 16-bit texture coordinates** | upload confirmed (code); scale confirmed (runtime) by Coney's viewer, visually; the dual microcode applies it to both sets, confirmed (code) ([Pipelines](#pipelines)) |
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
headers (5,155 of 5,315 atomics equal), and 4,882,437 distinct vertices against 4,943,282 (vertices compared in every
attribute librw keeps; the unused fourth position word and the normal's padding byte are ignored). The difference is
inferred to be vertices that became identical when packed to 16 bits, which merges them and turns their triangles
degenerate.

### Pipelines and the second texture-coordinate set {#pipelines}

The pipeline ids in the files are not the whole story: when a part's atomic (and the level file's sky, cloud and
skyline models) is set up, `Atomic_AssignGamePipelines` (`0x00426c78`) gives the atomic the pipeline `0x30083`
(`0x005151c4`, made by `0x00426d58`) and chooses each material's pipeline from its **MatFX effect** (`0x00426cc8`,
reading the effect type through `0x004653d8`), confirmed (code):

| Material's MatFX effect | Pipeline | Made by | Texture coordinates unpacked as | Microcode | Evidence |
| --- | --- | --- | --- | --- | --- |
| none, geometry without flag `0x80` | `0x30084` (`0x005151b4`) | `0x00428f30` | `V2_16`, one set (`0x6500000d`) | table `0x005045a0` | confirmed (code) |
| none, geometry with flag `0x80` (two sets) | `0x30088` (`0x005151b8`) | `0x004298c0` | `V4_16`, two sets (`0x6d00000d`) | the same table `0x005045a0` | confirmed (code) |
| 4, dual texture | `0x30086` (`0x005151bc`) | `0x004273f8` | | `0x004fc870` | confirmed (code) |
| 2, environment map | `0x30087` (`0x005151c0`) | `0x00428080` | | `0x004ff1c0` | confirmed (code) |
| any other | unchanged | | | | confirmed (code) |

The set-up of these pipelines is `0x00426e28` / `0x00426c40`. So `0x30084` and `0x30088` run the **same microcode**
and differ only in unpacking one or two texture-coordinate sets: on a plain material the second set is unpacked and,
inferred from the shared microcode, not used. `0x30086` is the dual-texture pipeline with its own microcode.

**Disc check (corroboration):** in the streamed worlds' part files, 9,516 materials carry MatFX effect 4 (dual), all
with the blend `SRCALPHA` / `INVSRCALPHA` and a second texture; 2,467 carry effect 1 (bump map), which keeps its stored
pipeline. So the **second texture-coordinate set** is, inferred, the coordinates of a dual material's second texture,
alpha-blended over the first (decals, grime, painted markings).

**The dual pipeline's upload and microcode** (`0x004275d0`, `0x004fc870`), confirmed (code) from the disassembly of
the VU1 program (our own decoding of the `MPG` blocks of the DMA chain at `0x004fc870`):

- **Both coordinate sets are scaled by `0x3F0 +0x04`.** The upload unpacks ten quadwords to VU1 address `0x3bc`
  (956); the eighth, at **963**, is the scale quadword (`+0x00` as `x`, `+0x04` as `z`). The microcode loads it
  (`LQ.xz vf6, 963(vi0)`, at VU address `0x0011`) and, for every vertex, converts the whole texture-coordinate
  quadword (both sets, `V4_16`: s1, t1, s2, t2) to float and multiplies **all four** components by its `z`
  (`ITOF0.xyzw vf14, vf14`, then `MULz.xyzw vf14, vf14, vf6z`, at `0x0023` / `0x0027`). Positions are multiplied
  by `x` the same way, the normals by 1/127, and the prelighting is only converted to float.
- **The second pass's blend comes from the material, not the microcode.** The upload writes `ALPHA_2` from the 64-bit
  word at `+0x28` of the material's dual effect. `RpMatFXMaterialSetDualBlendModes` (`0x004658d0`) stores the source and
  destination blend at the effect's `+0x04` / `+0x08` and rebuilds that word (`0x00465e78`) as **`table[dst][src]` with
  `FIX` `0x80`**, from a 6 × 6 byte table at `0x0052e620` over zero, one, source alpha, inverse source alpha,
  destination alpha and any other mode. Source alpha over inverse source alpha gives **`0x44`** (the ordinary alpha
  blend); one and one gives `0x68` (additive); unsupported pairs give `0xaa`. Every dual material in the streamed worlds
  is source alpha / inverse source alpha (disc check above), so in the retail worlds the second pass is always `0x44`; a
  faithful port can still convert the pair through the same table.
- The rest of `TEST_2` is the global Z test (`0x00596de8` & `0x70000`) with no alpha test (the effect's `+0x38`, 0). The
  fog colour of the second pass is the effect's `+0x30` unless that is `0x1000000`, the default (`0x00465f88`), when it
  is the current fog colour (`0x00596df8`); other writers of `+0x30` were not searched.

**Runtime corroboration** (the GS dumps of the [reference views](rendering.md#reference-views) and the level dumps,
fourteen dumps, 1,684 dual pairs): each context-2 strip draws the same screen positions as the context-1 strip before it
with different texture coordinates (the second set), and in most pairs the coordinates of both passes are multiples of
the same power of two, the scale (in the rest, inferred, one set's raw values were all even or its coordinates whole
numbers, so its step looks coarser or finer).

**Vertex colour range**, confirmed (code) at the uploads `0x004252c8` and `0x00426430`: the **material colour** is
scaled by `1/255` for an untextured material and by `0.0019700117` (about `0.5/255`) for a textured one; alpha
always by `0.00197`. A textured white material thus reaches the GS as 128, and the GS's texture modulate treats 128 as
1.0 (inferred from the GS's documented behaviour). The prelighting colours are unpacked unsigned (`V4_8`) and not
scaled by the CPU, so they too are on the GS's scale, where 0x80 is full brightness (inferred). The lit vertex colour
the GS receives **never exceeds 0x80** (confirmed (runtime): the brightest channel in a `level99` frame is exactly 128
on thousands of vertices), so there is no 2× overbright: `min(1, prelight + light) × material`, [Lighting: the
maths](lighting.md#world).

#### The pipeline unit {#pipeline-unit}

The game's PS2 pipelines are one unit of their own, `0x00424ee8`-`0x00429b18`, linked between `Warriors/` and
`Movie/`, with no path string (its directory is not known). It ends with the static-initialiser stub `0x00429af8`.
It holds two groups: the pipelines of the **character models** (`0x30080`-`0x30082`) and those of the **world**
(`0x30083`-`0x30088`, the table above). Each pipeline is a RenderWare PS2 all-in-one pipeline (made by `0x00461420`
from a static descriptor, found again by id with `0x00461470`) with two callbacks: a first that always answers 1 and
an **upload** callback that builds the material's data for the VU microcode (the colour scale above, the `0x3F0`
words, texture and blend state). Names ours; confirmed (code) unless marked.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00429a98` | `Pipelines_CreateGame` | creates both groups at device start (from `Init`, [Graphics](graphics.md#start-up)); returns whether all were made | confirmed (code) |
| `0x00424ee8` / `0x00426c30` | `CharPipelines_Construct` / `WorldPipelines_Construct` | empty constructors of the two groups' objects, which hold no data | confirmed (code) |
| `0x00429a60` | `Pipelines_ConstructStatic` | constructs the static object `0x006fecb0` that holds both groups | confirmed (code) |
| `0x00429ac8` / `0x00429af8` | `Pipelines_StaticInit` / its stub | the unit's static initialiser and stub | confirmed (code) |
| `0x00424ef8` | `CharPipelines_Create` | makes `0x30081` (`0x004250a8`, kept in `0x00515068`), `0x30082` (`0x00426240`, `0x00515064`) and the atomic pipeline `0x30080` (`0x00424fd0`, `0x0051506c`) | confirmed (code) |
| `0x00424f68` | `CharPipelines_AssignToAtomic(atomic)` | gives an atomic `0x30080` and each of its materials `0x30081` (the material callback `0x00424fb0`); called by the character model set-up `0x00174c18` (from `CharacterModel.cpp`'s `0x00178178`) | confirmed (code); that these are the character pipelines inferred from that caller |
| `0x00424fb0` | `CharPipelines_SetMaterialPipe` (*made*) | material callback: material `+0x08` = `0x30081` | confirmed (code) |
| `0x00424fd0` | `CharPipelines_CreateAtomicPipe` | the atomic pipeline `0x30080`, object callback `0x00425d78` | confirmed (code) |
| `0x00425d78` | `CharPipelines_AtomicSetup` | the atomic's per-draw set-up: the geometry's instance data, the morph interpolation, re-instance flags when the geometry changed, a test of the bounding sphere against the camera's 6 frustum planes (inside: no clipping), the lighting mode from the geometry's flags | confirmed (code) |
| `0x004250a8` | `CharPipelines_CreateMaterialPipe` | the material pipeline `0x30081`: microcode names `PS2user1.csl`-`PS2user4.csl` (`0x0052ea50`) and their unpack formats | confirmed (code) |
| `0x004250a0` / `0x004273e8` / `0x004273f0` | `Pipe_AlwaysTrue` (*made*, three copies) | the pipelines' first callback: returns 1 | confirmed (code) |
| `0x004252c8` | `CharPipelines_UploadMaterial` | upload callback of `0x30081` | confirmed (code) |
| `0x00426240` | `CharPipelines_CreateMaterialPipe2` | a second material pipeline `0x30082` (32 microcode slots); nothing reads `0x00515064` back, so it is never used | confirmed (code); unused inferred |
| `0x00426430` | `CharPipelines_UploadMaterial2` | upload callback of `0x30082` | confirmed (code) |
| `0x00426c40` | `WorldPipelines_Create` | the material pipelines (`0x00426e28`), then the atomic pipeline `0x30083` (`0x00426d58`) | confirmed (code) |
| `0x00426e28` | `WorldPipelines_CreateMaterialPipes` | `0x30084`, `0x30086`, `0x30087`, `0x30088` | confirmed (code) |
| `0x00426e70` | `WorldPipelines_AtomicSetup` | object callback of `0x30083`: as `0x00425d78` | confirmed (code) |
| `0x004275d0` | `WorldPipelines_UploadDual` | upload callback of the dual-texture `0x30086` | confirmed (code) |
| `0x00428410` | `WorldPipelines_UploadEnvMap` | upload callback of the environment map `0x30087` | confirmed (code) |
| `0x00428260` | `EnvMap_GetMatrix` | the environment map's texture matrix from the camera and the effect's frame (none: the camera alone), cached while camera, frame and frame counter stay the same | confirmed (code) |
| `0x004290d8` | `WorldPipelines_UploadPlain` | upload callback shared by `0x30084` and `0x30088`; the only reader of `0x3F0` `+0x08` | confirmed (code) |

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
7. Fog distance = draw distance × 0.5: the **humans** in view (the resource manager's list `+0xc78`, last queued
   first, back-face culling; [Drawing a human](graphics.md#human-draw)). Fog distance restored (× the fog start):
   the **cars'** opaque pass (list `+0xc98`, culling off; [Drawing a car](graphics.md#car-draw)), then the object
   instances of list `+0xc88` (`0x0017fd78`, back-face culling) except those of type `0x20`.
8. **The `d` world:** the same as the `s` world.
9. The water effect (`0x00191dd8`, a phase that grows by 0.16 a frame).
10. With Z write off: the instances of type `0x20` (last first), then the cars' **glass** pass (list `+0xc98`, last
    first, culling off). Then the remaining effects, each after the world camera's update has ended: the players'
    throw arcs (`ThrowArcs_RenderAll`, `0x00419da0`), litter (`Garbage_Render`, `0x001712c0`), ground fog
    (`GroundFog_Render`, `0x00171f58`) and, while `0x005971ac` is set, embers (`Embers_Render`, `0x001795f8`)
    ([Graphics](graphics.md) has each). The toggles `0x005e5380`-`0x005e5398` gate steps 5-10 in the code; all are
    1, so every step runs. Confirmed (code).

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

The `LightManager` (`0x0050cce4`) lights the world; the whole subject is on [Lighting](lighting.md). For the streamed
world, confirmed (code): `World_RenderSectorAtomic` selects the world's lights for the sector's bounding sphere
(`LightManager_SelectLights`, `0x0017de10`, flags 2): the world ambient, any world directional lights and the nearest
overlapping world point lights, up to 8, which the PS2 pipeline adds to the prelighting
([World lighting](lighting.md#world)). With no script call the world ambient is the brightness, 40/255 = 0.157 grey;
`SetWorldAmbient(r, g, b)` makes it `rgb + 0.07 + 0.157` (0.227 grey in `level99`). The background is lit the same
way with a sphere far away and no point lights ([Level loading](level-loading.md#render-order)).

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

- `SetFogColor(r, g, b)` (`0x0036e558` → `Level_SetFogColour`, `0x0040c868`): floats in 0 to 1, times 255, alpha 255
  (confirmed (code)).
- `SetFogDistance(d)` (`0x0036e5f8` → `Level_SetFogDistance`, `0x0040c908`; confirmed (code)): the device's fog start
  `+0x444` (0.5 by default), the fraction of the far clip where fog begins.

**The fog curve**, confirmed (runtime) from a PCSX2 GS dump of `level99` (checkpoint 3, the street at night): fog is
per vertex and **linear in the camera distance** `w` (metres). The GS fog value (255 = no fog, 0 = all fog colour) is
`255 × (far − w) / (far − start)` with `far` the draw distance and `start = far × fog start`: 255 up to 57.5 m and 0
at 115 m, for the 115 m draw distance and fog start 0.5 (a least-squares fit over 2,305 partly fogged world vertices
puts the ends at 57.3 m and 115.1 m). The GS blends `colour × f + fogColour × (1 − f)` with `f` = value / 255, after
texturing and before alpha blending. Fog is off for the sky, clouds and skyline ([Level
loading](level-loading.md#render-order)) and for the HUD.

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
  `pendingDistance` is `FLT_MAX` when the last search found nothing. `collectSectors` walks the world's BSP back to
  front from the viewport camera (the far side of each plane first) and returns the sectors last collected first, as
  the original draws its list; the reader keeps the BSP's planes for it (`world_streams.h`, `BspPlane`).
- `world_streamer.h`: `updateStreaming` (`WorldManager_Update`) makes one decision a frame across the `s` and `d`
  worlds through a `PartStore`; `requestPart` (`0x00412310`); `preloadWorlds` (`WorldManager_Preload`, radius = the
  draw distance); `adjustDrawDistance` (step 3 of [A frame](#a-frame)). The 5.0 unload margin is added to the wanted
  sector's squared distance, and `nearestPendingDistance` (`0x0040e100`) reads the last searches without searching
  again, so the preload loads one part past its radius before it stops, as the original does.
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
  it in the background colour, then the `s` world's collected sectors and the `d` world's in the BSP's drawing order,
  Z test and write, back faces culled, each atomic through `World_RenderSectorAtomic` (material alpha for the
  one-second fade, then librw's render). With a level object it first draws the level's background (sky, turning
  clouds, skyline, then a Z-only clear), and then step 5, the glow world
  ([Level loading](level-loading.md#coneys-implementation)).
- `world_viewer_mode.h`: the mode behind `--view-world`. Per frame: camera, one streaming decision (from the last
  frame's visibility), draw distance, visibility pass, draw. Game time, so `--frames` and `--input-script` give the
  same run every time. The camera is Coney's free-flying debug camera, looking through the player camera's lens
  (`src/camera/camera_lens.h`: 65°, view window (0.637, 0.478), near clip 0.1, far clip 115 as the draw distance's
  ceiling). The documented clip planes keep the debug view useful: the draw distance still follows the missing
  scenery, and 115 is far enough to see down a street; beyond it the fog's colour takes over.
- **Second layers** (`world_atomic.h`, `dualLayerOf`): librw's MatFX plugin is not attached (it opens a driver for
  every backend and draws through pipelines Coney does not use), so Coney reads the MatFX material extension itself,
  keeping only the dual effect's texture. `WorldAtomic::unpack()` gives an atomic with dual materials a second atomic
  on the same frame: the same vertices with the second texture-coordinate set, one mesh per dual material textured
  with its dual texture. `SceneLighting` draws it right after the atomic, as [the second pass](rendering.md#dual)
  does: blended by the texture's alpha, no alpha test, Z write, at the base's fade (`level99` checkpoint 3: 137 dual
  meshes, stains and dirt over the ground and walls).
- **First passes' alpha test**: the world, objects and humans are drawn with librw's emulation of the GS's GEQUAL
  `0x40` test with `AFAIL` `FB_ONLY` ([Shared state](rendering.md#shared-state)): a fainter pixel is blended but
  writes no Z.
- **Mipmapped textures**: librw's OpenGL conversion makes a whole mip chain for a texture flagged mipmapped but fills
  only the levels the PS2 texture has (most have one), and OpenGL draws such a texture black; each converted texture is
  limited to its source's levels (`texture_dictionary.h`). Before this, `level99`'s roller shutters drew black.
- **Mip level by distance**: OpenGL picks a level by the texture's size on screen, the GS by the camera distance
  ([Rendering](rendering.md#world)). Coney keeps each texture's `K` and `L` when it converts it and draws atomics
  through its own copy of librw's default GL3 shaders, which sample at `log2(depth) × 2^L + K` for the sectors and
  at level 0 for everything else (`repo:src/platform/texture_lod.h`). With an OpenGL 2.1 context librw's own shaders
  stay.
- **Mipmap levels unswizzled once**: librw's conversion unswizzles each mipmap level twice (its level lock, then the
  image's), which scrambled every level and turned the padded small ones into a lattice (`level99`'s far roller
  shutters). Coney converts PS2 mipmapped rasters level by level itself, each unswizzled once at its sent size and
  cropped ([GS upload packets](formats/renderware.md#gs-packets)); the far shutters then show their slats.

**Seen in the viewer** (screenshots of `level2`, `level14`, `level51`, `level83`, `level100` and `objarena`, checked by
eye; none kept):

- Sectors meet without cracks or overlaps, so placement at the sector origin with the `+0x00` scale is right.
- **`0x3F0 +0x04` is the texture-coordinate scale**: with it, road markings, crossings, tiled pavements and trees
  look right; with the `+0x00` scale instead, the same textures tile visibly wrong (trees become rows of repeated leaf
  patches, crossings lose their stripes). Coney's evidence: confirmed (runtime), visually.
- **The first texture-coordinate set** is the base texture's: drawn with set 1 only, everything looks right. The
  second set (in the `s` worlds) is the MatFX dual texture's, below.
- **Prelighting is dark**: over all of `level2s`'s vertices the colour channels average about 14 of 255 and rarely
  pass 128; alpha is always 255. Coney doubles red, green and blue (clamped) when it unpacks, reading 0x80 as full
  brightness as the GS does when it modulates a texel by a vertex colour (**Coney's choice**, inferred from the GS).
  The viewer lights the scenery with the light manager as it starts (world ambient 0.157, [Lighting](#lighting)), as
  a level is lit before its script runs; play lights it with the level's scripted lights.
- **Winding and culling**: in `level2s`, `level2d` and `level51s` 99.6 % of the triangles face the way their vertex
  normals point (153,453 against 525); with back-face culling, the faces that go are the backs of one-sided backdrop
  façades seen from outside the play area, as expected.
- Billboards and signs read left to right: the image is not mirrored.

**Coney's choices** (marked in the code): a part's recorded heap size stays the manifest's (the original replaces it
with what the part used); reads are synchronous; a part that fails to read is marked failed and not asked for again;
freeing happens only when a wanted part does not fit; the fade uses game time; a window that is not 4:3 keeps the
player camera's view-window height; the viewer starts above the middle of the first world's part 1, looking along
+z. The choices the research contradicted (2026-10-04) now follow the original: the `Sector Pool` is 17,217,536 bytes
([Memory](memory.md#sizes-at-runtime)), the unload margin is added to squared distances, the preload reads the last
search, `pendingDistance` is `FLT_MAX`, the camera's far clip is 115 with near 0.1 and a 65° view, the background and
fog colour is white, the ambient 0.157, and collected sectors are drawn last collected first in the BSP's order.

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[world_streaming]"` streams all 80 levels' worlds
(79 pairs and `objarena`) under a camera that visits the centre of every streamed sector, three frames each (15,945
frames), with the player camera's lens and the retail `Sector Pool` (charged with the worlds and parts only): 1,605
part reads, 43 parts freed, 435 frames short of room, every used part read at least once, none failed. Most atomics
resident at once: 598; budget peak 17,168,058 bytes. With the budget cut to the worlds plus 2 MB, `level51`,
`level83` and `level54` stream with 723 parts freed, 752 read and 451 frames short of room, peak 3,753,423 bytes; in
both runs no frame breaks an invariant (budget never exceeded and equal to what is loaded, every loaded part's atomics
present, no freed part seen last frame, the last part never freed, the margin always kept). `coney_tests
"[disc][world]"` also checks the BSP: in all 15,915 planes of the 159 worlds the left subtree lies below the right
one on the plane's axis, the side `collectSectors` takes it to be.

**The implementer's questions, answered** (2026-10-04, from code; the contradictions are marked in the paragraph
above):

- **`0x3F0 +0x04`**: the texture-coordinate scale. The code shows both floats uploaded in one quadword; the microcode
  of the plain pipelines was not read, so for them the evidence stays Coney's visual check; the dual pipeline's
  microcode multiplies both texture-coordinate sets by it, confirmed (code) ([Pipelines](#pipelines)).
- **Vertex colour range**: consistent with 0x80 = 1.0, so Coney's doubling stays; the CPU halves textured material
  colours for the same reason ([Pipelines](#pipelines)). The lit result is clamped at 1.0 before the material
  multiplies it ([Lighting: the maths](lighting.md#world)), confirmed (runtime). The world is lit by Coney's light manager
  ([Lighting](lighting.md#coneys-implementation)).
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

- **The microcode**: how the VU1 programs at `0x005045a0` and `0x004ff1c0` use the `0x3F0` scales, the
  prelighting and the lights. The dual program `0x004fc870` scales both texture-coordinate sets by `+0x04`
  ([Pipelines](#pipelines)); its lighting part was not read.
- **MatFX effect 1** (bump map, 2,467 materials): which pipeline it ends up with, and whether it draws differently.
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
- **Runtime confirmation** with PCSX2: a breakpoint on `0x00412310` while walking through a level would show the
  order parts are requested in; a watch on world `+0x0000` the reads in flight.
