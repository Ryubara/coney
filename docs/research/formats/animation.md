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
| `+0x18` | time in seconds |
| `+0x1c` | current frame |
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
| `+0x0e` | u16 | flags: `0x800` plays the clip at rate 0.85, `0x1000` and `0x2000` at 1.0 | `0x00104a38` |

The second word on the disc is 1000 in every record seen, which is consistent with `+0x02` being a scaled value of
1.0 m (inferred). The ranges are most likely for attacks and grabs (speculative);
the locomotion clips carry flag `0x1000` (confirmed (runtime) on Rembrandt). The class record (`CfgChar`'s 45 floats
and 16-bit values) overrides the range data for some ids, through the jump tables at `0x0055d640` and `0x0055d6f0`
(called from `Human_AttachInstance`, `0x00217a98`), confirmed (code).

## Behaviour

### Playing a clip

1. **Init** (`0x001041f8`): find each channel's first key in sections A, B and C and set up its countdown.
2. **Advance** (`0x001044a0`) by dt times the rate (`0x00104a38`: the id's [Anim Range List](#anim-range-list)
   flags `0x800`, `0x1000`, `0x2000` choose a multiplier from `0x00510260`-`0x0051026c`, which hold 1.0, 0.85, 1.0,
   1.0): the time grows, the frame is
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
can play over legs that walk. Confirmed (code). Which clips the locomotion blends, and with which weights, is open
([Characters](../characters.md#locomotion)); at runtime a gait change shows a start clip (413 walk start, 414 run
start) and then the gait clip, and release goes straight to idle.

**Root motion and speed.** A character's walking speeds are computed from its clips: the descriptor's displacement
over the duration divided by the rate ([Characters](../characters.md#speed-classes)), confirmed (code) and runtime.
While a start clip plays the character's speed follows the clip (the run start's speed varies from 2.5 to 5.7 m/s
over its 0.36 s), inferred from runtime samples to be section A's root velocity.

**Disc survey (counts only):** 31,274 clip occurrences in the WAD, 1,875 distinct; all parse with the channel rule;
the channel count equals the mask's bit count in all; at most 606 keys in a channel; clips run 1 to 680 frames; bone
33 is the highest animated bone (in 1,854 clips).

## Coney's implementation

None yet.

## Notes for implementers

- Decode a clip into channels of `(start frame, value)` keys at load; sampling is then a search (or a cursor) per
  channel and one lerp or nlerp. Keep the hold rule for a channel's last key.
- Rebuild `w` with a non-negative square root and nlerp with the sign flip, or the joints will flip.
- The root's motion is data: the descriptor's displacement over the duration is the speed of a looping gait (the
  game derives each character's walk, jog, run and sprint speeds from it), and start clips appear to move the
  character by their own root velocity ([Characters](../characters.md#speed-classes)).
- Apply the [Anim Range List](#anim-range-list) rate flag when advancing a clip (0.85 for `0x800`).
- A synthetic test fixture is easy to make: one clip with two channels and a few keys per channel, checking the
  channel split, the scales and the interpolation; no game data is needed.
- Bones are 0-33 in the clip and the pose, 32 in the HAnim hierarchy: map them once through the parent table.

## Open questions

- The meaning of byte `+1` of a key (not looked at in this pass).
- The Anim Range List's `+0x0a` and `+0x0c`, and the class record overrides by id.
- The event types (11 is the most common) and what they trigger (footsteps, sounds, hit windows; speculative).
- How section A drives the character during a start clip (confirm by comparing a clip's keys with the speeds
  sampled at runtime).
- The exact HAnim bone ↔ pose bone mapping (inferred above, not checked).
