# Memory card icon (`.ico`)

Verified against: the NTSC-U disc's `WARRIORS.WAD` (entry `warr.ico`, 79,128 bytes). Disc check made with
`coney-tools extract` (2026-10-07), counts only.

## Purpose

The icon the PS2's memory card browser shows for the game's save: a small 3D model that morphs between shapes, with
one texture. The game copies the file into the save folder as `WARR.ICO` ([Save](../save.md)); it never parses it, so
there is no loader in the executable. The format is the console's own, documented publicly by the homebrew
community; this page records what this file holds.

## Original structure

None in the game: the console's browser reads the file. [Save](../save.md) has where the game copies it.

## Data

All values little-endian. Fixed-point values are 4.12 (`4096` = 1.0). Inferred from the public format; the disc file
reads exactly to its end this way (disc check, corroboration).

| Part | Size | Contents |
| --- | --- | --- |
| Header | 20 | `u32 0x00010000`, `u32` shape count (8), `u32` texture type (7), `u32` 0, `u32` vertex count (576) |
| Vertices | `vertices × (8 × shapes + 16)` | per vertex: one `s16 x, y, z, w` position per shape, an `s16` normal (`x, y, z, w`), `s16 u, v` and an RGBA colour (128 = full) |
| Animation header | 20 | `u32 1`, `u32` frame length (100), `f32` speed (0.5), `u32` play offset (0), `u32` frame count (8) |
| Frames | | per frame `{u32 shape, u32 key count}` and that many `{f32 time, f32 weight}` keys |
| Texture | 32,768 | 128 × 128, 16-bit `A1B5G5R5` (red in the low bits); uncompressed because bit 3 of the texture type is clear |

The vertices form a triangle list (every three vertices one triangle). On this icon each frame fades one shape in
and the previous one out over 14.0625 time units.

## Coney's implementation

`coney-tools extract` (type `data`) writes `data/icon/warr.gltf` (shape 0, the other shapes as morph targets),
`warr.png` and `warr.json` (header and animation)
([python/src/coney_tools/ps2icon.py](repo:python/src/coney_tools/ps2icon.py)). The engine does not need the icon.

## Open questions

- None for extraction. How the browser maps the frames' time units to seconds was not checked.
