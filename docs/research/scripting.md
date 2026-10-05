# The script system (Lua 4.0)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime observations were
made in PCSX2 2.9.94 (2026-10-04) by reading the live Lua state's global table over PINE, and say so. The disc-side
survey read the NTSC-U disc's WAD and is reported as names, counts and layouts only.

## Purpose

What runs the game's Lua scripts: when the Lua state is made and remade, which libraries and bindings it has, which
scripts run in which order and in which state, how C++ calls back into Lua, and what the bindings return to a script.
It is what an implementer needs to run `enum_preload.lua`, the `config_preload*.lua` scripts, `global.lua` and a
level script (`level100.lua` for the front end) the way the game does.

In one paragraph: there is **one** Lua 4.0.1 state, owned by a script-system object. It is made at start-up and **remade
every time a level is unloaded**. It has the standard `string`, base and `math` libraries (no `io`), the tolua support
table and **956 game bindings** (every one in the [script bindings](../references/bindings/index.md) reference). Script
errors are silent (`_ERRORMESSAGE` and `_ALERT` do nothing). The legal screen runs `enum_preload.lua`, then
`config_preload.lua`, `config_preload2.lua` and `config_preload3.lua`; loading a level runs `global.lua` and then
`<level>.lua`, all in the same state, so a level script sees the preloads' globals and `global.lua`'s helpers. C++ calls
Lua functions **by name** (dotted, such as `Menu.onStart`), never by reference.

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

Counts are by name prefix (a name goes in the first row that matches); 956 in all. confirmed (code). Every binding, with
its arguments, result, effect, evidence and usage counts, is in the [script bindings](../references/bindings/index.md)
reference, generated from `research/bindings/`.

**Survey (disc, counts only):** the 467 compiled Lua chunks in the WAD reference 802 of the 956 bindings by their
global names; 154 are never used by any script. The scripts on the boot-to-front-end path (`enum_preload.lua`, the
three `config_preload*.lua`, `config_strings_en.lua`, `global.lua`, `level100.lua`) use 266 distinct bindings, at least
37 of them for their result. The first mission's four scripts (`level99.lua` and its `_combat`, `_lesson1` and
`_lesson2` scripts) use 175; the two paths together need 366. An earlier count of 792, 256 and 171 traced calls through
the stack and missed calls made inside `for` loops (`CfgHUDMessage` in `config_preload2.lua`, for one); counting
references does not. Per-binding counts are in the reference.

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

### Bindings the front end and the script system depend on

