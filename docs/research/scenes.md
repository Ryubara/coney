# Scenes (in-engine cutscenes)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). The disc check
`coney-tools wad scenes` ([coney-tools](../guides/coney-tools.md#scenes)) parsed all 2,765 records of the NTSC-U disc on
2026-10-06 and prints counts and hashes only. Runtime claims come from the `warriors_passive` trace
(`repo:research/traces/scenarios/warriors_passive.toml`, recorded in PCSX2 2.9.94 on 2026-10-05,
[AI](ai.md#level99-fight)) and from [Characters](characters.md#creation), and from the scene-binding hooks of
[Cast at runtime](#cast-runtime) (`repo:research/traces/scenarios/scene_cast_state.toml` and
`repo:research/traces/scenarios/scene_cast_fence.toml`, PCSX2 2.9.94, 2026-10-11).

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
| `0x003541a0` | `Scene_BindHuman` | resets the human (`Human_ResetForMode(h, 1)` at `0x003541d8`), then writes it into the role ([The reset at the join](#join-reset)) | confirmed (code) |
| `0x002325e0` | `Human_ResetForMode` | the reset every join runs: pairs, grabs, holds, rage, stun, props ([The reset at the join](#join-reset)) | confirmed (code) |
| `0x0016b8d0` | `Gang_ClearBums` | a cinematic's start destroys the bums in player 1's gang ([The player's gang](#gang)) | confirmed (code) |
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
| `0x00354280` | `Scene_BindObject` | `SceneAddObject`'s worker: writes the object into the slot by index (definition `+0x50`) and pins its record; refuses an object already in a scene | confirmed (code) |
| `0x00299248` | `Goal_StartParlay` | `GoalStartParlay`'s worker: preloads `<base>_000` to `<base>_004` (formats `0x00561760`-`0x00561780`) and pushes the parlay goal; no script on the disc calls it ([Which level](#which-level)) | confirmed (code) |
| `0x002cb5d0` | `Brain_PlayScene` | the parlay goal's steps: looks the five parlay scenes up by name and plays them as animations | confirmed (code) |
| `0x00306c68` | `AddressTactic_Init` | `TacticAddress`'s tactic: looks its scene up by the name the script gives (no script on the disc calls it) | confirmed (code) |

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
or object at runtime (0 in the file). A role's name looks like a model name (`warrcl`, `warrrecv`, `warrashcv`, ...)
but is only a label: binding is by index, and no code maps the name to a model ([Cast and props](#cast)).

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
4. **Cinematic only**, steps of the global scene state `0x0051489c + 0x410` (one per update): 0 → 1 (the bums in
   player 1's gang destroyed, `Gang_ClearBums` `0x0016b8d0`, [The player's gang](#gang); the player panels hidden,
   `0x001b2380`, and
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

**Binding.** `GoalJoinCinematic(human, scene, role, gait)` pushes goal `0x29` and at once resets the human and writes it
into the role (`0x003541a0`: the reset always, [The reset at the join](#join-reset); the role only if it exists and
the human is in no scene, human `+0x280` = −1); a pad-controlled
human's brain is switched off (`BrDead(brain, 1)`) right away. The goal's Start switches an AI brain off too
(remembering whether it was), pushes a move to the role's start mark at the given gait and a turn to its start
heading; its Process finishes when the scene has. Its End restores an AI brain; its destroy turns a pad brain back on
(`BrDead(brain, 0)`). Confirmed (code) at `0x002e53e0`, `0x002e5480`, `0x002e55a0`, `0x002e55e8`, `0x002e5618`.

**At the start** each bound human that is free (below) and not already in a scene: counted in (`+0x1e`), `+0x280` =
scene id, its brain's actions cleared, its state reset, record `+0x08` `0x2000` set, its scene transform set to the
scene's position and rotation, message 10 sent to its held object's task; its **pick target** (`+0x33c`,
`Human_SetObjectTarget` `0x00227080`) set to the scene object in the slot that the value (`+0x04`, read by
`Clip_GetEventValue` `0x00101ce0`) of its clip's first type-9 event names, so the clip's own event 9 later takes that
object into the hand ([Objects in a hand](objects.md#held); `l80_c5`'s cops hold their batons this way,
[Scripts](scripting.md#level80-arrest-fire)); then its clip is pushed as three animation tasks with the end callback
`0x003a00b8`, looping when the
scene loops, streaming when it has segments, blended in over 0.3 s only for a non-cinematic scene placed on its humans
(and not in game modes 8 and `0xb`); the instance is put in mode 3. Confirmed (code) at `0x0039d870`.

**The state reset** is a handful of small resets, not a release, confirmed (code): `0x0023e6e8(h, 0)` takes the
human out of shadow (`Brain_SetHiddenInShadow`) and, for player 1, ends a pending gang command and clears `+0x658`;
brain `+0x2d5` = 0 (`0x0028ef00`); the motion reset (slot `+0x14c`, `0x0023f158`: velocities and turn zeroed, the
transform re-applied as it is); `0x00227388` sends message `0x15` to the object whose handle is at `+0x360` (not
traced) and clears it; the record's state code `+0x14` and `+0x18` = 0. None of them touches the state bits of a
grab or a mount, and none calls `Grab_Release` or `Human_BreakPair` ([Combat](combat.md#pair-break)).

**A human in a grab at the start is not taken in** (rare: the join has already let a bound human go,
[The reset at the join](#join-reset)). "Free" (`0x002263d8`) means: state `0x100000000` clear, none of the grab,
mount and tackle-mount bits `0xcf0` (grabbed `0x10` / `0x20`, grabbing `0x40` / `0x80`, mounting `0x400`, mounted
`0x800`), and none of `+0x08` `0xc1e600`. Confirmed (code) at `0x0039d870`. Step 5 above waits (state 4) only for
the `+0x08` bits; a human that is grabbing, grabbed, mounting or mounted is **skipped**: not counted, not reset, no
clip, its `+0x280` left −1, while the scene plays on without it. The grab is not released by the start, on either
human (the partner need not be bound at all). Its join goal still holds a player's pad off, so the grab runs on
until something breaks it: its power running out (`Grab_Release`, 95 / 94; inferred, [Combat](combat.md#pair-break)),
a script teleporting either human, or, for a skipped scene, the end's placement below (both through
`Human_BreakPair`, the partner playing its reaction clip). The cinematic's step 0 → 1 (`Gang_ClearBums`,
`0x0016b8d0`) destroys the player's gang members whose type byte `+0x11b` is 6; it does not release grabs either.

**`l99_c7` and the lesson-6 bum** (issue #225). `P1.PowerMovesDone`, 2500 ms after the extension 59 is counted,
binds `l99_c7`'s 13 roles through `SuperRunScene` ([below](#superrunscene)): 0 Cleon, 1 Vermin, 2-4 `Generic1`-`3`,
5 `NilHandle`, **6 `CombatEnemy.GenWarrior`** (the lesson's victim, Rudy; role name `rudy`), 7-10
`CombatEnemy2.SecondWave01`-`04` (`bum01`-`bum04`), 11 the player (`warrrecv`), 12 `player2` or `NilHandle`; objects
the left door of `ChA00` and `dyn_beerbottle`; return `P1.SetupSnapAttacks`. Before that it clears Rudy's message
handler 1 and flushes his brain (`BrFlush`). Inferred from the disassembly of `level99_combat.lua` (names and order
only). In the original the 59 pair is **gone by the time the scene starts**, so both are free and bound
(confirmed (runtime), PCSX2 2.9.94, two plays of checkpoint 1 with the course of
`repo:research/traces/missions/level99_cp1.toml`, the pad let go after `P1.PowerMovesDone` so the scene was not
skipped, hooks `pair-break`, `throw-clear-link`, `prop-release`):

| Update (second play) | What happened |
| --- | --- |
| 2057 | 59 starts (Rembrandt state `0x1005`, Rudy `0x2001`, held `0x400000`, Rudy's clip 60); `P1.PowerMovesDone` scheduled |
| 2131 | `P1.PowerMovesDone`: `ScenePreload("l99_c7")` |
| 2141 | 59's event `0x31` removes the spray can ([Combat](combat.md#power-move-props)) |
| 2142 | `ScenePlayCinematic`; in the same update `Human_ResetForMode` (`0x002325e0`) runs on every human: `Human_ReleaseCarriedProps` for each, and **`Human_BreakPair(Rudy)`** (return `0x00232690`), which clears the throw link (`Throw_ClearLink(Rembrandt, —)` from `0x00258db4`): the pair ends there, 85 updates into the 94-update 59 |
| 2143 on | both in the scene: state `0x800000`, scene id 2630 at `+0x280`, clip 388; Rudy stands on role 6's start mark (−282.58, 127.29) |
| about 2540-2730 | Rudy's role clip moves him (−282.6, 127.3) → (−276.6, 122.2): the Warriors drag him off |

In the first play (scene skipped by the course's presses) the same states showed and the end placed Rudy at role 6's
end mark (−273.62, 120.81). Which caller runs `Human_ResetForMode` at the cinematic's start was not traced (open).
This qualifies the paragraph above: the scene task's own start does not release a pair (confirmed (code) at
`0x0039d870`), but in this cinematic the pair was broken in the play call's update, before the free test, so a
player's grab or paired move does not survive a `SuperRunScene` cinematic here; whether every cinematic does this
waits for that caller.

**Coney** (#225 and #281, implemented): `SceneSystem::joinHuman`, the bind, resets the human first, before it checks
the scene or the role (`SceneHost::resetHumanForJoin()`, `Human::resetForJoin()`), with the steps of
[the reset at the join](#join-reset) that Coney models: a throw's aim cancelled, any pair broken (`Human_BreakPair`,
[Combat](combat.md#pair-break)), a wound ended, then `HuSetNormalMode(human, true)` (a grab, a stun, rage and a fall
ended, the idle, at least 1 health), and the carried prop gone; and every raging human's rage ends at once
(`human::endAllRage()`, play mode's join reset). A human never bound is not reset. Stand-in: fire, mini-games, overlay
tasks, held kind-`0x27` / `0x10` objects, slow motion, a climb's body and a picked object are not in Coney, so those
steps are left out. Rudy then joins role 6 and is moved by his role clip. Disc tests: through lesson 6 into `l99_c7`
Rudy is in role 6 and moves 11.2 m (`tests/platform/disc_level99_power_lesson_test.cpp`); the player still holds Rudy
on the update before the play and no human is in a pair as it starts
(`tests/platform/disc_level99_scene_grab_test.cpp`). Known difference: Coney starts `l99_c7` 7 updates
earlier than the original (update 78 of 59 against 85), probably because Coney's scene preload is instant where the
original takes 11 updates from `ScenePreload` to the play, so the reset removes the can before 59's own `0x31`.

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

### The reset at the join {#join-reset}

**Every human bound to a role is reset first, at the join, not at the scene's start.** `Scene_BindHuman`
(`0x003541a0`) begins with `Human_ResetForMode(human, 1)` (the call at `0x003541d8`), before it loads the scene,
checks the role or checks that the human is in no scene, so the reset happens even when the bind is then refused.
`Human_ResetForMode` has three callers: `Scene_BindHuman`, the `HuSetNormalMode` binding's worker
`Human_SetNormalMode` (`0x0023a210`, which passes the script's `full` flag) and `GeneralParlayGoal_Init`
(`0x002cb348`). `Scene_BindHuman` in turn is called by the six goals that put a human in a scene:
`GoalJoinCinematic`'s constructor (`0x002e53e0`, the call at `0x002e5464`, run when the binding pushes the goal),
`GoalJoinAnimation` (`0x002e5750`), `GoalJoinFixedScene` (`0x002e5188`), the join-scene goal's Start (`0x002e4eb8`)
and the two play-animation goals' Starts (`0x002e4a10`, `0x002e4ba0`). Confirmed (code) at those addresses.

So the rule is: **every scene of every kind** (cinematic, fixed scene, animation) resets each human it binds, once,
when the script binds it; a role left to `NilHandle` resets nobody, and humans that are not bound are not reset
(except for rage, below). For a `SuperRunScene` cinematic, `gPlayCutScene` binds every role and then calls
`ScenePlayCinematic` in the same Lua call ([`SuperRunScene`](#superrunscene)), which is why the reset showed at
runtime in the update of the play call (`l99_c7`, issue #225). The reset is exactly `HuSetNormalMode(human, true)`
([binding](../references/bindings/character.md#husetnormalmode)); confirmed (code) at `0x002325e0`:

1. A human with any hold flag drops its overlay task; **every player in rage** (`+0xe0` bit `0x80000`) leaves it
   (`Humans_EndAllRage`, all players, not only this human); fire put out; a mini-game ended; a throw's aim
   cancelled; `+0x644` = 0.
2. When the human is in any paired or special state (mask `0x7001e193ff0`): **the pair is broken**
   (`Human_BreakPair`, [Combat](combat.md#pair-break)), then a mini-game aborted, a special state and a workout
   ended, a mug (`0x100` / `0x200`), a tag (`0x2000000`), a tackle hold (`0x8000000000` / `0x10000000000`), a tackle
   (`0x800` / `0x400`) or a grab (`0xc0` / `0x30`, both sides) ended with its partner, and wounded (`0x10000`), down
   (`0x80000`) and stunned (`0x100000`) ended.
3. Always: a held object of kind `0x27` or `0x10` dropped, the carried props released (`Human_ReleaseCarriedProps`),
   the fight stance left, and the slow motion a player caused turned off.
4. Then (the join always passes 1): state bit `0x80000000` and bits `0x3c00000000` cleared (ungrounding first when
   set), the body restored after a climb, an object it was picking (hold flag `0x4000`) unpinned and let go, its idle
   clip rebuilt with action 0, and the motion reset (vtable `+0x14c`). Last, state bit `0x20000000` is set.

What this means for [Humans](#humans): a human that is in a grab, a mount or a tackle when the script binds it is
let go there and then, its partner too (the partner need not be bound), so it is normally free by the time the
start's free test runs. The free test still matters for a human that gets into a pair between its join and the
scene's start (the start can wait for loads and for the walk to the marks).

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

### Cast and props: what a role's or object's name means {#cast}

**The original never turns a role's name or an object definition's name into a model.** A role is drawn as
whatever human the script binds to it, and an object slot moves whatever object the script binds; a role or slot
nobody binds plays nothing. Confirmed (code):

- **Binding is by index only.** `Scene_BindHuman` (`0x003541a0`) and `Scene_BindObject` (`0x00354280`) write the
  handle's object into definition `+0x50` of the role or slot given by number, checking only the index against the
  header's counts (`+0x20`, `+0x21`) and that the human or object is in no other scene.
- **Nothing reads a role's name.** Every load of the role table (header `+0x90`) in the scene code
  (`SceneRecord_Fixup`, `Scene_BindHuman`, `Scene_GetRoleStart`, `Scene_FitToHumans`, `SceneTask_StepToMarks`,
  `SceneTask_Start`, `SceneTask_ClipDone`, `SceneTask_EndClips`, `SceneTask_StopLooping`, `SceneTask_End`,
  `SceneTask_Abort`) reads a pose (`+0x10` to `+0x4c`) or the bound human (`+0x50`), never `+0x00`; no binding takes a
  role by name.
- **One read of an object's name**, in the start (`0x0039d870`, the compare set up at `0x0039e4a0`): when a bound
  object's definition name begins with **`s_`** (its first two bytes `0x5f73`), the object gets message 8 before its
  track is bound. For a `simple_object`, the class of every object the scripts bind to an `s_` slot (44 of 44), message
  8 puts it back at its attached pose ([`simple_object`](objects.md#simple-object)). 775 of the 2,317 object slots on
  the disc are `s_` names (`s_spraycan01`).
- **What decides the look**: a human's model is its **character type**, the second argument of the `HuCreate` that
  made it ([Characters](characters.md#creation), [character list](../references/characters.md)), so the same role
  wears different models in different scripts (`l11_c1`'s `warrcl` is Cleon's `warr_cl_gen`, `l80_c1`'s Cleon is
  `warr_cl`). An object's model is its **object type**: the scripts bind `Objects.<key>`, which holds the
  `ObjSpawn(type, ...)` of that type (a type spawned again is keyed `<type>01`, `<type>02`), or a door's leaf
  (`GetLeftDoorHandle`, `GetRightDoorHandle`). An intro card is an object type of its own: `l99_c1`'s slot `cleon`
  is bound to `dyn_intro_test`, not to the level's `dyn_cleon` ([Intro cards](#intro-cards)).
- **One cast detail at the start** (`0x0039d870`, around `0x0039e7dc`): for a cinematic, a bound human of behaviour
  class `0x12`, `0x14` or `0xbc` (Cowboy's classes) with no hat object at `+0x364` gets the handle of a free object of
  its class's hat type (class `+0x14c`, looked up by name among the object manager's kind-24 objects and taken only
  if not attached), so a Cowboy who lost his hat may wear it again in the scene (inferred: the hat,
  [Characters: where a hat comes from](characters.md#creation)).

**The names are labels** (inferred from the disc): the 1,236 differently named headers hold 3,144 role slots under
689 distinct names, in several conventions: a model name without its underscores (`warrrecv` for `warr_re_cv`), the
same with a number (`warrso01`), two-letter initials (`sw`, `ve`, `cb`), a gang and a part (`destso01`, `cops02`) or
a first name (`sanchez`, `mercy`). Only 68 of the 689 equal a model name with or without its underscores. Object
definitions are named the same way (805 distinct names: props, `s_` props, card names such as `cleon`).

**The cast the scripts give** (scratch script over the disc's compiled scripts, names and ids only; inferred, since
it is read from the code statically and a script may pick a table by checkpoint). 266 scene tables in the scripts
(the `SuperRunScene` table: `SceneId`, `Humans`, `Objects`, [From Lua](#superrunscene)) cover 188 scenes. Following
each `Humans` entry through assignments to a `HuCreate` gives, of their 1,374 roles: 937 one character type, 112
two or more (the same name made by several `HuCreate` calls of a level, `Warriors.Rembrandt` as type 30 or 32 in
`level99`), 25 the player with no `HuCreate` in sight, 204 not resolved (globals set from calls the walk cannot
follow, `hVermin`, `Ajax`), and 96 not played (15 `NilHandle`, 81 beyond the list's end). Of the 937, the role name
equals the model without underscores in 142. Their 563 object slots: 346 an object type, 31 a door leaf, 41 not
resolved, 145 not bound. The result is the **Cast** and **Props** columns of [Scenes and
movies](../references/scenes.md); `l99_c1` reads Cleon `warr_cl_gen`, Vermin `warr_ve` or `warr_ve_gen`, Rembrandt
`warr_re` or `warr_re_cv`, nobody, three soldiers `warr_so_vb2_hr`, `warr_so_vb1_hr`, `warr_so_vb3_hr`, and Ash
`warr_ty`, with props `dyn_s_spraycan`, `dyn_rembrandt`, `dyn_vermin` and `dyn_intro_test`.

**A stand-in from the name alone** (for a viewer that shows a scene with no script, not the original's behaviour):
taken in order, (a) the model whose name without underscores is the role's (with or without a trailing number), (b)
for one to three letters, `warr_` and the first two, (c) the gang code of the first four letters with `so` in the
name, the gang's first `<gang>_so` model, (d) the gang's first model. It names a model for 3,029 of the 3,144 slots;
checked against the 850 script-resolved roles it covers, it gives the same person (same `<gang>_<who>` start) in
458, the right gang in 340 and another in 52. Inferred (measured on the disc).

#### Cast at runtime {#cast-runtime}

**Confirmed (runtime)**, PCSX2 2.9.94, 2026-10-11, with three hooks of `repo:research/traces/patches.toml`:
`scene-bind-human` at `0x00354248` (the store of `Scene_BindHuman`: the scene id, the role index, the role's name,
the human's name `+0x80` and character type `+0xd0`), `scene-bind-object` at `0x00354340` (the same store in
`Scene_BindObject`: the slot's name, the object's type index `+0x112` and type name through `+0xc0`) and
`scene-find` at `0x00353698` (every scene name lookup and its caller). Scenarios `scene_cast_state` (no input, from
a state passed with `--state`) and `scene_cast_fence` (slot 10, the charge through the wooden fence); thirteen
states in `level99` and `level80`, 40 to 90 s each. A state saved during a level load stops the tick count for
longer than `pcsx2 record`'s 5 s stall limit, so those runs called the recorder with a 180 s limit; the command
has no option for it yet.

**`l99_c1`** (scene 2613), from a state saved as checkpoint 1 starts (the level load is still running; the binding
comes at update 5): seven of its eight roles bound:

| Role | Name in the scene | Human bound | Character type | Model |
| ---: | --- | --- | ---: | --- |
| 0 | `warrcl` | Cleon | 2 | `warr_cl_gen` |
| 1 | `warrve` | Vermin | 27 | `warr_ve_gen` |
| 2 | `warrrecv` | Rembrandt | 32 | `warr_re_cv` |
| 3 | (`-`) | none | | |
| 4 | `warrso02` | Generic2 | 59 | `warr_so_vb2_hr` |
| 5 | `warrso03` | Generic1 | 58 | `warr_so_vb1_hr` |
| 6 | `warrso04` | Generic3 | 60 | `warr_so_vb3_hr` |
| 7 | `warrashcv` | Ash | 40 | `warr_ty_cv` |

and its four object slots: `s_spraycan01` ← `dyn_s_spraycan`, `rembrandt` ← `dyn_rembrandt`, `vermin` ←
`dyn_vermin`, `cleon` ← `dyn_intro_test`. So the model is the bound human's type, never the role's name: `warrve`
draws `warr_ve_gen`, `warrso02` a `_vb2_hr` soldier, and a role nobody binds draws nothing. This settles two of
`l99_c1`'s either-or roles (Vermin `warr_ve_gen`, Rembrandt `warr_re_cv`) and corrects the static walk for Ash,
who is type 40 (`warr_ty_cv`), not 38 (`warr_ty`); the [scenes list](../references/scenes.md) now says so. A
checkpoint that makes these humans with other types would show other models; only checkpoint 1's start was read.

**The subway scenes** bind objects only: `Coney_Sub2` (scene 5, `level99`, five runs) and `Coney_Sub4` (scene 7,
`level80`) each get seven `SceneAddObject` calls from the script, slots 0-2 and 4-6 (`s_train_low`, `s_train_low01`
... in `Coney_Sub2`; `s_train_med_ful` ×6 in `Coney_Sub4`) all bound to objects of type `dyn_s_train_med_full`
(type index 1144), so `s_train_low` draws a `med_full` car: the slot name is not the type. Slot 3, `train_sound`, is
bound to an object with no world-object type (type index 0, no type name at `+0xc0`); what it is was not traced.

### Which level a scene plays in {#which-level}

**The scene list is global and no level owns a scene.** `scene_list.cnk` is one list for the whole game
(`SceneList_Load`, `0x00353460`), every `.scn` record is a file of the WAD, and a scene id is its index in that list.
A level uses a scene when its scripts name it, and every name reaches the scene system from a script. Confirmed (code)
at the three name lookups and their callers:

- `SceneList_FindBySubstring` (`0x00353698`, the first record whose name contains the string) is called by
  `ScenePreload` (`0x00353f88`), `SceneIsPreloaded` (`0x00354b38`), the fix-up (the record's own name),
  `Goal_StartParlay` and `Brain_PlayScene` (the parlay names `<base>_000` to `<base>_004` built from the script's
  `base`), and `AddressTactic_Init` (the name `TacticAddress` was given).
- The exact-name lookup (`0x00353778`) serves only the segment chain, and the lookup by id (`0x00353730`) only
  `SceneSlot_Load`. The play bindings, `GoalJoin*` and `GoalPlayAnimation` take ids that a script got from
  `ScenePreload`.
- The one scene record read outside the scene list is a movie's caption timing, `<movie>_sub.scn`, which `Movie_Play`
  reads as a file and plays on its own ([Movies](movies.md#caption-timing)).

So C++ code names no scene of its own, and **`GoalStartParlay` and `TacticAddress` are never called** by a script on
the disc (both have no call in any compiled chunk), so the parlay families (`l14_p1_000` to `_004`, `l5_m5_000` to
`_010`, ...) and any tactic scene are not reached by name in the shipped game (inferred).

**At runtime every lookup came from a script.** Confirmed (runtime), PCSX2 2.9.94, 2026-10-11, the `scene-find`
hook ([Cast at runtime](#cast-runtime)) over thirteen runs in `level99` and `level80`: every call of
`SceneList_FindBySubstring` came from `ScenePreload` (return `0x00353f9c`), each followed a few updates later by
the fix-up finding the loaded record's own name (`SceneRecord_Fixup`, return `0x003523fc`); no other caller. The
names were `l99_c1`, `l99_c9`, `Coney_Sub1`, `Coney_Sub2` and `Coney_Sub4`, each in the level whose scripts hold
it. The subway scenes are among them: `level99`'s script preloads `Coney_Sub1` or `Coney_Sub2` (its `random`
binding is called just before, so it picks one) and `level80`'s `Coney_Sub4`, so a `*_Sub*` scene is a level
script's like any other, and the ones no script names (`Benson_Sub1`, `Train_Sub2`, `EConey_Sub1`, ...) were not
seen loaded.

**No computed name reaches the unnamed families** (inferred, from the disc's compiled scripts, names only). A
script could build a scene name by joining strings, so every string constant of every compiled chunk was compared
with the scene names: the only constants that are the start or the end of a scene name without being one are
`rumble_` (33 chunks, joined with a level number: `rumble_102`, [above](#which-level)), `L1_IN`, `L86_M`,
`turn_bd`, `level95.lua`'s `l`, `_in` and `_out` (movie and caption names) and `level84.lua`'s `_01` to `_07`
(its debug `PlaySceneTest`). None is a piece of an `l<N>_l1_`, `_p1_`, `_s1_`, `_m2_` or `_t<N>_` name, so those
families are reached by neither a literal nor a joined name; with `GoalStartParlay` and `TacticAddress` never
called, they are not played in the shipped game.

**Where a scene plays** is the world of the level whose scripts play it: cinematics and fixed scenes are played at
the origin, so the records hold that world's coordinates ([Playing](#playing)). Evidence for each scene, from a
scratch pass over the disc (names and counts only):

| Source | Scenes | Evidence |
| --- | ---: | --- |
| a level's scripts hold the name as a string (any string constant, not only a `ScenePreload` argument: most go through a `SuperRunScene` table) | 309 | inferred (the scripts are the only way in, above); one is named by two levels' scripts (`Pel_subway`: `level31`, `level115`) |
| the name is a scene marker in a level's Subtitles chunk (the captions come from the current level's `.lev` only, [Movies](movies.md#caption-text)) | 3 | inferred; 210 markers in 26 levels, and each of the 192 scenes that the scripts also name is marked in the same level; the others are the 11 caption timings below and these 3 |
| a movie's caption timing, `l<N>_in_sub` / `l<N>_out_sub` | 11 | confirmed (code), [Movies](movies.md#caption-timing) |
| only a level's debug scene test (`level<N>_scenetest.lua`) names it | 3 | inferred |
| a level number at the end of the name (`rumble_102`, `wonderwheel_81`; the Rumble levels' scripts hold the string `rumble_`) | 7 | inferred |
| only the `l<N>_` prefix | 663 | speculative, below |
| none (shared animation sets such as `gen_*`, `bar_*`, `craps_*`; the subway scenes `Benson_Sub1`, `Train_Sub2`, ...; `l4_*` and `l88_*`, with no such level) | 244 | |

**The `l<N>_` prefix names the mission, not the world.** Of the 299 `l<N>_` scenes whose level the scripts or the
subtitles give, 280 play in level *N* and 19 in the hub, `level95` (`l3_c1`, `l5_c1`, `l14_c6_a`, `l31_c1`, ...):
mission starts and ends played in the clubhouse, at around (−170, 90, −193) and (−200, 130, −698) in the hub's world,
named by `level95.lua` and captioned in `level95.lev`. The scene soundtracks agree with the prefix, not the world:
each of the 213 scenes found in the sound stream table (an unnamed WAD entry listing `speeches/<folder>/<scene>`) sits
in the folder of its prefix (`l3_c1` in `l3`).
So the prefix is a good guess (94 % on the scenes we can check) but wrong for a mission's clubhouse scenes, and the
663 scenes known only by it are marked speculative. A scene's position does not settle it either: the levels' worlds
overlap in coordinates, and placing each script-known scene at the level of its nearest script-known neighbour is
right for only 196 of 321.

The **Plays in** and **Known from** columns of [Scenes and movies](../references/scenes.md) hold this for every scene.

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
   are out ([HUD](hud.md#who-shows-the-hud-again)); when a player was bound (or player 1's
   brain `+0x2e4` is set), player 1's command menu unlocked, **follow** dispatched to his gang, forced and silent, and
   one gang update run ([The player's gang](#gang)).
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

### The player's gang {#gang}

What a cinematic (task `+0xed`) does to player 1's gang, the Warriors, in order. Only player 1's gang is touched
(game state `0x0051489c + 0x228`); a fixed scene or an animation does none of the engine steps below, only the
[reset at the join](#join-reset) of the humans it binds. Confirmed (code) at the addresses given; not checked at
runtime.

**Before the scene** (the scripts, [`SuperRunScene`](#superrunscene), read as bytecode): unless `NoClearWanted`, the
Warriors' wanted state is cleared (`GangClearWanted(GangWarriors)`: police neutral to them again, the wanted HUD
reset) and every responding gang dismissed (`GangClearResponders()`: non-police responders deleted, police stood
down, [bindings](../references/bindings/gang.md#gangclearresponders)); each bound human of gang type 0 is un-arrested
and revived. Then each bound Warrior is [reset as it joins](#join-reset): his grab, tackle or mug ends, and
**every player's rage ends**, bound or not.

**At the start** (`SceneTask_Start`, `0x0039d870`, step 0 → 1 of the global scene state, once per cinematic):
`Gang_ClearBums` (`0x0016b8d0`) walks the 16 member slots of player 1's gang (his brain `+0x20c`, gang `+0x48`) and
**destroys every member whose class byte `+0x11b` is 6** (`Human_Destroy(member, 0)`): the bums the player has
taken into his crew. A class-7 bum and every other member stay. Nothing else in the start touches the gang:
members that are not bound keep their brains and their tactic (the follow tactic, usually) through the scene unless
the scene freezes ([Starting](#starting) step 9; `SuperRunScene`'s scenes do not), and they are neither moved nor
hidden. The same `Gang_ClearBums` is the `GangClearBums` binding.

**At the end** (`SceneTask_End`, `0x0039f450`, step 5 of [Ending](#ending)), after the camera is popped and the
letterbox goes out, and before the end function is called:

1. Each bound Warrior's join goal is popped (step 2), which gives a player his pad back and an AI Warrior his brain.
2. Only when one of the scene's bound humans was a player (human `+0x1b0` not −1), or else player 1's brain `+0x2e4`
   is set (a Warrior hitting back at his chief, [AI](ai-gangs.md#warrior-follow)): that player's command menu is unlocked
   (game state `+0x42e` + player = 0), and **command 0, follow, is dispatched forced** for player 1
   (`WarriorCommand_Dispatch(state, player 1, 0, 1, 0, 1)` at `0x0039ff5c`). A forced dispatch is silent, still
   refused while commands are locked or follow is disabled, and otherwise clears the gang's tactic and shared target,
   records follow as the last command and builds a new follow tactic ([AI: commands](ai-gangs.md#warrior-follow)).
3. In the same case, one **gang update** is run at once (`0x00166708(gang, 0)` at `0x0039ff70`; Ghidra calls it
   `Gang_CachePlayerInfo`): with 0 it skips the spawner step, refreshes the cached player position, runs the new
   tactic once (`Tactic_Process`, which starts the follow tactic and hands out its goals) and the wanted level's
   update ([AI: the gang update](ai-gangs.md#gang-update)). It is not an "alert clear" as this page used to say.
4. With `freeze`, every brain is resumed (step 6).

**A cinematic given up** (`SceneTask_Abort`, `0x0039ec60`, after 10 s without starting) does less: fade in at once on
every view, then follow dispatched forced for player 1 (`0x0039f3f0`) without the menu unlock, the player test or
the gang update, and the HUD shown again.

So after any cinematic in which the player took part, the crew is back under a fresh follow command with no target,
whatever the command was before (attack, hold, scatter): a scene resets the player's last command. The wanted state
is cleared before the scene (by the scripts) and not restored.

### Timing {#timing}

- Scene time is in 1/30 s frames; the task and its runners advance on the 30-a-second update, the roles' clips on the
  animation clock ([Animation](formats/animation.md#playing-a-clip)). Confirmed (code).
- While the scene camera is current the game clock follows real time without the 40 ms clamp
  ([Boot](boot.md#timers)), so a slow frame is not stretched out. A fixed-step Coney plays the same frames.
- `SceneLength` reports the first part's duration only (`0x003540b8`), not `+0x84`. Confirmed (code).

### The scene test (`SCENETEST`, `PlaySceneTest`) {#scene-test}

The level scripts carry the original developers' scene viewer: set the Lua global `SCENETEST` to a scene's **name**
and load a level, and instead of its mission the level spawns that scene's cast and plays the scene on a loop. It is
dead in the retail game: `global.lua`'s first statement sets `SCENETEST` to nil and no script or engine code sets it
again ([Debug](debug.md#debug-only-script-paths)). Everything here is confirmed (code), read as bytecode from the
disc's chunks (`global.lua`, the level scripts and the `*_scenetest` chunks), unless marked; names and counts only.

**The switch.** A level whose scripts have a scene test reads `SCENETEST` where it would start its mission
(`level99`'s `RunLevel`, most others' `RunMission` or `Main` call). When it is set:

- **Thirteen levels load a separate chunk**: `level2`, `level5`, `level9`, `level11`, `level20`, `level31`,
  `level34`, `level51`, `level54`, `level83`, `level84`, `level87` and `level99`. The level makes its Warriors (in
  `level99`, `AddWarriors()`, Rembrandt as `player` and Ash as `player2`; `level31` picks one of two Warrior sets by
  the scene name), then `preLoadFile("<level>_scenetest", <entry>)` and `AddCameras()`, and skips everything else
  of its start: no checkpoint chapter, no start-game callback (`level99`'s `StartAmbient`), no mission. The entry
  the chunk defines and the level names is `PlaySceneTest`, or `PlayScene` in `level9`, `level54`, `level83` and
  `level87`, or `PLAYSceneTest` in `level20`.
- **Ten levels define the test in the level script itself** and call it instead of their mission: `level3`,
  `level14`, `level52`, `level55`, `level80`, `level81`, `level82`, `level86`, `level92` and `level93`. A
  `level86_scenetest` chunk is on the disc too, with the same five names, but nothing loads it.
- **The hub, `level95`**, has `LoadSceneTest` instead: for 19 names (the scenes played in the clubhouse, such as
  `l34_c5_a` or `l51_c11`) it sets the hub chapter that holds the scene as the checkpoint, loads that chapter, and
  its set-up then runs the chapter's own cutscene action whose `SceneId` is `SCENETEST` (`story.RunMissionAction`,
  after `ScreenQueueEffect(0, 2)`), or, with `CfgSceneTables`, schedules `DoQuickMap` instead. So the hub plays the
  scene through its real mission action, once, not looped.

**The dispatcher.** `PlaySceneTest` (or its other name) compares `SCENETEST` with each scene name it knows, in turn,
and calls that scene's play function; a value that matches none (`SCENETEST = 1`, say) plays nothing and leaves the
level without a mission. Across the 24 levels there are **196 scene names**, every one a scene of `scene_list.cnk`
(checked against the [scene list](../references/scenes.md)). Two are unreachable: `level20`'s list has `"l20_c6 "`
with a trailing space, and `level5_scenetest` defines `Playl5t9010` but its dispatcher never calls it.

**A play function** sets up the scene the way the story does, from the level's own spawners: it calls the same
`Add…` functions the chapters use for the scene's cast (`level99`'s `PlayL99C1` calls `AddCombatTeacher` and
`AddCombatWarriors`), sometimes spawns or enables objects (`ObjSpawn`, `ObjEnableZone`), builds the
[`SuperRunScene`](#superrunscene) table with the story's `SceneId`, `Humans` and `Objects` in the story's role order
(`level99`'s `l99_c1` table matches `IntroScene`'s), and calls `SuperRunScene`. What differs from the story:

- **`Looping` = true** (1 in `level9`, `level54`, `level83` and `level87`) in all but a few: the scene plays again and
  again ([Objects, camera and lights](#camera): a looping scene restarts its pass, the role clips looping with it).
  `level86`'s five do not loop. A few in-level tests keep a story return function while looping: `level80`'s
  `RunIntroScene` for `l80_c1` (its `EndIntroScene` with three call-backs, [Skipping](#skipping)) and `level93`'s
  `RunFinalScene` for `l93_c3` (`EndFinalScene`).
- **Otherwise the end function does nothing**: the chunks' `EndScene(id)` and `SceneFinished()` are empty, and many
  tables set no `ReturnFunc` at all (`PreCashTheWorld` skips a missing one). Nothing follows the scene.
- `Preloaded` is false (`level99`, `level34`, `level52`): `SuperRunScene` preloads the scene itself.

**What `global.lua` does differently while `SCENETEST` is set**: `gPlayCutScene` calls `CfgSubtitles(SCENESUBTITLES)`
before every play, and `SCENESUBTITLES` is 1, so **subtitles are forced on**. `global.lua` also sets `SCENELANGUAGE`
= 0 and calls `CfgLanguage(SCENELANGUAGE)` when `SCENETEST` is set, but only at its own load, right after setting
`SCENETEST` to nil, so that call never runs.

**How it ends.** A looping scene with a loop point (event 29) plays to the end of its pass when skipped; one without
ends at once ([Skipping](#skipping), [Ending](#ending)); either way the empty end function runs and the level then
sits with no mission (inferred: no script is left to run but the level's ambient set-up).

**For a debug entry** (inferred): the developers' review path is "load the level with `SCENETEST` = the scene's
name". A faithful debug entry sets the global after `global.lua` has run and before the level's start function, as
[Debug](debug.md#pcsx2-recipes) describes, and lets the level's own test path spawn the cast and play the scene
with the story's bindings. It needs the level script and, for the thirteen chunk levels, `preLoadFile` of the
`*_scenetest` chunk; the scene then loops until skipped.

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
  settle the feet on the ground) and the 21/22 marks, which the data shows hold `f32` positions and headings. Event
  11 is the human's animation sound, as in play ([Sound](sound.md#anim-sounds)): footsteps, shuffles and cloth during
  the scene. Clip events other than 11, 13, 21 and 22 are not acted on.
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
`--scene NAME` with `--play-level` plays one scene at once ([Running Coney](../guides/running.md#playing-a-level)).

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
- How a role's model is decided (answered: by the bound human's character type; the name is a label, [Cast and
  props](#cast), confirmed at runtime for `l99_c1`, [Cast at runtime](#cast-runtime)). Still open: the 204 roles
  and 41 props of scripted scenes the static walk does not resolve, and which of two or more `HuCreate` types a
  level's checkpoint picks in the other either-or roles (the `scene_cast_state` scenario reads them from a state
  saved before each scene starts; only `level99` and `level80` states exist). What the object bound to the subway
  scenes' `train_sound` slot is (no world-object type).
- Which level a scene plays in (answered for the 333 scenes a script, a Subtitles chunk or a movie places,
  [Which level](#which-level)). Open: the 663 known only by their prefix and the 244 with no level; whether the
  never-named `l<N>_` families are reached by a computed name (answered: no, [Which level](#which-level)).
