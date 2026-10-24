# The coney-tools command line

`coney-tools` is Coney's own command line (see [Conventions](conventions.md#python)). Its commands are in groups, and
this page is the overview: how to run it and which page documents which group. Each group's page holds its commands,
options, exit codes and the rules it enforces, and every group has an anchor of its own, such as
`coney-tools-build.md#build`. `coney-agent` is a command of its own, not a group: the session launcher.

## Running it {#running}

Run the commands from inside the checkout:

```sh
uv run --project python coney-tools wad --help
```

## The groups {#groups}

### [The disc commands](coney-tools-disc.md)

These read **your own disc**; [naming the disc](coney-tools-disc.md#naming-the-disc) says how they find it.

* [`wad`](coney-tools-disc.md#wad): the game's archive.
* [`wad scenes`](coney-tools-disc.md#scenes): the scene records.
  {#scenes}
* [`extract`](coney-tools-disc.md#extract): every asset, in open formats.
  {#extract}
* [`audio`](coney-tools-disc.md#audio): the sound data.
  {#audio}
* [`movies`](coney-tools-disc.md#movies): the movies' headers.
  {#movies}
* [`xbox`](coney-tools-disc.md#xbox): the Xbox disc.
  {#xbox}

<!-- The five ids above keep the links that name coney-tools.md#<group> working in the legacy research pages that cite
them (audio, inventory, movies, scenes, xbox-assets). Touching a research page holds it to the whole research standard,
so they go, with these ids, once those pages are touched and cite the disc page. -->

### [The reference commands](coney-tools-references.md)

* [`progress`](coney-tools-references.md#progress): the progress tables.
* [`natives`](coney-tools-references.md#natives): the script-binding masterlist.
* [`refs`](coney-tools-references.md#refs): the game reference lists.
* [`missions`](coney-tools-references.md#missions): the mission status pages.

### [The fidelity and research commands](coney-tools-fidelity.md)

* [`orig`](coney-tools-fidelity.md#orig): the `@orig` index and audit.
* [`original`](coney-tools-fidelity.md#original): study and compare an original function.
* [`stamp`](coney-tools-fidelity.md#stamp): a unit's stamp log.
* [`research`](coney-tools-fidelity.md#research): the function database and the claims report.
* [`lua`](coney-tools-fidelity.md#lua): the game's scripts, decompiled.

### [The PCSX2 and clip commands](coney-tools-pcsx2.md)

* [`pcsx2`](coney-tools-pcsx2.md#pcsx2): drive PCSX2 and record the original.
* [`clips`](coney-tools-pcsx2.md#clips): in-game video clips.

### [The trace commands](coney-tools-trace.md)

* [`trace`](coney-tools-trace.md#trace): the original's traces and event logs, compared with Coney's.

### [The repository commands](coney-tools-repo.md)

* [`repo`](coney-tools-repo.md#repo): titles, descriptions, labels.
* [`pr`](coney-tools-repo.md#pr): commit messages.
* [`task`](coney-tools-repo.md#task): claiming issues and writing a task file.

### [The issue commands](coney-tools-issues.md)

* [`issue`](coney-tools-issues.md#issue): drafting, checking and creating issues.
* [`claim`](coney-tools-issues.md#claim): the issue claims.

### [The check, build and benchmark commands](coney-tools-build.md)

* [`check`](coney-tools-build.md#check): what CI would fail on.
* [`docs`](coney-tools-build.md#docs): the documentation standards.
* [`build`](coney-tools-build.md#build): configure, build and test under the build slots.
* [`perf`](coney-tools-build.md#perf): the engine benchmark.

### [The coordination and setup commands](coney-tools-setup.md)

* [`coord`](coney-tools-setup.md#coord): what waits for the coordinator.
* [`conclave`](coney-tools-setup.md#conclave): the read model of the Claude Code mod.
* [`mcp`](coney-tools-setup.md#mcp), [`ghidra`](coney-tools-setup.md#ghidra), [`doctor`](coney-tools-setup.md#doctor),
  [`workspace`](coney-tools-setup.md#workspace) and [`config`](coney-tools-setup.md#config): the debugging servers,
  your Ghidra project, the machine check, the local workspace and its configured paths.
* [`coney-agent`](coney-tools-setup.md#coney-agent): the session launcher.

## The parameter reference {#parameter-reference}

Every command is documented with all its parameters, and the parameters cannot drift from the code: they are read from
the command line's own definitions (the argparse parsers in `python/src/coney_tools/cli.py` and the modules it
registers) and written into the group's page. The last section of each group, **Parameters of `<group>`**, holds a
generated block with one entry per command: the usage line, then a table of every parameter with its type, default and
meaning. The text before it, in the group's own section, is written by hand: why the command exists, how to use it, its
exit codes and the rules it enforces. The two do not repeat each other, so a hand-written usage line that disagrees with
the table is the one to fix.

* **Never edit a generated block.** The meaning column is the parameter's `help=` text, and a command's heading
  sentence is its `help=` too: change the words in the parser, then run `coney-tools docs render`. A command or
  parameter without `help=` is an error, because its row would be empty.
* **A new command** in a group that has a block appears in it when you run `coney-tools docs render`; commit the page
  with the code. On a merge conflict inside a block take either side and run it again.
* **A new group** needs its two marker lines once, on the page of its cluster at the end of the group's section:

  ```text
  <!-- command-reference:begin <group> -->
  <!-- command-reference:end -->
  ```

  `docs render` fills it, and refuses a group that has no block, a group with two, a marker that names no group and a
  block that is out of date.
* **The gate.** `coney-tools docs render --check` changes nothing and exits 1 for any of those; it runs as the audit
  check `files.command-reference` (`repo audit --files`), which `check changed` and the `python` workflow run, so a
  pull request that adds or changes a command without rendering its page is refused.

Which group's page holds which block is where the block sits; the list above says where each group is documented. The
launcher `coney-agent` has a block too, on [its own section](coney-tools-setup.md#coney-agent).

## Adding a command {#adding}

A command is documented in the section of its group, on the page of the cluster that group belongs to, with an
explicit `{#id}` that code and other pages cite as `coney-tools-<page>.md#<id>`; its parameters come from its parser
([the parameter reference](#parameter-reference)), so give every argument a `help=`. A new group goes on the page whose
commands it works with, and is added to the list above. A page that nears the 800 lines a guide may have
([Writing these docs](writing-docs.md#touch-it)) is split along the groups it holds, as this one was, and the anchors
of the sections it moves are cited at their new page in the same pull request.
