# Characters (humans)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made in
PCSX2 2.9.94 (2026-10-04) by reading memory over PINE in `level99`, checkpoint 1, and say so. The disc survey read the
NTSC-U disc's WAD with throwaway scripts outside the repository and reports counts and hashes only.

## Purpose

Every person in the game, the player included, is a **human**: one object of a single class with a skinned model,
an animation player and a brain. The player is a human whose controller is a pad instead of the AI. This page covers
what the first playable milestone needs: how a level script creates the player, which files make a character, the
object's layout as far as it is known, how pad input becomes movement, and how a speed picks idle, walk or run. The
animation format is on [Animation](formats/animation.md); the camera that follows the player on
[Camera](camera.md).

In one paragraph: a level script calls `HuCreate(name, type, position, heading, ..., player, gang)`. The game takes a
free slot among **60 static humans**, remaps the type to a behaviour class, finds the character's model by name in
the **Character List** of `warriors.glr`, snaps the position to the ground with a short ray cast and, for player 1,
binds pad 0 and the camera. The character's files are three resources named by CRC: the model (an RW clump with a
32-bone HAnim skeleton and a PS2 skin), its texture dictionary, and its **character data** (its animations and an
anim id → animation table). Each update (30 per second) the pad's stick, already turned into camera space, gives a
direction and a magnitude; the magnitude chooses walk (from 0.12) or run (above 0.95), the speed ramps towards it at
24 m/s², and the heading turns towards the stick at a limited rate.

## Original structure

