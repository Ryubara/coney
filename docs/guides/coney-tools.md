# The coney-tools command line

`coney-tools` is Coney's own command line (see [Conventions](conventions.md#python)). This page covers sixteen groups:
[`check`](#check), which runs what CI would fail on for the files a change touched, [`build`](#build), which
configures, builds and tests the engine under the machine's build slots, [`repo`](#repo), which checks
the repository's own rules (titles, descriptions, labels), `wad`, which reads the game's
archive, `WARRIORS.DIR` and `WARRIORS.WAD`, from **your own disc**, [`extract`](#extract), which turns every asset of
the disc into open formats, [`audio`](#audio), which reads its sound data, [`movies`](#movies), which reads the movies'
headers, [`xbox`](#xbox), which reads the Xbox disc's archive, [`progress`](#progress), which keeps the progress tables
of the README and the docs current, [`natives`](#natives), which renders the script-binding masterlist, [`refs`](#refs),
which builds the game reference lists, [`missions`](#missions), which builds the mission status pages, [`pcsx2`](#pcsx2)
and [`trace`](#trace), which record the original's per-update traces in PCSX2 and compare them with Coney's,
[`mcp`](#mcp), which starts the debugging servers for agent tools, and [`workspace`](#workspace), which sets up the
local workspace. Everything is read in place and streamed, so the 1.4 GB WAD is never loaded into memory. The format
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
* a progress page without its single `<!-- progress:generated -->` marker, where the build puts the tables.

`mkdocs build --strict` shows the tables on the page locally too, and fails on the same problems.

```sh
uv run --project python coney-tools progress render <folder>
```

Writes the progress page as the build fills it (`progress/index.md`) and the badge files (`progress/badges/*.json`,
shields.io endpoint JSON that the README's badges read) to `<folder>`, to look at them without a docs build. It
refuses to write while the inputs disagree, as `check` finds.

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

The `pcsx2` commands drive the original in PCSX2 over PINE, for runtime research and parity checks. What they do and
why, the scenario format and the address expressions are in [Recording a trace](research-workflow.md#recording-a-trace).
The folders come from `coney.local.toml` (`pcsx2_dir`, `game_dir`, `scratch_dir`) unless `--pcsx2-dir`, `--iso` and
`--scratch` give them; PINE must be enabled in PCSX2's `inis/PCSX2.ini`. Problems print one line and exit with code 2.

Several agents share the portable copies of PCSX2 (`pcsx2`, `pcsx2-b`, ...) through claims
([Several at once](research-workflow.md#driving-pcsx2)); any command that starts PCSX2 needs one.

```sh
uv run --project python coney-tools pcsx2 claim --agent ID [--copy NAME] [--json]
uv run --project python coney-tools pcsx2 release --agent ID [--copy NAME] [--force]
uv run --project python coney-tools pcsx2 status [--json]
```

`claim` takes a free copy (or the named one) atomically and prints its folder and PINE port; it hands back your own
claim when you hold one, and takes over a stale one (its processes gone, or older than `--max-age-hours`, default 4,
with no process) saying so. A copy running PCSX2 without a claim is never handed out. `release` closes the PCSX2 it
recorded (only while that process is still a PCSX2) and drops the claim; another agent's claim needs `--force`.
`status` lists every copy with its holder, the time, the port, whether PCSX2 runs (by pid or port) and stale claims;
it warns about copies whose ini has `[InputSources] SDL = true`. The copies are the `pcsx2*` folders with an
`inis/PCSX2.ini` under `pcsx2_root` (default: the main checkout), the claims are under `pcsx2_claims_dir` (default:
`pcsx2-claims/` in the shared scratch folder, the main checkout's `../../scratch`, whatever a worktree's
`scratch_dir` says, so every worktree sees the same claims).

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
patches of `research/traces/patches.toml` applied to its EE memory. It refuses an `OUT` inside the repository or in
PCSX2's `sstates/` folder, and refuses the copy when the state does not hold a patch's original instruction words.
PCSX2 saves states with zstd, which Python reads from 3.14 (uv installs it).

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
returns once PINE answers. It is the only way agents start PCSX2: it never takes the keyboard focus (WMI starts the
process outside the agent's process tree; why: [Driving PCSX2](research-workflow.md#driving-pcsx2)). It refuses to
start when something already serves PINE. It runs under `--agent`'s claim (made, and kept, if you hold none) and records
PCSX2's pid in it;
`--pcsx2-dir` must be that claim's copy.

```sh
uv run --project python coney-tools pcsx2 record SCENARIO --out CSV --agent ID [--state SOURCE] [--attach] [--keep-open]
```

Makes the scenario's patched state copy, starts PCSX2 on it, plays the scenario's input script and writes one CSV row
per character update, then closes PCSX2. It prints the updates recorded, the reads per poll, the time per poll and the
updates missed. The scenario names a quick-save slot (`slot`) or a state file under the scratch folder (`state`);
`--state` copies another state, `--attach` records a PCSX2 already running a patched state, and `--keep-open` leaves
PCSX2 running; the claim is released at the end when `record` made it. The CSV is a measurement of the game: it is
refused inside the repository. With
hooks among the scenario's patches it also writes each hook's call log to `<CSV stem>.<hook>.csv` beside it, and
makes the scenario's `calls` ([Hooks](research-workflow.md#hooks)).

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
    [--engage-distance METRES]
uv run --project python coney-tools trace fight-report --original TRACE... --coney TRACE... [--margin SHARE]
    [--original-gangs ID,ID,...] [--coney-gangs ID,ID,...]
```

Behaviour statistics of a gang fight, from the all-humans trace of either game (Coney's `--trace-humans`, the
original's `pcsx2 record` of the all-humans field set; [Tracing](running.md#tracing)). The two games draw different
random numbers, so a fight is compared by rates, shares and distributions, not update by update. A trace needs the
columns `step`, `human` and `gang`; a metric whose columns are missing is left out. For the whole match (`all`) and
each gang (and each human with `--humans`) it prints:

* the **time share of each goal** (`goal.NAME`), of the time the human is on its feet;
* **attacks per minute** by kind (`attack.square_per_min`, ...: a change to an attack command in `command`) and
  **blocks per minute** (`block.per_min`: `blocking`, or the block command), per human on its feet;
* **attackers on one target** (`attackers.mean`, `p90`, `max` and the shares of 1, 2 and 3 or more humans going for
  one target at the same step, from `target`);
* the **distance to the target** (`distance.p10`, `p50`, `p90`, from `x` and `y`);
* **target switches per minute** (`target.switches_per_min`) and **goal changes and interruptions per minute**
  (`goal.changes_per_min`, `goal.interruptions_per_min`: a goal pushed over another, or a stack replaced instead of
  popped; the stack is `goal0`...`goal5`, or `goal` alone);
* the **time to the first engagement** (`engage.first_s`, `median_s`, `share`: the first attack, or a target within
  `--engage-distance`, 3 m by default, in seconds from the first step);
* the **knock-out times** (`ko.count`, `first_s`, `median_s`, `last_s`: `health` of 0 or less);
* the **match's length and winner** (`match.length_s`, `match.winner`): it ends at the first step where one gang or
  none has anyone on its feet, and the gang left wins; a match still going at the trace's end has no winner.

Rates are per minute of `--hz` updates a second (30). `--exclude` leaves humans out (a spectator, the pad's player).

With `--original` and `--coney` (several recordings of each) it prints, for every metric, the original's spread
(min / median / max over its runs) beside Coney's and marks with `<<` each metric where Coney's median is outside the
original's spread (`above`, `below`, `missing`, or `different` for a winner the original never had). `--margin`
widens the spread by that share of its width. A goal or an attack one side never uses counts as zero. Gangs are paired
by rank, the lowest id first, or in the order of `--original-gangs` and `--coney-gangs` when their ids differ. The
exit code is 1 when a metric is outside the spread, 0 when none is, 2 when a trace cannot be read. With one run of the
original the spread is a point, so record several ([Comparing with Coney](research-workflow.md#comparing-with-coney)).

```sh
uv run --project python coney-tools trace mission SCENARIO --side coney|original --out CSV [--coney EXE] [--disc DISC]
    [--state STATE] [--agent ID] [--updates N]
```

Plays a mission scenario (`research/traces/missions/*.toml`) on one game with its course, an adaptive pad that plays
by what is on screen, and writes the game's event log to `CSV` (keep it outside the repository: it holds the game's
text). `--side coney` runs Coney headless with `--pad-pipe` and `--event-log` (`--coney`, `--disc` as for `trace
coney`); `--side original` starts PCSX2 on the scenario's state with its patches and event hooks, as `pcsx2 record`
does, under your claim (`--agent`), and reads the events from the hooks' ring. `--state` starts from another state
file or `slot:N` (a copy, never the slot itself); `--updates` caps the run. It prints its progress every 1000 updates
and ends when the scenario's `until` event comes, or at its `updates`.

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
uv run --project python coney-tools repo check-body [--body FILE|-]
uv run --project python coney-tools repo labels [--apply] [--repo OWNER/NAME]
uv run --project python coney-tools repo render [--check]
uv run --project python coney-tools repo audit [--files] [--github] [--local] [--json] [--only CHECK ...] [--strict]
    [--show N] [--snapshot FILE] [--save-snapshot FILE] [--workspace DIR] [--list]
uv run --project python coney-tools repo pr-health [--json] [--pending-hours H] [--verbose]
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
  shows](testing.md#pr-evidence)).
* `repo labels` prints the `gh label create` commands for `labels.list`; `--apply` runs them (the coordinator does).
* `repo render` rewrites the generated blocks of `AGENTS.md`, `CONTRIBUTING.md` and every guide under `docs/guides/`
  from the conventions: the text between a `<!-- conventions:begin NAME -->` line and the next
  `<!-- conventions:end -->` line (the commit areas and verbs, the label table, the rules for a pull request
  description, the required checks, the documentation-only paths). Run it after changing `.github/conventions.yml` and
  commit both. `--check` changes nothing and exits 1 naming each file with a stale block, as the `python` workflow does;
  a wrong marker (an unknown block name, an end without a begin, a block never closed) is a one-line error with the
  file and line, exit code 2. [Generated blocks](repo-conventions.md#generated-blocks) lists the blocks.
* `repo audit` reports where the repository departs from the conventions and changes nothing: see [audit](#repo-audit).
* `repo pr-health` lists the open pull requests that are stuck: see [pr-health](#repo-pr-health).
* `repo hygiene` writes the audit's findings back as comments: see [hygiene](#repo-hygiene).

### repo audit {#repo-audit}

`repo audit` runs the checks of `hygiene.checks` in `.github/conventions.yml`, in up to three scopes
([the checks](repo-conventions.md#audit-checks)):

* `--files` reads the checkout and needs no network (the default when no scope is given). The `python` workflow runs
  it on every pull request, and `check changed` runs it as its `repo` step. It checks the pointer files, that every
  file the conventions name exists, that the repository's address is only in the README, that the issue forms, the
  pull request template, `.github/scripts/ci-scope.sh` and pre-commit's size limit still agree with the file, and that
  the generated blocks are current.
* `--github` reads issues, pull requests, labels, the repository settings and the ruleset through `gh` (read only;
  `gh` must be signed in) and checks them: the sections of each open issue (a missing one, one left as `x`), its
  Screenshots, its labels, a closed issue that still carries an `S-` status label, the word "owner" for the person
  (say "user"), closed issues and merged pull requests that
  keep an unchecked item with no follow-up issue number, open pull requests whose description names a path on your
  machine or an image that is not uploaded, stacked pull requests, how long an item has waited, and drift between the
  labels, settings and ruleset on GitHub and the file. `--save-snapshot FILE` also writes what it read, and
  `--snapshot FILE` audits a saved snapshot instead of reading GitHub (the tests do; so can you, offline). The link
  check needs `origin/main` in the checkout (`git fetch origin`) and says so when it is missing.
* `--local` reads the workspace around the checkout: the free space of its drive, merged worktrees and stray files
  among the worktrees (`workspace clean` would remove them), and scratch folders of finished work that still hold
  captures. The limits are the `workspace` section of the file. It asks GitHub once for the merged pull requests of
  every branch, judges the worktrees and walks the scratch folders several at a time, and does not read the last
  activity of a worktree it will not report (a `build/` folder is `workspace clean`'s business), so a workspace of
  80 worktrees takes seconds, not minutes.

Each finding has the check's id, a severity (`error` or `warn`), the item (`#201`, a file, a worktree), a message and a
fix class: `auto` (a coney-tools command fixes it), `skill` (a person or the `/repo-hygiene` skill fixes it with
judgement) or `owner` (a maintainer decides). The text report groups findings by check and prints the counts;
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
number, and changes nothing:

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
`other` (the `--verbose` lines) and `notices`. Exit code 0 when nothing is stuck, 1 when something is, 2 when `gh` or
the conventions cannot be read. Run it from the coordinator's loop to find the pull requests to unstick or to push.

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
* Without `--apply` nothing is written: it prints `would create comment on #7: ...` for each write. `--snapshot FILE`
  audits a saved snapshot ([audit](#repo-audit)); the comments are still read from GitHub.

Only a comment by `hygiene.comments.author` (the Actions bot) whose first line is `<!-- MARKER -->` (the report's is
`<!-- MARKER:report -->`) counts as the workflow's own, so pasting the marker never makes it edit someone else's
comment. Text from GitHub quoted in a comment (a title in a finding) cannot mention anyone, link, embed an image or
close the marker, and the report writes `#12` so that it makes no cross-reference in the item's timeline. Exit code 0
when it ran, 2 when `gh` or the conventions cannot be read.

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
| `docs` | a file matching `paths.docs_only` of `.github/conventions.yml` changed (`docs/`, `research/`, top-level Markdown, `mkdocs.yml`, the MkDocs hooks, `requirements-docs.txt`, ...) | `mkdocs build --strict` into a throwaway folder |
| `python` | `python/` changed | `pytest python/tests` and `mypy python/src python/tests` |

**Which files clang-tidy checks.** The same rules as CI (`.github/scripts/tidy-files.sh`): a changed `.cpp` itself, a
changed header through every `.cpp` that includes it, directly or through other headers. A test keeps the two in
agreement. When the build or tidy setup changed (`CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `.clang-tidy`, the
build workflow) CI checks every file, an hour's work; the command says so and checks the changed ones only.

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
`--jobs` runs that many files at once (default: half the cores).

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

## build {#build}

```sh
uv run --project python coney-tools build --agent ID [--preset dev|release|ci|asan] [--test] [-R REGEX] [-L LABEL]
    [--configure | --configure-only] [--build-dir FOLDER] [--env NAME=VALUE ...] [--priority] [--max-wait MIN]
    [--budget MIN] [-j N] [--dir CHECKOUT]
uv run --project python coney-tools build status [--json]
uv run --project python coney-tools build release --agent ID [--force] [--slot N] [--lane main|fast]
```

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
   `--configure`, then `cmake --build --preset P -j N`. `--configure-only` stops after the configure (the compile
   database `check changed` reads).
4. *Test*, with `--test` and only after a successful build, a claim of its own (it queues again between the two, so a
   long test run does not hold a slot against other agents' builds longer than it must): `ctest --preset P -j N`,
   with the filters `-R REGEX` and `-L LABEL` when given (see "Choosing tests" below).

**The log.** The full output goes to a file of its own per run, `build-<preset>-<agent>-<date>-<time>.log` in the
worktree's folder of the scratch directory (`scratch/<worktree>/`; the process id is added if that name exists), and
its path is the first line printed. No two runs share a log, so one never garbles another's; the newest 20 logs of a
worktree are kept. What is printed after is short: a line per step (outcome, time, lane, how many steps were out of
date, time spent waiting), the first compile, link and CMake errors (MSVC, GCC and Clang forms) and the failed steps,
the failed tests by name (the first eight, then a count; the counts are ctest's own) with `N passed, M failed of T`,
and a last `result:` line that names the failing step or the first failing tests. A command that exits non-zero with
no error, no failed step and no failed test in its output says so plainly, `stopped from outside (no failing step
found)`: it was killed, timed out or interrupted by something else. Exit code `0` on success, `1` on a failed step or a
filter that selects no tests, `2` on a precheck or usage problem, `75` when it gave up waiting for a slot or for the
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
holders, every minute. **It does not wait for ever:** after `--max-wait` minutes (default 45, per claim; also spelled
`--slots-wait-timeout`), so that an agent never outlives its one-hour prompt cache, it leaves the queue and exits `75`
with `still queued, position N of M, try again later`. On `75`, do other work and run the same command again.

**The time budget.** `--max-wait` caps one wait; `--budget` (minutes, default 55; `0` turns it off) caps the whole
call, waits and steps together, so an agent's wait plus its build stays inside the hour its prompt cache lasts. Each
finished step's duration is kept in `durations.json` in the claims directory, the last ten per preset, step and size
(`ci/configure/main`, `ci/build/fast` or `ci/build/main` by lane, `ci/test/filtered` with `-R` or `-L`, `ci/test/full`
without); a failed step is not recorded. While a run waits for a slot, it leaves the queue with exit `75` as soon as
the time the call has taken plus the median of what it waits for (a phase's steps added up; with no history, the
typical 5 minutes for a configure, 15 for a build, 10 for a full test run, 3 for anything in the fast lane) would pass
the budget, and says so with the numbers: `time budget: 25 min of the call so far; the build would take about 30 min
(median of 4 runs) and the budget is 55 min`. The test phase counts the build before it, and the wait for the build
folder counts too. The budget only decides whether to keep waiting: a step that has started always runs to its end,
and a free slot is always taken.

**The fast lane.** A small job should not queue behind a full build. Before claiming, the command measures the job.
For a build: the manifest is brought up to date for real with `ninja build.ninja` (it re-checks the `CONFIGURE_DEPENDS`
globs, which is cheap, and re-runs CMake only when a CMake file or a glob changed, which the build would do first
anyway; under the folder's lock, outside a slot), then `ninja -n -d explain` runs on a *copy* of `build.ninja`, and the
**causes** it names are counted: each "output x older than most recent input y", "output x doesn't exist" or "command
line changed for x". A plain `ninja -n` is no use here: the globs make ninja want to re-run CMake first, and a dry run
stops there having planned nothing; and a dry run on a copy plans too much, because it cannot ask a `restat` step
whether its output really changed, so CMake's C++ dependency scan (one for the whole target, in front of every object)
drags every object in with it: 410 planned steps for a touched source whose real build is 10. The causes are what a
real build must do whatever the `restat` steps find: one per touched source, and one for each source that includes a
touched header. For a test run, `ctest -N` with the same `-R` and `-L` counts the tests selected. A build with fewer
than `build_fast_lane_steps` causes (default 50) or a filtered test run of at most `build_fast_lane_tests` tests
(default 20) takes the **fast lane**: one extra slot of its own (`fast-1.claim/`, queue `queue-fast/`), so at most one
such job runs at once, with a short queue of its own. A first build, a `--configure`, a build that cannot be measured
(the manifest refresh failed, ninja printed no cause) and any test run without a filter take a main slot. A threshold of
`0` turns the lane off. The summary line says what decided it ("fast lane, 2 steps out of date").

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

## mcp {#mcp}

```sh
uv run --project python coney-tools mcp serve ghidra-mcp|pcsx2
```

Runs one of the debugging servers that the repository's `.mcp.json` registers for agent tools, with stdin and stdout
passed through to it: `ghidra-mcp` is ghidra-mcp's Python bridge from `ghidra_mcp_repo` (run by uv, talking to the
headless server on port 8090 unless `GHIDRA_MCP_URL` says otherwise), `pcsx2` is PCSX2-MCP's Node.js server from
`pcsx2_mcp_dir`. The checkout is `CLAUDE_PROJECT_DIR` when the agent tool sets it, else the current folder; a worktree
reads the main checkout's `coney.local.toml`. A missing key, folder or program (`uv`, `node`) prints one line on
stderr and exits with code 2, so a server that is not installed fails without affecting the others. Why and how it is
set up: [Setting up an agent tool](agents.md#setup).

## workspace {#workspace}

```sh
uv run --project python coney-tools workspace init [--workspace DIR] [--repo-url URL] [--fetch TOOL ...] [--dry-run]
```

Creates the missing folders of the [local workspace](workspace.md), clones the repository when run outside one
(`--repo-url`), writes `coney.local.toml` from the example when there is none, checks the prerequisites and, with
`--fetch ghidra|pcsx2|ghidra-mcp|pcsx2-mcp|all`, downloads the research tools at pinned versions, checked by SHA-256.
Every step already done is skipped; `--dry-run` only prints them. It never downloads game files or a BIOS. Exit
codes: 1 when a prerequisite of the engine is missing, 2 for a usage error. The launchers `bootstrap.sh` and
`bootstrap.ps1` install uv (a pinned version, with Astral's installer, which adds it to `PATH`) and run it. What each
step does and the pinned versions: [Bootstrap](workspace.md#bootstrap).

```sh
uv run --project python coney-tools workspace clean [--apply] [--build-hours N] [--worktree-hours N] [--fetch]
uv run --project python coney-tools workspace disk [--min-gb N]
```

`clean` removes the worktrees whose work is merged (with their local branches) and deletes the `build/` folders
nobody has touched for `--build-hours` (default 12), skipping any worktree holding the busy file
(`workspace.busy_marker`, `.coney-busy`); it is a dry run without `--apply`, and reports the bytes reclaimed and
the free space before and after. `disk` prints the workspace drive's free space and exits 1 below `--min-gb`
(default: `workspace.disk_min_free_gb` of
[.github/conventions.yml](repo:.github/conventions.yml), 30). Rules and the busy file:
[Cleanup](workspace.md#cleanup).
