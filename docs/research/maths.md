# Maths

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`).

## Purpose

The small maths unit every other directory calls: the game's random numbers, a fast arctangent, quaternions,
transforms and matrices, distances, ground-plane tests, ray-triangle tests and bounding boxes. Pages elsewhere cite
these by address; this page says what each one computes, so a reimplementation can use ordinary maths code with the
same conventions and edge cases. What a reimplementation must keep is listed under
[What a reimplementation must keep](#what-a-reimplementation-must-keep).

## Original structure

The unit has no path string: it sits at `0x00335320`-`0x00338420`, between the Lua library and `Memory/`, with two
static-init stubs (`0x00335670`, `0x003383e0`), so it is probably two files, the random numbers and the rest
(inferred, [Source map](source-map.md)). Names are ours; every address below has the same name and a plate comment
in the Ghidra project. Unless a row says otherwise its evidence is the code at that address.

### Random numbers {#random}

The generator is a fixed table of 1,024 32-bit numbers at `0x005117e0`. A **stream** is a 32-bit index: each draw
adds 1, masks it to 10 bits and reads that entry (`Random_NextRaw`), so a fresh stream's first draw reads entry 1.
There are ten indices, `0x006eb870`-`0x006eb8b8` (8 bytes apart), all zeroed at start-up and never seeded; every
function below takes the stream in `a0` and passes it through. Most callers pass `0x006eb880` (the Lua `random`
binding, items, brains, humans) or `0x006eb8b8` (particles, strikes, glass), directly or from a saved register;
confirmed (code) for those two, the use of the other eight not traced. How `random(a, b)` depends on the draw count:
[World flags](flags.md#player-starts).

Two integer forms differ at the top: `Random_Int(n)` returns 0 to n **inclusive** (raw mod (n + 1)), while
`Random_IntBelow(n)` returns 0 to n − 1.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00335320` | `Math_NextPowerOfTwo` | The smallest power of two not below n (n itself when it is one). | confirmed (code) |
| `0x00335390` | `Random_NextRaw` | Next 32-bit number of a random stream: the stream's index (a0) is advanced by 1 and masked to 10 bits, then entry index of the fixed 1,024-entry table at `0x005117e0` is returned (never seeded). | confirmed (code) |
| `0x003353b8` | `Random_Int` | Random integer 0 to n inclusive from stream a0: raw mod (n + 1), unsigned; trap 7 when n + 1 is 0. | confirmed (code) |
| `0x003353f0` | `Random_IntRange` | Random integer low to high inclusive: low + Random_Int(high - low). The Lua random binding's core. | confirmed (code) |
| `0x00335420` | `Random_FloatRange2` | Random float in [lo, hi): Random_Unit × (hi - lo) + lo; the same as Random_FloatRange, compiled apart. | confirmed (code) |
| `0x00335460` | `Random_FloatRange` | Random float in [lo, hi): Random_Unit × (hi - lo) + lo. | confirmed (code) |
| `0x00335498` | `Random_IntBelow` | Random integer 0 to n - 1 (raw mod n, signed); 0 when n is 0. | confirmed (code) |
| `0x003354e0` | `Random_Unit` | Random float in [0, 1): the raw number (unsigned) × 2⁻³². | confirmed (code) |
| `0x00335540` | `Random_Signed` | Random float in [-1, 1): 2 × Random_Unit - 1. | confirmed (code) |
| `0x00335570` | `Random_ScaledVector` | Random vector (Random_Unit × sx, Random_Unit × sy, Random_Unit × sz, 1), each axis drawn in turn. | confirmed (code) |
| `0x00335608` | `Random_StaticInit` | Static initialiser: zeroes the ten random-stream indices `0x006eb870`-`0x006eb8b8` (8 bytes apart). | confirmed (code) |
| `0x00335670` | `Random_StaticInitStub` | Static-init entry: Random_StaticInit(1, 0xffff). | confirmed (code) |

### Arctangent {#atan}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003356a0` | `Math_AtanApprox` | atan(x) by a rational approximation: x folded to [0, 1] (1/x, then π/2 - result), above tan(π/12) shifted by π/6 with tan(π/6) (`0x006eb908`, `0x006eb904`), then x(0.99999905 + 0.25797766 x²) / (1 + 0.5912045 x²). | confirmed (code) |
| `0x003357a8` | `Math_Atan2` | atan2(y, x) in (−π, π] from the same approximation as Math_AtanApprox; π/2 when x is within 5e-7 of 0 or either argument is the all-ones NaN at `0x005116b0`. | confirmed (code) |

