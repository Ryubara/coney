# Animation data format (PS2 disc)

## Scope and sources

How character animation is stored on the NTSC-U PS2 disc: the files and containers that hold clips, the clip
descriptor and keyframe records, how keys are quantised, the 34-bone pose the keys address, the reference pose, the
per-character clip table and the anim range list. How clips are played, blended, chained or faded is out of scope.

Every count below comes from reading `WARRIORS.DIR` and `WARRIORS.WAD` on the PS2 disc image, plus `SLUS_212.15` for
the facts marked "executable". All values are little-endian. Counts of "copies" include every duplicate a pack
carries; "distinct" clips are counted once by their keyframe bytes and descriptor (runtime slots excluded).

## Where clips are stored

### Archive directory (only what is needed to find entries)

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | entry count: 10,701 on the PS2 disc |
| `+0x04` | 12 bytes | zero |
| `+0x10` | 12 bytes per entry | u32 offset into `WARRIORS.WAD`, u32 size, u32 name hash |

`WARRIORS.DIR` is 128,428 bytes, exactly 16 plus 10,701 entries of 12 bytes. The name hash of a standalone
animation file is the CRC-32 of `./ee_files/<name>.anm` in lower case (563 of the 564 `.anm` entries match).

### Resource and chunk headers

Every WAD entry that holds clips is either one resource or a pack of resources. Both start with the same 16-byte
header shape.

| Header | `+0x00` | `+0x04` | `+0x08` | `+0x0c` |
| --- | --- | --- | --- | --- |
| Pack | u32 resource count | u32 entry size minus 16 | u32 zero | u32 hash |
| Resource | u32 chunk count | u32 sum of its chunk data sizes (headers excluded) | u32 zero | u32 resource hash |
| Chunk | u32 chunk type | u32 data size (a multiple of 16 in every clip chunk) | u32 zero | u32 chunk hash |

A resource's chunks follow its header back to back, each a 16-byte chunk header and then its data. A pack's
resources follow the pack header back to back. All 881 packs and 43,143 resources on the disc obey both size rules.

### Containers that hold clips

A clip is always a pair of chunks: type `0x00` (keyframes) immediately followed by type `0x02` (descriptor, 80
bytes). No other chunk type `0x02` size occurs, and no `0x02` appears without a `0x00` before it.

| Container | Chunks | Where | Count |
| --- | --- | --- | --- |
| Animation resource | one `0x00`, `0x02` pair | standalone `.anm` entries | 564 entries, 564 clips |
| Animation resource | one `0x00`, `0x02` pair | inside 225 packs | 2,051 clips |
| Character resource | N pairs, then `0x45`, then `0x08` | standalone entries | 53 entries, 1,694 clips |
| Character resource | N pairs, then `0x45`, then `0x08` | inside 501 packs | 1,156 resources, 26,965 clips |

In all, the disc carries 31,274 clip copies and 1,875 distinct clips. There are 53 distinct character resources,
holding 1 to 594 clips each (594 in `generic_header`, 42 in `warr_cl_header`, 25 in `bopp_bo_header`). A character
resource's hash is the CRC-32 of its name, for example `generic_header`. Level files (`.lev`) hold no clips.

### Names and hashes

| Item | Fact |
| --- | --- |
| Animation resource hash | CRC-32 of `<clip name>.anm` as the tools spelled it (2,614 of 2,615 animation resources) |
| Keyframe chunk (`0x00`) hash | CRC-32 of `<clip name>.anm.b.cnk` (25,723 of 31,274 copies; the rest have a cut name) |
| Descriptor chunk (`0x02`) hash | not matched to any spelling yet |
| Clip name | the descriptor's name field, see below |

