# Scenes (in-engine cutscenes)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). The disc check
`coney-tools wad scenes` ([coney-tools](../guides/coney-tools.md#scenes)) parsed all 2,765 records of the NTSC-U disc on
2026-10-06 and prints counts and hashes only. Runtime claims come from the `warriors_passive` trace
(`repo:research/traces/scenarios/warriors_passive.toml`, recorded in PCSX2 2.9.94 on 2026-10-05,
[AI](ai.md#level99-fight)) and from [Characters](characters.md#creation).

## Purpose

How the game plays a scripted scene in the engine: the `.scn` records, how a scene is loaded into one of 12 slots,
how a script binds humans and objects to its roles, what the scene does to them, to the camera, the screen and the
player's control while it plays, how it is skipped, and how it hands back to the game and to Lua. The first mission
needs two: `level99`'s intro `l99_c1` (`IntroScene`) and `l99_c5`, which comes before the sparring fight.

In one paragraph: a scene is a header record and, when long, a chain of segment records of the same tracks. The
header names **roles** (humans), **objects**, at most one **camera** and some **lights**, each with a start and an end
pose; each part holds one track per role (an ordinary animation clip, played on the human) and keyed tracks for the
objects, camera and lights, with events (fades, lens changes, captions, sounds, warps). A script preloads the scene by
name, binds humans to roles by index (`GoalJoinCinematic`) and objects to object slots (`SceneAddObject`), then plays
it. The scene task runs at 30 updates a second: it waits until everything is loaded, pushes the current camera and
makes its own camera current, letterboxes the screen, suspends the AI, drives every bound thing from its track in the
scene's space, streams the next segment while the current one plays, and lets cross or START skip after 2 s. When all
roles' clips have ended it puts skipped humans at their end poses, pops the camera back with a cut or a blend, removes
the letterbox, gives the player control back and calls the script's end function with the scene id.

## Original structure

`Scene/SceneCache.cpp` (`0x00351da0`-`0x00354bb8`) holds the scene list, the 12 slots and the bindings' workers;
`TaskEngine/SceneTask.cpp` (`0x0039d278`-`0x003a13d0`) the scene task; the track runner is `0x00355798`-`0x00356390`
([Source map](source-map.md#scene)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00353460` | `SceneList_Load` | copies the Scene List chunk | confirmed (code) |
| `0x00353698` / `0x00353778` / `0x00353730` | find by substring / exact name / id | scene id lookups ([Scripts](scripting.md#scenes-and-movies)) | confirmed (code) |
| `0x00353298` | `SceneSlot_Get` | the slot holding an id, else a free one, else one to evict | confirmed (code) |
| `0x00353af0`, `0x003525f8` | `Scene_Request` | `ScenePreload`'s worker: load into a slot, or count one more user | confirmed (code) |
| `0x003524a8` / `0x00352430` | allocate / loaded | memory for the record, then the file request and its completion | confirmed (code) |
| `0x00352098` | `SceneRecord_Fixup` | turns the header's offsets into pointers, sets state 2 | confirmed (code) |
| `0x003531c8` | `Scene_CallLoaded` | calls the `ScenePreload` callback with the scene id | confirmed (code) |
| `0x00352c08` / `0x00352a48` / `0x00352788` | segment request / allocate / fixup | streams the next segment into the other of two buffers | confirmed (code) |
| `0x00353818` | `Scene_Play` | common worker of the play bindings | confirmed (code) |
| `0x003a13d0` | `SceneTask_Create` | a scene task with its flags | confirmed (code) |
| `0x0039ca48` | `SceneTask_Init` | the task's defaults | confirmed (code) |
| `0x0039d3a8` | `SceneTask_Bind` | runners for the camera, objects and lights | confirmed (code) |
| `0x0039d870` | `SceneTask_Start` | waits for everything, then starts; re-entered until it does | confirmed (code) |
| `0x0039cbf0` | `SceneTask_Update` | vtable `+0x13c`, every 2 ticks | confirmed (code) |
| `0x003a00b8` | `SceneTask_ClipDone` | a role's clip ended: chain the next segment's clip or leave the scene | confirmed (code) |
| `0x00353a10` | `Scene_Stop` | `SceneStop`'s worker | confirmed (code) |
| `0x003a0a68` / `0x003a0be8` | end the clips / stop looping | used by a stop and a skip | confirmed (code) |
| `0x0039f450` | `SceneTask_End` | gives everything back | confirmed (code) |
| `0x0039ec60` | `SceneTask_Abort` | gives up a scene that did not start in 10 s | confirmed (code) |
| `0x003a0da8` | `SceneTask_CallEnd` | calls the play binding's end function with the scene id | confirmed (code) |
| `0x00356290`, `0x00356308`, `0x003560a8`, `0x00355ab8`, `0x00356188` | track runner: bind camera / bind object, advance, step keys, apply | | confirmed (code) |
| `0x00354d98` | `SceneTrack_Events` | the object, camera and light tracks' events | confirmed (code) |
| `0x002e5300`, `0x002e53e0` | `GoalJoinCinematic` and its goal (type `0x29`) | binds a human to a role | confirmed (code) |
| `0x003541a0` | `Scene_BindHuman` | writes the human into the role | confirmed (code) |

## Data

All values little-endian. Offsets inside a record count from its start; the game adds the record's address on load
(`0x00352098`, `0x00352788`). Layout confirmed (code) at those fix-ups and the readers cited; every record on the disc
parses with it (`coney-tools wad scenes`, [Disc check](#disc-check)).

### The scene list and the slots {#slots}

`scene_list.cnk` and the id lookups are on [Scripts](scripting.md#scenes-and-movies). A record is loaded as
`%s.scn` (`0x00578a08`) with the list name, so the 24 names the list cuts to 16 characters are those of records the
game cannot open by that name (inferred: no WAD name hashes to them; the disc check finds them by content).

There are **12 slots** of 0x40 bytes at `0x006eba18`, confirmed (code) at `0x00353298`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | the loaded header record |
| `+0x04` | the scene list record (its id at `+0`) |
| `+0x0c`, `+0x10` | the two segment buffers; `+0x14`, `+0x18` their list records |
| `+0x24` | the scene task while it plays |
| `+0x2c` | flags: `0x1` loaded, `0x2` waiting for memory, `0x4` file requested; the same three per segment buffer in the next nibbles; `0x1000` the scene has segments; `0x2000` restart the chain (looping) |
| `+0x30` | `s16` current segment buffer (0 or 1), −1 when the chain is done |
| `+0x34` | users (`ScenePreload` calls on a loaded scene) |
| `+0x38` | the interned name of the `ScenePreload` callback |
| `+0x3c` | the time of the last request (for eviction) |

A request finds the slot already holding the id, else an empty slot, else a slot whose scene has ended (state 8),
else the least recently requested slot in state 2 or 8, which it unloads first (`0x00353298`). Confirmed (code).

### The header record {#header}

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | the record's size |
| `+0x04` | u32 | 0 in the file; the scene id after loading (`0x00353698` of the name) |
| `+0x08` | char[16] | name |
| `+0x18` | char[4] | suffix of the first segment (`aa`), empty for a one-record scene; non-empty sets slot flag `0x1000` (`0x00352430`) |
| `+0x1c` | u16 | runtime state: 2 loaded, 4 starting, 5 playing, 6 stopping, 7 ending, 8 ended |
| `+0x1e` | u16 | runtime: humans still in the scene |
| `+0x20` | u8 | number of human roles |
| `+0x21` | u8 | number of objects |
| `+0x22` | u8 | 1 when the scene has a camera |
| `+0x23` | u8 | number of lights |
| `+0x30` | char[84] | a name, the scene's own in 657 of 1,240 headers (`l99_c1_01` in `l99_c1`); no reader found |
| `+0x84` | u32 | the whole scene's length in frames at 30 a second: the parts' durations summed, in all 1,238 headers that have tracks (inferred from that match) |
| `+0x90` / `+0x94` / `+0x98` / `+0x9c` | u32 | offsets of the role definitions (0x60 each), object definitions (0x60), the camera definition (0x70) and the light definitions (0x70) |
| `+0xa0` / `+0xa4` / `+0xa8` / `+0xac` | u32 | offsets of four tables of track offsets: a clip per role, a track per object, the camera's track, a track per light |
| `+0xb0` | u32 | equal to `+0x90` in every record; not read |

**A role or object definition** (0x60): `char name[16]`; the **start pose** at `+0x10` (position `x, y, z, 1`, then a
quaternion `x, y, z, w` at `+0x20`); the **end pose** at `+0x30` (`+0x40` the quaternion); `+0x50` the bound human
or object at runtime (0 in the file). A role's name is a model name (`warrcl`, `warrrecv`, `warrashcv`, ...); binding
is by index, not by name.

**The camera definition** (0x70): the same name and poses, then `+0x54` near plane, `+0x58` far plane (slot `+0x1ac`),
`+0x60` the world preload's radius, `+0x64` field of view in degrees, `+0x6c` the scene camera at runtime; `+0x50`,
`+0x5c` and `+0x68` are not read by the code traced. Confirmed (code) at `0x0039d870`; `l99_c1` has near 0.5, far 75,
radius 500, field of view 65.47°.

**A light definition** (0x70): name and poses, `+0x50` u32 kind (1 → light flags `0x81`, 2 → `1`, else `0x80`),
`+0x54` colour `r, g, b` (0-1), `+0x64` cone angle in degrees (halved and turned to radians), `+0x68` range (0 means
15), `+0x6c` the light at runtime; made by the light task manager when the scene starts (`0x003a0928`). Confirmed
(code). All 21 lights on the disc are `fspot01`.

### The segment record {#segment}

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | the record's size |
| `+0x04` | char[16] | name: the scene's name cut to 15 characters, then the suffix |
| `+0x14` | char[4] | the next segment's suffix, empty for the last |
| `+0x18`-`+0x1b` | u8 × 4 | counts of role clips, object tracks, camera tracks (0 or 1) and light tracks |
| `+0x1c`-`+0x28` | u32 × 4 | offsets of the four track tables, as the header's `+0xa0`-`+0xac` |

A segment has no definitions: it continues the header's roles, objects, camera and lights in the same order. The
game names the next segment `strncpy(name, 15) + strncat(suffix, 3)` (`0x00352c08`) and finds it by exact name.
Confirmed (code).

### Tracks {#tracks}

**A role's track** is an animation clip: the 80-byte descriptor of chunk `0x02` followed by its keys and events,
exactly as in a character's resource ([Animation](formats/animation.md#descriptor-chunk-0x02-80-bytes)); the fix-up
gives it the clip vtable `0x00534240`. A role clip's events count frames **from the scene's start**, not the part's:
`l99_c1`'s first-part clips (506 frames) carry events up to frame 2022 of its 2,026, and the segments' clips carry
none. Inferred from the data and the frame offset `0x003a00b8` adds at each segment change.

**An object, camera or light track** (vtable `0x00544908`):

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x04` | f32 | duration in seconds |
| `+0x08` | u32 | bytes of keys and events |
| `+0x0c` | u32 | bytes of position keys |
| `+0x10` | u32 | offset of the keys |
| `+0x14` | u16 | number of events |

Then **position keys** (16 bytes: `u16 frame`, 2 unused, `f32 x, y, z`), **rotation keys** (8 bytes: `u16 frame`,
`s16 x, y, z` × 2⁻¹⁵, `w` rebuilt as in clips) filling the rest less the events, and the **events** (24 bytes: `u16
frame`, `u16 type`, arguments) at the end. Frames are absolute (frame × 1/30 s is compared with the time), and
between keys the runner interpolates positions linearly and rotations as quaternions (`0x00355ab8`). Confirmed (code)
at `0x00355e98`, `0x00355ab8`.

### Events {#events}

**Object, camera and light tracks** (`0x00354d98`, confirmed (code) for the actions; counts from the disc):

| Type | Count | Action |
| --- | ---: | --- |
| 13 | 8 | preloads (at load) and plays a sound by hash (`+8`) on the sound manager `0x0050aa84` (inferred: dialogue) |
| 14, 71 | 29, 2 | a sound by hash on the track's human or object; on a car, also its horn or engine (`0x0038d6d8`) |
| 24, 25 | 203, 212 | sends message `0x12` / `0x13` to the track's object (meaning not traced) |
| 26 | 41,408 | the scene camera's lens: field of view `+8` (degrees), near `+0xc`, far `+0x10` |
| 27 | 300 | fade out over `+8` seconds (`ScreenQueueEffect` type 1 on player 1's view); stops the caption |
| 28 | 380 | fade in over `+8` seconds (type 0) |
| 29 | 38 | loop point: in a looping scene, the frame to restart from (`+4`) |
| 30 | 72 | a light's colour (`+8`), range (`+0x14`) and cone (`+0x10`, degrees) |
| 31 | 33 | calls the scene's end function now |
| 33 | 333 | a particle effect at a position and rotation (s16 values, scaled as clip keys) named by `+0x14` with a prefix |
| 41 | 1,422 | caption control: `+4` = 0 shows the next caption, 4 or 5 clear it, 6 sets a flag first (inferred from `0x001cb190`, `0x001cb010`; [Boot](boot.md#timers) has the caption system) |
| 69 | 0 | an object or car action (`0x00396048`, `0x0038d798`) |
| 73 | 23 | holds object `+4` in front of the camera for `+8` seconds (`0x003a0e48`) |
| 74 | 0 | a coloured fade: type in bit 31 of `+4`, colour in its low 24 bits, `+8` seconds |
| 76 | 241 | pad rumble on every player: strength `sqrt(+6 × 0.01) × 180 + 75`, at most 255 |

Types 9, 10 and 54 also occur (4 events) and have no case (nothing happens). **Role clips** carry clip events
([Animation](formats/animation.md#keyframes-chunk-0x00)) handled by the animation code (`0x00101dd8`); scene clips use
11 (4,486, footsteps and cloth sounds by name), **21 and 22** (4,125 each, always in pairs) and others. Type 21 sends
message `0x95` with the position at `+8` and type 22 message `0x96` with the value at `+8` (confirmed (code) at
`0x00101dd8`); every role clip on the disc starts with a 21/22 pair at frame 0 whose position is the role's start
position and whose value is its start heading in radians (`2·atan2(z, w)` of the start quaternion), and the last pair
is the end pose. **Inferred:** they place the human at the role's marks (at the start and at camera cuts). Type 13 at
frame 0 of one clip names the scene (inferred: its dialogue).

## Behaviour

### From Lua: `SuperRunScene` {#superrunscene}

The scripts never call the play bindings directly for a story scene; they build a table and call the `global.lua`
helper. Confirmed (code) for `global.lua`, read as bytecode (functions at its lines 4107, 4156 and 4233):

1. **`SuperRunScene(t)`**: unless `t.Animation`, `HideHud()`, then (unless `t.NoClearWanted`, and when
   `GangWarriors` exists) `GangClearWanted(GangWarriors)` and `GangClearResponders()`, then `ScreenQueueEffect(1, 0)`
   (black at once). With `t.StageLighting`, `EnterStore({0, 0, 0, 0})` and `EnableStageLighting(true)`. Each human of
   `t.Humans` that is not `NilHandle` and is in gang type 0 is un-arrested and revived if needed (each counted with
   `StatAdd(player, 5, 4, 1)`). With `t.Preloaded`, `t.SceneId` is already an id: `tblScene[id] = t` and
   `gPlayCutScene(id)` at once. Otherwise `ScenePreload(t.SceneId, "gPlayCutScene")`, `tblScene[id] = t`. Returns
   the id.
2. **`gPlayCutScene(id)`** (called by the engine with the id once loaded, `0x003531c8`): unless `SpotON`, warrior
   and enemy spotting off (`CfgSetWarriorSpotting(0)`, `CfgSetEnemySpotting(0)`); defaults `Bars` = true, `Delay` =
   0, `Speed` = 0, and **`BlendCam` = −1 with `FadeIn` = true** unless `BlendCam` is given (then `FadeIn` = false);
   with `bBackOff`, `SetBackOff(Humans)`. Each non-`NilHandle` human *i* (from 1) joins role *i* − 1:
   `GoalJoinCinematic(h, id, i − 1, Speed, true)` (`GoalJoinAnimation` when `Animation` is 1,
   `GoalJoinFixedScene` for another `Animation` value). Each object *i* joins slot *i* − 1 (`SceneAddObject`). Then
   `ScenePlayCinematic(id, Delay, "PreCashTheWorld", Bars, not NoSkip, Looping, Freeze, BlendCam, Final, Chain)`
   (`ScenePlayAnimation` / `ScenePlayFixedScene` with `"AnimReturnFunc"` for the animation kinds).
3. **`PreCashTheWorld(id)`** (the end function): with `NumCallBacks` > 0, calls `ReturnFunc(NumCallBacks)` and counts
   down instead. Otherwise `EndBackOff` with `bBackOff`; if not `Final`: `HUDShowMissionSummaryText(HudText, 6)` with
   `HudText`, **`ReturnFunc(id)`**, spotting back on unless `BlendCam` is 0, `ExitStore()` and stage lighting off
   with `StageLighting`, and with `FadeIn` **`ScreenQueueEffect(0, 0.5)`** (or `ScheduleFunc("DelayedFadeIn",
   DelayFadeIn)`); `tblScene[id] = nil`. With `Final`: black at once, `ReturnFunc(NumCallBacks)`, `tblScene[id] =
   nil`, `HUDLaunchMissionComplete()`. Then `bSuppressHud = nil`.

Nothing in these helpers shows the HUD again; that is left to `ReturnFunc`.

**`level99`'s two scenes** (`level99_combat.lua`, [Scripts](scripting.md#level99)):

| Table | `SceneId` | `Humans` (role order) | `Objects` | `ReturnFunc` |
| --- | --- | --- | --- | --- |
| `IntroScene`, built in `P1.SetupCombat`, run by `StartAmbient` at checkpoint 1 | `l99_c1` | Cleon, Vermin, Rembrandt, `NilHandle`, `Generic2`, `Generic1`, `Generic3`, `player2` (Ash) | `dyn_s_spraycan`, `dyn_rembrandt`, `dyn_vermin`, `dyn_intro_test` | `P1.StartTraining` |
| built in `P1.SetupWarriors` (when `bAshIntro` is false; else retried in 50 ms) | `l99_c5`, `Preloaded` = false | Cleon, Vermin, `Generic1`, `Generic2`, `Generic3`, `NilHandle`, `player`, `player2` if `HuIsAPlayer(player2)` else `NilHandle` | none | `P1.SendWarriors` (the fight) |

Neither sets `Bars`, `BlendCam`, `Delay`, `NoSkip`, `Looping`, `Freeze`, `Final` or `Chain`, so both are cinematic,
letterboxed, skippable, **not frozen** and end with a cut hidden by the 0.5 s fade-in. `freeze` defaults to true only
when the argument is absent: the binding's boolean reader (`0x00409318`) returns false for an explicit `nil`, and
`global.lua` always passes ten arguments. Confirmed (code). Their records:

| Scene | Frames | Parts | Roles (index: name) | Objects | Camera |
| --- | ---: | --- | --- | --- | --- |
| `l99_c1` | 2,026 (67.5 s) | header + `aa`-`ae` | 0 `warrcl`, 1 `warrve`, 2 `warrrecv`, 3 `warrso01`, 4 `warrso02`, 5 `warrso03`, 6 `warrso04`, 7 `warrashcv` | `s_spraycan01`, `rembrandt`, `vermin`, `cleon` | yes |
| `l99_c5` | 500 (16.7 s) | header + `aa` | 0 `warrcl`, 1 `warrve`, 2 `warrash`, 3 `warrlynx`, 4 `warrjones`, 5 `warrmal`, 6 `warrrecv`, 7 `warrashcv` | none | yes |

A role bound to `NilHandle` (role 3 of `l99_c1`, role 5 of `l99_c5`) simply does not play.

`level99.lua` also plays one scene directly: `F.RunWonderWheel(id)`, the `ScenePreload("WonderWheel_99", ...)`
callback, binds 29 objects (the wheel, its carts and neon signs) to slots 0-28 and calls `ScenePlayFixedScene(id, 0,
0, true, false)`: the wheel turns as a looping object scene, not frozen, with no camera change.

### Loading {#loading}

`ScenePreload(name, callback)` finds the id and requests the slot (`0x00353af0`): a scene loaded or loading gets one
more user (`+0x34`) and nothing else; otherwise memory of size + 0x40 is requested from the pool `0x0050cd4c`
(`0x001886d8`, queued until it fits), then the file `%s.scn` is requested (`0x003524a8`). On arrival the header is
fixed up (state 2), the slot is flagged loaded, and the callback is called with the scene id (`0x003531c8`, the task
manager's phase set to 0 for the call). A play binding on a scene not yet loaded requests it and waits for the file
there and then (`0x00353020` services the file manager until it arrives). Confirmed (code).

**Segments** stream during play (`0x00352c08`): the first request loads the first segment into buffer 0; each later
one toggles the buffer, frees what it held, and loads the next by name. The update requests the next segment once
every runner has moved onto the current one (`+0xf2` runners ≤ `+0xf4` switched) and, for a cinematic, unless a
chain skip is in progress. A role clip ending switches to the same role's clip in the newest segment
(`0x003a00b8`, waiting for the file if it is still in flight); with no segment left, the human leaves the scene.
Confirmed (code).

### Playing {#playing}

`Scene_Play` (`0x00353818`) takes the id, a position and rotation, the delay, the end function and the flags below,
creates the scene task (`0x003a13d0`, cancelling game mode `0xf` if it is on top), binds its runners (`0x0039d3a8`),
stores the end function and `BlendCam` (task `+0x94`) and calls the start (`0x0039d870`). Confirmed (code). The task's
flags and how each binding sets them:

| Task | Flag | `ScenePlayCinematic` | `ScenePlayFixedScene` | `ScenePlayAnimation` |
| --- | --- | --- | --- | --- |
| `+0xed` | **cinematic** (the binding's `bars`): letterbox, preload, player hand-over | `bars` | 0 | from an argument |
| `+0xe5` | skippable | `skippable` | not set (a register left over) | from an argument |
| `+0xeb` | looping | `looping` | `looping` | from an argument |
| `+0xef` | freeze | `freeze` (1 when omitted) | `freeze` | from an argument |
| `+0xe7` | final | `final` | 0 | 0 |
| `+0xe8` | chain | `chain` | 0 | 0 |
| `+0xea` | placed relative to its humans | 0 | 0 | 1 |

Which of `ScenePlayAnimation`'s arguments lands in which flag is not traced (`0x00353f40` passes its registers on
in a different order).

The scene's space is placed by the position and rotation: `ScenePlayCinematic` and `ScenePlayFixedScene` pass the
origin and the identity, so the records' coordinates are **world coordinates**; `ScenePlay` passes the script's;
`ScenePlayAnimation` passes none and the scene is fitted to its bound humans (`0x003547e8`, details not traced).
`ScenePlayCinematic` with `skippable` also sets `0x00510174`, a flag the screen effects clear and a player check
(`0x0027c120`) reads. Confirmed (code). The `bars` argument therefore switches the whole cinematic behaviour, not only
the bars.

### Starting {#starting}

The task's update (`0x0039cbf0`) runs every **2 ticks** (30 a second, `Task_SetUpdateInterval(2)`). While the state is
4 it counts the delay down by one per update and re-enters the start, which returns at the first thing not ready
(state stays 4). Confirmed (code) at `0x0039d870` unless marked:

1. **Once:** keep the position and rotation; the delay becomes `delay × 60` updates (inferred: so 2 s per unit);
   give up after 10,000 ms of game time (`GameTimer +0x48`), when the update calls the abort (`0x0039ec60`, which
   gives everything back as the end does, without the end function being traced).
2. With a camera, for every player: end screen look 2 if the human has flag `0x80000`, and queue screen effect 5 (end
   the blur pulse) at once.
3. Mark every bound human in-scene (human flag `0x800000`); a human not yet ready waits.
4. **Cinematic only**, steps of the global scene state `0x0051489c + 0x410` (one per update): 0 → 1 (an action on
   some of the player gang's members, `0x0016b8d0`, not traced); 1 → 2: player 1's camera (the camera inside a
   wrapper of types 5-8) saves its place in the task and moves to the scene camera's start position, and **the world
   around it is preloaded** with a 10 s budget and the camera definition's radius, loading `<scene>.pak` when it
   exists (`WorldManager_Preload`, [Level loading](level-loading.md#preload)); 3 comes in step 6.
5. Wait until every bound human's character instance and model are loaded (requesting them), then every bound object.
6. Cinematic: a game-mode switch (`0x0041a008(0)`, restored at the end; not traced); state 3; wait one more update;
   then clear pad flags `0x40` and `0x800` on every player, and unless `chain`, clear the chain-skip flag
   (`0x0051489c + 0x56e4`).
7. While the delay has not run out, step every bound human toward its start mark (`0x0039d618`, 0.1); wait until all
   arrive or the delay ends.
8. Wait until the caption system is ready for the scene's name (`0x001cae60(0x00619570)`; [Boot](boot.md#timers)).
9. **Start:** call the global scene callback (`SceneSetCallback`) with no arguments; make or reuse the scene camera
   ([Camera](#camera)); start the camera, object and light runners at frame 0; for each bound human ([Humans](#humans));
   for a cinematic, **letterbox in** on every player's view (`ScreenQueueEffect` type 2 over **1.5 s**, at once when
   `chain`); with `freeze`, **suspend every brain**; for a cinematic with roles, stop every human's held sound
   (`0x0021ecd0`). State 5.

### Humans {#humans}

**Binding.** `GoalJoinCinematic(human, scene, role, gait)` pushes goal `0x29` and at once writes the human into the
role (`0x003541a0`: only if the role exists and the human is in no scene, human `+0x280` = −1); a pad-controlled
human's brain is switched off (`BrDead(brain, 1)`) right away. The goal's Start switches an AI brain off too
(remembering whether it was), pushes a move to the role's start mark at the given gait and a turn to its start
heading; its Process finishes when the scene has. Its End restores an AI brain; its destroy turns a pad brain back on
(`BrDead(brain, 0)`). Confirmed (code) at `0x002e53e0`, `0x002e5480`, `0x002e55a0`, `0x002e55e8`, `0x002e5618`.

**At the start** each bound human not already in a scene: counted in (`+0x1e`), `+0x280` = scene id, its brain's
actions cleared, its state reset, human flag `0x2000` set, its scene transform set to the scene's position and
rotation, message 10 sent to its task; a held object named by clip event 9 is taken from the object slots
(`0x00227080`); then its clip is pushed as three animation tasks with the end callback `0x003a00b8`, looping when the
scene loops, streaming when it has segments, blended in over 0.3 s only for a non-cinematic scene placed on its humans
(and not in game modes 8 and `0xb`); the instance is put in mode 3. Confirmed (code) at `0x0039d870`.

**While it plays** the human is driven by the clip's animation and root motion ([Animation](formats/animation.md)),
and the clip's 21/22 events place it at its marks ([Events](#events)). **When its clip ends** (`0x003a00b8`): unless
skipping, it leaves the scene (`+0x280` = −1), flags `0x2000` and `0x800000` cleared and `0x20000000` set, message
10 sent, and the count drops; the scene goes to state 7. With segments the clip is replaced by the next part's
instead. Confirmed (code).

**Confirmed (runtime)** in the `warriors_passive` trace (from the state saved as `P1.SetupWarriors` starts `l99_c5`):
`Generic1` holds goal `0x29` with scene 2627 (`l99_c5`) for 481 updates (16.0 s of the scene's 16.7 s; the state was
saved after it began); when it pops, `Generic2` stands at (−282.55, 128.97), role 3 `warrlynx`'s end position
(−282.56, 128.97), and the player at role 6's (−286.93, 121.58). After `l99_c1`, Rembrandt stood at role 2
`warrrecv`'s end position (−289.03, 120.29) ([Characters](characters.md#creation)).

### Objects, camera and lights {#camera}

A **track runner** (0x90 bytes) per object, camera and light holds the track, the target, the time, the scene's
position (`+0x40`) and rotation (`+0x50`) and the sampled pose (`+0x60`, `+0x70`). Each update it advances by the
frame's time; at the end of a part it returns the time left over and switches to the next part's track (`0x003a0400`
camera, `0x003a05a8` objects, `0x003a0768` lights); with none left, a looping scene starts again from the header's
track (restarting the segment chain, slot flag `0x2000`, or from the loop point of event 29) and any other scene
without roles ends. It then sets
the target's transform to **scene rotation × sampled position + scene position** (`0x00356188`, target vtable
`+0x6c`). Confirmed (code).

**The camera** (when `+0x22` is set): a type-4 camera (`Cam_Scene`) is made, given the scene's name and the current
camera's view, the current camera is **pushed** and the scene camera made current with no blend (`0x0011ee08`); its
lens is the definition's field of view, near and far; each update the track moves it and its own update runs
(vtable `+0x134`). The details of the push, of `CameraMakeActive` during a scene and of the pop are on
[Camera](camera.md#scenes). A scene without a camera keeps the current camera. Confirmed (code).

**Objects** bound by `SceneAddObject` follow their tracks; at the end they are released to the object manager.
**Lights** are made from their definitions at the start and released at the end. Confirmed (code).

### Skipping {#skipping}

Only a skippable scene (`+0xe5`), confirmed (code) at `0x0039cbf0`:

- The task counts **60 updates** (2 s) from its creation (`+0xe0`) before a button counts.
- Then, on any player's pad, **cross (`0x0040`) or START (`0x0800`)** held ([pad bits](frontend.md#input)) skips:
  the caption is cleared, that player's view goes **black at once** (`ScreenQueueEffect` type 1, 0 s), and the scene
  is stopped like `SceneStop(id, false)`. START also sets the chain-skip flag (`0x0051489c + 0x56e4`), so the next
  scenes played with `chain` skip at once without a button.
- The stop ends every role's clip, first firing its remaining events (`0x003a0a68`, `0x00103e90`); the scene then ends
  as below with the skip flag set.

### Ending {#ending}

State 7 (all clips done, or a part ended with no roles) calls the end (`0x0039f450`). Confirmed (code):

1. Cinematic: scene state `0x410` = 0; the game-mode switch of step 6 restored.
2. Each bound human: when skipped, `+0x280` = −1 and it is **placed at its role's end pose** (in the scene's space);
   its join goal (types `0x27`-`0x2a`) is popped, which gives a player back control ([Humans](#humans)); unbound.
3. Each object: when skipped, placed at its end pose; released. Each light: released.
4. **The camera**: popped and made current over `BlendCam` seconds, the scene camera released and the cameras updated
   once with 0.17 s ([Camera](camera.md#scenes)).
5. Cinematic: **letterbox out** on every player's view (type 3, 1.5 s); player 1's gang command reset
   (`0x0041c4e0(…, 0, 1, 0, 1)`) and its gang's alert cleared (`0x00166708`).
6. With `freeze`, **every brain is resumed**, including any a script had suspended.
7. State 8; **the end function is called with the scene id** (`0x003a0da8`, only while scripting runs, `0x00512b28`
   = 1). For `level99` this is `PreCashTheWorld`, which calls `ReturnFunc` and fades in over 0.5 s.
8. Cinematic and not `final`: preload the world around the camera again, loading `<scene>_end.pak` when it exists.
9. When skipped and `BlendCam` > 0: fade in over `BlendCam` seconds.

The next update sees state 8 with no humans left and frees the task and **unloads the slot** (`0x00353bf0`,
`0x00351da0`), so a scene is loaded again for its next play. Confirmed (code).

`SceneStop(id, force)` (`0x00353a10`): a starting scene goes straight to state 7; a playing one with roles has its
clips ended (state 6 while a role is in a paired move, retried each update); without roles it goes to state 7; a
looping one that is not forced only stops looping (`0x003a0be8`) and ends after the current pass. Confirmed (code).

### Timing {#timing}

- Scene time is in 1/30 s frames; the task and its runners advance on the 30-a-second update, the roles' clips on the
  animation clock ([Animation](formats/animation.md#playing-a-clip)). Confirmed (code).
- While the scene camera is current the game clock follows real time without the 40 ms clamp
  ([Boot](boot.md#timers)), so a slow frame is not stretched out. A fixed-step Coney plays the same frames.
- `SceneLength` reports the first part's duration only (`0x003540b8`), not `+0x84`. Confirmed (code).

### Disc check {#disc-check}

`coney-tools wad scenes` (2026-10-06, NTSC-U): `scene_list.cnk` sha256 `f97e241f…190b927e`; 2,765 records parsed
(1,240 headers, 1,525 segments; the 24 cut names found by content), none failing; 3,153 roles, 2,352 objects, 512
cameras, 21 lights, 405,373 frames (3 h 45 min); every segment chain resolves; the header's frame count equals its
parts' durations in all but 2 headers that have no tracks. Records sha256 `0f465735…c1e979ef`.

## Coney's implementation

None yet. The reader for the disc check is `repo:python/src/coney_tools/scenes.py`.

## Open questions

- Which human state the start resets (`0x0023e6e8`, `0x00227388`, `0x002266a8`) and what messages 10, `0x95`, `0x96`
  do in the human's handler; the 21/22 warp is inferred from the data.
- The game-mode switch `0x0041a008` (`W_GameState +0x3f8`) a cinematic turns off and back on.
- How `ScenePlayAnimation` fits the scene to its humans (`0x003547e8`), and the 0.1 at `0x0039d618`.
- Who shows the HUD again after `SuperRunScene`'s `HideHud` (not `global.lua`).
- What the camera definition's `+0x50`, `+0x5c`, `+0x68` and the header's `+0x30` name are for.
- Track events 24/25 (messages `0x12`/`0x13`), and most clip event types besides 9, 11, 21, 22.
- Where a scene's captions come from (the caption system is keyed by the scene's name).
