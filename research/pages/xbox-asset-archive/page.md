# Xbox asset archive

## Scope and sources

The Xbox disc stores the game's data in one archive: a resource index, `XBoxWad.idx`, and eight data volumes
(`System.wad0`, `Main.wad0` to `Main.wad3`, `Paks.wad0` to `Paks.wad2`). This page gives the index format, the name
hash, the kinds of entry, the chunk container most entries use, and the Xbox layouts of textures and models, with
counts taken from the disc. Where the files sit on the disc is on the Xbox disc layout page.

All multi-byte values are little-endian. Entry numbers are positions in the index, counted from 0.

## The resource index (`XBoxWad.idx`)

169,700 bytes: a 20-byte header, a table of 8 volume records of 36 bytes, then 10,587 entry records of 16 bytes
(20 + 8 x 36 + 10,587 x 16 = 169,700).

### Header

| Offset | Size | Meaning | Value on this disc |
| --- | --- | --- | --- |
| 0x00 | 4 | number of volumes | 8 |
| 0x04 | 4 | number of entries | 10,587 |
| 0x08 | 4 | not identified | 0x00100000 |
| 0x0C | 4 | file offset of the volume table | 0x14 (20) |
| 0x10 | 4 | file offset of the entry table | 0x134 (308) |

### Volume records (36 bytes each, from offset 0x14)

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 32 | volume file name, zero-terminated; the bytes after the terminator are leftovers, not zeros |
| 0x20 | 4 | 0xFFFFFFFF in all 8 records |

| Volume number | File | Entries | Bytes in entries | Entry numbers |
| ---: | --- | ---: | ---: | --- |
| 0 | `System.wad0` | 2 | 1,677,888 | 0-1 |
| 1 | `Main.wad0` | 5,666 | 298,075,191 | 2-5,667 |
| 2 | `Main.wad1` | 2,651 | 301,605,376 | 5,668-8,318 |
| 3 | `Main.wad2` | 1,202 | 303,957,376 | 8,319-9,520 |
| 4 | `Main.wad3` | 183 | 46,325,504 | 9,521-9,703 |
| 5 | `Paks.wad0` | 252 | 363,552,624 | 9,704-9,955 |
| 6 | `Paks.wad1` | 491 | 366,501,184 | 9,956-10,446 |
| 7 | `Paks.wad2` | 140 | 44,903,168 | 10,447-10,586 |

### Entry records (16 bytes each, from offset 0x134)

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | position: bits 24-31 the volume number, bits 0-23 the offset in the volume in units of 1,024 bytes |
| 0x04 | 4 | size in bytes |
| 0x08 | 4 | always 0 |
| 0x0C | 4 | name hash (below) |

Facts that hold for every record on the disc:

- Every offset is an even number of units, so every entry starts on a 2,048-byte boundary of its volume.
- Within a volume, entries do not overlap, and the record order is the order of the entries in the volume.
- The gap after an entry is under 2,048 bytes; the last entry of `Main.wad1`, `Main.wad2`, `Main.wad3` and
  `System.wad0` ends exactly at the end of its volume.
- The records list the volumes in order 0 to 7; entry sizes add up to 1,726,598,311 bytes.

Compared with the PS2's `WARRIORS.DIR` (a 16-byte header holding the count 10,701 then 12 zero bytes, and 12-byte
records of byte offset, size and hash into the single file `WARRIORS.WAD`), the Xbox record adds a zero word and
replaces the byte offset with a volume number and a 1,024-byte unit offset.

## Name hash

The hash in an entry record is the standard CRC-32 (the reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF,
final inversion; the same value zlib gives) of the entry's lowercase path written with the prefix `ee_files\` and
backslashes.

| Name | Hash | Entry |
| --- | --- | --- |
| `ee_files\paks\global.pak` | 0x4CFC9C5C | 0 and 9,704 |
| `ee_files\scene_list.cnk` | 0x7516D704 | 1 and 3,523 |
| `ee_files\level1.lev` | 0xE79741B6 | 2,950 |
| `ee_files\sectors\level1.xlev` | 0x8B2420EE | 7,656 |
| `ee_files\sectors\level100_0.xsec` | 0xC3F707B9 | 7,658 |

