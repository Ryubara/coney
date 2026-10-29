---
kind: survey
---
# How a level's world splits into buildings

Verified against: `SLUS_212.15` (NTSC-U), read in the shared Ghidra project; the NTSC-U disc's 64 levels, counts only.

## Summary

The question, asked for Coney Studio
([Buildings and collision of a disc level](../guides/studio/disc-world.md)): can a level's buildings be
taken out of its streamed world, moved, and given collision where they have none? Nothing on the disc is "a building".
A level's map is two streamed worlds, `<level>s` (solid scenery) and `<level>d` (detail), each cut into box sectors
whose geometry is one atomic per sector ([The streamed world](world.md)), and its collision is a separate triangle mesh
in the level file ([Collision](collision.md)). This page answers four questions about how those fit together, from the
code and from counts over the whole disc, and reports what Coney's own experiment showed when it cut a building out.

In short:

1. **Sectors cut through buildings.** A sector's atomic is a box cut out of a larger model: its geometry is split at
   the box's faces, with copies of the same vertices on both sides of the seam. A third of the building-sized pieces
   lie in two or more sectors. Welded, a world's triangles fall into connected pieces of which many are buildings, but
   a building that touches the pavement is one piece with the street, so connected pieces alone do not find buildings.
2. **Interiors are in the street level's own worlds,** about 200 m below the street, and load with it (the hub's
   clubhouse lies below the hub's street in `level95`); a level that is wholly an interior (Club 45) is a level of
   its own. A level's worlds load only at its entry.
3. **Collision is drawn on its own,** a much simpler mesh that follows floors and walls closely but shares few
   triangles with the visible mesh. A building's collision is found by region (a box), not by connected piece, and
   most roofs have none.
4. **A building can be separated by a box,** cutting both worlds' sector atomics and the collision mesh, as Coney's
   experiment does. What the original ties to a sector (streaming, visibility, textures) has to be given to the piece
   once it leaves its sector; lighting baked into the vertices stays as it was.

## Method

- **Code.** The functions that load, stream, draw and light the worlds and switch collision were read in Ghidra,
  starting from the world manager's level load and the collision mesh's box switch. Most are already described on
  [The streamed world](world.md) and [Collision](collision.md); this page cites them and adds only what the question
  needs.
- **Disc.** Coney's disc test `coney_tests "[world_split]"` (`tests/platform/disc_world_split_test.cpp`) reads every
  level with a `.lev` and an `s` world (64 levels, 128 worlds) through Coney's own world and level readers, and prints
  counts per level and in all. It welds vertices within 1 cm, finds connected pieces per atomic and per world, measures
  each collision triangle's distance to the nearest `s` world triangle, and sums the area of up-facing faces with and
  without collision under them. The counts corroborate the claims below; anyone with the disc can rerun them.
- **Experiment.** Coney's debug tool `--world-edit` ([World edits](../guides/running-viewers.md#world-edits)) draws
  the sectors apart, hides a world, cuts the triangles a box holds out of the sector atomics into pieces of their own
  (with the collision triangles in the box) and moves them, and turns a roof's up-facing faces into collision. It
  changes nothing faithful; its clips were recorded with Coney's `--record`.

A building-sized piece is one at least 3 m tall and 3 m wide. "Below the street" is below z −100.

## Findings

### Sectors and buildings {#sectors}

