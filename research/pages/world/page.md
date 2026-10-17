# Streamed world

A level's fixed scenery (buildings, streets, fences, graffiti) is not stored in its level file. It is held in two
streamed worlds per level, each made of a world stream that holds the layout and a set of numbered part files that
hold the geometry. This page gives the layout of those files as found on the PS2 disc (NTSC-U, `SLUS_212.15`), how
the game picks which parts to bring in, how far the world is drawn, and the per-level world settings the level
scripts make: fog colour and rain. Every count was read from entries of `WARRIORS.WAD` located through
`WARRIORS.DIR`. All values are little-endian.

The memory budgets that streams and parts get, the `_sec.mem` manifest layout and the rules for releasing parts are
on the memory budgets page. File kinds and totals for the whole archive are on the archive contents page.

## Disc sources

| Source | Count on the PS2 disc | Values taken from it |
| --- | ---: | --- |
| `<world>_sec.wld` | 160 | World layout: sector tree, sector records, texture dictionary, part count |
| `<world>_ms<n>.sec` | 1,915 (1,564 listed by their worlds) | Geometry of the sectors, one or more per part |
| `<world>_sec.mem` | 159 | Part count, used here only to cross-check the streams |
| `level<N>.lua` and chapter scripts | 467 compiled Lua 4.0 scripts in all | Fog colour, fog distance and rain settings |

## Two worlds per level

Every level that has world files has two of them, named after the level with `s` or `d` appended: `level99s` and
`level99d` for the tutorial. The disc holds 79 such pairs (158 worlds), plus two single worlds, `courtyard` and
`objarena`.

| | `s` world | `d` world |
| --- | --- | --- |
| What it shows | The solid scenery: walls, buildings, shop fronts, roofs, streets | A detail layer: graffiti, fences, wire, vegetation, road markings, grime |
| Example textures in the tutorial | `4_brikwallb_000`, `4_msumwall1_000`, `road_db_000`, `4_door02_000` | `4_graf01_000`, `fence_hirez_000`, `fen_barb_b_000`, `vegeeric001_000`, `road_details_000` |
| Tutorial counts | 142 sectors, 47 with geometry, 20 parts, 168 textures in the stream | 68 sectors, 27 with geometry, 11 parts, 30 textures in the stream |
| Geometry flags `0x010200B9` (two texture-coordinate sets) | 74 of 79 worlds | 12 of 79 worlds |
| Geometry flags `0x0101003D` (one texture-coordinate set) | 5 of 79 (`level62s`, `level100s`, `level110s`, `level125s`, `level130s`) | 67 of 79 worlds |
| Potentially-visible-set data | 18 of 79 worlds (see Visibility) | none |
| Material-effects data on part geometry | 2,111 of 3,449 geometry pieces | 27 of 1,865 geometry pieces |

Both geometry flag values mark triangle strips, pre-lit vertex colours, normals and lighting; they differ in the
number of texture-coordinate sets. The world header carries the same flags with `0x40000000` added
(`0x410200B9`, `0x4101003D`). `courtyard` uses `0x4101007D`.

## World stream

`<world>_sec.wld` holds no geometry of its own: its world section has no triangles, no vertices and no materials.

| Offset | Size | Contents |
| --- | --- | --- |
| `0x00` | 4 | Part count: the number of part files the world uses |
| `0x04` | varies | A RenderWare texture dictionary section (type `0x16`, stamp `0x1C02000A`, device 6) |
| after it | varies | A RenderWare world section (type `0x0B`), up to the end of the file |

| Fact | Value on the disc |
| --- | --- |
| Part count equals the manifest's part count | all 159 worlds that have a manifest |
| Part count range | 1 to 46 |
| World header triangles, vertices, materials | 0, 0, 0 in all 160 |
| Plane sections | sector count less one, in all 160 |
| Sectors per world | 1 (several `d` worlds such as `level100d`) to 2,406 (`level70s`) |
| Textures per stream | 1 (`level1d`) to 251 (`level5s`) |
| Stamp | `0x1C02000A` in 159 streams; `courtyard_sec.wld` is stamped `0x1803FFFF` |
| Tutorial stream sizes | `level99s_sec.wld` 811,848 bytes, `level99d_sec.wld` 274,856 bytes |