On the PS2 the directory hashes the same names with the prefix `./ee_files/` and forward slashes
(`./ee_files/scene_list.cnk` gives 0x13650964), so no hash is shared between the two directories.

The index has 10,584 distinct hashes. Three hashes occur twice:

| Hash | Entries | Why |
| --- | --- | --- |
| 0x4CFC9C5C | 0 (`System.wad0`), 9,704 (`Paks.wad0`) | `paks\global.pak` is stored twice; the two copies are byte-identical |
| 0x7516D704 | 1 (`System.wad0`), 3,523 (`Main.wad0`) | `scene_list.cnk` is stored twice; byte-identical |
| 0xBCE5ADDE | 229, 397 | two different scene records whose names collide: `ee_files\bar_wcm_swn_001.scn` and `ee_files\gen_piss_end.scn` |

### Recovered names

The names below were recovered by hashing candidate paths built from strings found in the disc's own data (scene
names, script text, object lists, the executable's strings) and keeping only matches whose entry kind agrees with the
extension. 6,103 of the 10,587 entries are named this way.

| Name pattern | Kind | Named | Entries of that kind |
| --- | --- | ---: | ---: |
| `ee_files\<scene>.scn` | scene record | 2,801 | 2,814 |
| `ee_files\sectors\<level>_<n>.xsec` | streamed world sector | 1,969 | 1,969 |
| `ee_files\anims\<name>.anm` | animation resource | 561 | 564 |
| `ee_files\<name>.lua` | Lua script | 274 | 503 |
| `ee_files\paks\<name>.pak` | package | 239 | 884 |
| `ee_files\<level>.lev` | level resource | 95 | 95 |
| `ee_files\<level>_objs.txt` | object list | 80 | 80 |
| `ee_files\sectors\<level>.xlev` | streamed world level file | 79 | 79 |
| `ee_files\scene_list.cnk` | scene list resource | 2 | 2 |
| `ee_files\warriors.glr` | global resource | 1 | 2 |
| `ee_files\cn12.bmp`, `ee_files\cn12.met` | font bitmap and metrics | 2 | 2 |

Package names follow `level<N>_<k>.pak` (158), `l<N>_c<k>.pak` and its `_a`, `_b`, `_c` variants (65), `global.pak`
and a few scene names (`l<N>_cops.pak`, `l<N>_bus_hide.pak` ...). Level resources include `level1.lev` to
`level137.lev` (79 of them), `combatselect.lev`, `objarena.lev` and 15 test levels (`test_car.lev`,
`test_combat_ben.lev` ...).

In `Main.wad0`, entries 210 to 3,705 hold the named scene records, Lua scripts, levels and object lists sorted by
name (the sort departs from plain byte order in a few dozen places), and entries 3,706 to 4,269 hold the animations.
The `.xlev` and `.xsec` entries of `Main.wad1` to `Main.wad3` are in byte order of their names, so `level100.xlev`
and its sectors come between `level1.xlev` and `level1_0.xsec`.

## Kinds of entry

Every entry falls into exactly one of these kinds, recognised by its first bytes or by its chunk types.

| Kind | Entries | Bytes | How it is recognised |
| --- | ---: | ---: | --- |
| Package | 884 | 776,567,280 | header hash 0xDE686795 (below) |
| Texture resource | 2,062 | 138,983,792 | one chunk 0x2A, or 0x2A and 0x4C |
| Model resource | 1,477 | 20,586,960 | chunk 0x47 (1,324), or 0x47 and 0x28 (153) |
| Animation resource | 564 | 2,600,960 | chunks 0x00 and 0x02 |
| Character resource | 53 | 7,532,016 | chunks 0x00, 0x02, 0x08, 0x45 (4 to 1,190 chunks) |
| Level resource | 95 | 44,488,240 | 17 chunks of types 0x03-0x07, 0x15, 0x17, 0x2A, 0x40, 0x47, 0x51, 0x52, 0x53 |
| Global resource | 2 | 1,389,840 | 9 chunks: 0x29, 0x31, 0x44, 0x46, 0x48, 0x49, 0x4D, 0x4E, 0x4F |
| Scene list resource | 2 | 135,168 | one chunk 0x43 |
| Scene record | 2,814 | 200,615,808 | first 32-bit value equals the entry size |
| Lua 4.0 bytecode | 503 | 10,931,421 | byte 0x1B, the letters `Lua`, then version byte 0x40 |
| Object list and font metrics text | 81 | 1,154,288 | text: a decimal count line, or `METRICS1` |
| Streamed world level (`.xlev`) | 79 | 216,996,864 | first 32-bit value 5 |
| Streamed world sector (`.xsec`) | 1,969 | 299,972,608 | first 32-bit value 4 |
| XACT sound bank | 1 | 4,544,708 | magic `SDBK`; identical to `audio/xbox000.xsb` |
| Bitmap (`cn12.bmp`) | 1 | 98,358 | magic `BM`, 256 x 128, 24 bits |
| **Total** | **10,587** | **1,726,598,311** | |

