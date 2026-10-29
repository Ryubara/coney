# The coney-tools reference commands

The commands that keep Coney's generated reference material current: [`progress`](#progress), the progress
tables of the README and the docs; [`bindings`](#bindings), the script-binding masterlist; [`refs`](#refs), the game
reference lists; and [`missions`](#missions), the mission status pages. Run them from inside the checkout, as
[the overview](coney-tools.md#running) says; the groups that read the disc take it as
[the disc commands](coney-tools-disc.md#naming-the-disc) do.

## progress {#progress}

The [Progress](../progress/index.md) page and the badges in the Progress section of `README.md` are generated from
four inputs: the [function database](research-function-database.md) (`research/functions/<system>.toml`; the entries
with a `coney` field are the original functions Coney reimplements, their sizes the Ghidra listing's),
`docs/progress/totals.toml` (the denominators, taken from the [source map](../research/source-map.md); its file ranges
are `research/source-files.toml`), the Ghidra listing `docs/progress/ghidra-functions.tsv` and the status table of the
[roadmap](../roadmap.md). The page says what the numbers mean; this section says how to run the commands.

```sh
uv run --project python coney-tools progress summary [--json]
```

Prints the reimplemented and researched shares, the compared share (a `compared:` line: how much of the reimplemented
code is `compared` with the original), the understood share, the milestones and the subsystems that have
reimplemented code. Every share counts functions of the game's own code in the Ghidra listing
([why](../progress/index.md#by-function)). `--json` prints everything, per subsystem and per coverage category, for
other tools; each share is a `functions`, `total` and `percent` (the compared share is its `compared` key, with the
reimplemented functions as `total`; the total and percent are `null` for a share the checkout has no Ghidra listing to
count). It exits with 1 when the
database's `coney` entries and the `@orig` tags in `src/` disagree, and lists each problem.

```sh
uv run --project python coney-tools progress check [--base REF] [--require-base]
```

Checks the inputs and exits with 1 when they disagree; CI runs it. Nothing generated is committed: the tables of the
progress page and the README's badge data are produced by the documentation build (`mkdocs_hooks.py`), which publishes
them from `main`, so pull requests never conflict on them. `check` holds the function database's `coney` entries and
the `@orig` tags in `src/` together ([the rules](research-function-database.md#ported)), and refuses an address in
middleware or outside `.text`, a progress page without its single `<!-- progress:generated -->` marker and a history
page without its `<!-- progress:history -->` marker. `--base REF` (default `origin/main`) is what tag names are
measured from: a tag the branch adds that spells a name differently from the database fails, the ones already there
are a warning with their count. When git cannot read the base (a copy of the files, a shallow clone) the rule is
skipped with a warning, unless `--require-base` makes that a failure; CI passes `--base HEAD^1 --require-base`, the
commit its checkout (depth 2) holds. `mkdocs build --strict` shows the tables on the page locally too, and fails on the
same problems.

The pictures come from `progress_grid.py` (one square per function, from the Ghidra listing, the tracker's measures
and the file ranges of `research/source-files.toml`), `progress_picture.py` (layout and SVG), `progress_history.py`
(one point per day from git) and `progress_site.py` (the page's HTML and the files of the site); `mkdocs_hooks.py`
calls them. The
page's tooltips are `docs/javascripts/progress-grid.js`, the colours `docs/stylesheets/progress-grid.css`, and what
they mean is on [the page](../progress/index.md#the-picture). Where the fidelity audit's results will be read is
`progress_grid.load_audited`: it returns no addresses until the audit stores a machine-readable result on `main`, and
the picture then draws the Audited state.

The [home page](../index.md) shows the same picture in a compact form, filled in at its `<!-- progress:home -->`
marker by the same build (`progress_site.home_section`). The squares and their order are the progress page's, but
the five states fold into three (`progress_grid.HOME_STATE_OF`): Reimplemented (audited included), Understood, and
Not yet started (researched and untouched), coloured by the home page's own classes `cg-h-<state>` and palette
(`HOME_PALETTE`, repeated under `.progress-home` in `progress-grid.css`; a test holds the two equal). Reimplemented is a
darker green; the two greys read by lightness on both themes: **Understood is the softer grey and Not yet started the
stronger one** (light page: `#d0d6dc` and `#b0b8c2`; dark page: `#4a5160` and `#9da5b0`), and a test keeps every pair
of the three at least 8 L* apart and each one visible against the page (green 3:1, the greys 1.4:1). There are no
block labels: the blocks flow on from one another, `HOME_COLS` squares to a row (`progress_picture.flow`; each
block's `data-o` is where it starts), so the picture is some 50 rows high. It reads the same `progress/functions.json`
with the same script, so hovering or tapping a square shows the progress page's tooltip, Coney file included, with
the swatch in the home colour, and **the state named as the legend names it**: `HOME_LABELS` is the one table, and
the section hands it to the script as `data-labels` (`progress_grid.home_state_labels`, by state code), so a
researched or untouched square says "Not yet started", an audited one "Reimplemented". The block text shown before the
table loads (`picture_blocks(..., home=True)`) counts the same three states. The progress page keeps its five names.
`progress check` refuses a home page without its single marker, and `progress render` writes the filled page as
`index.md`.

**The README's picture.** The README on GitHub cannot run the home page's script, and `main` takes no direct pushes,
so the picture there is not committed: the docs build draws it (`progress_png.py`, Pillow) and publishes it with the
site as `progress/readme-light.png` and `progress/readme-dark.png`, and the README embeds them by the site's address
inside a `<picture>` (GitHub shows the dark file on a dark page) that links to the home page. It is the home page's
squares in the same order, states and colours (`HOME_STATE_OF`, `HOME_PALETTE` and `HOME_LABELS` are read, never
repeated), `COLS` (170) to a row so that it fits a README column; the headline (`progress_grid.home_headline`, which
the home section prints too, with "counted by function" added), the counts and the legend are drawn into the picture,
so it never needs the README text to change. Its percentage and the badges beside it both count functions, so they
agree; the README says in one sentence how its legend differs from the Understood badge. The file is drawn twice as
large as its 880 px display width (`SCALE`) for dense screens, stays under 60 KB, and holds no date, so the same data
is the same bytes. The site's address is in the README only (the one file allowed to hold it), and a test holds the README's
file names equal to `progress_png.FILES`. It follows `main` when the next docs build is published: GitHub's image
proxy may keep showing the old picture for a few minutes. The docs build therefore needs Pillow, which
`requirements-docs.txt` pins to the version `python/uv.lock` locks (a test holds the two equal). To see the files
without a build, `progress render` writes them with the other pictures.

```sh
uv run --project python coney-tools progress render <folder> [--no-history]
```

Writes what the build publishes to `<folder>`, to look at it without a docs build: the progress and history pages as
the build fills them (`progress/index.md`, `progress/history.md`) and the home page with its picture (`index.md`), the
badge files (`progress/badges/*.json`,
shields.io endpoint JSON that the README's badges read), the pictures (`progress/progress.svg`,
`missions.svg`, `history.svg`, and the README's `readme-light.png` and `readme-dark.png`),
the raw numbers (`progress/progress.json`), the per-function
table the tooltips read (`progress/functions.json`) and the history (`progress/history.json`). It rebuilds the daily
history from git, a few seconds a day; `--no-history` keeps it to today's point. It refuses to write while the inputs
disagree, as `check` finds.

The docs build does the same, with the history limited so a pull request's build stays quick: it keeps the days of the
file named by `CONEY_PROGRESS_HISTORY_CACHE` (the previously published `progress/history.json`) and today, and measures
the missing days only when `CONEY_PROGRESS_HISTORY=full`. The `docs` workflow sets both on a push to `main`: it
downloads the published file (a missing one just means every day is measured), builds with `full` and deploys the
result, so each deploy extracts one commit. A cached day counts only for the commit it was measured at, and a day
that could not be measured is recorded with its commit and not tried again. The file also records a digest of the
tracker's measuring code (`progress.py`, `progress_import.py`, `progress_research.py`, `research_functions.py`): when
that code changes, the cache is ignored and
every day is measured again, so the chart never mixes two definitions. A day before the move to the database has the
retired list `docs/progress/functions.toml` instead; it is read for that day, with the Ghidra listing's sizes where the
day has a listing. A pull request's build has no cache (only the
deploy learns the site's address), so its History page shows today alone; locally, point
`CONEY_PROGRESS_HISTORY_CACHE` at a downloaded `history.json` to see the days.

```sh
uv run --project python coney-tools progress check-sizes [DISC]
```

Checks the size of every reimplemented function against the executable `SLUS_212.15` on your own disc (`DISC` works
as for the `wad` commands). The size is the Ghidra listing's; a function the listing has no function at has none, and
the line shows `-`. The executable has no symbols, so the command takes every `jal` target, every function pointer in
`.data` and every address into `.text` that code builds with a `lui` / `addiu` pair (the function pointers handed over
in code, such as the script binding wrappers) as the start of a function, and a function's **span** runs to the next
of those starts, alignment padding included. A size larger than the span is an error (exit code 1); one smaller than
the code before the padding, or an address no call reaches, is a warning. It prints only addresses and sizes.

The span is an upper bound: a function that nothing calls directly and no pointer names (one reached only by a tail
jump) is not seen as a start, and the span of the function before it includes it, unless that function is itself
reimplemented: a reimplemented function always ends the span of the one before it.

```sh
uv run --project python coney-tools progress import-functions [FILE] [--dry-run] [--all]
```

A one-off, for the move of the reimplemented list into the function database (issue #778): it reads the retired
`docs/progress/functions.toml` (or `FILE`) and gives each of its functions an entry with `coney` and `status =
"legacy"`. It also serves a branch that still adds rows to the old file: when its rebase meets the file's deletion,
save the branch's copy of the file outside the checkout, take main's side and run the command on the copy, which adds
only the rows the database lacks. An entry the database has gets `coney` and keeps everything else; any other function
gets a new entry from the research that cites it; which page is its home and when a level is tied to it are in [the
function database](research-function-database.md#ported), the one place that states the order. A level is never
guessed: an entry whose level cannot be tied has none, and a note saying that the import tied none. A short table of
overrides in the importer (address, page, level, the page's words and a reason; a test keeps each in step with its
page) fixes the few entries the rules read wrongly. Among equals it takes the page with the most such functions, and
the entry goes in the file of that page's system (`combat-grabs.md` is in `combat` when `combat.md` is a page). The
name is Ghidra's, `coney` comes from the `@orig` tag in `src/`. The report counts the new entries by source and by
system and lists the rows whose name is not Ghidra's, the rows with no function in the listing, the entries with no
level and any row left out (the exit code is 1 for those); `--dry-run` writes nothing and `--all` lists every row.
Delete the old file afterwards. The command is to be removed once no branch carries the old list.

<!-- command-reference:begin progress -->
## Parameters of `progress` {#progress-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `progress summary` {#progress-summary-usage}

Print how much is reimplemented and researched.

```text
coney-tools progress summary [--json]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print everything as JSON |

### `progress check` {#progress-check-usage}

Check the tracker's inputs (the function database, @orig tags, ranges, roadmap).

```text
coney-tools progress check [--base BASE] [--require-base]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--base BASE` | text | `origin/main` | what tag names are measured from (default: origin/main) |
| `--require-base` | flag | off | fail when --base cannot be read, instead of skipping the name rule (CI) |

### `progress render` {#progress-render-usage}

Write the pages, pictures, badge JSON and numbers the docs build publishes, to a folder.

```text
coney-tools progress render OUT_DIR [--no-history]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `OUT_DIR` | path | required | a folder to write progress/ (the pages, badges and pictures) to |
| `--no-history` | flag | off | skip rebuilding the daily history from git (only today's point) |

### `progress check-sizes` {#progress-check-sizes-usage}

Check the reimplemented functions' sizes against your own disc.

```text
coney-tools progress check-sizes [DISC]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC]` | text | none | a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml |

### `progress import-functions` {#progress-import-functions-usage}

One-off: move the rows of the retired docs/progress/functions.toml into the function database.

```text
coney-tools progress import-functions [FILE] [--dry-run] [--all]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[FILE]` | path | none | the old list (default docs/progress/functions.toml) |
| `--dry-run` | flag | off | write nothing; print what would be done |
| `--all` | flag | off | list every row of the report, not the first few |

### `progress backlog` {#progress-backlog-usage}

Write one Markdown file per subsystem of not-yet-understood code.

```text
coney-tools progress backlog OUT_DIR
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `OUT_DIR` | path | required | a folder outside the repository |
<!-- command-reference:end -->

## bindings {#bindings}

The script-binding reference (`docs/references/bindings/`, under Game references) is generated from
`research/bindings/<category>.yaml` (schema: [The research data](research-data.md#bindings)).

```sh
uv run --project python coney-tools bindings render [--check]
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
uv run --project python coney-tools bindings sync-coney [--check]
```

Sets the `coney` key of every entry from Coney's binding table, `kBindings` in `src/scripting/script_bindings.cpp`:
`real` is *implemented*, `routed` (handed to a stand-in for a subsystem Coney lacks) is *partial*, `stub` and
`recording` are *not implemented*, and so is a binding the table does not list; the default is written as no key. Run
it, then `bindings render`, after changing the table. It fails when a line of the table is not one `kind("Name", ...)`
entry, or names a binding the masterlist lacks. With `--check` it writes nothing and exits with 1 when a status is
stale; CI runs it that way.

```sh
uv run --project python coney-tools bindings render-cpp [--check]
```

Writes `src/debug/native_signatures.cpp`, the C++ table the [debug menus](debug-menu.md#natives) build their argument
editors from: every binding's name, category, the type of each argument (number, integer, handle, boolean, string, a
table of numbers or strings with its fixed count, userdata) with its default, and the types of its results. A number
argument whose description says it is a handle gets the handle editor. Only names, types and defaults go in, never a
description. Run it after editing the YAML and commit the file with the change; with `--check` it writes nothing and
exits with 1 when the file is stale, as CI runs it.

```sh
uv run --project python coney-tools bindings scan-usage --first-mission [DISC] [--check]
```

Reads the first mission's compiled scripts (`level99.lua` and its three chapter scripts) and `global.lua` from your
disc (the `game_dir` of `coney.local.toml` when no disc is given), and sets every entry's `usage.mission1`: true when
those scripts name the binding, or a `global.lua` helper they reach does (followed by name, as a `Table.field` or as a
callback string), or one of the two helpers the engine calls when a mission ends (`UnlockAndLoad`, `runNextMission`).
It prints the coverage (bindings, traced, Coney status), which `bindings render` also publishes as
[Mission 1 coverage](../references/bindings/mission1.md). Only names are read; nothing from the disc is written. Run
`bindings render` after it. With `--check` it writes nothing and exits with 1 when a marker is stale; it needs the disc,
so CI does not run it.

```sh
uv run --project python coney-tools bindings scan-usage --story [DISC] [--check]
```

The same for the rest of the story: for each later level of `STORY_LEVELS` (`coney_tools/natives.py`: missions 2-18
and the hub in story order, then the flashbacks and the Armies of the Night levels) it reads the level's scripts
(`levelNN.lua` and its `levelNN_*.lua` chapters, found by name in the WAD names list, without the language files and
the scene tests) with the same reach rule, and sets every entry's `usage.levels` to the numbers of the levels that can
call it (no key when none). It prints each level's bindings and how many are new (called by no earlier level, the
first mission included) and traced; `bindings render` publishes them as
[Story coverage](../references/bindings/story.md). Run `bindings render` after it; `--check` as for `--first-mission`.

```sh
uv run --project python coney-tools bindings stats
```

Prints the counts by category, evidence level, detail and Coney status, and how many bindings the game's scripts
call.

<!-- command-reference:begin bindings -->
## Parameters of `bindings` {#bindings-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `bindings render` {#bindings-render-usage}

Check the YAML and regenerate docs/references/bindings/.

```text
coney-tools bindings render [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--check` | flag | off | change nothing; exit 1 when a page is stale |

### `bindings sync-coney` {#bindings-sync-coney-usage}

Set each entry's coney status from src/scripting/script_bindings.cpp.

```text
coney-tools bindings sync-coney [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--check` | flag | off | change nothing; exit 1 when a status is stale |

### `bindings render-cpp` {#bindings-render-cpp-usage}

Write the debug menus' C++ signature table, src/debug/native_signatures.cpp.

```text
coney-tools bindings render-cpp [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--check` | flag | off | change nothing; exit 1 when the table is stale |

### `bindings stats` {#bindings-stats-usage}

Print the counts by category, evidence level and usage.

```text
coney-tools bindings stats
```

Takes no parameters.

### `bindings scan-usage` {#bindings-scan-usage-usage}

Set the usage fields from the scripts on your disc.

Exactly one of `--first-mission`, `--story` is required.

```text
coney-tools bindings scan-usage [DISC] (--first-mission | --story) [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC]` | text | none | a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml |
| `--first-mission` | flag | off | set usage.mission1 from the first mission's scripts. Not with `--story`. |
| `--story` | flag | off | set usage.levels from the story's later levels' scripts. Not with `--first-mission`. |
| `--check` | flag | off | change nothing; exit 1 when a field is stale |
<!-- command-reference:end -->

## refs {#refs}

The [Game references](../references/index.md) are generated from the lists in `research/references/`
([The research data](research-data.md#reference-lists) has their schema and rules).

```sh
uv run --project python coney-tools refs refresh [DISC] [--only LIST ...] [--names FILE]
```

Reads your own disc (`DISC` works as for the `wad` commands) and refreshes every list, or only the ones named
(`characters`, `levels`, ...): the configuration calls of the compiled scripts, the Character List of
`warriors.glr`, every animation clip, three tables of the executable, the scene list `scene_list.cnk`, the
movies in the disc's `PSS` folder (when `DISC` is a folder) and the WAD's entry names. It keeps every
hand-written field and every entry it did not produce, then renders the pages. `--names` adds WAD names found
another way (a file with one name per line, or the last word of each line, as `wad recover-names` writes), each kept
only when it hashes to an entry; the names already in `wad-names.yaml` are always used. A full run takes about half a
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
`refs refresh` links the images.

<!-- command-reference:begin refs -->
## Parameters of `refs` {#refs-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `refs render` {#refs-render-usage}

Check the lists and write the pages of docs/references/.

```text
coney-tools refs render [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--check` | flag | off | change nothing; exit 1 when a page is stale |

### `refs refresh` {#refs-refresh-usage}

Refresh the lists from your own disc, keeping hand-written fields.

```text
coney-tools refs refresh [DISC] [--only LIST [LIST ...]] [--names NAMES]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC]` | text | none | a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml |
| `--only LIST [LIST ...]` | choice | none | these lists only. One of `characters`, `character-models`, `gangs`, `goal-types`, `speed-classes`, `objects`, `object-groups`, `cars`, `particles`, `levels`, `level-starts`, `flags`, `zones`, `boxes`, `scenes`, `glass-types`, `doors`, `tints`, `lights`, `spawner-states`, `crime-types`, `cameras`, `screen-effects`, `inventory`, `unlockables`, `power-classes`, `warrior-classes`, `attacks`, `hats`, `rumble`, `statistics`, `animations`, `anim-ids`, `controls`, `commands`, `hud-colours`, `radar-icons`, `text-formatting`, `text-labels`, `enums`, `sound`, `speech`, `script-events`, `wad-names`. |
| `--names NAMES` | path | none | extra WAD names, one per line (the last word of each line) |

### `refs compress-images` {#refs-compress-images-usage}

Rewrite the thumbnails as 256-colour PNGs, in place.

```text
coney-tools refs compress-images [FOLDER]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[FOLDER]` | path | none | default: docs/references/images/ |
<!-- command-reference:end -->

## missions {#missions}

The [Missions](../missions/index.md) pages are generated from `research/missions.yaml`
([The research data](research-data.md#missions) has its schema and who updates it).

```sh
uv run --project python coney-tools missions render [--check]
```

Checks the list (every story level present, each mission's checkpoint count equal to the Sections column of the
levels list, each status consistent with its checkpoints) and writes `docs/missions/index.md` and one page per level,
with the script-binding numbers of [Story coverage](../references/bindings/story.md) read from
`research/bindings/`. With `--check` it writes nothing and exits with 1 when a page is out of date; CI runs it that
way. Run `bindings render` first when bindings changed.

<!-- command-reference:begin missions -->
## Parameters of `missions` {#missions-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `missions render` {#missions-render-usage}

Check the list and write the pages of docs/missions/.

```text
coney-tools missions render [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--check` | flag | off | change nothing; exit 1 when a page is stale |
<!-- command-reference:end -->
