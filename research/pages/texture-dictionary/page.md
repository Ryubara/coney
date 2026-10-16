# PS2 texture dictionaries and texture natives

## Scope and sources

This page records how the PS2 version stores its textures on the disc: the RenderWare stream section header as it
appears in this game's data, the texture dictionary (section `0x16`) and texture native (section `0x15`) layouts,
the PS2 raster header and transfer packets, the GS memory layouts needed to turn the stored texel data into an
image, and the palette order. Models, worlds and the WAD directory itself are covered on other pages.

Source files, all on the PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso":

| File | Size (bytes) | Role here |
| --- | ---: | --- |
| `WARRIORS.DIR` | 128,428 | Directory of `WARRIORS.WAD`: a 16-byte header whose first word is the entry count (10,701), then one 12-byte record per entry: offset, size, name hash |
| `WARRIORS.WAD` | 1,495,371,776 | The archive; every texture on the disc is inside it |
| `SLUS_212.15` | 4,903,976 | The executable; source of the code addresses below |

"WAD entry N" below means the entry described by the N-th directory record (counting from 0). All values are
little-endian. Counts count every copy: the same texture can appear in several packs and streams.

## Where the dictionaries are

Every texture on the disc is a texture native section inside a texture dictionary section. The dictionaries sit in
five kinds of WAD entry:

| Container | Where the dictionary starts | Dictionaries | Textures |
| --- | --- | ---: | ---: |
| Resource inside a pack (881 packs; 867 of them hold dictionaries) | payload of a chunk of type `0x2a` | 15,903 | 15,903 |
| Standalone resource | payload of a chunk of type `0x2a` | 2,085 | 2,085 |
| Level resource (64, four dictionaries each) | payload of a chunk of type `0x2a` | 256 | 258 |
| World stream (159 entries) | byte 4, after a leading u32 count | 159 | 11,664 |
| Sector file (1,911 entries) | byte 16 | 1,911 (552 empty) | 12,301 |
| Older streams (WAD entries 3,793 to 3,797) | byte 0 (entries 3,793 to 3,796) or byte 4 (entry 3,797) | 5 (4 empty) | 94 |
| **Total** | | **20,319** (556 empty) | **42,305** |

- Each dictionary in a pack, standalone resource or level resource holds one texture, except two level
  dictionaries that hold two; world stream and sector file dictionaries hold up to 251 textures.
- A chunk header is 16 bytes: u32 chunk type, u32 payload size, u32 zero, u32 name hash. In every `0x2a` chunk the
  payload is the dictionary section padded to a multiple of 16 bytes (padding of 0 bytes in 9,831 chunks, 4 in
  2,100, 8 in 1,298, 12 in 5,015).
- The 42,305 textures carry 14,602 distinct names.

## Section header

Every RenderWare section starts with a 12-byte header; a section's size counts its children.

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| `+0x00` | 4 | u32 | Section type |
| `+0x04` | 4 | u32 | Size of the section body in bytes (everything after this 12-byte header) |
| `+0x08` | 4 | u32 | Library stamp (version and build) |

Section types in a texture dictionary:

| Type | Section |
| --- | --- |
| `0x01` | Struct (plain data) |
| `0x02` | String |
| `0x03` | Extension (plugin data) |
| `0x15` | Texture native |
| `0x16` | Texture dictionary |
| `0x110` | Plugin inside a texture native's extension: the mipmap K value |

Library stamps seen on the disc. Every section inside a dictionary carries the same stamp as the dictionary.

| Stamp | Version | Build | Dictionaries | Textures |
| --- | --- | --- | ---: | ---: |
| `0x1C02000A` | 3.7.0.2 | `0x000A` | 20,314 | 42,211 |
| `0x1803FFFF` | 3.6.0.3 | `0xFFFF` | 5 | 94 |

The stamp's bit fields:

| Bits | Meaning | In `0x1C02000A` | In `0x1803FFFF` |
| --- | --- | --- | --- |
| 0-15 | Build | `0x000A` | `0xFFFF` |
| 16-21 | Fourth version field | 2 | 3 |
| 22-25 | Third version field | 0 | 0 |
| 26-29 | Second version field | 7 | 6 |
| 30-31 | First version field minus 3 | 0 | 0 |

## Texture dictionary (section `0x16`)