Scene records come in two shapes: 1,255 have a zero word at byte 4 and their name at byte 8, and 1,559 have their
name at byte 4. The two global resources carry the same chunk types; entry 3,690 (`warriors.glr`, 506,736 bytes) has a
sound list chunk 0x29 of 282,288 bytes (a count of 23,523 then 12-byte records), and the unnamed entry 3,689
(883,104 bytes) has a 0x29 chunk of 658,656 bytes (the same count, 28-byte records that include part of each name).

## The chunk container

Packages and resources share one layout of 16-byte headers. Every header ends in a 32-bit zero and a 32-bit hash.

| Header | 0x00 | 0x04 | 0x08 | 0x0C |
| --- | --- | --- | --- | --- |
| Package | number of resources | bytes that follow this header | 0 | 0xDE686795, the CRC-32 of `package` |
| Resource | number of chunks | sum of its chunks' data sizes (headers not counted) | 0 | resource name hash |
| Chunk | chunk type | data size in bytes (header not counted) | 0 | chunk name hash |

A resource is its header, then each chunk's header followed by that many bytes of data. A package is its header
followed by its resources back to back. A standalone resource entry is one resource; padding to the entry's size
follows. Every one of the 4,255 standalone resources and 884 packages on the disc reads this way to its end.

The 884 packages hold 38,849 resources, 2 to 351 each. The PS2's `WARRIORS.WAD` uses the same container: its 881
packages carry the same header hash 0xDE686795. What differs is the content of the graphics chunks (below).

Chunk totals over every resource on the disc (standalone and in packages):

| Chunk type | Chunks | Bytes | In packages | Content |
| --- | ---: | ---: | ---: | --- |
| 0x00 | 30,951 | 134,689,520 | 28,693 | animation keyframes |
| 0x02 | 30,951 | 2,476,080 | 28,693 | animation descriptor |
| 0x08 | 1,199 | 3,491,488 | 1,146 | character data |
| 0x28 | 2,370 | 1,289,280 | 2,217 | skinned model bone data, 544 bytes each |
| 0x2A | 18,245 | 501,984,640 | 15,898 | one Xbox texture |
| 0x45 | 1,199 | 13,870,032 | 1,146 | character data |
| 0x47 | 21,520 | 315,719,168 | 19,758 | one Xbox model |
| 0x4C | 1,336 | 147,632 | 760 | particle page (48 bytes in 1,219 of them) |

## Resource images

The Xbox graphics data (texture chunks, model chunks, `.xsec` and `.xlev` entries) share one shape: a 128-byte
header, a block of graphics data, then Xbox Direct3D resource headers and records, filled with bytes 0xEE up to a size
that is a multiple of 128. All 18,245 texture chunks and all 1,477 model chunks have sizes that are multiples of 128.

| Header offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | image kind (table below) |
| 0x04 | 4 | a constant per kind (table below) |
| 0x08 | 4 | number of Direct3D resource headers after the data block |
| 0x0C | 4 | size of the data block, which starts at 0x80 |
| 0x10 | 112 | not decoded; holds values that look like leftover memory addresses (0x0012xxxx, 0x008Cxxxx) |

| Image | Kind (0x00) | Value at 0x04 | Value at 0x08 | Count on disc |
| --- | ---: | ---: | --- | ---: |
| Texture chunk 0x2A | 0 | 111 | 1 | 18,245 |
| Skinned model chunk 0x47 | 1 | 118 | 3 | 153 |
| Model chunk 0x47 | 2 | 109 | 2 | 1,318 |
| Multi-part model chunk 0x47 | 3 | 103 | 94 | 6 |
| `.xsec` entry | 4 | 111 | varies; 0 in the 84 empty entries | 1,969 |
| `.xlev` entry | 5 | 106 | does not match the header count | 79 |