`Human/` (`0x0021c7c8`-`0x00273fa0` and probably beyond, [Source map](source-map.md)) holds the human; the model side
is in `Graphics/Character.cpp`, `CharacterModel.cpp` and `Animations.cpp`. Names are ours unless a path or class
string gives them.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00358428` | `HuCreate` binding | reads the Lua arguments, calls `Human_Create` | confirmed (code) |
| `0x00233d60` | `Human_Create` | heading quaternion, slot, init, name, player; returns the handle | confirmed (code) |
| `0x00217f08` / `0x00217ec8` | `Human_AllocSlot` / `Human_FreeSlot` | a free index below 60 in the object table | confirmed (code) |
| `0x00218008` | `Human_Init` | type remap, model, ground snap, defaults | confirmed (code) |
| `0x0021cda8` | `Human_SetName` | 15 characters at `+0x80` | confirmed (code) |
| `0x00229c40` | `Human_MakePlayer` | for player 1: pad, HUD, camera target | confirmed (code) for the call; roles inferred |
| `0x00228730` / `0x0021cb78` | static initialiser / constructor | builds the 60 humans at start-up | confirmed (code) |
| `0x00228800` | `CharClass_Get(id)` | `0x00684620 + id × 0x1ac` | confirmed (code) |
| `0x00383f38` → `0x00228af8` | `CfgChar` | fills a character class | confirmed (code) |
| `0x003842c0` → `0x00228888` | `CfgSpeedClass` | fills a speed class | confirmed (code) |
| `0x00221710`, `0x002215d0`, `0x00221620`, `0x00221670`, `0x002216c0` | speed getters | base, walk, jog, run, sprint | confirmed (code) |
| `0x00221760` | `Human_GaitForSpeed` | a speed to gait 0, 2, 3, 4 or 5 | confirmed (code) |
| `0x002213d8` | `Human_MaxTurn` | the turn limit per update | confirmed (code) |
| `0x00146078` | `PlayerRecord_Update` | pad → stick angle and magnitude | confirmed (code) |
| `0x00240e38` | `Human_PlayerLocomotion` | stick → velocity and heading | confirmed (code) |
| `0x00248df0` | `Human_Lean` | a lean from the turn rate | confirmed (code) |
| `0x00249108` | `Humans_Update` | the characters' update, every second task-manager tick | confirmed (code) |
| `0x00249b98` | `Humans_MarkSkeletons` | marks every skeleton for an update, from mode 1 | confirmed (code) |
| `0x001783d0`, `0x0018e9e0`, `0x0016e8f0` | resource loaders | model (type 3), textures (type 4), character data (type 5) | confirmed (code) |
| `0x0016e258` | chunk `0x08` handler | the character data's anim table | confirmed (code) |
| `0x00177b80` / `0x00174d00` | `CharacterInstance` create / constructor | the model instance (0x330 bytes, vtable `0x00538a78`) | confirmed (code) |
| `0x00217a98` | `Human_AttachInstance` | gives the human its instance (`+0xd8`) | confirmed (code) |
| `0x00175080` | `CharacterInstance_GetAnim(id)` | an anim id to an animation | confirmed (code) |
| `0x001897a8` | resource manager update | loads missing characters and dynamic animations later | confirmed (code) |

## Data

### The human object (0x6d0 bytes) {#the-human-object}

Sixty humans live in one static array at `0x00640c80` (human `i` at `0x00640c80 + i × 0x6d0`), built at start-up by
`0x00228730` with the constructor `0x0021cb78`. A human in use has an entry in the handle table
(`0x006ebd38`, 0xb00 entries of `{pointer, u16 serial}`; a handle is `serial | index << 16`, resolved by
`0x00390180` / `0x00390268`). Vtables: `0x0053f088` at `+0x00`, `0x0053f020` at `+0x70`, `0x00544b98` at `+0xe8`.
Confirmed (code); offsets with "runtime" were checked on Rembrandt.

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| `+0x10` | vec4 | position, game axes (`z` up), metres | confirmed (code), runtime |
| `+0x20` | quat | rotation `(x, y, z, w)`; a heading is a rotation about `z` | confirmed (code), runtime |
| `+0x80` | char[16] | name (`Rembrandt`) | confirmed (code), runtime |
| `+0x90` / `+0x92` | u16 | own handle's serial / index | confirmed (code) |
| `+0xcc` | int | behaviour class (`0x1e` for Rembrandt): selects the class record for speeds | confirmed (code), runtime |
| `+0xd0` | int | the type passed to `HuCreate` (`0x20`) | confirmed (code), runtime |
| `+0xd4` | pointer | the human's 0x180-byte record (`0x0065a540 + i × 0x180`) | confirmed (code), runtime |
| `+0xd8` | pointer | the `CharacterInstance` (model, skeleton, animation) | confirmed (code) |
| `+0x1b0` | s8 | player index, -1 for none | confirmed (code) |
| `+0x1a8` | int | the gait (0, 2-5) used by the lean | confirmed (code) |
| `+0x298` / `+0x29c` | float | turn this update / smoothed lean (radians) | confirmed (code) |
| `+0x37c` (`+0xdf` as a word index) | int | model index in the Character List | confirmed (code) |
| `+0x3a0` | float | vertical velocity, kept by the locomotion | confirmed (code) |
| `+0x3a4` | float | speed multiplier (1.0 for the player; 0.98-1.02 at random for others, `0x00219608`) | confirmed (code), runtime |
| `+0x3c8` | 7 × 0x28 | dynamic animation slots | confirmed (code) |
| `+0x5d8` / `+0x5dc` | float | last stick angle / magnitude | confirmed (code) |
| `+0x5e0` / `+0x5e4` | float | last turn step / last heading error (turn smoothing) | confirmed (code) |

**Per-player record** (`0x00660f50 + i × 0x2c`, 60 of them, one per human index), confirmed (code) at `0x00146078`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | the pad's buttons (8 bytes copied from the pad record's `+0x08`, [Front end](frontend.md#pad-record)) |
| `+0x08`, `+0x0c` | stick angle, two buffers (radians, `atan2` of the camera-turned stick) |
| `+0x10`, `+0x14` | stick magnitude, two buffers, clamped to 1 |
| `+0x18` | which buffer is current (toggles 0/1 each update; the other is the previous update's) |
| `+0x19` | pad index, -1 for none (0 for the player at runtime) |
| `+0x1b` | 1 while the human is pad-controlled; 0 hands it to the AI |
| `+0x1f` | input locked: angle π/2, magnitude 0 |

A second per-human record of 0x2f0 bytes is at `0x006d53f0 + i × 0x2f0` (contents not traced).

### Character classes {#classes}

`CfgChar` (in `config_preload2.lua`) fills a 0x1ac-byte record per type at `0x00684620 + id × 0x1ac`: 45 floats from
`+0x00`, 16-bit values from `+0xb8`, the model index by name at `+0x112` and by `"<name>_a"` at `+0x114` (used in
levels 60-64), the speed class byte at `+0x11c`, and four strings (32 bytes each) from `+0x14c`. Rembrandt's call is
`CfgChar(32, ..., 1800, DamageFox, Att_Warrior, 1, "warr_re_cv", "none", 7, 0, RangeNormal, "none", 0, "none")`; type
40 (Ash) uses `warr_ty_cv`. Confirmed (code) for the layout; the arguments inferred from the disassembly.

`Human_Init` remaps the type: 32 becomes behaviour class 30 (`0x1e`) with a variant flag (confirmed (code) at
`0x00218008`, runtime `+0xcc` = `0x1e`, `+0xd0` = `0x20`).

### Speed classes {#speed-classes}

`CfgSpeedClass(class, ...)` writes six floats at `0x006b6548 + class × 0x18`. The getters read entry `+0x00` (base),
`+0x08` (walk), `+0x0c` (jog), `+0x10` (run) and `+0x14` (sprint); `+0x04` has no getter found. Values from
`config_preload2.lua` (inferred from the disassembly):

| Class | +0x00 | +0x04 | walk | jog | run | sprint |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 3 | 0.8 | 1.6 | 4.1 | 6.5 | 8 |
| 1 | 3 | 0.8 | 1.6 | 4.1 | 7.5 | 9 |
| 2 | 3 | 0.8 | 1.6 | 1.6 | 1.6 | 9.75 |
| 3 | 3 | 0.8 | 1.6 | 4.1 | 7.5 | 7.5 |
| 4 | 3 | 0.964 | 0.964 | 0.965 | 2.723 | 2.723 |

**But these are not used in play.** When `0x005101e0` is set (it is 1 at runtime), each getter returns the human's
own value from its 0x180-byte record times `+0x3a4` instead (confirmed (code) at `0x00221710`, `0x002215d0`): `+0x164`
for the base, `+0x170` for walk, and (by the same pattern, inferred) `+0x174`, `+0x178`, `+0x17c` for jog, run and
sprint. **Rembrandt at runtime:** base 3.43, walk 1.63, jog 4.86, run 7.80, sprint 10.25 m/s (`+0x16c` = 1.59).
Confirmed (runtime). Who writes these values is not traced; that they come from the character's own animations
(a clip's root displacement over its duration) is speculative.

### Files of a character {#files}

A character is three resources, each a file named by the decimal CRC of a hash (`"%u"`, `0x00551ed8`) and loaded by
the resource manager ([Chunk system](chunk-system.md)):

- the **model** (resource type 3, loader `0x001783d0`): chunk `0x47` with the RenderWare clump, then chunk `0x28`
  with the bones' bind offsets (34 × float4, 544 bytes);
- the **texture dictionary** (type 4, `0x0018e9e0`): one chunk `0x2a`;
- the **character data** (type 5, `0x0016e8f0`): the animations (a `0x00` keyframe chunk and a `0x02` descriptor
  for each, [Animation](formats/animation.md)), one **Anim Range List** (`0x45`) and one **Character Data** chunk
  (`0x08`).

The **Character List** (chunk `0x44` in `warriors.glr`, 543 records of 32 bytes) finds them by model name:

| Offset | Meaning |
| --- | --- |
| `+0x00` | CRC-32 of the model name, lower case, no path prefix ([Name hashing](name-hash.md)) |
| `+0x04` | hash naming the character data resource |
| `+0x08` | hash naming the model resource |
| `+0x0c` | hash naming the texture dictionary |
| `+0x10` | the character data's size |
| `+0x14` | the model's size |
| `+0x18` | a size close to the model's (`+0x14` plus `0x1df` in the cases checked; meaning not traced) |
| `+0x1c` | the texture dictionary's size |

Confirmed (code) for `+0x00`-`+0x0c` (the loaders' lookups); the sizes are inferred from matching the WAD entries.

**The clump**, the same for every skinned character on the disc: one atomic, 33 frames, an HAnim hierarchy (`0x11E`)
of 32 bones with key size 36 and flags 0, geometry in PS2 native format (`0x010200f3`, two texture coordinate sets)
with the extensions `0x50e`, `0x510` (native data) and `0x116` (Skin, PS2 platform 4). Plugins:
[Graphics](graphics.md). Corroboration (disc survey).

**Character Data** (`0x08`, 2,912 bytes for Rembrandt; handler `0x0016e258`, vtable `0x005389a0`): a placeholder vtable
word, then **722 slots** at `+0x08`, one per anim id, then 16 bytes. A slot of `0xffffffff` means "use the default",
taken from the table at resource manager `+0x70`; a slot value `n` picks the `n`-th animation loaded with this
resource, counted on the stack of objects the chunk system pops last-in first-out. `CharacterInstance_GetAnim`
(`0x00175080`) answers an id with this table. For Rembrandt, id 408 is walk and 413 walk-start; ids 11 and 12 are
combo attacks (inferred from the clip names). The handler keeps the Anim Range List at `+0xb50` / `+0xb54`. Confirmed
(code) for the slot rule; the id meanings inferred.

**Anim Range List** (`0x45`, 11,568 bytes): a count word, then 722 records of 16 bytes, one per anim id, of the form
`(u16, u16 = 1000, f32, u16, u16, u16, u16)`. Not decoded.

**Example, Rembrandt (`warr_re_cv`)**: character data `0xe72f9fb5` (200,640 bytes, shared with `warr_re`: 36
animations, one `0x45`, one `0x08`), model `0xdb1cdf36`, textures `0x46af47d8`. Corroboration (disc).

**Disc survey (counts only):** 153 distinct skinned models, all with 33 frames, 32 HAnim bones, key size 36, a PS2
skin and a 544-byte bone offset chunk; 1,209 Character Data and Anim Range List chunks.

### Movement constants {#movement-constants}

Confirmed (code) at the readers; values from `.data`.

| Address | Value | Use |
| --- | --- | --- |
| `0x005102cc` | 1/30 s | the characters' step (`Humans_Update` runs at 30 Hz) |
| `0x005102e8` | 0.95 | stick magnitude above which a run is allowed |
| (code) | 0.12 | stick dead zone |
| (code) | 24 | speed change per second of step (m/s²), so 0.8 m/s per update |
| `0x005101b0`-`0x005101dc` | 1.5°, 2.5°, 4°, 6°, 12°, 24° (each twice: player, other) | turn limits per update |
| `0x00510308` / `0x00510304` | 1 / 0 | turn smoothing on (eased) |
| `0x0051030c` | 0.8 | turn step carried over from the last update |
| `0x00510310` | 1.5 rad | heading error at which the turn reaches its full rate |

## Behaviour

### Creation {#creation}

`HuCreate(name, type, {x, y, z}, headingDegrees, str, playerIndex, gang, flag)` (`0x00358428` → `0x00233d60`),
confirmed (code):

1. Build the rotation about `z` from the heading (degrees × 0.0174533, half-angle quaternion with the axis at
   `0x00511740`).
2. Take a free slot (`0x00217f08`); on failure free it again (`0x00217ec8`) and return `NilHandle` (`0x006ebd30`).
3. `Human_Init` (`0x00218008`): remap the type to the behaviour class; take the model index from the class
   record (`+0x112`, or `+0x114` in levels 60-64); load or find the character; **snap to the ground**: cast a ray from
   the position plus (0, 0, 1) straight down for 2.5 m through `WorldManager_RayCast`
   ([Collision](collision.md)) and, on a hit, put the human on it 0.01 above (skipped in game modes `0xb` and `0x11`).
4. Name (`+0x80`), player index (`+0x1b0`); for player 1, `Human_MakePlayer` (`0x00229c40`).
5. Return the handle (`+0x2c` of the slot) and write the snapped position back into the Lua table.

The fifth argument (`"warr_sw"` in `level99.lua`) is not read by `0x00233d60`. **At runtime** Rembrandt was created at
`(-284.4, 120.4, 0.3)` and stood at `(-289.03, 120.29, 0.25)` once the intro scene ended (confirmed (runtime)).

**Loading the files.** When the character's resources are already resident (the level's dependency list or the
section's pack holds them), the instance is made at once (`0x00177b80`) and attached (`0x00217a98`). Otherwise the
resource manager's update (`0x001897a8`) loads them later and attaches them then; the dynamic animations a script asks
for (`SetDynamicAnimation`) go the same way into the slots at `+0x3c8`. Confirmed (code) for both paths.

### The characters' update {#update}

The task manager's set-up step (`0x003a3148`, called first in mode 1's frame, [Level loading](level-loading.md#a-frame-of-play))
runs `0x003a3000`, which calls `Humans_Update` (`0x00249108`) whenever 0x4b0000 ticks (about 16.7 ms) have passed.
`Humans_Update` does its work on every **second** call, so the characters step at 30 Hz with dt = 1/30. In order,
confirmed (code), each step behind a debug switch that is on in play (`0x005e5350`-`0x005e5368`):

1. Three animation managers (`0x00170c88`, `0x00171d38`, `0x00184568`); `Pads_Update`; the 60 player records
   (`0x00146078`, below).
2. For every human with an instance, `0x0023bd78` (the instance's animation step, `0x00175610`), then
   `0x00105570`.
3. Per human: a position 1.3 above the human handed to `0x0019c3f0` for humans with flag `0x4000` (an effect or
   sound, inferred), and checks against the player's gang.
4. Per human: vtable slot `+0x13c` (its state update) or a flag when it is idle.
5. The brains, alternating the order (0 → 59, then 59 → 0) on each update: AI decisions, targeting and the actions
   (`0x00254e78`, `0x00221108`, `0x00256f28`, `0x00265f70`).
6. The cameras' update (`0x0011e878`) with dt, at least 1/30.

### From pad to intent {#input}

`PlayerRecord_Update` (`0x00146078`), each update, for a record with a pad and control on: flip the buffer; copy the
buttons; take the pad record's **camera-turned** left stick (`+0x00`, `+0x04`, [Front end](frontend.md#pad-record)),
store its angle (`atan2`, `0x003357a8`) and its length clamped to 1. With `+0x1f` set the angle is π/2 and the length
0. Confirmed (code). The stick is thus already relative to the camera: pushing up moves away from it, which is what
the game does at runtime (confirmed (runtime)).

### Locomotion {#locomotion}

`Human_PlayerLocomotion` (`0x00240e38`) for a pad-controlled human; others run their state's function instead.
Confirmed (code) for the steps; the state predicates are named by what they test where known.

1. **Current speed** = the length of the velocity (vtable slot `+0x94`).
2. **Target speed.** With the stick above the dead zone (0.12): walk by default; **run** when the magnitude is above
   0.95 (`0x00225c10`); **jog** instead of run when the human carries an object of class 4 or 6 (`0x00225a50`,
   `0x00224000`); **sprint** when the state flag `0x1000000` is set and the human has stamina (`0x00225dc0`, record
   `+0x14a` ≠ 0). Three state tests override this: one (`0x00223ad0`) keeps the target at 0, one (`0x00227d98`) uses the
   record's walk speed `+0x170` unscaled, and two others (`0x00228340`, or the global `0x0051031c`) the base speed. With
   `0x00510258` set, run and sprint come from an analog button's pressure instead (more than 100 sprints); it is 0
   in play. Below the dead zone the target is 0.
3. **Skid.** In gait 4 or 5, at run speed or above, after a stick that was over 0.95, a stick under 0.2 or one
   pointing more than 120° away from the velocity (dot product below -0.5) zeroes the velocity and changes state
   (`0x002266a8`; a skid or stop, inferred).
4. **Turn.** Target heading = stick angle − π/2; current heading = the facing from the rotation. The error is
   wrapped to (−π, π]. The limit per update comes from `Human_MaxTurn` (`0x002213d8`): for the player, 12° walking,
   6° jogging, 4° running and 2.5° sprinting (360, 180, 120 and 75 degrees per second); 4° and 6° in two special
   states, 24° in another. With smoothing on, the step is the limit times an ease `(1 − cos(π · error / 1.5)) / 2`
   (full at 1.5 rad), plus 0.8 times the previous step (−0.5 times it when the error changed sign), clamped to
   [0, limit]. When the error is smaller than the step, the heading snaps to the target. The new heading becomes
   the rotation (unless `+0x3bb` locks it) and the velocity's direction.
5. **Accelerate.** Speeding up adds 0.8 m/s per update (24 m/s²) until the target; slowing down either drops to the
   target at once or, in some states (`0x00223db8`), at the same rate.
6. **Velocity** = facing × speed, with `z` = `+0x3a0` (the vertical velocity is kept); some states zero it.
7. **Lean** (`0x00248df0`): the heading change times a factor (0.4 × `+0x1ac`, or 8 × `+0x1ac` when walking) is
   clamped to 2°, 3°, 5° or 7° by gait and stored at `+0x298`; the lean at `+0x29c` follows it by 0.625 of the
   difference per update, at most 1°, 1.2°, 1.3° or 1.8° per update. What `+0x1ac` holds is not traced.

**Idle, walk, run.** `Human_GaitForSpeed` (`0x00221760`) maps a speed to a gait: 5 (sprint) at or above the sprint
speed, else 4 (run) at or above the run speed, 3 (jog), 2 (walk), else 0 (idle). Confirmed (code). Which clip each
gait plays, and how the blend between them is weighted, is not traced; the blend itself is the animation player's
([Animation](formats/animation.md#blending)).

**Measured** (PCSX2 2.9.94, the stick held fully forward from a standstill, positions read over PINE): 4.10 m after a
0.75 s hold, 10.08 m after 1.5 s: 5.98 m in the second 0.75 s, **8.0 m/s**, close to Rembrandt's run speed of
7.80 m/s (confirmed (runtime); the hold times are those of the key presses, so about ±1 frame).

## Coney's implementation

None yet. Coney loads RW clumps for the world ([The streamed world](world.md#coneys-implementation)) but has no
human, character resource or animation code.

## Notes for implementers

- **Make the player first.** For `level99`, checkpoint 1: one human named `Rembrandt`, model `warr_re_cv` (found
  through the Character List), at `(-284.4, 120.4, 0.3)` heading 0°, snapped to the ground with a 2.5 m ray from 1 m
  above; pad 0; the follow camera targeting it ([Camera](camera.md)).
- **Step at 30 Hz.** Movement, turning and the animation step all use dt = 1/30 and per-update limits; a PC build
  that runs faster should keep a fixed 30 Hz step (or scale every limit) so speeds and turn rates match.
- **Speeds per human.** Read the five speeds per character; for Rembrandt walk 1.63, jog 4.86, run 7.80, sprint
  10.25 m/s. Until their source is known, the speed class table times the multiplier is the fallback the code has.
- **The stick is camera-relative** before the human sees it: turn the stick by the camera's heading first, then
  apply the dead zone (0.12) and the run threshold (0.95) to its length.
- **Turn with a limit and an ease**, not instantly: 12° per update walking, 4° running, eased below 1.5 rad.
- **Characters are skinned with 32 bones** (34 pose entries, [Animation](formats/animation.md)); every character on
  the disc has the same skeleton layout, so one bone mapping serves all.
- **A disc test**: every Character List record resolves to three resources that load, and every model is the clump
  described above (counts only).

## Open questions

- **Gravity and ground following** while moving: the locomotion keeps `+0x3a0` as the vertical velocity, but where
  it is integrated and how the human is kept on the ground (a ray per update like the spawn's, or the collision
  mesh's sphere push) is not traced. `Collision_DropToGround` (`0x0034f950`) and `SpherePush` (`0x003519f8`) on
  [Collision](collision.md) are the candidates.
- **Who writes the per-human speeds** (`+0x164`-`+0x17c` of the 0x180 record), and whether they are derived from the
  walk and run clips.
- **Which anim ids** the gaits play (walk is 408 for Rembrandt; idle, run and sprint not found), and how the
  locomotion blend is weighted.
- **The Anim Range List** (`0x45`) records.
- **The rest of the human**: the 0x180 and 0x2f0 records, the state flags tested by `0x002265f0` / `0x00226660`, and
  `Human_MakePlayer`'s steps.
