# Physics units: rays and the world

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-06, the whole of `Physics/` on 2026-10-07); the game under PCSX2 2.9.94 (PINE, hooks and `pcsx2 record`, in
`level99`, 2026-10-07). The exact specifications of
[Reimplementing Physics, RayCast and World](physics.md#units) were read in the disassembly on 2026-10-12.

Part of [Reimplementing Physics, RayCast and World](physics.md#units): the exact specifications of rays against bodies,
the physics world, the world manager's ray, the level file and object, the world manager and the streamed world object.
The plan, the order of the units and their dependencies are on that page; the subsystem itself is [Physics](physics.md).

## Unit 14: Rays against bodies {#unit-14}

**What it is.** The ray the AI's line tests cast against the bodies of the world (the level mesh is a separate ray,
[Collision](collision.md)): the candidate bodies, the ray table's box and capsule tests, and the nearest-hit rule.

- **Coney:** `src/physics/rays.*`, beside Unit 7's candidate query. The AI's line tests (`Goal_LineTest`
  `0x002e7608`, `Shot_CanHitHuman`, the stationary shooter's two, `BossTactic_Process`) have no Coney counterpart yet;
  they cast this ray and the mesh ray and keep the nearer ([AI goals](ai-goals.md)).
- **Tests:** [Test ideas](#u14-tests): the slab entry of a rotated box, a ray starting inside a box (fraction 0) and
  inside a capsule (its exit), the rounded capsule top, the straight-down capsule quirk, the filters and the nearest
  hit.

5 functions, 2436 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00349488` | `Ray_ClipSlab` | 212 |
| `0x00349560` | `Ray_OrientedBox` | 864 |
| `0x003459f0` | `PhysicsRay_Capsule` | 412 |
| `0x00345b90` | `PhysicsRay_Box` | 388 |
| `0x0033eee0` | `IPhysics_RayCastBodies` | 560 |

The capsule kernel itself, `Sweep_SphereCapsule` (`0x00349a60`), belongs to [Unit 11](physics-bodies.md#unit-11); what
it does with a
ray is restated [below](#u14-capsule-kernel) so this unit stands alone. All claims confirmed (code) at the address of
the section unless a line says otherwise (read in the disassembly, 2026-10-12). Notation as in [Unit
3](physics-bodies.md#u3-records).

### The ray and the hit list {#u14-records}

**Ray** (16-byte aligned, the caller's): `+0x00` start S (w = 1), `+0x10` direction d. The caller passes a length
`len` in `f12` beside it. Every function here uses the **segment** from S to S + D, with **D = d × len** (xyz; the
product keeps d's w). Fractions are of D: the point at fraction f is S + f·D, its distance from S is f·len·|d|. The
AI's callers pass a unit d, so the distance is f·len (inferred from the callers' use, not checked for each one).

**Hit list** (a stack record the ray kernels fill; `Ray_OrientedBox` and `Sweep_SphereCapsule` share its layout):
`+0x00` count (u32, the caller sets 0), `+0x04 + 4i` fraction i, `+0x10 + 16i` a normal for hit i. The kernels write
the **constant (0, 0, 0, 1)** (`0x005116c0`) as every normal: no ray here produces a surface normal. At most two
entries are written. Confirmed (code) at `0x003497e4`-`0x00349890` and `0x00349d1c`-`0x0034a2a4`.

**Contact fields written** (the 0xc0 [contact](physics.md#contacts)): the shape functions write `+0x80` (the fraction)
and
`+0xb0` = 0 on a hit and nothing on a miss; `IPhysics_RayCastBodies` then writes `+0x40` (owner), `+0x44` (shape)
and `+0x80` (the nearest fraction). Nothing else: no point (`+0x90`) and no normal (`+0x50`). The callers compute
those themselves (`Goal_LineTest` writes `+0x90` and `+0xa0`, [AI goals](ai-goals.md)).

### Ray_ClipSlab {#u14-clip-slab}

`0x00349488`, 0xd4 bytes. `bool Ray_ClipSlab(float den /*f12*/, float num /*f13*/, float* t0 /*a0*/, float* t1
/*a1*/)`. One half-space of a box: the points at parameter t with **den·t ≥ num** are inside. It narrows the interval
[t0, t1] to them, or returns false when nothing of it is left:

| Case | Test (in this order) | Result |
| --- | --- | --- |
| den > 0 (entering) | den·t1 < num | false (the half-space starts after t1) |
| | else den·t0 < num | t0 = num / den; true |
| | else | true, unchanged |
| den < 0 (leaving) | den·t0 < num | false (the half-space ended before t0) |
| | else den·t1 < num | t1 = num / den; true |
| | else | true, unchanged |
| den = 0 (parallel) | num ≤ 0 | true (the line lies inside), else false |

**No epsilon anywhere**: the parallel case is den exactly 0.0 (`c.lt.S` both ways, `0x00349490`, `0x003494ec`), and
touching counts as inside (all tests are strict `<` for rejection). The multiplications are done, never a division
before the test, so den = 0 never divides. Because a den > 0 clip rejects when the new t0 would pass t1 and a den < 0
clip rejects when the new t1 would fall under t0, the interval stays ordered: t0 ≤ t1 after any sequence of clips.

### Ray_OrientedBox {#u14-oriented-box}

`0x00349560`, 0x360 bytes. `bool Ray_OrientedBox(HitList* out /*a0*/, const Segment* ray /*a1*/, const BoxFrame* box
/*a2*/)`: `ray` is {S at `+0x00`, D at `+0x10`} (already scaled); `box` is a 0x50-byte frame {`+0x00` A0, `+0x10`
A1, `+0x20` A2 (unit axes), `+0x30` P (centre), `+0x40`, `+0x44`, `+0x48` half extents h0, h1, h2}, which is a box
shape's `+0x50`-`+0x80` followed by its `+0x40` ([Shapes](physics.md#shapes)).

1. **Local coordinates** (VU0 dot products, x+y then +z): r = S − P (xyz, w = 1); d_i = r·A_i and m_i = D·A_i for
   i = 0, 1, 2 (`0x003495b0`-`0x00349690`).
2. **Clip** with t0 = 0, t1 = 1, six calls in this order, stopping at the first false (`0x003496bc`-`0x00349770`):
   for i = 0, 1, 2: `ClipSlab(m_i, −d_i − h_i)` (the face at −h_i: d_i + m_i·t ≥ −h_i) then `ClipSlab(−m_i, d_i −
   h_i)` (the face at +h_i: d_i + m_i·t ≤ h_i). Any false: return false, `out` untouched.
3. **No hit when nothing was clipped**: if t0 = 0.0 **and** t1 = 1.0 exactly, return false (`0x0034978c`-`0x003497b0`).
   This is the whole segment inside the box, and also a zero D (every den is 0, so every clip is the parallel case).
4. Otherwise: if 0 ≤ t0 ≤ 1, append t0. Then if 0 ≤ t1 ≤ 1 **and t0 < t1**, append t1. Return **true** (even if
   nothing was appended; with t0 ≤ t1 inside [0, 1] after step 2 both conditions always hold except t0 = t1).

So a segment that **starts inside** the box and leaves it gets [0, exit]; one that starts outside and ends inside gets
[entry, 1]; one that only touches a face, edge or corner gets [t] once (t0 = t1). Confirmed (code) at `0x00349560`.

### PhysicsRay_Box {#u14-ray-box}

`0x00345b90`, 0x184 bytes; ray table `+0x108` entry for type 1. Also called by `ObjectRender_Draw` (`0x0017fd78`).
`bool PhysicsRay_Box(float len /*f12*/, Contact* c /*a0*/, const Ray* ray /*a1*/, const BoxShape* box /*a2*/)`.

1. D = ray.d × len (xyz; `vmulx.xyz` keeps d's w). Copies S and D, and the box's A0, A1, A2, P (`+0x50`-`+0x80`) and
   half extents (`+0x40`) into a frame on its stack (`0x00345bb8`-`0x00345c04`). The box's **current** pose is used.
2. `Ray_OrientedBox` into a hit list with count 0. False: return 0.
3. **Filter**: walk the entries from the last to the first; an entry whose fraction is < 0 or > 1 is removed (the later
   entries shift down). With `Ray_OrientedBox` every entry is already in [0, 1], so nothing is removed here.
4. Count 0: return 0. Else `c+0xb0` = 0, `c+0x80` = entry 0's fraction, return 1.

**What it answers:** the entry fraction of the segment into the box; **0 when the ray starts inside** the box (or on
its surface) and leaves it; a hit at fraction 1.0 when the segment ends exactly on a face; no hit when the whole
segment lies inside. Confirmed (code) at `0x00345b90`.

### PhysicsRay_Capsule {#u14-ray-capsule}

`0x003459f0`, 0x19c bytes; ray table entry for type 3. `bool PhysicsRay_Capsule(float len /*f12*/, Contact* c
/*a0*/, const Ray* ray /*a1*/, const CapsuleShape* cap /*a2*/)`.

1. D = ray.d × len (`0x00345a38`). The capsule argument for the kernel, built on its stack (`0x00345a18`-`0x00345a84`):
   base P = shape `+0x50`; axis A = (P with z += height) − P, xyz, w = 1, so A = (0, 0, h') with **h' = (Pz + h) − Pz**
   computed in floats (h = shape `+0x44`; h' can differ from h by rounding when Pz is large); radius R = shape `+0x40`.
   No radius is given for the ray: it is a line.
2. **The kernel is `Sweep_SphereCapsule` (`0x00349a60`)**, the same one the sphere-capsule sweep uses, not
   `Ray_InfiniteCylinder` (`0x0034d938`, the mesh's edge cylinder). Confirmed (code): the `jal 0x00349a60` at
   `0x00345a88`.
3. A false answer: return 0. Else the same filter as the box (drop fractions outside [0, 1], last to first), then count
   0: return 0; else `c+0xb0` = 0, `c+0x80` = **entry 0** (the first one **stored**, which is not always the
   smallest, [below](#u14-capsule-kernel)), return 1.

The capsule here is a **true capsule**: an upright cylinder of radius R from Pz to Pz + h' with a hemisphere of radius R
on each end, so a ray reaches R above the top and R below the base. (The overlap and sweep tests treat the same shape
differently, [Shapes](physics.md#shapes).)

### What Sweep_SphereCapsule does with a ray {#u14-capsule-kernel}

`Sweep_SphereCapsule(HitList* out /*a0*/, const Segment* seg /*a1: S, D*/, const Capsule* cap /*a2: P, A, R*/)`,
`0x00349a60`, returns true when it stored at least one root. Restated from the disassembly for the ray case
(confirmed (code) at the addresses given):

1. **Frame** (`0x00349a98`-`0x00349b64`). L = |A|, U = A / L. A perpendicular basis E1, E2 from U
   (`Vec_MakePerpendicularBasis` `0x00349180`, [Unit 1](physics-kernels.md#u1-capsule)); for the upright capsule only
   the horizontal
   plane matters and every result below is independent of which E1, E2 it picks. Local D' = (D·E1, D·E2, D·U);
   n = |D'| (= |D|); invn = 1 / n; u = D' × invn, the unit direction (`0x00349bac`-`0x00349be0`).
   Start: r = S − P, s = (r·E1, r·E2, r·U); **s3 is the start's height above the base**.
2. **Nearly vertical, or too short** (`0x00349c1c`-`0x00349c90`): if |u3| ≥ 0.999999 (`0x3f7fffef`, 0.99999899) or
   n < 1e-6 (`0x358637bd`), take the parallel branch: q² = R² − s1² − s2² and k = D·A (the world dot product, unscaled).
   - k < 0 (going down) and q² ≥ 0: store **(s3 + q)·invn** then **(L − s3 + q)·invn**; return true.
   - k > 0 (going up) and q² ≥ 0: store **−(s3 + q)·invn** then **(L − s3 + q)·invn**; return true.
   - otherwise (q² < 0, or k = 0, which covers a zero D): return false.

   Going up, the two are the entry through the bottom (height −q) and the exit through the top (L + q): correct.
   **Going down they are wrong**: the first is the exit through the bottom, and the second is the entry through the top
   with its **sign flipped** (the true entry distance is s3 − L − q). So a ray coming straight down onto a capsule from
   above reports, after the filter, the fraction where it leaves the **bottom**, or no hit at all when that point is
   past the end of the segment. Confirmed (code) at `0x00349cd0`-`0x00349d74`.
3. **Otherwise, the side** (`0x00349e2c`-`0x00349fa0`): b = s1·u1 + s2·u2, c = s1² + s2² − R², a = u1² + u2²,
   disc = b² − a·c.
   - disc < 0: return false (the line misses the infinite cylinder, so it misses the caps too).
   - disc > 0: for t = (−b − √disc) / a, then t = (−b + √disc) / a: the height z = s3 + t·u3; if 0 ≤ z ≤ L store t·invn.
     With two stored, return true. Else go to the caps.
   - disc = 0 exactly: t = −b / a; if 0 ≤ z ≤ L store it and return true; else go to the caps.
4. **The bottom hemisphere** (centre P; `0x0034a018`-`0x0034a174`): c' = c + s3² (= |r|² − R²), b' = b + s3·u3
   (= r·u), disc' = b'² − c'. If disc' > 0: for t = −b' − √disc', then −b' + √disc': store t·invn when z = s3 + t·u3
   ≤ 0, returning true as soon as two are stored. If disc' = 0: t = −b', the same test. disc' < 0: nothing.
5. **The top hemisphere** (centre P + L·U; `0x0034a178`-`0x0034a2a4`): b'' = b' − u3·L, c'' = c' + L·(L − 2·s3);
   the same roots, stored when z ≥ L, returning true at two.
6. Return count ≠ 0.

The roots are in **stored order**, not sorted: side near, side far, bottom near, bottom far, top near, top far, the
first two that pass. Roots behind the start (t < 0) are stored too; the caller's filter removes them. Consequences
for `PhysicsRay_Capsule`, which takes entry 0:

- A ray **starting inside** the capsule reports its **exit** (the near root is negative and filtered out), unlike the
  box, which reports 0.
- A slanted ray that **enters through the top hemisphere and leaves through the side** reports the side exit: the
  side's far root is stored first (its near root lies above L), then the top's near root. Coming in from the side and
  leaving through a cap gives entry first, which is right.
- The z tests are inclusive at both ends, so a root exactly at the base or top height can be stored by the side and
  again by a cap.

The arithmetic is floats in this order (EE rounding, no NaN; [Floating point](physics.md#units)); at disc = 0 exactly
the two
branches differ, so tests should avoid exact tangency.

### IPhysics_RayCastBodies {#u14-raycast-bodies}

`0x0033eee0`, 0x230 bytes. **`bool IPhysics_RayCastBodies(float len /*f12*/, IPhysics* phys /*a0*/, Contact* out
/*a1*/, const Ray* ray /*a2*/, u32 typeMask /*a3*/, Owner* ignored /*t0*/)`**. Every caller passes `typeMask` =
0xffffffff (`lui` + `ori 0xffff` before each `jal`, `0x002e765c`, `0x002e78c4`, `0x002f01c4`, `0x002f05dc`,
`0x00308ed8`) and its own human as `ignored`; `BossTactic_Process` casts 25 m (`0x41c80000`).

1. **The box.** E = S + d × len (w = 1). A body node on its stack is initialised (`PhysicsNode_Init` `0x00341158`)
   and given min = componentwise min(S, E), max = max(S, E) (`vmini` / `vmax`, `0x0033efb0`-`0x0033efe0`). No
   margin.
2. **Candidates** (`IPhysics_QueryBox` `0x0033feb0`, [Queries](physics.md#queries)): start body 0 (so the walk runs
   forwards
   from the list head), ignored owner `ignored`, type mask `typeMask`, **flag mask 0x2000** (`RAYCASTTARGET`,
   [layers](physics.md#layers)). QueryBox keeps a body when `body+0x44 & typeMask` ≠ 0, so a **type mask of 0 finds
   nothing**;
   and when `body+0x40 & 0x2000` ≠ 0; overlap tests are inclusive. Up to 512 bodies fit the stack array
   (`sp+0x20`, 0x800 bytes); QueryBox does not check that bound.
3. **Shapes.** For each candidate in list order, its shapes from body vtable `+0x28` (called with (2, 4, 0), which
   both implementations ignore: `PhysicsBody_GetShapes` `0x00341ae0` and the human body's `0x003420c8` return
   `+0x30`), following each shape's `+0x00`. A shape is tested when its flag `0x1` (enabled) is set and the ray table
   has an entry for its type (`phys+0x108 + 4·type`: box and capsule only, [dispatch](physics.md#dispatch)). A **human
   body's
   chain is its capsule alone**: the ten bone shapes hang from `+0x80`, not `+0x30`, so a ray never meets them.
4. **Nearest.** best = 1e9 (`0x4e6e6b28`). The shape function is called with (len, `out`, ray, shape); on a hit it has
   written `out+0x80`; if that is **strictly less** than best, best, the shape and the body are kept. Ties keep the
   earlier one: list order (x-sorted by AABB min), then chain order.
5. **Answer.** With a kept body: `out+0x44` = the shape, `out+0x80` = best, `out+0x40` = the body's owner (`+0x34`);
   return true. None: return false. On a miss nothing of `out` is written; on a hit `out+0x80` and `out+0xb0` may also
   have been written by shapes that were not the nearest (`+0x80` is rewritten at the end, `+0xb0` is 0 either way).

So the answer is the **owner, shape and fraction of the nearest entry** (by the per-shape rules above, including the
capsule's quirks) among enabled box and capsule shapes of bodies with `RAYCASTTARGET`. In `level99` those are the
humans' capsules (body flags `0x2203f` etc.), the cars' boxes (`0x2211a`) and the objects whose type sets the layer;
not glass panes (`0x7a`) (confirmed (runtime), the flags in [layers](physics.md#layers)).

### Test ideas {#u14-tests}

Values computed from this spec (Python, doubles; compare with a tolerance of 1e-5). Box B: centre (0, 0, 1), axes
identity, half (1, 0.5, 1).

1. **Through a box.** S (−3, 0, 1), d (1, 0, 0), len 4: list [0.5, 1.0] (the exit is exactly x = 1 at t = 1); answer
   0.5.
2. **Rotated box.** Centre (10, 5, 0), A0 = (0.7071, 0.7071, 0), A1 = (−0.7071, 0.7071, 0), A2 = (0, 0, 1), half
   (1, 0.5, 1); S (7, 5, 0), d (1, 0, 0), len 6: the entry is the A1 face, list [0.382149, 0.617851], answer 0.382149
   (x = 9.292893); the A0 slab alone would give 0.264298.
3. **Start inside, leave.** S (0, 0, 1), len 4: list [0, 0.25]; answer **0**.
4. **Start and end inside.** S (0, 0, 1), len 0.5: no hit (t0 = 0, t1 = 1 untouched).
5. **Parallel.** S (−3, 0.6, 1), d (1, 0, 0), len 4: the y slab is parallel with num 0.1 > 0: no hit. S (−3, 0.5, 1)
   (on the face plane): num 0, inside: hit, 0.5.
6. **Ends on the face.** S (−3, 0, 1), len 2: t0 = t1 = 1.0, list [1.0]: hit at 1.0.
7. **Away, zero.** d (−1, 0, 0): no hit. len 0 (or d = 0), outside or inside: no hit.

Capsule C: base (0, 0, 0), height 1.8, radius 0.35.

1. **Side.** S (−2, 0, 1), d (1, 0, 0), len 4: list [0.4125, 0.5875]; answer 0.4125 (x = −0.35).
2. **Rounded top.** S (−2, 0, 2.0), same d and len: the side roots are at z = 2.0 > 1.8, the top hemisphere gives
   [0.428193, 0.571807]; answer 0.428193. At z 2.2 (0.4 above the top centre, more than R): no hit. At z −0.2: the
   bottom hemisphere, 0.428193.
3. **Start inside.** S (0, 0, 1), len 1: stored [−0.35, 0.35]; answer **0.35** (the exit).
4. **Slanted through the top, out the side.** S (−1, 0, 3.2), d = (1, 0, −1.5) / 1.802776, len 4: stored
   [0.608437 (side exit, z 1.175), 0.343499 (top entry, z 2.056768)]; answer **0.608437**, not 0.343499.
5. **Straight down (the quirk).** S (0.1, 0, 3), d (0, 0, −1), len 4: q = 0.335410; stored [0.833853, −0.216148];
   answer **0.833853** (z −0.335410, the bottom), where the true entry is 0.216148 (z 2.135410). With len 3: stored
   [1.111803, −0.288197]: **no hit**, though the ray passes 0.86 m into the capsule.
6. **Straight up.** S (0.1, 0, −1), d (0, 0, 1), len 4: stored [0.166148, 0.783853]; answer 0.166148 (correct).
7. **Miss.** S (−2, 0.4, 1), d (1, 0, 0), len 4: disc < 0, no hit.

RayCastBodies, ray S (0, 0, 1), d (1, 0, 0), len 10, typeMask 0xffffffff, flags of every body `0x2000` unless said:

1. **Nearest.** Body A, box centre (5, 0, 1), half (1, 1, 1); body B, box centre (3, 0, 1), half (0.5, 0.5, 0.5):
   owner B, B's box, 0.25 (A's 0.4 loses).
2. **Filters.** B without `0x2000`: A, 0.4. `ignored` = B's owner: A, 0.4. typeMask 0: no hit. B's box disabled
   (flag `0x1` clear): A, 0.4. A sphere shape on B instead of the box: never tested, A.
3. **A human.** A human body whose capsule has base (2, 0, 0), height 1.8, radius 0.35: hit at 0.165 on the capsule;
   a bone shape switched on and in the ray's way changes nothing.
4. **Tie.** Two bodies with identical boxes: the one earlier in the x-sorted list wins.
5. **Miss leaves `out` alone.** Fill `out` with a pattern; a ray that meets nothing leaves every byte.

## Unit 15: The physics world: construction, settle and step {#unit-15}

**What it is.** The `IPhysics` object: what its constructor builds (fields, pools, the shape-pair tables, the human
body sets, the punch bag's set), the 60 Hz step (the bound refresh and the settle turns), starting and cancelling a
settle in one of 64 slots, the settle's end rotation, and the file's static initialiser.

- **Coney:** `src/physics/physics_world.*` (one world per level, its pools and tables, the step);
  `src/world_objects/loose_objects.*` moves its settle (`SettleTurn`, `settleAxis`, `settleTarget`, tagged) onto the
  world's 64 slots; `src/platform/play_level_mode.*` calls the step once per 60 Hz tick after the humans and the
  wheel, replacing Coney's two settle ticks per 1/30 s step (`LooseObjects::kTicksPerStep`).
- **Tests:** [Test ideas](#u15-tests): the pools and tables after construction, the bound refresh on the 120th step,
  a settle taking 11 ticks (`t` = 0.01 n (n − 1)), 64 busy slots refusing a 65th, cancelling, and the end rotations.

8 functions, 4392 bytes (3 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0033c288` | `IPhysics_Construct` | 2168 |
| `0x00340918` | `IPhysics_Step` (tagged) | 544 |
| `0x00340b38` | `Settle_NearestAxis` (tagged) | 464 |
| `0x00340d08` | `Settle_ComputeTarget` (tagged) | 724 |
| `0x00340fe0` | `IPhysics_StartSettle` | 272 |
| `0x003410f0` | `IPhysics_CancelSettle` | 104 |
| `0x0034f6c0` | `Physics_StaticInit` | 84 |
| `0x0034f718` | `Physics_StaticInitStub` | 32 |

All claims confirmed (code) at the address of their section unless a line says otherwise (read in the disassembly,
2026-10-12). Owner vtable slots are written as in [Unit 3](physics-bodies.md#u3-records) (adjust / function offsets);
the world
object's vtable is `0x005453a0` (stored by `ObjectTaskPool_New`, `0x004f3ef0`).

### IPhysics_Construct {#u15-construct}

`0x0033c288`, 0x878 bytes. `IPhysics* IPhysics_Construct(IPhysics* phys /*a0*/)`, returns `phys`. Called once, by
`Game_InitializeSubsystems` (`0x00160fa8`), on a 0x4f460-byte block; the result goes to `0x00597198`
([Boot](boot.md#initialisation-order)). In order:

1. **Fields**: `+0x04` (human list head) = 0, `+0x10` (u16) = 0, `+0x12` (u16, ticks since the bound refresh) = 0,
   `+0x14` (the widest body) = 0.0 (`0x0033c2d0`-`0x0033c2dc`). `+0x08` (the body list head) = 0 later
   (`0x0033c86c`). What reads `+0x10` is not traced.
2. **Pools**, each element constructed in place, in this order (the loop counts are `count − 1` counted down to −1):

   | Offset | Count × stride | Constructor | Later use ([IPhysics](physics.md#iphysics)) |
   | --- | --- | --- | --- |
   | `+0x120` | 300 × 0x60 | sphere `0x00342aa0` | the humans' bone spheres, 5 each |
   | `+0x71a0` | 300 × 0x70 | segment `0x003437b0` | the humans' bone segments, 5 each |
   | `+0xf7b0` | 60 × 0x60 | sphere | weapon strike spheres |
   | `+0x10e30` | 60 × 0x60 | capsule `0x003434a8` | the humans' capsules |
   | `+0x124b0` | 60 × 0xe0 | human body `0x00341e98` | the human body sets |
   | `+0x15930` | 416 × 0x80 | body `0x003418f8` | world objects' bodies |
   | `+0x22930` | 416 × 0x60 | sphere | world objects' spheres |
   | `+0x2c530` | 416 × 0x90 | box `0x00342d18` | world objects' boxes |
   | `+0x3af30` | 255 × 0x80 | body | glass panes' bodies |
   | `+0x42eb0` | 255 × 0x90 | box | glass panes' boxes |
   | `+0x4be20` | 18 × 0x80 | body | cars' bodies |
   | `+0x4c720` | 36 × 0x90 | box | cars' boxes |
   | `+0x4db60` | 30 × 0x60 | sphere | push spheres |

   What the constructors write is [Unit 6](physics-bodies.md#unit-6) (shapes: next 0, offset (0, 0, 0, 1), bone −1,
   flags `0x1`) and
   [Unit 7](physics-bodies.md#unit-7) (a body: node min = max = (0, 0, 0, 1), index −1, links 0, shapes 0, owner 0,
   flags
   `0x80000400`, push-out (0, 0, 0, 1), scale from `0x005116d0`; a human body adds `+0x80`, `+0x84`, `+0xc0`-`+0xd4`
   = 0, a broad bound node at `+0x90`, and flags `| 0x3f`).
3. **Settle slots**: `+0x4e6a0`, 64 × 0x30; only each slot's `+0x00` (the object) is set to 0 (`0x0033c558`).
4. **The punch bag's set**: a human body at `+0x4f2a0`, a segment at `+0x4f380`, a capsule at `+0x4f3f0`, constructed
   (filled in at step 10).
5. **The body vector** at `+0x4f450`: `+0x4f454`, `+0x4f458` and `+0x4f45c` = 0 (`+0x4f454` is the array
   `IPhysics_QueryBox` indexes by body `+0x20`; the other two inferred count and capacity; `+0x4f450` is not written).
6. **The tables** cleared with `memset` (`0x004307e8`): `+0x18` and `+0x7c`, 100 bytes each (5 × 5 entries);
   `+0xe0`, `+0xf4`, `+0x108`, 20 bytes each (5 entries). Then the non-zero entries stored, exactly those of
   [The shape-pair tables](physics.md#dispatch): entry (row, column) of the overlap table at `+0x18 + 0x14·row +
   4·column`, of the
   sweep table at `+0x7c + 0x14·row + 4·column`, rows and columns being the shape types 1-4 (row and column 0 stay
   empty); the mesh sweep, mesh overlap and ray entries at `+0xe0`, `+0xf4`, `+0x108` + 4·type
   (`0x0033c630`-`0x0033c7dc`; every address checked against that table, all agree).
7. **Strike spheres**: for each of the 60 at `+0xf7b0`, switch it off (shape vtable `+0x18` / `+0x1c` with 0, which
   clears `0x1`) and set flag `0x2`, so they end with flags `0x2` (`0x0033c7e0`-`0x0033c820`).
8. **Ignore slots**: `+0xf4e0`, 720 bytes (60 × 12) zeroed (`0x0033c830`).
9. **The 60 human body sets** (`0x0033c8a8`-`0x0033ca44`), for i = 0 to 59, body H = `+0x124b0 + 0xe0·i`:
   - capsule K = `+0x10e30 + 0x60·i`: `PhysicsCapsule_SetSize(K, height 2.0, radius 0.35)` (`0x00343508`: f12 to
     `+0x44`, f13 to `+0x40`); **H `+0x30` = K** (its only shape in the `+0x30` chain);
   - five segments, j = 0 to 4, at `+0x71a0 + 0x70·(5i + j)`: offset `+0x10` from `0x00512810 + 0x10·j`, bone `+0x31`
     the low byte of word j of the table copied from `0x00577198`, radius `+0x60` from `0x005128b0 + 4j`, length
     `+0x64` from `0x005128e0 + 4j`; flags `|= 0x2`, and `|= 0x4` when the bone is 3; appended to H's bone list
     (`PhysicsHumanBody_AddBoneShape` `0x003429e8`: H `+0x80`, chained through shape `+0x00`, appended at the tail);
   - five spheres, j = 0 to 4, at `+0x120 + 0x60·(5i + j)`: offset from `0x00512860 + 0x10·j`, bone from the table
     copied from `0x005771b0`, radius `+0x40` from `0x005128c8 + 4j`; flags `|= 0x2`, `|= 0x4` when the bone is 6;
     appended the same way.

   So H's bone list is, in order, the segments on bones 3, 24, 18, 32, 29 and the spheres on bones 6, 25, 19, 33, 30,
   with the sizes on [Combat](combat/index.md#moving-strikes); after construction their flags are `0x3`, or `0x7` for bones
   3 and 6 (enabled). `IPhysics_CreateHumanBody` (`0x0033cc48`) later switches every bone shape off and the capsule
   on, which gives the `0x2` / `0x6` of the Combat table ([Unit 9](physics-bodies.md#unit-9)).
10. **The punch bag** (`0x0033ca4c`-`0x0033caf4`): capsule `+0x4f3f0` `SetSize(2.0, 0.35)`; bag body `+0x30`
    (`+0x4f2d0`) = the capsule; bag body `+0x80` (`+0x4f320`) = the segment **directly** (no append call); segment
    offset `+0x10` = (0, 0, 0, 1), bone `+0x31` = 3, flags `+0x32` = **`0x4`** (stored, not or-ed: not enabled, no
    `0x2`), radius `+0x60` = 0.35, length `+0x64` = 1.5 (`0x3fc00000`). The posed sizes are in
    [Bodies](physics.md#bodies).

Constants: 2.0 `0x40000000`, 0.35 `0x3eb33333`, 1.5 `0x3fc00000`. Nothing else of the object is written: the human
and body lists start empty, and the widest-body bound starts at 0 until a body is inserted.

### IPhysics_Step {#u15-step}

`0x00340918`, 0x220 bytes. `void IPhysics_Step(IPhysics* phys /*a0*/)`. Called once per 60 Hz tick from
`TaskManager_TickGame` (at `0x003a2f8c`), after `Humans_Update` and the wheel's bucket
([Tasks](tasks.md#tick)), with `phys` = `[0x00597198]`.

1. **Bound refresh** (`0x00340940`-`0x00340994`). `+0x12` += 1 (u16). If it is now 120 or more: `+0x12` = 0,
   `+0x14` = 0.0, then for each body from the head `+0x08` along `+0x28`: w = body max.x (`+0x10`) − min.x (`+0x00`);
   if `+0x14` < w, `+0x14` = w. So the first refresh is on the 120th step after construction, then every 120 steps,
   and an empty list sets the bound to 0.
2. **Settle turns**, for slot i = 0 to 63 at `+0x4e6a0 + 0x30·i` whose object O (`+0x00`) is not 0
   (`0x003409b0`-`0x00340b08`):
   1. t′ = t + speed; speed′ = speed + accel (both from the old values); store both; if t′ > 1.0, t′ = 1.0.
   2. Read O's pose: owner slot `+0xa8` / `+0xac` (the world object's `Task_GetPose` `0x003a1e30`: the stored position
      and rotation, 32 bytes).
   3. rotation = `Quat_Slerp(t′, from = start (+0x20), to = end (+0x10))` (`0x00336a00`, f12 = t′, `a0` out, `a1`
      from, `a2` to); keep the position.
   4. Write the pose back: owner slot `+0x68` / `+0x6c` with (O, &pose). For the world object that is `0x00395220`
      (named `WorldObject_OnDetach` in Ghidra): **unless O is held (flag `0x10`)** it sets the task's transform
      (`Task_SetTransform` `0x003a16b8`), sets byte `+0x108` = 1 and refreshes O's render; a held object's rotation is
      not changed, but its slot still advances.
   5. If O has a body (owner slot `+0xf0` / `+0xf4`, called three times): body `+0x40 |= 0x80000000`, then **body
      slot `+0x10` / `+0x14`, `PhysicsBody_Update(body, O)`** (`0x00341c10`) at once, which re-poses its shapes and
      re-sorts it.
   6. If t′ = 1.0 exactly: O's flags (`+0x54`) `&= ~0x40000`, `|= 0x8000000`; the slot's object = 0.

With accel 0.02 (`0x3ca3d70a`, 0.0199999996) and t = speed = 0 at the start, t after tick n is 0.01·n(n − 1); in
float32: 0, 0.02, 0.06, 0.12, 0.2, 0.3, 0.42, 0.56, 0.72, 0.9 (0.89999986), then 1.1 clamped to 1.0 at tick 11. The
first tick changes nothing visible (t stays 0).

### IPhysics_StartSettle {#u15-start}

`0x00340fe0`, 0x110 bytes. `void IPhysics_StartSettle(IPhysics* phys /*a0*/, WorldObject* O /*a1*/, const vec4*
normal /*a2*/, u32 axisMask /*a3*/)`. One caller, `WorldObject_OnContact` (`0x00394714`): `normal` = the contact's
`+0x50`, `axisMask` = the type's `axis` byte (`+0x85`), only when that byte is non-zero and O's flags have neither
`0x40000` nor `0x8000000` ([Settling](physics.md#settle)).

1. Walk the 64 slots in index order; take the **first slot that is free or already holds O**
   (`0x00341038`-`0x003410c0`). A free slot before O's own is taken first, so a second call for an object that is
   already settling would give it two slots; the one caller never does that.
2. **No slot: return**, with nothing changed: no flag, the angular velocity kept, no end rotation (the 65th settle is
   refused silently).
3. The slot: `+0x08` accel = 0.02, `+0x00` = O, `+0x04` t = 0, `+0x0c` speed = 0.
4. **start** (`+0x20`) = the rotation of O's pose from owner slot `+0xa0` / `+0xa4`: for the world object
   `Obj_GetPoseNow` (`0x003a1ad8`), the pose **extrapolated to now**. Its rotation is O's rotation (`+0x20`) turned
   by the angular velocity (`+0x40`) over dt = phase time − O's update stamp (`+0x50`), at least 0
   (`0x003a1688`), when that velocity is non-zero and O is neither held (`0x10`) nor flagged `0x1000` (which
   slerps). `WorldObject_Integrate` (`0x00395a10`) sweeps (`0x00395b14`, where the contact calls this) before it stamps
   `+0x50` (`0x00395b48`), so dt here is the whole interval since O's last update (1/30 s in flight): the settle
   starts from the rotation O would have reached at the end of this update. Inferred that the extrapolation matches
   the integrator's own turn (`Quat_IntegrateAngular` `0x00335b08`), which this update then skips because O is
   settling.
5. O's angular velocity = (0, 0, 0, 1) (`0x005116c0`) through owner slot `+0x78` / `+0x7c`: for the world object
   `0x003a19a0` (no function in Ghidra: `lq` / `jr` / `sq` storing the vector at O `+0x40`).
6. O's flags `|= 0x40000`.
7. `Settle_ComputeTarget(phys, slot, normal, axisMask)` writes the end rotation.

### IPhysics_CancelSettle {#u15-cancel}

`0x003410f0`, 0x68 bytes. `void IPhysics_CancelSettle(IPhysics* phys /*a0*/, WorldObject* O /*a1*/)`. One caller,
`WorldObject_Remove` (`0x00391c38`). First O's flags `&= ~0x40000`, **always**, slot or not; then the first slot (in
index order) whose object is O gets object 0. Nothing else: O keeps the rotation it had reached, `0x8000000` is not
touched, and a second slot for O (step 1 above) would survive.

### Settle_NearestAxis {#u15-nearest}

`0x00340b38`, 0x1d0 bytes. `void Settle_NearestAxis(IPhysics* /*a0, unused*/, vec4* out /*a1*/, u32 mask /*a2*/,
const quat* q /*a3*/, const vec4* n /*t0*/)`.

1. M = the rotation matrix of q (`0x00336500`); its rows 0, 1, 2 are the object's local x, y, z axes in the world.
2. best = 1e9, k = −1. For i = 0, 1, 2 with mask bit i set: c = row_i · n; the angle is 0 if c ≥ 1.0, π
   (`0x40490fdb`) if c ≤ −1.0, else π/2 − atan(c / √(1 − c²)) (`0x003356a0`; π/2 is `0x3fc90fdb`), which is acos(c).
   If angle < best (strict), best = angle, k = i. So the **positive** axis nearest n wins, ties to the lower index.
3. Bit 3 of the mask (`_ROUND`): for each axis whose bit is clear, a local records the first such index, then 3; it is
   never read ([Settling](physics.md#settle)).
4. `out` = row k. **With no allowed axis (mask & 7 = 0) k stays −1 and `out` is read from 16 bytes below the matrix**
   (stack garbage). The one caller passes a non-zero `axis` byte, and no type in `level99` has `axis` 8 (bit 3 alone),
   so this is not reached in play; Coney's `settleAxis` returns none instead.

Comparing the dot products (largest first, strictly) as Coney does gives the same axis except when two angles round
to the same float.

### Settle_ComputeTarget {#u15-target}

`0x00340d08`, 0x2d4 bytes. `void Settle_ComputeTarget(IPhysics* phys /*a0*/, SettleSlot* s /*a1*/, const vec4* n
/*a2*/, u32 mask /*a3*/)`. Reads the slot's start (`+0x20`), writes its end (`+0x10`).

1. a = `Settle_NearestAxis(phys, mask, start, n)`.
2. c = a · n; k = n × a (`vopmula` / `vopmsub`), then k̂ = k × rsqrt(k·k) (`vrsqrt`).
3. θ = 0 if c ≥ 1.0, π if c ≤ −1.0, else acos(c) as above.
4. If k̂·k̂ < 1e-6 (`0x358637bd`): θ = 0 and k̂ = a. The test is on the **normalised** k̂: when a and n are parallel
   (k = 0) the EE's rsqrt of 0 gives a huge value and 0 × huge is 0, so k̂ = 0 and the test fires. Under IEEE that
   product is NaN and the test fails: an implementation must test |k| itself.
5. Wrap: if θ > π/2, θ −= π. (A second test, θ < −π/2 → θ = −(θ + π), cannot fire after the first.) Then θ′ = −θ.
6. r = (k̂·sin(θ′/2), cos(θ′/2)) (`sinf` `0x004b8c40`, `cosf` `0x004b8a70`; x, y, z then w at `+0x0c`).
7. end = r ⊗ start, the Hamilton product (w = r_w·s_w − r·s, xyz = r_w·s + s_w·r + r × s; `0x00340f28`-`0x00340f58`).
8. If end · start (4D) < 0, end = −end. Since (r ⊗ s)·s = r_w for a unit s and |θ′| ≤ π/2 gives r_w ≥ 0.7071, this
   never fires for a unit start (inferred, from the algebra).

The turn by θ′ = −acos(a·n) about n × a carries a onto n; when a is more than 90° from n it is turned by π − θ the
other way, which carries a onto −n. Either way the face across a lies on the surface: the object rests on its nearest
allowed face. Coney's `settleTarget` (`src/world_objects/loose_objects.cpp`) computes the same rotation by flipping a
and taking the shortest arc.

### Physics_StaticInit and its stub {#u15-static-init}

`0x0034f6c0`, 0x54 bytes: `void Physics_StaticInit(int initialise /*a0*/, int priority /*a1*/)`, GCC's static
initialiser for `physics.cpp`. When `priority` = 0xffff and `initialise` ≠ 0 it copies three vec4 from `0x00511720`,
`0x00511730`, `0x00511740` to `0x006eb9e0`-`0x006eba0f`: **(1, 0, 0, 1), (0, 1, 0, 1), (0, 0, 1, 1)**, the three unit
axes with w = 1. The stub `0x0034f718` (from the constructor list at `0x00534198`) calls it with (1, 0xffff). No
reader of `0x006eb9e0` was found (Ghidra lists only this write; an operand search for its low half `-0x4620` finds no
other use with that base), so it is inferred to be a file-scope constant nothing reads. Coney needs nothing for it.

### Test ideas {#u15-tests}

Synthetic: a world built by Coney's constructor, fake owners with a pose, a flags word and an optional body.

1. **Pools.** After construction: 300 bone spheres and 300 bone segments, 60 each of strike spheres, capsules and
   human body sets, 416 / 255 / 18 bodies and 416 spheres, 416 / 255 / 36 boxes, 30 push spheres, 64 free settle
   slots, 60 free ignore slots; lists empty, `+0x12` = 0, `+0x14` = 0.
2. **Human set i.** Capsule radius 0.35, height 2.0, flags `0x1`, the body's only `+0x30` shape; bone list order bones
   3, 24, 18, 32, 29, 6, 25, 19, 33, 30, segment lengths 0.38, 0.20, 0.20, 0.40, 0.40, radii 0.18, 0.07, 0.07,
   0.10, 0.10, sphere radii 0.15, 0.09, 0.09, 0.15, 0.15; flags `0x7` for bones 3 and 6, `0x3` for the rest.
   Strike spheres `0x2`. The bag's segment: bone 3, flags `0x4`, radius 0.35, length 1.5.
3. **Tables.** Every entry of [The shape-pair tables](physics.md#dispatch) is where the offsets say; row 0, column 0 and
   the
   listed "none" entries are empty; the ray table has types 1 and 3 only.
4. **Bound refresh.** Bodies of x widths 1.0 and 3.0: a body of width 5.0 inserted then removed leaves `+0x14` = 5.0
   for steps 1-119 (the insert grows it, [Unit 7](physics-bodies.md#unit-7)); step 120 sets it to 3.0 and `+0x12` to 0;
   with no
   bodies step 120 sets 0.0.
5. **Settle timing.** A slot started at tick 0: t after each step 0, 0.02, 0.06, 0.12, 0.2, 0.3, 0.42, 0.56, 0.72,
   0.9, 1.0 (float32 within 1e-6); at step 11 the flags go `0x40000` → `0x8000000`, the slot is free and the rotation
   equals the end.
6. **Refusal.** 64 objects settling: a 65th `StartSettle` leaves its flags, angular velocity and rotation untouched
   and no slot changes.
7. **Slot order.** Slots 0-63 busy, then slot 5 freed (cancel): the next start takes slot 5.
8. **Cancel.** Clears `0x40000` on an object with no slot; on a settling object frees its slot after, say, 4 steps and
   leaves the rotation at t = 0.12 of the turn; `0x8000000` untouched.
9. **Held.** A settling object with flag `0x10`: its rotation does not change, its slot still finishes at step 11.
10. **Body follows.** An object with a body: after each step the body is marked dirty and re-posed (its AABB follows
    the turned box).
11. **End rotation** (quaternions as (x, y, z, w), normal (0, 0, 1)): start identity, mask 4 (z): end = identity.
    Start identity, mask 1 (x): end = (0, −0.707107, 0, 0.707107) (x turned onto +z). Start 30° about y
    (0, 0.258819, 0, 0.965926), mask 4: end = identity. Start 120° about y (0, 0.866025, 0, 0.5), mask 4: θ = 120°
    wraps to −60°, end = (0, 1, 0, 0) (local z points down: the face across z is on the floor).
12. **Parallel guard.** Start identity, mask 4, normal (0, 0, −1): c = −1, θ = π wraps to 0, end = start (no NaN).
13. **Start pose.** An object spinning (angular velocity non-zero) that lands: the settle's start is its rotation
    advanced by the angular velocity over the time since its last update, and its angular velocity is then zero.

## Unit 16: RayCast leftovers and the world manager's ray {#unit-16}

**What it is.** The material id to name lookup, the collision mesh's chunk frees, the empty per-frame mesh update and
the world manager's ray cast, the one through which everything outside `RayCast/` and the cameras casts into the
level. All claims below are confirmed (code) at the cited addresses (read in the disassembly, 2026-10-12) unless a
line says otherwise; no runtime claims.

- **Coney:** `src/raycast/collision_mesh.*`: a material name table (reference data, names only) and a world ray
  whose result keeps the original's preset and its pass-through fill ([WorldManager_RayCast](#u16-world-ray)); the
  callers keep mask 0 and no exclusion list, which is what the original passes almost everywhere
  ([Who passes what](#u16-callers)). The mesh's destruction order is Unit 17's.
- **Tests:** [Test ideas](#u16-tests): the names of ids 0, 1, 2, 116, 190, 191 and 255; the world ray's preset
  (material 1, hit 0, every other field left as the caller had it) and its fill by an excluded triangle.

4 functions, 2636 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0034fc08` | `CollisionMaterial_GetName` | 2340 |
| `0x00350688` | `CollisionMesh_FreeChunks` | 240 |
| `0x00351158` | `CollisionMesh_UpdateEmpty` | 8 |
| `0x0040db88` | `WorldManager_RayCast` | 48 |

### CollisionMaterial_GetName {#u16-material-name}

`0x0034fc08`, `const char* CollisionMaterial_GetName(int id)` (`a0` id, `v0` the name). No static caller (no
cross-reference reaches it; inferred: a debug or tool leftover, or a caller through a pointer not found).

1. **Bound:** `id < 191`, compared **unsigned** (`sltiu a0, 0xbf` at `0x0034fc08`). Any other value, including
   every negative `int`, returns the string `unknown` (`0x005785c8`, the default case at `0x00350520`).
2. **Table:** otherwise jump through the 191-word table at `0x005785d0`. Every entry points at its own 12-byte case
   (`0x0034fc2c + 12 × id`), and every case returns a different string: no two ids share a name, no id falls to the
   default. The strings run from `0x00577560` (id 0) to `0x005785b8` (id 190) in id order.

Note the two "no material" names: id 0 is `undefined` and an id past 190 is `unknown`; id 1 is `MATERIAL_NONE`.

### The material names {#u16-names}

A curated reference list (names only, read from the strings the cases return). Every name is `MATERIAL_` followed by
the word given, except id 0 (`undefined`, no prefix) and id 1 (written in full). `ASHPHALT`, `LAMPOST`,
`GUILOTINE`, `PARKNMETER` and `SWITCHBLD` are spelt as the game spells them.

0 `undefined`, 1 `MATERIAL_NONE`, 2 `GLASS`, 3 `STEEL`, 4 `ALUMINIUM`, 5 `CONCRETE`, 6 `ASHPHALT`, 7 `LAMPOST`,
8 `SHOE`, 9 `FIST`, 10 `TORSO`, 11 `GARBAGE_CAN`, 12 `DUMPSTER`, 13 `CAR_HOOD`, 14 `CAR_HOOD_STREAM`, 15 `DRYWALL`,
16 `WOOD`, 17 `HEAD`, 18 `TRAINRAIL`, 19 `SHOVEL`, 20 `TRAINCAR`, 21 `METALDIVIDER`, 22 `GARBAGEBAG`, 23 `TINBOX`,
24 `RUBBLE`, 25 `ROLLUPDOOR`, 26 `HUMAN`, 27 `TV`, 28 `DRUM`, 29 `BAT`, 30 `LOW_FENCE`, 31 `OPAQUE_FENCE`, 32 `MAILBOX`,
33 `ELBOW`, 34 `KNEE`, 35 `GRASS`, 36 `DIRT`, 37 `BOTTLE`, 38 `FRIDGE`, 39 `SIGNS`, 40 `BASKETS`, 41 `SKID`, 42 `GEAR`,
43 `TRASHCAN`, 44 `TRASHCANLID`, 45 `SOFAS`, 46 `TYPEWRITER`, 47 `WOODFURNITURE`, 48 `MILKCRATE`, 49 `RADIATOR`,
50 `METALCHAIR`, 51 `OFFICECHAIR`, 52 `CINDERBLOCK`, 53 `WHEELCHAIR`, 54 `STEELBARRICADE`, 55 `THINWOOD`, 56 `MEDWOOD`,
57 `SHOPPINGCART`, 58 `HEAVYBOXES`, 59 `HOTDOGCART`, 60 `POOLBALL`, 61 `EMPTYBARREL`, 62 `FULLBARREL`, 63 `GARDENTOOLS`,
64 `PAN`, 65 `CROWBAR`, 66 `STICKS`, 67 `BASEBALL`, 68 `FIREEXTINGUISHER`, 69 `BENCH`, 70 `WELDINGTANKS`, 71 `TIRE`,
72 `RAT`, 73 `FIREWHOOSH`, 74 `STEELOBJECTS`, 75 `SOLIDOBJECTS`, 76 `ELECDEBRIS`, 77 `METALRODS`, 78 `GUILOTINE`,
79 `MINECART`, 80 `RIDECART`, 81 `PICKAXE`, 82 `WOODTABLE`, 83 `WIDEBOARD`, 84 `SMALLBOARD`, 85 `DEBRIS`, 86 `CHAIR`,
87 `LAMP`, 88 `GLASS_SMALL`, 89 `MUG`, 90 `MACHETE`, 91 `WOODFENCE`, 92 `MOLOTOV`, 93 `PORCELAIN`, 94 `PORCE_PART`,
95 `CAMERA_GHOST`, 96 `BLASTER`, 97 `SWITCHBLD`, 98 `PAINT`, 99 `CERAMIC`, 100 `CERAM_PART`, 101 `FEET`, 102 `BRICK`,
103 `CASHREG`, 104 `GLASS_FRAGILE`, 105 `ROOFTOP`, 106 `GRAVEL`, 107 `CHAINLINK_NOCLIMB`, 108 `BAG`, 109 `MEAT`,
110 `PLASTIC`, 111 `JAB`, 112 `BIGPUNCH`, 113 `BIGKICK`, 114 `VEGETATION`, 115 `GARBAGE`, 116 `BRICKWALL`,
117 `WOODWALL`, 118 `STEELWALL`, 119 `CAR_PART`, 120 `CAR_PART_STREAM`, 121 `CARPET`, 122 `RAILING`, 123 `JUKEBOX`,
124 `BEAM`, 125 `CABINET`, 126 `BATON`, 127 `BLOCK`, 128 `WEAPON_BLOCK`, 129 `CAR_DAMAGE`, 130 `RAZOR_CLUB`,
131 `DRUMKIT`, 132 `CYMBAL`, 133 `WOODSPLINTER`, 134 `GONG`, 135 `KEYBOARD`, 136 `PAINTCAN`, 137 `CHICKENBOX`,
138 `RICEBAG`, 139 `SPIKEBAT`, 140 `GLASSPART`, 141 `PLYWOOD`, 142 `GARBPART`, 143 `RUBBLEDEBRIS`, 144 `METALDEBRIS`,
145 `PAPERDEBRIS`, 146 `BIGWOOD`, 147 `WIREFRAME`, 148 `SMALLBOARDPART`, 149 `WIDEBOARDPART`, 150 `TINYWOOD`,
151 `SPEAKER`, 152 `CHAINMETAL`, 153 `CHAINWOOD`, 154 `GUITAR`, 155 `DRUMSTICK`, 156 `PARKBENCH`, 157 `PLASTERWALL`,
158 `BROKENBOTTLE`, 159 `TORSO_PRONE`, 160 `BREAKABLEDOOR`, 161 `HUMAN_LIGHT`, 162 `HUMAN_HEAVY`, 163 `FEET_LIGHT`,
164 `FEET_LAND`, 165 `COKEMACHINE`, 166 `PHONE`, 167 `FIREHYDRANT`, 168 `PARKNMETER`, 169 `PAINTING`, 170 `SUBWAYSEAT`,
171 `AQUARIUM`, 172 `DEFAULT_OBJ`, 173 `LIZZIE_DOOR`, 174 `BOSS_SHOE`, 175 `BOTTLE_SPECIAL`, 176 `MOLOTOV_SPECIAL`,
177 `BRICK_SPECIAL`, 178 `CHAIR_SPECIAL`, 179 `CLIMB_WOOD`, 180 `CHIMNEY`, 181 `STATUE`, 182 `STATUE_PIECE`,
183 `BOSS_FIST`, 184 `DEAD`, 185 `ICECREAM`, 186 `SAND`, 187 `STOREDOOR_GLASS`, 188 `PUNCHING_BAG`, 189 `FOAMHAND`,
190 `SALAMI`.

The same 191 ids index the sound matrix's material side (`src/audio/sound_matrix.h`, `kMaterials = 191`, from
`enum_preload.lua`); that the two lists name the same ids in the same order is inferred from the count, not compared.

### CollisionMesh_FreeChunks {#u16-free-chunks}

`0x00350688`, `void CollisionMesh_FreeChunks(CollisionMesh* mesh)`; the anchor of `RayCast/CollisionMesh.cpp` (path
string `0x005788f8`). Its only caller is the mesh's deleting destructor `CollisionMesh_DeletingDtor` (`0x004f12a8`,
vtable `0x005448b0` slot `+0x08`), which writes the mesh's vtable, calls it, writes the base vtable `0x00534298` and
calls `operator delete` only when its flags have bit 0.

It frees five pointers through the memory manager's `Free` (`0x005127e4`, slot `+0x58`, [Memory](memory.md)): each
goes to the pool on top of the heap stack, with the file string and a line number. **Order:**

| # | Field | Chunk | Line |
| --- | --- | --- | --- |
| 1 | `+0x80` | `0x06` index lists | 285 |
| 2 | `+0x78` | `0x05` grid | 286 |
| 3 | `+0x88` | `0x04` triangles | 287 |
| 4 | `+0x8c` | `0x07` vertices | 288 |
| 5 | `+0x94` | `0x52` checked bits | 289 |

No pointer is tested for null and none is cleared afterwards. The header (the `0x03` chunk, which **is** the mesh
object) is not freed here: the caller's delete frees it after the destructor ([LevelObject_Destroy](#u17-destroy)).
Nothing else in the mesh is touched.

### CollisionMesh_UpdateEmpty {#u16-update-empty}

`0x00351158`: `jr ra` and nothing else. Called with no arguments by eleven game modes' `Update` functions, not only
mode 1's: `Gm_Level_Update` (`0x00158d0c`), `PauseMode_Update`, `Gm_ANGameOver_Update`, `Gm_MissionFailed_Update`,
`GameStats_Update`, `RumbleResult_Update`, `MissionSelect_Update`, the modes `0x0e`, `0x11` and `0x12`, and
`Gm_Error_DrawOverWorld` (call sites `0x00155d00`-`0x0015f380`). Coney needs nothing for it; a tag on the mode update
is enough.

### WorldManager_RayCast {#u16-world-ray}

`0x0040db88`, `void WorldManager_RayCast(WorldManager* wm, const Ray* ray, RayResult* result, const u32*
excludeList, u32 mask)`: `a0`..`a3` and `t0` in the EE ABI. The ray and result records are on
[Collision](collision.md#ray-cast).

1. `result+0x28 = 1` (a `u32` store: `MATERIAL_NONE`) and `result+0x10 = 0` (hit). **Nothing else is preset:** the
   normal (`+0x00`), `t` (`+0x14`), `+0x18`, the flags (`+0x2c`), the area byte (`+0x2e`) and the triangle pointer
   (`+0x30`) keep whatever the caller left in them.
2. Call `CollisionMesh_RayCast(*(wm+0x40) + 0x04 → mesh, ray, result, excludeList, mask)` with every other
   argument passed through unchanged. `wm+0x40` (the level object) is read **without a null check**: a cast before
   `LoadLevel` or after `WorldManager_Unload(0)` reads a null or a freed level ([Unit 18](#u18-unload)).
3. **Return value:** none worth reading. `CollisionMesh_RayCast` (`0x00350cd8`) returns with `v0` holding the last
   loop comparison of its cell walk (`sltu` at `0x00351110`), and the wrapper passes it on. Callers read `+0x10`.

What the cast then writes, restated from the cell test (`0x00350778`) so this unit is complete:

- For each triangle reached and accepted (`0 <= t <= length`), the material filter (`0x00350538`) answers 1 (kept)
  or 0 (in `excludeList`).
- **While `hit` is 0**, the triangle fills the result whatever the filter said and with **no nearer-than test**:
  `t`, `hit` = the filter's answer, `+0x18` = 0, the material (`u32` at `+0x28`), the flags `& 0xfff` (`u16` at
  `+0x2c`), the area byte (`+0x2e`) and the triangle (`+0x30`). Only a kept triangle (filter 1) also writes the unit
  normal (`+0x00`), flipped to face the ray when the triangle is two-sided and `n·dir > 0`.
- **Once `hit` is 1**, an excluded triangle is skipped, and a kept one replaces the result only when its `t` is
  smaller than the stored `t`.

So after a cast: `hit` 1 means the nearest kept triangle is in the result. `hit` 0 with material 1 means nothing was
crossed (and `t`, normal, flags, area and triangle are the caller's old values). `hit` 0 with any other material
means an excluded triangle was crossed and none kept: the result holds the **last excluded triangle tested** (in the
walk's cell order, then the cell list's order), its `t`, flags, area and triangle, and the caller's old normal. A
kept triangle met after an excluded fill becomes the hit whatever the two `t` values.

### Who passes what {#u16-callers}

`WorldManager_RayCast` has **61 call sites in 46 functions** (the "46 callers" of Collision counts functions). Read at
each call site, 2026-10-12:

- **No exclusion list and mask 0** at 55 sites, among them `Collision_DropToGround` (`0x0034f994`),
  `Collision_DropToMarkedGround` (`0x0034faa0`), `Collision_MarchRay` (`0x0034f888`), every climb probe
  (`Climb_TryStart`, `Climb_ProbeTop`, `Climb_Start`, the two `Climb_OnFirstClipEnd*`), the human's ground and
  landing rays (`Human_SnapToGround` `0x0023eb9c`, `Human_LandingTest` `0x0023e5a4`, `Human_StartFall`,
  `Human_SetTransform`, `Human_Init`, `Human_TeleportNear`), `Player_TryJump`, `Player_Throw`, the AI's line and
  point tests and `Explosion_DamageHumansInRadius` (`0x00392834`). So Coney's mask 0 and empty list for the ground
  helpers and the human are the original's, not a choice.
- **Mask 8**, no list: the first cast of `Human_CanSeeObject` (`0x0021c69c`); its second (`0x0021c780`) passes 0.
- **A list and a mask**: `Human_TestStrikes` (`0x0033f6c0`) passes the list at its `sp+0x930` and mask `0x48`, or
  another value chosen by a test just before (`movn` at `0x0033f6a8`; not read further here).
- **Not read**: `Ray_IsClear` (`0x0024e040`), the second cast of `Garbage_Update` (`0x001710bc`) and
  `Interact_FindPartner` (`0x002a9454`) set `a3` and `t0` outside the 25 instructions before the call.

### What Coney changes {#u16-coney}

- **Material names.** A constexpr table of the 191 names above (`collision_mesh.h` or a `materials.h` beside it) and
  a `materialName(int id)` that returns `unknown` for any id outside 0 to 190, negative ones included.
  `@orig 0x0034fc08 CollisionMaterial_GetName (unknown)`; the file is inferred to be in `RayCast/`, not read.
- **The world ray.** `CollisionMesh::rayCast` returns `std::optional<RayHit>` and skips an excluded triangle
  **before** intersecting (`collision_mesh.cpp`, the exclusion test in the cell loop), so it cannot report the
  see-through triangle a clear ray crossed, which [AI: GetItem](ai/index.md#get-item) reads as "an intact pane in the way".
  Add a world-ray result that mirrors the original's: `hit`, `t`, normal, material (preset 1), flags, area and
  triangle, filled as [above](#u16-world-ray), with a `worldRayCast(level, ray, result&, exclude, mask)` that sets
  only `material = 1` and `hit = false` before the walk and leaves the rest of the caller's result alone.
  `@orig 0x0040db88 WorldManager_RayCast (WorldManagerPS2.cpp)` (inferred file: it sits in the
  `World/ps2/WorldManagerPS2.cpp` range, [Source map](source-map.md#world)). `rayCast` can stay as the convenience
  form that returns only a kept hit.
- **No level.** The original dereferences a missing level; Coney returns no hit (material 1) and asserts in debug
  builds, a marked Coney choice.
- **Freeing the mesh.** Nothing to add: the mesh owns its chunks, and the order of the five frees is not observable
  ([Unit 17](#u17-destroy) gives the level object's order, which is).

### Test ideas {#u16-tests}

All on synthetic meshes from `src/raycast/collision_builder.h`; expected values follow from the spec above.

- **Names:** `materialName(0)` = `undefined`, `1` = `MATERIAL_NONE`, `2` = `MATERIAL_GLASS`, `116` =
  `MATERIAL_BRICKWALL`, `190` = `MATERIAL_SALAMI`, `191` = `unknown`, `255` = `unknown`, `-1` = `unknown`; 191
  distinct names.
- **Preset, nothing crossed:** an empty mesh area, ray from (0, 0, 1) along +x, length 5, a result prefilled with
  `t` = 7.0, normal (0, 0, 9) and triangle 42: after the cast `hit` false, material 1, `t` 7.0, normal (0, 0, 9),
  triangle 42.
- **A pane crossed, glass excluded:** a two-sided vertical triangle of material 2 across x = 1 and a ray from
  (0, 0, 1) along +x, length 5, list {2}: `hit` false, material 2, `t` = 1.0, the pane's triangle, normal still the
  caller's.
- **The same pane, no list:** `hit` true, material 2, `t` = 1.0, normal (−1, 0, 0) (flipped to face the ray).
- **Excluded then kept, farther:** the pane at x = 1 (material 2) and a wall at x = 3 (material 3), list {2}: `hit`
  true, `t` = 3.0, material 3.
- **Kept then excluded, nearer, in the same cell** (wall listed before the pane): `hit` true, `t` = 3.0, material 3
  (an excluded triangle never replaces a kept hit).
- **Two excluded panes in one cell's list**, at x = 2 listed first and x = 1 listed second, list {2}: `hit` false,
  `t` = 1.0 (the last tested, not the nearest); listed the other way round, `t` = 2.0.
- **Mask:** a floor with flag `0x08` under a ray cast with mask 8 is not hit (material stays 1); with mask 0 it is.

## Unit 17: Level file and level object lifecycle {#unit-17}

**What it is.** The WAD object's opener and lookups, the level file's load (`WorldLevel_Load`), the `0x17` handler's
registration, the level object's constructor, destructor and occluder setter, and `WorldLevel_Release`, which
destroys the level object on unload. Confirmed (code) at the cited addresses unless a line says otherwise.

- **Coney:** `src/fileio/wad.*` (the lookups), `src/world/level_object.*` (an explicit destructor in the original's
  order, the `WorldLevel_Load` tag), `src/platform/level_file.*`, and the unload path that stands for
  `WorldLevel_Release` ([What Coney changes](#u17-coney)).
- **Tests:** [Test ideas](#u17-tests): a synthetic WAD index finds, sizes and misses entries in any case; a level
  file round trip leaves both chunk stacks empty; destroying the level object releases its parts in the original's
  order (models before their dictionaries).

10 functions, 1028 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0040c5e0` | `Wad_Open` | 68 |
| `0x0040c628` | `Wad_FindEntry` | 28 |
| `0x0040c648` | `Wad_GetEntrySize` | 32 |
| `0x0040c668` | `Wad_HasEntry` | 32 |
| `0x0040c688` | `WorldLevel_Load` | 356 |
| `0x0040c7f0` | `WorldLevel_Release` | 116 |
| `0x0040cf18` | `LevelHeader_RegisterChunkHandler` | 40 |
| `0x0040cf40` | `LevelObject_Construct` | 60 |
| `0x0040cf80` | `LevelObject_Destroy` | 264 |
| `0x0040d088` | `LevelObject_SetOccluders` | 32 |

### The WAD object {#u17-wad}

A one-word object (`+0x00` the `DVDWadIndex*`), the global `0x006f39e8` reached through `0x005147a4`
([Boot](boot.md#initialisation-order)).

- **`Wad_Open(wad)`** (`0x0040c5e0`, boot step 8): `wad+0x00 = DVDWadIndex_Create("cdrom0:\WARRIORS.DIR;1")`
  (`0x00149040`), then `MS_OpenStreamFile(0, "cdrom0:\WARRIORS.WAD;1", 0)` (`0x001500a8`, the IOP command `0x1f`).
  No error check of either ([File I/O](file-io.md#opening-the-wad-at-boot)).
- **`Wad_FindEntry(wad, name)`** (`0x0040c628`): returns `DVDWadIndex::Find(wad+0x00, name)` (`0x001490b8`): the
  12-byte entry or null. `Find` **lowercases `name` in place** before hashing ([Resolving a
  name](file-io.md#resolving-a-name)), so the caller's buffer changes. Callers: `IopStream_Start` (`0x00149410`) and
  `AudioDevice_LoadBank` (`0x0014c620`).
- **`Wad_GetEntrySize(wad, name)`** (`0x0040c648`): `Find(...)` then the entry's `+0x04` (size), **with no null
  check**: a missing name reads address 4. Caller: `PS2StreamFileSys_GetSize` (`0x00148ba8`).
- **`Wad_HasEntry(wad, name)`** (`0x0040c668`): `Find(...) != 0`. Caller: `PS2StreamFileSys_Exists` (`0x00148bf0`).

The names these receive are already prefixed `./ee_files/` by the stream file system ([File
I/O](file-io.md#resolving-a-name)); the WAD object adds nothing.

### WorldLevel_Load {#u17-load}

`0x0040c688`, `LoadedObject* WorldLevel_Load(const char* name)`. Its only caller is `WorldManager_LoadLevel`, which
has the `World Level Pool` on top of the heap stack. Steps:

1. `snprintf(buf, 255, "%s.lev", name)` (format `0x00588638`, a 256-byte stack buffer).
2. Push the `Level Dynamic & LUA Pool` (`0x006eb9b8`). `file = fs.Open(buf, 0)` (device `0x0050b788` `+0x90`, the
   current file system, slot `+0x60`). Build a `BufferedStream` on the stack over `file`, the file manager's buffer
   (`+0x3c`) and its size (`+0x40`). Pop.
3. `ChunkSystem_LoadContainer(stream)` (`0x00144180`) with the caller's pool on top: every chunk and object the
   level file makes goes into the `World Level Pool`.
4. Push the Lua pool; detach the stream; `fs.Close(file)` (slot `+0x68`); pop.
5. `level = ChunkSystem_PopObject()`; destroy the stream (`File_Destroy(stream, 2)`); return `level`.

No step checks a failure: a missing file, a short read or an empty object stack is not detected here (inferred: the
file system and chunk system handle or ignore them; not traced). Nothing checks that the popped object is a level.

### LevelHeader_RegisterChunkHandler {#u17-register}

`0x0040cf18`: `ChunkSystem_SetHandlers(0x17, 0x0040ce30, 0)` (`0x00144348`): chunk `0x17` gets `onLoaded` =
`LevelHeader_OnLoaded` and no second handler. Boot step 9 ([Chunk
system](chunk-system.md#handlers-registered-at-run-time)).

### LevelObject_Construct and LevelObject_SetOccluders {#u17-construct}

- **`LevelObject_Construct(level)`** (`0x0040cf40`): `+0x00` = vtable `0x00545b88`; the ten words `+0x04` to `+0x28`
  = 0; returns `level`. It runs on the `0x17` chunk's own 48 bytes, so `+0x2c` to `+0x2f` keep the chunk's bytes
  (unused). `LevelHeader_OnLoaded` (`0x0040ce30`) calls it only when `PopChunk(0x17)` is non-null, and then writes
  through the result unconditionally (a missing chunk would write through null).
- **`LevelObject_SetOccluders(level, occluders)`** (`0x0040d088`): `level+0x28 = occluders`, then
  `Occluders_Set(occluders)` (`0x0017a560`), which converts their points ([Occluders](level-loading.md#occluders)).

The vtable `0x00545b88` has one used slot: `+0x08` / `+0x0c` = `LevelObject_Destroy`.

### LevelObject_Destroy {#u17-destroy}

`0x0040cf80`, `void LevelObject_Destroy(LevelObject* level, u32 flags)`. Runs from `WorldLevel_Release` on every
`WorldManager_Unload(keep = 0)` ([below](#u17-release)); this answers [Level loading](level-loading.md#open-questions)'s
"no caller on the unload path". Order of frees, exactly:

| # | What | How | Then |
| --- | --- | --- | --- |
| 1 | vtable | `+0x00` = `0x00545b88` | |
| 2 | collision mesh `+0x04` | `SafeDelete` `0x004f4b78`: if non-null, its destructor (slot `+0x08`, flags 2: [FreeChunks](#u16-free-chunks)), then `Free(mesh)` (`WorldLevelPS2.cpp`, line 144) | `+0x04` = 0 |
| 3 | occluders `+0x28` | `SafeFree` `0x004f4bf8`: if non-null, `Free` (line 147) | `+0x28` = 0 |
| 4 | level world `+0x0c` | device (`0x0050cdb4`) slot `+0x178`: destroy the RenderWare world | `+0x0c` = 0 |
| 5 | glow dictionary `+0x08` | device slot `+0x158`: destroy the texture dictionary | `+0x0c` = 0 **again**; `+0x08` is never cleared |
| 6 | skyline model `+0x14`, then its dictionary `+0x10` | `0x004693f0` (clump destroy), `0x00488c58` (dictionary destroy) | not cleared |
| 7 | sky box `+0x1c`, then `+0x18` | the same | not cleared |
| 8 | cloud box `+0x24`, then `+0x20` | the same | not cleared |
| 9 | vtable | `+0x00` = `0x00534298` (the base) | |
| 10 | the object | `operator delete` (`0x0042c820`) only when `flags & 1` | |

Steps 4 to 8 pass their pointers without a null test (the device's and RenderWare's functions are not read here for
their null handling). Every `Free` goes to the pool on top of the heap stack, which `WorldManager_Unload` has made
the `World Level Pool`. Each model goes **before its own dictionary**, and the three model pairs go in the order
skyline, sky box, cloud box, which is not the order the handler linked them (sky box, skyline, cloud box).

### WorldLevel_Release {#u17-release}

`0x0040c7f0`, `void WorldLevel_Release(LevelObject* level)`; anchor of `World/WorldLevel.cpp` (path `0x00588640`).
Only caller: `WorldManager_Unload` with `keep` = 0, passing `wm+0x40`. Steps:

1. `LightGlow_Release(lightManager)` (`0x0017e680` on `0x0050cce4`, [Lighting](lighting.md)).
2. `Captions_Release(0x00619570)` (`0x001cafa0`, [Movies](movies.md#caption-text)).
3. **Delete the level object**: `SafeDelete_WorldLevel(level, "WorldLevel.cpp", 45)` (`0x004f4af8`): if non-null,
   the destructor through vtable `+0x08` with flags 2 ([above](#u17-destroy)), then `Free(level)` into the current
   pool.
4. Five calls to empty functions (`0x00386f58`, `0x00386f68`, `0x00386f78`, `0x00386f88`, `0x00386f98`, each
   `jr ra`).

There are no string tables here: what [Level loading](level-loading.md#the-level-object) calls "frees the string
tables" is step 3 and the empty calls.

### What Coney changes {#u17-coney}

- **WAD lookups.** `io::Wad::lookup` already covers `Wad_FindEntry` and `Wad_HasEntry` (any case, no in-place
  lowercasing, a `NotFound` error for a miss). Add `entrySize(name)` returning `std::expected<std::uint32_t, Error>`
  for `Wad_GetEntrySize`, where the original crashes on a miss (a marked Coney choice). Tags:
  `@orig 0x0040c5e0 Wad_Open (World/unknown)` on `Wad::open`, `0x0040c628 Wad_FindEntry`, `0x0040c648
  Wad_GetEntrySize`, `0x0040c668 Wad_HasEntry` (all `World/unknown`).
- **The level file.** `world::loadLevelFile` stands for `WorldLevel_Load`; its comment says the function is in the
  tolua range and carries no tag, which [Level loading](level-loading.md#open-questions) has since answered (game
  code in `World/`, no path string). Tag it `@orig 0x0040c688 WorldLevel_Load (World/unknown)`. Its stack checks
  are Coney's (the original checks nothing).
- **The destructor.** `world::LevelObject`'s members are destroyed in reverse declaration order: subtitles, path data,
  occluders, cloud box, sky box, skyline, level world, glow dictionary, collision. Give it an explicit destructor that
  releases them in the original's order ([table](#u17-destroy)): collision, occluders, level world, glow dictionary,
  then each model **before** its dictionary, skyline, sky box, cloud box. Within `LevelModel` the default order
  already destroys the model (declared second) before the dictionary. `@orig 0x0040cf80 LevelObject_Destroy
  (WorldLevelPS2.cpp)`. Whether the order shows to a player is not known (inferred: it matters only for texture
  reference counts, which librw keeps itself); it costs nothing to keep.
- **Release on unload.** `WorldLevel_Release`'s three effects belong to Coney's unload ([Unit 18](#u18-coney)): the
  light glows reset, the captions cleared, the level object destroyed. `@orig 0x0040c7f0 WorldLevel_Release
  (WorldLevel.cpp)`.
- **Small ones.** `LevelObject_Construct` and `LevelHeader_RegisterChunkHandler` are what `makeLevelHeaderHandler`
  and `addLevelFileHandlers` already do; tag them there. `LevelObject_SetOccluders` is the occluders' assignment in
  the `0x17` handler (`readOccluders` does the conversion).

### Test ideas {#u17-tests}

- **Lookups on a synthetic index** of three entries named `./ee_files/level1.lev`, `./ee_files/warriors.glr` and
  `./ee_files/a.pak` with sizes 100, 200, 300: `lookup("LEVEL1.LEV")` finds the first, `entrySize("warriors.glr")` =
  200, `lookup("missing.lev")` and `entrySize("missing.lev")` fail with `NotFound`, a name differing only in case
  hashes the same.
- **A level file round trip** (synthetic chunks in the 18-chunk order of [The level
  file](level-loading.md#the-level-file)): after `loadLevelFile` both chunk stacks are empty and the object's ten
  fields are set.
- **Destruction order:** with recording stand-ins for the platform objects, destroying a `LevelObject` records
  collision, occluders, level world, glow dictionary, skyline model, skyline dictionary, sky model, sky dictionary,
  cloud model, cloud dictionary.
- **No leak:** a level object built over a synthetic collision mesh and destroyed leaves nothing allocated (the
  `asan` preset, or an allocation counter in the test).

## Unit 18: World manager lifecycle {#unit-18}

**What it is.** The `Sector Pool`'s creation at boot, the world manager's constructor (the global data), loading and
unloading a level, and its small accessors. This unit settles the pool sizes, the heap-stack pairing, what an unload
gives back and what it leaves behind. Confirmed (code) at the cited addresses unless a line says otherwise.

- **Coney:** a platform-neutral `world::WorldManager` (`src/world/world_manager.*`, new) owning the level's budget
  reservations, its streamed worlds' state and its level object, with `loadLevel(name, headerOnly)` and
  `unload(keepLevelFile)`; `src/platform/world_viewer_mode.cpp` (`loadLevelScenery`), `play_scenery.*`,
  `game_session.*` and `play_level_world.cpp` go through it ([What Coney changes](#u18-coney)).
- **Tests:** [Test ideas](#u18-tests): the pool sizes; load then `unload(0)` returns the budget to what it was;
  `unload(1)` keeps the level file's reservation; `ResetVisibility` clears the latch and both worlds' marks; the load
  priority stores its value as given (no clamp).

10 functions, 3700 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0040d688` | `WorldManager_CreatePools` | 628 |
| `0x0040d900` | `WorldManager_Construct` | 644 |
| `0x0040dbb8` | `WorldManager_LoadLevel` | 1280 |
| `0x0040f5b8` | `WorldManager_Unload` | 664 |
| `0x0040e0b8` | `WorldManager_ResetVisibility` | 68 |
| `0x0040e1a0` | `WorldManager_QueuePack` | 308 |
| `0x0040e8d0` | `WorldManager_GetSolidWorld` | 8 |
| `0x0040f850` | `WorldManager_GetSectorPool` | 80 |
| `0x0040c938` | `WorldManager_SetLoadPriority` | 12 |
| `0x0040cc68` | `PVS_CaptureSamplePoint` | 8 |

Notation: "push P" and "pop" are the memory manager's heap stack (slots `+0xb8` / `+0xc0`; pop ignores its argument,
[Memory](memory.md#memory-manager)); "new X (line n)" is an `Alloc` from the pool on top of the stack with
`WorldManagerPS2.cpp` and line `n`; "track" is `TrackPool` (slot `+0xd8`, which on retail returns the pool unchanged);
"register" is `RegisterPool(name, pool, parent)` (slot `+0xc8`).

### WorldManager_CreatePools {#u18-create-pools}

`0x0040d688`, no arguments; boot step 21 ([Boot](boot.md#initialisation-order)).

1. Push the global heap (manager `+0x08`). `L` = the current pool's `LargestFreeBlock` (slot `+0x80`).
2. new `MemoryPoolHeap` object (0x10 bytes, tag `MemoryPoolHeap`, align 16, line 121, flag 1); vtable `0x00544428`.
   `MemoryPoolHeap_InitInParent(heap, global, L − 0x20000, "Sector Pool")`: **no minimum** (an `L` under 128 KB
   would give a negative size; inferred never to happen). Track it into `0x006eb9c8`; register `Sector Pool` under
   the global heap. Pop.
3. `L2` = the current pool's `LargestFreeBlock`; `size2 = L2 − 0x20000`, raised to `0x1000` when smaller (a signed
   compare). new heap object (line 134, flag 1); `InitInParent(global, size2, "Sector Pool 2")`; track into
   `0x006eb9cc`; register under the global heap. No push around this second one: it is allocated from whatever pool
   is on top after the pop (the global heap at boot).

Sizes on a retail boot: 17,217,536 and 4,096 bytes ([Sizes at runtime](memory.md#sizes-at-runtime), confirmed
(runtime) there).

### WorldManager_Construct {#u18-construct}

`0x0040d900`, `WorldManager* WorldManager_Construct(WorldManager* wm)` on the 0x60-byte object (boot step 26).

1. Zero `+0x04` to `+0x28` and call `PackDeque_InitMap(wm, 0)` (`0x004f4d20`): `+0x00`-`+0x28` is an SGI-STL
   `deque<string>` of pack names (start iterator `+0x0c`-`+0x18`, finish iterator `+0x1c`-`+0x28`: current, first,
   last, node, as `WorldManager_QueuePack` reads them). Zero `+0x50`, `+0x54`, `+0x58` (the draw list), `+0x38`,
   `+0x40`, `+0x44`, `+0x48`, `+0x5c`, `+0x2c`, `+0x34`. `+0x30` and `+0x4c` are not written.
2. `size` = the file system's `GetSize("warriors.glr")` (slot `+0x58`).
3. new `MemoryPoolClump` object (0x18 bytes, line 179) from the pool on top of the stack (**not** the `Sector
   Pool`: nothing is pushed); vtable `0x00544370`. `MemoryPoolClump_InitInParent(clump, SectorPool, size × 101 / 100,
   "Global Data")` (32-bit unsigned multiply and divide, so the size is truncated). Track it as `GlobalPool` into
   `+0x3c`; register `Global Data Pool` under the `Sector Pool`.
4. Open `warriors.glr` (slot `+0x60`), wrap it in a `BufferedStream` on the file manager's buffer, push `+0x3c`,
   `ChunkSystem_LoadContainer`, pop, detach, close (slot `+0x68`), destroy the stream. Return `wm`.

Nothing ever frees the `Global Data Pool` (no other reference to `+0x3c` frees it; inferred from the unload below).

### WorldManager_LoadLevel {#u18-load}

`0x0040dbb8`, `int WorldManager_LoadLevel(WorldManager* wm, const char* name, int headerOnly)`; returns 1 always.
Callers: `main` (`0x00144778`, `level1` with `headerOnly` 1) and `InitLevel` (`0x00160210`, `headerOnly` 0).

1. `snprintf(buf, 255, "%s.lev", name)`; print `Looking for file : %s`; `size` = `GetSize(buf)` (slot `+0x58`).
2. Push the `Sector Pool`. new clump object (line 234). `levelPool = max(size × 103 / 100, 0x40000)` (unsigned,
   truncated; 0x40000 = 262,144). `InitInParent(clump, SectorPool, levelPool, "World Level Pool")`; track into
   `+0x38`; register `World Level Pool` under the `Sector Pool`. Pop.
3. **Push the `Level Dynamic & LUA Pool` and leave it pushed.** This function never pops it; `WorldManager_Unload`
   with `keep` 0 does ([below](#u18-unload)). So during a level the Lua pool is the default pool, and the world
   objects of step 4 come from it.
4. Unless `headerOnly`, with `world` = the current level record's world name (`W_GameState` `0x0051489c`, record
   index `u16` at `+0x56dc`, records of 0x84 bytes from `+0x14d4`, name at record `+0x39`):
    - if the file system's `Exists("<world>s_sec.wld")` (slot `+0x50`): new `World` (0x2188 bytes, tag `World`, line
      251) → `World_Construct` → `+0x44`, then `World_ReadManifest(+0x44, "<world>s")` and
      `World_LoadStream(+0x44, "<world>s")`; then new `World` (line 259) → `+0x48`, read and load `"<world>d"`;
    - otherwise `+0x48 = 0` first, then new `World` (line 270) → `+0x44`, read and load `"<world>"`.

    A failed allocation stores null and the manifest read follows anyway (no check).
5. Push `+0x38`; `+0x40 = WorldLevel_Load(name)` ([Unit 17](#u17-load)); pop.
6. Print the banner and the `Sector Pool`'s free bytes (slot `+0x88`); `0x005147d0` = those free bytes (written, never
   read); `0x005147cc` = 0.001 (`0x3a83126f`).

With `headerOnly` the worlds are skipped but the `World Level Pool`, the level file and the Lua push all happen.

### WorldManager_Unload {#u18-unload}

`0x0040f5b8`, `void WorldManager_Unload(WorldManager* wm, int keepLevelFile)`. Callers: `main` (`0x00144830`, keep 0,
after the boot's header-only load), `UnloadLevel` (`0x00160b64`, keep = its own argument) and `Gm_Level_Exit`
(`0x00158554`, keep 0). Mode 1's exit passes the "level change pending" flag `0x0050c754` to `UnloadLevel`, so a
level change keeps the level file through the outro movie, then `Gm_Level_Exit` unloads it with keep 0; without a
level change `UnloadLevel(0)` unloads everything at once (`0x001584f4`, `0x00158554`).

**Always:**

1. Device (`0x0050cdb4`) slot `+0x18`: flush the render queue. Task manager (`0x00512c7c`) slot `+0x48`.
2. If `+0x44`: `World_Unload(GetSolidWorld(wm))` (`0x00410b70`: its parts, world, dictionary and clump, [The
   streamed world](world.md#streaming)); delete the world object (`0x004f57d8`: `World_Destroy` then `Free`, line
   1160); `+0x44 = 0`.
3. If `+0x48`: the same (line 1168); `+0x48 = 0`.
4. If `+0x5c`: delete the water effect (`0x004f5848`, line 1174); `+0x5c = 0`.

The deletes free into the pool on top of the stack, which is the Lua pool that `LoadLevel` pushed, where the objects
came from.

**Only when `keepLevelFile` is 0:**

1. **Pop** (the Lua pool pushed by `LoadLevel`).
2. Push the `World Level Pool` (`+0x38`); its `SetOption(0, 1)` (count frees).
3. `WorldLevel_Release(+0x40)` ([Unit 17](#u17-release)): the glows, the captions, the level object and its mesh,
   occluders and models.
4. Free the path data (`0x0051058c`, line 1187, `0x004f58b8`: if non-null).
5. `SetOption(0, 0)`; pop.
6. Push the `Sector Pool`. The level clump's `CheckEmpty` (slot `+0x70`; its result is not used); `UnregisterPool`
   (manager slot `+0xd0`); the `Sector Pool`'s `SetOption(0, 1)` (a heap ignores it); the clump's `Destroy(Sector
   Pool)` (slot `+0x90`: its block goes back); delete the 0x18-byte clump object (`0x004de3d8`, line 1200), which
   `LoadLevel` allocated from the `Sector Pool`; `SetOption(0, 0)`; pop.
7. `+0x38 = 0`.

**Left behind**, by both paths: `+0x40` still points at the freed level object (a `WorldManager_RayCast` after an
unload reads freed memory; inferred that nothing casts then); the path data globals `0x0051058c` and `0x00510584`
keep their old values; `+0x2c`, `+0x34`, the draw list and the pack queue are not touched; the `Global Data Pool` is
never freed. With keep 1, the `World Level Pool`, the level object and the Lua push all stay.

**What an unload with keep 0 gives back to the `Sector Pool`:** the `World Level Pool`'s block and its clump object,
and (inside step 2's `World_Unload`) each world's clump and every loaded part's `Sectors<i>` clump. To the Lua pool:
the two 0x2188-byte world objects, their 140 vector buffers (32 bytes each, [Unit 19](#u19-construct)) and the
water effect. So the `Sector Pool` returns to what it held before `LoadLevel`, apart from what the resource manager
holds in it (its clumps are its own, freed by `UnloadLevel`'s later steps).

**The heap stack:** `LoadLevel` leaves it one deeper; `Unload(0)` makes it one shallower; `Unload(1)` leaves it as
it is. Each path above runs `LoadLevel` once and `Unload(0)` once, so they pair (inferred from the three call sites;
whether a level can start again after `Unload(1)` without an `Unload(0)` is not traced).

### The small ones {#u18-small}

- **`WorldManager_ResetVisibility(wm)`** (`0x0040e0b8`): `+0x34 = 0`; `World_ResetVisibility(GetSolidWorld(wm))`
  **without a null check** of `+0x44`; then `World_ResetVisibility(+0x48)` when it is non-null. Callers:
  `WorldManager_Preload` and `ResourceMgr_LoadAllBlocking` (`0x001889d0`). What `World_ResetVisibility` clears:
  [Visibility](world.md#visibility).
- **`WorldManager_QueuePack(wm, name)`** (`0x0040e1a0`): appends a copy of `name` (a reference-counted string,
  allocated through the STL pool `0x006eb9c0`) at the deque's back: in place when the finish node has room (finish
  current ≠ finish last − 4), else through `PackDeque_PushBack`. No limit, no duplicate check. Only caller:
  `World_QueuePackToPrecache` (`0x0040cc40`).
- **`WorldManager_GetSolidWorld(wm)`** (`0x0040e8d0`): returns `+0x44`.
- **`WorldManager_GetSectorPool(wm, &largestFree)`** (`0x0040f850`): `wm` is not read; when `largestFree` is
  non-null it receives the `Sector Pool`'s `LargestFreeBlock`; returns the `Sector Pool` (`0x006eb9c8`). Callers:
  `GameStats_Update`, `Gm_Mode11_Update`, `ResourceMgr_Heap`, `ResourceMgr_ServiceDeferred`, `WorldManager_Update`,
  `World_RequestPart`.
- **`WorldManager_SetLoadPriority(scale)`** (`0x0040c938`): stores the float argument (`f12`) into `0x005147cc`
  as it is: **no clamp**, no check, no return value. Callers: `Gm_Mode11_Enter` (`0x0015e9d0`, 10.0) and the Lua
  binding `lua_SetLoadPriority` (`0x0036e630`); `LoadLevel` writes 0.001 itself. The streaming update multiplies the
  resource manager's distances by it ([Choosing what to stream](world.md#streaming)).
- **`PVS_CaptureSamplePoint`** (`0x0040cc68`): `jr ra` ([Debug](debug.md)).

### What Coney changes {#u18-coney}

Coney allocates normally ([Memory](memory.md#what-a-reimplementation-must-keep)), so the pools become reservations in
`world::SectorBudget` and the heap stack has no counterpart. What a player can see is the budget, which decides
evictions, so it must come back exactly on unload.

- **A `world::WorldManager`** (new, platform-neutral; the worlds' renderer objects stay in `platform::WorldSet`)
  holding: the `Global Data` reservation, made **once** when the manager is made (`globalDataPoolBytes`, already
  101 % truncated); the `World Level Pool` reservation (`worldLevelPoolBytes`); the streamed worlds; the level object;
  the visibility latch (`+0x34`); the pack queue; the load priority (initially whatever the boot leaves; `LoadLevel`
  sets 0.001). `@orig 0x0040d900 WorldManager::WorldManager (WorldManagerPS2.cpp)`.
- **`loadLevel(name, headerOnly)`**: reserve the level pool first, then (unless header-only) the worlds in the order
  above, then the level file; set the load priority to 0.001. `@orig 0x0040dbb8`.
- **`unload(keepLevelFile)`**: release the worlds (and their parts) and the water always; with keep false, also run
  the [release](#u17-release) (glows, captions, level object), drop the path data and release the level pool's
  reservation. Coney clears the level pointer (where the original leaves it dangling, a marked Coney choice).
  `@orig 0x0040f5b8`.
- **A leak to fix now.** `loadLevelScenery` (`src/platform/world_viewer_mode.cpp`) reserves the `Global Data` and
  `World Level` pools on every level load and nothing releases them (`LevelPlayScenery` and `WorldSet` release only
  the worlds and parts), so a session that plays a second level (the one budget made in `main.cpp`) streams with
  less room than the original. The manager above fixes it; until then, release both when the scenery goes.
- **`setLoadPriority(float)`** stores the value unchanged; it has no reader until the resource manager streams
  (a stand-in). `@orig 0x0040c938`.
- **`queuePack(name)`** appends to a `std::deque<std::string>`; the preload consumes it (`world_streamer`'s preload).
  `@orig 0x0040e1a0`.
- **`resetVisibility()`** clears the latch and calls `StreamedWorld::resetVisibility` on each world. Coney's
  `StreamedWorld` cannot be null, so the original's missing null check has nothing to copy. `@orig 0x0040e0b8`.
- **`sectorPool(&largestFree)`**: the budget, with `freeBytes()` standing for the largest free block (the
  approximation [Memory](memory.md#what-a-reimplementation-must-keep) recommends). `@orig 0x0040f850`.
- **`CreatePools`**: `kSectorPoolSize` (the retail runtime value) already stands for it; tag the constant
  `@orig 0x0040d688 WorldManager_CreatePools (WorldManagerPS2.cpp)`. `Sector Pool 2` has nothing to stand for.

### Test ideas {#u18-tests}

With a `SectorBudget` of 17,217,536 bytes and synthetic file sizes:

- **Pool sizes:** `worldLevelPoolBytes(100,000)` = 262,144; `(254,500)` = 262,144 (262,135 raised);
  `(254,524)` = 262,159; `(300,000)` = 309,000. `globalDataPoolBytes(99)` = 99 (9,999 / 100 truncated);
  `(1,000)` = 1,010. `Sector Pool 2` for `L2` = 100,000: 4,096; for 200,000: 68,928.
- **Load then unload(0):** with the global data reserved, `used()` before `loadLevel` equals `used()` after
  `unload(false)`, for a two-world level and for a header-only load; the level object is gone.
- **Unload(1):** after `unload(true)` the worlds are released and exactly the level pool's reservation remains; a
  following `unload(false)` releases it.
- **Twice:** load, unload(0), load, unload(0) leaves `used()` where it started (the leak above fails this today).
- **ResetVisibility:** with marks set in both worlds and the latch set, all marks and the latch are clear afterwards.
- **Load priority:** `setLoadPriority(10.0)` then reading gives 10.0; `-3.0` gives -3.0 and `1e9` gives 1e9 (no
  clamp); `loadLevel` sets 0.001.
- **Pack queue:** three `queuePack` calls with the same name give three entries, in order.

## Unit 19: Streamed world object lifecycle and sector callbacks {#unit-19}

**What it is.** The world object's constructor and destructor, the part-loaded callback, the release of one
sector's atomic, the material alpha callback, the visibility pass's sector callback and the modulate flag. Confirmed
(code) at the cited addresses unless a line says otherwise.

- **Coney:** `src/world/streamed_world.*` (the visibility callback's order and its quirk), `src/platform/world_set.*`
  (the release of an atomic, the modulate flag, tags), `src/platform/world_renderer.*` (the alpha callback).
- **Tests:** [Test ideas](#u19-tests): a marked sector of the `d` world sets the `s` world's mark; an occluded sector is
  neither marked nor collected nor counted; a sector without an index is neither collected, marked nor counted; the
  alpha callback changes only the alpha byte of every material; a sector whose atomic is in use stays pending.

7 functions, 1684 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00410308` | `World_Construct` | 588 |
| `0x00410558` | `World_Destroy` | 236 |
| `0x004114d8` | `World_PartLoaded` | 248 |
| `0x004118b8` | `WorldSector_ReleaseAtomic` | 156 |
| `0x00411958` | `Material_SetAlphaCallback` | 56 |
| `0x00411b98` | `World_VisibilitySectorCallback` | 376 |
| `0x004102f0` | `Atomic_SetModulateMaterialColour` | 24 |

Records: the world object ([The world object](world.md#world-object)) and the sector plugin (offset in `0x0050ced0`;
`+0x00` atomic, `+0x04` streamed index or -1, `+0x08` state, [Sector plugin](world.md#sector-plugin)).

### World_Construct {#u19-construct}

`0x00410308`, `World* World_Construct(World* world)`; no vtable. Writes, in this order:

1. The visible bits `+0x08f8`: 18 words = 0.
2. The 140 part-sector vectors at `+0x0940` (16 bytes each: `+0x04` begin, `+0x08` end, `+0x0c` capacity end; `+0x00`
   unused): all three pointers = 0, then each **reserves 8 entries**: a 0x20-byte buffer from the STL pool
   (`0x006eb9c0`, the `Level Dynamic & LUA Pool`'s STL copy; align 16), begin = end = buffer, capacity end = buffer +
   0x20. So a world takes 140 × 32 = 4,480 bytes of the Lua pool besides itself.
3. `+0x10 = 0`, `+0x04 = -1`, `+0x08 = 0`, `+0x2158` to `+0x2168` (dictionary, `RpWorld`, has-PVS, part count,
   streamed-sector count) = 0, `+0x00 = -1`, `+0x0c = 0`, `+0x14 = 0`, `+0x2170 = 0`, `+0x216c = 0`, `+0x2150 = 0`.
4. The 140 part records at `+0x1200` (0x1c bytes each): all 7 words = 0.

**Not written:** `+0x18`-`+0x37` (cleared by the first viewport's pass), the sector table `+0x38`-`+0x8f7`, the
world's clump `+0x2154` and its name `+0x2174`. The memory is the Lua heap's, not cleared. `World_FindPartToLoad`
(`0x00411eb0`) reads every table entry from 0 to the streamed-sector count − 1 without a null test, so every index
must be filled by `World_RegisterSector` (inferred from the disc check that indices run 0 to k − 1 without gaps,
[Sector plugin](world.md#sector-plugin)).

### World_Destroy {#u19-destroy}

`0x00410558`, `void World_Destroy(World* world, u32 flags)`, called through `0x004f57d8` by `WorldManager_Unload`
after `World_Unload`:

1. For each of the 140 vectors, first to last: end = begin (the entries are plain pointers; nothing is destroyed).
2. For each vector **from the last to the first**: if begin is non-null, free it to the STL pool.
3. `operator delete(world)` only when `flags & 1`.

It releases nothing else: the parts, the `RpWorld`, the dictionary and the clump are `World_Unload`'s
([Choosing what to stream](world.md#streaming)).

### World_PartLoaded {#u19-part-loaded}

`0x004114d8`, the file manager's completion callback for a part read, `(request, World* world)`; the first argument
is not read.

1. Push the `Level Dynamic & LUA Pool`.
2. Wrap the file manager's buffer (`+0x3c`) as an in-memory file whose size and capacity are both the part record's
   file size: record `world+0x1200 + 0x1c × p`, `+0x10`, with `p = world+0x00` (the part being read).
3. A RenderWare custom stream over it (`0x0047f1f8(4, 1, …)`: custom type, read access; RenderWare's constants,
   inferred).
4. `World_ReadPart(world, stream)` (`0x004110c0`, [Part file](world.md#part-file)), then close the stream.
5. Pop. `world+0x00 = -1` (no part being read). Destroy the in-memory file.

No step checks the read's status or `World_ReadPart`'s result.

### WorldSector_ReleaseAtomic {#u19-release}

`0x004118b8`, `RpWorldSector* WorldSector_ReleaseAtomic(RpWorldSector* sector, void* unused)`, called for each sector
of a part by `World_UnloadPart` (`0x004115d0`, with a null second argument):

1. Plugin state (`+0x08`) = 3.
2. `atomic` = plugin `+0x00`. If it is null, or `Atomic_IsInUse(atomic)` (`0x00198220`) is true: return `sector`,
   state left at 3.
3. Otherwise: `frame` = `atomic+0x04`; detach the frame (`0x0046a0c0(atomic, 0)`); destroy the frame
   (`0x00484240`); destroy the atomic (`0x0046a3f0`); plugin atomic = null; state = 4. Return `sector`.

`World_UnloadPart` goes through the part's sector vector and **stops at the first sector not at 4**; the part is
finished (dictionary destroyed, `world+0x04 = -1`, clump checked, unregistered and destroyed) only when every sector
reached 4, else the unload is tried again on a later frame. So a sector with **no atomic** in a part being unloaded
would hold its part forever (inferred: a loaded part's sectors all have atomics, [Part file](world.md#part-file)).
What `Atomic_IsInUse` tests (the atomic's MatFX or particle-tank users) is not read further here.

### Material_SetAlphaCallback {#u19-alpha}

`0x00411958`, `RpMaterial* Material_SetAlphaCallback(RpMaterial* material, const u8* alpha)`: copies the material
colour (`+0x04`, four bytes RGBA, read and written unaligned), replaces byte 3 with `*alpha`, writes it back and
returns `material` (non-null, so a `ForAllMaterials` walk goes on to the next). R, G and B are unchanged.
`World_RenderSectorAtomic` runs it over every material of a sector's atomic during the fade-in ([A
frame](world.md#a-frame)).

### Atomic_SetModulateMaterialColour {#u19-modulate}

`0x004102f0`, `RpAtomic* Atomic_SetModulateMaterialColour(RpAtomic* atomic)`: geometry = `atomic+0x18`; geometry
flags (`+0x08`) `|= 0x40` (modulate by material colour); returns `atomic`. No null test of the geometry. Applied to
every atomic of a part as it is read ([Part file](world.md#part-file), step 4), which is what lets the alpha above
fade it.

### World_VisibilitySectorCallback {#u19-visibility}

`0x00411b98`, `RpWorldSector* World_VisibilitySectorCallback(RpWorldSector* sector)`, installed as the world's sector
render callback by `World_FindVisibleSectors` (`0x00411d10`) for the length of one `RpWorldRender`; the world's own
callback is saved in `0x005147e4` and the world being walked in `0x005147ec`.

1. **PVS:** `visible` = 1. When the walked world has PVS data (`+0x2160`) and PVS is on (`0x005e539c`), `visible` =
   `0x004bef30(sector)` (RenderWare's PVS test, inferred from its place).
2. **Unindexed sectors:** if the plugin's streamed index (`+0x04`) is -1 (the value at `0x00553790`), skip to step 6.
3. **Occluders:** build a box from the sector's bounding box, `(+0x6c, +0x70, +0x74, 1)` then `(+0x60, +0x64,
   +0x68, 1)` (RenderWare's `inf` then `sup`, inferred from the `RwBBox` layout); if
   `Occluders_HidesBox(level+0x28, box)` (`0x0017a738`, the level from world manager `+0x40`) returns 1, **return
   null** at once.
4. **Count:** `0x005147f4 += 1`, before and whatever the PVS answer.
5. **Keep:** if `visible`: call the saved callback (`World_CollectSector` `0x00411b20`, which queues the sector when
   its atomic is loaded), then set bit `index` in the visible bits of **`GetSolidWorld(wm)`**, the `s` world
   (`+0x08f8 + 4 × (index >> 5)`, bit `index & 31`), **whichever world is being walked**.
6. Return `sector` when `visible`, else null.

**Consequences** (confirmed (code) for each mechanism, the effects inferred): the `d` world's own visible bits are only
ever cleared (by `World_ResetVisibility` on the first viewport), so its `World_FindPartToLoad` never finds a visible
candidate and always falls back to all its sectors (its `+0x10` is always set after a search), and its
`World_FindPartToUnload` treats every loaded part as unseen. The `s` world's bits gain the `d` world's indices, so an
`s` sector can count as visible because the `d` sector with the same index is. The counter `0x005147f4` is reset by
`World_ResetVisibility` (`0x004123e8`, the store at `0x004124e8`) and read once by `World_FindVisibleSectors`
(`0x00411e20`): when it is still 0 after a PVS walk, the walk is repeated with PVS off. Since PVS-hidden sectors count,
the repeat happens only when no indexed sector survived the occluders (or none was walked), not when the PVS hid them
all.

Whether RenderWare's walk stops or goes on after a null return is RenderWare's, not read here; the original returns
null for both an occluded and a PVS-hidden sector.

### What Coney changes {#u19-coney}

- **The visibility quirk.** `StreamedWorld::markVisible` marks the world's own sector. To match the original's
  streaming order, the pass over the `d` world must mark the **`s` world's** bit with the `d` sector's index, and the
  `d` world's marks stay clear: an "Original quirk, kept" in `findVisibleSectors` (a pass needs the `s` world as the
  mark target), with a test. Single-world levels are unaffected.
- **Order inside the callback.** When Coney adds occluders, the test comes after the index check and before the
  count; when it adds PVS, the PVS answer only decides marking and collecting. Until then the frustum test stands for
  both (already marked in `streamed_world.h`).
- **Unindexed sectors** are not collected by the original (the saved callback runs only in the indexed branch,
  `0x00411c9c`; the sector is only returned to RenderWare when visible); Coney's `collectSectors` skips them too.
- **Releasing an atomic.** `WorldSet::unloadPart` frees a part at once. The original keeps a sector at state 3 while
  its atomic is in use and retries the part later; Coney has no in-use atomics yet (a stand-in, to mark), and a
  pending state should be added with whatever first holds an atomic. `@orig 0x004118b8 WorldSector_ReleaseAtomic
  (WorldPS2.cpp)` on the per-atomic release in `unloadPart`.
- **Part loaded.** `WorldSet::loadPart` reads synchronously and stands for both the request's completion and
  `World_PartLoaded`; tag it `@orig 0x004114d8 World_PartLoaded (WorldPS2.cpp)` beside `World_ReadPart`.
- **Construct and destroy.** `StreamedWorld::create` and `~WorldSet` stand for them; Coney sizes from the data, so the
  140 reserved vectors have no counterpart. Tags: `@orig 0x00410308 World::World`, `@orig 0x00410558
  World_Destroy` (both `WorldPS2.cpp`).
- **Alpha and modulate.** `loadPart` already sets geometry flag `0x40` (tag `@orig 0x004102f0
  Atomic_SetModulateMaterialColour (WorldPS2.cpp)`); the renderer's fade sets the alpha of every material and must
  leave R, G and B alone (tag `@orig 0x00411958 Material_SetAlphaCallback (WorldPS2.cpp)`).

### Test ideas {#u19-tests}

On a synthetic two-world layout (no renderer needed for the first four):

- **The `s` world takes the marks:** walking the `d` world with its sector of index 5 in view sets index 5 in the `s`
  world's marks and leaves the `d` world's clear; `findSectorToLoad` on the `d` world then reports `searchFellBack()`.
- **Occluded:** a sector of index 3 whose box an occluder hides is not marked, not collected, and does not raise the
  counter; one not hidden but PVS-hidden raises the counter and is neither marked nor collected.
- **Unindexed:** a sector with index -1 in view is not marked and not counted.
- **Repeat without PVS:** when every indexed sector in view is occluded, the pass runs again with PVS off (counter
  still 0); when all are only PVS-hidden, it does not.
- **Alpha:** materials with colours (10, 20, 30, 255) and (200, 100, 50, 7), alpha 128: afterwards (10, 20, 30, 128)
  and (200, 100, 50, 128).
- **Release:** a part of three sectors whose second atomic is in use: the first is released (state 4), the second
  stays at 3, the third is not visited, the part is not finished; once the use ends, a retry finishes it.
