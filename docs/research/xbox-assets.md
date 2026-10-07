# Xbox assets (optional)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the PS2 disc's
`WARRIORS.DIR` / `.WAD`, compared with the NTSC-U Xbox disc: a full (Redump-style) image of 7,825,162,240 bytes whose
`default.xbe` has SHA1 `fe246a74501cd10d2ce89ca5dbda5080d2b2edcc`. No runtime claims. Survey date 2026-10-04.

Addresses written **XBE `0x...`** are virtual addresses in that `default.xbe` (base `0x00010000`), not in
`SLUS_212.15`. The executable was only read as far as the archive and its name hash needed (strings, headers and
four short functions); no game logic was disassembled here. Whether the executable helps reverse engineering is
measured on [Xbox executable](xbox-executable.md).

## Purpose

The decision (owner, 2026-10-07): **Coney installs from the player's discs, and the Xbox disc is the preferred
source of assets, the PS2 disc filling the gaps.** At install `coney-tools extract` converts the assets to open
formats outside the repository; given the Xbox disc as well, it takes each asset from whichever disc has the better
version. The PS2 version stays the only reference for behaviour, and level scripts, data tables and game text are
not taken from either disc. This page says what is on the Xbox disc, how its archive, names and graphics formats
work, how each kind of asset corresponds to the PS2's, and the rule that picks a disc per asset.

