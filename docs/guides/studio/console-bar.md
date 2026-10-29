# Coney Studio: the Coney console's F8 bar

The menu bar of [the Coney console](console.md): its menus, the panels they open and what each panel reads, the mods'
resource monitor, the engine's frame and step timings, the log viewer, the network figures and profiler captures. The
console itself (the terminal, the commands, access levels, input, the code boundary, the tests and the build issues) is
[the console's page](console.md); this page is the part of that design the bar's panels are. The parent issue is
[#1054](issue:1054); this page is [#1064](issue:1064).

**Status: design.** Nothing on this page is built. It is a design in the sense of
[Page kinds](../writing-docs.md#page-kinds).

## Contents {#contents}

1. [The bar](#bar): its menus and pinned panels.
2. [Resources](#resources): the mods' monitor.
3. [Performance](#performance): the engine's timings.
4. [Log](#log): the run log, filtered.
5. [Network](#network).
6. [Profiler captures](#captures).

## The bar {#bar}

F8 opens the console: a bar across the top of the screen and the terminal under it, over the game. The bar's menus
open its panels, each a window that can be moved, resized, docked to an edge and **pinned**: a pinned panel stays on
screen when the console closes, drawn but taking no input, as FiveM shows a resource graph with its console closed.

| Menu | Panels and actions | Level |
| --- | --- | --- |
| **Console** | The terminal; *Clear*; *Write bug report* (`bugreport`); *Quit* | player |
| **Resources** | The [resource monitor](#resources): one row per running mod, and the game scripts | player |
| **Performance** | The [engine's frame and step timings](#performance); *Record capture* | player |
| **Log** | The [log viewer](#log) | player |
| **Network** | The [network figures](#network), in a network session | player |
| **Mods** | The installed mods as the `mods` command lists them, each with its commands' buttons (start, stop, restart, enable, disable) | player |
| **Debug** | Every page of the debug menus as a window, as the developer overlay shows them ([The developer overlay](../debug-menu.md#the-developer-overlay)) | developer |

The panels read the game; none changes it, except through the commands their buttons run, which go through the same
registry and queue as a typed line. Where the panels were and which were pinned is kept for the next run in
`coney-console.ini` in the user's data folder; a test-mode run keeps nothing.

## Resources: the mods' monitor {#resources}

The release-ready answer to "which mod makes my game slow", as FiveM's `resmon` is, and the F8 front end of the SDK's
[monitor](lua-sdk.md#monitor): the SDK owns the numbers, Studio's Monitor panel and this panel show them, so there is
one count and two front ends. One row per running mod, one per state when a mod runs a server and a client state, and
a row for the game scripts (the game state) to measure against:

| Column | What it shows | Where it comes from |
| --- | --- | --- |
| Mod | Name and version, `server` or `client` | The mods loader |
| Time per step | The real time the mod's part of the step took (its `update` handlers, threads, scheduled calls and other entry points): the last step, the mean and the worst of the last 30, as the SDK's monitor counts it | The monitor's timing, with the real clock the frame pacer reads ([The monitor](lua-sdk.md#monitor)) |
| Share | That time as a share of the whole step | The `step` zone |
| Instructions | Lua instructions run per step, only while the panel is open or pinned or a capture records | The count hook (`lua_sethook`), set only then: with no budget for now, nothing else runs it ([Limits](security.md#limits)) |
| Memory | Bytes the state holds now, and its peak | The state's allocator, which counts for the monitor; there is no cap for now |
| Threads | Threads running and waiting | `Coney.CreateThread` ([Threads and waits](lua-sdk.md#threads)) |
| Handlers | Event handlers and callbacks registered, and how many ran this step | `Coney.On`, the natives' callbacks |
| Natives | Native calls per step; opening the row lists the ten most called, with counts | The value bridge, which every native call crosses ([The value bridge](lua-sdk.md#bridge)) |
| Exports | Calls into other mods' exports, and from them | The exports bridge ([Mods: Dependencies and exports](mods.md#dependencies)) |
| Errors | Errors raised since the mod started, the last one's file and line | The project state |

- **The counts are exact and the times are not.** Instructions, memory, threads, handlers, native calls and errors are
  counted by the runtime and are the same in every run of the same input; the times are real time on the player's
  machine, shown and plotted, never read by a step ([Determinism](console.md#determinism)). In test mode the time column
  is off and the counts stay, as the SDK's monitor has it.
- **Cheap while closed.** Memory, threads, handlers and errors are counted anyway, for the monitor; the times and the
  instruction count are taken only while Resources is open or pinned, or a capture records, and otherwise cost one
  branch per call, as a zone does with measuring off.
- **It only reads.** As the SDK's monitor, the panel never stops, slows or changes a mod: the player sees which one is
  slow or growing and stops it with `stop` or the Mods menu.
- **Sorting and plots.** Any column sorts; a row's time per step plots over the last 300 steps.
- **In Studio** the same model is a docked panel, for the project in Play ([The code boundary](console.md#boundary)).

The rows exist once the mods loader and the project runtime do (the SDK's build issues 2 and 3), and read the counts
of the SDK's monitor (its build issue 10); until then the panel shows the game scripts' row alone.

## Performance: the engine's timings {#performance}

The [profiler](../performance.md#zones) is already part of the game: its zones and counters are in the game library,
and the release build carries them. The Performance panel is their release front end, as the debug menus' Performance
page is the developers' ([The in-game view](../performance.md#overlay)):

- **The frame and the step.** Frames a second, the frame time and the step time, plotted over the last 300 frames, with
  the mean, the 95th percentile and the worst of the last 60, and the steps run per second (30 at 1x).
- **The zones** (`step`, `play.humans`, `humans.brains`, `render`, `present.swap` and the rest), each one's mean over
  the last 60 frames, nested as they nest.
- **The counters** the release build keeps: draws, triangles and the scene counters by kind
  ([Counters](../performance.md#counters)).

Measuring is on while the panel is open or pinned, or a capture records, and off otherwise, as the debug page's
*Measure* is.

## Log: the run log, filtered {#log}

The [run log](../run-log.md), read in the game. The panel keeps its own copy of the lines since the console was first
opened, up to the last 10,000, fed by a listener on the logger (the ring keeps only 30 s,
[The ring](../run-log.md#ring)):

- **Filters**, combined: by level (`error`, `warn`, `info`, and `detail` where the run keeps it), by category (`player`,
  `ai`, `combat`, `script`, `world`, `engine` and a new `mods`, [Levels and categories](../run-log.md#categories)), by
  mod (the lines a mod printed or raised), and by text. `log error` and `log yard-brawl` open the panel filtered.
- **Follow.** The view follows new lines until scrolled up, and again once scrolled to the bottom.
- **Copy.** Selected lines copy as the log file writes them. A line the run log keeps out of the file, the game's own
  text that a level script prints ([What a log may hold](../run-log.md#rules)), shows in the panel, as it shows on the
  console today, but copies as the file writes it, with its length only, so a pasted log never carries game text.
- **Write bug report** writes the 30 s ring into the file and names it, as `bugreport` does.

The new `mods` category holds what a mod's scripts print and the errors they raise, each line tagged with the mod's
name (`mods: yard-brawl: wave 3 started`), within the print limits of the sandbox.

## Network {#network}

In a network session, the figures the debug menus' Network page shows, read-only: the ping (plotted), loss, bytes and
packets a second, and the players ([Link quality](../multiplayer.md#link-quality)). Host, Join and Disconnect are the
`host`, `connect` and `disconnect` commands; the simulated network stays the developers' ([The pages](../debug-menu.md#pages)).

## Profiler captures {#captures}

`profiler record <frames>` (at most 3,600) records every frame's zones, every mod's calls with the entry point, the
file and the line of the handler, and each step's boundary, then `profiler save` writes them as a Chrome trace event
file, the format Chrome's performance view and Perfetto open, as FiveM's `saveJSON` does. The file goes into
`captures/` in the user's data folder, named by the level and step (`capture-level95-step4518.json`), and the terminal
prints its path. A capture holds names, files, lines and times, never script source or game text, so it can be attached
to a bug report.
