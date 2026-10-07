# Source map of the executable

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims.

## Purpose

`SLUS_212.15` has no symbols, but its `.rodata` holds 153 source paths of the form
`c:/Warriors/Source/<Subsystem>/<File>.cpp` (152 `.cpp` files plus `Scripting/ScriptLua.inl`). This page uses them to
say which address ranges of `.text` came from which original source file, and where the middleware (Lua, tolua,
RenderWare, Bink, the SCE libraries, the C and C++ runtimes) sits. It is the index for every later analyst: before
studying a subsystem, look up its range here.

## How the map was made {#method}

The steps, so that anyone with the executable and Ghidra can repeat or extend them:

1. **Path strings.** List every `.rodata` string containing `/Warriors/`. Besides the 153 `.cpp` and `.inl` paths
   there are about 125 copies of header paths (`Memory/MemoryStl.h`, `Memory/FreeList.h`,
   `Memory/FreeListContainer.h`, `Memory/FreeListManager.h`, `Utils/Queue.h`): each translation unit (TU) that uses
   those templates gets its own copy, so they also mark TU boundaries in `.rodata`.
2. **Anchors.** Find the functions that load each path string (a `lui`/`addiu` pair). Most of these loads pass the
   path as the `file` argument of the tagged allocator (see [Compiler](compiler.md)); the rest are assertions. A
   function that loads `<File>.cpp`'s path was compiled from that file: **confirmed (code)**. Ghidra's own
   cross-references miss some of these loads; scanning each function's `lui`/`addiu` pairs finds 302 anchor functions
   for 135 of the 153 files.
3. **Link order.** GCC emits a TU's functions together, and the linker keeps the object files in one order for every
   section, so `.text`, `.rodata` and `.data` all follow the same file order. The path strings sit in `.rodata` in
   the same order as their anchors sit in `.text` (true for all 135 anchored files), which bears this out.
4. **Data neighbourhoods.** A function that is not an anchor but loads `.rodata`, `.data`, `.bss`, `.sdata` or
   `.lit4` addresses lying between two addresses already tied to the same file is given that file. Addresses used by
   code more than 16 KB apart are treated as shared and ignored. The candidates are then filtered so that file order
   never goes backwards along `.text` (a weighted longest non-decreasing run; an anchor weighs 50, any other
   candidate 1). **Inferred.**
5. **Sandwich fill.** Functions lying between two functions of the same file are given that file. **Inferred.**
6. **Static-initialiser stubs.** A TU with global constructors ends with a small GCC stub that calls the TU's
   initialiser, the function immediately before it, with `a1 = 0xffff`. There are 91 such stubs, and each marks the
   **end** of a TU. Where a file's last attributed function is followed by a stub within 32 KB, with no other file's
   function in between, the tables list the stub, and the coverage numbers count the stretch up to it as that
   file's. Every file whose end was already pinned by its own functions ends exactly at a stub (for example
   `GameModes/Gm_MemoryCard.cpp` at `0x0015c450`, `GUI/OptionMenu.cpp` at `0x001dab10`, `WorldObjects/flags.cpp` at
   `0x00417ad0`). **Inferred.**
7. **Directories.** Functions between two attributed functions of the same top-level directory are counted to that
   directory. **Inferred.** The order of files inside and across directories is the project's own, not alphabetical
   (`Warriors/` and `Movie/` come last, `System/tolua` sits between `TaskEngine/` and `Utils/`), so this is only done
   inside a directory.
