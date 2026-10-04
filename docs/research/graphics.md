# Graphics device, frame and textures

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims: PCSX2 was
not running when this page was written, so the frame rate below is settled from the code. The disc-side checks were
run on the NTSC-U disc (2026-10-04) and are reported as counts only.

## Purpose

Everything the player sees goes through one object: the game's **RenderWare graphics device**, a C++ wrapper around
RenderWare Graphics 3.7 for PS2. It starts RenderWare, chooses the video mode, owns the cameras and frame buffers,
clears and presents each frame, and reads texture dictionaries and worlds from the chunk system's streams. This page
is what the "First pixels" milestone needs: how the device is set up, what one frame does on the screen, why the game
runs at 30 frames a second, how textures are loaded and found by name, how the front end draws in 2D, and what the
first screen after the start-up movies is.

Coney draws through librw (an open reimplementation of RenderWare 3.7) on OpenGL 3, so most RenderWare calls below
have a direct librw equivalent; the PS2-specific parts (DMA packets, vertical-blank handling, GS registers) do not,
and the page says what behaviour they produce instead.

## Original structure

The device class is in `c:/Warriors/Source/Graphics/Devices/Renderware/DevRWGeneric.cpp` (the path string at
`0x005534d0`, passed by the allocation in `0x00194488`; the source map attributes `0x00192908`-`0x00197cd0` to the
file). Its base class, with constructor `0x0017a1c8` and vtable `0x00538b40`, sits in an unnamed `Graphics/` file
before the stub `0x0017b1c0` (inferred). The functions after `0x00197cd0` up to `0x00198d40` (the texture and world
slots, the camera wrapper) have no anchor of their own; they are probably the end of `DevRWGeneric.cpp` (inferred).
RenderWare itself is the library block `0x0044e060`-`~0x004aa000` ([Source map](source-map.md#middleware)); the
RenderWare function names used on this page are inferred from their arguments, the structures they touch and their
place in that block.

Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00194488` | `RwDevice_Create(progressive, widescreen)` | allocates the device (0x460 bytes), sets the globals, picks the screen mode | confirmed (code) |
| `0x00194d58` | `RwDevice::RwDevice` | constructor: vtable, ten camera wrappers, zeroes the mode fields | confirmed (code) |
| `0x00194e28` | `RwDevice_SetWidescreen(dev, on)` | flags and field-of-view constants for 4:3 or 16:9 | confirmed (code) |
| `0x0017a1e0` | `GraphicsDevice_Open(dev)` | base-class start-up: device slot `+0x08`, background colour, fog start | confirmed (code) |
| `0x00194558` | device slot `+0x08` `Init` | starts RenderWare and creates the cameras and rasters | confirmed (code) |
| `0x001958b0` | device slot `+0x30` `Present` | shows the main camera's raster | confirmed (code) |
| `0x00195c78` | device slot `+0x88` `SetUpViewport(i)` | split-screen camera for viewport `i` | confirmed (code) |
| `0x00197f70` | device slot `+0x150` `ReadTexDictionary(stream)` | RenderWare texture dictionary from a chunk stream | confirmed (code) |
| `0x001980e0` | device slot `+0x168` `FindTexture(dict, name)` | named lookup in one dictionary | confirmed (code) |
| `0x00197ba0` | device slot `+0x170` `ReadWorld(stream)` | RenderWare world (BSP) from a chunk stream | confirmed (code) |
| `0x001986b0` | `RwCam::RwCam` | camera wrapper constructor (0x60 bytes, vtable `0x00538f78`) | confirmed (code) |
| `0x001906e8` | chunk `0x0B` reader | texture dictionary, pushed as `0x0B` | confirmed (code) |
| `0x00190770` | chunk `0x2A` reader | texture dictionary through slot `+0x150`, pushed as `0x0B` | confirmed (code) |
| `0x00197df0` | `RwStreamCustom_Init(custom, stream)` | wraps a game stream in a RenderWare custom stream | confirmed (code) |
| `0x0018e898` | texture-dictionary holder destructor (`Graphics/Texture.cpp`) | destroys the dictionary's textures, the dictionary and its heap | confirmed (code); role inferred |
| `0x00410648` | `World_LoadStream` (`World/ps2/WorldPS2.cpp`) | reads `%s_sec.wld` | confirmed (code) |
| `0x004114d8`, `0x004110c0` | `World_LoadSectorAtomics` | read `%s_ms%i.sec` | confirmed (code) |
| `0x00159c08` | `StartupScreen_Draw` (mode 5) | draws the legal screen | confirmed (code) |
| `0x0048bdb0` | RenderWare PS2 vertical-blank handler | flips the display at most every second vertical blank | confirmed (code) |

## Data

### Globals

| Global | What | Evidence |
| --- | --- | --- |
| `0x0050cdb4`, `0x0050cdb8` | the device (both set by `0x00194488`) | confirmed (code) |
| `0x0050cdb0` | vertical blanks per displayed frame: **2** | confirmed (code), read by `0x00194aec` |
| `0x0050cdac` | `0x1c0000`: size of the PS2 DMA packet buffer handed to the driver (`0x0048e210`) | confirmed (code); meaning inferred |
| `0x0050b208` | aspect of the overlay camera's view window, 1.3333 (4:3) | confirmed (code); meaning inferred |
| `0x0050b20c` | overlay view-window scale: 1.0 (4:3) or 1.1 (16:9) | confirmed (code) |
| `0x0050b6f8` | "cameras set up this frame" flag, set by `0x001448c8` in slot `+0x28`, cleared by `Present` | confirmed (code) |
| `0x005fd260`... | static colour table built by `0x0017ae38` (black at `0x005fd260`, white at `0x005fd268`, ...) | confirmed (code) |

### The device object (0x460 bytes) {#device-object}

| Offset | Size | Meaning | Evidence |
| --- | --- | --- | --- |
| `+0x000` | 4 | mode flags: `0x01` interlaced, `0x02` PAL (speculative), `0x04` 16:9, `0x20` progressive (480p) | confirmed (code) at `0x00194558`, `0x00194e28`; names inferred |
| `+0x004` | 4 | pointer copied from `0x0050ce84` by `0x0017a1e0` | confirmed (code); meaning unknown |
| `+0x008` | 4 | vtable (`0x00538d78`; the base class's is `0x00538b40`) | confirmed (code) |
| `+0x010` | 0x60 | **main camera**: the 3D scene, full screen | confirmed (code) |
| `+0x070` | 4 × 0x60 | viewport cameras 0-3 (split screen), sub-rasters of the main camera's | confirmed (code) |
| `+0x1f0` | 0x60 | **overlay camera**: HUD and front end, shares the main frame and Z buffers | confirmed (code) |
| `+0x250` | 4 × 0x60 | viewport overlay cameras 0-3 | confirmed (code) |
| `+0x3d0` | 0x60 | effects camera, parallel projection, 512 × 256 Z buffer | confirmed (code) |
| `+0x430` | 4 | frame-buffer raster: width × height, camera raster, 32-bit 8888 (type `0x502`) | confirmed (code) |
| `+0x434` | 4 | Z raster, same size (type `1`) | confirmed (code) |
| `+0x43c` | 4 | 512 × 256 camera-texture raster for the blur effect (slot `+0x108`) | confirmed (code) |
| `+0x440` | 4 | background colour, RenderWare `RwRGBA` byte order (R, G, B, A) | confirmed (code) |
| `+0x444` | 4 | fog start as a fraction of the far clip, clamped to (0, 1); 0.5 at start-up | confirmed (code) at `0x00195168` |
| `+0x448` | 1 | cleared by slot `+0x10` | confirmed (code); meaning unknown |
| `+0x44c` | 4 | screen width, float: 640.0 | confirmed (code) |
| `+0x450` | 4 | screen height, float: 448.0 (480.0 in progressive mode) | confirmed (code) |
| `+0x454` | 4 | scale, float, 1.0 | confirmed (code) |
| `+0x458` | 4 | progressive-scan mode on | confirmed (code) |
| `+0x45c` | 4 | 16:9 on | confirmed (code) |

### Device vtable {#device-vtable}

Vtable `0x00538d78`, GCC 2 layout (8-byte `{delta, function}` entries, see [Compiler](compiler.md)); slot offsets
are from the vtable start, as on [Boot and the main loop](boot.md). Evidence: confirmed (code) at the function's
address for what it calls; the RenderWare call names are inferred.

| Slot | Function | What it does |
| --- | --- | --- |
| `+0x08` | `0x00194558` | `Init`: start RenderWare, cameras, rasters ([Start-up](#start-up)) |
| `+0x10` | `0x00195030` | clears byte `+0x448` |
| `+0x18` | `0x00195038` | **flush the render queue**: submits the PS2 packet buffer and waits for the previous one, twice (`0x0048c508`) |
| `+0x20` | `0x001951c0` | screen width and height as integers |
| `+0x28` | `0x00195400` | **set up the cameras** from the player camera: view window, near and far clip, fog distance, matrix ([A frame](#a-frame)) |
| `+0x30` | `0x001958b0` | **present**: the main camera's show-raster, then clears `0x0050b6f8` |
| `+0x38` | `0x001958f8` | **clear** the main camera (colour and Z) to the background colour `+0x440` |
| `+0x40` | `0x00195928` | clear the main camera to a given colour |
| `+0x48` | `0x00195060` | set the background colour; it is also the fog colour (fog type, density, colour and fog-enable render states, set inside a camera update) |
| `+0x50` / `+0x58` | `0x00195168` / `0x004e3928` | set / get the fog start `+0x444` |
| `+0x60` | `0x004e3930` | main camera (`this + 0x10`) |
| `+0x68` | `0x00195c48` | viewport camera `i` (`this + 0x70 + i × 0x60`) |
| `+0x70` | `0x004e3938` | overlay camera (`this + 0x1f0`) |
| `+0x78` | `0x00195c60` | viewport overlay camera `i` (`this + 0x250 + i × 0x60`) |
| `+0x80` | `0x004e3940` | effects camera (`this + 0x3d0`) |
| `+0x88` | `0x00195c78` | set up viewport `i` (split-screen sub-raster, clip planes, view window; creates the viewport cameras on first use) |
| `+0x90` | `0x00195238` | convert a GUI point to overlay-camera space ([2D drawing](#2d-drawing)) |
| `+0x98` | `0x00195330` | convert a GUI width (multiplies by width / height) |
| `+0xa0` | `0x001953a8` | viewport `i`'s rectangle in pixels |
| `+0xa8` / `+0xb0` | `0x004e3948` / `0x004e3950` | screen width / height (floats `+0x44c`, `+0x450`) |
| `+0xb8` / `+0xc8` | `0x004e3958` / `0x004e3968` | get / set progressive mode `+0x458` |
| `+0xc0` | `0x004e3960` | scale `+0x454` |
| `+0xd0` | `0x004e3970` | 16:9 flag `+0x45c` |
| `+0xd8` | `0x004e3978` | returns 0 |
| `+0xe0` | `0x00192c20` | build a screen-space textured quad: destination rectangle, raster, source rectangle |
| `+0xe8` | `0x00192f58` | draw that quad with a colour ([2D drawing](#2d-drawing)) |
| `+0xf8`, `+0x100` | `0x00193298`, `0x001939b8` | build and draw a 9 × 9 grid of the same, with wobbling UVs (a screen distortion) |
| `+0x108` | `0x00193d80` | blur or glow through the 512 × 256 raster `+0x43c` and the effects camera |
| `+0x110` | `0x00195c00` | destroy a raster |
| `+0x118` | `0x00195980` | calls slot `+0x120` |
| `+0x120` | `0x00195c20` | heat-distortion effects (`0x0019c438`, `DistortionEffectManager.cpp`) |
| `+0x128` | `0x00195be0` | screen effects (`0x0018dac0`, `ScreenEffectsManager.cpp`) |
| `+0x130` | `0x001959a8` | fill viewport `i` with a colour (fades): a full-screen RwIm2D triangle strip |
| `+0x138`, `+0x140` | `0x00196468`, `0x001964a0` | PS2 driver settings; no PC equivalent |
| `+0x150` | `0x00197f70` | read a texture dictionary ([Loading textures](#loading-textures)) |
| `+0x158` | `0x00198080` | destroy a texture dictionary |
| `+0x160` | `0x001980a0` | set the current texture dictionary (or none) |
| `+0x168` | `0x001980e0` | find a texture by name in one dictionary; on failure, prints the dictionary's contents to a debug buffer |
| `+0x170` | `0x00197ba0` | read a world (section `0x0B`) and install the game's sector render callback |
| `+0x178` | `0x00197d00` | destroy a world (detach its clumps and lights first) |

The vtable ends at `+0x180`; the zero entries at `+0x188` start the next vtable, `0x00538f08`, a small immediate-mode
drawing class in the same file (slot `+0x08` `0x001964c0` begins an overlay-camera update with a vertex list, slot
`+0x18` `0x001965d8` draws it with RwIm2D), used by particle code (confirmed (code); its users are not traced).

### Camera wrapper (0x60 bytes) {#camera-wrapper}

Vtable `0x00538f78`. `+0x00` vtable, `+0x04` the `RwCamera`, `+0x50`-`+0x5c` the sub-raster rectangle (x, y, w, h;
zero = whole screen). Confirmed (code) at `0x00198738`-`0x00198c80`; the RenderWare names are inferred from the
`RwCamera` fields each one touches (`+0x60` frame buffer, `+0x64` Z buffer, `+0x68` view window, `+0x78` view
offset, `+0x80` near, `+0x84` far, `+0x88` fog plane: the RenderWare 3.7 layout).

| Slot | Function | What it does |
| --- | --- | --- |
| `+0x08` | `0x00198750` | create the `RwCamera` and its frame; perspective projection |
| `+0x10` | `0x001987b8` | destroy |
| `+0x20` | `0x00198808` | identity matrix, near and far clip |
| `+0x28` | `0x00198890` | view offset (first pair) and view window (second pair) |
| `+0x30` / `+0x38` / `+0x40` | `0x001988f0` / `0x00198910` / `0x00198930` | near clip / far clip / fog distance |
| `+0x48` | `0x00198980` | projection: non-zero = perspective, zero = parallel |
| `+0x50` | `0x001989b0` | set the frame from a game matrix, converting axes (the game's x and z are negated) |
| `+0x58` | `0x00198a58` | sub-raster rectangle of the main camera's frame and Z buffers (split screen) |
| `+0x60` / `+0x68` | `0x00198940` / `0x00198960` | `RwCameraBeginUpdate` / `RwCameraEndUpdate` |
| `+0x70` | `0x00198b08` | `RwCameraClear(colour, flags)`: colour is `RwRGBA` packed R in the low byte; arguments 2 and 3 select image and Z |
| `+0x78` | `0x00198b60` | `RwCameraShowRaster(camera, NULL, 0)` |
| `+0x80`-`+0xb0` | `0x00198ba8`... | getters for view offset, view window, fog plane, near and far |
| `+0xb8` | `0x00198bf8` | frame matrix back in game axes |
| `+0xc0` | `0x00198c80` | rectangle in pixels |

### Video mode {#video-mode}

`Init` (`0x00194558`) walks RenderWare's video-mode list (90 modes, a table of 24-byte records `{width, height,
depth, flags, refresh, raster format}` at `0x0052ec38` in the PS2 driver) and picks the first with:

| | Interlaced (the default) | Progressive |
| --- | --- | --- |
| Width × height | 640 × 448 | 640 × 480 |
| Depth | 32 | 32 |
| Flags | `0x203` | `0x1` |
| Refresh in the table | 60 | 60 |

Confirmed (code) for the selection and the table. Flag `0x1` is RenderWare's "exclusive" and `0x2` "interlace";
`0x200` is a PS2-only flag, read-circuit anti-aliasing in the RenderWare 3.7 PS2 headers (inferred). For Coney this
means: a **640 × 448 logical screen** (4:3; 16:9 when the widescreen option is on), 32-bit colour with a Z buffer.

### RenderWare plugins {#plugins}

`Init` attaches, in this order (confirmed (code); plugin identities from their RenderWare plugin ids, inferred):
PDS pipelines (id `0x131`, `0x00461340`, then seven pipeline registrations through `0x00461420`), the world plugin
(`0x00471438`, ids `0x501`-`0x50b`), HAnim (`0x11E`, `0x00462940`), Skin (`0x116`, `0x00466a78`), Material Effects
(`0x120`, `0x00464e30`), PTank (`0x12F`, `0x0044eae8`), GPVS (`0x12A`, `0x004bede8`) and two of the game's own:

| Id | Attached to | Stream data | Read by | Evidence |
| --- | --- | --- | --- | --- |
| `0x3F1` | world sector (32 bytes of plugin data, offset in `0x0050ced0`) | 20 bytes: streamed-sector index, part number, origin of the sector's atomic | `0x00198e20` | confirmed (code); see [The streamed world](world.md#sector-plugin) |
| `0x3F0` | atomic (16 bytes, offset in `0x0050cd98`) | 12 bytes: two floats for the game's PS2 pipelines, one word | `0x00192688` | confirmed (code); meaning of the values open, see [The streamed world](world.md#atomic-plugin) |

**Disc check (corroboration):** RenderWare extension chunks in the WAD with the `0x1C02000A` stamp: `0x110` (PS2
sky mipmap value) 148,835; `0x11E` 102,149; `0x3F0` 34,857 (all 12 bytes); `0x120` 19,059; `0x3F1` 16,138 (all
20 bytes); `0x12A` 3,304; `0x116` 2,370; `0x182` 40 (unidentified). librw skips extensions it has no plugin for, so
the two game plugins only matter once their data is understood.

### Texture dictionaries on the disc {#texture-formats}

All textures are **PS2 native textures** (platform `PS2\0`) inside RenderWare texture dictionaries (section `0x16`).
A throwaway disc walk (2026-10-04, outside the repository) found 20,314 dictionary sections holding 42,211 native
textures, 20,582 of them distinct. Every one parsed.

| Depth | Palette | Mipmaps flag | Occurrences | Distinct |
| --- | --- | --- | ---: | ---: |
| 8 | 256 colours, 8888 | no | 32,333 | 13,508 |
| 4 | 16 colours, 8888 | no | 9,137 | 6,510 |
| 4 | 16 colours, 8888 | yes | 532 | 391 |
| 8 | 256 colours, 8888 | yes | 209 | 173 |

- **Every texture is palettised** (`PAL8` or `PAL4`) with a 32-bit palette; there are no true-colour textures.
- Sizes are all powers of two, from 4 × 4 to 512 × 512. Most common: 64 × 64 (9,807 distinct), 32 × 32 (5,938),
  128 × 128 (1,835), 16 × 16 (989), 256 × 256 (365), 512 × 512 (358); 2,489 are not square.
- Filter mode: 18,431 linear-mip-linear (`6`), 2,151 linear (`2`). Addressing: 20,559 wrap/wrap, 23 clamp/clamp.
- The five older streams stamped `0x1803FFFF` ([WAD contents](formats/wad-contents.md#renderware)) were not counted.

**Evidence:** inferred (data), counts as stated. PS2 colours put full intensity and opaque alpha at 128, not 255: the
game itself scales an alpha by 128 / 255 when it builds a sprite (`0x001a2690`, confirmed (code)). Whether librw's
PS2 texture reader expands palette alpha from 0-128 to 0-255 needs checking before the first textured frame
(speculative).

### Particle pages (sprite sheets) {#particle-pages}

Chunk `0x4C` beside a texture dictionary is a **sprite sheet**: rectangles of texture coordinates into the
dictionary's texture, each `{u0, v0, u1, v1}` as floats (inferred from `0x00181e38`, which returns rectangle `i` as
16 bytes, and from the data). The legal screen's page has one rectangle, `(1/2048, 1/2048)` to
`(1 - 1/2048, 0.75 - 1/2048)`: the top 512 × 384 of its 512 × 512 texture, inset half a texel (data, inferred). The
front end and the HUD draw sprites from these pages ([2D drawing](#2d-drawing)). The full layout, now confirmed
(code) and checked on all 1,335 pages, the sprite batches and fonts are on [GUI](gui.md#particle-page).

## Behaviour

### Start-up {#start-up}

From `DS_PS2Device_Init` ([Boot](boot.md#device-initialisation)), step 6. Confirmed (code) for the order.

1. **`RwDevice_Create(0, 0)`** (`0x00194488`): allocate 0x460 bytes, construct (`0x00194d58`: base constructor,
   vtable, the ten camera wrappers), store the device in `0x0050cdb4` and `0x0050cdb8`, set progressive mode off
   (slot `+0xc8`), then `RwDevice_SetWidescreen(dev, 0)`: 4:3, field-of-view constants 1.3333 and 1.45 handed to
   the camera system (`0x00122ca0`, `0x00122d38`), `0x0050b20c` = 1.0.
2. **`GraphicsDevice_Open`** (`0x0017a1e0`) calls `Init` (slot `+0x08`), then sets the background colour to the
   static white (`0x005fd268`, `{255, 255, 255, 255}`) and the fog start to 0.5.

`Init` (`0x00194558`):

1. Screen 640 × 448, flags `0x01`, scale 1.0; in progressive mode 640 × 480 and flags `0x20`; 16:9 adds `0x04`.
2. Print the largest free block ("... Before Initializing RenderWare"), set RenderWare's free-list sizes, and
   `RwEngineInit` (`0x00483530`) with a 256 KB resource arena.
3. Attach the plugins ([above](#plugins)).
4. `RwEngineOpen` (`0x004833e8`), choose the video mode ([above](#video-mode)), `RwEngineSetVideoMode`.
5. **Two vertical blanks per displayed frame** (`0x0048df98(2)`, the value from `0x0050cdb0`), see
   [Presenting and the frame rate](#frame-rate).
6. `RwEngineStart` (`0x00483258`); on failure close and terminate RenderWare.
7. Install a callback the driver calls while it waits for the GPU (`0x0048df90(0x00154ee8)`).
8. Create the main, overlay and effects cameras (camera slot `+0x08`). Create the frame-buffer raster (640 × 448,
   camera, 8888) and the Z raster, and give both to the main and the overlay camera; give the effects camera a
   512 × 256 Z raster and a parallel projection.
9. Background colour white, fog start 0.5; set RenderWare's texture-read callback to `0x00194550`, which returns
   no texture (so a texture missing from every dictionary is never looked for in a file; inferred from what the
   callback slot is); Bink set-up (`0x00429a98`); create the 512 × 256 raster `+0x43c`.

### A frame {#a-frame}

What [one in-game frame](boot.md#one-frame) does on the screen, confirmed (code) at `0x0015d160` and its callees:

1. **Cameras** (`0x001562c8`): device slot `+0x28` with the player-1 camera's view window, near and far clip and fog
   distance and its matrix. That sets the main camera, each viewport's camera (fog distance = far clip × fog start)
   and the overlay cameras: view offset 0, view window `(0x0050b20c × 0x0050b208 × 0.5, 0x0050b20c × 0.5)` =
   (0.6667, 0.5), near 0.5, far 10,000,000, identity matrix. Then slot `+0x38` **clears the frame buffer and Z** to
   the background colour.
2. Simulation runs; then slot `+0x18` flushes the render queue before the world update (why there: inferred, so that
   streaming does not free data the GPU still reads).
3. **Each viewport** (`0x00156408`): slot `+0x88` positions viewport `i`'s cameras on their part of the screen
   (columns × rows from `0x0050b1a8`/`0x0050b1aa`; one viewport covers the whole screen); lights; the world
   (`0x0040e8d8`: several `RwCameraBeginUpdate`/`EndUpdate` passes: the level world with culling off, the streamed
   world's sectors with back-face culling, objects, the detail world, water, translucent objects; Z test and write
   on and fog on except where stated; the full order is on [The streamed world](world.md#a-frame)); resources;
   particles; slot `+0x118` (heat distortion).
4. **Overlays** (`0x00156658`): screen effects (slot `+0x128`), the HUD, subtitles and the front-end layers, each
   followed by the overlay pass `0x00185d20` ([2D drawing](#2d-drawing)).
5. **Present** (slot `+0x30`), then file streaming.

The world pass also adjusts the **draw distance** to the measured frame rate: while the rate (`0x0050c680`) is below
29.5 (24.5 when flag `0x02` is set) the far clip shrinks, down to 60 − 10 × viewports, and otherwise grows back
toward the camera's own far clip (`0x0040e8d8`, confirmed (code); that `0x0050c680` is the debug counter's frames per
second is inferred). While the rate is good, the draw distance instead follows the distance of the nearest scenery
that is not loaded yet, so missing sectors stay beyond the far clip ([The streamed world](world.md#a-frame)).

### Presenting and the frame rate {#frame-rate}

The game runs at **30 frames a second (29.97 on NTSC) because the PS2 driver shows a new frame at most every second
vertical blank.** Confirmed (code):

- `Present` (`0x001958b0`) calls the main camera's show-raster with flags 0 (the 1 that the device passes in is
  dropped by camera slot `+0x78`), which reaches the driver's show-raster (`0x00498890`, standard function 20 in the
  table at `0x0052f538`). That queues a **flip token** into the DMA chain (`0x0048c8f0`; tokens `0x40` and `0x41`
  alternate by frame).
- The DMA handler (`0x0048b708`, installed for channels 1 and 2 by `0x0048d9f0`) stops at a flip token and marks a
  flip as pending.
- The **vertical-blank interrupt handler** (`0x0048bdb0`, installed with `AddIntcHandler(2, ...)` by `0x0048d9f0`)
  counts vertical blanks since the last flip (`0x005970f9`). Until the count reaches the limit (`0x00596d70`, set to
  2 at start-up) it only counts. Once it has, and a flip is pending, it switches the displayed buffer (GS `DISPFB1`
  and `DISPLAY1`), resets the count to 1 and restarts the DMA chain.
- The CPU cannot run ahead: submitting the next frame's packet buffer (`0x0048c508`) waits until the previous one
  has gone, so the game loop is held to the flip rate.

So a frame is shown for at least two vertical blanks. A frame that takes longer is shown at the first vertical
blank after it is ready (three blanks, 20 frames a second), while game time still advances by the fixed 1/30 s: the
game slows down rather than skipping. One exception: `0x00159468(0)` sets a 33 ms limit (`0x00596d9c`); if that much
time has passed since the last flip when the flip token is reached, the DMA handler flips at once without waiting
for a vertical blank (it tears rather than drops to 20). `0x00159468(1)` sets 65,535 ms, which turns this off. Mode
8 turns it off when it resumes (`0x0015c6f8`); mode 1 turns it off when it is suspended or left and chooses per frame
in its `Update` (`0x00158728`, from a screen-effect state). Why the game allows tearing there is not known.

For Coney: present once per fixed step of 1/30 s. With a 60 Hz display that is a swap interval of 2; the engine's
test mode needs no display at all.

### Loading textures {#loading-textures}

Texture dictionaries reach the game four ways, all through RenderWare's stream reader over the game's own stream (a
custom stream, `0x00197df0`: close, read, write and skip functions over a game stream object):

| Source | Code | What it does | Evidence |
| --- | --- | --- | --- |
| chunk `0x0B` | `0x001906e8` | find section `0x16`, read the dictionary, push it as chunk `0x0B` | confirmed (code) |
| chunk `0x2A` | `0x00190770` | device slot `+0x150` reads it (the stream object is allocated in the `Level Dynamic & LUA Pool` heap, the dictionary in the current heap); make it current and at once current = none; push as `0x0B` | confirmed (code) |
| world stream `%s_sec.wld` | `0x00410648` → `0x00410a50` | `u32` part count, then a dictionary (made current and back to none), then the world (section `0x0B`) | confirmed (code) |
| sector atomics (world parts) `%s_ms%i.sec` | `0x004114d8` → `0x004110c0` | skip the 16-byte header, a dictionary (current, then none), `u32 n`, then `n` × `{u32 sector, atomic (section 0x14)}`, each atomic placed in its sector | confirmed (code); formats on [The streamed world](world.md) |

The world reader also sets RenderWare's sector render callback (`0x00411b20`) and checks every atomic's textures
(`0x00198190`, which prints "Potential Crash from Missing Texture" for a missing one).

**The 1,911 WAD entries that are a 16-byte header `{1, 0, 0, id}` followed by a RenderWare stream are the sector
atomics files**, read by `0x004114d8`/`0x004110c0` in `World/ps2/WorldPS2.cpp`. They are not chunk containers.
Confirmed (code) for the reader; the match to the entries is a disc check (corroboration): exactly 1,911 entries start
with `{1, 0, 0, id}` and a `0x16` section stamped `0x1C02000A`, and all 1,911 are now named `<world>_ms<i>.sec`, with
`id` the CRC-32 of that name ([The streamed world](world.md#file-names)).

Each texture dictionary loaded in a resource pack has a holder object (`Graphics/Texture.cpp`, vtable `0x00538d60`)
that, when the resource is freed, destroys every texture in the dictionary, the dictionary and the resource's heap
(`0x0018e898`, confirmed (code); that the holder belongs to the resource manager is inferred).

### Finding textures by name {#texture-lookup}

Materials find their textures by name while a model or world is read, through RenderWare's texture lookup. The game
leaves the **current texture dictionary unset** at all times except the moment right after a dictionary is read
(`0x001980a0` is the only setter outside the world loaders, and every caller sets it back to none). With no current
dictionary, RenderWare's default find function (`0x00489170`) searches **every loaded dictionary**, in the order of
RenderWare's dictionary list, and returns the first texture with that name; with a current dictionary it searches
only that one. Confirmed (code) at `0x00489170`. So texture names form one global name space, first match wins; the
list order (RenderWare adds a new dictionary at the head of its list, so the newest is searched first) is inferred
from RenderWare's behaviour, not read here. A name found nowhere gives no texture (the read callback, step 9 of
`Init`).

Game code looks up a texture by name in one particular dictionary through device slot `+0x168` (used by the HUD and
effects; callers not traced).

### 2D drawing {#2d-drawing}

There is no 2D layer in pixels. The front end and the HUD draw **sprites through RenderWare's PTank plugin**
(particle tanks) seen by the overlay camera; a few effects use RwIm2D quads.

**Sprites (front end, HUD, legal screen).** A widget (`GUI/BaseWidget.cpp`) owns a resource-manager instance (id at
widget `+0xc4`) whose atomic is a PTank using a [particle page](#particle-pages)'s texture. Each frame the widget
adds one sprite to it (`0x001a2690` → `0x00182de0`: position, size, colour, texture rectangle), plus a drop shadow
when it has one (the same sprite offset by (0.0025, 0.004), black, at the widget's alpha × 128/255). Then the overlay
pass (`0x00185d20`) renders, through the overlay camera with **Z test and Z write off** and culling off, the overlay
world and every queued PTank in sorted order (per viewport for viewport-bound ones), and empties them again
(`0x00185cc8`). Confirmed (code).

**Coordinates.** A GUI point `(x, y)` with `x` to the right and `y` down, the whole screen being about `[0, 1]²`,
becomes overlay-camera space through slot `+0x90` (`0x00195238`), confirmed (code):

```text
X = (x - 0.5) * W / H        # W, H = 640, 448: the device's width and height
Y = 0.5 - y
depth = 1.1                  # in front of the overlay camera
```

Widths go through slot `+0x98` (× W / H). The overlay camera has a perspective view window of (0.6667, 0.5) and an
identity matrix, so on the screen `x` = 0 and 1 land at about 1.3 % and 98.7 % of the width and `y` = 0 and 1 at
about 4.5 % and 95.5 % of the height: a built-in safe-area margin (computed from the constants above, inferred).

**Screen quads.** Device slots `+0xe0`/`+0xe8` build four RwIm2D vertices for a destination rectangle in pixels, with
texture coordinates from a source rectangle in texels plus half a texel, and draw them as two triangles (indices
`{0, 1, 2, 0, 2, 3}` at `0x0050cdd8`) with: nearest filtering, fog off, Z test and Z write off, vertex alpha on,
source blend `SRCALPHA`, destination blend `INVSRCALPHA`; afterwards Z test and write go back on and the raster is
unset. Slot `+0x130` fills a viewport with a flat colour the same way (a four-vertex triangle strip, Z write off,
culling off). Confirmed (code).

### The first screen {#first-screen}

After the three start-up movies, `main` pushes modes 8, 6 and 5, so **mode 5 runs first**
([Boot](boot.md#main)). Mode 5 is the **legal screen**, confirmed (code):

1. `Enter` (`0x00159a58`) records the start time, sets a 5,000 ms timeout and calls `StartupScreen_Draw`
   (`0x00159c08`).
2. `StartupScreen_Draw` picks a resource name by language (`W_GameState + 0x120`: 0 English, 1 Spanish, 2 French, 3
   Italian, 4 German) and aspect: `legal_screen`, `legal_screen_sp`, `_fr`, `_it`, `_ge`, with a `_w` variant of
   each for 16:9 (`legal_screen_w`, `legal_screen_w_sp`, ...); with flag `0x02` set and English it uses
   `legal_screen_euro`. The **WAD file name is the decimal CRC-32 of that resource name** (`"%u"`, `0x0054efc8`).
3. It loads that resource through the resource manager and waits for it, servicing the file manager. For each of the
   two display buffers it clears the overlay camera to opaque black, draws the page's first sprite, sized from the
   overlay camera's near clip times a per-mode factor (centred: inferred), and shows the raster. Then it releases the resource.
4. `Update` (`0x00159ae0`) draws nothing and presents nothing: it ticks the timer, reads the pads and leaves once
   5,000 ms have passed. The display keeps showing the last flipped buffer.

The scale factors (horizontal, vertical) by mode flags, confirmed (code): interlaced 4:3 (1.55, 1.35), interlaced
16:9 (1.9, 1.45), progressive 4:3 (1.35, 1.19), progressive 16:9 (1.6, 1.25), neither flag (1.0, 1.0) or with 16:9
(1.45, 1.19). How these combine with the sprite's placement to fill the screen exactly is not worked out (see
[Open questions](#open-questions)).

**Disc check (corroboration):** all eleven names exist in `WARRIORS.DIR` under their decimal CRC. `legal_screen`
(CRC-32 863681355) is entry 3,480: 263,680 bytes, one resource of two chunks, a `0x2A` texture dictionary holding one
512 × 512 8-bit palettised texture and a `0x4C` particle page with one rectangle (the top 512 × 384). So the faithful
first screen is that image, scaled to fill a black 640 × 448 screen, held for five seconds; then mode 6 (memory card
checks) and mode 8 take over.

This naming scheme names the whole first block of the WAD: see [WAD contents](formats/wad-contents.md#names).

## Coney's implementation

First pixels (2026-10-04), in `src/platform/` and `src/graphics/`:

- `RenderEngine` (`src/platform/render_engine.h`) starts librw, built for its GL3 platform with SDL3. librw's GL3
  device creates the window and an OpenGL 3.3 core context itself, so the engine owns the window and the event loop
  gets a non-owning `Window`. A run-time NULL backend (`--headless`) installs librw's NULL device instead, for CI and
  tests: no window, no GPU. It implements `graphics::RenderDevice` (begin a frame cleared to a colour, present),
  which game modes call as the original's modes call the device; the idle mode clears and presents every frame.
  There are no device cameras or 30 Hz present yet: one camera covers the window, and the present waits for every
  vertical blank (librw's GL3 device sets a swap interval of 1, not the 2 of [the frame rate](#frame-rate)).
- **The logical screen** (`src/graphics/screen.h`): every 2D position is in the 640 × 448 pixels of
  [the video mode](#video-mode). Coney shows that screen at the television's shape, 4:3 (16:9 later, with the
  widescreen option), as the largest such rectangle centred in the window (`fitLogicalScreen`), and fills the rest of
  the window black: **letterboxing or pillarboxing is Coney's choice** for a window that is not 4:3; the original
  has a television picture and no border. The window opens at 960 × 720. `beginFrame(colour)` clears the window to
  black and the logical screen to the colour (with a flat quad, since librw's clear covers the whole frame buffer).
- **2D quads** (`RenderDevice::drawQuads`): textured or flat rectangles in logical pixels with a colour and texture
  coordinates, drawn with Z test and Z write off, no culling, no fog, vertex alpha on and source alpha / inverse
  source alpha blending ([2D drawing](#2d-drawing)), with the texture's own filter mode and clamped addressing. Game
  code passes a `graphics::Texture`, which the platform implements over a librw texture.
- **GUI coordinates and the overlay camera** (`src/graphics/overlay_camera.h`, `OverlayCamera`): device slots `+0x90`
  and `+0x98` as [2D drawing](#2d-drawing) gives them (`guiToOverlay`, `guiWidthToOverlay`), and the overlay camera's
  perspective projection with its view window (0.6667, 0.5) × the view scale, which takes a point or a size of
  overlay-camera space to logical pixels (`project`, `projectSize`; `unproject` for Coney's tools). GUI 0 and 1 land
  at 1.3 % and 98.7 % across and 4.5 % and 95.5 % down, the margin the page computes; a test pins it. Coney computes
  the projection itself rather than rendering through a librw camera, and draws the result as 2D quads.
- `TextureDictionary` (`src/platform/texture_dictionary.h`) reads a dictionary with librw after
  `graphics::inspectTexDictionary` has checked the stream, and converts it to RGBA images (any backend) or to OpenGL
  textures (`Raster::convertTexToCurrentPlatform`). The chunk readers for `0x0B` and `0x2A` push it as `0x0B`
  ([Chunk system](chunk-system.md#coneys-implementation)); sector atomics files and world streams are read at their
  fixed offsets by `loadTextureDictionaries`. Dictionaries are not kept per resource and there is no lookup by name
  yet.
- **Alpha:** librw's PS2 reader scales alpha by 255/128 when it converts a raster to an image (palette entries and
  32-bit texels alike), so 128 becomes 255; that answers the open question below, and a unit test pins it.
- **librw and the game's data:** librw recomputes each PS2 texture's GS layout and asserts that the stream agrees;
  some of the game's textures disagree (the palette's place), so librw is built without its asserts and keeps the
  stream's values. Its reader also copies as many bytes as the stream's header says into buffers it sized itself; for
  five textures on the disc (tiny 4-bit ones, such as a 2 × 2) the stream's sizes are larger and the copy would
  overrun the heap, so Coney leaves those textures out of their dictionaries (`skippedTextures()`).
- `coney --disc <disc> --view-txd <entry>` draws an entry's textures in a grid with RwIm2D quads, nearest filtering,
  Z off and source-alpha blending, as the device's screen quads do ([2D drawing](#2d-drawing)); `--screenshot` saves
  the last frame. The legal screen (`--view-txd 863681355`) shows correctly.
- **The first screen** is mode 5 ([Front end](frontend.md#coneys-implementation)): `coney --disc <disc>` loads
  `legal_screen` through the chunk system (its `0x2A` dictionary and its `0x4C` sprite sheet,
  [GUI](gui.md#coneys-implementation)) and draws the sheet's first rectangle, the top 512 × 384 of the texture, over
  the whole logical screen on black for 5,000 ms. **Coney's choice:** the picture fills the logical screen exactly;
  the original sizes it from the overlay camera's near clip and the per-mode factors, whose result is not worked out
  (TODO below). On the NTSC-U disc it shows correctly and gives way to the idle mode after frame 150.

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[disc]"` with `CONEY_DISC` set reads 20,314
dictionaries (from 3,016 chunk containers, 1,911 sector atomics files and 159 world streams) holding 42,211 textures;
42,206 are read and converted to RGBA images, the 5 above are left out, no entry fails. By format: 32,333 `PAL8`,
9,132 `PAL4`, 209 `PAL8` with mipmaps and 532 `PAL4` with mipmaps, all with 32-bit palettes and raster layout version
2.

What is still to do:

- A device object that owns librw's engine start-up (with the HAnim, Skin, MatFX and world plugins; PTank for
  sprites) and the cameras of [the device object](#device-object): main, per-viewport, overlay and effects, sharing
  one frame and Z buffer.
- `Present` once per fixed 1/30 s step ([frame rate](#frame-rate)); a test mode that renders without a display.
- Clear to the background colour (white until a level sets one; the legal screen clears to black) with Z.
- Texture dictionaries read with librw from the chunk stream (`0x0B`, `0x2A`, world streams, sector atomics) and kept
  per resource; texture lookup by name across all loaded dictionaries, newest first (check the order), no file
  fallback.
- PS2 native textures: 4- and 8-bit palettised, 32-bit palettes, power-of-two sizes up to 512; check alpha scaling.
- Readers for the game's plugin data `0x3F0` (atomic, 12 bytes) and `0x3F1` (sector, 20 bytes): the streamed world
  needs both ([The streamed world](world.md#coneys-implementation)).
- Sprites: GUI coordinates `[0, 1]²` mapped as in [2D drawing](#2d-drawing), drawn after the 3D scene with depth test
  and write off and source-alpha blending.
- The 16:9 option: a 16:9 logical screen shape, the overlay view-window scale 1.1 and the `_w` legal screens.

TODO for the analysts, found while implementing:

- **Legal-screen placement:** how the near clip and the factors (1.55, 1.35) turn into the sprite's position and
  size, and so whether the picture fills the screen (Coney's choice) or leaves a border. Taken at face value with the
  overlay camera's view window, the factors give a picture narrower in proportion than the screen, which would
  distort a 4:3 image, so something in the reading is missing.
- **Names for device slots `+0x90` and `+0x98`:** the `@orig` tags call them `RwDevice::GuiToOverlay`
  (`0x00195238`) and `RwDevice::GuiWidthToOverlay` (`0x00195330`) until the research database names them.
- **The legal screen's colour:** what colour `StartupScreen_Draw` gives its sprite; Coney draws it white (unchanged).

## Open questions

- **Exact legal-screen placement.** The sprite's position and size come from the overlay camera's near clip and the
  per-mode factors; whether the image fills the screen exactly, or leaves borders, is not worked out. A PCSX2
  screenshot of the legal screen would settle it.
- **Mode flag `0x02`.** Read as PAL from the 24.5 frames-a-second threshold and the `_euro` screen; nothing that sets
  it has been found (speculative).
- **What the game plugins hold** (answered for `0x3F1`): see [The streamed world](world.md#sector-plugin). For `0x3F0`
  the two floats feed the game's PS2 pipelines; what they mean is open ([The streamed world](world.md#open-questions)).
- **The overlay world** (`ResourceManager + 0x9034`) rendered before the sprites. The sort order of the queued
  PTanks is answered on [GUI](gui.md#draw-order): ascending key; that the 2D key is the creation depth is inferred.
- **librw and PS2 alpha:** does librw's PS2 native texture reader scale palette alpha from 0-128 to 0-255?
- **Texture dictionary list order** for name lookups (newest first is RenderWare's usual behaviour; not read here).
- **The remaining slots**: `+0xf0` (`0x001931f8`, not a defined function in our Ghidra project), `+0x148`
  (`0x004dee48`), `+0x180` (`0x004e3820`), and the byte `+0x448`.
- **Runtime confirmation** of the two-vertical-blank flip with PCSX2 (a watch on the vertical-blank count
  `0x005970f9` and the flip-pending flag `0x0059708b` while the game runs).
