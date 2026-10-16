# PS2 display and frame layers

How the PS2 version puts a frame on the television: the video mode, the buffers in GS memory, the blend, alpha-test,
depth and fog settings in force, the layers a viewer sees in front of or behind one another, and the two display
settings the player can change. Texture formats are on texture-dictionary and model data on model-format.

## Sources

| Source | What it gave |
| --- | --- |
| `SYSTEM.CNF` on the PS2 disc (57 bytes) | the declared video standard |
| `SLUS_212.15` (4,903,976 bytes on the disc) | the blend table, the option ranges, the aspect values |
| Ten PCSX2 2.9.94 savestates of the US disc | GS privileged and general registers, the frame's GS packets in EE memory, camera values |

All ten captures are in the opening Coney streets during the tutorial objectives (break into the store, steal a car
radio, mug someone, climb, jump across roofs, bust through the fence), on foot, at night, Video set to Normal. Values
below are the same in all ten unless a row says otherwise.

## Video mode and output

The disc's `SYSTEM.CNF` gives NTSC as its video mode. The measured output registers:

| Register | Value | Meaning |
| --- | --- | --- |
| SMODE1 | `0x0000000740814504` | NTSC timing |
| SMODE2 | `0x0000000000000001` | interlaced, field mode (each field reads every other line of the buffer) |
| PMODE | `0x0000000000008067` | both read circuits on, circuit 1 blended over circuit 2 with fixed alpha 0x80 (128 of 255) |
| DISPFB1 | buffer at page 0 or 140, width 640 (FBW 10), PSMCT32, DBX 0, DBY 0 | circuit 1 reads the shown buffer from line 0 |
| DISPFB2 | same buffer, DBY 1 | circuit 2 reads the same buffer one line lower |
| DISPLAY1 | DX 636, DY 50, MAGH 3, MAGV 0, DW 2559, DH 447 | a 640 x 448 picture, each pixel 4 video clocks wide |
| DISPLAY2 | DX 636, DY 50, MAGH 3, MAGV 0, DW 2559, DH 446 | the same window, one line shorter |
| BGCOLOR | 0, 0, 0 | black outside the picture |

Outcomes a viewer sees:

- The picture is 640 x 448 pixels, interlaced at NTSC rates.
- Every output line is a 50/50 mix (128/255) of two neighbouring rendered lines: a fixed vertical anti-flicker blur.
  A one-pixel horizontal line in the frame buffer appears on two output lines at about half strength.
- No PAL or progressive mode was seen in any capture.

## GS memory layout

GS memory is 4 MiB. Pages are 8 KiB (2,048 words); texture base pointers count 256-byte blocks.

| Item | Page | Byte offset | Size | Format |
| --- | --- | --- | --- | --- |
| Frame buffer A | 0 | `0x000000` | 1,146,880 bytes (640 x 448 x 4) | PSMCT32, width 640 |
| Frame buffer B | 140 | `0x118000` | 1,146,880 bytes | PSMCT32, width 640 |
| Depth buffer | 280 | `0x230000` | 1,146,880 bytes | PSMZ24 (24-bit depth held in 32-bit words) |
| Texture area | 420 onward | `0x348000` to `0x400000` | 753,664 bytes | texture base pointers seen from `0x3480` to `0x3fe0` |

The two frame buffers swap every frame: one is shown while the other is drawn. In the captures the shown buffer was B
six times and A four times, and the drawing registers always named the other one.

## Drawing area

| Register | Value | Meaning |
| --- | --- | --- |
| XYOFFSET (both contexts) | OFX 1728, OFY 1824 | the 640 x 448 area is centred on GS coordinate (2048, 2048) |
| SCISSOR (both contexts) | x 0 to 639, y 0 to 447 | drawing is cut to the buffer |
| COLCLAMP | 1 | blended colours clamp at 255 rather than wrap |
| DTHE | 0 | no dithering |
| PABE | 0 | the alpha blend applies to every pixel |
| FBA (both contexts) | 0 | the alpha written to the frame buffer is not forced |
| TEXA | TA0 0, AEM 0, TA1 0x80 | 24-bit texels read with alpha 0x80 |
| PRMODECONT | 1 | primitive attributes come from PRIM |

GS alpha runs from 0 to 0x80, where 0x80 means fully opaque.

## Render settings

### Blend modes

Every blended draw in the captures uses ALPHA `0x44`: result = (source - destination) x source alpha + destination,
the usual "source over" blend. The executable holds a 6 x 6 table of ALPHA values at `0x0052f4a8` (36 signed 32-bit
words, rows by destination factor, columns by source factor). It covers six factors: zero, one, source alpha,
inverse source alpha, destination alpha, inverse destination alpha. Cs and Cd are the source and destination colours,
As and Ad their alphas.

