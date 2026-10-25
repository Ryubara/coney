# HUD radar, target panel and health rings

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 as on [The in-game HUD](hud.md), which this page was split from because that page had grown too
large. The overview, the data tables and the rest of the behaviour stay on [The in-game HUD](hud.md).
Every claim keeps the evidence level it had there.

This page covers the radar on screen, the target panel and the health rings.

## The radar on screen

Each radar (HUD `+0x15d0`, player 1 `+0x3f10`) is drawn by `Radar_Render` (`0x001c60b0`) as a textured **disc of the
level's map** turned so that the player's view points up, centred at its position `+0x2930` in **overlay-camera space
at depth 1.0**. `HUD_Update` sets that position every frame: with one player **(0.51, -0.31)** (`0x0050d428`,
`0x0050d430`), which projects to 85.2 % across and 81 % down. Confirmed (code) at `0x001af010`, `0x001c60b0`;
**confirmed (runtime):** the disc's centre at 0.851 and 0.812 of the screen. The blips over it are on
[GUI: the radar](gui.md#radar-icons).

**The map texture.** `Radar_Setup` (`0x001c3de0`) takes the current level record's world name (`+0x39`,
[Front end](frontend.md#the-level-table)), and when the file manager knows a resource named `"%u"` of its CRC-32 it
creates a sprite batch over the **sprite sheet named after the world** (`ResourceMgr_CreateInstance`, depth 10,000,
[GUI](gui.md#resource-instances)) and keeps its slot at radar `+0x94`; otherwise `+0x94` is `0xff` and no disc is
drawn. The sheet's texture is the map. Confirmed (code); **confirmed (runtime)**, `level99`: radar `+0x94` = 24, and
resource slot 24 holds the sheet `0xe36542ae` (the CRC-32 of `level99`), resident. The same set-up keeps a
`part_page0` batch at `+0x96` for the blips.

**Where the map sheet is.** Like every sprite sheet it is the WAD file named by the decimal CRC-32 of its name
(`"3815064238"` for `level99`, entry 2,611; `level80` entry 1,129; `level95` entry 2,741): a `0x2A` texture dictionary
holding one texture named after the world, then a `0x4C` page with **one** rectangle, inset 1/1024 from the texture's
edges. Disc check (2026-10-07): 27 of the names `level0`-`level100` have such a file. The three checked are 256 × 256,
4- or 8-bit palettised, filter linear, addressing wrap in both directions (texture flags `0x1102`). The texels
(decoded): the streets and open ground are **transparent** (alpha 0), the blocked areas grey `(128, 128, 128)` at
alpha about 0.73, and a few areas (inside buildings the player can enter) a lighter opaque `(192, 192, 192)`. So the
disc shows the world through its streets and a grey tint elsewhere. Confirmed (disc) for the file and contents; the
meaning of the lighter areas is inferred.

**Nothing is drawn without the map**: `Radar_Render` draws the disc only when the map instance (`+0x94`) exists and is
resident, and the blip batch `+0x96` is resident; there is no plain grey disc. It ignores the page's rectangle and maps
`u`, `v` over the **whole texture** (the page's texture, `**(page + 0x14)`), and values outside 0-1 are **clamped** to
the edge texels, not wrapped: confirmed (runtime) by the GS dumps of `level99` ([Rendering: HUD](rendering.md#hud)),
where the radar is the only clamped texture of the frame, although the texture's own flags (`0x1102`) say wrap, so the
radar's draw path sets the addressing (where is not traced). The fans are drawn in GS context 1 with alpha test GEQUAL
`0x40` (of `0x80`) and `AFAIL` `FB_ONLY`, Z test always, no Z write (confirmed (runtime), same dumps). Since a failing
pixel is still blended into the colour and the draw writes no Z anyway, the test changes nothing on screen: low-alpha
texels (the streets) are blended at their own alpha, not dropped (inferred from the GS register meanings). The texel is
multiplied by the disc colour below (vertex colour; inferred, the Im2D default). Confirmed (code) at `0x001c60b0`.

**Where the map is read.** The level record's three floats map world metres to the texture
(`W_GameState + 0x14d4 + index × 0x84`, set by `CfgLevelName` arguments 13-15, [binding](../references/bindings/config.md#cfglevelname)):
`+0x6c` an x offset, `+0x70` a y offset and `+0x74` a scale in metres per texture width. The disc's centre in the
texture is

```text
u = (x + offsetX) / scale        v = (offsetY − y) / scale
```

for the radar's world position `(x, y)` (radar `+0x70`, `+0x74`: the player's; the world's `z` is up), and its radius in
texture units is `zoom / scale`, times the same video-mode factors `fx`, `fy` as the disc's screen radii (below), so
the map is not stretched by them. Confirmed (code) at `0x001c60b0`; runtime, `level99`: offsets −1.33 and 61.55, scale
95, the player at (75.155, 41.135) (radar `+0x70`, while the camera stood at (80.32, 41.12)), so the disc showed the
texture around (0.78, 0.21) with a radius of 0.53: a 50 m disc covers half of `level99`'s 95 m map.

**Zoom.** Radar `+0x20` is the radius shown, in metres. Each frame it moves toward a target by `k = min(0.0004 × ms,
1)` of the difference (`ms` the game time since the last frame): target = (`rest` + (`fast` − `rest`) × min(speed, 12)
/ 12) × `zoomScale`, with `rest` = `+0x28`, `fast` = `+0x24`, the player's speed in m/s and `zoomScale` = `+0x2920`
(`HUDSetRadarZoomScale`, 1.0 by default). Confirmed (code); runtime, `level99`: `rest` 50 m, `fast` 75 m, scale 1.0, so
the disc shows 50 m around a standing player and 75 m around one running at 12 m/s.

**Rotation.** The disc is a fan of 32 segments: vertex `i` sits at screen angle `s = π/2 − i × 2π/32` and samples
the texture at angle `t = −h − i × 2π/32`, where `h` is the camera's heading (`Vec_Heading` of `Quat_AxisY`: the
heading of the camera's forward, clockwise from world +y):

```text
screen  = centre + (Rx × sin s,  Ry × cos s)
texture = (u, v) + (ru × cos t,  −rv × sin t)
```

(`0x004b8c40` is `sinf`, `0x004b8a70` `cosf`.) **What it looks like, confirmed (runtime)**, PCSX2 2.9.94, `level99`
street with the camera looking along −x: the disc shows a plain top-down view of the map, **not mirrored**, turned so
that **screen up is the camera's forward and screen right the camera's right** (a simulation of the formula with that
orientation matched the screenshot texel for texel; the mirrored reading did not). Implement it in those terms: a
world point at offset `(right, forward)` metres from the player in the camera's ground axes is drawn at `centre +
(right × Rx, forward × Ry) / zoom` (overlay y up), sampling the map at its own `u`, `v`. Radar `+0x80` holds the **active
camera's rotation** (a quaternion; confirmed (runtime): equal to the camera's), `+0x70` the player's position.

**Blips** use the same frame: `Radar_Update` places a blip at `centre + (right, forward) × 0.12 / zoom` in overlay
units, or on the edge at `0.9 × 0.12` along that direction beyond the zoom ([GUI](gui.md#fn-radarhud)). Confirmed
(runtime): a Warrior 27.8 m ahead and 9.4 m to the right of the player, zoom 50, was placed at `(+0.02, +0.07)` from
the centre, upper right on screen. The blips' scale (0.12 per zoom) is not the map's (`Rx`, `Ry` per zoom), so a
blip and the map under it can be a few per cent apart; that is the original's.

**Draw order.** `GameMode_DrawOverlays` (`0x00156658`) calls `HUD_Render` (which draws the disc at once through
Im2D) and only then `ResourceMgr_RenderOverlay`, the sorted pass of every sprite batch, so the disc is **under every
blip, the player arrow and all HUD text**, whatever their depth keys. Confirmed (code).

**Overlay units to pixels** (default mode, 640 × 448): the overlay camera's view window at depth 1 is ±0.725 by ±0.5
([Graphics](graphics.md#2d-drawing)), so one unit is 441 px across and 448 px down; overlay y is up. Confirmed
(runtime) through the disc's centre and a blip's size ([GUI](gui.md#radar-icons)).

**Size and shape.** The disc's outer radius in overlay units is

```text
R = 0.9 × 0.19 × w / 2        (0x0050e9bc = 0.9; w = the third value the active camera's slot +0xc4 returns)
Rx = R × fx,  Ry = R × fy
```

with the video-mode factors `fx`, `fy` below. It is drawn in two parts (`Im2D` helpers, 32 segments): a filled disc
out to **0.825 R** (`0x0017bc28`, alpha of the colour below) and a ring from 0.825 R to R whose alpha falls from 240
to 0 (`0x0017b8a8`), a soft edge. Confirmed (code). Measured (runtime): the whole disc with its soft edge about
110 × 100 px of 640 × 448, so with 441 and 448 px per unit (below) `Rx` ≈ 0.125 and `Ry` ≈ 0.112, R ≈ 0.113 and
`w` ≈ 1.33 (inferred: the overlay camera's 4:3 width, not the 1.595 assumed before).

| Device flags ([Graphics](graphics.md#video-mode)) | fx | fy |
| --- | --- | --- |
| `0x01` set, `0x20` clear, 4:3 (the default) | 1.1 | 1.0 |
| `0x01` set, `0x20` clear, 16:9 (`0x04`) | 0.8 | 1 + 1.5 × (a − 1) |
| `0x01` and `0x20` set, 4:3 | 1.0 | 1 + 1.5 × (a − 1) |
| `0x01` and `0x20` set, 16:9 | 0.85 | 1.0 |
| `0x01` clear, 4:3 | 1.0 | 1.3 |
| `0x01` clear, 16:9 | 0.85 | 1.5 |

`a` is the device's slot `+0xc4` value (1.0 unless changed; not traced). Confirmed (code) at `0x001c60b0`.

**Colour.** The disc is drawn in one of three RGBA colours (`0x0050e9e8`): state 0 **(191, 191, 191, 240)**, state 1
**(100, 120, 200, 240)** (blue), state 2 (100, 50, 50, 140) (no caller sets it); a change of state blends from
the old colour to the new over 500 ms. The state (radar `+0x38`, the previous `+0x3c`, the change's time `+0x40`) is
set by `0x001c4500`, which only `Human_SnapToGround` (`0x0023eab8`) calls for the player, with 1 or 0 by what the player
stands on (`0x001b26a8` / `0x001b2790`): blue on shadow ground where he may hide, grey otherwise
([Stealth: the HUD cue](stealth.md#hud-cue)). Confirmed (code). The `(143, 143, 143, 255)`
written at `0x001c60b0` is a stack word the drawing does not read.

**On and off:** `HUDTurnOnRadar` / `HUDTurnOffRadar` set radar `+0x04` and the HUD's `+0x177b0`. During a screen fade
(`0x005fdeb8 + 0x1d4` or `+0x1d8`) both radars are turned off; when no fade runs and HUD `+0x177ac` is set, `HUD_Update`
turns them back on if `+0x177b0` is 0, so a radar turned off by a script stays off only while `+0x177ac` is 0. Confirmed
(code); who sets `+0x177ac` is not traced.

## The target panel

While the player has a target (human `+0xc8`, the one attacks and grabs aim at) that is not an ally (`0x00222a90`) and
not down (`0x00227dd8`), `TargetPanel_Update` (`0x0020e6f8`, called from `0x00210e48`) shows a panel for it: its name
and a bar. Each player has two panel slots (`0x0063f240` + player × `0xd00` + slot × `0x680`); a new target takes a
free slot or the older one, so the last two targets can show side by side. Confirmed (code); not seen at runtime.
The target panel belongs to the **Armies of the Night panel** (`ANHud`, [its
functions](hud-functions-panels.md#fn-anhud)): the only
`TargetPanel` is built by `ANHud_Construct` and updated by `ANHud_Update` (`0x00210e48`), and `HUD_InitLevel`
makes `ANHud`s only in levels 60-69, so the story HUD never shows it. Confirmed (code) at `0x0020fd80`,
`0x00210e48`.

- **Layout** (per player, `0x0050f6a0` + player × `0x80`, GUI units): base (0.085, 0.14) for player 0 and (0.81,
  0.14) for player 1; the second slot 0.22 to the right (player 0) or left (player 1, `+0x70`). The name is a markup
  text (`"<SIZE 1.0><COLOR ..."` at `0x005596e0` with the target's name) at base + (-0.09, 0.06), aligned left (player
  0) or right (player 1); two sprites at base + (-0.06, 0.04) with sizes 0.08 and 0.065, the first tinted
  `(133, 74, 172, 255)`; the bar at base + (-0.13, 0.06) offset by (0.062, 0), 0.24 × 0.018, scaled by the camera's
  distance factor (the active camera's slot `+0x1fc` × 0.75; × 1.15 when the device's slot `+0xb8` is set; × 0.7 in
  progressive mode, `0x001c22b0`).
- **The bar** (`0x001a1be8`): red `(255, 16, 16)` and green `(115, 183, 11)`. When `0x00223e20(target)` is false
  its fill is the target's health / maximum (`0x00222e40` / `0x00222e60`). When it is true (what it tests is not
  traced) the fill is a percentage (`0x00222ef0`, 0-100) in three bands, each filling over its points with the previous
  band's colour behind it: below 40 `(37, 37, 37)`, 40-70 `(128, 100, 0)`, 70 and above `(76, 122, 27)`.
- **Fade:** each slot keeps its last update time; 2,000 ms after it (`0x0050f9e4`) the slot fades out over 1,000 ms
  (`0x0050f9e8`) and is freed below 10 % alpha, or at once when the target dies or its fill drops under 0.01.

## The health rings {#the-health-rings}

The game's health display: two flat rings on the ground around a human's feet, drawn in the world (not by the HUD's
`Render`), the **outer ring showing health** and the **inner ring power**, or **rage** while raging. The source calls
them reticules (`HuForceEnableReticule`). Confirmed (code) at the functions below; confirmed (runtime), PCSX2 2.9.94,
on copies of quick-save slots 1 and 6 (`level99`), writing health, power, rage, the raging flag and `0x005104f8` over
PINE, reading the queued rings at `0x005fd330` and taking screenshots. The player stood still (no stick input) except
for one walk at stick magnitude 0.5 straight up; the camera was turned with the right stick at 0.7 right.

**Who gets one.** `Reticules_Update` (`0x0024b780`) runs every update from the task manager (`0x003a31a8`). It does
nothing in an Armies of the Night level (`0x0041d110`) or while the HUD is hidden (HUD `+0x177a0`, `0x00617fe0`), so
`HideHud` and a scene's letterbox take the rings away with the rest. Then, for each player (game state `+0x228` + 4 ×
i, `+0x224` players):

- **The player's own rings**, when the player is not airborne (state `0x1c00000000`), not in a scene, not climbing
  over (record `+0x08` `0x40`) and not down or dead. Each frame this also turns the player's blob shadow on (render
  instance `+0x2b4` = 1, [Lighting](lighting.md#humans)).
- **The player's current target's rings** (the human its `+0xc8` names), with the same tests and not in state `0x1cc`;
  for a non-player target only the outer (health) ring. A new target fades in over 500 ms (alpha = elapsed × 0.51),
  and leaves at once when it stops being the target.

**When the player's rings show** (each player keeps an entry `{handle, first seen, last trigger}` in a list of up to
four at `0x006c72d8`, `0x0024b190`):

- a **trigger** sets the last trigger to now: **SELECT newly pressed** (`0x00144bf0(pad, 0x100)`, pad record by the
  per-player `+0x19`) while the per-player record's `+0x1b` is set (1 for the player in `level99`), or the HUD's
  per-panel request byte (HUD `+0x225d0` + panel, `0x001b28a0`), which **using a flash** sets (`0x00284280`) and the
  update clears;
- **in a fight stance** (record `+0x00` & 3, [Combat](combat/index.md#state-flags)) or with **health at or below 20 %**
  the last trigger is renewed every update, so the rings stay;
- otherwise the rings are drawn only while the player is in the list and within 4,500 ms of the last trigger: they
  fade in over 500 ms after first appearing, hold, and from **4,000 ms after the last trigger** fade out linearly over
  500 ms (alpha = (500 − (t − 4000)) × 0.51);
- `HuForceEnableReticule` (`0x005104f8`) draws every player's rings at alpha 255 every update instead.

Confirmed (runtime): at 90 % health with no trigger no ring showed; SELECT pressed over PINE brought them up about a
second later and they were gone some 5.5 s after; with `0x005104f8` set they stayed.

**What they measure** (`Reticule_QueueHealthRings`, `0x0024a230`). Each ring is a circle of 64 segments
(`0x00510500`) split into three arcs, in this order from the start angle: the **fill** (colour A), the **lost** part
(colour B) and the part **beyond the capacity** (colour C). Two numbers on a 0-100 scale set them: the fill ends at
`value` % of the circle and the lost part at `capacity` %. Both rings scale by the human's power class byte `+0x40`
([Power classes](characters.md#power-classes)) as `k` = byte / 100 (35 for Rembrandt's class 64, confirmed (runtime);
the field is otherwise untraced):

| Ring | value | capacity | A | B | C |
| --- | --- | --- | --- | --- | --- |
| outer, health | health % (record `+0x144` / maximum × 100, clamped 0-100) × `k` | 100 × `k` | by health, below | black | `(60, 60, 60)` |
| inner, power (players only) | power % (record `+0x148` / its maximum `0x00223068` × 100, clamped) × `k` | 100 × `k` | `(100, 100, 100)` | black | `(60, 60, 60)` |
| inner while raging (human `+0xe0` `0x80000`) | rage (human `+0x650`, raw, not divided by its maximum) × `k` | 100 × `k` | `(217, 158, 12)` | black | `(60, 60, 60)` |

All colours take the ring's alpha. So for Rembrandt at 35 % health a short orange arc fills 12 % of the outer circle,
black runs on to 35 % and the rest is dark grey (confirmed (runtime)); with the class byte set to 100 the arcs reached
25, 50 and 90 % of the circle at those healths (confirmed (runtime)).

**The health colour** (A of the outer ring): above 75 % green `(76, 122, 27)`; at or below 0.1 % `(16, 16, 16)`;
otherwise `(158, 24 + int(h × 1.1571), 24)` with `h` the health % (`h × 0.0133 × 87`), orange at 75 %, red near 0
(runtime: `(158, 81, 24)` at 50 %). For a player:

- **low health**: above 0.1 % and at or below 25 % (`0x00510514`) the fill **blinks**: black for 232 ms
  (`0x00510510`), its colour for the next 232 ms, and again (one clock for all players, read from the clock at
  `0x0050b8b8`, inferred: the real-time clock); seen at runtime at 25 %;
- **rage full**: when rage reaches the Warrior class's maximum (`0x00223260`, [Combat](combat-rage.md#rage)) after being
  below it, the whole outer ring turns gold `(128, 100, 0, 255)` for 200 ms (`0x00510528`), then shows normally for
  200 ms, three times;
- **raging**, once those flashes are done: the fill is light grey `(191, 191, 191)` (runtime: confirmed).

**Bosses**: for a target of class `+0x11b` 13 (`0x00223e20`) outside Rumble the outer ring shows three bands like the
[target panel's bar](#the-target-panel): from 70 % `(76, 122, 27)` over `(128, 100, 0)`, from 40 % `(128, 100, 0)`
over `(134, 26, 0)`, below that `(134, 26, 0)` over black, each band filling over its range (× 3.33, or × 2.5 for the
last); the value is the band's fill, not the health. Not seen at runtime.

**Shape and placement** (confirmed (code); the numbers confirmed (runtime) from the queued records):

- Both rings are fans of 64 triangles from the centre, radius 0.6 × `s` in the ring's own units, then scaled by **0.6
  (inner) and 0.9 (outer)**: 0.36 × `s` and 0.54 × `s` metres to the rim. The visible band is the texture's.
- `s` is the **hit pulse**: min(1 + `p` / 100, 1.15). Each hit sets the target `p` (human `+0x5d2`) to the damage
  (`Human_AddPendingDamage`, `0x00264bd8`); `p` (human `+0x5d4`, clamped 0-100) moves toward it by 45 (`0x0051051c`)
  per update while the ring is drawn, and when it arrives the target goes back to 0, so a hit swells the rings by up
  to 15 % and shrinks them back.
- They lie **flat** in the horizontal plane (not turned to the ground's normal), centred on the human's position, its
  height + 0.055 m (`0x00510518`) + 0.001 m for each human's rings drawn before them this update (`0x00510558`, so
  rings do not fight in depth); the inner and outer of one human share that height. Runtime: player at z 0.2231, rings
  at 0.2791.
- **Start angle** = the player camera's heading + 2.5 rad (`0x00510520`); the heading is `atan2(x, y)` of the camera's
  orientation quaternion turned on +y (`0x00335f48`). Segment `k` (0-64) is at start + `k` × 2π / 64, at
  (r cos, r sin) on the game's x and y. So the arcs **turn with the camera, not the human**: on screen the fill starts
  at the front right of the feet (about half past four on a clock face) and grows clockwise, through the front, then
  the left; the same place after the camera turned half round (confirmed (runtime)).
- **Texture**: `part_page1` (resource instance 9, or 10 in the second view, the blob shadow's sheet), centred on the
  middle of rectangle 1; each rim vertex takes u = centre u + 0.062 cos, v = centre v − 0.124 sin (whole-sheet units),
  the centre vertex the middle. Runtime: the middle is (0.6973, 0.3789).
- **Colours per vertex**: the centre has colour A; each rim vertex k has C when 64 − k ≤ floor(64 × (100 − capacity) /
  100) (and that is above 0), else B when 64 − k ≤ floor(64 × (100 − value) / 100), else A (or black when blinking).
  Drawn with **flat shading**, so each segment takes one vertex's colour and the arcs end on hard edges.
- **Drawing** (`GroundRing_DrawQueued`, `0x0017b2e0`, from the viewport pass `0x00156408` after the world and the
  sprite batches, so after the blob shadows, [Boot](boot.md#one-frame)): the queue is drawn and emptied once a frame,
  each ring as an `RwIm3D` triangle fan (`0x00196b58`) in every viewport, with the shade mode flat, the ring's raster,
  fog off, and the blend and Z states as the world pass left them; the legs hid the ring behind them at runtime (Z
  test on).

**Markers on targets** (confirmed (code)): while a player holds L1 (record `+0x00` `0x8`, [Combat](combat-targets.md#targets))
at a non-player target, a sprite of `part_page1` rectangle 0 lies under the target at 0.041 m, size 0.9 × `s`, white
at the ring's alpha (black in Rumble), human `+0x66c` = 1 (only while the per-player `+0x1b` is set). With two players
on one screen (not Rumble), each player also gets an icon above the head (`0x0024b2a8`, `Human_AttachSpinningIcon`).
In **Rumble** (a level numbered 100 or more, `0x0041d160`) a third part is drawn first (`0x00249cb0`): a disc of
rectangle 64 at 0.038 m in the fighter's gang colour (player 1's gang `(134, 26, 26)`, player 2's `(128, 100, 0)`,
others `(35, 83, 188)`), which turns the fighter's blob shadow off, and for a player a pointer (rectangle 65 or 66)
along the camera's view; in Rumble the inner ring is not drawn and even a class-13 target gets the plain health ring.
