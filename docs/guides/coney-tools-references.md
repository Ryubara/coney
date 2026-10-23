# The coney-tools reference commands

The commands that keep Coney's generated reference material current: [`progress`](#progress), the progress
tables of the README and the docs; [`natives`](#natives), the script-binding masterlist; [`refs`](#refs), the game
reference lists; [`lua`](#lua), which decompiles the game's compiled scripts for research; and
[`missions`](#missions), the mission status pages. Run them from inside the checkout, as the
[overview](coney-tools.md#groups) says; the groups that read the disc take it as
[the disc commands](coney-tools-disc.md#naming-the-disc) do.

## progress {#progress}

The [Progress](../progress/index.md) page and the badges in the Progress section of `README.md` are generated from
three inputs:
`docs/progress/functions.toml` (the original functions Coney reimplements), `docs/progress/totals.toml` (the
denominators, taken from the [source map](../research/source-map.md); its file ranges are
`research/source-files.toml`) and the status table of the
[roadmap](../roadmap.md). The page says what the numbers mean; this section says how to run the commands.

```sh
uv run --project python coney-tools progress show [--json]
```

Prints the reimplemented and researched shares, the milestones and the subsystems that have reimplemented code.
`--json` prints everything, per subsystem and per coverage category, for other tools. It exits with 1 when
`functions.toml` and the `@orig` tags in `src/` disagree, and lists each problem.

```sh
uv run --project python coney-tools progress check
```

Checks the inputs and exits with 1 when they disagree; CI runs it. Nothing generated is committed: the tables of the
progress page and the README's badge data are produced by the documentation build (`mkdocs_hooks.py`), which
publishes them from `main`, so pull requests never conflict on them. `check` fails on:

* an `@orig` tag in `src/` with no `[[function]]` in `functions.toml`, or the other way round, or the two naming the
  function differently;
* a `subsystem` that is not the one whose range in `totals.toml` holds the address (the message names the right
  one; outside every range it is `unattributed`);
* an address inside middleware or outside `.text`, an address listed twice, or sizes that overlap;
* a progress page without its single `<!-- progress:generated -->` marker, where the build puts the tables and
  pictures, or a history page without its `<!-- progress:history -->` marker.

`mkdocs build --strict` shows the tables on the page locally too, and fails on the same problems.

The pictures come from `progress_grid.py` (one square per function, from the Ghidra listing, the tracker's measures
and the file ranges of `research/source-files.toml`), `progress_picture.py` (layout and SVG), `progress_history.py`
(one point per day from git) and `progress_site.py` (the page's HTML and the files of the site); `mkdocs_hooks.py`
calls them. The
page's tooltips are `docs/javascripts/progress-grid.js`, the colours `docs/stylesheets/progress-grid.css`, and what
they mean is on [the page](../progress/index.md#the-picture). Where the fidelity audit's results will be read is
`progress_grid.load_audited`: it returns no addresses until the audit stores a machine-readable result on `main`, and
the picture then draws the Audited state.

```sh
uv run --project python coney-tools progress render <folder> [--no-history]
```

Writes what the build publishes to `<folder>`, to look at it without a docs build: the progress and history pages as
the build fills them (`progress/index.md`, `progress/history.md`), the badge files (`progress/badges/*.json`,
shields.io endpoint JSON that the README's badges read), the pictures (`progress/progress.svg`,
`progress-size.svg`, `missions.svg`, `history.svg`), the raw numbers (`progress/progress.json`), the per-function
table the tooltips read (`progress/functions.json`) and the history (`progress/history.json`). It rebuilds the daily
history from git, a few seconds a day; `--no-history` keeps it to today's point. It refuses to write while the inputs
disagree, as `check` finds.

The docs build does the same, with the history limited so a pull request's build stays quick: it keeps the days of the
file named by `CONEY_PROGRESS_HISTORY_CACHE` (the previously published `progress/history.json`) and today, and measures
the missing days only when `CONEY_PROGRESS_HISTORY=full`. The `docs` workflow sets both on a push to `main`: it
downloads the published file (a missing one just means every day is measured), builds with `full` and deploys the
result, so each deploy extracts one commit. A cached day counts only for the commit it was measured at, and a day
that could not be measured is recorded with its commit and not tried again. The file also records a digest of the
tracker's measuring code (`progress.py`, `progress_research.py`): when that code changes, the cache is ignored and
every day is measured again, so the chart never mixes two definitions. A pull request's build has no cache (only the
deploy learns the site's address), so its History page shows today alone; locally, point
`CONEY_PROGRESS_HISTORY_CACHE` at a downloaded `history.json` to see the days.

```sh
uv run --project python coney-tools progress sizes [DISC] [--fill]
```

Checks the size of every function in `functions.toml` against the executable `SLUS_212.15` on your own disc (`DISC`
works as for the `wad` commands). The executable has no symbols, so the command takes every `jal` target, every
function pointer in `.data` and every address into `.text` that code builds with a `lui` / `addiu` pair (the function
pointers handed over in code, such as the script binding wrappers) as the start of a function, and a function's
**span** runs to the next of those starts, alignment padding included. Before the pairs were counted, a binding
wrapper's span ran over its unseen neighbours (38,440 bytes for `HuCreate`'s 480). A given size larger than the
span is an error (exit code 1); one smaller than the code before the padding, or an address no call reaches, is a
warning. `--fill` writes the span into each entry that has no
`size` yet. It prints only the addresses and sizes of the functions already listed.

The span is an upper bound: a function that nothing calls directly and no pointer names (one reached
only by a tail jump) is not seen as a start, and the span of the function before it includes it, unless that function
is itself listed in `functions.toml`: a listed function always ends the span of the one before it. When `--fill`
gives a size that looks too large for what the research page describes, check the size Ghidra shows.

## natives {#natives}

The script-binding reference (`docs/references/bindings/`, under Game references) is generated from
`research/bindings/<category>.yaml` (schema: [Research workflow](research-workflow.md#bindings)).

```sh
uv run --project python coney-tools natives render [--check]
```

Checks the YAML and writes `docs/references/bindings/`: `index.md`, one page per category, each binding under an anchor
of its name in lower case (`hud.md#hudsetobjective`), `mission1.md` (the first mission's coverage) and `story.md`
(the later levels' coverage). A page left over from a removed category is deleted. Run it after editing the YAML and
commit the pages with the change. With `--check` it writes nothing and exits with 1 when a page is stale; CI runs it
that way. Either way it refuses to go on while the YAML has problems, and lists them: an unknown key, a file that is
not a category, a missing description on a described entry, a thorough entry with an argument left unexplained, an
argument name used twice, usage counts that contradict each other, a `levels` list out of story order, a name listed
twice.

```sh
uv run --project python coney-tools natives coney [--check]
```

Sets the `coney` key of every entry from Coney's binding table, `kBindings` in `src/scripting/script_bindings.cpp`:
`real` is *implemented*, `routed` (handed to a stand-in for a subsystem Coney lacks) is *partial*, `stub` and
`recording` are *not implemented*, and so is a binding the table does not list; the default is written as no key. Run
it, then `natives render`, after changing the table. It fails when a line of the table is not one `kind("Name", ...)`
entry, or names a binding the masterlist lacks. With `--check` it writes nothing and exits with 1 when a status is
stale; CI runs it that way.

```sh
uv run --project python coney-tools natives cpp [--check]
```

Writes `src/debug/native_signatures.cpp`, the C++ table the [debug menus](debug-menu.md#natives) build their argument
editors from: every binding's name, category, the type of each argument (number, integer, handle, boolean, string, a
table of numbers or strings with its fixed count, userdata) with its default, and the types of its results. A number
argument whose description says it is a handle gets the handle editor. Only names, types and defaults go in, never a
description. Run it after editing the YAML and commit the file with the change; with `--check` it writes nothing and
exits with 1 when the file is stale, as CI runs it.

```sh
uv run --project python coney-tools natives mission1 [DISC] [--check]
```

Reads the first mission's compiled scripts (`level99.lua` and its three chapter scripts) and `global.lua` from your
disc (the `game_dir` of `coney.local.toml` when no disc is given), and sets every entry's `usage.mission1`: true when
those scripts name the binding, or a `global.lua` helper they reach does (followed by name, as a `Table.field` or as a
callback string), or one of the two helpers the engine calls when a mission ends (`UnlockAndLoad`, `runNextMission`).
It prints the coverage (bindings, traced, Coney status), which `natives render` also publishes as
[Mission 1 coverage](../references/bindings/mission1.md). Only names are read; nothing from the disc is written. Run
`natives render` after it. With `--check` it writes nothing and exits with 1 when a marker is stale; it needs the disc,
so CI does not run it.

```sh
uv run --project python coney-tools natives missions [DISC] [--check]
```

The same for the rest of the story: for each later level of `STORY_LEVELS` (`coney_tools/natives.py`: missions 2-18
and the hub in story order, then the flashbacks and the Armies of the Night levels) it reads the level's scripts
(`levelNN.lua` and its `levelNN_*.lua` chapters, found by name in the WAD names list, without the language files and
the scene tests) with the same reach rule, and sets every entry's `usage.levels` to the numbers of the levels that can
call it (no key when none). It prints each level's bindings and how many are new (called by no earlier level, the
first mission included) and traced; `natives render` publishes them as
[Story coverage](../references/bindings/story.md). Run `natives render` after it; `--check` as for `mission1`.

```sh
uv run --project python coney-tools natives stats
```

Prints the counts by category, evidence level, detail and Coney status, and how many bindings the game's scripts
call.

## refs {#refs}

The [Game references](../references/index.md) are generated from the lists in `research/references/`
([Research workflow](research-workflow.md#reference-lists) has their schema and rules).

```sh
uv run --project python coney-tools refs extract [DISC] [--only LIST ...] [--names FILE]
```

Reads your own disc (`DISC` works as for the `wad` commands) and refreshes every list, or only the ones named
(`characters`, `levels`, ...): the configuration calls of the compiled scripts, the Character List of
`warriors.glr`, every animation clip, three tables of the executable, the scene list `scene_list.cnk`, the
movies in the disc's `PSS` folder (when `DISC` is a folder) and the WAD's entry names. It keeps every
hand-written field and every entry it did not produce, then renders the pages. `--names` adds WAD names found
another way (a file with one name per line, or the last word of each line, as `wad names` writes), each kept only
when it hashes to an entry; the names already in `wad-names.yaml` are always used. A full run takes about half a
minute and keeps only names, ids and numbers.

```sh
uv run --project python coney-tools refs render [--check]
```

Checks every list against its schema and writes `docs/references/index.md` (with the families of
`still-to-list.yaml`) and one page per list. With `--check` it
writes nothing and exits with 1 when a page is out of date; CI runs it that way. A list that fails its schema stops
both, with each problem named (exit code 2).

```sh
uv run --project python coney-tools refs compress-images [FOLDER]
```

Rewrites every PNG below `docs/references/images/` (or `FOLDER`) in place as a 256-colour palette image with
transparency. `coney --render-references` writes full-colour images of about 26 KB each; with a palette they take
under a quarter of that and look the same at 256 pixels. The quantizer is deterministic, so a re-render gives the same
files. A file the palette would not make smaller (most radar icons) is left as it is. Run it after rendering and before
`refs extract` links the images.

## lua {#lua}

The game's 467 scripts are compiled Lua 4.0 ([Compiled Lua chunks](../research/formats/lua-chunks.md)). `lua
decompile` turns them back into readable Lua for research, with Coney's own decompiler (the `lua4` package under
`python/src/`; how it works and what it gets back: [Lua 4.0 bytecode](../research/formats/lua-bytecode.md)). Its
output is game script text: it is written only outside the repository, and research pages describe what a script does
in our own words ([The game's scripts](research-workflow.md#scripts)).

```sh
uv run --project python coney-tools lua decompile [DISC] --all [--out DIR] [--roundtrip] [--chunks DIR] [--luac PATH]
uv run --project python coney-tools lua decompile [DISC] NAME OUT_DIR [--roundtrip] [--chunks DIR] [--luac PATH]
```

`--all` decompiles every chunk of your disc (`DISC` works as for the `wad` commands) into the **shared folder**,
`lua_dir` in `coney.local.toml` (default `<workspace>/scratch/lua`), so every worktree and agent reads the same copy;
`--out` writes elsewhere. Each chunk becomes `<name>.lua` (the WAD's file name, or the entry's hash in hex when the
name is not known). Beside them it writes:

* `index.md` and `index.json`: per script, every function (named by the global or table field that holds it, with
  its path among the chunk's functions and the line it is defined on), the natives it calls (the names in
  `research/bindings/`), the script functions it calls, and the handlers it registers (calls of a native whose name
  ends in `Handler` or `Callback`, `RegisterUpdate`, `ScheduleFunc*` and `TriggerSphereCfg`) with their constant
  arguments (`python/src/coney_tools/lua_index.py`). Start a level's research here.
* `fingerprints.txt`: a hash of every distinctive line of every script, which the guard below reads.

`NAME OUT_DIR` decompiles one chunk, by file name (`level99.lua`) or hash, into `OUT_DIR`. Both refuse an output
folder inside a checkout. `--chunks DIR` reads the chunks from a folder (`wad extract` or `extract` output) instead of
the disc. The output prints counts only: chunks, functions, the functions that could not be read with the kind of
problem (a pc and an opcode, never a constant). A chunk the loader refuses (another Lua version, cut short) is named
with the reason: `--all` goes on with the rest, still writes the fingerprints and the index for every script it wrote,
and exits 1; one chunk by name is a one-line error.

`--roundtrip` compiles each output with a stock Lua 4.0.1 `luac` and compares the result with the original chunk,
function by function: instructions, constants, parameters, vararg flag, stack size and nested functions. It prints how
many chunks match, the kind of each first difference and the names of the chunks that failed, never the script's
text, and exits 1 when any differs. On the NTSC-U disc all 467 match (about two minutes). The `luac` is `--luac`,
else `luac_program` in `coney.local.toml`, else the one `coney-tools workspace init --fetch lua` builds from Lua's
pinned source in `<workspace>/tools/lua-4.0.1/bin/` ([Fetching the research tools](workspace.md#fetch)).

**The guard.** `.claude/hooks/lua_guard.py` refuses decompiled Lua in the repository: every file the command writes
ends with a marker line (`-- lua4 decompile <hash> <name>`), and text holding that line is refused. So is text in
which three of any five distinctive lines in a row (24 characters or more) have their fingerprint in the shared
folder's `fingerprints.txt`. A line is compared without the markup a paste picks up: quote markers, list bullets and
numbers, inline-code backticks, `--` before commented-out code and a trailing `--` comment are stripped, and each cell
of a table row counts as a line. One quoted line passes, and so do short lines. It runs in pre-commit
(`no-decompiled-lua`, on every text file) and as a Claude Code hook before every Write, Edit, MultiEdit and
NotebookEdit into a checkout ([The hooks](enforcement.md#hooks)). Without the shared folder (CI, or before the first
`--all`) only the marker is checked. A shell command that writes a file is not seen by the hook; pre-commit catches
the commit. Text that never enters a checkout is not checked at all: an issue or pull request body, or a description
file in scratch. Neither is a paraphrase. After a change to the guard's normalising, rerun `--all` so the
fingerprints match it.

**The decompiler's tests** (`python/tests/lua4/`) run on our own Lua source, compiled with a stock `luac` with and
without debug information (`fixtures/<name>.luac`, `<name>.s.luac`), against the decompiles committed beside them
(`<name>.expected.lua`, `<name>.s.expected.lua`). With a `luac` found (`LUA4_LUAC`, else `luac_program` or its
default) they also round-trip each fixture. CI has no `luac`, so it runs neither the round trip nor the check that
the committed chunks match their source: there the expected outputs only catch a change in the output, not a wrong
one, and the round trip is a local check. After a change to the decompiler or a fixture, rewrite them with
`LUA4_REGENERATE=1 uv run --project python pytest python/tests/lua4 -k regenerate`, run the tests again with the
`luac` found and read the diff. The committed chunks have 4-byte sizes, as the game's do, and the index's reader takes
only those. The `luac` that `--fetch lua` builds is a 64-bit program that writes 8-byte sizes (fine for the round
trip, which compares functions, not headers), so the regenerate step refuses it. Regenerate with a 32-bit build named
by `LUA4_LUAC`: on Windows, the same build as `--fetch lua` from the x86 tools (`vcvars32.bat`), with the core
`src/*.c` in a library and `src/luac/*.c` linked against it.

## missions {#missions}

The [Missions](../missions/index.md) pages are generated from `research/missions.yaml`
([Research workflow](research-workflow.md#missions) has its schema and who updates it).

```sh
uv run --project python coney-tools missions render [--check]
```

Checks the list (every story level present, each mission's checkpoint count equal to the Sections column of the
levels list, each status consistent with its checkpoints) and writes `docs/missions/index.md` and one page per level,
with the script-binding numbers of [Story coverage](../references/bindings/story.md) read from
`research/bindings/`. With `--check` it writes nothing and exits with 1 when a page is out of date; CI runs it that
way. Run `natives render` first when bindings changed.
