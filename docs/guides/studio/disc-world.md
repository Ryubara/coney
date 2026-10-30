# Coney Studio: buildings and collision of a disc level

How Coney Studio takes a building out of a disc level's streamed world, moves or removes it, and how a user adds
collision where the disc has none: volumes drawn independent of the render mesh, collision made from selected faces, and
colliders on a separated building, with the overlay that shows the result. It ends with the build issues it proposes, in
order. The question came from [#1123](issue:1123), part of [#1054](issue:1054); what the original does is on [How a
level's world splits into buildings](../../research/world-split.md).

**Status: design.** Nothing on this page is built but the experiment behind the debug tools that it follows ([World
edits](../running-viewers.md#world-edits)). It extends [Content and formats](content-and-formats.md), whose terms (a
`base` level, overrides, the Level view) it uses, and whose override table lists the statements it adds.

## Buildings {#buildings}

Nothing on the disc is a building: a level's map is two streamed worlds cut into box sectors, one atomic of geometry per
sector, and its collision a mesh of its own. Coney's experiment ([How a level's world splits into
buildings](../../research/world-split.md)) took buildings out of `level99` and showed what Studio can rely on:

- **A box finds a building; a connected piece often does not.** Sectors cut through buildings (a third of the
  building-sized pieces span two or more sectors), and a building whose walls meet the pavement is one piece with the
  street. A box cut through every sector under it, in both worlds, takes the building whole; its edges are as ragged as
  the sectors' own seams, and it takes whatever else lies inside it (it took a neighbouring stand's whole front).
- **Its collision is found by the same box,** never from its visible mesh: the collision is a simpler mesh drawn on its
  own, and the original's own operation on it is switching the triangles in a box on or off.
- **Most roofs have no collision** (80 % of the up-facing area), so standing on one needs collision made for it; the
  roof's visible up-facing faces make a good one.
- **What follows the piece and what stays:** its materials, both texture sets and its prelight go with it, and the light
  manager lights it for its new place; the baked light round it (the ground's prelight, the `d` world's shadow decals)
  stays where it was baked, and the path map knows nothing of it.

