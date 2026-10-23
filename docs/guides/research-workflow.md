# Research workflow

Research turns what engineers learn about the original game into documents. A research page says what a part of the
game does, what its data looks like and how sure we are of each claim, so the next engineer does not have to work it
out again. This guide covers how we study the original, how claims are graded, where findings go and how to write
them up.

The pages themselves live in `docs/research/`, one living page per subject. Their kinds, front matter and sections
are in [Writing these docs](writing-docs.md#research-pages).

## Before you research {#upstream}

Check what already exists before starting a question, in the merged pages and in work that is not merged yet:

1. Search `docs/research/` (`git grep -i <term> -- docs/research research`) for a page that already covers it.
2. List the open pull requests (`gh pr list`) and search their branches: `git fetch`, then
   `git log --oneline origin/<branch> -- docs/research` and `git grep -i <term> origin/<branch> -- docs/research`.
   Another track may have the finding written up, or be part-way through it.
3. If something covers the question, extend that page or branch (or wait for it to merge) instead of writing a second
   account of the same thing; if it disagrees with what you find, say so on the page with both evidence levels.

Only a question that nothing covers starts new research.

## Studying the original {#studying-the-original}

Coney is a reimplementation: its code has the same observable behaviour as the original. There is one role, the
engineer, for people and agents alike. An engineer studies the original (`SLUS_212.15` in Ghidra through
ghidra-mcp, the game running in PCSX2 through the pcsx2 MCP server, the game's data files), writes up what is worth
keeping on a research page and in `research/*.yaml`, and implements it in `src/`, tools and tests.

A page is worth writing where the next engineer would otherwise have to work the same thing out again: data
layouts, constants, file formats, what a subsystem does and the questions still open. Nothing from the game goes into
the repository on the way ([LEGAL.md](repo:LEGAL.md#no-game-data)): a page holds facts and behaviour, with short
pseudocode or a short snippet where it explains a point (see [Writing up a finding](#writing-up-a-finding)).

### When a research page isn't enough

Sooner or later you need something the page doesn't say. Look at the original to answer it, and put the answer on
the page, which is how it reaches every later reader too. If it can't be answered now:

1. Add the question to the page's **Open questions** section, saying what you need and why (what decision in the
   code depends on it).
2. Raise it as a GitHub issue or, for an agent, in its report as a blocker.
3. Carry on with whatever doesn't depend on the answer, or stop and say so. A guess written into code without a note
   is worse than a gap, because nobody knows to come back to it.

## Evidence levels {#evidence-levels}

Every non-obvious claim on a research page says how we know it. A reader deciding whether to build on a claim needs
to know whether it was read in the code, seen happening, or worked out from clues. There are four levels.

| Level | Meaning | YAML value |
| --- | --- | --- |
| **confirmed (code)** | Read in the original's code, at a cited address | `confirmed-code` |
| **confirmed (runtime)** | Observed in the original game running under PCSX2; the claim cites the [run record](#run-records) of the run (`run <id>`), which holds the PCSX2 version and the method. A legacy claim states them in its own text | `confirmed-runtime` |
| **inferred** | Follows from evidence that points one way, without being read or observed directly | `inferred` |
| **speculative** | A plausible guess, written down so it can be tested | `speculative` |

### How a level is written {#level-forms}

`coney-tools docs check` accepts only these spellings, so a claim graded one way reads the same on every page:

| Level | In prose | In a table cell | Must carry |
| --- | --- | --- | --- |
| confirmed (code) | `confirmed (code)` | `confirmed (code)` | An address (or script locator) in the same block |
| confirmed (runtime) | `confirmed (runtime), run <id>` | `confirmed (runtime), run <id>` | The id of a run record, `research/runs/<id>.toml` |
| inferred | `inferred from <basis>` | `inferred` (the basis in the role text) | What it follows from |
| speculative | `speculative ([q-...](#q-...))` | `speculative` and a question link in the row | A linked open question that says how to test it |

Mixed grades are written out: `confirmed (code); role inferred`. A claim that is not worth a level because nobody needs
it is `not needed: <reason>` (the [Understood measure](#understood) reads it). Refused: `confirmed (disc)`,
`confirmed (name hash)`, `confirmed (runtime, coney)`, `Confidence:` and "probably" or "likely" in an Evidence cell.
A check against the game's files is **corroboration**, not a level: write `Disc check:` beside the claim it supports.

A run record, `research/runs/<id>.toml`, holds what a run observed in PCSX2: who ran it, the build, the state it started
from, the recipe or the method and the values read (numbers and addresses only, no dumps). It is written by the tool
that drives PCSX2, so the method cannot be paraphrased away, and the page does not repeat it. That tool is planned; while
`research/runs/` does not exist no run id can name a record, so a page that is not legacy cannot make a runtime claim
(it says `inferred` or `speculative`), and a runtime claim of the old form (PCSX2 named, with the method) stands on a
page that is listed as legacy. Its method goes to the page's *How this was checked*
section when the page is next restructured.

Where the level sits on the page is the page standard's rule, not a style: in the claim's block, in the Evidence cell of
its table row, or in the lead line under a `###` heading ([Evidence levels on a page](writing-docs.md#evidence-levels)).
The examples:

- **Confirmed (code).** [WARRIORS.DIR](../research/formats/wad-dir.md): the index's constructor at `0x00149160`
  reads the entry count, seeks to `0x10`, allocates `count * 12` bytes and reads the entry table. The claim names
  the address where anyone with the executable can check it.
- **Confirmed (runtime).** [Memory](../research/memory.md#sizes-at-runtime): the `Sector Pool` is 17,217,536 bytes,
  read in PCSX2 over PINE from the pool object the page documents (`0x00760f80`, block size at `+0x0c`) on a retail
  boot. The page is a legacy one and names the emulator version and the moments of the reads; a new claim cites the run
  that made them.
- **Inferred.** [Compiler](../research/compiler.md): the vtables use 8-byte `{delta, fn}` slots, the GCC 2.x
  layout, which points to GCC 2.95.x. The layout is confirmed; the compiler version is a conclusion drawn from it,
  and the page says the exact version is not confirmed.
- **Speculative.** [Recon](../research/overview.md#disc-layout): the custom `IOP.IRX` module is "probably" the
  audio driver. Nothing has been read or observed yet; the guess is recorded so that someone can test it.

A check against the game's files makes a claim more trustworthy but grades nothing by itself.
[WARRIORS.DIR](../research/formats/wad-dir.md) shows it: its layout is confirmed (code), and the line beside it notes
that the layout predicts `16 + 10701 × 12 = 128,428` bytes, the size of `WARRIORS.DIR` on the disc.

A claim that moves up a level (a guess confirmed in the code, say) is updated in place, and the old level goes away.
Some older pages say "Confidence: confirmed from code"; that means confirmed (code), and those pages are brought to the
new wording when they are next edited.

A page also says what it was checked against: the executable (`SLUS_212.15`, see
[Writing up a finding](#writing-up-a-finding)) and, for runtime claims, the run record that holds the PCSX2 version.

### Run records {#run-records}

A `confirmed (runtime)` claim cites the run that observed it: `confirmed (runtime), run r-20261023-grab-reach`. The run
is a file, `research/runs/<id>.toml`, written by the tool that drove PCSX2 and by nothing else, so the method cannot be
paraphrased away on the page and a reviewer can read the numbers the claim rests on. A record holds:

| Field | What it says |
| --- | --- |
| `id`, `date`, `kind`, `tool` | `r-<date>-<name>`; the day (UTC); `recipe`, `trace` or `method`; the command that wrote it |
| `who` | The agent id, type and model, and whether the hook named them or they are `unverified` ([Who](stamps.md#who)) |
| `pcsx2` | The version PCSX2 reported over PINE, and the copy |
| `start` | The state library id the run started from (`unknown` when the engineer did not say), and the patches it needed, with the state's from the library |
| `run` | The recipe or scenario and its arguments, or the method in words |
| `values`, `addresses`, `hashes` | What was read: numbers by name, the addresses of the original the run names, and the SHA-256 of a file the run made (a trace's CSV, which stays in scratch) |

Three commands write one ([`pcsx2` in coney-tools](coney-tools.md#pcsx2)):

- `coney-tools pcsx2 recipe run NAME --record --state ID ...` after a good [recipe](pcsx2-recipes.md) run: the numbers
  the recipe showed and its arguments.
- `coney-tools pcsx2 record SCENARIO --run-record --library-state ID ...` with a [trace](#recording-a-trace): the
  scenario, patches, the counts of updates and missed updates, and the CSV's hash.
- `coney-tools pcsx2 record-run --method "..." --value NAME=NUMBER ...` for an observation made by hand through the
  `pcsx2` MCP server (a watch, a read, a frame count): the method in words, and the numbers and addresses read.

`recipe run --record` and `record-run` refuse a copy you do not hold a claim on or whose PCSX2 is not running;
`pcsx2 record` takes its claim and starts PCSX2 itself. Each fills in the date, who, version and copy itself, checks
every rule of the record it will write before the run (only what the run measures is filled in afterwards), and writes
a record only for a run that went well. A record holds names, numbers and addresses as printable ASCII, within the
limits of `runs` in [.github/conventions.yml](repo:.github/conventions.yml), and a key the schema does not know is
refused. Those limits cap how much a record holds; they do not keep game data out, since the game's text is English
and numbers can carry bytes. So the reviewer reads the method, the arguments and the value names, and refuses anything
that is not our own words or a measurement ([LEGAL.md](repo:LEGAL.md#no-game-data)). A record is never overwritten: a
second run of the same name on the same day takes another name. It carries a digest of its fields, so a hand edit of
a value or the who shows when it is read; that catches an edit, not someone who writes a whole record with its digest,
which is why the reviewer reads the values against the claim. A run is not a stamp: the record in the pull request is
the proof, with no `observed` step in [the stamp log](stamps.md) (maintainer decision, 2026-10-23).

Commit the record with the page that cites it. `coney-tools research claims` ([research](coney-tools.md#research))
lists every graded block a branch changed and fails on a runtime claim whose record is missing or breaks a rule; the
reviewer checks that the record's values support the claim's number and scenario. A runtime claim written before run
records keeps its level when a change only touches it, with a warning; a new runtime claim needs a record.

## The research database {#the-research-database}

Research pages are for reading; a research database is planned: the same knowledge as YAML files in `research/` at
the repository root, one entry per function, global and struct, that tools can check against the code. It does not
exist yet. Until it does, findings live on the research pages only, and the YAML is seeded from them when it is
created. One file is already there, because it is Coney's own: the script bindings.

### The script bindings {#bindings}

The files `research/bindings/<category>.yaml` list every function the game registers with its Lua state, one entry
per name: the masterlist of the game's script bindings (what FiveM calls natives), which is also the reference for
script mods. There is one file per category, named by the category's id (the ids are in `coney_tools/natives.py`), so
an entry has no `category` key; moving a binding to another category means moving its entry to that file.
`coney-tools natives render` checks the files and generates the reference pages in `docs/references/bindings/` from
them; the pages are never edited by hand, and CI runs `natives render --check` ([coney-tools](coney-tools.md#natives)).
An entry of `level.yaml`:

```yaml
- name: GetLevelId                 # the Lua global's name
  origin: game                     # game (default) | coney: a binding Coney adds for mods, with no wrapper or usage
  registered_by: RegisterBindings  # RegisterBindings (default) | ScriptSystem (the four Cfg* of the constructor)
  wrapper: 0x0036c568              # the tolua wrapper
  calls:                           # the functions of the game the wrapper calls, with our name when we have one
    - {addr: 0x0041d6f0}           # add `name: ...` once the function has one of ours
  args:                            # in the order the wrapper reads them (Lua argument 1, 2, ...)
    - {name: index, lua: number, ctype: unsigned, desc: "Index of a level record (0 is the front end)."}
  results:                         # what the wrapper pushes
    - {lua: number, desc: "The record's level number, as in levelNN.lua."}
  overloads:                       # only for the ten names registered twice: the first registration, same keys
    - {wrapper: 0x..., calls: [...], args: [...], results: [...], note: "when it is used"}
  description: "What the binding does, in one to four sentences."
  evidence: confirmed-code         # the level of the description and meanings; the signature is always confirmed (code)
  depth: thorough                  # thorough (the callee was traced) | brief (a shorter description)
  notes: "Caveats: ignored arguments, edge cases."
  usage: {chunks: 2, calls: 8, boot: true, mission1: false, result_used: true}
  coney: not implemented           # not implemented (default) | partial | implemented
```

- `lua` is how the wrapper reads an argument: `number`, `boolean`, `string`, `table` (with `elem: number` or `string`
  and, when fixed, `count`) or `userdata`; a number's `ctype` is what it becomes (`int`, `unsigned`, `float` or
  `double`). `default` is the value an argument left off takes, and `written_back: true` marks a table the binding
  writes into (every table argument of the game's bindings is). A result's `lua` is `number`, `boolean` (1 or nil),
  `string` or `usertype` (with `type`).
- `usage` is counted from the disc's compiled Lua chunks: how many chunks reference the binding's global name and
  how many references there are (`calls`), whether the boot-to-menu scripts or the first mission's scripts
  reference it (`mission1` also counts the `global.lua` helpers those scripts reach; `coney-tools natives mission1`
  sets it from the disc), and whether a traced call keeps its result (a lower bound). It holds counts only. The
  optional `levels: [80, 87, ...]` lists the story's later levels whose scripts can call it, in story order
  (`coney-tools natives missions` sets it from the disc).
- `coney` is never edited by hand: `coney-tools natives coney` sets it from Coney's binding table
  (`src/scripting/script_bindings.cpp`), writing the key only when it is not the default
  ([coney-tools](coney-tools.md#natives)).
- `depth: mechanical` (signature only, no description) is accepted so a new entry can land before it is described;
  the masterlist has none.

## Reference lists {#reference-lists}

The [Game references](../references/index.md) are lists of the things scripts, mods and Coney's code name:
characters, power and Warrior classes, attack kinds and tables, hat fittings, gangs, the Rumble roster, AI goal types,
objects, inventory items, cars, particle effects, glass types, doors, object tints, lights, levels and their player
starts, world flags, object zones, volume boxes, scenes and movies, camera types and switches, screen effects, clips,
anim ids, controls, commands, spawner states, crime types, unlockables, statistics, colours, radar icons, text tags,
text labels, script constants, sounds, speech, script events and WAD entry names. Each is a YAML file in
`research/references/<list>.yaml`, and its page in `docs/references/` is generated from it. They hold only names, ids,
numbers and our own short descriptions (`LEGAL.md`, "Reference lists"): never game text, script source or a file from
the disc.

A list file has a fixed shape; the fields of its entries are defined per list in
`python/src/coney_tools/refs_topics.py`, and each page ends with its list's fields:

```yaml
title: Characters (humans)
about: |                                  # Markdown, our own words: what the list is
  ...
complete: |                               # Markdown: what is complete and what is not
  ...
defaults: {source: "config_preload2.lua, CfgChar", evidence: inferred}
entries:
  - id: 32
    model: warr_re_cv
    label: Rembrandt (`_cv` model)        # hand-written
```

- In `complete:` write the number of entries as `{count}` (`{count} of 10,701 names are known`), never as a
  number: the page fills in the real count, so it cannot go stale when entries are added, and `refs render`
  refuses a text that spells the count out. Counts of a part of the list (the 9 modes among a roster's 450
  entries) stay hand-written, so check them whenever you add entries.
- Every entry has a **source** and an **evidence** level (the [levels](#evidence-levels) above, spelled
  `confirmed-code`, `confirmed-runtime`, `inferred`, `speculative`), its own or the list's `defaults`.
- Each entry has a stable **anchor**, `<list page>#<prefix>-<key>` (`characters.md#char-32`,
  `levels.md#level-29`), for links from research pages, issues and mods. A key never changes once published.
- Fields are either **read from the disc** or **hand-written** (each page's field table says which).
  `coney-tools refs extract` rewrites the first kind and keeps the second, and keeps entries it did not produce,
  so a hand-added entry or a name found another way survives a refresh.

Commands ([coney-tools](coney-tools.md#refs)):

```sh
uv run --project python coney-tools refs extract [DISC] [--only LIST ...] [--names FILE]   # refresh from your disc
uv run --project python coney-tools refs render [--check]                                 # write or check the pages
```

A list too long for one file is **split** (`split=True` on its topic; [World flags](../references/flags.md) is one):
`<list>.yaml` keeps the title, prose and defaults, `research/references/<list>/<group>.yaml` holds the entries of one
`group_by` group each, and the page `<list>.md` becomes an overview linking one page per group,
`docs/references/<list>/<group>.md`, where the entries keep their anchors. Every file stays under the repository's
512 KB file guard, and each page loads fast.

To change a list by hand, edit its YAML and run `refs render`; CI fails when a page is out of date with its YAML.

The families of ids and names that scripts use but no list covers yet are in
`research/references/still-to-list.yaml`, most useful first, each with `family`, `what`, `source`, `count`, `image`
and `needs`; `refs render` shows them on the index as [Still to list](../references/index.md#still-to-list). A family
that gets its own list leaves that file. [Entities](../references/entities.md), the map of what scripts can make and
refer to, is written by hand; the index links it.

To add a field or a list, add it in `refs_topics.py` (and its reader in `refs_extract.py` when it comes from the
disc), then run `refs extract`. **Thumbnails** go in `docs/references/images/` (`characters/<model>.png`,
`objects/<name>.png`, `cars/<type>.png`; 256 × 256, transparent, rendered by `coney --render-references` from the
player's disc, then shrunk with `coney-tools refs compress-images`); `refs extract` links each one that exists. The
character images are rendered with `--names` given every model name in `character-models.yaml`, so only the models
with no recovered name are named by hash. 2D icons (`radar/icon-<n>.png`, `particles/<name>.png`) are one sheet
rectangle each, at most 64 × 64 (`LEGAL.md`); no renderer makes them yet.

## The mission list {#missions}

[Missions](../missions/index.md) is the status checklist of the story: one page per level (the 18 story missions,
the hub, the flashbacks and the Armies of the Night stages), generated by `coney-tools missions render` from
`research/missions.yaml`. Each entry has the level, its `group` and `slot`, a `label` ("Mission 2"), a summary in our
own words (never game text) with its `summary_source` (research, disc, web or mixed; web summaries list their URLs
under `sources` and are replaced by our own walkthrough), a `status`, its `checkpoints` (`count`, which must equal the
levels list's Sections column, a `default` status and per-checkpoint `each` overrides with a note), `research` links,
`issues`, `questions` and the disc `test`. A mission's title is not kept in `missions.yaml`: it is the level's `title`
in the levels list (`GSTRING.MISSIONNAME` of the English string file, filled by `coney-tools refs extract`), shown in
the page heading and the overview. The five statuses are Not Started, In Progress, Pending Gameplay Approval, Needs
Fixes and Approved, for missions and checkpoints alike.

**Whose job:** the engineer (human or agent) updates `missions.yaml` in the same commit as mission work: In Progress
when a checkpoint is started, each checkpoint to Pending Gameplay Approval when it plays to its end, the mission to
Pending when every checkpoint does, and re-runs `missions render`. Only a maintainer moves a mission to Needs Fixes or
Approved after play-testing it against the original, listing what was off under `issues`; the commit that merges the
fix moves it back to Pending. A research finding that changes what a mission needs goes on the research page, which
the entry links.

## Tools

- **Ghidra with ghidra-mcp.** Static analysis of `SLUS_212.15`, with the Emotion Engine processor extension; the
  ghidra-mcp server lets agents drive it. Setup: [Ghidra + ghidra-mcp](ghidra.md). The Ghidra project holds
  the game's code, so it stays on your machine and is never committed.
- **PCSX2.** For runtime evidence: memory reads and writes on the game as it runs. `coney-tools pcsx2` makes patched
  state copies, starts PCSX2 on one and records a scripted run as a per-update trace
  ([Recording a trace](#recording-a-trace)); `coney-tools trace diff` compares it with Coney's
  ([Comparing with Coney](#comparing-with-coney)). How PCSX2 is driven underneath: [Driving PCSX2](#driving-pcsx2).
- **The Xbox executable (optional).** For string- and float-heavy code (front end, audio cues, scripting glue,
  camera maths), the same function in the Xbox build's `default.xbe` can read faster: string literals inline, the
  object argument visible, library calls named. Find the twin through a string both builds share, load the XBE in
  its own Ghidra project (never the main one), and use it only to read the logic: offsets, sizes and vtable slots
  differ, and evidence is cited at `SLUS_212.15` addresses only. Measurements and the loader used:
  [Xbox executable](../research/xbox-executable.md).
- **Save states in Ghidra (evaluated 2026-10-05, not used).** The EE extension's `PCSX2SaveStateImporter.java`
  script pours a state's RAM into a program's writable blocks and the heap above `.bss` (a `.other` block); run on a
  scratch copy of our project with `pcsx2 repack-state` first, it took 10 s, and a script labelling level99's 19 live
  brains, their goals and the 7 gangs ran in 2 s. The same walk over PINE gave the same facts in 0.03 s after a
  19 s PCSX2 launch, with names from the static project either way, and reading `eeMemory.bin` straight from the state
  file gives the same frozen moment without either. Ghidra adds nothing PINE lacks but a frozen, searchable heap; it
  costs a 170 MB project copy per state, its labels never reach the main project, and code patches and the hook cave
  below `0x00100000` do not show, so we read live data over PINE or from the state file.
- **Capture analysis.** Recording what the game sends to the graphics hardware or the sound processor and studying
  the capture. It is a later fallback, for questions that the code and the debugger answer badly (exact rendering
  state, timing). Captures contain game data: they stay in your scratch folder and never enter the repository.

### Running Coney {#running-coney}

A window that takes the keyboard focus interrupts whoever is typing, so agents and tools start Coney only in ways that
leave the focus alone:

- **Test mode, always.** Run `coney` with `--frames N` (and `--input-script`, `--screenshot` as needed) or
  `--headless`, never as a plain interactive run. In test mode Coney's window opens without taking the focus (it
  may show on top, but keystrokes stay where they were), and `--render-references` (which hides its window) never
  takes it either. Any other windowed run started by a tool adds `--no-activate`
  ([Frame rate](running.md#frame-rate)).
- **On the agents' display.** Every window an agent opens goes on the display set aside for agents, never on the one
  the user works on. The agent tool's environment sets `CONEY_WINDOW_DISPLAY` (the [setup](agents.md#setup)), and
  Coney opens its window centred on that display, never taking the keyboard focus
  ([Which display](running.md#window-display)); the PCSX2 launcher
  reads the same variable (below). An agent never overrides it with `--window-display` for its own runs.
- **ctest needs nothing.** The disc tests inside `coney_tests` run on librw's NULL renderer and open no window; the
  `coney` smoke tests (`repo:src/platform/CMakeLists.txt`) run `--headless` or stop at the command line before a
  window opens; the two tests with a window (`coney.frame_copies_upright` and `a renderer started again after one
  stopped meets none of its state`) hide it and so never take the focus. A new smoke test keeps to
  `--headless`. `coney-tools trace coney` runs Coney `--headless` too.
- **How.** librw's GL3 device makes and shows the window with SDL's defaults, which activate it. Before it does,
  `RenderEngine::start` (`repo:src/platform/render_engine.cpp`) turns off SDL's `SDL_WINDOW_ACTIVATE_WHEN_SHOWN` and
  `SDL_WINDOW_ACTIVATE_WHEN_RAISED` hints, so on Windows the window is shown with `SWP_NOACTIVATE`. Unlike PCSX2's Qt
  ([below](#driving-pcsx2)), SDL honours that, so no launcher is needed and the run's output stays on the console.

### Driving PCSX2 {#driving-pcsx2}

How the runtime passes are done. The first ones (from 2026-10-04) ran the official portable PCSX2 2.9.94 over PINE
alone; since 2026-10-20 every copy runs the maintainer's PCSX2-MCP fork, a build of PCSX2 2.9.129 with a debug
server built in, beside PINE. Both are reached through `coney-tools pcsx2` and the `pcsx2` MCP server.

**Recipes first.** The basic things (find the player or a human, teleport, change health or rage, spawn a human, give
money, send an event, run a Lua chunk or call one of the game's functions, load a checkpoint, pause and step) are
[PCSX2 recipes](pcsx2-recipes.md), each confirmed in PCSX2 and run with `coney-tools pcsx2 recipe run`. Use them
rather than working an address out again, and add a recipe when you find a new technique: an entry in
`research/pcsx2-recipes.toml`, run in PCSX2 and its `confirmed_runtime` filled in, then `coney-tools pcsx2 recipe
render`.

**States first.** Do not drive the original to a place someone has already been: the [state
library](pcsx2-states.md) keeps shared save states (a pedestrian to grab, a fence to jump, a store door, a window to
break), each with its level, checkpoint, the player's position and what it is set up for. **Check it first** with
`coney-tools pcsx2 states list`, start a claimed copy on one with `states use ID --copy COPY --agent ID --launch` (it
hands out a copy, so the library file is never changed), and **add any state you make that others could use** with
`states add`, saying in `--made-by` how you got there. It is also the fast way back to a level or checkpoint, in place
of the `load-checkpoint` recipe's slow reload.

- **Set-up.** `coney-tools pcsx2 setup-copies` puts the fork's build (`pcsx2_mcp_dir`) into every copy and sets its
  ini: `[EmuCore]` `EnablePINE = true` with `PINESlot` 28011 + *i*, `EnableDebugServer = true` with
  `DebugServerPort` 21512 + *i* for copy *i* (`pcsx2` is 0, `pcsx2-b` 1, ...), and `[InputSources]` `SDL = false`
  ([PCSX2 copies](workspace.md#pcsx2-copies)). PCSX2 2.9.94 failed to open an ISO whose path holds commas and
  parentheses ("Requested filename ... does not exist"), so the launcher boots an NTFS hard link with a plain name in
  the scratch folder (it copies nothing).
- **PCSX2 is started only by `coney-tools pcsx2 launch` or `pcsx2 record`, never by hand** (no `pcsx2-qt.exe` from
  a shell, a script or `Start-Process`). The launcher is what keeps PCSX2 from taking the keyboard focus, which
  interrupts whoever is typing. `pcsx2 launch --agent <id> [<state.p2s>]` starts it on a state file, or with no
  state boots the disc and returns once PINE answers; it runs under your claim and leaves PCSX2 running.
- **How the launcher keeps the focus off** (`repo:python/src/coney_tools/pcsx2_proc.py`, whose docstring says why it
  must stay this way). A no-activate show command alone (`SW_SHOWNOACTIVATE` in `STARTUPINFO`) was not enough: Qt
  ignores it for its first window, and Windows lets a process activate its window when the foreground process
  started it, which every agent's process tree (under the user's terminal) is. So the launcher has WMI start PCSX2
  (`Win32_Process.Create`): its parent is WMI's provider host, outside that tree, with no foreground rights to hand
  down. As a backstop, while `pcsx2 record` runs a guard polls every 50 ms and, if a PCSX2 window is in the
  foreground, hands it back to the window that had it (no input sent, PCSX2 never minimised, so screenshots keep
  working). Checked 2026-10-07: the foreground owner logged every 50 ms over a state launch and over a cold boot
  (about 900 polls each) was never PCSX2.
- **Where its windows go.** With `CONEY_WINDOW_DISPLAY` set, the launcher also moves each PCSX2 window (the main
  window, and the game's when it has its own) onto that display as soon as it shows, centred and shrunk to the
  display's usable area if larger, without activating it (`SetWindowPos` with `SWP_NOACTIVATE`); a window is moved
  once, so one dragged elsewhere stays there. The value means what it means for Coney
  ([Which display](running.md#window-display)): an index counts the primary display as 1 and the rest in Windows'
  order, as SDL does, and a name matches the display's device name (`DISPLAY2`) or its monitor's description as
  Windows lists it. A value that matches nothing prints one warning listing the displays, and PCSX2 opens where it
  last was (its own `PCSX2.ini` keeps that place, `[UI]` `MainWindowGeometry`; the launcher does not edit it).
  `repo:python/src/coney_tools/pcsx2_proc.py` (`WindowPlacer`).
- **Several at once.**{#several-at-once} A portable PCSX2 keeps its settings, states and memory cards in its own
  folder, so each copy of the folder (`pcsx2`, `pcsx2-b`, ..., each with its own `PINESlot` 28011, 28012, ... and
  `DebugServerPort` 21512, 21513, ...) is an independent instance, and `coney-tools pcsx2` reads both ports from
  that copy's `PCSX2.ini`; two copies on one port are refused. The fork binds its debug server port exclusively, so a
  second PCSX2 on a port in use logs `[DebugServer] Failed to bind on port N` rather than sharing it (stock
  PCSX2-MCP let every instance listen on 21512 and connections landed in any of them). Every PCSX2 use starts with
  `coney-tools pcsx2 claim --agent <your id>` (it prints the copy's folder and both ports; `--json` for scripts) and
  ends with `pcsx2 release --agent <your id>`, which also closes your PCSX2. `pcsx2 status` shows who holds what, built
  from the live claims and processes; a claim whose process is gone is stale and the next `claim` takes it over.
  `pcsx2 launch` and `pcsx2 record` take `--agent` and run under your claim (making one if you hold none), and wait for
  both ports. Input to the game is over PINE (scripted pad input, the stick table below) or through the debug server
  (the pad, frame by frame, and input recordings: below); no tool takes the keyboard focus. When a hotkey is
  unavoidable (Space to pause), `pcsx2 keys --agent <id> --copy <name> space` posts the key to that copy's window by
  handle, and `pcsx2 screenshot --copy <name> --out <png>` reads the window by handle (not F8); both are Windows-only
  and leave the focus alone. PCSX2 never takes the focus (the launcher, above), and every copy has `[InputSources] SDL =
  false` so the user's gamepad cannot drive it; a maintainer turns SDL on in a copy only to test by hand, and `claim`
  and `status` warn when it is on. Claims live in `pcsx2-claims/` of the main checkout's shared scratch folder
  (`../../scratch`), never a worktree's own `scratch_dir`, so every worktree sees them (`pcsx2_root` and
  `pcsx2_claims_dir` in `coney.local.toml` move the copies and the claims). **Act only on your own copy:** the ports
  of every copy are readable, but a client may use only the ports of the claim it holds ([The PCSX2 MCP
  guard](agents.md#pcsx2-guard) says what is enforced and what to check yourself).
- **What PINE gives.** Memory reads and writes, game info, the emulator's status and save/load state slots; no
  breakpoints, registers, pause or frame capture. PCSX2 serves **one PINE client at a time**: a second client (`pcsx2
  record`, a script of your own) blocks while another is connected, so the MCP server connects in `mode: "debug"`,
  which leaves PINE free.
- **What the debug server gives** (the fork's, newline-delimited JSON over TCP on the copy's `DebugServerPort`; it
  starts with a game and serves several clients at once). Everything the `pcsx2` MCP server's tools do: memory reads,
  writes and pattern searches, all registers at 128 bits, PCSX2's own disassembly and expressions, breakpoints with
  conditions, watchpoints (read, write, change), stepping and the debugger's pause, plus `pcsx2_emulation` (pause or
  resume emulation as a whole), `pcsx2_frame_advance` (run N frames, then pause), `pcsx2_controller` (a DualShock 2's
  whole state, held until the next call), `pcsx2_screenshot` (a PNG at internal resolution in the copy's `snaps/`),
  `pcsx2_capture` (PCSX2's video capture), `pcsx2_gs_dump` (a GS dump of N frames), save and load state, and
  `pcsx2_input_recording` (below). The MCP server connects to nothing at start: `pcsx2_connect` with your claim's
  `debug_port` and `pine_port` picks the copy, and **every `pcsx2_*` call carries `agent: "<your id>"`** (the id you
  claimed with; it names your connection, [Agents](agents.md#setup)). `coney-tools pcsx2 record --recording`
  speaks the same protocol itself (`repo:python/src/coney_tools/debug_server.py`).
- <span id="input-recordings"></span>**Input recordings.** PCSX2's own `.p2m2` files hold every frame's pad state from
  a start savestate (`<file>.p2m2_SaveState.p2s` beside it) or from power-on, so a run's input repeats exactly.
  `pcsx2_input_recording` drives them as PCSX2's "Input Recording" menu does: `new` (with `author`, `from`
  `savestate` or `power_on`, `path`) records from now on, `play` loads the start and replays, `stop` closes the file,
  `status` reports the mode and frames. At a replay's last frame PCSX2 pauses and the fork stops the replay there, so
  playing never adds to the file. A `play` sent while emulation is paused (`pcsx2_emulation`) loads the start at once
  and stays paused until resumed, which is how `pcsx2 record --recording` makes its step 0 the recording's first frame
  (checked 2026-10-20 on copy `pcsx2`: a 181-frame recording made in the front end, played while paused, put PCSX2's
  frame counter back to the recording's start at recording frame 0, ran on resume, and ended paused with
  `replay_ended`). Paths are relative to the copy's folder and must stay inside it, so recordings live
  in the copy's `recordings/`; `coney-tools pcsx2 recordings make` writes one from a Coney input script (the same file
  that drives Coney, so both games get the same pad; checked 2026-10-21 on PCSX2 2.9.129: a recording of `stick left 0
  60` for 2 s made the player walk at 1.629 m/s, gait 2, as the `pad` recipe's 60 % does, and stop when it ended), and
  `recordings push | pull` moves them (with the savestate,
  SHA-256 checked) to and from the shared scratch folder's `recordings/`, where they are kept like any capture. `pcsx2
  record --recording <p2m2>` replays one while it traces memory, so a runtime trace can be made again on the same input
  ([`pcsx2`](coney-tools.md#pcsx2)). A recording is game data: never committed.
- **States across versions.** The copies keep the states their stock PCSX2 2.9.94 saved in `sstates/`, but PCSX2
  2.9.129 may refuse them (a state is tied to the version that saved it). The maintainer's slots 1 to 10 are in the
  [state library](pcsx2-states.md), saved by 2.9.94, and the 2.9.129 fork loaded the two tried (2026-10-21); a state
  that is refused is made again on the fork build from its catalog entry's steps and added under a new id.
- **Input and hotkeys.** Keyboard messages posted to the PCSX2 window by handle (`pcsx2 keys`, `WM_KEYDOWN`) reach
  the hotkeys in `[Hotkeys]` (verified: a posted Space paused and resumed the game with the window unfocused) and the
  pad bindings in `[Pad1]` (Return = Start, K = Cross, arrows = D-pad, WASD = left stick; unchecked with posted
  keys). The hotkeys: **Space pauses**, F8 saves a screenshot to `snaps/` (aspect-corrected, 1240 × 930 for the
  game's 640 × 448), F4 toggles the frame limiter. Hold a pad key for about 300 ms so that a 30 Hz game sees it.
  Leave the frame limiter on: with it off, real-time waits (the legal screen's 5 s) pass in a blink.
- **A chosen analog stick value.** Keyboard keys bound to a stick only give full deflection, but the gait thresholds
  need partial values: the stick table at `0x0050b8d0` sets what full deflection reads as. How, with its addresses:
  the [`stick-magnitude`](pcsx2-recipes.md#stick-magnitude) recipe. State the magnitude used with every runtime claim
  that depends on input ("stick magnitude 0.5, straight up").
- **Catching a moment.** Load a state, press Space to pause, make the memory writes, then send Space followed by a
  run of F8 presses (one a second, or faster) and look at the screenshots afterwards. Data drawn in one frame
  usually survives in memory until reused: a sprite batch's arrays keep the last frame's sprites after its count is
  reset, which is how the draw-order keys and colours were read.
- **Staging a fight.** To study the player being hit, write a pedestrian's position and heading over PINE so it
  stands in front of the player, then force its commands and animations so it attacks or grabs: a controlled run that
  repeats exactly. The front end's Rumble mode (quick rumble) is the fallback for live AI fights. Either way, save the
  starting point as a state file of your own.
- **Patching code to see something.** A code write lands before the recompiler first compiles the block if the
  function has not run yet; the legal screen's placement was measured by patching its clear colour to blue and
  halving its size factors, and the start-up movies were skipped by making their skip check (`0x0042a820`) return 1.
  Say on the page which patch a claim depends on. A write to code that has already been compiled is not picked up
  (PCSX2 logs "Impossible block clearing failure", and writing during a state load crashed it).
- **Scripted pad input** (2026-10-05, the traversal pass). For a **closed-loop driver**, one that decides each input
  from what the game shows, the `scripted-pad` patch (`research/traces/patches/state-patches.toml`) makes the pad code
  read player 1's buttons and left stick from spare bytes that a PINE write sets; the patch must be in the state copy
  before the code is compiled (`coney-tools pcsx2 prepare-state`, then `pcsx2 launch`). The addresses, the byte order
  and how to hold a pad state: the [`pad`](pcsx2-recipes.md#pad) recipe. `coney-tools pcsx2 prepare-state` and
  `pcsx2 record` make the copy and restart PCSX2 for you. For input that does not depend on the game's state, use an
  [input recording](#input-recordings) instead: it is the default for runtime evidence (a maintainer decision,
  2026-10-21), because PCSX2 replays it frame by frame, the same every time.
- **The right stick, too** (2026-10-05, the feel pass). The `right-stick` patch puts the right stick's x and y in the
  same spare bytes (the [`pad`](pcsx2-recipes.md#pad) recipe). The stick table at `0x0050b8d0` is the same map as
  Coney's `pad::stickValue` (dead band 95-160, `(raw − 160) / 95`), so writing the raw bytes Coney makes from an input
  script's percentages (`160 + round(0.95 × v)`, `95 − round(0.95 × |v|)`, 128 at rest) gives both games the same
  stick, diagonals included, without touching the table.
- **A puppet human** (2026-10-05, the combat runtime pass). To make an AI human attack, grab or block the player,
  write command ids ([Combat](../research/combat.md#commands)) into its per-player record with the `puppet` patch,
  which stops the game clearing them: the [`puppet-command`](pcsx2-recipes.md#puppet-command) recipe, with what a grab
  and a held button need. Pick the puppet by name: the nearest human changes as pedestrians walk, and an ally's hits
  play reactions without damage.
- **Following a script at runtime** (2026-10-07, the level99 hints pass). To log which Lua functions run and which
  bindings a script calls, with their arguments, hook the Lua 4 VM. Its interpreter entry is `0x00334478`
  (`luaV_execute`, called by `luaD_call` `0x00328b40`); its `a1` is the closure, whose prototype (`+0x00`) holds
  `lineDefined` at `+0x38` and the chunk name at `+0x3c`. The C-function call is `0x00328a30` (`callCclosure`): `a1`
  is the closure, `+0x00` the binding's address (as in a bindings dump), and `a2` its first argument. Arguments are
  16-byte TObjects: type at `+0`, value at `+8` (1 nil, 2 number as a double, 3 string with its text at `+0x10`).
  Only the first argument is reliable: the slots after the last real argument hold old values. The hooks are
  `lua-exec`, `c-call` and `c-call-args`. A script's tables are read the same way, from the globals table
  (`ScriptSystem *0x00512b04` → state `+4` → globals `+0x44`). That gives `Objects.<name>`, a spawn handle (index
  `<< 16`), which leads to the live object through the spawn record (`+0x24` low 16 bits) and the handle table
  `0x006ebd38` ([Objects](../research/objects.md#spawn-records)). A driver that plays a level's steps by what they
  show ([Scripts](../research/scripting.md#level99-hints)) must send partial sticks through Coney's byte map (below):
  a plain linear map falls inside the dead band and the player stands still.
- **One sample per update.** Batch every read of a sample into one PINE message and keep a sample only when a
  character update has run since the last one: the game counts its 60 Hz ticks at `0x005104f4` and steps the
  characters on each tick that makes the count even ([Tasks](../research/tasks.md#tick)), so the count halved numbers
  the updates. Apply the scripted input as each update is seen. `coney-tools pcsx2 record` does exactly this
  ([Recording a trace](#recording-a-trace)). (The first recorder counted updates by the game time in milliseconds,
  `*(0x0050b734) + 0x48`; its rounding skipped and repeated steps after the frame that catches a tick up, about every
  33 s.)
- **Hooks: a call log without breakpoints** (2026-10-05, the runtime-checks pass). PINE has no breakpoints (the debug
  server's do stop the game, which a recording cannot afford), so "who
  writes this" and "which objects does this run" are answered by patching code in the state copy: a hook replaces two
  instructions with a jump to a cave in memory the game leaves empty (`0x000a0000`; zeros from `0x00095100` to the
  ELF at `0x00100000` in every state checked), which writes chosen registers and loads into a ring buffer
  (`0x000b0000`) and runs the two instructions. The recorder reads the ring every poll and writes one CSV per hook
  ([Hooks](#hooks)). A **call hook** makes the game call one of its own functions on its own thread, as a script
  binding would: that is how a fight is started on a pedestrian (`GoalFight`'s `0x002b2b90`) for the AI checks.
- **Absolute paths from a worktree.** Give `pcsx2 launch` and `pcsx2 record` absolute `--iso`, `--scratch` and
  `--state` paths when you run them from a worktree: PCSX2 opens the disc link relative to its own folder, so a
  relative path fails ("Requested filename ... does not exist" in its `logs/emulog.txt`) and the tool only reports
  that the state did not run within 60 s. A state that already holds a patch (`scripted-pad` in most saved states)
  is refused by that patch: leave it out of the scenario's `patches`.
- **Leave the quick-save slots alone.** Load existing slots read-only (they may hold someone else's test spots; the
  [state library](pcsx2-states.md) holds copies of the maintainer's, so use those), and keep your own states as files
  in your scratch folder, made as above, and add the useful ones to the library. PINE can only save to a slot number:
  when a state must be saved from a running game, save it to an empty slot above 10 and move the file to your scratch
  folder at once. A state saved from a patched copy still holds the patches; put each patched word back to the
  original that `research/traces/patches/` lists before using it as a scenario's `state`, or the copy is refused.
- **Hygiene.** Save states, screenshots and logs contain game data: keep them in your scratch folder (move the
  `.p2s` files out of `pcsx2/sstates/` afterwards) and close PCSX2 when done. Screenshots are measured, never
  committed; a claim quotes the numbers.

### Recording a trace {#recording-a-trace}

A parity question ("does Coney's walk start take as long as the original's?") is answered by playing **one input
script** on both games and comparing their per-update traces. The original's side is a **scenario**: a TOML file in
`research/traces/scenarios/` that names the save state, the patches, the input script beside it, how many updates to
record, and the fields to read. Scenarios, patches, field sets and input scripts are committed; the traces they record
are measurements of the game and stay in your scratch folder.

```sh
uv run --project python coney-tools pcsx2 record research/traces/scenarios/walk60.toml --out ../../scratch/walk60-original.csv
```

`record` copies the scenario's quick-save slot (read only), or the state file it names under `scratch_dir`, to
`<scratch_dir>/pcsx2/<scenario>.p2s` with the patches applied, starts PCSX2 on the copy with `-fastboot -statefile`
(through a hard link to the disc when its path has commas or parentheses), waits until the patched state runs, plays the
script and samples every update, then closes PCSX2 and warns if a file appeared in its `sstates/` folder. The folders
come from `coney.local.toml` (`pcsx2_dir`, `game_dir`, `scratch_dir`) or from `--pcsx2-dir`, `--iso` and `--scratch`;
PINE must be on in PCSX2's ini. `--state` records from another state file (or `slot:N`), `--attach` records a PCSX2 you
started yourself on a patched copy (`coney-tools pcsx2 prepare-state`, then `pcsx2 launch`), and `--keep-open` leaves it
running. It prints the number of updates, how many reads each poll made, the time per poll and the updates it missed.

**Timing.** Step 0 is the first sample, before any input. After the sample of step N the recorder writes the pad for
frame N of the script, which the update of step N + 1 reads: the numbering of Coney's `--trace`, where frame N of an
input script is step N + 1. A missed update (one the poll did not see) is listed by step; its input lands an update
late. The stick bytes are made from the script's percentages exactly as Coney makes them, so both games see the same
stick; a tap holds the button for one update. With PCSX2 2.9.94 on Windows a poll of 26 reads took about 0.05 ms, so
the tick count is seen many times per update; no update was missed in the smoke runs (130 to 230 updates). A step
counts from the tick that follows the update's own (the count turning odd), so a sample never sees an update half
done; the frame that catches up a tick (one in about 1000, [Tasks](../research/tasks.md#tick)) runs two updates and
shows as one missed step.

**Where a run's time goes** (`walk60`, 2026-10-05, two runs alike): the patched copy 0.15 s, PCSX2 from start to PINE
answering 12.1 s, the state running 0.5 s later, the 170 updates 5.7 s (real time), closing 0.2 s: 19.1 s, two thirds
of it PCSX2 starting. Started without a state it took as long (11.6-16.4 s to PINE over three launches), so the
state load itself is under a second. Loading each next state into a PCSX2 left running would save about 12 s a run;
PINE loads only numbered slots, so that needs a slot above 10 that the tool fills and empties, which is worth adding
once runs come in batches.

**The scenario file:**

```toml
description = "level99 street: the walk start and the walk, stick 60 % up for 100 updates, then the stop"
research = "docs/research/feel.md"
input = "walk60.txt"              # a Coney input script, beside the scenario
updates = 170                     # updates to record; Coney runs as many frames
fields = ["player", "camera"]     # field sets of research/traces/fields.toml

[original]
slot = 1                          # the quick-save slot to copy, never written
# state = "states/x.p2s"          # or a state file of your own, relative to scratch_dir (never committed)
patches = ["scripted-pad", "right-stick"]
let = { puppet = 'human("PoizoCiv")' }       # names resolved once, before the first update
setup = [ { address = "prec(puppet) + 0x1e", type = "u8", value = "0", frame = 0, until = 0 } ]

[coney]
level = "level99"                 # --play-level
args = []                         # more of Coney's options, such as ["--spawn", "start"]
settle = 0                        # updates Coney plays first with no input, dropped from its trace

[diff]                            # how `coney-tools trace diff --scenario` compares the two traces
start_frame = true
columns = ["x", "y", "heading", "speed", "gait", "clip"]
tolerance = { x = 0.1, speed = 0.05 }
```

`setup` writes are made after the samples of frames `frame` to `until`; the values are expressions worked out at that
moment (`f32(tf(player)) - 1.5 * sin(heading(player))` puts the puppet 1.5 m in front of the player).

`calls` (in `[original]`) make the game call one of its functions after the sample of frame `frame`, through the call
hook among the scenario's patches: `{ function = "0x002b2b90", args = ["u32(enemy + 0x90)", "u32(player + 0x90)"],
frame = 3 }` is `GoalFight(enemy, player)`. One call per frame; the arguments (up to six) are expressions worked out
then, and the recorder stops if the call before was not made.

**Patches** (the files of `research/traces/patches/`) are named groups of edits to the copy's `eeMemory.bin`: a code
edit gives the address, the instruction word the state must hold there and the word that replaces it, and the copy is refused
when the state holds something else. `scripted-pad`, `right-stick` and `puppet` are the patches of
[Driving PCSX2](#driving-pcsx2). The patches and the hooks ([Hooks](#hooks)) are declared in the TOML files of that
folder, read together in file-name order; a name must be unique across them. **A pass adds its own file**
(`patches/<pass>.toml`, with a comment at the top saying what the pass asked and the research page), and never appends
to an existing one: two research branches that each append to one shared file conflict on nearly every merge, two
files never do. Put a patch or hook in an existing file only to change that very entry.

**Fields** (`research/traces/fields.toml`) are named sets of columns. A field is read every update at an
**address expression** (`tf(player) + 0x4`, type `f32`), or worked out by a **formula** from the fields before it
(`wrap(deg(2 * atan2(qz, qw)))`); `hidden = true` keeps a helper out of the CSV, and `follow = true` works the address
out again at every sample, for a chain of pointers that changes as the game plays (the top of an animation stack).
A **value** field (`value = "handles(brain(h) + 0x164, 16)"`) is an expression over memory worked out at every sample,
whose result is the column. Expressions are arithmetic over these names, and nothing else of Python:

| Name | Value |
| --- | --- |
| `player` | the human whose player number `+0x1b0` is 0, among the 60 at `0x00640c80 + i × 0x6d0` in use (`+0xd4` set) |
| `human("Name")` | the human in use with that name (`+0x80`) |
| `index(h)`, `rec(h)` | the handle index (`+0x92`); the record (`*(h + 0xd4)`) |
| `prec(h)`, `tf(h)`, `brain(h)` | `0x00660f50 + index × 0x2c`; `0x00714b00 + index × 0x20`; `0x006d53f0 + index × 0x2f0` |
| `heading(h)` | the heading in radians from the transform's rotation (0 faces +y, anticlockwise) |
| `camera`, `game_time` | `*(0x005d9158)`; `*(0x0050b734) + 0x48` |
| `h` | with `rows = "humans"`, the row's human |
| `hidx(handle)` | the handle index a handle names (high half), -1 for the null handle `0xffffffff` or a stale one (`0x00390180`) |
| `goal_type(vt)`, `goal_at(brain, k)` | a goal class's type id ([goal types](../references/goal-types.md)); the type of stack slot k, -1 above the top |
| `handles(address, n)` | how many of n words are handles, neither 0 nor `0xffffffff` (a handle list's length) |
| `u8` ... `f32` (address) | a value in memory (`u8 s8 u16 s16 u32 s32 f32`) |
| `sin cos atan2 hypot sqrt abs min max deg rad wrap pi` | math; `wrap` takes degrees to (-180, 180] |

A column that Coney's `--trace` also writes has Coney's name and unit ([Tracing](running.md#tracing)): `x`, `y`,
`z`, `heading`, `speed`, `vz`, `gait`, `clip`, `stamina`, `command`, `health`, `power` and the camera's `cam_*`,
`look_*`, `wanted_*`, `cam_distance`, `cam_pitch`, `cam_yaw`, `band_near` and `target_pitch`. `phase` (record
`+0x08`) is the original's only.

**Every human.** `rows = "humans"` in a scenario writes one row per human in use per update (`step`, `human`, then the
fields, with `h` the row's human) instead of one per update: the `humans` field set, a gang battle's every fighter
([Rumble: gang battle trace](../research/rumble.md#gang-battle-trace)), is the contract for Coney's all-humans
`--trace`. One message reads which humans are in use and every fixed address; the `value` and `follow` fields are then
worked out together, one message per level of pointers (four or five messages a sample). The recorder prints how many
samples were **torn**: read while the next update had already started, so they mix two updates. A sample of 30 humans
takes about 11 ms (2026-10-12, PCSX2 2.9.94: 1.6 ms of it PINE), close to the 16 ms between the tick that ends an
update and the next update's, and at full speed 64 of 601 samples tore; `speed` in `[original]` (PCSX2's
`[Framerate]` `NominalScalar`, set in the claimed copy's ini for the run and put back afterwards) slows the emulation:
at 0.4 a 9,000-update match had none missed. The game steps by emulated ticks, but not every wait is a fixed number
of updates (the Rumble intro ended after 151 to 172, [Rumble](../research/rumble.md#gang-battle-setup)), so time a
scenario's calls with a margin. A frame that runs two updates (about one in 1,000) still tears its sample; the trace's
`.quality.txt` lists every missed and torn step.

`pcsx2 record --let NAME=EXPR` gives or replaces a name of the scenario's `let`, so one scenario is recorded with
different values: the gang battle's matches differ by an offset of the brains' random stream (`--let seed=341`).

#### Hooks {#hooks}

Hooks are declared in the same file as `[hook.NAME]` and named in a scenario's `patches` like a patch. Each gives the
address, the two instruction words it displaces (checked against the state; neither may be a branch, and the first may
be a jump with its delay slot, as in a two-instruction setter) and up to seven values to `log`: a register (`a0`, `ra`,
`sp`), a float register (`"f28"` logs an `f32`; in a table without `type`, the raw word, to compare floats exactly),
`count` (the EE's cycle counter, 4,915,200 a 60 Hz tick) or a load such as `[a0 + 0x14]`, `[[a0 + 0x0] + 0x200]` or
`[0x005104f4]`, a word unless a table gives `type`. The hook's address must not be a delay slot or a branch target, and
a load must only follow pointers the hooked code itself uses: a bad load crashes the game. `pcsx2 record` writes each
hook's calls to `<trace>.<hook>.csv` (the ring's sequence number, the step, the values). The step is the update the call
was made in, the same numbering as the trace's rows, so a call logged at step N shows its effect in the sample of step
N. It comes from the tick count read in the same message: while the count is 2Q or 2Q + 1 the game is in the update a
sample labels Q + 1 (an entry of a missed update goes to the step after it; logging `[0x005104f4]` pins the tick
exactly). Recordings made before 2026-10-07 labelled entries with the step of the poll before: one step early for a call
made while the count was even (everything in `Humans_Update`), usually right for one made while it was odd (the second
tick, after the pair). `call = true` makes the call hook of `calls` above; put it at the entry of a function that takes
no floating-point arguments (`call-brains` is at `Brains_Update`, before the brains run). The hooks of the
runtime-checks pass: `tick-game` and `humans-update` (the play step and the character step), `wheel-update` (each object
the wheel updates), `state-code` (record `+0x14`'s setter), `set-command` and `get-command` (per-player `+0x20`),
`brain-think`, `brain-event`, `attack-warning`, `try-block`, `block-start`, `skid-test` (the locomotion's run-stop test)
and `call-brains`. The hint-queue pass added `hint-queue`, `hint-clear`, `hint-withdraw` and `hint-flush` (the hint
box's queue, [HUD](../research/hud-messages.md#hint-queue-runtime)) and `anim-dispatch` and `anim-callback-match` (who starts
which anim id and which script callback it runs). The level80 passes added the teammate hooks (`hit-by-chief`,
`hit-reaction`, `attack-target-push`, `push-contact`, `can-push`, `say-command`) and the fire hooks (`ember-init`,
`burn-init`, `fade-flame-init`, `debris-flame` and their update hooks); the revive pass added `revive-start` and
`revive-finish` ([Combat](../research/combat-rage.md#revive-runtime)), the speech pass `say-anim-line`, `stop-speech`,
`play-speech` and `play-speech-cut` (who starts, stops or refuses a human's line, with his line state before each;
[Sound](../research/sound.md#speech-cut-in)), and the scene-mapping pass `scene-find`, `scene-bind-human`,
`scene-bind-object` (what each scene binds, [Scenes](../research/scenes.md#cast-runtime)) and `lua-exec-source`
(`lua-exec` with the chunk name's text), and the gang-battle pass `set-target`, `help-call` and `send-helper` (who
changes a fighter's target, help calls and helpers sent; [Gang fights](../research/gang-fights.md#runtime)). One
run installs at most 64 hooks, fewer than the file holds, so name only the ones a pass needs. Two hooks at one address
cannot be installed together (`hint-show` and `event-hint` are the same place): pick one, and add any extra value it
needs to the one you keep. When the update of an entry matters, log the tick count and use it: in that pass one Lua
call's four `HintBox_Queue` entries, all at one tick, were labelled with two consecutive steps.

A logged load can also log the **text** it points to: `text = N` (at most N bytes, up to 1024) reads the string at the
value plus `text_offset`, and `text_when = "NAME=VALUE"` reads it only when another logged value has that value (a
script value's type tag, so a number is never read as a pointer). The recorder reads the texts while the game is
paused for the ring, writes them to a `<name>_text` column, and takes a text whose second byte is zero for UTF-16.
The event hooks use this: `event-call` (the script VM's call of a C binding, `0x00328a30`, its binding and first two
arguments), `event-callback` (the engine's look-up of a script function by name before it calls it, `0x00356e08`),
`event-hint` (the hint box showing a new text, `0x001cdf7c`) and `event-sound` (a sound started, `0x00111de8`, its
hash).

### Comparing with Coney {#comparing-with-coney}

`coney-tools trace coney` plays the same scenario on Coney headless (`--play-level` from its `[coney]` table,
`--frames` its updates, its input script, `--trace`), and `coney-tools trace diff` compares the two traces. A
scenario whose level opens with a script that holds the pad (level99 checkpoint 3: Vermin's fence lesson, about 9 s)
sets `settle` in `[coney]`: Coney plays that many updates first, the input script is shifted by them, and they are
dropped from the trace (renumbered from 1), so the run starts where the original's saved slot does:

```sh
uv run --project python coney-tools trace coney research/traces/scenarios/walk60.toml --out ../../scratch/walk60-coney.csv
uv run --project python coney-tools trace diff ../../scratch/walk60-original.csv ../../scratch/walk60-coney.csv --scenario research/traces/scenarios/walk60.toml
```

The diff aligns the rows by step, from the first update of input (the script's first frame + 1; `--from` and `--to`
choose others), and prints for each column the first step outside its tolerance, the largest difference and where it
is, then a short table of both traces around each divergence. It exits with 1 when a column is outside its
tolerance, so it can gate a check, and 2 when a trace cannot be read. Without a scenario it compares every numeric
column the two share; `--columns` picks some, `--tolerance COLUMN=VALUE` (or `*=VALUE`) sets tolerances (whole-number
columns are exact by default, others 0.001), `--shift N` compares the original's step s with Coney's s + N, and
headings compare round the circle.

The two games seldom start at the same spot: Coney starts a level where its script puts the player, the original where
the state was saved. `--start-frame` (or `start_frame = true`) moves each trace into its player's frame at the first
compared step, so positions are metres from where he stood, forward along +y, and headings are turns from his
heading then. The camera's points move with him, so a camera that starts at another angle shows as a heading offset.
To start both at the same spot, give Coney the state's place with `--start X,Y,Z,HEADING,DISTANCE,YAW` in the
scenario's `args`, taken from the original trace's first row (`x`, `y`, `z`, `heading`, `cam_distance`, `cam_yaw`).

The smoke scenarios reproduce claims of the research pages: `walk60` (the walk start, level99's street, slot 1),
`run_circle` (the run and the camera's auto-centre turning it into a circle, slot 1), both started at slot 1's spot
(level99 checkpoint 3 with `--start`), and `combat_cross` (`X1` then `XX2` at a puppet civilian 1.5 m away, slot 6, which
walks in to 1.36 m; on Coney the fight yard's `cross` spawn, a still target 1.05 m away that X1 steers onto alike).

**Gang fights are compared by behaviour, not by step.** The two games draw different random numbers, so a big fight
(a Rumble gang battle) never plays out alike twice, even in the original. Record several matches of the original and
several of Coney with the all-humans trace (one row per human per update; Coney's `--trace-humans`, the original's
`[[humans]]` field set of `research/traces/fields.toml`, with its hook logs beside it), then `coney-tools trace
fight-report --original ... --coney ...` computes rates, shares and distributions per side by the rules of
[Rumble: what the comparison computes](../research/rumble.md#gang-battle-metrics) (attacks, blocks, reactions and
target changes per standing minute, goal shares and fight goals, fighters on one enemy and active places, waiting
distances, help, each side's tactic, the match's length and winner) and flags each metric where Coney is outside the
original's spread ([coney-tools](coney-tools.md#trace)). Give one `--original` and one `--coney` per orientation (which
gang is side 1), as the gangs and the sides each change the numbers. Keep the recordings in the scratch folder, never
in the repository. Match the original's set-up on Coney's side: where the recording hands player 1 to the AI after
the countdown, run Coney with `--ai-player`, and give each Coney match its own `--ai-seed`
([Running: tracing](running.md#tracing)).

### Differential playthroughs {#differential-playthroughs}

A trace diff compares two games update by update, which holds for a few seconds of walking but not for a mission: the
AI, load times and the camera move every later event, and a fixed input script falls out of step with the game it
drives. A **differential playthrough** compares a mission at the level a player notices instead, the **order of
events**, and drives both games through the same stretch with one **adaptive course**:

1. **Event logs on both sides, at the same semantic points.** Each game writes one CSV line per event (`step, kind,
   name, detail`): a script binding called, a call into the scripts by name, a hint shown, a sound started, a human
   appearing, going down or removed. Coney writes them with `--event-log`
   ([Running Coney](running.md#event-log)); the original's come from hooks at the places that do the same thing (the
   script VM's binding call, the engine's script look-up, the hint box, the sound start), cited to their addresses on
   the research pages. A binding's call is the best hook: it is the mission script's own vocabulary, so both games
   name the event alike without interpretation.
2. **One course, two games.** The course is a small policy that looks, once an update, at what is on screen (the
   player and camera, the other humans, the world objects of interest) and at the update's events, and answers with
   the pad: walk into the marker that is shown, fight the nearest enemy when a lesson is armed, stand before a cabinet
   and press square then triangle. Both games give the same observation (Coney through `--pad-pipe`, the original from
   its tables over the emulator's memory interface), so one policy plays both, with the same partial stick
   deflections. A course plays the player's path: from the state at a checkpoint's start or a save made there, through
   the level as the Story path sets it up, never from a debug spawn the player cannot reach.
3. **Compare the order, not the frames** (`coney-tools trace events-diff`). The bindings the rules name become kinds
   (`tutorial`, `objective`, `checkpoint`, `hint`, `speech`, `spawn`, ...), long texts become hashes and arguments
   past a binding's arity are dropped. **Milestones** (tutorial steps, checkpoints, objectives, mission ends) are
   matched in order first and cut both logs into segments; every other kind is matched in order within its segment,
   so a hint is matched only between the milestones it came between. Sounds compare by set (their order follows the
   AI's timing), an event repeated within a few updates is one event, and an event seen more than `frequent` times
   compares by count. The report names the furthest milestone both games reached, the original's next one Coney never
   reached, and the missing, extra and out-of-order events with their steps; milestones whose spacing differs by more
   than a window are listed under timing.

A scenario (`research/traces/missions/*.toml`) names the course, the event that ends the run (`until`), the
original's save state and hooks, and Coney's level and checkpoint:

```sh
uv run --project python coney-tools trace mission research/traces/missions/level99_cp1.toml --side original --agent YOU --out ../../scratch/cp1-original.csv
uv run --project python coney-tools trace mission research/traces/missions/level99_cp1.toml --side coney --out ../../scratch/cp1-coney.csv
uv run --project python coney-tools trace events-diff ../../scratch/cp1-original.csv ../../scratch/cp1-coney.csv
```

Read a report from the top: where Coney stopped is the first finding, the rest are what the player meets on the way.
A missing hint or callback names a script path Coney never takes; a milestone far later on Coney than on the original
names a lesson the course could not finish, which is a Coney bug only once the same course finished it on the
original. When the two runs start at different points (a save state made inside a checkpoint, Coney at its start),
`--from "kind name"` starts both logs at the first event they share. The logs hold the game's text: they stay in
scratch, and `--labels` names the hashed texts from a string table read from your own disc.

Write a course from the player's view only: what a policy cannot see on screen it must not use (a script's internal
state, a handle number), or it will pass on one game for a reason the other does not share. When the course itself
fails on the original, fix the course before reading anything into Coney's run.

The `level3-chase` course (`research/traces/missions/level3_cp4.toml`, the rooftop chase from checkpoint 4) is the
disc test's route driver in Python: the same route and the same moves, so the two can be checked against each
other. It differs where a course sees less: it judges a leg done from where the player stands (the C++ driver also
reads his climb, jump and charge state), and it backs off for another charge when he stops gaining ground short of
the fence row (the C++ driver, when the charge ends short of it). It taught two things.
The course steers by the camera that is shown (`Camera_GetPlayerCamera`'s `0x005d9150`), not the follow camera
alone: the chase runs on rail and fixed cameras. And a course must play as fast as a player who wants to win: one that
walked up to each climb lost Snow to the pursuers on the original as on Coney, and passed on Coney only for some AI
seeds; sprinting into the climbs, holding triangle from a jump's take-off and charging again when a charge stops short
of the fence row, it reached checkpoint 5 on the original in 3,099 updates (issue #531).

## Lessons learned {#lessons-learned}

What past sessions taught, written so that it holds for any game. Multi-agent coordination, the shared machine and
merging are in [How we work](how-we-work.md#shared-machine); the emulator's mechanics are in
[Driving PCSX2](#driving-pcsx2).

### Method

- **Runtime beats reading.** When the code is ambiguous, run it: read or write the memory of the original while a
  scripted scenario plays, and grade the result `confirmed (runtime)`. Say on the page which patch or input the claim
  depends on.
- **Drive every movement like a player would.** Give input as a gamepad with analog sticks, at realistic partial
  deflections, and state the magnitude with every claim that depends on it ("stick 0.5, straight up"). A behaviour
  found only at full deflection is usually not what the player meets.
- **Input goes through the game's memory, never the window.** Writing pad state over the remote-memory interface (the
  patches above) is exact, repeatable and leaves the user's keyboard and focus alone. Use posted window messages only
  for the rare hotkey, and never move focus.
- **One sample per update.** Compare games by the update, not by wall-clock time: count the game's own ticks, record
  one row per update, and apply scripted input as each update is seen. Rows labelled by the poll rather than the
  update are off by one, which looks like a behaviour difference and is not.
- **Test your instruments.** A recorder, hook or diff tool is code too. Pin its labelling with a test that drives a
  fake game (the hook recorder once labelled every entry one update early until a test with two-message updates
  exposed it), and say in the guide how recordings made before a fix are wrong. When a result is off by a fixed
  amount, suspect the tool before the code.
- **Compare missions by events, not frames.** Past a few seconds, two games drift apart in time for harmless reasons
  (AI timing, loads, the camera). Compare the order of the events a player notices instead, drive both with one
  adaptive policy that plays by what is on screen, and let the first milestone one game never reaches say where to
  look ([Differential playthroughs](#differential-playthroughs)).
- **Compare, then localise.** Run the same scenario on both games and diff the traces; the first differing column and
  step says where to read. A difference goes back as a question or a fix, never as a page edit that hides it.
- **Trace the cause, not the symptom.** A stuck character, a missing effect or a wrong value usually comes from a
  state that is never reset or a handler that never fires; follow who writes the value and who clears it. Write down
  what you ruled out, in one line, so nobody repeats it.
- **Test through the path the player uses.** A reimplementation grows shortcuts for testing (start a level directly,
  skip the menus), and each shortcut that sets the game up its own way hides bugs the player meets: a pause that only
  the real path has, a hand-over between checkpoints that a direct start never plays, a debug service wired to one
  path only. Keep one set-up that every start goes through, differing only in where it starts, and run playthrough
  tests through it. When a play-tester sees a bug that a test does not, first ask which path each one took.
- **Find a second source for the same function.** Another build of the game (named library calls, inline strings) can
  read faster; cite evidence only at the primary executable's addresses.
- **Ask the data.** Tables, lists and counts can be read from the disc by tool and printed as counts and hashes;
  that is evidence anyone can rerun, and it puts no game data in the repository.

### Conduct

- **Claim before use, release after.** Anything shared and scarce (an emulator copy, a lock) has one user at a time:
  claim it with the tool, release it when done, and close what you started.
- **Keep other people's slots read-only.** Quick-save slots, settings and files someone else made are never overwritten;
  save your own states to files in scratch, and put patched words back before reusing a saved state.
- **Add names, never undo them.** The shared Ghidra project is written by many engineers: rename and annotate freely,
  but do not revert another engineer's names; a disagreement goes on the page as an open question.
- **Do not guess in code.** A gap in a page becomes an open question on the page and a marked stand-in in code, never
  a silent guess ([When a research page isn't enough](#when-a-research-page-isnt-enough)).
- **Research is done when the page maps the behaviour to the code:** a name and plate comment in Ghidra, and a page
  that names the function, cites its address with an evidence level and says what the code alone does not show
  ([The Understood measure](#understood)). The engineer who ports it reads the function itself.
- **Weak evidence stays labelled.** A screenshot is a source of measurements, not a claim; a claim quotes numbers, the
  scenario that produced them and the build or state they came from.

## Writing up a finding

A finding is done when someone else can use it without asking you. For each one:

1. **Update the living page.** Find the page for the subsystem or format and change it in place; create a page only
   when none covers the subject. Don't start a dated notes page: one page per subject means the reader never has to
   reconcile two versions.
2. **Grade every claim** with an [evidence level](#evidence-levels); a runtime claim cites the
   [run record](#run-records) of the run, and `coney-tools research claims` lists what the branch graded.
3. **Add or update the YAML entries** for the functions, globals and types the finding touches (once the database exists),
   with the same evidence level as the page.
4. **Cite addresses** as `0x001490b8`: eight hex digits, in the NTSC-U executable `SLUS_212.15` (SHA1
   `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Every address in the project refers to that one file, so a reader
   with the same disc can check any claim. The check holds an address to the Ghidra listing and the executable's
   sections ([Addresses](writing-docs.md#addresses)), and a function is cited with the name Ghidra gives it.
5. **Describe, don't reproduce.** Explain behaviour in prose, with short pseudocode or a short snippet where it
   makes the point clearer. Never paste a whole function listing from Ghidra or a disassembly, and never paste a long
   stretch of one: it puts the original's code in the repository. See
   [LEGAL.md](repo:LEGAL.md#research-limits).
6. **No game data.** Tables of offsets, constants and counts are facts and belong on the page; the bytes of a game
   file, a dump or an extracted asset never do.
7. **Check and build the docs:** `coney-tools docs check` holds every page you touched to the standard
   ([Checking a page](writing-docs.md#checking)), and `mkdocs build --strict` catches broken links and pages missing
   from the navigation. A page in the legacy file keeps its excuses until a change touches it; then the mechanical
   rules apply page-wide and the section you edit is graded ([Legacy pages](writing-docs.md#legacy-pages)).
8. **Ship the docs with the code.** CI refuses a pull request that changes `src/` or `python/src/` without changing
   anything under `docs/` or `research/`. A change that needs no documentation (a refactor, a rename) says so with a
   line `Docs: none` in the pull request description; editing the description re-runs the check. Check it yourself with
   `git diff --name-only main... | uv run --project python coney-tools repo check-docs`. (The description itself is
   checked against the pull request template by `coney-tools repo check-body`.) Counts quoted in a page by
   hand are held to the data by tests and by `{count}` in the reference lists ([above](#reference-lists)).

## The Understood measure {#understood}

A function is researched in full ("understood" on the [progress page](../progress/index.md)) when it has a
meaningful name and a plate comment in the shared Ghidra project, and a research page cites its address in a table
row or paragraph that states an evidence level and says what it does. A function nobody needs (dead code, a debug
stub) is cited with `not needed: <reason>` instead. The bar checks the name and the citation; the plate comment is
listed in the backlog. Citations on the [source map](../research/source-map.md) do not count; a script binding's
wrapper counts through its `evidence` in `research/bindings`.

The names, sizes and plate flags come from `docs/progress/ghidra-functions.tsv`, exported from Ghidra; the
citations are read from the pages on every docs build. After renaming functions in Ghidra, refresh both
(ghidra-mcp on `:8090` must be running; the export only reads):

```sh
uv run --project python coney-tools progress ghidra    # re-export the listing (--check: exit 1 when stale)
uv run --project python coney-tools progress check     # the inputs agree; the docs build writes the bars
uv run --project python coney-tools progress backlog "<scratch>/research-backlog"   # per-subsystem to-do lists
```

The backlog writes one Markdown file per subsystem, outside the repository: every function that is not understood,
largest first, with what it lacks (`name`, `cite` or `evidence`), whether it has a plate comment, and the name a page
already gives it. A row that lacks only `name` is documented already; renaming it in Ghidra is the cheapest step.
