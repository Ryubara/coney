# Tasks (the TaskEngine and when a move hands control back)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
only (2026-10-05); every claim is confirmed (code) at the cited address unless it says otherwise. Runtime checks this
page needs are listed at the end ([Runtime checks wanted](#runtime-checks)).

## Purpose

What the game calls a *task*, how tasks are scheduled and updated, in what order a frame of play runs them, and the
one mechanism that decides when a human's move is over: the **flags an animation task holds on its human**. It is
the page an implementer needs to replace Coney's per-symptom timing guards with the original's machinery, and the
frame the [AI humans](ai.md) run in.

In one paragraph: the `TaskEngine` is the game's object scheduler. A singleton `TaskManager` owns two **timing
wheels** of 256 buckets (one per phase: play and pause), a set of sub-managers (objects, particles, lights, glass,
scenes, cars) and the clocks. Every world object is a *task*: an object with a position, a rotation, velocities, a
flags word and a vtable whose slot `+0x13c` is its update. The wheel runs one bucket per 60 Hz tick and re-inserts
each object at its update interval. The **characters** are not stepped by the wheel but by `Humans_Update`, which
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
| `0x003a2ea0` | `TaskManager_TickGame` | play: up to two 60 Hz ticks of `Humans_Update`, the wheel bucket and physics | confirmed (code) |
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

1. Read the bus clock. **While more than `0x95ffff` ticks** (two 60 Hz ticks) have passed since the last step, run
   **two** 60 Hz ticks; otherwise one.
2. Each 60 Hz tick: set the phase's time, call **`Humans_Update`** (`0x00249108`), run the wheel's current bucket
   (`TaskWheel_RunBucket`), then the physics step (`0x00340918`).
3. After the ticks: the game state (`0x0041a370`) and the object spawns (`ObjectTaskManager_UpdateSpawns`, objects
   within 70 m of a player, 4900 = 70²).

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
   one step in five and updates every step. Brains write their human's **command** into its per-player record
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
update; the player's pad command reaches the record in step 3.

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
it. The block's **5 updates** after R1's release are the state code `+0x14` = 5 seen at runtime (the idle 388 with
`+0x14` = 5): the gate holds the stick while it lasts. Who sets and clears 5 after a block is not traced.

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
  code 5 or 6).
- **Objects**: a scheduler with an update interval per object is enough until the world objects (doors, pick-ups,
  cars) come; the wheel's exact bucket order matters only for runtime diffs of objects other than humans.
- **Messages are synchronous**: a hit's warning (`0xa4`, `0xa6`) is handled inside the attacker's event, before the
  sender's next line ([Combat](combat.md#block)).

## Runtime checks wanted {#runtime-checks}

For the runtime-traces harness. Addresses are of `SLUS_212.15`; "the record" is the human's 0x180 record (human
`+0xd4`).

1. **The tick split.** In the street (slot 1 copy), no input, sample `0x005104f4`, `0x0050b734` and Rembrandt's
   position every frame for 120 frames. Expect `Humans_Update`'s body on alternate 60 Hz ticks and two ticks on a
   frame that ran late.
2. **Held flags through an attack.** Square once, the stick at 60 % up from k = 0 to k = 40. Each update sample
   record `+0x08`, `+0x14`, `+0x20`, the top animation task's `+0x24` (the instance's stack `+0x2bc`, count `+0x2ac`)
   and the velocity. Expect the task's `+0x24` and record `+0x08` to agree, `0x40000` cleared on the update the clip
   ends, and the velocity to come back that update.
3. **The block's 5.** Hold R1 for 30 updates, release, the stick at 60 % right from the release. Sample record
   `+0x14`, `+0x08` and the state word each update: who sets 5 (a watch on `+0x14`) and on which update it is
   cleared.
4. **Humans on the wheel?** Break on `TaskWheel_RunBucket`'s update call (`0x003a3588`) and log the vtables it calls
   for 60 ticks in the street: whether any human (vtable `0x0053f088`) is on the wheel as well as in `Humans_Update`.
5. **Brain then dispatcher.** With a civilian fighting the player (slot 6 copy, the puppet set to fight), watch the
   civilian's per-player `+0x20` (`0x00660f50 + i × 0x2c + 0x20`): written by `0x00147ef0` from the brain's step and
   read by `0x00147ef8` in the dispatcher in the same update.

## Open questions {#open-questions}

- Whether humans are also scheduled on a wheel (check 4); their `+0x6e` was not read.
- The file split of `0x003a1570`-`0x003a4288` between `TaskManager.cpp` and a base task file.
- What the sub-managers at `+0x844`-`+0x850` and `0x00390e20` update, and the `SceneTask` (`WarMoveInstance`).
- The anim events `0x3e`, `0x3f` and the one that sets `0x2000`; who sets and clears state code 5 after a block, and
  what code 6 is.
- The event types `0`, `1`, `7`, `0x10` and `0x17`.