| Part | Size | Content |
| --- | ---: | --- |
| Section header, type `0x16` | 12 | |
| Struct header, type `0x01`, size 4 | 12 | |
| Texture count | 2 | u16, 0 to 251 on the disc |
| Device id | 2 | u16, always 6 (PS2) |
| Texture natives | varies | Texture count sections of type `0x15`, back to back |
| Extension header, type `0x03` | 12 | Size 0 on every dictionary on the disc |

Texture counts per dictionary: 1 in 18,381 dictionaries, 0 in 556, 2 to 251 in the other 1,382.

## Texture native (section `0x15`)

| Part | Size | Content |
| --- | ---: | --- |
| Section header, type `0x15` | 12 | |
| Struct, type `0x01`, size 8 | 12 + 8 | u32 platform `0x00325350` (the bytes `PS2` and a zero), then u32 filter and addressing word |
| String, type `0x02` | 12 + n | Texture name, zero-terminated, padded with zeros to a multiple of 4 (n is 4 to 28 on the disc) |
| String, type `0x02` | 12 + 4 | Mask name: empty (four zero bytes) on every texture on the disc |
| Struct, type `0x01` | 12 + ... | The raster: a struct of size 64 (the raster header, below) followed by a struct holding the texel data then the palette data |
| Extension, type `0x03` | 12 + 16 | Exactly one plugin section of type `0x110` holding a u32 equal to the raster header's K field |

The filter and addressing word:

| Bits | Meaning | Values on the disc |
| --- | --- | --- |
| 0-7 | Filter mode | 2 = linear (18,246 textures); 6 = linear, blended linearly between mipmap levels (24,059) |
| 8-11 | U addressing | 1 = wrap (42,274); 3 = clamp (31) |
| 12-15 | V addressing | Always equal to U on the disc |
| 16-31 | Unused | Always 0 |

Whole words seen: `0x1102` (18,246), `0x1106` (24,028), `0x3306` (31). Filter mode 6 is also used on 23,318
textures without mipmaps. The 31 clamped textures are decals: graffiti tags (`train_tag3_000` to `train_tag6_000`,
`tag_murder_lic_000`, ...), wall murals (`wallmural004_000`, ...), ground marks and wreck decals.

## Raster header

The first struct inside the raster, 64 bytes:

| Offset | Size | Type | Meaning | Values on the disc |
| --- | ---: | --- | --- | --- |
| `+0x00` | 4 | u32 | Width in texels | Powers of two, 2 to 512 |
| `+0x04` | 4 | u32 | Height in texels | Powers of two, 2 to 512 |
| `+0x08` | 4 | u32 | Depth: bits per texel | 8 (32,551) or 4 (9,754) |
| `+0x0c` | 2 | u16 | Raster format flags (next table) | `0x2504`, `0x4504`, `0xA504`, `0xC504` |
| `+0x0e` | 2 | u16 | Data layout version | Always 2: the texel and palette data are GS transfer packets |
| `+0x10` | 8 | u64 | GS `TEX0` register value (fields below) | |
| `+0x18` | 4 | u32 | Palette position in GS memory, in 256-byte blocks from the texture's base | 28, 31, 32, 60, 63, 64, 92, 124, 128, 256, 512, 1,024 |
| `+0x1c` | 4 | u32 | Low word of the GS `TEX1` register; only bits 2-4 (number of mipmap levels after the first) are ever set | 0, 4, 8, 12, 16 |
| `+0x20` | 8 | u64 | GS `MIPTBP1`: base block and buffer width of mipmap levels 1 to 3 | |
| `+0x28` | 8 | u64 | GS `MIPTBP2`: base block and buffer width of mipmap levels 4 to 6 | |
| `+0x30` | 4 | u32 | Bytes of texel packet data | |
| `+0x34` | 4 | u32 | Bytes of palette packet data | 1,104 (8-bit) or 176 (4-bit) |
| `+0x38` | 4 | u32 | GS memory the texture and its palette occupy, in 32-bit words; always a multiple of 2,048 (one 8 KB page) | `0x800` to `0x10800` |
| `+0x3c` | 4 | u32 | Mipmap K value: 12-bit signed, 4 fraction bits | `0xF2D` (-13.1875) to `0xFF7` (-0.5625); `0xFC0` (-4.0) on 19,048 |

The texel data struct's size is exactly `+0x30` plus `+0x34` on every texture.

Raster format flags:

