# Restarting the renderer

Verified against: librw at the commit pinned in `cmake/deps.cmake` (`18532c2e13efbc1aa43f0b00b4459831f9414de0`), its
`src/gl/` files (and `src/gl/glad/glad.c` for the glad row); this page is about Coney's renderer, not the game's
executable, and has no `SLUS_212.15` address. Runtime claims are Coney's own tests, run on Windows on 2026-10-22 and
2026-10-23 (the glad row's is `a flat pass on the NULL backend leaves librw alone though OpenGL was loaded before`, in
`tests/platform/mesh_views_test.cpp`, run on 2026-10-31).

## Purpose

Coney's rule: after any `RenderEngine` stops, nothing of it remains that a later one could meet. A change of display,
of video settings or of mode (a co-op screen too) can then stop the renderer and start another in the same
process. A game starts one renderer a process today; the rule is for what is built on it. It came from issue #757:
`coney.frame_copies_upright`, which starts the renderer once per display, crashed now and then with a segmentation
fault, because the second start read a shader the first had destroyed.

## Original structure

None. The original's renderer (RenderWare for the PS2) is set up once at boot and never stopped
([Graphics device and textures](graphics.md)). The code this page maps is librw's OpenGL 3 device and Coney's
`RenderEngine` (`src/platform/render_engine.cpp`, `RenderEngine::start` and `RenderEngine::shutDown`).

## Data

Every librw OpenGL global that outlives `Engine::stop`, and what Coney does for it. Evidence for each row is
confirmed (code): librw `src/gl/gl3device.cpp`, `gl3shader.cpp`, `gl3immed.cpp`, `gl3raster.cpp`, `gl3matfx.cpp`,
`gl3skin.cpp` and `texture.cpp` at the pinned commit. The rows marked (Coney test) were also seen going wrong in
Coney's own tests before the fix; no PCSX2 observation is involved. A row cites the function or line where its claim
can be read; line numbers are those of the pinned commit.

