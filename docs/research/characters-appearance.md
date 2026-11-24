---
kind: topic
system: characters
owns:
  functions: [
    "0x0016e258", "0x00174d00", "0x00177b80", "0x001897a8", "0x0018e9e0", "0x00217a98", "0x00228d70", "0x003a3ba0",
  ]
---
# Characters: the model, its files and its hats

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records or, for the older ones, How this was checked.

## Summary

What a human is drawn as. A character is three resources named by CRC: the model (an RW clump with a 32-bone
HAnim skeleton and a PS2 skin), its texture dictionary and its character data (its animations and an anim id
to animation table). This page holds those files, what the model's bytes hold beyond the clump's sections, the
hat-fit sets and worn hats, and the spinning icon over a human's head. How a type picks the model is on
[Classes](characters-classes.md#type-to-model); the clips are on [Animation](characters-animation.md) and the
animation format on [Animation](formats/animation.md).

## Map {#original-structure}

The functions that load and attach a character:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x001783d0`, `0x0018e9e0`, `0x0016e8f0` | resource loaders | model (type 3), textures (type 4), character data (type 5) | confirmed (code) |
| `0x0016e258` | chunk `0x08` handler | the character data's anim table | confirmed (code) |
| `0x00177b80` / `0x00174d00` | `CharacterInstance` create / constructor | the model instance (0x330 bytes, vtable `0x00538a78`) | confirmed (code) |
| `0x00217a98` | `Human_AttachInstance` | gives the human its instance (`+0xd8`) | confirmed (code) |
| `0x001897a8` | resource manager update | loads missing characters and dynamic animations later | confirmed (code) |

## Data

### Files of a character {#files}

Confirmed (code) at the cited addresses unless a line says otherwise; what each file's chunks hold is inferred from the
chunk handlers.

A character is three resources, each a file named by the decimal CRC of a hash (`"%u"`, `0x00551ed8`) and loaded by the
resource manager ([Chunk system](chunk-system.md)). The resource types and their loaders are confirmed (code); the
chunks each file holds are inferred from the chunk handlers on that page:

- the **model** (resource type 3, loader `0x001783d0`, confirmed (code)): chunk `0x47` with the RenderWare clump, then
  chunk `0x28` with the bones' bind offsets (34 × float4, 544 bytes);
- the **texture dictionary** (type 4, `0x0018e9e0`, confirmed (code)): one chunk `0x2a`;
- the **character data** (type 5, `0x0016e8f0`, confirmed (code)): the animations (a `0x00` keyframe chunk and a `0x02`
  descriptor for each, [Animation](formats/animation.md)), one **Anim Range List** (`0x45`) and one **Character Data**
  chunk (`0x08`).

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
of 32 bones with key size 36 and flags 0, geometry in PS2 native format (`0x10200f3`, two texture coordinate sets)
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
`(s16, s16 = 1000, f32, s16, s16, s16, u16)`: offsets, reach, ranges and rate flags
([Animation](formats/animation.md#anim-range-list)).

**Example, Rembrandt (`warr_re_cv`)**: character data `0xe72f9fb5` (200,640 bytes, shared with `warr_re`: 36
animations, one `0x45`, one `0x08`), model `0xdb1cdf36`, textures `0x46af47d8`. Corroboration (disc).

**Disc survey (counts only):** 153 distinct skinned models, all with 33 frames, 32 HAnim bones, key size 36, a PS2
skin and a 544-byte bone offset chunk; 1,209 Character Data and Anim Range List chunks.

### Character geometry {#character-geometry}

What the model's bytes hold beyond the clump's sections, found by Coney's decoder and its disc test over the 128
models the Character List names (no code was read for this subsection, so it is corroboration (disc) unless marked).

- **The native geometry** is the world's PS2 layout ([Graphics](graphics.md)) with a fifth slot: `STCYCL 5,1` and
  five `UNPACK`s per batch, slot 0 positions (`V4_16`), slot 1 texture coordinates (`V4_16`, two sets), slot 2
  colours (`V4_8` unsigned, all zero in the characters checked), slot 3 normals (`V4_8`) and slot 4 the **bone
  weights** (`V4_32`, format `0x6c`). Vertex counts match the mesh plugin's in every model.
- **A weight** is a float whose low 10 bits are replaced by `(node index + 1) << 2`, or 0 for an unused slot; the
  weight is the float with those bits cleared. The same encoding as librw's PS2 skin reader.
- **One mesh, one material**, colour `0x969696ff`, and the material is **not textured** (no texture section): every
  character's texture dictionary holds exactly one texture (507 dictionaries), named after the character in 427 of
  them. The game binds it to the material at each draw ([Drawing a human](graphics.md#human-draw)).
- **The skin's inverse bind matrices** are the inverses of the HAnim frames' world matrices (checked to float
  precision for every model).
- **Frames to pose bones**: frame 0 is the clump's root, frame 1 carries HAnim id 0 and the hierarchy; node index
  equals id on the disc, and the pose bone of HAnim id `n` is `n + 2` ([Animation](formats/animation.md)), so pose
  bones 0 and 1 have no frame. Inferred, from the next point.
- **Pose axes are the clump's turned +90° about x**, `(x, y, z) → (x, −z, y)`: the bone offset chunk's entries are
  the frames' translations turned so (bone `n + 2` against frame of id `n`), and of the 24 axis rotations this one
  gives the smallest mismatch between clip-posed joints and the bind skeleton (0.022 m for Rembrandt's walk, 0.054 m
  for the next best). In pose space z is up and a character faces +y (the walk's root velocity is +y). Inferred.
- **The parent table differs from the frames in one place**: pose bone 3's parent is 1 in `Skeleton_InitParents`
  (`0x00101120`), while its frame's parent is HAnim id 0 (pose bone 2). Inferred (Coney follows the table).
- The float at `+4` of the bone offset chunk (entry 0's `y`) is 0 in every model.

## Behaviour

### Hats {#hats}

Confirmed (code) at the cited addresses unless a line says otherwise.

`CfgHat(set, type, hat, {x, y, z}, {i, j, k, r})` (`Cfg_AddHatFit`, `0x00228d70`) fills a **hat-fit set**: a record of
0xa90 bytes at `0x00662b70 + set × 0xa90` with 48 slots of a 32-byte transform (the offset with w = 1, then the
rotation) from `+0x00`, the 48 hat names (24 bytes each) from `+0x600`, and the owning character type at `+0xa80`; a
hat goes into the first slot still named `none`. Placing a worn hat (`0x003a3ba0`): for a human whose class has brain
kind 3 (`+0x11a`, the Warriors), the set owned by the type it was created as (`+0xd0`) if there is one, else by its
class (`+0xcc`), and in it the slot named as the hat's object type (`0x00228f40` for the offset, `0x00229028` for the
rotation); a hat in no slot of the set gets a default transform (`0x005116c0`). Every other human uses the hat
model's own attach point (`0x00391828`, `0x00391880`). Confirmed (code). The 908 fittings:
[Hat fittings](../references/hats.md).

**Where a hat comes from.** The character class's string at `+0x14c`
([`CfgChar`](../references/bindings/config.md#cfgchar)'s argument 11, `none` for most classes) is the **hat object's
type name**: when the human's character instance is attached (`Human_AttachInstance` `0x00217a98`, at creation or when
the model streams in later) and that name is not `none`, `Human_SpawnHat` (`0x0024bfc0`) creates the object by name at
the human, adds it to the world's object list pinned, stores its handle at **`+0x364`** and sends it message `0x32`
(wear) with attach point **6**, a zero offset, an identity rotation and whether the human is in a scene.
`HuPlaceHatOnHead` (`0x002385f8`) goes through the same function. Confirmed (code). So a dealer with no script hat still
wears his class's hat when his class names one.

**On the head.** The hat's `0x32` handler (`HatObject_Wear`, `0x003e77c0`, [Script types](script-types.md#hat-object))
zeroes its velocity and spin, attaches it (`Obj_Attach`) to **bone 6, the head** (the head strike sphere's bone,
[Combat](combat/index.md#moving-strikes)), clears its flags `0x6000000`, and writes its local transform in that bone's
frame: the position is the fit offset (`Human_PlaceHat(…, 0xb)`, the set's or the model's, above) **times the human's
scale** (`Human_GetScale`), the rotation the fit rotation (`Human_PlaceHat(…, 0xc)`), unscaled; then it tells the human
(message 3). Confirmed (code). That the attach composes the head bone's matrix with this local translation and then
rotation (bone × T × R), as every attached object does, is inferred.

**Knocked off** (`Human_KnockOffHat`, `0x00258330`), confirmed (code):

- **Never** while human flag `0x10000000000` is set
  ([`HuSetKeepHat`](../references/bindings/character.md#husetkeephat)), without a hat, or when the hat's object kind
  (`+0x86`) is 27.
- **Who calls it**: every knockdown reaction (`Human_PlayReaction` `0x0026a6d0`), except for a brain of type 1, class
  `0x80` and class 13; health running out (`0x002677c8`); being mounted (`Pair_LinkMount` `0x0022c0a0`); the human's
  messages 3 and `0xc2` (not for brain type 1); `Human_ResetToOneHealth`, `Human_ScriptDrop`, the hat object's
  destruction (`Obj_Destroy`), class `0x80` going down (`0x00257ef0`), and picking up another hat (kind 24) in the
  pick-up search (`0x0024d810`). Ordinary hits that do not knock down leave it on.
- **The throw**: by human `+0x668` & 3 (inferred: the side the blow came from) it picks a direction *d* and a spin axis
  *a* from the unit x and y axes (0: *d* = +y, *a* = +x; 1: +x, −y; 2: −y, −x; 3: −x, +y), sets the hat's spin
  (vtable `+0x7c`) to *a* × 8 rad/s and sends it message `0x1c` (drop) with *d* × **2 m/s**. The drop
  (`HatObject_Drop` `0x003e7958`) turns that velocity by the wearer's orientation (inferred from the cross products),
  detaches the hat at its world pose (`Obj_Detach`), gives it flag `0x4000000` (and `0x80` for three models) and
  state 2, updated every 2 ticks: from here it is a loose physics object that falls and lands
  ([Script types](script-types.md#hat-object)). `+0x364` is cleared.
- **Afterwards** it stays where it lands, and may be picked up again. Only when the game state's `+0x35c` is 1 (a mode
  flag, not traced) does it get a respawn timer, by model hash (`+0xc4`): none for the Warriors' hat `0xbbbef927`;
  8000 ms for a player's `0x50e8de39` or `0x17d7194b`; none for the other `0x331fd29`, `0x50e8de39`, `0x17d7194b` and a
  player's `0x1d5ee290`; the game state's `+0x26c` for every other hat. A wearer of brain type 1 or 2 marks the
  hat's `+0x10b` with its type.

### The spinning icon {#spinning-icon}

The marker over a human's head (a mission target's star, a dealer's sign, the co-op player markers, the crown in
Rumble) is a **3D world object of class `dyn_icon`**, attached to the human; it is not a HUD sprite. One per human,
its handle at human `+0x360`. Confirmed (code) at the cited addresses unless marked; not checked at run time. The
dealer's case is also on [AI: the dealer's icon](ai-scripting.md#dealer-icon).

**Making one** (`Human_ShowOverheadIcon`, `0x002271f0`; reached from `HuAttachSpinningIcon` `0x00238a88`,
`GangAttachSpinningIcon` `0x0016b2c8` for each of the gang's 16 member slots, and the game's own callers):

1. The name is the **object type to spawn** (any `dyn_icon` type: `dyn_rockstar`, `dyn_crown`, `dyn_exclamation`,
   `dyn_gangrat`, `dyn_weapdeal`, ...; [the object list](../references/objects.md)). A name whose first 9 characters
   are `dyn_p_one` becomes `dyn_play_one`, and `dyn_p_two` becomes `dyn_play_two`, each with `_euro` appended when
   game state `+0x120` is set (inferred: a European language).
2. When the human already has an icon of that type nothing happens; another icon is removed first (message `0x15`).
3. `Task_CreateObjectByName` spawns it with two message arguments, the human's handle and the **attach bone** (the
   script's third argument; every script and every game caller passes 0, the human's own transform).

**The script arguments** (from the bindings `0x0035aeb0`, `0x0035f128`, `0x0035af80`):

| Function | Arguments |
| --- | --- |
| `HuAttachSpinningIcon(human, name, bone)` | the human's handle; the type name; the pose bone the icon hangs from, 0 = the human |
| `GangAttachSpinningIcon(gang, name, bone)` | as above, for every current member; gang −1 does nothing |
| `HuSetSpinningIconColor(human, steps, colour)` | a **fade**: the step count, then the target colour word; the colour defaults to `0xFFFFFF00` (white, alpha 0) |
| `HuRemoveSpinningIcon(human)` | message `0x15`, the handle cleared (`0x00227388`) |

`HuSetSpinningIconColor`'s two numbers are not two colours, as its argument names in the
[binding reference](../references/bindings/character.md#husetspinningiconcolor) suggest: the message's argument stack
is last in, first out, so `DynIcon_SetColourFade` (`0x003e93d0`) pops the second script argument as the step count
and the third as the target colour. The game's own call, `(human, 3, 0xFFFFFF00)` in `0x0024b2a8`, fades an icon
out in 3 steps. No level script calls it.

**Init** (`DynIcon_Init`, `0x003e8fa0`):

- **Model**: the object type's own model (`Obj_SetModel(obj, 0)`), at its own size: no scale is applied.
- **Attachment**: `Obj_Attach(obj, human, bone)`; its world pose is the human's transform (position and rotation)
  ∘ the bone's (none for bone 0) ∘ the icon's local pose ([World objects: the attachment](objects.md#held)).
  Flags `+0x54` = `0x91`. Update every 2 ticks.
- **Local position**: (0, 0, **2.25**) m; (0, 0, **2.5**) for `dyn_weapdeal`, `dyn_flashdeal` and `dyn_spraydeal`
  (model hashes `0x30f09efb`, `0x51d27ab6`, `0xd939c21d`); the type record's offset (property 11) for `dyn_cross`
  (`0xe3f92003`). The human's transform is at his feet (inferred), so the icon floats 2.25 m above the ground.
  There is **no bob**: nothing moves the local position afterwards.
- **Local rotation**: the quaternion (0, 0, 1, 0) (`0x00511700`), a half turn about z.
- **Spin**: angular velocity (0, 0, **π**) rad/s (`π × (0, 0, 1)`, `0x00511740`): a half turn a second about the
  local z axis, which is the human's up axis, one turn in 2 s. The attached pose advances the local rotation by it
  ([World objects: the attachment](objects.md#held)).
- **No spin** for model hashes `0x8b7fe995`, `0x7644ee5d`, `0x1de2e2ca`, `0x1a72a32d`, `0x687e742b`: the angular
  velocity is zeroed and flag bit 1 cleared. These are the player markers (`dyn_play_one`, `dyn_play_two` and their
  `_euro` forms) and `dyn_lizziestarget` ([AI](ai-scripting.md#dealer-icon)); they keep the half-turn local rotation, so
  they turn with the human.
- **`dyn_cuffs`** (`0x464ac521`) takes the type record's offset and rotation (properties 11 and 12), turns at π/2
  rad/s, and is detached at once, so it stays where it appeared ([Crimes: arrest](crimes.md#arrest)).
- **Colour state** (5 words): not fading, colour **`0xFFFFFFFF`** (white, opaque), the rest 0.

**Colour** (`DynIcon_Update`, `0x003e92e0`, every 2 ticks). The colour is the world object's tint word (`+0xcc`,
copied to `+0xc8` at the next update; `0xRRGGBBAA`), which multiplies the model's colours and its alpha its opacity
([World objects: the tint](objects.md#tint)):

- **Not fading**: `+0xcc` := the stored colour, each update.
- **A fade** (message `0x34`, `DynIcon_SetColourFade`): steps 0 sets `+0xcc` := `0xFFFFFFFF` and stores the colour,
  which the next update writes (not fading, above). Otherwise step := (`+0xc8` − target) ÷ steps, an **unsigned division
  of the whole 32-bit word**, not per channel; each update the counter goes up, and `+0xcc` := `+0xc8` − step until the
  counter reaches the step count, when `+0xcc` := the target and the fade ends. The game only fades the alpha byte,
  where the word arithmetic is a plain linear fade; a port that fades other channels must keep the word arithmetic to
  match. Because the step is truncated, the alpha jumps to the target at the end: from 255 to 0 in 80 steps it falls by
  3 an update to 18, then to 0.

**Hidden** (confirmed (code); the visible result inferred from the tint rule, an alpha under 10 is not drawn):

- **While the letterbox is up**: when not fading and player 1's screen-effects record (`0x005fdeb8`) has the
  letterbox state (`+0x1e8`) set or its level (`+0x1ec`) non-zero ([Screen effects](graphics.md#screen-effects)),
  `+0xcc` := `0xFFFFFF00` and `+0xc8` := `0xFFFFFF01`: alpha 0. Not for `dyn_cross`; not during a fade.
- **Message `0x0a`** (argument 0 hides, non-zero shows) sets or clears the hidden flag (`+0x54` bit 4); **`0x20`**
  sets it and also clears the attached bit (`0x10`); **8** detaches; **`0x15`** removes the icon
  (`DynIcon_OnMessage`, `0x003e9470`).
- **Distance**: an attached object uses its parent's `ObjShow` distance and is not drawn beyond it
  ([World objects: the tint](objects.md#tint)).
- **Removal**: when the human is beaten (`Human_CreditDefeat`, `0x00230740`) or destroyed ([Destroying a
  human](characters/index.md#destroy), message `0x15` to `+0x360`), by the death sequence's camera (which removes the
  players' icons, [Camera](camera.md)), and by the goals that made it.

**Drawing**: as any world object (`ObjectRender_Draw` `0x0017fd78`, [World objects: the tint](objects.md#tint)):
the model with its own materials, lit and Z-tested like the other world objects, the tint multiplying it, alpha
scaled by the distance and fade-in rules there (inferred: nothing in `dyn_icon` changes the draw).

**The co-op player markers** (`0x0024b2a8` at a respawn, `Reticules_UpdateTarget` `0x0024b3d8` at the hints): with
two players on one screen, not in Rumble and not during the letterbox, `dyn_p_one` / `dyn_p_two` (so
`dyn_play_one` / `dyn_play_two`) on the players, then a fade to `0xFFFFFF00`: from alpha 255 over **80** updates (160
on an armies level, `GameState_IsArmiesLevel` `0x0041d110`), or at a respawn from the alpha byte passed in over 3
updates (`+0xc8` and `+0xcc` set to `0xFFFFFF00` | alpha first). [HUD](hud.md) has the context.

## In Coney {#coneys-implementation}

- `src/characters/character_list.*` reads the Character List from `warriors.glr` and finds a record by model name;
  `character_assets.*` loads the three resources by the records' hashes (`"%u"` file names).
- `src/characters/clump_reader.*` and `character_model.*` decode the clump (frames, HAnim, skin, material) and the
  native geometry, with the shared PS2 decoder in `src/graphics/ps2_world_mesh.*`, into merged skinned vertices and
  triangles; no librw outside `src/platform/`.
- `src/characters/character_rig.*` builds the skeleton and the skinning matrices (pose bone `n + 2` · the pose turn
  · the inverse bind matrix) and skins on the CPU.
- `src/platform/character_mesh.*` holds the skinned model as two librw atomics: the first texture-coordinate set
  with the character's texture, and the second set for the material's dual layer. In play
  (`src/platform/play_lighting.*`) every human is drawn in the two passes of
  [Drawing a human](graphics.md#human-draw): the second pass blends the blood texture
  (`src/graphics/human_blood.*`, the three `charblood` textures of `global.pak`) by its alpha below 90% health.
- `--view-character [NAME] [--anim CLIP]` ([Running Coney](../guides/running.md#the-character-viewer)) shows a skinned
  character playing a clip in place on the fixed 30 Hz step, with a pad-driven orbit camera.
- `--render-references DIR` ([Running Coney](../guides/running.md#reference-images)) writes a 256x256
  transparent PNG of every character, posed at its default clip's first frame (and of every object,
  [Level loading](level-loading.md#the-object-list)), from a fixed three-quarter camera
  (`src/characters/reference_render.*` frames and reduces; `src/platform/reference_renderer.*` draws offscreen).
- **Hats** ([Hats](#hats); `src/world_objects/hats.*`, `src/gamemodes/play_level_hats.cpp`): the class's hat when the
  human is made, and `HuPlaceHatOnHead`'s, each a pinned world object on bone 6. A Warrior wears it at his set's
  fitting; anyone else at the hat type's own pose (`CfgObj` arguments 21 and 22). The offset is multiplied by the
  wearer's scale. A hat comes off when a knockdown grounds its wearer (not a cop's, nor class `0x80`'s or 13's unless
  he is out of health), when he is mounted, or when a new hat replaces it, unless he has `HuSetKeepHat` or the hat is
  kind 27. It is thrown at 2 m/s spinning at 8 rad/s, then falls until it rests on the ground. `HuHasHat` gives the
  handle of the hat worn. **Stand-ins**: the side of the blow is not modelled, so every hat leaves behind its wearer
  (side 2); the fall is Coney's own (gravity 9.81 m/s², stopping where it meets the ground); a deleted human's hat goes
  with him; the respawn timers are not applied.
**Disc test** (`[characters]`, counts only): all 543 records load (128 models, 52 character data resources, 507
dictionaries); 150,509 vertices and 155,493 triangles; 1,692 clips and 2,843 resolved ids; the joint mismatch of a
clip's first pose over the [reference pose](formats/animation.md#reference-pose) averages 0.045 m (worst 0.098 m); the
skinned heights run from -0.07 to 4.06 m (2.29 m at most over the bind rotations; which first clips reach higher is
not looked at yet); every model's bind rotation for
bone 2 is 120° from the reference pose's, and 292 of the bones 2-33 over the models are more than 10° from it.

**Coney choices** where the research is silent: the material takes its dictionary's only texture; bones 0 and 1
rest at the identity; weights are used as stored, not renormalised; the viewer's lights, camera and clip keys, and the
reference images' pose, camera and lights, are Coney's own. For the human:

### Notes for implementers {#notes-for-implementers}

- **Characters are skinned with 32 bones** (34 pose entries, [Animation](formats/animation.md)); every character on
  the disc has the same skeleton layout, so one bone mapping serves all.
- **A disc test**: every Character List record resolves to three resources that load, and every model is the clump
  described above (counts only).

## Open questions

- **How the texture reaches the material**, which names none ([Character geometry](#character-geometry)).
- **The rest rotations of pose bones 0-2** and why bone 3's parent in the table differs from its frame's.
- **The vertex colour slot** (zeros in every character checked): what the renderer does with it. The second
  texture-coordinate set is the blood layer's ([Graphics](graphics.md#human-draw)).

## How this was checked

Code claims were read in Ghidra in `SLUS_212.15`. The geometry subsection was corroborated by Coney's decoder over the
128 models the Character List names, counts and hashes only.
