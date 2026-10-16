# Archive contents

What the PS2 archive `WARRIORS.WAD` holds, sorted by file kind. The archive and its directory `WARRIORS.DIR` sit at
the root of the NTSC-U disc (`SLUS_212.15`). Every count and size below was read from the disc image. All values are
little-endian.

## The archive in brief

| Fact | Value |
| --- | ---: |
| `WARRIORS.WAD` size | 1,495,371,776 bytes |
| `WARRIORS.DIR` size | 128,428 bytes |
| Entries listed in the directory | 10,701 |
| Bytes held by the entries | 1,484,729,244 |
| Padding between entries | 10,641,917 bytes; no gap reaches 2,048 bytes |
| Bytes after the last entry | 615 |
| Entries whose name was recovered | 6,114 |

Every entry starts at a multiple of 2,048 bytes. The directory gives each entry's offset, size and name hash, not
its name. A name is confirmed when the CRC-32 of `./ee_files/` plus the lower-case name equals an entry's hash. The
directory layout itself belongs to the archive directory page.

## File kinds

Each entry was assigned a kind by a structural test that must account for the whole entry. Every entry passes
exactly one test, and every recovered name has the extension its kind implies.

| Kind (extension) | Entries | Bytes | Named | Format |
| --- | ---: | ---: | ---: | --- |
| Pack (`.pak`) | 881 | 671,648,576 | 234 | Grouped chunk container: 2 to 351 resources |
| Sector file (`<world>_ms<i>.sec`) | 1,915 | 311,862,912 | 1,886 | 16-byte header, then a texture dictionary section and the sector's geometry |
| Scene record (`.scn`) | 2,765 | 197,667,392 | 2,722 | Not chunked; the first u32 equals the entry's size in all 2,765 |
| Flat resource (several extensions, see below) | 4,246 | 194,058,208 | 627 | Flat chunk container: one resource |
| World stream (`<world>_sec.wld`) | 160 | 70,424,239 | 160 | u32, then a texture dictionary section (0x16), then a world section (0x0B) |
| Sound bank (`.msb`) | 21 | 27,360,592 | 13 | No header; 16-byte blocks |
| Lua script (`.lua`) | 467 | 10,379,282 | 235 | Compiled Lua 4.0 chunk; all 467 begin with the same 13 header bytes, `1B 4C 75 61 40` first |
| Object list (`<level>_objs.txt`) | 63 | 1,111,944 | 62 | Plain text; the first line is a count |
| Font bitmap (`.bmp`) | 1 | 98,358 | 1 | Windows bitmap (`BM`) |
| Memory card icon (`.ico`) | 1 | 79,128 | 1 | Starts `00 00 01 00` |
| Sound bank index (`.msd`) | 21 | 21,568 | 13 | Pairs of u32 (hash, offset into the matching `.msb`), ended by a zero pair |
| Stream manifest (`<world>_sec.mem`) | 159 | 14,420 | 159 | 3 u32, then as many 8-byte pairs as the third u32 says |
| Font metrics (`.met`) | 1 | 2,625 | 1 | Text starting `METRICS1` |
| **Total** | **10,701** | **1,484,729,244** | **6,114** | |

### Flat resources by shape

The chunk types a flat resource holds tell what it is. The container layout and the type ids are on the chunk
container page.

| Shape (chunk types) | Extension | Entries | Bytes | Named |
| --- | --- | ---: | ---: | ---: |
| Texture dictionary (0x2A) | none recovered | 1,509 | 36,595,936 | 0 |
| Texture dictionary with a sprite page (0x2A, 0x4C) | none recovered | 576 | 88,751,392 | 0 |
| Model (0x47) | none recovered | 1,324 | 9,979,200 | 0 |
| Skinned model (0x28, 0x47) | none recovered | 153 | 14,952,240 | 0 |
| Character (0x00, 0x02, 0x08, 0x45) | none recovered | 53 | 7,532,016 | 0 |
| Animation (0x00, 0x02) | `.anm` | 564 | 2,600,960 | 561 |
| Level (13 types, 18 chunks) | `.lev` | 64 | 31,902,768 | 64 |
| Global lists (9 types) | `.glr` | 2 | 1,677,296 | 1 |
| Scene list (0x43) | `.cnk` | 1 | 66,400 | 1 |