## Clip descriptor (chunk 0x02, 80 bytes)

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | runtime header slot: `0x0045de40` in every copy on the disc, replaced on load |
| `+0x04` | f32 | root displacement over the whole clip along x (metres) |
| `+0x08` | f32 | root displacement over the whole clip along y (metres); +y is forward |
| `+0x0c` | f32 | duration in seconds; always a whole number of 1/30 s frames |
| `+0x10` | u16 | size in bytes of section A (root velocity); 0 in 957 distinct clips |
| `+0x12` | u16 | size in bytes of section B (root translation); never 0 |
| `+0x14` | u32 | size in bytes of section C (bone rotations) |
| `+0x18` | u16 | rotation channel count; equals the number of bits set in the bone mask in every copy |
| `+0x1a` | u16 | event count |
| `+0x1c` | u32 | runtime slot for the keyframes' address; leftover values on the disc |
| `+0x20` | 5 bytes | bone mask: bit b of the 40-bit little-endian value set when bone b has a rotation channel; only bits 0-33 occur |
| `+0x25` | 30 bytes | the clip name, a zero byte, then the start of `anm.b.cnk`, cut at 30 bytes |
| `+0x43` | u8 | runtime use count; 0 in every copy |
| `+0x44` | u32 | clip flags (meaning belongs to playback); 0 in 12,103 copies, `0x20000` in 5,208, `0x20004` in 4,086 |
| `+0x48` | u32 | not decoded; 0 in 11,378 copies, 898 distinct values |
| `+0x4c` | u32 | not decoded; 0 in 12,383 copies, the bits of 1.0f in 3,335 |

The three section sizes are multiples of 8 in every clip. Frame count equals duration times 30 and ranges from 1
(`bum_trans_combat_to_neutral`, 0.0333 s) to 680 (`gen_smoke_loop`, 22.667 s).

## Keyframe chunk (chunk 0x00)

### Layout

| Part | Size | Content |
| --- | --- | --- |
| Section A | descriptor `+0x10` | root velocity keys (one channel), absent when the size is 0 |
| Section B | descriptor `+0x12` | root translation keys (one channel) |
| Section C | descriptor `+0x14` | one rotation channel per set mask bit, in ascending bone order |
| Events | 24 bytes times descriptor `+0x1a` | event records |
| Padding | 0 or 8 bytes | brings the chunk to a multiple of 16 bytes |

Channels are stored one after another. A channel starts at a key whose frame delta is 0; every other key has a
delta of 1 or more. Section C holds exactly as many delta-0 keys as the descriptor's channel count in every clip.

### Key record (8 bytes)

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u8 | frames since the channel's previous key (0 for its first key, at most 255) |
| `+0x01` | u8 | no observed meaning; zero in every key of 549 distinct clips, noise-like elsewhere |
| `+0x02` | i16 | value x |
| `+0x04` | i16 | value y |
| `+0x06` | i16 | value z |

In every channel of every clip the deltas add up to the clip's frame count: the first key sits on frame 0 and the
last on the final frame, so an N-frame clip spans N + 1 frame positions. Keys may skip frames; only 3,650 of 47,201
rotation channels hold a key on every frame.

### Encodings and scales

| Section | Stored values | Decoded value |
| --- | --- | --- |
| A, root velocity | x, y, z | x and y in metres per second as value / 1023; z as value / 2047 |
| B, root translation (bone 1) | x, y, z | x and y in metres as value / 1023; z in metres as value / 2047, plus the f32 at `+0x04` of the character's chunk `0x08` (0.0 on the disc) |
| C, rotation | x, y, z of a unit quaternion | each value / 32767; w rebuilt as the non-negative square root of 1 - x² - y² - z² |

The same position scales apply to sections A and B. Bone 0's rotation channel, present in 387 distinct clips, holds
the root's turn per 1/30 s frame as a quaternion; bone 1's translation is section B. Axes: z is up and +y is the
character's forward direction.

Worked examples from the disc:

| Clip | Frames | Stored keys | Decoded |
| --- | --- | --- | --- |
| `wheelchair_run` | 20 (0.667 s) | A: (0, 6138, 0) at frames 0 and 20 | 6.0 m/s forward; 4.0 m, as the descriptor's displacement (0, 4.0) |
| `wheelchair_walk` | 30 (1.0 s) | A: (0, 2046, 0) | 2.0 m/s; 2.0 m, as the descriptor |
| `warr_re_walk` | 35 (1.1667 s) | A: (0, 1667, 0) on 32 keys; B first key (0, 0, 2126) | 1.6295 m/s, 1.901 m (descriptor 1.9008 m); hips 1.039 m up |
| `wheelchair_run` | 20 | B: (0, 0, 1285) | hips 0.628 m up (seated) |

