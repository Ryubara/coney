# Engine: the GPU renderer

Coney's renderer on SDL3's GPU API (Vulkan, Direct3D 12, Metal), which the [renderer design](renderer.md#backend) chose
to replace librw's GL3 device as it grows ([Migration](renderer.md#migration)). This page says how what is built so far
works: the start of [Order](renderer.md#order) item 3, which opens the window, the GPU device and the swapchain and
opens librw for data only, and item 11, which draws the frame description's 2D passes (the front end, the legal screens,
the movies, the HUD) with Coney's own shaders and reads the frame back for `--screenshot`, item 12, which draws Dear
ImGui's developer overlay over it, item 13, which draws the scene passes: the level's background, the streamed world,
the world objects and the parked cars ([below](#scene)), and item 14, which skins the humans with their held objects and
hats among them ([Skinning](#skinning)). It is Coney's own code with no counterpart in the original, which drew through
the PS2's GS. GL3 stays the default.

## The choice {#choice}

`--renderer gpu` or `gl3` wins; without it a windowed run takes the overrides file's `Display/Renderer` (0 GL3, the
default; 1 the GPU renderer), and test mode and `--render-references` take GL3 whatever is saved
(`takesSavedRenderer()`), so a test run never depends on a player's file (`chooseRenderer()`,
`src/core/renderer_choice.h`). The setting is read from the file before the renderer starts,
ahead of the full load of the overrides, and shows on the debug menus' Tunables page under *Display*; a change there
applies at the next start. The release build takes `--renderer` but keeps no overrides file. `--renderer gpu` is refused
with `--headless`, `--load` and `--render-references`: it needs a window, and the reference renders are GL3's
offscreen. It takes `--screenshot` and `--record` ([The capture](#capture)).

## The way back to GL3 {#way-back}

The GPU renderer draws the 2D layers, the pad's debug menus among them, and the developer overlay ([below](#overlay)),
so the Tunables page that sets `Display/Renderer` is reachable on it and a saved 1 starts it at the next start (a guard
that started GL3 instead, while it could not, is gone). It draws a level's background, world, objects, cars and humans
([below](#scene)), but not yet its effects (item 20 of [Order](renderer.md#order)). The release build has no overlay and
no overrides file, so there `--renderer gpu` is the only way to it.

To go back to GL3 for good, set *Display / Renderer* to 0 on the Tunables page and *Save overrides*, or delete the
`Display/Renderer = 1` line from the overrides file (`coney-tunables.ini` in your config folder, or the file
`--tunables FILE` names); `--renderer gl3` takes GL3 for one run whatever is saved.

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
the game runs; `drawsPixels()` is false, so nothing converts textures for librw's GL3 device, and what still draws
through librw (the effects) is not drawn (item 20 of [Order](renderer.md#order)). The humans come through the
description and are skinned by the GPU renderer ([Skinning](#skinning)).
`drawsScene()` is true, as on GL3 (`drawsSceneOn()`), so play loads the world objects and parked cars, their textures
held as read, for the scene passes ([below](#scene)). What draws the 2D layers runs on either windowed backend; the
movie player picks its screen with `makeMovieScreen()` by backend.

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

**Not yet:** the screen passes and `--line-blend` (item 20), the 3D scene's effects (item 20; the background, world,
objects, cars and humans are [below](#scene)), and the texture viewer's `RenderEngine::drawTexture`, which is not in the
description. The renderer also has no `gpu.frame` measure: GL3 takes it from OpenGL timer queries
([GPU time](../guides/performance.md#gpu-time)), and SDL3's GPU API has no timer or timestamp queries, so a bench of
`--renderer gpu` reports `gpu.frame none` (its CPU zones are all there). Its GPU time can be read only with a
platform's own tools until SDL adds them. A frame the GPU renderer cannot draw part of misses that part; it never
fails.

## Drawing the scene passes {#scene}

Item 13 of [Order](renderer.md#order): the frame description's scene passes, the level's background, the streamed
world's sectors, the world objects and the parked cars, drawn from Coney's own meshes and textures ([Mesh and texture
formats](asset-formats.md)) with Coney's own shaders, `scene3d.vert.hlsl` and `scene3d.frag.hlsl`, by `GpuScene`
(`src/platform/gpu_scene.*`) inside the GPU renderer's frame. What a sub-mesh is drawn with is worked out headless in
`graphics/scene_batch.h`, and its reference is what librw's GL3 device does with the same atomic, at the pin
`cmake/deps.cmake` names, so the two pictures can be compared ([How pictures are compared](renderer.md#comparing)); the
original's own drawing is the PS2's VU1 pipelines and the GS, which GL3 already stands in for.

**What it reads.** With the GPU backend there is no librw camera, so `WorldRenderer::render` places a camera of its own
(as the headless recording does) and the world pass's draws, objects and cars still go into the frame description; the
draws librw would make go nowhere, librw being open for data only ([above](#librw)). Each scene pass carries the camera
its draws are seen through (`graphics::SceneCamera`, set by `RenderEngine::setSceneCamera()` from the world pass's
view: `sceneCameraOf()`). The humans come as scene draws too since item 9, each with his pose (his skinning matrices,
`FrameDescription::bonesOf()`), the sphere the pose gave him (`Draw3d::bounds`) and his blood layer, and are skinned in
the vertex shader ([Skinning](#skinning)).

**The steps.** In the renderer's one command buffer:

1. **Prepare.** For each scene pass with a camera, each draw's mesh and dual layer are found in the asset store by id
   (`SceneAssets::store()`, which `RenderEngine::present()` hands down); a mesh or texture new to the GPU gets its
   buffers or texture, every level of it, and is queued for upload. Both are kept by id and let go after 600 frames no
   frame drew them (`GpuScene::kKeepFrames`); an id is never handed out twice, so a mesh that is gone is never asked
   for again.
2. **Upload.** The new meshes' vertices (Coney's `MeshVertex` as the store holds it) and 16-bit indices, and the new
   textures' levels, go up through the renderer's one transfer buffer after its own.
3. **Draw.** One render pass with a depth buffer (24-bit where the device has it, else 32-bit float) cleared to the
   far plane; the passes in their order, each 2D pass's draws (`OverlayBatch::passDraws`) and each scene pass's
   between them, every sub-mesh one draw per pass it needs (below). A scene pass that clears the depth first
   (`ScenePass::clearDepth`, the Z-only clear after the level's skyline) ends the render pass and begins another
   that keeps the colour drawn so far and clears the depth buffer to the far plane.

**What it matches.** Per draw and sub-mesh, as librw's GL3 device draws an atomic through Coney's level-of-detail step
(`platform/texture_lod.cpp`):

| Part | What GL3 does, and the GPU renderer with it | Where |
| --- | --- | --- |
| Camera | librw's view (the camera's frame inverted, its x flipped back) and perspective (the view window, the near and far planes); OpenGL's depth from -w to w taken onto 0 to w, so both depth buffers hold the same values | `viewMatrixOf()`, `projectionOf()` |
| Lights | none for a geometry without RenderWare's LIGHT flag; the ambients summed and times the material's ambient; the directional lights, then the point and spot lights whose sphere meets the mesh's (both only with normals), eight at most, times its diffuse; prelighting (black for a mesh without it) plus the lights, clamped, times the material colour | `lightsFor()`, `scene3d.vert.hlsl` |
| Colours | a sector's fade, an object's tint (with MODULATE while it is not opaque white), a car part's paint, a human's colour times his shadow dimming at his fade (`Shade`), as `MaterialChange` writes them into librw's materials; white for a mesh that does not modulate; a dual layer at its base's first alpha, a human's blood layer in his own colours | `coloursOf()`, `layerColoursOf()`, `dualColoursOf()` |
| Blending | on only for vertex alpha (prelighting not opaque), a material colour not opaque or a texture with a texel that is not; then the draw's blend (source alpha, or replace) and its alpha test as the render states stand at the draw, against librw's start reference (10 of 255) | `drawPassesOf()`, `vertexAlphaOf()`, `hasAlpha()` |
| GS alpha test | a first pass (alpha test "at least") of a blended mesh that writes depth is drawn twice, as librw emulates the GS's FB_ONLY: the fragments at or above 128 of 255 with depth written, then the rest without ([shared state](../research/rendering.md#shared-state)); a dual layer with the test off | `drawPassesOf()` |
| Depth and culling | tested less or equal (always while off but written), written as the pass says; back or front faces dropped, counter-clockwise the front | `GpuScene::pipelineFor()` |
| Fog | linear in the distance in front of the camera, from the camera's fog start to its far plane, toward the draw's fog colour | `fogAt()`, both shaders |
| Texture level | a PS2 texture picked by distance (log2 of the depth times 2^L plus K, never below 0) on the sectors and at level 0 elsewhere ([world](../research/rendering.md#world)); one of Coney's own as the GPU chooses; librw's GL3 filters for each RenderWare filter, its addressing per axis | `textureLevelsOf()`, `GpuScene::samplerFor()` |

Two parts follow librw's GL3 shader where it parts from RenderWare, until GL3 stops being the reference: a spot light
returns its own colour alone, dropping the lights before it in the list (librw's `header.vert`), and a bordered texture
is clamped (SDL's GPU API has no border colour). A material whose texture the store does not hold draws untextured
([texture lifetime](renderer.md#texture-lifetime)).

**The render states it reads.** A draw takes the render states in force when it is recorded (`sceneStatesInForce()`,
and the world pass's own), read from the draw layer, which on the GPU backend does not forward to librw and keeps them
itself ([How pictures are compared](renderer.md#comparing)). A state nobody has set reads back as librw's GL3 device
starts it (`resetRenderState` in librw's `gl3device.cpp`; `gl3StartStates()` in `draw_calls.cpp`): above all the alpha
test, "at least 10 of 255", which play never sets. Read as 0 ("always"), as it was at first, the see-through fence by
the start of `level99` wrote depth over its clear texels and hid the `d` world drawn after it (the post under the red
sign, a lit window, the wall behind the fence), and what showed through instead looked brighter than on GL3.

**The background** ([#1527](issue:1527)): the sky box, the cloud box, the skyline and the level
world's light glows go into the frame description as scene draws, in the original's order
([The level in a frame](../research/level-loading.md#render-order): `LevelObject_RenderBackground`, `0x0040d0a8`,
then the glows first in `WorldManager_Render`, `0x0040e8d8`). `WorldRenderer::renderBackground` moves librw's camera
and the description's together (`moveCameras()`): the sky and the clouds are a scene pass through a camera at the
origin with the view's axes (near 0.05, far 5), the skyline one through the view's own position (near 39 or the
nearest missing scenery, far 560), then `RenderEngine::clearSceneDepth()` starts the world pass's scene pass, which
clears the depth first, and the glows open it through the world pass's camera. Each part is drawn by
`drawBackgroundPart()` in the render states in force, with the background's lights (`SceneLighting::backgroundLights`:
the world's ambient and directional lights) and `Material3d{.colour = Own, .lit = true}`: its colours as loaded, and
lit though its models carry no light flag, as the original lights them ([Lighting](../research/lighting.md#coneys-implementation));
GL3 sets the geometry's LIGHT flag for the draw (`MaterialChange`) and the GPU renderer takes the lights though
`MeshData::lit` is off (`lightsFor(..., alwaysLit)`). The level's models hold their meshes and their materials'
textures in the scene assets while the level object holds them (`holdLevelMeshes()`, from `loadLevel()` before any
conversion for drawing); a model with no mesh id, or any while the mesh views are on, draws through the lighting rig
as before. The GL3 renderer draws the parts and clears the depth at once, through librw's camera as placed, so its
draw stream is the one it was (the `keeps` hash of a Coney-made scene with a level in
`tests/platform/world_draw_stream_test.cpp`).

**Not yet:** the effects drawn through librw (the water, the see-through panes, the throw arc, particles, blob shadows,
rings and coronas), item 20. A frame on `--renderer gpu` misses them; `--scene-only` leaves them out on both renderers,
and draws the background, so that the frames can be compared on the part both draw ([How pictures are
compared](renderer.md#comparing)).

**The World page** ([debug menu guide](../guides/debug-menu.md#world)): its fog reaches the GPU renderer as it does GL3,
through the camera's fog start and the draws' fog colour, and the parts the play mode's passes skip before a draw is
recorded (the glass) or hands to the world renderer (the Background) hide there too; the effects it hides are not drawn
there yet anyway (above). The Scenery, Humans and Objects parts do not ([#1545](issue:1545)): they hide through the mesh
views' hook, and while it is set (any of the three hidden, or any mesh view on) the sectors, the objects and the humans
are drawn through librw instead of being recorded (`SceneLighting::meshViewsOn()`), so on `--renderer gpu` hiding one of
them leaves the others out too. The mesh views themselves are GL3's.

## Skinning {#skinning}

Item 14 of [Order](renderer.md#order) ([#1206](issue:1206)): the humans, posed, drawn by the same shaders as the rest of
the scene. It ports the GL3 renderer's skinning pipeline (`platform/character_skin.cpp`, [GPU
skinning](../guides/performance.md#gpu-skinning)), Coney's own: the original skins on the PS2's vector unit.

**The data.** A character's two meshes (the body and the blood layer, `CharacterMesh`) are converted with each vertex's
four bones and weights (`SceneAssets::addAtomic()` with the mesh's `packSkinVertices()`, into `MeshVertex::bones` and
`weights`), and the character's texture is held in the scene assets with them, so his materials name it; the three
blood textures (`BloodTextures`) are held there too, so a blood layer is drawn with its texture. On the GPU backend
(`RenderEngine::skinsInShaders()`, `SceneAssets::skinsInShaders()`) a character's mesh is posed as on GL3's skinned
pipeline: its bone palette and the sphere round its bones' boxes (`posedBox()`), no vertex blended on the CPU, so the
lights are chosen by the same sphere on both renderers and `render.skin` holds only the pose. Play poses the humans,
their hats and held objects when the backend draws the scene (`drawsScene()`), not only on GL3.

**The palette.** Each frame's poses, every skinned draw's skinning matrices in the order the draws were added
(`FrameDescription::bones()`), go up at once into one storage buffer, each bone four columns of a 4 x 4 matrix as GL3's
palette holds them (`bonePaletteOf()`; the buffer is cycled, so the frame the GPU still reads is never overwritten, and
grows as a frame needs; `GpuBonePalette`, `src/platform/gpu_bone_palette.*`). A draw names its first bone
(`Draw3d::firstBone`) in its uniforms. Because the palette is one plain array the frame fills, the posing can later move
to threads or to a compute pass without changing the shader.

**The shader.** `scene3d.vert.hlsl` skins a posed draw first: each slot with a weight moves the position and turns the
normal by its bone, times the weight, summed (weights a hair below 1 kept as stored), the normal then made unit, then
the draw's transform places the vertex, as the GL3 renderer's skinning shader does (`skinVertex()` is its CPU twin, tested
against `characters::skinVertices()`). Every other draw has no weights and skips it.

**What it matches.** A posed draw is drawn only when its mesh carries bones and weights and the pose holds every bone its
vertices weigh (`posingOf()`, `bonesNeededBy()`); otherwise it is left out rather than drawn in its bind pose. His
lights are tested against his posed sphere (`boundsOf()`, `lightsFor()`), as librw tests his atomic's. His blood layer
is the GL3 renderer's blood pass (`Gl3Renderer::drawHuman()`, GS context 2 of
[rendering](../research/rendering.md#characters)): in his own colours (`dualColoursOf()`), blended by source alpha with
the alpha test off, his depth test, depth write and culling (`dualStatesOf()`). `--cpu-skinning` is GL3's: the GPU
renderer always skins in its shader.

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

## The developer overlay {#overlay}

The overlay (F1, [the debug menu guide](../guides/debug-menu.md#the-developer-overlay)) draws through Dear ImGui's
`imgui_impl_sdlgpu3` backend on this renderer and through `imgui_impl_opengl3` on GL3 (`ImGuiOverlay::start()` chooses
by `RenderEngine::backend()`); both sit under the same `imgui_impl_sdl3` platform backend, which reads the window's
events and size. Dear ImGui's own shaders (embedded in the backend, for SPIR-V, DXBC and MSL) do the drawing, so the
overlay needs none of Coney's.

The overlay is drawn *into the frame*, after the 2D passes and before the copy to the window: the pad menu draws over
every frame through the frame description, and `ImGuiOverlay::draw()` (called from the present overlay, still inside the
frame) only builds Dear ImGui's draw data. `GpuPresenter::present()` then hands the command buffer and the renderer's
target to each drawing added with `RenderEngine::addGpuOverlay()`, in the order they were added; the overlay makes its
own Dear ImGui context current, uploads the draw data and draws it in a second render pass over the target (load, not
clear). Any number of drawings may be added (the F8 console bar has its own context beside F1's), each removed by the
handle `addGpuOverlay()` gave, and removing one leaves the others drawing. Because the target is also what
`--screenshot` reads back, a capture holds the overlay on both renderers, and a frame with it hidden is byte-identical
to one without it (`--dev-overlay N` with `--screenshot`). The overlay is sized by the window, so the target and window
must be the same size, as they are.

It needs the 2D drawing: where that could not be made (`gpuDrawingProblem()`), `start()` fails and the run log says so.
It stops before the renderer does (the overlay waits for the GPU, shuts the backends down, and removes its drawing).

## The capture {#capture}

`--screenshot` (`RenderEngine::requestCapture()`) works on the GPU renderer: in the frame it names, the target is copied
into memory the CPU reads in the same command buffer, the buffer is submitted with a fence and waited for, and the
pixels are written as a PNG with alpha 255, as the GL3 renderer writes its read-back. The picture is the frame as drawn,
whether or not the window showed it. A build without the shaders (`-DCONEY_SHADERS=OFF`, [The shader
build](../guides/shaders.md)) cannot draw: its GPU renderer clears each frame to the frame's colour and presents, and a
capture fails saying so.

`--record` ([Recording a clip](../guides/running-options.md#record)) works on it too: while a frame tap is set
(`RenderEngine::setFrameTap()`), every frame's target is also copied before the overlays draw over it (the developer
overlay, the console bar), into a second buffer (`GpuRenderer::ReadBack::Game`, the screenshot's being `Picture`, after
them), waited for with the same fence, and the logical screen's rectangle of it handed to the tap
(`RenderEngine::tapGpuFrame()`), as the GL3 renderer reads that rectangle from its frame buffer before the debug menus.
So a clip on either renderer shows the game alone, and `clips pair --renderer gpu` records the GPU renderer's picture.
Each recorded frame waits for the GPU, which test mode's fixed step allows. A frame whose copy cannot be made or read
is left out of the clip with a warning in the log, so a clip one frame short says why.

## What the scene assets cost {#scene-assets}

`WorldSet` converts every texture of a dictionary and every sector's mesh into Coney's own formats as the world and its
parts load ([the frame description](renderer.md#frame-description)), and only a renderer that reads the converted data
needs that: GL3 draws the librw atomic a mesh id names and reads none of it. Measured on the `playtest` build (the
bench's `scene_assets_*` fields, 120 frames of `--play-level` from a loading screen, the whole level's parts loaded by
the end, two runs each with identical counts, and the time of the world's reads and parts with and without the
conversion), confirmed (runtime):

| Level | Textures, meshes | Held at most | Conversion | The world's load, without and with it |
| --- | --- | --- | --- | --- |
| `level95` (the hub), 49 parts | 621, 198 | 25.7 MB (9.8 texels, 16.0 meshes) | 22.2-22.5 ms | 153-176 ms, 178-182 ms |
| `level99` (the street), 31 parts | 510, 118 | 18.3 MB (8.3 texels, 10.1 meshes) | 18.3-18.5 ms | 104-109 ms, 125-129 ms |

So the conversion's own time was 12 to 17 per cent of the world's load (22 ms of 178-182 ms, 18 ms of 125-129 ms) and
it held a second CPU copy of 18 to 26 MB, for nothing under GL3. The load column is one-off wall clock from a timer
that was not kept: the hub's ranges overlap, so the difference between them is within noise, and only the counts, bytes
and conversion times are reproduced by the bench report. It is therefore done only when
`RenderEngine::convertsSceneAssets()` says so: with GL3 (`RenderBackend::OpenGl`) the scene assets hand a mesh an id for
its atomic and convert nothing ([#1422](issue:1422)); the NULL backend, whose tests read the data, and the GPU renderer
convert as before.

## The fallback {#fallback}

When no GPU device opens, the run log says so with SDL's reason (`could not start the GPU renderer: no GPU device opened
...; drawing with GL3 instead`) and the run draws with GL3. When the device opens but SDL refuses the 2D pipeline, the
run log says so and the GPU renderer only clears and presents.

## The proof {#proof}

`tests/platform/gpu_renderer_test.cpp` reads a synthetic clump and decodes a synthetic PS2 raster with librw opened this
way, headless; checks that a start with no GPU device fails with that message and leaves SDL's video stopped; and, where
a GPU device opens, starts the GPU renderer in a hidden window, presents, and draws a frame of flat and textured quads
and alpha-tested triangles whose capture it reads back pixel by pixel; and that the GPU backend draws the scene, so
play loads its objects and cars on it. `tests/graphics/scene_batch_test.cpp` checks headless what each sub-mesh is
drawn with against librw's GL3 maths, the skinning formula against `characters::skinVertices()` among them,
`tests/platform/gpu_scene_test.cpp` draws a scene pass on the GPU, a skinned one too, a pass that clears
the depth and a lit draw of an unlit mesh, and reads it back, `tests/platform/world_draw_stream_test.cpp`
that a level's background is recorded before the world through its own cameras,
`tests/platform/human_draw_stream_test.cpp` that a human's meshes carry his bones, weights and texture and his draw his
posed sphere, and `tests/platform/draw_calls_test.cpp` that the headless draw layer's states start as librw's GL3
device's do.
`tests/platform/imgui_overlay_test.cpp` runs the developer overlay on each renderer in a hidden window, stepping every
frame itself: closed, its frame is the background; opened, a capture shows it; closed again, the frame is byte-identical
to the first. `tests/graphics/overlay_batch_test.cpp` checks the batch, the alpha ranges, the window's clear and the
upload cache headless. On CI, every build job builds it, and the Linux GCC job starts `coney --renderer gpu --frames 10`
under lavapipe (Mesa's software Vulkan) on a virtual X display and checks the run log's `renderer: gpu on vulkan` and
`renderer: N of 10 frames shown` lines ([What CI runs](../guides/building.md#what-ci-runs)). The [software
pictures](../guides/testing.md#software-pictures) draw the test cards, the 3D `scene` card among them, on both renderers
and compare them by the cross-renderer tolerance ([How pictures are compared](renderer.md#comparing)).
