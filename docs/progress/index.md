# Progress

How far Coney has come, measured four ways:

- **Reimplemented:** the share of the original game's own code that Coney's code reimplements, in bytes of the
  executable's `.text`. This is the decompilation projects' progress bar, adapted to a functional reimplementation:
  a function counts once Coney has code that does its job and cites it with an `@orig` tag
  ([Conventions](../guides/conventions.md#comments-and-orig)), not when its machine code matches.
- **Researched:** the share of the game's own code that the [source map](../research/source-map.md) places in at
  least an original source file or directory. It says how much of the executable a contributor can already find their
  way around in, not how much is understood.
- **Understood:** the share of the game's own functions (by bytes, and by count) that are researched in full: the
  function has a meaningful name in the shared Ghidra project (not `FUN_<address>`), and a research page cites its
  address in a table row or paragraph that states an evidence level, or says `not needed: <reason>` (the source
  map's citations do not count: placing is not understanding); a script binding's wrapper counts when its
  `research/bindings` entry has an `evidence`. Names and sizes come from `docs/progress/ghidra-functions.tsv`
  (addresses, sizes and our names only), the citations from the pages themselves, so citing a function moves the
  bar in the same commit. How to refresh it: [Research workflow](../guides/research-workflow.md#understood).
- **Compared:** the share of the reimplemented code (in bytes, and by count of functions) whose port has been checked
  side by side with the original function in Ghidra, paired and reviewed: its `status` in the
  [function database](../guides/research-function-database.md#entry) is `compared` and not `legacy`. It starts at 0%
  because every function ported before the standards were set is `legacy`; it rises as a port is compared, which a
  change to a ported function owes ([the standards](../guides/coney-tools-standards.md#standards)).
- **Milestones:** the status table of the [roadmap](../roadmap.md).
- **Missions:** the [mission checklist](../missions/index.md): each story level's status, from Not Started to
  Approved by the user's play-test, with its checkpoints and the script bindings it needs.
- **The picture:** every function of the game's own code as one small square, grouped by source
  directory and coloured by the furthest state it has reached ([How the picture is drawn](#the-picture)), and the
  missions' checkpoints the same way. The [history](history.md) shows how the shares moved day by day.

The tables below are not in the repository: they are generated each time the documentation is built, so this page
always shows the numbers of `main`, and the badges and the square chart in the repository's `README.md` read the same figures
from the published site. The rest of this page is written by hand.

<!-- progress:generated -->

## How it is measured

All of it is about the NTSC-U executable `SLUS_212.15`, whose `.text` is 4,154,744 bytes.

**The game's own code** is every byte of `.text` that is not middleware, as the source map's
[coverage table](../research/source-map.md#coverage) counts it: 3,342,408 bytes. It is the denominator of both
bars. The middleware (Lua 4.0.1 and tolua, RenderWare Graphics, the SCE libraries, Bink, the C and C++ runtimes)
is left out: Coney replaces it with librw, SDL3, the C++ standard library and its own Lua 4.0 interpreter, it does
not reimplement it. The game's own glue around a library (`Movie/BinkMovie.cpp`,
`Graphics/Devices/Renderware/DevRWGeneric.cpp`) is game code and counts.

**Per subsystem**, the total is the size of the address range the source map gives the top-level directory, from
its first to its last attributed function. That is **inferred** (the ranges are the map's inferred directory
ranges), and it undercounts every directory: the game code the map has not placed, nearly half of it, is shown as
*Unattributed*, and a directory with no attributed function yet (`Debug`, `Utils`) shows "not placed yet". As the
source map improves, code moves from Unattributed into the directories, and the per-subsystem shares can go down
while the overall share stays the same. *Link-once* is the game's template and inline code, which the linker
placed after the libraries.

**Reimplemented bytes** are the sizes of the original functions that the [function
database](../guides/research-function-database.md#ported) (`research/functions/<system>.toml`) records a port for, the
entries with a `coney` field. The database lists the researched functions too; only the ported ones count here. The
denominators are in [`docs/progress/totals.toml`](repo:docs/progress/totals.toml), copied from the source map by hand.
A function counts in full once it is recorded; there is no partial credit.

The executable has no symbols, so a function's size comes from the shared Ghidra project: the committed listing
`docs/progress/ghidra-functions.tsv` gives the size of the function at each address, the unit the Understood bar
measures too. A port tagged at an address the listing has no function at yet counts as a function but adds no bytes
until Ghidra defines it. Before the database held the list (October 2026) the sizes were kept by hand in
`docs/progress/functions.toml`, estimated from the disc; the move changed the Reimplemented bar from 33.3% to 33.9%,
mostly because the hand-kept sizes of a few large functions were too small and 19 had none. `coney-tools progress
sizes` still checks the sizes against your own disc. How to use the commands: [The progress
commands](../guides/coney-tools-references.md#progress).

## How the picture is drawn {#the-picture}

The picture of every function (under *Every function* above) draws one square for each function of the game's own code in
`docs/progress/ghidra-functions.tsv` (the middleware ranges of `totals.toml` left out, as for the bars). It does not
define anything new: each colour is a measure this page already has.

| State | What puts a function in it | Measure it comes from |
| --- | --- | --- |
| Audited | The fidelity audit found Coney's version equal to the original | Not drawn yet: no audit result is stored on `main` in a machine-readable place |
| Reimplemented | Recorded with `coney` in the function database, so Coney has code for it with an `@orig` tag | The Reimplemented bar |
| Understood | Named in Ghidra and cited on a research page with an evidence level | The Understood bar |
| Researched | Inside a source directory or file range of the source map, but not understood | The Researched bar |
| Untouched | None of the above | |

A function shows the furthest state it has reached, top to bottom. The headline above the picture counts squares by
measure, each measure as the table defines it, so a square counts under every measure it meets: a reimplemented function
that is also understood counts in both, while the legend counts squares by the colour they show. Researched is the
source map's measure, not "researched or further": a function can be understood without the map placing it, so the
Researched share can be below the Understood share. Blocks are the subsystems of `totals.toml`, the original's top-level
source directories, in address order, so the picture follows the executable; code in no subsystem range comes last as
*Unattributed*, and the game's link-once template code is the `link-once` block. A block's label is its share of
reimplemented functions, and its tooltip (the browser's own) gives `reimplemented/total (share)` and the squares of each
state. A reimplemented function whose address is not the start of a function in the Ghidra listing (an entry inside
another function, or a listing that split a function differently) is counted in the bars but has no square and is not in
the headline's Reimplemented count, which is therefore a few below the bar's; `progress.json` gives the number
(`unlisted_reimplemented`).

The picture has two views, switched above it. **One square per function** is the one that reads at a glance. Most
functions are tiny (half are under 125 bytes, the largest is 30 KB), so it gives a four-byte accessor the area of a
physics solver; the bars are in bytes. **One square per 512 bytes** gives a function one square per started 512
bytes (at least one), so the area is the code and a large reimplemented function shows as the bar it is. Both are
drawn the same way and carry the same tooltips.

Hovering a square shows the function's name, address, size and state, the original source file the source map places
it in, and, for a reimplemented function, the line of Coney's source that carries its `@orig` tag as a link; a click
or a tap keeps the tooltip so the link can be followed. The tooltip needs the script and the per-function table
(`progress/functions.json`, fetched on the first hover); without them the picture, the legend and every table still
show the same numbers. The mission picture below the milestones draws one square per checkpoint, a block per mission,
with the checklist's colours; a mission's label links to its page.

The same pictures and numbers are published as files beside the page, for other places to embed or read; the list
is in [the progress commands](../guides/coney-tools-references.md#progress).

### The history {#history}

[The history page](history.md) has one point per day of `main`: the Reimplemented, Understood and Researched shares
as the bars measure them. Git has the days, so nothing is appended or committed: for each day's last commit the docs
build extracts the tracker's inputs (`docs/progress/`, the research pages, the function database, the roadmap, the
bindings and the `@orig` tags) into a temporary folder and measures them with the current code, so every day is on
today's definition. A day before the tracker existed, or in a format it can no longer read, is left out. How a deploy
reuses the published days and what a pull request's build shows: [the progress commands](../guides/coney-tools-references.md#progress).

## Keeping it current

- Whoever writes the code records the port in the function database, `coney` on the function's entry, in the same
  commit as the code carrying its `@orig` tag ([Conventions](../guides/conventions.md#comments-and-orig)).
- A source-map change that moves a range or a coverage figure updates `totals.toml` in the same commit; a change to a
  file's range is made in `research/source-files.toml`, and `research render --check` holds each file inside its
  directory's range here and no range over another directory's file ([the source files](../guides/research-source-files.md#checks)).
- A roadmap status change needs nothing extra: the milestones are read from the roadmap's table.

Nothing generated is committed, so these changes never conflict with another pull request's. The tables and the
badge data are produced from the inputs by the documentation build (`mkdocs build --strict` shows them locally too),
and `uv run --project python coney-tools progress check`, which CI runs, fails when the database's `coney` entries and
the `@orig` tags in `src/` disagree or an input is malformed. `coney-tools progress render <folder>` writes the filled pages,
the badge files, the pictures and the numbers to a folder, to look at them without a docs build.