### Angles, quaternions, transforms and matrices {#rotations}

A transform is `{position, quaternion at +0x10}` (16 bytes each, w = 1 for points); quaternions are `(x, y, z, w)`;
matrices are 4 rows of 16 bytes used with row vectors, the translation in the fourth row. Headings are radians
clockwise from +y (the [Camera](camera.md) convention).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003359b0` | `Transform_Compose` | Composes transforms {pos, quat at `+0x10`}: out.quat = a.quat × b.quat, out.pos = a.pos + b.pos rotated by a.quat (b expressed in a's frame). | confirmed (code) |
| `0x00335a78` | `Transform_ApplyPoint` | A point through a transform: p rotated by the transform's quaternion (`+0x10`) plus its position; w = 1. | confirmed (code) |
| `0x00335b08` | `Quat_IntegrateAngular` | Integrates an angular velocity into a quaternion in place: q += 0.5 × dt × (w (x) q), then normalised (identity when it collapses to 0). | confirmed (code) |
| `0x00335c08` | `Quat_IntegrateAngularTo` | As Quat_IntegrateAngular but writes the result to a separate output: out = normalise(q + 0.5 × dt × (w (x) q)). | confirmed (code) |
| `0x00335d08` | `Angle_Wrap2Pi` | Wraps an angle in radians into [0, 2π) by repeated steps of 2π. | confirmed (code) |
| `0x00335d98` | `Vec_Heading` | Heading of a vector in [0, 2π): atan2(x, y), so clockwise from +y. | confirmed (code) |
| `0x00335e00` | `Vec_HeadingFromTo` | Heading in [0, 2π) of b - a (from point a toward point b), by Vec_Heading. | confirmed (code) |
| `0x00335e40` | `Vec_FromHeading` | Unit direction for a heading in radians: (sin h, cos h, 0, 1), clockwise from +y. | confirmed (code) |
| `0x00335ea0` | `Quat_FromAxisAngle` | Quaternion from an axis and an angle in radians: (axis × sin(a/2), cos(a/2)). | confirmed (code) |
| `0x00335f48` | `Quat_Heading` | Heading of a quaternion in [0, 2π): Vec_Heading of its rotated y axis (Quat_AxisY). | confirmed (code) |
| `0x00335f70` | `Quat_AngleFromX` | Angle of a quaternion's rotated y axis from +x, counter-clockwise: atan2(y.y, y.x) in (−π, π]. | confirmed (code) |
| `0x00335fa0` | `Mat_Invert` | General 4x4 inverse of a transform matrix (rows at 0, 0x10, 0x20, 0x30): transposed into a scratch array, Gauss-Jordan elimination with partial pivoting against the identity (`0x005117a0`), transposed back. | confirmed (code) |
| `0x00336308` | `Quat_AxisX` | A quaternion's rotated × axis: (1 - 2(y² + z²), 2(xy + zw), 2(xz - yw), 1). | confirmed (code) |
| `0x003363b0` | `Quat_AxisY` | A quaternion's rotated y axis (its forward): (2(xy - zw), 1 - 2(x² + z²), 2(yz + xw), 1). | confirmed (code) |
| `0x00336458` | `Quat_AxisZ` | A quaternion's rotated z axis (its up): (2(xz + yw), 2(yz - xw), 1 - 2(x² + y²), 1). | confirmed (code) |
| `0x00336500` | `Quat_ToMatrix` | A quaternion to a 4x4 rotation matrix (rows are the rotated axes), translation zero and w = 1. | confirmed (code) |
| `0x003365c8` | `Mat_ToQuat` | A rotation matrix to a quaternion: from the trace when it is positive, else from the largest diagonal element (the usual branch order 1, 2, 0). | confirmed (code) |
| `0x00336808` | `Transform_ToMatrix` | A transform {pos, quat at `+0x10`} to a 4x4 matrix: Quat_ToMatrix rows, the position as the fourth row. | confirmed (code) |
| `0x003368d8` | `Mat_SwapYZ` | Re-expresses a 4x4 matrix with y and z exchanged (y negated first, pexcw), rows 1 and 2 swapped, w words cleared and the last row's w = 1: the change between the game's z-up frame and a y-up one (inferred). Used by the render paths (glass, fog, embers, tag HUD). | inferred |
| `0x00336940` | `Mat_MulSwapYZ` | Multiplies two 4x4 matrices (row vectors: out = b × a, b applied first) and converts the product as Mat_SwapYZ. The skeleton update's bone matrices. | confirmed (code) |
| `0x00336a00` | `Quat_Slerp` | Spherical interpolation of two quaternions by t clamped to [0, 1]; takes the shorter arc (negates when the dot is negative) and falls back to a linear mix within 1e-6 of parallel. | confirmed (code) |
| `0x00336bb8` | `Vec_Lerp` | Linear mix of two vectors: out.xyz = a × (1 - t) + b × t; w kept from a. | confirmed (code) |
| `0x00336bf8` | `Quat_Nlerp` | Normalised linear interpolation of two quaternions: a when their dot is exactly 1, else normalise(a(1 - t) + b t) with b negated when the dot is negative. | confirmed (code) |

### Distances, lines and look-at {#distances}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00336c98` | `Vec_DistSq2D` | Squared distance of two points in the ground plane (x, y). | confirmed (code) |
| `0x00336ce0` | `Vec_DistSq` | Squared distance of two points (x, y, z). | confirmed (code) |
| `0x00336d28` | `Segment_PointDistSq` | Squared distance from a point to a segment (via Segment_ClosestPoint). | confirmed (code) |
| `0x00336d68` | `Vec_Dist2D` | Distance of two points in the ground plane: sqrt(Vec_DistSq2D). | confirmed (code) |
| `0x00336d88` | `Vec_Dist` | Distance of two points: sqrt(Vec_DistSq). | confirmed (code) |
| `0x00336da8` | `Vec_Dir2D` | Unit direction in the ground plane from a to b: (b - a).xy normalised, z = 0, w = 1. | confirmed (code) |
| `0x00336e08` | `Line_ClosestPoint` | Closest point to p on the infinite line through a and b: a + d × dot(p - a, d), d the unit direction. | confirmed (code) |
| `0x00336ee0` | `Segment_ClosestPoint` | Closest point to p on the segment a-b: a when the projection is at or before a, b when at or past b, else the projection. | confirmed (code) |
| `0x00337028` | `Mat_LookAt` | Orientation matrix looking along (target - position): returns 0 when they are within 0.005 m; the up vector is +z (`0x00511700`), or (1, 1, 1) normalised when the direction is within 0.98 of vertical; then Mat_LookAtUp. Every camera class's placement. | confirmed (code) |
| `0x00337168` | `Mat_LookAtUp` | Builds an orthonormal basis from a direction and an up vector: row 1 (`+0x10`) the unit direction, row 0 = direction × up normalised, row 2 = row 0 × direction normalised, w words 0; false when the direction is shorter than 0.005 m. | confirmed (code) |

