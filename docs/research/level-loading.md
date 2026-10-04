# Level loading

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims: PCSX2 was
not running when this page was written. The disc-side checks (2026-10-04) read the NTSC-U disc's WAD with throwaway
scripts outside the repository and are reported as names, counts and sizes only.

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
| `Sector Pool` (`0x006eb9c8`) | heap, in the global heap | `0x0040d688`, at start-up | the largest free block of the global heap minus 128 KB | everything below |
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
the same 18 chunks in the same order:

| # | Chunk | What the chunk system does with it | Ends up in |
| --- | --- | --- | --- |
| 1 | `0x53` Occluders | raw | level `+0x28` |
| 2-7 | (`0x2A`, `0x47`) × 3 | texture dictionary (pushed as `0x0B`) and a model (pushed as `0x41`), three times | level `+0x10`/`+0x14`, `+0x18`/`+0x1c`, `+0x20`/`+0x24` |
| 8 | `0x2A` | texture dictionary | level `+0x08` |
| 9 | `0x15` Sector BSP Data | RenderWare world, read through device slot `+0x170` (pushed as `0x42`) | level `+0x0c` |
| 10 | `0x40` PathData | its handler fixes it up in place and keeps it in globals | path system |
| 11-16 | `0x52`, `0x07`, `0x04`, `0x05`, `0x06`, `0x03` | collision data; the `0x03` handler pops the other five and pushes the mesh as an object | level `+0x04` |
| 17 | `0x17` Level Header | the run-time handler `0x0040ce30` builds the level object from everything above | the level object |
| 18 | `0x51` Subtitles | its handler keeps it in `0x0050ea74` | subtitle system |

The level object (`0x0040cf40`, vtable `0x00545b88`, 0x2c bytes), confirmed (code) at `0x0040ce30` for what goes
where; the roles of the three models come from their texture names on the disc (inferred):