So Studio separates a building **by a box, never by saving its triangles**: a project holds the box and the rule, and
Coney cuts the player's own disc at level entry, so no geometry of the disc enters a project
([Principles](content-and-formats.md#principles)). In the Level view the user draws the box over the world, sees the
pieces each rule would take (by centroid, the default for a building in a street; by whole connected pieces, for a
free-standing model) with the collision triangles it holds, and then moves, rotates or removes the result like an
entity:

- **A moved piece** is drawn as an object of its own, by its own bounds, not by its old sector's visibility, which would
  hide it whenever the old box left the view; its textures are held for as long as the piece lives, since its part's
  dictionary goes when the part unloads. Its collision triangles move with it.
- **A removed piece** is not drawn and its collision is switched off, as a door's is.
- **The path map** is not changed by a cut: Studio marks the walkable areas under the old and the new box and offers
  their edit ([Paths](content-and-formats.md#paths-kind)), so the AI does not walk through where the building now
  stands.
- **Its collision** is the disc's triangles in the box, moved or switched off with it, plus whatever the user adds
  ([Collision on a disc level](#disc-collision)).

Coney's debug tool `--world-edit` already cuts by box, moves a piece with its collision and adds collision, as an
experiment behind the debug tools ([World edits](../running-viewers.md#world-edits)); it removes nothing, marks no
walkable areas and draws its pieces every frame rather than by their own bounds. The engine work is to do all of the
above as overrides a level file holds and level entry applies ([the build issues](#build-issues)).

## Collision {#disc-collision}

Most of a disc level's roofs have no collision, and a user who opens one up for the player has to add it. Editors offer
three ways, and Studio offers the same three, in this order:

1. **Collision volumes**, drawn independent of the render mesh: a box, a ramp, stairs or a convex hull placed in the
   Level view where the player should stand or be stopped, as Unreal's blocking volumes and Source's clip brushes are.
   They are the project's own content, so the level file holds them as shapes (`collider` statements, with material,
   flags and area by name, [Colliders and primitives](content-and-formats.md#collision)), and at entry they become
   triangles of the level's one collision mesh beside the disc's. An **erase volume** does the opposite: the disc's
   collision triangles wholly inside its box are switched off, the original's own operation on the mesh (`collide box=
   remove`). A roof is one box a few centimetres thick on its top.
2. **Collision from selected faces**, the `roof` edit made a tool: the user selects visible faces of the world (by a
   box, by clicking a face and growing over its coplanar neighbours, filtered by a minimum slope for floors) and makes
   them collision of a chosen material. Studio then **simplifies** the result: coplanar neighbours are merged into
   polygons and triangulated again, since render triangles are far denser than collision (`level99`'s `s` world has
   39,691 triangles where its collision mesh has 1,718). Its limits: a box cuts across faces, so the edge is as ragged
   as the box rule ([Buildings](#buildings)); render faces leave gaps, overlap at sector seams and carry decal layers;
   and a visible wall's winding says nothing of which side is solid, so walls get two-sided triangles or a volume
   instead. The faces come from the disc, so the level file holds the selection as a rule (`collide box= [up=]
   material=`, a face list by sector and index for clicked faces), never the triangles, and Coney makes them at entry.
   **Fit a volume** turns a selection into a box or convex hull of option 1, which is then the project's own.
3. **Colliders on a separated building**: once a `cut` makes a building an object, it carries its collision as a
   component, as a Unity or Godot object carries collider components: the disc's triangles from its box, and any volume
   of option 1 placed as its child in the entity tree, so moving the building moves them all.

**The collision overlay** shows the result of all three: the Level view's collision layer draws the level's mesh as a
wireframe coloured by material and flag (climbable, sneak zone, two-sided), the disc's and the added alike, with the
added triangles marked; in the Game view the Debug draw page's collision wireframe does the same while playing ([Debug
draw](../debug-menu.md#debug-draw)).

**Why volumes come first.** They need the least: the collider builder that project levels need anyway (build issue 2 of
[Content and formats](content-and-formats.md#build-issues)) and the overlay; they are the project's own content, so
nothing derived from the disc is saved; they make few triangles, which the mesh's per-cell lists and the AI's queries
prefer; and one box covers the common case, a roof or a ledge. Faces come second: they need entry-time reading of the
parts under a box, the same machinery as world cuts, plus the simplifier. Colliders on a building come last, as they
need world cuts and the entity tree's children.

## Open questions {#open-questions}

### A moved building's streaming {#q-streaming}

Whether a cut piece should be read at level entry and kept for the level's whole life (simple; its triangles and
textures stay in memory) or stream with a box of its own as the original's sectors do; the memory a level's cuts take
decides it ([How a level's world splits into buildings](../../research/world-split.md#separating)).

## Proposed build issues {#build-issues}

In this order, after the collider builder of [Content and formats](content-and-formats.md#build-issues) issue 2 and the
base levels of its issue 8:

1. **gameplay: Add collision volumes and erase boxes to base levels.** `collider` statements (box, ramp, stairs, convex
   hull) in a level file with a `base`, built at entry into the disc level's collision mesh with their material, flags
   and area, and `collide box= remove` switching the disc's triangles wholly inside off, on the collider builder of
   issue 2 of [Content and formats](content-and-formats.md#build-issues). Tests: a synthetic level's box volume on a
   roof stops a player dropped onto it at its top; an erase box lets him fall through a floor; with neither, the mesh
   equals the disc's.
2. **tools: Add Coney Studio's collision overlay and volume tool.** The Level view's collision layer (the mesh as a
   wireframe coloured by material and flag, added triangles marked) and the drawing of volumes and erase boxes with the
   gizmo, written as the statements of issue 1. Tests: a synthetic level shows its triangles by material; a drawn box
   writes one `collider` statement; the disc file is unchanged.
3. **gameplay: Add world cuts to base levels.** The `cut`, `edit cut:NAME move` and `edit cut:NAME remove` statements of
   [Buildings](#buildings): at level entry every part under a cut's box is read, the box's triangles (by centroid or
   whole pieces) leave both worlds' sector atomics for one object with their materials, texture sets, prelight and
   textures, drawn by its own bounds after its world and lit for its sphere, and the collision triangles in the box move
   or switch off with it; the debug tool's `cut` becomes this path. Tests: a synthetic world's building cut by a box
   leaves the sectors without its triangles and the object with all of them; moved, its collision stops a ray where it
   now stands and not where it stood; removed, it is not drawn and the player walks through; a disc check prints the
   counts per level.
4. **gameplay: Add collision made from a world's faces.** The `collide box= [up=] material=` rule and face lists: the
   `s` world's selected faces become collision triangles at entry, coplanar neighbours merged and triangulated again,
   walls two-sided; the debug tool's `roof` becomes this path. Tests: a synthetic roof of eight coplanar faces gets two
   upward collision triangles and a player dropped on it stands at its height; a wall in the box adds a two-sided
   triangle only when walls are asked for; a disc check prints faces in and triangles out per level.
5. **gameplay: Add colliders to cut buildings.** A cut object of issue 3 as a parent in the entity tree: its disc
   triangles and the volumes placed as its children move with it as one. Tests: a synthetic cut with a child box, moved,
   stops a ray at the box's new place and not its old one.
6. **tools: Add Coney Studio's world cut and face tools.** In the Level view of a `game:/` level: a box drawn over the
   world, the pieces each rule takes and the collision triangles in it shown before cutting, the walkable areas under
   the old and new place marked; faces selected by box or by click and growth over coplanar neighbours, made into
   collision or fitted with a volume; the results written as the overrides of issues 1, 3 and 4, never as triangles.
   Tests: a synthetic level's cut writes one `cut` and one `edit` statement; a fitted selection writes one `collider`;
   the disc file is unchanged; a box over nothing is refused with the reason.