The world header's bounding box gives the world's extent. For `level99s` it runs from (-333.83, -2.89, -232.57) to
(227.88, 42.52, 125.69).

## Sectors

Each leaf of the world's sector tree is a sector section (type `0x09`) with a bounding box and an extension. In
every world but `courtyard`, each sector's extension holds a sector record (plugin id `0x3F1`, 20 bytes). Sectors
that also carry potentially-visible-set data have plugin `0x12A` beside it.

### Sector record

| Offset | Size | Type | Meaning |
| --- | --- | --- | --- |
| `0x00` | 4 | s32 | Geometry index: which geometry piece belongs to this sector; -1 when the sector has none |
| `0x04` | 4 | u32 | Part number (counting from 1) of the part file that holds the geometry; 0 when there is none |
| `0x08` | 12 | 3 x f32 | Placement point of the geometry; all zero when there is none |

| Fact | Value on the disc |
| --- | --- |
| Sectors in all 160 worlds | 16,113 |
| Sectors with geometry | 5,315 |
| Sectors without geometry | 10,759; every one has index -1, part 0, a zero point and a zero-size box |
| Sectors with no sector record | 39, all in `courtyard`; with the two rows above they make up the 16,113 |
| Geometry indices in a world | always 0 up to one less than the number of sectors with geometry, each used once |
| Part numbers | always between 1 and the world's part count |
| Placement points | multiples of 1/8 unit in 5,314 of 5,315 records |
| Most sectors with geometry in a world | 593 (`level70s`); 144 (`level84s`) among worlds of levels that have a level file |

## Part file

`<world>_ms<n>.sec` holds the geometry of the sectors whose records name part n.

| Offset | Size | Contents |
| --- | --- | --- |
| `0x00` | 4 | 1 |
| `0x04` | 4 | 0 |
| `0x08` | 4 | 0 |
| `0x0C` | 4 | CRC-32 of the part's own lower-case file name, for example of `level99s_ms1.sec` |
| `0x10` | varies | A RenderWare texture dictionary section (type `0x16`) |
| after it | 4 | Count of geometry pieces in this part |
| after it | varies | For each piece: a u32 geometry index, then one RenderWare atomic section (type `0x14`) |

| Fact | Value on the disc |
| --- | --- |
| Header hash equals the CRC-32 of the file name | all 1,564 listed parts |
| Each geometry index of a world | in exactly one listed part, the part its sector record names (all 159 worlds with a manifest) |
| Pieces per part | 1 to 61; parts with 2 pieces (444) and 3 pieces (409) are the most common |
| Triangles and vertices in all listed parts | 2,884,456 and 4,965,046 |
| Example | `level99s_ms1.sec`, 248,154 bytes, holds geometry indices 29, 32 and 44 |
| `courtyard` | its four parts have no 16-byte header and start with a dictionary stamped `0x1803FFFF` |

Each atomic holds one native PS2 geometry (section `0x0F`) and, in its extension, plugin `0x1F` (8 bytes,
`03 00 00 00 83 00 03 00` in 5,314 of the 5,315 pieces; the one `objarena` piece stores `0x00030082`),
material-effects plugin `0x120` on some pieces, and plugin `0x3F0`.

### Geometry plugin `0x3F0`

| Offset | Size | Type | Values on the disc |
| --- | --- | --- | --- |
| `0x00` | 4 | f32 | Position scale (as on `model-format`): a power of two from 1/32,768 to 1/64 in 5,314 of 5,315 pieces; 1/512 and 1/1,024 are the most common |
| `0x04` | 4 | f32 | Texture-coordinate scale (as on `model-format`): a power of two from 1/32,768 to 1/64 in 5,314 of 5,315 pieces; 1/2,048 and 1/4,096 are the most common |
| `0x08` | 4 | u32 | 0 in all 5,315 |

