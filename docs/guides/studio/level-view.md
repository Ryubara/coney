# Coney Studio: the Level view, gizmos and composing

How a user moves round a level and builds it in Coney Studio's Level view: the fly camera, the keymap, Unity's transform
gizmo, snapping and the grid, selection, undo, and composing a level from primitives, textures, colliders, spawn points
and path points. **Status: design; the first prototype builds the fly camera, click selection and framing the
selection** ([Coney Studio: the prototype](prototype.md)), and nothing else here yet. It is the second half of the
[editor experience
design](editor-experience.md), which holds the panels, the project, Play mode, importing, the two rules every choice
follows and the build issues both pages propose; the editors' documentation read for both is listed in its
[Sources](editor-experience.md#sources), except the snapping survey's, which [Snapping](#snapping) lists itself.

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
| `\` | Snap on or off ([Snapping](#snapping)) | Toggle snapping | Y: Use Snap | the toolbar's grid snap | the Model tab's step boxes |
| Ctrl held while dragging | snapping the other way: steps while Snap is off, free while it is on | increment snapping (from 6.4 also free while on) | Ctrl: the same | not surveyed | not surveyed |
| Shift pressed during a snapped drag | a finer step: a tenth of a move step, a third of a turn, half a scale | none | Shift: the same | not surveyed | not surveyed |
| Ctrl+[ / Ctrl+] | grid smaller / larger, along 0.1, 0.2, 0.5, 1, 2, 5 ... 100 m | the same keys, halving and doubling | none | not surveyed | not surveyed |
| Ctrl+`\` | push the selection onto the grid, in x and y | Push to Grid, on all axes | none | not surveyed | not surveyed |
| V held | snap a vertex onto another thing's vertex (Shift: the pivot; Ctrl+Shift: onto a surface) | vertex snapping | B held (4.7) | V | none |
| Shift+V | keep vertex snapping on | the same | none | not surveyed | not surveyed |
| Ctrl+Shift, dragging the move centre | the selection follows the surface under the cursor | surface snapping | Shift+G: reposition using collisions | not surveyed | not surveyed |
| Ctrl+Shift, dragging the rotate ring | turn to face the point under the cursor | look-at rotation | none | not surveyed | not surveyed |
| End | drop the selection to the ground below | none | Page Down: Snap Object to Floor | End: snap to floor | none |
| Ctrl+Alt+F | move the selection to where the camera looks | Move to View | none | none | none |
| Ctrl+Shift+F | move the selection to the camera itself | Align with View | none | none | none |
| Ctrl+Z, Ctrl+Y (Ctrl+Shift+Z) | undo, redo | same | same | same | same |
| Ctrl+D | duplicate | Duplicate | Duplicate | Duplicate | Duplicate |
| Delete | delete the selection | same | same | same | same |
| F2 | rename | same | same | same | same |
| Ctrl+A | select all in the focused panel | same | same | same | same |
| Ctrl+S | save the open level | Save | Save scene | Save | publish or save |
| Ctrl+P, Ctrl+Shift+P, Ctrl+Alt+P | Play and Stop, Pause, Step | same | F5, F7 | Alt+P | F5, Shift+F5 |
| F8 | Eject in Play (Studio catches it before the game; the Coney console's F8 is the game's outside Studio, [The Coney console](console.md#input)) | none | none | Eject | Run |
| Ctrl+Shift+Alt+P | Play here, from the Level view's camera | none | none | none | Play Here (no key) |
| Ctrl+Shift+R | Restart Play from the level's start | none | none | none | none |

T is left free: Unity's Rect tool edits 2D and interface layouts, which Coney Studio does not make. On macOS, Ctrl reads
Cmd, as in all four. "Not surveyed" marks a snapping key of Unreal or Roblox Studio that the [Snapping](#snapping)
survey, Unity's and Godot's, did not read.

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

Both toggles are on the toolbar beside the tools, as in Unity, and Studio remembers them per user. Snapping, the
toolbar's next group, has its own section: [Snapping](#snapping).

#### How Godot and Roblox Studio differ {#gizmo-differences}

| | Unity (Studio's model) | Godot | Roblox Studio |
| --- | --- | --- | --- |
| Tools | W, E, R and the combined Y | W, E, R; Q is select, which also shows a move and rotate gizmo | Separate Move, Scale and Rotate draggers, chosen with Ctrl+2, Ctrl+3 and Ctrl+4 |
| Handles | arrows and planes; rings, outer ring and free sphere; cubes and a centre cube | the same arrows, planes, rings and scale handles in one style | Move: arrows; Rotate: rings with a ball to drag; Scale: balls on the faces, and Shift or Ctrl to scale evenly or both sides |
| Snapping | a grid snap toggle; Ctrl held for increments ([Snapping](#snapping)) | the *Use Snap* toggle (Y), which Ctrl held turns the other way; steps in *Configure Snap* | Always snapped to the Model tab's Move and Rotate steps, until unticked |
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
level file keeps all three ([The level file](project-formats.md#level-file)), so that every handle
of the Unity gizmo works on every placed thing. A human keeps one ring for good: Coney's humans have a heading, not a
free orientation. Coordinates and units are the game's: metres, degrees, z up ([The layout
format](../sandbox.md#the-layout-format)).

### Snapping {#snapping}

How Unity 6 and Godot 4 snap in their scene views, and what Studio does. Each value says where it comes from:

- **Unity**: its manual for Unity 6.0, 6.2, 6.4 and the current 6.6, and its scripting reference; the defaults were
  **measured** in Unity 6000.2.10f1 in batch mode, reading the snap and grid settings of a new project through the
  editor's own types (`EditorSnapSettings`, `GridSettings`, `SceneViewGrid`) without drawing a Scene view. How Unity
  *draws* its grid as the camera zooms is in neither the manual nor that probe: an editor run that draws the Scene view
  needs a Unity licence, which is the maintainer's question, so those cells say "not documented".
- **Godot**: its manual for 4.7 ([Introduction to 3D][snap-godot-intro]) and its source at `4.7.2-stable`
  ([node_3d_editor_plugin.cpp][snap-godot-plugin], [its header][snap-godot-plugin-h] and
  [editor_settings.cpp][snap-godot-settings]), read, not copied; the grid was **measured** in the official Godot 4.7.2
  editor, with the 3D view's camera placed at known heights and its picture saved and measured line by line.

Unity changed its snapping in 6.4: before 6.4 one *grid snapping* toggle snaps Move, Rotate and Scale, and the grid size
is also the Move increment ([Grid and Snap overlay, 6.0][snap-unity-overlay-60], [automatic snapping,
6.2][snap-unity-gridsnap-62]); from 6.4 the overlay has a toggle each for absolute grid snapping, incremental grid
snapping, angle snapping and scale snapping, and the increment is a setting of its own beside the grid size
([overlay reference, 6.6][snap-unity-overlay-ref], [incremental snapping, 6.4][snap-unity-increments-64], [Unity's
6.4 announcement][snap-unity-64]). The tables give both where they differ.

Coney's axes are the game's: x and y across the ground, **z up**, in metres ([The layout
format](../sandbox.md#the-layout-format)). Unity and Godot are y up, so their ground grid, the XZ plane, is Coney's
**XY plane**, and their "Y" axis is Coney's z.

#### The grid {#snap-grid}

| | Unity 6 | Godot 4.7 |
| --- | --- | --- |
| Default cell | 1 unit on each axis, measured (`GridSettings.size`); the manual: "Grid size is 1 on all axes" can be set per axis ([Customize the grid][snap-unity-customize]) | 1 m, the manual ("set to 1x1 meter by default", [Introduction to 3D][snap-godot-intro]); the drawn cell is never smaller (source: `grid_division_level_min` 0) |
| Changing it | The overlay's *Grid size* field; Ctrl+] and Ctrl+[ make it larger and smaller ([Grid and snap shortcuts][snap-unity-shortcuts]), measured as doubling and halving (0.125, 0.25, 0.5, 1, 2, 4, 8) | Not set by the user: the cell follows the camera, below |
| As the camera zooms | Not documented | **Levels of 8.** The cell is 8 to the power of the level, level = log₈(camera height above the plane) − 0.2, clamped to 0 to 2: 1 m cells while the camera is below 12.1 m, 8 m cells to 97 m, 64 m above. Every 8th line is a major line. Within a level the minor lines fade out as the camera rises (alpha × (1 − the level's fraction)) and the major lines dim towards the minor colour, so at the change the major lines become the next level's minor lines without a jump. Lines also fade with distance from the camera along the plane and where the view grazes the plane (source: `_init_grid` and the grid shader; settings `primary_grid_steps` 8, `grid_division_level_bias` −0.2, `grid_size` 200 lines each side) |
| Pixels per cell | Not documented; the default Scene camera has a 60° vertical field of view and a view size of 10, measured (`kDefaultPerspectiveFov`, `kDefaultViewSize`) | The grid is fixed in the world, so a cell's size on screen follows the camera: looking straight down from height *h*, a cell of *c* metres is *H · c / (2 · h · tan(fov / 2))* pixels for a view *H* pixels high (the editor's fov is 70°, vertical). Measured, below |
| Planes | One plane, Y (the XZ plane) by default, measured (`defaultRenderAxis`); the *Grid Plane* setting picks X, Y, Z or all; an orthographic view along an axis shows the plane facing it ([Customize the grid][snap-unity-customize]); from 6.4 the grid can be moved and turned, and *Move to handle* puts it at the selection ([6.6 reference][snap-unity-overlay-ref]) | XZ only in perspective; all three planes in an orthographic view (source: `_init_grid`; settings `grid_xz_plane` on, `grid_xy_plane` and `grid_yz_plane` off). Always through the origin |
| Look | Opacity 0.5 by default, measured; colour in *Preferences > Colors* | Major lines grey 0.56, minor 0.38, both at alpha 0.5 (`primary_grid_color`, `secondary_grid_color`) |

Godot's grid, measured in the 4.7.2 editor, the camera looking straight down at a 1876-pixel-high view:

| Camera height | Level | Cell drawn | Predicted (formula above) | Measured spacing | Minor lines' contrast |
| --- | --- | --- | --- | --- | --- |
| 2 m | 0.13 | 1 m | 669.8 px | 669.5 px | 29 |
| 5 m | 0.57 | 1 m | 267.9 px | 268 px | 16 |
| 10 m | 0.91 | 1 m | 134.0 px | 134 px | 4 (nearly faded; the 8 m lines 36) |
| 20 m | 1.24 | 8 m | 535.8 px | 536 px | 26 |
| 50 m | 1.68 | 8 m | 214.3 px | 214 px | 13 |
| 150 m | 2.21 | 64 m | 571.6 px | 571.5 px | 32 |

*Contrast is the line's brightness above the floor colour, in 8-bit steps: it falls as the level's fraction grows and
jumps back at each new level, as the source says.*

**Studio's grid:**

- **Where:** on the ground plane, Coney's XY plane, at a height that starts at z = 0 (the sandbox's ground top,
  [The layout format](../sandbox.md#the-layout-format)). *Grid to selection* puts the plane at the selection's pivot
  height and *Grid to origin* puts it back (Unity's *Move to handle* and *Origin*), so the grid can lie on a floor that
  is not at z = 0. An orthographic view along an axis (top, front, side) shows the plane facing it, as both editors do.
- **Cell:** the grid size, **1 m** by default (both editors agree).
- **Zooming:** Godot's scheme, the only one documented, in **tens** instead of eights: cells of the grid size times 10
  to the power of the level, level = log₁₀(camera height above the grid ÷ grid size) − 0.2, clamped to −1 to 2, so 0.1
  m cells below 1.6 m, 1 m to 15.8 m, 10 m to 158 m and 100 m above; a major line every 10 cells; the minor lines fading
  with the level's fraction, and every line with distance and grazing angle, as Godot does. Tens because Coney measures
  in decimal metres and steps in tenths of one ([Moving](#snap-move)); Godot's own source says it took 8 for the
  powers of two of level design.
- **On screen:** fixed in the world, so its spacing follows the camera by the formula above. With a 60° view 1080
  pixels high, a camera 10 m up sees 1 m cells 94 pixels apart; within any level the drawn cell runs from about 590
  pixels down to 59, where its lines have faded out and the next level's cells take over.
- **Look:** Godot's two greys and Unity's opacity 0.5, each set per user in the preferences. The grid hides with the
  view menu's *Grid* toggle.

#### Moving with snap on {#snap-move}

| | Unity 6 | Godot 4.7 |
| --- | --- | --- |
| Turning it on | Before 6.4, the overlay's *Toggle grid snapping* (key `\`), which snaps Move, Rotate and Scale ([6.2][snap-unity-gridsnap-62], [shortcuts][snap-unity-shortcuts]); from 6.4 a toggle each for absolute and incremental Move snapping, angle and scale ([6.6 reference][snap-unity-overlay-ref]). Off by default, measured (`snapEnabled` false) | The toolbar's *Use Snap* toggle, key Y, which snaps move, rotate and scale (source); off by default |
| One step | Before 6.4 the grid size, 1 unit, measured (`EditorSnapSettings.move` (1, 1, 1)); from 6.4 the *Increment Snap*, its own setting per axis | *Translate Snap*, 1 m (source: the `snap_translate_value` default), set in *Transform > Configure Snap*, apart from the grid |
| Absolute or relative | Both. **Absolute** grid snapping rounds the position to the grid along the handle's axis, only while the handle's rotation is Global (or, from 6.4, Grid); with a Local handle it falls back to incremental. **Incremental** moves in steps from where the drag began ([6.6 reference][snap-unity-overlay-ref], [6.4][snap-unity-gridsnap-64]) | **Relative**: the drag's distance is rounded to the step and added to where the node started, so a node at 0.3 m moves to 1.3, 2.3 (source: `_compute_transform`). A node dropped from the FileSystem dock or moved with Shift+G is the exception: its position itself is rounded (`snap_point`) |
| Ctrl held | With snapping off, Ctrl held snaps in increments ([increments, 6.0][snap-unity-increments-60]); from 6.4, Ctrl held while snapping is on moves freely ([6.4][snap-unity-gridsnap-64]) | Turns snapping the other way while held: on when the toggle is off, off when it is on (source: `is_snap_enabled` is the toggle exclusive-or Ctrl); the manual: "press Ctrl while moving, scaling, or rotating" |
| Shift held while snapping | Nothing documented | A finer step: a tenth of Translate Snap, a third of Rotate Snap, half of Scale Snap (source: `get_translate_snap` and the two beside it) |
| Push onto the grid | Ctrl+`\` pushes the selection onto the grid on all axes; the menu's *Align Selected* does one axis ([Align to the grid][snap-unity-align]) | None |
| Keyboard nudges | None: the arrow keys move the camera ([Scene view navigation][snap-unity-nav]) | None: no arrow key is bound in the 3D editor (source) |
| Feedback | Not documented | The distance moved, rounded, as a message in the view ("Translating: …", source) |

**Studio's moving:**

- **One Snap toggle** on the toolbar, key `\` (Unity's), off by default. On, every drag snaps: Move absolutely to the
  grid, Rotate and Scale in their steps. One toggle as in Unity before 6.4 and in Godot; Unity 6.4's four toggles are
  four buttons for one habit.
- **Move snaps absolutely** while the gizmo is Global: the dragged axes of the pivot land on multiples of the grid size
  from the grid's origin, so a shape's `at=` (the middle of its footprint at its bottom, [Shapes](../sandbox.md#shapes))
  or a human's feet sits on a line. With the gizmo Local it moves in steps from where it started, Unity's fallback,
  since a turned axis crosses the grid lines at no fixed spacing.
- **Ctrl turns snapping the other way while held** (Godot's rule, which is also Unity 6.4's both ways): with Snap off,
  Ctrl steps the move **relatively**, from where the drag began, by the **move step**, 1 m by default (both editors);
  with Snap on, Ctrl moves freely.
- **Shift held while snapping takes a finer step** (Godot's): a tenth of the move step (0.1 m), a third of the rotate
  step (5°) and half of the scale step (5%). A tenth of a metre fits most sizes of the sandbox's courses (a 10 cm ledge,
  a 0.6 m fence, [The shipped layouts](../sandbox.md#the-shipped-layouts)) without a second setting. Shift has a meaning
  in two tools already ([The four tools](#gizmo-tools)): on the move centre it chooses the camera-facing plane, and with
  the Transform tool it switches the gizmo to screen space. So Shift held **when the drag starts** keeps that meaning,
  and Shift pressed **during the drag** takes the finer step (Godot reads it during the drag too); on any other handle
  either way gives the finer step.
- **The move step is its own setting** beside the grid size (Unity 6.4 and Godot both keep them apart), each set per
  axis or linked, as in Unity.
- **Ctrl+[ and Ctrl+]** make the grid smaller and larger (Unity's keys) along a metric ladder, 0.1, 0.2, 0.5, 1, 2, 5,
  10, 20, 50 and 100 m, where Unity halves and doubles: halving from 1 m gives 0.125 m, which no Coney size uses.
- **Ctrl+`\`** pushes the selection onto the grid (Unity's *Push to grid*) in x and y only, keeping each height, so a
  thing standing on a floor stays on it; Unity rounds all three axes.
- **No keyboard nudges**, as in both: an exact value is typed in the Inspector.
- **Feedback:** the distance moved shows beside the cursor ([How the handles look and pick](#gizmo-handles)), rounded
  to the step.
- **A new thing** from the Create menu or a drop lands on the ground where the camera looks ([Create](#create-menu));
  with Snap on its x and y are rounded to the grid and it is dropped onto the ground there, Godot's drop rule kept on
  the ground.

#### Rotate and scale steps {#snap-rotate-scale}

| | Unity 6 | Godot 4.7 |
| --- | --- | --- |
| Rotate step | 15°, measured (`EditorSnapSettings.rotate`, `SnapSettings.defaultRotation`); set in the Snapping menu's *Rotate* field, from 6.4 *Incremental angle snap size* | 15° (source: `snap_rotate_value` default), 0.1° steps up to 360° in *Configure Snap*; a third (5°) with Shift |
| Scale step | 1, measured (`EditorSnapSettings.scale`, `SnapSettings.defaultScale`); the manual calls it the scale increment, from 6.4 the *Scale snap multiplier*, and does not say how a step applies | 10 %, a percentage of the starting scale: the factor moves 1.0, 1.1, 1.2 (source: `snap_scale_value` 10, divided by 100); 1 % steps up to 100 % in *Configure Snap*; half (5 %) with Shift |
| Absolute or relative | The manual calls both increments; it does not say | Relative: the angle turned and the scale factor are rounded, from the start (source) |

**Studio:** **15°** (both editors) and **10 % of the starting size** (Godot's, the only documented one), both relative
to where the drag began, as in Godot; set in the toolbar's snap menu. A Coney thing turns about z only for a heading
or yaw ([What a gizmo can change](#gizmo-limits)), so the vertical ring steps 15°; a size changes in tenths of what it
was (a 3 m box by 0.3 m), and the Inspector takes any exact size.

#### Snapping to geometry {#snap-geometry}

| | Unity 6 | Godot 4.7 |
| --- | --- | --- |
| Vertex | **V** held with the Move tool: hover a vertex of the selection, then drag it onto a vertex of another mesh; with Shift+Ctrl also held, onto another mesh's surface; with Shift held, the pivot onto a vertex; **Shift+V** toggles it ([Positioning GameObjects][snap-unity-positioning]). The feedback drawn is not documented | **B** held, new in 4.7 (V is Godot's select tool): the vertex of the selection nearest the cursor, within 30 pixels, gets a **yellow dot**; dragging moves the selection in the plane facing the camera, and within 30 pixels of another node's vertex it jumps there and that vertex gets a **green dot**; dots behind geometry are drawn at 30 % alpha; Escape or a right click cancels. The Transform menu chooses *Snap Vertex to Vertex* or *Snap Origin to Vertex*, and mesh or collision vertices, which Shift held swaps (source; the 4.6 editor has no vertex snapping) |
| Surface | **Shift+Ctrl** while dragging the Move handle: the object goes to where the cursor meets a Collider ([Positioning GameObjects][snap-unity-positioning]) | **Shift+G**, *Reposition Using Collisions*: one selected node follows the cursor over collision shapes, its bounds set against the surface along the surface's normal, the position rounded if snapping is on; the same placement as a scene dropped from the FileSystem dock (source: `_get_instance_position`) |
| Floor | None | **Page Down**, *Snap Object to Floor* ([Introduction to 3D][snap-godot-intro] names it, the key from the source): a ray straight down from 1 m above the bottom centre of each selected node's collision bounds (else its visual bounds, else its origin), up to 500 m, skipping its own bodies; the node's bottom goes to the hit; one undo step, made only if something was hit (source: `_snap_selected_nodes_to_floor`) |
| Look at | **Shift+Ctrl** while dragging the Rotate handle turns the object to face the point under the cursor on a collider ([Positioning GameObjects][snap-unity-positioning]) | None |
| Edges, bounds | None | None (*Snap Origin to Vertex* is the pivot form) |

**Studio's geometry snapping:**

| Snap | Key | Does | From |
| --- | --- | --- | --- |
| Vertex | **V** held, **Shift+V** to keep it on | The selection's vertex nearest the cursor within 30 pixels (scaled with the [UI scale](../debug-menu.md#ui-scale)) is marked; dragging moves the selection in the plane facing the camera, and within 30 pixels of another thing's vertex the marked vertex lands on it. The source is drawn in the gizmo's **yellow**, the target **green**, both dimmed behind geometry; Escape or a right click puts the selection back | Unity's key and modifiers; Godot's threshold, dots and cancel, since Unity documents none of them |
| Pivot to vertex | V and **Shift** held | The pivot, not a vertex, lands on the target vertex | Unity; Godot's *Snap Origin to Vertex* |
| Vertex to surface | V and **Ctrl+Shift** held | The marked vertex lands on the collision surface under the cursor | Unity |
| Surface | **Ctrl+Shift** while dragging the move centre | The pivot follows the collision surface under the cursor. A Coney pivot is at the bottom (a shape's `at=`, a human's feet), so this is also Godot's rule of the bounds resting on the surface. With Snap on, x and y are rounded to the grid and the height is found on the surface there | Unity's keys |
| Drop to ground | **End** | Godot's *Snap Object to Floor*: a ray down from 1 m above the bottom of each selected thing, up to 500 m, skipping its own collision; it moves only if it hits; one undo step for the selection | Godot's rule; the key is Unreal's End ([the keymap](#keymap)), since Page Down has no Unity meaning to keep |
| Face a point | **Ctrl+Shift** while dragging the rotate ring | The heading or yaw turns to face the point under the cursor on a surface: a spawn point or fighter faces a door | Unity's look-at rotation, about z only |

The targets are what the Level view draws: the shapes' meshes and the level's world; in the *Collision* view mode, the
collision triangles' vertices and faces instead (Godot's mesh or collision choice, made by the view mode Studio
already has rather than a menu). Surface and ground snapping always use the collision triangles, which every Coney
level has ([Collision](../../research/collision.md#triangles)). Neither editor snaps to edges or to another thing's
bounds, and Studio does not either: vertex and surface snapping place everything a level is built from, boxes, ramps
and stairs by their corners and floors.

#### Settings and the toolbar {#snap-settings}

| | Unity 6 | Godot 4.7 |
| --- | --- | --- |
| Snap steps and toggles | Per project and per user: `Library/EditorSnapSettings.asset` in the project, measured, a folder kept out of version control; the overlay's settings "apply globally to all Scene views" ([6.0][snap-unity-overlay-60]) | The steps per project, in the project's editor metadata under `.godot/` (source: `set_project_metadata`), and with the toggle per scene, in the scene's saved editor state (source: `snap_enabled`, `translate_snap` in the 3D editor's state) |
| The grid | Size per project with the steps; visibility, plane and opacity per Scene view, measured (`SceneViewGrid` is saved with the view); colour in the per-user Preferences | Editor Settings, per user for every project: `editors/3d/grid_size`, `primary_grid_steps`, the division levels and bias, the planes and colours |
| Keys | The Shortcuts Manager, per user | Editor Settings, per user |
| Toolbar | The *Grid and Snap* overlay: grid visibility and size; before 6.4 the snap toggle and its Snapping menu (grid size, Snap to Grid, Rotate, Scale, Align Selected); from 6.4 the four toggles with their step fields and a *Grid and Snap Settings* window | The *Use Snap* button (a magnet) in the 3D toolbar; *Transform > Configure Snap* (Translate in metres, Rotate in degrees, Scale in percent), *Snap Object to Floor* and the vertex snap choices |

**Studio:** the grid size, the move, rotate and scale steps, the grid's height and the Snap toggle are saved **per
project, for this user**, in `.studio/`, which never travels with a shared project or a package ([Coney Studio's
project](editor-experience.md#project-coney)): Unity's rule, so a project reopens as it was left and two users of one
project keep their own. The grid's look and the keys are **per user** in the preferences, as in both. The toolbar's
snap group holds the **Snap** toggle (a magnet) and beside it the **snap menu**: grid size, move step (per axis or
linked), rotate step, scale step, *Push to grid*, *Grid to selection*, *Grid to origin*. The toolbar shows no words
([Where Studio puts icons](icons.md#studio)), so the grid size is in the snap menu and the Snap button's tooltip rather
than beside it, as Unity's overlay has it; the Snap toggle is the button [Icons](icons.md) calls *grid snap*, which
snaps the move to the grid and the rotate and scale to their steps.

[snap-unity-overlay-60]: https://docs.unity3d.com/6/Documentation/Manual/GridAndSnapOverlay.html
[snap-unity-gridsnap-62]: https://docs.unity3d.com/6000.2/Documentation/Manual/GridSnap.html
[snap-unity-gridsnap-64]: https://docs.unity3d.com/6000.4/Documentation/Manual/GridSnap.html
[snap-unity-increments-60]: https://docs.unity3d.com/6/Documentation/Manual/SnapIncrements.html
[snap-unity-increments-64]: https://docs.unity3d.com/6000.4/Documentation/Manual/SnapIncrements.html
[snap-unity-overlay-ref]: https://docs.unity.com/en-us/engine/current/manual/working-with-scenes/scenes-manage-gameobjects/overlays/reference/overlay-grid-snap-reference
[snap-unity-customize]: https://docs.unity3d.com/6000.2/Documentation/Manual/CustomizeGrid.html
[snap-unity-shortcuts]: https://docs.unity3d.com/6000.2/Documentation/Manual/GridShortcuts.html
[snap-unity-align]: https://docs.unity3d.com/6000.2/Documentation/Manual/GridAlign.html
[snap-unity-positioning]: https://docs.unity.com/en-us/engine/current/manual/working-with-scenes/scenes-manage-gameobjects/positioning-game-objects
[snap-unity-nav]: https://docs.unity.com/en-us/engine/current/manual/unity-editor/editor-windows-views-reference/using-the-scene-view/scene-view-navigation
[snap-unity-64]: https://discussions.unity.com/t/feature-highlight-improved-grid-and-snap-workflows/1695070
[snap-godot-intro]: https://docs.godotengine.org/en/4.7/tutorials/3d/introduction_to_3d.html
[snap-godot-plugin]: https://github.com/godotengine/godot/blob/4.7.2-stable/editor/scene/3d/node_3d_editor_plugin.cpp
[snap-godot-plugin-h]: https://github.com/godotengine/godot/blob/4.7.2-stable/editor/scene/3d/node_3d_editor_plugin.h
[snap-godot-settings]: https://github.com/godotengine/godot/blob/4.7.2-stable/editor/settings/editor_settings.cpp

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
| **Stealth zone** | Floor that puts the player in stealth when he walks onto it: an upright box, drawn translucent blue, that marks the floor triangles already inside it as shadow ground ([Stealth zones](entities.md#stealth-zone)) | The game's shadow ground, collision triangles with flag `0x10` ([Stealth: shadow ground](../../research/stealth.md#shadow-ground)), which Coney implements only in a scripted level, so a layout shape's `flags=0x10` does not hide the player in a sandbox | waits on level files of version 2 and on Play running the hiding rules ([Stealth zones](entities.md#stealth-zone)) |
| **Path point** | A point of the level's path network, linked to others | Coney's AI pathing ([AI moving](../../research/ai-moving.md)) | waits on Coney's path overlays; a project's paths are the content page's ([Paths](content-and-formats.md#paths-kind)) |
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
  needs the project's level file, whose `collider` statements hold it
  ([Colliders and primitives](content-and-formats.md#collision)), and a collider for an imported model waits on
  custom content ([Custom content](../../roadmap.md#custom-content)).
- **Path points.** Studio's design for them, for when Coney can draw and read a project's paths: a *Paths* mode of the
  Level view shows the points and their links as lines on the ground; Create adds a point linked to the selected one,
  the Move gizmo places it (snapped to the ground), and *Link* (L) links or unlinks two selected points. The AI walks
  them with Coney's own path planning; Studio adds no path logic of its own.
- **Custom and customised models, custom missions.** They wait on what Coney lacks: models from outside the disc on
  custom content, a human's model changed in place on [#1053](issue:1053), missions on the mods runtime and a mission
  format of Coney's own. Each comes into Studio's Create menu and Inspector when Coney has it.
