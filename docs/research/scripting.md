# The script system (Lua 4.0)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime observations were
made in PCSX2 2.9.94 (2026-10-04) by reading the live Lua state's global table over PINE, and say so. The disc-side
survey read the NTSC-U disc's WAD and is reported as names, counts and layouts only.

## Purpose

What runs the game's Lua scripts: when the Lua state is made and remade, which libraries and bindings it has, which
scripts run in which order and in which state, how C++ calls back into Lua, and what the bindings return to a script.
It is what an implementer needs to run `enum_preload.lua`, the `config_preload*.lua` scripts, `global.lua` and a
level script (`level100.lua` for the front end) the way the game does.

In one paragraph: there is **one** Lua 4.0.1 state, owned by a script-system object. It is made at start-up and
**remade every time a level is unloaded**. It has the standard `string`, base and `math` libraries (no `io`), the
tolua support table and **956 game bindings**. Script errors are silent (`_ERRORMESSAGE` and `_ALERT` do nothing). The
legal screen runs `enum_preload.lua`, then `config_preload.lua`, `config_preload2.lua` and `config_preload3.lua`;
loading a level runs `global.lua` and then `<level>.lua`, all in the same state, so a level script sees the
preloads' globals and `global.lua`'s helpers. C++ calls Lua functions **by name** (dotted, such as `Menu.onStart`),
never by reference.

