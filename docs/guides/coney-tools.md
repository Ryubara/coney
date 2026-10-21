# The coney-tools command line

`coney-tools` is Coney's own command line (see [Conventions](conventions.md#python)). This page covers nineteen groups:
[`check`](#check), which runs what CI would fail on for the files a change touched, [`docs`](#docs), which checks
pages against the documentation standards, [`build`](#build), which
configures, builds and tests the engine under the machine's build slots, [`repo`](#repo), which checks
the repository's own rules (titles, descriptions, labels), `wad`, which reads the game's
archive, `WARRIORS.DIR` and `WARRIORS.WAD`, from **your own disc**, [`extract`](#extract), which turns every asset of
the disc into open formats, [`audio`](#audio), which reads its sound data, [`movies`](#movies), which reads the movies'
headers, [`xbox`](#xbox), which reads the Xbox disc's archive, [`progress`](#progress), which keeps the progress tables
of the README and the docs current, [`orig`](#orig), which indexes the `@orig` tags for the fidelity audit,
[`natives`](#natives), which renders the script-binding masterlist, [`refs`](#refs),
which builds the game reference lists, [`missions`](#missions), which builds the mission status pages, [`pcsx2`](#pcsx2)
and [`trace`](#trace), which record the original's per-update traces in PCSX2 and compare them with Coney's,
[`clips`](#clips), which checks, publishes and cleans up in-game video clips,
[`mcp`](#mcp), which starts the debugging servers for agent tools, [`ghidra`](#ghidra), which sets up your own Ghidra
project with our function names, and [`workspace`](#workspace), which sets up the local workspace. Everything is
read in place and streamed, so the 1.4 GB WAD is never loaded into memory. The format
is described in [WARRIORS.DIR / .WAD](../research/formats/wad-dir.md).

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

## wad extract {#wad-extract}

```sh
uv run --project python coney-tools wad extract [DISC] ../../scratch/wad [--names FILE] [--only HASH_OR_NAME ...]
```

Writes entries into the output folder. A file is named by its recovered name, otherwise `<hash>.bin`. `--only`
takes entry hashes (8 hex digits) or names (`global.lua`), and without it every entry is written, about 1.4 GB.

!!! warning "Game data stays out of the checkout"
    `extract` and `names` refuse an output path inside the repository, because extracted files are game data and
    must never be committed ([LEGAL.md](repo:LEGAL.md#no-game-data)). Use a
    folder beside the checkout, such as `../../scratch/`.

## scenes {#scenes}

```sh
uv run --project python coney-tools wad scenes [DISC]
```

Parses every record `scene_list.cnk` lists (scene headers and the segments of long scenes) with the layout on
[Scenes](../research/scenes.md#data) and prints counts and hashes only: records parsed and failed, roles, objects,
cameras and lights, total frames, broken segment chains, how many headers' frame counts differ from their parts, the
event types seen, and SHA-256 digests of the list and of the records. Records whose list names are cut to 16
characters are found by content. Exits 1 when a record fails to parse.

## extract: every asset in open formats {#extract}

```sh
uv run --project python coney-tools extract [DISC] OUT_DIR [--only TYPE ...] [--verify] [--jobs N] [--xbox XBOX_DISC [--xbox-index FILE] [--xbox-texture-map FILE]]
```

Reads your disc once and writes every asset into `OUT_DIR` (outside the repository, like `wad extract`): the
folder Coney's install step will produce, and that the engine and mods will read. Each type has its own top folder:

| Type | Writes |
| --- | --- |
| `disc` | `disc/`: the executable, the IOP modules and image and `SYSTEM.CNF`, copied; `disc/files.json` lists every disc file with its size and SHA-1 |
| `movies` | `movies/*.bik`, copied, and `movies/index.json` (size, frames, rate, audio tracks) |
| `audio` | `audio/sounds/<name>.wav` (streamed sounds, named as the game names them, `unnamed/<hash>.wav` otherwise), `audio/banks/<bank>/<name>.wav`, `audio/music/<track>.wav`, and `sounds.json`, `music.json`, `classes.json` |
| `scripts` | `scripts/<name>.lua`: the compiled Lua chunks as stored (not source), and `scripts/index.json` |
| `textures` | `textures/<dictionary>/<texture>.png`, the streamed world's in `textures/worlds/<file>/`, and `textures/index.json` (sizes, depth, mipmaps, filter, addressing, sprite rectangles) |
| `models` | `models/<name>.gltf` and `.bin`: every model as glTF 2.0, a character with its skin, each textured from `../textures/`; `models/index.json` (dictionary, counts, bone offsets) |
| `animations` | `animations/<clip>.json` (root channels, a rotation channel per bone, events), `animations/characters/<name>.json` (clips, the 722 anim slots, the range list) and `animations/index.json` |
| `levels` | `levels/<level>/`: `collision.json` and `collision.gltf`, `paths.json`, `occluders.json`, `subtitles.json`, the sky models and the light glows as glTF, `level.json` and the file as stored (`source.lev`) |
| `worlds` | `worlds/<world>/world.gltf`: the streamed world assembled, every part's atomics at their sectors; `world.json` (manifest, sectors); parts no sector names in `unused/` |
| `scenes` | `scenes/<record>.json`: every in-engine scene record (roles, poses, clips, object, camera and light tracks) and `scenes/list.json` |
| `data` | `data/global/*.json` (Character, Object, Anim and Dependency Lists, sprite sheet table), `data/objects/<level>.json` (placed objects), `data/fonts/`, `data/icon/` (the memory card icon as glTF and PNG) |
| `index` | `index/wad.json`: every WAD entry's kind, name and resources, and every resource's chunks |
| `raw` | what no decoder takes, as stored, each with a `.json` description: only the five old RenderWare files no code reads |

`--only` limits the run to some types; the others' files and records stay as an earlier run left them. A dictionary
or resource is named by its recovered name, or by its hash in hex. `manifest.json` lists every file with its SHA-256
and, per type, the file count, the bytes and a digest over the type's files, so two runs (or two discs) can be compared
with it. The output is deterministic. At the end the command prints, per type, the files, bytes and digest and the
stage's counts; `--verify` then compares each type's file count with the expected one for the NTSC-U disc and exits 1
on a difference. `--jobs` sets the worker processes that decode the audio (default up to four). A full run takes
about 15 minutes and writes about 11 GB, most of it WAV and scene JSON. Models, levels and worlds are glTF 2.0 that
any viewer opens, textured from `textures/`; coordinates keep the axes the game stores. Which formats are decoded
and which are still raw: [Asset inventory](../research/formats/inventory.md).

`--xbox XBOX_DISC` adds your Xbox disc (a full image, an XISO or an extracted folder) as the preferred source: the PS2
disc is still read whole, then each texture and movie the Xbox has a better version of is replaced in place, at the same
path, and its `index.json` record says `"source": "xbox"` (with the PS2 size kept). A texture is replaced when its Xbox
match (same picture by colour and structure) is larger; a movie by its 1280 × 720 `_hd` version when that is as long.
Everything else stays the PS2 disc's. The rules and the numbers: [Xbox assets](../research/xbox-assets.md#asset-kinds).
The Xbox disc adds about half an hour (matching the world textures) and its `default.xbe` SHA-1 goes into
`manifest.json`. Finding the Xbox resources takes a pass over its 1.7 GB archive; `--xbox-index FILE` keeps the result
(the file `xbox index` writes): it is read when it was made from this disc and otherwise made and written there.
`--xbox-texture-map FILE` also writes which Xbox texture replaced which PS2 one, for a reader of the disc formats
([layout](../research/xbox-assets.md#texture-map)).

## audio {#audio}

The game's sound data, read from your own disc (`DISC` as for `wad`): the sound, music and class tables of
`warriors.glr`, the sound banks in the WAD and the streamed files `IOP/BFW.SND` and `IOP/MUSIC.SND`. The formats are
on [Audio data](../research/formats/audio.md).

```sh
uv run --project python coney-tools audio info [DISC]
uv run --project python coney-tools audio list {sounds,music,banks} [DISC]
uv run --project python coney-tools audio decode [DISC] NAME OUT.wav [--bank BANK]
```

`info` prints how many sounds stream from `BFW.SND` (mono and stereo) and how many play from a bank, the sample
rates, the music tracks and every bank with its sound count, with short SHA-1s of the tables to tell discs apart.
`list` prints one line per sound (hash, where its bytes are, offset, size, rate, class, flags, priority, distances,
volume, pitch variation), per music track or per bank sound. `decode` writes one sound or music track as a 16-bit
WAV file and prints its length, peak and RMS: `NAME` is a sound's name as the game writes it
(`vags/character/voices/5/attack_01`), a track's (`music/warriors_theme`) or a `0x` hash; `--bank` takes a bank
sound from that bank. Like `wad extract`, it refuses a path inside the repository.

## movies {#movies}

```sh
uv run --project python coney-tools movies list [DISC]
```

Reads the header and frame index of each movie the game names (`PSS/<name>.BIK`) and prints one line per movie:
file size, Bink revision letter, width and height, frame count, frame rate, running time and each audio track's rate,
channels, sample size, transform (DCT or RDFT) and flags; then the totals and a SHA-256 of the headers and frame
indexes. Nothing is decoded. Exits 1 when a movie is missing or its header does not hold together. What the values
mean for playback: [Movies](../research/movies.md#the-movies).

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
| `xbox index DISC OUT_FILE` | Writes the resource index, `xbox-resources.bin` (where each resource's chunks lie in the volumes; [layout](../research/xbox-assets.md#resource-index)); prints its record count, size and SHA-256 |
| `xbox texture-map PS2_DISC DISC OUT_FILE [--xbox-index FILE]` | Matches every PS2 texture as `extract --xbox` does (into a temporary folder, deleted after) and writes the texture map, `xbox-textures.bin`; prints its record count and SHA-256. About 25 minutes |
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

`--ps2` takes the PS2 disc as the `wad` commands do and reads its `WARRIORS.WAD`. `resources`, `index` and
`textures` read the whole Xbox archive (1.7 GB) and, with `--ps2`, the whole PS2 one.

!!! warning "Game data stays out of the checkout"
    `xbox extract`, `xbox names`, `xbox index` and `xbox texture-map` refuse an output path inside the repository, as
    `wad extract` and `wad names` do.

## progress {#progress}

The [Progress](../progress/index.md) page and the badges in the Progress section of `README.md` are generated from
three inputs:
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
uv run --project python coney-tools progress check
```

Checks the inputs and exits with 1 when they disagree; CI runs it. Nothing generated is committed: the tables of the
progress page and the README's badge data are produced by the documentation build (`mkdocs_hooks.py`), which
publishes them from `main`, so pull requests never conflict on them. `check` fails on:

* an `@orig` tag in `src/` with no `[[function]]` in `functions.toml`, or the other way round, or the two naming the
  function differently;
* a `subsystem` that is not the one whose range in `totals.toml` holds the address (the message names the right
  one; outside every range it is `unattributed`);
* an address inside middleware or outside `.text`, an address listed twice, or sizes that overlap;
* a progress page without its single `<!-- progress:generated -->` marker, where the build puts the tables and
  pictures, or a history page without its `<!-- progress:history -->` marker.

`mkdocs build --strict` shows the tables on the page locally too, and fails on the same problems.

The pictures come from `progress_grid.py` (one square per function, from the Ghidra listing, the tracker's measures
and the source map's file tables), `progress_picture.py` (layout and SVG), `progress_history.py` (one point per day
from git) and `progress_site.py` (the page's HTML and the files of the site); `mkdocs_hooks.py` calls them. The
page's tooltips are `docs/javascripts/progress-grid.js`, the colours `docs/stylesheets/progress-grid.css`, and what
they mean is on [the page](../progress/index.md#the-picture). Where the fidelity audit's results will be read is
`progress_grid.load_audited`: it returns no addresses until the audit stores a machine-readable result on `main`, and
the picture then draws the Audited state.

```sh
uv run --project python coney-tools progress render <folder> [--no-history]
```

Writes what the build publishes to `<folder>`, to look at it without a docs build: the progress and history pages as
the build fills them (`progress/index.md`, `progress/history.md`), the badge files (`progress/badges/*.json`,
shields.io endpoint JSON that the README's badges read), the pictures (`progress/progress.svg`,
`progress-size.svg`, `missions.svg`, `history.svg`), the raw numbers (`progress/progress.json`), the per-function
table the tooltips read (`progress/functions.json`) and the history (`progress/history.json`). It rebuilds the daily
history from git, a few seconds a day; `--no-history` keeps it to today's point. It refuses to write while the inputs
disagree, as `check` finds.

The docs build does the same, with the history limited so a pull request's build stays quick: it keeps the days of the
file named by `CONEY_PROGRESS_HISTORY_CACHE` (the previously published `progress/history.json`) and today, and measures
the missing days only when `CONEY_PROGRESS_HISTORY=full`. The `docs` workflow sets both on a push to `main`: it
downloads the published file (a missing one just means every day is measured), builds with `full` and deploys the
result, so each deploy extracts one commit. A cached day counts only for the commit it was measured at, and a day
that could not be measured is recorded with its commit and not tried again. The file also records a digest of the
tracker's measuring code (`progress.py`, `progress_research.py`): when that code changes, the cache is ignored and
every day is measured again, so the chart never mixes two definitions. A pull request's build has no cache (only the
deploy learns the site's address), so its History page shows today alone; locally, point
`CONEY_PROGRESS_HISTORY_CACHE` at a downloaded `history.json` to see the days.

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
only by a tail jump) is not seen as a start, and the span of the function before it includes it, unless that function
is itself listed in `functions.toml`: a listed function always ends the span of the one before it. When `--fill`
gives a size that looks too large for what the research page describes, check the size Ghidra shows.

## orig {#orig}

The fidelity audit compares each function Coney reimplements with the original function at its `@orig` address in
Ghidra ([The @orig tag](conventions.md#comments-and-orig)). The `orig` commands give it its index and its order of
work. They only read: the source, the research pages, the shared Ghidra project through reading endpoints, and the
open issues through `gh`.

```sh
uv run --project python coney-tools orig index --out <folder> [--ghidra auto|live|listing|off] [--url URL]
uv run --project python coney-tools orig queue --out <folder> [--issues FILE] [--label bug] [--show N] [--ghidra ...]
```

`orig index` reads every `@orig` tag in `src/` and writes to `<folder>` (use your scratch folder):

* `index.tsv`: one line per tag: the original's address, name and source file, the Coney function the tag
  documents, its file and line, where the tag sits (`comment` above a function, `body` inside one, `other` on a
  class or file), and the research links that document it (the function's comment, else its declaration's, its
  class's or its file's);
* `untagged.tsv`: the functions defined in the gameplay directories (every directory of `src/` but those of
  `orig_index.CONEY_ONLY_DIRS`: `platform`, `core`, `fileio`, `debug`, `net` and `sandbox`, so a new directory is in
  scope) that have no tag and no Coney-only marker. A tag covers a function through its own comment, or through its
  declaration's: the declaration with the same scope, name and parameter types (any one, when the name is not
  overloaded), in the same file pair, folder or, for a member, anywhere; a file's private helper only through a
  declaration in its own file. Each is a `member`, a `free` function, a file's private `helper` (an anonymous
  namespace or `static`) or an `inline` member defined in its class. A marker is the proposed `@coney-only <reason>`
  in the function's comment or its file's header comment, or the older wording ("Coney's own", "(no @orig)");
* `research-gaps.md`: the research sections tagged code links that cite no original address, links to a page or
  section that does not exist, tags whose linked pages never cite their address, and tags with no research link;
* `report.md`: the counts, the malformed tags (a line with `@orig 0x` that is not the whole form `@orig 0x<address>
  <Name> (<File>.cpp)`), the tags Ghidra does not bear out, and with a running server `callees.tsv` and its
  table: the original functions the tagged ones call that no tag covers (game code only; the libraries the original
  links in are left out, by `docs/progress/totals.toml`), most-called first.

The Ghidra check (`--ghidra`, default `auto`) uses a running ghidra-mcp ([Ghidra + ghidra-mcp](ghidra.md)) when one
answers on `--url`, else the committed listing `docs/progress/ghidra-functions.tsv`, which is as current as its last
`progress ghidra` export and gives no callees. Each tag gets one of: `no-function` (Ghidra has no function there),
`not-a-start` (the address is inside another function), `names-differ` (they share fewer than half the shorter
name's words: the tag may name the wrong function), `renamed` (half or more, usually a rename in Ghidra since the tag was
written) or `unnamed` (Ghidra still has its default `FUN_` name). Names agree when they are equal without case and
punctuation, so `DVDWadIndex::Find` matches `DVDWadIndex_Find`, and a binding's `lua_` wrapper prefix is ignored. A
full live run asks the server about each of the 2,400 tagged functions twice (the function, its callees), one request
at a time; on the development machine it took about 11 seconds.

`orig queue` writes the same files and the audit's queue, `queue.md` (a table per `src/` directory, then the first
`--show` entries) and `queue.tsv` (every entry). An entry is an original function with each Coney place that tags it,
or an untagged function; its score adds up its reasons (`orig_queue.WEIGHTS`):

* an open issue with `--label` (default `bug`) that names it (its address, its original or Coney name), its file or
  a research section it links, weighted by the issue's priority label; the area labels of the issues add at most
  4 points between them;
* a tag Ghidra does not bear out, as above;
* research that grades it only `inferred` or `speculative` anywhere, cites it with no level, or does not cite it;
* a linked section that cites no address, or linked pages that lack its address;
* for untagged code, its kind and length.

`--issues FILE` reads a saved `gh issue list --json number,title,body,labels,comments` instead of asking GitHub. The
order is a starting point for the audit team, which re-ranks as it learns. The files hold addresses, names, paths and
issue numbers, never the original's code; they change with every pull request that touches `src/`, so they are
regenerated when needed and never committed.

## natives {#natives}

The script-binding reference (`docs/references/bindings/`, under Game references) is generated from
`research/bindings/<category>.yaml` (schema: [Research workflow](research-workflow.md#bindings)).

```sh
uv run --project python coney-tools natives render [--check]
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
uv run --project python coney-tools natives mission1 [DISC] [--check]
```

Reads the first mission's compiled scripts (`level99.lua` and its three chapter scripts) and `global.lua` from your
disc (the `game_dir` of `coney.local.toml` when no disc is given), and sets every entry's `usage.mission1`: true when
those scripts name the binding, or a `global.lua` helper they reach does (followed by name, as a `Table.field` or as a
callback string), or one of the two helpers the engine calls when a mission ends (`UnlockAndLoad`, `runNextMission`).
It prints the coverage (bindings, traced, Coney status), which `natives render` also publishes as
[Mission 1 coverage](../references/bindings/mission1.md). Only names are read; nothing from the disc is written. Run
`natives render` after it. With `--check` it writes nothing and exits with 1 when a marker is stale; it needs the disc,
so CI does not run it.

```sh
uv run --project python coney-tools natives missions [DISC] [--check]
```

The same for the rest of the story: for each later level of `STORY_LEVELS` (`coney_tools/natives.py`: missions 2-18
and the hub in story order, then the flashbacks and the Armies of the Night levels) it reads the level's scripts
(`levelNN.lua` and its `levelNN_*.lua` chapters, found by name in the WAD names list, without the language files and
the scene tests) with the same reach rule, and sets every entry's `usage.levels` to the numbers of the levels that can
call it (no key when none). It prints each level's bindings and how many are new (called by no earlier level, the
first mission included) and traced; `natives render` publishes them as
[Story coverage](../references/bindings/story.md). Run `natives render` after it; `--check` as for `mission1`.

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
`warriors.glr`, every animation clip, three tables of the executable, the scene list `scene_list.cnk`, the
movies in the disc's `PSS` folder (when `DISC` is a folder) and the WAD's entry names. It keeps every
hand-written field and every entry it did not produce, then renders the pages. `--names` adds WAD names found
another way (a file with one name per line, or the last word of each line, as `wad names` writes), each kept only
when it hashes to an entry; the names already in `wad-names.yaml` are always used. A full run takes about half a
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
`refs extract` links the images.

## missions {#missions}

The [Missions](../missions/index.md) pages are generated from `research/missions.yaml`
([Research workflow](research-workflow.md#missions) has its schema and who updates it).

```sh
uv run --project python coney-tools missions render [--check]
```

Checks the list (every story level present, each mission's checkpoint count equal to the Sections column of the
levels list, each status consistent with its checkpoints) and writes `docs/missions/index.md` and one page per level,
with the script-binding numbers of [Story coverage](../references/bindings/story.md) read from
`research/bindings/`. With `--check` it writes nothing and exits with 1 when a page is out of date; CI runs it that
way. Run `natives render` first when bindings changed.

## pcsx2 {#pcsx2}

The `pcsx2` commands drive the original in PCSX2 over PINE and the PCSX2-MCP fork's debug server, for runtime research
and parity checks. What they do and why, the scenario format and the address expressions are in
[Recording a trace](research-workflow.md#recording-a-trace). The folders come from `coney.local.toml` (`pcsx2_dir`,
`game_dir`, `scratch_dir`) unless `--pcsx2-dir`, `--iso` and `--scratch` give them; PINE must be enabled in PCSX2's
`inis/PCSX2.ini`. Problems print one line and exit with code 2.

```sh
uv run --project python coney-tools pcsx2 setup-copies [--count N] [--dry-run]
```

Makes or updates the portable copies `pcsx2`, `pcsx2-b`, ... (`--count`, default `pcsx2_copies`, 5) under
`pcsx2_root` from the fork build `pcsx2_mcp_dir` names: the build's program files only, `portable.txt`, and in each
copy's ini PINE on `pcsx2_pine_base` + *i*, the debug server on `pcsx2_debug_base` + *i* and `SDL = false`. A copy's
settings, BIOS, memory cards, states and recordings stay; a copy whose PCSX2 runs is refused (exit code 1).
`--dry-run` prints what would change. Details: [PCSX2 copies](workspace.md#pcsx2-copies).

Several agents share the copies through claims ([Several at once](research-workflow.md#several-at-once)); any
command that starts PCSX2 needs one.

```sh
uv run --project python coney-tools pcsx2 claim --agent ID [--copy NAME] [--json]
uv run --project python coney-tools pcsx2 release --agent ID [--copy NAME] [--force]
uv run --project python coney-tools pcsx2 status [--json]
```

`claim` takes a free copy (or the named one) atomically and prints its folder, PINE port and debug server port (a line
`pcsx2-dir=... port=... debug-port=...`; `--json` has `port` and `debug_port`, which is `null` for a copy without the
debug server); it hands back your own claim when you hold one, and takes over a stale one (its processes gone, or
older than `--max-age-hours`, default 4, with no process) saying so. A claim whose copy has a live process is never
stale, whatever its age or owner: a PCSX2 running from the copy's folder, a live recorded PCSX2 or holder pid, or a
listening PINE or debug server port; that includes a claim whose owner file is unreadable (shown as holder `?`), which
is stale only after a few seconds and only with nothing running in the copy. A copy running PCSX2 without a claim is
never handed out. `release` closes the PCSX2 it recorded (only while that process is still a PCSX2) and drops the
claim; another agent's claim needs `--force`. `status` lists every copy with its holder, the time, the PINE and debug
ports, whether PCSX2 runs (by pid or by either port) and stale claims; it warns about copies whose ini has
`[InputSources] SDL = true`. The copies are the `pcsx2*` folders with an `inis/PCSX2.ini` under `pcsx2_root` (default:
`../../emulators`); two copies on one port (PINE or debug server) are refused, naming both. The claims are under
`pcsx2_claims_dir` (default: `pcsx2-claims/` in the shared scratch folder, the main checkout's `../../scratch`,
whatever a worktree's `scratch_dir` says, so every worktree sees the same claims).

```sh
uv run --project python coney-tools pcsx2 keys --agent ID --copy NAME KEY [KEY...] [--hold-ms N] [--gap-ms N]
uv run --project python coney-tools pcsx2 screenshot --copy NAME --out PNG
```

Both are Windows-only and never focus a window. `keys` posts key down and up messages (`WM_KEYDOWN`) to the copy's
window by handle, one key after another (`W+K` presses two together; names: letters, digits, `space`, `return`,
arrows, `f1`..`f12`), each held `--hold-ms` (default 300). `screenshot` writes the game widget's picture, read by
handle, so it works while other windows cover it (not while it is minimised); it is refused inside the repository.

```sh
uv run --project python coney-tools pcsx2 prepare-state SOURCE OUT [--patch NAME ...]
```

Copies a save state (a `.p2s` file, or `slot:N` for quick-save slot N, which is only read) to `OUT` with the named
patches of `research/traces/patches/` (one file per pass) applied to its EE memory. It refuses an `OUT` inside the
repository or in PCSX2's `sstates/` folder, and refuses the copy when the state does not hold a patch's original
instruction words. PCSX2 saves states with zstd, which Python reads from 3.14 (uv installs it).

```sh
uv run --project python coney-tools pcsx2 repack-state SOURCE OUT
```

Copies a save state (a `.p2s` file or `slot:N`, only read) to `OUT` with every compressed entry rewritten with plain
deflate, the same contents byte for byte. Ghidra's zip reader, which the EE extension's `PCSX2SaveStateImporter.java`
script uses, cannot read PCSX2's zstd, and Python cannot write PCSX2's other choice, Deflate64; plain deflate is read
by all three, so PCSX2's own compression setting stays as it is. The same refusals as `prepare-state` apply. Why we
do not usually load states into Ghidra: [Tools](research-workflow.md#tools).

```sh
uv run --project python coney-tools pcsx2 launch [STATE] --agent ID
```

Starts PCSX2 on a state file (`-fastboot -statefile`, the disc through a hard link in the scratch folder when its path
holds commas or parentheses), waits until the game runs, and leaves it running; with no state it boots the disc and
returns once PINE answers; when the copy runs the debug server, it also waits for that port, and prints both. It is the
only way agents start PCSX2: it never takes the keyboard focus (WMI starts the
process outside the agent's process tree; why: [Driving PCSX2](research-workflow.md#driving-pcsx2)). With
`CONEY_WINDOW_DISPLAY` set ([Which display](running.md#window-display)), it moves each PCSX2 window onto that
display as it appears, without activating it; `pcsx2 record` does the same. It refuses to
start when something already serves PINE or the copy's debug server port. It runs under `--agent`'s claim (made, and
kept, if you hold none) and records
PCSX2's pid in it;
`--pcsx2-dir` must be that claim's copy.

```sh
uv run --project python coney-tools pcsx2 record SCENARIO --out CSV --agent ID [--state SOURCE] [--attach] [--keep-open]
                                     [--recording P2M2]
```

Makes the scenario's patched state copy, starts PCSX2 on it, plays the scenario's input script and writes one CSV row
per character update, then closes PCSX2. It prints the updates recorded, the reads per poll, the time per poll and the
updates missed. The scenario names a quick-save slot (`slot`) or a state file under the scratch folder (`state`);
`--state` copies another state, `--attach` records a PCSX2 already running a patched state, and `--keep-open` leaves
PCSX2 running; the claim is released at the end when `record` made it. The CSV is a measurement of the game: it is
refused inside the repository. With
hooks among the scenario's patches it also writes each hook's call log to `<CSV stem>.<hook>.csv` beside it, and
makes the scenario's `calls` ([Hooks](research-workflow.md#hooks)).

`--recording P2M2` takes the pad from a PCSX2 input recording instead of the scenario's input script, so a runtime
trace can be repeated exactly: the recording (and its start savestate `<P2M2>_SaveState.p2s`, patched as the
scenario says) is copied into the copy's `recordings/coney-record/`, PCSX2 starts on that savestate, and through the
debug server `record` pauses emulation, plays the recording (which loads its savestate again), samples step 0 and
resumes. The trace's steps then count from the recording's first frame. A recording that ends before the scenario's
`updates` stops with PCSX2 paused at its last frame and an error saying so; a power-on recording (no savestate) takes
no patches. It cannot be combined with `--attach` or `--state`, and needs the copy's debug server.

```sh
uv run --project python coney-tools pcsx2 recordings make NAME --script SCRIPT [--state STATE.p2s] [--updates N]
    [--frames-per-update 2] [--author TEXT] [--force]
```

Writes an input script (Coney's format, [Input scripts](running.md#input-scripts)) as a PCSX2 input recording
`NAME.p2m2` in the scratch folder's `recordings/`, so the original replays the script exactly, frame by frame, as Coney
does. A script's frame is one character update of 1/30 s and PCSX2 takes the pad at 60 Hz, so each script frame becomes
`--frames-per-update` frames (2). Port 1 follows the script with the pad bytes Coney makes (buttons with full
pressure, sticks from the percentages), port 2 stays at rest. `--state` is the savestate it starts from, copied beside
it as `NAME.p2m2_SaveState.p2s`; without it the recording starts from power-on. The length is the script's last frame
plus one second unless `--updates` says. A recording already there needs `--force`. Then `recordings push` it to a copy
and play it ([Input recordings](research-workflow.md#input-recordings)). The layout it writes is PCSX2's own, from
`pcsx2/Recording/InputRecordingFile.h`.

```sh
uv run --project python coney-tools pcsx2 recordings push|pull NAME --agent ID --copy NAME [--force]
```

Copies an input recording (`NAME.p2m2` below `recordings/`, such as `fights/rumble-1.p2m2`) and its start savestate
between the shared scratch folder's `recordings/` and the claimed copy's `recordings/` (`push` to the copy, `pull`
back), checking each copy's SHA-256. A different file already there needs `--force`. Recordings are made and played
in the copy with the MCP server's `pcsx2_input_recording` ([Input recordings](research-workflow.md#input-recordings)).

```sh
uv run --project python coney-tools pcsx2 clip STATE --out CLIP --agent ID [--seconds N] [--iso DISC] [--scratch DIR]
```

Records a short clip of the original (`--seconds`, 1 to 10, default 10) with **PCSX2's own video capture of its GS
output**: nothing on the desktop is read. For the run, the claimed copy's ini switches the capture on (with sound),
points PCSX2's videos folder at a staging folder beside the clip, turns every on-screen display element off (FPS,
messages, the recording indicator) and binds "Toggle Video Capture" to F12, which is posted to PCSX2's window by handle
to start and stop it; the ini is put back when PCSX2 closes. The capture is then re-encoded into `CLIP` as Coney's
clips are (960 x 720, H.264 and AAC, no metadata, cut at the length) through `clips.run_ffmpeg`, and the staging
folder deleted. PCSX2 2.9.94 tears the first ~1.15 s of its capture (a black band along the bottom), so the capture
runs 1.5 s longer (`CAPTURE_START_TRIM`) and the clip starts after that, still lasting `--seconds`.
`--out` is a path under the scratch folder, `<workspace>/scratch/<task>/clips/NAME.mp4`; give it absolute (the
folder `clips folder TASK` prints), because a relative `../../scratch/...` is wrong from a worktree, which sits three
levels below the workspace. The game comes from `game_dir` in `coney.local.toml`; without it pass `--iso DISC`,
and likewise `--scratch DIR` (an absolute path) without `scratch_dir`.
`CLIP` follows the [clips](#clips) rules. It needs FFmpeg as `clips` finds it, and Windows (keys by
handle). PCSX2 starts on `STATE` under `--agent`'s claim, released at the end when `clip` made it.

```sh
uv run --project python coney-tools pcsx2 recipe list
uv run --project python coney-tools pcsx2 recipe show NAME
uv run --project python coney-tools pcsx2 recipe run NAME --copy COPY --agent ID [--arg NAME=VALUE ...] [--json]
uv run --project python coney-tools pcsx2 recipe render [--check]
```

The shared [PCSX2 recipes](pcsx2-recipes.md) of `research/pcsx2-recipes.toml` (the schema is the docstring of
`repo:python/src/coney_tools/recipes.py`). `list` names them by group, `show` prints one with its parameters,
preconditions, evidence and steps. `run` makes its steps on a copy `--agent` holds a claim on and whose PCSX2 runs,
over that copy's PINE port (and its debug server for `pause`, `resume` and `step`): it refuses another agent's copy, a
copy that runs nothing, an emulation state the recipe does not take and a running state that lacks a patch or hook the
recipe names (read back from memory). `--arg` gives a parameter, `--json` adds one line with every value shown.
`render` writes [the page](pcsx2-recipes.md) from the file; with `--check` it writes nothing and exits with 1 when
the page is out of date, as CI runs it. Every recipe must have an evidence level, a Ghidra address and a
`confirmed_runtime` date, copy and state, or the file is refused.

## clips {#clips}

```sh
uv run --project python coney-tools clips folder TASK
uv run --project python coney-tools clips check CLIP [CLIP...]
uv run --project python coney-tools clips publish (--pr N | --issue N) [--edit] (--body TEXT | --body-file FILE) CLIP [CLIP...]
uv run --project python coney-tools clips clean [--older-than HOURS] [--dry-run]
```

In-game video clips for issues and pull requests, made by `coney --record` ([Recording a clip](running.md#record)) or
`pcsx2 clip` (above). The rules and why: [Clips](testing.md#clips); the limits are `workspace.clips` in
`.github/conventions.yml`.

* `folder` makes and prints a task's clips folder, `<scratch>/<task>/clips/`, where `<scratch>` is the workspace's
  shared scratch folder (`../../scratch` beside the main checkout).
* `check` runs ffprobe on each clip and prints its length, size, picture and whether it has sound. It refuses a clip
  outside a task's clips folder or inside a git checkout, one longer than `max_seconds` (10), and one with any tag
  beyond the fixed ones an MP4's boxes carry (`major_brand`, `minor_version`, `compatible_brands`, `language`,
  `handler_name`, `vendor_id` and a version-free `encoder`); one bigger than `warn_mb` (10 MB) gets a warning.
* `publish` makes the same checks, then uploads every clip in one gh command, `gh pr|issue comment N` (or `edit` with
  `--edit`, into the description) with one `--attach` per clip, and **deletes the clips once gh succeeds**. When gh
  fails, it keeps them and says so (exit 1). A body may place a clip with a `./clips/<name>.mp4` reference, which gh
  replaces with the upload when the clip is named the same way. It is the only way clips are uploaded.
* `clean` deletes every file in the task clips folders older than `keep_hours` (24; `--older-than` overrides it),
  including what a failed recording left (part files, a `pcsx2 clip` staging folder); `--dry-run` only counts.

FFmpeg and ffprobe are found in `ffmpeg_dir` of `coney.local.toml`, else beside `CONEY_FFMPEG` (the variable Coney's
`--record` reads), else on `PATH`. Every FFmpeg command the tools run goes through `clips.run_ffmpeg`, which refuses a
capture device or any input that is not a file.

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

```sh
uv run --project python coney-tools trace fight-report TRACE... [--humans] [--hz N] [--exclude HUMAN ...]
    [--engage-distance METRES] [--from STEP] [--original-gangs ID,ID,...]
uv run --project python coney-tools trace fight-report --original TRACE... [--original TRACE...]
    --coney TRACE... [--coney TRACE...] [--margin SHARE] [--original-gangs ID,ID,...] [--coney-gangs ID,ID,...]
```

Behaviour statistics of a gang fight, from the all-humans trace of either game (Coney's `--trace-humans`, the
original's `pcsx2 record` of the all-humans field set; [Tracing](running.md#tracing)), by the rules of
[Rumble: what the comparison computes](../research/rumble.md#gang-battle-metrics). The two games draw different
random numbers, so a fight is compared by rates, shares and distributions, not update by update. A trace needs the
columns `step`, `human` and `gang`; a metric whose columns are missing is left out.

* **The fighters** are the humans of the gangs in `--original-gangs` (`--coney-gangs` for Coney's runs), or else of
  the gangs that fight (a target, an attack, a block or a knock-out), less a crowd of brain type 4 (`btype`).
* **The fight** runs from the first step a fighter's brain is on (`brain_off` 0, or without that column the first
  goal pushed) to `RumbleOver`, the step before every fighter's `brain_off` is 1 (or every goal stack has been empty
  for 30 steps). A trace without those columns runs from `--from` to the first step where one gang or none stands.
* **Out of the fight** is `Human_IsDownOrDead`'s mask, `state` `0x80050000` (dying, knocked out, wounded) or
  `state_hi` `0x1` (dead), or clip 349, or gone from the trace; in a trace without `clip` and `state`, `ko` 1 or
  `health` 0. Every rate is per minute a human stands, at `--hz` updates a second (30).

For the whole match (`all`) and each side (and each human with `--humans`) it prints:

* **attacks** (`attack.total_per_min`, the wind-ups: `phase` bit `0x1` turning on, with `attack.distance_p10`,
  `p50`, `p90` to the target then; without `phase`, a change to an attack command in `command`, also by kind as
  `attack.square_per_min`, ...);
* **blocks** (`block.per_min`, the Blocking goal, 27, coming on top) and **held blocks** (`block.held_per_min`,
  `blocking` or the block command); **reactions** (`react.per_min`), **knock-downs** (`down.per_min`), **health
  losses** (`health.losses_per_min`) and **taunts** (`taunt.per_min`, clip 603 starting);
* **target changes** (`target.changes_per_min`, split into `target.switch_per_min` A to B, `set_per_min` from none
  and `clear_per_min` to none, and by cause as `target.cause.pick_per_min`, `front`, `hit_back`, `help`, `engage`,
  `displaced`, `reaction`, `order`, `other` and `unknown`: from Coney's `target_causes` column, or the original's
  `set-target` hook log beside the trace, `<stem>.set-target.csv`, counting only the calls that change the target,
  each caller named by the table `SET_TARGET_CAUSES` in `repo:python/src/coney_tools/fight_trace.py`, which follows
  [What each set-target caller means](../research/gang-fights.md#set-target-causes); a caller it does not name yet is
  `unknown`. The causes are not a split of `target.changes_per_min` and need not add up to it: they count every
  change in the window, also while the human is out (the knock-out's drop) and both halves of a pick's clear and set,
  where `changes_per_min` counts the changes seen between two sampled rows while he stands; both are divided by the
  minutes he stands) and goal changes and interruptions (`goal.changes_per_min`,
  `goal.interruptions_per_min`: a goal pushed over another, or a stack replaced instead of popped);
* the **time share of each goal** (`goal.NAME`) and the **idle share** (`idle.share`, an empty stack while standing);
  the **fight goals**, runs of Fight on top, a new one when `fight_updates` starts again (`fight_goal.count`,
  `median_s`, `p90_s`, `share_4s`);
* **fighters on one enemy** (`attackers.mean`, `p90`, `max`, `share1` to `share4`) and the **active places** held on
  a fighter (`active.share0`, `share1`, `share2`, `share3plus`);
* **waiting**: the distance to the target with Fight on top and no active place (`fight_active` 0), split by whether
  the target's own target is him (`wait.mutual_p50`, `wait.other_p50`, and the share in 4-4.75 m, `_band`);
* **help** (`help.calls_per_min`, `help.helpers_per_min`, `help.respond_per_min`): Coney's `help_calls` and
  `helpers_sent` columns, or for the original the `help-call` and `send-helper` hook logs beside the trace
  (`<stem>.help-call.csv`, `<stem>.send-helper.csv`);
* the **distance to the target** (`distance.p10`, `p50`, `p90`), the **time to the first engagement**
  (`engage.first_s`, `median_s`, `share`: the first attack, or a target within `--engage-distance`, 3 m by default)
  and the **knock-out times** (`ko.count`, `first_s`, `median_s`, `last_s`);
* per side, its **fighters out at the end** (`out.at_end`), its **tactic** (`tactic.first`, `last`, `changes`,
  `first_updates`, from `g_tactic`), `teamups.max`, `order.share` and `chosen.share`;
* the **match's length and winner** (`match.length_s`, `match.winner`: the side with fighters standing at the end).

`--exclude` leaves humans out (a spectator, the pad's player); `--from` skips the steps before one.

With `--original` and `--coney` it prints, for every metric, the original's spread (min / median / max over its runs)
beside Coney's and marks with `<<` each metric where Coney's median is outside the original's spread (`above`,
`below`, `missing`, or `different` for a winner the original never had). `--margin` widens the spread by that share of
its width. A goal or an attack one side never uses counts as zero. Each `--original` and `--coney` names the runs of
one orientation (which gang is side 1), and the two are paired in order, so each orientation of Coney's is compared
with the original's runs of the same orientation; give as many of one as of the other. Sides are labelled `g0`, `g1`,
... by rank, the lowest id first, or in the order of `--original-gangs` and `--coney-gangs` when their ids differ. The
exit code is 1 when a metric is outside the spread, 0 when none is, 2 when a trace cannot be read. With one run of the
original the spread is a point, so record several ([Comparing with Coney](research-workflow.md#comparing-with-coney)).

```sh
uv run --project python coney-tools trace mission SCENARIO --side coney|original --out CSV [--coney EXE] [--disc DISC]
    [--state STATE] [--agent ID] [--updates N] [--follow ROUTE [--follow-from EVENT]]
```

Plays a mission scenario (`research/traces/missions/*.toml`) on one game with its course, an adaptive pad that plays
by what is on screen, and writes the game's event log to `CSV` (keep it outside the repository: it holds the game's
text). `--side coney` runs Coney headless with `--pad-pipe` and `--event-log` (`--coney`, `--disc` as for `trace
coney`); `--side original` starts PCSX2 on the scenario's state with its patches and event hooks, as `pcsx2 record`
does, under your claim (`--agent`), and reads the events from the hooks' ring. `--state` starts from another state
file or `slot:N` (a copy, never the slot itself); `--updates` caps the run. It prints its progress every 1000 updates
and ends when the scenario's `until` event comes, or at its `updates`. Beside the log it writes the run's route,
`<CSV stem>.route.csv` (`step,x,y,z,heading,buttons,lx,ly`: the player's feet and heading after each update in play,
then the pad given for the next one; the buttons are Coney's bits in hexadecimal, the left stick as raw bytes).

`--follow ROUTE` (Coney only) plays another run's route instead of the course, usually the original's, so Coney's
player goes where the original's went and what is heard and spawned on the way can be compared. The route is cut
into walks (a waypoint each 0.5 m of the recorded travel, walked to with the stick at 60 %) and actions (the updates
in which the recorded player pressed a button, or turned the stick while standing still, played back pad for pad once
he stands within 0.25 m of where it began and faces its heading); updates out of play hold the pad at rest, so a
scene or a load does not use up the route. `--follow-from "kind name"` lets the scenario's course play until that
event (`"call CarSpawnRadio"`), then follows the route from the step its own run saw it, found in the event log beside
the route: for a stretch one game's start state has half done. A route is followed, not replayed: Coney may take
longer over it (a waypoint it cannot reach is given up after 40 updates without headway).

```sh
uv run --project python coney-tools trace events-diff ORIGINAL CONEY [--rules RULES] [--kinds KIND ...] [--window N]
    [--limit N] [--labels JSON] [--from EVENT]
```

Compares two event logs in order, not frame by frame, by the rules of `research/traces/events.toml` (`--rules` for
another): the furthest milestone both reached and the one Coney never reached, then the events missing in Coney, the
extra ones, those out of order, milestones whose spacing differs by more than `--window` updates, and the frequent
events by count. `--from "kind name"` starts both logs at their first such event, for two runs that began at
different points of a mission; `--labels` names hashed texts by a JSON object of label to text (a string table read
from your disc, kept in scratch). It exits with 0 when Coney has every compared event in order, 1 when not.
[Differential playthroughs](research-workflow.md#differential-playthroughs) explains the method.

## repo {#repo}

```sh
uv run --project python coney-tools repo check
uv run --project python coney-tools repo check-title FILE|-
uv run --project python coney-tools repo check-docs [--body FILE]      # changed paths on stdin
uv run --project python coney-tools repo check-body [--body FILE|-] [--title TITLE] [--worktree WT [--base REF]]
uv run --project python coney-tools repo review PR.md --worktree WT [--base REF] [--model M] [--effort E]
    [--claude EXE] [--timeout-minutes N]
uv run --project python coney-tools repo check-review PR.md --worktree WT [--base REF]
uv run --project python coney-tools repo review-stamp PR.md --worktree WT [--base REF]
uv run --project python coney-tools repo labels [--apply] [--repo OWNER/NAME]
uv run --project python coney-tools repo render [--check]
uv run --project python coney-tools repo audit [--files] [--github] [--local] [--json] [--only CHECK ...] [--strict]
    [--show N] [--snapshot FILE] [--save-snapshot FILE] [--attachments] [--workspace DIR] [--list]
uv run --project python coney-tools repo pr-health [--json] [--pending-hours H] [--verbose] [--labels] [--rerun] [--apply]
uv run --project python coney-tools repo hygiene (--event N | --sweep) [--apply] [--snapshot FILE]
```

The checks of the repository's own rules, which CI runs on every pull request. Every value they apply (the commit
areas and verbs, the description's sections, the path classes, the labels) is a key of `.github/conventions.yml`
([Repository conventions](repo-conventions.md)); a missing or malformed file is a one-line error and exit code 2.

* `repo check` loads and validates `.github/conventions.yml` and checks that each agent entry file (`CLAUDE.md`,
  `GEMINI.md`) holds the `@AGENTS.md` import line and nothing more. Exit 1 when a pointer file is wrong.
* `repo check-title` checks the first line of a commit message or a title (`-` reads stdin) against `commits.title`:
  `area: Verb the rest`, a known area and verb, the length limit, no trailing period, the breaking suffix last. The
  `title` job of the `pr` workflow runs it on the pull request title.
* `repo check-docs` reads the changed paths on stdin and refuses a change under `paths.code` that has no change under
  `paths.docs` and no `Docs: none` line in the body.
* `repo check-body` checks a pull request description against the template ([What a pull request
  shows](testing.md#pr-evidence)). Given `--worktree`, it also reads the branch's commits (`--base`, default
  `origin/main`, to `HEAD`) and requires the AI use `Model:` line to name every model a `Co-Authored-By` trailer on them
  credits: see [the Model line](#repo-model-line). Given `--title` (the `body` job passes the pull request's title), a
  title area a player sees (`pull_requests.runtime.areas`: `gameplay`, `graphics`, `audio`) must have a line starting
  `Runtime:` in the Testing section: the evidence of a comparison with the original running, with numbers, or `Runtime:
  none (<reason>)` ([the Runtime line](testing.md#runtime-line)). Without `--title` that rule is not checked and the
  output says so; `repo check-review` always has the title (line 1 of `PR.md`).
* `repo review`, `repo check-review` and `repo review-stamp` are the release gate on a description's content: see
  [review](#repo-review). All three refuse a description whose title or body fails the checks above.
* `repo labels` prints the `gh label create` commands for `labels.list`; `--apply` runs them (the coordinator does).
* `repo render` rewrites the generated blocks of `AGENTS.md`, `CONTRIBUTING.md` and every guide under `docs/guides/`
  from the conventions: the text between a `<!-- conventions:begin NAME -->` line and the next
  `<!-- conventions:end -->` line (the commit areas and verbs, the label table, the rules for a pull request
  description, the required checks, the documentation-only paths). Run it after changing `.github/conventions.yml` and
  commit both. `--check` changes nothing and exits 1 naming each file with a stale block, as the `python` workflow does;
  a wrong marker (an unknown block name, an end without a begin, a block never closed) is a one-line error with the
  file and line, exit code 2. [Generated blocks](repo-conventions.md#generated-blocks) lists the blocks.
* `repo audit` reports where the repository departs from the conventions and changes nothing: see [audit](#repo-audit).
* `repo pr-health` lists the open pull requests that are stuck, and with `--rerun` restarts the checks that failed for a
  reason that no longer holds: see [pr-health](#repo-pr-health).
* `repo hygiene` writes the audit's findings back as comments: see [hygiene](#repo-hygiene).

### The Model line and the branch's trailers {#repo-model-line}

A branch whose commits carry a `Co-Authored-By: <model name> <noreply address>` trailer was written with that model, so
the description's `Model:` line must say so. Without this check only the `pr-reviewer` agent caught a line that named
the wrong model, and it cost three review rounds (pull requests #460, #466 and #467).

* **What counts.** A trailer on a commit of `base..HEAD` whose key is `commits.trailers.ai_co_author.key` (any case) and
  whose address is one of `commits.trailers.ai_co_author.noreply`; a person credited as a co-author is not a model. Each
  model counts once (compared without regard to case or runs of spaces).
* **The match.** The model name must appear on the `Model:` line, in any case and anywhere on it (`Claude Opus 5.5` is
  found in `Claude Sonnet 5.5 and claude opus 5.5 (review)`), not as the start of a longer version (`Claude Opus 5.55`).
  A branch with no such trailer needs nothing; a `Model:` line of `n/a` for a human-written change passes as before.
  Whoever the description says wrote the commits, a trailer on them has to be named: `a person` with an AI trailer is
  refused. When the AI use lines are themselves unusable (a missing or empty `Model:` for an AI-written branch) only that
  is reported, not the trailers too.
* **The base must be fresh and right.** `base..HEAD` is what the branch has that the base lacks. With a stale
  `origin/main`, a branch that has merged a newer `main` also counts the commits it merged in, and so their trailers
  (other authors' models) must be named: run `git fetch` and the refusal goes away. A stacked branch
  ([Dependent units are stacked](how-we-work.md#release-queue)) with the default base also counts the parent unit's
  trailers: pass `--base <parent branch>`.
* **The message.** One reason per missing model, naming it:
  `AI use: 'Model:' does not name Claude Opus 5.5, which a Co-Authored-By trailer on the branch's commits credits`.
* **Where it runs.** Locally, where the branch is at hand: `repo check-review`, `repo review` and `repo review-stamp` read
  the commits of the worktree after `--base` (default `origin/main`) and refuse before the agent starts, and
  `repo check-body --worktree WT` does the same for a description on its own. **CI does not run it:** the `body` job
  gets the description from the event and has no commits (its checkout is shallow, and a full history for one string
  comparison would slow a required check on every edit), so it passes no models and stays a description-only check, as
  `Docs: none` is. The coordinator's release step is the gate
  ([The description review](how-we-work.md#pr-review)). A branch git cannot list (an unknown `--base`, not a worktree)
  exits 2 rather than passing as one without trailers.
* **One source.** The trailer's key and addresses are `commits.trailers` and the rule's switch is
  `pull_requests.ai_use.model.names_trailer_models` in [.github/conventions.yml](repo:.github/conventions.yml); the check
  holds no copy, and the generated rules in [CONTRIBUTING.md](repo:CONTRIBUTING.md) state it.

### repo review {#repo-review}

The gate a description passes before the coordinator pushes it ([The description review](how-we-work.md#pr-review)).
`PR.md` is the description file as agents write it: the title on line 1, then the body; image paths in it are relative
to its folder.

* `repo review` runs the `pr-reviewer` agent once, headlessly: it reads `.claude/agents/pr-reviewer.md` from the
  checkout the command runs in, hands it to `claude -p --agents FILE --agent pr-reviewer --model M
  --dangerously-skip-permissions` (a throwaway run, in `PR.md`'s folder) with a task naming `PR.md`, the worktree and
  `--base` (default `origin/main`), and then runs `check-review`. Its task has the agent stamp with the same `--base`.
  It removes an old `REVIEW.json` and `REVIEW.md` first, so a run that writes no verdict cannot leave an earlier pass
  behind. `--model` overrides the agent file's model, `--effort` sets the effort level, `--claude` names the
  executable and `--timeout-minutes` (default 30) stops a run that hangs. Exit 0 when the verdict releases the branch,
  1 when it does not or `claude` failed, 2 when the review cannot run (no description, no agent file, not a worktree).
* `repo check-review` first runs the checks CI runs on the description: `check-title` on `PR.md`'s line 1 and
  `check-body` on the rest (`PR.md`'s convention: title on line 1, body after it), except that a local image counts as an
  upload, since `gh --attach` uploads it when the pull request is opened. Each reason is printed with a `title:` or
  `body:` prefix, together with the verdict's, so a missing template section is caught before the push. It also holds
  the `Model:` line to the `Co-Authored-By` trailers of the commits after `--base` (default `origin/main`), which CI
  cannot see ([the Model line](#repo-model-line)). `repo review` runs the same checks before it starts the agent and
  stops at once when they fail. It then reads `REVIEW.json` next to
  `PR.md` and passes only when the verdict is `pass` with no blocker, its `tip` is the worktree's HEAD, its
  `body_sha256` is `PR.md`'s hash now, and every local image `PR.md` shows is listed with its hash now and differs from
  every other one. Exit 1 lists each reason (a title or body CI would refuse, a stale tip, an edited description, a
  replaced or unreviewed picture, the blockers); 2 when `PR.md` cannot be read.
* `repo review-stamp` prints those values as JSON (`tip`, `body_sha256`, `images` with `path` and `sha256`, `null` for a
  missing file). The agent copies them into its verdict, so the hash is computed in one place. It first runs the same
  title and body checks as `check-review` (one function, `precheck`, including the `Model:` line against the commits
  after `--base`, default `origin/main`) and, when they fail, prints the reasons with their `title:` or `body:` prefix on
  stderr, prints no stamp and exits 1, so a reviewer launched as a subagent cannot review a description that
  `check-review` would refuse at release. Exit 2 when `PR.md` cannot be read or the worktree's commits cannot be listed.
  It takes the rules from the checkout the tool is installed in, so it runs from any folder (the agent runs it in
  the scratch folder).

The file names, the agent and the hash rule are `pull_requests.review` in `.github/conventions.yml`; the rules in full
are the generated list in [The description review](how-we-work.md#pr-review).

### repo audit {#repo-audit}

`repo audit` runs the checks of `hygiene.checks` in `.github/conventions.yml`, in up to three scopes
([the checks](repo-conventions.md#audit-checks)):

* `--files` reads the checkout and needs no network (the default when no scope is given). The `python` workflow runs
  it on every pull request, and `check changed` runs it as its `repo` step. It checks the pointer files, that every
  file the conventions name exists, that the repository's address is only in the README, that the issue forms, the
  pull request template, `.github/scripts/ci-scope.sh` and pre-commit's size limit still agree with the file, and that
  the generated blocks are current.
* `--github` reads issues, pull requests, labels, the repository settings and the ruleset through `gh` (read only;
  `gh` must be signed in) and checks them: the sections of each open issue (a missing one, one left as `x`, headings
  that are not the form's `###` headings in its order, an Area that is not an option), its Screenshots, its labels,
  a closed issue that still carries an `S-` status label, the word "owner" for a person (say "user" for who tested or
  filed it, "maintainer" for who decides), closed issues and merged pull requests that
  keep an unchecked item with no follow-up issue number, open pull requests whose description names a path on your
  machine or an image that is not uploaded, stacked pull requests, how long an item has waited, and drift between the
  labels, settings and ruleset on GitHub and the file. `--save-snapshot FILE` also writes what it read, and
  `--snapshot FILE` audits a saved snapshot instead of reading GitHub (the tests do; so can you, offline). The link
  check needs `origin/main` in the checkout (`git fetch origin`) and says so when it is missing. `--attachments` also
  downloads the `.txt` and `.log` files attached to open issues and scans them for possible game data and personal
  information (`issue.attachment-data`, `issue.attachment-personal`; [Attached logs](issues.md#attachments)); without
  it those two checks do not run and a notice says so.
* `--local` reads the workspace around the checkout: the free space of its drive, merged worktrees and stray files
  among the worktrees (`workspace clean` would remove them), and scratch folders of merged or abandoned branches that
  still hold files taken from the original (save states, traces, dumps). The limits are the `workspace` section of the
  file. It asks GitHub once for the merged pull requests of every branch, judges the worktrees and walks the scratch
  folders several at a time, and does not read the last activity of a worktree it will not report (a `build/` folder
  is `workspace clean`'s business), so a workspace of 80 worktrees takes seconds, not minutes.

Each finding has the check's id, a severity (`error` or `warn`), the item (`#201`, a file, a worktree), a message and a
fix class: `auto` (a coney-tools command fixes it), `skill` (a person or the `/repo-hygiene` skill fixes it with
judgement) or `maintainer` (a maintainer decides). The text report groups findings by check and prints the counts;
`--show N` limits the findings printed per check (0 prints all). `--json` prints one object with `errors`,
`warnings`, `checks` (the count per check that ran), `notices` (what could not be read) and `findings`, each with
`check`, `keys` (the conventions keys it enforces), `severity`, `item`, `url`, `message` and `fix`. `--only` takes a
check id or a group (`issue`) and may be repeated. Exit code 1 when a finding has severity `error` (any finding with
`--strict`), 2 when `gh` or the snapshot cannot be read or the conventions are unusable, otherwise 0. The audit never
edits an issue, a pull request or a label; fixing the findings is the coordinator's job.

### repo pr-health {#repo-pr-health}

`repo pr-health` answers "which open pull requests need someone to act?" with one read of the open pull requests and
their checks (`gh pr list --json`) and one of the branch ruleset (`gh api repos/{owner}/{repo}/rules/branches/main`,
which names the required checks, so nothing is hard-coded). It prints one line per cause, sorted by pull request
number, and changes nothing (except `--rerun --apply`, below):

```text
#7 fix-bat-cross checks-failed lint: body, markdownlint -> author
#9 research-x checks-failed code: python-ok; lint: markdownlint -> author
#12 throw-arc conflict with main -> author
#15 char-stats ci-stuck build-ok 4.2 h (limit 3.0 h) -> coordinator
#18 workouts-2 auto-merge-off into main -> coordinator
```

| Cause | When | Acts |
| --- | --- | --- |
| `checks-failed` | a **required** check failed (failure, timeout, cancelled). Its class is `lint` for the checks in `hygiene.pr_health.lint_checks` (title, body, docs, format, mkdocs, markdownlint, actionlint) and `code` for the others (`build-ok`, `python-ok`); a mix lists both | author |
| `conflict` | GitHub says the pull request conflicts with its base | author |
| `ci-stuck` | a required check has been queued or running longer than `hygiene.thresholds.pr_pending_hours` (default 3; `--pending-hours H` overrides it for one run) | coordinator |
| `auto-merge-off` | the pull request is not a draft, targets `main` and has auto-merge off | coordinator |

A failing check that is not required is ignored. A draft is never stuck. A stacked pull request (base other than
`main`) is reported as `stacked` and never as lacking auto-merge; its failures and conflicts count. GitHub answers
`UNKNOWN` for mergeability while it computes it, which means "not known yet": it is not a finding and shows only with
`--verbose`, together with the drafts and stacked pull requests, so a rerun a minute later settles it.
`--json` prints `checked`, `stuck` (each with `number`, `branch`, `cause`, `kind`, `detail`, `who`, `checks`, `url`),
`other` (the `--verbose` lines), `notices` and `labels` (see below). Exit code 0 when nothing is stuck, 1 when something
is, 2 when `gh` or the conventions cannot be read. Run it from the coordinator's loop to find the pull requests to
unstick or to push.

`--labels` also compares the labels on every open pull request with its causes and prints what would change, after the
findings: `add S-conflict to #12 (conflict with main)`, `remove S-failing from #7 (cleared)` (`would add ...` until
`--labels --apply`). `hygiene.pr_health.labels` in `.github/conventions.yml` maps the causes to labels (`conflict` to
`S-conflict`, `checks-failed` to `S-failing`); a pull request with the cause gets the label, one without it loses the
label (a draft is never stuck, so it carries neither), and no other label is ever added or removed.
`--labels --apply` makes the
changes through `gh api` on the issue labels endpoints and needs a token that may write issues; the exit code is still
the findings' (the `hygiene` workflow accepts 1). While GitHub answers `UNKNOWN` for a pull request's mergeability the
command rereads up to `hygiene.pr_health.unknown_retries` times, `unknown_wait_seconds` apart, and a pull request still
unknown keeps its conflict label as it is. The labels must exist in the repository with their colours: `repo labels
--apply` creates them from `labels.list`.

**`--rerun`** restarts CI that failed for a reason that no longer holds, so a stuck pull request does not wait for a
person to notice. It adds a line per run, `#<n> <branch> rerun <checks> (run <id>): <why> -> <outcome>`, and in
`--json` a `rerun` list (an empty list when there is nothing to rerun; the `stuck` list and the exit code are the same
with or without `--rerun`, so exit code 1 still means a pull request is stuck). Without `--apply` it is a dry run that
prints `would rerun` and changes nothing; `--apply` calls `gh run rerun <run> --failed`. It reads the
head commit and description of each pull request and, for a failed required check, the runs of the head commit and
sometimes its job log. A required check is judged stale in two ways, and a real lint, build or test failure is never
restarted:

| Stale because | How it is known | What happens |
| --- | --- | --- |
| the text was fixed | the `title` or `body` check failed and the pull request's **current** title or description passes `repo check-title` or `check-body` | status `new-event`: the report says to edit the title or description; no rerun is made |
| the runner or GitHub failed | the check concluded `CANCELLED`, `STARTUP_FAILURE` or `TIMED_OUT`, or it failed and its log matches a pattern of `hygiene.pr_health.rerun.log_patterns` (the runner was lost or shut down, an action could not be downloaded) | `would rerun` / `reran` |

Each `rerun` entry is one run, with these fields (stable; scripts such as the PR guard read them):

| Field | Type | Meaning |
| --- | --- | --- |
| `number` | integer | The pull request's number |
| `branch` | string | Its head branch |
| `run` | integer | The workflow run (an id of `gh run list`) holding the stale checks |
| `checks` | list of strings | The stale required checks of that run, sorted |
| `reasons` | list of strings | Why each is stale, in the order of `checks` |
| `status` | string | `would-rerun` (dry run), `reran`, `refused` (GitHub would not), `capped` (already rerun `max_per_sha` times) or `new-event` (a fixed title or description) |
| `detail` | string | Empty for `would-rerun` and `reran`; otherwise what happened and the new event the pull request needs |

A rerun reuses the event the run started from, with the title and description of that time, so it cannot make `title`
or `body` see a fix (observed: a rerun made after a description was fixed failed on the old text). A fixed text needs a
new event instead (an edit of the title or description, or closing and reopening the pull request), which the report
names; it is not done automatically because reopening changes the pull request's state.

The limits are in `hygiene.pr_health.rerun` of the conventions file: `max_per_sha` (a workflow is rerun at most that
many times on one head commit, counted from the attempts GitHub keeps for its runs, so restarting the tool loses
nothing; at the cap the status is `capped` and the line says to push a commit), the names of the title and
description checks, the conclusions and the log patterns. A run is restarted with `--failed` only when all its failed
jobs are stale; when it also holds a real failure, only the stale jobs are restarted (`--job`). When GitHub refuses
(`gh run rerun` fails, for example "cannot be retried"), the status is `refused` and the line carries GitHub's message
and the new event the pull request needs: push a commit for a runner failure. The reads are `gh run list --commit`
and `gh api repos/{owner}/{repo}/actions/jobs/<id>/logs`; only `--apply` writes.

### repo hygiene {#repo-hygiene}

`repo hygiene` is what the `hygiene` workflow runs ([Workflows](repo-conventions.md#workflows)); you can run it too.
It audits GitHub (`repo audit --github`, plus `--files` for a sweep) and writes the result back as comments, with the
settings in `hygiene.comments` of `.github/conventions.yml`:

* `--event N` is for issue or pull request `N` that just changed. It creates the item's one comment when it has
  findings, edits it when they change, and edits it to "found nothing to fix here now" when they are gone. A comment is
  never deleted. Findings of the checks in `hygiene.comments.report_only` (the idleness checks) are left out, because a
  comment is activity and would end the idleness it reports.
* `--sweep` is the weekly run. It keeps one report comment (counts per check, then the findings) on the open issue
  titled `hygiene.report_issue.title`, creating that issue (with the report issue's labels and a task-form body) when
  it is missing. Item comments are written by a sweep only when `hygiene.comments.on_sweep` is true, and then at most
  `max_per_sweep` of them, resolves first; it starts false, so the sweep only reports and item comments come from
  events.
* Both scan attached logs, as `repo audit --attachments` does ([Attached logs](issues.md#attachments)): an event the
  logs of its own issue, a sweep those of every open issue, within `hygiene.attachments.max_per_run` downloads. A
  download that fails is a notice (printed, and in the report), not a finding.
* Without `--apply` nothing is written: it prints `would create comment on #7: ...` for each write. `--snapshot FILE`
  audits a saved snapshot ([audit](#repo-audit)); the comments are still read from GitHub.

Only a comment by `hygiene.comments.author` (the Actions bot) whose first line is `<!-- MARKER -->` (the report's is
`<!-- MARKER:report -->`) counts as the workflow's own, so pasting the marker never makes it edit someone else's
comment. Text from GitHub quoted in a comment (a title in a finding) cannot mention anyone, link, embed an image or
close the marker, and the report writes `#12` so that it makes no cross-reference in the item's timeline. Exit code 0
when it ran, 2 when `gh` or the conventions cannot be read.

## issue {#issue}

```sh
uv run --project python coney-tools issue draft [task|bug]
uv run --project python coney-tools issue check --title T --body FILE [--form F] [--leftover] [--online] [--attach F]
uv run --project python coney-tools issue create --title T --body FILE [--label L ...] [--parent N] [--leftover]
    [--attach F] [--offline] [--dry-run]
uv run --project python coney-tools issue edit N --body FILE [--title T] [--offline] [--dry-run]
uv run --project python coney-tools issue link N --parent P [--dry-run]
```

Drafts, checks and files issues in their form's exact shape ([Filing an issue](issues.md#filing)). Every rule is
read from `issues` in `.github/conventions.yml` and from the form files in `.github/ISSUE_TEMPLATE/`; nothing is
copied into the code.

* `draft` prints a blank body: the form's `###` headings in order, each preceded by an HTML comment with the form's
  description, required or optional, and the allowed values (the Area's `A-` labels, a bug's dropdown choices), then
  the `From:` line. The form is `task` unless you name `bug`.
* `check` validates a title and a body file (`-` reads stdin) and prints one line per problem, tagged with its kind
  (`title`, `headings`, `empty`, `choices`, `screenshots`, `from`, `links`, `wording`, `status`); exit 0 when there is
  none, 1 otherwise. A dropdown answer is the option as written under its heading, with or without a list bullet or
  backticks (`--play-level` is an option, not a bullet). The form comes from `--form`, else the title prefix, else
  the headings. Docs and research pages and their `{#anchor}`s are looked up in `origin/main` of the checkout with
  git, offline; a missing `origin/main` is a note, not a pass. `--leftover` wants the `From:` line. `--online` reads
  the state of each cited `#N` with `gh` and compares it with the words "open", "merged" and "closed" beside it.
  `--attach` says local screenshots will be uploaded, so a local image in the Screenshots section is accepted.
* `create` runs `check` (the states online unless `--offline`), then `gh issue create` with the form's labels and
  the Area's `A-` label. `--label` adds a label (checked against `labels.list`; a type or status label replaces the
  form's). `--parent N` links the new issue as a sub-issue of N through the sub-issues API, and tells you the
  `issue link` command to run if only that step fails. `--dry-run` checks and prints the `gh` commands, running none.
* `edit` runs the same check on the new body (with `--title`, or the issue's current title read with `gh`) before
  `gh issue edit`. `link` makes an existing issue a sub-issue of another.

The body is filed without the draft's comments. Exit 2 when the conventions, a form file or the body file cannot be
read.

## check {#check}

```sh
uv run --project python coney-tools check changed [--base origin/main] [--build-dir build/dev] [--only STEP ...]
    [--skip STEP ...] [--jobs N] [--clang-tidy PATH]
```

The local gate for the files a change touched ([Testing](testing.md#local-gate)): run it in your worktree before you
hand a unit over. The changed files are everything added or modified since your branch left `--base` (the merge
base), committed or not, with untracked files. Each step runs only when the change touches what it checks, prints one
line, and any failure makes the command exit 1:

| Step | Runs when | What it does |
| --- | --- | --- |
| `repo` | always | `repo audit --files`: `.github/conventions.yml` loads, and the agent pointer files, the generated blocks, the issue forms, the pull request template, the CI scope script and pre-commit's size limit agree with it |
| `pre-commit` | a file changed | `pre-commit run --files` on the changed files: clang-format, ruff, whitespace, the 512 KB guard, the real-clock rule |
| `clang-tidy` | C++ under `src/` or `tests/` changed | clang-tidy on the files CI's pull-request job would check, with the compiler warnings the GCC and Clang builds enable |
| `docs` | a file matching `paths.docs_only` of `.github/conventions.yml` changed (`docs/`, `research/`, top-level Markdown, `mkdocs.yml`, the MkDocs hooks and theme overrides, the shared artwork, `requirements-docs.txt`, ...), or the python the build imports (`python/src/coney_tools/progress*.py`, `config.py`) | [`docs check`](#docs) on the changed pages, then `mkdocs build --strict` into a throwaway folder; both run, so one run shows every problem |
| `python` | `python/` or `.claude/` changed (the tests also run the hooks) | `ruff check` and `ruff format --check` over `python` and `.claude/hooks`, `pytest python/tests` and `mypy --config-file python/pyproject.toml python/src python/tests` |

**Which files clang-tidy checks.** The same rules as CI (`.github/scripts/tidy-files.sh`): a changed `.cpp` itself, a
changed header through every `.cpp` that includes it, directly or through other headers. A test keeps the two in
agreement. When the build or tidy setup changed (`CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `.clang-tidy`, the
build workflow) the nightly CI run checks every file, an hour's work; the command says so and checks the changed ones
only. Pull requests get no clang-tidy in CI, so this is their gate ([building](building.md#clang-tidy)).

**How it runs clang-tidy.** With the repository's `.clang-tidy` and the `compile_commands.json` of `--build-dir`
(every preset writes one; configuring is enough, no build, and a stale or missing entry is reported with the
`coney-tools build --configure-only` to rerun). It finds clang-tidy 19 as `clang-tidy-19` on `PATH`, in Homebrew's
`llvm@19`, in the Visual Studio LLVM folder that `vswhere` names, or as `clang-tidy` on `PATH` or in Homebrew's `llvm`
(the first of version 19 wins; another version is used with a note), or from `--clang-tidy`. A `ccache` or `sccache`
launcher in the database is removed from the copy clang-tidy reads, as CI's job does. The compile database of an MSVC
build makes clang-tidy run in clang-cl mode, where MSVC's `/W4` misses what GCC and Clang reject (a designated
initializer that leaves a field out, `-Wmissing-designated-field-initializers`). So the command adds `-Wall -Wextra
-Wpedantic` (the flags of `cmake/coney_target.cmake`, passed as `/clang:-W...` in that mode) and lets the compiler's
diagnostics through the `-*` of `.clang-tidy`; with the config's `WarningsAsErrors` they fail like CI's `-Werror`.
`--jobs` runs that many files at once (default: a build's share, the cores divided by `build_slots`, as `coney-tools
build -j` defaults: the gate runs outside the build slots, beside the builds that hold them). Every program the command
starts runs below normal priority, like a build's (Windows: below-normal priority class; elsewhere `nice -n 5`).

**Linux and macOS: use a Clang database.** On Windows the `dev` preset uses MSVC and the command adapts. On Linux the
default compiler is GCC, whose database can carry flags clang-tidy rejects, and CI tidies a Clang 19 build. The
command notes a GCC database; configure a Clang one once, beside `build/dev`, with [`build`](#build) (it sets the
compilers for the run and configures a second folder; a hook refuses a raw `cmake`), and point the command at it:

```sh
uv run --project python coney-tools build --agent ID --configure-only --preset dev --build-dir build/tidy \
    --env CC=clang-19 --env CXX=clang++-19         # macOS: the Apple or Homebrew clang
uv run --project python coney-tools check changed --build-dir build/tidy
```

CI runs the `repo` and `pre-commit` steps on Windows, Linux and macOS in its `python` workflow, so the command's
handling of paths, programs and long command lines is tested on all three.

`--only` and `--skip` take step names, for a rerun of the one that failed. The command prints at most 40 lines of a
failing step; run the step's tool by hand for the rest. What stays CI's: warnings only GCC emits, macOS, the
sanitizers and the full clang-tidy pass (see [the local gate](testing.md#local-gate)).

## docs {#docs}

```sh
uv run --project python coney-tools docs check [PATHS ...] [--base origin/main] [--summary] [--quiet]
```

Checks Markdown pages against the standard of [Writing these docs](writing-docs.md#checking): the skeleton, the
"Verified against" line and the evidence levels of a research page, and the Style rules every page keeps. Writing-docs
owns the rules and the heuristics; this entry is the command. Without `PATHS` it checks the pages changed since
`--base` (committed or not, untracked included), as [`check changed`](#check) does in its `docs` step and the `docs`
workflow does on a pull request. With `PATHS` it checks those files, or every page under a folder, whether or not they
changed. It prints `path:line: rule: message` for each finding (`--quiet` prints only the counts), a warning (a long
page) never fails, and `--summary` adds how many pages of each kind pass and how many pages break each rule: run it
on `docs` for the standing of the whole tree. Exit 0 when no page has an error, 1 when one has, 2 when the command is
not run inside a checkout, the conventions cannot be read, `--base` is unknown, or a named path is missing, outside
the checkout or not a page the check covers (`README.md`, a generated page).

## build {#build}

```sh
uv run --project python coney-tools build --agent ID [--preset dev|release|ci|asan] [--test] [-R REGEX] [-L LABEL]
    [--target NAME ...] [--configure | --configure-only] [--build-dir FOLDER] [--env NAME=VALUE ...] [--priority]
    [--max-wait MIN] [--budget MIN] [--detach] [-j N] [--dir CHECKOUT]
uv run --project python coney-tools build status [--json] [--dir CHECKOUT]
uv run --project python coney-tools build result [--json] [--dir CHECKOUT]
uv run --project python coney-tools build release --agent ID [--force] [--slot N] [--lane main|fast] [--dir CHECKOUT]
```

`--dir` names the checkout the command is about and works before or after the subcommand (`build --dir X result` and
`build result --dir X`).

The one way an agent configures, builds and tests the engine. Several agents share one machine and a full build uses
every core, so a build takes a **slot** like a PCSX2 copy takes a claim, and at most `build_slots` (default 2) run at
once. The command replaces `cmake --preset`, `cmake --build --preset` and `ctest --preset`; a hook in
`.claude/settings.json` refuses those, `ctest` and `ninja` typed directly in a shell
([`.claude/hooks/build_guard.py`](repo:.claude/hooks/build_guard.py)). The tool's own subprocesses are not shell
commands, so the hook never sees them.

**What a run does.** From the checkout (the current one, or `--dir`):

1. *Precheck*, before it waits for anything: `CMakePresets.json` names the preset, and `cmake`, `ctest` and `ninja`
   are found. On Windows it runs `vcvars64.bat` once and captures the environment (found with `vswhere` in the
   installer folder under `Program Files (x86)`, which is not on `PATH`; inside a Developer shell it does nothing), so
   no special shell is needed.
2. *The build folder's lock*, held to the very end (see "One run per build folder" below).
3. *Configure and build*, one claim: `cmake --preset P` when the build folder has no `CMakeCache.txt` or with
   `--configure`, then `cmake --build --preset P -j N`, through the [compiler cache](building.md#compiler-cache).
   `--configure-only` stops after the configure (the compile database `check changed` reads). `--target NAME`
   (repeatable) builds only those targets and what they need: `--target coney` is a play build, the executable
   without the roughly thousand test files of `coney_tests`. It is refused with `--test` (the tests would run an
   old `coney_tests` or `coney`) and with `--configure-only`.
4. *Test*, with `--test` and only after a successful build, a claim of its own (it queues again between the two, so a
   long test run does not hold a slot against other agents' builds longer than it must): `ctest --preset P -j N`,
   with the filters `-R REGEX` and `-L LABEL` when given (see "Choosing tests" below).

**The log.** The full output goes to a file of its own per run, `build-<preset>-<agent>-<date>-<time>.log` in the
worktree's folder of the scratch directory (`scratch/<worktree>/`; the process id is added if that name exists), and
its path is the first line printed. No two runs share a log, so one never garbles another's; the newest 20 logs of a
worktree are kept. What is printed after is short: a line per step (outcome, time, lane, how many steps were out of
date, time spent waiting), a line on the compiler cache after the build (`compiler cache: 980 hits, 12 misses
(98.8 %)`: this run's calls alone, from a stats log of its own, though other builds share the cache; or `off` and
why, or `not used` for a folder configured before the cache was on), the first compile, link and CMake errors (MSVC,
GCC and Clang forms) and the failed steps,
the failed tests by name (the first eight, then a count; the counts and the names are ctest's own, read from its "The
following tests FAILED:" list up to its blank line or "Errors while running CTest" line, labels after a state
included, so a test's output that looks like a list entry is never taken for a failure; without that list, or when it
holds fewer names than ctest counted, the `***Failed` and `***Timeout` lines supply the rest) with `N passed, M failed
of T`, and a last `result:` line that names the failing step or the first failing tests. A command that exits non-zero
with no error, no failed step and no failed test in its output says so plainly, `stopped from outside (no failing step
found)`: it was killed, timed out or interrupted by something else. Exit code `0` on success, `1` on a failed step or
a filter that selects no tests, `2` on a precheck or usage problem, `75` when it gave up waiting for a slot or for the
build folder, `130` on an interrupt or when its caller is gone. The children run at below-normal priority, on Windows
with no console window of their own (`CREATE_NO_WINDOW`, so a build started from a background task opens no terminal
that takes the focus), and never outlive the command (below).

**Process lifetimes.** An orphaned `ctest` goes on starting tests after its caller is gone, and killing the tests one
by one only lets it start the next, so nothing the command starts may outlive it
([`process_lifetime.py`](repo:python/src/coney_tools/process_lifetime.py)):

* *The command is killed.* On Windows it puts itself in a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` before
  it starts anything; `cmake`, `ninja`, the compilers, `ctest` and every `coney_tests` inherit the job, and when the
  command ends, however it ends (Task Manager included), Windows ends them all. On Linux and macOS each child runs in
  a session of its own, so its whole tree is one process group that the command stops on an interrupt, `SIGTERM` or
  `SIGHUP`; on Linux the child also gets `SIGKILL` when the command dies by `SIGKILL` (`PR_SET_PDEATHSIG`).
* *Its caller is gone.* A watch thread looks every two seconds at the command's ancestors, up to the shell that ran it
  (through `uv` and Python's launchers; under Git Bash, as far as the native chain goes). When one has ended (the
  agent tool restarted, the background shell was killed), the command stops its child, starts nothing more and
  leaves as on an interrupt, releasing its claims (by force 15 seconds later if a wait does not notice); exit `130`.
* *An older run died without these guards.* When a claim or a folder lock whose process is gone is taken over or
  released with `build release`, the build programs that run started (`cmake`, `ctest`, `ninja`, `coney_tests`: its
  direct children by parent pid, started after the claim was made, as Windows reuses pids) are stopped with everything
  under them, and the run that took over says `stopped N process trees <agent>'s run left running`. A claim that is
  stale only by age (its pid lives) is taken over without stopping anything, as the pid may be somebody else's by now.

**Parallelism.** `-j` defaults to the cores divided by the slots (at least 1), so two builds share the machine
instead of each starting a job per core; `-j N` overrides it, for both the build and `ctest`.

**The slots.** Each run holds a claim: a folder `slot-N.claim/` with `owner.json` (agent, worktree, preset, the step it
is at, pid, start time) in the claims directory, built whole in a private folder and renamed into place, so exactly one
caller wins and none sees a half-made claim. The directory is `build-claims/` in the shared scratch folder, the main
checkout's `../../scratch`, whatever a worktree's `scratch_dir` says (keys `build_claims_dir` and `build_slots` in
[`coney.local.toml`](workspace.md#local-config)). Nothing is cached: `status` is rebuilt from the folders and the live
processes every time. A claim is released on success, on failure and on an interrupt, and is **stale** (taken over by
the next claimer, who says so and stops what the dead run left running; see "Process lifetimes" above) when its process
is gone, or when it is older than four hours with a process that lives (a hung build, or a reused pid). Takeovers go
through a lock folder, as the PCSX2 claims do.

**The queue.** With no slot free, the build waits in a first-come queue of ticket files (`queue/`). Only the head of
the queue may claim, so waiters cannot overtake one another; `--priority` tickets go ahead of normal ones (for the
coordinator, or a build somebody is waiting on). A ticket whose process is gone is dropped. While it waits, the
command prints its place, a rough wait (from the holders' recorded step and how long they have run it) and the
holders, every minute. **It does not wait for ever:** after `--max-wait` minutes (default and most 40, per claim;
also spelled `--slots-wait-timeout`), so that an agent never outlives its one-hour prompt cache, it leaves the queue and
exits `75` with `still queued, position N of M, try again later` and the way on: do other work and run the same
command again, or start it with `--detach`, write `STATUS.md` and stop ([Build waits](agents.md#build-waits)).

**The time budget.** `--max-wait` caps one wait; `--budget` (minutes, default and most 45) caps the whole call,
waits and steps together, so an agent's wait plus its build stays inside the hour its prompt cache lasts. A larger
`--max-wait` or `--budget`, or `--budget 0` (no budget), is refused with exit `2` unless the run is detached. Each
finished step's duration is kept in `durations.json` in the claims directory, the last ten per preset, step and size
(`ci/configure/main`, `ci/build/fast` or `ci/build/main` by lane, `ci/test/filtered` with `-R` or `-L`, `ci/test/full`
without); a build of some targets only has them in its key (`dev/build/main:coney`) and a build through the compiler
cache `+ccache` (`dev/build/main+ccache`), because both take a fraction of a full build without the cache and would
otherwise pull its median down or up; a failed step is not recorded. While a run waits for a slot, it leaves the
queue with exit `75` as soon as
the time the call has taken plus the median of what it waits for (a phase's steps added up; with no history, the
typical 5 minutes for a configure, 15 for a build, 10 for a full test run, 3 for anything in the fast lane) would pass
the budget, and says so with the numbers: `time budget: 25 min of the call so far; the build would take about 30 min
(median of 4 runs) and the budget is 45 min`. It also leaves at its first look when the queue's own estimate of the
wait still ahead (from the holders' steps), added to those, would pass the budget: `the queue ahead is about 50 min`.
The test phase counts the build before it, and the wait for the build folder counts too. The budget only decides
whether to keep waiting: a step that has started always runs to its end, and a free slot is always taken.

**Detached runs.** `--detach` checks the run as usual (precheck, refusals), then starts the same run in a process of its
own, apart from the calling shell (on Windows `CREATE_NO_WINDOW` in a new process group, never `DETACHED_PROCESS`, which
makes Windows Terminal show an empty window for the venv launcher's child; out of the caller's job when the job allows
it; elsewhere a session of its own), and returns at once printing the log path and the child's pid. A detached run may
wait and run as long as it needs: `--max-wait` defaults to 240 and `--budget` to `0`, and neither is capped. Its
children still end with it, but nothing watches the shell that started it. It records itself in `build-result.json` in
the worktree's scratch folder (state, agent, preset, pid, when that process started, log, start time; at the end the exit
code, the printed summary and the end time; its console output goes to `build-detached.out` beside it), however it ends,
a refused precheck included. A second `--detach` in a worktree whose detached run still runs is refused. `build result`
prints that record: the run's own exit code and summary once it has ended, `75` and `still running` while it runs, and
`1` with `ended without a result` when its process is gone without writing one (killed); `--json` prints the record
itself. "Still running" means the recorded pid lives **and** that process started when the record says: the system
hands a dead process's pid to a later one, and a stale record once kept a worktree "busy" for good because another
program had been given the pid. A pid whose process began at another time is gone (an older record without the start
time is judged by the record's own time: a process that began long after it is a stranger), and a start time that
cannot be read counts as running.

**The fast lane.** A small job should not queue behind a full build. Before claiming, the command measures the job.
For a build: the manifest is brought up to date for real with `ninja build.ninja` (it re-checks the `CONFIGURE_DEPENDS`
globs, which is cheap, and re-runs CMake only when a CMake file or a glob changed, which the build would do first
anyway; under the folder's lock, outside a slot), then `ninja -n -d explain` (of the `--target` targets only, when
given) runs on a *copy* of `build.ninja`, and the
**causes** it names are counted: each "output x older than most recent input y", "output x doesn't exist" or "command
line changed for x". A plain `ninja -n` is no use here: the globs make ninja want to re-run CMake first, and a dry run
stops there having planned nothing; and a dry run on a copy plans too much, because it cannot ask a `restat` step
whether its output really changed, and everything behind such a step is planned too (CMake's module dependency collation
was one, before [module scanning](building.md#module-scanning) was turned off: 410 planned steps for a touched source
whose real build was 10). The causes are what a real build must do whatever the `restat` steps find: one per touched
source, and one for each source that includes a touched header. For a test run, `ctest -N` with the same `-R` and `-L`
counts the tests selected. A build with fewer than `build_fast_lane_steps` causes (default 50) or a filtered test run of
at most `build_fast_lane_tests` tests (default 20) takes the **fast lane**: one extra slot of its own (`fast-1.claim/`,
queue `queue-fast/`), so at most one such job runs at once, with a short queue of its own. A first build, a
`--configure`, a build that cannot be measured (the manifest refresh failed, ninja printed no cause) and any test run
without a filter take a main slot. A threshold of `0` turns the lane off. The summary line says what decided it ("fast
lane, 2 steps out of date").

**One run per build folder.** Two runs in one build folder collide: a second build's link fails
with `LNK1168` while the first's tests hold `coney_tests.exe`, and the logs and the dependency log are written by both.
So each build folder (the checkout, the preset or `--build-dir`) has a **lock**, taken before anything else and held from
the configure to the last test, however the run ends. A second run in the same folder **waits** for it, in a queue
like the slots' (`claims/folders/<worktree>-<folder>-<digest>/`, the same stale rules: the holder's process is gone, or
four hours), printing who holds it and the step it is at every minute, and gives up after `--max-wait` minutes with
exit `75` as for a slot. Runs in other folders, other worktrees or other presets are not held up by it. The lock
covers the run from its measuring on, so the manifest refresh and the dry run never overlap a build either, and
a run that waited looks at the folder again once the lock is its own (the run before it may have configured it).

**Choosing tests.** `-R REGEX` runs the tests whose names match and `-L LABEL` those with a label that
matches (ctest's two filters; both together select the tests that satisfy both). The command lists the selection
first (`ctest -N`), takes the fast lane for 20 tests or fewer and fails, naming the filter, when nothing matches. Every
test that needs the player's disc has the label **`disc`**: the Catch2 tag `[disc]` of a test case becomes a ctest label
(`catch_discover_tests(... ADD_TAGS_AS_LABELS)`), and the `coney --disc` checks that exist only when `CONEY_DISC` is set
at configure time carry it too. So `build --test -L disc` runs exactly the disc tests (they skip without the disc), and
every other tag is a label as well (`-L routes`). `-L` matches a part of a label name: `-L '^disc$'` is the exact one.
`-R` is a **CMake regular expression**, and the tool passes it to ctest exactly as it was typed (a test checks that a
regex with `^ ( | ) $ . + \` and spaces reaches ctest unchanged), so a test that matches nothing is usually the
expression. Test names hold spaces, `.`, `+`, parentheses, `:` and quotes, and `. + ( ) [ ] * ? ^ $ |` mean something to
the expression: escape the ones that are part of a name with a backslash (`'^(L1 \+ R1 with a full rage meter starts
rage|coney\.help)$'`), and quote the argument against your shell with single quotes (PowerShell and bash both leave
those alone; a double-quoted `$` or backtick is the shell's). Without anchors a name matches as a substring, which is
often enough: `-R 'rage meter'`. Preview a filter without a run: `ctest --preset P -N -R ... -L ...` lists what it
selects, and the hook allows it.

**Another build folder.** `--build-dir FOLDER` (relative to the checkout) configures, builds and tests in that folder
instead of `build/<preset>`, with the preset's settings (`cmake --preset P -B FOLDER`, `cmake --build FOLDER`, `ctest
--test-dir FOLDER`), for a second configuration beside the first, such as the Clang one that
[`check changed`](#check) reads for clang-tidy. `--env NAME=VALUE` (repeatable) sets an environment variable for the
run, such as `CC=clang-19`; CMake reads `CC` and `CXX` at a folder's first configure and keeps them in the cache.

`build status` prints both lanes: each slot's holder, worktree, preset, step and minutes, the queue in order (place,
agent, priority, minutes waiting) and the stale entries; `--json` prints the same as one object. `build release
--agent ID` is manual cleanup: it frees the slots a crashed or stuck run of that agent left, stops what a crashed run
left running, and drops its tickets. A slot whose process still runs, or another agent's, needs `--force`, which frees
the slot but does not stop the process. How agents use it, and what exit `75` asks of them: [Builds and tests](agents.md#builds).

## coord {#coord}

`coord status` answers "what waits for the coordinator?" in one read. Anyone can run it at a prompt, and the
coordinator's [Tether](#coord-wakes) runs it to wake the session when new work arrives:

```sh
uv run --project python coney-tools coord status            # one line per section, then the ids
uv run --project python coney-tools coord status --json --github
uv run --project python coney-tools coord status --ids --github --kind unit   # one id per line, for a poller
```

Without `--github` it reads only the machine, in a few seconds, and never touches the network. It reads:

* the PCSX2 copies (the rows of `pcsx2 status --json`);
* the build slots and queues (`build status --json`);
* each worktree's last detached build (`build result`);
* the release queue: each `PR.md` or `PR-<n>.md` in the scratch folder of a branch that has a worktree, and whether
  `origin` has the branch, as the local clone knows it;
* the tracks: each scratch folder's `STATUS.md`, kept while its worktree exists or for three days after its last
  change.

`--github` adds, through `gh`:

* the open issues: number, status, priority and area labels, the other labels and age, but no titles;
* the open pull requests: branch, draft, mergeability, auto-merge, the required checks in one word, and the causes
  the [`repo pr-health`](#repo-pr-health) judge finds, whose report is included whole;
* the CI queue's counts (`gh run list --status queued` and `--status in_progress`);
* for each unit file, the pull request that released it: an open one for its branch, or one merged or closed after
  the file last changed. A unit with neither is queued.

`--json` prints one object: `time`, `checkout`, `github`, the sections `worktrees`, `pcsx2`, `builds`, `results`,
`tracks`, `release`, `issues`, `prs` and `ci`, and `action`. The last three sections are `null` without `--github`.
Each section has `error`, `null` when it was read. A section that could not be read holds only `error`, and the
others stand, so a failure is never taken for "nothing waits". The exit code is 0, and 2 when the command cannot run
at all (outside a checkout). `action` lists the ids of what waits, sorted:

| Id | When |
| --- | --- |
| `issue:<n>:S-needs-triage` | an open issue carries `S-needs-triage` |
| `pr:<n>:<cause>` | `repo pr-health` calls the pull request stuck for that cause |
| `unit:<branch>:<file>` | a unit file is queued |
| `build:done:<worktree>` | the worktree's detached build ended within the last hour |

An id stays the same while the thing waits, and leaves the list once it has been dealt with, so a reader can tell new
work from old by comparing lists. The `issue`, `pr` and `unit` ids need GitHub and appear only in a `--github` read.

`--ids` prints only the ids, one per line, for a program that watches them, and cannot be combined with `--json`.
`--kind issue|pr|unit|build` (repeatable) reads only the sections those kinds come from, and prints only their ids,
so a poller pays for one read. A GitHub kind needs `--github` (exit 2 without it). When a section the ids come from
could not be read (for `unit` and `build` ids that includes the worktree list), `--ids` prints nothing, names the
section and its error on stderr and exits 1, so a watcher reports a failed read instead of an empty list.

### Waking the coordinator {#coord-wakes}

Between turns the coordinator's session sees nothing, so new work would wait until someone typed. The
[Tether](https://github.com/Ryubara/tether) plugin, which the maintainer runs in every session, watches for it and
wakes the session when it arrives. It needs Tether 1.0.0-alpha.31 or later. Tether reads `.claude/tether.json` in the
session's folder. The repository commits no such file, because Tether would then apply it to every agent's worktree
too, and the maintainer's own file holds machine settings. The coordinator's local file (listed in
`.git/info/exclude`) carries the `watch` settings below. This block is the source those settings are copied from:
change it here first, then in the local file. `python/tests/test_coord_cli.py` checks that every `coney-tools`
command in it parses.

```json
{
  "watch": {
    "storeName": "coney",
    "wakePreamble": "Coordinator: new work. Handle it or say why it can wait:",
    "idleWakeMinutes": 0,
    "pollers": [
      {
        "id": "triage",
        "kind": "issues",
        "title": "Issues to triage",
        "labels": ["S-needs-triage"],
        "everySeconds": 120,
        "remindMinutes": 30,
        "remindMaxMinutes": 240
      },
      {
        "id": "release",
        "title": "Release queue",
        "command": ["uv", "run", "-q", "--project", "python", "coney-tools", "coord", "status", "--ids", "--github", "--kind", "unit"],
        "everySeconds": 120,
        "timeoutMs": 240000,
        "remindMinutes": 30,
        "remindMaxMinutes": 240
      },
      {
        "id": "builds",
        "title": "Detached builds",
        "command": ["uv", "run", "-q", "--project", "python", "coney-tools", "coord", "status", "--ids", "--kind", "build"],
        "everySeconds": 15,
        "timeoutMs": 90000
      },
      {
        "id": "prs",
        "kind": "prs",
        "title": "Open pull requests",
        "command": ["uv", "run", "-q", "--project", "python", "coney-tools", "repo", "pr-health", "--json"],
        "everySeconds": 300,
        "timeoutMs": 240000,
        "worktrees": "<workspace>/worktrees/coney",
        "busyMarker": ".coney-busy",
        "assignHours": 4
      }
    ]
  }
}
```

Replace `<workspace>` in the `prs` poller with the folder that holds `repos/` and `worktrees/` on your machine. What
each poller wakes for:

* **`triage`** wakes for each issue waiting for [triage](issues.md#triage): an open issue carrying `S-needs-triage`,
  read every two minutes. It wakes once per issue, keyed by its number, so the coordinator's own comment does not wake
  it again.
* **`release`** wakes for each `unit:` id, a [release queue](how-we-work.md#release-queue) file with no pull request
  yet, read every two minutes.
* **`builds`** wakes for each `build:done:` id, a detached build that ended within the hour, read every 15 seconds.
* **`prs`** is Tether's pull-request guard, reading health through `repo pr-health`. It wakes for stuck pull
  requests and keeps their owner and waiver records.

Tether starts a turn only for ids it has not told before, and never while a turn runs or you type. Ids still waiting
are told again 30 minutes later, then after 60, 120 and at most every four hours (`remindMinutes`). The `builds`
poller has no reminders: a build result is news once.

A wake the engine refuses (after you interrupt a turn) shows `wake refused`. It is tried again two minutes later, or
as soon as your next turn starts. A wake that started no turn is sent again.

`idleWakeMinutes` above 0 also wakes the session after that many idle minutes with nothing new, but never while a
subagent is at work. A poller whose read fails shows as failing in the status line and in `/tether-watch`. Its ids
are never taken for "nothing waits". `/tether-watch` reports each poller's last read.

## ghidra {#ghidra}

```sh
uv run --project python coney-tools ghidra setup [--disc DISC] [--no-names] [--any-release] [--project-dir DIR]
    [--project-name NAME] [--overwrite] [--dry-run]
uv run --project python coney-tools ghidra import-names [--program NAME] [--listing FILE] [--project-dir DIR]
    [--project-name NAME] [--overwrite] [--dry-run]
```

`setup` makes a contributor's own Ghidra project of `SLUS_212.15`: it fetches the pinned Ghidra and the Emotion
Engine extension when `ghidra_install` has none, compiles `r5900.sla`, imports and analyses the executable from the
disc (`--disc`, else `game_dir`) into `<ghidra_projects>/warriors` after checking its SHA-1 against the NTSC-U
release, and then runs `import-names`. `import-names` applies the names of `docs/progress/ghidra-functions.tsv` to the
program by address with a Ghidra script run by `analyzeHeadless`, and prints how many were matched, created and
skipped. Both need a JDK 21, skip what is done, print counts and hashes only, and keep their logs in `ghidra/` in the
scratch folder; `--dry-run` changes nothing (for `import-names` it opens the program read-only and prints the counts a
real run would give). Exit codes: 0 done, 2 a missing piece, a refused disc or a failed Ghidra step (the message names
its log). What each step does and what the counts mean: [Starting your own project](ghidra.md#own-project).

## mcp {#mcp}

```sh
uv run --project python coney-tools mcp serve ghidra-mcp|pcsx2 [--copy NAME]
```

Runs one of the debugging servers that the repository's `.mcp.json` registers for agent tools, with stdin and stdout
passed through to it: `ghidra-mcp` is ghidra-mcp's Python bridge from `ghidra_mcp_repo` (run by uv, talking to the
headless server on port 8090 unless `GHIDRA_MCP_URL` says otherwise), `pcsx2` is the PCSX2-MCP fork's Node.js server
from `pcsx2_mcp_dir` (its `pcsx2-mcp-server/`). The server connects to nothing until `pcsx2_connect`; with
`--copy NAME` it starts with that copy's ports in `PCSX2_DEBUG_PORT` and `PCSX2_PINE_PORT`, which a fork server from
the `env-default-ports` change on takes as `pcsx2_connect`'s defaults (an older build ignores them, so pass the ports
to `pcsx2_connect` anyway). The checkout is `CLAUDE_PROJECT_DIR` when the agent tool sets it, else the current
folder; a worktree reads the main checkout's `coney.local.toml`. A missing key, folder or program (`uv`, `node`)
prints one line on stderr and exits with code 2, so a server that is not installed fails without affecting the
others. Why and how it is set up: [Setting up an agent tool](agents.md#setup).

## workspace {#workspace}

```sh
uv run --project python coney-tools workspace init [--workspace DIR] [--repo-url URL] [--fetch TOOL ...] [--dry-run]
```

Creates the missing folders of the [local workspace](workspace.md), clones the repository when run outside one
(`--repo-url`), writes `coney.local.toml` from the example when there is none, checks the prerequisites and, with
`--fetch ghidra|ghidra-mcp|pcsx2-mcp|ccache|all`, downloads the research tools and, on Windows, the
[compiler cache](building.md#compiler-cache) at pinned versions, checked by SHA-256 (the two MCP projects are cloned
at a pinned commit; PCSX2 comes from the fork's own build, `pcsx2 setup-copies`; on Linux and macOS ccache comes from
the package manager).
Every step already done is skipped; `--dry-run` only prints them. It never downloads game files or a BIOS. Exit
codes: 1 when a prerequisite of the engine is missing, 2 for a usage error. The launchers `bootstrap.sh` and
`bootstrap.ps1` install uv (a pinned version, with Astral's installer, which adds it to `PATH`) and run it. What each
step does and the pinned versions: [Bootstrap](workspace.md#bootstrap).

```sh
uv run --project python coney-tools workspace clean [--apply] [--build-hours N] [--worktree-hours N] [--fetch]
    [--min-gb N] [--margin-gb N] [--shell-hours N] [--log-idle-hours N]
uv run --project python coney-tools workspace disk [--min-gb N]
```

`clean` removes the worktrees whose work is merged (with their local branches) and deletes the `build/` folders
nobody has touched for `--build-hours` (default `workspace.build_idle_hours`, 4), skipping any worktree holding the
busy file (`workspace.busy_marker`, `.coney-busy`). When the drive is still under `--min-gb` (default
`workspace.disk_min_free_gb`, 30) after that, it deletes more `build/` folders, oldest last use first and never a
worktree's sources, until `--margin-gb` (default `workspace.disk_margin_gb`, 10) above the floor is free, skipping
worktrees that are busy or hold or wait for a build slot (disk pressure). It is a dry run without `--apply`, and
reports each deletion, the bytes reclaimed and the free space before and after. It also lists, and with `--apply`
stops, the **stale processes**: shells older than `--shell-hours` (default 3) waiting in a sleep loop on files
unchanged for `--log-idle-hours` (default 1), and orphaned `ctest` or `coney_tests` runs whose parent is gone.
`disk` prints the workspace drive's free space and exits 1 below `--min-gb` (default: `workspace.disk_min_free_gb` of
[.github/conventions.yml](repo:.github/conventions.yml), 30). Rules and the busy file:
[Cleanup](workspace.md#cleanup).
