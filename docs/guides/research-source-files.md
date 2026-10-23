# The source files of the executable

The executable has no symbols, but its read-only data holds the paths of the source files it was built from, and the
[source map](../research/source-map.md) works out which stretch of `.text` came from which file. That knowledge is
data in `research/source-files.toml`, one `[[file]]` for each file, in the order the source map's tables had. Tools
read the file directly: the lookup of which research page describes an address, the progress picture and the check of
a page's `owns`. The tables a person reads on the source map are made from it by the docs build, so the two cannot
drift. This is the [research database](research-workflow.md#the-research-database)'s rule applied to the map: a fact
a tool must find lives in a file whose entries all have the same fields.

## A file {#file}

```toml
[[file]]
directory = "Camera"                       # the source directory whose table the file is in
path = "Camera/Cam_Mug.cpp"                # the file as the executable names it below c:/Warriors/Source/
start = 0x00138078                         # the attributed range [start, end): inferred
end = 0x00139d48
functions = 3                              # functions the map attributes to the file
stub = 0x00139d48                          # optional: the static-initialiser stub that probably ends its unit
anchors = [                                # functions that load the file's path string: confirmed (code)
  {address = 0x00138078, tags = ["TPhysicsBoundSphere"]},
  {address = 0x00138b90, tags = ["TPhysicsBoundSphere", "vags/misc/mug_outro"]},
]
more_anchors = 0                           # optional: anchors beyond the ones listed
```

A file the map cannot place has no range: it gives `path_string`, where its path string sits in `.rodata`, and no
functions, anchors or stub. A unit with no path string that the map places by its callers (`World/(no path string)`)
is a file whose `note` says why. The writer is the one format of the file (`research/source-files.toml` is written
by `coney_tools.source_files.dumps`), so a hand edit keeps the order, one blank line between files and the field order
above.

| Field | Needed | Meaning |
| --- | --- | --- |
| `directory` | yes | The directory heading of the file's table; the first part of `path` |
| `path` | yes | The file's path, directory first. Two files never share a path (case apart) |
| `start`, `end` | both or neither | The attributed range, `0x` and eight hex digits; `end` is the first byte after it. No two ranges overlap |
| `path_string` | without a range | Where the path string sits, as an address |
| `functions` | yes | The number of functions the map attributes to the file: at least 1 with a range, 0 without |
| `stub` | no | The stub at or after the file's start that probably ends its unit |
| `anchors` | no | Each anchor's `address` (inside the range) and the allocation `tags` it passes, mostly class names |
| `more_anchors` | no | How many anchors there are beyond the listed ones (the tables list up to three) |
| `note` | no | One line of what the fields do not say; the table shows it in place of the anchors |

The ranges are inferred, from the link order and the static-initialiser stubs; an anchor is confirmed (code) at its
address. How the map is made, and what each level covers, is the source map's
[method](../research/source-map.md#method).

## What the tools check {#checks}

`coney-tools research render --check` ([coney-tools](coney-tools.md#research)) runs in CI and in the docs build and
fails on:

- a file that breaks the schema above: an unknown key, a wrong type, a path outside its directory, a range that ends
  before it starts, an anchor outside the range, two files with the same path or overlapping ranges;
- a file whose range lies outside its directory's range in [`docs/progress/totals.toml`](repo:docs/progress/totals.toml)
  and in every middleware range: the file data and the progress tracker's denominators cannot drift apart;
- a source map page that does not have its `<!-- source-files:generated -->` line exactly once.

`totals.toml` stays a hand-kept file. Its subsystem ranges reach from a directory's first attributed function to its
last and are widened by the stretches the map places by position and calls, and its coverage bytes come from the whole
map; no list of file ranges gives either. The check above is what holds the two together.

## The generated tables {#generated-tables}

The source map's "Files by subsystem" section ends in a marker line; the docs build replaces it with one table per
directory, each heading with the id it had before the tables were data, so a link to `source-map.md#gui` still lands
on the GUI files. `coney-tools research render <folder>` writes the page as the build shows it, to
`<folder>/research/source-map.md`. Nothing generated is committed.

Once a research page owns a whole file (its front matter lists the file, or a range or function set that holds all
of it), the file's row gains a Page column that links the page; until then the column is not there. The same lookup
answers `coney-tools research where Human/cns/cnsplayertag.cpp` ([Finding where a behaviour lives](research-where.md)).

## Changing a row {#changing}

When you place a function in a file, or find a range's real end, change the row in `research/source-files.toml`, keep
the order, and run `research render --check` and `progress check`. A file's `path` is the name a research page lists in
its front matter `owns.files`.