### Ground-plane tests, angle steps and small helpers {#helpers}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003372a0` | `Math_Orient2D` | Which side of a-b point c lies on in the ground plane: 1 (left, counter-clockwise), -1 (right) or 0 (on the line), from the 2D cross product. | confirmed (code) |
| `0x00337308` | `Segment_Intersect2D` | Whether two segments p1-p2 and p3-p4 cross in the ground plane: p1 and p2 on different sides of p3-p4 and p3 and p4 on different sides of p1-p2 (Math_Orient2D). | confirmed (code) |
| `0x003373b8` | `Angle_StepToward` | Turns an angle in [0, 2π) toward a target by at most step, the shorter way; within slow (when positive) the step is scaled by the remaining distance / slow; returns the target when the step reaches it. Human_UpdateHeadLook. | confirmed (code) |
| `0x003374b8` | `Angle_Diff` | Unsigned shortest difference of two angles in [0, 2π), 0 to π. | confirmed (code) |
| `0x00337568` | `Math_MapRange` | Linear map of × from [inMin, inMax] onto [outMin, outMax]. | confirmed (code) |
| `0x003375a0` | `Math_RoundUpToMultiple` | Rounds n up to a multiple of m: (n - 1 + m) - (n - 1) mod m; n when m is 0. | confirmed (code) |
| `0x003375d0` | `Math_CircleRadius2D` | Radius of the circle through a and b (ground plane) whose centre lies from a along the direction c: length(b - a) / (2 abs(dot(unit(b - a), c))); 0 when a and b coincide or the dot is 0. | confirmed (code) |
| `0x003376c0` | `Vec_PointOnCircle` | Point on a circle in the ground plane: centre + r × (cos a, sin a, 0) with a in°rees (counter-clockwise from +x); w = 1. | confirmed (code) |
| `0x00337788` | `Vec_PointOnCircleCopy` | Vec_PointOnCircle with the centre copied from a pointer first. | confirmed (code) |
| `0x003377c0` | `Quat_FromDirection` | Quaternion facing along a direction: an up of +z (`0x00511740`; +x, `0x00511720`, when the direction is within 0.99 of vertical), two cross products for the basis, then Mat_ToQuat. | confirmed (code) |