The first 3,615 entries of the directory are texture dictionaries, models, skinned models and characters, and none
of their names is recovered.

### Sector files and world streams

- Of the 1,915 sector files, 1,911 begin with a 16-byte header: u32 1, two u32 zeros and a u32 hash. A texture
  dictionary section (type 0x16, stamp `0x1C02000A`) follows the header in all 1,911.
- The other four, `courtyard_ms1.sec` to `courtyard_ms4.sec`, have no such header. They start directly with a
  texture dictionary section stamped `0x1803FFFF`.
- Of the 160 world streams, 159 are stamped `0x1C02000A`; `courtyard_sec.wld` is stamped `0x1803FFFF`.
- In all 159 stream manifests, the first u32 equals the size of the matching `<world>_sec.wld`. In 157 of them, the
  first u32 of pair k equals the size of `<world>_ms<k>.sec`, counting k from 1.
- The manifests list 1,564 parts in all, 347 fewer than the 1,911 headed sector files. `courtyard_sec.wld` is the
  one world stream without a manifest.

### Sound banks

The 13 named index files all fit their banks. Every offset lies inside the matching `.msb`, offsets ascend, and
each is a multiple of 16.

| Bank | `.msb` bytes | Pairs in `.msd` |
| --- | ---: | ---: |
| `sound` | 1,613,664 | 319 |
| `level81` | 1,607,392 | 311 |
| `gallery` | 1,475,456 | 287 |
| `armies` | 1,330,592 | 261 |
| `level3` | 1,292,176 | 243 |
| `lizzie` | 1,359,248 | 227 |
| `spookorama` | 1,142,160 | 206 |
| `birdie` | 1,244,624 | 204 |
| `diego` | 919,344 | 190 |
| `luther` | 1,116,816 | 188 |
| `menu` | 175,536 | 14 |
| `pause` | 57,152 | 4 |
| `armload` | 1,055,936 | 2 |

The other eight banks and eight index files are unnamed. Their entries are consecutive in the directory.

## Notable named files

### Global files

| File | Bytes | Kind | Contents |
| --- | ---: | --- | --- |
| `warriors.glr` | 635,488 | Flat resource, 9 chunks | Game-wide lists: static sounds, music, character list, object list, sound command and material data, particle page header, anim list, dependency list |
| `global.pak` | 599,968 | Pack of 11 resources | Texture dictionaries |
| `scene_list.cnk` | 66,400 | Flat resource, one 0x43 chunk | 2,765 records of 24 bytes: u32 index, u32 size, 16-byte scene name |
| `global.lua` | 169,601 | Lua script | |
| `enum_preload.lua` | 12,501 | Lua script | |
| `credits.lua` | 24,153 | Lua script | |
| `warr.ico` | 79,128 | Memory card icon | |
| `cn12.bmp`, `cn12.met` | 98,358 and 2,625 | Bitmap and its metrics text | |

The scene list's records are numbered 0 to 2,764 in order. For 2,741 records, the name plus `.scn` finds an archive
entry, and 2,740 of those have the record's size.

### Level files

Each level `level<N>` has a set of files. The disc has 64 level files, for N = 1, 2, 3, 5, 9, 11, 14, 20, 31, 34,
51, 52, 54, 55, 60 to 64, 80 to 84, 86, 87, 92, 93, 95, 99 to 105, 107 to 116, 118 to 124, 126 to 134, 136 and 137.

| File | Count on the disc | Kind |
| --- | ---: | --- |
| `level<N>.lev` | 64 | Flat resource, 18 chunks; resource hash is the CRC-32 of `level<N>` |
| `level<N>.lua` | 64 | Lua script |
| `level<N>_objs.txt` | 62 named of 63 | Text; 26 of them hold only the count `0` |
| `level<N>_<k>.pak` | 159 named | Packs of level props |
| `level<N>d_sec.wld` and `level<N>s_sec.wld` | 79 of each | World streams, with `_sec.mem` manifests and `_ms<i>.sec` sector files |

