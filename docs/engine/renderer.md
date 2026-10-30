# Renderer

How Coney draws, and the design for where its renderer goes next: high and uncapped frame rates, a modern graphics
API, per-pixel lighting and shadows, anti-aliasing and scaling, and a *PS2* preset that keeps today's picture for the
fidelity tests. **The sections from [Frame rate](#frame-rate) on are a design**: they say what is built today and
what is planned, and the planned work is the ordered list of issues in [Order](#order).

The rule this design follows: gameplay matches the PS2 build exactly, while presentation and performance may go past
what the PS2 and the Xbox could do, as options, with the PS2 look always selectable
([Faithful first](repo:AGENTS.md), a maintainer decision). Nothing on this page may change what the simulation
computes; every option is render-side only.

**Decided by the maintainers:** librw stays only as Coney's reader of RenderWare data (streams and the PS2 and Xbox
rasters); Coney's own renderer draws through SDL3's GPU API (Vulkan, Direct3D 12, Metal) ([Backend](#backend)). The
migration is prepared now, not left to the end: the seams that let the game draw without librw's GL3 device are the
first items of [Order](#order).

The option-by-option catalogue (what each graphics option means for *The Warriors*, its feasibility and cost) is the
[Enhancements plan](../guides/enhancements.md), and the lighting model for shadows and reflections over the game's
baked light is the [Realtime lighting design](../guides/realtime-lighting.md). This page owns the decisions those two
depend on: the frame-rate model, the backend, the split of the simulation and drawing into two threads, the presets
and their shadow source, the performance budget and the order.

## How Coney draws today {#today}

| Part | What it is | Where |
| --- | --- | --- |
| Window, input, audio | SDL3 (pinned in `cmake/deps.cmake`, zlib licence), linked statically | `src/platform/window.*`, `sdl_input.cpp` |
| Graphics API | librw's GL3 platform: an OpenGL 3.3 core context that librw's device makes through SDL3. librw is built for exactly one platform; headless runs swap in librw's NULL device at run time. `--renderer gpu` starts the GPU renderer's beginning instead ([The GPU renderer](gpu-renderer.md)): Coney's own window on SDL3's GPU API, frames cleared and presented, librw on its NULL device | `src/platform/render_engine.*`, `gpu_presenter.*` |
| 3D | librw atomics drawn through librw's default GL3 pipelines and shaders, with Coney's own plugins and render callbacks round them (the world's dual layer and MatFX reader in `world_atomic.cpp`; per-atomic lights in `scene_lighting.cpp`); humans skinned on the GPU by Coney's own librw GL3 pipeline (`character_skin.cpp`: each character's vertices uploaded once, a bone palette set each frame), with CPU skinning kept as a fallback and behind `--cpu-skinning` ([Performance: GPU skinning](../guides/performance.md#gpu-skinning)) | `src/platform/` |
| 2D | `graphics::RenderDevice` (`src/graphics/render_device.h`), Coney's own interface for quads and triangles in logical pixels; `RenderEngine` implements it with librw's Im2D | `src/graphics/`, `render_engine.cpp` |
| Screen passes | the motion-blur stand-in (`motion_blur_pass.cpp`), the blur pulse and the optional line blend (`RenderEngine::blurScreen`, `setLineBlend`), all by copying the back buffer into a texture | `src/platform/` |
| Debug overlay | Dear ImGui through its SDL3 and OpenGL 3 backends | `src/platform/imgui_*` |
| Timing | a fixed 1/30 s step; drawing apart from it and blended between the last two steps | [Update and render](../guides/conventions.md#update-and-render) |
| Measuring | `--bench` and `coney-tools perf bench`, with zones and OpenGL counters | [Performance](../guides/performance.md) |

How much of Coney talks to librw's drawing side, counted in `src/platform/` and `src/graphics/` (the count the
backend estimates below rest on):

| Kind of call | Files | Call sites |
| --- | --- | --- |
| `rw::SetRenderState` (librw's immediate render states) | 17 | 173 |
| `rw::gl3::` (the GL3 backend's own types and functions: Im2D and Im3D vertices, native rasters, texture binds, the skinning pipeline's buffers and shaders) | 10 | 133 |
| `rw::im2d::`, `rw::im3d::` (immediate-mode drawing) | 7 | 37 |
| Raster creation and locking | 7 | about 27 |
| OpenGL called directly (`gl...(`: copies, read-backs, state the device does not cover) | 9 | 35 |
| Camera begin, end and clear | 7 | about 18 |
| Pipeline hooks (`ObjPipeline`, instance callbacks, MatFX) | 6 | about 22 |
| Atomic and clump renders | 6 | about 11 |
| **Total** | **about 24 distinct files, about 8,600 lines** | **about 460** |

The first, second, third and fifth rows are exact `git grep` counts; the others are approximate, since their calls
take several forms. The GPU skinning pipeline (`character_skin.cpp`, 39 of the `rw::gl3::` sites) is the largest
single file among them.

About 40 other files use librw only for data: streams, geometry, frames, matrices, texture dictionaries. That side is
not in question in any option below; librw stays Coney's reader of RenderWare data.

## Frame rate {#frame-rate}

### Built

The simulation runs in fixed 1/30 s steps; each real frame runs 0 to 4 steps and then one render, blended between the
last two steps by `RenderTime::alpha` (`FrameClock`, `src/core/frame_clock.h`). Only the frame pacer reads the real
clock (`src/platform/frame_pacer.cpp`), holding frames to `--fps-cap` with precise sleeps; `--vsync on|off` sets the
swap interval. `--fps-cap 30` and test mode are lockstep: one step, one render at alpha 1. What is blended, and how,
is in [Update and render](../guides/conventions.md#update-and-render) and
[Drawing between steps](../research/rendering.md#interpolation).

The proof that gameplay is the same at every rate exists: `tests/gamemodes/frame_rate_test.cpp` runs the same
simulation at 30, 60, 144, 240 and 1000 frames a second and with irregular frames and compares it bit for bit, and
`tests/platform/disc_play_frame_rate_test.cpp` does the same through `--play-level` with the disc.

### Gaps {#frame-rate-gaps}

What still moves at 30 Hz, or behaves differently with the frame rate, above 30 frames a second:

| Thing | Today | Change |
| --- | --- | --- |
| Particles, debris sprites, glints, camera litter (`src/effects/`) | drawn as the newest step has them | keep each particle's position on the step before (set at birth to its birth position), blend in `render()` |
| Cars and their stereos | stand still in Coney | blend through `world_objects::DrawnPoses` when cars move |
| Motion blur | lays the last *frame* over at a fixed strength, so trails shorten as the rate rises | convert the per-step strength to a per-frame one, `s_frame = 1 - (1 - s)^(30 dt)`; with a velocity buffer later ([Enhancements](../guides/enhancements.md#lighting-and-post-processing)) |
| Blur pulse, screen fades and washes, the level tint (`effects::BlurPulse`, `graphics::ScreenFade`, `effects::ScreenTint`) | per-step values, drawn at the newest step | blend the level as an `Interpolated<float>` where it ramps |
| UV scrolls and texture animation (water, signs) | stepped | blend the offset with `lerpLooping` |
| HUD | discrete per step (meters, text) | stays discrete; only its fades blend |

**Frame pacing.** The pacer's sleep-to-a-cap stays the one clock. Two additions, render-side only: a *VRR* mode (vsync
off and a cap just under the display's refresh rate, for G-Sync and FreeSync displays, where the display follows the
frame; [Order](#order) item 18) and, on the new backend, the present mode chosen by the setting (`VSYNC`, `MAILBOX` for
uncapped without tearing, `IMMEDIATE`), which SDL3's GPU API offers directly ([Backend](#backend)). Interpolation draws
up to one step behind the newest state; that latency (at most 33 ms, and the original's own frame at 30 frames a second)
is kept, because extrapolating past the newest step would draw states the simulation never had. With the simulation
on a thread of its own, drawing may lag by a little more, bounded by one step ([The drawing clock](#threads-clock)).

**Tests.** Every gap closed above adds its case to `frame_rate_test.cpp`'s pattern: the simulation's state hashed at
30, 60, 144 and uncapped (frames of random length from a fixed seed) must match lockstep bit for bit, and the drawn
value must equal the newest step's at alpha 1. A render-side value that depends on the frame rate (the blur's
per-frame strength) is tested as a function: its effect over one simulated second is the same at 30, 60 and 144.

## Backend {#backend}

Whether to stay on librw's OpenGL 3.3 and extend its shaders, or draw through Direct3D 12, Vulkan and Metal: four ways
were weighed, and the maintainers chose (b). Sizes are one engineer's time: **M** 1-3 weeks, **L** 1-2 months, **XL**
more.

**What librw's design gives a new backend.** librw (MIT) has one `Device` per platform: a table of function pointers for
the camera's begin, end and clear, `setRenderState`, Im2D and Im3D, plus a `DeviceSystem` that opens and closes the
window. Each platform has its own object pipelines whose instance callback turns platform-neutral geometry into the
platform's vertex buffers and whose render callback sets uniforms and draws. At Coney's pin its GL3 platform is 5,832
lines of C++ (`src/gl/gl3*.cpp` and `src/gl/rwgl3*.h`, counted with `wc -l`, blank and comment lines included; `wdgl`
and the bundled `glad` left out) plus 325 lines of GLSL in 9 files (`src/gl/shaders/*.vert`, `*.frag`); its Direct3D 9
platform is 6,320 (the shared `src/d3d/d3d.cpp`, `d3ddevice.cpp`, `d3dimmed.cpp`, `d3drender.cpp`, `rwd3d.h` and
`rwd3dimpl.h`, with `d3d9*.cpp` and `rwd3d9.h`; the Direct3D 8 and Xbox files left out). Three parts of that design are
made for old APIs: render states are set one at a time between draws and flushed at the next draw (a Vulkan or D3D12
backend must hash them into pipeline-state objects at draw time, compiling new ones mid-frame unless every combination
is warmed first); Im2D and Im3D draw immediately (they need per-frame upload rings and descriptor sets); and shaders are
GLSL strings compiled at run time per platform. The pipelines draw one atomic at a time in a single camera pass and know
nothing of several passes, depth pre-passes, shadow casters, previous-frame transforms or jitter.

| | (a) Fork librw, add Vulkan or D3D12 platforms | (b) librw for data; Coney's renderer on SDL3's GPU API | (c) A third-party abstraction | (d) Raw Vulkan, D3D12 and Metal |
| --- | --- | --- | --- | --- |
| What it is | New `rw::vk` (and `rw::d3d12`) beside `gl3` and `d3d9`: a device, raster, pipelines and shaders for each, Coney carrying the fork | librw keeps reading RenderWare streams and PS2 rasters; Coney uploads meshes and textures itself and draws with its own passes through SDL3's GPU API, which runs on Vulkan, D3D12 and Metal | **bgfx** (BSD-2-Clause; GL, Vulkan, D3D11 and 12, Metal; own shader dialect and tools); **Diligent Engine** (Apache-2.0; GL, Vulkan, D3D11 and 12; its Metal backend is not open source); **The Forge** (Apache-2.0); **wgpu-native** or **Dawn** (WebGPU; MIT or Apache-2.0, BSD-3-Clause) | Three backends written by Coney against each API |
| Windows | Vulkan or D3D12 | D3D12 or Vulkan | all of them | D3D12 and Vulkan |
| Linux | Vulkan | Vulkan | Vulkan, GL | Vulkan |
| macOS | only through MoltenVK (Apache-2.0), or a third librw platform for Metal | Metal, native | Metal (bgfx, The Forge, WebGPU); MoltenVK for Diligent | Metal, a third backend |
| Machines with no Vulkan or D3D12 (old GPUs, some VMs) | the GL3 platform stays | the GL3 renderer stays as fallback while needed | bgfx and Diligent keep a GL backend | none |
| Coney's code that changes | the 133 `rw::gl3::` sites and 35 direct GL calls get a per-platform twin; the rest keeps calling librw | the ~460 drawing sites in about 24 files move behind Coney's own draw interface ([Migration](#migration)) | the same ~460 sites, against the library's API | the same ~460, three times below them |
| New code | a librw platform per API, larger than the GL3 one (memory, descriptors, synchronisation, pipeline cache): **XL per API** | Coney's renderer once, shaders once in HLSL compiled offline to SPIR-V, DXIL and MSL: **XL**, in steps that each land | the renderer once against the library, plus its build and shader tools: **XL** | three backends and their shaders: **XL × 3** |
| Risk | Coney maintains a fork of a library whose upstream is a small project with occasional commits; PSO hitches from immediate render states; the fork only reaches what librw's single-pass pipelines can express | SDL's GPU API is young (SDL 3.2 and later); it exposes no native device or command-list handles, so a vendor upscaler cannot be plugged in without patching SDL ([Upscalers](#upscalers)); no geometry or tessellation stages, no ray tracing | a large dependency with its own build and shader compiler; Diligent needs MoltenVK on macOS; bgfx's API has its own conventions for views and state | highest effort and three sets of bugs; only worth it for ray tracing |
| Per-pixel lighting, shadow maps | only inside librw's pipelines, one camera pass: shadow passes and light lists fight its model | yes, as Coney's passes | yes | yes |
| TAA, motion vectors | needs previous transforms and jitter that librw has no place for | yes: the blended steps already give previous poses | yes | yes |
| Uncapped rate without tearing | mailbox present only if the fork adds it | `MAILBOX` present mode built in | yes | yes |
| HDR output | per platform | HDR10 and scRGB swapchains built in | bgfx and Diligent: yes | yes |
| Vendor upscalers (DLSS, XeSS, FSR 3 SDK) | possible: the fork owns the native handles | not without a small SDL patch or a native-handle extension; FSR 1, Coney's own TAA and SMAA are shaders and need none | Diligent and The Forge expose native handles; bgfx through its internal-data calls | yes |
| Ray tracing | no | no (SDL's GPU API has none) | Diligent and The Forge: yes | yes |

**Decided by the maintainers: (b), Coney's own renderer on SDL3's GPU API, with librw kept for reading RenderWare
data.**

- **One API for all three systems, already in the build.** SDL3 is pinned and statically linked today; its GPU API
  (in the pinned 3.4.18: Vulkan, D3D12 and Metal; compute pipelines; MSAA up to 8×; BC7 textures; `VSYNC`, `MAILBOX`
  and `IMMEDIATE` present modes; SDR, linear and HDR10 swapchains) adds no runtime dependency. macOS gets Metal
  natively instead of a deprecated OpenGL 4.1 or MoltenVK.
- **A fork of librw does not reach the goal.** It buys the same APIs at the same size of work, but the result is still
  librw's single-pass, one-atomic-at-a-time model with immediate render states: every enhancement on this page (light
  lists, shadow passes, motion vectors, a post chain) would have to be forced through it, and Coney would maintain the
  fork alone. The enhancements need passes that Coney owns whatever the API.
- **The work is the same size in (b), (c) and (d) on Coney's side** (the ~460 sites move behind one interface), and (b)
  adds the least on top: no third-party renderer to learn, build and track, and one backend instead of three.
- **What (b) gives up** is native handles (vendor upscalers) and ray tracing. Both are optional extras on this page;
  [Upscalers](#upscalers) says how they would be reached, and either can be added later behind the same seam with a
  narrow native path, without changing the passes above it.

A local spike settled what the reading could not, and nothing from it lands: [Spike](#spike).

### What stays and what moves under (b) {#stays-and-moves}

Coney stays as it is above the draw path, and keeps growing on it. librw is not removed: only its drawing is
replaced.

| Stays (unchanged) | Moves (to Coney's draw interface) |
| --- | --- |
| librw reading RenderWare data: clumps and atomics (DFF), texture dictionaries (TXD), the streamed world's sectors, frames, geometry, skin and animation data, MatFX and the game's plugins | the files and about 460 call sites in the table [above](#today): render states, `rw::gl3::` types, Im2D and Im3D, camera begin and clear, raster creation, direct OpenGL |
| PS2 and Xbox raster decoding (librw's PS2 code already runs under the NULL device) | librw's instancing into OpenGL buffers, replaced by Coney's own upload of the same geometry |
| Collision, physics and the camera's ray tests: the game's own collision mesh, read and queried by Coney's code (`src/physics/`, [Collision](../research/collision.md)), which never touches librw | Coney's GL3 skinning pipeline (`character_skin.*`), ported as the GPU renderer's skinning shader from the same bones, weights and palette |
| Every game mode, the simulation, scripts, sound, input, the 2D `RenderDevice` interface | Dear ImGui's OpenGL backend, swapped for its `sdlgpu3` backend |

**Scaling, (a) against (b):**

| | (a) Fork librw | (b) Coney's renderer on SDL3's GPU API |
| --- | --- | --- |
| Features | bounded by librw's model: one camera pass, one atomic at a time, per-platform pipelines; every new technique is a change to the fork in each platform | Coney's passes: light lists, shadow passes, motion vectors, a post chain, GPU skinning, instancing, all in one place and written once |
| Platforms | one new librw platform per API (Vulkan, D3D12, and Metal or MoltenVK for macOS), each maintained | SDL3 maintains the Vulkan, D3D12 and Metal backends; Coney writes one renderer |
| Upstream | Coney carries a fork of librw and merges upstream by hand | librw stays unforked at its pin; SDL3 is followed by moving its pin |
| Who maintains what | Coney maintains librw's drawing code for every API it adds, and its own code above it | Coney maintains its renderer and shaders; librw's maintainers its data code; SDL's its GPU backends |

(a) scales to new APIs, but not to new features: it ends with today's renderer on modern APIs. (b) scales on both.

### Migration {#migration}

The game keeps drawing at every step; each step is one pull request or a short series ([Order](#order) has one item
per pull request), and the *PS2* preset's draw streams and pictures prove nothing changed
([How pictures are compared](#comparing)). The groundwork comes first, before any enhancement, so that the game can
draw without librw's GL3 device as early as possible:

1. **The proof** (items 1 and 2). The draw-stream hashes and the picture comparison, so every later step shows that
   the *PS2* picture did not move.
2. **A start without librw's GL3 device** (item 3, built). `--renderer gl3|gpu` (and `Display/Renderer` in the
   overrides file, applied at the next start): Coney makes the window through SDL3 itself, claims SDL's GPU device and
   swapchain with the present modes, and opens librw with its NULL device, as headless runs already do, so streams and
   rasters load and the game runs while the GPU renderer only clears and presents. From here on, librw's GL3 device is
   used only by the GL3 renderer. [The GPU renderer](gpu-renderer.md) says how it is built.
3. **The shader toolchain** (item 4). HLSL sources in `src/platform/shaders/`, compiled at build time by
   SDL_shadercross (zlib; with DXC and SPIRV-Cross, which the build fetches itself, see [Licences](#licences)) into
   SPIR-V, DXIL and MSL, embedded in the executable. A test compiles every shader for every target.
4. **The seam** (items 5 and 7 to 10). A backend-neutral description of a frame, in `src/graphics/` and testable
   headless: passes (scene, overlays, screen passes), and per pass a list of draws, each a mesh id, a material
   (texture ids, blend, alpha test, cull, depth, fog, dual layer, environment map), a transform, the chosen lights
   and, for a skinned mesh, its pose. The platform code that calls librw today builds this list instead, and a
   `Gl3Renderer` draws the list through librw exactly as today. The 2D side and the screen passes move first, then
   the world, objects and cars, humans, and effects, one pull request each. Each keeps the draw-stream hashes and
   passes the picture comparison; which draws the hash covers at each step is in
   [How pictures are compared](#comparing).
5. **Coney's mesh and texture formats** (item 6, built: [Mesh and texture formats](asset-formats.md)). Meshes
   are converted once from `rw::Geometry` into Coney's vertex format (position, normal, prelit colour, UVs, skin
   weights) and textures from the PS2 rasters into RGBA (BC formats join with the Xbox rasters), held by id. The GL3
   renderer may keep using librw's instancing; the GPU renderer uses these.
6. **The GPU renderer reaches parity** (items 11 to 14 and 20), each usable alone: the 2D layers (front end, legal
   screens, movies, HUD); Dear ImGui (its `sdlgpu3` backend is in the pinned Dear ImGui); the world (prelit colour,
   dual layer, fog), objects and cars; humans, skinned on the GPU; particles, coronas and the screen passes. Then
   each scene of the [benchmark](#benchmark) and the named `--play-level` frames are drawn by both renderers on the
   same machine and compared as [How pictures are compared](#comparing) says, with the cross-renderer tolerance;
   differences are listed. librw's GL3 output stays the reference until the switch.
7. **The switch** (item 21). When parity holds for every scene, `gpu` becomes the default and `gl3` the fallback for
   machines with no Vulkan, D3D12 or Metal. When the GL3 renderer retires, and with it librw's GL3 platform (librw
   would then be built for its NULL platform, data only), is a maintainer question ([Questions](#questions)).

Enhancements are written once, against the GPU renderer: a new option targets `gpu` only, and the GL3 renderer stays
the *PS2* picture. The presets, the benchmark, the frame-rate gaps and the lighting frame do not depend on the
backend, so they land beside the groundwork (items 15 to 19).

### Testing while it grows {#migration-testing}

The GPU renderer grows on `main`, never on a long-lived branch: from item 3 on it is in every build, chosen at start by
`--renderer gl3|gpu`, with GL3 the default until a maintainer gives the go-ahead for the switch (item 21). Each item
merges small, and a part the GPU renderer does not draw yet is simply missing from its picture, never a crash.

- **Both renderers in CI.** The unit tests build the frame description headless and need neither renderer. The
  software-picture job (item 2) draws each synthetic scene twice under Mesa, GL3 through llvmpipe and `gpu` through
  lavapipe (Vulkan) from item 11 on, and the Windows and macOS jobs build the GPU renderer and compile every shader
  for their targets (item 4). A smoke run starts `--renderer gpu` with `--frames` under lavapipe and checks that it
  presents.
- **GL3 is the reference.** Each frame-description item keeps the draw-stream hashes ([How pictures are
  compared](#comparing)), so GL3's picture does not move while the seam is built.
- **The GPU renderer against GL3.** Each item that adds a part to `gpu` (11 to 14, 21) draws the same synthetic scenes
  and named `--play-level` frames on both renderers and compares them with the cross-renderer tolerance (the Across
  renderers row of [the gates](#comparing)): in CI on the software drivers, locally on a real GPU with the counts in the
  pull request. A difference the tolerance does not cover is listed on this page with its reason, or the item is not
  done.

### Expected gains {#migration-gains}

Estimates from the [Baseline](#baseline), not results; each is measured by `perf bench --compare` against the
baseline that item 16 records, on a quiet machine. Humans are already skinned on the GPU through GL3 (which took the
hub with 100 fighters from 37.4 to 7.8 ms a frame: [Performance: findings](../guides/performance.md#findings)), so
today's frame is bound by the CPU in two places: the simulation's step (1.85 ms of the hub's 3.14, 3.69 of the crowd's
7.02) and the drawing work around librw (the crowd's render is 3.18 ms, of which 1.76 ms is posing the humans' bones).
The GPU is nearly idle: 17 times the pixels cost no more, and a frame makes 410-690 draws.

| Step | What it gains (estimate) | Measured by |
| --- | --- | --- |
| The backend switch itself (items 11, 13, 14, 20) | a little on this machine: the drawing besides posing (0.82 ms in the hub, 1.42 with 50 fighters, 1.19 in the street) and the present (0.19-0.38 ms) can shrink, perhaps by half, with fewer state changes, instancing and no per-draw librw dispatch: about 0.4-0.8 ms a frame. Its main gains are what GL3 cannot give: Metal on macOS, the present modes, HDR, the post chain | `perf bench --scene all --renderer gpu --compare` at each item, against GL3's baseline |
| Skinned humans on the GPU renderer (item 14) | none beyond GL3's: the skinning is already on the GPU, and the posing (about 25-30 µs a human) stays on the CPU. The item keeps the gain; it does not add one | `--scene hub,hub-crowd --renderer gpu --compare`, `render.skin` and `gpu.frame` side by side |
| The simulation thread (items 37 to 42) | the largest now: the step leaves the frames that carry one, so a frame is drawing only (1.4 ms in the hub, 3.6 with 50 fighters); the p99 frame and the frames over the display's period fall ([What it gains](#threads-gain)), and an uncapped rate rises by the steps' share of the second, about 6 % in the hub and 11 % in the crowd | item 42's real-time runs at a 240 cap, with and without the thread |

The [Budget](#budget) turns these into the gates each item is held to.

### Spike {#spike}

A throwaway program, kept in the scratch folder and never committed, linked against the SDL3 static library that
Coney's own build makes from the pinned version, asked SDL's GPU API for each driver, read its shader formats and
device properties, claimed a 1920 × 1080 window and timed 300 frames that only clear and present, in each present
mode. On the Windows machine the agents share (an RTX 5080, a 240 Hz display):

| Driver | Shader formats | `IMMEDIATE` | `MAILBOX` | `VSYNC` | HDR10 swapchain |
| --- | --- | --- | --- | --- | --- |
| `vulkan` | SPIR-V | 0.35 ms a frame | 0.29 ms | 4.4 ms (the display's rate) | yes |
| `direct3d12` | DXBC, DXIL | 0.27 ms | 0.23 ms | 4.1 ms | yes |

What it settles: the GPU API is already compiled into Coney's SDL3 (`direct3d12` and `vulkan` on Windows; Metal is
SDL's driver on macOS), so option (b) adds no runtime dependency; both drivers open on this machine with every
present mode and HDR10; an empty frame costs about a quarter of a millisecond, so the API itself leaves the whole
budget to the game. Linux (Vulkan) is proven by CI's smoke run of the renderer's start under lavapipe; macOS (Metal)
by CI's build of it and its unit tests, whose GPU case runs where the runner offers a Metal device
([The GPU renderer](gpu-renderer.md#proof)); not by this spike.

## Simulation and drawing threads {#threads}

Today one thread does a frame's work in order: wait out the frame cap, read the window's events, run the 0 to 4 steps
the real time calls for, draw once and present (`GameModeStack::runUntilEmpty`). A frame that runs a step takes the
step's time on top of its drawing, so at a high display rate the frames that carry a step come late and the others do
not. **The plan:** the fixed 1/30 s simulation runs on a thread of its own, and the main thread only draws, at the
display's rate, from a **snapshot** that each step publishes. What the simulation computes does not change; what
changes is the thread that runs it and what drawing may read.

### What it gains {#threads-gain}

The gain is an even frame time, not more frames a second. A step costs about 1.9 ms in the hub, about 3-3.7 ms with
50 fighters ([Baseline](#baseline)) and about 4 ms with 100 in free roam (the frame-spike study in
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

### The snapshot {#snapshot}

**What it holds** is the step's half of the frame description ([Migration](#migration), step 4; items 5 and 7 to 10):
for every drawn thing, its id, its mesh and material ids and its pose on this step (a transform; a human's bone
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

### What stays on the main thread {#threads-main}

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

### Input {#threads-input}

The main thread reads the window's events into the input source every frame, as today, and the source keeps every
press until a step takes it. The simulation thread samples the pads at the start of each step (`samplePads`), taking
the source's state under a short lock, so each press is seen by exactly one step, as today. Which step a live press
lands on depends on real time with one thread or two; what makes runs repeatable is that a step's input is recorded
and replayed by step number (`--input-script`, `--record`, a network client's commands), and that path does not
change. Sampling more often than once a step is not planned
([Input sampled at the display rate](../guides/network-architecture.md#sixty-input)).

### The drawing clock {#threads-clock}

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
  ([Frame rate](#frame-rate-gaps)); frames keep presenting, and the delay grows. Once it has, a step up to its 33 ms
  costs no frame; a single spike beyond the delay of the moment shows as the motion pausing for the spike's length,
  not as frames missed. A step slower than a whole step drops time as today (`kMaxStepsPerFrame`).

### Streaming {#threads-streaming}

The first human of a model not yet loaded costs 28-41 ms inside its step today
([Performance](../guides/performance.md)). With the split, the step's share is hidden by the drawing delay, but the
librw share moves to drawing (item 38), where it would make a frame late. Item 43 adds a loader thread that reads and
decodes model and texture files into Coney's own formats ([Migration](#migration), step 5) off both threads; drawing
uploads what it decoded. A step that needs a model's simulation data waits for it inside the step, so what spawns and
when stays the same and the wait is the step's own time; the step asks early where it knows (a spawner's models, a
level's start). Test mode loads at once, on its one thread. How the original streams a spawned human's model is
studied first, in the first-model-load issue drafted by the frame-spike study; item 43 follows its findings.

### Debug tools {#threads-debug}

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

### Audio {#threads-audio}

The mixer already works across two threads: its game side queues commands without locking and SDL's audio thread
mixes (`src/audio/mixer.h`), and only one thread may be the game side. That side becomes the simulation thread, since
every `GameSound` call comes from a step; a call from the overlay goes through the boundary lock. Test mode mixes
offline on its one thread, unchanged.

### Test mode, lockstep and networked play {#threads-modes}

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

### How tests prove nothing changed {#threads-tests}

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

## Lighting and shadows {#lighting}

The content is the original's, as the Xbox port draws it; the rendering is Coney's own and goes past both consoles. What
the Xbox port draws differently from the PS2 is the research of [#881](issue:881), on [Xbox lighting and
shadows](../research/xbox-lighting.md). Its Rules table is what the *Xbox* preset reproduces and its In Coney section
lists the options; this section says what Coney keeps and how it draws it, and the [Presets](#presets) table says what
each preset draws.

**Kept in every preset that draws it:** what the research's In Coney section lists under "What to keep exactly"
([Xbox lighting: In Coney](../research/xbox-lighting.md#in-coney)): the lights and their choice, the level's shadow
light and its rules, the gamma curves. All of it reaches the renderer through the
**lighting frame** of the [Realtime lighting design](../guides/realtime-lighting.md#the-lighting-frame), built each
frame from simulation state and never read back by it. The *Xbox* preset's gamma curves are read from the player's
Xbox disc, not carried in Coney, and the values of the 33 records the research lists are checked against it (item
24).

**Kept in *Xbox*, widened in *Enhanced* and *High*:** who casts a shadow and who receives one. *Xbox* keeps the port's
casters and receivers as the research gives them; *Enhanced* adds receivers and *High* adds range
([Presets](#presets)).

**One shadow source.** Every preset that draws a shadow map draws it from the level's shadow light, not from a sun of
Coney's choosing. That answers the question the Xbox research leaves to this design: *Enhanced* and *High* take the
Xbox's per-level shadow light and its rules ([Xbox lighting: the shadow
light](../research/xbox-lighting.md#shadow-light)) as their inputs, so shadows fall the same way in *Xbox*, *Enhanced*
and *High*, and a level whose script does not turn shadows on keeps the blob in every preset. The realtime lighting
design's shadow map is this map, and its bounded multiply takes the level's shadow colour as its strength ([Realtime
lighting: the lighting model](../guides/realtime-lighting.md#lighting-model)).

Reflections stay as the [Realtime lighting design](../guides/realtime-lighting.md#reflections) gives them. Where the
research leaves a question open for a running Xbox (the shadow buffer's filtering, the blob under the shadow map,
the ramp's exact arithmetic), the *Xbox* preset uses that page's marked stand-ins.

**Never gameplay.** The stealth "in shadow" flag and every lit or unlit rule the AI reads keep the PS2's ground check;
a test makes sure no renderer state reaches them.

The work order is in [Order](#order): the lighting frame first (item 19: simulation-side data that needs no new
renderer), then, once the new renderer has its scene target (item 22), the gamma ramp, the *Xbox* preset's light caps
and fog, the shadow maps and per-pixel lights (items 24, 25 and 28 to 31).

## Anti-aliasing and scaling {#aa}

All of these draw into the scene target at the internal resolution and resolve or upscale into the window
([Enhancements: foundation F2](../guides/enhancements.md#foundations)).

| Technique | Where it fits | Licence |
| --- | --- | --- |
| MSAA 2×, 4×, 8× | geometry edges, which this game's long straight scenery edges are full of; with alpha-to-coverage for fences and foliage. On the SDL3 GPU renderer only (item 26): the GL3 path draws none | none (API feature) |
| FXAA 3.11 | cheapest post AA, slight blur | NVIDIA's FXAA licence, permissive; check at adoption |
| SMAA 1×, T2× | better edges than FXAA, cheap; the default post AA | MIT |
| TAA (Coney's own) | shimmer on thin wires and fences; uses the blended steps' previous poses for motion vectors and a jittered projection | Coney's code |
| Render scale 0.5-2× | above 1 is supersampling; a *640 × 448* mode with nearest filtering for the PS2 look | Coney's code |
| Upscalers | from a render scale below 1, in tiers ([below](#upscalers)) | per tier |

### Upscalers {#upscalers}

Listed as options, in tiers, each off in *PS2* and *Xbox*. Each licence is dealt with when its item comes: FSR's MIT
needs no decision, and a proprietary SDK or binary is a maintainer decision then. The versions are the current ones
as this design was written; the item checks them again.

| Tier | Upscaler | Runs on | Licence (as its project states it; not confirmed, checked at adoption) | Reach from SDL3's GPU API | Item |
| --- | --- | --- | --- | --- | --- |
| Fallback | AMD FSR 1 (EASU and RCAS): spatial upscale and sharpen | any GPU; the one tier CI can test, on its software drivers | MIT | plain shaders of Coney's | 33 |
| Main, portable | AMD FSR 3.1 (FidelityFX SDK): temporal, from TAA's motion vectors | any vendor | MIT | the SDK's upscaler shaders ported as Coney's compute passes; its own backends want native handles | 34 |
| Vendor | NVIDIA DLSS 4 and DLAA (Streamline or NGX SDK) | NVIDIA RTX | NVIDIA's proprietary SDK licence; redistributable binaries | native handles and a vendor binary beside the executable; a separate opt-in plug-in, as the [Enhancements plan](../guides/enhancements.md#decided) records for proprietary SDKs | 35 |
| Vendor | AMD FSR 4 ("Redstone"): machine-learned | AMD RX 9000 and 7000; 4.1 adds RX 6000 and some APUs; never NVIDIA. Shader Model 6.6, so Direct3D 12 only until AMD's announced Vulkan version ships | the FidelityFX SDK's terms for its prebuilt ML upscaler | as DLSS | 36 |
| Optional | Intel XeSS | Intel Arc best; other GPUs through its fallback | Intel's XeSS SDK licence; redistributable binaries | as DLSS | filed if a maintainer asks for it |

The vendor tiers need the device and command buffer underneath SDL's GPU API: a small patch to Coney's pinned SDL (or
an upstream extension) that exposes them, used only by the plug-in. Whether to carry that change to SDL is decided
with item 35, the first that needs it; FSR 3.1 is ported so that it does not.

## HDR output {#hdr}

The game's content is SDR on the GS scale, so HDR output is a presentation of it, not new range
([Enhancements: HDR](../guides/enhancements.md#lighting-and-post-processing)). On the GPU renderer, an HDR10 or scRGB
swapchain (SDL3's GPU API has both) takes an inverse tone map of the SDR picture, with highlights from what the
lighting work adds (coronas, glows, bloom). Paper white and peak brightness are display values; the HUD and the front
end draw at paper white. It is a live toggle on the debug menu's Display page and a command-line flag, greyed out with
the reason when the display or its driver has no HDR. The PS2 look stays the default and test mode forces SDR. Its
pull request shows a picture check for each output mode (item 23).

## Presets {#presets}

A **preset** is a named set of display values ([Where the settings
live](../guides/enhancements.md#where-the-settings-live)), `Display/Preset`. There are four, and this table is their one
definition: the [Enhancements plan](../guides/enhancements.md) and the [Realtime lighting
design](../guides/realtime-lighting.md#quality-ladder) link it. Changing any value after choosing a preset shows the
setting as *Custom*, the player's own set, which is not a fifth preset. *Faithful*, the name those two pages used
before, is *PS2*.

| Part | *PS2* | *Xbox* | *Enhanced* | *High* |
| --- | --- | --- | --- | --- |
| Renderer | GL3 today; SDL3 GPU once it draws the same picture | SDL3 GPU | SDL3 GPU | SDL3 GPU |
| Lights | per vertex, the PS2's caps | per vertex, the Xbox's caps | the same lights per pixel, `1 - d / r` falloff kept, the Xbox's caps | caps lifted, clustered per-tile light lists |
| Shadow source | none | the level's shadow light | the level's shadow light | the level's shadow light, plus point-light shadows for fires and lamps, which no console had |
| Casters | none | the Xbox's: humans within its range and their held props | as *Xbox* | as *Xbox*, out to the outer cascade |
| Receivers | the ground under the blob | the Xbox's: the world and world objects | adds humans, and cars once the research's open question on cars is answered | as *Enhanced* |
| Shadow map | none | one map fitted round the casters at the Xbox's size, hard compare, the research's stand-in filter | a fitted map at 2048 or more, PCF, normal-offset bias | two cascades round the camera, PCSS-style contact hardening |
| Blob | on | off (the research's stand-in) | off | off |
| Fog | the PS2's | per pixel, linear | per pixel, linear | per pixel, linear |
| Gamma | none | the level's ramp, a 1D LUT in the last pass | the ramp | the ramp, or a tone map with HDR output |
| Brightness default | the PS2's | the Xbox's, for a profile made with the ramp on | as *Xbox* | as *Xbox* |
| Reflections and AO | none | none | environment-map materials with cube maps | adds GTAO, probes and tagged reflections |
| Anti-aliasing | none | none | SMAA | MSAA or TAA, with SMAA |
| Render scale | 1 | 1 | 1 | 1, or above where the budget allows |

Each Xbox part (the shadow map, the gamma ramp with its brightness default, the per-pixel fog, the light caps) is
also a toggle of its own, off in *PS2*, so a player can take one without the rest: these are the toggles the Xbox
research's In Coney section places under *Faithful*. The frame-rate gaps are closed in every preset
([Gaps](#frame-rate-gaps)); at 30 frames a second and in test mode the drawn values are the newest step's, so the
*PS2* tests do not see them. Ray-traced shadows and AO are in no preset: SDL3's GPU API has no ray tracing, so they
need a native Vulkan or D3D12 path beside it (the same native handles as [Upscalers](#upscalers)), a later option
after item 21.

*PS2* is what tests use and is forced in test mode. Which preset a player gets by default is open
([Questions](#questions)); until a maintainer decides, every option ships off, which is *PS2*.

### The PS2 preset and the tests {#ps2-preset}

The *PS2* preset is what keeps the fidelity tests honest. `--screenshot`, `--record` and the disc tests force it, so
their frames are comparable with the original's captures
([The runtime comparison](../guides/testing.md#runtime-comparison)), and every step of the
[Migration](#migration) is checked in it as below.

### How pictures are compared {#comparing}

A GPU's output differs between GPUs and drivers, and CI has no GPU, so no test hashes a hardware GPU's pixels. The
exact check is on what Coney asks librw to draw; pictures are compared with a tolerance, in CI through a software
rasteriser and locally on a real GPU.

**Where the draw stream is recorded.** A librw device sees only camera begin and end, render states, Im2D, Im3D and
rasters: an atomic is drawn by its platform pipeline's render callback, which calls OpenGL itself, and Coney's screen
copies are direct OpenGL calls. So the recorder does not sit in librw. It is a thin layer on Coney's side of the
boundary. Item 1 routes through it every librw drawing call Coney makes except the atomic, clump and human renders (the
render states, Im2D, Im3D, camera, rasters), and each direct OpenGL operation as a named operation (copy the back
buffer, read back); items 7 to 10 add the atomic, clump and human renders, each for its own draws. Live, it forwards
each call; under `--headless` it records instead, with no OpenGL context. An atomic or clump render is recorded as one
draw: the mesh, its transform, the lights chosen for it, its material's states and, for a human, a hash of its skinned
vertices. What librw's pipelines then do inside a draw is librw's code, which items 5 and 7 to 10 do not change, so it
needs no hash; the picture comparison covers it.

**What each item's hash covers.**

| Items | Draws through the recorder | Covered by the draw-stream hash | Covered by the picture comparison only |
| --- | --- | --- | --- |
| 1 to 6 | render states, Im2D, Im3D (particles, coronas, the 2D layers), camera, rasters, the named OpenGL operations | all of them | world, objects, cars and humans, which still call librw directly |
| 7, 8, 9, 10 | each first routes its own atomic, clump or human renders (world; objects and cars; humans; the effects drawn as models, such as debris, since particles and coronas are Im2D and Im3D and are recorded from item 1) through the recorder in a first commit that changes nothing else and records the hashes, then moves them onto the frame description in a second commit | its own draws: the GL3 renderer, drawing from the description, makes the same calls, so the second commit must leave the hashes the first recorded unchanged (the reviewer checks that the expected hashes do not change after the first commit) | what librw's pipelines draw inside each call |
| after 10 | everything, through the GL3 renderer | the *PS2* picture on GL3 | the SDL3 GPU renderer, which draws through no librw call |

The hash is a hash of the stream with floats rounded to a fixed grid (1/1024 of a unit for positions and texture
coordinates, 8 bits a colour channel). It runs in CI, on Windows, Linux and macOS, on synthetic scenes (Coney-made
meshes, textures and lights, no game data) whose expected hashes are in the test; with the disc, named `--play-level`
frames print theirs locally. A change to a hash fails CI until the pull request explains it and updates the hash. A
value that lands on a rounding boundary is the grid's weak spot: item 1's first CI run on the three systems shows
whether the grid holds, and a coarser one replaces it if not.

**Built (item 1).** The layer is `coney::platform::draw` ([draw_calls.h](repo:src/platform/draw_calls.h)); the stream,
its grid and its hash are `DrawStream` ([draw_stream.h](repo:src/platform/draw_stream.h)).

- **What goes through it.** Every `rw::SetRenderState`, `SetRenderStatePtr` and `GetRenderState`, Im2D and Im3D call,
  camera clear, begin, end and show, the GL3 rasters Coney makes, locks, fills from an image and converts from the PS2,
  and five named OpenGL operations (the frame copied into a raster, the frame read back, the wireframe switch, drawing
  sent back to the window's frame buffer, a raster's mip levels): 223 call sites in 15 files of `src/platform/`, fewer
  than the estimate of about 290 because the `rw::gl3::` vertex types are not calls and the renders below wait. Routed
  with items 7 to 9, whose renders they set up: `WorldRenderer::render` (the world, [#1199](issue:1199)),
  `ParkedCars::draw` and `PlacedObjects::draw` (cars and objects, [#1200](issue:1200)) and
  `PlayLighting::drawHumanPasses` (humans, [#1201](issue:1201)); with items 7 to 10, the atomic, clump and human renders
  and what runs inside their pipelines (the one `VERTEXALPHA` state in each of `texture_lod.cpp` and
  `character_skin.cpp`, the skinning buffers). Never: the device's set-up at start and the profiler's OpenGL queries,
  which draw nothing.
- **What a record holds.** A draw holds its primitive, every render state set so far with its value as it stands at
  the draw (a raster as its place in the recording's order of first use, its size and format, and a digest of the
  pixels Coney uploaded to it), the vertices and the indices; Im3D also its normals, world matrix and flags. A camera's
  begin holds its matrix, view window, offset, clip and fog planes, projection and frame size. So setting a state twice,
  or to the value it already has, changes no hash. Bytes are written little endian in a fixed order, hashed with 64-bit
  FNV-1a; each record also has a hash of its own, which says where two streams first differ.
- **Live and headless.** Live, each call is forwarded and also recorded while a recording runs. Under `--headless` no
  call reaches librw or OpenGL, the layer keeps the render states itself, a raster is made with librw's `DONTALLOCATE`
  and a lock gets a buffer of the layer's; `RenderEngine`'s 2D drawing, which draws nothing headless, runs while a
  recording does, with librw's default near plane in place of a camera's.
- **Synthetic scenes** in [draw_calls_test.cpp](repo:tests/platform/draw_calls_test.cpp), drawn headless: a 2D scene
  through `RenderEngine` (the fill, quads, alpha-tested triangles, a view wash, a window rectangle), a textured Im3D
  mesh under a placed camera with a Coney-made checkerboard, the throw arc's ribbon drawn by `ThrowArcRenderer`, and
  the named operations. Their expected hashes are in the test, beside checks that a repeated state keeps the hash and
  a grid step, a texel or a state moves it; the grid and the encoding are checked against the definition in
  [draw_stream_test.cpp](repo:tests/platform/draw_stream_test.cpp).
- **Disc frames.** `--draw-hash` prints the last frame's counts and hash ([Running Coney](../guides/running.md)). The
  named frames are run with a window, the world frozen and a fixed size, from the level's first frame of play:

  | Name | Command |
  | --- | --- |
  | `level99-street` | `coney --play-level level99 --frames 90 --freeze-world --render-size 640x448 --draw-hash` |
  | `level95-hub` | `coney --play-level level95 --frames 90 --freeze-world --render-size 640x448 --draw-hash` |

  Each is run twice on the branch and on its base; a pull request that changes drawing reports the four hashes in its
  Testing section, and two runs of one build must agree. They need the window: headless, play draws only its 2D layer.
  Item 1's hashes, twice each on Windows: `level99-street` 23 records, `0xe923080c2453d3a4`; `level95-hub` 16 records,
  `0x4ba9bf8c9bdc48ed`. A PS2 texture's digest is of its texel and palette blocks alone: librw's data block also holds a
  heap pointer and gaps, which made two runs differ.
- **The grid across systems** is the open question this item leaves to CI: the expected hashes were recorded on
  Windows, and the first CI run on Linux and macOS shows whether they hold. If one differs, the fallback grid is 1/256
  of a unit for positions and texture coordinates (`kDrawGridSteps`) with colours kept at 8 bits, and the expected
  hashes are recorded again; this paragraph then says which grid holds.

**The picture comparison, and its gates.**

| Check | What it compares | Gate | Tolerance |
| --- | --- | --- | --- |
| Software pictures | the synthetic scenes drawn under a virtual display through Mesa's software drivers (llvmpipe for GL3, lavapipe for Vulkan once item 11 lands), against reference images of those scenes kept in the tests (Coney-made, small). The runner image's own Mesa moves with the image, so the job runs in a container image pinned by its digest, with Mesa at a fixed package version, cached on the runner. Moving the pin is a pull request of its own that regenerates the reference images in the same pull request and reports the old and new counts | CI on Linux (item 2), a required check: its key in `merge.required_checks` of `.github/conventions.yml` lands with the job | the tolerance below; exact if the pinned Mesa proves to give the same pixels on every run |
| Disc pictures, one renderer | `--screenshot` of named frames at `--render-size 640x448` with a pinned camera ([Matched views](../guides/running-play-level.md#matched-views)), from the branch against the same frames from its base commit, on one machine | local: every pull request from item 5 on that changes drawing reports the counts in its Testing section, from the tool's output kept in the scratch folder, and the reviewer refuses one without them, as for the Runtime line ([Capturing](../guides/testing.md#pr-media)) | at most 2 of 255 apart in any channel, over at most 0.1 % of the pixels: marked stand-ins until item 2 measures repeated captures of one build |
| Across renderers | the same frames drawn by GL3 and by SDL3 GPU, on one machine and in CI's software pictures | as above, from item 11 on | looser; item 11 sets it from its first 2D comparison and records it here |

The counts reported are the pixels over the tolerance and the largest difference. Pictures of the game stay in the
scratch folder and are never committed; only the synthetic scenes' references are in the repository.

## Performance {#performance}

### Benchmark {#benchmark}

`coney-tools perf bench` and `coney --bench` ([Performance](../guides/performance.md#bench)) are the benchmark;
they gain named scenes and a comparison:

```sh
uv run --project python coney-tools perf bench --scene hub,hub-crowd,street --out ../../scratch/perf
uv run --project python coney-tools perf bench --scene all --compare ../../scratch/perf-baseline --out ../../scratch/perf
```

- **Scenes** are a table in the tool (name, level, checkpoint, camera, fighters, frames, render size), so a scene is
  the same work on every run: a lockstep test-mode run with a pinned camera and a fixed seed. Each scene runs three
  times, and the report's `meta` records the render size and the window beside the level.
- **`--compare FOLDER`** prints each scene's median frame time beside the baseline's and the change in percent, and
  exits 1 when a scene is slower than the budget allows, on a quiet machine only ([Budget](#budget)).
- **GPU time.** A `gpu.frame` measure from GPU timestamp queries (OpenGL timer queries, then the new renderer's), so
  a report shows whether a frame is bound by the CPU or the GPU.
- **`--display-preset` and `--renderer`** are passed through to Coney, so every display preset and renderer is
  measured the same way (`--preset` stays the build's CMake preset).

### Baseline {#baseline}

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
  ([Simulation and drawing threads](#threads)) takes off the frames that draw.
- **The GPU is barely used.** In the hub, 640 × 448 and 2560 × 1920 (17 times the pixels) have the same median frame,
  3.36 and 3.22 ms, within each other's brackets. There is room for per-pixel lighting, shadow maps and AA.
- **About 410-690 draw calls** a frame through librw's one-atomic-at-a-time pipelines; instancing and fewer state
  changes on the new renderer bring that down, but at this count draws are not the limit.

[Order](#order) item 16 records this again on the scene table with GPU time, on a quiet machine, and that record
becomes the baseline `--compare` reads; the figures above are the starting point it replaces. The items that draw
through GL3 (1 to 10 and 15 to 19) must keep these numbers (the *PS2* budget below); the GPU renderer's items are
measured against them.

### Budget {#budget}

Frame-time budgets on this machine, each scene's median of three runs, checked by `perf bench --compare` once item 16
lands. They are targets for the work in [Order](#order), not results:

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

## Licences {#licences}

What this design would bring in, each licence checked when its item comes: a permissive one needs no decision, a
proprietary SDK or binary is the maintainer's. The shader build checked the first four rows at its pins.

| Component | Licence | Used for |
| --- | --- | --- |
| SDL3 (GPU API) | zlib | already pinned |
| SDL_shadercross | zlib | build-time shader compiler; none of its code is in the executable ([The shader build](../guides/shaders.md)) |
| DXC, built from source (libsdl-org's fork of v1.9.2602, the commit SDL_shadercross's submodule names, with two build fixes) | LLVM Release License (University of Illinois/NCSA), Apache-2.0 with LLVM exception (its HLSL headers) and miniz's MIT-style licence; its submodules SPIRV-Tools (Apache-2.0), SPIRV-Headers (Khronos, MIT-style) and DirectX-Headers (MIT, off Windows only), each fetched at the commit the fork names. Microsoft's release archives are not used: they carry Microsoft's own terms (`LICENSE-MS.txt`, use "solely on Windows") | HLSL to SPIR-V and DXIL, build time; nothing of it is in the executable or distributed |
| SPIRV-Cross | Apache-2.0 | SPIR-V to MSL, build time |
| DXIL validator (`dxil.dll`, `libdxil.so`) | Microsoft's own licence, shipped as a binary in DXC's releases | **not used, not fetched**: the source-built DXC validates and signs DXIL itself (below) |
| SMAA | MIT | post AA |
| FSR 1, FSR 3.1 | MIT | upscaling |
| FXAA 3.11 | NVIDIA's FXAA licence | optional post AA |
| DLSS, FSR 4, XeSS | proprietary SDKs or prebuilt binaries | optional plug-ins only |

**DXIL signing, confirmed with the pinned versions.** Direct3D 12 loads only DXIL that carries the validator's hash.
DXC's validator hash is open source since 1.8.2502 (`lib/DxilHash`), and from 1.8.2505 the compiler always uses its
internal validator, loading an outside `dxil.dll` only when `DXC_DXIL_DLL_PATH` names one
(`lib/DxcSupport/dxcapi.extval.cpp` in the pinned fork; SDL_shadercross's README, which lists `dxil.dll`, is out of
date). So DXC built from source signs DXIL with nothing of Microsoft's. A test checks that every embedded DXIL
container carries a non-zero hash (with validation off, `-Vd`, it is all zero), on every system CI builds.

## Questions for the maintainer {#questions}

- `LEGAL.md` naming the shader build's tools (SDL_shadercross, SPIRV-Cross, DXC and DXC's submodules), which are
  build-time only ([Licences](#licences)).
- Which preset a player gets by default: *PS2*, or *Enhanced* on a capable machine.
- When the GL3 renderer retires after the GPU renderer becomes the default (item 21), and with it librw's GL3
  platform.

## Order {#order}

The implementation issues, in the order they should land, each one pull request. Their drafts, in the task form, are
written for the coordinator to file. Sizes are those of [Backend](#backend), with **S** under a week.

**The SDL3 GPU migration's groundwork** comes first: the proof that nothing moves, a start that draws without librw's
GL3 device, the shader build, the frame description and Coney's own formats, then the GPU renderer to parity and the
switch ([Migration](#migration)).

| # | Issue | Depends on | Size |
| --- | --- | --- | --- |
| 1 | `graphics: Add draw-stream hashes for the PS2 picture` | none | L |
| 2 | `tools: Add a picture comparison with a tolerance` | none | M |
| 3 | `platform: Add the GPU renderer's start with librw opened for data only` | none | M |
| 4 | `build: Add the HLSL shader build through SDL_shadercross` | none | S |
| 5 | `graphics: Add the frame description for the 2D and screen passes` | 1, 2 | M |
| 6 | `graphics: Add Coney's own mesh and texture formats from librw's data` | none | M |
| 7 | `graphics: Rework the world onto the frame description` | 5, 6 | M |
| 8 | `graphics: Rework objects and cars onto the frame description` | 7 | M |
| 9 | `graphics: Rework humans onto the frame description` | 8 | M |
| 10 | `graphics: Rework effects and sprites onto the frame description` | 9 | M |
| 11 | `graphics: Add the 2D layers to the SDL3 GPU renderer` | 2, 3, 4, 5 | M |
| 12 | `graphics: Add Dear ImGui's SDL GPU backend to the GPU renderer` | 11 | S |
| 13 | `graphics: Add the world and objects to the SDL3 GPU renderer` | 8, 11 | M |
| 14 | `graphics: Add skinned humans to the SDL3 GPU renderer` | 9, 13 | M |

**Beside the groundwork:** these do not depend on the backend and may land at any point of items 1 to 14.

| # | Issue | Depends on | Size |
| --- | --- | --- | --- |
| 15 | `graphics: Add display presets with PS2 forced in test mode` | none | S |
| 16 | `tools: Add named scenes, GPU time and a compare to perf bench` | none | M |
| 17 | `graphics: Fix particles and screen effects drawn between steps` | none | M |
| 18 | `graphics: Add a VRR frame-pacing mode` | none | S |
| 19 | `graphics: Add the lighting frame and the level's shadow light` | none | S |

**Parity, the switch, then the enhancements**, each on the GPU renderer only:

| # | Issue | Depends on | Size |
| --- | --- | --- | --- |
| 20 | `graphics: Add effects and parity tests to the SDL3 GPU renderer` | 10, 14, 16 | M |
| 21 | `graphics: Update the default renderer to the SDL3 GPU renderer` | 12, 15, 20, a maintainer's go-ahead | S |
| 22 | `graphics: Add the scene target and render scale` | 20 | M |
| 23 | `graphics: Add HDR output with a live toggle` | 22 | M |
| 24 | `graphics: Add the Xbox gamma ramp and brightness default` | 22 | S |
| 25 | `graphics: Add the Xbox preset's light caps and per-pixel fog` | 13, 19 | M |
| 26 | `graphics: Add MSAA and alpha-to-coverage` | 22 | S |
| 27 | `graphics: Add SMAA as the post anti-aliasing` | 22 | S |
| 28 | `graphics: Add the Xbox preset's character shadow map` | 19, 22 | M |
| 29 | `graphics: Add per-pixel lights and clustered light lists` | 19, 22 | M |
| 30 | `graphics: Add the Enhanced and High shadow maps` | 28 | M |
| 31 | `graphics: Add point-light shadows for fires and lamps` | 30 | M |
| 32 | `graphics: Add TAA with motion vectors` | 22 | M |
| 33 | `graphics: Add FSR 1 upscaling` | 22 | S |
| 34 | `graphics: Add FSR 3.1 upscaling` | 32 | M |
| 35 | `graphics: Add DLSS and DLAA as an opt-in plug-in` | 32, a maintainer's licence decision | M |
| 36 | `graphics: Add FSR 4 as an opt-in plug-in` | 34, 35 | M |

**The simulation and drawing threads** ([Simulation and drawing threads](#threads)):

| # | Issue | Depends on | Size |
| --- | --- | --- | --- |
| 37 | `graphics: Add the step snapshot that drawing reads` | 10, 17, 19 | L |
| 38 | `graphics: Rework model creation onto the drawing side` | 37 | M |
| 39 | `core: Add the step-boundary lock for the developer overlay` | 37 | S |
| 40 | `core: Add the simulation thread and the drawing delay` | 38, 39 | M |
| 41 | `build: Add a ThreadSanitizer preset and CI job` | 40 | S |
| 42 | `core: Update the simulation thread to on by default` | 16, 40, 41 | S |
| 43 | `core: Add a loader thread for models and textures` | 38, 40, the first-model-load study | M |

Item 1 is one of the two L items: it routes about 290 of the 460 call sites, each change mechanical, so it stays one
pull request. Items 1, 2, 3, 4 and 6 depend on nothing and can run beside each other; item 3 is the first point at
which the game runs with librw's GL3 device closed. Item 21 is the switch the groundwork leads to and waits for a
maintainer's go-ahead; the enhancements after it need parity (item 20), not the switch, and never draw through GL3.

Of the realtime lighting design's steps, item 19 is L0, item 22 with the GPU renderer's shaders is L1, items 28 and
30 are L2, item 29 is L3 and item 31 is L6; L4, L5 and GTAO follow item 22 and are filed from that design. XeSS, ray
tracing and the [Enhancements plan](../guides/enhancements.md)'s other options follow item 21 and are filed from those
pages.

Items 37 to 43 follow item 10, because the snapshot is the frame description's per-step half and building it before
would build it twice, and they do not depend on the GPU renderer, so they can run beside items 11 to 36. Item 37 is L:
every mode's `render()` moves onto snapshots, the front end, movies and menus as well as the game.