The short answer, kind by kind ([Asset kinds](#asset-kinds)): **textures and movies gain; nothing else does.** 23,210 of
the PS2's 26,308 textures have a matching Xbox texture that is larger, nearly all twice the width and height. The 16
movies have 1280 × 720 versions, 15 of them as long as the PS2's. Models, animations, collision and the rest are the
same data (the models re-encoded as float triangle lists), and the sound is the same recordings at the same or a lower
sample rate, so those stay the PS2 disc's.

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

How the executable compares with `SLUS_212.15` for reverse engineering (function counts, anchors, decompiler
output, library signatures, mapping functions between the builds) is on [Xbox executable](xbox-executable.md).

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
and levels gives 4,038 names, which cover 4,041 of the 10,587 entries: three hashes occur twice in the index
(`paks\global.pak` in both `System.wad0` and `Paks.wad0`, `scene_list.cnk` in both `System.wad0` and `Main.wad0`,
and one scene record stored twice at two different sizes). As on the PS2, the named entries from index 210 on are
in name order. **Evidence:** inferred (hash matches; with these three fixed prefixes chance matches are negligible).

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
| Object lists and other text | 63 | 81 | 1,154,288 | 74 identical; includes the font metrics (`METRICS1`) |
| Streamed world | 2,229 | 2,048 | 516,969,472 | no: a different split (below) |
| Sound banks (`.msb`, `.msd`) | 42 | 0 | | moved to XACT |
| XACT sound bank (`SDBK`) | 0 | 1 | 4,544,708 | identical to `audio/xbox000.xsb` |
| 24-bit bitmap (`BM`, 256 × 128) | 1 | 1 | 98,358 | identical |
| Font metrics, memory card icon, older RenderWare streams | 7 | 0 | | font metrics counted as text above |
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

**Models (`0x47`) and worlds (`0x15`).** Decoded far enough to compare: [Models](#models), [Levels and the
streamed world](#worlds).

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
only be taken whole from one disc; its names are under [Levels and the streamed world](#worlds). **Evidence:**
inferred.

**Sound.** XACT replaces the PS2's sound banks and IOP files: 369 wave banks (`WBND` version 3; 368 streaming)
hold 23,523 waves, all Xbox ADPCM except 2 PCM, plus one sound bank (`SDBK`, also stored in the archive). How they
map to the PS2's sounds: [Sound, speech and music](#sound). **Evidence:** inferred (header fields per the XDK's XACT
bank layout).

**Movies.** The PS2's 16 movies exist on the Xbox in three versions each: [Movies](#movies). The XBE's `%s_hd`,
`%s_w`, `%s_ls_%d_w` and `%s_w_pal` format strings show the same three-way choice for loading screens.

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

`coney-tools xbox textures --ps2` reproduces the 2,092 shared resources, the 2,027 with equal counts and the 35 / 30
that differ. Over the first instance of each of the 2,092 it counts 2,222 Xbox textures (DXT1 1,403, DXT2/3 375,
DXT4/5 444) and 2,182 PS2 ones, so the pixel-format row's totals (2,312 and 2,205) were taken over a set the tool does
not reproduce; the format shares are close either way.

So about 30% of the textures double in resolution; the rest are the PS2 images re-encoded from 4- and 8-bit palettes
to DXT, which trades palette banding for block artefacts rather than adding detail. **Evidence:** inferred.

### Resource images {#resource-images}

Every Xbox graphics object is a memory image the build tools wrote: a texture chunk (`0x2a`), a model chunk
(`0x47`), a sector BSP chunk (`0x15`), and the streamed world's files ([below](#worlds)). One layout serves them all:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | kind: 0 texture, 1 skinned model, 2 model, 3 model with a frame tree, 4 world sector, 5 world level, 6 sector BSP |
| `+0x04` | u32 | tag: `0x6f`, `0x76`, `0x6d`, `0x67`, `0x6f`, `0x6a`, `0x66` in the same order |
| `+0x08` | u32 | number of Direct3D resource headers |
| `+0x0c` | u32 | `dataSize`: bytes of GPU data from `+0x80` (texels of every mip level, vertices, indices) |
| `+0x10` | u32 | bytes of the resource headers after the data (padded to 16) |
| `+0x14` | u32 | bytes of the object after the headers |
| `+0x18`-`+0x7f` | | build-tool memory (stack and heap addresses, log text) |

The headers are the XDK's: a texture's is 20 bytes (`Common`, `Data`, `Lock`, `Format`, `Size`), a vertex or index
buffer's 12 (`Common`, `Data`, `Lock`); `Common` bits 16-18 give the type (0 vertex buffer, 1 index buffer, 4
texture) and `Data` is the offset into the data block. **Evidence:** confirmed (code): the `0x47` reader (XBE
`0x0009d030`, entry `0x47` of the chunk-type table at XBE `0x00458360`, also used by `0x09` and `0x25`) loads the
image and dispatches on kind 1, 2 or 3; XBE `0x000bb190` walks the `+0x08` headers after the data, registering a
texture (20 bytes) or vertex buffer (12) and rebasing an index buffer's `Data` (12); the object starts
`dataSize + headersSize` after `+0x80`. The kind and tag pairs and the sizes are inferred from the data, which they
fit in every graphics chunk and world file.

A texture's `Format` word holds the D3D format (bits 8-15), the mip count (16-19) and log2 of the width (20-23) and
height (24-27). Every texture on the disc is block-compressed and unswizzled: `0x0C` DXT1, `0x0E` DXT2/3, `0x0F`
DXT4/5. The `0x0F` ones are **DXT5, straight alpha**: in 141 of 142 same-size matched textures with partly
transparent texels the colours equal the PS2's, not the PS2's multiplied by alpha (inferred); the `0x0E` ones have
no partly transparent texels to measure.

### Asset kinds, Xbox against PS2 {#asset-kinds}

| Kind | Xbox format | Correspondence | Coverage | Gain | Coney takes |
| --- | --- | --- | --- | --- | --- |
| Textures (resources) | DXT1/3/5 images, no names | resource hash, then position, checked by content | 578 of 2,343 larger | 2× width and height | **Xbox when larger** |
| Textures (streamed world) | DXT images in `.xlev` / `.xsec` | level name, then content | 22,632 of 23,965 larger | 2× (33 more) | **Xbox when larger** |
| Models | float triangle lists | resource hash | 1,541 of 1,541 | none: same meshes | PS2 |
| Animations | the PS2's chunks | resource hash | 616 of 617 | none: same keys | PS2 (behaviour) |
| Levels, collision, paths | the PS2's chunks | resource hash, level name | 64 of 64 | none | PS2 (behaviour) |
| World geometry | Xbox vertex and index buffers | level name | every PS2 level | none known | PS2 |
| Sound effects and speech | XACT, Xbox ADPCM | name hash in the sound list | 23,147 of 25,395 | none: same rates | PS2 |
| Music | XACT, one wave bank a track | track name | 345 of 345 | none: 26,500 Hz against 30,000 | PS2 |
| Movies | Bink `i`, three versions each | file name | 16 of 16 | 1280 × 720 | **Xbox `_hd` when as long** |

#### Textures {#textures}

Xbox textures carry no names, so each PS2 texture is matched by content
([repo:python/src/coney_tools/xbox_match.py](repo:python/src/coney_tools/xbox_match.py)): an Xbox candidate is shrunk to
the PS2 texture's size with a box filter, and the score is the mean colour difference weighted by both alphas plus the
mean alpha difference (0-255 scale). Right pairs score about 5-15 (palette against DXT, plus the detail a larger texture
adds; the colours carry no systematic offset: a fit over level99's matched texels gives Xbox = 0.91 × PS2 + 7 to 12 per
channel); unrelated ones 25 and more. A low score alone is not enough: shrunk far enough, any texture of about the same
colour scores low against a small or blurry PS2 one. So a match also needs the same structure: the correlation of the
two textures' alpha-weighted luminance, at the PS2 size, of 0.5 or more (right pairs reach 0.9 and more; same-coloured
wrong ones lie near 0). A match needs a score under 20, that structure (unless the PS2 texture is flat) and the same
aspect ratio. The candidates of a resource's dictionary are the textures of the Xbox resource with the same hash (the
one at the same position first, when both hold the same number); those of a streamed-world dictionary are all textures
of the level's `.xlev` and `.xsec` files, since the PS2 splits a level's world differently (`s` and `d` halves, world
streams and sector parts).