The front end needs the results of [`GetPlatform`](../references/bindings/util.md#getplatform),
[`isRelease`](../references/bindings/util.md#isrelease), [`GetLanguage`](../references/bindings/level.md#getlanguage),
[`GetCurrentLevelIndex`](../references/bindings/level.md#getcurrentlevelindex),
[`GetLevelId`](../references/bindings/level.md#getlevelid),
[`GetDifficulty`](../references/bindings/level.md#getdifficulty),
[`GetProfileDifficulty`](../references/bindings/level.md#getprofiledifficulty),
[`GetCheckPoint`](../references/bindings/level.md#getcheckpoint),
[`UM_IsLevelComplete`](../references/bindings/level.md#um_islevelcomplete),
[`ToInt`](../references/bindings/util.md#toint), [`ScenePreload`](../references/bindings/scene.md#scenepreload),
[`GetPTank`](../references/bindings/world.md#getptank), [`ObjSpawn`](../references/bindings/world.md#objspawn) and
[`CameraCreateLocked`](../references/bindings/camera.md#cameracreatelocked) (the last four return handles); a script
runs other scripts and schedules calls through [`doFile`](../references/bindings/script.md#dofile),
[`preLoadFile`](../references/bindings/script.md#preloadfile),
[`ScheduleFunc`](../references/bindings/script.md#schedulefunc),
[`ScheduleFuncArg1`](../references/bindings/script.md#schedulefuncarg1),
[`FlushScheduledFuncs`](../references/bindings/script.md#flushscheduledfuncs),
[`gc`](../references/bindings/script.md#gc), [`PadSetHandler`](../references/bindings/input.md#padsethandler) and
[`ShowProfileManager`](../references/bindings/hud.md#showprofilemanager). Their arguments, results and addresses are in
the [script bindings](../references/bindings/index.md) reference. Two facts matter for the order of things: `doFile`
runs its script synchronously through slot `+0x34`, and `preLoadFile` loads asynchronously and runs the script, then
calls the callback, when the file arrives (completion routine `0x00356d00`). `ScheduleFuncArg1` takes `(name, arg, ms)`:
the number comes before the delay (slot `+0x9c`, `0x003572e8`, adds its last argument to the game time). confirmed
(code).

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

### `runNextMission` (story progress) {#run-next-mission}

`global.lua`'s `runNextMission(arg)` (function 61 of the main chunk) chooses the story's next level. Inferred from the
disassembly:

1. It builds a table from the **last completed level** to the next mission's `{checkpoint, level}`. In order: 0 →
   99; 99 → 80; 80 → 87; 87 → 95 (checkpoint 1); 34 → 95 (2); 2 → 95 (3); 3 → 95 (4); 5 → 95 (5); 81 → 95 (6);
   86 → 93; 93 → 95 (7); 31 → 95 (8); 14 → 95 (9); 9 → 95 (10); 51 → 52; 52 → 54; 54 → 55; 55 → 84; 84 → 95 (11);
   64 → 95 (12). Entries without a checkpoint in this list use 1. Level 95 is the hub the story returns to between
   missions, entered at a different checkpoint each time (inferred from the pattern).
2. The last level is the global `LastLevel` (set to 0 by `global.lua`'s main chunk, so 0 after every level load).
   When it is 0 or nil, `findLastMission()` takes the first of 64, 84, 55, 54, 52, 51, 9, 14, 31, 93, 86, 81, 5, 3,
   2, 34, 87, 80, 99 for which `UM_IsLevelComplete` is true, else 0.
3. If the table has an entry: `SetCheckPoint(checkpoint)`, `SoundStopMusicTrack()`,
   `MenuLoadLevel("level" .. level)`, and when `arg` is 1, `HUDLaunchMissionComplete(4)`. Otherwise it calls
   `HUDLaunchMisssionComplete(3)` (three `s`), a name no binding has, so that call fails and, since errors are silent,
   ends the function. That this branch is a bug in the shipped script is inferred.

So a **new game** starts `level99` at checkpoint 1 ([Front end](frontend.md#story-start)).

### Cameras from Lua {#cameras-from-lua}

`CameraCreateFollow(name, target)` is a `global.lua` helper (function 15), not a binding: it calls
`CamSetupFollow(name, target)` and then `CfgFollowCamera(FOLLOWCAM_MINDIST, FOLLOWCAM_MAXDIST, FOLLOWCAM_DEFAULTDIST,
FOLLOWCAM_DEFAULTANGLE, FOLLOWCAM_FOV, FOLLOWCAM_NEARPLANE, {FOLLOWCAM_OFFSET_X, _Y, _Z}, FOLLOWCAM_SLOWMO)` and
returns the camera's handle. `global.lua` sets these globals to **3, 6.6, 4.8, 13 (degrees), 65, 0.1, (0, 0, 1.4)
and 0.2**; `CameraNormal()` applies them again to `MainCam`. Inferred from the disassembly; the values are confirmed
(runtime) in the camera object ([Camera](camera.md#the-follow-camera-object)).

### `level99.lua` (the first mission) {#level99}

The first story level's script, inferred from the disassembly. Its main chunk defines helpers, calls
`AddFlagsBoxesPaths()` and `RegisterObjects()`, sets the animation ids it uses (`ANIM_*`) and a `Buttons` table, and
calls `Main()`:

- **`Main`**: `checkpoint = GetCheckPoint()`, `SetFogColor(0.05, 0.05, 0.02)`, `ReportCrime(0)`, `ShowHud(0)`,
  `RestoreHud()`; a table `tMission` of three checkpoints, each `{create the Warriors, start function, script}`:
  1 = `{AddWarriors2, "Checkpoint1", "level99_combat"}`, 2 = `{AddWarriors, "Checkpoint2", "level99_lesson1"}`,
  3 = `{AddWarriors1, "Checkpoint3", "level99_lesson2"}`; then `RunLevel()`.
- **`RunLevel`**, in order: `CfgSetStatValue(0, 0, 1000)`; `F.PreloadAnims()` (six `SetDynamicAnimation` calls
  naming `.anm` clips); `Warriors, GangWarriors = tMission[checkpoint][1]()`; `player = Warriors.Rembrandt`,
  `player2 = Warriors.Ash`; `preLoadFile(script, startFunction)` (the checkpoint's script, which runs the start
  function when it has loaded); `AddCameras()`; `SetStartGameCallback("StartAmbient")`;
  `HuSetDemiGodMode(player, true, 0.25)` and the same for `player2`; `CfgPlayerMugging(false)`; extra set-up for
  checkpoints 2 and 3; `EnableCommand(player, 38, 0)` and `EnableCommand(player, 37, 0)` (two player commands off
  for the tutorial).
- **`AddWarriors2`** (checkpoint 1): `GangCreate(0, "Warriors2", 0, 0)`, then
  `HuCreate("Rembrandt", 32, {-284.4, 120.4, 0.3}, 0, "warr_sw", 1, gang)` (player 1) and
  `HuCreate("Ash", 40, {-285.5, 125.1, -8.5}, 235, "warr_sw", 2, gang)`; returns the table of humans and the gang.
- **`AddCameras`**: `Cameras.follow = CameraCreateFollow("follow", player)`, `MainCam = Cameras.follow`,
  `CameraMakeActive(Cameras.follow, 0)`, `CameraReset(Cameras.follow)`, then `AddCameras = nil`.
- **`StartAmbient`** (run by `InitLevel` once the level is ready): at checkpoint 1, `SuperRunScene(IntroScene)`
  (the in-engine intro, defined in `level99_combat.lua`); at checkpoint 3, the ambient loop
  `vags/ambient/city/distant_traffic_loop`.
- **`Checkpoint1`** (`level99_combat.lua`) calls `P1.SetupCombat`: the tutorial's sections (`P1.SetupCam`,
  `SetupBasicAttacks`, ...), `HUDSetObjective`, `HUDTurnOffRadar` and the training enemies (`AddCombatEnemy`).

The player therefore exists and the follow camera is active before the first frame of mode 1; the intro scene takes
the camera over and gives it back.

These calls reach [`HuCreate`](../references/bindings/character.md#hucreate),
[`GangCreate`](../references/bindings/gang.md#gangcreate),
[`CamSetupFollow`](../references/bindings/camera.md#camsetupfollow),
[`CfgFollowCamera`](../references/bindings/config.md#cfgfollowcamera),
[`CameraMakeActive`](../references/bindings/camera.md#cameramakeactive),
[`CameraReset`](../references/bindings/camera.md#camerareset),
[`SetStartGameCallback`](../references/bindings/script.md#setstartgamecallback),
[`SetCheckPoint`](../references/bindings/level.md#setcheckpoint),
[`SetDynamicAnimation`](../references/bindings/character.md#setdynamicanimation),
[`HuSetDemiGodMode`](../references/bindings/character.md#husetdemigodmode),
[`EnableCommand`](../references/bindings/character.md#enablecommand),
[`CfgPlayerMugging`](../references/bindings/config.md#cfgplayermugging),
[`ShowHud`](../references/bindings/hud.md#showhud), [`RestoreHud`](../references/bindings/hud.md#restorehud),
[`ReportCrime`](../references/bindings/level.md#reportcrime),
[`MenuLoadLevel`](../references/bindings/level.md#menuloadlevel) and
[`HUDLaunchMissionComplete`](../references/bindings/hud.md#hudlaunchmissioncomplete), among the 175 bindings the first
mission uses; each is described in the [script bindings](../references/bindings/index.md) reference (`ShowHud`, for
one, does nothing in this build).

## Notes for implementers

- **Table constructors flush every 62 items**, not 64: `SETLIST` stores its items at `A × 62 + 1` onwards
  (`luaV_execute`, `0x00334478`, the multiplier `0x3e`; the largest `B` on the disc is 62). This is this build's
  `LFIELDS_PER_FLUSH`. A VM that uses 64 shifts every list item past the 62nd (in `config_preload3.lua`'s
  `levelNames`, every level record after the 62nd). confirmed (code).
- What a script needs to run without errors on the front-end path: the preloads in one state, then `global.lua` and
  `level100.lua` in that same state, with numbers from `GetLanguage`, `GetPlatform`, `GetCurrentLevelIndex`,
  `GetLevelId` and `UM_IsLevelComplete` (nil for false).

## Coney's implementation

The script system and the front end's scripts run in Coney (2026-10-04), written from this page; the Lua 4.0 virtual
machine underneath is [Front end](frontend.md#coneys-implementation)'s.

- **`ScriptSystem`** (`src/scripting/script_system.h`): one Lua state, made by `create()` (the libraries in the
  original's order, string, base, math; then the bindings; then `_ERRORMESSAGE` and `_ALERT` as functions that do
  nothing) and made again by the level flow's unload. `runFile` runs a WAD script by name, `runFiles` a list,
  `enterLevel` runs `global.lua` and then `<level>.lua`, `call` finds a function by dotted name (`Menu.onStart`; a `:`
  passes the table as `self`) and calls it, `schedule`/`flushScheduled` keep the schedule of named calls with up to two
  number arguments, and `update` runs the calls that are due and the update function (with the step in ms). The
  original's errors leave no trace; Coney writes each to its log (`script error: <script>: <message>`) and counts it.
- **The libraries** (`src/scripting/lua_libraries.h`): the 33 base functions with the four compatibility names and
  `_VERSION`, the string library (12 names with Lua's patterns) and the math library (23 functions, `PI`), written anew
  from the public Lua 4.0 library.
- **The bindings** (`src/scripting/script_bindings.h`): one table, by name, saying for each whether it is **real**
  (does its job: the getters and script-system bindings of the tables above, `CfgLevelName`, the five string bindings,
  `ShowProfileManager`, `MenuLoadLevel`, `ScreenQueueEffect`), **routed** (handed to a Coney stand-in that logs it:
  `PlayMovie`, the three music bindings, `ShowRumbleModeInterface`) or a **stub** (returns its documented default:
  nothing, a new handle for `ScenePreload`, `GetPTank`, `ObjSpawn` and `CameraCreateLocked`, false for
  `SceneIsPreloaded` and `UM_IsTypeDirty`). The configuration stubs (the `Cfg*` bindings, `CfgObj`, sound, unlockables
  and commands) keep their arguments (`RecordedCalls`) for the subsystems that will need them. 27 real, 5 routed and 111
  stubs (73 of them recording): every binding the front-end path calls, and no more.
- **The level table** (`src/warriors/level_table.h`, `GameState`): `CfgLevelName`'s records by index, read by
  `GetLevelId` and the level flow (record 0 is `level100`).
- **The front end** runs the preloads at the legal screen and `global.lua` and `level100.lua` in the same state when the
  level flow starts the front end, then calls `Menu.onStart` ([Front end](frontend.md#coneys-implementation)).

Coney's choices, where the page is silent or Coney differs:

- A call of an unset global (a binding Coney does not list; the original registers all 956) is skipped as a no-op
  returning nothing, and logged once by name, instead of stopping the script.
- Scheduled calls due at the same time run in the order they were scheduled; a call scheduled during `update` waits for
  the next update, even with a delay of 0. There is no garbage collection to force (Coney's VM counts references), and
  the 32-character limit on the parts of a dotted name is not applied.
- `CfgLevelName`'s argument order: the scripts pass 18 arguments, the index, four names, the level number, then twelve
  numbers. Coney maps the index, the level name (`+0x14`), the second name (`+0x24`), the world name (`+0x39`), the
  fourth name (`+0x49`) and the level number (`+0x04`) by the disc's data (record 0 is `level100` with number 100; the
  world name equals the level name in every record with packs), and keeps the twelve numbers in order (inferred).
- `random(a, b)`: whole numbers in `[a, b]` from Coney's own deterministic generator; the math library's `random` uses
  another one of Coney's, never the C library's. The trigonometry works in degrees, as stock Lua 4.0 does.
- `tolua`, `M_Vector4` and `M_Quat` are empty tables and `NilHandle` and `NilSoundHandle` are 0, below the first handle
  a stub gives out.
- `preLoadFile` runs its file at once (Coney's reads are synchronous) and ignores its callback.

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[disc][scripts]"` with `CONEY_DISC` set runs the four
preloads, `global.lua`, `level100.lua`, `Menu.onStart` and the menu callbacks (`fadeToRMI`, `launchRMI`,
`cancelRumbleMode`, `playMovie`, `startGame`, `onFinish`) in one state: no script error and no call of a missing
binding. The preloads add 25 globals and the level entry 193 more, as at run time (1,034 → 1,059 → 1,252); the level
table holds 111 records; `CfgObj` is called 1,371 times; 9,273 configuration calls are recorded; 388 HUD strings are
set. `Menu.startGame` asks for `level99` (`runNextMission(1)`), and after the unload `global.lua` and `level100.lua` run
again in the fresh state without the preloads, also without errors (a Coney observation for the front end, not a check
of the original).

## Open questions

- What a level loaded after an unload (a fresh state without the preloads) does when it needs `PHYS`, `MATERIAL` or
  `GSTRING`: does the level flow run the preloads again, or do the level scripts not need them? (For the front end,
  `global.lua` and `level100.lua` run without errors in Coney's fresh state.)
- `IntroScene` and the scene system (`SuperRunScene`): how a scripted scene takes the camera and the player's control
  and gives them back.
- `RegisterUpdate`, and what `preLoadFile`'s completion routine (`0x00356d00`) does with the callback name.
- **`CfgLevelName`'s arguments:** which of the twelve numbers after the level number fills which field (`+0x08`
  sections, `+0x0c`, the three flags at `+0x0d`, `+0x10`, `+0x6c`-`+0x80`), and why the scripts pass 18 arguments where
  the writer (`0x0041f118`) is described with 17 values. The section count does not simply equal the number of
  `<level>_<k>.pak` files for any one argument (25 of 63 levels with packs match the seventh).
- **`CfgObj` count:** the page counts 1,279 `CfgObj` calls in `config_preload3.lua`; running it calls the binding 1,371
  times. Static call sites against calls made (loops, or functions called twice)?
- **Degrees or radians:** does this build's math library keep Lua 4.0's degrees (Coney's assumption)?
- **`random`'s generator** (`0x00386488`, state `0x006eb880`), and what `random(a, b)` returns (whole numbers?).
- **`NilHandle` and `NilSoundHandle`:** their values.
- **`PadSetHandler`'s arguments:** this page gives `(pad, button, name)`, [Front end](frontend.md#input) gives
  `(button, player, "function")`. Coney implements neither yet: `PadSetHandler` is a stub that ignores its
  arguments, since the front-end path needs no Lua pad handler.
- **Names:** the `@orig` tags call the script system's slots `ScriptSystem::Update`, `EnterLevel`, `RunFile`,
  `RunFiles`, `FindFunction`, `Call`, `Schedule`, `ScheduleArg1`, `ScheduleArg2`, `FlushScheduled` and
  `SetUpdateFunction`, the binding wrapper `0x0036eef8` `ShowProfileManager_Binding`, `0x0041f118`
  `W_GameState_SetLevelRecord`, `0x00160d78` `MenuLoadLevel_Choose`, `0x0015c7b0` `LevelFlow_ChooseLevel` and
  `0x0020a268` `PM_Mode::HandleCommand`, until the research database names them.
- Binding arguments the [script bindings](../references/bindings/index.md) reference marks as not understood yet:
  which bit of `PadSetHandler`'s mask is which PS2 button; what each message number of `SetMsgHandler` means; the 23
  values `GetRumbleModeData` returns; the scene-play flags (`ScenePlay` and its relatives) beyond their `global.lua`
  names; when animation callbacks (`AddAnimCallback`) fire and with what arguments; most fields of the large `Cfg*`
  records (`CfgChar`, `CfgPowerClass`, `CfgWarriorClass`), which are written through computed addresses with no reader
  found yet. Each would move up from inferred once a reader or a runtime observation is found.
