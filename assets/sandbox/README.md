# Sandbox assets

Coney's own test worlds: the sandbox layouts (`*.layout`) and the textures they use. The sandbox is Coney's feature,
not part of the original game; how to run it and the layout format are in
[the sandbox guide](../../docs/guides/sandbox.md). Nothing here comes from the game.

## Layouts

| File | What it is |
| --- | --- |
| `default.layout` | The general movement course: slopes, stairs, ledges, gaps, a room and corridors |
| `combat.layout` | The fight yard: passive targets to fight (`target` lines), one in front of a wall |
| `parkour.layout` | The traversal course: fences, low walls, climb blocks, jump gaps, drops and a run-up lane |

## Textures

The textures are from **Prototype Textures 1.0 by Kenney** ([www.kenney.nl](https://www.kenney.nl)), released under
Creative Commons Zero (CC0 1.0); Kenney's licence is in `License.txt`. Thanks to Kenney for them.

| File | Kenney's file |
| --- | --- |
| `dark_grid.png` | `PNG/Dark/texture_01.png` |
| `light_grid.png` | `PNG/Light/texture_01.png`, re-encoded from a 2-bit palette to 8-bit RGB (librw cannot read 2-bit PNGs); the pixels are unchanged |
| `orange_grid.png` | `PNG/Orange/texture_01.png` |
| `green_grid.png` | `PNG/Green/texture_01.png` |
| `purple_grid.png` | `PNG/Purple/texture_01.png` |
| `red_grid.png` | `PNG/Red/texture_01.png` |
| `light_checker.png` | `PNG/Light/texture_08.png` |

Each is a 1024x1024 tile; a layout maps one tile to one metre unless it says otherwise.