The world loader places each sector's atomic at its sector's origin ([Atomic plugin `0x3F0`](world.md#atomic-plugin),
[Part file](world.md#part-file); `World_ReadPart`, `0x004110c0`, confirmed (code)), and the atomic holds the geometry
that falls in its sector's box (inferred from the disc check below; whether a vertex ever lies outside the box is not
checked). A sector holds whatever of the level's models falls in its box, not whole models: the geometry is cut at the
box's faces, inferred from the disc check below, which finds the seams.

Disc check: 128 worlds, 4,152 sectors with an atomic, 2,549,564 triangles. Welded within 1 cm, the atomics hold
173,118 connected pieces, of which 26,901 touch a face of their sector's box. Welded across a whole world, 1,659,414
vertex positions remain, and 97,914 of them are used by two or more sectors: the same point copied on both sides of a
seam. Of 126,172 pairs of sector boxes in the same world, 8,563 overlap by more than 5 cm on every axis.

**Pieces and buildings.** Welded across sectors, the worlds hold 152,733 connected pieces, 12,471 of them in two or
more sectors. 7,603 are building-sized, and 2,449 of those (32 %) span two or more sectors; the rest lie within one
sector's box. Each world's largest piece is the street and what is welded to it: those 128 pieces hold 272,665
triangles. So a free-standing building, a shop front or a roof is often a piece of its own, but a building whose walls
meet the pavement at shared vertices is part of the street's piece (inferred from the disc check: a connected piece
is a building only when the artist modelled it apart).

`level99`: the `s` world has 47 sectors with 39,691 triangles, the `d` world 27 with 13,172. Its worlds hold 2,468
pieces (270 in two or more sectors); 96 are building-sized, 47 of those in two or more sectors.

**What an atomic carries that a cut must keep**, each described on [The streamed world](world.md); the prelight's role
is inferred from the vertex format:

| What | Where it lives | Why a cut keeps it | Evidence |
| --- | --- | --- | --- |
| Materials, with a MatFX effect (dual texture or environment map) | the geometry's material list; the pipeline chosen per material by `Atomic_AssignGamePipelines` (`0x00426c78`, [Pipelines](world.md#pipelines)) | a piece's triangles keep their materials and pipelines, or they draw untextured or with the wrong effect | confirmed (code) |
| Two texture-coordinate sets (geometry flag `0x80`) and the scales of positions and coordinates | the PS2 native geometry and the atomic plugin `0x3F0` ([PS2 world geometry](world.md#ps2-world-geometry)) | the second set carries the dual texture's layer | confirmed (code) |
| Prelighting colour per vertex, and normals | the native geometry | the baked light and shadows are in the vertices | inferred |
| Textures | the part file's texture dictionary, destroyed when the part unloads ([Choosing what to stream](world.md#streaming)) | a piece that outlives its part needs its textures kept | confirmed (code) |
| Position | the sector plugin's origin, the atomic's frame ([Sector plugin `0x3F1`](world.md#sector-plugin)) | a piece is placed by its own frame | confirmed (code) |

### Interiors {#interiors}

**A level's worlds are loaded only at its entry.** `World_LoadStream` (`0x00410648`) has one caller,
`WorldManager_LoadLevel` (`0x0040dbb8`), whose callers are `InitLevel` (`0x0015fe90`) and `main` (`0x001446d0`, the
boot's load), confirmed (code). Nothing loads another world while a level runs: an interior that is entered without a
level change is part of the street level's own two worlds.

**Interiors sit below the street in the same worlds.** Rooms are placed about 200 m under the city
([Characters](characters.md), interiors; [The hangout's railings](collision.md#hangout-railings)). Disc check: below z
−100 there are 13,964 collision triangles in 20 levels, and 576 sectors lie wholly below it. So the interiors of those
levels are sectors of the level's `s` and `d` worlds and triangles of its collision mesh, streamed like the rest
(inferred from the counts and the single world load). `level99` has 2 collision triangles below the street, and
`level95`, the hub, 1,057 of its 5,943: the clubhouse below the hub's street
([Collision](collision.md#hangout-railings)). A level that is wholly an interior is a level of its own, entered through
a level change: `level110` (Club 45) has all 465 of its collision triangles below −100 (inferred from the disc check and
the single world load above).

### Collision and the visible mesh {#collision}

The original never builds collision from the visible geometry: the collision mesh is read from the level file, and at
run time triangles are only switched on and off by box (`CollisionMesh_SetEnabledInBox`, `0x00351160`, reached for a
game object's box through `CollisionMesh_SetEnabledInVolume`, `0x0034fba0`; confirmed (code),
[Switching triangles on and off](collision.md#enable)). Nothing in the code moves or adds a collision triangle
(confirmed (code) for those two writers; that there is no other is inferred from the callers read for
[Collision](collision.md)).

Disc check: the 64 levels hold 150,567 collision triangles, against 2,549,564 visible ones. Measured from its centroid
to the nearest `s` world triangle, a collision triangle lies within 2 cm in 62,124 cases (41 %), within 10 cm in a
further 33,049, within 50 cm in 30,090, within 2 m in 19,100, and beyond in 6,204. Of the 94,294 collision vertices,
44,890 (48 %) lie within 2 cm of a visible triangle. So the collision is a mesh of its own, about one triangle for
every seventeen visible ones, laid on the visible surfaces (floors and walls) where it matters and simplified away
from them (inferred from the counts: it shares the visible mesh's surfaces, not its triangles).

**A building's collision is found by region,** not by connected piece: the collision triangles that lie in a box drawn
round the building are its collision, which is the rule the original's own box switch uses. Its triangles do not weld
to the visible piece (inferred from the distances above and from the box switch: the collision shares no vertices
with the visible mesh to follow).

**Most roofs have no collision.** The `s` worlds' up-facing faces cover 3,460,389 m²; 643,539 m² of them have a
collision triangle within 25 cm, and 2,780,476 m² (254,213 triangles) have none and lie 3 m or more above the next
collision below them, or above none. In `level99`, 63,568 of 68,591 m² are such roofless faces and 4,875 m² have
collision (inferred from the disc check: a face 3 m above the nearest collision is one nobody stands on in the
original).

### Separating a building {#separating}

What Coney's experiment did, and what the original ties to a sector:

- **The cut.** The triangles a box holds (by centroid, by all three corners, or whole connected pieces in the box)
  come out of every sector atomic under the box, in both worlds, into atomics of their own that keep the materials,
  texture sets, prelight and normals; the collision triangles wholly in the box are switched off and added back as
  extra triangles that move with the pieces. By centroid suits a building cut out of the street, since the seam at the
  pavement is ragged by any rule; whole pieces suit a free-standing model.
- **Streaming.** The original loads and unloads a part's atomics by the distance from the camera to their sectors'
  boxes (`World_FindPartToLoad`, `0x00411eb0`; `World_FindPartToUnload`, `0x004120a8`; confirmed (code)
  [there](world.md#streaming)), and the part's textures go with it. A piece moved away from its sector is loaded and
  unloaded with a box it no longer stands in, so it needs streaming of its own (or its part kept loaded).
- **Visibility.** A sector is drawn only when the BSP walk finds its box in view and no occluder or the PVS hides it
  (`World_FindVisibleSectors`, `0x00411d10`; `World_VisibilitySectorCallback`, `0x00411b98`; confirmed (code)
  [there](world.md#visibility)). A moved piece needs its own visibility test, or it vanishes when its old box leaves
  the view and shows through an occluder that hid the old box.
- **Lighting.** Each atomic is lit for its bounding sphere when it is drawn (`World_RenderSectorAtomic`, `0x00411990`,
  calling `LightManager_SelectLights`, `0x0017de10`; confirmed (code) [there](world.md#lighting)), so a moved piece
  takes the point lights of its new place; the prelight in its vertices, and the shadows the `d` world's decals and
  the ground's prelight paint round it, stay as they were baked (inferred from the vertex format and seen in the
  experiment's clip).
- **Collision.** Switching by box is the original's own operation; moving or adding triangles is Coney's
  (`CollisionMesh::addExtraTriangles`).
- **The path map** is its own chunk of the level file, read by `PathData_Load` (`0x0024e720`, confirmed (code),
  [Level loading](level-loading.md#path-data)), and knows nothing of the world's geometry, so its paths still run
  where a moved building stood (inferred from that). Whether the AI then steers round the building's new place by its
  collision is open ([The AI and a moved building](#q-ai-moved-building), speculative).

What the experiment showed is in [How this was checked](#checked).

## Open questions

### Why sector boxes overlap {#q-box-overlap}

8,563 of 126,172 same-world sector box pairs overlap by more than 5 cm on every axis. Speculative: the world tool grew
a sector's box to hold geometry it did not cut, or kept a tolerance at the cut planes. How to find out: for the
overlapping pairs, check whether the overlap holds whole triangles that cross the plane, with the disc test.

### Where the collision mesh came from {#q-collision-source}

48 % of the collision's vertices lie on the visible surfaces, and 59 % of its triangles lie more than 2 cm from them.
Speculative: an artist's collision model per building, made beside the visible one in the level's source scene.
How to find out: compare one building's collision with its visible piece face by face, for repeated simplifications
(a wall's collision as one quad where the visible wall has many).

### The AI and a moved building {#q-ai-moved-building}

Speculative: the AI follows the path map and meets a moved building only through its collision, so it walks into
the building's new place unless its collision moved with it. How to find out: cut and move a building on a walked path
with `--world-edit`, with and without its collision, and watch a gang member's route past it;
in Ghidra, check whether the path follower casts against the collision mesh.

## How this was checked {#checked}

The disc counts come from `coney_tests "[world_split]"` on the NTSC-U disc, 2026-10-29 (counts only): the test
prints one line per level and the totals, which ctest keeps for a passing run in `Testing/Temporary/LastTest.log` of
the build folder. The code claims
were read in the shared Ghidra project for [#1123](issue:1123). The experiment's clips and stills, all of Coney, were
attached to the pull request; what they showed:

- **The sectors spread on a grid** (`spread`, `--view-world level99`): all 74 sectors of `level99s` and `level99d`
  drawn apart, each scaled to fit a 5 m cell. Each is a box cut out of the street: walls, roofs and pavement end
  sharply at its faces and go on in the neighbouring sector.
- **`level99d` without `level99s`** (`hide s`, a slow flight along the street): the `d` world is the shop signs,
  awnings, hanging clothes, lamps, fences, bunting and graffiti, and the decals laid on the ground (drains, crossing
  lines, grime and shadow patches), floating over nothing. It carries no building of its own.
- **Collision on a roof** (`roof`, `debugdraw collision`, `level99` checkpoint 4, the flat roof south of the street
  at x 29 to 44, y 13 to 30.5): on the disc the roof has no collision, and a player dropped onto it from 16 m falls
  through it and out of the world. With its 141 up-facing faces (z 0.22 to 10.47) made into collision he lands on
  it at z 10.47, walks to its edge and drops to the street.
- **A building cut out and moved** (`cut` and `move`, `level99` checkpoint 2, a fixed camera on the street): the shop
  with the graffiti shutters, in the box x 50 to 65, y 11 to 32, comes out of 3 sectors as 3 pieces of 359
  triangles, with 52 collision triangles. Lifted 6 m, brought 6 m forward and set down at the kerb, it keeps its baked
  light. Where it stood, the lot and the back walls behind it show; the box also took the whole front of the
  neighbouring hamburger stand, sign and shutter, which lifts and moves with the shop, since a box takes every
  triangle inside it whatever building it belongs to.

The clips were recorded with `coney --record` (`--record-debug-draw` for the roof, to keep the wireframe); the edit
files are in [World edits](../guides/running-viewers.md#world-edits)' form.
