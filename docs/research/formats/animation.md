# Animation (character clips)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). The disc survey (2026-10-04)
read the NTSC-U disc's WAD with throwaway scripts outside the repository and reports counts only.

## Purpose

How a character's animations are stored and played: the clip's descriptor and keyframes, how keys are quantised
and interpolated, how a pose is built for the 32-bone skeleton, and how a character finds a clip by its anim id.
The game does not use RenderWare's HAnim animation format for characters; the clump's HAnim hierarchy only
supplies the skeleton ([Characters](../characters.md#files)). Every walk, run, attack and scene clip uses this format.

In one paragraph: a clip is two chunks inside a character data resource: a **descriptor** (chunk `0x02`, 80 bytes:
duration, root displacement, section sizes, a bone mask, a name) and its **keyframes** (chunk `0x00`): root velocity
keys, root translation keys, one rotation channel per bone in the mask, then events. A key is 8 bytes: a frame delta
and three signed 16-bit values. Rotations store a unit quaternion's `x, y, z` and rebuild `w`. Playback is 30 frames
a second; positions are lerped and rotations nlerped between keys, and two poses are blended with slerp.

## Original structure

The player code is in the unnamed stretch before `Animation/` (`0x00100200`-`0x00104630`, whose rodata holds the
skeleton's `bip_sw_*` bone names) and in `Animation/AnimationBlend.cpp` and `AnimationMgr.cpp`
([Source map](../source-map.md)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x001041f8` | `AnimCursor_Init` | points a cursor at a clip's channels | confirmed (code) |
| `0x00104110` | `AnimCursor_Step` | moves each channel to the key for the next frame | confirmed (code) |
| `0x001044a0` | `AnimCursor_Advance(dt)` | advances the time; returns the overshoot past the end | confirmed (code) |
| `0x00104ce0` | `AnimCursor_SamplePose` | the pose at the current time | confirmed (code) |
| `0x00105158` | `Pose_BlendPartial` | blends two poses, optionally for a subtree only | confirmed (code) |
| `0x00104a38` | `Anim_RateMultiplier` | playback rate from the clip's flags | confirmed (code) |
| `0x00101120` | `Skeleton_InitParents` | fills the parent table `0x00597200` | confirmed (code) |
| `0x00101dd8`, `0x00103e90` | event processing | the clip's events as time passes | confirmed (code) |
| `0x00101950` | `Anim_RootDisplacementLength` | the length of the descriptor's displacement | confirmed (code) |
| `0x00336bb8` / `0x00336bf8` / `0x00336a00` | vector lerp / quaternion nlerp / quaternion slerp | | confirmed (code) |
| `0x0010bb60` | `WarAnimInstance` (in a free list, `AnimationMgr.cpp`) | the playing state of one clip | confirmed (code) |
| `0x00175080` | `CharacterInstance_GetAnim(id)` | anim id → descriptor ([Characters](../characters.md#files)) | confirmed (code) |
| `0x001048a0` / `0x001054c8` / `0x00105510` | `AnimTask` free list: create / take / give back | 360 tasks of 0x48 bytes, list at `0x0050a898` | confirmed (code) |
| `0x00175610` | `CharacterInstance_RunQueuedTasks` | runs the instance's queued tasks once and empties the queue | confirmed (code) |
| `0x001754e8` / `0x001753d8` | `TaskStack_InsertBottom` / `TaskStack_Push` | put a task under / on top of the instance's [task stack](#task-stack) | confirmed (code) |
| `0x00175210` | `TaskStack_Top` | the top task, or the bottom one when the top is a fade | confirmed (code) |
| `0x00105570` / `0x001752c8` | refresh the playing anim id | top task's id into the human's record `+0x20` | confirmed (code) |
| `0x00106c80`, `0x00105678`, `0x00105990`, `0x0010a310` | task constructors: fade, clip, clip-then-next, gait blend | see [Animation tasks](#animation-tasks) | confirmed (code) |
| `0x002364a0` (binding `CfgAnimSpeeds`, `0x0035a510`) | sets the four playback rates | `0x00510260`-`0x0051026c` | confirmed (code) |
| `0x0023f238` | `Human_ApplyRootMotion` | the pose's root velocity and turn into the human's velocity and heading | confirmed (code) |

## Data

All values little-endian.

### Descriptor (chunk `0x02`, 80 bytes)

Confirmed (code) at the readers above unless stated.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | placeholder for the vtable (`0x0045de40` in the file; replaced on load) |
| `+0x04`, `+0x08` | f32 | the root's displacement in `x` and `y` over the whole clip (metres) |
| `+0x0c` | f32 | duration in seconds |
| `+0x10` | u16 | size in bytes of section A (root velocity) |
| `+0x12` | u16 | size in bytes of section B (root translation) |
| `+0x14` | u32 | size in bytes of section C (rotations) |
| `+0x18` | u16 | number of rotation channels; equals the number of bits set in the mask (0 mismatches on the disc) |
| `+0x1a` | u16 | number of events |
| `+0x1c` | u32 | pointer to the keyframes (set on load) |
| `+0x20`-`+0x24` | 5 bytes | bone mask: bit `b` (byte `b / 8`, bit `b % 8`) set when bone `b` (0-33) has a rotation channel |
| `+0x25` | char[30] | the clip's name, cut to fit |
| `+0x43` | u8 | reference count (runtime) |

### Keyframes (chunk `0x00`)

The sections follow each other with no header: **A**, **B**, **C**, then the **events** (`count × 24` bytes).
Confirmed (code) for the order and sizes.

- **A, root velocity**: a channel of keys whose `x, y, z` are the root's velocity in m/s. For Rembrandt's walk-start
  clip the `y` key is 1.048, which is the descriptor's displacement (0.349 m) over its duration (0.333 s); inferred
  from that match.
- **B, root translation** (the pelvis): a channel of positions. Its `z` is relative to the bone's bind height (the
  model's bone offset chunk `0x28`, entry `+4`) and added to it.
- **C, rotations**: one channel per bit set in the mask, in bone order.

**A key** (8 bytes):

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0` | u8 | frames since the previous key (0 for a channel's first key) |
| `+1` | u8 | not read by the sampler found |
| `+2`, `+4`, `+6` | s16 | `x`, `y`, `z` |

A channel starts with a key whose `+0` is 0 and runs until the next key whose `+0` is 0, which starts the next
channel. Confirmed (code) at `0x001041f8`, `0x00104110`; every clip on the disc parses with this rule.

**Decoding.** Positions: `x / 1023`, `y / 1023`, `z / 2047` (metres). Rotations: `x, y, z × 2⁻¹⁵` (the constant
`0x38000100` in the code), `w = sqrt(1 − x² − y² − z²)`, so `w` is never negative. Confirmed (code).

**Events** (24 bytes each): `+0` u16 frame, `+2` u16 type, `+6` u16, `+8`-`+0xc` three s16 (a position, scaled
`/1023`, `/1023`, `/2047`), `+0xe`-`+0x12` three s16 (a quaternion's `x, y, z`). Types 8, 9 and 10 use the position
and rotation (a transform); the others' fields are not traced. Confirmed (code) for the layout at `0x00101dd8`;
the type meanings are open. Survey: type 11 is the most common (4,287 events).

### The pose

A sampled pose is `0x240` bytes: two translations (the root velocity and the root translation) and 34 rotations, one
per bone 0-33 (bones without a channel keep the bind rotation). The player keeps a stack of seven. Confirmed (code)
at `0x00104ce0`.

**Parent table** (`0x00597200`, 34 entries, filled by `0x00101120`), bone → parent:

```text
-1, 0, 1, 1, 3, 4, 5, 6, 6, 8, 6, 10, 6, 12, 6, 6, 5, 16, 17, 18, 19, 19, 5, 22, 23, 24, 25, 25, 2, 28, 29, 2, 31, 32
```

Confirmed (code). Bone 0 is the root, 1 the pelvis; the clump's 32 HAnim bones and its 33 frames match bones 1-33
of this table with the root above them (inferred).

### The cursor (`WarAnimInstance`)

| Offset | Meaning |
| --- | --- |
| `+0x04` | the clip's descriptor |
| `+0x18` | time in seconds |
| `+0x1c` | current frame |
| `+0x1e` | `u16` flags: `0x1` no root velocity, `0x2` no root turn, `0x10` events muted (set from the [task flags](#animation-tasks)) |
| `+0x24`... | a pointer per channel to its current key |
| `+0xb4`... | a countdown byte per channel: frames left to the next key |
| `+0xd8`... | each channel's current key's start frame |

Confirmed (code) at `0x001041f8`, `0x00104110`.

### Where clips are found

- In a character data resource (resource type 5): pairs of `0x00` + `0x02` chunks, then the Anim Range List and the
  Character Data table that maps 722 anim ids to these clips ([Characters](../characters.md#files)).
- **Which clip an id gets**: Character Data slot `n` counts the clips of the resource from the **last** one loaded
  (the chunk system pops them last-in first-out), confirmed (runtime) on Rembrandt's walk (id 408). After loading the
  slots hold descriptor pointers, which `CharacterInstance_GetAnim` (`0x00175080`) returns.
- Dynamic animations (`SetDynamicAnimation`) loaded by name later; the **Anim List** in `warriors.glr` (chunk `0x4E`)
  has 564 records of `{hash, size}` after a count word (inferred: the list of loadable clips).

### The Anim Range List (chunk `0x45`) {#anim-range-list}

A count word, then one 16-byte record per anim id (722), next to the clips in each character data resource. The
character keeps it at Character Data `+0xb54` (`+0xb50` the count); the human's 0x180 record points at it from
`+0x160` (`+0x15c` the count), set by `0x00254568`. The readers, confirmed (code) at the cited addresses:

| Offset | Type | Meaning | Reader |
| --- | --- | --- | --- |
| `+0x00`, `+0x02` | s16 × 0.001 | `x`, `y` of a target offset in metres (where the other human should be for this move) | `0x00254418`; its angle `0x00254310` |
| `+0x04` | f32 | reach; 0 means the id has no range data | `0x002544a0` |
| `+0x08` | s16 × 0.001 | a far range; when 0, `+0x04` × 1.25 | `0x00254508`; setter `0x002545a8` |
| `+0x0a` | s16 | a value not named | `0x002542e8`; setter `0x002548c8` |
| `+0x0c` | s16 | a kind (10 by default; 0 maps to `0x26` in one state) | `0x00254d60` |
| `+0x0e` | u16 | flags choosing the playback rate: `0x800`, `0x1000`, `0x2000` or none ([rates](#playback-rate)) | `0x00104a38` |

The second word on the disc is 1000 in every record seen, which is consistent with `+0x02` being a scaled value of
1.0 m (inferred). The ranges are most likely for attacks and grabs (speculative);
the locomotion clips carry flag `0x1000` (confirmed (runtime) on Rembrandt). The class record (`CfgChar`'s 45 floats
and 16-bit values) overrides the range data for some ids, through the jump tables at `0x0055d640` and `0x0055d6f0`
(called from `Human_AttachInstance`, `0x00217a98`), confirmed (code).

## Behaviour

### Playing a clip

1. **Init** (`0x001041f8`): find each channel's first key in sections A, B and C and set up its countdown.
2. **Advance** (`0x001044a0`) by dt times the task's [playback rate](#playback-rate): the time grows, the frame is
   `time × 30`, and each passing frame steps the channels (`0x00104110`). Past the duration it returns the overshoot,
   so the caller can loop or chain the next clip without losing time.
3. **Sample** (`0x00104ce0`): for each channel, `t = (frame − key start) / next key's delta`; lerp positions
   (`0x00336bb8`) and nlerp rotations (`0x00336bf8`: flip the second quaternion when the dot product is negative,
   then normalise). A next delta of 0 (the channel's end) holds the last key.
4. **Events** (`0x00101dd8`, `0x00103e90`) fire as their frame is passed.

Confirmed (code) for the steps.

### Blending {#blending}

`Pose_BlendPartial` (`0x00105158`) mixes two poses with a weight: rotations by slerp (`0x00336a00`), translations by
lerp; with a bone given, only that bone's subtree (found through the parent table) is blended, so an upper-body clip
can play over legs that walk. Confirmed (code). Every blend in the game goes through it; what decides the poses and
the weights is the task system below. Which tasks the locomotion builds, and when, is on
[Characters](../characters.md#clip-selection).

### Animation tasks {#animation-tasks}

A character never plays a clip directly. It plays **tasks** (`AnimTask`, `Animation/AnimationBlend.cpp`): small
objects of 0x48 bytes taken from one free list of 360 (`0x001048a0` builds it, `0x001054c8` takes one, `0x00105510`
gives it back). Each task owns one or more cursors (`WarAnimInstance`, [above](#the-cursor-waraniminstance)) and
answers the same virtual calls. Confirmed (code) at the cited addresses unless marked.

**Common fields:**

| Offset | Meaning |
| --- | --- |
| `+0x00` | vtable (selects the type) |
| `+0x04` | the `CharacterInstance` it plays on |
| `+0x0c` | playback rate (multiplies dt) |
| `+0x10` | task flags (below) |
| `+0x14` | type number |
| `+0x18` | the first cursor |

**Virtual calls** (function word offset in the vtable): `+0x54` start (run once from the instance's queue,
`0x00175610`); `+0x5c` sample: push the task's pose on the pose stack; `+0x64` advance by dt; `+0x6c` current time and
`+0xd4` duration (seconds, inferred from their use in `0x00105ba0`); `+0x94` release the cursors; `+0xac` set a blend
target, `+0xb4` read the blend value, `+0xbc` set the blend value at once; `+0xcc` set the task flags; `+0x104` the
anim id the task reports (inferred from `0x001752c8`).

**Types** (by vtable; the constructors fill the fields):

| Type | Vtable | Constructor | What it plays |
| --- | --- | --- | --- |
| 1 | `0x005350d0` | `0x00105678` | one clip, looping; advancing past the end wraps and calls an optional end callback (`0x00105770`) |
| 3 | `0x00534fa8` | `0x00105990` | one clip, then a **next task**: during the clip's last *b* seconds (*b* = the blend time given, at most the next task's duration) the next task is advanced too and the two poses are blended by the share of *b* elapsed (`0x00105ba0`); at the clip's end the next task takes its place (`0x00105af0`) |
| 9 | `0x00534c30` | `0x00106c80` | a **cross-fade** over *d* seconds from the tasks below it ([Fades](#fades)) |
| 10 | `0x005349e0` | `0x0010b090` | two clips mixed by a weight |
| 11 | `0x00534b08` | `0x00106f50` | four clips, one audible at a time chosen by a value 0-3 (thresholds 0.51, 1.51, 2.51) |
| 12 | `0x00534790` | `0x0010a310` | the **gait blend**: five clips on a scale 0-4 ([Gait blend](#gait-blend)) |
| 13 | `0x005348b8` | `0x00109210` | an eight-direction blend: three neighbouring clips of eight (ids `base + (dir ± 1) & 7`) |
| 7 | `0x00534d58` | `0x001069c0` | a scene's clip (made by the scene code, `0x0039d870`) |
| 4, 6 | `0x00534668`, `0x00534540` | `0x00108450`, `0x00108a78` | as 5 and 3, with the clip taken from another human's character (paired moves; inferred from the handle they resolve) |
| 5, 15, 16 | `0x00534e80`, `0x005342f0`, `0x00534418` | `0x00106140` / `0x00106240`, `0x0010b608`, `0x0010b7c8` | not traced |

**Task flags** (`+0x10`, set through `+0xcc`, `0x001058f0`, `0x0010a1f0`):

| Flag | Meaning |
| --- | --- |
| `0x1` | no root velocity: sets cursor flag `0x1`, so section A is not sampled |
| `0x2` | no root turn: sets cursor flag `0x2`, so bone 0's rotation is not sampled |
| `0x10` | events muted: sets cursor flag `0x10`; the event code (`0x00101dd8`, `0x00103e90`) skips a cursor with it |
| `0x200` | gait blend: the blend value is kept continuous (without it a target is rounded to the nearest integer, `0x0010a500`) |
| `0x40`, `0x80` | set on the locomotion blends (`0x2c1` = `0x200` + `0x80` + `0x40` + `0x1`); `0x80` is what the locomotion tests to know that the base task is already a gait blend (inferred, `0x0025b200`) |

The flags the locomotion uses are `0x2c1` for the gait blend and `0xc1` for the paired idle blends, so **looping gaits
never move the character by their root**; start and stop clips have no flags and do.

### The task stack {#task-stack}

The `CharacterInstance` holds a stack of up to 18 task pointers at `+0x2bc` (count `u16` at `+0x2ac`) and a queue of
up to 10 at `+0x304` (count at `+0x2ae`). Confirmed (code):

- `0x001754e8` **inserts a task at the bottom** (index 0), `0x001753d8` **pushes one on top**. Both first finish the
  newest fade early when the stack already holds more than 6 tasks (`0x00177eb8` finds it; it is advanced by its
  remaining time), and both then refresh the human's playing anim id (record `+0x20`) from the top task
  (`0x001752c8`). `0x00105570`, run after each characters' step, refreshes it too.
- `0x00175210` returns the top task, or the bottom one when the top is a fade (type 9).
- The tasks are sampled from the bottom up onto the pose stack (at most 7 poses; `0x00104ce0` refuses an eighth), so
  a fade on top blends the two poses under it (inferred from the fade's sampler and the insert order).

A **change of animation** is always the same three moves (seen in every locomotion builder, `0x0025b200`,
`0x0025f1b8`, `0x0025c0e8`): make a fade task with the fade time, make the new task, insert the new task at the bottom
and push the fade on top. The stack is then *new, old…, fade*.

### Fades {#fades}

A fade (type 9, `0x00106c80`: duration *d* at `+0x18`, elapsed `+0x1c`) blends the outgoing pose into the incoming one
with the weight

```text
w_old = (1 + cos(π · t / d)) / 2      (1 at the start, 0 at the end; d = 0 gives 0 at once)
```

(`0x00106e50` computes `1 − (sin(π · t/d − π/2) + 1) / 2`, which is the same). The outgoing tasks keep advancing during
the fade. When *t* passes *d*, or the stack holds more than 12 tasks, the fade gives back itself and every task below
the new one (`0x00106cc8`), and on release clears the human flags it was given (`+0x24`, `0x00106ee8`). Confirmed
(code) for the curve and the clean-up; that `w` weighs the outgoing pose is inferred (the other way round the change
would pop at the start). At runtime a fade of 0.15 s ran 0, 0.033, 0.100 s and was gone at the next sample
(PCSX2 2.9.94, the release in [Characters](../characters.md#clip-selection)).

### Gait blend {#gait-blend}

Type 12 (`0x0010a310`; methods `0x0010a5b8` advance, `0x0010adf8` sample, `0x0010a500` set target, `0x0010a558` set
value) plays one of five clips, or a mix of two neighbours, along a **blend value** *v* from 0 to 4. The locomotion gives
it the human's anim slots 4, 5, 6, 7 and 7 (walk, jog, run, sprint, sprint). Confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x18` / `+0x1c` | the lower / upper cursor |
| `+0x20`-`+0x30` | the five anim ids |
| `+0x34` | target value, clamped to 0-4 (rounded unless flag `0x200`) |
| `+0x38` | current value *v* |
| `+0x3c` | the speed at which *v* moves, units per second (10 for the locomotion) |
| `+0x40` / `+0x44` | a handle and a callback called each time the leading clip wraps |

Each advance:

1. *v* moves toward the target by at most `rate × dt × 10` (a third of a unit per 30 Hz update).
2. The pair is clip `floor(v)` and clip `floor(v) + 1` (the floor uses 0.995, 1.995, 2.995 as its steps). When the pair
   changes, the clip that comes in starts at the **same normalised time** (time / duration) as the one it joins.
3. One cursor **leads**: it advances by `rate × dt` and the other is set to the same normalised time, so the two feet
   stay in step. The lower clip leads, except when *v* is falling (above the target) or the fraction `v − floor(v)` is
   over 0.875, when the upper one leads. The leading cursor's events fire, the other's are muted (flag `0x10`).
4. Sampling: fraction below 0.01, the lower clip alone; above 0.995, the upper alone; otherwise both, blended with the
   fraction as the upper clip's weight (`0x001054a8` → `Pose_BlendPartial`).

So the playback rate of a gait is **not scaled by the speed**: a walk cycle lasts its clip's 1.167 s whatever the
speed, and between gaits the cycle length is that of the leading clip. Confirmed (runtime, PCSX2 2.9.94): the walk
clip's time grew by 0.033 s per update at 1.63 m/s, and the run's by 0.033 s at 7.80 m/s.

### Playback rate {#playback-rate}

A task's rate (`+0x0c`) is set by its constructor:

- **Single clips** (types 1, 3, 5, 15) take it from the clip's [Anim Range List](#anim-range-list) flags through
  `0x00104a38`: flag `0x800`, `0x1000` or `0x2000` picks `0x00510264`, `0x00510268` or `0x0051026c`, no flag picks
  `0x00510260`. The four values are set by the Lua binding **`CfgAnimSpeeds(default, f800, f1000, f2000)`**
  (`0x0035a510` → `0x002364a0`); `.data` holds 1.0, 0.85, 1.0, 1.0 before it runs. At runtime they are **0.75, 0.8,
  1.0 and 0.9** (PCSX2 2.9.94, `level99`). So the idle, the start clips and the stop clip (no flag) play at **0.75**:
  the walk start's 0.333 s lasts 0.444 s, which is the "about 0.45 s" measured before. Confirmed (code), values
  confirmed (runtime).
- **Locomotion blends** (types 9, 10, 12, 13) use the human's speed multiplier `+0x3a4` (1.0 in play).

The human's walking speeds (`0x00254078`) divide by the same rate, and the gait loops carry flag `0x1000` (rate 1.0),
so the speeds on [Characters](../characters.md#speed-classes) stand.

### Root motion {#root-motion}

The sampler (`0x00104ce0`) puts section A's root velocity, **times the task's rate**, in the pose's first
translation, unless the cursor has flag `0x1` or the clip has no section A (then it keeps the instance's default and
marks the pose "none"). Bone 0's rotation channel (mask bit 0) is the root's **turn per frame**, likewise unless flag
`0x2`. Both blend like any other channel. When the instance builds its bone matrices (`0x00104630`) it keeps the
blended root velocity at instance `+0x00` with a flag at `+0x461`, and the turn with a flag at `+0x462`. Confirmed
(code).

The human's state update (`0x0023fea8`) then calls `0x0023f238`, before the move:

- **Velocity:** the root velocity, turned by the human's rotation into world axes and capped at 50 m/s, is **added**
  to the velocity the state function set.
- **Turn:** with the turn's angle *a* = `2 · acos(w)`, the human turns about `z` (the sign from the quaternion's `z`) by
  `a × 30 × dt` per update, so the channel holds a turn per 1/30 s.

Confirmed (code). The locomotion sets no horizontal velocity of its own while a start or stop clip plays (human flags
`0x10000000` or `0x80000`, mask `0x110c0880` tested at `0x00241a88`, or move state 5 or 6), so the clip alone moves the
character then. At runtime the walk start moved Rembrandt at a steady 0.76 m/s, and section A of that clip is about
1.05 m/s × 0.75 ≈ 0.79 m/s (confirmed (runtime) for the speed; the match is inferred).

**Disc survey (counts only):** 31,274 clip occurrences in the WAD, 1,875 distinct; all parse with the channel rule;
the channel count equals the mask's bit count in all; at most 606 keys in a channel; clips run 1 to 680 frames; bone
33 is the highest animated bone (in 1,854 clips).

## Coney's implementation

`src/animation/` holds the format and the player, pure and deterministic: `anim_clip.*` parses a descriptor and its
keyframes into channels and pairs them as the chunk system loads them; `anim_pose.*` samples a clip into a pose
(search per channel, lerp or nlerp, the last key held), blends poses and steps a cursor; `skeleton.*` holds the
parent table and turns a pose into bone transforms; `anim_math.*` the vectors, quaternions and matrices. The
characters' skinning is on [Characters](../characters.md#coneys-implementation).

`anim_task.*` is the task system the human plays through ([Animation tasks](#animation-tasks)): the looping clip
(type 1), the clip that hands over to a next task (type 3), the gait blend (type 12: five clips on a 0-4 scale, the
value easing at 10 units/s, the pair stepped at 0.995, 1.995 and 2.995, the leading clip by the falling direction or a
fraction above 0.875, a shared normalised phase), and the stack with its cosine fades. Root motion is taken from the
pose of the newest task and switched off by its flags (`0x1` velocity, `0x2` turn; the locomotion tasks carry
`0x2c1`). Rates come from `CfgAnimSpeeds` by the Anim Range List's flags at `+0x0e` (`src/characters/anim_set.*`).

**Disc test** (`[anim]`, counts only): 5,127 resources, 31,274 occurrences, 1,875 distinct clips, none failing,
unpaired or sampling to a non-finite pose; 957 clips have no section A, none lacks B; 9,468 events.

**Coney choices**: "entry `+4`" of the bone offset chunk is read literally, the float at byte 4, which is 0 on the
disc; bones without a channel keep the model's bind rotation; the character viewer plays clips in place at rate 1
with no root motion; a looping clip carries its overshoot into the next pass. In the task system:

- **The stack** is kept as a list of layers, newest first; when a fade completes, everything older than it goes. A
  change finishes the newest fade at once when the stack holds more than 6 tasks, and drops the oldest above 12.
- **The clip that hands over** supports only a blend time of 0 (all the locomotion uses) and carries its overshoot into
  the next task.

## Notes for implementers

- Decode a clip into channels of `(start frame, value)` keys at load; sampling is then a search (or a cursor) per
  channel and one lerp or nlerp. Keep the hold rule for a channel's last key.
- Rebuild `w` with a non-negative square root and nlerp with the sign flip, or the joints will flip.
- **Build the player on a small task stack**, not on "one clip at a time": a looping clip, a clip that hands over to
  a next task, a cross-fade, and the gait blend cover all of `level99`'s locomotion
  ([Animation tasks](#animation-tasks)). A change of animation is "fade over *d*, new task under it"; the fade's weight
  follows `(1 + cos(π t / d)) / 2`.
- **The gait blend is one task**: five clips on a 0-4 scale, the value easing at 10 units/s toward a target the
  locomotion computes from the speed, neighbours phase-locked by normalised time, only the leading clip's events
  firing ([Gait blend](#gait-blend)).
- **Rates:** single clips play at the `CfgAnimSpeeds` rate chosen by their range flags (0.75 with no flag, 0.8, 1.0,
  0.9 at runtime); the gait loops at 1.0. Nothing scales a clip's rate with the character's speed.
- **Root motion only where the clip is meant to move the body:** start and stop clips add their root velocity (times
  the rate, turned into world axes) to the human's velocity, and their root turn to its heading; the gait loops and the
  idle have it switched off by the task flags ([Root motion](#root-motion)).
- The root's motion is also data for the speeds: the descriptor's displacement over the duration is the speed of a
  looping gait (the game derives each character's walk, jog, run and sprint speeds from it,
  [Characters](../characters.md#speed-classes)).
- A synthetic test fixture is easy to make: one clip with two channels and a few keys per channel, checking the
  channel split, the scales and the interpolation; no game data is needed. The gait blend's leader rule and the fade
  curve can be tested with two synthetic clips of different lengths.
- Bones are 0-33 in the clip and the pose, 32 in the HAnim hierarchy: map them once through the parent table.

## Open questions

- The meaning of byte `+1` of a key (not looked at in this pass).
- The Anim Range List's `+0x0a` and `+0x0c`, and the class record overrides by id.
- The event types (11 is the most common) and what they trigger (footsteps, sounds, hit windows; speculative).
- How section A drives the character during a start clip (confirm by comparing a clip's keys with the speeds
  sampled at runtime).
- The exact HAnim bone ↔ pose bone mapping (inferred above; Coney's disc test supports `n + 2`,
  [Character geometry](../characters.md#character-geometry)).
- Task types 5, 15 and 16, and the paired types 4 and 6 (used by grabs and two-human moves, not by locomotion).
- The order in which the instance samples its task stack (bottom-up is inferred from the fade's sampler).
- The type 3 task's blend into its next task when its blend time is not 0.