The Direct3D headers use the Xbox resource layout: a 32-bit common word (reference count 1 in the low bits, the
resource type in bits 16-18), then a data word giving the resource's byte offset within the data block.

| Common word | Resource | Header size |
| --- | --- | --- |
| 0x00040001 | texture | 20 bytes: common, data offset, lock, format, size |
| 0x00800001 | vertex buffer | 12 bytes: common, data offset, lock |
| 0x00010001 | index buffer | 12 bytes: common, data offset, lock |

For textures, models and `.xsec` entries the count at 0x08 equals the number of these headers after the data block.
The 1,969 `.xsec` entries hold 12,372 texture headers, 122,124 vertex buffer headers and 122,124 index buffer headers;
84 of them are empty (data size 0, no headers).

## Textures (chunk 0x2A)

Every texture chunk holds exactly one texture. Layout from the chunk's data start:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 16 | image header: 0, 111, 1, pixel data size |
| 0x80 | pixel data size | DXT pixel data, largest mipmap level first |
| after the data, +0x00 | 20 | texture header: 0x00040001, 0, 0, format word, 0 |
| +0x14 | 12 | 0xEE bytes |
| +0x20 | 8 | zero |
| +0x28 | 2 | width in pixels |
| +0x2A | 2 | height in pixels |
| +0x2C | 4 | 0x0046872B, or 0x0046874B in the 1,336 textures that have a particle page |
| +0x30 | rest | 0xEE bytes up to the chunk end |

The format word:

| Bits | Meaning | Values on the disc |
| --- | --- | --- |
| 0-7 | Xbox texture flags and dimension count | 0x29 in all 18,245 |
| 8-15 | pixel format | 0x0C DXT1 (14,370), 0x0E DXT2/3 (878), 0x0F DXT4/5 (2,997) |
| 16-19 | mipmap levels | 1 to 10 |
| 20-23 | log2 of the width | 2 to 10 |
| 24-27 | log2 of the height | 2 to 10 |

In all 18,245 texture chunks the width and height fields equal the sizes in the format word, and the pixel data size
equals the size of the mipmap chain (8 bytes per 4 x 4 block for DXT1, 16 for the others, at least one block per
level). The largest texture side is 1,024. The most common sizes are 64 x 64 (3,878), 32 x 32 (3,841), 256 x 256
(3,288), 128 x 128 (2,037), 16 x 16 (1,725) and 512 x 512 (1,332).

| Mipmap levels | 1 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 3 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Textures | 1,106 | 323 | 2,024 | 4,203 | 4,226 | 2,083 | 3,249 | 1,030 | 1 |

The PS2 texture chunks hold RenderWare texture dictionaries with 4-bit and 8-bit palette images; the Xbox chunks
hold DXT images in this layout instead, so no texture chunk is byte-identical between the two discs.

## Models (chunk 0x47)

### Props and scene models (kind 2, 1,318 chunks)

The data block holds the vertex buffer at offset 0 and the index buffer right after it. After the data block:

| Offset | Size | Meaning |
| --- | --- | --- |
| +0x00 | 12 | vertex buffer header: 0x00800001, 0, 0 |
| +0x0C | 12 | index buffer header: 0x00010001, byte offset of the indices in the data block, 0 |
| +0x18 | 8 | not used (0xEE bytes in the models examined) |
| +0x20 | 4 | leftover memory address |
| +0x24 | 4 | vertex format (Direct3D FVF bits) |
| +0x28 | 1 | primitive type: 5, triangle list |
| +0x29 | 1 | vertex size in bytes |
| +0x2A | 1 | flag: 0 (719 models) or 1 (599) |
| +0x2B | 1 | 0 |
| +0x2C | 2 | number of triangles |
| +0x2E | 2 | 0x0047 |
| +0x30 | 4 | float; a size of the model (not confirmed) |
| +0x3C | 4 | 12 |

| Vertex format | Vertex layout | Size | Models |
| --- | --- | ---: | ---: |
| 0x152 | position (3 floats), normal (3 floats), colour (32-bit), one texture coordinate pair (2 floats) | 36 | 1,101 |
| 0x252 | position, normal, colour, two texture coordinate pairs | 44 | 119 |
| 0x052 | position, normal, colour | 28 | 98 |

