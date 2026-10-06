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
| `+0x10` | float | rate the elapsed time is multiplied by (its writer is not traced) |
| `+0x14` | u32 | running (`W_StartStopWatch`'s argument; the update clears it at the target) |
| `+0x20` | char[32] | the callback's name, kept as text (empty for none) |
| `+0x40` / `+0x48` | s32 | warning window (ms) / time of the last warning beep; `W_SetStopWatch` sets 0 / 1000 |

**Each frame of play** (mode 1 step 1, `0x004233f8`), while running and the game timer has advanced by `d` ms: the
current time moves `d × rate` toward the target (down when the target is below it). On reaching or passing the target
it is clamped there, the watch stops, and the callback is found by name (slot `+0x4c`, dotted names work) and called
with no arguments. While counting down within the warning window, a beep (sound `0x0058b9f0`) plays at most once a
second. Confirmed (code) at `0x004233f8`. The HUD's display of it (`W_ShowStopWatch`) belongs to the HUD.

### Message handlers {#message-handlers}

Game objects (humans, flags, boxes, doors, props) talk to scripts through **messages**: a record whose `+0x20` is
the message number and `+0x24` the object it is about, with `+0x00`, `+0x04` and `+0x11` as extra values.
[`SetMsgHandler`](../references/bindings/script.md#setmsghandler) (`0x00386298` → `0x003860b8`) gives the object a
handler component (vtable `0x00544b98`, made on demand by `0x003848c8`) and stores the interned callback name in its
slot for that number: 26 slots, `+0x0c + 4 × message`. Message 0 also keeps a prompt pointer at `+0x78`
([`SetMsgHandlerEx`](../references/bindings/script.md#setmsghandlerex)). The component's `+0x74` is the **repeat
period** in ms (1000 by default, `0x00384a10`; set by slot `+0x44`). Confirmed (code).

**Delivery** (`0x00384c38`): a message reaches Lua only when the object has a name in that slot, the component is
attached, and **the level-end state (`W_GameState + 0x14c`) is 0**: once a mission is won, failed or left, triggers
stop calling scripts. The marshaller (`0x00384ce0`, only while scripting runs, `0x00512b28` = 1) pushes the
arguments by message number and calls the function; some numbers ask for one result, and a true result means the
message was consumed. Confirmed (code); the meanings in the last column are from the
[Script events](../references/script-events.md) list and the senders below.

| Message | Callback arguments | Result asked |
| --- | --- | --- |
| 0 | `(self, subject)` | no |
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
| `0x19` | `(self, other or NilHandle, n, flag)`: the shorts `+0x04` and `+0x06` | no |

`self` is the object the handler belongs to; `subject` is the record's `+0x24`; `other` is `+0x00` unless the row
says otherwise. A gang's handlers
(`GangSetMsgHandler`) use their own marshalling for 18, `0x11` and 2 ([AI: gang events](ai.md#gang-events)).

### Trigger boxes and spheres {#triggers}

Most of the first mission's progress is driven by message 3 on volume boxes. Confirmed (code):

- **A volume box** (`AddVolumeBox`, kind 0: vtable `0x00545df8`, pools on [Tasks](tasks.md#classes)) keeps the
  handles of up to 60 occupants (`+0x70`, cleared to `NilHandle` by `0x004151c0`) and its own handler component at
  `+0x160`. Its update (`0x00415378`), while enabled (byte `+0x68`), collects the humans within its bounding sphere
  (centre `+0x30`, radius `+0x40`; `0x002274a8`, at most 60), skips the dead (`0x00227eb0`), and tests each:
    - inside and new: added to the occupants, message **3** (entered);
    - inside and already an occupant: message **5**, at most once per repeat period (next time at `+0x1e0`);
    - an occupant no longer inside, or dead: removed, message **4** (left).
- **Inside** (`0x00412a18`): within the bounding sphere, between the box's lowest and highest `z` (`+0x18`, `+0x28`),
  and inside the four corners rotated about the centre by the 2 × 2 matrix at `+0x48`-`+0x54`
  (`x' = m00 dx + m01 dy`, `y' = m10 dx + m11 dy`, [`RotateVolumeBox`](../references/bindings/world.md#rotatevolumebox)).
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

**Who calls it.** The front end's `Menu.startGame` calls `runNextMission(1)` for a new or loaded story game. After a
mission, the mission-complete mode (0xb, `MissionComplete_Launch` `0x0015d420` pushes it) calls the Lua function
**`UnlockAndLoad`** from its `Enter` (`0x0015cf70`, the name at `0x0054f528`; confirmed (code)), a `global.lua` helper
(function 59) that calls `MissionCompleteUnlocks()`, releases the players' assets (`LiquidizeAssets`) and calls
`runNextMission(1)` (inferred from the disassembly). The failure and pause menus call `runNextMission(0)`
(`0x00155408`, `0x00155648`, `0x001557f8`: the name at `0x0054e798`, one number argument 0 or 1; confirmed (code)).

**The story order**, from this table and the hub's `fRunMission` ([The hub](#the-hub)); inferred from the
disassembly, and it matches the level records' `+0x0c` (`order` 1-18 in [Levels](../references/levels.md)):

| # | Level | # | Level | # | Level |
| --- | --- | --- | --- | --- | --- |
| 1 | `level99` | 7 | `level5` | 13 | `level9` |
| 2 | `level80` | 8 | `level81` | 14 | `level51` |
| 3 | `level87` | 9 | `level86` | 15 | `level52` |
| 4 | `level34` | 10 | `level93` | 16 | `level54` |
| 5 | `level2` | 11 | `level31` | 17 | `level55` |
| 6 | `level3` | 12 | `level14` | 18 | `level84` |

`level95` (the hub) comes between missions 3-4, 4-5, 5-6, 6-7, 7-8, 8-9, 10-11, 11-12, 12-13 and 13-14 (its
checkpoints 1-10) and after mission 18 (11); its checkpoint 12 follows `level64`.

### The hub (`level95`) {#the-hub}

`level95` is the **Warriors' clubhouse and the streets of Coney around it**, where the story returns between
missions: its functions set up a clubhouse (`SetupClubhouseEnvironment`, `AddClubhouseFlagsBoxesPaths`) and Coney
(`SetupConeyEnvironment`), and it loads `level95_clubhouse.lua` (the clubhouse: the Warchief, the Warriors and their
girls at their spots, workouts, the trophies that start flashbacks, the mission and Rumble menus) and
`level95_coney.lua` (preLoadFile with `env.FinishClubhouseLua` / `env.FinishLoadConeyLua`). Inferred from the
disassembly. Its `Main` loads **`level95_chapter<checkpoint>.lua`** (12 files, names recovered by their CRC) with
the callback `RunLevel`; each chapter sets the numbers of Warriors and girls, the radio track, the Warchief's type
(`WarchiefTable`) and the chapter's **mission actions** (`MissionAction`: red circles, cut-scenes, `MA_MISSION`
errands, ending in `MA_LOADLEVEL`). `MA_LOADLEVEL` calls `story.LoadLevel(fRunMission[checkpoint].level)`:

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
  front end's `Menu.movies` (`TRAILER`, `L1_IN`). The disc's `PSS` folder holds exactly those 16 movies.

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
tutorial's text callbacks (`HUDSetTutorialCallback`) and pad handlers (`PadSetHandlerEx`). How each is delivered:
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
   calls the save system's slot `+0xb4`, **pops mode 1** (now on top: its `Exit` unloads `level99`), rebuilds the two
   inventories and asks for the **autosave** (`0x00155308`, mode 6). Mode 8 then loads `level80`
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
  `ShowProfileManager`, `MenuLoadLevel`, `ScreenQueueEffect`, `HUDLaunchMissionComplete`, `GetGameTime`, `HuCreate`,
  which keeps the humans a level script makes, [Level loading](level-loading.md#coneys-implementation), and the level
  bindings of `src/scripting/level_bindings.h`: the flags, `GetPosition`, the saved script numbers,
  `SetStartGameCallback`, `GetRumbleModeData`, `GetRumbleModeGangName` and `CfgSetDatabaseSizes`,
  [World flags](flags.md#coneys-implementation), and the AI and gang bindings, which hand their calls to the AI host,
  [AI](ai.md#coney)),
  **routed** (handed to a
  Coney stand-in that logs it: `PlayMovie`, the three music bindings, `ShowRumbleModeInterface`) or a **stub** (returns
  its documented default: nothing, a new handle for `ScenePreload`, `GetPTank`, `ObjSpawn` and `CameraCreateLocked`,
  0 for `InvNumberOf`,
  false for `SceneIsPreloaded` and `UM_IsTypeDirty`). The configuration stubs (the `Cfg*` bindings, `CfgObj`, sound, unlockables
  and commands) keep their arguments (`RecordedCalls`) for the subsystems that will need them. 74 real, 5 routed and 109
  stubs (72 of them recording): every binding the front-end path calls, and what the level scripts need for their
  starts.
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
- `preLoadFile` runs its file at once (Coney's reads are synchronous), then calls its callback by name.
- Without an AI host (a level script run on its own) `GangCreate` returns a new handle and the gang counts 0, and
  `InvNumberOf` returns 0 until inventories exist, so the hub's and `level5`'s start functions run to their end.
- A runtime error's message names the last call of a missing binding skipped before it, the likely cause
  (`...; last skipped call HuTagPattern`).
- `GetLUASaveDataFloat` and `SetLUASaveDataFloat` outside slots 1 to 8 read 0 and write nothing (the original does not
  check).
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

## Open questions

- **The first mission:** what a restart (failure or pause menu) restores from `SetCheckPoint`'s copies, and who
  reads the object manager's 35-word list; whether `TriggerSphereCfg`'s interval reaches the sphere's period
  (`+0x164`); who writes the stopwatch's rate (`+0x10`); what commands 37, 38 and 40 (d-pad down, up, right), which
  the tutorial switches with `EnableCommand`, do ([Commands](../references/commands.md)).
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
  relatives) beyond their `global.lua` names; when animation callbacks (`AddAnimCallback`) fire and with what arguments;
  most fields of the large `Cfg*` records (`CfgChar`, `CfgPowerClass`, `CfgWarriorClass`), which are written through
  computed addresses with no reader found yet. Each would move up from inferred once a reader or a runtime observation
  is found.
