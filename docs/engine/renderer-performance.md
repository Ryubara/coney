# Renderer performance

The benchmark, the baseline measured with it and the frame-time budgets of the [renderer design](renderer.md): the part
of that page that holds the numbers, split out to keep it short. The items that the budgets hold are in the renderer's
[Order](renderer.md#order); the tools are `coney-tools perf bench` and `coney --bench`
([Performance](../guides/performance.md#bench)).

## Benchmark {#benchmark}

`coney-tools perf bench` and `coney --bench` ([Performance](../guides/performance.md#bench)) are the benchmark, with
named scenes, GPU time and a comparison (built, [Scenes](../guides/performance.md#scenes)):

```sh
uv run --project python coney-tools perf bench --scene hub,hub-crowd,street --out ../../scratch/perf
uv run --project python coney-tools perf bench --scene all --compare ../../scratch/perf-baseline --out ../../scratch/perf
```

- **Scenes** are a table in the tool (name, level, checkpoint, camera, fighters, frames, render size), so a scene is
  the same work on every run: a lockstep test-mode run with a pinned camera and a fixed seed. Each scene runs three
  times, and the report's `meta` records the render size and the window beside the level.
- **`--compare FOLDER`** prints each scene's median frame time beside the baseline's and the change in percent, and
  exits 1 when a scene is slower than the budget allows, on a quiet machine only ([Budget](#budget)).
- **GPU time.** A `gpu.frame` measure from GPU timestamp queries (OpenGL timer queries, built; the SDL3 GPU renderer has
  none, as SDL3's GPU API has no timer queries, [GPU time](../guides/performance.md#gpu-time)), so a report shows
  whether a frame is bound by the CPU or the GPU.
- **`--display-preset` and `--renderer`** are passed through to Coney, so every display preset and renderer is
  measured the same way (`--preset` stays the build's CMake preset).

## Baseline {#baseline}

Measured with `coney --bench` (lockstep, 600 frames after 60 of warm-up, `--vsync off`, the window 960 × 720 unless
a render size is named) from a `playtest` build (optimised, with the debug tools that `--play-level` needs) of `main`
at `b749a7dca`, with humans skinned on the GPU, on the agents' Windows machine: an AMD Ryzen 7 9800X3D, an RTX 5080,
OpenGL 3.3 through the NVIDIA driver. Each scene ran three times, the scenes interleaved, and every run is counted: a
cell is the median of the three runs' means, with the slowest and fastest run in brackets. Times are milliseconds a
frame; Posing is `render.skin`, the bones posed on the CPU for the GPU's skinning. Draws and triangles are a frame's
mean, the same in every run of a scene. Other agents' builds held the machine's CPU at 67-100 % during most runs, so
some brackets are wide.

| Scene | Fighters | Render size | Frame | p95 | Step | Render | of which posing | Present | Draws | Triangles |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Hub (`level95`) | 0 | 960 × 720 | 3.14 (2.73-3.21) | 3.50 (3.26-4.04) | 1.85 (1.70-1.91) | 1.17 (0.97-1.23) | 0.35 (0.32-0.36) | 0.19 (0.15-0.21) | 411 | 72,756 |
| Hub, crowd | 50 | 960 × 720 | 7.02 (6.66-8.95) | 9.48 (8.68-17.20) | 3.69 (3.49-4.70) | 3.18 (3.02-4.07) | 1.76 (1.75-2.08) | 0.38 (0.26-0.81) | 540 | 128,796 |
| Street (`level99`, checkpoint 1) | 0 | 960 × 720 | 1.86 (1.82-4.34) | 2.34 (2.26-16.04) | 0.24 (0.24-0.57) | 1.52 (1.49-2.97) | 0.33 (0.32-0.47) | 0.22 (0.21-1.07) | 693 | 77,582 |
| Hub | 0 | 640 × 448 | 3.36 (3.19-6.38) | 4.14 (3.74-20.17) | 1.99 (1.90-2.93) | 1.29 (1.21-3.25) | 0.36 (0.35-0.49) | 0.24 (0.21-1.51) | 411 | 72,756 |
| Hub | 0 | 2560 × 1920 | 3.22 (2.98-4.87) | 3.78 (3.49-6.00) | 1.88 (1.79-2.36) | 1.21 (1.13-2.41) | 0.35 (0.34-0.39) | 0.20 (0.18-1.31) | 411 | 72,756 |
| Hub, headless | 0 | none | 1.86 (1.80-2.02) | 2.18 (2.02-2.74) | 1.80 (1.74-1.96) | none | none | none | none | none |
| Hub, crowd, headless | 50 | none | 3.15 (3.04-3.25) | 3.68 (3.48-3.85) | 3.04 (2.91-3.11) | none | none | none | none | none |

The reports, one JSON and one log per run with the command line (render size included) and the processor's load at
the log's top, and the script that made the table, are kept in the scratch folder of this design's task.

What it says:

- **The frame is bound by the CPU, in the step and around librw.** With GPU skinning on GL3, the hub's frame is 3.1
  ms, of which the step is 1.85 ms (nearly all the AI's brains,
  [Performance: findings](../guides/performance.md#findings)) and the render 1.17 ms. With 50 fighters the step is
  3.7 ms and the render 3.2 ms, of which 1.8 ms is posing the humans' bones.
- **The simulation costs 1.8-3 ms** in the hub (the headless runs) and 0.24 ms in the street, and does not grow with
  the frame rate, since steps are fixed. It is now the largest share of the frame, which the simulation thread
  ([Simulation and drawing threads](renderer.md#threads)) takes off the frames that draw.
- **The GPU is barely used.** In the hub, 640 × 448 and 2560 × 1920 (17 times the pixels) have the same median frame,
  3.36 and 3.22 ms, within each other's brackets. There is room for per-pixel lighting, shadow maps and AA.
- **About 410-690 draw calls** a frame through librw's one-atomic-at-a-time pipelines; instancing and fewer state
  changes on the new renderer bring that down, but at this count draws are not the limit.

[Order](renderer.md#order) item 16 records this again on the scene table with GPU time, on a quiet machine, and that
record becomes the baseline `--compare` reads; the figures above are the starting point it replaces. The scene table,
the GPU time and the comparison are built ([Scenes](../guides/performance.md#scenes)); the recording is not yet
([#1383](issue:1383)), since it needs a machine no other build is using: `coney-tools perf bench --scene all --out
../../scratch/perf-baseline` on a quiet one, the table pasted here and the folder kept for `--compare`. Until then
`--compare` has no folder to read and refuses. The items that draw through GL3 (1 to 10 and 15 to 19) must keep these
numbers (the *PS2* budget below); the GPU renderer's items are measured against them.

## Culling before posing {#cull-before-posing}

Posing a human (his bone transforms, skinning matrices and posed bounding sphere, `render.skin`) costs about 25-30
µs of the main thread. `PlayLevelMode::render` used to pose every AI human and only then let the 70 m distance fade
(`PlayLighting::drawHumanPasses`, [Drawing a human](../research/graphics.md#human-draw)) or the camera's clipping drop
him. Since [#1554](issue:1554) it first tests a sphere that holds any pose of his (`graphics::cullHuman()`: 3 m round a
point 1 m above his feet, times his scale when it is over 1) against the view window's four sides and the camera's
back, then against the draw distance plus the radius:

- **Out of view or past the draw distance:** he is not posed and not drawn (his mesh's `culled` flag; the Mesh group
  reads 0 passes for him). His blob shadow and health ring are still queued at his blended feet, as before: each is
  its own draw, clipped by the camera.
- **Wearing, holding or carrying something** (a hat, a held object, a carried prop): the distance test is skipped and
  only the view test applies, since what he carries is placed among the world objects and is not faded with him.
- **The picture is the same.** The test is conservative: any human whose body could reach the picture or come nearer
  than 70 m is posed as before. Only the AI fighters are culled; player 1, the sandbox's targets and a scene's
  puppets are few and are always posed.

**What it saves.** The sweep's brawl of 1000 fighters in `level95` (500 enemies, 500 allies, rows of 25, lockstep,
960 × 720, a `playtest` build of `main` at `c0285b415` and of the change, the main thread's CPU time, the median of two
interleaved runs): with the view pinned on the crowd `render.skin` goes from 27.3 to 15.7 ms, the render
from 34.4 to 21.2 ms and the draws from 1389 to 1158; with the follow camera `render.skin` from 27.4 to 13.1 ms, the
render from 34.5 to 17.9 ms and the draws from 1290 to 931. The last frame's screenshot hashes the same before and
after with either camera. The draw distance it tests is `graphics::humanDrawDistance()`, the value the fade reads
too, so a render-side draw distance moves both together.

**Code**: `cullHuman()` in [human_lighting.h](repo:src/graphics/human_lighting.h), the fighters' loop in
[play_level_mode.cpp](repo:src/platform/play_level_mode.cpp). It came from the performance sweep's experiment 8
([#1542](issue:1542)).

## Posing on workers {#posing-on-workers}

After the cull, posing the fighters left in the picture still cost the main thread about 15 ms at a brawl of 1000.
Each fighter's posing is independent of the others', so since [#1553](issue:1553) `PlayLevelMode::render` poses
them in three passes:

1. **On the main thread, in the fighters' order:** each is culled ([above](#cull-before-posing)) or given a job: his
   mesh, his character's skeleton and model, his scale, and his pose, a scene's or his two steps' snapshots to blend.
2. **On the worker threads** ([Worker threads](render-threads.md#workers)): each job's arithmetic,
   `prepareHumanPoses()`: his blend, bone transforms and skinning matrices, then `CharacterMesh::preparePose()`, which
   on GL3's skinned pipeline fills his bone palette and works out his posed box, and on the CPU blends his vertices
   into the mesh's own buffers. Nothing here calls librw or writes what another job reads.
3. **On the main thread, in the fighters' order again:** `commitHumanPose()` places his librw atomics and gives them
   the sphere (or the CPU's vertices), then what he wears, holds and carries is posed and his blob shadow and ring are
   queued, as when one thread did everything.

`CharacterMesh::pose()`, which player 1, the sandbox's targets and a scene's puppets still use, is `preparePose()`
then `commitPose()`, so the arithmetic is the same on either path, and the results do not depend on how many threads
ran it. **What the simulation reads is unchanged:** the hats, held objects and carried props that read the drawing
pose (#1557) are posed in pass 3, in the same order and from the same pose as before.

**What it saves.** The same brawl of 1000 as the cull's (500 enemies, 500 allies, rows of 25, lockstep, 960 × 720,
`level95`), a `playtest` build of `main` at `a1368987d` and of the change in the same build folder, the main thread's
CPU time, the median of three interleaved runs, on a 16-thread machine (a pool of 8 threads): with the view pinned on the
crowd `render.skin` goes from 15.3 to 4.0 ms (the sweep measured 3.9 ms with the cull, [#1542](issue:1542)) and the
render from 21.1 to 9.5 ms; with the follow camera `render.skin` from 13.0 to 3.6 ms and the render from 18.0 to
8.5 ms. The draws are the same (1158 and 931), and the last frame's screenshot hashes the same before and after with
either camera. The all-humans trace, the event log and the lockstep digest hash the same too (a brawl of 300 for 500
steps, an engaged 300 for 700 steps, headless and in a window).

**Code**: [human_posing.h](repo:src/platform/human_posing.h), `CharacterMesh::preparePose()` and `commitPose()` in
[character_mesh.h](repo:src/platform/character_mesh.h), the fighters' loop in
[play_level_mode.cpp](repo:src/platform/play_level_mode.cpp). It came from the performance sweep's experiment 9.

## Budget {#budget}

Frame-time budgets on this machine, each scene's median of three runs, checked by `perf bench --compare` (built; it
reads the baseline item 16 records). They are targets for the work in [Order](renderer.md#order), not results:

| Preset and renderer | Scene | Budget | Why |
| --- | --- | --- | --- |
| *PS2*, GL3 | every scene | no slower than the baseline's median by more than the larger of 5 % and the baseline's own spread (its slowest run over its fastest) | the reference must not get slower while the seam is built; the spread term keeps the gate wider than run-to-run noise |
| *PS2*, SDL3 GPU | Hub, crowd (50) | 6.9 ms (144 frames a second) | the step's 3.0 ms (the headless runs, which the busy machine disturbed least) leaves 3.9 ms to draw, against today's 3.6 ms of render and present on GL3 |
| *PS2*, SDL3 GPU | Hub | 4.2 ms (240 frames a second, this display's rate) | the step's 1.85 ms leaves 2.35 ms, against today's 1.17 ms of render and 0.19 ms of present |
| *PS2*, SDL3 GPU | Street | 2.1 ms (about 480 frames a second) | Street's step is 0.24 ms; today's render is 1.52 ms and present 0.22 ms, so 1.85 ms to draw keeps a margin and does not let the draw grow into the hub's budget |
| *Enhanced*, SDL3 GPU, 2560 × 1440 | Hub, crowd (50) | 6.9 ms | shadow maps, per-pixel lights and SMAA fit in the GPU time the baseline leaves idle |
| *High*, SDL3 GPU, 3840 × 2160 | Hub, crowd (50) | 8.3 ms (120 frames a second) | the most expensive preset at the largest common size |

**The gate's condition.** `--compare` fails a pull request only on a quiet machine: no other build holding a build
slot while it runs, which the tool checks and prints. On a busy machine it reports the comparison and does not fail,
because the runs above show a busy machine spreading one scene's runs by more than 130 %.

A low-end target (an integrated GPU, such as this machine's own AMD Radeon graphics) is measured once the new renderer
draws: *Enhanced* holds 60 frames a second there or becomes opt-in, as the
[Realtime lighting design](../guides/realtime-lighting.md#risks) says.