### Part files no world lists

347 headed part files in 70 worlds are numbered above their world's part count, for example `level99s_ms21.sec` to
`level99s_ms27.sec`. No sector record names them, and 87 of them hold geometry indices higher than any their world
uses. They are never streamed in.

## Coordinates

The world files are y-up. A game position (x, y, z), as used in the level list and the scripts, is the point
(x, z, -y) of the world files. All eleven story-start positions checked (in `level99`, `level34`, `level2`,
`level31`, `level20` and `level64`) fall inside their `s` world's box under this mapping; seven of them fall outside
it when read unchanged.

## Streaming

Both world streams are loaded when a level starts and kept until it ends. Parts come and go during play.

| Fact | Outcome |
| --- | --- |
| Parts in flight | At most one world part is being read at any time, and none while other file reads are queued |
| Sectors in view | A sector seen in a player view that lacks its geometry and is close enough to player 1's camera gets it before any other, nearest first (see Open questions) |
| Sectors out of view | A sector lacking its geometry gets it while no sector in view qualifies, nearest first, wherever it is, so with room every part of both worlds ends up loaded |
| Distance | Measured from the nearest player-view camera to the sector's box |
| Unit of transfer | A whole part file; every sector whose geometry it holds gets its geometry together |
| Placement | Each geometry piece is placed in the world at its sector record's placement point |
| Arrival | Newly arrived geometry fades in from fully transparent to fully opaque over 1,000 ms of the game clock |
| Competition | World parts and models of nearby characters, objects and cars are brought in through the same queue and compete by distance |

On the tutorial, all 31 parts of both worlds are resident at once.

## Visibility

| Fact | Outcome |
| --- | --- |
| Sectors drawn | Only sectors with geometry loaded, seen in the view and not hidden behind the level's occluders |
| Worlds with potentially-visible-set data | The `s` worlds of `level2`, `level3`, `level5`, `level11`, `level14`, `level31`, `level34`, `level51`, `level52`, `level80`, `level82`, `level83`, `level86`, `level87`, `level92`, `level93`, `level95` and `level99`, and `courtyard` |
| In those worlds | A sector is also left out when that data says it cannot be seen from the region holding a camera's position |

## Draw distance

The streamed world is drawn out to the player view's draw distance. How that distance is set and how it changes
during play, also while world parts inside it are still loading, is on page `draw-distance`.

## What is drawn where

Only the 18 `s` worlds and `courtyard` listed under Visibility say, place by place, which sectors can be seen. In
every other world, and in all 79 `d` worlds, the data names no sectors from any place, and what is drawn follows the
view, the occluders and the draw distance (see Visibility and Draw distance). Neither world of 46 of the 64 levels
with a level file has this data.

### Sector regions

Every sector covers one box-shaped region of the world: the world header's box divided by the plane sections of the
sector tree. This region is not the sector's own box in the file, which is zero-size for a sector without geometry.
Sector numbers below count the sector sections of a world in the order they are stored, from 0.

| Fact | Value on the disc |
| --- | --- |
| Plane section, first field (s32) | The axis it divides: 0 for x, 4 for y, 8 for z; 1,111, 953 and 1,204 planes in the 18 worlds |
| Plane section, second field (f32) | Where it divides; the child stored before the other covers the lower side |
| Placement points | 1,206 of the 1,211 sectors with geometry in the 18 worlds have their placement point inside their own region |
| Story start positions in a region | Every one of the 18 checked in `level99`, `level34`, `level2` and `level31` lies in the region of a sector with geometry, and so do points 2 and 4 units above them |

### Visibility map

Plugin `0x12A` of a sector holds its visibility map. In the 18 `s` worlds the world's own extension also carries
plugin `0x12A`, 8 bytes holding the values 1 and 0.

