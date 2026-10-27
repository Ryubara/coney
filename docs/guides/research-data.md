# The research data

The research database and the lists built from it: the function database and the source files, the script bindings,
the game reference lists and the mission list. Each is data in `research/` that a tool reads and that a generated page
shows. The rest of the research workflow is listed in [its overview](research-workflow.md#pages).

## The research database {#the-research-database}

Research pages are for reading; the research database is the same knowledge as data in `research/`, which tools look
up directly. It is built one kind of fact at a time: the script bindings ([below](#bindings)) and the functions
([below](#function-database)) exist, and globals and structs are planned the same way. The rule behind all of it:

> A fact that a tool must find, check or count lives in a data file whose entries all have the same fields. The page a
> person reads is generated from that file, or links to it.

That layout earns its keep four ways:

- **Lookup.** An agent asks the file (is `0x0026c548` researched, what is it called, which page describes it, has its
  port been compared?) with `grep` or `tomllib`, and does not read prose to find out.
- **Uniform fields.** Every entry has the same fields in the same order, so nothing is missing because the author of a
  page forgot a column.
- **Checks.** A program proves the answers are still true (the address is a function, the name is Ghidra's, the
  section exists) and CI refuses a pull request that breaks them; a hand-written table of 600 rows cannot be checked.
- **Generated pages.** The list a person browses is built from the file by the docs build and is never edited, so the
  two cannot drift.

### The function database {#function-database}

`research/functions/<system>.toml` lists the researched functions of one system, one entry each with its address,
Ghidra name, page section, role and, where the page states one for the function, evidence level;
`coney-tools research render --check` holds it to the Ghidra listing and the pages. Its fields, checks, import and
generated lists are on their own page, [The function database](research-function-database.md).

### The source files {#source-files}

`research/source-files.toml` lists the original's source files with the address range the source map gives each, one
entry per file, and the source map's file tables are generated from it. It is where the owner of an address is looked
up first. Its fields and checks are on their own page, [The source files](research-source-files.md).

### Owning pages and the glossary {#owning-pages}

Every research page of a kind that owns code lists what it owns in its front matter, so every address has one owning
page; `coney-tools research where` answers from that, and the docs build makes the
[address page](../research/find/addresses.md) from it. `research/glossary.yaml` links the words a player uses to the
page and functions that describe them, and the [words page](../research/find/words.md) is made from it. How the lookup
works, its output and the glossary's schema are on [Finding where a behaviour lives](research-where.md).

### The script bindings {#bindings}

The files `research/bindings/<category>.yaml` list every function the game registers with its Lua state, one entry per
name: the masterlist of the game's script bindings (what FiveM calls natives), which is also the reference for script
mods. There is one file per category, named by the category's id (the ids are in `coney_tools/natives.py`), so an entry
has no `category` key; moving a binding to another category means moving its entry to that file.
`coney-tools bindings render` checks the files and generates the reference pages in `docs/references/bindings/` from
them; the pages are never edited by hand, and CI runs `bindings render --check`
([coney-tools](coney-tools-references.md#bindings)). An entry of `level.yaml`:

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
  reference it (`mission1` also counts the `global.lua` helpers those scripts reach; `coney-tools bindings scan-usage
  --first-mission` sets it from the disc), and whether a traced call keeps its result (a lower bound). It holds counts
  only. The optional `levels: [80, 87, ...]` lists the story's later levels whose scripts can call it, in story order
  (`coney-tools bindings scan-usage --story` sets it from the disc).
- `coney` is never edited by hand: `coney-tools bindings sync-coney` sets it from Coney's binding table
  (`src/scripting/script_bindings.cpp`), writing the key only when it is not the default
  ([coney-tools](coney-tools-references.md#bindings)).
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
- Every entry has a **source** and an **evidence** level (the [levels](research-evidence.md#evidence-levels), spelled
  `confirmed-code`, `confirmed-runtime`, `inferred`, `speculative`), its own or the list's `defaults`.
- Each entry has a stable **anchor**, `<list page>#<prefix>-<key>` (`characters.md#char-32`,
  `levels.md#level-29`), for links from research pages, issues and mods. A key never changes once published.
- Fields are either **read from the disc** or **hand-written** (each page's field table says which).
  `coney-tools refs refresh` rewrites the first kind and keeps the second, and keeps entries it did not produce,
  so a hand-added entry or a name found another way survives a refresh.

Commands ([coney-tools](coney-tools-references.md#refs)):

```sh
uv run --project python coney-tools refs refresh [DISC] [--only LIST ...] [--names FILE]   # refresh from your disc
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
disc), then run `refs refresh`. **Thumbnails** go in `docs/references/images/` (`characters/<model>.png`,
`objects/<name>.png`, `cars/<type>.png`; 256 × 256, transparent, rendered by `coney --render-references` from the
player's disc, then shrunk with `coney-tools refs compress-images`); `refs refresh` links each one that exists. The
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
in the levels list (`GSTRING.MISSIONNAME` of the English string file, filled by `coney-tools refs refresh`), shown in
the page heading and the overview. The five statuses are Not Started, In Progress, Pending Gameplay Approval, Needs
Fixes and Approved, for missions and checkpoints alike.

**Whose job:** the engineer (human or agent) updates `missions.yaml` in the same commit as mission work: In Progress
when a checkpoint is started, each checkpoint to Pending Gameplay Approval when it plays to its end, the mission to
Pending when every checkpoint does, and re-runs `missions render`. Only a maintainer moves a mission to Needs Fixes or
Approved after play-testing it against the original, listing what was off under `issues`; the commit that merges the
fix moves it back to Pending. A research finding that changes what a mission needs goes on the research page, which
the entry links.
