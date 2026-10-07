# Animation system: code

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims of its own;
runtime figures are on the pages linked.

## Purpose

The function-by-function map of the `Animation/` code (`0x00100200`-`0x0010d758`): the clip queries, the cursor, the
pose sampler and blender, the animation task types and their methods, the free lists, the dynamic animation slots, and
the ambient-sound manager that the linker put at the end of the same stretch. What the data looks like and how a clip
plays, blends and moves a human is on [Animation (character clips)](formats/animation.md); this page says which
function does what, so each one can be found and checked. Names are ours.

## Original structure

| Range | Source file | Evidence |
| --- | --- | --- |
| `0x00100200`-`0x00104630` | the unnamed stretch before `Animation.cpp` (reference pose, skeleton, clip queries, events, cursor) | inferred ([Source map](source-map.md#position)) |
| `0x00104630`-`0x001048a0` | `Animation/Animation.cpp` (anchor `0x00104818`, `AnimationSystem`) | confirmed (code) for the anchor |
| `0x001048a0`-`0x0010b9f0` | `Animation/AnimationBlend.cpp` (anchor `0x001048a0`, `FreeList<AnimTask>`; static-init stub `0x0010b9d0`) | confirmed (code) for the anchor |
| `0x0010b9f0`-`0x0010d758` | `Animation/AnimationMgr.cpp` (anchor `0x0010b9f0`, `FreeList<WarAnimInstance>`; stub `0x0010d738`): the cursor list, the dynamic animation slots and the ambient-sound manager | confirmed (code) for the anchor; the ambient code's file is inferred from the stub |

Most of the task methods are reached only through vtables; the [task types](#task-types) table maps each vtable slot
to its function.

## Clip queries and events {#clip-queries}

A **clip** here is its 80-byte descriptor ([Descriptor](formats/animation.md#descriptor-chunk-0x02-80-bytes)). Most
queries take a human and an anim id and find the clip through the human's character (`0x00221bf0`:
`CharacterInstance_GetAnim(human +0xd8, id, 1)`, the form that does not fire the [anim
callbacks](characters.md#anim-callbacks)); they return 0 (or 0.0) when the human has no character or the id no clip.
Event records are 24 bytes ([Keyframes](formats/animation.md#keyframes-chunk-0x00)); besides the documented fields,
`+0x04` is an s16 (or u32) **value** that several types use. Confirmed (code) at each address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00100200` | `Pose_InitReference` | writes the [reference pose](formats/animation.md#reference-pose) | confirmed (code) |
| `0x00100a80` | `Skeleton_InitBoneOffsets` | writes 34 `(x, y, z, 1)` constants into a table: the default bone offsets (bone 0 and 1 zero, bone 1's `z` −1.077, then the spine, arms, fingers and legs), the same layout as the character's offset table; only caller `0x00174d00` (a character instance's default set-up) | confirmed (code); its use as the fallback offsets inferred |
| `0x00101120` | `Skeleton_InitParents` | fills the parent table | confirmed (code) |
| `0x00101250` | `Clip_GetEvents` | the clip's event array (`keyframes + A + B + C` sizes), or 0 when it has none | confirmed (code) |
| `0x00101288` | `Anim_SendEventMessage1` | from the event code (`0x00101dd8`, two calls): sends task message 1 to an object with the human twice, 0, 1 and two vectors read from the object's slot `+0xa8` (inferred: a position) | confirmed (code); what message 1 does is not traced |
| `0x00101360` | `Anim_SendPickupPlacement` | the **first type-9 event** of a human's clip: decodes its position and rotation and sends message `0x1b` (with the event's `+0x06`, and whether the target's `+0x110` is set) to a task: the scripted [placement in hand](objects.md#held) | confirmed (code) |
| `0x00101558` | `Anim_GetPartnerOffset` | the first **type-8 event**'s position `(x / 1023, y / 1023, z / 2047, 1)`: the partner's place ([Paired tasks](formats/animation.md#paired-tasks)) | confirmed (code) |
| `0x00101658` | `Anim_TimeToFirstEvent` | the time of the first event of types `0xf`, `0x13`, 9, `0x36`, `0x34`, `0x41` or `0x2c`, else the duration, divided by the rate ([Combat](combat.md)) | confirmed (code) |
| `0x001017a0` | `Anim_ChainWindowMidpoint` | `(t(0x2c) + t(0x2d)) / (2 × rate)`, with `t(e)` = (frame − 1) / 30 of the last type-`0x2c` event and of the first `0x2d` after it; with no `0x2d` the clip's duration / rate stands in for it. Used by the AI's attack chaining (`AttackKind_ChainDelay`, [AI](ai.md)) | confirmed (code) |
| `0x001018b8` | `Anim_GetEvent2aValue` | the value (`+0x04`) of the first type-`0x2a` event; 0 without one, 0.01 without a clip; read by the stun code `0x0026a6d0` | confirmed (code); meaning not traced |
| `0x00101950` | `Anim_RootDisplacementLength` | length of the descriptor's displacement | confirmed (code) |
| `0x001019a0` | `Anim_GetRootDisplacement` | the descriptor's displacement as `(dx, dy, 0, 1)`; used by the climb code (`Climb_Start`, `0x00281450`, `0x00281838`) | confirmed (code) |
| `0x00101a00` | `Anim_GetPlayTime` | duration / [playback rate](formats/animation.md#playback-rate) | confirmed (code) |
| `0x00101a60` | `Anim_GetKnockdownValue` | the value of the first **type-7 event**; true when there is one ([knockdowns](combat.md)) | confirmed (code) |
| `0x00101af8`, `0x00101b28`, `0x00101b58`, `0x00101b88`, `0x00101bc0`, `0x00101bf0`, `0x00101c20` | `Anim_IsPairedById`, `Anim_StartsTackleById`, `Anim_StartsGrabById`, `Anim_HitsWithCapsule`, `Anim_HasFlag80`, `Anim_HasFlag200`, `Anim_HasFlag100` | one bit each of the descriptor's [flags](#clip-flags) | confirmed (code) |
| `0x00101c58` | `Clip_GetSoundtrackHash` | the `+0x08` word of a clip's first type-`0xd` event, else 0: the [scene soundtrack](sound.md#scene-sound) | confirmed (code) |
| `0x00101ce0` | `Clip_GetEventValue(type)` | the value of a clip's first event of a type, or -1; used by the scene code (`0x0039d870`) | confirmed (code) |
| `0x00101d68` | `Clip_FindEvent(type)` | a clip's first event of a type, or 0; used by the scene code | confirmed (code) |
| `0x00101dd8` | `Anim_FireFrameEvents` | the events a cursor passes ([Paired tasks](formats/animation.md#paired-tasks)) | confirmed (code) |
| `0x00103e90` | `Cursor_FireEffectEvents` | walks a cursor's pending events (`+0x120` the next, `+0x124` its index, `+0x126` the count) unless muted (`0x10`): a type-9 event sends message `0x89` (rotation, position, value `+0x06`), a type-10 event message `0x8a` (position) to the instance's human; called by the scene player (`0x003a0a68`) | confirmed (code) |

### Clip flags (descriptor `+0x44`) {#clip-flags}

A u32 after the reference count. Confirmed (code) at the readers; meanings from their callers:

| Bit | Reader | Meaning |
| --- | --- | --- |
| `0x1` | `0x00101af8` | a paired clip |
| `0x4` | `0x00175120` | has strike events (type `0xf` or `0x13`): releasing the clip turns every strike shape off ([Combat](combat.md#moving-strikes)); 222 of the disc's 1,752 clips |
| `0x20` | `0x00101b28` | starts a tackle |
| `0x40` | `0x00101b58` | starts a grab |
| `0x80` | `0x00101bc0` | read by `0x00228488` (not traced) |
| `0x100` | `0x00101c20` | read by `0x0022f4f8` (a reaction; not traced) |
| `0x200` | `0x00101bf0` | read by the stun and reaction code (`Human_Stun`, `0x0022f4f8`, `0x0022f9e0`) |
| `0x10000` | `0x00101b88` | the strike is tested with the attacker's capsule ([Combat](combat.md)) |

## The cursor and the pose {#cursor}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00104110`, `0x001041f8`, `0x001044a0` | `AnimCursor_Step`, `_Init`, `_Advance` | [Playing a clip](formats/animation.md#playing-a-clip) | confirmed (code) |
| `0x00104410` | `AnimCursor_SetClip` | sets the clip (`+0x04`) and re-inits at the start frame `+0x20` | confirmed (code) |
| `0x00104478` | `AnimCursor_SetInstance` | sets `+0x08` | confirmed (code) |
| `0x00104480`, `0x00104490` | `AnimCursor_NoRootVelocity`, `_NoRootTurn` | set cursor flag `0x1` / `0x2` | confirmed (code) |
| `0x00104570` | `AnimCursor_Restart` | re-inits on a clip (the loop and chain step) | confirmed (code) |
| `0x00104590` | `AnimCursor_Bind(clip, instance, task)` | start frame 0, owner task `+0x0c`, instance, clip: every task constructor calls it | confirmed (code) |
| `0x00104ac8` | `Key_LerpRotation` | decodes two rotation keys (`× 2⁻¹⁵`, `w` rebuilt) and nlerps them at `(frame − start) / delta` | confirmed (code) |
| `0x00104bc8` | `Key_LerpPosition` | the same for two position keys (`/1023`, `/1023`, `/2047`), lerp | confirmed (code) |
| `0x00104ce0` | `AnimCursor_SamplePose` | the [sampler](formats/animation.md#the-pose) | confirmed (code) |
| `0x00105158` | `Pose_BlendPartial` | the [blend](formats/animation.md#blending) | confirmed (code) |
| `0x001054a8` | `Pose_BlendTop` | `Pose_BlendPartial(weight, instance, 0)`: the two top poses, whole body | confirmed (code) |
| `0x00104630` | `Instance_BuildBoneMatrices` | [Bone transforms](formats/animation.md#bone-transforms) | confirmed (code) |
| `0x001047d0` | `Instance_CopyRootSlots` | copies eight 16-byte rows from `src + 0x80` to `dst + 0x60` (from `CharacterInstance_Sample`, `0x00176d60`) | confirmed (code); what the rows hold is not traced |
| `0x00104a38` | `Anim_RateMultiplier` | [Playback rate](formats/animation.md#playback-rate) | confirmed (code) |

## Systems and free lists {#systems}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00104818` | `AnimationSystem_Create` | sets the `Anim Data` chunk handler (type 2, `0x001045e0`), allocates the 0x88-byte `AnimationSystem` (its pointer at `0x00597200`) and fills it with `Skeleton_InitParents`: the object **is** the 34-entry parent table | confirmed (code) |
| `0x001045e0` | `AnimData_OnLoaded` | pops chunks `0x02` and `0x00`, clears the descriptor's reference count, sets its vtable `0x00534240` and keyframe pointer (`+0x1c`), pushes it ([Chunk system](chunk-system.md)) | confirmed (code) |
| `0x001048a0` | `AnimTaskList_Create` | the free list of 360 tasks of 0x48 bytes (`0x0050a898`), then the reference pose into `0x00598420` and `0x005fd040` | confirmed (code) |
| `0x001054c8` / `0x00105510` | `AnimTask_Take` / `AnimTask_Give` | take a task (clears `+0x10`, `+0x14`); give one back (calls its release `+0x94` first) | confirmed (code) |
| `0x00105640` | `AnimTask_DeferGive` | puts a task on the list given back after the update (`0x0050a8a8`; on a second list `0x0050a8ac` while that list is being emptied) | confirmed (code) |
| `0x00105570` | `AnimTask_FlushDeferred` | gives back every deferred task and refreshes its human's playing anim id (`0x001752c8`), then moves the second list over | confirmed (code) |
| `0x001083d0` | `AnimTask_SetRate(factor, task)` | rate = factor × the human's speed multiplier `+0x3a4` (the locomotion, workouts, AI) | confirmed (code) |
| `0x00108370` | `AnimTask_GetRate` | the task's rate `+0x0c`; the branch that multiplies by the human's `+0x3a4` is taken only when the instance has no human (and then reads through a null pointer), so in practice the rate alone | confirmed (code); "never taken" inferred |
| `0x0010b9f0` | `CursorList_Create` | the free list of 260 cursors (`WarAnimInstance`) of 300 bytes (`0x0050a8bc`) | confirmed (code) |
| `0x0010bb60` / `0x0010bbd0` | `Cursor_Take(owner, id)` / `Cursor_Give` | take a cursor (vtable `0x005351f8`, `+0x10` owner, `+0x14` anim id, flags cleared); give it back (its slot `+0x08` with 2 first) | confirmed (code) |
| `0x0010b9a8`, `0x0010b9d0` | `AnimationBlend_StaticInit`, its stub | sets `0x00597210` to -1 | not needed: compiler static initialiser |

## Task types {#task-types}

Every type shares the [common fields](formats/animation.md#animation-tasks). The vtables have 36 slots; the ones
below differ between types (the others are link-once defaults after `0x004da000`). Slot = the function word's offset:
`+0x14` the class name (a string, below), `+0x54` **update**: advance by dt and append itself to the stack again
(`TaskStack_Append`, `0x00175380`; `TaskQueue_Append`, `0x00175928`, for the face tasks) unless it has finished,
`+0x5c` sample, `+0x64` advance only, `+0x74` restart, `+0x7c` end (hand over at the end), `+0x84` cut off, `+0x94`
release, `+0xac` set target, `+0xbc` set value, `+0xcc` set task flags, `+0xdc` / `+0xe4` / `+0xec` test / set /
clear bits of the task's state flags. **State clean-up**, shared by the clip types' end, cut-off and release: resolve
the human (`+0x28`, or `+0x38` for types 4 and 5), clear bit `0x4000000000` of its record's 64-bit word `+0x00`
(`0x00226620`), clear the task's **state flags** from record `+0x08`; when they include `0x400000`, set bit 31 of
`+0x40` of the object the human's slot `+0xf0` returns; run the end callback; clear both. Confirmed (code) at each
address.

| Type | Vtable | Constructor | Update | Sample | Advance | Other slots | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 clip, looping | `0x005350d0` | `0x00105678` | `0x00105810` | `0x00105860` | `0x00105770` | restart `0x00105740`; release `0x00105898`; flags `0x001058f0` | confirmed (code) |
| 3 clip then next | `0x00534fa8` | `0x00105990` | `0x00105af0` | `0x00105ba0` | `0x00105ab0` | end `0x00105e90`; cut-off `0x00105d70`; set clip `0x00105d08` (`+0x8c`); release `0x00105f78`; flags `0x00106098`; state bits `0x00106108`, `0x00106118`, `0x00106128` | confirmed (code) |
| 4 paired layer | `0x00534668` | `0x00108450` | `0x001086a8` | `0x00108800` | `0x00108570` | release `0x001088c0`; flags `0x001089d0`; state bits `0x00108a40`, `0x00108a50`, `0x00108a60` | confirmed (code) |
| 5 layer | `0x00534e80` | `0x00106140`, `0x00106240` | `0x00106498` | `0x001065f0` | `0x00106360` | cut-off `0x001066c0`; release `0x001067a8`; flags `0x00106918`; state bits `0x00106988`, `0x00106998`, `0x001069a8` | confirmed (code) |
| 6 paired clip then next | `0x00534540` | `0x00108a78` | `0x00108bd0` | `0x00108c80` | `0x00108ba8` | end `0x00108f10`; cut-off `0x00108dd0`; release `0x00109018`; flags `0x00109168`; state bits `0x001091d8`, `0x001091e8`, `0x001091f8` | confirmed (code) |
| 7 scene clip | `0x00534d58` | `0x001069c0` | `0x00106a80` | `0x00106b60` | default | release `0x00106b90`; flags `0x001083e8` | confirmed (code) |
| 9 fade | `0x00534c30` | `0x00106c80` | `0x00106cc8` | `0x00106e50` | default | release `0x00106ee8`; flags `0x001083e8`; state bits `0x00106e18`, `0x00106e28`, `0x00106e38` | confirmed (code) |
| 10 two-clip mix | `0x005349e0` | `0x0010b090` | `0x0010b3f8` | `0x0010b448` | `0x0010b2e8` | weight step `0x0010b1c8`; set target `0x0010b290`, set value `0x0010b298`; time `0x0010b2c8`, duration `0x0010b2d8` (clip A's); release `0x0010b538`; flags `0x0010b5a0` | confirmed (code) |
| 11 four-clip mix | `0x00534b08` | `0x00106f50` | `0x00107f10` | `0x00107f60` | `0x00107458` | restart `0x001072a8`; set target `0x00107320`, set value `0x00107328`; time `0x00107338`, duration `0x001073c8` (of the clip nearest the value); release `0x00108200`; flags `0x001082a0` | confirmed (code) |
| 12 gait blend | `0x00534790` | `0x0010a310` | `0x0010ada8` | `0x0010adf8` | `0x0010a5b8` | restart `0x0010a4b0`; set target `0x0010a500`, value `0x0010a558`; release `0x0010af58`; flags `0x0010afc0` | confirmed (code) |
| 13 eight-direction | `0x005348b8` | `0x00109210` | `0x00109fb8` | `0x0010a008` | `0x00109588` | restart `0x001093f8`; set target `0x00109460`, value `0x001094f0`; release `0x0010a168`; flags `0x0010a1f0` | confirmed (code) |
| 15 face clip | `0x005342f0` | `0x0010b608` | `0x0010b6e0` | type 1's | type 1's | type 1's other slots | confirmed (code) |
| 16 face fade | `0x00534418` | `0x0010b7c8` | `0x0010b858` | type 9's | default | type 9's other slots | confirmed (code) |

**Class names** (slot `+0x14`): type 1 `Anim Task Looping` (`0x001058d8`), 3 `Anim Task OneOffChained`
(`0x00106088`), 4 `Anim Task PairedOneOffOverlay` (`0x001089c0`), 5 `Anim Task OneOffOverlay` (`0x00106908`), 6 `Anim
Task PairedOneOffChained` (`0x00109158`), 7 `Anim Task SceneAnim` (`0x00106bf8`), 9 `Anim Task Blend Out`
(`0x00106f40`), 10 `Anim Task Blend Two Loop` (`0x0010b590`), 11 `Anim Task Blend Two Of Four Loop` (`0x00108290`),
12 `Anim Task Blend Two Of Five Loop` (`0x0010afb0`), 13 `Anim Task Blend Two Of Eight Loop` (`0x0010a1e0`), 15 **`Face
Task Looping`** (`0x0010b7b8`), 16 `Anim Task Blend Out` (`0x0010b998`). Confirmed (code).

Shared small methods: the "set task flags" slots copy the flags to `+0x10` and set cursor flags `0x1`, `0x2`
and `0x10` from them on every cursor the type owns (`0x001083e8`, for types 7, 9 and 16, skips them for a fade);
`0x00106c38` / `0x00106c50` set / clear cursor flags on a task's first cursor (the scene code, and type 7 itself);
`0x00106c08` pushes a type-7 task's next task and clears it (the scene player); `0x001072a8`, `0x001093f8`,
`0x0010a4b0`, `0x00105740` restart every cursor of their type at frame 0. Confirmed (code).

### Clip types 1, 3, 5, 15 {#clip-types}

- **Type 1** (`0x00105678(task, instance, id, owner, callback)`): `+0x24` handle, `+0x28` callback. Its advance wraps
  the clip and runs the callback with the resolved human each time it wraps.
- **Type 3** (`0x00105990(blend, task, instance, id, next, stateFlags, owner, callback)`), as on the format page:
  `+0x1c` next, `+0x20` blend time = min(blend, next's duration). The end (`0x00105af0`) defers its own give-back and
  puts the next task on the stack, advanced by the overshoot when the blend time is not 0.
- **Type 5, the layer**: a clip blended **over** the pose below with a weight, from a bone down. Fields: `+0x20` bone
  (0 = the whole body), `+0x24` fade-in, `+0x28` fade-out, `+0x2c` the phase's time, `+0x30` phase (0 fading in,
  1 playing, 2 fading out), `+0x34` state flags, `+0x38` handle, `+0x3c` callback, `+0x1c` the top task's slot
  `+0x9c` value + 1, refreshed each advance (inferred: how many stack entries the layer covers).
  The weight is the share of the fade-in, 1, then one minus the share of the fade-out (`0x001065f0`). `0x00106140`
  makes one with no fades (phase 1); `0x00106240(fadeIn, fadeOut, task, instance, id, bone, stateFlags, handle,
  callback)` with fades, starting in phase 1 when **fadeOut** is 0 (as compiled: a zero fade-out also skips the
  fade-in). When the fade-out ends the task is deferred for give-back; on release with no fade-out, a looping base
  task (types 1, 11, 12, 13) is restarted (its slot `+0x74`).
- **Type 4** is type 5 with the clip and its rate from another human's character (handle as its third argument), the
  way [paired tasks](formats/animation.md#paired-tasks) work.
- **Type 15, the face clip** (`Face Task Looping`): `0x0010b608(task, instance, firstId, lastId, handle, callback)`,
  task flag `0x20`; each time the clip ends it starts a random id from `firstId` to `lastId` (`0x003353f0`, seed
  `0x006eb870`) and runs the callback. Face tasks live on the instance's **queue** (`+0x304`, `TaskQueue_Append`), not
  on the body's stack; their fade is type 16 (as type 9, but it cleans up past 4 tasks instead of 12). So the queued
  tasks that `CharacterInstance_Sample` samples first into the default pose
  ([The pose](formats/animation.md#the-pose)) are the face's (inferred from the class name).

### Type 7, the scene clip {#scene-clip}

`0x001069c0(task, instance, clip, next, handle, callback)`, made by the scene code (`0x0039d870`) with a clip
descriptor rather than an id, rate 1.0. At the clip's end (or at once when cursor flag `0x8000` is set) it calls the
callback `(human, task, &handle)`: a non-zero result restarts the clip and clears `0x8000`; zero defers the task's
give-back and puts the next task (`+0x20`) on the stack. The release calls the callback with `(human, 0, 0)`.
Confirmed (code); [Scenes](scenes.md).

### The mixers: types 10, 11, 13 {#mixers}

All three keep a current value that moves toward a target each advance, lead with one cursor, mute the others'
events, and wrap together. Confirmed (code):

| | Type 10 | Type 11 | Type 13 |
| --- | --- | --- | --- |
| Constructor | `(value, task, instance, idA, idB)` | `(value, speed, task, instance, id0..id3, handle, callback)` | `(value, task, instance, baseId, handle, callback)` |
| Clips | 2 (`+0x18`, `+0x1c`) | 4 (`+0x18`-`+0x24`) | 3 of 8: `base + (floor(v) − 1, floor(v), floor(v) + 1) & 7` (`+0x18`-`+0x20`) |
| Value | `+0x24` → target `+0x20`, 0-1 | `+0x2c` → target `+0x28`, 0-3 | `+0x28` → target `+0x24`, 0-8 wrapping |
| Speed per second × rate | 2.0 (`0x0050a8a4`) | `+0x30`, × 2 when more than 1 away, × 8 when more than 2 | 4.0, the short way round; no move when more than 3 away (then, in move state 3, `0x002266a8(human, 2)`) |
| Leader (events on) | B when value ≥ 0.51 | the clip nearest the value (0.51, 1.51, 2.51) | the middle clip |
| Sample | A below 0.01, B above 0.99, else blend by the value | one clip within 0.005 of an integer (0.01 at 0), else the two neighbours by the fraction | middle below 0.01, upper above 0.995, else the two by the fraction |

Type 13's set value (`0x001094f0`) rounds to the nearest of 8 unless task flag `0x200`, like the gait blend
(`0x0010a558`, which clamps to 0-4). `0x0010a2a0` is the gait blend's pair index: 0, 1, 2 or 3 at the 0.995, 1.995,
2.995 steps. When a type 11 or 13 crosses to another clip the incoming one starts at the same normalised time.
Confirmed (code) at the addresses cited.

## Dynamic animation slots {#dynamic-slots}

A 0x28-byte slot (seven per human at `+0x3c8`, [Characters](characters.md)): `+0x00` the clip's name (up to 31
characters), `+0x20` the loaded clip resource (`+0x00` the time it was last let go on the real-time clock, `+0x04` a
reference count, `+0x0c` the clip), `+0x24` the anim id it stands for. Confirmed (code):

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0010bc38` | `DynAnimSlot_Set(slot, name, id)` | lets the old clip go, copies the name and id; when the clip is already loaded (`0x0016f980`) takes it (`0x00178f40`, `0x0016f128`) and returns 1, else 0 (the [resource manager](characters.md) attaches it later); a null name empties the slot | confirmed (code) |
| `0x0010bcf8` | `DynAnimSlot_Release` | drops the clip's count, stamps the time, empties the slot | confirmed (code) |
| `0x0010bd70` / `0x0010bd80` / `0x0010bd90` | `DynAnimSlot_Attach` / `_GetClip` / `_GetRefCount` | set `+0x20`; its clip; its count (0 when empty) | confirmed (code) |

## The ambient-sound manager {#ambient}

The object behind `AddAmbientSound` and the emitters ([Sound](sound.md#ambience), [Sound
bindings](../references/bindings/sound.md#addambientsoundemitter2)): 512 emitters of 0xd0 bytes from `+0x10`, their
count at `+0x08`, the ambient table of name hashes at `+0x1a014`, and four timers at `+0x1b784`-`+0x1b794`.
Confirmed (code) at each address:

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0010bdb0` | `AmbientManager_Construct` | marks every emitter's sound handle (`+0x80`) empty, clears them (`0x0010d2f8`) and the timers | confirmed (code) |
| `0x0010bf40` | `AmbientManager_Reset` | the same without the handles (from `0x00110f58`) | confirmed (code) |
| `0x0010d2f8` | `AmbientManager_ClearEmitters` | zeroes all 512 emitters (default transforms) and the count | confirmed (code) |
| `0x0010d398` | `AmbientManager_ClearActive` | clears every emitter's `+0x8c` (enabled) word | confirmed (code) |
| `0x0010be40` | `AmbientManager_MarkEvent` | stamps the game clock (`*(0x0050b734) + 0x38`) into `+0x1b78c`, or 2 s before it when already set (from the AI, `0x002936a8`) | confirmed (code); what the event is not traced |
| `0x0010be78` | `AmbientManager_UpdateTimers` | `+0x1b788` = 1 from 2 s to 15 s after that stamp; `+0x1b790` = 1 while the game state's `+0x40c` is 1 and for 5 s after | confirmed (code) |
| `0x0010bf90` | `Ambient_GetVolume` | 0.75 while the game state's `+0x410` is set, else 1.0 | confirmed (code) |
| `0x0010c098` | `Ambient_GetDuck` | 0.5 when `0x0010def8` reports a sound and the state's `+0x40c` is not 2, else 1.0 | confirmed (code) |
| `0x0010bfc0`, `0x0010c020` | `AmbientSound_ApplyVolume`, `_ApplyDuck` | write those factors into a playing sound task (`+0xa8`, dirty `+0x60`) | confirmed (code) |
| `0x0010c100` | `AmbientManager_UpdateEmitters` | runs the emitters ([Sound](sound.md#ambience)) | confirmed (code) |
| `0x0010cc70` | `AmbientManager_FindEmitter` | an emitter by name | confirmed (code) |
| `0x0010cd68` | `AmbientManager_AddSphereEmitter` | a type-7 emitter: name, position, five values, a random interval between two limits (`0x003353f0`, seed `0x006eb8b0`), a byte 0-2 at `+0x90` (else 0; from `0x00110e28`) | confirmed (code); the binding not traced |
| `0x0010ced0` | `AmbientManager_AddParticleEmitter` | `AmbientManager_AddEmitter` with the type name `"particle task"` | confirmed (code) |
| `0x0010cf58` | `AmbientManager_AddEmitter` | the general emitter ([Sound bindings](../references/bindings/sound.md#addambientsoundemitter2)) | confirmed (code) |
| `0x0010d270` | `AmbientEmitter_SetEnabled` | sets or clears `+0x8c`; returns the new state | confirmed (code) |
| `0x0010d2a0` | `AmbientEmitter_SetVolume` | stores the volume and passes it to the playing sound | confirmed (code) |
| `0x0010d3c8` | `AmbientTable_Set(slot, name)` | CRC-32 of the name into the ambient table | confirmed (code) |
| `0x0010d420` | `AmbientEmitter_NextSound` | the table entry for the emitter's next slot, cycling through its `count` slots | confirmed (code) |
| `0x0010d480` | `AmbientEmitter_TryPlay` | an enabled emitter with nothing playing plays its next sound in 3D (`0x0010fdd0`) when the listener is within its radius (`+0x84`) | confirmed (code) |
| `0x0010d570` | `AmbientManager_EnableEmitter` | sets an emitter's `+0x8c` | confirmed (code) |
| `0x0010d590` | `AmbientEmitter_SetPoints` | copies up to `n` points into the first emitter whose name matches | confirmed (code) |
| `0x0010d668` | `Ambient_Nop` | returns at once (called from the update) | not needed: empty function |
| `0x0010d670`, `0x0010d738` | `AnimationMgr_StaticInit`, its stub | copies eleven colour constants from `0x005fd268`.. into `0x00598640`.. | not needed: compiler static initialiser |

## Open questions

- Task message 1 (`0x00101288`) and messages `0x89` / `0x8a` from effect events: the receivers' handling.
- Clip flags `0x80`, `0x100`, `0x200` and event types `0x2a`, `0x2c`, `0x2d`: what the moves that carry them do.
- Which ids the face tasks play, and who starts them.