Examples: `level1.lev` is 46,992 bytes, `level1.lua` 448 bytes and `level1_1.pak` 220,624 bytes. `level1_objs.txt`
is 3 bytes long. The largest pack is `level64_1.pak` at 5,658,864 bytes. Two more worlds, `courtyard` and
`objarena`, are not numbered levels.

### Script files

Of the 235 named Lua scripts, 64 are the `level<N>.lua` files. The rest include:

- `level<N>_<part>.lua` scripts for parts of a level, such as `level11_chapter1.lua`, `level20_bossfight.lua`,
  `level3_gallery.lua` and `level31_scenetest.lua`.
- Configuration scripts: `config_easy.lua`, `config_normal.lua`, `config_hard.lua`, `config_fury.lua`,
  `config_rumble.lua` and `config_preload.lua`.
- Other game scripts: `brawl.lua`, `caps.lua`, `car.lua`, `charnames.lua`, `dlgpopup.lua`, `hifi.lua` and
  `kinghill.lua`.

### Scene packs

Of the named packs, 74 are scene packs named `l<N>_<scene>.pak`, for example `l11_c1.pak` (27,264 bytes, 3
resources) and `l11_cam_pan.pak` (569,056 bytes).

## Behaviour

- `warriors.glr`, `global.pak` and `scene_list.cnk` are each loaded once, when the game starts.
- A sound bank is loaded by its bank name, for example `armies`, which brings in `armies.msb` and `armies.msd`
  together.
- A partial load of a sound bank reads only the first 65,536 bytes of its `.msb`.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00187c38` | Load a pack and its resources | File kinds |
| `0x0040d900` | Load the game-wide lists in `warriors.glr` | Global files |
| `0x00184918` | Load the global texture pack `global.pak` | Global files |
| `0x003535e8` | Load the scene list | Global files |
| `0x0014c620` | Load a sound bank with its index | Behaviour |

## Acceptance checks

- The PS2 archive directory lists 10,701 entries, and every entry starts at a multiple of 2,048 bytes.
- The entries hold 1,484,729,244 bytes; the rest of the 1,495,371,776-byte archive is padding.
- The archive holds 881 packs and 4,246 flat resources, 2,765 scene records and 467 compiled Lua scripts.
- It also holds 1,915 sector files, 160 world streams, 159 stream manifests and 63 object lists.
- It also holds 21 sound banks with 21 index files, one bitmap, one metrics text and one memory card icon.
- `warriors.glr` is 635,488 bytes, `global.pak` is 599,968 bytes and `scene_list.cnk` is 66,400 bytes.
- `level1.lev` is 46,992 bytes and `level1_objs.txt` holds only a zero count.
- 64 level files exist, each with a `level<N>.lua` script of the same number.
- Every `_sec.mem` manifest begins with the size of its world stream.
- Every named `.msd` index points only inside its `.msb` bank, at multiples of 16.
- `courtyard_sec.wld` and `courtyard_ms1.sec` to `courtyard_ms4.sec` carry the older stamp `0x1803FFFF`.

## Open questions

- 4,587 entries have no recovered name. Most are the 3,615 model, texture and character resources at the start of
  the directory, 647 packs, 232 Lua scripts and 43 scene records.
- The name `throwcam.anm` matches a model resource (entry 923). It is counted as a chance match and left out of the
  named counts. Chance matches were estimated at a few dozen, at most, over the whole name search. Names not tied
  to a fixed pattern carry that small risk.
- The 347 headed sector files that no manifest lists are not explained.
- The second global resource (1,041,808 bytes, same hash and chunk list as `warriors.glr`) has no name. Which game
  mode or language uses it is open.
- The eight unnamed sound banks and their indexes are not tied to names.
- The `.msb` blocks look like PS2 sound ADPCM, but no bank was decoded here.
- The second u32 of a stream manifest and the second value of each pair are not explained. Two manifests do not
  match their sector file sizes.
- The code that loads the second, unnamed global resource was not located.
- The Xbox disc was not examined for this page. Its archive contents may differ.
