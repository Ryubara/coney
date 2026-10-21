# Physics units: shapes, bodies and moves

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-06, the whole of `Physics/` on 2026-10-07); the game under PCSX2 2.9.94 (PINE, hooks and `pcsx2 record`, in
`level99`, 2026-10-07). The exact specifications of
[Reimplementing Physics, RayCast and World](physics.md#units) were read in the disassembly on 2026-10-12.

Part of [Reimplementing Physics, RayCast and World](physics.md#units): the exact specifications of a shape against the
collision mesh, moving a body, shapes, bodies and the sort-and-sweep world, ignore lists, creating bodies, the human
body set and shapes moved against bodies. The plan, the order of the units and their dependencies are on that page; the
subsystem itself is [Physics](physics.md).

## Unit 2: A shape against the collision mesh {#unit-2}

**What it is.** A shape swept against (or overlapping) the level's collision mesh: the cells, the visited bits, the
per-triangle rules, the capsule's wall rules and corner slide, the sort, and the contact each hit becomes.

- **Coney:** `repo:src/physics/mesh_sweep.h` (done): the swept test from the start of the move, which replaced the
  walking body's old push-out (`human::nearestWallPush`, `slideAlongTouchedFaces`, `edgeSlide`).
- **Tests:** [Test ideas](#u2-test-ideas-synthetic-meshes-src-raycast), built with `src/raycast/collision_builder.h`;
  test 1 is the fast mover that must not tunnel.

10 functions, 7136 bytes (2 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00347170` | `CollisionMesh_BeginShapeQuery` | 396 |
| `0x00347300` | `MeshContacts_SortByFraction` | 184 |
| `0x0033d718` | `Contact_InsertSorted` | 160 |
| `0x0033d2d8` | `IPhysics_SweepShapeVsMesh` | 92 |
| `0x00347c08` | `PhysicsMesh_SweepCapsule` (tagged) | 2344 |
| `0x00348a50` | `PhysicsMesh_SweepSphere` | 1836 |
| `0x003473c8` | `PhysicsMeshOverlap_Sphere` | 1016 |
| `0x003473b8` | `PhysicsMeshOverlap_Box` | 8 |
| `0x003473c0` | `PhysicsMeshOverlap_Capsule` | 8 |
| `0x003477c0` | `PhysicsBody_PushOutOfWalls` (tagged) | 1092 |

Conventions: `n·x` is a dot product over x, y, z (the VU sums only those three), `a × b` a cross product, "unit" means
divided by its length. Vectors are 16-byte `float[4]`; `w` is noted only where code sets it.

### Shared records {#u2-shared-records}

### The working triangle (`T`, 0xd0 bytes on the caller's stack) {#u2-the-working-triangle-t-d0-bytes-on-the-c}

Filled by every mesh function for the triangle under test; the helpers below write into it. Confirmed (code) at
`0x00343a90`, `0x00343b20`, `0x00343c30`, `0x00343c90`.

| Offset | Content | Written by |
| --- | --- | --- |
| `+0x00`, `+0x10`, `+0x20` | corners `v0`, `v1`, `v2` (copied whole from the vertex buffer, `w` = 1.0) | the caller |
| `+0x30`, `+0x40`, `+0x50` | edges `e0 = v1 − v0`, `e1 = v2 − v1`, `e2 = v0 − v2` (`w` = 1) | `CollTri_ComputeEdges` `0x00343a90` |
| same | with its second argument non-zero, the edges are made **unit** in place | `CollTri_ComputeEdgeNormals` `0x00343b20` (the name is wrong: it normalises the edges; no edge normals are made) |
| `+0x60`, `+0x64`, `+0x68` | the three edge lengths (only when normalised) | `0x00343b20` |
| `+0x70` | unit normal `unit((v1 − v0) × (v2 − v0))` | `CollTri_ComputeNormal` `0x00343c30` |
| `+0x80` | plane constant `d = n·v0` (a scalar in `x`) | the caller, just before the kernel |
| `+0x90` | a copy of the normal (capsule sweep only) | the caller |
| `+0xc0` | the kernel's result record: `+0xc0` hit count (cleared to 0 before the call), `+0xc4` fraction | the kernel |

**Flip** (`CollTri_Flip` `0x00343c90`): swaps corners 1 and 2 and negates the normal (all four lanes). It does not
recompute `d` or the edges. Confirmed (code). So after a flip, "edge k" means the edge of the swapped corner order.

### The mesh contact (0xb0 bytes) {#u2-the-mesh-contact-b0-bytes}

Each mesh function collects hits in a stack array of **100** mesh contacts (17,600 bytes) plus a 100-entry pointer
list; nothing checks the count against 100 (confirmed (code) in all four collectors: `0x003481a0`, `0x00348ec0`,
`0x00347674`, `0x00347abc`). Fields:

| Offset | Capsule sweep | Sphere sweep | Sphere overlap | Push-out |
| --- | --- | --- | --- | --- |
| `+0x00` sort key | fraction `t` | fraction `t` | depth `r − dist` | squared distance |
| `+0x04` | pointer to the 10-byte file triangle | same | same | same |
| `+0x08` | not written | flags `0x10` / `0x20` / `0x40` (below) | 0 | 0 |
| `+0x0c` | not written | signed start distance `n·c − d` (normal before any flip) | not written | not written |
| `+0x10` | 0.0 | not written | not written | not written |
| `+0x14` | the kernel's region code (below) | not written | not written | not written |
| `+0x20`…`+0xaf` | a copy of `T+0x00`…`T+0x8f`: corners at `+0x20/+0x30/+0x40`, edges at `+0x50/+0x60/+0x70`, lengths `+0x80`, **normal `+0x90`**, `d` at `+0xa0` | same | same (key and pointer only otherwise) | same |

Confirmed (code) at the cited stores. The edges in the copy are whatever `T` held at the copy: for both sweeps that
is the **un-normalised** set (`CollTri_ComputeEdges(T, 0)` runs again just before the kernel, `0x00348118`,
`0x00348e18`), while the lengths at `+0x80` are left from the capsule's earlier normalising call.

**Region code** (capsule only, the kernel's fifth argument): bit `i` is set when the sphere's centre, taken at the
point where it meets the plane, lies on the inner side of edge `i`, i.e. `(e_i × n)·p ≤ (e_i × n)·v_i`
(`0x0034ee60`, Unit 1). 7 means inside all three edges (a face hit). Confirmed (code).

### Mapping into the 0xc0 contact {#u2-mapping-into-the-c0-contact}

Every sweep and overlap writes its output straight into a caller-supplied array of 0xc0 contacts (layout in
physics.md #contacts), one per kept mesh contact, in the order given under each function. Fields a mesh function
leaves alone are filled by the caller through `Contact_InsertSorted` (`+0x00` own owner, `+0x40` other owner = 0).
Common to the three that write contacts, confirmed (code):

| 0xc0 field | Value |
| --- | --- |
| `+0x04` own shape | the querying shape |
| `+0x44` other shape | 0 (the level) |
| `+0x50` normal | mesh contact `+0x90` (unit, turned to the shape when flipped) |
| `+0x80` fraction | mesh contact `+0x00` (for the overlap this is the **depth**, not a fraction) |
| `+0xb4` triangle | mesh contact `+0x04` |

Per function: the sweeps add `+0x10` = unit move, `+0x90` = contact point = `t × move` + the shape's position
(shape vtable `+0x24`, which for the sphere `0x004f1210` and capsule `0x004f1248` returns `&shape+0x50`; for the
capsule that is the feet, not the touching point; `w` = 1); the capsule adds `+0xa0` slide vector, `+0xb0` = 0 or 1
and `+0x84` = 0.0; the sphere adds `+0xb0` = its mesh `+0x08` flags and `+0x88` = mesh `+0x0c`; the overlap writes
`+0xb0` = 0 and nothing else.

### The contact list and its cap of 50 {#u2-the-contact-list-and-its-cap-of-50}

The callers own the list. `PhysicsBody_Sweep` (`0x0033e278`) keeps a pool of exactly 50 contacts (`sp+0x110` to
`sp+0x2690`), a 50-pointer list and a count, reset to 0 at the start of each pass (`0x0033e4a4`).
`IPhysics_CollideShape` (`0x0033e7f0`) and `IPhysics_OverlapShape` (`0x0033ebb8`) pass the mesh function the room
left, `50 − count` (`0x0033ea90`, `0x0033ee8c`). The cap is honoured by the sphere sweep and the sphere overlap and
**ignored by the capsule sweep** (it never reads that argument). `Contact_InsertSorted` itself does not check it.
Confirmed (code).

### `0x00347170` CollisionMesh_BeginShapeQuery {#u2-00347170-collisionmesh-beginshapequery}

Size `0x18c`. `BeginShapeQuery(&mesh, &cellMin, &cellMax, &worldBox)`; returns nothing. Confirmed (code) unless marked.

- **Mesh.** Writes the level's collision mesh (world manager global `0x005147c4` → `+0x40` level object → `+4`) to
  `*mesh`.
- **Visited bits.** Zeroes the mesh's checked bit set (`+0x94`), `(((count + 7) >> 3) + 15) & 0x7ff0` bytes for
  `count` = `+0x84`, 16 bytes at a time. Every collector then tests and sets bit `idx & 7` of byte `idx >> 3` before
  looking at triangle `idx`, so a triangle listed in several cells is looked at once per query. The bit set is
  shared mesh state: two queries cannot be interleaved (inferred: nothing in this unit nests them).
- **Cells.** `worldBox` is `{min, max}` (two vectors). Each corner is transformed separately by the header's
  world-to-grid matrix (`g = x·r0 + y·r1 + z·r2 + r3`, header `+0x10`…`+0x4f`), its `w` set to 0, clamped per axis
  to `[header+0x50, header+0x60]` (that is `[0.5, n − 0.5]` on the disc) and truncated toward zero (`vftoi0`).
  `cellMin`/`cellMax` receive the integer cell coordinates (x, y, z). Because the corners are transformed one by one,
  a negative scale would swap them; every disc matrix is a positive diagonal scale plus a translation
  (collision.md #header), so `cellMin ≤ cellMax` holds (inferred from the disc survey).
- **The cell walk** (in each caller, confirmed (code) in all four): x from `cellMin.x` to `cellMax.x` inclusive
  (outermost), then y, then z (innermost), unsigned compares; cell entry `(z·ny + y)·nx + x` of the grid
  (collision.md #grid); an entry of 0, or a list whose count is 0, is skipped; the list is walked in stored order.
  So triangles are met in (x, y, z, list) order, which only matters through the sort's tie rule below.

### `0x00347300` MeshContacts_SortByFraction {#u2-00347300-meshcontacts-sortbyfraction}

Size `0xb8`. `SortByFraction(pointerList, meshContacts, count)`; fills `pointerList[0..count−1]` with pointers into
the 0xb0 array, ordered by the float at `+0x00`. Confirmed (code).

Algorithm: insertion sort in arrival order. For contact `i`, scan the list from the front for the first `j` whose key
is **strictly greater** than contact `i`'s; place `i` there, else append. Equal keys therefore keep arrival order.

**Quirk, faithful behaviour (confirmed (code) at `0x00347360`-`0x00347374`):** the shift that makes room copies
**forwards** (`list[j+1] ← list[j]`, then reads the slot it has just written), so instead of moving `list[j..i−1]` up
by one it writes the old `list[j]` into every slot `j+1..i` (and one past). Effect:

- `list[0]` is always the true minimum, and the list stays non-decreasing;
- each insertion before the end throws away the entries that were after `j` and replaces them with copies of the old
  `list[j]`.

Example: keys arriving 0.5 (A), 0.6 (B), 0.3 (C) end as `[C, A, A]`; B is lost. A correct sort would give `[C, A, B]`.
Who sees it: only callers that use more than the first entry, i.e. the capsule's corner-slide loop and the contacts
`IPhysics_CollideShape` passes on (duplicates are inserted twice), and the overlap's choice of contact (below). The
walking sweep (`PhysicsBody_Sweep`) and the push-out use only the first entry, so for them the sort is exact.

### `0x0033d718` Contact_InsertSorted {#u2-0033d718-contact-insertsorted}

Size `0xa0`. `InsertSorted(phys, contact, list, &count, ownOwner, otherOwner, ownShape, otherShape)`; returns
nothing. Confirmed (code).

1. Writes `+0x00` = `ownOwner`, `+0x40` = `otherOwner`, `+0x04` = `ownShape`, `+0x44` = `otherShape`.
2. Finds the first list position whose contact has a fraction (`+0x80`) **strictly greater** than the new one's,
   moves the tail up by one (copying backwards, correctly), and stores the pointer there; else appends.
3. `count += 1`. No check against 50 (the callers keep to it, above). Ties: the new contact goes **after** existing
   contacts of equal fraction.

### `0x0033d2d8` IPhysics_SweepShapeVsMesh {#u2-0033d2d8-iphysics-sweepshapevsmesh}

Size `0x5c`. Called only by `IPhysics_SweepBodyVsMesh` (`0x0033d340`, pass step 2 of physics.md #sweep). Returns
true when the type's mesh sweep returned a non-zero count. Confirmed (code).

It picks the mesh sweep by the shape's type byte (shape `+0x30`) from IPhysics `+0xe0 + 4·type` (none → false) and
calls it with:

| Argument | Value |
| --- | --- |
| out | the pool slot at the current count (`pool + count·0xc0`) |
| max contacts | **1** |
| world box | the pass's box: the shape's box at the start of the move, grown by the move, **not** scaled by body `+0x60` ([Unit 3](#u3-sweep), step 5.2) |
| body, shape | the body and its first shape |
| move | the pass's move (`v × dt`) |
| options | `4` when the body's type mask (`+0x44`) has `0x40` (a human), else `0` (`(mask >> 4) & 4`) |
| minimum `n.z` (f12) | **−0.65** (`0xbf266666`) |
| maximum `n.z` (f13) | 0.0 (read only with option 8, never set here) |

`0x0033d340` then inserts **only the first** returned contact (`Contact_InsertSorted` with owner = body `+0x34`, other
owner 0, own shape, other shape 0; `0x0033d3ac`-`0x0033d3d4`). So in the walking sweep each pass meets at most one
mesh contact, the nearest. For the capsule this is fine (its first entry is exact, and the slide vector it carries
was built from all of them). The −0.65 threshold and option 4 are **ignored by the capsule** sweep; for a sphere body
they mean "skip ceilings steeper than n.z < −0.65" for humans only. The landing segment of an airborne human is added
after this call (physics.md #sweep), not here.

### `0x00347c08` PhysicsMesh_SweepCapsule {#u2-00347c08-physicsmesh-sweepcapsule}

Size `0x928`. Mesh sweep table `+0xe0`, type 3. Register arguments: `out` (0xc0 array), `max` (ignored), `worldBox`,
`body`, `shape`, `move` (pointer); `f12`, `f13` and the options word are never read (confirmed (code): no use of
`a1`, `t2`, `f12` or `f13` in `0x00347c08`-`0x0034852c`). Returns the number of contacts written, 0 if none. All
steps confirmed (code) at the cited addresses.

**The sphere** (as characters.md #walls): `r = shape+0x40 × body+0x60 × Human_GetScale(body+0x34)`
(`0x00347c98`-`0x00347cbc`); centre `c` = shape `+0x50` (the feet at the start of the move) with `z += r + 0.05`
(`0.05` = `0x3d4ccccd`). The kernel is given the record `{c, r}`.

**Per triangle**, in cell-walk order, once per query (visited bit):

1. Skip unless flag bit 0 (enabled) is set (`0x00347de0`). No type-bit mask, no `0x800` rule.
2. Load the corners, compute the unit normal `n`.
3. **Wall:** skip if `|n.z| > 0.65` (`0x3f266666`, `0x00347e60`; so exactly 0.65 is a wall).
4. **Side:** `s = n·c − n·v0`. If `s < 1e-6` (`0x358637bd`): a one-sided triangle (flag `0x2` clear) is skipped; a
   two-sided one is flipped (`0x00347ec0`-`0x00347ee4`). A centre exactly on the plane counts as behind.
5. **Into the face:** skip if `n·move ≥ −0.001` (`0xba83126f`, `0x00347f2c`), using the possibly flipped normal. A
   zero move therefore never yields a contact.
6. **Low:** normalise the edges (lengths kept). Take the edge `k` with the largest `|unit e_k.z|` (first one wins a
   tie). Skip if `|v_{k+1}.z − v_k.z| < 0.25` (`0x3e800000`, `0x00347fc4`), corners indexed mod 3.
7. **Thin:** take the longest edge `m` (first one wins a tie). If `|unit e_m.z| < 0.8` (`0x3f4ccccd`): with
   `p = n × unit e_m` (a unit vector in the plane, across the edge), skip if `|p·v_{(m+2) mod 3} − p·v_m| < 0.25`,
   i.e. the third corner is less than 0.25 from the edge's line (`0x00348060`-`0x003480d4`). A nearly vertical
   longest edge skips this test.
8. Store `d = n·v0`, recompute the edges un-normalised, clear the result count and the region, and call
   `Sweep_SphereTriangle(result, {c, r}, T, move, &region)` (`0x0034ee60`).
9. Keep the hit when the kernel returns non-zero and `0 ≤ t ≤ 1` (`t` = result `+4`): append a mesh contact
   (`t`, triangle pointer, `+0x10` = 0, `+0x14` = region, the copy of `T`).

**Bug (confirmed (code) at `0x00348170`):** a hit with `t > 1` leaves the cell's loop counter un-incremented while
the list pointer moves on, so that cell's walk reads one `u16` past the end of its list and treats it as a triangle
index (subject to the visited bit and the tests). The kernel's face path refuses `t > 1`, but its edge steps can return
a fraction above 1
([Unit 1](physics-kernels.md#u1-edge-cylinder)), so the overrun is reachable at a triangle's edges and corners
(inferred: [Unit
1](physics-kernels.md#u1-tests) cases 12
and 13 return 1 with a fraction above 1; not seen at runtime). An implementation should not
read past the list; note the difference in a comment.

**After the walk** (none kept → return 0):

1. Sort (`0x00347300`, with its smear).
2. **Corner slide** (`0x00348244`-`0x00348374`). Start `S = (0, 0, 0)` and `flag = 0`. For each entry of the sorted
   list, in order (duplicates included):
    - skip a face hit (region 7);
    - skip if the body's owner (body `+0x34`, state flags through owner vtable `+0x54`) has `0x4000000` (airborne);
    - `k` = triangle flags `>> 14` (bits 14-15, read from the file triangle, not the flipped copy); skip if `k` = 3;
    - skip if region bit `k` is set (the centre was inside edge `k`);
    - otherwise `t_h = n × (0, 0, 1) = (n.y, −n.x, 0)` with the contact's normal; if edge `k`'s `z` (mesh contact
      `+0x58 + 16k`, the un-normalised edge of the possibly flipped order) is **> 0**, `S −= t_h`, else `S += t_h`;
      `flag = 1`.
3. `S.z = 0`; `S = unit(S) × max(r, |move|)` (`0x00348378`-`0x003483f8`). With no corner contact `S` is zero; the
   VU's reciprocal square root of 0 gives the largest float and `0 × that` = 0, so `S` stays zero on the PS2. In C++
   guard the zero length explicitly (inferred: the VU result is the documented `RSQRT` overflow behaviour).
4. **Output** (`0x00348404`-`0x003484ec`), for each sorted entry `i` into `out[i]`: the common fields, `+0x10` unit
   move, `+0x90` point, `+0xa0 = S` (the same for all), `+0xb0 = flag`, `+0x84 = 0.0`. Returns the count (all of
   them; `max` is not applied).

**What the corner slide is.** Bits 14-15 name one edge of the triangle (0, 1 or 2; 3 = none). When the swept sphere
meets the triangle beyond that edge (not on its face) and the body is on the ground, `S` is a horizontal unit
direction along the wall, chosen by the edge's direction: worked through for a vertical wall whose front faces the
viewer, an edge running down is the wall's left end and gives "left", an edge running up the right end and gives
"right", so `S` always points **outward past the flagged end**. So it eases a walker round an outside corner instead
of stopping on the edge. The arithmetic is confirmed (code); the purpose is inferred. Which faces carry it on the disc
is not counted here (characters.md #slopes notes gravel faces with 3).

**Who consumes it** (`PhysicsBody_ResolveContacts` `0x0033d9d8`, confirmed (code)): for a contact whose handler
answered a non-zero code, `+0xb0` is read (`0x0033e108`).

- `+0xb0 & 1` (the corner slide): unless the body is a human in a pair or block (`Human_IsInPairOrBlock`
  `0x00227fd8`) or the body's flags `+0x40` have `0x8000`, take `S` from `+0xa0`, remove from it the part that points
  back against the move (`Vec_RemoveNormalPart(S, unit move)` `0x0033d870`: only when `S·m < 0`) and **add it to the
  pass velocity** (the vector the next pass's move is made from as `velocity × dt`, `PhysicsBody_Sweep`'s
  `sp+0x40`), `0x0033e148`-`0x0033e178`. So the next pass (there are at most three) moves an extra `S × dt`
  sideways; the velocity reported back to the owner (`sp+0x50`) does not get it. In this branch the `0x20000`
  contact-scale rule is **not** applied.
- Otherwise `+0xb0 & 2` is the other slide (store `+0xa0` once per move in body `+0x50`), and `0x20000` the scale.
  (physics.md #contacts calls this "bit 2"; it is the value 2, bit 1.)

Since the body sweep inserts only the first contact, it is that contact's `+0xa0`/`+0xb0` that is read; they hold the
`S` and flag computed from all contacts.

**How it differs from Coney's stand-in.** The test is swept from the start position (`c` at the start, `t` the
fraction of the move where the sphere first touches), the side is decided **before** the move, and only faces the
move goes into count. A body moving fast enough to cross a wall within one update still meets it at the fraction
where it touches, so there is no tunnelling; a body that starts behind a one-sided wall passes through it.

### `0x00348a50` PhysicsMesh_SweepSphere {#u2-00348a50-physicsmesh-sweepsphere}

Size `0x72c`. Mesh sweep table `+0xe0`, type 2. Arguments: `out`, `max`, `worldBox`, `body` (may be read for its
flags), `shape`, `move`, `options`, `f12` = minimum `n.z`, `f13` = maximum `n.z`. Returns the number of contacts
written. Confirmed (code) at the cited addresses.

**The sphere:** centre = shape `+0x50` exactly (no raise), radius = shape `+0x40` (no body or human scale).

**Per triangle** (same walk and visited bits; the loop counter is correct here):

1. **Bit 3** (`0x0008`, within the low 12 bits): if set, skip when the body's flags `+0x40` have `0x100` or the
   options have `0x8000` (`0x00348c34`-`0x00348c54`). Meaning of bit 3 and of body flag `0x100`: not traced.
2. Skip unless enabled (bit 0 of `flags & 0xfff`).
3. Normal. Option `4`: skip if `n.z < min`; option `8`: skip if `n.z > max` (`0x00348cd4`, `0x00348cf8`). No fixed wall
   rule, no 0.25 m rules, no corner slide.
4. **Side:** `s = n·c − n·v0` (kept for `+0x0c`). If `s < 1e-6`: two-sided → flip; one-sided → skip, unless option
   `0x40`, in which case it is kept with mesh flag `0x10` (the normal is not flipped).
5. **Into the face:** if `n·move ≥ −0.001`: skip, unless option `0x40`, which keeps it and ORs `0x40` when
   `n·move ≤ 0.001` (`0x3a83126f`, moving along the face) or `0x20` otherwise (moving away).
6. `d = n·v0`, edges un-normalised, kernel `0x0034ee40` (the same kernel without the region output) with `{c, r}`,
   `T`, `move`. Keep it when it returns non-zero and `0 ≤ t ≤ 1`: mesh contact with `t`, triangle, `+0x08` flags,
   `+0x0c` = `s`, copy of `T`.

**After the walk:**

1. Sort (with its smear); then `count = min(count, max)` (`0x00348f6c`).
2. **Option `0x20000`, one contact per plane** (`0x00348f84`-`0x00349044`): for each kept entry `i` in order, scan the
   entries after it; when `|n_i · n_j| > 0.9` (`0x3f666666`) swap entry `j` with the last entry and drop the last
   (`count −= 1`), then move on to `j + 1` **without testing the entry just swapped into `j`**. The order after
   removals is therefore no longer sorted. The cap to `max` happens first, so this can leave fewer than `max` even
   when more were found.
3. Output in list order: the common fields, `+0x10` unit move, `+0x90` point, `+0xb0` = the mesh flags, `+0x88` =
   the start distance `s`. No `+0xa0` slide vector, no `+0x84`.

**Differences from the capsule, in one list:** the sphere is the shape's own (no `+0.05` raise, no body or human
scale); walls are not singled out (the caller's `n.z` range does it, if asked); no low/thin rules; no corner slide;
the `max` argument is honoured; options `0x40` (keep backfaces and receding faces, flagged), `0x8000` and `0x20000`
exist; bit-3 triangles can be excluded. Reached through the walking sweep for a type-2 body (max 1, options 4 for a
human type mask else 0, min `n.z` −0.65) and through `IPhysics_CollideShape` with the query's own options and `n.z`
range (below).

**The query record of `IPhysics_CollideShape` / `IPhysics_OverlapShape`** (the fields the mesh path reads,
confirmed (code) at `0x0033e834`-`0x0033e8dc`, `0x0033ea64`-`0x0033eab4`, `0x0033ee48`-`0x0033eea0`):

| Offset | Meaning |
| --- | --- |
| `+0x00` | the body (passed as `body`; its owner `+0x34` becomes contact `+0x00`) |
| `+0x04` | the shape (its type byte `+0x30` picks the mesh function) |
| `+0x08` | a **pointer** to a box node (three quadwords copied, `0x0033e84c`-`0x0033e868`), used for the box when option `0x400`; the record also has `+0x0c`, `+0x14`, `+0x1c`, `+0x20`, `+0x24` and `+0x38` ([Unit 11](#u11-collide-shape)) |
| `+0x10` | options: `1` test the mesh, `2` the bodies; the whole word is passed on, so `4`, `8`, `0x40`, `0x8000`, `0x20000` reach the mesh function |
| `+0x18` | the most contacts to copy out |
| `+0x28`, `+0x2c` | minimum and maximum `n.z` (f12, f13) |

`CollideShape` inserts every returned contact with `Contact_InsertSorted` (duplicates from the smear included);
`OverlapShape` adds the returned value to its count without re-sorting.

### `0x003473c8` PhysicsMeshOverlap_Sphere {#u2-003473c8-physicsmeshoverlap-sphere}

Size `0x3f8`. Mesh overlap table `+0xf4`, type 2. Arguments: `out`, `room`, `worldBox`, (an unused fourth), `shape`,
`options`, `f12` min `n.z`, `f13` max `n.z`. Confirmed (code).

1. Sphere: centre shape `+0x50`, radius shape `+0x40`.
2. Per triangle (walk, visited bits): enabled (raw bit 0); normal; option `4` / `8` as for the sweep
   (`0x003475f8`, `0x0034761c`). **No side test and no flip**: one-sided faces are met from behind too, and the
   normal is the file's.
3. `SphereTriangle_Penetration` (`0x0034edd0`): kept when the point-triangle distance² < `r²`, key = depth
   `r − distance`.
4. Sort ascending by depth (shallowest first, with the smear); `n = min(count, room)`; write the kept entries
   **in reverse**, so `out[0]` is the deepest of the `n` shallowest; each with `+0x04` shape, `+0x44` 0, `+0x50`
   normal, `+0x80` depth, `+0xb0` 0, `+0xb4` triangle.
5. **Returns 1** when anything was found, 0 otherwise, **not the count** (`0x0034770c`-`0x0034777c`).
   `IPhysics_OverlapShape` adds the return to its count (`0x0033eea8`), so only `out[0]` is ever seen. In effect the
   overlap reports one contact: the deepest penetration when there are no more than `room` (inferred from the
   two facts above).

### `0x003473b8` and `0x003473c0`: the box and capsule mesh overlaps {#u2-003473b8-physicsmeshoverlap-box-and-0034}

8 bytes each: return 0. A box or a capsule never overlaps the level mesh; `IPhysics_OverlapShape` with such a shape
gets no mesh contacts. Confirmed (code).

### `0x003477c0` PhysicsBody_PushOutOfWalls {#u2-003477c0-physicsbody-pushoutofwalls}

Size `0x444`. `PushOutOfWalls(phys, body, sphere)` with `sphere = {centre, radius at +0x10}`; returns nothing.
Caller: `Human_PushOutInAir` (`0x0021a490`) only (characters.md #slopes). Confirmed (code).

1. Box `centre ± r` (`AABB_FromSphere` `0x00341660`), cells as above.
2. Per triangle: enabled; normal; **wall** `|n.z| ≤ 0.65`; side `n·centre − n·v0 < 1e-6` → one-sided skipped,
   two-sided flipped; then `dist² = PointTriangle_DistanceSq(centre, T)` (closest point on the whole triangle,
   edges and corners included); kept when `dist² < r²`, key `dist²`.
3. Sort; take **the nearest only** (`list[0]`, exact despite the smear).
4. Push = `(n.x, n.y, 0) × (r − sqrt(dist²))` with that triangle's (possibly flipped) normal: **horizontal only**,
   `n.z` is zeroed first (`0x00347b74`), so the push is shorter than `r − dist` on a sloped wall. Added to the body's
   pending push-out (body `+0x50`), and body flag `0x1000` set (`0x00347b70`), which makes `PhysicsBody_Sweep` apply
   the push-out without dividing it by the push weight (`0x0033e278`, the `0x1000` test).
5. The push is along the face normal even when the closest point is on an edge or corner (so its length is
   `r − dist`, not the depth along `n`). No contacts are produced; nothing happens with no wall in reach.

Compared with `CollisionMesh_SpherePush` (collision.md #sphere-push): 0.65 instead of cos 15°, one nearest wall
instead of the average of all, edge and corner distances count, and the push is horizontal and deferred to the next
sweep instead of moving the centre.

### Test ideas (synthetic meshes, `src/raycast/collision_builder.h`) {#u2-test-ideas-synthetic-meshes-src-raycast}

All with one-sided triangles unless stated, flags given as the file stores them, cell size 2 m.

1. **Wall stops a fast mover (no tunnelling).** Wall: two triangles in the plane `x = 0`, `y` −5..5, `z` 0..3, front
   facing −x. Capsule body, `r` = 0.5, feet at (−2, 0, 0), move (+10, 0, 0) in one call. Expect 1+ contacts, the
   first at `t = (2 − 0.5)/10 = 0.15`, normal (−1, 0, 0), point (−0.5, 0, 0). Stand-in today: the body ends beyond.
2. **Starting behind a one-sided wall passes.** Same wall, feet at (+1, 0, 0), move (−3, 0, 0): no contact. Set flag
   `0x2`: one contact, normal (+1, 0, 0), `t = (1 − 0.5)/3`.
3. **Moving away or along.** Feet at (−2, 0, 0), move (−1, 0, 0) or (0, 1, 0): no contact (`n·move ≥ −0.001`). Zero
   move: no contact.
4. **Low rule.** A vertical face 0.24 m tall (steepest-edge rise 0.24): no contact; 0.26 m: contact. Rise exactly 0.25:
   contact (`< 0.25` skips).
5. **Thin rule.** A vertical sliver: longest edge horizontal 4 m, third corner 0.2 m above it and its steepest edge
   rising 0.3 m (so the low rule passes): skipped. Same with the third corner 0.3 m off the line: kept. A triangle
   whose longest edge is vertical (`|unit z| ≥ 0.8`) is kept however thin.
6. **Wall threshold.** `n.z` = 0.65 is tested, 0.66 is not (capsule).
7. **Side uses the start.** A wall the sphere's centre is 1e-7 in front of (`s < 1e-6`) counts as behind: one-sided
   skipped, two-sided flipped (normal reversed in the contact).
8. **Disabled triangle.** Clear bit 0 (via `CollisionMesh_SetEnabledInBox`'s equivalent): no contact.
9. **Cells and visited bits.** A wall spanning four cells is reported once (one contact per triangle). A box whose
   max is past the grid clamps to the last cell.
10. **Sort.** Keys arriving 0.5, 0.6, 0.3 come out `[0.3, 0.5, 0.5]` (the smear); keys 0.3, 0.3 keep arrival order.
    `Contact_InsertSorted` of 0.2, 0.4, then 0.4 puts the new 0.4 after the old one; of 0.4 then 0.2 gives `[0.2, 0.4]`.
11. **Corner slide.** Vertical triangle `v0 = (0, 0, 0)`, `v1 = (0, 1, 0)`, `v2 = (0, 0, 1)` (normal +x; edge 2 runs
    down the wall's `y = 0` end), flags `bit0 | (2 << 14)`; it passes the low rule (edge 2 rises 1 m) and the thin rule
    (longest edge 1 not steep, `v0` 0.71 m from it). Grounded capsule body, `r` = 0.5, feet at `x` > 0.5, moving −x so
    that it meets the triangle's `y = 0` edge
    from `y < 0` (centre path at `y = −0.3`): one contact, region without bit 2, `S = (0, −1, 0) × max(0.5, |move|)`,
    `+0xb0 = 1`. Same with the owner airborne, or bits 14-15 = 3, or a face hit (centre path at `y = 0.5`): `S = 0`,
    `+0xb0 = 0`. The mirrored end (an up-running flagged edge at the other end) gives `+y`.
12. **Corner slide consumer.** In a full sweep pass, the pass velocity gains `S` (after removing any part against
    the move) and the reported velocity does not; a body with flag `0x8000` gains nothing.
13. **Sphere sweep options.** Same wall, sphere shape: `max` = 1 keeps only the nearest of two parallel walls; option
    `0x40` with the sphere behind keeps the face with `+0xb0 = 0x10`; option `0x20000` with two coplanar triangles hit
    keeps one; option 4 with min −0.65 drops a ceiling (`n.z` = −1).
14. **Overlap.** A sphere of `r` 0.5 at 0.3 from one wall and 0.1 from another, room ≥ 2: the call returns 1 and
    `out[0]` is the 0.4-deep wall. One-sided wall approached from behind: still found.
15. **Push-out.** Sphere `r` 0.5 at 0.3 m in front of a vertical wall: body `+0x50` gains `0.2 × n`, flag `0x1000` set.
    Two walls at 0.3 and 0.2: only the 0.2 one pushes (by 0.3). A wall sloped to `n.z` = 0.6: the push is
    `(n.x, n.y, 0) × (r − dist)`, shorter than `r − dist`. Sphere behind a one-sided wall: no push.

## Unit 3: Moving a body: sweep passes and contacts {#unit-3}

**What it is.** `PhysicsBody_Sweep`'s up-to-three passes and `PhysicsBody_ResolveContacts`' contact codes: where the
body is put, what is left of the move, how velocities slide, bounce and are written back, the push-outs, and the
airborne human's landing segment.

- **Coney:** `repo:src/physics/body_sweep.h` (done), which `Human::sweep` (`src/human/human.cpp`) runs. The loose
  objects' bounce (`src/world_objects/loose_objects.*`) is still its own (Unit 13).
- **Tests:** [Test ideas](#u3-tests), with a fake owner that answers fixed codes.

10 functions, 5148 bytes (4 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0033e278` | `PhysicsBody_Sweep` (tagged) | 1396 |
| `0x0033d9d8` | `PhysicsBody_ResolveContacts` (tagged) | 2208 |
| `0x0033d7b8` | `PhysicsVec_BackOffPlane` | 180 |
| `0x0033d870` | `Vec_RemoveNormalPart` (tagged) | 128 |
| `0x0033d8f0` | `PhysicsVec_Bounce` (tagged) | 228 |
| `0x0033d340` | `IPhysics_SweepBodyVsMesh` | 340 |
| `0x003414c0` | `AABB_ScaleAboutCentre` | 372 |
| `0x00341840` | `AABB_ExtendByMove` | 184 |
| `0x00341e18` | `PhysicsBody_GetContactScale` | 40 |
| `0x00342288` | `PhysicsHumanBody_GetContactScale` | 72 |

Notation: vectors are 4 floats; "xyz" operations leave `w` alone unless said. `a·b` is the xyz dot product.
Every "w = 1" below is the code writing 1.0 into `w` (it uses the VU constant register for it).

### Shared records and calls {#u3-records}

**Owner vtable slots.** GCC 2.x vtables: each slot is 8 bytes, a 16-bit `this` adjustment then the function
pointer. The pages so far name a slot by either offset; this table gives both. The functions are the human's
(vtable `0x0053f088`). Confirmed (code): the slot offsets at the call sites cited, the human's entries read from
the vtable.

| Slot (adjust / function) | What the sweep uses it for | Human's function | Called at |
| --- | --- | --- | --- |
| `+0x20` / `+0x24` | type mask (bit `0x40` a human) | | `0x0033e070`, `0x0033e090` |
| `+0x50` / `+0x54` | the owner's flags; for a human the 32-bit object flags word `+0x54`, bit `0x4000000` airborne | `0x004f4780` | `0x0033d3f4`, `0x0033e0bc`, `0x00342288` |
| `+0x70` / `+0x74` | **set velocity** `(owner, &vec4)` | `0x0023cf00` | `0x0033e6b0`, `0x0033e760` |
| `+0x98` / `+0x9c` | **get the cached transform** (sret, 32 bytes: position, rotation): the copy the owner took at the start of its update (human `+0x10` / `+0x20`) | `0x004ed860` | `0x0033e3e0` |
| `+0xa8` / `+0xac` | **get the transform** (sret, 32 bytes) from the authoritative table `0x00714b00` | `0x004ed828` | `0x0033e5ec`, `0x0033dd4c` |
| `+0xb0` / `+0xb4` | **set the transform** `(owner, &transform32)`: position and rotation; the human also refreshes its cached copy while airborne and re-poses its task | `0x0021b048` | `0x0033e668`, `0x0033ddac` |
| `+0xd0` / `+0xd4` | **attribute** `(owner, n)` → float: 1 weight, 7 push weight, 8 restitution ([Attributes](physics.md#attributes)) | `0x00221260` | `0x0033e314`, `0x0033dbec`, `0x0033def4` |
| `+0xf0` / `+0xf4` | **get the physics body** (0: none) | `0x0021cf78` (returns human `+0x1a0`) | `0x0033dac8`, `0x0033db70` |
| `+0xf8` / `+0xfc` | **contact handler** `(owner, contact)` → answer code | `Human_OnContact` `0x00219d50` | `0x0033daec` |
| `+0x118` / `+0x11c` | **after the move** `(owner)` | `Human_PushOutInAir` `0x0021a490` | `0x0033e7b0` |

So the "owner that does not want them" of the current [Contacts](physics.md#contacts) text is wrong: slot `+0xf0`
returns the other owner's body, and a contact whose other owner has **no body** is skipped. Confirmed (code) at
`0x0033dac8` (the skip) and `0x0033db70`-`0x0033db78` (the same call's result read as a body: flags `+0x40`).

**Body vtable slots used here** (body vtable at body `+0x74`): `+0x28` / `+0x2c` first shape (called with extra
arguments `(1, 3, 0)` or `(0, 0, 0)` that `PhysicsBody_GetShapes` `0x00341ae0` ignores: it returns `+0x30`);
`+0x48` / `+0x4c` contact scale `(body, contact)`; `+0x50` / `+0x54` "you were pushed" `(otherBody, contact,
&push, &move)`. **Shape vtable** (shape `+0x34`): `+0x28` / `+0x2c` pose `(shape, owner)`. Confirmed (code) at
`0x0033e404`, `0x0033e1d8`, `0x0033dcb8`, `0x0033e670`.

**Body fields** this unit reads or writes ([Bodies](physics.md#bodies)): `+0x34` owner, `+0x40` flags, `+0x44`
type mask, `+0x50` pending push-out (vec4), `+0x60` scale (vec3), `+0x70` "could not move" counter, `+0x74` vtable.
Body flags read here: `0x1` sweep against the bodies too, `0x400` write the slid velocity back, `0x1000` do not
divide the push-out by the push weight, `0x8000` no corner slide, `0x80000000` dirty (set). Confirmed (code) at the
addresses in the functions below.

**Contact fields** used by this unit (0xc0 bytes, [Contacts](physics.md#contacts)):

| Offset | Meaning | Read / written here |
| --- | --- | --- |
| `+0x00` | own owner | written by the insert; read for attributes and the type mask |
| `+0x04` | own shape | re-posed after the body is moved |
| `+0x40` | other owner, 0 for the level | |
| `+0x44` | other shape, 0 for the level | the generic contact scale tests it |
| `+0x50` | unit normal, pointing out of the surface **towards the mover** | inferred from the code 4 push direction and the mesh sweep's "move goes into the face" test; confirmed (code) that the handlers treat it so |
| `+0x80` | fraction `f` of this pass's move at the contact | |
| `+0xa0` | slide vector (a displacement) | |
| `+0xb0` | contact flags: `0x1` add the slide to the move, `0x2` store the slide as the next push-out, `0x80` a human's landing | |
| `+0xb4` | mesh triangle (10-byte record, flags `u16` at `+0x06`), 0 for a body | |

**The move state** (locals of the sweep, one set per call, kept across the passes). Confirmed (code) at
`0x0033e3c0`-`0x0033e418`:

| Name | Start value | Meaning |
| --- | --- | --- |
| `V` | `v + push/dt/k` | the **move velocity**: what the passes move by; slid, bounced, gets corner slides |
| `v` | the caller's velocity | the **real velocity**: slid and bounced like `V`, written back to the owner |
| `moveFlags` | 0 | `0x1` a slide was stored in `+0x50` this move; `0x2` write `v` back; `0x4` a contact scale applies |
| `scale` | 0.0 | the largest contact scale met (with `0x4`) |
| `passes` | 0 | passes done, 0 to 3 |
| `found` | 0 | contacts found over all passes (all of them, including skipped and code 0 ones) |
| `last` | 0 | the last resolution's return |

### PhysicsBody_Sweep {#u3-sweep}

`0x0033e278`, 0x574 bytes. `PhysicsBody_Sweep(float dt, IPhysics* phys, Body* body, const vec4* v, vec4* out)`;
returns nothing. `v` is only read; the velocity goes back through the owner. `out` receives the displacement
(sometimes not written at all, below). Confirmed (code) for every step unless marked.

1. **Move velocity** (`0x0033e2bc`-`0x0033e364`). `p = body+0x50 × (1/dt)` (xyz; `p.w` keeps `+0x5c`). Unless body
   flag `0x1000` is set, `p = p × (1 / owner.attribute(7))` (`0x0033e314`; the attribute call happens even when the
   result is not needed later). `V = v + p` (xyz, `w = 1`). The real velocity `v` starts as the caller's `v`,
   unchanged.
2. **Clear the push-out** (`0x0033e3a0`): body `+0x50 = (0, 0, 0, 1)` (the constant at `0x005116c0`), **always**,
   before the next test. So a push-out is used once, by the next sweep after it was added, even if that sweep does
   not move.
3. **Too slow to move** (`0x0033e39c`): if `|V|² ≤ 1e-6` (`0x358637bd`), return at once: nothing else happens (no
   dirty flag, no velocity write, `out` **not written**, no `+0x11c` call, `+0x70` unchanged).
4. **Set up** (`0x0033e3ac`-`0x0033e454`): set body flag `0x80000000` (dirty). `start` = the owner's **cached**
   transform (slot `+0x98`). `shape` = the body's first shape (body slot `+0x28`). If the shape is not enabled
   (shape flags `+0x32` bit `0x1` clear), skip the passes and go to step 6 with `passes = 0`, `found = 0`
   (`0x0033e428`): the body then moves freely by `V × dt`. Else `box` = the shape's world AABB
   (`PhysicsBody_ComputeBounds` `0x00341638`), computed **once** at the start position and not moved between passes.
5. **The passes** (loop at `0x0033e470`-`0x0033e5bc`). Each pass:
    1. `D = V × dt` (xyz; `dt` is the caller's, **never reduced**: the address of `dt` is handed to the resolution,
       which never reads it). If `|D|² ≤ 1e-6`, set `passes = 3` and leave the loop (`0x0033e4b8`, `0x0033e694`).
    2. Two boxes from `box` (AABB + the node fields `+0x20`): `qbox = AABB_ExtendByMove(box, D)`; and
       `sbox = AABB_ExtendByMove(AABB_ScaleAboutCentre(box, body+0x60), D)`. **`sbox` is never passed on**: both
       queries get `qbox` (`0x0033e518`, `0x0033e550`), and their box readers read only `+0x00`-`+0x18`
       (`0x00347170`, `0x0033feb0`). So body `+0x60` has no effect on which triangles or bodies are tested here (it
       does widen the capsule's mesh sphere, [Characters](characters.md#walls)). Confirmed (code).
    3. `count = 0`; `IPhysics_SweepBodyVsMesh(phys, &qbox, body, shape, contacts, sorted, &count, &D)`
       ([below](#u3-mesh)); then, if body flag `0x1` is set, the body sweep `0x0033d498` with the same arguments
       ([Unit 11](#unit-11): it adds its contacts to the same sorted list).
    4. `last = PhysicsBody_ResolveContacts(phys, body, sorted, count, &D, &V, &v, &dt, &moveFlags, &scale)`;
       `found += count`.
    5. If `last ≤ 0`, leave the loop. Else `passes += 1`; if `passes` is now 3, leave the loop; else next pass.

   The contact array holds 50 contacts (0xc0 each) and the sorted list 50 pointers, fresh for each pass (the count
   restarts at 0). Confirmed (code) for the layout (`sp+0x110`, `sp+0x2690`).
6. **After the passes** (`0x0033e5c4`):
    - `last < 0`: **return at once** (`0x0033e5c4`): no final move, no velocity write, `out` not written, no
      `+0x11c` call, `+0x70` unchanged. Whatever the aborting contact's code did (it may have moved the body and
      changed velocities, and `+0x50`) stays.
    - (Body or owner null: return; the start already called the owner, so this is not reachable in practice.)
    - `T` = the owner's transform (slot `+0xa8`), which includes what the resolutions moved.
    - **Moved** when `passes < 3` **or** `found == 0` (`0x0033e5f4`-`0x0033e604`): `T.position += V × dt` (xyz,
      `w = 1`), set the transform (slot `+0xb0`; the rotation is the one just read), re-pose the shape (shape slot
      `+0x28` with the owner), body `+0x70 = 0` (`0x0033e68c`).
    - **Stuck** otherwise (`passes == 3` and at least one contact was found): the body is **not** moved by the rest
      (it stays where the last resolution left it); `v.x = v.y = 0` (`v.z` kept) and the owner's velocity is set to
      `v` (slot `+0x70`) **whatever the body flags** (`0x0033e698`-`0x0033e6b0`); body `+0x70 += 1`.
    - **Contact scale** (`0x0033e6c4`): if `moveFlags & 0x4`: `v.x *= scale`, `v.y *= scale`, and `v.z *= scale` only
      when `v.z > 0`; the same on `V` (dead afterwards).
    - **Write-back** (`0x0033e734`): if body flag `0x400` **and** `moveFlags & 0x2`, set the owner's velocity to `v`.
    - `*out = T.position − start.position` (xyz, `w = 1`) (`0x0033e788`). `T` is the position just set (moved) or
      just read (stuck). `start` is the owner's cached copy, so `out` is the displacement since the owner's update
      began, not only this sweep's (they are the same for a human, who refreshes the copy before moving).
    - Call the owner's slot `+0x118` (`0x0033e7b0`).

So the three cases of "how far did it go" are (confirmed (code), the summary inferred):

| Pass result | Where the body ends |
| --- | --- |
| no contact or every contact answered 0 | start + `V × dt` |
| a contact stopped pass `k` (`k` = 1 or 2), and pass `k + 1` had no stopping contact | the advance of each stopping contact along its normal (below), + the slid `V × dt` in full |
| three passes each stopped, or the slid move fell under 1e-6² | the advances only; `v.xy` zeroed, counter up |

Note what this means: **the move is not shortened by the contact fraction**. Each pass sweeps the whole slid
`V × dt` again from wherever the body now is. A body that slides along a wall therefore travels its normal-direction
advance plus a full slid step in one update. Confirmed (code): `dt` is written once (`0x0033e2f0`) and re-read each
pass (`0x0033e5c0`); the resolution never reads the `dt` pointer (no use of its fourth register argument).

A body whose move is slower than `0.001 / dt` (0.03 m/s at 30 Hz) skips the passes and moves freely by `V × dt`
with `+0x70 = 0` (pass 1's `|D|² ≤ 1e-6` with `found = 0`). Confirmed (code); its effect is inferred.

A head-on hit whose slide leaves `V ≈ 0` ends the loop on the next pass's `|D|²` test with `passes = 3` and
`found > 0`: **stuck**, so the counter goes up each update a body is pressed straight into a wall. Confirmed (code);
this is what the human's "stuck in the air" rule reads ([Characters](characters.md#ground)).

### PhysicsBody_ResolveContacts {#u3-resolve}

`0x0033d9d8`, 0x8a0 bytes. `int PhysicsBody_ResolveContacts(IPhysics* phys, Body* body, Contact** sorted, int count,
const vec4* D, vec4* V, vec4* v, float* dt /*unused*/, u32* moveFlags, float* scale)` (the last two on the stack).
Returns 0, 1 or −1. Confirmed (code) for all of it.

At the start, `u = D / |D|` (reciprocal square root; `D` is never zero here). Then the contacts are walked **in
sorted order** (nearest first; equal fractions keep their insertion order, [the insert](#u3-insert)):

1. **Skip** the contact (go to the next) when (`0x0033da80`-`0x0033dad4`):
    - it has a triangle (`+0xb4` ≠ 0) whose flags (`+0x06`) lack bit `0x1` (disabled since it was found); or
    - the moving body has no owner; or
    - it has another owner (`+0x40` ≠ 0) whose body (slot `+0xf0`) is null.
2. `answer = owner.onContact(contact)` (slot `+0xf8`). `code = answer & 0xffff`.
3. **Code 0** (`0x0033db04`): if `answer & 0x10000`, return 0; if `answer & 0x40000`, return −1; otherwise go on with
   the next contact. Nothing else: **`0x20000` with code 0 does nothing** (the scale rule is in the tail, which code
   0 never reaches).
4. **Codes 1 to 4** act ([below](#u3-codes)), then the tail. **Codes 5 and above** go straight to the tail.
5. **Tail** ([below](#u3-tail)), then: return **0** if `answer & 0x10000`; else **−1** if `answer & 0x40000`; else
   **1**. So any non-zero code ends the walk: later contacts of the pass are never seen.
6. All contacts walked (or `count ≤ 0`): return 0.

What the returns mean to the sweep: 1 "stopped, slide and try again", 0 "done, move the rest", −1 "abort".

### The codes {#u3-codes}

Let `n` = contact `+0x50`, `f` = contact `+0x80`, and **the advance** be:

> `d = f × (D · n)`. If `d < −0.01`, the body's position (owner slot `+0xa8`) becomes `position + n × (d + 0.01)`,
> set back through slot `+0xb0` with its rotation unchanged, and the contact's own shape (`+0x04`) is re-posed
> (shape slot `+0x28` with the owner). Otherwise the body is not moved.

Because `n` points at the mover and `d` is negative, `n × (d + 0.01)` moves the body **towards the surface, along
the normal only**, by `|d| − 0.01`: it stops **0.01 m short, measured along the normal**, of where the swept shape
touched. The part of the move along the surface up to the contact is **not** applied (the next pass's slid move
covers the surface direction). −0.01 is `0xbc23d70a`, 0.01 `0x3c23d70a`. Confirmed (code) at `0x0033dcdc`-`0x0033ddc8`
(code 1), `0x0033ddec`-`0x0033dedc` (code 2), `0x0033df40`-`0x0033e02c` (code 3). The same formula is the stand-alone
`PhysicsVec_BackOffPlane` (below), which the resolution does not call. That this 0.01 is the 0.006-0.010 m
clearance measured on [Characters](characters.md#walls) is inferred (the walls page measured stops, not the code).

| Code | Effect, in order |
| --- | --- |
| 1 | the advance; then `Vec_RemoveNormalPart(V, n)` and `Vec_RemoveNormalPart(v, n)` |
| 2 | the advance; then `e = ownOwner.attribute(8)` and `PhysicsVec_Bounce(V, n, e)`; `e` is asked again and `PhysicsVec_Bounce(v, n, e)` |
| 3 | the advance; then `V = v = (0, 0, 0, 1)` |
| 4 | `other = otherOwner.body` (slot `+0xf0`). If `other.flags & 0x200`: exactly code 1. Else: `push = n × 0.5 × (D · n)` (the **whole** pass move `D`, not `f × D`), `push *= w_m / (w_o × k_o)` with `w_m` = own owner's attribute 1, `w_o` = other owner's attribute 1, `k_o` = other owner's attribute 7 (asked in that order); `other+0x50 += push` (xyz, `w = 1`); call `other`'s body slot `+0x50` with `(other, contact, &push, &D)`; then exactly code 1 (the advance and both slides) |

Confirmed (code): code 4 at `0x0033db60`-`0x0033dcd8`, the 0.5 is `0x3f000000` at `0x0033dbac`. "Own owner" is
contact `+0x00`, "other owner" contact `+0x40`.

### The tail {#u3-tail}

Run for every code that is not 0. Confirmed (code) at `0x0033e058`-`0x0033e204`:

1. **Ask for the velocity write-back**: `moveFlags |= 0x2`, **except** when
    - the contact is with the level (`+0x40` = 0) and its triangle has flag `0x100` (bit 8; 20,231 of the disc's
      150,567 triangles, [Collision](collision.md#triangles)); or
    - the mover and the other owner are both humans (type mask `0x40`, slot `+0x20`) and **both** airborne (state
      `0x4000000`, slot `+0x50`).

   (A level contact must carry a triangle: the code reads `+0xb4` without a null test.)
2. **Contact flag `0x1`** (corner slide into the move): unless the mover is a human in any state of `0x18003ff0`
   (`Human_IsInPairOrBlock` `0x00227fd8`: grab, held, mug, tackle, throw, block...) or the body has flag `0x8000`:
   `s = contact+0xa0`, `Vec_RemoveNormalPart(s, u)` (drops the part of `s` that goes against the pass's direction),
   `V += s` (xyz, `w = 1`). **Only `V`**, not `v`. Then go to step 5: **the scale rule is skipped** (as it is when
   the human or `0x8000` test refuses).
3. Else, **contact flag `0x2`** (slide kept for the next move): `s = contact+0xa0`, `Vec_RemoveNormalPart(s, u)`;
   if `moveFlags & 0x1` is clear, set it and store `body+0x50 = s` (whole vec4). So only the **first** such contact
   of the move is kept, and since the sweep cleared `+0x50` before the passes, it survives as the next sweep's
   push-out (`s / dt / k` in the next move velocity).
4. **Scale rule** (flags `0x1` not set): if `answer & 0x20000`: `moveFlags |= 0x4`, and
   `*scale = max(*scale, body.contactScale(contact))` (body slot `+0x48`; `max.s` at `0x0033e1f4`). The **largest**
   scale met wins, starting from 0.
5. The return ([above](#u3-resolve), step 5).

Who sets the contact flags: the capsule's mesh sweep writes `0x1` when it built a corner slide (`0x00348360`,
`0x00348438`), the capsule-box sweep writes `0x1` for its four corner regions with a unit diagonal slide in `+0xa0`
(`0x00345078`, [Unit 12](physics-kernels.md#unit-12)), the sphere and box mesh sweeps copy their hit's flags word
(`0x003490f0`,
`0x003489d0`; the box's word is always 0, `0x003488e0`, so a box mesh contact never carries `0x1` or `0x2`), the landing
contact is `0x80` ([below](#u3-mesh)). Whether any sweep produces `0x2` is not traced. Confirmed (code) for the stores
cited.

### PhysicsVec_BackOffPlane {#u3-backoff}

`0x0033d7b8`, 0xb4 bytes. `void PhysicsVec_BackOffPlane(vec4* p, const vec4* move, const vec4* n, float f)`:
`d = (move × f) · n`; if `d < −0.01`, `p.xyz += n × (d + 0.01)` and `p.w = 1`; else nothing. The same advance as the
resolution's codes 1-3 (which inline it). Its only caller is `Car_UpdateLoosePart` (`0x003881b4`). Confirmed (code).
The current plate comment's "moves the point back" describes it from the moved point's side; from the start
position, as the resolution uses it, it moves the point forward along `−n`.

### Vec_RemoveNormalPart {#u3-remove}

`0x0033d870`, 0x80 bytes. `void Vec_RemoveNormalPart(vec4* v, const vec4* n)`: `d = v · n`; if `d < 0` (strictly),
`v.xyz −= n × d` and `v.w = 1`; else `v` is unchanged (its `w` too). `n` is assumed unit (no normalising).
Confirmed (code).

### PhysicsVec_Bounce {#u3-bounce}

`0x0033d8f0`, 0xe4 bytes. `void PhysicsVec_Bounce(vec4* v, const vec4* n, float e)`: `d = v · n`, computed once.
If `d < 0`, `v.xyz −= n × d`. Then, **in both cases**, `v.xyz −= n × (d × e)`, `v.w = 1`. Confirmed (code) at
`0x0033d924`-`0x0033d9b4`. So:

- `d < 0` (going in): `v −= n d (1 + e)`, the reflection with restitution `e` (as the page says);
- `d ≥ 0` (already going out): `v −= n d e`: the outgoing normal speed is **reduced** to `(1 − e)` of itself. The
  current page's "when `v·n < 0`" misses this half.

### IPhysics_SweepBodyVsMesh {#u3-mesh}

`0x0033d340`, 0x154 bytes. `void IPhysics_SweepBodyVsMesh(IPhysics* phys, const AABB* box, Body* body, Shape* shape,
Contact* contacts, Contact** sorted, int* count, const vec4* D)`. Confirmed (code):

1. **The shape's mesh sweep** through `0x0033d2d8(phys, &contacts[*count], box, body, shape, D)`, which picks the
   function from IPhysics `+0xe0 + 4 × shape.type` (shape `+0x30`, a byte) and passes it, besides those: a 1, the
   wall threshold **−0.65** (`0xbf266666`), a second float 0, and an option `4` when the body's type mask has `0x40`
   (a human; `(mask >> 4) & 4`), else 0 (`0x0033d2e0`-`0x0033d320`). No function for the type: no contact. It returns
   1 when the type's sweep reported any hit. The type's sweep writes its hits, nearest first (it sorts them,
   `0x00347300`), into consecutive contacts from `&contacts[*count]`; **only the first is inserted** (step 2), so
   **one mesh contact per pass, the nearest**. Each written contact has `+0x04` shape, `+0x44` 0, `+0x50` normal,
   `+0x80` fraction, `+0x90` the shape's position at the contact, `+0xa0` slide, `+0xb0` flags, `+0xb4` triangle
   (capsule: `0x00348410`-`0x003484e4`). The details are in [Unit 2](#u2-00347c08-physicsmesh-sweepcapsule).
2. **Insert it** (when step 1 found one): `Contact_InsertSorted(phys, &contacts[*count], sorted, count, ownOwner =
   body+0x34, otherOwner = 0, ownShape = shape, otherShape = 0)`.
3. **The landing segment**, only when the body's type mask has `0x40` **and** the owner's state has `0x4000000`
   (airborne) (`0x0033d3dc`-`0x0033d40c`): `Human_LandingTest(owner, contacts, count, D)` (`0x0023e408`, below)
   writes `contacts[*count]`; if it returns 1, set that contact's `+0xb0 = 0x80` and insert it like step 2. (The
   current text "when body flag `0x40` is set" on Characters is the **type mask** `+0x44`, not body flag `+0x40`.)

A mesh contact found in step 1 and a landing contact can both be in one pass's list; the landing one is written after
the first is inserted, so they never share a slot. Confirmed (code).

### Human_LandingTest {#u3-landing}

`0x0023e408`, 0x2dc bytes. `int Human_LandingTest(Human* h, Contact* contacts, int* count, const vec4* D)`; returns 1
with a contact written at `contacts[*count]` (the count is **not** incremented here), else 0. Confirmed (code):

1. `A` = the feet: the position in the transform table (`0x00714b00 + 32 × h+0x92`).
2. **Raised feet**: when (`0x00227f68`(h) is true and the state lacks `0x1000000000`, a long fall) **or** the human
   holds with flag `0x800` (`0x00226660`): `A.z += 0.4 × Human_GetScale(h)` (`0x3ecccccd`), but at most
   `h+0x560 + 0.7` (`0x3f333333`).
3. `B` = the upper point, human vtable slot `+0x60` / `+0x64` (`0x00226a20`: the feet + `+0x4e8` − 0.16 × scale,
   [Characters](characters.md#falling)).
4. `A += D` (the moved, possibly raised, feet).
5. The ray goes from the **higher** of `A` and `B` to the lower: origin `B`, end `A`, unless `B.z < A.z`, then the
   two swap. `L` = the distance between them, direction = (end − origin) / `L`.
6. `WorldManager_RayCast(world 0x005147c4, {origin, dir, L}, &hit, 0, 0)` (mask 0: every enabled triangle).
7. No hit, or the hit normal's `z ≤ 0.65` (`0x3f266666`): return 0.
8. Else: human `+0x1d8` = the hit material, `+0x230` = the hit normal (vec4); and in the contact: `+0x04` = the
   body's first shape (body `h+0x1a0`, slot `+0x28` with `(0, 0, 0)`), `+0x50` = the hit normal, `+0x44` = 0,
   `+0xa0` = `(0, 0, 0, 1)`, `+0xb4` = the hit triangle, `+0xb0` = 0, `+0x84` = 0 (the point `+0x90` is not
   written), and the fraction, with `h0` = human vtable slot `+0x58` / `+0x5c` (1.0, `0x004ed818`):
   `f = 0` when the hit distance `< h0`, else `f = (dist − h0) / (L − h0)`. Return 1.

The fraction is measured on the segment **beyond 1.0 m from the origin**, not along `D`: with the origin 0.88 m above
the starting feet, `f` is `(g − 0.12) / (|D.z| − 0.12)` for a vertical fall with the feet `g` above the ground (worked
example in the tests). Confirmed (code) for the formula; the reading is inferred.

### Contact_InsertSorted {#u3-insert}

`0x0033d718` (called by both queries). `(phys, Contact* c, Contact** sorted, int* count, ownOwner, otherOwner,
ownShape, otherShape)`: writes `c+0x00`, `+0x40`, `+0x04`, `+0x44`; finds the first entry whose fraction is
**strictly greater** than `c`'s, shifts it and the rest up one, puts `c` there (or at the end), `*count += 1`. So
equal fractions keep their insertion order: the mesh contact before a landing contact before body contacts at the
same fraction. It does **not** check the 50 limit. Confirmed (code).

### AABB_ScaleAboutCentre {#u3-scale-box}

`0x003414c0`, 0x174 bytes. `void AABB_ScaleAboutCentre(AABB* box, const vec3* s)`: if `s.x`, `s.y` and `s.z` are
all exactly 1.0, nothing. Else, per axis, `c = (min + max) / 2`, `h = (max − min) / 2 × s`, and the new
`min = min(c + h, c − h)`, `max = max(c + h, c − h)` (so a negative scale still gives a valid box); `w = 1`.
Confirmed (code). In the sweep its result is unused ([above](#u3-sweep), step 5.2).

### AABB_ExtendByMove {#u3-extend}

`0x00341840`, 0xb8 bytes. `void AABB_ExtendByMove(AABB* box, const vec4* d)`: `a = min + d`, `b = max + d`; new
`min = min(min, a, b)`, new `max = max(max, a, b)`, per axis (xyz). Confirmed (code).

### PhysicsBody_GetContactScale {#u3-contact-scale}

`0x00341e18`, 0x28 bytes, body vtable `+0x48`. `float (Body*, const Contact* c)`: **1.0** when `c+0x44` (the other
shape) is non-zero, a body; **0.98** (`0x3f7ae148`) when it is 0, the level. Confirmed (code).

### PhysicsHumanBody_GetContactScale {#u3-human-scale}

`0x00342288`, 0x48 bytes, human body vtable `+0x48`. `float (Body*, const Contact*)`: **0.25** (`0x3e800000`) when
the owner's state (slot `+0x50`) has `0x4000000` (airborne), else **1.0**; the contact is not read. Confirmed
(code).

Who answers `0x20000` ([Contacts](physics.md#contacts)): the human (level and objects; `0x20004` against a human in
the air), the world object on a floor (`0x20002`). So a world object sliding on a floor keeps 0.98 of its horizontal
speed per sweep (and of an upward `v.z`), written back because world-object bodies carry flag `0x400` (`0x80000500`,
[Bodies](physics.md#bodies)). An airborne human's contact scale is 0.25, but it reaches the human only when its
body has `0x400` (a player). Inferred from the code paths above.

### Corrections to the current pages {#u3-corrections}

Each confirmed (code) at the address given:

- physics.md #contacts, code 1 "moves the body back to the contact": it **advances** the body along the normal to
  0.01 m short of the touch, only when it would go more than 0.01 m, and drops the move's surface-parallel part
  (`0x0033dcdc`). Codes 2 and 3 do the same, not "moves back".
- #contacts, `0x10000` "go on with the next contact (return 0)": it **ends** the walk and returns 0; going on with
  the next contact is code 0 without `0x10000` / `0x40000` (`0x0033db04`, `0x0033e1fc`).
- #contacts, `0x20000` "keeps the smallest contact scale": the **largest**, and only for a non-zero code whose
  contact has no flag `0x1` (`0x0033e1f4`, `0x0033e17c`).
- #contacts, "+0xb0 has bit 2 removes the normal part of its slide vector and stores it": flag `0x2` stores (once
  per move, `moveFlags 0x1`); flag `0x1` adds it to the move velocity instead; in both the part removed is the one
  against the **move's direction**, not the contact normal (`0x0033e148`, `0x0033e18c`).
- #contacts, `+0xf0` "owner that does not want them": the other owner's body; skipped when it has none.
- #sweep, "still blocked after three, the body stays, its horizontal velocity is zeroed (owner vtable `+0x74`)": the
  body stays where the **last pass's advance** put it, and the zeroed velocity is the slid `v` written whatever the
  body flags; the same happens when a slide leaves no move (`0x0033e4b8`).
- #sweep step 1, "scaled about its centre by body `+0x60` ... grown to cover the move": the scaled box is computed and
  discarded; the queries get the unscaled box grown by the move (`0x0033e518`).
- #sweep, "The velocity the slides leave is written back ... only when body flag `0x400` is set": and only when some
  stopping contact of the move asked for it (`moveFlags 0x2`: not a level triangle with flag `0x100`, not two
  airborne humans) (`0x0033e734`).
- #sweep / characters.md #ground, "the body's pending push-out (divided by dt)": also divided by the owner's
  attribute 7 unless body flag `0x1000` (`0x0033e2e4`).
- characters.md #falling, "run from `0x0033d340` when body flag `0x40` is set": the body's **type mask** `+0x44`
  bit `0x40` and the owner airborne (`0x0033d3dc`).
- `PhysicsVec_Bounce` also damps an outgoing normal speed by `(1 − e)` ([above](#u3-bounce)).

### Test ideas (synthetic) {#u3-tests}

A fake owner (answers a fixed code per call, attributes 1 = 82, 7 = `k`, 8 = `e`; records every set-velocity,
set-transform and after-move call), a fake mesh query that returns a scripted contact per pass (normal, fraction,
a triangle with flags `0x0001` unless said, contact flags), no body query (body flag `0x1` clear). Start position
origin, `dt = 1/30`, push-out 0, `k = 1`, body flags `0x400` unless said. The values below follow from the spec
(computed by hand, float rounding aside).

1. **Head-on wall, code 1.** `v = (3, 0, 0)`, contact `n = (−1, 0, 0)`, `f = 0.5` on pass 1. `D = (0.1, 0, 0)`,
   `d = −0.05` → position `(0.04, 0, 0)`; `V = v = 0`; pass 2 `|D|² = 0` → stuck: velocity set to `(0, 0, 0)` (stuck
   branch) and again by the write-back; `+0x70` 0 → 1; `out = (0.04, 0, 0)`; one after-move call.
2. **Same, answer `0x10001`.** Same position `(0.04, 0, 0)`; resolution returns 0 → moved branch (by `V × dt = 0`);
   `+0x70` reset to 0; velocity `(0, 0, 0)` written once (write-back). Shows `0x10000` ends the walk without stuck.
3. **Same, answer `0x40001`.** Position `(0.04, 0, 0)` (the advance happened); no velocity call, `out` keeps its
   sentinel, `+0x70` unchanged, no after-move call.
4. **Wall at an angle.** `v = (3, 0, 0)`, `n = (−0.6, 0.8, 0)`, `f = 0.5`, code 1, pass 2 no contact. Advance
   `n × (−0.03 + 0.01)` = `(0.012, −0.016, 0)`; `V = v = (1.92, 1.44, 0)`; pass 2 `D = (0.064, 0.048, 0)`, no contact →
   end `(0.076, 0.032, 0)`; `+0x70 = 0`; velocity `(1.92, 1.44, 0)`; `out = (0.076, 0.032, 0)`. With body flags 0
   (an AI): the same position and **no** velocity call.
5. **Level triangle with flag `0x100`.** As test 4 with triangle flags `0x0101`: same position, **no** velocity call
   (write-back not asked).
6. **Corner.** `v = (3, 3, 0)`, pass 1 `n1 = (−1, 0, 0)`, `f = 0.5`; pass 2 `n2 = (0, −1, 0)`, `f = 0.5`; code 1 both.
   Pass 1: `D = (0.1, 0.1, 0)`, `d = −0.05` → `(0.04, 0, 0)`, `V = (0, 3, 0)`. Pass 2: `D = (0, 0.1, 0)` → `(0.04,
   0.04, 0)`, `V = 0`. Pass 3: `|D|² = 0` → stuck: `+0x70 = 1`, velocity `(0, 0, 0)`; `out = (0.04, 0.04, 0)`.
7. **Three-pass limit.** As test 4 but the fake returns the same contact (code 1, `f = 0.5`) on all passes. Pass 1 as
   test 4; passes 2 and 3: `D · n = 0` → no advance, `V` unchanged (`d = 0` is not `< 0`). After pass 3 → stuck:
   position stays `(0.012, −0.016, 0)` (the slid `V × dt` is **never** applied); the velocity written is the slid
   `v = (1.92, 1.44, 0)` with x and y zeroed, `(0, 0, 0)`; `+0x70 = 1`; `out = (0.012, −0.016, 0)`.
8. **Code 0 and `0x20000` alone.** Every contact answered `0x20000`: position `D = (0.1, 0, 0)`, no velocity call,
   `+0x70 = 0`, no scale.
9. **Push-out.** `v = 0`, `+0x50 = (0.02, 0, 0)`, no contacts: position `(0.02, 0, 0)`, `+0x50` reads `(0, 0, 0, 1)`
   afterwards, no velocity call. With `k = 2`: `(0.01, 0, 0)`. With `k = 2` and body flag `0x1000`: `(0.02, 0, 0)`.
10. **Too slow.** `v = (0.0005, 0, 0)`, `+0x50 = (0.00001, 0, 0)`: `V = (0.0008, 0, 0)`, `|V|² = 6.4e-7` → nothing
    moves, `+0x50` cleared,
    dirty flag **not** set, `out` keeps its sentinel, no after-move call.
11. **Slow but above the cut.** `v = (0.02, 0, 0)`, a contact scripted for pass 1: `|V|² = 4e-4 > 1e-6` but
    `|D|² ≈ 4.4e-7 ≤ 1e-6` → no query is made (the fake records no call), position `(0.000667, 0, 0)`, `+0x70 = 0`.
12. **Bounce on a floor (world object).** Body type mask 0, flags `0x80000500`, `e = 0.1`, `v = (1, 0, −5)`, floor
    `n = (0, 0, 1)`, `f = 0.4`, answer `0x20002`, pass 2 no contact. `D = (0.0333, 0, −0.1667)`, `d = −0.0667` →
    `z = −0.0567`; `v = V = (1, 0, 0.5)`; pass 2 `D = (0.0333, 0, 0.0167)` → end `(0.0333, 0, −0.04)`; scale 0.98
    (level contact) → velocity written `(0.98, 0, 0.49)`.
13. **Bounce going out.** `PhysicsVec_Bounce((0, 0, 2), (0, 0, 1), 0.1)` → `(0, 0, 1.8)`;
    `PhysicsVec_Bounce((0, 0, −2), (0, 0, 1), 0.1)` → `(0, 0, 2.2)`.
14. **Scale, `v.z` rule.** Test 4 with answer `0x20001`, a generic body (type mask 0) and the starting
    `v = (3, 0, −1)`: the slide leaves `v = (1.92, 1.44, −1)`; the write-back is `(1.8816, 1.4112, −1)` (`v.z < 0`
    kept). With `v = (3, 0, 1)`: `(1.8816, 1.4112, 0.98)`. The same with a human-body fake airborne (state
    `0x4000000`): scale 0.25, `(0.48, 0.36, ·)`; on the ground 1.0. With contacts at 0.98 and 1.0 in one move: 1.0
    (the largest). The position is not affected by the scale.
15. **Code 4 push.** Mover `v = (3, 0, 0)`, contact with another owner (body flags 0, `+0x50 = 0`), `n = (−1, 0, 0)`,
    `f = 0.5`, attributes 1 = 82 for both, other `k = 1`. Other's `+0x50 = (0.05, 0, 0, 1)` (`0.5 × (−0.1) × n`),
    its slot `+0x50` called once with that push and `D`; mover as test 1 (position `(0.04, 0, 0)`). Other `k = 1e9`:
    push `≈ 5e-11`. Other body flag `0x200`: no push, no call, mover as test 1.
16. **Corner slide (contact flag `0x1`).** Test 1 with contact flags `0x1`, `+0xa0 = (0, 0.5, 0)`: after code 1,
    `V = (0, 0.5, 0)`, `v = 0`; pass 2 `D = (0, 0.01667, 0)`, no contact → end `(0.04, 0.01667, 0)`, `+0x70 = 0`,
    velocity `(0, 0, 0)`; no scale even with `0x20001`. With `+0xa0 = (−0.5, 0.5, 0)`: the `−0.5` along `−u` is
    removed first → same result. With body flag `0x8000`: `V = 0` → stuck as test 1.
17. **Stored slide (contact flag `0x2`).** Test 4 with flags `0x2`, `+0xa0 = (0, 0.05, 0)`: same position and
    velocity; afterwards body `+0x50 = (0, 0.05, 0, ·)`; a second flag-`0x2` contact in pass 2 does not overwrite it.
18. **Landing segment fraction.** Human, feet at `z = 0.3`, upper point 0.88 m above, `D = (0, 0, −0.5)`, ground plane
    `z = 0`, `n = (0, 0, 1)`: `L = 1.38`, hit at 1.18 → `f = 0.18 / 0.38 = 0.4737`, contact flags `0x80`; with
    code 1 the advance is `z += 0.4737 × (−0.5) + 0.01` → feet at `0.0732`. Feet at `z = 0.05`, `D = (0, 0, −0.2)`:
    hit at 0.93 < 1.0 → `f = 0`, no advance. A ground normal `z = 0.6`: no contact.
19. **Insert order.** Fractions 0.5, 0.2, 0.5 inserted in that order → sorted 0.2, first 0.5, second 0.5.
20. **AABB helpers.** Scale `[(0,0,0),(2,2,2)]` by `(1.5, 1, −1)` → `[(−0.5, 0, 0), (2.5, 2, 2)]`; by `(1,1,1)`
    unchanged. Extend `[(0,0,0),(1,1,1)]` by `(2, −1, 0)` → `[(0, −1, 0), (3, 1, 1)]`.

## Unit 6: Shapes: records, poses and bounds {#unit-6}

**What it is.** The four shape records (oriented box, sphere, upright capsule, segment), how each is posed from its
owner's transform and bone every update, how its "previous position" is kept for sweeps (the flag `0x10` rule), and
the world AABB of a posed shape. Specified exactly below; everything confirmed (code) at the cited addresses (read in
the disassembly, 2026-10-12), unless a line says otherwise.

- **Specified by:** this section; background in [Shapes](physics.md#shapes), [The human body](physics.md#human-body) and
  [Combat: moving strikes](combat.md#moving-strikes).
- **Coney:** a new `src/physics/shapes.{h,cpp}` (posed shapes as values, with a pose function per type that takes
  the owner's transform and bone transform as inputs); `src/human/human.h` capsule height moves here;
  `world_objects/object_bodies.*` builds its box and sphere from it.
- **Tests:** [Test ideas](#u6-tests): each shape posed from a synthetic body and bone transform; the flag `0x10`
  sequence; a rotated box's corners and bound against a brute-force corner bound; the segment bound quirk; the
  capsule's height rules (0.61, 0.7 and its cap, 0.35 cuffed, never under 0.2 × scale).

21 functions, 5272 bytes (1 tagged). Two names changed in Ghidra on 2026-10-12 because the old ones were wrong:
`0x003430e8` was `PhysicsBox_GetBounds` (it returns the eight corners, not a bound) and `0x00341708` was
`AABB_FromSphereMethod` (it builds an upright cylinder's box).

| Address | Name | Size |
| --- | --- | --- |
| `0x00342a30` | `PhysicsShape_Construct` | 60 |
| `0x00342a70` | `PhysicsShape_Destroy` | 48 |
| `0x00342aa0` | `PhysShapeSphere_Construct` | 60 |
| `0x00342ae0` | `PhysicsSphere_Destroy` | 40 |
| `0x00342b08` | `PhysicsSphere_Pose` | 512 |
| `0x00342d18` | `PhysicsBox_Construct` | 124 |
| `0x00342d98` | `PhysicsBox_Destroy` | 40 |
| `0x00342dc0` | `PhysicsBox_SetSize` | 52 |
| `0x00342df8` | `PhysicsBox_Pose` | 580 |
| `0x00343040` | `PhysicsBox_SetMatrix` | 168 |
| `0x003430e8` | `PhysicsBox_GetCorners` | 956 |
| `0x003434a8` | `PhysShapeCapsule_Construct` | 56 |
| `0x003434e0` | `PhysicsCapsule_Destroy` | 40 |
| `0x00343508` | `PhysShapeCapsule_SetSize` | 12 |
| `0x00343518` | `PhysicsCapsule_Pose` (tagged) | 636 |
| `0x003437b0` | `PhysShapeSegment_Construct` | 56 |
| `0x003437e8` | `PhysicsSegment_Destroy` | 40 |
| `0x00343810` | `PhysShapeSegment_Pose` | 632 |
| `0x00341190` | `PhysicsShape_GetBounds` | 812 |
| `0x00341660` | `AABB_FromSphere` | 168 |
| `0x00341708` | `AABB_FromUprightCylinder` | 180 |

Notation as in [Unit 3](#u3-records): vectors are 4 floats, "xyz" operations leave `w` alone, "w = 1" is the code
writing 1.0 (the VU constant register). Arguments follow the EE ABI: integers and pointers in `a0`-`a3`, floats in
`f12`, `f13`, `f14`.

### Transforms and rotations {#u6-transforms}

Every pose reads its transforms through owner vtable slots (GCC 2.x 8-byte slots, adjust then function; [Unit 3's
table](#u3-records)). Each returns a **32-byte transform: position (vec4) then rotation quaternion `(x, y, z, w)`**.

| Slot (adjust / function) | Called as | Human's function | World object's (vtable `0x005453a0`) | Used by |
| --- | --- | --- | --- | --- |
| `+0xa0` / `+0xa4` | `(out, owner)`, the body transform | `0x0023cd00` | `Obj_GetPoseNow` `0x003a1ad8` | sphere, box |
| `+0xa8` / `+0xac` | `(out, owner)`, the body transform | `0x004ed828` | `Task_GetPose` `0x003a1e30` | capsule, segment |
| `+0xb8` / `+0xbc` | `(owner, out, bone)`, a bone's transform in the body's frame | `Human_GetBoneTransform` `0x0023bde8` | | sphere, box, segment |
| `+0x20` / `+0x24` | `(owner)`, type mask (`0x40` a human) | | | capsule |

For a human, `0x0023cd00` and `0x004ed828` are the same code (both copy the entry of the transform table `0x00714b00`
indexed by human `+0x92`), so the two body slots agree. For a world object, `+0xa0` is the pose extrapolated to now
(`Obj_GetPoseNow`, [Objects](objects.md)) and `+0xa8` the stored pose (`Task_GetPose`: rotation `+0x20`, position
`+0x10`, moved by `0x003a2508`'s offset when flags `+0x54 & 0x600` are clear and slot `+0xf0` / `+0xf4` returns 0); its
`Obj_GetWorldPose` (`0x003a19b0`) is the slot before, `+0x98` / `+0x9c`, which no pose reads. Only spheres and
boxes, which read `+0xa0`, are built for world objects ([Bodies](physics.md#bodies)). Confirmed (code): the slots at the
call
sites below, the human's entries read from its vtable `0x0053f088`, the world object's from `0x005453a0` (the vtable
`ObjectTaskPool_New` `0x004f3ef0` stores). An earlier draft read them from `0x00544c00`, which is the car's vtable
`0x00544c08` minus 8, so its column was one slot off.

`Human_GetBoneTransform` returns the bone cache entry (model space) with the **position multiplied by the human's
scale** and the rotation as is ([Objects](objects.md)); it writes nothing when the human has no model instance
(`+0xd8` = 0), so the caller's buffer keeps what it held. In the sphere, box and segment poses that buffer is the one
the body transform was just read into, so such a human's bone transform **is its body transform** (confirmed (code):
the same stack slot at `0x00342b4c`/`0x00342b80`, `0x00342e3c`/`0x00342e78`, `0x00343858`/`0x0034389c`; that it
happens in play is not known).

**Rotation by a quaternion** (`R(q)`). Two forms appear, and they give the same result for a unit quaternion:

- The sphere's no-bone case rotates the offset directly (`0x00342c5c`-`0x00342c9c`):
  `v' = (w² − |u|²) v + 2 (u·v) u + 2 w (u × v)`, with `u = (x, y, z)`. No normalisation.
- Everything else builds a matrix with `Quat_ToMatrix` (`0x00336500`) and multiplies. Its four stored rows are the
  images of the axes (row 0 = `R e_x`, and so on) and row 3 is `(0, 0, 0, 1)`:

  | Row | x | y | z | w |
  | --- | --- | --- | --- | --- |
  | 0 | `1 − 2(y² + z²)` | `2(xy + zw)` | `2(xz − yw)` | 0 |
  | 1 | `2(xy − zw)` | `1 − 2(x² + z²)` | `2(yz + xw)` | 0 |
  | 2 | `2(xz + yw)` | `2(yz − xw)` | `1 − 2(x² + y²)` | 0 |
  | 3 | 0 | 0 | 0 | 1 |

  A vector is transformed as `row0·v.x + row1·v.y + row2·v.z + row3·v.w`, so `M v = R v` in xyz and its `w` is
  `v.w`. Every pose then adds a position in xyz and writes `w = 1`, so the `w` never matters.

So a shape's "bone x axis" is row 0 of the bone's matrix, `R(qb) e_x`.

### The shape record and vtable {#u6-record}

Common head, written by `PhysicsShape_Construct` (`0x00342a30`, `(Shape* s, u8 type)`, returns `s`):

| Offset | Type | Meaning | Construct writes |
| --- | --- | --- | --- |
| `+0x00` | ptr | next shape of the body | 0 |
| `+0x10` | vec4 | local offset | `(0, 0, 0, 1)` (the constant at `0x005116c0`) |
| `+0x20` | vec4 | previous position (sphere centre, box position, capsule base, segment start) | **not written** |
| `+0x30` | u8 | type: 1 box, 2 sphere, 3 capsule, 4 segment | `type` |
| `+0x31` | s8 | bone, −1 the body's own frame | −1 |
| `+0x32` | u16 | flags: `0x1` enabled, `0x2`, `0x4` a strike target, `0x8` allocated, `0x10` reset the previous position on the next pose | `0x1` |
| `+0x34` | ptr | vtable | `0x00544870` (the base) |

**Shape vtable** (both offsets of each slot; the type vtables are `0x005447f0` box, `0x00544830` sphere,
`0x005447b0` capsule, `0x00544770` segment; read from the vtables):

| Slot | What | Sphere | Box | Capsule | Segment |
| --- | --- | --- | --- | --- | --- |
| `+0x08` / `+0x0c` | **set position** `(s, const vec4* p)`: copies `p` (all four words), previous untouched | `+0x50` (`0x004f1200`) | `+0x80` (`0x004f1220`) | `+0x50` (`0x004f1238`) | `+0x40` (`0x004f1250`) |
| `+0x10` / `+0x14` | destructor `(s, flags)` | `0x00342ae0` | `0x00342d98` | `0x003434e0` | `0x003437e8` |
| `+0x18` / `+0x1c` | **enable** `(s, bool on)` (`0x004f11c8`, shared) | on: flags `\|= 0x11`; off: flags `&= ~0x1` | | | |
| `+0x20` / `+0x24` | **position pointer** `(s)` → `&` the field | `+0x50` (`0x004f1210`) | `+0x80` (`0x004f1230`) | `+0x50` (`0x004f1248`) | `+0x40` (`0x004f1260`) |
| `+0x28` / `+0x2c` | **pose** `(s, owner)` | `0x00342b08` | `0x00342df8` | `0x00343518` | `0x00343810` |
| `+0x30` / `+0x34` | **set from a matrix** `(s, const mat4* m)`, `m` = 4 rows, row 3 the position | `0x00342d08`: centre = row 3; previous untouched | `PhysicsBox_SetMatrix` `0x00343040` | `0x00343798`: previous = base, base = row 3 | `0x00343a88`: **does nothing** |

The base vtable's entries for these slots point at `0x0042b2b0` (inferred: the pure-virtual stub) except the enable
(`0x004f11c8`) and the destructor (`0x00342a70`). The small entries (`0x004f11c8`-`0x004f1260`, `0x00342d08`,
`0x00343798`, `0x00343a88`) have no function in Ghidra; they are read as listed (confirmed (code)).

**Destructors.** `PhysicsShape_Destroy` (`0x00342a70`, `(s, int flags)`) sets the base vtable and frees `s`
(`0x0042c820`) when `flags & 1`. Each type's destructor sets its own vtable and calls it with the same flags.

### The flag 0x10 rule {#u6-reset}

Switching a shape **on** sets flags `0x11` (enabled and reset). The sphere, box and segment poses then do:

1. At the start: if `0x10` is **clear**, previous = the current position (the pose before this one).
2. Pose (below).
3. At the end: if `0x10` is **set**, previous = the new position. Then **clear `0x10`**, always.

So the first pose after a shape is switched on has previous = current: a zero move, so a strike sphere just switched
on can only hit what it starts inside ([Unit 5](physics-kernels.md#u4b-how-human-teststrikes-builds-the-sweep-0)); every
later pose
keeps the pose before. Switching off clears only `0x1`. Confirmed (code) at `0x00342b20`/`0x00342ccc` (sphere),
`0x00342e10`/`0x00343000` (box), `0x0034382c`/`0x00343a48` (segment), `0x004f11d0` (enable).

The **capsule** pose never reads or clears `0x10`: its previous base is always the base before this pose
(`0x0034358c`, `0x0034373c`). `PhysicsBox_SetMatrix` also ignores it (previous = the old position, always). A
capsule's `0x10`, once set by enabling it, stays set (no reader of it found in this unit).

What the segment keeps as "previous" is its **start** only; the end has no previous. The sphere keeps its centre, the
box its position (`+0x80`; not its axes).

### Sphere {#u6-sphere-record}

0x60 bytes. `PhysShapeSphere_Construct` (`0x00342aa0`, `(s)`): the head with type 2, then `+0x40` radius = 0, vtable
`0x00544830`; the centre `+0x50` is not written. Fields: `+0x40` radius (float), `+0x50` centre.

### PhysicsSphere_Pose {#u6-sphere-pose}

`0x00342b08`, 0x200 bytes. `void (Sphere* s, Owner* o)`.

1. Flag `0x10` start step ([above](#u6-reset)).
2. `(P, q)` = owner slot `+0xa0` (`0x00342b4c`).
3. Bone −1 (`0x00342b68` → `0x00342c5c`): `centre = P + R(q) offset` (the direct quaternion form), `w = 1`.
4. Bone ≥ 0: `(b, qb)` = owner slot `+0xb8` with the bone (`0x00342b80`);
   `centre = P + R(q) (b + R(qb) offset)` (two matrix products, `0x00342bc4`, `0x00342c1c`), `w = 1`.
5. Flag `0x10` end step; clear `0x10`.

### Box {#u6-box-record}

0x90 bytes. `PhysicsBox_Construct` (`0x00342d18`, `(b)`): the head with type 1, vtable `0x005447f0`, and `+0x50`,
`+0x60`, `+0x70`, `+0x80` = the four rows of the identity at `0x005117a0`-`0x005117d0`: axes identity, position
`(0, 0, 0, 1)`. The half extents are not written. Fields: `+0x40` half extents (x, y, z; `w` 1.0 once sized),
`+0x50` / `+0x60` / `+0x70` the axes `A0`, `A1`, `A2` (unit vectors, `w` 0), `+0x80` position (the centre).

`PhysicsBox_SetSize` (`0x00342dc0`, `(b, float sx, float sy, float sz)`): `+0x40 = 0.5 sx`, `+0x44 = 0.5 sy`,
`+0x48 = 0.5 sz` (the constant 0.5 is `0x3f000000`), `+0x4c = 1.0`.

### PhysicsBox_Pose {#u6-box-pose}

`0x00342df8`, 0x244 bytes. `void (Box* b, Owner* o)`.

1. Flag `0x10` start step (previous = old `+0x80`).
2. `(P, q)` = owner slot `+0xa0` (`0x00342e3c`).
3. Bone −1 (`0x00342f98`): axes `A0, A1, A2` = rows 0-2 of `Quat_ToMatrix(q)` (row 3 is written to `+0x80` and
   then overwritten); `position = P + R(q) offset`, `w = 1`.
4. Bone ≥ 0: `(b, qb)` = owner slot `+0xb8` (`0x00342e78`). Axes: `Ai = R(q) (row i of R(qb))`, i = 0, 1, 2 (the
   loop at `0x00342eb0` multiplies all four rows; row 3 stays `(0, 0, 0, 1)` and is overwritten).
   `position = P + R(q) (b + offset)`, `w = 1`. **Quirk:** the offset is added to the bone's position **without the
   bone's rotation** (`0x00342f10`), unlike the sphere and the segment. No box with a bone is known (inferred: the
   object and car boxes have bone −1), so it is unlikely to matter.
5. Flag `0x10` end step; clear `0x10`.

### PhysicsBox_SetMatrix {#u6-box-set-matrix}

`0x00343040`, 0xa8 bytes; also the box's slot `+0x30` / `+0x34`. `void (Box* b, const mat4* m)`, `m` = rows
`m0`-`m3`, `m3` the position. In order: previous `+0x20` = the old position `+0x80` (always); `A0 = m0`, `A1 = m1`,
`A2 = m2` (all four words copied); `position = m3 + m0·off.x + m1·off.y + m2·off.z` (xyz), `w = 1`. Flags untouched.
Called by `Car_UpdateLoosePart` (`0x0038806c`) and, through the slot, by `PhysicsBody_Moved` (`0x00341e40`) for a
body's first shape.

### PhysicsBox_GetCorners {#u6-box-corners}

`0x003430e8`, 0x3bc bytes. `void (const Box* b, vec4 out[8])`. Corner `i` (0 to 7):

`out[i] = position + sx·hx·A0 + sy·hy·A1 + sz·hz·A2`, `w = 1`, with `sx = +1` when `i & 1` else −1,
**`sy = −1` when `i & 2` else +1**, `sz = +1` when `i & 4` else −1.

(The code builds the x part for all eight, then adds or subtracts the y and z parts in place: `0x00343100`,
`0x003431a0`-`0x00343438`, `0x00343448`.) The y sign is the odd one out; only the order of the corners depends on it.
Its only caller is the generic bound.

### Capsule {#u6-capsule-record}

0x60 bytes. `PhysShapeCapsule_Construct` (`0x003434a8`, `(c)`): the head with type 3, vtable `0x005447b0`; radius
and height are not written. Fields: `+0x40` radius, `+0x44` height, `+0x50` base (the bottom centre; the capsule is
the vertical range `[base.z, base.z + height]`, [Shapes](physics.md#shapes)).

`PhysShapeCapsule_SetSize` (`0x00343508`, `(c, float height, float radius)`): `+0x40 = radius` (from `f13`),
`+0x44 = height` (from `f12`). Note the order: height first. `IPhysics_Construct` passes 0.35 (`0x3eb33333`) as the
radius (`0x0033c8e4`).

### PhysicsCapsule_Pose {#u6-capsule-pose}

`0x00343518`, 0x27c bytes (tagged). `void (Capsule* c, Owner* o)`. The flag `0x10` is not used ([above](#u6-reset)).

**Not a human** (the owner's type mask, slot `+0x20`, lacks `0x40`; `0x0034354c` → `0x00343734`): previous = old
base; `(P, q)` = slot `+0xa8`; `base = P + offset` (xyz, **not rotated**), `w = 1`. Nothing else: the height stays as
set.

**A human** (`0x00343558`). Let `s` = `Human_GetScale` (`0x0021d020`: human `+0x65c`, or 1.0 when `0x00227d28`
holds and the scale is under 1.1), called afresh each time it is used.

1. `(P, q)` = slot `+0xa8` (`0x00343570`). Previous = old base; `base = P` (all four words; **the offset is not
   used**).
2. `(b6, ·)` = slot `+0xb8` with bone **6** (the head; `0x003435a8`). `height = b6.z + 0.2 s`
   (`0x3e4ccccd`). (`b6.z` is already scaled, [above](#u6-transforms).) The capsule's top is therefore
   `P.z + b6.z + 0.2 s` in every case below except the cuffed one.
3. If the byte human `+0x3be` is non-zero (`0x003435c8`): `base.z += 0.61 s`, `height −= 0.61 s` (`0x3f1c28f6`).
   Go to 6.
4. Else if the human is jumping (state flag `0x400000000`, `0x00227f68`) **and not** in a long fall (state flag
   `0x1000000000`, `0x002265f0`) (`0x00343614`-`0x00343634`): `base.z += 0.7 s`, `height −= 0.7 s`
   (`0x3f333333`). Then if `base.z − h560 > 0.7` (strictly; `h560` = human `+0x560`, the take-off height; the
   constant is 0.7 **not** scaled), `base.z = h560 + 0.7` and `height = b6.z + 0.2 s + P.z − base.z` (the top kept).
   Go to 6.
5. Else if the human is cuffed (state flag `0x20000`, `0x00223b70`; `0x003436c4`): `height = 0.35 s` (base
   unchanged, so the top drops).
6. If `height < 0.2 s` (strictly), `height = 0.2 s` (`0x00343710`).

A state test reads the 64-bit word at the human's current state record (`+0xd4`), and is false when there is none
(`0x002265f0`). The radius is never touched here.

### Segment {#u6-segment-record}

0x70 bytes. `PhysShapeSegment_Construct` (`0x003437b0`, `(s)`): the head with type 4, vtable `0x00544770`; nothing
else written. Fields: `+0x40` start, `+0x50` end, `+0x60` radius, `+0x64` length.

### PhysShapeSegment_Pose {#u6-segment-pose}

`0x00343810`, 0x278 bytes. `void (Segment* s, Owner* o)`.

1. Flag `0x10` start step (previous = old start `+0x40`).
2. `(P, q)` = slot **`+0xa8`** (`0x00343858`), and `M = Quat_ToMatrix(q)`.
3. Bone −1 (`0x00343a00`): `start = P` (all four words; **the offset is not used**), `end = P + length · M row 0`
   (the body's x axis), `w = 1`.
4. Bone ≥ 0: `(b, qb)` = slot `+0xb8` (`0x0034389c`), `B = Quat_ToMatrix(qb)`. Local `a = b + B offset`, local
   `e = a + length · B row 0` (the bone's x axis). `start = P + M a`, `end = P + M e`, `w = 1`
   (`0x00343970`, `0x003439c0`).
5. Flag `0x10` end step (previous = the new start); clear `0x10`.

The radius `+0x60` is not read.

### PhysicsShape_GetBounds {#u6-bounds}

`0x00341190`, 0x32c bytes. `void (unused, vec4* min, vec4* max, const Shape* s)`: the first argument is not read
(`PhysicsBody_ComputeBounds` `0x00341638` passes the body, whose `+0x00` / `+0x10` are `min` / `max`;
`PhysicsBody_AddShapeBounds` `0x003417c0` passes stack boxes and then merges them into the body's). By type
(`+0x30`):

| Type | `min` before the sort | `max` before the sort |
| --- | --- | --- |
| 2 sphere (`0x003412b0`) | `centre − r` (xyz; `w` = centre's) | `centre + r` |
| 3 capsule (`0x003411f0`) | `(base.x − r, base.y − r, base.z)` | `(base.x + r, base.y + r, base.z + height)` |
| 4 segment (`0x00341248`) | `start − r` (`r` = `+0x60`) | `end + r` |
| 1 box (`0x00341320`) | per axis, the corner with the smallest value | per axis, the corner with the largest value |
| anything else | nothing written, return | |

The box's corners come from [PhysicsBox_GetCorners](#u6-box-corners); the scan is over corners 1 to 7 against the
best so far with strict `<` (`0x00341348`-`0x00341404`), so the exact rotated-box bound. The box path writes only xyz
of `min` and `max`.

**The sort** (`0x0034146c`), for all four types: `min' = min(min, max)` per axis, then `max' = max(min', max)` per
axis; both keep `max`'s `w`. The second step can never change `max`. So:

- sphere, capsule and box bounds come out right (their `min ≤ max` already, for a radius and height ≥ 0);
- **a segment whose end is below its start on an axis gets a wrong bound on that axis**: `[min(start − r, end + r),
  end + r]`, which misses the part from `end + r` to `start + r`, and is a single value (`end + r`) when
  `start − r > end + r`. Confirmed (code). A punch bag's segment points down ([Bodies](physics.md#bodies)), so its z
  bound is the
  single height `end.z + r` (inferred: where that bound is used, through the body's bound, is Unit 7's).

The capsule bound has **no radius below or above**: it is the cylinder's box, matching the upright capsule of the
pair tests ([Shapes](physics.md#shapes)).

### AABB_FromSphere and AABB_FromUprightCylinder {#u6-aabb}

Both write a 0x20-byte AABB (`min` at `+0x00`, `max` at `+0x10`) and set **both `w` to 1.0**; both sort with the
original `min` and `max` (so unlike the generic bound, a negative size still gives a valid box).

- `AABB_FromSphere` (`0x00341660`, `(AABB* out, const vec4* c, float r)`): `min = c − r`, `max = c + r` (xyz). Its
  only caller is `PhysicsBody_PushOutOfWalls` ([Unit 2](#u2-003477c0-physicsbody-pushoutofwalls), `0x00347808`).
- `AABB_FromUprightCylinder` (`0x00341708`, `(AABB* out, const vec4* c, float height, float radius)`; formerly
  `AABB_FromSphereMethod`): `min = (c.x − radius, c.y − radius, c.z − height/2)`,
  `max = (c.x + radius, c.y + radius, c.z + height/2)`. Callers: the human body's broad bound `+0x90`, in its update
  (`0x0034252c`, height = the capsule's `+0x44`) and in its moved handler (`0x00342878`: height 2.0, radius 0.8,
  centre = the new position + 1.0 m in z) ([Unit 10](#unit-10)).

### Test ideas {#u6-tests}

Values computed from this spec (double precision; compare with 1e-5). `qz` = 90° about z = `(0, 0, 0.70711,
0.70711)`, `qy` = 90° about y = `(0, 0.70711, 0, 0.70711)`. `Quat_ToMatrix(qz)` rows: `(0, 1, 0)`, `(−1, 0, 0)`,
`(0, 0, 1)`; `Quat_ToMatrix(qy)` rows: `(0, 0, −1)`, `(0, 1, 0)`, `(1, 0, 0)`. Body `P = (10, 5, 0)`, `q = qz`
unless said; the bone `(b, qb) = ((0.2, 0, 1.5), qy)`.

1. **Sphere, no bone.** Offset `(1, 0, 0.5)`: centre `(10, 6, 0.5, 1)` (both rotation forms agree).
2. **Sphere on a bone.** Offset `(0.08, 0, 0)` (the hand's): `R(qb) offset = (0, 0, −0.08)`, local
   `(0.2, 0, 1.42)`, centre `(10, 5.2, 1.42, 1)`.
3. **Segment on a bone.** Offset `(−0.07, 0, 0.05)`, length 0.38 (the spine's): local start `(0.25, 0, 1.57)`, local
   end `(0.25, 0, 1.19)`; start `(10, 5.25, 1.57, 1)`, end `(10, 5.25, 1.19, 1)`.
4. **Segment, no bone.** Any offset, length 0.38: start = `P` = `(10, 5, 0)`, end `(10, 5.38, 0, 1)`.
5. **Box, no bone.** Offset `(1, 0, 0)`: axes `(0, 1, 0)`, `(−1, 0, 0)`, `(0, 0, 1)`; position `(10, 6, 0, 1)`.
6. **Box on a bone (the quirk).** Offset `(0.1, 0, 0)`: axes `(0, 0, −1)`, `(−1, 0, 0)`, `(0, 1, 0)`; position
   `(10, 5.3, 1.5, 1)` (`b + offset` unrotated by the bone; a rotated offset would give `(10, 5.2, 1.4)`).
7. **Flag 0x10 sequence** (sphere, no bone, offset 0). Pose at `P = (0, 0, 0)` → centre 0. Enable (flags `0x11`).
   Pose at `P = (1, 0, 0)`: previous = `(1, 0, 0)` = centre (zero move), flags `0x01`. Pose at `P = (2, 0, 0)`:
   previous `(1, 0, 0)`, centre `(2, 0, 0)`. Disable then enable again: the next pose again has previous = centre.
   The same sequence on a capsule (non-human owner) gives previous = the old base every time.
8. **SetMatrix.** Rows `(0, 1, 0)`, `(−1, 0, 0)`, `(0, 0, 1)`, position `(5, 5, 0)`; offset `(0, 0, 0.5)`; old
   position `(1, 1, 1)`: previous `(1, 1, 1)`, position `(5, 5, 0.5, 1)`. Offset `(1, 0, 0)`: position `(5, 6, 0)`.
9. **Box corners.** Centre `(0, 0, 0.5)`, identity axes, half `(2, 1, 0.5)`: corners 0-7 are `(−2, 1, 0)`,
   `(2, 1, 0)`, `(−2, −1, 0)`, `(2, −1, 0)`, `(−2, 1, 1)`, `(2, 1, 1)`, `(−2, −1, 1)`, `(2, −1, 1)`.
10. **Rotated box bound.** The same box turned 45° about z (`q = (0, 0, 0.38268, 0.92388)`): bound
    `(−2.1213, −2.1213, 0)` to `(2.1213, 2.1213, 1)`, equal to the brute-force min/max of the eight corners.
11. **Sphere and capsule bounds.** Sphere centre `(1, 2, 3)`, r 0.5: `(0.5, 1.5, 2.5)` to `(1.5, 2.5, 3.5)`. Capsule
    base `(1, 2, 0)`, r 0.35, height 1.8: `(0.65, 1.65, 0)` to `(1.35, 2.35, 1.8)`.
12. **Segment bound quirk.** Start `(0, 0, 2.15)`, end `(0, 0, 0.65)`, r 0.35: x `[−0.35, 0.35]`, y the same, z
    **`[1.0, 1.0]`** (the true bound is `[0.3, 2.5]`). Reversed (start `(0, 0, 0.65)`, end `(0, 0, 2.15)`): z
    `[0.3, 2.5]`. Start `(0, 0, 1.0)`, end `(0, 0, 0.8)`, r 0.35: z `[0.65, 1.15]` (true `[0.45, 1.35]`).
13. **AABB helpers.** `AABB_FromSphere(c = (1, 2, 3), r = 0.5)`: `(0.5, 1.5, 2.5, 1)` to `(1.5, 2.5, 3.5, 1)`;
    with r = −0.5 the same box. `AABB_FromUprightCylinder(c = (1, 2, 3), height 2.0, radius 0.8)`:
    `(0.2, 1.2, 2, 1)` to `(1.8, 2.8, 4, 1)`.
14. **Capsule height, human** (`P = (0, 0, 10)`, `b6.z = 1.6`, `s = 1` unless said): plain: base z 10, height 1.8
    (top 11.8). `+0x3be` set: base z 10.61, height 1.19. Jumping, take-off `+0x560` = 9.8: base z 10.7, height 1.1
    (10.7 − 9.8 = 0.9 > 0.7 → base z 10.5, height 1.3; top still 11.8). Jumping with take-off 10.2: 10.7 − 10.2 = 0.5,
    no cap: base z 10.7, height 1.1. Jumping and long fall: plain (1.8). Cuffed: height 0.35, base z 10 (top 10.35).
    `+0x3be` and cuffed: 1.19 (the `+0x3be` branch wins). `b6.z = −0.1`: 0.1 → clamped to 0.2. Scale 0.97,
    `b6.z = 1.552` (the bone already scaled): height 1.746; cuffed 0.3395; clamp floor 0.194.
15. **Capsule, not a human.** Owner at `(3, 4, 5)` with rotation `qz`, offset `(1, 0, 0)`: base `(4, 4, 5, 1)` (the
    offset not rotated); height and radius unchanged.

## Unit 7: Bodies and the sort-and-sweep world {#unit-7}

**What it is.** The body record and its update, the body vector, the list of bodies sorted by AABB min x, the
widest-body bound, and the box queries every shape, sweep, strike and ray test starts from. Read in the disassembly
on 2026-10-12; every claim is confirmed (code) at the cited address unless it says otherwise.

- **Coney:** `src/physics/physics_world.{h,cpp}` (body vector, x-sorted list, `+0x14` bound, the two box queries, the
  overlap query); `play_level_mode` owns one per level. The queries have **no** 512 cap of their own: the callers'
  lists hold 512 and nothing checks it ([below](#u7-cap)); Coney uses a growable list and marks the difference.
- **Tests:** [Test ideas](#u7-tests): the tie order of insert and reinsert, the backward walk's order and its missing
  upper x test, the filters, the bound refresh on the 120th tick, the overlap query's option bits.

19 functions, 4400 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00341158` | `PhysicsNode_Init` | 52 |
| `0x003418f8` | `PhysicsBody_Construct` | 120 |
| `0x00341970` | `PhysicsBody_ConstructForOwner` | 140 |
| `0x00341a00` | `PhysicsBody_Destroy` | 104 |
| `0x00341a68` | `PhysicsBody_SetOwner` | 64 |
| `0x00341aa8` | `PhysicsBody_AddShape` | 56 |
| `0x00341ae0` | `PhysicsBody_GetShapes` | 8 |
| `0x00341638` | `PhysicsBody_ComputeBounds` | 36 |
| `0x003417c0` | `PhysicsBody_AddShapeBounds` | 128 |
| `0x00341c10` | `PhysicsBody_Update` | 516 |
| `0x00341e40` | `PhysicsBody_Moved` | 84 |
| `0x003404a8` | `BodyList_Insert` | 304 |
| `0x003405d8` | `BodyList_Remove` | 80 |
| `0x00340628` | `BodyList_Reinsert` | 64 |
| `0x00340668` | `IPhysics_AddBody` | 152 |
| `0x00340700` | `IPhysics_RemoveBody` | 156 |
| `0x0033feb0` | `IPhysics_QueryBox` | 624 |
| `0x00340120` | `IPhysics_QueryBoxFiltered` | 904 |
| `0x0033ebb8` | `IPhysics_OverlapShape` | 808 |

Notation as in [Unit 3](#u3-records): vectors are 4 floats, "xyz" leaves `w` alone, owner and body vtable slots are
named by the offset of their 8-byte entry (the function pointer is 4 bytes further). Arguments follow the EE ABI:
`a0`-`a3`, then `t0`-`t3`, then 64-bit stack slots at the caller's `sp+0x00`, `+0x08`, `+0x10`.

### The records {#u7-records}

**The sort node** is the first 0x30 bytes of a body: `+0x00` AABB min, `+0x10` AABB max, `+0x20` index in the body
vector (s32), `+0x24` previous and `+0x28` next in the x-sorted list. The queries build bare nodes on their stack
for their boxes. **The body** is the node plus the fields of [Bodies](physics.md#bodies) (0x80 bytes, vtable
`0x00544710`).

**IPhysics fields** used here ([IPhysics](physics.md#iphysics)): `+0x08` list head (lowest min x), `+0x12` u16 tick
counter,
`+0x14` float bound (the widest body's `max.x − min.x`), `+0x18` the overlap table, `+0xf4` the mesh overlaps by
shape type, `+0x4f450` the body vector (`+0x04` begin, `+0x08` end, `+0x0c` capacity end; zeroed by the constructor,
`0x0033c5cc`-`0x0033c5dc`, grown on demand by `0x004f0fa8`). The constructor sets `+0x08` = 0, `+0x12` = 0 and
`+0x14` = 0.0 (`0x0033c86c`, `0x0033c2d8`, `0x0033c2dc`).

**Body vtable** `0x00544710`, read from memory: `+0x08` `PhysicsBody_Destroy`, `+0x10` `PhysicsBody_Update`, `+0x18`
`PhysicsBody_Moved`, `+0x20` `PhysicsBody_AddShape`, `+0x28` `PhysicsBody_GetShapes`, `+0x30` `IsIgnoring`, `+0x38`
`StopIgnoring`, `+0x40` `Ignore` ([Unit 8](#unit-8)), `+0x48` `PhysicsBody_GetContactScale` (`0x00341e18`), `+0x50`
"you were pushed" (`0x004f12a0`). The human body's vtable `0x005446b0` replaces `+0x08`, `+0x10`, `+0x18`, `+0x28`,
`+0x30`-`+0x50` ([The human body](physics.md#human-body), [Unit 8](#u8-human)) and keeps `+0x20`.

### PhysicsNode_Init {#u7-node}

`0x00341158`. `Node* PhysicsNode_Init(Node* n)`: `min = max = (0, 0, 0, 1)` (the constant at `0x005116c0`), `+0x20 =
−1`, `+0x24 = +0x28 = 0`; returns `n`.

### PhysicsBody_Construct and PhysicsBody_ConstructForOwner {#u7-construct}

`0x003418f8`. `Body* PhysicsBody_Construct(Body* b)`: vtable `0x00544710`, `PhysicsNode_Init`, `+0x30` first shape
= 0, `+0x34` owner = 0, `+0x38` ignore chain = 0, `+0x3c` group (s16) = 0, `+0x40` flags = `0x80000400` (dirty and
"write the slid velocity back"), `+0x44` type mask = 0, `+0x50` push-out = `(0, 0, 0, 1)`, `+0x60` scale =
`(1, 1, 1, 1)` (`0x005116d0`), `+0x70` counter = 0. Not written: `+0x2c`, `+0x48`, `+0x4c`.

`0x00341970`. `Body* PhysicsBody_ConstructForOwner(Body* b, Owner* o)`: the same, but `+0x34 = o` and, **only when
`o` is not null**, `+0x44` = the owner's type mask (owner vtable `+0x20`, `0x003419d8`). It writes **neither
`+0x44` for a null owner nor the scale `+0x60`** (no store of `0x005116d0`): a stack body built with it keeps
whatever the stack held there. Only the camera builds such bodies ([Bodies](physics.md#bodies)); nothing in this unit
reads
`+0x60`, and [Unit 3](#u3-sweep) shows the sweep ignores it, so Coney may set 1.0 (a marked difference).

### PhysicsBody_Destroy {#u7-destroy}

`0x00341a00`, vtable `+0x08`. `void (Body* b, int flags)`: resets the vtable pointer; if `+0x30` is not null,
deletes that first shape (`SafeDelete_PhysicsBodyDestroy` `0x004f1148`: the shape's destructor, then the heap free
with this file's path `0x005771d0` and line 3309) and sets `+0x30 = 0`; then frees the body itself when `flags & 1`.
The pool free functions ([Unit 9](#unit-9)) clear `+0x30` first, so pooled shapes are never deleted (inferred from
`IPhysics_FreeObjectBody` `0x0033cf00`, which does so at `0x0033cf50`).

### PhysicsBody_SetOwner {#u7-set-owner}

`0x00341a68`. `void (Body* b, Owner* o)`: `+0x34 = o` always; when `o` is not null, `+0x44` = owner vtable `+0x20`
(the type mask). A null owner leaves `+0x44` as it was.

### PhysicsBody_AddShape and PhysicsBody_GetShapes {#u7-shapes}

`0x00341aa8`, vtable `+0x20`. `void (Body* b, Shape* s)`: if `+0x30` is null, `+0x30 = s`; else walks the chain
through shape `+0x00` and sets the last one's `+0x00 = s`. It does **not** write `s+0x00` (the shape constructor
`0x00342a30` zeroes it) and does not check whether `s` is already in the chain.

`0x00341ae0`, vtable `+0x28`. `Shape* (Body* b, int, int, Owner*)`: returns `+0x30`; the three extra arguments the
callers pass (`(1, 3, 0)`, `(2, 4, owner)`) are ignored. The human body's version answers its capsule chain.

### PhysicsBody_ComputeBounds and PhysicsBody_AddShapeBounds {#u7-bounds}

`0x00341638`. `void (AABB* box, Shape* s)`: `PhysicsShape_GetBounds(—, &box.min, &box.max, s)` (`0x00341190`,
[Unit 6](#unit-6)); `box` is any 0x20-byte AABB, the body's own `+0x00` or a stack box.

`0x003417c0`. `void (AABB* box, Shape* s)`: the shape's bounds into a temporary, then `box.min = min(box.min, t.min)`
and `box.max = max(box.max, t.max)` per axis (VU0 `vmini.xyz` / `vmax.xyz`), then `box.min.w = box.max.w = 1.0`.

### PhysicsBody_Update {#u7-update}

`0x00341c10`, vtable `+0x10`. `void PhysicsBody_Update(Body* b, Owner* o)`. Called by the owners' updates and by
the [step](physics.md#step)'s settle (body vtable `+0x10` with the object).

1. If `b+0x40 & 0x80000800` is 0, return (`0x00341c44`).
2. Pose the first shape (shape vtable `+0x28` with `o`; no null check, a body always has a shape), `b`'s AABB =
   its bounds (`PhysicsBody_ComputeBounds(b, first)`). For each further shape of the chain: pose it, then
   `PhysicsBody_AddShapeBounds(b, s)`. **Every** shape is posed and bounded, enabled or not.
3. `BodyList_Reinsert(the global IPhysics at 0x00597198, b)` ([below](#u7-reinsert)); clear `0x80000000`.
4. If `b+0x40 & 0x801` is exactly `0x801` (`0x00341ce0`), the **update sweep**:
    1. `s` = body vtable `+0x28` with `(1, 3, 0)`; `p` = the shape's world position (shape vtable `+0x20`);
       `d = p − s+0x20` (xyz, `w = 1`): from its previous world position to its new one.
    2. A stack node: `PhysicsNode_Init`, then a copy of `b`'s min, max and `+0x20` word, and the box is extended by
       `d` (`AABB_ExtendByMove`, [Unit 3](#u3-extend)). **The box is the new AABB grown forward by `d`**, so it
       covers the new pose and one more `d` beyond it, not the path from the old pose (`0x00341d34`-`0x00341d7c`).
    3. `count = 0`; `PhysicsBody_SweepHumanMask(phys, &box, b, s, contacts, sorted, &count, &d)`
       (`0x0033d498`, [Unit 11](#unit-11)); 50 contacts of 0xc0 at `sp+0x20`, 50 pointers at `sp+0x25a0`.
    4. If `count > 0`: `PhysicsBody_ResolveContacts(phys, b, sorted, count, &d, &V, &v, &dtOne, &moveFlags,
       &scale)` ([Unit 3](#u3-resolve)) with `V = d`, `v` **not initialised** (stack), `dtOne = 1.0` (unused by the
       resolution), `moveFlags = 0`, `scale` not initialised. The return value, `V`, `v` and `scale` are dropped;
       what remains is what the contact codes did to the owner (an advance along the normal, a push-out on the
       other body).

So a body with `0x801` meets bodies that lie **ahead** of its new position but can miss one it passed through in
this update (inferred from step 4.2; which bodies carry `0x801` is [Unit 9](#unit-9)'s).

### PhysicsBody_Moved {#u7-moved}

`0x00341e40`, vtable `+0x18`. `void (Body* b)`: resets the **first** shape's previous position (shape vtable `+0x30`),
`b`'s AABB = that shape's bounds only (`PhysicsBody_ComputeBounds`), then `BodyList_Reinsert(0x00597198's IPhysics,
b)`. Other shapes are neither reset nor bounded, the dirty flag is not touched. The human body has its own
(`0x00342828`, [The human body](physics.md#human-body)).

### BodyList_Insert {#u7-insert}

`0x003404a8`. `void BodyList_Insert(IPhysics* p, Body* b, Body* hint)`:

1. **Bound** (`0x003404a8`-`0x003404c4`): `w = b.max.x − b.min.x`; if `p+0x14 < w` (strict), `p+0x14 = w`. This
   happens on every insert and so on every reinsert.
2. **Empty list**: `head = b`, `b.prev = b.next = 0`; done.
3. `start = hint`, or the head when `hint` is 0.
4. **Backward** when `b.min.x < start.min.x` (`0x003404f0`): walk `n = start.prev, n.prev, …`; at the first `n` with
   `n.min.x < b.min.x` (strict), link `b` **after** `n`. If the walk runs off the front, `b` becomes the head (before
   the last node visited). So `b` goes **before every node of equal min x** it passed.
5. **Forward** otherwise (`b.min.x ≥ start.min.x`, `0x00340574`): walk `n = start.next, n.next, …`; at the first `n`
   with `b.min.x < n.min.x` (strict), link `b` **before** `n`. If the walk runs off the end, `b` is the tail. So `b`
   goes **after every node of equal min x** it passed, and after `start` itself.

The tie rule is therefore not "stable": a body lands after its equals when it is placed moving forward and before
them when moving backward.

### BodyList_Remove {#u7-remove}

`0x003405d8`. `Body* BodyList_Remove(IPhysics* p, Body* b)`: returns `b`'s **old previous node** (0 for the head).
If `b` is the head: `head = b.next` and, if that is not null, its `prev = 0`. Otherwise `b.prev.next = b.next` (when
`b.prev` is set) and `b.next.prev = b.prev` (when `b.next` is set). Then `b.prev = b.next = 0`. There is no
membership check: removing a body that is not in the list and has no links changes nothing and returns 0.

### BodyList_Reinsert {#u7-reinsert}

`0x00340628`. `void (IPhysics* p, Body* b)`: `prev = BodyList_Remove(p, b)`, then `BodyList_Insert(p, b, prev)`.

Consequences of the hint (confirmed (code); the examples computed from it):

- A body whose min x did **not** change still moves: from its old previous node it walks forward and lands after
  every following node of the same min x. In `A(0) B(1) C(1) E(1) D(2)` (min x in brackets), reinserting `B`
  unchanged gives `A C E B D`.
- A body that moves back lands before the equals it passes: `A(0) B(1) C(1) D(2)` with `D` moved to 0 gives
  `D A B C`; moved to 1 it walks forward from `C` and stays `A B C D`.
- The old head has no previous node, so it starts from the new head (its old successor).

### IPhysics_AddBody and IPhysics_RemoveBody {#u7-addremove}

`0x00340668`. `void IPhysics_AddBody(IPhysics* p, Body* b)`: pushes `b` at the end of the body vector (growing it
through `0x004f0fa8` when full), `b+0x20 = size − 1`, then `BodyList_Insert(p, b, 0)`: from the head, so a new body
goes after every body of equal min x. No check that `b` is already present.

`0x00340700`. `void IPhysics_RemoveBody(IPhysics* p, Body* b)`:

1. `IPhysics_ForgetIgnoredOwner(p, b)` ([Unit 8](#u8-forget)).
2. **Swap-remove** from the vector: when the size is 2 or more, the last body `L` takes `b`'s place (`L+0x20 =
   b+0x20`, `vector[b+0x20] = L`); then the size goes down by one (also when it is 1). `b+0x20` keeps its old value
   (not −1).
3. `BodyList_Remove(p, b)`.

So removing a body renumbers the last one; nothing outside these functions reads `+0x20` except the queries, which
use it to fetch the body ([below](#u7-querybox)).

### IPhysics_QueryBox {#u7-querybox}

`0x0033feb0`. `int IPhysics_QueryBox(IPhysics* p, Body** out, const AABB* box, Body* start, Owner* ignored, u32
typeMask, u32 flagMask)` → the number written to `out`. Leaf function (no calls).

**Keep test** for a node `n` (the same in both walks): `B = vector[n+0x20]` (the body is fetched through the
vector; for a body in the list this is `n` itself, so Coney may use the node), then, in this order:

1. `B+0x34 == ignored` → drop. (With `ignored` = 0, ownerless bodies are dropped and every owned body passes.)
2. `B+0x44 & typeMask == 0` → drop. So a body with type mask 0 is never found, and `typeMask` 0 finds nothing.
3. `flagMask != 0` and `B+0x40 & flagMask == 0` → drop.
4. y and z, inclusive, on the **node's** box: keep when `n.min.y ≤ box.max.y`, `box.min.y ≤ n.max.y`, and the same
   for z. The x tests belong to the walks below.

**The walk** (`0x0033feb4`-`0x00340118`):

1. **Without `start`**: go to step 3 from the head.
2. **With `start`, backward** from `start` itself (`start` is tested too): for each `n = start, start.prev, …`:
    - `n.max.x < box.min.x` → skip it, go on to `n.prev` (no bound test);
    - else, if `box.min.x − n.min.x > p+0x14` (strict) → **stop** the backward walk;
    - else apply the keep test and append; go on to `n.prev`.

   When it stops or runs off the front, continue with step 3 from `start.next`.
3. **Forward** from the given node: stop at the end or at the first `n` with `box.max.x < n.min.x`; skip `n` when
   `n.max.x < box.min.x`; else the keep test and append.

What follows from this, confirmed (code) for the branches and worked out from them:

- **Order:** the backward hits come first, **in reverse list order** (from `start` towards the head), then the forward
  hits in list order. Not one list order.
- **The bound never stops the walk while it is current.** The stop is tested only on a node that reaches the box
  (`n.max.x ≥ box.min.x`), whose own width is then at least `box.min.x − n.min.x`; and `+0x14` is at least every
  width as long as each body was inserted after its last box change (which `Update`, `Moved` and the human body's
  update all do). So the backward walk runs to the head; the bound matters only if it is smaller than a body's width
  (inferred; it changes the cost, not the answer).
- **No upper x test going backward.** A body before `start` is kept even when its min x is beyond `box.max.x`. With
  the usual callers the box contains `start`'s own box, so every body before `start` has `min.x ≤ start.min.x ≤
  box.max.x` and nothing changes (inferred). A test can show it with a box placed left of `start`.
- `start` is a candidate like any other; the callers drop it by passing its owner as `ignored` (all four do,
  [below](#u7-callers)).
- A `start` that is not in the list (no links, not the head) yields at most itself: the backward walk sees only
  `start`, the forward walk begins at its null `next`. A query must start from a listed body or from none.

### IPhysics_QueryBoxFiltered {#u7-queryboxfiltered}

`0x00340120`. `int IPhysics_QueryBoxFiltered(IPhysics* p, Body* query, Body** out, const AABB* box, Body* start,
Owner* ignored, u32 typeMask, u32 flagMask, u32 excludedFlags, u32 excludedState, int (*keep)(Owner* mine, Owner*
theirs))`: `a0`-`a3`, `t0`-`t3`, then the caller's `sp+0x00`, `+0x08`, `+0x10` (read at `0x003401d8`, `0x003401fc`,
`0x00340174`). The same two walks as `IPhysics_QueryBox`, with these filters after the first three (owner, type mask,
flag mask) and before y and z (`0x003401d8`-`0x00340224`, mirrored at `0x00340388`-`0x003403d4`):

- (4) `B+0x40 & excludedFlags != 0` → drop (0 excludes nothing).
- (5) `state` = the candidate owner's state flags (owner vtable `+0x50`, the 64-bit word of [Unit 3](#u3-records)),
  **called for every candidate that got this far, even when `excludedState` is 0**, with no null check: an
  ownerless body reaches it unless `ignored` is 0 (filter 1 then drops it). `state & excludedState != 0` → drop.
  The mask is a 32-bit load, sign-extended (bit 31 also tests the high word).
- (6) When `keep` is not null: `keep(query+0x34, B+0x34)` returns 0 → drop. `query` is read only here.

The only caller is `IPhysics_CollideShape` (`0x0033e91c`, [Unit 11](#unit-11)), which passes its query body as both
`query` and `start`, the stack box, and these from its query record (`0x0033e890`-`0x0033e8f8`):

| Argument | Option bit | Field when set | Value when clear |
| --- | --- | --- | --- |
| `ignored` | `0x2000` | `+0x0c` | 0 |
| `typeMask` | `0x10` | `+0x14` | −1 |
| `flagMask` | `0x800` | `+0x1c` | 0 |
| `excludedFlags` | `0x1000` | `+0x20` | 0 |
| `excludedState` | `0x10000` | `+0x24` | 0 |
| `keep` | `0x40000` | `+0x38` | null |

So **without option `0x2000` the querying body itself is a candidate** (`ignored` = 0); `IPhysics_CollideShape`
then drops it only through its own group test and its owner's `+0x110` answer. Which callers set which bits, and
which `keep` functions exist, is [Unit 11](#unit-11)'s (callers not traced here).

### IPhysics_OverlapShape {#u7-overlapshape}

`0x0033ebb8`, 0x328 bytes. `int IPhysics_OverlapShape(IPhysics* p, Contact* out, const OverlapQuery* q)` → contacts
written to `out`. The record and the contact it fills are on [Unit 4](physics-kernels.md#overlapshape); here, every bit
and limit.

**Options read** (`q+0x10`, at `0x0033ebfc`, `0x0033ec20`, `0x0033ec7c`, `0x0033ed20`, `0x0033ee5c`): exactly
`0x1` (the mesh), `0x2` (bodies), `0x10` (type mask `+0x14`, else −1), `0x80` (skip the same non-zero group),
`0x100` (ask the querying owner, vtable `+0x110`), `0x200` (only shapes with flag `0x1`), `0x400` (`q+0x08` is a
**pointer** to the box to use; read with a word load at `0x0033ec10`). **No other bit is read**, and `q` is not passed
on: the push sphere's `0x2000` (`0x2012`) and the climb's `0x4` and `0x8` (`0xd`) have no effect here.

1. **The box**: `*q+0x08` with `0x400`, else the querying shape's bounds into a stack node (`PhysicsBody_ComputeBounds`
   on `q+0x04`).
2. **Without `0x2`**: no bodies, go to step 5 with 0 contacts.
3. **Candidates**: `n = IPhysics_QueryBox(p, list, box, q+0x00, (q+0x00)+0x34, typeMask, 0)` into a 512-entry
   stack list (`sp+0x30`-`sp+0x82c`). The querying body is `start`, so it must be in the list
   ([above](#u7-querybox)).
4. **Each candidate** `C`, in the order the query returned them, **while `q+0x18` (max) is not 0** (tested before each
   candidate; no other early stop, not when `max` or 50 contacts are reached):
    - `0x80`: `C+0x3c` (u16) and the querying body's `+0x3c` both non-zero and equal → skip `C`.
    - `0x100`: querying owner's vtable `+0x110` with `(owner, C+0x34)` returns 0 → skip `C`.
    - shapes = `C`'s vtable `+0x28` with `(2, 4, querying owner)`; null → skip.
    - Each shape `s` of the chain (with `0x200`, only those with flag `0x1`): `fn = overlap[own.type][s.type]`
      (`p+0x18 + own.type × 0x14 + s.type × 4`); null → next shape. Else `slot = contacts[count]` (50 × 0xc0 at
      `sp+0x830`), and if `fn(slot, own, s)` is non-zero: `slot+0xb4 = 0`, then `Contact_InsertSorted(p, slot,
      sorted, &count, querying owner, C+0x34, own, s)` ([Unit 3](#u3-insert)). **All** shapes of `C` are tested
      (several contacts per candidate are possible).
5. **Copy out** `k = min(count, max)` contacts, in sorted order, 0xc0 bytes each (`0x0033eddc`-`0x0033ee44`).
6. **The mesh**: if `k < max` (unsigned) and `0x1`, and the querying shape's type has a mesh overlap (`p+0xf4 + type ×
   4`): `k += meshOverlap(q+0x28, q+0x2c, out + k, max − k, &box, q+0x00)` (floats in `f12`, `f13`; then `a0`-`a3`;
   `0x0033ee84`-`0x0033eea8`). Return `k`.

**Limits, none checked** (`0x0033ec44`, `0x0033ed10`, `0x0033ed8c`): 512 candidates, 50 contact slots, 52 sorted
pointers (`sp+0x2db0`-`sp+0x2e7f`). A 51st hit writes its contact over the sorted list, and the insert writes
pointers past it; a 513th candidate writes over the contact slots. Inferred: not reached in play (the four callers
use small boxes and max 1 to 32).

### The 512 candidates {#u7-cap}

Neither query has a cap, and neither is told the size of `out`. Every caller provides 512 entries:
`IPhysics_OverlapShape` (`sp+0x30`, 0x800 bytes), `IPhysics_CollideShape` (`sp+0x26a0`-`sp+0x2e9f`),
`PhysicsBody_SweepHumanMask` (`sp+0x000`-`sp+0x7ff`), `IPhysics_RayCastBodies` (`sp+0x20`-`sp+0x81f`); in
`Human_TestStrikes` the list is at `sp+0x130` and the next local at `sp+0x990` (room for 536). Confirmed (code) for
the four frames; past 512 the original writes over its own stack (whatever lies after the list), which is undefined
behaviour, not a rule to reproduce. The pools allow 60 + 1 + 416 + 255 + 18 = 750 bodies, so a level-sized box could
pass 512 (inferred; no caller asks for one). Coney: a growable list, and a debug assertion at 512 so a test can see
the original's limit.

### The callers' arguments {#u7-callers}

| Caller | Query | `start` | `ignored` | `typeMask` | `flagMask` |
| --- | --- | --- | --- | --- | --- |
| `IPhysics_OverlapShape` `0x0033ec58` | box | the query body | its owner | `+0x14` or −1 | 0 |
| `PhysicsBody_SweepHumanMask` `0x0033d598` | box | the moving body | its owner | −1 | the mover's layer mask ([Sweeping a body](physics.md#sweep)) |
| `Human_TestStrikes` `0x0033f4bc` | box (the human body's broad bound `+0x90`) | none | the striker's owner | −1 | `0x18` |
| `IPhysics_RayCastBodies` `0x0033efe8` | box | none | its `t0` argument | its `a3` argument | `0x2000` |
| `IPhysics_CollideShape` `0x0033e91c` | filtered | the query body | table above | | |

### The bound refresh {#u7-refresh}

In `IPhysics_Step` (`0x00340940`-`0x00340994`), before the settle slots: `+0x12 += 1` (u16); if it is now below 120
(`0x78`), nothing else. Otherwise `+0x12 = 0`, `+0x14 = 0.0`, and for each body of the list from the head, `+0x14 =
max(+0x14, max.x − min.x)` (strict `>`). So after construction the refresh runs on the 120th, 240th, … step, and
between refreshes the bound only grows (on inserts). The refresh reads the bodies' current boxes; an empty list
leaves 0.

### Test ideas {#u7-tests}

Synthetic bodies with boxes only (y and z `[0, 1]` unless said), owners as distinct tags, type mask −1, flags 0. The
numbers come from a model of this spec (computed 2026-10-12).

1. **Insert ties.** Add `A(0) B(1) C(1) D(2)` (min x; width 0.5) → list `A B C D`. Add `E(1)` → `A B C E D` (after
   its equals). Bound 0.5.
2. **Reinsert unmoved.** From test 1, reinsert `B` without changing it → `A C E B D`.
3. **Reinsert backward.** `A(0) B(1) C(1) D(2)`; `D` moved to `[0, 0.5]`, reinsert → `D A B C` (before its equal
   `A`). `D` moved to `[1, 1.5]` instead → `A B C D`.
4. **Remove returns the previous node.** In `A B C`, removing `B` returns `A`; removing the head `A` returns null
   and makes `B` the head with `B.prev` null.
5. **Swap-remove.** Add `A, B, C` (indices 0, 1, 2); remove `A` → vector `C, B`, `C+0x20 = 0`, `A+0x20` still 0.
6. **Query order.** Bodies `W[0, 10] P[1, 2] Q[6, 7] S[8, 9] R[8.5, 9.5] T[11, 12]` (list in that order, bound 10).
   `QueryBox(box x [8, 9], start S, ignored S)` → `W, R` (backward: `S` dropped as ignored, `Q` and `P` skipped on
   max x, `W` kept; forward: `R` kept, `T` stops it). With no start → `W, R`. Box `[8, 12]`, start `S`, `ignored`
   none → `S, W, R, T`.
7. **No upper x test backward.** Same bodies, box x `[0, 0.2]`, start `S`, ignored `S` → `Q, P, W`; a brute-force
   overlap gives only `W`.
8. **A stale bound stops the walk.** Bodies `W[0, 10] Q[6, 7] S[8, 9]`, bound forced to 1.0, box `[8, 9]` from `S`,
   ignored `S` → nothing (`8 − 0 > 1` stops at `W`). With the true bound 10 → `W`.
9. **Filters.** A body with type mask 0 is never found; `typeMask` 0 finds nothing; `flagMask` `0x4` finds only
   bodies with `0x4`; `ignored` = 0 drops an ownerless body; touching y or z boxes (`max.y == box.min.y`) are found.
10. **Filtered query.** As test 6 with `excludedFlags 0x200` on `R` → `W`; with a fake state `0x4000000` on `W`'s
    owner and `excludedState 0x4000000` → `R`; with `keep` refusing `W`'s owner → `R`; the state callback is called
    once per candidate that passed the first four filters, also with `excludedState` 0.
11. **Bound refresh.** `A[0, 4]`, `B[5, 6]` → bound 4; shrink `A` to `[0, 1]` and reinsert → still 4. After step 1:
    counter 1, bound 4; after step 119: 119, 4; after step 120: counter 0, bound 1; step 121: counter 1, bound 1.
12. **Overlap options.** A fake overlap entry that always hits: with `max` 0, no candidate is tested (fake called 0
    times); with options `0x2012` the result equals options `0x12`; two shapes on one candidate give two contacts;
    with `0x80` and both groups 7 the candidate is skipped, with groups 7 and 0 it is not.
13. **Update sweep box.** A sphere body (radius 0.35) whose shape moved from x 0 to x 1 with flags `0x801`: the
    candidate box is x `[0.65, 2.35]`, so a body at x `[0.1, 0.4]` is not met and one at `[2.0, 2.2]` is.
14. **Update gate.** Flags 0: `Update` poses nothing and does not reinsert; flags `0x800`: poses and reinserts every
    call, keeping `0x800`; flags `0x80000000`: poses, reinserts and clears it, so the next call does nothing.

## Unit 8: Ignore lists and shape pools {#unit-8}

**What it is.** The 60 ignore slots a body uses to pass through particular owners, the human body's two extra rules,
and the fixed shape pools with their allocators. Read in the disassembly on 2026-10-12; all confirmed (code) at the
cited addresses unless marked.

- **Coney:** `src/physics/physics_world.*`: fixed pools with the original's counts and "first free" scans, so running
  out and reuse behave the same; the ignore slots as one array of 60.
- **Tests:** [Test ideas](#u8-tests): the 61st `Ignore`, slot reuse, pointer matching, the grab partner, the pool
  limits and the flags a freed shape keeps.

14 functions, 1032 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00340890` | `IPhysics_FindFreeIgnoreSlot` | 68 |
| `0x0033cd70` | `IPhysics_ForgetIgnoredOwner` | 140 |
| `0x00341ae8` | `PhysicsBody_IsIgnoring` | 56 |
| `0x00341b20` | `PhysicsBody_Ignore` | 92 |
| `0x00341b80` | `PhysicsBody_StopIgnoring` | 140 |
| `0x003421d0` | `PhysicsHumanBody_IsIgnoring` | 92 |
| `0x00342230` | `PhysicsHumanBody_Ignore` | 48 |
| `0x00342260` | `PhysicsHumanBody_StopIgnoring` | 36 |
| `0x0033d1a0` | `IPhysics_AllocObjectSphere` | 68 |
| `0x0033d1e8` | `IPhysics_AllocObjectBox` | 68 |
| `0x0033d230` | `IPhysics_AllocCarBox` | 68 |
| `0x0033d278` | `IPhysics_AllocBigSphere` | 80 |
| `0x0033d2c8` | `IPhysics_FreeShape` | 16 |
| `0x003408d8` | `IPhysics_FindFreeStrikeSphere` | 60 |

### The ignore slot {#u8-slot}

60 slots of 12 bytes at IPhysics `+0xf4e0`, zeroed by the constructor (720 bytes, `0x0033c830`): `+0x00` the body
whose list holds it, `+0x04` the ignored owner, `+0x08` the next slot of that body's chain. A slot is **free when
`+0x04` is 0**. The chain starts at body `+0x38`. Owners are matched by **pointer equality** on the owner object
(the task object a body's `+0x34` holds), so ignoring an owner passes through every body that owner has.

### IPhysics_FindFreeIgnoreSlot {#u8-find}

`0x00340890`. `Slot* (IPhysics* p)`: the lowest-index slot with `+0x04 == 0`, or null when all 60 are in use. It
does not mark the slot.

### PhysicsBody_IsIgnoring {#u8-is}

`0x00341ae8`, body vtable `+0x30`. `bool (Body* b, Owner* o)`: false when `o` is null; else walks the chain from
`+0x38` and answers true at the first slot with `+0x04 == o`.

### PhysicsBody_Ignore {#u8-ignore}

`0x00341b20`, vtable `+0x40`. `void (Body* b, Owner* o)`: nothing when `o` is null. Else `slot =
IPhysics_FindFreeIgnoreSlot(*0x00597198)` (the global IPhysics, not an argument); none → nothing. Else `slot+0x04 =
o`, `slot+0x08 = b+0x38`, `slot+0x00 = b`, `b+0x38 = slot`: **pushed at the front**. There is **no duplicate check**:
ignoring the same owner twice uses two slots.

### PhysicsBody_StopIgnoring {#u8-stop}

`0x00341b80`, vtable `+0x38`. `void (Body* b, Slot* s)`: the argument is a **slot**, not an owner.

- `s` not null: if `s` is the chain head, `b+0x38 = s+0x08`; else walk the chain for the slot whose `+0x08 == s`
  and set it to `s+0x08`. Then, **found or not**, `s+0x00 = s+0x04 = s+0x08 = 0` (freed).
- `s` null: every slot of the chain, from the head: `+0x04 = 0`, `+0x08 = 0`, `+0x00 = 0`; `b+0x38` ends null.

Only `IPhysics_ForgetIgnoredOwner` passes a slot; the free functions pass null ([Unit 9](#unit-9), for example
`0x0033cf3c`).

### IPhysics_ForgetIgnoredOwner {#u8-forget}

`0x0033cd70`. `void (IPhysics* p, Body* removed)`: for slot 0 to 59, if `slot+0x04 == removed+0x34`, call
`slot+0x00`'s vtable `+0x38` with that slot (the slot's body, which may be a human body). Called first by
`IPhysics_RemoveBody` ([Unit 7](#u7-addremove)). It frees the slots in **other** bodies' lists that name the removed
body's owner; the removed body's **own** list is freed by its free function (`StopIgnoring(null)`). An ownerless
body would match every free slot, whose body is null: undefined, never done (inferred: every free function runs
while the owner is still set, `0x0033cf0c` before `0x0033cf48`).

### The human body's three {#u8-human}

Human body vtable `0x005446b0`: `+0x30` `0x003421d0`, `+0x38` `0x00342260`, `+0x40` `0x00342230`. The human body's
`+0xd4` is a word: non-zero = "ignore the level".

- **`PhysicsHumanBody_IsIgnoring`** (`0x003421d0`), `(hb, o)`: `o` null → returns **`hb+0xd4`** (the level). Else,
  if `o` is the owner human's **grab partner** (`Human_GetGrabOther` `0x002269d8` on `hb+0x34`: the other human of
  the human's grab record, 0 when none) → true. Else `PhysicsBody_IsIgnoring(hb, o)`.
- **`PhysicsHumanBody_Ignore`** (`0x00342230`), `(hb, o)`: `o` null → `hb+0xd4 = 1`; else `PhysicsBody_Ignore`.
- **`PhysicsHumanBody_StopIgnoring`** (`0x00342260`), `(hb, s)`: `s` null → `hb+0xd4 = 0`, then
  `PhysicsBody_StopIgnoring(hb, null)` (all slots too); else `PhysicsBody_StopIgnoring(hb, s)`.

Who asks `IsIgnoring` (body vtable `+0x30`) is not traced here: the static calls are only the human's to the base
(`0x00342204`); the owners' `+0x110` answers are the likely askers ([Unit 11](#unit-11)). Inferred.

### The shape pools {#u8-pools}

Every shape is built once by the IPhysics constructor (flags `0x1`, enabled, by `PhysicsShape_Construct`
`0x00342a30`; `0x8` clear). The allocators scan from index 0 and take the **first** shape whose test passes; past the
end they return null. They write only `+0x32` (and the big sphere's radius): type, bone, offset and size keep the
previous user's values until the caller sets them.

| Pool | IPhysics offset | Count × size | Allocator | Free when | On allocation |
| --- | --- | --- | --- | --- | --- |
| object spheres | `+0x22930` | 416 × 0x60 | `IPhysics_AllocObjectSphere` `0x0033d1a0` | flag `0x8` clear | sets `0xb` |
| object boxes | `+0x2c530` | 416 × 0x90 | `IPhysics_AllocObjectBox` `0x0033d1e8` | flag `0x8` clear | sets `0xb` |
| car boxes | `+0x4c720` | 36 × 0x90 | `IPhysics_AllocCarBox` `0x0033d230` | flag `0x8` clear | sets `0xb` |
| 2.0 m spheres | `+0x4db60` | 30 × 0x60 | `IPhysics_AllocBigSphere` `0x0033d278` | flag `0x8` clear | radius `+0x40` = 2.0 (`0x40000000`), sets `0xb` |
| weapon strike spheres | `+0xf7b0` | 60 × 0x60 | `IPhysics_FindFreeStrikeSphere` `0x003408d8` | flag **`0x1`** clear | **nothing**: the caller enables it |
| ignore slots | `+0xf4e0` | 60 × 12 | `IPhysics_FindFreeIgnoreSlot` `0x00340890` | `+0x04` == 0 | nothing; `Ignore` fills it |

The counts are the loop limits `0x1a0`, `0x1a0`, `0x24`, `0x1e`, `0x3c`, `0x3c` (signed compares for the shape
pools). `0xb` = enabled `0x1`, `0x2`, allocated `0x8`. The strike spheres are switched off and given `0x2` by the
constructor (`0x0033c7e0`-`0x0033c814`), so they start free; a strike sphere stays taken while it is enabled, and the
same one is returned again until its caller enables it.

`IPhysics_FreeShape` (`0x0033d2c8`), `(p, shape)`: clears **only** `0x8`. A freed pool shape keeps `0x1` and `0x2`
(flags `0x3` after a plain allocate and free), so it still counts as enabled to any code that reaches it through a
stale pointer; the next allocation sets `0xb` again. The other pools (the human sets, the world-object, glass and car
bodies, glass boxes) are taken by the create functions of [Unit 9](#unit-9) (a body is free when its owner `+0x34`
is 0, [Bodies](physics.md#bodies)).

### Test ideas {#u8-tests}

1. **Sixty slots.** 60 `Ignore` calls with distinct owners on one body take slots 0 to 59 in order; the chain reads
   59, 58, …, 0 from `+0x38`; a 61st `Ignore` changes nothing (the chain and all slots unchanged).
2. **Reuse.** Free slot 5 (`StopIgnoring(b, slot 5)`); the next `Ignore` takes slot 5 and puts it at the chain's
   front.
3. **Duplicates.** `Ignore(o)` twice → two slots; `IsIgnoring(o)` true; `ForgetIgnoredOwner` of a body owned by `o`
   frees both.
4. **Null owner.** Base body: `Ignore(null)` takes no slot, `IsIgnoring(null)` is false. Human body: `Ignore(null)`
   sets `+0xd4 = 1`, `IsIgnoring(null)` returns 1; `StopIgnoring(null)` sets it to 0 and frees all its slots.
5. **Stop a slot not in the chain.** `StopIgnoring(b, s)` with `s` from another body's chain: `b`'s chain unchanged,
   `s` zeroed (its own chain then still points at it: the original's behaviour, a test of the zeroing only).
6. **Forget.** Bodies `X` (owner `x`) and `Y`, `Z` both ignoring `x` and `Y` ignoring `z`; removing `X` frees the
   two `x` slots and keeps `Y`'s `z` slot.
7. **Grab partner.** A human body whose owner's grab partner is `g`: `IsIgnoring(g)` true with an empty chain; with no
   grab, false.
8. **Pools.** 416 `AllocObjectSphere` calls succeed, the 417th returns null; `FreeShape` on the 3rd, then the next
   allocation returns the 3rd. Flags after allocate `0xb`, after free `0x3`, after a second allocate `0xb`.
   `AllocBigSphere` sets radius 2.0 even if the slot held 0.5. 37 `AllocCarBox` → the 37th is null.
9. **Strike spheres.** `FindFreeStrikeSphere` twice without enabling returns the same sphere; after enabling it, the
   next; with all 60 enabled, null.

## Unit 9: Creating and freeing bodies {#unit-9}

**What it is.** The pools' create and free functions: which slot is taken, what each kind of body starts with, the
human list, and what a free leaves behind. All confirmed (code) at the cited addresses (read in the disassembly,
2026-10-12) unless a line says otherwise.

- **Coney:** `src/physics/physics_world.*` (fixed pools with the original's counts, so running out behaves the same);
  callers in `src/human/human.cpp` (`Human_CreatePhysicsBody`'s rules, [below](#u9-callers)),
  `src/world_objects/object_bodies.*`, `cars.*`, `glass.*`.
- **Tests:** [Test ideas](#u9-tests): each kind's starting flags (human `0x8002243f`, then `0x8002203f` /
  `0x8002223f` for an AI; punch bag `0x8000243f` then `0x8000263f`; object `0x80000500`; car `0x80000000`, then
  `0x8002211a` after its boxes; pane `0x8000007a`), shapes and scales; the first free slot is reused (the heal keeps
  the same body pointer, as seen at runtime); a full pool returns null; the body vector's swap-remove.

12 functions, 1752 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0033cc48` | `IPhysics_CreateHumanBody` | 292 |
| `0x0033ce00` | `IPhysics_FreeHumanBody` | 84 |
| `0x0033cb08` | `IPhysics_CreatePunchBagBody` | 228 |
| `0x0033cbf0` | `IPhysics_FreePunchBagBody` | 84 |
| `0x0033ce58` | `IPhysics_CreateObjectBody` | 168 |
| `0x0033cf00` | `IPhysics_FreeObjectBody` | 100 |
| `0x0033cf68` | `IPhysics_CreateCarBody` | 140 |
| `0x0033cff8` | `IPhysics_FreeCarBody` | 120 |
| `0x0033d070` | `Physics_CreateBoxBody` | 228 |
| `0x0033d158` | `IPhysics_FreeBoxBody` | 68 |
| `0x003407a0` | `IPhysics_AddHumanBody` | 88 |
| `0x003407f8` | `IPhysics_RemoveHumanBody` | 152 |

Notation as in [Unit 3](#u3-records): vtable slots are named by their adjust / function offsets (`+0x38` / `+0x3c`);
"enable(shape, on)" is the shape vtable `+0x18` / `+0x1c` slot, `0x004f11c8` for every shape type: on sets flags
`0x11` (enabled and reset-previous, [Unit 6](#u6-reset)), off clears `0x1` only.

### What the constructor leaves {#u9-constructed}

The pools are built once by `IPhysics_Construct` (`0x0033c288`); a create function overwrites only some fields, so the
rest is whatever the constructor or the slot's last user left. Confirmed (code) at `0x003418f8` (body),
`0x00341e98` (human body), `0x00342a30` (shape head):

- **Body** (`PhysicsBody_Construct`): `+0x30` first shape 0, `+0x34` owner 0, `+0x38` ignore list 0, `+0x3c` group 0,
  `+0x40` flags `0x80000400`, `+0x44` type mask 0, `+0x50` push-out (0, 0, 0, 1), `+0x60` scale (1, 1, 1, 1),
  `+0x70` 0, the sort node cleared (`PhysicsNode_Init`, index `+0x20` = −1).
- **Human body** (`PhysicsHumanBody_Construct`): the body, then `+0x80` = `+0x84` = 0, the broad bound `+0x90`'s node
  initialised, `+0xc0` = `+0xc4` = 0, flags `|= 0x3f` (so `0x8000043f`), `+0xc8` = `+0xcc` = `+0xd0` = `+0xd4` = 0.
- **Each human body set *i*** (0 to 59) gets the capsule *i* of `+0x10e30` (`PhysShapeCapsule_SetSize(2.0, 0.35)`,
  `0x40000000`, `0x3eb33333`) as its first shape `+0x30`, and ten bone shapes chained on `+0x80` in this order
  (`PhysicsHumanBody_AddBoneShape` `0x003429e8` appends at the tail): the five segments *5i* to *5i+4* of `+0x71a0`
  for bones 3, 24, 18, 32, 29, then the five spheres *5i* to *5i+4* of `+0x120` for bones 6, 25, 19, 33, 30. Their
  offsets, radii and lengths come from the tables at `0x00512810` (segment offsets, 16 bytes each), `0x005128b0`
  (segment radii), `0x005128e0` (segment lengths), `0x00512860` (sphere offsets) and `0x005128c8` (sphere radii),
  index *k* of each table going with the *k*-th bone of its list; the values are on
  [Combat](combat.md#moving-strikes). Flags: the shape head's `0x1`, plus `0x2`, plus `0x4` for bone 3 and bone 6
  (`0x7`; the others `0x3`). The capsule's flags are `0x1`.
- **The 60 weapon strike spheres** (`+0xf7b0`) are switched off and get `0x2` (flags `0x2`). The 30 push spheres
  (`+0x4db60`) are plain spheres (bone −1, offset 0, flags `0x1`).
- **The punch bag's set** (`+0x4f2a0`): capsule `+0x4f3f0` (2.0, 0.35) as `+0x30`, one segment `+0x4f380` as `+0x80`
  with offset 0, radius 0.35 (`+0x4f3e0`), length 1.5 (`0x3fc00000`), bone 3 and flags set to **`0x4`** outright
  (not enabled, a strike target, not a striker).

### Taking a slot {#u9-create}

Every create function takes `(phys, owner)` in `a0`, `a1` (the box body also two floats, [below](#u9-box)) and scans
its pool from index 0 for the first body whose owner `+0x34` is 0; a full pool returns **0** (null) and changes
nothing. On the slot it calls `PhysicsBody_SetOwner(body, owner)` (`0x00341a68`: `+0x34` = owner and, when owner is
not null, `+0x44` = the owner's type mask from its vtable `+0x20` / `+0x24`), writes the fields below, and ends with
`IPhysics_AddBody` (`0x00340668`), which appends the body to the body vector (`+0x4f450`; `+0x20` = its index, the
size before the append) and inserts it into the x-sorted list ([Queries](physics.md#queries)) **with its stale AABB**:
the
dirty bit makes the next update re-pose and re-sort it.

| Kind | Function | Pool (count, stride) | Writes | Then |
| --- | --- | --- | --- | --- |
| human | `IPhysics_CreateHumanBody` `0x0033cc48` | `+0x124b0` (60, 0xe0) | flags `0x8002243f`, scale (1, 1, 1, 1), group 0, `+0x70` 0, `+0xc0` 0, `+0xc4` 0, `+0xd0` 0; enable(capsule, on) then capsule flags `&= ~0x2`; every bone shape enable(off) | `IPhysics_AddHumanBody`, `IPhysics_AddBody`, `PhysicsBody_ApplyHumanScale` ([Unit 10](#u10-scale)) |
| punch bag | `IPhysics_CreatePunchBagBody` `0x0033cb08` | the one set `+0x4f2a0`, no free test | flags `0x8000243f`, scale 1, group 0, `+0x70` 0, `+0xc0` 0, `+0xc4` 0, `+0xd0` 0; enable(capsule, on), `&= ~0x2`; enable(segment, off) | `IPhysics_AddHumanBody`, `IPhysics_AddBody`; **no** scale step |
| world object | `IPhysics_CreateObjectBody` `0x0033ce58` | `+0x15930` (416, 0x80) | flags `0x80000500`, group 0, `+0x70` 0, scale 1; no shape (the caller adds one) | `IPhysics_AddBody` |
| car | `IPhysics_CreateCarBody` `0x0033cf68` | `+0x4be20` (18, 0x80) | flags `0x80000000`, group 0, `+0x70` 0; **scale not written**; no shape | `IPhysics_AddBody` |
| glass pane | `Physics_CreateBoxBody` `0x0033d070` | `+0x3af30` (255, 0x80) with box *i* of `+0x42eb0` | flags `0x8000007a`, group 0, `+0x70` 0, scale not written, the box ([below](#u9-box)) | `IPhysics_AddBody` |

The human flags `0x8002243f` are dirty, `THROWNWEAPONTARGET` (`0x20000`), `RAYCASTTARGET` (`0x2000`), `0x400` (write
the slid velocity back, [Unit 3](#u3-records)) and `0x3f` (`0x1` sweep against bodies, then the layers `0x2`, `0x4`,
`0x8`, `0x10`, `0x20`); the punch bag's lack `0x20000`. Quirks of the creates, confirmed (code) by what they do not
write: none clears the pending push-out `+0x50`, the ignore list `+0x38` or (human) `+0x84` and `+0xd4`; the frees
clear those that matter ([Freeing](#u9-free)), so a body is only stale if its last free skipped them (the pane, below).
None writes `+0x00`-`+0x1c` (the AABB). A car's and a pane's scale is never written by anyone in these units, so it
stays the constructor's 1 (inferred: no other writer of a car or pane body's `+0x60` was found).

### The box body {#u9-box}

`Physics_CreateBoxBody(phys a0, owner a1, width f12, height f13)` (`0x0033d070`; the name lists its floats first as
the decompiler showed them). For pool index *i* it uses box *i* of `+0x42eb0` (the two pools advance together):
`PhysicsBox_SetSize(width, 0.5, height, box)` (`0x00342dc0`, which halves its three sizes: half-extents
**(width / 2, 0.25, height / 2)**, `+0x4c` = 1), box flags `|= 0x9` (enabled, allocated), body `+0x30` = the box.
The only caller is `GlassPane_CreateBody` (`0x0038ed68`) with the pane's `+0xcc` and `+0xd4` (width and height,
[Objects](objects.md#glass)), which then calls the body's update (vtable `+0x10` / `+0x14`) with the pane, so the box
is posed by the pane's matrix at once.

### The human list {#u9-human-list}

`IPhysics_AddHumanBody(phys, body)` (`0x003407a0`) **appends at the tail** of the list headed at IPhysics `+0x04`
(next `+0xc8`, previous `+0xcc`; an empty list makes the body the head with both links 0). `IPhysics_RemoveHumanBody`
(`0x003407f8`): first the body's `StopIgnoring(null)` (vtable `+0x38` / `+0x3c`; for a human body
`PhysicsHumanBody_StopIgnoring` `0x00342260`: `+0xd4` = 0 and every slot of its ignore list freed), then the unlink
(the head moves to the next, whose previous becomes 0; otherwise previous.next = next and next.previous = previous),
then `PhysicsHumanBody_ClearStrikeSpheres` (`0x00342168`, [Unit 10](#u10-weapon)), then both links 0. Nothing here
reads the list; who walks it is not traced in these units.

### Freeing {#u9-free}

All take `(phys, body)`. `IPhysics_RemoveBody` (`0x00340700`) is common to all: `IPhysics_ForgetIgnoredOwner`
(`0x0033cd70`: every ignore slot in any body's list that names this body's owner is freed through that body's
`StopIgnoring`), then a **swap-remove** from the body vector (when it holds two or more, the last body moves into
this one's index and takes its `+0x20`; the removed body's `+0x20` is **not** reset), then `BodyList_Remove`.

| Function | Steps, in order |
| --- | --- |
| `IPhysics_FreeHumanBody` `0x0033ce00` / `IPhysics_FreePunchBagBody` `0x0033cbf0` (identical) | `IPhysics_RemoveHumanBody`, `IPhysics_RemoveBody`, owner 0, `PhysicsHumanBody_SetPushSphere(body, 0)` (frees the push sphere, [Unit 10](#u10-push-sphere)). Flags, `+0xc0` and the bone shapes are left as they are (the next create resets them) |
| `IPhysics_FreeObjectBody` `0x0033cf00` | `IPhysics_RemoveBody`; the first shape's `0x8` cleared (not its other flags); `StopIgnoring(null)`; owner 0; `+0x30` = 0 |
| `IPhysics_FreeCarBody` `0x0033cff8` | `IPhysics_RemoveBody`; each shape unchained in turn (its next 0, its `0x8` cleared, `+0x30` = the following one), ending with `+0x30` = 0; `StopIgnoring(null)`; owner 0 |
| `IPhysics_FreeBoxBody` `0x0033d158` | `IPhysics_RemoveBody`; the box's `0x8` cleared; owner 0. **No** `StopIgnoring` and `+0x30` kept (the next create of that slot rewrites it with the same box) |

Owner 0 is written with `PhysicsBody_SetOwner(body, 0)`, which leaves the type mask `+0x44` as it was.

### The callers' rules {#u9-callers}

`Human_CreatePhysicsBody` (`0x0021ce08`), confirmed (code): if the human has a body (`+0x1a0`) it is freed first
(`Human_FreePhysicsBody` `0x0021cf20`: the punch bag's free for class `0x1cc`, else the human free; `+0x1a0` = 0).
For class `0x1cc` (`HeavyBag`) the punch bag's set, flags `|= 0x200`. Otherwise a human body; then when the human's
brain `+0x04` is 0 (the player, inferred from the 1.4286 scale [Characters](characters.md#walls) gives the player) flags
`|= 0x400` (already set) and scale `+0x60` = (1.4286, 1.4286, 1, 1) (`0x3fb6db6e`); otherwise flags `&= ~0x400`;
then, when human byte `+0x3bf` is 0, flags `|= 0x200`. Corroboration: the runtime flags of [Layers](physics.md#layers)
(humans
`0x2203f` and `0x2223f`, cars `0x2211a`, panes `0x7a`) are these values without the dirty bit.

`Car_MakeBodies` (`0x00387d50`) ORs **`0x2211a`** into its body's flags at `0x00387ea8`, so a car's body reads
`0x8002211a` (not `0x8002231a`).

### Test ideas {#u9-tests}

A fake owner (type mask 0x40 for a human, 8 for an object, scale attribute *s*). Values from this spec.

1. **Human create.** Fresh world, owner with scale 1: returns pool slot 0; flags `0x8002243f`; capsule flags `0x11`;
   bone shapes `0x6` (bones 3, 6) and `0x2` (the rest); `+0x60` = (1, 1, 1, 1); `+0x20` = 0; human list = [it]; after
   the scale step the capsule is height 2.0, radius 0.35 and the body is still dirty until its update.
2. **Scale 1.1** (not in a scene): capsule height 2.2, radius 0.385; spine segment offset (−0.077, 0, 0.055), radius
   0.198, length 0.418; forearms radius 0.077, length 0.22; shins 0.11, 0.44; head offset (0.055, 0, 0.033), radius
   0.165; hands offset (0.088, 0, 0), radius 0.099; feet 0.165 (float32 values to 1e-6). In a scene state the same
   human reads scale 1.0 (below 1.1, `Human_GetScale` `0x0021d020`).
3. **Pool limits.** 60 human creates succeed, the 61st returns null; 416 objects, 18 cars, 255 panes likewise.
4. **Reuse.** Create A, B, C (slots 0, 1, 2); free B; the next create returns slot 1 (lowest free), not slot 3.
5. **Swap-remove.** Bodies A, B, C in the vector at 0, 1, 2; remove A: C is at index 0 with `+0x20` = 0, size 2, A's
   `+0x20` still 0.
6. **Player and AI.** Player: flags `0x8002243f`, scale (1.4286, 1.4286, 1, 1); AI with `+0x3bf` = 1: `0x8002203f`;
   AI with `+0x3bf` = 0: `0x8002223f`; punch bag: `0x8000263f`, segment flags `0x4`, capsule `0x11`.
7. **Car.** Create: `0x80000000`, no shape; after its boxes: `0x8002211a`. Free: both boxes lose `0x8`, `+0x30` = 0.
8. **Pane.** `CreateBoxBody(1.2, 2.0)`: half-extents (0.6, 0.25, 1.0), box flags gain `0x9`; free clears `0x8` only.
9. **Free human.** A human body with a push sphere, two ignore slots and `+0xd4` = 1: after the free, the sphere's
   `0x8` is clear, both slots are free, `+0xd4` = 0, the list no longer holds it, its owner is 0; another body's slot
   naming its owner is freed too.

## Unit 10: The human body set: update, strikes and push sphere {#unit-10}

**What it is.** What the human body adds to a body: the scale step, the per-update pose and broad bound, the strike
switch, the strike test's gathering (the parts [Unit 5](physics-kernels.md#u4b-how-human-teststrikes-builds-the-sweep-0)
does not
cover), the weapon spheres and the push sphere. All confirmed (code) at the cited addresses (disassembly read
2026-10-12) unless a line says otherwise.

- **Coney:** `src/human/human.*` (body update after the move, the push), `src/combat/*` (`Human_TestStrikes`: the
  gather, the line-of-sight ray and the per-candidate loop below, sweeping each enabled strike shape from its last pose
  through the sweep table: [Unit 5](physics-kernels.md#unit-5) for boxes, [Unit 11](#unit-11) for humans),
  `src/platform/play_level_mode.*` (`strikeObjects()`). Issue #72's step 1 (strikes hit cars) needs Unit 5, [the
  gather](#u10-strikes) and the sweep loop, not the whole of this unit.
- **Tests:** [Test ideas](#u10-tests): bone shapes switch on by bit `1 << ((id − 2) & 31)` and every switch empties the
  struck list; `Moved` moves each switched-on sphere to the body's new origin (prev untouched), so the next sweep runs
  from the origin, not across the teleport; the broad bound's radius `max(0.8 × scale, reach)`; the push of
  0.25 m × w / (w × k) along the 3-D line, not applied to the target, a player, a held (k = 1e9) or an unpushable one;
  one strike per candidate body per update.

14 functions, 6168 bytes (1 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00341e98` | `PhysicsHumanBody_Construct` | 104 |
| `0x00341f00` | `PhysicsHumanBody_Destroy` | 40 |
| `0x00341f28` | `PhysicsBody_ApplyHumanScale` | 412 |
| `0x003420c8` | `PhysicsHumanBody_GetShapes` | 8 |
| `0x003420d0` | `PhysicsHumanBody_SetPushSphere` | 92 |
| `0x00342130` | `PhysicsHumanBody_OnStruck` | 28 |
| `0x00342158` | `PhysicsHumanBody_AddStrikeSphere` | 16 |
| `0x00342168` | `PhysicsHumanBody_ClearStrikeSpheres` | 100 |
| `0x003422d0` | `PhysicsHumanBody_Update` | 748 |
| `0x003425c0` | `PhysicsHumanBody_PushAwayHumans` | 616 |
| `0x00342828` | `PhysicsHumanBody_Moved` | 168 |
| `0x003428d0` | `PhysBody_SetShapeEnabled` | 280 |
| `0x003429e8` | `PhysicsHumanBody_AddBoneShape` | 72 |
| `0x0033f110` | `Human_TestStrikes` (tagged) | 3484 |

### The record and its vtable {#u10-record}

Human body vtable `0x005446b0` (read from the binary), slot adjust / function: `+0x08` / `+0x0c` destructor
`0x00341f00` (resets the vtable, then the body destructor); `+0x10` / `+0x14` Update `0x003422d0`; `+0x18` / `+0x1c`
Moved `0x00342828`; `+0x20` / `+0x24` AddShape `0x00341aa8` (the body's); `+0x28` / `+0x2c` GetShapes `0x003420c8`
(returns `+0x30`, the capsule, whatever arguments it gets); `+0x30` / `+0x34` IsIgnoring `0x003421d0`; `+0x38` /
`+0x3c` StopIgnoring `0x00342260`; `+0x40` / `+0x44` Ignore `0x00342230`; `+0x48` / `+0x4c` GetContactScale
`0x00342288` ([Unit 3](#u3-human-scale)); `+0x50` / `+0x54` OnStruck `0x00342130` (calls `Human_OnPushContact`
`0x00219b08` with the owner, [Human contacts](physics.md#human-contacts)). The struck list of the strike test is this
ignore
list: IsIgnoring answers `+0xd4` for a null owner, **yes for the owner human's grab partner** (`Human_GetGrabOther`
`0x002269d8`, the anim record `+0xd4`'s handle `+0xc4`), else the ordinary list; Ignore(null) sets `+0xd4` = 1;
StopIgnoring(null) clears `+0xd4` and the whole list.

Fields this unit uses ([The human body](physics.md#human-body)): `+0x30` capsule, `+0x34` owner, `+0x40` flags (`0x4000`
skips the update; `Human_RestoreBodyAfterClimb` `0x00281368` clears it, so a climb sets it, inferred), `+0x80` bone
chain, `+0x84` push sphere, `+0x90` broad bound (two vec4, min and max, `w` = 1), `+0xc0` strike bits, `+0xc4`
weapon sphere chain, `+0xd0` weapon timer (updates left), `+0xd4` ignore the level.

### PhysicsBody_ApplyHumanScale {#u10-scale}

`0x00341f28 (body)`. `s` = `Human_GetScale(owner)` (`0x0021d020`: human `+0x65c`, but 1.0 while
`Human_IsInSceneState` and `+0x65c` < 1.1). Capsule height `+0x44` = `Human_GetCapsuleHeight` (`0x00219890`: 2 ×
*s*), radius `+0x40` = `Human_GetCapsuleRadius` (`0x00219860`: 0.35 × *s*). Then it walks the bone chain assuming the
constructor's order ([Unit 9](#u9-constructed)), with no null test: the first five shapes as segments (`+0x10` =
table offset × *s* in xyz, `w` the table's 0; `+0x60` radius × *s*; `+0x64` length × *s*), the next five as spheres
(`+0x10` offset × *s*, `+0x40` radius × *s*). It reads the scale afresh for every field (the same value). Finally it
sets the dirty bit and calls the body's Update with the owner, so the capsule and bound are rebuilt at once.

### PhysicsHumanBody_Update {#u10-update}

`0x003422d0 (body a0, owner a1)`; called at the end of `Human_Move` (`0x0023db20`), just before the push
([below](#u10-push)), and so before `Human_TestStrikes`, which `0x0023fea8` runs after the move
([Objects](objects.md#glass)). Let `F` be the flags read on entry.

1. If `F & 0x4000`: return (nothing posed).
2. If `F` is dirty (`0x80000000`): pose the capsule (shape `+0x28` / `+0x2c` with the owner), body AABB = the
   capsule's bounds alone (`PhysicsBody_ComputeBounds` `0x00341638`), re-sort (`BodyList_Reinsert` `0x00340628`),
   clear the dirty bit in memory. `F` keeps its dirty bit for step 4.
3. If the weapon timer `+0xd0` ≠ 0: mark `F` dirty (locally), and pose every weapon sphere of `+0xc4` with the held
   object (`Human_GetHeldObject` `0x00226ff0`) or, when none, the owner. Else, if `+0xc0` ≠ 0, mark `F` dirty
   (locally).
4. If `F` is dirty (so: dirty on entry, or a weapon timer, or any strike bit), rebuild the **broad bound** `+0x90`:
    - `R` = 0.0; if the owner is a human with an anim record (`+0xd4`) whose Anim Range List pointer (`+0x160`) is
      set, `R` = that list's reach (`+0x04`) for the record's clip id (`+0x20`) (`AttackTable_GetReach` `0x002544a0`,
      [Animation](formats/animation.md#anim-range-list)); the id is not range-checked.
    - `c` = the capsule's world position (its base `+0x50`), `c.z += capsule height × 0.5`.
    - `r` = `Human_GetScale(owner)` × **0.8** (`0x3f4ccccd`).
    - With weapon spheres (`+0xc4` set): `r = max(r, |(sphere centre − capsule base) with z = 0| + sphere radius)` for
      the **first** sphere of the chain only, and `R` is **not** used. Without: `r = max(r, R)`.
    - `+0x90` = `AABB_FromUprightCylinder(c, height, r)` (`0x00341708`, [Unit 6](#u6-aabb)): x and y in
      `[c ± r]`, z from the capsule's base to base + height; `w` = 1.
5. Pose every bone shape whose flags have `0x1` **or** `0x4` (so the spine and head always) with the owner.
6. If a push sphere is held (`+0x84`), pose it with the owner (bone −1, offset 0: its centre is the human's origin).

Numbers (Rembrandt-class reaches from [Frame data](combat-moves.md#frame-data), scale 1): a standing human, no strike
and not dirty, keeps his last bound; during `S1` (reach 0.94) `r` = 0.94; `SX2` 1.37; `SSX3` 1.47; `STRAFE_FRONT`
1.74; the push 653 1.58; at scale 1.3 and `S1`, `r` = 1.04 (0.8 × 1.3 wins). The reach is never scaled. Which clip
the record's `+0x20` names during a chain (the attack's id or the clip's) is not traced; inferred the playing id.

### PhysicsHumanBody_Moved {#u10-moved}

`0x00342828 (body, matrix)`, vtable `+0x18` / `+0x1c`. Confirmed (code); who calls it (a virtual call) is not traced.

1. The body's `Moved` (`0x00341e40`): the capsule's set-from-matrix slot (shape `+0x30` / `+0x34`,
   `0x00343798`: prev `+0x20` = base, base = the matrix's translation `+0x30`), body AABB from the capsule, re-sort.
   The dirty bit is not touched.
2. `+0x90` = `AABB_FromUprightCylinder(translation + (0, 0, 1.0), height 2.0, radius 0.8)`: x and y ± 0.8, z from
   the translation's z to z + 2.0. Not scaled, unlike the update's.
3. Every bone shape whose flags have `0x1` or `0x4` gets its set-from-matrix slot with the matrix: a **sphere's centre
   becomes the translation** (`0x00342d08`; offset and bone ignored, prev `+0x20` untouched); a **segment** does
   nothing (`0x00343a88`). [Unit 6](#u6-reset) gives the slot per type.

So `Moved` does **not** reset previous positions. Its effect on strikes: at the next pose a sphere copies its centre
(now the origin) to prev and computes the new one, so the next sweep runs from the human's origin up to the hand,
inside the human, not across the distance moved (inferred from the pose rule, [Unit 6](#u6-sphere-pose)).

### PhysBody_SetShapeEnabled {#u10-enable}

`0x003428d0 (body, id, on)`, through `Human_SetStrikeShape` (`0x0021c030`) from the clip events
([Combat](combat.md#moving-strikes)).

1. **Always first:** `StopIgnoring(null)` on the body (vtable `+0x38` / `+0x3c`): the human body clears `+0xd4` and
   frees its whole ignore list. So every strike-on or strike-off event, even one that changes nothing below, empties
   the struck list, and a body already struck can be struck again in the next window.
2. `id < 0`: only when `on` = 0 and `+0xc0` ≠ 0: `+0xc0` = 0 and every bone shape enable(off). (`id < 0` with
   `on` ≠ 0 does nothing more.)
3. `id ≥ 0`: `bit = 1 << ((id − 2) & 31)` (the hardware shift uses the low five bits). On with the bit already set,
   or off with it clear: return. Else set or clear the bit and enable(shape, on) for the **first** shape of the chain
   whose bone byte `+0x31` equals `id`; none: the bit alone changes.

Bits: 3 → `0x2`, 6 → `0x10`, 18 → `0x10000`, 19 → `0x20000`, 24 → `0x400000`, 25 → `0x800000`, 29 → `0x8000000`,
30 → `0x10000000`, 32 → `0x40000000`, 33 → `0x80000000`; the shapeless event bones 0 → `0x40000000`, 4 → `0x4`,
23 → `0x200000`, 28 → `0x4000000`, 31 → `0x20000000`. **Bones 0 and 32 share bit 30**: an event for bone 0 sets it,
so a later on-event for shin 32 finds it set and does not switch the shin on, and an off-event for either clears
both (confirmed (code) by the arithmetic; whether any clip has both in one window is not checked).

### Weapon strike spheres {#u10-weapon}

`PhysicsHumanBody_AddStrikeSphere(body, sphere)` (`0x00342158`) pushes the sphere at the **head** of `+0xc4`
(sphere `+0x00` = old head). `PhysicsHumanBody_ClearStrikeSpheres` (`0x00342168`) pops them one by one: enable(off),
next 0, head = the following one. Who builds them is on [Combat](combat.md#moving-strikes)
(`Human_BuildWeaponStrikeSpheres` `0x00247be8`, the timer `+0xd0` = event word + 1).

### The push sphere {#u10-push-sphere}

`PhysicsHumanBody_SetPushSphere(body, on)` (`0x003420d0`; through `Human_SetPushSphere` `0x0021c058`, which does
nothing when the human has no body): on with none held takes the first push sphere whose `0x8` is clear
(`IPhysics_AllocBigSphere` `0x0033d278`: radius `+0x40` = 2.0, flags `|= 0xb`; null when all 30 are taken); off frees
it (`IPhysics_FreeShape` `0x0033d2c8`: clears `0x8` only) and sets `+0x84` = 0. Of the 94 call sites of
`Human_SetPushSphere`, **only `Grab_Start` (`0x0026c7a8`) and `Tackle_Start` (`0x00270a34`) pass 1**; every other one
passes 0 (byte search for the call with its argument, confirmed (code)). So the sphere is on from a grab's or a
tackle's start until the next action that switches it off (most clip starters do).

### PhysicsHumanBody_PushAwayHumans {#u10-push}

`0x003425c0 (body)`, called at the end of `Human_Move` (`0x0023db28`) right after the body's Update.

1. `target` = `Human_GetTarget(owner)` (`0x00226e60`). No push sphere (`+0x84` = 0): return.
2. `IPhysics_OverlapShape` ([How it fills a contact](physics-kernels.md#overlapshape)) with the query: body, the push
   sphere, box pointer
   0, the owner, options `0x2012` (`0x2` bodies, `0x10` type mask; `0x2000` is never read), type mask `0x40` (humans),
   at most **32** contacts, thresholds 0. Each human meets it through his capsule (SphereCapsule).
3. The contacts are walked **from the last to the first**. For each, `other` = contact `+0x40`. Skip it when it is
   `target`; when its player byte `+0x1b0` is not −1 (any player); when its body (owner vtable `+0xf0`) has flag
   `0x200`.
4. `d` = other's position − this human's position, both from the authoritative transforms (`0x00714b00` + slot
   `+0x92` × 0x20), **in 3-D** (z included); `u = d / |d|` (VU `rsqrt`).
5. `push = u × 0.25 × w(this) / (w(other) × k(other))` with `w` = attribute 1 and `k` = attribute 7
   ([Attributes](physics.md#attributes); 82 and 82 for humans, so `0.25 / k`), added in xyz to the other body's push-out
   `+0x50` (`Vec4_AddTo` `0x004e6918`, `w` = 1). The other's next sweep moves it ([Unit 3](#u3-sweep)).

Two humans at the same point: `|d|` = 0, the PS2's `rsqrt` gives the largest float and `0 × max` = 0, so no push
(inferred from the hardware; an IEEE port must special-case it rather than add NaNs).

### Human_TestStrikes: the gather and the loop {#u10-strikes}

`0x0033f110 (phys a0, body a1)`. [Unit 5](physics-kernels.md#u4b-how-human-teststrikes-builds-the-sweep-0) gives the
per-shape sweep
(prev, cur, the restore, the contact fields, the pose rule); this is the rest, confirmed (code) at the addresses.

0. Nothing unless `+0xd0` ≠ 0 or `+0xc0` ≠ 0. `held` = `Human_GetHeldObject(owner)`; the weapon handler's owner is
   `held`, or the human when none.
1. **Weapon spheres against the level** (only when `held` ≠ 0, `0x0033f218`). For each sphere of `+0xc4` whose type
   has a mesh sweep (IPhysics `+0xe0` + type × 4): a scratch body's box = the shape's bounds grown by its move
   (`AABB_ExtendByMove`), then the mesh sweep with max 1, the human body, the shape, the move, options **`0x800c`**,
   `n.z` limits −0.65 and 0.65 ([Unit 2](#u2-00348a50-physicsmesh-sweepsphere): walls only, triangles with bit 3
   skipped). On a hit: contact `+0x00`, `+0x04`, `+0x40`, `+0x44` = 0, the shape put back to `cur`, the held
   object's handler (vtable `+0xf8` / `+0xfc`) with the contact; the rest of the weapon spheres are skipped.
2. **Strike spheres against the level** (when `+0xc0` ≠ 0, `0x0033f360`): the same for each bone shape with flags
   `0x1` and `0x2` **and type 2** (spheres only), options **`0x804c`** (adds `0x40`: one-sided faces kept). On the
   first hit: the owner fields zeroed as above, the shape restored, then the owner's `Strike_Contact` (vtable
   `+0x100` / `+0x104`, the level branch: a particle at `+0x90`); the rest of this step is skipped.
3. **Gather** (`0x0033f4bc`): `IPhysics_QueryBox(phys, list, &body+0x90, start 0, ignored owner = own owner,
   type mask −1, flag mask 0x18)` ([Queries](physics.md#queries)): bodies whose flags have `0x8` or `0x10`. A body with
   only
   `CHARGETARGET` (`0x20`) is never gathered here. The ray origin `o` = own authoritative position + (0, 0, 1.0).
4. **Per candidate**, in list order:
    1. Skip: null or ownerless; own group non-zero and equal to its group; own `IsIgnoring(its owner)` (vtable
       `+0x30` / `+0x34`: struck already, the grab partner); its type mask has `0x8` (an object) and its owner's state
       flags (owner vtable `+0x50` / `+0x54`) have `0x4000000` (airborne: a flying object is not struck).
    2. **Line of sight.** Target point `t` = its first shape's world position (GetShapes with (0, 0, 0), shape
       `+0x20` / `+0x24`), `+1.0` in z when the candidate is a human (type mask `0x40`). `d = t − o`, `L = |d|`,
       ray `{o, d / L, L}` through `WorldManager_RayCast` (`0x0040db88`) with the material exclusion list at
       `0x005771c8` and options `0x48`, or `0x8` for a human candidate ([Objects](objects.md#glass) gives the
       exclusions). A hit (result `+0x10` ≠ 0): skip the candidate.
    3. **Weapon spheres** (candidate flags `0x8`): for each sphere of `+0xc4` (no flag test), its move as in Unit 5,
       against the candidate's shapes: a human's bone chain filtered by flag `0x4`, otherwise GetShapes(2, 4, own
       owner). On the first nonzero sweep: contact `+0x00` = `held` (or the human), `+0x04` the sphere, `+0x40` its
       owner, `+0x44` its shape; the handler `+0xfc`; for a non-human candidate an answer of 5 means "next candidate
       shape", for a human the answer is not read. A hit with an owner: own `Ignore(its owner)` (vtable `+0x40` /
       `+0x44`), and the weapon spheres stop for this candidate.
    4. **Strike shapes** (`+0xc0` ≠ 0, candidate flags `0x30` (`0x10` or `0x20`), and own owner vtable `+0x108` /
       `+0x10c` with its owner nonzero): for each bone shape with `0x1` and `0x2`, its move, against: a non-human's
       GetShapes(2, 4, own owner) chain; a human's bone shapes with flag `0x4`, or **his capsule alone** when
       `Anim_HitsWithCapsule(owner, Human_GetAnimId(owner))` (`0x00101b88`, clip flag `0x10000`,
       [Combat](combat.md#capsule-strike)). The first nonzero sweep: contact as Unit 5, `Strike_Contact`, then
       `Ignore(its owner)` when it has one; the strike shapes stop for **this candidate**.
    5. Go on with the **next candidate**: a hit does not end the test. Each body can be struck once per update (and
       once per window, by the ignore list), but one swing can strike several bodies in the same update.
5. **Weapon timer** (`0x0033fe50`): if `+0xd0` ≠ 0, decrement it; at 0 `ClearStrikeSpheres`; either way set the
   body's dirty bit.

### Test ideas {#u10-tests}

Synthetic bodies; fake owners with scale, reach, type mask and attributes. Values from this spec (float32).

1. **Switch bits.** `SetShapeEnabled(25, on)`: `+0xc0` = `0x800000`, the right-hand sphere flags gain `0x11`; again:
   nothing changes but the ignore list is emptied. `(0, on)` then `(32, on)`: `+0xc0` = `0x40000000`, shin 32
   stays off. `(−1, off)`: `+0xc0` = 0, every bone shape off.
2. **Struck list emptied.** Ignore(owner X) on a human body, then `SetShapeEnabled(19, off)` with bit 19 clear:
   `IsIgnoring(X)` is false afterwards.
3. **Broad bound.** Capsule base (10, 5, 0), height 2.0, scale 1, strike bit set: reach 1.37 → `+0x90` =
   (8.63, 3.63, 0) to (11.37, 6.37, 2.0); reach 0 → ±0.8; scale 1.3, reach 0.94 → ±1.04. Weapon sphere at
   (10.6, 5.8, 1.2) radius 0.1 with reach 1.37: `r` = 1.1 (the reach is ignored).
4. **No refresh.** Not dirty, `+0xc0` = 0, `+0xd0` = 0: `+0x90` is left as it was; flag `0x4000`: nothing posed.
5. **Moved.** Matrix translation (3, 4, 0.5): `+0x90` = (2.2, 3.2, 0.5) to (3.8, 4.8, 2.5). An enabled hand sphere
   at (10.6, 5.0, 1.4) with prev (10.5, 5.0, 1.4) → centre (3, 4, 0.5), prev unchanged; after the next pose with the
   hand at (3.6, 4.0, 1.9): prev (3, 4, 0.5), move (0.6, 0, 1.4).
6. **Push.** This at (0, 0, 0), other at (1, 1, 0.5), k = 1: other's `+0x50` += (0.166667, 0.166667, 0.083333);
   k = 2: (0.083333, 0.083333, 0.041667); other at (0, 2, 0): (0, 0.25, 0). Not pushed: the target, a player, body
   flag `0x200`; k = 1e9: 2.5e-10 (effectively 0). Same position: no push, no NaN.
7. **Push sphere switch.** On twice takes one sphere; off frees it (`0x8` clear) and `+0x84` = 0; a 31st on with
   all 30 taken leaves `+0x84` = 0.
8. **Ray.** Striker at the origin, human candidate's capsule base (3, 4, 0): ray from (0, 0, 1), direction
   (0.6, 0.8, 0), length 5.0, options 8; an object whose first shape is at (3, 4, 0.5): direction
   (0.597022, 0.796030, −0.099504), length 5.024938, options `0x48`. A wall triangle across the ray: no strike.
9. **Several bodies.** Two boxes both reached by one hand sweep in one update: both get `Strike_Contact`, each once;
   the next update neither (ignored); after an off-event, both can be struck again.
10. **Airborne object.** An object body (type 8) whose owner reports `0x4000000`: never struck.

## Unit 11: Moving shapes against bodies: spheres, capsules, segments {#unit-11}

**What it is.** The sweep table's sphere, capsule and segment pairs (IPhysics `+0x7c`, [The shape-pair
tables](physics.md#dispatch)), their three kernels, the body pass of a body's sweep (`PhysicsBody_SweepHumanMask`, which
puts the
bodies' contacts in the same sorted list as the mesh's), and the general shape query `IPhysics_CollideShape` with every
option bit it reads. All confirmed (code) at the cited addresses, read in the disassembly on 2026-10-12, unless a line
says otherwise; no runtime claims.

- **Coney:** `src/physics/sweeps.*` for the pair functions and kernels; `src/human/human_contacts.h` moves onto
  `PhysicsSweep_CapsuleCapsule` as specified here (four differences from it today: [below](#u11-coney-differs));
  `PhysicsBody_SweepHumanMask` replaces `Human::meetHumans`, so a pass's human and wall contacts are sorted together
  by fraction (today a marked Coney choice).
- **Tests:** [Test ideas](#u11-tests): every number there was computed from this spec (a Python model of it), with
  the original's quirks pinned: start-of-move normals, swapped heights, unsorted capsule roots, the missed vertical
  drop onto a capsule, the inward sphere-capsule normal.

16 functions, 8672 bytes (3 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x003440d0` | `PhysicsSweep_SphereSphere` | 560 |
| `0x00344300` | `PhysicsSweep_SegmentSegment` | 32 |
| `0x00344320` | `PhysicsSweep_SphereSegment` | 536 |
| `0x00344538` | `PhysicsSweep_SegmentSphere` | 224 |
| `0x00345670` | `PhysicsSweep_SphereCapsule` | 668 |
| `0x00345910` | `PhysicsSweep_CapsuleSphere` | 224 |
| `0x00345d18` | `PhysicsSweep_CapsuleSegment` | 32 |
| `0x00345d38` | `PhysicsSweep_SegmentCapsule` | 32 |
| `0x00343cd8` | `PhysicsSweep_CapsuleCapsule` (tagged) | 1012 |
| `0x00349268` | `Sweep_SphereSphere` | 544 |
| `0x003498c0` | `Sweep_CircleCircle2D` | 416 |
| `0x0034a8d0` | `Sweep_IntervalOverlap` | 396 |
| `0x00349a60` | `Sweep_SphereCapsule` (tagged) | 2160 |
| `0x00349180` | `Vec_MakePerpendicularBasis` | 232 |
| `0x0033d498` | `PhysicsBody_SweepHumanMask` (tagged) | 640 |
| `0x0033e7f0` | `IPhysics_CollideShape` | 964 |

### Summary for the implementer {#u11-summary}

- **Calling convention of a sweep pair** (confirmed (code) at `0x0033d68c` and `0x0033ea0c`, the two callers):
  `bool fn(Contact* c, Body* ownBody, Shape* own, Body* otherBody, Shape* other, const vec4* move)` in `a0`-`a3`,
  `t0`, `t1`. `own` is the moving shape, `move` its whole move for this pass (the other shape is at rest). Returns 1
  on a hit. Only `PhysicsSweep_CapsuleCapsule` reads a body (the other body's type mask `+0x44`).
- **What a hit writes** (the caller then writes `+0xb4 = 0` and `Contact_InsertSorted` writes `+0x00`, `+0x04`,
  `+0x40`, `+0x44`; [Unit 3](#u3-insert)). The slot is **not cleared** first: a field not listed holds what the
  50-slot stack array held.

  | Pair | `+0x10` | `+0x50` | `+0x80` | `+0x88` | `+0x90` | `+0xa0` | `+0xb0` |
  | --- | --- | --- | --- | --- | --- | --- | --- |
  | sphere-sphere (2, 2) | unit(other − own), start | unit(own − other), start | first touch | | own centre + f·move | | 0 |
  | capsule-capsule (3, 3) | −`+0x50` | horizontal unit(own − other) at the start; (0, 0, 1) in the start case | max(circle entry, vertical entry) | | own base + f·move | start case only | 0, or 2 in the start case |
  | sphere-capsule (2, 3) | −`+0x50` | unit(capsule mid-height − sphere start): **into** the capsule | first stored root in [0, 1] | | sphere centre + f·move | | 0 |
  | capsule-sphere (3, 2) | halves of (2, 3) with −move swapped | | | | sphere centre − f·move | | 0 |
  | sphere-segment (2, 4) | unit(move) | −unit(move) | closest approach, not first touch | that distance | sphere centre + f·move | | 0 |
  | segment-sphere (4, 2) | halves of (2, 4) with −move swapped | | | | | | 0 |
  | segment-segment, capsule-segment, segment-capsule | nothing written: the static overlaps of [Unit 4](physics-kernels.md#unit-4), move ignored | | **leftover** | | | | **leftover** |

- **Normals are taken at the start of the move**, not at the contact, in sphere-sphere and capsule-capsule; a
  glancing approach gets the start direction. **The sphere-capsule normal points the other way** from every other
  pair (from the mover into the other shape). Resolved with code 1 or 2 ([Unit 3](#u3-codes)), such a normal gives
  no advance (`D · n > 0`) and no slide (`Vec_RemoveNormalPart` acts only on `v · n < 0`), so the pass returns 1
  and the next pass meets the same contact: after three passes the mover is **stuck where it started**, its
  horizontal velocity zeroed. Inferred from the two functions; not seen at runtime. It concerns a world object's
  sphere hitting a human (sphere-capsule) and a walking human meeting a `BLOCKHUMANS` object of sphere shape
  (capsule-sphere).
- **The capsule is three different shapes** in this unit: an upright cylinder `[B.z, B.z + h]` against another
  capsule (with the heights swapped, [below](#u11-capsule-capsule)); a true capsule whose rounded ends lie **inside**
  `[B.z, B.z + h]` against a sphere (hemisphere centres at `B.z + r` and `B.z + h − r`); and Unit 4's true capsule
  reaching `r` past both ends against a segment (the overlap, used as the sweep).
- **Sorting with walls.** In `PhysicsBody_Sweep` each pass's list gets, in this order, the nearest mesh contact,
  the landing contact, then one contact per candidate body ([Unit 3](#u3-mesh), [The body pass](#u11-body-pass)), all
  through `Contact_InsertSorted` (strictly-greater insertion), so humans and walls are resolved nearest first, and
  at an equal fraction the mesh contact comes first. Any non-zero answer ends the walk ([Unit 3](#u3-resolve)), so a
  human met at 0.3 is resolved and the wall at 0.6 is not seen in that pass.
- **The capsule-capsule start case** ("already within reach at fraction 0"), settled: [below](#u11-start-case).

### Shared records and notation {#u11-records}

Shape fields read ([Shapes](physics.md#shapes)): sphere `+0x40` r, `+0x50` C; capsule `+0x40` r, `+0x44` h, `+0x50` B
(`+0x58`
is B.z); segment `+0x40` S, `+0x50` E, `+0x60` r. `a·b` is the xyz dot product summed `(x·x + y·y) + z·z`;
"unit(v)" is `v × rsqrt(v·v)` on VU0 (a zero vector gives a zero result on the EE, inferred: `0 × FLT_MAX`; Coney
should write (0, 0, 0) and say so). 1e-6 is `0x358637bd`, −1e-6 `0xb58637bd`, 1.0 `0x3f800000`, −1.0 `0xbf800000`,
0.5 `0x3f000000`, 0.05 `0x3d4ccccd`. Vectors written as unit vectors keep `w = 1`.

**The hit record of the two root kernels** (a stack record of the caller, count cleared to 0 first): `+0x00` count
(int), `+0x04` the roots `t[i]` (floats, at most two used here), `+0x10 + 16·i` a vector per root, always
(0, 0, 0, 1) from `0x005116c0` (unused). **The root filter** of `PhysicsSweep_SphereSphere` (`0x00344144`) and
`PhysicsSweep_SphereCapsule` (`0x00345730`): walk the roots from the last to the first and remove each `t` with
`t < 0` or `t > 1` (shifting the rest down); no root left is no hit; otherwise the fraction is `t[0]`, the **first
kept in stored order**.

### Sweep_SphereSphere {#u11-sphere-sphere-kernel}

`0x00349268`, 0x220 bytes. `bool Sweep_SphereSphere(Hits* hits, const {vec4 P; vec4 m}* ray, const {vec4 C; float
R}* sphere)`. It finds the `t` with `|(C − P) + t·m| = R`, that is a centre moving from C by `t·m` against a fixed
point P (equivalently a ray from P along −m against a sphere at C). Returns 1 when it appended a root:

1. `d = C − P`, `c = d·d − R²`, `b = m·d`.
2. `c ≤ 0` (P within R of C, touching included): append `t = 0`, return 1.
3. `a = m·m`; if `|a| < 1e-6`: return 0.
4. `disc = (2b)² − 4·c·a` (computed as `(2b)·(2b) − (4c)·a`).
5. `disc > 0`: `s = √disc`; append `(−2b − s)/(2a)` and `(−2b + s)/(2a)`, the smaller first (a swap if needed).
   `disc == 0`: append `−2b/(2a)`. `disc < 0`: return 0.

The roots are not clamped. Also called by `ObjectRender_Draw` ([Geometry kernels](physics.md#kernels)).

### PhysicsSweep_SphereSphere {#u11-sphere-sphere}

`0x003440d0`, 0x230 bytes, table (2, 2). Calls the kernel with `P` = the **other** centre, `m` = move, `C` = the
**own** centre, `R` = r_own + r_other, then the root filter. On a hit (`0x00344214`-`0x003442cc`): `+0x10` =
unit(C_other − C_own), `+0x50` = −`+0x10` = unit(C_own − C_other) (both from the centres at the **start**), `+0x80`
= f, `+0x90` = C_own + f·move (w = 1), `+0xb0` = 0. Consequences: spheres already touching at the start hit at
fraction 0 **whatever the move** (also a move away, also a zero move); a move with `|move|² < 1e-6` (shorter than
1 mm) never hits otherwise.

### Sweep_CircleCircle2D {#u11-circles}

`0x003498c0`, 0x1a0 bytes. `bool (float r1 f12, float r2 f13, float* t0 a0, float* t1 a1, const vec4* c1 a2, const
vec4* c2 a3, const vec4* m t0, int flag t1)`. The caller zeroes the z of `c1`, `c2` and `m`, so this is the circle of
radius r1 at c1 moving by `t·m` against the circle r2 at c2:

1. `d = c1 − c2`, `b = m·d`.
2. If `flag & 1` and `b ≥ 0` (not closing in, a tangent or zero move included): return 0.
3. `c = d·d − (r1 + r2)²`. If `c ≤ 0` (overlapping or touching): `*t0 = 0`, `*t1 = 1.0`, return 1.
4. `a = m·m`, `disc = (2b)² − 4·a·c` (as `(2b)² − a·(4c)`). `disc > 0`: the two roots `(−2b ± √disc)/(2a)`, written
   so that `*t0 ≤ *t1`, return 1. `disc == 0`: both = `−2b/(2a)`, return 1. Else return 0.

With `flag` 0 and a zero move, step 4 divides 0 by 0 (EE: −`FLT_MAX`, inferred), which the caller rejects; Coney may
return 0 there.

### Sweep_IntervalOverlap {#u11-interval}

`0x0034a8d0`, 0x18c bytes. `int (float* lo a0, float* hi a1, float maxT f12, float v f13, float A0 f14, float A1 f15,
float B0 f16, float B1 f17)`: the interval `[B0, B1]` moves by `v·t` against the fixed `[A0, A1]`; `[lo, hi]` comes
in as the allowed times and is narrowed to the times they overlap. Returns **1 when they are separated**, 0 when they
overlap:

| Start | `v` | Effect |
| --- | --- | --- |
| `B1 < A0` (B below) | `v ≤ 0` | return 1 |
| | `v > 0` | `lo = max(lo, (A0 − B1)/v)`; if `maxT < lo` return 1; `hi = min(hi, (A1 − B0)/v)` |
| `A1 < B0` (B above) | `v ≥ 0` | return 1 |
| | `v < 0` | `lo = max(lo, (A1 − B0)/v)`; if `maxT < lo` return 1; `hi = min(hi, (A0 − B1)/v)` |
| overlapping (neither) | `v > 0` | `hi = min(hi, (A1 − B0)/v)` |
| | `v < 0` | `hi = min(hi, (A0 − B1)/v)` |
| | `v == 0` | `lo = 0`, `hi = 1.0`, return 0 |

Then return 1 if `hi < lo`, else 0. Touching counts as overlap (strict `<` tests). The `min`/`max` are written as
"store when strictly smaller/greater". Also used by the box-triangle sweep, where the `v == 0` reset matters
([Unit 13](physics-kernels.md#u13-interval)). In the capsule sweep it does not: the caller always passes `lo = 0`, `hi =
1`, so two
humans moving level (`m.z` exactly 0) with overlapping ranges get `lo = 0` and the fraction is the circles' `t0`.

### PhysicsSweep_CapsuleCapsule {#u11-capsule-capsule}

`0x00343cd8`, 0x3f4 bytes, table (3, 3). Own capsule (B1, r1, h1) moves by `m`; the other (B2, r2, h2) is at rest:

1. **Circles** (`0x00343d20`-`0x00343db4`): `c1`, `c2`, `m2` = B1, B2, m with z = 0. `flag` = 1, except 0 when the
   mover goes down (`m.z ≤ −1e-6`) **and** starts strictly above (`B1.z > B2.z`) (`0x00343d48`-`0x00343d90`).
   `Sweep_CircleCircle2D(r1, r2, &t0, &t1, c1, c2, m2, flag)`; 0 → no hit. Then require `0 ≤ t0 ≤ 1` (else no hit).
   `t1` is never used.
2. **Vertical** (`0x00343df0`-`0x00343e2c`), skipped when the **other body's** type mask (`+0x44`) has `0x200`:
   `lo = 0`, `hi = 1`, `Sweep_IntervalOverlap(&lo, &hi, 1.0, m.z, A0 = B2.z, A1 = B2.z + h1, B0 = B1.z, B1 = B1.z +
   h2)`; 1 → no hit. **The heights are swapped**: the fixed range is the other's base with the mover's height, the
   moving range the mover's base with the other's height. Equal heights (every human of one scale and state) hide it.
3. **Fraction** `f = max(t0, lo)` (`max.s` at `0x0034402c`; `lo` is 0 when step 2 was skipped). The circle times
   `[t0, t1]` and the vertical times `[lo, hi]` are **never intersected**: a contact is reported at `max(t0, lo)` even
   when the circles have separated again before `lo` (or the ranges before `t0`).
4. **Normal source** `d = c1 − c2` (horizontal, at the start), unless the start case below replaced it.
5. **Write** (`0x00343ff0`-`0x003440a0`): `n = unit(d)`; `+0x50 = n`, `+0x10 = −n`, `+0x80 = f`, `+0x90 = B1 + f·m`
   (the mover's base at the contact, w = 1), `+0xb0` = 0 or 2. Return 1.

### The start case {#u11-start-case}

Taken when `t0 < 1e-6` (the circles touch or overlap at the start; the test reads `t0`, not `f`), `m.z ≤ −1e-6` and
`B2.z ≤ B1.z` (`0x00343e34`-`0x00343fec`): a capsule coming down onto another from above, already within reach
across the ground. Then:

- `u` = unit(c1 − c2), and `k = (r1 + r2) − |c1 − c2| + 0.05`; if `|c1 − c2|² < 1e-6`, `u` = unit(m2) and
  `k = r1 + r2 + 0.05`, or, if also `|m2|² < 1e-6`, `u = (0, 1, 0)` (`0x00511730`) with the same `k`.
- `+0xa0 = u × 0.5k` (the slide), contact flags `+0xb0 = 2`, and the normal source becomes (0, 0, 1, 1)
  (`0x00511740`), so `+0x50 = (0, 0, 1)`, `+0x10 = (0, 0, −1)`.

What the resolution then does (inferred from [Unit 3](#u3-tail)): with a non-zero answer, the slide (less its part
against the move's direction) is stored once as the mover's next push-out, so a human dropping onto another's head
stops falling there (the upward normal removes `v.z < 0`) and is pushed off sideways by half of the overlap plus
0.025 m on its next sweep; code 4 also gives the one below a **downward** push-out, `0.5·(D·n)·n`.

The other start positions, settled (all confirmed (code) through the steps above): within reach and closing in
(`flag` 1, `m2·d < 0`) → a hit at fraction 0 (or at `lo`) with the start normal, no slide; within reach and not
closing in (moving away, along the tangent, or no horizontal move) → **no hit**; coincident axes with `flag` 1 → no
hit (`b = 0`). This is what Coney's `sweepCapsules` already does across the ground.

### Vec_MakePerpendicularBasis {#u11-basis}

`0x00349180`, 0xe8 bytes. `void (vec4* U a0, vec4* V a1, const vec4* W a2)`, `W` a unit vector. If `|W.x| ≥ |W.y|`:
`k = 1/√(W.x² + W.z²)`, `U = (−W.z·k, 0, W.x·k)`; else `k = 1/√(W.y² + W.z²)`, `U = (0, W.z·k, −W.y·k)`. `U.w` is
not written. `V = W × U` (VU `opmula`/`opmsub`), the whole quadword written, `V.w` = `U.w`. For `W = (0, 0, 1)`:
`U = (−1, 0, 0)`, `V = (0, −1, 0)`; for `(0, 0, −1)`: `U = (1, 0, 0)`, `V = (0, −1, 0)`.

### Sweep_SphereCapsule {#u11-sphere-capsule-kernel}

`0x00349a60`, 0x870 bytes. `bool (Hits* hits a0, const {vec4 O; vec4 dir}* ray a1, const {vec4 P0; vec4 A; float R}*
cap a2)`: a ray against the capsule of radius R around the segment P0 → P0 + A. Roots are in units of `dir`
(unnormalised), appended in the order below, at most two. Returns `count != 0` (1 after two roots).

1. **Frame** (`0x00349a98`-`0x00349c68`): `L = |A|` (VU `sqrt`), `W = A/L`; `U, V` from
   `Vec_MakePerpendicularBasis(W)`. `D = (U·dir, V·dir, W·dir)`, `len = |D|`, `inv = 1/len`, `D ×= inv` (unit).
   `P = (U·(O − P0), V·(O − P0), W·(O − P0))`.
2. **Parallel** when `|D.z| ≥ 0.999999` (`0x3f7fffef` = 0.99999899, about 0.08° from the axis) **or** `len < 1e-6`
   (`0x00349c1c`-`0x00349c90`). With `g = dir·A` and `q = R² − P.x² − P.y²`:
   - `g < 0` and `q ≥ 0`: append `(P.z + √q)·inv`, then `(L − P.z + √q)·inv`, return 1.
   - `g > 0` and `q ≥ 0`: append `−(P.z + √q)·inv`, then `(L − P.z + √q)·inv`, return 1.
   - otherwise return 0.

   The rising case (`g > 0`) gives the entry at the bottom and the exit at the top. **The falling case is wrong**: its
   second root is the entry time negated (the true entry is `(P.z − L − √q)·inv`), so a sphere moving **exactly**
   down the axis from above the capsule has a negative "entry" (dropped by the filter) and is met only at its exit
   below the bottom, or not at all. Confirmed (code) at `0x00349d38`-`0x00349d5c`; the consequence (an object dropped
   straight down onto a human's head passes into him) is inferred.
3. **Side** (`0x00349e2c`-`0x00349f98`): `a = D.x² + D.y²`, `b = P.x·D.x + P.y·D.y`, `c = P.x² + P.y² − R²`,
   `disc = b² − a·c`. `disc < 0`: return 0 (**the caps are not tried**). `disc > 0`: for `s = (−b − √disc)/a`, then
   `(−b + √disc)/a`, append `s·inv` when `0 ≤ P.z + s·D.z ≤ L`; two appended → return 1. `disc == 0`: `s = −b/a`,
   appended under the same test, and then return 1 at once; if not appended, go on.
4. **Bottom cap** (`0x0034a018`-`0x0034a174`): `b' = b + P.z·D.z`, `c' = c + P.z²` (the sphere at P0),
   `disc = b'² − c'`. `> 0`: for `s = −b' − √disc`, then `−b' + √disc`, append `s·inv` when `P.z + s·D.z ≤ 0`,
   returning 1 once two are stored. `== 0`: `s = −b'`, the same test. `< 0`: nothing.
5. **Top cap** (`0x0034a178`-`0x0034a2a8`): `b'' = b' − D.z·L`, `c'' = c' + L·(L − 2P.z)` (the sphere at P0 + A),
   `disc = b''² − c''`; the same as step 4 with the test `P.z + s·D.z ≥ L`.

Edge cases: **no inside test**: a ray starting inside gets its exit (and a negative entry); the caller's filter
keeps the exit when it is within the move. **Store order is not time order**: an entry through a cap and an exit
through the side are stored exit first, and the caller takes the first kept root. `L = 0` (`h = 2r` in the caller)
divides by zero (EE: `W = 0`, then `len = 0`, the parallel branch with `g = 0`: no hit; inferred).

### PhysicsSweep_SphereCapsule {#u11-sphere-capsule}

`0x00345670`, 0x29c bytes, table (2, 3). Sphere (C, r_s) moving by `m`, capsule (B, r_c, h) at rest. Kernel arguments
(`0x003456ac`-`0x00345720`): `O = C`, `dir = m`, `P0 = B + (0, 0, r_c)`, `A = (0, 0, (h − r_c) − r_c)`,
`R = r_c + r_s`; then the root filter. On a hit (`0x003457fc`-`0x003458dc`): `M = B + (0, 0, 0.5h)`; `+0x50 =
unit(M − C)` (from the sphere's **start** centre to the capsule's mid-height, a 3D vector pointing **into** the
capsule), `+0x10 = −+0x50`, `+0x80` = f, `+0x90 = C + f·m`, `+0xb0` = 0. With `h < 2r_c` the axis points down and
the shape spans `[B.z + h − 2r_c, B.z + 2r_c]` (the formula as written; inferred to be unintended).

### PhysicsSweep_SphereSegment {#u11-sphere-segment}

`0x00344320`, 0x218 bytes, table (2, 4). **Not a first-touch sweep.** `(s, t) = Segment_ClosestPoints(d1 = m, d2 = E −
S, w = C − S)` ([Unit 4](physics-kernels.md#u4-segment-closestpoints-0034ad50)); `v = (C − S) + s·m − t·(E − S)`; hit
when `v·v ≤ (r_s +
r_seg)²` (touching counts). Then `+0x80 = s` (the path parameter of **closest approach**), `+0x88 = |v|` (the distance
there, not a depth), `+0x90 = C + s·m`, `+0x10 = unit(m)`, `+0x50 = −unit(m)`, `+0xb0` = 0. A zero move gives `s = 0`
and a zero normal. Unit 4's kernel quirks (absolute parallel threshold) apply.

### The mirrors and the overlap entries {#u11-mirrors}

- `PhysicsSweep_SegmentSphere` (`0x00344538`, (4, 2)) and `PhysicsSweep_CapsuleSphere` (`0x00345910`, (3, 2)): call
  (2, 4) or (2, 3) as `(c, otherBody, otherShape, ownBody, ownShape, −m)` (x, y, z negated, `w` kept), and on a hit
  swap the quadwords `+0x00..+0x3f` with `+0x40..+0x7f`. `+0x80`, `+0x90` and `+0xb0` are not swapped: `+0x90` is
  the **sphere's** centre moved by `−f·m`. After the swap `+0x50` is still "the other's half"; for (3, 2) it is
  unit(C_sphere − M), again pointing from the mover into the other.
- `PhysicsSweep_SegmentSegment` (`0x00344300`, (4, 4)), `PhysicsSweep_CapsuleSegment` (`0x00345d18`, (3, 4)),
  `PhysicsSweep_SegmentCapsule` (`0x00345d38`, (4, 3)): call the overlap `0x00346500`, `0x00347148`, `0x00346fb8`
  with `(c, own, other)` and return its answer; the move is ignored and **nothing is written**, so a hit's fraction
  `+0x80` and flags `+0xb0` are whatever the slot held. In a strike test (the only caller that moves a segment,
  [Unit 5](physics-kernels.md#u4b-how-human-teststrikes-builds-the-sweep-0)) the first hit ends that body's shapes and
  the fraction is not
  read.
  Coney should write fraction 0 and flags 0 there and say so.

### PhysicsBody_SweepHumanMask: the body pass {#u11-body-pass}

`0x0033d498`, 0x280 bytes. `void (IPhysics* phys, const AABB* box, Body* body, Shape* shape, Contact* contacts,
Contact** sorted, int* count, const vec4* D)`, the same arguments as `IPhysics_SweepBodyVsMesh` ([Unit 3](#u3-mesh));
called by `PhysicsBody_Sweep` when body flag `0x1` is set and by `PhysicsBody_Update` (`0x00341c10`). The name is
another engineer's and is kept; the function is the whole body pass, not only its mask.

1. **Mask** (`0x0033d4d4`-`0x0033d578`): `2` when the body's type mask `+0x44` lacks `0x40`. For a human: `4`;
   `0x44` when the owner's state (slot `+0x50`) has `0x4000000` (airborne) or `0x00226660(owner, 0x800)` is true
   (holding); `| 0x20` when the shape's flags `+0x32` have `0x2`; `| 0x40000` when owner `+0x1a8` (gait) ≥ 4;
   `| 0x80000 << p` when the owner's byte `+0x1b0` (player number, signed) is not −1.
2. **Candidates**: `IPhysics_QueryBox(phys, list, box, startBody = body, ignoredOwner = body+0x34, typeMask = −1,
   flagMask = mask)` ([Queries](physics.md#queries)); list order (x-sorted).
3. **Per candidate**, in list order (`0x0033d5ac`-`0x0033d6e4`): skip when both groups `+0x3c` (u16) are non-zero
   and equal; ask the mover's owner, slot `+0x110` `(owner, candidateOwner)`, skip on 0; the chain is the
   candidate's body slot `+0x28` called with `(2, 4, owner)`. Walk it: a shape is tried only with flag `0x1`
   (enabled) and a non-empty `sweep[shape.type][candidate.type]` entry; the contact slot is `contacts[*count]`.
   On the **first** hit: `+0xb4 = 0`, `Contact_InsertSorted(phys, slot, sorted, count, body+0x34, candidate+0x34,
   shape, candidateShape)`, and go to the **next candidate**: at most one contact per body, the first shape of the
   chain that hits, not the nearest.
4. No check against the 50-slot array (inferred: never reached).

### IPhysics_CollideShape {#u11-collide-shape}

`0x0033e7f0`, 0x3c4 bytes. `int IPhysics_CollideShape(IPhysics* phys, Contact* out, const Query* q, const vec4* move)`:
**four** arguments, the move separate from the query. Returns the number of contacts copied to `out`.

| Query offset | Read when | Meaning |
| --- | --- | --- |
| `+0x00` | always | the querying body (its owner becomes contact `+0x00`; its group and owner filter the candidates) |
| `+0x04` | always | the shape swept (its type picks the sweep and mesh functions) |
| `+0x08` | option `0x400` | a **pointer** to a 0x30-byte box node, copied and used instead of the shape's world bounds |
| `+0x0c` | option `0x2000` | an owner whose bodies are skipped (else none) |
| `+0x10` | always | options |
| `+0x14` | option `0x10` | type mask the candidates must meet (else −1) |
| `+0x18` | always | the most contacts copied out |
| `+0x1c` | option `0x800` | body flags a candidate must have at least one of (else no requirement) |
| `+0x20` | option `0x1000` | body flags that exclude a candidate |
| `+0x24` | option `0x10000` | owner state bits (slot `+0x50`) that exclude a candidate |
| `+0x28`, `+0x2c` | option `0x1` | the mesh sweep's min and max `n.z` (f12, f13) |
| `+0x38` | option `0x40000` | a callback `(ownOwner, candidateOwner)`; 0 excludes |

Steps (confirmed (code) at the addresses):

1. **Box** (`0x0033e82c`-`0x0033e87c`): a local node (`PhysicsNode_Init`); the shape's world bounds
   (`PhysicsBody_ComputeBounds` `0x00341638`), or with `0x400` the node at `*(q+0x08)`; then `AABB_ExtendByMove(box,
   move)`.
2. **Bodies**, with option `0x2` (`0x0033e890`-`0x0033ea60`): candidates from `0x00340120` (the filtered query,
   specified in [Unit 7](#unit-7): start body `q+0x00`, the fields above as its arguments, list order). Per candidate:
   skip the same non-zero group and an owner refusal (slot `+0x110`), **always** (no option bit, unlike
   `IPhysics_OverlapShape`); chain from body slot `+0x28` with `(2, 4, owner)`; **every** enabled shape with a sweep
   entry is tried, and every hit inserted (`+0xb4 = 0`, then `Contact_InsertSorted`), so one body can give several
   contacts. The querying body itself is a candidate unless a filter drops it (inferred from the start-body walk; the
   callers seen set `0x2000` with their own owner or sweep a temporary body).
3. **Mesh**, with option `0x1` (`0x0033ea64`-`0x0033eb10`): `phys+0xe0 + 4·type`; if set, it is called with
   `(contacts + count, max = 50 − count, box, body, shape, move, options = the whole word, f12 = q+0x28,
   f13 = q+0x2c)`, and each contact it returns is inserted with other owner 0 and other shape 0. So the mesh's option
   bits (`0x4`, `0x8`, `0x40`, `0x8000`, `0x20000`, [Unit 2](#u2-00348a50-physicsmesh-sweepsphere)) come from the same
   word, and **body and mesh contacts are sorted together** by fraction; at an equal fraction the body contact, being
   inserted first, comes first.
4. **Copy** (`0x0033eb14`-`0x0033eb7c`): `n = min(count, q+0x18)` contacts from the sorted list to `out` (0xc0
   each); return `n`.

Bits not read here: `0x80`, `0x100`, `0x200` (the overlap query's) do nothing in this function.

**Callers' option words** (confirmed (code) at the call sites; the others are not traced):

| Caller | Options | Other fields |
| --- | --- | --- |
| `Cam_Follow_SphereTest` `0x001305a0`, `CamMug_PlaceShot` `0x00138798`, `CamMug_Update` `0x001397bc` | `0x12812`: bodies, type mask `+0x14` = 8 (objects), required flags `+0x1c` = 4 (`BLOCKHUMANS`), ignored owner, excluded owner state `+0x24` = `0x4000000` (airborne) | max 1 |
| `CamMini_TestShot` `0x001375d4` | `0x52813` (the above, plus mesh and a callback `0x00136040`) | |
| `Human_IsWayClearTo` `0x0021c1d0`, `Human_CapsuleOverlaps` `0x0021c524` | `0x2803`: mesh, bodies, required flags 4, ignored owner (the human) | max 1 |
| `Human_IsClearToTarget` `0x0021c3e0` | `1`: mesh only | max 1 |
| `Human_PushOutInAir` `0x0021aa14`, `WorldObject_UpdateBodyContacts` `0x00394be8` | `0x2004d`: mesh only, sphere-sweep options `0x4`, `0x8`, `0x40`, `0x20000`; `n.z` range 0 to 0.7 | max 2 (the human) |
| `Human_HandleMessage` `0x0024732c` | `0x52`: bodies, type mask `0x100408` | max 2 |

### Where Coney differs today {#u11-coney-differs}

`human::sweepCapsules` (`src/human/human_contacts.h`) against `PhysicsSweep_CapsuleCapsule`, each confirmed (code)
on the original's side: (1) Coney's normal is taken at the contact, the original's at the start; (2) Coney tests the
mover's swept height range against the other's, the original intersects times with the heights swapped and takes
`max(t0, lo)` without checking the circle exit; (3) Coney has no descending start case (normal up, slide, flags 2);
(4) Coney resolves humans before walls, the original sorts both. The start case across the ground already agrees.

### Test ideas {#u11-tests}

Values from the spec (doubles; compare with 1e-5). Capsules are `(B; r = 0.35, h = 1.8)` unless said; spheres
`(C; r)`.

1. **Sphere-sphere kernel.** `P = (2, 0, 0)`, `m = (−4, 0, 0)`, `C = 0`, `R = 1`: roots −0.75, −0.25 (none kept by the
   filter). Same with `m = (4, 0, 0)`: 0.25, 0.75.
2. **Sphere-sphere head-on.** Own `(0; 0.5)`, other `((2, 0, 0); 0.5)`, move `(4, 0, 0)`: f = 0.25, `+0x50 = (−1, 0,
   0)`, `+0x10 = (1, 0, 0)`, `+0x90 = (1, 0, 0)`, `+0xb0` 0.
3. **Glancing, start normal.** Own at `(0, 0.8, 0)`: f = 0.35, `+0x90 = (1.4, 0.8, 0)`, `+0x50` = unit(−2, 0.8, 0) =
   `(−0.92848, 0.37139, 0)` (the contact direction would be `(−0.6, 0.8, 0)`). Own at `(0, 1.2, 0)`: no hit.
4. **Already touching.** Own `(0; 0.5)`, other `((0.5, 0, 0); 0.5)`, move `(−1, 0, 0)` (away): hit, f = 0. Move
   `(0.0005, 0, 0)` from 2 m apart: no hit (`|m|² < 1e-6`).
5. **Circles.** r 0.35 each, `c1 = 0`, `c2 = (1, 0, 0)`, `m = (0.5, 0, 0)`, flag 1: `(0.6, 3.4)`. `c2 = (0.5, 0, 0)`,
   `m = (0.1, 0, 0)`: `(0, 1)`; `m = (−0.1, 0, 0)`: no hit. Flag 0, `c2 = (1, 0, 0)`, `m = (−0.5, 0, 0)`: `(−3.4,
   −0.6)` (returned; the caller rejects).
6. **Intervals.** `A = [1, 2.8]`, `B = [−0.2, 0.8]`, `v = 0.5`: overlap, `lo = 0.4`, `hi = 1`. `A = [0, 1.8]`,
   `B = [2.3, 4.1]`, `v = −1`: `lo = 0.5`. `A = [1, 2.8]`, `B = [−1, 0.5]`, `v = 0`: separated. `A = [2, 3.8]`,
   `B = [−1, 0.8]`, `v = 0.5`: `lo = 2.4 > 1`, separated. Overlapping, `v = 0`: `lo = 0`, `hi = 1`.
7. **Capsules head-on.** `B1 = 0`, `B2 = (1, 0, 0)`, `m = (0.5, 0, 0)`: f = 0.6, `+0x50 = (−1, 0, 0)`, `+0x90 =
   (0.3, 0, 0)`, `+0xb0` 0. `B1 = (0, 0.4, 0)`: f = 0.85109, `+0x50 = (−0.92848, 0.37139, 0)` (start normal).
8. **Capsules within reach.** `B2 = (0.5, 0, 0)`: `m = (0.1, 0, 0)` → f = 0, `+0x50 = (−1, 0, 0)`; `m = (−0.1, 0, 0)`
   → no hit; `m = 0` → no hit.
9. **Vertical miss and the swap.** `B1 = (0, 0, 2)`, `B2 = (1, 0, 0)`, `m = (0.5, 0, 0)`: no hit. `B1 = (0, 0, 1.5)`
   with `h1 = 1.0`, other `h2 = 1.8`: **no hit** (the swap tests `[1.5, 3.3]` against `[0, 1.0]`); with `h1 = 1.8`,
   `h2 = 1.0`: a hit at 0.6, `+0x90 = (0.3, 0, 1.5)`.
10. **Rising.** `B1 = (0, 0, −2.4)`, `m = (0.5, 0, 1.0)`, `B2 = (1, 0, 0)`: f = 0.6 (`lo` = 0.6, `t0` = 0.6).
11. **Times never intersected.** `B1 = (−0.8, 0, −2.4)`, `B2 = 0`, `m = (3, 0, 1)`: circles `[0.0333, 0.5]`, vertical
    `lo = 0.6`: hit at **0.6** with `+0x50 = (−1, 0, 0)` and `+0x90 = (1.0, 0, −1.8)`, where the axes are 1.0 m
    apart.
12. **Landing on a head (start case).** `B1 = (0.3, 0, 1.7)`, `B2 = 0`, `m = (0, 0, −0.2)`: f = 0, `+0x50 = (0, 0,
    1)`, `+0x10 = (0, 0, −1)`, `+0xa0 = (0.225, 0, 0)`, `+0xb0 = 2`, `+0x90 = (0.3, 0, 1.7)`. `B1 = (0, 0, 1.7)`:
    `+0xa0 = (0, 0.375, 0)`; with `m = (0.1, 0, −0.2)`: `(0.375, 0, 0)`. The same with the other's type mask `0x200`:
    identical. `B1 = (0.3, 0, 2.0)`, `m = (0, 0, −0.5)`: f = 0.4 (vertical entry), start case still taken (it reads
    `t0` = 0), `+0x90 = (0.3, 0, 1.8)`, `+0xa0 = (0.225, 0, 0)`.
13. **Descending, outside reach.** `B1 = (1, 0, 2)`, `m = (−0.5, 0, −0.5)`: f = 0.6, `+0x50 = (1, 0, 0)`, flags 0.
14. **Sphere-segment.** Segment `(−1, 0, 0)`-`(1, 0, 0)` r 0.05, sphere `((0, −1, 0); 0.1)`, `m = (0, 2, 0)`: f = 0.5
    (closest approach, not the touch at 0.425), `+0x88 = 0`, `+0x10 = (0, 1, 0)`, `+0x50 = (0, −1, 0)`, `+0x90 =
    0`. Sphere at z 0.1: f = 0.5, `+0x88 = 0.1`. At z 0.2: no hit. Segment-sphere with the roles swapped and
    `m = (0, −2, 0)` moving the segment: the same fraction, halves swapped.
15. **Basis.** `W = (0, 1, 0)`: `U = (0, 0, −1)`, `V = (−1, 0, 0)`. `W = (0.6, 0.8, 0)`: `U = (0, 0, −1)`,
    `V = (−0.8, 0.6, 0)`.
16. **Sphere-capsule** (capsule `B = 0`, r 0.35, h 1.8; sphere r 0.1; `m` along x unless said):
    - side, `C = (−1, 0, 1)`, `m = (1, 0, 0)`: roots `[0.55, 1.45]`, f = 0.55, `+0x50` = unit(1, 0, −0.1) =
      `(0.99504, 0, −0.09950)` (into the capsule), `+0x90 = (−0.45, 0, 1)`;
    - `C = (−1, 0.3, 1)`: f = 0.66459; `C = (−1, 0, 1.6)` (top cap): f = 0.57574; `C = (−1, 0, −0.05)` (bottom cap):
      f = 0.79384; `C = (−1, 0, 2.0)`: no hit;
    - inside, `C = (0, 0, 1)`, `m = (0.2, 0, 0)`: roots `[−2.25, 2.25]`, no hit; `m = (1, 0, 0)`: roots
      `[−0.45, 0.45]`, **hit at the exit** 0.45, `+0x50 = (0, 0, −1)`;
    - exact vertical drop, `C = (0.1, 0, 3)`, `m = (0, 0, −2)`: roots `[1.54437, −0.55563]`, **no hit** (true touch
      at 0.55); `m = (0.01, 0, −2)` (tilt 0.29°, not parallel): f = 0.55628;
    - exact vertical rise, `C = (0.1, 0, −1)`, `m = (0, 0, 2)`: f = 0.45563 (correct);
    - store order, `C = (−0.3, 0, −0.5)`, `m = (1.2, 0, 2.4)`: roots stored `[0.625, 0.17089]` (side exit, then bottom
      entry), **f = 0.625**; with `m = (0.6, 0, 1.2)` the exit is 1.25 and f = 0.34177.
17. **Capsule-sphere** with the capsule moving: the (2, 3) case with `m` negated; `+0x90` is the sphere's centre
    minus `f·m`.
18. **Overlap entries.** Segment-capsule via (4, 3): a hit leaves `+0x80` and `+0xb0` as the slot held (fill the
    slot with a sentinel and check it survives).
19. **Body pass.** A fake world: a human mover (type mask `0x40`) with candidates flagged 4, `0x44`, `0x20`; masks
    4 (walking), `0x44` (airborne), `0x64` (charging and airborne), `0x40004` at gait 4, `0x100004` for player 1;
    a non-human mover → 2. A candidate with two capsules that both hit gives one contact (the first in the chain); the
    same through `IPhysics_CollideShape` gives two.
20. **Humans and walls in one pass.** Through `PhysicsBody_Sweep` with a fake mesh contact at 0.6 and a capsule met at
    0.3, answers 4 for the human: the human's contact is resolved, the wall's is not reached in pass 1. A mesh and a
    body contact both at 0.5: the mesh's first in the walk; through `IPhysics_CollideShape`, the body's first.
21. **CollideShape options.** `max` 1 with two hits returns the nearer; option `0x2000` naming the mover's own owner
    removes its self-contact; `0x800` with `+0x1c = 4` drops a candidate without `BLOCKHUMANS`; `0x10000` with
    `0x4000000` drops an airborne owner's body; option `0x80` makes no difference (same-group bodies are always
    skipped).
