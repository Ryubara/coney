# Xbox assets (optional)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the PS2 disc's
`WARRIORS.DIR` / `.WAD`, compared with the NTSC-U Xbox disc: a full (Redump-style) image of 7,825,162,240 bytes whose
`default.xbe` has SHA1 `fe246a74501cd10d2ce89ca5dbda5080d2b2edcc`. No runtime claims. Survey date 2026-10-04.

Addresses written **XBE `0x...`** are virtual addresses in that `default.xbe` (base `0x00010000`), not in
`SLUS_212.15`. The executable was only read as far as the archive and its name hash needed (strings, headers and
four short functions); no game logic was disassembled.

## Purpose

The decision: **the PS2 version stays the only reference for behaviour; the Xbox version may become an
optional asset source.** A player who also owns the Xbox disc could point Coney at it, and Coney would load that
disc's higher-quality assets in place of the PS2 ones wherever the two correspond, falling back to the PS2 disc
everywhere else. This page is the feasibility survey for that: what is on the Xbox disc, how its archive and names
work, which assets correspond to PS2 ones, how their formats differ, and what a resolver would need.

The short answer: **feasible for textures and movies, with a modest gain; not worth it (or not possible) for
models, levels and sound.** The Xbox port shares the game's data pipeline and most of its non-graphics data byte for
byte, so names and resources line up almost perfectly, but it does not use RenderWare: every graphics asset is in an
Xbox-only format that librw cannot read, and the gain is concentrated in textures (612 of 2,027 matched textures at
twice the resolution, DXT-compressed with mipmaps) and in 720p versions of 15 of the 16 movies.

## Original structure

### The disc

The image is an XGD1 disc: the game partition (XDVDFS) starts at byte `0x18300000`, where sector 32 holds the
`MICROSOFT*XBOX*MEDIA` volume descriptor. The file tree has 2 directories and 431 files, 5,620,148,362 bytes in all:

