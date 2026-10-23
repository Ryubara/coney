# The function database

The function database is the [research database](research-data.md#the-research-database)'s list of the original's
functions: what each is called, which research page describes it, how sure we are and where Coney ports it.

`research/functions/<system>.toml` lists the functions of one system of the original that we have researched, one
`[[function]]` each, in ascending address order. A system is a subject named as a research folder is: lower case with
hyphens (`combat`, `ai`, `hud`). A function is in one file only, the file of the system that owns it, so a function
has one entry whoever cites it. A file grows with its system and stays under the 512 KB limit by being split by
subject (`ai` into its goals and its brains), never by being excluded from the check.

## An entry {#entry}

```toml
[[function]]
address = 0x0026c548            # the entry point, 0x and eight digits as everywhere in the project
name = "Grab_Start"             # the name in the shared Ghidra project, exactly (docs/progress/ghidra-functions.tsv)
page = "combat/grabs.md#grab"   # below docs/research: the page, and the section that describes the function
kind = "start"                  # optional: a short tag (helper, init, binding, ...), as a table's Role column had
role = "plays the grab intro (anim 71) and decides miss, counter, front or rear grab"   # one line
level = "confirmed-code"        # optional: confirmed-code | confirmed-runtime | inferred | speculative | not-needed
note = "the anim id inferred"   # optional with a level (what it does not say), required without one
coney = "src/core/combat/grab.cpp:Grab::start"   # optional: where the port lives
status = "compared"             # optional, only with coney: legacy | compared

[[function.gaps]]               # optional, any number: what the port leaves out or does differently
what = "the counter roll uses a fixed chance"
relation = "stand-in"           # stand-in | not-ported
question = "combat/grabs.md#q-counter"   # an open question that tracks it, and/or
issue = 123                     # the issue that does
```

| Field | Needed | Meaning |
| --- | --- | --- |
| `address` | yes | The function's entry point in `SLUS_212.15`, written `0x` and eight hex digits |
| `name` | yes | The Ghidra name: letters, digits, `_`, and `::` for a member (`DVDWadIndex::Find`) |
| `page` | yes | `<page>.md` or `<page>.md#section`, below `docs/research`: the section describes the function and is the one to open first. The wrapper of a script binding names the generated binding reference instead, `../references/bindings/<group>.md#<name>` (relative to `docs/research`, the name in lower case); only the `scripting` system's entries do |
| `level` | no | How we know what the function does: the [evidence levels](research-evidence.md#evidence-levels), spelled as the YAML values, and `not-needed` for a function nobody needs (dead code, a debug stub). Leave it out when the page states no level for this function's own claim: an entry never claims a level the page does not give it |
| `role` | no; with `not-needed`, the reason | What the function does, in one line of our own words; Markdown, with links relative to `docs/research` |
| `kind` | no | A short tag for the sort of function |
| `note` | with no `level`; else no | The part of the claim the level does not cover (`confirmed (code); the rock inferred` becomes level `confirmed-code` with that text as the note), or, for an entry with no level, why it has none and where to read about the function; Markdown, links relative to `docs/research` like the role's |
| `coney` | no | The source file of the port and, after a colon, its function or class |
| `status` | no | How far the port has been checked against the original: `legacy` (ported before the side-by-side comparison) or `compared`. Only a function with `coney` has one |
| `gaps` | no | Each piece the port leaves out or does differently: `what`, a `relation` (`stand-in`: on purpose, the reason on the open question; `not-ported`: waits for the issue) and an `issue` number, a `question` (`page.md#q-...`), or both |

Keep to what the file is for. A function's logic, its branches and its constants belong in the page's Behaviour
section, which the entry links to; the entry is the row of a map, and its `role` the sentence a reader needs to decide
whether to open that section. Pseudocode, decompiler output and game data never go in the file
([LEGAL.md](repo:LEGAL.md#research-limits)); the check refuses the marks of decompiler output in the text fields.

## What the tools check {#checks}

`coney-tools research render --check` ([coney-tools](coney-tools-fidelity.md#research)) runs in CI
(`python`) and in the docs build, and fails on:

- a file that breaks the schema above: an unknown key, a wrong type, a level, relation or status not in its list,
  an entry with no `level` and no `note`, `status` without `coney`, a gap with no issue and no question, addresses
  out of order or twice;
- decompiler output in `role`, `kind`, `note` or a gap's `what`: the marks `orig copy-check` refuses in C++ (Ghidra's
  made-up names such as `iVar1`, `param_2`, `DAT_003a1f20` or `FUN_00123456`, its `undefined4` types, a raw read at
  a byte offset), as on a research page: cite an unnamed function by its address;
- an address in two systems;
- an address that is not a function entry in the committed Ghidra listing (`docs/progress/ghidra-functions.tsv`), or a
  name that is not the one Ghidra has; for a ported entry (`coney`) an address in a gap between listed functions is a warning
  ([below](#ported)), and one inside a listed function an error;
- a `page` that is not a file, or a section that is not an anchor of it (for a binding page, the file under
  `docs/references/bindings` and the binding's name as its anchor), and a gap's question that does not resolve;
- a `coney` file that is not in the checkout;
- an entry whose page does not own its address, once that page has front matter (with or without `owns`), or whose
  address another page owns ([one owner per address](writing-docs.md#research-pages), the most specific `owns` entry
  winning); `docs check` reports the same on the page, as rule `function-entries`;
- a hand-committed page where the build writes a system's list.

A name for which the listing still has Ghidra's default is a warning: rename it in Ghidra and refresh the listing ([the
Understood measure](research-workflow.md#understood)). The fix for a name that differs is the entry, or the listing when
Ghidra was renamed since; never a second name.

## Writing an entry {#writing}

When you study a function, add or correct its entry in the file of the system that owns it, in the
same commit as the page section it points at, and keep the file in address order. The tools write the file in one format
(`research import-table` rewrites it), so a comment of your own belongs in `role` or `note`; the comment lines at the
top of the file are kept.

## Importing a page's code index {#importing}

A research page that still holds its code index as a table of
`Address | Name | Role | Evidence` moves into the database with one command, then a review:

```sh
uv run --project python coney-tools research import-table docs/research/tasks.md --system tasks
```

It reads every table with an Address column, a Name (or Function) column and an Evidence column, plus a Role, What it
does, Does, Description or Purpose column (when a table has both Role and a longer one, Role becomes the entry's
`kind`); other tables are left alone, and a table of functions with no Evidence column, or with a column it does not
know, is reported and skipped. `--section ANCHOR` limits it to the tables under that heading. It turns each row into an
entry: the section is the heading above the table, the level is read from the Evidence cell, and links in the role are
rewritten to be relative to `docs/research`. It prints what it did and what to check:

- rows it could not read, with the page line and the reason;
- names it shortened (a signature or a tag) and addresses it padded to eight digits;
- rows that grade more than one thing: the first level became the entry's and the whole cell its note;
- rows that listed several functions at once, split with the row's role copied to each;
- what `research render --check` would say about the file afterwards, which is where a page's name that differs from
  Ghidra's shows.

The file is rewritten in its one canonical format with its leading comment lines kept. A second run adds new rows,
leaves an entry that differs from the page as it is (`--replace` takes the page's row and keeps the entry's `coney`,
`status` and `gaps`; an entry that differs only in those three is the same), and leaves out a function another system
owns. After the review, cut the table from the page and keep, under Map, only the entry points a reader opens first,
with a link to the generated list (the Map section of [the page standard](writing-docs.md#sections)).

## The generated list {#generated-list}

For each system file the docs build writes `research/<system>/functions.md`: the functions in
address order with their name, role, level and a link to the section that describes each, a column for the kind and for
the port only when some entry has one, and a table of the gaps. The pages are never committed (a committed one is an
error), like the progress tables. `coney-tools research render <folder>` writes them to a folder to look at without a
docs build. Each row has an anchor, `functions.md#0x0026c548`, for a page that wants to link one function.

## Recording a port {#ported}

The entry is also where Coney says it ports the function. Whoever tags a function `@orig 0x... Name` adds, in the same
commit, to the entry of that function (or a new entry, when the function has none: a page and a level are needed
first, which is the [research](research-workflow.md#understood) the port was made from; the level can wait, the page
cannot):

- `coney`, the file that carries the tag and, after a colon, the function or class: `src/core/combat/grab.cpp:Grab::start`.
  A function tagged in several files names one of them;
- `status`, `compared` once the port has been checked side by side with the original (`orig review`), else `legacy`;
- a `[[function.gaps]]` for each piece the port leaves out or does differently.

`coney-tools progress check` holds the two together (CI runs it): every `@orig` tag must have an entry with `coney`,
and every entry with `coney` a tag, in the file `coney` names; an address outside game code (middleware, outside
`.text`) is refused. The tracker counts the entries with `coney` as the
functions Coney reimplements ([the progress commands](coney-tools-references.md#progress)); an entry without it is a
researched function. Nothing else is written for the tracker: the entry's subsystem is the one whose range in
`docs/progress/totals.toml` holds the address, and its size is the function's size in the Ghidra listing, so the
reimplemented bytes are measured in the same unit as the Understood measure. A function the listing has no function at
yet (the port is tagged at an address in a gap between the functions the shared Ghidra project defines) counts with no
size and is a warning of `research render --check`: define the function in Ghidra and refresh the listing. An address
inside a listed function is an error, a wrong tag address. Without the listing file the sizes are missing for want of
it, which `progress show` says.

The name is Ghidra's. A tag that spells it differently fails `progress check` when the branch adds the tag, and is a
warning with a count when it was already there when the branch left `origin/main` (the way `orig check` holds only
what the branch changed; CI names the base and insists on it, `progress check --base HEAD^1 --require-base`). Write
new tags with the name the entry has.

The reimplemented list used to be `docs/progress/functions.toml` (address, name, subsystem, size). The move into this
database is `coney-tools progress import-functions`, kept for a branch that still adds rows to the old file
([the progress commands](coney-tools-references.md#progress)). An entry it made from prose is a starting point.
It names the section that describes the function as its page (a table row of its own, then its script binding's
reference, then a section that opens with the function, then any section that cites it; never a Coney or open-questions
section while another exists, nor another function's row), and gives a level only when it can tie one to that
function's own claim: a page-table row's Evidence cell, a sentence that reads its code and is about it alone, a level
that stands right after its address (`(F, 0x1, confirmed (code))`), one that lists it (`Confirmed (code) at 0x1, 0x2`)
or closes the statements before it, a lead line that grades the blocks under its heading. A section that describes the
function but states no level takes the level another page states for it, and its note says where from; a function the
research grades two ways keeps one row and its note says where the other is. Anything else has no level and a note
saying that the import tied none, not that the page has none: the page may state a level in a form the tool does not
read. When you study such a function, grade it: check its page, write its level and role and drop the note.

## Other readers of the file {#other-readers}

The Understood measure counts an entry with a `role` and a `level` as a citation with evidence ([the Understood
measure](research-workflow.md#understood)); without a role the entry says nothing of what the function does, and
without a level nothing says how we know. `research render` lists an entry with no level as "no level" and counts
them in its summary, and `research where` prints an entry's page, level (`null` in its JSON when there is none) and
port. The page-ownership check (`function-entries`) skips the entries of binding wrappers, whose page is not a
research page. The progress tracker reads the `coney` field ([above](#ported)).
