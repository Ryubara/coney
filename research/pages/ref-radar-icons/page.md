# Radar icons

This reference page lists the radar icon ids found on the disc. Each id is a rectangle index on the sprite sheet
`part_page0`, and the table gives that rectangle, its size in texels, what it shows and which scripts pass the id.
Every value was read from the PS2 disc (NTSC-U, `SLUS_212.15`): the sprite sheet from `WARRIORS.WAD` entries 4,106
(`global.pak`) and 528, and the icon ids from the compiled Lua 4.0 scripts in the same archive. The Xbox disc was
read for comparison.

## Disc sources

| Source | Directory entry (zero-based) | Size in bytes | Values taken from it |
| --- | ---: | ---: | --- |
| `global.pak` | 4,106 | 599,968 | A pack of 11 resources; its seventh resource (hash `0x5CD2A2BC`) is `part_page0`, a texture dictionary chunk `0x2A` and a sprite page chunk `0x4C` |
| Unnamed entry, hash `0xA30CDA28` | 528 | 138,512 | A flat container holding only the `part_page0` resource; its `0x2A` and `0x4C` payloads are byte-identical to those in `global.pak` |
| Lua scripts | 467 entries | various | The icon ids passed to the radar item texture function, and three global values used for them |

The script names on this page were matched to their directory hashes (CRC-32 of `./ee_files/` plus the name). One
script, entry 8,258 with hash `0x36763710`, has no recovered name.

## The sprite sheet

`part_page0` is a 512 x 256 texture with 8-bit palette indices. Its sprite page is the `0x4C` chunk of the same
resource, 5,968 bytes long: a 20-byte header, 371 rectangle records of 16 bytes, then 12 zero bytes that pad the
payload to a multiple of 16.

### Header

| Offset | Size | Type | Value in `part_page0` | Meaning |
| ---: | ---: | --- | --- | --- |
| 0x00 | 4 | u32 | `0x00421798` | The same constant in every `0x4C` chunk |
| 0x04 | 4 | u32 | 371 | Number of rectangle records |
| 0x08 | 4 | u32 | 94 | Not identified |
| 0x0C | 4 | u32 | 0 | Zero on the disc |
| 0x10 | 4 | u32 | 0 | Zero on the disc |

### Rectangle record

Record n starts at payload offset 0x14 + 16 x n. An icon id is the record's index n.

| Offset | Size | Type | Meaning |
| ---: | ---: | --- | --- |
| 0x00 | 4 | f32 | u0, left edge as a fraction of the texture width |
| 0x04 | 4 | f32 | v0, top edge as a fraction of the texture height |
| 0x08 | 4 | f32 | u1, right edge as a fraction of the texture width |
| 0x0C | 4 | f32 | v1, bottom edge as a fraction of the texture height |

Each edge is moved inward by 1/2048 of the full texture coordinate range: on the 512 x 256 PS2 texture that is a
quarter texel across and an eighth of a texel down. With that inset removed, all 371 rectangles fall on whole
texels.

Example: icon 28 is the record at payload offset 0x1D4, holding 0.06494140625, 0.56298828125, 0.08935546875 and
0.61279296875, which gives x 33, y 144, 13 x 13 texels.

## Icon table

Position and size are in texels on the PS2 `part_page0` texture, with 0, 0 at the top left. The image column
describes the decoded PS2 texels inside the rectangle.