| Path | Files | Bytes | What |
| --- | ---: | ---: | --- |
| `default.xbe` | 1 | 4,636,672 | The executable |
| `XBoxWad.idx` | 1 | 169,700 | The archive index, see [The archive](#the-archive) |
| `Main.wad0`-`Main.wad3` | 4 | 958,392,320 | Archive volumes: resources, scenes, scripts, streamed worlds |
| `Paks.wad0`-`Paks.wad2` | 3 | 775,854,080 | Archive volumes: 883 packs |
| `System.wad0` | 1 | 1,679,360 | Archive volume: the global pack and a scene list |
| `audio/xbox000.xwb`-`xbox368.xwb` | 369 | 2,217,542,292 | XACT wave banks |
| `audio/xbox000.xsb` | 1 | 4,544,708 | XACT sound bank |
| `audio/dsstdfx.bin`, `Warriors.bin`, `version.txt` | 3 | 33,854 | DSP effects image and two small files |
| `bik/*.bik` | 48 | 1,657,295,376 | Bink movies: the PS2's 16, each in three versions |

The PS2 disc, for comparison, has one archive (`WARRIORS.WAD`, 1.5 GB), its sound in `IOP/BFW.SND` and
`IOP/MUSIC.SND` (2.4 GB) and 16 movies in `PSS/` ([Recon](overview.md#disc-layout)).

### The executable

| Field | Value |
| --- | --- |
| Title, title ID | *The Warriors*, `0x5454009B`; region 1 (North America); certificate version 3 |
| Build | header time stamp 2005-09-25 21:34:01 UTC, certificate 2005-09-28 |
| Debug paths | `d:\Warriors\Build\rundata_XBox\WarriorsDVD.exe`, `d:\Warriors\Source\XBox\Final DVD no deploy\XBox.pdb` |
| Sections | `.text` (`0x34befc` bytes), `.rdata`, `.data`, and library sections `D3D`, `D3DX`, `XGRPH`, `DSOUND`, `XACTENG`, `BINK*`, `WMADEC`, `XPP`, `DOLBY` |
| Libraries | XDK **1.0.5849** for all nine: `XAPILIB`, `D3D8`, `D3DX8`, `XGRAPHC`, `DSOUND`, `XACTENG`, `XBOXKRNL`, `LIBCPMT`, `LIBCMT` |
| Middleware | Lua 4.0.1 (the version string), Bink (its own sections), XACT for sound, WMA decoding for custom soundtracks |

**There is no RenderWare in the Xbox build.** None of the PS2's RenderWare strings (`Renderware`, `Renderware Calloc`,
the `DevRWGeneric.cpp` and `DevRWDebug.cpp` paths) occur in the XBE, and no file in the Xbox archive contains a
RenderWare stream (below). The graphics device is the port's own Direct3D 8 code: `GraphicsDeviceXBox`,
`GraphicsTextureXBox`, `GraphicsResourceXBox`. **Evidence:** inferred, from the strings and the data.

**Source paths.** The XBE embeds 149 `.cpp` and `.inl` paths (plus 5 headers) of the form
`\Warriors\Source\<Subsystem>\<File>.cpp`, against the PS2's 153 ([Source map](source-map.md)); 135 are shared.
The same string scan finds 152 of the PS2's 153.

- Xbox only (14): `Device/XBox/` (`DS_XBoxDevice`, `DS_XBoxFileSys`, `DS_XBoxStreamFileSys`, `fileio/XBoxstream`,
  `sound/XBoxAudioDevice`), `Graphics/Devices/XBox/` (`GraphicsDeviceXBox`, `GraphicsResourceXBox`,
  `GraphicsShadowXBox`, `ParticleSystemXBox`, `XBoxParticleSystem`), `World/XBox/` (`WorldLevelXBox`,
  `WorldManagerXBox`), `Scripting/ScriptLuaXBox.inl` and **`WadFile/WadFile.cpp`**, the Xbox archive reader.
- PS2 only (17 of this scan's 152): `Device/ps2/` (including `fileio/DVDWadIndexPS2.cpp` and
  `fileio/RockWadIndexPS2.cpp`), `World/ps2/`, the RenderWare device (`DevRWGeneric.cpp`, `DevRWDebug.cpp`),
  `FileIO/FS_TCPSocketFileSys.cpp`, `Warriors/W_PS2SaveSystem.cpp`, `Scripting/ScriptLua.inl` and
  `lua-4.0.1/src/lmem.cpp`.

So everything above the platform layer (game modes, AI, scenes, scripting, the chunk system) is the same code; the
file system, renderer, world streaming and sound are platform-specific. **Evidence:** inferred.

## Data

All values little-endian, as on the PS2.

### The archive {#the-archive}

The Xbox uses neither `WARRIORS.DIR`/`.WAD` nor loose files: it has **one index, `XBoxWad.idx`, over eight archive
volumes** (`System.wad0`, `Main.wad0`-`3`, `Paks.wad0`-`2`). `WadFile.cpp` replaces the PS2's `DVDWadIndexPS2.cpp`.

```c
struct XboxWadHeader {          // at 0x00
    uint32_t volumeCount;       // 8
    uint32_t entryCount;        // 10,587
    uint32_t unknown;           // 0x00100000
    uint32_t volumesOffset;     // 0x14
    uint32_t entriesOffset;     // 0x134 = 0x14 + 8 * 36
};
struct XboxWadVolume {          // 36 bytes, volumeCount of them
    char     name[32];          // "System.wad0", "Main.wad0", ...; NUL-terminated, the rest is build-tool garbage
    uint32_t minusOne;          // 0xffffffff
};
struct XboxWadEntry {           // 16 bytes, entryCount of them
    uint32_t location;          // bits 24-31: volume index; bits 0-23: offset in 1,024-byte units
    uint32_t size;              // bytes
    uint32_t zero;              // 0 in every entry
    uint32_t nameHash;          // see "Name hash" below
};
```

`0x134 + 10,587 × 16 = 169,700`, the file's size. All 10,587 entries lie inside their volume, at 2,048-byte
boundaries, in ascending offset order within each volume. Volume use: `System.wad0` 2 entries, `Main.wad*` 9,702,
`Paks.wad*` 883. **Evidence:** the lookup loop (16-byte entries, hash at `+0x0c`, count from the header's second
word) is confirmed (code) at XBE `0x000704a0`; the path `D:\XBoxWad.idx` is loaded at XBE `0x0033f880`; the rest of
the layout is inferred from the data, which it fits exactly.

### Name hash

The Xbox hashes **`ee_files\<name>`** (backslash, no leading `./`), lowercased, with the same standard CRC-32 as the
PS2 ([Name hashing](name-hash.md)):

```python
xbox_hash = zlib.crc32(("ee_files\\" + name).lower().encode("ascii"))   # PS2: "./ee_files/" + name
```

Unlike the PS2, the Xbox folder is **not flat**: packs live in `ee_files\paks\` and standalone animations in
`ee_files\anims\`. So the PS2 hash of a name never equals the Xbox hash (0 of 10,701 PS2 hashes occur in the Xbox
index), but the name does carry over.

**Evidence:** confirmed (code). XBE `0x0006c4f0` copies the prefix `ee_files\` in front of the name, normalises the
path (XBE `0x00070170`, which splits on `\`, `/` and `:`) and looks it up; XBE `0x00068590` is the hash: CRC-32 with
the table at `0x004a7bf0`, initial value `0xffffffff`, final complement, each character case-folded before it is
hashed. Corroborated by the [name overlap](#name-overlap) below.

### Name overlap {#name-overlap}

Of the 3,990 PS2 names recovered so far ([WAD contents](formats/wad-contents.md#names)), **3,957 exist on the Xbox**:

| PS2 kind | PS2 names | On the Xbox | Xbox name |
| --- | ---: | ---: | --- |
| Scene records (`.scn`) | 2,760 | 2,760 | `ee_files\<name>` |
| Animations (`.anm`) | 563 | 563 | `ee_files\anims\<name>` |
| Lua bytecode (`.lua`) | 275 | 275 | `ee_files\<name>` |
| Packs (`.pak`) | 236 | 231 | `ee_files\paks\<name>` |
| Levels (`.lev`) | 64 | 64 | `ee_files\<name>` |
| Object lists (`_objs.txt`) | 63 | 63 | `ee_files\<name>` |
| `scene_list.cnk`, the global `.glr` | 2 | 2 | `ee_files\<name>` |
| Sound banks and indexes (`.msb`, `.msd`) | 26 | 0 | none: the Xbox uses XACT |
| Memory card icon (`.ico`) | 1 | 0 | none |

The five packs not found belong to levels 60, 64 and 100. Adding the Xbox-only scene records (by their own names)
and levels gives 4,038 of the 10,587 Xbox names. As on the PS2, the named entries from index 210 on are in name
order. **Evidence:** inferred (hash matches; with these three fixed prefixes chance matches are negligible).

**By resource.** The standalone models and textures are unnamed on both discs, but the [chunk
container](formats/wad-contents.md#chunk-container)'s resource hashes are the same on both (they hash the resource's
own name, with no folder): 2,092 texture resources, all 1,541 PS2 model resources, 616 of 617 animations and all 53
characters have an Xbox resource with the same hash. **This, not the WAD name, is what a resolver should key on.**

### Kinds of entry

| Kind | PS2 entries | Xbox entries | Xbox bytes | Same bytes as the PS2? |
| --- | ---: | ---: | ---: | --- |
| Pack | 881 | 884 | 776,567,280 | no: their graphics chunks differ |
| Resource: texture dictionaries (with or without a particle page) | 2,085 | 2,062 | 138,983,792 | no: Xbox textures |
| Resource: models (plain and skinned) | 1,477 | 1,477 | 20,586,960 | no: Xbox models |
| Resource: animations | 564 | 564 | 2,600,960 | no: same sizes, different keyframe bytes (below) |
| Resource: characters | 53 | 53 | 7,532,016 | no |
| Resource: levels (`.lev`) | 64 | 95 | 44,488,240 | no |
| Resource: global, scene list | 3 | 4 | 1,525,008 | no |
| Scene records (`.scn`) | 2,765 | 2,814 | 200,615,808 | **all 2,765 PS2 records identical**; 49 more on the Xbox |
| Lua 4.0 bytecode | 467 | 503 | 10,931,421 | **all 467 PS2 scripts identical**; 36 more on the Xbox |
| Object lists and other text | 63 | 81 | 1,154,288 | 74 identical |
| Streamed world | 2,229 | 2,048 | 516,969,472 | no: a different split (below) |
| Sound banks (`.msb`, `.msd`) | 42 | 0 | | moved to XACT |
| XACT sound bank (`SDBK`) | 0 | 1 | 4,544,708 | identical to `audio/xbox000.xsb` |
| 24-bit bitmap (`BM`, 256 × 128) | 1 | 1 | 98,358 | identical |
| Font metrics, memory card icon, older RenderWare streams | 7 | 0 | | |
| **Total** | **10,701** | **10,587** | **1,726,598,311** | |

**Evidence:** inferred: every entry is assigned by the same structural tests as on the PS2 (the container parse,
the Lua header, the scene-record name, magic numbers), and "identical" means equal SHA1 against some PS2 entry.
(The bitmap was counted among the PS2's sound banks on [WAD contents](formats/wad-contents.md#kinds-of-entry); it
is one 256 × 128 24-bit `BM` image.)

### Formats, kind by kind

**Chunk container: the same.** Packs (marker `0xDE686795`), resources and chunks use the PS2's 16-byte headers
unchanged ([WAD contents](formats/wad-contents.md#chunk-container)); all 884 packs and 4,255 standalone resources
parse to their exact size (43,104 resources, 108,836 chunks). The XBE's chunk-type table (XBE `0x00458360`, 84
records of 12 bytes and a terminator, found through its `Renderware Texture Dic` name) has **the same 84 names in
the same order** as the PS2's at `0x0050b2e0`, and the same types have `onLoaded` and `readFromStream` handlers.
The 29 types the WAD uses are the same on both. **Evidence:** inferred (table read from the XBE's data, as on
[Chunk system](chunk-system.md#chunk-type-table)).

**Graphics chunks: Xbox-only formats, no RenderWare.** On the PS2, types `0x15` (world), `0x2a` (texture dictionary)
and `0x47` (model) hold RenderWare streams. On the Xbox they hold the port's own resources, each starting with
`{u32 kind, u32 tag, ...}`:

| Chunk | First words | Chunks |
| --- | --- | ---: |
| `0x2a` texture | `0`, `0x6f` | 18,245 |
| `0x47` model, unskinned | `2`, `0x6d` | 18,993 |
| `0x47` model, skinned (with `0x28` bone offsets) | `1`, `0x76` | 2,370 |
| `0x47` other | `3`, `0x67` | 157 |
| `0x15` world (sector BSP) | `6`, `0x66` | 95 |
| Streamed world entries | `4`, `0x6f` / `5`, `0x6a` | 1,969 / 79 |

Bytes `0x20`-`0x7f` of a texture chunk, and similar stretches elsewhere, are uninitialised memory of the build tool
(stack and heap addresses), so these are memory images written by the tools. **Evidence:** inferred.

**Textures (`0x2a`).** One texture per chunk, laid out as: a 32-byte header `{0, 0x6f, 1, dataSize, 0x20, 0x10,
pointer, 0}`, 96 bytes of tool garbage, `dataSize` bytes of pixel data (every mip level), then a 64-byte
descriptor: an Xbox Direct3D texture header (`Common = 0x00040001`, `Data = 0`, `Lock = 0`, `Format`, `Size = 0`),
`0xee` filler, and `u16 width, u16 height` at `+0x28`; then `0xee` filler to the chunk's end. The `Format` word's
bits 8-15 are the D3D format, 16-19 the mip count, 20-23 and 24-27 log2 of width and height; in all 18,245 chunks
they agree with the stored width and height. Every texture is block-compressed: `0x0C` DXT1, `0x0E` DXT2/3, `0x0F`
DXT4/5, which are not swizzled on the Xbox. **Evidence:** inferred (all 18,245 parse; the format numbers are the
XDK's).

**Models (`0x47`) and worlds (`0x15`).** Not decoded. Matched models are about the same size on both: the 1,541
shared model resources total 32,838,784 bytes of model chunks on the Xbox and 32,564,944 on the PS2 (first instance
each), so there is no sign of more detailed meshes. **Evidence:** inferred.

**Animations, characters, collision, paths.** Non-graphics chunks are the same data, often byte for byte: character
data (`0x08`, 53 of 53), anim ranges (`0x45`, 53 of 53), bone offsets (`0x28`, 153 of 153), particle pages (`0x4c`,
537 of 541), path data (`0x40`), subtitles (`0x51`), occluders (`0x53`) and most collision chunks are identical in
the 30 levels compared. Keyframes (`0x00`) have the same sizes but different bytes in 551 of 616; the level header
(`0x17`) grows from 48 to 128 bytes. **Evidence:** inferred (SHA1 per chunk, matched by resource hash).

**Lua.** All 503 scripts carry the PS2's header `1B 4C 75 61 40 01 04 04 04 20 06 09 08` ([WAD
contents](formats/wad-contents.md#lua)), and the 467 PS2 scripts are byte-identical on the Xbox.

**Scene records.** Same layout; all 2,765 PS2 records identical. The 49 extra records are cutscenes of
two levels.

**Levels and the streamed world.** The Xbox has 95 level resources against 64: all 64 of the PS2's, plus 31 that
the PS2 does not ship: 2 front-end or arena levels, 14 test levels (named `test...`) and 15 more `level<N>`. The
streamed world is
split differently: 1,969 + 79 entries on the Xbox, 1,911 sector-atomics files + 159 world streams + 159 manifests on
the PS2 ([Level files](formats/wad-contents.md#level-files)), with no manifests on the Xbox. A level's world can
only be taken whole from one disc. **Evidence:** inferred.

**Sound.** XACT replaces the PS2's sound banks and IOP files: 369 wave banks (`WBND` version 3; 368 streaming)
hold 23,523 waves, all Xbox ADPCM except 2 PCM, mostly mono at 22,050 Hz (17,430) or 22,500 Hz (4,708), plus one
sound bank (`SDBK`, also stored in the archive). The XBE's music cue names start `vags/xboxmusic/`. Nothing on the
PS2 side maps to these by name yet. **Evidence:** inferred (header fields per the XDK's XACT bank layout).

**Movies.** The PS2's 16 movies (`BIKi`, 640 × 448) exist on the Xbox as `<name>.bik` (640 × 480), `<name>_w.bik`
(640 × 480) and `<name>_hd.bik` (1280 × 720 for 15; `trailer_hd.bik` is 640 ×
480). Same Bink revision `i` on both. The XBE's `%s_hd`, `%s_w`, `%s_ls_%d_w` and `%s_w_pal` format strings show the
same three-way choice for loading screens. **Evidence:** inferred (Bink headers); that `_w` means anamorphic
widescreen is speculative.

### Quality of matched textures

Over the 2,092 texture resources on both discs, comparing the first instance of each; 2,027 hold the same number of
textures on both (65 differ: 35 have two more on the Xbox, 30 one fewer).

| | PS2 | Xbox |
| --- | ---: | ---: |
| Textures compared | 2,027 | 2,027 |
| Same width and height | 1,413 | 1,413 |
| Twice the width **and** height | | 612 |
| Larger in another way / smaller | | 2 / 0 |
| Total pixels | 128,302,496 | 155,473,872 (× 1.21) |
| Largest side | 512 | 1,024 (one texture) |
| Textures with a side of 512 or more | 370 | 424 |
| Pixel format | palettised: 8-bit (1,967 of all 2,205), 4-bit (238) | DXT1 (1,484 of all 2,312), DXT2/3 (375), DXT4/5 (453) |
| Mipmaps | | full chains (1 to 10 levels) |
| Bytes of `0x2a` chunks, every instance | 330,158,928 | 501,984,640 |

So about 30% of the textures double in resolution; the rest are the PS2 images re-encoded from 4- and 8-bit palettes
to DXT, which trades palette banding for block artefacts rather than adding detail. **Evidence:** inferred.

## Behaviour

How the Xbox finds a file, for comparison with [File I/O](file-io.md): a name is prefixed with `ee_files\`,
normalised, hashed (case-folded CRC-32) and found by a linear scan of the 10,587 entries, as `DVDWadIndex::Find`
does on the PS2. **Evidence:** confirmed (code) at XBE `0x0006c4f0`, `0x000704a0` and `0x00068590`. Nothing else
about the port's behaviour is in scope: it is not a reference for Coney.

## Coney's implementation

Not started; this is the plan.

**Policy.** The PS2 disc is required and stays the reference for behaviour. The Xbox disc is optional, supplied by
the player like the PS2 one, and only ever replaces *assets*: never scripts, scenes, levels, collision or anything
the simulation reads. Its extra content (31 levels, 49 scene records, 36 scripts; most of the levels are test
levels) is ignored.

**Reading the disc.** An XDVDFS reader (the format is public: a volume descriptor at sector 32 of the partition and
binary-tree directories) that finds the partition by its magic at `0x18300000` (XGD1) or `0x1FB20000` (XGD2), or
reads an extracted folder; then `XBoxWad.idx` and the eight volumes as above. `coney-tools` should gain the same
`wad` commands for it, to keep the survey reproducible.

**The resolver.** It works below the chunk system, at **resource** level, keyed by the resource hash and the chunk
type, because the WAD names of the PS2's models and textures are unknown:

1. At start-up, index every Xbox resource (in packs and standalone) by `(resource hash, chunk type)`. One scan of
   the 1.7 GB archive's headers; cache the result.
2. When the PS2 loader decodes a texture dictionary (`0x2a`) whose resource hash has an Xbox counterpart with the
   same number of textures, decode the Xbox texture instead. A resource whose counts differ (65) stays PS2.
3. Movies: prefer `bik/<name>_hd.bik` (or `_w` for 4:3 widescreen output) from the Xbox disc when present.

| Kind | Swap? | What Coney needs |
| --- | --- | --- |
| Textures | **yes** | A reader for the Xbox texture chunk (above), then either a DXT raster (librw's GL3 backend allocates S3TC rasters when the driver supports them) or a CPU decode (librw's `Image::setPixelsDXT` handles DXT1/3/5). librw's own Xbox support reads RenderWare Xbox streams, which these are not. Map each PS2 texture to its Xbox one by resource hash and position. |
| Movies | **yes** | Nothing beyond the PS2 Bink path: same revision, different file names and sizes. |
| Models | no | Xbox-only geometry format, no sign of more detail; decoding it is research for no visible gain. |
| Levels, world | no | Different streamed-world split, Xbox-only world format; the world's textures could be swapped later if their resource hashes match (open question). |
| Sound | not yet | XACT banks and Xbox ADPCM are documented formats, but the PS2's sounds would first have to be mapped to Xbox waves (no names). |
| Scripts, scenes, animations, characters | never | Identical or behaviour data; the PS2 copy is the reference. |

**Recommended next step.** Add `coney-tools xbox` (XDVDFS reader, `XBoxWad.idx` parser, the Xbox name hash, resource
index and a texture-chunk reader that prints counts only), turning this survey into a repeatable check; then, once
[First pixels](../roadmap.md#first-pixels) draws PS2 textures, add the texture path behind an option. Track it as the
roadmap's [Xbox assets](../roadmap.md#xbox-assets-optional) milestone.

## Open questions

1. **Texture names.** The Xbox texture chunk stores no name; PS2 materials refer to textures by name. Mapping by
   resource hash and position works for the 2,027 equal-count resources; are the 65 others reordered, split or new
   (the loading screens' `_w`/`_hd` variants are a candidate)?
2. **DXT2/3 and DXT4/5.** The Xbox format code does not say whether alpha is premultiplied (DXT2, DXT4) or not
   (DXT3, DXT5); the texture header's sixth word or the renderer's blend state would.
3. **World textures.** Do the Xbox streamed-world entries (kinds 4 and 5) carry textures with resource hashes that
   match the PS2 world streams' texture dictionaries? If so, level textures can be swapped without the geometry.
4. **The index header's third word** (`0x00100000`) and the volumes' trailing `0xffffffff`: unused by the lookup;
   probably a volume size limit and a flag.
5. **Unnamed Xbox entries.** 6,549 of 10,587 (models, textures, streamed world, 653 packs, 228 scripts), as on the
   PS2; the subfolder scheme (`paks\`, `anims\`) suggests the rest also live in subfolders.
6. **Sound mapping.** Which XACT cue or wave corresponds to which PS2 sound; the global resource's sound chunks
   (`0x29`, `0x48`, `0x49`) differ between the discs and probably hold the mapping.
