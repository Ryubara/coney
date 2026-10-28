# Coney Studio: the Level view, gizmos and composing

How a user moves round a level and builds it in Coney Studio's Level view: the fly camera, the keymap, Unity's transform
gizmo, selection, undo, and composing a level from primitives, textures, colliders, spawn points and path points.
**Status: design; nothing here is built.** It is the second half of the [editor experience
design](editor-experience.md), which holds the panels, the project, Play mode, importing, the two rules every choice
follows and the build issues both pages propose; the editors' documentation read for both is listed in its
[Sources](editor-experience.md#sources).

## The Level view {#level-view}

### The fly camera {#fly-camera}

All four editors fly the camera the same way: hold the **right mouse button** in the view, move the mouse to look, **W A
S D** to move along the view and sideways, **Q and E** to sink and rise. They differ on Shift: in Unity and Godot Shift
flies **faster**, in Roblox Studio it moves the camera **slower** for close work, and in Godot Alt is slower. Unity,
Godot and Unreal change the base speed with the mouse wheel while the right button is held.

Coney Studio copies the shared controls and Unity's Shift (the maintainer's choice), and takes Godot's Alt for slower:

| Input | Does | Source |
| --- | --- | --- |
| Right mouse held, move the mouse | look round | all four |
| Right mouse held, W A S D | fly forward, left, back, right along the view | all four |
| Right mouse held, Q / E | sink / rise | Unity, Godot, Roblox (Unreal: Q and E too) |
| Right mouse held, Shift | fly faster while held | Unity, Godot |
| Right mouse held, Alt | fly slower while held | Godot |
| Right mouse held, mouse wheel | change the base speed, which stays | Unity, Godot, Unreal |
| Alt + left drag | orbit round the pivot | Unity |
| Middle drag | pan | Unity, Unreal (Godot pans with Shift and the middle button; its middle drag orbits) |
| Mouse wheel | zoom towards the pivot | Unity, Godot |
| F | frame the selection | all four |
| Shift+F | keep the view locked on the selection while it moves | Unity |
| Pad 1 | fly as the world viewer's camera does | Coney's [world viewer](../running-viewers.md#the-world-viewer) |

Q to Y are tools only while the right button is up (Unity's rule), so Q and E fly while it is held. The fly camera moves
at the fixed step like everything else, so a scripted input flies the same path every time, as the world viewer does.

### The keymap {#keymap}

**Unity's keymap**, so a Unity user's hands already know it; Godot and Unreal use the same letters for move, rotate and
scale, and the table says where they differ. Every key can be changed: the keymap is data, saved per user beside the
layouts, as Unity's Shortcuts Manager keeps it. Studio ships one keymap, not one per editor, so there is one set of keys
to document and test.

| Key | Does | Unity | Godot | Unreal | Roblox Studio |
| --- | --- | --- | --- | --- | --- |
| Q | View tool: drag pans, nothing is selected | View (hand) | Select | none | none |
| W | Move tool | Move | Move | Translate | Ctrl+2 |
| E | Rotate tool | Rotate | Rotate | Rotate | Ctrl+4 |
| R | Scale tool | Scale | Scale | Scale | Ctrl+3 |
| Y | Transform tool: move, rotate and scale in one gizmo | Transform | (Q: move and rotate) | none | none |
| Z | Gizmo at the pivot or at the centre of the selection | Pivot / Center | none | none | none |
| X | Gizmo in world or local axes | Global / Local | T: local space | Ctrl+` | Ctrl+L |
| Ctrl held while dragging | snap to the grid's step | snap increment | Ctrl: snap | grid snap on by default | the grid step |
| V held | snap a vertex onto another model's vertex | vertex snapping | none | V | none |
| End | drop the selection to the ground below | (Ctrl+Shift drag: surface snapping) | none | End: snap to floor | none |
| Ctrl+Alt+F | move the selection to where the camera looks | Move to View | none | none | none |
| Ctrl+Shift+F | move the selection to the camera itself | Align with View | none | none | none |
| Ctrl+Z, Ctrl+Y (Ctrl+Shift+Z) | undo, redo | same | same | same | same |
| Ctrl+D | duplicate | Duplicate | Duplicate | Duplicate | Duplicate |
| Delete | delete the selection | same | same | same | same |
| F2 | rename | same | same | same | same |
| Ctrl+A | select all in the focused panel | same | same | same | same |
| Ctrl+S | save the open level | Save | Save scene | Save | publish or save |
| Ctrl+P, Ctrl+Shift+P, Ctrl+Alt+P | Play and Stop, Pause, Step | same | F5, F7 | Alt+P | F5, Shift+F5 |
| F8 | Eject in Play (Studio catches it before the game; the Coney console's F8 is [#1064](issue:1064)'s to place) | none | none | Eject | Run |
| Ctrl+Shift+Alt+P | Play here, from the Level view's camera | none | none | none | Play Here (no key) |
| Ctrl+Shift+R | Restart Play from the level's start | none | none | none | none |

T is left free: Unity's Rect tool edits 2D and interface layouts, which Coney Studio does not make. On macOS, Ctrl reads
Cmd, as in all four.

### Gizmos {#gizmos}

Studio's transform gizmo is **Unity's, handle for handle** (the maintainer's choice): the four tools below, the same
colours, the same modifiers and the same toggles, so a Unity user moves, turns and sizes things in Studio without
learning anything.

#### The four tools {#gizmo-tools}

| Tool | Key | Handles | Dragging |
| --- | --- | --- | --- |
| **Move** | W | An arrow per axis; a small square per pair of axes (the plane handles); the centre | An arrow moves along its axis; a square moves in its plane; Shift on the centre turns it into a flat square that moves in the plane facing the camera |
| **Rotate** | E | A ring per axis; an outer ring round the whole gizmo; the sphere inside the rings | A ring turns about its axis; the outer ring turns about the line of sight (screen space); a drag inside the sphere turns freely, as a trackball |
| **Scale** | R | A line per axis ending in a cube; a cube at the centre | An axis cube scales along that axis; the centre cube scales evenly on every axis |
| **Transform** | Y | Move's arrows and plane squares, Rotate's rings and Scale's cubes together, in one gizmo | Each handle as in its own tool; Shift held switches the whole gizmo to screen space, as in Unity |

Q (the View tool) shows no gizmo: a drag pans.

#### How the handles look and pick {#gizmo-handles}

- **Colours.** x red, y green, z blue, a plane square in the colour of the axis it does not move along, the free and
  outer handles grey-white. The handle under the cursor, and the one being dragged, turn **yellow** (Unity's highlight
  colour).
- **Constant size on screen.** The gizmo is drawn the same size whatever the distance, scaled with the [UI
  scale](../debug-menu.md#ui-scale), and drawn over the level without depth, so it is never hidden in a wall.
- **Facing the camera.** The plane squares sit in the quadrant towards the camera, and an axis that points nearly
  straight at the camera fades out, since a drag along it would be erratic (both Unity's).
- **Picking.** A click tests the gizmo's handles first, within a few pixels of each line, ring or cube, the nearest to
  the cursor winning; only a click that hits no handle selects something in the level ([Selection](#selection)). So a
  handle in front of another object is always the one dragged.
- **Feedback while dragging.** The value being changed (a distance in metres, an angle in degrees, a scale factor) shows
  beside the cursor, and the Inspector's field changes with it.
- **One undo step.** A drag that ends is one step of [undo](#undo), however long it lasted; Escape during a drag puts
  the selection back where the drag began.

#### The toggles {#gizmo-toggles}

| Toggle | Key | Choices | As in |
| --- | --- | --- | --- |
| Handle position | Z | **Pivot**: at the selection's own origin (an `at=`, a human's feet); **Center**: at the middle of the selection's bounds, which a multiple selection turns about | Unity's Pivot / Center |
| Handle rotation | X | **Global**: the world's axes; **Local**: the selection's own axes, so W moves a turned ramp along its slope | Unity's Global / Local |

Both toggles are on the toolbar beside the tools, as in Unity, and Studio remembers them per user.

#### Snapping {#gizmo-snapping}

| Snap | How | Step |
| --- | --- | --- |
| **Increment** | **Ctrl** held while dragging (Unity) | Move 0.1 m along each axis, Rotate 15°, Scale 0.1 of the size; each step set on the toolbar's snap menu, Move per axis or linked, as in Unity's snap increment menu |
| **Grid** | The toolbar's grid snap toggle | Moves land on the Level view's grid (1 m, with 0.1 m lines up close) along the dragged axis, as Unity's grid snapping does |
| **Vertex** | **V** held | Drag a vertex of the selection onto a vertex of another model (Unity's vertex snapping); Shift+V keeps it on |
| **Surface** | **Ctrl+Shift** while dragging the move centre | The selection lands on the collision surface under the cursor (Unity's surface snapping); End drops it straight down ([the keymap](#keymap)) |

The steps are Coney's: a tenth of a metre lands on most sizes of the sandbox's courses (a 10 cm ledge, a 0.6 m fence;
[The shipped layouts](../sandbox.md#the-shipped-layouts)), the Inspector takes any exact value (a 1.65 m wall), and 15°
is the rotation step all of Unity, Godot and Roblox Studio users meet.

#### How Godot and Roblox Studio differ {#gizmo-differences}

| | Unity (Studio's model) | Godot | Roblox Studio |
| --- | --- | --- | --- |
| Tools | W, E, R and the combined Y | W, E, R; Q is select, which also shows a move and rotate gizmo | Separate Move, Scale and Rotate draggers, chosen with Ctrl+2, Ctrl+3 and Ctrl+4 |
| Handles | arrows and planes; rings, outer ring and free sphere; cubes and a centre cube | the same arrows, planes, rings and scale handles in one style | Move: arrows; Rotate: rings with a ball to drag; Scale: balls on the faces, and Shift or Ctrl to scale evenly or both sides |
| Snapping | Ctrl held for increments; a grid toggle | Ctrl held for increments, or the snap toggle; steps in *Configure Snap* | Always snapped to the Model tab's Move and Rotate steps, until unticked |
| Local and global | X | T | Ctrl+L |
| Pivot and centre | Z | none | the part's pivot, edited with the Pivot tool |

Studio skips Roblox's always-on snapping (a Unity user expects free movement unless Ctrl is held) and its face-scale
balls (one face moved, the opposite fixed), which no Coney thing needs while a box's size is set from its middle.

#### What a gizmo can change {#gizmo-limits}

A gizmo is a face of the Inspector, never a way round it: it changes only what the selected thing's format or Coney's
engine can hold, and an axis the selection cannot take is drawn grey and does not pick.

| Selected | Move | Rotate | Scale |
| --- | --- | --- | --- |
| A sandbox shape | its `at=` | its `yaw=`: the vertical ring alone, the only turn the layout format has | its size arguments: a box's `size=` on each axis; a cylinder's or capsule's `radius=` across and `height=` up; a sphere's `radius=`, evenly only; a ramp's `width=` across, `length=` along and `height=` up (its angle follows); stairs' `width=` across, `run=` along and `rise=` up, the `steps=` count unchanged |
| A spawn point, a target, a fighter | its `at=` | its `heading=`: the vertical ring alone | none |
| A camera view | its `at=` | its `yaw=` and `pitch=` | none |
| The player or an AI human, while playing | *Teleport* | *Heading*: the vertical ring alone | *Scale*, evenly ([Entity editor](../debug-menu.md#entity-editor)) |
| A target, while playing | none: the Entity editor takes only *Frozen* for a target | none | none |
| An object, while playing | none: the Entity editor takes no object edit today | none | none |

So the full gizmo (three rings, the free sphere, scale on any axis of anything) needs a level format that keeps a full
position, rotation and scale for every placed thing. The sandbox layout keeps a position, a yaw and sizes; Coney's own
level format is the content page's ([#1058](issue:1058)), and this page asks it to keep all three, so that every handle
of the Unity gizmo works on every placed thing. A human keeps one ring for good: Coney's humans have a heading, not a
free orientation. Coordinates and units are the game's: metres, degrees, z up ([The layout
format](../sandbox.md#the-layout-format)).

### Selection {#selection}

What all four do, and Studio with them:

- A click in the Level view selects what is under the cursor; a click on empty space clears the selection.
- **Shift+click** adds to the selection and **Ctrl+click** toggles one in or out (Unity); a **drag on empty space**
  draws a box that selects what it encloses (Unity and Godot; Unreal's perspective view needs Ctrl+Alt with the drag).
- **One selection** shared by every panel: choosing in the Hierarchy selects in the Level view and fills the Inspector,
  and the reverse. In Play it is the Entity inspector's selection, so the Debug draw's highlight follows it.
- The Inspector shows the values a multiple selection shares and edits them all together; a value they differ in shows
  as mixed (Unity).
- A selected thing is outlined in the Level view; what is under the cursor is lit before the click (Unity's and Unreal's
  pre-selection highlight).

### Undo {#undo}

- **Every edit is a command** with an inverse, kept on one undo stack per open level: a gizmo drag (one step, however
  long the drag), an Inspector value, a create, a duplicate, a delete. This is the model of the Entity editor, whose
  edits are already commands, queued, applied between steps, logged and replayable ([Entity
  editor](../debug-menu.md#entity-editor)); Studio's edit-time commands add the inverse.
- **Edit > Undo History** lists the stack, and choosing a line goes back to it (Unity's Undo History).
- Saving does not clear the stack; closing the level does.
- **In Play**, a level edit is undone like any other and the undo is sent to the running game too; an edit of the
  running game has no undo, since Stop throws it away.
- **File operations are not undone** (none of the four undoes them); a delete asks first and goes to the recycle bin.

## Composing a level {#composing}

Users compose Coney-compatible content in Studio the way they do in Unity, Roblox Studio and Godot: maps and levels from
primitives, textures applied, colliders, path points, the characters and things a level holds, and later custom missions
and models. This section says what each is in the four editors and what it is in Coney.

### What the four editors do {#composing-survey}

| Thing | Unity | Godot | Roblox Studio |
| --- | --- | --- | --- |
| Primitives | *GameObject > 3D Object*: cube, sphere, capsule, cylinder, plane, quad | a MeshInstance3D with a box, sphere, capsule, cylinder, plane or prism mesh; CSG shapes for blockouts | Parts: block, sphere, wedge, corner wedge, cylinder |
| Texturing | a material with a texture, dragged onto the object | a material on the mesh, a texture dragged onto it | a texture or decal on a face, a material and a colour per part |
| Colliders | collider components (box, sphere, capsule, mesh), added to an object or alone | a CollisionShape3D under a body | every part collides unless *CanCollide* is off |
| Path points | a baked NavMesh, with links between areas | a NavigationRegion3D baked, Path3D curves | PathfindingService over the parts, with modifiers and links |
| Where a new thing appears | at the Scene view's pivot | at the scene's origin | in front of the camera, on the surface |

### Coney Studio's Create menu {#create-menu}

*Create* (the menu, and a right-click in the Hierarchy or the Level view; on an entity, the new one becomes its child
once the level format keeps parents) offers exactly the kinds a Coney level can hold today, as Roblox's *Insert Object*
offers Roblox's classes. A new thing appears where the camera looks, dropped on the ground below (Roblox's rule, made of
the Entity editor's *Move to camera* and *Drop to ground*), is selected, and the Move tool is chosen.

| Create | What it makes | Realizes in Coney | State |
| --- | --- | --- | --- |
| **Primitive**: box, ramp, stairs, cylinder, capsule, sphere | A solid textured shape, sized in metres | The sandbox layout's shapes ([Shapes](../sandbox.md#shapes)), the Spawner page's objects | ready |
| **Spawn point** | Where the player can start; Play uses the first | A layout's `spawn` | ready |
| **Target** | A passive human to fight | A layout's `target` | ready |
| **AI fighter** | An AI human that fights back | A layout's `fighter` ([AI fighters](../sandbox.md#ai-fighters)) | ready |
| **Camera view** | A viewpoint, chosen from the Level view's view menu | A layout's `view` | ready |
| **Path point** | A point of the level's path network, linked to others | Coney's AI pathing ([AI moving](../../research/ai-moving.md)) | waits on Coney's path overlays and a project path format ([#1058](issue:1058)) |
| **Character** of any type | A human of one of the game's character types, on a side | The AI fighters page's model picker and *Change character* ([pages](../debug-menu.md#pages)) | waits on a level format that places one (a layout's fighter is one fixed type) |

Primitives repeat as the layout's `repeat=` and `step=` do: the Inspector's *Repeat* field makes a row of copies, and
Ctrl+D duplicates one.

### Textures, colliders and paths {#composing-parts}

- **Applying a texture.** Drag a PNG from the File system onto a shape in the Level view, or pick it in the Inspector:
  it becomes the shape's `texture=`, with `uv=` (tiles per metre) and `tint=` beside it
  ([Shapes](../sandbox.md#shapes)). A texture imported is a texture usable at once
  ([Importing](editor-experience.md#import)).
- **Colliders.** Every primitive collides, as a Roblox part does, and the Inspector's *Collision* group holds its
  collider: solid or not (`solid=`), the collision material that decides footsteps and what a fall does (`material=`),
  the triangle flags (`flags=`) and the area byte (`area=`), all of them Coney's collision fields
  ([Collision](../../research/collision.md#triangles)). The Level view's *Collision* view mode draws them, from the
  Debug draw's collision lines. Two kinds wait: a collider with nothing drawn (an invisible wall, Unity's lone collider)
  needs a layout option or Coney's level format ([#1058](issue:1058)), and a collider for an imported model waits on
  custom content ([Custom content](../../roadmap.md#custom-content)).
- **Path points.** Studio's design for them, for when Coney can draw and read a project's paths: a *Paths* mode of the
  Level view shows the points and their links as lines on the ground; Create adds a point linked to the selected one,
  the Move gizmo places it (snapped to the ground), and *Link* (L) links or unlinks two selected points. The AI walks
  them with Coney's own path planning; Studio adds no path logic of its own.
- **Custom and customised models, custom missions.** They wait on what Coney lacks: models from outside the disc on
  custom content, a human's model changed in place on [#1053](issue:1053), missions on the mods runtime and a mission
  format of Coney's own. Each comes into Studio's Create menu and Inspector when Coney has it.
