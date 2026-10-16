# In-game radar

The radar of the PS2 release (NTSC-U disc `SLUS-21215`): the round map in the lower right of the screen, its map
sheets, its icons, its range and its tint. Values come from the PS2 archive `WARRIORS.WAD` (entries named by
`WARRIORS.DIR`), the compiled Lua scripts `global.lua` and `level99.lua` inside it, the executable's data, and ten
PCSX2 savestates of the first mission (`level99`, on foot). Screen positions are in pixels of the 640 x 480 savestate
screenshots and are read by eye. Sprite sheets and the sprite page layout are described on the HUD page.

## Radar on screen

| Property | Value |
| --- | --- |
| Place | Lower right; in level 99 a disc centred near (545, 375) with a radius of about 55 pixels |
| Map | A part of the level's map, drawn in light grey (vertex grey 0x8F) |
| Player | A red triangle at the centre |
| Disc | Built of 32 segments; the inner map disc has 0.825 of the outer ring's radius |
| Other marks seen in level 99 | A white dot and a yellow round marker with a black `W` (the objective marker) |
| Split screen | A second radar shows only in split screen |
| Letterbox (cut-scene bars) on | Both radars are off |

## Map sheets

Each level with a radar map has a sprite sheet named after the level (for example `level99`), holding one 256 x 256
texture and a 48-byte sprite page with a single rectangle covering the whole texture (u and v from 0.0009766 to
0.99902, the usual quarter-texel inset). The page's word at 0x08 is 0xFFFFFFFF. The sheets are stand-alone archive
entries and also sit inside the level packs; the `level99` sheet is entry 2,611 and also sits in entries 9,649 to
9,651.

38 of the names `level0` to `level130` have a map sheet.

| Texture depth | Chunk 0x2A size | Levels | Count |
| --- | --- | --- | --- |
| 8-bit | 66,960 | 2, 4, 5, 7, 9, 11, 20, 23, 31, 34, 51, 54, 55, 80, 82, 87, 93, 103, 104, 111, 114, 121, 122 | 23 |
| 4-bit | 33,264 | 3, 14, 52, 81, 83, 84, 86, 92, 95, 99, 106, 107, 108, 112, 120 | 15 |

## Map placement

Each level has a map origin (x and z in metres) and a map width in metres that tie the map texture to the level's
ground plan. For level 99 the values in memory are an origin of x -1.33, z 61.55 and a width of 95 m.

## Range and zoom

| Property | Value |
| --- | --- |
| Near range in level 99 | 50 (both radars, all savestates) |
| Far range in level 99 | 75 (both radars, all savestates) |
| Zoom in level 99 | 1.0 (both radars) |
| Range standing still | The near range, times the zoom |
| Range at speed 12 or faster | The far range, times the zoom |
| Range between those speeds | In proportion to the speed, between the near and far range |
| Change of range | Gradual, never a jump; the rate value is 0.0004 per millisecond |
| Blip placement | A blip at the edge of the range sits 0.12 screen units from the centre |
| Blips beyond the range | Held 0.108 screen units (0.9 of 0.12) from the centre, in their own direction |

## Blips

The radar holds up to 128 blips at once. Blip icons are rectangles of the `part_page0` sheet (see the HUD page).

| Rectangle | Size in texels | Use |
| --- | --- | --- |
| 25 | 16 x 16 | Arrow icon |
| 26 | 16 x 16 | Arrow icon |
| 27 | 13 x 13 | Objective marker (set by `global.lua`) |
| 28 | 13 x 13 | Objective marker (set by `global.lua`) |
| 356 | 11 x 11 | A gang member a level script adds to the radar |
| 22 | 25 x 27 | Not confirmed |
| 34 | 34 x 34 | Not confirmed |
| 350 | 7 x 12 | Not confirmed |
| 351 | 9 x 12 | Not confirmed |
| 355 | 11 x 11 | Not confirmed |
| 360 | 16 x 12 | Not confirmed |
| 361 | 19 x 12 | Not confirmed |

| Property | Value |
| --- | --- |
| Arrow icon sizes | 0.026, 0.03 and 0.035 screen units |
| Other people a level script adds | Grey blips, colour 80 80 80 FF |
| Objective blip on another floor | The blip changes icon while its target is more than 1.0 m above or below the player and within 20 m (sum of the x and z distances) |
| Script use in level 99 | `level99.lua` adds people to the radar |

## Radar tint

The radar's colour moves between three tint states, blending over 0.5 s. The states are a table of RGBA values in
the executable's data at 0x0050e9e8.

| State | Red | Green | Blue | Alpha | Look |
| --- | --- | --- | --- | --- | --- |
| 0 | 191 | 191 | 191 | 240 | Grey |
| 1 | 100 | 120 | 200 | 240 | Blue |
| 2 | 100 | 50 | 50 | 140 | Dim red |

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x001c3de0` | Set up the radar for a level | Map placement |
| `0x001c5210` | Place the blips on the radar each frame | Blips |
| `0x001c60b0` | Draw the radar | Radar on screen |
| `0x001b4168` | Add a person to the radar | Blips |

## Acceptance checks

| Outcome |
| --- |
| The PS2 archive entry 2,611 holds a sheet named `level99` with a 256 x 256 4-bit texture and a 48-byte sprite page |
| 38 levels between `level0` and `level130` have a map sheet; 23 are 8-bit and 15 are 4-bit |
| Every map sheet's sprite page holds one rectangle covering the whole texture |
| Rectangles 25 and 26 of `part_page0` are 16 x 16 sprites and rectangles 27 and 28 are 13 x 13 |
| In the first mission the radar is a disc in the lower right of the screen with the player as a red triangle at its centre |
| The objective's yellow `W` marker shows on the radar and over its place in the world |
| The radar shows more of the map while the player runs than while the player stands still, and changes smoothly |
| A target outside the radar's range stays on the radar, near its edge, on the side the target lies |
| No radar is drawn while the cut-scene letterbox bars are on screen |

## Open questions

- The source of each level's map origin and width: they were not found in the Lua scripts, the executable's data,
  `level99.lev`, `warriors.glr` or `scene_list.cnk`.
- The unit of the speed that sets the range (12 is likely metres per second), the unit of the range itself and of
  its rate of change.
- Which situation sets each tint state; tints 1 and 2 seem tied to the player being knocked down or cuffed.
- Which icon shows for an objective above and which for one below the player, and what rectangles 22, 34, 350, 351,
  355, 360 and 361 mark.
- Which kinds of people a level script adds as gang-member blips and which as grey blips.
- What the white dot seen in level 99 marks.
- Where the radar sits and how large it is in screen units, and how split screen and the wide-screen setting change it.
- Differences between the PS2 and Xbox radar have not been looked at.
