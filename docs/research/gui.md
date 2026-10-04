# GUI: widgets, sprite sheets and text

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims. The
disc-side checks (2026-10-04) walked every chunk container of the NTSC-U WAD and are reported as counts and layouts
only.

## Purpose

How the front end and the HUD put 2D things on the screen: the widget classes, the screen flow that chains menu
screens, the **sprite sheets** (chunk `0x4C`, "Particle Page") every 2D image comes from, how a sprite reaches a
RenderWare PTank, how text is laid out and drawn as one sprite per character, where the UI strings come from, and the
order in which it is all drawn. [Start-up and the front end](frontend.md) says which screens run when; the screen
geometry (GUI coordinates, the overlay camera) is on [Graphics](graphics.md#2d-drawing).

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
| `+0x74` | the largest sprite count seen |

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

### Markup tags {#markup}

Text strings carry tags in angle brackets. The table at `0x0050d718` holds 66 tag strings of 61 bytes each; the
layout code (`0x001b9600`) compares the text after a `<` with each in turn and acts by index. Confirmed (code):

| Index | Tag | Effect |
| --- | --- | --- |
| 0 | `<COLOR rrggbbaa>` | colour (hex); alpha multiplied by the widget's fade |
| 1 | `<SIZE f>` | font size × `f` |
| 2 | `<PULSE ms>` | colour pulses between × 1.5 and × 0.5 with that period |
| 3 | `<SOUND name>` | plays the sound (CRC-32 of the name) once, on the first frame shown |
| 4 | `<FREEZE ms>` | sets the display end and freezes the game timer for `ms` (`0x00145ea8(gameTimer, ms, 2000)`) |
| 5 | `<DISPLAYTIME ms>` | the text disappears after `ms`; it fades during its last second |
| 6 | `<BOLD>` | |
| 7 | `<BIGFONT>` | font slot 6 (`big_font`) |
| 8 | `<MONEYFONT>` | an icon font (glyph base set from the font) |
| 9 | `<BGFONT>` | glyphs on a background (creates an instance over sheet 0, depth 8,000) |
| 10, 11 | `<MONEYPLUS>`, `<MONEYMINUS>` | the icon characters `=` and `<` |
| 12-16 | `<CENTER>`, `<CCENTER>`, `<RIGHT>`, `<RRIGHT>`, `<LEFT>` | alignment |
| 17-20 | `<CR>`, `<CR2>`, `<CR3 f>`, `<CRM>` | new line; `CR2` only in single-player, `CR3` adds `f`, `CRM` conditional |
| 21 | `<AUTOINDENT ...>` | |
| 22-32 | `<FIST>` ... `<BGCIRCLE>` | HUD icons, each a single character of the current font (`:`, `>`, `?`, `;`, `@`, `A`, `B`, `C`, `E`, `F`, `y`) |
| 33-49 | `<S>`, `<O>`, `<T>`, `<ST>`, `<X>`, `<START>`, `<SELECT>`, `<R1>`, `<R2>`, `<R3>`, `<L1>`, `<L2>`, `<L3>`, `<DU>`, `<DD>`, `<DL>`, `<DR>` | **button glyphs**: characters `0x9f`, `0x9d`, `0x96`, `n`, `0x9e`, `0x97`, `0x93`, `0x9c`, `0x94`, `0x92`, `0xa0`, `0x95`, `0x91`, `0x9b`, `0x99`, `0x9a`, `0x98` |
| 50, 51 | `<LAS>`, `<RAS>` | (no character set) |
| 52-55 | `<SDD>`, `<SDL>`, `<SDR>`, `<SDU>` | animated stick glyphs: alternate between two characters every 500 ms |
| 56-65 | `</COLOR>`, `</SIZE>`, `</PULSE>`, `</BOLD>`, `</BIGFONT>`, `</MONEYFONT>`, `</BGFONT>`, `</CENTER>`, `</RIGHT>`, `</LEFT>` | restore |

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
player camera to the atomic minus the square of a radius. Confirmed (code) for the reads; that the 2D key is the
depth given at creation (`+0x20`, placed in the atomic's frame) is inferred, and with it the order: `menu_system`
(8,500) under `big_font` text (9,000) under the 10,000 and 11,000 sheets (speculative until checked on screen).

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

The chunk types `0x0F`-`0x13` (English to German string tables) and `StringTable/StringTableCache.cpp` are used by
other screens (credits, Rumble mode); they do not occur in the WAD ([WAD contents](formats/wad-contents.md#chunk-types)).

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

TODO for the analysts, found while implementing:

- A name for the `0x4C` handler: the `@orig` tag calls it `ChunkLoaded_ParticlePage` (`0x00181b20`) until the
  research database names it.

What the implementer still needs:

- The `0x4D` table from `warriors.glr` to map sheet index or name hash to a resource.
- Sprite batches (instances): a sheet, a capacity, a format (position + size, + rotation, or matrix), a blend of
  source alpha / inverse source alpha, a world (none, 3D overlay, 2D overlay) and a depth; `AddSprite` with the record
  above, dropped past capacity; emptied after each frame. librw's PTank, or a plain textured-quad batch, both fit.
- The 2D pass: Z test and write off, culling off, batches sorted by ascending key, drawn through the overlay camera.
- Widgets with `Init`, `Update`, `Render`, `Shutdown`, focus; the screen flow with push, pop, unwind and a per-screen
  transition table, entering and exiting covered screens as described.
- Text: the size formula, proportional advance, alignment, shadow, the markup tags (at least `COLOR`, `SIZE`,
  `BIGFONT`, `CENTER`, `CR`, the button glyphs and their closing tags for the menus), and glyph = `firstGlyph + byte`.
- The global UI string table filled by `CfgHUDMessage` from the language's `config_strings_*.lua`.

## Open questions

- **The 2D sort key**: confirm that the overlay key is the creation depth, and the resulting order, with a PCSX2
  capture of a menu.
- **`firstGlyph` of `part_page0` (94) and `part_page1` (20)**: which text uses them, and the two explicit-base call
  sites in `Font_Measure`/`Font_Draw`.
- **`<MONEYFONT>`**: the glyph base it sets (`0xd0100` for font 6, `0xb` for font 3) looks like a packed value; how
  it is used is not worked out.
- **The message box** used by the memory-card mode (`0x001c6b78`-`0x001c73e8`, `0x005e5840`): its file, layout and
  input.
- **`OptionGrid`**: how an item's code reaches the screen's result (the setup's last argument is a pointer that
  receives it, inferred), and its layout rules (rows, columns, spacing).
- **The widget base's fields** (`+0x0c` initialised, `+0x40`, `+0x50` the input record, `+0x54`-`+0x68`) and the
  second interface at `+0x6c`.