| Global | What happens after a stop | Coney's action |
| --- | --- | --- |
| `currentShader` (the shader in use) | Dangling: `Shader::destroy` never clears it, and the next renderer's first `Shader::create` reads through it (Coney test: the restart crash of `coney.frame_copies_upright`, with a stack) | Set to null after `Engine::stop` |
| `im2dOverrideShader` (a 2D shader a program may set) | Dangling if set (`Shader::destroy` never clears it, `gl3shader.cpp:352-358`; every 2D draw calls `im2dOverrideShader->use()` when it is set, `gl3immed.cpp:139-140`, `:173-174`); Coney sets none. Coney test: the restart test sets one, and without the fix its first report is the failed check `im2dOverrideShader == nullptr` after the next start; in 4 of 20 runs of the restart test built without the fix (Windows, Debug build) the first frame then died with SIGSEGV, the other 16 only failing checks; the faulting read was not located, so that the freed override caused it is inferred from the draw code above) | Set to null after the stop |
| `boundTexture[8]` (the cache of what is bound on each unit) | Never reset by librw: `resetRenderState` (`gl3device.cpp:977-983`, called from `initOpenGL` at `:2029`) sets `activeTexture` to -1 and then to 0 through `setActiveTexture`, so the active unit is issued afresh, but nothing clears `boundTexture[]`. It binds the white texture on each unit through `bindTexture`, which skips the call when the cached name equals the one asked for, and the new context's white texture gets the first name again, so units 1 to 7 stay on texture 0 while librw believes white is bound (Coney test) | After the start, white is bound on every unit directly in OpenGL, so reality matches the cache (`bindWhiteTextureOnEveryUnit`, which asserts that unit 0 is active, where `resetRenderState` leaves it, and that white is a name) |
| `currentFramebuffer` (the cached bound framebuffer) | A name left cached stops the next context's first bind of the same name: `bindFramebuffer` skips the call when the name matches (`gl3device.cpp:493-499`) (Coney test) | Bound to 0 through librw before the stop, while the context exists |
| `rwStateCache.fogEnable` | Carries over (`setRenderState`, `FOGENABLE`, `gl3device.cpp:729-735`); the flush rebuilds the fog data from it (`flushCache`, `:1107-1110`), so nothing is drawn wrong, but a restart would not start from a fresh process's state (Coney test) | Set to 0 (`FOGENABLE`) before the stop |
| The fog colour in `uniformState` and in the registry (`u_fogColor`) | `resetRenderState` sets `uniformState.fogColor` to white but marks nothing changed, so the registry that new shaders upload from keeps the last colour set (zeros in a first renderer) (Coney test) | `FOGCOLOR` set to white after the start, which marks it changed: the first draw puts white in the registry |
| `uniformStateDirty[]` for fog data, fog start and end, and `rwStateCache.fogStart`, `fogEnd` | Carry over; every flush rebuilds the fog data from the cache. The planes are marked changed only when the camera's differ from the cache (`gl3device.cpp:1352-1361`), so a restart with the same planes uploads nothing: the uniform registry keeps the values the last upload put there, and the next renderer's shaders read those | Nothing needed |
| `alphaFunc`, `alphaRef`, `rwStateCache.alphaFunc`, the alpha-test flags | Set again by `resetRenderState`, which also marks `ALPHAREF` changed; the flush that follows rebuilds the alpha test uniform from them | Nothing needed |
| `stateDirty`, `sceneDirty`, `objectDirty`, `lastShaderUploaded` | `stateDirty` is set again by `resetRenderState`; the others are set again by the matrix setters, which every camera update calls; `lastShaderUploaded` is set to null by `initOpenGL` | Nothing needed |
| `rwStateCache.texstage[8].raster` | Names the raster held in a stage. The GL3 raster plugin's destructor `destroyNativeRaster` clears it through `evictRaster` (`gl3raster.cpp:824-829`; `Raster::destroy` runs it through the plugin list, `raster.cpp:98`); a raster alive at the stop would leave it dangling, which the raster rule below excludes | Nothing needed; the rule is asserted |
| `rwStateCache.texstage[8].filter`, `addressingU`, `addressingV` | Carry over; they are the filter and addressing the program last asked for, applied to the next raster that is set | Nothing needed |
| Other `rwStateCache` and `curGlState`, `oldGlState` | Reset by `resetRenderState` in librw's init (`oldGlState` is filled with a value no state has, so every GL state is applied afresh) | Nothing needed |
| `uniformState`, `uniformScene`, `uniformObject`, `uniformData` and the shaders' serial numbers | Carry over except as the rows above say; a new shader starts with serial numbers that force an upload of what the registry holds (`Shader::create`, `gl3shader.cpp:322`) | Nothing needed |
| `uniformRegistry`, its name buffer, the `u_*` ids | Registering a name twice returns the same slot (`registerUniform`, `gl3shader.cpp:44-53`); the ids stay valid | Nothing needed |
| `defaultShader*`, `im2dShader`, `im3dShader`, `whitetex` | Destroyed and set to null (the shaders) or 0 by librw's term step (`termOpenGL`, `closeIm2D`, `closeIm3D`) | Nothing needed |
| `im2DIbo`, `im2DVbo`, `im3DIbo`, `im3DVbo`, `im2DVao`, `im3DVao`, the device's `vao` | The buffers and arrays are deleted by the term step, or die with the context; the integers keep their old values until `openIm2D`, `openIm3D` and `initOpenGL` make new ones, and nothing reads them between | Nothing needed |
| `shaderDecl`, `maxAnisotropy`, `gl3Caps.maxAnisotropy`, `gl3Caps.dxtSupported` | Set again by `initOpenGL` from the new context (`shaderDecl` points at a string literal) | Nothing needed |
| `glGlobals`: window, context, display modes | `glGlobals` is one file-scope object that nothing clears. `openSDL3` (at `Engine::open`) sets the window size and title, the display and rebuilds the mode list; `startSDL3` sets `window`, `glcontext` and the present size, and only after a window and context were made. `currentMode` and `numSamples` are never reset: they are set only by `Engine::setVideoMode` and `Engine::setMultiSamplingLevels`, and `startSDL3` reads them (`gl3device.cpp:1729`, `:1731`). `numModes` is set to 1 only when SDL reports a current display mode (`:1656`-`:1659`); without one the old count is kept, and `addVideoMode` then writes the new modes from the old count on, past the end of the new, smaller array (`:1654`, `:1638`). `window` and `glcontext` keep the old, destroyed values after a start that fails (`stopSDL3`, `:1783`, does not clear them) | Nothing needed today: Coney calls neither setter; the `numModes` case needs a display for which SDL reports no current mode, and none is known (inferred from the code; no test makes one); a start that fails is stopped through the NULL device, which never reads `window` or `glcontext`. A change of video mode or settings would have to reset `currentMode` and `numSamples` (and `numModes`, to 0 before `Engine::open`) through `rw::gl3::glGlobals` before the next `Engine::start`, and clear `window` and `glcontext` when a start fails |
| `gl3Caps` | `openSDL3` clears it; `startSDL3` and `initOpenGL` fill it from the new context | Nothing needed |
| The OpenGL function table (glad) | Loaded again by `startSDL3`; never cleared, so after an OpenGL renderer stops every pointer stays set though its context is gone | Nothing needed for an OpenGL restart. Code that must not call OpenGL under the NULL backend asks the renderer (`draw::forwarding()`), never whether a glad pointer is set: the mesh views' flat pass did, and drew through a stale table when a NULL renderer followed an OpenGL one in the same process, and its wire pass checked `glad_glPolygonMode` (Coney tests: the flat pass's and the wire pass's, in `tests/platform/mesh_views_test.cpp`) |
| `nativeRasterOffset` | The raster plugin's offset, registered again by the GL3 driver at `Engine::init` before any raster exists (`registerNativeRaster`, `gl3raster.cpp:1035`, from `registerPlatformPlugins`, `gl3.cpp:52`). Registering twice is safe because `Engine::term` runs `PluginList::close` (`engine.cpp:336`, `plg.cpp:32-46`), which frees every plugin and gives a list that is left empty its default size back, so the next `Engine::init` registers on empty lists and the raster gets no second GL3 slot | Nothing needed |
| `needToReadBackTextures` | Never assigned anywhere in librw at the pin: declared at `gl3device.cpp:28`, read only under `gl3Caps.gles` (`gl3raster.cpp:336`) | Nothing needed |
| `im3dMaterialColor`, `im3dSurfaceProps` | Defaults a program may change; Coney changes neither | Nothing needed |
| `tmpprimbuf`, `num3DVertices`, `skinMatrices` | Scratch written before it is read | Nothing needed |
| `Texture::findCB` | Coney's lookup function (`src/platform/texture_lookup.cpp`), a free function that stays valid | Nothing needed |
| The MatFX and skin shaders, `lastEnvFrame` | Coney attaches neither librw's MatFX nor its skin plugin (`src/platform/world_atomic.cpp` reads the MatFX chunk itself), so those shaders never exist; `lastEnvFrame` is set to null at the start of every MatFX draw | Nothing needed |
| Coney's own: the texture-LOD shaders, the profiler's wrappers of OpenGL calls, the NULL backend's `glDeleteTextures` stand-in | Destroyed and nulled by `stopTextureLod`, put back by `removeGlCounters` and by `shutDown` | Already done |