### Roots, rays and segments {#rays}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x003378b0` | `Math_QuadraticLargerRoot` | Larger real root of a x² + b × + c = 0; nothing written when the discriminant is negative. | confirmed (code) |
| `0x00337920` | `RayTriangle_OneSided` | Ray-triangle test (Moller-Trumbore), front faces only (determinant at least 1e-6): the hit distance along the ray, or failure. | confirmed (code) |
| `0x00337a60` | `RayTriangle_TwoSided` | Ray-triangle test (Moller-Trumbore) for either face (determinant at least 1e-6 in size): the hit distance along the ray, or failure. | confirmed (code) |
| `0x00337bd8` | `Segment_SegmentDistSq` | Squared distance between two segments: the closest parameters s and t on each, clamped to [0, 1], with the near-parallel case (denominator below 0.005) taking s = 0. TrainRecord_IsPathClear. | confirmed (code) |

### Boxes, colours and start-up {#boxes}

A box is `{min, max at +0x10}`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00337ed8` | `Box_SetEmpty` | Box {min, max at `+0x10`} made empty: min = FLT_MAX (`0x00576a7c`), max = -FLT_MAX (`0x00576a80`), so any point extends it. | confirmed (code) |
| `0x00337f08` | `Box_FromPoint` | Box with min = max = a point. | confirmed (code) |
| `0x00337f28` | `Box_Union` | Box grown to contain another box (component-wise min of the mins, max of the maxes). | confirmed (code) |
| `0x00337f78` | `Box_AddPoint` | Box grown to contain a point. | confirmed (code) |
| `0x00337fc8` | `Box_Centre` | A box's centre: min + (max - min) × 0.5. | confirmed (code) |
| `0x00338048` | `Box_ContainsPoint` | Whether a point is inside a box, bounds included. | confirmed (code) |
| `0x003380d8` | `Box_Overlaps` | Whether two boxes overlap on all three axes, touching included. | confirmed (code) |
| `0x00338178` | `Box_Corners` | A box's eight corners (16 bytes apart) from its min and max. Camera_FrustumTestBox and the occluders. | confirmed (code) |
| `0x00338240` | `Colour_LerpRatio` | Blends two RGBA byte colours by t = a / b: the bytes unpacked to floats, c1 × (1 - t) + c2 × t, repacked; 1 - t and t written out; c1 when the colours are equal. cars.md, the HUD. | confirmed (code) |
| `0x00338300` | `Maths_StaticInit` | Static initialiser: copies the identity quaternion (`0x005116c0`), `0x00511780` and the identity matrix rows (`0x005117a0`-`0x005117d0`) into `0x006eb8c0`-`0x006eb94c` and stores tan(π/6) (`0x006eb904`) and tan(π/12) (`0x006eb908`) for Math_AtanApprox. | confirmed (code) |
| `0x003383e0` | `Maths_StaticInitStub` | Static-init entry: Maths_StaticInit(1, 0xffff). | confirmed (code) |

## What a reimplementation must keep

- **The random table and its streams** where a page depends on a draw order: the table comes from the player's
  executable at run time (`GameRandom`, [World flags](flags.md#coneys-implementation)), and `Random_Int(n)` includes
  n. Elsewhere any generator with the same ranges will do.
- **Headings clockwise from +y** (`Vec_Heading`, `Vec_FromHeading`, `Quat_Heading`): angles stored in scripts and
  flags use it; `Vec_PointOnCircle` and `Quat_AngleFromX` use the mathematical convention instead.
- **Transforms as `{position, quaternion}`** composed as `Transform_Compose` does (the child in the parent's frame).
- **The degenerate cases** callers rely on: `Mat_LookAt` fails within 0.005 m and swaps its up vector near vertical;
  `Math_Atan2` returns π/2 for x near 0; `Quat_Slerp` clamps t to [0, 1].

## Open questions

- Which callers use the other eight random streams (`0x006eb870`-`0x006eb8b0`, except `0x006eb880`).
- Whether `Mat_SwapYZ` converts to RenderWare's frame or to the vector unit's (its callers are all render paths).
