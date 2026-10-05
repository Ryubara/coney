# The coney-tools command line

`coney-tools` is Coney's own command line (see [Conventions](conventions.md#python)). This page covers seven groups:
`wad`, which reads the game's archive, `WARRIORS.DIR` and `WARRIORS.WAD`, from **your own disc**, [`xbox`](#xbox), which
reads the Xbox disc's archive, [`progress`](#progress), which keeps the progress tables of the README and the docs
current, [`natives`](#natives), which renders the script-binding masterlist, [`refs`](#refs), which builds the
game reference lists, and [`pcsx2`](#pcsx2) and [`trace`](#trace), which record the original's per-update traces in
PCSX2 and compare them with Coney's. Everything is read in place and streamed, so the 1.4 GB WAD is never loaded into
memory. The format is described in [WARRIORS.DIR / .WAD](../research/formats/wad-dir.md).

Run the commands from inside the checkout:

```sh
uv run --project python coney-tools wad --help
```

## Naming the disc

Every `wad` command takes the disc as an optional first argument, `DISC`:

* a **folder**: a mounted disc (`H:\` on Windows, `/mnt/disc` on Linux) or any folder that holds `WARRIORS.DIR`,
  `WARRIORS.WAD` and, for `names`, `SLUS_212.15`;
* an **`.iso` image** (ISO 9660, 2048-byte sectors). Coney reads it with its own small reader.

Without `DISC`, the tools use `game_dir` from your `coney.local.toml` (see
[Local workspace](workspace.md)). Problems print one line, `coney-tools: <what is wrong>`, and exit with code 2.

## info

```sh
uv run --project python coney-tools wad info [DISC] [--names FILE]
```

Prints the entry count, the total size of the entries and of the WAD, a count of entries by their first four bytes
(a rough content breakdown; printable ones show as text) and how many entries a names file resolves.

## list

```sh
uv run --project python coney-tools wad list /path/to/WARRIORS.iso --names ../../scratch/names.txt
```

One line per entry: index, offset, size, the hash as 8 hex digits and the name when the names file knows it.

## names

```sh
uv run --project python coney-tools wad names [DISC] ../../scratch/names.txt
```

Recovers names: it takes file-name-like strings from `SLUS_212.15` and from every WAD entry, hashes each as
`./ee_files/<name>` (CRC-32 of the lowercased text, [Name hashing](../research/name-hash.md)) and keeps those that
match an entry. It writes one name per line to the output file and prints how many it found. It takes a few
minutes because it reads the whole WAD. Whether a names list may be published is an open legal question, so the
tool always recovers the names from your disc and the list is never committed.

## extract

```sh
uv run --project python coney-tools wad extract [DISC] ../../scratch/wad [--names FILE] [--only HASH_OR_NAME ...]
```

Writes entries into the output folder. A file is named by its recovered name, otherwise `<hash>.bin`. `--only`
takes entry hashes (8 hex digits) or names (`global.lua`), and without it every entry is written, about 1.4 GB.

!!! warning "Game data stays out of the checkout"
    `extract` and `names` refuse an output path inside the repository, because extracted files are game data and
    must never be committed ([LEGAL.md](repo:LEGAL.md#no-game-data)). Use a
    folder beside the checkout, such as `../../scratch/`.

## xbox {#xbox}

The Xbox version is an optional asset source ([Xbox assets](../research/xbox-assets.md)). The `xbox` commands read
its archive, `XBoxWad.idx` over eight volume files, from **your own Xbox disc** and print counts, offsets and hashes,
so the survey on that page can be repeated. `DISC` is required and is one of:

* a **full disc image** (Redump style, 7.8 GB): the game partition is found by its `MICROSOFT*XBOX*MEDIA` volume
  descriptor at byte `0x18300000` (XGD1) or `0x1FB20000` (XGD2);
* an **XISO**, an image of just the game partition;
* an **extracted folder** holding `XBoxWad.idx`, the `*.wad<N>` volumes and, for `names`, `default.xbe`.

The file system (XDVDFS) is read with Coney's own small reader and every file is streamed. Problems print one line
and exit with code 2, as for `wad`.

| Command | What it prints |
| --- | --- |
| `xbox files DISC` | Every file with its size, then the file, folder and byte totals |
| `xbox info DISC [--names FILE]` | The entry count, entries and bytes per volume, entries and bytes per kind (pack, texture dictionary, model, scene record, streamed world, ...) and how many entries a names file resolves |
| `xbox list DISC [--names FILE]` | One line per entry: index, volume, offset, size, hash, and the name when known |
| `xbox names DISC OUT_FILE [--candidates FILE ...]` | Matches names against the index and writes them, one path below `ee_files\` per line; prints how many entries stay unnamed, by kind |
| `xbox extract DISC OUT_DIR [--names FILE] [--only HASH_OR_NAME ...]` | Writes entries as files: a named one under its path (`paks/global.pak`), the rest as `<hash>.bin` |
| `xbox resources DISC [--ps2 PS2_DISC]` | The resource index: packs, resources, chunks, distinct `(resource hash, chunk type)` keys, and the graphics chunks by their first two words; with `--ps2`, how many of the PS2's texture, model, animation and character resource hashes the Xbox has |
| `xbox textures DISC [--ps2 PS2_DISC]` | Texture chunks by format (DXT1, DXT2/3, DXT4/5), mip count and size; with `--ps2`, the texture resources both discs share and how many hold the same number of textures |

A names file has one name per line and the tools take the **last word** of each line, so the output of `wad names`,
`xbox names`, `wad list` and `xbox list` all work. A bare name (`global.pak`, as from the PS2) is tried in each of the
Xbox folders `ee_files\`, `ee_files\paks\` and `ee_files\anims\`; a name with a folder as given.

`names` keeps every name from `--candidates` and every scene record's own name (plus `.scn`) that matches. It also
guesses: `level<N>.lev`, every file-name-like string in `default.xbe` and in the Lua and text entries, and every
identifier there plus `.lev`. A guess is kept only when its extension fits the kind of the entry it matches (`.lev`
on a level, `.pak` on a pack, ...), which rejects chance matches. With the PS2 names as candidates it gives the
4,038 names on the research page:

```sh
uv run --project python coney-tools wad names PS2_DISC ../../scratch/names.txt
uv run --project python coney-tools xbox names XBOX_DISC ../../scratch/xbox-names.txt --candidates ../../scratch/names.txt
```

`--ps2` takes the PS2 disc as the `wad` commands do and reads its `WARRIORS.WAD`. `resources` and `textures` read
the whole Xbox archive (1.7 GB) and, with `--ps2`, the whole PS2 one.

!!! warning "Game data stays out of the checkout"
    `xbox extract` and `xbox names` refuse an output path inside the repository, as `wad extract` and `wad names` do.

## progress {#progress}

The [Progress](../progress/index.md) page and the Progress section of `README.md` are generated from three inputs:
`docs/progress/functions.toml` (the original functions Coney reimplements), `docs/progress/totals.toml` (the
denominators, copied from the [source map](../research/source-map.md)) and the status table of the
[roadmap](../roadmap.md). The page says what the numbers mean; this section says how to run the commands.

```sh
uv run --project python coney-tools progress show [--json]
```

Prints the reimplemented and researched shares, the milestones and the subsystems that have reimplemented code.
`--json` prints everything, per subsystem and per coverage category, for other tools. It exits with 1 when
`functions.toml` and the `@orig` tags in `src/` disagree, and lists each problem.

```sh
uv run --project python coney-tools progress update [--check]
```

Rewrites the generated block between `<!-- progress:start -->` and `<!-- progress:end -->` in `README.md` and in
`docs/progress/index.md`; the text outside the markers is never touched. Run it after changing any of the inputs and
commit the result with the change. With `--check` it writes nothing and exits with 1 when a block is stale; CI runs
it that way. It refuses to write while the inputs disagree:

* an `@orig` tag in `src/` with no `[[function]]` in `functions.toml`, or the other way round, or the two naming the
  function differently;
* a `subsystem` that is not the one whose range in `totals.toml` holds the address (the message names the right
  one; outside every range it is `unattributed`);
* an address inside middleware or outside `.text`, an address listed twice, or sizes that overlap.

```sh
uv run --project python coney-tools progress sizes [DISC] [--fill]
```

Checks the size of every function in `functions.toml` against the executable `SLUS_212.15` on your own disc (`DISC`
works as for the `wad` commands). The executable has no symbols, so the command takes every `jal` target, every
function pointer in `.data` and every address into `.text` that code builds with a `lui` / `addiu` pair (the function
pointers handed over in code, such as the script binding wrappers) as the start of a function, and a function's
**span** runs to the next of those starts, alignment padding included. Before the pairs were counted, a binding
wrapper's span ran over its unseen neighbours (38,440 bytes for `HuCreate`'s 480). A given size larger than the
span is an error (exit code 1); one smaller than the code before the padding, or an address no call reaches, is a
warning. `--fill` writes the span into each entry that has no
`size` yet. It prints only the addresses and sizes of the functions already listed.

The span is an upper bound: a function that nothing calls directly and no pointer names (one reached
only by a tail jump) is not seen as a start, and the span of the function before it includes it. When `--fill`
gives a size that looks too large for what the research page describes, ask an analyst for the size Ghidra shows.

## natives {#natives}

The script-binding reference (`docs/references/bindings/`, under Game references) is generated from
`research/bindings/<category>.yaml` (schema: [Research workflow](research-workflow.md#bindings)).

```sh
uv run --project python coney-tools natives render [--check]
```

Checks the YAML and writes `docs/references/bindings/`: `index.md` and one page per category, each binding under an
anchor of its name in lower case (`hud.md#hudsetobjective`). A page left over from a removed category is deleted. Run it
after editing the YAML and commit the pages with the change. With `--check` it writes nothing and exits with 1 when a
page is stale; CI runs it that way. Either way it refuses to go on while the YAML has problems, and lists them: an
unknown key, a file that is not a category, a missing description on a described entry, a thorough entry with an
argument left unexplained, an argument name used twice, usage counts that contradict each other, a name listed twice.

```sh
uv run --project python coney-tools natives coney [--check]
```

Sets the `coney` key of every entry from Coney's binding table, `kBindings` in `src/scripting/script_bindings.cpp`:
`real` is *implemented*, `routed` (handed to a stand-in for a subsystem Coney lacks) is *partial*, `stub` and
`recording` are *not implemented*, and so is a binding the table does not list; the default is written as no key. Run
it, then `natives render`, after changing the table. It fails when a line of the table is not one `kind("Name", ...)`
entry, or names a binding the masterlist lacks. With `--check` it writes nothing and exits with 1 when a status is
stale; CI runs it that way.

```sh
uv run --project python coney-tools natives cpp [--check]
```

Writes `src/debug/native_signatures.cpp`, the C++ table the [debug menus](debug-menu.md#natives) build their argument
editors from: every binding's name, category, the type of each argument (number, integer, handle, boolean, string, a
table of numbers or strings with its fixed count, userdata) with its default, and the types of its results. A number
argument whose description says it is a handle gets the handle editor. Only names, types and defaults go in, never a
description. Run it after editing the YAML and commit the file with the change; with `--check` it writes nothing and
exits with 1 when the file is stale, as CI runs it.

```sh
uv run --project python coney-tools natives stats
```

Prints the counts by category, evidence level, detail and Coney status, and how many bindings the game's scripts
call.

## refs {#refs}

The [Game references](../references/index.md) are generated from the lists in `research/references/`
([Research workflow](research-workflow.md#reference-lists) has their schema and rules).

```sh
uv run --project python coney-tools refs extract [DISC] [--only LIST ...] [--names FILE]
```

Reads your own disc (`DISC` works as for the `wad` commands) and refreshes every list, or only the ones named
(`characters`, `levels`, ...): the configuration calls of the compiled scripts, the Character List of
`warriors.glr`, every animation clip, two tables of the executable and the WAD's entry names. It keeps every
hand-written field and every entry it did not produce, then renders the pages. `--names` adds WAD names found
another way (a file with one name per line, or the last word of each line, as `wad names` writes), each kept only
when it hashes to an entry; the names already in `wad-names.yaml` are always used. A full run takes about half a
minute and keeps only names, ids and numbers.

```sh
uv run --project python coney-tools refs render [--check]
```

Checks every list against its schema and writes `docs/references/index.md` and one page per list. With `--check` it
writes nothing and exits with 1 when a page is out of date; CI runs it that way. A list that fails its schema stops
both, with each problem named (exit code 2).

```sh
uv run --project python coney-tools refs compress-images [FOLDER]
```

Rewrites every PNG below `docs/references/images/` (or `FOLDER`) in place as a 256-colour palette image with
transparency. `coney --render-references` writes full-colour images of about 26 KB each; with a palette they take
under a quarter of that and look the same at 256 pixels. The quantizer is deterministic, so a re-render gives the same
files. Run it after rendering and before `refs extract` links the images.

## pcsx2 {#pcsx2}

The `pcsx2` commands drive the original in PCSX2 over PINE, for runtime research and parity checks. What they do and
why, the scenario format and the address expressions are in [Recording a trace](research-workflow.md#recording-a-trace).
The folders come from `coney.local.toml` (`pcsx2_dir`, `game_dir`, `scratch_dir`) unless `--pcsx2-dir`, `--iso` and
`--scratch` give them; PINE must be enabled in PCSX2's `inis/PCSX2.ini`. Problems print one line and exit with code 2.

```sh
uv run --project python coney-tools pcsx2 prepare-state SOURCE OUT [--patch NAME ...]
```

Copies a save state (a `.p2s` file, or `slot:N` for quick-save slot N, which is only read) to `OUT` with the named
patches of `research/traces/patches.toml` applied to its EE memory. It refuses an `OUT` inside the repository or in
PCSX2's `sstates/` folder, and refuses the copy when the state does not hold a patch's original instruction words.
PCSX2 saves states with zstd, which Python reads from 3.14 (uv installs it).

```sh
uv run --project python coney-tools pcsx2 launch STATE
```

Starts PCSX2 on a state file (`-fastboot -statefile`, the disc through a hard link in the scratch folder when its path
holds commas or parentheses), waits until the game runs, and leaves it running. It refuses to start when something
already serves PINE.

```sh
uv run --project python coney-tools pcsx2 record SCENARIO --out CSV [--state SOURCE] [--attach] [--keep-open]
```

Makes the scenario's patched state copy, starts PCSX2 on it, plays the scenario's input script and writes one CSV row
per character update, then closes PCSX2. It prints the updates recorded, the reads per poll, the time per poll and the
updates missed. `--state` copies another state, `--attach` records a PCSX2 already running a patched state, and
`--keep-open` leaves PCSX2 running. The CSV is a measurement of the game: it is refused inside the repository.

## trace {#trace}

```sh
uv run --project python coney-tools trace coney SCENARIO --out CSV [--coney EXE] [--disc DISC]
```

Plays the scenario's input script on Coney headless (`--play-level` and the options of its `[coney]` table, `--frames`
its updates, `--trace CSV`). `--coney` defaults to `build/dev/src/platform/coney`, `--disc` to `game_dir`.

```sh
uv run --project python coney-tools trace diff ORIGINAL CONEY [--scenario SCENARIO] [--columns COLUMN ...]
    [--tolerance COLUMN=VALUE ...] [--from STEP] [--to STEP] [--shift N] [--start-frame] [--context N]
```

Compares two traces step by step and prints, per column, the first step outside its tolerance, the largest difference
and its step, then the rows of both around each divergence. It exits with 0 when every column is within tolerance, 1
when one is not and 2 when a trace cannot be read. With `--scenario`, the columns, tolerances, start frame and first
step (the first update of input) come from the scenario's `[diff]` table; flags override them.
[Comparing with Coney](research-workflow.md#comparing-with-coney) explains the alignment and the start frame.
