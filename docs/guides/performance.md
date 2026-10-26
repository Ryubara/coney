# Performance

How fast Coney runs, how to measure it, and what we found. The game is a 2005 PS2 title, so a modern machine should
draw a crowd of a hundred fighters with room to spare; this page is where the measurements that say whether it does
are kept, with the build and the machine each came from. Everything here is Coney's own debug tooling: the profiler
only measures, nothing in the game reads it, and a run measured with it does exactly the same steps as one without.

## Measuring a run {#bench}

`--bench FILE` measures a `--play-level` run and writes a summary as JSON at the end; `--bench-fighters N` spawns N AI
fighters in front of the player on the level's first frame of play, as the debug menus'
[AI fighters page](debug-menu.md#pages) does (rows of five from 4 m ahead, each model drawn at random from a fixed
seed, more than 50 placed as several clicks' worth one behind the other); `--bench-warmup N` leaves the first N frames
of play unmeasured (60 by default) while the crowd settles. `--frames` counts from the first frame of play, as always,
so the bench measures `frames - warmup` frames:

```sh
build/playtest/src/platform/coney --disc /path/to/warriors.iso --play-level level95 --skip-movies --frames 660 \
    --bench ../../scratch/bench-100.json --bench-fighters 100 --vsync off --no-activate
```

It prints one line (`bench: 100 fighters, 600 frames: frame 27.10 ms mean, ...`) and the report holds the frame time's
mean, median, 95th and 99th percentile and maximum, the same for every [zone](#zones) and [counter](#counters), and
what was run: the level, the build type, headless or not, vsync and the OpenGL renderer. A bench is a test-mode run:
lockstep, one step and one render a frame, no frame cap, so two builds measured with the same command did the same
work and only the times differ. Run it windowed with `--vsync off` (a frame then takes as long as the work, not the
display's refresh) to see the whole cost, or `--headless` (librw's NULL renderer, so nothing is drawn or skinned) to see
the simulation's alone.

`coney-tools perf bench` runs the bench once per fighter count (0, 10, 25, 50 and 100 by default) in the hub,
`level95` (free play from the first frame: in `level99` the opening scene holds the crowd back), from the `playtest`
build and prints a Markdown table of them, keeping each report and log in `--out`
([coney-tools perf](coney-tools-build.md#perf)):

```sh
uv run --project python coney-tools perf bench --out ../../scratch/perf --fighters 0,10,25,50,100
uv run --project python coney-tools perf bench --out ../../scratch/perf --headless
```

**Measure the right build.** The `dev` preset is a Debug build, several times slower than an optimised one and not
what a player runs; measure the `playtest` preset (`RelWithDebInfo`: optimised, with debug information and the debug
tools) and say so. The `release` preset is the player's build: it has no debug tools, so it refuses `--bench`
([The debug tools and the game](building.md#debug-tools)). The report's `build` field names the build type.

**Measure on a quiet machine.** The times are real time on a shared computer. Numbers taken while other builds run
are noisy (on the agents' machine a dozen builds may be running): check `coney-tools build status` first, run each
count more than once, and record which numbers were taken quiet.

## The in-game view {#overlay}

The debug menus' **Performance** page ([The debug menus](debug-menu.md#pages)) turns the profiler on and off
(*Measure*) and shows the frame time (plotted in the developer overlay), the [scene counters](#counters) by kind on
one line (*Scene*) and every zone's and counter's mean over the last 60 frames. It works in any run, at any frame
rate; while *Measure* is off nothing is timed.

## Zones {#zones}

A zone is a named stretch of code timed with the real clock, `CONEY_PERF_ZONE("name")` at the top of a scope
(`src/core/perf.h`). Zones nest, each reports its own total for the frame (two places with one name add up), and they
sit at the level of a subsystem, not inside per-human loops, so measuring costs a few microseconds a frame. Core code
reads no clock: the platform installs one (`src/platform/perf_probes.cpp`, the only file besides the frame pacer and
the clip recorder that the `no-real-clock` check allows to), and with measuring off a zone costs one branch.

| Zone | What it covers |
| --- | --- |
| `frame.begin` | the window's events, the debug menus' jumps, the event log, before the frame's steps |
| `step` | each fixed step's update of the mode on top (all of a frame's steps add up) |
| `play.update` | the play mode's step: everything below |
| `play.humans` | every human's update (`human::Humans::update`), the brains included |
| `humans.brains` | the AI's brains writing their humans' commands (think, goals, actions, steering) |
| `humans.animate` | every human's animation tasks |
| `humans.state` | every human's state update: the move, the sweep against the level and the other humans, strikes |
| `humans.actions` | every human's actions, with the targets gathered for each |
| `play.ai_capture` | the AI fighters' pose snapshots for drawing |
| `play.anim_overrides`, `play.scenes`, `play.world` | the scripts' clip overrides, the scenes, the scenery and world objects round the camera |
| `render` | the frame's render, the present included |
| `render.skin` | every human posed: its bone matrices (GPU skinning), or every vertex skinned and written (CPU) |
| `render.scene` | the scenery's draw, with the humans, cars and objects among it |
| `render.humans` | every human's atomics drawn (librw instancing and uploading the skinned vertices, then drawing) |
| `render.shadows_rings` | the blob shadows and health rings |
| `present` | the 2D layers (HUD, effects, menus), the debug overlays and the buffer swap |
| `present.debug_overlays` | the pad menu and the developer overlay |
| `present.swap` | the buffer swap: the time the driver makes the CPU wait for the GPU shows here |

## Counters {#counters}

A counter is a running total sampled at the end of each frame; the report gives how much it grew per frame.

| Counter | What it counts |
| --- | --- |
| `gl.draws` | OpenGL draw calls (librw's and Coney's, not Dear ImGui's, which has its own loader) |
| `gl.triangles` | triangles those draws made |
| `gl.uploads`, `gl.upload_bytes` | buffer uploads (`glBufferData` with data, `glBufferSubData`) and their bytes |
| `gl.programs`, `gl.textures` | shader program switches and texture binds: state changes |
| `alloc.count`, `alloc.bytes` | C++ heap allocations on every thread (a counting `operator new`) |
| `scene.<kind>.draws`, `.triangles`, `.vertices` | the same draws split by what was drawn, `<kind>` one of `scenery` (the streamed world, the skyline), `humans` (their bodies), `objects` (world objects, doors, cars, glass, carried props), `effects` (water, particles, glints, blob shadows, health rings, the throw arc) and `other` (the 2D layers, menus, movies, debug lines); vertices are what the draws sent, an indexed draw's index count |

The scene counters are kept by the same wrappers: the play mode's render marks each part of the frame with the kind
it draws (`platform::DrawKindScope`, `src/platform/perf_probes.h`), and a draw counts as the innermost kind open, else
`other`. The Performance page's **Scene** line sums them a frame, `scenery 92.4k tris 160.2k verts 410 draws; humans
...`, and a bench report has them as every counter. With a [mesh view](debug-menu.md#mesh-views) on, its passes are
draws too and count with the model's kind.

The OpenGL counters wrap the functions librw's loader fills in (`glad_glDrawElements` and the rest), so librw itself is
unchanged. The allocation counter replaces the global `operator new` in the executable and the tests; librw's and SDL's
own allocations use `malloc` and are not counted.

## GPU skinning {#gpu-skinning}

In play and in scenes every human is skinned in the vertex shader (`src/platform/character_skin.*`): a librw GL3
object pipeline of Coney's own uploads a character's vertices once, with each vertex's four bone indices and weights,
and per frame only its bone matrices change (at most 64, in the character's space). Its lighting, fog,
texture levels and alpha test are those of Coney's default pipeline (`src/platform/texture_lod.*`), and the blended
normal is normalised as the CPU skinning normalised it, so a human looks as it did. Before, every human's every vertex
was blended on the CPU each frame, written into two librw geometries (the second for the blood layer) and uploaded
again whole.

The placement in the world (the lean, the heading, the feet and RenderWare's axes) is the atomic's frame on both
paths, so it moves the blended vertex whole. Folded into each bone matrix instead, it would move a vertex by the sum
of its weights, and the stored weights sum a hair below 1 (the decoder clears the bits that hold the bone,
[Character geometry](../research/characters.md#character-geometry)): on the disc, 71,380 of the 150,509 distinct
vertices fall short, by 1.9 × 10⁻⁵ on average and 9.2 × 10⁻⁵ at most, which would pull a human towards the world's
origin by up to a centimetre a hundred metres out.

The bounding sphere that chooses a human's lights and fades it out at a distance comes from the box round each skin
node's vertices as the node's matrix moves it, and round the feet (`posedBox()`), which holds every vertex and is a
little larger than the vertices' own box (**Coney's choice**: the original's choice of sphere is not traced). Where the
pipeline cannot start (OpenGL 2.1 or ES 2, a model with more than 64 skin nodes), the mesh skins on the CPU, blending
each vertex straight into librw's geometry. The character viewer and the reference images skin on the CPU.
`--cpu-skinning` skins every human by that CPU path in any run, to compare the two paths' pictures and times in one
build; it is not the per-vertex skinning that came before GPU skinning.
`platform::characterSkinOn()` says which path new meshes take, and `CharacterMesh::gpuSkinned()` which one a mesh took.

The two paths draw the same picture as the skinning that came before them. On 2026-10-25, `playtest` builds of the
change and of the `main` it starts from took the same frames at 640 × 448: a close-up of the player in `level99` with
the view pinned, and the hub with 25 fighters in motion at frame 200. GPU skinning and `--cpu-skinning` drew both
frames identically, pixel for pixel. Against `main`'s build, the close-up was identical too, and the hub frame had 1
of 286,720 pixels different, by one level of one channel.

## Findings {#findings}

Taken on 2026-10-25 with `coney-tools perf bench` in the hub from two `playtest` builds: the change that brought
GPU skinning (with and without `--cpu-skinning`) and the `main` it starts from (`516482455`, CPU skinning). Windowed,
vsync off, a Ryzen 7 9800X3D with an RTX 5080 (NVIDIA driver 610.88). The machine was **not quiet**: two to four
other builds and test runs held build slots, and the processor's load read 93 to 100 %. The table gives the median
frame of each run (the means carry the other builds' stalls), as the median of three repeats, the three builds
interleaved. The zones are medians too.

| Fighters | Frame, `main` (ms) | Frame, `--cpu-skinning` (ms) | Frame, GPU skinning (ms) | Frames a second, `main` / GPU | `render.skin`, `main` / GPU (ms) | Uploads a frame, `main` / GPU (KB) |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 6.00 | 5.91 | 2.93 | 167 / 341 | 3.20 / 0.34 | 535 / 19 |
| 10 | 9.22 | 9.97 | 3.49 | 108 / 287 | 5.61 / 0.59 | 891 / 28 |
| 25 | 12.36 | 11.69 | 4.09 | 81 / 244 | 8.71 / 0.97 | 1,329 / 31 |
| 50 | 22.67 | 17.88 | 5.31 | 44 / 188 | 16.03 / 1.59 | 2,058 / 36 |
| 100 | 37.43 | 37.56 | 7.79 | 27 / 128 | 28.30 / 2.87 | 3,480 / 46 |

- **Skinning.** GPU skinning makes the hub with 100 fighters about 4.8 times faster than `main`. The change's own CPU
  path (`--cpu-skinning`) is as fast as `main`'s within this machine's noise. What is left of `render.skin` is the work
  each human's pose takes on the CPU (its bones' transforms, the skinning matrices and the palette), about 25 µs a
  human.
- **The empty hub.** At 0 fighters the hub still has its own 14 humans. Of the 2.93 ms, `step` is 1.86 ms, and nearly
  all of that is `humans.brains` (1.54 ms). `render` is 0.99 ms (`render.scene` 0.64 ms, `render.skin` 0.34 ms), and
  `present.swap` 0.34 ms. The floor is CPU time spent in the AI's brains, not the GPU, the 2D layers or waiting for
  the swap.
