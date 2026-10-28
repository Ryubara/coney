# The coney-tools disc commands

The commands that read **your own disc**: [`wad`](#info) (`info`, `list`, `recover-names`, `extract`, `scene-info`),
[`extract`](#extract), [`audio`](#audio), [`movies`](#movies) and [`xbox`](#xbox). `wad` reads the game's archive,
`WARRIORS.DIR` and `WARRIORS.WAD`; `extract` turns every asset into open formats; `audio` reads the sound data,
`movies` the movies' headers and `xbox` the Xbox disc's archive. Everything is read in place and streamed, so the
1.4 GB WAD is never loaded into memory. The format is described in
[WARRIORS.DIR / .WAD](../research/formats/wad-dir.md). The other groups are listed in
[the command line overview](coney-tools.md#groups).

## Naming the disc

Every `wad` command takes the disc as an optional first argument, `DISC`:

* a **folder**: a mounted disc (`H:\` on Windows, `/mnt/disc` on Linux) or any folder that holds `WARRIORS.DIR`,
  `WARRIORS.WAD` and, for `recover-names`, `SLUS_212.15`;
* an **`.iso` image** (ISO 9660, 2048-byte sectors). Coney reads it with its own small reader.

Without `DISC`, the tools use `game_dir` from your `coney.local.toml` (see
[Local workspace](workspace.md)). Problems print one line, `coney-tools: <what is wrong>`, and exit with code 2.

## wad {#wad}

The `wad` group reads the game's archive, `WARRIORS.DIR` and `WARRIORS.WAD`: `wad info`, `wad list`, `wad
recover-names`, [`wad extract`](#wad-extract) and [`wad scene-info`](#scenes) follow, each taking the disc as
[named above](#naming-the-disc).

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

## recover-names

```sh
uv run --project python coney-tools wad recover-names [DISC] ../../scratch/names.txt
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
    `extract` and `recover-names` refuse an output path inside the repository, because extracted files are game data and
    must never be committed ([LEGAL.md](repo:LEGAL.md#no-game-data)). Use a
    folder beside the checkout, such as `../../scratch/`.

## scene-info {#scenes}

```sh
uv run --project python coney-tools wad scene-info [DISC]
```

Parses every record `scene_list.cnk` lists (scene headers and the segments of long scenes) with the layout on
[Scenes](../research/scenes.md#data) and prints counts and hashes only: records parsed and failed, roles, objects,
cameras and lights, total frames, broken segment chains, how many headers' frame counts differ from their parts, the
event types seen, and SHA-256 digests of the list and of the records. Records whose list names are cut to 16
characters are found by content. Exits 1 when a record fails to parse.

<!-- command-reference:begin wad -->
## Parameters of `wad` {#wad-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `wad info` {#wad-info-usage}

Entry count, sizes and a breakdown by first four bytes.

```text
coney-tools wad info [DISC] [--names NAMES]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC]` | text | none | a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml |
| `--names NAMES` | path | none | a names file, one name per line (made by `wad recover-names`) |

### `wad list` {#wad-list-usage}

One line per entry: index, offset, size, hash, name.

```text
coney-tools wad list [DISC] [--names NAMES]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC]` | text | none | a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml |
| `--names NAMES` | path | none | a names file, one name per line (made by `wad recover-names`) |

### `wad extract` {#wad-extract-usage}

Write entries to OUT_DIR (outside the repository).

```text
coney-tools wad extract [DISC] OUT_DIR [[DISC] OUT_DIR ...] [--names NAMES] [--only HASH_OR_NAME [HASH_OR_NAME ...]]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC] OUT_DIR [[DISC] OUT_DIR ...]` | text | required | [DISC] (a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml) and OUT_DIR |
| `--names NAMES` | path | none | a names file, one name per line (made by `wad recover-names`) |
| `--only HASH_OR_NAME [HASH_OR_NAME ...]` | text | none | extract just these entries |

### `wad recover-names` {#wad-recover-names-usage}

Recover names by hashing strings found on the disc.

```text
coney-tools wad recover-names [DISC] OUT_FILE [[DISC] OUT_FILE ...]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC] OUT_FILE [[DISC] OUT_FILE ...]` | text | required | [DISC] (a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml) and OUT_FILE |

### `wad scene-info` {#wad-scene-info-usage}

Parse every scene record of scene_list.cnk; counts and hashes.

```text
coney-tools wad scene-info [DISC]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC]` | text | none | a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml |
<!-- command-reference:end -->

## extract: every asset in open formats {#extract}

```sh
uv run --project python coney-tools extract [DISC] OUT_DIR [--only TYPE ...] [--verify] [--jobs N] [--xbox-disc XBOX_DISC [--xbox-index FILE] [--xbox-texture-map FILE]]
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

`--xbox-disc XBOX_DISC` adds your Xbox disc (a full image, an XISO or an extracted folder) as the preferred source: the
PS2 disc is still read whole, then each texture and movie the Xbox has a better version of is replaced in place, at the
same path, and its `index.json` record says `"source": "xbox"` (with the PS2 size kept). A texture is replaced when its
Xbox match (same picture by colour and structure) is larger; a movie by its 1280 × 720 `_hd` version when that is as
long. Everything else stays the PS2 disc's. The rules and the numbers: [Xbox
assets](../research/xbox-assets.md#asset-kinds). The Xbox disc adds about half an hour (matching the world textures) and
its `default.xbe` SHA-1 goes into `manifest.json`. Finding the Xbox resources takes a pass over its 1.7 GB archive;
`--xbox-index FILE` keeps the result (the file `xbox write-resource-index` writes): it is read when it was made from
this disc and otherwise made and written there. `--xbox-texture-map FILE` also writes which Xbox texture replaced which
PS2 one, for a reader of the disc formats ([layout](../research/xbox-assets.md#texture-map)).

<!-- command-reference:begin extract -->
## Parameters of `extract` {#extract-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

Extract every asset of your disc to open formats in OUT_DIR (outside the repository).

```text
coney-tools extract [DISC] OUT_DIR [[DISC] OUT_DIR ...] [--only TYPE [TYPE ...]] [--verify] [--jobs N]
    [--xbox-disc XBOX_DISC] [--xbox-index FILE] [--xbox-texture-map FILE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC] OUT_DIR [[DISC] OUT_DIR ...]` | text | required | [DISC] (a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml) and OUT_DIR |
| `--only TYPE [TYPE ...]` | choice | none | just these types: disc, movies, audio, scripts, textures, models, animations, levels, worlds, scenes, data, index, raw. One of `disc`, `movies`, `audio`, `scripts`, `textures`, `models`, `animations`, `levels`, `worlds`, `scenes`, `data`, `index`, `raw`. |
| `--verify` | flag | off | compare the file counts with the expected ones |
| `--jobs N` | integer | none | worker processes for decoding audio (default: up to 4) |
| `--xbox-disc XBOX_DISC` | path | none | your Xbox disc (image, XISO or folder): its larger textures and HD movies replace the PS2 ones |
| `--xbox-index FILE` | path | none | the Xbox resource index (`xbox write-resource-index`): read when it fits the disc, else built and written there |
| `--xbox-texture-map FILE` | path | none | also write the Xbox texture map there: which Xbox texture replaced which PS2 one (needs textures) |
<!-- command-reference:end -->

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

<!-- command-reference:begin audio -->
## Parameters of `audio` {#audio-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `audio info` {#audio-info-usage}

Counts of sounds, banks and music tracks, with hashes of the tables.

```text
coney-tools audio info [DISC]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC]` | text | none | a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml |

### `audio list` {#audio-list-usage}

One line per sound, music track or bank sound.

```text
coney-tools audio list WHAT [DISC]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `WHAT` | choice | required | which list. One of `sounds`, `music`, `banks`. |
| `[DISC]` | text | none | a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml |

### `audio decode` {#audio-decode-usage}

Decode one sound or music track to a WAV file outside the repository.

```text
coney-tools audio decode [DISC] NAME OUT [[DISC] NAME OUT ...] [--bank BANK]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC] NAME OUT [[DISC] NAME OUT ...]` | text | required | [DISC] (a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml), a sound or track name or 0x hash, and the WAV file |
| `--bank BANK` | text | none | decode from this bank instead of where the sound list says |
<!-- command-reference:end -->

## movies {#movies}

```sh
uv run --project python coney-tools movies list [DISC]
```

Reads the header and frame index of each movie the game names (`PSS/<name>.BIK`) and prints one line per movie:
file size, Bink revision letter, width and height, frame count, frame rate, running time and each audio track's rate,
channels, sample size, transform (DCT or RDFT) and flags; then the totals and a SHA-256 of the headers and frame
indexes. Nothing is decoded. Exits 1 when a movie is missing or its header does not hold together. What the values
mean for playback: [Movies](../research/movies.md#the-movies).

<!-- command-reference:begin movies -->
## Parameters of `movies` {#movies-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `movies list` {#movies-list-usage}

One line per movie: size, Bink revision, size, frames, rate, audio.

```text
coney-tools movies list [DISC]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[DISC]` | text | none | a folder (mounted disc) or .iso image; default: game_dir |
<!-- command-reference:end -->

## xbox {#xbox}

The Xbox version is an optional asset source ([Xbox assets](../research/xbox-assets.md)). The `xbox` commands read
its archive, `XBoxWad.idx` over eight volume files, from **your own Xbox disc** and print counts, offsets and hashes,
so the survey on that page can be repeated. `DISC` is required and is one of:

* a **full disc image** (Redump style, 7.8 GB): the game partition is found by its `MICROSOFT*XBOX*MEDIA` volume
  descriptor at byte `0x18300000` (XGD1) or `0x1FB20000` (XGD2);
* an **XISO**, an image of just the game partition;
* an **extracted folder** holding `XBoxWad.idx`, the `*.wad<N>` volumes and, for `match-names`, `default.xbe`.

The file system (XDVDFS) is read with Coney's own small reader and every file is streamed. Problems print one line
and exit with code 2, as for `wad`.

| Command | What it prints |
| --- | --- |
| `xbox disc-files DISC` | Every file with its size, then the file, folder and byte totals |
| `xbox info DISC [--names FILE]` | The entry count, entries and bytes per volume, entries and bytes per kind (pack, texture dictionary, model, scene record, streamed world, ...) and how many entries a names file resolves |
| `xbox list DISC [--names FILE]` | One line per entry: index, volume, offset, size, hash, and the name when known |
| `xbox match-names DISC OUT_FILE [--candidates FILE ...]` | Matches names against the index and writes them, one path below `ee_files\` per line; prints how many entries stay unnamed, by kind |
| `xbox extract DISC OUT_DIR [--names FILE] [--only HASH_OR_NAME ...]` | Writes entries as files: a named one under its path (`paks/global.pak`), the rest as `<hash>.bin` |
| `xbox resource-info DISC [--ps2 PS2_DISC]` | The resource index: packs, resources, chunks, distinct `(resource hash, chunk type)` keys, and the graphics chunks by their first two words; with `--ps2`, how many of the PS2's texture, model, animation and character resource hashes the Xbox has |
| `xbox write-resource-index DISC OUT_FILE` | Writes the resource index, `xbox-resources.bin` (where each resource's chunks lie in the volumes; [layout](../research/xbox-assets.md#resource-index)); prints its record count, size and SHA-256 |
| `xbox write-texture-map PS2_DISC DISC OUT_FILE [--xbox-index FILE]` | Matches every PS2 texture as `extract --xbox` does (into a temporary folder, deleted after) and writes the texture map, `xbox-textures.bin`; prints its record count and SHA-256. About 25 minutes |
| `xbox texture-info DISC [--ps2 PS2_DISC]` | Texture chunks by format (DXT1, DXT2/3, DXT4/5), mip count and size; with `--ps2`, the texture resources both discs share and how many hold the same number of textures |
| `xbox compare PS2_DISC DISC` | Looks every chunk of the PS2 archive up on the Xbox by its resource hash and chunk type, and every other entry whole, by SHA-1: per chunk type, how many of the PS2's distinct chunks are the same, the same size with other bytes, another size, missing from the resource or with no such resource; per kind of entry, how many are the same; the resources on one disc only ([Either disc](../research/xbox-assets.md#either-disc)). Reads both archives whole, a few minutes |

A names file has one name per line and the tools take the **last word** of each line, so the output of `wad
recover-names`, `xbox match-names`, `wad list` and `xbox list` all work. A bare name (`global.pak`, as from the PS2) is
tried in each of the Xbox folders `ee_files\`, `ee_files\paks\` and `ee_files\anims\`; a name with a folder as given.

`match-names` keeps every name from `--candidates` and every scene record's own name (plus `.scn`) that matches. It also
guesses: `level<N>.lev`, every file-name-like string in `default.xbe` and in the Lua and text entries, and every
identifier there plus `.lev`. A guess is kept only when its extension fits the kind of the entry it matches (`.lev`
on a level, `.pak` on a pack, ...), which rejects chance matches. With the PS2 names as candidates it gives the
4,038 names on the research page:

```sh
uv run --project python coney-tools wad recover-names PS2_DISC ../../scratch/names.txt
uv run --project python coney-tools xbox match-names XBOX_DISC ../../scratch/xbox-names.txt --candidates ../../scratch/names.txt
```

`--ps2` takes the PS2 disc as the `wad` commands do and reads its `WARRIORS.WAD`. `resource-info`,
`write-resource-index` and `texture-info` read the whole Xbox archive (1.7 GB) and, with `--ps2`, the whole PS2 one.

!!! warning "Game data stays out of the checkout"
    `xbox extract`, `xbox match-names`, `xbox write-resource-index` and `xbox write-texture-map` refuse an output path
    inside the repository, as `wad extract` and `wad recover-names` do.

<!-- command-reference:begin xbox -->
## Parameters of `xbox` {#xbox-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `xbox disc-files` {#xbox-disc-files-usage}

Every file on the disc with its size.

```text
coney-tools xbox disc-files DISC
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |

### `xbox info` {#xbox-info-usage}

Entries and bytes by volume and by kind.

```text
coney-tools xbox info DISC [--names NAMES]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |
| `--names NAMES` | path | none | a names file, one name per line (made by `xbox match-names`, or a PS2 names file) |

### `xbox list` {#xbox-list-usage}

One line per entry: index, volume, offset, size, hash, name.

```text
coney-tools xbox list DISC [--names NAMES]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |
| `--names NAMES` | path | none | a names file, one name per line (made by `xbox match-names`, or a PS2 names file) |

### `xbox match-names` {#xbox-match-names-usage}

Match candidate names against the index; write the matches.

```text
coney-tools xbox match-names DISC OUT_FILE [--candidates FILE [FILE ...]]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |
| `OUT_FILE` | path | required | where to write the names (outside the repository) |
| `--candidates FILE [FILE ...]` | path | none | names files to try |

### `xbox extract` {#xbox-extract-usage}

Write entries to OUT_DIR (outside the repository).

```text
coney-tools xbox extract DISC OUT_DIR [--names NAMES] [--only HASH_OR_NAME [HASH_OR_NAME ...]]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |
| `OUT_DIR` | path | required | the folder to write to |
| `--names NAMES` | path | none | a names file, one name per line (made by `xbox match-names`, or a PS2 names file) |
| `--only HASH_OR_NAME [HASH_OR_NAME ...]` | text | none | extract just these entries |

### `xbox resource-info` {#xbox-resource-info-usage}

The resource index: counts and graphics-chunk kinds.

```text
coney-tools xbox resource-info DISC [--ps2 PS2_DISC]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |
| `--ps2 PS2_DISC` | text | none | also compare with the PS2 disc (a folder or .iso, as for the wad commands) |

### `xbox write-resource-index` {#xbox-write-resource-index-usage}

Write the resource index (xbox-resources.bin) to OUT_FILE.

```text
coney-tools xbox write-resource-index DISC OUT_FILE
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |
| `OUT_FILE` | path | required | where to write it (outside the repository) |

### `xbox write-texture-map` {#xbox-write-texture-map-usage}

Match every PS2 texture and write the texture map (xbox-textures.bin) to OUT_FILE.

```text
coney-tools xbox write-texture-map PS2_DISC DISC OUT_FILE [--xbox-index FILE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `PS2_DISC` | text | required | the PS2 disc (a folder or .iso) |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |
| `OUT_FILE` | path | required | where to write it (outside the repository) |
| `--xbox-index FILE` | path | none | the resource index, as for extract |

### `xbox texture-info` {#xbox-texture-info-usage}

Texture chunk counts by format, mip count and size.

```text
coney-tools xbox texture-info DISC [--ps2 PS2_DISC]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |
| `--ps2 PS2_DISC` | text | none | also compare with the PS2 disc (a folder or .iso, as for the wad commands) |

### `xbox compare` {#xbox-compare-usage}

Look up every PS2 chunk and entry on the Xbox disc: same bytes, changed or missing (counts).

```text
coney-tools xbox compare PS2_DISC DISC
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `PS2_DISC` | text | required | the PS2 disc (a folder or .iso) |
| `DISC` | text | required | the Xbox disc: a full image, an XISO or an extracted folder |
<!-- command-reference:end -->
