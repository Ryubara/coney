# The coney-tools command line

`coney-tools` is Coney's own command line (see [Conventions](conventions.md#python)). This page covers eleven groups:
`wad`, which reads the game's archive, `WARRIORS.DIR` and `WARRIORS.WAD`, from **your own disc**,
[`extract`](#extract), which turns every asset of the disc into open formats, [`audio`](#audio),
which reads its sound data, [`movies`](#movies), which reads the movies' headers, [`xbox`](#xbox), which
reads the Xbox disc's archive, [`progress`](#progress), which keeps the progress tables of the README and the docs
current, [`natives`](#natives), which renders the script-binding masterlist, [`refs`](#refs), which builds the
game reference lists, [`missions`](#missions), which builds the mission status pages, and [`pcsx2`](#pcsx2) and
[`trace`](#trace), which record the original's per-update traces in PCSX2 and compare them with Coney's.
Everything is read in place and streamed, so the 1.4 GB WAD is never loaded into memory. The format is described in
[WARRIORS.DIR / .WAD](../research/formats/wad-dir.md).

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
`pcsx2-claims/` in the scratch folder, beside the main checkout's `../../scratch`).

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
uv run --project python coney-tools pcsx2 launch STATE --agent ID
```

Starts PCSX2 on a state file (`-fastboot -statefile`, the disc through a hard link in the scratch folder when its path
holds commas or parentheses), waits until the game runs, and leaves it running. It refuses to start when something
already serves PINE. It runs under `--agent`'s claim (made, and kept, if you hold none) and records PCSX2's pid in it;
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