8. **Middleware** is placed by its strings, by the libraries' own globals and by where calls stop crossing (see
   [Middleware](#middleware)).
9. **Position and calls.** A stretch left between the last file of one directory and the first file of the next is
   given the directory its code works with, when (a) the `.rodata` it alone uses lies between those two files' path
   strings, so link order puts it there, and (b) its calls go mostly to that directory's code (for example 3,580
   calls from the stretch after `Human/cns/cnsplayertag.cpp` into `Human/`, 2,667 from the one after
   `TaskEngine/TaskManager.cpp` into `TaskEngine/`). **Inferred.** It cannot rule out a directory with no path
   string that sorts between the two (the directories link in nearly alphabetical order); the
   [table](#position) names the cases where one is plausible.

TU boundaries are aligned only to 8 bytes, like functions, so padding gives no extra signal. The analysis was run
with throwaway scripts over the ELF and Ghidra's function list (13,789 functions in `.text`). In the local Ghidra
project, the first attributed function of each file carries the EOL comment `SourceMap first: <file>`, `main` is
named and the chunk type table is labelled `ChunkTypeNameTable`.

### Evidence levels used here

- An **anchor** (a function that loads the file's path string) is **confirmed (code)** at its address.
- An **attributed range** (from the first to the last function that steps 4 and 5 give the file) is **inferred**.
  Its ends are the first and last attributed functions, not necessarily the TU's real ends: the TU may extend a little
  either way.
- A **static-init stub** is confirmed (code) as a stub; reading it as the end of that file's TU is **inferred**.
- Directory ranges, stretches placed by position and calls (step 9) and middleware ranges are **inferred**; ends
  written `~` are approximate.

## Layout of `.text`

`.text` runs from `0x00100000` to `0x004f6578` (4,154,744 bytes).

| Range | Contents | Evidence |
| --- | --- | --- |
| `0x00100008`-`0x00100200` | crt0: `entry` (`0x00100008`) calls `main` (`0x001446d0`), `FlushCache` and the exit call | confirmed (code) |
| `0x00100200`-`0x0042af70` | game code, one TU after another, in the order of the tables below (the first 17 KB, before `Animation/`'s first path string, is placed by [position](#position)) | inferred |
| `0x00323298`-`~0x00335320` | Lua 4.0.1, built as C++ (`lua-4.0.1/src/lmem.cpp`), between `Human/` and `Memory/` | inferred |
| `~0x00408ac8`-`0x0040c5e0` | tolua, between `TaskEngine/` and `World/` | inferred |
| `0x0042b040`-`~0x004da118` | libraries: C++ runtime, C library, SCE SDK, RenderWare, Bink (see [Middleware](#middleware)) | inferred |
| `~0x004da118`-`0x004f6578` | game code from link-once sections: template and inline functions, with `MemoryStl.h`/`FreeList.h` asserts and class tags such as `DS_PS2Device` and `MemoryPoolClump` | inferred |

The top-level directories in `.text` order (from the path strings' order, inferred): (unnamed), `Animation`, `Audio`,
`Camera`, `Core`, `Debug`, `Device/ps2`, `FileIO`, `GameModes`, `Graphics`, `GUI`, `Human`, `lua-4.0.1`, `Memory`,
`Physics`, `RayCast`, `Scene`, `Scripting`, `StringTable`, `TaskEngine`, `System/tolua`, `Utils`, `World`,
`WorldObjects`, `Warriors`, `Movie`.

| Directory | Range (first to last attributed function) | Functions | Notes |
| --- | --- | --- | --- |
| `Animation` | `0x00104630`-`0x0010d758` | 135 | |
| `Audio` | `0x0010edd0`-`0x001167b8` | 257 | |
| `Camera` | `0x0011e1b0`-`0x0013b118` | 196 | |
| `Core` | `0x00143ea0`-`0x00144a08` | 22 | starts with the CRC unit (no path string); holds `main`; see [For the next steps](#for-the-next-steps) |
| `Debug` | none | 0 | `DebugStream.cpp` has no anchor; probably in `0x00144a08`-`0x001483e8` |
| `Device/ps2` | `0x00145790`-`0x0014d528` | 173 | starts at `DS_PS2Device`'s virtual `Init`; holds `GameTimer` (no path string); see [For the next steps](#for-the-next-steps) |
| `FileIO` | `0x001541e0`-`0x00155b30` | 40 | |
| `GameModes` | `0x00155b30`-`0x00162598` | 221 | starts with mode 0xd's and the game-mode base's units (no path strings); about 8 more TUs with no path string sit between `Gm_MemoryCard.cpp` and `Gm_XboxSaveSystem.cpp` (stubs `0x0015cac0` to `0x0015f540`) |
| `Graphics` | `0x0016e388`-`0x0019c5e8` | 605 | |
| `GUI` | `0x001a1f10`-`0x002176b8` | 1,662 | |
| `Human` | `0x0021c7c8`-`0x00273fa0` | 1,075 | the 718 KB after it, up to Lua, is placed by [position](#position) |
| `Memory` | `0x00338420`-`0x0033b1a0` | 70 | |
| `Physics`, `RayCast` | `0x003418f8`-`0x00350778` | 4 | one anchor each; the 60 KB between them is placed by [position](#position) |
| `Scene` | `0x00351da0`-`0x00354bb8` | 44 | |
| `Scripting` | `0x00356390`-`0x003865d8` | 1,064 | mostly the Lua bindings around `ScriptLua.inl` |
| `StringTable` | `0x00386b30`-`0x00386f58` | 2 | |
| `TaskEngine` | `0x00397a48`-`0x003a8698` | 279 | the 68 KB before it and the 394 KB after it, up to tolua, are placed by [position](#position) |
| `World` | `0x0040c5e0`-`0x004124f8` | 61 | the first 5 (the WAD object and `WorldLevel_Load`) are placed by their callers, below |
| `WorldObjects` | `0x00413218`-`0x00417af0` | 107 | |
| `Warriors` | `0x00417b10`-`0x00424ee8` | 389 | |
| `Movie` | `0x00429b18`-`0x0042af70` | 10 | |

### Placed by position and calls {#position}

The stretches between directories, placed by [step 9](#method) (inferred unless a column says otherwise). "Calls"
counts calls from the stretch into the directory's attributed code; the contents are confirmed (code) on the linked
pages. Names are ours.

| Range | Size | Placed in | Contents | Why there |
| --- | --- | --- | --- | --- |
| `0x00100200`-`0x00104630` | 17 KB | `Animation/` | the reference pose and skeleton (`Pose_InitReference`, `0x00100200`), animation cursors (`0x00104110`) and their frame events (`0x00101dd8`), [Animation format](formats/animation.md) | before `Animation.cpp`'s path string; called from `Animation/` 61 times, `Human/` 50 |
| `0x0010d758`-`0x0010edd0` | 6 KB | `Audio/` | the music player (`Music_Play`, `0x0010d8e8`; stream state names at `0x0010eab0`), [Sound](sound.md) | rodata between `AnimationMgr.cpp` and `MusicList.cpp`; called from `Audio/` |
| `0x001167b8`-`0x0011b770` | 20 KB | `Audio/` | sound tasks (`SoundTask_Update`, `0x0011a170`), the DJ's failure lines, alarm emitters | rodata between `SoundMatrix.cpp` and `Cam_ICamera.cpp`; calls `Audio/` 30 times, `Camera/` never |
| `0x0011b770`-`0x0011e1b0` | 11 KB | `Camera/` | the camera helpers behind the script bindings (`Camera_MakeActiveByHandle`, `0x0011b770`), [Camera](camera.md) | calls `Camera/` 80 times |
| `0x0013b118`-`0x00143ea0` | 36 KB | `Camera/` | more camera code, up to the CRC unit that opens `Core/` | rodata between `Cam_Power.cpp` and `ChunkSystem.cpp`; calls `Camera/` 55 times |
| `0x00144a08`-`0x00145790` | 3 KB | `Device/ps2/` | the pads (`Pads_Update`, `0x001454a8`; `Pad_Update`, `0x00144fb0`; the button history), [Front end](frontend.md#input) | reads `libpad`; called from `Device/ps2/` 22 times. `Debug/DebugStream.cpp` (no code) would also sort here |
| `0x0014d528`-`0x00153f60` | 27 KB | `Device/ps2/` | five more sound-device methods, the music's (`0x0014d528`-`0x0014d758`, after the unit's stub), then SCEE's MultiStream library and the IOP command layer (`0x0014d760`-`0x00151ed0`; `MUSIC.SND`, the WAD stream), [Sound](sound.md#device), [File I/O](file-io.md); the GIF callback list (`0x00151e68`, `0x00151ed0`); then an unused asynchronous memory-card library over `libmc` (`0x001520b0`-`0x00153258`, [Saving](save.md#card-library)) and three unreferenced shell functions before `Shell/Core/shellMemory.cpp`'s rodata: a `libpad` mode state machine (`ShellPad_Update`, `0x00153490`) and a display set-up for an aspect mode with its GS register packet (`0x00153c18`, `0x00153de0`), confirmed (code) | rodata between `sound/msaudiodevice.cpp` and `Shell/Core/shellMemory.cpp`, both `Device/ps2/` |
| `0x00153f60`-`0x001541e0` | 1 KB | `FileIO/` | `FS_FSToStreamFSFileSys` and its file, [File I/O](file-io.md) | its allocation tag; before `FS_MemoryFile.cpp` |
| `0x00162598`-`0x0016e388` | 49 KB | `GameModes/` | loading-screen texture names (`%s_ls_%d` with language suffixes, `0x00163270`, to the stub `0x00163c48`); the cheat codes (`Cheat_CheckSequence`, `0x00163c68`, [Debug](debug.md)); the gangs (`Gangs_Update`, `0x0016d170`, to the stub `0x0016d630`) and the responders (`0x0016df68`), [AI](ai.md#gangs) | rodata between `Initialize.cpp` and `Graphics/Animations.cpp`. The gangs are called from the AI 443 times; a directory of their own that sorts between `GameModes/` and `Graphics/` (say `Gang/`) would fit as well (speculative) |
| `0x0019c5e8`-`0x0019dfb0` | 6 KB | `Graphics/` | the heat-distortion effects' methods | called only from `DistortionEffectManager.cpp` (`0x0019c070`-`0x0019c438`) |
| `0x0019dfb0`-`0x001a1f10` | 16 KB | `GUI/` | `GlobalString_Get`/`_Set` (`0x0019ee70`, `0x0019eea0`), `Bar` (`0x001a0fd0`), the HUD meter (`0x001a1008`), soldier messages, [GUI](gui.md), [HUD](hud.md) | before `BaseWidget.cpp`'s path string; called from `GUI/` 232 times |
| `0x002176b8`-`0x0021c7c8` | 21 KB | `Human/` | the human's set-up and contacts (`Human_Init`, `0x00218008`; `Human_OnContact`, `0x00219d50`; `Strike_Contact`, `0x0021b290`), [Characters](characters.md) | before `Human.cpp`'s path string; calls `Human/` 115 times and is called from it 123 |
| `0x00273fa0`-`0x00323298` | 718 KB | `Human/` | the player's moves and combat (to `0x00287a18`, [Combat](combat.md)), then the AI: brains, goals, actions, tactics, formations ([AI](ai.md)); 8 static-init stubs | rodata between `cns/cnsplayertag.cpp` and Lua's; calls `Human/` 3,580 times. More `Human/` subdirectories like `cns/` would fit; a directory of its own between `Human/` and `lua-4.0.1/` cannot be ruled out |
| `0x00335320`-`0x00338420` | 13 KB | (unnamed directory) | maths: random numbers (a table of 1,024, `Random_Int` `0x003353b8`), quaternions (`Quat_Slerp`, `0x00336a00`, [Physics](physics.md)), ray-triangle tests (`0x00337920`) | between Lua and `Memory/` (stubs `0x00335670`, `0x003383e0`); called from every directory, calls neither; a `Math/` directory would sort there (speculative) |
| `0x0033b1a0`-`0x0033c288` | 4 KB | `Memory/` | the heaps' block allocator, [Memory](memory.md) | called only from `Memory/` |
| `0x0033c288`-`0x003418f8`, `0x00341a68`-`0x0034f740` | 82 KB | `Physics/` | `IPhysics`, bodies, sweeps, settling, [Physics](physics.md) | rodata between `physics.cpp` and `CollisionMesh.cpp`; ends at the stub `0x0034f718` |
| `0x0034f740`-`0x00350538` | 3 KB | `RayCast/` | ground-height helpers and the material names (`Collision_MarchRay`, `0x0034f740`), [Collision](collision.md) | after `Physics/`'s last stub; uses the level's mesh |
| `0x00350538`-`0x00351da0` | 6 KB | `RayCast/CollisionMesh.cpp` (file) | the mesh tests around the anchor, [Collision](collision.md) | file inferred on that page: nothing ends the unit before `Scene/` |
| `0x00354bb8`-`0x00356390` | 6 KB | `Scene/` | scene tracks (`SceneTrack_Events`, `0x00354d98`) and the `WarMoveInstance` tasks, [Scenes](scenes.md); probably `WarMovement.cpp`, whose path string has no code reference (speculative) | rodata between `SceneCache.cpp`'s and `WarMovement.cpp`'s path strings |
| `0x00386f58`-`0x00397a48` | 68 KB | `TaskEngine/` | the task classes before `ObjectTaskManager.cpp`: cars, glass, lights, world objects, doors (`Car_Spawn`, `Obj_Spawn`, `Door_Open`), [Cars](cars.md), [Objects](objects.md) | rodata between `StringTableCache.cpp` and `ObjectTaskManager.cpp`, with four `FreeListContainer.h` copies; calls `TaskEngine/` 235 times |
| `0x003a8698`-`0x00408ac8` | 394 KB | `TaskEngine/` | the script types: every particle system, object behaviour and light (`ScriptType_Find`, `0x003c55e8`), [Particles](particles.md), [Objects](objects.md); stubs `0x003e29c8`, `0x003ee318`, `0x00407798` | rodata between `TaskManager.cpp` and `tolua_tm.cpp`; calls `TaskEngine/` 2,667 times |
| `0x004124f8`-`0x00413218` | 3 KB | `WorldObjects/` | the boxes' shared code (a chunk reader, `0x004124f8`; `VolumeBox_Add`, `0x004125b8`; `0x004127c0`, called by the player, turf and volume boxes) | before `PlayerBox.cpp`; its callers |
| `0x00417af0`-`0x00417b10` | 32 B | `Warriors/W_ActionableManager.cpp` (file) | `Cfg_SetActionDistance` | the first function after `flags.cpp`'s stub, just before the anchor `0x00417b10` |

The middleware ends moved with this: Lua starts at `0x00323298` (its API's first function, called from `Scripting/`),
not at `~0x00321ab8`, whose next 6 KB is AI code (`0x00322740` works on brains and goals); it ends at `~0x00335320`,
before the maths unit. tolua starts at `~0x00408ac8`: `0x004077b8`-`0x00408ac8` holds script types
(`sub_shack_puff`'s at `0x00408860`, [Particles](particles.md)).

Still unplaced (21 KB): `0x003865d8`-`0x00386b30` (seven functions between `Scripting/ScriptUtilities.cpp` and
`StringTable/StringTableCache.cpp`, called from the AI), `0x00424ee8`-`0x00429b18` (the game's RenderWare pipelines,
`Atomic_AssignGamePipelines` `0x00426c78`, [The streamed world](world.md#pipeline-unit), and their creation
`0x00429a98`; linked between `Warriors/` and `Movie/`, which come out of alphabetical order, so position says
nothing) and `0x0042af70` (176 bytes, the C++ runtime's constructor walker called by `__main`, [Boot](boot.md)).

## Files by subsystem

Each table gives a file's attributed range, the number of functions in it, the static-initialiser stub that probably
ends its TU (blank when none qualifies) and up to three anchors. The names in brackets are the allocation tags or
other strings each anchor passes, mostly class names: they say what the function creates, which is usually enough to
name it. A file listed as "none" has a path string that no code loads (the assertion or allocation that used it was
compiled out, or reaches the string some other way); its code lies somewhere between its neighbours in the table.

### Animation

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `Animation.cpp` | `0x00104630`-`0x001048a0` | 3 | | `0x00104818` (`AnimationSystem`) |
| `AnimationBlend.cpp` | `0x001048a0`-`0x00105570` | 9 | `0x0010b9d0` | `0x001048a0` (`FreeList<AnimTask>`) |
| `AnimationMgr.cpp` | `0x0010b9f0`-`0x0010bc38` | 3 | `0x0010d738` | `0x0010b9f0` (`FreeList<WarAnimInstance>`) |

### Audio

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `MusicList.cpp` | `0x0010edd0`-`0x0010ee90` | 1 | `0x0010f5f0` | `0x0010edd0` (`MusicTrack`) |
| `SoundCallBack.cpp` | none (path string at `0x00546ec0` has no code reference) | 0 | | |
| `SoundList.cpp` | `0x001118d0`-`0x00111990` | 1 | | `0x001118d0` (`Sound`) |
| `SoundListener.cpp` | `0x00111ac0`-`0x00112d00` | 21 | | `0x00111ac0` (`FreeList<SoundTask>`) |
| `SoundMatrix.cpp` | `0x001164a8`-`0x001167b8` | 1 | | `0x001164a8` (`SoundVoice`, `vags/character/voices/`) |

### Camera

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `Cam_ICamera.cpp` | `0x0011e1b0`-`0x00120230` | 22 | `0x00123610` | `0x0011e1b0` (`Cam_Scene`, `Cam_Locked`); `0x0011e440`; `0x0011f9e0` (`Cam_Follow`), +9 more |
| `Cam_Follow.cpp` | `0x001303c8`-`0x00130990` | 1 | `0x00134b70` | `0x001303c8` (`TPhysicsBoundSphere`) |
| `Cam_Mini.cpp` | `0x00137670`-`0x00137808` | 1 | `0x00137cb0` | `0x00137670` (`TPhysicsBoundSphere`) |
| `Cam_Mug.cpp` | `0x00138078`-`0x00139d48` | 3 | `0x00139d48` | `0x00138078` (`TPhysicsBoundSphere`); `0x00138b90` (`TPhysicsBoundSphere`, `vags/misc/mug_outro`) |
| `Cam_Power.cpp` | `0x0013a818`-`0x0013b118` | 1 | | `0x0013a818` (`TPhysicsBoundSphere`) |

### Core

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `ChunkSystem.cpp` | `0x001440a0`-`0x001446d0` | 9 | `0x001449e8` | `0x00144180`; `0x00144398` |

### Debug

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `DebugStream.cpp` | none (path string at `0x00549cc0` has no code reference) | 0 | | |

### Device/ps2

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `DS_PS2Device.cpp` | `0x001483e8`-`0x00148580` | 3 | `0x001485a8` | `0x001483e8` (`PS2StreamFileSys`); `0x00148460` (`PS2FileSys`, `debug/`); `0x001484d8` (`FS_FSToStreamFSFileSys`) |
| `DS_PS2FileSys.cpp` | `0x00148608`-`0x00148828` | 5 | | `0x00148608` (`PS2DbgFile`); `0x001486c0` |
| `DS_PS2TCPSocket.cpp` | none (path string at `0x0054a438` has no code reference) | 0 | | |
| `fileio/DVDWadIndexPS2.cpp` | `0x00149040`-`0x00149248` | 3 | | `0x00149040` (`DVDWadIndex`); `0x00149160` (`DVDWadEntry`) |
| `fileio/RockWadIndexPS2.cpp` | none (path string at `0x0054a6b8` has no code reference) | 0 | | |
| `memorycard/mcbase.cpp` | `0x00149f30`-`0x0014b9d0` | 25 | | `0x00149f30` (`QueueDataType`, `Queue<MC_FILE *>`) |
| `sound/msaudiodevice.cpp` | `0x0014ba18`-`0x0014d528` | 55 | `0x0014d508` | `0x0014bbd8` (`cdrom0:\IOP\BFW.SND;1`, `cdrom0:\SLUS_212.15;1`) |
| `Shell/Core/shellMemory.cpp` | none (path string at `0x0054d720` has no code reference) | 0 | | |

### FileIO

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `FS_MemoryFile.cpp` | `0x001541e0`-`0x00154440` | 8 | | `0x001541e0`; `0x00154290`; `0x001543e0` |
| `FS_TCPSocketFileSys.cpp` | none (path string at `0x0054e1a8` has no code reference) | 0 | | |
| `FS_WriteCachedFile.cpp` | none (path string at `0x0054e4d8` has no code reference) | 0 | | |
| `StreamManager.cpp` | `0x001547b0`-`0x001549a8` | 3 | `0x00155b10` | `0x001547b0` (`FileManager`); `0x001548c0` (`File Stream Buffer`); `0x00154950` |

### GameModes

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `Gm_Error.cpp` | `0x00156dd8`-`0x00157df8` | 11 | `0x00157e28` | `0x00156dd8`; `0x00156f90` |
| `Gm_MemoryCard.cpp` | `0x0015a270`-`0x0015c450` | 38 | `0x0015c450` | `0x0015baa0`; `0x0015c2c0` |
| `Gm_XboxSaveSystem.cpp` | `0x0015f560`-`0x0015fe30` | 11 | `0x0015fe30` | `0x0015f830`; `0x0015fd20` |
| `InitLevel.cpp` | `0x0015fe50`-`0x001607b8` | 2 | `0x00160dd8` | `0x0015fe90` (`load`, `Wind_Manager`) |
| `Initialize.cpp` | `0x00160df8`-`0x00161218` | 1 | `0x00162568` | `0x00160df8` (`Timer`, `GameTimer`) |

### Graphics

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `Animations.cpp` | `0x0016e388`-`0x0016fcc8` | 12 | | `0x0016e420`; `0x0016e740`; `0x0016eb10`, +2 more |
| `CameraGarbage.cpp` | `0x0016fcc8`-`0x00170600` | 8 | | `0x0016fcc8` (`ICameraGarbage`); `0x00170330` (`TGarbageData`); `0x00170550` |
| `CameraGroundFog.cpp` | `0x00171540`-`0x00171b18` | 6 | | `0x00171540` (`ICameraGroundFog`); `0x00171920` (`TGroundFogData`); `0x00171a68` |
| `Car.cpp` | `0x00173170`-`0x00173928` | 4 | | `0x00173170`; `0x00173718` (`CarInstance`); `0x00173878` |
| `CarModel.cpp` | `0x00173bd0`-`0x00174200` | 1 | | `0x00173bd0` |
| `Character.cpp` | `0x001775d8`-`0x00177eb8` | 5 | | `0x001775d8`; `0x00177b80` (`CharacterInstance`); `0x00177e00` |
| `CharacterList.cpp` | none (path string at `0x00551e28` has no code reference) | 0 | | |
| `CharacterModel.cpp` | `0x00178178`-`0x00178b30` | 5 | | `0x00178208`; `0x001784f0` |
| `DependencyList.cpp` | none (path string at `0x00551f60` has no code reference) | 0 | | |
| `FallingEmbers.cpp` | `0x00178fd8`-`0x00179138` | 2 | `0x0017b1c0` | `0x00178fd8` (`IFallingEmbers`); `0x001790a8` |
| `LightManager.cpp` | `0x0017d640`-`0x0017e6c0` | 5 | | `0x0017d640` (`LightManager`, `part_page1`); `0x0017e680` |
| `Model.cpp` | `0x0017f380`-`0x0017f608` | 2 | | `0x0017f4a0` |
| `Object.cpp` | `0x001809c0`-`0x00181170` | 4 | | `0x001809c0`; `0x00180f58` (`Object`); `0x001810c0` |
| `ObjectList.cpp` | none (path string at `0x005525b0` has no code reference) | 0 | | |
| `ObjectModel.cpp` | `0x001814f0`-`0x00181b20` | 1 | | `0x001814f0` |
| `ParticlePage.cpp` | `0x00181b68`-`0x00182820` | 10 | | `0x00181ca0`; `0x001821d0` |
| `ParticlePageList.cpp` | none (path string at `0x005527c8` has no code reference) | 0 | | |
| `RainDrops.cpp` | `0x00183510`-`0x001835d8` | 1 | | `0x00183510` (`IRainDrops`) |
| `ResourceMgr.cpp` | `0x00184918`-`0x00187960` | 17 | | `0x00184918` (`ResourceManager`, `global.pak`); `0x00184eb0` (`MemoryPoolClump`, `generic_header`); `0x00186710` (`MemoryPoolClump`), +2 more |
| `ScreenEffectsManager.cpp` | `0x0018b1e0`-`0x0018ce58` | 21 | | `0x0018ba10` (`ScreenEffectsManager`); `0x0018bae0` (`OE_Rain`, `OE_Fog`); `0x0018bd48`, +2 more |
| `Texture.cpp` | `0x0018e7e8`-`0x0018f230` | 6 | | `0x0018e898`; `0x0018ec00` |
| `WarTexture.cpp` | none (path string at `0x005531d8` has no code reference) | 0 | | |
| `WaterEffect.cpp` | `0x00190810`-`0x00191230` | 2 | | `0x00190810` (`MemoryPoolClump`, `Water`); `0x00191158` |
| `Devices/Renderware/DevRWDebug.cpp` | none (path string at `0x00553368` has no code reference) | 0 | | |
| `Devices/Renderware/DevRWGeneric.cpp` | `0x00192908`-`0x00197cd0` | 57 | | `0x00192908` (`Renderware`); `0x00192a08`; `0x00192ac8` (`Renderware`), +2 more |
| `OverlayEffects/OE_FilmGrain.cpp` | `0x001990b8`-`0x00199230` | 2 | | `0x001990b8` (`OE_Particle`) |
| `OverlayEffects/OE_Fog.cpp` | `0x00199380`-`0x001995e8` | 2 | | `0x00199380` (`OE_Particle`) |
| `OverlayEffects/OE_Rain.cpp` | `0x0019a1b8`-`0x0019a648` | 1 | | `0x0019a1b8` (`OE_Particle`) |
| `OverlayEffects/OE_RoomSmoke.cpp` | `0x0019add0`-`0x0019aff8` | 2 | | `0x0019add0` (`OE_Particle`) |
| `OverlayEffects/OverlayEffect.cpp` | `0x0019ba70`-`0x0019bbb0` | 2 | | `0x0019bab8` |
| `DistortionEffectManager.cpp` | `0x0019bf50`-`0x0019c3f0` | 6 | `0x0019c5c8` | `0x0019bf50`; `0x0019c070` (`HeatDistortionEffect`); `0x0019c1d0` (`HeatWaveEffect`), +2 more |

### GUI

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `BaseWidget.cpp` | `0x001a1f10`-`0x001a2060` | 2 | | `0x001a1f10`; `0x001a1fc8` (`ParticleContainer`) |
| `ChecklistMessageHUD.cpp` | `0x001a4690`-`0x001a5d28` | 24 | `0x001a5d60` | `0x001a46d0`; `0x001a47d0`; `0x001a4930` (`TextItemContainer`), +4 more |
| `Credits.cpp` | `0x001a8fb8`-`0x001a9960` | 10 | `0x001aad70` | `0x001a8fb8` (`CreditData`); `0x001a9130` (`CreditList`, `StringTableCache`); `0x001a94e8` |
| `GridContainer.cpp` | `0x001abbf8`-`0x001abee8` | 3 | | `0x001abbf8`; `0x001abe10` (`GridContainerItem`) |
| `HUDInterface.cpp` | `0x001ad588`-`0x001b1640` | 11 | `0x001b3ea8` | `0x001ad588` (`HUDArmyNight`); `0x001ae4d8` (`VirtualPad`); `0x001ae980` |
| `HUDLua.cpp` | `0x001b44d8`-`0x001b4690` | 1 | `0x001b8f78` | `0x001b44d8` (`BaseWidget`) |
| `MessageHUD.cpp` | `0x001b9290`-`0x001b9370` | 1 | | `0x001b9290` |
| `MissionSelectHUD.cpp` | `0x001bc328`-`0x001be848` | 23 | `0x001c1688` | `0x001bc5a8` (`CircledTextHeader`); `0x001bc930` (`MS_Mission`); `0x001bca70` (`MS_Area`), +2 more |
| `RadarHUD.cpp` | `0x001c41a0`-`0x001c6878` | 25 | | `0x001c41a0`; `0x001c4a40` (`BaseWidget`); `0x001c4d00` (`BaseWidget`, `hud_radar_dot`), +1 more |
| `ScreenFlowController.cpp` | `0x001c7e80`-`0x001c8628` | 8 | | `0x001c7e80` (`SFC_States`, `SFC_SharedData`); `0x001c8590` |
| `ScrollInHUD.cpp` | `0x001c8880`-`0x001c8db0` | 3 | | `0x001c8880` (`STL_List(QueueElement*)`, `QueueDataType`); `0x001c8aa0`; `0x001c8b08` (`PedReactNoise`) |
| `SubTitle.cpp` | `0x001cafa0`-`0x001cb000` | 1 | | `0x001cafa0` |
| `TextEntryPad.cpp` | `0x001cc240`-`0x001cc998` | 2 | | `0x001cc240`; `0x001cc2f0` (`TextWidget`, `UsageInfo`) |
| `TutorialHUD.cpp` | `0x001cd988`-`0x001ce9a8` | 11 | `0x001cfea0` | `0x001cd988` (`TutorialItemPQueue`, `QueueDataType`); `0x001cdc20` |
| `OptionGrid.cpp` | `0x001d4000`-`0x001d43e8` | 4 | | `0x001d4000`; `0x001d4230` (`OptionGridItem`, `OptionGridTextWidget`) |
| `OptionMenu.cpp` | `0x001d5900`-`0x001dab10` | 77 | `0x001dab10` | `0x001d5b30`; `0x001d86a8` (`OptionItemLightingType`, `OptionItemOnOffType`) |
| `ScrollingMenu.cpp` | `0x001e0a98`-`0x001e1a38` | 16 | | `0x001e0a98`; `0x001e1550`; `0x001e1830` (`ScrollingMenuItem`) |
| `ControlMenuHUD.cpp` | `0x001e3520`-`0x001e5478` | 29 | `0x001e5478` | `0x001e3828` (`ScrollingMenu`); `0x001e3d98` |
| `GameStats.cpp` | `0x00215150`-`0x00216908` | 8 | `0x00216e90` | `0x00215150` (`861A1AFF`, `806400FF`); `0x002155a8` (`ScrollingMenu`, `CircledTextHeader`); `0x00215ce8` (`GS_Mission`), +2 more |
| `GameStatsSubItem.cpp` | `0x00216eb0`-`0x002176b8` | 3 | | `0x00216eb0` (`861A1AFF`, `806400FF`); `0x00217350` (`BaseWidget`, `ScrollingTextWidget`); `0x002175f8` |

### GUI/RumbleModeGUI

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `RM_BuySoldiers.cpp` | none (path string at `0x00556aa0` has no code reference) | 0 | | |
| `RM_ChooseArea.cpp` | `0x001eaa30`-`0x001ebe00` | 11 | | `0x001eaa30` (`RM_ChooseAreaWidget`); `0x001eabc0` (`ScrollingMenu`, `rumble_mode_arenas_%d`); `0x001eb0c8` (`RM_AreaList`, `TextWidget`), +1 more |
| `RM_ChooseGangs.cpp` | `0x001ec980`-`0x001f0420` | 16 | | `0x001ec980` (`RM_ChooseGangsWidget`); `0x001ecae0` (`RM_GangsList`, `RM_ChooseGangs2ndInput`); `0x001eecc0` |
| `RM_Controller.cpp` | `0x001f0e60`-`0x001f23c0` | 10 | `0x001f27b0` | `0x001f0e60` (`RM_CharData`); `0x001f1000` (`RM_Main`, `RM_No2ndController`); `0x001f1748` (`StringTableCache`, `CharDataMap`), +2 more |
| `RM_CreateGang.cpp` | `0x001f2a00`-`0x001f30c0` | 3 | | `0x001f2a00` (`TextWidget`, `MultiLineTextWidget`); `0x001f2ff8` |
| `RM_EditGang.cpp` | `0x001f3610`-`0x001f5bd0` | 19 | `0x001f5bd0` | `0x001f3610` (`TextWidget`, `BaseWidget`); `0x001f47c8` |
| `RM_EditGangs.cpp` | `0x001f5e48`-`0x001f6f60` | 2 | | `0x001f5e48` (`TextWidget`, `SimpleHeader`); `0x001f6d90` |
| `RM_GameMode.cpp` | `0x001f8110`-`0x001f9228` | 11 | | `0x001f8110` (`RM_GameModeWidget`); `0x001f82c0` (`ScrollingMenu`, `ScrollingTextWidget`); `0x001f85c0` (`RM_GameModesList`, `TextWidget`), +1 more |
| `RM_Intro.cpp` | `0x001f9418`-`0x001fa170` | 7 | | `0x001f9418`; `0x001f9558` (`MultiLineTextWidget`, `BaseWidget`); `0x001f9b38` |
| `RM_Main.cpp` | `0x001fb290`-`0x001fb868` | 2 | | `0x001fb290` (`TextWidget`, `UsageInfo`); `0x001fb790` |
| `RM_No2ndController.cpp` | `0x001fbe68`-`0x001fc118` | 2 | | `0x001fbe68` (`MultiLineTextWidget`, `UsageInfo`); `0x001fc078` |
| `RM_NumPlayers.cpp` | `0x001fc5b0`-`0x001fdaf8` | 8 | `0x001fe818` | `0x001fc5b0` (`TextWidget`, `BaseWidget`); `0x001fcff8` |
| `RM_SwapSoldier.cpp` | `0x001feb30`-`0x00201b50` | 15 | `0x002030f0` | `0x001feb30` (`RMSwapGang`); `0x001fee10` (`ScrollingMenu`, `ScrollingTextWidget`); `0x001ff1c8` (`ScrollingMenu`, `ScrollingTextWidget`), +2 more |

### GUI/ProfileManagementGUI

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `PM_Continue.cpp` | `0x00203300`-`0x00203788` | 2 | | `0x00203300` (`TextWidget`, `OptionGrid`); `0x002036d8` |
| `PM_Controller.cpp` | `0x00203f48`-`0x00204ea8` | 28 | | `0x002040c8` (`PM_Greet`, `PM_NoSpace`); `0x00204d18` |
| `PM_Create.cpp` | `0x002050f0`-`0x00205660` | 3 | | `0x002050f0` (`TextWidget`, `MultiLineTextWidget`); `0x002055b0` |
| `PM_Delete.cpp` | `0x00205b38`-`0x002061c0` | 2 | | `0x00205b38` (`TextWidget`, `MultiLineTextWidget`); `0x002060e8` |
| `PM_Difficulty.cpp` | `0x002067d8`-`0x00206d10` | 2 | | `0x002067d8` (`TextWidget`, `OptionGrid`); `0x00206c60` |
| `PM_Extras.cpp` | `0x002071d8`-`0x00207480` | 2 | | `0x002071d8` (`OptionGrid`, `UsageInfo`); `0x002073e8` |
| `PM_Greet.cpp` | `0x002079a0`-`0x00207d48` | 2 | | `0x002079a0` (`BaseWidget`, `TextWidget`); `0x00207cb8` |
| `PM_Light.cpp` | `0x00208510`-`0x00208cc0` | 2 | | `0x00208510` (`BaseWidget`, `MessageHUD`); `0x00208b40` |
| `PM_Load.cpp` | `0x00209250`-`0x00209838` | 2 | | `0x00209250` (`TextWidget`, `OptionGrid`); `0x00209788` |
| `PM_Mode.cpp` | `0x00209da8`-`0x0020a1f0` | 2 | | `0x00209da8` (`OptionGrid`, `UsageInfo`); `0x0020a158` |
| `PM_NoSpace.cpp` | `0x0020a7a8`-`0x0020ad30` | 2 | | `0x0020a7a8` (`%s%d%s`, `MultiLineTextWidget`); `0x0020ac48` |
| `PM_NumPlayers.cpp` | `0x0020b208`-`0x0020b698` | 2 | | `0x0020b208` (`TextWidget`, `OptionGrid`); `0x0020b5e8` |
| `PM_Profile.cpp` | `0x0020be68`-`0x0020c510` | 2 | | `0x0020be68` (`TextWidget`, `OptionGrid`); `0x0020c460` |
| `PM_Subtitles.cpp` | `0x0020cab8`-`0x0020ceb8` | 2 | | `0x0020cab8` (`TextWidget`, `OptionGrid`); `0x0020ce08` |
| `PM_TooManyProfiles.cpp` | `0x0020d3a0`-`0x0020d890` | 2 | `0x00211c80` | `0x0020d3a0` (`%s%d%s`, `MultiLineTextWidget`); `0x0020d7a8` |

### Human

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `Human.cpp` | `0x0021c7c8`-`0x0021cb78` | 1 | | `0x0021c7c8` |
| `HumanInterface.cpp` | `0x0022e848`-`0x0022eb40` | 1 | | `0x0022e848` |
| `pathfinding/path_waypoint.cpp` | none (path string at `0x0055d320` has no code reference) | 0 | | |
| `cns/cnsplayertag.cpp` | `0x00273c60`-`0x00273fa0` | 1 | | `0x00273c60` (`CNSPlayerTag`) |

### lua-4.0.1

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `src/lmem.cpp` | `0x0032c758`-`0x0032c860` | 1 | | `0x0032c758` |

### Memory

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `FreeListMemoryPool.cpp` | none (path string at `0x00576ab8` has no code reference) | 0 | | |
| `MemoryClump.cpp` | `0x00338420`-`0x00338578` | 3 | | `0x00338420` (`MemoryPoolClumpInternal`); `0x003384b0` |
| `MemoryFilter.cpp` | `0x003388c0`-`0x003389a0` | 1 | | `0x003388c0` |
| `MemoryHeap.cpp` | `0x003389a0`-`0x00338bf0` | 3 | | `0x003389a0`; `0x00338b38` |
| `MemoryPriv.cpp` | `0x00338e10`-`0x00339a20` | 11 | | `0x00338e10` (`MemoryPoolHeap`, `All System`); `0x003398f0` (`MemoryPoolTrack`); `0x00339988` (`MemoryPoolTrack`) |
| `MemoryTrack.cpp` | `0x00339e28`-`0x0033a640` | 2 | | `0x00339e28` (`TrackInfo`, `none`); `0x0033a050` |
| `WarriorsMemory.cpp` | `0x0033afe0`-`0x0033b128` | 1 | `0x0033b168` | `0x0033afe0` (`MemoryPoolHeap`, `Level Dynamic & LUA Memory`) |

### Physics

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `physics.cpp` | `0x003418f8`-`0x00341a68` | 3 | | `0x00341a00` |

### RayCast

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `CollisionMesh.cpp` | `0x00350688`-`0x00350778` | 1 | | `0x00350688` |

### Scene

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `SceneCache.cpp` | `0x00351da0`-`0x00354bb8` | 44 | | `0x00351da0`; `0x003524a8` (`Scene`, `%s.scn`); `0x00352a48` (`%s.scn`, `SceneSubChunk`), +2 more |
| `WarMovement.cpp` | none (path string at `0x00578e98` has no code reference) | 0 | | |

### Scripting

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `ScriptLua.cpp` | `0x00356390`-`0x003564d8` | 2 | | `0x00356390` (`ScriptLua`); `0x00356450` |
| `ScriptLua.inl` | `0x0036bb10`-`0x0036bcb8` | 1 | | `0x0036bb10` |
| `ScriptObject.cpp` | `0x00384688`-`0x00384a10` | 4 | | `0x00384688` (`FreeList<ScriptObject>`); `0x00384840` |
| `ScriptUtilities.cpp` | `0x003864e0`-`0x003865d8` | 1 | | `0x003864e0` |

### StringTable

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `StringTableCache.cpp` | `0x00386b30`-`0x00386f58` | 2 | | `0x00386b30`; `0x00386d08` |

### TaskEngine

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `ObjectTaskManager.cpp` | `0x00397a48`-`0x0039aef0` | 40 | | `0x00397c58`; `0x00397e08` (`ObjectTaskRec`); `0x00398130`, +2 more |
| `SceneTask.cpp` | `0x0039d278`-`0x0039d618` | 3 | | `0x0039d278`; `0x0039d3a8` (`WarMoveInstance`) |
| `TaskManager.cpp` | `0x003a2728`-`0x003a2b48` | 2 | `0x003a8678` | `0x003a2728` (`ParticleTaskManager`, `ObjectTaskManager`); `0x003a2a30` |

The functions around `TaskManager.cpp`'s anchors that work on the manager (`0x003a2b48`-`0x003a4288`: phases, the
timing wheels, messages) and on the base task object (`0x003a1570`-`0x003a2310`) are named on [Tasks](tasks.md);
that they belong to `TaskManager.cpp` (or a base task file beside it) is inferred from the range only.

### tolua

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `System/tolua/src/lib/tolua_tm.cpp` | `0x0040a270`-`0x0040bb20` | 32 | | `0x0040a5b0` (`tolua_tbl_class`, `tolua_raw_delete`) |

### Utils

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `Dictionary.cpp` | none (path string at `0x005884f0` has no code reference) | 0 | | |

### World

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| (no path string) | `0x0040c5e0`-`0x0040c7f0` | 5 | | none: the WAD object (`0x0040c5e0`, called from `Game_InitializeSubsystems`, and its methods) and `WorldLevel_Load` (`0x0040c688`, called only by `WorldManager::LoadLevel`); none calls the Lua API, unlike the tolua code before them, so `World/` (inferred); possibly the start of `WorldLevel.cpp` (speculative) |
| `WorldLevel.cpp` | `0x0040c7f0`-`0x0040c868` | 1 | | `0x0040c7f0` |
| `WorldManagerLua.cpp` | `0x0040ca18`-`0x0040cc40` | 1 | | `0x0040ca18` (`WaterEffect`, `water_tex`) |
| `ps2/WorldLevelPS2.cpp` | `0x0040cf40`-`0x0040d088` | 2 | | `0x0040cf80` |
| `ps2/WorldManagerPS2.cpp` | `0x0040d688`-`0x004101f0` | 13 | | `0x0040d688` (`MemoryPoolHeap`, `Sector Pool`); `0x0040d900` (`MemoryPoolClump`, `warriors.glr`); `0x0040dbb8` (`MemoryPoolClump`, `%s.lev`), +1 more |
| `ps2/WorldPS2.cpp` | `0x004101f0`-`0x004124f8` | 23 | | `0x00410648` (`MemoryPoolClump`, `%s_sec.wld`); `0x00410b70`; `0x004110c0` (`MemoryPoolClump`, `Sectors%d`), +1 more |

`0x0040f850` and `0x0040f8a0` (the world manager's sector-heap getter and its streaming update, which uses the
string `0x00588c68` among the file's strings) and `0x004123e8` (it works on the world object's fields, called only
from `World/ps2/WorldPS2.cpp`) were added to these two files while writing [The streamed world](world.md)
(inferred).

### WorldObjects

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `PlayerBox.cpp` | `0x00413218`-`0x004133d0` | 2 | `0x00414280` | `0x00413218` (`FreeList<PlayerBox>`); `0x00413398` |
| `TurfBox.cpp` | `0x00414d48`-`0x00414f08` | 2 | | `0x00414d48` (`FreeListContainer<TurfBox>`); `0x00414ed0` |
| `VolumeBox.cpp` | `0x00414f68`-`0x00415118` | 2 | | `0x00414f68` (`FreeList<VolumeBox>`); `0x004150e0` |
| `WorldPath.cpp` | `0x00415690`-`0x00415878` | 5 | | `0x00415740` (`WorldPath`) |
| `flags.cpp` | `0x004158f8`-`0x00417ad0` | 51 | `0x00417ad0` | `0x004158f8` (`WorldFlag`); `0x00415a38` |

### Warriors

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `W_ActionableManager.cpp` | `0x00417b10`-`0x00417b88` | 1 | | `0x00417b10` (`W_ActionableManager`) |
| `W_PS2SaveSystem.cpp` | `0x0041f8b0`-`0x00420928` | 18 | `0x004209a0` | `0x00420060` (`BASLUS-21215/BASLUS-21215`, `BASLUS-21215/WARR.ICO`); `0x004206a0` (`BASLUS-21215/icon.sys`, `WARR.ICO`) |
| `W_SaveSystem.cpp` | `0x00421348`-`0x00421578` | 2 | `0x00423338` | `0x00421348` (`FS_MemoryFile`); `0x004214c0` |
| `W_UnlockManager.cpp` | `0x00423ab8`-`0x00423c30` | 2 | `0x00424ec8` | `0x00423ab8` (`W_Unlockable`); `0x00423b88` |

### Movie

| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) |
| --- | --- | --- | --- | --- |
| `BinkMovie.cpp` | `0x00429b18`-`0x0042a6c0` | 5 | | `0x00429f50`; `0x00429fa8`; `0x00429fe8` (`cdrom0:\%s;1`, `Bink`) |
| `PlayMovie.cpp` | `0x0042a938`-`0x0042af70` | 1 | | `0x0042a938` (`%s_sub.scn`, `WarMoveInstance`) |

## Middleware {#middleware}

The libraries are linked after the game's object files, in this order (inferred from the order of their strings in
`.rodata`, which matches `.text`). Ends marked `~` are where calls into and out of the block stop, to within a few
functions.

| Range | Library | How it was placed |
| --- | --- | --- |
| `0x00323298`-`~0x00335320` | **Lua 4.0.1** core and libraries, compiled as C++ | path `lua-4.0.1/src/lmem.cpp` (anchor `0x0032c758`); Lua's own messages (`_ERRORMESSAGE` at `0x00325800`, `syntax error` at `0x0032e6d0`, `` `for' limit must be a number`` at `0x00334478`) are loaded by functions from `0x003234e8` to `0x00334478`; the first function, `0x00323298`, is called from `Scripting/` and the code before it from the AI ([position](#position)) |
| `~0x00408ac8`-`0x0040c5e0` | **tolua** (`c:/Warriors/System/tolua/src/lib/`) | anchor `0x0040a5b0` (`tolua_tm.cpp`); the `tolua_tbl_*`/`tolua_tag_*` registry names are loaded by 27 functions from `0x00408e98` to `0x0040c3a8`; the last function that calls the Lua API is `0x0040c538` |
| `0x0042b040`-`~0x00432000` | GCC 2.x C++ runtime (exceptions, `type_info` and its `__si_type_info`/`__class_type_info` family) | its type-name strings, the only RTTI names in the executable |
| `~0x00432000`-`~0x0043a000` | C library (locale tables `C-SJIS`/`C-EUCJP`, `printf` family) | strings |
| `~0x0043a000`-`~0x00446000` | SCE: `libcdvd` (`SceCdNcmdSema`, `0x0043bca0`), `libkernel` (named syscall stubs `0x0043cca0`-`0x0043d510`), SIF RPC, stdio, `libmc` (`sceMc_sema_regs`, `0x00444ea0`) | strings and Ghidra's syscall names |
| `0x00446000`-`~0x0044c600` | SCE `libmpeg` (MPEG-2 decoding for the IPU) | error strings (`_sceMpegSliceA0(): error happens`, `0x0044b8b8`) |
| `~0x0044c600`-`0x0044e060` | SCE `libpad` | `libpad: Module version mismatch` at `0x0044cc68` |
| `0x0044e060`-`~0x004aa000` | **RenderWare Graphics** for PS2 | the global at `0x0070ad18` (probably the RenderWare engine instance pointer, speculative) is loaded by 212 functions in this block and by a few game functions; PS2 pipeline strings (`FastIm3DTransform` `0x00473f00`, `PS2AllMatProcessInitData` `0x00479ba0`, `PS2 material pipes` `0x0047b560`, `Invalid projection type specified` `0x00482980`) |
| `~0x004aa000`-`~0x004bb000` | libio / libstdc++ streams (`filebuf`, `streambuf`, `ostdiostream`), maths library (`log10`), a `rom0:ROMVER` reader (`0x004aa320`) | strings |
| `~0x004bb000`-`~0x004d8000` | **Bink** (RAD Game Tools) | `Not a Bink file.` and the other open errors at `0x004c1cd0`; its rodata block (`0x00591200`-`0x00596200`) is loaded only from this range |
| `~0x004d8000`-`~0x004da118` | runtime leftovers: a second `rom0:ROMVER` reader (`0x004d8868`), `operator new`/`bad_alloc` (`0x004d9ec0`) | strings |

The game's own Bink and RenderWare glue is game code: `Movie/BinkMovie.cpp` (`0x00429b18`-`0x0042a6c0`) and
`Graphics/Devices/Renderware/DevRWGeneric.cpp` (`0x00192908`-`0x00197cd0`).

## Coverage

Bytes of `.text` (4,154,744) and functions (13,789), each counted once, in the first category that applies.

| Category | Functions | Bytes | Share of `.text` |
| --- | --- | --- | --- |
| Anchors: file confirmed (code) | 302 | 208,296 | 5.0% |
| File inferred (steps 4 to 6, and two [by position](#position)) | 1,762 | 429,792 | 10.3% |
| Directory inferred (step 7) | 4,395 | 1,076,912 | 25.9% |
| Directory inferred from position and calls (step 9) | 4,183 | 1,502,920 | 36.2% |
| Lua 4.0.1 and tolua | 493 | 82,440 | 2.0% |
| Other middleware and crt0 (C/C++ runtime, SCE, RenderWare, Bink) | 2,016 | 717,520 | 17.3% |
| Game link-once code (templates, inlines) | 603 | 115,808 | 2.8% |
| Unknown | 35 | 21,048 | 0.5% |

So 15.4% of `.text` is tied to a named file, 77.5% to at least a directory, and 19.3% is middleware or runtime. Of
the file-level share, 5.1% comes from extending files to their static-init stubs (step 6); without it the attributed
ranges in the tables cover 418,984 bytes (10.1%). RenderWare alone is 376,960 bytes (9.1%).

The unknown share left is the three stretches at the end of [Placed by position and calls](#position).

The full anchor list and every attributed function are reproducible with the [method](#method).

## For the next steps

What the [roadmap](../roadmap.md)'s "Boot the engine" step needs, with where it lives:

- **Boot path and main loop:** [Boot and the main loop](boot.md). `main` (`0x001446d0`) calls
  `GameModes/Initialize.cpp` (`0x00160df8`), plays the start-up movies, pushes game modes and runs the game-mode
  stack (`0x0015e6b8`), which is the frame loop. The virtual call through `0x00515024` is a save-system call
  (`W_PS2SaveSystem`, slot `+0xe8` = `0x0041f960`) that reads a QA file from the memory card, not the main loop.
  `0x0042b020`, called first in `main`, is the C++ runtime's `__main` (it runs the global constructors).
- **Chunk system:** [Chunk system](chunk-system.md), with all 84 chunk types and their handlers. In the type table
  `0x0050b2e0`, field `a` (`+4`) is the "on loaded" handler and field `b` (`+8`) reads the chunk from the stream
  itself. The CRC helpers before `ChunkSystem.cpp` form a unit of their own, `0x00143ea0`-`0x001440a0` (table
  builder, buffer and string CRCs, and a static initialiser that fills the table at `0x005d91e0`), ended by the stub
  `0x00144080`. It comes after the last camera code and holds none, so it is taken as the first unit of `Core/`
  (inferred); no string names it. `ChunkSystem.cpp` then starts right after the stub, with the chunk stack helpers
  (`0x001440a0`-`0x00144180`, inferred: they work on the stack the anchored loaders use).
- **File I/O:** [File I/O](file-io.md): the device's file systems (`DS_PS2Device.cpp`, `DS_PS2FileSys.cpp`), the WAD
  index (`DVDWadIndexPS2.cpp`), `FS_MemoryFile.cpp` and `StreamManager.cpp` (the buffered reader and the
  `FileManager` request queue). The WAD-opening code at `0x0040c5e0`-`0x0040c688` is game code, so tolua ends
  before it. The IOP sound bank path `cdrom0:\IOP\BFW.SND;1` is opened from `Device/ps2/sound/msaudiodevice.cpp`
  (`0x0014bbd8`).
- **Memory:** [Memory](memory.md). `Memory/` at `0x00338420`-`0x0033b1a0`, followed by the heaps' block allocator
  (`0x0033b1a0`-`~0x0033c288`, no path string); `Memory/WarriorsMemory.cpp` (`0x0033afe0`) sets up the
  `Level Dynamic & LUA Memory` heap.
- **Units without path strings near the boot path** (inferred from stubs and link order; details on
  [Boot](boot.md#original-structure)): mode 0xd's unit (`0x00155b30`-`0x00155ed8`, stub `0x00155eb8`) and the
  game-mode base's (`0x00155ed8`-`~0x00156d20`) open `GameModes/`, whose files link in alphabetical order;
  `GameTimer` (`0x00145940`-`0x00145fa0`) sits among `DS_PS2Device`'s methods and the pad code, in `Device/ps2/`,
  with no stub between `0x001449e8` and `DS_PS2Device.cpp`'s `0x001485a8`; the two texture chunk readers
  (`0x001906e8`, `0x00190770`) close the stretch after `Graphics/Texture.cpp`, probably `Graphics/WarTexture.cpp`
  (speculative; [Chunk system](chunk-system.md#open-questions)); `Stream_SkipBytes` (`0x00154440`) is the last
  function of `FileIO/FS_MemoryFile.cpp`.
- **Physics:** [Physics](physics.md). `IPhysics` starts at its constructor `0x0033c288`, right after the heaps' block
  allocator, so `0x0033c288`-`0x003418f8` is taken as `Physics/` too (inferred).
- **Level loading:** [Level loading](level-loading.md) and [The streamed world](world.md).
  `World/ps2/WorldManagerPS2.cpp` (`0x0040d688`: `Sector Pool`; `0x0040d900`: `warriors.glr`, `Global Data`;
  `0x0040dbb8`: `%s.lev`; `0x0040f8a0`: the streaming update) and `World/ps2/WorldPS2.cpp` (`0x00410648`:
  `%s_sec.wld`, `%s_ms%i.sec`).
- **Graphics:** [Graphics device and textures](graphics.md): the RenderWare device in
  `Graphics/Devices/Renderware/DevRWGeneric.cpp` (created by `0x00194488`, vtable `0x00538d78`), its camera wrapper
  (vtable `0x00538f78`), texture dictionaries (`Graphics/Texture.cpp`) and the world streaming loaders in
  `World/ps2/WorldPS2.cpp`.
- **Scripts:** the Lua bindings are the `Scripting/` block (`0x00356390`-`0x003865d8`), mostly around
  `ScriptLua.inl` (anchor `0x0036bb10`).
- **Front end and GUI:** [Start-up and the front end](frontend.md) (game modes 5, 6, 8, 0x12 and 1, the pads, the
  profile-manager screens in `GUI/ProfileManagementGUI/`) and [GUI](gui.md) (`ScreenFlowController.cpp`, the
  widgets, sprite sheets from `Graphics/ParticlePage.cpp`, text and draw order).

## Open questions

- **18 files have no anchor:** `Audio/SoundCallBack.cpp`, `Debug/DebugStream.cpp`, `Device/ps2/DS_PS2TCPSocket.cpp`,
  `Device/ps2/fileio/RockWadIndexPS2.cpp`, `Device/ps2/Shell/Core/shellMemory.cpp`, `FileIO/FS_TCPSocketFileSys.cpp`,
  `FileIO/FS_WriteCachedFile.cpp`, `Graphics/CharacterList.cpp`, `Graphics/DependencyList.cpp`,
  `Graphics/ObjectList.cpp`, `Graphics/ParticlePageList.cpp`, `Graphics/WarTexture.cpp`,
  `Graphics/Devices/Renderware/DevRWDebug.cpp`, `GUI/RumbleModeGUI/RM_BuySoldiers.cpp`,
  `Human/pathfinding/path_waypoint.cpp`, `Memory/FreeListMemoryPool.cpp`, `Scene/WarMovement.cpp`,
  `Utils/Dictionary.cpp`. Their code lies between their neighbours in the tables; vtables in `.data` (which follow
  the same file order) are the next thing to try.
- **Is `main` in `Core/ChunkSystem.cpp`** or in an unnamed `Core/` file (say `Main.cpp`) that has no path string?
  The stub at `0x00144080` ends the unit *before* `ChunkSystem.cpp`, and no stub separates `ChunkSystem.cpp`'s
  functions from `main`, so `main` is in `ChunkSystem.cpp` unless a unit without global constructors sits between
  them (inferred).
- **Files inside the stretches placed by position:** the 718 KB of `Human/` after `cns/cnsplayertag.cpp` and the
  394 KB of script types in `TaskEngine/` have no path strings. Splitting them into files needs other per-TU
  markers: the `MemoryStl.h` copies in `.rodata`, vtables in `.data`, or class tags passed to the allocator.
- **Gangs' directory:** `GameModes/`, or a directory of their own between it and `Graphics/`?
- **Maths unit** (`0x00335320`-`0x00338420`): random numbers, quaternions, ray-triangle tests; no page covers it.
- **Pipelines' directory** (`0x00424ee8`-`0x00429b18`): `Warriors/`, `Movie/` or a directory of their own?
- **Directory walker** (`0x00153118`, `/%s%s%s/*`, in `Device/ps2/`): what calls it through which pointer, and
  for which device.
- **Middleware boundaries** are good to a few functions only. FLIRT-style signatures for the SCE SDK 3.0.0
  libraries, and a RenderWare build with symbols, would pin them and name the functions.
- **Bink and RenderWare:** functions at `0x004be000`-`0x004c1000`, inside the Bink block, call into RenderWare. Are
  they RAD's own RenderWare glue, or does the RenderWare block extend there?
- **Tooling:** the scripts behind this map were throwaway. A `coney-tools` or Rekit command that rebuilds the map
  from a disc (printing only counts and addresses) would keep it repeatable.
