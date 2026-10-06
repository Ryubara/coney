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
| `0x00390180` | `Handle_Resolve` | handle → object | confirmed (code) |
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
| `+0x838`, `+0x83c` | two `ParticleTaskManager`s (sizes 0x578 and 100), one per phase (`0x003a2cd8` picks them) |
| `+0x840` | the `ObjectTaskManager` (0x67ec bytes) |
| `+0x844` | the car manager |
| `+0x848` / `+0x84c` / `+0x850` | the light, glass and scene managers |
| `+0x854`… | seven lists, flushed by `TaskManager_Reset` |

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
| `+0x54` | u32 | flags: `0x200`, `0x400` static or moved by its velocity as position; `0x1000`, `0x80000` rotation modes; `0x2000` on a wheel; `0x2000000` grounded; `0x4000000` airborne (gravity 15.68 m/s² in `Task_Integrate`); `0x80000000` integrated |
| `+0x5c` | ptr | the next object in its bucket |
| `+0x60` | s16 | its phase |
| `+0x62` | s16 | its bucket |
| `+0x64` | float | update interval, seconds |
| `+0x6e` | u16 | update interval, ticks; `0x100` means "run once" |

Vtable slots used by the engine (function word offsets): `+0x14` before the update, `+0x24` the class flags
(`0x800` forces wheel 0), `+0x44` event, `+0x4c` unschedule, `+0x94` velocity, `+0xcc` message, `+0x13c` **update**
(returns 1 to stay scheduled), `+0x144` integrate. The human's vtable is `0x0053f088` (`+0x13c` = `0x0023fea8`, its
state update, `+0x44` = `Human_OnEvent`, `+0xcc` = `Human_HandleMessage`).

**Handles.** Objects refer to each other by handle: a table at `0x006ebd38` of 0xb00 entries `{ptr, u16 serial}`
resolved by `Handle_Resolve`; a handle whose serial is 0 is looked up in the `ObjectTaskManager` instead
(`0x00398fe0`). The nil handle is `0x006ebd30`. Transforms live in a separate table at `0x00714b00` (32 bytes each,
indexed by the object's `+0x92`).

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
object manager (`0x003980c8`), the cars (`0x0038e590`), `0x00390e20`, the particles and the scenes.

### The wheel {#wheel}

`TaskWheel_RunBucket` (`0x003a3588`), each tick, for each object in the current bucket: call vtable `+0x14`; unlink
it (`+0x5c`, `+0x62` and flag `0x2000` cleared); call its update (vtable `+0x13c`). If the update returns 1, the
object is not flagged `0x2000` again, and its interval `+0x6e` is not `0x100`, re-insert it in bucket
`(current + interval) & 0xff`. An object with both bits of `0x1000200000000000` (the u64 at `+0x50`) gets its
interval adjusted by a ground ray (`0x003a1f40`). Then the bucket index advances by one (mod 256). So an object with
interval 1 updates every tick (60 Hz), one with interval 6 ten times a second. `TaskManager_Schedule` sets `+0x60`
to the phase; an object whose class flags have `0x800` always goes on wheel 0.

**Humans are not on the wheel.** Confirmed (runtime): a hook on the wheel's update call logged 2428 updates over 120
ticks in the street, from four vtables only (`0x00545660`, `0x005453a0`, `0x005458c8`, `0x00544c08`), never the
human's `0x0053f088`. A human's task head has flags `+0x54` = `0x42005001` (no `0x2000`), phase `+0x60` = 2, bucket
`+0x62` = -1 and interval `+0x6e` = 255, and no wheel bucket of the save state lists one.

### Humans_Update: the characters' step {#humans-update}

Called on every 60 Hz tick; its body runs only when `0x005104f4` is even, so **the characters step at 30 Hz**. Each
step behind a debug switch that is on in play. Confirmed (code) at `0x00249108`; this corrects the order given
before on [Characters](characters.md#update):

1. The three animation managers (`0x00170c88`, `0x00171d38`, `0x00184568`).
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
- `src/animation/anim_task.*` holds the held flags and the events that move them
  ([Animation](formats/animation.md#coneys-implementation)); the readers' masks are in `src/combat/` and
  `src/human/locomotion_gate.*` ([Combat](combat.md#coneys-implementation)).

## Runtime checks {#runtime-checks}

All five were run (2026-10-05) with the scenarios in `repo:research/traces/scenarios/` (`tick_split`,
`wheel_objects`, `held_attack`, `block_release`, `turn_on_spot`, `civ_fight`) and the call hooks in
`repo:research/traces/patches.toml`; the results are in the body above: [the tick split](#tick), [humans on the
wheel](#wheel), [held flags through an attack](#held-flags), [the block's 5](#locomotion-gate) and [brain then
dispatcher](#humans-update). None is left.

## Open questions {#open-questions}

- The file split of `0x003a1570`-`0x003a4288` between `TaskManager.cpp` and a base task file.
- What the sub-managers at `+0x844`-`+0x850` and `0x00390e20` update, and the `SceneTask` (`WarMoveInstance`).
- The anim events `0x3e`, `0x3f` and the one that sets `0x2000`; what state code 6 is.
- The event types `0`, `1`, `7` and `0x17` (`0x10` is the attack warning, [AI](ai.md#block)).
- Does `TaskManager_TickGame` cap the pairs it runs in one frame, and do millisecond timers lag the steps below 25 fps
  as inferred in [The play tick](#tick)?
- Resolved: the physics step `0x00340918` moves no human and decides no hit; it settles landed objects
  ([Physics](physics.md#tick-rate)).
