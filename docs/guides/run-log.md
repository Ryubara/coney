# The run log

Every line Coney writes about a run goes through one log, the run log (`src/core/log.h`), and comes out stamped with
the simulation step it happened in, the play time that step stands for, its level and the mission checkpoint. A bug
report, or an agent looking into one, can then see what happened and when, and line it up with a trace or a
playthrough. The original game has nothing like it: it is Coney's own debug tooling, and it never changes what the
game does.

It is not the [event log](running-play-level.md#event-log) (`--event-log`, a CSV of what a player would notice, for
comparing a run with the original) nor a [trace](running-play-level.md#tracing) (`--trace`, values per step): those are
written for a program to diff; the run log is written for a person to read.

## Reading a line {#reading}

```text
[step 4518 | 02:30.6 | info cp2] combat: knock-down: human 14 warr_cl by player 1 human 3 warr_re_cv
```

| Part | Meaning |
| --- | --- |
| `step 4518` | The simulation step: fixed 1/30 s steps the game-mode stack has run since start-up. Never real time, so a test-mode run writes the same lines every time |
| `02:30.6` | The play time the step stands for (`mm:ss.s`, the step divided by 30, tenths rounded down; the minutes go past 59). Time spent paused or in a held step does not count |
| `info` | The level: `error`, `warn`, `info` or `detail` |
| `cp2` | The checkpoint: the one the level started at, then each `SetCheckPoint` the scripts call; `cp-` in the front end and before any level |
| `combat:` | The category (below) |
| the rest | The text, which names its subsystem first (`knock-down:`) |

The step counts from start-up, through the front end and the loading screens. The other records of a run that number
steps (`--event-log`, and `--input-script` and `--frames` with `--play-level`) count from the level's first step of
play instead, so the two differ by the steps before it.

## Where it goes {#outputs}

- **The console.** Lines at `info` and `detail` go to standard output, `warn` and `error` to standard error, as Coney
  always printed them; the usage text and the `--pad-pipe` protocol lines (`@ev`, `@obs`) are not log lines and carry no
  stamp.
- **The log file.** A normal run writes `coney.log` in the user's data folder (`%APPDATA%\Coney\Coney\` on Windows,
  `~/.local/share/Coney/Coney/` on Linux, `~/Library/Application Support/Coney/Coney/` on macOS) and first renames the
  last run's to `coney.prev.log`, so the log of a run that crashed survives the next start. `--log-file FILE` writes
  to `FILE` instead. A test-mode run (`--headless`, `--frames`, `--input-script`, ...) touches no user folder, so it
  writes a file only when given `--log-file`. Each line is flushed as it is written, so a crash loses nothing.
