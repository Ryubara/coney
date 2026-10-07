# Rendering: the anatomy of a frame

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), running under PCSX2 2.9.94:
single-frame GS dumps of the [reference views](#reference-views), parsed register by register.

## Purpose

What the Graphics Synthesizer is told to do in one in-game frame, pass by pass, in the order it happens: the
primitives, the blend equation, the alpha test, the Z test and write, fog, texture filtering and the frame-buffer
formats. It is the "how it looks" companion to [Graphics](graphics.md#a-frame), which says which code issues each
pass, and to the RenderWare PS2 driver, which turns RenderWare render states into GS registers
([PS2 render driver](ps2-render.md)). A reimplementation that matches the tables below draws the same
picture; the differences found so far in Coney are tracked outside the repository and fixed against these sections.

Every value below is **confirmed (runtime)** from the GS dumps of the reference views unless a line says otherwise;
"code" points to the page where the issuing function is documented.

## How the frames were captured

PCSX2's single-frame GS dump (the `GSDumpSingleFrame` hotkey, Shift+F8, posted to the window by handle) records the
GS state at the start of the frame, the 4 MB of GS memory and every GIF packet until two frames have been drawn
(one into each frame buffer). A scratch parser replays the packets, tracks every register write and groups the
primitives into runs that share one state. The two frames of each dump agree pass for pass.

A view at any chosen camera, such as a scripted shot played without the script, is drawn by the game itself: over
PINE, player 1's follow camera object (`0x005d9158`) gets the locked camera's vtable (`0x005362d0`), the shot's
position at `+0x1e0` and orientation at `+0x1f0`, its field of view, near and far, and is made current
(`0x005d9150`); the locked update ([Scripted camera angles](camera.md#scripted-angles)) then places it each frame
and the world streams round it. The people and cars stay wherever the save state had them.

### Reference views {#reference-views}

Camera values are read from the current camera of player 1 (`0x005d9150`, [Camera](camera.md#the-base-camera-object):
position `+0x10`, orientation quaternion `+0x20` as (x, y, z, w), field of view `+0x48`, near / far `+0x50` /
`+0x54`). The orientation is the identity when the camera looks along +y with +z up.

| View | Where | Camera position | Orientation (x, y, z, w) | Field of view, near, far |
| --- | --- | --- | --- | --- |
| street | `level99` checkpoint 3, the street at night, Rembrandt walking away from the camera | (80.319, 41.122, 2.815) | (−0.07495, −0.07494, 0.70306, 0.70319) | 65°, 0.1, 115 |
| store | `level99`, inside the gift shop in front of the glass cabinets | (50.929, 51.816, 2.819) | (−0.13968, 0.00042, −0.00294, 0.99019) | 65°, 0.1, 115 |
| car | `level99`, beside a parked car with Ash, steam from a grate | (57.090, 44.868, 2.815) | (0.00802, −0.10177, 0.99170, −0.07816) | 65°, 0.1, 115 |
| fence | `level99`, a fence ahead, three pedestrians | (48.434, 48.591, 2.703) | (−0.00023, −0.10529, 0.99444, 0.00222) | 65°, 0.1, 115 |
| arena | `level99`, the initiation's last fight: lamps, the objective marker, the crowd behind the fence | (−284.326, 121.271, 2.792) | (−0.07781, −0.09395, 0.76440, 0.63309) | 65°, 0.1, 115 |
| l80_letsgo | `level80` checkpoint 1, the "Let's Go" tutorial's first locked camera (`StartCam1`), the Warriors in a hall with a pink lamp and vent steam | (−194.746, 130.353, 2.699) | (0.00498, −0.03360, 0.98860, −0.14667) | 65°, 0.1, 150 |
| title, menu | the front end (`level100`): the title screen and the main menu over the Wonder Wheel | (462.602, −122.348, −187.930) | (0.00012, 0, −0.29090, 0.95675) | 54.43°, 0.5, 150 |

## State shared by every pass {#shared-state}

| Register | Value | Meaning |
| --- | --- | --- |
| `FRAME_1`, `FRAME_2` | `PSMCT32`, `FBW` 10, `FBP` 140 or 0 | 32-bit colour, 640 pixels wide; two buffers at block 140 and block 0 (× 2,048 words), drawn alternately, both contexts the same |
| `ZBUF_1`, `ZBUF_2` | `0x31000118` | 24-bit Z (`PSMZ24`) at block `0x118`; Z test enabled in every pass (`ZTE` 1); a larger Z is nearer |
| `SCISSOR` | (0, 639, 0, 447) | the whole 640 × 448 frame (the clears use 2047 × 2047) |
| `DTHE` | 0 | no dithering |
| `COLCLAMP` | 1 | colours are clamped to 0-255, not wrapped |
| `PABE`, `FBA` | 0, 0 | blending is per pixel by the source alpha; the alpha written is the source alpha unchanged |
| `TEXA` | `TA0` 0, `TA1` `0x80`, `AEM` 0 | only matters for 16- and 24-bit textures; the game's are palettised (`T8`, `T4`) |
| `PRMODECONT` | `AC` 1 | primitive attributes come from `PRIM` |
| `TEX0` | `TFX` `MODULATE`, `TCC` 1 | every texture is multiplied by the vertex colour, alpha included |
| `TEX1` | `MMAG` linear, `LCM` 0, `L` 0 | bilinear magnification everywhere; minification per pass (below) |
| `PRIM` | `IIP` 1, `FST` 0 | Gouraud shading; texture coordinates as `STQ` (perspective-correct) |

**Colours are in GS units, where `0x80` is 1.0.** `MODULATE` gives texture × vertex colour / 128 for each channel and
for alpha, so a vertex colour of `0x80` shows the texture as it is, and values above it brighten up to twice. The
prelit night world uses vertex colours of about `0x1d`-`0x50` (the street's darkest walls are `0x1d1d1d`, about 23%
of the texture), and every opaque vertex's alpha is `0x80`.

**The blend is the same almost everywhere:** `ALPHA` `0x44`, Cv = (Cs − Cd) × As / 128 + Cd, the ordinary "source
over" mix by the source alpha (texture alpha × vertex alpha / 128). Only the character overlay pass uses another
value ([Characters](#characters)).

**Fog** is the GS's per-vertex fog: each vertex carries a fog value F (255 = no fog, 0 = all fog), interpolated across
the primitive, and the pixel becomes (F × colour + (255 − F) × `FOGCOL`) / 255 after texturing and before blending.
`FOGCOL` was (12, 12, 5) in every `level99` view and (17, 12, 15) in `level80`'s (it is the level's fog colour,
confirmed (runtime)); how F is computed from the distance is RenderWare's linear fog ([PS2 render
driver](ps2-render.md#fog)).

The two GS contexts are used deliberately: **context 1** for the first pass of everything (alpha test on:
`ATST` GEQUAL, `AREF` `0x40`, `AFAIL` `FB_ONLY`), **context 2** for second passes, sprites and text (alpha test off,
or `ATST` NOTEQUAL with `AREF` 0). With `AFAIL` `FB_ONLY`, a pixel whose alpha is below `0x40` is still blended into
the colour but does not write Z.

## The passes, in order {#passes}

The order is that of [a frame](graphics.md#a-frame); the numbers are the street view's (frame A), the other views
differ only in counts.

| # | Pass | Primitives | Blend | Alpha test | Z test / write | Fog | Texture filter | Code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | [Clear](#clear) | 40 untextured sprites over 640 × 448 | none | off | always / write Z = 0 | no | none | [a frame](graphics.md#a-frame), device slot `+0x38` |
| 2 | [Sky and clouds](#sky) | 2 strips, `T8` 256 × 512 and 512 × 256 | `0x44` | GEQUAL `0x40` | GEQUAL / **no write** | no | bilinear, no mipmaps | [the background](level-loading.md#render-order) |
| 3 | Skyline | 1 batch of 1,837 strip vertices, `T8` 32 × 16 | `0x44` | GEQUAL `0x40` | GEQUAL / write | no | bilinear | [the background](level-loading.md#render-order) |
| 4 | Z clear | 40 untextured sprites, `FBMSK` `0xffffffff` (colour masked) | none | off | always / write Z = 0 | no | none | [the background](level-loading.md#render-order) |
| 5 | [The world](#world) | strips, `T8` / `T4` 16-256 texels | `0x44` | GEQUAL `0x40` | GEQUAL / write | yes | **trilinear** (`MMIN` 5), `K` per texture | [the streamed world](world.md#a-frame) |
| 6 | [Second layers](#dual) | the same strips again, a second texture | `0x44` | **off** | GEQUAL / write | yes | as the first layer | [world pipelines](world.md) (MatFX dual) |
| 7 | Objects and props | strips | `0x44` | GEQUAL `0x40` | GEQUAL / write | yes | bilinear (`MMIN` 1) | [object drawing](objects.md) |
| 8 | [Characters](#characters) | strips, two passes per mesh | `0x44`, then the overlay's | GEQUAL `0x40`, then off | GEQUAL / write | yes | bilinear | [character drawing](graphics.md#code-characters) |
| 9 | Translucent objects, objective markers | strips | `0x44` | GEQUAL `0x40` | GEQUAL / **no write** | yes | bilinear | [the streamed world](world.md#a-frame), step 10 |
| 10 | [3D sprites](#sprites) | sprites and strips, `T8` 512 × 256 particle pages | `0x44` | NOTEQUAL 0 | GEQUAL / **no write** | yes | bilinear | [3D sprite batches](gui.md#draw-order) |
| 11 | [HUD](#hud) | radar fans (`T4` 256 × 256, clamped), text sprites (`T8` 512 × 256 font page) | `0x44` | radar GEQUAL `0x40`, text NOTEQUAL 0 | **always** / no write | no | bilinear | [HUD](hud.md) |
| 12 | [Level tint](#tint) | 1 untextured fan over the whole screen | `0x44` | off | GEQUAL / no write | no | none | [screen effects](graphics.md#screen-effects) |
| 13 | [Motion blur](#screen-overlays) | 1 fan over the whole screen textured with the previous frame | `0x44` | GEQUAL `0x40` | always / no write | no | **nearest** | [screen effects](graphics.md#screen-effects) |

No pass in any of the four views draws a shadow: the characters stand on the ground with nothing under them.

### Clear {#clear}

The colour clear writes the level's **background colour** with alpha `0x80`, the same value as `FOGCOL` in every
`level99` view (12, 12, 5) and in `level80` (17, 12, 15), so the far edge of the fogged world meets the background
without a seam. Z is cleared
to 0 (the farthest value under GEQUAL).

### Sky and clouds {#sky}

Drawn with Z test GEQUAL against the cleared Z (always passes) and **no Z write**, no fog, bilinear without
mipmaps; the sky box's Z values are near the top of the range (about 1,456,800 to 16,775,539), the camera's
translation being zeroed for it ([the background](level-loading.md#render-order)). The skyline that follows writes
Z, then a second clear resets Z (colour masked) so the world, drawn with its own near and far planes, covers it.

### The world {#world}

The streamed world's sectors are the only geometry minified with mipmaps: `MMIN` 5 (linear mipmap linear) with `L`
0 and a per-texture `K` from −7.6 to −2.0 (most −4 to −5); most textures have `MXL` 0 (no mip levels, so the mode
reduces to bilinear), a few 4-bit ones `MXL` 3. Objects, props, characters, the sky and everything 2D use `MMIN` 1
(bilinear). Repeat addressing (`CLAMP` 0) everywhere except the radar.

Vertex alpha is `0x80` for opaque geometry; a few world and prop strips carry lower values (`0x20`, `0x64` in the
street view, `0x55` on a distant prop in the car view), which the `0x44` blend turns into translucency while Z is still
written.

### Second layers {#dual}

Materials with the MatFX **dual** effect ([world pipelines](world.md)) draw their geometry twice back to back: first
the base texture in context 1, then, at once, the same strip with a second texture (128 × 128 `T8` in these views)
in **context 2**: blend `0x44` (the `ALPHA_2` value also carries `FIX` `0x80`, unused by this equation), **no alpha
test**, Z test GEQUAL with Z write, fog on, the same vertex colours and fog values as the first pass. The second
texture's alpha therefore decides where it shows (decals, dirt and grime on walls).

### Characters {#characters}

Every human is drawn mesh by mesh, each mesh twice in a row (the code: [Drawing a human](graphics.md#human-draw)):

1. Context 1: its 256 × 256 `T8` texture, blend `0x44`, alpha test GEQUAL `0x40`, Z write, fog, bilinear.
2. Context 2: the same vertices (same colours, a second set of texture coordinates) with a 64 × 64 `T8` texture,
   alpha test **off**, Z write, fog. The blend is `ALPHA_2` = A 0, B 1, C 0, D 1 (`0x44`, the texture shows by its
   alpha) for Rembrandt in every view, and A 1, B 1, C 0, D 1 (`0x45`: (Cd − Cd) × As + Cd = Cd, the pass changes
   nothing) for Ash and every pedestrian seen.

The second pass is the human material's MatFX **dual** texture, a shared **blood** texture chosen by the human's
health *h* in percent: 60 and above `charblood_d1`, 30-60 `charblood_d2`, below 30 `charblood_d3` (resource manager
`+0x74` / `+0x78` / `+0x7c`, set with `RpMatFXMaterialSetDualTexture`, `0x00465810`). The blend word of the material's
effect is `0x44` when *h* is below 90 and `W_GameState + 0x454` is 0, otherwise `0x45`, so a human at 90% health or
more shows no blood. Confirmed (code) at `HumanRender_Draw` (`0x00174320`, read by the graphics analyst; the texture
names confirmed (runtime) in `level99`); `+0x454` is only ever written as 0, so it is a no-blood switch the retail
game never sets (inferred). In the store view Rembrandt, under 90%, shows blood on his arms. Characters are lit per
vertex (prelit plus the lights, [Lighting](lighting.md)); the vertex colours of both passes are equal.

### 3D sprites {#sprites}

Particles (the steam from the grates, [Steam vents](particles.md#steam)) are `SPRITE` primitives and short strips
textured from a 512 × 256 particle page, in context 2: alpha test NOTEQUAL 0 (only fully transparent texels are
dropped), blend `0x44`, Z test GEQUAL, **no Z write**, fog on with the vertex fog value 254. Their vertex alphas are low
(about `0x02`-`0x38`), so each puff adds a little. `level80`'s vent steam at the top of the frame is the same class of
batch (17 sprites, grey `0x7f`, alpha 3-38, the same particle page, context 2, NOTEQUAL 0, fog, no Z write), confirmed
(runtime).

### Health rings and blood {#rings}

In a fight (`level99`, the Bumper Bash yard, a GS dump with Rembrandt hit and an enemy targeted; confirmed (runtime))
the ring on the ground under the target ([The health rings](hud.md#the-health-rings)) is one list of 64 triangles
(`TRI`, 192 vertices) textured from the same 512 × 256 particle page as the steam, in **context 1** (alpha test
GEQUAL `0x40`, `AFAIL` `FB_ONLY`), blend `0x44`, Z test GEQUAL **with Z write**, **no fog**, drawn after the
particles and before the tint. Its vertex colours are (38, 61, 13) green for the health it shows and (30, 30, 30)
grey for the rest, both alpha `0x7f`. The blood is a batch of `SPRITE`s from the same page in context 2 (alpha test
NOTEQUAL 0, no Z write, fog value 254), vertex colours (76, 10, 10) and (60, 0, 10) at alphas `0x6b`-`0x6f` among
grey dust puffs at `0x16`-`0x23`. The frame also carries the motion-blur fan of [Motion blur](#screen-overlays)
(vertex alpha 7).

### HUD {#hud}

The radar is two triangle fans of a 256 × 256 `T4` texture with **clamp** addressing (the only clamped texture
seen), alpha test GEQUAL `0x40`, Z test **always**, no Z write, no fog, at Z `0x7fffff`; the vertex alpha `0x78` makes
the disc 94% opaque. The objective text and its box are sprites from a 512 × 256 `T8` font page in context 2 (alpha test
NOTEQUAL 0, Z always), the box's vertex alpha `0x3e`-`0x40` (half transparent), the text `0x80`.

### Level tint {#tint}

The last primitive of each frame is a full-screen untextured fan of colour (7, 20, 30) with alpha `0x15` (21 / 128,
16%) in `level99`, blend `0x44`, Z test GEQUAL at Z `0xfffffe` (always passes), no Z write: a blue-green wash over
everything, **including the radar and the HUD text**, which were drawn before it. It is the screen-effects tint
([Screen effects](graphics.md#screen-effects), the `SetLevelColour` look), filled at once rather than queued. In
`level80` the colour is the same but the alpha is `0x19` (25 / 128), so the alpha (and so the whole tint) is per
level, from the level script's `SetLevelColour`, not a constant; confirmed (runtime).
`GameMode_DrawOverlays` (`0x00156658`) normally runs the screen effects after the HUD (`0x001b1688`) and its overlay
flush and before the captions, which stay on top of the tint; it runs them before the HUD instead while
`W_GameState + 0x268` is above 0 or a blur pulse runs in either view. Confirmed (code) at `0x00156658` (read by the
graphics analyst) and confirmed (runtime) for the normal order; no caption was on screen in these views.

**Per level** (confirmed (runtime), a GS dump of each level in play, PCSX2 2026-10-07; the script values read from
the compiled level scripts). The fog colour and the clear colour are the level script's `SetFogColor(r, g, b)`
× 255, truncated ([Lighting](lighting.md)); the tint is `SetLevelColour(LightData.overlay)`, called by `global.lua`'s
`SetupCharacterLights` with the level's lighting table, and differs in colour as well as alpha:

| Level | `SetFogColor` | `FOGCOL` = clear | Tint (r, g, b), alpha / 128 |
| --- | --- | --- | --- |
| `level99` | 0.05, 0.05, 0.02 | (12, 12, 5) | (7, 20, 30), 21 |
| `level80` | 0.07, 0.05, 0.06 | (17, 12, 15) | (7, 20, 30), 25 |
| `level87` | 0.012, 0.018, 0.035 | (3, 4, 8) | (0, 25, 45), 15 |
| `level2` | 0.05, 0.05, 0.02 | (12, 12, 5) | (38, 45, 51), 28 |
| `level3` | 0.02, 0.02, 0.02 | (5, 5, 5) | (0, 0, 5), 12 |
| `level5` | 0.04, 0.025, 0.057 | (10, 6, 14) | (56, 61, 51), 28 |
| `level34` | 0.02, 0.02, 0.02 | (5, 5, 5) | none, in the intro scene and in play (the subway tunnel at the start) |
| `level95` | 0.05, 0.05, 0.02 | (12, 12, 5) | (19, 19, 38), 31 |

**How the fill is drawn** (`ScreenFx_DrawTint` `0x0018ca50` → `ScreenFx_FillViewport` `0x0018cc20` → device slot
`+0x130`, `RwDevice_FillViewport` `0x001959a8`), confirmed (code) at those addresses:

1. **The colour.** `SetLevelColour` (`0x0018e5f0`) and `EnterStore` (`0x0018e6b8`) take the script's four floats
   0-1 and store each × 255, truncated (`Float_ToUInt`), as the look's packed tint, bytes red, green, blue, alpha
   (look 9 and look 10). The tint blend (`ScreenFx_BlendTintTo`) lerps the packed colour from `+0x1bc` to the target
   `+0x1c4` by the time left ([Looks](graphics.md#looks)); a tint whose alpha byte is 0 is not drawn, nor any tint
   while the current look is 2 with `+0x1b8` 0.
2. **The fill.** Four RwIm2D vertices over the viewport, each vertex colour the tint's four bytes as floats
   0-255, with the raster unset (`rwRENDERSTATETEXTURERASTER` 0), culling off (state 20 = 1), vertex alpha on
   (state 12 = 1), fog off (state 14 = 0) and Z write off (state 8 = 0, put back on after), drawn as a triangle
   strip. The source and destination blends are not set, so the current ones apply: the driver default
   `SRCALPHA` / `INVSRCALPHA`.
3. **What reaches the GS** (confirmed (runtime), the dumps above): `ALPHA` = `0x44`, that is A = Cs, B = Cd,
   C = As, D = Cd, so **Cd' = (Cs − Cd) × As / 128 + Cd** per channel, clamped (`COLCLAMP` 1); `FIX` is not used.
   **RGB is passed 0-255 as stored; the alpha is scaled to 0-128**: As = the alpha byte × 128 / 255, truncated.
   Every level matches: `level2`'s overlay (0.15, 0.18, 0.2, 0.22) is stored (38, 45, 51, 56) and drawn
   (38, 45, 51) with As 28; `level87`'s (0, 0.1, 0.18, 0.12) is (0, 25, 45, 30), drawn with As 15; the table's
   (0.03, 0.08, 0.12, 0.2) is (7, 20, 30, 51), drawn with As 25 (`level80`). Below 255, × 128/255 truncated
   and ÷ 2 truncated give the same As; at 255 the scale gives 128, which no dump shows (inferred from the driver's
   colour conversion elsewhere, `0x0049ea80`, which maps alpha by × `0x808081` >> 24: 255 → 128).

So in 0-1 terms the wash is `out = dst + (round_down(255 × rgb) / 255 − dst) × As / 128`: the opacity is about the
script's alpha (0.12 → 15/128 = 0.117), **not** twice it, and the colour is not halved. The game-over tint
`0xd0000014` ([Camera](camera.md#death-camera)) is red 20, green 0, blue 0 at As 104 (81%).

### Motion blur {#screen-overlays}

In the arena view (not in the street, store, car or fence views) one more full-screen fan follows the tint
([Motion blur](graphics.md#motion-blur)): its
texture is **the other frame buffer**, the previous frame (`TEX0` `PSMCT32`, 1024 × 512 with `TBW` 10, `TBP` = that
buffer's `FBP` × 32), `TCC` 0 so the alpha comes from the vertex: colour `0x80` with alpha `0x07`, the old frame laid
over the new at 7/128 (5.5%) with **nearest** filtering, Z test always, no Z write. It is the screen effects'
motion blur ([Screen effects](graphics.md#screen-effects)), whose strength is the current look's.

### The front end {#front-end}

The title screen and the main menu draw through the same passes as a level: a black clear, a small sky (`T4` 16 × 16
strips, no Z write), a Z clear, the world (trilinear, `K` down to −13.2) and the Wonder Wheel's objects, then the
menu text sprites and the tint. The same tint as in `level99` ((7, 20, 30), alpha 21) is drawn last, so the black
background shows as a very dark blue-grey.

**Fog and colour on the title screen** (the `fe_title` dump, confirmed (runtime)): `FOGCOL` is **black** (0, 0, 0)
and the fog is the usual linear curve ([Fog](ps2-render.md#fog)) for the scene camera's far clip of **150** and the
default fog start **0.5**: 255 up to 75 m, 0 at 150 m (fit 74.7 m and 151.2 m). The wheel stands 54-100 m from the
camera, so its vertex fog values run only from **255 down to 168**: its far side darkens by at most a third, it never
fades out. The values that reach 0 are those of the 16 × 16 `T4` world strips beyond 90 m. What keeps the wheel's
frame dark is its **vertex colour** (on the GS's scale, 128 = 1.0): of the three batches that span the wheel's
outline (bilinear, `K` −4; inferred to be its objects), the largest (`32 × 32 T8`, 2,561 vertices) has 68 on every
vertex, a `128 × 128 T8` batch 5-12, and a `32 × 32 T8` batch (338 vertices, the bright outline: inferred, the neon
tubes and the bulbs) a full 128; the nearby world strips have 29. Which object draws which batch was not identified.
**No sprite, particle or corona** is drawn for the bulbs: the frame has no batch between the world and the menu
text, so the bulbs are geometry at full vertex colour (confirmed (runtime) for the absence; the bulbs' batch
inferred).

The Rumble screens (Game Mode, Choose Gangs, [Front end](frontend.md)) draw no world and **no tint**: a black clear,
then one 512 × 512 `T4` sprite, the grey gang-logo collage, modulated by a per-screen vertex colour, (24, 49, 50)
alpha `0x7f` on Game Mode (teal) and (25, 61, 38) alpha `0x80` on Choose Gangs (green), then the `T8` font sprites
(text `0x59` grey, shadows `0x28`, half-alpha boxes). Choose Gangs' two 3D fighters are drawn exactly like characters
in a level ([Characters](#characters): each mesh twice, the second pass `0x45` at full health), with Z test GEQUAL
and fog value 255 (no fog). The sprites carry fog value 254 with `FOGCOL` black. Confirmed (runtime), PCSX2 GS dumps
of both screens.

## Output {#output}

The frame buffer is presented as an interlaced field-mode picture (`SMODE2` 1, `PMODE` `0x8067`): both read circuits
show the same 640-wide buffer, one from line 0 (`DISPLAY1` height 448) and one from line 1 (`DISPLAY2` height 447,
`DISPFB2` `DBY` 1), mixed half and half (`ALP` `0x80`), so every output line is the average of two neighbouring frame
lines: a slight vertical blur that softens the image and hides the jagged edges of thin lines. The driver side is
[The picture on the TV](ps2-render.md#video-output).

## Open questions

- The views not captured yet: rain, interiors with lights, water, glass breaking, split screen.
- `level80`'s pink lamp has one 254-vertex strip with a 64 × 32 `T8` texture in context 1 (alpha test GEQUAL `0x40`, no
  Z write) drawn after the world: which object draws it (a lamp shade, or a corona-like glow, [Lighting](lighting.md))
  is not known.
