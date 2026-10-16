# PS2 memory card icon and icon.sys

## Scope and sources

This page records the two files that make the game's save look right in the PS2's memory card browser: the 3D icon
`WARR.ICO` and the settings file `icon.sys`. Both sit in the save directory `BASLUS-21215` beside the save file; the
save file itself is on the save data page. Both formats belong to the console's browser and are documented publicly
by the homebrew community; this page records what this game's files hold.

Sources, all from the PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso":

| Source | Size (bytes) | Used for |
| --- | ---: | --- |
| `WARRIORS.DIR` / `WARRIORS.WAD` | 128,428 / 1,495,371,776 | The archive entry that holds the icon |
| WAD entry 10669 | 79,128 | The icon file, read in full on the disc image |
| `SLUS_212.15` | 4,903,976 | The `icon.sys` values, which exist only as code (addresses below) |

All values are little-endian. Fixed-point values are 4.12: 4,096 stands for 1.0.

## Where the files come from

| File on the card | Size (bytes) | Origin |
| --- | ---: | --- |
| `BASLUS-21215/WARR.ICO` | 79,128 | Copied unchanged from the archive entry `warr.ico` |
| `BASLUS-21215/icon.sys` | 964 | Made by the game; there is no copy on the disc |

- `warr.ico` is WAD entry 10669: offset 1,492,889,600 in `WARRIORS.WAD`, size 79,128, name hash `0xB684A988` (the
  archive's name hash of the entry's full archive path).
- The game writes both files when it makes the save directory and writes `icon.sys` again with every save. The game
  never reads either file back; only its size is checked (79,128 and 964 bytes).
- The browser shows the same icon in all three of its uses (list, copy and delete): all three icon names in
  `icon.sys` are `WARR.ICO`.

## Icon file layout

| Part | Offset | Size (bytes) | Contents |
| --- | --- | ---: | --- |
| Header | `0x00000` | 20 | Five u32 values (see below) |
| Vertices | `0x00014` | 46,080 | 576 vertex records of 80 bytes |
| Animation header | `0x0B414` | 20 | Five values (see below) |
| Frames | `0x0B428` | 240 | 8 frame records with their keys |
| Texture | `0x0B518` | 32,768 | 128 × 128 pixels of 16 bits |

The parts fill the file exactly: 20 + 46,080 + 20 + 240 + 32,768 = 79,128.

### Header

| Offset | Type | Field | Value in `warr.ico` |
| --- | --- | --- | --- |
| `0x00` | u32 | Magic | `0x00010000` |
| `0x04` | u32 | Shape count | 8 |
| `0x08` | u32 | Texture type | 7 (bit 3, value 8, is clear, so the texture is stored uncompressed) |
| `0x0C` | u32 | Unused | 0 |
| `0x10` | u32 | Vertex count | 576 |

### Vertex record

Each vertex is 8 × shape count + 16 bytes; with 8 shapes that is 80 bytes. Every three vertices form one triangle,
so the icon has 192 triangles.

| Offset | Type | Field | Values in `warr.ico` |
| --- | --- | --- | --- |
| `+0x00` | s16[4] × 8 | Position x, y, z, w for each shape | x −9,888 to 3,909; y −20,009 to −497; z −8,136 to 3,121; w 1,024 in every vertex |
| `+0x40` | s16[4] | Normal x, y, z, w | Unit length in 4.12 (squared length about 16.77 million); w 4,096 in every vertex |
| `+0x48` | s16[2] | Texture u, v (4.12) | u 258 to 3,925; v 285 to 3,980 (0.06 to 0.97) |
| `+0x4C` | u8[4] | Vertex colour R, G, B, A | 128, 128, 128, 128 in every vertex (128 is full) |

The eight shapes of `warr.ico` are identical: every vertex has the same position in all eight. The icon therefore
keeps one fixed shape in the browser even though its animation fades between shapes.

### Animation header

| Offset | Type | Field | Value in `warr.ico` |
| --- | --- | --- | --- |
| `+0x00` | u32 | Animation id | 1 |
| `+0x04` | u32 | Frame length | 100 |
| `+0x08` | f32 | Animation speed | 0.5 |
| `+0x0C` | u32 | Play offset | 0 |
| `+0x10` | u32 | Frame count | 8 |

### Frame records

Each frame record is a u32 shape number and a u32 key count, followed by that many keys of two f32 values (time,
weight). The 8 frames of `warr.ico` hold 22 keys:

| Frame | Shape | Keys (time, weight) |
| ---: | ---: | --- |
| 0 | 0 | (0, 1), (14.0625, 0) |
| 1 | 1 | (0, 0), (14.0625, 1), (28.125, 0) |
| 2 | 2 | (14.0625, 0), (28.125, 1), (42.1875, 0) |
| 3 | 3 | (28.125, 0), (42.1875, 1), (56.25, 0) |
| 4 | 4 | (42.1875, 0), (56.25, 1), (70.3125, 0) |
| 5 | 5 | (56.25, 0), (70.3125, 1), (84.375, 0) |
| 6 | 6 | (70.3125, 0), (84.375, 1), (98.4375, 0) |
| 7 | 7 | (84.375, 0), (98.4375, 1) |