| Flag | Meaning |
| --- | --- |
| `0x0004` | Texture raster (set on every texture) |
| `0x0500` | Palette colours are 32-bit (8 bits each of red, green, blue, alpha) |
| `0x2000` | 8-bit palette indices (256 colours) |
| `0x4000` | 4-bit palette indices (16 colours) |
| `0x8000` | Has mipmap levels |

Format and depth combinations on the disc (no true-colour texture exists):

| Raster format | Depth | `TEX0` pixel format | Mipmaps | Textures |
| --- | ---: | --- | --- | ---: |
| `0x2504` | 8 | `0x13` (PSMT8) | No | 32,342 |
| `0x4504` | 4 | `0x14` (PSMT4) | No | 9,222 |
| `0xC504` | 4 | `0x14` (PSMT4) | Yes | 532 |
| `0xA504` | 8 | `0x13` (PSMT8) | Yes | 209 |

Most common sizes: 64 x 64 (15,832), 32 x 32 (12,333), 128 x 128 (4,961), 16 x 16 (3,289), 256 x 256 (1,949),
8 x 8 (705), 64 x 32 (423), 128 x 64 (421), 512 x 512 (382). Non-square sizes range from 16 : 1 (512 x 32) to
1 : 8 (16 x 128).

Mipmapped textures always stop at an 8 x 8 level:

| Size | Levels after the first (`TEX1` bits 2-4) | Textures |
| --- | ---: | ---: |
| 16 x 16 | 1 | 2 |
| 32 x 32 | 2 | 223 |
| 64 x 64 | 3 | 500 |
| 128 x 128 | 4 | 16 |

### `TEX0` fields

| Bits | Field | Values on the disc |
| --- | --- | --- |
| 0-13 | `TBP0` texture base block | Always 0 (the base is assigned when the texture is placed in GS memory) |
| 14-19 | `TBW` buffer width, in units of 64 texels | 2 for widths up to 128, 4 for 256, 8 for 512 |
| 20-25 | `PSM` pixel format | `0x13` PSMT8 or `0x14` PSMT4 |
| 26-29 | `TW` log2 of width | Matches `+0x00` on every texture |
| 30-33 | `TH` log2 of height | Matches `+0x04` on every texture |
| 34 | `TCC` | Always 1 (alpha taken from the texture) |
| 35-36 | `TFX` | Always 0 (modulate) |
| 37-50 | `CBP` palette base block | Always 0 |
| 51-54 | `CPSM` palette pixel format | Always 0 (PSMCT32) |
| 55 | `CSM` palette storage mode | Always 0 (CSM1) |
| 56-60 | `CSA` palette offset | Always 0 |
| 61-63 | `CLD` palette load control | Always 1 |

### GS memory placement values

These values describe where the texture's levels and palette land relative to its base block; they are not needed to
decode an image. Examples from the disc:

| Texture | Size, depth, levels | Palette block (`+0x18`) | GS words (`+0x38`) | `MIPTBP1` bases (width) | `MIPTBP2` bases (width) |
| --- | --- | ---: | ---: | --- | --- |
| Any non-mipmapped texture up to one page | e.g. 64 x 64, 8-bit, 1 | 28 | `0x800` | 0, 0, 0 (1) | 0, 0, 0 (1) |
| `civl_hl_br5` | 128 x 128, 8-bit, 1 | 64 | `0x1800` | 0, 0, 0 (1) | 0, 0, 0 (1) |
| `level51_ls_2_w` | 512 x 512, 8-bit, 1 | 1,024 | `0x10800` | 0, 0, 0 (1) | 0, 0, 0 (1) |
| `brick_000` | 128 x 128, 4-bit, 5 | 63 | `0x1000` | 32, 40, 42 (2) | 43 (2), 0, 0 (1) |
| `fenccemeb_000` | 128 x 128, 8-bit, 5 | 92 | `0x1800` | 64, 80, 84 (2) | 85 (2), 0, 0 (1) |
| `fenc092_000` | 64 x 64, 4-bit, 4 | 31 | `0x800` | 8, 10, 11 (2) | 0, 0, 0 (1) |

On all 41,564 textures without mipmaps both `MIPTBP` words are `0x0040000400004000` (every base 0, every width 1).

## Transfer packets

The texel data is one packet group per mipmap level, largest level first; the palette data is one more group. Each
group is a GS transfer in GIF format:

