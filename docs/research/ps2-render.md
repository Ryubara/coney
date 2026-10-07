# The PS2 render driver

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static reading in Ghidra
(2026-10-07), and the game running under PCSX2 2.9.94 (EE memory read over PINE, in `level99` from a copy of the
owner's quick-save slot 1).

## Purpose

How RenderWare's PS2 driver, linked into the game, turns the game's drawing into Graphics Synthesizer (GS) state: the
video mode and how the picture is sent to the TV, how GS memory is split, and what each RenderWare render state becomes
in GS registers. [Graphics](graphics.md) covers the game's device object, the cameras, the frame and the flip; [The
streamed world](world.md#pipeline-unit) covers the game's own four PS2 pipelines; the GS state measured per pass of a
frame is [Rendering](rendering.md#passes). Coney draws through librw on a PC GPU, so most of this is
background. What changes the picture is listed under [What a reimplementation must
keep](#what-a-reimplementation-must-keep).

## Original structure

The driver is part of the RenderWare 3.7 library block ([Source map](source-map.md#middleware)); it has no path
strings. RenderWare's standard ids and render-state ids are those of the public RenderWare 3.7 headers, and the GS
register names are Sony's; matching the code's numbers to those names is inferred, the numbers themselves are
confirmed (code). Names are ours.

### Entry points {#driver}

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0052f618` | the driver's `RwDevice` record | gamma 1.0, system handler, Z near 65,535.0 and far 0.0, render-state set and get; the immediate-mode slots are 0 | confirmed (code) |
| `0x00497468` | `Sky_System` | the device system handler: open (`0x004959e0`), start (`0x00496758`), stop, register, 90 video modes (`0x0052ec38`), use-mode (`0x004958a8`), the standard functions, texture memory size, finalise start (`0x00495cd0`), maximum texture size 1024 | confirmed (code) |
| `0x0052f538` | standard-function table | 28 `{id, function}` pairs, copied into RenderWare's table by request 11 (below) | confirmed (code) |
| `0x00496758` | `Sky_Start` | resets the GS for the chosen mode, builds the display records and the frame, Z and texture registers, sets up the texture cache | confirmed (code) |
| `0x004aa180` | `Gs_ResetGraph(mode, interlace, video, field)` | GS reset and `SetGsCrt`; keeps the three values at `0x0052fa10` | confirmed (code) |
| `0x004aa748` | `Gs_PutDisplayRecord(record)` | writes `PMODE`, `SMODE2`, `DISPFB`, `DISPLAY` and `BGCOLOR` (or the circuit-1 set) from a 0x28-byte record | confirmed (code) |
| `0x0048e250` / `0x0048ece8` | `Sky_RenderStateSet` / `_Get` | RenderWare render states to GS registers ([Render states](#render-states)) | confirmed (code) |
| `0x0048cf68` | `Sky_OpenGifPacket(tag, qwords)` | reserves room in the DMA packet for a GIF tag and `n` register writes (A+D); every state change goes through it | confirmed (code); role inferred |
| `0x004a3600` | `TexCache_Init(start, size)` | the texture cache over the GS memory left after the frame and Z buffers | confirmed (code) |
| `0x004a3b48`, `0x004a4690` | `TexCache_Find`, `TexCache_Upload` | look up and upload a texture's raster before a draw; called by render state 1 and by the game's pipelines | confirmed (code) for the callers; roles inferred |

Standard functions (RenderWare 3.7 ids), all confirmed (code) at the table:

| Id | Function | | Id | Function |
| --- | --- | --- | --- | --- |
| 1 camera begin update | `0x0048f2f0` | | 15 raster lock | `0x004943f0` |
| 2 RGB to pixel | `0x0049f428` | | 16 raster unlock | `0x00494c60` |
| 3 pixel to RGB | `0x0049f520` | | 17 raster render | `0x00499e38` |
| 4 raster create | `0x00491750` | | 18 raster render scaled | `0x0049b778` |
| 5 raster destroy | `0x004937c0` | | 19 raster render fast | `0x0049ce08` |
| 6 image get raster | `0x0049e038` | | 20 show raster | `0x00498890` ([the flip](graphics.md#frame-rate)) |
| 7 raster set image | `0x0049f608` | | 21 camera clear | `0x0048fd00` |
| 8 texture set raster | `0x00498928` | | 22 hint render front to back | `0x00498be0` |
| 9 image find raster format | `0x0049deb0` | | 23 / 24 lock / unlock palette | `0x00498938` / `0x004989f0` |
| 10 camera end update | `0x004987d8` | | 25 native texture size | `0x00498b50` |
| 11 set raster context | `0x0049de78` | | 26 / 27 native texture read / write | `0x004954f0` / `0x00495400` |
| 12 sub-raster | `0x00493418` | | 28 raster mip levels | `0x00498af0` |
| 13 / 14 clear rectangle / clear | `0x00499350` / `0x0049de30` | | | |

Ids missing from the table get the stub `0x00498be8`. The frame flip (`0x0048bdb0`, `0x0048b708`, `0x0048c508`,
`0x0048c8f0`) is on [Graphics](graphics.md#frame-rate).

## Data

### Video modes {#video-modes}

The 90 modes are 24-byte records `{width, height, depth, flags, refresh, raster format}` at `0x0052ec38`
([Graphics](graphics.md#video-mode) for how the game picks one). The game uses **mode 82**: 640 × 448, 32-bit,
flags `0x203`, 60 Hz, format `0x500` (current mode `0x00597130` = `0x52`, confirmed (runtime)). Its neighbours differ
only in flags: 72 is `0x101`, 77 is `0x3`; 87 is the progressive 640 × 480. The 90 records use five flag sets: `0x1`
(22 modes), `0x3` (20), `0x203` (20), `0x5` (20, all 224 or 256 lines high) and `0x101` (8, all 448 or 512 high).
`Sky_Start` (`0x00496758`) resets the GS (`0x004aa180(0, interlace, video mode, frame mode)`) by flag: `0x2` (without
`0x4`) interlaced in **field** mode, `(0, 1, mode, 0)`; `0x4` and `0x100` interlaced in **frame** mode, `(0, 1, mode,
1)`; neither, non-interlaced, `(0, 0, mode, 1)`. `0x100` also halves two display factors (bytes `0x00710080` /
`0x00710081` = 1 instead of 2) and takes the display width from `0x00596e50`. `0x200` adds the second read circuit
(below). Confirmed (code) for the branches; reading the reset's arguments as interlace and field/frame is inferred
from the GS's `SMODE2`. So `0x4` is a half-height buffer shown in both fields, and `0x100` a full-height buffer
read a line in two per field: neither has the anti-flicker blend of mode 82.

### Display records {#display-records}

Two records of five GS registers, one per frame buffer, at `0x0070f610` and `0x0070f638`, a copy of both at `+0x350`
for the other field, and the circuit-1 originals at `0x0070f940`. The vertical-blank handler writes the record of
the displayed buffer for the current field (`0x004aa748`, picked by the field bit of `CSR`). Read in a level,
confirmed (runtime); the field layout is the GS's:

| Register | Value | Meaning |
| --- | --- | --- |
| `PMODE` | `0x8067` | both read circuits on, blended with the fixed alpha `ALP` = `0x80` |
| `SMODE2` | `0x1` | interlaced, `FFMD` = 0: field mode |
| `DISPFB2` | `0x00000800_00001400` (buffer 1: `FBP` `0x08c`) | 640 wide (`FBW` 10), 32-bit, starting one line down (`DBY` = 1) |
| `DISPLAY2` | `0x001be9ff_0183227c` | `DX` 636, `DY` 50, `MAGH` 3 (×4), `DW` 2560, `DH` 446 (447 lines) |
| `DISPFB1` / `DISPLAY1` (originals) | `DBY` = 0, `DH` 447 (448 lines) | the same frame buffer, from its first line |

`Gs_ResetGraph`'s record `0x0052fa10` holds interlace 1, video mode 2 (NTSC) and field 0, confirmed (runtime).

### GS register shadows {#gs-shadows}

The driver keeps the GS drawing registers it changes in memory and sends the whole register on each change.
Confirmed (code) for the roles; the values were read mid-frame in a level, confirmed (runtime):

| Address | Register | Value read | |
| --- | --- | --- | --- |
| `0x00596dd8` | `ZBUF_1` | `0x00000001_00000118` | Z at block `0x118`, write masked at that moment (format: [Z](#z-buffer)) |
| `0x00596de0` | `FRAME_1` | `0x000a008c` | the back buffer, 640 wide, 32-bit |
| `0x00596de8` | `TEST_1` | `0x0005140a` | see [Alpha test](#alpha-test) and [Z](#z-buffer) |
| `0x00596df8` | `FOGCOL` | `0x00050c0c` | fog colour (12, 12, 5) |
| `0x00596e00` | `CLAMP_1` | `0` | wrap in both directions |
| `0x00596e08` | `TEX1_1` | `0x0000000f_c0000060` | linear filtering; `K` = −4.0 for that texture |
| `0x00596e10` | `ALPHA_1` | `0x44` | `(Cs − Cd) × As + Cd` |
| `0x00596e20` | (primitive bits) | | `0x10` textured, `0x40` alpha blend, `0x20` fog, `0x08` Gouraud |

## Behaviour

### The picture on the TV {#video-output}

`Sky_Start` resets the GS with interlace on and **field mode** (`Gs_ResetGraph(0, 1, 0, 0)`; the NTSC value is
filled in by the reset). Because mode 82 has flag `0x200`, it turns on **both read circuits over the same frame
buffer**: one from its first line, the other from its second (`DBY` = 1, one line shorter), mixed half and half by
`PMODE`. Confirmed (code) at `0x00496758` and confirmed (runtime) by the records above.

In field mode the GS shows every other line of the 448-line frame buffer in each field (the GS's documented
behaviour; inferred). With the second circuit one line lower, **every line on the TV is the average of two adjacent
lines of the frame buffer**: a vertical two-tap filter, the usual PS2 anti-flicker. The picture is slightly soft
vertically and thin horizontal lines do not flicker between fields. It does not change the brightness: the two copies
are the same picture. The 70 % brightness seen in PCSX2 is the game's own vertex colours
([Graphics](graphics.md#open-questions), confirmed (runtime) for text).
The same registers read from a frame's GS dump are in [Rendering: Output](rendering.md#output).

### GS memory {#gs-memory}

The GS has 4 MB (`0x100000` 32-bit words). `Sky_Start` places the two 640 × 448 32-bit frame buffers at blocks `0` and
`0x08c` and the Z buffer (24-bit Z in 32-bit words) at `0x118`, and hands the rest to the texture cache: from word
`0xd2000`, **753,664 bytes**, through `TexCache_Init` (`0x004a3600`). The driver reports `(0x100000 − end) × 4` as the
texture memory size (request 12). Confirmed (code); `0x00597144` = `0xd2000` confirmed (runtime). So textures do not
stay in GS memory: the driver uploads each one when a draw needs it and evicts others to make room (`TexCache_Find` and
`TexCache_Upload`, called before each material's draw).

**Eviction** (`TexCache_Allocate`, `0x004a30c8`, the cache's placement function when it is on; confirmed (code)): the
cache is a **ring**. A table of `{raster, GS address, size}` entries (sizes rounded up to 2,048 words) records what
is resident, from the oldest (`0x0052f948`) to the newest (`0x0052f944`). A raster that is not resident is placed
right after the newest one, or back at the start of the cache when it does not fit before the end, and every older
entry whose range it overlaps is dropped (its raster loses its cache link). So eviction is first in, first out, by
address, with no use counts. Two entries are protected: the rasters bound to context 1 and context 2 for the current
draw (`0x0052f96c`, `0x0052f970`); when the new raster would overwrite one of them, it is not placed. A raster
flagged locked (raster extension `+0x17`) is never placed by it. The upload itself (`TexCache_UploadRaster`,
`0x004a2910`) sends every mip level and the palette, or a raster's pre-built packets.

**The cache does not hold a frame.** Confirmed (runtime), quick-save slot 1 in `level99`'s street (scenario
`render_texture_uploads`, hook `texture-upload` on every placement): standing still, every frame (one per two ticks,
30 per second) placed and uploaded **376-377 rasters, 300 of them different, about 1.08 million words (4.3 MB)**,
nearly six times the cache; with the camera turning (right stick 60 %), 190 to 436 a frame. So nearly every texture is
sent to the GS again in every frame, some twice. Nothing of this shows on screen; a PC renderer keeps its textures.

### Render states {#render-states}

`Sky_RenderStateSet` (`0x0048e250`) handles these RenderWare 3.7 states; anything else returns 0 (not supported).
Each change updates the register's shadow and sends it in a two-quadword GIF packet. Confirmed (code):

| State | Values handled | GS effect |
| --- | --- | --- |
| 1 texture raster | a raster or none | textured on or off; makes sure the raster is in the cache; alpha test on when the raster's format has alpha (any but 888), else off |
| 2 / 3 / 4 texture address (both, U, V) | 1 wrap, 3 clamp | `CLAMP_1` `WMS` / `WMT` repeat or clamp; **2 mirror is refused** |
| 5 texture perspective | on only | clears the `FST` bit (perspective-correct coordinates); off is refused |
| 6 Z test | off, on | `TEST_1`: `ZTST` always or greater-or-equal ([Z](#z-buffer)) |
| 7 shade mode | 1 flat, 2 Gouraud | the primitive's `IIP` bit |
| 8 Z write | off, on | `ZBUF_1` `ZMSK`; turning it on with Z test off sets `ZTST` always |
| 9 texture filter | 1-6 | `TEX1_1` ([Textures](#texture-states)) |
| 10 / 11 source / destination blend | 1, 2, 5, 6, 7, 8 | `ALPHA_1` from a table ([Blending](#blending)) |
| 12 vertex alpha | on, off | alpha blending (`ABE`) on when vertex alpha is on or the texture has alpha |
| 14 fog | on, off | the primitive's `FGE` bit |
| 15 fog colour | `RwRGBA` | `FOGCOL` (red and blue swapped into the GS order) |
| 16 fog type | 1 linear only | |
| 20 cull mode | 1 none, else | kept for the pipelines, which cull in VU1 code |
| 29 / 30 alpha test function / reference | 1-8 / 0-255 | `TEST_1` `ATST` / `AREF` = reference × 128/255 (`0x005969b8` = 0.50196) |

### Alpha test {#alpha-test}

`TEST_1` starts as `0x140b` (`0x00496758` sets `0x0070f6e0 |= 0x140b`): **test on, pass when alpha ≥ `0x40`**, and
`AFAIL` = 1, *frame buffer only*: a pixel that fails is still drawn and blended, but **does not write Z**. Nothing calls
render states 29 or 30: render states are set through the engine's pointer `0x0070ad38`, and the 31 functions that read
it (game and RenderWare code alike) set only states 1, 2, 6-12, 14-17 and 20. The driver's other `TEST_1` writers do not
change it either: `0x0048f0f8` replaces the low 16 bits in its case 3, but its four callers (`0x0040d0a8`,
`0x00195400`) only pass case 4, and the material upload `0x00477858` (through the table at `0x0051d8d4`) sets or clears
`ATE` alone. So this stays for the whole game. Confirmed (code), by decompiling every reader of the pointer and every
writer of `0x00596de8`. What changes per draw is only the enable bit (`ATE`), and
the rule for it depends on who draws, by the raster format nibble at raster `+0x23` (RenderWare's 1 = 1555, 5 = 8888, 6
= 888, and the others): render state 1 and the dual-texture and environment-map uploads (`0x004275d0`, `0x00428410`)
turn it on for any format but 888; the plain world upload and the second character upload (`0x004290d8`, `0x00426430`)
only for 1555 and 8888; the first character upload (`0x004252c8`, the character materials' pipeline `0x30081`) always.
Confirmed (code).

`AREF` `0x40` on the GS's alpha scale, where `0x80` is 1.0, is **0.5**. So for a cut-out texture (foliage, fences,
hair), the opaque half writes Z and hides what is drawn after it, while the transparent half is blended over what is
already there without occluding later draws. Which draws come later decides how the edges look (the pass order is on
[The streamed world](world.md#a-frame)). The frame dumps agree: every first pass uses context 1 with
this test, and the second passes, sprites and text use context 2's own `TEST_2`, off or `NOTEQUAL` 0
([Rendering](rendering.md#shared-state), confirmed (runtime)).

### Blending {#blending}

`ALPHA_1` comes from a 6 × 6 table at `0x0052f4a8`, indexed `[destination][source]` over the factors ZERO, ONE,
SRCALPHA, INVSRCALPHA, DESTALPHA and INVDESTALPHA (RenderWare's 1, 2, 5, 6, 7, 8; the colour factors 3 and 4 are not
handled). Pairs the GS cannot express are −1 and the state change is refused. Confirmed (code):

| Destination \ source | ZERO | ONE | SRCALPHA | INVSRCALPHA | DESTALPHA | INVDESTALPHA |
| --- | --- | --- | --- | --- | --- | --- |
| ZERO | 0 | `Cs` | `Cs·As` | `Cs − Cs·As` | `Cs·Ad` | `Cs − Cs·Ad` |
| ONE | `Cd` | `Cs + Cd` (`FIX` = `0x80`) | `Cs·As + Cd` | refused | `Cs·Ad + Cd` | refused |
| SRCALPHA | `Cd·As` | `Cd·As + Cs` | refused | `(Cd − Cs)·As + Cs` | refused | refused |
| INVSRCALPHA | `Cd − Cd·As` | refused | `(Cs − Cd)·As + Cd` | refused | refused | refused |
| DESTALPHA | `Cd·Ad` | `Cd·Ad + Cs` | refused | refused | refused | `(Cd − Cs)·Ad + Cs` |
| INVDESTALPHA | `Cd − Cd·Ad` | refused | refused | refused | `(Cs − Cd)·Ad + Cd` | refused |

20 of the 36 pairs exist. The usual game pair, SRCALPHA /
INVSRCALPHA, is `0x44`, the value read in a level. Additive (ONE / ONE) and its alpha-weighted form (SRCALPHA / ONE)
are exact; the GS clamps or wraps the sum by `COLCLAMP` (not read here).

### Z {#z-buffer}

The Z buffer is 24-bit (`PSMZ24`, `0x31`), or `PSMZ16S` (`0x3a`) when the Z depth `0x00596e4c` is 16: `Sky_Start`
builds `ZBUF_1` at `0x0070f680` that way and copies it to the shadow. Confirmed (code) at `0x00496758`, and confirmed
(runtime) by a GS dump reading `ZBUF_1` = `0x31000118`, with every Z below 2^24 (the sky near 16,775,500, the HUD at
8,388,607). The driver's `RwDevice` record gives Z near 65,535 and far 0, so the near value must be raised to the
24-bit range at start (inferred; the writer is not found). Either way **larger Z is nearer** and "Z test on"
is `ZTST` greater-or-equal. "Z test off" keeps the test enabled (`ZTE` = 1, which the GS requires) with `ZTST` always.
Z write is the `ZMSK` bit of `ZBUF_1`. Confirmed (code) at `0x0048e250` and `0x0052f618`; `TEST_1` = `0x5140a` read in
a level (Z test on, greater-or-equal).

### Textures {#texture-states}

Filtering (render state 9) sets `TEX1_1`'s `MMAG` / `MMIN`, confirmed (code):

| RenderWare filter | `MMAG` | `MMIN` |
| --- | --- | --- |
| 1 nearest | nearest | nearest |
| 2 linear | linear | linear |
| 3 mip nearest | nearest | nearest, nearest mip |
| 4 mip linear | linear | linear, nearest mip |
| 5 linear mip nearest | nearest | nearest, linear between mips |
| 6 linear mip linear | linear | linear, linear between mips |

Read against RenderWare's names, 4 and 5 look swapped (inferred; the effect is small). The mip bias `K` and `L` in
`TEX1_1` come from each texture's raster (the PS2 sky mipmap extension `0x110`, [Graphics](graphics.md#plugins));
`K` = −4.0 was read for one texture. Addressing is wrap or clamp per axis; mirrored textures cannot be drawn.

Textures arrive as RenderWare PS2 native textures (read by standard 26, `0x004954f0`; the formats are on
[Graphics](graphics.md#texture-formats)), already laid out for the GS, so the driver uploads them as they are.

### Fog {#fog}

Fog is per vertex: the pipelines' microcode writes the fog value and the primitive's `FGE` bit turns it on (render
state 14). The colour is `FOGCOL` (render state 15, the device's background colour, [Graphics](graphics.md#device-vtable)
slot `+0x48`), and only linear fog exists (state 16). Confirmed (code) for the state handling.

**The curve**: when the device sets up the cameras (`RwDevice_SetUpCameras`, `0x00195400`) it gives the main camera
a fog distance of **far × the fog start** ([Graphics](graphics.md#device-object) `+0x444`, 0.5 unless a level script
calls `SetFogDistance`), confirmed (code) at `0x001954f0`. The vertex fog value is then linear in the camera depth
`w`: 255 up to that distance, 0 at the **far clip**, `255 × (far − w) / (far − far × start)` between. Confirmed
(runtime) twice from GS dumps, with a least-squares fit of the value against `w` (= 1 / `Q`): `level99` (far 115, ends
57.3 m and 115.1 m, [The streamed world](world.md#fog)) and the title screen (far 150, ends 74.7 m and 151.2 m over
1,998 partly fogged vertices of the world and the Wonder Wheel, residuals about 1, [The front end](rendering.md#front-end)).
The world's own pipelines and the objects' share it: an object's atomics get the same game pipelines as the streamed
world (`ObjectModel_SetupAtomic`, `0x001807cc` → `Atomic_AssignGamePipelines`), confirmed (code). How the microcode
computes the value was not read.

### Packets {#packets}

Every state change and draw goes into the current DMA packet (`0x0059707c`, the write pointer) as a GIF tag in A+D
mode followed by `{value, register}` pairs; `0x0048cf68` makes room and flushes when the buffer
(`0x1c0000` bytes, [Graphics](graphics.md#globals)) is full. The game's pipelines write their own VIF and GIF data
into the same packet ([The streamed world](world.md#ps2-world-geometry)). Confirmed (code) for the A+D writes; the
flushing was not read.

### Per-pipeline states {#pipeline-states}

The game's four PS2 pipelines ([The streamed world](world.md#pipeline-unit)) change state themselves in their upload
callbacks: the alpha-test enable per material ([Alpha test](#alpha-test)), `CLAMP_1` clamp from the material's
texture addressing (its U and V nibbles, 3 = clamp), the texture through the cache, and the material colour scaled to
the GS's range (`0.5/255` when textured, [The streamed world](world.md#pipelines)). They leave blending, Z and fog to
the render states set around them. Confirmed (code) at the four upload callbacks.

## What a reimplementation must keep {#what-a-reimplementation-must-keep}

- **The alpha test**: pass at alpha ≥ 0.5, and a failing pixel blends its colour but writes no Z. A PC renderer that
  discards below 0.5 (or writes Z for every drawn pixel) draws cut-out edges and their sorting differently.
- **The vertical anti-flicker** (optional, a look): each output line is the average of two adjacent rendered lines.
- **Blend modes**: only the six factors above exist, so a reimplementation needs nothing more.
- Not visible: the texture cache, the packets, GS memory, the display records.

## Coney's implementation

Coney draws with librw on the PC; none of this is implemented yet. librw's own alpha-test default and its Z
handling of failing pixels have not been compared with the above.

## Open questions

None left for the driver; the per-pass measurements are on [Rendering](rendering.md).