Each shape peaks at a time 14.0625 later than the one before and fades out over the next 14.0625.

### Texture

| Property | Value |
| --- | --- |
| Size | 128 × 128 pixels, 32,768 bytes, rows top to bottom |
| Pixel | u16: red in bits 0 to 4, green in bits 5 to 9, blue in bits 10 to 14, bit 15 a flag |
| Flag bit | Clear in all 16,384 pixels |
| Colours | 698 distinct; the average is a dark red-brown (about 73, 52, 48 out of 255) |
| Picture | The Warriors' leather vest laid out flat: the front with the "WARRIORS" rocker patch above the flaming emblem, the back half beside it, on a dark background |

## icon.sys layout

The file is 964 bytes. The layout is the console's; the values are the ones the game writes.

| Offset | Size | Type | Field | Value written |
| --- | ---: | --- | --- | --- |
| `0x000` | 4 | char[4] | Magic | `PS2D` |
| `0x004` | 2 | u16 | Reserved | 0 |
| `0x006` | 2 | u16 | Byte offset of the title's second line | 32 |
| `0x008` | 4 | u32 | Reserved | 0 |
| `0x00C` | 4 | u32 | Background transparency | 96 (`0x60`) |
| `0x010` | 64 | u32[4] × 4 | Background colour of each corner (R, G, B, unused) | 170, 43, 43, 0 in all four corners |
| `0x050` | 48 | f32[4] × 3 | Light directions (x, y, z, unused) | (0.5, 0.5, 0.5, 0), (0, −0.4, −0.1, 0), (−0.5, −0.5, 0.5, 0) |
| `0x080` | 48 | f32[4] × 3 | Light colours (R, G, B, unused) | (0.48, 0.48, 0.03, 0), (0.5, 0.33, 0.2, 0), (0.14, 0.14, 0.38, 0) |
| `0x0B0` | 16 | f32[4] | Ambient light (R, G, B, unused) | (0.5, 0.5, 0.5, 0) |
| `0x0C0` | 68 | Shift-JIS | Title | "The Warriors" in full-width characters, 24 bytes, then zeros |
| `0x104` | 64 | char[64] | Icon file for the list view | `WARR.ICO`, then zeros |
| `0x144` | 64 | char[64] | Icon file for copying | `WARR.ICO`, then zeros |
| `0x184` | 64 | char[64] | Icon file for deleting | `WARR.ICO`, then zeros |
| `0x1C4` | 512 | | Reserved | Zeros |

- The title's 12 characters are stored as full-width Shift-JIS: capital letters `0x82 0x60` to `0x82 0x79`, small
  letters `0x82 0x81` to `0x82 0x9A`, the space `0x81 0x40`. The title bytes start
  `82 73 82 88 82 85 81 40 82 76`.
- The title is 24 bytes long and the second line would start at byte 32, so the browser shows the title on one line.
- The four background corners share one colour, a dark red (170, 43, 43).
- The light directions and colours are stored as f32: for example 0.48 is `0x3EF5C28F` and −0.4 is `0xBECCCCCD`.

## Differences from the Xbox version

The Xbox version saves to the console's hard disk and has no `WARR.ICO` or `icon.sys`; this page applies to the PS2
version only.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0014ad90` | Build the icon settings file | icon.sys layout |
| `0x00420060` | Make the save directory and its three files on the card | Where the files come from |

## Acceptance checks

- The PS2 archive entry `warr.ico` (WAD entry 10669) is 79,128 bytes and begins with the bytes
  `00 00 01 00 08 00 00 00 07 00 00 00 00 00 00 00 40 02 00 00`.
- Read as header, 576 vertices of 80 bytes, animation header, 8 frames and texture, the entry ends exactly at its
  last byte, with 32,768 bytes of texture starting at offset 46,360.
- All eight shapes in the entry give every vertex the same position.
- The texture decodes to a 128 × 128 picture of the Warriors' vest with the "WARRIORS" patch over a flaming emblem.
- `WARR.ICO` in the save directory of a card the game has saved to is byte-for-byte identical to the archive entry.
- `icon.sys` in the save directory is 964 bytes, starts with `PS2D`, holds 32 at byte 6 and 96 at byte 12, and
  holds `WARR.ICO` at bytes 260, 324 and 388.
- In the PS2 memory card browser the save shows the title "The Warriors" on one line over a dark red background,
  and the vest icon does not change shape.

## Open questions

- How the browser turns the frame keys' time units into seconds was not checked.
- Whether the browser draws the background transparency 96 as partly see-through was not checked on hardware.
