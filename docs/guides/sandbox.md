# The sandbox

The sandbox is a set of test worlds made of simple shapes for trying out movement: slopes, stairs, ledges, fences,
gaps and drops of known sizes, drawn with prototype grid textures and baked lighting, the way a level designer's
blockout looks in Unity or Unreal. It is **Coney's own feature, not part of the original game**: nothing in it is
reimplemented, and nothing in it comes from the disc. Its sizes cite the research where a value comes from the game;
every other size is a Coney choice.

You can use it two ways:

- **fly round it** with a free camera, with no disc at all (`--sandbox`);
- **play in it** as Rembrandt, with the same player, follow camera and character code as
  [playing a level](building.md#playing-a-level) (`--play-level sandbox:NAME`). This needs the disc, for the character.

## Running it

```sh
build/dev/src/platform/coney --sandbox                       # the default course
build/dev/src/platform/coney --sandbox parkour               # another layout, by name
build/dev/src/platform/coney --sandbox ../my-test.layout     # or a layout file anywhere
build/dev/src/platform/coney --sandbox parkour --frames 3 --input-script fly.txt --screenshot ../../scratch/parkour.png

build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox                       # the default course
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox:parkour --spawn lane  # at a spawn point
```

`--sandbox` takes a layout's name (`default` when none is given) or a path to a `.layout` file. Names are looked up in
the `sandbox` folder of Coney's assets: `assets/` beside the executable, which every build copies from the
repository's `assets/`, or the folder `--assets DIR` names. An unknown name fails with the list of known ones.

In the flying mode the camera starts at the layout's first viewpoint and is driven by pad 1, with the same controls as
[the world viewer](building.md#the-world-viewer), plus:

| Pad | Keyboard | Does |
| --- | --- | --- |
| triangle | I | jump to the layout's next viewpoint |
| square | J | jump to the previous viewpoint |

In the play mode Rembrandt starts at the layout's first spawn point, or at the one `--spawn NAME` names, dropped onto
the ground below it. The controls are those of [playing a level](building.md#playing-a-level). Both modes take
`--headless`, `--frames` and `--input-script` and print a summary when they stop (counts and positions only).

From any run, [the debug menu](debug-menu.md)'s Levels page plays a layout by name, and in the play mode its Player
page teleports to the spawn points, its Spawner adds objects in front of Rembrandt and its Debug draw page shows the
collision mesh.

## The shipped layouts

They live in `assets/sandbox/` with the textures; that folder's README lists them.

| Name | What it has |
| --- | --- |
| `default` | The general movement course: a slope gallery from 10° to 60°, stair sets with rises from 10 to 40 cm, ledges from 10 cm to 3 m, platforms with gaps from 0.5 to 4 m, a room with a doorway, corridors from 0.6 to 2 m wide, a measuring strip and a person-sized capsule for scale |
| `parkour` | The traversal course: fences from 0.6 to 3 m, kerbs and low walls from 0.2 to 1.65 m, climb blocks from 1 to 3.5 m, a raised run of platforms with gaps from 1 to 8 m, drop towers from 2 to 14 m with stairs up, jump-up blocks from 0.5 to 2 m, and a 120 m run-up lane with a mark every metre and a post every 5 m. The fences, walls and blocks sit on both sides of each climb threshold (0.69, 1.7, 2.5 and 2.91 m); its spawns `lane`, `fences`, `walls`, `climbs`, `gaps`, `towers` and `blocks` each start in front of one |

The values from the research that the courses are built round (all from
[Characters](../research/characters.md#ground) unless noted):

| Value | Where it shows |
| --- | --- |
| The ground snap climbs a step of up to 1.0 m | stairs and ledges: higher ledges are red |
| Wall faces under 0.25 m tall do not stop a walking body; taller ones do ([walls and steps](../research/characters.md#walls)) | stairs and ledges: in the original the 10 and 20 cm risers and ledges are walked up, the 30 cm and higher are walls |
| A drop of more than 0.5 m is a fall | gaps, drops and ledges |
| A character lands only on ground whose normal has z > 0.65 (about 49°) | the slope gallery: the 50° and 60° ramps are red |
| Gravity 15.68 m/s²; a landing hurts from 14.9 m/s (about 7.1 m) and kills from 20.5 m/s (about 13.4 m) | the drop towers bracket both heights (inferred from the speeds) |
| Walk 1.63, jog 4.86, run 7.80, sprint 10.25 m/s ([speed classes](../research/characters.md#speed-classes)) | the run-up lane's marks |
| A jump leaves at 5.5 m/s up at the run or sprint speed and rises 1.06 m; it lands 6.4 m on at a run ([jumping](../research/characters.md#jump)) | the platform gaps and jump-up blocks: in the original a 1 m block is about the limit |
| Climbs: short fence or short wall for an obstacle from 0.69 to 1.7 m, fence (below 2.5 m) or wall (top 1.7 to 2.91 m) above that ([climbing](../research/characters.md#climb)) | the fences, low walls and climb blocks |

The climbs follow two forward rays at 0.69 and 1.7 m above the feet and a downward probe 0.4 m behind the face: a
fence has nothing to stand on just behind it, a wall has a top inside the window
([Climbing](../research/characters.md#climb)). In the original only triangles of material 30 (`LOW_FENCE`) or with
flag bit 2 (`0x4`, players) or bit 7 (`0x80`) are climbable ([Collision](../research/collision.md#triangles)); a
primitive's `flags=` sets those bits raw.

What the courses show today. Coney's walking body is the original's: a sphere of 0.34 m whose bottom is 0.05 m above
the feet, and a wall triangle under 0.25 m tall is not a wall ([walls and steps](../research/characters.md#walls)).

- **Default course**: Rembrandt walks up the 20° ramp and the 20 cm stairs onto their 2 m platforms. He walks onto
  the 10 cm ledge and the 25 cm one (its 3 m wide face is two thin triangles the same rule skips), and the 50 cm
  ledge stops him 0.34 m short of its face. `tests/sandbox/disc_sandbox_player_test.cpp` checks these runs.
- **Parkour course**: L2 with the stick past 0.95 sprints until stamina runs out; triangle jumps from a run or a sprint
  and climbs the fences, low walls and blocks inside the windows. The 20 cm kerb is walked onto, and the 30, 50 and
  65 cm ledges are walls: too low to climb, so only a jump gets onto them. `tests/sandbox/disc_sandbox_traversal_test.cpp`
  checks a run past each threshold, with the input scripts `tests/support/parkour_*.txt`
  ([Characters](../research/characters.md#coneys-implementation) lists what each one shows).

Run with `CONEY_TRACE=1`, the traversal test prints every frame: position, speed, stamina, clip and traversal state.

## The layout format {#the-layout-format}

A layout is a text file with one statement per line: a keyword, then `key=value` arguments, in any order. `#` starts a
comment. Units are metres and degrees, in the game's axes: z is up, and a heading or yaw of 0 faces +y, turning
counter-clockwise seen from above. A mistake fails the whole layout with its line number, for example
`line 12: size= is missing`.

```text
title My test

texture floor dark_grid.png          # the first texture is the default for every shape
texture wall light_grid.png

sun direction=-0.5,0.45,-0.74 colour=0.62,0.58,0.50
fog start=80 end=220

spawn start at=0,0,0 heading=0
view start at=0,-8,3 yaw=0 pitch=-12

box at=0,0,-0.5 size=60,60,0.5      # the ground, its top at z = 0
box at=5,0,0 size=0.2,4,1.2 texture=wall repeat=5 step=2,0,0
ramp at=-5,4,0 width=3 height=2 angle=30
stairs at=0,8,0 width=2.5 steps=10 rise=0.2 run=0.3
```

### Statements

| Statement | Arguments | Does |
| --- | --- | --- |
| `title TEXT` | | The name shown in the summary |
| `texture NAME FILE` | | A PNG in the layout's folder, used as `texture=NAME` |
| `sky` | `colour=r,g,b` | The background colour |
| `sun` | `direction=x,y,z colour=r,g,b` | The direction the sunlight travels, and its colour |
| `ambient` | `sky=r,g,b ground=r,g,b` | The light from above and from below |
| `fog` | `start= end=` | Linear fog; `end` is also the far clip |
| `occlusion` | `radius= strength=`, or `off` | Ambient occlusion: how far it looks and how dark it gets |
| `shadows` | `on` or `off` | Sun shadows |
| `tessellate` | `edge=` | The longest drawn edge; finer faces carry finer lighting |
| `spawn NAME` | `at=x,y,z heading=` | Where the player can start (`--spawn NAME`) |
| `view NAME` | `at=x,y,z yaw= pitch=` | A camera viewpoint for the flying mode |

A layout without a `spawn` gets one named `origin` at the origin. A layout without a `view` gets one named `start`,
6 m behind the first spawn and 3 m up.

### Shapes

Every shape is placed by `at=`, the middle of its footprint at its bottom.

| Shape | Arguments |
| --- | --- |
| `box` | `size=width,depth,height` |
| `ramp` | `width= height=` and `angle=` or `length=`; it rises along +y |
| `stairs` | `width= steps= rise= run=`; solid steps rising along +y |
| `cylinder` | `radius= height=`, optional `segments=` (24) |
| `capsule` | `radius= height=` (in all), optional `segments=` |
| `sphere` | `radius=`, optional `segments=` |

Every shape also takes these:

| Argument | Default | Does |
| --- | --- | --- |
| `yaw=` | 0 | A turn about its base point |
| `texture=` | the first texture | A texture's name, or `none` for plain colour |
| `uv=` | 1 | Texture tiles per metre; 2 makes the pattern half as big |
| `tint=r,g,b` | 1,1,1 | Multiplies the texture |
| `solid=` | `yes` | `no` draws it without collision (markers, decals) |
| `flags=` | 0 | Collision triangle flag bits, decimal or `0x...` ([Collision](../research/collision.md#triangles)) |
| `material=` | 5 (concrete) | Collision material ([Collision](../research/collision.md#materials)) |
| `area=` | 1 | Collision area byte |
| `repeat=N step=x,y,z` | 1 | N copies, each moved by `step` from the one before |

Textures are projected in world space at one tile per metre. On walls the grid measures the height above the ground,
so a 1.25 m fence shows one and a quarter tiles.

## Adding an experiment

1. Copy a layout in `assets/sandbox/` to a new `NAME.layout`, or write one anywhere and pass its path.
2. Add the shapes, a `spawn` beside them and a `view` that looks at them.
3. Fly to the view with `coney --sandbox NAME` (no disc needed), then play it with
   `coney --disc ... --play-level sandbox:NAME --spawn ...`.
4. For a check that runs every time, write an input script with partial stick deflections (as in
   `tests/support/sandbox_walk_forward.txt`). Then add a run to `tests/sandbox/disc_sandbox_player_test.cpp` (walking)
   or `disc_sandbox_traversal_test.cpp` (sprints, jumps, climbs) that prints positions and a hash only.
5. Note in the layout where each size comes from: a research page and anchor, or "Coney's choice".

The layout tests (`tests/sandbox/`) parse and build every shipped layout, so a new layout under `assets/sandbox/`
is checked on every run.

## How it is drawn

The sandbox is built once at start-up. Each shape is made into faces with world-space texture coordinates and
normals. The solid shapes' faces go into a collision mesh in the same form a level's collision is loaded into
([Collision](../research/collision.md#coneys-implementation)), so the player's ground snap, wall push and the follow
camera's rays run unchanged on it. Lighting is baked into the vertex colours:

- a sky and ground ambient;
- the sun's diffuse light, with shadows from rays cast towards the sun;
- ambient occlusion from 16 rays over the hemisphere.

The rays are cast against the shapes themselves, through a coarse grid. The renderer (librw) draws the result with
mipmapped, anisotropically filtered textures and linear fog. Nothing is lit at run time, so a frame costs one batch
of triangles. Both modes keep to [update and render](conventions.md#update-and-render): the steps move the camera or
the player, and each frame draws the camera blended between the last two steps.

## Open questions

Answered and moved into the text above: whether a 75 cm ledge stops the player (it does in the original, and so does
a 50 cm one, [walls and steps](../research/characters.md#walls)); which heights choose the climbs
([climbing](../research/characters.md#climb)); the climbable flags (bits 2 and 7 and material 30,
[Collision](../research/collision.md#triangles)); whether Coney's body should match the original's (it does now:
the lower sphere and the 0.25 m rule); and the parkour ranges (now on both sides of 0.69, 1.7, 2.5 and 2.91 m).

- **Runtime checks of the step rule**: a kerb and ledges of 0.3, 0.5 and 0.75 m in the game have not been walked into
  with the patched pad yet; the courses show Coney's reading of the code.
- **The camera at a fence climb**: the follow camera pulls in close while Rembrandt passes through a fence, because
  its ray hits the fence; what the original's camera does there is open
  ([Characters](../research/characters.md#open-questions)).

## Credits

The textures are Kenney's [Prototype Textures](https://www.kenney.nl/assets/prototype-textures), released under
CC0 1.0. Kenney's licence is in `assets/sandbox/License.txt`, and `light_grid.png` is re-encoded to 8-bit RGB
because librw cannot read 2-bit PNGs ([LEGAL.md](repo:LEGAL.md#licences)).
