# Boot and the main loop

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). The few runtime claims
(which clock mode a level runs in, the player camera's type) were read in PCSX2 2.9.94 over PINE and say so.

## Purpose

What happens between the console starting the executable and the first frame the player sees, what runs every frame
after that, and how the game would stop. Every other subsystem is created somewhere on this path, so this page is
also the map of *when* each one comes to life. The chunk loader the boot path uses is on its own page,
[Chunk system](chunk-system.md); the disc and file layers are on [File I/O](file-io.md).

In one paragraph: `main` creates every subsystem in one long initialisation function, briefly loads the first level's
header so it can play the three start-up movies, then pushes game modes onto a **game-mode stack** and runs that stack.
The **main loop is the game-mode stack's run loop** (`0x0015e6b8`): every iteration calls the top mode's `Update`,
which runs one whole frame (simulation, rendering, scripts, present, file streaming). There is no other
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
| `0x0033afe0` | `WarriorsMemory_Init` | `Memory/WarriorsMemory.cpp` | the game's own pools ([Memory](memory.md)) | confirmed (code) |
| `0x0015e5e8` | `GameModeStack_Push` | `GameModes/` (file unknown, see below) | push a mode | confirmed (code) |
| `0x0015e650` | `GameModeStack_Pop` | `GameModes/` | pop the top mode | confirmed (code) |
| `0x0015e6b8` | `GameModeStack_RunUntilEmpty` | `GameModes/` | **the main loop** | confirmed (code) |
| `0x0015e718` | `GameModeStack_Top` | `GameModes/` | top mode or null | confirmed (code) |
| `0x0015e748` | `GameModeStack_TopId` | `GameModes/` | id of the top mode, 0 when empty | confirmed (code) |
| `0x00155ed8` | `GameMode::GameMode` | `GameModes/` (file unknown, see below) | base constructor | confirmed (code) |
| `0x0015d160` | `Gm_InGame::Update` (our name) | `GameModes/` (unnamed file) | one in-game frame | confirmed (code) |
| `0x00145a10` | `GameTimer::Update` | `Device/ps2/` (file unknown, see below) | game clock with a fixed-step mode and freezes | confirmed (code) |
| `0x0041f960` | `W_PS2SaveSystem_ReadBugstarFile` | `Warriors/W_PS2SaveSystem.cpp` | the `0x00515024` virtual call | confirmed (code) |

`main` sits between `Core/ChunkSystem.cpp`'s functions and the static-initialiser stub at `0x001449e8`, with no stub
in between, so it was most likely compiled as part of `ChunkSystem.cpp` (inferred; an unnamed `Core/` file without
global constructors would also fit).
Three more units have no path string; static-initialiser stubs and link order place them (all inferred, see
[Source map](source-map.md#for-the-next-steps)):

- **Mode 0xd and the game-mode base.** After `FileIO/StreamManager.cpp`'s stub (`0x00155b10`) come two units of
  `GameModes/`: mode 0xd's (`0x00155b30`-`0x00155ed8`, ended by the stub `0x00155eb8` whose initialiser builds the
  mode 0xd object), then the base class's (`0x00155ed8`-`~0x00156d20`, no global constructors, so no stub), then
  `Gm_Error.cpp`. The files of `GameModes/` link in alphabetical order (`Gm_Error`, `Gm_MemoryCard`,
  `Gm_XboxSaveSystem`, `InitLevel`, `Initialize`), so both names sort before `Gm_Error.cpp`.
- **The game-mode stack** (`0x0015e5e8`-`0x0015e838`) starts right after the profile-manager mode's stub
  (`0x0015e5c8`) and before mode 0x11's `Enter` (`0x0015e8b0`), in the unit that ends with mode 0x11's stub
  `0x0015f180`, or in a constructor-less unit of its own just before it.
- **`GameTimer`** (`0x00145940`-`0x00145fa0`) lies between `DS_PS2Device`'s own virtual methods (`Init`
  `0x00145790`, slot `+0x88` `0x001458b0`) and the pad code that runs to `Device/ps2/DS_PS2Device.cpp`'s anchors, with
  no stub from `0x001449e8` to `DS_PS2Device.cpp`'s stub `0x001485a8`. So it is `Device/ps2/` code (the `Timer` it
  wraps is too, `0x00148f60`), in `DS_PS2Device.cpp` itself or a constructor-less unit before it; no string names
  the file.

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
| `0x0050cdb4` | RenderWare graphics device (0x460 bytes, vtable at `+8` = `0x00538d78`; see [Graphics](graphics.md#device-object)) | `0x00194488` | confirmed (code) |
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
`0x0050c784` (initially -1, empty). No bound check is made on push (the array's size is not known; the next global in
`.bss` is mode 0x11's object at `0x005e66d0`, so at most 12 entries fit, inferred).

The modes, each a static object built by a static constructor (or a constructor function) and identified by its id:

| Id | Object | Vtable | `Enter` | Source file | Role | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `0x005e56b8` (ctor `0x001582a8`) | `0x00538480` | `0x001582e0` | `GameModes/` | **gameplay**: one level, loaded by `InitLevel` in `Enter` ([Front end](frontend.md#mode-flow)) | confirmed (code); role inferred |
| 5 | `0x005e57e0` | `0x00538538` | `0x00159a58` | `GameModes/` | **legal screen**: drawn once on `Enter`, held 5,000 ms ([Graphics](graphics.md#first-screen)) | confirmed (code) at `0x00159c08`, `0x00159ae0` |
| 6 | `0x005e5810` | `0x00538580` | `0x0015baa0` | `Gm_MemoryCard.cpp` | memory card checks and saving | confirmed (code) (anchor) |
| 7 | `0x005e6800` | `0x005388b8` | `0x0015f830` | `Gm_XboxSaveSystem.cpp` | save-system screens | confirmed (code) (anchor) |
| 8 | `0x005e5d90` | `0x005385c8` | `0x0015c688` | `GameModes/` | **level flow**: bottom of the stack in `main`; loads the front end, then pushes mode 1 with the chosen level ([Front end](frontend.md#mode-flow)) | confirmed (code); role inferred |
| 0xa | `0x005e6550` | `0x005386e8` | `0x0015dbb8` | `GameModes/` | | confirmed (code) id |
| 0xb | `0x005e5df8` | `0x00538658` | `0x0015cf70` | `GameModes/` | **mission complete**: pushed by `MissionComplete_Launch` (`0x0015d420`); `Enter` calls the Lua `UnlockAndLoad`, `Update` runs the game world on `GameTimer` until its `+0x24` (the kind) is set, then pops ([Front end](frontend.md#story-start)) | confirmed (code); role inferred |
| 0xc | `0x005e5dc0` | `0x00538610` | `0x0015cae0` | `GameModes/` | | confirmed (code) id |
| 0xd | `0x005e53a0` | `0x00538360` | `0x00155b30` | `GameModes/` (own unit, before the base class's) | | confirmed (code) id |
| 0xe | (ctor `0x00157e48`) | `0x00538438` | `0x00157e88` | `GameModes/` | | confirmed (code) id |
| 0xf | `0x005e5560` (ctor `0x00156d20`) | `0x005383f0` | `0x00156dd8` | `Gm_Error.cpp` | error screen (disc error, controller removed) | confirmed (code) |
| 0x10 | (ctor `0x0015d4f8`) | `0x005386a0` | `0x0015d648` | `GameModes/` | | confirmed (code) id |
| 0x11 | `0x005e66d0` | `0x00538828` | `0x0015e8b0` | `GameModes/` | | confirmed (code) id |
| 0x12 | `0x005e65c0` | `0x00538730` | `0x0015e048` | `GameModes/` | **profile manager**: the front-end menus ([Front end](frontend.md#profile-manager)) | confirmed (code); role inferred |
| 0x13 | `0x005e56f0` | `0x005384c8` | `0x00159538` | `GameModes/` | | confirmed (code) id |
| 0x14 | `0x005e67d0` | `0x00538870` | `0x0015f1a0` | `GameModes/` | | confirmed (code) id |

The modes without a role are for the analysts of the front end and gameplay to name; the boot path only needs 5, 6,
8, 0xb and 0xf.

### Timers

Both timers count the EE's cycle counter (COP0 `Count`, 294.912 MHz) into a 64-bit total, adding the 32-bit
difference since the last read and handling wrap-around (`0x00148fd8`). Confirmed (code). Conversions: milliseconds
= ticks / 294,912 (`0x004dcdd8`); seconds = ticks × 3.390842e-9 (`0x004dcf00`). Confirmed (code).

`GameTimer` (constructor `0x00145940`, vtable `0x00537bc0`, 0x68 bytes from the allocation in
`Game_InitializeSubsystems`) wraps a `Timer` and keeps two clocks: **game time** at `+0x40`, which modes and
everything else read, and an unscaled clock at `+0x30`. Layout, confirmed (code) at `0x00145980` (reset),
`0x00145a10` and the accessors:

| Offset | Size | Meaning |
| --- | --- | --- |
| `+0x00` | 4 | vtable |
| `+0x08` | 0x18 | the embedded `Timer` (real time) |
| `+0x20` | 8 | real-time ticks at the last update, for game time |
| `+0x28` | 8 | real-time ticks at the last update, for the unscaled clock |
| `+0x30` | 8 | unscaled clock, in ticks |
| `+0x38` | 4 | `+0x30` in milliseconds |
| `+0x40` | 8 | **game time**, in ticks |
| `+0x48` | 4 | game time in milliseconds |
| `+0x4c` | 4 | float: the fixed step in seconds, 1/30 (`0x3d088889`) |
| `+0x50` | 4 | float: time scale, 1.0 |
| `+0x54` | 4 | 1 = fixed step, 0 = real time (`0x00145f60(timer, on)`; turning it off resynchronises `+0x20`) |
| `+0x58` | 4 | single-step request: one fixed step even while paused (who sets it is not traced) |
| `+0x5c` | 4 | paused |
| `+0x60` | 4 | freeze end, in real-time milliseconds; 0 = none |
| `+0x64` | 4 | freeze minimum: before it, a button press cannot end the freeze |

Virtual slots: `+0x08` destructor, `+0x10` `Update`, `+0x18` `TogglePause` (`0x00145dc8`), `+0x20` `IsPaused`
(`+0x5c`), `+0x28` `Ticks` (`+0x40`), `+0x30` `Milliseconds` (`+0x48`), `+0x38` seconds (milliseconds × 0.001),
`+0x40` ticks since a given tick count, `+0x48` the same in milliseconds, `+0x50` in seconds. The real-time
milliseconds used for freezes come from the global `Timer`'s slot `+0x30`. Confirmed (code).

`Update` (`0x00145a10`, 952 bytes: the clock, plus the freeze handling below), confirmed (code):

- **Fixed step** (`+0x54 == 1`): unless paused (or when `+0x58` asks for one step, which it then clears), game time
  advances by `+0x4c × +0x50 × 294,912,000` ticks converted to an integer, which is exactly `0x960000` (1/30 s) at
  scale 1.0, and the unscaled clock by `0x960000`.
- **Real time** (`+0x54 == 0`): unless paused, read the real clock; game time advances by the elapsed ticks since
  `+0x20` times the scale, **clamped to `0xb40000` ticks (40 ms) only when player 0's camera exists and is not a
  scene camera** (`0x0011f9b0(0)`; camera slot `+0x1e8` returns the camera's *type*, and 4 is the scene camera);
  with no camera there is no clamp. The unscaled clock advances by the unclamped, unscaled elapsed time; both bases
  are set to now. So during a scripted scene (and the in-engine movies, which create a type-4 camera,
  `0x0042a938`) game time follows the real clock without a cap.
- **Camera types**, confirmed (code) at the camera factory `0x0011e1b0(type, ...)` (`Camera/Cam_ICamera.cpp`) and the
  classes' slot `+0x1e8`: type 4 allocates with the tag `Cam_Scene` and builds a camera whose slot `+0x1e8`
  (`0x004dc5a8`) returns 4; the other cases are 0 (tag `Cam_Fixed`), 1 (`Cam_Locked`), 0x10 (`Cam_3rdPerson`), and 2, 3,
  5, 7, 8, 0xc and 0xd, which use constructor functions without a tag. In a Quick Rumble fight the player camera
  (`0x005d9150` → vtable `0x00535d50`) is type 2, its slot `+0x1e8` (`0x004db698`) returning 2: confirmed (runtime),
  PCSX2 2.9.94, memory read over PINE.
- **Paused**: neither clock moves in either mode, and `TogglePause` resynchronises both bases when it unpauses, so the
  paused time is skipped. Pausing also refreshes the input state of every player who has one (`0x00146078`).
- **Freeze** (`0x00145ea8(timer, ms, minMs)`, used by the GUI's `<FREEZE ms>` tag with `minMs` = 2000,
  [GUI](gui.md)): if not paused, pause and set `+0x60` = now + `ms`, `+0x64` = now + `minMs`. While paused with a
  freeze set, each `Update` unpauses when `+0x60` has passed; otherwise, unless the caption system shows a
  "kind 2" caption (`0x001cae58(0x00619570)`, below),
  it unpauses when any player with a pad releases the button with mask `0x40` after `+0x64` (`0x00144ba8`: pressed
  last frame, not this one), or at once when no player has a pad.
- Finally `+0x48` and `+0x38` are recomputed (ticks / 294,912), paused or not.

Mode 1's `Enter` sets `+0x5c` = 1 (paused) and `+0x60` = 0 ([Level loading](level-loading.md#initlevel)).

**Which clock mode runs when.** Mode 8 switches the clock to the fixed step in its `Update` (`0x0015c858`) and its
`Resume` (`0x0015c6f8`), and to real time in its `Suspend` (`0x0015c780`) and `Exit` (`0x0015c6c8`); mode 1
(gameplay) never switches it. So **a level runs on real time, clamped to 40 ms**, and the front end on the fixed
step. Confirmed (code) at those addresses; confirmed (runtime), PCSX2 2.9.94: in a Quick Rumble fight the mode stack
(`0x005e66a0`, top index `0x0050c784` = 1) holds modes 8 and 1, and `GameTimer` (`0x00b8f980`) has `+0x54` = 0, scale
1.0, not paused. Mode 0xb's `Update` (`0x0015d160`), which switches the fixed step on every frame, was not on the stack
in that fight.

**The caption system** at `0x00619570` (`0x001ca950`-`0x001cb400`) shows the subtitles of a movie (`0x001cad38`,
from `0x0042a938`; during the intro movie it held the name `l1_in_sub`) and other captions. Each caption record
starts with a kind (0-6, larger values become 3); setting a caption (`0x001cb010`) sets `+0x58` to 1 only for kind
2, which is drawn centred, at 1.2 times the text size, in red (`0xff1a1a86`), and `0x001cae58` returns `+0x58`.
Confirmed (code) at `0x001cb010`, `0x001cae58`. So a kind-2 caption keeps a frozen game frozen until its time runs
out: the player cannot skip it with the button. Read at runtime (PCSX2 2.9.94): `+0x58` was 0 in the intro movie and
in a fight. The single-step request `+0x58` of `GameTimer` (a different object) has no writer found.

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
   skips the streamed worlds (`<world>s_sec.wld` and the rest, [Level loading](level-loading.md#worldmanager-loadlevel)),
   so only `level1.lev` is loaded into a `World Level Pool` sized
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
13. Push mode 5 (`0x005e57e0`), the legal screen.
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
| 8 | `0x0040c5e0` (`Wad_Open`, our name) | opens the WAD: reads `WARRIORS.DIR` into a `DVDWadIndex` and opens `WARRIORS.WAD` on the IOP ([File I/O](file-io.md#the-wad-index)) |
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
| 21 | `0x0040d688` | **sector heaps**: `Sector Pool` = the largest free block minus 128 KB, then `Sector Pool 2` = what is left minus 128 KB (at least 4 KB); every level's data lives in the `Sector Pool` ([Level loading](level-loading.md#memory)) |
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
   Pools created, in order: `All System` (a clump over the arena), `System Memory Pool` (a heap over the rest of it,
   registered as `Global Memory` and the default target of every allocation afterwards), `Filter Memory` /
   `Filter Pool` (`0x29000` bytes, RenderWare's scratch pool), `Debug Pool` / `Debug Heap` (1 KB on retail: a second,
   debug-only arena is asked for and is absent) and a free list of 512 registered pools. Confirmed (code) for names
   and sizes; the whole tree, the allocators and the heap stack: [Memory](memory.md).
5. Slot `+0xa0` (`0x001488f8`): **pads** (`scePadInit`, two port opens, eight 0x140-byte pad records starting near
   `0x005de4c0`; the library calls are identified by `libpad`'s position, inferred) and audio device setup (`0x0010f618`).
6. **Renderer**: `0x00194488(0, 0)` creates the RenderWare device (`Renderware`, 0x460 bytes) in 4:3, interlaced
   mode; `0x0017a1e0` starts RenderWare through the device's `Init` ([Graphics](graphics.md#start-up)).
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
PS2 driver shows a new frame at most every second vertical blank (below). The loop ends only when the stack is empty;
modes 8 and 0xb always return 1, so on a retail run it never ends (inferred).

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
6. Characters (`0x00249b98`), resource streaming (`0x0018a980`), render device slot `+0x18` (flush the render
   queue), world update (`0x0040f8a0`), resource manager (`0x00186068`).
7. **Render each viewport** (`0x00156408`, one or two): device begin-viewport (slot `+0x88`), lights, world sectors,
   resources, particles (`0x0017b2e0`), device slot `+0x118`.
8. **Overlays** (`0x00156658`): HUD (`0x001b1688`), subtitles, the front-end layers when active, screen effects with
   `dt` (device slot `+0x128`).
9. **Scripts**: `scriptSystem.Update(dt)` (slot `+0x14`; [Scripts](scripting.md#vtable-slots)).
10. **Present** (device slot `+0x30`, `0x001958b0`): the main camera's show-raster (the flag 1 the device passes
    is dropped; RenderWare gets 0), then clear the "cameras set up this frame" flag (`0x0050b6f8`). See
    [Graphics](graphics.md#frame-rate).
11. **File streaming**: `FileManager_Service(fileManager, 0)`: collect a finished asynchronous read and start the
    next ([File I/O](file-io.md#filemanager)).
12. Push the error mode if needed (`0x0015e7e8`).
13. Debug FPS counter (`0x001569c0`; prints frames per second, free memory and draw distance to a buffer).
14. If the mode was asked to leave (`+0x24` holds a reason, non-zero): pop it; for reason 2 select a level by name
    in mode 8 (`0x0015c7b0`), for reason 3 select the next level; call save-system slot `+0xb0`; pop once more if
    the new top is mode 1; update two per-player records in `W_GameState + 0x480`; push the memory-card mode if a
    save is due (`0x00155308`). The meaning of each reason value is inferred from these calls only.
15. Return 1 (stay).

**Timing:** game logic steps 1/30 s per frame, and the RenderWare PS2 driver's vertical-blank handler shows a new
frame at most every second vertical blank (the limit 2 is set at start-up), so the game runs at 30 frames a second
(29.97 on NTSC) and slows down rather than skipping when a frame takes longer. Confirmed (code); the details are on
[Graphics](graphics.md#frame-rate). This holds for mode 0xb's frame; in a Quick Rumble fight the stack held modes 8
and 1, not 0xb, and the clock ran on real time with the 40 ms clamp ([Timers](#timers), confirmed (runtime)).
Which levels run mode 0xb (answered): none as their play mode. It is the **mission-complete** mode that
`HUDLaunchMissionComplete(kind)` (`MissionComplete_Launch`, `0x0015d420`) pushes over mode 1 at the end of a mission,
and once over mode 8 when a story game starts; the reason in `+0x24` (step 14) is that call's `kind`, and `Enter`
calls the Lua function `UnlockAndLoad` ([Front end](frontend.md#story-start)). Confirmed (code).

### Shutdown

There is none in practice: `main` would return 0 to crt0, which calls the exit function, but the mode stack never
empties (inferred). No subsystem has a teardown call on this path. Confirmed (code) that `main` has no cleanup.

## Coney's implementation

Written from this page:

- `src/gamemodes/game_mode.h` (`GameMode`): `enter`, `exit`, `suspend`, `resume`, `update` and `id`, empty by
  default as in the base class. `update` is handed the frame's time (`FrameTime`) instead of reading a global timer,
  and returns `ModeResult::Stay` or `ModeResult::Leave`. The entered flag stands for the state field at `+0x0c`.
- `src/gamemodes/game_mode_stack.h` (`GameModeStack`): push, pop, top, top id and the run loop with the semantics of
  [The main loop](#the-main-loop), including the original's quirk that a mode which pushes another and then leaves
  pops the one it pushed. Pushing a mode already on the stack is a programmer error. There is no fixed capacity.
- `src/core/game_timer.h` (`GameTimer`): game time in EE ticks, with the fixed step of exactly `0x960000` ticks and a
  real-time mode clamped to 40 ms whose elapsed time the caller measures (the engine never reads a clock itself).
- The run loop advances the `GameTimer` once per step and hands the step to the top mode. The original computes
  `dt` per mode from the timestamp at `+0x00` (`0x00156220`); Coney's step is the same 1/30 s under the fixed step.
- **Update and render are split**, Coney's choice: the original's `Update` simulates, draws and presents one frame;
  Coney's `update()` only simulates one step and a separate `render()` draws and presents. Each real frame the loop
  runs the steps the real time calls for (0 to 4, from `FrameClock`), then renders once, blended between the last two
  steps, so the game keeps its 30 steps a second at any display rate
  ([Graphics](graphics.md#coneys-implementation),
  [Update and render](../guides/conventions.md#update-and-render)). Only the mode that ran the last step renders, as
  only the top mode's `Update` ran in the original; a mode that leaves on a frame's last step is popped after that
  frame's render, so its last picture is shown as the original's was. Test mode is lockstep: one step and one render
  per frame, with no clock.

- `coney` builds the stack in `main` (`src/platform/main.cpp`): it opens the WAD when given `--disc`, creates the
  chunk handler table, then runs the start-up flow: with a disc it loads the UI strings, skips the three movies (no
  video decoder yet), pushes mode 8, asks for the memory-card boot check, pushes mode 6 and then the legal screen
  (mode 5), which runs first ([Front end](frontend.md#coneys-implementation)); without one, an idle mode alone.
  `--load` and `--view-txd` run their own tool modes instead
  ([Building and testing](../guides/building.md#run-coney)). The rest of the initialisation order, level 1's header
  load, the controller check and its error mode, and the bugstar check are not done.
- `FrameTime` also carries the ticks the step advanced (`stepTicks`), so a mode can time itself from the moment it
  was entered, as mode 5 does.

TODO for the analysts, found while implementing:

- Whether `GameTimer` honours its pause in real-time mode, and what the time scale at `+0x50` does there
  (answered): the pause stops both clocks in both modes and the base is resynchronised on unpause, so Coney's
  behaviour is right; in real-time mode the scale multiplies the elapsed time before the 40 ms clamp, and the
  unscaled clock at `+0x30` ignores it ([Timers](#timers)). One difference remains for the implementer: the original
  applies the 40 ms clamp only while player 0's camera exists and is not a scene camera (type 4), so on screens
  without a camera and in scripted scenes real time is unclamped. And a level runs on real time, not on the fixed
  step ([Timers](#timers)); Coney's fixed step is its own choice there (it keeps test mode deterministic), and its
  limit is that slow frames slow the game down where the original catches up by up to 40 ms a frame.
- What `dt` a mode sees on its first update (the value of its timestamp at `+0x00` after a push).
- The `@orig` tags cite the game-mode functions and `GameTimer::Update` with file `(unknown)` (answered as far as the
  executable allows): no string names these files, so `(unknown)` stays; their directories are `GameModes/` and
  `Device/ps2/` ([Original structure](#original-structure)). The 952 bytes `coney-tools progress sizes --fill`
  gives `GameTimer::Update` are right: the function is `0x00145a10`-`0x00145dc8` and also handles freezes and the
  pads that end them ([Timers](#timers)); the tracker now counts them.

## Open questions

- Which mode is which: modes 0xa, 0xc, 0xd, 0xe, 0x10, 0x11, 0x13 and 0x14 have no role yet. The start-up path
  (5, 6, 8, 0x12, 1) is on [Start-up and the front end](frontend.md#mode-flow).
- What camera state 4 is (answered): the camera's type, 4 being the scene camera ([Timers](#timers)).
- Is `main` in `Core/ChunkSystem.cpp` or in an unnamed `Core/` file?
- The memory system's page (answered): [Memory](memory.md).
- What `0x001cae58(0x00619570)` is (answered): "a kind-2 caption is showing" ([Timers](#timers)). Still open: who
  sets the single-step request `GameTimer + 0x58` (no writer found; a debug feature, speculative), and what the
  untagged camera types 2, 3, 5, 7, 8, 0xc and 0xd are (candidates: the files `Cam_Follow.cpp`, `Cam_Mini.cpp`,
  `Cam_Mug.cpp`, `Cam_Power.cpp`; type 2 is the fight camera).
