# Finding where a behaviour lives

Before you study a function, find out whether a page already describes it. `coney-tools research where` answers for an
address, a Ghidra function name, a source file or a word a player uses, from the same data the checks read, so the
answer is never older than the checkout. `orig study` prints the same lines under its own summary, so a study
starts from the page that owns the function.

```sh
uv run --project python coney-tools research where 0x0026c548
uv run --project python coney-tools research where Grab_Start
uv run --project python coney-tools research where Human/cns/cnsplayertag.cpp
uv run --project python coney-tools research where stamina
uv run --project python coney-tools research where 0x0026c548 --json
```

## Who owns an address {#owners}

A research page of a kind that owns code says what it owns in its front matter
([research pages](writing-docs.md#research-pages)): source files by the path the executable names them with
([the source files](research-source-files.md)), address ranges and single functions. An address has one owning page.
When several entries hold it, the most specific one wins:

1. a single function a page lists in `owns.functions`;
2. otherwise the narrowest range or source file whose address range holds it. A file counts by its range in
   `research/source-files.toml`, so a page that owns `Human/cns/cnsplayertag.cpp` owns every function the map
   attributes to it unless another page takes some out with a smaller range or a function.

Two pages that own the same thing (a file and a range of the same code count), or two ranges or a file and a range
that cross, never reach the lookup: `docs check` refuses them (the `owns` rule in
[writing-docs](writing-docs.md#checking)). The code is `research_owners.py`; the function database check, the find
pages and `research where` all call it, so they cannot disagree.

## What `where` answers {#forms}

The query decides what is looked up; the output is plain text, or one JSON document with `--json`.

| Query | Looked up as | Output |
| --- | --- | --- |
| `0x00...` | An address anywhere in the executable | The function that holds it (with the offset when the address is inside), the section when it is no function, the source file whose range holds it, the owning page and the claim that gives it, the function database entry, the `@orig` tags in `src/`, the other pages that cite it and the open questions that name it |
| A Ghidra name | A function, by exact name, then ignoring case | The same report for each function of that name; names that only contain the query follow, up to fifteen |
| A path with `/`, or ending `.cpp`, `.inl`, `.h` | A source file, by its full path or the end of it | Its attributed range and function count, marked inferred, and the owning page of the whole file |
| Anything else | A word | The glossary term (its page, functions and other names), else the pages with a heading that holds the word |

The report says which entry makes a page the owning page (`lists function 0x0026c548`, `lists range 0x...-0x...` or
`lists file Human/cns/cnsplayertag.cpp`), so a disagreement is fixed in that page's front matter. A source file's range
is inferred ([the source map's method](../research/source-map.md#method)); the report says so.

Exit codes: 0 when something matched, 1 when nothing did (with a line saying what a query can be), 2 when the
arguments are wrong, the query is empty, a query like `0xzz` is no address, or a data file is malformed (the message
names the file). `--json` prints the same facts as the text under the keys `query`, `addresses`, `files`, `words` and
`similar`. An address lists `function`, `name`, `size`, `offset`, `listed` (whether the Ghidra listing was there),
`section` (the executable's section that holds the address, whether or not a function does; empty outside the image),
`code` (true when that section is the code, `.text`, so an address inside a function there is `true`; false in a data
section and outside the image), `source_file`, `owner` (`page`, `via`, `what`, or null), `database` (with `coney` and `status`),
`orig`, `cited` and `questions`.

## The find pages {#find-pages}

The docs build makes two pages from the same data, so a reader without a terminal can use them. Neither is committed;
`coney-tools research render <folder>` writes them to a folder to look at
([coney-tools](coney-tools-fidelity.md#research)).

- [Where each address is described](../research/find/addresses.md): the pages that own code with what each lists and how
  many functions it holds, then the executable in address order as runs of consecutive functions with one owning page.
  A run no page owns is listed too, so the gaps are visible. It needs the Ghidra listing and is left out without it.
- [Player words](../research/find/words.md): the glossary as a table. It exists once `research/glossary.yaml` does.

The source files' tables stay on the [source map](../research/source-map.md#files-by-subsystem), where
other pages already link to them (`source-map.md#gui` and the like). When a page owns a whole file, the row gains a
Page column that links it.

## The glossary {#words}

A player says "sprint", "stamina" or "mount"; the original's code calls the same things by other names. A study that
finds where a player-facing behaviour lives adds a word to `research/glossary.yaml`, a list of entries:

```yaml
- term: mount                          # the word a player uses
  page: combat/grabs.md#mount          # a page below docs/research, with an optional section
  functions: [0x0026f008, 0x0027ec20]  # entry addresses of the functions where its logic lives
  also: [pin, ground and pound]        # other words for the same thing
```

Only `term` and `page` are needed. `research render --check` fails on a key it does not know, a word that two entries
both claim, a page that does not exist, a section the page does not have, and a function that is no entry of the Ghidra
listing. A word is added with the study that proves the link, not as a guess: the entry cites the page where the
evidence is. `coney-tools research where WORD` and the words page read it; so does the
[task leads index](coney-tools-repo.md#task).

## In a study {#study}

`orig study ADDR` ends with the lookup of the function's address:

```text
research (coney-tools research where):
  owning page: docs/research/combat/grabs.md (lists function 0x0026c548)
  function database: combat.toml: confirmed-code, described at combat/grabs.md#grab
  also cited on: docs/research/tasks.md:310 (#grab-task)
```

Read the owning page before the decompile: it may already say what the function does and what is still open. A function
no page owns says `owning page: none`; the study's write-up then goes on the page that should own it, and that page
adds the function (or its file) to its `owns`. A malformed data file does not fail the study: the lookup prints one
line saying it is not available, and `research render --check` says what is wrong.
