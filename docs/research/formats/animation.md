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
([Source map](../source-map.md)); every function of the stretch is listed on [Animation system: code](../animation.md).
Names are ours.

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
| `0x00100200` | `Pose_InitReference` | writes the 34-rotation [reference pose](#reference-pose) that fills bones a clip has no channel for; 2,172 bytes (to `0x00100a7b`, one `jr ra`), then a 4-byte alignment `nop` before `0x00100a80` | confirmed (code) |
| `0x00104630` | `Instance_BuildBoneMatrices` | the pose into model-space bone transforms, from the pelvis down ([Bone transforms](#bone-transforms)) | confirmed (code) |
| `0x00176d60` | `CharacterInstance_Sample` | samples the task stack and calls `0x00104630`; picks the default pose | confirmed (code) |
| `0x00108450` / `0x00108a78` | `PairedTask_Init` (type 4 / type 6) | a clip of **another** human's character played on this one ([Paired tasks](#paired-tasks)) | confirmed (code) |
| `0x00108bd0`, `0x00108c80`, `0x00108ba8`, `0x00108dd0`, `0x00108f10` | type 6 methods: advance and hand over, sample, advance only, cut off, end | as type 3's | confirmed (code) |

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
| `+0x44` | u32 | clip flags: paired, tackle, grab, capsule strike and others ([Animation system](../animation.md#clip-flags)) |

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

**Events** (24 bytes each): `+0` u16 frame, `+2` u16 type, `+4` and `+6` u16 arguments (types 15, 16 and 19 pass
them), `+8`-`+0xc` three s16 (a position, scaled `/1023`, `/1023`, `/2047`), `+0xe`-`+0x12` three s16 (a quaternion's
`x, y, z`). Types 8, 9 and 10 use the position and rotation (a transform). Types 11 and 12 instead pass the **32-bit
word at `+8`** as their message's value (`lw a1,0x8(s1)` at `0x0010246c` for type 11, `0x00102498` for type 12): for
type 11 the animation sound id, so `+0xa`-`+0xb` must be 0 for the id to be used as such (on the disc `+4` and `+6`
are 0 in these events). Confirmed (code) for the layout at `0x00101dd8` (the cursor steps `0x18` at `0x00103df8`);
the type meanings are open, except type 8, the **partner's place** in a paired clip ([Paired tasks](#paired-tasks)),
read by `0x00101558`, types 9 and 10, effects, and type `0x21`, a **named effect**: `+0x06` the bone, the position
and rotation as above, and `+0x14` a three-letter code (`bld`, `pch`, `blo`, `dus` ...) that `0x00101dd8` makes
into the type name `sub_` + code for `Human_SpawnAnimEffect` (`0x0021de48`, [Combat: hit blood](../combat.md#hit-blood)).
Survey: type 11 is the most common (4,287 events); 344 distinct type-`0x21` events, 122 of them `bld`.

**Event types documented elsewhere.** `Anim_FireFrameEvents` (`0x00101dd8`) compares the type against a chain of
constants and handles each in place; these pages say what a type does and what it reads from the event:

| Type | Meaning | Reads | Page |
| --- | --- | --- | --- |
| `0x20` | camera shake on a player's current camera (the power camera during a power move) | `+0x04` as a 32-bit word, the level (`lw a2,0x4(s1)` at `0x00103540`) | [Camera: shake](../camera.md#shake) |
| `0x2e` / `0x2f` | slow motion on / off (a player's clip) | nothing | [Camera: slow motion](../camera.md#slow-motion) |
| `0x39` | **power camera: begin** | `+0x06` s16 mode (1 one shot, else 2 alternating) | [Power-camera events](../camera.md#power-events) |
| `0x3b` | **power camera: add a shot** | `+0x04` s16 (must be 0), `+0x06` s16 slot 0-7, `+0x08` position, `+0x0e` quaternion | [Power-camera events](../camera.md#power-events) |
| `0x3a` | **power camera: cut** | `+0x08` as a 32-bit float, the field of view (−1.0 on the disc: 50°) | [Power-camera events](../camera.md#power-events) |
| `0x3d` | **power camera: end** | nothing | [Power-camera events](../camera.md#power-events) |

Confirmed (code) at the cited handlers (`0x00102ac8`, `0x00102fbc`, `0x00102ea0`, `0x001032a8` for the four power
camera types); the shot's position and quaternion use the scaling above, in the frame of the human the camera
follows (x right, y forward, z up from the feet).

### The pose

A sampled pose is `0x240` bytes: two translations (the root velocity and the root translation) and 34 rotations, one
per bone 0-33. The player keeps a stack of seven. Confirmed (code) at `0x00104ce0`.

**Bones without a channel** do not keep the model's bind rotation: the sampler copies them from the instance's
**default pose** (`CharacterInstance +0x70`), which is the game's fixed [reference pose](#reference-pose) at
`0x00598420` (confirmed (code) at `0x00104ce0`, the pointer set at `0x00174d00` and `0x00176d60`). The same holds for
a clip without section B (the root translation then comes from the instance, `+0x90`), and bone 0 without a channel
is the identity. Each value taken from the default is **tagged** by setting the lowest bit of its `y` word (`| 1` on
the second 32-bit float); sampled values have the bit cleared. In one case the default is another pose: when the
instance has queued tasks (count at `+0x2ae`) and a human test (`0x00223440`) passes, `0x00176d60` samples the queued
tasks first into a second buffer (`0x005fd040`, initialised with the same reference pose) and uses that as the default
for the frame (confirmed (code); what the test means is not traced).

**Blending uses the tag** (`Pose_BlendPartial`, `0x00105158`, confirmed (code)): for bones 1-33 two tagged
rotations stay as they are (the result keeps the lower pose's), and otherwise the two are slerped, even when one of
them is a default; for the root velocity and bone 0 (the motion channels) a tagged side is ignored and the other side
is copied, so a clip without root motion does not damp the motion of the one it blends with.

**Parent table** (34 entries, the 0x88-byte `AnimationSystem` object that `0x00597200` points to, filled by
`0x00101120`), bone → parent:

```text
-1, 0, 1, 1, 3, 4, 5, 6, 6, 8, 6, 10, 6, 12, 6, 6, 5, 16, 17, 18, 19, 19, 5, 22, 23, 24, 25, 25, 2, 28, 29, 2, 31, 32
```

Confirmed (code). Bone 0 is the root, 1 the pelvis; the clump's 32 HAnim bones and its 33 frames match bones 1-33
of this table with the root above them (inferred).

#### Bone transforms {#bone-transforms}

`Instance_BuildBoneMatrices` (`0x00104630`) turns the pose into one `{position, rotation}` per bone, in the
character's model space (confirmed (code)):

- **Entry 0 is motion, not a bone**: it holds the root velocity and bone 0's rotation (the turn per 1/30 s), or the
  identity when the pose's value is tagged as a default (flags at instance `+0x461` / `+0x462`). Nothing is parented
  to it.
- **Entry 1, the pelvis, is absolute**: its position is the pose's root translation (section B, with the bind height
  added by the sampler) and its rotation is bone 1's rotation **as is**, not composed with bone 0's.
- **Bones 2-33** are composed down the parent table: rotation = parent rotation × local rotation, position = parent
  position + parent rotation · the bone's offset (from the character data's offset table).

So bone 0's channel moves and turns the human ([Root motion](#root-motion)) but never tilts the drawn body.

#### The reference pose {#reference-pose}

`Pose_InitReference` (`0x00100200`) writes the same 34 rotations into `0x00598420` and `0x005fd040` at start-up
(`0x001048a0`). They are constants of the code, `(x, y, z, w)` per pose bone, rounded here to four places
(confirmed (code); every one is a unit quaternion):

| Bone | x | y | z | w | Bone | x | y | z | w |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 0 | 0 | 0 | 1 | 17 | 0.0452 | -0.0282 | 0.1112 | 0.9924 |
| 1 | -0.4912 | -0.5087 | 0.4912 | 0.5087 | 18 | 0 | 0.1174 | 0 | 0.9931 |
| 2 | -0.5000 | 0.5000 | -0.5000 | 0.5000 | 19 | -0.6951 | -0.0567 | 0.0262 | 0.7162 |
| 3 | 0.4793 | -0.5199 | 0.5199 | -0.4793 | 20 | -0.0004 | -0.2858 | 0.0001 | 0.9583 |
| 4 | 0 | -0.0298 | 0 | 0.9996 | 21 | -0.0811 | -0.3223 | -0.0030 | 0.9431 |
| 5 | 0 | -0.1757 | 0 | 0.9844 | 22 | -0.6200 | -0.7643 | 0.1281 | 0.1227 |
| 6 | 0 | 0.1172 | 0 | 0.9931 | 23 | -0.0452 | -0.0282 | -0.1112 | 0.9924 |
| 7 | 0.5274 | -0.4710 | 0.4710 | 0.5274 | 24 | 0 | 0.1174 | 0 | 0.9931 |
| 8 | 0.6590 | 0 | 0.7521 | 0 | 25 | 0.6951 | -0.0567 | -0.0262 | 0.7162 |
| 9 | 0 | -0.2983 | 0 | 0.9545 | 26 | 0.0004 | -0.2858 | -0.0001 | 0.9583 |
| 10 | 0.6081 | -0.5780 | 0.3635 | 0.4050 | 27 | 0.0811 | -0.3223 | 0.0030 | 0.9431 |
| 11 | 0 | 0.6425 | 0 | 0.7663 | 28 | -0.0066 | -0.1154 | 0.9910 | -0.0678 |
| 12 | 0.4043 | -0.3602 | 0.5801 | 0.6085 | 29 | 0 | 0.0340 | 0 | 0.9994 |
| 13 | -0.0036 | -0.6166 | 0.0054 | 0.7873 | 30 | -0.0030 | -0.0194 | -0.0700 | 0.9974 |
| 14 | 0.7393 | 0 | 0.6733 | 0 | 31 | -0.0066 | 0.1154 | 0.9910 | 0.0678 |
| 15 | 0.5247 | -0.4805 | 0.4743 | 0.5186 | 32 | 0 | 0.0340 | 0 | 0.9994 |
| 16 | 0.6200 | -0.7643 | -0.1281 | 0.1227 | 33 | 0.0030 | -0.0194 | 0.0700 | 0.9974 |

**Why it matters: many clips leave bone 2 out.** Bone 2 is the parent of the spine (3) and of both legs (28, 31).
Every clip that animates it on the disc holds the constant `(-0.5, 0.5, -0.5, 0.5)` there, the reference pose's
value, so the animators left it out of many clips (disc check of Rembrandt's set, ids only): the grab clips 69-75
and 82-85, the grab strikes 51-56, the spins 78-81, the tackle 4-6, the grounded idle 196 and the let-go 94 / 95 have
no bone 2 channel, where the attacks (11, 12), the fight idle 358, the block 606 and the power strike 57 / 58 have
one. A
player that fills a missing bone with anything but this value turns the whole body below the pelvis; filling it from
the model's bind rotation is the likely cause of the grab clips posing "rolled on the side" in Coney (inferred: only
clips without bone 2 show it; confirmed by Coney's disc check, [below](#coneys-implementation)). Bones 7-15 (the
fingers) are also left out by most clips, bone 0 by most clips that do not turn.

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
| `+0x00`, `+0x02` | s16 × 0.001 | `x`, `y` of a unit direction (x right, y forward) towards the other human for this move: (1, 0) for a snap right, (-1, 0) left, (0, -1) back | `0x00254418`; its angle `0x00254310` |
| `+0x04` | f32 | reach; 0 means the id has no range data | `0x002544a0` |
| `+0x08` | s16 × 0.001 | a far range; when 0, `+0x04` × 1.25 | `0x00254508`; setter `0x002545a8` |
| `+0x0a` | s16 | the move's **damage** ([Combat](../combat.md#damage-table)) | `0x002542e8`; setter `0x002548c8` |
| `+0x0c` | s16 | the **hit code**: bits 0-1 direction, 2-3 height, 4-5 strength; 10 by default; picks the victim's reaction ([Combat](../combat.md#hit-codes)) | `0x00254d60`; `0x00266d00` |
| `+0x0e` | u16 | flags: `0x400` the hit **stuns** (`0x0026a6d0`); `0x800`, `0x1000`, `0x2000` choose the playback rate ([rates](#playback-rate)); `0x100` seen on escapes and throws, not traced | `0x00104a38` |

At runtime (Rembrandt-class player, PCSX2 2.9.94) `+0x00`, `+0x02` read as a unit vector × 1000: (0, 1000) for most
ids, (999, -12) for 25 `SNAP_RIGHT_01`, (-1000, 0) for 27, (-39, -999) for 29, (351, 936) for 147; so a direction
rather than an offset (inferred from the values). `+0x0a` is the damage: every hit tested took exactly this value off
the target's health (confirmed (runtime), [Combat](../combat.md#damage-table)). The ranges serve the attacks, grabs
and tackles: the grab searches within id 70's far range × 1.25, the tackle id 3's (confirmed (code) at `0x00284920`);
the locomotion clips carry flag `0x1000` (confirmed (runtime) on Rembrandt). The class record (`CfgChar`'s 45 floats
and 16-bit values) overrides the range data for some ids, through the jump tables at `0x0055d640` and `0x0055d6f0`
(called from `Human_AttachInstance`, `0x00217a98`), confirmed (code).

## Behaviour

### Playing a clip

1. **Init** (`0x001041f8`): find each channel's first key in sections A, B and C and set up its countdown.
2. **Advance** (`0x001044a0`) by dt times the task's [playback rate](#playback-rate): the time grows, the frame is
   `time × 30`, and each passing frame steps the channels (`0x00104110`). A step that would reach the clip's frame
   count is not taken: it returns an overshoot instead, which ends or wraps the clip ([When a clip ends](#clip-end)).
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
| 6 | `0x00534540` | `0x00108a78` | a **paired** type 3: the clip and its rate come from **another human's** character ([Paired tasks](#paired-tasks)) |
| 4 | `0x00534668` | `0x00108450` | a paired type 5: the same constructor as 5's first form with the clip from another human's character |
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

**Fields of the clip tasks** (types 1, 3, 6), confirmed (code) at the constructors and at `0x00108dd0` /
`0x00108f10`: `+0x1c` the next task (3, 6), `+0x20` the blend time into it, `+0x24` **state flags** the task holds on
its human (the combat state word at record `+0x08`, [Combat](../combat.md#state-flags)): the caller sets them on the
human as it pushes the task, and the task clears them from record `+0x08` when it ends or is cut off; `+0x28` the
handle of the human the task plays on; `+0x2c` a callback called with that human when the clip ends (the hook the
combat code chains on, [Combat](../combat.md#grab-posing)).

### Paired tasks {#paired-tasks}

Two humans in one move (a grab, a grab strike, a throw) each play their own clip on their own task stack; nothing
couples the two stacks while they play. What makes a task "paired" is only **where its clip comes from**
(confirmed (code) at `0x00108a78`, `0x00108450`):

- The constructor takes, besides the instance it plays on, the **handle of another human** (the attacker) and an
  anim id. It resolves the handle and looks the id up in **that human's** character (`0x00175080` on its
  `CharacterInstance`, human `+0xd8`), and takes the [playback rate](#playback-rate) from **that human's** Anim Range
  List flags (`0x00104a38`). The cursor is then bound to the instance it plays on, as for any clip.
- So the victim of a grab plays the reaction clip **of the grabber's animation set**: a grab by a Rembrandt-class
  human plays the Rembrandt reaction 73 on a civilian, whatever the civilian's own set holds for 73. The pair's two
  clips were authored together and play at the attacker's rate, so they stay in step.
- Type 6 is otherwise type 3: it advances by `rate × dt`, blends into its next task over the given time and hands over
  at the clip's end, runs the end callback and clears its state flags (`0x00108bd0`, `0x00108c80`, `0x00108ba8` sit in
  the vtable slots of type 3's `0x00105af0`, `0x00105ba0`, `0x00105ab0` and do the same steps). Type 4 takes the same
  arguments as type 5's first constructor (`0x00106140`) plus the handle.
- **No root rotation is stripped, replaced or composed**: the clips are sampled and drawn exactly as a single clip
  is, each body in its own frame. The task flags are those the caller gives (none for the grab's connecting clips, so
  their root motion moves both bodies). The two bodies are put in place by the combat code, not by the task:
  turned and slid together before the clip, snapped to the hold's offset at its end and then attached
  ([Combat: posing a grab](../combat.md#grab-posing)).
- **The clips record the pairing**: an attacker's paired clip carries an event of type 8 at frame 0 whose position is
  where the partner stands in the attacker's frame (`x` right, `y` forward; read by `0x00101558`), and its Anim Range
  List record holds the same point as a direction and a reach (for the hold 82: event `(0.380, 1.012)`, record
  direction `(0.351, 0.936)` × 1.081 m). Inferred from the match on every grab clip checked; the code that uses it reads
  the range record ([Combat](../combat.md#grab-posing)).

Events of type 9, 10 and `0x22` are not pair placements: `0x00101dd8` hands them to the effects system with their
position and rotation (confirmed (code)); type `0x40` starts a move of the human (`0x0023d2b8`, not traced further);
type 8 is never dispatched there.

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
   stay in step. With the new value's fraction `v − floor(v)` below 0.01 the lower clip leads (and plays alone);
   from 0.995 the upper one. Otherwise the upper one leads when *v* is still above the target after the move
   (falling), or when the fraction **before the move** (the old value less the old pair's index) was over 0.875; else
   the lower one. So on the update the pair changes downward (2.0 → 1.91, say) the new lower clip leads, whatever the
   new fraction. Confirmed (code) at `0x0010a5b8`; confirmed (runtime) at the gym's workouts
   ([Workouts](../workouts.md#rep-length)). The leading cursor's events fire, the other's are muted (flag `0x10`).
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

### When a clip ends {#clip-end}

**The end test** is in the cursor's advance (`AnimCursor_Advance`, `0x001044a0`), and every clip task uses it: the
clip that hands over (types 3 and 6, `0x00105af0`, `0x00108bd0`) and the loop (type 1, `0x00105770`). With *t* the
cursor's time (`+0x18`), *s* the step (dt × the task's rate) and *D* the descriptor's duration (`+0x0c`):

```text
end   = uint(D × 30 + 0.5)          the frame count F (D is a whole number of frames on the disc)
frame = uint((t + s) × 30 + 0.5)    the frame the step would reach
frame < end:  step the channels, add s to t, fire the events passed; return 0
otherwise:    change nothing; return (t + s) − (D − 1/60)
```

So **a clip ends on the update whose step would bring its frame to F**, the same rule as an event at frame F
([Reading the tables](../combat-moves.md#reading)): counting the clip's first update as k = 0 and its first step on
k = 1, it ends on the first k with k × *r* ≥ F − 0.5. A clip therefore **lasts ceil((F − 0.5) / *r*) updates** away
from the ties, not ceil(F / *r*); its time never reaches *D*, and the last frame it shows is F − 1. Confirmed (code)
at `0x001044a0`. **On an exact tie** at rates 0.75 and 0.9 (k × *r* = F − 0.5) it ends one update later, because the
summed time falls a hair short; that rests on the FPU rounding every add and multiply toward zero, which is
**inferred** ([Reading the tables](../combat-moves.md#reading)), not read in the code. Summed with round-to-nearest,
440 and 441 would last 10 and 14 updates instead of the 11 and 15 measured below. The one tie that is exact even then
is F = 2 at 0.75 (k = 2), which acts on time; see the overshoot of 0.0 below.

**On the end update** (confirmed (code) at `0x00105af0`, `0x00108bd0`, `0x00105570`, `0x00105f78`):

1. The ending step changes nothing: no channel steps and **no event fires**, so an event at frame F or later never
   fires in a clip that hands over (events fire only from the step, `AnimCursor_Step` `0x00104110`).
2. Types 3 and 6 put their **next task** on the stack in their own place, **in the same update**: the next task, not
   the old clip, is sampled for the pose and its root motion that update. With a blend time of 0 the next task is not
   advanced that update (it shows its time 0); with a blend time it is advanced by dt (its own advance applies its
   rate), having followed the last *b* seconds of the clip in the sampler (`0x00105ba0`: its time kept to the time
   into the blend window, its pose blended over the clip's by that share of *b*). The overshoot is used only as a
   flag; it is not carried into the next task.
3. The old task is put on a deferred list (`AnimTask_DeferGive` `0x00105640`), which `AnimTask_FlushDeferred`
   (`0x00105570`) empties once every human's animation has stepped, **before** the state updates and the actions of
   the same update ([Humans_Update](../tasks.md#humans-update)). The release (`0x00105f78` for type 3) clears the
   human's state flag `0x4000000000` and the **held flags** the task still holds from record `+0x08`, sets bit
   `0x80000000` at `+0x40` of an object the human reports (vtable `+0xf4`) when the held flags had `0x400000`, and
   calls the **end callback** (`+0x2c`) with the human. The flush then refreshes the human's playing anim id (record
   `+0x20`) from the new top task.

So on the end update the dispatcher, the stick gate and the AI already see the move as over: its held flags are clear,
its callback has run and record `+0x20` names the next clip. That is the "free again" update of the attack tables.
The release is the same function when a task is cut off rather than ending, so **the end callback also runs when a
clip is cut off** (confirmed (code) at `0x00105f78`: nothing in it tests how the task ended); a task cut off before its
end also gives its next task back.

**A loop** (type 1, `0x00105770`) wraps on the same test: it restarts its cursor (`AnimCursor_Restart`), advances it
by the returned overshoot and calls its wrap callback (`+0x28`, with the human at handle `+0x24`). Because the
overshoot is measured from *D* − 1/60, **a loop repeats every *D* − 1/60 seconds of clip time** (F − 0.5 frames), not
every *D*: the frame F key is never shown, and each pass starts 1/60 s of clip time later than an exact *D* period
would. Confirmed (code); confirmed (runtime) below.

A step whose overshoot comes out exactly 0.0 is taken as "not ended" by the callers (confirmed (code) at
`0x00105af0`, `0x00108bd0`, `0x00105770`), although the cursor was not stepped, so the task would repeat that update
for ever. An end exactly on a tie gives that 0.0: by the page's arithmetic (*s* = 0.033333335 × 0.75, toward-zero
rounding) a **2-frame clip at rate 0.75** reaches its k = 2 tie exactly, its frame test says "end" and its overshoot
`1/60 − ((D − t) − s)` is exactly 0.0 (inferred from the arithmetic, not seen). Such a clip would never end. No clip in
the [move tables](../combat-moves.md#frame-data) has fewer than 4 frames; to check the rest, list the clips whose
duration is 2/30 s and whose id's Anim Range List record has none of `0x800`, `0x1000`, `0x2000` (the 0.75 rate) in
any character set (not done).

**At runtime** (PCSX2 2.9.94, level99 street, slot 1 copied; scenarios `clip_end_chains` and `clip_end_loop` in
`repo:research/traces/scenarios/`, the bottom task's cursor read every update). The chain clips' lengths, from the
first update showing the id to the first showing the next one, all match the rule; the cursor's last time was below
*D* and its last frame F − 1 each time:

| Clip | F × rate | Measured | Rule | ceil(F / *r*) |
| --- | --- | --- | --- | --- |
| 19 `SSS3` | 21 × 0.8 | **26** (5 runs) | 26 | 27 |
| 20 `SSS3_HOLD` | 26 × 0.8 | 32 (5 runs) | 32 | 33 |
| 17 `SSX3` | 27 × 0.8 | 34 (2) | 34 | 34 |
| 11 `X1`, 14 `XS2` | 24 × 0.8 | 30 (2 each) | 30 | 30 |
| 15 `SX2` | 16 × 0.8 | 20 (2) | 20 | 20 |
| 389 (the attack's end clip) | 19 × 0.75 | 25 | 25 | 26 |

The lengths measured before agree as well: 12 `S1` 20 ([Tasks](../tasks.md#held-flags)), 34 37 and 36 35 (a tie,
25.5 / 0.75 = 34, one late) ([Cross with a bat in hand](../combat-moves.md#bat-cross)), 69 16, the fence climb's 440
11, 441 15 (both ties) and 442 13 ([Characters](../characters.md#climb)), the dive 1 61
([Combat](../combat.md#protected-moves)). The rule with the time summed in single precision rounded toward zero (the
inferred rounding mode, above) gives every one of them; ceil(F / *r*) is one too many for 19, 20, 389, 34 and the
dive, and the rule without the late tie one too few for 36, 440 and 441. The idle loop 388 (60 frames,
2.0 s, at 0.75) wrapped every 79 updates, its time after each wrap 0.0249, 0.0166, 0.0083 s: each pass 1.9833 s of
clip time, *D* − 1/60.

**What to build:** end a clip that hands over on the update its step would reach frame F, using the event rule
(summed time, the frame `uint(t × 30 + 0.5)`, the late ties); on that update sample the next task instead, clear the
held flags, run the end callback and report the next clip's id before anything reads them. Loop a looping clip with a
period of *D* − 1/60.

### Root motion {#root-motion}

The sampler (`AnimCursor_SamplePose`, `0x00104ce0`) is given the task's rate in `f12` and puts section A's root
velocity, **times the rate** (`vmulx` at `0x00104e68`), in the pose's first translation, unless the cursor has flag
`0x1` or the clip has no section A (then it keeps the instance's default and marks the pose "none"). Bone 0's
rotation channel (mask bit 0) is the root's **turn per frame**, likewise unless flag `0x2`, and it is **scaled by the
rate as well**:

- It is the channel's **key in force**, not interpolated: the sampler reads the `x, y, z` of the key the cursor holds
  for that channel (`+0x2c`, at `0x00104f48`) and rebuilds `w = sqrt(1 − x² − y² − z²)`; it never calls the rotation
  lerp (`0x00104ac8`) that every other bone goes through.
- Unless the rate is exactly 1.0 or `w` is within 5·10⁻⁷ of 1 (no turn), that quaternion is **slerped from the
  identity** (`0x00511780`, `(0, 0, 0, 1)`) toward the key by the rate: `Quat_Slerp(rate, out, identity, key)`
  (`0x00336a00`), which runs from its first quaternion at 0 to its second at 1 and clamps the factor to [0, 1]. The
  result is the same axis turned by **the key's angle × the rate**.

Both blend like any other channel. When the instance builds its bone matrices (`0x00104630`) it keeps the blended
root velocity at instance `+0x00` with a flag at `+0x461`, and the turn with a flag at `+0x462`. Confirmed (code) at
the addresses cited.

Every task type samples through this one function with its rate: types 1, 3, 5, 10, 12 and 13 pass
`AnimTask_GetRate` (`0x00105860`, `0x00105ba0`, `0x001065f0`, `0x0010b448`, `0x0010adf8`, `0x0010a008`), types 4, 6,
7 and 11 their `+0x0c` (`0x00108800`, `0x00108c80`, `0x00106b60`, `0x00107f60`); a paired task's rate is the
attacker's ([Paired tasks](#paired-tasks)). Confirmed (code).

The human's state update (`0x0023fea8`) then calls `Human_ApplyRootMotion` (`0x0023f238`), before the move:

- **Velocity:** the root velocity, turned by the human's rotation into world axes and capped at 50 m/s, is **added**
  to the velocity the state function set.
- **Turn:** with the turn's angle *a* = `2 · acos(w)`, the human turns about `z` (the sign from the quaternion's `z`) by
  `a × 30 × dt` per update (`dt` is `0x005102cc`, 1/30 s), so the channel holds a turn per 1/30 s. No rate is applied
  here: the sampler has applied it. A human with an attachment (`+0x280` not −1) instead takes the pose's quaternion
  once per update as it is.

Confirmed (code).

**So the root turn and the root velocity follow one rule:** each update the human turns by *rate × (the key's
angle)* and moves by *rate × (the key's velocity)* × dt, while the clip's time advances by *rate × dt*. Over a
whole clip the turn is therefore the sum of the key angles over the clip's frames, **whatever the rate**, just as
the distance is the descriptor's displacement whatever the rate. A turn not scaled by the rate comes out 1/rate times
too large: 4/3 at 0.75, which is what Coney showed on the throws (#263). The clip-end rule
([When a clip ends](#clip-end)) does not explain that third: it changes a clip's length by at most one update, and so
its turn by at most one key's angle × the rate (the throws turned about 3° per update at runtime, against the 30° of
149's missing third), and on the end update the original samples the next task, so the ending clip turns nothing
then. A Coney clip that plays one update too long adds one more key's turn on top of the missing rate (inferred).

**On the disc** (counts and angles only, from the extracted clips; the turn summed as the key in force over each
frame of the clip, the rate from the id's Anim Range List flags): in the generic set, 161 of the 722 ids have a bone 0
channel and 158 of them turn the body by a degree or more: 127 at rate 0.75 and 31 at 1.0. Every other character set
has some as well, at 0.75 and 0.8 (Rembrandt's set: 12 at 0.75, 3 at 0.8). The throws come out as whole angles once
the rate is applied: 149 −89.9°, 151 −180.0°, 153 +86.4°, 155 +150.0°, 159 −90.0°, 161 +90.0° (148 −90.0°,
152 −269.4°); unscaled they would be −119.9°, −240.0° and +115.1°.

**At runtime** the thrower's turns on the side throws were −90° (149), 180° (151) and +86.5° (153), and 3.2° and
2.7° per update off and onto the 1/6 s turn ([Combat: where a throw goes](../combat.md#throw-direction), PCSX2
2.9.94). They match the scaled totals above, not the unscaled ones, so the runtime agrees with the code.

What changes with the rule, by kind (generic set; ids at rate 1.0 are unchanged):

| Kind | Ids at 0.75 that turn |
| --- | --- |
| Tackles | 5, 6, 9, 10 |
| Strafes | 31, 32 |
| Grabs: power strikes, spins, reversals, breaks, escapes, hit-in-grab | 59, 60, 65, 79, 81, 90-95, 100, 102, 106, 107, 112-115, 137, 139, 145, 146 |
| Throws and wall throws | 148-161 (all but 160) |
| Tandem attacks (plain, bat, knife) | 166-168, 172-174, 177-180, 184-188, 190, 191 |
| Grounded and wounded | 198, 204, 205 |
| Mounting and the mount | 211, 214-218, 225, 227, 231, 233, 235, 238-240, 242-244, 246, 247 |
| Arrests | 254, 255, 266, 325, 327, 332 |
| Knockdown, extreme and death reactions | 285-315 (those with a turn) |
| Weapon grabs and mounts | 484, 486-488, 496, 497 |
| The ghetto set's turns (its fight and run turns play at 0.75) | 557-560, 568, 570, 575-578 |
| Stealth kill reactions | 638, 640, 642 |

At rate 1.0 and unchanged: the turns in place (360-363, 390-398), the run turns (416, 418), the barrel set's turns
(511-532), 8, 261-265 and 319. The locomotion sets no horizontal velocity of its own while a start or stop clip
plays (human flags `0x10000000` or `0x80000`, mask `0x110c0880` tested at `0x00241a88`, or move state 5 or 6), so the
clip alone moves the character then. At runtime the walk start moved Rembrandt at a steady 0.76 m/s, and section A
of that clip is about 1.05 m/s × 0.75 ≈ 0.79 m/s (confirmed (runtime) for the speed; the match is inferred).

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

**Root turn, a known difference** (#263): `AnimTask::applyFlags` scales the root velocity by the task's rate but not
bone 0's turn, and `samplePose` interpolates bone 0 like any other bone. The original holds bone 0's key in force and
turns by its angle × the rate ([Root motion](#root-motion)), so every turning clip played at a rate below 1 turns
1/rate times too far in Coney (the throws by a third).

**The pose** follows the original's: a bone without a channel takes the [reference pose](#reference-pose)
(`anim::referenceRotations()`, the 34 rotations above, normalised) and is marked as a default, and `blendPoses()` reads
the mark as `Pose_BlendPartial` does (two defaults keep the first pose's value; for the root velocity and bone 0 a
default side gives way to the other; otherwise slerp). `boneTransforms()` draws from the pelvis: entry 0 is the
identity, bone 1 takes the root translation and its own rotation in model space, so bone 0's turn never tilts the
body ([Bone transforms](#bone-transforms)). A paired clip is played through `HumanAnimator::playPaired()`, which takes
the clip and its rate from the attacker's anim set ([Paired tasks](#paired-tasks)).

**Coney's disc check** (`[disc][characters]`, values only) confirms the inferred cause of the grabs posing "rolled on
the side": Coney's bind rotation for bone 2 (from the model's HAnim frame) is the identity in every model, 120° from
the reference pose's `(-0.5, 0.5, -0.5, 0.5)`. With the reference pose the grab clips stand upright, and the disc test
`[disc][combat]` finds the model-space pelvis of the holds 82-85 within 2.5° and a millimetre of the values read at
runtime ([Combat](../combat.md#grab-pose-runtime)); the walk, run and attack clips, which animate bone 2, are unchanged.

**Disc test** (`[anim]`, counts only): 5,127 resources, 31,274 occurrences, 1,875 distinct clips, none failing,
unpaired or sampling to a non-finite pose; 957 clips have no section A, none lacks B; 9,468 events.

**Coney choices**: "entry `+4`" of the bone offset chunk is read literally, the float at byte 4, which is 0 on the
disc; the character viewer plays clips in place at rate 1 with no root motion; a looping clip carries its overshoot
into the next pass; a clip without section B has the pelvis at its skeleton offset (none on the disc).

**The key in force** (inferred from the runtime speeds, [Characters](../characters.md#locomotion)): the sampler takes
the key at or before the **nearest** whole frame (a tie going to the lower one) and carries it toward the next key by
`t = (frame − key) / delta`, not clamped, so `t` is negative while the frame is still short of that key. Every clip's
section A starts with a speed of 0 at frame 0, and a start clip's first update reaches frame 0.75 (its rate): a lerp
from frame 0 gave 0.75 of the original's first speed, while this reading gives the walk start's, the run start's and
the run stop's measured speeds on every update but the two that fall exactly half-way between keys (frames 4.5 and
7.5), where the original took the lower key and Coney's float time comes out a hair above the half. It is how a
cursor that steps its channels as each frame is passed ([Playing a clip](#playing-a-clip)) would behave if a frame
counted as passed from half-way; the code of that step (`0x00104110`) has not been read for it. Every channel,
rotations included, is sampled this way, except bone 0's turn, which takes the key in force without carrying it
toward the next key (confirmed (code) at `0x00104f48`, [Root motion](#root-motion)).

In the task system:

- **The stack** is kept as a list of layers, newest first; when a fade completes, everything older than it goes. A
  change finishes the newest fade at once when the stack holds more than 6 tasks, and drops the oldest above 12.
- **The clip that hands over** supports only a blend time of 0 (all the locomotion uses) and carries its overshoot into
  the next task. It ends when less than 0.1 ms of its clip is left: a clip of a whole number of updates at its rate
  ends on the update its time reaches its length (`XX2`'s 24 frames at 0.8 in 30 updates at runtime), not one later
  when the floats' sum falls a hair short. **A known difference:** that is ceil(F / *r*) updates, where the original
  ends on the update its frame would reach F (ceil((F − 0.5) / *r*), late on a tie, [When a clip ends](#clip-end)):
  Coney plays one update too many on 50 of the 174 clips on [Combat moves](../combat-moves.md#frame-data) (19, 20,
  34 and the dive 1 among them), and on 389, which the tables do not list. Clip 34 alone is ended a step early as a stand-in
  (`handOverEarly`). Coney's loop carries its whole overshoot (a period of *D*), the original's a period of
  *D* − 1/60.
- **Held flags** ([Tasks](../tasks.md#held-flags)): a task holds bits of its human's record `+0x08` and sets its start
  bits as it starts (first clearing the bits it holds). Events `0x2c` (clear `0x1`, set `0x2`), `0x2d` (clear `0x7`, set
  `0x4`) and `0x48` (all held bits replaced by `0x40000`) act only on a task that holds the bits they change, and only
  the newest task's events fire. A clip's event at frame `f` fires on the update whose cursor frame, `uint(t·30 + 0.5)`
  (`0x001044a0`), first reaches `f`, with the time summed in single precision. The exact ties at rates 0.75 and 0.9 fall
  one update late, and the frame tables on [Combat moves](../combat-moves.md#reading) give the rule; on Rembrandt's
  attack clips this gives the phases measured at runtime. A task that ends, hands over or is faded out and dropped
  clears the bits it still holds, but not those another task on the stack holds.

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
  the rate, turned into world axes) to the human's velocity, and their root turn (its angle **times the rate** too) to
  its heading; the gait loops and the idle have it switched off by the task flags ([Root motion](#root-motion)).
- **Bone 0's turn is the key in force, scaled by the rate:** take the channel's current key without interpolating it,
  and turn by its angle × the rate (a slerp from the identity, the factor clamped to [0, 1]) before blending. With
  that, a clip's total turn does not depend on its rate ([Root motion](#root-motion)).
- The root's motion is also data for the speeds: the descriptor's displacement over the duration is the speed of a
  looping gait (the game derives each character's walk, jog, run and sprint speeds from it,
  [Characters](../characters.md#speed-classes)).
- A synthetic test fixture is easy to make: one clip with two channels and a few keys per channel, checking the
  channel split, the scales and the interpolation; no game data is needed. The gait blend's leader rule and the fade
  curve can be tested with two synthetic clips of different lengths.
- Bones are 0-33 in the clip and the pose, 32 in the HAnim hierarchy: map them once through the parent table.
- **Fill a bone the clip leaves out from the [reference pose](#reference-pose)**, not from the model's bind rotation,
  and keep a "default" mark on it for blending. Bone 2 is the one that shows: most grab, tackle and grounded clips
  leave it out.
- **Draw from the pelvis**: bone 1 takes the root translation and its own rotation in model space; bone 0's rotation
  is the turn per frame and is never applied to the skeleton ([Bone transforms](#bone-transforms)).
- **A paired clip comes from the attacker's set**: look the victim's id up in the attacker's character, at the
  attacker's rate ([Paired tasks](#paired-tasks)); place the two bodies as [Combat](../combat.md#grab-posing) says.

## Open questions

- The meaning of byte `+1` of a key (not looked at in this pass).
- The Anim Range List's `+0x0a` and `+0x0c`, and the class record overrides by id.
- The event types (11 is the most common) and what they trigger (footsteps, sounds, hit windows; speculative).
- How section A drives the character during a start clip (confirm by comparing a clip's keys with the speeds
  sampled at runtime).
- The exact HAnim bone ↔ pose bone mapping (inferred above; Coney's disc test supports `n + 2`,
  [Character geometry](../characters.md#character-geometry)).
- Task types 5, 15 and 16 (and so type 4, the paired form of 5, beyond its constructor; it is built by the tackle and
  mount code, `0x00270b38`, `0x00271ef0`).
- The human test `0x00223440` that makes `0x00176d60` take the queued tasks' pose as the default pose.
- The order in which the instance samples its task stack (bottom-up is inferred from the fade's sampler).
- The type 3 task's blend into its next task when its blend time is not 0: how the next task's time is kept in the
  window is read ([When a clip ends](#clip-end)), but not checked at runtime; after 389 (blended into 388) the idle
  showed 0.15 s on its first update.
- Whether a loop's wrap fires the events at frame 0 of the new pass (`AnimCursor_Restart` was not read for it).
