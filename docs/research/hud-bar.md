---
kind: system
system: hud-bar
owns:
  functions: ["0x001a1138", "0x001c2530"]
---
# The meter bar's draw

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`).

## Summary

The game's horizontal meters, the rage meter under the health rings, the mini-game and mash meters, the scripted bars
and the four bars of the pause menu's Stats screen, are all drawn by one function: a rounded left cap, a dark body, a
pale strip for the capacity, the coloured fill over it and a rounded right cap. This page describes that draw and the
generic bar's fill setter. The meter object (`HudBar`) and what each meter puts in it are on the pages of those
meters: [The rage meter](hud.md#the-rage-meter), [Scripted bars](hud-messages.md#scripted-bars) and
[Pause: the Stats screen](pause.md#fn-after-controlmenuhud).

## Map

Both functions are in `GUI/` without a path string ([Source map](source-map.md#gui), the grouping inferred from the
address order). The names are ours. Every HUD function, with its role and level, is on
[the HUD function list](hud/functions.md).

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x001a1138` | `HudBar_Draw` | draws one meter ([The draw](#draw)) | confirmed (code) |
| `0x001c2530` | `HudGenericBar_SetFill` | the generic bar's fill ([The generic bar's fill](#generic-fill)) | confirmed (code) |

## Data

### The fields the draw reads {#fields}

Confirmed (code) at `0x001a1138`. The meter is `0x70` bytes (`HudBar_Construct`, `0x001a1008`); the draw reads:

| Offset | Meaning | Evidence |
| --- | --- | --- |
| `+0x00` / `+0x04` | width and height | confirmed (code) |
| `+0x10` | the left end, (x, y) | confirmed (code) |
| `+0x20` / `+0x24` / `+0x28` | the background, capacity and fill colours | confirmed (code) |
| `+0x2c` / `+0x2e` | the first rectangle, and the resource instance whose sheet holds it | confirmed (code) |
| `+0x38` / `+0x3c` | the fill and the capacity, fractions 0-1 | confirmed (code) |
| `+0x44`-`+0x4c` | the trail ([Flash and trail](#flash-trail)) | confirmed (code) |
| `+0x50` | drawn: 0 draws nothing | confirmed (code) |
| `+0x5c` | right to left | confirmed (code) |
| `+0x65` | the flash's brightness byte ([Flash and trail](#flash-trail)) | confirmed (code) |

The sprite word's high half (`+0x2e`) is a resource instance, not a sheet-table record, and its low half the first of
four rectangles: the body (+0), the strip of the fill and the capacity (+1), the left cap (+2) and the right cap (+3).
Confirmed (code) at `0x001a1138`; which sheet instance 3 holds (`part_page0`, so word `0x30036` is rectangles 54-57) is
on [HUD function index: which sheet](hud-functions-parts.md#mash-meter-layout).

## Behaviour

### The draw {#draw}

Confirmed (code) at the cited addresses unless a line says otherwise.

`HudBar_Draw(flashBelow, bar, flash, trail)` (`0x001a1138`) takes the threshold in `f12` and the bar, `flash` and
`trail` in `a0`-`a2`. With the drawn flag set it adds, left to right and all at the meter's height:

1. the left cap (rectangle +2) in the background colour;
2. the body (+0) over the inner width in the background colour;
3. the capacity strip (+1) over the inner width × the capacity, in the capacity colour;
4. the fill strip (+1) over the inner width × the fill, in the fill colour;
5. the right cap (+3) in the background colour.

**Where each goes.** A cap is as wide as its rectangle's u span (u1 − u0), taken as overlay units; the inner width is
the meter's width less the left cap's. The left cap starts at the left end; the body is centred on the meter's middle,
so it starts half a cap in; the two strips start half a cap in; the right cap starts half a cap short of the width. The
stretched body and strips have their u span moved in by one texel at each end, so filtering stays inside their
rectangles; their v span is left as it is.

### Flash and trail {#flash-trail}

Confirmed (code) at the cited addresses unless a line says otherwise.

Every one of the draw's 14 calls passes **`flash` = 0 and `trail` = 0** (`0x001a33a4`, `0x001c0aac`, `0x001c0ac0`,
`0x001c2504`, `0x001d737c`, `0x001d7390`, `0x001d73e4`, `0x001d7400`, `0x00209034`, `0x002119dc`, `0x00211a18`,
`0x00211a44`, `0x002135d0`, `0x0021360c`), so `flashBelow` (0.25 at most of them, 0 for the mash meter) changes
nothing and no meter flashes or trails through this function. The rage meter's gold pulse is the player panel's own
colour switch ([The rage meter](hud.md#the-rage-meter)).

The path no caller takes, for reference: with `flash` set and the fill at or below `flashBelow`, the byte `+0x65` drops
by 16 each draw and returns to 255 when it falls under 32 (a 14-draw cycle), and half of it is ORed into the red byte
of the background parts only (the caps and the body). With `trail` set, the fill rectangle is drawn between the old
and the new fill end for 64 draws after a drop (`+0x44`-`+0x4c`), at alpha (count & 3) × 85.

### The generic bar's fill {#generic-fill}

Confirmed (code) at `0x001c2530`. `HudGenericBar_SetFill` sets the fill of a generic bar's meter (the bar's `+0x78`,
the fill `+0x38` of its `HudBar` at `+0x40`) to its argument clamped to 0-1, and only when the bar has been created.
The generic bars are the scripted bars' ([Scripted bars](hud-messages.md#scripted-bars)), drawn by
`HudGenericBar_Render` (`0x001c2470`) as the label and then `HudBar_Draw(0.25, bar, 0, 0)`.

## In Coney

`Bar::render` (`repo:src/gui/bar.h`) draws the meter's form when its caps are set (`Bar::setCaps`), as the pause menu's
Stats screen does ([Pause](pause.md#coneys-implementation)): the left cap, the body, the fill and the right cap, placed
and inset as [The draw](#draw) says. `Bar::setFill` clamps the fill as `HudGenericBar_SetFill` does; the scripted bars'
fill is `ScriptedBars::setPercentage` (`repo:src/hud/scripted_bars.h`). Tests: `repo:tests/gui/widgets_test.cpp` and
`repo:tests/gui/pause_stats_test.cpp`.

Stand-ins: the capacity strip, the right-to-left form, the fill colour's blend (`+0x60`) and the `0x02` video modes'
offsets are not drawn, since no meter Coney draws with caps uses them; the flash and the trail are not drawn because
no caller asks for them; the bars set up without caps (PM_Light's, the Rumble gang screen's) draw a back and a fill
only ([q-capless](#q-capless)).

## Open questions

### Bars without caps {#q-capless}

Whether PM_Light's brightness bar and the Rumble gang screen's two bars
(`Bar_Construct`, `0x001a0fd0`) are drawn by `HudBar_Draw` (`0x001a1138`) with their caps, as the meter is, has not
been compared; Coney draws them as a back and a fill until it is.
