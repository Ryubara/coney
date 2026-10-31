# Enhancements plan

The design for the [Graphics and performance](../roadmap.md#graphics-and-performance) milestone: what each graphics
option means for *The Warriors*, whether it is possible, how Coney's renderer would do it, what it depends on, a rough
cost and the order. Since 2026-10-25 these options may be built now, beside the faithful renderer ([Faithful
first](repo:AGENTS.md)): presentation and performance go beyond the PS2, gameplay does not. Every option is forced off
in test mode, so tests and reference screenshots always show the faithful image, and the PS2 look stays selectable;
which preset a player gets by default is a maintainer question ([Renderer: Questions](../engine/renderer.md#questions)).
Code for an option carries `@coney-only enhancement: <option>`, or `@orig xbox` where it follows the Xbox port ([Xbox
tags](conventions.md#xbox-tags)).

## What the game gives us

| Fact | Consequence for enhancements | Source |
| --- | --- | --- |
| 640 × 448 logical screen, interlaced, 4:3; the original also has a **16:9 mode** (device flag `0x04`) with its own overlay aspect, HUD and menu layouts and `_w` legal screens | Widescreen is partly faithful: the original's 16:9 mode is the reference, and only wider shapes are Coney's | [Graphics: video mode](../research/graphics.md#video-mode), [HUD: coordinates](../research/hud.md#coordinates) |
| 30 frames a second (a flip every second vertical blank); simulation on a fixed 1/30 s step | Above 30 fps, drawing must blend the last two steps. Coney already does this | [Graphics: frame rate](../research/graphics.md#frame-rate), [Update and render](conventions.md#update-and-render) |
| World is **prelit** (a baked vertex colour per vertex) plus a 0.227 grey ambient and a few lamps; 88 of `level99`'s 90 lamps light only objects and humans | No light sources that match the baked light: shadows, AO and RT can only add to it, never replace it | [Lighting: world](../research/lighting.md#world) |
| Humans: vertex lighting from a few lights; one faint blob shadow (alpha 128) | Dynamic shadows are an addition. The Xbox build has a `GraphicsShadowXBox.cpp` (inferred from its path string), so another shipped version may have had better shadows | [Lighting: humans](../research/lighting.md#humans), [Xbox assets](../research/xbox-assets.md#original-structure) |
| Every texture is 4- or 8-bit palettised, up to 512²; only 741 of 42,211 have mipmaps; 20,559 of 20,582 distinct textures wrap | Mip generation is the cheapest big quality gain. Upscalers must handle tiling and palettes | [Graphics: texture formats](../research/graphics.md#texture-formats) |
| Draw distance 115 m with fog from half of it; the PS2 shrinks it when the frame rate drops | The level's mood is in its fog. A longer view changes the look | [World: a frame](../research/world.md#a-frame) |
| Screen effects: frame-feedback motion blur (looks), a 512 × 256 blur/glow raster, heat distortion, a 9 × 9 distortion grid | The faithful look already needs a render-to-texture post chain | [Graphics: screen effects](../research/graphics.md#screen-effects) |
| The simulation reads the camera: spawners test `Camera_AnyPlayerCanSeePoint` and place humans outside half the field of view + 10°; scripts set the field of view | A wider FOV or aspect must never reach the simulation | [AI: off screen](../research/ai-gangs.md#spawner-unseen), [AI: out of sight](../research/ai-gangs.md#spawner-placement), [Cameras](../research/camera.md) |

**Coney's renderer today** (`repo:src/platform/`): librw GL3 (OpenGL 3.3 core) through SDL3. The camera draws
straight into the window's back buffer at the window's size. The 4:3 logical screen is fitted inside the window.
Per-atomic lights go to librw's vertex lighting (`scene_lighting`). The motion blur stand-in copies the back buffer
into a texture (`motion_blur_pass`, `glCopyTexSubImage2D`). The decoupled loop (`frame_pacer`, `FrameClock`,
`Interpolated<T>`) already renders above 30 fps. librw itself offers default-framebuffer MSAA
(`DEVICESETMULTISAMPLINGLEVELS`, set before the window is made), per-texture anisotropy (`Texture::maxAnisotropy`),
automatic mipmaps (`Raster::AUTOMIPMAP`), DXT rasters and camera-texture render targets. It also offers a D3D9
backend. It has no Vulkan, D3D12 or Metal backend.

## Foundations

These are shared by most options. Each one is worth building for the faithful look anyway.

| Id | Foundation | Needed by | Cost |
| --- | --- | --- | --- |
| F1 | **Display settings** in the debug menus, never the game's own options screens ([below](#where-the-settings-live)): a settings store with the presets of [Renderer](../engine/renderer.md#presets); test mode forces *PS2*; which preset a player gets by default is a [maintainer question](../engine/renderer.md#questions) | every option | S |
| F2 | **Scene target and post chain**: draw 3D into an offscreen colour + depth texture at the internal resolution, then run post passes (each declared in units of time, not frames), then the 2D overlays at window resolution. The faithful 512 × 256 blur, heat distortion and distortion grid need it too | resolution scale, AA, AO, bloom, grading, DOF, CA, MSAA with blur | M |
| F3 | **Coney's own shaders** for the world, object and character materials, in HLSL on the SDL3 GPU renderer ([Renderer: Migration](../engine/renderer.md#migration)): extra inputs (shadow map, velocity) and outputs (normals) | shadow maps, TAA velocity, alpha-to-coverage, tessellation | M |
| F4 | **Texture resolver**: every texture load goes through one lookup, PS2 → Xbox → local pack, keyed by (dictionary resource hash, texture name, hash of the PS2 texels), falling back to the PS2 texture on any mismatch | Xbox textures, upscaled packs, player packs, mip generation | M |
| F5 | **Backend switch** (decided, [below](#decided)): Coney's own renderer over SDL3's GPU API (Vulkan, D3D12, Metal; already in our SDL3, zlib licence), with librw kept only for reading RenderWare data and the PS2 and Xbox rasters. Its groundwork (the start without librw's GL3 device, the shader build, the frame description, Coney's own formats) is the first work of [Renderer: Order](../engine/renderer.md#order), items 1 to 14; the switch is item 21. Raw Vulkan / D3D12 / Metal only for ray tracing | every option past the *PS2* picture (they target the GPU renderer only), HDR output, ray tracing, macOS (Apple's OpenGL is deprecated and stops at 4.1) | XL, in steps that each land |

**Choices to make now, at no cost** (they keep every option above open):

1. The simulation reads only the game's own camera values (field of view, view window, aspect, draw distance). The
   display's FOV, aspect and render distance are separate render-side values.
2. Everything that moves on screen keeps its last two steps (`Interpolated<T>`), including particles, objects,
   bones, scene cameras and fades. A per-frame effect gives its strength per 1/30 s and converts it per frame
   (`s_frame = 1 - (1 - s)^(30 dt)`). Today's motion blur lays the last frame over at a fixed per-frame strength,
   so its trails shorten as the frame rate rises.
3. No librw draw call outside the platform renderers, and no window-pixel coordinates in game code: 2D stays in GUI
   or logical units. Then the backend and the screen shape can change under it.
4. Record each HUD and menu element's anchor (left, centre or right) and its 16:9 variant while researching the HUD
   and the front end ([HUD](../research/hud.md#coordinates) lists the 16:9 overwrites as unlisted).
5. Load every texture through one function, so F4 can be added later in one place.

## Where the settings live {#where-the-settings-live}

Maintainer decision: the original's menus stay faithful, so every option on this page is a control in Coney's
[debug menus](debug-menu.md), on a *Display* page in both the pad menus and the developer overlay. None goes into
the game's options screens.

- **The store.** Each option is a named, typed, ranged value with a default, registered like a
  [tunable](debug-menu.md#tunables) in a *Display* category. It is saved in the same overrides file
  (`Display/MSAA samples = 4`), and a preset writes a set of values at once. Display values are render-side only:
  the simulation never reads them, so they need no step-boundary queue. They apply on the next frame. A few
  (MSAA on the default framebuffer, the backend) take effect at the next start, and the page says so.
- **Settings so far.** `Display/Cutscene letterbox` (on by default, the original's look): off, a cutscene plays
  without its black bars. The letterbox still runs underneath, so the HUD and the spinning icons hide during
  cutscenes as in the original; only the bars are not drawn. Set it on the Tunables page's *Display* category in
  the pad menus or the developer overlay, then *Save overrides*; or put `Display/Cutscene letterbox = 0` in the
  overrides file (`coney-tunables.ini` in your config folder, or the file `--tunables FILE` names).
  `Display/Renderer` (0, GL3, by default): 1 will start SDL3's GPU renderer at the next start, as `--renderer gpu`
  does, once it draws its own menus; until then a saved 1 starts GL3 with a warning
  ([The way back to GL3](../engine/gpu-renderer.md#way-back)); test mode ignores it.
- **Command line.** Flags where they help scripted captures and benchmarks: `--preset ps2|xbox|enhanced|high`,
  `--render-scale`, `--aspect`, `--fov-offset` and `--msaa`, beside the existing `--fps-cap` and `--vsync`. A
  general `--set Display/<name>=<value>` covers the rest.
- **Coney natives.** The maintainers plan a FiveM-style script API of Coney's own on top of the game's bindings
  ([Script mods](../roadmap.md#script-mods)). The store therefore exposes every option by its stable
  `Display/<name>` key, with its type and range, through one get/set interface. A native can read or set an option
  later without new plumbing. The natives themselves are designed elsewhere.

## Options

Feasibility: **yes**, **partly** (possible with limits or with a visible trade-off), **no**. Cost: **S** about a
week of one engineer, **M** 1-3 weeks, **L** 1-2 months, **XL** more. **Xbox** marks options that need the
player's Xbox disc ([Xbox assets](../research/xbox-assets.md)).

### Screen and timing

| Option | What it means here | Feasible | Approach | Depends on | Cost |
| --- | --- | --- | --- | --- | --- |
| Internal resolution | The PS2 drew 640 × 448. Coney already draws at window resolution | **yes** | Render scale 0.5-2× on F2's target (above 1 is supersampling), plus a *PS2 640 × 448* mode with nearest or CRT/interlace output (a shader of our own) | F2 | S after F2 |
| 16:9 widescreen | The original's own 16:9 mode: 3D camera constants, overlay aspect 1.6667 at scale 1.1, HUD and menu layouts, `_w` screens | **yes** (faithful) | Implement the 16:9 mode from research. The logical screen becomes 16:9 instead of 4:3 | research: the 16:9 camera constants (`0x00122ca0`, `0x00122d38`), HUD and menu 16:9 tables | M |
| 21:9, 32:9, any aspect | Coney's own extension | **partly** | Hor+ (keep the vertical FOV, widen the horizontal); overlay aspect from the window; HUD elements placed by their recorded anchor inside a chosen safe width; cinematics and movies pillarboxed at 16:9 or 4:3 | 16:9 mode, now-choice 4 | M |
| HUD scaling | GUI space is about [0, 1]² with a built-in 5% safe margin | **yes** | Scale and inset factors applied in the overlay projection (`OverlayCamera`), not per widget. Fonts are 2D bitmaps: crisp only with upscaled or SDF font textures (see [AI upscaling](#ai-upscaling)) | 16:9 mode | S |
| FOV | Player camera 65° on 4:3; scripts set and ease it; scene cameras set their own | **yes** | A render-side offset or scale on the follow camera only, never on scene or locked cameras (composition); the simulation keeps its value (now-choice 1). Streaming already pulls the far clip in on missing scenery | now-choice 1 | S |
| Frame rate above 30 | Simulation stays at 30 Hz (60 Hz physics tick); drawing blends steps | **yes** (exists) | Done: decoupled loop, `--fps-cap`, `--vsync`. Still to do: audit every drawn thing against now-choice 2 (skinned poses blended per bone as rotations, particles, cameras in scenes, UV scrolls); make the motion blur rate-independent; ignore the PS2's low-fps draw-distance shrink (it measures the display rate) | now-choice 2 | S-M (audit) |

### Anti-aliasing and filtering

| Option | What it means here | Feasible | Approach | Depends on | Cost |
| --- | --- | --- | --- | --- | --- |
| MSAA | Low-poly scenery, many long straight edges: MSAA suits it well | **yes** | On the SDL3 GPU renderer only, a multisampled F2 target ([Renderer: order](../engine/renderer.md#order), item 26); the GL3 path gets none. The motion blur's copy then becomes a resolve, since a multisampled buffer cannot be copied into a texture | F2 for the blur | S |
| Alpha-to-coverage | Chain-link fences, foliage and railings are alpha cut-outs that MSAA does not smooth | **yes** | Alpha-to-coverage on alpha-tested materials in Coney's shaders on the GPU renderer, with coverage-preserving mips (below) | F3, MSAA | S |
| FXAA / SMAA | Post-process AA; cheap, a little blur | **yes** | One post pass. SMAA's reference code is MIT; FXAA 3.11's NVIDIA licence is permissive (check at adoption) | F2 | S |
| TAA | Removes shimmer on fences and thin wires; blurs the already soft 64 × 64 textures and ghosts. Needs jitter, velocity and history | **partly** | Per-object velocity from the two blended steps (we have them); jittered projection. FSR 3.1 (MIT) can be the TAA and upscaler, and stays the default the build ships; FSR 1 is the fallback, and DLSS, FSR 4 and XeSS are optional plug-ins beside the GPL code ([Decided](#decided)); the tiers are on [Renderer: Upscalers](../engine/renderer.md#upscalers) | F2, F3, F5 | M |
| Anisotropic filtering | Streets and pavements at grazing angles | **yes**, but only with mips | The GPU renderer's sampler anisotropy per texture | mip generation | S |
| Mip generation | 98% of textures have no mips, so they alias in the distance at any resolution | **yes** | Build mips when textures are converted into Coney's own formats ([Renderer: Order](../engine/renderer.md#order), item 6), by Coney's own box filter with alpha coverage kept at the alpha-test reference so fences do not vanish; LOD bias as a setting. Changes the look slightly (less shimmer than the PS2) | F4 (one load path) | S |

### Assets and distance

| Option | What it means here | Feasible | Approach | Depends on | Cost |
| --- | --- | --- | --- | --- | --- |
| Xbox textures | 612 of 2,027 matched textures at twice the size; the rest are the same images re-encoded to DXT (block artefacts instead of palette banding) | **yes**, modest gain | Through F4, keyed by resource hash and position. Take the Xbox texture **only when it is larger**; keep the PS2 one otherwise | **Xbox**; F4; [open questions](../research/xbox-assets.md#open-questions) 1-3 (names, DXT alpha, world textures) | M |
| Xbox HD movies | 15 of 16 movies at 1280 × 720 | **yes** | Prefer `<name>_hd.bik` in the movie player; same Bink revision | **Xbox** | S |
| Upscaled textures | See [AI upscaling](#ai-upscaling) | **partly** | A local pack made by `coney-tools`, loaded through F4 | F4; Xbox optional | L |
| Render distance | 115 m far clip with fog from 57.5 m; levels are built for that view | **partly** | A fog-distance scale and a far-clip scale, separate (raising only the far clip shows nothing new, because fog is full at the far clip). Expect visible level edges and unbuilt backs. A larger render-side `Sector Pool` keeps more parts loaded, but streaming decisions stay in the simulation and faithful | world streaming, now-choice 1 | S-M |
| LOD bias | No geometry LODs are known; humans fade beyond a set distance; the `d` world (decals, fences) streams separately | **partly** | Texture LOD bias (after mip generation); human fade distance as a render-side scale. Geometry LOD: nothing to bias (an open research question) | mips; research on object/human fade (`0x0050cc64`) | S |
| Tessellation | Characters are low-poly skinned meshes. PN triangles would round silhouettes but crack at hard edges and UV seams, warp faces and clip held objects | **partly**, low value | CPU subdivision of the bind pose at load (PN or Phong, skin weights interpolated), characters only: no GL 4 tessellation stage needed. SDL3's GPU API has no tessellation stage | F3 | M |

### Lighting and post-processing

Realtime shadows, per-pixel lighting and reflections have their own design page:
[Realtime lighting design](realtime-lighting.md). The rows below stay the summary.

| Option | What it means here | Feasible | Approach | Depends on | Cost |
| --- | --- | --- | --- | --- | --- |
| Shadow quality | Faithful: a faint 1 m blob under each human. Upgrade: humans and objects casting onto the world | **partly** | Keep the blob as *PS2*. From *Xbox* up: a shadow map from the level's shadow light (the Xbox port's), receivers multiply their final colour by the level's shadow colour (the world is prelit, so the shadow darkens baked light, a plausible fake); casters, receivers and the map per preset on [Renderer: Presets](../engine/renderer.md#presets) | F3; the Xbox port's shadow map ([#881](issue:881), [Renderer: lighting](../engine/renderer.md#lighting)) | M |
| SSAO / HBAO / GTAO | Prelit corners probably already hold baked occlusion (inferred). AO helps where nothing is baked: characters' feet, props, parked cars | **yes** | GTAO from F2's depth (normals rebuilt from depth), at half resolution, low strength, masked off the sky. A fragment-shader version runs on GL 3.3 and macOS | F2 | M |
| Ray tracing | There is no physical light model to trace: the light is baked colour. What is honest: RT shadows and AO **for dynamic objects** over the baked light; reflections need roughness or wetness data the game does not have (only MatFX environment-map materials hint at shine) | **partly** (shadows, AO), **no** (GI or path tracing that keeps the look) | Vulkan ray queries over a TLAS of scenery and humans, added to the GTAO and shadow-map results. RTX Remix is not a route: it hooks fixed-function D3D9, and librw's D3D9 backend uses shaders (inferred) | F5 plus a raw Vulkan / DXR / Metal path; Windows and Linux first | XL |
| Bloom | The original glows through coronas, `propglow` halos and the unresearched 512 × 256 glow raster. A threshold bloom would also bloom white walls and text | **yes** | Bloom only from what Coney knows glows (coronas, glow sprites, lamp sprites) drawn into a bloom buffer, plus an optional low threshold pass | F2; research on device slot `+0x108` | S |
| Motion blur | The original has one: frame feedback, strength set by looks and scripts. Coney's is a stand-in | **yes** | Keep the faithful blur, rate-independent (now-choice 2). Optional per-object velocity blur from the blended steps; camera blur from depth reprojection | F2, F3 for velocity | S (rate fix), M (velocity) |
| Chromatic aberration | Not in the original; purely cosmetic | **yes** | One post pass, off by default | F2 | S |
| Colour grading | The game tints through looks (`SetLevelColour`, stores) and the brightness option; PCSX2 shows the picture about 70% bright, the game's own colours ([Graphics](../research/graphics.md#open-questions)) | **yes** | A 3D LUT after the game's own looks; presets *Faithful TV* (the measured brightness) and *Neutral* | F2; the display-brightness research | S |
| Depth of field | No focus data. Cutscene cameras have targets, the follow camera has a look-at point 1.4 m above the player | **partly** | Cutscenes only: focus on the scene camera's target; gameplay off (it would blur the street) | F2 | S-M |
| HDR | Content is LDR on the GS scale (0x80 = 1.0); lit colours clamp at 1.0, so there is no overbright range (measured in the graphics research) | **partly**, low value | HDR10 / scRGB output with a paper-white setting; only additive effects (glows, coronas, bloom) could go above it. Not on OpenGL portably; SDL3's GPU API has HDR swapchains. Designed on [Renderer: HDR output](../engine/renderer.md#hdr) | F5 | M after F5 |

## AI upscaling {#ai-upscaling}

**Legal frame.** An upscaled texture is derived from the player's disc, and converting or upscaling it does not
change who owns it. It is made on the player's machine by a tool the player runs, written to a directory the player
chooses outside the repository, and never committed or hosted by the project
([LEGAL](repo:LEGAL.md#no-game-data)). Coney ships the tool and the loader, never a pack. `coney-tools` may download
a model's weights itself ([Decided](#decided)), but the project does not host them. Sharing follows the pack kinds
in [Texture packs](#packs).

**Pipeline** (a new `coney-tools textures` group beside [`wad extract`](coney-tools-disc.md#extract), refusing output
paths inside the repository as `wad extract` does):

1. **Export**: decode every distinct texture (20,582, deduplicated by texel hash) to RGBA, with alpha scaled from
   0-128 to 0-255. Record its dictionary hash, name, size, addressing and alpha kind (opaque, cut-out, blended),
   and its sprite-sheet rectangles if it has any. With the Xbox disc, take the Xbox texture as input where it is
   larger.
2. **Prepare**: for wrap textures, pad by wrapping (otherwise seams appear) and crop afterwards; for clamp
   textures, replicate the edge. Spread colour into fully transparent texels (otherwise dark halos appear). For
   4-bit palettes, optionally run a 1× de-banding or de-dithering model first; for Xbox input, a 1× DXT-artefact
   model.
3. **Upscale colour** with the chosen model (2× default, 4× optional). **Alpha separately**: cut-outs get a smooth
   upscale and a re-threshold; blended alpha gets a smooth upscale.
4. **Classify exceptions**: below 16 px, use a deterministic pixel-art scaler or skip. Fonts and HUD glyphs are
   never sent to a GAN: use xBRZ or a signed distance field built from the font atlas. Optionally skip textures
   with lettering (signs, graffiti) by a curated name list.
5. **Finish**: mips with alpha coverage kept, compression (BC7 needs GL 4.2 or `ARB_texture_compression_bptc`;
   librw knows DXT only, so either Coney uploads BC7 itself or the pack stores DXT5 or PNG), and a manifest keyed as
   F4 expects. The loader rejects any entry whose PS2 texel hash no longer matches.

**Candidates** (licences as their projects state them; check at adoption, and the maintainer decides):

| Tool or model | Licence | Fit |
| --- | --- | --- |
| Real-ESRGAN (code and official weights) | BSD-3-Clause | Good general baseline; x4plus over-sharpens flat PS2 surfaces |
| Real-ESRGAN-ncnn-vulkan, waifu2x-ncnn-vulkan | MIT | Standalone Vulkan executables on Windows, Linux and macOS: no Python GPU stack for the player; fixed model set |
| spandrel (loads ESRGAN, SwinIR, HAT, DAT, ... in PyTorch) | MIT; non-commercial architectures are kept in a separate package | Best fit for `coney-tools` (Python): any community model by file |
| Community models (the OpenModelDB catalogue: game-texture, de-dither and de-DXT models) | per model: CC0 and CC-BY to **CC-BY-NC-SA** | Highest quality for this content; licences vary, so the player supplies the model file |
| chaiNNer (GUI), Upscayl | GPL-3.0, AGPL-3.0 | For players who want to run their own chains on the exported images |
| xBRZ | GPL-3.0 | Deterministic; fonts, UI and tiny textures |
| ONNX Runtime / PyTorch | MIT / BSD-3-Clause | Runtimes |
| Diffusion upscalers, face restoration (GFPGAN, CodeFormer) | mixed, some non-commercial | **Not recommended**: invented detail, changed faces |

**Quality on this content** (from the formats; to be measured on the disc):

| Content | Expectation |
| --- | --- |
| Tiling scenery (brick, asphalt, pavement; 64 × 64 the most common) | Good at 2-4×, once seams are padded. Risk: invented detail that repeats visibly across tiles |
| Palettised gradients (sky, skin, smoke) | GANs sharpen banding into contours unless de-banded first |
| Cut-out alpha (fences, foliage) | Fine with alpha handled separately; wrong without it (fringes, holes) |
| Character textures (faces a few texels across) | Clothes are fine at 2×; faces turn waxy. Use a conservative model, 2× at most, and an opt-out per texture |
| Sprite sheets, HUD, fonts | Per-rectangle padding stops bleeding between rectangles. GANs smear glyphs; use the deterministic route |
| Text in the world (shop signs, graffiti) | Letters get distorted, and graffiti is the game's art style. Opt-out list |

**Size and time** (rough): the 2,027 matched texture sets alone hold 128 M PS2 pixels. At 4× that is about 2 G
pixels: about 8 GB as RGBA, about 2.7 GB as BC7 with mips. So 2× is the default. A GPU upscales 64 × 64 textures in
milliseconds each, so the whole disc takes minutes to an hour; on a CPU it takes hours.

### Texture packs {#packs}

Coney imports any pack placed in `overrides/`, keyed by the original texture's hash. An entry
whose hash matches no texture on the player's disc is ignored. `coney-tools` makes and exports packs as a folder
with a manifest. There are three kinds:

| Kind | Holds | Sharing |
| --- | --- | --- |
| Original art | textures drawn by the pack's author: no game pixels | shared freely, under the author's licence |
| **Recipe** (the easy default) | the manifest, the model (or where to download it) and the settings; no textures | shared freely; each player rebuilds the pack from their own disc with `coney-tools` |
| Pre-made upscale | upscaled game textures | players may import one; the project never hosts or links one |

## Order

The renderer's own work (F1, F2, F3 and F5, the frame-rate audit, anti-aliasing, the upscalers, render scale, HDR
output, the Xbox port's lighting and the realtime lighting options) is ordered on [Renderer:
Order](../engine/renderer.md#order) and is not repeated here. The other options follow, each once what it depends on has
landed:

| Group | After | Items |
| --- | --- | --- |
| 0 | Now | The five choices above; 16:9 mode; any aspect with HUD anchoring; FOV |
| 1 | F4 | Xbox textures and HD movies (**Xbox**); player texture packs ([roadmap](../roadmap.md#graphics-and-performance)); the upscaling tool |
| 2 | the GPU renderer's world ([Renderer](../engine/renderer.md#order), item 13) | Mip generation and anisotropic filtering |
| 3 | the scene target ([Renderer](../engine/renderer.md#order), item 22) | Colour grading; bloom from glow sprites; velocity motion blur; cutscene depth of field; chromatic aberration; fog and render distance scales; the *PS2 640 × 448* mode's CRT and interlace output |
| 4 | the GPU renderer as the default ([Renderer](../engine/renderer.md#order), item 21) | Ray-traced shadows and AO (they need a native path beside SDL3's GPU API, which has no ray tracing); character subdivision |

## Decided {#decided}

Maintainer decisions (2026-10-07):

- **Model weights**: `coney-tools` may download an upscaling model itself.
- **Proprietary SDKs**: DLSS and XeSS are allowed as optional plug-ins next to the GPL-3.0-or-later code.
- **Backend**: librw stays only as the reader of RenderWare data; Coney's own renderer draws through SDL3's GPU API,
  and its groundwork comes first ([Renderer: Backend](../engine/renderer.md#backend)). The GPU renderer's MSAA
  replaces any on the GL3 path.
- **Sharing texture packs**: Coney helps players share packs, in the model under [Packs](#packs). It imports from
  `overrides/` by texture hash and exports with `coney-tools`; recipe packs are the default; pre-made upscales can
  be imported but are never hosted or linked by the project.

## Questions for the maintainer

- Which model licences are acceptable for a download that `coney-tools` starts itself, and may the docs name
  non-commercial community models?

## Research this plan needs

One line each; owned by the graphics and HUD engineers.

- The 16:9 mode's 3D camera constants: **done** ([Graphics: video mode](../research/graphics.md#video-mode)). Still
  open: the HUD and menu 16:9 tables (`0x00211ef8`, `0x001af010`, `0x001cdc80`).
- Device slot `+0x108` and the heat distortion: **done** ([Graphics: motion blur](../research/graphics.md#motion-blur),
  [heat distortion](../research/graphics.md#code-distortion)).
- The display brightness: **done**, the dimness is the game's own colours and the capture is faithful
  ([Graphics: open questions](../research/graphics.md#open-questions), "Display brightness").
- Human and object fade distances and geometry LOD: **done**, fades only, no geometry LOD
  ([Graphics: distance](../research/graphics.md#lod)).
- What `GraphicsShadowXBox.cpp` draws: researched in [#881](issue:881).
