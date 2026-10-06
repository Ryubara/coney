# Level loading

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Its one runtime figure (the
`Sector Pool`'s size) comes from [Memory](memory.md#sizes-at-runtime), PCSX2 2.9.94. The disc-side checks (2026-10-04)
read the NTSC-U disc's WAD with throwaway scripts outside the repository and are reported as names, counts and sizes
only.

## Purpose

What happens between "start this level" and the first frame of play: which files are read, in which order, into
which memory, what stays loaded for the whole level and what streams in and out while it runs, and what is thrown
away when the level ends. It is what the milestone after the main menu needs: load a level and draw its world. The
same code loads the front end's own level, `level100`, under the menus ([Start-up and the front end](frontend.md)).

In one paragraph: gameplay is game mode 1. Its `Enter` calls `InitLevel`, which resets the game's systems, has the
world manager load the level (first the two [streamed worlds](world.md)' layouts and textures, then the level file
`<level>.lev` with its collision, paths, sky and occluders), runs the level script's entry point, reads the object
list, loads the level's dependency list and then **preloads**: it loads the section's resource pack
`<level>_<section>.pak` and streams world parts and resources around the camera until they stop coming or 15 to 30
seconds have passed. From then on the world manager streams once a frame. `UnloadLevel`, called from mode 1's
`Exit`, takes everything down again.

## Original structure

Names are ours unless they come from a path or tag string.

| Address | Name | File | Role | Evidence |
| --- | --- | --- | --- | --- |
| `0x001582e0` | mode 1 `Enter` | `GameModes/` | timers, pads, `InitLevel` | confirmed (code) |
| `0x00158728` | mode 1 `Update` | `GameModes/` | one frame of play, with world streaming | confirmed (code) |
| `0x00158488` | mode 1 `Exit` | `GameModes/` | `UnloadLevel`, outro movie | confirmed (code) |
| `0x00158580` / `0x00158660` | mode 1 `Resume` / `Suspend` | `GameModes/` | play time bookkeeping, HUD | confirmed (code) |
| `0x0015fe90` | `InitLevel` | `GameModes/InitLevel.cpp` | the whole level start, below | confirmed (code) |
| `0x001607b8` | `UnloadLevel(keepLevelFile)` | `GameModes/` | the reverse | confirmed (code) |
| `0x0040d688` | `WorldManager_CreatePools` | `World/ps2/WorldManagerPS2.cpp` | `Sector Pool`, `Sector Pool 2` | confirmed (code) |
| `0x0040d900` | `WorldManager::WorldManager` | same | `Global Data Pool` clump, `warriors.glr` | confirmed (code) |
| `0x0040dbb8` | `WorldManager::LoadLevel(name, headerOnly)` | same | the `World Level Pool` clump, the worlds, the `.lev` | confirmed (code) |
| `0x0040e2d8` | `WorldManager_Preload(radius, budgetMs, value, pack)` | same | the blocking preload | confirmed (code) |
| `0x0040f8a0` | `WorldManager_Update` | same (inferred) | the per-frame streaming decision ([The streamed world](world.md#streaming)) | confirmed (code) |
| `0x0040e8d8` | `WorldManager_Render(viewport)` | same | the world part of a frame ([The streamed world](world.md#a-frame)) | confirmed (code) |
| `0x0040f5b8` | `WorldManager_Unload(keepLevelFile)` | same | the worlds, the water, the level file | confirmed (code) |
| `0x0040c688` | `WorldLevel_Load(name)` | `World/` (between tolua and `WorldLevel.cpp`) | `<name>.lev` through the chunk system | confirmed (code) |
| `0x0040ce30` | chunk `0x17` handler | `World/ps2/WorldLevelPS2.cpp` region | builds the level object | confirmed (code) |
| `0x0040cf40` / `0x0040cf80` | level object constructor / destructor | `World/ps2/WorldLevelPS2.cpp` | | confirmed (code) |
| `0x0040cdd8` | `LevelModel_LinkFirstTexture` | same | gives a sky, cloud or skyline model its pipelines and its texture | confirmed (code) |
| `0x0040d088` | `LevelObject_SetOccluders` | same | stores the occluders and converts their points (`0x0017a560`) | confirmed (code) |
| `0x0040d0a8` | `LevelObject_RenderBackground(viewport)` | same | sky, clouds and skyline, before the world | confirmed (code) |
| `0x00156408` | (none) | `GameModes/` region | the per-viewport passes of a frame | confirmed (code) |
| `0x0040c7f0` | `WorldLevel_Release` | `World/WorldLevel.cpp` | lights, subtitles, string tables | confirmed (code) |
| `0x00187c38` | `ResourceManager_LoadPack(name, block)` | `Graphics/ResourceMgr.cpp` | a `.pak`, grouped container | confirmed (code) |
| `0x00187d28` | `ResourceManager_MakeRoom(bytes)` | same | evicts unused resources until a block of that size is free | confirmed (code) |
| `0x00178bc8` | `ResourceManager_LoadDependencies(crc, block)` | `Graphics/` | the dependency list entry of a name | confirmed (code); role from [Chunk system](chunk-system.md#chunk-type-table) |

## Data

### Memory {#memory}

The PS2 has 32 MB, and a level's data is far larger than that ([Disc counts](world.md#disc-counts)). Everything a
level loads lives in one big heap created at start-up, the `Sector Pool`, as **clumps**: bump allocators that each
take one block of the pool and are freed as a whole. Confirmed (code) for the creation, the kinds and the sizes;
names are the original's tag and registration strings. How heaps, clumps and the heap stack work, and the rest of
the pool tree: [Memory](memory.md).

| Pool | Kind | Created by | Size | Holds |
| --- | --- | --- | --- | --- |
| `Sector Pool` (`0x006eb9c8`) | heap, in the global heap | `0x0040d688`, at start-up | the largest free block of the global heap minus 128 KB: 17,217,536 bytes on a retail boot ([Memory](memory.md#sizes-at-runtime)) | everything below |
| `Sector Pool 2` (`0x006eb9cc`) | heap, in the global heap | `0x0040d688` | what is left minus 128 KB, at least 4 KB | nothing: unused ([Memory](memory.md#the-pool-tree)) |
| `Global Data Pool` | clump, in the `Sector Pool` | `0x0040d900`, at start-up | 101 % of `warriors.glr` | the game-wide lists (sounds, music, characters, objects, particle pages, animations, dependencies), for the whole game |
| `World Level Pool` | clump, in the `Sector Pool` | `0x0040dbb8`, per level | 103 % of `<level>.lev`, at least 256 KB | the [level file](#the-level-file) |
| one per world, named after it | clump, in the `Sector Pool` | `0x00410648`, per level | the manifest's world heap size | the world's texture dictionary and BSP ([The streamed world](world.md#loading-a-world)) |
| `Sectors<i>`, one per loaded part | clump, in the `Sector Pool` | `0x004110c0`, while playing | the manifest's part heap size | one part's textures and atomics |
| one per resource group, named after the resource | clump, in the `Sector Pool` | the resource manager (`0x00186e88`) | the group's size | props, characters, animations from packs (`Graphics/ResourceMgr.cpp`, not yet on a page) |
| `Level Dynamic & LUA Pool` | heap, in the global heap | at start-up ([Boot](boot.md#initialisation-order)) | 2,027,520 bytes | the Lua state, every STL container, the clumps' own 0x18-byte objects and temporary objects such as the stream wrappers the loaders create |

Before a world or a part gets its clump, the resource manager is asked to make room
(`ResourceManager_MakeRoom`, `0x00187d28`): it repeatedly frees the resource with the oldest time stamp that nothing
holds, across its seven resource lists, until the `Sector Pool`'s largest free block is at least the size asked.
Confirmed (code): the resource manager's pool is the `Sector Pool` (`0x0040f850` returns `0x006eb9c8`). So scenery
and props compete for one pool.

Part files are read asynchronously into the file manager's 384 KB `File Stream Buffer`
([File I/O](file-io.md#filemanager)) and parsed from there (`0x004114d8`, confirmed (code)). **Disc check
(corroboration):** the largest part file is 346,962 bytes, under 393,216.

### The level record {#the-level-record}

Completes [the level table](frontend.md#the-level-table). Confirmed (code) at the cited readers:

| Offset | Read by | Meaning |
| --- | --- | --- |
| `+0x04` | `InitLevel` | a level number: names the intro movie `L%d_IN`, and chooses the preload budget (below 101: 15 s) |
| `+0x08` | `InitLevel` | number of sections (compared with the current section, `W_GameState + 0x33a`) |
| `+0x0d` bit `0x02` | `InitLevel` | play the intro movie |
| `+0x0d` bit `0x04` | mode 1 `Exit` | play an outro movie (format string `0x0054ed20`) |
| `+0x14` | `InitLevel`, `LoadLevel` | the level name: `<name>.lev`, the script entry, the object list, the dependency list, the pack |
| `+0x24` | `InitLevel` | copied to `W_GameState + 0x134`; `UnloadLevel` hashes it to release the dependency list |
| `+0x39` | `LoadLevel` | the **world name**, `<world>s` / `<world>d` |

`W_GameState + 0x33a` is the current **section** of the level (1-based; inferred: it names the pack
`<level>_<section>.pak`, `InitLevel`'s debug auto-advance steps it up to `+0x08`, and the packs on the disc run
`_1` to `_12`). Where [Front end](frontend.md#initlevel) read it as a player count, it is this section number.

**Disc check (corroboration):** for all 64 `.lev` files the world name equals the level name: `<level>s_sec.wld` and
`<level>d_sec.wld` exist. Fifteen more world pairs have no `.lev` of their name (`level70`-`level74`, `level90`,
`level91`, `level94`, `level96`-`level98`, `level106`, `level117`, `level125`, `level135`), and `objarena` has one
world and no `.lev`. All but `objarena` are in the level list of `config_preload3.lua`. Since `LoadLevel` reads
`<name>.lev` unconditionally, those levels cannot load as they stand (inferred; see [Open questions](#open-questions)).
160 named packs follow `level<N>_<k>.pak`: 36 levels have one section, the others 2 to 12.

### The level file (`.lev`) {#the-level-file}

A flat [chunk container](chunk-system.md#container-layout). **Disc check (corroboration):** all 64 `.lev` files hold
the same 18 chunks in the same order. The reader is the chunk type's handler ([Chunk system](chunk-system.md#chunk-type-table));
"raw" chunks are only pushed, for a later handler to pop. Confirmed (code) at the cited readers; sizes are the range
over the 64 files.

| # | Chunk | Reader | Layout | Becomes | Ends up in | Size on the disc (bytes) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `0x53` Occluders | raw; popped by the `0x17` handler (`0x0040d088`) | `u32` count, padding to 16, then count records of 0x70 ([Occluders](#occluders)) | | level `+0x28` | 16-1,136 |
| 2, 4, 6 | `0x2A` texture dictionary | `0x00190770` (device slot `+0x150`) | RenderWare texture dictionary, PS2 native | `0x0B` | level `+0x10`, `+0x18`, `+0x20` | 624-132,512 (all four) |
| 3, 5, 7 | `0x47` Preinstance Object | `0x0017f2c0` (as `0x09`) | RenderWare clump of one atomic | `0x41` | level `+0x14`, `+0x1c`, `+0x24` | 960-339,840 |
| 8 | `0x2A` texture dictionary | `0x00190770` | the glow textures | `0x0B` | level `+0x08` | (above) |
| 9 | `0x15` Sector BSP Data | `0x00197b30` (device slot `+0x170`) | RenderWare world | `0x42` | level `+0x0c` | 1,072-28,928 |
| 10 | `0x40` PathData | `0x0024e720` | fixed up in place ([Path data](#path-data)) | | globals `0x00510584`, `0x0051058c` | 208-373,248 |
| 11 | `0x52` Collision Checked | raw; popped by `0x03` | bit set | | mesh `+0x94` | 16-1,136 |
| 12 | `0x07` Collision Vertex Buffer | raw; popped by `0x03` | `float[4]` vertices | | mesh `+0x8c` | 160-86,416 |
| 13 | `0x04` Collision Triangles | raw; popped by `0x03` | 10-byte triangles | | mesh `+0x88` | 144-90,512 |
| 14 | `0x05` Collision Grid | raw; popped by `0x03` | `u32` per cell | | mesh `+0x78` | 16-229,152 |
| 15 | `0x06` Collision Strings | raw; popped by `0x03` | triangle index lists (`u16`) | | mesh `+0x80` | 48-54,592 |
| 16 | `0x03` Collision Mesh | `0x00350580` | 160-byte header | the mesh, as an object | level `+0x04` | 160 |
| 17 | `0x17` Level Header | `0x0040ce30` (installed at run time) | 48 bytes of stale tool pointers, overwritten | the level object, as an object | world manager `+0x40` | 48 |
| 18 | `0x51` Subtitles | `0x001cab90` | not decoded here | | `0x0050ea74` | 16-30,128 |

The collision chunks and their queries: [Collision](collision.md).

### The level object {#the-level-object}

`0x0040ce30`, the `0x17` handler, builds the level object **in place, on the `0x17` chunk's own 48 bytes**: the
constructor `0x0040cf40` writes the vtable `0x00545b88` and zeroes 11 words (0x2c bytes). It then takes the collision
mesh from the object stack and pops, in this order, `0x42`, `0x0B`, then three `0x41`/`0x0B` pairs and the `0x53`
occluders, links each pair (`0x0040cdd8`) and pushes the level as an object, which `WorldLevel_Load` (`0x0040c688`)
pops for `LoadLevel`. Confirmed (code) for the fields; the roles of the three models come from their texture names
and their drawing (inferred):

| Offset | What | Read by |
| --- | --- | --- |
| `+0x00` | vtable `0x00545b88` | |
| `+0x04` | [collision mesh](collision.md) | `WorldManager_RayCast` (`0x0040db88`) |
| `+0x08` | texture dictionary of the level world's glow sprites | destructor |
| `+0x0c` | the **level world**: the light glows (below) | `WorldManager_Render` (`0x0040e8d8`) step 5 ([The streamed world](world.md#a-frame)) |
| `+0x10`, `+0x14` | dictionary and model of the **skyline** (textures `shadow`, `shadow_<area>`) | `LevelObject_RenderBackground` (`0x0040d0a8`) |
| `+0x18`, `+0x1c` | dictionary and model of the **sky box** (`skybox_<name>`) | same |
| `+0x20`, `+0x24` | dictionary and model of the **cloud box** (`cloudbox`) | same |
| `+0x28` | occluders | `0x0017a610` (per viewport), `0x0017a738` (the visibility pass) |

**Linking a model** (`0x0040cdd8(clump, dictionary)`), confirmed (code): it gives the clump's first atomic the game's
pipelines (`0x00426c78`, [Pipelines](world.md#pipelines)) and sets the **first material's texture to the
dictionary's first texture** (`0x0046d010`; the texture's reference count at `+0x54` goes up). So each of the three
models shows its dictionary's first texture, whatever its material names.

**The models on the disc (corroboration):** every model is one atomic with geometry format `0x0101000f` (prelit, no
normals). The skyline has 2 to about 4,944 triangles (8 levels have a 2-triangle placeholder); the sky box 12 to 138;
the cloud box 28.

**The level world** (`+0x0c`): the disc's `0x15` chunks are each a RenderWare world with **one sector**, 4 to 324
triangles (6,870 in all), no planes, geometry format `0x4101000d` (PS2 native, prelit, textured, one texture-coordinate
set, no normals). Its textures are only `propglow…` sprites (63 of 64 levels name one texture), from the `+0x08`
dictionary: `propglow02k` in 38 levels, `01k` in 17, `03k` in 5, `05k` in 2, `02k` with `06k` in 1. So the level world
is inferred to be the **light glows**, the halos around street lamps and windows, placed once for the level and drawn
before the scenery with culling off.

**Lifetime**, confirmed (code) unless stated: the object lives in the `World Level Pool`; the world manager holds it at
`+0x40` from `LoadLevel` to `WorldManager_Unload(keep = 0)` (`0x0040f5b8`), which calls `WorldLevel_Release`
(`0x0040c7f0`: resets the light manager `0x0050cce4` through `0x0017e680`, the subtitles through
`0x001cafa0(0x00619570)`, frees the string tables), frees the path data (`0x0051058c`) and destroys the pool. The
destructor `0x0040cf80` frees the collision chunks (`0x004f4b78`) and the occluders (`0x004f4bf8`), destroys the level
world (device slot `+0x178`) and its dictionary (slot `+0x158`) and the three clumps (`0x004693f0`) and dictionaries
(`0x00488c58`). No call to it was found on the unload path; whether it runs at all is open.

### Occluders (chunk `0x53`) {#occluders}

Confirmed (code) at `0x0017a560` (load), `0x0017a8d8` (per viewport) and `0x0017a738`/`0x0017abe0` (test).

A `u32` count, padding to 16 bytes, then one 0x70-byte record per occluder:

| Offset | Meaning |
| --- | --- |
| `+0x00`, `+0x10`, `+0x20` | three planes, computed per viewport |
| `+0x30`-`+0x60` | four points `P0`-`P3`; at load converted from game axes to RenderWare axes, `(y, z) → (z, -y)` |
| `+0x6f` | active this viewport |

An occluder is a vertical wall: on the disc, `P2 = P0 + (0, 0, 1)` and `P3 = P1 + (0, 0, 1)` in game axes for all
66 occluders (0 to 10 per level). Per viewport (`0x0017a8d8`), it is inactive when `P0` and `P1` are both behind one
of the frustum planes 0, 1, 2 or 5; otherwise:

- plane 0 goes through the camera, `P0` and `P2`, oriented so `P1` is on the positive side;
- plane 1 through `P1`, the camera and `P3`, oriented so `P0` is positive;
- plane 2 through `P0`, `P2` and `P1`, oriented so the camera is negative.

A box is **hidden** when all 8 of its corners are on the positive side of all three planes of an active occluder: in
the wedge the wall casts away from the camera, behind the wall. The test is only applied when the box's float at `+0x14`
is at least -50.0 (`0x0050ccc4`; which coordinate that is was not traced). The wall is infinite upward (no top plane).

### Path data (chunk `0x40`) {#path-data}

`0x0024e720` fixes the chunk up in place; `0x0024eef0`, `0x0024ea60`, `0x0024f290` and `0x0024f718` read it.
Confirmed (code) for the layout. The AI walks the polygons and plans routes over the C records (nodes) and D
records (edges) ([AI, Path planning](ai.md#path-planning)). In order:

| Part | Size | Contents |
| --- | --- | --- |
| Header | 0x20 | `+0x00` path count P, `+0x04` vertex count, `+0x08` `s16` C count, `+0x0a` `s16` A count, `+0x0c` D count; `0x00510584` = chunk + 0x10, and `+0x14` is overwritten with a pointer to the C records |
| A records | 16 each | pointed to by the paths that have one (`+0x4c`): `+0x00` the polygon's node count, `+0x08` the index of its first C record |
| Vertices ("B") | 16 each | x, y at `+0x00`, `+0x04`; each path owns the next `+0x00` of them |
| Paths | 0x50 each | below |
| C records | 32 each | route **nodes**: `+0x00` position (vec4), `+0x10` pointer to its D records, `+0x14` `s16` their count, `+0x16` / `+0x18` / `+0x1a` the search's f / g / h (`u16`), `+0x1c` its parent, `+0x1f` the routes using it (cleared at load) |
| D records | 8 each | route **edges**: `+0x00` an index into the C records, turned into a pointer; `+0x04` a word whose low 16 bits are the edge's flags and bit 31 an "avoid" bit |
| Edge lists | `s16` | below; the global `0x006ca220` points at their start |
| Tail | 4 to 18 bytes | at least 4 bytes, then padding to a multiple of 16 (below) |

**A path is a polygon** (an area on the ground, inferred from the test below):

| Offset | Meaning |
| --- | --- |
| `+0x00` | `s16` vertex count *n* |
| `+0x04` | pointer to its first vertex |
| `+0x08` / `+0x0c` | x minimum / maximum |
| `+0x10` / `+0x14` | y minimum / maximum |
| `+0x20` / `+0x24` | next pointers (two chains, built at load) |
| `+0x28` | 16 `s16`: the start of each **slab**'s edge list, −1 when there are none |
| `+0x48` | flags (bit `0x10` cleared at load) |
| `+0x4c` | A record or 0 |

**Inside test** (`0x0024eef0`, called from `0x00250100`): reject a point outside the box; take its **slab**, `floor((y
− ymin) × 16 / (ymax − ymin))`; walk that slab's list of edge numbers (an edge *k* runs from vertex *k* to vertex
`(k + 1) mod n`) up to the next negative value, and add +1 or −1 for each edge that crosses the point's row to its
left, by the edge's direction (edges flatter than 0.0001 in y are skipped). The point is inside when the sum is
positive (a winding number). A path whose first slab start is negative has no lists, and every edge is walked.

**Where each part starts** (confirmed (code) at `0x0024e720`): the A records at chunk `+0x20`, the vertices after
the A count's records, the paths after the vertex count's, the C records after the paths, the D records after the C
count's, and the edge lists right after the D count's, where `0x006ca220` ends up once the C records' D pointers are
handed out. Nothing between the D records and the lists is skipped, and the loader reads nothing after them; it also
stores the lists' start plus 0x20 in `0x006ca224`, but no code reads that global (no other reference in Ghidra).
Each path takes the next `+0x4c`-flagged A record in order (the stored value only says whether it has one), and each C
record the next `+0x14` of the D records.

**Sizes** (disc check, NTSC-U, 2026-10-04, counts only, over all 64 `.lev` files, every one a `level<N>.lev` with
paths): the header's counts hold in every file: the paths' vertex counts add up to the vertex count, the paths with an
A record to the A count, and the C records' D counts to the D count. With the edge lists measured to the end of the
furthest list a path refers to, every chunk is exactly

```text
align16(0x20 + 16·A + 16·V + 0x50·P + 32·C + 8·D + edge-list bytes + 4)
```

so the tail after the lists is 4 to 18 bytes, 770 in all: padding left by the tool that wrote the files (inferred; in
the 12 files without lists it is all zeros in 7 and holds other values in 5, and the loader reads none of it). The
totals: 10,992 paths, 68,000 vertices, 1,754 A, 43,234 C and 252,896 D records, 4,912 slab lists in 52 of the files (the
other 12 have none) taking 77,678 bytes.

**The 79,472 bytes** Coney's disc test once reported past the counted records (with a 16-byte header) were
therefore 64 × 16 = 1,024 bytes of the 0x20-byte header, the 77,678 bytes of edge lists and the 770 bytes of tails.
No record size or count is missing.

**The records' values** (disc check, NTSC-U, 2026-10-06, counts only, all 64 files; `coney_tests "[disc][routes]"`
and a one-off count):

- **A records:** the node count is a 16-bit value (the upper half is always 0), and the counts add up to the C count
  in every file, so the nodes belong to the paths with A records in order. `+0x08` is not that running index in every
  record (in some it is past the C records), so its meaning is open.
- **Winding:** 1,593 paths are anticlockwise (x right, y up), among them 1,584 of the 1,754 with route nodes; 9,399
  are clockwise, nearly all with path flag 1 or 2 (`+0x48`; no path has flag 8). Counting an edge going down as +1,
  42,373 of the 43,234 route nodes lie inside the path that owns them, and no point lies inside a clockwise path.
- **Edges:** each has exactly one flag: 1 (238,422), 2 (2,126), 4 (5,890), 8 (982), `0x10` (746, every one with bit
  31 set) and `0x80` (4,730); 250,963 have an edge back. Path `+0x02` is not 0 on 1,263 paths.

### The Object List's models {#the-object-list}

The **Object List** of `warriors.glr` finds an object type's model and texture dictionary by its name, as the
[Character List](characters.md#files) does for characters; its record layout is on
[WAD contents](formats/wad-contents.md#object-list).

**The models** (all 1,406, disc check): 1,400 are clumps of one atomic and one geometry, like the level file's
[preinstanced models](#the-level-object), drawn by the game's world pipeline `0x30083` with the atomic plugin `0x3F0`;
the six cars' hold 47, one per part and damaged part ([Cars](cars.md#model)). Every geometry has one material,
untextured (no texture section), and every dictionary is one `0x2a` chunk. How the game binds the dictionary's texture
to the material is not traced; Coney's renderer gives the material the dictionary's first texture, as the level file's
models are linked, and the images look right. Read in RenderWare's axes, the models stand with **y up and their front
towards +z**: chairs, carts, amps and doors stand upright and face the camera when y is turned to z and z to y (with x
to -x), which is the turn Coney's reference images use (`kObjectToPose`, Coney's choice checked by eye).

### The world manager (0x60 bytes) {#world-manager}

`0x005147c4`, built by `0x0040d900`. Confirmed (code) for the offsets used on these pages:

| Offset | Meaning |
| --- | --- |
| `+0x0c`-`+0x24` | a queue of pack names to load at the end of the preload (`0x0040e1a0` appends) |
| `+0x2c` | a preload is running |
| `+0x34` | "nothing more to load" latch of the streaming update |
| `+0x38` | the `World Level Pool` |
| `+0x3c` | the `Global Data Pool` clump |
| `+0x40` | the level object |
| `+0x44` | the `s` world (the only world for a single-world level) |
| `+0x48` | the `d` world, or null |
| `+0x50`-`+0x58` | the list of sectors to draw this viewport ([Visibility](world.md#visibility)) |
| `+0x5c` | the water effect (`Graphics/WaterEffect.cpp`), or null |

## Behaviour

### Entering gameplay (mode 1) {#mode-1}

Mode 8 pushes mode 1 once a level is chosen ([Front end](frontend.md#mode-flow); a new story game chooses `level99`,
section 1, [Front end](frontend.md#story-start)). Mode 1's `Enter` (`0x001582e0`), confirmed (code), in order:

1. Audio: the manager's fields `+0x3fa58` = 1, `+0x3fa5c` = 1.0, `+0x3faac` = 0.2, `+0x3fab0` = 0.75, volume 0.1
   (`0x001110c8`); three channels cleared (`0x001104c8`).
2. The pending level change `0x0050c754` = 0.
3. Take `GameTimer` (fields `+0x5c` = 1, `+0x60` = 0), call its own `Resume`, read the pads once (`Pads_Update`).
4. **`InitLevel`** (`0x0015fe90`, [below](#initlevel)).
5. Clear five globals (`0x0050c550`-`0x0050c560`), reset the timer, call `0x0041a460(gameState, -1)` (sets up the
   player slots and their pads; inferred from its use for the second player's join, below) and set `+0x28` = 180.

`+0x28` is the **level-end countdown** in frames, read only while `W_GameState + 0x14c` is 1 or 2 ([A frame of
play](#a-frame-of-play)); it does not hold input at the start of a level (an earlier version of this page said so).
Confirmed (code) at `0x00158728`.

### InitLevel {#initlevel}

`0x0015fe90`, in this order. Confirmed (code) for the order; callee roles inferred from their files and arguments
where not stated.

1. Volume 1.0; read the current level record and section; reset `GameTimer`, the task manager, the audio, the game
   state, the scene system, the cameras, the screen effects, the actionables, the falling embers; set up the save
   buffers (one set for section 1, another for later sections); device slot `+0x10`.
2. If the record asks for an intro movie and this is section 1: reserve 3,200,000 bytes in the resource manager's
   heap for it (freed again just before the movie plays, step 12).
3. HUD reset; if the resource manager has no generic header yet, load it (`0x00184eb0`); copy the level's names into
   `W_GameState + 0x124` and `+0x134`.
4. **`WorldManager::LoadLevel(name, 0)`** ([below](#worldmanager-loadlevel)).
5. Reset the AI, path and character tables; create the task-manager objects `load` and `Wind_Manager`; HUD and
   audio set-up.
6. **Level script**: script system slot `+0x24` with the level name.
7. **Object list**: `<name>_objs.txt` (or `../levels/<name>/<name>_objs.txt` on the host file system) into the task
   manager's object list (`0x00398598`); add the `CrimeScene` and `GangCall` [flags](flags.md#sources) at the origin.
8. **Dependency list**: `0x00178bc8(resourceManager, crc32(name), 1)` loads the resources the level's entry in the
   dependency list names, blocking.
9. Camera: `0x0011e878(0.17)`; set the camera's draw distance to its far clip.
10. **Preload**: `WorldManager_Preload(500.0, worldManager, budget, 0, "<name>_<section>")` with a budget of 30,000 ms,
    or 15,000 ms when the record's `+0x04` is below 101 ([below](#preload)).
11. Audio, game state and script-system bookkeeping; start the level's music, or the track `sound` when the level's
    track is `none`; `0x00161378`; service the file manager.
12. Intro movie `L<n>_IN` (`n` = record `+0x04`) when step 2 reserved memory.
13. Call the pending Lua function `0x005e6d88` if one is set.
14. Debug only: when the auto-advance flag `0x0050c7bc` is set, ask for the next section or level at once
    (`W_GameState + 0x14c` = 3, `0x00160d78`).
15. Remember the level and section (`0x0050c7c0`, `0x0050c7c4`); distortion effects, HUD per player.

### From STORY to the player in level99 {#story-into-level99}

What happens between choosing STORY and controlling Rembrandt, in order. The front-end half (the profile screens,
`Menu.startGame`, `runNextMission`, the one frame of the mission-complete mode, mode 8 pushing mode 1) is on
[Front end](frontend.md#story-start); this is mode 1's half. C++ steps are confirmed (code) at `InitLevel`
(`0x0015fe90`); the script steps are inferred from the disassembly of `global.lua` and `level99.lua`
([Scripts](scripting.md#level99)).

1. **Mode 1 `Enter`** ([above](#mode-1)): audio, timers, then `InitLevel` with record 1 (`level99`) and
   checkpoint 1 (`W_GameState + 0x33a`, set by `runNextMission`'s `SetCheckPoint(1)`).
2. **Reset and load the world** (`InitLevel` steps 1-5): the systems reset, 3,200,000 bytes reserved for the intro
   movie (record flag `0x02`, checkpoint 1), `LoadLevel("level99")` (the two worlds' layouts and textures, then
   `level99.lev`), the AI, path and character tables reset, the `load` and `Wind_Manager` objects.
3. **The level script** (step 6): the script system runs `global.lua` (its helpers, `CfgAmbient()`,
   `SetupLevelInventory()`), then `level99.lua`. Its main chunk adds the flags, boxes and paths
   (`AddFlagsBoxesPaths`), registers the objects and runs `Main`: `GetCheckPoint()` (1), the fog colour, `ReportCrime(0)`,
   the HUD calls, the `tMission` table, then `RunLevel`:
    - `CfgSetStatValue`, six `SetDynamicAnimation` clips (loaded later by the resource manager);
    - **the player**: `AddWarriors2` creates the gang `Warriors2` and `HuCreate("Rembrandt", 32, {-284.4, 120.4,
      0.3}, 0, ..., 1, gang)`, then Ash as player 2's character (index 2); `player = Warriors.Rembrandt`. The human is
      made, snapped to the ground and bound to pad 0 here ([Characters](characters.md#creation)); its model is
      attached once its resources are resident;
    - `preLoadFile("level99_combat", "Checkpoint1")`: an asynchronous read; the file manager delivers it later, and
      then the chunk runs and `Checkpoint1` is called ([Scripts](scripting.md#bindings-the-front-end-and-the-script-system-depend-on));
    - **the camera**: `AddCameras` makes the follow camera on `player` and activates it
      ([Camera](camera.md)); `SetStartGameCallback("StartAmbient")`; demigod mode for both, mugging off, commands 37
      and 38 off for the tutorial.
4. **Objects and resources** (steps 7-8): `level99_objs.txt` into the task manager, `CrimeScene` and `GangCall`, then
   the dependency list of `level99`, blocking.
5. **Camera and preload** (steps 9-10): the cameras update once (`0x0011e878(0.17)`), which puts the follow camera
   behind Rembrandt (inferred), and `WorldManager_Preload` loads `level99_1.pak` and streams the world within the
   camera's draw distance for up to 15 s (record `+0x04` = 99, below 101). The preload services the file manager, so
   the checkpoint script requested in step 3 may arrive here (inferred; not traced).
6. **Music, movie, start** (steps 11-13): the level's music; the intro movie `L99_IN`; then the start callback
   `StartAmbient`, which at checkpoint 1 runs `SuperRunScene(IntroScene)`, the in-engine intro (the scene is defined
   in `level99_combat.lua`, so that script must have run by now).
7. **The first frame of play**: mode 1's `Update` ([A frame of play](#a-frame-of-play)). The intro scene holds the
   camera and gives it back; `Checkpoint1` (`P1.SetupCombat`) has set up the tutorial's sections, objective and
   training enemies.

**For an implementer** the order that matters: the player and the follow camera exist **before** the preload, so the
preload streams the world around the player's start; the checkpoint script and the start callback come after, and
anything they reference (the intro scene) must be loaded by then.

### WorldManager::LoadLevel {#worldmanager-loadlevel}

`0x0040dbb8(worldManager, name, headerOnly)`, confirmed (code):

1. Look up the size of `<name>.lev` and create the `World Level Pool` in the `Sector Pool`: 103 % of it, at least
   256 KB.
2. Unless `headerOnly` (boot loads `level1` this way for the intro movie's subtitles, [Boot](boot.md#main)):
    - take the world name from the current level record (`+0x39`);
    - if `<world>s_sec.wld` exists: create two world objects (0x2188 bytes each, tag `World`), the first for
      `<world>s` at `+0x44`, the second for `<world>d` at `+0x48`;
    - otherwise one world, `<world>`, at `+0x44`, and `+0x48` = null;
    - for each, in that order: construct (`0x00410308`), read the manifest (`0x00410e08`), load the world stream
      (`0x00410648`). See [The streamed world](world.md#loading-a-world). No part is loaded yet.
3. With the `World Level Pool` current, load `<name>.lev` (`0x0040c688`) and pop the level object into `+0x40`.
4. Print the free memory; store the `Sector Pool`'s free size in `0x005147d0`; set the resource-distance scale
   `0x005147cc` to 0.001.

So the worlds' textures and layout are loaded **before** the level file, and both before any scenery geometry.

### Preload {#preload}

`WorldManager_Preload` (`0x0040e2d8(radius, worldManager, budgetMs, value, packName)`), confirmed (code):

1. Refuse to run twice at once (`+0x2c`).
2. Reset a per-player state on every player character.
3. **Radius**: with a camera and `value` = 0 (as `InitLevel` calls it), the radius is the camera's current draw
   distance, not the 500.0 passed in.
4. Start the real-time timer if it was stopped; if `0x001458c8()` returns 0, call `0x001458d8` with `value`, or
   `0x161600` when `value` is 0 (meaning not traced; set back to 0 at the end).
5. Service the file manager; flush the render queue (device slot `+0x18`); reset both worlds' visibility.
6. **Pack**: if `<packName>.pak` exists, load it with the resource manager (`0x00187c38(rm, name, 1)`) and pump the
   resource manager until it reports done or a file read is in flight.
7. Reset the visibility again and note the time; deadline = now + `budgetMs`.
8. **Stream**: call `WorldManager_Update` ([The streamed world](world.md#streaming)) repeatedly. While it reports
   work (2) before the deadline, service the file manager and the resource manager and keep going as long as the
   nearest missing sector (`0x0040e100`) is within the radius or there is none; other passes count, up to 200.
9. Pump the resource manager until it is idle; then load every pack in the queue at `+0x0c`.
10. Restore the timer and the `0x001458d8` value; give the camera back its draw distance; return the time taken.

### A frame of play {#a-frame-of-play}

Mode 1's `Update` (`0x00158728`) is the [in-game frame](boot.md#one-frame) with the streaming in it. Confirmed (code)
for the order; the roles of callees not named elsewhere are inferred from what they touch. While
`W_GameState + 0x14c` is 0 (playing):

1. Frame pacing (`0x00159468` with the device's value from `0x0018d020`); an empty hook (`0x001561f8`); the
   mission stopwatch (`0x004233f8` on `*0x0051504c`, [Scripts](scripting.md#stopwatch)); `0x0048d420`; the save
   system's sub-object at `+0x128`, slot `+0x1c`.
2. The task manager's phase-0 set-up (`0x003a3148`), the **cameras** (`0x001562c8`), the **tick** (`0x00156220`,
   which stores the frame's step in seconds at `mode + 0x20`), the debug frame counter (`0x001569c0`).
3. **The simulation**, when `0x005e536c` is 1 (during normal play; what clears it is not traced):
    - `0x00249b98` walks the 60 human slots and calls each live one's slot `+0xc4` (`0x0023bdb8`), which only marks
      the character's skeleton for an update (`+0x255` = 1, `0x00177240(+0xd8, 0)`); the humans themselves update
      as task-manager objects ([Characters](characters.md#update));
    - the task manager's phase 0 (`0x003a31a8`): the game objects' updates (inferred: the characters among them);
    - `CollisionMesh_UpdateEmpty` ([Collision](collision.md)).
4. **`WorldManager_Update`** and the resource manager's update (`0x00186068`), when `0x0050c694` is set (it is in
   `.data`).
5. If player 0 exists: for each player, the audio listener at the camera's matrix (camera slot `+0xac`) and the
   player's position raised by 1.8; then `0x0010f810` and the HUD (`0x001af010`). START pauses (`0x00154f28`), or
   on the second pad lets a second player join (`0x0041a460`, `0x0041b2f8`).
6. Service the file manager; `0x001562a0` (`0x00412ca0`, `0x00414398`); the per-viewport passes (`0x00156408`,
   [below](#render-order)); the overlays (`0x00156658`); the **script update** (scheduled calls,
   [Scripts](scripting.md#scheduled-calls)); device slot `+0x34` (present); the [cheat-code](debug.md#cheats) sequence
   check (`0x00163c68` against the table at `0x0050c7f8`); and the error check `0x00156200`, which switches to the error
   mode (`0x0015e7e8`).

**Leaving.** `W_GameState + 0x14c` drives the way out, confirmed (code):

- **1 or 2** (a level end): the countdown `+0x28` is capped at 90 frames and drops to 10 when cross (`0x40`) is
  pressed; a type-`0xc` camera gets a 6.5 s fade (`0x0018c988`). When it reaches 0, 1 leads to `0x00155408` or
  `0x001557f8` (chosen by `0x0041d110`) and 2 to `0x0015d420(0)`. **1 is a failure**: a player who falls more than
  20 m below the collision mesh sets it, with `+0x152` = 2, when the game-state flags `+0x150` have bit 1
  (`Human_StateUpdate`, `0x002403e8`, [Characters](characters.md#ground)). **2 is a level completed**: `0x00160d00`
  sets it together with the next level's index (`+0x56dc` + 1) for the load mode. Confirmed (code) for the writers;
  that 1 means "mission failed" in general is inferred from this one writer. A story mission's normal ending does
  not pass through here: its final scene pushes the mission-complete mode directly
  ([Scripts: how the mission ends](scripting.md#level99)), and messages stop reaching Lua once this state is not 0.
- **Any other value** (3 is what `MenuLoadLevel` sets): `Update` returns 0, which pops the mode; its `Exit` then
  unloads the level ([Leaving gameplay](#unload)).

### The level in a frame {#render-order}

The per-viewport part of a frame (`0x00156408`; `0x00156540` is a variant of it), confirmed (code). Each step runs for
every viewport before the next step starts:

1. Device slot `+0x88`: position the viewport's cameras.
2. Lights for the viewport (`LightManager_BeginViewport`, `0x0017ea60`, [Lighting](world.md#lighting)), then the
   **background** (`LevelObject_RenderBackground`, `0x0040d0a8`, below).
3. **The world** (`WorldManager_Render`, `0x0040e8d8`): the level world (glows), the `s` world, objects, the `d`
   world, water, translucent objects ([The streamed world](world.md#a-frame)).
4. Resources (`0x00185b38`).

Then `0x0017b2e0` and device slot `+0x118` (heat distortion) once.

**The background** (`0x0040d0a8(level, viewport)`), confirmed (code):

1. Save the camera's matrix, near and far clip.
2. Light the background as one object far away: `LightManager_SelectLights` (`0x0017de10`) with a sphere at
   (1e6, 1e6, 1e6), radius 1, world lights only (flags 2), no point lights; upload (`0x0017e810`).
3. If `0x005e5378` is 1 (it is): far clip 5.0, near 0.05, the camera's **translation zeroed** (the sky moves with the
   camera); Z write off, fog off, no culling. Draw the **sky box** (`+0x1c`). Then the **cloud box** (`+0x24`),
   rotated about RenderWare's `y` axis (up; `0x00511730`) by `t / 60000` radians, `t` the game time in ms
   (`0x0050b734 + 0x48`): one radian a minute.
4. If `0x005e537c` is 1 (it is): restore the camera's matrix; far clip 560.0; near clip the smaller of 39.0 and the
   distance to the nearest missing sector (`0x0040e100`, [The streamed world](world.md#streaming)); tell the PS2
   driver the far clip (`0x0048f0f8(4, &far)`). Fog off, Z write on: draw the **skyline** (`+0x14`); fog on.
   Then **clear Z only** (camera wrapper slot `+0x70(colour, 0, 1)`).
5. Restore near, far and the driver's value.

So the skyline is a far backdrop drawn in the real world position from 39 units out to 560, and the Z clear lets the
world, drawn next with its own (much shorter) far clip, cover it wherever it has geometry (inferred). Where the world
has not streamed in, the skyline shows instead of the background colour; its near clip comes closer than 39 when
nearby scenery is still missing, so it fills those holes too (inferred).

### Leaving gameplay {#unload}

Mode 1's `Exit` (`0x00158488`) stops the sounds and music and calls **`UnloadLevel`** (`0x001607b8`), then, when a
level change is pending (`0x0050c754`), plays the outro movie if the record asks for it and unloads the level file
too. `UnloadLevel(keep)`, confirmed (code) for the order:

1. Service the file manager; put the camera far away (a position of 100,000,000 on all axes); background black.
2. Release the level's dependency list (`0x00178bc8(rm, crc32(record + 0x24), 0)`).
3. With the `Level Dynamic & LUA Pool` current: free the game objects, paths, boxes and flags; draw two black frames
   so neither display buffer shows the old level; flush the render queue.
4. Reset the HUD, the task manager and its object lists, cameras and scripts.
5. **`WorldManager_Unload(keep)`** (`0x0040f5b8`): flush the render queue and the task manager, unload each world
   (`0x00410b70`: wait for a part being read, unload every loaded part, destroy the world, its dictionary and its
   heap), free the water effect, and unless `keep`: release the level object (`0x0040c7f0`: lights, subtitles,
   string tables), the path data, and destroy the `World Level Pool`.
6. The resource manager drops the level's resources (`0x00185ae0`, `0x00188aa8`, `0x00189718`); the light manager
   resets; the **Lua state is destroyed and created again** (`0x00356450`, `0x00356390`).
7. Timers, effects and the game state's level flags are reset.

### Resident and streamed {#resident-and-streamed}

| Data | Loaded | Lives until |
| --- | --- | --- |
| `warriors.glr` | start-up | the end of the game |
| the two worlds' texture dictionaries and BSP layouts | `LoadLevel`, synchronously | `UnloadLevel` |
| `<level>.lev`: collision, paths, occluders, sky, clouds, skyline, glows (the level world), subtitles | `LoadLevel`, synchronously | `UnloadLevel` |
| the level script, the object list | `InitLevel` | `UnloadLevel` |
| the dependency list's resources | `InitLevel`, blocking | `UnloadLevel` |
| `<level>_<section>.pak` | the preload, blocking | the resource manager evicts what nothing holds |
| world parts (`_ms<i>.sec`) | the preload, then streamed by distance | unloaded when far, unseen and memory is short |
| other resources | streamed by the resource manager | evicted least recently used |

## Coney's implementation

The streamed-world half of `LoadLevel`, the level file and the streaming of a running level exist (2026-10-04),
behind Coney's world viewer (`coney --view-world <level>`, [Building](../guides/building.md#the-world-viewer)), and
Coney's play mode plays a level with Rembrandt (`--play-level`). Details on
[The streamed world](world.md#coneys-implementation).

**Mode 1 and the player's start** (2026-10-05), written from [From STORY to the player in level99](#story-into-level99)
and [Characters](characters.md#level-starts):

- **`GameplayMode`** (`src/gamemodes/gameplay_mode.h`) is mode 1. The level flow selects the chosen level and pushes it
  ([Front end](frontend.md#coneys-implementation)). Its `enter` is `InitLevel` in the order that matters for the
  player: first the level script (`runLevelScript`, `src/gamemodes/level_start.h`: the humans of the level before are
  forgotten, then `global.lua` and `<level>.lua` run in the Lua state the front end's unload made, and their `HuCreate`
  calls are kept), then the level loads with player 1 at the position and heading the script gave him, and the preload
  streams the world around him. Its `update` is the level's step, then the scripts' frame; its `exit` makes a fresh Lua
  state (`UnloadLevel`'s script part).
- **`HuCreate`** is a real binding (`src/scripting/script_bindings.cpp`): it reads the name, the type, the position
  table, the heading and the player index, keeps them (`CreatedHumans`, `src/warriors/created_humans.h`, at most 60)
  and returns a handle, or `NilHandle` when all 60 are taken.
- **The loading and the player** are the play mode's (`src/platform/play_level_mode.h`), given to gameplay as a level
  loader by `main`. It snaps the start to the ground as `HuCreate` does (a 2.5 m ray from 1 m above).
- **`--play-level NAME [--checkpoint N]`** runs the same script step alone (`runLevelScriptAlone`): the preloads, a
  fresh state, `SetCheckPoint(N)` and the level's index, then the level script; the play mode starts the player there.
- **Disc check (NTSC-U, 2026-10-05, positions only):** `coney_tests "[disc][story]"` finds player 1 at the
  [Level starts](../references/level-starts.md) values for `level99` checkpoints 1 and 2, `level2` 3, `level3` 4 and
  `level5` 2, with the name and type each lists; `level99` checkpoint 1 makes 2 humans with no script error.

Coney's choices and stand-ins for mode 1:

- The rest of `InitLevel` (the object and dependency lists, the music, the intro movie `L99_IN`, the pending Lua call)
  and of mode 1's `Enter` (the audio, the level-end countdown) is not done. Its flags step (`CrimeScene`, `GangCall`)
  and its start callback are ([World flags](flags.md#coneys-implementation)); the callback runs before the level
  loads, and `preLoadFile` runs the checkpoint's script at once and then calls its callback by name. A teleport of
  player 1 by the scripts, at the start or later, moves the player. The player has control on the first frame.
- `HuCreate` does not snap the position or write it back into the script's table (no collision is loaded while the
  script runs; the play mode snaps it); the gang, the unused string and the flag are not kept.
- The player is drawn as the model his type names ([Characters](characters.md#coneys-implementation)), Rembrandt
  when there is none.
- A creation whose position is not a table of three numbers is kept without a position and counts as a start only
  when a teleport places it; otherwise the play mode falls back to its stand-in.
- A level that fails to load leaves gameplay's frame black, with the error logged.

- **The worlds in `LoadLevel`'s order**: `<level>s_sec.wld` decides between two worlds (`<level>s`, `<level>d`) and one
  (`<level>`); each is constructed, its manifest read and its world stream loaded, and no part is loaded
  (`src/platform/world_set.h`). The world name is the level name (the level table does not exist yet).
- **Memory**: one `SectorBudget`, charged first with the `Global Data Pool` and the `World Level Pool` (sized from
  `warriors.glr` and `<level>.lev`, though neither file is loaded yet), then the worlds and parts
  ([Memory](memory.md#coneys-implementation)).
- **The preload**: `preloadWorlds` (`WorldManager_Preload`, `src/world/world_streamer.h`) with the camera's draw
  distance as the radius, before the first frame: no pack, no time budget (reads are synchronous), and the passes that
  load nothing count up to 200.
- **A frame of play**, as far as the world goes: one `WorldManager_Update` decision, then the world's part of
  `WorldManager_Render`. Game time and scripted input make it deterministic.
- **Unload**: destroying the world set frees every part and world (`World_Unload`) and gives the budget back.
- **The level file** (`src/world/level_object.h`, platform-neutral; `src/platform/level_file.h`, the RenderWare parts):
  `loadLevel` reads `<level>.lev` through the chunk system with a handler for every chunk of
  [the table above](#the-level-file) but the subtitles. `0x03` builds the [collision mesh](collision.md#coneys-implementation);
  `0x40` checks the path data's header and pushes it as an object; `0x47` rearranges each atomic of its clump into a
  standalone atomic with the geometry it names (`extractClumpModels`), reads it as a game-pipeline atomic, places it
  by its frames and unpacks it, and pushes one atomic as a model, several (a car's) as a `LevelClumpObject`; `0x15`
  rearranges its world's one sector the same way (`extractLevelWorld`) and reads it in RenderWare's own default PS2
  layout (`decodePs2DefaultMesh`: float positions and texture coordinates), with the glow dictionary just below it
  registered for the texture lookup during the read; `0x17` pops everything in the original's order into a
  `LevelObject`, linking each model to its dictionary's first texture (`linkLevelModel`). The loader then takes the
  raw subtitles and fails if a chunk or object is left over.
- **Coney's choices for the level file:** the path data and subtitles are kept as bytes in the level object (the
  original fixes the paths up in place and keeps both in globals); the clumps' models are unpacked into plain
  geometry at load, like the streamed parts; the glow world is one atomic at the origin rather than a RenderWare
  world (it has one sector and no planes, so nothing is lost).

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[disc][level]"` loads all 64 `.lev` files on librw's
NULL device: none fails, and nothing is left on the stacks. 66 occluders; 10,992 paths; 386,784 bytes of subtitles.
The models unpack to 147,462 skyline, 2,092 sky box and 1,792 cloud box triangles (28 per cloud box), every model's
first material gets its dictionary's texture, and every sky and cloud box lies within 2 units of the origin once
placed by its frames: the root frames turn the models and place the skyline in the world, while the boxes are drawn
round the camera ([The background](#render-order)). The glow worlds unpack to 6,870 triangles, as counted above, and
every one of their materials finds its `propglow…` texture.

**The background and the glows** (`src/platform/world_renderer.h`) follow [The background](#render-order) and step 5 of
[A frame](world.md#a-frame). The sky box and then the cloud box are drawn round the camera with its translation zeroed
(near 0.05, far 5; Z write and fog off, nothing culled). The cloud box turns about `y` by one radian a minute of game
time (`cloudFrame`), so a fixed-step run draws the same frames. The skyline is drawn in place from the smaller of 39
and the nearest missing scenery out to 560, with Z write on and fog off. Then only Z is cleared, and the glow world is
drawn before the `s` world: nothing culled, Z test and write and fog on. `coney --view-world <level>` loads the level
file whenever the level has one.

- **Coney's choices for the background:** it is lit by the viewer's one ambient light (0.157), the stand-in for the
  world lights the original selects for a far-away sphere ([Lighting](world.md#lighting)). The PS2 driver's far-clip
  call (`0x0048f0f8`) has no counterpart. Switching camera planes ends and begins librw's camera update, which makes
  the camera's world current, so Coney then puts the light world back.

What the implementer still needs:

- **Mode 1** (done as far as above): the rest of `InitLevel`, and `Exit` → the whole of `UnloadLevel`.
- **The level table** filled from `config_preload3.lua` ([Front end](frontend.md#the-level-table)), with the names at
  `+0x14` (level) and `+0x39` (world); in practice both are `level<N>`.
- **`LoadLevel`** in the order above: the worlds, then the level file (done, above); still missing are the path
  records' and subtitles' meaning, and handing the level object to a world manager.
- **The player's start** (done from the level script, above): still missing are the flags (`AddFlag`, `FlagPos`) that
  the hub and the Rumble arenas place player 1 with, and the start functions' teleports.
- **The level's lighting, fog colour and camera** from the level script and the player camera
  ([The streamed world](world.md#lighting)).
- **The preload** as the first frame's precondition: load the section's pack, then stream world parts until the
  update reports no more work within the camera's draw distance. With synchronous reads Coney can simply load every
  part within the radius; the time budget only matters on a disc.
- **Memory**: one budget for scenery and resources, and the "make room" eviction, decide what is resident and so what
  pops in; a PC build can raise the budget, but should model it, and the order of loading must stay deterministic
  for the test mode ([Memory](memory.md#what-a-reimplementation-must-keep)).
- **Unload** that frees everything a level made and recreates the Lua state.
- **A disc test**: every `.lev` loads through the chunk system (64 files, 18 chunks each), and every world loads
  ([The streamed world](world.md#disc-counts)).

**Coney's path data check:** `inspectPathData` (`src/world/level_object.cpp`) reads the 0x20-byte header, checks
that the counted records fit, walks every path's slab lists (16 `s16` starts at `+0x28`, each an index into the edge
lists and each list ended by a negative value, a path with a negative first start having none) and requires the chunk
to be `align16(end of the furthest list + 4)` bytes ([Path data](#path-data)). The disc test (`[disc][level]`) counts
77,678 bytes of edge lists and 770 bytes of tails over the 64 files. `world::PathMap` (`src/world/path_map.h`) decodes
the records for the AI's route planner, with the inside and walkable-line tests ([AI](ai.md#coney)).

**The Object List** (2026-10-06): `world_objects::ObjectList` (`src/world_objects/object_list.h`) parses the chunk
and finds a record by name ([The Object List](#the-object-list)); Coney's reference renderer
(`coney --render-references`, [Building](../guides/building.md#reference-images)) loads every record's model with the
level file's `0x47` reader and draws the thumbnails of [Objects](../references/objects.md). Disc check (NTSC-U,
2026-10-06, counts only; `coney_tests "[disc][object_list]"` and the renderer): every record's model and dictionary
is a WAD entry, all 1,406 models load, and 1,399 object images are drawn (one name hash is listed twice); the six
with several atomics are the cars, drawn as [Cars](../references/cars.md) instead.

## Open questions

- **The Object List's models**: how the game gives an object's untextured material its dictionary's texture
  ([The Object List's models](#the-object-list)); the record's untraced fields are on
  [WAD contents](formats/wad-contents.md#object-list).

- **Levels without a `.lev`**: `level70`-`74`, `90`, `91`, `94`, `96`-`98`, `106`, `117`, `125`, `135` have worlds but
  no level file of their name. Do their records give another name at `+0x14`? Decoding `config_preload3.lua`'s
  `CfgLevelName` calls (Lua 4.0 bytecode) would tell.
- **The level world** (answered, inferred): the light glows; its textures are only `propglow…` sprites
  ([The level object](#the-level-object)). A look in PCSX2 would confirm it.
- **Who draws the sky, cloud and shadow models** (answered): `LevelObject_RenderBackground` (`0x0040d0a8`), before the
  world ([The level in a frame](#render-order)). The "shadow" model is a skyline backdrop (inferred).
- **The level object's destructor** (`0x0040cf80`): no caller on the unload path was found; is it called through the
  vtable from elsewhere, or does the pool's destruction alone end the level's RenderWare objects?
- **The subtitles chunk** (`0x51`); the A record's `+0x08`, and what the clockwise paths and the path flags are for
  ([Path data](#path-data)).
- **The path data's size** (answered): the header is 0x20 bytes, and the chunk ends with the paths' edge lists and a
  4- to 18-byte tail; the 79,472 bytes Coney's disc test left uncounted were the header's second 16 bytes, the lists and
  the tails ([Path data](#path-data)). Still open: whether the tail's first 4 bytes mean anything (no reader found).
- **`WorldLevel_Load`** (answered): game code in `World/`. tolua ends at `0x0040c5e0`; the WAD object and
  `WorldLevel_Load` after it call no Lua API, and the source map and the progress totals now say so
  ([Source map](source-map.md#world)).
- **`Sector Pool 2`** (answered): nothing; it is created at its 4 KB minimum in practice and never read
  ([Memory](memory.md#the-pool-tree)).
- **The script entry** (answered, confirmed (code)): `global.lua`, then `<level>.lua`, in the Lua state the last
  unload made ([Scripts](scripting.md#life-of-the-lua-state)). The `*_strings_<lang>.lua` files are chosen by the level
  scripts themselves (`level95.lua` picks one of five by `GetLanguage`; inferred from the disassembly).
- **The resource manager** (packs, the dependency list, the time stamps behind "least recently used", its seven
  lists) needs its own page.
- **Runtime confirmation** with PCSX2: the size of the `Sector Pool` on a retail boot (answered: 17,217,536 bytes,
  with its use at the menu and in a level on [Memory](memory.md#sizes-at-runtime)); still open: the order of file
  requests during a level start (needs a breakpoint on the file manager's request function, which PINE does not
  offer).
