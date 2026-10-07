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
`hud_minigames`, **13 `big_font`**, 51 `legal_screen`, 530 `part_fog_00` and 531 `part_fog_01`
(the [3D fog](particles.md#fog)'s wisps).

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

Widget vtable slots (GCC 2 layout: a slot is an 8-byte `{s16 delta, fn}` pair, the call adds the delta at
`vtable + slot` to the object and calls the function at `vtable + slot + 4`; offsets from the vtable start), from
the screens read here; confirmed (code) for the calls, names inferred:

| Slot | Role |
| --- | --- |
| `+0x30` | `Update`: input and animation; a screen writes its result code at `+0x74` |
| `+0x38` | `Render`: add the widget's sprites |
| `+0x48` | is visible |
| `+0x60` | destructor |
| `+0x68` | `Shutdown`: release children and resources; in `BaseWidget` and `TextWidget` this slot is the setup with a rectangle, size, colour and flags, and `BaseWidget`'s shutdown is `+0x70` |
| `+0x70` | the widget's rectangle |
| `+0x88` | is active |
| `+0x90` | take focus (`OptionGrid`: `0x001d4d28`) |
| `+0x98` | drop focus |
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

**`OptionGrid`** (`0x001d3ef0`, vtable `0x0053b768`: update `0x001d4f20`, render `0x001d52c8`, is ready (slot `+0x88`) `0x001d4418`,
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

### The markup text widget's fields {#markup-text-fields}

`MessageHUD` (`0x001b8f98`, about `0x1e4` bytes), confirmed (code) at the functions of
[`GUI/MessageHUD.cpp`](#fn-messagehud):

| Offset | Type | Field | Meaning |
| --- | --- | --- | --- |
| `+0x04` | u32 | changed | set by every text change |
| `+0x0c` | u32 | created | |
| `+0x20` | vec4 | anchor | `(x, 0, y, 1)` |
| `+0x60` | char* | ownedText | a copy the widget frees on shutdown |
| `+0x68`, `+0x6c` | char* | text, originalText | the text being laid out, and the one set |
| `+0x70` | BaseWidget | icon | shown instead of text when its sprite word is set |
| `+0x170` | u32 | endTime | `<DISPLAYTIME>` end, game ms; -1 never, 0 none |
| `+0x174` | u32 | fadeTime | the prompt fade during a `<FREEZE>` |
| `+0x178` | u32 | frozen | a `<FREEZE>` is running |
| `+0x17c` | u8 | alignment | `Font_Draw` flags |
| `+0x180` | u32 | proportional | |
| `+0x184` | f32 | maxWidth | box right limit (10,000) |
| `+0x188` | u32 | allowCR2 | enables `<CR2>` and `<CRM>` |
| `+0x18c` | u8 | shadow | `0x80` |
| `+0x190` | s32 | bgFontInstance | the `<BGFONT>` batch, -1 none |
| `+0x194` | u32 | pendingSound | played once on the next draw |
| `+0x198` | s32 | glyphBase | `<MONEYFONT>`'s base, -1 none |
| `+0x19c` | u32 | fontSlot | 3 or 6 |
| `+0x1a0` | u32 | paged | draw only the line window |
| `+0x1a4`, `+0x1a8`, `+0x1ac` | u32 | firstLine, lastLine, lineCount | the line window of wrapped text |
| `+0x1b0`, `+0x1b1` | u8 | alpha, alphaOverride | the fade's alpha; a fixed alpha |
| `+0x1b4` | u32 | useAlphaOverride | |
| `+0x1b8` | u32 | wordWrap | wrap at `+0x1d4` |
| `+0x1bc` | u32 | drawn | drawn this frame |
| `+0x1c0` | f32[4] | metrics | `Font_Size` record |
| `+0x1d0` | f32 | baseSize | |
| `+0x1d4` | f32 | wrapWidth | from `<AUTOINDENT f>` or set |
| `+0x1d8` | u32 | colour | |
| `+0x1dc`, `+0x1e0` | u32, u8 | stickTimer, stickGlyph | the animated stick glyphs (`<SDD>`...) |

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
| 6 | `<BOLD>` | no effect: the layout has no case for it (confirmed (code) at `0x001b9600`) |
| 7 | `<BIGFONT>` | font slot 6 (`big_font`) |
| 8 | `<MONEYFONT>` | an icon font (glyph base set from the font) |
| 9 | `<BGFONT>` | glyphs on a background (creates an instance over sheet 0, depth 8,000) |
| 10, 11 | `<MONEYPLUS>`, `<MONEYMINUS>` | the icon characters `=` and `<` |
| 12-16 | `<CENTER>`, `<CCENTER>`, `<RIGHT>`, `<RRIGHT>`, `<LEFT>` | alignment: `CENTER` (flags 6) and `RIGHT` (5) shift the whole line by its measured width; `CCENTER` and `RRIGHT` pass the same flags to each `Font_Draw` run, aligning each run about the pen; `LEFT` is 4 |
| 17-20 | `<CR>`, `<CR2>`, `<CR3 f>`, `<CRM>` | new line; `CR2` only when the widget's `+0x188` is set and `*(s16)(W_GameState + 0x224)` < 2 (single player), `CR3` adds `f`, `CRM` only when `+0x188` and `+0x1b8` are set |
| 21 | `<AUTOINDENT f>` | not handled by the layout; at the start of a text it sets the wrap width (`+0x1d4`) to `f` (`MessageHUD_ReadAutoIndent`, `0x001bb390`) |
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
`<CENTER>` each line is centred on the box. Confirmed (code). `CRM` is the soft break `MessageHUD_WordWrap`
(`0x001bac20`) inserts when it wraps.

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

**The string cache** (`StringTable/StringTableCache.cpp`, our name `StringTableCache`) interns strings: one copy of
each distinct text, so callers can keep the returned pointer. `StringTableCache_Intern(cache, text)` (`0x00386d08`)
hashes the text (`h = h × 5 + c` over its bytes, signed), looks it up in an STL hash set (buckets at `+0x08`-`+0x0c`,
element count `+0x14`), and on a miss copies it into the `Level Dynamic & LUA Pool` (tag `char`, align 16), inserts
the copy and adds its length + 1 to the byte total `+0x18`; it returns the stored copy, or 0 for a null text. The
destructor (`0x00386b30`) frees every copy from the same pool, clears the set and frees its buckets. Callers: the
credits list (`0x001a94e8`, `0x001a8fb8`) and the Rumble character data (`0x001f1fa8`, and `0x001fdfe0`, which can
first copy the text and change its case through `0x00435e70`) into the cache at `0x0050f4f0`. Confirmed (code) at
the cited addresses.

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
code use are 7-34 texels a side (disc check). `HUD_RadarSetBlipIcon` (`0x001b2ca0`) draws icon 22 at 0.7 and tints icons
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

#### Radar blip slots {#radar-blip-slots}

A radar's fields and its 128 slots of `0x40` bytes (from radar `+0x920`). Confirmed (code) at `0x001c4d00`,
`0x001c4ff8`, `0x001c5210`, `0x001c41a0`; meanings marked inferred where the code only shows the use.

| Radar offset | Meaning |
| --- | --- |
| `+0x00` / `+0x04` | created / on (`HUDTurnOnRadar`) |
| `+0x08` / `+0x0c` | shown (`Radar_Show`) / hidden (`Radar_HideAllBlips`) |
| `+0x14` | enemy blips (types 5, 6, 8) may show when marked (inferred) |
| `+0x18` | enemy marks never time out (inferred) |
| `+0x20`, `+0x24`, `+0x28` | zoom, fast and rest radii ([HUD](hud.md#the-radar-on-screen)) |
| `+0x38`-`+0x40` | disc colour state, old state, change time |
| `+0x48`-`+0x60` | the sprite batches ([table above](#radar-icons)); `+0x48` holds icons 25 and 26 |
| `+0x70` / `+0x80` | the player's position / matrix, copied by `Radar_Update` |
| `+0x90` | which player |
| `+0x2920` / `+0x2930` | zoom scale / the disc centre |

| Slot offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | vec4 | fixed position (in-use 2) |
| `+0x10` | handle | the tracked object |
| `+0x14` | u32 | colour (`0xRRGGBBAA`) |
| `+0x18` | u32 | icon last set (`0x45` at creation) |
| `+0x20` | u32 | the batch the dot was made in |
| `+0x24` | handle | the `hud_radar_dot` |
| `+0x28` | ptr | the `BaseWidget` of a type-9 blip |
| `+0x2c` | s16 | blip type |
| `+0x2e` | s16 | mark timer, updates (300 per mark) |
| `+0x30` | u8 | shown: always for types 7, 9, 10, 12; set by a mark for the others |
| `+0x31` | u8 | in use: 1 follows its object, 2 a fixed point |
| `+0x32` / `+0x33` | u8 | objective flash countdown (100) / phase |
| `+0x34` | u32 | icon lock (`0x001c4640`) |

### The `METRICS1` file

The one "font metrics" WAD entry is **`cn12.met`** (entry 3,743), next to **`cn12.bmp`** (entry 3,742, the 256 × 128
24-bit bitmap). It is text: the line `METRICS1`, a line naming `cn12.bmp` and `mcn12.bmp`, a number, then 95 lines
`code x0 y0 x1 y1 # 'c'` for the characters 32-126: pixel rectangles of a 12-point bitmap font. **No code in the PS2
executable refers to it** (no `.met`, `.bmp` or `cn12` string), so the game does not use it; probably a leftover of a
debug or PC tool (inferred).

## Function index {#function-index}

Every function of the GUI files this page covers, by source file in address order, with the name it has in Ghidra
(ours). Rows link to the section that describes the behaviour where there is one. The files and ranges are from the
[Source map](source-map.md#gui).

### `GUI/BaseWidget.cpp` {#fn-basewidget}

`0x001a1bf8`-`0x001a29c0`: `BaseWidget` (vtable `0x005394b8`, `0x100` bytes; a second copy of its slots at
`0x0053a0d4`-`0x0053a10c` belongs to a subclass). Slot numbers are the `delta` offsets of the GCC 2 vtable
([Widgets](#widgets)).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001a1bf8`, `0x001a1c80` | `BaseWidget_Construct`, `BaseWidget_Reset` | vtable, then hidden, inactive, sprite word 0, colour 0, size 0, anchor 0, instance -1, depth 10,000 (`+0xe4`) | confirmed (code) |
| `0x001a1c30` | `BaseWidget_Destroy` | slot `+0x60`: releases; frees when the flag's bit 1 is set | confirmed (code) |
| `0x001a1db8` | `BaseWidget_Setup` | slot `+0x68`, [The widget classes](#widget-classes) | confirmed (code) |
| `0x001a1f10` | `BaseWidget_Release` | slot `+0x70`: an owned batch instance (`+0xc4`, `+0x0c` = 0) is freed, as is the shadow record `+0xec` | confirmed (code) |
| `0x001a1fc8` | `BaseWidget_EnableShadow(on)` | slot `+0x78`: allocates the `0x80`-byte shadow sprite record (`ParticleContainer`) or frees it | confirmed (code) |
| `0x001a2060` | `BaseWidget_SetPosition(pos, convert)` | slot `+0x08`: `+0x30`; converted through the device (slot `+0x90`) into the overlay position `+0x80` | confirmed (code) |
| `0x001a2118`, `0x001a2120` | `BaseWidget_SetAnchor`, `BaseWidget_SetSize(w, h, widget, convert)` | anchor `+0xc0`; size `+0x90`/`+0x94` (h = w when h ≤ 0) | confirmed (code) |
| `0x001a2190` | `BaseWidget_SetRect` | overrides the texture rectangle `+0xa4`-`+0xb0` | confirmed (code) |
| `0x001a21b8`, `0x001a21e8` | `BaseWidget_Update`, `BaseWidget_UpdateSize(mode)` | slot `+0x30` calls slot `+0x98` with 1: mode 0 sets the width from the height, 1 the height from the width, by the rectangle's pixel aspect ([Size](#widget-classes)); anchor offset `+0xd0` | confirmed (code) |
| `0x001a2538`, `0x001a25b8`, `0x001a25c0` | `BaseWidget_StartFade(widget, ms)`, `_CancelFade`, `_IsFadeDone` | the timed fade: duration `+0xf0`, end `+0xf4`; when done it hides the widget and returns 1 | confirmed (code) |
| `0x001a2638`, `0x001a2648` | `BaseWidget_SetAlpha`, `BaseWidget_SetColour` (slot `+0x20`) | alpha `+0xf8` and `+0xb7`; colour `+0xb4` | confirmed (code) |
| `0x001a2660`, `0x001a2690` | `BaseWidget_Render` (slot `+0x38`), `BaseWidget_RenderAlpha(alpha)` (slot `+0xa0`) | [The widget classes](#widget-classes) | confirmed (code) |
| `0x001a2910`, `0x001a2918` | `BaseWidget_SetSpriteWord`, `BaseWidget_SetInstance(widget, inst)` | `+0xc8`; -1 makes the widget its own batch over the sprite word's sheet (`ResourceMgr_CreateInstance`, depth `+0xe4`, flags `+0xfc`) | confirmed (code) |

### After `GUI/ChecklistMessageHUD.cpp` (no path string): the widget base {#fn-after-checklistmessagehud}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001a8e30` | `Widget_Construct` | the base of every widget (vtable `0x00539978`): `+0x04` = 1, `+0x08` = 1, `+0x0c` = `+0x10` = 0, `+0x2c` = 1.0 ([Widgets](#widgets)) | confirmed (code) |
| `0x001a8e78` | `Widget_Destroy` | slot `+0x60`; frees when the flag's bit 1 is set | confirmed (code) |
| `0x001a8ea8` | `Widget_IsBatchResident(widget, inst)` | slot `+0x80` of many widget vtables: whether a batch instance is resident (byte `+0x11`) | confirmed (code) |

### `GUI/GridContainer.cpp` {#fn-gridcontainer}

`0x001ab620`-`0x001acb90`: a grid of child widgets with d-pad navigation, used by the Rumble arena screen
(`0x001eabc0`). `GridContainer` (`0x001abad0`; vtable `0x00539b68`, input interface `0x00539b40` at `+0x7c`) holds
`GridContainerItem`s (`0x70` bytes, vtable `0x00539c90`: a child at `+0x60`, its index `+0x64`) in a vector at
`+0x84`-`+0x8c`. Fields: position `+0x20`, size `+0x30`, focused `+0x40`, input record `+0x60` (HUD player 0's),
owner `+0x90` (its handler sees every command first), last command time `+0x94`, spacing `+0xa0` (x gap `+0xa0`, y
gap `+0xa8`), vertical wrap `+0xb0`, horizontal wrap `+0xb4`, rows `+0xb8` and columns `+0xb9` (bytes), capacity
`+0xbc` = rows × columns, selected `+0xc0`, owns children `+0xc4`, last move refused `+0xc8`. Each update lays the
items in cells of width (W − gx (columns + 1)) / columns and height (H − gy (rows + 1)) / rows; the pen x advances
by a cell plus gx for **every** item and is not reset at a new row (so only one-row grids lay out as a grid); the row
is index / columns. Slots are the delta-word offsets (vtable entry − vtable − 4), as on this page's
[widget slot table](#widgets). Confirmed (code).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001ab620` | `GridContainerItem_Construct(child, index)` | widget base, vtable `0x00539c90`, scale `+0x50` 1.0, `SetChild`, initialised | confirmed (code) |
| `0x001ab6a0` | `GridContainerItem_Destroy` | slot `+0x60` | confirmed (code) |
| `0x001ab6c8` | `GridContainerItem_SetChild` | slot `+0xc0`: child, index, and the child's rectangle size into `+0x30` | confirmed (code) |
| `0x001ab718` / `0x001ab758` | `GridContainerItem_SetPosition` / `_SetSize` | own position `+0x20` / size `+0x30`, passed on to the child (its slots `+0x08` / `+0x10`) | confirmed (code) |
| `0x001ab798` | `GridContainerItem_GetRect` | slot `+0x70`: `+0x30` with the height × the reveal (slot `+0xb0`) | confirmed (code) |
| `0x001ab7f0` / `0x001ab838` | `GridContainerItem_Focus` / `_Unfocus` | slots `+0x90` / `+0x98`: the child's, and `+0x40` | confirmed (code) |
| `0x001ab8e0` | `GridContainerItem_IsActive` | slot `+0x88`: a child, initialised, child active | confirmed (code) |
| `0x001ab930` | `GridContainerItem_Update` | slot `+0x30`: child position, size and update; recentres itself vertically on the child's height; visible = reveal > 0 | confirmed (code) |
| `0x001aba58` | `GridContainerItem_Render` | slot `+0x38`: the child's render when active and visible | confirmed (code) |
| `0x001abad0` | `GridContainer_Construct` | widget base, the two vtables, empty vector | confirmed (code) |
| `0x001abb70` | `GridContainer_Destroy` | slot `+0x60`: frees the vector storage | confirmed (code) |
| `0x001abbf8` | `GridContainer_Shutdown` | slot `+0x68`: unfocus, delete the items (and their children's shutdown and delete when `+0xc4`), empty the vector | confirmed (code) |
| `0x001abd30` | `GridContainer_Setup(pos, size, spacing, rows, columns, owner, ownsChildren)` | slot `+0xc0`: the fields above, selection 0, wraps off, initialised | confirmed (code) |
| `0x001abe10` | `GridContainer_AddItem(child, index)` | slot `+0xc8`: below capacity, a new item appended | confirmed (code) |
| `0x001abee8` | `GridContainer_IsActive` | slot `+0x88`: initialised and every item active | confirmed (code) |
| `0x001abf60` | `GridContainer_IsSelectable(i)` | slot `+0xe0`: item `i` has a child whose slot `+0x58` test is true | confirmed (code) |
| `0x001ac000` / `0x001ac0e8` | `GridContainer_MoveUp` / `_MoveDown` | slots `+0xe8` / `+0xf0`: step by the column count to the next selectable item (over the capacity, with `+0xb0` wrap, else stay); `Select(i, 1)` when it changed | confirmed (code) |
| `0x001ac1d8` / `0x001ac2f8` | `GridContainer_MoveLeft` / `_MoveRight` | slots `+0xf8` / `+0x100`: step by 1 (wrap to the other end with `+0xb4`, else stay); no move: cue `0xe` and `+0xc8` = 1 | confirmed (code) |
| `0x001ac400` | `GridContainer_Select(i, sound)` | slot `+0x108`: `i` clamped to the last item; when selectable, unfocus the old, focus `i`, cue 4 if `sound`; clears `+0xc8` | confirmed (code) |
| `0x001ac4e8` | `GridContainer_GetSelectedChild` | slot `+0xd8` | confirmed (code) |
| `0x001ac528` | `GridContainer_OnCommand(cmd)` | interface slot `+0x08`: the owner's handler first; else 0 up, 1 down, 2 left, 3 right; stamps `+0x94`; returns 1 | confirmed (code) |
| `0x001ac620` / `0x001ac6b0` | `GridContainer_Focus` / `_Unfocus` | slots `+0x90` / `+0x98`: clears the input record (`0x00146000`), `+0x40`, the selected item's focus, stamps `+0x94` | confirmed (code) |
| `0x001ac7d0` | `GridContainer_GetRect` | slot `+0x70` | confirmed (code) |
| `0x001ac828` | `GridContainer_Update` | slot `+0x30`: when focused and 20 ms after the last command, the d-pad (mask `0xf000`) by the repeating query `0x001e93c0`, or the plain one `0x001e9518` after a refused move, then buttons (`0x001e9468`, mask `0xffff0fff`), input cleared; lays out and updates the items; recentres; visible = reveal > 0 | confirmed (code) |
| `0x001acb90` | `GridContainer_Render` | slot `+0x38`: every item's render when active and visible | confirmed (code) |

### `GUI/MessageHUD.cpp` {#fn-messagehud}

`0x001b8f98`-`0x001bb4a0`: the markup text widget (class `MessageHUD`, vtable `0x0053a248`; its path string is
passed by `MessageHUD_Shutdown`), the base of `UsageInfo`, the hint texts, the counter texts and message boxes. Its
fields are in [The markup text widget's fields](#markup-text-fields); the tags in [Markup tags](#markup).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001b8f98` | `MessageHUD_Construct` | the markup text widget (vtable `0x0053a248`): widget base, box `BaseWidget` at `+0x70`, proportional `+0x180` = 1, font slot `+0x19c` = 3, active `+0x54` = 1, `+0x50` = 1.0 | confirmed (code) |
| `0x001b9040` | `MessageHUD_Destroy` | slot `+0x60`: the box's destructor, then the widget base's | confirmed (code) |
| `0x001b9090` | `MessageHUD_Setup` | slot `+0xc0` ([The widget classes](#widget-classes)): box sprite (depth 11,000), right limit `+0x184` = 10,000, reveal 1.0, shadow `+0x18c` = `0x80`, base size 1.0, font slot 6 or 3, `Font_Size` metrics `+0x1c0`, colour `+0x1d8` | confirmed (code) |
| `0x001b9288` | `MessageHUD_SetBaseSize` | `+0x1d0` = size | confirmed (code) |
| `0x001b9290` | `MessageHUD_Shutdown` | slot `+0x68`: when created, releases the icon sprite (`+0x70`), frees the owned text copy (`+0x60`), releases the `<BGFONT>` instance (`+0x190`), hides the widget and turns word wrap (`+0x1b8`) off | confirmed (code) |
| `0x001b9370` | `MessageHUD_SetPosition` | slot `+0x08`: the anchor (`+0x20`), and moves the icon sprite with it | confirmed (code) |
| `0x001b93a0` | `MessageHUD_GetIconSprite` | the icon sprite's word (`+0x138`); non-zero means the widget shows a sprite instead of text | confirmed (code) |
| `0x001b93a8` | `MessageHUD_Measure` | word-wraps into a scratch copy when `+0x1b8` is set, then runs the layout in measure mode; returns the box `(x0, y0, x1, y1)` | confirmed (code) |
| `0x001b9478` | `MessageHUD_SetAlignment` | byte `+0x17c`, passed to `Font_Draw` as its alignment flags | confirmed (code) |
| `0x001b9480` | `MessageHUD_SetMetrics` | the `Font_Size` record (`+0x1c0`) and the proportional flag (`+0x180`) | confirmed (code) |
| `0x001b94d0`, `0x001b94d8` | `MessageHUD_SetColour`, `MessageHUD_GetColour` | the base colour `+0x1d8` | confirmed (code) |
| `0x001b94e0` | `MessageHUD_GetScaledMetrics` | the four metrics times the base size (`+0x1d0`) | confirmed (code) |
| `0x001b9548` | `MessageHUD_Update` | slot `+0x30`: updates the icon sprite when there is no text; visible (slot `+0x40`) while the reveal (slot `+0xb0`) is above 0 | confirmed (code) |
| `0x001b9600` | `MessageHUD_Layout` | the markup interpreter ([Text](#text), [Markup tags](#markup)). Measure mode makes one pass, draw mode two (the first fixes each line's width for `<RIGHT>` and `<CENTER>`). Tags 6 (`<BOLD>`) and 21 (`<AUTOINDENT>`) have no case here: `<BOLD>` does nothing and `<AUTOINDENT f>` is read by `MessageHUD_ReadAutoIndent`. While a `<FREEZE>` is running, text after the prompt line fades over 334 ms. The box's right edge is capped at `+0x184` × reveal | confirmed (code) |
| `0x001bac20` | `MessageHUD_WordWrap` | splits the text at spaces, measures each line with its tags stripped, and inserts `<CRM>` before the word that takes the line past the wrap width (`+0x1d4`); the line count goes to `+0x1ac` | confirmed (code) |
| `0x001baee0` | `MessageHUD_ClipToPage` | keeps only the wrapped lines from `+0x1a4` to `+0x1a8` (split at `<CRM>`): a scrolling page of a long text | confirmed (code) |
| `0x001bafd0`, `0x001bb000` | `MessageHUD_PageUp`, `MessageHUD_PageDown` | move that line window by one line; false at the top or at the last line (`+0x1ac`) | confirmed (code) |
| `0x001bb038` | `MessageHUD_Draw` | when visible, active and not expired: draws the icon, or copies the text, wraps it, clips it to the page (when `+0x1a0` is set), plays a pending `<SOUND>` (`+0x194`) and lays it out in draw mode; sets `+0x1bc` (drawn this frame) | confirmed (code) |
| `0x001bb1c0` | `MessageHUD_Render` | slot `+0x38`: `MessageHUD_Draw` in the widget's colour | confirmed (code) |
| `0x001bb1e0` | `MessageHUD_SetIcon` | drops the text and shows a sprite of the given word in its own batch; clears the end time and the freeze | confirmed (code) |
| `0x001bb250` | `MarkupText_SetText` | points `+0x68` and `+0x6c` at the text (not copied), clears the icon, the end time, the freeze and the stick-glyph timer (`+0x1dc`) | confirmed (code) |
| `0x001bb2b0` | `MessageHUD_ClearText` | clears the icon, the owned text, the end time and the freeze ([The HUD's frame](hud.md#the-huds-frame)) | confirmed (code) |
| `0x001bb2f0` | `MarkupText_IsExpired` | true once the `<DISPLAYTIME>` end (`+0x170`, never for -1) has passed, or a `<FREEZE>` (`+0x178`) is over | confirmed (code) |
| `0x001bb390` | `MessageHUD_ReadAutoIndent` | the wrap width (`+0x1d4`): the number after a leading `<AUTOINDENT f>`, or the width given | confirmed (code) |
| `0x001bb440` | `MessageHUD_SetDisplayTime` | end time = now + ms (-1 leaves it), freeze off | confirmed (code) |
| `0x001bb4a0` | `Hud_GreyLeadingZeros` | rewrites a digit string with its leading zeros in HUD colour tag 5 and the rest in tag 0: the grey zeros of the score ([Score and money](hud.md#score-and-money)) | confirmed (code) |

### After `GUI/MissionSelectHUD.cpp` (no path string): `MultiLineTextWidget` and the HUD's bars {#fn-multilinetext}

A translation unit without a path string runs from `0x001c16a8` to its static-init stub `0x001c3dc0`. Besides the
HUD pieces in [HUD](hud.md#fn-hud-bars-panels), it holds `MultiLineTextWidget` (vtable `0x0053a550`, class string
of the PM and Rumble screens' allocations): plain text (no markup) wrapped into at most 10 lines, used by the
captions, the error screen and the profile and Rumble dialogs.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c16a8`, `0x001c16f0` | `MultiLineText_Construct`, `MultiLineText_Destroy` | constructor and destructor (slot `+0x60`) | confirmed (code) |
| `0x001c1718` | `MultiLineText_Shutdown` | slot `+0x68`: no lines, not created | confirmed (code) |
| `0x001c1738` | `MultiLineText_Setup` | slot `+0x90` `(scale, widget, position, colour, flags, fontSlot)`: metrics `Font_Size(scale)` (`+0xa0`), colour `+0xb4`, `Font_Draw` flags `+0xb8`, font slot `+0xbc`, shadow `0x80` (`+0xc0`), wrap width 10,000 (`+0x9c`) | confirmed (code) |
| `0x001c1800` | `MultiLineText_SetScale` | slot `+0x98`: new metrics, relaid out on the next update | confirmed (code) |
| `0x001c1870` | `MultiLineText_WordLength` | the length of the word at a position (to a space, the end or a `<`) | confirmed (code) |
| `0x001c18c0` | `MultiLineText_SetText` | slot `+0xa0` `(width, widget, text, force)`: text `+0x40` (not copied), wrap width `+0x9c` | confirmed (code) |
| `0x001c1900` | `MultiLineText_Layout` | greedy word wrap: words are added while the line stays within `+0x9c` (a word wider than the width gets a line of its own); `<CR>` forces a break; at most 10 lines, each a `(start, end)` span at `+0x48`; box width `+0x30` = the widest line, height `+0x38` = lines × (`h` + `lineGap`) | confirmed (code) |
| `0x001c1ae0` | `MultiLineText_IsLoaded` | slot `+0x88`: created and its font batch resident | confirmed (code) |
| `0x001c1b10` | `MultiLineText_Update` | slot `+0x30`: relayout when the text, scale or width changed | confirmed (code) |
| `0x001c1b58` | `MultiLineText_Render` | slot `+0x38`: one `Font_Draw` per line at `+0x20`'s x; the block is centred on y (`+0x44` = 0) or ends at y (`+0x44` ≠ 0) | confirmed (code) |

### `GUI/RadarHUD.cpp` {#fn-radarhud}

`0x001c41a0`-`0x001c6878`: the two radars (HUD `+0x15d0` and `+0x3f10`, [The radar](#radar-icons)); a radar is a
plain struct, not a widget, with 128 blip slots of `0x40` bytes from `+0x920` (layout in
[Radar blip slots](#radar-blip-slots)). The map disc is drawn by `Radar_Render` ([HUD](hud.md#the-radar-on-screen)).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c3de0` | `Radar_Setup` | from `HUD_InitLevel`: the radar's sprite batches and map texture ([The radar](#radar-icons)); the first function after the stub `0x001c3dc0`, so the start of `RadarHUD.cpp` (inferred) | confirmed (code) |
| `0x001c41a0` | `Radar_Shutdown` | from `HUD_ShutdownLevel`: resets all 128 slots (frees type-9 widgets, handle to none, colour, type, flags 0), releases the seven sprite batches `+0x48`-`+0x60`; `+0x10` = 1, created `+0x00` = 0 | confirmed (code) |
| `0x001c42e0` | `Radar_IsCreated` | returns `+0x00` | confirmed (code) |
| `0x001c42e8` | `Radar_HideAllBlips` | when created: hidden `+0x0c` = 1 and every used slot's dot (`0x001e9b38`) or widget (vtable `+0x40`, 0) hidden | confirmed (code) |
| `0x001c4388`, `0x001c4448` | `Radar_Show`, `Radar_Hide` | only while the radar is on (`+0x04`): show or hide every used slot's dot or widget; show also clears `+0x0c` and sets shown `+0x08`, hide clears `+0x08` | confirmed (code) |
| `0x001c4500` | `Radar_SetTintState` | the disc colour state ([HUD](hud.md#the-radar-on-screen)) | confirmed (code) |
| `0x001c4528`, `0x001c45a0` | `Radar_SetBlipMode`, `Radar_SendSlotMode` | find the handle's slot on a layer, then send its dot message `0x19` with the mode ([Blip modes](#radar-icons)) | confirmed (code) |
| `0x001c4640` | `Radar_SetBlipIconLock` | layer 0 slot `+0x34` = the value; while it is set `Radar_SetSlotIcon` and `Radar_SetBlipIcon` do nothing (its caller `HUD_RadarSetBlipIconLock`, `0x001b2bf0`, is the `flag` of `HUDSetRadarItemTexture`) | confirmed (code) |
| `0x001c4690` | `Radar_SetSlotIcon(scale, radar, slot, icon)` | unless locked: the dot's two size floats (`+0xbc`, `+0xc0`) × `scale`, message `0x11` with the icon (the dot's rectangle), slot `+0x18` = icon | confirmed (code) |
| `0x001c4768` | `Radar_SetBlipIcon(scale, radar, handle, icon, layer)` | the same by handle; a type-9 slot's widget gets the sprite word and the size instead | confirmed (code) |
| `0x001c48c0`, `0x001c4960`, `0x001c49d0` | `Radar_SetBlipColour`, `Radar_SetSlotColour`, `Radar_ChangeBlipColour` | the dot's colour (`0x001e9ac0`) and slot `+0x14`, by handle and layer, by slot, or by handle on layer 0 | confirmed (code) |
| `0x001c4a40` | `Radar_SetBlipType(radar, handle, type)` | changing to type 9 makes the slot's `BaseWidget` (size 1, depth 20,000, colour `0x005fd310`, sprite `0x16a` = 362, batch 2); leaving type 9 frees it; slot `+0x2c` = type | confirmed (code) |
| `0x001c4be0` | `Radar_MarkBlip` | an enemy seen by the scanner: re-sends the slot's colour on a first mark, mark timer `+0x2e` = 300 updates, marked `+0x30` = 1 | confirmed (code) |
| `0x001c4c88` | `Radar_IsObjectiveBlip` | layer 0 slot type is 10 | confirmed (code) |
| `0x001c4d00` | `Radar_AddBlip` | [The radar](#radar-icons); also records the batch at `+0x20`, icon `0x45` at `+0x18`, flash counter `+0x32` = 100 and phase `+0x33` = 2; types 7, 9, 10 and 12 get `+0x30` = 1 (always shown) | confirmed (code) |
| `0x001c4ff8` | `Radar_FreeSlot` | frees the widget, kills the dot (vtable `+0x4c`) in task phase 0, resets every field | confirmed (code) |
| `0x001c5108` | `Radar_RemoveHandle` | frees the handle's slot when found | confirmed (code) |
| `0x001c5158` | `Radar_FindFreeSlot` | first slot whose in-use byte `+0x31` is 0, or -1 | confirmed (code) |
| `0x001c5188` | `Radar_FindSlot(radar, handle, layer)` | the (`layer` + 1)-th slot holding the handle, or -1 | confirmed (code) |
| `0x001c5210` | `Radar_Update(radar, playerMatrix)` | per frame from `HUD_Update`; below the table | confirmed (code) |
| `0x001c60b0` | `Radar_Render` | the map disc ([HUD](hud.md#the-radar-on-screen)) | confirmed (code) |
| `0x001c6730` | `Radar_MoveSlotToBatch48` | recreates a slot's dot in batch `+0x48`, keeping its size and icon; `Radar_Update` does it for icons 25 and 26 (so batch `+0x48`, depth 10,000, holds those two) | confirmed (code) |
| `0x001c6878` | `Radar_GetBlipPosition(out, radar, handle)` | the layer-0 dot's world position (its vtable `+0xac`); the instruction arrow uses it | confirmed (code) |

**`Radar_Update`** (`0x001c5210`), each frame while the radar is created and on, confirmed (code):

- The player's position and matrix are copied to `+0x70` and `+0x80`.
- A slot in use with `+0x31` = 1 follows its object (position from the handle's vtable `+0xac`; a dead handle frees the
  slot); `+0x31` = 2 is a fixed point (slot `+0x00`).
- **Placement:** the offset from the player in x and y is turned by the camera; at distance `d` < zoom (`+0x20`) the
  blip sits at offset × 0.12 / zoom from the disc centre (`+0x2930`, `+0x2938`), otherwise on the edge at 0.9 × 0.12
  along its direction (`0x0050e9bc`). Screen x adds, screen y subtracts the offset's second component.
- **Objectives (type 10):** when the object is more than 1 m (`0x0050e9ac`) above or below the player and within
  20 m (`0x0050e9b8`, |dx| + |dy|), icons 27 and 28 are re-coloured and drawn at 0.9 (`0x0050e9b0`), icon 22 at 1.2
  (`0x0050e9b4`), icons 74 and 75 left alone; the first time in a story level with hints on (`W_GameState + 0x56e2`,
  not an Armies of the Night level), hint `0x16` is queued (game flag `0x10000000`). A new objective blip flashes for
  100 updates (alpha toggled every 4) before it is drawn solid.
- **Enemies (types 5, 6, 8):** inside the zoom they show only while the radar is shown, not hidden, its `+0x14` is
  set and the slot is marked (`+0x30`). A police blip (type 8) within 0.9 × zoom of a story player whose brain is
  type 1, while marked or `+0x18` is set, queues hint 8 once (flag `0x200`; not in levels 80 and 99). The mark timer
  `+0x2e` counts down each update; at 0 the mark is cleared and the dot hidden unless `+0x18` is set.
- **The player arrow (type 9):** the widget sits at the blip position, size 0.026 (`0x0050e9cc`), turned by the
  difference between the camera's heading and the radar's; with two players and two views player 0's icon `0x16b`
  becomes `0x169` or `0x15f`, player 1's `0x16a` `0x168` or `0x15e` (by `W_GameState + 0x90`); icons 25, 26 and those
  four are not turned; size 0.035 for 25 and 26, 0.03 otherwise.
- Icons 25 and 26 are moved to batch `+0x48` when not already there (`0x001c6730`).

### `GUI/ScreenFlowController.cpp` {#fn-screenflowcontroller}

`0x001c7e80`-`0x001c8710`: the screen stack the profile manager and the Rumble menu run on; its methods are reached
through an interface vtable (`0x0053a9d8`, reused by `RM_Controller` and `PM_Controller`).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c7e80` | `ScreenFlowController_Construct` | allocates the state stack (`SFC_States`, a list) at `+0` and the shared-data map (`SFC_SharedData`) at `+4` | confirmed (code) |
| `0x001c8010` | `ScreenFlowController_AddTransition` | `(flow, state, code, next)`: state's map (state `+8`) entry `code` → `next` ([Screen flow](#screen-flow)) | confirmed (code) |
| `0x001c80c8` | `ScreenFlowController_PushState` | the vtable entry for push | confirmed (code) |
| `0x001c80e8` | `ScreenFlowController_Push` | `Exit` the old top, push, `Enter(flow)` the new one | confirmed (code) |
| `0x001c81e8` | `ScreenFlowController_UnwindTo` | `Exit` every state, erase those above the target, `Enter` the target | confirmed (code) |
| `0x001c82e8` | `ScreenFlowController_Pop` | `Exit` and free the top, `Enter` the new top | confirmed (code) |
| `0x001c83c8` | `ScreenFlowController_Update` | the top's `Update`: `-0x100` stay, `-0xff` pop, else follow the map (unwind or push); true when empty | confirmed (code) |
| `0x001c8590` | `ScreenFlowController_Destroy` | frees the stack and the shared-data map | confirmed (code) |
| `0x001c8628` | `ScreenFlowController_GetTop` | the top state, 0 when empty | confirmed (code) |
| `0x001c8658` / `0x001c8710` | `ScreenFlowController_SetShared` / `_GetShared` | shared-data map: set `key` → `value`; get (0 when absent) | confirmed (code) |

### `GUI/SubTitle.cpp` {#fn-subtitle}

The caption system, the object at `0x00619570` (HUD `+0x18d30`), `0x001ca898`-`0x001cb190`
([Movies](movies.md#caption-text) has the format and drawing); the anchor `0x001cafa0` is its release. After it, the
caption pager at HUD `+0x18e60`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001ca898` | `Captions_Construct` | text widget (class `0x001c16a8`) at `+0x60`, kind `+0x50` = 3, position (0.5, 0.8) | confirmed (code) |
| `0x001ca908` | `Captions_SetPosition` | stores the position, adjusts it to the screen (`Camera_AdjustScreenPoint`), resets | confirmed (code) |
| `0x001ca950` | `Captions_Draw` | [Movies](movies.md#caption-text) | confirmed (code) |
| `0x001cab90` | `Captions_OnSubtitlesChunk` | chunk `0x51` handler: the chunk into `0x0050ea74` | confirmed (code) |
| `0x001cabc0`, `0x001cacc0`, `0x001cad38` | `Captions_Init`, `_SelectLanguage`, `_SelectScene` | [Movies](movies.md#caption-text) | confirmed (code) |
| `0x001cac78` | `Captions_Reset(captions, keep)` | clears the current caption (`+0x14`, `+0x1c`, the name `+0x24`, `+0x44`, `+0x48`) | confirmed (code) |
| `0x001cae58` | `Captions_IsFreezing` | returns `+0x58` (a kind-2 caption, [Boot](boot.md#timers)) | confirmed (code) |
| `0x001cae60` | `Captions_WaitTitleCard` | [Scenes](scenes.md#title-card) | confirmed (code) |
| `0x001cafa0` | `Captions_Release` | from `WorldLevel_Release`: frees the Subtitles chunk, resets, releases the text widget | confirmed (code) |
| `0x001cb000`, `0x001cb008` | `Captions_SetHoldFlag`, `Captions_GetHoldFlag` | `+0x4c` (scene caption event 6) | confirmed (code) |
| `0x001cb010`, `0x001cb190` | `Captions_SetKind`, `Captions_Next` | [Movies](movies.md#caption-text) | confirmed (code) |
| `0x001cb2a8` | `CaptionPager_Init` | once: screen effect (1.0, type 1, `ScreenFx_Queue`), `W_GameState + 0x438` = 1, `Captions_Init`, player 1's record bound | confirmed (code) |
| `0x001cb340` | `CaptionPager_OnCommand` | command 1 shows the next caption (`Captions_Next`) and stamps the time `+0x70` | confirmed (code) |
| `0x001cb398` | `CaptionPager_Update` | 250 ms after the last step: the pad pass with mask `0xf000`, then clears the input record | confirmed (code) |
| `0x001cb418` | `CaptionPager_Render` | checks readiness only; draws nothing | confirmed (code) |

### `GUI/TextEntryPad.cpp` {#fn-textentrypad}

The name keyboard of PM_Create and the Rumble gang name ([Front end](frontend.md#pm-screens)); vtable `0x0053acc8`,
input interface `0x0053aca0`; parts allocated with the file's tag: an `OptionGrid` (`+0x70`), a `UsageInfo` (`+0x74`)
and a `TextWidget` (`+0x98`); the name buffer at `+0x7d`, its limit `+0x7c` = 25 bytes (PM_Create checks 8 itself).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001cc1a0`, `0x001cc210` | `TextEntryPad_Construct`, `TextEntryPad_Destroy` | widget base and vtables | confirmed (code) |
| `0x001cc240` | `TextEntryPad_Shutdown` | shuts down and frees the grid, the text and the usage line | confirmed (code) |
| `0x001cc2f0` | `TextEntryPad_Setup(pad, pos, ?, ?, onDone, owner, rumble)` | name text (size 1.2, font 3; size 2.0, font 6 for the profile style) at y `0x0050eb04` / `0x0050eb08`; usage line (string `0x1a` centred, or `0x1f` left-aligned); grid at y `0x0050eb0c` / `0x0050eb10`, rows of 12, cue 7, row gap 0.013, one item per character of string `0x97` (a space as size 0.94 code `0x20`), then `0x99` (OK) and `0x9a` (DEL); colour `0x005fd320` (`0x005fd328` profile style) | confirmed (code) |
| `0x001cc998` | `TextEntryPad_SetText` | the buffer and the shown name | confirmed (code) |
| `0x001cca08` | `TextEntryPad_IsReady` | grid and usage line ready | confirmed (code) |
| `0x001cca80` | `TextEntryPad_OnCommand` | the owner's handler (`+0x78`) first; then on accept: OK with a name that is not all spaces plays `0xb` and calls `onDone(name)`, otherwise `0xe`; DEL removes the last character (`0xc`); a character is appended (an `_` item types a space, `0xa`) or refused at the limit (`0xe`), and at the limit the cursor jumps to OK; commands 0 and 1 on items 8 and 32 also jump to OK (inferred: the grid's edges) | confirmed (code) |
| `0x001ccd08`, `0x001ccd90` | `TextEntryPad_Update`, `TextEntryPad_Render` | grid, text and usage line | confirmed (code) |

### After `GUI/TextEntryPad.cpp` (no path string): `TextWidget` {#fn-after-textentrypad}

`TextWidget` ([The widget classes](#widget-classes)), vtable `0x0053ae78`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001ccf88`, `0x001ccfc0` | `TextWidget_Construct`, `TextWidget_Destroy` | vtable, reset; delete frees when bit 0 | confirmed (code) |
| `0x001ccff0` | `TextWidget_Reset` | empty text, invisible, inactive, shadow `+0xc4` = `0x80`, reveal `+0xc0` = 1.0, set-up `+0x58` = 0 | confirmed (code) |
| `0x001cd060` | `TextWidget_Setup` | [The widget classes](#widget-classes); set-up `+0x58` = 1 | confirmed (code) |
| `0x001cd180`, `0x001cd188` | `TextWidget_IsSetUp`, `TextWidget_Release` | read and clear `+0x58` | confirmed (code) |
| `0x001cd190` | `TextWidget_SetPosition` | `+0x60` | confirmed (code) |
| `0x001cd1b0`, `0x001cd1b8`, `0x001cd1c0` | `TextWidget_IsActive`, `_SetActive`, `_SetVisible` | `+0x54`, `+0x54`, `+0x50` | confirmed (code) |
| `0x001cd1c8`, `0x001cd1d8` | `TextWidget_SetColour`, `TextWidget_SetAlpha` | colour `+0x5c`; its alpha byte `+0x5f` | confirmed (code) |
| `0x001cd1e0` | `TextWidget_SetText` | copies the text to `+0x04`, line height `+0xb4` = metrics `+0x7c` + `+0x84`, width `+0xb0` by `Font_Measure` | confirmed (code) |
| `0x001cd280` | `TextWidget_Update` | empty | confirmed (code) |
| `0x001cd288` | `TextWidget_Render` | visible, active and not empty: `Font_Draw` at `+0x60`; with mode3D (`+0x74`) the text is placed in 3D at the widget's world position (`0x0017a1b0`, font 5) | confirmed (code) |

### `UsageInfo` (`GUI/TutorialHUD.cpp`) {#fn-usageinfo}

The button legend line ([The widget classes](#widget-classes)): vtable `0x0053b0f0`, `0x2d0` bytes, a markup text
widget at `+0x40`, a 0x95-byte text buffer at `+0x230`, its rectangle at `+0x30`. It sits in `TutorialHUD.cpp`'s TU
(before its stub `0x001cfea0`; inferred). Slots are offsets from the vtable start minus 4 (GCC 2 `{delta, fn}`).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001cea70` | `UsageInfo_Construct` | widget base, the markup text widget | confirmed (code) |
| `0x001ceab0`, `0x001ceb00` | `UsageInfo_Destroy`, `UsageInfo_Shutdown` | slots `+0x60`, `+0x68`; shutdown clears the text and the created flag `+0x0c` | confirmed (code) |
| `0x001ceb40` | `UsageInfo_Setup(info, pos, leftAlign)` | once: text at `pos`, size 1.0, colour `0x005fd310`, font slot 3, alignment 0 (left) when `leftAlign`, else 2 (centred); first text string `0x1a`; base size 1.0; stores the measured rectangle | confirmed (code) |
| `0x001cec28` | `UsageInfo_SetText` | copies at most `0x95` bytes and sets them; nil is ignored | confirmed (code) |
| `0x001cec78` | `UsageInfo_SetColour` | slot `+0x20`: the text colour (`0x001b94d0`) | confirmed (code) |
| `0x001cec98` | `UsageInfo_SetAlpha(info, a)` | text `+0x1b1` = `a`, `+0x1b4` = 1 (alpha override); the menus' fades use it | confirmed (code) |
| `0x001cecb0` | `UsageInfo_SetPosition` | slot `+0x08` | confirmed (code) |
| `0x001cecd0` | `UsageInfo_GetRect` | slot `+0x70`: `(x, 0, height, w)` of the measured text | confirmed (code) |
| `0x001ced38` | `UsageInfo_IsCreated` | slot `+0x88`: `+0x0c` | confirmed (code) |
| `0x001ced40` / `0x001ced88` | `UsageInfo_Update` / `UsageInfo_Render` | slots `+0x30` / `+0x38`: update the text when ready; draw it when visible and ready | confirmed (code) |

### `CircledText` (no path string) {#fn-circledtext}

`0x001d0aa8`-`0x001d16e8`, in the TU that ends at the stub `0x001d2de0` with the `GameMenu` base. A widget class
(vtable `0x0053b390`, `0x5fc` bytes; our name) drawing a text after up to two **circled characters**: a back sprite
(`+0x140`), two circle sprites (`+0x250` + `0x100` × i) each with a one-character `TextWidget` (`+0x450` + `0xd0` ×
i), and the main `TextWidget` (`+0x60`). The mission-select and game-stats screens build it; the allocator tag
`CircledTextHeader` they pass is probably its owner's class (inferred). Fields: circle width/height `+0x5f0`/`+0x5f4`,
circle count `+0x5f8`, text offset `+0x30`, circle offset `+0x240`, colours `+0x130` (normal, `0x005fd320`) and `+0x134`
(focused, `0x005fd318`), reveal `+0x50`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001d0aa8` / `0x001d0b70` | `CircledText_Construct` / `_Destroy` | the parts above; slot `+0x60` | confirmed (code) |
| `0x001d0c68` | `CircledText_Shutdown` | slot `+0x68`: releases every part | confirmed (code) |
| `0x001d0d18` | `CircledText_Setup` | slot `+0xc0` `(circleW, circleH, owner, widget, pos, textOff, circleOff, metrics, backSprite, circleSprite, backInst, circleInst)`, once: text font flags 4; back sprite size 1.0, depth 8,400, grey (128, 128, 128); circles size 0.1, depth 8,500, red (160, 16, 16), sized `circleW` × `circleH`; spacing divisor `0x0050ec64` = 3 (2 with device flag `0x02`); character y offset `0x0050ec68` = `circleH` × 0.1; reveal 1.0 | confirmed (code) |
| `0x001d0fe8`, `0x001d1008` | `CircledText_SetText`, `_SetCircled` | slots `+0xc8`, `+0xd0`: the main text; one character of the string per circle, count = its length | confirmed (code) |
| `0x001d1098`, `0x001d1108`, `0x001d1178` | `_SetCircleColour`, `_SetCharColour`, `_SetBackColour` | slots `+0xd8`, `+0xe0`, `+0xe8` | confirmed (code) |
| `0x001d1198` | `CircledText_IsReady` | slot `+0x88`: created and the circle batch (`+0x204`) resident | confirmed (code) |
| `0x001d11c8` / `0x001d11f8` | `CircledText_SetReveal` / `_GetRect` | slots `+0xa8` / `+0x70`: reveal clamped to 0-1; the rectangle with its height × the reveal | confirmed (code) |
| `0x001d1250` | `CircledText_Update` | slot `+0x30`: the back sprite over the rectangle, its texture rectangle cut by the reveal; circles left to right from the left edge + `circleOff`, each step `circleW` / 3, their height × the reveal; the main text after the last circle; visible while the reveal is above 0 | confirmed (code) |
| `0x001d16e8` | `CircledText_Render` | slot `+0x38`: back sprite, text (focused colour when slot `+0xa0` is true), each circle and its character (recoloured to `+0x134` unless its colour is the colour-table entry 0) | confirmed (code) |

### `OptionGridItem` (`GUI/OptionGrid.cpp`) {#fn-optiongriditem}

`0x001d3bb0`-`0x001d3f78`, after the stub `0x001d3b90`, so the start of `OptionGrid.cpp`'s TU (inferred). The grid
item (vtable `0x0053b818`, `0x60` bytes; [OptionGrid](#widget-classes)) wraps one widget (`+0x50`) and a code
(`+0x54`); `+0x40` is focused, `+0x20` the position, `+0x30` the widget's rectangle.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001d3bb0` / `0x001d3c18` | `OptionGridItem_Construct` / `_Destroy` | from `OptionGrid_AddItem` (`0x001d4230`): widget, code, created; slot `+0x60` | confirmed (code) |
| `0x001d3c40` / `0x001d3c90` | `OptionGridItem_SetWidget` / `_SetPosition` | keep the widget and copy its rectangle; set the position and forward it (widget slot `+0x08`) | confirmed (code) |
| `0x001d3cd0` / `0x001d3d18` | `OptionGridItem_Focus` / `_Unfocus` | slots `+0x90` / `+0x98`: forward and set `+0x40` | confirmed (code) |
| `0x001d3d60` / `0x001d3d98` | `OptionGridItem_SetColour` / `_GetColour` | slots `+0x20` / `+0x28`: forward; without a widget the colour is `0x005fd310` | confirmed (code) |
| `0x001d3dd8` | `OptionGridItem_GetCode` | `+0x54`, read by `0x001d43f0` | confirmed (code) |
| `0x001d3de0`, `0x001d3e30`, `0x001d3e90` | `OptionGridItem_IsReady`, `_Update`, `_Render` | slots `+0x88`, `+0x30`, `+0x38`: forward to the widget when it is ready | confirmed (code) |
| `0x001d3ef0` / `0x001d3f78` | `OptionGrid_Construct` / `_Destroy` | vtable `0x0053b768`, input interface `0x0053b740` at `+0x6c`; the item array `+0x74` freed on destruction | confirmed (code) |

### `GUI/OptionGrid.cpp` {#fn-optiongrid}

`0x001d4000`-`0x001d5808`: `OptionGrid` (vtable `0x0053b768`, input interface `0x0053b740`) holding up to 50
`OptionGridItem`s (0x60 bytes) that each wrap an `OptionGridTextWidget` (vtable `0x0053b8c8`, 0x200 bytes).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001d4000` | `OptionGrid_Shutdown` | Slot +0x68: when created (+0x0c), unfocuses, frees every item's text widget and OptionGridItem and empties the vector (+0x74..+0x78). | confirmed (code) |
| `0x001d4110` | `OptionGrid_Setup` | Position (0,0,y,1), up to 5 per-row counts at +0xb0, owner +0x80, HUD pad 0 at +0x50, centre x 0.5, move cue 4, wrap on, row gap 0, selection 0. | confirmed (code) |
| `0x001d4200` | `OptionGrid_SetPad` | Input record +0x50 = `HUD_GetVirtualPad(hud, player)`. | confirmed (code) |
| `0x001d4230` | `OptionGrid_AddItem` | At most 50 items: allocates and sets up an OptionGridTextWidget, wraps it in an OptionGridItem with the item code, appends it. | confirmed (code) |
| `0x001d43e8`, `0x001d43f0` | `OptionGrid_GetSelected`, `OptionGrid_GetItemCode` | Selected index +0xa4; item i's code (OptionGridItem +0x54). | confirmed (code) |
| `0x001d4418` | `OptionGrid_IsReady` | Slot +0x88: 1 once every item is ready (item slot +0x88), latched in +0x10. gui.md's "active" name for this address is this readiness check. | confirmed (code) |
| `0x001d44b8` | `OptionGrid_IsSelectable` | Item i exists and its text widget is active (slot +0x58). | confirmed (code) |
| `0x001d4530`, `0x001d4570` | `OptionGrid_GetColumn`, `OptionGrid_ClampColumn` | Column of the selection within its row; a column clamped to a row's length - 1. | confirmed (code) |
| `0x001d4590`, `0x001d45c8`, `0x001d45f8` | `OptionGrid_GetRowOf`, `OptionGrid_GetRowLast`, `OptionGrid_GetRowFirst` | Row of item i, and a row's last and first item index, from the per-row counts. | confirmed (code) |
| `0x001d4628`, `0x001d4790` | `OptionGrid_MoveUp`, `OptionGrid_MoveDown` | With 2+ rows, the same column in the row above/below, wrapping, skipping unselectable items recursively; cue 0xe and +0xc4 set when the move lands on the same item. | confirmed (code) |
| `0x001d48e0`, `0x001d4a30` | `OptionGrid_MoveLeft`, `OptionGrid_MoveRight` | Previous/next selectable item: across all items (wrap when +0x98) when +0x9c = 0, else within the row; cue 0xe when nothing else is selectable. | confirmed (code) |
| `0x001d4b88` | `OptionGrid_Select` | Unfocus the old item, focus item i, play cue +0x94 when asked. | confirmed (code) |
| `0x001d4c40` | `OptionGrid_OnCommand` | Input interface slot +0x08: owner's handler (+0x80) first; if it returns 0, commands 0-3 move; stamps +0x84 with the game time. | confirmed (code) |
| `0x001d4d28`, `0x001d4db8` | `OptionGrid_Focus`, `OptionGrid_Unfocus` | Slots +0x90 / +0x98: clear the input record, focused +0x40 = 1 / 0, (un)focus the selected item, stamp +0x84. | confirmed (code) |
| `0x001d4e48` | `OptionGrid_SetAlpha` | Alpha of one item (all when -1; the others 255) through the item colour slots +0x28/+0x20. | confirmed (code) |
| `0x001d4f20` | `OptionGrid_Update` | Slot +0x30: when ready, focused and 20 ms after the last command, a d-pad pass (mask 0xf000; auto-repeat, or plain after a refused move) and a button pass (0xffff0fff); then lays rows out left-packed or centred from y +0x28, row step item height + gap +0xa0; total height +0x38; fewer than 3 items never wrap. | confirmed (code) |
| `0x001d52c8` | `OptionGrid_Render` | Slot +0x38: when visible and ready, renders every item. | confirmed (code) |
| `0x001d5350`, `0x001d5398`, `0x001d53f8` | `OptionGridTextWidget_Construct`, `_Destroy`, `_Shutdown` | Text widget +0x50 and separator text widget +0x120; destructor slot +0x60; shutdown slot +0x68 releases both. | confirmed (code) |
| `0x001d5450` | `OptionGridTextWidget_Setup` | Slot +0xa8: text in `0x005fd310`; with a separator, a " : " text in the item colour after it; box width covers both. | confirmed (code) |
| `0x001d55e8` | `OptionGridTextWidget_IsCreated` | Created (+0x0c) != 0; was undefined code, created by this pass. | confirmed (code) |
| `0x001d55f8`, `0x001d56a0` | `OptionGridTextWidget_SetPosition`, `_Update` | Slot +0x08 moves text and separator (at x + text width); slot +0x30 updates both. | confirmed (code) |
| `0x001d5700` | `OptionGridTextWidget_Render` | Slot +0x38: `0x005fd318` when active and focused, else the item colour +0x1f4 with alpha +0x1f3; separator alpha 255 or the text's by +0x1f8. | confirmed (code) |

### After the Rumble result screen: `GUI/ScrollingMenu.cpp` start, file inferred {#fn-scrollingmenu-start}

`0x001e09e8`-`0x001e0a98`: the first two `ScrollingMenuItem` methods (vtable `0x0053c190`), before the file's path
string appears.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001e09e8` | `ScrollingMenuItem_Construct` | From `0x001e1830`: index +0x60, scale 1.0, alpha 255. | confirmed (code) |
| `0x001e0a58` | `ScrollingMenuItem_SetIndex` | Slot +0xf0: index +0x60, refresh (slot +0xa8). | confirmed (code) |

### `GUI/ScrollingMenu.cpp` {#fn-scrollingmenu}

`0x001e0a98`-`0x001e33c8`: the vertical list used by the Tutorial page and Rumble screens. Each entry wraps a widget in
a `ScrollingMenuItem` (vtable `0x0053c190`, 0x90 bytes; constructor `0x001e09e8` in the previous chunk) that can show
a second "description" widget under it once focused. `ScrollingTextWidget` (vtable `0x0053c2c8`) is the usual entry
widget; it has no path string of its own and is placed here by address (inferred).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001e0a98` | `ScrollingMenuItem_ReleaseDescription` | slot `+0x108`: shuts down and frees the description widget `+0x64` | confirmed (code) |
| `0x001e0af0` | `ScrollingMenuItem_Destroy` | slot `+0x60` | confirmed (code) |
| `0x001e0b18` | `ScrollingMenuItem_SetPosition` | slot `+0x128`: `+0x20`, moves the main widget `+0x60` (its slot `+0x08`) | confirmed (code) |
| `0x001e0b58` | `ScrollingMenuItem_SetReveal` | slot `+0xa8`: reveal clamped 0-1 to the main widget; the description gets the share past the main widget's part (`+0x68` / height), 0.99 or more becomes 1 | confirmed (code) |
| `0x001e0c90` | `ScrollingMenuItem_GetRect` | slot `+0x70`: main rectangle plus the shown description's height, minus 2 / screen height, plus `+0x7c`; height times the reveal | confirmed (code) |
| `0x001e0e48`, `0x001e0eb0` | `ScrollingMenuItem_Focus`, `_Unfocus` | slots `+0x90`/`+0x98`: focus stamps the time `+0x74` and sets `+0x40`; unfocus clears both widgets' focus | confirmed (code) |
| `0x001e0f18` | `ScrollingMenuItem_IsReady` | slot `+0x88`: both widgets ready | confirmed (code) |
| `0x001e0f98` | `ScrollingMenuItem_Update` | slot `+0x30`: description placed under the main widget (x plus half or full width by `+0x78`, y offset `+0x7c`); while focused and `+0x6c` ms (550) after focus, its reveal grows by `+0x70` a frame (cue `0xd` as it starts), otherwise shrinks; alpha byte `+0x80` | confirmed (code) |
| `0x001e12a0` | `ScrollingMenuItem_Render` | slot `+0x38`: main widget, then description, when visible and ready | confirmed (code) |
| `0x001e1338` | `ScrollingMenu_Construct` | 0x110 bytes, vtable `0x0053bfc8`, input interface `0x0053bfa0` at `+0x7c`; code-to-item map `+0x84` (header 0x18 bytes, tag `STL`), item vector `+0x94`-`+0xa0` | confirmed (code) |
| `0x001e1458` | `ScrollingMenu_Destroy` | slot `+0x60`: frees vector and map | confirmed (code) |
| `0x001e1550` | `ScrollingMenu_Shutdown` | slot `+0x68`: clears the map; with `+0xd4` (owns widgets) frees each item's widgets; frees the items | confirmed (code) |
| `0x001e16f0` | `ScrollingMenu_Setup` | slot `+0xc0` (pos, owner, owns): input = HUD virtual pad 0, owner `+0xcc`, delay `+0xc4` = 550, step `+0xc8` = 0.25, max visible `+0xb0` = -1, clip height `+0xb8` = 10000 (none) | confirmed (code) |
| `0x001e1800` | `ScrollingMenu_SetPlayer` | slot `+0x198`: input `+0x60` = `HUD_GetVirtualPad(hud, n)` | confirmed (code) |
| `0x001e1830` | `ScrollingMenu_AddItem` | slot `+0xc8` (code, widget): new item with delay, step and `+0xd8`/`+0xdc`; map[code] = item; appended; count `+0xac` | confirmed (code) |
| `0x001e1a38` | `ScrollingMenu_SetItemDescription` | slot `+0xd0`: item slot `+0x100` with the widget, reveal 0 | confirmed (code) |
| `0x001e1be0`, `0x001e1c40` | `ScrollingMenu_GetRect`, `_IsReady` | slots `+0x70`/`+0x88`: rectangle `+0x30` scaled by reveal `+0x50`; all items ready | confirmed (code) |
| `0x001e1cb8`, `0x001e1d70` | `ScrollingMenu_MoveUp`, `_MoveDown` | slots `+0x1a8`/`+0x1b0`: at an end cue `0xe` and auto-repeat off (`+0x104` = 1); else `Select(i -/+ 1)`, starting scroll state 1 or 2 when the cursor passes the middle row (`+0x100` adjusts for even counts) | confirmed (code) |
| `0x001e1e48` | `ScrollingMenu_Select` | slot `+0x1b8`: `+0x104` = 0, unfocuses the old item (releases its description when `+0xe4`), `+0xa8` = i, focuses it, cue 4 | confirmed (code) |
| `0x001e1f28`, `0x001e1f68`, `0x001e1fa8` | `ScrollingMenu_GetSelectedWidget`, `_HasDescription`, `_GetItemWidget` | slots `+0x110`/`+0x118`/`+0x120` | confirmed (code) |
| `0x001e1fe0` | `ScrollingMenu_OnCommand` | interface slot `+0x08`: the owner `+0xcc` sees the command first; else 0 = up, 1 = down; stamps `+0xd0`; returns 1 | confirmed (code) |
| `0x001e2090`, `0x001e2130` | `ScrollingMenu_Focus`, `_Unfocus` | slots `+0x90`/`+0x98`: clears the input record (`0x00146000`); focuses the selected item unless `+0xe8` | confirmed (code) |
| `0x001e21c0` | `ScrollingMenu_SetPosition` | slot `+0x08`: `+0x20` | confirmed (code) |
| `0x001e21d8` | `ScrollingMenu_Update` | slot `+0x30`: input only when focused, not scrolling, 100 ms after `+0xd0` and the item's description not focused (repeat pass `0x001e93c0`, or plain `0x001e9518` when `+0x104`, mask `0xf000`; button pass `0x001e9468`); scroll states `+0x80` (1 up, 2 down, 3 reveal top, 4 hide top) step `+0xc8`; stacks the window (`+0xa4` first, `+0xac` count, at most `+0xb0`) from `+0x20`; clip to `+0xb8` sets fit count `+0xb4` and a partial reveal of the last item | confirmed (code) |
| `0x001e2e40` | `ScrollingMenu_Render` | slot `+0x38`: the visible items, one more in state 3 | confirmed (code) |
| `0x001e2f40`, `0x001e2f98` | `ScrollingTextWidget_Construct`, `_Destroy` | 0x270 bytes, vtable `0x0053c2c8`; MessageHUD text `+0x70` | confirmed (code); file inferred |
| `0x001e2fe8` | `ScrollingTextWidget_Setup` | slot `+0xc0`: position `+0x20`, size `+0x30`, offset `+0x60`; text width = size x, colour `0x005fd310`; focus colour `+0x260` = `0x005fd310` | confirmed (code) |
| `0x001e3070` | `ScrollingTextWidget_SetText` | slot `+0xc8` (time, text, instant): optional timed reveal (`MessageHUD_ReadAutoIndent`); height `+0x38` = text height | confirmed (code) |
| `0x001e3140`, `0x001e31e0` | `ScrollingTextWidget_SetMetrics`, `_Shutdown` | slots `+0xd0`/`+0x68`: `MessageHUD_SetMetrics` wrap box; shuts the text down | confirmed (code) |
| `0x001e3248` | `ScrollingTextWidget_GetRect` | slot `+0x70`: `+0x30`, height times reveal | confirmed (code) |
| `0x001e32a0` | `ScrollingTextWidget_Update` | slot `+0x30`: text at x - width / 2 + offset; crop `+0x1f4` = max(0, height - text height); visible while reveal > 0 | confirmed (code) |
| `0x001e33c8` | `ScrollingTextWidget_Render` | slot `+0x38`: focus colour `+0x260` when focused, else `0x005fd320` | confirmed (code) |

### After `GUI/ControlMenuHUD.cpp` (no path string): text boxes, headers, menu input {#fn-after-controlmenuhud}

`0x001e6e98`-`0x001e9b88`, between the pause Stats screen ([Pause](pause.md#fn-after-controlmenuhud)) and the Rumble
screens: the boxed text base of the HUD's scroll-in texts, the three header classes, the menu input passes
([Front end](frontend.md#input)) and particle helpers. No path strings; grouping by address (inferred).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001e6e98`, `0x001e6ef0` | `BoxedText_Construct`, `_Destroy` | vtable `0x0053c7e8`: MessageHUD base, backdrop BaseWidget `+0x210`, icon `+0x310` | confirmed (code) |
| `0x001e6f50` | `BoxedText_SetupBox` | (pos, style 0-7): margins `+0x418`-`+0x430` (0.02, 0.06, 0.005, -0.2, -0.027, 0.05, adjusted by style); backdrop `+0x204` (part_page0 rect 78, (0,0,0,143), depth 8000) and icon `+0x208` (rect 27, depth 11000) per style: default both, 0 icon with shadow, 4 backdrop only | confirmed (code) |
| `0x001e72c0`, `0x001e7300` | `BoxedText_Shutdown`, `_SetVisible` | slots `+0x68`/`+0x50`; the icon only shows with a sprite (`+0x3d8` != -1) | confirmed (code) |
| `0x001e7330`, `0x001e7338`, `0x001e7358` | `BoxedText_SetIconSize`, `_SetIconSprite`, `_GetIconSprite` | `+0x410`; icon sprite word | confirmed (code) |
| `0x001e7360`, `0x001e7380` | `BoxedText_SetBackdropSprite`, `_SetBackdropColour` | backdrop sprite word and colour | confirmed (code) |
| `0x001e73a0`, `0x001e7408` | `BoxedText_SetPosition`, `_IsReady` | slots `+0x08`/`+0x88` | confirmed (code) |
| `0x001e7468` | `BoxedText_Update` | slot `+0x30`: backdrop fitted round the measured text plus margins; its alpha `0x0050f2c4` = 0 except style 4; icon left, right or centred by style | confirmed (code) |
| `0x001e7750` | `BoxedText_Render` | slot `+0x38`: backdrop, icon, text | confirmed (code) |
| `0x001e77a0`, `0x001e7808`, `0x001e7868` | `IconTextHeader_Construct`, `_Destroy`, `_Shutdown` | vtable `0x0053cae0`: HeaderBase, MessageHUD `+0x70`, sprite `+0x260`, text `+0x60` | confirmed (code) |
| `0x001e7898`, `0x001e7a00` | `IconTextHeader_Setup`, `_SetupInBatch` | slots `+0xc0`/`+0xc8`: child `+0x50`; sprite size 0.1, depth 8000, `0x005fd310`; text size 1, font 3; y gap `0x0050f2e0` = -0.015 (-0.025 in mode 2); the second takes a sprite batch | confirmed (code) |
| `0x001e7b70` | `IconTextHeader_Update` | slot `+0x30`: sprite at pos + (0, `0x0050f2ec`) sized (`0x0050f2e4`, `0x0050f2e8`); text left-aligned, centred vertically; child below | confirmed (code) |
| `0x001e7d78`, `0x001e7df0` | `IconTextHeader_Render`, `_RenderColoured` | slots `+0x38`/`+0xd0`: grey `0x005fd310` shadow `0x80`, or a given colour | confirmed (code) |
| `0x001e7e58`, `0x001e7e78`, `0x001e7ea8` | `IconTextHeader_SetText`, `_ShowChild`, `_SetVisible` | text `+0x70`; child slot `+0x40`; visible flags | confirmed (code) |
| `0x001e7ef8` | `IconTextHeader_IsReady` | slot `+0xb8`: sprite batch `+0x324` and batches 3, 6 | confirmed (code) |
| `0x001e7f50`, `0x001e8020`, `0x001e8120` | `CircledTextHeader_Construct`, `_Destroy`, `_Shutdown` | vtable `0x0053ca00`, the pause screens' headers: sprite `+0x390`, 3 circles `+0x490`, 3 letters `+0x790`, title `+0xa00`, subtitle `+0xad0` | confirmed (code) |
| `0x001e81c0` | `CircledTextHeader_ApplyVideoMode` | globals `0x0050f2fc`-`0x0050f314`, scale `0x0050f2dc` | confirmed (code) |
| `0x001e8368` | `CircledTextHeader_Setup` | slot `+0xa8`: title size 2.23, subtitle 1.561, font slot 4; circles sprite `0x54` (rect 84), `0x005fd260`, depth 8500, size 0.082 x scale | confirmed (code) |
| `0x001e85e8`, `0x001e8658` | `CircledTextHeader_SetCircleColour`, `_SetLetterColour` | the three circles or letters | confirmed (code) |
| `0x001e86c8` | `CircledTextHeader_Update` | slot `+0x30`: used circles spaced `0x0050f324`, black letters, title after, subtitle under, child | confirmed (code) |
| `0x001e8d00`, `0x001e8dd0` | `CircledTextHeader_Render`, `_RenderColoured` | slots `+0x38`/`+0xd0` | confirmed (code) |
| `0x001e8ee0` | `CircledTextHeader_SetTitle` | characters before `:` become letters; a `:` adds a circle with `'@'` + game state `+0x33a`; rest is the title; last circle `0x005fd328`; subtitle = level name of game state `+0x56dc` (table at `+0x151d`, 0x84 each) | confirmed (code) |
| `0x001e9088` | `CircledTextHeader_IsReady` | slot `+0xb8`: base and circle batches | confirmed (code) |
| `0x001e90f8`, `0x001e9130` | `SimpleHeader_Shutdown`, `_Setup` | allocator tag `SimpleHeader`, vtables `0x0053c8e8`/`0x0053c968`; Rumble titles: TextWidget font slot 6 (big_font), `0x005fd310`, pos `+0xf0`, created `+0xdc` | confirmed (code) |
| `0x001e91e8`, `0x001e9250` | `SimpleHeader_Update`, `_Render` | measured size into `+0x100`; text when created | confirmed (code) |
| `0x001e9298`, `0x001e92d0` | `HeaderBase_Construct`, `_Destroy` | vtable `0x0053cbc0`, child `+0x50` | confirmed (code) |
| `0x001e92f8`, `0x001e9330`, `0x001e9368` | `HeaderBase_Render`, `_SetColour`, `_PlaceChild` | renders the child; colour via child slot `+0x78`; moves and updates it | confirmed (code) |
| `0x001e93c0` | `MenuInput_PadRepeatPass` | auto-repeating pressed bits of pad `+0x19` (`0x005dd810`, 0x50 each) & mask; plain `+0x08` = 0 | confirmed (code) |
| `0x001e9468` | `MenuInput_ButtonPass` | released bits & mask | confirmed (code) |
| `0x001e9518` | `MenuInput_PadPlainPass` | pressed bits & mask; plain `+0x08` = 1 (a held stick does not repeat) | confirmed (code) |
| `0x001e95c0` | `MenuInput_Dispatch` | handler `+0x1c` slot `+0x08`: back 5 (`0x30`), accept 4 (`0x40`) after 110 ms; directions 0-3 from bits `0x1000`/`0x4000`/`0x8000`/`0x2000` or stick past 0.5, gap 110 ms or 400 ms while held (`+0x04`); stick `+0x0c`/`+0x10`, time `+0x18` | confirmed (code) |
| `0x001e99f8` | `HudEffect_Spawn` | named particle effect via the task system `0x00512c7c` (radar blips) | confirmed (code) |
| `0x001e9aa8`, `0x001e9ac0` | `HudEffect_GetColour`, `_SetColour` | `+0xb0` (and `+0xb4`) | confirmed (code) |
| `0x001e9ae8`, `0x001e9b38` | `HudEffect_SendShow`, `_SendHide` | task messages `0x29` and `0x2a` | confirmed (code); meaning inferred |
| `0x001e9b88` | `HudEffect_IsValid` | not null | confirmed (code) |

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
  grows the box to the widest line, places each line by its alignment and draws each run with `Font::draw` into sprites
  tagged with their font slot; `addTextSprites` hands them to each slot's batch. It implements `COLOR` (alpha × the
  widget's fade), `SIZE`, `PULSE`, `DISPLAYTIME` (hidden after, fading over the last second), `BIGFONT` (slot 6),
  `MONEYPLUS`/`MINUS`, the alignments, `CR`, `CR2`, `CR3 f`, `CRM`, every glyph tag and the closing tags; `SOUND` names
  and the largest `FREEZE` are reported for the audio layer and the timer; `BOLD`, `MONEYFONT`, `BGFONT`, `AUTOINDENT`
  and the animated stick tags have no effect yet. Lines break at the `CR` tags and, for a multi-line widget given a
  wrap width (the message box, `RM_No2ndController`'s 0.6), before a word that would pass it; how the original breaks
  is not researched, so Coney breaks only at spaces. Coney's choices: a closing tag restores the value before its
  opening tag; a line takes the alignment in effect at its first character, centring and right-aligning inside the box
  (`CCENTER` and `RRIGHT` act as `CENTER` and `RIGHT`); `CR2` and `CRM` always break; `PULSE ms` scales the colour by
  1 + 0.5 × sin(2π t / ms); the style's y is the first line's centre line. **The text font:** a text starts in slot 2,
  a `part_page0` instance, and `<BIGFONT>` switches to slot 6, `big_font` (Coney's choice, from the data: in
  `part_page0`, first glyph 94, characters `0x91`-`0xa0` are exactly the button pictures of the tag table, while in
  `big_font` they are empty but for a triangle at `0x9c`; `big_font`'s rectangles 256-261 hold a circle and five d-pad
  pictures. Inferred from viewing the sheets with `--view-sheet`).
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
  plain query after a refused move, and a fade over every item (Coney's, for the pause menu's fade out). `ScrollingMenu`
  (`0x001e1338`, `src/gui/scrolling_menu.h`): no wrap, cue 4 and `0xe`, 100 ms between moves, a window around the
  cursor ([Front end](frontend.md#rm-layout)). `UsageInfo` (`0x001ceb40`, text `0x001cec28`): size 1.0, `kMenuGrey`,
  left-aligned or centred. `Bar` (`0x001a0fd0`) and the message box (`src/gui/message_box.h`, [Front end](frontend.md#message-box)).
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
  `PULSE`'s triangle swing; and the y is the first line's centre, confirmed (runtime) for the light widget). `<BOLD>` and
  `<AUTOINDENT>` answered too (the tag table). Still open: the two characters of each animated stick tag; whether the
  shadow is drawn per
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

- **Blip type 12**: who uses it (radar batch `+0x48` answered: icons 25 and 26, `0x001c6730`).
- **What icons 22 and 355 show**, and the dealer type 2's role.
- **The 2D sort key** (answered): the creation depth; see [Draw order](#draw-order).
- **The sheet `0x349348bd`** behind the Quick Rumble menus (sheet-table record 12, inferred;
  [Front end](frontend.md#rm-layout)): its resource name (not one of the names tried). Coney loads it by its WAD file
  name, `882067645`; on the NTSC-U disc it has 7 rectangles over a 512 × 512 texture, rectangle 5 the gang collage.
- **`firstGlyph` of `part_page0` (94) and `part_page1` (20)**: which text uses them, and the two explicit-base call
  sites in `Font_Measure`/`Font_Draw`.
- **`<MONEYFONT>`**: the glyph base it sets (`0xd0100` for font 6, `0xb` for font 3) looks like a packed value; how
  it is used is not worked out.
- **The message box** (answered for the layout and input: [Front end](frontend.md#message-box)): its file.
- **`OptionGrid`** (answered, [The widget classes](#widget-classes)): the owner reads the code itself
  (`0x001d43f0(grid, 0x001d43e8(grid))`). Still open: setup's `a2` (`+0xa8`) and the item flags argument (4).
- **The widget base's fields** (`+0x0c` initialised, `+0x40`, `+0x50` the input record, `+0x54`-`+0x68`) and the
  second interface at `+0x6c`.