| Offset | Size | Content |
| --- | ---: | --- |
| `+0x00` | 16 | GIF tag: 3 loops, packed mode, one register descriptor `0x0E` (address plus data) |
| `+0x10` | 16 | Data for register `0x51` (`TRXPOS`): source position 0, 0; destination x, y of the rectangle |
| `+0x20` | 16 | Data for register `0x52` (`TRXREG`): rectangle width and height |
| `+0x30` | 16 | Data for register `0x53` (`TRXDIR`): 0 (host to GS memory) |
| `+0x40` | 16 | GIF tag: image mode; loop count = rectangle bytes / 16 |
| `+0x50` | rectangle bytes | The rectangle's pixels, row by row |

- No group writes register `0x50` (`BITBLTBUF`); the destination base, buffer width and pixel format of the
  transfer are not stored.
- The rectangle byte count equals width x height x 4 for 8-bit textures and width x height x 2 for 4-bit textures on
  all 44,312 texel groups, and the texel data size `+0x30` is the sum of the groups' sizes, except on 5 textures of
  2 x 2 texels (`menu_black_000` twice, `black_4subtnl_022`, `63_blackubtnl_000`, `63_black_btnl_000`), whose texel
  data has 448 further bytes after the single group.
- `TRXPOS` destinations: level 0 is always at 0, 0; smaller levels sit beside or below it, for example (32, 0),
  (48, 0), (48, 8) for the three smaller levels of a 64 x 64 4-bit texture.

### Texel rectangles

The texel rectangle of a level is the level's texels stored in a wider pixel format: 8-bit levels as PSMCT32 pixels
(four texels in each 32-bit pixel), 4-bit levels as PSMCT16 pixels (four texels in each 16-bit pixel). Its size:

| Depth | Level size | Rectangle (`TRXREG`) | Examples on the disc |
| ---: | --- | --- | --- |
| 8 | W x H, W at least 16 | W/2 x H/2 PSMCT32 pixels | 128 x 128 is 64 x 64; 64 x 8 is 32 x 4; 16 x 8 is 8 x 4 |
| 8 | 8 x H | 8 x H/2 PSMCT32 pixels | 8 x 8 is 8 x 4; 8 x 32 is 8 x 16 |
| 4 | W x H, W at least 32 and H at least 4 | W/2 x H/2 PSMCT16 pixels | 512 x 512 is 256 x 256; 32 x 32 is 16 x 16 |
| 4 | W below 32 | 16 x H/2 PSMCT16 pixels, H counted as at least 4 | 16 x 16 is 16 x 8; 8 x 8 is 16 x 4; 4 x 4 and 2 x 2 are 16 x 2 |

The rectangle bytes are therefore not in row order of the texture. They are the GS memory image of the texture: the
rectangle's pixels at their PSMCT32 (or PSMCT16) addresses hold, byte for byte, the texture's texels at their PSMT8
(or PSMT4) addresses. Both views share one page grid: the rectangle's width in 64-pixel pages equals the
texture's width in 128-texel pages, with pages in rows from the top left. Read as plain rows, the bytes give a
scrambled image.

### GS memory layouts

GS memory is made of 8,192-byte pages, each of 32 blocks of 256 bytes; each block is four 64-byte columns, numbered
0 to 3 from the top of the block down.

| Format | Page (texels) | Block (texels) | Blocks across x down | Column (texels) |
| --- | --- | --- | --- | --- |
| PSMCT32 | 64 x 32 | 8 x 8 | 8 x 4 | 8 x 2 |
| PSMCT16 | 64 x 64 | 16 x 8 | 4 x 8 | 16 x 2 |
| PSMT8 | 128 x 64 | 16 x 16 | 8 x 4 | 16 x 4 |
| PSMT4 | 128 x 128 | 32 x 16 | 4 x 8 | 32 x 4 |

Block numbers by position in the page, for PSMCT32 and PSMT8 (8 blocks across, 4 down):

| Row | Col 0 | Col 1 | Col 2 | Col 3 | Col 4 | Col 5 | Col 6 | Col 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 1 | 4 | 5 | 16 | 17 | 20 | 21 |
| 1 | 2 | 3 | 6 | 7 | 18 | 19 | 22 | 23 |
| 2 | 8 | 9 | 12 | 13 | 24 | 25 | 28 | 29 |
| 3 | 10 | 11 | 14 | 15 | 26 | 27 | 30 | 31 |