| Icon id | Position (x, y) | Size (w x h) | Image | Passed by scripts |
| ---: | --- | --- | --- | ---: |
| 22 | 476, 125 | 25 x 27 | Purple diamond with a white W | 11 |
| 27 | 47, 144 | 13 x 13 | Red disc with a white W | 31 |
| 28 | 33, 144 | 13 x 13 | Yellow disc with a black W | 85 |
| 29 | 302, 125 | 15 x 23 | Green lightning bolt | 2 |
| 30 | 420, 157 | 18 x 23 | Green spray can | 3 |
| 31 | 400, 157 | 19 x 23 | Green knife | 0 |
| 32 | 352, 181 | 21 x 21 | Blue diamond with a white exclamation mark | 0 |
| 33 | 439, 157 | 21 x 21 | Orange diamond with a white exclamation mark | 0 |
| 34 | 276, 88 | 34 x 34 | Grey head with dark hair | 1 |
| 69 | 297, 246 | 10 x 10 | One small white dot in the middle; the rest is clear | 0 |
| 350 | 61, 144 | 7 x 12 | Red figure 1 | 3 |
| 351 | 338, 144 | 9 x 12 | Yellow figure 2 | 4 |
| 352 | 138, 109 | 13 x 13 | Yellow disc | 0 |
| 353 | 476, 153 | 21 x 21 | Yellow diamond | 0 |
| 355 | 388, 241 | 11 x 11 | Purple dot with a black outline | 1 |
| 356 | 376, 241 | 11 x 11 | Blue dot with a black outline | 0 |
| 359 | 352, 241 | 11 x 11 | Orange dot with a black outline | 4 |
| 360 | 297, 233 | 16 x 12 | Red 1P | 3 |
| 361 | 318, 144 | 19 x 12 | Yellow 2P | 4 |
| 362 | 485, 31 | 11 x 11 | Red triangle pointing up | 2 |
| 365 | 137, 144 | 11 x 11 | White dot with a black outline | 0 |

No script on the disc passes icons 31, 32, 33, 69, 352, 353, 356 or 365.

## Icons the scripts set

The scripts pass an icon id as the second value given to the global function `HUDSetRadarItemTexture`, a name that
appears in the scripts' own constant strings. The first value is a script variable in every use. On the PS2 disc the
function is used 154 times in 70 scripts, always with one of 13 icon ids. In 153 uses the id is a number in the
script; in one use (`level5_chapter2.lua`) it is the global `HUD_WAR`, which `level5.lua` sets to 28.

| Icon id | Times passed | Scripts | Scripts that pass it (times, when more than one) |
| ---: | ---: | ---: | --- |
| 22 | 11 | 8 | `level93_chapter2.lua`, `level95_busted.lua`, `level95_coney.lua` (2), `level95_flashhead.lua` (3), `level95_homeless.lua`, `level95_robinhood.lua`, `level95_vigilante.lua`, `tagbt.lua` |
| 27 | 31 | 25 | `global.lua`, entry 8,258, `level20_mainstreet.lua`, `level2_clinic.lua` (3), `level2_clubhouse.lua`, `level31_reddevil.lua` (2), `level31_sewers.lua`, `level34_gate.lua`, `level34_park.lua`, `level34_riot.lua`, `level5.lua`, `level52.lua`, `level52_chapter1.lua`, `level81_chase.lua`, `level81_cracker.lua`, `level86_chapter1_reg.lua`, `level86_chapter3_reg.lua`, `level87.lua`, `level93_chapter4.lua`, `level93_chapter5.lua`, `level95_coney.lua` (2), `level99_lesson1.lua`, `level9_chap3_escape.lua`, `run4life.lua` (2), `wchair.lua` (2) |
| 28 | 85 | 48 | `global.lua`, entry 8,258 (6), `level11_chapter2.lua`, `level11_chapter4.lua`, `level11_chapter5.lua` (2), `level14_dirtycop.lua`, `level14_jsb.lua`, `level20_backalley.lua`, `level20_bossfight.lua`, `level20_mainstreet.lua` (2), `level20_marketplace.lua` (2), `level2_clinic.lua`, `level2_junkyard.lua`, `level2_tenement.lua`, `level31_gclub.lua` (3), `level31_sewers.lua`, `level34_riot.lua`, `level3_balcony.lua` (3), `level3_comp.lua`, `level5.lua`, `level51_chapter3.lua`, `level52.lua` (6), `level5_chapter2.lua`, `level80_chapter3.lua` (4), `level81_horror_house.lua` (2), `level82_chapter2.lua` (2), `level83_c3_diner.lua`, `level83_c4_shanty.lua`, `level86.lua`, `level86_chapter1_reg.lua`, `level87.lua`, `level87_chap1_tutorial.lua`, `level87_chap2_street1.lua`, `level87_chap3_shanty.lua`, `level87_chap4_street2.lua`, `level87_chap5_club.lua`, `level92_chapter1.lua`, `level92_chapter2.lua` (5), `level93_chapter1.lua` (2), `level93_chapter2.lua` (3), `level93_chapter3.lua` (4), `level93_chapter5.lua`, `level95_coney.lua` (2), `level99_lesson2.lua` (4), `level9_chap1_radio.lua`, `level9_chap2_tags.lua`, `level9_chap3_escape.lua`, `wchair.lua` (2) |
| 29 | 2 | 1 | `level99_lesson2.lua` (2) |
| 30 | 3 | 2 | `level87_chap2_street1.lua` (2), `level95_coney.lua` |
| 34 | 1 | 1 | `caps.lua` |
| 350 | 3 | 2 | `tagbt.lua`, `wchair.lua` (2) |
| 351 | 4 | 2 | `tagbt.lua` (2), `wchair.lua` (2) |
| 355 | 1 | 1 | `level95_invasion.lua` |
| 359 | 4 | 3 | `level51_chapter2a.lua` (2), `level51_chapter3.lua`, `level82_chapter4.lua` |
| 360 | 3 | 2 | `tagbt.lua`, `wchair.lua` (2) |
| 361 | 4 | 2 | `tagbt.lua` (2), `wchair.lua` (2) |
| 362 | 2 | 2 | `level83.lua`, `level9_chap3_escape.lua` |

