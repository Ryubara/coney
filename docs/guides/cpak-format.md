# The .cpak archive format

The Coney Pack, `.cpak`, is Coney's own archive: the installed game keeps its data in these files, sorted by kind and
area, each entry holding the original bytes from the disc. This page is the format's specification, complete enough to
write another reader or writer, and the design of the install folder built from it: the catalogue, the folder layout
and the order in which Coney looks an asset up. The decisions behind it are a maintainer's, on the tracking issue
[#798](issue:798); the details this page settles itself say why next to them.

**Status.** The format, the engine's reader and writer and `coney-tools cpak` are built. The folder layout, the
catalogue and the lookup order are the design for the steps that follow (#798 steps 3 to 5: the installer, the
loader, mods): today's installer still copies the disc's files as they are
([The install folder](asset-and-script-architecture.md#install-folder)).

## Why a format of Coney's own {#why}

A maintainer chose a format of Coney's own over zip, and the name `.cpak` over `.cpk`, which CRI Middleware's format
already uses. What it gives:

- **The index at the front**, so a reader learns every entry from one read at the start of the file.
- **Entries readable in place**: each starts at an aligned offset and, stored as it is, needs no copy or decoding; a
  reader that maps or loads the archive hands out a view of its bytes.
- **A hash per entry and one over the index**, so Verify can name a damaged entry and a reader refuses a damaged
  index before trusting any offset in it.
- **A compression field per entry from the first version** (none, LZ4, zstd). Installs write none until compression is
  measured on the real archives (#798 step 6).

Every entry keeps the disc's bytes (RenderWare textures and models, the game's audio, compiled Lua, text tables), so
Coney's loaders stay ports of the original's and the base game is exact. Conversion to open formats happens only in
Coney Studio's export and import.

## The file {#file}

All numbers are little-endian. An archive is, in order:

```text
offset 0       header, 64 bytes
offset 64      index: entry records (64 bytes each), then the names
               zero bytes up to the first entry's aligned offset
               entry bytes, each at a multiple of the alignment, zero bytes between
end            the archive ends with the last entry's bytes (or with the index, when it holds no entry)
```

### The header {#header}

| Offset | Size | Field | Rule |
| --- | --- | --- | --- |
| 0x00 | 4 | magic | The bytes `C P A K` (`43 50 41 4b`) |
| 0x04 | 2 | version | `1`. A reader refuses any version it does not know |
| 0x06 | 2 | flags | `0`. A reader refuses any bit set, so a later version can give one a meaning |
| 0x08 | 4 | alignment | A power of two from 16 to 1,048,576: every entry's offset is a multiple of it |
| 0x0c | 4 | entry count | The number of records in the index |
| 0x10 | 4 | index size | The bytes of the index: the records and the names |
| 0x14 | 4 | reserved | `0`, refused otherwise |
| 0x18 | 8 | archive size | The file's size in bytes; a reader refuses a file of any other size |
| 0x20 | 32 | index SHA-256 | The SHA-256 of header bytes 0x00 to 0x1f followed by the whole index ([Hashes](#hashes)) |

### The index {#index}

The index starts at offset 64 and is `index size` bytes long: `entry count` records of 64 bytes, then the name table,
which holds the names' bytes with nothing between them. Records are sorted by name in byte order (the order of the
names' UTF-8 bytes compared as unsigned numbers), each name once, so a reader finds a name by binary search.

### A record {#records}

| Offset | Size | Field | Rule |
| --- | --- | --- | --- |
| 0x00 | 8 | offset | Where the entry's stored bytes start, from the start of the archive; a multiple of the alignment |
| 0x08 | 8 | stored size | The bytes the entry takes in the archive |
| 0x10 | 8 | size | The bytes it holds once decompressed; equal to the stored size when the compression is none |
| 0x18 | 4 | name offset | Where its name starts in the name table |
| 0x1c | 2 | name length | Its name's length in bytes |
| 0x1e | 1 | compression | 0 none, 1 LZ4, 2 zstd ([Compression](#compression)); any other value is refused |
| 0x1f | 1 | flags | `0`, refused otherwise |
| 0x20 | 32 | SHA-256 | Of the entry's decompressed bytes: the original's bytes, whatever the compression |

### Names {#names}

An entry's name is its asset's [`game:/` path](studio/content-and-formats.md#paths) without the `game:/`
(`textures/hud/hud_main`), so the name Studio shows, the catalogue's and the archive's are one. A name:

- is 1 to 1,024 bytes of UTF-8;
- is segments separated by `/`, none of them empty, `.` or `..` (no leading, trailing or double slash);
- holds no control character (bytes below 0x20 and 0x7f, NUL included), no `\` and no `:`.

These keep every name a relative path that a tool can export to a folder on any platform without escaping it. A writer
also refuses two names that differ only by ASCII case, which would be one file on Windows or macOS; a reader does not
check this, since such an archive can still be read.

### Alignment {#alignment}

Every entry starts at a multiple of the header's alignment, and the bytes between the index and the first entry, and
between entries, are zero. An empty entry takes no bytes and may share its offset with the next entry; no two entries
with bytes overlap, and none starts inside the header or the index.

Coney's writer uses 4,096 unless told otherwise: a memory page on Windows and x86-64 Linux, so one entry can be mapped
on its own and read in place. The padding costs half the alignment per entry on average, about 2 KB: for 30,000
entries, about 60 MB, under 2 % of a 4.3 GB install. An archive of many small entries (the sound effects) may use a smaller
alignment, down to 16; the field is per archive so the installer can choose.

### Compression {#compression}

| Value | Name | Stored bytes |
| --- | --- | --- |
| 0 | none | The entry's bytes as they are |
| 1 | lz4 | One LZ4 frame (the LZ4 frame format) |
| 2 | zstd | One Zstandard frame |

Installs write none. The values 1 and 2 are reserved in the first version so that compression, once measured, needs no
new version: the readers built so far list such an entry and refuse to read it, saying which compression it uses.

### Hashes {#hashes}

Both hashes are SHA-256, which Coney already has ([`sha256.h`](repo:src/core/sha256.h)) and which the `.cmod` package
design uses for its file list ([Mods: the `.cmod` package](studio/mods.md#cmod)), so one hash names a file in both.

- **The index hash** covers the first 32 bytes of the header (every field but the hash) and the whole index. A reader
  checks it before it reads a record, so a damaged count, offset or name is refused as a damaged index.
- **An entry's hash** is of its decompressed bytes, so it is the disc's bytes' hash whatever the compression, and the
  catalogue, Verify and an installer comparing with the disc all use the same value.

### What a reader checks {#reading}

A reader refuses the archive (Coney's: with `ErrorCode::Invalid`, or `Truncated` when the file is shorter than its
header says) unless, in this order:

1. the file holds at least the header; the magic, version, flags, reserved bytes and alignment are as above;
2. the file's size equals the header's archive size, and the index lies inside the file;
3. the index matches its SHA-256;
4. the records fit in the index; every record's compression and flags are known, its name lies in the name table and
   keeps the [name rules](#names), and an entry stored as it is has equal sizes;
5. the names rise strictly in byte order;
6. every entry is aligned, starts after the index and ends inside the archive, and no two entries with bytes overlap.

An entry's bytes are checked against its record's size and SHA-256 when they are read. A reader that has verified the
whole archive once may skip that check on later reads (`Archive::view` in Coney, against `Archive::verified`).

### What a writer does {#writing}

Coney's writer takes every entry first, then writes the archive in one pass, so an entry too large to hold in memory
(one read from the 1.4 GB `WARRIORS.WAD`) can be handed over in pieces:

1. sort the entries by name in byte order;
2. lay out the index (the records, then the names in the same order) and each entry at the next multiple of the
   alignment after the one before;
3. write the header with its index hash, the index, then each entry after its zero padding;
4. hash each entry's bytes again as they pass, and stop with an error when they are not the size and hash it was added
   with, so a source that changed is never written under the wrong hash.

### An example {#example}

The sample both test suites build: alignment 16 and three synthetic entries, `readme` (the five bytes `Coney`),
`art/logo` (`00 01 02`) and `empty` (no bytes). Sorted, the records are `art/logo`, `empty`, `readme`; the index is
3 × 64 + 19 = 211 bytes and ends at offset 275, so `art/logo` is at 288 and `empty` and `readme` both at 304. The
archive is 309 bytes, and its SHA-256 is
`188b0b7a40e4125e66ddf7ae6b81c61436070adc0e2406d5947b85441df6c486`. Its header:

```text
00000000  43 50 41 4b 01 00 00 00  10 00 00 00 03 00 00 00   CPAK, version 1, flags 0, alignment 16, 3 entries
00000010  d3 00 00 00 00 00 00 00  35 01 00 00 00 00 00 00   index 211 bytes, reserved, archive 309 bytes
00000020  01 c9 41 ad 58 cb 1b b9  d5 19 55 2c e1 b9 b9 49   the index SHA-256 ...
00000030  c4 a1 5d 9d 75 11 fa 53  7e 00 72 4a f1 47 14 36
```

## The install folder {#layout}

What #798 decided the installer builds (step 3). There is one layout: no choice between the disc's files and Coney's,
no copy of the disc's files and no `open/` folder of exported files; repair re-reads the disc, and Studio exports to a
folder the user picks.

```text
<install>/
  game/                          the base game, built by the installer and never edited
    common/scripts.cpak text.cpak data.cpak
    world/<area>.cpak            sectors, collision and placed objects of the streamed worlds, by area
    characters/models.cpak animations.cpak
    scenes/cutscenes.cpak
    textures/hud.cpak gui.cpak fx.cpak
    audio/sfx.cpak music.cpak banks.cpak
    movies/<name>.bik            the movies, as files
    xbox/                        only with the Xbox disc: its entries, grouped as above (Xbox entries, below)
    index/install.json           the install record: sources, disc hashes, Coney's version, counts and digests
    index/catalogue.json         every entry of every archive (The catalogue, below)
  mods/                          mods; their form is under review (Lookup order, below)
  packs/                         texture packs, keyed by the original texture's hash
<per-user save location>/        save data only
```

The archive names inside each folder (which areas `world/` has, which sounds go in `sfx` and which in `banks`) are the
installer's to choose, and this page lists them when it is built.

### Xbox entries {#xbox}

Decided (maintainer, 2026-10-28): the PS2 disc is required and is the only gameplay source; there is no install from
the Xbox disc alone. The Xbox disc adds presentation, HD textures and movies (and character models if they change
nothing in gameplay), through Coney's Xbox importer, and its entries go in **archives of their own** under
`game/xbox/`, grouped by the same kinds and areas as `game/` (`game/xbox/textures/hud.cpak`). A PS2 install is complete
without them, adding or removing the Xbox disc adds or deletes that one folder, and Verify of the PS2 archives never
depends on the Xbox disc. Studio never reads a disc: Xbox content reaches it only as these entries.

## The catalogue {#catalogue}

`game/index/catalogue.json` lists every entry of every archive, and every movie file, one row per entry per source. It
is JSON because machines write and read it (as the install record and `coney-tools extract`'s indexes are), and a
person can still read it. It is written by the installer and never edited.

```json
{
  "format": "coney-catalogue",
  "version": 1,
  "entries": [
    {
      "name": "textures/hud/hud_main",
      "kind": "texture",
      "source": "ps2",
      "format": "rw-texture-dictionary",
      "archive": "textures/hud.cpak",
      "size": 131072,
      "sha256": "<64 hex digits>",
      "original_id": {"wad_hash": "1a2b3c4d"}
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `name` | The entry's name, its `game:/` path without the root ([Names](#names)); the same in the archive |
| `kind` | What it is, for the lookup order and Studio: `script`, `text`, `data`, `world`, `collision`, `objects`, `model`, `animation`, `character`, `scene`, `texture`, `sound`, `music`, `bank`, `movie`. Each is a simulation or a presentation kind ([Layers](asset-and-script-architecture.md#asset-layers)) |
| `source` | `ps2` or `xbox`: the disc the bytes came from |
| `format` | The original format of the bytes, which Studio's exporter and importer are picked by, never by guessing from the bytes; an edited entry is imported back into this format |
| `archive` | The archive's path from `game/` (`textures/hud.cpak`, `xbox/textures/hud.cpak`), or `null` for a movie file |
| `file` | Only for a movie: its path from `game/` (`movies/intro.bik`) |
| `size`, `sha256` | The entry's size and SHA-256, as its record gives them ([Hashes](#hashes)) |
| `original_id` | The original's own key: `wad_hash` (8 hex digits, the WARRIORS.DIR name hash) for a WAD entry; `disc_path` (`IOP/BFW.SND`) for a file outside the WAD, with `part` for one piece of it (a sound's name hash in the sound data); `resource_hash` and `chunk_type` for an Xbox resource |

A reader refuses a `version` it does not know. The list of `format` values is the installer's (step 3) and is added to
this page with it.

## Lookup order {#lookup}

How Coney finds an asset once the loader reads the install (step 4), for a `game:/` name and a kind:

1. **Enabled mods**, in the load order. #798 planned them as patch archives under `mods/`; the Studio design proposes
   `.cmod` packages in their place ([Mods: the `.cmod` package](studio/mods.md#cmod)), and whether a `.cmod` uses this
   container or zip is a maintainer's to confirm. A mod that changes a simulation kind marks a co-op session as
   modded.
2. **Texture packs** in `packs/`, by the original texture's hash, for textures only.
3. **The game's archives**, through the catalogue: for a presentation kind, the `xbox` row when the Xbox disc was
   installed, else the `ps2` row; for a simulation kind, the `ps2` row only.

Test mode (`--headless`, `--input-script`, the disc tests) reads only the `ps2` rows, so reference comparisons never
see a mod, a pack or an Xbox entry.

## The tools {#tools}

- **The engine**: `coney::cpak` in [`cpak_index.h`](repo:src/core/cpak_index.h) (the header, the index and an entry
  read from a stream), [`cpak_archive.h`](repo:src/core/cpak_archive.h) (an archive in memory, read in place) and
  [`cpak_writer.h`](repo:src/core/cpak_writer.h). Every failure is a `coney::Error`; the tests build archives byte
  by byte from this page (`tests/core/cpak_*_test.cpp`).
- **`coney-tools cpak list`, `show` and `verify`** read an archive with a second implementation, in Python
  ([the commands](coney-tools-disc.md#cpak)). Both write the [example](#example) byte for byte, which their tests
  check.