Block numbers for PSMCT16 and PSMT4 (4 blocks across, 8 down):

| Row | Col 0 | Col 1 | Col 2 | Col 3 |
| --- | ---: | ---: | ---: | ---: |
| 0 | 0 | 2 | 8 | 10 |
| 1 | 1 | 3 | 9 | 11 |
| 2 | 4 | 6 | 12 | 14 |
| 3 | 5 | 7 | 13 | 15 |
| 4 | 16 | 18 | 24 | 26 |
| 5 | 17 | 19 | 25 | 27 |
| 6 | 20 | 22 | 28 | 30 |
| 7 | 21 | 23 | 29 | 31 |

Within a column, PSMCT32 pixels (8 x 2) occupy these 32-bit words of the column:

| Row | x0 | x1 | x2 | x3 | x4 | x5 | x6 | x7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 1 | 4 | 5 | 8 | 9 | 12 | 13 |
| 1 | 2 | 3 | 6 | 7 | 10 | 11 | 14 | 15 |

PSMCT16 pixels (16 x 2) occupy these 16-bit halves of the column (half h is bytes 2h and 2h + 1):

| Row | x0 | x1 | x2 | x3 | x4 | x5 | x6 | x7 | x8 | x9 | x10 | x11 | x12 | x13 | x14 | x15 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 2 | 8 | 10 | 16 | 18 | 24 | 26 | 1 | 3 | 9 | 11 | 17 | 19 | 25 | 27 |
| 1 | 4 | 6 | 12 | 14 | 20 | 22 | 28 | 30 | 5 | 7 | 13 | 15 | 21 | 23 | 29 | 31 |

PSMT8 texels (16 x 4) occupy these bytes of the column; columns 0 and 2 of a block use the first table, columns 1 and
3 the second:

| Columns 0, 2 | x0 | x1 | x2 | x3 | x4 | x5 | x6 | x7 | x8 | x9 | x10 | x11 | x12 | x13 | x14 | x15 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Row 0 | 0 | 4 | 16 | 20 | 32 | 36 | 48 | 52 | 2 | 6 | 18 | 22 | 34 | 38 | 50 | 54 |
| Row 1 | 8 | 12 | 24 | 28 | 40 | 44 | 56 | 60 | 10 | 14 | 26 | 30 | 42 | 46 | 58 | 62 |
| Row 2 | 33 | 37 | 49 | 53 | 1 | 5 | 17 | 21 | 35 | 39 | 51 | 55 | 3 | 7 | 19 | 23 |
| Row 3 | 41 | 45 | 57 | 61 | 9 | 13 | 25 | 29 | 43 | 47 | 59 | 63 | 11 | 15 | 27 | 31 |

| Columns 1, 3 | x0 | x1 | x2 | x3 | x4 | x5 | x6 | x7 | x8 | x9 | x10 | x11 | x12 | x13 | x14 | x15 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Row 0 | 32 | 36 | 48 | 52 | 0 | 4 | 16 | 20 | 34 | 38 | 50 | 54 | 2 | 6 | 18 | 22 |
| Row 1 | 40 | 44 | 56 | 60 | 8 | 12 | 24 | 28 | 42 | 46 | 58 | 62 | 10 | 14 | 26 | 30 |
| Row 2 | 1 | 5 | 17 | 21 | 33 | 37 | 49 | 53 | 3 | 7 | 19 | 23 | 35 | 39 | 51 | 55 |
| Row 3 | 9 | 13 | 25 | 29 | 41 | 45 | 57 | 61 | 11 | 15 | 27 | 31 | 43 | 47 | 59 | 63 |

PSMT4 texels (32 x 4) occupy these 4-bit halves of the column (half n is in byte n/2 rounded down, the low four
bits when n is even); columns 0 and 2 of a block use the first table, columns 1 and 3 the second:

| Columns 0, 2 | x0 | x1 | x2 | x3 | x4 | x5 | x6 | x7 | x8 | x9 | x10 | x11 | x12 | x13 | x14 | x15 | x16 | x17 | x18 | x19 | x20 | x21 | x22 | x23 | x24 | x25 | x26 | x27 | x28 | x29 | x30 | x31 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Row 0 | 0 | 8 | 32 | 40 | 64 | 72 | 96 | 104 | 2 | 10 | 34 | 42 | 66 | 74 | 98 | 106 | 4 | 12 | 36 | 44 | 68 | 76 | 100 | 108 | 6 | 14 | 38 | 46 | 70 | 78 | 102 | 110 |
| Row 1 | 16 | 24 | 48 | 56 | 80 | 88 | 112 | 120 | 18 | 26 | 50 | 58 | 82 | 90 | 114 | 122 | 20 | 28 | 52 | 60 | 84 | 92 | 116 | 124 | 22 | 30 | 54 | 62 | 86 | 94 | 118 | 126 |
| Row 2 | 65 | 73 | 97 | 105 | 1 | 9 | 33 | 41 | 67 | 75 | 99 | 107 | 3 | 11 | 35 | 43 | 69 | 77 | 101 | 109 | 5 | 13 | 37 | 45 | 71 | 79 | 103 | 111 | 7 | 15 | 39 | 47 |
| Row 3 | 81 | 89 | 113 | 121 | 17 | 25 | 49 | 57 | 83 | 91 | 115 | 123 | 19 | 27 | 51 | 59 | 85 | 93 | 117 | 125 | 21 | 29 | 53 | 61 | 87 | 95 | 119 | 127 | 23 | 31 | 55 | 63 |

| Columns 1, 3 | x0 | x1 | x2 | x3 | x4 | x5 | x6 | x7 | x8 | x9 | x10 | x11 | x12 | x13 | x14 | x15 | x16 | x17 | x18 | x19 | x20 | x21 | x22 | x23 | x24 | x25 | x26 | x27 | x28 | x29 | x30 | x31 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Row 0 | 64 | 72 | 96 | 104 | 0 | 8 | 32 | 40 | 66 | 74 | 98 | 106 | 2 | 10 | 34 | 42 | 68 | 76 | 100 | 108 | 4 | 12 | 36 | 44 | 70 | 78 | 102 | 110 | 6 | 14 | 38 | 46 |
| Row 1 | 80 | 88 | 112 | 120 | 16 | 24 | 48 | 56 | 82 | 90 | 114 | 122 | 18 | 26 | 50 | 58 | 84 | 92 | 116 | 124 | 20 | 28 | 52 | 60 | 86 | 94 | 118 | 126 | 22 | 30 | 54 | 62 |
| Row 2 | 1 | 9 | 33 | 41 | 65 | 73 | 97 | 105 | 3 | 11 | 35 | 43 | 67 | 75 | 99 | 107 | 5 | 13 | 37 | 45 | 69 | 77 | 101 | 109 | 7 | 15 | 39 | 47 | 71 | 79 | 103 | 111 |
| Row 3 | 17 | 25 | 49 | 57 | 81 | 89 | 113 | 121 | 19 | 27 | 51 | 59 | 83 | 91 | 115 | 123 | 21 | 29 | 53 | 61 | 85 | 93 | 117 | 125 | 23 | 31 | 55 | 63 | 87 | 95 | 119 | 127 |

Each level's rectangle is complete in itself: placed at position 0, 0 of its own page grid, it gives that level's
image, so the `TRXPOS` destinations and `MIPTBP` values are not needed to decode a level. On the disc each smaller
level matches the level above it scaled down by two.

### Palettes

| Depth | Rectangle (`TRXREG`) | Colours used | Bytes after the packet header |
| ---: | --- | ---: | ---: |
| 8 | 16 x 16 PSMCT32 | 256 | 1,024 |
| 4 | 8 x 3 PSMCT32 | 16 (the first two rows) | 96 |

- Each colour is four bytes: red, green, blue, alpha.
- Alpha runs from 0 (fully transparent) to 128 (`0x80`, fully opaque); no palette colour on the disc has an alpha
  above 128. All 129 values occur. 23,887 textures use only alpha 128.
- 8-bit palettes are in CSM1 order: texel value i takes the colour stored at position i with bits 3 and 4 of i
  swapped. Within every run of 32 values:

| Texel values | Stored positions |
| --- | --- |
| 0 to 7 | 0 to 7 |
| 8 to 15 | 16 to 23 |
| 16 to 23 | 8 to 15 |
| 24 to 31 | 24 to 31 |

- 4-bit palettes are in plain order: texel value i takes stored colour i. The third row (positions 16 to 23) is not
  used by any texel; it holds zeros on 329 textures and unrelated bytes on the rest.