A librw global that a later change of the pinned commit adds, or a new Coney global that names a librw object, joins
this table in the same commit. `cmake/deps.cmake` applies no patch to librw; none is needed today.

## Behaviour

**The raster rule.** Evidence for the librw claims in this paragraph: confirmed (code), `texture.cpp:67-81`
(`textureClose`) and `engine.cpp:370-374` (`Engine::stop`: the plugin destructors, then `DEVICETERM`). Every librw
raster, texture and texture dictionary (but the one dictionary librw makes for itself) is destroyed before
`RenderEngine::shutDown` reaches `Engine::stop`: the camera's and Coney's passes' by the engine, every other by its
owner's destructor, which therefore runs before the engine's. The reason is what happens to an object that is still
there. librw's stop (`textureClose`, inside `Engine::stop`, before the device is terminated) destroys the textures and
dictionaries still listed, so an owner that destroys its own after the stop frees it a second time, and a raster
destructor that calls OpenGL after the stop runs on a context that is gone. A raster that never had a texture is not
destroyed by librw at all: it leaks, and a texture stage may still name it. `shutDown` checks the counts with
`CONEY_ASSERT` (`Raster::numAllocated`, `Texture::numAllocated`, `TexDictionary::numAllocated` at most 1), so a
renderer stopped under a live texture stops the program at once instead of corrupting the next start.

**The order.** In `shutDown`: the rule is checked; then, while the context exists, framebuffer 0 is bound and fog is
turned off; librw stops; the two shader globals are cleared; librw closes and terms. In `start`, once librw has started
and before Coney makes its camera: white is bound on every texture unit and the fog colour is set to white.

## Coney's implementation

`src/platform/render_engine.cpp`: `bindWhiteTextureOnEveryUnit` and `RenderEngine::start`, `RenderEngine::shutDown`. A
window that is to stay hidden is hidden as SDL shows it (`placeWhenShown`): librw makes the window shown and offers no
way to make it hidden (confirmed (code): `startSDL3` creates it with `SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL` only,
`gl3device.cpp:1740` and `:1747`, with no hidden flag, and no librw call hides it), so the tests leave one on the
screen for no longer than SDL's own show call takes. Tests: the case `a renderer started again after one stopped meets
none of its state` (`tests/platform/render_engine_restart_test.cpp`) starts and stops the renderer four times in one
process (only the first start may skip, for want of a display; a later start that fails is a failure). Each time it
checks, before the first frame, the shader globals, the fog state, the active texture unit, the 2D texture bound on
units 0 to 7 in OpenGL itself, and a bind of a framebuffer name made in the new context until the old one's name comes
up; then it draws a frame and checks the fog colour in the registry, and sets it to red for the next start to find. It
leaves a framebuffer bound, the 2D override set, fog on and a texture in stage 0 (the only stage librw lets a check
read) when it stops; it checks that destroying the texture empties the stage, and the counts after.
`coney.frame_copies_upright` (`tests/support/frame_copy_check.cpp`) restarts the renderer once per display and checks
that `currentShader` is null after each stop, and that a start on SDL's dummy video driver returns an error.

## Open questions

- The rule is checked for rasters, textures and dictionaries only. A librw object of another kind that outlives the stop
  (a clump or an atomic holding a pipeline) is not counted; none is known to. It decides whether the assertion in
  `shutDown` is enough. To answer it, count `Clump::numAllocated`, `Atomic::numAllocated` and `Geometry::numAllocated`
  in `shutDown` for a run of the sandbox and the world, and read what each frees in `Engine::stop`.
- Only the OpenGL 3.3 core path was run. The OpenGL ES and 2.1 fallbacks librw can fall back to share the code above,
  but no test starts them. It decides whether this table holds there (for example `needToReadBackTextures`, read only
  under `gl3Caps.gles`). To answer it, start the renderer with the 3.3 profile refused (the `profiles` table in
  `startSDL3`, `gl3device.cpp:1711-1720`, or a driver that offers only 2.1 or ES) and run the restart case.