The rule: **the Xbox texture replaces the PS2 one when it matches and is larger.** One of the same size is the PS2 image
re-encoded from a palette to DXT blocks and stays PS2. One more than twice as wide needs a structure of 0.9 or more (a
flat PS2 texture is never replaced by one): without that test, 183 world textures took an Xbox texture 4 to 32 times as
wide, and by eye most were another picture of the same colour (a wooden box for a gravel ground); with it 33 remain, a
fire-truck texture (128 to 512) and one concrete texture at 4 × 4 in 31 world files. Over the NTSC-U pair (`coney-tools
extract --xbox`, 2026-10-07):

| | Resource dictionaries | Streamed world | All |
| --- | ---: | ---: | ---: |
| PS2 textures | 2,343 | 23,965 | 26,308 |
| Replaced by a larger Xbox texture | 578 | 22,632 | 23,210 |
| ... twice the width and height | 578 | 22,599 | 23,177 |
| ... 4 to 16 times | 0 | 33 | 33 |
| Pixels of the replaced textures, PS2 | 8,656,656 | 92,966,024 | 101,622,680 |
| Pixels of the replaced textures, Xbox | 34,626,624 | 372,094,496 | 406,721,120 |
| Kept: the Xbox match is the same size | | | 1,711 |
| Kept: no candidate matches | | | 1,354 |
| Kept: no Xbox candidates (resource not on the Xbox) | | | 33 |

**Evidence:** inferred (measured by the tool; samples scoring 10-36 were compared by eye: right pairs up to about
18, wrong ones from about 25; the structure limits from 300 random 2× matches, 299 right and kept, and the 183 wider
ones, compared by eye).

#### Models {#models}