## Original structure

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00512b04` | `g_ScriptSystem` | the one script-system object (pointer) | confirmed (code) |
| `0x00356390` | `ScriptSystem_Create` | allocates 0x28 bytes, builds the object; called by `Game_InitializeSubsystems` (`0x00160fd0`) and `UnloadLevel` (`0x00160bac`) | confirmed (code) |
| `0x00356450` | `ScriptSystem_Destroy` | deletes the object and clears the pointer; called by `UnloadLevel` just before the create | confirmed (code) |
| `0x003564d8` | `ScriptSystem::ScriptSystem` | opens the Lua state and the libraries, registers the bindings | confirmed (code) |
| `0x00356610` | `ScriptSystem::~ScriptSystem` | closes the state, frees the schedule | confirmed (code) |
| `0x0037d420` | `RegisterBindings` | the tolua registration of every game binding | confirmed (code) |
| `0x00383f38`, `0x00384398`, `0x00384480`, `0x003842c0` | `CfgChar`, `CfgObjectGroup`, `CfgGang`, `CfgSpeedClass` | four bindings registered by the constructor itself | confirmed (code) |
| `0x00334478` | `luaV_execute` | the Lua 4.0 interpreter | confirmed (code) |
| `0x00161218` | `RunPreloadScripts` | the legal screen's preload lists | confirmed (code) |

Names are ours. The vtable is at `0x005449b8`; its slots are below.

### The object

| Offset | Field | Evidence |
| --- | --- | --- |
| `+0x00` | vtable (`0x005449b8`) | confirmed (code) |
| `+0x04` | `lua_State*` | confirmed (code) |
| `+0x08`-`+0x14` | the schedule: a vector of 0x18-byte entries kept as a min-heap on the due time | confirmed (code) |
| `+0x1c` | set when the last name looked up had a `:` (a method: the table is pushed as `self`) | confirmed (code) |
| `+0x20` | the per-frame update function's name (interned string), or 0 | confirmed (code) |
| `+0x24` | the time of the next forced garbage collection (`now + 2000` ms) | confirmed (code) |

### Vtable slots

The function behind each slot, by the byte offset of its function word (each slot is an adjust word and a function
word; callers read the function at this offset and the adjust word 4 bytes before it, so the update slot `+0x14` has
its adjust word at `+0x10`, the offset [Boot](boot.md#one-frame) once gave).

| Slot | Function | What it does | Evidence |
| --- | --- | --- | --- |
| `+0x14` | `0x003566d8` | **update(dt)**: runs the scheduled calls whose time has come (below), then the update function with `dt × 1000` (ms) as its argument, then a garbage collection every 2 s | confirmed (code) |
| `+0x1c` | `0x00356990` | collect garbage now (`lua_setgcthreshold(L, 0)`), and push the next forced collection 2 s away | confirmed (code) |
| `+0x24` | `0x003569d8` | **level entry**: run `global.lua`, then `<name>.lua` | confirmed (code) |
| `+0x2c` | `0x00356a78` | run a string (`lua_dobuffer`) | confirmed (code) |
| `+0x34` | `0x00356af8` | **run a file** by WAD name: load it through the file manager, run it as a buffer named after the file, collect garbage | confirmed (code) |
| `+0x3c` | `0x00356ad0` | run a buffer | confirmed (code) |
| `+0x44` | `0x00356c58` | run each file of a NULL-terminated list through `+0x34` | confirmed (code) |
| `+0x4c` | `0x00356e08` | **find a function by name** and push it: the name is split at `.` and `:` (at most 32 characters per part); a `:` also pushes the table as `self` | confirmed (code) |
| `+0x54`-`+0x84` | `0x00356f70`-`0x00357138` | push an argument: unsigned, object handle (as its index), short, int, unsigned, string, `VolumeBox` usertype | confirmed (code) |
| `+0x8c` | `0x00357188` | **call** with n arguments (plus one for `self`) | confirmed (code) |
| `+0x94` / `+0x9c` / `+0xa4` | `0x003571b8` / `0x003572e8` / `0x00357430` | **schedule** a call of a named function after a delay in ms, with 0, 1 or 2 number arguments | confirmed (code) |
| `+0xac` | `0x00357588` | flush scheduled calls: all, or those whose name matches | confirmed (code) |
| `+0xb4` / `+0xbc` | `0x003578d8` / `0x004f1db0` | set / get the update function's name | confirmed (code) |
| `+0xc4` | `0x00357910` | pop a number | confirmed (code) |
| `+0xcc` | `0x00357958` | intern a name (`luaS_new` + fix), so a stored callback name is never collected | confirmed (code) |
| `+0xd4`, `+0xdc` | `0x00357988`, `0x00357bb8` | **do nothing** (the "mode switch" around the preloads is empty in this build) | confirmed (code) |
| `+0xe4` | `0x00357bc0` | returns 0 | confirmed (code) |

## Data

### Libraries

| Library | Opened by | Contents | Evidence |
| --- | --- | --- | --- |
| string | `0x00331a58` | 12 names: `strlen strsub strlower strupper strchar strrep ascii strbyte format strfind gsub strformat` (`strformat` is `format`, `ascii` is `strbyte`) | confirmed (code) |
| base | `0x00326548` | the 33 Lua 4.0 base functions, `_VERSION` = `"Lua 4.0.1"`, and the 4.0 compatibility names `foreachvar nextvar rawgetglobal rawsetglobal` | confirmed (code); compat names confirmed (runtime) |
| math | `0x0032c628` | 23 functions, `PI`, and the `pow` tag method for `^` | confirmed (code) |
| tolua | `0x0037d420` | the table `tolua`, the classes `M_Vector4` (`x y z w`) and `M_Quat` (`i j k r`), the variables `NilHandle` and `NilSoundHandle` | confirmed (code) |

There is no `io` library. `dofile` and `dostring` exist (base library) but the scripts use the binding `doFile`.
`random` is a game binding (registered after the math library, so it replaces `math`'s): `random(a, b)` returns a
number from the game's own generator (`0x00386488`, state at `0x006eb880`).

**Runtime check:** at the boot movies the global table holds exactly 1,034 entries: the 956 bindings and 78 library
and tolua names. confirmed (runtime).

### Bindings

`RegisterBindings` (`0x0037d420`) makes 962 function registrations under 952 names: ten names are registered twice
(tolua overloads; the second registration wins in Lua 4, so those names have the second function): `CfgScrFx`,
`FlagNetTraverse`, `GoalManWeaponPile`, `GoalPathBlocker`, `HUDSetInstArrowLocation`, `QueueMotionBlurEffect`,
`ScenePlay`, `SetLight`, `SoundLoopMusicTrack`, `SoundPlayMusicTrack`. With the constructor's four that makes **956**.
confirmed (code); the count confirmed (runtime).

| Family (name prefix) | Count | What for |
| --- | --- | --- |
| `Hu` | 174 | one human (character): state, flags, speech, animation |
| `Cfg` | 117 | configuration, called by the config scripts and levels |
| `Goal` / `Act` / `Br` / `Tactic` | 67 / 7 / 32 / 40 | AI goals, actions, brains, gang tactics |
| `HUD` | 61 | the in-game HUD, radar, objectives |
| `Gang` | 57 | gangs |
| `Sound`, `Snd`, ambient sounds and emitters, sound slots | 51 | sound and music |
| `Cam` | 41 | cameras |
| `UM_`, `Stat`, `Inv`, `SSMC_` | 37 | unlockables, statistics, inventory, save sequences |
| `Scene` | 15 | cutscenes |
| other | 257 | world objects (`Obj`, `Car`, doors, flags, paths, particles, weather), scheduling, pad and message handlers, level flow, menus, lighting |

Counts are by name prefix (a name goes in the first row that matches); 956 in all. confirmed (code). The full list is
the executable's
registration table; it is not copied here.

**Survey (disc, counts only):** the 467 compiled Lua chunks in the WAD call 792 of the 956 bindings; 164 are never
called by any script. The scripts on the boot-to-front-end path (`enum_preload.lua`, the three `config_preload*.lua`,
`config_strings_en.lua`, `global.lua`, `level100.lua`) call 256 distinct bindings; 37 of them have their result used.

### Argument and result conventions

Every binding takes its arguments with the tolua helpers below and checks no types: a missing argument reads as the
default (0, `false` or `NULL`), a wrong type as whatever Lua's conversion gives.

| Helper | Address | Reads / pushes | Evidence |
| --- | --- | --- | --- |
| number | `0x004091c8` | argument n as a number, default if n is past the top | confirmed (code) |
| boolean | `0x00409318` | nil → false, else number ≠ 0 | confirmed (code) |
| string | `0x00409220` | argument n as a string | confirmed (code) |
| table field | `0x004093a0` / `0x00409438` | `t[i]` as a number / string | confirmed (code) |
| push number | `0x004094e8` | a number | confirmed (code) |
| **push boolean** | `0x00409588` | **nil for false, the number 1 for true** (Lua 4.0 has no booleans) | confirmed (code) |
| push string | `0x00409508` | a string, nil for `NULL` | confirmed (code) |
| push usertype | `0x00409538` | a tolua object (`M_Vector4` and others), nil for `NULL` | confirmed (code) |
| write back | `0x004095d8` / `0x00409648` | stores a number / string into a table argument (bindings that fill a table they were given) | confirmed (code) |

The scripts follow the same convention: `global.lua` sets `true = 1` and `false = nil` before anything else.

### Bindings whose results the front end needs

| Binding | Returns | Source | Evidence |
| --- | --- | --- | --- |
| `GetPlatform()` | always **1** (`0x00357998`) | constant | confirmed (code) |
| `isRelease()` | always true (1) (`0x00357990`) | constant | confirmed (code) |
| `GetLanguage()` | the game state's language (`+0x120`): 0 English, 1 Spanish, 2 French, 3 Italian, 4 German (`LanguageExt` in `config_preload2.lua`); 0 on the NTSC-U disc | `0x0041d7f0` | confirmed (code); values confirmed (runtime) |
| `GetCurrentLevelIndex()` | the current level record's index (`+0x56dc`); 0 on the front end | `0x0041d718` | confirmed (code); values confirmed (runtime) |
| `GetLevelId(i)` | record `i`'s level number (record `+0x04`); 100 for record 0 | `0x0041d6f0` | confirmed (code); values confirmed (runtime) |
| `GetDifficulty()` / `GetProfileDifficulty()` | game state `+0x154` / `+0x43c` (1 at the front end) | `0x0041d800` / `0x0041d820` | confirmed (code); values confirmed (runtime) |
| `GetCheckPoint()` | game state `+0x33a` | `0x0041abe8` | confirmed (code) |
| `UM_IsLevelComplete(n)` | true if the unlockables manager has level `n` done; false when the manager is absent | `0x004238a8` | confirmed (code) |
| `ToInt(x)` | `x` truncated | `0x0036d938` | confirmed (code) |
| `ScenePreload(name, ...)`, `GetPTank(...)`, `ObjSpawn(...)`, `CameraCreateLocked(...)` | a handle (a number) | | confirmed (code) |

### Bindings that drive the script system

| Binding | Does | Evidence |
| --- | --- | --- |
| `doFile(name)` | runs `name .. ".lua"` (`0x003579a0`: formats `"%s.lua"`, `0x00579058`) through slot `+0x34`, synchronously; the PC-style `../levels/<level>/` path is dead as for the level entry | confirmed (code) |
| `preLoadFile(name, callback)` | requests `name .. ".lua"` from the file manager asynchronously (`0x00357a68`); when it arrives the completion routine `0x00356d00` runs it with the interned callback name | confirmed (code); what `0x00356d00` does with the name is not traced |
| `ScheduleFunc(name, ms)` / `ScheduleFuncArg1(name, ms, n)` | slot `+0x94` / `+0x9c` (`0x003863d8`, `0x00386410`) | confirmed (code) |
| `FlushScheduledFuncs(name)` | slot `+0xac` (`0x00386450`) | confirmed (code) |
| `gc()` | slot `+0x1c` (`0x00386370`) | confirmed (code) |
| `PadSetHandler(pad, button, name)` | stores a handler name for a pad button (`0x00145698`) | confirmed (code) for the arguments |
| `ShowProfileManager(first, second)` | interns both names (slot `+0xcc`) into `0x005e6690` / `0x005e6694` and pushes the profile manager mode `0x12` unless it is already on top (`0x001552b0`, `0x0015dfd0`); the first is the one `PM_Mode` calls for code 1 ([Front end](frontend.md#profile-manager)) | confirmed (code) |

### Scheduled calls

An entry of the schedule (0x18 bytes): due time (ms on the game timer `0x0050b734 + 0x48`), the function's interned
name, then two (flag, number) pairs for up to two arguments. `update` pops every entry whose time has come, looks the
name up (slot `+0x4c`) and calls it with its arguments; a name that does not resolve to a function is dropped
silently. confirmed (code).

## Behaviour

### Life of the Lua state

1. **Start-up.** `Game_InitializeSubsystems` creates the script system (`0x00160fd0`). The constructor opens a Lua
   state with a stack of 0x400 slots (`0x0032f698`), opens the string, base and math libraries, registers the tolua
   bindings and the four `Cfg` functions, and replaces `_ERRORMESSAGE` and `_ALERT` with functions that return
   without doing anything. A script error therefore stops that chunk (or that call) and leaves no trace.
   confirmed (code).
2. **Legal screen** (mode 5 `Enter`, `0x00161218`). Runs two lists through slot `+0x44`: `0x00512b08` =
   `enum_preload.lua`, then `0x00512b10` = `config_preload.lua`, `config_preload2.lua`, `config_preload3.lua`.
   The calls of slot `+0xdc` around them do nothing. confirmed (code).
   - `enum_preload.lua` defines `MATERIAL` and `SA` and calls nothing.
   - `config_preload.lua` sets up sound: material and animation sound slots, music info (bindings only).
   - `config_preload2.lua` defines `PHYS`, `ANIM`, `OBJECT`, `AXIS`, `PHYFLAG`, `Platform = GetPlatform()`,
     `LanguageExt`, then runs `doFile("config_strings_" .. LanguageExt[GetLanguage()])` and configures characters,
     hats, gangs, weapons, HUD messages and sounds through `Cfg*` bindings.
   - `config_preload3.lua` defines and runs `CfgObjectsAtoC` ... `CfgObjectsTtoZ` (1,279 `CfgObj` calls) and
     `CfgLevels`, which builds `levelNames` (111 records) and calls `CfgLevelName` for each, then sets its temporary
     globals (`levelNames`, `LT_*`, `LOCKED`, `UNLOCKED`, `SUBWAY_*`, `GSTRING`, `CfgLevels`) back to nil.
   **Runtime check:** 1,034 globals before the preloads, 1,059 after. confirmed (runtime).
3. **A level loads** (`InitLevel`, also for the front-end level `level100`). The level flow calls slot `+0x24` with
   the level's name: it runs `global.lua`, then `<name>.lua` (`"%s.lua"`). The PC-style branch (`../lua/global.lua`,
   `../levels/%s/%s.lua`) is dead in this build because `0x001458c0` returns 1. confirmed (code).
   **Runtime check:** 1,252 globals at the front end, including the preloads' (`Platform` = 1, `LanguageExt`,
   `PHYFLAG` ...), `global.lua`'s and `level100.lua`'s (`Menu`, `WonderWheelAnim`). confirmed (runtime).
4. **A level unloads** (`UnloadLevel`, `0x001607b8`): the script system is destroyed and created again
   (`0x00356450`, `0x00356390`), so the next level starts from the bindings alone. The preloads do **not** run again
   for it (they belong to the legal screen) (inferred from the call sites; what a level that needs `PHYS` or
   `GSTRING` does after an unload is an open question).

### Calling Lua from C++

The game calls a script function in three steps: find it by name (slot `+0x4c`), push the arguments (slots
`+0x54`-`+0x84`), call (slot `+0x8c`). Callbacks given to bindings (`ShowProfileManager("Menu.fadeToRMI",
"Menu.startGame")`, `ScheduleFunc("Menu.launchRMI", 500)`, `PadSetHandler`, `SetMsgHandler`) are kept as interned
names and looked up when they fire, so a script can replace the function behind a name at any time. confirmed (code).

### The language files and `Platform`

`config_preload2.lua` sets `Platform = GetPlatform()` (1 on the PS2) before it runs `config_strings_<lang>.lua`. About
twenty entries of each language file are written as `if Platform == 2 then A else B end`; the PS2 takes `B`: for
example `0x76` is "PRESS THE START BUTTON" (not "PRESS START") and `0x1f` names triangle as "back" (not circle).
The branches are inferred from the disassembly; the PS2 wording is confirmed (runtime) in PCSX2. `GSTRING.HUD` entries
are stored with
explicit indices (`GSTRING.HUD[118] = ...`), so the disassembly gives each global string id's text.

### `global.lua`

It runs before every level script and gives the levels their shared helpers. Its main chunk (9,438 instructions,
154 functions) sets `true`/`false`, the Rumble mode numbers (`RM_*`), the colour table `CL`, builds the five
languages' `GSTRING`/`LABEL` tables and keeps the one for `GetLanguage()` (then sets `strings` and `strings2` to
nil), picks the sound matrix (`SndLoadMatrix("armies")` for levels 60-64, else `"sound"`), sets `MenuTrack`
(`music/in_the_city` once level 84 is complete), and defines the helpers (`ChangeCam`, `TagInfo`, `NoScenes`,
`ObjectiveAdd`, ...). It ends with `CfgAmbient()` and `SetupLevelInventory()`. inferred (disassembly of the chunk).

### `level100.lua` (the front end)

Its first calls are `TagInfo.NoTag()` and `NoScenes()` (helpers from `global.lua` that remove the tag and scene
helpers for this level), then `CfgSetDatabaseSizes`, a locked camera (`ChangeCam(CameraCreateLocked("Black", ...))`),
the table `Menu` and two particle tanks. The `Menu` functions (inferred from the disassembly):

| Function | Does |
| --- | --- |
| `onStart` | `startScene`, `ShowProfileManager("Menu.fadeToRMI", "Menu.startGame")`, fade in over 1.5 s |
| `onFinish` | `Menu = nil`, release the two particle tanks |
| `fadeToRMI` | fade out (0.7 s), `ScheduleFunc("Menu.launchRMI", 500)` |
| `launchRMI` | loop `MenuTrack`, `ShowRumbleModeInterface("Menu.cancelRumbleMode", "Menu.startRumbleMode", 1)` |
| `startRumbleMode(n)` | `SetCheckPoint(1)`, stop the music, `stopScene`, `MenuLoadLevel("level" .. n)` |
| `cancelRumbleMode` | loop `music/wonderwheel_132b`, `ScheduleFunc("Menu.fadeIn", 100)` |
| `startGame` | stop the music, `stopScene`, `runNextMission(1)` (a `global.lua` helper) |
| `playMovie(id)` | `Menu.movieID = id`, stop music, fade out, `ScheduleFunc("Menu.playMoviePostFade", 500)` |
| `playMoviePostFade` | `stopScene`, `PlayMovie(Menu.movies[Menu.movieID])` (1 `TRAILER`, 2 `L1_IN`), `ScheduleFunc("Menu.movieFinished", 500)` |
| `movieFinished` | `startScene`, fade in |
| `startScene` / `stopScene` | `WonderWheelAnim:enable(true / false)` |

`ScreenQueueEffect(type, seconds)` with type 0 fades in and 1 fades out ([Front end](frontend.md#profile-manager)
for what a fade blocks).

## Notes for implementers

- **Table constructors flush every 62 items**, not 64: `SETLIST` stores its items at `A × 62 + 1` onwards
  (`luaV_execute`, `0x00334478`, the multiplier `0x3e`; the largest `B` on the disc is 62). This is this build's
  `LFIELDS_PER_FLUSH`. A VM that uses 64 shifts every list item past the 62nd (in `config_preload3.lua`'s
  `levelNames`, every level record after the 62nd). confirmed (code).
- What a script needs to run without errors on the front-end path: the preloads in one state, then `global.lua` and
  `level100.lua` in that same state, with numbers from `GetLanguage`, `GetPlatform`, `GetCurrentLevelIndex`,
  `GetLevelId` and `UM_IsLevelComplete` (nil for false).

## Open questions

- What a level loaded after an unload (a fresh state without the preloads) does when it needs `PHYS`, `MATERIAL` or
  `GSTRING`: does the level flow run the preloads again, or do the level scripts not need them?
- `RegisterUpdate`, and what `preLoadFile`'s completion routine (`0x00356d00`) does with the callback name.
