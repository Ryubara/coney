# Engine: the GPU renderer

Coney's renderer on SDL3's GPU API (Vulkan, Direct3D 12, Metal), which the [renderer design](renderer.md#backend) chose
to replace librw's GL3 device as it grows ([Migration](renderer.md#migration)). This page says how what is built so far
works: the start of [Order](renderer.md#order) item 3, which opens the window, the GPU device and the swapchain, opens
librw for data only and clears and presents each frame while the game runs. It is Coney's own code with no counterpart
in the original, which drew through the PS2's GS. GL3 stays the default.

## The choice {#choice}

`--renderer gpu` or `gl3` wins; without it a windowed run takes the overrides file's `Display/Renderer` (0 GL3, the
default; 1 the GPU renderer), and test mode and `--render-references` take GL3 whatever is saved
(`takesSavedRenderer()`), so a test run never depends on a player's file (`chooseRenderer()`,
`src/core/renderer_choice.h`). The setting is read from the file before the renderer starts, ahead of the full load of
the overrides, and shows on the debug menus' Tunables page under *Display*; a change there applies at the next start.
The release build takes `--renderer` but keeps no overrides file. `--renderer gpu` is refused with `--headless`,
`--load`, `--screenshot`, `--record` and `--render-references`: it needs a window, and nothing reads its frames back
yet.

## The device and the window {#device}

`GpuPresenter` (`src/platform/gpu_presenter.*`) makes SDL's GPU device first, for any driver that takes SPIR-V, DXIL or
MSL (the formats SDL_shadercross makes, item 4); `SDL_GPU_DRIVER` picks one. Then the window, made hidden and resizable
through SDL3, is claimed for the device on the main thread, the one that made it, as SDL requires; it is placed on its
display (`--window-display`) and only then shown, so it never appears on another display first, and a test run's window
never takes the keyboard focus, as with GL3.

## The present mode {#present-mode}

`choosePresentMode()` (`src/platform/present_mode.h`) picks `VSYNC` while `--vsync` is on; with it off, `MAILBOX` when
nothing caps the rate (uncapped without tearing) and `IMMEDIATE` under `--fps-cap` (the pacer holds the rate, and a
variable-refresh display follows each frame, as GL3's swap interval 0 does). A mode the window lacks falls back to the
other of those two, then to `VSYNC`. A change of vsync or cap on the debug menus' Display page takes effect at the next
present.

## librw for data only {#librw}

The GPU backend opens librw exactly as the NULL backend does, so texture dictionaries, streams and PS2 rasters load and
the game runs; `drawsPixels()` is false, so nothing converts textures for drawing. Each frame is cleared to its colour
and presented: a stand-in until item 11, with no black border round the logical screen and no developer overlay (its
`sdlgpu3` backend is item 12).

## The fallback {#fallback}

When no GPU device opens, the run log says so with SDL's reason (`could not start the GPU renderer: no GPU device opened
...; drawing with GL3 instead`) and the run draws with GL3.

## The proof {#proof}

`tests/platform/gpu_renderer_test.cpp` reads a synthetic clump and decodes a synthetic PS2 raster with librw opened this
way, headless; checks that a start with no GPU device fails with that message and leaves SDL's video stopped; and, where
a GPU device opens, starts the GPU renderer in a hidden window and presents. On CI, every build job builds it, and the
Linux GCC job starts `coney --renderer gpu --frames 10` under lavapipe (Mesa's software Vulkan) on a virtual X display
and checks the run log's `renderer: gpu on vulkan` and `renderer: N of 10 frames shown` lines ([What CI
runs](../guides/building.md#what-ci-runs)).