| Destination \ source | zero | one | source alpha | inverse source alpha | destination alpha | inverse destination alpha |
| --- | --- | --- | --- | --- | --- | --- |
| zero | 0 (`0x8a`) | Cs (`0x0a`) | Cs x As (`0x88`) | Cs - Cs x As (`0x02`) | Cs x Ad (`0x98`) | Cs - Cs x Ad (`0x12`) |
| one | Cd (`0x4a`) | Cs + Cd (`0x68`, FIX 0x80) | Cs x As + Cd (`0x48`) | not available | Cs x Ad + Cd (`0x58`) | not available |
| source alpha | Cd x As (`0x89`) | Cd x As + Cs (`0x09`) | not available | (Cd - Cs) x As + Cs (`0x01`) | not available | not available |
| inverse source alpha | Cd - Cd x As (`0x46`) | not available | (Cs - Cd) x As + Cd (`0x44`) | not available | not available | not available |
| destination alpha | Cd x Ad (`0x99`) | Cd x Ad + Cs (`0x19`) | not available | not available | not available | (Cd - Cs) x Ad + Cs (`0x11`) |
| inverse destination alpha | Cd - Cd x Ad (`0x56`) | not available | not available | not available | (Cs - Cd) x Ad + Cd (`0x54`) | not available |

- 20 of the 36 pairs exist; the other 16 hold -1. Asking for one of those 16 leaves the blend as it was.
- Plain additive (one, one) is the only pair that carries the fixed factor, `0x80` (1.0).
- The two colour factors (source colour, inverse source colour) have no entry.

### Alpha test

- The alpha reference is given on a 0 to 255 scale and lands on the GS at 128/255 of that value, so 128 becomes
  `0x40`: half of full alpha.
- Lit 3D geometry uses the test "alpha greater than or equal to 0x40" with fail mode FB_ONLY: a texel below half alpha
  still blends its colour into the frame but writes no depth.
- Overlay and in-world sprites in context 2 use "alpha not equal to 0" with fail mode KEEP: fully clear texels draw
  nothing.

### Depth

- The depth test is GEQUAL everywhere it is on: a larger depth value is nearer.
- The depth buffer is cleared to 0, the farthest value, so anything drawn onto a cleared pixel passes.
- Depth values are a scale over distance plus a shift. With the main camera's near 0.1 and far 115, a point at the
  near plane gets about 16,775,540 (just under 2^24), at 1 unit about 1,665,920, at 10 units about 154,960 and at the
  far plane about 1,680.
- Turning the depth test off gives the test "always"; turning depth writing off sets the depth mask.

### Settings seen in the frame

TEST values found in the captured frame packets, with what they draw:

| TEST | Context | Alpha test | On failure | Depth test | Drawn with it |
| --- | --- | --- | --- | --- | --- |
| `0x0005140b` | 1 and 2 | alpha >= 0x40 | colour only | GEQUAL | world, props, characters (by far the most common) |
| `0x0005140a` | 1 | off | - | GEQUAL | the sky dome and the full-screen colour wash |
| `0x00050000` | 2 | off | - | GEQUAL | the second texture layer on world surfaces |
| `0x0005000f` | 2 | alpha != 0 | keep | GEQUAL | sprites placed in the world (no depth write) |
| `0x0003140b` | 1 and 2 | alpha >= 0x40 | colour only | always | the radar disc |
| `0x0003000f` | 2 | alpha != 0 | keep | always | 2D overlay sprites |
| `0x0003140a` | 1 | off | - | always | the colour clear and the depth reset |

ZBUF is always page 280, PSMZ24; ZMSK 1 (no depth write) appears on the sky, the radar, the overlays, the in-world
sprites, the colour wash and a small group of see-through world strips.

### Texture filtering

| Filter setting | MMAG | MMIN | Used on |
| --- | --- | --- | --- |
| nearest | 0 | 0 (nearest) | - |
| linear | 1 | 1 (linear) | objects, characters, sky, radar, overlays |
| mip nearest | 0 | 2 | - |
| mip linear | 1 | 4 | - |
| linear mip nearest | 0 | 3 | - |
| linear mip linear | 1 | 5 | the streamed world's sectors |

- World sector textures carry MMIN 5 with L 0 and K around -4 to -5; most have MXL 0 (no mip levels), so they draw
  bilinear, while a few 4-bit textures carry MXL 3.
- Texture addressing is repeat (CLAMP 0) everywhere except the radar, which clamps on both axes (CLAMP `0x5`).

### Fog

| Item | Value in the captures |
| --- | --- |
| FOGCOL | (12, 12, 5) |
| Main camera far plane | 114.97 to 115.0 in the open streets, 79.63 in the alley capture |
| Main camera fog plane | always half the far plane (57.48 to 57.50; 39.81 in the alley) |
| Fogged | world, props and characters |
| Not fogged | sky, radar, overlays, colour wash |

The fog colour equals the clear colour, so distant world fades into the background with no seam.

## Frame layers

What a viewer sees, from the back of the picture to the front:

