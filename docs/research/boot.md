# Boot and the main loop

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims.

## Purpose

What happens between the console starting the executable and the first frame the player sees, what runs every frame
after that, and how the game would stop. Every other subsystem is created somewhere on this path, so this page is
also the map of *when* each one comes to life. The chunk loader the boot path uses is on its own page,
[Chunk system](chunk-system.md); the disc and file layers are on [File I/O](file-io.md).

In one paragraph: `main` creates every subsystem in one long initialisation function, briefly loads the first level's
header so it can play the three start-up movies, then pushes game modes onto a **game-mode stack** and runs that stack.
The **main loop is the game-mode stack's run loop** (`0x0015e6b8`): every iteration calls the top mode's `Update`,
which runs one whole frame (simulation, rendering, scripts, present with vsync, file streaming). There is no other
frame loop. The virtual call through `0x00515024` that the [source map](source-map.md#for-the-next-steps) suspected of
being the main loop is not: it is a save-system call that reads a QA file from the memory card (see
[main](#main)).

## Original structure

| Address | Name (ours) | File | Role | Evidence |
| --- | --- | --- | --- | --- |
| `0x00100008` | `entry` | crt0 | clears `.bss`, sets up thread and heap, calls `main`, exits | confirmed (code) |
| `0x0042b020` | `__main` | C++ runtime | runs the global constructors once (`0x0042af70` walks the list at `0x00534060` backwards) | confirmed (code) |
| `0x001446d0` | `main` | `Core/` (file unknown) | boot sequence, then runs the game-mode stack | confirmed (code) |
| `0x00160df8` | `Game_InitializeSubsystems` | `GameModes/Initialize.cpp` | creates every subsystem, in the order below | confirmed (code) |
| `0x00145790` | `DS_PS2Device_Init` | `Device/ps2/DS_PS2Device.cpp` (class) | IOP, memory, pads, renderer, file systems | confirmed (code) |
| `0x0033afe0` | | `Memory/WarriorsMemory.cpp` | game heaps | confirmed (code) |
| `0x0015e5e8` | `GameModeStack_Push` | `GameModes/` (base file) | push a mode | confirmed (code) |
| `0x0015e650` | `GameModeStack_Pop` | `GameModes/` | pop the top mode | confirmed (code) |
| `0x0015e6b8` | `GameModeStack_RunUntilEmpty` | `GameModes/` | **the main loop** | confirmed (code) |
| `0x0015e718` | `GameModeStack_Top` | `GameModes/` | top mode or null | confirmed (code) |
| `0x0015e748` | `GameModeStack_TopId` | `GameModes/` | id of the top mode, 0 when empty | confirmed (code) |
| `0x00155ed8` | `GameMode::GameMode` | `GameModes/` | base constructor | confirmed (code) |
| `0x0015d160` | `Gm_InGame::Update` (our name) | `GameModes/` (unnamed file) | one in-game frame | confirmed (code) |
| `0x00145a10` | `GameTimer::Update` | `Core/` region | game clock with a fixed-step mode | confirmed (code) |
| `0x0041f960` | `W_PS2SaveSystem_ReadBugstarFile` | `Warriors/W_PS2SaveSystem.cpp` | the `0x00515024` virtual call | confirmed (code) |

`main` sits between `Core/ChunkSystem.cpp`'s functions and the static-initialiser stub at `0x001449e8`, with no stub
in between, so it was most likely compiled as part of `ChunkSystem.cpp` (inferred; an unnamed `Core/` file without
global constructors would also fit).
The game-mode base class's functions (`0x00155b30`-`0x00156d20`) lie between `FileIO/StreamManager.cpp`'s stub
(`0x00155b10`) and `GameModes/Gm_Error.cpp`: a `GameModes/` file with no path string (inferred).

## Data

### Global singletons

All are pointers in `.data` set during initialisation; the table gives what each points to.

| Global | Points to | Created at | Evidence |
| --- | --- | --- | --- |
| `0x005127e4` | memory manager (`0x006eb958`, vtable `0x005444e0`) | `0x0033afc8`, from `DS_PS2Device_Init` | confirmed (code) |
| `0x0050b788` | platform device `DS_PS2Device` (`0x005de160`, vtable `0x00537c80`) | static constructor `0x00148580` | confirmed (code) |
| `0x0050b8b8` | `Timer` (24 bytes, real time) | `Game_InitializeSubsystems` | confirmed (code) |
| `0x0050b734` | `GameTimer` (0x68 bytes, game time) | `Game_InitializeSubsystems` | confirmed (code) |
| `0x005e5340` | `FileManager` (asynchronous file reader, see [File I/O](file-io.md#filemanager)) | `0x001547b0` | confirmed (code) |
| `0x006f39e8` (via `0x005147a4`) | the WAD: `+0` is the `DVDWadIndex*` | `0x0040c5e0` | confirmed (code) |
| `0x0050cdb4` | RenderWare graphics device (0x460 bytes, vtable at `+8` = `0x00538d78`) | `0x00194488` | confirmed (code) |
| `0x0050cd4c` | `ResourceManager` (`global.pak`) | `0x00184918` | confirmed (code) |
| `0x00512b04` | script system (Lua) | `0x00356390` | confirmed (code) |
| `0x00512c7c` | `TaskManager` (0x878 bytes) | `Game_InitializeSubsystems` | confirmed (code) |
| `0x005147c4` | `WorldManager` (0x60 bytes) | `Game_InitializeSubsystems` | confirmed (code) |
| `0x0051489c` | `W_GameState` (0x57c0 bytes) | `Game_InitializeSubsystems` | confirmed (code) |
| `0x00515024` | save system `W_PS2SaveSystem` (`0x006fe2f8`; vtable at `+0x128` = `0x00545fa0`) | static constructor `0x00420970` | confirmed (code) |

### Game mode

A game mode is a screen or state of the game (front end, in-game, memory card, error). Every mode derives from one
base class (constructor `0x00155ed8`, vtable `0x005383a8`). Layout of the base, 0x20 bytes:

| Offset | Size | Meaning | Evidence |
| --- | --- | --- | --- |
| `+0x00` | 8 | timestamp of the previous `Update`, in the mode's timer's ticks | confirmed (code) at `0x00156220` |
| `+0x08` | 4 | the timer the mode runs on: `Timer` or `GameTimer`, set by `Enter` | confirmed (code) at `0x00155b30`, `0x0015cf70` |
| `+0x0c` | 4 | state: 0 = not entered, 1 = entered | confirmed (code) at `0x00155f78`, `0x00155ff0` |
| `+0x10` | 4 × 2 | a per-viewport screen-effect value saved on `Enter` and restored on `Exit` (two viewports) | confirmed (code) at `0x00155f78`; meaning inferred |
| `+0x18` | 4 | vtable pointer | confirmed (code) |

Derived modes put their own fields from `+0x20`. Virtual slots (GCC 2 layout, 8-byte `{delta, fn}` entries, see
[Compiler](compiler.md)); offsets are from the vtable start:

| Slot | Method | Base behaviour | Evidence |
| --- | --- | --- | --- |
| `+0x08` | destructor | | inferred |
| `+0x10` | `GetId()`: returns the mode's id | | confirmed (code), every derived class returns a constant |
| `+0x18` | `Enter()` | empty | confirmed (code) |
| `+0x20` | `Exit()` | empty | confirmed (code) |
| `+0x28` | `Resume()`: the mode is on top again after the one above it was popped | empty | confirmed (code) |
| `+0x30` | `Suspend()`: another mode was pushed on top | empty | confirmed (code) |
| `+0x38` | `Update()`: run one frame; return non-zero to stay, 0 to be popped | pure virtual | confirmed (code) at `0x0015e6b8` |

The stack itself is an array of 4-byte mode pointers at `0x005e66a0` with the index of the top entry at
`0x0050c784` (initially -1, empty). No bound check is made on push (the array's size is not known; 16 entries fit
before the next known global, inferred).

The modes, each a static object built by a static constructor (or a constructor function) and identified by its id:

| Id | Object | Vtable | `Enter` | Source file | Role | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | (ctor `0x001582a8`) | `0x00538480` | `0x001582e0` | `GameModes/` | | confirmed (code) id |
| 5 | `0x005e57e0` | `0x00538538` | `0x00159a58` | `GameModes/` | timed start-up screen: stays 5,000 ms or until a button | confirmed (code) at `0x00159ae0`; "start-up screen" inferred |
| 6 | `0x005e5810` | `0x00538580` | `0x0015baa0` | `Gm_MemoryCard.cpp` | memory card checks and saving | confirmed (code) (anchor) |
| 7 | `0x005e6800` | `0x005388b8` | `0x0015f830` | `Gm_XboxSaveSystem.cpp` | save-system screens | confirmed (code) (anchor) |
| 8 | `0x005e5d90` | `0x005385c8` | `0x0015c688` | `GameModes/` | bottom of the stack in `main`; runs a world frame; picks the level (`0x0015c7b0`) | confirmed (code); role inferred |
| 0xa | `0x005e6550` | `0x005386e8` | `0x0015dbb8` | `GameModes/` | | confirmed (code) id |
| 0xb | `0x005e5df8` | `0x00538658` | `0x0015cf70` | `GameModes/` | **in-game**: runs the game world on `GameTimer` | confirmed (code); role inferred |
| 0xc | `0x005e5dc0` | `0x00538610` | `0x0015cae0` | `GameModes/` | | confirmed (code) id |
| 0xd | `0x005e53a0` | `0x00538360` | `0x00155b30` | `GameModes/` (base file) | | confirmed (code) id |
| 0xe | (ctor `0x00157e48`) | `0x00538438` | `0x00157e88` | `GameModes/` | | confirmed (code) id |
| 0xf | `0x005e5560` (ctor `0x00156d20`) | `0x005383f0` | `0x00156dd8` | `Gm_Error.cpp` | error screen (disc error, controller removed) | confirmed (code) |
| 0x10 | (ctor `0x0015d4f8`) | `0x005386a0` | `0x0015d648` | `GameModes/` | | confirmed (code) id |
| 0x11 | `0x005e66d0` | `0x00538828` | `0x0015e8b0` | `GameModes/` | | confirmed (code) id |
| 0x12 | `0x005e65c0` | `0x00538730` | `0x0015e048` | `GameModes/` | | confirmed (code) id |
| 0x13 | `0x005e56f0` | `0x005384c8` | `0x00159538` | `GameModes/` | | confirmed (code) id |
| 0x14 | `0x005e67d0` | `0x00538870` | `0x0015f1a0` | `GameModes/` | | confirmed (code) id |

The modes without a role are for the analysts of the front end and gameplay to name; the boot path only needs 5, 6,
8, 0xb and 0xf.

### Timers

Both timers count the EE's cycle counter (COP0 `Count`, 294.912 MHz) into a 64-bit total, adding the 32-bit
difference since the last read and handling wrap-around (`0x00148fd8`). Confirmed (code). Conversions: milliseconds
= ticks / 294,912 (`0x004dcdd8`); seconds = ticks × 3.390842e-9 (`0x004dcf00`). Confirmed (code).

`GameTimer` (`0x00145940`, vtable `0x00537bc0`) wraps a `Timer` at `+0x08` and keeps game time at `+0x30` (64-bit
ticks). Its `Update` (`0x00145a10`) has two modes, chosen by the flag at `+0x54` (set by `0x00145f60(timer, 1)`,
cleared by `0x00145f60(timer, 0)`, which also resynchronises the real-time base):

- **Fixed step** (`+0x54 == 1`): unless paused, game time advances by exactly `0x960000` ticks (1/30 s) per update.
  The float at `+0x4c` is 1/30 (`0x3d088889`) and `+0x50` is a time scale of 1.0. Confirmed (code).
- **Real time** (`+0x54 == 0`): game time advances by the real elapsed time, clamped to `0xb40000` ticks (40 ms)
  per update unless the player's camera is in state 4. Confirmed (code); what camera state 4 is, is open.

`+0x48` holds the game time in milliseconds after each update. Confirmed (code).

## Behaviour

### entry (crt0)

`entry` (`0x00100008`) zeroes `.bss` from `0x00596f80` to `0x00715b5c` in 16-byte stores, makes the kernel calls that
set up the main thread (stack top `0x018da4a4`, 64 KB) and the heap (from the end of `.bss`), runs `0x00443530` (SDK
start-up), flushes the caches, enables interrupts, calls `main(argc, argv)` and passes its return value to the exit
call `0x00443740`. Confirmed (code) for the order; the roles of `0x00443530` and the syscalls follow the standard SCE
crt0 (inferred). Coney has no equivalent: its own `main` starts directly with the next step.

### main {#main}

Confirmed (code) at `0x001446d0`, in this order:

1. **Global constructors** (`0x0042b020`, runs once). They build the static objects: the platform device (only its
   vtable), the memory manager object, the game-mode objects, the save system and others.
2. **Build stamps.** `sprintf` of `"<Release Sep 25 2005 20:21:44>"` into `0x005dd5f8` and
   `"<104242> <BuildApe>"` into `0x005dd6f8`. Debug text only.
3. **`Game_InitializeSubsystems()`** (below).
4. Clear the "no controller" flag (`0x005e5580 = 0`).
5. **Load `level1`, header only**: `WorldManager::LoadLevel(world, "level1", 1)` (`0x0040dbb8`). The third argument
   skips the sector data (`%ss_sec.wld` and friends), so only `level1.lev` is loaded into a `World Level Pool` sized
   at 103% of the file, at least 256 KB (confirmed (code)). Its purpose here is inferred: the intro movie `L1_IN`
   needs level 1's subtitle data.
6. With the `Level Dynamic & LUA Pool` heap current: subtitle system setup (`0x001cabc0` on the object at
   `0x00619570`).
7. **Movies**: `PlayMovie("LOGO", 0)`, `PlayMovie("PLOGO", 1)`, `PlayMovie("L1_IN", 1)` (`0x0042a938`; the second
   argument's meaning is open). Each movie blocks until it ends.
8. **Unload level 1** (`0x0040f5b8(world, 0)`).
9. **Game modes**: push mode 8 (`0x005e5d90`); set the memory-card mode's "boot check" flag (`0x0015a270(1)`);
   push mode 6 (memory card, `0x005e5810`).
10. **Save system virtual call** through `0x00515024`: the object is `W_PS2SaveSystem` at `0x006fe2f8`, its vtable
    pointer is at `+0x128`, and slot `+0xe8` is `0x0041f960`. That function opens memory card port 1, and if the file
    `BASLUS-21215/bugstar.dat` exists, reads 20 bytes from it and hands them to `W_GameState` (`0x00418bf0`) under
    the name `UNUSED`. Bugstar is a bug-tracking tool, so this is a QA hook (inferred); a retail card has no such
    file and nothing happens. It is **not** the main loop. Confirmed (code) for the behaviour.
11. Copy 20 bytes from `W_GameState + 0x46c` into a local buffer (`0x00418bc8`); unused afterwards (confirmed (code)).
12. **Controller check** (`0x00157860`): if no controller is connected, set the "no controller" flag; then
    `0x0015e7e8` pushes the error mode (id 0xf) if the flag is set and the error mode is not already on top.
13. Push mode 5 (`0x005e57e0`), the timed start-up screen.
14. **`GameModeStack_RunUntilEmpty()`**: the main loop.
15. `return 0` to crt0, which exits.

So the first frames belong to mode 5, then (when it pops) mode 6 checks the memory card, then mode 8 takes over.

### Game_InitializeSubsystems {#initialisation-order}

`0x00160df8` (`GameModes/Initialize.cpp`). Allocations name their class (tag) and size; "heap" means the memory
manager's current pool at the time. Confirmed (code) for the order and sizes; roles beyond the tag are inferred.

| # | Call | What it creates or does |
| --- | --- | --- |
| 1 | `0x00338680` | clears the memory manager's 50-entry registered-pool table (`0x00715628`) |
| 2 | device `Init` (`DS_PS2Device_Init`, slot `+0x50`) | see [Device initialisation](#device-initialisation) |
| 3 | `0x0033afe0` (`WarriorsMemory.cpp`) | free lists for animation instances, animation tasks and sound tasks; the **`Level Dynamic & LUA Memory`** heap of `0x1ef000` bytes (2,027,520) in the global heap, registered as `Level Dynamic & LUA Pool` (global `0x006eb9b8`) |
| 4 | `0x001547b0` | `FileManager` and its 384 KB `File Stream Buffer` ([File I/O](file-io.md#filemanager)) |
| 5 | alloc `Timer` (0x18) | real-time clock → `0x0050b8b8` |
| 6 | alloc `GameTimer` (0x68) | game clock → `0x0050b734` |
| 7 | alloc `W_StopWatch` (0x50) | → `0x0051504c` |
| 8 | `0x0040c5e0` | opens the WAD: reads `WARRIORS.DIR` into a `DVDWadIndex` and opens `WARRIORS.WAD` on the IOP ([File I/O](file-io.md#the-wad-index)) |
| 9 | `0x0040cf18` | registers the `Level Header` chunk handler ([Chunk system](chunk-system.md#handlers-registered-at-run-time)) |
| 10 | alloc `IPhysics` (`0x4f460`) | physics → `0x00597198` |
| 11 | `0x0016fcc8`, `0x00171540`, `0x00183510` | `ICameraGarbage`, `ICameraGroundFog`, `IRainDrops` effects |
| 12 | `0x00356390` | `ScriptLua`: the Lua state; sets the script system global `0x00512b04` |
| 13 | `0x003535e8` | scene cache: loads `scene_list.cnk` through the `FileManager` (blocking) and resets 12 scene slots |
| 14 | alloc `G_GangAttribs` (0xd58) | → `0x0050caa8` |
| 15 | `0x00104818` | `AnimationSystem` (0x88); registers the `Anim Data` chunk handler |
| 16 | alloc `TaskManager` (0x878) | → `0x00512c7c` |
| 17 | alloc `ObjectAttribs` (`0x34e0c`) | → `0x00512c04` |
| 18 | `0x00306520`, `0x00321828` | reset two tables in the unattributed `Human/` region |
| 19 | `0x0018ba10` | `ScreenEffectsManager` |
| 20 | `0x00417b10` | `W_ActionableManager` |
| 21 | `0x0040d688` | **sector heaps**: `Sector Pool` = the largest free block minus 128 KB, then `Sector Pool 2` = what is left minus 128 KB (at least 4 KB) |
| 22 | `0x00184918` | `ResourceManager` (`global.pak`) → `0x0050cd4c` |
| 23 | `0x00293840` | resets AI/character tables in the unattributed `Human/` region |
| 24 | alloc `W_GameState` (0x57c0) | → `0x0051489c` |
| 25 | `0x0017d640` | `LightManager` → `0x0050cce4` |
| 26 | alloc `WorldManager` (0x60), constructor `0x0040d900` | creates the `Global Data` heap (101% of `warriors.glr`'s size) and loads `warriors.glr` into it through the chunk system → `0x005147c4` |
| 27 | `0x0011e9c0` | camera system reset |
| 28 | `0x0010f768` | audio: music start time and volumes |
| 29 | save system slot `+0x10` (`0x00421348`) | `W_SaveSystem` setup: six save buffers in one `FS_MemoryFile` |

### Device initialisation {#device-initialisation}

`DS_PS2Device_Init` (`0x00145790`), the platform layer's start-up. Confirmed (code) for the order.

1. File-system stack index = -1 (`+0x04`).
2. **IOP** (`0x00148230`): `sceSifInitRpc`, reboot the IOP with `cdrom0:\MODULES\IOPRP300.IMG;1`, wait, re-init, then
   load six modules, retrying each until it loads: `SIO2MAN.IRX`, `PADMAN.IRX`, `LIBSD.IRX`, `IOP.IRX` (the game's own
   IOP module, which streams disc data and audio, see [File I/O](file-io.md#the-iop-stream)), `MCMAN.IRX`,
   `MCSERV.IRX` (all under `cdrom0:\MODULES\`).
3. Slot `+0x68`: empty.
4. **Memory**: set the memory-manager global (`0x0033afc8`), then its `Init` (slot `+0xb0`, `0x00338e10`). The arena is
   one `malloc` of `0x018d7c94` bytes (26,049,684; `0x00148828`, which also fills 2 KB at `0x01fef800` with `0xcd`).
   Pools created, in order: `All System` (the arena), `System Memory Pool`, `Filter Memory` (`0x29000` bytes),
   `Global Memory` (the rest; the default heap afterwards), `Debug Pool` (1 KB on retail: a second, debug-only arena
   is asked for and is absent), a free list of 512 registered pools, `Debug Heap` and `Filter Pool`. Confirmed (code)
   for names and sizes; the memory system's own page is still to be written.
5. Slot `+0xa0` (`0x001488f8`): **pads** (`scePadInit`, two port opens, eight 0x140-byte pad records starting near
   `0x005de4c0`; the library calls are identified by `libpad`'s position, inferred) and audio device setup (`0x0010f618`).
6. **Renderer**: `0x00194488(0, 0)` creates the RenderWare device (`Renderware`, 0x460 bytes) and starts RenderWare;
   `0x0017a1e0` finishes graphics setup.
7. **File systems**: slot `+0x58` creates `PS2StreamFileSys`; slot `+0x60` creates `FS_FSToStreamFSFileSys` over it
   and mounts it as the current file system; slot `+0x80` creates `PS2FileSys` on `host0:` with root `debug/`. Slot
   `+0x88` copies the current file system to `+0x88`. See [File I/O](file-io.md).

### The main loop {#the-main-loop}

`GameModeStack_RunUntilEmpty` (`0x0015e6b8`), confirmed (code):

```text
while stack is not empty:
    mode = top
    if mode.state == 0: mode.Enter()        # wrapper 0x00155f78 sets state = 1
    if mode.Update() == 0: pop()
```

Stack operations, confirmed (code):

- **Push** (`0x0015e5e8`): if the current top has been entered, call its `Suspend`; then place the new mode on top.
  The new mode is entered by the loop on its first iteration, not by the push.
- **Pop** (`0x0015e650`): remove the top, call its `Exit` (wrapper `0x00155ff0`: restores the saved screen-effect
  values, calls `Exit`, sets state = 0), then call `Resume` on the new top if it has been entered.

Modes push and pop each other from inside `Update` (for example the in-game mode pushes the memory-card mode when a
save is due, `0x00155308`, and pushes the error mode when a controller is unplugged).

**There is no frame pacing in the loop itself.** Each mode's `Update` renders and presents its own frame, and the
present waits for vertical sync (below). The loop ends only when the stack is empty; modes 8 and 0xb always return
1, so on a retail run it never ends (inferred).

### One in-game frame {#one-frame}

The in-game mode's `Update` (`0x0015d160`) is the frame Coney's fixed-timestep loop has to reproduce. Mode 8's update
(`0x0015c858`) runs the same sequence. Confirmed (code) for the order; the roles of the callees are inferred from the
files they belong to.

1. Switch `GameTimer` to fixed step (`0x00145f60(gameTimer, 1)`).
2. **Tick** (`0x00156220`): `timer.Update()`, `dt = timer.ElapsedSince(mode+0x00)`, `mode+0x00 = timer.Now()`. With
   the fixed step on, `dt` is 1/30 s.
3. Task manager, phase 0 setup (`0x003a3148`).
4. **Cameras** (`0x001562c8`): read the player-1 camera and hand its view to the render device (slots `+0x28`,
   `+0x38`).
5. **Simulation**: task manager phase 0 (`0x003a31a8`): object tasks, AI and per-viewport screen effects.
6. Characters (`0x00249b98`), resource streaming (`0x0018a980`), render device slot `+0x18` (RenderWare frame
   begin), world update (`0x0040f8a0`), resource manager (`0x00186068`).
7. **Render each viewport** (`0x00156408`, one or two): device begin-viewport (slot `+0x88`), lights, world sectors,
   resources, particles (`0x0017b2e0`), device slot `+0x118`.
8. **Overlays** (`0x00156658`): HUD (`0x001b1688`), subtitles, the front-end layers when active, screen effects with
   `dt` (device slot `+0x128`).
9. **Scripts**: `scriptSystem.Update(dt)` (slot `+0x10`).
10. **Present** (device slot `+0x30`, `0x001958b0`): the camera's show-raster call with flag 1, then clear the
    "cameras set up this frame" flag (`0x0050b6f8`). In RenderWare, show-raster flag 1 is "wait for vsync"
    (inferred).
11. **File streaming**: `FileManager_Service(fileManager, 0)`: collect a finished asynchronous read and start the
    next ([File I/O](file-io.md#filemanager)).
12. Push the error mode if needed (`0x0015e7e8`).
13. Debug FPS counter (`0x001569c0`; prints frames per second, free memory and draw distance to a buffer).
14. If the mode was asked to leave (`+0x24` holds a reason, non-zero): pop it; for reason 2 select a level by name
    in mode 8 (`0x0015c7b0`), for reason 3 select the next level; call save-system slot `+0xb0`; pop once more if
    the new top is mode 1; update two per-player records in `W_GameState + 0x480`; push the memory-card mode if a
    save is due (`0x00155308`). The meaning of each reason value is inferred from these calls only.
15. Return 1 (stay).

**Timing, as far as the code shows:** game logic steps 1/30 s per frame, and the present waits for vsync. NTSC
vsync is about 60 Hz, so for the game to run at the right speed each frame must take two vertical blanks (30 frames a
second). Where the second blank is waited for is not confirmed; see [Open questions](#open-questions).

### Shutdown

There is none in practice: `main` would return 0 to crt0, which calls the exit function, but the mode stack never
empties (inferred). No subsystem has a teardown call on this path. Confirmed (code) that `main` has no cleanup.

## Coney's implementation

Not started. The roadmap's "Boot the engine" step needs:

- A `GameMode` interface with `Enter`, `Exit`, `Suspend`, `Resume`, `Update(dt) -> bool` and an id, and a mode
  stack with the push/pop semantics above. `Update` should take `dt` from the engine instead of reading a global
  timer, which keeps the engine's test mode (fixed timestep, no real clock) possible.
- The game clock: a fixed step of exactly 1/30 s, with the real-time mode only where the original uses it (front-end
  screens on `Timer`).
- An initialisation sequence in the order above, minus everything PS2-specific (IOP, pads through `libpad`, the
  `host0:` file system, memory pools sized for 32 MB). Coney can keep the order of the game subsystems even where it
  replaces their insides, because later subsystems look up earlier ones through globals.
- The movies and the bugstar check can be skipped at first; the memory-card mode can start as a stub that pops at
  once.

## Open questions

- **Frame rate.** The fixed step is 1/30 s and the present waits for vsync once (inferred); what makes the game run at
  30 frames a second (a second vblank wait inside RenderWare's PS2 driver, an interlaced field mode, or nothing, in
  which case the game would run at 60 frames with a 1/30 s step)? A PCSX2 frame counter or a breakpoint on the present
  would settle it.
- Which mode is which: modes 1, 0xa, 0xc, 0xd, 0xe, 0x10 to 0x14 have no role yet, and mode 8's role (front end or
  level flow) is inferred.
- What camera state 4 is, which lifts the 40 ms clamp in real-time mode.
- Where the pads are read during an in-game frame (only the start-up screen and the file wait loop call the pad
  update `0x001454a8` directly).
- Is `main` in `Core/ChunkSystem.cpp` or in an unnamed `Core/` file?
- The memory system (`Memory/`) needs its own page: pools, clumps, the heap stack used by `main` (slots `+0xb8`
  push and `+0xc0` pop) and the allocator's tag arguments.
