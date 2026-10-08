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
| `0x001cae60` | `Captions_WaitTitleCard` | holds the start while a scene's title card shows ([Subtitles](#title-card)) | confirmed (code) |
| `0x0039d3a8` | `SceneTask_Bind` | runners for the camera, objects and lights | confirmed (code) |
| `0x0039d870` | `SceneTask_Start` | waits for everything, then starts; re-entered until it does | confirmed (code) |
| `0x0039cbf0` | `SceneTask_Update` | vtable `+0x13c`, every 2 ticks | confirmed (code) |
| `0x003a00b8` | `SceneTask_ClipDone` | a role's clip ended: chain the next segment's clip or leave the scene | confirmed (code) |
| `0x00353a10` | `Scene_Stop` | `SceneStop`'s worker | confirmed (code) |
| `0x003a0a68` / `0x003a0be8` | end the clips / stop looping | used by a stop and a skip | confirmed (code) |
| `0x0039f450` | `SceneTask_End` | gives everything back | confirmed (code) |
| `0x0039ec60` | `SceneTask_Abort` | gives up a scene that did not start in 10 s | confirmed (code) |
| `0x003a0da8` | `SceneTask_CallEnd` | calls the play binding's end function with the scene id | confirmed (code) |
| `0x003a0e48` | `SceneTask_HoldObject` | event 73: holds a bound object (an intro card) before the camera for a time ([Intro cards](#intro-cards)) | confirmed (code) |
| `0x003a0ed8` | `SceneTask_UpdateHeldObject` | each update, after the objects' runners: places the held object before the camera until its time is up | confirmed (code) |
| `0x00356290`, `0x00356308`, `0x003560a8`, `0x00355ab8`, `0x00356188` | track runner: bind camera / bind object, advance, step keys, apply | | confirmed (code) |
| `0x00354d98` | `SceneTrack_Events` | the object, camera and light tracks' events | confirmed (code) |
| `0x00355798` | `SceneTrack_Flush` | a skipped scene's remaining track events, a reduced set ([Skipping](#skipping)) | confirmed (code) |
| `0x002e5300`, `0x002e53e0` | `GoalJoinCinematic` and its goal (type `0x29`) | binds a human to a role | confirmed (code) |
| `0x003541a0` | `Scene_BindHuman` | writes the human into the role | confirmed (code) |
| `0x00354178`, `0x00353b78` | `Scene_IsPreloadedWrap`, `Scene_GetState` | `SceneIsPreloaded`'s worker; the state of an id's slot: 0 none, 1 loading, else the record's state (2 = ready). An AI pair goal (`0x00306ec0`) waits for 2, then plays its two leaders' animation | confirmed (code) |
| `0x00353fc8` | `Scene_Unload` | `SceneUnload`'s worker: a ready scene's user count to 0, then frees finished slots | confirmed (code) |
| `0x00354710` | `Scene_SetCallback` | `SceneSetCallback`'s worker (also from `InitLevel`): looks the Lua function up (script system `+0xcc`) and keeps it at `0x00512af4`; an empty name clears it | confirmed (code) |
| `0x00354768` | `Scene_UnloadAll` | from `UnloadLevel`: ends each of the 12 slots' tasks and unloads them | confirmed (code) |
| `0x00353c68` / `0x00353d60` | `Scene_PlayCinematic` / `Scene_PlayFixed` | `ScenePlayCinematic` / `ScenePlayFixedScene`: `Scene_Play` at the origin with no rotation | confirmed (code) |
| `0x00353e38` / `0x00353e70` | `Scene_PlayAt` / `Scene_PlayAtHeading` | `ScenePlay`'s two forms: straight through, or a position plus a rotation built from a heading (`0x00335ea0`) | confirmed (code) |
| `0x00354c00` / `0x00354bb8` | `Scene_InvokeInPlace` / `Scene_Invoke` | `SceneInvokeInPlace` / `SceneInvoke`: `Scene_Play` with no scene id of its own | confirmed (code) |
| `0x00354038` / `0x00354c48` | `Scene_StopWrap` / `Scene_Terminate` | `SceneStop` (also from `GoalJoinCinematic`'s goal) / `SceneTerminate`: `Scene_Stop(id, false)` | confirmed (code) |
| `0x00354378` | `Scene_GetRoleStart` | a role's start position and rotation (role record `+0x90 + 0x60 × role`, `+0x10`, `+0x20`), loading the scene first; optionally fitted to the bound humans (`Scene_FitToHumans`); out of range gives the identity | confirmed (code) |
| `0x002e51a8` | `GoalJoinCinematic_WalkToMark` | the join goal's step: a move action to the role's start, then a turn to it | confirmed (code) |
| `0x00354c80` | `WarMoveInstance_ctor` | the 0x90-byte scene instance (vtable `0x00544960`), two identity transforms | confirmed (code) |
| `0x00352fa0` | `SceneSlot_WaitSegment` | while the slot's next segment buffer is busy, services the file system and retries | inferred |
| `0x00354558` / `0x003546a0` | `SceneCam_Lock` / `SceneCam_Unlock` | a locked camera (type 4, `LockedSceneCam`) made the current scene camera (`0x003a1558`) over the player's camera (its active sub-camera in modes 5-8 pushed); unlock pops it back and frees it. Used by the in-game camera bindings (`0x002cb538`) | confirmed (code) |
| `0x003555d0` | `SceneTrack_ParticleEvent` | event type 33: the effect starts only when its position is inside player 1's view (each of the six planes, 2 m margin), except the effect `sub_shk`, which always plays | confirmed (code) |
| `0x00356028`, `0x00356090`, `0x003560a0` | `SceneTrack_SetTarget`, `SceneTrack_SetCamera`, `SceneTrack_SetObject` | track runner: set the track's target and re-read its keys at the current frame; the camera form sets `+8` and `+0xc`, the object form `+8` | confirmed (code) |

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
| `+0x34` | users: 1 when the record arrives, one more per later `ScenePreload`, one less per unload ([Loading](#loading)) |
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
at `0x00355e98`, `0x00355ab8`. Like the role clips, **the header's camera track carries the whole scene's events**,
counted from the scene's start: `l99_c1`'s (506 frames of keys) has events up to frame 1990, and its fades and caption
controls fall where the scene needs them only when read that way. Inferred from the data.

### Events {#events}

**Object, camera and light tracks** (`0x00354d98`, confirmed (code) for the actions; counts from the disc):

| Type | Count | Action |
| --- | ---: | --- |
| 13 | 8 | preloads (at load) and plays a sound by hash (`+8`) on the sound manager `0x0050aa84` (inferred: dialogue) |
| 14, 71 | 29, 2 | a sound by hash on the track's human or object; on a car, also its horn or engine (`0x0038d6d8`) |
| 24, 25 | 203, 212 | sends message `0x12` / `0x13` to the track's object; for a `simple_object`, **show** / **hide** ([Objects: the Wonder Wheel](objects.md#wonder-wheel)); other classes not traced |
| 26 | 41,408 | the scene camera's lens: field of view `+8` (degrees), near `+0xc`, far `+0x10` |
| 27 | 300 | fade out over `+8` seconds (`ScreenQueueEffect` type 1 on player 1's view); stops the caption |
| 28 | 380 | fade in over `+8` seconds (type 0) |
| 29 | 38 | loop point: in a looping scene, the frame to restart from (`+4`) |
| 30 | 72 | a light's colour (`+8`), range (`+0x14`) and cone (`+0x10`, degrees) |
| 31 | 33 | calls the scene's end function now (also when skipped past, [Skipping](#skipping)) |
| 33 | 333 | a particle effect at a position and rotation (s16 values, scaled as clip keys) named by `+0x14` with a prefix |
| 41 | 1,422 | caption control: `+4` = 0 shows the next caption, 4 or 5 set that kind (4 hides it), 6 sets a flag first (confirmed (code) at `0x00354d98`; [Movies](movies.md#caption-timing)) |
| 69 | 0 | an object or car action (`0x00396048`, `0x0038d798`) |
| 73 | 23 | **intro card**: holds the object in slot `+4` (`s16`) before the scene camera for `+8` seconds ([Intro cards](#intro-cards)) |
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
   The count-down calls come from the scene's type-31 events ([Events](#events)), which a skip still fires, one call
   each, before the final call ([Skipping](#skipping)); the helpers never check whether the scene was skipped.

Nothing in these helpers shows the HUD again, and `level99`'s `ReturnFunc`s do not either: the cinematic end's
letterbox-out does, as its bars finish going out ([HUD: who shows the HUD again](hud.md#who-shows-the-hud-again)).

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

- **Users.** The arrival sets the user count to 1 (`0x00352430` → `0x00353158`); a `ScenePreload` on a scene already
  loaded or loading, in any state (idle, playing, ended), only adds a user and stamps the request time
  (`0x00353158`): **no callback**, no restart. Confirmed (code) at `0x00353af0`.
- **Unloading** (`0x00351da0`, from the end of a play and from `SceneUnload`): one user less (`0x003531a8`, never
  below 0); at 0 the record and both segment buffers are freed and the slot emptied; otherwise the record stays and
  its header goes **back to state 2** (loaded, idle). Confirmed (code).
- **`SceneIsPreloaded(name)`** is true only for a slot whose header is in **state 2** (`0x00354b38`): false while the
  file loads and while the scene starts, plays or ends (states 4-8). Confirmed (code).

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
   some of the player gang's members, `0x0016b8d0`, not traced; the player panels hidden, `0x001b2380`, and
   **`HideHud`**, [HUD](hud.md#who-shows-the-hud-again)); 1 → 2: player 1's camera (the camera inside a
   wrapper of types 5-8) saves its place in the task and moves to the scene camera's start position, and **the world
   around it is preloaded** with a 10 s budget and the camera definition's radius, loading `<scene>.pak` when it
   exists (`WorldManager_Preload`, [Level loading](level-loading.md#preload)); 3 comes in step 6.
5. Wait until every bound human's character instance and model are loaded (requesting them), then every bound object.
6. Cinematic (or level id `0x3c`): wait until the prepared soundtrack's stream is primed, or its pending re-preload
   has run, stopping the music when it holds both stereo pairs ([Sound: scene
   soundtracks](sound.md#scene-sound)). Cinematic: **system music off** (`0x0041a008(0)`, game state `+0x3f8` = 0,
   [Sound](sound.md); whether it was on is kept in the task's `+0xf0`, and the end turns it back on only then);
   state 3; wait one more update; then clear pad flags `0x40` and `0x800` on every player, and unless `chain`,
   clear the chain-skip flag (`0x0051489c + 0x56e4`).
7. While the delay has not run out, step every bound human toward its start mark (`0x0039d618`, 0.1); wait until all
   arrive or the delay ends.
8. Wait while the scene's **title card** shows, if it has one (`0x001cae60(0x00619570)`, [Subtitles](#title-card)).
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

**At the start** each bound human that is free (below) and not already in a scene: counted in (`+0x1e`), `+0x280` =
scene id, its brain's actions cleared, its state reset, record `+0x08` `0x2000` set, its scene transform set to the
scene's position and rotation, message 10 sent to its held object's task; a held object named by clip event 9
is taken from the object slots
(`0x00227080`); then its clip is pushed as three animation tasks with the end callback `0x003a00b8`, looping when the
scene loops, streaming when it has segments, blended in over 0.3 s only for a non-cinematic scene placed on its humans
(and not in game modes 8 and `0xb`); the instance is put in mode 3. Confirmed (code) at `0x0039d870`.

**The state reset** is a handful of small resets, not a release, confirmed (code): `0x0023e6e8(h, 0)` takes the
human out of shadow (`Brain_SetHiddenInShadow`) and, for player 1, ends a pending gang command and clears `+0x658`;
brain `+0x2d5` = 0 (`0x0028ef00`); the motion reset (slot `+0x14c`, `0x0023f158`: velocities and turn zeroed, the
transform re-applied as it is); `0x00227388` sends message `0x15` to the object whose handle is at `+0x360` (not
traced) and clears it; the record's state code `+0x14` and `+0x18` = 0. None of them touches the state bits of a
grab or a mount, and none calls `Grab_Release` or `Human_BreakPair` ([Combat](combat.md#pair-break)).

**A human in a grab is not taken in.** "Free" (`0x002263d8`) means: state `0x100000000` clear, none of the grab,
mount and tackle-mount bits `0xcf0` (grabbed `0x10` / `0x20`, grabbing `0x40` / `0x80`, mounting `0x400`, mounted
`0x800`), and none of `+0x08` `0xc1e600`. Confirmed (code) at `0x0039d870`. Step 5 above waits (state 4) only for
the `+0x08` bits; a human that is grabbing, grabbed, mounting or mounted is **skipped**: not counted, not reset, no
clip, its `+0x280` left −1, while the scene plays on without it. The grab is not released by the start, on either
human (the partner need not be bound at all). Its join goal still holds a player's pad off, so the grab runs on
until something breaks it: its power running out (`Grab_Release`, 95 / 94; inferred, [Combat](combat.md#pair-break)),
a script teleporting either human, or, for a skipped scene, the end's placement below (both through
`Human_BreakPair`, the partner playing its reaction clip). The cinematic's step 0 → 1 (`Gang_ClearBums`,
`0x0016b8d0`) destroys the player's gang members whose type byte `+0x11b` is 6; it does not release grabs either.

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
camera, `0x003a05a8` objects, `0x003a0768` lights); with none left, a looping scene (runner bit `0x4`, set at the
start from the task's `+0xeb`) starts again from the header's track (restarting the segment chain, slot flag
`0x2000`, or from the loop point of event 29) with the time left over, so the pass repeats seamlessly; any other
scene without roles ends (state 7). It then sets the target's transform to **scene rotation × sampled position +
scene position** (`0x00356188`, target vtable `+0x6c`). Confirmed (code).

**The camera** (when `+0x22` is set): a type-4 camera (`Cam_Scene`) is made, given the scene's name and the current
camera's view, the current camera is **pushed** and the scene camera made current with no blend (`0x0011ee08`); its
lens is the definition's field of view, near and far; each update the track moves it and its own update runs
(vtable `+0x134`). The details of the push, of `CameraMakeActive` during a scene and of the pop are on
[Camera](camera.md#scenes). A scene without a camera keeps the current camera. Confirmed (code).

**Objects** bound by `SceneAddObject` follow their tracks; at the end they are released to the object manager.
**Lights** are made from their definitions at the start and released at the end. Confirmed (code).

### Intro cards {#intro-cards}

When a cinematic introduces a character or a gang, a card with the name and a picture fills the screen for a second
or two (Cleon, Vermin and Rembrandt in `l99_c1`, the Destroyers in `l80_c1`). The card is a **scene object**: the
level script binds it to an object slot like any other (`SceneAddObject`), its own track keeps it parked about 10 m
underground (`l99_c1`'s `cleon` at z −0.96 to −11.8 for its whole track, with no show or hide events), and a camera
event 73 lifts it in front of the camera. Confirmed (code) at the addresses below; the cards' names and places from
the disc.

1. **The event** (`SceneTrack_Events`, `0x003553ac`): the scene task is found through the current camera (a
   `Cam_Scene`'s `+0x1e4`, `0x00353298`), so only a scene with a camera holds a card; `SceneTask_HoldObject`
   (`0x003a0e48`) gets the slot from `+4` (`lh`, compared unsigned with the object count `+0x21`) and the seconds from
   `+8`. When the slot's bound object (object definition `+0x50`) is set, it stores it at task `+0xd0` and the end
   time, the game clock (`0x0050b734 + 0x48`, ms) plus seconds × 1000, at `+0xd4`; sets `0x00512c44`, which stops the
   [room smoke](graphics.md#room-smoke)'s widgets drawing (`OverlayEffect_Tick`, `0x0019bd24`); and places the card at
   once.
2. **Every update** (`SceneTask_Update`, `0x0039d0fc`, after the object runners have set their objects' poses and
   before the lights), `SceneTask_UpdateHeldObject` (`0x003a0ed8`): with `+0xd0` set and the game clock not past
   `+0xd4` (or `+0xd4` 0), and the header's camera count `+0x22` set, the card goes to the camera runner's world pose
   (task `+0x70`: scene rotation × sampled position + scene position, the rotations composed) moved along the
   camera's view axis (its rotation's +y, `0x003363b0`) by

   `d = k × aspect / tan(fov / 2)`

   with the fov the current camera's (`+0x44`, the scene lens's, without the progressive mode's added 3°), the aspect
   the camera's (`+0x4c`, the game aspect: 1.3333 at 4:3, 1.6667 at 16:9, [Graphics](graphics.md#video-mode)),
   and k 0.5, or 0.3 when the device's 16:9 flag is on (device vtable `+0xd0`, `+0x45c`). The card turns as the
   camera (object vtable `+0x6c`). Once the clock is past the end, `+0xd0`, `+0xd4` and `0x00512c44` are cleared, and
   the object's own track puts the card back underground from the next update.

A skip's flush drops event 73 ([Skipping](#skipping)). **Disc check** (scratch script over the scene list): the 23
events are all on camera tracks, one to three a scene, held 1 to 2 s, and every slot names a character or gang card:

| Scene | Frame: card (seconds) |
| --- | --- |
| `l99_c1` | 280: `cleon` (1), 360: `vermin` (1), 1032: `rembrandt` (1) |
| `l80_c1` | 747: `destroyers` (2) |
| `l80_c4` / `l80_c5` | 270: `ajax` (1.75) / 181: `cowboy` (1.75), 264: `snow` (1.6) |
| `l80_c6` / `l80_c7` | 388: `fox` (1.5) / 24: `cochise` (1), 169: `swan` (1) |
| `l2_c1_a`, `l3_c2`, `l5_c4`, `l9_mintro`, `l11_c2` | `orphans`, `hihats`, `hurricanes`, `moonrunners`, `bopp` |
| `l14_c1`, `l20_c6`, `l31_c5`, `l34_c4`, `l55_c1`, `l55_c5`, `l82_c2_a` | `saracens` and `jsbs`, `huns`, `turnbull_a`, `furies`, `lizzies`, `punks`, `samo` |

### Skipping {#skipping}

Only a skippable scene (`+0xe5`), confirmed (code) at `0x0039cbf0`:

- The task counts **60 updates** (2 s) from its creation (`+0xe0`) before a button counts.
- Then, on any player's pad, **cross (`0x0040`) or START (`0x0800`)** held ([pad bits](frontend.md#input)) skips:
  the caption is cleared, that player's view goes **black at once** (`ScreenQueueEffect` type 1, 0 s), and the scene
  is stopped like `SceneStop(id, false)`. START also sets the chain-skip flag (`0x0051489c + 0x56e4`), so the next
  scenes played with `chain` skip at once without a button.
- The stop ends every role's clip, first firing its remaining events (`0x003a0a68`, `0x00103e90`; only when skipped,
  `+0xe4`); the scene then ends as below with the skip flag set. The end fires what is left of the **object, light
  and camera tracks** too, a reduced set, described next.
- A **looping** scene with a loop point (`+0xec`, [Ending](#ending)) is not cut short: the skip's stop only clears the
  loop, the task retries it each update and the scene plays to the end of its pass. Confirmed (code) at `0x0039cbf0`,
  `0x00353a10`.

**The remaining track events on a skip** (`SceneTrack_Flush`, `0x00355798`). In the end (`0x0039f450`), only when
skipped, each bound object's track is flushed (before the object is placed at its end pose), then each light's,
then the camera's (before the camera is popped, and only while the scene camera's field `+0x1e0` is 0, the case in
which it is popped at all; that field's meaning was not traced). The flush walks every event not yet passed, in
track order, regardless of its frame, and does only these; confirmed (code) at `0x00355798`:

| Type | On a skip |
| --- | --- |
| 24 | message `0x12` (show) to the object, unless the **next** event of the track is a 25 (the code means to look further ahead but compares the same event each time) |
| 25 | message `0x13` (hide) |
| 27 | fade out on player 1's view **at once** (`ScreenQueueEffect` type 1, 0 s); the caption is not touched |
| 28 | fade in **at once** (type 0, 0 s) |
| 31 | **calls the scene's end function now** (`0x003a0da8`), once per pending event, as in play |
| 74 | the coloured fade, at once |
| 76 | sets each player's rumble strength (`min(255, +6 × 25.5)`, pads with a motor only), which the camera's pop a moment later sets back to 0 |

Every other type is dropped: sounds (13, 14, 71), lens (26), loop point (29), light colour (30), particles (33),
captions (41), object and car actions (69), holding an object before the camera (73). A type-31 event in the flush
finds its scene through the current scene camera (`0x00512c7c + 0x850`, `+0x14`, the camera's scene id `+0x1e4`),
without the in-play fallback to the object's scene slot; the pointer is cleared (`0x003a1558(0)`) only after the
camera's flush, so every flush of a scene with a camera finds it.

So on a skip the order is: the role clips' remaining events (at the stop); then, in the end, humans placed, each
object's pending events and placement, the lights' and the camera's pending events (the type-31 calls among them,
in frame order), the camera popped, the letterbox out, brains resumed, and **the end function called once more**
with the scene id (step 7). A scene whose tracks hold *n* pending type-31 events therefore calls its end function
*n* + 1 times, the last one after the camera is back. Confirmed (code) at `0x0039f450`, `0x00355798`; not yet seen
at runtime (no save state reaches `level80`).

**`level80`'s intro, skipped** (inferred from the code above and the scripts, read as bytecode). `level80_chapter1`
preloads `l80_c1` at checkpoint 1 and, once it is loaded, runs `SuperRunScene` with it already loaded, seven humans
(Cleon, Rembrandt, Vermin and four scene-only Destroyers), the clubhouse's two doors and a Destroyers object as
objects, `NoClearWanted`, **`NumCallBacks` = 3** and its `ReturnFunc` (an `EndIntroScene`). That function, given 3, 2
or 1, removes one of the clubhouse's three glass panes (`ObjDestroy`: the pane vanishes, with no shatter,
[Objects](objects.md#pane)) and sets off a molotov explosion at the matching flag (`ObjSpawn("dyn_molotv")` there and
`BreakObjectsInRadius(molotov, 0.5)`, which makes the molotov break itself:
[Script types: the Molotov](script-types.md#molotov)); given anything
else (the scene id), it closes the doors, removes the scene's Destroyers gang and goes on to the chapter's next setup.
The three type-31 events on the camera track (about 21.7 to 22.1 s) are flushed by a skip, so `PreCashTheWorld` runs
three times (`ReturnFunc(3)`, `(2)`, `(1)`, `NumCallBacks` down to 0, each pane and explosion at once), then a
fourth time from step 7, which calls `ReturnFunc(id)` and queues `ScreenQueueEffect(0, 0.5)`: the screen, black
since the skip, fades in over 0.5 s. Neither `global.lua` nor the level's scripts look at whether a scene was
skipped: no scene-state query, no skip callback, nothing zeroes `NumCallBacks`; the engine's flush is what keeps the
count right. (`level80.lua` also builds a looping `l80_c1` table, but only in its `SCENETEST` debug path.)

### Ending {#ending}

State 7 (all clips done, or a part ended with no roles) calls the end (`0x0039f450`). Confirmed (code):

1. Cinematic: scene state `0x410` = 0; system music back on when step 6 turned it off.
2. Each bound human: its sounds stopped; when skipped, `+0x280` = −1 and it is **placed at its role's end pose** (in
   the scene's space, through `Human_SetTransform`, so a human still in a grab is let go by `Human_BreakPair` and its
   partner plays a reaction clip, [Combat](combat.md#pair-break));
   its join goal (types `0x27`-`0x2a`) is popped, which gives a player back control ([Humans](#humans)); unbound.
3. Each object: when skipped, placed at its end pose; then its scene slot (`+0x110`) is −1 again, its sound
   stopped, its body flagged (`0x80000000`) and added back to the world, and its record unpinned (`SceneAddObject`
   pinned it and refuses an object already in a scene). The object stays where the scene left it; its **spawn
   record's pose** is written only when the object is stored (streamed out or its zone turned off), so it keeps the
   `ObjSpawn` pose until then. The abort (`0x0039ec60`) does the same, always placing at the end pose. Each
   light: released. Confirmed (code); at runtime (slot 1, after lesson 9) the three bats' records, no longer live,
   held poses on the ground near the pen, not their spawn poses 2.16 m below it, so storing wrote the objects' last
   places (where the player left them, which may not be the scene's end poses).
4. **The camera**: popped and made current over `BlendCam` seconds, the scene camera released and the cameras updated
   once with 0.17 s ([Camera](camera.md#scenes)).
5. Cinematic: **letterbox out** on every player's view (type 3, 1.5 s), which **shows the HUD again** once the bars
   are out ([HUD](hud.md#who-shows-the-hud-again)); player 1's gang command reset
   (`0x0041c4e0(…, 0, 1, 0, 1)`) and its gang's alert cleared (`0x00166708`).
6. With `freeze`, **every brain is resumed**, including any a script had suspended.
7. State 8; **the end function is called with the scene id** (`0x003a0da8`, only while scripting runs, `0x00512b28`
   = 1). For `level99` this is `PreCashTheWorld`, which calls `ReturnFunc` and fades in over 0.5 s.
8. Cinematic and not `final`: preload the world around the camera again, loading `<scene>_end.pak` when it exists.
9. When skipped and `BlendCam` > 0: fade in over `BlendCam` seconds.

The **scene soundtrack** is stopped by the end only when the scene was skipped, and by the abort only for a
cinematic; an unskipped scene's soundtrack plays on to its own end or until the next scene's preload stops it
([Sound: scene soundtracks](sound.md#scene-sound)). Confirmed (code) at `0x0039f450`, `0x0039ec60`; confirmed
(runtime) for an unskipped level99 scene.

The next update sees state 8 with no humans left and frees the task and **unloads the slot** (`0x00353bf0`,
`0x00351da0`): one user less, and with no user left the slot is emptied, so a scene is loaded again for its next
play ([Loading](#loading)). Confirmed (code).

`SceneStop(id, force)` (`0x00353a10`; the binding `0x00367e20` reads `force` with default **false**): a starting
scene goes straight to state 7; a playing one with roles has its clips ended (state 6 while a role is in a paired
move, retried each update); without roles it goes to state 7 **at once, mid-pass**. Only a scene whose task has the
**loop-point flag** (`+0xec`) and is not forced just stops looping instead (`0x003a0be8` clears the runners' loop bit
`0x4` and the roles' clip loop) and ends after the current pass. The start sets `+0xec` = looping **and** at least one
bound track or role clip has event 29 (loop point, `0x0039d870`); a plain looping scene without event 29 therefore
stops at once like any other. Confirmed (code). `WonderWheel_100` has no roles and no event 29 (disc: its 29 object
tracks and camera track carry only events 24 and 25), so a non-forced stop ends it at once: at runtime (PCSX2 2.9.94,
slot read over PINE) it went from state 5 to 7 with its camera track at 10.77 s of the 20 s pass.

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

## Subtitles {#subtitles}

A scene's subtitles are captions of the caption system (`0x00619570`, which is HUD `+0x18d30`), the same one the movies
use: their text is the level's Subtitles chunk, found by the scene's name, and they are drawn by `Captions_Draw`
([Movies: captions](movies.md#captions) has the chunk, the record kinds, the subtitles option and the drawing). What
is particular to scenes, confirmed (code) unless marked:

- **Selecting.** `SceneTask_Create` (`0x003a13d0`) calls `Captions_SelectScene` with the header's name (`+0x08`), so a
  scene's captions are those of the kind-1 record with its name in the current language's section. With no such
  record the previous selection stays (`0x001cad38` restores it).
- **Timing.** Captions follow the scene's own track events, type 41 on the **camera track** (only there on the disc):
  `+4` = 0 shows the next record (`Captions_Next`, `0x001cb190`), 4 hides the caption, 5 sets the ordinary kind
  without changing the text, 6 clears the fade flag below and shows the next. Events fire when the track runner passes
  their frame (`0x00354d98`), so captions run on the scene's 30-a-second clock and stop when the game is paused, while
  the soundtrack streams on its own from event 13 ([Sound](sound.md#scene-sound)). A caption has **no duration of its
  own**: it stays until the next type-41 event, a type-27 fade-out, or a skip. A scene without a camera shows none.
- **Fade-out.** A type-27 event hides the current caption first when the flag `+0x4c` is set (`Captions_SelectScene`
  sets it; an event 41 with `+4` = 6 clears it so that caption outlasts the fade), then sets the flag again.
- **Skip.** The skip clears the caption (`Captions_SetKind(4)` at `0x0039cbf0`, [Skipping](#skipping)); the end
  (`0x0039f450`) does not touch it.
- **The option.** Ordinary captions (kind 3) show only with the subtitles option on (`W_GameState + 0x438`, the
  `PM_Subtitles` screen, [Front end](frontend.md#pm-screens)); a kind-2 record always shows.
- **Position and letterbox.** An ordinary caption is centred at x 0.5, y 0.75 of the screen in the default mode, grey
  `(178, 178, 178, 255)`, wrapped at 0.7 of the width ([Movies](movies.md#caption-drawing)). The letterbox's bottom bar
  covers the bottom `level × 0.12` of the screen ([Graphics](graphics.md#screen-effects)), so the caption sits above
  it. The overlay pass (`0x00156658`) draws the captions **after** the screen effects and the HUD, so they are on top
  of the bars. While the letterbox is in or moving (screen effects `+0x1e8` or `+0x1ec` not 0) `HUD_Render` draws no
  HUD at all ([HUD](hud.md#the-huds-frame)), so nothing else competes with a scene's captions.
- **Speech in play has no subtitles.** The only callers of the caption functions are the scene task, the track runner,
  `Movie_Play` and the title card below; `SoundPlayCommand`, `HuSpeak` and the voice lines never reach them
  ([Sound](sound.md#speech)).

### The title card {#title-card}

A scene whose section starts with a **kind-2 record** opens with a title card, a place-and-time line (two lines with
`<CR>`) drawn in the screen's centre at 1.2 times the text size in dark red, shown whatever the subtitles option says.
`Captions_WaitTitleCard` (`0x001cae60`), called every start attempt (step 8 of [Starting](#starting)) until it returns
true:

1. When the selected scene is not this scene's name, or no record is next: ready.
2. If the next record is kind 2, show it (`Captions_Next`; `Captions_SetKind(2)` sets `+0x54` and `+0x58`).
3. With the game not paused: when `+0x54` is 0 the card is over: hide the caption (kind 4) and return ready. Otherwise
   clear `+0x54`, stamp `+0x5c` with the real clock and **freeze the game for 5,000 ms** (`GameTimer_Freeze(timer,
   5000, 0)`, [Boot](boot.md#timers)); not ready.

So the card holds the scene for 5 s of frozen game time; the freeze cannot be ended with the button while a kind-2
caption shows (`+0x58`), and `Captions_Draw` fades it in over the first 1.5 s and out over the last 1.5 s, all before
the letterbox comes in (step 9). **Disc check** (scratch script over the 25 levels with a Subtitles chunk, English
sections): 23 of 209 scene sections open with a kind-2 record and none has one later; in `level99` it is `l99_c1`, the
intro.

**Disc counts** (the same check): 1,422 type-41 events, all on camera tracks (`+4` = 0: 1,228; 4: 186; 5: 5; 6: 3). In
202 of the 206 scenes that have both a section and a header, the number of show events (0 and 6) equals the section's
ordinary captions; the other 4 are a movie's caption scene listed twice and three scenes with captions but no events
(one without a camera). 12 scenes have show events but no section; in them a show would take the record after the
previous scene's (inferred from `0x001cad38`; `l99_c4` is one, not checked at runtime). Following the events, 11 scenes
end with a caption still current (inferred: it stays on screen after the scene until something hides it). An ordinary
caption is current for 8 to 546 frames, median 89 (3 s).

## Coney's implementation

`repo:src/scenes/` is the format and the player, pure and deterministic: `scene_record.*` decodes header and segment
records (role clips through the animation decoder, keyed tracks, events kept as stored), `scene_list.*` the scene list,
`scene_cache.*` the 12 slots (requests, eviction, the callback on arrival, the two segment buffers), `scene_player.*`
the scene task and the system the bindings work on, and `scene_host.h` what a scene asks of the game (humans, objects,
camera, lights, screen effects, captions, sounds, rumble, brains), which the play mode implements. `scene_disc.*` reads
`scene_list.cnk` and `<name>.scn` from the WAD. The bindings are `repo:src/scripting/scene_bindings.cpp` (the scene
bindings above and the three `GoalJoin*` bindings that bind a role); global.lua's `SuperRunScene` path runs on them
unchanged. The Python reader for the disc check is `repo:python/src/coney_tools/scenes.py`.

**Disc test** (`[disc][scenes]`, counts only): `l99_c1` and `l99_c5` load with their 5 and 1 segments and play headless
with the roles `level99_combat.lua` binds: 2,026 and 500 updates playing, every bound human on its start mark (to
0.1 mm) at frame 0 and within 2 cm of its end mark at the end (on it, after a skip), one soundtrack prepared and started
each. `[disc][story][audio]` plays level99's checkpoint 1 with the sound mixed offline: `l99_c1`'s soundtrack plays on
a real voice for its whole 2,026 frames, and the mix is audible through every one of them. `[disc][scenes][audio]`
then plays the other ten `l99_` scenes with a soundtrack in turn, one to 430 updates after the last cinematic ends
(at least one while the last soundtrack still plays, so the new one is pending): each starts on a real voice and is
heard through its scene.
`[disc][story][combat][scenes]` plays checkpoint 1 through lesson 6, whose power move still holds a bum as `l99_c7`
starts (1 human holding, 1 held), and finds no human holding, held or grabbed 90 updates after the scene.

**Coney choices** where the page is silent:

- A record arrives on the update after `ScenePreload` (Coney's reads are synchronous); a play binding reads it at once.
- The bindings work on the scene system the binding context holds at each call. Gameplay makes one per level, over
  the disc's scene list (read once), before the level script runs, and drops it when the level ends; the front end
  makes one the same way for `level100`, hosted by its world ([Front end](frontend.md#coneys-implementation)). With
  none (a test) `ScenePreload` and the three play bindings are a stand-in that keeps the scripts' scene flow moving: a
  scene loads and ends at once, its load and end functions called with its id at the scripts' next update.
- Coney runs a level's start callback before it loads the level (`StartAmbient` binds `l99_c1`'s roles then), so a
  host attached later is told of the humans already joined, at their roles' start marks.
- The join goal's brain switch is done for player 1 only: his brain is off from the join (`BrDead(brain, 1)`) until
  the scene lets him go, when the goal's destroy turns it on again (`BrDead(brain, 0)`). This is what gives `level5`'s
  boss fight the pad back in stage 3: the stage-2 callback switches the brain off and relies on the scene `l5_c8` to
  turn it on. An AI human's brain is not switched, and a `BrFlush` that drops the join goal before the scene ends does
  not give the pad back.
- `ScreenQueueEffect` also goes to the scenes' host in play: the stage owns player 1's view's fades and letterbox,
  which the scene events use too (the original's effect managers are the views', [Graphics](graphics.md)).
- A role is driven from its clip alone: root motion (section A turned by the heading, all three axes; the host may
  settle the feet on the ground) and the 21/22 marks, which the data shows hold `f32` positions and headings. Clip
  events other than 13, 21 and 22 are not acted on.
- Keyed rotations are slerped; the camera looks along its rotation's +y with +z up (inferred: `l99_c5`'s first camera
  key aims +y at the roles).
- Event 30's colour is read as bytes r, g, b at `+8` (the cone's float is at `+0x10`); 69 does nothing.
- Event 73 holds its intro card as [Intro cards](#intro-cards) says (`heldObjectPose`, tested in
  `coney_tests "[scenes]"`); the host says whether the game is 16:9 (Coney's play is 4:3 so far). Coney does not
  stop the room smoke while a card shows. Before this, the cards stayed parked underground and never showed.
- A scene with roles but none bound ends when its tracks do; an aborted scene's end function is not called.
- A track's events fire against the scene's frame (the part's start frame plus its time), the header's and the
  current part's alike.
- A skip flushes the tracks as [Skipping](#skipping) says: in the end, each bound object's pending events (then its
  end pose), each light's, then the camera's before its pop, only types 24, 25, 27, 28, 31, 74 and 76, fades at once,
  and a rumble set back to 0 by the pop; a looping scene with a loop point plays out its pass. Before Coney did this,
  `level80`'s skipped intro stayed black for the rest of the level: `PreCashTheWorld` fades back in only once its
  three `NumCallBacks` are spent, by the camera track's type-31 calls.

**The play mode's stage** (`repo:src/platform/scene_stage.*`, `repo:src/platform/play_level_scene.cpp`) is the
`SceneHost` the play mode gives the scene system. It draws the scene camera's view (interpolated between steps, cut
when it jumps more than 1 m), the letterbox (two bars 0.12 of the screen high,
[Screen effects](graphics.md#screen-effects); the `Display/Cutscene letterbox` setting leaves them out) and the fades
over the frame. In a level the scene camera is player 1's ([Camera](camera.md#scenes)): the scene's start pushes the camera
shown, its keys set the scene camera's view and its end pops the camera back over `BlendCam` seconds (in the sandbox,
with no cameras, the end is a cut). The player and the level's cast (the humans its scripts made) are posed from
their roles' frames and, when let go, stand where the scene left them (a cast human placed as a spawn places it),
placed at the release itself so the end function's own moves (`level87`'s `TeleportToFlag`) win ([Ending](#ending));
that placement breaks any pair the human is in, its partner playing its reaction ([Combat](combat.md#pair-break)). A
player or cast human that is grabbing, grabbed, mounting or mounted at the start is left out as [Humans](#humans)
says (the stage's free test, `SceneHost::humanFree`): not taken in or posed, its grab going on, and at the end placed
only when the scene was skipped. Other bound humans are drawn as puppets of their characters. Sounds go through
the sound engine
([Sound](sound.md#scene-sound)): the soundtrack is prepared on the scene's load (or, pending a stream pair, by a
later sound update), a cinematic's start waits for it, event 13 starts whatever is prepared, and only a skip or a
cinematic given up stops it; otherwise it plays on past the scene's end. The music ducks to 0.75 while a cinematic
runs. The log says `scene sound: <hash> prepared` (or `pending`) and `scene sound: started`; events 14 and 71 play at
the human the scene holds, else unplaced on the effects bus. **Coney stand-ins:**
captions, particles and rumble are counted; a puppet's model follows its role's name (`warrcl` is Cleon's `warr_cl`,
and so on); a cast human keeps running its brain while a scene poses it.
`--scene NAME` with `--play-level` plays one scene at once ([Building](../guides/building.md#playing-a-level)).

**In play** (`--play-level level99`, headless): at checkpoint 1 `StartAmbient` runs `SuperRunScene(IntroScene)`, and
`l99_c1` plays through the scripts with its seven bound humans posed on the stage; it ends after 2,026 frames, calls
`P1.StartTraining`, whose fade-in reaches the stage, and Rembrandt stands at (−289.03, 120.29), as in the runtime
trace above.

## Open questions

- Which human state the start resets (answered: small resets, [Humans](#humans); a grab is never released by it).
  Messages `0x95` / `0x96` set the human's transform from the scene's space (`Human_HandleMessage`, `0x00245920`),
  which breaks a pair ([Combat](combat.md#pair-break)); the start's message 10 goes to the human's held object,
  not the human.
  What the object at human `+0x360` is (message `0x15` at the start).
- How `ScenePlayAnimation` fits the scene to its humans (`0x003547e8`), and the 0.1 at `0x0039d618`.
- What the camera definition's `+0x50`, `+0x5c`, `+0x68` and the header's `+0x30` name are for.
- Track events 24/25 (messages `0x12`/`0x13`), and most clip event types besides 9, 11, 21, 22.
- Where a scene's captions come from (answered): the level's Subtitles chunk, by the scene's name
  ([Movies](movies.md#caption-text)).
- Event 30's argument layout (colour at `+8` and cone at `+0x10` overlap as three floats).
