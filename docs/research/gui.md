# GUI: widgets, sprite sheets and text

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims (sprite
colours, draw-order keys) were read from the running game in PCSX2 2.9.94 over PINE and say so. The disc-side checks
(2026-10-04) walked every chunk container of the NTSC-U WAD and are reported as counts and layouts only.

## Purpose

How the front end and the HUD put 2D things on the screen: the widget classes, the screen flow that chains menu
screens, the **sprite sheets** (chunk `0x4C`, "Particle Page") every 2D image comes from, how a sprite reaches a
RenderWare PTank, how text is laid out and drawn as one sprite per character, where the UI strings come from, and the
order in which it is all drawn. [Start-up and the front end](frontend.md) says which screens run when; the screen
geometry (GUI coordinates, the overlay camera) is on [Graphics](graphics.md#2d-drawing); what the in-game HUD shows
and where is on [The in-game HUD](hud.md).

In short: **menus are C++ widgets, not data.** A screen's layout, items and transitions are in its constructor and
`Init`; the data it uses is a sprite sheet resource (named by a hash), global UI strings set from Lua, and markup tags
inside those strings. Every image, glyph and icon is a rectangle of one texture in a sprite sheet, drawn as a PTank
particle.

## Original structure

The GUI is `c:/Warriors/Source/GUI/` (`0x001a1f10`-`0x002176b8`, [Source map](source-map.md#gui)), with the
profile-manager screens in `GUI/ProfileManagementGUI/` and the Rumble-mode screens in `GUI/RumbleModeGUI/`. The sprite
sheets belong to `Graphics/ParticlePage.cpp` (`0x00181b68`-`0x00182820`) and the resource manager
(`Graphics/ResourceMgr.cpp`). Names are ours unless they are class strings passed to the allocator.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x001a8e30` | `Widget::Widget` | base constructor of every widget | confirmed (code) |
| `0x001a1bf8` | `BaseWidget::BaseWidget` | a sprite widget (`0x100` bytes) | confirmed (code) |
| `0x001ccf88` | `TextWidget::TextWidget` | a text widget (`0xd0` bytes); `0x001cd1e0` sets its text | confirmed (code) |
| `0x001b9600` | `TextWidget_Layout(out, widget, draw, colour, ...)` | measures and draws marked-up text | confirmed (code) |
| `0x001d3ef0` | `OptionGrid::OptionGrid` | a grid of selectable items (vtable `0x0053b768`) | confirmed (code) |
| `0x001d4110`, `0x001d4230` | `OptionGrid` setup, add item | at most 50 items | confirmed (code) |
| `0x001cea70` | `UsageInfo::UsageInfo` | the button legend line (`0x2d0` bytes); `0x001cec28` sets its text | confirmed (code) |
| `0x001b8f98` | markup text widget (class `MessageHUD`) | marked-up text in a box; setup `0x001b9090` | confirmed (code) |
| `0x001a0fd0` | `Bar` | a two-sprite meter (PM_Light, the Rumble gang screen) | confirmed (code) |
| `0x001e1338` | `ScrollingMenu` | the Rumble screens' scrolling lists | confirmed (code) |
| `0x0017ae38` | colour table set-up | fills the [colour table](#colour-table) at `0x005fd260` | confirmed (code) |
| `0x001c7e80` | `ScreenFlowController::ScreenFlowController` | stack of screens (`SFC_States`, `SFC_SharedData`) | confirmed (code) |
| `0x001c8010`, `0x001c80e8`, `0x001c82e8`, `0x001c81e8`, `0x001c83c8` | add transition, push, pop, unwind, update | | confirmed (code) |
| `0x001c6b78`... | message box (`0x005e5840`) | timed messages and two-choice dialogs | confirmed (code); file unnamed |
| `0x001e95c0` | `MenuInput_Dispatch` | pad and stick to menu commands ([Front end](frontend.md#input)) | confirmed (code) |
| `0x0019ee70` / `0x0019eea0` | `GlobalString_Get(id)` / `_Set(id, text)` | the UI string table | confirmed (code) |
| `0x00181b20` | chunk `0x4C` `onLoaded` | links a sprite sheet to its texture dictionary | confirmed (code) |
| `0x00182820` | chunk `0x4D` `onLoaded` | the table of all sprite sheets | confirmed (code) |
| `0x00181e38` | `Page_Rect(instanceData, i)` | rectangle `i` of a sheet | confirmed (code) |
| `0x0018aec0` | `ResourceMgr_CreateInstance(...)` | a sprite batch over a sheet | confirmed (code) |
| `0x001972b0` | `Instance_SetResident(inst, on)` | loads the sheet, creates the PTank | confirmed (code) |
| `0x00182de0` | `Instance_AddSprite(inst, sprite)` | appends one sprite to the batch | confirmed (code) |
| `0x00197168` | `Instance_Render(inst, viewport)` | draws the batch's PTank | confirmed (code) |
| `0x00185d20` | `ResourceMgr_RenderOverlay` | the 2D pass ([Draw order](#draw-order)) | confirmed (code) |
| `0x001c3de0`, `0x001c4d00`, `0x001c4768` | radar: set up, add a blip, set a blip's icon | [The radar](#radar-icons) | confirmed (code) |
| `0x001b4168` | `HUD_RadarAddHuman` | the blip a human gets from its class | confirmed (code) |
| `0x00179808`, `0x00179958`, `0x00179c30` | `Font_Size(scale)`, `Font_Measure`, `Font_Draw` | text as sprites | confirmed (code); file inferred (`Graphics/`, before the stub `0x0017b1c0`) |

## Data

### Sprite sheet: chunk `0x4C` "Particle Page" {#particle-page}

A sprite sheet is a resource of two chunks: a `0x2A` RenderWare texture dictionary holding **exactly one texture**,
then a `0x4C` chunk listing rectangles of that texture. Little-endian, 16-byte aligned:

```c
struct ParticlePage {          // chunk 0x4C, size = align16(0x14 + 16 * count)
    uint32_t placeholder;      // +0x00  0x00421798 in every page (a pointer from the build); not read
    uint32_t count;            // +0x04  number of rectangles, 1..371 seen
    int32_t  firstGlyph;       // +0x08  rectangle of character 0 when the sheet is used as a font; -1 otherwise
    uint32_t rects;            // +0x0c  0 in the file; set on load to the address of +0x14
    uint32_t texDictionary;    // +0x10  0 in the file; set on load to the dictionary read just before
    struct { float u0, v0, u1, v1; } rect[count];   // +0x14 texture coordinates, top-left and bottom-right
    // zero padding to a multiple of 16
};
```

**Evidence:** confirmed (code) for `+0x04` (the count, `0x001828b0`), `+0x08` (`0x00179958`, `0x00179c30`), `+0x0c`
and `+0x10` (`0x00181b20`) and the 16-byte rectangles (`0x00181e38`). The `onLoaded` handler pops the `0x4C` chunk and
the `0x0B` dictionary before it, stores the dictionary at `+0x10` and `&rect[0]` at `+0x0c`, and pushes the chunk back
as `0x4C`. **Disc check (corroboration):** all 1,335 `0x4C` chunks have exactly that size, zeros at `+0x0c` and
`+0x10`, a `0x2A` chunk with one texture just before them, and every coordinate in [0, 1] with `u1 >= u0` and
`v1 >= v0`. `firstGlyph` is -1 in 1,328 pages; the 7 others are fonts or carry a font (`big_font` 0, `part_page0`
94, `part_page1` 20 among them).

**The rectangles are inset by a quarter texel.** Disc check (2026-10-06, the 1,547 rectangles of the 576 sheet-table
records with a texture): every `u0` is a quarter texel past a texel edge and every `u1` a quarter texel short of one,
and `v0`, `v1` the same distance in texture coordinates (a quarter texel of the width: an eighth of a texel down a
512 × 256 texture). So the sprite is the whole texels from `floor(u0 × width)` to `ceil(u1 × width)`, and a radar
icon listed 12.5 texels wide is 13; the inset keeps bilinear filtering from reaching the neighbouring sprite
(inferred). Coney's reference images cut sprites this way (`graphics::rectTexels`, `src/graphics/reference_sprites.h`).

**How a sprite refers to its texture:** by sheet and rectangle index only. The sheet's texture is the first (only)
texture of its dictionary (`instance + 0x14` = the dictionary's texture list head − 8, `0x00181b68`); its raster's
width (`+0x0c`) and height (`+0x10`) turn a rectangle into pixels (`0x00181e18`, `0x00181e28`).

### Sprite sheet table: chunk `0x4D` "Particle Page Header"

One chunk in `warriors.glr`: `u32 count`, then `count` × `{u32 size, u32 nameHash}`. The `onLoaded` handler
(`0x00182820`) keeps the chunk at `ResourceManager + 0x9040`, the count at `+0x9048` and the records at `+0x9044`.
`0x001828c0(rm, i)` returns record `i`; `0x00181e50(rm, hash)` returns the size for a name hash (or, for a sheet not in
the table, the size of the WAD file named `"%u"` of the hash). Confirmed (code). The name hash is the CRC-32 of the
sheet's name, the same number that names its WAD file in the decimal block
([WAD contents](formats/wad-contents.md#names)).

**Disc check (corroboration):** 576 records; `size` is the resource's size in bytes. Names matched so far: 0
`part_page0`, 1 `part_page1`, **3 `menu_system`** (the menu sprites), 7 `part_fire`, 8 `lighting`, 10
`hud_minigames`, **13 `big_font`**, 51 `legal_screen`.

### Resource instances (sprite batches) {#resource-instances}

The resource manager has 255 instance slots of `0x78` bytes at `ResourceManager + 0x189c`; an instance is one PTank
(a batch of sprites) over one sheet. `ResourceMgr_CreateInstance` (`0x0018aec0`) takes the next free slot after the
last one used (round robin; 255 = none free) and fills it (`0x001828e0`). Confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x00` | sheet name hash |
| `+0x04` | the PTank atomic (once resident) |
| `+0x08` | the atomic's frame |
| `+0x0c` | the sheet's page data (once resident) |
| `+0x10` / `+0x11` | in use / resident |
| `+0x18` | capacity: most sprites per frame |
| `+0x1c` | sprites added this frame |
| `+0x20` | the float passed first at creation ("depth": 8,000 to 11,000 seen) |
| `+0x28` | PTank data flags: `0x10000082` plus `0x05` (format 0), `0x25` (format 1) or `0x08` (format 2) |
| `+0x30`, `+0x34` | source and destination blend: 5 and 6 (source alpha, inverse source alpha) |
| `+0x38` | format: 0 position and size, 1 adds a 2D rotation, 2 a full matrix |
| `+0x3c` | where the atomic lives: 0 drawn directly by its owner, 1 the 3D overlay world (`ResourceManager + 0x9038`), 2 the 2D overlay world (`+0x9034`) |
| `+0x42` | viewport, `0xff` = all |
| `+0x44` / `+0x48` | the PTank's position array and its stride (16) |
| `+0x4c` / `+0x50` | colour array and stride (4) |
| `+0x54` / `+0x58` | texture-rectangle array and stride (16) |
| `+0x6c` / `+0x70` | size array and stride (8) |
| `+0x74` | the largest sprite count seen |

The array rows are confirmed (code) at the accessors `0x00182d80`-`0x00182db8` and confirmed (runtime), PCSX2
2.9.94: the slots sit at `ResourceManager + 0x189c` (`0x01fd009c` on a retail boot), and on the main menu slot 3
(`part_page0`) has capacity 1,024, slot 6 (`big_font`) 2,048, both with format 0 and `+0x3c` = 2. The arrays keep
the last frame's sprites after the count at `+0x1c` is reset, which is how they were read.

The PTank flag names, inferred from RenderWare 3.7's `rpPTANKDFLAG*` values: `0x10000000` array, `0x80` two texture
coordinates per sprite, `0x02` colour, `0x01` position, `0x04` size, `0x20` 2D rotation, `0x08` matrix.

Instances created at start-up by the resource manager (`0x00184918`), in slot order 0-12 (the slot numbers assume the
first slot used is 0, inferred): `part_page1`, `part_page1`, `part_page0`, `part_page0`, `part_page1`, `part_page0`,
**`big_font` (slot 6)**, `lighting`, `lighting`, `part_page1`, `part_page1`, `hud_minigames`, `part_fire`. Text code
refers to fonts by slot ([Text](#text)).

### Sprite record

What `Instance_AddSprite` (`0x00182de0`) copies into the PTank. Confirmed (code):

| Offset | Size | Used when | Meaning |
| --- | --- | --- | --- |
| `+0x00` | 64 | format 2 | a 4 × 4 matrix |
| `+0x40` | 16 | format 0, 1 | position (x, y, z, 1) in overlay-camera space |
| `+0x50` | 8 | format 0, 1 | size (w, h) |
| `+0x60` | 4 | format 1 | rotation |
| `+0x64` | 16 | always | texture rectangle `u0, v0, u1, v1` (a sheet rectangle) |
| `+0x74` | 4 | always | colour, `RwRGBA` (R in the low byte) |

A sprite past the capacity is dropped (the call returns 0). An instance that is not in use or not resident ignores
sprites.

### Sprite colours {#sprite-colours}

**Sprite colours are RenderWare's 0-255, with 255 full intensity and opaque**, not the GS's 128. Confirmed
(runtime), PCSX2 2.9.94, from the colour arrays of the batches on the main menu: the `big_font` text is `(178, 178,
178, 255)` for the selected item and `(170, 43, 43, 255)` for the others, each preceded by its drop shadow `(0, 0,
0, 128)`; the Quick Rumble background picture is `(93, 106, 49, 254)`. The legal screen draws with `(255, 255, 255,
255)` (confirmed (code) at `0x0015a0b8`, [Graphics](graphics.md#first-screen)). The shadow's alpha × 128 / 255 is
therefore a half-transparent shadow (128 of 255 for a fully opaque widget), not a conversion to the GS range. On
PCSX2's screen the grey and the red text come out at the same 70 % of these values, as does the legal picture's white
at full colour, which fits one scale for all three (the cause of the 70 % is open,
[Graphics](graphics.md#open-questions)).

### The colour table {#colour-table}

The menus take their colours from a table of `RwRGBA` at `0x005fd260`, 8 bytes apart, filled once by `0x0017ae38`.
Confirmed (code); the three menu colours also confirmed (runtime), [Sprite colours](#sprite-colours):

| Address | RGBA | Address | RGBA |
| --- | --- | --- | --- |
| `0x005fd260` | black | `0x005fd2c8` | (192, 96, 0, 255) |
| `0x005fd268` | white | `0x005fd2d0` | (0, 128, 128, 128) |
| `0x005fd270` | (127, 127, 127) | `0x005fd2d8` | (128, 128, 0, 255) |
| `0x005fd278`-`0x005fd2a0` | red, green, blue, cyan, magenta, yellow | `0x005fd2e0`-`0x005fd2f8` | (32, 0, 0, 48), (4, 4, 4, 140), (0, 0, 32, 64), (10, 5, 40, 60) |
| `0x005fd2a8`-`0x005fd2c0` | (255, 128, 0), (128, 0, 0), (0, 0, 128), (0, 128, 0), alpha 255 | `0x005fd300` / `0x005fd308` | (0, 0, 0, 0) / (36, 75, 130, 255) |
| **`0x005fd310`** | **(178, 178, 178, 255)**: usage lines, Rumble titles, item text base | **`0x005fd318`** | **(178, 178, 178, 255)**: the selected grid item |
| **`0x005fd320`** | **(80, 80, 80, 255)**: message-box and Game Type items | **`0x005fd328`** | **(170, 43, 43, 255)**: the front end's red (titles, items, the logo's tint, bar fill) |

### Widgets

Every screen element derives from one base (`0x001a8e30`). A widget object has its vtable at `+0x00`, a second
interface vtable at `+0x6c`, and, for a menu screen, a screen-flow state at `+0x70` (its own vtable at `+0x88`, its
transition map at `+0x78`). Confirmed (code) at the constructors `0x002077b8`, `0x00209bc0`, `0x001d3ef0`.

Widget vtable slots (GCC 2 layout, offsets from the vtable start), from the screens read here; confirmed (code) for
the calls, names inferred:

| Slot | Role |
| --- | --- |
| `+0x30` | `Update`: input and animation; a screen writes its result code at `+0x74` |
| `+0x38` | `Render`: add the widget's sprites |
| `+0x48` | is visible |
| `+0x60` | destructor |
| `+0x68` | `Shutdown`: release children and resources |
| `+0x6c` | setup with a rectangle, size, colour and flags (sprite and text widgets) |
| `+0x74` | the widget's rectangle |
| `+0x88` | is active |
| `+0x90` | take focus (`OptionGrid`: `0x001d4d28`) |
| `+0xa8` | `Init`: create children, once (`+0x0c` marks it done) |
| `+0xb0` | how much of the text to show (0-1; text reveal) |

Screen-flow state vtable (at widget `+0x88`): `+0x10` `Enter(flow)`, `+0x18` `Update()` → result, `+0x20` `Exit()`.
For `PM_Greet` (`0x0053e050`): `Enter` (`0x00207d48`) resets the pad handlers and calls `Init`; `Update`
(`0x00207dd0`) sets the result to "stay" (`-0x100`), calls the widget's `Update` and `Render` and returns the result;
`Exit` (`0x00207da0`) calls `Shutdown`. Confirmed (code).

### The widget classes {#widget-classes}

The classes the menus are built from. Confirmed (code) at the addresses given; the screens that use them are on
[Front end](frontend.md#pm-screens).

**`BaseWidget`** (`0x001a1bf8`, vtable `0x005394b8`), one sprite. Setup (slot `+0x68`, `0x001a1db8`): `(size, depth,
widget, position, colour, visible, active, spriteWord, instance, anchor, aspectFix, ...)`.

- **Sprite word** (`+0xc8`): sheet-table record in the high half, rectangle in the low half (`0x30000` is
  `menu_system` rectangle 0). **Instance** `+0xc4`; -1 makes its own at the given depth.
- **Size:** `0x001a2120(size, 0, widget, 0)` sets width = height = `size` in **overlay units** (not GUI units: 1.1 is
  the screen's height, 1.595 its width in the default mode, [Graphics](graphics.md#2d-drawing)); each update keeps
  the height and sets the width to height × the rectangle's pixel aspect ((u1 − u0) × texW / ((v1 − v0) × texH)),
  times `*0x0050b208` × 0.80357 more when `aspectFix` (`+0xe0`) is set.
- **Anchor** (`+0xc0`): 0 the centre is at the position, **1 the left edge** (the sprite moves right by half its
  width), 2 the right edge; applied in `Render` (`0x001a2690`) only.
- **Render:** colour `+0xb4`; an optional timed fade, alpha = byte `+0xf8` × (end `+0xf4` − now) / duration `+0xf0`;
  an optional shadow record (`+0xec`, none after setup) at the position + (0.0025, 0.004), without the anchor shift,
  alpha × 0.502.

**`TextWidget`** (`0x001ccf88`, `0xd0` bytes, vtable `0x0053ae78`), one line without markup, used by grid items and the
PM titles. Setup (slot `+0x68`, `0x001cd060`): `(widget, position, metrics, colour, visible, active, mode3D, flags,
fontSlot)`. Fields: text `+0x04` (set by `0x001cd1e0`, which also measures it), visible / active `+0x50` / `+0x54`,
colour `+0x5c`, position `+0x60`, `Font_Draw` flags `+0x70` (4 proportional, 1 right, 2 centred), metrics `+0x78`
(`Font_Size` of the given scale), **font slot `+0xa0` as given**, measured width `+0xb0`, reveal `+0xc0` (1.0), shadow
byte `+0xc4` = `0x80`. Render (`0x001cd288`) is one `Font_Draw` at the position: the pen starts at x and the glyphs
are centred on y. Confirmed (runtime): text placed at y 0.81 has its capitals centred at 0.811.

**The markup text widget** (`0x001b8f98`, vtable `0x0053a248`, layout [`0x001b9600`](#text)), used by `UsageInfo`, the
hint texts and message boxes. Setup `0x001b9090(sizeScale, fontScale, widget, position, colour, visible, active, ?,
fontSlot)`: the font slot is **6 if 6 is passed, otherwise 3** (`part_page0`; the constructor's default is 3); base
size `+0x1d0` (`0x001b9288`); shadow byte `+0x18c` = `0x80`; box right limit `+0x184` = 10,000; proportional
`+0x180` = 1; **alignment `+0x17c`** (`0x001b9478`): 0 left, 1 right, 2 centred.

**`UsageInfo`** (`0x001cea70`, `0x2d0` bytes): a markup text widget at `+0x40`, size 1.0, colour `0x005fd310`, font slot
3. `0x001ceb40(widget, position, leftAlign)` sets it up, **left-aligned when `leftAlign` ≠ 0**, centred otherwise
(the PM screens pass 1, the Rumble screens 0); its first text is string `0x1a`, then the owner's (`0x001cec28`, at
most `0x95` bytes).

**`OptionGrid`** (`0x001d3ef0`, vtable `0x0053b768`: update `0x001d4f20`, render `0x001d52c8`, active `0x001d4418`,
focus `0x001d4d28`, unfocus `0x001d4db8`; input interface `0x0053b740`, handler `0x001d4c40`):

- **Setup** `0x001d4110(y, grid, rows, a2, owner)`: position (0, 0, y, 1); up to 5 **items-per-row** counts at
  `+0xb0` from `rows`; the owner (`+0x80`) sees every command first; the HUD player 0's input record (`+0x50`);
  centre x `+0x90` = 0.5; move cue `+0x94` = 4; wrap `+0x98` = 1; `+0x9c` = 0 (left and right walk all items); row
  gap `+0xa0` = 0; selected `+0xa4` = 0. Owners then set `+0x8c` (left x), `+0x88` = 1 (left-packed) and their own cue.
- **Add item** `0x001d4230(fontScale, grid, text, hasSeparator, code, flags, enabled, colour, fontSlot,
  separatorAlphaMode)`, at most 50: an `OptionGridTextWidget` (`0x200` bytes, `0x001d5350`, vtable `0x0053b8c8`, setup
  `0x001d5450`) in an `OptionGridItem` (`0x60` bytes, `0x001d3bb0`) holding the code at `+0x54` (read by
  `0x001d43f0`; the selected index by `0x001d43e8`). The text is drawn in `0x005fd310`'s alpha with the colour below;
  with `hasSeparator` a second text, **`" : "`** (`0x00555f98`), in font slot 3 and the given colour, follows it. The
  item's box is its width (plus the separator's) by `h + lineGap` = 6h / 7 of `Font_Size(fontScale)`.
- **Layout** (each update): rows in order; with `+0x88` = 1 items are packed left to right from `+0x8c` with no gap
  (`0x001b5e60`), otherwise each row is centred on `+0x90` (`0x001b5dd8`); the row y starts at `+0x28` and moves by
  the item height plus `+0xa0`. A grid of fewer than 3 items does not wrap.
- **Colour** (item render `0x001d5700`): `0x005fd318` (grey 178) when enabled and focused, otherwise the item's
  colour (`+0x1f4`); alpha from `0x005fd310`; the separator in the item's colour, alpha 255 or the text's by
  `separatorAlphaMode`. **No highlight sprite and no size change**: selection is colour only.
- **Input:** nothing until 20 ms after the last command or focus (`+0x84`); the d-pad by the auto-repeating query, or
  the plain one after a refused move (`+0xc4`); buttons by the button pass (mask `0xffff0fff`). The owner's handler
  runs first; if it returns 0: **up / down** (`0x001d4628` / `0x001d4790`, only with two or more rows) keep the column
  (clamped to the row's length), wrap from the first row to the last and back, skip items that are not selectable,
  and play `0xe` when they land on the same item; **left / right** (`0x001d48e0` / `0x001d4a30`) walk all items in
  order with wrap (`+0x9c` = 0) or stay in the row (`+0x9c` set), skipping unselectable ones; a move
  (`0x001d4b88(grid, i, 1)`) unfocuses the old item, focuses the new one and plays cue `+0x94`
  (`0x0010fc30(*0x0050aa84, cue)`); `0x001d4b88(grid, i, 0)` selects without a sound (defaults). Accept and back are
  the owner's.
- **Focus** (`0x001d4d28`) clears the input record (`0x00146000`), marks the grid focused, focuses the selected item
  and stamps `+0x84`.

**`Bar`** (`0x001a0fd0(width, height, 0.5, widget, position, backColour, spriteWord, 1, 0)`, `0x70` bytes): a meter of
two sprites from the given sheet rectangle, width and height in overlay units, left edge at the position (inferred
from the runtime: PM_Light's bar starts at x 0); fill colour `+0x28`, fill fraction `+0x38`; drawn by
`0x001a1138(0.25, bar, 0, 0)`.

**`ScrollingMenu`** (`0x001e1338`, vtable `0x0053bfc8`), the Rumble lists: [Front end](frontend.md#rumble-screens).

### Markup tags {#markup}

Text strings carry tags in angle brackets. The table at `0x0050d718` holds 66 tag strings of 61 bytes each; the
layout code (`0x001b9600`) compares the text after a `<` with each in turn and acts by index. Confirmed (code):

| Index | Tag | Effect |
| --- | --- | --- |
| 0 | `<COLOR rrggbbaa>` | colour (hex); alpha multiplied by the widget's fade |
| 1 | `<SIZE f>` | font size × `f` |
| 2 | `<PULSE ms>` | colour swings linearly between × 1.5 and × 0.5 (clamped to 255), reversing every period: a triangle wave |
| 3 | `<SOUND name>` | plays the sound (CRC-32 of the name) once, on the first frame shown |
| 4 | `<FREEZE ms>` | sets the display end and freezes the game timer for `ms` (`0x00145ea8(gameTimer, ms, 2000)`) |
| 5 | `<DISPLAYTIME ms>` | the text disappears after `ms`; in its last 1,000 ms its alpha is the remaining ms × 0.255 |
| 6 | `<BOLD>` | |
| 7 | `<BIGFONT>` | font slot 6 (`big_font`) |
| 8 | `<MONEYFONT>` | an icon font (glyph base set from the font) |
| 9 | `<BGFONT>` | glyphs on a background (creates an instance over sheet 0, depth 8,000) |
| 10, 11 | `<MONEYPLUS>`, `<MONEYMINUS>` | the icon characters `=` and `<` |
| 12-16 | `<CENTER>`, `<CCENTER>`, `<RIGHT>`, `<RRIGHT>`, `<LEFT>` | alignment: `CENTER` (flags 6) and `RIGHT` (5) shift the whole line by its measured width; `CCENTER` and `RRIGHT` pass the same flags to each `Font_Draw` run, aligning each run about the pen; `LEFT` is 4 |
| 17-20 | `<CR>`, `<CR2>`, `<CR3 f>`, `<CRM>` | new line; `CR2` only when the widget's `+0x188` is set and `*(s16)(W_GameState + 0x224)` < 2 (single player), `CR3` adds `f`, `CRM` only when `+0x188` and `+0x1b8` are set |
| 21 | `<AUTOINDENT ...>` | |
| 22-32 | `<FIST>` ... `<BGCIRCLE>` | HUD icons, each a single character of the current font (`:`, `>`, `?`, `;`, `@`, `A`, `B`, `C`, `E`, `F`, `y`) |
| 33-49 | `<S>`, `<O>`, `<T>`, `<ST>`, `<X>`, `<START>`, `<SELECT>`, `<R1>`, `<R2>`, `<R3>`, `<L1>`, `<L2>`, `<L3>`, `<DU>`, `<DD>`, `<DL>`, `<DR>` | **button glyphs**: characters `0x9f`, `0x9d`, `0x96`, `n`, `0x9e`, `0x97`, `0x93`, `0x9c`, `0x94`, `0x92`, `0xa0`, `0x95`, `0x91`, `0x9b`, `0x99`, `0x9a`, `0x98` |
| 50, 51 | `<LAS>`, `<RAS>` | (no character set) |
| 52-55 | `<SDD>`, `<SDL>`, `<SDR>`, `<SDU>` | animated stick glyphs: alternate between two characters every 500 ms |
| 56-65 | `</COLOR>`, `</SIZE>`, `</PULSE>`, `</BOLD>`, `</BIGFONT>`, `</MONEYFONT>`, `</BGFONT>`, `</CENTER>`, `</RIGHT>`, `</LEFT>` | restore: `</COLOR>` the widget's colour, `</SIZE>` `Font_Size(1.0)` (not the base size), `</BIGFONT>` the saved font |

A glyph tag is replaced by its one-character string (`"%c"`) and drawn like text, so the button icons are ordinary
characters of the font sheet.

## Behaviour

### The screen flow {#screen-flow}

`ScreenFlowController` (`GUI/ScreenFlowController.cpp`) is a stack of screen-flow states (a list, `SFC_States`) plus
shared data (`SFC_SharedData`). Confirmed (code):

- **Add transition** (`0x001c8010(flow, state, code, next)`): in `state`'s map, `code` → `next`.
- **Update** (`0x001c83c8`): call the top state's `Update`. Result `-0x100`: stay. `-0xff`: **pop** (`0x001c82e8`:
  `Exit` the top, remove it, `Enter` the new top). Any other value: look it up in the top state's map; no entry means
  stay; a target already on the stack is **unwound** to (`0x001c81e8`: `Exit` every state in the stack, remove those
  above the target, `Enter` the target); otherwise **push** it (`0x001c80e8`: `Exit` the old top, push, `Enter` the
  new one). Returns true when the stack is empty.

So a screen never draws while it is covered: covered screens are exited and re-entered.

### A frame of 2D

1. Widgets' `Update` and `Render` run in the mode's update (for the menus, inside `PM_Controller` and the HUD).
   `Render` calls `Instance_AddSprite` for each sprite: a sprite widget adds one sprite from its sheet (plus a black
   drop shadow offset by (0.0025, 0.004) at its alpha × 128/255, see [Graphics](graphics.md#2d-drawing)); a text
   widget adds one sprite per character ([Text](#text)).
2. In the overlay pass the overlay world is rendered through the overlay camera; each PTank atomic in it has the
   render callback `0x00197000`, which does not draw but **queues** the instance with a sort key.
3. `ResourceMgr_RenderOverlay` (`0x00185d20`) then sets Z test and Z write off and culling off, sorts the queue and
   draws each instance (`0x00197168`: blend states from `+0x30`/`+0x34`, the PTank's sprite count, render), with the
   overlay camera of its viewport when the instance is bound to one, and finally empties every instance
   (`0x00185cc8`: sprite count 0).

An instance with `+0x3c` = 0 is not in a world: its owner draws it directly (`0x00197168`, as the legal screen does).

### Draw order {#draw-order}

The queue holds `{instance, key}` pairs (`ResourceManager + 0xca4`, pointers at `+0x149c`, count at `+0x1898`) and is
sorted with the C library's `qsort` and the comparator `0x00184890`: **ascending key**, so the smallest key is drawn
first and the largest ends on top (Z test is off). Confirmed (code). The key, set by `0x00197000`: for an instance in
the 2D overlay world (`+0x3c` = 2) a float read from the atomic at `+0x28`; otherwise the squared distance from the
player camera to the atomic minus the square of a radius. Confirmed (code) for the reads. **The 2D key is the depth
given at creation** (`+0x20`): confirmed (runtime), PCSX2 2.9.94, reading the queue (`ResourceManager + 0xca4`,
`{instance, key}` pairs, still in memory after the pass empties the count) and the instances:

| Screen | Queued instances (queue order) and keys | Drawn (ascending) |
| --- | --- | --- |
| main menu | `part_page0` 10,000; `big_font` 9,000 | text, then the button glyphs |
| Quick Rumble menus | slot 60 (a one-sprite sheet, CRC `0x349348bd`: the background picture) 8,000; `part_page0` 10,000; `big_font` 9,000 | background, text, glyphs |
| in a fight | `part_page1` -1.21e8; `lighting` -1.00e8; `part_page1` -1.21e8; `part_page0` 10,000; `big_font` 9,000 | the 3D-overlay batches (`+0x3c` = 1, distance keys) first, then text, then glyphs |

Each 2D key equals the float at the instance's `+0x20`. The keys of instances in the 3D overlay world are negative:
-120,999,792 for the two `part_page1` batches (depth 11,000) and -99,999,792 for `lighting` (depth 10,000), which
is the squared camera distance (208, the same for all three) minus the square of the creation depth, so the radius
the key subtracts is the depth (inferred from the numbers). They always come before the 2D ones.
`menu_system` (8,500) was not on these screens.

### Text {#text}

**Fonts are sprite sheets.** A text widget draws through a font slot (an instance index: 6 is `big_font`); the
sheet's `firstGlyph` gives the rectangle of character 0, so character `c` (a byte) is rectangle `firstGlyph + c`.
`big_font` has 262 rectangles and `firstGlyph` 0, so its rectangles are indexed by the character code directly; the
button glyphs above (`0x91`-`0xa0`) are characters of the same sheet. Confirmed (code) at `0x00179958` and
`0x00179c30`; the disc check gives the counts. Two call sites pass an explicit base instead of `firstGlyph` and shift
the character: font slot 6 by -1, slot 3 by `-'0'` (digits); where they are used is not traced.

**Size.** `Font_Size(scale)` (`0x00179808`) gives `{w, h, spacing, lineGap}` in GUI units:

```text
w       = scale / 30
h       = w * (screenW / screenH) * 0.75 * 1.3333     # = w * 640/448 in 4:3
spacing = 0.003
lineGap = -h / 7
```

**Advance.** For each character, confirmed (code) at `0x00179958`:

- a space (`0x20`) or `0xac`: `(w + spacing) × 0.56`, halved in proportional mode;
- otherwise, in proportional mode (flag `0x04`): the glyph's pixel size is `wPx = (u1 - u0) × texW + 0.5`,
  `hPx = (v1 - v0) × texH + 0.5`, its drawn width is `wPx × w / hPx` (height `h`, aspect kept), and the advance is
  that width plus `spacing`;
- in fixed-width mode every glyph is `w` wide.

**Drawing** (`Font_Draw`, `0x00179c30`): measure the string; right-aligned (flag `0x01`) moves the start left by the
width plus half a glyph, centred (flag `0x02`) by half the width. Each glyph becomes a sprite centred at
`(pen + (width + spacing) / 2, baseline)` with size `(width, h)`, converted to overlay-camera space by device slots
`+0x90` and `+0x98`, with the glyph's rectangle and the colour. With a shadow alpha, a black copy offset by (0.0025,
0.004) and alpha `shadow × colourAlpha / 255` is added first. A reveal fraction below 1 shows only the top part of each
glyph (both the sprite and its rectangle are cut).

**Layout** (`0x001b9600`) runs twice for a visible widget, once to measure and once to draw. It splits the text at
`<`, draws the runs between tags with the current font, size, colour and alignment, applies tags, and moves to a new
line on `<CR>` by `h + lineGap` plus the extra of `<CR3 f>`. The widget's box is grown to the widest line; with
`<CENTER>` each line is centred on the box. Confirmed (code); `BOLD`, `AUTOINDENT` and `CRM` are not worked out.

### Strings {#strings}

UI text comes from **Lua**, not from the string-table chunks. `config_preload2.lua` runs `config_strings_<lang>.lua`
for the current language (`en`, `de`, `fr`, `it`, `es`; WAD entries 3,774-3,778) and passes each entry of its tables
to a binding: `GSTRING.HUD` to `CfgHUDMessage(id, text)` (`0x0035e5d0` → `0x0019eea0`), and likewise `CRIME`
(`CfgCrimeMessage`), `TSTRING` (`CfgTutorialMessage`), `COMMAND` (`CfgWarriorCommand`) and `ANNOUNCE`
(`CfgAnnounceMessage`). Confirmed (code) for `CfgHUDMessage`: it copies the text into the `Level Dynamic & LUA Pool`
heap and stores the pointer at `0x00600048 + id × 4`; `GlobalString_Get(id)` (`0x0019ee70`) returns it, or an empty
string. The other tables and the script side are inferred from the scripts' string constants.

The text a level shows (objectives, hints, prompts) never reaches a `Cfg*` table: `global.lua` builds `GSTRING`'s
other keys and `LABEL`, and each level script its own `LEVEL<n>` (some from `level<n>_strings_<lang>.lua`), once per
language, keeping the one for `GetLanguage()`; scripts then pass the strings themselves (`LEVEL34.MS_C1_5`) to
bindings such as `HUDSetObjective`. Inferred from the scripts' code. The keys are listed in
[Text labels](../references/text-labels.md).

The chunk types `0x0F`-`0x13` (English to German string tables) and `StringTable/StringTableCache.cpp` are used by
other screens (credits, Rumble mode); they do not occur in the WAD ([WAD
contents](formats/wad-contents.md#chunk-types)).

### The radar {#radar-icons}

`GUI/RadarHUD.cpp` (`0x001c41a0`-`0x001c6878`). The HUD object `0x00600840` holds two radars, one per player, at
`+0x15d0` and `+0x3f10`; every radar binding calls the same function on both. A radar keeps 128 blip slots of
`0x40` bytes from `+0x920` (`0x001c3de0`). The list of every icon and blip type is
[Radar icons and blips](../references/radar-icons.md). Confirmed (code) unless marked.

**A blip is a particle system** of type `hud_radar_dot` ([Particles](particles.md)), made by `0x001e99f8` with two
words: a colour and a sprite word. Blip type 9 alone is a sprite widget (`BaseWidget`, `0x001a1bf8`) instead. The
radar's add function `0x001c4d00(radar, handle, colour, type, layer)` frees the slot the handle already has on that
layer, takes a free one and records, at slot `+0x10` onwards, the handle, the colour, the type (`+0x2c`) and the
particle's handle (`+0x24`). An object may have two layers (0 and 1), each its own slot (found by
`0x001c5188(radar, handle, layer)`).

**Icons are rectangles of `part_page0`.** The radar makes its own sprite batches at start (`0x001c3de0`) with
`PTank_Create(depth, 0x45, ...)`: the sprite word's high half (0) is the record of the sprite-sheet table
([chunk `0x4D`](#sprite-sheet-table-chunk-0x4d-particle-page-header)) the batch draws, record 0 being `part_page0`.
A blip's sprite word is `batch << 16 | rect`; `hud_radar_dot`'s initialiser (`0x003e5bf8`) keeps the batch and sets
the rectangle to `0x45` (69, the plain dot). `HUDSetRadarItemTexture` sends message `0x11` with the icon id, and the
dot's handler (`0x003e5f20` → `0x0039bb18`) replaces the low half:

```c
// 0x0039bb18
*(uint *)(dot + 0xc4) = *(uint *)(dot + 0xc4) & 0xffff0000 | icon & 0xffff;
```

So an **icon id is a rectangle index of `part_page0`** (371 rectangles, a 512 × 256 texture); the icons scripts and
code use are 7-34 texels a side (disc check). `HUD_RadarSetIcon` (`0x001b2ca0`) draws icon 22 at 0.7 and tints icons
29-31 `0x63db4bff` (green; colours here are `0xRRGGBBAA`, as `HUDAddRadarObject` packs them at `0x001b40f8`).

| Radar field | Batch | Blip types |
| --- | --- | --- |
| `+0x4c` | 16 sprites, depth 9,000 | 10 (mission objective) |
| `+0x50`, `+0x54`, `+0x58` | 32 sprites each, depths 5,000-7,000 | `+0x58` type 2, `+0x54` type 3, `+0x50` every other |
| `+0x5c`, `+0x60` | 96 sprites each, depths 1,000, 2,000 | 6, 7, 8, by layer |
| `+0x48` | 16 sprites, depth 10,000 | not traced |

A new blip of type 7, 9, 10 or 12 stays visible while the radar's `+0x08` is set and `+0x0c` clear; any other is
sent message `0x2a`, which sets flag `0x04` of the dot (`+0x54`; hidden, inferred) until message `0x29` clears it.
`0x001b2990` adds nothing for type 5, and for type 6 adds two layers with icons 352 and 359.

**Humans.** `HUD_RadarAddHuman` (`0x001b4168`) picks the blip from the character class byte (`CfgChar` `+0x11a`,
[AI](ai.md#types)): class 1 (police) type 8 with icon 356 on both layers; class 0 or 3 type 9 (icon 362, grey
`0x787878ff`) for the player (brain type 0) and type 7 (icon 365) for any other; class 4 none; any other type 6. A
human with byte `+0x19d` set counts as class 2 unless a Warrior. `Human_Init` adds type 5 (nothing) for everyone. The
AI re-marks humans that turn on the player as type 6 (`0x002b0608`, `0x002c2e80`, `0x002d6e10`, `0x002d79a8`), and a
dealer greeting the player adds type 2, 4 or 3 with icon 29, 31 or 30 at 0.8 (`0x002c7ee0`, dealer types 0, 1, 2).

**Blip modes** (`0x001b32e0(hud, handle, mode)`), sent with message `0x19` to the dot (`0x003e5d00`): mode 0 plain;
1 and 2 icon 352; 3 and 4 icon 353 at 0.8 on layer 0 with a ring (icon 32 or 33 at 0.75) on layer 1, flashing. The
dot's own states: 0 dim (alpha `0x80`), 1 full, 2 pulsing size (× 1.3), 10 growing, 13 blinking, 14 cycling size
0, 0.6, 1.2, 99 opaque white. `HUDSetRadarObjectFlash` sets these states (inferred from the handler).

**To render an icon** as a reference thumbnail: load `part_page0` (WAD file named by the decimal CRC-32 of
`part_page0`; its `0x2A` dictionary and `0x4C` rectangles), cut rectangle *n* and scale it to fit 64 × 64. Coney's
sheet reader ([Coney's implementation](#coneys-implementation)) already does the first two steps.

### The `METRICS1` file

The one "font metrics" WAD entry is **`cn12.met`** (entry 3,743), next to **`cn12.bmp`** (entry 3,742, the 256 × 128
24-bit bitmap). It is text: the line `METRICS1`, a line naming `cn12.bmp` and `mcn12.bmp`, a number, then 95 lines
`code x0 y0 x1 y1 # 'c'` for the characters 32-126: pixel rectangles of a 12-point bitmap font. **No code in the PS2
executable refers to it** (no `.met`, `.bmp` or `cn12` string), so the game does not use it; probably a leftover of a
debug or PC tool (inferred).

## Coney's implementation

- **Sprite sheets** (`src/graphics/particle_page.h`): `parseParticlePage` reads a `0x4C` chunk's count, `firstGlyph`
  and rectangles as [laid out above](#particle-page), refusing data shorter than its header or count; the placeholder
  and the two load-time words are ignored, zero padding after the rectangles is allowed, the coordinates are not
  checked (the original does not either). A `graphics::SpriteSheet` is the rectangles plus a shared reference to the
  texture.
- **The `0x4C` handler** (`src/platform/sprite_sheets.h`, `onParticlePageLoaded`): pops the page and the `0x0B`
  dictionary before it and pushes a `SpriteSheetObject` back under `0x4C`, owning the dictionary and bound to its
  first texture. A dictionary with no texture fails the load. `loadSpriteSheetResource` loads a sheet by resource
  name from the WAD file named by the name's decimal CRC-32; the legal screen uses it
  ([Front end](frontend.md#coneys-implementation)).

- **The sheet table** (`graphics::parseSpriteSheetTable`, `SpriteSheetTable`): the `0x4D` chunk's records, with
  `record(i)` (`0x001828c0`) and `sizeOf(hash)` (`0x00181e50`, without the original's fallback to the WAD file's
  size, which a caller can ask the WAD for). Its `onLoaded` handler (`0x00182820`) pushes the table back under
  `0x4D`; **Coney's choice:** with no resource manager to keep it, whoever loads `warriors.glr` takes it off the
  stack. `addSpriteSheetHandlers` registers both handlers.

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[disc][sprite_sheets]"` with `CONEY_DISC` set finds
1,335 `0x4C` chunks in 814 entries; every one follows a `0x2A` chunk, has exactly the size `align16(0x14 + 16 ×
count)`, loads through the handlers, has a dictionary of exactly one texture and rectangles inside [0, 1] the right
way round; 1,328 have `firstGlyph` -1. The table in `warriors.glr` has 576 records; every name hash names a WAD file
whose size equals the record's `size`, and records 0, 1, 3, 7, 8, 10, 13 and 51 are the CRC-32s of `part_page0`,
`part_page1`, `menu_system`, `part_fire`, `lighting`, `hud_minigames`, `big_font` and `legal_screen`, as above.

- **Sprite batches** (`src/graphics/sprite_batch.h`, `SpriteBatch`): a sheet, a capacity and a depth;
  `addSprite` (`0x00182de0`) appends a [sprite record](#sprite-record) of format 0 (position in overlay-camera space,
  size, texture rectangle, colour) and drops it past the capacity, returning false; `render` (`0x00197168`) projects
  each sprite through the overlay camera ([Graphics](graphics.md#coneys-implementation)), centred on its position,
  and draws the batch as one call with the sheet's texture and source-alpha blending. The largest count seen is kept
  as at `+0x74`.
- **The 2D pass** (`OverlayPass`): batches queued for the frame, drawn in [ascending key](#draw-order) (the depth
  by default), then every batch and the queue emptied (`0x00185d20`, the comparator `0x00184890`, `0x00185cc8`).
  Coney's choices: batches with equal keys keep the order they were queued in (the original's `qsort` promises no
  order); batches are queued by their owner, where the original collects them by rendering its overlay world; only
  format 0 exists.
- `coney --disc <disc> --view-sheet <sheet>` lays a sheet's rectangles out in a grid and draws them as sprites
  through a batch and the 2D pass ([Building and testing](../guides/building.md#run-coney)): `menu_system` (6
  rectangles over a 512 × 256 texture) and `big_font` (262 glyphs) show correctly on the NTSC-U disc.

- **UI strings** (`src/gui/global_strings.h`, `src/scripting/config_strings.h`), loaded [as the game
  does](#strings): in a game run, by the legal screen's preloads in the script system
  ([Scripts](scripting.md#coneys-implementation)); for `--view-text` and the strings disc check, by a short load that
  runs the game's own bytecode, `enum_preload.lua` and then `config_preload2.lua`, in its Lua 4.0 virtual machine
  (`src/scripting/lua_vm.h`, [Front end](frontend.md#coneys-implementation)) with `GetLanguage` (the language field's
  value, 0 English to 4 German), `GetPlatform` and `doFile` provided. `doFile` runs
  `config_strings_<code>.lua`, and the script's loops pass every entry of `GSTRING.HUD`, `GSTRING.CRIME`, `TSTRING`,
  `GSTRING.COMMAND` and `GSTRING.ANNOUNCE` to `CfgHUDMessage` (`0x0035e5d0`), `CfgCrimeMessage`,
  `CfgTutorialMessage`, `CfgWarriorCommand` and `CfgAnnounceMessage`, which fill `GlobalStrings`; `get(id)`
  (`0x0019ee70`) returns a HUD string or an empty one, `set` (`0x0019eea0`) stores one. **The id is the table key the
  script gives**, explicit in the files (`GSTRING.HUD[n] = ...`), not the order of the entries. The other ~1,800
  binding calls of `config_preload2.lua` (character, weapon and sound configuration) are skipped as no-ops.
  Coney's choices: every table is a map, so any id works (the original's HUD array at `0x00600048` has a size not
  yet known); `doFile("config_strings_en")` adds the `.lua` the WAD entry has, as the original does; `GetPlatform`
  returns 1, as on the PS2 (below).

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[disc][strings]"` with `CONEY_DISC` set loads all five
languages. HUD strings: English, Spanish and German 388, French 386, Italian 387, with ids 0-388 (id 372 is unset in
English); every language has 15 crime, 27 tutorial, 8 command and 5 announce strings and a usage line `0x1f`. A load
runs about 42,600 Lua instructions and skips 1,796 binding calls.

- **Fonts** (`src/graphics/font.h`, `Font`): a sprite sheet with a first glyph, character `c` being rectangle
  `firstGlyph + c`. `fontMetrics(scale)` is `Font_Size` (`0x00179808`); `measure` is `Font_Measure` (`0x00179958`):
  proportional or fixed widths and the 0.56 gap of a space or `0xac` as [Text](#text) gives them; `draw` is
  `Font_Draw` (`0x00179c30`): right and centred alignment, one sprite per glyph centred at (pen + (width + spacing) /
  2, y) of size (width, h) through device slots `+0x90` and `+0x98`, and the black shadow at (0.0025, 0.004) with alpha
  shadow × colour alpha / 255. Coney's choices: a character past the sheet's last rectangle takes no room and draws
  nothing; a space draws nothing; the shadow comes just before each glyph (the page's "added first", read per glyph);
  the reveal fraction is not implemented yet.
- **Markup** (`src/gui/markup.h`): the tag table by index, with the characters of the glyph tags; `parseMarkup`
  splits a text at `<` ... `>`. A tag name compares exactly; a `<` with no `>` after it is text; a name not in the
  table (and the nine HUD icon tags 23-31, whose names are not on this page) is skipped and counted.
- **Layout** (`src/gui/text_layout.h`, `layoutText`, `TextWidget_Layout` `0x001b9600`): measures each line's runs,
  grows the box to the widest line, places each line by its alignment and draws each run with `Font::draw` into
  sprites tagged with their font slot; `addTextSprites` hands them to each slot's batch. It implements `COLOR` (alpha ×
  the widget's fade), `SIZE`, `PULSE`, `DISPLAYTIME` (hidden after, fading over the last second), `BIGFONT` (slot 6),
  `MONEYPLUS`/`MINUS`, the alignments, `CR`, `CR2`, `CR3 f`, `CRM`, every glyph tag and the closing tags; `SOUND`
  names and the largest `FREEZE` are reported for the audio layer and the timer; `BOLD`, `MONEYFONT`, `BGFONT`,
  `AUTOINDENT` and the animated stick tags have no effect yet. Lines break at the `CR` tags and, for a multi-line widget
  given a wrap width (the message box, `RM_No2ndController`'s 0.6), before a word that would pass it; how the original
  breaks is not researched, so Coney breaks only at spaces. Coney's choices: a closing tag restores the value before
  its opening tag; a line takes the alignment in effect at its first character, centring and right-aligning inside the
  box (`CCENTER` and `RRIGHT` act as `CENTER` and `RIGHT`); `CR2` and `CRM` always break; `PULSE ms` scales the colour by 1 + 0.5 × sin(2π t / ms);
  the style's y is the first line's centre line. **The text font:** a text starts in slot 2, a `part_page0` instance,
  and `<BIGFONT>` switches to slot 6, `big_font` (Coney's choice, from the data: in `part_page0`, first glyph 94,
  characters `0x91`-`0xa0` are exactly the button pictures of the tag table, while in `big_font` they are empty but for
  a triangle at `0x9c`; `big_font`'s rectangles 256-261 hold a circle and five d-pad pictures. Inferred from viewing
  the sheets with `--view-sheet`).
- **The screen flow** (`src/gui/screen_flow_controller.h`, `ScreenFlowController`, `ScreenFlowState`), as
  [above](#screen-flow): per-screen transitions (`0x001c8010`), push (`0x001c80e8`), pop (`0x001c82e8`), unwind
  (`0x001c81e8`) and update (`0x001c83c8`) with the results "stay" (`-0x100`) and "back" (`-0xff`); a code without a
  transition is ignored; the flow is done when its stack is empty. Coney's choice: `exit` is called only on an entered
  screen, so unwinding exits the top screen alone (the covered ones were exited when they were covered).
- **Widgets** (`src/gui/widget.h` and beside it): `Widget` (`0x001a8e30`) with `init` once (until `shutdown`),
  `update` with the frame's game time and the player's pad, `render` into batches, and visibility. `BaseWidget`
  (`0x001a1bf8`, its sprite `0x001a2690`): one rectangle of a sheet as one sprite, after the black shadow at (0.0025,
  0.004) with alpha × 128 / 255. The classes of [The widget classes](#widget-classes), as written there:
  `BaseWidget` (setup `0x001a1db8`): size in overlay units with the width from the rectangle's shape (or given), the
  aspect fix, the anchor (centre, left edge, right edge) and an optional shadow at the unshifted position with alpha ×
  0.502. `TextWidget` (`0x001ccf88`, setup `0x001cd060`, text `0x001cd1e0`) stands for both text widgets: a text
  through `layoutText` placed left at x, centred on it or ending at it, in the font slot given, shadow 0x80, with a
  fade and its own time for `<PULSE>` and `<DISPLAYTIME>`. `OptionGrid` (setup `0x001d4110`, add item `0x001d4230` up to
  50, focus `0x001d4d28`, handler `0x001d4c40`): row counts, left-packed or centred rows, the `" : "` separator,
  `kSelectedGrey` for the focused selection, the moves, wrap only from 3 items, cue `+0x94` and `0xe`, and the d-pad's
  plain query after a refused move. `UsageInfo` (`0x001ceb40`, text `0x001cec28`): size 1.0, `kMenuGrey`, left-aligned
  or centred. `Bar` (`0x001a0fd0`) and the message box (`src/gui/message_box.h`, [Front end](frontend.md#message-box)).
  The colour table is `src/gui/colour_table.h`. Coney's choices: a grid's row pitch adds 0.0036 to 6h / 7 so rows land
  0.0505 apart at size 1.15, as measured ([Front end](frontend.md#pm-layout)); items past the row counts get a row each;
  part_page0 is Coney's font slot 2 (the original's 3). The screens that use them are on
  [Front end](frontend.md#coneys-implementation).
- **The `METRICS1` file** is not read: no code uses it ([above](#the-metrics1-file)), and the fonts' metrics come from
  their sheets' rectangles.
- `coney --disc <disc> --view-text <font> <text or @id>` lays a text out and draws it through a batch per font and
  the 2D pass ([Building and testing](../guides/building.md#viewing-text)). On the NTSC-U disc, `@0x1f` shows the
  usage line as cross "ok" and triangle "back", and a test text shows `big_font` headings, colours, sizes, alignment
  and every button picture correctly.

**Disc check (NTSC-U, 2026-10-04, counts only):** `coney_tests "[disc][sprite_sheets]"` finds 7 fonts among the 1,335
sheets (4 with a rectangle for all 256 bytes), all loading as fonts; `"[disc][text]"` loads `big_font` (262
rectangles over 512 × 256) and `part_page0` (371, first glyph 94), finds a rectangle for all 21 glyph tags' characters
in both, and lays out all 388 English HUD strings: 10,675 sprites on 400 lines, 89 tags skipped (unknown names such
as `BOBJ`, `YOBJ`, `ROBJ`, and the tags with no effect yet), no string empty.

TODO for the analysts, found while implementing:

- **Sprite colours** (answered, 2026-10-04): 0-255 with 255 opaque ([Sprite colours](#sprite-colours)), as Coney
  takes them; nothing to change.
- **Draw order** (answered): the 2D key is the creation depth ([Draw order](#draw-order)), as Coney's `OverlayPass`
  defaults to; nothing to change. Note that instances of the 3D overlay world, when they come, sort before every 2D
  batch because their keys are negative.
- Names for the batch functions: the `@orig` tags call `0x00184890` `ResourceMgr_CompareOverlayKeys` and
  `0x00185cc8` `ResourceMgr_EmptyInstances` until the research database names them.
- Names for the sheet functions: the `@orig` tags call them `ChunkLoaded_ParticlePage` (`0x00181b20`),
  `ChunkLoaded_ParticlePageHeader` (`0x00182820`), `ResourceMgr_SheetRecord` (`0x001828c0`) and
  `ResourceMgr_SheetSize` (`0x00181e50`) until the research database names them.
- **`GetPlatform` on the PS2** (answered, 2026-10-04): it returns **1** (`0x00357998`), so the language files take
  their `else` branch: "PRESS THE START BUTTON" for `0x76`, triangle as "back" in `0x1f` (confirmed (code); the
  wording confirmed (runtime) in PCSX2). Coney now returns 1 as well
  ([Scripts](scripting.md#bindings-the-front-end-and-the-script-system-depend-on)). The original question:
  what does the binding return? Each language file has about twenty strings in an
  `if Platform == 2 then ... else ... end`; the two branches differ mainly in naming triangle or circle as "back"
  (`0x1f` among them). Coney returns 0 (the triangle branch, which matches [Front end](frontend.md#input)'s reading).
  Which platform is 2, and which branch does the NTSC-U game show?
- **Who runs `config_preload2.lua`, and when?** (answered): the legal screen's `Enter` (`0x00161218`), after
  `enum_preload.lua` and `config_preload.lua` and before `config_preload3.lua`, all in one Lua state
  ([Scripts](scripting.md#life-of-the-lua-state)); Coney's order matches for the two it runs. Originally: Coney runs it
  after `enum_preload.lua` to get the strings; the
  original's call site is not on this page.
- **`doFile`** (answered): it always adds `.lua` (`0x003579a0` formats `"%s.lua"`, `0x00579058`) and runs the
  result as a WAD name (script system slot `+0x34`), as Coney does. Originally: does the binding add `.lua` to a name
  without an extension (as Coney does), or look the name up some
  other way?
- **The HUD string array** at `0x00600048`: its size, and what `GlobalString_Get` does with an id past it.
- **The font a text widget starts in** (answered, [The widget classes](#widget-classes)): the light `TextWidget`
  draws in the slot its owner passes (the PM items and titles pass 6, `big_font`); the markup widget starts in slot 3
  (`part_page0`, the same sheet as Coney's slot 2) unless 6 is passed. Originally: which instance slot does a
  `TextWidget` draw with before any `<BIGFONT>`? Coney takes slot 2 (`part_page0`), the sheet whose characters
  `0x91`-`0xa0` are the button pictures (above). Still open: what the explicit base of the two
  `Font_Measure`/`Font_Draw` call sites (slot 6 with -1, slot 3 with `-'0'`) draws.
- **`<MONEYFONT>`:** the glyph base `0xd0100` (font 6) or `0xb` (font 3): which rectangles does it select? The strings
  wrap button tags in it (`<MONEYFONT><ST></MONEYFONT>`, 25 times in English); `part_page0`'s icons below its first
  glyph (fists, faces, W badges) look like its targets. Coney ignores it.
- **HUD icon tags 23-31:** their names and characters. The English strings use `<BOBJ>`, `<YOBJ>` and `<ROBJ>`, which
  are not among the names on this page.
- **Layout details** (answered in [the tag table](#markup): `CCENTER` / `RRIGHT`, `CRM`, the closing tags and
  `PULSE`'s triangle swing; and the y is the first line's centre, confirmed (runtime) for the light widget). Still
  open: `<BOLD>` and `<AUTOINDENT>`; the two characters of each animated stick tag; whether the shadow is drawn per
  glyph (Coney) or under the whole string first.
- Names: the `@orig` tags call `0x00179808`, `0x00179958` and `0x00179c30` `Font_Size`, `Font_Measure` and
  `Font_Draw`, and `0x001b9600` `TextWidget_Layout`, all with file `(unknown)`.
- **Widget geometry** (answered, [The widget classes](#widget-classes)): a sprite widget's anchor is chosen per
  widget (0 centre, 1 left edge, 2 right edge; the PM logo uses 1) and its size is in overlay units; the
  `OptionGrid`'s rows, packing, spacing and colours are listed there.
- Names: the `@orig` tags call `0x001a2690` `BaseWidget_AddSprite`, `0x001cd1e0` `TextWidget_SetText`, `0x001cec28`
  `UsageInfo_SetText`, `0x001d4110` `OptionGrid_Setup`, `0x001d4230` `OptionGrid_AddItem`, `0x001d4d28`
  `OptionGrid_TakeFocus` and the screen-flow functions `ScreenFlowController_AddTransition`, `_Push`, `_Pop`,
  `_Unwind` and `_Update`, until the research database names them.
- Names: the `@orig` tags call `0x0019ee70` `GlobalString_Get`, `0x0019eea0` `GlobalString_Set` and `0x0035e5d0`
  `CfgHUDMessage` with file `(unknown)` until the research database names them.

What the implementer still needs:

- The other instance formats (1, a 2D rotation; 2, a full matrix), the resource manager's 255 instance slots and
  the worlds an instance can live in (3D overlay, 2D overlay), where the original's pass finds its batches.
- The widget base's other slots (the rectangle, active, the text reveal) and `BaseWidget`'s timed fade.
- Text widgets around the layout: the reveal fraction, `<SOUND>` played once, `<FREEZE>` on the game timer, and
  the open tags above (`MONEYFONT` first: the menus use it around every button glyph).

## Open questions

- **Radar batch `+0x48`** and **blip type 12**: who uses them.
- **What icons 22 and 355 show**, and the dealer type 2's role.
- **The 2D sort key** (answered): the creation depth; see [Draw order](#draw-order).
- **The sheet `0x349348bd`** behind the Quick Rumble menus (sheet-table record 12, inferred;
  [Front end](frontend.md#rm-layout)): its resource name (not one of the names tried).
- **`firstGlyph` of `part_page0` (94) and `part_page1` (20)**: which text uses them, and the two explicit-base call
  sites in `Font_Measure`/`Font_Draw`.
- **`<MONEYFONT>`**: the glyph base it sets (`0xd0100` for font 6, `0xb` for font 3) looks like a packed value; how
  it is used is not worked out.
- **The message box** (answered for the layout and input: [Front end](frontend.md#message-box)): its file.
- **`OptionGrid`** (answered, [The widget classes](#widget-classes)): the owner reads the code itself
  (`0x001d43f0(grid, 0x001d43e8(grid))`). Still open: setup's `a2` (`+0xa8`) and the item flags argument (4).
- **The widget base's fields** (`+0x0c` initialised, `+0x40`, `+0x50` the input record, `+0x54`-`+0x68`) and the
  second interface at `+0x6c`.
