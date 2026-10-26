---
kind: system
system: circled-text
owns:
  ranges: ["0x001d0aa8-0x001d1848", "0x001e77a0-0x001e90f8"]
---
# Circled texts and headers

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`).

## Summary

Two widget families put a short code in coloured circles before a line of text. The **circled text** (our name
`CircledText`) is the level header of the mission-select and statistics screens: the level's number in one to three
circles of the area's colour, then its name. The **pause header** (our name `CircledTextHeader`, a kind of
`IconTextHeader`) is the big title at the top of the story pause menu: the mission's number and the checkpoint's
letter in circles, the last one red, the mission's title after them, a thin rule, and the level's name under it.

The two share an allocator tag but nothing else: the front-end screens allocate their headers under the tag
`CircledTextHeader` and build the smaller `CircledText` class in them. This page describes both classes, their set-up,
their layout and their drawing. What the screens feed them is on the screens' pages:
[The statistics screen](game-stats.md#list), [Front end: mission select](frontend.md#fn-missionselecthud) and
[Pause: the top header](pause.md#the-top-header). The widget base, the fonts and the coordinates are on
[GUI](gui.md#widget-classes).

## Map

Neither class has a path string; both are named by us. `CircledText` sits in the unit that ends at the stub
`0x001d2de0` with the `GameMenu` base, and the header classes between the Controls screen and the Rumble screens
([Source map](source-map.md#gui); grouping inferred from the address order). Every function, with its role and level,
is on [the GUI function list](gui/functions.md); these are the entry points.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x001d0aa8` | `CircledText_Construct` | the parts of a circled text ([`CircledText`](#circled-text)) | confirmed (code) |
| `0x001d0d18` | `CircledText_Setup` | its set-up, once ([Set-up](#ct-setup)) | confirmed (code) |
| `0x001d1250` | `CircledText_Update` | its layout ([Layout](#ct-layout)) | confirmed (code) |
| `0x001d16e8` | `CircledText_Render` | its drawing ([Drawing](#ct-drawing)) | confirmed (code) |
| `0x001e77a0` / `0x001e7898` | `IconTextHeader_Construct` / `IconTextHeader_Setup` | the header base: the label, rule A and the child ([The pause header](#pause-header)) | confirmed (code) |
| `0x001e7f50` | `CircledTextHeader_Construct` | the pause header's parts ([The pause header](#pause-header)) | confirmed (code) |
| `0x001e8ee0` | `CircledTextHeader_SetTitle` | the title split into circles ([The title](#header-title)) | confirmed (code) |
| `0x001e86c8` | `CircledTextHeader_Update` | the header's layout ([Header layout](#header-layout)) | confirmed (code) |
| `0x001e81c0` | `CircledTextHeader_ApplyVideoMode` | the layout's factors per video mode ([Header layout](#header-layout)) | confirmed (code) |
| `0x001e8dd0` | `CircledTextHeader_RenderColoured` | the header's drawing ([Header drawing](#header-drawing)) | confirmed (code) |

## Data

### `CircledText` {#circled-text}

Confirmed (code) at the cited addresses unless a line says otherwise.

A widget of `0x5fc` bytes, vtable `0x0053b390`, built by `CircledText_Construct` (`0x001d0aa8`) in the `0x600`-byte
blocks the mission-select screen (`MissionSelect_Build` `0x001bc5a8`, `MissionSelect_FillArea` `0x001bd838`) and the
statistics screen (`GameStats_BuildMissionList` `0x002155a8`) allocate with the tag `CircledTextHeader`; the tag is
only a name. Confirmed (code) at those call sites.

| Offset | Field | Evidence |
| --- | --- | --- |
| `+0x0c` | set up: a second set-up does nothing | confirmed (code) at `0x001d0d18` |
| `+0x20` / `+0x28` | position: x is the rectangle's **centre**, y its centre | confirmed (code) at `0x001d1250` |
| `+0x30` / `+0x38` | size: width and height | confirmed (code) at `0x001d1250` |
| `+0x50` | reveal, 0-1: the share of the height shown | confirmed (code) at `0x001d11c8` |
| `+0x60` | the main text, a `TextWidget` | confirmed (code) at `0x001d0aa8` |
| `+0x130` / `+0x134` | the normal colour (colour-table entry `0x005fd320`) and the focused one (`0x005fd318`) | confirmed (code) at `0x001d0d18` |
| `+0x140` | the back sprite, a `BaseWidget` | confirmed (code) at `0x001d0aa8` |
| `+0x240` | circle offset: where the circles start from the left edge | confirmed (code) at `0x001d1250` |
| `+0x250` + `0x100` × i, i < 2 | the circle sprites | confirmed (code) at `0x001d0aa8` |
| `+0x450` + `0xd0` × i, i < 2 | a one-character `TextWidget` per circle | confirmed (code) at `0x001d0aa8` |
| `+0x5f0` / `+0x5f4` | `circleW` / `circleH`: a circle's width and height | confirmed (code) at `0x001d0d18` |
| `+0x5f8` | the circles in use: the length of the circled string | confirmed (code) at `0x001d1008` |

The object has room for two circles, but `CircledText_SetCircled` (`0x001d1008`) sets the count to the string's
length with no bound, and the statistics screen passes up to three characters (confirmed (code) at `0x001d1008` and
`0x002155a8`); what a third circle overwrites is [an open question](#q-third-circle).

### The pause header {#pause-header}

Confirmed (code) at the cited addresses unless a line says otherwise.

`CircledTextHeader` (constructor `0x001e7f50`, vtable `0x0053ca00`) extends `IconTextHeader` (constructor
`IconTextHeader_Construct` `0x001e77a0`, vtable `0x0053cae0`). There is **one** of it, the pause menu's top header at
`0x006340c0`, which `PauseMenu_StaticInit` (`0x001dfc80`, the constructor's only caller) builds with the text
"Current Objectives" (string `0x108`); the pause menu's other two headers (`0x00634c70`, `0x00634fe0`) are plain
`IconTextHeader`s. Its parts, read in `0x001e7f50`, `IconTextHeader_Setup` (`0x001e7898`) and
`CircledTextHeader_Setup` (`0x001e8368`):

| Offset | Part | Set up as | Evidence |
| --- | --- | --- | --- |
| `+0x20` / `+0x28` / `+0x30` | position x, y and the box's width | the caller's | confirmed (code) |
| `+0x360` / `+0x368` | text offset x, y | the caller's | confirmed (code) |
| `+0x50` | the child (the pause menu's current-objectives checklist) | placed under the header | confirmed (code) |
| `+0x70` | the label, a markup text (the base's) | size 1.0, font slot 3, grey `0x005fd310`, the constructor's text | confirmed (code) |
| `+0x260` | rule A, a `BaseWidget` (the base's) | the caller's sprite word, size 0.1, depth 8,000, grey | confirmed (code) |
| `+0x390` | rule B, a `BaseWidget` | the same sprite word, depth 8,000, grey | confirmed (code) |
| `+0x490` + `0x100` × i, i < 3 | the circles | `part_page0` rectangle 84 (word `0x54`), depth 8,500, black `0x005fd260`, size 0.082 × the circle scale | confirmed (code) |
| `+0x790` + `0xd0` × i, i < 3 | a one-character `TextWidget` per circle | font slot 4, `Font_Size(1.561)`, grey | confirmed (code) |
| `+0xa00` | the title, a `TextWidget` | font slot 4, `Font_Size(2.23)`, grey | confirmed (code) |
| `+0xad0` | the subtitle, a `TextWidget` | font slot 4, `Font_Size(1.561)`, grey | confirmed (code) |
| `+0x370` | the title's characters | `CircledTextHeader_SetTitle` | confirmed (code) |
| `+0xba0` | the circles in use | `CircledTextHeader_SetTitle` | confirmed (code) |

The pause menu sets it up in `PauseMenu_Open` (`0x001dbee0`) at (0.5, 0.22), (0.5, 0.18) in 16:9, box width 0.5,
text offset (0.01, −0.01), with sprite word 10 (`part_page0` rectangle 10) for both rules. Confirmed (code).

### The layout factors {#header-factors}

Confirmed (code) at `0x001e81c0`. `CircledTextHeader_ApplyVideoMode` runs at the top of every header update and writes
one factor per y of [the layout](#header-layout), by the device's flags ([Graphics](graphics.md#video-mode)):

| Flags | a `0x0050f2fc` | b `0x0050f300` | c `0x0050f304` | d `0x0050f308` | e `0x0050f30c` | f `0x0050f310` | s `0x0050f2dc` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| default, and progressive 4:3 (`0x20`) | 0.9 | 0.95 | 0.78 | 0.68 | 1.0 | 1.0 | 1.2 (its static value) |
| `0x04` without `0x02` or `0x08` (16:9) | 0.6 | 0.67 | 0.1 | 0 | 1.0 | 1.0 | 1.2 (its static value) |
| `0x02` without `0x04` | 1.49 | 1.63 | 2.4 | 2.3 | 1.1 | 1.1 | 0.84 |
| `0x02` with `0x04` | 1.15 | 1.25 | 1.4 | 1.3 | 1.1 | 1.1 | 0.84 |

The function writes s only in the `0x02` modes and never puts it back, so a mode entered after one of them keeps 0.84
(inferred from the code having no other writer). It also writes `0x0050f314` (0.082, or 0.05 with `0x02`), which the
update does not read (confirmed (code) at `0x001e86c8`).

## Behaviour

### `CircledText`: set-up {#ct-setup}

Confirmed (code) at the cited addresses unless a line says otherwise.

`CircledText_Setup` (`0x001d0d18`, slot `+0xc0`) takes the circle size, the owner, the position, the size, the circle
offset, the text metrics, the back and circle sprite words and their batches, and runs once. It gives the text and the
characters the normal colour `+0x130` and font slot 4, the characters without a shadow; the back sprite size 1.0,
depth 8,400, grey (128, 128, 128); the circles size 0.1, depth 8,500, red (160, 16, 16), `circleW` × `circleH`; the
step divisor `0x0050ec64` = 3 (2 with device flag `0x02`) and the circles' drop `0x0050ec68` = `circleH` × 0.1; the
reveal 1.0. `CircledText_SetText` (`0x001d0fe8`) sets the main text, `CircledText_SetCircled` (`0x001d1008`) one
character per circle, and `CircledText_SetCircleColour`, `CircledText_SetCharColour` and `CircledText_SetBackColour`
(`0x001d1098`, `0x001d1108`, `0x001d1178`) the colours. `CircledText_SetReveal` (`0x001d11c8`) clamps the reveal to
0-1; `CircledText_GetRect` (`0x001d11f8`) is the rectangle with its height × the reveal.

### `CircledText`: layout {#ct-layout}

Confirmed (code) at the cited addresses unless a line says otherwise.

`CircledText_Update` (`0x001d1250`, slot `+0x30`) runs while the widget is ready (`CircledText_IsReady`, `0x001d1198`:
made and the circle batch resident). With X, Y the position, W × H the size and r the reveal:

- the back sprite is centred on X; its size (W, H × r) is set after its own update, through the device's width
  conversion, and it moves up by (H − H × r) / 2 so its top edge stays put; its texture rectangle is cut to r of its
  height;
- a pen starts at x0 = X − W / 2 + the circle offset. For each circle it moves `circleW` / 3 (the divisor, with the
  raw `circleW` added to a GUI x); the character goes half its measured width left of the pen, so centred on it, at
  y Y; the circle is centred on the pen at Y − (`circleH` − `circleH` × r) / 2 + `circleH` × 0.1, sized
  (`circleW`, `circleH` × r) after its update without the conversion, its texture cut to r; the pen moves `circleW` / 3
  again. So circle i is centred at x0 + (2i + 1) × `circleW` / 3;
- the main text goes at the last circle's centre + `circleW` / 2, or at x0 without circles, at y Y;
- the texts and the characters take the reveal r; the widget is visible while r is above 0.

### `CircledText`: drawing {#ct-drawing}

Confirmed (code) at `0x001d16e8`. `CircledText_Render` (slot `+0x38`) draws the back sprite, then the text in the
focused colour when the widget's slot `+0xa0` says it is focused and the normal colour otherwise, then each circle and
its character. Before a character is drawn it takes the focused colour `+0x134`, focused or not, unless its colour is
the colour table's black (`0x005fd260`); so the statistics screen's black characters stay black.

### The title {#header-title}

Confirmed (code) at the cited addresses unless a line says otherwise.

`CircledTextHeader_SetTitle` (`0x001e8ee0`) splits its string at the first `:`:

- each character before it gets a circle, in order;
- **each** `:` that is not the string's first character adds a circle whose character is `@` + the checkpoint (game
  state `+0x33a`: `A` for checkpoint 1, `B` for 2), grey `0x005fd310` until the next update turns it black;
- the other characters after the first `:` are the title, so a later `:` adds a circle and is left out of the title;
  a string with no `:` is all circles and no title;
- nothing bounds the count at the three circles there are; circles not in use keep their old letters;
- the last circle is then red, `0x005fd328` (170, 43, 43);
- the subtitle is the current level's name (the level record at game state `+0x14d4` + `0x84` × `+0x56dc`, name at
  `+0x49`).

The pause menu passes the mission title (game state `+0x134`), or the Rumble mode's title in a Rumble level, after
colouring all three circles with the level's colour ([Pause: the top header](pause.md#the-top-header)). So a title of
the form number, `:`, title shows the number in the level's colour and the checkpoint letter in red; that the mission
titles have that form is inferred from this code and the pause screenshot on [Pause](pause.md#opening-the-menu).

### Header colours {#header-colours}

Confirmed (code) at the cited addresses unless a line says otherwise.

`CircledTextHeader_SetCircleColour` (`0x001e85e8`) and `CircledTextHeader_SetLetterColour` (`0x001e8658`) colour all
three circles or characters. Every update sets each used character back to **opaque black**, so a letter colour set
from outside lasts until the next update (confirmed (code) at `0x001e86c8`); in effect the letters are always black
(inferred from the update running before every draw).

### Header layout {#header-layout}

Confirmed (code) at the cited addresses unless a line says otherwise.

`CircledTextHeader_Update` (`0x001e86c8`) places every part each update from the position (X, Y), the box width W, the
text offset (ox, oy) and [the factors](#header-factors) a-f and s, in GUI coordinates:

- rule A: centred at (X + 0.035, Y + 0.105), 0.57 × 0.007 through the device's conversion;
- rule B: centred at (X + 0.035, (Y − 0.07) × a), the same size;
- with h rule A's height (0.007, `+0x2f4`) and t the title's line height (`+0xb4` of its `TextWidget`, which
  `TextWidget_SetText` `0x001cd1e0` sets to the height plus the line gap of its metrics, `Font_Size(2.23)`), the row
  y0 = Y − h / 2 + t + oy − 0.045 − 0.151. `Font_Size` (`0x00179808`) multiplies the height by the live view aspect
  `0x0050b208`, static 1.3333, which the device sets to 1.45 in interlaced 4:3, so t is about 0.099 there and 0.091 at
  the static value (confirmed (code) at `0x00179808`; the 1.45 is [an open question](#q-line-height));
- the subtitle, `Font_Size(1.7)`, at (0.257, (y0 + 0.075) × b);
- the circles from the absolute x 0.272, 0.051 apart: each character at (x, y0 × d), `Font_Size(1.7)`, and its circle
  centred at (x + the character's width / 2, y0 × c), 0.082 × s square;
- the title, `Font_Size(2.23)`, after the last circle at (x, y0 × d), or at x 0.255 with no circle;
- the label at (X − W / 2 + ox − 0.005, (Y + h / 2 − its height / 2 + oy + 0.1) × e);
- the child at (the label's x + 0.005, (Y + h / 2 + 0.02 + 0.1 + g) × f), g = −0.015, or −0.025 with flag `0x02`
  alone (`0x0050f2e0`, confirmed (code) at `0x001e7898`).

A worked example, inferred from the constants above: on the pause grid in the default mode (X, Y = 0.5, 0.22) with
t = 0.099, y0 is about 0.109, so the title and the letters sit at y 0.074, the circles' centres at 0.085, rule B at
0.135, the subtitle at (0.257, 0.175) and rule A, never drawn, at 0.325. With t = 0.091 y0 is 0.102, the title at
0.069 and the subtitle at 0.168. In the 16:9 row d is 0, which would put the title and letters on y 0
([q-widescreen](#q-widescreen)).

### Header drawing {#header-drawing}

Confirmed (code) at the cited addresses unless a line says otherwise.

`CircledTextHeader_Render` (`0x001e8d00`, slot `+0x38`) and `CircledTextHeader_RenderColoured` (`0x001e8dd0`, slot
`+0xd0`, which the pause menu calls with grey `0x005fd320` or `0x005fd310` and the menu's fade as alpha) draw the label
in the given colour with shadow `0x80`, the child, then each used circle and its character (no shadow), rule B, the
title and the subtitle (shadow `0x80`). `RenderColoured` changes only the alpha of the circles, letters, rule, title
and subtitle. **Rule A is updated but never drawn**: neither this render nor the base's (`IconTextHeader_Render`,
`0x001e7d78`) draws `+0x260`. The pause grid hides the label, rule A and the child (`IconTextHeader_SetVisible(0)`,
`0x001e7ea8`).

## In Coney

`CircledText` (`repo:src/gui/circled_text.h`) ports the circled text: set-up, text, colours, reveal, layout and
drawing. Coney keeps the rectangle's left edge where the original keeps its centre; the statistics screen uses it
([The statistics screen](game-stats.md#in-coney)). `CircledTextHeader` (`repo:src/gui/circled_text_header.h`) ports
the pause header: the title split, the colours, the factors (`CircledHeaderFactors::forMode`), the layout and the
drawing; the pause menu's use is on [Pause](pause.md#coneys-implementation). Tests:
`repo:tests/gui/circled_text_header_test.cpp`, `repo:tests/gui/game_stats_test.cpp` and
`repo:tests/gui/widgets_test.cpp`.

Stand-ins and deliberate differences:

- `CircledText` draws the back and the circles whole at their reduced height where the original also cuts their
  textures, and its texts are not revealed, as Coney's text widget has no reveal ([q-reveal-crop](#q-reveal-crop)).
- `CircledText` drops characters past two, and `CircledTextHeader` circles past three, where the original writes past
  its room ([q-third-circle](#q-third-circle)).
- `CircledText`'s step divisor is always 3, and the header's s is set per mode (the original keeps 0.84 after a `0x02`
  mode); flag `0x08` is not modelled ([q-widescreen](#q-widescreen)).
- The header makes no rule A (never drawn) and has no child (the pause menu draws its lists itself); its letters and
  subtitle take the update's size 1.7 from the start where the original sets them up at 1.561, and a letter's width
  is measured at 1.7 ([q-letter-size](#q-letter-size)).
- The header's line height comes from Coney's font metrics at the aspect 1.3333, so it sits about 3 pixels higher
  than the original's ([q-line-height](#q-line-height)).

## Open questions

### The view aspect in the line height {#q-line-height}

`Font_Size` (`0x00179808`) scales line heights by
`0x0050b208`, which the device sets to 1.45 in interlaced 4:3. A PCSX2 read of the pause header (`0x006340c0`,
`level99` checkpoint 2) showed its title's line height `+0xab4` = 0.09898, the title at y 0.0745 and the subtitle at
0.1753, matching the worked example with t = 0.099 against Coney's 0.091, but no run record holds it. A run record
of that read, and Coney's font metrics taking the live aspect, would close it; the statistics screen's size-1.2
lines have the same cause ([The statistics screen](game-stats.md#q-line-height)).

### A third circle in `CircledText` {#q-third-circle}

With three characters `CircledText_SetCircled` (`0x001d1008`)
writes a third circle and character past the two the object holds; what they overwrite, and whether a three-digit
level number ever reaches it, is unread. The pause header likewise has room for three and no bound.

### 16:9 and the `0x02` modes {#q-widescreen}

In the 16:9 row the factor d is 0, which would put the title on y 0;
no 16:9 capture has checked it, nor the `0x02` modes' divisor and factors.

### The letters' first size {#q-letter-size}

The header's letters and subtitle are set up at 1.561 and replaced by
1.7 every update; whether a letter measured before the first update leaves its circle off-centre on the first frame
is unchecked.

### The reveal's texture cut {#q-reveal-crop}

Coney's sprite widgets cannot cut a texture rectangle, so a partly
revealed circle or back is squeezed rather than cut; a cropping sprite would make it faithful.