The count for icon 28 includes the one use through `HUD_WAR`. `caps.lua`, `run4life.lua`, `tagbt.lua` and
`wchair.lua` share their names with Rumble modes.

### Further values passed with the icon

Most uses pass only the object and the icon. The others pass one or two more values:

| Icon id | Third value | Fourth value | Uses | Scripts |
| ---: | --- | --- | ---: | --- |
| 22 | 0.65 | none | 9 | The six `level95_` scripts in the table above |
| 22 | 0.75 | none | 1 | `tagbt.lua` |
| 28 | 0.75 | none | 6 | `level3_balcony.lua` (3), `level3_comp.lua`, `level82_chapter2.lua` (2) |
| 28 | global `RADAR_ICON_SIZE`, which `level9.lua` sets to 1 | none | 3 | `level9_chap1_radio.lua`, `level9_chap2_tags.lua`, `level9_chap3_escape.lua` |
| 30 | 0.8 | none | 2 | `level87_chap2_street1.lua` |
| 34 | 1 | none | 1 | `caps.lua` |
| 350, 351, 360, 361 | 0.8 | global `true`, which `global.lua` sets to 1 | 14 | `tagbt.lua`, `wchair.lua` |
| 355 | 1 | none | 1 | `level95_invasion.lua` |
| 362 | 1 | nil | 1 | `level83.lua` |

## Differences on the Xbox disc

The Xbox disc holds `part_page0` three times, byte-identical: inside `global.pak` (index entries 0 and 9,704) and on
its own (entry 5,899 in `Main.wad1`, 355,840 bytes). There the texture is 512 x 512 in DXT4/5, and the `0x4C` chunk
has the same 5,968-byte size, the same header words and 371 rectangles, but a different arrangement. The same
1/2048 inset applies, a quarter texel on both axes of the square texture. 111 of the 371 rectangles keep their PS2
size; all 21 icons on this page do.