For 391 of the 590 distinct clips that move at least 0.3 m without turning, integrating section A matches the
descriptor's displacement to within 1 %. Most of the rest are paired grab, mount and tandem clips.

### Event record (24 bytes)

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u16 | frame of the event (never past the clip's frame count; 9,468 events checked) |
| `+0x02` | u16 | event type; values 7 to 75 seen, 11 the most common (4,287) |
| `+0x04` | 8 bytes | parameters (not decoded) |
| `+0x0c` | char[12] | a name, usually a sound name cut to 11 characters (`footstep`, `shuffle`, `footstep_ru`); empty in 2,786 events |

`warr_re_walk` holds two type-11 `footstep` events, at frames 12 and 30.

## Bones and the pose

### Bone numbering and hierarchy (executable)

The pose has 34 bone slots, numbered as the mask bits 0-33. The parent table is held in `SLUS_212.15`, not in any WAD
file.

| Bone | Parent | Bone | Parent | Bone | Parent | Bone | Parent | Bone | Parent |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | none | 7 | 6 | 14 | 6 | 21 | 19 | 28 | 2 |
| 1 | 0 | 8 | 6 | 15 | 6 | 22 | 5 | 29 | 28 |
| 2 | 1 | 9 | 8 | 16 | 5 | 23 | 22 | 30 | 29 |
| 3 | 1 | 10 | 6 | 17 | 16 | 24 | 23 | 31 | 2 |
| 4 | 3 | 11 | 10 | 18 | 17 | 25 | 24 | 32 | 31 |
| 5 | 4 | 12 | 6 | 19 | 18 | 26 | 25 | 33 | 32 |
| 6 | 5 | 13 | 12 | 20 | 19 | 27 | 25 | | |

Shape of the tree: bone 0 is the root (turn and velocity), bone 1 the hips (translation from section B), bones 3-6
a chain up to bone 6 with seven children (7, 8, 10, 12, 14, 15 and the chain 8-9, 10-11, 12-13), two mirrored
six-bone chains 16-21 and 22-27 hang from bone 5, and two mirrored three-bone chains 28-30 and 31-33 from bone 2.

### Bone mask frequencies (copies, out of 31,274)

| Bones | Copies with a channel |
| --- | --- |
| 1 | 31,274 (every clip) |
| 3, 4, 6, 16-19, 22-24, 28-31, 33 | 31,133 |
| 0 | 10,906 |
| 2 | 19,780 |
| 7-15 | 5,215 to 13,473 |

121 copies of one clip carry a single channel (bone 1 only). Channel counts range from 1 to 34.

### Reference pose (executable)

Bones without a channel in a clip take this rotation (quaternion x, y, z, w; values rounded to 4 places). The table
is held in `SLUS_212.15`. Bone 0's identity means "no turn".

| Bone | x | y | z | w |
| --- | --- | --- | --- | --- |
| 0 | 0.0000 | 0.0000 | 0.0000 | 1.0000 |
| 1 | -0.4912 | -0.5087 | 0.4912 | 0.5087 |
| 2 | -0.5000 | 0.5000 | -0.5000 | 0.5000 |
| 3 | 0.4793 | -0.5199 | 0.5199 | -0.4793 |
| 4 | 0.0000 | -0.0298 | 0.0000 | 0.9996 |
| 5 | 0.0000 | -0.1757 | 0.0000 | 0.9844 |
| 6 | 0.0000 | 0.1172 | 0.0000 | 0.9931 |
| 7 | 0.5274 | -0.4710 | 0.4710 | 0.5274 |
| 8 | 0.6590 | 0.0000 | 0.7521 | 0.0000 |
| 9 | 0.0000 | -0.2983 | 0.0000 | 0.9545 |
| 10 | 0.6081 | -0.5780 | 0.3635 | 0.4050 |
| 11 | 0.0000 | 0.6425 | 0.0000 | 0.7663 |
| 12 | 0.4043 | -0.3602 | 0.5801 | 0.6085 |
| 13 | -0.0036 | -0.6166 | 0.0054 | 0.7873 |
| 14 | 0.7393 | 0.0000 | 0.6733 | 0.0000 |
| 15 | 0.5247 | -0.4805 | 0.4743 | 0.5186 |
| 16 | 0.6200 | -0.7643 | -0.1281 | 0.1227 |
| 17 | 0.0452 | -0.0282 | 0.1112 | 0.9924 |
| 18 | 0.0000 | 0.1174 | 0.0000 | 0.9931 |
| 19 | -0.6951 | -0.0567 | 0.0262 | 0.7162 |
| 20 | -0.0004 | -0.2858 | 0.0001 | 0.9583 |
| 21 | -0.0811 | -0.3223 | -0.0030 | 0.9431 |
| 22 | -0.6200 | -0.7643 | 0.1281 | 0.1227 |
| 23 | -0.0452 | -0.0282 | -0.1112 | 0.9924 |
| 24 | 0.0000 | 0.1174 | 0.0000 | 0.9931 |
| 25 | 0.6951 | -0.0567 | -0.0262 | 0.7162 |
| 26 | 0.0004 | -0.2858 | -0.0001 | 0.9583 |
| 27 | 0.0811 | -0.3223 | 0.0030 | 0.9431 |
| 28 | -0.0066 | -0.1154 | 0.9910 | -0.0678 |
| 29 | 0.0000 | 0.0340 | 0.0000 | 0.9994 |
| 30 | -0.0030 | -0.0194 | -0.0700 | 0.9974 |
| 31 | -0.0066 | 0.1154 | 0.9910 | 0.0678 |
| 32 | 0.0000 | 0.0340 | 0.0000 | 0.9994 |
| 33 | 0.0030 | -0.0194 | 0.0700 | 0.9974 |

Every entry is a unit quaternion. Unlike decoded clip keys, four entries (bones 3, 8, 14, 28) have w at or below 0.

### Bone transforms

| Bone | Translation | Rotation |
| --- | --- | --- |
| 0 | root velocity (section A) | root turn (bone 0 channel, else identity) |
| 1 | section B | bone 1 channel |
| 2-33 | a fixed offset from the parent, from the character's model data, not from the clip | own channel, else the reference pose |

Each bone's rotation and offset are relative to its parent in the table above. No clip stores translation keys for
bones 2-33.

## Character clip table (chunk 0x08, 2,912 bytes)

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x000` | u32 | runtime header slot: `0x00436b10` in all 53 characters, replaced on load |
| `+0x004` | f32 | height added to section B's z; 0.0 in all 53 |
| `+0x008` | i32[722] | one entry per anim id 0-721: a clip index, or -1 for none |
| `+0xb50` | 16 bytes | runtime slots (the anim range list's count and address after load); the same bytes in all 53 |

A clip index v names the clip that is v-th counting back from the resource's last clip pair: index 0 is the last
clip, index N - 1 the first. The check holds for all 722 ids of `generic_header` against the clip names. Every clip
of every character is named by at least one id, several ids may share a clip, and `generic_header` has no -1
entries.

| Anim id | `generic_header` index | Clip | Frames |
| --- | --- | --- | --- |
| 0 | 117 | `gen_charge_shoulder` | 20 |
| 1 | 174 | `gen_dive` | 46 |
| 2 | 494 | `gen_tackle_miss` | 44 |
| 25 | 478 | `gen_snap_right1` | 12 |
| 721 | 54 | `face_speech_excited_small` | 30 |

## Anim range list (chunk 0x45, 11,568 bytes)

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x000` | u32 | record count: 722 in all 53 characters (one per anim id) |
| `+0x004` | 16 bytes per record | the records, in anim id order |
| `+0x2d24` | 12 bytes | `71 01 00 00 00 00 00 00 04 00 00 00` in all 53 |

| Record offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | i16 | offset x in millimetres |
| `+0x02` | i16 | offset y in millimetres (+y forward) |
| `+0x04` | f32 | reach in metres; non-zero in all 38,266 records |
| `+0x08` | i16 | a second distance in millimetres; 122 to 3,000 |
| `+0x0a` | i16 | damage value; -60 to 3,000, 0 in 29,984 records |
| `+0x0c` | u16 | not decoded; 0 to 59 |
| `+0x0e` | u16 | not decoded; 0 or a multiple of 256 (256 to 8,192) |

The offset's length is 1,000 mm in 37,897 records; the other lengths seen are 150 to 950 mm.

| `generic_header` id | Offset (mm) | Reach (m) | Distance (mm) | Damage | `+0x0c` | `+0x0e` |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | (0, 1000) | 3.2864 | 2250 | 20 | 54 | 0 |
| 1 | (38, 999) | 2.2177 | 3000 | 30 | 58 | 0 |
| 25 | (999, -12) | 0.9522 | 1299 | 10 | 27 | 1024 |
| 27 | (-1000, 0) | 0.8172 | 1299 | 10 | 25 | 1024 |
| 29 | (-39, -999) | 0.9626 | 1299 | 10 | 27 | 1024 |

## Value ranges across the disc (1,875 distinct clips)

| Item | Range |
| --- | --- |
| Section A values | x -15,754 to 13,049; y -14,151 to 24,548; z -1,127 to 8,129 |
| Section B values | x -862 to 817; y -1,550 to 1,776; z -2,308 to 5,503 |
| Section C values | -32,765 to 32,767 in each component |
| x² + y² + z² of a rotation key | up to 1.00004; 1,121 of 860,130 keys are just above 1 |
| Frame delta | up to 172 in A, 255 in B and C |
| Events per clip | 0 to 143 (`accoustic`); 9,468 in all |
| Most common frame counts | 20 (122 clips), 30 (112), 10 (77), 25 (67), 35 (60) |
| Longest clips | `gen_smoke_loop` 680 frames, `brkdance_ctr` 649, `sit_on_stair` 386 |

## Sampled pose between keys

A moment is a time into a clip. Below it is given in seconds and also in frames of 1/30 s, so a moment can fall on
a whole frame or between two. Values are at playback rate 1 except where a rate is named. The facts come from the
clips on the PS2 disc and from `SLUS_212.15` (executable).

### Keys a moment draws on

At every moment each channel uses two of its own keys. Through most of a span these are the keys on either side of
the moment, but the pair moves on half a frame before each key rather than on the key itself.

| Moment | Keys it uses | Where the value lies |
| --- | --- | --- |
| on a key's frame | that key and the channel's next key | exactly on that key |
| after a key, up to half a frame before the next key | the two keys either side of the moment | between them, as far along as the moment is between their frames |
| the last half frame before a key, that half-frame point included | that key and the key after it | just short of that key, on the side away from the key after it: the later span carried back by up to half a frame |

- In that last half frame the value is not a mix of the two keys either side of the moment.
- A clip is sampled from its start up to, not including, half a frame before its final key. The final key's own
  value is therefore not shown at any sampled moment.
- Section A, section B and every rotation channel follow the same pattern. Each channel uses its own keys, so at one
  moment two channels may draw on keys on different frames.

### In-between values

Values are decoded with the scales in "Encodings and scales".

| Channel | Value at a moment |
| --- | --- |
| Bones 1-33 with a channel (section C) | a unit quaternion on the shorter arc between the two keys; at the moment half way between the keys the bone shows exactly half the turn, and it turns faster through the middle of the span than near the keys |
| Bones 2-33 without a channel | the reference pose, the same at every moment |
| Root turn, bone 0 channel | the earlier key's turn, unchanged for the whole span: no in-between value |
| Root turn, no bone 0 channel | identity (no turn) |
| Root velocity (section A) | on the straight line between the two keys' values, as far along as the moment is between them, scaled in proportion to the playback rate |
| Root velocity, no section A (957 distinct clips) | not taken from the clip (see open questions) |
| Root translation (section B) | on the straight line between the two keys' values, as far along as the moment is between them; the height also includes the value at `+0x04` of the character's chunk `0x08` (0.0 on the disc) |

Effect of the playback rate on the root values (body bones and section B do not depend on it):

| Rate | Root velocity | Root turn per 1/30 s frame |
| --- | --- | --- |
| normal speed | as decoded | the key's turn |
| slower than normal | smaller in proportion to the rate | the key's turn, smaller in proportion to the rate, about the same axis |
| faster than normal | larger in proportion to the rate | the key's full turn, no larger |

### Worked examples

Stored values are (x, y, z) as on the disc.

| Clip | Moment | Channel | Keys used: frame (stored x, y, z) | Where the value lies | Value at the moment |
| --- | --- | --- | --- | --- | --- |
| `warr_re_walk` | 1.000 s (frame 30) | section B | 29 (31, -20, 2101), 32 (16, -9, 2110) | a third of the way from frame 29 to frame 32 | hips at (0.0254, -0.0160, 1.0278) m |
| `warr_re_walk` | 1.050 s (frame 31.5) | section B | 32 (16, -9, 2110), 35 (0, 0, 2126) | half a frame short of frame 32, on the line from 32 to 35 carried back | hips at (0.0182, -0.0103, 1.0295) m |
| `warr_re_walk` | 0.700 s (frame 21) | bone 17 | 20 (4222, 3332, 16036), 22 (3765, 2637, 16150) | half way | (0.1219, 0.0911, 0.4912, 0.8577): 1.47° from key 20, half the 2.95° between the keys |
| `gen_combat_turn_right120` | 0.1497 s (frame 4.49) | section A | 4 (1475, -194, 0), 5 (1142, 67, 0) | just under half way | (1.2823, -0.0646, 0) m/s |
| `gen_combat_turn_right120` | 0.150 s (frame 4.5) | section A | 5 (1142, 67, 0), 6 (444, 113, 0) | half a frame short of frame 5, on the line from 5 to 6 carried back | (1.4575, 0.0430, 0) m/s, faster in x than any stored key (largest 1475, 1.4418 m/s) |
| `gen_combat_turn_right120` | 0.1667 s (frame 5) | section A | 5 (1142, 67, 0), 6 (444, 113, 0) | on key 5 | (1.1163, 0.0655, 0) m/s |
| `gen_combat_turn_right120` | before 1/60 s (the opening half frame) | bone 0 | 0 (0, 0, 0), 1 (0, 0, -3114) | held at key 0 | no turn |
| `gen_combat_turn_right120` | 1/60 s to the end | bone 0 | 1 (0, 0, -3114), 11 (0, 0, -3114) | held at key 1 | 10.91° per frame about -z, a turn to the right |
| `gen_run_180_turn` | 0.0467 s (frame 1.4) | bone 0 | 1 (0, 0, 1330), 2 (0, 0, 1138) | held at key 1 | 4.65° per frame about +z |
| `gen_run_180_turn` | 0.050 s to 0.1163 s (frames 1.5 to 3.49) | bone 0 | 2 (0, 0, 1138), 4 (0, 0, 1330) | held at key 2 | 3.98° per frame about +z |
| `gen_run_180_turn` | 0.1167 s (frame 3.5) | bone 0 | 4 (0, 0, 1330), 5 (0, 0, 1714) | held at key 4 | 4.65° per frame about +z |

`bopp_bo_combo_sss`, bone 20, holds consecutive keys on frames 24 (699, 32510, -891) and 31 (-12, -9090, 4), 161.6°
apart. At the whole frames between them the bone has turned this far from the frame 24 key:

| Frame | 25 | 26 | 27 | 28 | 29 | 30 | 31 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Turned from frame 24 | 18.2° | 40.7° | 66.9° | 94.7° | 120.9° | 143.4° | 161.6° |
| Turned in that frame | 18.2° | 22.5° | 26.2° | 27.7° | 26.2° | 22.5° | 18.2° |

### What a viewer sees

- Body bones and the hips move through each span along one arc or one straight line, so motion changes direction at
  keys rather than curving through them. 243,329 of the 809,310 body-bone key pairs on the disc (1,875 distinct
  clips) span more than one frame.
- Within one span a bone turns faster through the middle than near its two keys, and the moment half way between
  the keys shows exactly half the turn. The difference grows with the angle between the keys: the middle is 0.2 %
  faster at 10°, 1.7 % at 30°, 17 % at 90° and 72 % at the 161.6° of `bopp_bo_combo_sss` above. 21,056 body-bone
  key pairs are more than 30° apart and 564 more than 90°.
- A bone never swings the long way round between two keys. 7,645 body-bone key pairs, in 3,334 channels, are stored
  pointing into opposite halves of the quaternion space, and these too turn by the short arc.
- Half a frame before a key, where the pair changes, the pose jumps by about half the change in per-frame motion at
  that key. Over the 762,496 such points in body-bone channels the jump is below 0.74° at half of them and above 18°
  at 1 %. In `warr_re_walk` the hips drop 0.57 mm at 1.05 s, too little to see; in `gen_combat_turn_right120` the
  root velocity rises from 1.282 to 1.457 m/s in x at 0.15 s.
- In that last half frame values can lie outside the range of every stored key, as in the 0.150 s example above.
- The root's turn speed is flat across each span and changes in one jump half a frame before each new key: in
  `gen_run_180_turn` it falls from 4.65° to 3.98° per frame at 0.050 s and rises back at 0.1167 s. 387 distinct
  clips carry a root turn channel.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x001045e0` | Make a playable clip from a keyframe and descriptor chunk pair | Clip descriptor (chunk 0x02, 80 bytes) |
| `0x00104ce0` | Sample a clip into a 34-bone pose at a given time | Sampled pose between keys |

## Acceptance checks

- The PS2 disc's `WARRIORS.DIR` is 128,428 bytes and lists 10,701 entries.
- The disc carries 31,274 clip copies, 1,875 distinct clips, 564 standalone `.anm` entries and 53 distinct character
  resources.
- Every clip descriptor is 80 bytes, starts with `0x0045de40`, and its channel count equals the bits set in its bone
  mask.
- In every channel of every clip the frame deltas add up to the clip's duration times 30.
- Every keyframe chunk's size equals sections A + B + C plus 24 bytes per event, rounded up to a multiple of 16.
- `warr_re_walk` is stored as 35 frames with a duration of 1.1667 s and a root displacement of 1.90 m forward.
- `wheelchair_run` is 20 frames (0.667 s) and moves the chair 4.0 m forward per cycle, a speed of 6.0 m/s.
- `gen_smoke_loop` is the longest clip: 680 frames, with a stored duration of 22.667 s.
- `bass_heavy` (standalone `bass_heavy.anm`) is 70 frames (2.333 s) with 23 rotation channels and 72 events.
- `warr_re_walk` raises two `footstep` events per cycle, on frames 12 and 30 (0.4 s and 1.0 s in).
- In `generic_header`, anim id 0 resolves to `gen_charge_shoulder` (20 frames) and id 2 to `gen_tackle_miss`
  (44 frames).
- Every character's chunk `0x08` is 2,912 bytes and its chunk `0x45` is 11,568 bytes with a count of 722.
- `generic_header`'s anim range record for id 0 holds offset (0, 1000) mm, reach 3.2864 m, distance 2,250 mm and
  damage 20.
- A clip that has no channel for a bone shows that bone in the reference pose, the same for every character.

## Open questions

- Byte `+0x01` of every key: no meaning found; it is not extra precision for any value.
- Descriptor `+0x48` and `+0x4c`: not decoded; their link to the clip flags at `+0x44` is unknown.
- The descriptor chunk's hash spelling is unknown.
- Event fields `+0x02` (type) and `+0x04` (parameters) are not decoded here; their meaning belongs with playback.
- Anim range record fields `+0x0c` and `+0x0e` are not decoded.
- Which chunk of the character model supplies the fixed bone offsets for bones 2-33 is not confirmed.
- Section A's z component: whether any clip relies on vertical root velocity is not checked.
- What a character's -1 clip table entries resolve to once the character is loaded: they may take another loaded
  character's clip for the same anim id. The entry point that loads a character's clip table and anim range list is
  left off the entry points until this is confirmed.
- The Xbox disc's animation storage has not been compared with the PS2's.
- Names for the bone slots (hips, spine, arms, legs, face) are inferred from the tree shape only.
- The root velocity of the 957 distinct clips without section A is not taken from the clip; which value the pose
  holds for them is not confirmed.
- Which moments the game samples on each displayed frame is not confirmed. The half-frame effects in "Sampled pose
  between keys" show only for moments that fall in the last half frame before a key.
- Sampled moments stop half a frame short of a clip's final key. Whether a looping clip's cycle therefore lasts 1/60 s
  less than its duration (1.15 s rather than 1.1667 s for `warr_re_walk`) is not confirmed.
- The root turn keys of `gen_combat_turn_right120` on frames 1-11 add up to 120.0°, but the keys in force at whole
  frames 0-10 add up to 109.1° (179.8° against 179.1° in `gen_run_180_turn`). How far a full play-through turns the
  character depends on playback and is not confirmed.
