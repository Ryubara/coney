# Coney Studio: UI and menus for mods

The design of how mods build user interface and add to Coney's menus, and what Coney Studio offers for it: how the
original's front end and HUD work and how far Coney has ported them, what FiveM, Roblox, Garry's Mod and World of
Warcraft give their creators, the extension points where mods add to the menus while the game without mods keeps its
faithful ones, the UI natives (the game's own and Coney's), input focus and gamepad navigation, how Studio loads the
main menu with its extension points and edits a mod's screens, what stays out for the players' safety, and the tests
that prove a mod's menu works with a pad and a keyboard. It ends with the build issues it proposes, in order. The parent
issue is [#1054](issue:1054); this page is [#1063](issue:1063).

**Status: design.** Nothing on this page is built. It is a design in the sense of
[Page kinds](../writing-docs.md#page-kinds): what is built moves into a guide. It builds on the
[Lua SDK and natives](lua-sdk.md) design (its terms, the `Coney` table, project scripts in Lua 5.4, the sandbox) and the
[technology](technology.md) design (Dear ImGui, the Test Engine, Play in Studio's process), and does not repeat them.
What the original does is on the research pages [Start-up and the front end](../../research/frontend.md),
[GUI](../../research/gui.md) and [The in-game HUD](../../research/hud.md); this page cites them and adds the few
readings it needed.

Two rules from the parent issue shape every section. **Studio offers only what Coney has:** a UI native or a widget
reaches mods once Coney implements it, and each one below says whether Coney has it today or what it waits on. **The
unmodded game keeps its faithful menus:** with no mod installed, every screen is the original's, item for item.

## Contents {#contents}

1. [Terms](#terms); [how UI works in the original](#original); [how UI works in Coney today](#coney); [what other
   platforms do](#prior-art); [the design at a glance](#summary).
2. [Extending the menus](#extending): the faithful menus, the extension points, order and clashes, the MODS entry,
   entries, menu data events, option rows, a mission selector.
3. [The UI natives](#natives): the game's own, Coney's, widgets, focus and navigation, HUD elements, where UI runs.
4. [Studio](#studio); [Safety](#safety); [Tests](#tests).
5. [What changes on other pages](#other-pages), [Maintainer decisions](#decisions), [Open questions](#open-questions),
   [Proposed build issues](#build-issues), [Sources](#sources).

## Terms {#terms}

| Term | Meaning |
| --- | --- |
| **Front end** | Everything between the legal screen and a level: the profile manager's screens (the main menu `PM_Mode` and the rest), the Rumble set-up menu, over the front-end level `level100` ([Start-up and the front end](../../research/frontend.md#mode-flow)) |
| **Screen** | One page of a menu that takes the pad: a front-end screen, a pause page, or a mod's screen. In the original a screen is a widget with a screen-flow state ([GUI: the screen flow](../../research/gui.md#screen-flow)) |
| **Widget** | One element of a screen or the HUD: a text, a sprite, a grid of items, a scrolling list, a bar. The game's widget classes are on [GUI: the widget classes](../../research/gui.md#widget-classes) |
| **Mod screen** | A screen a project defines, from a screen file or from its scripts, built of the game's widget kinds |
| **HUD element** | A mod's widget group shown during play without taking the pad: drawn with the HUD, hidden with it |
| **Trusted screen** | A screen only Coney's own code may open, draw over or drive: the Mods screen, a mod's details and approvals, and the profile, save and quit screens. A mod can never draw over, read or press anything on one ([Safety](#no-spoofing)) |
| **Menu entry** | An addition with a title, a description and an action, from `Coney.AddMenuEntry` or the manifest ([Entries](#entries)) |
| **Extension point** | A section of the front end, the pause menu or the Rumble screens built to take additions: what it takes (an entry, an option row, a team, a mission, a sub-screen) and where the additions go ([Extension points](#points)) |
| **Addition** | One thing a project adds at an extension point, named `<project>/<id>`, placed after the game's own items in load order ([Order and clashes](#order)) |
| **Menu data event** | The moment the game builds one of its data-driven lists (Rumble modes, gangs, arenas), at which a project's handler adds records with the game's own natives ([Menu data events](#data-events)) |
| **Frame label** | The project's title, drawn by Coney beside every mod screen and dialog so the player always knows who is asking ([Safety](#no-spoofing)) |
| **Screen view** | Studio's panel that edits a screen file, as the Level view edits a level ([The Screen view](#screen-view)) |

The scripting words (native, game native, Coney native, project, project state, server and client scripts, available)
are the [Lua SDK's](lua-sdk.md#terms); the editor's (project, Level view, Game view, entity, panel) are the editor
experience design's ([#1055](issue:1055)).

## How UI works in the original {#original}

### Modes, screens and widgets {#original-structure}

The game is a stack of game modes ([Boot: game modes](../../research/boot.md#game-mode)); the mode on top runs, reads
the pads and draws. Each menu is a mode: the profile manager is mode 0x12, the Rumble set-up 0x11, the mission select
0x10, the statistics screen 0x13, the Rumble result 0x14; the pause and failure menus are modes over gameplay
([Pause](../../research/pause.md)). Inside a menu mode, screens are chained by a **screen flow**
(`ScreenFlowController_Update` `0x001c83c8`): each screen ends a frame with a result code, and the flow stays, pops, or
pushes the screen that code leads to (confirmed (code), [GUI: the screen flow](../../research/gui.md#screen-flow)).
Every screen element is a C++ widget derived from one base (`Widget_Construct` `0x001a8e30`): sprites, texts with
markup, option grids, scrolling lists, bars, headers, message boxes; every image and glyph is a rectangle of a sprite
sheet drawn as a particle in the 2D pass, ordered by the batch's depth (confirmed (code), [GUI: a frame of
2D](../../research/gui.md#a-frame-of-2d)).

### The menus are code; their lists are data {#original-data}

**The menus' layout and flow are in code.** The main menu's three items are added in `PM_Mode_Init` (`0x00209da8`): an
`OptionGrid` of rows {2, 1} with global strings `0x78` STORY (code 0, with the `" : "` separator), `0x8a` EXTRAS (code
5) and `0x79` QUICK RUMBLE (code 1), or one row of STORY and QUICK RUMBLE with the device flag `0x02` (confirmed (code),
read for this page; the screen's layout is on [Front end: the screens](../../research/frontend.md#pm-screens)). The
screens and their transitions are wired in `PM_Controller::PM_Controller` (`0x002040f0`, confirmed (code), [The profile
manager](../../research/frontend.md#profile-manager)). No file on the disc describes a menu.

**The lists the menus show are data, filled by Lua.** Each Rumble screen runs a chunk from the disc when it opens, and
the chunk's `CfgRumbleGame`, `CfgRumbleGang` and `CfgRumbleArena` calls build its list (confirmed (code),
[Where the Rumble data lives](../../research/frontend.md#rumble-data)). `RM_ChooseGangs_Init` (`0x001ecae0`) first
adds the player's custom gangs from the gang store, each with id -1, then runs `rumble_gang.lua` through the script
system's run-a-file slot `+0x44`; `RM_ChooseGangs_AddGang` (`0x001ec980`) appends a record (id, interned name, the
nine-type roster with locked types replaced) to the list at `0x0063ee4c` when the id is negative or the gang is
unlocked, in call order (confirmed (code), read for this page). The levels come from `CfgLevelName` calls in
`config_preload3.lua` (`LevelRecord_Set` `0x0041f118`, confirmed (code),
[The level table](../../research/frontend.md#the-level-table)), and the mission select lists the level records whose
mission is complete (`MissionSelect_IsMissionListed` `0x001bc100`, confirmed (code),
[`MissionSelectHUD.cpp`](../../research/frontend.md#fn-missionselecthud)). The UI strings are Lua tables passed to
`CfgHUDMessage` and its siblings, kept by id (`GlobalString_Set` `0x0019eea0`, confirmed (code),
[GUI: strings](../../research/gui.md#strings)).

So the original already has the seam a mod needs for its lists: a Rumble gang is one more `CfgRumbleGang` call at the
moment the screen runs its chunk. The menus themselves have none.

**Menus hand control back to Lua by callback names.** `ProfileManager_Show` (`0x001552b0`, the binding
`ShowProfileManager`) keeps two Lua function names and pushes mode 0x12 unless it is on top (confirmed (code), read for
this page); the main menu's QUICK RUMBLE calls the first, `Menu.fadeToRMI`, and the end of the menus the second,
`Menu.startGame` (confirmed (code), [Front end: PM_Mode's handler](../../research/frontend.md#pm-screens)). The Rumble
menu, the mission select and the result screens work the same way ([QUICK
RUMBLE](../../research/frontend.md#quick-rumble)).

### Input and focus {#original-input}

Confirmed (code) at the cited functions unless a line says otherwise; the detail is on
[Front end: input](../../research/frontend.md#input).

- **Focus is the mode stack.** Only the top mode reads the pads, so a menu pushed over gameplay takes the input and
  gameplay stops until it pops.
- **A widget reads its player's virtual pad**, handed out by the HUD (`HUD_GetVirtualPad` `0x001ae898`).
  `MenuInput_Dispatch` (`0x001e95c0`) turns the pad into six commands (up, down, left, right, accept on the release of
  cross, back on the release of triangle or circle), at most one per 110 ms, 400 ms for a held stick, with the d-pad's
  auto-repeat after 0.5 s.
- **Taking focus** (`OptionGrid_Focus` `0x001d4d28`) clears the input record and blocks the d-pad for 20 ms, so the
  press that opened a screen does not also move it.
- **Every move and choice plays a front-end cue**: 4 or 5 a move, 8 or 9 an accept, `0xe` a refused move, `0xf` back
  ([Audio cues](../../research/frontend.md#audio-cues-references-only)).
- **Lua pad handlers** set with `PadSetHandler` run from the pad update for each newly pressed button (`Pad_Update`
  `0x00144fb0`, `Pad_SetLuaHandler` `0x00145698`).

### What the game's scripts can do with UI {#original-natives}

The game registers 956 Lua globals (`RegisterScriptBindings` `0x0037d420`); 78 are in the HUD and menus category
([Script bindings: HUD and menus](../../references/bindings/hud.md)). Every one is a **fixed-purpose** call into a part
the C++ already built: set an objective line, queue a hint, take a counter panel, fill a bar, add a radar blip, hide or
restore the HUD, open a menu mode with callback names. **No binding creates a widget, a sprite or a screen**, and no
binding draws text at a position: the scripts never lay out UI (confirmed (code), from the masterlist built from the
registration).

The disc's `level100main.lua`, a Lua-driven menu with its own dialogs, calls `Simon_*`, `CreateCenteredSprite` and
`GetDefaultUIFont`, which the executable never registers: no defined string of the executable holds any of those names
(confirmed (code), a search of the executable's strings for this page; that the script is a prototype or another
platform's menu stays speculative, [Front end: the front-end
scripts](../../research/frontend.md#the-front-end-scripts)). So the retail game's menus are C++, and a general UI API
for scripts is Coney's to add.

## How UI works in Coney today {#coney}

| Piece | Where | State |
| --- | --- | --- |
| The mode stack, each mode an update and a render | `src/gamemodes/game_mode_stack.h` | Built; the original's ids 1 to 0x14, Coney's own modes from 0x100 |
| The screen flow | `src/gui/screen_flow_controller.h` | Built: push, pop, unwind, transitions, results "stay" and "back" |
| Widgets: `Widget`, `BaseWidget`, `TextWidget` with markup, `OptionGrid`, `ScrollingMenu`, `UsageInfo`, `Bar`, `CircledText`, `CircledTextHeader`, the message box, the name keyboard | `src/gui/` | Built from the original's classes, `@orig` tagged ([GUI: Coney's implementation](../../research/gui.md#coneys-implementation)) |
| Text: fonts as sprite sheets, the 66 markup tags, layout | `src/graphics/font.h`, `src/gui/markup.h`, `src/gui/text_layout.h` | Built; `<BOLD>`, `<BGFONT>` and `<AUTOINDENT>` have no effect, as in the original or by choice |
| UI strings from the disc's Lua | `src/gui/global_strings.h` | Built: a map by id, any id works |
| Menu commands, the cues | `src/gui/menu_input.h` | Built: `MenuInput_Dispatch`'s commands and timings |
| The front end: legal screen, memory-card check, level flow, profile manager with all fourteen screens | `src/gamemodes/`, `src/gui/profile_management_gui/` | Built; the two Xbox screens are placeholders ([Front end: Coney's implementation](../../research/frontend.md#coneys-implementation)) |
| The Rumble set-up menu | `src/gamemodes/rumble_menu_mode.h`, `src/gui/rumble_mode_gui/` | Built with stand-ins: lists from the disc's chunks through Coney's `CfgRumble*`; Choose Area's framed previews, Choose Gangs' name boxes, badges and 3D fighters are missing |
| Pause, options, controls, mission failed, Armies game over, yes/no box | `src/gui/pause_menu/` | Built |
| The HUD: player panel, messages, hint box, counter panels, scripted bars, radar, prompts, credits | `src/hud/` | Built; 59 of the 78 HUD bindings are implemented, one partial (`ShowRumbleModeInterface`) |
| The mission select (mode 0x10) | none | **Missing**: `HUDShowMissionSelect` is a real binding whose host does nothing (`showMissionSelect` in `src/scripting/script_bindings.h` has no override) |
| The debug menus: one model of pages and items, drawn by the pad menu in the game's font and by the Dear ImGui overlay | `src/debug/`, `src/gui/debug_menu_view.h` | Built ([One model](../debug-menu.md#one-model)); debug tooling, never in a release build |
| The input gate: port 1 taken from the game while the debug menu is open | `src/debug/input_gate.h` | Built ([Controls](../debug-menu.md#controls)) |
| Keyboard and gamepads as the PS2 pad; input scripts | `src/platform/sdl_input.h`, `src/core/input_script.h` | Built ([Controls and input scripts](../running-input.md#controls)); scripts drive pad buttons and sticks, not keys |
| Anything a mod can draw or add to a menu | none | None: there is no mod runtime ([Lua SDK](lua-sdk.md)) |

Two things follow. **The widgets mods need exist**, as faithful ports, so a mod's screen can be drawn with the game's
own look and feel and needs no new renderer. **The debug menus already prove a data model drawn by front ends**: pages
of typed items, rendered by a generic renderer, driven by a scripted pad in tests. Mod screens take the same shape.

## What other platforms do {#prior-art}

Only public documentation and well-known behaviour were used; no code is taken from any of them.

| Platform | How creators build UI | Focus and gamepad | What protects players |
| --- | --- | --- | --- |
| **FiveM** (GTA V) | Three ways: **NUI**, a web page per resource in Chromium (CEF) over the game, talking to Lua by messages; **native menus**, libraries (NativeUI, RageUI and the like) that draw rectangles, sprites and text with the game's draw natives every frame; **Scaleform**, the game's own Flash movies (instructional buttons, warnings) driven by method calls | NUI takes keyboard and mouse with `SetNuiFocus`; native menus read the controls themselves each frame | Little at the UI level: a page can show anything, so a server can put up any look, including fake system prompts; the protection is that a player chooses the server |
| **Roblox** | `ScreenGui` objects holding a retained tree of `GuiObject`s (frames, labels, buttons, images, layouts), built in Studio's UI editor or by script, cloned to each player | `GuiService.SelectedObject` is the gamepad's focus; each object is `Selectable` with neighbours for the four directions, and an automatic navigator fills the rest | **CoreGui**: Roblox's own menu, purchase prompts and player list live in a container game scripts cannot read or change, and the Roblox menu is always one button away |
| **Garry's Mod** | **Derma**: Lua panels over the engine's VGUI (frames, buttons, lists), retained, made by code; and **DHTML** panels, a Chromium page whose Lua access is off unless the panel allows it | Mouse first; the game is a PC game | Client Lua runs in a client state; the **menu state** (the game's own menus) is not open to addons, the wiki saying it is unsupported for security because the game has no client permission system |
| **World of Warcraft** | **Frames** in XML and Lua: a retained tree of frames, textures and font strings with event handlers; Blizzard's own interface is written the same way and addons may change it | Mouse and keyboard; key bindings | **Secure execution and taint**: code that touches an addon's data is tainted and cannot call protected functions (casting, targeting) or change protected frames in combat, so an addon can rearrange the UI but cannot act for the player; the login screens are not open to addons |

What Coney takes:

- **A retained tree of the game's own widgets** (Roblox, Derma, WoW frames), not FiveM's per-frame drawing: data a test
  queries by id, Studio edits, a replay names and a network message carries, drawn with the game's look by construction.
- **No web pages**: NUI and DHTML bring a browser, its network stack and pages that can look like anything.
- **Directional focus with neighbours** (Roblox's selection) on top of the game's own six menu commands.
- **A protected layer** (Roblox's CoreGui, WoW's secure frames): Coney's own screens and the actions that change what
  runs are reachable only from Coney's code. And **a visual editor** for screens, as Roblox Studio's UI editor.

## The design at a glance {#summary}

| Question | Decision | Section |
| --- | --- | --- |
| The unmodded game's menus | Unchanged, item for item; nothing below exists while no mod is installed | [Faithful](#faithful) |
| How mods change the menus | They **add** at **extension points**, sections built to take additions; they never rewrite, hide or reorder the game's screens. Visual changes to the game's own art are asset override packs ([#1058](issue:1058)) | [Extension points](#points) |
| Which points | The Mods screen (behind a **MODS** item on the main menu of a modded install), the Rumble Game Mode, Choose Gangs and Choose Area lists, the mission select, and the pause menu's Options (as a MOD OPTIONS page) | [Extension points](#points) |
| Several mods at one point | Additions follow the game's items, grouped by project in load order, each named `<project>/<id>` | [Order and clashes](#order) |
| Adding entries | `Coney.AddMenuEntry(point, def)` from a script, or `[[menu.entry]]` in the manifest (what Studio writes): title, description, action | [Entries](#entries) |
| New Rumble teams, modes, arenas | **Menu data events**: when the game builds the list, a project's handler calls the game's own `CfgRumbleGang` and its siblings | [Data events](#data-events) |
| Mod settings | `Coney.AddOption(def)`: a toggle, a choice, a bar or a sub-screen row on the project's page under MOD OPTIONS | [Option rows](#options) |
| UI natives | The game's 78 HUD and menu natives as they are, plus Coney natives that build screens and HUD elements from the game's widget kinds | [Natives](#natives) |
| Focus and navigation | A mod screen takes its player's pad as a game mode or a screen-flow state does, with the game's six commands, cues and timings; focus moves by order or named neighbours; HUD elements never take input | [Focus](#focus) |
| Studio | Screens are their own asset kind (`ui/<name>.screen`) edited in a **Screen view**; the front end and the pause menu open there read-only with their extension points marked and the project's additions in place; Play can start at the main menu | [Studio](#studio) |
| Safety | No web views, no network, no mouse capture, no free text fields; trusted screens on a layer mods cannot touch; a frame label on every mod screen; back always closes a mod screen | [Safety](#safety) |

## Extending the menus {#extending}

The model, a maintainer's: **the Lua SDK's natives extend the menus by adding to sections built to be added to** (the
Options settings, the Rumble menu's teams, a mission selector), and never rewrite the game's screens. A mod adds; it
does not hide, rename, move or replace what the game shows. Changing how the game's own screens look (higher
resolution art, other textures) is a different layer, the asset override packs of the content design
([#1058](issue:1058)), and not this page's.

### The faithful menus stay as they are {#faithful}

With the mods folder empty or absent, Coney runs the faithful game ([The mods folder](lua-sdk.md#mods-folder)): no
MODS item, no MOD OPTIONS page, no data event fires and no addition exists, so every screen is the original's, item for
item. The disc checks of the front end ([Front end: Coney's
implementation](../../research/frontend.md#coneys-implementation)) hold as they are, and a test holds that a run with no
mods gives the same screens, items and layout as today ([Tests](#tests)).

### The extension points {#points}

Each point is a list the game already has, or a place Coney adds for the purpose, with what it takes and where the
additions are drawn. A point's name is what `Coney.AddMenuEntry` and the manifest use.

| Point | Where | Takes | Drawn | Coney today |
| --- | --- | --- | --- | --- |
| `mods` | The Mods screen's *Mod content* ([MODS](#mods-screen)) | Entries: play the project's level, open a sub-screen, send a request | Under the project's title, in load order | The screen is Coney's to build (build issue 3) |
| `rumble.modes` | The Rumble Game Mode list ([Per screen](../../research/frontend.md#rumble-screens)) | A game mode record (`CfgRumbleGame` in [`rumbleModes`](#data-events)), or an entry that opens the project's own fight set-up | After the game's modes, title over description as theirs | The screen and the native are built; a new mode needs its rules in the game state (script overrides, [the compiler](lua-sdk.md#compiler)) |
| `rumble.gangs` | Choose Gangs | A team: `CfgRumbleGang` in [`rumbleGangs`](#data-events) | After the custom gangs and the disc's | **Ready**: a team of the game's character types plays |
| `rumble.arenas` | Choose Area | An arena: `CfgRumbleArena` in [`rumbleArenas`](#data-events) | After the disc's | Waits on project levels that are arenas ([#1058](issue:1058)) |
| `missions` | The mission select (mode 0x10, [`MissionSelectHUD.cpp`](../../research/frontend.md#fn-missionselecthud)) | A mission: the project's level or mission file, its checkpoints, an area of the project's with its own map marker | After the game's areas | Waits on a port of the mission select screen, which Coney lacks ([How UI works in Coney today](#coney)) |
| `options` | The pause menu's Options title list ([The Options screen](../../research/pause.md#options)) | Option rows: a toggle, a choice, a bar, or a row that opens a sub-screen ([Option rows](#options)) | One title, MOD OPTIONS (Coney's text), after the game's seven, opening one page per project | The Options screen is built; the page is Coney's to add (build issue 7) |
| `hud` | During play | HUD elements ([HUD elements](#hud)) | After the game's HUD parts | Waits on the widget model (build issue 2) |

Sections that are **not** points: the main menu's own items (MODS is Coney's, [below](#mods-screen)), EXTRAS (its one
item, TRAILER, stays alone: a maintainer decision), the pause menu's grid, the profile, save and card screens, the
statistics screen and the game's result screens. Their layouts are fixed in code by the original and leave no room; a
project reaches the player from the points above. A new point is added by a Coney change, with its test, when a project
needs one.

### Order and clashes {#order}

Several mods add to the same point without stepping on each other or on the game:

1. **The game's items first, unchanged.** Additions are placed after the game's own items and never between them, so
   the game's indices, defaults and cursor start stay as in the original (Choose Gangs still starts on BASEBALL FURIES
   against ORPHANS on a fresh boot).
2. **By load order, then by registration order.** The projects' additions follow the load order of `enabled.toml`
   ([#1062](issue:1062)); inside one project, the order its scripts or manifest added them. The same mods in the same
   order give the same menus every run.
3. **Names are the project's.** Every addition is `<project>/<id>` (`yard-brawl/rounds`), so two projects never name the
   same thing, and a project adding an id twice replaces its own addition, never another's. A Rumble gang's id is given
   by Coney from a range for mods ([Data events](#data-events)), so two projects' gangs never share one.
4. **Grouped and labelled.** Where the point has room (the Mods screen, MOD OPTIONS), a project's additions sit under
   its title; in the game's own lists (gangs, modes) each addition keeps its own text, and the Mods screen lists what
   each project added where.
5. **Nothing removes.** No native or manifest field hides, renames, moves or replaces a game item or another project's
   addition. Switching a mod off removes its additions the next time the screen opens.

### The MODS entry and the Mods screen {#mods-screen}

When at least one mod is installed, enabled or not, the main menu gains **MODS** (Coney's own text, code 9): STORY :
EXTRAS over QUICK RUMBLE : MODS, the second row joined by the grid's `" : "` separator, at the same place, font, colours
and cues as the original's items (rows {2, 2} in place of {2, 1}; with the flag `0x02` one row of three). It leads to
the **Mods screen**, a trusted screen of Coney's, drawn with the profile manager's look (left-aligned title, a scrolling
list, the usage line):

1. **Installed mods**, in load order, each on or off, with its load mode; accept opens its **details**, the panel the
   security design defines (name, version, author, signer, what it changes, its permissions and limits,
   [#1062](issue:1062)), to which this page adds **what it adds where** (its additions at each point) and its option
   rows. On and off here or at the console; a permission only here, never by a console command ([#1062](issue:1062)).
2. **Mod content**: the `mods` point, every enabled project's entries grouped under its title, and each content mod that
   starts by hand (its level, [#1062](issue:1062)).

The Mods screen is Coney's: a project adds to it only through entries, which Coney draws, and can never change, hide or
cover it ([Safety](#no-spoofing)). MODS is the one item Coney adds to the main menu, and only on a modded install: a mod
is something the player chose to install, and the way to switch it off must be one step from where the game starts.
A maintainer decided it ([Maintainer decisions](#decisions)).

### Entries {#entries}

An entry is an item with a title, a description and an action, added at the `mods`, `rumble.modes` or `missions`
point. A script adds it with `Coney.AddMenuEntry` in its `start` handler, and it lasts for the session:

```lua
Coney.On("start", function()
  Coney.AddMenuEntry("mods", {
    id = "rounds",                               -- yard-brawl/rounds
    title = "YARD BRAWL",                        -- the project's own text
    description = "Five rounds in the yard.",    -- shown where the point shows descriptions
    action = "screen", screen = "project:/ui/rounds.screen",
  })
end)
```

The manifest holds the same as data, which is what Studio writes and what needs no script:

```toml
[[menu.entry]]
point = "mods"
id = "rounds"
title = "YARD BRAWL"
description = "Five rounds in the yard."
action = "screen"                        # play | screen | request
screen = "project:/ui/rounds.screen"
```

- **`play`** starts the project's `[play]` level as a content mod ([Play, step by step](lua-sdk.md#play)).
- **`screen`** opens a [mod screen](#widgets), a whole sub-screen, on the menu's own screen flow, so back returns to the
  screen under it as the game's screens do.
- **`request`** queues a [request](#apiset) to the project's server scripts, which do what they like (start a level at
  a checkpoint, open a screen).

The entry's text is the project's own, never a game string id, so a project carries no game text
([LEGAL.md](repo:LEGAL.md#no-game-data)); a project that wants several languages gives `title.de = "..."` and so on,
chosen by the game's language (`GetLanguage`).

### Menu data events: Rumble teams, modes and arenas {#data-events}

The Rumble lists stay the game's: a project adds records **with the game's natives, at the moment the game builds the
list**, exactly as the disc's chunk does. A handler registers with `Coney.On`, the loader's event function
([Entry points](lua-sdk.md#events)):

| Event | Fired | Natives a handler calls |
| --- | --- | --- |
| `rumbleModes` | When the Game Mode screen builds its list, after `rumble_data.lua` | `CfgRumbleGame` |
| `rumbleGangs` | When Choose Gangs builds its list, after the custom gangs and `rumble_gang.lua` | `CfgRumbleGang` |
| `rumbleArenas` | When Choose Area builds its list, after `rumble_arena.lua` | `CfgRumbleArena` |

A Rumble team, from a project's server script:

```lua
-- scripts/teams.lua: a new gang for QUICK RUMBLE, made of the game's own character types.
Coney.On("rumbleGangs", function()
  -- The gang's id comes from Coney's range for mods, so it never names a disc pack or another mod's gang.
  local id = Coney.ModGangId("yard-dogs")
  CfgRumbleGang(id, "YARD DOGS", {91, 92, 93, 94, 91, 92, 93, 94, 91})
end)
```

- **Records go after the game's**, project by project in load order ([Order and clashes](#order)).
- **Ids.** A gang's id names its character pack (`gang1Pak` is the id less one) and its unlock record. A mod gang takes
  an id from a range Coney keeps for mods (`Coney.ModGangId(name)`, stable per project and name), is always unlocked,
  and its fighters are loaded type by type when no disc pack has its id: a stand-in until the packs are traced, an
  [open question](#open-questions).
- **Called outside its event**, `CfgRumbleGang` and its siblings add to a list nobody shows; the native says so in the
  Console.
- **Custom characters** in a team (an imported model) wait on custom characters in Coney ([#1058](issue:1058)).

### Option rows {#options}

The pause menu's Options is a list of seven page titles, each opening a page of rows edited in place ([The Options
screen](../../research/pause.md#options)). With any enabled mod adding options, Coney adds one title after the seven,
**MOD OPTIONS**, which opens a list of the projects that added rows, in load order; each opens the project's page, laid
out and driven as the game's pages are (labels at size 1.0, the selected one grey, up and down between rows, left and
right on a row, cues 4, `0xe` and `0xf`).

```lua
Coney.On("start", function()
  Coney.AddOption{ id = "rounds", label = "ROUNDS", kind = "choice", choices = {"3", "5", "7"}, default = 2 }
  Coney.AddOption{ id = "crowd", label = "CROWD", kind = "toggle", default = true }
  Coney.AddOption{ id = "roster", label = "EDIT ROSTER", kind = "screen", screen = "project:/ui/roster.screen" }
end)
-- Anywhere later: the value the player chose.
local rounds = Coney.GetOption("rounds")
```

- **Kinds** are the game's rows: `toggle` and `choice` as the Camera and Video pages' choices, `bar` (0 to 100 in steps
  of 5) as the Audio page's bars, and `screen`, a row that opens a sub-screen of the project's.
- **Values** are kept per project in the project's own store ([#1062](issue:1062)), never in the game's options or the
  profile, so switching a mod off leaves the player's options exactly as they were. Restore Default restores the game's
  values only, as in the original; each project's page ends with its own *Restore Default*.
- **A change** runs the project's `Coney.On("option:" .. id, fn)` handlers at the next step boundary.
- **The manifest form** is `[[menu.option]]` with the same fields, which Studio writes ([Studio](#studio-frontend)).
- **The same page opens from the Mods screen**, from the project's details, so a mod's settings can be changed on the
  front end too.

### A mission selector {#mission-selector}

The maintainer's example, by two routes:

- **At the `missions` point.** Once Coney has ported the game's mission select (mode 0x10, `HUDShowMissionSelect`), a
  project adds its missions there with `Coney.AddMenuEntry("missions", {...})`: its level or mission file, its
  checkpoints and an area of its own. The game's areas and their completed missions come first, as the original lists
  them (`MissionSelect_IsMissionListed` `0x001bc100`, completed missions of levels 2 to 99, confirmed (code)).
- **As a project's own screen**: an entry at `mods` whose action opens a [mod screen](#widgets) with a list
  of the levels a script chooses, `accept` starting one at a checkpoint. It needs a list widget, the level table
  (`Coney.GetLevels`, a read of the records the game's `CfgLevelName` calls made) and `Coney.StartLevel`
  ([First Coney natives](lua-sdk.md#build-issues)); all three have their C++ today. The [example](#example) below is
  this selector.

## The UI natives {#natives}

### The game's own UI natives {#game-natives}

Every game native a UI needs is exposed to projects **as it is**, under the game's name and arguments
([Names](lua-sdk.md#names)), once implemented and allowed ([Availability](lua-sdk.md#availability)). Most are in the
[HUD and menus](../../references/bindings/hud.md) category; the menu data, the fades and the pads are in other
categories of the [masterlist](../../references/bindings/index.md). Coney today:

| Group | Natives | Implemented | Not implemented |
| --- | --- | --- | --- |
| Objectives, hints, announcements | `HUDSetObjective`, `HUDSetTutorialText`, `HUDCheckTutorialText`, `HUDFlushTutorialText`, `HUDRemoveAllGoalText`, `HUDSetAnnounceMsg` (kind 5 takes the script's own text) | all six | none |
| Counters, bars, scoreboards | `HUDGetNewPH`, `HUDReleasePH`, `HUDSetPHValue`, `HUDEnableBar`, `HUDSetBarPercentage`, `HUDSetBarProperty`, `HUDEnableTextProgress`, `HUDSetTextProgress`, `HUDSetNumIndicator` | all nine | `HUDSetPHLabelFlashing`, `HUDEnableGenBar` |
| The radar | `HUDTurnOnRadar`, `HUDTurnOffRadar`, `HUDAddRadarMissionObjective`, `HUDSetRadarItemTexture`, `HUDDeleteRadarObject` and seven more | twelve | `HUDAddRadarObject`, `HUDDeleteRadarItem`, `HUDChangeRadarMissionObjective` |
| Showing and hiding | `HideHud`, `RestoreHud`, `HidePlayerHud`, `ShowPlayerHud`, `ForceShowPlayerHud`, `FlashRageBar` | all six (`ShowHud` does nothing in the original either) | none |
| Prompts and pointers | `HUDTurnOnActionCycleAnim`, `HUDTurnOffActionCycleAnim`, `HUDEnableInstArrow`, `HUDEnableClubActionText`, `HUDEnableFixedCamIcon` | all five | `HUDSetInstArrowLocation`, `HUDSetInstArrowColor` |
| Screens and flow | `ShowGameStatsInterface`, `HUDLaunchRumbleWin`, `HUDLaunchMissionComplete`, `HUDLaunchMissionFailed`, `ShowRumbleModeIntro`, `PreloadCredits`, `ShowCredits`, `HUDShowMissionSelect` | all (the last opens nothing: no screen) | `ShowRumbleModeInterface` partial, `ShowOptionMenu` |
| Menu data and text | `CfgLevelName`, `CfgRumbleGame`, `CfgRumbleGang`, `CfgRumbleArena`, `CfgRumbleChar`, `GetRumbleModeData`, `GetRumbleModeGangName`, `GetLanguage`, `ScreenQueueEffect` | all | `CfgHUDColor`, `GetScreenDimensions`, `GetDefaultFont` |
| Pads | `PadSetHandler`, `PadSetHandlerEx`, `HuLockPad` | all three | none |

Six more join the SDK's [`mods: false` list](lua-sdk.md#availability), because they belong to the game's own front
end or its one-slot state: `ShowProfileManager` (it would restart the front end's menus with a project's callbacks),
`CfgHUDMessage` and its four siblings (they overwrite the game's strings by id, which a project has no reason to do and
two projects would fight over: a project's text is its own). The game's flow natives that end a mission or open a
result screen stay allowed: a project's own mission ends the way the game's do.

### Coney's UI natives {#coney-natives}

The game has no native that builds UI ([above](#original-natives)), so Coney adds them, as PascalCase fields of the one
`Coney` table ([Names and namespaces](lua-sdk.md#names)). They build a **retained tree**: a screen holds widgets, each
with an id the project chose; the tree lives in the game mode's state, owned by the project, and is named by ids,
never pointers ([Deterministic calls](lua-sdk.md#determinism)). Every native below wraps a C++ capability; the third
column says whether Coney has it today, and every one waits on the project runtime and the first Coney natives
([Proposed build issues](lua-sdk.md#build-issues) 1 to 4).

| Native | Does | Coney's C++ today |
| --- | --- | --- |
| `Coney.CreateScreen(def)` | A screen: its layer (`menu` takes the pad, `hud` never does), title, whether it pauses play offline; returns a screen id | Ready: the screen flow, the mode stack |
| `Coney.LoadScreen(path)` | A screen built from a [screen file](#screen-files); returns its id | Waits on the screen file reader (build issue 5) |
| `Coney.OpenScreen(screen, slot)`, `Coney.CloseScreen(screen)` | Shows a screen for player *slot*: on the front end's flow, or over play as a game mode | Ready: push and pop on the flow or the mode stack |
| `Coney.AddText(screen, def)` | A text with the game's markup, at a GUI point, size, font (`text` or `big`), colour, alignment, wrap width | Ready: `TextWidget`, `layoutText` |
| `Coney.AddSprite(screen, def)` | A rectangle of a sprite sheet, sized and anchored as the game's | Ready for the disc's sheets (`game:/` sheet names); project images wait on Coney loading PNG for UI ([Custom content](../../roadmap.md#custom-content)) |
| `Coney.AddGrid(screen, def)` | A grid of items in rows, left-packed or centred, with the `" : "` separator: the profile manager's menus | Ready: `OptionGrid` |
| `Coney.AddList(screen, def)` | A scrolling list of a number of visible rows, entries of a title and a description: the Rumble lists | Ready: `ScrollingMenu` |
| `Coney.AddItem(widget, def)` | An item of a grid or list: text, value, enabled | Ready |
| `Coney.AddBar(screen, def)` | A meter filled 0 to 1 | Ready: `Bar` |
| `Coney.AddHeader(screen, def)` | The circled header of the pause and statistics screens | Ready: `CircledTextHeader` |
| `Coney.SetUsage(screen, text)` | The usage line, with the button glyph tags (`<X>`, `<T>`, `<DU>`, ...) | Ready: `UsageInfo` |
| `Coney.SetWidget(widget, props)`, `Coney.GetWidget(widget, key)` | Changes or reads a property: text, position, colour, visible, fill, selected item | Ready |
| `Coney.RemoveWidget(widget)` | Removes a widget | Ready |
| `Coney.Focus(widget)` | Gives a focusable widget the screen's focus | Ready: the grid's and list's focus |
| `Coney.OnWidget(widget, event, fn)` | A handler for `accept`, `back`, `move` (the new item) or `change` (a value) | Ready: the commands reach the widget's owner first, as in the original |
| `Coney.ShowMessage(text, ms, slot)` | A timed message in the message box's place and style | Ready: the message box |
| `Coney.AskChoice(text, choices, default, fn, slot)` | The message box's choice dialog; `fn` gets the chosen index | Ready: the message box |
| `Coney.AskText(prompt, maxLength, fn, slot)` | The profile manager's on-screen keyboard, framed with the project's title | Ready: `NameKeyboard`; text entry is limited ([Safety](#safety)) |
| `Coney.PlayMenuCue(cue)` | One of the front end's cues (moves, accepts, refused, back) | Ready: the front end's audio |
| `Coney.RequestServer(name, ...)` | Sends a request from a client script to the project's server scripts ([Where UI runs](#apiset)) | Waits on the loader's events |
| `Coney.AddMenuEntry(point, def)` | An entry at an [extension point](#points): title, description, action | Ready for the screens that are points; the registry is build issue 5 |
| `Coney.AddOption(def)`, `Coney.GetOption(id)` | An [option row](#options) on the project's MOD OPTIONS page, and its value | Ready: the Options screen's row kinds; the page is build issue 7 |
| `Coney.GetLevels()` | The level table's names, numbers and titles, read only | Ready: the level table |
| `Coney.ModGangId(name)` | A stable gang id from Coney's range for mods | Waits on the [data events](#data-events) |

Every native follows the security design's rules for natives (checked arguments, bounded work, ids;
[#1062](issue:1062)).

### Screens and widgets {#widgets}

- **Coordinates** are the game's GUI points: x right and y down, about [0, 1] inside the overlay camera's 5 % safe
  margin ([Graphics: 2D drawing](../../research/graphics.md#2d-drawing)), sizes as the game's font scale and overlay
  units. A mod's screen lines up with the game's at every video mode, because the same code places both. A screen may
  give a value per video mode (4:3, 16:9 and the flag `0x02` layouts), as the game's layout floats do; one value serves
  all by default.
- **Looks** are the game's: the two fonts the menus use (`part_page0` as `text`, `big_font` as `big`), the colour
  table's named colours (`menuRed`, `menuGrey`, `selectedGrey`, ...) or any colour, the markup tags. Three tags are
  refused in a project's text: `<FREEZE>` (it stops the game timer), `<SOUND>` (a project plays sounds through natives,
  where the sandbox sees them) and `<BGFONT>` (it makes its own batch). A character the font has no rectangle for draws
  nothing, as in Coney's `Font` today ([open question](#open-questions)).
- **Kinds** are the game's classes in the [natives table](#coney-natives). A 3D model on a screen (the gang screen's
  fighters) waits on Coney drawing them on the gang screen itself ([The Choose Gangs
  fighters](../../research/frontend.md#rm-choose-gangs-fighters)).
- **Hosting.** A screen opened on the front end is a screen-flow state on the profile manager's flow; opened in play, it
  is a game mode of Coney's (id from 0x100) pushed over gameplay with its own flow, so a mod screen can lead to the
  project's next screen and back exactly as the game's do.

### Input focus and gamepad navigation {#focus}

- **A `menu` screen takes its player's pad**, by the original's rule: it is on top. Offline with `pause = true` it is a
  mode over gameplay, which is suspended as under the pause menu. Otherwise, and always in a networked session, play
  goes on and the screen takes only its player's pad: gameplay sees that port connected with nothing pressed, as the
  debug menu's input gate does ([Controls](../debug-menu.md#controls)), and the buttons still held when it closes stay
  hidden until let go. A `hud` screen never takes input.
- **Commands are the game's.** A focused grid or list gets `MenuInput_Dispatch`'s six commands with the original's gaps,
  stick rule and auto-repeat, and plays the game's cues; taking focus clears the pad and blocks the d-pad for 20 ms. So
  a mod's menu moves, sounds and feels like the game's, and the keyboard drives it through the same mapping (arrow keys,
  K or Space to accept, L or I to go back, [Controls](../running-input.md#controls)).
- **Between widgets**, focus moves in the order the widgets were added when the focused widget refuses a move at its
  edge (as the profile manager's grids move between rows), or to the neighbour a widget names (`up=`, `down=`, `left=`,
  `right=`), as Roblox's selection neighbours do. Studio draws the order as arrows ([The Screen view](#screen-view)).
- **Back always closes.** On a mod screen with no screen under it in its flow, back closes it: the `back` handler runs,
  but cannot keep the screen open. A project cannot trap the player ([Safety](#no-spoofing)).
- **One screen per player.** Each player slot has at most one mod screen open; player 2's screen reads port 2.
- **Pad and keyboard only, for now** (a maintainer decision): the game has no pointer, and neither do mod screens.
  Mouse support in the menus is planned for later ([Later](#later)).

### HUD elements {#hud}

A screen with `layer = "hud"` is a HUD element: its widgets draw in the HUD's render after the game's own parts, for one
player's view (`slot`), and hide with `HideHud` and the fades as the game's parts do, so a scene or a menu hides a
project's HUD with the game's. Before inventing one, a project uses the game's own HUD natives
([above](#game-natives)): a counter panel, a scripted bar, a scoreboard row and an announcement already cover most
mission HUDs, and they are faithful by construction.

### Where UI runs: client scripts and requests {#apiset}

UI is presentation, so the Coney UI natives are `client` natives and the [`apiset` rule](lua-sdk.md#determinism)
applies: a project builds its screens in its `client` scripts. Offline both kinds of script run on the one machine, and
the rule is checked all the same, so a mod's menu that works offline works on a server. A screen that changes the game
(starts a level, adds a fighter) does it through **`Coney.RequestServer(name, ...)`**: the request is queued and
delivered at the start of the next step to the project's server handler `Coney.On("request:" .. name, fn)`, with the
asking player's slot first. Offline that is the same machine; on a ConeyMP server the network page's events carry it
(P4, [Network architecture](../network-architecture.md#scripting)). The game's HUD natives stay `server` natives, as
every game native is: a server script sets an objective, and the HUD state reaches each client as the network page
plans. The extension natives (`Coney.AddMenuEntry`, `Coney.AddOption`, `Coney.GetOption`, `Coney.ModGangId`) are
`shared`: every machine builds the same menus from the same additions.

### Example: a mission selector {#example}

```lua
-- scripts/client/selector.lua: a list of levels; accept asks the server to start one.
local screen = Coney.CreateScreen{ layer = "menu", title = "Mission Select", pause = true }
Coney.AddText(screen, { id = "title", text = "<BIGFONT>MISSIONS", at = {0.5, 0.08}, size = 2.23, align = "center" })
local list = Coney.AddList(screen, { id = "levels", at = {0.2, 0.25}, rows = 6 })
for _, level in ipairs(Coney.GetLevels()) do
  if level.number >= 2 and level.number <= 99 then
    Coney.AddItem(list, { text = level.title, value = level.name })
  end
end
Coney.SetUsage(screen, "<DU><DD> select   <X> start   <T> back")
Coney.OnWidget(list, "accept", function(item)
  Coney.RequestServer("start", item.value)
  Coney.CloseScreen(screen)
end)
Coney.Focus(list)
```

```lua
-- scripts/server/selector.lua
Coney.On("request:start", function(slot, level) Coney.StartLevel(level, 1) end)
```

With `[[menu.entry]] action = "screen"` naming a screen file instead, the same selector is laid out in Studio and the
client script only fills the list and handles `accept`.

## Studio: screens, the Screen view and the front end {#studio}

### Screens as an asset kind {#screen-files}

Of the two shapes the issue offered, a UI view beside the Level view or screens as their own asset kind, this page
takes **screens as their own asset kind**: `ui/<name>.screen` files in the project, one per screen, in the project
formats' statement format ([#1058](issue:1058)), opened in a Screen view as a level file opens in the Level view. A
screen is not part of a level: the same screen opens in any level, on the front end, or from a menu entry.

```text
format screen 1
screen layer=menu pause=true title="Mission Select"
text id=title at=0.5,0.08 size=2.23 font=big align=center colour=menuGrey text="MISSIONS"
list id=levels at=0.2,0.25 rows=6 down=back
text id=hint at=0.2,0.85 size=1.0 text="Pick a level." at.16x9=0.15,0.86
usage text="<DU><DD> select   <X> start   <T> back"
focus levels
```

The file holds layout and fixed text; scripts fill lists and handle events by the widgets' ids. Studio writes it back
keeping comments and order, so an edit and its undo give the same bytes ([#1058](issue:1058)).

### The Screen view {#screen-view}

A panel of its own, opened by double-clicking a `.screen` file (as a level opens in the Level view), docked by default
as a tab beside the Level view:

- **The picture is Coney's.** The screen is drawn by the same widget code the game runs, at the game's 640 × 448
  logical size with the safe area outlined, over a background the user picks: black, the front end's Wonder Wheel, or a
  frame of a level. A switch shows each video mode's layout.
- **Editing.** Select a widget in the picture or in the Hierarchy, which shows the screen's widget tree while the Screen
  view has focus (as Unity's Hierarchy shows a prefab's contents in prefab mode); move it with the 2D move tool (W) and
  snapping to a GUI grid; its properties in the Inspector; the *Create* menu offers the [widget kinds](#widgets) Coney
  has, and no other.
- **Navigation view.** A toggle draws the focus order and the named neighbours as arrows, as Unity's *Visualize*
  navigation does, so a user sees where each pad direction goes.
- **Text.** Markup is shown as it renders; the refused tags are marked as errors in the Inspector and the Console.

### Opening the front end and its extension points {#studio-frontend}

The maintainer asked that Studio load the main menu. Studio opens **the game's menus** in the Screen view as
`game:/ui/frontend` and `game:/ui/pause`: the profile manager's screens, the Rumble screens and the pause menu's
Options, drawn live by Coney's port with the texts from the user's own disc, one screen at a time with a screen picker.
The game's screens are read-only. Each [extension point](#points) is marked on its screen as a slot after the game's
items, holding the project's additions as players will see them, and those are what the user edits there:

- **Add here** on a slot makes an entry or an option row and opens it in the Inspector (title, description, action,
  the screen it opens); Studio writes it to the manifest's `[[menu.entry]]` or `[[menu.option]]` tables, so the
  addition is data, plain to review, and needs no script.
- **Additions made by scripts** (`Coney.AddMenuEntry`, `Coney.AddOption`, the Rumble data events) show in the slot after
  the next Preview or Play, read from the session, marked as the scripts', and open their script line on a double-click.
- **The other enabled mods'** additions can be shown beside the project's, in load order, to see the point as players
  who combine them will.

**Play from the main menu.** Beside Play (which starts the project's level, [Play, step by step](lua-sdk.md#play)), a
second target starts the play session at the front end, with the project enabled as a mod, so the user walks the real
main menu, MODS, the Rumble lists and MOD OPTIONS with the project's additions in place, by pad or keyboard in the
Game view.

### Previewing and testing in Studio {#preview}

- **Preview** in the Screen view runs the screen alone in a play session: the screen opened for player 1, the project's
  client scripts loaded (their handlers fire; requests go to the server scripts), input from the Game view's pad and
  keyboard, and the handlers' `print` lines in the Console.
- **Live edits** of a screen during Preview or Play rebuild it at the next step boundary, as the technology design's
  edit commands do ([Live edits](technology.md#live-edits)).
- **A screen test** is an input script beside the screen (`ui/<name>.test.txt`) that Studio runs in Preview and
  `coney --project` runs headless: the same scripts the game's own tests use ([Tests](#tests)).

## Safety {#safety}

The sandbox and the permissions are the security design's ([#1062](issue:1062)); this section adds what UI needs.

### What stays out

- **No web views, no HTML, no browser.** The lesson of FiveM's NUI and Garry's Mod's DHTML: a browser brings a network
  stack, its own scripting and a page that can look like anything.
- **No network, no files, no clipboard, no system dialogs** from any UI native: the sandbox's rules for natives
  ([#1062](issue:1062)).
- **No free text field.** The one way to ask for text is `Coney.AskText`, the game's pad keyboard, framed with the
  project's title, at most 32 characters; nothing typed reaches anything but the project's own handler, and Coney asks
  for no password anywhere, so a "log in" box from a mod cannot be mistaken for Coney's.
- **No mouse capture and no window control**: no native moves, resizes or focuses the window, or hides the system
  cursor.
- **No drawing outside screens**: there is no per-frame draw call; everything a project shows is a widget in a screen,
  counted and named.

### A mod's UI can never pass for Coney's own {#no-spoofing}

- **The trusted layer.** Coney's own screens (the Mods screen, a mod's details, permission approvals, the profile
  creation and deletion screens, the card and save messages, the pause menu's Quit and the debug tools) draw on a layer
  above every mod widget. While a trusted screen is open no project receives that player's pad, no mod screen opens, and
  no native can open, close, read or press anything on it.
- **The frame label.** Every mod screen and dialog shows the project's title, small, in a fixed place (top right of the
  safe area), drawn by Coney in the trusted layer. A project cannot hide or move it and cannot draw over it, so a screen
  that imitates the Mods screen or a Coney message still says whose it is.
- **Reserved looks.** A project cannot set the frame label's text, and its menu entries are drawn by Coney under the
  project's title on the Mods screen, never mixed into Coney's own items.
- **Back always closes**, so no mod screen can hold the player ([Focus](#focus)).
- **Additions only, and disclosed.** A project can add at [extension points](#points) and nowhere else, never hiding or
  changing a game item; its details on the Mods screen list every addition and where it is, and MODS is always there
  to switch it off ([Order and clashes](#order)).

### Limits

Per project, enforced by the natives with an error the script can test: at most 16 screens and 512 widgets alive, 4 of
them HUD screens shown at once, 2,048 characters in one text, 100 items in one list. The numbers are first guesses, to
be measured on real projects with the instruction and memory budgets ([Open questions](lua-sdk.md#open-questions)). A
handler that errors stops its call, as every handler does ([The sandbox](lua-sdk.md#sandbox)); a screen whose handlers
error three times in a row is closed and the Console says why.

## Tests {#tests}

Nobody plays a mod's menu to check it ([Testing](../testing.md)). The tests, written first in each build issue:

- **The faithful menus are unchanged.** With no mod installed, the front-end run reaches PM_Mode with the original's
  three items and layout and the Rumble lists with the disc's records and defaults; the existing disc checks run
  unchanged ([Front end: Coney's implementation](../../research/frontend.md#coneys-implementation)).
- **A fixture project** (synthetic, in `tests/fixtures/`, no game data) with a menu entry, a screen file and a client
  script. An input script drives it from boot through the player's path ([Test through the player's
  path](../testing.md#test-through-the-players-path)): START on PM_Greet, right and down to MODS, the project's entry,
  the screen's list, accept; the test checks the handler's request reached the server script and the level started.
- **By pad and by keyboard.** The same walk twice: once with pad buttons and the stick at a partial push, once with the
  keyboard's keys through the keyboard's mapping. Input scripts gain a `key NAME` action that goes through
  `SdlInput`'s key table (build issue 1), so the keyboard test runs headless like the pad's.
- **Navigation.** Every focusable widget is reached from the first by the four directions, the refused moves play
  `0xe`, back closes the screen, the 110 ms gap and the auto-repeat hold, with the step counts the original's timings
  give ([Front end: input](../../research/frontend.md#input)).
- **Focus.** With a screen open over play, gameplay sees its player's port empty; port 2 still plays; buttons held at
  the close stay hidden until released.
- **Safety.** A project cannot open a screen while the Mods screen is open, cannot draw over the frame label, cannot
  keep a screen open past back, and cannot pass a refused tag or more widgets than its limit.
- **Extension points.** Two fixture projects add at the same point: their additions follow the game's items, in load
  order and then in each project's order; swapping the load order swaps them; the game's items keep their places,
  texts and default selection; an id added twice by one project
  replaces its own; no call can name a game item or another project's addition; switching a mod off removes its
  additions.
- **Data events.** A fixture gang added in `rumbleGangs` appears after the disc's, keeps the defaults, and plays a 1 ON
  1 with its roster's types.
- **Option rows.** A fixture's rows appear under MOD OPTIONS on its page, change by pad and keyboard with the game's
  cues, keep their values in the project's store across a restart, leave the game's options untouched, and run the
  `option:` handler at the next step.
- **Pictures.** `--screenshot` of the fixture's screen at each video mode, compared with the Screen view's capture of
  the same file (the same code draws both), for the pull request's Screenshots.
- **Studio** ([Testing Studio](technology.md#testing)): the Test Engine opens a screen file, drags a widget, edits a
  property, checks the file's statements, runs Preview with an input script, and opens the front end, adds an entry
  at the Mods screen's slot and checks the manifest's `[[menu.entry]]`.

## What changes on other pages {#other-pages}

When this design is accepted, these lines change on the pages that own them:

- [Lua SDK and natives](lua-sdk.md): the events `rumbleModes`, `rumbleGangs`, `rumbleArenas` and `request:<name>` join
  the [entry points](lua-sdk.md#events), with `option:<id>`; `Coney.RequestServer` joins the SDK functions;
  `[[menu.entry]]` and `[[menu.option]]` join the [manifest](lua-sdk.md#manifest); `ShowProfileManager`,
  `CfgHUDMessage`, `CfgTutorialMessage`, `CfgCrimeMessage`, `CfgAnnounceMessage` and `CfgWarriorCommand` join the
  [`mods: false` list](lua-sdk.md#availability).
- The project formats ([#1058](issue:1058)): `ui/<name>.screen` joins the project folder and the statement format's
  file kinds; the asset override packs stay the way to change how the game's own screens look.
- The editor experience ([#1055](issue:1055)): the Screen view joins the panels, and *Play from the main menu* joins
  Play's targets.
- The security design ([#1062](issue:1062)): the trusted layer and the frame label are the UI half of its rule that no
  text field pretends to be Coney's; the Mods screen is where its details and approvals are shown, and its details
  gain a mod's additions and option rows; a project's option values live in the project's store.

## Maintainer decisions {#decisions}

1. **Mods add, never rewrite**: they add at extension points and never rewrite, hide or reorder the game's screens; the
   game's own look is the asset override packs' ([#1058](issue:1058), [Extending the menus](#extending)).
2. **MODS on the main menu, only when a mod is installed** ([The MODS entry](#mods-screen)): with any mod installed the
   main menu shows MODS after QUICK RUMBLE; with none it is the original's three items.
3. **MOD OPTIONS is the one new place in the game's screens**: besides MODS, Coney adds only a MOD OPTIONS title to the
   pause menu's Options ([Option rows](#options)); the other points are lists the game already has. EXTRAS takes no mod
   items.
4. **Pad and keyboard only, for now** ([Focus](#focus)): mod screens and the menus are driven by the pad and the
   keyboard, as the original's are. Mouse support in the menus is planned for later ([Later](#later)).

## Open questions {#open-questions}

- **Which characters the fonts hold**: `part_page0` and `big_font` are byte-indexed sheets; which bytes beyond ASCII
  have rectangles, and so which accented letters a project's text can use, is not listed. A disc check can count them.
- **Mod gangs' packs**: a gang's id names a character pack that the arena script precaches; for an id no disc pack has,
  loading the roster's types one by one is the stand-in. Which file a pack id names, and whether the arena's precache
  needs the pack, are to be traced from `ConfigRumble`'s calls.
- **Pausing in co-op offline**: two players on one machine with one opening a screen; does `pause = true` pause both, as
  the game's pause menu does, or does it fall back to taking one pad?
- **More points**: whether the pause grid (a project's own pause page) or the statistics screen should take additions;
  this page leaves them out because their layouts are fixed and full, and adds a point only when a project needs one.
- **The limits' sizes** ([Limits](#limits)): measured on real projects before the first release.

## Proposed build issues {#build-issues}

In the order they should come; issues 4 to 7 build on the Lua SDK's issues 1 to 4 (the native list, the Lua 5.4
runtime, the loader and the first Coney natives, [Proposed build issues](lua-sdk.md#build-issues)).

1. **tools: Add keyboard keys to input scripts.** A `key NAME` action (`press`, `release` and `tap` forms) whose keys go
   through the same table `SdlInput` maps the keyboard with, factored out so both use it; the controls table and the
   input scripts section of [Controls and input scripts](../running-input.md) updated. Tests: a script's keys give the
   same pad samples as the matching buttons; the front-end walk to PM_Mode passes by keys alone.
2. **core: Add the UI widget model for mods.** The retained tree in the game mode's state: screens and widgets by id
   over the existing classes (text, sprite, grid, list, item, bar, header, usage line, message box, keyboard); screens
   hosted on the profile manager's flow or as a Coney game mode over play; focus, the game's commands and cues,
   neighbours and order; the input mask for a screen over play; the trusted layer and the frame label; back always
   closes; the limits. C++ only, no Lua. Tests: navigation reaches every focusable widget with the original's timings;
   the input mask; a model screen cannot open over a trusted one; the frame label is drawn above every widget.
3. **core: Add the MODS entry and the Mods screen.** MODS on PM_Mode when the mods folder holds a mod (rows {2, 2}, code
   9, the separator), the trusted Mods screen listing installed mods with their details and their entries, switching
   on and off through the loader's state. Tests: with no mod the main menu is the original's (items, layout, the disc
   check unchanged); with a fixture mod MODS appears and leads to the screen by pad and keyboard; the screen cannot be
   covered by a project.
4. **core: Add the Coney UI natives.** The natives of [Coney's UI natives](#coney-natives) in
   `research/bindings/coney.yaml` (`origin: coney`, `apiset: client`) over the model; `Coney.OnWidget` handlers as
   callback ids; `Coney.RequestServer` with the `request:<name>` event; `Coney.GetLevels`; the refused markup tags; the
   `mods: false` additions. Tests: a fixture client script builds the selector, the walk by pad and keyboard starts the
   level through a request; a server script calling a UI native is refused; a refused tag is an error.
5. **core: Add screen files, menu entries and the extension points.** The `ui/<name>.screen` reader in the statement
   format and `Coney.LoadScreen`; the extension point registry with the order and clash rules (game items first, load
   order then registration order, `<project>/<id>` names, nothing removable); `Coney.AddMenuEntry` and
   `[[menu.entry]]` with the three actions at the `mods` and `rumble.modes` points;
   the entries' languages; the additions in each mod's details. The `missions` point follows once the mission select
   is ported. Tests: a screen file round-trips; the extension-point tests of [Tests](#tests); a `play` entry starts the
   project's level.
6. **core: Add the menu data events for the Rumble lists.** `rumbleModes`, `rumbleGangs` and `rumbleArenas` fired after
   the disc's chunk, records appended in load order, `Coney.ModGangId` and its range, mod gangs always unlocked, the
   fighters loaded by type when no pack has the id. Tests: a fixture gang appears after the disc's with the defaults
   unchanged and plays a 1 ON 1; two projects' gangs follow the load order; a mode or arena record without its rules or
   level is refused with a clear error.
7. **core: Add mod option rows under MOD OPTIONS.** `Coney.AddOption`, `Coney.GetOption`, `[[menu.option]]` and the
   `option:<id>` event; the MOD OPTIONS title after the seven Options pages when a mod adds rows, its list of projects
   and each project's page with the game's row kinds (toggle, choice, bar, screen) and its own Restore Default; values
   in the project's store; the same page from the Mods screen. Tests: the option-row tests of [Tests](#tests); with no
   mod the Options list is the original's seven.
8. **tools: Add the Screen view to Coney Studio.** The panel over the widget code, the video-mode switch, backgrounds,
   selection, the 2D move tool with snapping, the Inspector's properties, the Create menu of the widget kinds Coney has,
   the navigation view, writing the screen file back, Preview with the project's client scripts. Tests (Test Engine):
   open, drag, edit, save and diff a fixture screen; Preview walks it with an input script.
9. **tools: Add the game's menus and their extension points to Coney Studio.** `game:/ui/frontend` and `game:/ui/pause`
   in the Screen view with the screen picker, the game's screens read-only, each extension point's slot with the
   project's additions (and optionally the other enabled mods'), *Add here* writing `[[menu.entry]]` and
   `[[menu.option]]`, script additions shown after a Preview, and the Play target that starts at the front end with the
   project enabled. Tests (Test Engine): an entry added at the Mods screen's slot appears in the manifest and, after
   Play from the main menu, on the Mods screen in the Game view.

### Later {#later}

Mouse support in the menus, planned by a maintainer decision: pointing and clicking in the game's menus and mod
screens beside the pad and the keyboard, filed as a build issue when it is taken up.

## Sources {#sources}

Ideas only; no code is taken from any of these.

| Idea | Source |
| --- | --- |
| NUI: web pages in Chromium (CEF) over the game | FiveM, [NUI development](https://docs.fivem.net/docs/scripting-manual/nui-development/) |
| `SetNuiFocus`; menu libraries drawing with draw natives; Scaleform movies | FiveM *(general knowledge, not re-checked)* |
| `GuiService.SelectedObject`, the gamepad navigator, `GuiNavigationEnabled` | Roblox, [GuiService](https://create.roblox.com/docs/reference/engine/classes/GuiService) |
| CoreGui holds Roblox's own menu, player list and prompts | Roblox, [CoreGui](https://create.roblox.com/docs/reference/engine/classes/CoreGui); that game scripts cannot change the prompts is community-sourced ([DevForum](https://devforum.roblox.com/t/what-is-coregui/1196413)) |
| `ScreenGui`, `Selectable` and selection neighbours, Studio's UI editor | Roblox *(general knowledge, not re-checked)* |
| Derma over VGUI; `DHTML:SetAllowLua` off by default | Garry's Mod wiki, [Introduction to using Derma](https://wiki.facepunch.com/gmod/Introduction_To_Using_Derma), [DHTML:SetAllowLua](https://wiki.facepunch.com/gmod/DHTML:SetAllowLua) |
| The menu state closed to addons for want of a client permission system | Garry's Mod wiki, [States](https://wiki.facepunch.com/gmod/States) |
| Secure execution, taint, protected functions, `hooksecurefunc` | Warcraft Wiki, [Secure Execution and Tainting](https://warcraft.wiki.gg/wiki/Secure_Execution_and_Tainting) |
| Frames in XML and Lua; the login screens closed to addons | World of Warcraft *(general knowledge, not re-checked)* |
