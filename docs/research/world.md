# The streamed world

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims: PCSX2 was
not running when this page was written. The disc-side checks (2026-10-04) read the NTSC-U disc's WAD with throwaway
scripts outside the repository and are reported as names, counts and sizes only.

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
| `0x00411880` | `World_PendingDistance` | distance to the sector found by `0x00411eb0` | confirmed (code) |
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

**347 part files are never loaded:** parts numbered above `partCount` (31,046,766 bytes). The loader only asks for
parts that sectors name, and no sector names them; they look like leftovers of earlier builds (inferred).

### Atomic plugin `0x3F0` {#atomic-plugin}

Every atomic gets 16 bytes (offset in `0x0050cd98`; registered by `0x001927d8`); 12 of them are streamed, confirmed
(code) at `0x00192618` (defaults), `0x00192688` (reader), `0x00192740` (writer):

| Offset | Stream | Default | Meaning | Evidence |
| --- | --- | --- | --- | --- |
| `+0x00` | float | 1.0 | uploaded to VU memory by the game's four custom PS2 pipelines (`0x004252c8`, `0x00426430`, `0x004275d0`, `0x00428410`) | confirmed (code); meaning speculative |
| `+0x04` | float | 1.0 | uploaded next to `+0x00` | confirmed (code); meaning speculative |
| `+0x08` | u32 | 0 | read by `0x004290d8` | confirmed (code); meaning unknown |
| `+0x0c` | | 0 | not streamed: the game object that owns the atomic (getter `0x00192870`, used by the object renderers) | confirmed (code); meaning inferred |

**Disc check (corroboration):** in the streamed worlds' atomics the two floats are always powers of two from 2⁻⁸ to
2⁻¹⁵ (for example 2⁻¹⁰ and 2⁻¹¹), and `+0x08` is always 0. Powers of two fed to a vertex program read like
dequantisation scales for packed vertex data (texture coordinates and positions, speculative). They matter only if
Coney decodes the PS2 native geometry itself; see [Open questions](#open-questions).

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
   thing (return 2). If there is nothing to unload, return 1 or 3.

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
`0x00411880` takes the square root of the result for the sector found by the last search.

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
    - never below `60 - 10 × viewports` and never above the camera's own far clip.

    So the view closes in on scenery that is not loaded yet rather than showing holes.
4. Far clip = draw distance; fog distance = draw distance × the device's fog start.
5. **The level world** (the small world in the `.lev`, [Level loading](level-loading.md#the-level-file)): culling off,
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

## Coney's implementation

Not yet implemented.

What the implementer needs:

- **Readers** for the three files: the manifest; the world stream with a librw world reader (section `0x0B`) that
  keeps the `0x3F1` sector data (20 bytes: index, part, origin); the part file (skip 16 bytes, texture dictionary,
  count, `{u32 index, atomic}` pairs) with the `0x3F0` atomic data (two floats and a word). Unknown extensions can be
  skipped; these two must not be.
- **Texture lookup**: the world's and each part's dictionary are loaded without being made current, so materials find
  their textures through the [global lookup](graphics.md#texture-lookup) across every loaded dictionary.
- **Placement**: each part atomic gets a frame translated to its sector's origin.
- **Streaming** with the behaviour of [Choosing what to stream](#streaming): one decision a frame, nearest missing
  sector first (visible ones first), farthest unseen part out when memory is short, a five-unit hysteresis, never the
  last part. Coney can read synchronously at first, but the test mode needs the decisions to be deterministic, so
  "now" and file completion must come from the engine's clock and file layer, not the wall clock.
- **Memory**: Coney has no 32 MB limit, but the eviction order is visible to the player (what pops in where), so a
  budget that mimics the original's `Sector Pool` is worth having as an option
  ([Memory](memory.md#what-a-reimplementation-must-keep)); tables sized from the data, not 560 sectors and 140
  parts.
- **Rendering** in the order of [A frame](#a-frame): the level world, `s` world sectors (back-face culling), opaque
  objects, `d` world sectors, water, translucent objects; a one-second fade-in per newly loaded atomic; the
  draw-distance adjustment; frustum, PVS and occluder culling (PVS and occluders can come later: they only save work).
- **Disc test**: every world and part on the disc parses (159 worlds, 1,564 used parts, 5,315 atomics), with the
  counts in [Disc counts](#disc-counts).

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

- **Can librw draw the part atomics?** They are PS2 native geometry built for the game's own pipelines (right-to-render
  plugin 3, `0x30082`-`0x30088`), and the pipelines upload the two `0x3F0` floats to the vector unit. Whether librw's
  PS2 native-geometry reader decodes this vertex layout correctly, and whether the floats are dequantisation scales
  it must apply, is not checked. A first test: read one part with librw and compare its decoded vertex range with
  the sector's box.
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
