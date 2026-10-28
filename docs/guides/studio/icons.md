# Coney Studio: icons

The design of how Coney Studio uses icons: where Unity, Godot and Roblox Studio put them and at what size, where
Studio puts them, the symbols Studio needs, the open icon sets it can take them from, and the one set this page
recommends. The recommendation's licence is a question for a maintainer ([below](#question)). The design is
[#1105](issue:1105); building it is [#1104](issue:1104). The theme and fonts it joins are the editor experience page's
([Look and theme](editor-experience.md#theme)).

**Status: design.** Nothing here is built; Studio has no icons today and its tool buttons show letters. This is a
design in the sense of [Page kinds](../writing-docs.md#page-kinds): when it is built, what is built moves into a guide.

Two rules decide every choice, the editor experience page's own:

1. **Familiar first.** An icon a Unity, Godot or Roblox user already reads (a triangle for play, an eye for visible, a
   padlock for locked) is used for that meaning and no other.
2. **Only what Coney has.** An icon appears with the feature it marks. The list of symbols
   ([The symbols Studio needs](#symbols)) says which are for panels that are ready and which wait.

**No icon file of any editor is in this repository or on this page.** The editors are described in our own words and by
the names their documents give; the sets Coney can use are open ones, fetched at build time like the fonts.

## Contents {#contents}

- [How three editors use icons](#editors): where, how big, how far apart
- [Where Studio puts icons](#studio): the sizes, the spacing, the colours, the states
- [The symbols Studio needs](#symbols), now and soon
- [Open icon sets](#sets): the licences, what each covers, how each looks beside Godot's Modern theme, the formats
- [Loading into Dear ImGui and pinning](#loading)
- [The recommendation](#recommendation) and [the question for a maintainer](#question)
- [What #1104 builds](#build), [Sources](#sources)

## How three editors use icons {#editors}

### Where they put them {#editors-where}

Read from each editor's documents and, for Godot, its source at tag `4.6-stable`. A statement marked *(use)* is from
using the editor: the documents do not describe it.

| Place | Unity | Godot 4 | Roblox Studio |
| --- | --- | --- | --- |
| Toolbar | The Play, Pause and Step buttons; icon-and-word buttons for the account, Asset Store, services, version control, undo history, search and the layout menu ([Toolbar](https://docs.unity.com/en-us/engine/6000.0/manual/unity-editor/editor-navigating-managing/toolbar)); the transform tools are icons in the Scene view's own overlay, not in the toolbar | An icon on every top-bar button; the main-screen switch (2D, 3D, Script) has icons beside its words; the run bar holds play, pause, stop and the play-scene buttons ([`editor_run_bar.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/run/editor_run_bar.cpp)) | A ribbon of tabs (Home, Model, Avatar, UI, Script, Plugins) of icon-over-word tools; a *Compact toolbar* option makes the icons and words smaller and *Show labels* removes the words ([Studio interface](https://create.roblox.com/docs/studio/ui-overview)) |
| Panel tabs | A small icon before each tab's name; the design system gives tab and window icons a smaller drawing area ([Iconography](https://unityeditordesignsystem.unity.com/foundations/iconography)) | The bottom panel's buttons and the docks' tabs carry icons *(use)*; the colour of an unselected tab's icon is a separate theme value ([`theme_modern.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/themes/theme_modern.cpp)) | Windows are named in text; the Window menu and the ribbon open them *(use)* |
| Tree rows | The type's icon before the name in the Hierarchy: a cube for an object, a coloured cube for a prefab *(use)* | The node's class icon before its name, drawn at the editor's class icon size; at the row's end the lock, the visibility eye, the groups, signals and script badges and a pin ([`scene_tree_editor.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/scene/scene_tree_editor.cpp)) | The class's icon before the instance's name in the Explorer, from a sheet of 16 by 16 images the community has read out of Studio ([forum](https://devforum.roblox.com/t/rbxassetid-of-the-studio-explorer-icons/860371)); plugins ask for one with `StudioService:GetClassIcon` ([reference](https://create.roblox.com/docs/reference/engine/classes/StudioService)) |
| File types | Every asset is an icon of its type (script, material, folder); a slider at the bottom of the Project window grows the icons, and at its far left they become a list ([Project window](https://docs.unity.com/en-us/engine/6000.0/manual/unity-editor/editor-windows-views-reference/project-view)); the icons have mip levels from 16 to 512 pixels | A folder or file icon, or the class icon for a scene, script or resource, in the FileSystem dock; thumbnails in two sizes in the list view; a favourites icon ([`filesystem_dock.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/docks/filesystem_dock.cpp)) | The Asset Manager and Toolbox show thumbnails *(use)* |
| Inspector or Properties header | The object's icon, its name, the *Static* box, a padlock that locks the Inspector and a three-dot menu *(use)*; a component's header has a fold arrow, its icon, an enable box and a menu | The Inspector's top row: new, load and save resource buttons, back and forward, a history button, and the edited object's class icon beside its name ([`inspector_dock.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/docks/inspector_dock.cpp)) | A header with the class and name, no icon; a fold arrow on each section; a plus and a cross on tags, a plus and a gear on attributes ([Properties](https://create.roblox.com/docs/studio/properties)) |
| Menus | Words only, with a tick for a checked item *(use)* | An icon beside most commands, drawn in a column before the words *(use)* | Words, with the shortcut at the right *(use)* |
| Console levels | Three toggle buttons by the search box, one per level, each an icon with a count: messages, warnings, errors ([Console](https://docs.unity.com/en-us/engine/6000.0/manual/unity-editor/editor-windows-views-reference/console-window/console)); a level icon opens each entry *(use)* | The Output panel has one toggle per level (plain, error, warning, editor), each an icon, with clear, copy, collapse and search buttons; the error and warning icons also start the entries ([`editor_log.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/editor_log.cpp)) | A type filter, a context filter, a search field, a clear button and display options ([Output](https://create.roblox.com/docs/studio/output)); the levels differ by text colour *(use)* |
| Play controls | Play, Pause and Step as three icon buttons ([Toolbar](https://docs.unity.com/en-us/engine/6000.0/manual/unity-editor/editor-navigating-managing/toolbar)); the whole editor is tinted while playing | A play button with the play-scene and play-custom buttons; while running, pause and stop appear ([`editor_run_bar.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/run/editor_run_bar.cpp)) | A mode menu with a Play button on the left of the ribbon; Stop, Pause or Resume and Step Forward appear during a test ([Testing modes](https://create.roblox.com/docs/studio/testing-modes)) |

What the three agree on:

- **Icons mark kinds and commands; words mark everything else.** A tree row has an icon for its kind and a name; a
  property has a name and no icon. Menus are the one place the editors differ (Godot uses icons, Unity and Roblox do
  not).
- **The toggle that filters the Console is an icon with a count**, the same in Unity and Godot.
- **Play, Pause, Stop and Step are the same four shapes everywhere**, and the running state is shown by more than the
  icon (a tint in Unity, Pause and Stop appearing in Godot and Roblox).
- **One size does nearly all the work: 16 pixels.**

### Sizes and spacing {#editors-sizes}

| | Unity | Godot 4 | Roblox Studio |
| --- | --- | --- | --- |
| Size at 100% | 16 by 16 pixels is the default; every icon is designed in a 16 pixel frame ([Iconography](https://unityeditordesignsystem.unity.com/foundations/iconography)) | 16 by 16 pixels: the editor's icon size is `16 * EDSCALE` (confirmed, code: [`editor_theme_manager.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/themes/editor_theme_manager.cpp), `class_icon_size`); the contribution guide says an icon "must be 16 by 16" ([Creating icons](https://docs.godotengine.org/en/stable/engine_details/editor/creating_icons.html)) | 16 by 16 for the Explorer's class icons and the quick-access bar, 32 by 32 for the larger tools, as the community reports ([forum](https://devforum.roblox.com/t/what-size-should-a-plugin-icon-be/1598548)); no document gives a size |
| Scaled | A second file at twice the size, named with `@2x` (32 by 32), for high-density displays; sizes in the sequence 16, 32, 64, 128, 256 | The SVG is drawn again at `16 * EDSCALE`; the scale is *Auto* (from the screen) or 75% to 200% or a custom 0.5 to 3 ([`editor_settings.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/settings/editor_settings.cpp), `display_scale`) | Not documented; the community reports a sheet that cannot give a 2x or 3x image |
| Drawing area | Most icons keep 1 pixel of padding (14 by 14 inside); tab and window icons keep 2 (12 by 12) | The whole 16 by 16 square, snapped to pixels "to remain crisp" | The whole square |
| Line | 1 pixel strokes with round ends, no stroke or gap thinner than 1 pixel, and 2 or 3 visual elements at most | 2 pixel strokes and filled shapes, in the two files read for this page (`Play`, `Node3D`) | not documented |
| Format | Transparent PNG; SVG is not supported while the old drawing code remains | SVG, drawn once at start-up; "keep the file small" | not documented |
| Dark and light | A `d_` prefix on the file for the dark skin; one file for both when no dark version exists | One file in light-on-dark colours; for a light theme a fixed colour map turns its grey and its few hues into darker ones ([`creating_icons`](https://docs.godotengine.org/en/stable/engine_details/editor/creating_icons.html)) | Not documented |
| A selected state | A second file with "On" at the end of its name | The button's icon colour changes (a normal, a hover, a pressed and a disabled colour, set per theme) | not documented |
| Colour | A palette for icons only: a grey for what needs no colour, hues by product area, and green, red and blue for positive, error and active states; colour is never the only signal | Mostly neutral; fixed hues on the few that need them (the error, success and warning icons take the theme's own colours) | not documented |

Spacing, in pixels at 100% and doubled at 200%, from Godot's Modern theme (`theme_modern.cpp`, with the default base
spacing of 4; confirmed, code):

| Where | Gap between the icon and the next thing |
| --- | --- |
| A button's icon and its word; a tab's icon and its word; the menu bar's items | 4 |
| A tree row's icon and its name, and the check box and its name | 6 (1.5 times the base) |
| An item list's icon margin | 6 |
| A popup menu's icon and its word | 7 (1.75 times the base) |
| A check box or a toggle and its word | 8 |

Unity and Roblox document no gaps. Godot's are a quarter to a half of an icon's width, and Studio uses the same.

## Where Studio puts icons {#studio}

Studio follows Godot's Modern theme for its look ([Look and theme](editor-experience.md#theme)) and the editors above
for the places. Every size is the 16 pixel base times the [UI scale](../debug-menu.md#ui-scale), rounded to whole
pixels, and the font atlas is built again when the scale changes, as it is for the text.

| Place | What Studio does | Size and gap at scale 1 |
| --- | --- | --- |
| Toolbar | One icon button per tool (View, Move, Rotate, Scale, Transform), per toggle (Pivot or Center, Global or Local, grid snap, the snap menu) and per Play control; no words, a tooltip with the name and the key on every button; a button in use takes the accent fill; a thin gap separates the groups. The layout menu at the right is an icon and a word ([Layouts](editor-experience.md#layouts)) | 16 pixel icon in a 28 pixel square button; 2 between buttons, 8 between groups |
| Panel tabs | A leading icon before each panel's name (the panel's own symbol, [below](#symbols)); icon-only when a tab is narrow | 16 pixels, 4 gap |
| Tree rows (Hierarchy, File system, Natives) | The entity's or file's kind icon before its name; at the end of a row, shown on hover and kept shown when set: the visibility eye and the lock. A prefab's mark is its own icon on the row. Rows from a base level are drawn with a lock and dimmed | 16 pixels, 6 gap; the end icons 16 |
| File types | One icon per file type in the File system's tree and list; a grid view later uses larger type icons and, for models, textures and sounds, the previews of [Previews](editor-experience.md#previews) | 16 pixels in a row; 48 in a grid cell |
| Inspector header | The kind's icon at the left of the name, the tags and a more menu at the right; the level row's icon is the level's. Section headers carry no icon | 24 pixels, 8 gap |
| Menus | An icon beside a command that has one elsewhere (the tools, Undo, Redo, Delete, Duplicate, Play); a menu with no icon at all leaves no empty column. The shortcut is at the right, as in all four editors | 16 pixels, 7 gap |
| Console levels | One toggle per level in the Console's bar, each an icon with its count, tinted in its level's colour: information, warning, error and detail (the [run log's four levels](../run-log.md#categories)); clear, copy, collapse and search buttons beside them. Each line starts with its level's icon in the same colour, and the word of the level is in the tooltip, so colour is never alone | 16 pixels, 4 gap |
| Play controls | Play (filled with the accent while idle), Pause and Step in the middle of the toolbar, as in Unity; Stop replaces Play while running; the tint of [Play mode](editor-experience.md#play-coney) shows the state as well as the icons | 16 pixels; Play 20 in a 28 pixel button |
| Empty states | A large icon, one line and the button that fixes it ("Open a project"); never an icon alone | 48 pixels |
| Search and clear | A magnifier inside every search box and a cross that clears it | 16 pixels, 4 gap |

**Colour.** An icon is one colour, drawn in the colour of the text beside it, so a theme change recolours every icon
for free and the *Light* preset needs no second set (Godot needs a map; Unity needs a second file). The states come from
the theme's tokens ([How the settings map onto Dear ImGui](editor-experience.md#theme-imgui)): resting is the dimmed
text colour, hover is the text colour, pressed or chosen is the accent (or the contrast colour on an accent fill), and
disabled is the dimmed colour at reduced opacity. Meanings with a fixed hue use the semantic tokens the theme work adds
(error, warning, success, and the red, green and blue of the three axes). The entity kinds are grouped in four families
tinted by tokens (shapes and colliders, characters, triggers and zones, scenery and effects); the glyphs differ inside a
family, so colour is never the only difference.

**State.** There is no second, filled glyph for "on" (the chosen set has none; Unity's "On" files and Godot's colour
change are the two models). A tool that is chosen takes the accent fill behind its icon and the icon turns to the
contrast colour; a toggle that is on does the same.

**Always a name.** An icon-only control has a tooltip with its name and its key, shown after half a second, because an
icon is never trusted to be understood alone (Unity's rule: do not rely on colour alone, and review every new icon).

## The symbols Studio needs {#symbols}

By group, with *now* for what Studio's ready panels and kinds need
([The panels](editor-experience.md#panel-list), [Entity kinds](entities.md#kinds)) and *soon* for what waits on
something Coney does not have yet. One concept each; the glyph is chosen from the set at build time.

| Group | Now | Soon |
| --- | --- | --- |
| Tools | View (hand), Move, Rotate, Scale, Transform, Pivot or Center, Global or Local, grid snap, snap menu | measure, a duplicate-along-path tool |
| Play | Play, Play here, Pause, Step, Stop, Run in Coney (open outside), time scale | record an input run, a stop-at-breakpoint marker |
| Panels | Level view, Game view, Hierarchy, Inspector, File system, Console, Natives, Model viewer, Animation viewer, Texture viewer, Debug panels, Level settings, layout menu | Script editor, Paths, Mission editor |
| Entity kinds, ready | Level, Primitive (and its shapes: box, ramp, stairs, cylinder, capsule, sphere), Spawn point, Human, Target, Camera view | |
| Entity kinds, waiting | Group, Prefab instance, Mesh, Collider, Gang, Flag, Path, Flag link, Walkable area, Nav link, Volume box, Stealth zone, Object, Emitter, Door, Glass pane, Car, Light, Spawner | the same kinds, once Coney makes them from a level file |
| File types | Folder, generic file, level, texture, sound, model, animation clip, movie, Lua script, text or JSON, the manifest | prefab, mission, character, `.cmod` package, import sidecar, an override pack |
| Row and field actions | Visible and hidden (eye), locked and unlocked, add, remove, delete, duplicate, rename, search, filter, clear, revert to default, more (three dots), drag handle, fold arrows (right and down), check, link, tag, pin or favourite, copy, paste, refresh, back and forward, close, settings | an Apply to prefab mark, a modified dot |
| Console levels | Information, warning, error, detail; clear, copy, collapse | success, a profiler mark |
| Status | Unsaved, running, busy (a spinner), read-only (the base level), a missing file | a connected peer, a failed script |
| Theme | Light and dark | the system accent |

About 100 concepts in all. Several kinds have no obvious picture in any set (a stealth zone, a volume box, a flag link,
a spawner); each is a familiar picture (a hidden eye, a dashed box, a link, a plus on a marker) chosen on a screenshot
at build time, not on a name.

## Open icon sets {#sets}

### What was compared {#sets-method}

- **Licence**, and whether it is compatible with Coney's: code is GPL-3.0-or-later, and third-party assets keep their
  licence with its file beside them if it is CC0 or compatible with GPL-3.0-or-later
  ([LEGAL.md](repo:LEGAL.md#licences)). The FSF's list says that ISC and the Expat (MIT) licence are
  compatible with the GNU GPL, CC BY 4.0 with all its versions, and Apache 2.0 with version 3 only
  ([the list](https://www.gnu.org/licenses/license-list.en.html)). This page reports licences; whether to accept one is
  [a maintainer's](#question).
- **Coverage**: each set's list of names tested against about 100 concepts of [the list above](#symbols) (98 were
  tested; the rest are drawn from two). A concept counts as covered when the set has an icon for it under any of the
  common names. It is a first pass by name; the check at build time is a test over the pinned font
  ([What #1104 builds](#build)).
- **Style** next to Godot's Modern theme: 16 pixel icons, a soft corner, a neutral look. The line width at 16 pixels
  is worked out from the set's grid and stroke, because a line that is not a whole number of pixels is soft on a
  display at 100%.
- **Format** and how it loads into Dear ImGui ([below](#loading)).

### The table {#sets-table}

Versions are the ones current when this page was written. Counts are of the distinct icons in the default style.

| Set | Licence | Icons | Grid and line | Line at 16 px | Ships as | Studio's concepts found |
| --- | --- | --- | --- | --- | --- | --- |
| [Lucide](https://lucide.dev) 1.63.0 | ISC; the few icons derived from Feather are MIT | 1,924 | 24, line 2, round ends and joins | 1.33 | SVG and a **ready font** (TTF, WOFF2, WOFF) in each release; the TTF is 0.9 MB, 1,000 units to the em, 2,213 names | 98 of 98 |
| [Phosphor](https://github.com/phosphor-icons/core) 2.1.1 | MIT | 1,512 in each of 6 weights (thin, light, regular, bold, fill, duotone) | 256, line 16 regular, 24 bold | 1.0 regular, 1.5 bold | SVG, and one font per weight (the regular TTF is 0.5 MB) in the web package | 98 of 98 |
| [Tabler](https://github.com/tabler/tabler-icons) 3.50.0 | MIT | 5,203 outline, 1,054 filled | 24, line 2 (adjustable), round ends and joins | 1.33 | SVG; the font is a separate package, not a release asset | 98 of 98 |
| [Material Symbols](https://github.com/google/material-design-icons) | Apache 2.0 | about 4,300 names | 24, with axes for weight, fill, grade and optical size | varies with the weight | three **variable** fonts; the outlined TTF is 10.7 MB | 97 of 98 (no snap magnet) |
| [Godot's editor icons](https://github.com/godotengine/godot/tree/4.6-stable/editor/icons) 4.6 | Expat (MIT): the repository's `COPYRIGHT.txt` lists only the logo as an exception | 1,010 | 16, pixel-snapped, about 2 wide, mostly filled shapes with a few fixed hues | 2 | SVG only | 87 of 98 (no step, hand, globe, pivot, target, door, filter, ellipsis and a few more) |
| [Codicons](https://github.com/microsoft/vscode-codicons) 0.0.46 | CC BY 4.0 (attribution) | 639 | 16, pixel-snapped, 1 wide | 1.0 | SVG and a TTF of 150 KB | 80 of 98 (no 3D, path, door, car or animation symbols) |
| [Font Awesome Free](https://github.com/FortAwesome/Font-Awesome) 7.3 | Icons CC BY 4.0, fonts SIL OFL 1.1, code MIT | 2,001 solid, 273 regular, 609 brands | solid shapes, heavy | not measured | SVG, OTF and web fonts; the other styles are paid | 96 of 98 |
| [Bootstrap Icons](https://github.com/twbs/icons) 1.13 | MIT | 2,078 | 16 | not measured | SVG, WOFF and WOFF2 only (no TTF) | 97 of 98 |

Also looked at, not tabled: Remix Icon (Apache 2.0, 3,229; its names end in `-line` and `-fill`, so the name test could
not score it), Iconoir (MIT, 1,383; 80 of 98), Heroicons (MIT, a few hundred), Feather (MIT, 287; Lucide continues it),
Octicons and Fluent UI System Icons (MIT; each tied to a brand's look), Carbon (Apache 2.0; a data-tool look) and
Kenney's game icons (CC0; about a hundred, for controller prompts rather than an editor).

### What the table says {#sets-reading}

- **Licence does not separate the four general sets.** Lucide (ISC), Phosphor and Tabler (MIT) and Godot's (MIT) each
  ask only that the copyright and permission notice go with the copies; Material Symbols (Apache 2.0) adds a notice and
  a patent clause and is compatible with version 3 of the GPL only, which is what Coney is. Codicons and Font Awesome's
  icons are CC BY 4.0, which asks for credit in a way the others do not.
- **Coverage does not separate them either.** The four big general sets have every concept; Godot's editor icons lack
  the generic ones (hand, globe, step, filter, more) because the engine never needed them, and Codicons has no
  three-dimensional symbols.
- **Style does.** Godot's icons are 16 pixel drawings with a heavy line and fills, snapped to the pixel; nothing else is
  that. The nearest are Phosphor's regular and bold weights (a 1 and a 1.5 pixel line at 16 pixels, round ends) and
  Codicons (a 1 pixel line, drawn on the pixel but without the 3D vocabulary). Lucide and Tabler are 24 pixel drawings
  with a 2 unit line, which is a 1.33 pixel line at 16 pixels: a little soft at 100%, exact at 24 and 48.
- **Format separates them most.** A font loads in a few lines and is tinted by the text colour. SVG-only sets
  (Godot's, and any set used for colour) need an SVG rasteriser and an image atlas.

## Loading into Dear ImGui and pinning {#loading}

### As a font {#loading-font}

Dear ImGui 1.92, the version `cmake/deps.cmake` pins, merges one font into another so that an icon is a character in a
string (`ICON_FOLDER_OPEN " Open"`), and needs no list of glyph ranges since 1.92
([Fonts](https://github.com/ocornut/imgui/blob/v1.92.9b/docs/FONTS.md)). The steps, in Coney's own words:

1. Add Inter Regular (and, separately, Inter Bold) as now.
2. Add the icon font straight after each, as a merge: `MergeMode = true` puts its glyphs into the font just added;
   `GlyphMinAdvanceX` set to the icon size makes every icon the same width, so labels line up; `GlyphOffset.y` moves the
   icon to the text's middle (Lucide's glyphs sit on the baseline with the whole em above it: ascent 1000, descent 0);
   `PixelSnapH = true` keeps the width a whole number of pixels. With `GlyphMinAdvanceX` set, the size passed to
   the add call must be given, because the value is for that size and scales with it.
3. Write icons in strings from constants (`ICON_PLAY "..."`) that a generator makes from the font's own table of names
   and codepoints, so a renamed icon is a build failure and not a blank square. The ImGui community's headers
   ([IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders), zlib licence) already cover Lucide, Codicons
   and Material Symbols; Coney generates its own from the pinned file so the names and the font can never drift.
4. Icon-only buttons draw the icon font alone, at their own size, so a button does not depend on the label font. The
   font stays one colour; the caller sets `ImGuiCol_Text` for the state.

The icon font is rasterised by the same code as the text. Dear ImGui's default (stb_truetype) draws a glyph at the
size asked; FreeType's hinting (`IMGUI_ENABLE_FREETYPE`, an optional file in Dear ImGui; FreeType is not a
dependency today) would sharpen a line that falls between pixels and is the remedy if the 1.33 pixel line of a 24 unit
set looks soft in the screenshots of #1104. A **variable font** (Material Symbols) draws its default instance with the
default loader; its axes need a static copy made first, which is another build step.

### As SVG {#loading-svg}

A set that is not a font (Godot's) is rasterised at start-up and whenever the UI scale changes: a small rasteriser
(nanosvg is one header under the zlib licence) turns each used SVG into a white image at `16 * scale` pixels, the images
are packed into one texture, and each icon is drawn with `AddImage` and a tint. This is the route for any multi-colour
icon. It costs a texture, a packer, a cache keyed by size and a second code path beside the text; Dear ImGui's
coloured-icon API was replaced in 1.92 by custom font loaders, a young interface the library sends questions about to an
issue, so it is not a base to build on.

### Pinning {#loading-pin}

Studio's fonts are pinned in `cmake/deps.cmake` as a release archive with its SHA-256, no CMake build of its own
(`SOURCE_SUBDIR` names a folder it does not have), and copied beside `coney-studio` with their licences
([Fonts](editor-experience.md#theme-defaults); the prototype, [#1090](issue:1090), does it). An icon set takes the same
pattern:

- **Lucide**: `FetchContent_Declare(lucide_font URL .../releases/download/1.63.0/lucide-font-1.63.0.zip URL_HASH
  SHA256=540488d26fcd50734879a3ca076d25b75e0c4f36267b5602d69617e12e4dd59b ...)`, then `lucide-font/lucide.ttf` and
  `codepoints.json` are the only files used. The archive carries no licence file, so the ISC text is pinned at the same
  tag as a second download of one file (`DOWNLOAD_NO_EXTRACT`, SHA-256
  `b495047bd93a9b06913511076f504daba17d5bbeb3e0650f3bb53a4220329c57`). Both hashes were computed from the downloads
  when this page was written; #1104 recomputes them for the release it pins.
- **Phosphor**: the web package's tag archive, or the npm tarball, with the regular font (and the fill font if active
  states want it).
- **Tabler**: the webfont package's tarball.
- **Material Symbols**: one 10.7 MB file from the repository, which makes a big download for one font.
- **Godot's icons**: the repository is large, so the used SVGs are copied into `assets/` with the licence file beside
  them ([LEGAL.md](repo:LEGAL.md#licences)), a file list and a script that checks them against the tag.

## The recommendation {#recommendation}

**Use Lucide, as its own font, merged into Inter.** Every concept on the list exists in it; its ISC licence is one short
permissive licence that the FSF counts compatible with the GPL; its release ships a ready TTF that pins with a hash
exactly as Inter and JetBrains Mono do; the font merges into Dear ImGui in the way the library documents, and the
library's community already has Lucide name headers; and a font is one colour, so the *Light* preset, the accent and the
state colours cost nothing. Its rounded 2 unit outline and soft corners sit comfortably next to Godot's 4 pixel corner
radius and Inter.

What it gives up, honestly: its line is 1.33 pixels at 16, softer than Godot's pixel-snapped 2 pixel line, and it has no
filled variant for an active state. Studio meets both: the 24 pixel size is exact (Inspector header, empty states
scaled to 48), the accent fill marks an active tool, and FreeType's hinting is the remedy to try for the 16
pixel size, judged on the screenshots in #1104. If those show the 16 pixel icons still too soft, the answer is Phosphor
(MIT; a 1 pixel line at 16 pixels, a *fill* weight for active states), loaded the same way, which is why every step of
[the build](#build) goes through one generated constants file and one font-adding function.

Why not the others, one line each:

- **Phosphor**: the runner-up, equal on licence and coverage and crisper at 16 pixels; its icons come as one font per
  weight, so two fonts to merge and pin if the fill weight is wanted, and IconFontCppHeaders does not list it.
- **Tabler**: the same style and size as Lucide with five times the icons Studio will never use; its font is a separate
  package, not a release file.
- **Material Symbols**: the look of Google's products, a 10.7 MB variable font whose axes the loader ignores, and the
  Apache 2.0 licence with its patent terms for no gain.
- **Godot's icons**: the nearest look and MIT, and the class icons (a marker, an area, a collision shape, a navigation
  region, a skeleton) are the very pictures a Godot user knows for Studio's kinds; but the set is SVG only, so it needs
  a rasteriser, an atlas and a colour map for the *Light* preset, and a thousand engine-class names to filter. It stays
  a good source of ideas for the entity-kind pictures, not of files.
- **Codicons and Font Awesome**: CC BY 4.0 credit terms, and Codicons lacks the three-dimensional vocabulary.

## The question for a maintainer {#question}

A licence is a maintainer's decision ([AGENTS.md](repo:AGENTS.md)), so this page recommends and does not decide.

**May Coney Studio ship the Lucide icon font (ISC licence), pinned at a release and copied beside `coney-studio` with
its licence text, as it ships Inter and JetBrains Mono?** The ISC licence asks that its copyright and permission notice
go with every copy; the notice goes in the same `fonts` folder as the other licences, and `LEGAL.md` gets a line
naming it beside the other dependencies. The same question would be asked of Phosphor (MIT) if the maintainer
prefers it, and of Godot's icons (MIT) if the maintainer wants Godot's own pictures; nothing is built until it is
answered.

## What #1104 builds {#build}

In this order, each step tests first ([Testing](../testing.md)):

1. **The font and its pin**: the two `FetchContent` entries and the copy beside `coney-studio`. *Test:* the build
   fails if the file's hash differs; the licence file is beside the font.
2. **The constants**: one generated header of `ICON_*` strings from the pinned font's `codepoints.json`, kept in step
   by a `--check` in the gate like the repository's other generators. *Test:* a Python test over a fixture table (a
   name appears, a duplicate fails); the generated header matches the pinned file.
3. **The atlas**: Inter Regular and Bold each get the icon font merged in at the UI scale, rebuilt when it changes.
   *Test:* under the Test Engine, every constant Studio uses has a glyph in the built atlas at scales 1, 1.5 and 2.
4. **The helpers**: an icon button (tooltip with name and key, states by token), an icon-and-label, a tree row with
   its kind icon and end icons, a search box, a level chip. *Test:* the Test Engine finds each by path, clicks it, and
   reads its state.
5. **The places**: the toolbar, tabs, Hierarchy and File system rows, Inspector header, Console, menus and the empty
   states, one pull request each, with before and after screenshots at scale 1 and 2.
6. **The pages**: [Technology](technology.md#gui-choice) and [Look and theme](editor-experience.md#theme) point at the
   built set; this page's *Status* changes and what is built moves into a guide.

## Sources {#sources}

- Unity: [Iconography](https://unityeditordesignsystem.unity.com/foundations/iconography) (sizes, padding, strokes,
  `@2x`, `d_`, the "On" files, colours, format),
  [Toolbar](https://docs.unity.com/en-us/engine/6000.0/manual/unity-editor/editor-navigating-managing/toolbar),
  [Console](https://docs.unity.com/en-us/engine/6000.0/manual/unity-editor/editor-windows-views-reference/console-window/console),
  [Project
  window](https://docs.unity.com/en-us/engine/6000.0/manual/unity-editor/editor-windows-views-reference/project-view),
  [window title icons](https://docs.unity3d.com/ScriptReference/EditorWindowTitleAttribute.html) (16 and 32 pixels).
- Godot at tag `4.6-stable`:
  [`editor/themes/editor_theme_manager.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/themes/editor_theme_manager.cpp),
  [`theme_modern.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/themes/theme_modern.cpp),
  [`editor_icons.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/themes/editor_icons.cpp) (icons drawn
  again at the scale), [`editor_log.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/editor_log.cpp),
  [`editor_run_bar.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/run/editor_run_bar.cpp),
  [`scene_tree_editor.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/scene/scene_tree_editor.cpp),
  [`filesystem_dock.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/docks/filesystem_dock.cpp),
  [`inspector_dock.cpp`](https://github.com/godotengine/godot/blob/4.6-stable/editor/docks/inspector_dock.cpp),
  [`COPYRIGHT.txt`](https://github.com/godotengine/godot/blob/4.6-stable/COPYRIGHT.txt) (the licence of the icons),
  [Creating icons](https://docs.godotengine.org/en/stable/engine_details/editor/creating_icons.html).
- Roblox Studio: [Studio interface](https://create.roblox.com/docs/studio/ui-overview), [Testing
  modes](https://create.roblox.com/docs/studio/testing-modes), [Output](https://create.roblox.com/docs/studio/output),
  [Properties](https://create.roblox.com/docs/studio/properties), and the developer forum for the icon sizes (the
  documents give none): [Explorer icons](https://devforum.roblox.com/t/rbxassetid-of-the-studio-explorer-icons/860371),
  [plugin icon size](https://devforum.roblox.com/t/what-size-should-a-plugin-icon-be/1598548).
- The sets: [Lucide](https://github.com/lucide-icons/lucide) (release 1.63.0, its LICENSE),
  [Phosphor](https://github.com/phosphor-icons/core) and [its web package](https://github.com/phosphor-icons/web),
  [Tabler](https://github.com/tabler/tabler-icons), [Material Symbols](https://github.com/google/material-design-icons),
  [Codicons](https://github.com/microsoft/vscode-codicons), [Font Awesome
  Free](https://github.com/FortAwesome/Font-Awesome), [Bootstrap Icons](https://github.com/twbs/icons).
- Dear ImGui: [Fonts](https://github.com/ocornut/imgui/blob/v1.92.9b/docs/FONTS.md) (icon fonts, `MergeMode`,
  `GlyphMinAdvanceX`, FreeType, coloured icons) and
  [IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders).
- The licences: [the FSF's list](https://www.gnu.org/licenses/license-list.en.html), [LEGAL.md](repo:LEGAL.md#licences).

A few facts are from the editors as they are used, marked *(use)* above; the sizes of Roblox's icons are the
community's, not a document's. The pixel line widths are worked out from each set's grid and stroke.