| Offset | Size | Type | Meaning |
| --- | --- | --- | --- |
| `0x00` | 4 | u32 | The sector's own number |
| `0x04` | 4 | u32 | Map length in bytes: the world's sector count divided by 8, rounded up (18 bytes for 142 sectors in `level99s`) |
| `0x08` | 4 | u32 | 0 in every sector of the 18 `s` worlds |
| `0x0C` | varies | bits | One bit per sector, sector 0 in the lowest bit of the first byte; a set bit means that sector can be seen from this sector's region |

| Fact | Value in the 18 `s` worlds |
| --- | --- |
| Sectors, with geometry | 3,286, of which 1,211 have geometry |
| Sectors whose map names itself | All 3,286 |
| Maps naming another sector without geometry | None; every other sector named has geometry |
| Sectors whose map names at least one sector with geometry | 1,327: the 1,211 with geometry and 116 without |
| Sectors whose map names no sector with geometry | 1,959, all without geometry, naming only themselves |
| Bits naming another sector | 31,898; in 17,388 of them the other sector's map does not name this one back |
| Sectors with geometry named only from their own region | 5: sector 237 of `level3s`, 109 of `level5s`, 4 of `level14s`, 11 and 128 of `level95s` |
| Parts | Every part of the 18 worlds holds a sector named from some region outside that part |

In `courtyard`, 38 of the 39 maps name no sector. The map of sector 38 names 16 other sectors and holds `0x02040803`
at offset `0x08`; `courtyard` has no sector records, so its maps tie to no part.

### Seen per level

Counts for the 18 `s` worlds. "Seen from a region" counts only regions whose map names at least one sector with
geometry; it gives the sectors with geometry named (the sector itself included) and the parts that hold them, as the
middle value over those regions and the largest.

| Level | Title | Sectors | With geometry | Parts | Regions naming geometry | Sectors seen from a region, median / most | Parts seen from a region, median / most |
| --- | --- | ---: | ---: | ---: | ---: | --- | --- |
| `level2` | Real Heavy Rep | 124 | 48 | 24 | 54 | 24.5 / 48 | 14.5 / 24 |
| `level3` | Writer's Block | 393 | 112 | 30 | 135 | 24 / 90 | 7 / 28 |
| `level5` | Adios Amigo | 228 | 67 | 23 | 67 | 18 / 63 | 8 / 23 |
| `level11` | Sharp Dressed Man | 163 | 71 | 30 | 76 | 21 / 70 | 12 / 30 |
| `level14` | Set Up | 111 | 54 | 21 | 57 | 23 / 44 | 13 / 21 |
| `level31` | Boys In Blue | 107 | 40 | 20 | 44 | 23 / 40 | 13 / 20 |
| `level34` | Blackout | 174 | 68 | 23 | 71 | 13 / 66 | 6 / 23 |
| `level51` | Desperate Dudes | 312 | 95 | 39 | 106 | 24 / 64 | 12 / 33 |
| `level52` | No Permits, No Parley | 276 | 75 | 26 | 80 | 23 / 71 | 11 / 26 |
| `level80` | Real Live Bunch | 99 | 48 | 19 | 50 | 17 / 38 | 9 / 18 |
| `level82` | Roots | 171 | 82 | 29 | 91 | 27 / 79 | 13 / 29 |
| `level83` | Heavy Muscle | 228 | 103 | 46 | 105 | 24 / 97 | 15 / 46 |
| `level86` | Payin' The Cost | 64 | 31 | 14 | 35 | 26 / 31 | 13 / 14 |
| `level87` | Payback | 113 | 56 | 24 | 68 | 22.5 / 40 | 12 / 21 |
| `level92` | The Best | 183 | 73 | 30 | 83 | 29 / 71 | 16 / 30 |
| `level93` | Destroyed | 152 | 63 | 23 | 67 | 17 / 62 | 9 / 23 |
| `level95` | none | 246 | 78 | 32 | 89 | 24 / 73 | 14 / 32 |
| `level99` | New Blood | 142 | 47 | 20 | 49 | 18 / 39 | 8 / 19 |