Kind 2 (1,413 distinct resources, all single-mesh) has a 0x20-byte object after one vertex and one index buffer
header:

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| `+0x04` | u32 | FVF: `0x152` (position, normal, colour, one UV; 1,105), `0x252` (two UV sets; 210), `0x52` (no UV; 98) | confirmed (code) at XBE `0x000bd3c0` (set as the vertex shader); counts inferred |
| `+0x08` | u8 | primitive type, 5 (triangle list) in all | confirmed (code) at XBE `0x000bd3c0` |
| `+0x09` | u8 | vertex stride: 36, 44, 28 for the FVFs above | confirmed (code) at XBE `0x000bd3c0` |
| `+0x0a` | u16 | 0 (814) or 1 (599); no reader found | inferred |
| `+0x0c` | u16 | triangles (indices = 3 ×) | confirmed (code) at XBE `0x000bd3c0` |
| `+0x10` | f32 | bounding radius | speculative |
| `+0x18`, `+0x1c` | ptr | the vertex and index buffer headers (rebased on load) | confirmed (code) at XBE `0x000bd4e0` |

Vertices are float positions, float normals, a D3DCOLOR and float UVs. The texture is not in the model: the draw
binds the one its caller passes (XBE `0x000bd3c0`), as the PS2 takes the first texture of the dictionary the Object
or Character List gives the model.

Kind 1 (153 skinned characters) has a 0x820-byte object: 32 matrices of 64 bytes at `+0x04`, the triangle count
(u16) at `+0x804` and the headers of two vertex streams and an index buffer at `+0x808`, `+0x80c`, `+0x810`
(confirmed (code): XBE `0x000bec60` rebases them; XBE `0x000beca0` draws stream 0 with stride `0x20` and stream 1
with stride `0x12`, loading one matrix per bone into vertex shader constants from `c60`, three registers each).
Stream 0 is a float position, a packed normal (11:11:10 signed; unit length in every vertex sampled) and two float
pairs, the first the UV; stream 1 is three s16 bone references (bone × 3, a constant-register offset; −3 for an
unused one) and three float weights summing to 1. Inferred from the data, except the strides and the draw.

Kind 3 (6 resources, in levels) is a tree of 0x68-byte nodes (`+0x50`, `+0x54`, `+0x58`: three node indices, −1 for
none; `+0x60`, `+0x64`: vertex and index buffer), rebased by XBE `0x000b83a0` (confirmed (code)).

**Against the PS2.** Each PS2 model is one atomic with one material too. Of 1,382 kind-2 pairs, 1,200 have the same
triangle count, 104 more on the Xbox and 78 fewer, most of these an 82-triangle mesh standing in for many different
PS2 models; of 153 skinned pairs, 145 are the same. In all, 321,262 Xbox triangles against 308,036 (kind 2) and
183,893 against 184,029 (kind 1). The Xbox meshes are the PS2's at float precision as triangle lists: no gain, so
models stay PS2. **Evidence:** inferred (counted against the PS2 clumps of the same resource hash).

#### Animations {#animations}

