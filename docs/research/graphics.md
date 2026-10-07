# Graphics device, frame and textures

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made
in PCSX2 2.9.94 (memory reads and writes over PINE, and PCSX2's own screenshots measured in scratch, never kept in
the repository); each says so where it is made. The disc-side checks were run on the NTSC-U disc (2026-10-04) and
are reported as counts only.

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
| `0x0050b208` | aspect of the overlay camera's view window. Its static value 1.3333 is overwritten when the device starts (`0x00194634`) and when the mode changes (`0x00194e28`): **1.45** for interlaced 4:3 (the default), 1.6667 for 16:9 (with the scale below 1.1), 1.59 for progressive | confirmed (code) at those addresses; confirmed (runtime), PCSX2 2.9.94: `0x3fb9999a` (1.45) at the front end and in a fight |
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

| Slot | Function | What it does | Evidence |
| --- | --- | --- | --- |
| `+0x08` | `0x00194558` | `Init`: start RenderWare, cameras, rasters ([Start-up](#start-up)) | confirmed (code) |
| `+0x10` | `0x00195030` | clears byte `+0x448` | confirmed (code) |
| `+0x18` | `0x00195038` | **flush the render queue**: submits the PS2 packet buffer and waits for the previous one, twice (`0x0048c508`) | confirmed (code) |
| `+0x20` | `0x001951c0` | screen width and height as integers | confirmed (code) |
| `+0x28` | `0x00195400` | **set up the cameras** from the player camera: view window, near and far clip, fog distance, matrix ([A frame](#a-frame)) | confirmed (code) |
| `+0x30` | `0x001958b0` | **present**: the main camera's show-raster, then clears `0x0050b6f8` | confirmed (code) |
| `+0x38` | `0x001958f8` | **clear** the main camera (colour and Z) to the background colour `+0x440` | confirmed (code) |
| `+0x40` | `0x00195928` | clear the main camera to a given colour | confirmed (code) |
| `+0x48` | `0x00195060` | set the background colour; it is also the fog colour (fog type, density, colour and fog-enable render states, set inside a camera update) | confirmed (code) |
| `+0x50` / `+0x58` | `0x00195168` / `0x004e3928` | set / get the fog start `+0x444` | confirmed (code) |
| `+0x60` | `0x004e3930` | main camera (`this + 0x10`) | confirmed (code) |
| `+0x68` | `0x00195c48` | viewport camera `i` (`this + 0x70 + i × 0x60`) | confirmed (code) |
| `+0x70` | `0x004e3938` | overlay camera (`this + 0x1f0`) | confirmed (code) |
| `+0x78` | `0x00195c60` | viewport overlay camera `i` (`this + 0x250 + i × 0x60`) | confirmed (code) |
| `+0x80` | `0x004e3940` | effects camera (`this + 0x3d0`) | confirmed (code) |
| `+0x88` | `0x00195c78` | set up viewport `i` (split-screen sub-raster, clip planes, view window; creates the viewport cameras on first use) | confirmed (code) |
| `+0x90` | `0x00195238` | convert a GUI point to overlay-camera space ([2D drawing](#2d-drawing)) | confirmed (code) |
| `+0x98` | `0x00195330` | convert a GUI width (multiplies by width / height) | confirmed (code) |
| `+0xa0` | `0x001953a8` | viewport `i`'s rectangle in pixels | confirmed (code) |
| `+0xa8` / `+0xb0` | `0x004e3948` / `0x004e3950` | screen width / height (floats `+0x44c`, `+0x450`) | confirmed (code) |
| `+0xb8` / `+0xc8` | `0x004e3958` / `0x004e3968` | get / set progressive mode `+0x458` | confirmed (code) |
| `+0xc0` | `0x004e3960` | scale `+0x454` | confirmed (code) |
| `+0xd0` | `0x004e3970` | 16:9 flag `+0x45c` | confirmed (code) |
| `+0xd8` | `0x004e3978` | returns 0 | confirmed (code) |
| `+0xe0` | `0x00192c20` | build a screen-space textured quad: destination rectangle, raster, source rectangle | confirmed (code) |
| `+0xe8` | `0x00192f58` | draw that quad with a colour ([2D drawing](#2d-drawing)) | confirmed (code) |
| `+0xf0` | `0x001931f8` | the 9 × 9 grid's index list (8 × 8 cells, two triangles each) at `0x005ff470` | confirmed (code) |
| `+0xf8`, `+0x100` | `0x00193298`, `0x001939b8` | build and draw a 9 × 9 grid of the same, with wobbling UVs: the motion blur's form for some looks ([Motion blur](#motion-blur)) | confirmed (code) |
| `+0x108` | `0x00193d80` | blur through the 512 × 256 raster `+0x43c` and the effects camera ([Motion blur](#motion-blur)) | confirmed (code) |
| `+0x110` | `0x00195c00` | destroy a raster | confirmed (code) |
| `+0x118` | `0x00195980` | calls slot `+0x120` | confirmed (code) |
| `+0x120` | `0x00195c20` | heat-distortion effects (`0x0019c438`, [Heat distortion](#code-distortion)) | confirmed (code) |
| `+0x128` | `0x00195be0` | screen effects (`0x0018dac0`, `ScreenEffectsManager.cpp`) | confirmed (code) |
| `+0x130` | `0x001959a8` | fill viewport `i` with a colour (fades): a full-screen RwIm2D triangle strip | confirmed (code) |
| `+0x138`, `+0x140` | `0x00196468`, `0x001964a0` | PS2 driver: `+0x138` sets bit 4 in raster `0x00596e1c`'s plugin word before the blur and distortion passes (meaning not traced); `+0x140` returns raster `0x00596e18`, which the motion blur samples (inferred: the frame just shown) | confirmed (code) |
| `+0x150` | `0x00197f70` | read a texture dictionary ([Loading textures](#loading-textures)) | confirmed (code) |
| `+0x158` | `0x00198080` | destroy a texture dictionary | confirmed (code) |
| `+0x160` | `0x001980a0` | set the current texture dictionary (or none) | confirmed (code) |
| `+0x168` | `0x001980e0` | find a texture by name in one dictionary; on failure, prints the dictionary's contents to a debug buffer | confirmed (code) |
| `+0x170` | `0x00197ba0` | read a world (section `0x0B`) and install the game's sector render callback | confirmed (code) |
| `+0x178` | `0x00197d00` | destroy a world (detach its clumps and lights first) | confirmed (code) |

The vtable ends at `+0x180`; the zero entries at `+0x188` start the next vtable, `0x00538f08`, a small immediate-mode
drawing class in the same file (slot `+0x08` `0x001964c0` begins an overlay-camera update with a vertex list, slot
`+0x18` `0x001965d8` draws it with RwIm2D), used by particle code (confirmed (code); its users are not traced).

### Camera wrapper (0x60 bytes) {#camera-wrapper}

Vtable `0x00538f78`. `+0x00` vtable, `+0x04` the `RwCamera`, `+0x50`-`+0x5c` the sub-raster rectangle (x, y, w, h;
zero = whole screen). Confirmed (code) at `0x00198738`-`0x00198c80`; the RenderWare names are inferred from the
`RwCamera` fields each one touches (`+0x60` frame buffer, `+0x64` Z buffer, `+0x68` view window, `+0x78` view
offset, `+0x80` near, `+0x84` far, `+0x88` fog plane: the RenderWare 3.7 layout).

| Slot | Function | What it does | Evidence |
| --- | --- | --- | --- |
| `+0x08` | `0x00198750` | create the `RwCamera` and its frame; perspective projection | confirmed (code); RenderWare name inferred |
| `+0x10` | `0x001987b8` | destroy | confirmed (code); RenderWare name inferred |
| `+0x20` | `0x00198808` | identity matrix, near and far clip | confirmed (code); RenderWare name inferred |
| `+0x28` | `0x00198890` | view offset (first pair) and view window (second pair) | confirmed (code); RenderWare name inferred |
| `+0x30` / `+0x38` / `+0x40` | `0x001988f0` / `0x00198910` / `0x00198930` | near clip / far clip / fog distance | confirmed (code); RenderWare name inferred |
| `+0x48` | `0x00198980` | projection: non-zero = perspective, zero = parallel | confirmed (code); RenderWare name inferred |
| `+0x50` | `0x001989b0` | set the frame from a game matrix, converting axes (the game's x and z are negated) | confirmed (code); RenderWare name inferred |
| `+0x58` | `0x00198a58` | sub-raster rectangle of the main camera's frame and Z buffers (split screen) | confirmed (code); RenderWare name inferred |
| `+0x60` / `+0x68` | `0x00198940` / `0x00198960` | `RwCameraBeginUpdate` / `RwCameraEndUpdate` | confirmed (code); RenderWare name inferred |
| `+0x70` | `0x00198b08` | `RwCameraClear(colour, flags)`: colour is `RwRGBA` packed R in the low byte; arguments 2 and 3 select image and Z | confirmed (code); RenderWare name inferred |
| `+0x78` | `0x00198b60` | `RwCameraShowRaster(camera, NULL, 0)` | confirmed (code); RenderWare name inferred |
| `+0x80`-`+0xb0` | `0x00198ba8`... | getters for view offset, view window, fog plane, near and far | confirmed (code); RenderWare name inferred |
| `+0xb8` | `0x00198bf8` | frame matrix back in game axes | confirmed (code); RenderWare name inferred |
| `+0xc0` | `0x00198c80` | rectangle in pixels | confirmed (code); RenderWare name inferred |

### Video mode {#video-mode}

`Init` (`0x00194558`) walks RenderWare's video-mode list (90 modes, a table of 24-byte records `{width, height,
depth, flags, refresh, raster format}` at `0x0052ec38` in the PS2 driver) and picks the first with:

| | Interlaced (the default) | Progressive |
| --- | --- | --- |
| Width × height | 640 × 448 | 640 × 480 |
| Depth | 32 | 32 |
| Flags | `0x203` | `0x1` |
| Refresh in the table | 60 | 60 |

Confirmed (code) for the selection and the table. Flag `0x1` is RenderWare's "exclusive" and `0x2` "interlace"; `0x200`
is a PS2-only flag, read-circuit anti-aliasing in the RenderWare 3.7 PS2 headers (inferred). How the driver then sends
the picture to the TV (field mode with a two-line anti-flicker mix) is on [The PS2 render
driver](ps2-render.md#video-output). For Coney this means: a **640 × 448 logical screen** (4:3; 16:9 when the widescreen
option is on), 32-bit colour with a Z buffer.

**The camera constants per mode** (`RwDevice_Init`, `0x001945cc`-`0x001946b0`, and `RwDevice_SetWidescreen`,
`0x00194e28`), confirmed (code). The 3D aspect goes to every camera (`Cameras_SetAspectAll`, `0x00122ca0`: global
`0x0050b204` and each camera's `+0x4c`), the overlay aspect to `0x0050b208` (`Camera_StoreHudAspect`, `0x00122d38`):

| Mode | 3D aspect | Overlay aspect | Overlay scale `0x0050b20c` | Field of view added `0x0050b178` | Device flags |
| --- | --- | --- | --- | --- | --- |
| Interlaced 4:3 (default) | 1.3333 (`0x3faaaa8f`) | 1.45 | 1.0 | 0 | `0x01` |
| Interlaced 16:9 | **1.6667** (`0x3fd5551d`) | 1.6667 | 1.1 | 0 | `0x01` + `0x04` |
| Progressive (480p; device `+0x458`) | 1.59 (`0x3fcb851f`) | 1.59 | 1.0 | **3°** | `0x20` (+ `0x04` when 16:9 is chosen) |

A camera's view window is `(tan(h) × 0.75 × aspect, tan(h) × 0.75)` with `h` half of (field of view + the added
degrees) ([The streamed world](world.md#player-camera)), so 16:9 widens the picture by 1.25 and keeps its height: the
player camera's 65° gives (0.637, 0.478) in 4:3 and **(0.796, 0.478)** in 16:9, and (0.804, 0.506) in progressive
mode. The 16:9 window is 5:3 rather than 16:9 (1.667 against 1.778), so on a 16:9 screen the original is stretched
about 6.7 % sideways (inferred from the constants). In progressive mode the 16:9 choice sets the flag but keeps the
1.59 aspect. The overlay camera's window `(scale × overlay aspect × 0.5, scale × 0.5)` is (0.725, 0.5) in 4:3,
(0.917, 0.55) in 16:9 and (0.795, 0.5) in progressive mode ([2D drawing](#2d-drawing)). The HUD's own 16:9 positions
are on [HUD](hud.md).

### RenderWare plugins {#plugins}

`Init` attaches, in this order (confirmed (code); plugin identities from their RenderWare plugin ids, inferred):
PDS pipelines (id `0x131`, `0x00461340`, then seven pipeline registrations through `0x00461420`), the world plugin
(`0x00471438`, ids `0x501`-`0x50b`), HAnim (`0x11E`, `0x00462940`), Skin (`0x116`, `0x00466a78`), Material Effects
(`0x120`, `0x00464e30`), PTank (`0x12F`, `0x0044eae8`), GPVS (`0x12A`, `0x004bede8`) and two of the game's own:

| Id | Attached to | Stream data | Read by | Evidence |
| --- | --- | --- | --- | --- |
| `0x3F1` | world sector (32 bytes of plugin data, offset in `0x0050ced0`) | 20 bytes: streamed-sector index, part number, origin of the sector's atomic | `0x00198e20` | confirmed (code); see [The streamed world](world.md#sector-plugin) |
| `0x3F0` | atomic (16 bytes, offset in `0x0050cd98`) | 12 bytes: two floats for the game's PS2 pipelines, one word | `0x00192688` | confirmed (code); the first float scales the packed positions (disc check), see [The streamed world](world.md#atomic-plugin) |

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

**Mipmap level at runtime**, confirmed (runtime) from PCSX2 GS dumps of `level99` (11,923 world vertices): the GS
picks a texture's level by distance, not by screen size. `TEX1` has `LCM` 0 and `L` 0, so `LOD = log2(1/Q) + K`, and
`1/Q` is the vertex's camera distance in metres (it runs from 3.8 to 115.0, the far clip, over the frame). `K` is
the raster header's `+0x3c` ([RenderWare formats](formats/renderware.md#ps2-raster)), -2 to -7.6 on the textures
seen. With `MMIN` linear-mip-linear and `MMAG` linear, a mipmapped texture uses level 0 up to `2^(−K)` m and blends
towards level 1 by `2^(1−K)` m (`K` = −4.875: 29 m and 59 m). Most textures have no mipmaps (`MXL` 0) and are
simply bilinear; character textures use filter mode 2 (`MMIN` linear).

**Evidence:** inferred (data), counts as stated. In the textures' palettes PS2 colours put opaque alpha at 128, not
255; librw scales it to 0-255 when it converts a raster ([Coney's implementation](#coneys-implementation)). Sprite
*vertex* colours are a different matter: they are 0-255 with 255 opaque, and the 128 / 255 that `0x001a2690` applies
is the drop shadow's half alpha, not a conversion ([GUI](gui.md#sprite-colours)).

### Particle pages (sprite sheets) {#particle-pages}

Chunk `0x4C` beside a texture dictionary is a **sprite sheet**: rectangles of texture coordinates into the
dictionary's texture, each `{u0, v0, u1, v1}` as floats (inferred from `0x00181e38`, which returns rectangle `i` as
16 bytes, and from the data). The legal screen's page has one rectangle, `(1/2048, 1/2048)` to
`(1 - 1/2048, 0.75 - 1/2048)`: the top 512 × 384 of its 512 × 512 texture, inset half a texel (data, inferred). The
front end and the HUD draw sprites from these pages ([2D drawing](#2d-drawing)). The full layout, now confirmed
(code) and checked on all 1,335 pages, the sprite batches and fonts are on [GUI](gui.md#particle-page); how particle
systems and radar blips name a sprite (a sheet and a rectangle in one word) is on
[Particles](particles.md#sprite-words).

### Screen effects {#screen-effects}

Two screen-effects managers (`ScreenEffectsManager.cpp`), `0x005fdeb8` and `0x005fdebc`, one per view, 0x220 bytes
each, made by `0x0018ba10` and drawn through device slot `+0x128` (`0x0018dac0`). Every number a script can pass is
on [Screen effects](../references/screen-effects.md). Layout, confirmed (code) at the functions cited:

| Offset | What | Written by |
| --- | --- | --- |
| `+0x10 + 0x18 × k` | look *k* (0-12): `+0x10` in seconds, `+0x14` out seconds, `+0x18` motion-blur alpha, `+0x1c` a value, `+0x20` a time in 60 Hz frames, `+0x24` tint RGBA | `CfgScrFx` (`0x0018b4a8`, colour form) |
| `+0x148` / `+0x14c` / `+0x150` | the blur pulse's strength and two factors | `CfgScrFx` numeric form (`0x0018b648`) |
| `+0x154 + 0x10 × layer` | effect layer 0-3 (rain, fog, film grain, room smoke): its object and two "on" words | `0x0018bae0` / `0x0018bd48` |
| `+0x194` / `+0x198` | the base look and the current look | `0x0018b460`, `0x0018b7d0`, `0x0018b950` |
| `+0x1d4`-`+0x1e4` | the fade: running, level, rate per second | `0x0018cc60` |
| `+0x1e8`-`+0x1f4` | the letterbox: state, level 0-1, rate | `0x0018d868` |
| `+0x1f8`-`+0x210` | the blur pulse: state (1 start, 2 rising, 3 held), level, rate, start time, auto-end | `0x0018d058` |
| `+0x212` | the view's player | |

`ScreenQueueEffect(type, seconds)` runs `0x0018d450(manager, type, seconds)` on both managers, a jump table of six
at `0x00552f70`; types 6 and above do nothing. The letterbox (`0x0018d5f8`) is two black quads, top and bottom,
each `level × 0.12` of the screen height. The blur pulse (`0x0018d1d0`) draws device slot `+0x108` with look 5's
two factors and `strength × level`; started by type 4 it holds for look 5's frame time and then queues type 5
itself.

#### Looks {#looks}

A look is a tint and a motion-blur strength the view blends to. `0x0018b7d0(manager, look)` switches to look 0, 2,
5 or 7 (blending the tint with `0x0018c988` and the blur with `0x0018c8c8` over the look's in time);
`0x0018b950(manager, look)` ends one, going on to its follower (0 → 4, 2 → 3) or back to the base look (look 3
over look 8's out time);
`0x0018b460` sets the base look (`SetLevelColour` 9, `EnterStore` 10, `ExitStore` 9). `QueueMotionBlurEffect`
blends the blur alone ([effects bindings](../references/bindings/effects.md)). Confirmed (code).

Coney (`effects::MotionBlur`, `repo:src/effects/motion_blur.h`; `platform::MotionBlurPass`) blends the blur's colour
and strength linearly over the time given and lays the last frame over the new one at that strength, a stand-in for
the drawing [above](#motion-blur); the looks' own blur values are not applied yet.

**Ground fog and litter** (`effects::GroundFog`, `repo:src/effects/ground_fog.h`; `effects::CameraLitter`,
`repo:src/effects/camera_litter.h`): Coney has one view, so one fog emitter.
[`Start3DFog`](../references/bindings/effects.md#start3dfog) starts it over: every 5 frames it tops the view up to 20
wisps (`MaxFogParticles` lowers it), at most 10 at a time, within 20 m of the camera's target and 0.5-2 m above it,
fading in over 9 / fadeSpeed frames to the colour's alpha; a wisp more than 20 m from the camera is dropped and one
within 4 m hidden. Each wisp drifts from birth [toward the camera](particles.md#fog), aimed up to 2 m to either side
along the camera's x axis, at drift × 1.75-2.25 m/s. **Coney's stand-ins**: a fade step is a frame, adding alpha ×
fadeRate / steps; the wisps are not drawn yet (the renderer does not load the `part_fog_00` / `_01` sheets).
`StartGarbage(kind)` arms the [litter](particles.md#garbage): 64 pieces on the 8 × 8 grid round the camera with the
kind's rectangles and sizes, a grey, a wind threshold and a 600-900-update life; at 30 Hz each falls (9.8 m/s²
edge-on, half lying flat), lands on what the collision ray meets and lies down over up to 8 updates; a ground ray every
20 updates (staggered) puts back a piece with no ground below and drops one just placed onto the ground; a piece
more than 28 m from the camera on x or y, or landed with its life over, fades out over 60 updates and comes back in
its grid cell round the camera. `EndGarbage` stops it. **Coney's readings**: the wind vector is not read, so no piece
is lifted, tumbled or pushed at the start; a piece's orientation is one tilt value; a piece that lands is put on the
hit; the pieces are not drawn yet (they are cards, not the renderer's camera-facing sprites).

#### Motion blur {#motion-blur}

Each view, after its tint and fade, `ScreenFx_DrawMotionBlur` (`0x0018c408`) lays the **last frame** over the new
one, confirmed (code):

1. Nothing in game mode 100's state or when `+0x214` is set. The alpha blends from `+0x1a4` to `+0x1a8` over
   `[+0x1ac, +0x1b0]` (`0x0017a258`), never below the floor `+0x1b4`; at 0 nothing is drawn (except in look 5).
2. While the alpha is 111 or more the rain layer's colour gets alpha 0, and gets its colour back below that, so rain
   is not drawn twice.
3. The source is device slot `+0x140`'s raster (`0x00596e18`) over the view's rectangle (slot `+0xa0`). It is **the
   other frame buffer**, the previous frame, read in place: nothing copies it. Confirmed (runtime) from a PCSX2 GS
   dump of the `level99` arena (re-frame): the blur is one full-screen fan drawn after the tint, textured from the
   other frame buffer (`CT32` 1024 × 512, `TBW` 10, `TBP` = that buffer's `FBP` × 32, `TCC` 0, nearest filtering),
   vertex colour `0x80808080` with alpha 7 (the old frame at 7/128, about 5.5 %), blend `0x44`, Z always, no Z
   write. A port with one back buffer must keep the last presented frame itself.
4. **Looks 0, 2, 3, 4 and 7** draw it as a **9 × 9 grid** (slots `+0xf8`, `+0x100`; `0x00193298`, `0x001939b8`):
   each frame every vertex's texture coordinates move by sin and cos of a phase (+0.5 a frame) × 0.001 × 0.9 with a
   random sign × 5 (look 0) or 3 (the others), clamped to the rectangle, so the old frame shimmers. The vertices'
   alpha comes from a 9-byte table at `0x0050ce30` by the distance from the centre (10 in the centre, 50, 180, 225
   up to 252 at the corners; that the index is `|dx| + |dy|` is inferred), flat 200 in look 4, so the edges show
   the most. Looks 3 and 4 also fade the wobble and alpha out over 2,500 and 1,500 ms from their start; then the
   current look becomes 8 and the manager goes back to its base look. Texture filter nearest, vertex alpha,
   `SRCALPHA` / `INVSRCALPHA`.
5. **Other looks** draw one quad (slots `+0xe0`, `+0xe8`) with the blended colour, so the old frame shows at the
   blur's alpha everywhere ([Screen quads](#2d-drawing)).

The **blur pass**, device slot `+0x108` (`0x00193d80`), used by the blur pulse (`0x0018d1d0`) and `0x0018c310`, is a
different effect: (1) the effects camera draws the source at half size into the 512 × 256 raster `+0x43c`
(linear filtering, `ONE` / `ZERO`, Z off); (2) as many times as asked, that half-size image is drawn onto itself
shifted by half the given offset, the sign flipping each pass (`0x0050ce64` = 1), each pass averaging neighbouring
texels; (3) the view's camera draws the result stretched over the whole rectangle, inset by one texel
(`0x0050ce60`), opaque. Confirmed (code).

#### Room smoke {#room-smoke}

Layer 3, `OE_RoomSmoke` (`OverlayEffects/OE_RoomSmoke.cpp`, 0x100 bytes, vtable `0x00539140`), started by
[`StartRoomSmoke`](../references/bindings/effects.md#startroomsmoke), is **one screen-filling sprite** of the sheet
**`room_smoke_overlay`** that scrolls sideways, follows the camera's tilt and slowly changes its size and alpha.
Confirmed (code) at the functions cited unless a line says otherwise.

- **Texture.** Sprite word `0x130000`: sheet-table record 19, rectangle 0
  ([sprite words](particles.md#sprite-words)). **Disc check (corroboration):** record 19's name hash is `0x834dd0f0`,
  the CRC-32 of `room_smoke_overlay`; it is a 256 × 256 texture with one rectangle covering the whole of it
  (texel-inset by 1/1024), linear filtering and **wrap** addressing in U and V (texture filter-and-address word
  `0x1102`). The wrap matters: the texture coordinates below run past 1. (Record 18 is `fog_overlay`, same shape.)
- **Start** (`OE_RoomSmoke_Construct`, `0x0019add0`): the overlay-effect base (`0x0019ba70`); the script's record
  (`0x0019aff8`: tint RGB `+0x9c`, alpha 0 at `+0x9f`, lowest and highest alpha `+0xa0`/`+0xa1`, amount `+0xa4`); a
  random U offset 0-1 (`+0xd8`); one `OE_Particle` sprite widget ([`BaseWidget`](gui.md#widget-classes)) set up with
  size 1.0, depth 10,000, GUI position (0.5, 0, 0.2, 1) (centred across, `y` 0.2), the tint, visible, sprite word
  `0x130000`, an instance of its own, anchored at its centre. Then two drifts are picked: the first is both the
  "from" (`+0xa8`) and the current drift (`+0xb8`), the second the "to" (`+0xc8`); the blend clock (`+0xe0`) starts
  at the real-time clock's now (`0x0050b8b8` slot `+0x30`, milliseconds).
- **A drift** (`OE_RoomSmoke_PickDrift`, `0x0019b198`), each value uniform random: a U scroll per update of
  0.0005-0.0015 × amount with a random sign, a width scale of 1.63-1.93, a height scale of 1.0-1.2, and an alpha (an
  integer from the lowest to the highest alpha); picking one also sets the **blend time** `+0xe4`: 4,000-20,000 ms
  (an integer).
- **One drift into the next** (`OE_RoomSmoke_BlendDrift`, `0x0019b2a8`, first thing each update):
  `t = min((now - start) / blendTime, 1)` on the real-time clock, and the current scroll, width scale, height scale
  and alpha are `from × (1 - t) + to × t` (straight lines, no easing; the alpha rounded to a byte and written into
  the sprite's colour). When `t` reaches 1: from = to, start = now, and a new "to" is picked with its own blend time.
  So the haze never rests: it moves along a chain of random targets, each reached in 4-20 s.
- **The camera** (`OE_RoomSmoke_FollowCamera`, `0x0019b070`), with the player camera's look vector (camera slot
  `+0x220`) of the effect's view:
    - **tilt → height on the screen:** the pitch (`0x0019b7d8`: `acos(look · (0, 0, -1)) - 90°`, in degrees),
      clamped to [-50°, 10°], mapped linearly onto [-10, 10] (`Math_MapRange`, `0x00337568`), clamped, then onto
      [-0.25, 0.26]: the sprite's GUI `y` (`+0xf8`). Pitch -50° puts the sprite's centre at `y` -0.25 (above the top
      of the screen), 0° at 0.175, 10° at 0.26; which sign is looking up is inferred from the vector, not seen;
    - **turn → scroll:** the heading (`0x0019b6a8`, the look vector's angle in the ground plane) in degrees, clamped
      to ±180 and mapped onto [0, 0.9999], is kept per view (`0x0019b968`, effect `+0x20 + 4 × view`); twice the
      change since the last update (old − new) is added to this update's scroll, so turning the camera slides the
      haze sideways (a whole turn, about two texture widths).
- **The sprite** (`OE_RoomSmoke_Update`, `0x0019b4c0`, the effect's slot `+0x18`): size (`0x001a2120`) width
  1.3 × width scale and height = height scale × `*0x0050cf00` (1.0; the update rewrites it to 1.0 in progressive or
  `0x02` mode), in overlay units (the screen is 1.595 wide and 1.1 high, [2D drawing](#2d-drawing)), so about 2.1-2.5
  × 1.0-1.2: wider than the screen and about as high; the position; the tint with the drift's alpha; the U offset
  `+0xd8` += scroll, wrapped into [0, 1]; texture rectangle (U, -0.1)-(U + 1, 0.9) (`0x001a2190`): one whole repeat
  of the texture across the sprite, shifted a tenth up.
- **Drawing:** the widget's sprite in the overlay pass ([2D drawing](#2d-drawing)): Z test and write off, the
  instance's blending, source alpha over inverse source alpha ([GUI](gui.md#resource-instances)), so the haze is an
  alpha-blended layer over the scene, with alpha (0-255) from the drift.
- **Cadence** (`OverlayEffect_Tick`, `0x0019bc10`, called by the manager's update `0x0018bf68` for each layer that
  is on, while the view's player camera exists): nothing while the game clock is paused (`0x0050b734` slot `+0x20`)
  or in game mode 10 or 12; when more than 1000 / 30 ms of game time (`*(0x0050b734) + 0x48`) have passed since the
  last tick (`0x0019bd90`, rate byte `+0x10` = 30), each visible widget's own update runs (it copies the sheet
  rectangle and sets the width from the height and the rectangle's shape) and then the effect's, which overrides
  both; then each visible widget is drawn, every frame, unless `0x00512c44` is set. So the drift advances at most
  30 times a second of game time while the blend runs on real time.
- **Again while running:** `StartRoomSmoke` on a running layer passes the new record to slot `+0x20`
  (`0x0019aff8`), which also picks a new "to" and sets its blend time to 1 ms, so the next update jumps to it.
  `EndRoomSmoke` deletes the effect (`0x0018bd48`) and its widget with it (`0x0019bab8`).

Coney (`effects::RoomSmoke`, `repo:src/effects/room_smoke.h`; `platform::RoomSmokeOverlay`) follows this, drawn after
the motion blur and before the HUD with the texture repeating. **Coney's readings**: its fixed step drives both the
blend and the ticks; the heading is measured from +x toward +y, and the first tick slides nothing.

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
   callback slot is); create the game's PS2 pipelines (`0x00429a98`, [The streamed world](world.md#pipeline-unit));
   create the 512 × 256 raster `+0x43c`.

### A frame {#a-frame}

What [one in-game frame](boot.md#one-frame) does on the screen, confirmed (code) at `0x0015d160` and its callees
(the GS state of each pass, measured: [Rendering](rendering.md#passes)):

1. **Cameras** (`0x001562c8`): device slot `+0x28` with the player-1 camera's view window, near and far clip and fog
   distance and its matrix. That sets the main camera, each viewport's camera (fog distance = far clip × fog start)
   and the overlay cameras: view offset 0, view window `(0x0050b20c × 0x0050b208 × 0.5, 0x0050b20c × 0.5)` =
   **(0.725, 0.5)** in the default interlaced 4:3 mode, near 0.5, far 10,000,000, identity matrix (confirmed
   (runtime), PCSX2 2.9.94: the overlay camera's `RwCamera` at `0x00a5bea0` holds view window 0.725 × 0.5 at `+0x68`,
   near 0.5 at `+0x80`, far 10,000,000 at `+0x84`). Then slot `+0x38` **clears the frame buffer and Z** to
   the background colour.
2. Simulation runs; then slot `+0x18` flushes the render queue before the world update (why there: inferred, so that
   streaming does not free data the GPU still reads).
3. **Each viewport** (`0x00156408`): slot `+0x88` positions viewport `i`'s cameras on their part of the screen
   (columns × rows from `0x0050b1a8`/`0x0050b1aa`; one viewport covers the whole screen); lights and the level's
   sky, clouds and skyline ([Level loading](level-loading.md#render-order)); the world
   (`0x0040e8d8`: several `RwCameraBeginUpdate`/`EndUpdate` passes: the level world with culling off, the streamed
   world's sectors with back-face culling, objects, the detail world, water, translucent objects; Z test and write
   on and fog on except where stated; the full order is on [The streamed world](world.md#a-frame)); the 3D sprite
   batches (`0x00185b38`: Z write and culling off, the batches farthest first, then the glass panes near a camera,
   [World objects: drawing a pane](objects.md#pane-draw));
   the ground rings (`0x0017b2e0`, [HUD](hud.md#the-health-rings)); slot `+0x118` (heat distortion).
4. **Overlays** (`0x00156658`): the HUD (`0x001b1688`) and the overlay pass `0x00185d20` that flushes it ([2D
   drawing](#2d-drawing)); the **screen effects** (slot `+0x128`: the tint, flashes and blur, drawn at once, not
   queued); the subtitles and their flush; the intro movie layer and the credits, each with a flush; the queued 2D
   shapes (`0x0017c308`). The screen effects come before the HUD instead while `W_GameState + 0x268` is above 0
   (meaning not traced) or a blur pulse runs in either view (screen-effects manager `+0x1f8`). So the level tint
   normally covers the HUD but not the subtitles. Confirmed (code); the tint drawn after the radar and the HUD text
   is also confirmed (runtime) in PCSX2 GS dumps.
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

So a frame is shown for at least two vertical blanks. **Evidence** for the rate at runtime: confirmed (runtime), PCSX2
2.9.94 at full speed: its on-screen counters show 30 frames for 60 vertical blanks a second on the front end and in a
fight (a coarse check; the counters at `0x005970f9`/`0x0059708b` were not watched). A frame that takes longer is shown
at the first vertical blank after it is ready (three blanks, 20 frames a second). Where the game clock is on its fixed
step (the front end, mode 8), game time still advances by 1/30 s and the game slows down rather than skipping; in a
level (mode 1) the clock runs on real time clamped to 40 ms ([Boot](boot.md#timers)), so a slow frame advances game time
by its real length. One exception: `0x00159468(0)` sets a 33 ms limit (`0x00596d9c`); if that much time has passed since
the last flip when the flip token is reached, the DMA handler flips at once without waiting for a vertical blank (it
tears rather than drops to 20). `0x00159468(1)` sets 65,535 ms, which turns this off. Mode 8 turns it off when it
resumes (`0x0015c6f8`); mode 1 turns it off when it is suspended or left and chooses per frame in its `Update`
(`0x00158728`, from a screen-effect state). Why the game allows tearing there is not known.

For Coney: the game advances in the same fixed steps of 1/30 s and slows down rather than skipping when frames run
long; drawing is decoupled from the step and may run at any rate, with `--fps-cap 30` as the original's one frame per
step ([Coney's implementation](#coneys-implementation)). The engine's test mode needs no display at all.

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

Widths go through slot `+0x98` (× W / H). Overlay sprites sit at `z` = -1.1 (seen in the batches' position arrays at
runtime), and the overlay camera has a perspective view window of **(0.725, 0.5)** in the default mode ([A
frame](#a-frame)) and an identity matrix, so a point projects to

```text
screen x (fraction of the width)  = 0.5 + X / (2 × 1.1 × 0.725)      # 0.5 + X / 1.595
screen y (fraction of the height) = 0.5 - Y / (2 × 1.1 × 0.5)        # 0.5 - Y / 1.1
```

and GUI `x` = 0 and 1 land at about **5.2 % and 94.8 %** of the width, `y` = 0 and 1 at about 4.5 % and 95.5 % of
the height: a built-in safe-area margin of about 5 % on every side. **Evidence:** confirmed (runtime), PCSX2 2.9.94:
on the main menu the button glyph's sprite in the `part_page0` batch sits at overlay `(-0.6945, -0.3700, -1.1)`
(read from the batch's position array); the formula puts it at 6.46 % across and 83.6 % down, and PCSX2's screenshot
shows the glyph centred at 6.45 % and 83.5 %. With the static 1.3333 (view window 0.6667) it would land at 2.7 %
across, which the screen rules out.

**Screen quads.** Device slots `+0xe0`/`+0xe8` build four RwIm2D vertices for a destination rectangle in pixels, with
texture coordinates from a source rectangle in texels plus half a texel, and draw them as two triangles (indices
`{0, 1, 2, 0, 2, 3}` at `0x0050cdd8`) with: nearest filtering, fog off, Z test and Z write off, vertex alpha on,
source blend `SRCALPHA`, destination blend `INVSRCALPHA`; afterwards Z test and write go back on and the raster is
unset. Slot `+0x130` fills a viewport with a flat colour the same way (a four-vertex triangle strip, Z write off,
culling off). Confirmed (code).

### Drawing a human {#human-draw}

The humans in view are drawn in the world pass after the `s` world ([The streamed world](world.md#a-frame), step 7):
`HumanAtomic_VisibilityCallback` (`0x00174248`) queues each one on the resource manager's list `+0xc78`-`+0xc7c`, and
`WorldManager_Render` draws the list from its end (the last queued first) with `HumanRender_Draw(human, viewport)`
(`0x00174320`), back-face culling (`0x0050c69c`), Z test and Z write on, and the fog distance set to draw distance ×
0.5 instead of × the level's fog start. Confirmed (code). Per human, in order, confirmed (code) unless marked:

1. **Texture.** The model's material is untextured on the disc ([Characters](characters.md#character-geometry)).
   For the draw, the geometry's first material gets the character's texture: the texture at `+0x10` of the instance's
   texture resource (`RpMaterialSetTexture`, `0x0046d010`). After the draw it is set back to none.
2. **Blood.** The same material is a MatFX **dual-texture** material. Its second texture is one of three shared
   blood textures, kept by the resource manager, chosen by the health percentage `h` (`Human_GetHealthPercentRaw`,
   `0x00222ef0`: health / maximum × 100), with `i = min(trunc(h / 30), 2)`:

    | `h` | `i` | Slot | Texture (confirmed (runtime), PCSX2 2.9.94, `level99`) |
    | --- | --- | --- | --- |
    | 60 and above | 2 | resource manager `+0x74` | `charblood_d1` |
    | 30 to 60 | 1 | `+0x78` | `charblood_d2` |
    | below 30 | 0 | `+0x7c` | `charblood_d3` |

    (`RpMatFXMaterialSetDualTexture`, `0x00465810`; the slot is `+0x74 + ((2 - i) mod 3) × 4`, the texture its
    object's `+0x10`.) The dual pass's GS `ALPHA` register, the 64-bit word at `+0x28` of the material's first MatFX
    effect, gets the low byte **`0x44`** while `h` < 90 and the half-word `W_GameState + 0x454` is 0, and **`0x45`**
    otherwise. `0x44` is `(Cs − Cd) × As + Cd`, an ordinary alpha blend of the blood texture over the first pass;
    `0x45` is `(Cd − Cd) × As + Cd`, which leaves the frame unchanged. So **blood shows below 90 % health**, and
    heavier textures replace it at 60 % and 30 % (that `d1`-`d3` grow heavier is inferred from the names).
    `W_GameState + 0x454` is written only as 0 (by the game state's constructor and by the options' Restore
    Defaults, `0x001d81c4`), and the blood particle effects read it too (`0x003a9168`, `0x003d5058`), so it is a
    "no blood" switch the retail game never sets (inferred). The dual pass draws the blood texture with the model's
    second texture-coordinate set; at runtime it is the GS context-2 pass that follows each character draw (PCSX2 GS
    dumps, confirmed (runtime)).
3. **Colour.** The material colour is the instance's colour (`+0x28`, R in the low byte, set by `HuColor`) with R, G
   and B × `f`. `f` follows the brain's "hidden in shadow" byte (`+0x2d4`, [Lighting](lighting.md#humans)): with
   `t` = min(time since the brain's stamp `+0x10`, 250 ms) × 0.002, `f = 0.5 + t` when not hidden (0.5 up to 1.0)
   and `f = 1 − t` when hidden (1.0 down to 0.5).
4. **Alpha.** The colour's alpha × `(drawDistance − d) / fadeLength` once the human's camera distance `d` (`+0x334`)
   is past the fade start, and × `(now − start) / 1000` during the first second after the instance's stamp `+0x30`.
   Values read at runtime (confirmed (runtime), `level99`): draw distance `0x0050cc60` = 70 m (clamped to
   [`0x0050cc5c` = 30, `0x0050cc58` = 70] by `HumanRender_SetDrawDistance`, `0x00174200`), fade length `0x0050cc68` =
   10 m, fade start `0x0050cc64` = draw distance − fade length = 60 m. So a human fades out from 60 to 70 m.
5. **Skeleton and lights**: `CharacterInstance_UpdateSkeleton(inst, 1)` (`0x00177240`), then the lights
   ([Lighting](lighting.md#humans)); then the atomic's saved render callback (`0x0050cc54`, RenderWare's default
   `0x00469ef8`), which runs the character pipelines ([The streamed world](world.md#pipeline-unit)).
6. **Shadow**: the blob ([Lighting](lighting.md#humans)).

**The bone matrices** (`CharacterInstance_UpdateSkeleton(inst, full)`, `0x00177240`), confirmed (code). This is the
matrix palette the skin is drawn with; `HumanRender_Draw` passes `full` = 1.

1. The source is the human's **bone cache** (`Human_GetBoneCache`, `0x0023bca0`: `0x006b6880` + human index ×
   `0x470`, [Combat: the bone cache](combat.md#grab-posing)): 34 entries of 32 bytes, entry `b` holding a position
   (vec4) at `+b × 0x20` and a rotation quaternion `(x, y, z, w)` at `+b × 0x20 + 0x10`, in the model's z-up frame
   and already composed through the parent table (model space, not parent-relative).
2. For each pose bone `b` from 2 to 33 (with `full` = 0, bone 2 only): `L_b` = the rotation as a matrix
   (`Quat_ToMatrix`) with the position × the human's scale (`Human_GetScale`) as its translation.
3. `R` = the human's world transform: the rotation and position of the object transform table `0x00714b00` at the
   human's index (`+0x92`), 32 bytes each ([Characters](characters.md#ground)).
4. Each skin matrix `i = b − 2` (the matrix array of the first atomic's hierarchy, 64 bytes each, flags word `+0x0c` =
   3) = `Mat_MulSwapYZ(R, L_b)` (`0x00336940`): the row-vector product `L_b × R` (bone first, then the human's
   placement), re-expressed in RenderWare's y-up axes: a game vector `(x, y, z)` becomes `(x, z, −y)`, so the
   product's rows become `(r0)`, `(r2)`, `(−r1)` with each row's components reordered the same way.
5. When the human's scale is not 1, each matrix is also scaled by `(s, s, s)` before it (`0x0047ce40`, pre-concat),
   so with the scaled translations the whole skinned body is scaled by `s` about the human's origin.
6. Then the hierarchy's frame is marked dirty for RenderWare (`0x00483bf8`).

So skin matrix `i` belongs to HAnim node `i` (pose bone `i + 2`, [Characters](characters.md#character-geometry)), and
a vertex is placed, inferred from RenderWare's skinning and the disc's inverse bind matrices, as
`Σ weight_k × (v × inverseBind_k × skin_k)` over its up to four weights. Whether the VU1 microcode renormalises the
weights was not read.

### Drawing a car {#car-draw}

Cars are drawn twice in the world pass ([The streamed world](world.md#a-frame)): `CarInstance_Render(car, opaque)`
(`0x00172c70`) over the resource manager's car list `+0xc98`-`+0xc9c`, confirmed (code):

| Pass | When | `opaque` | Render states | Order |
| --- | --- | --- | --- | --- |
| Opaque | after the humans, before the other objects | 1 | culling off, Z test and Z write on, fog on | list order |
| Glass | after the water and the type-`0x20` objects | 0 | culling off, **Z write off**, Z test and fog on | from the end of the list |

Each pass stores `opaque` at the instance's `+0x41`, selects the objects' lights for the clump's bounding sphere
(flags 1, no glow) and runs `CarPart_RenderCallback` (`0x00172940`) on every atomic. The glass pass then clears the
26 "seen" bits `+0x11fc` that the visibility callback (`0x00172d60`) sets. Per atomic `a` (part `p = a`, or
`a − 25` for the damaged form), confirmed (code):

1. Skip it unless bit `p` of `+0x11fc` is set. Skip it in the opaque pass when `p` is in the **glass mask**
   `0x2a80c0` (parts 6, 7, 15, 17, 19, 21) and in the glass pass when it is not.
2. Skip it when the part is removed (`+0x11f0`), when it is the undamaged form and the part's damage is 0.5 or more
   (wheels 22-25 excepted), or when it is the damaged form and the damage is below 0.5 ([Cars](cars.md#drawn)).
3. Upload the selected lights; give the first material the texture at `+0x10` of the car's first texture resource.
4. **Colour**: the car's tint (`+0x24`) for the parts in the **paint mask** `0x157c31` (parts 0, 4, 5, 10-14, 16,
   18, 20), else the white at `0x005fd268`. Alpha × `(now − start) / 1000` during the first second after `+0x2c`.
5. **MatFX** (`0x004653d8` reads the material's effect): an **environment map** (2) gets the texture at `+0x10` of
   the car's second texture resource and the frame at `*(0x0070ad18) + 4` (`0x00465538`, `0x004655e0`); a
   **dual** material (4) gets that second texture as its dual texture (`0x00465810`). That `0x0070ad18` is
   RenderWare's current camera, so that the reflection follows the view, is inferred.
6. Draw it with the saved render callback (`0x005fd030`), then set the material's texture back to none.

### Distance: fades and level of detail {#lod}

What changes with distance, from the draw paths read (confirmed (code) for each item; that there is nothing else is
inferred from those paths):

| What | Rule | Where |
| --- | --- | --- |
| World, everything | the far clip is the draw distance: 115 m for the player camera, shrinking when the frame rate drops or scenery is missing | [The streamed world](world.md#a-frame) |
| Fog | linear from draw distance × fog start (57.5 m) to the draw distance (115 m) | [The streamed world](world.md#fog) |
| Humans | fade out from 60 to 70 m (draw distance 70 m, fade 10 m); beyond, not queued | [Drawing a human](#human-draw) |
| World objects | by screen size: radius / squared distance under 0.0004 not drawn, faded between 0.0004 and 0.0005 (some types exempt) | [World objects](objects.md) |
| Cars | none: drawn while their parts are in view | [Drawing a car](#car-draw) |
| Textures | the mip level by distance, `log2(distance) + K` | [Texture dictionaries](#texture-formats) |
| Skyline | its own near and far clip (39 to 560 m) | [Level loading](level-loading.md#render-order) |

**No geometry level of detail**: a world sector, a human, a car part and an object each have one model, drawn whole
or not at all. The `s` and `d` worlds are streamed by distance ([The streamed world](world.md#streaming)), which
changes what is loaded, not how detailed it is.

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
   two display buffers it clears the overlay camera to opaque black, draws the page's first sprite (placement
   below), and shows the raster. Then it releases the resource.
4. `Update` (`0x00159ae0`) draws nothing and presents nothing: it ticks the timer, reads the pads and leaves once
   5,000 ms have passed. The display keeps showing the last flipped buffer.

The scale factors (horizontal, vertical) by mode flags, confirmed (code): interlaced 4:3 (1.55, 1.35), interlaced
16:9 (1.9, 1.45), progressive 4:3 (1.35, 1.19), progressive 16:9 (1.6, 1.25), neither flag (1.0, 1.0) or with 16:9
(1.45, 1.19).

**Placement.** The sprite record (`0x0015a0b8`-`0x0015a154`, confirmed (code)) has position `(0, 0, -d)`, where `d`
is the overlay camera wrapper's slot `+0xa8` getter (its near clip, 0.5, inferred from the getters' order; the
value cancels out below), size `(fx × d × (u1 - u0), fy × d × (v1 - v0))` with `(fx, fy)` the factors above and
`(u0, v0, u1, v1)` the page's first rectangle, and colour `(255, 255, 255, 255)` (four `0xff` bytes). A batch sprite
is centred on its position and its size is the full width and height, so the picture covers, as a fraction of the
screen,

```text
width  = fx × (u1 - u0) / (2 × viewWindowX)     # 1.55 × 0.99902 / 1.45 = 1.068 in interlaced 4:3
height = fy × (v1 - v0) / (2 × viewWindowY)     # 1.35 × 0.74902 / 1.0  = 1.011
```

centred: about **683 × 453 logical pixels on the 640 × 448 screen**, so the picture slightly overfills it. About
3.2 % of the image width (16 texels of 512) is cut off on the left and on the right and 0.5 % (2 texels of 384) at
the top and the bottom, and the 4:3 image is shown about 5.6 % wider than its own shape. The factors are matched to
the overlay camera's per-mode aspect (`0x0050b208`): 1.55 / 1.45 here.

**Evidence:** confirmed (runtime), PCSX2 2.9.94. The clear colour was patched to blue (`0x00159e40`, `mov.s f14, f15`)
and the two factors halved (`0x0015a03c`, `0x0015a048`) before mode 5 ran, so the picture's edges show: PCSX2's
screenshot has the picture centred, covering 0.534 of the width and 0.5075 of the height, against 0.5340 and 0.5056
from the formula with half factors (within the screenshot's one-pixel resolution). Unpatched, the picture fills the
screen to its edges with the image's margins cut as above (the text block scales by 2.0 about the centre between
the two captures). The device flags read 1 (interlaced 4:3), the overlay camera's view window 0.725 × 0.5.

**Disc check (corroboration):** all eleven names exist in `WARRIORS.DIR` under their decimal CRC. `legal_screen`
(CRC-32 863681355) is entry 3,480: 263,680 bytes, one resource of two chunks, a `0x2A` texture dictionary holding one
512 × 512 8-bit palettised texture and a `0x4C` particle page with one rectangle (the top 512 × 384). So the faithful
first screen is that image, scaled to slightly overfill a black 640 × 448 screen ([Placement](#first-screen)), held for
five seconds; then mode 6 (memory card checks) and mode 8 take over.

This naming scheme names the whole first block of the WAD: see [WAD contents](formats/wad-contents.md#names).

## Code index {#code-index}

Every function of the game's own `Graphics/` code (`0x0016e388`-`0x0019dfb0`, [Source map](source-map.md#graphics))
that the sections above do not already walk through, file by file in address order, with what it does. Lighting has
its own index ([Lighting](lighting.md#code-index)). Names are ours, as in the local Ghidra project, where each
function also carries a plate comment pointing here. A row that only links is described on the linked page.

### Resource manager: animations and cars {#code-resources}

The resource manager (`0x0050cd4c`, [Level loading](level-loading.md), [Chunk system](chunk-system.md)) keeps one
balanced-tree map per resource type, keyed by name hash: car models (type 2) at `+0x20`, texture dictionaries
(type 4) at `+0x30`, character data (type 5) at `+0x40`, named animation packs (type 7) at `+0x50`. A node's
value (`+0x14`) starts with the real-time stamp of its last use, then a reference count (`+0x04`). Each type has
the same methods: **get** (blocking: request it if absent, run the file manager until it is loaded, add a
reference), **request** (non-blocking; refreshes the stamp when present), **free** (only at zero references:
destroy, unlink, count − 1; called by the evictions `0x00187d28` and `0x00188aa8`), **is resident** (present, and
referenced when asked) and **fits** (would a resource of that size fit, evicting with `0x00187d28` when allowed).
Names are ours. Confirmed (code) at each address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0016e388` | `CharDataRes_Construct` | holder of one character's animation data (vtable `0x00538988`): stamp = now + 3,000 ms, the data, its heap, the stream | confirmed (code) |
| `0x0016e420` | `CharDataRes_Destruct` | frees each of the 722 animation slots that differs from the resource manager's default set (`+0x70`), the slot table, the heap | confirmed (code) |
| `0x0016e6a8` | `AnimPackRes_Construct` | holder of one named animation pack (vtable `0x00538970`), same layout | confirmed (code) |
| `0x0016e740` | `AnimPackRes_Destruct` | frees the pack, its heap and stream | confirmed (code) |
| `0x0016e8f0` | `ResourceMgr_GetCharData` | get, type 5 (file `"%u"` of the id, [Characters](characters.md#files)) | confirmed (code) |
| `0x0016ea20` | `ResourceMgr_RequestCharData` | request, type 5 | confirmed (code) |
| `0x0016eb10` | `ResourceMgr_FreeCharData` | free, type 5 | confirmed (code) |
| `0x0016f128` | `ResourceMgr_GetAnimPack` | get, type 7, keyed by the CRC-32 of the pack's name (`0x0010bc38` loads named packs through it) | confirmed (code) |
| `0x0016f260` | `ResourceMgr_RequestAnimPack` | request, type 7 (the file is named `"%s"` of the name) | confirmed (code) |
| `0x0016f368` | `ResourceMgr_FreeAnimPack` | free, type 7 | confirmed (code) |
| `0x0016f980` | `ResourceMgr_HasAnimPack(rm, name, needRef)` | is resident, type 7 | confirmed (code) |
| `0x0016fa00` | `ResourceMgr_AnimPackFits(rm, name, size, evict)` | fits, type 7: a trial allocation of the size on the resource heap | confirmed (code) |
| `0x00173068` | `ResourceMgr_HasCar(rm, desc, needRef)` | is resident for a car: its model and its one or two texture dictionaries | confirmed (code) |
| `0x00173170` | `ResourceMgr_CarFits` | fits, for a car's model | confirmed (code) |
| `0x001736a0` | `ResourceMgr_RequestCar` | request a car's model and texture dictionaries | confirmed (code) |
| `0x00173718` | `ResourceMgr_CreateCarInstance` | gets the model and textures and makes a `CarInstance` (0x44 bytes, `0x001721c8`) | confirmed (code) |
| `0x00173878` | `ResourceMgr_ReleaseCarInstance` | one reference less, stamp, delete the instance | confirmed (code) |
| `0x001739b0` | `ResourceMgr_RequestCarModel` | request, type 2 | confirmed (code) |
| `0x00173aa0` | `ResourceMgr_GetCarModel` | get, type 2 | confirmed (code) |
| `0x00173bd0` | `ResourceMgr_FreeCarModel` | free, type 2 (`CarModel.cpp`) | confirmed (code) |

### Litter and ground fog {#code-camera-effects}

`CameraGarbage.cpp` is the [blowing litter](particles.md#garbage) (object `0x005971a0`, 0x20 bytes, 64 pieces of
0x70 bytes); `CameraGroundFog.cpp` the ground fog (object `0x005971a4`, 0x0c bytes, 36 patches of 0x60 bytes on a
6 × 6 grid 6 m apart round the player camera, each put on the ground by a ray and kept 0.2 m above it, slowly
turning). The ground fog is switched only by `EnableGroundFog`, which no script calls
([effects bindings](../references/bindings/effects.md)), so it is never seen in the shipped game. Confirmed (code).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0016fcc8` | `Garbage_Create` | allocates the litter object at start (`Game_InitializeSubsystems`) | confirmed (code) |
| `0x0016fd90` | `Garbage_SpawnPiece(piece, rect, rectRange, sizeMin, sizeMax)` | places a piece in its grid cell round the camera and gives it its random looks, wind threshold and life ([litter](particles.md#garbage)) | confirmed (code) |
| `0x0016ffd8` | `Garbage_LayFlat(piece, normal)` | the turn that lays a piece on a surface, slerped over up to 8 updates; marks it grounded (flag 8) | confirmed (code) |
| `0x00170300` | `Garbage_Construct` | zeroes the object | confirmed (code) |
| `0x00170330` | `Garbage_Start(kind)` | `StartGarbage` | confirmed (code) |
| `0x00170528` | `Garbage_End` | `EndGarbage`: clears the active flag | confirmed (code) |
| `0x00170538` | `Garbage_SetPaused(on)` | sets `+0x04` while active; the update skips while it is set (called from `0x002195e0`) | confirmed (code) |
| `0x00170550` | `Garbage_Release` | frees the pieces and ends (level unload, `0x001607b8`) | confirmed (code) |
| `0x00170600` | `Garbage_ApplyWind(piece)` | the wind step | confirmed (code) |
| `0x00170b28` | `Garbage_Throw(pos, dir, count)` | extra pieces from breakables | confirmed (code) |
| `0x00170c88` | `Garbage_Update` | steps every piece at 30 Hz, from `Humans_Update` | confirmed (code) |
| `0x001712c0` | `Garbage_Render` | draws the pieces as textured cards from the world render (`0x0040f548`), not during scenes | confirmed (code) |
| `0x00171540` | `GroundFog_Create` | allocates the ground-fog object at start | confirmed (code) |
| `0x00171608` | `GroundFog_PlacePatch(patch)` | puts a patch in its grid cell round the player camera, on the ground below (no ground flags it `0x10` for a retry) | confirmed (code) |
| `0x00171908` | `GroundFog_Construct` | zeroes the object | confirmed (code) |
| `0x00171920` | `GroundFog_Start` | allocates the 36 patches (`TGroundFogData`, 0xd80 bytes) and places them | confirmed (code) |
| `0x00171a50` | `GroundFog_Stop` | clears the on flags | confirmed (code) |
| `0x00171a68` | `GroundFog_Release` | frees the patches and stops | confirmed (code) |
| `0x00171b18` | `GroundFog_NewDrift(patch)` | a random spin (±0.1, from the wind's strength over the patch's threshold) and a 30-60-update slerp | confirmed (code) |
| `0x00171d38` | `GroundFog_Update` | per update, from `Humans_Update`: re-places flagged patches, ages and re-drifts them | confirmed (code) |
| `0x00171f58` | `GroundFog_Render` | draws the patches from the world render (`0x0040f554`) | confirmed (code) |

### Cars' render objects {#code-car-render}

What [Cars](cars.md#drawn) says is drawn, as code. Confirmed (code).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001721c8` | `CarInstance_Construct(model, tex, tex2)` | render instance of a car (vtable `0x00538a10`, base `0x0017f650`), white tint `+0x24`, no damage record yet | confirmed (code) |
| `0x00172258` | `CarInstance_Destruct` | restores the vtable, base destructor | confirmed (code) |
| `0x00172280` | `Matrix_RwToGame` | copies a matrix into the game's axes: each row (x, y, z) becomes (x, −z, y) | confirmed (code) |
| `0x00172388`, `0x00172440` | `CarInstance_SetMatrix`, `CarInstance_SetPartMatrix` | write a matrix into the clump's frame (or a part's) and mark it dirty | confirmed (code) |
| `0x001724f8`, `0x001726f0` | `CarAtomic_CopyLtmCb`, `CarAtomic_CopyMatrixCb` | atomic callbacks: copy the world matrix / the frame matrix of the atomic whose part number matches | confirmed (code) |
| `0x001725d8`, `0x001727c8` | `CarInstance_GetPartLtm`, `CarInstance_GetPartMatrix(inst, out, part, toGame)` | a part's world / local matrix, converted by `0x00172280` when asked | confirmed (code) |
| `0x001728e0` | `CarInstance_PartColour(inst, part)` | the car's tint for the painted parts (mask `0x157c31`), white for the others | confirmed (code) |
| `0x00172940` | `CarPart_RenderCallback` | draws one atomic by its part's damage and the pass ([Drawing a car](#car-draw)) | confirmed (code) |
| `0x00172c70` | `CarInstance_Render(inst, opaque)` | [Drawing a car](#car-draw) | confirmed (code) |
| `0x00172d60` | `CarPart_VisibilityCallback` | per atomic in view: queues the car on the resource manager's car list (`+0xc94`) when one of its parts draws | confirmed (code) |
| `0x00172f00` | `CarInstance_MarkPartDrawn(part)` | sets bit `part` of `+0x11fc` | confirmed (code) |
| `0x00172f28` | `CarModel_SetupAtomic` | per atomic of a new car model: render callback `0x00172d60`, the game pipelines, the bounding sphere's y and z swapped | confirmed (code) |
| `0x00172ff0` | `CarInstance_Init` | base set-up, then every atomic's plugin owner (`+0x0c`) set to the instance and `+0x08` to 0 (`0x001925d0`) | confirmed (code) |
| `0x00173928` | `CarModel_Construct` | car model (vtable `0x00538a38`, base `0x0017f380`), each atomic set up | confirmed (code) |
| `0x00173988` | `CarModel_Destruct` | restores the vtable, base destructor | confirmed (code) |

### Characters: instance, task stack and model {#code-characters}

`Character.cpp` and `CharacterModel.cpp` are the render side of a human: the `CharacterInstance` (0x330 bytes,
vtable `0x00538a78`; [Characters](characters.md), its [task stack](formats/animation.md#task-stack)) and the character
model and its resource-manager methods (type 3 at map `+0x00`). Confirmed (code) at each address. Instance fields
used here:

| Offset | Meaning |
| --- | --- |
| `+0x04` | the render instance base (`0x0017f650`) |
| `+0x20` | the clump; `+0x50` / `+0x60` two quaternions, `+0x70` the default-pose pointer (`0x00598420`), `+0x80` the pose |
| `+0x2a0` / `+0x2a4` | head-look yaw and pitch (radians); `+0x2a8` the head-look weight 0-1 |
| `+0x2ac` / `+0x2ae` | task-stack count / queue count; stack at `+0x2bc` (18), queue at `+0x304` (10) |
| `+0x2b8` | the human |

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00174200` | `HumanRender_SetDrawDistance(d)` | the humans' draw distance `0x0050cc60` = `d` clamped to [`0x0050cc5c`, `0x0050cc58`] (and `0x0050cc64` = that − `0x0050cc68`), from the world render | confirmed (code) |
| `0x00174238` | `CharacterInstance_ReturnFalse` | returns 0 (a vtable slot) | confirmed (code) |
| `0x00174248` | `HumanAtomic_VisibilityCallback` | per atomic in view and not occluded (`0x0017a680`): queues the human on the resource manager's list `+0xc74` when its squared camera distance (`+0x334`) is under the draw distance | confirmed (code) |
| `0x00174320` | `HumanRender_Draw` | [Drawing a human](#human-draw), [Lighting](lighting.md#humans) | confirmed (code) |
| `0x00174af8` | `CharacterInstance_SetModel(inst, model, tex, data)` | base set-up, each atomic's plugin owner set (`0x001925d0`), copies the model's 34 bind offsets, then runs the queued tasks | confirmed (code) |
| `0x00174c18` | `CharacterModel_SetupAtomic` | per atomic of a new character model: render callback `0x00174248` (first one kept in `0x0050cc54`), the game pipelines, the bounding sphere's y and z swapped | confirmed (code) |
| `0x00174d00` | `CharacterInstance_Construct(model, data, tex)` | full instance: default pose `0x00598420`, reference pose (`0x00100a80`), empty stack and queue | confirmed (code) |
| `0x00174e70` | `CharacterInstance_ConstructPoseOnly(data)` | the same without a model (`0x00177b80` when no model is wanted) | confirmed (code) |
| `0x00174f40` | `CharacterInstance_Destruct` | frees every stacked and queued task | confirmed (code) |
| `0x00175080` | `CharacterInstance_GetAnim(id)` | [Characters](characters.md#files) | confirmed (code) |
| `0x00175120` | `CharacterInstance_ReleaseClip(clip)` | one user of a clip less (`+0x43`), telling the human first when the clip has flag 4 | confirmed (code) |
| `0x00175178`, `0x00175180` | `TaskStack_Count`, `TaskStack_SetCount` | `+0x2ac` | confirmed (code) |
| `0x00175188`, `0x001751c0`, `0x00175208` | `TaskStack_TopRaw`, `TaskStack_FromTop(n)`, `TaskStack_Bottom` | the top task, the n-th below it, task 0 | confirmed (code) |
| `0x00175210` | `TaskStack_Top` | [task stack](formats/animation.md#task-stack) | confirmed (code) |
| `0x00175258` | `TaskStack_TopAnimId` | the top task's anim id (slot `+0xf8`), or the bottom one's when the top has none | confirmed (code) |
| `0x001752c8` | `CharacterInstance_RefreshAnimId` | [task stack](formats/animation.md#task-stack) | confirmed (code) |
| `0x00175380` | `TaskStack_Append` | puts a task on top without the fade check | confirmed (code) |
| `0x001753d8`, `0x001754e8` | `TaskStack_Push`, `TaskStack_InsertBottom` | [task stack](formats/animation.md#task-stack) | confirmed (code) |
| `0x00175610` | `CharacterInstance_RunQueuedTasks` | [Animation](formats/animation.md#task-stack) | confirmed (code) |
| `0x00175840`, `0x00175848` | `TaskQueue_Count`, `TaskQueue_SetCount` | `+0x2ae` | confirmed (code) |
| `0x00175850`, `0x00175888`, `0x001758d0` | `TaskQueue_Last`, `TaskQueue_FromLast(n)`, `TaskQueue_LastNotFade` | the queue's newest task, the n-th before it, the newest unless it is a fade (type 9; then the first) | confirmed (code) |
| `0x00175928` | `TaskQueue_Append` | adds a task at the end | confirmed (code) |
| `0x00175980`, `0x00175a68` | `TaskQueue_AppendTrim`, `TaskQueue_InsertFirstTrim` | add at the end / the front; with more than 2 queued, first finish the queued fade with the most time left (`0x00177fa8`) | confirmed (code) |
| `0x00175b78`, `0x00175be8` | `TaskStack_TopAllowsHeadLook`, `TaskStack_TopAllowsCamera` | the top task's clip exists and lacks descriptor flag `0x4000` / `0x8000` | confirmed (code) |
| `0x00175c58` | `CharacterInstance_SetHeadLook(yaw, pitch)` | stores the head-look angles and builds the head and neck turns from them (pitch clamped to −30°..22.5°) | confirmed (code) |
| `0x00176290` | `CharacterInstance_ApplyHeadLook(inst, pose)` | while the top clip and the human allow it, raises the weight `+0x2a8` (faster for small angles) and turns the pose's head and neck by weight × angles; otherwise lowers it | confirmed (code) |
| `0x001768e8` | `CharacterInstance_ApplyTorsoTwist(inst, pose)` | turns the spine bones by the human's twist angle (human `+0x29c`), split over the bones when above 3° | confirmed (code) |
| `0x00176d60` | `CharacterInstance_Sample` | [Animation](formats/animation.md#the-pose) | confirmed (code) |
| `0x00177038` | `TaskStack_ForEach(slot +0xc8)` | calls each stacked task's slot `+0xc8` with an argument | confirmed (code) |
| `0x001770b8` | `CharacterInstance_SampleThunk` | calls `0x00176d60` | confirmed (code) |
| `0x001770d8` | `TaskStack_TopClipProgress` | for a clip task (type 3) with flags `0x48000`: current time / length, else 0 | confirmed (code) |
| `0x001771a0` | `TaskStack_TopCallEvent(arg)` | when the top task has flag `0x80` and slot `+0x110` agrees, calls its slot `+0xb8` | confirmed (code) |
| `0x00177210` | `Clump_GetFirstAtomicCb` | atomic callback: keeps the atomic's geometry | confirmed (code) |
| `0x00177240` | `CharacterInstance_UpdateSkeleton(inst, full)` | [the bone matrices](#human-draw) | confirmed (code) |
| `0x001774d0` | `ResourceMgr_HasCharacter(rm, desc, dataOnly, needRef)` | is resident for a character: data, and unless `dataOnly` texture dictionary and model ([Characters](characters.md#files)) | confirmed (code) |
| `0x001775d8` | `ResourceMgr_CharacterFits` | fits, for a character's model, data and textures in turn | confirmed (code) |
| `0x00177b00` | `ResourceMgr_RequestCharacter(rm, desc, dataOnly)` | request its model, data and textures | confirmed (code) |
| `0x00177b80` | `ResourceMgr_CreateCharacterInstance(rm, desc, dataOnly)` | gets them and makes a `CharacterInstance` (`0x00174d00`, or `0x00174e70` without a model) | confirmed (code) |
| `0x00177d50` | `ResourceMgr_SetInstanceModel(rm, inst, desc)` | gets a model and textures and gives them to an instance (`0x00174af8`) with the default data | confirmed (code) |
| `0x00177e00` | `ResourceMgr_ReleaseCharacterInstance` | one reference less, stamp, delete | confirmed (code) |
| `0x00177eb8` | `TaskStack_NewestFade` | the newest fade task on the stack (for the push's early finish) | confirmed (code) |
| `0x00177fa8` | `TaskQueue_LongestFade` | the queued fade (type 16) with the most time left | confirmed (code) |
| `0x00178098` | `CharacterList_OnLoaded` | chunk `0x44` ([Chunk system](chunk-system.md)) | confirmed (code) |
| `0x001780d0`, `0x001780d8` | `CharacterList_Count`, `CharacterList_Record(i)` | the list's count and 32-byte record *i* | confirmed (code) |
| `0x001780e8` | `CharacterList_Find(name)` | record index for the CRC-32 of a model name, or −1 (`Cfg_SetCharacterClass`) | confirmed (code) |
| `0x00178178` | `CharacterModel_Construct` | character model (vtable `0x00538b28`, base `0x0017f380`), atomics set up by `0x001983a8` and `0x00174c18` | confirmed (code) |
| `0x00178208` | `CharacterModel_Destruct` | frees the model's bind offsets, base destructor | confirmed (code) |
| `0x001782e8`, `0x001783d0` | `ResourceMgr_RequestCharModel`, `ResourceMgr_GetCharModel` | request / get, type 3 | confirmed (code) |
| `0x001784f0` | `ResourceMgr_FreeCharModel` | free, type 3 | confirmed (code) |
| `0x00178b30` | `DependencyList_OnLoaded` | chunk `0x4F`, then marks the `always` group needed | confirmed (code) |
| `0x00178bc8` | `DependencyList_SetGroup(hash, state)` | sets the state word of every 12-byte record `{group, resource, state, loaded}` whose group or resource is the hash | confirmed (code) |
| `0x00178c48` | `DependencyList_IsNeeded(hash)` | binary search; true when a record of that resource is wanted | confirmed (code) |
| `0x00178d48` | `DependencyList_NextToLoad(out)` | the next wanted, unloaded resource as a load request (`"%u"` name and size from the object list) | confirmed (code) |
| `0x00178ee8` | `AnimList_OnLoaded` | chunk `0x4E`: `{hash, size}` records of the animation packs | confirmed (code) |
| `0x00178f38`, `0x00178f40` | `AnimList_Count`, `AnimList_Size(name)` | the count; a pack's size by name (else the WAD file's size) | confirmed (code) |

### Embers, fonts, the device base, occluders and colours {#code-misc}

`FallingEmbers.cpp` and the unnamed file before the stub `0x0017b1c0` (fonts, the device's base class, the level's
occluders, colour helpers; inferred from position). Confirmed (code) at each address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00178fd8` | `Embers_Create` | makes the embers object (`0x005971ac`, 0x50 bytes) on first use | confirmed (code) |
| `0x001790a8` | `Embers_Destroy` | deletes it (`TermFallingEmbers`, level unload) | confirmed (code) |
| `0x00179138` | `Embers_Construct` | a free list of 150 embers of 0x30 bytes | confirmed (code) |
| `0x00179250` | `Embers_Destruct` | frees the list | confirmed (code) |
| `0x00179310` | `Embers_Setup(emb, pos, minRate, maxRate)` | the centre (the object's position, taken once), the spread vectors (5 m), the per-update counts (rates / 30) ([`InitFallingEmbers`](../references/bindings/effects.md#initfallingembers)) | confirmed (code) |
| `0x00179378` | `Embers_Update` | spawns a random count per update round the centre with random speed and size, sprite `0x10008` + 0-3 (batch 1 `part_page1`, rectangles 8-11), life 60-90 updates; moves and retires the live ones | confirmed (code) |
| `0x001795f8` | `Embers_Render` | draws the live embers from the world render (`0x0040f56c`) | confirmed (code) |
| `0x00179758` | `Embers_Init` | `InitFallingEmbers` | confirmed (code) |
| `0x001797e8` | `Embers_Term` | `TermFallingEmbers` | confirmed (code) |
| `0x00179808` | `Font_Size(scale)` | [GUI](gui.md#text) | confirmed (code) |
| `0x001798f8` | `Font_GetDefault(out)` | the default font's size record with spacing 3.0 (`GetDefaultFont`) | confirmed (code) |
| `0x00179958`, `0x00179c30` | `Font_Measure`, `Font_Draw` | [GUI](gui.md#text) | confirmed (code) |
| `0x0017a1b0` | `Font_NoOp` | empty; called after a text widget's draw (`0x001cd288`) | confirmed (code) |
| `0x0017a1c8` | `GraphicsDevice_Construct` | the device base class (vtable `0x00538b40`) | confirmed (code) |
| `0x0017a1e0` | `GraphicsDevice_Open` | [Start-up](#start-up) | confirmed (code) |
| `0x0017a258` | `Colour_LerpPacked(t, a, b)` | blends two packed RGBA colours by `t` (VU0) | confirmed (code) |
| `0x0017a3c0` | `Colour_LerpFloat(t, out, a, b)` | the same for float colours | confirmed (code) |
| `0x0017a4f8` | `Device_GetScreenSize` | screen width and height (device slots `+0xa8`, `+0xb0`; `GetScreenDimensions`) | confirmed (code) |
| `0x0017a560` | `Occluders_Set` | [Level loading](level-loading.md) | confirmed (code) |
| `0x0017a610` | `Occluders_SetUpViewport` | [Level loading](level-loading.md) | confirmed (code) |
| `0x0017a680` | `Occluders_HidesSphere(occ, sphere)` | true when a bounding sphere (radius at least `0x0050ccc4`) lies wholly behind the planes of an active occluder (records of 0x70 bytes, active byte `+0x6f`) | confirmed (code) |
| `0x0017a738` | `Occluders_HidesBox` | [Level loading](level-loading.md) | confirmed (code) |
| `0x0017a7f0` | `Plane_FromPoints(out, a, b, c)` | the plane through three points (normal, −distance) | confirmed (code) |
| `0x0017a8a8` | `Plane_Distance(point, plane)` | signed distance | confirmed (code) |
| `0x0017a8d8` | `Occluder_SetUpForViewport` | [Level loading](level-loading.md) | confirmed (code) |
| `0x0017ab78` | `Occluder_HidesSphere(occ, sphere)` | the sphere is at least its radius behind each of the occluder's 3 planes | confirmed (code) |
| `0x0017abe0` | `Occluder_HidesBox` | [Level loading](level-loading.md) | confirmed (code) |
| `0x0017aca8` | `Colour_PackFloat` | float RGBA × 255 to bytes ([Cars](cars.md), [World objects](objects.md)) | confirmed (code) |
| `0x0017ad30` | `Colour_PackFloatRounded` | the same with rounding (`0x0042c718`) | confirmed (code) |
| `0x0017adb0` | `Colour_Unpack` | bytes to float RGBA (÷ 255) | confirmed (code) |
| `0x0017ae38` | `ColourTable_Init` | [GUI](gui.md#colour-table) | confirmed (code) |
| `0x0017b1c0` | `Graphics_StaticInit` | calls `0x0017ae38`: not needed (compiler stub) | confirmed (code) |

### Models, render instances and world objects {#code-models}

`Model.cpp` (the model base: a RenderWare clump with its stamp and heap, vtable `0x00538cc8`), the render-instance
base every car, character and object instance derives from (vtable `0x00538ce0`, 0x40 bytes), `Object.cpp` and
`ObjectModel.cpp` (world objects, type 1 at map `+0x10`; [World objects](objects.md#models)). Confirmed (code) at
each address. Render-instance fields: `+0x0c` model, `+0x10`-`+0x18` texture dictionaries, `+0x1c` the clump copy,
`+0x20` the character data, `+0x24` tint, `+0x2c` the time it was made, `+0x30`/`+0x34` a state and the time it
changed, `+0x38` in the collision world.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0017f278` | `Model_SetupAtomicCb` | per atomic of a new model: sets the geometry's flag `0x40` and clears its first material's texture filter flag (`0x0046d010`) | confirmed (code) |
| `0x0017f2c0` | `ClumpChunk_Read` | reads a RenderWare clump from a chunk (`0x09`, `0x25`, `0x47`, [Chunk system](chunk-system.md)) and pushes it as `0x0A` (one atomic), `0x41` (one atomic, a single frame) or `0x50` (several) | confirmed (code) |
| `0x0017f380` | `Model_Construct(model, clump, heap, stream)` | stamp now + 3,000 ms, the clump, the atomics set up | confirmed (code) |
| `0x0017f4a0` | `Model_Destruct` | destroys the clump's atomics and the clump, frees the heap | confirmed (code) |
| `0x0017f608` | `Model_IsInUse` | true while any of the clump's atomics is still in use (`0x00198358`, `0x00198220`); asked by `MakeRoom` (`0x00187d28`) | confirmed (code) |
| `0x0017f650` | `RenderInstance_Construct(inst, data)` | base instance (stamp now + 3,000 ms), cleared (`0x0017f6d0`) | confirmed (code) |
| `0x0017f6d0` | `RenderInstance_Clear` | zero fields, white tint | confirmed (code) |
| `0x0017f708` | `RenderInstance_Destruct` | releases the model and textures (`0x0017f7c8`) and the character data | confirmed (code) |
| `0x0017f7a0` | `RenderInstance_TexDict(inst, i)` | texture dictionary *i* | confirmed (code) |
| `0x0017f7b0`, `0x0017f7b8`, `0x0017f7c0` | `RenderInstance_Model`, `RenderInstance_Clump`, `RenderInstance_CharData` | `+0x0c`, `+0x1c`, `+0x20` | confirmed (code) |
| `0x0017f7c8` | `RenderInstance_Release` | takes the clump out of the world and destroys it; one reference less (with a stamp) on the model and each dictionary | confirmed (code) |
| `0x0017f8f8` | `RenderInstance_Setup(inst, model, tex0, tex1, tex2)` | keeps them, clones the model's clump, adds it to the collision world, stamps the time (0 while the game clock is paused) | confirmed (code) |
| `0x0017f998` | `RenderInstance_SetState(inst, s)` | a new state and the real time of the change | confirmed (code) |
| `0x0017f9e8` | `RenderInstance_SetInWorld(inst, on)` | adds or removes the clump from the solid world's collision set | confirmed (code) |
| `0x0017fa80` | `ObjectRender_ApplyFadeDistance` | [World objects](objects.md) | confirmed (code) |
| `0x0017faf8` | `ObjectInstance_Construct` | instance of a world object (vtable `0x00538d08`), each atomic's plugin owner set (`0x001925d0`) | confirmed (code) |
| `0x0017fbc8` | `ObjectInstance_Destruct` | base destructor | confirmed (code) |
| `0x0017fbf0` | `ObjectAtomic_VisibilityCallback` | per atomic in view and not occluded: queues the object for drawing unless it is hidden (flag `0x10`), dead or flagged `1 << 26`, by its distance (`+0x134`) | confirmed (code) |
| `0x0017fd78` | `ObjectRender_Draw` | [World objects](objects.md), [Lighting](lighting.md) | confirmed (code) |
| `0x00180790` | `ObjectModel_SetupAtomic` | per atomic of an object model: callback `0x0017fbf0` (first kept at `0x005fde88`), the game pipelines | confirmed (code) |
| `0x001807e0` | `ObjectInstance_SetMatrix(inst, m)` | writes a game matrix (y and z swapped) into the clump's frame | confirmed (code) |
| `0x00180858` | `ObjectInstance_BoundingSphere` | the clump's first atomic's world bounding sphere | confirmed (code) |
| `0x001808b8`, `0x001809c0` | `ObjectModel_IsLoaded`, `ObjectModel_Request` | [World objects](objects.md) | confirmed (code) |
| `0x00180ee0` | `ResourceMgr_RequestObject(rm, rec)` | request an object record's model and dictionaries | confirmed (code) |
| `0x00180f58` | `ObjectModel_MakeInstance` | [World objects](objects.md) | confirmed (code) |
| `0x001810c0` | `ResourceMgr_ReleaseObjectInstance` | one reference less, stamp, delete | confirmed (code) |
| `0x00181170` | `ObjectList_OnLoaded` | chunk `0x46` ([WAD contents](formats/wad-contents.md#object-list)) | confirmed (code) |
| `0x001811a8`, `0x00181228` | `ObjectList_Count`, `ObjectList_Record(i)` | count, record *i* (0x24 bytes) | confirmed (code) |
| `0x001811b0` | `ObjectList_FindByHash` | [World objects](objects.md) | confirmed (code) |
| `0x00181248` | `ObjectModel_Construct` | object model (vtable `0x00538d30`), atomics set up by `0x00180790` | confirmed (code) |
| `0x001812a8` | `ObjectModel_Destruct` | base destructor | confirmed (code) |
| `0x001812d0`, `0x00181400` | `ResourceMgr_GetObjectModel`, `ResourceMgr_RequestObjectModel` | get / request, type 1 | confirmed (code) |
| `0x001814f0` | `ResourceMgr_FreeObjectModel` | free, type 1 (`ObjectModel.cpp`) | confirmed (code) |

### Sprite sheets, sprite batches and rain splashes {#code-sheets}

`ParticlePage.cpp` (sprite sheets: type 6 at map `+0x60`, [GUI](gui.md#particle-page)), the sprite-batch
("resource instance") methods that follow it ([GUI](gui.md#resource-instances)) and `RainDrops.cpp`. Confirmed (code)
at each address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00181b68` | `Sheet_Construct(sheet, page, heap, stream)` | sheet holder (vtable `0x00538d48`): the page, its texture (`+0x14`), the half-texel size | confirmed (code) |
| `0x00181ca0` | `Sheet_Destruct` | destroys the dictionary's textures and the dictionary, frees the page and the heap | confirmed (code) |
| `0x00181e18`, `0x00181e28` | `Sheet_TextureWidth`, `Sheet_TextureHeight` | [GUI](gui.md#particle-page) | confirmed (code) |
| `0x00181e38` | `Page_Rect` | [GUI](gui.md#particle-page) | confirmed (code) |
| `0x00181e50` | `ResourceMgr_SheetSize` | [GUI](gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header) | confirmed (code) |
| `0x00181f40` | `ResourceMgr_HasSheet(rm, hash, needRef)` | is resident, type 6 | confirmed (code) |
| `0x00181fb0` | `ResourceMgr_GetSheet` | get, type 6 (file `"%u"` of the hash) | confirmed (code) |
| `0x001820e0` | `ResourceMgr_RequestSheet` | request, type 6 | confirmed (code) |
| `0x001821d0` | `ResourceMgr_ReleaseSheet` | one reference less; at none, free, type 6 | confirmed (code) |
| `0x00182820` | `ChunkLoaded_ParticlePageHeader` | [GUI](gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header) | confirmed (code) |
| `0x001828b0`, `0x001828c0` | `SheetTable_Count`, `ResourceMgr_SheetRecord` | [GUI](gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header) | confirmed (code) |
| `0x001828e0` | `Instance_Init` | [GUI](gui.md#resource-instances) | confirmed (code) |
| `0x00182a50` | `Instance_Destroy` | if in use: unload (`0x001972b0`), switch off, destroy the PTank when no longer drawn | confirmed (code) |
| `0x00182ac8` | `Instance_Clear` | zeroes a slot (in use, not resident, no sprites) | confirmed (code) |
| `0x00182b20`, `0x00182b28`, `0x00182b30`, `0x00182b38` | `Instance_Capacity`, `Instance_Depth`, `Instance_Format`, `Instance_Field24` | `+0x18`, `+0x20`, `+0x38`, `+0x24` | confirmed (code) |
| `0x00182b40` | `Instance_PageTexture` | the page's texture (`page + 0x14`) | confirmed (code) |
| `0x00182b50` | `Instance_RequestByDistance` | asks the resource manager (`0x0018a830`) to stream the sheet by the instance's nearest camera distance less its depth squared | confirmed (code) |
| `0x00182bd8` | `Instance_SetInUse(inst, on)` | switching off also unloads a resident one | confirmed (code) |
| `0x00182c38`, `0x00182ca0` | `Instance_MakeRoomIfLoaded`, `Instance_MakeRoom` | evict (`0x00187d28`) enough for the sheet's size | confirmed (code) |
| `0x00182d08` | `Instance_RequestSheet` | request its sheet with its size | confirmed (code) |
| `0x00182d60`, `0x00182d70` | `Instance_Page`, `Instance_IsVisible` | `+0x0c`, byte `+0x13` | confirmed (code) |
| `0x00182d80`, `0x00182d88`, `0x00182d90`, `0x00182d98`, `0x00182da0`, `0x00182da8`, `0x00182db0`, `0x00182db8`, `0x00182dc0`, `0x00182dc8`, `0x00182dd0`, `0x00182dd8` | `Instance_PosArray`, `Instance_PosStride`, `Instance_ColourArray`, `Instance_ColourStride`, `Instance_UvArray`, `Instance_UvStride`, `Instance_SizeArray`, `Instance_SizeStride`, `Instance_Array64`, `Instance_Stride68`, `Instance_Array5C`, `Instance_Stride60` | the PTank's arrays and strides: `+0x44`/`+0x48` position, `+0x4c`/`+0x50` colour, `+0x54`/`+0x58` texture rectangle, `+0x6c`/`+0x70` size, `+0x64`/`+0x68` and `+0x5c`/`+0x60` the rotation and matrix arrays (which is which not traced) | confirmed (code) |
| `0x00182de0` | `Instance_AddSprite` | [GUI](gui.md#sprite-record) | confirmed (code) |
| `0x00183038` | `Instance_AddSpriteFormat` | the same into whichever arrays the format has (glass panes, [World objects](objects.md)) | confirmed (code) |
| `0x001831c0` | `Instance_DrawOneSpriteIm3D` | [World objects](objects.md) | confirmed (code) |
| `0x00183510` | `Raindrops_Create` | makes the splash object (`0x005971a8`, 0x6430 bytes) at start | confirmed (code) |
| `0x001835d8` | `Raindrops_Construct` | off; defaults: alpha 0.6, colour `0xffffff40` | confirmed (code) |
| `0x00183618`, `0x00183628` | `Raindrops_Enable`, `Raindrops_Disable` | on with the rain layer ([`StartRain`](../references/bindings/effects.md#startrain)), off with it and at level unload | confirmed (code) |
| `0x00183630` | `Raindrops_Configure` | `CfgRaindrops`: count per update, sprite, size | confirmed (code) |
| `0x00183640` | `Raindrops_ClassifyTriangle` | which side of the view's splash volume each corner of a ground triangle is on | confirmed (code) |
| `0x00183760`, `0x001837c8` | `Raindrops_AddTriangle`, `Raindrops_AddClippedTriangle` | store a ground triangle (64 bytes) in the view's list of up to 100 | confirmed (code) |
| `0x00183838` | `Raindrops_ClipTriangle` | cuts a triangle on the volume's plane and stores the pieces | confirmed (code) |
| `0x00183b88` | `Raindrops_CollectTriangle` | classify, then keep, clip or drop | confirmed (code) |
| `0x00183c58` | `Raindrops_BuildVolume` | the splash volume in front of the camera (10 m and 20 m spans) | confirmed (code) |
| `0x00183f48` | `Raindrops_GatherGround(rd, camera)` | walks the world sectors under the volume and collects their upward-facing triangles | confirmed (code) |
| `0x00184568` | `Raindrops_Update` | per update, from `Humans_Update`, while on and the game runs: for each view, gathers the ground and spawns the count of splash particles at random points of random triangles | confirmed (code) |
| `0x00184890` | `ResourceMgr_CompareOverlayKeys` | [GUI](gui.md#draw-order) | confirmed (code) |
| `0x001848d0` | `ResourceMgr_RoundSize(size)` | rounds a size up to its power of two, at least 32 KB (the heap's block) | confirmed (code) |

### The resource manager {#code-resource-manager}

`ResourceMgr.cpp`: the `ResourceManager` (`0x0050cd4c`, 0x904c bytes), which owns the per-type maps
([above](#code-resources)), the sprite batches, the streaming of resources by distance and the pending file
requests. How a level uses it: [Level loading](level-loading.md); a grouped container's load:
[Chunk system](chunk-system.md#loading-a-grouped-container). Fields used here, confirmed (code) at the functions
cited:

| Offset | Meaning |
| --- | --- |
| `+0x00`-`+0x60` | the type maps: character models, object models (`+0x10`), car models (`+0x20`), textures (`+0x30`), character data (`+0x40`), animation packs (`+0x50`), sheets (`+0x60`) |
| `+0x70` / `+0x74` | the default animation set and the default character data |
| `+0x80`-`+0xb0` | the Character List, Object List, Anim List and Dependency List chunks |
| `+0xb4` | 16 `{class, instance}` pairs: character instances kept resident by `SetCharacterModel` (`+0x1b4` changed) |
| `+0x1b8` | the dynamic animation slots, 0x28 bytes each (`SetDynamicAnimation`) |
| `+0xbbc`-`+0xbfc` | per kind (characters `+0xbc0`, objects `+0xbd0`, sprite batches `+0xbe0`, cars `+0xbf0`): the farthest resident one and its squared distance, and the nearest missing one and its distance, gathered each frame |
| `+0xc00`-`+0xc14` | the same for gang spawners |
| `+0xc18`-`+0xc3c` | a deque of deferred requests `{size, callback, arg}` (12 bytes) waiting for room |
| `+0xc44` / `+0xc54` | the glass panes queued far / near this frame ([World objects](objects.md#pane-draw)) |
| `+0xc64` / `+0xc6c` | the list of pending file requests and their count |
| `+0xc74` / `+0xc94` | the humans and cars in view this frame |
| `+0x189c` | 255 sprite batches of 0x78 bytes ([GUI](gui.md#resource-instances)) |
| `+0x9028`-`+0x9030` | a free list of request records (name, type, size, callback) |
| `+0x9034` / `+0x9038` | the 2D and 3D overlay worlds |

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00184918` | `ResourceManager_Create` | allocates and constructs it, loads `global.pak` (blocking) and makes the 13 start-up sprite batches ([GUI](gui.md#resource-instances)) | confirmed (code) |
| `0x00184eb0` | `ResourceMgr_LoadGenericHeader` | loads the resource named `generic_header` into its own heap and keeps its character data as the default set (`+0x70`, `+0x74`) | confirmed (code) |
| `0x001852d8` | `ResourceManager_Construct` | empty maps and lists, cleared distance records, the request free list, the overlay worlds | confirmed (code) |
| `0x00185990` | `ResourceMgr_HasPendingRequests` | `+0xc6c` ≠ 0 | confirmed (code) |
| `0x001859a0` | `ResourceMgr_IsRequested(rm, name)` | a pending request of that file name | confirmed (code) |
| `0x00185ab0` | `ResourceMgr_Instance(rm, i)` | sprite batch *i*, or null when it was never made (byte `+0x40`) | confirmed (code) |
| `0x00185ae0` | `ResourceMgr_DestroyLevelInstances` | destroys batches 13 and up (level unload) | confirmed (code) |
| `0x00185b38` | `ResourceMgr_Render3DSprites` | [World objects](objects.md) | confirmed (code) |
| `0x00185cc8` | `ResourceMgr_EmptyInstances` | [GUI](gui.md#draw-order) | confirmed (code) |
| `0x00185d20` | `ResourceMgr_RenderOverlay` | [GUI](gui.md#draw-order) | confirmed (code) |
| `0x00186068` | `ResourceMgr_UpdateInstances` | per update: each batch in use asks to be streamed by distance (`0x00182b50`); one no longer in use is destroyed | confirmed (code) |
| `0x00186110` | `ResourceMgr_FindForGroup` | [Chunk system](chunk-system.md#loading-a-grouped-container) | confirmed (code) |
| `0x001866e8` | `ResourceMgr_OnFileLoadedCb` | file-manager callback: `0x00186710` with the request | confirmed (code) |
| `0x00186710` | `ResourceMgr_OnFileLoaded(rm, req, file)` | a heap named after the file, the chunk system over the data, the resource object (`0x00187098`); the request leaves the pending list and returns to the free list | confirmed (code) |
| `0x00186e88`, `0x00186ff0` | `ResourceMgr_BeginGroup`, `ResourceMgr_EndGroup` | [Chunk system](chunk-system.md#loading-a-grouped-container) | confirmed (code) |
| `0x00187098` | `ResourceMgr_CreateResource(rm, req, heap, stream, group)` | by type: object model (chunk `0x41`), car model (`0x50`), character model (`0x28` bind offsets and `0x0A`), textures, character data, animation pack, sheet; puts it in its map and marks its dependency group | confirmed (code) |
| `0x00187960` | `ResourceMgr_AllocSize(rm, type, size, size2)` | the heap to ask for a type: size + `0x2000` for some types, + `0x70` for others, or the second size | confirmed (code) |
| `0x001879b8` | `ResourceMgr_Heap` | the world's sector pool (the heap resources live in) | confirmed (code) |
| `0x001879e0`, `0x00187a48` | `ResourceName_ToNumber`, `ResourceName_ToHash` | a file name's decimal part as a number / its CRC-32, after the last `/` | confirmed (code) |
| `0x00187aa8` | `ResourceMgr_Request(rm, type, name, size, size2)` | takes a request record, makes room (`0x00187d28`, forced), asks the file manager for the file with callback `0x001866e8` and adds it to the pending list | confirmed (code) |
| `0x00187c38` | `ResourceManager_LoadPack` | [Level loading](level-loading.md) | confirmed (code) |
| `0x00187d28` | `ResourceManager_MakeRoom` | [Level loading](level-loading.md) | confirmed (code) |
| `0x001886d8` | `ResourceMgr_QueueWhenRoom(rm, size, callback, arg)` | runs the callback at once if the size fits, else adds it to the deferred deque ([Scenes](scenes.md)) | confirmed (code) |
| `0x001887b8` | `ResourceMgr_ServiceDeferred` | for each deferred request whose size now fits: run it and drop it; true when the deque is empty | confirmed (code) |
| `0x00188878` | `ResourceMgr_ClearDeferred` | empties the deque | confirmed (code) |
| `0x001888f0` | `ResourceMgr_WaitForRoom` | [Chunk system](chunk-system.md#loading-a-grouped-container): queues, then streams for up to 200 ms | confirmed (code) |
| `0x001889d0` | `ResourceMgr_LoadAllBlocking` | until the deque is empty: stream, update the world, service the file manager; then drain the file queue (level load) | confirmed (code) |
| `0x00188aa8` | `ResourceMgr_FreeUnused` | flushes the renderer, releases the dynamic animations and characters, then frees every resource with no references in every map (level unload) | confirmed (code) |
| `0x001894f8` | `Sector_ClearFadeInCb` | world-sector callback: the sector's fade-in end ([sector plugin](world.md#sector-plugin) `+0x10`) = 0 | confirmed (code) |
| `0x00189528` | `ResourceMgr_ResetFadeIns` | clears the fade-in stamps so everything shows at once: every sector of both solid worlds (`0x001894f8`), each object task's instance (`+0x2c`) and 60 more instances' `+0x30` | confirmed (code) |
| `0x00189628`, `0x001896c0` | `ResourceMgr_ResetNearest`, `ResourceMgr_ResetNearestGangs` | clear the per-kind distance records (`+0xbbc`, `+0xc00`) | confirmed (code) |
| `0x00189718` | `ResourceMgr_ResetStreaming` | both resets and the dynamic animations released (level unload) | confirmed (code) |
| `0x00189750` | `ResourceMgr_NearestWanted` | the next dependency to load, or the nearest missing kind's distance ([The streamed world](world.md)) | confirmed (code) |
| `0x001897a8` | `ResourceManager_LoadNearestModel` | [World objects](objects.md), [Characters](characters.md) | confirmed (code) |
| `0x00189ed8` | `ResourceMgr_StreamGangs` | gang spawners: loads the nearest missing one's character, unloads the farthest when room is short | confirmed (code) |
| `0x0018a2b0` | `ResourceMgr_EvictFarthest` | drops the farthest resident character, object or car instance when it is no longer used | confirmed (code) |
| `0x0018a430` | `ResourceMgr_NoteHuman(dist, rm, human)` | records a human's squared distance (farthest loaded / nearest missing) and puts its clump in or out of the collision world by the camera's range | confirmed (code) |
| `0x0018a5f0`, `0x0018a720`, `0x0018a830`, `0x0018a900` | `ResourceMgr_NoteObject`, `ResourceMgr_NoteCar`, `ResourceMgr_NoteInstance`, `ResourceMgr_NoteGang` | the same for an object, a car, a sprite batch and a gang spawner | confirmed (code) |
| `0x0018a980` | `ResourceMgr_Stream` | each frame: object spawns, then the humans, objects and cars' distance notes, then the loads and evictions | confirmed (code) |
| `0x0018ab20` | `ResourceMgr_ReleaseDynamic` | releases the dynamic animation slots and the kept character instances | confirmed (code) |
| `0x0018abd0` | `ResourceManager_SetDynamicAnimation(rm, name, on)` | keeps a named pack in a free or least-used slot, or releases it | confirmed (code) |
| `0x0018ad40` | `ResourceManager_SetCharacterModel(rm, cfg, on)` | keeps (or drops) a character instance of that class's model resident | confirmed (code) |
| `0x0018aec0` | `ResourceMgr_CreateInstance` | [GUI](gui.md#resource-instances) | confirmed (code) |
| `0x0018af88` | `ResourceMgr_DestroyInstance(rm, i)` | destroys sprite batch *i* | confirmed (code) |
| `0x0018afb8`, `0x0018b008` | `ResourceMgr_QueuePaneFar`, `ResourceMgr_QueuePaneNear` | [World objects](objects.md#pane-draw) | confirmed (code) |
| `0x0018b058` | `ResourceMgr_DrawPanes` | draws the far panes, then the near ones, and empties the lists | confirmed (code) |

### Screen effects manager {#code-screen-effects}

`ScreenEffectsManager.cpp` and its script entry points; the manager's layout and behaviour are in
[Screen effects](#screen-effects). Two managers, `0x005fdeb8` (view 0) and `0x005fdebc` (view 1); a script call
acts on both. Times are on the real-time clock in seconds. Confirmed (code) at each address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0018b168` | `ScreenFx_Queue(type, seconds)` | `ScreenQueueEffect` on both managers (`0x0018d450`) | confirmed (code) |
| `0x0018b1e0` | `ScreenFx_Construct(mgr, view)` | a free list of 16 effect records, the 13 looks' defaults, base and current look 8 | confirmed (code) |
| `0x0018b460` | `ScreenFx_SetBaseLook(mgr, look)` | [Looks](#looks) | confirmed (code) |
| `0x0018b4a8`, `0x0018b648` | `ScreenFx_ConfigLook`, `ScreenFx_ConfigBlurPulse` | `CfgScrFx`'s colour and numeric forms ([Screen effects](#screen-effects)) | confirmed (code) |
| `0x0018b7d0`, `0x0018b950` | `ScreenFx_StartLook`, `ScreenFx_EndLook` | [Looks](#looks) | confirmed (code) |
| `0x0018ba10` | `ScreenFx_Create` | makes the two managers at start | confirmed (code) |
| `0x0018bae0`, `0x0018bd48` | `ScreenFx_StartLayer`, `ScreenFx_EndLayer` | [Room smoke](#room-smoke) | confirmed (code) |
| `0x0018bdd0` | `ScreenFx_SetCameraIndoors(mgr, on)` | from the camera's ground probe: tells the rain layer (its slot `+0x40`) and switches the layers that only show outdoors (`+0x16c`) or indoors (`+0x18c`) | confirmed (code) |
| `0x0018be58` | `ScreenFx_AddRainPlane(p, a, b)` | `SpawnRainPlane`: stores a plane (a point and two vectors) in one of 25 slots at `0x005fdec0` (count `0x005fdf24`) for the rain layer | confirmed (code) |
| `0x0018bf68` | `ScreenFx_UpdateView` | [Room smoke](#room-smoke) | confirmed (code) |
| `0x0018c058` | `ScreenFx_ResetAll` | at level start: ends the current look and every layer, clears the blends, base and current look 8 | confirmed (code) |
| `0x0018c310` | `ScreenFx_DrawBlurPass(a, b, mgr)` | device slot `+0x108` on the manager's viewport camera with the viewport rectangle | confirmed (code) |
| `0x0018c408` | `ScreenFx_DrawMotionBlur(mgr)` | blends the motion-blur strength from `+0x1a4` to `+0x1a8` over its time and draws it (not in game mode 100's state, nor when `+0x214` is set) | confirmed (code) |
| `0x0018c8c8` | `ScreenFx_BlendBlurTo(seconds, mgr, alpha)` | starts that blend (instant when 0 s) | confirmed (code) |
| `0x0018c988` | `ScreenFx_BlendTintTo(seconds, mgr, colour)` | starts the tint blend from the current tint (instant when 0 s) | confirmed (code) |
| `0x0018ca50` | `ScreenFx_DrawTint(mgr)` | the tint's current colour along its blend, filled over the viewport (`0x0018cc20`) | confirmed (code) |
| `0x0018cb78` | `ScreenFx_FinishTintBlend` | jumps the tint to its target once the time is past | confirmed (code) |
| `0x0018cc20` | `ScreenFx_FillViewport(mgr, colour)` | device slot `+0x130` for the manager's viewport | confirmed (code) |
| `0x0018cc60`, `0x0018ce58` | `ScreenFx_StartFade`, `ScreenFx_DrawFade` | [Front end](frontend.md) | confirmed (code) |
| `0x0018d020` | `ScreenFx_IsFadeActive` | true while a fade runs or its level is not 0 | confirmed (code) |
| `0x0018d058` | `ScreenFx_StartBlurPulse` | [Screen effects](#screen-effects) | confirmed (code) |
| `0x0018d1d0` | `ScreenFx_DrawBlurPulse` | [Screen effects](#screen-effects) | confirmed (code) |
| `0x0018d450` | `ScreenFx_QueueEffect` | [Screen effects](#screen-effects) | confirmed (code) |
| `0x0018d5f8`, `0x0018d868`, `0x0018d910` | `ScreenFx_DrawLetterbox`, `ScreenFx_StartLetterbox`, `ScreenFx_StepLetterbox` | [Screen effects](#screen-effects), [HUD](hud.md) | confirmed (code) |
| `0x0018da80` | `ScreenFx_CopyFadeAndLetterbox(dst, src)` | view 1 follows view 0's fade and letterbox | confirmed (code) |
| `0x0018dac0` | `ScreenFx_Render` | device slot `+0x128`: each view's blur pulse, tint, fade, letterbox and motion blur, in that order | confirmed (code) |
| `0x0018dcc0` | `ScreenFx_EnableHeat(on)` | `EnableHeat`: look 7 on or off | confirmed (code) |
| `0x0018dd38` | `ScreenFx_SpawnRainPlane` | `SpawnRainPlane` | confirmed (code) |
| `0x0018dd58` | `ScreenFx_SetRaindrops` | `CfgRaindrops` | confirmed (code) |
| `0x0018dda0`, `0x0018deb0` | `ScreenFx_StartRain`, `ScreenFx_EndRain` | `StartRain` / `EndRain` (layer 0, [effects bindings](../references/bindings/effects.md)) | confirmed (code) |
| `0x0018df00`, `0x0018dfa8` | `ScreenFx_StartFog`, `ScreenFx_EndFog` | `StartFog` / `EndFog` (layer 1) | confirmed (code) |
| `0x0018dff8`, `0x0018e048` | `ScreenFx_StartFilmGrain_Stub`, `ScreenFx_EndFilmGrain` | `StartFilmGrain` does nothing; `EndFilmGrain` ends layer 2 | confirmed (code) |
| `0x0018e070`, `0x0018e0f8` | `ScreenFx_StartRoomSmoke`, `ScreenFx_EndRoomSmoke` | [Room smoke](#room-smoke) | confirmed (code) |
| `0x0018e148`, `0x0018e2f0`, `0x0018e3c8` | `Fog3D_Start`, `Fog3D_SetMaxParticles`, `Fog3D_End` | [Drifting fog](particles.md#fog) | confirmed (code) |
| `0x0018e470`, `0x0018e530` | `ScreenFx_CfgLook`, `ScreenFx_CfgBlurPulse` | `CfgScrFx` on both managers | confirmed (code) |
| `0x0018e5f0` | `ScreenFx_SetColourOverlay` | `SetLevelColour`: look 9's tint (× 255) and base look 9 | confirmed (code) |
| `0x0018e6b8`, `0x0018e780` | `ScreenFx_EnterStore`, `ScreenFx_ExitStore` | `EnterStore`: look 10's tint and base look 10; `ExitStore` (and leaving a store screen): base look 9 | confirmed (code) |
| `0x0018e7d0` | `Gamma_SetRamp_Stub` | `SetGammaRamp` does nothing | confirmed (code) |

### Texture dictionaries and the throw-aim arc {#code-textures}

`Texture.cpp` (texture dictionaries, type 4 at map `+0x30`, [above](#code-resources)) and, after it, an unnamed file
that draws the arc a held object will fly along while the player aims a throw (its file is inferred from position).
Confirmed (code) at each address unless a row says otherwise.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0018e7e8` | `TexDict_Construct(holder, page, heap, stream)` | dictionary holder (vtable `0x00538d60`): stamp now + 3,000 ms, the dictionary, its heap and stream | confirmed (code) |
| `0x0018e898` | `TexDict_Destruct` | destroys the dictionary's textures, the dictionary and its heap | confirmed (code) |
| `0x0018e9d8` | `ResourceMgr_TexturesFit_Stub` | the type-4 "fits" test of `MakeRoom`: always 0 | confirmed (code) |
| `0x0018e9e0` | `ResourceMgr_GetTextures(rm, hash, size, size2, noLoad)` | get, type 4 (file `"%u"` of the hash): a resident one gains a reference; else, unless told not to, requested and loaded at once | confirmed (code) |
| `0x0018eb10` | `ResourceMgr_RequestTextures` | request, type 4; a resident one only has its stamp renewed | confirmed (code) |
| `0x0018ec00` | `ResourceMgr_FreeTextures` | free, type 4: a holder with no references is destroyed and leaves the map | confirmed (code) |
| `0x0018f230` | `ThrowArc_Clear` | a cleared arc record (0x30 bytes: clump, atomic, material, human, start and end points); one per player in the game state at `+0x5740` | confirmed (code) |
| `0x0018f268` | `ThrowArc_Create` | the arc's model: texture `aimthrow_tex` (loaded once, `0x005971b0`), a strip of 18 vertex pairs and 32 triangles in one material | confirmed (code) |
| `0x0018f628` | `ThrowArc_PlaceAtHand` | puts the arc's frame at the human's hand (position `+0x610` plus its facing `+0x620` turned by a fixed offset) | confirmed (code) |
| `0x0018f7b0` | `ThrowArc_SetHuman` | `+0x0c` (from `PlayerList_Add`) | confirmed (code) |
| `0x0018f7b8` | `ThrowArc_TargetFilter` | the sweep's callback: for a player, a human counts unless it is an ally (`0x00222a90`) or lies on the other side from the one the left stick pushes (dead zone 26) | confirmed (code); the side test inferred |
| `0x0018fa68` | `ThrowArc_BuildStrip(arc, points, count, hit)` | writes the arc's points as a strip 0.16 m wide, fading in from the hand; one colour when `hit` is set and another when not | confirmed (code) |
| `0x0018fee0` | `Human_TraceThrowAim` | each frame of the aiming state (`0x00244770`): steps the throw from the hand at 1/60 s with gravity (z speed − 0.2613 a step) for 108 steps, keeps every sixth point (18), sweeps a sphere of half the object's size along them, stops at the first human hit (the target, human `+0x638`) or wall, and rebuilds the strip | confirmed (code) |
| `0x00190588` | `ThrowArc_Render` | from `WorldManager_Render`, while its human is in the aiming state: draws the clump in each view with back-face culling off | confirmed (code) |
| `0x001906e8`, `0x00190770` | `ChunkReader_TextureDictionaryTid`, `ChunkReader_RenderwareTextureDic` | [Chunk system](chunk-system.md) | confirmed (code) |

### The water surface and the atomic plugin {#code-water}

`WaterEffect.cpp`: the level's single water surface, made and moved by
[`SetPositionOfWater`](../references/bindings/world.md#setpositionofwater) (world manager `+0x5c`). It is the world
manager's third streamer ([Choosing what to stream](world.md#streaming)): its file (a texture dictionary) is read
only when it is near enough. After it come the atomic plugin `0x3F0`'s methods ([The streamed world](world.md#atomic-plugin)).
Water fields, confirmed (code) at `0x00191230` and `0x00190810`:

| Offset | Meaning |
| --- | --- |
| `+0x00` / `+0x04` | the clump and its atomic (null until loaded) |
| `+0x10` / `+0x20` | the position and the rotation (a quaternion, normalised; all zeros means none) |
| `+0x30` / `+0x34` | streaming allowed (1) / loaded |
| `+0x38` / `+0x3c` | the texture's hash; the file's size (looked up in the file table by the hash) |
| `+0x40` / `+0x44` | width and length in metres |
| `+0x48` / `+0x4c` | grid vertices across and along (columns + 1, rows + 1) |
| `+0x50` / `+0x54` | texture repeats across and along |
| `+0x60` | the time of the last wave update |
| `+0x64` | colour, 4 bytes RGBA |
| `+0x68` / `+0x6c` | wave height and wave speed |
| `+0x70` | the 8 corners of a unit box, for the visibility and distance tests |
| `+0xf0` / `+0xf4` | the heap and the texture-dictionary holder |

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00190810` | `WaterEffect_Load` | once its file is read: a heap named `Water`, the dictionary from it (chunk `0x0B`), then the grid geometry (prelit `0x7f7f`, texture coordinates by the repeats, two triangles a cell) in a new clump | confirmed (code) |
| `0x00191158` | `WaterEffect_Unload` | destroys the clump and the dictionary; not loaded (also when the world manager evicts it) | confirmed (code) |
| `0x00191230` | `WaterEffect_Init` | [`SetPositionOfWater`](../references/bindings/world.md#setpositionofwater): the fields above from its arguments | confirmed (code) |
| `0x00191530` | `WaterEffect_Destruct` | unloads, then frees itself when asked | confirmed (code) |
| `0x00191580` | `WaterEffect_HeapSize` | the room its load needs: vertices × 64 + the file size + `0x1064` | confirmed (code) |
| `0x001915a0` | `WaterEffect_SetFrame(water, posRot)` | position and rotation, then (when loaded) the clump's frame from them, scaled by width and length | confirmed (code) |
| `0x00191728`, `0x00191750` | `WaterEffect_SetWidth`, `WaterEffect_SetLength` | then re-places the frame | confirmed (code) |
| `0x00191778` | `WaterEffect_SetColour` | `+0x64` | confirmed (code) |
| `0x00191798` | `WaterEffect_BoxCorners(water, out)` | the unit box's 8 corners through the water's frame | confirmed (code) |
| `0x00191908` | `WaterEffect_IsVisible` | false when every corner is outside one of player 1's camera planes | confirmed (code) |
| `0x00191a60` | `WaterEffect_CameraDistance` | the nearest face of the box to player 1's camera, for streaming | confirmed (code) |
| `0x00191dd8` | `WaterEffect_Update(time, water)` | from `WorldManager_Render`, when loaded and visible: at most every `0x0050cd84` s rewrites each column's height (sin(time × speed + column) × wave height), alpha and the texture's scroll along x, then draws the clump | confirmed (code) |
| `0x00192588`, `0x001925b0` | `WaterEffect_OnFileLoaded`, `WaterEffect_LoadThunk` | the file-read callback the world manager gives it | confirmed (code) |
| `0x001925d0` | `AtomicPlugin_SetOwnerCb` | per atomic of a new instance: owner (`+0x0c`) and word `+0x08` from the record passed | confirmed (code) |
| `0x00192688`, `0x00192740` | `AtomicPlugin_Read`, `AtomicPlugin_Write` | [Atomic plugin `0x3F0`](world.md#atomic-plugin) | confirmed (code) |
| `0x001927d8` | `AtomicPlugin_Register` | registers the 16 bytes and the stream methods | confirmed (code) |
| `0x00192858`, `0x00192870` | `AtomicPlugin_SetOwner`, `AtomicPlugin_Owner` | `+0x0c` | confirmed (code) |
| `0x00192888` | `AtomicPlugin_SetWord8` | `+0x08` | confirmed (code) |
| `0x001928a8`, `0x001928c0` | `AtomicPlugin_SetPosScale`, `AtomicPlugin_PosScale` | `+0x00` | confirmed (code) |
| `0x001928d8`, `0x001928f0` | `AtomicPlugin_SetUvScale`, `AtomicPlugin_UvScale` | `+0x04` | confirmed (code) |

**Drawing the water** (`WaterEffect_Update(phase, water)`, `0x00191dd8`), confirmed (code); constants read from
`.data`. `WorldManager_Render` passes a phase that grows by 0.16 each frame ([The streamed world](world.md#a-frame),
step 9) and draws with culling off; Z test, Z write and fog stay as the world pass left them (on).

1. Nothing while the water is not loaded or not in view (`0x00191908`).
2. When the phase has moved more than **0.3** (`0x0050cd84`) since the last update (`+0x60`), so every second frame,
   with `t = phase × speed` (`+0x6c`):
    - the texture scroll `u0 = fmod(t × 0.3, repeatsAcross)` (`0x0050cd90` = 0.3);
    - for each column `i` but the last: `s = sin(i + t)`; every vertex of the column gets height `s × waveHeight`
      (`+0x68`, the vertex's z) and the water's colour (`+0x64`) with alpha **`225 + 5 s`** (`0x0050cd8c` = 225,
      `0x0050cd88` = 5; RenderWare's 0-255 scale, so about 88 % opaque);
    - texture coordinates `u = u0 + repeatsAcross × i / (columns − 1)` and `v = repeatsAlong × j / (rows − 1)`;
    - the last row and the last column copy the first one's heights and colours, so the surface tiles.
3. Unlock the geometry and render the clump (`0x0046a100`).

### The RenderWare device's helpers {#code-device}

`DevRWGeneric.cpp`'s functions that are not in the [device](#device-vtable) or [camera](#camera-wrapper) tables
above: RenderWare's memory hooks, the immediate-mode drawer (vtable `0x00538f08`), the sprite batches' PTank methods
([GUI](gui.md#resource-instances)), the custom stream and render-state helpers, and the sector plugin `0x3F1`
([The streamed world](world.md#sector-plugin)). Confirmed (code) at each address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00192a08` | `RwMemory_Free` | RenderWare's free hook: the `Filter Pool` when the block is inside it, else the current heap ([Memory](memory.md)) | confirmed (code) |
| `0x00192ac8` | `RwMemory_Realloc` | the realloc hook (a null block allocates) | confirmed (code) |
| `0x00192b98` | `RwMemory_Calloc` | the calloc hook: allocates `Renderware Calloc` and zeroes it | confirmed (code) |
| `0x00193c20` | `ScreenQuad_DrawOffset` | one textured quad with a shift, for the [blur pass](#motion-blur) | confirmed (code) |
| `0x00194fb8` | `RwDevice_InitRasterCall` | from `Init`: a RenderWare call (`0x00486120`) with `0x85` and the main raster's format bits | confirmed (code); what the call is not traced |
| `0x001968c0` | `Im2DDrawer_DrawIndexed(…, raster, matrix, verts, count)` | the drawer's slot `+0x48`: an RwIm3D indexed list in one viewport only (none in split screen), nearest filtering, Z write off, alpha blending | confirmed (code) |
| `0x00196ac8` | `Im2DDrawer_Project(…, out, point)` | slot `+0x50`: a world point through the main camera (`0x00198548`), scaled by 1/1024 and 1/512 | confirmed (code) |
| `0x00196cd0`, `0x00196cf8` | `Im2DDrawer_StaticInit`, `Im2DDrawer_StaticInitThunk` | static initialiser: the drawer at `0x005ff770` gets vtable `0x00538f08` | confirmed (code) |
| `0x00196d18` | `Instance_Construct` | a cleared batch with its own frame | confirmed (code) |
| `0x00196d60` | `Instance_CanUnload` | not resident, or its PTank no longer in use (`0x00198220`) | confirmed (code) |
| `0x00196d90` | `Instance_Unload` | takes the PTank out of its overlay world, destroys it in the resource heap and releases the sheet | confirmed (code) |
| `0x00196ee0` | `Instance_SetDepth(depth, inst)` | `+0x20` and the PTank's bounding-sphere radius | confirmed (code) |
| `0x00196f48`, `0x00196f90` | `Instance_Position`, `Instance_SetPosition` | the frame's position, swapping the y and z axes (y negated) | confirmed (code) |
| `0x00197108` | `Instance_Restart` | when resident and in use: unlocks, empties the PTank and locks it again (loading screens, `ResourceMgr_EmptyInstances`) | confirmed (code) |
| `0x001976e8` | `Instance_MemoryNeeded` | bytes per sprite by the format's arrays × capacity (twice for format 2) + `0x4000`, plus the sheet's size when it is not resident | confirmed (code) |
| `0x001977c8` | `Instance_Lock` | locks the PTank arrays its format has (position `0x01`, colour `0x02`, texture rectangle `0x80`, matrix `0x08`, rotation `0x20`, size `0x04`) | confirmed (code) |
| `0x00197900` | `Instance_Unlock` | unlocks them and clears the six array pointers | confirmed (code) |
| `0x00197970` | `Instance_ClearSprites(inst, from, to)` | zeroes those sprites' arrays | confirmed (code) |
| `0x00197b30` | `ChunkReader_SectorBspData` | chunk `0x15` ([Chunk system](chunk-system.md)): a world through device slot `+0x170`, pushed as `0x42` | confirmed (code) |
| `0x00197b78` | `World_RenderSectorCb` | the sector render callback `ReadWorld` installs: calls the one it replaced (`0x005ff77c`) | confirmed (code) |
| `0x00197cd0` | `World_RemoveObjectCb` | `DestroyWorld`'s callback: takes each clump (and light) out of the world first | confirmed (code); the light half inferred |
| `0x00197d60`, `0x00197d98`, `0x00197dc8` | `RwStreamCustom_Read`, `RwStreamCustom_Write`, `RwStreamCustom_Skip` | the custom stream's callbacks over a game stream (`0x00197df0`) | confirmed (code) |
| `0x00197e28`, `0x00197ec0` | `RenderState_Save`, `RenderState_Restore` | six render states (vertex alpha, both blends, culling, Z test, fog) into and out of a record (fills, letterbox) | confirmed (code) |
| `0x001983a8` | `Atomic_SetPluginWord` | per atomic of a character model: one RenderWare plugin word (`0x0052e684`) | confirmed (code) |
| `0x001983d0`, `0x00198428` | `Frame_FindObjectCb`, `Clump_GameData` | the first object attached in the clump's frame tree (`0x00198428`'s caller reads its word `+4`, [Chunk system](chunk-system.md)) | confirmed (code); inferred for what the object is |
| `0x00198458` | `RwCam_Camera` | the wrapper's `RwCamera` (`+0x00`) | confirmed (code) |
| `0x00198548`, `0x001985b0` | `Camera_ProjectThrough`, `Matrix_Copy` | a point through a camera's view matrix and `0x00198460`; a 4 × 4 copy | confirmed (code) |
| `0x00198658` | `Matrix_SetIdentity` | identity, flags `0x20003` | confirmed (code) |
| `0x001986e8`, `0x00198738` | `RwCam_Destruct`, `RwCam_Clear` | the wrapper's destructor; its cleared fields | confirmed (code) |
| `0x00198ed0`, `0x00198f70` | `SectorPlugin_Write`, `SectorPlugin_Register` | the 20 streamed bytes; registration (32 bytes, `0x0050ced0`) from `Init` | confirmed (code) |
| `0x00198ff0` | `SectorPlugin_SetAtomic` | `+0x00` | confirmed (code) |
| `0x00199008`, `0x00199020`, `0x00199038`, `0x00199050` | `SectorPlugin_SetIndex`, `SectorPlugin_SetState`, `SectorPlugin_SetPart`, `SectorPlugin_SetFadeEnd` | `+0x04`, `+0x08`, `+0x0c`, `+0x10` | confirmed (code) |
| `0x00199068` | `SectorPlugin_SetOrigin` | `+0x14`-`+0x1c`: where the sector's atomic is placed | confirmed (code) |

### Overlay effects: film grain, fog and rain {#code-overlay-effects}

`OverlayEffects/`: the screen-effect layers ([Screen effects](#screen-effects)), each an `OverlayEffect` with a list
of `OE_Particle` sprite widgets (`+0x04`-`+0x0c`) and its settings from `+0x98`. They share the base's colour slots
(`+0x28` set every widget's colour, `+0x30` widget 0's colour) and its 30 Hz tick ([Room smoke](#room-smoke), which
also covers `OE_RoomSmoke.cpp` and the base). Vtables: film grain `0x00539050`, fog `0x005390a0`, rain `0x005390f0`,
room smoke `0x00539140`; slot `+0x10` destroys, `+0x18` updates, `+0x20` takes new settings. Confirmed (code) at each
address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001990b8` | `OE_FilmGrain_Construct` | one widget of the settings' sprite and colour, depth 11,000 (layer 2; `StartFilmGrain` never starts it) | confirmed (code) |
| `0x00199208`, `0x00199230` | `OE_FilmGrain_Destruct`, `OE_FilmGrain_Configure` | settings: sprite word `+0x9c`, colour `+0xa0`, repeats `+0xa4`/`+0xa5` | confirmed (code) |
| `0x00199258` | `OE_FilmGrain_Update` | width × 1/0.7, the colour, and a new random texture offset each tick over the repeats: the grain jumps every update | confirmed (code) |
| `0x00199380` | `OE_Fog_Construct` | one widget (depth 10,000), random start offsets, the first two drifts | confirmed (code) |
| `0x001995c0`, `0x001995e8` | `OE_Fog_Destruct`, `OE_Fog_Configure` | the 0x34 settings bytes ([`StartFog`](../references/bindings/effects.md#startfog)) | confirmed (code) |
| `0x00199658` | `OE_Fog_FollowCamera` | adds the camera's heading change to the sideways scroll and half its pitch change to the vertical one | confirmed (code) |
| `0x001996b0` | `OE_Fog_PickDrift` | a sideways and a vertical scroll (random sign), a width (1.43 to `p5` / 0.7), a height (1 to `p6`), a blend time (`p8`-`p9` ms) and an alpha (between the colour's fourth and fifth values) | confirmed (code) |
| `0x001997d8` | `OE_Fog_BlendDrift` | the same linear blend as the room smoke's, then the next drift | confirmed (code) |
| `0x00199a50` | `OE_Fog_Update` | blend, follow the camera, size, colour, then scroll the texture rectangle `(u, -v)`-`(u + 1, 1 - v)`, wrapping at 1 | confirmed (code) |
| `0x00199ba8` | `OE_Rain_Construct` | two screen sheets (depth 10,000) and, for each rain plane, two more on the plane | confirmed (code) |
| `0x00199e08` | `OE_Rain_Destruct` | frees the plane batches first | confirmed (code) |
| `0x00199e50` | `OE_Rain_Configure` | [`StartRain`](../references/bindings/effects.md#startrain) on a running layer: new layers, new sprites where they changed | confirmed (code) |
| `0x0019a048`, `0x0019a118` | `OE_Rain_CreatePlaneBatches`, `OE_Rain_FreePlaneBatches` | one sprite batch per layer (25 sprites, depth 8,000) for the planes | confirmed (code) |
| `0x0019a1b8` | `OE_Rain_AddSheet(rain, layer, plane, batch)` | a screen sheet, or a sheet on a plane: placed by the plane's point and two edges, each at least 6 m | confirmed (code) |
| `0x0019a648` | `OE_Rain_Tilt` | eases a vector toward `0x006f31a0` by 0.004 a frame and tilts the sheets by its first part, clamped to ±10° | confirmed (code); what the vector is not traced |
| `0x0019a780` | `OE_Rain_PlaneFacesCamera` | outdoors only: the plane's normal points away from the camera | confirmed (code) |
| `0x0019a8e0` | `OE_Rain_UpdateSheet` | a screen sheet: size 1.7 (2.0 in 16:9, × 1.15 in an armies level), tilt, colour, scroll; a plane sheet copies its layer's colour and rectangle (half the vertical repeats when the plane is 12 m or more) | confirmed (code) |
| `0x0019ac48` | `OE_Rain_Update` | indoors (`+0x90`) no tilt and no screen sheets; each plane's sheets only while it faces the camera | confirmed (code) |
| `0x0019afd0` | `OE_RoomSmoke_Destruct` | [Room smoke](#room-smoke) | confirmed (code) |
| `0x0019b8c0` | `OverlayFx_TakePitchDelta` | the camera's pitch mapped from −50°-10° to 0-1, less last time's | confirmed (code) |
| `0x0019bbb0`, `0x0019bbf8` | `OverlayEffect_AddWidget`, `OverlayEffect_GetWidget` | the widget list | confirmed (code) |
| `0x0019be28`, `0x0019bea0` | `OverlayEffect_SetColourAll`, `OverlayEffect_Colour` | slots `+0x28` and `+0x30` | confirmed (code) |

### Heat distortion {#code-distortion}

`DistortionEffectManager.cpp` and the effects after it: shimmering air over fires and the ring a car explosion sends
out. The manager (`0x005ff780`) holds three `HeatDistortionEffect` slots (`{effect, used}` at `+0x00`-`+0x14`, 0x3310
bytes each, vtable `0x005391e0`) and one `HeatWaveEffect` (`+0x18`/`+0x1c`, 0x3320 bytes, vtable `0x00539228`). An
effect is a grid of `(cols + 1) × (rows + 1)` vertices, `cell` metres apart, kept facing the camera at a world point;
each vertex samples the frame buffer at its own place on the screen plus a small moving offset, so what is behind it
wavers. It is drawn by device slot `+0x118` after the world (not in an armies level). Confirmed (code) at each
address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0019bed0`, `0x0019c598`, `0x0019c5c8` | `DistortionMgr_Construct`, `DistortionMgr_StaticInit`, `DistortionMgr_StaticInitThunk` | the static manager (its constructor does nothing) | confirmed (code) |
| `0x0019bee0` | `DistortionMgr_Reset` | at level start (`InitLevel`): destroys every effect and clears the slots | confirmed (code) |
| `0x0019bf50` | `DistortionMgr_DestroyAll` | also at level unload | confirmed (code) |
| `0x0019c030` | `DistortionMgr_Fits(mgr, size)` | the current heap has more than `size` free | confirmed (code) |
| `0x0019c070` | `DistortionMgr_StartHeat(cell, a, b, mgr, pos, cols, rows)` | a heat distortion in a free slot (−1 when none or no room): fires (`SubFadeFlame_Init`, `FireParticle_SetUpByName`: cell = size × 0.1, `size + 5` × `size + 8` cells) and `0x0021adf8` | confirmed (code) |
| `0x0019c1d0` | `DistortionMgr_StartWave` | the heat wave, if its slot is free: `Car_DoExplode` (cell 0.4, 16 × 10 cells, 0.7 m above the car) | confirmed (code) |
| `0x0019c328`, `0x0019c398` | `DistortionMgr_StopHeat(mgr, i)`, `DistortionMgr_StopWave` | destroy one (fires going out) | confirmed (code) |
| `0x0019c3f0` | `DistortionMgr_SetHeatPosition(mgr, pos, i)` | moves heat *i* | confirmed (code) |
| `0x0019c438` | `DistortionMgr_Render` | device slot `+0x120`: each live effect updates and draws; finished ones are destroyed | confirmed (code) |
| `0x0019c5e8`, `0x0019c608` | `HeatDistortion_Construct`, `HeatDistortion_Destruct` | the vtable, not started and not finished | confirmed (code) |
| `0x0019c638` | `HeatDistortion_Init` | slot `+0x10`: position, cell size, grid size, the weights; offsets are clamped to `[1/2048, 0.6255]` | confirmed (code) |
| `0x0019c740` | `HeatGrid_BuildIndices` | two triangles a cell into `0x005ff7a0`; returns the index count | confirmed (code) |
| `0x0019c7f0` | `HeatDistortion_BuildWeights` | slot `+0x20`: each vertex's strength from squared sines across and along the grid (`0x0050cf1c` = 0.8): strongest in the middle, fading to the edges | confirmed (code); the shape inferred from the formula |
| `0x0019cac0` | `HeatGrid_PlaceVertices` | slot `+0x28`: the vertices in the camera-facing plane, centred on the position | confirmed (code) |
| `0x0019cde0` | `HeatDistortion_SetPosition` | the position (axes converted) | confirmed (code) |
| `0x0019ce08` | `HeatGrid_ProjectCorners` | the grid's corners on the screen | confirmed (code) |
| `0x0019cf50`, `0x0019d5a0` | `HeatGrid_SetBaseUv`, `HeatGrid_Uv` | a vertex's resting and current texture coordinates | confirmed (code) |
| `0x0019cf90` | `HeatGrid_MapToScreen` | slot `+0x30`: resting coordinates spread over the projected rectangle (screen 1,024 × 512; 512 across when flag `0x02` is set) | confirmed (code) |
| `0x0019d1b0` | `HeatGrid_ClampUv` | to `[+0x32fc, +0x3300]` | confirmed (code) |
| `0x0019d1e0` | `HeatGrid_Shimmer(amount, fx)` | slot `+0x38`: a shared phase grows by 0.5 a frame; each vertex moves by sin and cos of it × `amount` × its weight × a random ±0.9 (`0x0019d318`) | confirmed (code) |
| `0x0019d318` | `HeatGrid_RandomSign` | ±0.9 (`0x0050cf3c`) for one vertex | confirmed (code) |
| `0x0019d390` | `HeatDistortion_Update` | slot `+0x18`: faces the camera, rebuilds the grid and shimmers by 0.0025 | confirmed (code) |
| `0x0019d5c8` | `HeatGrid_Draw` | the coordinates into the device's distortion mesh and one indexed triangle list | confirmed (code) |
| `0x0019d6e8`, `0x0019d728` | `HeatWave_Construct`, `HeatWave_Destruct` | the wave's vtable over the heat's | confirmed (code) |
| `0x0019d750` | `HeatWave_Init` | as above with 0.1 m, the ring's centre cell (`cols / 2`, given row) | confirmed (code) |
| `0x0019d9d0` | `HeatWave_Update` | faces the camera and rebuilds; while `0x0050cf4c` is set, steps the ring | confirmed (code) |
| `0x0019dbe8` | `HeatWave_InRing(radius, fx, col, row)` | the cell is within the radius of the centre | confirmed (code) |
| `0x0019dc50` | `HeatWave_StepRing` | the radius grows 0.3 cells a frame; a cell it reaches gets strength 5, which falls 0.3 a frame while the cell shimmers (0.005); once the radius passes `cols + rows` the wave is finished | confirmed (code) |

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00419da0` | `ThrowArcs_RenderAll` | from `WorldManager_Render`: draws each player's throw arc (game state `+0x5740 + 0x30 × p`) | confirmed (code) |
| `0x0041d728` | `Cfg_SetOutdoorMode` | `CfgSetOutdoorMode`: `+0x3e4` | confirmed (code) |

## Coney's implementation

First pixels (2026-10-04), in `src/platform/` and `src/graphics/`:

- `RenderEngine` (`src/platform/render_engine.h`) starts librw, built for its GL3 platform with SDL3. librw's GL3
  device creates the window and an OpenGL 3.3 core context itself, so the engine owns the window and the event loop
  gets a non-owning `Window`. A run-time NULL backend (`--headless`) installs librw's NULL device instead, for CI and
  tests: no window, no GPU. It implements `graphics::RenderDevice` (begin a frame cleared to a colour, present),
  which game modes call as the original's modes call the device (from their `render()`, see below); the idle mode
  clears and presents every frame. There are no device cameras yet: one camera covers the window.
- **The frame rate: a decoupled loop, Coney's choice.** The original runs one fixed 1/30 s step and one present per
  frame, held to 30 frames a second by its flip every second vertical blank ([the frame rate](#frame-rate)). Coney
  keeps the step and frees the display rate: the simulation always advances in steps of exactly `0x960000` ticks,
  and rendering runs once per real frame, blended between the last two steps
  ([Update and render](../guides/conventions.md#update-and-render)). The platform's frame pacer
  (`src/platform/frame_pacer.h`) measures each frame with SDL's nanosecond clock and holds frames to `--fps-cap`
  with precise sleeps; core's `FrameClock` (`src/core/frame_clock.h`) adds the real time to an exact integer
  accumulator (millionths of a tick, so 10 s at any rate are exactly 300 steps) and says how many steps to run and
  the alpha to render with. `--vsync on|off` picks librw's present flag: `FLIPWAITVSYNCH` (swap interval 1) or 0.
  The default is no cap with vsync on: as many frames as the display shows, each blended, the game at its fixed
  speed. `--fps-cap 30` is the original's rhythm: lockstep, one step and one frame, nothing blended.
    - **Slow frames:** a frame is credited with at most four steps (133 ms) of real time, and the rest is dropped, so
      below 7.5 frames a second the game slows down, as the original slows down when a frame runs long, rather than
      running ever more steps to catch up; a long gap (a dragged window, a debugger) moves the game on by four steps,
      not by the gap. The original's level clock catches up by up to 40 ms a frame ([Boot](boot.md#timers)); Coney's
      fixed step needs whole steps, and four keeps full speed at any usable rate.
    - **What it costs:** a blended frame shows the world up to one step (33 ms) behind the newest simulated state, and
      input is read once per step, so it is quantised to 30 a second as in the original.
    - **Test mode** runs lockstep with no clock (`--headless`, `--frames`, `--input-script`, `--screenshot`), so the
      screenshots and runs of the tests are the same as before the loop was split, pixel for pixel.
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
  perspective projection with the default mode's view window, 0.725 × 0.5 (aspect 1.45) × the view scale, which takes
  a point or a size of overlay-camera space to logical pixels (`project`, `projectSize`; `unproject` for Coney's
  tools). GUI 0 and 1 land at 5.2 % and 94.8 % across and 4.5 % and 95.5 % down, and the main menu's button glyph
  where PCSX2 shows it; tests pin both. Coney computes the projection itself rather than rendering through a librw
  camera, and draws the result as 2D quads. The 16:9 aspect (1.6667, scale 1.1) is there for the legal screen's `_w`
  pictures; the progressive modes are not offered.
- `TextureDictionary` (`src/platform/texture_dictionary.h`) reads a dictionary with librw after
  `graphics::inspectTexDictionary` has checked the stream, and converts it to RGBA images (any backend) or to OpenGL
  textures (`Raster::convertTexToCurrentPlatform`). The chunk readers for `0x0B` and `0x2A` push it as `0x0B`
  ([Chunk system](chunk-system.md#coneys-implementation)); sector atomics files and world streams are read at their
  fixed offsets by `loadTextureDictionaries`. Dictionaries are not kept per resource yet.
- **Texture lookup by name** (`src/platform/texture_lookup.h`): every dictionary of a loaded streamed world and its
  parts is registered, newest first, and librw's find callback searches them all, first match winning, with no file
  fallback ([Finding textures by name](#texture-lookup)). librw itself searches only the current dictionary and makes a
  dictionary of its own current at start-up, so Coney clears the current dictionary, as the game never leaves one set.
  The newest-first order is still RenderWare's inferred one (open question below).
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
  [GUI](gui.md#coneys-implementation)) and draws the sheet's first rectangle, the top 512 × 384 of the texture, on
  black for 5,000 ms, sized as [Placement](#first-screen) works out (`legalScreenQuad`, from the mode's factors and the
  overlay camera): 1.068 × 1.011 of the logical screen, centred, so the picture slightly overfills it as the
  original's does. A test pins the 1.068 × 1.011. On the NTSC-U disc it shows correctly and gives way to the
  memory-card check after frame 150.

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[disc]"` with `CONEY_DISC` set reads 20,314
dictionaries (from 3,016 chunk containers, 1,911 sector atomics files and 159 world streams) holding 42,211 textures;
42,206 are read and converted to RGBA images, the 5 above are left out, no entry fails. By format: 32,333 `PAL8`,
9,132 `PAL4`, 209 `PAL8` with mipmaps and 532 `PAL4` with mipmaps, all with 32-bit palettes and raster layout version
2.

What is still to do:

- A device object that owns librw's engine start-up (with the HAnim, Skin, MatFX and world plugins; PTank for
  sprites) and the cameras of [the device object](#device-object): main, per-viewport, overlay and effects, sharing
  one frame and Z buffer.
- Clear to the background colour (white until a level sets one; the legal screen clears to black) with Z.
- Texture dictionaries read with librw from the chunk stream (`0x0B`, `0x2A`, world streams, sector atomics) and kept
  per resource (the lookup by name exists, above; the chunk readers' dictionaries are not registered in it yet).
- PS2 native textures: 4- and 8-bit palettised, 32-bit palettes, power-of-two sizes up to 512; check alpha scaling.
- Drawing the streamed world: the readers for `0x3F0` and `0x3F1` and the decoder of the world atomics' PS2 geometry
  exist, and the world viewer streams and draws the `s` and `d` worlds
  ([The streamed world](world.md#coneys-implementation)); the level world, PVS and occluders do not.
- The 16:9 option: a 16:9 logical screen shape, the overlay view-window scale 1.1 and the `_w` legal screens.

The implementers' TODOs from the runtime answers (2026-10-04) are done: the overlay camera's view window is
0.725 × 0.5 and the legal screen overfills the screen as above; its colour was already white.

Still for the analysts:

- **Names for device slots `+0x90` and `+0x98`:** the `@orig` tags call them `RwDevice::GuiToOverlay`
  (`0x00195238`) and `RwDevice::GuiWidthToOverlay` (`0x00195330`) until the research database names them.

## Open questions

- **Exact legal-screen placement** (answered): it overfills the screen by 6.8 % across and 1.1 % down, centred
  ([Placement](#first-screen)).
- **Mode flag `0x02`.** Read as PAL from the 24.5 frames-a-second threshold and the `_euro` screen; nothing that sets
  it has been found (speculative).
- **What the game plugins hold** (answered for `0x3F1`): see [The streamed world](world.md#sector-plugin). For `0x3F0`
  the first float scales the packed vertex positions; what the second scales is open
  ([The streamed world](world.md#atomic-plugin)).
- **The overlay world** (`ResourceManager + 0x9034`) rendered before the sprites. The sort order of the queued
  PTanks is answered on [GUI](gui.md#draw-order): ascending key; that the 2D key is the creation depth is inferred.
- **librw and PS2 alpha:** does librw's PS2 native texture reader scale palette alpha from 0-128 to 0-255?
- **How the blur slot `+0x108` draws** (answered): see [Motion blur](#motion-blur). The previous frame is the other
  frame buffer, not a capture (answered, confirmed (runtime)).
- **Texture dictionary list order** for name lookups (newest first is RenderWare's usual behaviour; not read here).
- **The remaining slots**: `+0x148`
  (`0x004dee48`), `+0x180` (`0x004e3820`), and the byte `+0x448`.
- **Runtime confirmation** of the two-vertical-blank flip (partly answered): PCSX2 shows 30 frames for 60 vertical
  blanks ([Presenting](#frame-rate)); a watch on `0x005970f9` and `0x0059708b` through a slow frame (to see the
  20-a-second and the tearing cases) is still to do.
- **Display brightness.** (The output set-up is now read in full: [The PS2 render driver](ps2-render.md#video-output);
  it does not darken.) In PCSX2's screenshots the white of the legal image and of `big_font` text both come out
  at about 178 of 255 (and the menu's grey and red text at the same 70 % of their vertex colours), so the whole
  picture is about 70 % bright. **Not `PMODE`** (partly answered, 2026-10-04): the game writes the GS output
  registers from a display-buffer record in memory (`0x004aa748`, called at the vertical blank by `0x0048bdb0`), and
  at the front end that record holds `PMODE` = `0x8067`: both read circuits on, mixed with the fixed alpha `0x80`
  (`MMOD` = 1), `SMODE2` = 1, the two circuits' frame buffers one address step apart (the usual two-circuit
  anti-flicker set-up; inferred). A half-and-half mix of two copies of the picture keeps its brightness, so the
  70 % is in the drawn colours or in PCSX2's capture. confirmed (runtime) for the values, read from EE memory
  over PINE at `0x0070f610`. **Answered for text** (confirmed (runtime), PCSX2 GS dumps of `level99`): the HUD text
  sprites reach the GS with vertex colour `0x59` (89), RenderWare's halving of the game's grey 178 (`0x005fd310`,
  [GUI](gui.md)), and the GS's texture modulate gives 89 × 255 / 128 = 177 on white texels, the 178 measured. So
  the dimness is the game's own colours and the capture is faithful: a *Faithful* grade needs no brightness curve,
  only the line averaging above. That the legal image is drawn the same way (a 178 grey vertex colour) is
  inferred.