- Regions from which every sector with geometry is named: one in `level2s`, two in `level31s`, four in `level86s`;
  none in the other 15 worlds.
- Regions naming exactly one sector with geometry: 20 in `level99s` (each a sector with geometry naming only itself),
  none in `level31s`, 54 in `level3s`.
- In `level99s`, part 1 (`level99s_ms1.sec`, sectors 92, 102 and 136, geometry indices 29, 32 and 44) is named from 24
  of the 49 regions; part 7 is named from the fewest (11) and parts 10 and 20 from the most (25).

### Seen from the story starts

The region holding each story start position (ref-levels page, mapped to (x, z, -y) as under Coordinates) and what
its map names in the `s` world.

| Level | Entry | Region of sector | Its geometry index and part | Sectors with geometry seen | Parts seen | Parts not seen |
| --- | --- | ---: | --- | ---: | ---: | --- |
| `level99` | 1 | 11 | 4, part 18 | 25 of 47 | 14 of 20 | 1, 2, 5, 8, 12, 13 |
| `level99` | 2 and 3 | 84 | 24, part 2 | 25 of 47 | 11 of 20 | 3, 4, 6, 7, 9, 11, 15, 18, 19 |
| `level34` | 1 | 83 | 22, part 1 | 52 of 68 | 22 of 23 | 7 |
| `level34` | 2 | 151 | 50, part 8 | 27 of 68 | 13 of 23 | 10 others |
| `level34` | 3 and 5 | 148 | 47, part 5 | 39 of 68 | 18 of 23 | 3, 10, 15, 17, 22 |
| `level34` | 4 | 159 | 56, part 4 | 21 of 68 | 9 of 23 | 14 others |
| `level2` | 1 | 28 | 5, part 15 | 28 of 48 | 15 of 24 | 9 others |
| `level2` | 2 | 48 | 12, part 2 | 40 of 48 | 21 of 24 | 1, 10, 14 |
| `level2` | 3 | 109 | 39, part 6 | 19 of 48 | 11 of 24 | 13 others |
| `level2` | 4 | 118 | 45, part 6 | 8 of 48 | 4 of 24 (6, 11, 14, 19) | 20 others |
| `level31` | 1 and 4 | 20 | 10, part 13 | 23 of 40 | 14 of 20 | 1, 7, 8, 9, 16, 20 |
| `level31` | 2 | 88 | 33, part 4 | 36 of 40 | 19 of 20 | 20 |
| `level31` | 3 | 84 | 31, part 3 | 25 of 40 | 14 of 20 | 1, 2, 8, 12, 18, 20 |
| `level31` | 5 | 41 | 16, part 9 | 18 of 40 | 10 of 20 | 10 others |
| `level31` | 6 | 62 | 23, part 11 | 16 of 40 | 9 of 20 | 11 others |

At the tutorial's checkpoints 2 and 3 all 31 parts are resident (see Streaming), while the map of the region holding
the start names 11 of the 20 `s` parts.

## Fog colour

Each level script sets the fog colour with `SetFogColor` (three values from 0 to 1 for red, green and blue). The
fog colour is also the colour behind the world (see the PS2 display page). Each value times 255, with the fraction
dropped, gives the colour byte: the tutorial's 0.05, 0.05, 0.02 gives (12, 12, 5), the colour seen in play.