| Layer | What it is | Settings | Seen in front of |
| --- | --- | --- | --- |
| Background | the clear colour where nothing else draws | the level's fog colour (12, 12, 5), alpha 0x80; depth 0 | nothing |
| Sky | a 256 x 512 8-bit dome, a 512 x 256 8-bit alpha-tested layer and a small 32 x 16 piece | no fog; depth reset to 0 over the whole screen once the sky is down | the background only |
| World | streamed sectors, then objects and characters | fogged; depth tested and written; alpha >= 0x40 | sky and background |
| Second layers | grime and decals on world surfaces: a 128 x 128 8-bit texture over the same strip | context 2, blend `0x44` (ALPHA_2 also holds FIX 0x80), no alpha test, depth GEQUAL with write | the surface they lie on |
| See-through 3D | a few world strips and in-world sprites | depth tested, not written | whatever is behind them by depth |
| HUD | the radar disc (256 x 256 4-bit, clamped, vertex alpha 0x78 so about 94 % opaque) and the objective text and its box | depth test always, no depth write | all 3D, whatever its depth |
| Colour wash | one full-screen 640 x 448 untextured fan | colour (7, 20, 30), alpha 21 (about 16 %); no depth write | everything, the HUD included |

- Nothing in the sky ever hides world geometry, because the depth buffer is reset after the sky.
- The colour wash is the last primitive the GS holds in all ten captures, so it lies over the HUD too.
- Vertex alpha is 0x80 on opaque geometry; lower values on world strips make them partly see-through while they still
  write depth.

## Video option

The options menu has a Video entry with the values Normal and Wide. It changes camera values only; the output stays
640 x 448 interlaced.

| Value | Normal | Wide |
| --- | --- | --- |
| Camera aspect (all cameras) | 1.33333 (`0x3faaaa8f`) | 1.66666 (`0x3fd5551d`) |
| HUD aspect | 1.45 (`0x3fb9999a`) | 1.66666 (`0x3fd5551d`) |
| HUD scale | 1.0 | 1.1 (`0x3f8ccccd`) |
| View window (main camera) | 0.6371 x 0.4778 (measured) | 0.6371 x 0.3823 (from the aspect) |
| Horizontal view | 65 degrees | 65 degrees |
| Vertical view | 51.1 degrees | 41.8 degrees |

The field of view is horizontal: the camera holds 65 degrees across, and the aspect divides the width of the view
window to give its height. With Wide the picture keeps the same width of view and shows less height, which a 16:9 set
then stretches across the screen.

## Lighting option

The options menu's Lighting entry shows a box to adjust until it is just visible. It holds a level from 0 to 100
(stored as one byte; 40 in all ten captures). A level L adds L/255 to each of red, green and blue of the level's
moonlight, reflected and ambient lights; lamps keep their own colour. It changes no display register.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00194558` | Open the 640 x 448 display with its frame and depth buffers | Video mode and output |
| `0x00194e28` | Apply the Video option (Normal or Wide) | Video option |
| `0x0048e250` | Turn a render setting into GS register state | Render settings |
| `0x001b4838` | Set the Lighting level | Lighting option |

## Acceptance checks

- The PS2 disc's `SYSTEM.CNF` is 57 bytes and gives NTSC as its video mode.
- During play the GS reads SMODE2 `0x1` (interlaced, field mode) and PMODE `0x8067`.
- DISPLAY1 reads DX 636, DY 50, MAGH 3, DW 2559, DH 447: the picture is 640 x 448.
- DISPFB2 shows the same buffer as DISPFB1 one line lower, so a one-pixel horizontal line shows on two output lines at
  about half strength.
- The shown frame buffer alternates between byte offsets `0` and `0x118000` of GS memory; the depth buffer sits at
  `0x230000` in PSMZ24.
- `SLUS_212.15` holds at `0x0052f4a8` 36 words of which exactly 20 are not -1, and the source-over pair holds `0x44`.
- In the opening Coney streets FOGCOL reads (12, 12, 5), and the colour clear writes that same colour at alpha 0x80.
- The sky never covers any building, prop or character, however far away.
- Through the gaps of an alpha-tested cut-out (a chain-link fence, barbed wire) the scene behind shows, and objects
  behind the gaps are not hidden by the cut-out's depth.
- The radar and the objective text stay in front of walls and characters that come closer to the camera.
- The whole picture, HUD included, carries a faint dark blue cast (7, 20, 30 at about 16 %) in the opening streets.
- World objects fade toward the fog colour from half the far plane (about 57.5 units in the open streets) and reach it
  at the far plane.
- With Video set to Wide the horizontal view is unchanged and the picture shows less above and below than with Normal.
- The Lighting level runs from 0 to 100.

## Open questions

- Wide was not captured: the 41.8-degree vertical view is computed from the aspect value, and what the HUD aspect and
  HUD scale values change on screen is not known.
- The executable also holds a 640 x 480 configuration with aspect 1.59 (`0x3fcb851f`) and non-interlaced output; no
  capture shows it and whether the US release can reach it is not known.
- The front end, split screen, rain, interiors, fights and the motion blur effect were not captured, so their
  settings are unconfirmed here.
- Whether the colour wash's colour and strength change with level or time of day is not known; all captures show
  (7, 20, 30) at 21.
- The step size of the Lighting option and its default are not confirmed (the captures hold 40).
- The pixel contents of GS memory were not readable from these savestates, so the clear colour and depth values were
  read from the frame's packets and camera values, not from the buffers.
- The Xbox version's display was not measured.
