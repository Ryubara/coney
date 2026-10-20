# Progress

How far Coney has come, measured three ways:

- **Reimplemented:** the share of the original game's own code that Coney's code reimplements, in bytes of the
  executable's `.text`. This is the decompilation projects' progress bar, adapted to a functional reimplementation:
  a function counts once Coney has code that does its job and cites it with an `@orig` tag
  ([Conventions](../guides/conventions.md#comments-and-orig)), not when its machine code matches.
- **Researched:** the share of the game's own code that the [source map](../research/source-map.md) places in at
  least an original source file or directory. It says how much of the executable an engineer can already find their
  way around in, not how much is understood.
- **Understood:** the share of the game's own functions (by bytes, and by count) that are researched in full: the
  function has a meaningful name in the shared Ghidra project (not `FUN_<address>`), and a research page cites its
  address in a table row or paragraph that states an evidence level, or says `not needed: <reason>` (the source
  map's citations do not count: placing is not understanding); a script binding's wrapper counts when its
  `research/bindings` entry has an `evidence`. Names and sizes come from `docs/progress/ghidra-functions.tsv`
  (addresses, sizes and our names only), the citations from the pages themselves, so citing a function moves the
  bar in the same commit. How to refresh it: [Research workflow](../guides/research-workflow.md#understood).
- **Milestones:** the status table of the [roadmap](../roadmap.md).
- **Missions:** the [mission checklist](../missions/index.md): each story level's status, from Not Started to
  Approved by the user's play-test, with its checkpoints and the script bindings it needs.

The tables below are not in the repository: they are generated each time the documentation is built, so this page
always shows the numbers of `main`, and the badges at the top of the repository's `README.md` read the same figures
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

**Reimplemented bytes** are the sizes of the original functions listed in
[`docs/progress/functions.toml`](repo:docs/progress/functions.toml). Only the
functions Coney reimplements are listed, never the executable's whole function list. The denominators are in
[`docs/progress/totals.toml`](repo:docs/progress/totals.toml), copied from the
source map by hand. A function counts in full once it is listed; there is no partial credit.

The executable has no symbols, so a function's size has to come from somewhere. `coney-tools progress sizes`
estimates it from your own disc as the distance to the next address that a call or a function pointer reaches, and
checks a size given by hand against that bound. It is an upper bound: a function that nothing calls directly (only
a tail jump reaches it) is folded into the one before it. An engineer who has the function in Ghidra can give the
exact size instead. How to use the commands: [The coney-tools command line](../guides/coney-tools.md#progress).

## Keeping it current

- An engineer adds a `[[function]]` block to `functions.toml` in the same commit as the code carrying its `@orig`
  tag ([Conventions](../guides/conventions.md#comments-and-orig)).
- A source-map change that moves a range or a coverage figure updates `totals.toml` in the same commit.
- A roadmap status change needs nothing extra: the milestones are read from the roadmap's table.

Nothing generated is committed, so these changes never conflict with another pull request's. The tables and the
badge data are produced from the inputs by the documentation build (`mkdocs build --strict` shows them locally too),
and `uv run --project python coney-tools progress check`, which CI runs, fails when `functions.toml` and the `@orig`
tags in `src/` disagree or an input is malformed. `coney-tools progress render <folder>` writes the filled page and
the badge files to a folder, to look at them without a docs build.
