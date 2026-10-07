# RenderWare streams (textures and models)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the NTSC-U disc's
`WARRIORS.WAD`. The disc checks (2026-10-07) were made with `coney-tools extract` and report counts only.

## Purpose

The game's textures, models and world geometry are RenderWare 3.7 binary streams inside the WAD: texture
dictionaries in chunk `0x2A`, clumps (models) in chunk `0x47`, the level's light-glow world in chunk `0x15`, and the
streamed world's files ([The streamed world](../world.md)). The section layout is RenderWare's own and public; this
page records what the PS2 platform data looks like on this disc, which is what an extractor has to decode, and the
game's few departures. Where the chunks sit is on [WAD contents](wad-contents.md); how the game loads them is on
[Graphics](../graphics.md#loading-textures) and [Chunk system](../chunk-system.md#chunk-type-table).

## Original structure

RenderWare's PS2 driver is linked into the executable; its readers are reached through the stream reader the game
wraps around its own streams (`0x00197df0`, [Graphics](../graphics.md#loading-textures)).

| Address | Name (ours) | Role | Evidence |
| --- | --- | --- | --- |
| `0x004954f0` | `ReadPs2NativeTexture` | reads a texture native section (`0x15`): platform word, sampling word, name, mask, raster | confirmed (code) |
| `0x004950d8` | `ReadPs2NativeRaster` | reads the 64-byte raster header, then the texels and the palette | confirmed (code) |
| `0x00190770` | chunk `0x2A` reader | finds the dictionary section in the chunk and reads it ([Graphics](../graphics.md#loading-textures)) | confirmed (code) |
| `0x0017f2c0` | DFF reader | finds the clump section (`0x10`) in chunks `0x09`, `0x25`, `0x47` and reads it | confirmed (code) |

## Data

All values little-endian.

### Sections

Every section is `{u32 type, u32 size, u32 libraryStamp}` and then `size` bytes. A container section's data is its
children back to back; a struct section (`0x01`) holds plain fields; a string section (`0x02`) holds text padded with
NULs to a multiple of 4. Readers accept a version from `0x35000` to `0x37002` (`aiStack_9c[0] - 0x35000 < 0x2003` at
`0x004954f0`, confirmed (code)).

Stamps on the disc: `0x1C02000A` (3.7.0.2, build `0x000a`) everywhere except five files stamped `0x1803FFFF`
(3.6.0.3), below. **Disc check:** every RenderWare stream in the WAD walks to its end with these rules.

### Texture dictionary (section `0x16`) {#texture-dictionary}

```text
0x16 TexDictionary
    0x01 struct       u16 textureCount, u16 deviceId
    0x15 TextureNative  x textureCount
    0x03 extension    (empty)
```

### Texture native (section `0x15`) {#texture-native}

```text
0x15 TextureNative
    0x01 struct       char platform[4] = "PS2\0", u32 filterAddressing
    0x02 string       name
    0x02 string       mask name (alpha mask; empty on this disc)
    0x01 struct       the raster: two struct sections, below
        0x01 struct   the 64-byte raster header
        0x01 struct   pixelSize bytes of texels, then paletteSize bytes of palette
    0x03 extension    0x110 (PS2 sky mipmap value, 4 bytes) on most textures
```

`filterAddressing`: bits 0-7 the filter mode (2 linear, 6 linear-mip-linear on this disc), bits 8-11 addressing in
u, bits 12-15 in v (1 wrap, 3 clamp); a v of 0 takes u's value (confirmed (code) at `0x004954f0`). Platform other
than `PS2\0`: the reader fails.

### PS2 raster {#ps2-raster}

The 64-byte header, confirmed (code) at `0x004950d8` for the reads and offsets:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | width (a power of two, 4 to 512 on the disc) |
| `+0x04` | u32 | height |
| `+0x08` | u32 | depth: 4 or 8 bits a texel on the disc |
| `+0x0c` | u16 | raster format: `0x0500` 32-bit palette colours, `0x2000` 8-bit palette (`PAL8`), `0x4000` 4-bit palette (`PAL4`), `0x8000` mipmaps, `0x0004` texture |
| `+0x0e` | u16 | layout version: below 2 is one plain block of texels then palette; 2 is GS upload packets (every texture on the disc) |
| `+0x10` | u64 | the GS `TEX0` register, with the buffer addresses cleared |
| `+0x18` | u32 | palette offset in GS memory |
| `+0x1c` | u32 | low word of `TEX1`; bits 2-4 (`MXL`) are the number of mipmap levels after the first |
| `+0x20`, `+0x28` | u64 | `MIPTBP1`, `MIPTBP2` |
| `+0x30` | u32 | `pixelSize`: bytes of texel data that follow |
| `+0x34` | u32 | `paletteSize`: bytes of palette data after the texels |
| `+0x38` | u32 | total GS memory the raster needs |
| `+0x3c` | u32 | the mipmap `K` value |

**Disc check:** 42,211 textures (counting each copy in each pack): every one is version 2, raster format `0x2504`
(32,333), `0x4504` (9,137), `0xC504` (532) or `0xA504` (209), i.e. `PAL8` or `PAL4` with a 32-bit palette, with or
without mipmaps; `TEX0`'s pixel format is `PSMT8` (19) or `PSMT4` (20), its palette format `PSMCT32` and its palette
storage mode `CSM1`. There are no true-colour textures.

#### Texels: GS upload packets {#gs-packets}

With version 2, the texel data is one **GS upload packet** per mipmap level, back to back, and the palette data is
one more. A packet is what the PS2 sends to the GS to copy an image into its memory (public GS knowledge):

| Offset | Size | Contents |
| --- | ---: | --- |
| `+0x00` | 16 | GIF tag: `NLOOP` 3, `NREG` 1, register list `A+D` |
| `+0x10` | 16 | `TRXPOS` (`A+D` register `0x51`): destination x and y in GS memory |
| `+0x20` | 16 | `TRXREG` (`0x52`): the image's width (`+0x20`, u32) and height (`+0x24`, u32) **as sent** |
| `+0x30` | 16 | `TRXDIR` (`0x53`): 0, host to GS |
| `+0x40` | 16 | GIF tag, image mode: `NLOOP` (low 15 bits) quadwords of image data follow |
| `+0x50` | `NLOOP` × 16 | the image |

**Swizzled transfers.** Every 8-bit level on the disc is sent as 32-bit pixels at **half its width and half its
height**, and every 4-bit level as 16-bit pixels at half its width and height (`TRXREG` holds the halves). The bytes
are therefore in the order the GS stores a 32-bit (16-bit) image, and the GS reads them back as 8-bit (4-bit)
texels. Levels smaller than 16 × 4 (8-bit) or 32 × 4 (4-bit) are padded to that size before being halved; the
texture is then the top-left corner. **Disc check:** all 42,211 textures are sent this way (no plain transfers);
every one decodes into a recognisable image with the mapping below.

**Undoing the order.** Take the level as `W × H` texels (the halved `TRXREG` size doubled), and its bytes (or, for
4-bit, its nibbles, low nibble first) as an array `src`. Rows are handled in bands of four. With `log2W` = log₂ `W`,
the texel at (`x`, `y`) is `src[(y & ~3) × W + s]`, where `s` is:

```text
y1 = (y >> 1) & 1;  y2 = (y >> 2) & 1
x' = x ^ ((y1 ^ y2) << 2)                 flip bit 2 of x on rows 2, 3, 4, 5 of each 8
nx = (x' & 7) | ((x' >> 1) & ~7)
s  = y1 | (((x' >> 3) & 1) << 1) | (nx << 2) | ((y & 1) << (log2W + 1))
s  = s & ((1 << (log2W + 2)) - 1)           stay inside the band of four rows
```

This is the GS's column layout of `PSMT8` against `PSMCT32` (and `PSMT4` against `PSMCT16`) folded into one bit
shuffle; the same formula is used by the public RenderWare and GTA tools (inferred: it reproduces the original's
images on all 42,211 textures).

#### Palette {#palette}

The palette packet's image is the colours as RGBA bytes: 16 × 16 for an 8-bit texture (256 colours), 8 × 3 for a
4-bit one (the first 16 are used). Two PS2 rules apply:

- **CSM1 order** (8-bit only): within each group of 32 entries the second and third runs of eight are swapped, so the
  colour of index `i` is stored at `i ^ 0x18` when `i & 0x18` is `0x08` or `0x10`.
- **Alpha** runs 0-128, 128 opaque; scaled to 0-255 as `min(a × 255 / 128, 255)` ([Graphics](../graphics.md#texture-formats)).

**Disc check:** every 8-bit palette is 16 × 16 (32,542 textures, `paletteSize` 1,104 = `0x50` + 1,024), every 4-bit
one 8 × 3 (9,669, 176 = `0x50` + 96).

### The older streams (stamp `0x1803FFFF`) {#older-streams}

Five WAD entries (unnamed, entries 3,793-3,797) carry the 3.6.0.3 stamp. Four start with an empty texture dictionary
and continue with data that is not a section stream (a section of type `0x07` and size 0, then words that do not
parse); one (3,797) is laid out like a [world stream](../world.md#world-stream): a count, a texture dictionary and a
world section. No code names them (inferred: leftovers of an earlier build); `coney-tools extract` keeps them raw.

## Behaviour

The game reads these streams only through RenderWare; what it does with the results is on the subsystem pages
([Graphics](../graphics.md), [The streamed world](../world.md), [Characters](../characters.md#files)).

## Coney's implementation

The engine reads textures and clumps with librw (`src/platform/texture_dictionary.cpp`) and the packed PS2 geometry
with its own decoder ([The streamed world](../world.md#coneys-implementation)). `coney-tools extract` decodes the
textures above to PNG without RenderWare ([python/src/coney_tools/ps2tex.py](repo:python/src/coney_tools/ps2tex.py)).

## Open questions

- Whether the mask name is ever non-empty, and what the `0x110` extension's value is used for on this disc.
- The 4-bit palette's third row (8 colours past the 16 used): padding, or a second palette?