| Level | Title | Values | Colour bytes |
| --- | --- | --- | --- |
| `level99` | New Blood | 0.05, 0.05, 0.02 | 12, 12, 5 |
| `level80` | Real Live Bunch | 0.07, 0.05, 0.06 | 17, 12, 15 |
| `level87` | Payback | 0.012, 0.018, 0.035 | 3, 4, 8 |
| `level34` | Blackout | 0.02, 0.02, 0.02 | 5, 5, 5 |
| `level2` | Real Heavy Rep | 0.05, 0.05, 0.02 | 12, 12, 5 |
| `level3` | Writer's Block | 0.02, 0.02, 0.02 | 5, 5, 5 |
| `level5` | Adios Amigo | 0.04, 0.025, 0.057 | 10, 6, 14 |
| `level81` | Encore | 0.012, 0.018, 0.035 | 3, 4, 8 |
| `level86` | Payin' The Cost | 0.07, 0.05, 0.06 | 17, 12, 15 |
| `level93` | Destroyed | 0.012, 0.018, 0.035 | 3, 4, 8 |
| `level31` | Boys In Blue | 0, 0, 0 | 0, 0, 0 |
| `level14` | Set Up | 0.02, 0.02, 0.04 | 5, 5, 10 |
| `level9` | All-City | 0, 0, 0 | 0, 0, 0 |
| `level51` | Desperate Dudes | 0.027, 0.025, 0.01 | 6, 6, 2 |
| `level52` | No Permits, No Parley | 0.07, 0.05, 0.06 | 17, 12, 15 |
| `level54` | Home Run | 0.012, 0.018, 0.035 | 3, 4, 8 |
| `level55` | Friendly Faces | 0.07, 0.05, 0.06 | 17, 12, 15 |
| `level84` | Come Out To Play | 0.105, 0.17, 0.195 | 26, 43, 49 |
| `level82` | Roots | 0.012, 0.018, 0.035 | 3, 4, 8 |
| `level92` | The Best | 0.07, 0.05, 0.06 | 17, 12, 15 |
| `level83` | Heavy Muscle | 0.02, 0.02, 0.02 | 5, 5, 5 |
| `level20` | Scout's Honor | 0.07, 0.05, 0.06 | 17, 12, 15 |
| `level11` | Sharp Dressed Man | 0.025, 0.025, 0.025 | 6, 6, 6 |
| `level95` | none | 0.05, 0.05, 0.02 | 12, 12, 5 |
| `level60` to `level64` | Stage 1 to Stage 5 | 0, 0, 0 | 0, 0, 0 |
| All 33 Rumble arenas with a level script | various | 0, 0, 0 | 0, 0, 0 |

- `level1` and `level100` set no fog colour.
- In `level84_end.lua` the fog colour changes during the closing scenes, to 0, 0, 0 and back to 0.105, 0.17, 0.195.
- The chapter scripts `level9_chap2_tags.lua` and `level9_chap3_escape.lua` set 0, 0, 0 again.
- `SetFogDistance` appears with the value 0 in the scripts of `level3`, `level5`, `level11`, `level51`, `level52`,
  `level55`, `level80`, `level82`, `level83`, `level84`, `level86`, `level92` and `level93`, and with 0.5 in
  `level9`.

## Rain

Rain is the only weather in the game's data. A script starts it with `StartRain`, which sets two rain layers, each
with its own sprite and colour. `CfgRaindrops` sets the colour of the drops. `EndRain` stops it.

| Level | Title | Rain settings in its scripts | Layer colours (red, green, blue, alpha) | Drop colour |
| --- | --- | ---: | --- | --- |
| `level51` | Desperate Dudes | 6 | 190, 190, 190 with alpha 50 to 90, and 190, 190, 190 with alpha 80 to 160 | 255, 255, 255, 64 |
| `level82` | Roots | 6, the same as `level51` | as `level51` | 255, 255, 255, 64 |
| `level93` | Destroyed | 3, the same as three of `level51` | as `level51` | 255, 255, 255, 64 |
| `level61` | Stage 2 | 1, in `level61_hihats.lua` | 200, 200, 250, 100 and 200, 200, 240, 100 | 140, 140, 140, 140 |
| `level64` | Stage 5 | 1, in `level64_riffs.lua` and `level64_cyrus.lua` | as `level61` | 140, 140, 140, 140 |
| `level118` | Tack's Warehouse | 1, in its level script | 200, 200, 250, 60 and 200, 200, 240, 100 | none set |

Three more scripts whose names were not recovered start the same rain as `level61`. Drifting fog is a particle
effect and is on the particles page. No script on the disc sets a time of day or changes the sky.

## Xbox version

