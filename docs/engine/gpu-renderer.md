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
the game runs; `drawsPixels()` is false, so nothing converts textures for librw's GL3 device, and the effects,
which the frame description holds as effect passes ([renderer](renderer.md#frame-description)), are not drawn (item 20
of [Order](renderer.md#order)): the GPU renderer reads none of them yet. The humans come through the description and
are skinned by the GPU renderer ([Skinning](#skinning)).
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

**Not yet:** the screen passes and `--line-blend` ([#1646](issue:1646)), and the texture viewer's
`RenderEngine::drawTexture`, which is not in the description. The renderer also has no `gpu.frame` measure: GL3 takes it
from OpenGL timer queries ([GPU time](../guides/performance.md#gpu-time)), and SDL3's GPU API has no timer or timestamp
queries, so a bench of `--renderer gpu` reports `gpu.frame none` (its CPU zones are all there). Its GPU time can be read
only with a platform's own tools until SDL adds them. A frame the GPU renderer cannot draw part of misses that part; it
never fails.

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
| Depth and culling | tested less or equal (always while off but written), written as the pass says; back or front faces dropped, counter-clockwise the front; what lies past the far plane or before the near one cut away, as OpenGL clips it (SDL's default clamps the depth instead, which drew the level world's far light glows as fogged disks over the sky) | `GpuScene::pipelineFor()` |
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
conversion for drawing); a model with no mesh id, or on GL3 any while a hook is set, draws through the lighting rig
as before. The GL3 renderer draws the parts and clears the depth at once, through librw's camera as placed, so its
draw stream is the one it was (the `keeps` hash of a Coney-made scene with a level in
`tests/platform/world_draw_stream_test.cpp`).

**The effects** (the water, the see-through panes, the throw arc, particles, blob shadows, rings and coronas) are drawn
in the same render pass since item 20 ([below](#effects)); `--scene-only` leaves them out on both renderers, and draws
the background, so that the frames can be compared on the part both drew before ([How pictures are
compared](renderer.md#comparing)).

**The World page** ([debug menu guide](../guides/debug-menu.md#world)): its fog reaches the GPU renderer as it does GL3,
through the camera's fog start and the draws' fog colour, and each part hides there as on GL3. The play mode's own
passes skip the glass, the humans and the objects before a draw is recorded, and hand the Background to the world
renderer; the effects it hides (the water, the effects, the rings, the coronas, the blob shadows) are not drawn, so
never recorded, and hide here as on GL3. The streamed world's sectors are drawn by the
ported `WorldManager_Render`, so the Scenery part reaches them through the mesh views' hook: the hook says which kinds
it hides (`MeshViewHook::hides()`) apart from whether it draws passes of its own (`MeshViewHook::hasPasses()`). The play
mode hands the hidden kinds to the renderer for the frame (`RenderEngine::hideKindsOf()`), and
`RenderEngine::drawScene()` leaves out every recorded draw made in the scope of a hidden kind (`DrawKindScope`), so on
this renderer the sectors, the objects and the humans are still recorded and hiding one kind never takes another with
it. The level's background (the sky, the clouds, the skyline and the light glows, recorded as above) is drawn in the
same scenery scope as the sectors, so *Streamed world* off hides it too, as the guide says and as on GL3, while the
*Background* part hides it alone; *Humans* and *Objects* off leave it. GL3 is as it was: with a hook set (a part hidden
or a view on) the sectors, the background, the objects and the humans go through the lighting rig for it, which hides
the kind there (`SceneLighting::meshViewsOn()` is true whenever a hook is set and the play mode left its passes on), and
nothing of them is recorded; the GPU renderer says its passes off, so no hook sends a model through the rig there and
the mesh views' passes are recorded instead ([below](#mesh-views)). The tests record the frame description for each
kind (`tests/platform/world_draw_stream_test.cpp`, `object_draw_stream_test.cpp`, `human_draw_stream_test.cpp`; the
sectors also on a real GPU device).

### The mesh views {#mesh-views}

The wireframe, the flat views and the lines of the Debug draw page ([debug menu
guide](../guides/debug-menu.md#mesh-views), [#1599](issue:1599)), Coney's own debug tooling. On GL3 each view pass is a
draw of the model's own librw atomic through the lighting rig, which draws nothing on this backend, so the play mode
leaves the rig's passes off here (`drawsMeshViewPassesOn()`, false for `RenderBackend::Gpu`) and the views are drawn
from the frame description instead. The views set the hook when the wireframe or a mesh view other than shaded is on (or
a part is hidden); the normals and bone axes alone do not, being lines.

- **Recorded passes.** `RenderEngine::drawScene()` asks the hook it was given by `hideKindsOf()` for the passes of each
  model it records (`MeshViewHook::passesFor()`: the kind the draw scope names, the mesh id, each material's texture
  name and alpha), then records the shaded draw if the hook gives one, a Flat draw with a colour for each material and a
  Wire draw with one colour, in that order, each of the same mesh with the same transform, lights, states and pose
  (`Draw3d::view`; the colours are a range of the frame, `FrameDescription::viewColoursOf()`). No second layer rides
  with a Flat or Wire draw: a human's blood layer and an object's dual layer are drawn only with the shaded draw. A
  human's blood layer is the exception to the order: GL3 draws it as a second atomic after the body's passes, so where
  it blends it covers the wireframe, and with a wire over a shaded human the engine records the body without it, then
  the wire, then the layer alone (`ViewPass::DualLayer`). The selection is the model whose mesh id the hook holds
  (`MeshViews::selectMeshes()`, set from the Entity inspector's selection by `beginFrame()`); the draw-call colours
  count on over every material of every flat draw of the frame in the order recorded, as GL3 counts them in the order
  drawn; the LOD view reads the alpha the draw gives the material (`coloursOf()`), as GL3 reads the librw material's.
  The rules and the colours stay the debug tools' (`debug::meshPlanFor()`); the game links nothing of them.
- **Drawing them.** `GpuScene` draws a Flat or Wire draw like any other with the changes GL3's pass makes
  (`viewColoursOf()`, `viewLightsOf()` and `viewStatesOf()` of `graphics/scene_batch.h`): every material in the view's
  colour, opaque, with no texture and the mesh modulated by it; one white ambient light, the material's ambient at 1 and
  no other light, so a vertex is white before the colour modulates it; no fog; the alpha test passing every fragment; a
  Wire draw writing no depth. A Wire draw's pipeline fills lines (`SDL_GPU_FILLMODE_LINE`) with a depth bias of -1 and a
  slope of -1, as OpenGL's polygon offset of (-1, -1) pulls GL3's, so the lines sit on the fill; culling is the draw's,
  as OpenGL culls polygons drawn as lines too. Stand-in: the two APIs count the bias in their own units (OpenGL in the
  depth buffer's smallest steps, SDL passes the constant factor on as the API takes it), so how far a line is pulled
  differs a little between drivers.
- **Lines.** GL3 draws the normals and the bone axes, Debug draw lines, as one effect draw of lines (`LineList`). Here
  `PlayLevelMode::drawSceneLines()` hands them to `RenderEngine::drawSceneLines()`, which adds them to the scene pass
  the frame's draws are in (`ScenePass::lines`, two vertices to a line, `SceneLineVertex`), and `GpuScene` draws them
  after that pass's meshes with the same shaders as a line list: depth tested, not written, never fogged or culled, in
  the vertex colour alone. So every other line of the Debug draw page (the collision wireframe, the headings, the
  places) is drawn on this renderer too.

**Against GL3** (confirmed (runtime), `picture compare` in the task's scratch folder, Direct3D 12 on the agents' Windows
machine, `level99` at checkpoint 2, frame 100, 640x448, `--freeze-world`). With `--scene-only`, which leaves out what
the GPU renderer does not draw yet and, with it, the normals and bone axes, seven frames of the views: the four flat
ones (by material, by draw call, by LOD on the humans, by material on the selection) have 3 or 4 pixels over 4 of 255
(largest difference 192, 156, 67 and 67, single pixels along a polygon's edge), and the wireframe on everything over the
shaded scene 4 (largest 187). The wireframe over the shaded fill had 1,772 over until the blood layer was recorded after
the wire: the GPU drew the human's layer first and left his lines at full colour where GL3's layer dims them, which the
difference picture showed on the human alone. The two frames of lines alone differ more: wireframe only has 2,300 pixels
over (0.8 %, largest 203) and the humans' wireframe in a wireframe-only view 3,110 (1.1 %, largest 209), and 95 % of
those pixels have the other renderer's colour within a pixel of them (checked on both frames), so a line is covered a
pixel over where Direct3D and OpenGL rasterise a one-pixel line differently. These two are over the cross-renderer
tolerance of 0.2 % and are listed here with that reason.

The whole frame, without `--scene-only`, differs from GL3 by what the GPU renderer does not draw yet (the blob shadows,
the health rings, the coronas): the street frame with every view off, 10,583 pixels over 4 (3.7 %, largest 161); with
the humans' wireframe in a wireframe-only view and the selection's normals and bone axes on 13,862 (4.8 %), and with the
normals and bone axes alone 10,570 (3.7 %). GL3's own frames, branch against base, are the same in all nine frames (the
seven above and those two), and the GPU frame with every view off, 0 pixels over 2.

`tests/graphics/mesh_view_draw_test.cpp` checks the passes' colours, lighting and states and the frame description's
colours and lines without a window; `tests/platform/mesh_view_scene_test.cpp` checks on the NULL backend, which records
as this one does, what lands in the frame description for each pass, a hidden kind, a posed model and a human's blood
layer; `tests/platform/mesh_views_passes_test.cpp` checks the debug tools' answers and, on a hidden GPU window, the
pixels of each view, of the blood layer over the wire and of the lines read back.

## Drawing the effect passes {#effects}

Item 20 of [Order](renderer.md#order) ([#1212](issue:1212)): the frame description's effect passes (`EffectPass`: the
particles and debris, the glints, the ground fog, the water, the throw arc, the see-through panes, the blob shadows, the
health rings, the coronas and the debug tools' lines, [Built for the effects](renderer.md#frame-description)), drawn by
`GpuScene` with the scene's own shaders, in the same render pass and depth buffer as the scene passes, so an effect is
hidden by what the world drew in front of it and a later scene draw by what the effect wrote. What each draw is drawn in
is worked out headless in `graphics/effect_batch.h`; its reference is what librw's GL3 device does with the same Im3D
primitive (the GL3 renderer's `drawEffect()`), at the pin `cmake/deps.cmake` names.

**What it reads.** A draw sets only some states (`EffectStates`, each optional), as the game's effects always have, and
leaves the rest as they stand. With the GPU renderer nothing is drawn through librw, so the engine records the states in
force before each draw with it (`EffectDraw::inForce`, read from the draw layer by `effectStatesInForce()` in
`RenderEngine::drawEffect()`). So that the draw layer holds what GL3 would leave there, the GL3 renderer sets each effect
draw's states and its restores, and each scene draw's states, on every backend, and draws only where it has something to
draw into.

**The steps.** In `GpuScene::prepare()`, for each effect pass with a camera, each draw's corners become unlit vertices of
the scene's format (`graphics::meshVertexOf()`: the corner's colour as the prelighting, its texture coordinate), and its
indices (or the corners in order) go into one index buffer for the frame, each draw drawn from its first corner. Both
buffers are kept, grown by doubling, and rewritten each frame. Each draw's texture is one of the renderer's 2D textures
(the frame's table: a sprite sheet, a pixel texture), uploaded as the 2D draws' are, with whether a texel is not opaque.

**What it matches.** Per draw, as librw's GL3 device draws an Im3D primitive (`graphics::effectLookOf()`):

| Part | What GL3 does, and the GPU renderer with it | Where |
| --- | --- | --- |
| States | each depth, culling and fog state the draw's own, else the one in force | `effectLookOf()` |
| Blending | on only for the vertex alpha (the draw's or the one in force) or a texture with a texel that is not opaque, then by the draw's blend (alpha, or added: source alpha over one) or the one in force; off, the draw replaces what is below | `effectLookOf()`, `GpuScene::pipelineFor()` |
| Alpha test | on with the blending only: the test in force against librw's reference after its start (10 of 255), every fragment kept while it is off | `effectLookOf()` |
| Colour | the corner's colour times the texel (white for no texture and for a line list, which librw draws untextured), fogged toward the fog colour in force by the pass camera's planes | `scene3d.vert.hlsl`, `scene3d.frag.hlsl` |
| Shape | a triangle list, or a line list one pixel wide | `GpuScene::pipelineFor()` |

**Stand-ins.** A draw that sets no filter is sampled with its texture's own filter, and one that sets no addressing is
clamped: GL3 samples with the filter and addressing the render states hold, which librw's own atomic draws change
without the draw layer seeing it, so what they hold at an effect is not known to the description. The game's effects
that sample a texture set both, except the coronas, the blob shadows, the panes and the rings
(`SceneLighting`'s sprites), whose textures' own filters are used. The states a 2D pass sets before an effect are not
mirrored on the draw layer (the effects come before the 2D layers in a frame).

**Not yet:** the screen passes ([#1646](issue:1646)). `--scene-only` still leaves the effects out on both renderers, so
the part both drew before this item can be compared alone. `tests/graphics/effect_batch_test.cpp` checks the states,
blending, alpha test and vertices headless; `tests/platform/gpu_effect_test.cpp` draws sprites blended and added, a
surface in the fog in force, a sprite behind a mesh, a texture with alpha and a line on a hidden GPU window and reads
them back; `tests/platform/frame_recording_test.cpp` checks the states recorded in force; the `effects` [test
card](../guides/running-viewers.md#test-cards) draws them on both renderers in CI's software pictures.

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
the depth, a lit draw of an unlit mesh and a quad past the far clip, and reads it back, `tests/platform/world_draw_stream_test.cpp`
that a level's background is recorded before the world through its own cameras,
`tests/platform/human_draw_stream_test.cpp` that a human's meshes carry his bones, weights and texture and his draw his
posed sphere, and `tests/platform/draw_calls_test.cpp` that the headless draw layer's states start as librw's GL3
device's do.
`tests/platform/imgui_overlay_test.cpp` runs the developer overlay on each renderer in a hidden window, stepping every
frame itself: closed, its frame is the background; opened, a capture shows it; closed again, the frame is byte-identical
to the first. Each test that saves a capture keeps it in a folder of its own, so copies of it running at once never
share the file ([a path of its own](../guides/testing.md#capture-paths)). `tests/graphics/overlay_batch_test.cpp` checks
the batch, the alpha ranges, the window's clear and the upload cache headless. On CI, every build job builds it, and the
Linux GCC job starts `coney --renderer gpu --frames 10` under lavapipe (Mesa's software Vulkan) on a virtual X display
and checks the run log's `renderer: gpu on vulkan` and `renderer: N of 10 frames shown` lines ([What CI
runs](../guides/building.md#what-ci-runs)). The [software pictures](../guides/testing.md#software-pictures) draw the
test cards, the 3D `scene` card among them, on both renderers and compare them by the cross-renderer tolerance ([How
pictures are compared](renderer.md#comparing)).