- **In the release build.** The build a player gets ([without the debug tools](building.md#debug-tools)) starts the
  same outputs: the console, `coney.log` and the crash handlers. It refuses `--log-file` and `--log-verbose`, which
  are developers' options, and has no Time page to write a bug report from.

## Levels and categories {#categories}

The file and the console keep `info` and up. `--log-verbose ai,combat` keeps the `detail` lines of those categories
too (any of the six, comma-separated, or `all`). Every line, `detail` included, also goes into [the ring](#ring).

| Category | What its lines are about |
| --- | --- |
| `player` | What player 1 does: pick-ups, drops, throws, tags, thefts, muggings, deals, the flash |
| `ai` | What the AI decides: gang calls, revives, the fighters |
| `combat` | Damage (`detail`), knock-downs and deaths |
| `script` | The level scripts: errors, bindings Coney lacks, what the scripts print (`detail`, console only) |
| `world` | Level loads, checkpoints, cutscenes, the level flow, objects, cars |
| `engine` | Everything else: start-up, sound, pads, the front end's screens, the summaries at the end, the debug menus' [Entity editor](debug-menu.md#entity-editor) edits (`entity edit: applied 7 health 40`) |

Most subsystems log through a plain text callback (`std::function<void(std::string_view)>`) and name themselves at the
start of each line. Those lines keep their text and get their category from that prefix (`logging::routeOf()`, the
table `kPrefixRoutes` in `src/core/log.cpp`): `pickup:`, `throw:`, `theft:`, `swap:`, `tag:`, `uncuff:`, `mugging:`,
`deal:`, `flash:`, `lock pick:`, `game over:` and `player:` are `player`; `scout:`, `revive:`, `fighters:` and `targets:`
are `ai`; `damage:`, `knock-down:` and `death:` are `combat`; `script:`, `preload:` and `CfgLevelName:` are `script`,
with `script error:` at `error` and `script print:` at `detail`; `level flow:`, `level:`, `gameplay:`, `checkpoint:`,
`cutscene:`, `scene:`, `scene sound:`, `scenes:`, `cast:`, `objects:`, `cars:`, `humans:`, `world:`, `water:`,
`lighting:`, `room smoke:`, `path data:`, `missions:`, `mission complete:` and `mission failed:` are `world`. Anything
else is `engine` at `info`. Coney's own failures (`coney: ...`) are `engine` errors and warnings.

## The ring {#ring}

The logger keeps the last 30 s of steps (900 steps, and at most 20,000 lines), every level and every category, in
memory. It is written into the log file, framed by a first and a last line naming why:

- when a `CONEY_ASSERT` fails (after the assertion's own line on standard error);
- when the process crashes (a segmentation fault, an illegal instruction, a floating-point fault or an abort);
- when someone asks for a bug report from the debug menus (below).

So a log of a run that went wrong ends with everything the game did in its last half minute, the `detail` lines the
file left out included. Without a log file (a test-mode run without `--log-file`) the dump goes to standard error.

## Bug reports {#bug-reports}

**Players.** Right after the bug, open the [debug menus](debug-menu.md), choose **Time**, then **Write bug report
log**; a notice says how many events were written and where. Attach `coney.log` to the [bug report](issues.md#report)
(the form's *Log* box). After a crash, attach `coney.prev.log` if Coney was started again since. With the step and
checkpoint in the report's steps (from the log's last lines) a developer can find the moment at once.

**Agents investigating a bug.** Start from the reporter's log: the last lines before the dump say where the run was
(level, checkpoint, step), and the dump shows what led up to it at every level. To reproduce, run the same start with
`--log-file` and the categories that matter verbose, and compare the lines step by step:

```sh
coney --disc I:/ --headless --play-level level2 --checkpoint 3 --frames 900 \
      --input-script run.txt --log-file ../../scratch/<branch>/run.log --log-verbose combat,ai
grep "combat:" ../../scratch/<branch>/run.log
```

The log of a test-mode run is the same every time, so a change in it after a fix is the fix's effect. A log goes into
the scratch folder, never the repository.

## What a log may hold {#rules}

Logs are attached to public issues, so a line names things and gives numbers, nothing more:

- **Never the game's own text or script source**: no hint, caption or menu text, no strings out of the scripts. Names
  (a level, a scene, an object type, a character model), ids, handles, numbers and positions are fine. What the
  level scripts print (`script print:`) is the game's own text: it goes to the console only; the file and the ring
  keep its length (`logging::Privacy::LocalOnly`).
- **Paths** are written through one function, `platform::logPathText()` (`src/platform/log_output.h`), so how a path
  reads in a log is decided in one place.
- **No real time.** A line holds steps, game time or counts, never a clock reading, so test-mode logs stay
  deterministic ([Update and render](conventions.md#update-and-render)).

## Adding a line {#adding}

1. **Pick the category and the level.** `info` is what a person reading a bug report wants to see happen (a level
   load, a death); `detail` is every step of it (each hit's damage) and is only shown with `--log-verbose`; `warn` is
   something missing or odd that the game handles; `error` is a failure it goes on without.
2. **Write it where it happens**, with the subsystem's name first:

   ```cpp
   #include "core/log.h"

   logging::info(logging::Category::Combat, std::format("death: {} by {}", logName(), attacker));
   ```

   A class that already logs through a text callback (`m_log`, `m_print`) keeps doing so, with a prefix the routing
   table knows (add one to `kPrefixRoutes` and to the table above for a new subsystem); its tests can then keep
   capturing the callback.
3. **Name people and things the same way everywhere.** A human is its `Fighter::logName()` (`human 12 warr_cl`,
   `player 1 human 3 warr_re_cv`; the AI's brains set it from the script handle and the model), an object its handle
   and type name, a level its name.
4. **Test it.** A unit test can watch the log with `logging::setSink()` and must put it back with `logging::reset()`
   (see `tests/core/log_test.cpp`); text-callback lines are tested through the callback.
5. **List it** below.

## The events so far {#events}

| Line | Category, level | Where |
| --- | --- | --- |
| `level load: LEVEL checkpoint N` | world, info | the level's script step (`GameplayMode`) |
| `checkpoint: start N` | world, info | `SetCheckPoint` |
| `cutscene: start NAME (id N)`, `skip`, `end ... [, skipped]` | world, info | the scene player |
| `pickup: took object H [in hand] (type T, kind K, hold style S)` | player, info | the play mode's pick-up |
| `pickup: dropped object H (...)`, `pickup: threw object H at (VX, VY, VZ) m/s (...)` | player, info | the play mode's drop and throw |
| `damage: A hit V with anim N for D, health H of M left` | combat, detail | the fighter taking a hit |
| `knock-down: V by A` (or `by a hold`, `by his victim's escape`) | combat, info | the fighter's reaction, a hold putting him down, a grab broken |
| `death: V by A` | combat, info | the fighter running out of health |

Every subsystem adds its own as it is worked on (AI goals and targets, gangs, triggers and objectives, spawns, slow
frames, ...); the list grows here.
