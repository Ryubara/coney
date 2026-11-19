# Engine: the GPU renderer

Coney's renderer on SDL3's GPU API (Vulkan, Direct3D 12, Metal), which the [renderer design](renderer.md#backend) chose
to replace librw's GL3 device as it grows ([Migration](renderer.md#migration)). This page says how what is built so far
works: the start of [Order](renderer.md#order) item 3, which opens the window, the GPU device and the swapchain and opens
librw for data only, and item 11, which draws the frame description's 2D passes (the front end, the legal screens, the
movies, the HUD) with Coney's own shaders and reads the frame back for `--screenshot`. It is Coney's own code with no
counterpart in the original, which drew through the PS2's GS. GL3 stays the default.

## The choice {#choice}

`--renderer gpu` or `gl3` wins; without it a windowed run takes the overrides file's `Display/Renderer` (0 GL3, the
default; 1 the GPU renderer), and test mode and `--render-references` take GL3 whatever is saved
(`takesSavedRenderer()`), so a test run never depends on a player's file (`chooseRenderer()`,
`src/core/renderer_choice.h`). While the GPU renderer draws no developer overlay, a saved 1 is refused: the run draws
with GL3 and warns ([The way back to GL3](#way-back)). The setting is read from the file before the renderer starts,
ahead of the full load of the overrides, and shows on the debug menus' Tunables page under *Display*; a change there
applies at the next start. The release build takes `--renderer` but keeps no overrides file. `--renderer gpu` is refused
with `--headless`, `--load`, `--record` and `--render-references`: it needs a window, hands no frame to the clip
recorder, and the reference renders are GL3's offscreen. It takes `--screenshot` ([The capture](#capture)).

## The way back to GL3 {#way-back}

The GPU renderer draws the 2D layers, the pad's debug menus among them, but not yet the developer overlay
([#1204](issue:1204)) or the 3D scene, so the Tunables page that sets `Display/Renderer` may not be reachable. A saved
`Display/Renderer = 1` therefore never starts it: the run draws with GL3 and the run log says `the overrides file saves
Display/Renderer = 1, but the GPU renderer draws no developer overlay yet, so this run draws with GL3; ...` (`kGpuRendererDrawsMenus`
and `chooseRenderer()`, `src/core/renderer_choice.h`). Only `--renderer gpu`, asked for by name, starts it, and closing
that window ends the run.

To go back to GL3 for good, set *Display / Renderer* to 0 on the Tunables page and *Save overrides*, or delete the
`Display/Renderer = 1` line from the overrides file (`coney-tunables.ini` in your config folder, or the file
`--tunables FILE` names); `--renderer gl3` takes GL3 for one run whatever is saved.

When [#1204](issue:1204) lands, `kGpuRendererDrawsMenus` turns true, the test case `renderer: a saved GPU renderer
starts GL3 with a warning while it draws no menus` (`tests/core/renderer_choice_test.cpp`) stops compiling, since it
checks the constant with `STATIC_REQUIRE_FALSE`, and it is removed with the guard and this section's first paragraph.

## The device and the window {#device}

`GpuPresenter` (`src/platform/gpu_presenter.*`) makes SDL's GPU device first, for any driver that takes SPIR-V, DXIL or
MSL (the formats SDL_shadercross makes, item 4); `SDL_GPU_DRIVER` picks one. Then the window, made hidden and resizable
through SDL3, is claimed for the device on the main thread, the one that made it, as SDL requires; it is placed on its
display (`--window-display`) and only then shown, so it never appears on another display first, and a test run's window
never takes the keyboard focus, as with GL3.

## The present mode {#present-mode}

`choosePresentMode()` (`src/platform/present_mode.h`) picks `VSYNC` while `--vsync` is on; with it off, `MAILBOX` when
nothing caps the rate (uncapped without tearing) and `IMMEDIATE` under `--fps-cap` (the pacer holds the rate, and a
variable-refresh display follows each frame, as GL3's swap interval 0 does); so `--fps-cap vrr`, vsync off under a
cap, presents `IMMEDIATE`. A mode the window lacks falls back to the
other of those two, then to `VSYNC`. A change of vsync or cap on the debug menus' Display page takes effect at the next
present.

## librw for data only {#librw}

The GPU backend opens librw exactly as the NULL backend does, so texture dictionaries, streams and PS2 rasters load and
the game runs; `drawsPixels()` is false, so nothing converts textures for librw's GL3 device, and the 3D scene, which
still draws through librw, is not drawn (items 13, 14 and 20 of [Order](renderer.md#order)). What draws the 2D layers
runs on either windowed backend; the movie player picks its screen with `makeMovieScreen()` by backend.

## Drawing the 2D passes {#drawing}

`RenderEngine` records every 2D draw into the [frame description](renderer.md#frame-description) on every backend;
with the GPU backend, `present()` hands the whole description to `GpuPresenter`, whose `GpuRenderer`
(`src/platform/gpu_renderer.*`) draws it in one command buffer:

1. **The batch.** `graphics::batchOverlays()` (`src/graphics/overlay_batch.h`) turns the overlay passes into one vertex
   and index buffer and a list of draws, in the frame's order: a quad as four corners clockwise from its top left and
   two triangles, as the GL3 renderer's screen quads; a list of triangles as its corners. Screen passes (the blur
   pulse, the line blend, the motion blur) are left out and counted, until item 20.
2. **The uploads.** The vertices, the indices and any texture new to the GPU (or with new pixels) go up through one
   transfer buffer, in a copy pass.
3. **The pass.** One render pass into a target of the renderer's own at the frame's size (8 bits a channel, as GL3's
   frame buffer), cleared as the frame starts: black round the logical screen, whose colour comes with the first draw,
   or the whole window in the frame's colour (`graphics::windowClearOf()`). Then each draw, with the shaders
   `overlay2d.vert.hlsl` (window pixels mapped onto clip space as librw's `im2d.vert` maps them) and
   `overlay2d.frag.hlsl` (the texel, white for flat colour, times the corner colours).
4. **The window.** The target is copied onto the swapchain's image (`SDL_BlitGPUTexture`) and presented; a hidden or
   minimised window gives no image, and the frame is still drawn, so it can be read back.

**What it matches.** The 2D pass's states of the GL3 renderer ([2D drawing](../research/graphics.md#2d-drawing)): no
depth test or write, no culling, no fog, blended by source alpha over what is below (alpha blended by the same factors,
as librw's `glBlendFunc` does). The alpha test is librw's, so both renderers drop the same fragments
(`graphics::alphaRangeOf()`): a triangle's own reference rounded to 8 bits, or none at 0; and for the quads, which the
GL3 renderer draws with librw's test as it stands, librw's state after its start, alpha at least 10 of 255, so a quad
fainter than that draws nothing on either renderer (Coney's choice, to match GL3; what the original's GS test is for
the 2D quads is not known). Each texture is sampled with its own filter, nearest for RenderWare's `NEAREST` and
`MIPNEAREST`, linear for the rest, clamped or repeated as the draw says. No mip levels are uploaded (stand-in: GL3 would
sample a sheet's own levels for a mip filter; 2D draws are near their textures' sizes, where level 0 is the one taken).

**Not yet:** the screen passes and `--line-blend` (item 20), the 3D scene (items 13 and 14), the developer overlay
(item 12), and the texture viewer's `RenderEngine::drawTexture`, which is not in the description. A frame the GPU
renderer cannot draw part of misses that part; it never fails.

## Textures {#textures}

A frame's textures are uploaded the first time a frame draws them and kept by `Texture::serial()`, a number no other
texture of the run has had, never by address, which a later texture may reuse (`graphics::UploadCache`,
`src/graphics/upload_cache.h`). A texture no frame has drawn for 600 frames (`GpuRenderer::kTextureKeepFrames`) is let
go. A frame whose upload buffer, vertex buffer or index buffer cannot be made or mapped forgets the textures it meant
to upload (`GpuRenderer::forgetUploads()`, `UploadCache::forget()`), so none stays marked as holding pixels it never
received and the next frame uploads it again. Two kinds are drawn:

- **Sprite sheets** (`SheetTexture`): their RGBA pixels from librw's raster once (`SheetTexture::toImage()`, rows top
  first, the PS2 alpha scaled to 0-255), with the filter of the dictionary's texture.
- **Pixel textures** (`PixelTexture`, `src/platform/pixel_texture.h`): RGBA kept in memory, uploaded again at each new
  revision. The movie player draws its frames into one on the GPU renderer (`PixelMovieScreen`; `makeMovieScreen()`
  gives each backend its screen, a librw raster for GL3), and the [test cards](../guides/running-viewers.md#test-cards)
  make theirs with it.

## The capture {#capture}

`--screenshot` (`RenderEngine::requestCapture()`) works on the GPU renderer: in the frame it names, the target is copied
into memory the CPU reads in the same command buffer, the buffer is submitted with a fence and waited for, and the
pixels are written as a PNG with alpha 255, as the GL3 renderer writes its read-back. The picture is the frame as drawn,
whether or not the window showed it. A build without the shaders (`-DCONEY_SHADERS=OFF`, [The shader
build](../guides/shaders.md)) cannot draw: its GPU renderer clears each frame to the frame's colour and presents, and a
capture fails saying so.

## The fallback {#fallback}

When no GPU device opens, the run log says so with SDL's reason (`could not start the GPU renderer: no GPU device opened
...; drawing with GL3 instead`) and the run draws with GL3. When the device opens but SDL refuses the 2D pipeline, the
run log says so and the GPU renderer only clears and presents.

## The proof {#proof}

`tests/platform/gpu_renderer_test.cpp` reads a synthetic clump and decodes a synthetic PS2 raster with librw opened this
way, headless; checks that a start with no GPU device fails with that message and leaves SDL's video stopped; and, where
a GPU device opens, starts the GPU renderer in a hidden window, presents, and draws a frame of flat and textured quads
and alpha-tested triangles whose capture it reads back pixel by pixel. `tests/graphics/overlay_batch_test.cpp` checks
the batch, the alpha ranges, the window's clear and the upload cache headless. On CI, every build job builds it, and the
Linux GCC job starts `coney --renderer gpu --frames 10` under lavapipe (Mesa's software Vulkan) on a virtual X display
and checks the run log's `renderer: gpu on vulkan` and `renderer: N of 10 frames shown` lines ([What CI
runs](../guides/building.md#what-ci-runs)). The [software pictures](../guides/testing.md#software-pictures) draw the
test cards on both renderers and compare them by the cross-renderer tolerance ([How pictures are
compared](renderer.md#comparing)).
