# Start-up and the front end

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims: PCSX2 was
not running when this page was written. The disc-side checks (2026-10-04) read the NTSC-U disc's WAD and are reported
as names, counts and layouts only.

## Purpose

What the game does between the legal screen and the moment the player can move the cursor on the main menu: which
game modes run, in which order, what each loads, how long it holds and what input moves it on; how pad input reaches
the menus; and which sounds the front end asks for. It is what the milestone after "First pixels" needs.

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
| `0x00161218` | `RunPreloadScripts` | `GameModes/` | runs `enum_preload.lua` and `config_preload.lua` | confirmed (code) |
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
| `+0x28` | "load the front end on the next `Resume`"; set to 1 by `Enter`, cleared by mode 6's `Exit` when mode 8 is the mode below it | confirmed (code) at `0x0015c688`, `0x0015c2c0` |

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

## Behaviour

### From the movies to the main menu {#mode-flow}

`main` pushes modes 8, 6 and 5 in that order and runs the stack ([Boot](boot.md#main)). A push does not enter a mode,
so modes 8 and 6 have not been entered when mode 5 runs first.

1. **Mode 5, the legal screen** (5,000 ms). See [Graphics](graphics.md#first-screen) for the drawing. `Enter` also runs
   two scripts through the script system (`0x00161218`, slot `+0x44` with `enum_preload.lua` and
   `config_preload.lua`, wrapped in slot `+0xdc(1)` / `+0xdc(0)`). `Update` (`0x00159ae0`) switches `GameTimer` to
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
   (a card that is missing, unformatted or full opens further dialogs: `0x0015b918` with strings `0xb7`-`0xc1`),
   reads the pads, checks for a removed controller, clears to black and draws the HUD and the box. It leaves when the
   save system and the box are done. `Exit` (`0x0015c2c0`) frees the buffer, sets the boot flag to 2 and, because the
   mode below is 8, clears mode 8's `+0x28`. Confirmed (code) for the calls; what each dialog says is in the string
   table and not traced here.
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

Besides the codes, a screen can return "stay" (`-0x100`) or "back" (`-0xff`, pop to the previous screen). The flow
ends, and the controller reports done, when its stack is empty. Starting the controller (`0x00204a78`) resets its
globals (`0x0050f584`-`0x0050f5c0`, the first Lua callback in `0x0050f584`), picks per-video-mode layout constants
(`0x00203b98`: ten floats at `0x0050f5c4`-`0x0050f5e8`, by the device flags `0x02`, `0x04` and `0x20`) and enters
`PM_Greet`. The menu sprites come from the particle page **`menu_system`** (`0x00204c50`: particle page header entry
3, a resource instance at depth 8,500 with room for 50 sprites; see [GUI](gui.md#resource-instances)).

**`PM_Greet`** (`GUI/ProfileManagementGUI/PM_Greet.cpp`), confirmed (code):

- `Init` (`0x002079a0`): a sprite widget (`BaseWidget`) with the `menu_system` page, and a text widget
  (`TextWidget`) holding global string `0x76`, placed by the layout constants; the text blinks with a period of
  1,500 ms (`+0x98`).
- `Update` (`0x00207e28`): while the profile manager is not finishing, the text's alpha ramps 0 → 255 and 255 → 0 in
  alternate 1,500 ms halves (`0x00337568`, a linear interpolation). If START is pressed on the HUD player's pad, the
  result is 0 (→ `PM_Mode`) and the front-end sound cue **9** plays (`0x0010fc30`). Any activity on the HUD
  (`0x005fdeb8 + 0x1d4`, `+0x1d8`) restarts an idle timer; after **70,000 ms** idle the screen calls the Lua function
  **`Menu.playMovie(2)`** (an attract movie; which of the script's two movies, `TRAILER` or `L1_IN`, index 2 means is
  inferred to be the second, `L1_IN`).
- Render (`0x00208288`): draws the text and the sprite while the screen is active and visible.

**`PM_Mode`** (the main menu), confirmed (code) at `0x00209da8`: an `OptionGrid` with three items, global strings
`0x78` (code 0), `0x8a` (code 5, `PM_Extras`; left out when the device flag `0x02` is set) and `0x79` (code 1), the
first selected, scaled 1.15, plus a usage line (string `0x1f`). Code 1 has no transition in the flow, so choosing it
ends the profile manager (inferred); with the Lua script's `Menu.fadeToRMI` and `ShowRumbleModeInterface` it is
probably the Rumble mode entry (speculative). The item labels are in the string table; `level100main.lua` (below)
names the same choices "Story" and "Quick Rumble", which suggests `0x78` and `0x79` (speculative).

### The front-end scripts

The front end's behaviour beyond the C++ screens lives in Lua 4.0 bytecode on the disc. What the code and the
scripts' string constants show (inferred, from the constants; the bytecode was not disassembled):

- `level100.lua` defines the table `Menu` with `onStart`, `onFinish`, `fadeToRMI`, `launchRMI`, `startRumbleMode`,
  `cancelRumbleMode`, `fadeIn`, `startGame`, `stopScene`, `startScene`, profile reload and delete helpers, and
  `playMovie` with the movies `TRAILER` and `L1_IN`. It drives the Wonder Wheel cinematic (`WonderWheel_100`, objects
  `dyn_s_wwcart_simple_*`, `dyn_s_wwheel_a`, `dyn_s_neon_*`) and the music `music/wonderwheel_132b`, and uses the
  bindings `ShowProfileManager`, `ShowRumbleModeInterface`, `ScreenQueueEffect`, `ScheduleFunc`, `GetPTank`,
  `ReleasePTank`, `SoundLoopMusicTrack`, `SoundStopMusicTrack`, `SetCheckPoint`, `MenuLoadLevel`, `PlayMovie` and
  the `SSMC_*` save-sequence functions.
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
| `<SOUND name>` markup | any text widget, played once on first display ([GUI](gui.md#markup)) | confirmed (code) |
| sound bank `menu.msb` / `menu.msd` | WAD entries 9,799 and 9,800 | inferred (names) |
| `vags/interface/menu/*` | `level100main.lua` only (not the retail menu) | inferred |

## Coney's implementation

**Mode 5, the legal screen** (`src/gamemodes/legal_screen_mode.h`, `LegalScreenMode`), written from
[the flow](#mode-flow) and [Graphics](graphics.md#first-screen). `coney --disc <disc>` with no tool option pushes an
idle mode (standing in for mode 8) and then mode 5, as `main` does; mode 5 runs first:

- `enter` picks the resource name by language, 16:9 and the flag `0x02` (`legalScreenResourceName`; the defaults
  are NTSC-U English 4:3, `legal_screen`), loads that sprite sheet from the WAD file named by the name's decimal CRC
  (`resourceFileName`, `platform::loadSpriteSheetResource`) and keeps it until `exit`.
- `update` draws the sheet's first rectangle over the whole 640 × 448 logical screen on black and presents it, every
  frame; it reads no input. It leaves once 5,000 ms of **game time** have passed since the mode was entered: exactly
  150 frames of the fixed 1/30 s step.
- A sheet that fails to load is printed and the screen stays black for the hold.

Coney's choices, where the original does something else or the page is silent:

- The original draws once in `Enter` into both display buffers and presents nothing in `Update`; Coney redraws every
  frame (the same picture), so a window that is moved, resized or captured keeps showing it.
- The original times the hold in real milliseconds (`Timer`); Coney uses game time on the fixed step, so the engine
  never reads a clock and a test can run the whole hold.
- `legal_screen_euro` is used for English with the flag `0x02` whatever the 16:9 option, since no `_w` variant of it
  exists.
- The preload scripts `Enter` runs (`enum_preload.lua`, `config_preload.lua`) wait for a Lua system.

TODO for the analysts, found while implementing:

- Names for the mode 5 functions: the `@orig` tags call them `Mode5::Enter` (`0x00159a58`), `Mode5::Update`
  (`0x00159ae0`) and `Mode5::Exit` (`0x00159ab8`) until the research database names them.
- Does `legal_screen_euro` have a 16:9 counterpart in another region's build, or does the flag `0x02` with 16:9 pick
  `legal_screen_w`?

What the implementer still needs:

- Game modes 6, 8, 0x12 and 1 with the behaviour of [the flow](#mode-flow); mode 6 can at first be a pass-through
  that reports "no memory card, continue" (Coney's saves are files); mode 8 loads the front-end level on its first
  entry and starts the chosen level later. Their place is marked in `src/platform/main.cpp`.
- A Lua 4.0 virtual machine running the game's own bytecode (`level100.lua`, `config_preload*.lua`,
  `config_strings_*.lua`), with Coney's own implementations of the bindings it calls: at least `CfgLevelName`,
  `CfgHUDMessage`, `GetLanguage`, `ShowProfileManager`, `ShowRumbleModeInterface`, `MenuLoadLevel`, `PadSetHandler`,
  `ScheduleFunc`, `ScreenQueueEffect`, `GetPTank`, `ReleasePTank`, `PlayMovie`, the scene and sound functions. A
  binding that is not ready yet can be a logged no-op, as long as `ShowProfileManager` pushes the menu mode.
- The level table filled by `CfgLevelName`; `level100` as level 0.
- `InitLevel` far enough to load `level100.lev`, its world and its dependency list, and to call `Menu.onStart`.
- The pad model of [Data](#pad-record) on top of SDL3 gamepads: the 16-bit button word with the bit layout above,
  sticks in [-1, 1] with the 95..160 dead zone (SDL's axes need mapping onto that byte range or an equivalent dead
  zone), an 8-sample history, the hold counters and auto-repeat, the diagonal rule, and "pressed" and "released"
  queries. In test mode the same record is filled from scripted input.
- Menu commands exactly as in [the table](#input), with the 110 ms and 400 ms gaps, accept and back on release.
- The profile manager screens and their transition table; `PM_Greet` (START, blinking text, 70 s attract movie) and
  `PM_Mode` (three items) first. Their drawing is on [GUI](gui.md).

## Open questions

- **Mode 6 at boot** with no memory card, an empty card and a card with a save: which dialogs appear, with which
  strings, and whether the player must answer one before the front end loads. A PCSX2 run with each card state would
  settle it.
- **Rumble mode**: what `PM_Mode`'s code 1 does, and how `Menu.fadeToRMI` and the Rumble mode interface follow.
- **Global string ids**: the text behind `0x76`, `0x78`, `0x79`, `0x8a`, `0x1f` and the memory-card ids needs a
  decoder for `config_strings_*.lua` (the entries' order in the `GSTRING.HUD` table gives the id; a string that
  occurs twice is stored once among the constants, so the constants alone do not give the order).
- **`InitLevel` details**: what the script system's slot `+0x24` loads for a level (which `.lua` files), and the
  meaning of the remaining level record fields (the ones known are on
  [Level loading](level-loading.md#the-level-record)).
- **The device flag `0x02`** hides `PM_Extras` and selects other layouts; it is still unidentified (see
  [Graphics](graphics.md#open-questions)).
- **Script system slots** used here are named by their use only: `+0x44` run a script file, `+0x4c` find a function
  by name, `+0x6c` push a number, `+0x8c` call, `+0xcc` keep a reference to a function by name, `+0xdc` a mode switch
  around preload scripts, `+0x10` update, `+0x24` level entry. A page on the script system should confirm them.
