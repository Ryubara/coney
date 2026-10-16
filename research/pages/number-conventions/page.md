# Number conventions

The conventions a reimplementation must share with The Warriors (PS2, NTSC-U) so that positions, angles, colours
and random outcomes come out the same. Facts come from the NTSC-U disc (`WARRIORS.DIR`, `WARRIORS.WAD` and the
executable `SLUS_212.15`, SHA-1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`, read as data) and from ten PCSX2
savestates of the same game. The disc counts below come from the standalone entries of the WAD: 1,477 model
resources, 564 animation clips, 1,877 8-bit textures and 37 object lists. Formats themselves are on the published
pages model-format and animation-format; this page only states the conventions their numbers follow.

## World axes and units

| Convention | Value | Evidence on the disc |
| --- | --- | --- |
| Up axis | +Z | The 10,301 object-list placements span a median of 12.1 m in z per list, against 250.9 m in x and 165.7 m in y; 8,738 of their rotations turn about z alone |
| Ground plane | x and y | Clip root displacements are stored as x and y only; the downward ground probe direction in the executable's data is (0, 0, -1) |
| Handedness | right-handed: x cross y gives z | Every model frame has a positive determinant; 7,969 of 7,973 frames satisfy right cross up = at |
| Character forward | +Y of the character's own frame | Forward walk and jog clips move along +y: `gen_walk_dunno` 3.775 m in 2.33 s, `gen_jog` 3.238 m in 0.67 s, `cop_lost_target_walk_search` 3.872 m in 2.47 s, each with x below 0.01 m |
| Character right | +X of the character's own frame | Follows from up +Z, forward +Y and a right-handed system |
| Length | metres | Clip root heights have a median of 1.05 m; skinned skeletons measure a median of 1.75 m from ankle to head |
| Time | seconds | Clip durations are stored in seconds (0.033 s to 22.667 s on the disc) |
| Animation frame | 1/30 s | Every one of the 564 clip durations is a whole number of 30ths of a second |
| Game frame rate | 30 frames a second on NTSC | The averaged frame-rate reading in RAM is 30.0 in 9 of 10 savestates |

## Angles and headings

| Convention | Value |
| --- | --- |
| Unit | radians throughout the game's own angle values |
| Heading range | 0 inclusive to 2 pi exclusive |
| Heading 0 | facing +Y |
| Heading pi/2 | facing +X |
| Positive heading direction | clockwise seen from above (from +Y towards +X), so a larger heading is a turn to the right |
| Direction for heading h | (sin h, cos h, 0) |
| Signed angle range | above -pi up to and including pi, for angles measured counter-clockwise from +X |
| Relation to rotations about +Z | a positive rotation about +Z by angle a turns the facing counter-clockwise, so the heading changes by minus a |
| Difference of two headings | reported as an unsigned value from 0 to pi (the shorter way round) |

Edge outcomes of heading:

- A direction of zero length has heading pi/2 (facing +X).
- A direction whose y part is within 0.0000005 of zero has a heading of exactly pi/2 (x positive or zero) or
  exactly 3 pi/2 (x negative).
- Headings agree with an exact arctangent to within about 0.0000001 radians, far below anything visible.

## Quaternions and matrices

| Data | Convention | Disc evidence |
| --- | --- | --- |
| Quaternion component order | x, y, z, w (w last) | Object lists store `{x, y, z, w}`; 779 identity rotations are written (0, 0, 0, -1) |
| Quaternion meaning | unit axis a and angle t give (a sin t/2, cos t/2); positive angles turn counter-clockwise about the axis (right-hand rule) | The identity quaternion in the executable's data is (0, 0, 0, 1) |
| Sign of w | either sign; q and -q are the same rotation | 8,328 of 10,301 object-list rotations have negative w |
| Object-list rotations | unit length | 10,244 of 10,301 are unit length within 0.001 |
| Animation rotation keys | x, y, z stored as signed 16-bit values in units of 1/32767; w is the non-negative value that makes the quaternion unit length | 300,579 rotation keys in the 564 clips |
| Animation position keys | x and y in units of 1/1023 m, z in units of 1/2047 m | Root heights near 1.05 m (2,121 / 2,047 in the first clip, `accoustic`) |
| Frame matrix layout | three axis rows right, up, at, then a position row; each row is that axis (or the origin) in the parent's space | 7,973 frames in 1,477 model resources |
| Point transform | a point is a row vector: parent point = local point times the 3x3 rotation, plus the position | Skin matrices invert the frame transforms under this reading |
| Rotation matrix from a quaternion | the rows are the quaternion's rotated x, y and z axes; position zero | Matches the frame-row convention above |
| Skin inverse bind matrix | four rows of four floats: right, up, at, position; the fourth float of each row is not a matrix entry | All four fourth floats are 0 in 4,608 of 4,896 matrices; the rest hold unrelated values |
| Skin matrix handedness | positive determinant | All 4,896 inverse bind matrices of the 153 skinned models |

Character skeletons are built in their own bone space: in the bind pose of all 153 skinned models the spine runs
along +x of the root (head near +0.77 m), the legs along -x (ankles near -1.0 m) and the arms along +z and -z. The
upright pose in the world comes from the clip's rotation keys and root position, not from the bind frames.

Four frames (one in each of entries 1,423, 1,994, 2,492 and 2,844) are slightly sheared rather than pure rotations,
with determinants of 0.992 to 0.994.

## Colours

| Data | Format | Range | Disc evidence |
| --- | --- | --- | --- |
| Texture palettes | 32-bit RGBA, one byte a channel | colour 0 to 255; alpha 0 to 128, where 128 is fully opaque | 1,877 8-bit textures: no palette alpha above 128; 390,575 of 480,512 entries are exactly 128 |
| Material colours | RGBA, one byte a channel | 0 to 255, 255 is full | 1,859 materials: 1,801 are (255, 255, 255) and 1,833 have alpha 255 |
| Material surface values | three floats (ambient, specular, diffuse) | 0 to 1 | 1,445 materials hold (1, 0, 1) |

A reimplementation that draws palette alpha on a 0 to 255 scale has to double the stored value (128 becomes 255).

## Random sequences

| Fact | Value |
| --- | --- |
| Sequences | ten, each 32-bit position stored 8 bytes apart in RAM from `0x006eb870` to `0x006eb8b8` |
| Raw value | an unsigned 32-bit number |
| Cycle length | 1,024 draws: draw 1,025 of a sequence equals draw 1 |
| Values in a cycle | 1,024 different values; all ten sequences share the same cycle and differ only in position |
| Smallest and largest raw values | `0x001062af` and `0xffe0f5d8` |
| Fraction draws (0 to 1) | never below 0.000250 and never above 0.999527; 0 and 1 never occur |
| Fraction draws (-1 to 1) | the same relative place between -1 and 1 as the 0 to 1 fraction; never -1 or 1 |
| Whole-number draws between two limits | both limits can occur; results are never negative for a range starting at 0 |
| Values used per draw | one per whole number or fraction; three for a random point in a box (one per axis, in x, y, z order) |
| Empty range | a whole number below 0 is given as 0 and uses no value |
| Separation | sound has its own sequence; the others serve separate areas such as AI, the camera, combat and visual effects, so draws in one area never move another area's sequence |

Over one full cycle the outcomes are not evenly spread. Whole numbers 0 to 99 occur from 3 to 23 times each, and
the faces of a 1 to 6 draw occur 170, 165, 174, 173, 182 and 160 times. The mean of a fraction draw over a cycle is
0.49997.

## Starting positions

| Sequence | Start-up position | Outcome |
| --- | --- | --- |
| Nine of the ten (all but sound) | 0 at power-on, never set again | Every run gives the same values in the same order; the first draw of each is draw 1 of the table below |
| Sound (`0x006eb8b0`) | the console's millisecond clock (counted from power-on) when the sound system starts, taken modulo 1,024 | Sound variety (ambient emitters, line choice, random volume) differs from run to run |

All ten positions read from the ten savestates lie between 0 and 1,023.

## First draws

The first twelve draws from a sequence at start-up position 0, with the outcome of the common draw kinds. Fractions
are given to six decimals.

| Draw | Raw 32-bit value | Fraction 0 to 1 | Fraction -1 to 1 | Whole number 0 to 99 | Whole number 1 to 6 |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | `0xffc27482` | 0.999061 | 0.998122 | 90 | 3 |
| 2 | `0x82a6958b` | 0.510354 | 0.020709 | 39 | 6 |
| 3 | `0x21ac240a` | 0.131533 | -0.736934 | 46 | 3 |
| 4 | `0x09110bba` | 0.035416 | -0.929167 | 58 | 5 |
| 5 | `0xfe127bc0` | 0.992469 | 0.984939 | 92 | 3 |
| 6 | `0xa02e7212` | 0.625709 | 0.251417 | 18 | 3 |
| 7 | `0x10060698` | 0.062592 | -0.874816 | 60 | 1 |
| 8 | `0x692452f5` | 0.410710 | -0.178579 | 13 | 4 |
| 9 | `0x2280870e` | 0.134774 | -0.730453 | 26 | 3 |
| 10 | `0xfb1a7281` | 0.980872 | 0.961745 | 65 | 2 |
| 11 | `0xd65f6aa9` | 0.837393 | 0.674787 | 49 | 6 |
| 12 | `0xf72cdf2e` | 0.965528 | 0.931057 | 70 | 3 |

A fraction drawn between two other limits lands at the same relative place between them as the 0 to 1 fraction:
draw 1 between 2 and 4 gives 3.998122.

## Precision effects

| Effect | What a viewer or tester sees |
| --- | --- |
| Animation rotation resolution | each quaternion component moves in steps of 1/32767, about 0.0035 degrees: not visible |
| Animation position resolution | about 1 mm horizontally and 0.5 mm vertically; root positions reach at most about 32 m horizontally and 16 m vertically from the clip origin |
| Near half-turn rotation keys | 312 of 300,579 keys store x, y, z whose squares add up to slightly more than 1 (by at most 0.00005); the PS2 gives these a tiny positive w (at most about 0.0065) instead of 0, a difference that cannot be seen |
| Sign flips in keys | keys near a half turn can flip all signs between neighbours (q to -q) while the pose stays continuous |
| Frame timing | the game's frame-rate reading in RAM shows 29.412 or 30.303, which are 34 ms and 33 ms frames: frame times are counted in whole milliseconds |
| Float rounding | PS2 single-precision results round towards zero; results can differ from round-to-nearest hardware in the last bit, with no visible effect found |
| Random fraction extremes | no fraction draw lands within 0.00025 of 0 or within 0.00048 of 1 |

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x003353f0` | Pick a random whole number between two limits | First draws |
| `0x00335460` | Pick a random fraction between two limits | First draws |
| `0x00335608` | Put the random sequences at their start-up positions | Starting positions |
| `0x00335d98` | Give the heading of a direction | Angles and headings |
| `0x00336500` | Build a rotation matrix from a quaternion | Quaternions and matrices |

## Acceptance checks

- The executable `SLUS_212.15` on the NTSC-U disc has SHA-1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`.
- The 564 standalone animation clips have durations that are whole multiples of 1/30 s, from 0.033 s to 22.667 s.
- In every clip, the frame steps of each rotation channel add up to the clip's duration times 30 (13,432 channels).
- The forward walk clip `gen_walk_dunno` moves its root 3.775 m along +y and 0.003 m along x in 2.33 s.
- The 1,477 standalone model resources hold 7,973 frames, none with a negative determinant.
- All 4,896 skin inverse bind matrices of the 153 skinned models have a positive determinant.
- No palette entry of the 1,877 standalone 8-bit textures has an alpha above 128.
- The 37 object lists hold 10,301 placements; 779 of them have the rotation written as (0, 0, 0, -1).
- Draws 1 and 2 of a sequence at start-up position 0 give the raw values `0xffc27482` and `0x82a6958b`.
- Draw 1,025 of a sequence equals its draw 1, and no value repeats within 1,024 draws.
- A whole number from 0 to 99 taken as draw 1 of a fresh sequence is 90.
- In each of the ten savestates, the ten sequence positions at `0x006eb870` to `0x006eb8b8` are between 0 and 1,023.
- The averaged frame-rate reading is 30.0 in 9 of the 10 savestates.
- A character turning from heading 0 to heading pi/2 turns from facing +Y to facing +X, a turn to the right.

## Open questions

- The Xbox version's conventions were not checked; the axes and units are expected to match, but its random
  sequences and float rounding are unconfirmed.
- Whether any script-facing values (Lua) take angles in degrees rather than radians.
- Which of the nine non-sound sequences each game area draws from, in detail, beyond the broad split given here.
- Whether the game's simulation advances by the measured frame time or by a fixed 1/30 s step.
- The full 1,024-value cycle is not listed here; whether it should be carried as reference data is not decided.
- Whether vertex colours of the PS2 native geometry use 128 as full intensity, as palettes do; a first survey of
  the geometry packets was inconclusive.
- The 57 object-list rotations that are not unit length within 0.001: whether the game normalises them.
