# Coney Studio: the editor experience

The design of how Coney Studio looks and feels to the person using it: its words, its panels and their default layout,
the layouts a user saves, the project folder, a new level, Play mode, importing files and previews. Moving round a level
and building it (the fly camera, the keymap, the gizmo, selection, undo and composing) are on [The Level view, gizmos
and composing](level-view.md). **Status: design; nothing here is built.** What Studio is, and the other design pages,
are on [Coney Studio](index.md); the GUI library, the program and how Play and live edits run inside it are the
[technology](technology.md) page's.

The design comes from four editors people already know: Unity, Godot, Unreal and above all **Roblox Studio**, the
closest model, because it too is an editor for one game's content rather than a general engine. FiveM is the model for
resources and scripts, which the [Lua SDK page](lua-sdk.md) owns. Each section says what the four do, then what Coney
Studio copies and what it skips, and why. Two rules decide every choice:

1. **Familiar first.** Where the editors agree, Studio does the same, so a Unity, Godot or Unreal user is at home in a
   minute. Where they differ, Studio takes **Unity's** keys and words (the maintainer's choice) and Roblox Studio's
   model of a game-specific editor.
2. **Only what Coney has.** Each panel and tool realizes a feature Coney has today, and says which. A feature that needs
   something Coney lacks is listed with what it waits on, and is not in Studio until Coney has it ([What Studio
   is](index.md#principles)).

**Studio builds levels; it does not make models or animations.** What Blender and the other modelling tools do
(modelling, sculpting, rigging, animating, painting textures) Studio leaves to them. Studio imports what they make and
is where the user builds maps: placing, arranging, sizing and turning things, setting their collision, applying textures
and laying out spawn points and paths. What can be edited in Studio depends on the kind of asset and how it came in
([Importing](#import)); the model, animation and texture viewers only look.

## Contents {#contents}

- [Words](#words): level, Level view, prefab
- [The panels](#panels), [the Hierarchy's tree](#hierarchy-tree), [the default layout](#default-layout) and
  [layouts](#layouts), [look and theme](#theme)
- [The project](#project): the folder, its layout and the game's files
- [A new level](#new-level): the blank slate and the templates
- [Play mode](#play-mode) and [live edits while playing](#live-edits)
- [The Level view](level-view.md#level-view): the fly camera, [the keymap](level-view.md#keymap),
  [gizmos](level-view.md#gizmos), [selection](level-view.md#selection) and [undo](level-view.md#undo)
- [Composing a level](level-view.md#composing): primitives, textures, colliders and path points
- [Importing](#import)
- [Previews](#previews) of models, animations and sounds
- [What Studio skips](#skipped)
- [Proposed build issues](#build-issues)
- [Sources](#sources)

## Words: level, Level view, prefab {#words}

### What the editors call the world {#words-survey}

| | Unity | Godot | Unreal | Roblox Studio |
| --- | --- | --- | --- | --- |
| What a user builds and plays | a **scene** (`.unity`) | a **scene** (`.tscn`), which is also any reusable tree: a character, a door, a whole level | a **level**, also called a map (`.umap`) | a **place** (`.rbxl`), whose world is the **Workspace** tree |
| The view that edits it | Scene view | 3D viewport | Level viewport | viewport |
| A reusable group of things | a **prefab**, with overrides per instance and variants | a scene **instanced** inside another (nested scenes) | a Blueprint actor, a Level Instance | a **model**, shared between places as a **package** |
| A thing in the tree | a GameObject | a node | an actor | an instance |
| A group with no content of its own | an empty GameObject: its children follow its transform | a plain Node3D | a folder in the Outliner (no transform) | a model or a folder |

### Coney Studio's words {#words-coney}

**A maintainer decision: Studio says *level* wherever the other editors say *scene*.** What a user builds is a
**level**, the view that edits it is the **Level view** (Unity's Scene view), and every word made from it follows: a new
level, the level's tree, a level edit. The running game stays the **Game view**, as in Unity. The reason is *The
Warriors*' own conventions:

- The game is made of levels. The original loads a level (`InitLevel` and its `.lev` file, [Level
  loading](../../research/level-loading.md)), each mission is a level ([Levels](../../references/levels.md)), Coney
  plays one with `--play-level`, and the scripts are level scripts. It is also Unreal's word, so it is familiar.
- In the game, a **scene** is something else: an in-engine cutscene ([Scenes](../../research/scenes.md)), which Studio
  shows too. Studio's interface says *cutscene* for those, so neither word means two things.

The other words:

- **Entity** for a thing in the level's tree: Coney's existing word, as in the [Entity
  inspector](../debug-menu.md#entity-inspector) and the entity panel that [#1053](issue:1053) merges from the inspector
  and the editor. A primitive, a spawn point, a fighter, a camera view and a path point are all entities. Every entity
  has a **name** and **tags** in the Inspector, which is how scripts find it. A script is never an entity and is never
  attached to one: scripts are files in the project's `scripts/` folder, which the project's manifest runs ([Projects
  and the manifest](lua-sdk.md#manifest)).
- **Group** for an empty entity that only holds others (Unity's empty GameObject): it has a position, rotation and
  scale, and its children follow it.
- **Prefab** for a reusable group (Unity's word, the best known of the four): any mix of entities saved as one file of
  the project, say an ambush of five fighters, a spawn point and crates, tagged `ambush` so the project's script can
  spring it, placed in a level as many times as wanted. Each placed copy (an *instance*) follows the file, and the
  values a copy changes are kept as its overrides.

## The panels {#panels}

### What the four editors have {#panels-survey}

All four are built round the same five things: a **3D view** of the world being edited, a **tree** of what the world
holds, a **property sheet** of what is selected, a **file browser** of the project's assets and an **output log**. They
name them differently:

| Job | Unity | Godot | Unreal | Roblox Studio |
| --- | --- | --- | --- | --- |
| The 3D view, edited | Scene view | 3D viewport | Level viewport | 3D viewport |
| The game, running | Game view | a separate game process, shown in the editor's Game workspace since 4.4 | Play In Editor viewport or window | the same viewport, in a playtest |
| The world's tree | Hierarchy | Scene dock | Outliner | Explorer |
| What is selected | Inspector | Inspector | Details | Properties |
| The project's files | Project window | FileSystem dock | Content Browser | Asset Manager, Toolbox |
| Import settings | the Inspector, for an asset | Import dock | the asset editor | the 3D Importer's dialog |
| The log | Console | Output | Output Log | Output |
| A line of code to run | none built in | none built in | Console command line | Command Bar |

Coney Studio uses **Unity's names** (rule 1), but for *level* ([Words](#words-coney)), with **File system** for the file
browser (the maintainer's word, and Godot's) and **Inspector** for the property sheet, as Unity and Godot both call it.

### Coney Studio's panels {#panel-list}

Each panel and the Coney feature it realizes. A panel marked *waits* is designed here but is not in Studio until Coney
has what it names.

| Panel | Its job | Realizes in Coney | State |
| --- | --- | --- | --- |
| **Level view** | The level being edited, seen through the fly camera; selection, gizmos, and the debug views as view modes | The [world viewer](../running-viewers.md#the-world-viewer), the [sandbox](../sandbox.md)'s free camera, [Debug draw](../debug-menu.md#debug-draw) and the [mesh views](../debug-menu.md#mesh-views) | ready |
| **Game view** | The game running in Play mode, with Play, Pause, Step and Stop | The play mode (`--play-level`, a sandbox with `sandbox:NAME`) and the debug menu's Time page (pause, step, time scale) ([pages](../debug-menu.md#pages)) | ready |
| **Hierarchy** | The level's entities as a free tree in the user's order, with parenting, prefabs and a kind filter; choosing one selects it everywhere ([The Hierarchy's tree](#hierarchy-tree)) | The [Entity inspector](../debug-menu.md#entity-inspector)'s entity list (player, targets, AI humans, doors, panes, objects) and a sandbox layout's statements | ready |
| **Inspector** | The selection's name and tags, its values, live, and the edits it takes; for a file, its details and import settings | The merged Entity inspector and Entity editor ([#1053](issue:1053)) | ready once [#1053](issue:1053) merges |
| **File system** | The project folder as a tree, and the game's files read-only; drag-and-drop and the Import button land here | Coney's asset loading and the [install folder](../asset-and-script-architecture.md#install-folder) | ready |
| **Console** | The run log's lines, filtered by level and system, and a Lua line to run | The [run log](../run-log.md) and the [Lua console](../debug-menu.md#lua-console) (expressions and assignments; full Lua waits on [the Lua 4.0 compiler](lua-sdk.md#compiler)) | ready |
| **Natives** | Every native with its status, signature and an argument editor to call it | The debug menu's [Natives](../debug-menu.md#natives) page and the [masterlist](../../references/bindings/index.md) | ready |
| **Model viewer** | One model on its own, to look at (never to edit): shaded, wireframe, by material, normals and bone axes, its counts | The [character viewer](../running-viewers.md#the-character-viewer), the [mesh views](../debug-menu.md#mesh-views) and the [Mesh group](../debug-menu.md#mesh-group) | ready |
| **Animation viewer** | Any clip on a model: play, loop, speed, in place, by set and category | The [Animation player](../debug-menu.md#animation-player) | ready; a scrubbing timeline waits on a seek in the animation player |
| **Texture viewer** | A texture dictionary or sprite sheet, image by image | The texture and sprite sheet viewers ([viewers](../running-viewers.md#viewing-texture-dictionaries)) | ready |
| **Debug panels** | Every page of the debug menu that is not a panel above, each as a panel: among them Tunables, Player, Camera, Spawner, AI fighters, Levels, Missions, Movies, Cheats, HUD, Display, Audio and Network ([pages](../debug-menu.md#pages)) | The debug menu's model, which any front end renders ([One model](../debug-menu.md#one-model)) | ready |
| **Level settings** (in the Inspector, with the level selected) | The level's sky, sun, ambient light, fog, occlusion and shadows ([The Hierarchy's tree](#hierarchy-tree)) | A sandbox layout's `sky`, `sun`, `ambient`, `fog`, `occlusion` and `shadows` statements ([Statements](../sandbox.md#statements)) | ready for sandbox layouts; a game level's lights and a sun that changes sneaking wait on the [realtime lighting](../realtime-lighting.md) enhancement |
| **Script editor** | A project's Lua scripts, with VS Code's keys, completions from the masterlist and live errors | The mods runtime ([Script mods](../../roadmap.md#script-mods)) | waits on the mods runtime; the editor is the [technology](technology.md#code-editor) page's |
| **Paths** | A level's path network, drawn and edited | Path overlays from the level's path data ([Still to come](../debug-menu.md#still-to-come)) | waits on Coney's path overlays and a project path format ([#1058](issue:1058)) |
| **Mission editor** | A mission's checkpoints and objectives | Coney's level scripts ([Scripts](../asset-and-script-architecture.md#scripts)) | waits on the mods runtime and a mission format of Coney's own |

The **Debug panels** row is the cheapest and the most important: the debug menu is already one model drawn by two front
ends (the pad menu and the Dear ImGui overlay), and Studio becomes a **third front end** of the same model. A page added
to the debug menu then appears in Studio with no Studio code, which is how Studio keeps up with Coney by construction
for everything the debug menu shows.

### The Hierarchy's tree {#hierarchy-tree}

A maintainer decision: **the Hierarchy is the level, and works like Unity's.** At its top is a free list of entities, in
the order the user gives them; any entity can be dragged onto any other to become its child, and a child follows its
parent's position, rotation and scale (a fighter under a moving group moves with it). No folder is fixed and none
refuses a kind: an ambush group may hold fighters, a spawn point and crates together.

```text
yard                              the level; selected, the Inspector shows the level's settings
  Ground                          a box
  Alley                           a group
    Fence 1.2 m                   a box, a child: it follows the group
    Fence 1.6 m
    Crate                         an object
  start                           a spawn point
  Ambush (prefab)                 a prefab instance: five fighters, a spawn point, crates; tag ambush
    Fighter 1 ... Fighter 5
  Ambush (prefab)                 the same prefab placed again, its position overridden
  Rooftop view                    a camera view
  Game level: level80             a game level this one builds on, read-only (waits on Coney's own level format)
    World                         the two streamed worlds' parts
    Level file                    its collision, paths, sky and occluders
    Objects                       the Object List: doors, glass, world objects, spawners
    Zones and volume boxes
    Lights
    Level starts
```

*Figure: a level in the Hierarchy. While playing, the Running tab shows the running game's entities beside it ([Play
mode](#play-coney)).*

- **The level's own settings are on the level, in the Inspector.** Selecting the level's row (the tree's top) shows its
  sky, sun, ambient light, fog, occlusion and shadows in the Inspector, as Roblox shows its Lighting service in the
  Properties panel. Studio keeps no Lighting window apart (Unity has one): the Inspector is where every value is edited,
  so a user looks in one place, and the settings belong to the level, so they are saved with it.
- **Sorting by kind is a filter.** The Hierarchy's search box takes a name or a kind (`t:spawn` shows only spawn points,
  `t:fighter` only fighters, as Unity's `t:` search does), and its filter menu has one entry per kind; the tree shows
  the matches with their parents greyed for context.
- **Create** from an entity's right-click makes the new entity its child (once the level format keeps parents; in a
  sandbox layout it is a top-level entity); from empty space, a top-level entity ([The Create
  menu](level-view.md#create-menu)). Ctrl+Shift+G puts the selection under a new group (Unity's *Create Empty Parent*),
  and *Ungroup* in the right-click menu moves a group's children up and deletes it.
- **A prefab instance** is one row that opens to its entities, marked as a prefab; a value an instance changes is marked
  in the Inspector as an override, with *Revert* and *Apply to prefab* (Unity's). Double-clicking the row's prefab icon
  opens the prefab on its own to edit, and every instance follows.
- **A game level** loaded as the base of a project level is a read-only branch at the bottom of the tree: to fly through
  in the Level view as the [world viewer](../running-viewers.md#the-world-viewer) does and to examine, never to change.
  Its rows are the parts the original's level is made of: the streamed **World** ([The streamed
  world](../../research/world.md)), the **Level file** with its collision, path data, sky and occluders ([The level
  file](../../research/level-loading.md#the-level-file)), the **Objects** of the Object List with the doors, glass,
  world objects and spawners ([Objects](../../references/objects.md), [Spawner
  states](../../references/spawner-states.md)), the [zones](../../references/zones.md) and [volume
  boxes](../../references/boxes.md), the [lights](../../references/lights.md) and the [level
  starts](../../references/level-starts.md). Which of these Studio can list without playing is the content page's to
  settle ([#1058](issue:1058)); building on a game level waits on Coney's own level format and the mods runtime.

#### What the level format must store {#level-format-needs}

This Hierarchy needs a level format that keeps, for every entity: a stable id (the network checklist's ids, not
pointers, [Network architecture](../network-architecture.md#checklist)), its name, its kind and that kind's values, its
**parent** and its place among its siblings, and a **full local position, rotation and scale**; for every prefab
instance, the prefab file it comes from and its **overrides**; for the level, its settings and the game level it builds
on, if any. The sandbox layout keeps none of the parenting, prefabs or full rotation: it is a flat list of statements
with a position and a yaw. So until Coney's own level format exists ([#1058](issue:1058), told the same), a sandbox
layout opens in the Hierarchy as a flat list, one entity per statement and its settings on the level; parenting, groups
and prefabs are offered once the format holds them, and the layout's `repeat=` is its one way to make copies.

## The default layout {#default-layout}

### What the four editors do {#layout-survey}

Every editor puts the 3D view in the middle and the property sheet on the right. They differ on the file browser: Unity
(its *Default* layout) and Unreal put it along the bottom under the view, Godot puts its FileSystem dock at the lower
left under the Scene tree, and Roblox Studio puts Explorer and Properties together on the right.

### Coney Studio's default {#layout-default}

Coney Studio takes **Godot's arrangement**, as the maintainer asked for Studio: the **Inspector on one side and the File
system on the other**, so the two panels used most while building a level are both always in view, and the Hierarchy
sits over the File system, as Godot's Scene dock sits over its FileSystem dock. The middle holds **two views side by
side** (Unity's *2 by 3* layout has both in view too, one above the other): the **Level view** (fly round, select and
move things with the gizmos) and the **Game view** (the game running), so an edit made in the Level view while playing
is seen at once in the Game view ([Live edits](#live-edits)). The Console and Natives share the bottom.

```text
+-----------------------------------------------------------------------------------------------+
| File  Edit  Create  View  Play  Window  Help                                                  |
| [Q][W][E][R][Y]  Pivot | Global  Snap           [ Play ][ Pause ][ Step ]     Layout: Default v |
+----------------+------------------------------+------------------------------+----------------+
| Hierarchy      | Level                        | Game                         | Inspector      |
|  yard          |                              |                              |  Identity      |
|    Ground      |  the level, seen by the      |  the game running, seen by   |  Transform     |
|    Alley       |  fly camera, with gizmos     |  the game's own camera       |  Collision     |
|    start       |                              |                              |  ...           |
+----------------+                              |                              |                |
| File system    |                              |                              |                |
|  project:/     +------------------------------+------------------------------+                |
|   levels/      | Console | Natives                                             |                |
|   textures/    |  [info] level loaded ...                                    |                |
|  game:/ (read) |                                                             |                |
+----------------+-------------------------------------------------------------+----------------+
```

*Figure: the default layout. Outside Play the Game view shows the level from its first spawn point, as Unity's Game view
shows its scene's camera. The Model, Animation and Texture viewers open as tabs over the Level view; the debug panels
open where the user last docked them, or floating.*

The toolbar holds the tools (Q to Y, [the keymap](level-view.md#keymap)), the gizmo's pivot and space toggles, snapping,
the Play controls in the middle, as in Unity, and the layout menu on the right, as in Unity's toolbar.

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

- Every panel docks to any side, tabs with others or floats over the main window; every split resizes. A panel torn
  out into a window of its own waits on Dear ImGui's multi-viewport, which the technology page leaves off at first
  ([Choice: Dear ImGui with docking](technology.md#gui-choice)).
- **Window > Layouts** and the toolbar's layout menu: *Save layout as...* (a name), the saved layouts to switch to,
  *Delete*, and *Reset to Default*. *Default* is built in and always comes back on reset (Godot's rule).
- A layout is **per user, never per project**: it lives in Studio's settings folder, as Coney's overlay keeps its
  windows in `coney-imgui.ini` beside the tunables file ([The developer
  overlay](../debug-menu.md#the-developer-overlay)), so a project shared with someone else never changes their screen.
  Studio reopens with the layout it closed with.
- A layout saves which panels are open, where, their sizes and floating windows' places, at the [UI
  scale](../debug-menu.md#ui-scale) it was saved at, so it opens right on a display of another size, as the overlay's
  windows do.
- Two factory layouts besides *Default*: **Wide** (the Level and Game views as tabs in the middle and the Hierarchy and
  File system as tabs on the left, for a laptop's screen) and **Viewers** (the Model and Animation viewers in the
  middle, the File system on the left, the Inspector on the right, for looking through the disc's models and clips).
  Unity's other factory layouts add nothing Coney Studio needs.

## Look and theme {#theme}

The maintainer asks for a modern look that the user can change (colour scheme, accent and fonts), modelled on Godot's
new editor theme. The technology page chose Dear ImGui with one file of design tokens shared with the launcher
([Choice: Dear ImGui with docking](technology.md#gui-choice)); this section sets the tokens' values and the settings a
user changes.

### What Godot's editor uses {#theme-godot}

Godot 4.6's editor theme, read from its source (`editor/settings/editor_settings.cpp`,
`editor/themes/editor_theme_manager.cpp`, tag `4.6-stable`):

| Setting | Godot's default | What it does |
| --- | --- | --- |
| Style | *Modern* (or *Classic*, the older look) | Modern came from a community theme: neutral grey in place of the old blue, fewer lines, softer corners |
| Colour preset | *Default*: base `#292929` (0.161 grey), accent `#569EFF` | Also *Gray*, *Light*, *Black (OLED)*, *Breeze Dark*, *Solarized* dark and light, *Godot 2*, *Godot 3* (the old blue `#363D4A` with accent `#70BAFA`), *Custom* |
| Base colour, accent colour | as the preset | Every surface is derived from the base; the accent marks what is pressed, hovered or selected |
| Contrast | 0.3 (range −1 to 1; the light presets use −0.06) | How far the derived surfaces move from the base: darker when positive, lighter when negative |
| Corner radius | 4 px (Classic 3; range 0 to 6) | Rounding of panels, buttons and fields |
| Border size | 0 px (range 0 to 2) | Lines round panels; *Black (OLED)* turns extra borders on |
| Spacing preset | *Default*: base 4 px, additional 0 (*Compact* 2 and 2, *Spacious* 6 and 2) | Padding and gaps throughout |
| Interface font | **Inter**, 14 px (range 8 to 48) | SIL Open Font License 1.1 |
| Code font | **JetBrains Mono**, 14 px (range 8 to 48) | SIL Open Font License 1.1 |
| Follow the system | off; also *use the system's accent colour* | Picks the light or dark preset, and the accent, from the operating system |

The technology page already chose the same two fonts for Studio
([Choice: Dear ImGui with docking](technology.md#gui-choice)). Both are under the SIL Open Font License 1.1, which Godot
ships with their licence text in `thirdparty/fonts`; a maintainer approved shipping both with Coney Studio, with their
licence files alongside ([Maintainer decisions](index.md#decisions)).

### Coney Studio's defaults {#theme-defaults}

Studio's default theme is Godot's *Modern* with its *Default* preset, so a Godot user sees a familiar editor:

| Token | Default |
| --- | --- |
| Base colour | `#292929` |
| Accent colour | `#569EFF` |
| Contrast | 0.3 |
| Corner radius | 4 px |
| Border size | 0 px |
| Spacing | base 4 px, additional 0 |
| Interface font | Inter Regular and Bold, 14 px |
| Code font | JetBrains Mono Regular, 14 px |

Every size is multiplied by the [UI scale](../debug-menu.md#ui-scale), as the overlay's are, so the theme looks the
same at 1080p and at 4K. The Play mode tint ([Play mode](#play-coney)) is the accent at low opacity over the window's
frame.

### What a user can change {#theme-settings}

*Edit > Preferences > Theme* holds Godot's settings, fewer of them where one does nothing in Studio:

- **Preset**: *Default*, *Gray*, *Light*, *Black (OLED)* and *Custom*; choosing a colour by hand switches to *Custom*.
- **Base colour**, **accent colour** and **contrast** (−1 to 1).
- **Corner radius** (0 to 6 px) and **spacing** (*Compact*, *Default*, *Spacious*).
- **Interface font** and **code font**: the shipped Inter and JetBrains Mono, or a TTF or OTF file of the user's own,
  read from where it is and never copied into a project; each with a **size** (8 to 48 px).
- **Follow the system** (light or dark) and **use the system's accent colour**.

The settings are **per user**, in Studio's settings folder beside the [layouts](#layouts-coney) and the keymap, never in
a project, so a shared project never changes anyone's look. *Reset to Default* brings Godot's Modern values back.

### How the settings map onto Dear ImGui {#theme-imgui}

Studio builds Dear ImGui's style from the tokens, as Godot builds its theme from the same few settings:

| Setting | Dear ImGui |
| --- | --- |
| Base colour | The window, child and popup backgrounds (`ImGuiCol_WindowBg`, `ChildBg`, `PopupBg`, `DockingEmptyBg`) |
| Contrast | The other surfaces, the base moved towards black or white by multiples of the contrast, as Godot derives its contrast colours: frames, title bars, headers, tabs, scrollbars, separators and borders (`FrameBg`, `TitleBg`, `Header`, `Tab`, `ScrollbarBg`, `Separator`, `Border`) |
| Accent colour | What is active, hovered or chosen: `ButtonActive`, `HeaderHovered`, `TabSelected`, `CheckMark`, `SliderGrab`, `DockingPreview`, `NavCursor`, and `TextSelectedBg` at reduced opacity |
| Text colour | White or black by the base colour's luminance, as Godot picks its mono colour, with disabled text at reduced opacity |
| Corner radius | `WindowRounding`, `ChildRounding`, `FrameRounding`, `PopupRounding`, `TabRounding`, `GrabRounding`, `ScrollbarRounding` |
| Border size | `WindowBorderSize`, `FrameBorderSize`, `PopupBorderSize`, `TabBorderSize` |
| Spacing | `WindowPadding`, `FramePadding`, `ItemSpacing`, `ItemInnerSpacing`, `CellPadding` from the base spacing, plus the additional spacing |
| Fonts and sizes | The font atlas: Inter (and the icon font merged into it) as the default font and JetBrains Mono as the code font, loaded at size times UI scale; a change rebuilds the atlas at the next frame, as a change of UI scale does |

A change applies at the next frame, so the user sees the theme change as they drag a colour.

## The project {#project}

### What the four editors do {#project-survey}

- **Unity:** a project is a folder; its `Assets` folder is what the Project window shows, beside a read-only `Packages`
  tree. Every asset gets a `.meta` file beside it with its id and import settings; the imported, derived data is cached
  in `Library`, which is never shared and is rebuilt from the sources.
- **Godot:** a project is a folder with `project.godot` at its root, and `res://` always names that folder; the
  FileSystem dock shows it and nothing else. Godot keeps derived import data in a hidden `.godot` folder and import
  settings in a `.import` file beside each asset. A file moved outside the editor breaks the references to it, so moves,
  renames and deletes belong in the FileSystem dock, which fixes the references and lists what a delete would break.
- **Unreal:** a project is a folder with a `.uproject` file; the Content Browser shows its `Content` folder, and every
  imported asset becomes a `.uasset` of Unreal's own format.
- **Roblox Studio:** a place is one file (`.rbxl`) or lives in the cloud, and its contents are a tree of objects in the
  place, not files. Teams that want files and version control use a third-party tool (Rojo) that syncs a folder into the
  place.
- **FiveM:** a resource is a folder with an `fxmanifest.lua` at its root naming its version, game, client, server and
  shared scripts and its files; the server is told to start it by name.

### Coney Studio's project {#project-coney}

A project is **a folder Studio creates**, Godot's model, because a folder of plain files is what a modder can share, put
under version control and read without Studio, which Roblox's one-file place is not.

- **The project is the root of the File system panel, and nothing outside it is reachable.** Paths inside a project are
  written `project:/levels/yard.layout`, as Godot writes `res://`: no absolute path, no `..` above the root, in any file
  the project saves. The only window onto the rest of the computer is the Import button's file picker, which reads a
  file and copies it in ([Importing](#import)).
- **The game's files are a second, read-only root, `game:/`.** It shows what the player's installed disc holds (levels,
  models, animations, textures, sounds, scripts: everything reachable through a `.CPAK` or WAD file), as Unity shows its
  read-only `Packages` beside `Assets`. A project refers to a game asset by its `game:/` name and never copies it, so a
  project carries no game data and can be shared ([LEGAL.md](repo:LEGAL.md#no-game-data)). Opening a `game:/` entry
  opens it in its viewer; what can be edited, and how a game asset becomes a project's own (an export to glTF on the
  player's machine, say), is the content page's ([#1058](issue:1058)).
- **Export** (*File > Export project...*) packs the finished project into one `.cmod` file (a working name) for Coney's
  mods folder: the manifest, the levels, the scripts and the project's own assets, never `.studio/` and never a `game:/`
  file, which a player's own disc supplies. Export saves first, checks that the manifest's level and scripts exist, and
  says what it left out and why. The package's layout is the content page's ([#1058](issue:1058)). The Lua SDK page
  installs a project as a folder in `mods/` ([The mods folder](lua-sdk.md#mods-folder)); whether the loader reads a
  `.cmod` as it is or a player's install unpacks it into that folder is for that page to settle, and Export waits on it.
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
  coney-project.toml     the manifest: the project's name, version, SDK level, the level Play starts in, its scripts
  levels/                the project's levels (sandbox layouts today, Coney's own level format later)
  textures/              PNG images the levels use
  scripts/               the project's Lua scripts (once the mods runtime exists)
  .studio/               Studio's cache: never shared, rebuilt at will
```

The manifest's name and its fields are the Lua SDK page's ([Projects and the manifest](lua-sdk.md#manifest)), which
decides how far a project follows FiveM's resource folder; the formats of `levels/` are the content page's
([#1058](issue:1058)). The folders are where Studio puts what it creates and imports; a user may add others. A project's
first level format is the **sandbox layout** Coney loads today ([The layout format](../sandbox.md#the-layout-format)):
it already has shapes, textures, spawn points, targets, AI fighters, camera views and a sun, and Coney already plays one
with the player at a chosen spawn (`--play-level sandbox:NAME --spawn NAME`).

## A new level {#new-level}

### What the editors do {#new-level-survey}

| | Unity | Godot | Unreal | Roblox Studio |
| --- | --- | --- | --- | --- |
| Start | *File > New Scene* | *Scene > New Scene* | *File > New Level* | *New* on the start page |
| Templates | scene templates: *Basic* (a camera and a light), *Empty*, and templates a user saves | none: an empty scene, then a root node to pick (2D, 3D, interface, other) | *Empty Level*, *Basic* (a floor, sky and light), *Open World* | *Baseplate*, *Flat Terrain* and whole sample games (a village, a castle, an obby) |
| A whole map from a 3D tool | import the file, drag it into the scene | import the file; it opens as a scene of its own; `-col` and `-colonly` in a node's name make collision | import into the Content Browser; meshes named `UCX_` become collision | the 3D Importer brings a model into the Workspace |

### Coney Studio's New Level {#new-level-coney}

*File > New Level* (Ctrl+N) asks for a name and a template, and puts the new level in `project:/levels/`. **The default
is a blank slate**, and the user goes on from it in one of three ways, each of which the dialog also offers as a start:

| Template | What the level starts with | In the Hierarchy | State |
| --- | --- | --- | --- |
| **Empty** (the default) | Nothing: the blank slate | The level's row alone ([The Hierarchy's tree](#hierarchy-tree)) | ready |
| **Basic** | A 60 m ground, a sky and sun, a spawn point at its middle and a camera view behind it, so Play works at once (Unreal's *Basic*, Roblox's *Baseplate*) | Ground, start (a spawn point) and a camera view; the sky and sun on the level's settings | ready: a sandbox layout's statements |
| **A Coney sandbox layout** | A copy of one of Coney's own courses (`default`, `combat`, `parkour`), to change ([The shipped layouts](../sandbox.md#the-shipped-layouts)) | One entity per statement, in the layout's order; its sky, sun and fog on the level's settings | ready: they are Coney's own assets, never game data |
| **A level from the game** | One of the game's levels to work on, by its name ([Levels](../../references/levels.md)) | The read-only *Game level* branch with the original's contents, and above it nothing yet, for what the user adds | ready to open, fly through and play unchanged (the world viewer, `--play-level`); additions that play with it wait on Coney's own level format and the mods runtime |
| **A map from Blender** | A `.gltf` or `.glb` imported into `project:/models/` and placed whole at the origin | The map as one entity, its parts as its children (read-only meshes, each with its collision switch); the parts marked as collision shown as colliders | waits on custom content ([Custom content](../../roadmap.md#custom-content)) |

- **Building from scratch** is *Empty* or *Basic*, then the Create menu's primitives and the project's imported assets
  ([Composing a level](level-view.md#composing)).
- **A level from the game is referred to, never copied**: the level file holds its `game:/` name and the user's
  additions and changes, so a shared project carries no game data ([The project](#project-coney)). Opening a game level
  from `game:/` without making a level shows it read-only; *New Level* from it makes a level of the project that builds
  on it.
- **A map from Blender** is a map, not an editable mesh: Studio places it, sets its collision and assigns its textures,
  and the map is changed in Blender and imported again, since Studio makes no models. Which of its parts collide follows
  a naming rule in the Blender file, as Godot's `-col` and Unreal's `UCX_` do; the rule is the content page's to fix
  ([#1058](issue:1058)), with Godot's as this page's proposal, since it needs no second mesh.
- **A user's own template** is any level of the project, duplicated in the File system (Ctrl+D): Unity's saved scene
  templates add nothing a duplicate does not do.

## Play mode {#play-mode}

### What the four editors do {#play-survey}

| | Unity | Godot | Unreal | Roblox Studio |
| --- | --- | --- | --- | --- |
| Start | Play button (Ctrl+P) | Run Project (F5) or the current scene (F6) | Play In Editor (Alt+P), in the viewport or a new window | Play (F5): the avatar at a spawn location; Play Here: in front of the camera; Run (F8): no avatar |
| Where it runs | the Game view, in the editor's process | a separate game process, embedded in the editor's Game workspace since 4.4; the editor shows its live tree (the Remote tree) | in the editor, or a standalone process | the same viewport |
| Pause, step | Pause (Ctrl+Shift+P), Step (Ctrl+Alt+P) | Pause (F7); the Game workspace steps one frame (4.4+) | Pause, Frame Skip | none in the toolbar |
| Leave the player's view | the Scene view stays live beside the Game view | the Game workspace's camera override, a free camera in the running game (4.4+) | Eject (F8): the editor camera, the game still running | Client/Server toggle |
| Edits made while playing | made in the Scene view to the running game, seen at once in the Game view, and lost on Stop; the editor is tinted to warn; *Copy Component* in Play and *Paste Component Values* after keeps one | two kinds: an edit to the running game through the Remote tree is lost; an edit to the edited scene is kept, and with *Debug > Synchronize Scene Changes* it is replicated in the running game at once | lost, unless kept with *Keep Simulation Changes* (K) in Simulate | lost: Stop resets every object; an instance copied in the playtest can be pasted after Stop |
| Several players | none built in | several instances | several clients | Server and up to eight clients (F7) |

Unity, Unreal and Roblox Studio make Play a **sandbox**: whatever is done in it, the running game is thrown away on Stop
and the edited scene is as it was. Godot alone separates the two things an edit can touch: the **running game** (lost on
Stop) and the **edited scene** (kept, and sent live to the running game). Its users get the best of both, and pay with a
second tree to tell apart from the first.

### Coney Studio's Play mode {#play-coney}

- **Play** (Ctrl+P, Unity) starts the project's level in the Game view with the player at the level's first spawn point,
  as Roblox's Play puts the avatar at a spawn location; **Play here** (Ctrl+Shift+Alt+P) puts him where the Level view's
  camera looks, on the ground below it, as Roblox's Play Here does. Both realize what Coney does today: the play mode
  with `--spawn`, and the Entity editor's *Move to camera* and *Drop to ground* ([Entity
  editor](../debug-menu.md#entity-editor)). The player is Rembrandt unless the project's scripts pick another character;
  choosing him in Studio realizes the Player page's *Change character*.
- **The engine handles everything.** Play runs Coney's own play mode with the project's level: the same combat, AI,
  pathing, physics and camera as the game, at the fixed 1/30 s step, so the player can run round, fight the layout's
  fighters and climb what it holds. Nothing in Play is Studio's code.
- **Pause** (Ctrl+Shift+P) and **Step** (Ctrl+Alt+P) are Unity's keys for the Time page's pause and one fixed step; the
  time scale is on the toolbar while playing.
- **Stop** (Ctrl+P again, or the Stop button) ends Play and throws the running game away. The level keeps the edits made
  to it while playing ([Live edits](#live-edits)); while playing, the editor is **tinted** (Unity's Play mode tint), so
  it is always plain which state the screen shows.
- **While playing**, the Hierarchy has two tabs, as Godot's Scene dock has *Local* and *Remote*: **Level** (what is
  saved) and **Running** (the running game's entities, live, as the Entity inspector shows them). The Inspector edits
  whichever is selected; an edit of a running entity goes through the Entity editor's queue, between two steps and into
  the run log ([Entity editor](../debug-menu.md#entity-editor)), and is lost on Stop. The debug panels work as in the
  game.
- **Eject** (F8, Unreal) leaves the player where he stands and flies the Level view's camera through the running game,
  and F8 again takes the player back. It realizes the play mode's free camera (the Camera page's, which flies while the
  player stands still). Studio's own keys (the Play keys, F8) are caught by Studio before the game, even while the Game
  view has the keyboard; the Coney console's F8 in the game ([#1064](issue:1064)) is that unit's to place in Studio.
- **The mouse and keyboard** go to the game while the Game view has focus, and Escape gives them back to the editor
  (Unity's rule), so Escape is never the game's pause in Studio; the gamepads always go to the game, as they do with the
  overlay open.
- **Play saves first** (and a recovery copy, [How Play runs](technology.md#play-session)), so what plays is what a
  player of the project would get.

Play runs in Studio's own process, a play session on its own simulation thread drawn into the Game view ([In the process
or a child process](technology.md#play-process)); how the session is built and thrown away is the
[technology](technology.md#play-session) page's, and how it loads a project is the [Lua SDK](lua-sdk.md#play) page's.

### Live edits while playing {#live-edits}

The maintainer asks that an edit made in the Level view shows in the running Game view at once, where it can be done,
and accepts that it is rough at first. Studio follows **Godot's model**, the only one of the four that does this:

- **An edit in the Level view during Play edits the level**, as it would outside Play: it is a command on the undo
  stack, it is saved with the level, and it is **kept after Stop**. Losing work done while playing is the best known
  trap of Unity's Play mode, and Studio's users build levels, so the level is what they edit.
- **The same edit is sent to the running game**, as Godot's *Synchronize Scene Changes* does: an edit command applied
  between two steps ([Live edits during Play](technology.md#live-edits) has the queue), so a step never sees half an
  edit. What each edit does to the running game:

  | Edit to the level | In the running game |
  | --- | --- |
  | A primitive moved, turned, sized, added or deleted; its collision changed | Collision is a simulation asset, which a live edit may not replace ([Hot reload](technology.md#hot-reload)): the Game view marks the session stale and asks for Play again (*Restart*), and the change is in the level already |
  | A texture, tiling or tint | Drawn with the new one at the next frame |
  | Sky, sun, ambient light, fog, occlusion, shadows | Applied at the next frame |
  | An AI fighter moved or turned | Teleported there through the Entity editor's *Teleport* and *Heading*, which an AI human takes |
  | An AI fighter added or deleted | Spawned as the AI fighters page spawns one and teleported to its place, or deleted as the Entity editor deletes |
  | A target moved, turned, added or deleted | Nothing until the next Play: the Entity editor takes only *Frozen* for a target ([Entity editor](../debug-menu.md#entity-editor)) |
  | A spawn point or camera view | Nothing until the next Play: they matter only at a start |

- **An edit Coney cannot apply live is kept in the level and said so**: the Game view marks the session stale (*applies
  on the next Play*), never dropping the edit silently, and *Restart* (Ctrl+Shift+R) plays the level again from its
  start with every edit.
- **Edits of the running game itself** (a human's health, a fighter's state, the Running tab) are the Entity editor's
  and are lost on Stop, as in all four editors.
- **Rough at first is accepted:** the first version may mark the session stale for more edits than it must;
  correctness (the running game matching the saved level after a Restart) comes before doing more live.

Unity's *Copy Component* and Unreal's *Keep Simulation Changes* are not needed: the level's edits are already kept, and
the running game's are deliberately not.

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
  else (the Level view, the Inspector), nothing is imported and the panel says where to drop it. One place for imports
  means one place to look for what came in.
- **Dragging a project file from the File system into the Level view places it**, which is not an import: a texture
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
  Unity and Godot; for a script that is where hot reload begins ([Hot reload](technology.md#hot-reload)).

What can be imported is what Coney can load (rule 2), so the list grows with Coney:

| Kind | Becomes | What Studio edits | State |
| --- | --- | --- | --- |
| `.png` | A texture a level's shapes use | Where it is applied, its tiling and tint; never its pixels | ready: a sandbox layout's `texture` statement ([Statements](../sandbox.md#statements)) |
| `.layout` | A level | Everything in it: shapes, collision, spawns, targets, fighters, views, lighting | ready: Coney's sandbox layout format |
| `.lua` | A script | Its text, in the Script editor | waits on the mods runtime ([Script mods](../../roadmap.md#script-mods)) |
| `.gltf`, `.glb` | A map or a model | Its placement, its collision and which textures its materials use; never its mesh, bones or clips, which are remade in Blender and imported again | waits on custom content ([Custom content](../../roadmap.md#custom-content)) |
| `.wav` | A sound | Where it plays; never its samples | waits on Coney loading sounds from outside the disc |

The content page ([#1058](issue:1058)) owns the formats themselves and their export.

## Previews {#previews}

### What the editors do {#previews-survey}

| | Unity | Godot | Roblox Studio |
| --- | --- | --- | --- |
| In the file browser | a thumbnail per asset, sized with a slider | a thumbnail per resource in the FileSystem dock's grid | thumbnails in the Asset Manager and the Toolbox, with a play button on a sound |
| Selected | a **preview pane at the foot of the Inspector**: a model to turn with the mouse, an animation playing on a model with play, speed and a scrub bar, a sound with play, loop and its waveform | a preview in the Inspector for a mesh, texture or sound; the import dialog of a 3D file shows the model and plays its clips | a preview window in the 3D Importer before import |
| Opened (double-click) | the asset's own window (the Animation window for a clip) or an outside program | the resource's editor; animations in the **Animation panel** along the bottom | the Animation Editor for a rig's clips |

### Coney Studio's previews {#previews-coney}

Three steps, each one click deeper, taking Unity's preview pane and Godot's and Roblox's thumbnails:

1. **Thumbnails in the File system**, for `project:/` and `game:/` alike: a model drawn from the front, a texture
   shrunk, a clip as its model in the clip's middle frame, a sound as a sound icon. They are made once and kept in the
   `.studio/` cache (for a `game:/` file too, so no picture of game data leaves the player's machine).
2. **A quick preview at the foot of the Inspector** when one file is selected (Unity's pane), resizable and folded away
   with one click: a model turning, which a drag turns by hand; a clip playing on its model, with play, pause, loop and
   speed; a texture at its size, with its alpha; a sound with play and stop.
3. **The full viewer on double-click**, a tab over the Level view: the **Model viewer** (shaded, wireframe, by material,
   normals, bone axes and the [Mesh group](../debug-menu.md#mesh-group)'s counts), the **Animation viewer** (every clip
   of a set by category, loop, speed, in place, and a timeline) and the **Texture viewer**. These are the viewers of
   [the panel list](#panel-list), and the *Viewers* layout arranges them.

All three only look, since Studio makes no models or animations: nothing in a preview edits the file. What each
realizes:

| Preview | Realizes in Coney | State |
| --- | --- | --- |
| A model, still or turning | The [character viewer](../running-viewers.md#the-character-viewer) and the mesh views | ready |
| A clip on its model | The [Animation player](../debug-menu.md#animation-player) and the character viewer's clips | ready; the timeline's scrubbing waits on a seek in the animation player |
| A texture | The texture dictionary viewer | ready |
| A sound | The game's sound banks load ([Sound](../../research/sound.md)); playing one sound by itself is the Audio page's test tone today | waits on Coney playing one sound of a bank by itself |

## What Studio skips {#skipped}

| Feature | In | Why Coney Studio skips it |
| --- | --- | --- |
| An asset store or toolbox of other people's content | Unity, Unreal, Roblox | No server and no hosting; a store could carry game data, which no Coney service may ([LEGAL.md](repo:LEGAL.md#no-game-data)) |
| Visual scripting (Blueprints) | Unreal | The game's logic is Lua calling natives; scripting is the [Lua SDK](lua-sdk.md) |
| Components added freely to any object | Unity, Godot | Coney's entities are the kinds the game has (humans, objects, doors, panes) and the layout's statements; Studio's *Create* menu offers those, as Roblox's *Insert Object* offers Roblox's classes |
| Terrain and foliage tools | Unity, Unreal, Roblox | Coney's worlds are sector meshes with no terrain system |
| The Rect tool and an interface designer | Unity, Roblox | Coney has no interface a project can make |
| Copying edits out of Play one by one | Unity (*Copy Component*), Unreal (*Keep Simulation Changes*), Roblox (copy and paste) | Level edits made in Play are kept anyway ([Live edits](#live-edits)), and the running game's are deliberately thrown away |
| Modelling, sculpting, rigging, animating, texture painting | Unity (ProBuilder), Unreal (modelling mode), Roblox (solid modelling) | Blender and the other tools do it; Studio imports their work and builds levels from it |
| Running without a player | Roblox (Run), Unreal (Simulate) | The game's scripts and play mode expect a player; Eject gives the same view of a running level |
| Testing with several players | Roblox, Unreal, Godot | Waits on Coney's co-op ([Network architecture](../network-architecture.md#phases)) |
| Team Create, cloud places | Roblox | No server; a project is a folder, shared with git or any other way |
| One keymap per editor | Unreal, Godot (presets) | One keymap, rebindable, is one set of keys to document and test |

## Proposed build issues {#build-issues}

In the order they should come. Each realizes a feature Coney has today; the issues for what waits on Coney (the script
editor, paths, a mission editor, lighting for game levels, several players) are written when Coney gains it, from the
roadmap the parent issue asks for. They are the editor's side of the work, and five of them meet four of the [technology
page's build issues](technology.md#build-issues), which build the program, the play session, the edit command queue and
the importers: when filed, issues 1 and 2 here go with its issue 2 (the shell and the File system panel), issue 7 with
its 5, issue 8 with its 6 and issue 9 with its 10, as one issue each. In the import pair, the technology issue brings
the importer libraries (glTF, FBX, images, audio) and this one the import flow; a kind is offered in Studio once Coney
can load what it makes ([Importing](#import-coney)), PNG and `.layout` first.

1. **tools: Add the Coney Studio shell with projects and layouts.** Studio starts, makes a new project folder with its
   manifest and the folders above, or opens one, and remembers the recent ones. Its panels dock, tab, float and resize;
   *Window > Layouts* saves, switches, deletes and resets to the built-in *Default*, *Wide* and *Viewers* layouts, per
   user, at any UI scale; the theme's defaults and *Edit > Preferences > Theme*, saved per user. Tests: a project made
   and opened again with no window; a layout and a theme saved and loaded round-trip.
2. **tools: Add the File system panel rooted at the project.** The project as a tree under `project:/`, with the game's
   files read-only under `game:/`; nothing outside the two is reachable, and no saved path is absolute or climbs above
   the root. Moves, renames and deletes fix the references and ask first; changes made on disk are seen. Tests: a path
   that leaves the root is refused; a rename updates a level that refers to the file.
3. **tools: Add the Level view with the fly camera and Unity's keymap.** A sandbox layout or a level's world drawn as
   the world viewer draws it, flown with the right mouse and W A S D, Q and E, Shift faster, Alt slower, the wheel for
   speed, orbit, pan, zoom, F and Shift+F; pad 1 flies it too. The keymap is data, rebindable and saved per user. The
   Debug draw and mesh views are its view modes. Tests: a scripted input flies the same path every run; every key of the
   table is bound by default.
4. **tools: Add the Hierarchy and Inspector with one selection.** The level as a free list of entities with the level's
   settings in the Inspector, a sandbox layout opened as one entity per statement, the search and the kind filter
   (`t:spawn`), one selection shared by every panel (click, Shift and Ctrl, box), the merged inspector of
   [#1053](issue:1053), values shared by a multiple selection, and the pre-selection highlight; parenting, groups and
   prefab instances with overrides once Coney's own level format stores them ([#1058](issue:1058)). Tests: a selection
   made in one panel shows in the others; a multiple selection edits all; the filter shows only the kind asked for.
5. **tools: Add Unity's transform gizmo and undo to the Level view.** The Move, Rotate, Scale and Transform tools (W, E,
   R, Y) with every handle of Unity's gizmo, its colours, yellow highlight, constant screen size and handle-first
   picking; the Pivot / Center (Z) and Global / Local (X) toggles; increment, grid, vertex and surface snapping with the
   toolbar's steps; drop to ground, move to view; axes the selection cannot take drawn grey; every drag one command on
   an undo stack with its history. Tests: a scripted drag of each handle gives the expected value, snapped and not;
   every edit undoes to the same state.
6. **tools: Add composing levels to Coney Studio.** *New Level* with the Empty, Basic and sandbox layout templates;
   opening a game level read-only; the Create menu's primitives, spawn points, targets, AI fighters and camera views,
   placed where the camera looks and dropped to the ground; repeat and duplicate; textures applied by drag or in the
   Inspector with tiling and tint; each primitive's collider in the Inspector's Collision group and the Collision view
   mode; delete; saving the level as a sandbox layout. Tests: a level composed by script saves a layout that Coney loads
   and plays; a saved level reopens unchanged, byte for byte.
7. **tools: Add Play mode to Coney Studio.** Play, Play here, Pause, Step, Restart and Stop with Unity's keys; the level
   plays in the Game view beside the Level view with the player at its spawn or the camera's spot, the editor tinted;
   the Hierarchy's Level and Running tabs, the running game edited through the Entity editor's queue; Eject flies the
   free camera; Stop throws the running game away. Tests: a scripted Play reaches the spawn point and a fight with a
   layout's fighter; the level after Stop is the level as edited.
8. **tools: Add live level edits during Play.** A level edit made in the Level view while playing is kept in the level
   and applied to the running game between two steps: textures and lighting redrawn, AI fighters teleported, spawned or
   deleted; an edit that cannot apply live (a primitive's, whose collision only a new Play replaces, or a target's)
   marks the session stale and asks for *Restart*. Tests: a sun turned during a scripted Play shows at the next frame; a
   box moved marks the session stale; after Restart the player is blocked at its new place and the running game matches
   the saved level.
9. **tools: Add drag-and-drop import and the Import button.** Drops land only in the File system panel (into the folder
   under the cursor or the selected one) and nowhere else; the Import button and *File > Import...* do the same through
   a file picker; PNG and `.layout` import by kind with their settings in the Inspector and Reimport; name clashes ask;
   other kinds are refused with the list. Tests: a drop outside the panel imports nothing; a PNG dropped on a folder is
   there and a shape can use it.
10. **tools: Add the debug menu as Coney Studio panels.** Studio as the third front end of the debug model: the Console
    (the run log and the Lua console's line) and the Natives panel from their pages, and every other page of the session
    (Tunables, Player, Camera, Spawner, AI fighters, Levels, Missions and the rest) as a panel, with no Studio code per
    page. Tests: every page of the session draws as a panel, as the overlay test checks for the overlay.
11. **tools: Add previews and the model, animation and texture viewers.** Thumbnails in the File system, cached in
    `.studio/`; the quick preview at the foot of the Inspector; and on double-click a `game:/` model opens in the Model
    viewer (shaded, wireframe, by material, normals, bone axes, its Mesh group counts), a clip in the Animation viewer
    (the Animation player's sets, loop, speed, in place) and a texture dictionary in the Texture viewer; the *Viewers*
    layout arranges them. Tests: each viewer opens a synthetic fixture with no disc; a disc check prints counts only.
12. **tools: Add exporting a Coney Studio project as a .cmod package.** *File > Export project...* packs the manifest,
    levels, scripts and the project's own assets into one `.cmod` file for the mods folder, leaving out `.studio/` and
    every `game:/` file, after saving and checking that the manifest's level and scripts exist. Tests: an exported
    package of a fixture project unpacks to the same files; a project that refers to a `game:/` asset exports with no
    game data in it. It waits on the Lua SDK page settling how the mods folder takes a `.cmod` ([The mods
    folder](lua-sdk.md#mods-folder)).

## Sources {#sources}

The editors' own documentation and source, read for this page:

- Unity: [Scene view
  navigation](https://docs.unity.com/en-us/engine/current/manual/unity-editor/editor-windows-views-reference/using-the-scene-view/scene-view-navigation),
  [Positioning
  GameObjects](https://docs.unity.com/en-us/engine/current/manual/working-with-scenes/scenes-manage-gameobjects/positioning-game-objects)
  (the tool keys and snapping), [Customizing your
  workspace](https://docs.unity3d.com/Manual/CustomizingYourWorkspace.html) (layouts), [The Game
  view](https://docs.unity3d.com/Manual/GameView.html) (Play mode),
  [Preferences](https://docs.unity3d.com/Manual/Preferences.html) (the Play mode tint),
  [Prefabs](https://docs.unity3d.com/Manual/Prefabs.html) (instances and overrides).
- Godot: [Introduction to 3D](https://docs.godotengine.org/en/stable/tutorials/3d/introduction_to_3d.html) (freelook and
  the tool keys), [File system](https://docs.godotengine.org/en/stable/tutorials/scripting/filesystem.html) (`res://`,
  moving files), [Customizing the
  interface](https://docs.godotengine.org/en/stable/tutorials/editor/customizing_editor.html) (docks and editor layouts,
  `editor_layouts.cfg`), [Overview of debugging
  tools](https://docs.godotengine.org/en/stable/tutorials/scripting/debug/overview_of_debugging_tools.html)
  (*Synchronize Scene Changes* and the Remote tree); the 4.6 editor theme from its source at `4.6-stable`:
  [editor_settings.cpp](https://github.com/godotengine/godot/blob/4.6-stable/editor/settings/editor_settings.cpp) (the
  settings' defaults and ranges),
  [editor_theme_manager.cpp](https://github.com/godotengine/godot/blob/4.6-stable/editor/themes/editor_theme_manager.cpp)
  (the presets and spacing) and
  [thirdparty/fonts](https://github.com/godotengine/godot/tree/4.6-stable/thirdparty/fonts) (the fonts and their
  licences).
- Unreal: [Viewport
  controls](https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine), [Play and
  Simulate](https://dev.epicgames.com/documentation/en-us/unreal-engine/in-editor-testing-play-and-simulate-in-unreal-engine)
  (Play In Editor, Eject, *Keep Simulation Changes*).
- Roblox Studio: [Studio interface](https://create.roblox.com/docs/studio/ui-overview),
  [Testing modes](https://create.roblox.com/docs/studio/testing-modes).
- FiveM: [Creating your first
  script](https://docs.fivem.net/docs/scripting-manual/introduction/creating-your-first-script/) (the resource folder,
  its manifest, `refresh` and `restart`).

A few small facts come from the editors as they are used rather than a page: Unity's *Undo History* window and
*Create Empty Parent* key, Roblox Studio's Ctrl+1 to Ctrl+4 tool keys and Shift for a slower camera (the last from
Roblox's developer forum).
