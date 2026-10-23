# The coney-tools command line

`coney-tools` is Coney's own command line (see [Conventions](conventions.md#python)). Its commands are in groups,
and this page is the overview: how to run it and which page documents which group. Each group's page holds its
commands, options, exit codes and the rules it enforces, and every group has an anchor of its own, such as
`coney-tools-build.md#build`. `coney-agent` is a command of its own, not a group: the session launcher.

Run the commands from inside the checkout:

```sh
uv run --project python coney-tools wad --help
```

## The groups {#groups}

### [The disc commands](coney-tools-disc.md)

These read **your own disc**; [naming the disc](coney-tools-disc.md#naming-the-disc) says how they find it.

* [`wad`](coney-tools-disc.md#info) reads the game's archive, `WARRIORS.DIR` and `WARRIORS.WAD`: `info`, `list`,
  `names` and `extract`.
* [`wad scenes`](coney-tools-disc.md#scenes) parses the scene records.
  {#scenes}
* [`extract`](coney-tools-disc.md#extract) turns every asset of the disc into open formats.
  {#extract}
* [`audio`](coney-tools-disc.md#audio) reads the sound data.
  {#audio}
* [`movies`](coney-tools-disc.md#movies) reads the movies' headers.
  {#movies}
* [`xbox`](coney-tools-disc.md#xbox) reads the Xbox disc's archive.
  {#xbox}

<!-- The ids on the entries above keep the links that name coney-tools.md#<group> working in pages that cite them. -->

### [The reference commands](coney-tools-references.md)

* [`progress`](coney-tools-references.md#progress) keeps the progress tables of the README and the docs current.
* [`natives`](coney-tools-references.md#natives) renders the script-binding masterlist.
* [`refs`](coney-tools-references.md#refs) builds the game reference lists.
* [`lua`](coney-tools-references.md#lua) decompiles the game's compiled scripts for research.
* [`missions`](coney-tools-references.md#missions) builds the mission status pages.

### [The fidelity and research commands](coney-tools-fidelity.md)

* [`orig`](coney-tools-fidelity.md#orig) indexes the `@orig` tags for the fidelity audit.
* [`original`](coney-tools-fidelity.md#original) studies an original function and compares it with Coney's.
* [`stamp`](coney-tools-fidelity.md#stamp) verifies a unit's stamp log.
* [`research`](coney-tools-fidelity.md#research) keeps the function database and its generated lists, and reports
  the claims a branch changed on the research pages.

### [The PCSX2 commands](coney-tools-pcsx2.md)

* [`pcsx2`](coney-tools-pcsx2.md#pcsx2) and [`trace`](coney-tools-pcsx2.md#trace) record the original's per-update
  traces in PCSX2 and compare them with Coney's.
* [`clips`](coney-tools-pcsx2.md#clips) checks, publishes and cleans up in-game video clips.
* [`perf`](coney-tools-pcsx2.md#perf) benchmarks the engine.

### [The repository commands](coney-tools-repo.md)

* [`repo`](coney-tools-repo.md#repo) checks the repository's own rules (titles, descriptions, labels).
* [`pr`](coney-tools-repo.md#pr) checks commit messages.
* [`issue`](coney-tools-repo.md#issue) drafts, checks and creates issues.
* [`claim`](coney-tools-repo.md#claim) runs the issue claims.
* [`task`](coney-tools-repo.md#task) claims issues and writes a task file.

### [The check and build commands](coney-tools-build.md)

* [`check`](coney-tools-build.md#check) runs what CI would fail on for the files a change touched.
* [`docs`](coney-tools-build.md#docs) checks pages against the documentation standards.
* [`build`](coney-tools-build.md#build) configures, builds and tests the engine under the machine's build slots.

### [The coordination and setup commands](coney-tools-setup.md)

* [`coord`](coney-tools-setup.md#coord) shows what waits for the coordinator.
* [`mcp`](coney-tools-setup.md#mcp) starts the debugging servers for agent tools.
* [`doctor`](coney-tools-setup.md#doctor) checks a contributor's machine and disc.
* [`ghidra`](coney-tools-setup.md#ghidra) sets up your own Ghidra project with our function names.
* [`workspace`](coney-tools-setup.md#workspace) sets up the local workspace.
* [`coney-agent`](coney-tools-setup.md#coney-agent) is the session launcher.

## Adding a command {#adding}

A command is documented in the section of its group, on the page of the cluster that group belongs to, with an
explicit `{#id}` that code and other pages cite as `coney-tools-<page>.md#<id>`. A new group goes on the page whose
commands it works with, and is added to the list above. A page that nears the 800 lines a guide may have
([Writing these docs](writing-docs.md#touch-it)) is split along the groups it holds, as this one was, and the anchors
of the sections it moves are cited at their new page in the same pull request.
