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
| `+0x54`-`+0x84` | `0x00356f70`, `0x00356fc8`, `0x00357028`, `0x00357060`, `0x00357098`, `0x003570f0`, `0x00357138` | push an argument: unsigned, object handle (as its index, `0x00390068`), short, int, unsigned, string, `VolumeBox` usertype | confirmed (code) |
| `+0x8c` | `0x00357188` | **call** with n arguments (plus one for `self`) | confirmed (code) |
| `+0x94` / `+0x9c` / `+0xa4` | `0x003571b8` / `0x003572e8` / `0x00357430` | **schedule** a call of a named function after a delay in ms, with 0, 1 or 2 number arguments | confirmed (code) |
| `+0xac` | `0x00357588` | flush scheduled calls: all, or those whose name matches | confirmed (code) |
| `+0xb4` / `+0xbc` | `0x003578d8` / `0x004f1db0` | set / get the update function's name | confirmed (code) |
| `+0xc4` | `0x00357910` | pop a number | confirmed (code) |
| `+0xcc` | `0x00357958` | intern a name (`luaS_new` + fix), so a stored callback name is never collected | confirmed (code) |
| `+0xd4`, `+0xdc` | `0x00357988`, `0x00357bb8` | **do nothing** (the "mode switch" around the preloads is empty in this build) | confirmed (code) |
| `+0xe4` | `0x00357bc0` | returns 0 | confirmed (code) |

Two empty functions are called on the way: `0x00357ba0` by the collection slot `+0x1c` and `0x00383f30` by the
destructor (confirmed (code); leftovers of a debug build, inferred).

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
whole number in `[a, b]`, both ends included, from the game's own generator (`0x00386488` → `0x003353f0`): the next
entry of a fixed table of 1,024 32-bit numbers (`0x005117e0`) taken modulo `b − a + 1` (unsigned) and added to `a`.
The generator's state is only the table index at `0x006eb880`, advanced by 1 (masked to 10 bits) on every draw,
zeroed by a static initialiser (`0x00335608`) and never seeded; many C++ callers draw from the same index.
Confirmed (code). Details: [World flags](flags.md#player-starts).

**Runtime check:** at the boot movies the global table holds exactly 1,034 entries: the 956 bindings and 78 library
and tolua names. confirmed (runtime).

### Bindings

`RegisterBindings` (`0x0037d420`) makes 962 function registrations under 952 names: ten names are registered twice
(tolua overloads; the second registration wins in Lua 4, so those names have the second function): `CfgScrFx`,
`FlagNetTraverse`, `GoalManWeaponPile`, `GoalPathBlocker`, `HUDSetInstArrowLocation`, `QueueMotionBlurEffect`,
`ScenePlay`, `SetLight`, `SoundLoopMusicTrack`, `SoundPlayMusicTrack`. With the constructor's four that makes **956**.
confirmed (code); the count confirmed (runtime).

**The first registrations** are reached only from the second: each second wrapper checks its argument types and, when
they do not match, calls the first (tolua's overload fallback). Confirmed (code); each form's arguments are in the
binding's `overloads` entry.

| Name | First wrapper | Second wrapper | Evidence |
| --- | --- | --- | --- |
| `CfgScrFx` | `0x0036ae60` | `0x0036b020` | confirmed (code) |
| `FlagNetTraverse` | `0x0037a8d0` | `0x0037a968` | confirmed (code) |
| `GoalManWeaponPile` | `0x00361a50` | `0x00361b80` | confirmed (code) |
| `GoalPathBlocker` | `0x00362250` | `0x00362360` | confirmed (code) |
| `HUDSetInstArrowLocation` | `0x0036fec8` | `0x0036ff40` | confirmed (code) |
| `QueueMotionBlurEffect` | `0x0037be18` | `0x0037be90` | confirmed (code) |
| `ScenePlay` | `0x00367810` | `0x00367a20` | confirmed (code) |
| `SetLight` | `0x0037bfb8` | `0x0037c348` | confirmed (code) |
| `SoundLoopMusicTrack` | `0x003712d8` | `0x00371348` | confirmed (code) |
| `SoundPlayMusicTrack` | `0x003711d0` | `0x00371230` | confirmed (code) |

**The tolua types and variables** (confirmed (code)): `0x00357bc8` declares the usertypes `M_Vector4`, `M_Quat`,
`WorldPath` and `SoundHandle`. The fields have one getter and one setter each, at offsets 0, 4, 8 and `0xc`:
`M_Vector4`'s `x y z w` (`0x00357c28`/`0x00357c90`, `0x00357d28`/`0x00357d90`, `0x00357e28`/`0x00357e90`,
`0x00357f28`/`0x00357f90`) and `M_Quat`'s `i j k r` (`0x00358028`/`0x00358090`, `0x00358128`/`0x00358190`,
`0x00358228`/`0x00358290`, `0x00358328`/`0x00358390`). The global `NilHandle` is the number at `0x006ebd30`
(get `0x0036d418`, set `0x0036d478`) and `NilSoundHandle` the `SoundHandle` at `0x00598690` (get `0x0036d4e0`,
set `0x0036d530`). A script can assign either.

**Workers of the script and object bindings** (each called only by its binding's wrapper unless the row says
otherwise; confirmed (code)):

| Address | Name | Binding: what it does | Evidence |
| --- | --- | --- | --- |
| `0x00357a68` | `Script_PreloadFile` | `preLoadFile`: the path is the name itself when it starts `~/` (stripped) or the device says so (`0x001458c0`), else the current level's directory (level record `0x0051489c + 0x14e8 + 0x84 ×` index) plus the name; requests the file with the completion `0x00356d00` and the interned callback | confirmed (code) |
| `0x00386370` | `Script_CollectGarbage` | `gc`: slot `+0x1c` | confirmed (code) |
| `0x003863a0` | `Script_RegisterUpdate` | `RegisterUpdate`: slot `+0xb4` | confirmed (code) |
| `0x003863d8` / `0x00386410` | `Script_ScheduleFunc` / `Script_ScheduleFuncArg1` | `ScheduleFunc` / `ScheduleFuncArg1`: slots `+0x94` / `+0x9c` | confirmed (code) |
| `0x00386450` | `Script_FlushScheduledFuncs` | `FlushScheduledFuncs`: slot `+0xac` | confirmed (code) |
| `0x003864b0` | `GameTimer_GetMilliseconds` | `GetGameTime` | confirmed (code) |
| `0x00386308` | `Script_SetObjZoneMsgHandler` | `SetObjZoneMsgHandler`: `ObjZone_SetMsgHandler` on the object-zone manager (task manager `+0x840`) | confirmed (code) |
| `0x00386340` | `Script_SetGeneralCarMsgHandler` | `SetGeneralCarMsgHandler` ([Cars](cars.md)) | confirmed (code) |
| `0x00385950` | `Object_GetTypeBits` | `GetRTTI` | confirmed (code) |
| `0x00385918` | `Object_GetHitpoints` | `GetHitpoints` | confirmed (code) |
| `0x003859a0` | `Obj_GetName` | `GetName`: the object's name (its vtable `+0x14`), a fixed default string for a bad handle | confirmed (code) |
| `0x003859f0` | `Obj_GetTypeName` | `GetObjectName` | confirmed (code) |
| `0x00385a50` | `Obj_GetPosition` | `GetPosition`: a human's position, else the object's, else zero, in one static vector (`0x006ebd20`) that the next call overwrites | confirmed (code) |
| `0x00385b08` | `AreaEffect_Spawn` | `SpawnAreaEffect` (also `0x002a05a4`) | confirmed (code) |
| `0x00385b90` | `IsHumanHandle` | `IsAHuman` | confirmed (code) |
| `0x00385c90` | `Obj_SetAxisAngle` | `OrientObject` | confirmed (code) |
| `0x00385ea8` | `Obj_TestDistance` | `TestDistance(d, a, b)`: true when `a` and `b` are strictly nearer than `d`; false for a bad handle | confirmed (code) |
| `0x00385f60` / `0x00386010` | `WalkingDistance` / `Obj_PathExists` | `WalkingDistance` / `PathValid` | confirmed (code) |
| `0x003858b8` | `ScriptHandlers_ClearAll` | no binding: clears all 26 message handlers of a handler component (slot `+0xc` with none, messages 0-25); from a gang's and a human's teardown (`0x0016ac10`, `0x00233f48`) | confirmed (code) |
| `0x003864e0` | `Script_StrDup` | no binding: a heap copy of a string, used by the GUI | confirmed (code) |

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

### The mission stopwatch {#stopwatch}