## The older streams

WAD entries 3,793 to 3,797 carry the 3.6.0.3 stamp. Entries 3,793 to 3,796 begin with an empty dictionary. Entry
3,797 holds a count, a dictionary of 94 textures and more data after it. Its textures use the same layout as the
rest: layout version 2, `PS2` platform, filter word `0x1106`, PAL4 (85) or PAL8 (9) with 32-bit palettes, no
mipmaps, sizes 16 x 16 to 128 x 128.

## Xbox version

A byte scan of the whole Xbox disc image finds no texture dictionary section (a `0x16` header followed by its 4-byte
struct) stamped with a 3.x library version, and no `WARRIORS.WAD` name. The Xbox textures are stored in some other
form, not described here; everything above is PS2 only.

## Chunk types read

| Chunk type | What the game reads from it | Chunks of this type in PS2 resources |
| --- | --- | ---: |
| `0x2a` | One texture dictionary (section `0x16`) from the payload | 18,244 |

- The 18,244 chunks of type `0x2a` are the packed, standalone and level resource dictionaries counted above (15,903
  plus 2,085 plus 256).
- The dictionaries in world streams, sector files and the older streams are not resource chunks; they are read as
  part of their own files.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00190770` | Read a texture dictionary held in a resource chunk | Chunk types read |

## Acceptance checks

- The PS2 `WARRIORS.WAD` holds 20,319 texture dictionary sections, 20,314 stamped `0x1C02000A` and 5 stamped
  `0x1803FFFF`; 556 of them are empty.
- Those dictionaries hold 42,305 texture native sections with 14,602 distinct names.
- Every dictionary's device id is 6 and every texture native's platform word is `0x00325350`.
- Every raster header has layout version 2 and one of the raster formats `0x2504` (32,342 textures), `0x4504`
  (9,222), `0xC504` (532) or `0xA504` (209).
- Every texture native's extension holds exactly one `0x110` plugin, equal to the raster header's word at `+0x3c`.
- Every mask string on the disc is empty.
- No palette colour on the disc has an alpha above 128.
- Every texel group's rectangle bytes equal its `TRXREG` width x height x 4 (8-bit) or x 2 (4-bit).
- The texture `civl_hl_br5` (the only texture in WAD entry 0) decodes to a 128 x 128, fully opaque image: a
  character's skin sheet with a front view of a man's face (dark hair, moustache and goatee) in the top right, a pair
  of dark grey trousers down the left side and a round patch with yellow lettering in the lower middle.
- The same texture read as plain rows of bytes shows scrambled noise, and read with its 256 colours in plain order
  (no bit 3 and 4 swap) shows the face in wrong, dark brown and grey colours.
- The texture `dyn_intro_boppers` (WAD entry 4,180, 4-bit) decodes to a 512 x 512 image: the word BOPPERS in purple
  and gold block letters between two stars across the middle, over a group of men in hats in a street, all in
  purple, gold and black.
- The texture `level51_ls_2_w` (WAD entry 1, 8-bit) decodes to a 512 x 512 image whose top 384 rows show an orange
  and black picture of a railway boxcar with THE WARRIORS logo in red and the captions "14 Gunhill" and "Desperate
  Dudes"; its bottom 128 rows are fully transparent.
- The texture `fenccemeb_000` (WAD entry 6,967) decodes to a 128 x 128 image of a wrought-iron fence: vertical bars,
  a band of rings along the top and two crossing arches, with fully transparent gaps between the bars; its four
  smaller levels (64, 32, 16 and 8 texels square) show the same fence.
- The texture `brick_000` (WAD entry 6,928, 4-bit) decodes to a 128 x 128 grey-brown brick wall with four smaller
  levels down to 8 x 8.
- Exactly 31 textures use clamp addressing (filter and addressing word `0x3306`).

## Open questions

- How the Xbox version stores its textures: no RenderWare texture dictionary was found in the Xbox image.
- What the third row of a 4-bit palette (positions 16 to 23) is for, if anything.
- What the 448 bytes after the texel group of the five 2 x 2 textures are.
- The rectangle size an 8-bit level shorter than 8 texels would use: none occurs on the disc.
- How the mipmap K values (139 distinct values from -13.1875 to -0.5625) affect the picture in game was not
  observed.
