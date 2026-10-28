# Coney Studio

Coney Studio is a planned editor for making content that runs on Coney: a modder opens a project folder, builds or
edits a level, places spawn points, characters, paths and scripts, presses **Play** and is in the game at once, with
all of Coney's behaviour (combat, AI, pathing, missions) working. It is to Coney what Roblox Studio is to Roblox: an
editor for one game's content, not a general engine like Unity. The parent issue is [#1054](issue:1054).

**Status: design.** Nothing in this folder is built. These pages are designs in the sense of
[Page kinds](../writing-docs.md#page-kinds): each one says what Studio should do and why, and lists the questions only a
maintainer can answer. What gets built moves into a guide.

## What Studio is {#principles}

- **Its own program, and a reflection of Coney.** Studio does not copy the engine: it *realizes* the features Coney
  has in editor form. What Coney has decides what Studio has. A feature Coney lacks is not in Studio, and a native that
  is not in the Lua SDK does not exist in Studio. Studio is updated alongside Coney and grows with it.
- **Every Coney feature that applies to Studio is brought into Studio.** Each design page here ties every Studio
  feature to the Coney feature it realizes, and names the Coney feature a Studio feature waits on. A long-term Studio
  roadmap, written once these designs are done, keeps that list and the rule for deciding whether a new Coney feature
  belongs in Studio.
- **Nothing in Studio changes how the story plays.** Studio and its projects run Coney's engine with Coney's game logic;
  the faithful game is the same with or without Studio ([Faithful first](repo:AGENTS.md)).
- **No game data leaves the player's machine.** Studio reads the player's own disc and shows what it holds; a project
  refers to the disc's assets by name and never carries them ([LEGAL.md](repo:LEGAL.md#no-game-data)).

## The design pages {#pages}

One page per research unit, each a sub-issue of [#1054](issue:1054). A unit that merges its page adds its row here
and its line to the `nav:` of `mkdocs.yml`, under *Coney Studio design*.

| Page | What it decides | Issue |
| --- | --- | --- |
| [Editor experience](editor-experience.md) | The panels and the default layout, saved layouts, the project folder, Play mode, scene navigation and the keymap, gizmos, selection and undo, the import flow | [#1055](issue:1055) |
| Technology (planned) | The GUI library, one program or an editor mode of `coney`, how Play runs the engine, the code editor and language server, hot reload | [#1056](issue:1056) |
| Lua SDK and natives (planned) | The natives and their names, the mods runtime and loader, resources, the sandbox, typed stubs for completions | [#1057](issue:1057) |
| Content and formats (planned) | What Studio shows and edits from the disc's files, a project's own formats, import and export | [#1058](issue:1058) |

## Terms {#terms}

| Term | Meaning |
| --- | --- |
| Project | A folder Studio creates: the root of the File system panel; nothing outside it is reachable ([The project](editor-experience.md#project)) |
| Panel | One dockable window of Studio: the Scene view, the Inspector, the File system and the others ([Panels](editor-experience.md#panels)) |
| Layout | Where the panels sit, saved per user, never in a project ([Layouts](editor-experience.md#layouts)) |
| Natives | The script bindings a project's scripts call: the game's own and Coney's ([Script bindings](../../references/bindings/index.md)); the Lua SDK page decides the rest of the scripting terms |

## Maintainer decisions {#decisions}

- **Studio's content and faithful first.** Studio's tooling (the editor, its viewers, its panels) is debug tooling,
  which the rules allow now. Custom levels, Coney natives and Studio's own lights are enhancements, which the rules
  hold until the game is complete. Proposal: Studio and its projects run in a mode of their own, the story stays
  faithful and unchanged, and the faithful-first rule gets this exception. A change to that rule is a maintainer
  decision ([AGENTS.md](repo:AGENTS.md)).
