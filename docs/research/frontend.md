# Start-up and the front end

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). One runtime observation (the
boot sequence with an unformatted card, below) was made in PCSX2 2.9.94 and says so. The disc-side checks (2026-10-04)
read the NTSC-U disc's WAD and are reported as names, counts and layouts only.

## Purpose

What the game does between the legal screen and the moment the player can move the cursor on the main menu: which
game modes run, in which order, what each loads, how long it holds and what input moves it on; how pad input reaches
the menus; and which sounds the front end asks for. Then the menus themselves, screen by screen, down to positions,
colours, cues and fades, and the 3D scene behind them, with a [checklist](#matching) for matching them.

In one paragraph: after the legal screen (mode 5) pops, the memory-card mode (6) runs its boot check and pops; the
mode at the bottom of the stack (8, the **level flow**) then loads the front-end level, **`level100`** (the Coney
Island Wonder Wheel scene), starts the `menu` music and calls the level script's Lua function `Menu.onStart`. That
script calls `ShowProfileManager`, which pushes the **profile manager** mode (0x12). The profile manager is a small
state machine of C++ screens (`GUI/ProfileManagementGUI/PM_*.cpp`); its first screen, `PM_Greet`, waits for START,
and the next, `PM_Mode`, is the main menu. The menus themselves are not data-driven: their layout and flow are in
code, their text comes from a Lua string table, and their sprites from a sprite sheet resource. How the menus draw
is on [GUI](gui.md).

## Original structure

Game modes are described on [Boot](boot.md#game-mode); this page adds the roles of the modes on the front-end path.
Names are ours unless they come from a path or class string.

| Address | Name | File | Role | Evidence |
| --- | --- | --- | --- | --- |
| `0x00159a58` / `0x00159ae0` | mode 5 `Enter` / `Update` | `GameModes/` | legal screen | confirmed (code) |
| `0x00161218` | `RunPreloadScripts` | `GameModes/` | runs `enum_preload.lua`, then `config_preload.lua`, `config_preload2.lua` and `config_preload3.lua` ([Scripts](scripting.md#life-of-the-lua-state)) | confirmed (code) |
| `0x00156148` | `AnyPadHoldsL1OrR1` | `GameModes/` (base file) | latch read by mode 5 | confirmed (code) |
| `0x0015baa0` / `0x0015be00` / `0x0015c2c0` | mode 6 `Enter` / `Update` / `Exit` | `GameModes/Gm_MemoryCard.cpp` | memory-card boot check | confirmed (code) |
| `0x0015c6f8` | mode 8 `Resume` (also called by its `Enter`) | `GameModes/` | first entry loads the front-end level | confirmed (code) |
| `0x0015c4b0` | `LevelFlow_StartFrontEnd` | `GameModes/` | level 0, `menu` music, `Menu.onStart` | confirmed (code) |
| `0x0015c858` | mode 8 `Update` | `GameModes/` | front-end world frame, or start the chosen level | confirmed (code) |
| `0x0015c5f8` | `LevelFlow_FinishFrontEnd` | `GameModes/` | `Menu.onFinish`, unload the level | confirmed (code) |
| `0x0015fe90` | `InitLevel` | `GameModes/InitLevel.cpp` | loads the current level (both the front end and a game level) | confirmed (code) |
| `0x001607b8` | `UnloadLevel` | `GameModes/` | the reverse of `InitLevel` | confirmed (code); role inferred |
| `0x001582e0` | mode 1 `Enter` | `GameModes/` | gameplay: loads the chosen level through `InitLevel` | confirmed (code) |
| `0x001552b0` | `ShowProfileManager` | `GameModes/` | stores two Lua callbacks, pushes mode 0x12 | confirmed (code) |
| `0x0015e048` / `0x0015e238` / `0x0015e130` | mode 0x12 `Enter` / `Update` / `Exit` | `GameModes/` | profile manager (the menus) | confirmed (code) |
| `0x002040f0` | `PM_Controller::PM_Controller` | `GUI/ProfileManagementGUI/PM_Controller.cpp` | builds the 14 screens and their transitions | confirmed (code) |
| `0x00204a78` / `0x00204ba0` / `0x00204c20` | `PM_Controller` start / update / stop | same | | confirmed (code) |
| `0x002079a0`, `0x00207e28`, `0x00208288` | `PM_Greet` init / update / render | `GUI/ProfileManagementGUI/PM_Greet.cpp` | the "press START" screen | confirmed (code) |
| `0x00209da8`, `0x0020a4b8` | `PM_Mode` init / update | `GUI/ProfileManagementGUI/` | the main menu | confirmed (code); file inferred |
| `0x001454a8` | `Pads_Update` | `Device/ps2/` region | reads both pads into the pad records | confirmed (code) |
| `0x00144fb0` | `Pad_Update(index, cameraMatrix)` | same | one pad record per frame: sample, repeat counters, diagonals, Lua handlers | confirmed (code) |
| `0x001e95c0` | `MenuInput_Dispatch` | `GUI/` (unnamed file before `RumbleModeGUI/`) | turns pad and stick input into menu commands | confirmed (code) |
| `0x0037d420` | `RegisterScriptFunctions` | `Scripting/` | registers about 960 Lua globals (C functions) | confirmed (code) |

`0x002040f0` and the comparator `0x00184890` ([GUI](gui.md#draw-order)) were not defined as functions in our Ghidra
project; they were created while writing this page.

## Data

### Game modes on the front-end path

Completes the table on [Boot](boot.md#game-mode). Evidence: confirmed (code) at the cited functions; the role names
are inferred from what the code does.

| Id | Object | Role | What leaves it |
| --- | --- | --- | --- |
| 5 | `0x005e57e0` | legal screen | 5,000 ms of real time |
| 6 | `0x005e5810` | memory-card check (here with the boot flag) | the save system and its message box report done |
| 8 | `0x005e5d90` | **level flow**: front end, then the choice of level | never (bottom of the stack) |
| 0x12 | `0x005e65c0` | **profile manager**: the front-end menus | the screen flow empties (`PM_Controller` returns done) |
| 1 | `0x005e56b8` (static constructor `0x00159490`) | **gameplay**: one loaded level | the level is left |

### Mode 5 fields

Derived fields after the 0x20-byte base, confirmed (code) at `0x00159a58`:

| Offset | Meaning |
| --- | --- |
| `+0x20` | start time, real-time milliseconds (`Timer` slot `+0x30`) |
| `+0x24` | minimum hold, 5,000 ms |
| `+0x28` | maximum hold, 5,000 ms |

### Mode 8 fields

| Offset | Meaning | Evidence |
| --- | --- | --- |
| `+0x20` | level chosen to start next (index into the level table), -1 = none | confirmed (code) at `0x0015c858`, `0x0015c7b0` |
| `+0x24` | 1 while the front-end level is loaded | confirmed (code) at `0x0015c4b0`, `0x0015c5f8` |
| `+0x28` | "load the front end on the next `Resume`": set to 1 by `Enter` and again at the end of **every** `Resume` (`0x0015c6f8`); cleared by mode 6's `Exit` and by mode 0xb's `Update` when mode 8 is the mode directly below them | confirmed (code) at `0x0015c688`, `0x0015c6f8`, `0x0015c2c0`, `0x0015d160` |

### The level table

`W_GameState + 0x14d4` holds 128 level records of `0x84` bytes (cleared by `0x0041f070`), the count at `+0x4200`
(`W_GameState + 0x56d4`) and the current index at `W_GameState + 0x56dc` (16 bits). The records are filled from Lua:
the binding `CfgLevelName` (`0x0036b220`) passes 18 arguments to `0x0041f118`, which writes record `id`: `+0x00` id,
`+0x04`, `+0x08`, `+0x0c` (byte), `+0x0d` flags (bit 0, 1, 2 from three booleans), `+0x10`, `+0x14` the level name
(15 characters), `+0x24` a second name (20), `+0x39` a third (15, the streamed world's name, see
[Level loading](level-loading.md#the-level-record)), `+0x49` a fourth (31), `+0x6c`-`+0x80` five more
values. Confirmed (code) for the layout; what the fields mean beyond the name is open.

**Disc check (corroboration):** `config_preload3.lua` calls `CfgLevelName`, and its `levelNames` list begins with
`level100`; so level index 0, which mode 8 loads first, is `level100` (inferred: that the list order gives the index is
not read). The front-end files exist: `level100.lev` (31,872 bytes), `level100.lua` (10,007 bytes),
`level100main.lua` and `level100_1.pak`.

### Pad record

The game keeps 8 pad records of `0x50` bytes at `0x005dd810` (record `i` at `0x005dd810 + i × 0x50`); only records 0
and 4 (port 1 and port 2, first multitap slot) are updated. The device keeps 8 raw records of `0x140` bytes at
`0x005de3c0` with the `libpad` state machine. Confirmed (code) at `0x001454a8`, `0x00144fb0`, `0x001498a8`.

| Offset | Size | Meaning |
| --- | --- | --- |
| `+0x00`, `+0x04` | 4 × 2 | left stick turned by the camera (in game) |
| `+0x08`, `+0x0c` | 4 × 2 | left stick x, y in [-1, 1], y up (from `libpad` bytes 6, 7) |
| `+0x10`, `+0x14` | 4 × 2 | right stick x, y (bytes 4, 5) |
| `+0x18`-`+0x1b` | 1 × 4 | the raw stick bytes |
| `+0x1c` | 2 × 8 | ring of the last 8 button words |
| `+0x2c` | 4 | ring index of the current sample (0-7) |
| `+0x30`-`+0x33` | 1 × 4 | hold counters for up, right, down, left |
| `+0x34`-`+0x3f` | 12 | pressure bytes (right, left, up, down, triangle, circle, cross, square, L1, R1, L2, R2) |
| `+0x42` | 2 | the player the pad belongs to (-1 none) |
| `+0x48` | 4 | time of the last real sample, ms |
| `+0x4c` | 4 | connected |

**Button word.** The game stores `~((byte2 << 8) | byte3)` of the `libpad` data (`0x00149e58`), so a set bit is a
held button. With the standard `libpad` byte layout (inferred from the SDK's documentation, and consistent with every
use below) the bits are:

| Bit | Button | Bit | Button |
| --- | --- | --- | --- |
| `0x0001` | L2 | `0x0100` | SELECT |
| `0x0002` | R2 | `0x0200` | L3 |
| `0x0004` | L1 | `0x0400` | R3 |
| `0x0008` | R1 | `0x0800` | START |
| `0x0010` | triangle | `0x1000` | up |
| `0x0020` | circle | `0x2000` | right |
| `0x0040` | cross | `0x4000` | down |
| `0x0080` | square | `0x8000` | left |

The pressure bytes confirm the d-pad bits: when two directions are held, the code keeps the one with the highest
pressure, and pressure byte 0 maps to `0x2000`, 1 to `0x8000`, 2 to `0x1000` and 3 to `0x4000` (`0x00144fb0`).

**Sticks.** A raw stick byte `r` becomes a float through the table at `0x0050b8d0`: 0 for `r` in 95..160 (dead
zone), `(r - 95) / 95` below it and `(r - 160) / 95` above it, so the range is exactly [-1, 1]. Confirmed (data in the
executable). The y values are negated so that up is positive.

**Diagonals and the stick's shape.** The dead zone and the scale are applied to each axis on its own, and the
in-game stick length is `min(1, sqrt(x² + y²))` ([Characters](characters.md): a run needs more than 0.95, `0x005102e8`).
So a full push only runs along a diagonal if both bytes reach near 0 or 255 there. The DualShock 2's sticks do: their
output is close to a square, both axes at their extremes at a full diagonal (inferred, from the game's design and from
PCSX2, whose default analog sensitivity of 1.33 scales a modern pad up to match; not measured on hardware). A modern
gamepad reports a circle: about 0.71 of full travel on each axis at a full diagonal, raw bytes about 37 and 218, which
the table turns into 0.61 each and a length of about 0.86, so the player walks. W A S D are unaffected (bytes 0 and
255). **Coney's implementation:** the SDL layer stretches each gamepad stick from the circle onto the square before
making the bytes (`stickBytesFromAxes`, `src/platform/sdl_input.cpp`: each point scaled by its length over its larger
axis), so a full diagonal runs as on a PS2; straight pushes and the table above are unchanged. Open: measuring a real
DualShock 2's diagonal bytes (through PCSX2 with a DualShock 2 adapter, or on hardware).

### Pad queries

Functions over a pad record, all confirmed (code); `cur` is the current button word, `prev(n)` the word `n` samples
back:

| Address | Returns |
| --- | --- |
| `0x00144a08(pad, n)` | the word `n` samples back (`n` clamped to 7) |
| `0x00144b88(pad, mask)` | held now: `cur & mask != 0` |
| `0x00144a80(pad, 0)` | **pressed** this sample: `cur & ~prev(1)` |
| `0x00144a30(pad, 0)` | **released** this sample: `~cur & prev(1)` |
| `0x00144ad0(pad, 0)` | pressed, plus each d-pad direction whose hold counter is 15 (**auto-repeat**) |
| `0x00144bf0` / `0x00144ba8` | pressed / released for a mask |
| `0x00144ce0`, `0x00144d60`, `0x00144e50` | taps and holds over the 8-sample history (in-game use) |
| `0x00144ef8`, `0x00144f48` | button combinations (all held, one newly pressed) |

Until the research database names them, Coney's `@orig` tags call the first seven `Pad_ButtonsBack`, `Pad_Held`,
`Pad_Pressed`, `Pad_Released`, `Pad_PressedRepeat`, `Pad_PressedMask` and `Pad_ReleasedMask` (our names).

## Behaviour

### From the movies to the main menu {#mode-flow}

`main` pushes modes 8, 6 and 5 in that order and runs the stack ([Boot](boot.md#main)). A push does not enter a mode,
so modes 8 and 6 have not been entered when mode 5 runs first.

1. **Mode 5, the legal screen** (5,000 ms). See [Graphics](graphics.md#first-screen) for the drawing. `Enter` also runs
   the preload scripts through the script system (`0x00161218`, slot `+0x44` twice: the list `enum_preload.lua`,
   then the list `config_preload.lua`, `config_preload2.lua`, `config_preload3.lua`; the slot `+0xdc(1)` /
   `+0xdc(0)` calls around them do nothing in this build, [Scripts](scripting.md#vtable-slots)). `Update` (`0x00159ae0`)
   switches `GameTimer` to
   the fixed step, ticks, calls `Pads_Update` and stays while fewer than `+0x28` = 5,000 ms of real time have passed.
   **No button skips it**: the code checks for a pressed button only once the time is already up, and a latch set by
   holding L1 or R1 (`0x00156148`, bits `0x000c`) only copies `+0x24` into `+0x28`, which are equal. Confirmed (code);
   that these are leftovers of a build where the times differed is speculative. `Exit` (`0x00159ab8`) switches
   `GameTimer` back to real time.
2. **Mode 6, the memory-card boot check.** Mode 6 is entered next (it was never entered, so it gets no `Resume`).
   With the boot flag set (`0x0015a270(1)` in `main`), `Enter` (`0x0015baa0`) creates a 512-byte work buffer in the
   `Level Dynamic & LUA Pool` heap, starts the save system's card scan (save-system slot `+0x3c`) and opens a message
   box (`0x001c6b78` on the box at `0x005e5840`). Then either a two-choice dialog (`0x0015a370`: global strings
   `0xa8`, `0xba`, `0xbb`) when the scan flag `0x0050c6fc` is set, or a timed message (`0x0015a328`: global string
   `0xb5` for 3,000 ms). Each frame `Update` (`0x0015be00`) services the save system, runs the box's callbacks
   (when the save system reports the card not usable, slot `+0x34` returning 0 while slot `+0x24` reports it idle,
   the next step is the card dialog `0x0015b918`, below),
   reads the pads, checks for a removed controller, clears to black and draws the HUD and the box. It leaves when the
   save system and the box are done. `Exit` (`0x0015c2c0`) frees the buffer, sets the boot flag to 2 and, because the
   mode below is 8, clears mode 8's `+0x28`. Confirmed (code) for the calls.

   **The card dialog** (`0x0015b918`; the box and every dialog's layout: [The message box](#message-box)), confirmed
   (code); the texts are the English strings of
   `config_strings_en.lua`: a message box (`0x001c7128` on the box at `0x005e5840`) with a message and two choices.
   The message is the one stored at `0x0050c730` if any, else by the save system's state words (not identified;
   `0x005e5d80`, `0x0050c700`): `0xb7` "Autosave failed! Please check the memory card ... and try again.", `0xb8`
   "Load failed! ..." or `0xb9` "Delete failed! ...". The first choice is `0xc0` "Continue without saving" (or `0xc1`
   "Start Game", `0xbc` "OK", `0xbe` "Cancel" in the other states) and sets the next step to `0x0015b8c0`, which
   lets the mode finish; the second is `0xbd` "Retry": the "Checking memory card ..." message (`0xb5`) for 3,000 ms
   (`0x0015a328`), then a new card scan (`0x0015a2b0`). Which message a boot with **no card** shows is not
   identified (the state words); a runtime look with the card removed would tell.

   **At runtime with an unformatted card** in slot 1 (PCSX2 2.9.94, a blank card file, no button pressed; one
   screenshot a second): the legal screen for 4 to 5 seconds, then a white message on black, "Checking memory card
   ... in MEMORY CARD slot 1. Do not remove ...", for about 4 seconds, then a notice over the legal screen's
   picture (without its text) that the game autosaves, then the picture alone as the title screen loads. **No dialog
   asked for an answer** before the title screen; the profile manager later offered `CREATE NEW PROFILE` and
   `RELOAD PROFILES`. Confirmed (runtime) for that card state only.
3. **Mode 8 enters.** `Enter` (`0x0015c688`) sets `+0x28` = 1, takes `GameTimer` and calls its own `Resume`
   (`0x0015c6f8`), which turns off the flip-without-vsync limit (`0x00159468(1)`), switches to the fixed step and,
   because `+0x28` is set and no level is chosen, calls `LevelFlow_StartFrontEnd` (`0x0015c4b0`):
   - waits, servicing the save system, until it is idle (save-system slot `+0x24` returns 1);
   - selects level index 0 (`0x0041ce88`), which is `level100` ([the level table](#the-level-table));
   - **`InitLevel`** (`0x0015fe90`), below;
   - plays the music track **`menu`** (`0x0010fa50`);
   - looks up the Lua function **`Menu.onStart`** (script slot `+0x4c`) and calls it (slot `+0x8c`, no arguments);
   - sets the background colour to black (device slot `+0x48`, `0x005fd260`) and `+0x24` = 1.
4. **`Menu.onStart`** (in `level100.lua`, Lua 4.0 bytecode on the disc) starts the Wonder Wheel cinematic scene and
   calls **`ShowProfileManager("Menu.fadeToRMI", "Menu.startGame")`** (inferred from the order of the script's
   constants: the names of the calls and arguments are certain, their exact order within the function is not). The
   binding (`0x0036eef8` → `0x001552b0`) stores references to the two Lua functions (`0x005e6690`, `0x005e6694`) and,
   unless mode 0x12 is already on top, pushes it.
5. **Mode 0x12, the profile manager.** `Enter` (`0x0015e048`) takes `GameTimer`, plays `menu` if it is not already
   playing and starts the `PM_Controller` (`0x00204a78`) at `mode + 0x20` with the first callback; the controller
   enters its first screen, `PM_Greet` ([below](#profile-manager)). `Update` (`0x0015e238`) runs one frame of the
   world (cameras, resources, world update; the simulation only when `0x005e536c` is 1), the HUD, then the
   controller (`0x00204ba0`), the overlays, the scripts and `Present`, like the [in-game frame](boot.md#one-frame).
   It stays until the controller is done.
6. **`PM_Greet`** waits for START. **`PM_Mode`**, the screen it leads to, is the **main menu**; once it is on screen
   the menu is interactive.

When the player starts a game, the flow continues: the profile manager's `Exit` (`0x0015e130`) calls the second Lua
callback (`Menu.startGame`) when the controller finished normally and the flag read by `0x001fe208` (in the
`RumbleModeGUI/` files; probably "Rumble mode chosen", inferred) is clear; that script stops
the scene and the music and calls `MenuLoadLevel(name)` (`0x0036df48` → `0x00160d78`), which sets
`W_GameState + 0x14c` = 3 and stores the level's index in mode 8's `+0x20` (`0x0015c7b0`, a name search through the
level table). Mode 8's next `Update` then calls `Menu.onFinish` and `UnloadLevel(0)` (`0x0015c5f8`), selects the
level and pushes **mode 1** (gameplay), whose `Enter` loads it with `InitLevel`. Confirmed (code) for the C++ side.

### Starting a story game {#story-start}

What `Menu.startGame` leads to on a new profile, from the script side ([Scripts](scripting.md#run-next-mission)) to
the first frame of mode 1 ([Level loading](level-loading.md#mode-1)):

0. **The menus.** STORY (code 0) in `PM_Mode` leads to `PM_Profile` (or `PM_NumPlayers` with two pads); a new
   profile goes `PM_Create` → `PM_Difficulty` → `PM_Light` → `PM_Subtitles`, a saved one `PM_Load`
   ([the screens](#pm-screens)). `PM_Subtitles`' accept (or `PM_Load`'s) sets the controller's done flag
   `0x0050f5b0`; mode 0x12's `Update` then fades out over 1.0 s ([Fades](#fades)), returns 0 and the loop pops it;
   its `Exit` (`0x0015e130`) asks the save system to create the new profile (when `0x0050f598` is set), stops the
   controller, calls `Menu.startGame` (the second `ShowProfileManager` callback, `0x005e6694`) when Rumble mode was
   not chosen, and applies the 16:9 choice. Confirmed (code). With an unformatted card the profile's save then asks
   to format the card (string `0xac`, choices `0xba` Yes and `0xc0` continue without saving, the second selected;
   `0x0015af20`), over black, before the level loads: confirmed (runtime). What each screen writes: [Saving](save.md#story-screens).
1. `Menu.startGame` stops the music and the Wonder Wheel scene and calls `runNextMission(1)` (`global.lua`), which
   finds the last level completed (none on a new profile), looks up the next mission, **`level99` with checkpoint 1**,
   and calls `SetCheckPoint(1)` (`0x0037b760`: `W_GameState + 0x33a` = 1, the checkpoint and section),
   `SoundStopMusicTrack()` and `MenuLoadLevel("level99")`, then `HUDLaunchMissionComplete(4)`. Inferred from the
   disassembly of `global.lua`; the bindings' effects confirmed (code).
2. `MenuLoadLevel` (`0x00160d78`) sets `W_GameState + 0x14c` = 3 and mode 8's `+0x20` to the index of `level99`
   (record 1, a name search `0x0015c7b0`). **`HUDLaunchMissionComplete(4)`** (`MissionComplete_Launch`, `0x0015d420`)
   finds mode 8 on top (the pop has already taken 0x12 off before calling `Exit`) and so **pushes mode 0xb**, the
   mission-complete mode, and stores 4 in its `+0x24` (`0x005e5e1c`). Confirmed (code).
3. **Mode 0xb, one frame.** The loop enters it: `Enter` (`0x0015cf70`) notes the id of the mode below it (`+0x28`),
   takes `GameTimer`, **sets the kind `+0x24` back to 0** (so the 4 stored by the launch is dropped), puts every live
   human into a still state (inferred; there is none at the front end) and calls the Lua function `UnlockAndLoad`,
   which runs
   `MissionCompleteUnlocks()` and
   `runNextMission(1)` again (the same `SetCheckPoint(1)` and `MenuLoadLevel("level99")`; mode 0xb is on top now, so
   no second push, but `MissionComplete_Launch` stores the kind 4 all the same). Its `Update` (`0x0015d160`) runs one
   world frame (task manager, cameras, `WorldManager_Update`, the resource manager, the passes, the scripts,
   `Present`), sees `+0x24` ≠ 0, clears mode 8's `+0x28` because mode 8 is the mode below, and pops itself; 4 is none
   of the kinds it acts on (1: checkpoint 1; 2: reload the current level; 3: the next record), so it only services the
   save system, pops mode 1 as well **when mode 1 is now on top** (the end of a mission: mode 1's `Exit` unloads the
   level), banks both players' money (`0x0041e420` / `0x0041e398`, [Saving](save.md#record)) and asks for an
   **autosave** (`0x00155308`: pushes mode 6 when the save system is on and the level index is not 0 or `0x00204008`
   says so). Confirmed (code) for the C++ side; the Lua side inferred.
4. Mode 8 is on top again (`Resume`; it does not reload the front end, since `+0x28` is 0, and sets it to 1 again).
   Its next `Update`
   (`0x0015c858`) sees `+0x20` ≥ 0, calls `LevelFlow_FinishFrontEnd` (because `+0x24` is 1: `Menu.onFinish`,
   `UnloadLevel(0)`), selects the level (`0x0041ce88`), sets `+0x20` = -1 and pushes **mode 1**. Confirmed (code).
5. Mode 1's `Enter` runs `InitLevel` for `level99`, checkpoint 1: the loading screen, `global.lua` and `level99.lua`
   (which create Rembrandt, Ash and the follow camera and ask for `level99_combat.lua`), the object and dependency
   lists, the preload around the camera, the music, the intro movie `L99_IN` (record flag `0x02`, section 1), then the
   start callback `StartAmbient`, which starts the in-engine intro scene. The order, step by step, is on
   [Level loading](level-loading.md#story-into-level99).

**Level record 1**, read from `W_GameState + 0x14d4 + 0x84` in PCSX2 2.9.94 with the `level99` level loaded
(`W_GameState` = `0x01fd8400`, from the pointer at `0x0051489c`; confirmed (runtime)):

| Offset | Value |
| --- | --- |
| `+0x00` | 1 (the index) |
| `+0x04` | 99 (the level number: `L99_IN`) |
| `+0x08` | 3 sections |
| `+0x0c` | 1 |
| `+0x0d` | `0x02` (intro movie) |
| `+0x10` | 1 |
| `+0x14` | `level99` |
| `+0x24` | `1:Coney` (the mission's number and place) |
| `+0x39` | `level99` (the world) |
| `+0x49` | `New Blood` (the mission's title) |

**At runtime** (PCSX2 2.9.94, a new profile, screenshots): after the new game is chosen the screen shows a loading
picture titled "1 Coney" / "New Blood" with a progress bar, later a letterboxed in-engine scene, and then control of
Rembrandt with the first tutorial text. Confirmed (runtime) for that order; the movie between them was not watched
for (the disc has `PSS/L99_IN.BIK`, corroboration). That `+0x24` and `+0x49` are the loading screen's two lines is
inferred from the matching text.

### From QUICK RUMBLE to an arena fight {#quick-rumble}

The chain from the main menu's **QUICK RUMBLE** to a fight (the menu's screens and values at runtime are in [The Rumble
set-up](#rumble-setup)). Confirmed (code) for the C++ steps; the Lua steps inferred from the disassembly of
`level100.lua`, `level102.lua` (the Fight Pen; every arena `level101`-`level137` has the same functions) and
`brawl.lua`.

1. **Main menu.** QUICK RUMBLE (code 1, [Input](#input)) calls the profile manager's first callback,
   `Menu.fadeToRMI`: fade out over 0.7 s and `ScheduleFunc("Menu.launchRMI", 500)`.
2. **`Menu.launchRMI`** loops the menu music and calls `ShowRumbleModeInterface("Menu.cancelRumbleMode",
   "Menu.startRumbleMode", 1)`. `RumbleMenu_Show` (`0x00155228`) keeps the two names (`0x005e67c0` cancel,
   `0x005e67c4` start; `0x0015e838`), stores "opened from the front end" (1, at `0x0063ef64`) and pushes **mode 0x11**,
   the Rumble set-up menu, unless it is on top.
3. **Mode 0x11** (`Enter` `0x0015e8b0`, `Exit` `0x0015ea40`) runs the Rumble menu screens (`RumbleModeGUI/`). They write
   the set-up the arena will read: 23 16-bit values from `0x0063eec0` (game type, gangs, options; the Game Mode screen's
   input handler, `0x001f8d80`, fills several). Backing out sets "cancelled" (`0x0050f4dc`); the Choose Area screen's
   launch (`0x001ebb90`, [the screens' code](#rumble-data)) sets "started" (`0x0050f4e0`) and the chosen arena's **level
   number** (`0x0050f4e8`, the `+0x04` of its level record). When the menu pops, `Exit` calls `Menu.cancelRumbleMode()`
   if cancelled, else, when started, `Menu.startRumbleMode(levelNumber)`; it also asks for an autosave when `0x0063f1d0`
   is set (`0x00155308`).
4. **`Menu.startRumbleMode(n)`**: `SetCheckPoint(1)`, stop the music and the menu scene, `MenuLoadLevel("level" ..
   n)`: state 3 and mode 8's `+0x20` = the arena's record ([Starting a story game](#story-start), step 2).
5. **Mode 8** is on top again: its `Resume` does not reload the front end (a level is chosen); its `Update` finishes
   the front end (`Menu.onFinish`, `UnloadLevel(0)`) and pushes **mode 1** for the arena.
6. **`InitLevel`** for `level<n>` ([Level loading](level-loading.md#initlevel)): `global.lua`, then `level<n>.lua`,
   whose `Main` calls **`ConfigRumble`**: `GetRumbleModeData` copies the 23 values (`0x001f26e0`) into `RM_LuaData`,
   `ParseLuaData` spreads them over `Rumble` (`gameMode`, `gameType`, `gangSize`, the gangs and their packs, ...), the
   mode's flags come from `doFile("level<n>_<mode>_init")` (which creates `fP1`, `fP2`, [World flags](flags.md)) and
   its rules from `doFile("<mode>")` (`brawl.lua` ...); a stand-in human and camera are made at `fP1[1]`
   (`AddDummyPlayer`, `AddDummyCamera`); the gangs' character packs are queued (`QueueFileToPrecache`); and, when
   gangs were chosen, `SetStartGameCallback("DoRules")` (`ShowRules1` without gangs, `MakeRumblePak` in one debug
   case). The object list, dependencies and the preload follow.
7. **The players are placed last**: `InitLevel`'s step 13 calls the start callback, **`DoRules`**, which calls the
   mode's **`StartRumble`**: for a brawl `AddBrawlGang1` → the arena's `AddRumbleGang1`, which creates player 1 with
   `HuCreate("P11", Rumble.gang1[1], FlagPos(fP1[1]), 270, nil, 1, gang, true)` and teleports it onto `fP1[1]` with
   the flag's heading (and gang 2 at `fP2`); then `DoRules` makes the gangs enemies, hides the HUD and starts the
   intro and countdown (`ShowRumbleModeIntro("FinishCountdown", ...)`). The fight runs in mode 1.

What Coney needs for QUICK RUMBLE to reach a fight: mode 0x11 (or a stand-in that fills the 23 set-up values and
calls `Menu.startRumbleMode` with an arena's level number), `GetRumbleModeData`, the flag bindings, the start
callback, and `HuCreate` with the chosen character types. The same menu opens in game from the hub with
`fromFrontEnd` 0.

### The Rumble set-up {#rumble-setup}

**The screens** (confirmed (runtime), PCSX2 from a fresh boot with no save, every screen's default accepted with
cross): QUICK RUMBLE → **Game Mode** (two entries: "1 ON 1", 1P or VS, and "WAR PARTY", five to a side, 1P, COOP or
VS) → **Game Type** ("1 Player : Vs." against the computer, or two players) → **Choose Gangs** (up / down picks the
gang, left / right the warchief; default player 1 **BASEBALL FURIES** against the computer's **ORPHANS**) →
**Choose Area** (one arena offered, the **Fight Pen**) → a "BASEBALL FURIES vs ORPHANS" title, and the fight in
`level102`. The other modes are presumably unlocked by the story (inferred: the list held two entries without a
save).

**The 23 values** at `0x0063eec0` (the C++ index; `ParseLuaData` reads them as `RM_LuaData[index + 1]`), who writes
them, and the default 1 ON 1 set-up read at runtime once the gangs were confirmed:

| Index | Lua | Written by | Meaning | Default |
| --- | --- | --- | --- | --- |
| 0 | `gameMode` | Game Type screen (`0x001fd1c8`) | players: 3 one player against the computer, 2 co-op, 1 versus (both two players, needing pad 2); 255 no menu choice, 254 a special path (`PakCleanup`) | 3 |
| 1 | `gameType` | mode list (`0x001f8d80`), entry `+0x0c` | the mode: an `RM_*` number (`RM_Brawl1` 12 "1 ON 1", `RM_Brawl5` 14, `RM_Brawl` 1, `RM_Koth` 2, ...); `RumbleInfo[gameType]` names the rules file (`brawl` for 1, 12 and 14) | 12 |
| 2 | `gangSize` | mode list, entry `+0x1c` | fighters per side; 0 takes the "no gangs" path | 1 |
| 3 | `gang1Pak` − 1 | gang screen (`0x001ef7c0`) | side 1's gang: its record's `[0]` − 1 (255 for the preset gangs) | 4 |
| 4 | `gang2Pak` − 1 | gang screen | side 2's gang, the same way | 2 |
| 5-13 | `gang1[1..9]` | gang screen, record shorts `[4 + 2i]` | side 1's nine character types (`HuCreate`'s type) | 91, 94, 91, 92, 93, 94, 91, 92, 93 |
| 14-22 | `gang2[1..9]` | gang screen, record shorts `[0x16 + 2i]` | side 2's nine character types | 225, 226, 224, 225, 226, 227, 228, 225, 226 |

Confirmed (code) for the writers and the Lua names (`0x001ef7c0`, `0x001f8d80`, `0x001fd1c8`, `level102.lua`'s
`ParseLuaData`), confirmed (runtime) for the defaults. The mode list also copies the entry's name (`+0x08`) with a
`":"` prefix to `0x0063ef30` (`":1 ON 1"`), its `+0x10` / `+0x14` / `+0x18` to `0x0063ef6c` / `70` / `74`, and, when
the entry's `+0x20` is not 0, fills all nine members of both sides with its `+0x20` / `+0x22` and sets `0x0063ef78`
(preset gangs; the Game Type screen then writes 255 to indices 3 and 4). The arena confirm wrote "started" 1 and
level number **102**.

**`GetRumbleModeGangName(side)`** (`0x001f26a8`) returns a **string**: the gang's display name, side 1 from
`0x0063eef0` (`0x001fe048`), any other side from `0x0063ef10`. The gang screen fills them (`0x001fe070`,
`0x001fe0d8`: `strncpy` of at most 32 bytes from the chosen gang record's name pointer, its shorts `[2..3]`). Runtime:
`"BASEBALL FURIES"` and `"ORPHANS"`, empty until the gangs are confirmed. `ParseLuaData` asks for them only when
`gameMode` is not 255 (`Rumble.gang1Name`, `gang2Name`). Confirmed (code), confirmed (runtime).

### Where the Rumble data lives {#rumble-data}

The menu's lists are not tables in the executable. Each screen runs a Lua chunk from the disc when it opens, and the
chunk's `CfgRumble*` calls build the list ([Config bindings](../references/bindings/config.md)). Confirmed (code) for
the C++ side; the chunks' calls are read from the disc (inferred from their disassembly, as for the other scripts).

| Screen (source file) | Init | Chunk | Calls | List | Record |
| --- | --- | --- | --- | --- | --- |
| Game Mode (`RM_GameMode.cpp`) | `0x001f85c0` | `rumble_data.lua` | 9 `CfgRumbleGame` | `0x0063ee84` | 0x1b4 bytes |
| Game Type (`RM_NumPlayers.cpp`) | `0x001fc5b0` | none | three fixed entries | | |
| Choose Gangs (`RM_ChooseGangs.cpp`) | `0x001ecae0` | `rumble_gang.lua` | 46 `CfgRumbleGang` | `0x0063ee4c` | 0x54 bytes |
| Choose Area (`RM_ChooseArea.cpp`) | `0x001eb0c8` | `rumble_arena.lua` | 29 `CfgRumbleArena` | `0x0063ee40` | |

Each chunk sets a per-language string table, calls `GetLanguage`, then calls its own `CfgRumble...Data` function.
The Game Mode screen runs its chunk through the script system's slot `+0x44` and keeps the list at screen `+0x8c`.
The arenas are levels 101-134.

**A mode** (`0x001f8110`) is added only when the unlockables manager says it is unlocked (`0x00424130(0x006fe998,
1, mode)`). Its record:

| Offset | Meaning |
| --- | --- |
| `+0x00`, `+0x04` | 0 |
| `+0x08` | the title |
| `+0x0c` | the mode id (`RM_*`), copied to `gameType` |
| `+0x10` / `+0x14` / `+0x18` | bools: offer one player, co-op, versus (copied to `0x0063ef6c` / `70` / `74`) |
| `+0x1c` | fighters per side, copied to `gangSize` |
| `+0x20` / `+0x22` | u16 preset character types for sides 1 and 2 (0 = the player chooses gangs) |
| `+0x24` | the text widget (the title over the description) |

The nine entries on the disc, in list order (the argument the binding calls `unused` always equals the size):

| Mode | Id | 1P, co-op, versus | Size | Presets |
| --- | --- | --- | --- | --- |
| `RM_Brawl1` "1 ON 1" | 12 | yes, no, yes | 1 | none |
| `RM_Brawl5` "WAR PARTY" | 14 | yes, yes, yes | 5 | none |
| `RM_Brawl` | 1 | yes, yes, yes | 9 | none |
| `RM_Surv` | 9 | yes, no, yes | 1 | none |
| `RM_Mercy` | 23 | no, no, yes | 1 | none |
| `RM_TagBt` | 19 | yes, no, yes | 1 | none |
| `RM_Royal` | 3 | yes, yes, yes | 9 | none |
| `RM_Wchair` | 24 | yes, no, yes | 1 | 458, 459 |
| `RM_Koth` | 2 | yes, yes, yes | 3 | none |

`global.lua` defines more `RM_*` ids that no entry uses (`RM_Hifi` 11, `RM_Food` 7, `RM_Run` 10, `RM_Caps` 4,
`RM_Car` 6, `RM_Shoot` 8, `RM_Snuff` 15, `RM_Last` 16, `RM_Muggr` 18; `RM_MaxGangs` 47), and the ranks `RM_SOLDIER`
0, `RM_LT` 1, `RM_WARCHIEF` 2, `RM_BOSS` 3, `RM_BUM` 4, `RM_CIVILIAN` 5.

So **WAR PARTY** writes `gameType` 14, `gangSize` 5, `":WAR PARTY"` to `0x0063ef30`, 1, 1, 1 to `0x0063ef6c` /
`70` / `74`, and 0 to `0x0063ef78` (no presets). Confirmed (code) for the copy; the values are the disc's.

**The Game Type screen** (input `0x001fd1c8`, confirmed (code)) lists up to three entries, each only when the mode
offers it: id 0 "1 Player : Vs." (global string `0x34`), id 1 (`0x35`), id 2 (`0x36`). Confirm maps id 0 to
`gameMode` 3, id 1 to **2 (co-op)** and id 2 to **1 (versus)**. The runtime list order (one player, co-op, versus
for WAR PARTY; one player, versus for 1 ON 1) fits (confirmed (runtime) for the order). For ids 1 and 2:

- the first confirm shows a message (global string `0x77`), flagged at screen `+0x90`, that blinks (alpha 0 → 255 →
  0 in 1,500 ms halves) until the selection changes or the **second** pad presses START, which re-sends accept
  (confirmed (code) at `0x001fd408`; an earlier reading of a 1.5 s message was wrong);
- with no second pad (`0x001fe5d0` < 0) the next screen is 4, the no-second-controller screen.

When the mode has presets (`0x0063ef78`) the screen writes 255 to both packs and moves to screen 3.

**A gang** (`0x001ec980`) is added when its id is negative or the unlockables manager says it is unlocked (kind 3).
Its record:

| Offset | Meaning |
| --- | --- |
| `+0x00` | the gang id (pack = id − 1) |
| `+0x04` | the name, interned in the Rumble string pool |
| `+0x08` | side 1's roster: nine u32 character types |
| `+0x2c` | side 2's roster: a copy, rotated on its own |
| `+0x50` | the entry's index in the list |

Each type passes through `0x001ec490`: a type not yet unlocked (kind 4) is replaced by a stand-in. The pairs
(locked → used): `0xb2`→`0xc4`, `0xba`→`0xc7`, `0xb9`→`0xc0`, `0xc4`→`0xc5`, `0xdc`→`0xe1`, `0xdf`→`0xe2`,
`0x64`→`0x65`, `0x63`→`0x66`, `0x59`→`0x5b`, `0x5a`→`0x5e`, `0xed`→`0xf1`, `0x4d`→`0x51`, `0x87`→`0x8d`,
`0x89`→`0x8a`, `0x6a`→`0x6e`, `0xa1`→`0xa4`, `0xa0`→`0xa2`, `0x80`→`0x82`, `0x7b`→`0x7c`, `0x77`→`0x7e`,
`0x7a`→`0x7d`, `0x9a`→`0x9c`, `0x79`→`0x1a1`, `0x1a5`→`0x1a2`, `0x13b`→`0x1a3`, `0x103`→`0xfd`, `0x106`→`0xff`,
`0x105`→`0x102`. Confirmed (code). This explains the runtime defaults: gang 5's disc roster 89, 90, 91, 92, 93, 94,
91, 92, 93 becomes 91, 94, 91, 92, 93, 94, 91, 92, 93 without a save, and gang 3's 220, 223, ... becomes 225, 226,
224, ... (confirmed (runtime), [the 23 values](#rumble-setup)).

**The screens' code** (confirmed (code); names suggested for the `@orig` tags):

| Address | Role | Suggested name |
| --- | --- | --- |
| `0x001f8d80` | Game Mode input: event 4 confirms (next-screen code `+0x74` = 0, copies the entry, sound 8); event 5 backs out (from the front end, `0x0063ef64` set: "cancelled" `0x0050f4dc`; else `+0x74` = `0xffffff01`; sound `0xf`) | `RM_GameMode_OnInput` |
| `0x001fd1c8` | Game Type input (above) | `RM_NumPlayers_OnInput` |
| `0x001ef7c0` | Choose Gangs input | `RM_ChooseGangs_OnInput` |
| `0x001fe070` / `0x001fe0d8` | copy side 1's / side 2's gang name to `0x0063eef0` / `0x0063ef10` | `RumbleMode_SetGang1Name` / `2Name` |
| `0x001eb9f8` | Choose Area input: event 4 sets byte `+0xc4` = 1 and plays sound 8; event 5 sets `+0x74` = `0xffffff01`, sound `0xf` | `RM_ChooseArea_OnInput` |
| `0x001ebb90` | Choose Area launch: the selected entry's level index (`+0x60`) picks the level record (`*(0x0051489c)` + index × 0x84 + `0x14d8`); it writes the level number and "started" (`0x001fe1d8`, `0x001fe218`) | `RM_ChooseArea_Launch` |

`0x001ebb90` is called by the Choose Area update (`0x001ebc10`) one update after the input set `+0xc4`; it is not an
input handler.

The Choose Gangs input, by event: 0 / 1 move the cursor; 2 / 3 rotate the active side's roster left or right
(`0x001ec130` / `0x001ec028`), which picks the warchief, except in co-op (`gameMode` 2); 4 locks the active side, and
when both sides are locked (the pair at `+0x138` reads `0x0000000100000001`) writes the packs and types and calls
the two name copies, otherwise it switches to the other side; 5 unlocks or backs out.

**The "vs" title** is not part of the menu. `ShowRumbleModeIntro` (in the arena) calls `0x001b5f88`, which opens the
RM_Intro screen (`0x001b3880(0x600840)`, the HUD's screen at `+0xe530`, `0x001f9418`, init `0x001f9558`). That screen
makes a text widget for each name in the Lua table it is given (up to 10) with a separator widget (a `BaseWidget`,
code `0x1d0000`) between two names, and plays the announcer's `dj_vs` voice. Confirmed (code).

### The Rumble menu's screens {#rumble-screens}

How mode 0x11 looks and moves; its data and values are [above](#rumble-setup). Confirmed (code) at the cited functions
unless marked; "measured" is confirmed (runtime), PCSX2 2.9.94, window captures of a fresh boot, GUI units ±0.003.

**The mode around the screens.**

- `Enter` (`0x0015e8b0`) keeps the active camera (`+0xd0`), plays `menu` unless it is playing, holds every player's
  brain (`0x0028ced8(brain, 1)`), saves `0x005147cc` and calls `0x0040c938(10.0)` (role not traced), and starts the
  controller (`0x001f1748`): the [layout values](#rm-layout), `rumble_preload.lua` only in game (`0x0063ef64` = 0), the
  screens, two lights at the camera's position + (0, 8, 0) (`0x0017ef20`: kind 2 grey 0.18 at the front end, kind 3
  black; inferred to be lights), and the first screen, **Game Mode** from the front end (`RM_Main` in game).
- `Update` (`0x0015eca0`) runs a world frame with the HUD hidden. On the first frame the controller is ready
  (`0x001f23c0`) it queues a **0.7 s fade in** and makes a locked camera **`RM_Camera`** active at once: the active
  camera's position, field of view **50°**, heading **160.229°** (`0x43203aac`), pitch and roll 0, near 0.1, far 100.
  So behind the Rumble menu the view stops following the Wonder Wheel scene and holds still; the scene keeps playing
  (inferred). It also makes a dummy fighter (`0x001f1c78`, [below](#rm-dummy)).
- Once "started" (`0x0050f4e0`) or "cancelled" (`0x0050f4dc`) is set, every screen freezes (`0x001fe228`); started:
  the music stops (`0x00110528`) and a **1.5 s fade out**; cancelled: a **0.7 s fade out**. The mode pops when the
  fade's running flag (`0x005fdeb8 + 0x1d4`) clears.
- `Exit` (`0x0015ea40`) stops the controller, restores `0x005147cc`, and when cancelled makes the kept camera active
  again (no blend) and frees `RM_Camera`; plays `sound` only when the mode below is neither 8 nor 0x12; then the Lua
  callbacks ([QUICK RUMBLE](#quick-rumble)).

**The flow** (controller `0x001f1000`; field → class → constructor): `+0x7c` `RM_Main` `0x001fb060` (in game only),
`+0x80` `RM_NumPlayers` `0x001fc380`, `+0x84` `RM_No2ndController` `0x001fbc80`, `+0x88` `RM_GameMode` `0x001f7ee0`,
`+0x8c` `RM_ChooseArea` `0x001ea7e8`, `+0x90` `RM_ChooseGangs` `0x001ec238`, and the hub's gang editor
(`RM_CreateGang`, `RM_EditGang`, `RM_EditGangs`, `RM_SwapSoldier`). Transitions: Game Mode 0 → Game Type; Game Type
0, 1, 2 → Choose Gangs, 3 → Choose Area, 4 → No 2nd Controller; No 2nd Controller 1 → Choose Gangs, 2 → Choose Area;
Choose Gangs 0 → Choose Area, **1 → Game Type** (its back with nothing locked); every other back pops (`-0xff`).

#### Layout and background {#rm-layout}

The layout values (`0x001fdce0`, getters `0x001fdc80`-`0x001fdcd0`):

| Float | Default | `0x04` | `0x20` | `0x02` | `0x02` + `0x04` | Used for |
| --- | --- | --- | --- | --- | --- | --- |
| `0x0050f4f8` | 1.15 | 1.45 | 0.74 | 1.1 | 1.55 | background width |
| `0x0050f4fc` | 1.1 | 1.2 | 0.74 | 0.8 | 0.95 | background height |
| `0x0050f500` | 0.08 | 0.03 | 0.08 | 0.18 | 0.14 | title y |
| `0x0050f504` | 0.84 | 0.88 | 0.84 | 0.77 | 0.805 | option-grid y |
| `0x0050f508` | 0.805 | 0.83 | 0.805 | 0.74 | 0.76 | not traced |
| `0x0050f50c` | 0.91 | 0.96 | 0.91 | 0.82 | 0.86 | usage-line y |

- **Background:** one sprite, sheet-table record **12** rectangle 5, its own batch at depth 8,000 (`0x001f1c08`),
  centred at (0.5, 0.5), 1.15 × 1.1 with the aspect correction (`0x001a2120(w, h, widget, 1)`) (controller `Init`
  `0x001f1e00`). Its colour **cycles** red (150, 50, 50) → green (50, 150, 50) → blue (50, 50, 150) → red, alpha
  255, each leg a linear blend over **5,000 ms** (`0x0050f3e4`) on the real-time timer (`0x001f2468`, colours at
  `0x0063ee70`-`78` set by `0x001f2720`). Record 12 is the one-sprite sheet of CRC `0x349348bd` (inferred); the
  (93, 106, 49, 254) read at runtime ([GUI](gui.md#sprite-colours)) is 56 % of the way from red to green (inferred).
  Measured: red-brown on Game Mode, green two screens later; it covers the whole screen, so the Wonder Wheel is not
  seen behind any Rumble screen, yet the gang screen's 3D fighters are drawn over it (how they get in front of a 2D
  batch is open).
- **Other sprites:** sheet-table record **28**, a batch of 50 at depth 8,500 (`0x001f1b90`). Rectangle 30 is the
  scroll and side arrow (depth 8,100; the down arrow rotated π).
- **Title** (every screen): a text widget at (0.5, 0.08), centred, size **2.23**, `big_font`, grey `0x005fd310`.
  Measured: Choose Area's title centred at (0.500, 0.081).
- **Usage line:** a `UsageInfo` at (0.5, 0.91), centred, one line. Measured: centred at y 0.910.
- **Colours:** text and arrows grey `0x005fd310`; unselected grid items `0x005fd320` (80, 80, 80, 255) on Game Type.

**`ScrollingMenu`** (`0x001e1338`, vtable `0x0053bfc8`; Game Mode and Choose Area): up and down move the cursor with
cue **4** (`0x001e1e48`); **at either end the cursor stays**, cue **`0xe`**, and auto-repeat is switched off (`+0x104`);
at least 100 ms between inputs (`+0xd0`); the view scrolls to keep the cursor near the middle row, the scroll
animated by a reveal stepped 0.25 a frame (`+0xc8`).

#### Per screen

| Screen (`Init`, input) | Widgets (default mode) | Input and cues |
| --- | --- | --- |
| **Game Mode** `RM_GameMode` (`0x001f85c0`, `0x001f8d80`; update `0x001f9068`) | Title `0x2c`; usage `0x18`. A `ScrollingMenu` (`0x001f82c0`) of at most **3 visible** entries, set up at (0.42, 0.21), recentred each frame so its top is at 0.5 − h/2 + 0.02 (`0x001f8f98`); each entry a `ScrollingTextWidget` (`0x001e2f40`, wrap width 0.66 `0x0050f464`, backdrop 28/28) whose text is `<SIZE 1.6><BIGFONT>` title `</BIGFONT><SIZE 1.3><CR><SIZE 1.0>` description (`0x00557880`; both from `CfgRumbleGame`); scroll arrows at y 0.5 ∓ (h/2 + 0.03). Measured: entries left-aligned near x 0.20, the selected entry grey and the other dimmer. | Up / down as `ScrollingMenu`. Accept: cue 8, next. Back: cue `0xf`; from the front end "cancelled", in game a pop. |
| **Game Type** `RM_NumPlayers` (`0x001fc5b0`, `0x001fd1c8`; update `0x001fd408`, render `0x001fdaf8`) | Title `0x2e`; usage `0x21` with one entry, else `0x17`. `OptionGrid` at (0.5, 0.84), centred: `0x34`, `0x35`, `0x36` as offered, size 1.15, colour `0x005fd320`. **Badges** (size 0.1, depth 8,100, by language: English P1 28/0, P2 28/5, CPU 28/20; Spanish 4, 9, 20; French 1, 6, 21; Italian 3, 8, 20; German 2, 7, 22) at y 0.56, x 0.5 − 0.04 (right edge) and 0.5 + 0.04 (left edge): one player P1 vs CPU, co-op P1 at 0.505 and P2 at 0.615 vs CPU, versus P1 vs P2. A sprite 28/29 turned −90°, 0.28 × 0.15 (placement not traced). Message `0x77` at (0.5, 0.78), size 1.15, hidden. | Left / right move between the entries (one grid row). Accept: cue 8 (none on the message-showing first accept); see [the set-up](#rumble-data) for the values. Back: `0xf`, pop. |
| **No 2nd Controller** `RM_No2ndController` (`0x001fbe68`; update `0x001fc200`) | Text `0x3d` (a multi-line text widget) at (0.5, 0.5), wrap 0.6, grey, `big_font`; usage `0x1e` at (0.5, 0.75). | Each frame looks for a second pad (`0x001fe5d0`); when one appears it becomes player 2's and the result is 1 (Choose Gangs), or 2 for a mode with preset gangs (Choose Area). Back pops. |
| **Choose Gangs** `RM_ChooseGangs` (`0x001ecae0`, `0x001ef7c0`) | Title `0x2b`; usage `0x18` in co-op or while side 2 is active, else `0x24`. Two name boxes (`SimpleHeader`, 0.425 × 0.07, size 1.2, backdrop 28/10) at x **0.2505** and **0.7495**, mid-height (title bottom + 0.91) / 2, each with the gang's name and a sprite 28/23 (191, 191, 191) 0.13 above it, depth 11,000; a centre box (0.5, mid, 0.1 × 0.07) with `0x2f` ("vs."). Badges P1 / P2 / CPU (size 0.08) at (0.259, 0.19) and (0.745, 0.19), co-op's P2 at (0.339, 0.205). `OptionGrid` at (0.5, 0.84): `0x37` / `0x39` with one human, `0x37` / `0x38` with two (side labels; inferred). Two `Bar`s at (0.15, 0.62) and (0.65, 0.62), 0.27 × 0.03, back (64, 64, 64), fill (134, 26, 26) (role not traced). Arrows: up / down 0.022 beside the active side, left / right 0.015 (turned ±π/2) around its name. **3D fighters**: each side's first gang-size members (at most 5), `Human_Create` named `RM_HUMAN_%d` in the gang group `RM_Gangs0` / `RM_Gangs1`, dead (no AI), placed in camera space (`0x0011b970`) **5.7 m ahead and 1.05 m down, side 1 at x −1.15 and side 2 at +1.15**, further members alternating lateral steps of 0.5 and 0.325 m and 0.45 m further back, turned to face the camera; rebuilt when a gang or warchief changes (`0x001ef150`). | Up / down move the active side's gang cursor, cue 4; **no wrap**: at an end it stays with cue `0xe`; the two sides may not hold the same gang unless the gang size (`+0x1e4`) is 1 (the cursor skips the other side's gang, or bumps). Left / right rotate the roster (warchief), cue 4, unless the side is locked, both are locked or co-op. Accept locks the side, cue 8. Back unlocks, cue `0xf`; with nothing locked (one human, or side 1) result 1 (Game Type). |
| **Choose Area** `RM_ChooseArea` (`0x001eb0c8`, `0x001eb9f8`; list `0x001eabc0`, update `0x001ebc10`, launch `0x001ebb90`) | Title `0x2a`; usage `0x21` with one arena, `0x17` with fewer than two rows, else `0x1a`. A `ScrollingMenu` of `GridContainer` rows: up to **3 arenas a row** and **2 visible rows** (one row with fewer than 4 arenas), row width 0.95 (× count / 3 when short), row height 0.28, scroll arrows at y 0.18 and 0.845 (size 0.03). Each arena a `RumbleAreaPreviewWidget` (`0x00201fc0`, set-up `0x00202090`): the picture is the sprite word at its **level record `+0x80`**, white, depth about 9,001; frame 28/10 (about 8,001) and 28/19 (the highlight; inferred); a black label of size 0.055 at offset (0.002, −0.04) with the level record's **`+0x49` name** ("Fight Pen"). With two or more arenas a blocking preload (`0x0040e2d8`, up to 50,000 ms) loads `rumble_mode_arenas_<gameType>` first. Measured (one arena): the framed picture spans GUI x 0.40-0.57, y 0.43-0.55. | Move as `ScrollingMenu`. Accept: cue 8 (only if that sound slot is free), marks the arena; the next update launches it. Back: `0xf`, pop. |

#### The dummy fighter {#rm-dummy}

`0x001f1c78`, called once `RM_Camera` exists, makes a gang `RM_DUMMY_GANG` (`0x0016a1c8(0x19, ...)`) and a human
`RM_DUMMY_HUMAN` of type `0x160` at camera-space (0, 8.0, −0.85) turned to world space (`0x0011b970`), facing the
camera, brain dead; its handle goes to `0x001fe2c0`. No fighter was seen on the Game Mode or Game Type screens at
runtime (inferred: hidden by the opaque background).

### InitLevel

`0x0015fe90` (`GameModes/InitLevel.cpp`), used for the front end and every game level, in this order (confirmed
(code); roles of the callees inferred from their files and arguments): reset the audio, timers and task manager,
the game state, cameras, screen effects and actionables; reset the save buffers; copy the level's names into
`W_GameState + 0x124` and `+0x134`; **`WorldManager::LoadLevel(name, 0)`** (`0x0040dbb8`, the full level this time);
reset the AI, path and character tables; create the task-manager objects `load` and `Wind_Manager`; reset the HUD;
the script system's level entry point (slot `+0x24` with the level name); read the object list (`%s_objs.txt`, or
`../levels/%s/%s_objs.txt` on the host file system); add the `CrimeScene` and `GangCall` objects; load the level's
**dependency list** (`0x00178bc8` with the CRC-32 of the level name, blocking); preload the section's pack and the
world around the camera, for at most 30,000 ms (15,000 ms when the record's `+0x04` is below 101); start the level's
music (or the track `sound` when the level's is `none`); flush the file manager; play the intro movie `L%d_IN` when
the level record's flag `0x02` is set and this is the level's first section (`W_GameState + 0x33a` below 2, earlier
read here as a player count); call a pending Lua function (`0x005e6d88`) if one is set. The full order, the memory it
uses and what each step loads are on [Level loading](level-loading.md#initlevel).

### Input on the front end {#input}

**Reading the pads.** `Pads_Update` (`0x001454a8`) calls `Pad_Update` for records 0 and 4. It runs once per frame in
the start-up modes (mode 5, mode 6, mode 1's `Enter`, the error mode), in the movie player (`0x00429fe8`) and, in a
level, from a task-manager step that runs at most every `0x4b0000` ticks (16.7 ms, `0x003a3000`) or from the
character update (`0x00249108`, every second call). Confirmed (code) for the callers; the front-end menus get their
input from these records. `Pad_Update`
(`0x00144fb0`), confirmed (code):

1. Advance the ring index. If less than 6 ms of real time have passed since the last real sample, copy the previous
   button word; otherwise read the pad (`0x001498a8`: the `libpad` state machine, buttons, sticks, pressures) and
   stamp the time. A disconnected pad stops here.
2. Turn the left stick by the player camera into `+0x00`/`+0x04` (in game; it does not matter on the menus).
3. **Hold counters** for up, right, down and left: 0 while released; while held, count 1, 2, ... 15, and then wrap
   15 → 12, so a counter equals 15 on the 15th held sample and every 4th sample after it.
4. **Diagonals**: if more than one d-pad direction is held, keep only the most pressed one (none if no pressure).
5. **Lua pad handlers**: unless the top mode is 0xa, for each of the 16 bits pressed this sample (`0x00144a80`), if
   a handler is set in the table at `0x005dda90 + record × 0x40` (one script reference per bit), call it (script
   slot `+0x8c`). Lua sets handlers with `PadSetHandler(button, player, "function")` (`0x0036d878` → `0x00145698`,
   which stores the reference for the highest set bit of `button`).
6. Vibration state for the record.

**Auto-repeat** for menus is `0x00144ad0`: a direction counts as newly pressed on its first sample, and again on the
15th held sample and every 4 samples after it, so at 30 frames a second the first repeat comes after 0.5 s and then
7.5 a second. Confirmed (code); the timing in seconds assumes one pad update per frame.

**Menu commands.** The menu widgets read their player's pad: the HUD (`0x00600840`) hands out an input record per
player (`0x001ae898(hud, n)`) whose byte `+0x19` is the pad index, -1 for none (confirmed (code)); that these are the
`0x2c`-byte player input records at `0x00660f50` that the in-game update refreshes is inferred. `MenuInput_Dispatch`
(`0x001e95c0`) turns input into six commands, sent to the widget's handler (its vtable slot `+0x0c`):

| Command | From | Evidence |
| --- | --- | --- |
| 0 up | d-pad up (`0x1000`), or left stick y > 0.5 | confirmed (code) |
| 1 down | d-pad down (`0x4000`), or left stick y < -0.5 | confirmed (code) |
| 2 left | d-pad left (`0x8000`), or left stick x < -0.5 | confirmed (code) |
| 3 right | d-pad right (`0x2000`), or left stick x > 0.5 | confirmed (code) |
| 4 accept | cross (`0x0040`) | confirmed (code) |
| 5 back | triangle or circle (`0x0030`) | confirmed (code) |

- The d-pad bits come from the auto-repeating query (`0x001e93c0`) or the plain pressed query (`0x001e9518`); accept
  and back from the **released** query (`0x00144a30`) in the button pass (`0x001e9468`), so they fire when the
  button is let go. Confirmed (code) for the queries; that the two passes are combined as described is inferred from
  `0x001d4f20`, which runs the d-pad pass and then the button pass with the mask `0xffff0fff`.
- A command is accepted only if more than 110 ms have passed since the last one; for a stick held in one direction
  the gap is 400 ms until the stick returns to neutral. Confirmed (code).
- A widget that takes focus clears its record's state and blocks the d-pad for 20 ms (`0x001d4d28`, `0x001d4f20`).
- `PM_Greet` itself checks START (`0x0800`) with the auto-repeating query (`0x00207e28`).

The on-screen legends use the same mapping: the usage line of the menus (global string `0x1f`) shows the d-pad
glyphs for "select", cross for "ok" and triangle for "back" (corroboration from `config_strings_en.lua`, inferred).

### The profile manager {#profile-manager}

`PM_Controller` (`0x002040f0`) creates its screens and wires them into a **screen flow** (the
`ScreenFlowController`, [GUI](gui.md#screen-flow)). Each screen is a widget that ends a frame with a result code;
the flow follows the screen's transition for that code. Screens and their transitions, confirmed (code) at
`0x002040f0` (field = offset in the controller, then the screen's constructor):

| Field | Screen (class string) | Constructor | Code → next screen |
| --- | --- | --- | --- |
| `+0x64` | `PM_Greet` | `0x002077b8` | 0 → `PM_Mode`, 1 → `PM_NoSpace`, 2 → `PM_TooManyProfiles` |
| `+0x68` | `PM_NoSpace` | `0x0020a5c0` | 0 → `PM_Mode` |
| `+0x6c` | `PM_TooManyProfiles` | `0x0020d1b8` | 0 → `PM_Mode` |
| `+0x70` | `PM_Mode` | `0x00209bc0` | 0 → `PM_Profile`, 2 → `PM_Create`, 3 → `PM_Load`, 4 → `PM_Continue`, 5 → `PM_Extras`, 6 → `PM_NumPlayers`, 8 → `PM_Greet` |
| `+0x74` | `PM_Extras` | `0x00206ff0` | |
| `+0x78` | `PM_NumPlayers` | `0x0020b020` | 0 → `PM_Profile`, 1 → `PM_Create`, 2 → `PM_Load`, 3 → `PM_Continue` |
| `+0x7c` | `PM_Profile` | `0x0020bc80` | 0 → `PM_Load`, 1 → `PM_Create`, 2 → `PM_Load`, 4 → `PM_Profile` |
| `+0x80` | `PM_Create` | `0x00204f08` | 0 → `PM_Difficulty` |
| `+0x84` | `PM_Load` | `0x00209068` | 1 → `PM_Delete` |
| `+0x88` | `PM_Continue` | `0x00203110` | 1 → `PM_Delete` |
| `+0x8c` | `PM_Delete` | `0x00205950` | 0 → `PM_Profile`, 2 → `PM_Greet` |
| `+0x90` | `PM_Difficulty` | `0x002065f0` | 0 → `PM_Light` |
| `+0x94` | `PM_Light` | `0x00208320` | 0 → `PM_Subtitles` |
| `+0x98` | `PM_Subtitles` | `0x0020c8d0` | |

Besides the codes, a screen can return "stay" (`-0x100`) or "back" (`-0xff`, pop to the previous screen). The table
also holds transitions no screen produces (PM_Mode code 4 → `PM_Continue`, PM_Profile code 4) and the two Xbox screens
(below). Confirmed (code). Starting the controller (`0x00204a78`) resets its globals (`0x0050f584`-`0x0050f5c0`, the
first Lua callback in `0x0050f584`) and save-system `+0x124`, picks the [layout floats](#pm-layout) and enters
`PM_Greet`. The menu sprites come from the particle page **`menu_system`** (`0x00204c50`: sheet-table record 3, a
resource instance at depth 8,500 with room for 50 sprites, its id kept in `0x0050f588`; see
[GUI](gui.md#resource-instances)).

**The menus end through a flag, not an empty stack.** `PM_Controller`'s update (`0x00204ba0`) returns the global
`0x0050f5b0` ("done"), which `PM_Subtitles`, `PM_Load` and `PM_Continue` set (below); mode 0x12's `Update` then fades
out (see [Fades](#fades)) and pops. The controller draws nothing of its own: its embedded widget (`+0x10`, set up by
`0x0015e530`) only answers "is active" (`0x00204ea8`, `0x00204ed8`). There is no background picture, no title and
no transition animation on the PM screens: each screen's widgets appear and disappear on the frame the flow enters
or exits it (no widget fade or slide in `PM_Mode`, the flow or `OptionGrid`). Confirmed (code).

The profile manager's globals (getters and setters `0x00203f58`-`0x002040c8`), confirmed (code); meanings inferred
from their use:

| Global | Meaning |
| --- | --- |
| `0x0050f588` | the `menu_system` instance |
| `0x0050f594` | the chosen profile slot |
| `0x0050f598` | "create the new profile on exit": mode 0x12 `Exit` then calls save-system slot `+0x4c(slot, name)` |
| `0x0050f5a0` | delete mode (PM_Load lists profiles to delete) |
| `0x0050f5b0` | done |
| `0x0050f5b4` | a new game was started; read by `0x00204008`, the autosave check after the mission-complete mode |
| `0x0050f5bc` | fade in on resume (set by mode 6's `Exit`, `0x00203fa8(1)`) |
| `0x0050f5c0` | the 16:9 choice, applied to the device (`0x00194e28`, device `+0x45c`) by mode 0x12 `Exit` |
| `0x0063f1d8` | the profile name being made, at most 8 characters (`0x00204098`) |

#### Screen geometry {#pm-layout}

**Coordinates.** Every position below is a GUI point (x right, y down, the screen about [0, 1]², with the overlay
camera's 5 % safe margin, [Graphics](graphics.md#2d-drawing)). The PM screens are **left-aligned**: the x of every
widget is the layout float `0x0050f5c4`, which is **0 by default, the left edge of the safe area**, not a centre
(the earlier "x relative to the centre" reading was wrong). Texts start at x (left-aligned, proportional font) and
are vertically centred on their y. Confirmed (code) at the screens' `Init`s; confirmed (runtime), PCSX2 2.9.94,
screenshots of the window by handle: "PRESS THE START BUTTON" starts at 5.56 % of the width (GUI x 0.004) and its
capitals are centred at 78.2 % of the height (GUI y 0.811 for 0.81).

**The layout floats** (`0x00203b98`, picked at controller start from the device flags `*0x0050cdb4`; the `0x20` test
comes before `0x04`; getters `0x00203af8` x, `0x00203b08` usage y, `0x00203b18`-`0x00203b88` `0x5c8`-`0x5e4` in
order), confirmed (code); the default column confirmed (runtime):

| Float | Default (interlaced 4:3) | `0x20` | `0x04` (16:9) | `0x02` | `0x02` + `0x04` | Used for |
| --- | --- | --- | --- | --- | --- | --- |
| `0x0050f5c4` x | 0.0 | -0.05 | -0.11 | 0.02 | -0.14 | every widget's x (13 screens) |
| `0x0050f5c8` | 0.81 | 0.815 | 0.80 | 0.734 | 0.74 | greeting, one-row grids |
| `0x0050f5cc` | 0.76 | 0.765 | 0.74 | 0.70 | 0.695 | PM_Mode's grid, two-row grids |
| `0x0050f5d0` | 0.71 | 0.71 | 0.68 | 0.667 | 0.655 | three-row grids |
| `0x0050f5d4` | 0.657 | 0.665 | 0.625 | 0.63 | 0.61 | four-row grids |
| `0x0050f5d8` | 0.745 | 0.745 | 0.73 | 0.687 | 0.687 | title over a one-row grid |
| `0x0050f5dc` | 0.70 | 0.70 | 0.67 | 0.65 | 0.65 | title over two rows |
| `0x0050f5e0` | 0.65 | 0.64 | 0.605 | 0.62 | 0.61 | title over three rows |
| `0x0050f5e4` | 0.60 | 0.60 | 0.545 | 0.585 | 0.56 | title over four rows |
| `0x0050f5e8` | 0.87 | 0.885 | 0.87 | 0.78 | 0.79 | the usage line |

**Common widgets**, confirmed (code) at each screen's `Init` (the widget classes are on [GUI](gui.md#widgets)):

- **Title:** a text widget at (x, title y), font size **1.4**, red `0x005fd328` (170, 43, 43, 255), `big_font`.
- **Menu:** an `OptionGrid` at (x, grid y), items packed left to right in each row, font size **1.15** for every item
  (it is not a selection scale), `big_font`; unselected items red, the selected one grey `0x005fd318` (178, 178,
  178, 255); an item followed by another in its row carries the separator `" : "` in red (`part_page0`). Rows are
  `6h / 7` apart by the code (h = 0.0548 at size 1.15: 0.0469); **measured 0.0505** on every screen (rows at 0.761,
  0.812 for 0.76; 0.711, 0.762, 0.812 for 0.71), so the measured pitch is the one to match (open which factor the
  code adds). A move plays cue **5**; a move that cannot leave its item plays **`0xe`**. Accept plays cue **9**, back
  cue **`0xf`**, unless a screen says otherwise.
- **Usage line:** a `UsageInfo` at (x, `0x0050f5e8`), left-aligned, size 1.0, grey `0x005fd310`, `part_page0`, string
  `0x1f` ("ok" with cross, "back" with triangle: two lines). Measured: the lines centred at GUI y 0.871 and 0.915, the
  glyphs from x 0.009 and the words from x 0.040 (PM_Mode).
- **Re-entering a screen** runs its `Init` again (the flow exits covered screens), so the default selection comes
  back: after backing out of EXTRAS, a left press on PM_Mode went from STORY to QUICK RUMBLE. Confirmed (code) for
  the `Enter` → `Init` path; confirmed (runtime).

#### The screens {#pm-screens}

Confirmed (code) at the cited functions unless marked; "measured" values are confirmed (runtime), PCSX2 2.9.94, a
boot with an unformatted card, GUI units from window captures (±0.003). Text is named by global string id with a
few words of ours; the texts are in `config_strings_<lang>.lua` ([GUI](gui.md#strings)).

| Screen (ctor, `Init`, handler) | Widgets and layout (default mode) | Input |
| --- | --- | --- |
| **`PM_Greet`** (`0x002077b8`, `0x002079a0`, update `0x00207e28`) | **Logo:** `menu_system` rectangle 0 (sprite word `0x30000`), red `0x005fd328`, depth 11,000, **left edge** at x (anchor 1), centred on y `0x0050f628` = 0.2, height `0x0050f624` = 0.33 overlay units (0.30 of the screen height), width from the rectangle's pixel aspect; no shadow (0.23 and 0.27 with flag `0x02` alone). **Text:** `0x76` (press START) at (x, 0.81), size 1.15, red, `big_font`, proportional, blinking (below). Measured: logo at x 0.001-0.44, y 0.041-0.361; text x 0.004-0.445. | START (auto-repeating query) → result 0 (PM_Mode), cue 9, and the screen stops drawing; ignored while the attract flag `+0xac` is set and a fade runs. No back. |
| **`PM_Mode`** (`0x00209bc0`, `0x00209da8`, `0x0020a268`) | Grid at (x, 0.76), rows **{2, 1}**: `0x78` STORY (code 0, with separator) and `0x8a` EXTRAS (5) on row 0, `0x79` QUICK RUMBLE (1) on row 1; with flag `0x02` one row {2} of STORY and QUICK RUMBLE at 0.81. Measured: "STORY : EXTRAS" capitals centred at 0.761, QUICK RUMBLE at 0.812, both from x 0.004. | Left / right walk STORY → EXTRAS → QUICK RUMBLE with wrap; up / down switch rows keeping the column (clamped). Accept and back: the handler paragraph below. |
| **`PM_Extras`** (`0x00206ff0`, `0x002071d8`, `0x002074f8`) | Grid at (x, 0.81), one item `0x8c` TRAILER (code 0). Measured: 0.811. | Accept: `Menu.playMovie(1)` (the `TRAILER` movie, `0x00558b00`), cue 9, stay. Back: pop, `0xf`. Both ignored while the fade level is not 0. |
| **`PM_NumPlayers`** (`0x0020b020`, `0x0020b208`, `0x0020b710`) | Grid at (x, 0.81), rows {2}: `0x8d` 1 player, `0x8e` 2 players. Prompt `0x77` (player 2 press START), size 1.15, red, at y 0.74 (0.72 16:9; 0.68 / 0.685 flag `0x02` / + `0x04`), hidden. | First accept on 2 players shows the prompt, blinking 0 → 255 → 0 in 1,500 ms halves, and waits for START on the **second** pad (HUD player 1's), which re-sends accept; moving back to 1 player hides it. Accept: `0x00419ac0(W_GameState, index ≠ 0)` (two-player flag), result 0 (PM_Profile). Input ignored while a fade runs. |
| **`PM_Profile`** (`0x0020bc80`, `0x0020be68`, `0x0020c588`) | Title `0x7b` (profile manager). One item a row: `0x7d` use existing (code 0, if any profile), `0x7c` create new (1, when the count `+0x9c` is 0, or not 6 and save-system `+0x144` reports room), `0x7e` delete (2, if any), `0x7f` reload (3, always). Title / grid y by the count of optional items 3, 2, 1, 0: `5e4`/`5d4`, `5e0`/`5d0`, `5dc`/`5cc`, `5d8`/`5c8`. Measured (no profile, two items): title 0.701, rows 0.761 and 0.812. | Accept clears delete mode, then: 0 → result 0 (PM_Load); 1 → 1 (PM_Create); 2 → delete mode, result 2 (PM_Load); 3 → Lua `Menu.reloadProfiles`, stay. Input ignored while a fade runs. |
| **`PM_Create`** (`0x00204f08`, `0x002050f0`, `0x002056d8`; keyboard `0x001cc1a0`, its handler `0x001cca80`) | Title `0x89` (enter name) at y `0x0050f5f8` = 0.5 (0.44 16:9; 0.485 / 0.463 flag `0x02`). Name: size 2.0, grey 178, at y `0x0050eb08` = 0.565 (0.52 16:9), starting from `0x0063f1d8`. Keyboard: the characters of `0x97`, then `0x99` OK and `0x9a` DEL, size 1.0, red, `big_font`, rows of **12, 12, 12, 11** cells from y `0x0050eb10` = 0.625 (0.6 16:9), x = grid x − 0.005; blank cells (0.94) are not selectable. Error text (message widget `0x001c16a8`) at y `0x0050f5fc` = 0.45 (0.38 16:9), size 0.85, red, hidden. Measured: title 0.50, name 0.564, key rows 0.627, 0.684, 0.740, 0.797; columns about 0.036 apart (centres of the first and twelfth near x 0.014 and 0.413, ±0.005). | Key move cue **7**. A character appends it (`_` is a space; at most **8**), cue `0xa`, or `0xe` when full; at the maximum the cursor jumps to OK. DEL removes one (`0xc`). Up from item 8 or down from item 32 jumps to OK. OK: an empty or all-space name plays `0xe`; otherwise `0xb`, then a name already used by one of the 6 slots shows `0x86`, else the name is kept and the result is 0 (PM_Difficulty). Back empties the name and pops. `Init` stores the free slot (save-system `+0xa4`) in `0x0050f594`. |
| **`PM_Difficulty`** (`0x002065f0`, `0x002067d8`, `0x00206d88`) | Title `0x8f` at `5e0` (`5e4` with four items). One item a row from `5d0` (`5d4` with four): `0x90`, `0x91`, `0x92`, and `0x93` when a save-system query reports the fourth unlocked. Default selection index 1, or 3 with four items. Measured: title 0.651, rows 0.711, 0.762, 0.812, the middle selected. | Accept: byte `W_GameState + 0x43c` = index, result 0 (PM_Light). |
| **`PM_Light`** (brightness; `0x00208320`, `0x00208510`, `0x00208cc0`, set-value `0x00208c18`) | **Square:** `menu_system` rectangle 5 (sprite word `0x30005`), left edge at x, centred on y `0x0050f634` = 0.71, height `0x0050f644` = 0.14 overlay units, depth 11,000, drawn each frame in (v, v, v, 255). **Bar** (class `Bar`, `0x001a0fd0`): left at x, y `0x0050f638` = 0.815, width `0x0050f64c` = 0.6 and height `0x0050f648` = 0.025 overlay units, back (64, 64, 64, 255), fill red to v / 100, `menu_system` rectangle 1. **Hint** `0x118` (a markup text widget), size 0.85, red, `part_page0`, at (`0x0050f63c` = 0.13, `0x0050f640` = 0.654). Per mode: 16:9 x 0.05, height 0.03, width 0.65; flag `0x02` square y 0.65, bar y 0.74, text (0.21, 0.59), width 0.45 (0.08 and 0.55 with `0x04`); flag `0x20` keeps the default values but a text x of 0.1 and width 0.55. Measured: square 0.10 × 0.124 of the screen from x 0.001; bar 0.375 of the screen width, filled 40 %; hint's first line at (0.131, 0.656). | The value v (0-100) starts at **40**; left / right step 5 within the range, cue **6**, auto-repeating (the screen runs the d-pad pass itself, mask `0xf000`). Each change calls `Gamma_Set(v)` (`0x001b4838`: `W_GameState + 0x57a4` = v and a colour (v/255, v/255, v/255, 1) to `0x0017ec38` on `0x0050cce4`). Accept: save-system `+0x124` = 1, result 0 (PM_Subtitles). |
| **`PM_Subtitles`** (`0x0020c8d0`, `0x0020cab8`, `0x0020cf30`) | Title `0x94` at `5d8`; grid at `5c8`, rows {2}: `0x95` ON (separator), `0x96` OFF. Default OFF when the language (`W_GameState + 0x120`) is English, else ON. Measured: title 0.746, row 0.811, OFF selected. | Accept: `W_GameState + 0x438` = (ON chosen), cue 9, then `0x0050f5b4`, `0x0050f598` and **`0x0050f5b0` (done) = 1**: the menus end and the story starts. |
| **`PM_Load`** (`0x00209068`, `0x00209250`, `0x002098b0`) | Title `0x88` (choose profile), or `0x7e` in delete mode, size 1.4. One item per used slot (its name, code = slot), two a row: rows {n}, {2, n−2}, {2, 2, n−4} for n < 3, 3-4, 5-6, with title / grid y `5d8`/`5c8`, `5dc`/`5cc`, `5e0`/`5d0`. | Accept stores the slot (`0x0050f594`); in delete mode, or when save-system `+0x7c(slot)` reports it damaged, result 1 (PM_Delete); otherwise load (`+0xcc(slot)`), `+0x124` = 1 and done = 1. |
| **`PM_Continue`** (`0x00203110`, `0x00203300`, `0x00203800`) | The profile's name, size 2.0, grey, at y 0.73 (0.72 16:9; 0.68 flag `0x02`); grid at `5c8`, rows {2}: `0x7a` CONTINUE (0), `0x80` DELETE (1). Not reachable on the PS2 (no screen returns code 4 from PM_Mode; inferred). | CONTINUE loads and ends like PM_Load; DELETE → result 1 (PM_Delete). |
| **`PM_Delete`** (`0x00205950`, `0x00205b38`, `0x00206238`) | The profile's name, size 2.0, grey, at y `0x0050f608` = 0.73; text `0x82` (sure?) or `0x83` (damaged) at y `0x0050f604` = 0.66, size 0.85, red; grid at `5c8`, rows {2}: `0x84` YES, `0x85` NO, **NO selected**. | YES: save-system `+0x54(slot)` and `+0x18c(slot)`, Lua `Menu.deleteProfile`, result 2 (PM_Greet) when damaged, else 0 (PM_Profile). NO: pop with cue 9. Input ignored while a fade runs. |
| **`PM_NoSpace`**, **`PM_TooManyProfiles`** (`0x0020a5c0` / `0x0020d1b8`, `Init` `0x0020a7a8` / `0x0020d3a0`) | Xbox screens: free-blocks and too-many-profiles texts `0xc3`-`0xc8`, choices `0xc9` Continue and `0xca` Xbox Dashboard, text at y 0.76 (0.85 with device flag 8). | Unreachable on the PS2 (PM_Greet never returns 1 or 2; inferred). |

Every screen's back is "back" (`-0xff`) with cue `0xf`, except PM_Mode's (result 8, to PM_Greet) and PM_Delete's NO.

**PM_Greet's blink and idle**, confirmed (code) at `0x00207e28`: the period `+0x98` is 1,500 ms and the phase `+0x9c`
starts at 1; phase 1 ramps the text's alpha 0 → 255, phase 0 ramps it 255 → 0, each linearly over the period
(`0x00337568`), then the phase flips. While a screen fade is running or its level is not 0 (`0x005fdeb8 + 0x1d4`,
`+0x1d8`) the text is fully lit, the phase is forced to 1 and its timer restarts, and the idle clock `+0xa8` restarts.
After **70,000 ms** of real time without a fade the screen sets `+0xac`, restarts the clock and calls the Lua function
**`Menu.playMovie(2)`** (`0x00558c60`): the attract movie ([Movies](#movies)). `+0xac` is never cleared here. The
blink and the attract movie are confirmed (runtime): the greeting's capitals fade in and out, and with no input the
`L1_IN` movie began playing over the greeting, its first picture the Wonder Wheel.

**PM_Mode's handler** (`0x0020a268`), confirmed (code), ignores input while a screen fade is not
finished (`0x005fdeb8 + 0x1d8` ≠ 0). **Back** (command 5): result 8 (→ `PM_Greet`) and front-end sound cue `0xf`.
**Accept** (command 4):
sound cue 9, then by the selected item's code: 0 → result 0 (`PM_Profile`), or 6 (`PM_NumPlayers`) when two or more
pads are connected (the count of non-zero words at `0x005dd85c`, stride 0x50, eight pads); 5 → result 5; 7 → call
the Lua function `Menu.reloadProfiles` (`0x00558fe0`; no item of this menu has code 7); any other code, so **code 1
"QUICK RUMBLE"**, → call the Lua function the profile manager was started with (`0x0050f584`, the first
`ShowProfileManager` argument, `Menu.fadeToRMI`) and stay. The script then fades out and opens the Rumble mode
interface ([Scripts](scripting.md#level100lua-the-front-end)).

### The 3D background {#background}

Behind the PM screens is `level100`'s world, seen through the **Wonder Wheel scene's own camera**. The script's
camera never shows anything, the PM screens never change the camera, and only the Rumble menu replaces it
([`RM_Camera`](#rumble-screens)). What the script spawns and in which order is on
[Scripts](scripting.md#level100lua-the-front-end); how a scene plays is on [Scenes](scenes.md).

- **What loads** (`InitLevel` with record 0): `level100.lev` and its two packs (disc check, names and counts only:
  the `.lev` holds one world chunk `0x15`, four texture dictionaries, three models, path data and chunks `0x03`-`0x07`,
  `0x17`, `0x51`-`0x53`; `level100_1.pak` 66 resources, `level100_2.pak` 64). The preload around the camera may take
  at most **15,000 ms** (number 100 is below 101). There is **no `level100_objs.txt`** on the disc (its name hash,
  CRC-32 of `./ee_files/level100_objs.txt`, is not in `WARRIORS.DIR`, while `level99_objs.txt`'s is), so every object
  shown is spawned by the script. Record 0 has no intro movie (`LT_NONE`). Inferred, from the disc and the rules on
  [InitLevel](#initlevel).
- **The script's camera**: `CameraCreateLocked("Black", {0, 2, -10}, 40, 180, 0, 0, 0.5, 200)` made active at once;
  `Camera_CreateLocked` (`0x0011bc48`) clamps far to **150** (confirmed (code)). It stands about 550 m from the wheel,
  past its far clip, so it shows black until the scene's camera takes over (inferred, from the positions).
- **The scene** `WonderWheel_100` (`wonderwheel_100.scn`, scene id 34) plays looping, with no letterbox, not
  skippable and without freezing the world. It has a camera, so the scene pushes the current camera and makes its own
  current ([Scenes](scenes.md#camera)): the view is the `camera01` track. Its definition (inferred, the file read
  with the layout of [Scenes](scenes.md#header)): first pose at (462.60, −122.35, −187.93), about 75 m from the hub at
  (515.51, −68.89, −188.67), near 0.5, far 150, preload radius 500, field of view **54.43°**. The wheel's eight rim
  cars ride a ring about 21.5 m in radius, the sixteen others slide; their tracks drive them.
- **Lights** (`global.lua`'s matrix entry for level 100): moon (0.06, 0.06, 0.1) from (0.391, −0.474, −0.789),
  reflected (0.04, 0.02, 0.02) from (0.496, 0.552, 0.67), no ambient, level colour (0.03, 0.08, 0.12, 0.17). No fog,
  sky or time-of-day call in the scripts (presumably from the world chunk; not traced). Inferred, disassembly.
- **What the screen shows** (confirmed (runtime), PCSX2 2.9.94): the wheel's neon outline on black at the right of
  the screen, its lit outline spanning logical x 335-615 and y 53-408 of 640 × 448 (GUI x 0.53-1.01, y 0.08-0.95),
  the "WONDER WHEEL" sign at its hub near logical (410, 217); the same on every PM screen, the outline still to within 2
  logical pixels over 12 s (the camera does not visibly move), while the spokes' neon turns between captures (the
  wheel rotates). It also rotates after an attract movie (so the restart below does start the scene again).
- **Who drives it:** `Menu.startScene` / `stopScene` (`WonderWheelAnim:enable(true / false)`) around movies and the
  start of a level; nothing changes between PM screens. Both always loop `music/wonderwheel_132b` first. Inferred.
  `start` calls `GetSceneID`, which no binding or script defines, when the scene is already loaded and idle
  (inferred: the script would fail there; the runtime above shows the wheel moving after the movie, so that path is
  not the one taken, or fails harmlessly).

### Fades {#fades}

The fades are the screen-effects managers' ([Graphics](graphics.md#screen-effects)), confirmed (code) at `0x0018cc60`
(start) and `0x0018ce58` (each frame): a fade out of `t` seconds runs its level from 0 to 1 over `t − 0.2` s when `t` >
0.2 (so a "1.0 s" fade out takes 0.8 s), a fade in from 1 down to the base alpha over `t`; the level is clamped to
[0, 1]; the black quad's alpha is `level × 255`; the first frame after a request only marks it running (state 1 → 2,
so a fade in starts at full black); a fade out also calls `0x001b2658` on both HUD players (inferred: hides the HUD). A
running fade (`+0x1d4`) or a level ≠ 0 (`+0x1d8`) is what the menus wait on.

| When | Fade | Evidence |
| --- | --- | --- |
| `Menu.onStart` (the menus appear) | in, 1.5 s | inferred (script) |
| PM menus done (story) | out, 1.0 s, then mode 0x12 pops (`0x0015e238`) | confirmed (code) |
| mode 0x12 `Update` with `0x0050f5bc` (after a card save) | in, 1.0 s | confirmed (code) |
| QUICK RUMBLE (`Menu.fadeToRMI`) | out, 0.7 s; `Menu.launchRMI` 500 ms later | inferred (script) |
| Rumble menu ready (`0x001f23c0`) | in, 0.7 s | confirmed (code) |
| Rumble started / cancelled | out, 1.5 s / 0.7 s | confirmed (code) |
| `Menu.cancelRumbleMode` | `Menu.fadeIn` after 100 ms: in, 0.7 s | inferred (script) |
| `Menu.playMovie(id)` | out, 1.0 s; the movie 500 ms later | inferred (script) |
| after a movie (`Menu.movieFinished`, 500 ms after it returns) | in, 1.0 s | inferred (script) |
| `Menu.reloadProfiles` | out, 1.0 s; the load 700 ms later | inferred (script) |
| `Menu.deleteProfile` | instant black (`ScreenQueueEffect(1, 0)`) | inferred (script) |

**At runtime** (PCSX2 2.9.94, captures about 50 ms apart): after PM_Subtitles' accept the picture went from full to
black in about 0.35 s, faster than the 0.8 s the code gives; the frame-time argument of `0x0018ce58` (its `f12`) is
not traced, so the real speed is open. The fade in after an attract movie reached full brightness within about 1 s.

### Movies and the attract loop {#movies}

- **Start-up:** `main` loads `level1`, plays `LOGO` (argument 0), `PLOGO` and `L1_IN` (argument 1) through
  `Movie_Play` (`0x0042a938`), unloads, then pushes the modes ([Boot](boot.md#main)). Confirmed (code). At runtime
  START skips a movie (the `L1_IN` intro gave way to the legal screen on START).
- **`Movie_Play`** stops the music, loads `<name>_sub` when subtitles are on (`0x0050ea74`), clears both buffers to
  black, plays (`0x00429fe8`), and on return sets **both screen-effects managers fully black**
  (`0x0018cc60(0, mgr, 1, black, 0)`), so the screen stays black until a fade in. Confirmed (code).
- **Attract:** PM_Greet calls `Menu.playMovie(2)` after 70,000 ms of real time with no fade ([the screens](#pm-screens));
  the 1.5 s fade in at `onStart` keeps the clock at 0, so the first attract movie starts about **71.5 s** after the
  menus appear (inferred). `Menu.movies` is `{[1] = "TRAILER", [2] = "L1_IN"}`, so the attract movie is **`L1_IN`**
  (the intro, which opens on the Wonder Wheel), and the loop repeats every 70 s of idling on PM_Greet. Only PM_Greet
  has an idle timer; PM_Mode and the other screens wait forever. Confirmed (code) for the timer and the call;
  confirmed (runtime): idle on PM_Greet, `L1_IN` played over it and returned to PM_Greet.
- **`TRAILER`** plays only from EXTRAS (`PM_Extras`' accept, `Menu.playMovie(1)`); no idle path reaches it.
  Confirmed (code).
- `Menu.playMovie` (inferred, script): keep the id, stop the music, fade out 1.0 s; after 500 ms `stopScene`, then
  `PlayMovie(movies[id])` (blocking); 500 ms after it returns `startScene` and a 1.0 s fade in.

### The message box and card dialogs {#message-box}

The message box at `0x005e5840` (constructor `0x001c6a20`, `Init` `0x001c6c18`, update `0x001c7630`, render
`0x001c76a8`, handler `0x001c7408`) shows mode 6's messages and choices. Confirmed (code):

- **Timed message** `0x001c6fc8(box, text, ms, style)`: style 0 centred at (0.5, 0.5), grey `0x005fd310`,
  `big_font`; style 1 at (`0x0050ea24`, 0.9), (134, 26, 26, 255), font slot 4.
- **Choice dialog** `0x001c7128(box, text, n, labels, default, callbacks)`: the message as above; a one-row grid at y
  `0x0050ea2c` = **0.745**, items grey (80, 80, 80) unselected, `part_page0`, a single choice gets an empty label;
  the usage line `0x1d` (one choice) or `0x23` (two) at y `0x0050ea30` = **0.8**; it plays `menu` if it is not
  playing. Accept plays cue **8** and runs the chosen callback once that sound has finished (inferred,
  `0x001c73e8`). While it is open, the first pad to press a button or push a stick past 0.5 becomes player 1's when
  player 1 has none (`0x001c7488`).
- Measured (the format question after a new profile, PCSX2 2.9.94): four centred lines from GUI y about 0.44 to 0.57,
  the choices "Yes : Continue without saving" centred at y 0.745 with the second selected (grey) and the first
  dimmer, the usage line at 0.80, all over black.

| Function | Message | Choices | Default |
| --- | --- | --- | --- |
| `0x0015a370` | `0xa8` | `0xba` / `0xbb` | 1 |
| `0x0015a9f0` | `0xa3` | `0xba` / `0xc0` | 0 |
| `0x0015aa80` | `0xa4` | `0xba` / `0xc0` | 1 |
| `0x0015ab10` | `0xa5` + size + `0xa6` | `0xc0` / `0xbd` | 1 |
| `0x0015ac88` | `0xa1` or `0xa2` | `0xba` / `0xc0` | 0 |
| `0x0015ada0`, `0x0015b0b0` | `0xa0` | `0xbc` / `0xbd` | 0 |
| `0x0015af20` | `0xac` (card unformatted: format?) | `0xba` / `0xc0` | 1 |
| `0x0015afb0` | `0xad` | `0xba` / `0xbb` | 1 |
| `0x0015b2d8` | `0xaf` + `0xb0` or `0xb1` | `0xc0` / `0xbd` or `0xba` / `0xbd` | 1 |

Timed messages: `0xb5` checking the card (3,000 ms, `0x0015a328`), `0xae` formatting (3,000 ms), `0xa9` autosaving
(2,000 ms, style 1), `0xab` deleting (3,000 ms).

### The front-end scripts

The front end's behaviour beyond the C++ screens lives in Lua 4.0 bytecode on the disc. The scripts have been
disassembled (2026-10-04); the script system and `level100.lua`'s `Menu` functions in detail are on
[Scripts](scripting.md). In short (inferred, from the disassembly):

- `level100.lua` defines the table `Menu` with `onStart`, `onFinish`, `fadeToRMI`, `launchRMI`, `startRumbleMode`,
  `cancelRumbleMode`, `fadeIn`, `startGame`, `stopScene`, `startScene`, profile reload and delete helpers, and
  `playMovie` with the movies `TRAILER` and `L1_IN`. It drives the Wonder Wheel cinematic (`WonderWheel_100`, objects
  `dyn_s_wwcart_simple_*`, `dyn_s_wwheel_a`, `dyn_s_neon_*`) and the music `music/wonderwheel_132b`, and uses the
  bindings `ShowProfileManager`, `ShowRumbleModeInterface`, `ScreenQueueEffect`, `ScheduleFunc`, `GetPTank`,
  `ReleasePTank`, `SoundLoopMusicTrack`, `SoundStopMusicTrack`, `SetCheckPoint`, `MenuLoadLevel`, `PlayMovie` and
  the `SSMC_*` save-sequence functions. `Menu.startGame` (the profile manager's second callback) stops the music
  and the scene and calls `runNextMission(1)`, a `global.lua` helper; `Menu.startRumbleMode(n)` calls
  `MenuLoadLevel("level" .. n)`.
- `level100main.lua` is a different, Lua-driven menu (dialogs `DlgGreet`, `DlgMode`, `DlgProfile`, ..., a serial
  number and "dongle" check). It calls functions that the PS2 executable does not register (`Simon_*`,
  `GetDefaultUIFont`, `CreateCenteredSprite`; none of these names occur in the executable or elsewhere in the WAD),
  so it is not the retail front end. Inferred; that it is a leftover of another platform or a prototype is
  speculative.
- `config_preload2.lua` loads the UI strings for the current language: `doFile("config_strings_" .. ext)` with `ext`
  one of `en`, `de`, `fr`, `it`, `es` from `GetLanguage`, then `CfgHUDMessage(id, text)` for each entry of
  `GSTRING.HUD` ([GUI](gui.md#strings)).

### Audio cues (references only)

| Cue | Where | Evidence |
| --- | --- | --- |
| music track `menu` | mode 8 `LevelFlow_StartFrontEnd` (`0x0015c4b0`); mode 0x12 `Enter` if not playing | confirmed (code) |
| music `music/wonderwheel_132b`, `MenuTrack` | `level100.lua` | inferred (script constants) |
| front-end sound cue 9 | `PM_Greet` on START (`0x0010fc30`: entry 9 of the audio manager's table at `+0x1e0`, played with flags `0x12`) | confirmed (code); what the table is (probably the `Static Sounds` chunk `0x29`) is inferred |
| front-end cues (`0x0010fc30(*0x0050aa84, id)`) | **4** list move (`ScrollingMenu`, gang screen, `OptionGrid` default); **5** PM grid move; **6** brightness step; **7** name-keyboard move; **8** Rumble accept, message-box accept; **9** PM accept, START on PM_Greet; **`0xa`** name character; **`0xb`** name OK; **`0xc`** name delete; **`0xe`** a move or entry refused (end of a list, full name); **`0xf`** back | confirmed (code) at the screens above; how they sound is [Sound](sound.md) |
| `<SOUND name>` markup | any text widget, played once on first display ([GUI](gui.md#markup)) | confirmed (code) |
| sound bank `menu.msb` / `menu.msd` | WAD entries 9,799 and 9,800 | inferred (names) |
| `vags/interface/menu/*` | `level100main.lua` only (not the retail menu) | inferred |

## Matching the original {#matching}

What a screenshot-for-screenshot front end needs, one line per screen or feature; "today" is Coney as of this page's
[implementation section](#coneys-implementation).

| Done | Screen or feature | Coney today | Needed to match |
| --- | --- | --- | --- |
| [x] | Legal screen | the picture, overfilled, 5 s | nothing |
| [ ] | Start-up movies | the `MoviePlayer` hook; skipped | a player for `LOGO`, `PLOGO`, `L1_IN`, START skipping ([Movies](#movies)) |
| [ ] | Mode 6 at boot | one black frame | the card scan, `0xb5` for 3 s, the dialogs and their layout ([message box](#message-box)) |
| [ ] | 3D background | black | `level100` loaded, the script's objects, the `WonderWheel_100` scene and its camera, lights ([Background](#background)) |
| [ ] | Menu music | recorded and logged | `menu`, then `wonderwheel_132b` ([Sound](sound.md)) |
| [ ] | Fades | linear black quad over the given time | fade out over `t − 0.2` s, levels clamped, the one-frame start, both managers ([Fades](#fades)) |
| [ ] | PM_Greet | logo centred, text centred under it, white | logo rect 0 **red**, left edge at x 0, centre y 0.2, 0.30 of the screen high; text `0x76` left at (0, 0.81), red, size 1.15; blink; 70 s attract ([screens](#pm-screens)) |
| [ ] | Attract loop | `Menu.playMovie(2)` called, movie skipped | the `L1_IN` movie with the fades around it ([Movies](#movies)) |
| [ ] | PM_Mode | one centred column, white / grey 160, wrap | rows {2, 1} "STORY : EXTRAS" / QUICK RUMBLE at (0, 0.76), red items, grey (178) selection, size 1.15 for all, the red `" : "`, cues 5 / `0xe`, left / right / up / down rules |
| [ ] | Usage line | centred text | left at (0, 0.87), two lines ([geometry](#pm-layout)) |
| [ ] | PM_Extras | placeholder | TRAILER at (0, 0.81); accept → `Menu.playMovie(1)` |
| [x] | PM_NumPlayers | rows {2}, the blinking `0x77`, pad 2's START | nothing known |
| [x] | PM_Profile | title `0x7b`, up to four items placed by count; reload | nothing known |
| [x] | PM_Create | the 12-12-12-11 keyboard, the 8-character name, its cues and the duplicate check | the keyboard's moves and the "name used" text's lifetime are Coney's |
| [x] | PM_Difficulty | three or four items, default index 1 (3), `W_GameState + 0x43c` | the fourth item's query |
| [ ] | PM_Light | square, bar and hint; value 40, step 5 | the brightness applied (`0x0017ec38`), the bar's exact width |
| [x] | PM_Subtitles | ON : OFF, default by language; sets done | nothing known |
| [ ] | PM_Load / PM_Continue / PM_Delete | written over Coney's session-only profiles | Coney's saves (save research) |
| [ ] | Leaving the menus | done flag, 1.0 s fade out, the profile created, `Menu.startGame` | the 16:9 apply, the card dialogs, the fade's arithmetic |
| [ ] | Rumble menu frame | PM_Mode's style on black, lines of text | the cycling background, centred titles (size 2.23, y 0.08), usage at 0.91, `RM_Camera`, fades 0.7 / 1.5 s ([Rumble screens](#rumble-screens)) |
| [ ] | Game Mode | a list of titles | three-row `ScrollingMenu` of title and description, arrows, no wrap |
| [ ] | Game Type | text entries | centred one-row grid, the P1 / P2 / CPU badges by language, the blinking `0x77` |
| [ ] | Choose Gangs | text, wrapping cursors | name boxes, badges, arrows, the 3D fighters, no wrap, no shared gang, back → Game Type |
| [ ] | Choose Area | the arena's label | framed preview pictures (level record `+0x80`) in rows of 3, label `+0x49` |
| [ ] | No 2nd Controller | not made | text `0x3d`, waits for a second pad |
| [x] | Input timing | repeat, 110 ms gap, release-to-accept | nothing for the PM screens; the `ScrollingMenu`'s 100 ms gap and end-stop |

## Coney's implementation

**The start-up path** (`src/gamemodes/start_up_flow.h`, `StartUpFlow`), written from [the flow](#mode-flow) and
[Boot](boot.md#main): `coney --disc <disc>` with no tool option makes the script system (the Lua state), then does what
`main` does from the movies on: the three start-up movies (skipped, below), push mode 8, ask for the memory-card boot
check, push mode 6, push mode 5. Mode 5 runs first; on the NTSC-U disc the main menu is reached as follows (frames of
the 1/30 s step): legal screen frames 0-149, mode 6 frame 150, mode 8 frame 151 (it pushes 0x12), PM_Greet from frame
152, PM_Mode on the frame START is pressed.

**Mode 5, the legal screen** (`src/gamemodes/legal_screen_mode.h`, `LegalScreenMode`), written from
[the flow](#mode-flow) and [Graphics](graphics.md#first-screen):

- `enter` picks the resource name by language, 16:9 and the flag `0x02` (`legalScreenResourceName`; the defaults
  are NTSC-U English 4:3, `legal_screen`), loads that sprite sheet from the WAD file named by the name's decimal CRC
  (`resourceFileName`, `platform::loadSpriteSheetResource`) and keeps it until `exit`.
- `update` draws the sheet's first rectangle on black, centred and 1.068 × 1.011 the size of the 640 × 448 logical
  screen as the original sizes it ([Placement](graphics.md#first-screen)), and presents it, every frame; it reads no
  input. It leaves once 5,000 ms of **game time** have passed since the mode was entered: exactly
  150 frames of the fixed 1/30 s step.
- A sheet that fails to load is printed and the screen stays black for the hold.

Coney's choices, where the original does something else or the page is silent:

- The original draws once in `Enter` into both display buffers and presents nothing in `Update`; Coney redraws every
  frame (the same picture), so a window that is moved, resized or captured keeps showing it.
- The original times the hold in real milliseconds (`Timer`); Coney uses game time on the fixed step, so the engine
  never reads a clock and a test can run the whole hold.
- `legal_screen_euro` is used for English with the flag `0x02` whatever the 16:9 option, since no `_w` variant of it
  exists.
- `enter` runs the preload scripts first (`enum_preload.lua`, then `config_preload.lua`, `config_preload2.lua` and
  `config_preload3.lua`, in the one Lua state, [Scripts](scripting.md#coneys-implementation)); they fill the UI strings
  and the level table. The page does not say where in `Enter` the original runs them; Coney runs them before the
  picture loads, and since Coney's reads are synchronous they take no frame.

**The pads** (`src/core/pad.h`, `Pad`; `src/core/pads.h`, `Pads`), written from [the pad record](#pad-record),
[the queries](#pad-queries) and [the input section](#input):

- `Pad` is one record: the 16-bit button word with the `libpad` bit layout (`coney::pad::kCross` and so on), a ring of
  the last 8 words, the four hold counters (1 to 15, then 15 → 12), the diagonal rule, the sticks in [-1, 1] through
  the 95..160 dead zone with y up, the raw stick and pressure bytes, and the queries: `buttons(n)`, `held`,
  `pressed`, `released`, `pressedWithRepeat` (the auto-repeat of `0x00144ad0`) and the masked forms.
- `Pads` holds the 8 records and updates records 0 and 4 from ports 1 and 2 (`Pads_Update`).
- The input comes from an `InputSource`, asked once per frame for both ports' raw samples (`PadSample`: connected,
  button word, the four stick bytes, the twelve pressure bytes). `GameModeStack::runUntilEmpty` updates the records
  after the window's events and before the top mode's update; a mode reads them with `stack.pads().port(0)`. The
  legal screen reads none, so its behaviour is unchanged.
- Sources: `platform::SdlInput` (`src/platform/sdl_input.h`), SDL3 gamepads and the keyboard; and
  `ScriptedInput` (`src/core/input_script.h`), which plays an input script (`--input-script FILE`, format and
  controls in [Building and testing](../guides/building.md#controls)) for tests and headless runs.

Coney's choices for the pads, where the original does something else or the page is silent:

- The original's modes call `Pads_Update` themselves, each in its own place; Coney's main loop updates the pads once
  for every frame, whatever the mode. The original's 6 ms rule (repeat the previous word when the last real read is
  too recent) never applies, since Coney reads once per 1/30 s step.
- A disconnected pad: the original stops its update after the read; Coney stores an empty word, clears the hold
  counters and centres the sticks, so a pad pulled out mid-hold neither stays held nor keeps auto-repeating.
- The diagonal rule with two directions at equal pressure keeps the first in pressure-byte order (right, left, up,
  down). Digital inputs (keys, SDL buttons, scripts) report full pressure (255), so ties are the rule on PC: with the
  arrow keys, up and right gives right, down and left gives left.
- SDL's triggers are axes: their travel becomes the L2 and R2 pressure bytes, and the button counts as held from a
  quarter of the travel. SDL's face buttons are positional (south is cross on any gamepad). The first gamepad plays
  on port 1, the second on port 2; the keyboard always plays on port 1, which is therefore always connected in a
  windowed run. A headless run without a script has no input source: every record stays disconnected.
- Left out for now: the camera-turned left stick (`+0x00`, in game only), the Lua pad handlers (`PadSetHandler`, a stub
  binding for now), vibration, the owning player (`+0x42`) and the sample time (`+0x48`).

**Mode 6, the memory-card check** (`src/gamemodes/memory_card_mode.h`, `MemoryCardMode`): `setBootCheck` is
`0x0015a270(1)`; `exit` is the original's (`0x0015c2c0`): the boot flag becomes 2 and, because the mode below is the
level flow, its "load the front end on resume" (`+0x28`) is cleared. **Coney's choice:** Coney has no memory card (its
saves will be files, and none exist yet), so `enter` and `update` are a pass-through on the "no saved profile" path:
one black frame, then the mode leaves. No message box, no "checking" message (string `0xb5`) and no card dialogs; what
the original shows with no card is an [open question](#open-questions).

**Mode 8, the level flow** (`src/gamemodes/level_flow_mode.h`, `LevelFlowMode`): the three fields of
[Mode 8 fields](#mode-8-fields); `enter` sets `+0x28` and calls `resume`, which starts the front end when `+0x28` is set
and no level is chosen (`LevelFlow_StartFrontEnd`): select level 0 (record 0 of the level table, `level100`), run the
level's scripts (`global.lua`, then `level100.lua`), play `menu`, call `Menu.onStart` (which shows the menus through
`ShowProfileManager`), mark the front end loaded. `MenuLoadLevel(name)` chooses a level by name in the level table
(`+0x20`); the next `update` finishes the front end when it is loaded (`Menu.onFinish`, then the unload, which makes a
fresh Lua state), selects the level (`W_GameState + 0x56dc`) and pushes gameplay, mode 1
([Level loading](level-loading.md#coneys-implementation)). `chooseLevelIndex` chooses by index, for the
mission-complete mode's kinds 2 and 3. Coney's stand-ins, each because the research or the subsystem is not there yet:

- The front end's `InitLevel` is only its script step: nothing else of `level100` is loaded, and the background is
  black where the Wonder Wheel scene would be.
- A flow made without a level loader (the tests without a disc) has no gameplay: after finishing the front end,
  `update` logs `level start requested: <level>` and starts the front end again, so the player is back on the menus.
- Without a script system (no disc), or when `Menu.onStart` did not push the menus, the level flow calls
  `ShowProfileManager("Menu.fadeToRMI", "Menu.startGame")` itself and logs it, so the menus always come up.
- `update` clears to black, runs the scripts' frame and presents.

**Mode 0xb, the mission-complete mode** (`src/gamemodes/mission_complete_mode.h`, `MissionCompleteMode`), from
[Starting a story game](#story-start) and [Boot](boot.md#one-frame): `HUDLaunchMissionComplete(kind)` (a real binding
now) calls `launch`, which stores the kind and pushes the mode unless it is on top (`MissionComplete_Launch`); `enter`
calls the Lua function `UnlockAndLoad`; `update` runs the scripts' frame and, once a kind is set, pops itself and acts
on it (1: the checkpoint back to 1; 2: the current level chosen again; 3: the next record), then pops gameplay too when
it is the new top. So STORY goes as in the original: the profile manager's exit calls `Menu.startGame`, whose
`runNextMission(1)` sets checkpoint 1, chooses `level99` and pushes mode 0xb over mode 8; the next frame its enter runs
`UnlockAndLoad` (the same choice again; already on top, so no second push) and its update pops it; the frame after,
mode 8 finishes the front end and pushes mode 1, which loads `level99` with Rembrandt where its script creates him.
Coney's choices: the kind is stored on every launch, on top or not, as in the original; no mission-complete screen,
save-system call, inventories or autosave (Coney has no saves); the frame is black.

**Mode 0x12, the profile manager** (`src/gamemodes/profile_manager_mode.h`, `ProfileManagerMode`): `show` is
`ShowProfileManager` (`0x001552b0`: keep the two callbacks, push unless on top); `enter` plays `menu` unless it is
playing and loads `menu_system` (a batch of 50 sprites at depth 8,500); `update` runs the controller with the HUD
player's pad (port 1) and the frame's game time, then the 2D pass and the present; once a screen sets the done flag
(`0x0050f5b0`) it fades out over 1.0 s ([Fades](#fades)) and leaves when the fade has run; `exit` creates the new
profile when PM_Subtitles asked for it (`0x0050f598`, with the name, difficulty, brightness and subtitles chosen), stops
the controller and calls `Menu.startGame` when the menus finished. Coney's choices: the controller starts at
the top of the first update instead of in `enter` (same step), so PM_Greet's blink is timed from that frame; the two
fonts are loaded here (`part_page0` for slot 2, `big_font` for slot 6, depth 9,000); the screen is cleared to black (no
world); each change of screen is logged (`profile manager: PM_Greet`), which is what a headless run shows. Each frame
also advances the screen fade (`ScreenQueueEffect`, `src/graphics/screen_fade.h`: a black quad drawn over the menus)
and runs the scripts' frame (the scheduled calls, such as `Menu.launchRMI` 500 ms after `Menu.fadeToRMI`). The Lua
callbacks reach the script system. Coney's choices for the fade (the page gives the fields' roles only): its level
runs linearly from 1 (black) to 0 for a fade in and from 0 to 1 for a fade out, over the given time of game time; a new
fade replaces a running one. The 16:9 choice is not applied (no device setting yet), and no "format the card?"
dialog follows a new profile: Coney has no memory card.

**The profile manager's screens** (`src/gui/profile_management_gui/`): `PmController` builds all fourteen screens and
the [transition table](#profile-manager) on the screen flow ([GUI](gui.md#coneys-implementation)) and starts at
PM_Greet. The menus end when a screen sets the done flag (`PmSession`, the controller's globals `0x0050f584`-
`0x0050f5c0` and the name `0x0063f1d8`, reset at start). PM_Extras and the two Xbox screens are still Coney's
`PmPlaceholder` (it shows the screen's name and goes back on the back command).

- **PM_Greet** (`PmGreet`): the logo (`menu_system` rectangle 0, keeping its shape) and global string `0x76` centred
  under it; the text's alpha ramps 0 → 255 → 0 in 1,500 ms halves from the screen's entry; START (the auto-repeating
  query) returns 0, which leads to PM_Mode, and plays cue 9; a screen fade in progress keeps the text lit and restarts
  the idle time (the pad does not); 70,000 ms without a fade call `Menu.playMovie(2)`, and the wait starts again.
- **PM_Mode** (`PmMode`): an option grid of `0x78` (code 0), `0x8a` (code 5, left out with the flag `0x02`) and `0x79`
  (code 1), the first selected and drawn at 1.15 times the size, and the usage line `0x1f` below. The command handler
  is the original's: no input while the fade level is not 0; back returns 8 with cue `0xf`; accept plays cue 9, then
  story returns 0 (6 with two or more pads connected), extras 5, code 7 calls `Menu.reloadProfiles`, and quick rumble
  calls the first callback (`Menu.fadeToRMI`) and stays.
- **The story screens** (`pm_profile_screens.h`: PM_NumPlayers, PM_Profile, PM_Load, PM_Continue, PM_Delete;
  `pm_new_game_screens.h`: PM_Create with its `NameKeyboard`, PM_Difficulty, PM_Light, PM_Subtitles), written from
  [the screens](#pm-screens) and [the geometry](#pm-layout): each screen's logic (`handle`, over `PmChoices`, the grid's
  rows and selection) is apart from its look (its `open` and `draw`, with the shared placements and colours in
  `pm_look.h`), so the look can move to the shared widgets without touching the logic. They write the game state (the
  two-player flag, `+0x43c`, `+0x57a4`, `+0x438`) and ask the save system through `ProfileStore`
  (`src/warriors/profile_store.h`). **Coney's stand-ins:** the profile record is `Profile` (name, difficulty,
  brightness, subtitles, damaged) until the save research lands; `SessionProfileStore` keeps profiles in memory for
  the run only, so every run starts with no profile (PM_Profile offers create and reload) and the fourth difficulty is
  locked. Coney's choices where the page is silent: a grid move that cannot leave its item never wraps; the keyboard's
  left and right stay in their row and skip blank cells, up and down keep the column (then the nearest selectable cell
  to its left), DEL is drawn after OK's word; PM_Light steps on the menu commands' left and right and plays nothing past
  either end, and its brightness is only stored; PM_Create's "name used" text shows until the screen is left.
- Coney's choices: the layout (every position and size: `PmLayout`; the ten layout floats above are not used yet);
  the logo is rectangle 0 (it is the game's logo, from viewing the sheet); "a fade in progress" for PM_Greet is a fade
  running or a screen not fully clear; unselected items are grey (160), the selected one white. **Quick rumble**:
  `Menu.fadeToRMI` fades out and
  schedules `Menu.launchRMI`, whose `ShowRumbleModeInterface` opens the Rumble menu (below).

**Mode 0x11, the Rumble menu** (`src/gamemodes/rumble_menu_mode.h`, `RumbleMenuMode`), from
[QUICK RUMBLE](#quick-rumble): `show` keeps the two callbacks and pushes the mode unless it is on top; `enter` loads
the fonts and starts the screens; `exit` calls `Menu.cancelRumbleMode` when cancelled, or
`Menu.startRumbleMode(level)` when started, whose level request (mode 8) loads the arena and pushes gameplay. On
start the profile manager under it is popped too, so mode 8 is on top for the level request (inferred from the chain).

**The screens** (`src/gui/rumble_mode_gui/rumble_menu.h`, `RumbleMenu`), from [The Rumble set-up](#rumble-setup) and
[Where the Rumble data lives](#rumble-data): each screen with a list builds it when it opens by running its chunk
from the disc (`rumble_data.lua`, `rumble_gang.lua`, `rumble_arena.lua`), whose `CfgRumbleGame`, `CfgRumbleGang` and
`CfgRumbleArena` calls (`src/scripting/rumble_bindings.h`) add the records in `src/gui/rumble_mode_gui/rumble_data.h`
when they are unlocked; `CfgRumbleChar` keeps the characters' descriptions. The text shown is the chunks' (titles,
descriptions, gang names) and the HUD strings' (the Game Type entries `0x34`-`0x36`, message `0x77`). Each confirm
writes what the tables above give it: Game Mode the mode's id (index 1), gang size (2), its `:`-prefixed title, its
three player options and, for a mode with presets, all nine types of each side; Game Type the players (0: 3, 2 or 1),
with 255 in both packs for a preset mode, which skips the gang screen; Choose Gangs, once both sides are locked, the
packs (3, 4), the nine types of each side (5-13, 14-22) and the names (`src/gui/rumble_mode_gui/rumble_gang_chooser.h`:
up and down move the active side's cursor, left and right rotate its roster except in co-op, accept locks the side,
back unlocks); Choose Area marks the arena on confirm and launches it on the next update, writing the level number
from the arena's level record. Sounds 8 and `0xf` are logged as cues. Backing out of the first screen leaves
"cancelled" only when the menu was opened from the front end. `GetRumbleModeGangName`
(`src/scripting/level_bindings.h`) returns the names, empty until the gangs are confirmed. Accepting every first entry
gives the default set-up read at run time (checked by a test over synthetic chunks with the disc's ids and rosters,
and by the disc test); `--play-level` of an arena computes the same set-up by running the chunks
(`rumbleMenuDefaults`), with that arena's level number (`rumbleArenaOf`).

**The unlocks** (`src/warriors/unlockables.h`, `Unlockables`, kept in the game state): Coney has no unlockables manager
or saves yet, so the check answers from a set that a fresh profile fills with what a fresh boot shows: modes 12 and 14,
arena 102, gangs 5 and 3, and no character type (so every type in the stand-in table is replaced, as the runtime
defaults show). Which records the story unlocks: [Unlockables](../references/unlockables.md); every mode, arena, gang
and character: [Rumble roster](../references/rumble.md).

Coney's choices: the screens' titles are their names on this page; an arena's label is its level record's fifth
`CfgLevelName` argument (`level102`'s is "Fight Pen"), since the page does not say where the Choose Area screen's text
comes from; side 2's cursor starts on the gang list's second entry and side 1's on its first (the fresh boot's
pairing), and the cursors wrap; rotating left makes the second member the warchief; a two-player confirm with fewer
than two pads shows message `0x77` again instead of screen 4 (not researched); the HUD player's pad drives both sides;
back from a later screen goes to the one before with its first entry selected (the Game Mode screen keeps the mode
chosen); the layout is PM_Mode's style on black (no front-end world yet), with the selected mode's description, or
the gang screen's two sides, as lines under the list. The "vs" title is not a menu screen in Coney: it is read as the
arena's intro, `ShowRumbleModeIntro`, which `DoRules` calls after the menu has popped (inferred from the chain); Coney
does not have that binding yet. Each change of screen is logged (`rumble menu: Game Mode`).

**Menu commands** (`src/gui/menu_input.h`, `MenuInput`, `MenuInput_Dispatch` `0x001e95c0`), from [Input](#input): up,
down, left and right from the auto-repeating d-pad query or the left stick past ±0.5; accept on the release of cross;
back on the release of triangle or circle. A command is accepted only when more than 110 ms have passed since the last
one, and a stick held the same way waits 400 ms until it returns to neutral. Taking focus forgets the last command and
blocks the d-pad for 20 ms. Coney's reading: at most one command a frame, in the order d-pad, stick, accept, back; a
command refused by the gap is dropped (a release too soon after a move is lost); the stick's up or down wins over left
or right.

**The front end's other requests** (`src/gamemodes/front_end_services.h`, `FrontEndServices`): music and sound cues
are recorded, logged and not played; Lua calls are logged and handed to the script system; movies (`LOGO`, `PLOGO`,
`L1_IN` at start-up, the attract movie, a level's intro) go to the `MoviePlayer` attached to it
(`src/gamemodes/movie_player.h`, `Movie_Play`). **Coney's choice for movies:** no player is attached yet (Coney has no
video decoder), so each movie is skipped as if it had ended at once; the original blocks until it ends.

**Disc check (NTSC-U, 2026-10-04, states only):** `coney_tests "[disc][frontend]"` with `CONEY_DISC` set runs the
start-up path headless with the disc's sheets and the game's own scripts: PM_Greet is on top by frame 160 with every
sheet loaded, and START on frame 200 reaches PM_Mode with three items and cue 9; quick rumble (chosen with the analog
stick) opens the Rumble menu at its Game Mode screen, and triangle there fades back to PM_Mode; story with a new
profile (PM_Profile, PM_Create with the disc's 47-cell keyboard in rows of 12, 12, 12 and 11, PM_Difficulty, PM_Light,
PM_Subtitles), after the fade out, creates the profile and calls `Menu.startGame`, which asks for a level
(`runNextMission(1)`) and launches the
mission-complete mode, whose `UnlockAndLoad` asks again (two requests, two launches), and, with no level loader in this
test, the front end comes back at PM_Greet in a second Lua state. 111 level records, no script error and no call of a
missing binding in either state. With `CONEY_DISC` set when CMake configures, the smoke test `coney.reaches_main_menu`
runs `coney --disc` the same way.

**Disc check (NTSC-U, 2026-10-05, states only):** `coney_tests "[disc][story]"` runs STORY with the play mode as the
level loader, through the new-profile screens (`tests/support/story_new_profile.txt`, since 2026-10-06): gameplay is
on top by frame 370 with `level99` loaded, one profile, `L99_IN` asked for once, the checkpoint is 1, Rembrandt stands at
(-284.4, 120.4) on the ground (z 0.25, the height seen at run time) and not airborne; the stick then moves him
(6.12 m in 200 frames at 35 % and a 30/65 diagonal) and he stands again on release. No script error; the level's
scripts call 29 bindings Coney lacks (177 calls skipped). `coney --disc` with STORY chosen does the same in a window.

**The Lua 4.0 virtual machine** (`src/scripting/`), Coney's own implementation of the public Lua 4.0 language,
so the game's precompiled scripts run unchanged:

- `parseLuaChunk` (`lua_chunk.h`) reads a precompiled chunk: the header (`ESC "Lua"`, version `0x40`, then the
  compiling machine's layout), the test number, and the main function with its constants, nested functions and code.
  It accepts only the layout every script on the disc has: little-endian, 4-byte int, size_t and instruction,
  6-bit opcodes and 9-bit B operands, 8-byte numbers; debug information is skipped. Checked on the disc (data,
  inferred): the five `config_strings_*.lua`, `config_preload2.lua` and `enum_preload.lua` parse to their last byte.
- `LuaVm` (`lua_vm.h`) interprets all 49 Lua 4.0 instructions over nil, numbers, strings, tables and functions (native
  bindings or Lua closures with Lua 4.0's copied upvalues). Coney's choices and limits: no tag methods, so indexing a
  non-table, arithmetic on a non-number and ordering mixed types fail with an error; tables iterate in the order keys
  were first set (Lua's hash order is unspecified); no garbage collector (reference counting only); strings compare by
  byte. Numeric strings are converted in arithmetic, and `SETLIST` stores in blocks of 62, as this build does
  ([Scripts](scripting.md#notes-for-implementers)). Malformed bytecode (an unknown opcode, an operand out of range,
  stack underflow) fails instead of crashing, and an instruction budget and a call-depth limit stop a runaway script
  deterministically. An option turns calls of nil into counted no-ops, so a script runs past bindings Coney does not
  have yet.
- The string load that uses it is on [GUI](gui.md#coneys-implementation).

TODO for the analysts, found while implementing:

- **The level script entry and `Menu.onStart`** (answered, 2026-10-04): the preloads (four scripts, at the legal
  screen), then `global.lua`, then `level100.lua`, all in one Lua state; what each needs is on
  [Scripts](scripting.md#notes-for-implementers) (also: table constructors flush every **62** items, not 64).
  Originally: which scripts run before `level100.lua` (script system slot `+0x24`,
  [Level loading](level-loading.md#open-questions)), and which globals and binding results `level100.lua`,
  `config_preload3.lua` and `global.lua` expect. In Coney's VM, with every unknown binding a no-op returning nothing,
  `level100.lua` fails on its first instructions indexing a nil global, `config_preload3.lua` on instruction 18
  likewise, and `global.lua` compares nil with a number (a binding that should return a number). Done: the script
  system runs them ([Scripts](scripting.md#coneys-implementation)).
- **The ten layout floats** (answered, with the rectangle and the anchoring: [Screen geometry](#pm-layout) and
  [The screens](#pm-screens): rectangle 0, tinted red, its **left edge** at x, and x is the left of the safe area, not
  the centre) at `0x0050f5c4`-`0x0050f5e8` (`0x00203b98`): their values per video mode and which
  widget each places (PM_Greet's logo and text, PM_Mode's grid and usage line); also which `menu_system` rectangle
  PM_Greet's sprite widget shows (Coney: 0, the logo) and how its rectangle is anchored (Coney: the centre).
- **PM_Mode** (answered, [above](#profile-manager), and implemented): back returns 8 with cue `0xf`; code 1 calls
  `Menu.fadeToRMI` and stays. The item colours, the rows and the spacing are answered too
  ([The screens](#pm-screens)): two rows, red and grey 178, all at size 1.15, rows 0.0505 apart on screen.
- **PM_Greet's idle timer** (answered, [above](#profile-manager), and implemented): the fields are the screen-effects
  manager's fade state, not input, and the timer restarts after `Menu.playMovie(2)`. Originally: what the HUD fields
  `0x005fdeb8 + 0x1d4` and `+0x1d8` record (Coney: any button or stick),
  and whether the timer restarts after `Menu.playMovie(2)`.
- **`MenuInput_Dispatch`:** whether more than one command can fire in a frame, and the order of the d-pad, stick and
  button passes when several are active.
- **The unwind** (`0x001c81e8`): does it call `Exit` on screens that are already exited (covered), or only on the top?
  Coney calls `exit` only on an entered screen.
- Names: the `@orig` tags call the mode 6, 8 and 0x12 functions `Mode6::Exit`, `MemoryCard_SetBootCheck`,
  `Mode8::Enter`, `Mode8::Resume`, `LevelFlow_StartFrontEnd`, `Mode12::Enter`/`Update`/`Exit`, and the screen
  functions `PM_Controller_Start`/`Update`/`Stop`, `PM_Greet_Enter`/`Update`/`Exit` (the flow-state slots),
  `PM_Greet::Init`/`Update`/`Render`, `PM_Mode::Init`/`Update`, until the research database names them.
- Names for the mode 5 functions: the `@orig` tags call them `Mode5::Enter` (`0x00159a58`), `Mode5::Update`
  (`0x00159ae0`) and `Mode5::Exit` (`0x00159ab8`) until the research database names them.
- Does `legal_screen_euro` have a 16:9 counterpart in another region's build, or does the flag `0x02` with 16:9 pick
  `legal_screen_w`?
- Names and file for the pad functions (`0x00144a08`-`0x00144bf0`, `0x00144fb0`, `0x001454a8`): the tags use our
  names (above) and `(unknown)`. What is the second argument of `0x00144a80`, `0x00144a30` and `0x00144ad0`, passed
  as 0 by every caller on this page?
- `Pad_Update` order: are the hold counters counted from the word before the diagonal rule (as step 3 before step 4
  reads, and as Coney does) or after it? Before means a direction the rule drops still auto-repeats.
- The diagonal rule with equal pressures: which direction wins (strict or non-strict comparison, and in which
  order)?
- A disconnected pad: what does `Pad_Update` leave in the new ring slot, the hold counters and the sticks?

What the implementer still needs:

- Mode 6's real card check once Coney has saves, and the autosave the mission-complete mode asks for.
- PM_Extras, the profile record and Coney's saves for PM_Load / PM_Delete, and the message box mode 6 uses
  ([GUI](gui.md#open-questions)).
- The Rumble menu's other entries: the mode list's and the gang records' addresses (so their names and values can be
  read from the player's executable), the other gangs and arenas, the warchief choice, the screens' layout, titles,
  usage lines and sounds, and `ShowRumbleModeIntro`.
- The bindings that are stubs today (cameras, scenes, particles, sound, `PadSetHandler`), each with its subsystem;
  the list is the binding table in `src/scripting/script_bindings.cpp` ([Scripts](scripting.md#coneys-implementation)).
- `InitLevel` far enough to load `level100.lev`, its world and its dependency list.

## Open questions

- **Mode 6 at boot** (answered for an unformatted card: no dialog, see [the flow](#mode-flow)): still open with no
  card, a formatted card without a save and a card with a save.
- **Rumble mode** (answered for the entry, the way to the arena, the screens, the 23 values, the data's source, WAR
  PARTY and the "vs" title, [Where the Rumble data lives](#rumble-data)). The Game Type entries `0x35` / `0x36` read
  "Co-op" and "Vs." and message `0x77` asks player 2 to press START (confirmed (runtime): the English
  `config_strings` run in Coney's script system). Screen 4 and the Choose Area labels are answered in [The Rumble
  menu's screens](#rumble-screens).
- **Global string ids** (answered for the front end: `GSTRING.HUD` entries are set with explicit indices, so the
  disassembly gives each id's text; the texts are quoted above). Originally: the text behind `0x76`, `0x78`, `0x79`,
  `0x8a`, `0x1f` and the memory-card ids needs a
  decoder for `config_strings_*.lua` (the entries' order in the `GSTRING.HUD` table gives the id; a string that
  occurs twice is stored once among the constants, so the constants alone do not give the order).
- **`InitLevel` details** (answered for the scripts: `global.lua`, then `<level>.lua`,
  [Scripts](scripting.md#life-of-the-lua-state)): the meaning of the remaining level record fields (the ones known are
  on [Level loading](level-loading.md#the-level-record)).
- **The new-game screens** (answered, [The screens](#pm-screens)): they set the controller's done flag
  `0x0050f5b0`; `0x00204008` reads `0x0050f5b4`, "a new game was started", which `PM_Subtitles` sets.
- **A second launch while mode 0xb is on top** (answered): `MissionComplete_Launch` (`0x0015d420`) stores the kind
  at `0x005e5e1c` on every call, pushing mode 0xb only when it is not on top, and sets the level-change flag
  `0x0050c754` only when it pushes with kind 0 (confirmed (code)). Coney's choice matches. The launch's own kind never
  survives anyway: mode 0xb's `Enter` sets it to 0 before calling `UnlockAndLoad`, and `UnlockAndLoad` always ends
  with `runNextMission(1)`, which launches 4 (confirmed (code) for `Enter`; the Lua side inferred). On the disc the
  only kinds passed are 0 (no argument: 11 calls in ten scripts, and the C++ caller `0x00158c4c`) and 4
  (`runNextMission`); nothing passes 1, 2 or 3 (inferred from the disassembly of every script), so the kinds `Update`
  acts on are unused by the shipped scripts unless a script launches during mode 0xb's frames.
- **Mode 8's `+0x28` after a level** (answered, confirmed (code)): mode 8's `Resume` (`0x0015c6f8`) loads the front
  end when `+0x28` is set and no level is chosen (`+0x20` = -1), then **sets `+0x28` to 1 again every time**. Mode 6's
  `Exit` (`0x0015c2c0`) and mode 0xb's `Update` (`0x0015d160`) clear it when mode 8 is directly below them, so their
  own pop never reloads the front end; the autosave's mode 6 does clear it (after a mission, mode 0xb has popped
  itself and mode 1, so mode 8 is below), but the next `Resume` sets it again. So a mode 1 popped with no level chosen
  does bring the front end back, as Coney does. **Quitting a mission to the menus:** the in-game menus of modes 0xc
  and 0x14 (`0x00155408`, the failure menu, and `0x00155648`; roles inferred), on their quit choice, call
  `MenuLoadLevel("menu")` (checkpoint 1, `W_GameState + 0x14c` = 3), and no level record is named `menu`, so mode
  8's `+0x20` becomes -1; mode 1's own `Update` then returns 0 for the state 3 and the loop pops it, its `Exit`
  unloads the level, and mode 8's `Resume` reloads the front end (inferred that no record is named `menu`, from the
  level table; the rest confirmed (code)). The binding `Quit` (`0x00160d38`: level index 0, state 3, checkpoint 1)
  would end the same way, but no script calls it.
- **The device flag `0x02`** hides `PM_Extras` and selects other layouts; it is still unidentified (see
  [Graphics](graphics.md#open-questions)).
- **Script system slots** (answered: [Scripts](scripting.md#vtable-slots); update is `+0x14`, its adjust word
  `+0x10`, and `+0xdc` does nothing). Originally named by their use only: `+0x44` run a script file, `+0x4c` find a
  function
  by name, `+0x6c` push a number, `+0x8c` call, `+0xcc` keep a reference to a function by name, `+0xdc` a mode switch
  around preload scripts, `+0x10` update, `+0x24` level entry. A page on the script system should confirm them.
- **Grid row pitch:** the code gives `6h / 7` (0.0469 at size 1.15), the screen 0.0505; which term is missing?
- **Fade speed:** the frame-time argument of `0x0018ce58`; a 1.0 s fade out went black in about 0.35 s at runtime.
- **Rumble draw order:** how the gang screen's 3D fighters end up over the opaque 2D background (depth 8,000).
- **Sheet-table records 12 and 28** (the Rumble background and sprites): their resource names and rectangles.
- **The `WonderWheel_100` tracks** (answered): a fixed camera, a 20 s loop turning the wheel 45° (2.25° a
  second), the neons shown and hidden by events ([Objects: the Wonder Wheel](objects.md#wonder-wheel)).
- **`0x005147cc`**, set to 10.0 while the Rumble menu is open, and `0x0040c938`.
- **PM_Difficulty's fourth item:** what the save-system query that unlocks it reports.
- **The profile record:** its fields and how `+0xcc(slot)` (load) applies them to the game state (Coney's `Profile`
  stand-in keeps name, difficulty, brightness and subtitles).
- **The level loading screen** ("1 Coney" / "New Blood" with a progress bar): which code draws it and when, so mode 1
  can show it (Coney shows none).
- **The music at PM_Greet:** `menu` or `wonderwheel_132b` (mode 0x12 `Enter` replays `menu` when the names differ;
  [Sound](sound.md)).
- **PM_Light's colour** (answered): the light manager's brightness, added to every ambient and directional light
  ([Lighting](lighting.md#brightness)); 40 is also the constructor's default.