| Icon id | PS2 position | Xbox position | Size on both |
| ---: | --- | --- | --- |
| 22 | 476, 125 | 175, 465 | 25 x 27 |
| 27 | 47, 144 | 354, 447 | 13 x 13 |
| 28 | 33, 144 | 340, 447 | 13 x 13 |
| 29 | 302, 125 | 225, 465 | 15 x 23 |
| 30 | 420, 157 | 492, 399 | 18 x 23 |
| 31 | 400, 157 | 492, 375 | 19 x 23 |
| 32 | 352, 181 | 285, 465 | 21 x 21 |
| 33 | 439, 157 | 263, 465 | 21 x 21 |
| 34 | 276, 88 | 387, 410 | 34 x 34 |
| 69 | 297, 246 | 432, 466 | 10 x 10 |
| 350 | 61, 144 | 378, 447 | 7 x 12 |
| 351 | 338, 144 | 368, 447 | 9 x 12 |
| 352 | 138, 109 | 460, 90 | 13 x 13 |
| 353 | 476, 153 | 241, 465 | 21 x 21 |
| 355 | 388, 241 | 420, 466 | 11 x 11 |
| 356 | 376, 241 | 408, 465 | 11 x 11 |
| 359 | 352, 241 | 384, 465 | 11 x 11 |
| 360 | 297, 233 | 336, 90 | 16 x 12 |
| 361 | 318, 144 | 297, 90 | 19 x 12 |
| 362 | 485, 31 | 372, 465 | 11 x 11 |
| 365 | 137, 144 | 348, 465 | 11 x 11 |

The decoded Xbox icons show the same images as the PS2 ones. The Xbox scripts (503 entries) pass the same 13 icon
ids the same number of times, 154 in all, including the one use through `HUD_WAR`.

## Acceptance checks

- Entry 4,106 of the PS2 disc's `WARRIORS.DIR` has the hash of `./ee_files/global.pak` and is a 599,968-byte pack of
  11 resources, one of them `part_page0` with resource hash `0x5CD2A2BC`.
- Entry 528 is a 138,512-byte flat container of the same resource; its `0x4C` payload equals the one in `global.pak`
  byte for byte.
- The `part_page0` texture is 512 x 256 texels with 8-bit indices.
- The `part_page0` `0x4C` payload is 5,968 bytes; its header words are `0x00421798`, 371, 94, 0 and 0, and the 12
  bytes after the 371st record are zero.
- All 371 rectangles fall on whole texels once the 1/2048 inset is removed from each edge.
- Rectangle 28 is 13 x 13 texels at 33, 144; rectangle 34 is 34 x 34 at 276, 88; rectangle 22 is 25 x 27 at
  476, 125.
- Decoded, rectangle 28 shows a yellow disc with a black W and rectangle 27 a red disc with a white W; rectangles 350,
  351, 360 and 361 show a red 1, a yellow 2, a red 1P and a yellow 2P.
- The PS2 scripts use `HUDSetRadarItemTexture` 154 times in 70 scripts, with the 13 icon ids and counts in the table
  above.
- No PS2 script passes icon 31, 32, 33, 69, 352, 353, 356 or 365.
- The Xbox disc holds three byte-identical copies of `part_page0`, a 512 x 512 texture with 371 rectangles, and the 21
  icons on this page have the same sizes there as on the PS2.

## Open questions

- Blip types: the radar's blip type numbers 1 to 10 and the icon each starts with are not held in any data file on
  the disc, so they are not listed. The claim that types 1 to 8 start with icons 22 to 29, type 9 with 30 and type
  10 with 31 is unconfirmed, as are the sprite batches the blips are drawn in.
- What puts icons 31, 32, 33, 69, 352, 353, 356 and 365 on the radar; no script passes them.
- That the radar draws icon n from rectangle n of `part_page0` fits the images on every rectangle in the table, but
  was not observed in game.
- What each icon marks in game (for example, when a mission objective shows icon 27 rather than 28) was not observed.
- What the third and fourth values passed with an icon change on screen (size, a flag) is not confirmed.
- Per-icon scale or colour values other than those the scripts pass were not found in any data file.
- The meaning of header word 0x08 of the `0x4C` chunk: 94 on `part_page0`, other values on other sprite pages.
- The name of script entry 8,258 (hash `0x36763710`).
- Whether `HUD_WAR` always holds 28 when `level5_chapter2.lua` uses it; `level5.lua` is the only script that sets it.