One countdown (or count-up) timer for the whole game, the object at `*0x0051504c`, set by
[`W_SetStopWatch`](../references/bindings/level.md#w_setstopwatch) (`0x004235f0`) and run by
[`W_StartStopWatch`](../references/bindings/level.md#w_startstopwatch) (`0x00423648` → `0x004233a8`). Confirmed (code):

| Offset | Type | Field |
| --- | --- | --- |
| `+0x00` | u32 | game-timer reading at the last step |
| `+0x08` / `+0x0c` | s32 | current time / target time, ms |
| `+0x10` | float | rate the elapsed time is multiplied by: 1.0 from the constructor (`0x00423358`); other writers not traced |
| `+0x14` | u32 | running (`W_StartStopWatch`'s argument; the update clears it at the target) |
| `+0x18` / `+0x1c` | u32 / char* | shown on the HUD / the label drawn before the time; read by the HUD's stopwatch text (`StopWatchHud_UpdateMinutes`, `0x001cd5e8`, and `0x001cd748`; [HUD](hud.md#fn-after-textentrypad)); written by `W_ShowStopWatch` (`0x00423670` → `0x004235e0`) |
| `+0x20` | char[32] | the callback's name, kept as text (empty for none) |
| `+0x40` / `+0x48` | s32 | warning window (ms) / time of the last warning beep; `W_SetStopWatch` sets 0 / 1000 |

**Each frame of play** (mode 1 step 1, `0x004233f8`), while running and the game timer has advanced by `d` ms: the
current time moves `d × rate` toward the target (down when the target is below it). On reaching or passing the target
it is clamped there, the watch stops, and the callback is found by name (slot `+0x4c`, dotted names work) and called
with no arguments. While counting down within the warning window, a beep (sound `0x0058b9f0`) plays at most once a
second. Confirmed (code) at `0x004233f8`. The HUD's display of it (`W_ShowStopWatch`) belongs to the HUD.

Coney (`src/warriors/stop_watch.h`, stepped by `src/gamemodes/player_frame.h` before the scripts' frame): the rate is
1 and the warning beep is reported but plays no sound yet.

### Message handlers {#message-handlers}

Game objects (humans, flags, boxes, doors, props) talk to scripts through **messages**: a record whose `+0x20` is
the message number and `+0x24` the object it is about, with `+0x00`, `+0x04` and `+0x11` as extra values.
[`SetMsgHandler`](../references/bindings/script.md#setmsghandler) (`0x00386298` → `0x003860b8`) gives the object a
handler component (vtable `0x00544b98`, made on demand by `0x003848c8`) and stores the interned callback name in its
slot for that number: 26 slots, `+0x0c + 4 × message`. Message 0 also keeps a prompt pointer at `+0x78`
([`SetMsgHandlerEx`](../references/bindings/script.md#setmsghandlerex)). The component's `+0x74` is the **repeat
period** in ms (1000 by default, `0x00384a10`; set by slot `+0x44`). Confirmed (code).

The components come from a pool of 100 (0x80 bytes each, a free list, pointer `0x00512b24`), made by `InitLevel`
(`0x00384688`) and freed by `UnloadLevel` (`0x00384840`); a car or prop that is destroyed gives its component back
(`0x00384968`: clear it, detach it from the object, destroy it, push it on the free list). Its methods: set a
slot (`0x00384aa0`, message 0 also stores the prompt at `+0x78`; `0x00384b30`, message 0 stores its value at
`+0x7c` instead), get a slot (`0x00384bc0`), "has any handler" (`0x00384bd0`, any of the 26), the two message-0
values (`0x00384c00`, `0x00384c18`) and set the owner `+0x08` (`0x00384c30`). Confirmed (code).

**Delivery** (`0x00384c38`): a message reaches Lua only when the object has a name in that slot, the component is
attached, and **the level-end state (`W_GameState + 0x14c`) is 0**: once a mission is won, failed or left, triggers
stop calling scripts. The marshaller (`0x00384ce0`, only while scripting runs, `0x00512b28` = 1) pushes the
arguments by message number and calls the function; some numbers ask for one result, and a true result means the
message was consumed. Confirmed (code); the meanings in the last column are from the
[Script events](../references/script-events.md) list and the senders below.

| Message | Callback arguments | Result asked |
| --- | --- | --- |
| 0 | `(self, subject)`: the interaction (triangle), the subject the human | yes: true consumes the press |
| 1 | `(self, other, n)`: `other` is the record's `+0x04`, `n` the byte `+0x11` | no |
| 2 | `(self, other or NilHandle)` from `+0x00` | no |
| 3, 4, 5 | `(self, subject)`: entered, left, still inside (below) | no |
| 6 | `(subject, n, other or NilHandle)`, `n` from `+0x00` | no |
| 7 | `(subject, other)`, `other` from `+0x00` | no |
| `0xb` | `(self, other)` | no |
| 8 | not marshalled here; a flag's own handler (`0x00416038`) calls `(flag, human)` when a human arrives at it ([AI: GoalMoveToFlag](ai.md#move-to-flag)) | no |
| 9 | `(n)`, the short at `W_GameState + 0x412` | yes |
| 10, `0xd` | `(self)` | no |
| `0xc` | `(self)` | yes |
| `0xe` | `(self, other, n)`, `n` from `+0x04` | yes |
| `0x10` | `(subject, other, n)`, `n` from `+0x04` | yes |
| `0xf` | `(subject, other)` | yes |
| `0x11` | `(subject, other or NilHandle, n)`, `n` from `+0x04` | no |
| `0x12`, `0x13` | `(subject, other or NilHandle)`: died or knocked out / revived; `other` is the attacker | no |
| `0x19` | `(self, other or NilHandle, n, flag)`: `n` the signed short `+0x04`, `flag` the number 1 or 0 (`+0x06` ≠ 0, not a boolean); a car's explosion sends `(car, NilHandle, -1, 1)` ([Cars](cars.md#explode)) | no |

`self` is the object the handler belongs to; `subject` is the record's `+0x24`; `other` is `+0x00` unless the row
says otherwise. A gang's handlers
(`GangSetMsgHandler`) use their own marshalling for 18, `0x11` and 2 ([AI: gang events](ai.md#gang-events)).

**World objects** (props, weapons, pick-ups; the shared vtable `0x005453a0`) take a message through their `+0x44`
(`0x00392520`): it first offers it to the object zone's handler (`ObjZone_DispatchMessage`, `0x003982a0`, whose result
is ignored), then to the handler component at object `+0xfc` (its `+0x5c` is `0x00384c38`, the delivery above), and
returns that result. Confirmed (code).

**The interaction prompt.** [`SetMsgHandlerEx`](../references/bindings/script.md#setmsghandlerex) (`0x00386168`)
with message 0 and a non-empty prompt also calls the object's `+0x124`; a world object's (`0x00391c98`) registers,
when it has none yet (`+0x130`), a **kind-1 context record** holding the prompt (`+0x10`) and the second text
(`+0x14`), reach 1.1 m ([Crimes: context records](crimes.md#context-records)). A nil callback or an empty prompt
calls `+0x12c` (`0x00391ce8` → `0x00417df8`), which unregisters the record and clears any player's current record
(`+0x660`) that pointed at it. Triangle by the object then sends it message 0 with the human as the subject
([Crimes: triangle](crimes.md#triangle), step 4); a true result ends the press, anything else lets the game's own
pick-up go on. Confirmed (code), and at runtime (slot 1, a bat 1 m ahead of the player given a handler by a call):
the call registered a kind-1 record, the player's `+0x660` took it the next update, and triangle delivered message 0
(three calls in the one press, from steps 4 and 5) before the bat's pick-up clip started.

### Trigger boxes and spheres {#triggers}

Most of the first mission's progress is driven by message 3 on volume boxes. Confirmed (code):

- **A volume box** (`AddVolumeBox`, kind 0: vtable `0x00545df8`, pools on [Tasks](tasks.md#classes)) keeps the
  handles of up to 60 occupants (`+0x70`, cleared to `NilHandle` by `0x004151c0`) and its own handler component at
  `+0x160`. Every box's update runs **once every fifth frame**, round-robin by handle (`VolumeBoxes_UpdateAll`,
  `0x00412ca0`, from mode 1's step 6; the same pass marks in `+0x69` whether player 1's camera is inside a box that
  asks for it, `+0x6a`). The update (`0x00415378`), while enabled (byte `+0x68`), collects the humans within its
  bounding sphere (centre `+0x30`, radius `+0x40`; `0x002274a8`, at most 60), skips the dead (`0x00227eb0`), and
  tests each:
    - inside and new: added to the occupants, message **3** (entered);
    - inside and already an occupant: message **5**, at most once per repeat period (next time at `+0x1e0`);
    - an occupant no longer inside, or dead: removed, message **4** (left).
    - an occupant whose handle no longer resolves (`0x00390268`): skipped, with no message 4, and its entry stays.
      "Dead" here (`0x00227eb0`) is no brain or state flag `0x100000000`; a human taken out by `HuDelete` (flag
      `0x200000000`) is neither, so while it still resolves it leaves like anyone else (message 4). When the deleted
      human stops resolving is not traced.
- **A kind-2 box is a player trigger** (vtable `0x00545cb8`, made by `0x004133d0`; update `0x004134f8`): the same
  messages 3, 5 and 4 with the same inside test and repeat period (next time at `+0xf8`), but its only candidates
  are the **two players' humans** (the handles at `0x0051489c + 0x228` and `+0x22c`) and it keeps just those two
  occupants (`+0xf0`). An AI human never sets one off, however it moves. The chase levels mark their camera and
  hint zones with kind 2 (`level2`: 5 boxes, `level3`: 29, among them `vbStartRail`, which hands the player back his
  pad in [`level3`'s chase](#level3)). Confirmed (code). Kind-3 boxes (turf) have no trigger update traced.
  Coney: `VolumeBoxes::update()` runs kind-2 boxes over the players' humans only (a player's brain, alive or
  brain-dead).)
- **Disabling** a box (`EnableVolumeBox(box, false)`, `0x004152e0`) empties its occupant list without sending
  message 4, so a human still inside when it is enabled again gets a fresh message 3.
- **Teleporting** (`TeleportToFlag`, `0x00385db0`) only sets the object's position (and calls a human's slot
  `+0x14c`); it does not touch any box, so a teleported occupant gets message 4 on the box's next update. Messages
  still reach Lua only while `W_GameState + 0x14c` is 0 ([delivery](#message-handlers)).
- **Inside** (`0x00412a18`): within the bounding sphere, between the box's lowest and highest `z` (`+0x18`, `+0x28`),
  and inside the four corners rotated about the centre by the 2 × 2 matrix at `+0x48`-`+0x54`
  (`x' = m00 dx + m01 dy`, `y' = m10 dx + m11 dy`, [`RotateVolumeBox`](../references/bindings/world.md#rotatevolumebox)).
- **Message 6, damage done from inside a box** (`VolumeBoxes_SendDamageMessage`, `0x00413018`): when a human
  damages something, every enabled box (handles `0x26c`-`0x2eb`) that has a Lua handler for message 6 and whose
  inside test holds for **the human's position** (not the object's) is sent message 6; then humans within 30 m are
  alerted (`0x00293768`). The Lua handler gets **(human, box, object)**, the object `NilHandle` when there is none
  (marshaller case 6, `0x00384ce0`). Senders: a strike landing (`Strike_Contact`, `0x0021b290`, the object hit), a
  thrown object hitting something (`0x00393538`, `0x003939a8`), an object breaking on a human (`0x00392b88`, no object)
  and a car hit (`0x0038bea0`). Confirmed (code). `level34`'s riot meter and `level3`'s gallery count on it.
    - **Only kind-0 boxes** get it: the box's kind (slot `+0x64`: 0 volume, 2 player, 3 turf) must be 0, and the box
      enabled (`+0x68`). Every such box that contains the human gets it, not just one. Confirmed (code).
    - **What `object` is**, by sender (confirmed (code) unless marked): a strike on a world object, a glass pane or
      any other non-human, non-car target (`Strike_Contact`): the struck object, but only if it was still intact
      before the blow (both its damage counters `+0x10d`, `+0x10e` non-zero, or −1 for unbreakable), so the blow
      that breaks a newsstand or a store item is counted and blows on its wreck are not; a strike on a human sends
      nothing. A car hit (`Car_OnHit`): the car, for every strike by a human, for a thrown object's first contact
      that damages a part, and for a molotov. A thrown object (`0x003939a8`): the thrown object itself, when it
      is broken by the impact, with the thrower as the human (inferred). An object breaking on a human it hit
      (`ThrownObject_HitHuman`, `0x00392b88`): no object (`NilHandle`). Glass broken by an explosion or
      `BreakGlassInRadius` sends nothing.
- **A trigger sphere** ([`TriggerSphereCfg`](../references/bindings/world.md#triggerspherecfg), `0x00414bc0`) is a
  0x184-byte record from a pool of 100 (`0x006f3f50`, slots `0x006fd6e0`) attached to an object's handler component:
  `+0x170` the object, `+0x174` the radius, `+0x17c` the mode, `+0x180` armed, `+0x164` its message-5 period (1000
  from `0x00414480`). `TriggerSphereCfg`'s last argument goes to the object's handler component (`+0x74`); whether
  that reaches the sphere's period is not traced.
  The pool is updated round-robin, each sphere every fifth frame (`0x00414398`, from mode 1's step 6), with the same
  enter / still-inside / leave rules as a box (`0x004146e0`), the messages going to the **object** (so
  `SetMsgHandler(dealer, 3, ...)` hears a human come within the radius). Mode 0 tests distance only; mode 1 adds a
  test between the sphere's centre and the human (`0x0024dee8`, inferred: line of sight); mode 2 the same from
  raised points (`0x0024df40`). The first mission's one sphere is the dealer's: radius 4, mode 2, interval 500.
- **Arming and disarming a sphere** ([`TriggerSphereEnable`](../references/bindings/world.md#triggersphereenable),
  `0x00414ae0`). Arming an object with no sphere creates one with the defaults of `0x00414480`: radius 0 (so it
  accepts nobody until a radius is set), mode 1, period 1000 ms, empty inside list. Disarming (`0x004144d0`) only
  empties the 60-handle inside list (`+0x00`, filled with 0xff) and clears `+0x180`, without message 4; the sphere,
  its settings and the object's handlers stay. The pool slot is freed when the handler component is cleared
  (`0x00384a50`). confirmed (code).

The other box and sphere functions. Confirmed (code):

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00412888` | `Box_Destruct` | the box base's destructor (vtable `0x00545c48`): unregisters the handle (`+0x44`) | confirmed (code) |
| `0x004128e8` | `Box_SetName` | the name at `+0x58`, at most 15 characters | confirmed (code) |
| `0x00412910` | `Box_ComputeBounds` | the bounding sphere: centre `+0x30` the midpoint of the corners `+0x10` and `+0x20`, radius `+0x40` half the diagonal | confirmed (code) |
| `0x004129c0` | `Box_ContainsObject` | the inside test on an object's position | confirmed (code) |
| `0x00412bf8` / `0x00412c40` | `VolumeBox_Enable` / `VolumeBox_SetRotation` | `EnableVolumeBox` (the box's slot `+0x5c`) / `RotateVolumeBox` (the matrix `+0x48`-`+0x54`) | confirmed (code) |
| `0x00413198` | `VolumeBox_ContainsObject` | `IsInVolumeBox`: both handles resolve and the object is inside | confirmed (code) |
| `0x00413398` / `0x00413448` / `0x004134a0` | `PlayerBox_DestroyPool` / `PlayerBox_Destruct` / `PlayerBox_HasOccupant` | the kind-2 pool (`0x00514814`); the destructor (vtable `0x00545cb8`, clears the handler component `+0x70`); whether a human is one of its two occupants | confirmed (code) |
| `0x00414ed0` / `0x00414f08` / `0x00414f40` | `TurfBox_DestroyPool` / `TurfBox_Construct` / `TurfBox_Destruct` | kind 3 (vtable `0x00545d88`, pool `0x00514834`) | confirmed (code) |
| `0x004150e0` / `0x00415288` / `0x00415320` | `VolumeBox_DestroyPool` / `VolumeBox_Destruct` / `VolumeBox_HasOccupant` | kind 0 (pool `0x0051483c`); the destructor clears the handler component `+0x160`; whether a handle is in the occupants `+0x70` | confirmed (code) |
| `0x00415118` | `VolumeBox_FindByName` | the first box (handles `0x26c`-`0x2eb`) with that name, else `NilHandle` | confirmed (code) |
| `0x00413208` | | empty | confirmed (code) |
| `0x00414438`, `0x00414cc8` / `0x00414d28` | `TriggerSphere_Construct`, its static initialiser and stub | 60 inside handles set to -1, the handler vtable at `+0xf0`; all 100 built at boot | confirmed (code) |
| `0x004142a0` / `0x004142d0` / `0x00414338` | `TriggerSpheres_Reset` / `TriggerSphere_Alloc` / `TriggerSphere_Free` | clear the 100 slots; take the first free; free one (the sphere being updated, `0x00514824`, is only marked at `0x00514828`) | confirmed (code) |
| `0x00414548` / `0x00414688` | `TriggerSphere_Accepts` / `TriggerSphere_AcceptsObject` | within the radius of the object, then mode 1's test (`0x0024dee8`) or mode 2's clear ray (`Ray_IsClear`); on a point or an object | confirmed (code) |
| `0x004144f8` | `TriggerSphere_HasOccupant` | whether a handle is in the inside list | confirmed (code) |
| `0x00414a28` | `TriggerSphere_SetRadius` | `TriggerSphereSetRadius`: makes the sphere when the object has none, then `+0x174` | confirmed (code) |

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

The main chunk, in order (inferred from the disassembly; what the result looks like is on
[Front end](frontend.md#background)):

1. `TagInfo.NoTag()` and `NoScenes()` (helpers from `global.lua` that remove the tag and scene helpers for this
   level).
2. `CfgSetDatabaseSizes(48, 34, {2, 1, 2, 3})`.
3. `ChangeCam(CameraCreateLocked("Black", {0, 2, -10}, 40, 180, 0, 0, 0.5, 200), 0)`: a locked camera (fov 40°,
   heading 180°, near 0.5, far 200, clamped to 150 by the binding), made active at once (`ChangeCam` is
   `CameraMakeActive` then `CameraReset`). It sees nothing: the wheel is about 550 m away.
4. `Menu = {}`, `Menu.movies = {[1] = "TRAILER", [2] = "L1_IN"}`, `Menu.movieID = 0`.
5. Two sprite batches, `L100_RM_PTank1 = GetPTank(786437, {0, 0, 0}, 1, 9000, 256)` and `L100_RM_PTank2 =
   GetPTank(1835027, ...)`: sprite words `0x000c0005` and `0x001c0013`, sheet-table records 12 and 28, the Rumble
   menu's sheets ([Front end](frontend.md#rm-layout)); `level95_clubhouse.lua` makes the same two before its menus.
6. The `Menu` functions (below), `AddObjects`, and the table `WonderWheelAnim` (`sceneName = "WonderWheel_100"`,
   callbacks `preload = "WonderWheelAnim:startScene"` and `finished = "WonderWheelAnim:finishedScene"`, the latter
   never defined nor passed) with `enable`, `start` and `startScene`.
7. `if not Objects then AddObjects() end`: **29 `ObjSpawn` calls**, all with the rotation `{0, 0, 0.707107,
   -0.707107}` (−90° about z): `dyn_s_neon_a`-`d` and `dyn_s_wwheel_a` (tint `0x474542FF`) at (515.51, −68.88,
   −188.65), and the carts `dyn_s_wwcart_simple_a`, `_b`, `_c` with `_01`-`_07` copies (tint `0x888888FF`) in three
   parked rows at z −208.59 (x 522.95-544.82; y −73.15, −65, −69.19); the scene moves them onto the wheel.

`WonderWheelAnim:start` puts the 29 handles in scene-slot order (a-carts 0-7, b-carts 8-15, the wheel 16, c-carts
17-24, neons 25-28) and marks itself playing, then, when `SceneIsPreloaded(name)` (loaded and idle), keeps
`GetSceneID(name)` (which no binding or script defines) as the id and calls `startScene` itself; otherwise keeps the
id `ScenePreload(name, "WonderWheelAnim:startScene")` returns. It does nothing while disabled. `startScene(id)` adds
the 29 objects (`SceneAddObject`) and calls `ScenePlayCinematic(id, 0, nil, false, false, true, false)`: no delay, no
end callback, no letterbox, not skippable, looping, the world not frozen; reached while disabled (a load arriving
after `enable(false)`), it instead unloads the scene when it is loaded and idle. `enable(on)` keeps the flag, always
loops `music/wonderwheel_132b`, then on `true` calls `start` unless already playing, and on `false`, when playing,
clears the playing mark and calls `SceneStop(sceneId)` without `force` (so false), which ends this scene at once
([Front end: stop and restart](frontend.md#background)). The lights come from `global.lua`'s matrix entry for level 100
([Front end](frontend.md#background)).

The `Menu` functions (inferred from the disassembly):

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
| `reloadProfiles` | fade out (1.0 s), `ScheduleFunc("Menu.reloadProfilesPostFade", 700)`, which calls `SSMC_StartLoadSequence` |
| `deleteProfile` | instant black (`ScreenQueueEffect(1, 0)`), `SSMC_StartDeleteSequence` |

`ScreenQueueEffect(type, seconds)` with type 0 fades in and 1 fades out ([Front end](frontend.md#fades) for the
timing and what a fade blocks).

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

**Who calls it.** The front end's `Menu.startGame` calls `runNextMission(1)` for a new or loaded story game. After a
mission, the mission-complete mode (0xb, `MissionComplete_Launch` `0x0015d420` pushes it) calls the Lua function
**`UnlockAndLoad`** from its `Enter` (`0x0015cf70`, the name at `0x0054f528`; confirmed (code)), a `global.lua` helper
(function 59) that calls `MissionCompleteUnlocks()`, releases the players' assets (`LiquidizeAssets`) and calls
`runNextMission(1)` (inferred from the disassembly). The failure and pause menus call `runNextMission(0)`
(`0x00155408`, `0x00155648`, `0x001557f8`: the name at `0x0054e798`, one number argument 0 or 1; confirmed (code)).

**The story order**, from this table and the hub's `fRunMission` ([The hub](#the-hub)); inferred from the
disassembly, and it matches the level records' `+0x0c` (`order` 1-18 in [Levels](../references/levels.md)). Every
level is entered at checkpoint 1 except the hub, whose checkpoint picks its chapter and its next mission:

| Mission | Level | Loaded by |
| --- | --- | --- |
| 1 | `level99` | a new game: `Menu.startGame`'s `runNextMission(1)` with nothing complete |
| 2 | `level80` | `runNextMission` after 99 |
| 3 | `level87` | `runNextMission` after 80 |
| hub | `level95` checkpoint 1 | `runNextMission` after 87 |
| 4 | `level34` | the hub, checkpoint 1 |
| hub | `level95` checkpoint 2 | `runNextMission` after 34 |
| 5 | `level2` | the hub, checkpoint 2 |
| hub | `level95` checkpoint 3 | `runNextMission` after 2 |
| 6 | `level3` | the hub, checkpoint 3 |
| hub | `level95` checkpoint 4 | `runNextMission` after 3 |
| 7 | `level5` | the hub, checkpoint 4 |
| hub | `level95` checkpoint 5 | `runNextMission` after 5 |
| 8 | `level81` | the hub, checkpoint 5 |
| hub | `level95` checkpoint 6 | `runNextMission` after 81 |
| 9 | `level86` | the hub, checkpoint 6 |
| 10 | `level93` | `runNextMission` after 86 |
| hub | `level95` checkpoint 7 | `runNextMission` after 93 |
| 11 | `level31` | the hub, checkpoint 7 |
| hub | `level95` checkpoint 8 | `runNextMission` after 31 |
| 12 | `level14` | the hub, checkpoint 8 |
| hub | `level95` checkpoint 9 | `runNextMission` after 14 |
| 13 | `level9` | the hub, checkpoint 9 |
| hub | `level95` checkpoint 10 | `runNextMission` after 9 |
| 14 | `level51` | the hub, checkpoint 10 |
| 15 | `level52` | `runNextMission` after 51 |
| 16 | `level54` | `runNextMission` after 52 |
| 17 | `level55` | `runNextMission` after 54 |
| 18 | `level84` | `runNextMission` after 55 |
| hub | `level95` checkpoint 11 | `runNextMission` after 84; the hub offers no next mission |

Outside this chain, the hub's trophies load the five flashback missions and the Armies of the Night levels
([The hub](#the-hub)), and `level64`, their last, returns to the hub at checkpoint 12. Since `MissionCompleteUnlocks`
ends with `LastLevel = findLastMission()` (inferred from the disassembly of `global.lua` function 60), the level
`runNextMission` looks up after any completion is the **latest story mission complete**, not the level just played:
finishing a flashback, or a story mission replayed from the hub's mission menu, returns to the hub at the story's
current checkpoint. Once `level64` is complete it is the first `findLastMission` finds, so from then on every
completion leads to hub checkpoint 12 (inferred, not observed). Which bindings each of these levels needs:
[Story coverage](../references/bindings/story.md).

#### Missions coverage {#missions-coverage}

Which [script bindings](../references/bindings/index.md) the story needs after `level99` is generated from the disc
by `coney-tools natives missions` onto [Story coverage](../references/bindings/story.md): for every later level
(missions 2-18, the hub, the flashbacks and Armies of the Night) the bindings it can call, the ones it is the first
to call, how far each is researched and whether Coney implements it. A level's bindings are those its chunks
(`levelNN.lua` and its `levelNN_*.lua` chapters, without the `_scenetest` and string files) name, plus those of the
`global.lua` helpers they reach and the engine's two callbacks, the same reach rule as
[`natives mission1`](../references/bindings/mission1.md) (an upper bound). Where each family's behaviour is
described:

| Family | Behaviour |
| --- | --- |
| character | [Characters](characters.md), [Combat](combat.md) |
| world | [World objects](objects.md), [World flags](flags.md) |
| level | [Level loading](level-loading.md) |
| ai: goals and actions (`Goal*`, `Act*`) | [AI](ai.md#scripted) |
| gang | [AI](ai.md#gangs) |
| hud | [HUD](hud.md) |
| config | [Config bindings](../references/bindings/config.md) |
| sound | [Sound](sound.md) |
| effects | [Particles](particles.md), [The streamed world](world.md#fog) |
| camera | [Camera](camera.md) |
| ai: tactics (`Tactic*`) | [AI](ai.md#tactic-kinds) |
| ai: brains and the rest (`Br*`, ...) | [AI](ai.md#brain) |
| scene | [Scenes](scenes.md) |
| script | this page |
| util, debug, input | this page |

### The hub (`level95`) {#the-hub}

`level95` is the **Warriors' clubhouse and the streets of Coney around it**, where the story returns between
missions: its functions set up a clubhouse (`SetupClubhouseEnvironment`, `AddClubhouseFlagsBoxesPaths`) and Coney
(`SetupConeyEnvironment`), and it loads `level95_clubhouse.lua` (the clubhouse: the Warchief, the Warriors and their
girls at their spots, workouts, the trophies that start flashbacks, the mission and Rumble menus) and
`level95_coney.lua` (preLoadFile with `env.FinishClubhouseLua` / `env.FinishLoadConeyLua`). Inferred from the
disassembly. Its `Main` loads **`level95_chapter<checkpoint>.lua`** (12 files, names recovered by their CRC) with
the callback `RunLevel`; each chapter sets the numbers of Warriors and girls, the radio track, the Warchief's type
(`WarchiefTable`) and the chapter's **mission actions** (`MissionAction`: red circles, cut-scenes, `MA_MISSION`
errands, ending in `MA_LOADLEVEL`). `MA_LOADLEVEL` calls `story.LoadLevel(fRunMission[checkpoint].level)`
(`level95.lua` function 55), which stops the music, banks a live player's assets (`LiquidizeAssets(player, true,
true, true, true)`), sets checkpoint 1, starts a save (`SSMC_StartSaveSequence()`) and calls
`MenuLoadLevel("level" .. level)` (inferred from the disassembly):

| Hub checkpoint | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Next mission | 34 | 2 | 3 | 5 | 81 | 86 | 31 | 14 | 9 | 51 | none | none |

**Flashbacks**: the clubhouse's trophies (`flashback.PlayMission`, action `MA_LOADFBLEVEL`) load `FBMission[k].level`:
`level82`, `level92`, `level83`, `level20`, `level11`, and `level60` once those five are complete (its `req`). Levels
60-64 are the **Armies of the Night** bonus game (`SndLoadMatrix("armies")`, the `_a` models): each one's last
checkpoint script loads the next (`level60_mothers` → `level61`, ... `level63_moonrunners` → `level64`). The mission
menu itself (`menu.BeginChosenLevel(level, checkpoint)` in `level95_clubhouse.lua`) clears the save data, calls
`SetCheckPoint(checkpoint)`, starts a save and `MenuLoadLevel("level" .. level)`. Inferred from the disassembly.

**Not the hideout:** `level2_clubhouse.lua` is checkpoint 3 of story mission 5 (`level2`), a clubhouse full of
Orphans (`orph_*` models named `Clubhouse1` ...): another gang's (inferred).

### Cameras from Lua {#cameras-from-lua}

`CameraCreateFollow(name, target)` is a `global.lua` helper (function 15), not a binding: it calls
`CamSetupFollow(name, target)` and then `CfgFollowCamera(FOLLOWCAM_MINDIST, FOLLOWCAM_MAXDIST, FOLLOWCAM_DEFAULTDIST,
FOLLOWCAM_DEFAULTANGLE, FOLLOWCAM_FOV, FOLLOWCAM_NEARPLANE, {FOLLOWCAM_OFFSET_X, _Y, _Z}, FOLLOWCAM_SLOWMO)` and
returns the camera's handle. `global.lua` sets these globals to **3, 6.6, 4.8, 13 (degrees), 65, 0.1, (0, 0, 1.4)
and 0.2**; `CameraNormal()` applies them again to `MainCam`. Inferred from the disassembly; the values are confirmed
(runtime) in the camera object ([Camera](camera.md#the-follow-camera-object)).

### Scenes and movies {#scenes-and-movies}

- **The scene list.** Boot step 13 (`0x003535e8`, [Boot](boot.md#main)) loads `scene_list.cnk`, and `0x00353460`
  copies its Scene List chunk (type `0x43`): a u32 count, then 24-byte records `{u32 id, u32 size, char name[16]}`,
  to `0x006eba10`, the count to `0x00512aec`. Confirmed (code). On the NTSC-U disc there are 2,765 records, each id
  its own index, one per `.scn` record: 1,240 scene headers (24 of them under names cut to 16 characters) and 1,525
  segments of long scenes. The records, the slots and playback are on [Scenes](scenes.md).
- **`ScenePreload(name)`** (`0x00353f88`) takes the id `0x00353698` finds: the **first** record whose name
  *contains* `name` (`0x00435d30` is a case-sensitive `strstr`), or 0 when none does; then `0x00353af0` loads it into
  a free slot of the 12 unless it is loaded or loading already. Confirmed (code). The scene id is global, not per
  level. Each of the 187 names the scripts pass finds its own record on the NTSC-U disc (no shorter name hits an
  earlier record). Lookups by exact name (`0x00353778`, `strcmp`) and by id (`0x00353730`) exist too, and C++ code
  calls `0x00353698` itself (the AI, `0x002cb5d0`), so not every scene is preloaded by a script.
- **Movies.** `PlayMovie(name, flag)` → `Movie_Play` (`0x0042a938`) opens `PSS\<name>.BIK` (format `0x0058bfe0`),
  confirmed (code). Who names them: the boot (`LOGO`, `PLOGO`, `L1_IN`), a level record's intro `L%d_IN`
  (`0x00550160`) and outro `L%d_OUT` (`0x0054ed20`) ([Level loading](level-loading.md#the-level-record)), and the
  front end's `Menu.movies` (`TRAILER`, `L1_IN`). The disc's `PSS` folder holds exactly those 16 movies. How a
  movie plays: [Movies](movies.md).

Both are listed in [Scenes and movies](../references/scenes.md).

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
the camera over and gives it back ([Camera](camera.md#scenes)).

**Chapters and checkpoints.** The three chapter scripts run one after another in the same level, without a reload:
the last step of each calls `SetCheckPoint(n + 1)` and `preLoadFile` on the next chapter's script with its set-up
function (`P1.Cleanup` → checkpoint 2, `level99_lesson1`, `P2.SetupLesson1`; `P2.NavigationSection` → checkpoint 3,
`level99_lesson2`, `P3.SetupLesson2`). Inferred from the disassembly. `SetCheckPoint` (`0x0041ce98`) stores the number
(`W_GameState + 0x33a`) and takes a **checkpoint copy** of what a restart needs, confirmed (code): the inventories
(`W_GameState + 0x484`, 0x7e8 bytes, copied to `+0x7ec`, `0x0041e0b8`), the stats (`0x006fe490`, 0x180 bytes, copied
after themselves, `0x00422c60`), and the object manager's 35-word list (`0x00715780` → `0x007156f0`, the source
cleared) with the checkpoint and the level index (−1 unless the level is `level34`, `0x00397e88`). The checkpoint
picks the chapter only when the level is entered (`Main`, above); `RunLevel` also has a branch that preloads
`level99_scenetest` (`PlaySceneTest`), a test path with no script of that name on the disc (inferred).

**Triggers in this mission** (counted from the scripts): 16 volume boxes, whose message 3 starts most steps (`vMark01`
→ `P1.FirstGlow`, `vClimb` → `P3.MoveToClimb`, ...); message 8 on four flags (an AI teacher arrived); messages 1, 3, 4
and 16 on the dealer, whose trigger sphere gives 3 and 4; 18 on a civilian and, through `GangSetMsgHandler`, on a gang;
2 on two breakable fences; one 50-second stopwatch (`P1.SendWarriors` → `P1.TimesUp`); about 60 scheduled calls; the
tutorial's attack callback (`HUDSetTutorialCallback`, [HUD](hud.md#tutorial-callback)), animation callbacks
([Characters](characters.md#anim-callbacks)) and pad handlers (`PadSetHandlerEx`). How each is delivered:
[Message handlers](#message-handlers), [Trigger boxes and spheres](#triggers), [The mission stopwatch](#stopwatch),
[Scheduled calls](#scheduled-calls). No script of the mission launches a failure: the players are demi-gods (below)
and the only failure the engine raises by itself is a player falling out of the world
([Level loading](level-loading.md#a-frame-of-play)).

**Demi-god mode.** `HuSetDemiGodMode(player, true, 0.25)` sets flag `0x20000000000` and stores 0.25 in one global
(`0x0051024c`) shared by every human. When a hit would take such a human's health below that fraction of its
maximum, its health is set to the fraction × maximum and flag `0x10` (god mode) is set, so it takes no more damage
until a script clears it (`Human_ApplyPendingDamage` `0x00265f70`, and `0x00256f28`, which sets `0x10` once health is
at or below the fraction). Confirmed (code); [Combat](combat.md#damage-table) has the rest of the damage order.

**How the mission ends** (the Lua side inferred from the disassembly, the C++ side confirmed (code)):

1. `P3.UseFlash` (the player uses the flash on the dealer) sets the last objective and schedules `P3.MissionOver`,
   which runs `SuperRunScene` with a scene table whose `Final` is true and `ReturnFunc` is `P3.MissionCompleted`.
2. When that cinematic ends, its end callback, `global.lua`'s **`PreCashTheWorld(sceneId)`**, sees `Final`: it queues
   screen effect 1, calls the `ReturnFunc` (which restores the default follow slots and re-enables command 1), clears
   the scene's table entry and calls **`HUDLaunchMissionComplete()`** with no argument. (Without `Final` it would
   show the scene's `HudText`, call the `ReturnFunc`, and switch spotting, the stage lighting and the fade back.) How
   the scene system plays and ends the cinematic: [Scenes and movies](#scenes-and-movies).
3. `MissionComplete_Launch(0)` (`0x0015d420`) finds mode 1 on top, **pushes mode 0xb** and sets the level-change flag
   `0x0050c754` = 1 (read by mode 1's `Exit`, [Leaving gameplay](level-loading.md#unload)). Mode 1 is suspended, so
   its level-end countdown does not run; `W_GameState + 0x14c` stays 0 until `MenuLoadLevel`.
4. Mode 0xb's `Enter` stills every live human and calls **`UnlockAndLoad`**: `MissionCompleteUnlocks()` unlocks the
   level (`UM_Unlock(Level, 0, 0)`) and one more record chosen from the players' scores (`StatGetScore`);
   `LiquidizeAssets` adjusts each player's inventory (money, revives, spray-can charges, keys, item 5) and resets
   their stats (`StatResetPlayer`); and `runNextMission(1)` finds 99 complete and loads **`level80`**
   at checkpoint 1 (`SetCheckPoint(1)`, `SoundStopMusicTrack()`, `MenuLoadLevel("level80")`, which sets
   `+0x14c` = 3 and mode 8's next level), then `HUDLaunchMissionComplete(4)`, which only stores kind 4 since mode 0xb
   is on top.
5. Mode 0xb's `Update` (`0x0015d160`) runs one world frame, sees the kind (4, none of 1-3 it acts on), pops itself,
   calls the save system's slot `+0xb4`, **pops mode 1** (now on top: its `Exit` unloads `level99`), banks both players'
   money and asks for the **autosave** (`0x00155308`, mode 6). Mode 8 then loads `level80`
   ([Front end](frontend.md#story-start) describes the same modes on the new-game path).

The level-end countdown (mode 1 `+0x28`, `W_GameState + 0x14c` = 1 or 2) is therefore not used by this ending; it
serves `MissionComplete` (2, which no shipped script calls) and failures (1). Messages stop reaching Lua as soon as
`+0x14c` is not 0 ([Message handlers](#message-handlers)).

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
[`HUDLaunchMissionComplete`](../references/bindings/hud.md#hudlaunchmissioncomplete), among the bindings the first
mission can call; [Mission 1 coverage](../references/bindings/mission1.md) lists them all with how far each is
researched and whether Coney implements it (`ShowHud`, for one, does nothing in this build).

#### The combat tutorial's lessons {#level99-lessons}

`level99_combat.lua` runs twelve sections in order from a
table, each started by the previous one's end (directly, or scheduled 1-2.5 s later). A lesson that teaches a move
sets `HUDSetTutorialCallback` and counts the anim ids the [callback](hud.md#tutorial-callback) passes, which is the
attacker's id when the **victim applies the damage**, so only moves that hurt someone count. Inferred from the
disassembly of the script (ids and counts only); the moves themselves are confirmed where linked.

| # | Section | Waits for (anim ids, in order) | Move | Researched |
| --- | --- | --- | --- | --- |
| 1 | camera, intro | flags and scenes | | [Scenes](scenes.md) |
| 2 | basic attacks | 12; 11; 51 or 53; 55; 219 or 221; 223 | square, cross, grab strikes, mount strikes | [Combat](combat.md#grabbing), [mount](combat.md#mount) |
| 3 | targeting | the `PadSetHandlerEx` handler | L1 | [Input](../references/bindings/input.md#padsethandlerex) |
| 4 | light, then heavy combos | 12, 16, then 19 or 20; 12, 15; 11, 13; 12, 16, then 17 or 18 | chains | [Attacks](combat.md#attacks) |
| 5 | strong attacks | 653 or 655; then **82 or 84** | cross + square; circle + cross | [Specials](combat.md#run-attacks), [Strong grapple](combat.md#strong-grapple) |
| 6 | power moves | 57; then 59 | in a front grab, cross + square, then square or cross in its window | [Power strike](combat.md#grabbing) |
| 7 | snaps (after the scene `l99_c7`; Rudy drinks at the fence, the second wave surrounds the player) | three hits of 25, 27 or 29 | square, standing, with the stick past 0.95 more than 45° off the facing, at a bum within 2 m there | [Attacks](combat.md#attacks), [the second wave](ai.md#level99-snaps) |
| 8 | throws | two hits of 147, 149, 151 or 153; then zone 1 is enabled | circle with the stick in a grab | [Throws](combat.md#throws) |
| 9 | weapons (after the scene `l99_c8`, three bats on the ground) | the pick-up (message 0 on a bat, with a prompt); then 34; 36; then two more of 34 or 36 (four counted hits) | triangle at a bat; square, cross | [A bat in hand](combat.md#bat), [the prompt](#message-handlers) |
| 10 | rage (2 s after lesson 9) | the meter set to half and locked, then filled by hits; the full callback; L1 + R1 (every other command off); 3.5 s into rage, 645 or 647; 3.5 s later, 65 or 233 | rage, the rage special, the extended power move in rage (grab or mount) | [Rage](combat.md#rage); 65 and 233 inferred |
| 11 | finish the second wave | the gang's message 18 with none standing | any | [AI](ai.md#level99) |
| 12 | the Warriors (after the scene `l99_c5`) | the 50-second stopwatch; a hint the first time the player holds a grab (anim callback on 82 / 84) | any | [Stopwatch](#stopwatch), [anim callbacks](characters.md#anim-callbacks) |

Then the scene `l99_c6` and `P1.Cleanup` (checkpoint 2). Coney's disc test checks lessons 1-10, driven by mission
1's play-through pad script (`repo:tests/platform/disc_level99_course_test.cpp`). The pad script is frame-locked, so
lessons 11-12 (the second wave and checkpoint 2) wait for an adaptive driver: any change to the AI's movement moves the
bums. Points an implementer needs:

- **6**: from the rear hold the hit is scored with the spin's id 80, not 57, so only a front grab passes the first step.
- **10**: the rage special is cross + square while raging (645 / 647); the last step wants the extension of the rage
  power strike in a grab (63 → 65) or of the rage power strike in the mount (231 → 233); both use the + 2 of
  `Player_UpdatePowerMove` (inferred for the mount). **What arms `P1.RageMoves`** (the step order):
    1. `P1.WeaponsDone` clears the tutorial callback and starts the section 2000 ms later.
    2. `P1.SetupRageMode` makes rage gains possible again (`HuSetPreventRage(false)`). It sets the meter to 0.5 and
       locks it, and it registers the rage handlers: enter `P1.EnterRageMode`, exit `P1.ExitRageMode`, full
       `P1.CheckRage`, 20000 and 5000 ms. It arms **no** tutorial callback.
    3. The meter fills only from the player's own hits. The lock stops the drain but not `Human_AddRage`
       ([Rage](combat.md#rage)), so the player must earn the other half (39 of class 6's 78) by hurting someone.
    4. When it is full, `Human_AddRage` calls the full handler once, `P1.CheckRage(player, true)`. That shows the
       next text, drops his weapon, turns every command off and turns command 31 (L1 + R1) back on.
    5. Pressing L1 + R1 starts rage, which calls `P1.EnterRageMode(player, …)`. That turns every button back on except
       commands 1 and 40, sets the rage protections, and schedules `P1.EnableMoveCheck` for **3500 ms** later.
    6. `P1.EnableMoveCheck` is what calls `HUDSetTutorialCallback("P1.RageMoves")`.
    7. In `P1.RageMoves`, step 1 (645 / 647) clears the callback, schedules `P1.EnableMoveCheck` 3500 ms later again,
       and moves to step 2. Step 2 (65 / 233) calls `P1.RageDone`. Any other id is ignored.

  So RageMoves arms 2 s + the fill + the L1 + R1 press + 3.5 s after the fourth bat hit. In the passing run that was
  631 frames (4260 → 4891). An L1 + R1 pressed before the meter is full, or before command 31 is the only one on,
  does not start rage, and nothing re-arms it until the meter is full. Inferred from the disassembly of the script
  (`main/46`-`main/52`); the game side is confirmed where linked.
- **9**: the scene's return function schedules the setup 50 ms later. The setup gives each of the three bats a
  message-0 handler with the lesson's prompt, places the players in the pen, sets up the fence's enemies and starts a
  check that runs every 250 ms. The handler, on the first bat taken, removes the handler and prompt from all three,
  arms the tutorial callback and returns nothing, so the game's own pick-up puts the bat in hand ([A bat in
  hand](combat.md#bat)); no binding does it. The check asks `HuGetHeldObject`: holding, it shows the lesson's current
  text once; empty-handed after holding, it shows a "pick it up again" text; it stops rescheduling once the lesson is
  done. A dropped bat has no prompt any more, but triangle's plain search still takes it (bats are
  [pickable](objects.md#pickable)). Inferred from the disassembly of the script; the game side is confirmed where
  linked.

  **The steps** (`P1.Weapons`, `main/45`). It keeps `wAnims` = {34 `ANIM_BAT_COMBO_S1`, 36 `ANIM_BAT_COMBO_X1`},
  `weaponCount` = 1 and `multiCount` = 1. Each call is one hurting hit:
    1. While `multiCount` is 1, only `wAnims[weaponCount]` counts. Step 1 wants 34 and step 2 wants 36; anything
       else, a 36 before the first 34 included, is **ignored**, not a reset. Step 1 → `weaponCount` 2,
       `CombatEnemy.GenWarrior`'s line `l99_t1_052` and the next text. Step 2 → 3, the next text, and `multiCount` = 2.
    2. While `multiCount` is 2, any 34 or 36 adds one and says `l99_t1_053`. At `weaponCount` 5, so on the **fourth**
       counted hit in all, it sets `weaponDone` and calls `P1.WeaponsDone`.

  There is **no re-arm delay** between steps: the callback stays armed throughout, and the next counted hit can come
  on the next frame. Lesson 10's 3.5 s gaps are its own (above). So 34, 36, 34, 36 ends the lesson on the fourth
  hit.
- **7**: `P1.StartSnaps` (5 s after the set-up) arms `P1.Snaps` with `ANIM_SNAP_RIGHT_01`, `_LEFT_01` and
  `_BACK_01` (25, 27, 29). Any of the three counts, in any order and on any bum; any other id (an `S1`, 12) is ignored.
  The first two update the text and give Rudy a line; the third calls `P1.SnapsDone`. Confirmed (runtime):
  snaps 27, 29 and 25 that landed on the bums each reached the callback once, with that id. Coney passes it with the
  disc ([the second wave](ai.md#coney), [the snap](combat.md#coneys-implementation)).
- **2, 6, 8**: a plain grab scores nothing; strikes, throws and power moves are scored with their own ids when they
  start, since a grab move applies its damage on its first update.

#### The hints in order (runtime reference) {#level99-hints}

The texts the hint box showed over one play of checkpoint 1 on the original (2026-10-07, the copy
`l99-tutorial` of lesson 1 with `TT_20a` already up, driven over PINE as Coney's `CourseDriver` plays it), by
`LEVEL99` key. "Shown" is the moment `HintBox_Update` takes the text off its queue (`0x001cdf7c`), which can be later
than the `HUDSetTutorialText` call: a text with `<DISPLAYTIME n>` holds the box for *n* ms first. Updates count from
the copy's first; the gaps are where the driver was stopped and the game idled, so only differences within one
stretch mean anything. The hooks are `lua-exec`, `c-call`, `c-call-args`, `game-hint` and `hint-show` of
`repo:research/traces/patches.toml`. Confirmed (runtime).

| Update | Lesson | Shown (and what the script set) |
| --- | --- | --- |
| 0 | 1 | `TT_20a` (marker 01 shown) |
| 164-165 | 1 | marker 01 entered: `dyn_w_mission02` destroyed, `TT_20c`, `dyn_w_mission03` shown |
| 280 | 1 | marker 03 entered: `dyn_w_mission03` destroyed, the text cleared twice; then the intro scene (per the lesson table) |
| 497-866 | 2 | callback `P1.BasicAttacks`; `TT_22a`, `TT_22b`, `TT_19a` (the grab step), `TT_22a`, `TT_22b`, `TT_19c` (the mount step), `TT_22a`, `TT_22b` |
| 884-929 | 2 | callback cleared; `TT_19b` |
| 1009-1239 | 3 | `TT_26a` (`<DISPLAYTIME 3000>`), then `TT_26b` **91 updates later** (set at 1009, shown at 1100); pad handler `P1.Target` at 1129, cleared at 1239 |
| 1357-1726 | 4 | callback `P1.LightCombos`: `TT_23a`, `TT_23b`; `TT_23e` with callback `P1.HeavyCombos`, `TT_23f`, `TT_23d`; callback cleared |
| 1756-1916 | 5 | `TT_24a` with callback `P1.Power`, `TT_24b`; callback cleared at 1916 |
| 1916-2278 | 6 | `TT_24d` with callback `P1.PowerMove`; `TT_24e` after the first power strike; callback cleared at 2278 |
| 2508 | 7 | `dyn_beerbottle` destroyed, `TT_25a`; then the scene `l99_c7` (per the lesson table) |
| 2659-2857 | 7 | `TT_25c` with callback `P1.Snaps`, shown again after each counted snap |
| 2993-3188 | 8 | callback `P1.Throws`; `TT_25e` (shown again after each counted throw); callback cleared at 3188 |
| 3390 | 9 | `TT_25h` (take a bat), after the scene `l99_c8` (per the lesson table) |
| gap | | |
| 19654-19903 | 9 | the bat taken: callback `P1.Weapons`; `TT_25i` (16 updates later), `TT_25j`, `TT_25k`, `TT_25k`; callback cleared |
| 19963 | 10 | `TT_28a` (60 updates after lesson 9's end) |
| 20745 | 10 | `TT_28b` (the meter full, `P1.CheckRage`) |
| gap | | |
| 35827-36588 | 10 | `TT_28e` (`<DISPLAYTIME 3000>`), `TT_28f` 93 updates later, callback `P1.RageMoves` at 35933; `TT_28h` and the callback cleared at 36156, `TT_28d` 91 updates later, callback `P1.RageMoves` at 36261; cleared at 36588 |
| 36648 | 11 | `TT_30` |
| 38396 | 12 | `TT_40` |
| 40057 | | `SetCheckPoint(2)` and the two `HUDSetObjective(2, …)` lines |

Points a playthrough test needs:

- **The lesson texts** are the ones in the table above that are not `TT_19*`. They come in this order on every play.
- **`TT_19a`, `TT_19b`, `TT_19c` and `TT_19e`** are the grab hints: tap circle to grab, L2 to let go, hold circle for
  the mount, circle to pick up a downed enemy. The lesson handlers show them on a step that needs a grab. The same
  texts also show whenever the player's state calls for them. In lesson 6, a circle at a bum lying on the ground
  showed `TT_19e` and went into the pick-up, where the power strike does not start. So how often they show depends on
  the driver: assert the lesson texts in order, and allow the `TT_19*` texts between them.
- **Repeats**: `TT_25c`, `TT_25e` and `TT_25k` are shown again for each counted move. A test that checks the order
  should merge consecutive repeats.
- **The `<DISPLAYTIME>` texts** (`TT_26a`, `TT_28e`, `TT_28h`) hold the box for about 90 updates. The next text set
  with them waits in the queue that long.

#### Checkpoints 2 and 3 {#level99-checkpoints}

`level99_lesson1` (`P2`, checkpoint 2, the street) and `level99_lesson2` (`P3`, checkpoint 3, the run to the
dealer), in order. Each step is started by the trigger in its row; the mechanics are on the linked pages. Inferred from
the disassembly of the scripts (names, ids and values only).

| Step | Started by | What the script sets up | Waits for | Mechanics |
| --- | --- | --- | --- | --- |
| 2.1 set-up | `P2.SetupLesson1` | the Warriors remade for the checkpoint, flash command 40 off, both players at 35 % health and demi-god 0.25, scene `l99_c2` | the scene's end | [Scenes](scenes.md) |
| 2.2 store | `P2.EndStreetScn` | Vermin's talk prompt (message 0), `CfgInventoryCallback(P2.UpdateLootCount)`, `vInsideStore` message 3 | three items of item 10 | [Breakables](combat.md#breakables), [inventory callback](player-state.md#pickup-callback) |
| 2.3 car | `P2.SetupCars`, 3 s after the third item | `CarMakeGoodAsNew` and `CarSpawnRadio` on the three cars, the locked camera `VerminCar`, Vermin's line; then `CfgSetSteroTheftHandler(P2.CarRadioStolen)` | the first stereo a player steals (`radioNeeded` 2 is never read) | [Cars: windows](cars.md#windows), [the theft](combat.md#stereo-theft), [context kind 3](crimes.md#context-records) |
| 2.4 mugging | `P2.SetupPeds`, 3 s later | `PoizoCiv` sent to the phone flag `wPhone01` (dead brain, `GoalMoveToUseFlag`), the bums (`GoalBumLogic`, `GoalMoveToUseFlag`), `HuSetMugCallback(P2.PedMugged)` on both players, `SetInterrogateParam` (below), message 18 on `PoizoCiv` (`P2.MugPedDied`: a hint only); after 3 s `FlagNetTraverse(PoizoCiv, 1, 0)` walks him off | the callback with success true; then all-zero `SetInterrogateParam` | [Mugging](combat.md#mugging), [who and the callback](crimes.md#mugging), [AI](ai.md#scripted) |
| 2.5 Vermin | `P2.SeeVermin`, 5 s later | the camera `VerminWait`, `ObjShow(dyn_objective_w)`, Vermin a radar objective (texture 27), command 38 on after 2.5 s | `vFenceSection` message 3 → `SetCheckPoint(3)`, `level99_lesson2` | [HUD](hud.md) |
| 3.1 set-up | `P3.SetupLesson2` | the dealer `FlashDealer` with `GoalDealer(dealer, 0, 17, 0, 0, false)` and messages 1 and 16 (`P3.DealerHit`), Vermin (god mode, unpushable, ungrabbable, fast climber), scene `l99_c3` | the scene's end | [GoalDealer](ai.md#dealer) |
| 3.2 fences | `P3.EndChapterScene` | `DoorOpenDegree(WXL00, 100)`, command 40 off and 38 on, Vermin (secondary camera target) to `fVerminFencePoizo` | `vFenceJump` 3, the flag's 8, then `vFenceJump2` / `3` texts | [Climbs](characters.md), [AI fence climb](ai.md#path-planning) |
| 3.3 climb | `vClimb` 3 (`P3.MoveToClimb`) | Vermin to `fStop2`; `P3.GetBack(1)` 10 s later flashes his radar icon while the player is lost | `fStop2` 8 → the camera `ClimbPoizo`, the players put at `fClimbWarp`; `vFirstClimb` / `vDoneClimb` 3 | [Characters](characters.md) |
| 3.4 rooftops | `vReachRooftop` / `2` 3 (`P3.PoizoJump`) | `ObjShow(dyn_w_mission)`, the camera `JumpCam`, the players put at `fRoofTeleport`, radar target `fRadarJump` | `vReachWindow` 3 (both players with two, below), then Vermin's run to `fStop3`, his line, `vVerminJumps` 3 | [Jumps](characters.md#jump), [Moving into a pane](objects.md#pane-break) |
| 3.5 wooden fences | `vAlmostDone` 3 (`P3.WoodenFences`) | the players put at `fBreakFencePoizo` (a held object destroyed), two locked cameras, message 2 on `dyn_door_fence02` and `03` | either fence's message 2 (`P3.FenceBroken`) | [Barriers](objects.md#barriers) |
| 3.6 dealer | `P3.DealerPoizo`, 0.5 s later | `CfgSetGlobalTimeToLive(1000)`, money raised to at least $20, the dealer's icon `dyn_flashdeal`, `TriggerSphereCfg(dealer, true, 4, 2, 500)` with messages 3 / 4, the scene `l99_c9` preloaded | message 3 on the dealer → `P3.CheckForFlash` every 0.5 s until `InvNumberOf(1)` ≥ 1 | [Buying](ai.md#dealer), [trigger spheres](#triggers) |
| 3.7 flash | `P3.FlashSpeak`, 3 s after the buy | `CfgSetGlobalTimeToLive(10000)`, command 40 on, `PadSetHandlerEx(P3.UseFlash)` | command 40 (d-pad right) → `P3.MissionOver` 1.5 s later | [The flash](combat.md#rage), [How the mission ends](#level99) |

Points an implementer needs:

- **2.4**: `SetInterrogateParam(160, 75, 255, 5000, 2500, 20000, 40, 60, 20000, 0, 0)` makes the mugging 5 s on
  target with 20 s off target allowed ([Mugging](combat.md#mugging)). The mug callback runs for every end of a
  mugging; the lesson ignores a false one.
- **3.6**: a player hitting the dealer (message 1 or 16) fades out, removes his gang and makes him again after 2 s,
  the players put back at `fHitDealer`. Triangle in the dealer's 1.75 m buys ([Buying](ai.md#dealer)): $20 for one
  flash, refused at 3 carried; a dirty dealer would take the money and give nothing.
- **3.7**: the last step waits only for the command; the flash itself heals the player when he is hurt (he is at 35 %
  from 2.1) and does nothing at full health without upgrade (6, 8).
- **3.4**: the jump is the player's own ([Jumping](characters.md#jump)) and the script never touches the window: the
  two type-11 panes in it break when the jumping player reaches them ([Moving into a pane](objects.md#pane-break)).
  `P3.JumpPoizoDone` arms `vReachWindow` (corner 43.2, −4.3, 4.1, size 4 × 6 × 4, just past the panes at x 47.4)
  with `P3.ReachWindow`, which counts only players. With one player, his entering removes the handler and runs
  `P3.Player2Jumps`. With two, each player who enters is set brain-dead (`BrDead`, so he gives up the pad,
  [AI](ai.md#handlers)) and counted; the second removes the handler and runs `P3.Player2Jumps` (player 2 arriving
  first sets `bWindowWait`; if he then drops out, player 1 still has to enter). `P3.Player2Jumps` marks the
  objective done, flushes the Warriors' brains and sets the gang dead (the player is `Warriors.Rembrandt`, so he too
  gives up the pad), gives each player `GoalAddressPerson` towards Vermin, turns the follow camera off with
  Vermin as its secondary target, destroys the marker `dyn_w_mission` and sends Vermin to `fStop[3]`
  (`P3.VerminInWindow` on arrival). A jump enters the box in the air (at about x 47.2), so the pad is lost
  mid-jump and the jump finishes on its own. Confirmed (runtime) in the jump of
  [Moving into a pane](objects.md#pane-break), one player, the stick held at 100 % throughout: he landed with 435,
  the jump end for a centred stick (436 follows a held one, [Characters](characters.md#jump)), slid to a stop 0.57 m
  on, then turned on the spot about 164° (heading 88° to −108.6°, clip 398) in 14 updates and stood idle. If player
  2 drops out while player 1 waits at the window, `level99.lua`'s drop-out handler runs `P3.Player2Jumps` itself.
- **3.5**: who sends a barrier's script message 2 is not traced; its class update (`0x003b3220`) reports done once the
  barrier has broken (data `+0x00`), which is inferred to make the object system send it.

### `level34.lua` (mission 4) {#level34}

Story mission 4, loaded from the hub's checkpoint 1: the riot. Vermin is player 1, Snow player 2, Ash the third
Warrior. Read from the disassembly of `level34.lua` and its three chapter scripts; the bindings each call reaches are
on [Story coverage](../references/bindings/story.md#level34). Inferred unless marked.

- **`Main`**: fog colour, HUD hidden then restored with the radar, `SetSpawnMax(45)`, the start callback `StartGame`,
  then the checkpoint table: checkpoints **1, 2 and 3 all run `level34_riot`** (`P1.SetupRiot`), each with its own
  Warriors set-up at a different spot of the street; 4 `level34_gate` / `P2.SetupGate`; 5 `level34_park` /
  `P3.SetupPark`.
- **`RunLevel`**: at checkpoints 1-3 stores the checkpoint as Lua save float 1 (the riot's **section**, 1-3), then
  reads floats 1-5 back (`F.LoadSaveData`: section, riot done, radios done, loot done, muggings so far); the
  Warriors; `preLoadFile` of the chapter; the pedestrian flag net (`SetupFlagNet`, 34 nodes, as in
  [`level2`](#level2)); the follow camera; car colours from a 21-colour table; `CarPlaceInTrunkOnDetach` on four
  cars (a crowbar, a bat, item 5, a revival); the anim preloads; the tag spots (`TagInfo.SetupTags("Tag", 7,
  checkpoint)`).
- **`StartGame`** (the start callback): at checkpoints 2-4 the riot ambient track, the 3D fog (`Start3DFog`) with
  `MaxFogParticles(10)` and a screen effect; at 4 also `ForceCrimeLevel(true)` and a crime at the cop search flag
  (`CrimeIsHappening`, type 7), so the police are already after the Warriors; at 5 the distant-traffic track; from 3
  on the molotov car is already blown up (`CarExplode`); every light off, then the subway lights (checkpoint 1) or
  the wall light.
- **The riot's three objectives** (objective line 0; each one completed is saved as its target in a Lua float and
  shown as done by `P1.CheckRiotStatus` on a reload; partial progress is not saved):
    - **Riot meter** (a HUD bar, `HUDGetNewPH(3, ...)`, its text in per cent): 200 points. `vLevelVandal` hears
      [message 6](#triggers) (a human standing in it damaged something; only a Warrior counts): a newsstand gives
      3, another world object (`GetRTTI` 8) 1, a glass pane (1024) 2; every car message 25 from a Warrior
      with its flag true (`SetGeneralCarMsgHandler`) gives 3. The first point ever counts as 1 %. At 35 % a tutorial line.
    - **Loot**: $350 of money picked up (`CfgMoneyCallback`; the start counts both players' money at that moment).
      On a reload with the loot not done, both players' money is set to 0.
    - **Car radios**: 3 picked up (`CfgInventoryCallback`, item 11 `dyn_carstereo`), from the three cars given a
      radio by `CarSpawnRadio`.
    - **Bonus**: 10 pedestrians mugged (`HuSetMugCallback` on both players, successes only) on line 1 with a counter;
      the tenth unlocks `UM_Unlock(34, 1, 2)`. The count is saved (float 5) at every checkpoint.
    - All three done (`F.CheckObjectives`) runs **`P1.HeadToGate`**.
- **No scripted failure**: none of the four chunks calls `HUDLaunchMissionFailed` and the script protects no one; the
  mission fails only in the engine's own ways (a player falling out of the world,
  [Level loading](level-loading.md#a-frame-of-play)).

| Checkpoint | Chapter | What happens | Ends with |
| --- | --- | --- | --- |
| 1 | `level34_riot`, section 1 | Riot extras (`GangRiotCivs1`, suspended until the intro ends), six roof throwers (`GoalStationaryThrower` with bottles, speakers, TVs, boxes and drum kits; one hit by a Warrior fights), the dealer factory (flash and paint), riot and wanted gangs with turf boxes, the police spawner, 17 object zones on. The intro scene `l34_c1` (the three Warriors and the subway door; callbacks 1 and 2 switch lights); at its end the riot ambient at 0.15, tunnel effects and an objective with a radar marker out of the subway. `vIntro` (a player) wakes the car flippers and plays the preloaded scene `l34_car_flip`, after which the loot objective starts and the flippers riot (`GoalRiot`). `vEnterRiot` (a Warrior) starts the riot meter, the fog and the full ambient. `vStartIntroLooters` sets a looter gang wrecking zone 51 (`TacticVandalize`); two looters talking (trigger sphere 10 m), two TV looters who fall out and fight, a weapon-break tutorial box | `vSection2` entered by a player: muggings and tags saved, float 1 = 2, **`SetCheckPoint(2)`**, section 1's encounters removed, section 2 set up. Finishing the riot meter or the loot while still in section 1 also calls `SetCheckPoint(2)` (the section stays 1) |
| 2 | `level34_riot`, section 2 | (Entered at 2: the flipped car replaced by a fresh `CarSpawn`.) The pharmacy owner with a bat (a line and an attack when a player reaches `vNearPanzerStore`; Panzers spawn off-screen 2 s later and `TacticSteal`), Huns wrecking zone 29 at `vNearHunStore` (a throw tutorial), a hardware owner waving the player in (sphere 5 m) who fights if hit, meat and radio looters (they talk, then leave or fight), a molotov thrower at `vNearMolotovCar` (`GoalThrowObject`; the car blows up 1 s after), a Moonrunner tagging. The riot spawners run while a player is in `vSection2` (message 3 starts, 4 stops them). `vCarPoizo` (a Warrior): roof throwers paused, a locked camera, the Warriors' brains off, that Warrior warped to `fStereoWarp`, Vermin's line about the stereos; at its end the radio objective with its counter and a marker to the section 3 gate, brains back on 1 s later | `vSection3` or `vSection3Gate` entered by a player: the police scene `l34_c3` (eight riot cops with batons, eight extras, the three Warriors, brain-off and suspended meanwhile), then the gate `DblGt01` opens, wanted gangs and cops spawn, a scatter tutorial, `ForceCrimeLevel(true)` and a crime at `fCopSearch1`; float 1 = 3, **`SetCheckPoint(3)`**, section 2 removed |
| 3 | `level34_riot`, section 3 | The music-store owner and his men with bats (sphere 7 m: a random line when Warriors or wanted gangs come near), wanted gangs and cops spawning while a player is in `vSection3`; whichever objectives are not done yet | **`P1.HeadToGate`** (all three objectives): the riot gangs leave through exits, level cops spawn as enemies of everyone, the turf boxes move to the riot turf, muggings and tags saved, **`SetCheckPoint(4)`**, `preLoadFile("level34_gate")` |
| 4 | `level34_gate` | Entered fresh: riot extras, wanted gangs, cops, level cops, the factory and the mugging bonus are set up again. Then an objective to `fFinalGate` (`ObjectiveSetup`, the `global.lua` helper), the gate `DblGt00` opens for the player, a line, the objective on line 2. At the gate (`P2.AtGate`): the park scene preloaded, `DblGt02` opens, an objective and marker to the Furies | `vNearFuries` entered by a Warrior: every riot gang removed, the factory shut, **`SetCheckPoint(5)`**, `preLoadFile("level34_park")` |
| 5 | `level34_park` | Eleven Furies, three at the fence and two hurt Rogues (wounded, god mode). The scene `l34_c4` (preloaded when continuing: the gate closes, the fog ends). Then the objective and marker to the subway exit, Vermin's lines every 15 s, system music on (tracks `160b_risen2` and `the_fight_loop_01`). The fence Furies shake the fence (two sound emitters); entering `vEnterDiamond` or `vFuryConfront`, or 5 s after the first fence Fury arrives, sets the Furies attacking (`TacticAttack`) and kills the two Rogues. Beating the Furies is not required | `vSubwayExit` entered by a Warrior: the scene `l34_c2` with **`Final`** true and the park gangs removed; its end runs `PreCashTheWorld`, which calls **`HUDLaunchMissionComplete()`** ([how a mission ends](#level99)); mode 1's `Exit` then plays **`L34_OUT`** (the level record asks for an outro, [Movies](movies.md)) |

Engine behaviour this mission needs beyond its bindings: [message 6](#triggers) from `vLevelVandal` when a human
standing in it damages something (the box tests the human's position, not the object's), car message 25 with its
flag, the money and inventory callbacks, the mugging callback, gang tactics' callback codes (1, 5, 6, 9) and the
police answering a forced crime level ([Crimes](crimes.md)). Type bit `0x400` is a glass pane ([Tasks](tasks.md));
which prop counts how often: [Breakable props](objects.md#breakable-props).

### `level2.lua` (mission 5) {#level2}

Story mission 5, loaded from the hub's checkpoint 2: the Warriors (six, Cleon player 1 and Fox player 2) against the
Orphans. Read from the disassembly of `level2.lua` and its four chapter scripts; the bindings each call reaches are
on [Story coverage](../references/bindings/story.md#level2). Inferred unless marked.

- **`Main`**: fog colour, radar on, `SetupCars` (colours, radios and trunk items of five parked coupes),
  `SetSpawnMax(25)`, then the checkpoint table, the same shape as `level99`'s: 1 `level2_clinic` /
  `P1.SetupClinic`, 2 `level2_tenement` / `P2.SetupTenement`, 3 `level2_clubhouse` / `P3.SetupClubhouse`, 4
  `level2_junkyard` / `P4.SetupJunkyard`, each with its own Warriors set-up at the chapter's start.
- **`RunLevel`**: the Warriors; `preLoadFile` of the chapter; the follow camera (`AddCameras`, then
  `CameraSetClipping(MainCam, 0.1, 90)`); the mission's objective; at checkpoints **2-4** only: `peds = 1`,
  **`SetupFlagNet2()`**, system music on and the subway-intro objects destroyed. Then the anim preloads, the start
  callback `StartAmbient`, three bums (`GoalBumLogic`), `ReportCrime(true)`, `CfgSetMaxThrowError(5, 5)`, the dealer
  "factory" (`StartFactory`: a revive dealer and a paint dealer that respawn through gang spawners, all Lua) and the
  tag spots (`TagInfo.SetupTags`, a `global.lua` helper over the tagging bindings).
- **`SetupFlagNet2`** is a **Lua function of `level2.lua` itself** (function 96 of the main chunk), not a binding: it
  links the 13 flags `PedNet_01`-`13` (made by `AddFlagsBoxesPaths`) into a pedestrian network with 13
  [`FlagNetAddLink`](../references/bindings/ai.md#flagnetaddlink) calls (two or three neighbours each), calls
  `AddCivilians` (gang 23 "Civilians" with one spawner whose per-spawn callback, `global.lua`'s `CivFlagNet`, sets
  each civilian walking the net with [`FlagNetTraverse`](../references/bindings/ai.md#flagnettraverse)) and then
  sets itself and its twin `SetupFlagNet52` (a 28-node net for `level52`, never called here) to nil, so it runs once.
  Its other caller is `P2.SetupTenement`, when `peds` is still 0 (the player came from checkpoint 1 in the same
  level). Nothing for the engine to register.
- **`StartAmbient`**: from checkpoint 2 on, the subway loop and `LoadBonus` (the bonus count from Lua save float 1);
  a fade-in at checkpoints 3 and 4; the traffic ambient; Warrior command 4 (scatter) off.
- **The bonus**: every Orphan killed counts (`AnyOrphanDied`, also hooked to most Orphan gangs' message 18) toward
  40 on a second objective line; the 40th unlocks `UM_Unlock(2, 1, 2)`. Each checkpoint saves the count
  (`SaveBonus`, `SetLUASaveDataFloat(1, n)`) and the tags (`TagInfo.SaveTags`).

| Checkpoint | Chapter | What happens | Ends with |
| --- | --- | --- | --- |
| 1 | `level2_clinic` | Fade out, a locked intro camera, jumping blocked for both players, the scene `l2_subexit` (out of the subway). Three **kind-2** boxes switch the player's camera on the way down to the street: `vbIntroCam` back to the locked camera, `vbLeadRail` a rail down the stairs (two points, field of view 50, `CamLeadRail(3, 0, nil)`: trailing 3 m behind, mode 2), `vbSideRail` a level rail (distance 3.5 m, height 3 m). `vEnterStreet` (kind 0, a Warrior) kills them, gives the follow camera back and plays `l2_c1_a`. The Orphans at the dice game run to a fence and fight; five down, the bums open a gate (a path camera); the clinic scene, then 13 Orphans to beat (a backup gang spawns after 4) and a counter tutorial | `vJesseScene`: `SetCheckPoint(2)`, `preLoadFile` of the tenement |
| 2 | `level2_tenement` | `SetupFlagNet2` when not run yet; the scene `l2_c2_a`; Jesse runs a [lead chase](../references/bindings/ai.md#goalleadchase) along a path ahead of the player, in god mode, and is [interrogated](../references/bindings/character.md#husetinterrogation) when caught; wandering, dancing and roof gangs; entering `vStartWander01` already preloads the clubhouse chapter (its set-up runs then) | Jesse gives up → an objective (`ObjectiveSetup`) → a cut-away → `SetCheckPoint(3)`, bonus and tags saved |
| 3 | `level2_clubhouse` | The Orphans' clubhouse, friendly until `vNearClubhouse` (or a hit) plays `l2_c7`; then 15 to beat (backup after 4), a rage-throw tutorial; breaking the two speakers quiets the radio (`SetAmbientEmitterVolumeMod`, then off) | the scene `l2_c3` → `SetCheckPoint(4)`, saves, `preLoadFile` of the junkyard |
| 4 | `level2_junkyard` | Sully and the junkyard Orphans (suspended), Sully's car spawned; an objective to the gate whose callback removes the street gangs, shuts the dealer factory, drops molotovs and plays `l2_c5`; then a backup spawner, Sully shaking a fence and fleeing the player (trigger sphere 3 m / 6 m), a HUD bar for the car (`HUDEnableBar`, `HUDSetBarPercentage`); every car message 25 (`SetGeneralCarMsgHandler`) about Sully's car with its flag true counts one hit, 15 hits empty the bar | the final scene `l2_c6_b` → `HUDLaunchMissionComplete()` |

Engine behaviour this mission needs beyond its bindings: [kind-2 boxes](#triggers) (the intro cameras) and the
[rail camera](camera.md#rail), mode 0 and the trailing lead. Open: which car events send message 25 with the flag
true besides `Car_DoExplode` (`0x0038ab50`, flag 1), since the junkyard needs 15 of them on one car.

### `level3.lua` (mission 6) {#level3}

Story mission 6, loaded from the hub's checkpoint 3: the tag competition in Hi-Hat turf and the rooftop chase. Read
from the disassembly of `level3.lua` and its five chapter scripts; bindings on
[Story coverage](../references/bindings/story.md#level3). Inferred unless marked.

- **The main chunk** sets `CHAPTER = GetCheckPoint()`, a chapter table (1 `level3_street`, 2 `level3_comp`, 3
  `level3_balcony`, 4 `level3_chase`, 5 `level3_gallery`), the Warriors' five tag patterns, and runs `Main` then
  `RunMission`. `Main`: radar range, objective, fog, ambient sound boxes, the pedestrian flag net (`SetupFlagNet`, as
  in `level2`), the chapter's Warriors (`PlayerGang[CHAPTER]`), the rival gangs' tag settings; player 1 is
  **Rembrandt** at chapters 1-2 and **Snow** from 3, player 2 Ajax; the follow camera; outdoor mode.
- **`RunMission`** `preLoadFile`s the chapter with its `Setup`; **`NextMission`** runs the chapter's `Cleanup`, adds
  1 to `CHAPTER`, `SetCheckPoint(CHAPTER)` and runs `RunMission`: every chapter hands over to the next in place.
  The global `Startup` is true only for the chapter the level was entered at, so a chapter knows whether it starts
  the level (its own scene set-up, `SetStartGameCallback`) or continues from the last.

| Checkpoint | Chapter | What happens | Ends with |
| --- | --- | --- | --- |
| 1 | `level3_street` | The clubhouse scene, then the Soho street: three gangs at their spots (`GoalGuardFlag`, `GoalPlayDynIdle`, walks started by trigger spheres), a truce (hitting any of them fails the mission, `C0.Damage`), an objective to the competition | entering the gate: the Warriors walk in (brain off, `GoalMoveToFlag`), two locked cameras, `NextMission` 3 s later |
| 2 | `level3_comp` | The tag competition: the scene `l3_c1_a`, locked cameras for the tutorial, the Warriors' wall (`SetMsgHandlerEx` message 0 → `HuTag` with patterns; message 14 per tag finished), rival painters ([`GoalTag`](../references/bindings/ai.md#goaltag)), [`HuEnableTagCheer`](../references/bindings/character.md#huenabletagcheer) | the victory scene → `NextMission` |
| 3 | `level3_balcony` | Chatterbox throws molotovs, bottles and bricks from a balcony (`GoalBigLedgeThrower`), vans to save, a button and an elevator (`HuUseAnyAnim` for the button press, `HuSetAutoCombat`), mimes, a pulley | the balcony scene → `NextMission` |
| 4 | `level3_chase` | **The rooftop chase**, below | `vbEnterGallery`: the jump scene → warp to the gallery, `NextMission` |
| 5 | `level3_gallery` | A 180 s stopwatch (`W_SetStopWatch`), the gallery's statues and paintings to wreck (a HUD bar; `vGallery` hears message 6, `ChangeCollision` on statues that fall), seven tag spots; falling (`vDeathGallery`) or time running out fails | damage ≥ 200: player brain off, fade, the end scene → `HUDLaunchMissionComplete()` |

**The rooftop chase (checkpoint 4).** Snow is player 1. In order:

1. **`Setup`**: the Hi-Hat pursuers (`AddPursuit`: five, gang type 20, and a spawner off-screen), each taking 5× damage
   (`HuApplyDamageModifier`); the path `roofpath2` (eight rooftop flags); a fade to black; Snow tireless; five locked
   cameras for the jumps and a locked intro camera made **current**; the **rail camera** (`ch4.SetupRailCam`:
   `CamSetupRail("ch4.railCam", player, 78, {0, 0, 2}, 0.05, 150)` with two points, (−273.776, 349, 29.971) and
   (248, 349, 29.971)) and its main framing (`ch4.MainRail`: settings 9 = 2, 2 = 78, 3 = −8, 4 = 6, 0 = 5.5, 1 = 4,
   eased over 0.5 s); Warrior commands 0-5 off, radar and command HUD off. Entered at checkpoint 4 the start
   callback is `ch4.HoldIntroPoizo`; continuing from checkpoint 3 it runs at once.
2. **`ch4.HoldIntroPoizo`**: switches 3 off and 4 on; every non-player Warrior brain off, short sight, fast climber;
   **`BrDead(player, true)`**: the player's brain is off, which also [takes his pad away](ai.md#handlers); fade in
   over 2 s; schedules the mimes' door (0.1 s), `ch4.SetupPoizo` (3 s) and the pursuit (4 s).
3. **`ch4.SetupPoizo`**: a path camera from the intro camera through three points (the second calls Snow's line);
   every other Warrior: brain flushed, normal mode, a friendly [devil run](ai.md#devil-run) on `roofpath2` against the
   Warriors' gang (gait 4, pace 4 m, up to 10 m/s, urgency 3) under a `GoalMoveToFlag` to the first chase flag
   (gait 5); **the player**: normal mode and `GoalMoveToFlag(player, fRoofChase[1], 4, ...)`. With his brain off the
   player's brain still runs the goals a script pushes (`Brain_UpdateGoals`), so **the AI walks Snow** from the
   warp point (−268.5, 353.6, 16.6) toward the first chase flag (−214.7, 364, 20.2) while the pad is ignored.
4. **`ch4.StartMimePursuit`** (4 s): the pursuers run to the first chase flag; two Hi-Hat lines.
5. **`ch4.StartRail`**, message 3 of **`vbStartRail`**, a [kind-2 box](#triggers) (x −260.5 to −256.1, y 357.4 to
   376.1, z 18.3 to 27.6), which only a player can set off: the box disabled; the pursuit spawner started; the
   objective to the roof's end; the pursuers made enemies and each given a hostile devil run on `roofpath2` against
   the Warriors (gait 5, attack at 1 m, pace 13 m, up to 13 m/s, urgency 0.5); switches 3 on and 4 off; **the rail
   camera made current over 0.5 s**; `HuLockPad` and `HuLockPadMovement` cleared and **`BrDead(player, false)`**:
   from here the stick moves Snow, [relative to the rail camera's heading](characters.md#input). Player 2 gets the
   same.
6. **During the chase**, all by kind-2 boxes (message 3): `vMainRail` / `vSideRail` / `vTopRail` / `vBigRail`
   re-frame the rail with `CamModifyRail` (side: distance 8, height 2, setting 3 = 2, 4 = 0, field of view 65 or 78;
   top: height 20; big: distance 10, height 7, field of view 65, setting 9 = 4). `vFixCam` boxes switch to a locked
   (box 13: a fixed) camera over a jump; while no second player is alive they also give **slow motion 0.2** for
   2.5 s (boxes 1-9) or 2 s (box 13, the first time only, `HuSetSlowMo`) and **lock the pad** (`HuLockPad`,
   `HuLockPadMovement`; not in boxes 11 and 14, and no more once box 13 has been entered) until `ch4.TurnOffSlow` or
   leaving the box (message 4, which also returns to the rail camera). `vbJumpWarn` boxes show a jump prompt
   (`HUDTurnOnActionCycleAnim`) for 1.5 s. Falling into `vChaseKill` (kind 0) fades and fails the mission (`MF_4`);
   a pursuer there dies, a Warrior is warped to the gallery.
7. **`ch4.EnterGallery`** (`vbEnterGallery`, kind 2): objective done, handlers off, the chase gangs removed,
   tireless and fast-climber cleared, the jump scene, then the Warriors warped to the gallery and `NextMission`.

So **the player has no control from the fade-in until he reaches `vbStartRail`**, by design: the original walks him
there with a goal on a brain-dead player. A port in which a kind-2 box never sends message 3, or in which a pushed
goal does not move a brain-dead player, leaves the player standing for good. Confirmed (code) for the brain and box
behaviour; that Snow's walk crosses `vbStartRail` is inferred from the positions (not seen at runtime).

Engine behaviour this mission needs beyond its bindings: [kind-2 boxes](#triggers), the [rail
camera](camera.md#rail) mode 0, [GoalDevilRun](ai.md#devil-run) and [message 6](#triggers) from `vGallery` when a
human standing in it damages something.

### `level5.lua` (mission 7) {#level5}

Story mission 7: Ajax and Cochise after Sanchez in Hurricane turf, the bar, the chase across the yards and the boss
fight with Diego and Vargas. Ajax is player 1, Cochise player 2. Read from the disassembly of `level5.lua` and its
four chapter scripts; bindings on [Story coverage](../references/bindings/story.md#level5). Inferred unless marked.

- **The main chunk** sets `CHAPTER = GetCheckPoint()` and a chapter table (1 `level5_chapter1`, 2
  `level5_chapter1a`, 3 `level5_chapter2`, 4 `level5_chapter3`), then runs `Main` and `RunMission`, as in
  [`level3`](#level3): `RunMission` `preLoadFile`s the chapter with its `Setup`; **`NextMission`** runs the chapter's
  `Cleanup`, sets `LastChapter`, adds 1 to `CHAPTER`, **`SetCheckPoint(CHAPTER)`**, saves the tags and runs
  `RunMission`. A chapter's `Setup` checks `LastChapter` to tell "continued in place" from "entered at this
  checkpoint" (then it waits for its start callback and plays its opening scene itself).
- **`Main`**: fog colour, fog distance 0, the tag spots, the radar, the chapter's Warriors (checkpoint 1: Rembrandt,
  Ajax, Cochise, Snow outside; 2: the same four in the bar; 3 and 4: Ajax and Cochise only), the follow camera, car
  colours, `SetSpawnMax(30)`. At checkpoint 1 the bum and idle anims are preloaded. The **bonus flags** ("Car",
  "Bar") are packed into Lua save float 1 (`EncodeU32`): written at checkpoint 1, read back from 2 on, and a done
  bonus is shown done; `SetupFlagNet` (the pedestrian net) runs from checkpoint 3 (chapter 1 runs it itself).
- **Helpers in `level5.lua`** used by the chapters:
    - **`StartChase{Human, MaxDistance, FailDialog, Reason}`**: every 50 ms the distance from the chased human to
      the nearer player is checked; at `MaxDistance` (35 when not given) or more a 5 s watch starts, and getting
      back within range cancels it. When it runs out the player says `FailDialog` and its end calls
      **`HUDLaunchMissionFailed(Reason)`**. `EndChase` stops it. Several tables pass `Distance` instead, which
      `StartChase` does not read, so those chases use 35.
    - **`SRun`** (Sanchez's run): entering volume *n* of the run's list (a human of gang type 0, a Warrior)
      disables it, Sanchez says line *n*, and if path *n* exists he runs it with `GoalRunCarrotRun(..., 5, 1,
      GangWarriors, 13)` (keeping ahead of the Warriors); the next volume is then armed. `SRun.WarpSanchez` teleports
      him over a pile (a chair-break sound and a line).
    - **`SetUpPOIZOCAM{Cam, SayVag, SayVag2, Human, Human2, bActionDialog, Time, Delay, Callback, bSkippable,
      ...}`**, the in-game cut-away: spotting off, Warriors deactivated, **rage ended on both players**
      (`HuSetRageMode(false)`) and normal mode, fade, the camera switched, any non-Warrior gang that comes within
      5 m of a Warrior suspended (trigger spheres), the listener moved; then one line, two lines one after the other
      (the second started from the first's end callback), or both through `HuActionDialog`; skippable with the
      pad (`RegisterPOIZOCallback`, a `global.lua` helper on pad bits 64 and 2048). `POIZOCamBack` undoes it all
      and calls `Callback(skipped)`.
    - **`SetSanchezMode`**: Sanchez is god-mode, ungrabbable, ungroundable, untargetable, unreachable, does not
      react and never wants a weapon, so he can only be chased.
    - **`SwitchNodeStack` / `SwitchNode`**: when a listed crate stack, skid or trash can breaks (message 2, or 1
      for the last two) the nearby path jump node is turned into a door node (`ConvertJumpToDoor` at its
      `fNodeSwitch` flag), opening the way for the AI.

| Checkpoint | Chapter | What happens | Ends with |
| --- | --- | --- | --- |
| 1 | `level5_chapter1` | Fade out; four Hurricanes at a car (suspended, playing idle clips), the stoop crowds and civilians, zone 1, the car's music (a looping emitter) and a radio in it (`CarSpawnRadio`; stealing it, `CfgSetSteroTheftHandler`, stops the music), car parts 17 and 21 removed. Start callback: brains off, the scene `l5_c8_b` (the Warriors and four civilians); then a line, an objective to `fObjective01` (`ObjectiveSetup`, callback `C1.OpenBar`), the passing-train sound scene `Harlem_Subsound` every 30 s. `vAdvCombat`: a wall-smash tutorial. `volCar[1]` entered by a Warrior (or a Hurricane hit by one): the Warriors warped, three bums with molotovs (`GoalBumLogic`), the scene `l5_c4`; then the Hurricanes confront (`CONFRONT` tactic), the dealer factory (revive, paint), trunk items, a backup spawner, the **car bonus** (line 1 in per cent and HUD bar 1: 15 car messages 25 with the flag true on that car, or one whose third argument is −1, complete it, `UM_Unlock(5, 1, 2)`, music off, the "Car" flag saved) | the objective reached: a locked camera on the bar's front, the train stopped, the wanted level cleared, the arriving Warrior put at the door, the cut-away with one skippable line; its end runs `NextMission` → **`SetCheckPoint(2)`** |
| 2 | `level5_chapter1a` | The bar: Sanchez (`SetSanchezMode`), the bartender (god mode), two Hurricane gangs (`CONFRONT`, aggressive), three bar girls; zones 10 and 6. The scene `l5_c2` (entered at 2: from the start callback), then room smoke (`StartRoomSmoke`), the bar's music loop, a line, the fight objective, a rage tutorial after 7 s | every Hurricane of both gangs dead (message 18 counts down): 3 s, the scene `l5_c5`; Rembrandt and Snow deleted, the music tracks set, `NextMission` → **`SetCheckPoint(3)`** |
| 3 | `level5_chapter2` | **The chase.** The scene `l5_t16_000`, then Sanchez with a spinning icon and a flashing radar marker, spotting off, the objective, `StartChase` (33 m, fail line `l5_t16_012`, reason `LEVEL5.MF`), his first run. `volFightF1[1]` (once Sanchez has reached `volSanEnd[1]`): a door closes, the chase ends, the scene `l5_t9_004`, four Hurricanes fight, two more 1 s later; "back-timer" boxes restart and stop a chase watch while player 1 is between them. All six down: the scenes `l5_t8_000` and `l5_chase_poizo2`, Sanchez runs to a gate, a new chase (60 m, line `l5_t16_013`). Then the yards (zone 3): `volSanDoor01` arms the second run, `volFightPit[1]` stops the chase for the scene `l5_t9_010` and three pit gangs (`MANPILE`, `HANGINGOUT`) 3 s later, `volFightPit[2]` sends Sanchez on, `volSanWarp` boxes warp him over piles, `volSanGo[1]` adds a bum blocker, `volSanGo[2]` starts the third run and a chase (35 m) | `volStartHouse` entered by a Warrior: the chase ends, `NextMission` → **`SetCheckPoint(4)`** |
| 4 | `level5_chapter3` | **The boss fight.** Rembrandt and Snow rejoin; Diego with his weapon; lights and music off; zone 5; continuing in place, player 1's health is set to 1170. The scene `l5_c6`, then a locked opening camera, the boss rail camera (`CamSetupRail`, two points), `TacticBossScenarioA` stage 1 with the boss HUD bars (bar 3, two names), Warrior commands limited to 1, 2 and 4 (command 1 issued), Warrior weapons off. The tactic's callback: code 1 ends a stage (stage 2: the scene `l5_c7`, Vargas fights in god mode; stage 3: the scene `l5_c8`, two tenants join, a second Vargas breaks through a door 5 s later); code 18 (Diego down) puts the camera on Diego and backs the Warriors off 5 m with rage ended. Diego's bar never shows below 66 % in stage 1 or 33 % in stage 2. Hitting the plaster drops a chandelier (the scene `l5_chandelier`) | stage 3's callback: the tenants leave, Diego suspended; 3 s later (waiting while a player is tagging) the Warriors are stilled, a line, a 2 s fade, the scene `l5_c3` with **`Final`** true (its callback 1 breaks the glass within 10 m of `fSanDead`); `PreCashTheWorld` then calls **`HUDLaunchMissionComplete()`** ([how a mission ends](#level99)). The level has no outro movie |

**Fail states**: only the Sanchez chase in checkpoint 3 calls `HUDLaunchMissionFailed` (a player more than the
chase distance from him for 5 s); no one else is protected (Sanchez and the bartender cannot be hurt).

**The two binding questions** (confirmed (code)):

- **`HuSetRageMode(human, true)`** calls the Lua function whose name `CfgRageHandlers` stored first (copied to
  `0x006b6830` by `Cfg_SetRageHandlers`, `0x00236c58`). `global.lua` registers `CfgRageHandlers("SetRageMode",
  "ClrRageMode", "RageFull", 20000, 5000)` at boot, so it is **`SetRageMode`**, which sets god mode, unstunnable,
  reduced and increased reaction, ungrabbable, ungroundable and keep-weapon on the human (and locks rage with the
  rage cheat on). `Human_StartRageMode` (`0x00236d28`) passes the human and a flag (a count at `0x0051489c +
  0x268` below 1). Ending rage (`Human_EndRageMode`, `0x00236fb8`) calls the second name, `ClrRageMode`, which
  clears the same flags. `Rage_ResetConfig` (`0x00236be0`) clears the three names.
- **`HuActionDialog`'s two lines start together**, not one after the other: `Human_ActionDialog` (`0x00239788`)
  starts the first speaker's line and then the second's in the same call, each through `Human_PlaySpeechCutting`
  (`0x0021e698`), which plays a mono stream at once (mono streams use channels 5-12, [Sound](sound.md#stream-pairs))
  with no queue between humans. The partner link (human `+0x18c`) is only read by `Human_UpdateSpeech`
  (`0x0021e940`): while a line plays and the partner is within 10 m it calls `Human_StopSpeech` (`0x0021ec38`) on
  both without force, which stops nothing that is still playing, and clears both links. The callback runs when the
  second speaker's line ends. That the lines overlap audibly is not checked at runtime. This mission uses it only
  in cut-aways with `bActionDialog`; the two-line form without it (`SayVag2`) does wait for the first line.

Engine behaviour this mission needs beyond its bindings: [kind-0 volume boxes](#triggers) for a Warrior, message 8
from a flag when a human reaches it (the banter switches), message 18 on a death, car message 25 with its flag
and third argument, `ConvertJumpToDoor` on a path node, and `TacticBossScenarioA`'s callback codes 1 and 18.

### Errors in a fresh state {#errors-in-a-fresh-state}

Two level scripts stopped in Coney's fresh Lua state (scripts read with the disassembly; the bindings' code
confirmed):

- **`level5.lua` at checkpoint 2, "comparing nil with a number" on instruction 19** is in `DecodeU32` (a `level5.lua`
  helper that turns a number into a table of 30 bits; instruction 19 compares the remaining value with `2 ^ i`). The
  level's start-up calls it, for every checkpoint above 1, as `DecodeU32(GetLUASaveDataFloat(1))` to restore the
  mission's saved progress bits (`tblSaveData`). Coney has no `GetLUASaveDataFloat`, so the call returned nothing.
  **The original returns a number: slot 1 of the eight saved script floats** at `W_GameState + 0x570c`
  (`0x0037b850` → `0x0041ad00`, slot `n` at `+0x570c + (n − 1) × 4`, `n` read as 16 bits, no bounds check). The game
  state's constructor (`0x00418588`) zeroes the eight floats and the saved flag bits at `+0x572c`, so a level entered
  at checkpoint 2 without a save gets **0.0** (no bits set). `SetLUASaveDataFloat` (`0x0041acd8`) writes the slot;
  `level5.lua` itself stores the encoded bits there (`EncodeU32`, in two of its functions). Confirmed (code)
  for the bindings and the zeroing; the script side inferred.
- **`level102.lua`, "indexing nil" on instruction 5** is in the `global.lua` helper **`FlagPos`**: instruction 5 reads
  `.x` of what `GetFlagPos` returned. The arena's `ConfigRumble` calls `AddDummyPlayer`, which does
  `HuCreate("Dummah", 352, FlagPos(fP1[1]), ...)`; `fP1` comes from `level102_brawl_init.lua`'s `AddFlag` calls.
  Coney has neither `AddFlag` (so `fP1[1]` is nil) nor `GetFlagPos` (so `FlagPos` indexes nil). **The original
  provides both as bindings**: `AddFlag` returns a handle, `GetFlagPos` an `M_Vector4` with `x y z w`
  ([World flags](flags.md)). Nothing else is missing before that point: `Level` is set by `global.lua`,
  `RumbleInfo` and `Rumble` by the arena's main chunk.
- **`GetRumbleModeData`** (also missing in Coney) fills `RM_LuaData` with the Rumble menu's 23 choices; with it
  absent the table keeps the zeros `ParseLuaData` put there, so `Rumble.gameType` is 0 (`RumbleInfo[0]` is
  `"brawl"`, the right file by chance), `Rumble.gangSize` is 0 (the "no gangs" path, `ShowRules1`) and
  `Rumble.gang1[1]`, the player's type, is nil. A playable arena needs the menu's values (inferred from the
  disassembly of `level102.lua`; the values are in [Front end](frontend.md#rumble-setup)).
- **`level95`, `events.ChatEvent: attempt to index a nil value`**: `events.SetupConversationEvent(cluster)` adds
  1 to `events.NumEvents`, stores `events.ChatTable[NumEvents] = {eventCluster = cluster, lastStatement = 1,
  lastTalker = NilHandle}` and calls `ScheduleFuncArg1("events.ChatEvent", events.NumEvents, 5000 + random(1,
  5000))`; `ChatEvent(n)` then reads `events.ChatTable[n].eventCluster` and that cluster's `CClusters[c].occupied`.
  The script expects the **number first and the delay last** (as `0x003572e8` does). Coney's binding takes them the
  other way round, so it called `ChatEvent` with 5001-10000 after `NumEvents` ms, and `ChatTable[n]` was nil.
  Confirmed (code) for the binding, the script side from the disassembly. (`flags.Cleanup` also sets `CClusters` to
  nil, so a chat event that fires after it would fail the same way; inferred.)
- **`level87`, `ch3.BoozerChallenge` is not a function**: the third chapter's table at run time holds `Cleanup`,
  `KillHandlers`, `AjaxSaysPissTank`, `DoNextChapter`, `BumScrap`, `OneBum`, `Done`, `Preloaded` and `Exit`, but no
  `BoozerChallenge`, although a callback names it. Confirmed (runtime) in Coney's state; inferred to be a slip in the
  original script, which would also find nothing to call. Coney logs the call ("is not a function; not called") and
  skips it; what the original does with a missing callback is not traced.
- **`level87` from checkpoint 1 into 2, "attempt to index a nil value (instruction 19)"** after the drunk's scene
  (`l87_drunk_dest`): the scene's end runs `PreCashTheWorld`, which calls the second chapter's return function. That
  function calls `RestorePreviousTags` and then `ch2.ArrowRadarStuff`. Instruction 19 of the return function is the
  read of `.ArrowRadarStuff` from the global `ch2`, which is nil. The cause is the order in the first chapter's end
  (`The_REAL_End`): `SaveLevelData`, `SetCheckPoint(2)`, `ChLoader` (`preLoadFile` of chapter 2 with `"Setup"`),
  then the global `Cleanup`. In the original the file is only requested there (`Script_PreloadFile`, `0x00357a68`,
  hands `FileManager_Request` the completion routine `0x00356d00`; confirmed (code)), so `Cleanup` is still
  chapter 1's: it sets `ch1`, `Setup` and `Cleanup` to nil, and chapter 2's chunk and `Setup` run later, when the
  file arrives. Coney's `preLoadFile` runs the chunk and `Setup` inside the call, so the `Cleanup` that follows is
  chapter 2's, which sets `ch2`, `Setup` and `Cleanup` to nil. A direct start at checkpoint 2 never runs chapter 1's
  end, so `ch2` survives. Save float 1 (1024 for tag 10) is not the cause: with a bit set `RestorePreviousTags` reads
  `master[tagflag[i]].wtag` (its own instruction 19), and `master` exists, since chapter 1's `Setup` builds it
  (`LoadTagTable`) and nothing clears it. The original does not fail here (inferred from the disassembly). A
  `preLoadFile` must therefore run its chunk and callback after the calling Lua function has returned, at the
  earliest.

## Notes for implementers

- **Table constructors flush every 62 items**, not 64: `SETLIST` stores its items at `A × 62 + 1` onwards
  (`luaV_execute`, `0x00334478`, the multiplier `0x3e`; the largest `B` on the disc is 62). This is this build's
  `LFIELDS_PER_FLUSH`. A VM that uses 64 shifts every list item past the 62nd (in `config_preload3.lua`'s
  `levelNames`, every level record after the 62nd). confirmed (code).
- What a script needs to run without errors on the front-end path: the preloads in one state, then `global.lua` and
  `level100.lua` in that same state, with numbers from `GetLanguage`, `GetPlatform`, `GetCurrentLevelIndex`,
  `GetLevelId` and `UM_IsLevelComplete` (nil for false).

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00418518` / `0x00418520` / `0x00418528` | `GameState_IsDongleValid` / `GameState_CreateDongle` / `GameState_CreateDongleChallengeKey` | the workers of `IsDongleValid` (always 1), `CreateDongle` and `CreateDongleChallengeKey` (always 0): stubs on the PS2 | confirmed (code) |
| `0x0041abc0` / `0x0041abe8` | `Script_SetCheckPoint` / `Script_GetCheckPoint` | the workers of `SetCheckPoint` (`GameState_SetCheckPoint`) and `GetCheckPoint` (`+0x33a`) | confirmed (code) |
| `0x0041abf8` / `0x0041ac88` | `GameState_SetSaveBit` / `GameState_GetSaveBit` | bit n of the saved flag bits at `+0x572c` (32 per word), set or cleared / tested | confirmed (code) |
| `0x0041ad28` / `0x0041ad60` | `Script_SetLUASaveDataBool` / `Script_GetLUASaveDataBool` | the workers of `SetLUASaveDataBool` and `GetLUASaveDataBool`: bit n − 1 of the same set | confirmed (code) |
| `0x0041af20` / `0x0041af48` | `Script_SetHatCallback` / `GameState_SetHatCallback` | `SetHatCallBack`: the name at `+0x33c` | confirmed (code) |
| `0x0041af80` | `GameState_CallHatCallback` | from `Human_HandleMessage`: unless `+0x410` is set, calls the hat callback with (human, object, the object's type name) | confirmed (code) |
| `0x0041cf20` / `0x0041cf40` / `0x0041cf58` | `GameState_SetFlagByte` / `GameState_SetFlagBits` / `GameState_ClearFlagBits` | the four game-state flag bytes at `+0x3e8 + n` that `GameState_TestFlag(n)` reads: write, OR, AND NOT (`GameState_SetDetailFlag` and `GameState_ClearDetailFlag` use the last two) | confirmed (code) |
| `0x0041d830` / `0x0041d860` | `GameState_SetDetailFlag` / `GameState_ClearDetailFlag` | `setDetailFlag(n, bits)` / `clearDetailFlag(n, bits)` on the flag bytes `+0x3e8 + n` | confirmed (code) |
| `0x00423358` | `StopWatch_Construct` | the stopwatch (0x50 bytes, from `Game_InitializeSubsystems`): fields zeroed, rate `+0x10` = 1.0, `+0x44` from `0x00598690` | confirmed (code) |
| `0x004235a8` | `StopWatch_SetCallbackName` | copies up to 31 characters of the callback name into `+0x20`, or empties it for none; from `W_SetStopWatch` and `UnloadLevel` | confirmed (code) |
| `0x004235e0` | `StopWatch_SetDisplay` | sets shown (`+0x18`) and the label (`+0x1c`) | confirmed (code) |
| `0x00423638` | `StopWatch_GetTime` | the stopwatch's current time (`+0x08`, ms) | confirmed (code) |
| `0x00423670` | `StopWatch_Show` | `W_ShowStopWatch(show, label, warn)`: shown sets the warning window `+0x40` to warn and the next beep `+0x48` to warn + 1000; hidden resets them to 0 and 1000; then `StopWatch_SetDisplay` | confirmed (code) |

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
  `ShowProfileManager`, `MenuLoadLevel`, `ScreenQueueEffect`, `HUDLaunchMissionComplete`, `GetGameTime`, `HuCreate`,
  which keeps the humans a level script makes, [Level loading](level-loading.md#coneys-implementation), and the level
  bindings of `src/scripting/level_bindings.h`: the flags, `GetPosition`, the saved script numbers,
  `SetStartGameCallback`, `GetRumbleModeData`, `GetRumbleModeGangName` and `CfgSetDatabaseSizes`,
  [World flags](flags.md#coneys-implementation), the AI and gang bindings, which hand their calls to the AI host,
  [AI](ai.md#coney), the trigger bindings of `src/scripting/trigger_bindings.h`: `SetMsgHandler`,
  `SetMsgHandlerEx`, `AddVolumeBox` and `RotateVolumeBox`, below, the three animation callback bindings,
  [Characters](characters.md#anim-callbacks), and the scene bindings with the `GoalJoin*` goals, which play scenes,
  [Scenes](scenes.md#coneys-implementation); with no scene system the preload and play bindings fall back to Coney's
  stand-in, where a scene loads and ends at once),
  **routed** (handed to a
  Coney stand-in that logs it: `PlayMovie`, the three music bindings, `ShowRumbleModeInterface`) or a **stub** (returns
  its documented default: nothing, a new handle for `GetPTank`, `ObjSpawn` and `CameraCreateLocked`, 0 for
  `InvNumberOf`, false for `UM_IsTypeDirty`). The configuration stubs (the `Cfg*` bindings, `CfgObj`, sound, unlockables
  and commands) keep their arguments (`RecordedCalls`) for the subsystems that will need them. 97 real, 5 routed and
  106 stubs (72 of them recording): every binding the front-end path calls, and what the level scripts need for their
  starts.
- **Message handlers and triggers** (`src/scripting/message_handlers.h`, `src/world_objects/volume_boxes.h`): an
  object's callback per message number (26 slots), delivered with the arguments of the
  [table above](#message-handlers); the volume boxes with their turn and the kind-0 trigger update (3, 5 once per
  1000 ms, 4) over the scripts' humans, run by gameplay every frame before the scripts' frame. An occupant whose handle
  no longer resolves is skipped with no 4 and kept; **Coney stand-in**: a human the scripts made resolves for the whole
  level, `HuDelete`d or not (when the original's stops is not traced), so a deleted occupant leaves with 4 and
  `HuGetGang` still gives its gang. A human's events reach its own handlers through its brain, a flag arrival is
  message 8 ([AI](ai.md#coney)). **Message 6** (`VolumeBoxes::sendDamage()`): a pane, door or car hit that the
  object took sends `(human, box, object)` to every enabled kind-0 box the attacker (a thrown object's thrower)
  stands in. Not yet: props, the 30 m alert after it, and the boxes' every-fifth-frame round-robin (Coney updates
  them every frame). **`--script-trace`** (`ScriptSystem::traceCalls()`,
  [Building](../guides/building.md#tracing)) logs every binding call and every call into the scripts by name,
  Coney's own tool.
- **Trigger spheres** (`src/world_objects/trigger_spheres.h`, `TriggerSphereCfg` in `src/scripting/world_bindings.h`):
  a pool of 100, one per object, each checked every fifth frame (index modulo 5) with the boxes' rules, 3, 5 once per
  the sphere's 1000 ms and 4 going to the object's handler; gameplay finds the object among the scripts' humans, the
  flags and the spawn records. **Coney's choices:** the sphere's own object is never its occupant; the clear-line test
  of modes 1 and 2 is a collision-mesh ray from the centre (mode 2: raised 1 m) to 1 m above the human's feet;
  `TriggerSphereCfg`'s interval is kept as the handler's period but not used. `TriggerSphereEnable` arms a sphere
  (making one with radius 0, mode 1 and a 1000 ms period when the object has none) or disarms it, forgetting who is
  inside without message 4; Coney has no handler components, so it acts on any object.
- **Play** (`src/gamemodes/gameplay_mode.h`): the scripts keep running in a level, stepped every frame after the
  level's step; `--play-level LEVEL` enters a level the same way, after the preloads and a fresh state
  (`LevelScripts`, `src/gamemodes/level_start.h`). In `level99` at checkpoint 1 the tutorial runs from the intro
  through the two markers (`vMark01`, `vMark03`; how the original draws them:
  [Objective markers](objects.md#objective-markers)), the `l99_t1` scene and `P1.SetupBasicAttacks`; the basic-attacks
  lesson then waits for the tutorial callback (`HUDSetTutorialCallback`, [HUD](hud.md#tutorial-callback)), which the
  play mode calls with each hit player 1 strikes. The lessons run through the light and heavy attacks, the grab and
  its strikes, the mount and its strikes ([Combat](combat.md#mount)), the L2 let-go, the targeting lesson (L1 held
  2 s, heard through the `PadSetHandlerEx` handler, which the play mode calls after the level's step with each
  human's pad command, [PadSetHandlerEx](../references/bindings/input.md#padsethandlerex)), the combos and the strong
  attack, then stop at the strong grapple (circle + cross), whose research is under way.
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
- `random(a, b)`: whole numbers in `[a, b]` from the game's own table, read from the player's executable
  ([World flags](flags.md#coneys-implementation)), or a deterministic stand-in without it; the math library's `random`
  uses another one of Coney's, never the C library's. The trigonometry works in degrees, as stock Lua 4.0 does.
- `tolua`, `M_Vector4` and `M_Quat` are empty tables and `NilHandle` and `NilSoundHandle` are 0, below the first handle
  a stub gives out.
- `preLoadFile` queues its file: the caller's script goes on, and the file runs, then its callback by name, at the
  level start's preload (with any files those scripts ask for) or, during play, at the top of the next script frame,
  as the original runs both later, when the file arrives; `level87`'s first chapter depends on that
  ([Errors in a fresh state](#errors-in-a-fresh-state)).
- Without an AI host (a level script run on its own) `GangCreate` returns a new handle and the gang counts 0, and
  `InvNumberOf` returns 0 until inventories exist, so the hub's and `level5`'s start functions run to their end.
- A runtime error's message names the last call of a missing binding skipped before it, the likely cause
  (`...; last skipped call HuTagPattern`).
- `GetLUASaveDataFloat` and `SetLUASaveDataFloat` outside slots 1 to 8 read 0 and write nothing (the original does not
  check).
- **Scene stand-in** while no scene system is attached (gameplay and the front end attach one, [Scenes](scenes.md)):
  `ScenePreload` returns a new handle and calls its load function with it at the next script update; the three play
  bindings return true and call their end function (third argument) with the scene at the next update, so a scene
  ends at once. With a system, `SceneAddObject` resolves and pins the object's spawn record and binds it.
- A message is delivered whatever the level-end state (`W_GameState + 0x14c`), which Coney does not keep yet; a
  message that asks for a result counts as taken when its call runs, but for message 0 (the interaction), which is
  taken when its callback returns anything but nil. `SetMsgHandlerEx` with message 0 keeps the object's prompt as its
  kind-1 context record (`script::MessageHandlers::setPrompt`); the second text is not kept and no prompt is shown.
- `ScheduleFuncArg1(name, n, ms)` takes the number before the delay, as the original does; the hub's chat events
  (`events.ChatEvent`) run without errors since (`coney_tests "[disc][story]"` runs `level95` for 20 seconds of
  script frames).

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[disc][scripts]"` with `CONEY_DISC` set runs the four
preloads, `global.lua`, `level100.lua`, `Menu.onStart` and the menu callbacks (`fadeToRMI`, `launchRMI`,
`cancelRumbleMode`, `playMovie`, `startGame`, `onFinish`) in one state: no script error and no call of a missing
binding. The preloads add 25 globals and the level entry 193 more, as at run time (1,034 → 1,059 → 1,252); the level
table holds 111 records; `CfgObj` is called 1,371 times; 9,273 configuration calls are recorded; 388 HUD strings are
set. `Menu.startGame` asks for `level99` (`runNextMission(1)`), and after the unload `global.lua` and `level100.lua` run
again in the fresh state without the preloads, also without errors (a Coney observation for the front end, not a check
of the original).

**A sphere's radius** (2026-10-06): `TriggerSphereSetRadius` sets an object's sphere radius, first making one with the
defaults of `0x00414480`, not armed, when it has none (`TriggerSpheres::setRadius`; Coney counts any object as having a
handler component, as for `TriggerSphereEnable`).

## Open questions

- **The first mission:** what a restart (failure or pause menu) restores from `SetCheckPoint`'s copies, and who
  reads the object manager's 35-word list; whether `TriggerSphereCfg`'s interval reaches the sphere's period
  (`+0x164`); whether a sphere's own object can be its occupant, and what the clear-line tests `0x0024dee8` and
  `0x0024df40` test between; who writes the stopwatch's rate (`+0x10`); what commands 37, 38 and 40 (d-pad down,
  up, right), which the tutorial switches with `EnableCommand`, do ([Commands](../references/commands.md)).
- What a level loaded after an unload (a fresh state without the preloads) does when it needs `PHYS`, `MATERIAL` or
  `GSTRING`: does the level flow run the preloads again, or do the level scripts not need them? (For the front end,
  `global.lua` and `level100.lua` run without errors in Coney's fresh state, and so do `level99.lua` with
  `level99_combat.lua`, `level2.lua` and `level3.lua` at the checkpoints tried.) The two errors Coney met are
  answered below ([Errors in a fresh state](#errors-in-a-fresh-state)): both are bindings Coney lacks, not missing
  preloads or globals.
- The scene system (`SuperRunScene`). (Answered on [Scenes](scenes.md#superrunscene).)
- `RegisterUpdate`. (Answered: `preLoadFile`'s completion routine `0x00356d00` runs the loaded chunk through slot
  `+0x3c`, then, when a callback name was given, finds it (slot `+0x4c`) and calls it with no arguments (slot `+0x8c`);
  confirmed (code).) Still open: whether the checkpoint scripts arrive during `InitLevel`'s preload, which services the
  file manager, or in the first frames of play.
- **`CfgLevelName`'s arguments:** which of the twelve numbers after the level number fills which field (`+0x08`
  sections, `+0x0c`, the three flags at `+0x0d`, `+0x10`, `+0x6c`-`+0x80`), and why the scripts pass 18 arguments where
  the writer (`0x0041f118`) is described with 17 values. The section count does not simply equal the number of
  `<level>_<k>.pak` files for any one argument (25 of 63 levels with packs match the seventh).
- **`CfgObj` count:** the page counts 1,279 `CfgObj` calls in `config_preload3.lua`; running it calls the binding 1,371
  times. Static call sites against calls made (loops, or functions called twice)?
- **Degrees or radians:** does this build's math library keep Lua 4.0's degrees (Coney's assumption)?
- **`random`'s generator** (answered: a table of 1,024 numbers walked by an unseeded index, results in `[a, b]`
  inclusive, [Bindings](#libraries)). Still open: whether Coney should reproduce the table (it is data in the
  executable, so a reimplementation would read it from the player's disc) or keep its own seeded generator.
- **`NilHandle` and `NilSoundHandle`:** their values.
- **`PadSetHandler`'s arguments:** this page gives `(pad, button, name)`, [Front end](frontend.md#input) gives
  `(button, player, "function")`. Coney implements neither yet: `PadSetHandler` is a stub that ignores its
  arguments, since the front-end path needs no Lua pad handler.
- **Names:** the `@orig` tags call the script system's slots `ScriptSystem::Update`, `EnterLevel`, `RunFile`,
  `RunFiles`, `FindFunction`, `Call`, `Schedule`, `ScheduleArg1`, `ScheduleArg2`, `FlushScheduled` and
  `SetUpdateFunction`, the binding wrapper `0x0036eef8` `ShowProfileManager_Binding`, `0x0041f118`
  `W_GameState_SetLevelRecord`, `0x00160d78` `MenuLoadLevel_Choose`, `0x0015c7b0` `LevelFlow_ChooseLevel` and
  `0x0020a268` `PM_Mode::HandleCommand`, until the research database names them.
- Binding arguments the [script bindings](../references/bindings/index.md) reference marks as not understood yet: which
  bit of `PadSetHandler`'s mask is which PS2 button; who sends messages 0, 6, 7 and 9-`0x19` and what their extra values
  mean (the arguments are in [Message handlers](#message-handlers)); the scene-play flags (`ScenePlay` and its
  relatives) beyond their `global.lua` names; most fields of the large `Cfg*` records (`CfgChar`, `CfgPowerClass`,
  `CfgWarriorClass`), which are written through computed addresses with no reader found yet. Each would move up from
  inferred once a reader or a runtime observation is found.
