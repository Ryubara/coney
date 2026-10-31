# Engine: simulation and drawing threads

The part of the [renderer design](renderer.md) that splits a frame's work between two threads, so that drawing keeps an
even pace at a high display rate. It is a design: nothing here is built yet, and its items (37 to 42) are in the
renderer's [Order](renderer.md#order).

## The plan {#threads}

Today one thread does a frame's work in order: wait out the frame cap, read the window's events, run the 0 to 4 steps
the real time calls for, draw once and present (`GameModeStack::runUntilEmpty`). A frame that runs a step takes the
step's time on top of its drawing, so at a high display rate the frames that carry a step come late and the others do
not. **The plan:** the fixed 1/30 s simulation runs on a thread of its own, and the main thread only draws, at the
display's rate, from a **snapshot** that each step publishes. What the simulation computes does not change; what
changes is the thread that runs it and what drawing may read.

## What it gains {#threads-gain}

The gain is an even frame time, not more frames a second. A step costs about 1.9 ms in the hub, about 3-3.7 ms with
50 fighters ([Baseline](renderer.md#baseline)) and about 4 ms with 100 in free roam (the frame-spike study in
[Performance](../guides/performance.md)); drawing costs about 1.4-3.6 ms (the Baseline's render plus present: 1.4 ms in
the hub, 1.7 in the street, 3.6 with 50 fighters).

| Frame | One thread (today) | Two threads |
| --- | --- | --- |
| A frame with no step | draw: 1.4-3.6 ms | draw: 1.4-3.6 ms |
| A frame with a step (one in eight at 240 frames a second) | step and draw: 3.3 ms in the hub, under the 4.2 ms of a 240 Hz refresh; 7.3 ms with 50 fighters, over it, so 30 frames a second miss theirs | draw only; the step runs on another core |
| A slow step (a first model load: 28-41 ms) | that frame is late by the whole step: 7 to 10 refreshes at 240 Hz | no frame is late; the motion waits only if the step outruns the drawing delay ([The drawing clock](#threads-clock)) |
| Frames a second, uncapped | the steps take about 6 % of each second in the hub, 11 % with 50 fighters | that much more |

At 60 frames a second, and at 144 with vsync, a 4 ms step fits inside the frame, so the split changes little there
until a step runs long. At 240 Hz with a crowd and on VRR displays it removes the judder that each step causes today,
and at any rate it removes the hitch of a slow step. What it costs: a second core kept busy, a snapshot built each
step, up to one step of extra lag, and a developer overlay that waits for the step in progress while it is open.

## The snapshot {#snapshot}

**What it holds** is the step's half of the frame description ([Migration](renderer.md#migration), step 4; items 5 and 7
to 10): for every drawn thing, its id, its mesh and material ids and its pose on this step (a transform; a human's bone
matrices; a world object's `DrawnPoses` entry), with a mark when it jumped (a teleport, a new clip) so that it is drawn
unblended; the cameras; the lighting frame (item 19); the particles, with their positions on this step and the one
before (item 17); the screen effects' levels (the blur pulse, fades, washes, the tint); the 2D layers' lists (HUD,
menus, the pad menu, text; a movie's frame by reference to a decoded-frame buffer); the debug draw's lines; and an
**outbox** of requests for the platform (rumble, cursor capture, quit). It holds ids, never pointers into the simulation
([Network architecture: checklist](../guides/network-architecture.md#checklist)).

**What it does not hold** is what drawing works out per frame: the blend of two snapshots by alpha, culling, the lights
chosen for each atomic from the blended positions, sorting, and the renderer's own objects.

**Who publishes it:** the simulation thread, as each step's last act after the mode's update. Drawing never writes it.
A frame reads two, the newest published step and the one before, and blends them by alpha as `Interpolated<T>` and
`DrawnPoses` blend previous and current today. The snapshots live in a ring of four slots (the two the frame is
drawing, the newest published, and the one being written), so the writer never waits for drawing, and publishing is
one atomic store of the newest slot's index.

**One thread first.** Item 37 makes every mode's `render()` read only its two snapshots while everything still runs on
one thread, so the change is proved by item 1's draw-stream hashes before a second thread exists. Test mode builds and
reads snapshots the same way, so it runs the code the threaded game runs.

## What stays on the main thread {#threads-main}

- **The window and its events.** SDL reads events and changes the window only on the thread that started its video
  (macOS requires it), so the main thread reads them every frame and hands input on ([Input](#threads-input)).
- **Every librw call.** librw is not thread-safe (its engine state, the current texture dictionary and the list of
  dirty frames are globals), and on its GL3 platform creating a raster and drawing need the OpenGL context, which is
  current on the main thread. Today the play modes create a human's librw mesh and textures inside the step that
  spawns it (`CharacterMesh`, built in `src/platform/play_level_*.cpp`). Item 38 moves that to drawing: the step names
  a model id in the snapshot, and drawing creates the librw objects before their first draw. The simulation's own
  data for the model (skeleton, animation, collision) stays in the step. The SDL3 GPU renderer keeps the rule: it
  acquires its swapchain on the window's thread.
- **Dear ImGui**, which builds its frame from the main thread's events and is drawn by the renderer.

## Input {#threads-input}

The main thread reads the window's events into the input source every frame, as today, and the source keeps every
press until a step takes it. The simulation thread samples the pads at the start of each step (`samplePads`), taking
the source's state under a short lock, so each press is seen by exactly one step, as today. Which step a live press
lands on depends on real time with one thread or two; what makes runs repeatable is that a step's input is recorded
and replayed by step number (`--input-script`, `--record`, a network client's commands), and that path does not
change. Sampling more often than once a step is not planned
([Input sampled at the display rate](../guides/network-architecture.md#sixty-input)).

## The drawing clock {#threads-clock}

`FrameClock` moves with the steps to the simulation thread; the frame pacer stays on the main thread. Drawing then
chooses its own time: the drawn game time is the real time less one step (today's lag) less a **drawing delay**, and a
frame blends the two snapshots either side of it.

- **Why a delay.** A step published 4 ms after it fell due leaves drawing nothing new to blend towards for those 4 ms.
  Today's frame that runs a step already shows its picture the step's time late, since it draws after the step; a
  delay near the usual step time therefore adds no lag against those frames, and makes the lag the same in every
  frame.
- **How long.** The delay follows the recent steps' cost (the slowest step of the last second, with a margin),
  between none and one step, and changes slowly, so the drawn time never runs backward or jumps. A player may cap it
  to favour latency (a `Display` value, render-side).
- **A step slower than the delay.** Drawing holds the newest snapshot at alpha 1 and never extrapolates
  ([Frame rate](renderer.md#frame-rate-gaps)); frames keep presenting, and the delay grows. Once it has, a step up to
  its 33 ms costs no frame; a single spike beyond the delay of the moment shows as the motion pausing for the spike's length,
  not as frames missed. A step slower than a whole step drops time as today (`kMaxStepsPerFrame`).

## Streaming {#threads-streaming}

The first human of a model not yet loaded costs 28-41 ms inside its step today
([Performance](../guides/performance.md)). With the split, the step's share is hidden by the drawing delay, but the
librw share moves to drawing (item 38), where it would make a frame late. Item 43 adds a loader thread that reads and
decodes model and texture files into Coney's own formats ([Migration](renderer.md#migration), step 5) off both threads;
drawing uploads what it decoded. A step that needs a model's simulation data waits for it inside the step, so what
spawns and when stays the same and the wait is the step's own time; the step asks early where it knows (a spawner's
models, a level's start). Test mode loads at once, on its one thread. How the original streams a spawned human's model
is studied first, in the first-model-load issue drafted by the frame-spike study; item 43 follows its findings.

## Debug tools {#threads-debug}

- **The pad menu**, its navigator and its actions run on the simulation thread in the step, as the mode stack's input
  does; what it draws is in the snapshot's 2D lists, and so are the debug draw's lines.
- **The developer overlay** (Dear ImGui, [Debug menu](../guides/debug-menu.md#one-model)) runs on the main thread, and
  its panels read and edit live state (the Entity inspector, the tunables, time control, the Lua console, the native
  caller). Item 39 gives it a **step-boundary lock**: the simulation thread holds it while a step runs and releases it
  between steps, and the overlay holds it while it calls the model's getters, setters and actions. The panels then
  read and change state as today, only between steps. While the overlay is open a frame may wait for the step in
  progress, so the even frame time is promised with the overlay closed.
- **Actions that change the game** (Lua, natives, teleport) may instead queue to run at the next boundary, as network
  play needs anyway ([Network architecture: debug tools](../guides/network-architecture.md#sub-debug)). Time control's
  pause, single step and speed act on `FrameClock` under the same lock.

## Audio {#threads-audio}

The mixer already works across two threads: its game side queues commands without locking and SDL's audio thread
mixes (`src/audio/mixer.h`), and only one thread may be the game side. That side becomes the simulation thread, since
every `GameSound` call comes from a step; a call from the overlay goes through the boundary lock. Test mode mixes
offline on its one thread, unchanged.

## Test mode, lockstep and networked play {#threads-modes}

- **Test mode** (`--headless`, `--frames`, `--screenshot`, `--record`, `--input-script`, the disc tests) stays on one
  thread: the option is refused there, and a run gives the same steps, state and pixels bit for bit, as today.
- **Lockstep** (`--fps-cap 30`, and a live cap of 30 from the debug menus) runs one step and one draw at alpha 1, which
  a thread cannot speed up, so it runs on the main thread; changing the cap live parks the simulation thread at a step
  boundary.
- **Networked play.** The network runs on the simulation thread, receiving before each step and sending after it, so
  a client's command leaves as soon as it is sampled ([Input commands](../guides/network-architecture.md#input)). A
  client draws from snapshot pairs whether they come from its own steps or from the server's snapshots through the
  jitter buffer ([Interpolation](../guides/network-architecture.md#interpolation)): the snapshot is drawing's one
  input either way. A dedicated server draws nothing and has no second thread.

## How tests prove nothing changed {#threads-tests}

- **The same simulation.** `frame_rate_test.cpp` gains a threaded case: the same scripted input by step number, the
  simulation on its own thread fed made-up frame times, and each step's state hash matching lockstep bit for bit at
  30, 60, 144, 240 and uncapped, with irregular frames.
- **The same drawing.** Item 37 leaves item 1's draw-stream hashes unchanged at alpha 1, and a unit test blends two
  synthetic snapshots and gets what each `Interpolated<T>` and `DrawnPoses` helper gives.
- **No live state in drawing.** `render()` is handed its two snapshots and the renderer, and nothing that reaches the
  simulation, so a mode that reads live state does not compile.
- **ThreadSanitizer.** A `tsan` build preset (Clang on Linux and macOS; MSVC has none) and a CI job beside the `asan`
  one run the threaded tests and a threaded headless run on librw's NULL device: a few thousand steps of scripted
  input with a test client taking the boundary lock. SDL3 and librw are built from source in Coney's build, so they
  are instrumented too. The job is a required check (a maintainer decision): its key in `merge.required_checks` of
  `.github/conventions.yml` lands with it (item 41).
- **The gain, measured.** Item 16's scenes run in real time at a 240 frames-a-second cap and report the frame time's
  p99 and the frames a minute over the display's period, with and without the thread. Item 42 makes the thread the
  default only when those frames fall and the median frame does not rise.
