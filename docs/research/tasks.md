# Tasks (the TaskEngine and when a move hands control back)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-05); every claim is confirmed (code) at the cited address unless it says otherwise. The five runtime checks
were run in PCSX2 the same day with the trace harness and its call hooks ([Runtime checks](#runtime-checks)); their
results are in the body, marked confirmed (runtime).

## Purpose

What the game calls a *task*, how tasks are scheduled and updated, in what order a frame of play runs them, and the
one mechanism that decides when a human's move is over: the **flags an animation task holds on its human**. It is
the page an implementer needs to replace Coney's per-symptom timing guards with the original's machinery, and the
frame the [AI humans](ai.md) run in.

In one paragraph: the `TaskEngine` is the game's object scheduler. A singleton `TaskManager` owns two **timing
wheels** of 256 buckets (one per phase: play and pause), a set of sub-managers (objects, particles, lights, glass,
scenes, cars) and the clocks. Every world object is a *task*: an object with a position, a rotation, velocities, a
flags word and a vtable whose slot `+0x13c` is its update. The wheel runs one bucket per 60 Hz tick and re-inserts
each object at its update interval. The **characters** are not on the wheel but stepped by `Humans_Update`, which
the play tick calls on every 60 Hz tick and which works on every second one (30 Hz): pads, player records, gangs,
**brains**, animation, locomotion and then **actions** (the command dispatcher). A move does not decide its own
length: the clip's **animation task** holds bits of the record's `+0x08` flags while it plays, the clip's events
change them (wind-up, chain window, end, recovery), and the task clears them when the clip ends or is cut. The
dispatcher, the locomotion and the AI's attack action all read those bits. So **the clip's end hands back the
presses and the stick**, which is what was seen at runtime ([Combat](combat.md#input-return)).

Not to be confused with **animation tasks** (`AnimTask`, `Animation/AnimationBlend.cpp`), the small objects that play
clips on a character ([Animation](formats/animation.md#animation-tasks)); they are covered here only where they hold
flags on a human.

## Original structure

`TaskEngine/` (`0x00397a48`-`0x003a8698`, [Source map](source-map.md)): `ObjectTaskManager.cpp`, `SceneTask.cpp`
and `TaskManager.cpp`. The functions below `0x003a2728` that work on the base task object (`0x003a1570`-`0x003a2310`)
and those above it that work on the manager (`0x003a2b48`-`0x003a4288`) sit around `TaskManager.cpp`'s anchors;
which file each belongs to is inferred from the range only. Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x003a2728` | `TaskManager_Construct` | builds the singleton `0x00512c7c`: the two wheels and the sub-managers | confirmed (code) |
| `0x003a2a30` | `TaskManager_Destruct` | | confirmed (code) |
| `0x003a2b48` | `TaskManager_Reset` | flushes the seven per-manager lists, resets the clocks | confirmed (code) |
| `0x003a2c80` | `TaskManager_SetPhase` | selects the wheel and the per-phase manager for phase 0 (play) or 1 (pause) | confirmed (code) |
| `0x003a3148` | `TaskManager_Tick(mgr, phase)` | the frame's task step: phase 0 → `0x003a2ea0`, phase 1 → `0x003a3000` | confirmed (code) |
| `0x003a2ea0` | `TaskManager_TickGame` | play: up to two 60 Hz ticks of `Humans_Update`, the wheel bucket and the [physics step](physics.md#step) | confirmed (code) |
| `0x003a3000` | `TaskManager_TickPaused` | pause and menus: one tick on the UI clock | confirmed (code) |
| `0x003a31a8` | `TaskManager_UpdateManagers` | the sub-managers' updates for a phase | confirmed (code) |
| `0x003a32f0` / `0x003a3368` | `TaskManager_Schedule` / `TaskManager_Unschedule` | put an object on, or take it off, its phase's wheel | confirmed (code) |
| `0x003a33d8` | `TaskWheel_Init` | | confirmed (code) |
| `0x003a3478` / `0x003a34b0` / `0x003a3408` | `TaskWheel_Insert` / `_Remove` / `_ClearLink` | bucket list operations | confirmed (code) |
| `0x003a3588` | `TaskWheel_RunBucket` | updates the objects of the current bucket and re-inserts them | confirmed (code) |
| `0x003a1570` / `0x003a1580` / `0x003a15a0` | `TaskClock_SetPhase` / `_SetTime` / `_GetTime` | the per-phase clocks | confirmed (code) |
| `0x003a1630` | `Task_SetUpdateInterval` | | confirmed (code) |
| `0x003a1660` | `Task_Unschedule` | the base vtable `+0x4c` | confirmed (code) |
| `0x003a1f00` | `Task_GetVelocity` | zero when the object is static | confirmed (code) |
| `0x003a1f40` | `Task_AdjustIntervalByGround` | | confirmed (code), purpose inferred |
| `0x003a2310` | `Task_Integrate` | the default integrator (vtable `+0x144`) | confirmed (code) |
| `0x003a2e00` → `0x003a4288` | `Task_SendMessage` → `Task_DeliverMessage` | synchronous message to an object | confirmed (code) |
| `0x00390180` | `Handle_Resolve` | handle → object ([Handles](#handles)) | confirmed (code) |
| `0x0038ffd0` / `0x0038fe28` | `Handle_Assign` / `Handle_FindFree` | give an object a handle; the first free index from a start | confirmed (code) |
| `0x003a15c0` | `Task_Init` | the task head's defaults | confirmed (code) |
| `0x0039c698` / `0x00397fb8` | `TaskPool_Create` / `ObjectTaskManager_Create` | make a task of a named type ([Task classes](#classes)) | confirmed (code) |
| `0x00397a48` | `ObjectTaskManager_Construct` | | confirmed (code) |
| `0x00399d88` | `ObjectTaskManager_UpdateSpawns` | spawns and removes world objects near the players | confirmed (code) |
| `0x00249108` | `Humans_Update` | the characters' step ([below](#humans-update)) | confirmed (code) |
| `0x00254e78` | `Human_UpdateActions` | per human: the command dispatcher and the anim state choice | confirmed (code) |
| `0x00223cb0` | `Human_IsBusy` | the locomotion's "no stick" test ([below](#locomotion-gate)) | confirmed (code) |
| `0x0021d480` | `Human_OnEvent` | the human's event entry (vtable `+0x44`) | confirmed (code) |
| `0x00245920` | `Human_HandleMessage` | the human's message handler (vtable `+0xcc`) | confirmed (code) |

## Data

### The TaskManager {#task-manager}

One object, pointer at `0x00512c7c`. Confirmed (code) at the constructor `0x003a2728`:

| Offset | Meaning |
| --- | --- |
| `+0x08`, `+0x10` | per phase, the tick count of its last update |
| `+0x18 + n × 0x408` | wheel *n* (0 play, 1 pause): `+0x0` unused, `+0x4` the current bucket (0-255), `+0x8` 256 bucket heads |
| `+0x82c` | the current phase |
| `+0x830` / `+0x834` | the current wheel / the current per-phase manager |
| `+0x838`, `+0x83c` | two `ParticleTaskManager`s (1,400 and 100 particles), one per phase (`0x003a2cd8` picks them) |
| `+0x840` | the `ObjectTaskManager` (0x67ec bytes) |
| `+0x844` | the `CarTaskManager` |
| `+0x848` / `+0x84c` / `+0x850` | the `LightTaskManager`, `GlassTaskManager` and `SceneTaskManager` |
| `+0x854`… | seven lists, flushed by `TaskManager_Reset` |

The manager names are the allocation tags the constructor passes (strings `0x00581848`-`0x005818f0`); what each
manager holds is in [Task classes and pools](#classes).

### The base task object {#task-object}

Every scheduled object shares this head. Confirmed (code) at the accessors cited; the flag meanings are inferred from
their uses.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x10` | vec4 | position |
| `+0x20` | quat | rotation |
| `+0x30` | vec4 | velocity (`Task_GetVelocity` returns zero while the flags have `0x600`) |
| `+0x40` | vec4 | angular velocity |
| `+0x50` | u32 | the time of its last update |
| `+0x54` | u32 | flags: `0x200`, `0x400` static or moved by its velocity as position; `0x1000` rotation eased: each update makes `+0x40` (then a target rotation) the rotation, and between updates the pose is a slerp toward it over the interval ([World objects: leaves](objects.md#leaves)); `0x80000` a rotation mode; `0x2000` on a wheel; `0x2000000` grounded; `0x4000000` airborne (gravity 15.68 m/s² in `Task_Integrate`); `0x80000000` integrated |
| `+0x5c` | ptr | the next object in its bucket |
| `+0x60` | s16 | its phase |
| `+0x62` | s16 | its bucket |
| `+0x64` | float | update interval, seconds |
| `+0x6e` | u16 | update interval, ticks; `0x100` means "run once" |

Vtable slots used by the engine (function word offsets): `+0x14` before the update, `+0x24` the class flags
(`0x800` forces wheel 0), `+0x44` event, `+0x4c` unschedule, `+0x94` velocity, `+0xcc` message, `+0x13c` **update**
(returns 1 to stay scheduled), `+0x144` integrate. The human's vtable is `0x0053f088` (`+0x13c` = `0x0023fea8`, its
state update, `+0x44` = `Human_OnEvent`, `+0xcc` = `Human_HandleMessage`).

Every task class's initialiser starts with `Task_Init` (`0x003a15c0`): position and velocities zero, rotation
identity, phase 2, bucket −1, interval 255, flags 0, the values a human keeps because it is never scheduled
([The wheel](#wheel)). Transforms live in a separate table at `0x00714b00` (32 bytes each, indexed by the object's
`+0x92`).

### Handles {#handles}

Scripts and objects refer to things by **handle**, a 32-bit number: the low 16 bits a serial, the high 16 bits an
index into the handle table at `0x006ebd38` (2,816 = 0xb00 entries of `{pointer, u16 serial}`). Confirmed (code):

- **Assigning** (`Handle_Assign`, `0x0038ffd0`): store the pointer at the index, give it the next serial from the
  counter `0x006f1538` (which skips 0 when it wraps) and return `serial | index << 16`.
- **Resolving** (`Handle_Resolve`, `0x00390180`): −1 is nil; a serial that matches the entry's gives its pointer,
  any other serial gives 0, so a stale handle (the slot reused since) resolves to nothing. The global `NilHandle`
  scripts compare against is the word at `0x006ebd30`.
- **Serial 0** names a spawn record of the `ObjectTaskManager` (`0x00398fe0`, records of 0x28 bytes): the record's
  object is spawned first (`0x00399080`) when bit `0x20000` of record `+0x24` is clear (inferred: the "live" bit),
  then the low 16 bits of `+0x24` index the handle table and that object's own handle (its vtable `+0x2c`) is
  resolved; a record with `0x40000` set gives `NilHandle`. `ObjSpawn` hands out such handles
  ([Objects: spawning](objects.md#spawning)).

Each kind searches for a free index from its own start ([Task classes and pools](#classes) lists the kinds):

| Kind | Finder | First index searched | Evidence |
| --- | --- | --- | --- |
| humans | `0x0038fe88` | 0, and must be below 60; when none is, `Humans_CullCorpses` (`0x00232230`) runs once and the search is retried | confirmed (code) |
| cameras (`Cam_ICamera` base, `0x00120868`) | `0x0038fed8` | 60 | confirmed (code) |
| world flags (`0x00415e70`) | `0x0038fef8` | 108 | confirmed (code) |
| boxes (the base of volume, turf and player boxes, `0x004127c0`) | `0x0038ff18` | 620 | confirmed (code) |
| cars, glass panes, world objects, particles | `0x0038ff38` | a cursor (`0x00512bec`) that runs from where it last stopped to 2,815 and wraps to 748 | confirmed (code) |

The starts are only where a search begins: a kind that fills its range takes the next free entries above it
(`0x0038fe28` scans to the end of the table). Only the humans have a hard limit here.

### Task classes and pools {#classes}

Each class below starts with the [task head](#task-object) and lives in a fixed pool its manager allocates at boot
(a `FreeList`: one block of *n* records and a slot array). Making one, given a type name and the creation arguments
(position, rotation, a parent handle) pushed into the [message](#messages) scratch: the manager's allocator (its vtable
`+0x14`, which also writes the class's vtable), the class's initialiser (vtable `+0x134`, given the name), its
creation handler if it has one (vtable `+0x19c`, given the arguments), then `TaskManager_Schedule` and flag
`0x80000000`. Confirmed (code) at `0x0039c698` (particles and the other FreeList managers) and `0x00397fb8` (world
objects, which first call `0x003998f0` when the pool is full; that it frees a slot is inferred). Class names are ours;
the pool names are the managers' allocation tags.

| Class | Vtable | Initialiser | Pool (manager, allocator) | Records | Handle |
| --- | --- | --- | --- | --- | --- |
| human | `0x0053f088` | `Human_Init` `0x00218008` | the static array `0x00640c80` ([Characters](characters.md#the-human-object)) | 60 × 0x6d0 | yes |
| world object (props, weapons, pick-ups, doors) | `0x005453a0` | `0x003918b8` | `ObjectTaskManager`, `0x004f3e98` | 384 × 0x140 | yes |
| particle system | `0x00545660` | `0x0039aef0` | `ParticleTaskManager` per phase, `0x004f42b0` | 1,400 (play) and 100 (pause) × 0xf0 | yes |
| scene | `0x005458c8` | `0x0039ca48` | `SceneTaskManager`, `0x004f4610` | 12 × 0x100 | no: a scene id ([Scene bindings](../references/bindings/scene.md)) |
| car | `0x00544c08` (constructor `0x00387498`) | `0x00387bc8` | `CarTaskManager`, `0x004f2a70` | 18 × 0x1310 | yes |
| glass pane | `0x00544ed0` | `0x0038ec30` | `GlassTaskManager`, `0x004f2dd8` | 100 × 0x100 | yes |
| light task | `0x00545138` | `0x00390370` (interval 2) | `LightTaskManager`, `0x004f3158` | 28 × 0xb0 | no |

**Type bits.** Every task class answers its vtable `+0x24` with a constant: the bits the game's casts test and
[`GetRTTI`](../references/bindings/util.md#getrtti) returns (with bit `0x2`, common to all tasks, cleared). Confirmed
(code), each getter read:

| Class | Vtable `+0x24` | Bits | `GetRTTI` |
| --- | --- | --- | --- |
| human | `0x004ed800` | `0x62` | `0x60` (`0x40` human, tested by `0x00229868`; `0x20` not traced) |
| world object | `0x004f3b08` | `0x0a` | `0x08` (tested by `0x00395f38`) |
| particle system (and tags) | `0x004f40f0` | `0x12` | `0x10` (tested by `0x0039bba0` and `ProcessTag`) |
| car | `0x004f2868` | `0x100002` | `0x100000` |
| glass pane | `0x004f2c10` | `0x402` | `0x400` |
| scene | `0x004f4488` | `0x802` | `0x800` |
| light task | `0x004f2fb0` | `0x06` | `0x04` |
| world flag (`0x00545e68`) | `0x004f62d0` | `0x80` | `0x80` (tested by `0x00417a60`) |
| volume box (base `0x00545c48`) | `0x004f5b30` | `0x100` | `0x100` |
| camera (`Cam_ICamera`, and `0x00535a90`) | `0x004db3a0` | `0x200` | `0x200` |

So a script tells a car from anything else by `0x100000`, and a world object by `0x08`. The other camera classes
and the box subclasses were not each read.

Confirmed (code): each vtable is written by the allocator the manager's vtable points to (`0x005455b4`, `0x0054581c`,
`0x00545a84`, `0x00544dc4`, `0x0054508c`, `0x005452f4`), and each initialiser calls `Task_Init`. The four classes the
wheel was seen updating ([The wheel](#wheel)) are the world object, the particle system, the scene and the car. A
world object's kind is its `ObjectAttribs` entry (`+0x112`, the `CfgObj` type), not a subclass. The car initialiser
picks the car's model index from the name in a table of six (`0x00512ba8`: `car_osedan`, `car_coupe`, `car_wagon`,
`car_copcar`, `car_van`, `car_sullycar`) and, for `car_copcar`, makes a particle system as well ([Cars](cars.md)).
A particle system's behaviour is the code of its record in the script type table ([Particles](particles.md)). The 18
cars match the 18-body pool of [IPhysics](physics.md#iphysics) (inferred). The dynamic lights of `SetLight` belong to the
graphics light manager (`0x0017ef20`), not to this pool; what makes a light task is not traced.

Not on the wheel, with pools of their own:

| Kind | Where | Size | Handle |
| --- | --- | --- | --- |
| world flag | `WorldFlag` pool ([World flags](flags.md#pool)), vtable `0x00545e68` | `CfgSetDatabaseSizes` flags + 4, 0xf0 each | yes |
| volume, turf and player boxes | `FreeList<VolumeBox>`, `FreeListContainer<TurfBox>`, `FreeList<PlayerBox>`; base constructor `0x004127c0`, vtable `0x00545c48` | per level, `CfgSetDatabaseSizes` | yes |
| spawn record (a placed object, live or not) | `ObjectTaskRec`, `ObjectTaskManager +0x14` | per level, `CfgSetDatabaseSizes` objects + 500, 0x28 each | serial 0 ([Handles](#handles)) |
| camera | `Cam_ICamera` base `0x00120868`, vtable `0x00535510` ([Camera](camera.md)) | not traced | yes |
| object zone | a bit mask in the `ObjectTaskManager`; zone 0 on, 1-254 off at start (`0x00398348` from `0x00397a48`); the zones scripts use: [Object zones](../references/zones.md) | 255 | the zone number |
| brain, goal, action | [AI](ai.md#brain) | 60, 170, 100 | by human |
| gang | `0x005e6e30` ([AI](ai.md#gang-record)) | 32 × 0xb10 | the slot 0-31 |
| formation | `0x006ceaf0` ([AI](ai.md#formations)) | 41 × 0x280 | |
| path | `AddPath` | 32 | a userdata |
| physics bodies and shapes | [Physics](physics.md#iphysics) | | |

**Boxes.** `AddVolumeBox`'s kind picks the pool (`VolumeBox_Add`, `0x004125b8`): 0 a `VolumeBox`
(`FreeList<VolumeBox>` at `0x0051483c`, 0x1f0-byte records), 2 a `PlayerBox` (`FreeList<PlayerBox>` at `0x00514814`,
0x100 bytes), 3 a `TurfBox` (through the factory `FreeListContainer<TurfBox>` at `0x00514834`, vtable `0x00545d28`);
kind 1 returns `NilHandle`. `CfgSetDatabaseSizes`' third argument sizes them: its items 1, 3 and 4 the volume,
player and turf pools; item 2's sizer (`0x00413200`) does nothing. Confirmed (code) at `0x0041d628`, `0x00414f68`,
`0x00413218`, `0x00414d48`; what a player box does differently is not traced. The boxes the scripts add:
[Volume boxes](../references/boxes.md).

`FreeList<AnimTask>`, `FreeList<WarAnimInstance>`, `FreeList<SoundTask>` and `FreeList<ScriptObject>` are pools
too (their allocation tags); their sizes are not traced.

### Clocks and phases {#clocks}

The game clock is the EE bus counter at `0x0050b734`: 0x4b0000 ticks are 1/60 s, and seconds are ticks ×
3.390842e-09. The menus use their own counter at `0x0050b8b8`. `TaskClock` keeps the phase in `0x00512c60` and each
phase's time in seconds at `0x00512c58[phase]`. Phase 0 is play (mode 1's enter, `0x00158580`), phase 1 the pause and
menus (mode `0xd`, `0x00155b30`). Confirmed (code).

### Messages and events {#messages}

- `Task_SendMessage` (`0x003a2e00`) calls the target's vtable `+0xcc` **at once**, with the message in a 0x410-byte
  scratch at `0x006f1550 + depth × 0x410` (`DAT_00512c8c` is the nesting depth). There is no queue.
- An **event** (a struct with its type at `+0x20` and its sender at `+0x24`) goes to vtable `+0x44`. For a human
  (`Human_OnEvent`, `0x0021d480`) the order is: the script handlers on human `+0xe8` (`0x00384c38`), then the gang's
  (`0x00164c20`), then the brain's (`0x0028f928`, [AI](ai.md#events)).

Event types seen: `0xb` a new enemy (to the gang's tactic), `0xd` the brain's goals ran out, `0x14` violence nearby;
`0`, `1`, `7`, `0x10` and `0x17` reach the civilian brain (not identified).

## Behaviour

### The play tick {#tick}

`TaskManager_Tick(mgr, 0)` → `TaskManager_TickGame` (`0x003a2ea0`), once per frame. Confirmed (code):

1. Read the bus clock. **While at least `0x960000` ticks** (two 60 Hz ticks) have passed since the last step, run
   **two** 60 Hz ticks and take `0x960000` off; with less, run **none** this frame (the time carries over). So the
   ticks always come in pairs.
2. Each 60 Hz tick: set the phase's time, call **`Humans_Update`** (`0x00249108`), run the wheel's current bucket
   (`TaskWheel_RunBucket`), then the physics step (`0x00340918`), which only turns landed objects to rest and
   refreshes a broad-phase bound ([Physics](physics.md#step)).
3. After the ticks: the game state (`0x0041a370`) and the object spawns (`ObjectTaskManager_UpdateSpawns`, objects
   within 70 m of a player, 4900 = 70²).

At runtime (confirmed (runtime), street, no input, 1198 frames with hooks on `0x003a2ea0` and `0x00249108`): the
bus clock is the COP0 `Count` (`0x4b0000` per 60 Hz tick), `TaskManager_TickGame` runs once per frame at 29.97 Hz,
and each frame runs exactly **two** `Humans_Update` calls, the body on the first (the counter `0x005104f4` turns even).
Because a frame (two 59.94 Hz fields) is slightly longer than two ticks, the clock gains on the game: one frame in
about 1000 (every ~33 s) runs **four** ticks, so two character steps. No frame ran none.

A tick is not 1/60 s of real time: it is a burst of work that moves the world on by 1/60 s, and a pair runs back to
back on the one CPU thread before the frame is drawn. After a pair every system has advanced exactly 1/30 s: the
characters in one step (the first tick), the wheel and physics in two halves. The bus clock works like a bucket that
real time fills and each pair empties by `0x960000` (33.3 ms). Worked example (arithmetic from the rule above, not a
measurement): frames of 77 ms (13 fps) run 2, 2, 2, then 3 pairs as the remainder builds up (77, 87.3, 97.6, 107.9 ms
in the bucket), on average 2.3, so the world keeps real-time speed and only the pictures are fewer.

The same rule drawn out, at the PS2's normal 29.97 frames a second (each frame adds 33.37 ms to the bucket, each
pair takes 33.33 ms out; nothing runs in parallel):

```text
frame n        bucket 33.37 ms -> one pair -> 0.03 ms left
  tick A (60 Hz)   Humans_Update: body runs, characters move 1/30 s
                   wheel bucket k: objects move 1/60 s
                   physics: 1/60 s
  tick B (60 Hz)   Humans_Update: counter odd, body skipped
                   wheel bucket k+1: objects move 1/60 s
                   physics: 1/60 s
  game state, spawns, then the frame is drawn
frame n+1      bucket 33.40 ms -> one pair -> 0.07 ms left
...            after ~1000 frames the leftover reaches 33.33 ms: that frame runs two pairs
```

Whether `0x003a2ea0` caps the pairs of one frame is not traced ([Open questions](#open-questions)).

Inferred, from the two confirmed pieces: game time in a level is real time clamped to 40 ms a frame
([Boot: timers](boot.md#timers)): each frame adds the real time it took, measured on the CPU clock, but never more
than 40 ms, so at 25 fps or faster it is true seconds. The pairs catch up without that clamp. Below 25 fps, timers
in milliseconds of game time (an AI's attack delay, a block's length) would therefore fall behind the movement, which
counts steps.
On a PS2 that rarely dropped frames this did not show; Coney derives game time from the step count, so the two
cannot disagree ([Update and render](../guides/conventions.md#update-and-render)).

Phase 1 (`0x003a3000`) runs one tick on the UI clock and calls `Humans_Update` only when `0x005104f4` is 1, else just
the pads and the camera. `TaskManager_UpdateManagers` (`0x003a31a8`) runs the sub-managers of the phase: for play the
object manager (`0x003980c8`), the cars (`0x0038e590`), the lights (`0x00390e20`), the particles and the scenes.
Confirmed (code).

### The wheel {#wheel}

`TaskWheel_RunBucket` (`0x003a3588`), each tick, for each object in the current bucket: call vtable `+0x14`; unlink
it (`+0x5c`, `+0x62` and flag `0x2000` cleared); call its update (vtable `+0x13c`). If the update returns 1, the
object is not flagged `0x2000` again, and its interval `+0x6e` is not `0x100`, re-insert it in bucket
`(current + interval) & 0xff`. An object with both bits of `0x1000200000000000` (the u64 at `+0x50`) gets its
interval adjusted by a ground ray (`0x003a1f40`). Then the bucket index advances by one (mod 256). So an object with
interval 1 updates every tick (60 Hz), one with interval 6 ten times a second. `TaskManager_Schedule` sets `+0x60`
to the phase; an object whose class flags have `0x800` always goes on wheel 0.

**Humans are not on the wheel.** Confirmed (runtime): a hook on the wheel's update call logged 2428 updates over 120
ticks in the street, from four vtables only (`0x00545660` particles, `0x005453a0` world objects, `0x005458c8` scenes,
`0x00544c08` cars, [Task classes](#classes)), never the
human's `0x0053f088`. A human's task head has flags `+0x54` = `0x42005001` (no `0x2000`), phase `+0x60` = 2, bucket
`+0x62` = -1 and interval `+0x6e` = 255, and no wheel bucket of the save state lists one.

### Humans_Update: the characters' step {#humans-update}

Called on every 60 Hz tick; its body runs only when `0x005104f4` is even, so **the characters step at 30 Hz**. Each
step behind a debug switch that is on in play. Confirmed (code) at `0x00249108`; this corrects the order given
before on [Characters](characters.md#update):

1. The litter, ground fog and rain splashes (`0x00170c88`, `0x00171d38`, `0x00184568`,
   [Graphics](graphics.md#code-camera-effects)).
2. `Pads_Update` (`0x001454a8`); with `0x005e5354`, the **formations** (`0x00293c68`: 41 records of 0x280 at
   `0x006ceaf0`, each `0x002956d0`).
3. The 60 **per-player records** (`0x00146078`): pad to command. A record with no pad has its command `+0x20`
   cleared here (`0x00146000`).
4. With `0x005e5358`, the **gangs** (`0x0016d170`: 32 records of 0xb10 at `0x005e6e30`, a fifth of them per step;
   the gang's tactic through `0x00306630`).
5. With `0x005e535c`, the **brains** (`Brains_Update`, `0x00293b28`, [AI](ai.md#update)): each enabled brain thinks on
   one step in five (brain *i* when *i* mod 5 = (`0x005104f4` >> 1) mod 5, confirmed (runtime)) and updates every
   step. Brains write their human's **command** into its per-player record
   `+0x20`, as a pad would.
6. With `0x005e5360`, each human's animation step (`0x0023bd78`), then `0x00105570` (refresh the playing anim ids).
7. Per human: an effect for flag `0x4000`, the player gang checks.
8. With `0x005e5364`, each human's **state update**: vtable `+0x13c` (the locomotion for a pad-controlled human,
   [Characters](characters.md#locomotion)), unless `0x0023d790` says it is not ready.
9. With `0x005e5368`, each human's **actions**, the order alternating (0 → 59, then 59 → 0) between steps:
   `Human_UpdateActions` (`0x00254e78`) runs `Player_UpdateActions` (`0x0027c120`, the dispatcher,
   [Combat](combat.md#dispatch)) unless the state word has any of `0x1f80974000` or per-player `+0x1e` is set, then the
   anim state choice. A dead or knocked-out human takes another path (`0x00221108`, `0x00256f28`, `0x00265f70`).
10. The cameras (`0x0011e878`).

So an AI human's command is written in step 5 and taken by the same dispatcher as the player's in step 9 of the same
update; the player's pad command reaches the record in step 3. Confirmed (runtime) with a civilian fighting the player:
the attack action's start (the call at `0x002fac98`) writes the command through `0x00147ef0`, the dispatcher
(`Player_UpdateActions`, return addresses in `0x0027b5d0`-`0x0027c73c`) reads it through `0x00147ef8` in the same
character step, and the next step's record update (`0x00146000`, the AI has no pad) has cleared it. The command is
written once per attack, not again while the action waits ([AI](ai.md#attack-action)).

### Held flags: how a move ends {#held-flags}

The record's `+0x08` word ([Combat](combat.md#state-flags)) is not set and cleared by the move's logic but **held by
the clip's animation task**: a clip task (types 1, 3, 6) keeps at `+0x24` the bits its caller set on the human as it
pushed the task, and clears them from `+0x08` when the clip ends or is cut off; a fade clears the bits it was given on
release ([Animation](formats/animation.md#animation-tasks)). For the attacks, confirmed (code):

- `Attack_Start` (`0x002625a8`) builds a type-3 clip task holding **`0x7`**, whose next task is the looping idle (anim
  record `+0x54`) blended in over at most 0.2 s, pushes it with a fade, sets anim state 11 and sets `+0x08` bit `0x1`.
- The clip's **events** (`Anim_FireEvents`, `0x00101dd8`) move the phase: event `0x2c` clears `0x1` and sets `0x2`
  (the chain window opens, `0x001023d8`); `0x2d` clears `0x7` (or `0x8`) and sets `0x4` (`0x00102410`); `0x48`
  replaces the bits the task holds with `0x40000`, the recovery (`0x00102358`). Others: `0x3e` (`0x00227e18`), `0x3f`,
  and one at `0x0010208c` that sets `0x2000`.
- A press while `0x2` is set with a combo count of 1-2 swaps the clip in place (task vtable `+0x8c`) rather than
  pushing a new task.
- When the clip ends, the task clears what it still holds, so `0x40000` goes and the dispatcher's gate opens.

At runtime (confirmed (runtime), street, square once, the stick at 60 % up throughout): S1 (clip 12) lasts 20
updates. Record `+0x08` shows `0x1` on k = 0-5, `0x2` on k = 6-14, `0x4` on k = 15 and `0x40000` on k = 16-19; the
clip task (type 3) **keeps holding `0x7`** in its `+0x24` through k = 0-15 while `+0x08` shows one phase bit at a time
(the events change `+0x08`, not the task's copy), then holds `0x40000` from k = 16 (event `0x48` replaces it) and the
two agree. On k = 20 the clip has ended, `+0x08` is clear and the walk start (413, holding `0x10000000`) begins with
the body at 0.89 m/s: **the stick comes back on the first update after the clip**. The instance's task stack drops to
one task after the first update (the fade has finished).

So every attack's timing (wind-up, window, end, recovery) comes from its clip's events, and its length from the clip.
The same holds for any move whose builder hands flags to its task (grab `0x10`, start clips `0x10000000`, run stop and
climbs `0x80000`, landing `0x1000000`; seen at runtime, [Characters](characters.md#the-record)).

### Who reads the held flags {#readers}

| Reader | Address | Refuses while `+0x08` has | Evidence |
| --- | --- | --- | --- |
| the dispatcher, before every command | `0x0027c120` | any of `0x5c7fee0` | confirmed (code), [Combat](combat.md#input-return) |
| the chain | `0x00280630` | takes the press only while any of `0x7` | confirmed (code) |
| square, cross | `0x00286cc8`, `0x00287a18` | any of `0x100101f` | confirmed (code) |
| grab, tackle | `0x00284920` | any of `0xfc7eaf7` | confirmed (code) |
| the stick ([below](#locomotion-gate)) | `0x00241a88`, `0x00241cd4`, `0x00223cb0` | any of `0x110c0880`, or (busy) any outside `0x51140800` | confirmed (code) |
| the AI's attack action ([AI](ai.md#attack-action)) | `0x002fad70` | waits while any of `0x5c0221f` | confirmed (code) |

### The locomotion gate: when the stick comes back {#locomotion-gate}

Both `Human_PlayerLocomotion` (`0x00240e38`, the test at `0x00241a88`) and the fight-stance walk
(`Human_FightStanceMove`, `0x00241b90`, at `0x00241cd4`) set the stick's velocity to zero when **any** of these holds.
Confirmed (code):

- record `+0x08` has any of **`0x110c0880`**: `0x80`, `0x800`, `0x40000` (recovery), `0x80000`, `0x1000000`,
  `0x10000000` (a start clip, whose own root motion moves the body instead);
- record `+0x14` (the state code, setter `0x002266a8`) is **5 or 6**; the locomotion's own skid and stop step sets 5
  (`0x002419a0`);

and before that, the whole stick step is skipped (turning included) when:

- **`Human_IsBusy`** (`0x00223cb0`) is true: the state word has any of `0x79b9e1e0f30`, or the human is airborne
  (`0x00227f90`, state `0x1c00000000`), or state `0x100000000` (`0x00227eb0`), or per-player `+0x1e` is set, or
  record `+0x08` has any bit **except** `0x800`, `0x40000`, `0x100000`, `0x1000000`, `0x10000000` and `0x40000000`
  (mask `0xaeebf7ff`);
- or `0x00227d28` is true: the human is attached (`+0x280` ≠ -1) or in state `0x800000`.

Read against the runtime table on [Combat](combat.md#input-return): the wind-up, window and end bits (`0x1`, `0x2`,
`0x4`) make the human busy, the recovery `0x40000` zeroes the stick's velocity, and 389's `0x40000000` is in neither
mask, so **the stick moves the human on the first update after the clip has cleared its bits**, and 389 never holds
it.

**The 5 updates after a block** are not the state code but the **fade** into the idle. Confirmed (runtime), hooks on
the state code's setter (`0x002266ac`): after R1's release with the stick held at 60 % right, the idle 388 is pushed
with a type-9 fade task holding **`0x10000000`** for 5 updates. That bit is in the velocity gate, so the stick turns
the human on the spot (about 20° per update) but does not move him; the locomotion's call at `0x002419a0` writes code
5 on each of those updates. When the fade ends the walk start 413 begins (its call sites `0x0025b9cc` and
`0x0025f750` set code 3). A walk stop (408 → 388) gives the same 5-update fade with `0x10000000`; from a settled idle
413 starts at once. So **code 5 means turning in place**: nothing clears it, the walk start overwrites it with 3 and
the stop (`0x0025fa2c`) with 0. This corrects the earlier reading that code 5 itself held the stick for those 5
updates. On the first update of a start the locomotion's site sets 5 and then the dispatcher's call (`0x0027cbcc`)
sets 1.

## What an implementer needs {#implementer}

- **A fixed 60 Hz tick, a 30 Hz character step.** Run the world's objects on a 60 Hz tick (at most two ticks per
  frame) and `Humans_Update` on every second tick, in the order of [Humans_Update](#humans-update). Coney's test mode
  already steps at fixed dt; the order is what matters.
- **Brains before actions.** In one character step: player records (pad → command), then brains (AI → command into the
  same field), then animation, locomotion, then the dispatcher for every human. An AI human then needs no input path
  of its own.
- **Held flags instead of timers.** Give a clip task a set of `+0x08` bits that it clears when it ends or is replaced;
  make the clip's events `0x2c`, `0x2d` and `0x48` move the attack's phase; and make the dispatcher, the chain, the
  attack paths, the locomotion and the AI's attack action read the bits with the masks in [Who reads](#readers). This
  replaces the per-attack timing tables and the "every other move refuses while its clip plays" choice.
- **The locomotion gate** as one predicate: `IsBusy` (skip the stick step) and the velocity gate (`0x110c0880`, state
  code 5 or 6). The pause after a block or a stop comes from the fade into the idle holding `0x10000000` for its 5
  updates ([gate](#locomotion-gate)), not from a timer and not from code 5: a fade task must be able to hold bits.
- **Objects**: a scheduler with an update interval per object is enough until the world objects (doors, pick-ups,
  cars) come; the wheel's exact bucket order matters only for runtime diffs of objects other than humans, which are
  never on it.
- **Catch-up steps.** A runtime diff against the original sees two character steps in one frame about every 1000
  frames ([The play tick](#tick)); a trace keyed on the tick counter, not the frame, lines up.
- **Messages are synchronous**: a hit's warning (`0xa4`, `0xa6`) is handled inside the attacker's event, before the
  sender's next line ([Combat](combat.md#block)).

## Coney's implementation {#coneys-implementation}

- `src/human/humans.*` is `Humans_Update`'s order on Coney's fixed 1/30 s step (a Coney choice: no 60 Hz tick and no
  wheel until the world's objects come). Every human has a per-player record (`human::PlayerRecord`) that a pad or a
  brain writes; the dispatcher runs from it for every human, so a human no pad drives fights from the command its
  brain writes ([Characters](characters.md#coneys-implementation)).
- `src/animation/anim_task.*` holds the held flags and the events that move them, and a fade that holds bits until it
  ends (the idle's fade holds `0x10000000`, which gives the 5 updates after a block or a stop)
  ([Animation](formats/animation.md#coneys-implementation)); the readers' masks are in `src/combat/` and
  `src/human/locomotion_gate.*` ([Combat](combat.md#coneys-implementation)).

## Runtime checks {#runtime-checks}

All five were run (2026-10-05) with the scenarios in `repo:research/traces/scenarios/` (`tick_split`,
`wheel_objects`, `held_attack`, `block_release`, `turn_on_spot`, `civ_fight`) and the call hooks in
`repo:research/traces/patches.toml`; the results are in the body above: [the tick split](#tick), [humans on the
wheel](#wheel), [held flags through an attack](#held-flags), [the block's 5](#locomotion-gate) and [brain then
dispatcher](#humans-update). None is left.

## Code index {#code-index}

Every function of the engine core below the script types (`0x00386f58`-`0x003a8698`: `CarTask`, the glass, light and
object managers, `SceneTask` and `TaskManager`), grouped by the section of the page that explains it. The script
types are on [Script types](script-types.md); the AI on [AI code index](ai-code.md).

### Cars: The car object {#code-cars-car-object}

See [Cars: The car object](cars.md#car-object).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003875b8` | `CarTask_Destructor` | Car task destructor: releases the car and frees it when asked. | confirmed (code) |
| `0x00387650` | `Car_SetInstance` | Sets the car's model instance and re-opens parts flagged open (8). | confirmed (code) |
| `0x00387a90` | `Car_ReleaseInstance` | Releases the car's model instance. | confirmed (code) |
| `0x00387ad8` | `Car_Release` | Releases a car task. | confirmed (code) |
| `0x00387ee0` | `Car_FreeBody` | Frees the car's physics body. | confirmed (code) |
| `0x00388c80` | `CarTask_Update` | Car update: physics substeps, explode countdown, loose parts and sound position. | confirmed (code) |
| `0x003895d8` | `Car_Integrate` | Integrates the car's motion; gravity 15.68 while airborne. | confirmed (code) |
| `0x00389760` | `Car_OnDetach` | Handles the car being detached from its parent. | confirmed (code) |
| `0x003897b0` | `Car_SetPosition` | Sets the car's position. | confirmed (code) |
| `0x003897c8` | `Car_SetRotation` | Sets the car's rotation. | confirmed (code) |
| `0x003897e0` | `CarTask_SetVelocity` | Thunk to Task_SetVelocity. | confirmed (code) |
| `0x00389800` | `Car_SetVelocityXYZ` | Sets the car's velocity from three components. | inferred |
| `0x00389898` | `Car_SetPose` | Sets position and rotation together. | confirmed (code) |
| `0x003898d0` | `Car_NoteStreamingDistance` | Records the car's distance for model streaming. | confirmed (code) |
| `0x00389d68` | `Car_ForwardTrunkMessage` | Forwards messages 0x12/0x13 to the object in the trunk. | confirmed (code) |
| `0x00389e08` | `Car_OnLanded` | Handles the car landing; sends message 0x3f. | inferred |
| `0x0038d528` | `Car_SetTrunkItemKind` | Sets the kind of trunk item. | confirmed (code) |
| `0x0038d538` | `Car_SetTrunkObject` | Sets the object in the trunk. | confirmed (code) |
| `0x0038d690` | `Car_SpawnRadio` | Spawns the car stereo. | confirmed (code) |
| `0x0038d6d8` | `Car_PlaySound` | Plays a sound on the car's sound handle. | confirmed (code) |
| `0x0038d758` | `Car_UpdateSoundPosition` | Moves the car's sound to the car. | confirmed (code) |
| `0x0038d798` | `Car_StopSound` | Stops the car's sound. | confirmed (code) |
| `0x0038d7d8` | `Car_FindSpotOfHuman` | Finds which of the 10 spots round the car a human holds. | inferred |
| `0x0038d810` | `Car_ReserveSpot` | Reserves a free spot round the car for a human. | inferred |
| `0x0038d998` | `Car_ReleaseSpot` | Releases a spot round the car. | inferred |
| `0x0038d9d8` | `Car_ClearSpots` | Clears the 10 spot bytes. | confirmed (code) |
| `0x0038da88` | `Car_IsSpotBlocked` | Says whether a spot is blocked by missing parts. | inferred |
| `0x0038dae0` | `Car_AreAllSpotsBlocked` | Says whether every spot is blocked. | inferred |
| `0x0038db40` | `Car_GetSpotPosition` | Returns a spot's world position (height -0.6 or -0.95). | confirmed (code) |
| `0x0038dde8` | `Car_Spawn` | Spawns a car. | confirmed (code) |
| `0x0038dea8` | `Car_Destroy` | Destroys a car. | confirmed (code) |
| `0x0038e098` | `Car_PlaceInTrunk` | Places an object in the trunk. | confirmed (code) |
| `0x0038e430` | `CarTaskManager_Create` | Takes a free car, makes the shared particle once, inits it by name and schedules it. | confirmed (code) |
| `0x0038e538` | `CarManager_SetMsgHandler` | Sets the car manager's message handler. | confirmed (code) |
| `0x0038e5f8` | `CarTaskManager_FindNearest` | Finds the nearest live car to a point. | confirmed (code) |
| `0x0038e738` | `CarTaskManager_ListInRange` | Lists the cars within a range of a point. | confirmed (code) |
| `0x0038ebd0` | `CarTaskManager_ClearHandlers` | Clears the manager's handler block (0x68 bytes). | confirmed (code) |

### Cars: What is drawn {#code-cars-drawn}

See [Cars: What is drawn](cars.md#drawn).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00389918` | `CarTask_PreRender` | Prepares the car and its parts for drawing. | confirmed (code) |

### Cars: Explosion {#code-cars-explode}

See [Cars: Explosion](cars.md#explode).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0038b1f8` | `Car_StartExplosionLaunch` | Starts the upward launch of an exploding car. | confirmed (code) |
| `0x0038dfa0` | `Car_Explode` | Explodes a car. | confirmed (code) |

### Cars: Parts {#code-cars-parts}

See [Cars: Parts](cars.md#parts).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00387368` | `CarPart_Init` | Clears one 0xa0-byte car part record. | confirmed (code) |
| `0x003873c8` | `CarPart_Reset` | Resets a part record and destroys its attached object with message 0x15. | confirmed (code) |
| `0x003875e8` | `Car_ResetParts` | Resets all 26 part records and clears the part masks; the worker of Car_Repair. | confirmed (code) |
| `0x00387780` | `Car_CachePartTransforms` | Caches part transforms, marks the body dirty and forwards trunk messages 0x35/0x40. | confirmed (code) |
| `0x00387f18` | `Car_UpdateLoosePart` | Counts a loose part down, swings hinged ones, plays up to 3 impact sounds, removes it at 0. | confirmed (code) |
| `0x00389f40` | `Table_BandIndexBelow` | Finds the band of a sorted float table below a value. | confirmed (code) |
| `0x00389fa8` | `Table_BandIndexAbove` | Finds the band of a sorted float table above a value. | confirmed (code) |
| `0x0038a010` | `Car_DetachPart` | Makes a part loose for 360 ticks with a random spin; particle when blown off. | confirmed (code) |
| `0x0038a7e8` | `Car_BreakPartByIndex` | Breaks one part by its index. | confirmed (code) |
| `0x0038b520` | `Car_GetHitZoneFromPoint` | Maps a hit point on the car to a hit zone. | confirmed (code) |
| `0x0038c990` | `Car_HitZoneHasParts` | Says whether a hit zone still has parts (height tests +1.5/+1.0). | inferred |
| `0x0038cb28` | `Car_FindPartRecordByBits` | Finds a part record by its mask bits. | confirmed (code) |
| `0x0038da08` | `Car_GetSpotPartMask` | Returns the part mask of a spot (jump table). | confirmed (code) |
| `0x0038dee8` | `Car_SetPartDamage` | Sets a part's damage. | confirmed (code) |
| `0x0038dfe0` | `Car_RemovePart` | Removes a part. | confirmed (code) |
| `0x0038e030` | `Car_SetDamageFilter` | Sets the damage filter. | confirmed (code) |
| `0x0038e068` | `Car_Repair` | Repairs a car. | confirmed (code) |

### Cars: Type record {#code-cars-type-record}

See [Cars: Type record](cars.md#type-record).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0038d000` | `Car_GetTypeRecord` | Returns the 0x5f0-byte type record of the car's type. | confirmed (code) |

### Lighting: Flicker {#code-lighting-flicker}

See [Lighting: Flicker](lighting.md#flicker).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003906b8` | `LightTask_Update` | Fades a light over its time and switches it by flag 4. | confirmed (code) |

### Lighting: The light manager {#code-lighting-manager}

See [Lighting: The light manager](lighting.md#manager).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00390310` | `LightTask_Release` | Releases a light task. | confirmed (code) |
| `0x00390370` | `LightTask_Init` | Inits a light task: flags 0xb, updated every 2 ticks. | confirmed (code) |
| `0x00390920` | `LightTask_CallCallback` | Calls the light's callback. | confirmed (code) |
| `0x00390970` | `LightTask_Process` | Per-tick light processing. | confirmed (code) |
| `0x00390cb8` | `LightTaskManager_CreateByName` | Creates a light task of a named script type. | confirmed (code) |
| `0x00390d90` | `LightTaskManager_CreateWithParams` | Creates a light and sets its parameters. | confirmed (code) |

### Lighting: The light record (0x50 bytes) {#code-lighting-record}

See [Lighting: The light record (0x50 bytes)](lighting.md#record).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00390480` | `LightTask_SetParams` | Sets a light's colour, range and fade parameters. | confirmed (code) |

### World objects: Swinging doors: states and commands {#code-objects-door-states}

See [World objects: Swinging doors: states and commands](objects.md#door-states).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003971c8` | `Door_SetState0` | Sends the door message 0x22 with state 0. | confirmed (code) |
| `0x00397508` | `Door_IsOpen` | Says whether a door is open. | confirmed (code) |

### World objects: Swinging doors {#code-objects-doors}

See [World objects: Swinging doors](objects.md#doors).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00395020` | `ObjType_IsDoorKind` | Says whether a kind is a door (15, 25, 30 or 33). | confirmed (code) |
| `0x00397330` | `Door_EmptyStub` | Empty stub. | confirmed (code) |
| `0x00397338` | `Door_DisableCollision` | Turns off a door's collision. | confirmed (code) |
| `0x003973a0` | `Door_SetCameraGhost` | Lets the camera pass through a door. | confirmed (code) |

### World objects: Dynamic objects {#code-objects-dynamic-objects}

See [World objects: Dynamic objects](objects.md#dynamic-objects).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003924d8` | `Obj_FreePhysicsBody` | Frees the object's physics body. | confirmed (code) |
| `0x003925b0` | `WorldObject_EmptyStub` | Empty stub. | confirmed (code) |
| `0x003925f8` | `WorldObject_SetVelocity` | Thunk that sets the object's velocity. | confirmed (code) |
| `0x00392638` | `Explosion_DamageHumansInRadius` | Damages the humans in an explosion's radius. | confirmed (code) |
| `0x00393450` | `WorldObject_TakeHit` | An object takes a hit. | confirmed (code) |
| `0x003939a8` | `WorldObject_OnImpact` | On contact: impact sound by material, damage, a hearing event; kind 8 reports a crime. | confirmed (code) |
| `0x00393e20` | `WorldObject_Break` | Breaks an object: dropped, body freed, break message; kind 8 explodes. | confirmed (code) |
| `0x00394810` | `WorldObject_BreakEffects` | Spawns the break particles. | confirmed (code) |
| `0x00394920` | `WorldObject_UpdateBodyContacts` | Updates the body's contact springs from type fields +0x78..+0x80. | speculative |
| `0x003952f8` | `WorldObject_SetPose` | Sets position and rotation together. | confirmed (code) |
| `0x00395dc8` | `WorldObject_OnTaskMessage` | Message handler: 0x30 creates the body; others go to the script handler. | confirmed (code) |
| `0x00395f38` | `Object_AsWorldObject` | Returns the task as a world object. | confirmed (code) |
| `0x00395f88` | `WorldObject_PlaySound` | Plays a sound on the object. | confirmed (code) |
| `0x00396008` | `WorldObject_UpdateSoundPosition` | Moves the object's sound to the object. | confirmed (code) |
| `0x00396048` | `WorldObject_StopSound` | Stops the object's sound. | confirmed (code) |
| `0x00396710` | `Object_IsStrikeTarget` | Says whether an object can be struck. | confirmed (code) |
| `0x00396a08` | `Obj_Show` | Shows an object. | confirmed (code) |
| `0x00396a90` | `Obj_EnablePhysics` | Enables an object's physics. | confirmed (code) |
| `0x00396b68` | `Obj_Hide` | Hides an object. | confirmed (code) |
| `0x00396c58` | `Obj_Destroy` | Destroys an object. | confirmed (code) |
| `0x00396d78` | `WorldObjects_CallScriptCallback` | Calls an object's script callback through the script VM. | confirmed (code) |
| `0x00397598` | `Obj_Exists` | Says whether an object exists. | confirmed (code) |
| `0x003976c8` | `Obj_ChangeState` | Changes an object's state. | confirmed (code) |
| `0x003978d0` | `WorldObject_PreloadSound` | Preloads an object's sound. | confirmed (code) |
| `0x00397998` | `WorldObject_StartSound` | Starts an object's sound. | confirmed (code) |
| `0x003980c8` | `ObjectTaskManager_Update` | Updates the live objects. | confirmed (code) |
| `0x0039a6b0` | `ObjectManager_FindNearestNamed` | Finds the nearest object of a named type. | confirmed (code) |
| `0x0039ab20` | `ObjectList_FindInRange` | Lists objects within a range. | confirmed (code) |
| `0x0039acb8` | `ObjectManager_FindKind24ByName` | Finds an object of kind 24 by name. | confirmed (code) |
| `0x003a4810` | `Body_ClearFlags` | Clears flags of a physics body. | confirmed (code) |
| `0x003a4b50` | `ScriptObj_Explode` | Explodes a script object. | confirmed (code) |
| `0x003a4c48` | `ScriptObj_ScoreBoardSignal` | Signals the score board. | confirmed (code) |
| `0x003a4cb0` | `WorldObject_SetField124` | Sets the object's field +0x124. | confirmed (code) |
| `0x003a5530` | `ScriptObj_SpawnThrownDebris` | Creates a debris object and sends it message 0x30. | inferred |
| `0x003a57d8` | `ScriptObj_Destroy` | Destroys a script object. | confirmed (code) |
| `0x003a6ec8` | `WorldObject_SetHitMode` | Sets the object's hit mode (+0x128). | confirmed (code) |
| `0x003a6f68` | `WorldObject_HitEffectsByModel` | Spawns debris, splinters or particles chosen by the model's hash. | confirmed (code) |

### World objects: Glass types {#code-objects-glass}

See [World objects: Glass types](objects.md#glass).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0039c290` | `Cfg_SetGlassProperties` | Sets glass properties. | confirmed (code) |
| `0x003a4d48` | `GlassTypes_GetEntryValue` | Returns a value of a glass type entry. | confirmed (code) |

### World objects: Objects in a human's hand {#code-objects-held}

See [World objects: Objects in a human's hand](objects.md#held).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003932c8` | `OverheadWeapon_ContactDamage` | Contact damage of an overhead weapon. | confirmed (code) |
| `0x003951d8` | `WorldObject_GetHolder` | Returns the human holding the object. | confirmed (code) |
| `0x00395220` | `WorldObject_OnDetach` | Handles the object being detached. | confirmed (code) |
| `0x00395268` | `WorldObject_SetSlot` | Sets the object's slot (+0x110) and body flag 0x10. | confirmed (code) |

### World objects: Leaves: model, hinge and swing {#code-objects-leaves}

See [World objects: Leaves: model, hinge and swing](objects.md#leaves).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00397400` | `Door_GetLeftLeaf` | Returns the left leaf. | confirmed (code) |
| `0x00397478` | `Door_GetRightLeaf` | Returns the right leaf. | confirmed (code) |
| `0x003a50a8` | `Door_GetLeafResultA` | Returns a door-leaf result global. | confirmed (code) |
| `0x003a50c0` | `Door_GetLeafResultB` | Returns a door-leaf result global. | confirmed (code) |
| `0x003a50d0` | `Door_GetLeafResultC` | Returns a door-leaf result global. | confirmed (code) |

### World objects: The model {#code-objects-models}

See [World objects: The model](objects.md#models).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00395330` | `WorldObject_UpdateRender` | Updates the object's model for drawing. | confirmed (code) |
| `0x003954c8` | `WorldObject_ReleaseInstance` | Releases the object's model instance. | confirmed (code) |
| `0x00395720` | `WorldObject_PreRender` | Prepares the object for drawing. | confirmed (code) |

### World objects: Navigation links {#code-objects-nav-links}

See [World objects: Navigation links](objects.md#nav-links).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003a46c8` | `CollisionTri_ClearFlag1` | Clears flag 1 on collision triangles. | confirmed (code) |
| `0x003a4710` | `CollisionTri_SetFlag1` | Sets flag 1 on collision triangles. | confirmed (code) |
| `0x003a49e8` | `CollisionTri_SetFlag40` | Sets flag 0x40 on collision triangles. | confirmed (code) |
| `0x003a4a40` | `CollisionMesh_OrTriangleTypeBits` | Ors type bits into triangles. | confirmed (code) |
| `0x003a4a98` | `CollisionTri_SetMaterial` | Sets the material of collision triangles. | confirmed (code) |
| `0x003a4ad8` | `CollisionTri_EmptyStub` | Empty stub. | confirmed (code) |
| `0x003a4ae0` | `NavLink_Close` | Closes a nav link. | confirmed (code) |
| `0x003a4b00` | `NavLink_Open` | Opens a nav link. | confirmed (code) |
| `0x003a4b30` | `NavLink_Call250c00` | Calls 0x00250c00 with 0x40. | inferred |

### World objects: Object types {#code-objects-object-types}

See [World objects: Object types](objects.md#object-types).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00390e88` | `ObjType_ResolveNames` | Resolves the names an object type refers to. | inferred |
| `0x003910f0` | `ObjType_SetFloats68` | Sets the type's floats +0x68/+0x6c by type name. | confirmed (code) |
| `0x00391138` | `ObjTypeList_Reset` | Clears the object type list and its groups. | confirmed (code) |
| `0x00391330` | `ObjTypeList_Get` | Returns the 0x90-byte type record at an index. | confirmed (code) |
| `0x00391378` | `ObjTypeList_Grow` | Adds a record to the type list. | inferred |
| `0x00391518` | `ObjType_GetName` | Returns the type's name (+0x28). | confirmed (code) |
| `0x00391530` | `ObjType_GetSecondName` | Returns the type's second name (+0x43). | confirmed (code) |
| `0x00391548` | `ObjType_GetKind` | Returns the type's kind byte (+0x86). | confirmed (code) |
| `0x00391560` | `ObjType_FindByName` | Finds a type record by name. | confirmed (code) |
| `0x00391590` | `ObjType_FindByHash` | Finds a type record by name hash (+0x8c). | confirmed (code) |
| `0x003915d0` | `ObjTypeList_SetGroup` | Fills one of the 10 type groups. | confirmed (code) |
| `0x003916a8` | `ObjTypeList_PickFromGroup` | Picks a type from a group. | confirmed (code) |
| `0x00391d20` | `WorldObject_GetSecondName` | Returns the second name of the object's type. | confirmed (code) |
| `0x00395e40` | `WorldObject_GetFloatProperty` | Returns a float property of the object's type. | confirmed (code) |
| `0x00395f00` | `WorldObject_GetIntProperty` | Returns an integer property of the object's type. | confirmed (code) |
| `0x00397780` | `ObjType_FindIndex` | Finds a type's index. | confirmed (code) |
| `0x00397838` | `Obj_IsType` | Says whether an object is of a type. | confirmed (code) |
| `0x003a37b8` | `ObjType_GetIntField` | Returns an integer field of a type. | confirmed (code) |
| `0x003a5998` | `ScriptObj_IsNamed` | Says whether an object's type has a name. | confirmed (code) |

### World objects: The level's placed objects (_objs.txt) {#code-objects-objs-file}

See [World objects: The level's placed objects (_objs.txt)](objects.md#objs-file).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0039ae48` | `ObjectManager_LoadChunks` | Loads the level's object chunks 0x24, 0x23 and 0x22. | confirmed (code) |

### World objects: How a human opens a door {#code-objects-opening}

See [World objects: How a human opens a door](objects.md#opening).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00396e28` | `Door_OpenToAngle` | Opens a door to an angle. | confirmed (code) |
| `0x00396ea0` | `Door_Open` | Opens a door. | confirmed (code) |
| `0x00396f08` | `Door_OpenBy` | Opens a door for a human. | confirmed (code) |
| `0x00396fa8` | `Door_OpenAnimated` | Opens a door with its animation. | confirmed (code) |
| `0x00397010` | `Door_Close` | Closes a door. | confirmed (code) |

### World objects: A pane's life {#code-objects-pane}

See [World objects: A pane's life](objects.md#pane).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0038ed08` | `GlassPane_Release` | Releases a glass pane task. | confirmed (code) |
| `0x0038eef8` | `GlassPane_QueryBody` | Returns the pane's physics body. | confirmed (code) |
| `0x0038ef30` | `GlassPane_NoteDistance` | Records the pane's distance for streaming. | confirmed (code) |
| `0x0038f170` | `GlassPane_OnLink` | Handles the pane being linked to its parent. | inferred |
| `0x0038f588` | `Object_AsGlassPane` | Returns the task as a glass pane when its type bit 0x400 is set. | confirmed (code) |
| `0x0038f5d8` | `GlassPane_SetTimer` | Sets the pane's timer. | confirmed (code) |
| `0x0038fae0` | `GlassManager_GetEntry8` | Returns the manager field +0x08. | confirmed (code) |
| `0x0038faf0` | `GlassManager_GetEntryC` | Returns the manager field +0x0c. | confirmed (code) |
| `0x0038fb20` | `GlassTaskManager_FindNearest` | Finds the nearest pane to a point. | confirmed (code) |
| `0x0038fbe8` | `GlassTaskManager_Update` | Updates the live panes. | confirmed (code) |
| `0x0038fc70` | `GlassTaskManager_ListInRange` | Lists the panes within a range of a point. | confirmed (code) |
| `0x003a4c78` | `GlassPane_SetType` | Sets a pane's glass type. | confirmed (code) |
| `0x003a4ce8` | `GlassPane_GetType` | Returns a pane's glass type. | confirmed (code) |

### World objects: Moving into a pane {#code-objects-pane-break}

See [World objects: Moving into a pane](objects.md#pane-break).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003a5810` | `ScriptObj_BreakGlassAround` | Breaks the glass panes round a point. | confirmed (code) |

### World objects: Pickable objects {#code-objects-pickable}

See [World objects: Pickable objects](objects.md#pickable).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00395148` | `WorldObject_CanBePickedBy` | Says whether a human may pick the object up. | confirmed (code) |
| `0x00395510` | `WorldObject_TryTouchPickUp` | Picks an object up on touch; health is skipped at full health. | confirmed (code) |
| `0x00396090` | `WorldObjects_TouchPickUpByHash` | Runs touch pick-up for the objects whose type hash matches. | inferred |

### World objects: Spawn records {#code-objects-spawn-records}

See [World objects: Spawn records](objects.md#spawn-records).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00392510` | `WorldObject_SetOwnedFromFlags` | Sets the object's owned state from its spawn record flags. | confirmed (code) |
| `0x00395d20` | `WorldObject_SetRespawnTimer` | Sets the object's respawn timer. | confirmed (code) |
| `0x00396778` | `ObjZone_Enable` | Enables a spawn zone. | confirmed (code) |
| `0x003967a8` | `ObjZone_IsEnabled` | Says whether a zone is enabled. | confirmed (code) |
| `0x003967e0` | `ObjZone_Mark` | Marks a zone. | confirmed (code) |
| `0x00396810` | `Obj_GetZone` | Returns an object's zone. | confirmed (code) |
| `0x003978a8` | `ObjMgr_UnloadTimed` | Unloads timed objects. | confirmed (code) |
| `0x003979f8` | `Bitset_Test` | Tests one bit of a bit set. | confirmed (code) |
| `0x00397a20` | `Bitset_Set` | Sets one bit of a bit set. | confirmed (code) |
| `0x00397e08` | `ObjectManager_AllocSpawnRecords` | Allocates the spawn records: the level's count plus 500. | confirmed (code) |
| `0x00397f28` | `ObjectManager_ResetLevelLists` | Clears the two 35-word per-level lists. | confirmed (code) |
| `0x00398130` | `ObjectManager_ClearLevel` | Clears the level's objects and spawn records. | confirmed (code) |
| `0x00398348` | `ObjZone_SetEnabled` | Sets or clears a zone's enable bit. | confirmed (code) |
| `0x003983b0` | `ObjZone_MarkRecords` | Marks the spawn records of a zone. | confirmed (code) |
| `0x003984f8` | `SpawnTable_AllocRecord` | Takes a free 0x28-byte spawn record. | confirmed (code) |
| `0x00398ec8` | `SpawnTable_ClearOwnedBit` | Clears a record's owned bit 0x80000. | confirmed (code) |
| `0x00398f60` | `ObjectManager_GetSpawnZone` | Returns a spawn record's zone. | confirmed (code) |
| `0x00399b20` | `SpawnTable_UnloadTimed` | Unloads timed records. | confirmed (code) |
| `0x00399bf0` | `SpawnTable_GetRecordIndex` | Returns a record's index in the table. | confirmed (code) |
| `0x00399cd0` | `SpawnTable_FindRecord` | Finds a spawn record. | confirmed (code) |
| `0x00399d60` | `SpawnTable_GetLiveRecord` | Returns a record when it is live (0x20000). | confirmed (code) |
| `0x0039a7e0` | `ObjZone_SetMsgHandler` | Sets a zone's message handler. | confirmed (code) |
| `0x0039aa40` | `ObjZone_ListObjects` | Lists the objects of a zone. | confirmed (code) |
| `0x0039ada0` | `SpawnTable_DisableRecordFlag` | Clears a flag of a spawn record. | confirmed (code) |
| `0x0039aea0` | `ObjectManager_FreeArrays` | Frees the manager's arrays. | confirmed (code) |

### World objects: Streaming {#code-objects-streaming}

See [World objects: Streaming](objects.md#streaming).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003956c0` | `WorldObject_NoteStreamingDistance` | Records the object's distance for streaming. | confirmed (code) |

### World objects: The tint {#code-objects-tint}

See [World objects: The tint](objects.md#tint).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0039c2e0` | `Obj_SetWidgetColour` | Sets an object's tint. | confirmed (code) |
| `0x003a4bd0` | `ScriptObj_GetColourVec` | Returns the object's colour as a vector. | confirmed (code) |
| `0x003a5f80` | `Colour_UnpackToVec` | Unpacks a packed colour into floats. | confirmed (code) |

### Particles: Drifting fog {#code-particles-fog}

See [Particles: Drifting fog](particles.md#fog).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003977f8` | `GroundFog_Enable` | Turns ground fog on or off. | confirmed (code) |

### Particles: Blowing litter {#code-particles-garbage}

See [Particles: Blowing litter](particles.md#garbage).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003977a8` | `Garbage_Start` | Starts blowing garbage. | confirmed (code) |
| `0x003977d0` | `Garbage_End` | Ends blowing garbage. | confirmed (code) |

### Particles: Spawning {#code-particles-spawning}

See [Particles: Spawning](particles.md#spawning).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003975c0` | `Particle_Start` | Starts a particle system. | confirmed (code) |
| `0x00397610` | `Particle_End` | Ends a particle system. | confirmed (code) |
| `0x00397660` | `Particle_ChangeState` | Changes a particle system's state. | confirmed (code) |
| `0x00397730` | `Particle_Kill` | Kills a particle system. | confirmed (code) |
| `0x0039bc28` | `Tag_Configure` | Configures a tag. | confirmed (code) |
| `0x0039bd50` | `Tag_SetState` | Sets a tag's state with messages 0x19 (4 or 6) or 0x39/0x3a. | confirmed (code) |
| `0x0039c388` | `PTank_Create` | Creates a particle tank. | confirmed (code) |
| `0x0039c3d0` | `PTank_Release` | Releases a particle tank. | confirmed (code) |
| `0x003a2e20` | `Task_CreateGlassByName` | Creates a glass pane of a named type. | confirmed (code) |
| `0x003a2e60` | `Task_CreateObjectByName` | Creates an object of a named type. | confirmed (code) |
| `0x003a2e80` | `Task_CreateLightByName` | Creates a light of a named type. | confirmed (code) |
| `0x003a41a8` | `Task_CreateGlass` | Creates a glass pane and returns its handle. | confirmed (code) |
| `0x003a41f0` | `Task_CreateObject` | Creates an object and returns its handle. | confirmed (code) |
| `0x003a4240` | `Task_CreateLight` | Creates a light and returns its handle. | confirmed (code) |
| `0x003a4f78` | `PTank_Destroy` | Destroys a particle tank. | confirmed (code) |
| `0x003a4fd0` | `PTank_GetInstance` | Returns a particle tank's instance. | confirmed (code) |
| `0x003a58e8` | `PTank_SetDepth` | Sets a particle tank's depth. | confirmed (code) |
| `0x003a5ad8` | `Effects_SpawnBurst` | Spawns a burst of two particle kinds. | confirmed (code) |
| `0x003a5fb0` | `Effects_BurstA` | Spawns an effect burst. | confirmed (code) |
| `0x003a61d8` | `Effects_BurstB` | Spawns an effect burst. | confirmed (code) |
| `0x003a65e0` | `Effects_BurstC` | Spawns an effect burst. | confirmed (code) |
| `0x003a6a10` | `Effects_BurstD` | Spawns an effect burst. | confirmed (code) |
| `0x003a6e48` | `Effects_ReturnZeroA` | Returns 0. | confirmed (code) |
| `0x003a6e88` | `Effects_ReturnZeroB` | Returns 0. | confirmed (code) |
| `0x003a7d58` | `Effects_CanSpawnAt` | Says whether an effect may spawn: camera within 15 m and on screen. | inferred |

### Particles: Steam vents {#code-particles-steam}

See [Particles: Steam vents](particles.md#steam).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0039be28` | `Steam_Configure` | Configures steam. | confirmed (code) |

### Particles: Particle task fields {#code-particles-task-fields}

See [Particles: Particle task fields](particles.md#task-fields).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0039b020` | `ParticleTask_Draw` | Draws a particle task's sprites. | inferred |
| `0x0039b6b8` | `ParticleTask_DrawSimple` | Draws a particle task the simple way. | inferred |
| `0x0039ba30` | `ParticleTask_OnMessage` | Passes a message to the particle type's handler. | confirmed (code) |
| `0x0039ba68` | `ParticleTask_Process` | Per-tick particle processing. | confirmed (code) |
| `0x0039bb18` | `ParticleTask_SetRect` | Sets the particle task's rectangle. | confirmed (code) |
| `0x0039bb38` | `ParticleTask_Release` | Releases a particle task. | confirmed (code) |
| `0x0039bbf0` | `ParticleTask_QueryBody` | Returns the particle task's body. | confirmed (code) |
| `0x0039c770` | `ParticleTaskManager_Draw` | Draws the pool's live particle tasks. | confirmed (code) |
| `0x0039c7d8` | `ParticleTaskManager_FindNearest` | Finds the nearest particle task to a point. | confirmed (code) |
| `0x0039c8f8` | `ParticleTaskManager_KillInRange` | Kills the particle tasks within a range. | confirmed (code) |
| `0x0039ca10` | `ParticleTaskManager_Count` | Counts the pool's live tasks. | confirmed (code) |
| `0x003a2cf0` | `ParticleSystem_SetFieldDA` | Sets a particle system's byte +0xda. | confirmed (code) |
| `0x003a5938` | `ParticleManager_Query5938` | Particle manager query; exact use not traced. | speculative |
| `0x003a5a50` | `ParticlePool_HasRoom` | Says whether more than 512 particles are free. | confirmed (code) |

### Scenes: Objects, camera and lights {#code-scenes-camera}

See [Scenes: Objects, camera and lights](scenes.md#camera).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003a0e48` | `SceneTask_StartCameraPart` | Starts the next camera part of a scene. | confirmed (code) |
| `0x003a0ed8` | `SceneTask_UpdateCamera` | Updates the scene camera. | confirmed (code) |
| `0x003a1540` | `SceneTaskManager_GetCamera` | Returns the scene camera. | confirmed (code) |
| `0x003a1558` | `SceneTaskManager_SetCamera` | Sets the scene camera. | confirmed (code) |

### Scenes: Ending {#code-scenes-ending}

See [Scenes: Ending](scenes.md#ending).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0039d278` | `SceneTask_FreeData` | Frees the scene's loaded data. | confirmed (code) |
| `0x0039d360` | `SceneTask_Release` | Releases a scene task. | confirmed (code) |
| `0x003a0a28` | `SceneTask_SetEndCallback` | Sets the callback called when the scene ends. | confirmed (code) |
| `0x003a0d40` | `SceneTask_Finish` | Finishes a scene. | confirmed (code) |

### Tasks: Task classes and pools {#code-tasks-classes}

See [Tasks: Task classes and pools](#classes).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0038e168` | `CarTaskManager_Init` | Builds the car pool: 18 cars of 0x1310 bytes. | confirmed (code) |
| `0x0038e2b0` | `CarTaskManager_Destroy` | Frees the car pool. | confirmed (code) |
| `0x0038e6c0` | `CarTaskManager_Reset` | Resets every car in the pool. | confirmed (code) |
| `0x0038f600` | `GlassTaskManager_Init` | Builds the glass pool: 100 panes of 0x100 bytes. | confirmed (code) |
| `0x0038f728` | `GlassTaskManager_Destroy` | Frees the glass pool. | confirmed (code) |
| `0x00390a10` | `LightTaskManager_Init` | Builds the light pool: 28 lights of 0xb0 bytes. | confirmed (code) |
| `0x00390b38` | `LightTaskManager_Destroy` | Frees the light pool. | confirmed (code) |
| `0x00397c58` | `ObjectTaskManager_Destroy` | Frees the object pool. | confirmed (code) |
| `0x0039c3f0` | `ParticleTaskManager_Init` | Builds a particle pool of 0xf0-byte tasks. | confirmed (code) |
| `0x0039c518` | `ParticleTaskManager_Destroy` | Frees a particle pool. | confirmed (code) |
| `0x003a1120` | `SceneTaskManager_Init` | Builds the scene pool: 12 scenes of 0x100 bytes. | confirmed (code) |
| `0x003a1250` | `SceneTaskManager_Destroy` | Frees the scene pool. | confirmed (code) |
| `0x003a14e8` | `SceneTaskManager_Free` | Frees the scene manager. | confirmed (code) |

### Tasks: Handles {#code-tasks-handles}

See [Tasks: Handles](#handles).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0038fdf0` | `Handles_Clear` | Clears the 0xb00-entry handle table. | confirmed (code) |
| `0x00390038` | `Handle_Free` | Frees a handle slot. | confirmed (code) |
| `0x00390068` | `Handle_ResolveWorldObject` | Resolves a handle to a world object or null. | confirmed (code) |
| `0x003900b0` | `Handle_GetByIndexA` | Wrapper of Handle_GetByIndex. | confirmed (code) |
| `0x003900d0` | `Handle_GetByIndexB` | Wrapper of Handle_GetByIndex. | confirmed (code) |
| `0x003900f0` | `Handle_GetByIndexC` | Wrapper of Handle_GetByIndex. | confirmed (code) |
| `0x00390110` | `Handle_GetByIndexD` | Wrapper of Handle_GetByIndex. | confirmed (code) |
| `0x00390130` | `Handle_GetByIndexE` | Wrapper of Handle_GetByIndex. | confirmed (code) |
| `0x00390150` | `Handle_GetByIndex` | Returns the handle stored at a table index. | confirmed (code) |
| `0x00390200` | `Handle_ResolveA` | Thunk to Handle_Resolve. | confirmed (code) |
| `0x00390220` | `Handle_ResolveB` | Thunk to Handle_Resolve. | confirmed (code) |
| `0x00390248` | `Handle_ResolveC` | Thunk to Handle_Resolve. | confirmed (code) |
| `0x003902a8` | `Handle_ResolveD` | Thunk to Handle_Resolve. | confirmed (code) |
| `0x003902c8` | `Handles_StaticInit` | Static init: sets the null handle to -1. | confirmed (code) |
| `0x003902f0` | `Handles_StaticInitThunk` | Thunk to Handles_StaticInit. | confirmed (code) |

### Tasks: Messages and events {#code-tasks-messages}

See [Tasks: Messages and events](#messages).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003a2600` | `ObjMsg_Begin` | Begins an object message. | confirmed (code) |
| `0x003a2628` | `ObjMsg_End` | Ends an object message. | confirmed (code) |
| `0x003a2660` | `ObjMsg_PushInt` | Pushes an integer. | confirmed (code) |
| `0x003a2688` | `ObjMsg_Send` | Sends an object message. | confirmed (code) |
| `0x003a26e0` | `ObjMsg_PushObject` | Pushes an object. | confirmed (code) |
| `0x003a2d20` | `Msg_Begin` | Begins a message: takes the next 0x410-byte scratch frame. | confirmed (code) |
| `0x003a2d40` | `Msg_End` | Ends a message: releases the scratch frame. | confirmed (code) |
| `0x003a2d80` | `Msg_PushVec4b` | Pushes a vector onto the message stack. | confirmed (code) |
| `0x003a2de0` | `Msg_PushFloat` | Pushes a float onto the message stack. | confirmed (code) |
| `0x003a3ff8` | `MsgFrame_Push` | Pushes a message scratch frame. | confirmed (code) |
| `0x003a4030` | `MsgFrame_Pop` | Pops a message scratch frame. | confirmed (code) |
| `0x003a4048` | `MsgFrame_PushVec4A` | Pushes a vector into the current frame. | confirmed (code) |
| `0x003a4080` | `MsgFrame_PushVec4B` | Pushes a vector into the current frame. | confirmed (code) |
| `0x003a40b8` | `MsgFrame_PushFloat` | Pushes a float into the current frame. | confirmed (code) |
| `0x003a40f0` | `MsgFrame_PushInt` | Pushes an integer into the current frame. | confirmed (code) |
| `0x003a4128` | `MsgFrame_PushHandle` | Pushes a handle into the current frame. | confirmed (code) |
| `0x003a7da8` | `MsgStack_Init1` | Inits a message stack with 1 argument. | confirmed (code) |
| `0x003a7e10` | `MsgStack_Init2` | Inits a message stack with 2 arguments. | confirmed (code) |
| `0x003a7e98` | `MsgStack_Init3` | Inits a message stack with 3 arguments. | confirmed (code) |
| `0x003a7f40` | `MsgStack_Init4` | Inits a message stack with 4 arguments. | confirmed (code) |
| `0x003a8010` | `MsgStack_Init5` | Inits a message stack with 5 arguments. | confirmed (code) |
| `0x003a8100` | `MsgStack_Init6` | Inits a message stack with 6 arguments. | confirmed (code) |
| `0x003a8210` | `MsgStack_Init7` | Inits a message stack with 7 arguments. | confirmed (code) |
| `0x003a8340` | `MsgStack_Init8` | Inits a message stack with 8 arguments. | confirmed (code) |
| `0x003a84c8` | `MsgStack_PopVec4b` | Pops a vector. | confirmed (code) |
| `0x003a84e8` | `MsgStack_PopFloat` | Pops a float. | inferred |
| `0x003a8500` | `MsgStack_PopInt` | Pops an integer. | inferred |
| `0x003a8530` | `MsgStack_PopHandle` | Pops a handle. | inferred |
| `0x003a8548` | `MsgStack_PushVec4A` | Pushes a vector. | confirmed (code) |
| `0x003a8560` | `MsgStack_PushVec4B` | Pushes a vector. | confirmed (code) |
| `0x003a8578` | `MsgStack_PushVec4C` | Pushes a vector. | confirmed (code) |
| `0x003a8590` | `MsgStack_PushFloat` | Pushes a float. | confirmed (code) |
| `0x003a85a8` | `MsgStack_PushInt` | Pushes an integer. | confirmed (code) |
| `0x003a85c0` | `MsgStack_PushIntB` | Pushes an integer. | confirmed (code) |
| `0x003a85d8` | `MsgStack_PushHandle` | Pushes a handle. | confirmed (code) |
| `0x003a85f0` | `MsgStack_StaticInit` | Static init of the 6 message frames. | confirmed (code) |
| `0x003a8678` | `MsgStack_StaticInitThunk` | Thunk to MsgStack_StaticInit. | confirmed (code) |

### Tasks: The TaskManager {#code-tasks-task-manager}

See [Tasks: The TaskManager](#task-manager).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003a2cd8` | `TaskManager_GetParticleManager` | Returns the particle manager of a phase. | confirmed (code) |
| `0x003a3398` | `TaskManager_QueueMoved` | Adds a task to the moved list (+0x870). | confirmed (code) |

### Tasks: The base task object {#code-tasks-task-object}

See [Tasks: The base task object](#task-object).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00386f58` | `Task_EmptyStub0` | Empty stub that returns at once. | confirmed (code) |
| `0x00386f68` | `Task_EmptyStub1` | Empty stub that returns at once. | confirmed (code) |
| `0x00386f78` | `Task_EmptyStub2` | Empty stub that returns at once. | confirmed (code) |
| `0x00386f88` | `Task_EmptyStub3` | Empty stub that returns at once. | confirmed (code) |
| `0x00386f98` | `Task_EmptyStub4` | Empty stub that returns at once. | confirmed (code) |
| `0x00386fa0` | `Mat3_Multiply` | Multiplies two 3x3 rotation matrices. | confirmed (code) |
| `0x003871d0` | `Mat_TransformPoint` | Transforms a point by a matrix with w = 1. | confirmed (code) |
| `0x003872a0` | `Mat_InvertRigid` | Inverts a rotation-plus-translation matrix. | confirmed (code) |
| `0x003917c8` | `Object_SetAttachOffset` | Sets the object's attach offset. | confirmed (code) |
| `0x003917d8` | `Object_GetAttachOffset` | Returns the object's attach offset. | confirmed (code) |
| `0x00391818` | `Vec3_Set` | Sets a vector from three floats. | confirmed (code) |
| `0x00391868` | `Record_SetVec4At0c` | Stores a vector at record +0x0c. | confirmed (code) |
| `0x00394f20` | `Task_IsAttached` | Says whether the task's attached flag 0x10 is set. | confirmed (code) |
| `0x0039caf0` | `Task_DebugPrint` | Debug print of a task. | confirmed (code) |
| `0x0039cb70` | `Task_DebugPrint2` | Second debug print of a task. | confirmed (code) |
| `0x003a16b8` | `Task_SetTransform` | Sets the task's position and rotation. | confirmed (code) |
| `0x003a1730` | `Task_SetVelocity` | Sets the task's velocity. | confirmed (code) |
| `0x003a1790` | `Task_WakeIfMoving` | Wakes the task when it moves. | confirmed (code) |
| `0x003a18b8` | `Task_SetParent` | Sets the task's parent (+0x58). | confirmed (code) |
| `0x003a18c0` | `Task_GetParent` | Returns the task's parent. | confirmed (code) |
| `0x003a1940` | `Task_GetAttachPoint` | Returns the attach point bytes +0x6c/+0x6d. | confirmed (code) |
| `0x003a1988` | `Task_SetPose` | Sets the task's pose. | confirmed (code) |
| `0x003a1e30` | `Task_GetPose` | Returns the task's pose. | confirmed (code) |
| `0x003a1f20` | `Task_GetVec30` | Returns the vector at +0x30. | confirmed (code) |
| `0x003a1f28` | `Task_GetVec40` | Returns the vector at +0x40. | confirmed (code) |
| `0x003a1f30` | `Task_SetByte6c` | Sets the byte at +0x6c. | confirmed (code) |
| `0x003a2238` | `Task_ResetMotion` | Clears the task's motion vectors. | confirmed (code) |
| `0x003a2508` | `Task_Unknown2508` | Task helper; purpose not traced. | speculative |
| `0x003a25b0` | `Task_SetAttachPoint` | Sets the attach point. | confirmed (code) |
| `0x003a25b8` | `Object_AsTypeBit2` | Returns the task when its type bit 2 is set. | confirmed (code) |
| `0x003a3978` | `ScriptObj_GetVectorProperty` | Returns a vector property of a script object. | confirmed (code) |
| `0x003a3d70` | `ScriptObj_GetGlobalVector` | Returns a global vector by id. | confirmed (code) |
| `0x003a3e78` | `ScriptObj_GetGlobalInt` | Returns a global integer by id. | confirmed (code) |
| `0x003a3f00` | `ScriptObj_GetCamera` | Returns the camera. | confirmed (code) |
| `0x003a3f30` | `ScriptObj_GetHumanPoint` | Returns a point of a human. | confirmed (code) |
| `0x003a42d8` | `ScriptObj_GetPoseA` | Object pose getter; exact use not traced. | speculative |
| `0x003a4328` | `ScriptObj_GetViewPosition` | Returns the view position. | confirmed (code) |
| `0x003a43b8` | `ScriptObj_HasBodyFlag4` | Says whether the object's body has flag 4. | confirmed (code) |
| `0x003a4428` | `ScriptObj_GetPoseB` | Object pose getter; exact use not traced. | speculative |
| `0x003a4478` | `ScriptObj_GetPoseC` | Object pose getter; exact use not traced. | speculative |
| `0x003a45a0` | `ScriptObj_EmptyStub` | Empty stub. | confirmed (code) |
| `0x003a4608` | `ScriptObj_CameraCall1d4` | Calls camera method +0x1d4 (likely a shake). | inferred |
| `0x003a4668` | `ScriptObj_CameraCall1dc` | Calls camera method +0x1dc (likely a shake). | inferred |
| `0x003a4b88` | `ScriptObj_GetUserValue` | Returns a script object's user value. | confirmed (code) |
| `0x003a4d90` | `Vec_PickConstant` | Returns one of two constant vectors. | speculative |
| `0x003a50e0` | `Camera_IsPointInPlayerView` | Says whether a point is in a player's view. | confirmed (code) |
| `0x003a51f8` | `Cameras_IsPointVisibleAny` | Says whether any camera sees a point. | confirmed (code) |
| `0x003a5280` | `Cameras_IsWithinRange` | Says whether any camera is within a range of a point. | confirmed (code) |
| `0x003a53b8` | `ScriptObj_Unknown53b8` | Script-object helper; purpose not traced. | speculative |
| `0x003a58d8` | `Task_SetFieldC8` | Sets the task's field +0xc8. | confirmed (code) |
| `0x003a5a10` | `GameState_Call41b3d8` | Calls game-state function 0x0041b3d8. | confirmed (code) |
| `0x003a5ab8` | `AI_RemoveNoGoSphereWrapper` | Wrapper of the AI no-go sphere removal. | confirmed (code) |
| `0x003a65a8` | `Human_Call219320` | Calls human function 0x00219320. | confirmed (code) |

### Tasks: The wheel {#code-tasks-wheel}

See [Tasks: The wheel](#wheel).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003a33b0` | `TaskManager_InitWheel` | Inits the timing wheel. | confirmed (code) |
| `0x003a3430` | `TaskWheel_FindBucket` | Finds the wheel bucket for a time. | confirmed (code) |
| `0x003a36e0` | `TaskWheel_Unknown36e0` | Wheel helper; purpose not traced. | speculative |
| `0x003a45a8` | `ScriptObj_Unschedule` | Takes the task off the wheel. | confirmed (code) |
| `0x003a45d8` | `ScriptObj_Schedule` | Puts the task on the wheel. | confirmed (code) |

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0041b3d8` | `GameState_CallRadioCallback` | from `Radio_HandleMessage` (through `0x003a5a10`): calls the function named at `+0x360` with (object, value); the name's writer is not traced | confirmed (code) |
| `0x0041d968` | `Cfg_SetTimeScale` | `CfgSetTimeScale`: the game clock's `+0x50` ([Tasks](tasks.md#clocks)) | confirmed (code) |

## Open questions {#open-questions}

- The file split of `0x003a1570`-`0x003a4288` between `TaskManager.cpp` and a base task file.
- What the sub-managers' updates at `+0x844`-`+0x850` and `0x00390e20` do, and how the `SceneTask` relates to
  `WarMoveInstance`.
- What makes a light task, and the pool sizes of `AnimTask`, `SoundTask`, `ScriptObject` and the cameras.
- The anim events `0x3e`, `0x3f` and the one that sets `0x2000`; what state code 6 is.
- The event types `0`, `1`, `7` and `0x17` (`0x10` is the attack warning, [AI](ai.md#block)).
- Does `TaskManager_TickGame` cap the pairs it runs in one frame, and do millisecond timers lag the steps below 25 fps
  as inferred in [The play tick](#tick)?
- Resolved: the physics step `0x00340918` moves no human and decides no hit; it settles landed objects
  ([Physics](physics.md#tick-rate)).
