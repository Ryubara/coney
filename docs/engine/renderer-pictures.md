# Renderer pictures

How the renderer's pictures and draw streams are compared while the [renderer design](renderer.md) is built: the draw
stream and its hash, the synthetic scenes, the disc frames and the gates of the picture comparison with the tolerances
measured so far. It is the part of that page that holds the numbers, split out to keep it short; the items it speaks of
are in the renderer's [Order](renderer.md#order).

## How pictures are compared {#comparing}

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
  with item 7 ([#1199](issue:1199)): `WorldRenderer::render`'s render states and camera begin, and the world's atomic
  renders (`draw::renderAtomic`): each streamed sector, its dual layer, and the level's sky, clouds, skyline and light
  glows, each one record (the states, distance levels, frame, geometry, materials and lights: draw_calls.h). Routed with
  item 8 ([#1200](issue:1200)): `ParkedCars::draw`'s and `PlacedObjects::draw`'s render states and their atomic renders,
  each car part and world object one record (`SceneLighting::drawObjectAtomic` and an object drawn with no lights of its
  own). Routed with item 9 ([#1201](issue:1201)): each human's body and blood layer, the states
  `PlayLighting::drawHumanPasses` sets, and the `VERTEXALPHA` the default and skinned pipelines set inside a draw; a
  record of a GPU-skinned body also holds its weights, bones and palette, the vertex buffer being filled once from them.
  Item 10 ([#1202](issue:1202)) routed nothing new, since no effect is drawn as a model; its scenes are below.
  Never: the start-up and the profiler's queries.
- **What a record holds.** A draw holds its primitive, every render state set so far with its value as it stands at
  the draw (a raster as its place in the recording's order of first use, its size and format, and a digest of the
  pixels Coney uploaded to it), the vertices and the indices; Im3D also its normals, world matrix and flags. A camera's
  begin holds its matrix, view window, offset, clip and fog planes, projection and frame size. So setting a state twice,
  or to the value it already has, changes no hash. Bytes are written little endian in a fixed order, hashed with 64-bit
  FNV-1a; each record also has a hash of its own, which says where two streams first differ.
- **Live and headless.** Live, each call is forwarded and also recorded while a recording runs. Under `--headless` no
  call reaches librw or OpenGL, the layer keeps the render states itself, a raster is made with librw's `DONTALLOCATE`
  and a lock gets a buffer of the layer's; `RenderEngine`'s 2D drawing, which draws nothing headless, runs while a
  recording does, with librw's default near plane in place of a camera's. A null camera, which no caller passes
  today, is recorded as "no camera" by both camera begin and end and reaches librw in neither, forwarding or not.
  Not forwarding, a state nobody has set reads back as librw's GL3 device starts it (`resetRenderState`: alpha test
  "at least 10 of 255", depth on, no culling, source-alpha blending, white fog), as live (item 13); a recording holds
  only the states set. It read 0 ("always") before, so four synthetic hashes moved: the 2D scene's to
  `0x602c8c03483a7de4`, the object scene's to `0x952ee961e9bcbbc1`, the world scene's to `0x0d90e4b7a17f8c7f` and the
  human scene's (item 9's) to `0x374f7363903b7a91`. Live drawing does not read the layer's states, so the disc frames'
  hashes do not move.
- **Synthetic scenes** in [draw_calls_test.cpp](repo:tests/platform/draw_calls_test.cpp), drawn headless: a 2D scene
  through `RenderEngine` (the fill, quads, alpha-tested triangles, a view wash, a window rectangle), a textured Im3D
  mesh under a placed camera with a Coney-made checkerboard, the throw arc's ribbon drawn by `ThrowArcRenderer`, and the
  named operations. Their expected hashes are in the test, beside checks that a repeated state keeps the hash and a grid
  step, a texel or a state moves it; the grid and the encoding are checked against the definition in
  [draw_stream_test.cpp](repo:tests/platform/draw_stream_test.cpp). The world scene
  ([world_draw_stream_test.cpp](repo:tests/platform/world_draw_stream_test.cpp), item 7): two Coney-made textured
  sectors drawn headless by `WorldRenderer` through a camera of its own, with its hash and a check that a fade moves it.
  The object scene ([object_draw_stream_test.cpp](repo:tests/platform/object_draw_stream_test.cpp), item 8): two
  Coney-made crates (one tinted and translucent) and a painted car of 26 parts, loaded from a synthetic archive by
  `PlacedObjects` and `ParkedCars` and drawn headless by `WorldRenderer` among the objects and after the worlds, with
  its hash and checks that a tint and a paint move it. The human scene
  ([human_draw_stream_test.cpp](repo:tests/platform/human_draw_stream_test.cpp), item 9): four Coney-made characters
  drawn headless by `PlayLighting::drawHumanPasses` (skinned on the CPU; the disc frames cover the skinned pipeline),
  with its hash and checks that a turn, the blood layer and the fade move it. The effects scenes
  ([effect_draw_stream_test.cpp](repo:tests/platform/effect_draw_stream_test.cpp), item 10): Coney-made particle systems,
  glints and fog wisps (two sprite sheets of a Coney-made texture, blended and added, and a flat shard), the sprites of
  `SceneLighting` (a corona, two blob shadows, two glass panes, a health ring and a target marker), the water and the
  debug tools' lines, each drawn headless in a frame, the throw arc's ribbon in `draw_calls_test.cpp` as before; their
  hashes were recorded before the effects moved onto the description and held through the move.
- **Disc frames.** `--draw-hash` prints the last frame's counts and hash ([Running Coney](../guides/running.md)). The
  named frames are run with a window, the world frozen and a fixed size, from the level's first frame of play:

  | Name | Command |
  | --- | --- |
  | `level99-street` | `coney --play-level level99 --frames 90 --freeze-world --render-size 640x448 --draw-hash --skip-movies` |
  | `level95-hub` | `coney --play-level level95 --frames 90 --freeze-world --render-size 640x448 --draw-hash` |

  The hashes are taken from the GL3 renderer (`--renderer gl3`, the default): `--renderer gpu` records the headless
  stream until the GPU renderer draws, so its hash is not one of these frames'. Each is run twice on the branch and on
  its base; a pull request that changes drawing reports the four hashes in its Testing section, and two runs of one
  build must agree. They need the window: headless, play draws only its 2D layer.
  Item 1's hashes, twice each on Windows: `level99-street` 23 records, `0xe923080c2453d3a4`; `level95-hub` 16 records,
  `0x4ba9bf8c9bdc48ed`. A PS2 texture's digest is of its texel and palette blocks alone: librw's data block also holds a
  heap pointer and gaps, which made two runs differ. Item 7's, the same after both its commits: `level99-street` 48
  records (23 atomic draws), `0xe25af3f2c222b401`; `level95-hub` 47 (29), `0x996fcc94058f3360`; their screenshots
  matched the base's pixel for pixel. `level95-hub`'s screenshot is black in every build (base included), so only its
  draw-stream hash covers that frame. Item 8's, the same after both its commits, the objects' and cars' draws now
  recorded: `level99-street` 132 records (107 atomic draws), `0x3f33251f6811e9d6`; `level95-hub` 60 (42),
  `0x87ef01e331e744cb`; their screenshots again matched the base's pixel for pixel. `level99` now plays its intro movie
  before its first frame of play, so `level99-street` is run with `--skip-movies`: `0x31226d29c17742e4` (132 records,
  107 atomic draws), the same before and after item 8's two fixes (a car's lights at squared distance 0, an object's
  tint on its first material), with the screenshot unchanged. Item 9's, the same after both its commits:
  `level99-street` 144 records (119 atomic draws), `0xbd96883072a978f9`; `level95-hub` 74 (56), `0x3ea8db9c32d56092`;
  with `--cpu-skinning` `0x292b3d9dc97355d1` and `0x37f02cb07ff2b76f`; both paths' screenshots matched the base's. With
  the humans' colour then ported (their white `HuColor` times the dimming, not the material's `0x969696`), the same
  counts: `level99-street` `0xc8c5be7c138a978e`, `level95-hub` `0x5a39681c1dd355c9`; with `--cpu-skinning`
  `0xf737a41632222772` and `0x304202659c4bd0c8`; `level99-street`'s screenshot 19,151 pixels over 2 (6.7 %, largest
  difference 59), all on the humans, and `level95-hub`'s black one unchanged.
  Item 10's, the effects moved onto the description, run twice each with the base's `coney.exe` (before the move) and the
  branch's: `level99-street` 132 records (1 Im2D, 11 Im3D and 107 atomic draws), `0xb9196d69498e07d9`; `level95-hub` 69
  (2 Im2D, 3 Im3D, 51 atomic), `0xcd6e4c66e508569c`; the same in both builds and in every run, with the screenshots
  unchanged (0 of 286,720 pixels over 2, largest difference 0, in both frames; `level95-hub`'s still black).
- **The grid across systems** is the open question this item leaves to CI: the expected hashes were recorded on
  Windows, and the first CI run on Linux and macOS shows whether they hold. If one differs, the fallback grid is 1/256
  of a unit for positions and texture coordinates (`kDrawGridSteps`) with colours kept at 8 bits, and the expected
  hashes are recorded again; this paragraph then says which grid holds.

**The picture comparison, and its gates.**

| Check | What it compares | Gate | Tolerance |
| --- | --- | --- | --- |
| Software pictures | the synthetic scenes drawn under a virtual display through Mesa's software drivers (llvmpipe for GL3, lavapipe for Vulkan once item 11 lands), against reference images of those scenes kept in the tests (Coney-made, small). The runner image's own Mesa moves with the image, so the job runs in a container image pinned by its digest, with Mesa at a fixed package version, cached on the runner. Moving the pin is a pull request of its own that regenerates the reference images in the same pull request and reports the old and new counts | CI on Linux (item 2, built: the `software-pictures` job, [the software pictures](../guides/testing.md#software-pictures)), a required check (`software-pictures` in `merge.required_checks` of `.github/conventions.yml`) with the four scenes' references in `tests/pictures/reference/`, made from a `main` run's pictures | 2 of 255 and 0.1 % (stand-ins). Measured: 4 scenes drawn 10 times each, in separate runs of one build, on one machine's GPU driver: 0 pixels over, largest difference 0 (confirmed (runtime), `picture scenes --repeat 10`); under the pinned llvmpipe: the 4 scenes drawn twice in each of two `main` runs (44131342786 and 44183284280): every picture byte for byte the same, so 0 pixels over and largest difference 0 (confirmed (runtime), the `software-pictures-drawn` artifacts); the stand-ins stay as the room for a driver change |
| Disc pictures, one renderer | `--screenshot` of named frames at `--render-size 640x448` with a pinned camera ([Matched views](../guides/running-play-level.md#matched-views)), from the branch against the same frames from its base commit, on one machine | local: every pull request from item 5 on that changes drawing reports the counts in its Testing section, from the tool's output kept in the scratch folder, and the reviewer refuses one without them, as for the Runtime line ([Capturing](../guides/testing.md#pr-media)) | at most 2 of 255 apart in any channel, over at most 0.1 % of the pixels: stand-ins: item 2's repeats of one build were identical (the Software pictures row), so a value waits for counts across drivers |
| Across renderers | the same frames drawn by GL3 and by SDL3 GPU, on one machine and in CI's software pictures (the [test cards](../guides/running-viewers.md#test-cards), the 2D ones, the 3D `scene` and `effects`, `renderers` in `tests/pictures/scenes.toml`); a level frame whole, or on the scene alone with `--scene-only`, which leaves out the effects (drawn by both since item 20: [Drawing the effect passes](gpu-renderer.md#effects); the humans since item 14) | as above, from item 11 on | 4 of 255 and 0.2 % (`cross_channel_tolerance`, `cross_max_share_percent`), twice one renderer's room. Item 11's first comparison, on the agents' Windows machine (RTX 5080): the three test cards at 320x224 drawn by GL3 and by `gpu` on Direct3D 12 and on Vulkan, 0 pixels over 4, largest difference 0 (each drawn twice with `--repeat 2` on both APIs, the repeats identical too); five 640x448 frames of the boot (a logo and the opening movie at frames 60, 200 and 400; with `--skip-movies` the legal screen at frame 300 and the title screen at 900) drawn by both on Direct3D 12, 0 pixels over 4 in the first four, largest difference 1, and the title screen 32,160 pixels over 4 (11.2 %, largest difference 213), all of them on the Wonder Wheel, a 3D model the GPU renderer does not draw yet (item 13; its dark spokes differ by 5 to 9; confirmed (runtime), the difference picture). CI's llvmpipe against lavapipe prints its counts on the job's `across renderers:` line; a first run past these is recorded here and the tolerance set from it. Item 13's, on the same machine on Direct3D 12, the named `--play-level` frames at 640x448 with `--freeze-world` (confirmed (runtime), `picture compare` in the task's scratch folder): with `--scene-only`, `level99-street` 14 pixels over 4 (0.005 %, largest difference 55) and `level95-hub` 0 (its picture black on both, as in every build); the whole frame, `level99-street` 58,932 over (20.6 %, largest difference 162; 59,130 before the humans brightened with item 9), all of them the sky, the player and the light glows the GPU renderer does not draw yet, and `level95-hub` 0. Item 14's, the same way, with the humans drawn on both: with `--scene-only`, `level99-street` 16 over 4 (0.006 %, largest difference 55; the player and the three on the roof match) and the hub camera of the benchmark with its crowd of 50 at frame 300, 5 over (largest 22); the whole `level99-street` frame 42,615 over (14.9 %, largest 162), the sky, skyline and glows, and the crowd 4,788 (1.7 %, largest 177), the blob shadows, rings and a light's glow, all effects; GL3 against its base, those three frames 0 over. The level's background ([#1527](issue:1527)), on the same machine (confirmed (runtime), `picture compare` in the task's scratch folder), with the GPU renderer's pipelines now cutting away what lies past the far clip as OpenGL does (SDL's default clamped its depth, which drew the level world's far light glows as fogged disks over the sky and left dots along the skyline's edges): with `--scene-only`, which now draws the background, `level99-street` 16 pixels over 4 (0.006 %, largest difference 55), the benchmark's street view 0 and its hub view (`perf bench`'s `hub`, `level95` at checkpoint 1 with its camera, frame 300) 5 (largest 22); the whole frame, `level99-street` 31,051 over (10.8 %, largest 162; 42,615 before), the street view 4,312 (1.5 %, largest 60; 36,846 before) and the hub view 2,993 (1.0 %, largest 177; 3,308 before), nearly all of them the coronas, the player's blob shadow and the see-through panes of the shop windows, which item 20 adds; GL3's own frames, branch against base, 0 pixels over 2 in all three. The 3D `scene` test card: largest difference 2. Item 20's effect passes ([#1212](issue:1212)), on the same machine on Direct3D 12, the whole frames with the effects on both (confirmed (runtime), `picture compare` and `picture scenes` in the task's scratch folder): `level99-street` 18 pixels over 4 (0.006 %, largest difference 38; 31,051 before), the benchmark's street view 0 (largest 2; 4,312 before, as recorded above), its hub view 5 (largest 22) and its hub with a crowd of 50 at frame 300 7 (largest 22), the coronas, blob shadows, rings and panes now within the tolerance; the left-over pixels are those of `--scene-only` above, on the scene's own edges. The `effects` test card, 0 pixels over 4 (largest difference 2), each card drawn twice with `--repeat 2` and the repeats identical. Item 20's screen passes ([#1646](issue:1646), [Drawing the screen passes](gpu-renderer.md#screen-passes)), on the same machine on Direct3D 12 (confirmed (runtime), `picture compare` and `picture scenes` in the task's scratch folder): `level99-street` 18 pixels over 4 (largest difference 38, as before: level99's play lays no motion blur) and with `--line-blend on` 24 (largest 21; on the GPU renderer that frame moved 44,720 pixels from its base, which drew no line blend); the `blur-pulse`, `line-blend` and `motion-blur` test cards 0 pixels over 4 (largest difference 2), the repeats identical; GL3's own frames, branch against base, 0 pixels over 2 with and without the line blend. The comparison found a GL3 fault the GPU renderer does not have: a frame copy left the copy's texture bound behind librw's back, so the next draw with no texture sampled it (the motion blur's kept frame darkened the next frame's flat quads), fixed in `draw::copyFrameToRaster`. Item 20's parity suite ([#1647](issue:1647)) draws every benchmark scene and the named frames whole, and is on [The GPU renderer's parity](#parity) |

The counts reported are the pixels over the tolerance and the largest difference
([`picture compare`](../guides/coney-tools-picture.md#picture)). Pictures of the game stay in the scratch folder and are
never committed; only the synthetic scenes' references are in the repository. The software job's counts are in the table
above; the tolerance is here and in `tests/pictures/scenes.toml`.

## The GPU renderer's parity {#parity}

`coney-tools picture parity` ([the command](../guides/coney-tools-picture.md#parity)) is the check that closes item 20
(the effect passes, the screen passes, and this suite): every [benchmark scene](renderer-performance.md#benchmark) that
draws and the named `--play-level` frames, each drawn whole by GL3 and by the GPU renderer, the GPU renderer's picture
compared with GL3's by the cross-renderer tolerance (4 of 255 in any channel, over at most 0.2 % of the pixels).
Whole frames, since both renderers now draw the effects and the screen passes: `--scene-only` is left to the frames
that want the scene alone. The test cards, which need no disc, are CI's software pictures ([How pictures are
compared](#comparing)); this suite is their disc-backed half, run locally.

**What it found** (confirmed (runtime), `picture parity` in the task's scratch folder; the agents' Windows machine, an
RTX 5080, Direct3D 12; the benchmark scenes at frame 300 of a `playtest` build, the level frames at frame 90 with
`--freeze-world`):

| Frame | Size | Pixels over 4 | Share | Largest difference |
| --- | --- | --- | --- | --- |
| `hub` | 960x720 | 13 | 0.002 % | 23 |
| `hub-crowd` (50 fighters) | 960x720 | 20 | 0.003 % | 23 |
| `street` | 960x720 | 0 | 0.000 % | 1 |
| `hub-640` | 640x448 | 5 | 0.002 % | 22 |
| `hub-2560` | 1058x793 | 13 | 0.002 % | 33 |
| `level99-street` | 640x448 | 18 | 0.006 % | 38 |
| `level99-line-blend` | 640x448 | 24 | 0.008 % | 21 |
| `level95-hub` | 640x448 | 0 | 0.000 % | 0 (both black) |

Every frame is within the tolerance, by two orders of magnitude of its share. What is left, and why, is listed on
[the renderer design](renderer.md#parity-differences).
