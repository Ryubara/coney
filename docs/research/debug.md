# Debug features and leftovers

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the Lua chunks in
`WARRIORS.WAD` (467 chunks, disassembled locally; nothing from them is reproduced here). No claim on this page was
observed at runtime yet; the [PCSX2 recipes](#pcsx2-recipes) are untested.

## Purpose

What the retail build still holds of the developers' tools: the cheat-code system, overlays whose drawing was
removed, dormant switches, debug-only script paths, unused script bindings and the strings left behind by stripped
code. Coney needs the cheat system (it is a retail feature). The rest tells us what to ignore, and gives us a few
switches worth turning on in PCSX2 to watch the original work.

Short version: the build has **no debug menu, no free camera and no debug pad handler**. The cheat codes are the only
live developer-facing feature. A level auto-advance switch, a frame-rate line that is computed but never drawn, a
memory-pool dump to `host0:` and a scene-test script mode are all still in the build, dormant. Many debug displays
survive only as orphan format strings.

## Original structure

| Address | Name (ours) | File | Role | Evidence |
| --- | --- | --- | --- | --- |
| `0x00163c68` | `Cheat_CheckSequence` | `GameModes/` (base file, inferred from position) | matches player 1's last six button words against the code table | confirmed (code) |
| `0x00163e98` | `Cheat_SetCallback` | same | stores the Lua callback name | confirmed (code) |
| `0x0036e488` | `lua_SetCheatCallback` | `Scripting/` | the binding `SetCheatCallback(name)` | confirmed (code) |
| `0x001569c0` | `Stats_UpdateFrameRate` | `GameModes/` (base file) | frame counter and the never-drawn FPS line | confirmed (code) |
| `0x0033ab98` | `MemoryManager_DumpToHost` | `Memory/` | writes every pool block to `host0:memdump.txt` | confirmed (code) |
| `0x0033aed0` | (memory manager slot `+0x1b4`) | `Memory/` | chooses between the host dump and two other reports | confirmed (code) |
| `0x0036d5a8` | `lua_isRelease` | `Scripting/` | always true | confirmed (code) |
| `0x0036b4c0` | `lua_SetBugstarIP` | `Scripting/` | stores a string under the name `UNUSED` | confirmed (code) |
| `0x0036c868` | `lua_CapturePVSSamplePoint` | `Scripting/` | calls an empty function | confirmed (code) |

## Data

### Globals

| Address | Size | Meaning | Evidence |
| --- | --- | --- | --- |
| `0x0050c7f8` | 27 × 24 | the cheat-code table: 27 entries of six 32-bit button words, ended by a zero word | confirmed (code) at `0x00163c68` |
| `0x0050ca98` | 4 | the cheat callback's interned name; 0 = none | confirmed (code) at `0x00163e98` |
| `0x005e6df0` | 6 × 4 | the last six distinct button words of player 1, oldest first | confirmed (code) |
| `0x005e6e0c` | 4 | the last button word pushed (0 after a release) | confirmed (code) |
| `0x0050c7bc` | 4 | **level auto-advance** flag; 0 in `.data`, nothing sets it | confirmed (code) at `0x0015fe90` |
| `0x005e5450` | 256 | the FPS line (text), rewritten periodically, never read | confirmed (code) at `0x001569c0` |
| `0x005e53d8` | 30 × 4 | a ring of the last 30 frame-rate samples (float) | confirmed (code) |
| `0x0050c680` / `0x0050c67c` | 4 / 4 | instantaneous and averaged frame rate (float) | confirmed (code) |

### The cheat-code table {#cheat-table}

Each entry is six button words, each a single button from the [pad mask](frontend.md#pad-record) table. The index
is what the script receives. The effects are those of `DoCheat` in `global.lua`, described in our words.
Corroboration: the sequences match the codes published for the retail game.

| # | Buttons | Effect (`DoCheat`) |
| --- | --- | --- |
| 0 | R1 R2 L1 X Down L1 | revives, spray-can charges, $200; a skeleton key and an item when those are unlocked |
| 1 | Up Triangle L3 Select X L2 | god mode (`HuSetGodMode`) |
| 2 | Down Square Left X L1 Select | tireless (`HuSetTireless`) |
| 3 | Up Select X Triangle Triangle Circle | clears the Warriors' wanted level |
| 4 | Select Square Left Square R3 R2 | **none** (returns false) |
| 5 | Square Circle Triangle Select X Left | toggles locked full rage (not in level 99) |
| 6 | Down Down Select Up Up L3 | weapon `dyn_hunter` (not in boss fights) |
| 7 | Square R2 Down Down L1 L1 | weapon `dyn_bat` (not in boss fights) |
| 8 | R2 Circle Select Up L1 Right | weapon `dyn_pipe_a` (not in boss fights) |
| 9 | L1 X R1 R1 Select R2 | weapon `dyn_machet` (not in boss fights) |
| 10 | Right Select Circle Left Circle Square | **none** (returns false) |
| 11 | L3 L3 Circle Up Circle Select | weapon `dyn_bat_tuff` (not in boss fights) |
| 12 | Down Square X Select R1 Left | completes the mission (story levels below 100, not 95, player 1 only) |
| 13 | L1 Select Square Down L2 Right | unlocks every level, Rumble arena and clubhouse unlockable, then completes the mission |
| 14 | Circle Circle Circle L1 Select Triangle | clubhouse unlock 3 and `HuAttachGear` |
| 15 | R3 R2 R1 L3 L2 L1 | clubhouse unlock 6 and `HuAttachGear` |
| 16-25 | (see the table at `0x0050c7f8`) | clubhouse unlocks 22, 27, 21, 26, 28, 9, 10, 23, 25, 30 (`UM_Unlock(95, 1, n)`) |
| 26 | Up Up Down Down Left Right | 99 credits in Armies of the Night (levels 60-64) |

Codes 16-25 in full: 16 Left X X R2 L1 Down; 17 Up X Up Select L3 L1; 18 Triangle Triangle Triangle Select
Triangle R1; 19 L2 X R2 L1 L1 Circle; 20 X L1 Down Square Up X; 21 L2 Select Select Select Select Triangle; 22 Down
Left Up Up Square Right; 23 Circle Circle Down R2 L2 Circle; 24 R1 R1 L1 R1 L1 Up; 25 Right R1 Circle X Select
Square.

## Behaviour

### Cheat codes {#cheats}

**Native side** (confirmed (code) at `0x00163c68`, called once per frame by mode 1's
[frame of play](level-loading.md#a-frame-of-play)):

1. Run only when `W_GameState + 0x224` (the player count) is above 0, `W_GameState + 0x410` is 0 (set while a cinematic
   scene plays: the speech players `0x0021e400` / `0x0021e698` stay silent and music ducks only then), and
   player 1's human and its controller resolve. The pad is the controller's (`+0x19`); only player 1 is read.
2. Take the pad's **whole current button word** (`0x00144a08(pad, 0)`). If it is 0, reset the last word and stop.
   If it equals the last word, stop. Otherwise shift it into the six-word history (`0x005e6df0`).
3. Compare the history with each table entry. Because whole words are compared, every step must be **one button
   pressed alone**; a chord produces a different word and breaks the sequence. Pressing the same button twice needs
   a release in between (the release resets the last word).
4. On a match, if a callback name is set, find it (script slot `+0x4c`), push the index as unsigned (`+0x74`) and
   call it (`+0x8c`, [Scripts](scripting.md)). Clear the history either way.

**Script side** (inferred from the disassembly of `global.lua`): at load, `global.lua` calls
`SetCheatCallback("DbgEnterCheat")`. `DbgEnterCheat(n)` ignores the call when `n` is 27 or more or the level is the
front end (`level100`). It then calls `DoCheat(n, player, 1)` and, if that returns true, plays the voice sample
`cheat_<n>` (two digits) and shows the global string `CHEAT` as an announcement. It also calls
`DoCheat(n, player2, 2)` for a second player, silently. Rumble levels define `CreateCheatCodeHandles`, which only
points `player` and `player2` at the Rumble players so cheats apply there. Codes 12 and 13 complete the mission
through scheduled calls (`CheatLoadLevel` / `CheatArmiesLoadLevel`, 1.5 s later, a fade, then
`HUDLaunchMissionComplete`). In Armies of the Night they save, unlock the level and load the next one.

Codes 4 and 10 are in the table, but `DoCheat` returns false for them. They are accepted and silently do nothing
(inferred: removed cheats).

### Dormant switches

- **Level auto-advance** (`0x0050c7bc`), confirmed (code) at `0x0015fe90` ([Level loading](level-loading.md)):
  when non-zero, `InitLevel` sets `W_GameState + 0x14c` to 3 (leave) and asks for the next section. After the last
  section it asks for the next level (index + 1, below 128). When the list ends, it clears the flag. The only write
  in the code is that clear; the flag is 0 in `.data`.
- **Frame-rate line** (`0x001569c0`), confirmed (code): every frame it counts frames and measures the frame time. At
  each period it formats one line into `0x005e5450` with the format at `0x0054e9c8`: the averaged FPS, the game
  time in seconds, the distance to the nearest pending world sector (capped at 500) and a value from player 1's
  slot `+0x214`. The words "Loaded View" and "Cur View" in the format suggest view distances (inferred). The buffer
  has no other reference (Ghidra xrefs and a `lui`/`addiu` scan), so the drawing call was compiled out (inferred).
- **Memory dump to the host** (`0x0033ab98`), confirmed (code): it opens `host0:memdump.txt` with mode `0x602`
  (create, truncate, write, as in [File I/O](file-io.md)). It writes a banner per pool, one line per block (address,
  size, two tag strings, a line number) and the unused gaps of 128 bytes or more. It is reached only through memory
  manager slot `+0x1b4` (`0x0033aed0`, vtable entry `0x00544694`), and only when the pool's `+0x5c` is set. No caller
  of that slot was found. A retail console has no `host0:` device.
- **Bugstar**: `SetBugstarIP(s)` (unused by every script) stores its string in `W_GameState` under the name
  `UNUSED`, the same name the boot-time `bugstar.dat` read uses ([Boot](boot.md#main)). Confirmed (code) at
  `0x0041d5e8`. Bugstar is Rockstar's bug tracker (inferred: a QA hook).

### Debug-only script paths

- **`isRelease()` is always true** (`0x00357990` returns 1; [Scripts](scripting.md)). 36 scripts branch on it, so
  their non-release branches are dead. In those branches, files run at once with `doFile` instead of being
  preloaded with `preLoadFile` and a completion callback, `PrecacheWorld` runs before Rumble matches, and
  `ParseLuaData` and the clubhouse's `CfgSceneTables` are skipped or deferred. Inferred from the disassembly.
- **`debuglua`** (`global.lua`) runs a file named `debug` and calls `dumpFullTables`. No script calls it, neither is
  defined anywhere, and no `debug.lua` is on the disc (its name hash is absent from `WARRIORS.DIR`).
- **Scene test**: `global.lua` sets `SCENETEST` to nil first thing, and nothing sets it again. Level start functions
  test it (240 reads); when it is set, a level preloads `<level>_scenetest` and calls `PlaySceneTest`. Thirteen
  `*_scenetest` chunks are on the disc, for `level2`, `level5`, `level9`, `level11`, `level20`, `level31`, `level34`,
  `level51`, `level54`, `level83`, `level84`, `level87` and `level99`. Inferred: a cinematic-scene review mode.
- Leftover helpers in level scripts, defined but never called: `Debug` (`LEVEL20`), `Test` and `test`
  (`level82` chapters), `PrintFlagPairs` (`wchair`), `AddTestDealers` (`level83`).

### Unused bindings

The script survey finds 164 of the 956 bindings ([Scripts](scripting.md)) with no call in any of the 467 chunks
(corroboration: disc survey). Most are gameplay variants never used (`Goal*`, `Tactic*`, `Cfg*`, HUD setters). The
developer-flavoured ones:

| Binding | Address | What it does | Evidence |
| --- | --- | --- | --- |
| `SetBugstarIP` | `0x0036b4c0` | see [Dormant switches](#dormant-switches) | confirmed (code) |
| `CapturePVSSamplePoint` | `0x0036c868` | calls `0x0040cc68`, which returns at once: the PVS sampler was removed | confirmed (code) |
| `HuRender` | `0x00358aa0` | draws one human and its attached items outside the normal pass (`0x00237e98`) | inferred |
| `FreezeWorld` | `0x0036c740` | pushes the freeze mode (`GameMode_PushFreeze`) | confirmed (code) |
| `SwapPlayerControl`, `Quit`, `ShowOptionMenu`, `SSMC_detect`, `SSMC_format`, `UM_SetUnlockable`, `doInclude` | see the bindings table | not examined | |

### Orphan strings

These format strings are in `.rodata`, but no instruction pair loads their address and no data word points at them
(a `lui`/`addiu` scan of `.text` that finds the known references, plus a pointer search). Their code was stripped.
Confirmed (code) for the absence.

| Address | What it was for (inferred from the format) |
| --- | --- |
| `0x0055d0a8`-`0x0055d1d0` | a per-human overlay: index, hit points, `CNS Flags`, `SFlags`, attacker, grapple, name |
| `0x0058aed8`-`0x0058af98` | a crime display: `Snap`, `Strike`, the wanted timers and the crime scene position |
| `0x005467a8` | a trace of a found teleport position |
| `0x00578b58`, `0x00578b80` | a memory table and its total |
| `0x0054e3d8` | a TCP receive progress trace |

The `DebugStream.cpp`, `DevRWDebug.cpp`, `DS_PS2TCPSocket.cpp` and `FS_TCPSocketFileSys.cpp` path strings are
orphans as well ([Source map](source-map.md)). The TCP socket and file-system classes' error strings remain, but
none is loaded by live code we found. The `host0:` file system is set up at boot but never receives game files
([File I/O](file-io.md)).

### Not present

- **Debug menu**: no menu strings beyond the retail GUI, and no mode on the [game-mode stack](boot.md) that is not a
  retail screen.
- **Free camera**: the 14 camera classes (`Cam_Scene`, `Cam_Locked`, `Cam_Fixed`, `Cam_3rdPerson`, `Cam_Follow`,
  `Cam_Transition`, `Cam_Rail`, `Cam_Power`, `Cam_Mug`, `Cam_Mini`, `Cam_Hood`, `Cam_Spline`, `Cam_Failed`,
  `Cam_Win`) are all gameplay cameras ([Camera](camera.md)).
- **Debug pad**: the callers of the pad mask queries (`0x00144b88`, `0x00144bf0`) are the action map, the camera
  input, the HUD, menus and mode 1's frame. None reads a second pad or a SELECT, L3 or R3 combination outside
  gameplay.
- **Asserts and logs**: the assertion macros are compiled out; the source paths that remain are allocation tags or
  orphans ([Source map](source-map.md)). The Lua library keeps its own `assert` and error strings.

### Unused disc content

Covered elsewhere: fifteen world pairs without a `.lev` and the single-world `objarena`
([Level loading](level-loading.md)), the 13 scene-test scripts above, and the bugstar file hook. The Lua chunk with
hash `69178ffb` has no recovered name.

## PCSX2 recipes {#pcsx2-recipes}

Untested; for a later runtime pass. Addresses are NTSC-U.

1. **Cheats**: in a story level, press a sequence from the [table](#cheat-table) on pad 1, one button at a time.
   Watch `0x005e6df0` fill and the announcement appear. To shortcut a code, write its first five button words
   into `0x005e6df4`-`0x005e6e07` (a new word shifts the history down by one) and `0` into `0x005e6e0c`, then
   press the sixth button.
2. **Level auto-advance**: write `1` (32-bit) to `0x0050c7bc` before starting a level. Each `InitLevel` should then
   go straight to the next section or level until the list ends.
3. **FPS line**: during play, read a string at `0x005e5450`. It should show the averaged FPS, the time, and the two
   view distances.
4. **Memory dump**: enable PCSX2's host file system and make the memory manager's slot `+0x1b4` run (set the
   pool's `+0x5c`, then call the slot from a breakpoint). Speculative: PCSX2 may not map `host0:`.
5. **Scene test**: set the Lua global `SCENETEST` before a level's start function runs. One way is to run the string
   `SCENETEST=1` through script slot `+0x2c` (`0x00356a78`) from a breakpoint at the level entry `0x003569d8`.
   Speculative.

## What a reimplementation must keep

- The cheat checker exactly: player 1 only, whole-word comparison, the reset on release, the history cleared after
  a match, and the index passed to the Lua callback. The game's scripts implement the effects, including the two
  inert codes.
- `isRelease()` returns true, so the scripts take their release paths.

Coney may add its own debug tools (a free camera, overlays). They are ours, not the original's, and do not belong
in an `@orig` tag.

## Coney's implementation

Coney's debug menu is its own ([The debug menus](../guides/debug-menu.md)), opened with L3 and R3 together, a chord no
retail control uses (see [Not present](#not-present)). Its Cheats page calls the script's cheat callback with a code's
index, as the checker does on a match; the checker itself is not in Coney yet.

## Open questions

- What player slot `+0x214` returns (the FPS line's "Cur View").
- Who, if anyone, calls memory manager slot `+0x1b4`, and what the two other reports (`0x0033aa30`,
  `0x00338798`) print.
- Whether the scene-test scripts still run against the retail levels.
