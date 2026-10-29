# Coney Studio: the prototype

Coney Studio's first program, `coney-studio`: a window with Studio's theme and its default docked layout, projects
made and opened, a sandbox layout drawn in the Level view with the fly camera and click selection, the Hierarchy and
the Inspector for the selection, and Play as *Run in Coney*. It is build issues 2 and 11 of the
[technology design](technology.md#build-issues), cut down to a prototype ([#1068](issue:1068)); everything else in the
designs waits for its own build issue. This page says how to build and run it, what each part does, where its code is
and what it leaves out.

## Building and running {#running}

`coney-studio` is built with the debug tools, beside `coney`, by the same command (the `dev`, `ci` and `playtest`
presets; the `release` preset, which has no debug tools, leaves it out):

```sh
uv run --project python coney-tools build --agent <id> --preset dev --target coney-studio
build/dev/src/platform/coney-studio.exe
```

The build puts `coney`, Coney's own assets (`assets/`) and Studio's fonts (`fonts/`) in the same folder, which is
where Studio looks for them.

| Option | What it does |
| --- | --- |
| `--project DIR` | Opens the project in `DIR` and its play level |
| `--new-project DIR` | Makes a project in `DIR` (named after the folder) and opens it |
| `--template NAME` | The first level of `--new-project`: `basic` (the default), `default`, `combat` or `parkour` |
| `--disc PATH` | The player's disc, for Run in Coney to play the level; `CONEY_DISC` otherwise |
| `--select N` | Selects entity `N` of the open level |
| `--frame` | Glides the Level view's camera to frame the selection, as *F* does |
| `--view NAME` | Puts the Level view's camera at the level's camera view `NAME` |
| `--frames N` | Runs `N` frames and quits, in lockstep with no clock (tests, screenshots) |
| `--screenshot PNG` | Saves the last frame of `--frames`, read from Studio's own frame buffer |
| `--window-size WxH` | The window's size (1600x900) |
| `--no-activate` | The window does not take the keyboard focus |

A picture for a pull request, with no click:

```sh
coney-studio --new-project <scratch>/yard --template combat --select 3 --frames 30 --no-activate \
    --screenshot <scratch>/studio.png
```

## What it does {#features}

| Part | In the prototype | Design |
| --- | --- | --- |
| Theme | Godot 4's *Modern* default: base `#292929`, accent `#569EFF`, contrast 0.3, 4 px corners, Inter for the interface (bold for the Inspector's headers) and JetBrains Mono for the Console, all at the display's scale; the panels stand on Godot's darker background with gaps between them, and the accent marks the selected tab, chosen rows, toggles, focus and Play. Colours, sizes and font files are one set of tokens (`ThemeTokens`), ready for the preferences | [Look and theme](editor-experience.md#theme) |
| Layout | Godot's arrangement, docked: Hierarchy over File system on the left, Level and Game views in the middle, Console under them, Inspector on the right; *Window > Reset layout* puts it back; the layout is kept per user (`coney-studio-layout.ini` in the user's data folder), never in a project | [The default layout](editor-experience.md#layout-default) |
| Projects | *File > New project* (a name, a folder, the first level) and *Open project*; the File system panel shows the project as `project:/` and nothing outside it | [The project](editor-experience.md#project-coney) |
| Level view | The open level drawn as `coney --sandbox` draws it, into a texture the panel shows; the spawn points, targets, fighters and camera views as coloured dots with their names; the selection outlined in the accent colour | [The Level view](level-view.md#level-view) |
| Fly camera | Right mouse held: move the mouse to look, W A S D to fly, Q and E to sink and rise, Shift faster, Alt slower, the wheel changes the base speed | [The fly camera](level-view.md#fly-camera) |
| Selection | A click in the Level view selects what is under it (the nearest primitive's triangle or point's box), a click on the sky clears it; the Hierarchy and the Inspector show the same selection | [Selection](level-view.md#selection) |
| Frame the selection | *F* (or a double-click on a Hierarchy row) glides the camera to frame the selection, as Unity's *Frame Selected*: the heading and pitch kept, the selection's bounding sphere filling the view's height, the glide easing in over half a second, slow at first and faster each step (the maintainer's request) | [The fly camera](level-view.md#fly-camera) |
| Hierarchy | The level's row (its settings in the Inspector), then one row per entity, flat, as a sandbox layout is | [The Hierarchy's tree](editor-experience.md#hierarchy-tree) |
| Inspector | The selection's values in groups (Identity, Transform, Shape, Look, Collision; the level's Lighting), drawn by the debug menu's own renderer, read-only | [The panels](editor-experience.md#panel-list) |
| Game view | A placeholder that says Play runs in Studio later, with a Run in Coney button | [Play](technology.md#play) |
| Play | *Run in Coney* (the toolbar's button, *Play > Run in Coney*, Ctrl+P) starts `coney` as a child process: with a disc, `--play-level sandbox:<level> --spawn <manifest spawn>`, Rembrandt at the spawn; without one, `--sandbox <level>`, flown round | [Build issue 11](technology.md#build-issues) |
| Console | Studio's messages and the run log's lines | [The panels](editor-experience.md#panel-list) |

A new project's folder is [the editor experience design's](editor-experience.md#project-coney): `coney-project.toml` (the
[manifest](lua-sdk.md#manifest)'s `[project]` and `[play]` tables), `levels/main.layout`, `textures/` with the
sandbox's textures and their CC0 licence copied in (a shipped course's `texture` lines are rewritten to
`../textures/`), `scripts/` and `.studio/`.

### Fonts {#fonts}

Inter 4.1 (`Inter-Regular.ttf`, `Inter-Bold.ttf`) and JetBrains Mono 2.304 (`JetBrainsMono-Regular.ttf`) come from
their release archives, pinned by URL and SHA-256 in `cmake/deps.cmake` and never in the repository; the build copies
them and their licences (SIL Open Font License 1.1, [LEGAL.md](repo:LEGAL.md#licences)) into `fonts/` beside
`coney-studio`. Without them Studio falls back to Dear ImGui's own font.

## Where the code is {#code}

| Code | What it holds |
| --- | --- |
| `src/studio/project.h`, `project_manifest.h` | The project folder, the `project:/` root rule and the manifest |
| `src/studio/level_document.h`, `level_templates.h` | A sandbox layout as a level of entities with ids; the Basic template and the shipped courses' copy |
| `src/studio/level_camera.h` | The fly camera, the view math, the click's ray and picking |
| `src/studio/inspector.h` | The Inspector's page as debug menu items |
| `src/studio/theme.h` | The design tokens and the Dear ImGui style derived from them |
| `src/studio/run_in_coney.h` | Run in Coney's command line |
| `src/studio/studio_app.h` | The editor's state and its interface (`studio_panels.cpp`); `StudioHost` is what it asks of the platform |
| `src/platform/studio_main.cpp`, `studio_host.h`, `studio_level_renderer.h` | The entry point, the SDL3 folder picker and child processes, and the Level view drawn by librw into a texture |

`src/studio/` touches no window, graphics API or operating system call itself, so its tests
(`tests/studio/`, the `[studio]` tag) run over a bare Dear ImGui context with no window, as the developer overlay's do:
the root rule, the manifest, a layout opened as entities, the Basic template, the camera, picking, the Inspector's
groups, the default layout's six docked panels, New and Open project, a selection and Run in Coney's command. The
`coney_studio.*` ctests (label `studio`) start the real program: `--help`, a refused option, and a new project opened
in a window with a screenshot, which reports itself skipped where there is no display.

## Not in the prototype {#not-yet}

Each waits for its own build issue of the [technology design](technology.md#build-issues) or the [editor experience
design](editor-experience.md#build-issues): the transform tools and gizmos (the toolbar's Q to Y are shown disabled),
editing in the Inspector, undo, saving a level, the Create menu, Play inside Studio and the Game view, the Dear ImGui
Test Engine harness, the script editor, the debugger, hot reload, importing, the `game:/` root, named layouts and the
theme preferences. Two stand-ins:

- **Entity order.** The Hierarchy lists the primitives in the layout's order, then the spawn points, targets, fighters
  and views, each in theirs, not every statement in file order: the sandbox's parser keeps a line number for
  primitives only.
- **The folder picker** is SDL's operating-system dialog; a test never opens it (its `StudioHost` answers instead).