In all 1,318 of these models:

- the vertex buffer size is the vertex count times the vertex size, rounded up to a multiple of 16;
- the index buffer holds 16-bit indices, three per triangle, and ends exactly at the data block's size;
- every index is below the vertex count.

Together they hold 214,821 vertices and 149,943 triangles.

### Skinned models (kind 1, 153 chunks)

Each skinned model resource is a 0x47 chunk and a 0x28 chunk. The 0x47 chunk's data block holds two vertex buffers
then one index buffer; after it come two vertex buffer headers and one index buffer header (the first vertex buffer
at data offset 0), then further float data not decoded here. All 2,370 chunks 0x28 on the disc (153 standalone,
2,217 in packages) are 544 bytes: 34 rows of four floats, the fourth of each row being 1.0.

### Multi-part models (kind 3, 6 chunks)

Six model chunks (147,584 to 235,520 bytes) hold 47 vertex buffers and 47 index buffers each (header count 94).

## Entry points

The addresses are in the PS2 executable `SLUS_212.15` and serve the PS2 counterpart of each job, on the PS2
archive `WARRIORS.DIR` and `WARRIORS.WAD`. No Xbox addresses are given.

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0040c5e0` | Open the archive, its directory and its data file | The resource index (`XBoxWad.idx`) |
| `0x001490b8` | Find an archive entry by name | Name hash |
| `0x00144180` | Read a standalone resource | The chunk container |
| `0x00187c38` | Load a package by its name | The chunk container |
| `0x00190770` | Read a texture chunk | Textures (chunk 0x2A) |
| `0x0017f2c0` | Read a model chunk | Models (chunk 0x47) |

## Acceptance checks

- `XBoxWad.idx` lists 8 volumes and 10,587 entries, with the volume table at byte 20 and the entry table at byte 308.
- Every entry's offset plus size lies within its volume, and every entry starts on a 2,048-byte boundary.
- The entries' sizes add up to 1,726,598,311 bytes.
- The CRC-32 of `ee_files\paks\global.pak` is 0x4CFC9C5C, the hash of entries 0 and 9,704.
- `ee_files\bar_wcm_swn_001.scn` and `ee_files\gen_piss_end.scn` both hash to 0xBCE5ADDE.
- Entry 0 is a package of 11 resources whose header hash 0xDE686795 is the CRC-32 of `package`.
- There are 884 packages holding 38,849 resources, and every package and standalone resource parses to its end.
- There are 18,245 texture chunks; each holds one DXT texture whose pixel data size equals its mipmap chain size.
- The texture chunks use DXT1 14,370 times, DXT2/3 878 times and DXT4/5 2,997 times; the largest side is 1,024.
- The 1,318 kind-2 model chunks use vertex formats 0x152 (1,101), 0x252 (119) and 0x052 (98) and hold 149,943
  triangles.
- Every 0x28 chunk of a skinned model is 544 bytes.
- The archive holds 2,814 scene records and 503 Lua scripts, and the PS2's 2,765 scene records and 467 Lua scripts are
  among them byte for byte.

## Open questions

- The meaning of the index header value 0x00100000 and of the 0xFFFFFFFF word in each volume record.
- The meaning of the image header value at 0x04 (111, 109, 118, 103, 106) and of the words between 0x10 and 0x80.
- The word 0x0046872B / 0x0046874B after the texture size fields, and the prop flag byte (0 or 1).
- The vertex layout of the two vertex buffers of skinned models, and the meaning of the 34 rows in chunk 0x28.
- The layout of the `.xlev` header, whose resource count does not match the headers found after its data block.
- The internal layout of `.xsec` entries beyond their resource headers.
- The names of 229 Lua scripts, 645 packages, 3 animations and 13 scene records; no candidate built from the disc's
  strings matched their hashes. Texture, model and character resources are not named in the index at all.
- The Xbox executable (`default.xbe`) is not available in the address study tools this session, so no Xbox addresses
  could be listed; the PS2 entry points above are counterparts only. The PS2 texture and model chunks hold RenderWare
  data, so the Xbox readers of chunks 0x2A and 0x47 are not expected to match their PS2 counterparts.