The Xbox archive keeps the streamed world under `ee_files\sectors\`, with one `.xlev` file per level instead of a
pair of worlds, and `.xsec` files numbered from 0. It holds 79 `.xlev` and 1,969 `.xsec` entries; `level99` has
one `.xlev` of 4,274,688 bytes and 34 `.xsec` files, against 31 listed parts in the PS2's two tutorial worlds. The
Xbox archive has no `courtyard` or `objarena` world. The Xbox layouts were not studied for this page.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0040f8a0` | Choose the world part to stream in or out each frame | Streaming |
| `0x00411d10` | Find the sectors a player view can see | Visibility |
| `0x0040c868` | Set the fog colour | Fog colour |

## Acceptance checks

- The PS2 archive holds 160 world streams; `level99s_sec.wld` is 811,848 bytes and begins with the part count 20.
- `level99s` has 142 sectors, 47 of them with geometry, spread over 20 parts; `level99d` has 68 sectors, 27 with
  geometry, over 11 parts.
- In every world stream with a manifest, the leading part count equals the manifest's part count.
- In every world, the geometry indices run from 0 without gaps, and each sits in the part its sector record names.
- The header hash of every listed part file equals the CRC-32 of its lower-case file name.
- `level99s_ms1.sec` holds three geometry pieces, with geometry indices 29, 32 and 44.
- The tutorial's `d` world textures include graffiti, fences and vegetation, and its `s` world textures include
  brick walls, doors and road surfaces.
- The tutorial's background and fog colour is (12, 12, 5), and in Come Out To Play it is (26, 43, 49).
- It rains in Desperate Dudes, Roots, Destroyed, Stage 2, Stage 5 and the Rumble arena Tack's Warehouse.
- When the camera turns quickly toward a stretch of street whose geometry is not yet loaded, that stretch appears
  over about one second, fading in.
- A player start position (x, y, z) from the level list lies inside the `s` world's box at (x, z, -y).
- The 18 `s` worlds with visibility data hold 3,286 sectors; every sector's map names the sector itself, and the
  maps of 1,327 of them name at least one sector with geometry.
- No visibility map names another sector that has no geometry.
- In `level99s` each map is 18 bytes long, and the region holding New Blood's entry 1 start (sector 11) names 25
  sectors with geometry in 14 of the 20 parts; parts 1, 2, 5, 8, 12 and 13 are not among them.
- In `level99s`, `level99s_ms1.sec` holds sectors named from 24 of the 49 regions whose map names geometry.
- No `d` world and no `s` world outside the 18 listed under Visibility carries plugin `0x12A`.

## Open questions

- How close a sector seen in a view must be to come first in line. The executable's figures give about 10.7 units at
  a draw distance of 115; not seen in play.
- What `SetFogDistance` 0 and 0.5 change on screen; the tutorial, which sets no fog distance, shows a fog plane at
  half the far distance.
- Whether the `d` world's parts follow the visibility of their own sectors in play; not observed.
- The remaining `StartRain` values (two small whole numbers and two fractions per layer) and the sprite each layer
  uses are not tied to what the player sees.
- A level can also stream a water surface through the same queue; which levels have one was not studied.
- Why 347 part files exist that no world lists, and whether any build of the game used them.
- None of the streaming or visibility outcomes was measured in play for this page; they come from the executable
  and the disc.
- The Xbox `.xlev` and `.xsec` layouts, and whether the Xbox draws the same two layers.
- What is drawn of the `s` world while the camera is in one of the 1,959 regions whose map names no sector
  with geometry, and whether the camera enters such regions in play; not observed.
- Whether a camera in the region of one of the 20 `level99s` sectors whose map names only itself sees only that
  sector's geometry of the `s` world in play.
- What the map of `courtyard` sector 38 means, with its value `0x02040803` at offset `0x08`, and whether `courtyard`
  is shown anywhere in the game.
- Which region holds a camera that lies exactly on a plane between two regions.
- Whether the Xbox world files carry visibility maps for the same 18 levels.
- With two player views, which camera's position the second view's visibility is read from.