| Offset | What |
| --- | --- |
| `+0x04` | collision mesh |
| `+0x08` | a texture dictionary of glow sprites (`propglow01k_000`, `propglow02k_000`) |
| `+0x0c` | the **level world**: a small RenderWare world with one sector and 12 to 156 triangles, drawn first every frame ([The streamed world](world.md#a-frame)); what it shows is open |
| `+0x10`, `+0x14` | dictionary and model of the **shadow** (texture `shadow`, `shadow_<name>`) |
| `+0x18`, `+0x1c` | dictionary and model of the **sky box** (`skybox_<name>`) |
| `+0x20`, `+0x24` | dictionary and model of the **cloud box** (`cloudbox_<name>`) |
| `+0x28` | occluders: `u32 count`, 12 bytes, then `count` records of 0x70 bytes (66 on the whole disc) |

`0x0040cdd8` links each model to its dictionary. Who draws the shadow, sky and cloud models is not traced.

The collision chunks, as far as loading needs them (layout from [Chunk system](chunk-system.md#chunk-type-table) and
the disc): `0x03` is a 160-byte header; `0x04` triangles of 10 bytes, `0x05` a grid, `0x06` strings (material
names), `0x07` a vertex buffer, `0x52` a "checked" bit set cleared on load. Sizes on the disc: triangles 144 to
90,512 bytes (at most 9,051 triangles in one level), vertex buffer 160 to 86,416 bytes. Mode 1's `Update` calls
`0x00351158` on the mesh every simulated frame. A collision page will describe the format.

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

Mode 8 pushes mode 1 once a level is chosen ([Front end](frontend.md#mode-flow)). Mode 1's `Enter` (`0x001582e0`),
confirmed (code): sets up the audio manager (`0x001110c8` with 0.1; three channels cleared through `0x001104c8`),
takes `GameTimer` (setting its fields `+0x5c` = 1, `+0x60` = 0), calls its own `Resume`, reads the pads once, then
**`InitLevel`**
(`0x0015fe90`). Afterwards it clears five globals (`0x0050c550`-`0x0050c560`), resets the timer and sets `+0x28` =
180 (a frame countdown that the `Update` uses to hold input and fades at the start).

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
   manager's object list (`0x00398598`); add the `CrimeScene` and `GangCall` objects.
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

### A frame of play

Mode 1's `Update` (`0x00158728`) is the [in-game frame](boot.md#one-frame) with the streaming in it: after the
simulation, **`WorldManager_Update`** and the resource manager's update run once each frame (when `0x0050c694` is
set, which it is in `.data`). The world is drawn per viewport by `WorldManager_Render`
([The streamed world](world.md#a-frame)). `W_GameState + 0x14c` drives the way out: 1 leads to `0x00155408` or
`0x001557f8` and 2 to `0x0015d420` once the frame countdown `+0x28` runs out; any value other than 0, 1 and 2 (3 is
what `MenuLoadLevel` sets) makes `Update` return 0, which pops the mode. The meanings of 1 and 2 are not traced.

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
| `<level>.lev`: collision, paths, occluders, sky, clouds, shadow, glows, level world, subtitles | `LoadLevel`, synchronously | `UnloadLevel` |
| the level script, the object list | `InitLevel` | `UnloadLevel` |
| the dependency list's resources | `InitLevel`, blocking | `UnloadLevel` |
| `<level>_<section>.pak` | the preload, blocking | the resource manager evicts what nothing holds |
| world parts (`_ms<i>.sec`) | the preload, then streamed by distance | unloaded when far, unseen and memory is short |
| other resources | streamed by the resource manager | evicted least recently used |

## Coney's implementation

The streamed-world half of `LoadLevel` and the streaming of a running level exist (2026-10-04), behind Coney's world
viewer (`coney --view-world <level>`, [Building](../guides/building.md#the-world-viewer)); mode 1, `InitLevel`, the
level file and `UnloadLevel` do not. Details on [The streamed world](world.md#coneys-implementation).

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

What the implementer still needs:

- **Mode 1** with `Enter` → `InitLevel`, an `Update` that streams once a frame, and `Exit` → `UnloadLevel`.
- **The level table** filled from `config_preload3.lua` ([Front end](frontend.md#the-level-table)), with the names at
  `+0x14` (level) and `+0x39` (world); in practice both are `level<N>`.
- **`LoadLevel`** in the order above: the worlds (`<world>s`, `<world>d`, or one `<world>`), then `<level>.lev`
  through the chunk system with handlers for `0x53`, `0x15`, `0x40`, the collision chunks, `0x17` and `0x51`. A first
  milestone can skip the paths, collision and subtitles (keep them as raw chunks) and build the level object with the
  sky, cloud and shadow models and the level world.
- **The preload** as the first frame's precondition: load the section's pack, then stream world parts until the
  update reports no more work within the camera's draw distance. With synchronous reads Coney can simply load every
  part within the radius; the time budget only matters on a disc.
- **Memory**: one budget for scenery and resources, and the "make room" eviction, decide what is resident and so what
  pops in; a PC build can raise the budget, but should model it, and the order of loading must stay deterministic
  for the test mode ([Memory](memory.md#what-a-reimplementation-must-keep)).
- **Unload** that frees everything a level made and recreates the Lua state.
- **A disc test**: every `.lev` loads through the chunk system (64 files, 18 chunks each), and every world loads
  ([The streamed world](world.md#disc-counts)).

## Open questions

- **Levels without a `.lev`**: `level70`-`74`, `90`, `91`, `94`, `96`-`98`, `106`, `117`, `125`, `135` have worlds but
  no level file of their name. Do their records give another name at `+0x14`? Decoding `config_preload3.lua`'s
  `CfgLevelName` calls (Lua 4.0 bytecode) would tell.
- **The level world** (`+0x0c`, one sector, a few dozen triangles, drawn first with culling off): what is it?
- **Who draws the sky, cloud and shadow models**, and the glow dictionary's users.
- **`Sector Pool 2`** (answered): nothing; it is created at its 4 KB minimum in practice and never read
  ([Memory](memory.md#the-pool-tree)).
- **The script entry** (script system slot `+0x24`): which `.lua` files a level runs (`<level>.lua`,
  `<level>_strings.lua`, `<level>main.lua`).
- **The resource manager** (packs, the dependency list, the time stamps behind "least recently used", its seven
  lists) needs its own page.
- **Runtime confirmation** with PCSX2: the size of the `Sector Pool` on a retail boot, and the order of file
  requests during a level start (a breakpoint on the file manager's request function).
