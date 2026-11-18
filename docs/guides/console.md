# The Coney console

The Coney console is the terminal F8 will open in the game: a line to type a command into, the output above it, and
the commands every build offers. This page is what is built of it; the whole design, with the front end, the panels,
the commands' handlers and the mods' commands still to come, is [Coney Studio: the Coney console](studio/console.md).
Every command is listed in the generated [console commands reference](console-commands.md).

## What is built {#built}

The console's core, build issue 1 of [the design](studio/console.md#build-issues): the model in `src/console/` with no
user interface, its catalog and generators, the input script's `console` action and test mode's rules; and the player
commands, build issue 2: the handlers of the release set, over the game session's and network play's hosts
([Player commands](#player-commands)). Not built yet: the F8 front end (issue 3), the panels and the commands that open
them (`log`, `resmon`, `perf`, `net` and `profiler`, issues 4 and 5: they wait in the catalog for their feature and
are unknown when typed), the developer set (issue 6) and mod commands through the SDK (issue 7). Until the front end
exists, lines reach the console only from an input script.

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
does, and where it reaches:

| Command | Scope | What it does | Reaches |
| --- | --- | --- | --- |
| `help [command]` | local | Lists the present commands by group, under the banner `Coney console`, a developer's marked `[dev]` and a mod's with its name; with a command, its usage, summary and arguments | the registry |
| `clear` | local | Empties the terminal's output | the terminal |
| `version` | local | Coney's version and build, the disc's region and the renderer | the session host |
| `quit` | local, at the step boundary | A guest leaves the host's game first, as the Player 2 quit does; then the game ends at its next frame, through the exit closing the window takes | the session and network hosts |
| `where` | local | The level in play, the checkpoint, the step and the play time, as the run log's stamp reads them | the session host, the run log |
| `bugreport` | local | The run log's last 30 s, every level, into the log file, saying where; through the logger's dump when the host has no log file | the session host, the run log |
| `settings`, `get <setting>` | local | The settings with their values, and one of them | the session host |
| `set <setting> <value>` | local, at the step boundary | Changes a setting for this session only: nothing is written to a file ([Settings](#settings)) | the session host |
| `missions`, `mission <n> [checkpoint]` | local; session | The completed missions with their checkpoints; starts one as the mission select does (the profile's list is read again when it runs, so a mission or a checkpoint that is not there is refused) | the session host |
| `host [port]`, `connect <address>` | session | Hosts, or joins, from an offline game; refused in a session already, with a word to `disconnect` first | the network host |
| `disconnect` | local, at the step boundary | A guest's is the Player 2 quit, the host's ends the session; "not in a network session" offline | the network host |

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

## The input script's console action {#input-script}

An [input script](running-input.md#input-scripts) line `FRAME [p1|p2] console LINE` types `LINE` (the rest of the line,
as written, quotes and spacing kept) into the console on frame `FRAME`, for the player of that port. It is checked and
run or queued as a typed line is, its output goes to the terminal model and to the run log, and nothing is drawn. A `#`
still starts a comment, so a console line cannot hold one.

```text
30 console set fps_cap 60
45 p2 console log "level load"
```

## Off in test mode {#test-mode}

A test-mode run (`--headless`, `--frames`, `--input-script`, `--screenshot`) reads no device and touches no user folder
([Update and render](conventions.md#update-and-render)), and the console keeps to that: it takes lines only from the
input script (a typed line is ignored) and neither reads nor writes `console-history.txt`. The console's unit tests
prove both on the model (`tests/console/console_test.cpp`). The rest of the design's test-mode rules come with what
they govern: the pad chord and F8 with the front end, `--console-open` and `--capture-dir` with the front end and the
captures ([Off in test mode](studio/console.md#test-mode)).
