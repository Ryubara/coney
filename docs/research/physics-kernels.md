# Physics units: geometry kernels

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-06, the whole of `Physics/` on 2026-10-07); the game under PCSX2 2.9.94 (PINE, hooks and `pcsx2 record`, in
`level99`, 2026-10-07). The exact specifications of
[Reimplementing Physics, RayCast and World](physics.md#units) were read in the disassembly on 2026-10-12.

Part of [Reimplementing Physics, RayCast and World](physics.md#units): the exact specifications of the geometry kernels:
a sphere swept against a triangle, the static shape-pair overlaps, a sphere, a capsule and a box swept against boxes and
triangles. The plan, the order of the units and their dependencies are on that page; the subsystem itself is
[Physics](physics.md).

## Unit 1: Triangle sphere-sweep kernels {#unit-1}

**What it is.** A moving sphere against one triangle: the face, then an edge (as a capsule) or a corner's region;
the point-to-triangle distance; and the working triangle every mesh test fills. Answers issue #150's three questions
([Issue #150](#issue-150)).

- **Coney:** one shared kernel for every sphere against the level. Coney's walking body cites parts of it
  (`src/human/body.cpp`); a branch not yet merged when this was written adds a sphere sweep for the cameras
  (`raycast::sweepSphere`) that follows the same reading. Both should end on this one.
- **Tests:** [Test ideas](#u1-tests), including the late and missed corner contacts the original produces.

13 functions, 8116 bytes (3 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00343a90` | `CollTri_ComputeEdges` | 140 |
| `0x00343b20` | `CollTri_ComputeEdgeNormals` | 272 |
| `0x00343c30` | `CollTri_ComputeNormal` | 92 |
| `0x00343c90` | `CollTri_Flip` | 72 |
| `0x0034ee60` | `Sweep_SphereTriangle` (tagged) | 2144 |
| `0x0034ee40` | `Sweep_SphereTriangleThunk` | 32 |
| `0x0034e3b8` | `SphereSweep_Edge` (tagged) | 536 |
| `0x0034e5d0` | `SphereSweep_VertexRegion` (tagged) | 612 |
| `0x0034dff8` | `SphereSweep_EdgeCylinder` | 296 |
| `0x0034d938` | `Ray_InfiniteCylinder` | 1728 |
| `0x0034e120` | `Segment_SphereIntersect` | 664 |
| `0x0034e840` | `PointTriangle_DistanceSq` | 1420 |
| `0x0034edd0` | `SphereTriangle_Penetration` | 108 |

Conventions: `a·b` is a dot product over x, y, z, summed as `(ax·bx + ay·by) + az·bz` (the VU0 `vmul` +
`vaddy` + `vaddz` order). `a × b` is a cross product. Vectors are 16-byte `float[4]`, and `w` is noted only where
code sets it. "Sphere" arguments point to `{centre: vec4 at +0x00, radius: float at +0x10}`. Arguments follow the
EE ABI: `a0`-`a3`, then `t0`-`t3` for the fifth to eighth integer or pointer arguments, and `f12`... for floats.

**Floating point on the EE** (inferred: a hardware fact, not read in these functions). The EE FPU and VU0 have no
infinities, NaNs or denormals. A division by zero gives ±`FLT_MAX` (the sign is the XOR of the operands' signs),
and an overflow clamps to ±`FLT_MAX`. Rounding is toward zero, not to nearest. So an IEEE implementation matches to
about 1 ulp, not bit for bit: compare fractions with a tolerance (1e-5 is ample at these magnitudes). Where an edge
case depends on division by zero, its section says what the EE does.

### The working triangle {#u1-working-triangle}

The kernels read a working triangle filled by the mesh function under test. [Unit
2](physics-bodies.md#u2-the-working-triangle-t-d0-bytes-on-the-c) gives the caller's whole stack
record (0xd0 bytes, with a normal copy at `+0x90` and the result record at `+0xc0` in the capsule sweep; the sphere
sweep keeps its result record right after `+0x90`). What the kernels use, confirmed (code) at the cited addresses:

| Offset | Content | Written by |
| --- | --- | --- |
| `+0x00`, `+0x10`, `+0x20` | corners `v0`, `v1`, `v2` (whole 16-byte vertices) | the caller (`0x00348c8c`-`0x00348cc4` in the sphere sweep) |
| `+0x30` | `e0 = v1 − v0`, `w` = 1.0 | `CollTri_ComputeEdges` `0x00343a90` |
| `+0x40` | `e1 = v2 − v1`, `w` = 1.0 | `0x00343a90` |
| `+0x50` | `e2 = v0 − v2`, `w` = 1.0 | `0x00343a90` |
| `+0x60`, `+0x64`, `+0x68` | `len(e0)`, `len(e1)`, `len(e2)`, only when the edges were normalised | `0x00343b20` |
| `+0x6c` | unused by these functions | |
| `+0x70` | unit normal `n`, `w` = 1.0 | `CollTri_ComputeNormal` `0x00343c30` |
| `+0x80` | plane constant `d = v0·n` (scalar, `x` lane) | the caller after the edges (`0x00348e24`-`0x00348e64`) |

So edge `k` runs from corner `k` to corner `k + 1` (mod 3). Its **outward in-plane normal** is `m_k = e_k × n`,
which is not normalised (for counter-clockwise corners seen from the front, `m_k` points away from the triangle).
Confirmed (code) at `0x0034eecc`-`0x0034ef1c`.

### CollTri_ComputeEdges {#u1-compute-edges}

`0x00343a90`, 0x8c bytes. `a0` = working triangle, `a1` = normalise flag. It writes `e0`, `e1`, `e2` as in the
table above, each with `w` = 1.0. When `a1` is non-zero it then calls `0x00343b20`. Both mesh sweeps that use the
sphere kernel pass `a1` = 0 (sphere `0x00348e1c`, capsule `0x003480f0`), so **the kernels see unnormalised edges**:
the edge step sweeps the real edge, from `v_k` to `v_k + e_k`. Confirmed (code) at `0x00343a90`.

### CollTri_ComputeEdgeNormals {#u1-edge-normals}

`0x00343b20`, 0x110 bytes. `a0` = working triangle. **The name is wrong**: it divides each edge by its length
(VU0 `vsqrt` of `e·e`, then FPU `1/len` and a VU0 scale of x, y, z, leaving `w`), writes the unit edges back to
`+0x30`/`+0x40`/`+0x50` and the lengths to `+0x60`/`+0x64`/`+0x68`. A zero-length edge is not guarded against (on
the EE, `1/0` gives `FLT_MAX` and `0 × FLT_MAX` = 0, so the edge stays zero). It is not used on the sphere path.
Confirmed (code) at `0x00343b20`. The Ghidra name is kept, and its plate comment now says what the function does.

### CollTri_ComputeNormal {#u1-compute-normal}

`0x00343c30`, 0x5c bytes. `a0` = working triangle (a second argument some callers pass is ignored).
`n = c / sqrt(c·c)` with `c = (v1 − v0) × (v2 − v0)`, computed as VU0 `vrsqrt` then a scale. `w` is set to 1.0,
and the result goes to `+0x70`. Confirmed (code) at `0x00343c30`. For a degenerate triangle (`c` = 0), the EE's
`rsqrt(0)` gives `FLT_MAX` and `n` comes out as (0, 0, 0) (inferred, EE semantics). An IEEE build would get NaN, so
an implementation should make `n` = 0 explicitly. The mesh sweeps then skip such a triangle at the side test
(`n·c − n·v0` = 0 < 1e-6, then `n·move` = 0 ≥ −0.001), so it never reaches the kernel.

### CollTri_Flip {#u1-flip}

`0x00343c90`, 0x48 bytes. `a0` = working triangle. It swaps corners `+0x10` and `+0x20` (whole 16 bytes) and
multiplies the normal's x, y, z by −1.0 (`0xbf800000`). **`w` is kept** (the VU op is `.xyz`). It does **not** recompute
the edges or `d`: the callers flip
before `CollTri_ComputeEdges` and before setting `+0x80` (sphere sweep: flip `0x00348d7c`, edges `0x00348e18`, `d`
`0x00348e64`). Confirmed (code) at `0x00343c90`.

### Sweep_SphereTriangle {#u1-sweep-sphere-triangle}

`0x0034ee60`, 0x860 bytes. The thunk `Sweep_SphereTriangleThunk` (`0x0034ee40`, 0x20 bytes) calls it with `t0`
pointing at a zeroed scratch word and drops that word. Its only caller is the sphere mesh sweep `0x00348a50`. The
capsule mesh sweep `0x00347c08` calls `0x0034ee60` directly and keeps the word. Confirmed (code).

**Arguments.** `a0` = result record. `a1` = sphere (centre `c`, radius `r`), the centre at the **start** of the
move. `a2` = working triangle (with `+0x30`-`+0x80` filled). `a3` = move `M` (a vec4, the whole displacement, not a
direction). `t0` = `int*` for the **inside mask**. **Return** `v0`: 1 when it recorded a fraction, 0 otherwise.

**Result record.** `+0x00` is an `int` count (the caller zeroes it). On a hit the kernel writes the fraction to
`+0x04 + 4·count`, the vector (0, 0, 0, 1) (`0x005116c0`) to `+0x10 + 16·count`, and adds 1 to the count. So
**only a fraction comes back**: no normal, point or depth. The callers read `+0x04` only and make one call per
record. Confirmed (code) at `0x0034f688`-`0x0034f6a8`.

**The fraction is not range-checked here.** The kernel can return 1 with a fraction above 1 or below 0, which only
the edge steps produce ([SphereSweep_Edge](#u1-edge)). Both callers drop a fraction outside [0, 1] (sphere sweep
`0x00348e78`/`0x00348e94`, capsule sweep `0x0034814c`/`0x00348168`). Confirmed (code).

**Algorithm**, confirmed (code) at `0x0034ee60`:

1. `nn = n·n` (1, or 0 for a degenerate triangle) and `dist = n·c − d`.
2. **Already reaching the plane** if `dist·dist ≤ (nn·r)·r` (inclusive). Then the test point is `P = c` and the
   base fraction is 0.
3. Otherwise **swept to the plane**:
    - Centre in front (`d < n·c`, strict): no hit (return 0) unless `n·M < 0`. The sphere's leading point is
      `P0 = c + (−r)·n`.
    - Centre behind (else): no hit unless `n·M > 0`, and `P0 = c + r·n`. (The mesh callers never pass a sphere
      behind a face: they flip two-sided faces and skip one-sided ones. The kernel itself handles both sides.)
    - `t = (d − n·P0) / (n·M)`. If `t > 1`, no hit (return 0). There is no `t < 0` test: with the sphere off the
      plane, `t` is positive.
    - The test point is `P = P0 + t·M` (where the sphere first touches the plane).
4. **The inside mask**: bit `k` (1, 2, 4 for edges 0, 1, 2) is set when `m_k·P ≤ m_k·v_k` (inside or on edge `k`).
   The mask is written to `*t0` in both branches, before the dispatch (`0x0034efd4`, `0x0034f43c`). It is not
   written on the early returns of step 3. The capsule sweep stores it in its contact at `+0x14` (`0x003481d8`).
5. **Dispatch on the mask** (jump tables `0x00577450`, already reaching, and `0x00577470`, swept). The two tables
   differ only in case 7:

    | Mask | Centre is | Step | Arguments |
    | --- | --- | --- | --- |
    | 7 | inside all edges | **hit**: fraction 0 (already reaching) or `t` (swept) | none |
    | 6 | outside edge 0 only | [SphereSweep_Edge](#u1-edge) | `A = v0`, `E = e0` |
    | 5 | outside edge 1 only | SphereSweep_Edge | `A = v1`, `E = e1` |
    | 3 | outside edge 2 only | SphereSweep_Edge | `A = v2`, `E = e2` |
    | 2 | outside edges 0 and 2 (past `v0`) | [SphereSweep_VertexRegion](#u1-vertex-region) | `k = 0`, `m0`, `e0` |
    | 4 | outside edges 0 and 1 (past `v1`) | SphereSweep_VertexRegion | `k = 1`, `m1`, `e1` |
    | 1 | outside edges 1 and 2 (past `v2`) | SphereSweep_VertexRegion | `k = 2`, `m2`, `e2` |
    | 0 | outside all (only a degenerate triangle) | none: return 0 | |

   The edge and corner steps get the **original** sphere (centre `c`, radius `r`) and the **whole** move `M`, not
   `P` or `t`. Their fraction is that of the whole move, and it is what the kernel records. Confirmed (code) at
   `0x0034eff4`-`0x0034f220` and `0x0034f45c`-`0x0034f684`.

Edge cases, confirmed (code) unless marked:

- **Zero move.** If the sphere is not already reaching the plane, `n·M` = 0 and there is no hit. If it is already
  reaching, case 7 still gives fraction 0, and so does an edge whose segment already meets the ball. The capsule
  step returns nothing for a zero move ([Ray_InfiniteCylinder](#u1-capsule)).
- **Already overlapping and moving away.** Case 7 gives fraction 0 whatever the direction of `M`, and so does an
  edge within `r`. Only the corner step looks at the direction. The mesh callers have already dropped moves that do
  not go into the face ([Unit 2](physics-bodies.md#u2-00347c08-physicsmesh-sweepcapsule)).
- **Exactly on the plane** (`dist` = 0): already reaching.
- **On an edge line** (`m_k·P = m_k·v_k`): counts as inside that edge.
- **Degenerate triangle** (`n` = 0): `0 ≤ 0`, so it is "already reaching" and all `m_k` = 0, which gives mask 7 and
  a hit at fraction 0 for any sphere. The callers' side test keeps such triangles out (above), and an
  implementation of the kernel alone should return 0 for `n` = 0 (inferred: unreachable in the original).

### SphereSweep_Edge {#u1-edge}

`0x0034e3b8`, 0x218 bytes. **Arguments**: `a0` = edge start `A`, `a1` = edge vector `E` (`A + E` is the other
corner), `a2` = sphere (centre `c`), `a3` = move `M`, `f12` = `r`, `t0` = `float*` fraction out, `t1` = `int*` count
out, `t2` = `vec4*` point(s) out (room for two). **Return** 1 for a contact, 0 for none. Confirmed (code) at
`0x0034e3b8`.

1. If the segment already meets the ball ([Segment_SphereIntersect](#u1-segment-sphere)`(A, E, c, r)` returns 1),
   then `*fraction = 0` and the function returns 1. The count and points are what that call wrote.
2. Otherwise run the capsule step ([SphereSweep_EdgeCylinder](#u1-edge-cylinder)`(r, c, M, A, E)`, with its roots
   in a local `roots[2]`). If it returns 0, return 0 and leave `*fraction` unwritten.
3. Otherwise `*fraction = roots[0]`: **the first root in the capsule's list, not the smallest, and not necessarily
   in [0, 1]** (see the capsule's root order). The contact point is the centre at that fraction, `Pc = c + roots[0]·M`,
   clamped to the segment by projection. If `E·Pc < E·A` it is `A`. Else if `E·(A+E) < E·Pc` it is `A + E` (the
   code forms `E·(A+E)` as `E·A + E·E`). Else it is `A + u·E` with `u = (E·Pc − E·A) / (E·(A+E) − E·A)`. The point
   goes to `*t2`, and `*t1` = 1. `Sweep_SphereTriangle` ignores the point and the count.

### SphereSweep_VertexRegion {#u1-vertex-region}

`0x0034e5d0`, 0x264 bytes. It handles a centre past a corner (the corner's region) and gives **no** contact when
the corner is near, so the Ghidra name is `SphereSweep_VertexRegion` (a rename to `SphereSweep_EdgeIfNear` was
reverted, issue #383). **Arguments**: `a0` = corner index `k` (0-2), `a1` =
working triangle (corners read at `+0x10·i`), `a2` = `&n` (unused), `a3` = `&m_k`, `t0` = `&e_k`, `t1` = sphere,
`t2` = move `M`, `t3` = `float*` fraction out, `f12` = `r`, `f13` = 1.0 (unused). The caller's stack words `+0` and
`+8` are the count and point outputs, handed on to the edge step. **Return** 1 or 0. Confirmed (code) at
`0x0034e5d0`.

1. `V = v_k`. If `|V − c|² < r²` (strict): `*fraction = 0` but **return 0**, so there is no contact when the corner
   is already inside the ball.
2. If `M·(V − c) ≤ 0` (moving away from or across the corner), return 0.
3. **Which edge.** `u = (m_k·V − m_k·c) / (m_k·M)`, the fraction at which the centre's path crosses edge `k`'s side
   plane (the plane through edge `k` that holds `n`). `X = c + u·M`. If `e_k·V ≤ e_k·X` (the crossing lies along
   edge `k`, past the corner), test **edge `k`**: `A = v_k`, `E = v_(k+1 mod 3) − v_k`. Otherwise test **edge
   `k − 1`**: `A = v_(k−1 mod 3)`, `E = v_k − A`. These are built from the corners, not read from `+0x30`.
4. Return [SphereSweep_Edge](#u1-edge)`(A, E, sphere, M, r, fraction, count, point)`. **Only one** of the corner's
   two edges is ever tested, and the corner itself is that edge's end cap.

**Edge case**, `m_k·M = 0` (the move is parallel to edge `k`'s side plane): on the EE, `u` is ±`FLT_MAX` and `X`
clamps, which decides the comparison by signs (inferred, EE semantics; not reached in any test here). For a
faithful IEEE version, use edge `k` when the numerator `m_k·V − m_k·c` and `e_k·M` have the same strict sign. If
`e_k·M` = 0, compare `e_k·V ≤ e_k·c`. If the numerator is 0, use edge `k − 1` (the EE's 0/0 gives a clamped value
and the test then fails on `0·FLT_MAX` sums, which is inferred). The move then lies in edge `k`'s side plane or
parallel to it, so either choice is rare in play.

### SphereSweep_EdgeCylinder {#u1-edge-cylinder}

`0x0034dff8`, 0x128 bytes. **Arguments**: `f12` = `r`, `a0` = centre `c`, `a1` = `M`, `a2` = `A`, `a3` = `E`,
`t0` = `int*` count out, `t1` = `vec4*` points out, `t2` = `float roots[2]` out. Confirmed (code) at `0x0034dff8`:

1. `n = Ray_InfiniteCylinder(r, c, M, A, E, roots)`, which writes `roots[0..n−1]`.
2. Count the roots with `0 ≤ roots[i] ≤ 1` (both inclusive). For each, in list order, write the centre at that
   time, `c + roots[i]·M` (`w` = 1), to the next `points[]` slot.
3. `*count` = that number, and the function returns 1 when it is above 0. **`roots[]` is not compacted or sorted**.
   So the edge step's `roots[0]` can be a root outside [0, 1] while `roots[1]` is inside, and the step then reports
   the outside root (which the mesh callers drop).

### Ray_InfiniteCylinder (a capsule) {#u1-capsule}

`0x0034d938`, 0x6c0 bytes. **The name is wrong: it is the move against a capsule** (radius `r`, axis from `A` to
`A + E`, rounded ends), as the plate comment from another analyst already says. **Arguments**: `f12` = `r`, `a0` =
centre `c`, `a1` = `M`, `a2` = `A`, `a3` = `E`, `t0` = `float roots[2]` out. **Return** the root count, 0-2, with
each root a fraction of `M`. Confirmed (code) at `0x0034d938`.

**Frame.** `L = |E|`, `W = E / L` (VU `vsqrt`, then `1/L`). `U, V` = `Vec_MakePerpendicularBasis(W)`
(`0x00349180`): if `|Wy| ≤ |Wx|`, `U = (−Wz, 0, Wx) / sqrt(Wx² + Wz²)`; else `U = (0, Wz, −Wy) / sqrt(Wy² + Wz²)`.
`V = W × U`. Only `Px² + Py²` and `Dx² + Dy²` are used, so any orthonormal `U, V` gives the same roots up to
rounding. Then `Δ = c − A` and `P = (U·Δ, V·Δ, W·Δ)`. With `m = |M|` (computed as the length of
`(U·M, V·M, W·M)`), `D = (U·M, V·M, W·M) / m`, which is unit, and `k = 1/m`. All roots below are in units of
distance along the unit `D` and are multiplied by `k` to become fractions of `M`.

**Parallel or zero move** (`|Dz| ≥ 0.99999899`, `0x3f7fffef`, or else `m < 1e-6`, `0x358637bd`):
`ρ = (r² − Px²) − Py²` and `s = sqrt(ρ)`.

- If `M·E < 0` and `ρ ≥ 0`: `roots = [(Pz + s)·k, −((L − Pz) + s)·k]`, return 2. Moving toward `A`, this is
  **[exit, entry]**.
- If `M·E > 0` and `ρ ≥ 0`: `roots = [−(Pz + s)·k, ((L − Pz) + s)·k]`, return 2 ([entry, exit]).
- Otherwise (`ρ < 0`, or `M·E` = 0, which includes a zero move) return 0.

So a parallel move meets only the corner spheres (their entry and exit along the axis), and in the `M·E < 0` order
the edge step reports the **exit**.

**General case**, in this order, appending each kept root and **returning as soon as two are kept**:

1. **Side wall.** `a0 = (Px² + Py²) − r²`, `a1 = Px·Dx + Py·Dy`, `a2 = Dx² + Dy²`, `Δw = a1² − a2·a0`.
    - `Δw < 0`: return 0, **with no cap test** (a line that misses the infinite cylinder misses the capsule too).
    - `Δw > 0`: `q = sqrt(Δw)`, then `τ1 = (−a1 − q)·(1/a2)` and `τ2 = (−a1 + q)·(1/a2)`. Keep each, in that order,
      whose axial position `Pz + τ·Dz` is in **[0, L]** (inclusive). If both are kept, return 2.
    - `Δw = 0`: `τ = −a1 / a2`. If `0 ≤ Pz + τ·Dz ≤ L`, keep it and **return 1** (no cap test).
2. **Sphere at `A`.** `b0 = a0 + Pz²` (`= |P|² − r²`), `b1 = a1 + Pz·Dz` (`= P·D`), `Δa = b1² − b0`. If `Δa > 0`,
   try `τ = −b1 − sqrt(Δa)`, then `−b1 + sqrt(Δa)`. If `Δa = 0`, try `τ = −b1` alone. Keep each with axial
   position `≤ 0`.
3. **Sphere at `A + E`.** `c1 = b1 − Dz·L`, `c0 = b0 + L·(L − 2·Pz)`, `Δb = c1² − c0`. Same pattern, keeping each
   with axial position `≥ L`.
4. Return the number kept.

**Consequence: the list is ordered wall, then `A` cap, then `B` cap, not by time.** A move that enters through a
cap and leaves through the wall lists the wall's exit first. The same holds for a move nearly along the axis toward
`A`: the `A` cap's exit comes before the `B` cap's entry. Since [SphereSweep_Edge](#u1-edge) takes `roots[0]`, such
a sphere is reported **late** (at the exit), or **not at all** when the exit is past the move (the caller drops a
fraction above 1). This mostly happens at a triangle's corners, because the corner step's edge always has the corner
as one of its caps. Confirmed (code) at `0x0034dd04`-`0x0034dfc8` and `0x0034e450`. Worked numbers are in
[Test ideas](#u1-tests).

Edge cases: a zero-length edge gives `L` = 0 and `1/L` = `FLT_MAX` on the EE, so `W` is 0, `Dz` = 0, and the
parallel test fails unless `m < 1e-6`. Then `a2` = 1 and the wall roots are kept only when `Pz + τ·Dz` (= 0) is in
[0, 0], that is always. So a zero-length edge behaves like a sphere at `A` (inferred from EE semantics; the mesh
skips degenerate triangles before this). `Δw > 0` strictly and `Δw = 0` exactly are separate branches. There is no
tolerance band.

### Segment_SphereIntersect {#u1-segment-sphere}

`0x0034e120`, 0x298 bytes. **Arguments**: `a0` = `A`, `a1` = `E`, `a2` = ball centre `C`, `f12` = `r`, `a3` =
`int*` count out, `t0` = `vec4 points[2]` out. **Return** 1 when the segment `A + u·E`, `u ∈ [0, 1]`, meets the
closed ball, else 0. Confirmed (code) at `0x0034e120`.

`D = A − C`, `c = D·D − r²`, `b = D·E`, `a = E·E`, `Δ = b² − a·c`:

- `Δ < 0`: count 0, return 0.
- `Δ > 0`: `s = sqrt(Δ)`, `u1 = (−b − s)·(1/a)`, `u2 = (−b + s)·(1/a)`. If `u1 > 1` or `u2 < 0`: count 0, return 0.
  If `u1 ≥ 0` and `u2 ≤ 1`: count 2, with points `A + u1·E` and `A + u2·E`. If `u1 ≥ 0` and `u2 > 1`: count 1, with
  point `A + u1·E`. If `u1 < 0`: count 1, with point `A + u2·E`, **even when `u2 > 1`** (the segment lies inside the
  ball, and the point is then past the segment). Return 1 in all three.
- `Δ = 0`: `u = −b / a`. If `0 ≤ u ≤ 1`: count 1, point `A + u·E`, return 1. Else count 0, return 0.

A zero-length edge (`a = b = 0`) takes the `Δ = 0` branch with `−0/0`, which is −`FLT_MAX` on the EE, and returns
0 (inferred: EE semantics).

### Ray_SphereRoot {#u1-ray-sphere-root}

`0x0034af50`, 0x88 bytes, a leaf on FPU registers only. **Arguments**: `f12`-`f14` = `p` (the ray origin minus the
sphere centre), `f15`-`f17` = `d` (the ray direction, any length), `f18` = `R` (the squared radius). **Return**
`f0`. Confirmed (code) at `0x0034af50`.

`c = |p|² − R`, `b = p·d`, `a = |d|²` (each summed as `(x + y) + z`), `s = sqrt(|b² − a·c|)`, `i = 1/s`, and the
result is `(c·i) / (1 − b·i)`. This equals `c / (s − b)` = `(−b − s)/a`, the **smaller root**, in the form that does
not cancel. There is **no miss test**: a negative discriminant is used as its absolute value, so the callers must
check first. `s = 0` gives `i = FLT_MAX` on the EE. It is **not** used by the triangle kernels: its callers are the
sphere-box steps `0x0034b040`, `0x0034b3b8` and `0x0034b7b0` (unit for the box sweep).

### Ray_CircleRoot {#u1-ray-circle-root}

`0x0034afd8`, 0x64 bytes, the 2D form. **Arguments**: `f12`, `f13` = `p`; `f14`, `f15` = `d`; `f16` = `a`
(supplied by the caller, normally `|d|²`); `f17` = `R`. `c = |p|² − R`, `b = p·d` (summed `dx·px + dy·py`), and
the result is `(c·i) / (1 − b·i)` with `i = 1/sqrt(|b² − a·c|)`. No miss test. Callers: `0x0034b040`, `0x0034b3b8`.
Confirmed (code) at `0x0034afd8`.

### PointTriangle_DistanceSq {#u1-point-triangle}

`0x0034e840`, 0x58c bytes. **Arguments**: `a0` = point `P`, `a1` = triangle (corners at `+0x00`, `+0x10`,
`+0x20`; nothing else is read), `a2` = `float*` `s` out or null, `a3` = `float*` `t` out or null. **Return** `f0` =
the squared distance, passed through `abs`. The closest point is `v0 + s·E0 + t·E1`, with `E0 = v1 − v0` and
`E1 = v2 − v0`. Confirmed (code) at `0x0034e840`.

This is the public region method of D. Eberly, "Distance Between Point and Triangle in 3D" (Geometric Tools),
followed exactly. With `D = v0 − P`, `a = E0·E0`, `b = E0·E1`, `c = E1·E1`, `d = E0·D`, `e = E1·D`, `f = D·D`,
`det = |a·c − b²|`, `s = b·e − c·d` and `t = b·d − a·e`:

| Region (test order) | Rule |
| --- | --- |
| `s + t ≤ det`, `s < 0`, `t < 0` (4) | if `d < 0`: `t = 0`, then the `t = 0` edge rule; else `s = 0`, then the `s = 0` edge rule |
| `s + t ≤ det`, `s < 0` (3) | `s = 0`, the `s = 0` edge rule |
| `s + t ≤ det`, `t < 0` (5) | `t = 0`, the `t = 0` edge rule |
| `s + t ≤ det` (0, inside) | `s /= det`, `t /= det` (as `× (1/det)`), `q = s·(a·s + b·t + 2d) + t·(b·s + c·t + 2e) + f` |
| `s < 0` (2) | `t0 = b + d`, `t1 = c + e`. If `t0 < t1`: `num = t1 − t0`, `den = (a − 2b) + c`. If `num ≥ den`, `s = 1, t = 0, q = a + 2d + f`; else `s = num/den, t = 1 − s`, `q` as in region 0. Else (`t0 ≥ t1`): `s = 0`; if `t1 ≤ 0`, `t = 1, q = c + 2e + f`; else if `e ≥ 0`, `t = 0, q = f`; else `t = −e/c, q = e·t + f` |
| `t < 0` (6) | the same with the roles swapped: `t0 = b + e`, `t1 = a + d`. If `t0 < t1`: `num = t1 − t0`, `den = (a − 2b) + c`. If `num ≥ den`, `t = 1, s = 0, q = c + 2e + f`; else `t = num/den, s = 1 − t`, `q` as in region 0. Else: `t = 0`; if `t1 ≤ 0`, `s = 1, q = a + 2d + f`; else if `d ≥ 0`, `s = 0, q = f`; else `s = −d/a, q = d·s + f` |
| otherwise (1) | `num = ((c + e) − b) − d`. If `num ≤ 0`: `s = 0, t = 1, q = c + 2e + f`. Else `den = (a − 2b) + c`; if `num ≥ den`, `s = 1, t = 0, q = a + 2d + f`; else `s = num/den, t = 1 − s`, `q` as in region 0 |

The **`t = 0` edge rule**: if `d ≥ 0`, `s = 0, q = f`; else if `−d ≥ a`, `s = 1, q = a + 2d + f`; else
`s = −d/a, q = d·s + f`. The **`s = 0` edge rule** is the same with `e`, `c` and `t`. In region 0, the code adds
`2d` to `(a·s + b·t)` and forms `s·(…) + t·(…)` before adding `f`. A degenerate triangle (`det` = 0) can land in
region 0 and divide by zero (`FLT_MAX` on the EE). The callers do not guard against it (inferred).

### SphereTriangle_Penetration {#u1-penetration}

`0x0034edd0`, 0x6c bytes. **Arguments**: `a0` = sphere (centre `+0x00`, radius `+0x10`), `a1` = triangle (corners
as above), `a2` = `float*` depth out. **Return** 1 and `*depth = r − sqrt(q)` when `q < r²` (strict), where `q` =
`PointTriangle_DistanceSq(centre, triangle, null, null)`. Otherwise it returns 0 and leaves `*depth` unwritten. It
gives no normal and no point. Caller: the sphere mesh overlap `0x003473c8`. Confirmed (code) at `0x0034edd0`.

### Issue #150 {#issue-150}

Answered from the code:

1. **Which faces the sphere sweep skips** (`PhysicsMesh_SweepSphere` `0x00348a50`, confirmed (code) at
   `0x00348c30`-`0x00348e10`). Per triangle, in this order:
    - an already visited one;
    - flag `0x8` when the sweeping body has flag `0x100` or the query has `0x8000`;
    - enabled bit `0x1` clear;
    - with query option `0x4`, `n.z` below the first threshold (`f12`), and with `0x8`, `n.z` above the second
      (`f13`);
    - **side**: `n·c − n·v0 < 1e-6` (`0x358637bd`, `c` = the centre at the start, not raised) flips a two-sided face
      (flag `0x2`) and skips a one-sided one;
    - **direction**: `n·M ≥ −0.001` (`0xba83126f`, after a flip, `M` the whole move, not normalised) skips the face.

   With option `0x40` the last two skip nothing and mark the contact `0x10` (behind), `0x20` (away) or `0x40`
   (`|n·M| ≤ 0.001`, `0x3a83126f`) instead. Through the body sweep (`IPhysics_SweepShapeVsMesh` `0x0033d2d8`) the
   query asks for one contact and sets only option `0x4` (from body `+0x44` bit `0x40`), with thresholds −0.65 and
   0. So a sphere body skips faces it is behind and faces it does not move into by more than 0.001.
2. **The edge cylinder's end caps**: **capped.** `0x0034d938` is a capsule: a finite wall over the edge's length
   plus a sphere of radius `r` at each corner (and only the corner spheres for a move parallel to the edge). The cap
   roots are listed after the wall roots, and the first listed root is the one reported, which gives the late or
   missed corner contacts described under [the capsule](#u1-capsule).
3. **Which corner edge the vertex-region step tests**: **one edge**, chosen by where the centre's path crosses edge
   `k`'s side plane. If that point lies along edge `k` past the corner (`e_k·X ≥ e_k·v_k`), it tests edge `k`
   (`v_k → v_k+1`); otherwise edge `k − 1` (`v_k−1 → v_k`). There is no contact at all when the corner is already
   inside the ball or the move does not go toward it ([SphereSweep_VertexRegion](#u1-vertex-region)).

### Test ideas {#u1-tests}

These use synthetic geometry only. The numbers come from a double-precision model of this spec (not from the
game), and a tolerance of 1e-5 is enough. Triangle `T`: `v0 = (0, 0, 0)`, `v1 = (4, 0, 0)`, `v2 = (0, 4, 0)`, which
gives `n = (0, 0, 1)` and `d = 0`. Radius 0.5 throughout unless stated. "Fraction" is the value the kernel records;
mask is `*t0`.

**Working triangle.** Edges of `T`: `e0 = (4, 0, 0)`, `e1 = (−4, 4, 0)`, `e2 = (0, −4, 0)`, all with `w` 1. Normalised
(`0x00343b20`), `e1` becomes (−0.707107, 0.707107, 0) with lengths 4, 5.656854, 4. Flip: corners become `v0`,
(0, 4, 0), (4, 0, 0), `n = (0, 0, −1)` with `w` 1, and the edges are unchanged until recomputed.
`m0 = (0, −4, 0)`, `m1 = (4, 4, 0)`, `m2 = (−4, 0, 0)`.

| # | Centre `c` | Move `M` | Expected |
| --- | --- | --- | --- |
| 1 | (1, 1, 2) | (0, 0, −3) | hit, fraction **0.5**, mask 7 (face) |
| 2 | (1, 1, 0.3) | (5, 0, 0) | hit, fraction **0**, mask 7 (already reaching, any move) |
| 3 | (2, −0.3, 2) | (0, 0, −3) | hit, fraction **0.533333** (= 8/15), mask 6, edge 0, capsule roots [0.533333, 0.8] |
| 4 | (2, −0.3, 0.3) | (0, 0, −1) | hit, fraction **0**, mask 6 (the segment already meets the ball) |
| 5 | (1, 1, 2) | (0, 0, 1) | no hit (moving away), mask not written |
| 6 | (1, 1, 2) | (0, 0, −1) | no hit (`t` = 1.5 > 1), mask not written |
| 7 | (1, 1, −2) | (0, 0, 3) | hit, fraction **0.5**, mask 7 (from behind: the kernel alone accepts it; the mesh caller would flip or skip first) |
| 8 | (−1, −1, 1) | (2, 1, −1.5) | hit, fraction **0.538462** (= 7/13), mask 2: the corner `v0` step picks **edge 0** |
| 9 | (−0.2, −0.2, 0.1) | (1, 1, 0) | **no hit**: mask 2, and corner `v0` is inside the ball (`*fraction` set to 0, return 0) |
| 10 | (−1, −1, 0.2) | (−1, −1, 0) | no hit: mask 2, moving away from the corner |
| 11 (late) | (−0.4, −0.6, 0.6) | (−0.2, 1.8, −1.4) | hit, fraction **0.476954**, mask 2, corner step picks **edge 2**; capsule roots [0.476954 (wall exit), 0.295703 (cap entry)], so the true first touch 0.295703 is **not** reported |
| 12 (missed) | (−0.9, −0.9, 0.6) | (0.6, 1.2, −0.6) | kernel returns 1 with fraction **1.783594** (mask 2, edge 2, roots [1.783594, 0.718294]); the mesh caller drops it, so the sphere passes the corner although it touches it at 0.718294 |
| 13 (parallel) | (6, −0.3, 0.2) | (−3, 0, 0) | mask 4, corner `v1` step picks **edge 0**, parallel branch with `M·E < 0`: roots [2.115470, 0.551197]; kernel returns 1 with fraction **2.115470**, dropped by the caller |
| 14 (cap) | (−2, −0.3, 0.2) | (3, 0, 0) | mask 2, corner `v0` step picks **edge 2** (the move crosses it). Hit, fraction **0.551197**, from the cap at `v0`: the wall roots fall outside the edge's length, and the end sphere's roots are [0.551197, 0.782137]. The caps do stop a sphere when they are the only thing it meets |

**Kernels on their own:**

- `Segment_SphereIntersect(A = 0, E = (4, 0, 0), C, r)`: `C = (2, −0.3, 0.3)`, r 0.5 gives 1, count 2, points
  (1.735425, 0, 0) and (2.264575, 0, 0). `C = (4.3, −0.3, 0)` gives 1, count 1, point (3.9, 0, 0) (`u1` 0.975,
  `u2` 1.175). `C = (2, 0, 0)`, r 5 gives 1, count 1, point **(7, 0, 0)** (`u1 < 0`, so `u2` = 1.75 is used,
  outside the segment). `C = (2, −0.5, 0)`, r 0.5 is tangent (`Δ` = 0 exactly) and gives 1, count 1, point
  (2, 0, 0). `C = (2, −1, 0)` gives 0.
- `Ray_InfiniteCylinder(r 0.5, c = (2, −0.3, 2), M = (0, 0, −3), A = 0, E = (4, 0, 0))`: 2, [0.533333, 0.8]. With
  `c = (−0.9, −0.2, 0.45)`, `M = (1.6, 0, −0.5)`: 2, **[1.816515, 0.336596]** (wall exit first, then the `A` cap's
  entry); `SphereSweep_EdgeCylinder` keeps 1 (0.336596) and returns 1, and `SphereSweep_Edge` reports
  **1.816515**.
- `Ray_SphereRoot`: `p = (−3, 0, 0)`, `d = (1, 0, 0)`, `R` 1 gives **2**. `p = (−3, 0.6, 0)`, `d = (2, 0, 0)`, `R` 1
  gives **1.1**. `p = (0.5, 0, 0)`, `d = (1, 0, 0)`, `R` 1 (inside) gives **−1.5**. `p = (−3, 2, 0)`,
  `d = (1, 0, 0)`, `R` 1 (a miss, `Δ` = −3) gives **2.535898** (`12 / (sqrt 3 + 3)`): no miss signal.
- `Ray_CircleRoot`: `p = (−3, 0)`, `d = (1, 0)`, `a` 1, `R` 4 gives **1**.
- `PointTriangle_DistanceSq(P, T)` as (`q`, `s`, `t`): (1, 1, 2) gives (4, 0.25, 0.25); (−1, −1, 0) gives (2, 0, 0);
  (5, −1, 1) gives (3, 1, 0); (3, 3, 0) gives (2, 0.5, 0.5); (0.5, 0.5, −0.5) gives (0.25, 0.125, 0.125);
  (2, −2, 0) gives (4, 0.5, 0); (−1, 2, 0) gives (1, 0, 0.5). This covers regions 0, 4, 6/5, 1, 0, 5 and 3.
- `SphereTriangle_Penetration`: centre (1, 1, 0.3), r 0.5 gives 1, depth **0.2**. Centre (1, 1, 0.5), r 0.5 gives
  **0** (`q = r²`, the test is strict). Centre (5, −1, 1), r 2 gives 1, depth **0.267949** (`2 − sqrt 3`).

### Seen at runtime (issue #348) {#u1-runtime}

Checked 2026-10-12 in PCSX2 2.9.94 (portable, PINE), from quick-save slots 1 and 6 copied with the patches and hooks
of `repo:research/traces/patches/physics-runtime.toml`.

**The kernel on synthetic triangles.** Through the `call-brains` call hook, the game itself called
`Sweep_SphereTriangle` (`0x0034ee60`) on the triangle `T` of [Test ideas](#u1-tests) (written into free EE memory at
`0x000e0000`, edges unnormalised, `d` = 0), radius 0.5, for all 14 rows of the table, with the `edge-roots` hook
(`0x0034e454`, in `SphereSweep_Edge` after its capsule step kept a root) logging `roots[0]`, `roots[1]`, the number of
roots in [0, 1] and the caller. Every row returned what the table says, to the printed digits (at most 2e-6 off):
fractions 0.5, 0, 0.533333, 0, none, none, 0.5, 0.538462, none, none, 0.476954, 1.783593, 2.11547, 0.551195, masks
7, 7, 6, 6, unwritten, unwritten, 7, 2, 2, 2, 2, 2, 4, 2. **Confirmed (runtime):**

- the **late** contact (row 11): the corner step (caller `0x0034e828`, inside `SphereSweep_VertexRegion`) got roots
  [0.476954, 0.295699], both in range, and the kernel returned the later 0.476954;
- the **missed** contact (row 12): roots [1.783593, 0.71829], one in range, and the kernel returned 1.783593, which
  the mesh callers drop;
- the parallel row 13 returned 2.11547 with 0.551197 second, and row 3's edge (caller `0x0034f644`, the swept
  branch's edge case) [0.533333, 0.8].

**In play.** Five scripted walks in level99's street (slot 1, stick 60 %, about 1,100 `Sweep_SphereTriangle` calls
from the walking capsule sweep `0x0034813c`, 540 edge steps) along and into a 0.37 m square post (material 16 at
(75.9, 35.4)) and towards a 1.3 m planter: the corner step (`0x0034e5d0`) **never ran**, and every edge step that kept
a root reported its first root in range (133 of 133 with one root in [0, 1]), so no late or missed contact. The walking
sphere's centre is 0.52 m above the feet (radius 0.4704), and the post's and planter's triangles are taller than it
on both sides, so the centre's projection is never past two edges of one triangle. Scenarios: `corner_post` and
`corner_planter` in `repo:research/traces/scenarios/`.

**Over a kerb and onto a ledge** (issue #348). Level99's mesh (1,718 triangles, read over PINE) has no kerb or
jumpable ledge on the street, so the 2.62 × 0.61 m box of material 41 at (47.40-48.01, 51.85-54.47), against a wall,
was given new heights in RAM (its eight vertices serve its ten triangles only, as in the
[step test](characters.md#walls)): a **0.15 m kerb** (`corner_kerb`, walk 60 % over it into the wall behind) and a
**0.6 m ledge** (`corner_ledge`, a run at 100 % and a jump that leaves the ground 2.8, 1.5, 1.0 or 0.5 m before it). The
`tri-sweep-tri` hook (`0x0034ee68`, two instructions into `Sweep_SphereTriangle`) logged every triangle that reached the
kernel and its caller. The kerb row and the 1.5 m row are the committed scenarios; the other jumps differ only in
the frame of the triangle tap (33, 40, 42 instead of 38) and were logged with the hook at the function's entry (the
1.5 m jump logged that way gave 2,937 sphere calls and 244 on the box, the rest equal). Confirmed (runtime),
PCSX2 2.9.94, slot 1:

| Run | Kernel calls (walking capsule / sphere in the air) | On the box's triangles | Corner steps (in the air, `vz` ≠ 0) | Edge or corner steps that kept a root |
| --- | --- | --- | --- | --- |
| kerb, walked onto in one update | 1,268 / 0 | **0** | 76 (0) | **0** |
| ledge, take-off 2.8 m before: lands on it | 2,528 / 4,919 | 30 / 428 | 89 (89) | **0** |
| ledge, take-off 1.5 m before: lands on it | 2,489 / 2,827 | 30 / 240 | 78 (75) | **0** |
| ledge, take-off 1.0 m before: lands on it | 2,450 / 2,367 | 30 / 194 | 75 (72) | **0** |
| ledge, take-off 0.5 m before: hits the face, falls back | 2,879 / 4,661 | 108 / 418 | 258 (132) | **0** |

- **A kerb never reaches the kernel.** Its faces are under 0.25 m, so the walking capsule sweep drops them before the
  kernel (the low-triangle rule of `0x00347c08`, [Characters](characters.md#walls)), and the snap lifts the feet onto
  it; no corner contact of any kind can come from a kerb on the ground. Confirmed (code) at `0x00347c08` and
  (runtime) above.
- **A jump takes a second path to the kernel.** In the air the walking sweep (caller `0x0034813c`) still runs, and
  `Human_PushOutInAir` (`0x0021a490`) adds, in the air and in a few states around it (50 to 371 of its calls fell in
  updates with `vz` = 0), the mesh-only sphere sweep through `IPhysics_CollideShape` (`0x0021aa14`,
  options `0x2004d`, [Unit 11](physics-bodies.md#u11-collide-shape)) to `PhysicsMesh_SweepSphere` (`0x00348a50`) and the
  thunk
  (`0x0034ee40`, return address `0x0034ee54`). That sweep has no 0.25 m rule, so in the air even short faces reach
  the kernel; the corner slide skips airborne bodies ([Unit 2](physics-bodies.md#u2-00347c08-physicsmesh-sweepcapsule)).
  Confirmed
  (code) at the cited addresses; the two callers confirmed (runtime) above.
- **Corner steps are common; late or missed contacts were not seen.** `SphereSweep_VertexRegion` ran 75 to 258
  times per run, nearly all in the air, but no edge step, from an edge or a corner, kept a cylinder root in any of
  the five runs (the `edge-roots` hook logged none; it logged 133 in a rerun of `corner_post` with the same hooks,
  so it works). Each step ended earlier: the edge segment was already within the sphere (fraction 0), the corner
  already inside it, or the move went away from the corner (inferred from the logs: those exits are not hooked).
  So the late and missed contacts of rows 11 and 12 of [Test ideas](#u1-tests) need a sphere sweeping onto a corner
  from outside within one update, which these walks and jumps did not do. They are rare in play (inferred: none
  in seven runs at a post, a planter, a kerb and a ledge). Where one does happen, the late root delays a contact
  within one update's move and the missed one lets the sphere pass a corner by less than its radius.

**A strike shape's previous position.** `strike_prev` (slot 6, three squares at a parked sedan), with the
`sphere-pose` hook (`0x00342b08`) and the `strike-move-*` hooks at the six places `Human_TestStrikes` reads a strike
shape's `+0x20` (`0x0033f284`, `0x0033f3d4`, `0x0033f714`, `0x0033f9f4`, `0x0033fb54`, `0x0033fcec`). **Confirmed
(runtime):** a right-hand sphere switched on in update 32 (`strike-shape`, bone `0x19`) was posed once per update
(updates 32 to 36), each pose before that update's strike test, and at each test its previous centre was exactly
the centre of the update before (for example update 33: from (40.6391, 46.1587) to (41.1509, 45.4583), 0.87 m in
one update). Its first pose had flag `0x10` set (`+0x32` = `0x0013`) and a stale centre from elsewhere in the
level (56.75, 41.42); the test in that update had previous = current, a zero move, as
[the reset rule](physics-bodies.md#u6-reset) says. A sphere is posed only while switched on, so the reset is what keeps
the first
swing from sweeping from where the hand was last posed.

## Unit 4: Static shape-pair overlaps {#unit-4}

**What it is.** The static overlap of two shapes (the overlap table at IPhysics `+0x18`), the segment kernels, and how
`IPhysics_OverlapShape` turns a pair's answer into a contact. Its callers are the push sphere, pick-ups and an
object touched by a human; **strikes do not use it** ([Who reaches a car](#who-reaches-a-car)).

- **Coney:** `src/world_objects/object_bodies.*` (today a sphere-against-body push of its own) and the push sphere.
- **Tests:** [Test ideas](#u4-test-ideas-synthetic-shapes-expected-val), one per quirk the original has.

19 functions, 6044 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00345d58` | `PhysicsOverlap_SphereSphere` | 124 |
| `0x00345dd8` | `PhysicsOverlap_BoxBox` | 1452 |
| `0x00346388` | `PhysicsOverlap_CapsuleCapsule` | 372 |
| `0x00346500` | `PhysicsOverlap_SegmentSegment` | 384 |
| `0x00346680` | `PhysicsOverlap_BoxSphere` | 388 |
| `0x00346808` | `PhysicsOverlap_CapsuleBox` | 1292 |
| `0x00346d18` | `PhysicsOverlap_BoxCapsule` | 156 |
| `0x00346db8` | `PhysicsOverlap_SphereBox` | 36 |
| `0x00346de0` | `PhysicsOverlap_SegmentSphere` | 208 |
| `0x00346eb0` | `PhysicsOverlap_SphereSegment` | 36 |
| `0x00346ed8` | `PhysicsOverlap_CapsuleSphere` | 184 |
| `0x00346f90` | `PhysicsOverlap_SphereCapsule` | 36 |
| `0x00346fb8` | `PhysicsOverlap_SegmentCapsule` | 396 |
| `0x00347148` | `PhysicsOverlap_CapsuleSegment` | 36 |
| `0x0034a2d0` | `CapsuleBox_CornerCircle` | 68 |
| `0x0034a6a0` | `CapsuleBox_SideInterval` | 44 |
| `0x0034ac08` | `Segment_ClosestParam` | 192 |
| `0x0034acc8` | `Segment_ClosestPoint` | 132 |
| `0x0034ad50` | `Segment_ClosestPoints` | 508 |

### Summary for the implementer {#u4-summary-for-the-implementer}

- **Calling convention** (confirmed (code) at `0x0033ebb8`): every overlap entry is
  `bool fn(Contact* c, Shape* own, Shape* other)`, `own` being the querying (row) shape. It returns 1 on overlap, 0
  otherwise.
- **Touching counts as overlap** in every test except the capsule-capsule horizontal test (strict) and the box-box
  test (inclusive; it separates only on a strict `>`). Each function below says which.
- **Most tests write nothing into the contact.** Only three do: capsule-capsule (`+0x10`, `+0x50`, `+0x88`),
  capsule-box (`+0x50`, `+0x88`) and its mirror box-capsule (`+0x10` and whatever was in `+0x10`, swapped into
  `+0x50`). **No overlap test writes the point `+0x90`, the fraction `+0x80` or the slide `+0xa0`.**
- **New contact field `+0x88`**: a penetration depth, written only by the two tests above (metres, except a capsule-box
  corner, where it is a squared quantity, m²; see [CapsuleBox](#u4-physicsoverlap-capsulebox-00346808)).
- **Half normals** (inferred from the three writers and the mirrors): each 0x40-byte half has a normal at its `+0x10`
  that points from **that half's shape towards the other one**. So `+0x50` (second half) is the other shape's outward
  normal, pointing at the querying shape, and `+0x10` points the other way. A mirrored entry swaps the whole halves,
  which keeps that meaning.
- **Strikes do not use these functions against cars.** `Human_TestStrikes` (`0x0033f110`) calls the **sweep** table
  (IPhysics `+0x7c`), not the overlap table: a sphere strike reaches a box through `PhysicsSweep_SphereBox`
  (`0x003453f8`), and a segment strike through `PhysicsSweep_SegmentBox` (`0x00345588`), which always returns 0. So
  **only the sphere strike shapes can hit a car**: the hands (bones 19, 25, radius 0.09), the head (6, 0.15), the
  feet (30, 33, 0.15) and a held weapon's strike spheres. The forearms (18, 24), the shins (29, 32) and the spine (3)
  never do. Confirmed (code); see [Who reaches a car](#who-reaches-a-car). The car's contact point therefore comes
  from the sphere-box **sweep** ([Unit 5](#unit-5)), not from this one.

### How `IPhysics_OverlapShape` fills a contact {#overlapshape}

`IPhysics_OverlapShape(phys, out, query)` (`0x0033ebb8`) returns the number of contacts copied to `out`. Confirmed
(code) at `0x0033ebb8` unless marked.

The query record:

| Offset | Meaning |
| --- | --- |
| `+0x00` | the querying body |
| `+0x04` | the querying shape (`own`) |
| `+0x08` | a **pointer** to a box to gather candidates with (word load at `0x0033ec10`), used instead of the shape's own world box when option `0x400` is set |
| `+0x10` | options: `0x1` the level mesh, `0x2` bodies, `0x10` use the type mask at `+0x14`, `0x80` skip the same non-zero group (body `+0x3c`), `0x100` ask the querying owner (owner vtable `+0x110`/`+0x114` with the other owner; 0 skips), `0x200` only shapes with flag `0x1` (enabled), `0x400` the box at `+0x08` |
| `+0x14` | type mask (−1, any, unless option `0x10`) |
| `+0x18` | the most contacts to return (0: no body is tested at all) |
| `+0x28`, `+0x2c` | two thresholds passed to the mesh overlap (`+0xf4`) |

Steps:

1. The candidate box is the querying shape's world bounds (`PhysicsBody_ComputeBounds` on `+0x04`), or `+0x08`
   with option `0x400`. With option `0x2`, the candidates are `IPhysics_QueryBox(phys, list, box, body,
   body's owner, typeMask, 0)` ([Queries](physics.md#queries)): bodies whose world box overlaps it in x, y and z, the
   querying owner's own bodies excluded. **This box test is the only vertical test that a capsule-box pair gets**
   (see [CapsuleBox](#u4-physicsoverlap-capsulebox-00346808)). Inferred from the two functions together.
2. For each candidate (after the `0x80` and `0x100` filters), its shape chain is fetched with body vtable `+0x2c`
   called with `(2, 4, querying owner)`; a human body answers its capsule chain (`+0x30`, `0x003420c8`). Every shape
   of the chain (only enabled ones with `0x200`) is tested with `overlap[own.type][other.type]` (IPhysics
   `+0x18 + own.type × 0x14 + other.type × 4`; an empty entry is skipped).
3. A test writes into the next free 0xc0-byte slot of a 50-slot stack buffer. **The slot is not cleared first**: any
   field the pair function does not write holds whatever the stack held. On a hit: `+0xb4` (mesh triangle) = 0, then
   `Contact_InsertSorted` (`0x0033d718`) sets `+0x00` = querying body's owner (body `+0x34`), `+0x04` = `own`,
   `+0x40` = other body's owner, `+0x44` = the other shape, and inserts the slot into the list in ascending order of
   `+0x80`, before the first entry whose `+0x80` is strictly greater (equal keys keep discovery order).
4. Because no overlap test writes `+0x80`, **the order of body contacts is undefined in the original** (sorted by
   stack leftovers). Inferred from 1-3. None of the four callers depends on it for more than which of several
   overlapping bodies is tried first (see below); Coney should keep discovery order (the candidates' x-sorted list
   order, then the other body's shape-chain order) and say so in a comment.
5. The first `min(count, max)` contacts are copied to `out` (0xc0 bytes each). There is no check that more than 50
   hits fit the buffer (the sorted list holds 52 pointers; inferred: never reached in play). The `0x2000` of the
   push sphere's `0x2012` and the `0x4` and `0x8` of the climb's `0xd` are never read here ([Unit
   7](physics-bodies.md#u7-overlapshape)).
6. If fewer than `max` were copied and option `0x1` is set, the shape type's mesh overlap (`+0xf4`) appends up to
   `max − count` more after them, unsorted against the body contacts; only the sphere has one that can hit
   ([Mesh tests](physics.md#mesh-tests)).

The callers, and what they read (confirmed (code) at each):

| Caller | Querying shape | Options, type mask, max | Pairs it can reach | Reads from each contact |
| --- | --- | --- | --- | --- |
| `PhysicsHumanBody_PushAwayHumans` `0x003425c0` | the 2.0 m push sphere (`+0x84`) | `0x2012`, `0x40` (humans), 32 | sphere → the other humans' capsules: **SphereCapsule** | `+0x40` only (the push direction comes from the two humans' positions, not the contact) |
| `Human_FindPickupNear` `0x002193e4` | the human's capsule | `0x12`, `8` (objects), 32 | **CapsuleBox**, CapsuleSphere | `+0x40` only |
| `WorldObject_TryTouchPickUp` `0x003955cc` | the object's first shape | `0x12`, `0x40` (humans), 24 | **BoxCapsule**, SphereCapsule | `+0x40` only; the first human that sees it and can pick it up takes it |
| `Climb_IsSpaceClear` `0x00281150` | a temporary sphere of the capsule radius | `0xd` (mesh only, no bodies), max 1 | none of this unit (sphere-mesh) | the count |

So no current caller reads the normal or depth an overlap test writes. The writes are specified anyway because
Coney's contacts should match the record.

### The shape fields read {#u4-the-shape-fields-read}

| Type | Fields the overlap tests read |
| --- | --- |
| 1 box | `+0x40`/`+0x44`/`+0x48` half extents h0, h1, h2; `+0x50`, `+0x60`, `+0x70` axes A0, A1, A2 (x, y, z; w taken as 0); `+0x80` centre P (w 1) |
| 2 sphere | `+0x40` radius r; `+0x50` centre C |
| 3 capsule | `+0x40` radius r; `+0x44` height h; `+0x50` base B (x, y, z); `+0x58` is base z |
| 4 segment | `+0x40` start S; `+0x50` end E; `+0x60` radius r (`+0x64` length is not read: the direction is E − S) |

### The kernels {#u4-the-kernels}

### Segment_ClosestParam (0x0034ac08) {#u4-segment-closestparam-0034ac08}

Size 0xc0. `void Segment_ClosestParam(float* t, const Vec4* p0, const Vec4* d, const Vec4* q)`: the parameter of the
point of the segment `p0 + t·d`, `t ∈ [0, 1]`, nearest `q`. Confirmed (code) at `0x0034ac08`:

```text
k = (q − p0) · d          // xyz dot
t = 0
if k > 0:
    t = (d·d < k) ? 1 : k / (d·d)
```

Edge cases: a degenerate segment (`d = 0`) gives `k = 0`, so `t = 0` (no division). `k` exactly equal to `d·d`
takes the division and gives 1.

### Segment_ClosestPoint (0x0034acc8) {#u4-segment-closestpoint-0034acc8}

Size 0x84. `void Segment_ClosestPoint(Vec4* out, const Vec4* p0, const Vec4* d, const Vec4* q)`: `out = p0 + t·d`
with `t` from `Segment_ClosestParam` on the same arguments; `out.w = 1`. Confirmed (code) at `0x0034acc8`.

### Segment_ClosestPoints (0x0034ad50) {#u4-segment-closestpoints-0034ad50}

Size 0x1fc. `void Segment_ClosestPoints(float* s, float* t, const Vec4* d1, const Vec4* d2, const Vec4* w)`, with
`w = p1 − p2` (the two starts): the parameters of the closest points `p1 + s·d1` and `p2 + t·d2`, both in [0, 1].
It is the standard clamped solution (the form published by Dan Sunday), with one threshold 1e-6 (`0x358637bd`).
Confirmed (code) at `0x0034ad50`:

```text
a = d1·d1;  b = d1·d2;  c = d2·d2;  d = d1·w;  e = d2·w;  D = a·c − b²
if D < 1e-6:          sN = 0;  sD = 1;  tN = e;  tD = c          // parallel (or degenerate)
else:
    sN = b·e − c·d;  tN = a·e − b·d;  sD = tD = D
    if sN < 0:        sN = 0;  tN = e;  tD = c
    elif sN > sD:     sN = sD; tN = e + b; tD = c
if tN < 0:
    tN = 0
    if −d < 0: sN = 0          elif −d > a: sN = sD       else: sN = −d;  sD = a
elif tN > tD:
    tN = tD
    if b − d < 0: sN = 0       elif b − d > a: sN = sD    else: sN = b − d; sD = a
s = (|sN| < 1e-6) ? 0 : sN / sD
t = (|tN| < 1e-6) ? 0 : tN / tD
```

(When `sN` is set to `sD` in the last two branches, `s` comes out 1.) Edge cases, confirmed (code), consequences
inferred:

- **The parallel test is absolute**, not relative to the lengths: two short segments are treated as parallel when
  `a·c·sin²θ < 1e-6`. For two forearms (length 0.20 m) that is θ < 1.4°; for a forearm against a shin (0.40 m),
  θ < 0.7°. Parallel means `s = 0` and `t` from the start of the first segment, then the usual clamping.
- **A degenerate second segment** (`d2 = 0`) gives `b = c = e = 0`, so `D = 0`, `tN = 0`, and the `tN` branches are
  not taken: **`s` stays 0** whatever the first segment's direction. The first segment is then measured from its start
  only, not from its nearest point. (A degenerate first segment is handled correctly: `s = 0`, `t` clamped from `e/c`.)
- Both zero: `s = t = 0`, no division.

### CapsuleBox_CornerCircle (0x0034a2d0) {#u4-capsulebox-cornercircle-0034a2d0}

Size 0x44. `bool CapsuleBox_CornerCircle(float x, float y, float r, float hx, float hy, float* depth)` with the point
already mirrored so that `x < −hx` and `y < −hy`: the circle of radius `r` at `(x, y)` against the corner
`(−hx, −hy)`. Confirmed (code) at `0x0034a2d0`:

```text
q = (x + hx)² + (y + hy)² − r²
if q <= 0: *depth = −q (= r² − distance²); return 1
return 0
```

Touching (q = 0) is a hit with depth 0. **The depth is a squared length** (m²), not a distance.

### CapsuleBox_SideInterval (0x0034a6a0) {#u4-capsulebox-sideinterval-0034a6a0}

Size 0x2c. `bool CapsuleBox_SideInterval(float x, float y, float r, float hx, float hy, float* depth)` with `x < −hx`
and `|y| ≤ hy`: the circle against the face `x = −hx`. Only `x`, `r`, `hx` are used. Confirmed (code) at
`0x0034a6a0`:

```text
s = x + hx + r            // r minus the gap to the face
if s >= 0: *depth = s; return 1
return 0
```

Touching (gap = r) is a hit with depth 0.

### The pair functions {#u4-the-pair-functions}

### PhysicsOverlap_SphereSphere (0x00345d58) {#u4-physicsoverlap-spheresphere-00345d58}

Table (2, 2). Size 0x7c. Reads sphere `+0x40` r, `+0x50` C of both. Writes nothing. Confirmed (code) at
`0x00345d58`:

> hit when `|C_own − C_other|² ≤ (r_own + r_other)²` (xyz).

Touching is a hit. Coincident centres are a hit.

### PhysicsOverlap_BoxBox (0x00345dd8) {#u4-physicsoverlap-boxbox-00345dd8}

Table (1, 1). Size 0x5ac. Reads both boxes' half extents, axes and centres. Writes nothing. Confirmed (code) at
`0x00345dd8`.

It is the 15-axis separating-axis test (the three axes of each box, then the nine cross products `A_i × B_j`, in the
usual Gottschalk form, with no epsilon added to the absolute matrix). With `a`, `b` the half extents of `own` (A)
and `other` (B), `t[k] = A_k · (P_B − P_A)` and a 3 × 3 matrix `R`, the boxes are separated when any of these holds
(strict `>`, so touching boxes overlap):

```text
for k in 0..2:  |t[k]|                     > a[k] + Σ_m b[m]·|R[k][m]|
for m in 0..2:  |Σ_k t[k]·R[k][m]|         > Σ_k a[k]·|R[k][m]| + b[m]
for i, j in 0..2 (i1 = i+1, i2 = i+2, j1 = j+1, j2 = j+2, mod 3):
                |t[i2]·R[i1][j] − t[i1]·R[i2][j]|
                                           > a[i1]·|R[i2][j]| + a[i2]·|R[i1][j]| + b[j1]·|R[i][j2]| + b[j2]·|R[i][j1]|
```

The order of the tests does not change the answer.

**The quirk: how `R` is built.** A correct test uses `R[k][m] = A_k · B_m`. The original computes

```text
R[k][m] = Σ_{n=0..3} A_n[k] · B_n[m]        // A_3 = P_A, B_3 = P_B: the centres take part
```

that is, the transpose-product the other way round, over all four rows of each box's matrix, centres included.
Confirmed (code) at `0x00345eb8`-`0x00345ee8` (the loop broadcasts the columns of A against the rows of B). For two
axis-aligned boxes with A at the origin it gives the right answer; otherwise it does not. Example (computed from the
spec): two axis-aligned boxes of half extent 1 at (100, 0, 0) and (101.5, 0, 0) overlap by 0.5 m, but `R[0][0] =
1 + 100 × 101.5 = 10151` and the B-axis test `|1.5 × 10151| > 10151 + 1` reports them separated, so the function
returns 0. **No caller pairs two boxes** (inferred: the four callers of `0x0033ebb8` query with a sphere, a capsule
or one object's shape against humans, whose chain is the capsule), so the quirk never shows in play. Coney should
reproduce it behind a comment and a test, or, if the maintainers prefer, implement the correct test and record the
difference: it has no player-visible effect.

### PhysicsOverlap_CapsuleCapsule (0x00346388) {#u4-physicsoverlap-capsulecapsule-00346388}

Table (3, 3). Size 0x174. Reads `+0x40` r, `+0x44` h, `+0x50` B of both (`+0x58` = base z). Confirmed (code) at
`0x00346388`. Both capsules are **upright cylinders** here (flat ends):

1. Vertical: the ranges `[B_own.z, B_own.z + h_own]` and `[B_other.z, B_other.z + h_other]` must overlap, inclusive
   (`B_own.z ≤ B_other.z + h_other` and `B_other.z ≤ B_own.z + h_own`). Otherwise 0.
2. Horizontal: `d = B_other − B_own` with `d.z = 0`; hit only when `|d|² < (r_own + r_other)²` (**strict**:
   touching side by side is not a hit).
3. On a hit it writes:
   - `+0x10` (own half) = `d / |d|`, w = 1: the horizontal unit vector from `own` to `other`. When `|d|² < 1e-6`
     (`0x358637bd`, coincident axes) it is the constant **(0, 1, 0, 1)** (`0x00511730`) instead.
   - `+0x50` (other half) = −`+0x10` in xyz, w = 1: pointing from `other` to `own`.
   - `+0x88` = `(r_own + r_other) − |d|` (horizontal penetration, metres; with the constant normal, the full sum of
     the radii).

The vertical overlap depth is not computed. `+0x80`, `+0x90` are not written.

### PhysicsOverlap_SegmentSegment (0x00346500) {#u4-physicsoverlap-segmentsegment-00346500}

Table (4, 4); also the sweep table's (4, 4) through `0x00344300` (strikes, below). Size 0x180. Reads `+0x40` S,
`+0x50` E, `+0x60` r of both. Writes nothing. Confirmed (code) at `0x00346500`:

```text
d1 = E_own − S_own;  d2 = E_other − S_other;  w = S_own − S_other
(s, t) = Segment_ClosestPoints(d1, d2, w)
hit when |w + s·d1 − t·d2|² ≤ (r_own + r_other)²
```

Touching is a hit. The kernel's two edge cases (absolute parallel threshold; a zero-length `other` measured from
`own`'s start) apply.

### PhysicsOverlap_BoxSphere (0x00346680) {#u4-physicsoverlap-boxsphere-00346680}

Table (1, 2). Size 0x184. Reads the box (own) half extents, axes, centre and the sphere's r, C. Writes nothing.
Confirmed (code) at `0x00346680`:

```text
for k in 0..2:  x[k] = A_k · (C − P)                    // the centre in the box's frame
dist² = Σ_k (x[k] < −h[k] ? (x[k] + h[k])² : x[k] > h[k] ? (x[k] − h[k])² : 0)
hit when dist² ≤ r²
```

A centre inside the box gives 0, so it always hits. Touching is a hit. The box is treated as a true 3D box.

### PhysicsOverlap_SphereBox (0x00346db8) {#u4-physicsoverlap-spherebox-00346db8}

Table (2, 1). Size 0x28. Mirror: calls `PhysicsOverlap_BoxSphere(c, other, own)` and returns its result. **No halves
are swapped** (there is nothing written to swap). Confirmed (code) at `0x00346db8`.

### PhysicsOverlap_CapsuleBox (0x00346808) {#u4-physicsoverlap-capsulebox-00346808}

Table (3, 1). Size 0x50c. Reads the capsule's `+0x40` r and `+0x50` B (x, y; **not** its height `+0x44` or base z),
the box's half extents, axes (including each axis's z, `+0x58`/`+0x68`/`+0x78`) and centre. Writes `+0x50` and
`+0x88`. Confirmed (code) at `0x00346808` and the two kernels.

**It is a 2D test** in the box's horizontal plane: a circle against a rectangle. Neither the capsule's vertical range
nor the box's vertical extent is read, so two shapes far apart in height overlap if their footprints do; only the
query's box gather (x, y and z, [above](#overlapshape)) keeps them apart vertically. Inferred consequence.

1. **The vertical axis**: `u` = the box axis with the largest `|A_k.z|` (first one wins a tie). The two in-plane axes
   are `i = (u + 1) mod 3` and `j = (i + 1) mod 3`, with half extents `hx = h[i]`, `hy = h[j]`.
2. `d = B − P` with `d.z = 0`; `x = d · A_i`, `y = d · A_j` (full 3D dot products with the zeroed d).
3. The nine regions (r = capsule radius):

| Region | Test | Normal written to `+0x50` | `+0x88` |
| --- | --- | --- | --- |
| `x < −hx`, `abs(y) ≤ hy` | `SideInterval(x, y, r, hx, hy)` | **−A_i**, w 1 | `x + hx + r` |
| `x > hx`, `abs(y) ≤ hy` | `SideInterval(−x, y, r, hx, hy)` | **+A_i**, w 1 | `−x + hx + r` |
| `abs(x) ≤ hx`, `y < −hy` | `SideInterval(y, x, r, hy, hx)` | **−A_j**, w 1 | `y + hy + r` |
| `abs(x) ≤ hx`, `y > hy` | `SideInterval(−y, x, r, hy, hx)` | **+A_j**, w 1 | `−y + hy + r` |
| `x < −hx`, `y > hy` | `CornerCircle(x, −y, r, hx, hy)` | normalize(`(x + hx)·A0 + (y − hy)·A1`) | `r² − dist²` |
| `x > hx`, `y < −hy` | `CornerCircle(−x, y, r, hx, hy)` | normalize(`(x − hx)·A0 + (y + hy)·A1`) | `r² − dist²` |
| `x > hx`, `y > hy` | `CornerCircle(−x, −y, r, hx, hy)` | normalize(`(x − hx)·A0 + (y − hy)·A1`) | `r² − dist²` |
| `x < −hx`, `y < −hy` | `CornerCircle(x, y, r, hx, hy)` | normalize(`(x − hx)·A0 + (y − hy)·A1`) (**quirk**) | `r² − dist²` |
| `abs(x) ≤ hx`, `abs(y) ≤ hy` (centre over the box) | always a hit | if `x < y` (signed): `A_j`, negated when `y < 0`; else `A_i`, negated when `x < 0`; w 1 | `hy − abs(y) + r` or `hx − abs(x) + r`, for the axis chosen |

The boundaries: "`x < −hx`" and "`x > hx`" are strict, so `x = ±hx` is in the middle column (the same for y).
A failed kernel returns 0 at once.

Quirks, each confirmed (code) at `0x00346808`:

- **The (−, −) corner's normal is built from the opposite corner** `(+hx, +hy)`: `(x − hx, y − hy)` instead of
  `(x + hx, y + hy)`. It still points into the (−, −) quadrant, so it is outward, but skewed. The other three corners
  are right. The hit test and depth are right for all four.
- **Corner normals always use axes 0 and 1** (`A0`, `A1`), whatever `i` and `j` are, and the in-plane offset is put
  in their x and y slots with z = 0. Correct only when `u = 2` (the box's third axis is the most vertical), which is
  the case for every box posed from an upright owner (cars, objects at rest); inferred.
- The corner normal is normalized in xyz (`vrsqrt`); its w is not set (stack leftover). Face normals have w = 1.
- **The corner depth is squared** (m²), the face depth is in metres.
- **Over the box** the face is chosen by comparing the signed coordinates `x < y`, not by the nearest face: a capsule
  at `x = −1.5, y = 0.1` over a box with `hx = 2, hy = 1` gets `+A_j` and depth `1.25`, though the nearest face is −x.

### PhysicsOverlap_BoxCapsule (0x00346d18) {#u4-physicsoverlap-boxcapsule-00346d18}

Table (1, 3). Size 0x9c. Mirror: calls `PhysicsOverlap_CapsuleBox(c, other, own)`; on a hit it **swaps the two
0x40-byte halves of the contact wholesale** (`+0x00..+0x3f` ↔ `+0x40..+0x7f`); `+0x80` onwards are untouched.
Confirmed (code) at `0x00346d18`. So after it:

- `+0x10` = the box's outward normal towards the capsule (what CapsuleBox put in `+0x50`), pointing from the querying
  box to the other shape;
- `+0x50` = whatever `+0x10` held before the call, which CapsuleBox never writes: **uninitialised in the original**
  (the slot is a fresh, uncleared stack buffer, [above](#overlapshape)). Coney: write the capsule-side normal (the
  negated box normal) there and note that the original leaves it undefined; no caller reads it.
- `+0x88` = the depth, unchanged.

The owner and shape words swapped here are overwritten afterwards by `Contact_InsertSorted`, so they end correct.

### PhysicsOverlap_SegmentSphere (0x00346de0) {#u4-physicsoverlap-segmentsphere-00346de0}

Table (4, 2). Size 0xd0. Reads the segment's S, E, r and the sphere's r, C. Writes nothing. Confirmed (code) at
`0x00346de0`:

> `Q = Segment_ClosestPoint(S, E − S, C)`; hit when `|Q − C|² ≤ (r_seg + r_sph)²`.

Touching is a hit. A zero-length segment is a sphere at S.

### PhysicsOverlap_SphereSegment (0x00346eb0) {#u4-physicsoverlap-spheresegment-00346eb0}

Table (2, 4). Size 0x24. Mirror: `PhysicsOverlap_SegmentSphere(c, other, own)`, result returned, no swap. Confirmed
(code) at `0x00346eb0`.

### PhysicsOverlap_CapsuleSphere (0x00346ed8) {#u4-physicsoverlap-capsulesphere-00346ed8}

Table (3, 2). Size 0xb8. Reads the capsule's r, h, B and the sphere's r, C. Writes nothing. Confirmed (code) at
`0x00346ed8`. **Here the capsule is a true capsule** (a sphere-swept vertical segment with rounded ends), not a flat
cylinder:

> `Q = Segment_ClosestPoint(B, (0, 0, h), C)`; hit when `|Q − C|² ≤ (r_cap + r_sph)²`.

So the capsule reaches `r_cap` below its base and above `B.z + h`. Touching is a hit. h = 0 makes it a sphere at B.
This corrects [#shapes](physics.md#shapes)' "every test treats it as the vertical range": the capsule-sphere and
capsule-segment tests do not.

### PhysicsOverlap_SphereCapsule (0x00346f90) {#u4-physicsoverlap-spherecapsule-00346f90}

Table (2, 3). Size 0x24. Mirror: `PhysicsOverlap_CapsuleSphere(c, other, own)`, no swap. Confirmed (code) at
`0x00346f90`. This is the push sphere's test against another human ([The human body](physics.md#human-body)): the
2.0 m sphere against the human's rounded capsule.

### PhysicsOverlap_SegmentCapsule (0x00346fb8) {#u4-physicsoverlap-segmentcapsule-00346fb8}

Table (4, 3); also the sweep table's (4, 3) through `0x00345d38`. Size 0x18c. Reads the segment's S, E, r and the
capsule's r, h, B. Writes nothing. Confirmed (code) at `0x00346fb8`. The capsule is again a **true capsule**:

```text
d1 = E − S;  d2 = (0, 0, h)  (from the constant (0, 0, 1, 1) at 0x00511740 scaled by h);  w = S − B
(s, t) = Segment_ClosestPoints(d1, d2, w)
hit when |w + s·d1 − t·d2|² ≤ (r_seg + r_cap)²
```

Touching is a hit.

### PhysicsOverlap_CapsuleSegment (0x00347148) {#u4-physicsoverlap-capsulesegment-00347148}

Table (3, 4); also the sweep table's (3, 4) through `0x00345d18`. Size 0x24. Mirror:
`PhysicsOverlap_SegmentCapsule(c, other, own)`, no swap. Confirmed (code) at `0x00347148`.

### Who reaches a car {#who-reaches-a-car}

- **A segment never touches a box**, in either table: overlap (4, 1) and (1, 4) are empty (`IPhysics_Construct`,
  [#dispatch](physics.md#dispatch)); sweep (4, 1) is `PhysicsSweep_SegmentBox` `0x00345588`, which returns 0, and
  sweep (1, 4) `0x00345590` calls it with the move negated, so also 0. Confirmed (code).
- **Strikes use the sweep table.** `Human_TestStrikes` (`0x0033f110`) takes, for each enabled strike shape with flag
  `0x2` (and each held-weapon strike sphere), its current world position (shape vtable `+0x24`) and its previous one
  (shape `+0x20`), moves the shape back to the previous position (shape vtable `+0xc`), calls
  `sweep[strike.type][other.type](contact, body, shape, otherBody, otherShape, &move)` (IPhysics `+0x7c`) with
  `move = current − previous`, and puts the shape back. Confirmed (code) at `0x0033f110`. Against an object's body
  (a car's two boxes) every shape of its chain is tried; the first hit calls the striker's `+0x104`
  (`Strike_Contact`) with this contact as the hit record, and the test goes on with the next body.
- So a car box is reached only by **sphere** strike shapes, through `PhysicsSweep_SphereBox` (`0x003453f8`,
  `Sweep_SphereOrientedBox` `0x0034bbe8`), which writes the fraction `+0x80`, the point `+0x90`, `+0x10`, `+0x50`,
  `+0x60`, `+0x70`/`+0x74` and clears `+0xb0`; what each means, and which `Car_OnHit` reads, is in
  [Unit 5](#u4b-summary-for-the-implementer). Confirmed (code) at `0x003453f8`.
- The strike spheres (from [Combat](combat.md#moving-strikes)): hands 19 and 25 (0.09 m), head 6 (0.15 m), feet 30
  and 33 (0.15 m), and the held weapon's row of 0.1 m spheres (or its single sphere). The `car_glance` runtime hit
  (`S1`, bone 19) is consistent with this.
- **The segment's "previous position" moves only its start.** A segment's position setter (`0x004f1250`, vtable
  `0x00544770` `+0xc`) writes `+0x40` only, and its position getter (`0x004f1260`) returns `+0x40`. So during a strike
  test a forearm, shin or spine segment runs from its **previous start to its current end**, and segment-segment and
  segment-capsule are tested on that stretched segment. Confirmed (code) at `0x004f1250`, `0x004f1260` and
  `0x0033f110`; the consequence (a slightly longer reach along the swing) is inferred. A sphere's setter
  (`0x004f1200`) moves its centre; the sphere-box sweep then sweeps it back to the current position.

### Test ideas (synthetic shapes) {#u4-test-ideas-synthetic-shapes-expected-val}

All boxes: axes A0 = (1, 0, 0), A1 = (0, 1, 0), A2 = (0, 0, 1) unless stated.

1. **SphereSphere touching.** r 0.5 at the origin and r 0.5 at (1, 0, 0): hit. At (1.001, 0, 0): miss. The contact
   slot is unchanged in both cases.
2. **CapsuleCapsule.** own r 0.35, h 2.0, base (0, 0, 0); other r 0.35, h 2.0:
   - base (0.5, 0, 1.0): hit; `+0x10` = (1, 0, 0, 1), `+0x50` = (−1, 0, 0, 1), `+0x88` = 0.2.
   - base (0, 0, 1.5) (same axis): hit; `+0x10` = (0, 1, 0, 1), `+0x50` = (0, −1, 0, 1), `+0x88` = 0.7.
   - base (0.5, 0, 2.0) (ends touching): hit (vertical test inclusive).
   - base (0.7, 0, 0) (sides touching, 0.7² vs (0.35 + 0.35)², equal in float): **miss** (strict).
   - base (0.5, 0, 2.01): miss.
3. **SegmentSegment.** own (0, 0, 0)-(1, 0, 0) r 0.1:
   - other (0.5, −1, 0.15)-(0.5, 1, 0.15) r 0.1: hit (distance 0.15).
   - other at z 0.2 (distance 0.2 = 0.1 + 0.1, equal in float): hit (inclusive). At z 0.21: miss.
   - other (0.5, 0.1, 0)-(1.5, 0.1, 0) r 0.1 (parallel): closest params s = 0.5, t = 0; distance 0.1: hit.
   - other (2, 0.1, 0)-(3, 0.1, 0) r 0.1 (parallel, apart): s = 1, t = 0; distance² 1.01: miss.
   - **Degenerate other** (0.9, 0.05, 0)-(0.9, 0.05, 0) r 0.1: s = 0, t = 0, distance² 0.8125: **miss** (the true
     distance is 0.05). Pins the kernel quirk.
4. **BoxSphere / SphereBox.** Box at the origin, half (1, 1, 1):
   - sphere r 0.5 at (1.5, 0, 0): hit (touching). At (1.51, 0, 0): miss.
   - sphere r 0.01 at (0.2, 0.3, 0) (inside): hit.
   - sphere r 0.7 at (1.5, 1.5, 0): distance² 0.5 > 0.49: miss; r 0.75: hit.
   - box rotated 45° about z (A0 = (0.7071, 0.7071, 0), A1 = (−0.7071, 0.7071, 0)), half (1, 0.5, 0.5); sphere r 0.4 at
     (0, 1, 0): local (0.7071, 0.7071, 0), outside y by 0.2071, distance² 0.0429: hit; r 0.2: miss.
   - SphereBox with the arguments swapped gives the same results.
5. **CapsuleBox** (car-like box: centre (0, 0, 0.5), half (2, 1, 0.5); so u = 2, i = 0, j = 1). Capsule r 0.35,
   h 2.0:
   - base (0, −1.2, 0): face; hit; `+0x50` = (0, −1, 0, 1); `+0x88` = 0.15.
   - base (0, −1.36, 0): gap 0.36 > 0.35: miss.
   - base (2.2, 1.2, 0): corner (+, +); hit; `+0x50` xyz = (0.7071, 0.7071, 0); `+0x88` = 0.1225 − 0.08 = 0.0425 (m²).
   - base (2.3, 1.3, 0): corner distance² 0.18 > 0.1225: miss.
   - base (−2.2, 1.2, 0): corner (−, +); `+0x50` xyz = (−0.7071, 0.7071, 0).
   - **base (−2.2, −1.2, 0): corner (−, −); hit, depth 0.0425; `+0x50` xyz = normalize(−4.2, −2.2, 0) =
     (−0.8858, −0.4640, 0)**, not (−0.7071, −0.7071, 0). Pins the corner quirk.
   - base (0.5, 0.2, 0) (over the box): `x < y` false: `+0x50` = (1, 0, 0, 1), `+0x88` = 2 − 0.5 + 0.35 = 1.85.
   - **base (−1.5, 0.1, 0)**: `x < y` true: `+0x50` = (0, 1, 0, 1), `+0x88` = 1 − 0.1 + 0.35 = 1.25. Pins the
     face-choice quirk.
   - base (0, −1.2, 50): the pair function still hits (height ignored); a test through the whole query must miss
     (the gather box rejects it).
6. **BoxCapsule.** Same as the first CapsuleBox case with the box querying: hit; `+0x10` = (0, −1, 0, 1); `+0x88` =
   0.15; `+0x50` is whatever Coney defines (the original leaves it undefined).
7. **SegmentSphere.** Segment (0, 0, 0)-(0.4, 0, 0) r 0.1: sphere r 0.15 at (0.5, 0, 0): hit (distance 0.1 to the
   end). Sphere r 0.15 at (0.2, 0.3, 0): distance² 0.09 > 0.0625: miss; r 0.25: hit. Zero-length segment at the origin
   r 0.1 against sphere r 0.1 at (0.2, 0, 0): hit (touching).
8. **CapsuleSphere (rounded ends).** Capsule base (0, 0, 0), h 2.0, r 0.35:
   - sphere r 0.1 at (0, 0, 2.3): closest (0, 0, 2), distance² 0.09 ≤ 0.2025: hit (a flat-ended cylinder would miss).
   - sphere r 0.1 at (0, 0, −0.3): hit (below the base).
   - sphere r 0.1 at (0.46, 0, 1): distance 0.46 > 0.45: miss.
   - push sphere r 2.0 at (1.5, 0, 1): hit.
9. **SegmentCapsule / CapsuleSegment.** Capsule base (0, 0, 0), h 2.0, r 0.35; segment (0.3, −0.5, 2.2)-(0.3, 0.5,
   2.2) r 0.07: s = 0.5, t = 1, closest (0.3, 0, 2.2) and (0, 0, 2), distance² 0.13 ≤ 0.1764: hit. Raise the segment
   to z 2.4: distance² 0.25 > 0.1764: miss. CapsuleSegment with the arguments swapped: same.
10. **BoxBox.** Both half (1, 1, 1): A at the origin, B at (1.9, 0, 0): hit; at (2.0, 0, 0): hit (touching); at
    (2.1, 0, 0): miss. **Quirk**: A at (100, 0, 0), B at (101.5, 0, 0): the original's matrix gives a miss although
    they overlap (see the function).
11. **Segment never reaches a box.** Overlap table (4, 1), (1, 4): no entry, no contact; the strike step's sphere
    shapes are the only ones that can damage a car (an integration test for issue #72: a hand sphere overlapping box 1
    hits; a forearm segment through the same box does not).
12. **Query order.** Two bodies both overlapped by one push sphere: both pushed, whatever their order; a contact list
    built by Coney's overlap query is in discovery order.

## Unit 5: A sphere swept against a box: strikes on cars and objects {#unit-5}

**What it is.** A sphere swept against an oriented box (sweep table entries (2, 1) and (1, 2)): the region of the
start centre, the face, edge and corner steps, and the contact a strike hands to `Strike_Contact` and `Car_OnHit`.
It answers which contact field holds the hit point: `+0x90` in the world, `+0x60` in the struck box's frame.

- **Coney:** `src/platform/play_level_mode.*` (`strikeObjects()`) and `src/world_objects/car_hits.*`: issue #72's
  step 1 ([Cars](cars.md#coney-fix)) is this unit plus the strike loop.
- **Tests:** [Test ideas](#u4b-test-ideas-synthetic-shapes-values-compu).

10 functions, 5728 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x003453f8` | `PhysicsSweep_SphereBox` | 200 |
| `0x003454c0` | `PhysicsSweep_BoxSphere` | 200 |
| `0x0034bbe8` | `Sweep_SphereOrientedBox` | 2116 |
| `0x0034baf0` | `SphereBox_ToBoxSpace` | 248 |
| `0x0034b040` | `SphereBox_SweepCorner` | 884 |
| `0x0034b3b8` | `SphereBox_SweepEdge` | 584 |
| `0x0034b600` | `SphereBox_SweepEdges` | 432 |
| `0x0034b7b0` | `SphereBox_SweepFaceEdges` | 828 |
| `0x0034af50` | `Ray_SphereRoot` | 136 |
| `0x0034afd8` | `Ray_CircleRoot` | 100 |

### Summary for the implementer {#u4b-summary-for-the-implementer}

- **The answer on `+0x60` vs `+0x90`.** Both hold the same contact point, in two frames. `+0x90` is the point in
  **world** space (w = 1). `+0x60` is the point in the **struck box's own frame**: coordinates along the box's axes
  A0, A1, A2 (shape `+0x50`, `+0x60`, `+0x70`), measured from the **box's centre** (shape `+0x80`), not from the car's
  origin. `Car_OnHit` (`0x0038bea0`) reads `+0x60` x and y for its zone bands (hit kinds 0, 1, 2), `+0x90` for the
  rock (`Car_StartHitRock`), the dent sound and the window-break effect, and `+0x44` for which box was struck.
  Confirmed (code) at `0x003453f8` (writes), `0x0034c27c`-`0x0034c3e8` (how they are made), `0x0021b764` (Strike_Contact
  passes this very record to `Car_OnHit` in `a1`) and `0x0038bea0` (reads). The cabin box's offset (y +0.281 on the
  sedan) is already noted as confirmed (runtime) in the Ghidra comment in `Car_OnHit`.
- **The whole contact a sphere-box sweep writes** (`PhysicsSweep_SphereBox`, sphere moving, box = second half):

  | Offset | Value | Frame |
  | --- | --- | --- |
  | `+0x10` | unit vector from the sphere's **start** centre to the box centre, **horizontal** (z set to 0, then normalised), w = 1 | world |
  | `+0x50` | the box's outward unit normal of the **feature region the sphere's start centre is in** (face, edge or corner; table below), w = 0 | world |
  | `+0x60` | the contact point (x, y, z); w not written (stack leftover) | box frame |
  | `+0x70` | feature type: 0 face, 2 edge, 1 corner, 4 start centre inside the box | |
  | `+0x74` | feature index (tables below) | |
  | `+0x80` | fraction of the move at first contact, 0 to 1 | |
  | `+0x90` | the contact point, w = 1 | world |
  | `+0xb0` | 0 | |

  Nothing else is written; `+0x00`/`+0x04`/`+0x40`/`+0x44` are set by `Human_TestStrikes` after a hit, and every
  other byte is whatever the caller's stack held. Confirmed (code) at `0x003453f8`.
- **What counts as a hit** (confirmed (code) at `0x0034bbe8` and its steps):
  1. The sphere's **centre** at the start of the move inside the box or on its surface (each |local coordinate| ≤
     its half extent): a hit at fraction 0, whatever the move (also a zero move).
  2. Otherwise the swept sphere must reach the box during the move (fraction ≤ 1.0, touching counts).
  3. **A sphere that already overlaps or touches the box at the start, with its centre outside, never hits** (each
     step returns −1, which the caller treats as no contact). For a strike this means a hand that starts an update
     within its radius of the car gets no contact that update; it hits the next update only if its centre is then
     inside the box (case 1).
  4. A zero move hits only in case 1.
- **Normals are chosen by the start region, not by the feature actually touched**: a sphere starting in a face's
  region that clips that face's edge still gets the face normal. And **six of the 26 region normals are wrong in the
  original's tables** (the edges parallel to y and four of the eight corners): see
  [the normal tables](#the-normal-tables). No strike consumer reads `+0x50` (Strike_Contact uses `+0x50` only for a
  level-mesh hit, which does not come from this function), so for strikes this is harmless; Coney should reproduce
  the table as is and pin it with a test.
- **The corner region's point is the sphere centre**, not the corner: when the start centre is outside all three
  slabs and the first contact is the corner itself, `+0x60`/`+0x90` hold the sphere's centre at contact (0.09 m
  away from the box for a hand). Every other case gives a point on the box surface. Confirmed (code) at `0x0034b984`.
- **The inside case writes the world z in `+0x60`.** Its `+0x60` is (local x, local y, **world** z of the sphere
  centre), and `+0x90` is the sphere's start centre. Confirmed (code) at `0x0034beb0`.

### Records and conventions {#u4b-records-and-conventions}

**Box record** (shape type 1, from [Shapes](physics.md#shapes)): `+0x40`, `+0x44`, `+0x48` half extents h0, h1, h2;
four 16-byte rows `+0x50` A0, `+0x60` A1, `+0x70` A2 (unit axes), `+0x80` P (centre, w = 1). The kernel reads the
rows as a matrix whose rows 0-2 are the axes and row 3 the centre. **Sphere record** (type 2): `+0x40` radius r,
`+0x50` centre C. Confirmed (code) at `0x003453f8`.

**Local coordinates** used below: d = ((C − P)·A0, (C − P)·A1, (C − P)·A2) for the start centre and m = (move − boxMove)
projected the same way (dot products of xyz only). Confirmed (code) at `0x0034bc44`-`0x0034bd58`.

**Folding.** Each axis i with d_i < 0 (strictly) is flipped: d_i and m_i are negated and the axis's sign s_i is −1
(else +1). All steps then work with d_i ≥ 0, so "the face" on an axis is always the positive one; the sign is put
back at the end. Confirmed (code) at `0x0034bcb8`-`0x0034bdac`.

**Region** of the start centre, by folded d against the half extents, inclusive (`≤` means within the slab):

| x within | y within | z within | Region | Step called | Its axes (u, v, w) or (P, Q, R) |
| --- | --- | --- | --- | --- | --- |
| yes | yes | yes | inside | none | |
| yes | yes | no | face z | FaceRegion | (x, y, z) |
| yes | no | yes | face y | FaceRegion | (x, z, y) |
| no | yes | yes | face x | FaceRegion | (y, z, x) |
| yes | no | no | edge along x | EdgeRegion | P = y, Q = x, R = z |
| no | yes | no | edge along y | EdgeRegion | P = x, Q = y, R = z |
| no | no | yes | edge along z | EdgeRegion | P = x, Q = z, R = y |
| no | no | no | corner | CornerRegion | (x, y, z) |

Confirmed (code) at `0x0034bdac`-`0x0034c1f0`. The radius plays no part in the region choice.

**Step results**: 1 hit (fraction and folded local point written), 0 no hit, −1 the sphere already touches (fraction
0 written, no hit). Only 1 counts.

### The functions {#u4b-the-functions}

### PhysicsSweep_SphereBox (0x003453f8) {#u4b-physicssweep-spherebox-003453f8}

Size 0xc8. Sweep table entry (2, 1). `bool fn(Contact* c, Body* own, Shape* sphere, Body* other, Shape* box, const
Vec4* move)`; the bodies are not read. Calls `Sweep_SphereOrientedBox(r = sphere+0x40, maxFrac = 1.0 (0x3f800000),
rows = box+0x50, half = box+0x40, boxMove = (0, 0, 0, 1) at 0x005116c0, C = sphere+0x50, sphereMove = move, ...)`.
On a hit it copies the outputs into the contact as the summary table says (`+0x80` fraction, `+0x74` index, `+0x90`
world point, `+0x60` local point, `+0x50` feature normal, `+0x10` direction to the box, `+0x70` type) and clears
`+0xb0`; returns 1. On no hit it writes nothing and returns 0. Confirmed (code) at `0x003453f8`.

### PhysicsSweep_BoxSphere (0x003454c0) {#u4b-physicssweep-boxsphere-003454c0}

Size 0xc8. Sweep table entry (1, 2): the box moves, the sphere is the other shape. Same kernel with the roles
swapped: `r` and `C` from the other (sphere) shape, the rows and half extents from the moving box, `boxMove = move`
and `sphereMove = (0, 0, 0, 1)`, so the kernel sees the sphere moving by −move relative to the box. Writes the same
values with the halves swapped: `+0x80` fraction, `+0x34` index, `+0x30` type, `+0x90` world point, `+0x20` local
point, `+0x10` the box's feature normal, `+0x50` the horizontal direction from the sphere to the box centre, `+0xb0`
= 0. Confirmed (code) at `0x003454c0`.

The box's frame is its pose at the **start** of the move, so the world point `+0x90` is where the contact is on the
box's start pose, short of the true contact by fraction × move. Inferred from the kernel taking the box rows as
given. Not used by strikes (a strike sphere is always the moving shape).

### Sweep_SphereOrientedBox (0x0034bbe8) {#u4b-sweep-sphereorientedbox-0034bbe8}

Size 0x844. Arguments (EE ABI, float registers then integer registers then stack): `f12` r, `f13` maxFrac, `a0` box
rows (A0, A1, A2, P), `a1` half extents, `a2` box move, `a3` sphere start centre C, `t0` sphere move, `t1` out
fraction, `t2` out world point, `t3` out local point, stack: out feature normal (world), out direction to box, out
feature type, out feature index. Returns 1 on a hit, 0 otherwise. Confirmed (code) at `0x0034bbe8`.

1. Local start d and relative move m, folded; region (above).
2. **Inside**: fraction 0; type 4; index 0; world point = C (the start centre, unchanged w); local point =
   (clamp((C·A0 − P·A0), −h0, h0), clamp((C·A1 − P·A1), −h1, h1), **C.z**) (signed, not folded; the third component is
   the world z, a quirk); feature normal n = normalise((C − P) with z set to 0), w = 1; direction to box = −n (w
   kept 1). Returns 1 without testing maxFrac. Confirmed (code) at `0x0034bddc`-`0x0034bf48`.
3. Otherwise the region's step (with the axis order of the table and the flag t0 = 1) gives a result, a fraction and
   a folded local point (three floats in the step's axis order, mapped back to x, y, z). The type and index are
   written before the step runs (face 0, edge 2, corner 1; index from the signs, below).
4. If the result is not 1, or fraction > maxFrac (strict: fraction = maxFrac hits), return 0. Confirmed (code) at
   `0x0034c244`-`0x0034c264`.
5. On a hit: feature normal = `SphereBox_ToBoxSpace`(type, index); direction to box = normalise((P − C) with z set
   to 0), w = 1; local point = (s0·x, s1·y, s2·z) (unfold); world point = P + A0·lx + A1·ly + A2·lz (w = 1). Return 1.
   Confirmed (code) at `0x0034c26c`-`0x0034c3f4`.

Edge case: a sphere whose start centre is straight above or below the box centre (horizontal distance 0) normalises
a zero vector for the direction to the box (VU `rsqrt` of 0; the result is not meaningful). Inferred; Coney should
write (0, 0, 0, 1) there and note it.

**Feature index** (signs s0, s1, s2 of x, y, z after folding). Confirmed (code) at `0x0034bf88`-`0x0034c240`:

| Region | Index |
| --- | --- |
| face x | 1 if s0 > 0, else 3 |
| face y | 0 if s1 > 0, else 2 |
| face z | 4 if s2 > 0, else 5 |
| edge along z (x, y outside) | (s0, s1): (−, +) 0, (+, +) 1, (+, −) 2, (−, −) 3 |
| edge along y (x, z outside) | (s0, s2): (−, −) 4, (−, +) 5, (+, −) 6, (+, +) 7 |
| edge along x (y, z outside) | (s1, s2): (+, +) 8, (+, −) 9, (−, −) 10, (−, +) 11 |
| corner | (s0, s1, s2): (−, +, +) 0, (+, +, +) 1, (+, +, −) 2, (−, +, −) 3, (−, −, +) 4, (+, −, +) 5, (+, −, −) 6, (−, −, −) 7 |

### SphereBox_ToBoxSpace (0x0034baf0) {#u4b-spherebox-toboxspace-0034baf0}

Size 0xf8. `void fn(Vec4* out, const Mat4* rows, int type, int index)`. Picks a 16-byte local normal: type 0 from the
face table `0x00512930`, type 2 from the edge table `0x00512990`, type 1 from the corner table `0x00512a50` (each
entry 16 bytes, index × 16), any other type the constant (0, 1, 0, 1) at `0x00511730`; then
out = A0·nx + A1·ny + A2·nz + (0, 0, 0, 1)·nw (all four lanes), so it turns a box-frame normal into world space. The
name (from an earlier pass) is the wrong way round: it goes from box space to world. Confirmed (code) at
`0x0034baf0`; tables read from the executable.

### The normal tables {#the-normal-tables}

a = 0.70710677 (`0x3f3504f4`), b = 0.57735026 (`0x3f13b646`); all w = 0.

| Index | Face table `0x00512930` | Edge table `0x00512990` | Corner table `0x00512a50` |
| --- | --- | --- | --- |
| 0 | (0, 1, 0) | (−a, a, 0) | (−b, b, b) |
| 1 | (1, 0, 0) | (a, a, 0) | (−b, b, −b) |
| 2 | (0, −1, 0) | (a, −a, 0) | (b, b, −b) |
| 3 | (−1, 0, 0) | (−a, −a, 0) | (b, b, b) |
| 4 | (0, 0, 1) | (a, 0, a) | (−b, −b, b) |
| 5 | (0, 0, −1) | (a, 0, −a) | (−b, −b, −b) |
| 6 | | (−a, 0, −a) | (b, −b, −b) |
| 7 | | (−a, 0, a) | (b, −b, b) |
| 8-11 | | (0, a, a), (0, a, −a), (0, −a, −a), (0, −a, a) | |

Joined with the index table above, the faces, the edges along z, the edges along x and corners 0, 2, 4, 6 give the
true outward normal. **The edges along y and corners 1, 3, 5, 7 do not**:

| Start region (signs) | Index | Normal written (box frame) | True outward normal |
| --- | --- | --- | --- |
| edge along y, (s0, s2) = (+, +) | 7 | (−a, 0, a) | (a, 0, a) |
| edge along y, (+, −) | 6 | (−a, 0, −a) | (a, 0, −a) |
| edge along y, (−, +) | 5 | (a, 0, −a) | (−a, 0, a) |
| edge along y, (−, −) | 4 | (a, 0, a) | (−a, 0, −a) |
| corner (+, +, +) | 1 | (−b, b, −b) | (b, b, b) |
| corner (−, +, −) | 3 | (b, b, b) | (−b, b, −b) |
| corner (+, −, +) | 5 | (−b, −b, −b) | (b, −b, b) |
| corner (−, −, −) | 7 | (b, −b, b) | (−b, −b, −b) |

Confirmed (code) at `0x0034c118`-`0x0034c240` (index choice) with the tables read from the executable. Whether
anything visible depends on it is not known: no strike consumer reads `+0x50`; the camera's tilt (the kernel's other
caller) might. Coney should keep the original's tables (faithful first) behind a comment naming the mismatch.

### SphereBox_SweepCorner (0x0034b040): the face-region step {#u4b-spherebox-sweepcorner-0034b040-the-face}

Size 0x374. The name is from an earlier pass; the function handles a **face region** and reaches the corner only as
its last case. Arguments: half extents (a, b, c) for (u, v, w); folded start (u, v, w) with u ≤ a, v ≤ b (when called
from the main kernel; from EdgeRegion u or v may lie outside) and w > c; move (mu, mv, mw); r on the stack; out
fraction and out (u, v, w); flag t0. Confirmed (code) at `0x0034b040`:

1. If t0 ≠ 0 and w ≤ c + r: fraction 0, return −1 (already touching the face).
2. If mw ≥ 0: return 0 (not moving towards the face plane).
3. Let g = w − c (> r here when t0 = 1). For u, pick the edge the move heads for: if mu ≥ 0, su = +1 and
   ku = mu·g − mw·(u − a); else su = −1 and ku = mw·(u + a) − mu·g. The same for v with b: sv, kv. (ku / |mw| is how
   far past the edge at su·a the centre is when it reaches the plane w = c + r, plus r·|mu|/|mw|; so the centre is
   still over the face at that moment exactly when ku ≤ r·|mu|.)
4. **Face**: if ku ≤ r·|mu| and kv ≤ r·|mv|: fraction = (r − g) / mw; point = (u + fraction·mu, v + fraction·mv, c).
   Return 1.
5. **u edge**: if ku > r·|mu| and kv ≤ r·|mv|: if ku² > r²·(mu² + mw²) return 0 (the path passes the edge farther
   than r). Else fraction = Ray_CircleRoot(q = (u − su·a, g), m = (mu, mw), A = mu² + mw², r²); point = (su·a,
   v + fraction·mv, c). Return 1.
6. **v edge**: ku ≤ r·|mu| and kv > r·|mv|: as 5 with v, b, mv: if kv² > r²·(mv² + mw²) return 0; fraction =
   Ray_CircleRoot((v − sv·b, g), (mv, mw), mv² + mw², r²); point = (u + fraction·mu, sv·b, c). Return 1.
7. **Corner**: both beyond. If ku² > r²·(mu² + mw²) or kv² > r²·(mv² + mw²) return 0. With q = (u − su·a, v − sv·b,
   g) and m = (mu, mv, mw): if |q × m|² > r²·|m|² return 0 (the line misses the corner sphere); else fraction =
   Ray_SphereRoot(q, m, r²); point = (su·a, sv·b, c), the corner. Return 1.

The edge in 5 and 6 is an infinite line: the point's other coordinate (v + fraction·mv, or u + fraction·mu) is not
clamped to the face. Comparisons are exactly as written (`<` vs `≤`): step 4 takes the face when ku equals r·|mu|.
When the path is beyond both edges but misses the corner sphere, the function returns 0 without trying the two edge
cylinders (it can miss a sphere that would graze an edge near the corner). Confirmed (code); the geometric readings
are inferred from the algebra.

### SphereBox_SweepEdges (0x0034b600): the edge-region step {#u4b-spherebox-sweepedges-0034b600-the-edge-r}

Size 0x1b0. Arguments: half extents (a, b, c) for (P, Q, R); folded start (p, q, s) with p > a, s > c (Q is the axis
along the edge); move (mp, mq, ms); r on the stack; out fraction, out (P, Q, R); flag t0. Confirmed (code) at
`0x0034b600`. Let ep = p − a, es = s − c.

1. If t0 ≠ 0 and ep² + es² − r² ≤ 0: fraction 0, return −1 (already within r of the edge line).
2. If mp·ep + ms·es ≥ 0: return 0 (not approaching the edge line in the P-R plane).
3. k = ms·ep − mp·es. (k / |mp| is how far above the R face plane, s − c, the centre is when it crosses p = a; −k / |ms|
   is how far out along P it is when it crosses s = c.)
4. If k ≥ 0: if mp ≥ 0 return 0. If k ≤ r·|mp|: the **edge** (SweepEdge, axis 1 = R with (c, es, ms), axis 2 = P
   with (a, ep, mp)). Else the **R face**: FaceRegion with (u, v, w) = (P, Q, R), t0 = 0.
5. If k < 0: if ms ≥ 0 return 0. If k ≥ r·ms (that is |k| ≤ r·|ms|): the **edge** (SweepEdge, axis 1 = P with
   (a, ep, mp), axis 2 = R with (c, es, ms)). Else the **P face**: FaceRegion with (u, v, w) = (R, Q, P), t0 = 0.

Returns what the step it calls returns. Confirmed (code).

### SphereBox_SweepEdge (0x0034b3b8) {#u4b-spherebox-sweepedge-0034b3b8}

Size 0x248. One edge, its line along Q at (h1 on axis 1, h2 on axis 2). Arguments: q (coordinate along the edge),
h1, b (half extent along Q), h2, e1 = (coordinate − h1) on axis 1, e2 on axis 2, m1, mq, m2 on the stack, r on the
stack; out fraction, out axis 1, out Q, out axis 2. Confirmed (code) at `0x0034b3b8`:

1. Pick the edge's end the move heads for: if mq ≥ 0, sq = +1, f = q − b, else sq = −1, f = q + b;
   j1 = sq·(e1·mq − f·m1), j2 = sq·(e2·mq − f·m2) (the offsets from the edge line, times |mq|, at the moment the
   centre crosses the end plane).
2. **End corner**: if j1 ≥ 0 and j2 ≥ 0 and r²·mq² < j1² + j2² (the centre leaves past the end still farther than r
   from the line): with qv = (e1, f, e2), m = (m1, mq, m2): if |qv × m|² > r²·|m|² return 0; else fraction =
   Ray_SphereRoot(qv, m, r²), point = (h1, sq·b, h2). Return 1.
3. **Cylinder** otherwise: fraction = Ray_CircleRoot((e1, e2), (m1, m2), m1² + m2², r²); point = (h1, q + fraction·mq,
   h2) (not clamped to the edge's length). Return 1.

There is no separate "line misses the cylinder" test here: the callers only come here when the P-R path passes
within r of the edge (inferred from the conditions in EdgeRegion step 4/5, which bound |k| / |m_PR| by r).

### SphereBox_SweepFaceEdges (0x0034b7b0): the corner-region step {#u4b-spherebox-sweepfaceedges-0034b7b0-the-co}

Size 0x33c. The name is from an earlier pass; it handles the **corner region** (start centre outside all three
slabs). Arguments: half extents (hx, hy, hz), folded start (dx, dy, dz), move (mx, my, mz) with mz on the stack, r on
the stack; out fraction and out (x, y, z). No t0 flag. Let e = (dx − hx, dy − hy, dz − hz). Confirmed (code) at
`0x0034b7b0`:

1. If |e|² − r² ≤ 0: fraction 0, return −1 (already within r of the corner).
2. If m·e ≥ 0: return 0.
3. g2 = mx·ez − mz·ex, g3 = my·ez − mz·ey, g4 = my·ex − mx·ey. (When the centre crosses the plane of one axis, these
   give its offsets from the corner on the other two, times that axis's move.)
4. **Corner sphere** if any of, in this order: (A) g2 < 0, g4 ≥ 0 and g2² + g4² ≤ r²·mx²; (B) g4 < 0, g3 < 0 and
   g3² + g4² ≤ r²·my²; (C) g2 ≥ 0, g3 ≥ 0 and g2² + g3² ≤ r²·mz². Then fraction = Ray_SphereRoot(e, m, r²) and the
   point = **(dx + fraction·mx, dy + fraction·my, dz + fraction·mz), the sphere's centre at contact** (not the
   corner). Return 1. Confirmed (code) at `0x0034b974`-`0x0034b9b8`.
5. Otherwise one of the three edges through the corner, by EdgeRegion with t0 = 0: if g2 < 0 and g4 ≥ 0, the edge
   along x (P = y, Q = x, R = z); else if g4 < 0 and g3 < 0, the edge along y (P = x, Q = y, R = z); else the edge
   along z (P = x, Q = z, R = y). Returns its result.

The corner sphere test has no "line misses the sphere" check of its own: (A)-(C) bound the crossing point's distance
from the corner by r, which implies it (inferred).

### Ray_SphereRoot (0x0034af50) and Ray_CircleRoot (0x0034afd8) {#u4b-ray-sphereroot-0034af50-and-ray-circlero}

Sizes 0x88 and 0x64. `float Ray_SphereRoot(q.x, q.y, q.z, m.x, m.y, m.z, rr)` (all in `f12`-`f18`);
`float Ray_CircleRoot(q.x, q.y, m.x, m.y, A, rr)` (A = |m|² passed in). Both give the smaller root of
|q + t·m|² = rr as

```text
C = |q|² − rr;  B = q·m;  A = |m|²;  D = sqrt(|B² − A·C|);  s = 1 / D
t = (C·s) / (1 − B·s)          // = C / (D − B), the stable form of (−B − D) / A
```

Confirmed (code) at `0x0034af50` and `0x0034afd8`. Note the absolute value under the root (a negative discriminant is
not rejected here; the callers screen misses first) and that D = 0 (exact tangency) divides by zero (on the PS2 this
saturates rather than giving inf/NaN). Coney may compute C / (D − B) directly; the two differ only in float rounding
and at D = 0, where C / (D − B) gives the exact tangent root.

### How Human_TestStrikes builds the sweep (0x0033f110) {#u4b-how-human-teststrikes-builds-the-sweep-0}

Confirmed (code) at `0x0033f110` unless marked; the target filters are already on
[combat.md#moving-strikes](combat.md#moving-strikes).

- **Which bodies.** For a candidate body that is not a human (type mask `+0x44` without `0x40`), the striking shapes
  are tried only when the candidate's flags `+0x40` have `0x10` (MELEETARGET) or `0x20` (CHARGETARGET) and the
  striker's owner accepts the other owner (owner vtable slot `+0x108`/`+0x10c`). A car's body flags `0x2211a` include
  `0x10`. The candidate's shapes come from its body vtable `+0x2c` called with (2, 4, striker's owner); every shape of
  that chain is tried, **with no flag test** (a human target's chain is filtered by flag `0x4` instead).
- **Which strike shapes.** The human body's strike chain (`+0x80`), in chain order, those with flags `0x1` (enabled)
  and `0x2`.
- **The move.** For each such shape: `cur` = its world position (shape vtable `+0x24`; a sphere returns its centre
  `+0x50`), `prev` = shape `+0x20`; the shape's position is set to `prev` (vtable `+0x0c`; a sphere's setter
  `0x004f1200` writes `+0x50` only); `move` = cur − prev (xyz; w = 1). Then for each candidate shape,
  `sweep[strike.type][other.type](&contact, strikerBody, strikeShape, candidateBody, candidateShape, &move)`
  (IPhysics `+0x7c` + strike.type × 0x14 + other.type × 4; an empty entry is skipped). The shape's position is put
  back to `cur` (vtable `+0x0c`) after its candidates are done, hit or not.
- **A hit ends that candidate's shapes, not the test.** On the first nonzero result for a candidate: contact `+0x00` =
  striker's owner (body `+0x34`), `+0x04` = the strike shape, `+0x40` = the candidate's owner, `+0x44` = the candidate
  shape; then the striker owner's `Strike_Contact` (vtable `+0x104`) with the contact; then, if the candidate has an
  owner, the striker body's vtable `+0x44` (put it on the struck list); no further shape is tried against that body, but
  the loop goes on with the next candidate (`0x0033fb04`, `0x0033fc94`, `0x0033fe3c`), so one update can strike several
  bodies ([Unit 10](physics-bodies.md#u10-strikes)). The contact is a single stack record reused for every try, never
  cleared: fields the
  sweep does not write hold leftovers of earlier tries.
- **Where `prev` comes from** (`PhysicsSphere_Pose` `0x00342b08`, the sphere's vtable `+0x28` slot): each pose first
  copies the current centre to `+0x20` and then computes the new centre, unless flag `0x10` is set, in which case the
  new centre is copied to `+0x20` after it is computed (so prev = cur), and `0x10` is cleared. The shape's enable
  (vtable `+0x1c` slot, `0x004f11c8`) sets flags `0x11` when switching on (enabled + reset) and clears `0x1` when
  switching off. So **the first test after a strike shape is switched on has a zero move**, and afterwards the move
  is from the centre at the previous pose to the current one. Confirmed (code) at `0x00342b08` and `0x004f11c8`; that
  a human body is posed once per update before `Human_TestStrikes` is inferred (combat.md places the test after the
  move).
- **A held weapon's spheres** (body `+0xc4` chain) are tried only against candidates whose flags `+0x40` have `0x8`
  (WEAPONTARGET; cars have it), with the same move and sweep; their hit goes to the **held object's** contact
  handler (vtable `+0xfc` slot) with `+0x00` = the held object, not to `Strike_Contact`; an answer of 5 means "try
  the next shape". How that handler reaches `Car_OnHit` (kind 1) is not traced here.

So for a hand, head or foot sphere against a car box, `PhysicsSweep_SphereBox` sweeps the sphere from its previous
centre `prev` to `cur` with the box at rest (box move (0, 0, 0)), and the contact Strike_Contact gets is the one in
the summary.

### What Strike_Contact and Car_OnHit read (0x0021b290, 0x0038bea0) {#u4b-what-strike-contact-and-car-onhit-read-0}

Confirmed (code) at the addresses:

- `Strike_Contact(human, contact)`: for a car (`Car_FromObject(contact+0x40)` nonzero) it calls the car's vtable
  `+0x104` (`Car_OnHit`) with **the same contact** (`a1` = the record, `0x0021b764`). For another object it sends
  message 1 to `+0x40` with the point `+0x90` and the direction `+0x10`. For the level (`+0x40` = 0) it spawns a
  particle at `+0x90` along `+0x50` (a mesh contact, not this kernel).
- `Car_OnHit(car, contact)`: `+0x00` the striker (human or object, which picks the kind); `+0x60` x and y, the
  point in the struck box's frame, for the band lookups of kinds 2, 1 and 0 (kind 3, a human's own strike, takes
  its parts from `Car_GetHitZone` and the striker's feet, [cars.md](cars.md#hit-volumes)); `+0x44` the struck box
  (compared with the car's cabin box to pick the cabin table); `+0x90` the world point for `Car_StartHitRock`
  (rock direction from the car's position to the point), the dent sound and the window-break effect
  (`Car_BreakWindow` pushes `+0x90` for the sparks kinds).

For issue #72 step 1 (kind 3): the strike needs `+0x90` (rock, sound, effects) and `+0x44` (only for kinds other than
3); the zone comes from the feet, so `+0x60` matters only from step 3 on (kinds 2, 1, 0).

### Test ideas (synthetic shapes; values computed from this spec) {#u4b-test-ideas-synthetic-shapes-values-compu}

Box B: centre P = (0, 0, 0.5), axes identity, half (2, 1, 0.5) (so its faces are x = ±2, y = ±1, z = 0 and 1).
Hand sphere r = 0.09. "prev" is the sphere's start centre, "move" = cur − prev. Fractions to 4 decimals.

1. **Face.** prev (0, −1.3, 0.5), move (0, 0.4, 0): face y region, s1 = −1. Hit, fraction 0.525 (= (0.3 − 0.09) /
   0.4); `+0x60` = (0, −1, 0); `+0x90` = (0, −1, 0.5, 1); `+0x50` = (0, −1, 0, 0); `+0x10` = (0, 1, 0, 1); type 0,
   index 2.
2. **0.1 m short.** prev (0, −1.3, 0.5), move (0, 0.11, 0) (ends with the hand's surface 0.1 m from the face): the
   fraction would be 0.21 / 0.11 = 1.909 > 1: no hit, contact untouched.
3. **Face region, clipping the face's edge.** prev (1.98, −1.3, 0.5), move (0.1, 0.4, 0): face y region; ku = 0.022 >
   r·|mu| = 0.009, ku² = 0.000484 ≤ r²·(0.01 + 0.16) = 0.001377: the edge at x = 2. Hit, fraction 0.5419;
   `+0x60` = (2, −1, 0); `+0x90` = (2, −1, 0.5); `+0x50` = (0, −1, 0, 0) (**the face normal**, though the edge was
   touched); `+0x10` = (−0.8359, 0.5488, 0, 1); type 0, index 2.
4. **Glancing miss past an edge.** prev (1.98, −1.3, 0.5), move (0.2, 0.4, 0): ku = 0.052 > 0.018 and ku² = 0.002704
   > r²·0.2 = 0.00162: no hit (the path passes the edge 0.116 m away).
5. **Vertical edge (edge along z).** prev (2.3, −1.3, 0.5), move (−0.4, 0.4, 0): edge region, P = x, R = y, k = 0 →
   SweepEdge cylinder. Hit, fraction 0.5909 (= (0.3√2 − 0.09) / (0.4√2)); `+0x60` = (2, −1, 0); `+0x90` =
   (2, −1, 0.5); `+0x50` = (0.7071, −0.7071, 0, 0) (index 2, a correct entry); `+0x10` = (−0.8706, 0.4921, 0, 1);
   type 2.
6. **Edge along y, the wrong normal.** prev (2.3, 0, 1.3), move (−0.4, 0, −0.4): edge region (x, z outside), fraction
   0.5909; `+0x60` = (2, 0, 0.5); `+0x90` = (2, 0, 1.0); type 2, index 7, `+0x50` = (−0.7071, 0, 0.7071, 0) (the true
   outward normal would be (0.7071, 0, 0.7071)). `+0x10`: prev is 2.3 m from P horizontally along x: (−1, 0, 0, 1).
7. **Corner.** prev (2.3, −1.3, 1.3), move (−0.4, 0.4, −0.4): corner region, g2 = g3 = g4 = 0, case (C): corner
   sphere. Hit, fraction 0.6201 (= (0.3√3 − 0.09) / (0.4√3)); `+0x60` = (2.0520, −1.0520, 0.5520) and `+0x90` =
   (2.0520, −1.0520, 1.0520): **the sphere's centre**, 0.09 m off the corner (2, −1, 1); type 1, index 5,
   `+0x50` = (−0.5774, −0.5774, −0.5774, 0) (true: (0.5774, −0.5774, 0.5774)); `+0x10` = (−0.8706, 0.4921, 0, 1).
8. **Start inside.** prev (0.5, 0.2, 0.6), any move (also zero): hit, fraction 0, type 4, index 0; `+0x90` =
   (0.5, 0.2, 0.6); `+0x60` = (0.5, 0.2, **0.6**) (world z, the local z would be 0.1); `+0x50` = (0.9285, 0.3714, 0, 1);
   `+0x10` = (−0.9285, −0.3714, 0, 1).
9. **Start touching, centre outside.** prev (0, −1.05, 0.5), move (0, 0.1, 0) (the hand overlaps the face by 0.04):
   no hit (the face step returns −1). Next update prev (0, −0.95, 0.5): inside, hit at fraction 0 (case 8's rules).
10. **Zero move, outside.** prev = cur = (0, −1.3, 0.5): no hit. Zero move, centre inside: hit (case 8).
11. **Exactly reaching.** A sphere of r 0.25 (exact in binary), prev (0, −1.5, 0.5), move (0, 0.25, 0): fraction
    (0.25 − 0.5) / −0.25 = 1.0 exactly: a hit (only fraction > 1 is rejected). Move (0, 0.125, 0): fraction 2, no hit.
12. **Rotated box, the frame of `+0x60`.** Box centre (10, 5, 0.5), A0 = (0, 1, 0), A1 = (−1, 0, 0), A2 = (0, 0, 1),
    half (2, 1, 0.5). prev (11.3, 5, 0.5), move (−0.4, 0, 0): local start (0, −1.3, 0), local move (0, 0.4, 0); hit,
    fraction 0.525; `+0x60` = (0, −1, 0) (box frame); `+0x90` = (11, 5, 0.5); `+0x50` = (1, 0, 0, 0) (local (0, −1, 0)
    rotated); `+0x10` = (−1, 0, 0, 1).
13. **Mirror (BoxSphere).** Box B moving by (0, −0.4, 0) against a resting sphere at (0, −1.3, 0.5): fraction 0.525;
    `+0x30` type 0, `+0x34` index 2, `+0x20` = (0, −1, 0), `+0x90` = (0, −1, 0.5) (on the box's start pose; the true
    contact is at y −1.21), `+0x10` = (0, −1, 0, 0), `+0x50` = (0, 1, 0, 1).
14. **Strike integration (issue #72).** A hand sphere switched on this update (flag `0x11`): its first test has a zero
    move, so it hits a car box only if its centre is inside. On the next update, prev = the last centre: a hand moving
    from 0.15 m outside box 1's side to 0.05 m inside hits with the fraction of case 1's kind, and `Car_OnHit` gets
    `+0x60` in box 1's frame (for the cabin, box 2's frame, offset from box 1's).

## Unit 12: A capsule swept against a box {#unit-12}

**What it is.** An upright capsule swept against an oriented box (sweep table entries (3, 1) and (1, 3)): a walking or
jumping human's capsule against an object's box body, and a moving box against a human. It is a **2D test** in the
box's horizontal plane, a circle swept against a rectangle; no height or vertical range is read. Specified exactly
below from the disassembly (2026-10-12), confirmed (code) unless a line says otherwise; the fractions were checked
against a brute-force sampled sweep ([Test ideas](#u12-tests)).

- **Coney:** `src/physics/sweeps.*`; a walking human against an object's box body (today the stand-in push of
  `world_objects::ObjectBodies::pushOut`), and the mirror for a moving box against a human (with [Unit 13](#unit-13)).
- **Tests:** [Test ideas](#u12-tests): a face, a face's end, a corner, start overlaps, the over-the-box case, the
  quirks (start-centre normals, the opposite-corner normal, A0/A1 corner normals, grazes missed, negative fractions),
  the mirror; there is **no** vertical test to pin, the pair ignores heights.

4 functions, 4436 bytes (0 tagged). The two kernels' names were swapped in the shared Ghidra project; they were
renamed on 2026-10-12 from their call sites (`0x00344884`, `0x0034499c`), so the table and the
[kernels](physics.md#kernels) row now read: `0x0034a318` is the **corner** step, `0x0034a6d0` the **side** step.

| Address | Name | Size |
| --- | --- | --- |
| `0x00344618` | `PhysicsSweep_CapsuleBox` | 2800 |
| `0x00345108` | `PhysicsSweep_BoxCapsule` | 224 |
| `0x0034a318` | `CapsuleBox_SweepCorner` (was `CapsuleBox_SweepSide`) | 900 |
| `0x0034a6d0` | `CapsuleBox_SweepSide` (was `CapsuleBox_SweepCorner`) | 512 |

### Summary for the implementer {#u12-summary}

- **2D only.** The capsule's base x, y, its radius and the move's x, y decide everything; the capsule's height, its
  base z, the move's z and the box's extent along its vertical axis are never read for the test. Two shapes far apart
  in height collide when their footprints do; only the candidate gather (the swept body's AABB against the box body's
  AABB, [Sweeping a body](physics.md#sweep)) keeps them apart vertically. Confirmed (code) at `0x00344618` (the reads);
  the
  consequence is inferred.
- **Already touching counts as a hit**, at fraction 0, unlike the sphere-box sweep of [Unit 5](#unit-5): a kernel's
  −1 is non-zero and the caller only tests for 0 (`0x003448ac`, `0x00344fe0`). But a capsule that touches and moves
  **away** (or parallel) gets **no** contact, unless the kernels' flag is set ([below](#u12-flag)), so a human can
  walk out of a box it overlaps.
- **Over the box** (the base inside the footprint): always a hit at fraction 0, whatever the move, with an
  **unsigned** axis as the normal.
- **The contact** ([What the contact holds](#u12-contact)): `+0x80` fraction, `+0x50` normal (a face's outward axis,
  or for a corner the direction from the touch point to the capsule's **start** centre), `+0x90` the capsule's axis
  point at mid-height at contact (not a surface point), `+0x60` the touch point's two in-plane box coordinates with
  the world mid-height z, `+0xa0` a corner slide (the region's diagonal) with flag `+0xb0` = 1 for corner regions,
  `+0x10` the move's direction.
- **Quirks to keep** (each pinned by a test): the (−, −) corner's start-touching normal is built from the opposite
  corner, as in the overlap ([Unit 4](#u4-physicsoverlap-capsulebox-00346808)); corner normals use the box's axes A0
  and A1 whatever the in-plane axes are; a corner's normal is taken from the start centre, not the centre at contact;
  the corner step misses some grazes of its near corner; with the flag set a receding capsule can get a **negative**
  fraction.

### Records and calls {#u12-records}

`bool PhysicsSweep_CapsuleBox(Contact* c, Body* capsuleBody, Shape* capsule, Body* boxBody, Shape* box, const Vec4*
move)` (`a0`-`a3`, `t0`, `t1`, the sweep table's convention, [Unit 5](#u4b-physicssweep-spherebox-003453f8)).
Returns 1 on a hit, else 0 with nothing written. Confirmed (code) at `0x00344618`.

| Record | Fields read |
| --- | --- |
| capsule (type 3) | `+0x40` radius r; `+0x44` height h (only for the output z); `+0x50` base B (x, y used; z only for the output) |
| box (type 1) | `+0x40`, `+0x44`, `+0x48` half extents h0, h1, h2; `+0x50`, `+0x60`, `+0x70` axes A0, A1, A2 (x, y, z); `+0x80` centre P |
| capsule body | `+0x34` owner, which must be a human (`Object_AsHuman` `0x00229868` is called and its result used without a null test, `0x003446a0`; inferred safe: only human bodies hold capsules) |
| box body | `+0x40` flags, bit `0x4` (`BLOCKHUMANS`, [layers](physics.md#layers)) |
| owner | vtable slot `+0x50` / `+0x54`, the human's `0x004f4780`, which returns the object flags word `+0x54` (bit `0x4000000` airborne); state flag `0x2000000000` through `0x00227f40` (landing, [Characters](characters.md)) |

The box's own move is not an argument: the box is taken at rest at its current pose.

### The kernels' flag {#u12-flag}

Before the test (`0x0034468c`-`0x003446cc`): `flag = 0` if the box body's flags have `0x4` (`BLOCKHUMANS`); else
`flag = 1` if the owner's object flags have `0x4000000` (airborne), else `flag = (0x00227f40(owner) ≠ 0)`. The flag
goes to every kernel in `a3`. **Flag 0** makes the kernel return "no hit" at once when the capsule does not move
towards the face (side) or the corner point (corner); **flag 1** skips that screen. So the screen is off only for a
box without `BLOCKHUMANS` (one the human meets through `JUMPTARGET`, `CHARGETARGET` or `RUNTARGET`,
[layers](physics.md#layers)) while the human is airborne or landing. Confirmed (code); the reading of the bits is from
the
pages cited.

### PhysicsSweep_CapsuleBox (0x00344618): the regions {#u12-capsulebox}

1. **In-plane axes** (`0x003446d0`-`0x00344738`): `u` = the axis with the largest `|A_k.z|`, scanning k = 0, 1, 2
   with a strict `>` from −1 (the first wins a tie); `i = (u + 1) mod 3`, `j = (i + 1) mod 3`; `hx = h_i`,
   `hy = h_j`. The same choice as the overlap ([Unit 4](#u4-physicsoverlap-capsulebox-00346808)).
2. **Coordinates** (`0x00344740`-`0x0034485c`): `d = B − P` and `m = move`, each with z set to 0; `x = d·A_i`,
   `y = d·A_j`, `mx = m·A_i`, `my = m·A_j` (xyz dot products, VU0).
3. **Region** by x against ±hx and y against ±hy, strict outside (`x < −hx`, `x > hx`), inclusive inside. Each outer
   region calls a kernel on the coordinates **folded** so that the start lies left of (and for a corner, below) the
   rectangle, then unfolds the kernel's touch point `(px, py)` by negating what was negated:

| Region | Kernel and arguments `(x, y, r, mx, my, hx, hy)` | Unfold | Normal `+0x50` (w) | Slide `+0xa0` | `+0xb0` |
| --- | --- | --- | --- | --- | --- |
| `x < −hx`, `y < −hy` | Corner `(x, y, r, mx, my, hx, hy)` | | corner rule, `n0 = (x − hx, y − hy)` (**quirk**) | norm(−A_i − A_j) | 1 |
| `x < −hx`, `abs(y) ≤ hy` | Side `(x, y, r, mx, my, hx, hy)` | | −A_i (1) | none | 0 |
| `x < −hx`, `y > hy` | Corner `(x, −y, r, mx, −my, hx, hy)` | py | corner rule, `n0 = (x + hx, y − hy)` | norm(−A_i + A_j) | 1 |
| `abs(x) ≤ hx`, `y < −hy` | Side `(y, x, r, my, mx, hy, hx)`, outputs (py, px) | | −A_j (1) | none | 0 |
| `abs(x) ≤ hx`, `abs(y) ≤ hy` | none: a hit at fraction 0, `(px, py) = (0, 0)` | | A_j if `abs(x) < abs(y)`, else A_i, **unsigned** (1) | none | 0 |
| `abs(x) ≤ hx`, `y > hy` | Side `(−y, x, r, −my, mx, hy, hx)`, outputs (py, px) | py | +A_j (1) | none | 0 |
| `x > hx`, `y < −hy` | Corner `(−x, y, r, −mx, my, hx, hy)` | px | corner rule, `n0 = (x − hx, y + hy)` | norm(A_i − A_j) | 1 |
| `x > hx`, `abs(y) ≤ hy` | Side `(−x, y, r, −mx, my, hx, hy)` | px | +A_i (1) | none | 0 |
| `x > hx`, `y > hy` | Corner `(−x, −y, r, −mx, −my, hx, hy)` | px, py | corner rule, `n0 = (x − hx, y − hy)` | norm(A_i + A_j) | 1 |

"None" means the slide stays `(0, 0, 0, 1)` (`0x005116c0`, loaded at `0x00344670`). A kernel result of 0, or a
fraction above 1.0 (`0x3f800000`; `c.le`, so exactly 1.0 hits), is no hit. Confirmed (code) at
`0x0034485c`-`0x00344f34`.

**The corner rule** (`0x00344f38`-`0x00344fa4`): `(nx, ny) = (x − px, y − py)` when the fraction is > 0, else `n0`
from the table; then `n = normalize(nx·A0 + ny·A1)` (xyz; its w is the stack word at `sp+0x2c`, never set: undefined).
Three quirks, confirmed (code):

- `(x, y)` is the capsule's **start**, not its centre at contact, so the normal points from the touch point to where
  the capsule started (an oblique approach skews it; [test 5](#u12-tests)).
- **A0 and A1**, not `A_i` and `A_j`: right only when `u = 2` (the box's third axis is the most vertical, every box
  posed from an upright owner, inferred); a box lying with A0 vertical gets a corner normal with a z part.
- At fraction 0 (start touching, or the zero-discriminant guard below) the (−, −) corner uses `(x − hx, y − hy)`, the
  offset from the **opposite** corner; the other three use their own corner. The overlap test has the same slip.

The slide vectors use `A_i`, `A_j` correctly and are unit vectors (VU0 `vrsqrt`), w = 1.

**After the region** (`0x00344fa8`-`0x00344fdc`): the owner's object flags are read again; with `0x4000000`
(airborne) the slide is reset to `(0, 0, 0, 1)` (the flag `+0xb0` stays 1). So an airborne human never gets a
corner slide. Confirmed (code).

### CapsuleBox_SweepSide (0x0034a6d0) {#u12-side}

Size 0x200. `int fn(float x, float y, float r, float mx, float my, float hx, float hy, int flag, float* frac,
float* px, float* py)`: `f12`-`f18` then `a3`, `a0`-`a2`. The start is folded so `x < −hx`, `abs(y) ≤ hy`; the
target is the face `x = −hx` and its two end corners `(−hx, ±hy)`. Returns 1 hit, 0 none, −1 touching. Confirmed
(code) at `0x0034a6d0`. Let `g = x + hx` (< 0), `s = g + r`, `A = mx² + my²`.

1. If flag = 0 and `mx ≤ 0`: return 0 (not moving towards the face).
2. If `s ≥ 0` (within r of the face plane): `*frac = 0`, return −1 (`px`, `py` not written).
3. Pick the end the move heads for: if `my ≥ 0`, the end `+hy` and `k = mx·(y − hy) − my·g`; the miss test and the
   end test are `k ≥ 0 and k² > r²·A` → return 0, and `k > r·my` → the end corner. If `my < 0`, the end `−hy` and
   `k = mx·(y + hy) − my·g`; miss when `k ≤ 0 and k² > r²·A`; the end corner when `r·my > k`.
   (`k / mx` is how far past that end the centre is as it crosses `x = −hx`; `|k| / |m|` the line's distance from
   the corner, inferred from the algebra.)
4. **Face**: `*frac = −s / mx`; `px = −hx`, `py = y + frac·my` (not clamped to the face). Return 1.
5. **End corner** `(−hx, e)`: `*frac` = the smaller root of `|q + t·m|² = r²` with `q = (g, y − e)`; `px = −hx`,
   `py = e`. Return 1.

The root is computed inline as in `Ray_CircleRoot` ([Unit 5](#u4b-ray-sphereroot-0034af50-and-ray-circlero)):
`C = |q|² − r²`, `B = q·m`, `D = sqrt(abs(B² − A·C))`, `t = (C/D) / (1 − B/D)`. With flag 1 nothing stops a receding
path: the face branch is then reached only in degenerate cases, but the end corner's root is the **past** entry time,
a negative fraction, which the caller accepts (only > 1 is rejected); see [Edge cases](#u12-edge).

### CapsuleBox_SweepCorner (0x0034a318) {#u12-corner}

Size 0x384. Same signature. The start is folded into the (−, −) corner region, `x < −hx`, `y < −hy`; the target is
the whole rectangle as reached from there. Confirmed (code) at `0x0034a318`. Let `e = (x + hx, y + hy)` (both < 0),
`A = |m|²`, `B = m·e`, `C = |e|² − r²`, `k = mx·ey − my·ex`.

1. If flag = 0 and `B ≥ 0`: return 0 (not moving towards the corner point).
2. If `C ≤ 0` (within r of the corner): `*frac = 0`, return −1.
3. If `k ≥ 0` (the path crosses the line `y = −hy` left of the corner, by `k / my`):
    - `k ≤ r·my`: **near corner**: root of `q = e`; if `D` is exactly 0 the fraction is **0** (an explicit guard,
      `0x0034a3f0`); `(px, py) = (−hx, −hy)`. Return 1.
    - else, if `mx ≤ 0` return 0; `k2 = mx·(y − hy) − my·ex`; if `k2 ≥ 0 and k2² > r²·A` return 0; if `k2 ≤ r·my`,
      the **face `x = −hx`**: `frac = −(ex + r) / mx`, `(px, py) = (−hx, y + frac·my)`; else the **far corner
      `(−hx, +hy)`**: root of `q = (ex, y − hy)`, `(px, py) = (−hx, hy)`. Return 1.
4. If `k < 0` (it crosses `x = −hx` below the corner, by `−k / mx`):
    - `−k ≤ r·mx`: **near corner**, root of `q = e` (no zero guard here), `(px, py) = (−hx, −hy)`. Return 1.
    - else, if `my ≤ 0` return 0; `k2 = mx·ey − my·(x − hx)`; if `k2 ≤ 0 and k2² > r²·A` return 0; if
      `−k2 ≤ r·mx`, the **face `y = −hy`**: `frac = −(ey + r) / my`, `(px, py) = (x + frac·mx, −hy)`; else the
      **far corner `(+hx, −hy)`**: root of `q = (x − hx, ey)`, `(px, py) = (hx, −hy)`. Return 1.

The near-corner branches have no miss test of their own: `abs(k) ≤ r·abs(m_axis)` already puts the line within r of
the corner (inferred). **Grazes are missed**: when the path crosses the extended side line farther than r from the
corner but still passes within r of it, and does not move towards that side (`my ≤ 0` or `mx ≤ 0`), the step returns
0 although the circle touches the corner ([test 11](#u12-tests)). In 40,000 random sweeps (box B, r 0.3, flag 0) this
lost 17 of 1,124 corner-region contacts; every other hit matched the sampled sweep to 1e-4. Inferred from the model
of this page, checked by brute force.

### What the contact holds {#u12-contact}

Written on a hit only (`0x00345000`-`0x003450c8`), `f` = the fraction, `B` = capsule base, `M` = move:

| Offset | Value | Frame |
| --- | --- | --- |
| `+0x10` | `normalize(M)` in xyz (the full 3D move, `vrsqrt`), w = M.w (as the caller left it) | world |
| `+0x50` | the normal of the region table (faces and over the box w = 1; corners w undefined) | world |
| `+0x60` | `(px, py)`: the touch point's coordinates along `A_i`, `A_j` from the box centre (in the table's unfolded form; `(0, 0)` when the kernel returned −1 or over the box; negated zeros may be −0.0) | box plane |
| `+0x68` | `B.z + f·M.z + h/2`: the capsule's mid-height, **world** z (`0x0034505c`-`0x00345094`) | world |
| `+0x6c` | 1.0 | |
| `+0x80` | `f` (0 for a start overlap or over the box; ≤ 1; negative possible with flag 1) | |
| `+0x90` | `B + f·M` with z replaced by the mid-height above, w = 1: the capsule's axis point, **not** a point on the box | world |
| `+0xa0` | the slide of the region table (`(0, 0, 0, 1)` unless a corner region and not airborne) | world |
| `+0xb0` | 1 for the four corner regions, else 0 | |

Nothing else is written: `+0x00`/`+0x04`/`+0x40`/`+0x44` and `+0xb4` are set by the caller
(`PhysicsBody_SweepHumanMask` clears `+0xb4` and calls `Contact_InsertSorted`, [Unit 3](physics-bodies.md#u3-insert));
`+0x20`,
`+0x30`, `+0x70`-`+0x7c`, `+0x84`-`+0x8c` hold whatever the slot held. Confirmed (code).

What the resolution makes of it ([Unit 3](physics-bodies.md#u3-codes), inferred): flag `0x1` with a corner's diagonal
slide adds that
unit vector, less its part against the pass's direction, to the move velocity, which steers a walker around the box's
corner; a fraction-0 contact advances nothing and only removes the velocity's part into `+0x50`; over the box the
unsigned normal blocks only the part of the move along −A_i (or −A_j).

### PhysicsSweep_BoxCapsule (0x00345108) {#u12-boxcapsule}

Size 0xe0. Table (1, 3): the box moves. Calls `PhysicsSweep_CapsuleBox(c, capsuleBody, capsule, boxBody, box,
&(−M.x, −M.y, −M.z, M.w))`; on a hit swaps the contact's two 0x40-byte halves wholesale (`+0x00..+0x3f` ↔
`+0x40..+0x7f`; `+0x80` on untouched) and returns 1. Confirmed (code) at `0x00345108`. So:

- `+0x10` = the box's normal towards the capsule (what the capsule-box test put in `+0x50`);
- `+0x50` = `normalize(−M)`: the **reversed move direction**, not a surface normal; `+0x20` the in-plane point, `+0x28`
  the mid-height z, `+0x2c` 1.0;
- `+0x90` = the capsule's base **minus** `f·M` at mid-height (a point in the box's start frame, not a place in the
  world); `+0xa0`, `+0xb0` as in the table, with the airborne test applied to the **capsule's** owner.

Inferred consequence: the moving box's resolution uses `+0x50` = −move direction, so its advance is along its own
move and the slide removes its whole velocity along that direction; a corner slide (flag 1) points from the box towards
the capsule's quadrant.

### Who calls it {#u12-callers}

Only through the sweep table (IPhysics `+0x7c`, entries (3, 1) and (1, 3), filled by `IPhysics_Construct`,
`0x0033c7a0`); `PhysicsSweep_BoxCapsule` is the only direct caller of `0x00344618`. Confirmed (code), xrefs. The
table users that can pair a capsule with a box:

- `PhysicsBody_SweepHumanMask` (`0x0033d498`), from `PhysicsBody_Sweep` and the `0x801` update: a human's capsule
  (its first shape) against the candidate bodies whose flags meet the human's mask (`0x4`, or `0x44` while airborne
  or holding, plus `0x20`, `0x40000`, [Sweeping a body](physics.md#sweep)), every enabled shape of each; and a world
  object's
  box (mask `0x2`) against the humans' capsules (humans' bodies have `0x2`), the (1, 3) mirror. Confirmed (code).
- `IPhysics_CollideShape` (`0x0033e7f0`) with a temporary capsule: `Human_CapsuleOverlaps` (`0x0021c418`, radius
  0.2) and `Human_IsWayClearTo` (`0x0021c0a8`, radius 0.2) sweep it from the human to another human with options
  `0x2803` (bodies included); `Human_IsClearToTarget` (`0x0021c240`) uses option 1 (mesh only) and never reaches this
  pair. Confirmed (code) at the callers.
- Strikes (`Human_TestStrikes`) sweep spheres and segments, not capsules
  ([Unit 5](#u4b-how-human-teststrikes-builds-the-sweep-0)).

### Edge cases {#u12-edge}

- **Negative fractions** (flag 1 only): a receding path whose line passes within r of a side's end corner gets that
  corner's past root, e.g. −0.5 ([test 12](#u12-tests)). In 40,000 random sweeps with flag 1, 89 side-region hits
  were negative; none came from flag 0. A negative fraction sorts first and, through the advance `d = f·(D·n)`, would
  move the body back **towards** the box by `abs(d) − 0.01` (inferred from [Unit 3](physics-bodies.md#u3-codes); whether
  it shows in
  play depends on `Human_OnContact`'s answer for such a box, not traced).
- **Zero move.** Flag 0, outside the footprint: no hit (side: `mx ≤ 0`; corner: `B = 0`). Flag 1, corner region:
  `k = 0 ≤ r·0` takes the near corner, `D = 0` hits the guard, a hit at fraction 0 with the corner rule's `n0`. Flag 1,
  side region: `frac = −s / 0`: on the PS2 a signed `FLT_MAX` whose sign follows the zero's sign (`−mx` of +0 is −0
  in the regions that negate), so the left and bottom sides give +`FLT_MAX` (no hit) and the right and top sides
  −`FLT_MAX` (a hit) (inferred from the FPU's divide-by-zero rule). `+0x10` is then `normalize(0)` (VU0 `rsqrt` of 0;
  meaningless). Coney: write `(0, 0, 0, w)` for `+0x10` and reproduce the hit / no-hit pattern, noting it.
- **Exact tangency** (`D = 0`) outside the guarded branch divides by zero; Coney may compute `C / (D − B)` (the
  tangent root), as in [Unit 5](#u4b-ray-sphereroot-0034af50-and-ray-circlero), but must return 0 in the guarded
  near-corner branch (`k ≥ 0`).
- Region boundaries are inclusive inside (`x = ±hx` is the middle column), and the side step's "touching" is `s ≥ 0`,
  the corner's `C ≤ 0`, so exact contact at the start is a hit at 0 (when approaching or flag 1).

### Test ideas {#u12-tests}

Box B: centre P = (0, 0, 0.5), axes identity, half (2, 1, 0.5). Capsule r 0.35, h 1.8, base z 0 unless given; flag 0,
grounded, unless given. Values from the model of this page (`scratch` Python), fractions checked against a sampled
sweep of a circle against the rectangle; 6 decimals.

1. **Face.** Base (−2.6, 0.2), move (0.5, 0, 0): fraction 0.5; `+0x50` (−1, 0, 0, 1); `+0x60` (−2, 0.2, 0.9, 1);
   `+0x90` (−2.35, 0.2, 0.9, 1); `+0xa0` (0, 0, 0, 1); `+0xb0` 0; `+0x10` (1, 0, 0).
2. **No vertical test.** Test 1 with base z 5.0 (far above the box): fraction 0.5, `+0x68` and `+0x98` 5.9. Move
   (0.5, 0, −0.4) from base z 1.0: fraction 0.5, `+0x68` 1.7, `+0x10` (0.780869, 0, −0.624695).
3. **A face's end.** Base (−2.6, 0.9), move (0.5, 0.4, 0): side region, the end corner (−2, 1): fraction 0.539394;
   `+0x60` (−2, 1); `+0x50` (−1, 0, 0, 1) (the face normal though the corner was touched); `+0x90` (−2.3303, 1.1158).
4. **Corner, diagonal.** Base (−2.5, −1.5), move (0.4, 0.4, 0): fraction 0.631282 (= (√0.5 − 0.35) / (0.4√2));
   `+0x50` (−0.707107, −0.707107, 0); `+0x60` (−2, −1); `+0x90` (−2.2475, −1.2475, 0.9); `+0xa0`
   (−0.707107, −0.707107, 0, 1); `+0xb0` 1. Airborne owner: the same, but `+0xa0` (0, 0, 0, 1) and `+0xb0` still 1.
5. **Corner, oblique: the start-centre normal.** Base (−2.5, −1.2), move (0.5, 0.1, 0): near corner, fraction
   0.379545; `+0x50` (−0.928477, −0.371391, 0) (from the start; the true normal at contact is (−0.886, −0.463)).
6. **Corner region reaching a face.** Base (−2.5, −1.1), move (0.5, 0.5, 0): the face x = −2, fraction 0.3;
   `+0x60` (−2, −0.95); `+0x50` (−0.957826, −0.287348, 0) (true (−1, 0, 0)); slide (−0.707107, −0.707107, 0, 1);
   `+0xb0` 1.
7. **Start touching a corner: the opposite-corner normal.** Base (−2.2, −1.2), move (0.1, 0.1, 0): fraction 0;
   `+0x50` (−0.885832, −0.464007, 0) (from (−4.2, −2.2)); `+0x60` (0, 0). Mirror (+, +): base (2.2, 1.2), move
   (−0.1, −0.1, 0): `+0x50` (0.707107, 0.707107, 0). Base (−2.2, −1.2), move (−0.1, −0.1, 0): no hit; the same with
   flag 1: a hit at 0, `+0x50` (−0.885832, −0.464007, 0).
8. **Start touching a face.** Base (−2.2, 0): move (0.1, 0, 0): fraction 0, `+0x50` (−1, 0, 0, 1), `+0x60` (0, 0);
   move (−0.1, 0, 0) or (0, 0.3, 0): no hit; move (−0.1, 0, 0) with flag 1: a hit at 0.
9. **Over the box.** Base (−1.5, 0.1), any move including zero: fraction 0, `+0x50` (1, 0, 0, 1) (unsigned, though the
   capsule is on the −x half); base (0.5, −0.9): (0, 1, 0, 1); base (0.5, −0.5) (tie): (1, 0, 0, 1); `+0x60` (0, 0).
10. **Short and exact.** Base (−2.6, 0.2), move (0.2, 0, 0): fraction 1.25, no hit. r 0.25, base (−2.5, 0), move
    (0.25, 0, 0): fraction exactly 1.0, a hit.
11. **A graze missed.** Base (−2.6, −1.01), move (0.48, −0.3, 0): no hit (`−k` = 0.1848 > r·mx = 0.168, `my` < 0);
    the sampled sweep touches the corner at 0.6667.
12. **Negative fraction.** Base (−2.5, 0.8), move (−1.0, 0.3, 0), flag 1: a hit at fraction −0.5 (q = (−0.5, −0.2),
    `B` 0.44, `C` 0.1675, `D` 0.105); `+0x50` (−1, 0, 0, 1); `+0x60` (−2, 1); `+0x90` (−2.0, 0.65, 0.9). Flag 0: no hit.
13. **A0/A1 corner normals.** Box lying on its side: centre (0, 0, 0.5), A0 = (0, 0, 1), A1 = (1, 0, 0),
    A2 = (0, 1, 0), half (0.5, 2, 1) (u = 0, the same footprint as B). Test 1: identical. Test 4: fraction 0.631282,
    `+0x50` (−0.707107, 0, −0.707107) (a z part), slide (−0.707107, −0.707107, 0, 1) (correct).
14. **Rotated box.** Centre (10, 5, 0.5), A0 = (0, 1, 0), A1 = (−1, 0, 0), A2 = (0, 0, 1), half (2, 1, 0.5); base
    (11.6, 5.2), move (−0.5, 0, 0): local (0.2, −1.6) → the −A1 side; fraction 0.5; `+0x50` (1, 0, 0, 1); `+0x60`
    (0.2, −1); `+0x90` (11.35, 5.2, 0.9).
15. **Mirror.** Box B moving by (−0.5, 0, 0) against a capsule at rest at base (−2.6, 0.2): fraction 0.5; `+0x10`
    (−1, 0, 0, 1); `+0x50` (1, 0, 0, w of the move); `+0x20` (−2, 0.2), `+0x28` 0.9, `+0x2c` 1.0; `+0x90`
    (−2.35, 0.2, 0.9); `+0xa0` (0, 0, 0, 1); `+0xb0` 0.
16. **Zero move.** Base (−2.6, 0.2): no hit. Base (−2.5, −1.5), flag 1: a hit at 0, `+0x50` (−0.874157, −0.485643, 0)
    (from (−4.5, −2.5)), `+0x60` (−2, −1).
17. **Brute force.** Random starts and moves around B (flag 0): every hit's fraction within 1e-4 of the sampled sweep;
    the only misses are start overlaps moving away or parallel and corner grazes as in test 11; with flag 1 the only
    extra hits are negative fractions as in test 12.

## Unit 13: Box against box and triangle {#unit-13}

**What it is.** An oriented box swept against another box (sweep table entry (1, 1)), against one triangle, and
against the level's collision mesh (mesh sweep table type 1); and the two box-segment entries, which never touch.
The box-box test only says **whether** the boxes meet during the move: its contact always has fraction 0. The
box-triangle test is a separating-axis sweep with a quirk that makes it return fraction 0 (or too early) whenever an
axis has exactly zero speed, which an axis-aligned box falling straight down always has.

- **Coney:** `src/physics/sweeps.*`, `src/world_objects/loose_objects.*` (a falling object's box then sweeps instead
  of the centre ray stand-in), the car's loose parts.
- **Tests:** [Test ideas](#u13-tests): a box dropped flat (fraction 0, the quirk) and tilted (the true fraction)
  onto a floor triangle; the early and the false contact of the zero-speed quirk; two boxes sliding past without
  touching, and one passing a corner that only the velocity axes separate; the brick of
  [Settling a landed object](physics.md#settle) resting at 0.2831 (floor 0.2231 + 0.05 + 0.01), which needs a move with
  a
  horizontal part (test 9).

8 functions, 7848 bytes (0 tagged). `Sweep_IntervalOverlap` (`0x0034a8d0`, [Unit 11](physics-bodies.md#unit-11)'s table)
is specified
here too, because the box-triangle sweep depends on it.

| Address | Name | Size |
| --- | --- | --- |
| `0x003451e8` | `PhysicsSweep_BoxBox` | 524 |
| `0x00345588` | `PhysicsSweep_SegmentBox` | 8 |
| `0x00345590` | `PhysicsSweep_BoxSegment` | 224 |
| `0x0034c430` | `Sweep_BoxBox` | 4484 |
| `0x0034d5d0` | `Sweep_BoxTriangle` | 872 |
| `0x0034aa60` | `BoxTri_ProjectBox` | 200 |
| `0x0034ab28` | `BoxTri_ProjectTriangle` | 224 |
| `0x00348530` | `PhysicsMesh_SweepBox` | 1312 |

Conventions as in [Unit 1](#unit-1): `a·b` sums x, y, z as `(ax·bx + ay·by) + az·bz`; `a × b` is the cross product
(the VU0 `vopmula` / `vopmsub` pair computes first × second); vectors are 16-byte `float[4]`. Everything below is
confirmed (code) at the cited addresses, read in the disassembly (2026-10-12), unless a line says otherwise.

### Records {#u13-records}

**The box record** the kernels read: four rows `A0`, `A1`, `A2` (unit axes) and `P` (centre), 16 bytes each, then
the half extents `h0`, `h1`, `h2` at `+0x40`. The wrappers pass the box shape's `+0x50` (rows) and `+0x40` (half
extents) as two pointers ([Shapes](physics.md#shapes)); the mesh sweep copies both into one 0x50-byte record on its
stack
(`0x003485a8`-`0x003485e0`). No kernel normalises an axis or checks that the rows are orthogonal: the projections
assume they are unit and orthogonal.

**Separation, not a fraction, is what the box-box test returns.** The box-triangle test returns a fraction.

### Sweep_IntervalOverlap (0x0034a8d0) {#u13-interval}

Size 0x18c. `int fn(float* tFirst, float* tLast, f12 tMax, f13 v, f14 aMin, f15 aMax, f16 bMin, f17 bMax)`.
Interval `A` = `[aMin, aMax]` stands still; interval `B` = `[bMin, bMax]` moves by `v` per unit of `t`. It narrows the
window `[tFirst, tLast]` in which the two overlap, and returns **1 when they are separated** for the whole move
(or the window is empty), 0 otherwise. Confirmed (code) at `0x0034a8d0`-`0x0034aa58`:

1. **B below A** (`bMax < aMin`, strict): if `v ≤ 0`, return 1. Else `k = 1/v`; `t = (aMin − bMax)·k`; if
   `tFirst < t`, `tFirst = t`; if `tMax < tFirst`, return 1 (`tFirst` keeps the new value). Then
   `t = (aMax − bMin)·k`; if `t < tLast`, `tLast = t`; go to the end test.
2. **B above A** (`aMax < bMin`): if `v ≥ 0`, return 1. Else `k = 1/v`; `t = (aMax − bMin)·k`, raise `tFirst` as
   above, `tMax` test; then `t = (aMin − bMax)·k`, lower `tLast`; end test.
3. **Overlapping now** (neither, so touching counts): if `v > 0`, `t = (aMax − bMin)/v`, lower `tLast`; if `v < 0`,
   `t = (aMin − bMax)/v`, lower `tLast`; end test. Here the code divides; cases 1 and 2 multiply by `1/v` (a last-bit
   difference only).
4. **Overlapping and `v` exactly 0** (`0x0034aa1c`-`0x0034aa50`): **`tFirst = 0` and `tLast = 1.0`**, return 0.
   This is a quirk: it throws away the window the earlier axes built (a correct test leaves it alone).
5. **End test:** return 1 if `tLast < tFirst` (strict), else 0.

A separated axis with `v = 0` (cases 1, 2) returns 1, which is right. Only case 4 is wrong, and it decides most of
the box-triangle results below. Callers: `Sweep_BoxTriangle` and `PhysicsSweep_CapsuleCapsule` (`0x00343cd8`).

### BoxTri_ProjectBox (0x0034aa60) and BoxTri_ProjectTriangle (0x0034ab28) {#u13-project}

`BoxTri_ProjectBox(float* min, float* max, rows, half, axis L)`, size 0xc8: `r = (h0·|L·A0| + h1·|L·A1|) +
h2·|L·A2|`, `c = L·P`, `min = c − r`, `max = c + r`. `L` need not be unit (both sides of every comparison scale
with it).

`BoxTri_ProjectTriangle(float* min, float* max, v0, v1, v2, axis L)`, size 0xe0: `min = max = L·v0`; then for `v1`
and then `v2`: if below `min`, it becomes `min`; otherwise, if above `max`, it becomes `max`.

### Sweep_BoxTriangle (0x0034d5d0) {#u13-box-triangle}

Size 0x368. `float fn(Box* box, Tri* T, const vec4* move)`: `a0` the box record (rows and half extents, above),
`a1` the working triangle ([Unit 2](physics-bodies.md#u2-the-working-triangle-t-d0-bytes-on-the-c): corners
`+0x00`/`+0x10`/`+0x20`,
edges `e0`, `e1`, `e2` at `+0x30`/`+0x40`/`+0x50`, as the caller left them), `a2` the box's move. The triangle stands
still; the box moves by `move` over `t` from 0 to 1. Returns the fraction `t` of first contact, or **1e28**
(`0x6e013f39`) for no contact. Nothing else is written.

1. `tFirst = 0`, `tLast = 1e24` (`0x6753c21c`), `tMax = 1.0` for every axis.
2. **Axis 1, the face:** `N = e0 × e1` (not normalised). The triangle's interval is the single value `N·v0`; the box's
   is `BoxTri_ProjectBox` on `N`; speed `N·move`. `Sweep_IntervalOverlap(&tFirst, &tLast, 1.0, speed, triangle min,
   triangle max, box min, box max)`; 1 → return 1e28.
3. **Axes 2-4, the box's axes** `A0`, `A1`, `A2` in that order: triangle interval by `BoxTri_ProjectTriangle`, box
   interval `A_i·P ± h_i`, speed `A_i·move`; same call, 1 → 1e28.
4. **Axes 5-13, the edge crosses** `e_i × A_j`, `i` = 0, 1, 2 outer, `j` = 0, 1, 2 inner: both intervals by the two
   projections, speed `(e_i × A_j)·move`; same call, 1 → 1e28. A zero axis (an edge parallel to a box axis) is not
   skipped: both intervals are `[0, 0]`, the speed 0, so it is case 4 of the interval test and **resets the window**.
5. `t = tFirst`. If `t < 0` or `t > 1.0`, return 1e28; else return `t` (so `t = 1` exactly is a contact).

No epsilon anywhere; touching counts as contact.

**What the quirk does** (the arithmetic is confirmed (code); the cases below were computed from it in Python,
2026-10-12, and the exact answers by a brute-force static separating-axis test sampled at 20,000 steps):

- An axis whose speed is exactly 0 and whose intervals overlap sets `tFirst = 0`, `tLast = 1`. Later axes with a
  non-zero speed raise `tFirst` again, so the result is the entry time of the **last** moving axis after the last
  reset, or 0.
- **A box falling straight down** (move `(0, 0, −d)`) with a horizontal axis (any box lying flat or standing upright)
  has `A·move = 0` on that axis. If the true contact is within the move, the result is very often **0** instead of the
  true fraction (test 1 below: 0 instead of 0.75). A box with no exactly horizontal axis and no edge parallel to an
  axis gets the true fraction (test 2).
- It can give a contact **earlier** than the true one (test 3: 0.5 instead of 1.0), and a contact where there is
  **none** (test 4: 0.25 where the box passes the triangle), because the reset also forgets an empty window.
- A move that falls short is still refused: the face axis comes first and returns 1e28 when the box cannot reach the
  plane within the move. So the quirk only changes the fraction of a box that reaches the triangle's plane, plus
  the false contacts of test 4.

What it means in the game (inferred): a resolution moves the body by `f × (D·n)` ([the
advance](physics-bodies.md#u3-codes)), so a
fraction of 0 means **no advance**: the box stops where it was, short of the floor, and bounces or stops there. In
the game an object's rotation is rarely exactly axis-aligned, so its axes are rarely exactly horizontal; the brick of
[Settling](physics.md#settle) ended exactly 0.01 above the floor after its settle, which needs a non-zero fraction on
that
contact. Coney must reproduce the quirk exactly (faithful first), so its tests set up exact zeros on purpose.

### PhysicsMesh_SweepBox (0x00348530) {#u13-mesh}

Size 0x520. Mesh sweep table `+0xe0`, type 1. `int fn(Contact* out, int max, const AABB* worldBox, Body* body,
Shape* box, const vec4* move)` (`a0`-`a3`, `t0`, `t1`); the options word (`t2`) and `f12`/`f13` (the `n.z` range of
[Unit 2](physics-bodies.md#u2-0033d2d8-iphysics-sweepshapevsmesh)) are **never read**. Returns the number of contacts
written. It
follows the pattern of the sphere sweep ([Unit 2](physics-bodies.md#u2-00348a50-physicsmesh-sweepsphere)): the same
query start
(`CollisionMesh_BeginShapeQuery`), cell walk (x outer, y, z inner), visited bits, 100-entry stack array of 0xb0
mesh contacts with no overflow check, sort and cap.

**The box:** rows and position from shape `+0x50`…`+0x80`, half extents from `+0x40`, as they are at the call (the
start of the move). `P` = shape `+0x80`.

**Per triangle** (index from the cell list, visited bit tested and set first):

1. **Bit 3:** `flags = u16 at +0x06`, masked to `0xfff`. If bit `0x8` is set and the body's flags `+0x40` have
   `0x100`, skip (`0x003486e8`-`0x003486fc`; the body pointer is read with no null test). The sphere's option
   `0x8000` has no counterpart here.
2. Skip unless bit 0 (enabled).
3. Corners into `T`, unit normal `n` (`CollTri_ComputeNormal`).
4. **Side:** `s = n·P − n·v0`. If `s < 1e-6` (`0x358637bd`): a one-sided triangle (raw flags bit `0x2` clear) is
   skipped, a two-sided one is flipped (`CollTri_Flip`) (`0x003487b8`-`0x003487ec`). The box's **centre** decides the
   side, not its corners.
5. **Into the face:** skip if `n·move ≥ −0.001` (`0xba83126f`), with the possibly flipped normal.
6. Edges un-normalised (`CollTri_ComputeEdges(T, 0)`). The plane constant `T+0x80` is **not** stored (unlike the
   sphere and capsule), and the lengths `T+0x60` are not computed, so the copy's `+0x80` and `+0xa0` hold stale
   stack bytes; nothing reads them.
7. `t = Sweep_BoxTriangle(&box, T, move)`. Kept when `0 ≤ t ≤ 1`: a mesh contact with `+0x00 = t`, `+0x04` the
   10-byte file triangle, `+0x08 = 0`, `+0x20`…`+0xaf` the copy of `T+0x00`…`T+0x8f` (normal at `+0x90`)
   (`0x00348894`-`0x003488e0`). `+0x0c`, `+0x10`, `+0x14` are not written.

**Bug, confirmed (code) at `0x0034888c`:** the `t > 1` exit branches past the loop counter's increment (as the
capsule's does, [Unit 2](physics-bodies.md#u2-00347c08-physicsmesh-sweepcapsule)), and here the kernel's "no contact"
value 1e28 takes
that exit. So **every triangle the kernel misses** leaves the cell's counter where it was while the list pointer
moves on, and the walk reads one more `u16` past the end of the cell's list per miss (and per miss among those).
Every other skip (visited, disabled, bit 3, behind, receding, `t < 0`) counts correctly. What the extra entries
are depends on what follows the list in memory (the next cell's count and indices, inferred from the layout in
[Collision](collision.md)); each is treated as a triangle index, subject to the visited bit and every test. Since a
triangle the swept box really reaches must lie in a cell the query box covers, the extra triangles can only add
contacts that the cell walk would also find (inferred), except through the false contacts of the quirk above.
Coney should walk the lists exactly and note the difference in a comment.

**After the walk** (`0x00348930`-`0x00348a1c`): none kept → return 0. Sort (`MeshContacts_SortByFraction`, with its
smear); `count = min(count, max)`. For each sorted entry `i`, into `out[i]`:

| 0xc0 field | Value |
| --- | --- |
| `+0x04` own shape | the box shape |
| `+0x44` other shape | 0 |
| `+0x50` normal | mesh contact `+0x90` (unit, flipped towards the box centre when two-sided) |
| `+0x80` fraction | `t` |
| `+0x90` point | `P + t × move`, `w = 1`: the box **centre** at contact, not a point on the surface |
| `+0xb0` flags | mesh contact `+0x08`, always 0 |
| `+0xb4` triangle | mesh contact `+0x04` |
| `+0x10` | `unit(move)` (xyz; `w` = the move's `w`) |

Nothing else (`+0xa0`, `+0x84`, `+0x88` untouched). Returns the capped count. Reached through
`IPhysics_SweepShapeVsMesh` (max 1, [Unit 2](physics-bodies.md#u2-0033d2d8-iphysics-sweepshapevsmesh)) for a body whose
first shape is
a box, and through `IPhysics_CollideShape` with a box shape.

**Differences from the sphere sweep, in one list:** no options (no `n.z` range, no `0x40` back faces, no `0x8000`, no
one-contact-per-plane `0x20000`); the side is taken at the box centre; the point is the box centre; the flags word is
always 0; the counter bug on every kernel miss.

### Sweep_BoxBox (0x0034c430) {#u13-box-box}

Size 0x1184. `bool fn(rowsA, halfA, moveA, rowsB, halfB, moveB)` (`a0`-`a3`, `t0`, `t1`). Do two oriented boxes,
each moving linearly over `t` from 0 to 1 without turning, overlap at any time? Returns 1 if yes (touching counts),
0 if a separating axis exists. **No fraction, point or normal is computed.** The test is the standard one for boxes
moving with constant velocity (15 box axes tested at both ends of the move, then 6 axes built from the relative
velocity), which is exact for that motion (inferred: the swept difference of two boxes is a convex body whose face
normals are among these 21 axes).

Let `D = P_B − P_A` (`w = 1`), `W = moveB − moveA`, `D1 = D + W` (the centre offset at the end), `c_ij = A_i·B_j`.
Separation on an axis of the first 15 means **both** projections of the centre offset, at `t = 0` and at `t = 1`, lie
beyond `R` on the same side: `R < p0 and R < p1`, or `p0 < −R and p1 < −R` (strict; `0x0034c5a0`-`0x0034c614` for
the first axis). In order, returning 0 at the first separated axis:

| Axes | `p0` (at `t = 0`; `p1` the same with `D1`) | `R` |
| --- | --- | --- |
| `A_i`, i = 0, 1, 2 | `A_i·D` | `a_i + ((b0·abs(c_i0) + b1·abs(c_i1)) + b2·abs(c_i2))` |
| `B_j`, j = 0, 1, 2 | `B_j·D` | `a0·abs(c_0j) + a1·abs(c_1j) + a2·abs(c_2j) + b_j` |
| `A_i × B_j`, i outer, j inner | `(A_{i+2}·D)·c_{i+1,j} − (A_{i+1}·D)·c_{i+2,j}` | `a_{i+1}·abs(c_{i+2,j}) + a_{i+2}·abs(c_{i+1,j}) + b_{j+1}·abs(c_{i,j+2}) + b_{j+2}·abs(c_{i,j+1})` |

(indices mod 3). Then the velocity axes (`0x0034d0f8` on), with `X = W × D` and separation `R < |X·axis|`:

| Axis | `R` |
| --- | --- |
| `W × A_i`, i = 0, 1, 2 | `a_{i+1}·abs(W·A_{i+2}) + a_{i+2}·abs(W·A_{i+1}) + Σ_j b_j·abs(c_{i+1,j}·(W·A_{i+2}) − c_{i+2,j}·(W·A_{i+1}))` |
| `W × B_j`, j = 0, 1, 2 | `Σ_i a_i·abs(c_{i,j+1}·(W·B_{j+2}) − c_{i,j+2}·(W·B_{j+1})) + b_{j+1}·abs(W·B_{j+2}) + b_{j+2}·abs(W·B_{j+1})` |

With `W = 0` every velocity axis passes (`0 ≤ R`), so two still boxes reduce to the static 15-axis test. The axes
are not normalised and parallel boxes give zero cross axes, which pass (`|0| ≤ R`); no epsilon. All 21 pass → 1.

### The pair wrappers: 0x003451e8, 0x00345588, 0x00345590 {#u13-wrapper}

**PhysicsSweep_BoxBox**, size 0x20c, sweep table (1, 1). `bool fn(Contact* c, Body* own, Shape* ownBox, Body* other,
Shape* otherBox, const vec4* move)`; the bodies are not read. Calls `Sweep_BoxBox(ownBox+0x50, ownBox+0x40, move,
otherBox+0x50, otherBox+0x40, (0, 0, 0, 1) at 0x005116c0)`, so `W = −move` and `D` = other centre − own centre. No
overlap: return 0, nothing written. Overlap (`0x00345240`-`0x003453d4`), with `P` = own centre (`+0x80`), `Q` = other
centre, `B0`, `B1` and `b0`, `b1` the other box's axes and half extents:

| Field | Value |
| --- | --- |
| `+0x80` fraction | **0.0**, always |
| `+0x50` normal | `−unit(move)` (xyz), `w` = the move's `w`: straight back along the move, whatever the faces |
| `+0x10` | `unit(move)`, `w` the same |
| `+0x60` | `clamp(B0·P − B0·Q, −b0, b0)` |
| `+0x64` | `clamp(B1·P − B1·Q, −b1, b1)` |
| `+0x68` | `P.z`, the own centre's **world** z |
| `+0x90` point | `(P.x + [+0x60], P.y + [+0x64], P.z, 1)`: the other-box-frame offsets added to world x and y |
| `+0xb0` | 0 |

`+0x6c` and everything else are not written. Returns 1. The point mixes frames (a quirk; it is a point on the other
box only when that box is world-aligned and the own centre is level with it). Consequences in a resolution
([the codes](physics-bodies.md#u3-codes)), inferred: with `f = 0` there is no advance, and code 2 (a world object or a
car) bounces
the velocity off `−unit(move)`, i.e. reverses its component along the move, scaled by the restitution.

**PhysicsSweep_SegmentBox**, size 8, sweep table (4, 1): returns 0.

**PhysicsSweep_BoxSegment**, size 0xe0, sweep table (1, 4): calls `0x00345588` with the bodies and shapes swapped and
the move negated; that returns 0, so it returns 0 and writes nothing. Its code to swap the contact's halves
(`+0x00`…`+0x3f` with `+0x40`…`+0x7f`) after a hit never runs. A box and a segment never touch.

### Test ideas {#u13-tests}

Values computed in Python from this spec (scratch `u13/model.py`, 2026-10-12); "exact" is a brute-force static test
sampled at 20,000 steps. Boxes have half extents (0.5, 0.5, 0.5) unless stated; `I` is the world-aligned box.
Triangles are given as `v0, v1, v2`; their kernel normal is `(v1 − v0) × (v2 − v1)`.

1. **Flat drop, the quirk.** Floor `(−10, −10, 0), (30, −10, 0), (−10, 30, 0)`, box `I` at (0, 0, 2), move
   (0, 0, −2): `Sweep_BoxTriangle` returns **0.0** (exact 0.75). Window after each axis: face 0.75; `A0`, `A1` reset
   to 0; `A2` 0.75; the last resets come from `e1 × A2`, `e2 × A1` and `e2 × A2`. The same with move (0.1, 0.05, −2)
   also gives 0.0 (the edge along x is parallel to `A0`: a zero axis). Box `I` rotated 30° about z and move
   (0, 0, −2): 0.0; with move (0.1, 0.05, −2): **0.75**, the true value.
2. **Tilted drop.** Same floor, box rotated 40° about the unit axis along (1, 2, 3), centre (0, 0, 2), move (0, 0, −2):
   **0.629511** (the lowest corner is at z 1.259023; exact 0.62955 at the sampling step).
3. **Too early.** Wall `(0, 0, 0), (0, 0, 2), (0, 2, 0)` (normal −x), box `I` at (−1, −2, 0), move (1, 1.5, 0):
   **0.5** (exact 1.0: the `A2` axis has speed 0 and resets the window that `A1` had raised to 1.0).
4. **A contact where there is none.** Triangle `(1, 3, −1), (0, 0, 1), (1, −1, 0)` (normal (5, 1, 4), the box centre in
   front, `n·move` = −4), box `I` at (2, 1.5, 0), move (−2, 0, 1.5): **0.25**; exact: no contact. Window: face
   [0.625, 3.125], `A0` [0.625, 1.25], `A1` resets to [0, 1], then the crosses narrow it to [0.25, 0.556].
5. **Short and touching.** Test 1's box with move (0, 0, −1): 1e28 (refused by the face axis). With move (0, 0, −1.5)
   (touching exactly at the end): 0.0 by the quirk (exact 1.0). Tilted box of test 2 with move (0, 0, −1): 1e28.
6. **Interval test alone.** `A` = [0, 1], window (0, 1e24), `tMax` 1: `B` = [−3, −2], `v` = 4 → 0, window (0.5, 1.0);
   `v` = 1 → 1 (`tFirst` = 2 > 1); `v` = −1 → 1. `B` = [0.5, 2], window (0.7, 0.9), `v` = 0 → 0 and the window becomes
   **(0, 1)**. `B` = [1, 2] (touching), `v` = 0 → the same reset. `B` = [2, 3], `v` = 0 → 1.
7. **Mesh filters.** With test 2's tilted box over a one-sided floor: centre below the plane → no contact; two-sided
   → flipped normal (0, 0, −1) and no contact for a downward move (receding). Move (0, 0, −0.0009): receding by the
   −0.001 rule, no contact. Bit 3 set and body flag `0x100`: skipped; bit 3 with the body flag clear: found.
8. **Mesh contact.** Test 2 through `PhysicsMesh_SweepBox` with `max` 1: one contact, `+0x80` 0.629511, `+0x50`
   (0, 0, 1), `+0x90` = (0, 0, 2 − 1.259023) = (0, 0, 0.740977, 1), `+0xb0` 0, `+0x10` (0, 0, −1).
9. **The brick lands.** Floor at z 0.2231 (test 1's triangle raised), brick half extents (0.035, 0.105, 0.05) rotated
   30° about z, centre (0, 0, 0.35), velocity (0.3, 0.2, −5.4886) m/s, `dt` 1/30, so `D` = (0.01, 0.006667,
   −0.182953): fraction **0.420326** (= 0.0769 / 0.182953; no reset, since no axis has zero speed). The advance
   `0.420326 × −0.182953 = −0.0769` then puts the centre at 0.35 − 0.0769 + 0.01 = **0.2831**. With `D` = (0, 0,
   −0.182953) instead the fraction is 0 and the brick is not advanced (the quirk).
10. **Box-box, passing.** Own box `I` at (0, 0, 0), other `I` at (0, 2, 0): move (3, 0, 0) → 0 (axis `A1`); move
    (0, 0.99, 0) → 0; move (0, 1.0, 0) → 1 (touching at the end); move (0, 1.5, 0) → 1.
11. **Box-box, a corner only the velocity axes see.** Own `I` at (−2, 0, 0), other `I` at the origin, move (2, 3, 0):
    0, separated first by `W × A2`; without the six velocity axes the 15 end-point axes would say 1. Move (3, 3, 0)
    → 1.
12. **Box-box contact.** Own `I` at (0, 0, 1), other `I` at (1.5, 0.3, 1.2), move (2, 0, 0): returns 1 with `+0x80`
    0, `+0x50` (−1, 0, 0), `+0x10` (1, 0, 0), `+0x60` −0.5, `+0x64` −0.3, `+0x68` 1.0, `+0x90` (−0.5, −0.3, 1.0, 1),
    `+0xb0` 0. Zero move with overlapping boxes: 1 (a zero move never reaches it from the body sweep, which stops
    below `|D|² ≤ 1e-6`).
13. **Box-segment.** Both orders return 0 and leave the contact untouched.
