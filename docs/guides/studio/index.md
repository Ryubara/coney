# Coney Studio

Coney Studio is a planned editor for making content that runs on Coney: a modder opens a project folder, builds or edits
a level, places spawn points, characters, paths and scripts, presses **Play** and is in the game at once, with all of
Coney's behaviour (combat, AI, pathing, missions) working. It is to Coney what Roblox Studio is to Roblox: an editor for
one game's content, not a general engine like Unity. The parent issue is [#1054](issue:1054).

**Status: design.** Nothing in this folder is built. These pages are designs in the sense of [Page
kinds](../writing-docs.md#page-kinds): each one says what Studio should do and why, and names the questions only a
maintainer can answer. The maintainer's decisions so far are [below](#decisions). What gets built moves into a guide.

## What Studio is {#principles}

- **Its own program, and a reflection of Coney.** Studio does not copy the engine: it *realizes* the features Coney has
  in editor form. What Coney has decides what Studio has. A feature Coney lacks is not in Studio, and a native that is
  not in the Lua SDK does not exist in Studio. Studio is updated alongside Coney and grows with it.
- **Every Coney feature that applies to Studio is brought into Studio.** Each design page here ties every Studio feature
  to the Coney feature it realizes, and names the Coney feature a Studio feature waits on. A long-term Studio roadmap,
  written once these designs are done, keeps that list and the rule for deciding whether a new Coney feature belongs in
  Studio.
- **Nothing in Studio changes how the story plays.** Studio and its projects run Coney's engine with Coney's game logic;
  the faithful game is the same with or without Studio ([Faithful first](repo:AGENTS.md)).
- **No game data leaves the player's machine.** Studio reads the player's own disc and shows what it holds; a project
  refers to the disc's assets by name and never carries them ([LEGAL.md](repo:LEGAL.md#no-game-data)).

## The design pages {#pages}

One page per research unit, each a sub-issue of [#1054](issue:1054). A unit that merges its page adds its row here and
its line to the `nav:` of `mkdocs.yml`, under *Coney Studio design*.

| Page | What it decides | Issue |
| --- | --- | --- |
| [Editor experience](editor-experience.md) | The words (level, Level view, prefab), the panels, the Hierarchy's tree and the default layout, saved layouts, the project folder, a new level and its templates, Play mode and live edits while playing, the import flow, previews; the build issues of both pages | [#1055](issue:1055) |
| [The Level view, gizmos and composing](level-view.md) | The fly camera and the keymap, Unity's transform gizmo, selection and undo, composing a level from primitives, textures, colliders and path points | [#1055](issue:1055) |
| [Technology](technology.md) | The GUI library, the `coney-studio` program, how Play and live edits run, the code editor and language server, the debugger, hot reload, the importers | [#1056](issue:1056) |
| Script security and sandbox (planned) | How Studio and Coney keep a project's scripts from harming the player's machine | [#1062](issue:1062) |
| [Lua SDK and natives](lua-sdk.md) | The natives and their names, the Lua 5.4 runtime for projects and its loader, the manifest, the sandbox, typed stubs for completions, hot reload | [#1057](issue:1057) |
| Content and formats (planned) | What Studio shows and edits from the disc's files, a project's own formats, import and export | [#1058](issue:1058) |

## Terms {#terms}

A maintainer decision: Studio says *level* wherever other editors say *scene*, and so in every word made from it,
because *The Warriors* is made of levels and calls its in-engine cutscenes scenes
([Words](editor-experience.md#words-coney)).

| Term | Meaning |
| --- | --- |
| Project | A folder Studio creates: the root of the File system panel; nothing outside it is reachable ([The project](editor-experience.md#project)) |
| Panel | One dockable window of Studio: the Level view, the Inspector, the File system and the others ([Panels](editor-experience.md#panels)) |
| Level | What a user builds and plays, where other editors say "scene" ([Words](editor-experience.md#words-coney)) |
| Level view, Game view | The view that edits a level (Unity's Scene view) and the view of the game running |
| Entity | A thing in a level's tree: a primitive, a spawn point, a fighter, a camera view; it has a name and tags that scripts find it by; Coney's word, as in the Entity inspector ([Words](editor-experience.md#words-coney)) |
| `.cmod` | A finished project packed by Studio's Export for the mods folder (a working name) ([The project](editor-experience.md#project-coney)) |
| Prefab | A reusable group of any mix of entities, saved as one file and placed many times ([Words](editor-experience.md#words-coney)) |
| Layout | Where the panels sit, saved per user, never in a project ([Layouts](editor-experience.md#layouts)) |
| Natives | The script bindings a project's scripts call: the game's own and Coney's ([Script bindings](../../references/bindings/index.md)); the [Lua SDK](lua-sdk.md#terms) has the scripting terms |

## Maintainer decisions {#decisions}

- **Studio and faithful first: decided, no exception needed.** Studio is a reflection of Coney's progress: it offers
  only what Coney has, an enhancement or a native appears in Studio once Coney has it, and nothing in Studio changes how
  the story plays. So Studio never runs ahead of the faithful-first rule ([AGENTS.md](repo:AGENTS.md)): whatever Coney
  is allowed to have, Studio realizes, and nothing more.
- **The Hierarchy is the level, as in Unity: decided.** A free list of entities with free parenting, prefabs of any mix
  of entities, the level's settings on the level and sorting by kind as a filter ([The Hierarchy's
  tree](editor-experience.md#hierarchy-tree)).
- **Level, not scene: decided.** Studio says *level* wherever other editors say *scene* ([Terms](#terms)).
