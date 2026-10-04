# Collision

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims: PCSX2 was
not used for this page. The disc-side checks (2026-10-04) read all 64 `.lev` files of the NTSC-U disc's WAD with
throwaway scripts outside the repository and are reported as counts, ranges and invariants only.

## Purpose

The static collision of a level: the triangles that characters, cameras and the scripts' "drop to the ground" test
against. It lives in the level file ([Level loading](level-loading.md#the-level-file)) as six chunks that the chunk
system joins into one **collision mesh**, held by the level object at `+0x04`. This page gives the format and the
queries (ray casts, the sphere push-out walls use, ground height, switching triangles on and off) closely enough to
reimplement them. Moving objects' collision (`Physics/`) is not on this page.

Everything here is in **game axes, z up**: the mesh is not converted to RenderWare's axes (confirmed by the disc: see
[Disc counts](#disc-counts)).

## Original structure

The file is `c:/Warriors/Source/RayCast/CollisionMesh.cpp` (path string `0x005788f8`, anchor `0x00350688` on the
[Source map](source-map.md#raycast)). The mesh functions below sit around the anchor, from `0x00350538` to the end of
`0x003519f8` at `0x00351da0`, where `Scene/` begins (inferred: one file; the source map still attributes only the
anchor). The ground-height helpers and the material names (`0x0034f740`-`0x0034fc08`) lie just before, in the
unattributed block between `Physics/` and `RayCast/`. Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00350580` | `CollisionMesh_OnLoaded` | chunk `0x03` handler: builds the mesh | confirmed (code) |
| `0x00350688` | `CollisionMesh_FreeChunks` | frees the chunks of a mesh | confirmed (code) |
| `0x00350538` | `CollisionTri_PassesMaterialFilter` | material exclusion list | confirmed (code) |
| `0x00350778` | `CollisionMesh_RayTestCell` | ray against one cell's triangles | confirmed (code) |
| `0x00350cd8` | `CollisionMesh_RayCast` | ray against the mesh: the grid walk | confirmed (code) |
| `0x00350aa0` / `0x00351160` | `CollisionMesh_SetEnabledInBox(Cell)` | switch triangles inside a box on or off | confirmed (code) |
| `0x00351468` / `0x003519f8` | `CollisionMesh_SphereTestCell` / `CollisionMesh_SpherePush` | push a sphere out of walls | confirmed (code) |
| `0x00351158` | `CollisionMesh_UpdateEmpty` | called by mode 1's `Update` every frame; an empty function | confirmed (code) |
| `0x00337920` / `0x00337a60` | `RayTriangle_OneSided` / `RayTriangle_TwoSided` | ray-triangle intersection (`Utils` maths, outside the file) | confirmed (code) |
| `0x0040db88` | `WorldManager_RayCast` | the world manager's ray cast into the level's mesh; 46 callers | confirmed (code) |
| `0x0034f950` | `Collision_DropToGround` | ground height under a point | confirmed (code) |
| `0x0034fa28` | `Collision_DropToMarkedGround` | the same, skipping triangles whose area byte is 0 | confirmed (code) |
| `0x0034f740` | `Collision_MarchRay` | a long ray cast in steps | confirmed (code) |
| `0x0034fba0` | (none) | switches the triangles inside a game object's box on or off | confirmed (code) |
| `0x0034fc08` | (none) | material id to name (`MATERIAL_*`) | confirmed (code) |

## Data

### The chunks {#chunks}

The level file holds, in this order, `0x52`, `0x07`, `0x04`, `0x05`, `0x06` and `0x03`, all raw except the last
([Chunk system](chunk-system.md#chunk-type-table)). The `0x03` handler `0x00350580` writes the mesh's vtable
(`0x005448b0`) into the 160-byte `0x03` chunk itself, then pops the other five and stores their data pointers, then
pushes the mesh as an object (taken by the level's `0x17` handler). Confirmed (code):

| Chunk | Name in the type table | Content | Stored at |
| --- | --- | --- | --- |
| `0x03` | Collision Mesh | the header, below | the mesh itself |
| `0x06` | Collision Strings | **triangle index lists**, not names: `u16` values | `+0x80` |
| `0x05` | Collision Grid | one `u32` per cell | `+0x78` |
| `0x04` | Collision Triangles | 10-byte triangles | `+0x88` |
| `0x07` | Collision Vertex Buffer | `float[4]` vertices, `w` = 1.0 | `+0x8c` |
| `0x52` | Collision Checked | one bit per triangle, scratch for queries | `+0x94` |

After linking, the handler sets bit 0 (enabled) in every triangle's flags and clears the checked bit set. The bit
set's size is `(((n + 7) >> 3) + 15) & 0x7ff0` bytes for `n` triangles (confirmed (code); the disc's `0x52` chunks have
exactly that size).

### The header (chunk `0x03`, 160 bytes) {#header}

Confirmed (code) for the offsets the queries read; the disc column is the survey of all 64 files.

| Offset | Type | Meaning | On the disc |
| --- | --- | --- | --- |
| `+0x00` | | vtable, written at load | junk pointers |
| `+0x10` | `float[4][4]` | world-to-grid matrix, rows applied as `g = x·r0 + y·r1 + z·r2 + r3` | always a diagonal scale and a translation |
| `+0x50` | `float[4]` | grid clamp minimum | always `(0.5, 0.5, 0.5, 1)` |
| `+0x60` | `float[4]` | grid clamp maximum | always `(nx - 0.5, ny - 0.5, nz - 0.5, 1)` |
| `+0x70` / `+0x72` / `+0x74` | `u16` | `nx`, `ny`, `nz`: cells per axis | `nx` 2-251, `ny` 1-124, `nz` 1-2 |
| `+0x76` | `u16` | padding | 0, except `level118` |
| `+0x78` | pointer | the grid | |
| `+0x7c` | `u32` | number of `u16` values in the index lists | matches the `0x06` chunk |
| `+0x80` | pointer | the index lists | |
| `+0x84` | `u16` | triangle count | 13-9,051 |
| `+0x88` | pointer | the triangles | |
| `+0x8c` | pointer | the vertices | |
| `+0x90` | `u32` | vertex count | 10-5,401 |
| `+0x94` | pointer | the checked bit set | |
| `+0x98` | float | the lowest vertex `z` (inferred: no reader traced) | equal on all 64 files |
| `+0x9c` | `u32` | 0 | always 0 |

### The grid {#grid}

A uniform grid over the mesh's bounding box. The cell of cell coordinates `(x, y, z)` is entry `(z·ny + y)·nx + x` of
the grid. An entry is an offset, in `u16` units, into the index lists; 0 means the cell is empty (the lists' first
`u16` is 0 on every file, so offset 0 also reads as an empty list). At an offset there is a `u16` count followed by
that many `u16` triangle indices. Confirmed (code) at `0x00350cd8` and `0x003519f8`.

**Point to cell**, confirmed (code): `g = M·p` with the header matrix, clamped per axis to `[min, max]` from
`+0x50`/`+0x60`, then truncated. On the disc the matrix maps the vertices' bounding box exactly onto `0` to `n - 1` on
each axis (so cells are centred on integer grid coordinates, and a point on the box's low face lands in cell 0 through
the clamp at 0.5).

**Disc check (corroboration):** cells are 5.03 to 13.57 units wide. 45 levels have `nz` = 1 with a `z` scale of 0
(every point maps to `z` = 0, clamped to 0.5). 2.3 % to 100 % of cells are non-empty; the longest list holds 11 to
215 triangles; a triangle is listed in 1.07 to 7.24 cells on average. Every triangle is in at least one cell. The
lists are not exactly the cells of each triangle's bounding box: 350 entries fall outside it and 18,650 triangles
miss some cells of it (inferred: the tool tested triangle against cell, with a margin).

### Triangles (10 bytes) {#triangles}

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| `+0x00` | `u16[3]` | vertex indices `v0`, `v1`, `v2` | confirmed (code) |
| `+0x06` | `u16` | flags, below | confirmed (code) |
| `+0x08` | `u8` | material id (`MATERIAL_*`, below) | confirmed (code) |
| `+0x09` | `u8` | the **area byte**: 0 to 124; copied into hit results; `Collision_DropToMarkedGround` passes through triangles where it is 0 | confirmed (code) for the uses; meaning speculative (an area or zone number) |

The face normal is `(v1 - v0) × (v2 - v0)`; one-sided tests only hit the side it points to.

Flags, confirmed (code) at `0x00350778` and `0x00351468` unless stated:

| Bits | Meaning |
| --- | --- |
| 0 (`0x0001`) | **enabled**; set on every triangle at load, switched by `CollisionMesh_SetEnabledInBox` |
| 1 (`0x0002`) | **two-sided** |
| 2-10 (`0x07fc`) | type bits: a query skips a triangle when `flags & 0xfff & mask & ~0x801` is non-zero; their meanings are not traced |
| 11 (`0x0800`) | testable while disabled, if the query's mask has `0x800` |
| 12-15 | a value 1 to 15; never read by the queries on this page (meaning unknown) |

**Disc check (corroboration), triangles with each bit:** bit 0 all 150,567; bit 1 4; bit 2 2,717; bit 3 4,231; bit 4
1,052; bit 5 17,421; bit 6 0; bit 7 16,316; bit 8 20,231; bit 9 4,786; bit 10 3,989; bit 11 4.

### Materials {#materials}

`0x0034fc08` maps a material id to its name through a jump table at `0x005785d0` with 191 entries (confirmed (code)):
0 `undefined`, 1 `MATERIAL_NONE`, 2 `GLASS`, 3 `STEEL`, 4 `ALUMINIUM`, 5 `CONCRETE`, 6 `ASHPHALT` (sic), 7 `LAMPOST`,
..., 35 `GRASS`, 36 `DIRT`, ..., 186 `SAND`, 187 `STOREDOOR_GLASS`, 188 `PUNCHING_BAG`, 189 `FOAMHAND`, 190 `SALAMI`
(names without their `MATERIAL_` prefix after the first). The material decides sounds and effects elsewhere (inferred
from the names). **Disc check:** 31 ids occur in the level meshes: 0-7, 12, 13, 15, 16, 18, 28, 30, 31, 35, 36, 41,
47, 95, 105-107, 114-116, 118, 122, 152, 153; the most common is 116 (34,199 triangles).

## Behaviour

### Ray cast {#ray-cast}

A ray is `origin` (`+0x00`, `float[4]`), a **unit** `direction` (`+0x10`) and a `length` (`+0x20`). A result
(confirmed (code) for the fields the casts write and the callers read):

| Offset | Meaning |
| --- | --- |
| `+0x00` | unit face normal; for a two-sided triangle, flipped to face the ray |
| `+0x10` | `u32` hit: 1 when something was hit |
| `+0x14` | `t`, the distance along the ray |
| `+0x18` | 0 |
| `+0x28` | material id (the world wrapper sets 1, `MATERIAL_NONE`, before the cast) |
| `+0x2c` | `u16` the triangle's flags `& 0xfff` |
| `+0x2e` | `u8` the triangle's area byte |
| `+0x30` | pointer to the triangle |

`CollisionMesh_RayCast(mesh, ray, result, excludeMaterials, mask)` (`0x00350cd8`), confirmed (code):

1. `end = origin + direction × length`. Both ends go to grid space (`M·p`); per axis, the smaller and the larger,
   clamped to the clamp box and truncated, give the cell box `[xmin, xmax] × [ymin, ymax] × [zmin, zmax]`.
2. Clear the checked bit set.
3. If `xmin == xmax` or `ymin == ymax`: test every cell of the box.
4. Otherwise walk the `x` columns: for each `ix` from `xmin` to `xmax`, evaluate the ray's 2D line in grid space
   (`y = slope·x + c`, from the *unclamped* grid-space ends) at `ix` and at `ix + 1`; each value is `max(·, 0)` and
   truncated; order the two, clamp them to `[ymin, ymax]`, and test every cell from the lower to the upper `y`, for
   every `z` from `zmin` to `zmax`.

    Note that the walk uses column edges at integer grid coordinates while the point-to-cell rule centres cells on
    integers, so the walk covers its columns half a cell off; a reimplementation that wants the same hits keeps the
    same arithmetic.

`CollisionMesh_RayTestCell(mesh, list, ray, result, excludeMaterials, mask)` (`0x00350778`), for each triangle of the
cell's list, confirmed (code):

1. Skip it if its checked bit is set; set it (so a triangle listed in several cells is tested once per cast).
2. Skip it if `flags & 0xfff & mask & ~0x801` is non-zero.
3. Skip it unless it is enabled: flag bit 0, or mask bit 0, or (mask bit 11 and flag `0x800`).
4. Skip it if its material is in `excludeMaterials` (`0x00350538`: a `u32` list ended by the value 1; a null list
   excludes nothing).
5. Intersect: `RayTriangle_TwoSided` (`0x00337a60`) when flag bit 1 is set, otherwise `RayTriangle_OneSided`
   (`0x00337920`). Both are Möller-Trumbore tests that return `t` or -1. The one-sided test rejects a determinant
   below `1e-6` (rays hitting the back are ignored); the two-sided one rejects `|det| < 1e-6`.
6. Accept `0 <= t <= length`, and keep it if it is nearer than the hit so far: fill the result.

So a cast returns the **nearest** accepted hit along the whole ray, not the first cell's.

`WorldManager_RayCast(worldManager, ray, result, exclude, mask)` (`0x0040db88`) sets result `+0x28` = 1 and the hit
to 0, then casts into the level object's mesh (`*(worldManager + 0x40) + 0x04`). Everything outside `RayCast/` uses
this one (46 callers); the cameras (`Camera/`, `0x00121720`-`0x0013c710`) also call `CollisionMesh_RayCast` directly
(19 callers).

### Ground height {#ground-height}

Both take a position and replace it; the ray points down, `(0, 0, -1)` (`0x00511770`), from the position, with the
length given. Confirmed (code):

- `Collision_DropToGround(length, &pos)` (`0x0034f950`): on a hit, `pos.z -= t - 0.1`: the point ends **0.1 above**
  the ground. Returns whether it hit.
- `Collision_DropToMarkedGround(length, &pos)` (`0x0034fa28`): casts down; while the hit triangle's area byte is 0,
  moves the origin down by `t + 0.01`, shortens the length by the same and casts again; on a triangle with a non-zero
  area byte, sets `pos` to **0.25 above** that hit. Returns 0 when a cast misses.
- `Collision_MarchRay(step, max, &out, &origin, &dir)` (`0x0034f740`): casts the segment `dir × step` from `origin`,
  then from its end, and so on until `max` is covered; `out` = the first hit point.

### Sphere push-out {#sphere-push}

`CollisionMesh_SpherePush(radius, mesh, &centre, exclude, &firstNormal)` (`0x003519f8`) moves a sphere out of the
**walls** it overlaps and returns whether it touched any. Called by the cameras and `0x00143590`. Confirmed (code):

1. Cell box of `centre ± radius` (per axis, as for points); visit its non-empty cells (`0x00351468` for each) with the
   checked bit set cleared first, so each triangle counts once.
2. For each triangle: enabled (bit 0), not excluded by material, and a **wall**: unit normal `n` with
   `|n.z| <= cos 15°` (`cosf` at `0x004b8a70`). Floors and ceilings never push.
3. `dist = n·centre - n·v0`. A one-sided triangle with `dist < -1e-5` is skipped (the sphere is behind it); a
   two-sided one flips `n` and `dist` instead.
4. If `radius - dist > 1e-5` and the centre's projection onto the plane lies inside all three edges (for each edge,
   `(edge × n)` against the point): add `n × (radius - dist)` to a push sum, count it, and set `firstNormal` to `n` if
   it is still zero.
5. Afterwards, if anything was counted: `centre += push / count` (the **average**, not the sum).

Only the triangle's face counts: a sphere touching an edge or corner and not the face is not pushed.

### Switching triangles on and off {#enable}

`CollisionMesh_SetEnabledInBox(mesh, min, max, on)` (`0x00351160`) visits the cells of the box and, for every
triangle whose **three vertices** lie inside it, sets (`on`) or clears flag bit 0 (`0x00350aa0`). Its only caller,
`0x0034fba0(objectId, on)`, uses a game object's box (object `+0x10`/`+0x20`); inferred to be how doors and
breakable barriers open a gap in the static collision (five level scripts call `ChangeCollision`; the binding was not
traced to it).

### Lifetime {#lifetime}

The mesh lives in the `World Level Pool` with the rest of the level file and goes with it. The level object's
destructor (`0x0040cf80`) frees the mesh's chunks (`0x004f4b78`). Nothing updates it per frame:
`CollisionMesh_UpdateEmpty` (`0x00351158`), which mode 1 calls every frame, does nothing.

## Disc counts {#disc-counts}

All 64 `.lev` files (NTSC-U, 2026-10-04): 150,567 triangles and 94,294 vertices in all. Facing, from each triangle's
normal in game axes: 39,531 face up (`n.z > cos 15°`), 5,060 down and 105,976 are walls. Ground mostly faces up, so
the mesh is in game axes with z up. Every vertex has `w` = 1.0; no index is out of range; the `0x52` and `0x07` sizes
match the header's counts; `+0x98` equals the lowest vertex `z` everywhere. `level118`'s header is odd: a `y` scale of
0 and a non-zero `u16` at `+0x76` (its grid still has `nx·ny·nz` entries).

## Coney's implementation

`src/raycast/collision_mesh.h` (2026-10-04), platform-neutral, in game axes:

- `onCollisionMeshLoaded` is the `0x03` handler: it pops the header and the five other chunks in reverse file order and
  pushes a `CollisionMesh` object. `CollisionMesh::build` checks what the queries follow before anything is used: the
  header's counts against the chunk sizes, the clamp box against the grid, every grid offset and list against the
  index lists, every listed triangle and every triangle's vertices against their counts. It sets every triangle's
  enabled bit, as the original's handler does.
- `rayCast` is `CollisionMesh_RayCast` with `RayTestCell`: the same cell box, the same column walk (half a cell off,
  as above), the per-cast checked set, the mask and enabled rules, the material exclusion (a span of ids in place of
  the list ended by 1) and the nearest hit. `spherePush`, `setEnabledInBox`, `dropToGround`, `dropToMarkedGround`
  and `marchRay` follow the sections above.
- **Coney's choices:** the ground helpers cast with mask 0 and no exclusions (what the original passes to its world
  cast is not on this page); `marchRay`'s last step is shortened to end at its maximum; the sphere push tests the
  centre's projection against the edges with the face's own normal, whichever side the sphere is on (the page gives
  the test as "edge × n against the point" without its sign).
- Unit tests on synthetic meshes cover a floor, a one- and a two-sided triangle, a wall from either side, the type
  bits, the mask, disabled triangles, material exclusion, a triangle listed in many cells, a long diagonal ray across
  the grid, marked ground and refused data.

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[disc][collision]"` loads all 64 `.lev` files
through the chunk system with the `0x03` handler: none fails; 150,567 triangles and 94,294 vertices, 39,531 facing up,
5,060 down and 105,976 walls, as above. A point 1 above the centre of every up-facing triangle drops onto ground in all
39,531 cases (39,202 onto that triangle; the rest onto something in between). Two slanted rays at each of those
centres, a steep one 22 long and a shallow one about 102 long, give the same nearest hit through the grid walk as a
brute-force cast over every triangle in all 79,062 casts: on the disc's data the walk's half-cell offset loses no hit
on these rays.

## Open questions

- **Flag bits 2-10 and 12-15**, and the masks the callers pass: which bits mean what (stairs, no-camera, water?). The
  characters' ground snap passes bits 4 and 5 of the triangle under the feet on: bit 4 to a per-player "under cover"
  state (`0x0028ef00`), bit 5 to `0x002195e0` (inferred from the callees, [Characters](characters.md#ground)). The
  follow camera's rays use mask `0x200` ([Camera](camera.md#collision)).
- **The area byte** (`+0x09`): what the 125 values number.
- **`level118`'s header**: is that level's grid usable as it is (its `y` scale is 0)?
- **The cameras' and characters' use** (partly answered): characters stand on the ground by a ray 1.0 m above the
  feet, 1.5 m down, each update, and land on triangles with `n.z` > 0.65; walls for moving bodies are the physics
  code's own push-out from triangles with `|n.z|` ≤ 0.65 ([Characters](characters.md#ground)). The masks and
  exclusion lists of the other `WorldManager_RayCast` callers are not listed.
- **`ChangeCollision`**: whether the Lua binding reaches `0x0034fba0`.
