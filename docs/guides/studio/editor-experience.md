# Coney Studio: the editor experience

The design of how Coney Studio looks and feels to the person using it: its panels and their default layout, the
layouts a user saves, the project folder, Play mode, moving round the scene, the keymap, gizmos, selection, undo and
importing files. **Status: design; nothing here is built.** What Studio is, and the other design pages, are on
[Coney Studio](index.md); the GUI library and how Play runs the engine are the technology page's
([#1056](issue:1056)), so this page holds for whichever library that page picks.

The design comes from four editors people already know: Unity, Godot, Unreal and above all **Roblox Studio**, the
closest model, because it too is an editor for one game's content rather than a general engine. FiveM is the model for
resources and scripts, which the Lua SDK page ([#1057](issue:1057)) owns. Each section says what the four do, then
what Coney Studio copies and what it skips, and why. Two rules decide every choice:

1. **Familiar first.** Where the editors agree, Studio does the same, so a Unity, Godot or Unreal user is at home in a
   minute. Where they differ, Studio takes **Unity's** keys and words (the maintainer's choice) and Roblox Studio's
   model of a game-specific editor.
2. **Only what Coney has.** Each panel and tool realizes a feature Coney has today, and says which. A feature that
   needs something Coney lacks is listed with what it waits on, and is not in Studio until Coney has it
   ([What Studio is](index.md#principles)).

## Contents {#contents}

- [The panels](#panels), [the default layout](#default-layout) and [layouts](#layouts)
- [The project](#project): the folder, its layout and the game's files
- [Play mode](#play-mode)
- [The Scene view](#scene-view): the fly camera, [the keymap](#keymap), [gizmos](#gizmos), [selection](#selection)
  and [undo](#undo)
- [Composing a level](#composing): primitives, textures, colliders and path points
- [Importing](#import)
- [What Studio skips](#skipped)
- [Proposed build issues](#build-issues)
- [Sources](#sources)

## The panels {#panels}

### What the four editors have {#panels-survey}

All four are built round the same five things: a **3D view** of the scene being edited, a **tree** of what the scene
holds, a **property sheet** of what is selected, a **file browser** of the project's assets and an **output log**.
They name them differently:

| Job | Unity | Godot | Unreal | Roblox Studio |
| --- | --- | --- | --- | --- |
| The 3D view, edited | Scene view | 3D viewport | Level viewport | 3D viewport |
| The game, running | Game view | a separate game window | Play In Editor viewport or window | the same viewport, in a playtest |
| The scene's tree | Hierarchy | Scene dock | Outliner | Explorer |
| What is selected | Inspector | Inspector | Details | Properties |
| The project's files | Project window | FileSystem dock | Content Browser | Asset Manager, Toolbox |
| Import settings | the Inspector, for an asset | Import dock | the asset editor | the 3D Importer's dialog |
| The log | Console | Output | Output Log | Output |
| A line of code to run | none built in | none built in | Console command line | Command Bar |

Coney Studio uses **Unity's names** (rule 1), with **File system** for the file browser (the maintainer's word, and
Godot's) and **Inspector** for the property sheet, as Unity and Godot both call it.

### Coney Studio's panels {#panel-list}

Each panel and the Coney feature it realizes. A panel marked *waits* is designed here but is not in Studio until Coney
has what it names.

| Panel | Its job | Realizes in Coney | State |
| --- | --- | --- | --- |
| **Scene view** | The level being edited, seen through the fly camera; selection, gizmos, and the debug views as view modes | The [world viewer](../running-viewers.md#the-world-viewer), the [sandbox](../sandbox.md)'s free camera, [Debug draw](../debug-menu.md#debug-draw) and the [mesh views](../debug-menu.md#mesh-views) | ready |
| **Game view** | The game running in Play mode, with Play, Pause, Step and Stop | The play mode (`--play-level`, a sandbox with `sandbox:NAME`) and the debug menu's Time page (pause, step, time scale) ([pages](../debug-menu.md#pages)) | ready |
| **Hierarchy** | Every entity of the level, by kind; choosing one selects it everywhere | The [Entity inspector](../debug-menu.md#entity-inspector)'s entity list (player, targets, AI humans, doors, panes, objects) and a sandbox layout's statements | ready |
| **Inspector** | The selection's values, live, and the edits it takes; for a file, its details and import settings | The merged Entity inspector and Entity editor ([#1053](issue:1053)) | ready once [#1053](issue:1053) merges |
| **File system** | The project folder as a tree, and the game's files read-only; drag-and-drop and the Import button land here | Coney's asset loading and the [install folder](../asset-and-script-architecture.md#install-folder) | ready |
| **Console** | The run log's lines, filtered by level and system, and a Lua line to run | The [run log](../run-log.md) and the [Lua console](../debug-menu.md#lua-console) (expressions and assignments; full Lua waits on a compiler) | ready |
| **Natives** | Every native with its status, signature and an argument editor to call it | The debug menu's [Natives](../debug-menu.md#natives) page and the [masterlist](../../references/bindings/index.md) | ready |
| **Model viewer** | One model on its own: shaded, wireframe, by material, normals and bone axes, its counts | The [character viewer](../running-viewers.md#the-character-viewer), the [mesh views](../debug-menu.md#mesh-views) and the [Mesh group](../debug-menu.md#mesh-group) | ready |
| **Animation viewer** | Any clip on a model: play, loop, speed, in place, by set and category | The [Animation player](../debug-menu.md#animation-player) | ready; a scrubbing timeline waits on a seek in the animation player |
| **Texture viewer** | A texture dictionary or sprite sheet, image by image | The texture and sprite sheet viewers ([viewers](../running-viewers.md#viewing-texture-dictionaries)) | ready |
| **Debug panels** | Every other page of the debug menu (Tunables, Player, Camera, Spawner, AI fighters, HUD, Display, Audio, Network), as a panel | The debug menu's model, which any front end renders ([One model](../debug-menu.md#one-model)) | ready |
| **Lighting** | The level's sky, sun, ambient light, fog, occlusion and shadows | A sandbox layout's `sky`, `sun`, `ambient`, `fog`, `occlusion` and `shadows` statements ([Statements](../sandbox.md#statements)) | ready for sandbox layouts; a game level's lights and a sun that changes sneaking wait on the [realtime lighting](../realtime-lighting.md) enhancement |
| **Script editor** | A project's Lua scripts, with VS Code's keys, completions from the masterlist and live errors | The mods runtime ([Script mods](../../roadmap.md#script-mods)) | waits on the mods runtime and the technology page's editor choice |
| **Paths** | A level's path network, drawn and edited | Path overlays from the level's path data ([Still to come](../debug-menu.md#still-to-come)) | waits on Coney's path overlays and a project path format ([#1058](issue:1058)) |
| **Mission editor** | A mission's checkpoints and objectives | Coney's level scripts ([Scripts](../asset-and-script-architecture.md#scripts)) | waits on the mods runtime and a mission format of Coney's own |

The **Debug panels** row is the cheapest and the most important: the debug menu is already one model drawn by two front
ends (the pad menu and the Dear ImGui overlay), and Studio becomes a **third front end** of the same model. A page added
to the debug menu then appears in Studio with no Studio code, which is how Studio keeps up with Coney by construction
for everything the debug menu shows.

## The default layout {#default-layout}

### What the four editors do {#layout-survey}

Every editor puts the 3D view in the middle and the property sheet on the right. They differ on the file browser:
Unity (its *Default* layout) and Unreal put it along the bottom under the view, Godot puts its FileSystem dock at the
lower left under the Scene tree, and Roblox Studio puts Explorer and Properties together on the right.

### Coney Studio's default {#layout-default}

Coney Studio takes **Godot's arrangement**, which is what the brief asks for: the **Inspector on one side and the
File system on the other**, so the two panels used most while building a level are both always in view, and the
Hierarchy sits over the File system, as Godot's Scene dock sits over its FileSystem dock. The Scene and Game views
share the middle as tabs, as in Unity, and the Console and Natives share the bottom.

```text
+------------------------------------------------------------------------------------------+
| File  Edit  Create  View  Play  Window  Help                                             |
| [Q][W][E][R][Y]  Pivot | Global   Snap           [ Play ][ Pause ][ Step ]   Layout: Default v |
+------------------+------------------------------------------------+----------------------+
| Hierarchy        |  Scene | Game                                  | Inspector            |
|  Player          |                                                |  Identity            |
|  Spawns          |                                                |  Transform           |
|  Shapes          |            the level, seen by the              |  Meters              |
|  AI humans       |            fly camera                          |  ...                 |
+------------------+                                                |                      |
| File system      |                                                |                      |
|  project:/       |                                                |                      |
|   levels/        +------------------------------------------------+                      |
|   textures/      | Console | Natives                              |                      |
|  game:/ (read)   |  [info] level loaded ...                       |                      |
+------------------+------------------------------------------------+----------------------+
```

*Figure: the default layout. The Model, Animation and Texture viewers open as tabs beside Scene and Game; the
debug panels and Lighting open where the user last docked them, or floating.*

The toolbar holds the tools (Q to Y, [the keymap](#keymap)), the gizmo's pivot and space toggles, snapping, the Play
controls in the middle, as in Unity, and the layout menu on the right, as in Unity's toolbar.

## Layouts {#layouts}

### What the four editors do {#layouts-survey}

- **Unity:** any window docks, tabs or floats; *Window > Layouts* saves the arrangement under a name, loads one, and
  resets to the factory layouts (*Default*, *2 by 3*, *4 Split*, *Tall*, *Wide*); the layout menu is also on the
  toolbar.
- **Godot:** docks move from a menu on each dock or float in their own window; *Editor > Editor Layouts > Save* keeps
  dock positions, sizes, floating windows and the FileSystem dock's settings under a name, in `editor_layouts.cfg` in
  the editor's own settings folder. *Default* is built in and cannot be deleted; saving over it changes it, and deleting
  that copy brings the factory one back.
- **Unreal:** tabs dock and float; *Window > Save Layout* and *Load Layout*, and a reset to the default.
- **Roblox Studio:** windows dock, tab, pin to an edge or float, and the arrangement is kept between sessions, but there
  is no named layout to save and switch to (asked for by its users, not offered).

### Coney Studio's layouts {#layouts-coney}

Copied from Godot and Unity together:

- Every panel docks to any side, tabs with others or floats in its own window; every split resizes.
- **Window > Layouts** and the toolbar's layout menu: *Save layout as...* (a name), the saved layouts to switch to,
  *Delete*, and *Reset to Default*. *Default* is built in and always comes back on reset (Godot's rule).
- A layout is **per user, never per project**: it lives in Studio's settings folder, as Coney's overlay keeps its
  windows in `coney-imgui.ini` beside the tunables file ([The developer overlay](../debug-menu.md#the-developer-overlay)),
  so a project shared with someone else never changes their screen. Studio reopens with the layout it closed with.
- A layout saves which panels are open, where, their sizes and floating windows' places, at the
  [UI scale](../debug-menu.md#ui-scale) it was saved at, so it opens right on a display of another size, as the
  overlay's windows do.
- Two factory layouts besides *Default*: **Wide** (the Hierarchy and File system as tabs on the left, for a laptop) and
  **Viewers** (the Model and Animation viewers in the middle, the File system on the left, the Inspector on the right,
  for looking through the disc's models and clips). Unity's other factory layouts add nothing Coney Studio needs.

## The project {#project}

### What the four editors do {#project-survey}

- **Unity:** a project is a folder; its `Assets` folder is what the Project window shows, beside a read-only
  `Packages` tree. Every asset gets a `.meta` file beside it with its id and import settings; the imported, derived data
  is cached in `Library`, which is never shared and is rebuilt from the sources.
- **Godot:** a project is a folder with `project.godot` at its root, and `res://` always names that folder; the
  FileSystem dock shows it and nothing else. Godot keeps derived import data in a hidden `.godot` folder and import
  settings in a `.import` file beside each asset. A file moved outside the editor breaks the references to it, so moves,
  renames and deletes belong in the FileSystem dock, which fixes the references and lists what a delete would break.
- **Unreal:** a project is a folder with a `.uproject` file; the Content Browser shows its `Content` folder, and every
  imported asset becomes a `.uasset` of Unreal's own format.
- **Roblox Studio:** a place is one file (`.rbxl`) or lives in the cloud, and its contents are a tree of objects in the
  place, not files. Teams that want files and version control use a third-party tool (Rojo) that syncs a folder into
  the place.
- **FiveM:** a resource is a folder with an `fxmanifest.lua` at its root naming its version, game, client, server and
  shared scripts and its files; the server is told to start it by name.

### Coney Studio's project {#project-coney}

A project is **a folder Studio creates**, Godot's model, because a folder of plain files is what a modder can share, put
under version control and read without Studio, which Roblox's one-file place is not.

- **The project is the root of the File system panel, and nothing outside it is reachable.** Paths inside a project
  are written `project:/levels/yard.layout`, as Godot writes `res://`: no absolute path, no `..` above the root, in
  any file the project saves. The only window onto the rest of the computer is the Import button's file picker, which
  reads a file and copies it in ([Importing](#import)).
- **The game's files are a second, read-only root, `game:/`.** It shows what the player's installed disc holds (levels,
  models, animations, textures, sounds, scripts: everything reachable through a `.CPAK` or WAD file), as Unity shows its
  read-only `Packages` beside `Assets`. A project refers to a game asset by its `game:/` name and never copies it, so a
  project carries no game data and can be shared ([LEGAL.md](repo:LEGAL.md#no-game-data)). Opening a `game:/` entry
  opens it in its viewer; what can be edited, and how a game asset becomes a project's own (an export to glTF on the
  player's machine, say), is the content page's ([#1058](issue:1058)).
- **Moves, renames and deletes happen in the File system panel**, Godot's rule, so Studio can fix the project's
  references to the file; a delete lists the files that refer to it and asks first, and goes to the system's recycle
  bin. A change made outside Studio (an editor saving a script, a texture repainted) is seen and the file is imported
  again.
- **Derived data is a cache**: what Studio makes from a project's files to load them quickly goes in `.studio/` at the
  project's root, which Studio rebuilds at any time and a shared project leaves out (Unity's `Library`, Godot's
  `.godot`).

The folder Studio makes for a new project:

```text
my-project/
  coney-project.toml     the manifest: name, version, the Coney version it was made with, the level Play starts in
  levels/                the project's levels (sandbox layouts today, Coney's own level format later)
  textures/              PNG images the levels use
  scripts/               the project's Lua scripts (once the mods runtime exists)
  .studio/               Studio's cache: never shared, rebuilt at will
```

The manifest's name and its script and resource fields are the Lua SDK page's ([#1057](issue:1057)), which decides how
far a project follows FiveM's resource folder; the formats of `levels/` are the content page's
([#1058](issue:1058)). The folders are where Studio puts what it creates and imports; a user may add others. A
project's first level format is the **sandbox layout** Coney loads today ([The layout format](../sandbox.md#the-layout-format)):
it already has shapes, textures, spawn points, targets, AI fighters, camera views and a sun, and Coney already plays one
with the player at a chosen spawn (`--play-level sandbox:NAME --spawn NAME`).

## Play mode {#play-mode}

### What the four editors do {#play-survey}

| | Unity | Godot | Unreal | Roblox Studio |
| --- | --- | --- | --- | --- |
| Start | Play button (Ctrl+P) | Run Project (F5) or the current scene (F6) | Play In Editor (Alt+P), in the viewport or a new window | Play (F5): the avatar at a spawn location; Play Here: in front of the camera; Run (F8): no avatar |
| Where it runs | the Game view, in the editor's process | a separate game process; the editor shows its live tree (the Remote tree) | in the editor, or a standalone process | the same viewport |
| Pause, step | Pause (Ctrl+Shift+P), Step (Ctrl+Alt+P) | Pause (F7) | Pause, Frame Skip | none in the toolbar |
| Leave the player's view | the Scene view stays live beside the Game view | none | Eject (F8): the editor camera, the game still running | Client/Server toggle |
| Edits made while playing | lost on Stop; the editor is tinted to warn | lost (they were made in another process) | lost, unless kept with *Keep Simulation Changes* in Simulate | lost: Stop resets every object |
| Several players | none built in | several instances | several clients | Server and up to eight clients (F7) |

All four agree on the one rule that matters: **Play never changes the edited scene**, and Stop puts everything back as
it was.

### Coney Studio's Play mode {#play-coney}

- **Play** (Ctrl+P, Unity) starts the project's level in the Game view with the player at the level's first spawn
  point, as Roblox's Play puts the avatar at a spawn location; **Play here** (Ctrl+Shift+Alt+P) puts him where the Scene
  view's camera looks, on the ground below it, as Roblox's Play Here does. Both realize what Coney does today: the play
  mode with `--spawn`, and the Entity editor's *Move to camera* and *Drop to ground*
  ([Entity editor](../debug-menu.md#entity-editor)). The player is Rembrandt unless the project's scripts pick another
  character; choosing him in Studio realizes the Player page's *Change character*.
- **The engine handles everything.** Play runs Coney's own play mode with the project's level: the same combat, AI,
  pathing, physics and camera as the game, at the fixed 1/30 s step, so the player can run round, fight the layout's
  fighters and climb what it holds. Nothing in Play is Studio's code.
- **Pause** (Ctrl+Shift+P) and **Step** (Ctrl+Alt+P) are Unity's keys for the Time page's pause and one fixed step; the
  time scale is on the toolbar while playing.
- **Stop** (Ctrl+P again, or the Stop button) ends Play and puts the edited level back exactly as it was before Play.
  Edits made while playing are lost, as in all four editors; while playing, the whole editor is **tinted** (Unity's
  Play mode tint) so nobody edits for nothing.
- **While playing**, the Hierarchy and Inspector show the running game, live, as the Entity inspector does, and the
  Inspector's edits go through the Entity editor's queue, between two steps and into the run log
  ([Entity editor](../debug-menu.md#entity-editor)). The debug panels work as in the game.
- **Eject** (F8, Unreal) leaves the player where he stands and flies the Scene view's camera through the running game,
  and F8 again takes the player back. It realizes the play mode's free camera (the Camera page's, which flies while the
  player stands still).
- **The mouse and keyboard** go to the game while the Game view has focus, and Escape gives them back to the editor
  (Unity's rule), so Escape is never the game's pause in Studio; the gamepads always go to the game, as they do with the
  overlay open.
- Play reads the project's files **as saved**, so what plays is what a player of the project would get; Play saves
  first when there are unsaved edits, and says so.

How the engine runs for Play (in Studio's process, as Unity, or as a child process, as Godot) is the technology page's
choice ([#1056](issue:1056)). This page asks only that Stop restores the edit state exactly and that the Game view's
frames are Coney's.

## The Scene view {#scene-view}

### The fly camera {#fly-camera}

All four editors fly the camera the same way: hold the **right mouse button** in the view, move the mouse to look,
**W A S D** to move along the view and sideways, **Q and E** to sink and rise. They differ on Shift: in Unity and Godot
Shift flies **faster**, in Roblox Studio it moves the camera **slower** for close work, and in Godot Alt is slower.
Unity, Godot and Unreal change the base speed with the mouse wheel while the right button is held.

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
| Middle drag | pan | Unity, Godot, Unreal |
| Mouse wheel | zoom towards the pivot | Unity, Godot |
| F | frame the selection | all four |
| Shift+F | keep the view locked on the selection while it moves | Unity |
| Pad 1 | fly as the world viewer's camera does | Coney's [world viewer](../running-viewers.md#the-world-viewer) |

Q to Y are tools only while the right button is up (Unity's rule), so Q and E fly while it is held. The fly camera
moves at the fixed step like everything else, so a scripted input flies the same path every time, as the world viewer
does.

### The keymap {#keymap}

**Unity's keymap**, so a Unity user's hands already know it; Godot and Unreal use the same letters for move, rotate
and scale, and the table says where they differ. Every key can be changed: the keymap is data, saved per user beside the
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
| F2 | rename | same | same | same | none |
| Ctrl+A | select all in the focused panel | same | same | same | same |
| Ctrl+S | save the open level | Save | Save scene | Save | publish or save |
| Ctrl+P, Ctrl+Shift+P, Ctrl+Alt+P | Play and Stop, Pause, Step | same | F5, F7 | Alt+P | F5, Shift+F5 |
| F8 | Eject in Play | none | none | Eject | Run |

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
- **Constant size on screen.** The gizmo is drawn the same size whatever the distance, scaled with the
  [UI scale](../debug-menu.md#ui-scale), and drawn over the scene without depth, so it is never hidden in a wall.
- **Facing the camera.** The plane squares sit in the quadrant towards the camera, and an axis that points nearly
  straight at the camera fades out, since a drag along it would be erratic (both Unity's).
- **Picking.** A click tests the gizmo's handles first, within a few pixels of each line, ring or cube, the nearest to
  the cursor winning; only a click that hits no handle selects something in the scene ([Selection](#selection)). So a
  handle in front of another object is always the one dragged.
- **Feedback while dragging.** The value being changed (a distance in metres, an angle in degrees, a scale factor)
  shows beside the cursor, and the Inspector's field changes with it.
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
| **Increment** | **Ctrl** held while dragging (Unity) | Move 0.25 m along each axis, Rotate 15°, Scale 0.1 of the size; each step set on the toolbar's snap menu, Move per axis or linked, as in Unity's snap increment menu |
| **Grid** | The toolbar's grid snap toggle | Moves land on the Scene view's grid (1 m, with 0.25 m lines up close) along the dragged axis, as Unity's grid snapping does |
| **Vertex** | **V** held | Drag a vertex of the selection onto a vertex of another model (Unity's vertex snapping); Shift+V keeps it on |
| **Surface** | **Ctrl+Shift** while dragging the move centre | The selection lands on the collision surface under the cursor (Unity's surface snapping); End drops it straight down ([the keymap](#keymap)) |

The steps are Coney's: a quarter metre is fine enough for the sandbox's courses, whose sizes are in tenths and quarters
of a metre ([The shipped layouts](../sandbox.md#the-shipped-layouts)), and 15° is the rotation step all of Unity,
Godot and Roblox Studio users meet.

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
| A sandbox shape | its `at=` | its `yaw=`: the vertical ring alone, the only turn the layout format has | its size arguments (`size=`, `radius=`, `height=`, `width=`); a sphere evenly only |
| A spawn point, a target, a fighter | its `at=` | its `heading=`: the vertical ring alone | none |
| A camera view | its `at=` | its `yaw=` and `pitch=` | none |
| A human, while playing | *Teleport* | *Heading*: the vertical ring alone | *Scale*, evenly ([Entity editor](../debug-menu.md#entity-editor)) |
| An object, while playing | none: the Entity editor takes no object edit today | none | none |

So the full gizmo (three rings, the free sphere, scale on any axis of anything) needs a level format that keeps a full
position, rotation and scale for every placed thing. The sandbox layout keeps a position, a yaw and sizes; Coney's own
level format is the content page's ([#1058](issue:1058)), and this page asks it to keep all three, so that every handle
of the Unity gizmo works on every placed thing. A human keeps one ring for good: Coney's humans have a heading, not a
free orientation. Coordinates and units are the game's: metres, degrees, z up
([The layout format](../sandbox.md#the-layout-format)).

### Selection {#selection}

What all four do, and Studio with them:

- A click in the Scene view selects what is under the cursor; a click on empty space clears the selection.
- **Shift+click** adds to the selection and **Ctrl+click** toggles one in or out (Unity); a **drag on empty space**
  draws a box that selects what it encloses (Godot, Unreal, Unity).
- **One selection** shared by every panel: choosing in the Hierarchy selects in the Scene view and fills the
  Inspector, and the reverse. In Play it is the Entity inspector's selection, so the Debug draw's highlight follows it.
- The Inspector shows the values a multiple selection shares and edits them all together; a value they differ in shows
  as mixed (Unity).
- A selected thing is outlined in the Scene view; what is under the cursor is lit before the click (Unity's and
  Unreal's pre-selection highlight).

### Undo {#undo}

- **Every edit is a command** with an inverse, kept on one undo stack per open level: a gizmo drag (one step, however
  long the drag), an Inspector value, a create, a duplicate, a delete. This is the model of the Entity editor, whose
  edits are already commands, queued, applied between steps, logged and replayable
  ([Entity editor](../debug-menu.md#entity-editor)); Studio's edit-time commands add the inverse.
- **Edit > Undo History** lists the stack, and choosing a line goes back to it (Unity's Undo History).
- Saving does not clear the stack; closing the level does.
- **Play has no undo**: an edit made while playing is lost on Stop anyway, and Stop is the undo.
- **File operations are not undone** (none of the four undoes them); a delete asks first and goes to the recycle bin.

## Composing a level {#composing}

Users compose Coney-compatible content in Studio the way they do in Unity, Roblox Studio and Godot: maps and levels from
primitives, textures applied, colliders, path points, the characters and things a level holds, and later custom
missions and models. This section says what each is in the four editors and what it is in Coney.

### What the four editors do {#composing-survey}

| Thing | Unity | Godot | Roblox Studio |
| --- | --- | --- | --- |
| Primitives | *GameObject > 3D Object*: cube, sphere, capsule, cylinder, plane, quad | a MeshInstance3D with a box, sphere, capsule, cylinder, plane or prism mesh; CSG shapes for blockouts | Parts: block, sphere, wedge, corner wedge, cylinder |
| Texturing | a material with a texture, dragged onto the object | a material on the mesh, a texture dragged onto it | a texture or decal on a face, a material and a colour per part |
| Colliders | collider components (box, sphere, capsule, mesh), added to an object or alone | a CollisionShape3D under a body | every part collides unless *CanCollide* is off |
| Path points | a baked NavMesh, with links between areas | a NavigationRegion3D baked, Path3D curves | PathfindingService over the parts, with modifiers and links |
| Where a new thing appears | at the Scene view's pivot | at the scene's origin | in front of the camera, on the surface |

### Coney Studio's Create menu {#create-menu}

*Create* (the menu, and a right-click in the Hierarchy or the Scene view) offers exactly the kinds a Coney level can
hold today, as Roblox's *Insert Object* offers Roblox's classes. A new thing appears where the camera looks, dropped on
the ground below (Roblox's rule, made of the Entity editor's *Move to camera* and *Drop to ground*), is selected, and
the Move tool is chosen.

| Create | What it makes | Realizes in Coney | State |
| --- | --- | --- | --- |
| **Primitive**: box, ramp, stairs, cylinder, capsule, sphere | A solid textured shape, sized in metres | The sandbox layout's shapes ([Shapes](../sandbox.md#shapes)), the Spawner page's objects | ready |
| **Spawn point** | Where the player can start; Play uses the first | A layout's `spawn` | ready |
| **Target** | A passive human to fight | A layout's `target` | ready |
| **AI fighter** | An AI human that fights back | A layout's `fighter` ([AI fighters](../sandbox.md#ai-fighters)) | ready |
| **Camera view** | A viewpoint, chosen from the Scene view's view menu | A layout's `view` | ready |
| **Path point** | A point of the level's path network, linked to others | Coney's AI pathing ([AI moving](../../research/ai-moving.md)) | waits on Coney's path overlays and a project path format ([#1058](issue:1058)) |
| **Character** of any type | A human of one of the game's character types, on a side | The AI fighters page's model picker and *Change character* ([pages](../debug-menu.md#pages)) | waits on a level format that places one (a layout's fighter is one fixed type) |

Primitives repeat as the layout's `repeat=` and `step=` do: the Inspector's *Repeat* field makes a row of copies, and
Ctrl+D duplicates one.

### Textures, colliders and paths {#composing-parts}

- **Applying a texture.** Drag a PNG from the File system onto a shape in the Scene view, or pick it in the Inspector:
  it becomes the shape's `texture=`, with `uv=` (tiles per metre) and `tint=` beside it
  ([Shapes](../sandbox.md#shapes)). A texture imported is a texture usable at once ([Importing](#import)).
- **Colliders.** Every primitive collides, as a Roblox part does, and the Inspector's *Collision* group holds its
  collider: solid or not (`solid=`), the collision material that decides footsteps and what a fall does (`material=`),
  the triangle flags (`flags=`) and the area byte (`area=`), all of them Coney's collision fields
  ([Collision](../../research/collision.md#triangles)). The Scene view's *Collision* view mode draws them, from the
  Debug draw's collision lines. Two kinds wait: a collider with nothing drawn (an invisible wall, Unity's lone collider)
  needs a layout option or Coney's level format ([#1058](issue:1058)), and a collider for an imported model waits on
  custom content ([Custom content](../../roadmap.md#custom-content)).
- **Path points.** Studio's design for them, for when Coney can draw and read a project's paths: a *Paths* mode of the
  Scene view shows the points and their links as lines on the ground; Create adds a point linked to the selected one,
  the Move gizmo places it (snapped to the ground), and *Link* (L) links or unlinks two selected points. The AI walks
  them with Coney's own path planning; Studio adds no path logic of its own.
- **Custom and customised models, custom missions.** They wait on what Coney lacks: models from outside the disc on
  custom content, a human's model changed in place on [#1053](issue:1053), missions on the mods runtime and a mission
  format of Coney's own. Each comes into Studio's Create menu and Inspector when Coney has it.

## Importing {#import}

### What the four editors do {#import-survey}

- **Unity:** drop files from the system onto the Project window, or *Assets > Import New Asset...*; each kind of file
  has an importer, its settings in the Inspector and a *Reimport*; a file changed on disk is imported again when the
  editor gets focus.
- **Godot:** drop files onto the FileSystem dock or copy them into the folder; each kind is imported by kind, its
  settings in the Import dock with a *Reimport* button; changes on disk are seen and imported again.
- **Unreal:** drop files onto the Content Browser or press its *Import* button; each file becomes a `.uasset` through an
  import dialog with its options; *Reimport* reads the source again.
- **Roblox Studio:** *Import 3D* opens the 3D Importer with a preview and settings; the Asset Manager imports images,
  meshes and sounds in bulk; files dropped onto the viewport are imported and placed.

### Coney Studio's import {#import-coney}

- **Drag and drop imports only into the File system panel** (Unity, Godot and Unreal; not Roblox's viewport drop). A
  file dropped on a folder goes into that folder; dropped on empty space, into the selected folder; dropped anywhere
  else (the Scene view, the Inspector), nothing is imported and the panel says where to drop it. One place for imports
  means one place to look for what came in.
- **Dragging a project file from the File system into the Scene view places it**, which is not an import: a texture
  dropped on a shape textures it, a level opens. This is how all four place assets.
- **The Import button** on the File system panel's toolbar, and *File > Import...*, open the system's file picker and
  import the chosen files into the selected folder (Unreal's button, Unity's menu). It is the alternative to
  drag-and-drop and the one window onto files outside the project.
- **By kind, automatically.** Studio picks the importer from the file's kind, with no dialog when the defaults serve;
  the file's import settings are shown in the Inspector when it is selected, with a **Reimport** button (Unity and
  Godot). The source file is copied into the project as it is; what Studio derives from it goes in the `.studio/` cache.
- **A name already taken** asks: replace the file, or keep both under a new name. A file of a kind Studio cannot use is
  refused with the list of the kinds it can; nothing is copied in "for later".
- **Changes on disk are imported again**: a file of the project saved by another program is seen and reimported, as in
  Unity and Godot; for a script that is where hot reload begins ([#1056](issue:1056)).

What can be imported is what Coney can load (rule 2), so the list grows with Coney:

| Kind | Becomes | State |
| --- | --- | --- |
| `.png` | A texture a level's shapes use | ready: a sandbox layout's `texture` statement ([Statements](../sandbox.md#statements)) |
| `.layout` | A level | ready: Coney's sandbox layout format |
| `.lua` | A script | waits on the mods runtime ([Script mods](../../roadmap.md#script-mods)) |
| `.gltf`, `.glb` | A map or a model | waits on custom content ([Custom content](../../roadmap.md#custom-content)) |
| `.wav` | A sound | waits on Coney loading sounds from outside the disc |

The content page ([#1058](issue:1058)) owns the formats themselves and their export.

## What Studio skips {#skipped}

| Feature | In | Why Coney Studio skips it |
| --- | --- | --- |
| An asset store or toolbox of other people's content | Unity, Unreal, Roblox | No server and no hosting; a store could carry game data, which no Coney service may ([LEGAL.md](repo:LEGAL.md#no-game-data)) |
| Visual scripting (Blueprints) | Unreal | The game's logic is Lua calling natives; scripting is the Lua SDK ([#1057](issue:1057)) |
| Components added freely to any object | Unity, Godot | Coney's entities are the kinds the game has (humans, objects, doors, panes) and the layout's statements; Studio's *Create* menu offers those, as Roblox's *Insert Object* offers Roblox's classes |
| Terrain and foliage tools | Unity, Unreal, Roblox | Coney's worlds are sector meshes with no terrain system |
| The Rect tool and an interface designer | Unity, Roblox | Coney has no interface a project can make |
| Prefabs and packages of placed things | Unity, Godot, Roblox | Waits on Coney's own level format, which decides whether it has them ([#1058](issue:1058)) |
| Keeping edits made in Play | Unreal (*Keep Simulation Changes*) | Stop restoring everything is the rule all four share and the simplest to trust; the Entity editor's log could replay them later if users ask |
| Running without a player | Roblox (Run), Unreal (Simulate) | The game's scripts and play mode expect a player; Eject gives the same view of a running level |
| Testing with several players | Roblox, Unreal, Godot | Waits on Coney's co-op ([Network architecture](../network-architecture.md#phases)) |
| Team Create, cloud places | Roblox | No server; a project is a folder, shared with git or any other way |
| One keymap per editor | Unreal, Godot (presets) | One keymap, rebindable, is one set of keys to document and test |

## Proposed build issues {#build-issues}

In the order they should come. Each realizes a feature Coney has today; the issues for what waits on Coney (the script
editor, paths, a mission editor, lighting for game levels, several players) are written when Coney gains it, from the
roadmap the parent issue asks for. The technology page ([#1056](issue:1056)) comes before the first, since it picks the
library and the program.

1. **tools: Add the Coney Studio shell with projects and layouts.** Studio starts, makes a new project folder with its
   manifest and the folders above, or opens one, and remembers the recent ones. Its panels dock, tab, float and resize;
   *Window > Layouts* saves, switches, deletes and resets to the built-in *Default*, *Wide* and *Viewers* layouts, per
   user, at any UI scale. Tests: a project made and opened again with no window; a layout saved and loaded round-trips.
2. **tools: Add the File system panel rooted at the project.** The project as a tree under `project:/`, with the game's
   files read-only under `game:/`; nothing outside the two is reachable, and no saved path is absolute or climbs above
   the root. Moves, renames and deletes fix the references and ask first; changes made on disk are seen. Tests: a path
   that leaves the root is refused; a rename updates a level that refers to the file.
3. **tools: Add the Scene view with the fly camera and Unity's keymap.** A sandbox layout or a level's world drawn as
   the world viewer draws it, flown with the right mouse and W A S D, Q and E, Shift faster, Alt slower, the wheel for
   speed, orbit, pan, zoom, F and Shift+F; pad 1 flies it too. The keymap is data, rebindable and saved per user. The
   Debug draw and mesh views are its view modes. Tests: a scripted input flies the same path every run; every key of
   the table is bound by default.
4. **tools: Add the Hierarchy and Inspector with one selection.** The level's entities by kind, one selection shared by
   every panel (click, Shift and Ctrl, box), the merged inspector of [#1053](issue:1053), values shared by a multiple
   selection, and the pre-selection highlight. Tests: a selection made in one panel shows in the others; a multiple
   selection edits all.
5. **tools: Add Unity's transform gizmo and undo to the Scene view.** The Move, Rotate, Scale and Transform tools
   (W, E, R, Y) with every handle of Unity's gizmo, its colours, yellow highlight, constant screen size and handle-first
   picking; the Pivot / Center (Z) and Global / Local (X) toggles; increment, grid, vertex and surface snapping with the
   toolbar's steps; drop to ground, move to view; axes the selection cannot take drawn grey; every drag one command on
   an undo stack with its history. Tests: a scripted drag of each handle gives the expected value, snapped and not;
   every edit undoes to the same state.
6. **tools: Add composing levels to Coney Studio.** The Create menu's primitives, spawn points, targets, AI fighters and
   camera views, placed where the camera looks and dropped to the ground; repeat and duplicate; textures applied by
   drag or in the Inspector with tiling and tint; each primitive's collider in the Inspector's Collision group and the
   Collision view mode; delete; saving the level as a sandbox layout. Tests: a level composed by script saves a layout
   that Coney loads and plays; a saved level reopens unchanged, byte for byte.
7. **tools: Add Play mode to Coney Studio.** Play, Play here, Pause, Step and Stop with Unity's keys; the level plays in
   the Game view with the player at its spawn or the camera's spot, the editor tinted; Stop puts the edited level back
   exactly; the Hierarchy and Inspector follow the running game and edit it through the Entity editor's queue; Eject
   flies the free camera. Tests: the level after Stop equals the level before Play; a scripted Play reaches the spawn
   point and a fight with a layout's fighter.
8. **tools: Add drag-and-drop import and the Import button.** Drops land only in the File system panel (into the folder
   under the cursor or the selected one) and nowhere else; the Import button and *File > Import...* do the same through
   a file picker; PNG and `.layout` import by kind with their settings in the Inspector and Reimport; name clashes ask;
   other kinds are refused with the list. Tests: a drop outside the panel imports nothing; a PNG dropped on a folder is
   there and a shape can use it.
9. **tools: Add the debug menu as Coney Studio panels.** Studio as the third front end of the debug model: every page
   (Console with the run log and Lua line, Natives, Tunables, Player, Camera, Spawner, AI fighters, HUD, Display, Audio,
   Network) as a panel, with no Studio code per page. Tests: every page of the session draws as a panel, as the overlay
   test checks for the overlay.
10. **tools: Add the model, animation and texture viewers to Coney Studio.** A `game:/` model opens in the Model viewer
   (shaded, wireframe, by material, normals, bone axes, its Mesh group counts), a clip in the Animation viewer (the
   Animation player's sets, loop, speed, in place) and a texture dictionary in the Texture viewer; the *Viewers* layout
   arranges them. Tests: each viewer opens a synthetic fixture with no disc; a disc check prints counts only.

## Sources {#sources}

The editors' own documentation, read for this page:

- Unity: [Scene view navigation](https://docs.unity.com/en-us/engine/current/manual/unity-editor/editor-windows-views-reference/using-the-scene-view/scene-view-navigation),
  [Positioning GameObjects](https://docs.unity.com/en-us/engine/current/manual/working-with-scenes/scenes-manage-gameobjects/positioning-game-objects)
  (the tool keys and snapping).
- Godot: [Introduction to 3D](https://docs.godotengine.org/en/stable/tutorials/3d/introduction_to_3d.html) (freelook and
  the tool keys), [File system](https://docs.godotengine.org/en/stable/tutorials/scripting/filesystem.html) (`res://`,
  moving files), [Customizing the interface](https://docs.godotengine.org/en/stable/tutorials/editor/customizing_editor.html)
  (docks and editor layouts).
- Unreal: [Viewport controls](https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine).
- Roblox Studio: [Studio interface](https://create.roblox.com/docs/studio/ui-overview),
  [Testing modes](https://create.roblox.com/docs/studio/testing-modes).
- FiveM: [Creating your first script](https://docs.fivem.net/docs/scripting-manual/introduction/creating-your-first-script/)
  (the resource folder, its manifest, `refresh` and `restart`).
