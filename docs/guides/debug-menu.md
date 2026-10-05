# The debug menus

Coney has a debug menu of its own, in the spirit of the trainer menus players know from the GTA games: one place to
pause and step the game, change tuned values while it runs, call any script binding with arguments you pick, run Lua in
the game's script state and watch the pad. It is for testing and for messing around, from a gamepad in hand.

The original has no debug menu, no free camera and no debug pad handler
([Debug features](../research/debug.md#not-present)), so nothing here reimplements the original: none of this code
carries an `@orig` tag, and nothing in it changes how the game plays while the menu is closed.

## One model, rendered by front ends {#one-model}

Every feature of the menus is defined once, in `src/debug/`, as data: the menu tree (pages of items), the tunables
registry, the native signature table and the actions with their callbacks. A front end is a generic renderer of that
model: it has one way to draw each kind of item and no feature logic of its own, so a feature added to the model
appears in every front end with no front-end code.

| Part | Where | What it is |
| --- | --- | --- |
| The model | `src/debug/menu_model.h` | pages of items of eight kinds, pins, plotted channels |
| The session | `src/debug/debug_session.h` | the pages, the state they share, the input gate |
| The pad menu's state | `src/debug/menu_navigator.h`, `pad_menu_input.h` | breadcrumb, cursor, typing, hold-to-repeat |
| The pad menu's renderer | `src/gui/debug_menu_view.h`, `debug_text_painter.h` | the in-game list over the game's screen |
| The platform glue | `src/platform/debug_menus.h` | draws the pad menu at the end of every frame |
| The developer overlay | `src/platform/imgui_model_view.h`, `imgui_overlay.h` | the same model in Dear ImGui windows |

The item kinds are: **action** (run a callback), **toggle**, **number** (a slider with a range and a step, whole or
real), **choice** (one of a list), **text** (typed text or a number, with an optional history), **submenu**,
**watch** (a live read-only value, optionally with a plotted channel) and **log** (read-only lines).

There are two front ends. The **pad menu** is a trainer-style list drawn in the game's own font, driven by the pad
alone, in every run including headless ones; it is the one to use with a gamepad in hand, on a television, and in
scripted tests. The **developer overlay** shows the same pages as Dear ImGui windows for the mouse and keyboard, in a
windowed run only; it is the one to use at a desk, for many values at once, searching the bindings, plots and typing
Lua. Both act on the same session at the same time: a value changed in one shows at once in the other, and a pin made in
one is in the other's Favourites.

## Opening it and the controls {#controls}

Press **L3 and R3 together** (both sticks in) to open and close the menu; on a keyboard, **F4** (or F and H together).
No control of the retail game uses that chord, and the cheat codes are single buttons, so it never collides with play.
The game never sees the chord, and while the menu is open the game sees port 1 with nothing pressed: menu input never
reaches gameplay. When the menu closes, the buttons still held (the circle that closed it) stay hidden from the game
until you let go of them. Port 2 always reaches the game.

The menu is driven entirely by the pad:

| Pad | Does |
| --- | --- |
| d-pad or left stick up / down | the previous / next item; held, it repeats and speeds up |
| d-pad or left stick left / right | lower / raise the value; held, the change grows (1, 2, 5, then 10 steps) |
| L2 held / R2 held | fine steps (a tenth) / coarse steps (ten) while changing a value |
| cross | choose: run an action, flip a toggle, open a submenu, start typing a text or a number |
| circle | back a page; on the first page, close the menu |
| square | pin the item to Favourites (or unpin it) |
| triangle | reset the value to its default |
| L1 / R1 | a page of items up / down |

The left stick counts as a direction past half its travel, after the pad's own dead zone, so a firm push of about three
quarters moves the cursor. Hold-to-repeat counts simulation steps, not real time: a direction fires at once, again after
12 steps (0.4 s), then every 4 steps, every 2 after 1.5 s and every step after 3 s.

**Typing with the pad.** Cross on a text item (or on a number, to type it exactly) starts typing. Up and down turn the
character under the cursor through the letters, digits and punctuation; left and right move the cursor, and right past
the end adds a character (a copy of the last, so runs of digits are quick); square deletes; L1 and R1 recall older and
newer lines of the item's history (the Lua console keeps one); cross enters the text and circle cancels.

The breadcrumb on the title bar shows where you are, the counter how far down the page; every page remembers its
cursor for the next visit. The footer shows the item's help, the last message, a plot of a watched channel and the
page's log lines.

## The developer overlay {#the-developer-overlay}

**F1** shows and hides the overlay (a windowed run only; it starts hidden). A menu bar lists the pages; each opens as a
window, and the windows that were open come back in the next run, with their places, from `coney-imgui.ini` in the
config folder next to the tunables file. Every window has a filter box over its items: a submenu whose name matches
shows everything under it, and one whose items match opens to show them, so typing `Ped` in the Natives window shows
the matching bindings in every category. *Refresh* makes the page again for lists that changed.

| Item | Widget |
| --- | --- |
| action | a button |
| toggle | a checkbox |
| number | a drag box over its range, with its units; Ctrl+click types a value |
| choice | a drop-down list |
| text | a text box: Enter commits it; Up and Down walk its history (the Lua console's box empties after each line) |
| submenu | a tree node, labelled with its tag (a category's count, a binding's status) |
| watch | the live value; *Plot* draws its channel's last 300 steps |
| log | a scrolling box that follows new lines |

Hovering an item shows its help; right-clicking it pins it to Favourites (or unpins it) and resets a number to its
default. While a text box has the keyboard, or the mouse is over a window, those events stay with the overlay: the
keyboard does not play on port 1 and Escape does not quit. The gamepads always go to the game and the pad menu.

Hidden, the overlay draws nothing and takes no input. Shown, it draws last in the frame, through Dear ImGui's OpenGL 3
backend, which saves the OpenGL state it changes and restores it afterwards, so librw's own idea of the state stays
right. `--dev-overlay N` shows it for the first N frames and then hides it, to check that: the last frame of
`--frames 150 --dev-overlay 100 --screenshot a.png` is byte-identical to the same run without `--dev-overlay`.

Dear ImGui (MIT licence) is pinned in `cmake/deps.cmake` and used only in `src/platform/`
([LEGAL.md](repo:LEGAL.md#licences)).

## The pages {#pages}

| Page | What it has |
| --- | --- |
| Favourites | every pinned item, working as itself |
| Time | pause, step one fixed step, slow motion, the frame and step counts with a plot, the real frame time |
| Tunables | one page per category of the [tunables](#tunables), reset all, save and load the overrides file |
| Natives | the [script bindings](#natives) by category with their Coney status, an argument editor and a call |
| Lua console | a Lua line to run in the script state, a file to run, and the output |
| Cheats | the 27 retail cheat codes, each sent to the script's cheat callback |
| Levels | the level table's levels, loaded by name, and a name to type |
| Display | the frame-stats line, the GUI safe area and the logical screen's edges, drawn over the game |
| Input | port 1 live: buttons held, both sticks (plotted), the raw stick bytes, the triggers' pressure |

**Time.** The game always advances by whole fixed 1/30 s steps. Paused, no step runs; *Step one* runs exactly one.
Slow motion runs one step out of every N that real time calls for, so the game runs at 1/N speed and every step is still
1/30 s: never a variable step. A held step still reads the pad, so the menu keeps working, and the game's last step
stays on screen, drawn as it is (not blended towards a step that has not run). In a window
the page also shows the real time each frame took, as the frame pacer measures it (a headless run has no real clock
and leaves it out); it is only shown and plotted, never used by a step.

**Cheats.** The retail cheat checker (six single buttons matched against a table, [Debug features](../research/debug.md#cheats))
is not in Coney yet. The page does what the checker does on a match: it calls the script's cheat callback,
`DbgEnterCheat`, with the code's index. `global.lua` defines that callback when a level loads; at the front end or in
the sandbox the page reports that it is not set.

**Levels.** With a disc the level table holds the game's levels (filled by `CfgLevelName`); choosing one asks the level
flow to start it next (`MenuLoadLevel`). Loading straight into a level in play waits for the sandbox and level-play
work.

## Tunables {#tunables}

A tunable is a named, typed value a subsystem lets the menus edit while the game runs. The subsystem keeps the variable;
the registry (`src/debug/tunables.h`) keeps a pointer to it with its range, step, units, default (the variable's value
when registered) and a description. The Tunables page builds one page per category from the registry by itself.

The first tunables are the player's and the camera's researched values (`src/debug/game_tunables.cpp`):

| Category | Tunables (default) |
| --- | --- |
| Movement | stick dead zone (0.12), run threshold (0.95), acceleration (24 m/s², 0.8 m/s an update), turn limit walking, jogging, running and sprinting (12°, 6°, 4°, 2.5° an update) |
| Follow camera | position lag (0.22), collision margin (0.2 m), closest after collision (0.5 m), leash near and far (3.0 m, 3.5 m), pitch (13°), look-at height (1.4 m) |

The leash, pitch and look-at height apply when the camera is next placed (a level start); the others at the next step.

**Determinism.** A change never lands in the middle of a step: the registry queues it, and the input gate applies the
queue between two steps (`TunableRegistry::applyPending()`). A run with the same overrides file and the same input is
the same run.

**The overrides file.** *Save overrides* writes the values that differ from their defaults, one `category/name = value`
line each (`#` starts a comment; a bool is `on` or `off`):

```text
# Coney tunable overrides (docs/guides/debug-menu.md#tunables): category/name = value
Movement/Run threshold = 0.9
Follow camera/Leash far = 4
```

Coney loads the file at start-up: the one `--tunables FILE` names, or `coney-tunables.ini` in your config folder in a
windowed run (`%APPDATA%\Coney\Coney\` on Windows, `~/.local/share/Coney/Coney/` on Linux,
`~/Library/Application Support/Coney/Coney/` on macOS). A headless run without `--tunables` reads and writes no file.
A file with a bad line stops Coney at start-up with the line's number. An override for a tunable that has not
registered yet waits for it. The file is yours: it never goes in the repository.

## Natives {#natives}

The Natives page lists every one of the game's 956 script bindings ([Script bindings](../references/bindings/index.md))
under its category, each with its Coney status:

| Status | Meaning |
| --- | --- |
| implemented | real in Coney's binding table: it does its job |
| partial | routed to a stand-in (music, movies) that logs it |
| stub | registered; returns its documented default and does nothing else |
| missing | not registered: a script's call of it is skipped |

A binding's page shows its status and the call as a script would write it, then one editor per argument, built from its
signature: a number, a whole number, a handle (a number naming a human, gang or camera), a toggle for a boolean, text
for a string, numbers separated by commas for a table, nothing for userdata. Each starts at its default. *Arguments
passed* leaves the last ones off, so their defaults apply. *Call it* calls the binding through the script VM exactly as
a script's call does (the global's function, through `LuaVm::call`), then logs the call, its results and what it wrote
into table arguments; *Recorded calls* counts the calls a recording stub kept.

Calls go to the game's script state when a game runs (with `--disc`, the front end's), otherwise to the menus' own
**sandbox state**: Coney's bindings over a fresh game state, whose menus, music and movies only log. A call there acts
on the sandbox, never on a game.

The signatures come from the masterlist: `coney-tools natives cpp` generates `src/debug/native_signatures.cpp` from
`research/bindings/*.yaml` ([coney-tools](coney-tools.md#natives)), and CI checks it is current.

## The Lua console {#lua-console}

*Run line* runs a line of Lua in the same script state the Natives page calls into, as the original's dormant scene-test
hook would have run `SCENETEST=1` ([Debug features](../research/debug.md#debug-only-script-paths)). Coney's VM runs the
game's compiled bytecode and has no Lua compiler, so the console understands the part of Lua a console needs, evaluated
directly on the VM:

- statements separated by `;`: an assignment (`SCENETEST = 1`, `t.x = 2`, `t[1] = 3`) or an expression list, whose
  values are printed; `=expr` prints, as Lua's own console does;
- numbers, strings, `nil`, table constructors, globals, fields, calls `f(...)`, method calls `obj:m(...)`, unary minus
  and `not`, `+ - * / ..`, comparisons, `and`, `or` and parentheses.

Not: `if`, loops, `local` or `function` definitions. *Run file* runs a text file of such lines, or a compiled Lua 4.0
chunk, which can hold anything. The lines run are kept in a history (L1 and R1 recall them while typing).

## Fonts {#fonts}

With a disc, the pad menu draws its text in the game's own text font (`part_page0`) through a sprite batch and the 2D
pass, as the game draws its own text, so it looks at home over the game. Without a disc (the disc-free idle screen, the
sandbox) it draws in Coney's built-in 5 × 7 bitmap font (`src/graphics/bitmap_font.cpp`), whose glyphs were drawn for
Coney: no game data and no third-party font. The panels are flat translucent quads in both cases.

## Adding a feature {#adding-a-feature}

**The rule:** every debug feature (a page, an action, a tunable, a native call, a time control, a level load, and later
teleport and spawn) is defined once, in `src/debug/`, as data: pages of items, tunables, signatures, actions with
callbacks. The front ends only render that model and hold no feature logic. A feature added there appears in every
front end with no front-end code. If a feature seems to need its own front-end code, add the missing item kind or a
generic capability to the model instead, so every front end gets it. Tests check that every item kind has a renderer
in each front end and that the overlay draws every item of the session's pages (`tests/gui/debug_menu_view_test.cpp`,
`tests/platform/imgui_model_view_test.cpp`); the overlay's switch over the kinds has no default case, so a new kind
without a widget does not compile. The overlay's only extras are generic ones: the filter box, plotting any watch with
a channel, and a text box for any text item.

A tunable is one line where the subsystem has its variable:

```cpp
registry.add("Movement", "Run speed", &m_runSpeed).range(0, 20, 0.1).units("m/s").describe("Top speed on foot");
```

A page is one call with a function that fills it from the items' factories (`src/debug/menu_model.h`):

```cpp
session.model().addPage("Player", [&player](debug::MenuPage& page) {
    page.add(debug::toggleItem("God mode", [&] { return player.god(); }, [&](bool on) { player.setGod(on); }));
    page.add(debug::actionItem("Teleport to start", [&] { player.teleport(start); }));
}, "The player: position, health, model.");
```

The standard pages are added in `DebugSession`'s constructor (`src/debug/debug_session.cpp`); give a new one an
`add...Page()` function in `debug_pages.h`. A value worth plotting gets a channel
(`model.addChannel("Player/Speed", [&] { return player.speed(); })`) and a watch item naming it. Callbacks run between
simulation steps (the pad menu acts in the input gate, before the step), so they may change game state freely. Add the
page to the table above and a test that drives it through the model (`tests/debug/debug_session_test.cpp`).

## Testing it without a person {#testing}

The menu counts steps, never time, so a scripted pad drives it exactly like a player. `tests/support/debug_menu.txt`
opens it with the chord, walks to Cheats and sends a code; ctest runs it headless (`coney.debug_menu_by_pad`). For a
picture, run the same script in a window with `--screenshot` (keep screenshots outside the repository):

```sh
build/dev/src/platform/coney --frames 30 --input-script tests/support/debug_menu.txt --screenshot ../../scratch/menu.png
```

The overlay is checked the same way: its renderer runs over a bare Dear ImGui context in the unit tests, and
`--dev-overlay` with `--screenshot` checks that it leaves the frame as it found it ([above](#the-developer-overlay)).

## Still to come {#still-to-come}

- The player, camera, spawner and debug-draw pages, once level play and the sandbox are in: teleport, god mode, model
  swap, free camera, spawning characters and objects, collision and path overlays.
- The retail cheat checker itself, after which the Cheats page gains on/off states.
