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
build/release/src/platform/coney --disc /path/to/warriors.iso --play-level level95 --skip-movies --frames 660 \
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
`level95` (free play from the first frame: in `level99` the opening scene holds the crowd back), from the `release`
build and prints a Markdown table of them, keeping each report and log in `--out`
([coney-tools perf](coney-tools-pcsx2.md#perf)):

```sh
uv run --project python coney-tools perf bench --out ../../scratch/perf --fighters 0,10,25,50,100
uv run --project python coney-tools perf bench --out ../../scratch/perf --headless
```

**Measure the right build.** The `dev` preset is a Debug build, several times slower than an optimised one and not
what a player runs; measure the `release` preset (`RelWithDebInfo`: optimised, with debug information) and say so. The
report's `build` field names it.

**Measure on a quiet machine.** The times are real time on a shared computer. Numbers taken while other builds run
are noisy (on the agents' machine a dozen builds may be running): check `coney-tools build status` first, run each
count more than once, and record which numbers were taken quiet.

## The in-game view {#overlay}

The debug menus' **Performance** page ([The debug menus](debug-menu.md#pages)) turns the profiler on and off
(*Measure*) and shows the frame time (plotted in the developer overlay) and every zone's and counter's mean over the
last 60 frames. It works in any run, at any frame rate; while *Measure* is off nothing is timed.

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
| `render.skin` | every human skinned on the CPU and written into its mesh |
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

The OpenGL counters wrap the functions librw's loader fills in (`glad_glDrawElements` and the rest), so librw itself is
unchanged. The allocation counter replaces the global `operator new` in the executable and the tests; librw's and SDL's
own allocations use `malloc` and are not counted.
