# The Coney console

The Coney console is the terminal F8 opens in the game: a line to type a command into, the output above it, and the
commands every build offers. This page is what is built of it; the whole design, with the panels, the commands'
handlers and the mods' commands still to come, is [Coney Studio: the Coney console](studio/console.md).
Every command is listed in the generated [console commands reference](console-commands.md).

## What is built {#built}

The console's core, build issue 1 of [the design](studio/console.md#build-issues): the model in `src/console/` with no
user interface, its catalog and generators, the input script's `console` action and test mode's rules; the player
commands, build issue 2: the handlers of the release set, over the game session's and network play's hosts
([Player commands](#player-commands)); and the F8 bar, issue 3 ([The F8 bar](#front-end)), in the release build and the
debug tools' builds alike. Not built yet: the panels and the commands that open them (`log`, `resmon`, `perf`, `net` and
`profiler`, issues 4 and 5: they wait in the catalog for their feature and are unknown when typed), the developer set
(issue 6) and mod commands through the SDK (issue 7).

| Part | Where | What it does |
| --- | --- | --- |
| Command specifications | `src/console/command_spec.h` | A command's name, group, access level, scope, summary, needs, the feature it waits for and its arguments |
| The tokenizer | `src/console/tokenizer.h` | Splits a line into words ([Lines](#lines)) |
| Argument types | `src/console/arg_types.h` | Each type's check and completion ([Argument types](#argument-types)) |
| The registry | `src/console/registry.h` | The built-ins and the mods' commands, by id ([The registry](#registry)) |
| The catalog | `src/console/commands.toml`, `catalog.h` | The player set as data, and its generated table ([The command catalog](#catalog)) |
| The player commands | `src/console/player_commands.h` | The release set's handlers, bound to the catalog by name ([Player commands](#player-commands)) |
| The terminal | `src/console/terminal.h` | The output (2,000 lines) and the history (200 lines) ([The terminal](#terminal)) |
| The queue | `src/console/command_queue.h` | Commands waiting for the step boundary ([When a command runs](#queue)) |
| The hosts | `src/console/hosts.h` | The interfaces the game implements ([Hosts](#hosts)) |
| The console | `src/console/console.h` | A line in: checked, then run now or queued; its output in the terminal and the run log |
| The game's console | `src/platform/game_console.h`, `game_console_links.h`, `game_settings.h` | Both entry points' set-up: the session host, network play's host and the settings ([In the game](#in-the-game)) |
| The line, the picker, the pad gate | `src/console/line_editor.h`, `command_picker.h`, `console_pad.h` | The F8 bar's model: typing, the gamepad's picker, SELECT and R3 ([The F8 bar](#front-end)) |
| The bar | `src/platform/console_view.h`, `console_ui.h` | The bar's widgets and its window, in `coney_console_ui` ([The F8 bar](#front-end)) |

The core is in `coney_core`, so the release build has it. Its files include only `console/`, `core/` and the standard
library, which the split test checks (`python/tests/test_debug_tools_split.py`,
[The debug tools and the game](building.md#debug-tools)): the console names nothing of the debug tools, the scripting
runtime, the entities or the game modes, and reaches the game only through its hosts.

## Lines {#lines}

A line is a command name and its arguments, separated by spaces or tabs: `mission 3 2`, `set fps_cap 60`. An argument
with spaces is written in double quotes (`log "level load"`), and `\"` is a quote inside one; quotes join with the text
beside them, as a shell's do. One command per line and nothing is evaluated: `;` is text like any other. A line is at
most 512 bytes; a longer one is refused, never cut, and so is a quote left open. A blank line does nothing.

## Argument types {#argument-types}

Each argument has a type, which brings its check and its completion:

| Type | Takes | Completes from |
| --- | --- | --- |
| `int` | A whole number | nothing |
| `number` | A decimal number | nothing |
| `bool` | `on` or `off` | `on`, `off` |
| `text` | Anything | nothing |
| `choice` | One of the argument's `choices` | its choices |
| `mission`, `checkpoint` | A number from 1, in the session host's list when it gives one (a checkpoint's list is the mission's before it) | the session host's list |
| `level`, `setting` | A name, in the session host's list when it gives one | the session host's list |
| `mod` | A name, in the mods host's list when it gives one | the installed mods |
| `command` | A name of a present command | the registry's names |

A refusal names the argument and what it takes, then the usage line and the argument's help:
`"count": expected a whole number, got "two"; usage: wave [count] (How many)`. Numbers complete in their order, names
alphabetically.

## The registry {#registry}

The registry holds every command a line can name, each by a `CommandId`, never a pointer: a queued line keeps its
command's id, and a command removed before the queue runs is found missing. There is one registration call for each
kind, and the console itself names no command:

- **Built-ins** come through `addBuiltin`, the player set at start-up by `registerBuiltins` from the catalog, each
  bound to its handler by name. A second registration of a name is refused, and so is a developer command in a registry
  made for the player level (the release build's). A default that its argument would not take (`many` for an int, a
  value outside a choice's list) is refused too, here and by the generator.
- **Mod commands** come through `addModCommand`, always at the player level: a name of at most 32 characters, never a
  built-in's, at most 64 a mod, whose own name is spelled as a command name is (at most 64 characters), since
  `<mod>:<name>` has to reach it. The same mod registering its name again replaces the command and keeps its id;
  `removeMod` takes all of a mod's commands away. Two mods' commands of one name are reached as `<mod>:<name>`
  (`yard-brawl:wave`), and the bare name is refused as ambiguous with both listed. Nothing calls it yet: the SDK
  function `Coney.RegisterCommand` is build issue 7.
- **`available`.** A command whose `available` names a Coney feature that is not built is absent: not listed, not
  completed, unknown when typed. No feature is marked built yet, so `missions` and `mission` (waiting for
  `mission-select`) stay absent, and so do the panel commands (`log-panel`, `resources-panel`, `performance-panel`,
  `network-panel` and `profiler`), whose handlers come with their build issues. A test holds that every entry that is
  present has its handler.

An unknown name is answered with the closest present one, within two edits:
`unknown command "noclp"; did you mean "noclip"? help lists the commands`. In the release build a developer command is
unknown, never "not allowed", since it is not registered there at all.

## The command catalog {#catalog}

The built-in commands are kept as data, as the PCSX2 recipes are: `src/console/commands.toml` holds the player set,
and `src/debug/console_commands.toml` will hold the developer set (issue 6). Each `[[command]]` has these keys:

| Key | Meaning |
| --- | --- |
| `name` | Lower case, words joined by `-` or `_`; a name is in one catalog only |
| `group` | The heading `help` and the reference list it under |
| `access` | `player` in the player file, `developer` in the developer file; anything else fails the generator |
| `scope` | `local` (this machine's view) or `session` (the game everyone in a network session plays) |
| `changes` | `true` when a local command changes something, so it waits for the step boundary (default `false`) |
| `summary` | One line: what it does |
| `doubles` | The way in the player already has that it doubles, or `none: a reading` |
| `needs` | What must exist: `game`, `profile`, `level`, `disc` |
| `available` | The Coney feature it waits for |
| `args` | `{ name, type, help, optional, default, choices }`; a required argument never follows an optional one |
| `examples` | At least one `{ line, output }`: a line typed and the lines it prints |

Two files are generated from it, and CI fails when either is stale: `coney-tools console render-cpp` writes
`src/console/commands_table.cpp`, the table `playerCatalog()` returns, and `coney-tools console render` writes the
[console commands reference](console-commands.md), laid out as the script bindings reference is: a table, then an entry
per command with its syntax, arguments, access level, scope and examples. Both refuse a catalog with a problem,
naming each, a command without an example among them ([`console`](coney-tools-references.md#console)). A unit test
checks what the game library compiled: player commands only, each name once and well spelled.

To add a player command: add its entry with an example to `commands.toml`, run both generators, write its handler
and bind it by name in `playerHandlers()` (`src/console/player_commands.cpp`), and test it against fake hosts. The
release set was confirmed by a maintainer on 2026-10-29, and any command added to it is a maintainer's to confirm
([Maintainer decisions](studio/console.md#decisions)).

## When a command runs {#queue}

A line is checked when it is entered (its name, its scope against this machine's role, its needs, the number of its
arguments and each against its type), so a mistake is answered at once. Then:

- a **local** command that only reads runs at once;
- a **session** command, or a local one with `changes`, is queued, and the queue runs before the next simulation step,
  in the order the lines were entered. A step therefore never sees the game change half-way, and a paused game takes
  the command at its next frame, since the step hook runs for held steps too
  ([Update and render](conventions.md#update-and-render)). A queued line whose command was removed, or registered
  again by its mod with other arguments, does not run: the terminal says it changed.

**Scope in a network session.** On a guest, a session command is refused before anything is sent: "only the host can
do this in a network game". Local commands run on any machine. The role comes from network play
(`platform::NetPlay`): hosting is the host, joined is a guest.

**Needs.** A command whose need is not met is refused, saying what is missing (`"mission" needs an active profile`).
With no session host yet, no need is met.

Each line goes to the run log as an engine line, `console: <line>`, stamped with its step, and its output as
`console:   <text>` (a mod command's with `[mod]`); a queued command adds `console: ran <line>` at the step it runs, the
step a replay would run it at ([The run log](run-log.md)).

## The terminal {#terminal}

The terminal model keeps the output, the last 2,000 lines, each marked as the echoed line, a result or an error, and
with the mod that printed it; and the history, the last 200 lines entered, skipping blanks and repeats, walked with
`previous` (Up) and `next` (Down). A typed line goes into the history whatever becomes of it, and the history is saved
at once to `console-history.txt` in the user's data folder, beside `coney.log` ([Where it goes](run-log.md#outputs)).
Lines from an input script are not typed, so they never go into the history.

## Hosts {#hosts}

The console reaches the game only through interfaces it declares in `src/console/hosts.h`, which the game implements
beside what they implement, so a command cannot reach further without a change there that review sees. Each may be
absent; the console then acts as if nothing it would ask for exists.

| Host | Gives | Implemented by |
| --- | --- | --- |
| `SessionHost` | Whether a need is met; the lists of missions, checkpoints and settings; the version and build, the level in play, the settings and their change, the bug report, the quit and the completed missions with their start | `platform::GameConsole`, over what the entry points link ([In the game](#in-the-game)) |
| `ModsHost` | The installed mods' names | Nothing yet (the mods loader) |
| `NetworkHost` | This machine's role: offline, host or guest; hosting, joining and leaving | `platform::GameConsole`, over network play |
| `HistoryStore` | The kept history | `platform::GameConsole`, `console-history.txt` |

Each grows with the commands that need it. The calls the player commands added to the session host and to network
play's have a default that says the game gives nothing, so a fake that needs none of them leaves them out, and the
command that needs one says so when it runs.

## Player commands {#player-commands}

The release set's handlers, `src/console/player_commands.cpp`, are bound to the catalog's entries by name
(`registerPlayerCommands()` over `playerCatalog()`), and each reaches the game only through the console's hosts when it
runs. Every one is in the [console commands reference](console-commands.md) with its syntax and examples; what each
does, and where it reaches, is there and not repeated here.

A few things are not obvious from the reference:

- `quit` asks for the end of the run first (the exit closing the window takes, at the next frame), then a guest leaves
  the host's game as the pause menu's Player 2 quit does and a host's session ends with the game. A run that cannot be
  ended from the console leaves its session alone.
- `set`, `quit` and `disconnect` change something and so wait for the step boundary (the catalog's `changes`); the
  rest read at once.
- Two stand-ins, counted until Coney replaces them: the disc region `version` prints is `NTSC-U` whenever a game
  session exists, as it is the only disc Coney knows (`makeGameConsoleLinks()`), and on a guest `missions` lists the
  guest's own profile where the design has the host's, read-only ([Who may run what in
  co-op](studio/console.md#coop)); both wait on features not built (a second disc, a profile sent to the guest).

`mission` and `missions` are registered with their handlers and wait in the registry for the feature `mission-select`,
which the game marks built when Coney has a mission select; the game gives no profile yet, so until then they stay
out. A host that is absent answers with a line saying so (`network play is not available in this run`), never silence.

## Settings {#settings}

`set` and `get` reach the settings the entry points link (`playerSettings()` in `src/platform/game_console_links.h`),
each a name, a one-line help, the value now and a check-and-apply: `fps_cap` (a whole number 0 to 1000; with a frame
pacer, so not in test mode), `vsync` (`on` or `off`; in a window), `volume` (0 to 1, the master volume; with sound)
and `display_preset` (`ps2`, `xbox`, `enhanced`, `high`). A setting is applied at once and written nowhere: it lasts
for the session, as a command-line option does, and a test checks that a `set` leaves no file behind. A test-mode run
draws the pinned preset whatever `display_preset` says. More settings (`show_fps` among them) come when what they
change can be changed live.

## In the game {#in-the-game}

Both entry points set the console up the same way with `platform::GameConsole` (`src/platform/game_console.h`): the
release build's `coney` takes player commands only, the debug tools' `coney` developer commands as well. It registers
the player catalog's entries that have a handler, gives the console the game's session host (the quit flag and what the
entry points link), network play's host (its role, hosting, joining and leaving) and, outside test mode, the history
file, hands it the input script's `console` lines, and runs the queue in the step hook, after network play's receive.
The entry points call `setLinks()` once the game, the renderer and the sound exist (`makeGameConsoleLinks()` in
`src/platform/game_console_links.h`): the needs (a game and a disc when there is a session, a level in play), the
version and build, the level in play, the settings and the bug report. `quit` makes the frame hook's `beginFrame` say
the run is over at the next frame.
Beside it each entry point puts the console's pad gate in front of the game's input (outside test mode; in the debug
tools' `coney`, in front of their own gate, so SELECT and R3 reach the console first) and, in a window, starts the bar
([The F8 bar](#front-end)), whose events come before the developer overlay's and which draws over it.

## The input script's console action {#input-script}

An [input script](running-input.md#input-scripts) line `FRAME [p1|p2] console LINE` types `LINE` (the rest of the line,
as written, quotes and spacing kept) into the console on frame `FRAME`, for the player of that port. It is checked and
run or queued as a typed line is, its output goes to the terminal model and to the run log, and nothing is drawn. A `#`
still starts a comment, so a console line cannot hold one.

```text
30 console set fps_cap 60
45 p2 console log "level load"
```

The trace tools' reader of input scripts (`coney_tools.input_script`) takes the line too, so a script with console lines
still drives the original, whose pad ignores them: it has no console.

## The F8 bar {#front-end}

F8 opens the console's bar over the game in a window, and F8 or Escape closes it; on a gamepad, SELECT and R3 held
together (the back button and the right stick's press) open and close it. The game goes on while it is open, as
FiveM's console does. It is drawn with Dear ImGui in its own context, at the same physical size at any resolution by
the developer overlay's rule ([Size at any resolution](debug-menu.md#ui-scale)), which lives outside the debug tools
for it (`core/ui_scale.h`, `platform/ui_scale.h`).

| Part | Does |
| --- | --- |
| The bar | Across the top: the title, the View menu (pin the terminal, the command picker), Close, and the reminder of how to close it |
| The terminal | The output, the echoed line dimmed and errors in red, following new lines; the line under it, with up to 8 candidates above it while typing |
| The line | Tab completes (one candidate whole, followed by a space; several to what they share), Up and Down walk the history, Enter runs the line (`console::LineEditor`) |
| The command picker | Shown when the pad opened the console: the commands by group, then one row per argument, then `run` (`console::CommandPicker`) |
| A pinned terminal | Pinned from the View menu, the terminal stays on screen, half see-through, with the console closed and takes no input; kept in `coney-console.ini` |

**The keyboard and the pad while it is open.** The keyboard is the console's: it does not play on port 1, and Escape
does not quit. The console's own gate (`console::ConsolePad`, in the console's model, since the debug tools' input gate
is not in the release build) sits between the pads and the game: the game sees port 1 plugged in with nothing pressed
and port 2 as it is, and port 1's presses drive the picker. The chord's buttons never reach the game while both are
held, and the buttons still held when the console closes stay hidden until let go, by the debug tools' rule
([Opening it and the controls](debug-menu.md#controls)). No control of the original holds SELECT and R3 together
([Input and the gamepad](studio/console.md#input)).

**The picker with a pad.** Up and down (the d-pad, or the left stick pushed well over) move between the rows, cross
chooses, circle goes back and, on the commands, closes the console. A command without arguments runs at once. On an
argument's row, left and right step through the values its type completes to (a mission's from the session host's
list, `on` and `off`, a choice's), or change a number by one; a required argument starts at its first value, one with
a default at the default, and changing one puts the arguments after it back to their defaults. Cross on the last row
runs the line, which the terminal shows as typed. A `text` argument cannot be typed with the pad yet: the debug
menus' pad typing has not moved into the console's model, and the platform's on-screen keyboard is not used yet.

**Where it lives.** The model (the line, the picker, the gate) is in `src/console/` in `coney_core`; the bar is
`coney_console_ui` in `src/platform/` (`ConsoleView` draws it in a frame someone else begins, so its tests run on a
bare Dear ImGui context; `ConsoleUi` gives it the window, the backends and the events). Both entry points start it
when there is a window with an OpenGL context; a headless run and the GPU renderer run without it. Dear ImGui is
fetched in every build for it, and `coney_console_ui` and `coney_devtools` are the only targets that link it
([The debug tools and the game](building.md#debug-tools)). `coney.release.has_console_bar` finds the bar's settings
file name in the release executable, so the release build is proved to carry it.

## Off in test mode {#test-mode}

A test-mode run (`--headless`, `--frames`, `--input-script`, `--screenshot`) reads no device and touches no user folder
([Update and render](conventions.md#update-and-render)), and the console keeps to that: it takes lines only from the
input script (a typed line is ignored) and neither reads nor writes `console-history.txt`. The console's unit tests
prove both on the model (`tests/console/console_test.cpp`). The bar keeps to it too: F8 is not caught, the pad gate is
left out of the input chain (so a scripted SELECT and R3 reach the game as on a PS2), and `coney-console.ini` is
neither read nor written, so no pinned terminal from an earlier run appears. `--console-open N` opens the bar for the
first N frames, the one way a test-mode run draws it ([The console](running-options.md#console)), and
`coney.console_bar_leaves_frame` checks that the last frame after it closes is byte-identical to a run without it.
`--capture-dir` comes with the captures ([Off in test mode](studio/console.md#test-mode)).