Keyframe chunks (`0x00`) match by resource hash (616 of 617) and have the same sizes. 71 are identical; in 326 only
byte `+1` of the 8-byte keys differs, which the sampler does not read ([Animation data](formats/animation.md));
in the other 290 the differences also reach the 24-byte events at the end, mostly their bytes `+0x0c`-`+0x17` (a
transform's rotation and fields not traced). Animations are behaviour (events drive effects and paired moves), so the
PS2's stay. **Evidence:** inferred (byte comparison).

#### Levels and the streamed world {#worlds}

The streamed world is named `ee_files\sectors\<level>.xlev` (79 entries, kind 5) and
`ee_files\sectors\<level>_<n>.xsec` (1,969, kind 4): all 2,048 world entries take these names, and one more `.xsec`
name hashes to a texture dictionary resource. **Evidence:** confirmed (name hash); the names were read from
build-tool log text left in the images (`Writing sector: rundata_XBox/ee_files/sectors/level1_0.xsec`, `Saving BSP:
.../sectors/level1.xlev`). A `.xlev` holds the level's textures and an object of sector records; a `.xsec` holds a
sector's vertex and index buffers (stride 28: float position, packed normal, colour, float UV; inferred) and its own
textures. Level resources, collision and paths are the PS2's data ([Formats, kind by kind](#formats-kind-by-kind)).
The world geometry is not converted: the PS2 world is the reference and the Xbox one models the same city; only its
textures are taken.

#### Sound, speech and music {#sound}

The Xbox **sound list** (chunk `0x29` of the global resource) has 23,523 records of 12 bytes `{u32 hash, u32 sound
index, u16 wave bank, u8 volume, u8 class}`, sorted by hash. The hash is the PS2's (CRC-32 of the name), so a PS2
sound finds its Xbox sound by name: 23,147 of the PS2's 25,395 hashes are on the Xbox, and 376 Xbox hashes are not on
the PS2. The records per bank equal each bank's wave count (bank 0: 542, 1: 1,014, 2: 19,276 speech and voices,
3: 2,228, 4: 99, then one each for 5-368). The sound index is global (it runs past a bank's wave count) and the sound
bank `xbox000.xsb` (XACT `SDBK` version 11, 23,523 entries) maps it to a wave; that table is not decoded. A debug copy
of the list, in another instance of the global resource, has the last 16 characters of each name in front of each
record (28 bytes; the PS2's has the same, 32 bytes). **Evidence:** inferred (the counts and the shared hashes).

The waves are Xbox ADPCM (4 bits a sample, like the PS2's ADPCM), mono at the PS2's rates (22,050 and 22,500 Hz for
most effects and speech). The **music** is 364 one-wave banks, stereo at 26,500 Hz (two at 30,000), named
`vags/xboxmusic/music/<track>` in the music list (chunk `0x31`, the PS2's 345 records with that prefix), against the
PS2's 30,000, 44,100 and 32,250 Hz. The same recordings at no higher quality: the sound stays PS2.

#### Movies {#movies}

Each of the 16 PS2 movies (640 × 448, Bink `i`, 48,000 Hz stereo) has three Xbox versions: `<name>.bik` (640 × 480,
4:3), `<name>_w.bik` (640 × 480, a 16:9 picture squeezed into 4:3) and `<name>_hd.bik` (1280 × 720, 16:9; 640 × 480
for `trailer`). The 16:9 versions are rendered wider, not cropped: at the same frame the `_hd` picture shows more of
the scene at both sides than the PS2's (compared by eye on `l9_in`; inferred). Most Xbox movies have 44,100 Hz audio;
`plogo`, silent on the PS2, has a track. The frame counts and rates match the PS2's to 0.1 s except `l51_in` (609.7 s
on the PS2, 617.7 s on the Xbox), a different cut. **Evidence:** inferred (Bink headers; frames by eye).

The rule: **`_hd` replaces the PS2 movie when it has more pixels and lasts as long (to 0.5 s)**, since the subtitles are
timed against the PS2's. Over the NTSC-U pair 15 of the 16 are replaced (14 at 1280 × 720, `trailer` at 640 × 480);
`l51_in` stays the PS2's. A 16:9 movie needs the player to keep the file's aspect ratio ([Movies](movies.md)).

## Behaviour

How the Xbox finds a file, for comparison with [File I/O](file-io.md): a name is prefixed with `ee_files\`,
normalised, hashed (case-folded CRC-32) and found by a linear scan of the 10,587 entries, as `DVDWadIndex::Find`
does on the PS2. **Evidence:** confirmed (code) at XBE `0x0006c4f0`, `0x000704a0` and `0x00068590`. Nothing else
about the port's behaviour is in scope: it is not a reference for Coney.

## Coney's implementation

**The survey as a check (done).** `coney-tools xbox` ([The coney-tools command line](../guides/coney-tools.md#xbox))
reads the disc as this page describes and prints counts only: `python/src/coney_tools/xdvdfs.py` (the XDVDFS reader:
full image, XISO or folder), `chunks.py` (the chunk container, shared with the PS2 archive) and `xbox.py` (the index,
the name hash, the kinds of entry, the resource index and the texture-chunk header). Run against the NTSC-U disc on
2026-10-04 it reproduces this page's counts: 431 files in 2 directories (5,620,148,362 bytes), 10,587 entries
(1,726,598,311 bytes) and every row of [Kinds of entry](#kinds-of-entry); 43,104 resources and 108,836 chunks; the
graphics chunks by first words; 18,245 texture chunks (501,984,640 bytes), all readable; the resource-hash overlap
(2,092 texture dictionaries, 1,541 models, 616 of 617 animations, 53 characters); the 2,027 equal-count texture
resources; and 4,038 names (3,957 from the PS2's names). The rest of this section is the plan for the engine.

**Policy.** The PS2 disc is required and stays the reference for behaviour. The Xbox disc is optional, supplied by
the player like the PS2 one, and only ever replaces *assets*, at install: never scripts, scenes, levels, collision,
animations or anything the simulation reads. Its extra content (31 levels, 49 scene records, 36 scripts; most of the
levels are test levels) is ignored.

**Extraction (done).** `coney-tools extract [DISC] OUT_DIR --xbox XBOX_DISC`
([guide](../guides/coney-tools.md#extract)) reads the Xbox disc straight from the image (XDVDFS, then `XBoxWad.idx`
and its volumes) and resolves per asset as [Asset kinds](#asset-kinds) says: the PS2 stages write everything, then
each Xbox texture that matches and is larger is written over the PS2 file at the same path, and each `_hd` movie that
is as long is copied in place of the PS2 one; the record in `textures/index.json` or `movies/index.json` gets
`"source": "xbox"` and keeps the PS2 size. The folder therefore has the same files with or without the Xbox disc, and
nothing reading it needs to know which disc an asset came from. Code:
[repo:python/src/coney_tools/xbox_gfx.py](repo:python/src/coney_tools/xbox_gfx.py) (resource images, DXT),
[repo:python/src/coney_tools/xbox_match.py](repo:python/src/coney_tools/xbox_match.py) (matching and the rule),
[repo:python/src/coney_tools/xbox_source.py](repo:python/src/coney_tools/xbox_source.py) (the disc as a source) and
[repo:python/src/coney_tools/extract_xbox.py](repo:python/src/coney_tools/extract_xbox.py) (the hooks).

**What the engine needs.** The engine reads the discs' own formats, not the extracted PNGs, so to draw an Xbox
texture it needs: the XDVDFS reader and `XBoxWad.idx`, the [texture
map](#texture-map) to know which PS2 texture to swap, the [resource image](#resource-images) reader and DXT1/3/5
textures (a DXT raster or a CPU decode). A 16:9 movie needs the movie player to keep the file's aspect ratio rather
than 4:3.

### Resource index {#resource-index}

Finding an Xbox resource by its hash means parsing every pack and standalone resource of the archive (about 1 GB),
so the result is kept in a file, `xbox-resources.bin` (`coney-tools xbox index`, or `extract --xbox-index`). It is
Coney's own format, and the layout the engine's port reads. All values little-endian:

```c
struct XboxResourceIndexHeader {   // 32 bytes
    char     magic[4];             // "CXRI"
    uint32_t version;              // 1
    uint32_t recordCount;
    uint8_t  indexSha1[20];        // SHA-1 of the XBoxWad.idx it was built from; another value means rebuild
};
struct XboxResourceIndexRecord {   // 24 bytes, recordCount of them, sorted by (resourceHash, chunkType)
    uint32_t resourceHash;         // the chunk container's resource hash, the same on both discs
    uint16_t chunkType;            // 0x2a texture, 0x47 model, ...
    uint16_t volume;               // index into XBoxWad.idx's volume list
    uint64_t offset;               // byte offset of the chunk's data (after its 16-byte header) in that volume
    uint32_t size;                 // bytes of chunk data
    uint32_t entry;                // the XBoxWad.idx entry holding it
};
```

Only the first instance of each resource hash is kept (packs repeat shared resources), every chunk of it, and the
chunks of one key stay in resource order (a texture dictionary's `0x2a` chunks are its textures in order). Over the
NTSC-U disc: 10,445 records (250,712 bytes) for 4,218 resource hashes; 2,312 of them are texture chunks. Code:
[repo:python/src/coney_tools/xbox_index.py](repo:python/src/coney_tools/xbox_index.py).

### Texture map {#texture-map}

Which Xbox texture replaces which PS2 one is found by decoding and comparing both ([Textures](#textures)), far too
slow for loading a level, so it is done once, at install, and kept in `xbox-textures.bin` (`coney-tools xbox
texture-map`, or `extract --xbox-texture-map`). A reader looks a PS2 texture up by the dictionary it was loaded from
and its name, and gets where the Xbox texture lies in the archive volumes. All values little-endian:

```c
struct XboxTextureMapHeader {      // 56 bytes
    char     magic[4];             // "CXTM"
    uint32_t version;              // 1
    uint32_t recordCount;
    uint32_t reserved;             // 0
    uint8_t  ps2DirSha1[20];       // SHA-1 of the PS2 disc's WARRIORS.DIR it was made from
    uint8_t  xboxIdxSha1[20];      // SHA-1 of the Xbox disc's XBoxWad.idx it was made from
};
struct XboxTextureMapRecord {      // 32 bytes, recordCount of them, sorted by (kind, ps2Key, nameHash)
    uint32_t ps2Key;               // kind 0: the PS2 dictionary's resource hash; kind 1: the WARRIORS.DIR name hash
                                   // of the streamed-world file (`_sec.wld`, `_ms<n>.sec`) holding the dictionary
    uint32_t nameHash;             // CRC-32 of the PS2 texture's name, lower-cased
    uint16_t kind;                 // 0 resource dictionary, 1 streamed-world dictionary
    uint16_t textureNumber;        // the texture's position among the Xbox resource image's texture headers
    uint16_t volume;               // index into XBoxWad.idx's volume list
    uint16_t reserved;             // 0
    uint64_t imageOffset;          // byte offset of the Xbox resource image in that volume
    uint32_t imageSize;            // bytes of the resource image
    uint16_t width, height;        // the Xbox texture's size
};
```

A key that two PS2 textures share with different Xbox textures (two variants of one resource, a name used twice in one
dictionary) is left out, so a reader never takes the wrong one. Over the NTSC-U pair: 23,210 records (742,776 bytes),
one per replaced texture, none left out. Code:
[repo:python/src/coney_tools/xbox_texture_map.py](repo:python/src/coney_tools/xbox_texture_map.py).

## Open questions

1. **DXT2/3.** Whether the `0x0E` textures are premultiplied (DXT2) or not (DXT3): none of the matched ones has
   partly transparent texels to tell (the `0x0F` ones are straight DXT5).
2. **The index header's third word** (`0x00100000`) and the volumes' trailing `0xffffffff`: unused by the lookup;
   probably a volume size limit and a flag.
3. **Unnamed Xbox entries.** 4,498 of 10,587 (models, textures, characters, 652 packs, 227 scripts, 18 text
   entries) once the 2,048 world files are named; the subfolder scheme (`paks\`, `anims\`, `sectors\`) suggests the
   rest also live in subfolders.
4. **Sound index to wave.** The table in `xbox000.xsb` that maps the sound list's index to a wave bank entry; only
   needed if an Xbox sound is ever wanted.
5. **Model fields.** The kind-2 model's `+0x0a` (0 or 1) and the skinned vertex's second float pair.
6. **Unmatched PS2 textures.** 1,354 PS2 textures with Xbox candidates find no match: art the Xbox port
   changed, or candidates in another level's files.
