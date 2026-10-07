# Asset inventory

Verified against: the NTSC-U disc (`SLUS_212.15`, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Counts from
`coney-tools extract` (2026-10-07); counts and hashes only.

## Purpose

Every kind of file on the disc, where its format is documented and what `coney-tools extract` turns it into. The
extracted folder is what Coney's install step will write from the player's disc and what the engine and mods will
read ([coney-tools](../../guides/coney-tools.md#extract)). A format marked **raw** is written as stored, with a JSON
description of its header, until its decoder lands.

## Data

### Disc files

| File | Count | Format page | Extracted as (type) |
| --- | ---: | --- | --- |
| `WARRIORS.DIR`, `WARRIORS.WAD` | 2 | [WARRIORS.DIR / .WAD](wad-dir.md), [WAD contents](wad-contents.md) | the entries, below (`index/wad.json` lists them all) |
| `IOP/BFW.SND` | 1 | [Audio data](audio.md#bfw-snd) | 22,472 mono and 248 stereo WAV (`audio`) |
| `IOP/MUSIC.SND` | 1 | [Audio data](audio.md#music) | 345 stereo WAV (`audio`) |
| `PSS/*.BIK` | 16 | [Movies](../movies.md) (public Bink 1) | copied as `.bik` with their header values (`movies`) |
| `SLUS_212.15` | 1 | [Boot](../boot.md) | copied (`disc`) |
| `MODULES/*.IRX`, `IOPRP300.IMG` | 7 | [File I/O](../file-io.md), [Boot](../boot.md) (stock Sony modules but `IOP.IRX`) | copied (`disc`) |
| `SYSTEM.CNF` | 1 | [Overview](../overview.md) | copied (`disc`) |

### WAD entries

| Kind | Entries | Format page | Extracted as (type) |
| --- | ---: | --- | --- |
| Pack | 881 | [WAD contents](wad-contents.md#chunk-container) | its resources, once each (below); `index/wad.json` lists each pack's resources |
| Resource | 4,246 | [WAD contents](wad-contents.md#common-resource-shapes) | by shape, below |
| Lua bytecode | 467 | [Compiled Lua chunks](lua-chunks.md) | the chunks as stored (`scripts`) |
| Scene record | 2,765 | [Scenes](../scenes.md#data) | **raw** |
| Sector atomics | 1,911 | [The streamed world](../world.md#part-file), [RenderWare](renderware.md) | textures as PNG (`textures`); geometry **raw** |
| World stream | 159 | [The streamed world](../world.md#world-stream) | textures as PNG; world **raw** |
| Stream manifest | 159 | [The streamed world](../world.md#manifest) | **raw** with every value in the JSON |
| Object list (`_objs.txt`) | 63 | [Objects](../objects.md) | **raw** (text) |
| Sound bank and index | 21 + 21 | [Audio data](audio.md#banks) | 2,675 WAV in `audio/banks/<bank>/` (`audio`) |
| Older RenderWare files | 5 | [RenderWare](renderware.md#older-streams) | **raw** |
| Font metrics, bitmap, memory card icon | 3 | [GUI](../gui.md#the-metrics1-file), [Save](../save.md) | **raw** |

### Resources (distinct, by shape)

A resource held by several packs is extracted once; 35 hashes have two different resources, the second written with
`~2`.

| Shape (chunks) | Distinct | Format page | Extracted as (type) |
| --- | ---: | --- | --- |
| Texture dictionary (`0x2A`, `0x4C`) | 2,085 | [RenderWare](renderware.md#texture-dictionary), [GUI](../gui.md#particle-page) | PNG per texture, sprite rectangles in the index (`textures`) |
| Model (`0x47`, `0x28`) | 1,477 | [RenderWare](renderware.md), [Characters](../characters.md#character-geometry) | **raw** |
| Animation (`0x00`, `0x02`) | 564 | [Animation](animation.md) | **raw** |
| Character (`0x00`, `0x02`, `0x08`, `0x45`) | 53 | [Characters](../characters.md#files), [Animation](animation.md#anim-range-list) | **raw** |
| Level (`.lev`, 13 chunk types) | 64 | [Level loading](../level-loading.md#the-level-file), [Collision](../collision.md) | textures as PNG; the rest **raw** |
| Global (`warriors.glr`) | 2 | [WAD contents](wad-contents.md#object-list), [Audio data](audio.md) | sound tables as JSON (`audio`); the rest **raw** |
| Scene list (`0x43`) | 1 | [Scenes](../scenes.md#data) | **raw** |

### Extracted totals

| Type | Files | Notes |
| --- | ---: | --- |
| `disc` | 10 | 9 copies and `disc/files.json` (every disc file with its size and SHA-1) |
| `movies` | 17 | 16 movies and `movies/index.json` |
| `audio` | 25,743 | 25,740 WAV (8.2 GB) and three tables; 20,568 sound names recovered, the rest `unnamed/<hash>.wav` |
| `scripts` | 468 | 467 chunks (356 named) and `scripts/index.json` |
| `textures` | 26,309 | 26,308 PNG from 4,411 dictionaries, and `textures/index.json` |
| `index` | 1 | `index/wad.json` |
| `raw` | 14,452 | 7,226 files, each with its JSON description |

## Open questions

- Formats still raw (the list above): models, animations, characters, levels (collision, paths, occluders,
  subtitles), the streamed world's geometry, scene records, the global lists, object lists, the memory card icon and
  font files.
