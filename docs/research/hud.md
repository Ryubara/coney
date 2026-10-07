# The in-game HUD

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made in
PCSX2 2.9.94 (2026-10-06) on a copy of quick-save slot 1 (`level99`, checkpoint 3), reading the HUD over PINE, writing
the player's rage, health and the panel's force-show flag, and measuring PCSX2's screenshots (1240 × 930 for the
game's 640 × 448); they say so.

## Purpose

What the HUD puts on screen during play, where, in which colours, and what drives each part: the player panel (name
banner, rage meter, score, money and item counters), the hint box, objective and announcement messages, the counter
panels, the instruction arrow, the radar's place on screen, and the calls that show and hide it all. The sprite and
text machinery underneath (widgets, sprite sheets, fonts, markup, draw order) is on [GUI](gui.md); the radar's blips
and icons are on [GUI: the radar](gui.md#radar-icons); the script bindings are in the
[HUD bindings](../references/bindings/hud.md).

In short: **health is not on the screen's edge but on the ground**: two flat rings at the player's feet, the outer one
showing health and the inner one power (or rage while raging), shown on SELECT, in a fight stance, at low health and
after a flash ([The health rings](#the-health-rings)); the player's current target gets a health ring too. The player
panel at the top left is the character's **name banner**, tinted with the rage colour, a thin **rage meter** under it,
the **score** and the **money**, with up to four **item counters** (flash, spray paint, handcuffs, keys) beside the
money. It shows when something on it changes (or SELECT is pressed), stays 2 s and fades out over 1 s. Hints sit in a
dark box at the bottom left; objective and announcement messages appear at the same place and hide the hint box while
they show. The radar is a disc of the level's map at the bottom right; there is no full-screen map. START opens the
pause menu ([Pause menu](pause.md)).

## Original structure

`GUI/HUDInterface.cpp` (`0x001ad588`-`0x001b1640`) and the HUD functions after it (`0x001b1640`-`0x001b5ff0`, the
bindings' callees), `GUI/TutorialHUD.cpp` (the hint box), `GUI/ScrollInHUD.cpp` (queued messages),
`GUI/RadarHUD.cpp`, `GUI/ChecklistMessageHUD.cpp`; the player panel class lives at `0x00211ca0`-`0x00214c28`, after
the profile-manager screens, in a file not yet named ([Source map](source-map.md#gui)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00600840` | the HUD object | one static object, about `0x22600` bytes | confirmed (code) |
| `0x001acee0` | `HUD::HUD` | constructor: every part below; `+0x08`/`+0x0c` point at the two player panels | confirmed (code) |
| `0x001af010` | `HUD_Update(hud, pad0, pad1)` | per frame from mode 1 ([Level loading](level-loading.md)) | confirmed (code) |
| `0x001b1688` | `HUD_Render(hud)` | from the overlay pass `0x00156658` ([Boot](boot.md)) | confirmed (code) |
| `0x001b1f38` / `0x001b20f8` | `HUD_HideAll` / `HUD_ShowAll` | `HideHud` / `RestoreHud` | confirmed (code) |
| `0x001b2030` / `0x001b2088` | hide / show both player panels | `HidePlayerHud` / `ShowPlayerHud` | confirmed (code) |
| `0x001b2200` | `HUD_AttachPlayer(hud, kind, slot)` | binds a player panel to a human (from `Human_MakePlayer`) | confirmed (code) |
| `0x001b2610` / `0x001b2658` | radar on / off for one player | | confirmed (code) |
| `0x00211ca0` | `PlayerHUD::PlayerHUD` | the player panel (`0x4a50` bytes; interface vtable `0x0053edf8` at `+0x4120`) | confirmed (code) |
| `0x00212840` | `PlayerHUD_Init(panel, player)` | builds the panel from the layout table | confirmed (code) |
| `0x00214138` | `PlayerHUD_Update` | values, colours and positions each frame | confirmed (code) |
| `0x00213290` | `PlayerHUD_Render` | fade, rage flashing, draws the parts | confirmed (code) |
| `0x0020dc98` | `PlayerHUD_SetBanner(panel, kind)` | picks the name banner | confirmed (code) |
| `0x00211ef8` | `PlayerHUD_ApplyVideoMode` | patches the layout table per video mode | confirmed (code) |
| `0x001a1008` / `0x001a1138` | `HudBar_Setup` / `HudBar_Draw` | the meter used for rage (and the mini-game bars) | confirmed (code) |
| `0x001b6c50` | `HudCounter_Setup` | icon plus number (the item counters) | confirmed (code) |
| `0x001cdc80` | `HintBox_Update` | `TutorialHUD.cpp`; the box at HUD `+0x8a10` (`0x00609250`) | confirmed (code) |
| `0x001ce3c0` / `0x001ce218` | `HintBox_Queue` / `HintBox_InsertByPriority` | queue a hint by priority ([Hints](#hints-hudsettutorialtext)) | confirmed (code) |
| `0x001ce1e8` / `0x001ce718` | `HintBox_QueueGameHint` / `HintBox_WithdrawGameHint` | the game's own hints, by `CfgTutorialMessage` id | confirmed (code) |
| `0x001ce5c0` / `0x001ce748` / `0x001ce550` | `HintBox_Withdraw` / `HintBox_FlushPriority` / `HintBox_Contains` | remove one hint, flush by priority, `HUDCheckTutorialText` | confirmed (code) |
| `0x001bb250` / `0x001bb2f0` | `MarkupText_SetText` / `MarkupText_IsExpired` | the markup text widget's text and its `<DISPLAYTIME>` / `<FREEZE>` end | confirmed (code) |
| `0x0019ef80` / `0x0019f1b0` / `0x0019f528` | `ActionPrompt_Setup` / `_SetText` / `_Update` | the action prompts at HUD `+0x6b40` ([Action prompts](#action-prompts)) | confirmed (code) |
| `0x001b3cb0` | `HUD_ShowAnnouncement(hud, kind)` | a built-in announcement in the widget at `+0xe340` | confirmed (code) |
| `0x001ce9a8` | `Tutorial_CallCallback` | calls the `HUDSetTutorialCallback` function on each player-1 hit ([Tutorial callback](#tutorial-callback)) | confirmed (code) |
| `0x001c8b08` / `0x001c9028` | `ScrollIn_Queue` / `ScrollIn_Update` | queued messages at HUD `+0x8dd0` | confirmed (code) |
| `0x001dad88` | `HUD_SetObjective` | `HUDSetObjective` | confirmed (code) |
| `0x001b7330` | `InstArrow_Update` | the instruction arrow at HUD `+0x134a0` | confirmed (code) |
| `0x001c3de0` | `Radar_Setup` | the radar's batches and map texture | confirmed (code) |
| `0x001c60b0` | `Radar_Render` | the radar disc ([GUI](gui.md#radar-icons) has the blips) | confirmed (code) |
| `0x0017bc28` / `0x0017b8a8` | `Im2D_DrawTexturedDisc` / `_Ring` | a textured disc, and a ring with a fading edge | confirmed (code) |
| `0x0020e6f8` | `TargetPanel_Update` | the target's name and bar | confirmed (code) |
| `0x00221108` | `Human_ReportBarsToHUD` | sends health and power fractions to the HUD, which ignores them | confirmed (code) |
| `0x0024b780` | `Reticules_Update` | per update: who gets a health ring, with what alpha ([The health rings](#the-health-rings)) | confirmed (code), runtime |
| `0x0024a230` | `Reticule_QueueHealthRings(yaw, lift, human, alpha, view)` | one human's two rings: values, colours, size | confirmed (code), runtime |
| `0x0017b1e0` / `0x0017b2e0` | `GroundRing_Queue` / `GroundRing_DrawQueued` | the ring queue (`0x005fd330`, `0xc0` bytes each, count `0x0050ccd4`) and its draw | confirmed (code), runtime |
| `0x0024af78` / `0x00249cb0` | `Reticule_AddTargetMarker` / `Reticule_AddRumbleTeamDisc` | the locked target's marker; the Rumble team disc | confirmed (code) |

## Data

### Coordinates

Positions are **GUI coordinates** ([Graphics](graphics.md#2d-drawing)): `x` right and `y` down, `[0, 1]` across a
safe area that maps to 5.2 %-94.8 % of the width and 4.5 %-95.5 % of the height. On the 640 × 448 screen:

```text
px = 640 × (0.0517 + 0.8966 × x)        py = 448 × (0.0455 + 0.9091 × y)
```

The layout tables store a point as four floats `(x, 0, y, 1)` (the 3D vector form, `y` in the third float); sizes as
`(w, h)` in the first two. Confirmed (code): `vadd.xyz` of table vectors at `0x00212840`, and the drop shadow
`(0.0025, 0, 0.004)` at `0x001a2690`.

**Video modes.** Every value below is the **default mode** (NTSC, interlaced, 4:3: device flags `0x01`,
[Graphics](graphics.md#video-mode)), which is what the static data holds. `0x00211ef8`, `0x001af010` and
`0x001cdc80` overwrite many of them each frame for 16:9 (`0x04`), progressive (`0x20`) and the other modes; those
variants are not listed here. Confirmed (code).

### The player panel layout (`0x0050fa10`)

A **base** per player, then a **table** of `0x240` bytes per player at `0x0050fa40 + player × 0x240` whose entries are
offsets from the base. Bases: player 0 `(0.10, 0.05)`, player 1 `(0.785, 0.05)`; `0x0050fa30` `(0.04, 0.56)` is
used for player 1 in a two-player split (`0x00214138`). Confirmed (code); values read from the executable.

| Table offset | Part | Player 0 entry | Player 0 on screen (GUI) | 640 × 448 |
| --- | --- | --- | --- | --- |
| `+0x60`, size `+0x70` | name banner | `(-0.105, -0.005)`, size 0.06 | `(-0.005, 0.045)` | x 30, y 39 |
| `+0x80`, size `+0x90` | rage meter (left end) | `(-0.107, 0.035)`, 0.30 × 0.009 | `(-0.007, 0.085)` | x 29, y 55; 172 × 3.7 px |
| `+0xa0` | score | `(-0.103, 0.064)` | `(-0.003, 0.114)` | x 31, y 67 |
| `+0xc0`, `+0xe0` | money, its icon offset | `(-0.095, 0.104)`, `(0.012, -0.001)` | `(0.005, 0.154)` | x 36, y 83 |
| `+0x00` | text size of score and counters | `(0.04, 0.05)` | | |
| `+0x20`, size `+0x30`, colour `+0x10` | backing plate | `(0.07, 0.02)`, 0.46 × 0.095, `0x00111111` | | |
| `+0x100`...`+0x1e0` | the four item counters | see [below](#item-counters) | | |
| `+0x1f0`, `+0x210`-`+0x230` | nine tally marks | `(-0.095, 0.104)`, size 0.04 | | |

Player 1's entries differ only in x (for example the banner `(-0.015, -0.005)`, the meter `(-0.018, 0.035)`), so its
panel is the same shape at the top right. **Runtime check:** the rage meter's left end measured at 29 px across and
55 px down (screen fractions 0.047 and 0.124), the formula gives 29 and 55; the banner's left edge at x 0.05 of the
screen, and its centre at y 0.086, which fits a banner anchored by its left edge and its vertical centre (measured
for this widget only).

The **backing plate** (rect 78 of `part_page0`) is drawn with the table's colour `0x00111111`, whose alpha is 0, and the
fade never touches its alpha, so it is invisible in the default mode. Confirmed (code) at `0x00212840`,
`0x00214138`, `0x00213290`; not visible in the screenshots.

### The name banner

`PlayerHUD_SetBanner` (`0x0020dc98`) gives three sprite widgets the whole first rectangle of one sprite sheet, chosen
by a kind that `0x00229570` derives from the character id (human `+0xcc`):

| Kind | Character ids | Sheet record |
| --- | --- | --- |
| 0 | 5, 7-10 | `0x31` |
| 1 | `0xf`, `0x11` | `0x22` |
| 2 | `0x12`, `0x14`, `0xbc` | `0x23` |
| 3 | `0xb`, `0xd`, `0xe` | `0x1f` |
| 4 | `0x1e`, `0x20` (Rembrandt, id 32) | `0x2f` |
| 5 | `0x15`, `0x17`-`0x19` | `0x24` |
| 6 | `0x1a`, `0x1c`, `0x1d`, `0xbf` | `0x32` |
| 7 | `0x21`, `0x23`-`0x25`, `0xbe` | `0x30` |
| 8 | 1, 3, 4, `0xbd` | `0x21` |
| 9 | `0x26`-`0x28` | `0x20` |
| 10, 11, `0x15` | `0x2b`, `0x2c` / `0x2d`-`0x30` / any other whose s8 `+0x1b0` is 1 | `0x2a`-`0x2e` by language (`W_GameState + 0x120`: 0 `0x2a`, 1 `0x2e`, 2 `0x2b`, 3 `0x2d`, 4 `0x2c`) |
| `0x14` | any other whose s8 `+0x1b0` is 0 | `0x25`-`0x29` by language (0 `0x25`, 1 `0x29`, 2 `0x26`, 3 `0x28`, 4 `0x27`) |
| any other | any other (12 when `+0x1b0` is negative, else `0x14` + `+0x1b0`) | `0x31` |

The sheet record is the high half of the sprite word (`record << 16`, rectangle 0); the widget makes its own sprite
batch over that sheet. Confirmed (code); kind 4 shows "REMBRANDT" (confirmed (runtime)). Which character each other
banner names is not checked.

The three widgets: two black copies at depth 10,000 offset by `(+0.0025, +0.004)` and `(-0.001, -0.002)` (`0x00510054`,
`0x00510058`, `0x00510060`, `0x00510064`, alpha `0xff`), then the banner itself at depth 11,000, all of size 0.06 at the
banner position. The banner's colour is the **rage colour** (below), or `(100, 100, 200, 160)` while the human has state
flag `0x200000` (`0x00228168`, not traced). Confirmed (code) at `0x00214138`; the red banner confirmed (runtime).

### Colours

| What | RGBA | Where |
| --- | --- | --- |
| rage colour, normal (`+0x4148`) | `(170, 43, 43, 255)` red | `0x00212840` |
| rage colour, raging (`+0x4144`) | `(217, 158, 12, 255)` gold | `0x00212840` |
| meter background | `(80, 80, 80, 255)` grey | `0x00212840` |
| meter capacity | `(255, 255, 255, 255)` | `0x00214138` |
| flash and spray counters | `(99, 219, 75, 255)` green | `0x00212840` |
| handcuff and key counters | `(35, 83, 188, 255)` blue | `0x00212840` |
| money and score text | white `(255, 255, 255, 255)` | `0x00212840` |
| hint box background | `(0, 0, 0, 128)` | `0x0050eb90` |
| radar disc | `(191, 191, 191, 240)`, or blue `(100, 120, 200, 240)` ([below](#the-radar-on-screen)) | `0x0050e9e8` |

In a level numbered 100 or more (`0x0041d160`: the level record's `+0x04` ≥ 100) player 1's two rage colours are
swapped. Confirmed (code). The meter's red, white and grey confirmed (runtime).

### The rage meter

A `HudBar` (`0x001a1008`, the first `0x70` bytes of the player panel). Its fields, confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x00`, `+0x04` | width, height (GUI) |
| `+0x10` | left end `(x, 0, y, 1)` |
| `+0x20` | background colour |
| `+0x24` | capacity colour |
| `+0x28` | fill colour |
| `+0x2c` / `+0x2e` | first rectangle / sprite batch (rectangle 54, batch 3 = `part_page0`) |
| `+0x38` | fill fraction 0-1 |
| `+0x3c` | capacity fraction 0-1 |
| `+0x50` | drawn (0 hides it) |
| `+0x5c` | right to left |

**Values** (`0x00214138`): fill = min(rage, rage maximum) / 150 and capacity = rage maximum / 150, each clamped to
0-1. Rage is human `+0x650` and the maximum the Warrior class's s16 `+0x00` ([Combat](combat.md#rage)); 150 is the
constant `0.0066667`. So a Warrior with maximum 78 has a white strip over 52 % of the meter, and the red fill grows
along it. Confirmed (code); 40 of 78 confirmed (runtime): red to 27 %, white to 52 %, grey to the end.

**Drawing** (`0x001a1138`), left to right, all at the meter's height: the left cap (rectangle 56) in the background
colour; the body (rectangle 54) over the inner width (`width` less the cap's width) in the background colour; the
capacity strip (rectangle 55) over inner width × capacity in the capacity colour; the fill strip (rectangle 55) over
inner width × fill in the fill colour; the right cap (rectangle 57). The stretched rectangles are inset by one texel
on each side. Confirmed (code).

**Fill colour:** the rage colour for raging (human `+0xe0` flag `0x80000`) or not. **A full meter** (rage ≥ maximum)
plays `vags/interface/rage_indicator_02` once and flashes: with `t` = game time mod 400 ms, `f` = |200 − t| / 200 (a
triangle wave), the fill is gold while `f³ < 0.2` (`0x005100c8`) and red otherwise, so a short gold pulse every
400 ms. Confirmed (code) at `0x00214138`, period `0x005100cc` = 200.

**`FlashRageBar(player, n)`** (`0x001b3f58`, panel `+0x4114`): with `n` > 0 the whole meter is drawn for `n` frames
and skipped for `n` frames, repeating; `n` = 0 draws it every frame. Confirmed (code) at `0x00213290`. The tutorial
passes 5: on and off every 5 frames (1/6 s at 30 frames a second).

### Score and money

- **Score** (`0x001c7730`, panel `+0x70`): seven digits, `"%07d"`, of the player's score in the stats object
  (`0x006fe490`, [Inventory and statistics](player-state.md)); leading zeros grey and the rest white (confirmed
  (runtime), "0000628"). It counts toward a new value by (difference / 16) ± 1 per frame, and a change shows a
  `"+N"` (or `"-N"`) beside it that shrinks over the 400 ms before it is removed at 1,000 ms (`0x0050ea3c`,
  `0x0050ea40`). Drawn only in levels numbered below 100. Confirmed (code) at `0x001c7928`, `0x00213290`.
- **Money** (`0x001bea00`, panel `+0x400`): inventory item 2 ([Inventory items](../references/inventory.md)), clamped to
  -99..9999, counting toward a new value by (difference / 16) ± 1 per frame with a sound (cue `0x10`) while it counts,
  and a floating `±$N` line (`<MONEYPLUS>` / `<MONEYMINUS>` markup) for 1 s after a change. Confirmed (code) at
  `0x001becb0`. Hidden when 0 (inferred: the runtime panel with 0 dollars showed none).

### Item counters {#item-counters}

Four icon-plus-number widgets, each shown only while its count is at least 1 (count clamped to 9):

| Panel offset | Item | Icon (`part_page0` rectangle) | Colour | Getter |
| --- | --- | --- | --- | --- |
| `+0xe30` | 1 flash | 29 | green | `0x001c68e8` |
| `+0x1260` | 3 spray paint | 30 | green | `0x001c96e0` |
| `+0x1690` | 5 handcuffs | rectangle of `0x00212840` | blue | `0x001aaf88` |
| `+0x1ac0` | 6 skeleton keys | rectangle of `0x00212840` | blue | `0x001aadf0` |

They fill four **slots** in the order above, each counter taking the first free one (`0x00213718`). A slot's
position is an offset from the panel's base; x is relative to `x0` = `0x00510040` (player 0) or `0x00510044`
(player 1), and the slots move right as the money's digits grow. Line 1 is y `0x00510048`, line 2 y `0x0051004c`:

| Money | Slot 0 | Slot 1 | Slot 2 | Slot 3 |
| --- | --- | --- | --- | --- |
| 0 or less | `x0 − 0.095`, line 1 | `x0 − 0.045`, line 1 | `x0 + 0.005`, line 1 | `x0 + 0.055`, line 1 |
| 1-9 | `x0 − 0.045`, line 1 | `x0 + 0.005`, line 1 | `x0 + 0.055`, line 1 | `x0 − 0.09`, line 2 |
| 10-99 | `x0 − 0.028`, line 1 | `x0 + 0.02`, line 1 | `x0 + 0.07`, line 1 | `x0 − 0.09`, line 2 |
| 100-999 | `x0 − 0.005`, line 1 | `x0 + 0.042`, line 1 | `x0 − 0.092`, line 2 | `x0 − 0.04`, line 2 |

With 1,000 or more the slots keep their previous places. Confirmed (code) at `0x00213770`. A changed count makes the
panel show (the fade timer restarts). Icon size 0.04, text size the table's `(0.04, 0.05)`.

### Hint box layout (`0x0050eb50`)

Set at every `HintBox_Update` (`0x001cdc80`), default mode, confirmed (code) and read back at runtime:

| Offset | Value | Meaning |
| --- | --- | --- |
| `+0x00`, `+0x08` | 0.016, 1.0 | the text's x, and the bottom it sits on (one player) |
| `+0x10`, `+0x18` | 0.245, 1.0 | the same in a split screen |
| `+0x28` | 0.73 | the wrap width (inferred: the measured box is 0.73 wide) |
| `+0x40` | `0x80000000` | box colour, black at alpha 128 |
| `+0x50`, `+0x54` | -0.018, -0.035 | box offset from the text's top-left |
| `+0x60`, `+0x64` | 0.03, 0.035 | box size beyond the text's |
| `+0x70`, `+0x74` | 4,000, 200 | written every update but **read by nothing found** (the box's functions read only `+0x00`-`+0x64` through its pointer `+0x3a4`); a hint's time comes from its markup ([Hints](#hints-hudsettutorialtext)) |

The box is the text's rectangle grown by those amounts and drawn first; the text is placed so that its bottom is at
`y` = 1.0 less the box's height. **Runtime check:** the box spans screen x 0.052-0.708 and y 0.812-0.923 (GUI x
0.00-0.73, y 0.84-0.97).

## Behaviour

### The HUD's frame

`HUD_Update` (`0x001af010`) runs once per mode-1 frame with both players' pads; `HUD_Render` (`0x001b1688`) in the
overlay pass ([Boot](boot.md)). Mode 0xb (mission complete) runs the overlay pass but not `HUD_Update`, so the HUD is
drawn as it was left. Confirmed (code) at `0x0015d160`.

**Not during a letterbox.** `HUD_Render` draws **nothing** (and turns both radars off) unless player 1's screen
effects have the letterbox fully out: state `+0x1e8` = 0 and level `+0x1ec` = 0 ([Graphics](graphics.md#screen-effects)).
So no HUD part, text or prompt shows while a cinematic scene's bars are in or moving, whatever `HideHud` says; the
scene's subtitles are drawn by the caption system after the HUD ([Scenes: subtitles](scenes.md#subtitles)). Confirmed
(code) at `0x001b1688`. `HUD_Update` likewise does its work only while the HUD is active (`+0x177a4`) and player 1's
letterbox is fully out; otherwise it only turns the radars off. So the hint box, the scroll-in queue, the
announcements and the panels neither advance nor expire while a scene's bars are up, but do while the HUD is merely
hidden. Confirmed (code) at `0x001af010`. What shows the HUD again after a scene:
[Who shows the HUD again](#who-shows-the-hud-again).

**Render order** (`0x001b1688`), when the HUD is shown (`+0x177a0` = 1, set by `RestoreHud`): the radars and their
frames, the instruction arrow and two other widgets, the counter panels, the bars of `HUDEnableBar`, each **player
panel**, the announcement, per player the score boards and other panels, the centred custom announcement (`+0xe150`),
then the scroll-in messages, the hint box, and last the **action prompts**. When hidden (`HideHud`), only the player
panels' hide is applied (`0x001b2380`) and the centred custom announcement is still drawn when `+0x177a8` is set. The
2D pass sorts batches by depth afterwards ([GUI](gui.md#draw-order)), so this order matters only within one batch.
Confirmed (code); the roles of the unnamed widgets are not traced.

**What hides what** (the bottom-left text), confirmed (code) at `0x001b1688`:

| Showing | Hidden that frame |
| --- | --- |
| an announcement (`+0xe340`, until its markup time ends) or a mini-game panel | the scroll-in message, the hint box, the action prompts |
| a scroll-in message (objective) | the hint box |

Hidden parts are not paused: a hidden hint keeps its place and its timer runs on. The prompts are also not drawn in an
Armies of the Night level (`0x0041d110`).

### The player panel

**Attach.** `Human_MakePlayer` (`0x00229c40`) calls `HUD_AttachPlayer` for the human: if panel `slot` (0 or 1) is not
attached yet, it runs its `Init` once (`0x00212840`), attaches it, sets the banner (`0x0020dc98`), and hides it again
when the HUD is hidden (`+0x177a0` = 0); it returns the slot, or -1. The panel index is kept at human
`+0x380`. Confirmed (code).

**Health and power are not shown on the panel** (they are on the ground: [The health rings](#the-health-rings)). Each
frame `0x00221108` passes the player's health fraction (record `+0x144` / `+0x146`) and power fraction (`+0x148` or
`+0x14a` over their maxima) to `0x001b2430` / `0x001b2460`, which call `0x0020dfd0`, a function that returns at once.
Confirmed (code); confirmed (runtime): health 314 and 900 of 900 left the panel unchanged.

**Visibility.** The panel draws when attached (`+0x40f8`) and shown (`+0x40fc`). Its **fade**: `+0x411c` holds the
time of the last activity; 0 means "now" and is stamped on the next render. For 2,000 ms after it (`0x00510070`) the
panel is opaque, then its alpha falls linearly to 0 over 1,000 ms (`0x00510074`) and it is not drawn. The alpha
multiplies the banner, the meter's colours, the counters and the score. Confirmed (code) at `0x00213290`.

**Activity** (resets `+0x411c` to 0, confirmed (code)):

- `ForceShowPlayerHud(player, true)` (`+0x4104` ≠ 0): every frame, so the panel stays up until it is turned off;
- a change of score, money or an item count (`0x001c7928`, `0x001becb0`, `0x001ab038` and the other counters);
- SELECT pressed (`0x00144bf0(pad, 0x100)`) while the per-player record's `+0x1b` is set;
- the action prompt (below) naming one of the prompts `Spray`, `Flash`, `Blades` or `Give Mon...`.

Confirmed (runtime): with no activity the panel was invisible; setting `+0x4104` brought it up.

**Other parts** (drawn when their state applies, not seen in `level99`'s street): nine **tally marks** (panel
`+0x4150`, rectangles 366, every fifth 367, of `part_page0`, size 0.04, in the player's colour `+0x4148`, count
`+0x413c`, enabled `+0x4130`); the **mini-game panel** (`+0x3480`, `0x001bf7e0`: two dark `(32, 32, 32)` bars and
`hud_minigames` icons 5-9; the mugging drives it, `0x001b28b8`-`0x001b2918`, [Crimes](crimes.md#mugging));
the **Warrior command display** (`+0x1ef0`, `0x001a62c0`, the d-pad pictures of `big_font` 256-261
with command icons, `HUDShowWarCommand`). Confirmed (code) for the structure; their layouts are not covered here.

### Objectives (`HUDSetObjective`)

`HUD_SetObjective(slot, text, mode, silent, ms)` (`0x001dad88`), confirmed (code):

- The objective lines are kept in the **pause menu's checklists** (the menu at `0x0062e790`, `ChecklistMessageHUD.cpp`,
  lists of `0x360` bytes at `+0x20d0` and `+0x2430`, `0x001dd0b8`); mode 0 clears slot `slot` (`0x001dd178`) and sets
  the text in it (`0x001a4d20`), mode 1 clears it, mode 2 marks it (`0x001a5070(item, text, 1)`, inferred: ticks it
  off), and mode 3 sets and marks it with no message. Slot 2 goes to a third list at `+0x2790`. The lists are the
  pause menu's Objectives screen: current, bonus and overview ([Pause menu](pause.md#the-items-screens)).
- **Mode 0, not silent:** a **scroll-in message** is queued: a header (an objective icon, `<YOBJ>` for slot 0 at
  `<SIZE 0.8>`, `<BOBJ>` for slot 1, the HUD colour slot 4 or 6 of `CfgHUDColor`, then the HUD string `0xe5` or `0xe6`)
  followed by the text, at **(0.025, 0.88)**, size 0.05, for **`ms`** milliseconds (default 8,000), with the
  interface sound cue `0x11`. A text starting with `<AUTOINDENT` builds the header another way (`0x001dab30`).
- **Mode 0, slot 1,** while the game's tutorial hints are on (`W_GameState + 0x56e2`) and not in an Armies of the
  Night level: the first time only (game-state flag `0x40000`), hint `0x15` is queued in the hint box.
- **Mode 2:** two scroll-in messages, the header with the HUD string `0xe5`/`0xe6` and then the text; for slot 1 also
  `0x001b3cb0(hud, 2)` per player (not traced).

**Scroll-in messages** (`ScrollInHUD.cpp`, HUD `+0x8dd0`): a **first-in, first-out** queue with no priorities
(`ScrollIn_Queue` appends at the tail; nothing is queued when its pool `+0x548` has no free entry, and a message equal
to the one showing, header and text compared as strings, is dropped). The front message is shown at its position with
its wrap width (`0x0050ea50` one player, `0x0050ea4c` split), its y centred on the given y less `0x0050ea58`, and
removed once game time (`GameTimer +0x48`, so not while frozen) is past its start plus its `ms` plus 500 ms; the next
one starts on the following update. Confirmed (code) at `0x001c8b08`, `0x001c9028`.

### Hints (`HUDSetTutorialText`) {#hints-hudsettutorialtext}

The hint box (`TutorialHUD.cpp`, HUD `+0x8a10`, constructor `0x001cd8e0`, set-up `0x001cd988`) is a markup text widget
([GUI](gui.md#widget-classes)) over a box sprite (`part_page0` rectangle 78, depth 11,000). It shows **one hint at a
time** from a queue ([HUDSetTutorialText](../references/bindings/hud.md#hudsettutorialtext)). Confirmed (code) at the
functions named:

- **Queue.** A pool of **20** entries `{text, priority}` (box `+0x1f0`, free list `+0x294`); the queue (`+0x290`) is
  kept **sorted by priority, lowest number first, first-in first-out within a priority** (`HintBox_InsertByPriority`
  inserts before the first entry with a higher number). When the pool is empty a new hint is dropped. The text is
  kept by pointer, not copied.
- **Queueing** (`HintBox_Queue(box, text, priority)`, `0x001ce3c0`): a nil text clears the showing hint unless the game
  is paused. Otherwise the hint is inserted; if a hint is showing and the new priority is **lower** (more urgent), the
  showing hint is put back in the queue with its own priority and cleared, so the new one shows on the next update and
  the old one **starts again from the beginning** later. Equal or higher numbers wait.
- **Taking the next** (`HintBox_Update`, `0x001cdc80`, each HUD update and also during a freeze, from `GameTimer_Update`
  `0x00145a10`): when no hint shows, the front of the queue becomes the text (`MarkupText_SetText`), its priority goes
  to `+0x3ac`, interface cue `0x15` plays, and the box is laid out ([layout](#hint-box-layout-0x0050eb50)).
- **How long a hint stays** comes **only from its markup** (`MarkupText_IsExpired`, `0x001bb2f0`; when it returns true
  the text is cleared, vtable `+0x6c` = `0x001b9290`, and the next update takes the next hint):
    - `<DISPLAYTIME ms>`: gone `ms` after it was first laid out, timed on the clock at `0x0050b8b8` (inferred: the
      real clock, as it also times the caption fades during a freeze); in its last 1,000 ms its alpha falls as
      remaining × 0.255 ([GUI markup](gui.md#markup)), and the box's alpha follows (`0x001ce8b8`: 125 × the text's
      alpha / 255);
    - `<FREEZE ms>`: the game is paused for at least 2,000 ms and until `ms` has passed, or cross is released after the
      2,000 ms ([Boot](boot.md#timers)); the hint goes when the game runs again. A last line containing `press` and
      `to continue` is not drawn for the first 2,000 ms and then fades in over 334 ms (`0x001b9600`; matched in
      English only, inferred);
    - **neither tag: it stays** until it is flushed, withdrawn or interrupted by a more urgent hint.

  Disc check (counts only): of the hint strings, `config_strings_en.lua` has 7 `<DISPLAYTIME>` tags and `level99.lua` 8
  (2,500-8,000 ms); neither uses `<FREEZE>`, so most hints stay until the script removes them.
- **Removing:** `HUDFlushTutorialText(p)` (`HintBox_FlushPriority`, `0x001ce748`) removes every queued hint of priority
  `p`, and the showing one when its priority is `p`; 4 removes all. `HintBox_Withdraw(box, text)` (`0x001ce5c0`)
  removes one hint by its text pointer (the showing one only when the game is not paused). `HUDCheckTutorialText`
  (`HintBox_Contains`, `0x001ce550`) is true while the text is showing or queued.
- **Style:** text colour `0x005fd310` (178, 178, 178, 255) ([GUI](gui.md#colour-table)), markup lines `<CR2>` and
  `<CRM>` enabled; the widget's `+0x1d4` is 0.74 with one player and 0.52 split, or the number after a leading
  `<AUTOINDENT` tag (`0x001bb390`). Read at runtime (PCSX2 2.9.94, copies of quick-save slots 3-8, `level99`): a
  priority-3 hint showing with no end time (`+0x170` = -1), `+0x1d4` 0.74, box height (`+0x3b0`) 0.124.
- **The game's own hints.** `CfgTutorialMessage(id, text)` (`0x001cd888`) fills a table at `0x00622fd0`;
  `HintBox_QueueGameHint(box, id)` (`0x001ce1e8`) queues entry `id` at **priority 2**, and `HintBox_WithdrawGameHint`
  removes it. 19 functions queue them (human, brain, radar and objective code, for example hint `0x15` from
  `HUDSetObjective` above and hint 6 from `Humans_Update`); the two read show each hint once, behind a game-state flag
  and the tutorial switch `W_GameState + 0x56e2` (`HUDEnableGameTutorialText`). Which event raises which id is not
  catalogued.
- **Action-object hints** go in at **priority 0**, the most urgent ([Action prompts](#action-prompts)).

The box is hidden while a scroll-in message, an announcement or a mini-game panel shows ([What hides
what](#the-huds-frame)); it is drawn by `0x001ce8b8` and hidden by `0x001bb2b0`.

### The tutorial callback (`HUDSetTutorialCallback`) {#tutorial-callback}

Despite its place in the hint box (`+0x3a0`, `0x006095f0`), the callback has nothing to do with the hint text: it is
the combat tutorial's **"player 1 hit someone"** hook. Confirmed (code):

- `HUDSetTutorialCallback(name)` (`0x001b5e90`) stores the name pointer as given; nil stores 0.
- The only reader is `Tutorial_CallCallback(box, animId)` (`0x001ce9a8`): when the name is set and the Lua function
  is found (script slot `+0x4c`, [Scripting](scripting.md)), it is called at once with **one argument, the attacker's
  current anim id** (pushed as an unsigned number, slot `+0x74`; call `+0x8c` with 1).
- Its one caller is the attack-scoring step `0x002653d8(attacker, victim, landed)`, which `Human_ApplyPendingDamage`
  (`0x00265f70`) runs on the victim's pending hit when the attacker is pad-controlled (player record `+0x1b`): with
  `landed` 1 for a hit that lands and 0 for one a block stops (both call the callback). `0x002653d8` returns first when
  the attacker's brain `+0x4` is not 0 or the two are allies (`0x00290230`), then calls the callback only when the
  attacker's player index (`+0x1b0`) is **0**, before it awards the hit's rage ([Combat](combat.md#rage)). The anim id
  is the attacker's record `+0x20` (11 `X1`, 12 `S1`, 13 `XX2`, ...; [Combat](combat.md#attacks)).
- The id is read when the **victim** applies the pending damage, not when the hit was dealt. For a hit dealt as a
  clip ends, the attacker has already moved on: the strong grapple's damage is added at the end of 657 and applied as
  the hold 82 starts, so the callback gets **82** (84 from the rear). Confirmed (runtime)
  ([Strong grapple](combat.md#strong-grapple)).
- So it fires **once per hit, every hit**, for as long as it is set; nothing in the engine clears it after a call. It is
  cleared by `HUDSetTutorialCallback(nil)` and when the HUD is released at `UnloadLevel` (`0x001607b8` →
  `0x001ae980` → `0x001cdc20`, which zeroes `+0x3a0`).

### Announcements and other messages

- **`HUDSetAnnounceMsg(kind, text, flag)`** (`0x001b5b08`): kind **5** sets `text` in the **centred** widget (HUD
  `+0xe150`) at **(0.5, 0.25)** (`0x0050d470`; 0.35 in one other mode); any other kind shows the built-in message
  `kind` of the `CfgAnnounceMessage` table (`0x00622e20`) in the widget at HUD `+0xe340` (`HUD_ShowAnnouncement`,
  `0x001b3cb0`, which also looks up interface cue `0x14` into `+0xe4d4`), at **(0.02, 0.90)** with one player, (0.22,
  0.90) with two (`0x0050d4a0`, `0x0050d4a4`, `0x0050d4a8`). Each call **replaces** the text at once: there is no
  announcement queue. An announcement lasts as its markup says (`<DISPLAYTIME>`): `HUD_Render` clears `+0xe344` when
  `MarkupText_IsExpired` says so; without the tag it stays until replaced. While the `+0xe340` one shows it hides the
  bottom-left texts ([What hides what](#the-huds-frame)); the centred one hides nothing. `flag` goes to
  `0x00617fe8`. Confirmed (code) at `0x001b5b08`, `0x001b3cb0`, `0x001af010`, `0x001b1688`.
- **Counter panels** (`HUDGetNewPH`, five at HUD `+0xacb0`): rows at x 0.96 (`0x0050d510`), y 0.26 + 0.08 × row
  (`0x0050d518`, `0x0050d520`), filled from the top with the visible panels. Confirmed (code) at `0x001af010`.

### Action prompts {#action-prompts}

The "what can I do here" text at the bottom of the screen: **one prompt per player** (HUD `+0x6b40` and `+0x70d0`,
`0x590` bytes each), a scroll-in text widget (base `0x001e6e98`, vtable `0x00539270`) plus an optional two-frame
icon sprite at `+0x480`. Confirmed (code) unless marked:

- **Set-up** (`ActionPrompt_Setup`, `0x0019ef80`, from the HUD's init `0x001adb60`): position x **0.5**, base y
  `+0x464` = **0.86** (read at runtime; the code compares it with 0.86, 0.78, 1.0 and 0.8 for the other modes), size
  0.06, colour **(128, 128, 128, 255)**, font slot 3 (`part_page0`), not visible; `+0x468` the player.
- **Place** (`ActionPrompt_Update`, `0x0019f528`): one player: x 0.5, centred; y = base + the raise `+0x46c`. Two
  players: x 0.33 or 0.66 (`0x0050cfcc` / `0x0050cfd0`, 0.25 / 0.74 in 16:9).
- **The raise** (`ActionPrompt_SetRaise`, `0x0019f430`, each HUD update) keeps the prompt clear of the texts below it:
  while the hint box shows (box `+0x3a8` set and no scroll-in), raise = 0.05 (`0x0050d500`) − the box's height (`+0x3b0`);
  otherwise −0.02 (`0x0050d504`) − the showing scroll-in's height (scroll-in `+0x578`, 0 when none). A prompt of more
  than one line moves up a further 0.025 (`0x0050cfb4`) × its height in lines. **Runtime check** (PCSX2 2.9.94, quick
  save slot copies, `level99`): with a hint box 0.124 high the raise was −0.0738 = 0.05 − 0.124.
- **Its text** is chosen each update by `HUD_Update` for each player who is free to act (not in a scene, a paired
  move, a mini-game, a car or several other states, `0x001af010`), in this order: when `0x00225ff0` holds and the
  human's `+0xc4` handle resolves (inferred: something held), HUD string 1 or 0 by that object's `+0x5a0`; when
  `0x00279078` finds a human and the game state's check `0x0041e420(…, 1)` is at least 1 for either, string 4
  (neither traced further); the **action object** in reach (`0x00240888`): its prompt `+0x10`, and its hint `+0x14` is
  queued in the hint box **at priority 0** once (remembered at HUD `+0x177bc` per player, withdrawn by `0x001b3e20`
  when the object or its hint changes or none is in reach); else a talkable human within 1.5 m whose brain state is 0
  or 3 (`ActionPrompt_FindNearbyHuman`, `0x001acd60`): the HUD string its interface names (ids 1-388), its own text,
  or string 9. The strings are `GSTRING.HUD` entries ([Strings](gui.md#strings)); the action objects and their kinds:
  [Crimes](crimes.md#triangle).
- **Showing:** no text hides the prompt (`0x001b2490`); a new or changed text restarts it (`ActionPrompt_SetText`,
  `0x0019f1b0`; a text that parses as a number goes through `0x0019f128`). A prompt naming `Spray`, `Flash`, `Blades`
  or `Give Mon...` also wakes the player panel ([Activity](#the-player-panel)). With the cycle animation on
  (`HUDTurnOnActionCycleAnim`, `+0x45c`) the icon alternates between two sprite words (`+0x440`/`+0x444`) at the rate
  `+0x448` (`0x0019f328`).
- **Drawn last** in the HUD, and not while an announcement or a mini-game panel shows ([The HUD's
  frame](#the-huds-frame)).
- **Mini-game widgets:** the lock-picking dial (HUD `+0xf420` + player × `0x540`), the stereo theft's (`+0xfea0` +
  player × `0xb10`) and the mash meter (`+0xebc0` + player × `0x430`), [Crimes](crimes.md).

### The instruction arrow (`HUDEnableInstArrow`)

A sprite widget at HUD `+0x134a0`: rectangle 10 of `hud_minigames` (batch 11), size 0.06, colour `(191, 191, 191,
255)`, placed at `(x, y)` and turned by `angle` (`0x001b70e8`). It **bobs** along its direction: a step counter runs
from 0 to 10 frames (`+0x100`, `HUDSetInstArrowAnimSpeed`) by 2 per frame and back by 0.5, and the arrow is offset by
step × (sin angle, −cos angle) / 20 / 10 (`0x0050d61c` = 20). Confirmed (code) at `0x001b7330`; the sprite and
size confirmed (runtime). The enable flag is HUD `+0x134a4`.

### The radar on screen

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
resource slot 24 holds the sheet `0xe36542ae` (the CRC-32 of `level99`), resident, two rectangles. The same set-up keeps
a `part_page0` batch at `+0x96` for the blips. Which WAD or pack file holds a world's map sheet is not traced.

**Where the map is read.** The level record's three floats map world metres to the texture
(`W_GameState + 0x14d4 + index × 0x84`, set by `CfgLevelName` arguments 13-15, [binding](../references/bindings/config.md#cfglevelname)):
`+0x6c` an x offset, `+0x70` a y offset and `+0x74` a scale in metres per texture width. The disc's centre in the
texture is

```text
u = (x + offsetX) / scale        v = (offsetY − y) / scale
```

for the radar's world position `(x, y)` (radar `+0x70`, `+0x74`: the player's; the world's `z` is up), and its radius in
texture units is `zoom / scale`. Confirmed (code) at `0x001c60b0`; runtime, `level99`: offsets −1.33 and 61.55, scale
95, the player at (75.6, 41.2), so the disc showed the texture around (0.78, 0.21).

**Zoom.** Radar `+0x20` is the radius shown, in metres. Each frame it moves toward a target by `k = min(0.0004 × ms,
1)` of the difference (`ms` the game time since the last frame): target = (`rest` + (`fast` − `rest`) × min(speed, 12)
/ 12) × `zoomScale`, with `rest` = `+0x28`, `fast` = `+0x24`, the player's speed in m/s and `zoomScale` = `+0x2920`
(`HUDSetRadarZoomScale`, 1.0 by default). Confirmed (code); runtime, `level99`: `rest` 50 m, `fast` 75 m, scale 1.0, so
the disc shows 50 m around a standing player and 75 m around one running at 12 m/s.

**Rotation.** The texture coordinates turn by −(the camera's heading) (`0x00335d98` of the matrix at radar `+0x80`),
the screen positions start at π/2, so the map turns under a fixed disc with the view direction up. Confirmed (code);
that `+0x80` holds the active camera's matrix is inferred.

**Size and shape.** The disc's outer radius in overlay units is

```text
R = 0.9 × 0.19 × w / 2        (0x0050e9bc = 0.9; w = the third value the active camera's slot +0xc4 returns)
Rx = R × fx,  Ry = R × fy
```

with the video-mode factors `fx`, `fy` below. It is drawn in two parts (`Im2D` helpers, 32 segments): a filled disc
out to **0.825 R** (`0x0017bc28`, alpha of the colour below) and a ring from 0.825 R to R whose alpha falls from 240
to 0 (`0x0017b8a8`), a soft edge. Confirmed (code). That `w` is the overlay view's width (1.595 in the default mode,
[Graphics](graphics.md#2d-drawing)) is inferred from the runtime size: it gives R = 0.136, so Rx = 60 and Ry = 56
pixels of 640 × 448 with the solid part 50 × 46; measured (runtime): the solid part about 103 px wide, the whole disc
with its soft edge about 110 × 100 px.

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
stands on (`0x001b26a8` / `0x001b2790`; the condition is not traced). Confirmed (code). The `(143, 143, 143, 255)`
written at `0x001c60b0` is a stack word the drawing does not read.

**On and off:** `HUDTurnOnRadar` / `HUDTurnOffRadar` set radar `+0x04` and the HUD's `+0x177b0`. During a screen fade
(`0x005fdeb8 + 0x1d4` or `+0x1d8`) both radars are turned off; when no fade runs and HUD `+0x177ac` is set, `HUD_Update`
turns them back on if `+0x177b0` is 0, so a radar turned off by a script stays off only while `+0x177ac` is 0. Confirmed
(code); who sets `+0x177ac` is not traced.

### The target panel

While the player has a target (human `+0xc8`, the one attacks and grabs aim at) that is not an ally (`0x00222a90`) and
not down (`0x00227dd8`), `TargetPanel_Update` (`0x0020e6f8`, called from `0x00210e48`) shows a panel for it: its name
and a bar. Each player has two panel slots (`0x0063f240` + player × `0xd00` + slot × `0x680`); a new target takes a
free slot or the older one, so the last two targets can show side by side. Confirmed (code); not seen at runtime.

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

### The health rings {#the-health-rings}

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
- **in a fight stance** (record `+0x00` & 3, [Combat](combat.md#state-flags)) or with **health at or below 20 %**
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
- **rage full**: when rage reaches the Warrior class's maximum (`0x00223260`, [Combat](combat.md#rage)) after being
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

**Markers on targets** (confirmed (code)): while a player holds L1 (record `+0x00` `0x8`, [Combat](combat.md#targets))
at a non-player target, a sprite of `part_page1` rectangle 0 lies under the target at 0.041 m, size 0.9 × `s`, white
at the ring's alpha (black in Rumble), human `+0x66c` = 1 (only while the per-player `+0x1b` is set). With two players
on one screen (not Rumble), each player also gets an icon above the head (`0x0024b2a8`, `Human_AttachSpinningIcon`).
In **Rumble** (a level numbered 100 or more, `0x0041d160`) a third part is drawn first (`0x00249cb0`): a disc of
rectangle 64 at 0.038 m in the fighter's gang colour (player 1's gang `(134, 26, 26)`, player 2's `(128, 100, 0)`,
others `(35, 83, 188)`), which turns the fighter's blob shadow off, and for a player a pointer (rectangle 65 or 66)
along the camera's view; in Rumble the inner ring is not drawn and even a class-13 target gets the plain health ring.

### Showing and hiding {#showing-and-hiding}

The HUD's **shown** flag is `+0x177a0`. Nothing is saved when it is hidden: each part keeps its own flags, and
`RestoreHud` shows a part only if those still allow it. The HUD's per-level set-up (`0x001ad588`) sets `+0x177a4`
(the HUD is active), `+0x177ac` (the radars come back on their own, below) and ends with `HideHud`, so **a level
starts with the HUD hidden** until something below shows it. Confirmed (code).

| Part | `HideHud` (`0x001b1f38`) | `RestoreHud` (`0x001b20f8`) |
| --- | --- | --- |
| each player panel | hidden (`+0x40fc` = 0), when attached | shown (`0x0020e028`) only if attached and its "may show" flag `+0x4108` is set |
| the widget at `+0x15b0` | `+0x08` = 1 | `+0x08` = 0 |
| each radar (`+0x15d0`, `+0x3f10`) | blips off, "hidden by the HUD" `+0x0c` = 1 | only a radar that is **on** (`+0x04`, set by `HUDTurnOnRadar`): blips on, `+0x0c` = 0, `+0x08` = 1; a radar that is off stays off |
| the score board `+0x8960` | hidden, `+0xac` = 1 | `+0xac` = 0; shown only when enabled (`+0xa8`) and both `+0xa0` and `+0xa4` are set |
| both radar frames (`+0x177d0`, `+0x18280`) | their parts off | their parts on (when set up, `+0xa98`) |
| hint box, objectives and scroll-in messages, announcements, counter panels, bars, arrow, prompts | untouched | untouched |

The parts in the last row are simply not drawn while `+0x177a0` is 0 ([The HUD's frame](#the-huds-frame)); a hint
or objective queued while hidden (`HUDSetTutorialText`, `HUDSetObjective`) is kept, and `HUD_Update` keeps stepping
it while no letterbox is up. `HideHud` does nothing in an Armies of the Night level whose game state `+0x14c` is 1.
Confirmed (code).

- **`ShowHud`** does nothing: its function (`0x001b3ec8`) is a bare return
  ([binding](../references/bindings/hud.md#showhud)); `level99`'s `Main` calls `ShowHud(0)` then `RestoreHud()`
  ([Scripts](scripting.md#level99)). Confirmed (code).
- **`HidePlayerHud` / `ShowPlayerHud`** clear or set each panel's "may show" flag `+0x4108` and hide or show it.
  Confirmed (code).

#### Who shows the HUD again {#who-shows-the-hud-again}

Besides a script's `RestoreHud`, three engine paths call it. Confirmed (code) unless marked.

1. **The letterbox going out** (`0x0018d910`, the letterbox step of each view's screen effects, run in the overlay
   pass). Starting a letterbox move (in or out, state 1) sets the record's `+0x1f4` to 0; when the bars reach level
   0 the time is stamped there; the next step that finds the bars out (state 0, level 0) with `+0x1f4` not −1 shows
   both player panels (`0x001b2330`), calls `RestoreHud`, and sets `+0x1f4` = −1. So **every cinematic scene shows
   the HUD as its bars finish going out**, 1.5 s after its end ([Scenes: ending](scenes.md#ending)), whatever hid it
   (`SuperRunScene`'s `HideHud`, the scene start's own `HideHud`); a script's `ScreenQueueEffect` type 2 or 3 does
   the same. A skipped scene ends the same way ([Scenes: skipping](scenes.md#skipping)). Bars that only go in
   restore nothing until they go out, and a scene played without bars (`Bars` = false) leaves `SuperRunScene`'s
   `HideHud` in force until a script's `RestoreHud`. This is how `level99`'s HUD returns after
   `l99_c1`: `P1.StartTraining` calls no `RestoreHud`. Confirmed (runtime) on a copy of slot 1: with `+0x177a0` and
   the panel's shown flag cleared by PINE writes and a letterbox put in and out by writing the screen-effects record
   (1.5 s each way), both stayed 0 until `+0x1f4` was stamped as the bars reached 0, and on the next frame the panel
   showed, `+0x177a0` became 1 and `+0x1f4` −1. A real scene's end was not watched.
2. **Play resuming** (mode 1 `Resume`, `0x00158580`, when a mode pushed over play is popped, such as the pause menu,
   mode 0xa): `RestoreHud` when the HUD is active (`+0x177a4`) and hidden, game state `+0x14c` is 0, and player 1 is
   not in a scene (`Human_IsInSceneState`, `0x00227d28`: human `+0x280` ≠ −1 or human flag `0x800000`). Its
   `Suspend` (`0x00158660`) calls `HideHud` unless `0x005e5580` is set (not traced) or in an Armies of the Night
   level. So pausing hides the HUD under the menu, and **closing the menu shows it even if a script had hidden it**,
   unless player 1 is in a scene.
3. **After wasted or busted.** The death camera's start (`0x0011daa8`, and the same steps in mode 1 `Update`,
   `0x00158728`) keeps `+0x177a0` in player 1's camera record (`+0x1fc`) and calls `HideHud`; mode 0xc's handler
   (`0x00155408`) calls `RestoreHud` if it was shown, when player 1's camera is still that type-0xc camera.

The other hides: a cinematic scene's start calls `HideHud` (and hides the panels, `0x001b2380`) when the global scene
state `0x0051489c + 0x410` is 0 ([Scenes: starting](scenes.md#starting), step 4, at `0x0039da90`); `HUD_Render` draws
nothing while a letterbox is up ([The HUD's frame](#the-huds-frame)). The Armies of the Night end screen
(`HUD_ANLaunchEndScreen`) hides and later restores it. Confirmed (code).

#### The radars across a scene {#radars-across-a-scene}

While a letterbox is in or moving, `HUD_Render` and `HUD_Update` turn **both radars off** every frame
(`0x001b2658`, which clears each radar's on flag `+0x04` and `+0x177b0`), so the `RestoreHud` at the end finds them
off and leaves them off. `HUD_Update` brings them back on its own: when the letterbox and the fade (screen effects
`+0x1d4`, `+0x1d8`) are both at 0, `+0x177ac` is set and `+0x177b0` is 0, it turns both on (`0x001b2610`); while a
fade runs it turns them off. `+0x177ac` is set by the level's set-up and by `HUDTurnOnRadar` (`0x001b4328`, for one
player or both) and cleared by `HUDTurnOffRadar` (`0x001b43a8`). So after a scene the radars return only if the last
radar call was "on": `level99`'s `P1.SetupCombat` calls `HUDTurnOffRadar` before `l99_c1`, so its radars stay off
after the intro. Confirmed (code); confirmed (runtime) with the letterbox writes above: with `+0x177ac` 0 radar 0
stayed off after the bars went out, with it 1 both radars came on in the frame `+0x177a0` became 1.

### What `level99` uses

The first mission's scripts call, among the HUD bindings: `HUDSetObjective`, `HUDRemoveAllGoalText`,
`HUDSetTutorialText`, `HUDFlushTutorialText`, `HUDSetTutorialCallback`, `HUDEnableGameTutorialText`,
`HUDEnableInstArrow`, `FlashRageBar`, `ForceShowPlayerHud`, `HUDTurnOnRadar`, `HUDTurnOffRadar`, the radar objective and
item calls, `HUDGetNewPH` / `HUDSetPHValue` / `HUDReleasePH`, `HUDSetAnnounceMsg`, `ShowHud`, `HideHud` and `RestoreHud`
(the `mission1` flags of the [HUD bindings](../references/bindings/hud.md)). It does not end on a HUD screen: the
mission ends through `HUDLaunchMissionComplete` and mode 0xb ([Front end](frontend.md#story-start),
[Scripts](scripting.md#run-next-mission)), which draws no screen of its own.

## Coney's implementation

`src/hud/` is the HUD, platform-neutral, stepped on the fixed 1/30 s step with the game time in ms; `src/platform/hud_layer.h`
loads its sheets and draws it through the [sprite batches and the 2D pass](gui.md#coneys-implementation).

- **`hud::Hud`** (`0x001acee0`): the panels, the hint box, the checklist and its scroll-in queue, the announcement,
  the counter panels, the arrow and the radars' state. `update` (`HUD_Update`) steps them; `render` (`HUD_Render`)
  draws in the [render order](#the-huds-frame) with [what hides what](#the-huds-frame), nothing while hidden. A mode
  that stops stepping it (mode 0xb) draws it as it was left. [Showing and hiding](#showing-and-hiding) as the page
  says: `levelSetUp` (before the level's script, so a level starts hidden), `hideAll` / `showAll` keeping each part's
  own flags, and `update` running the letterbox's restore mark (armed by the bars, stamped on the first step they are
  out, `RestoreHud` on the next) and the [radars across a scene](#radars-across-a-scene); under the bars nothing else
  steps. **Coney's placement**: the restore mark is the view's (`0x0018d910`), run in `HUD_Update` from
  `HudFrame::letterbox`. Play's suspend and resume (`GameplayMode`) hide the HUD under a mode pushed over play and
  show it again unless player 1 holds a scene role; `0x005e5580` and game state `+0x14c` are not checked.
- **`hud::PlayerPanel`**: the [layout](#the-player-panel-layout-0x0050fa10), the banner by type, the
  [meter](#the-rage-meter) (caps and strips of `part_page0` 54-57, one-texel inset, the full meter's pulse and sound,
  `FlashRageBar`), the [score and money](#score-and-money) counting by a sixteenth plus one with their popups,
  the [counters and their slots](#item-counters), and the 2 s hold and 1 s fade. Activity: a changed value, SELECT, the
  force-show flag. The parts land within a pixel of the page's 640 × 448 figures (`tests/hud/player_panel_test.cpp`).
- **`hud::HintBox`**: the [hint queue](#hints-hudsettutorialtext) of 20 sorted by priority, interrupt and restart, a
  hint's time only from its `<DISPLAYTIME>` (else until flushed), the box laid out from the
  [table](#hint-box-layout-0x0050eb50) and following the text's fade.
- **`ScrollInQueue`**, **`CounterPanels`**, the announcements and the action prompt: the objectives' messages at
  (0.025, 0.88); the five panels; the bottom-left and centred [announcements](#announcements-and-other-messages),
  each replaced at once and timed by its markup; player 0's [prompt](#action-prompts) at (0.5, 0.86) raised over the
  hint box or message. Nothing draws while `HudFrame::letterbox` is set.
- **The bindings** (`src/scripting/hud_bindings.h`): every HUD binding `level99` calls, through `BindingContext::hud`.
  `HUDSetTutorialCallback` keeps the name; the play mode calls that function after the level's step with the anim
  id of each hit player 1 struck in it, landed or blocked (`Fighter::strikes`). **Coney's choice**: called after the
  step, not from inside the damage step.
- **Sound**: the cues by the table `SoundCfgInterfaceSound` fills, and the named sounds, play through
  `audio::SoundPlayer` ([Sound](sound.md#interface-sounds)); silent without sound.
- **In play**: the play mode steps the HUD with player 1's rage and draws it over the frame; the story shares the
  flow's HUD with the scripts. A disc test (`[disc][hud]`, `repo:tests/platform/disc_level99_hud_test.cpp`) plays
  `level99` checkpoint 1 through `l99_c1` unskipped: hidden and nothing drawn under the bars, then shown, drawn and
  the first hint up, and a pause hiding and showing it. The debug menus' HUD page sets its values ([Debug menu](../guides/debug-menu.md#pages)).

**The health rings** (`repo:src/hud/health_rings.h`, drawn by `repo:src/platform/play_level_world.cpp`), as
[the health rings](#the-health-rings) say: each step after the HUD's, `hud::HealthRings` takes player 1 and his target
and queues the rings (nothing while the HUD is hidden: `HideHud`, a scene's start); the show list (SELECT newly
pressed, a fight stance or health at or below 20 % renewing it; 4 s hold, 0.5 s fades), `HuForceEnableReticule`
(`GameState::forceReticules`) at alpha 255, the target's outer ring fading in over 500 ms and the L1 marker under a
non-player target; the arcs from health, power or raw rage × the power class byte (`PowerClass::ringByte`), the
colours, the blink at or below 25 %, the rage-full flashes and the raging grey, the hit pulse (damage this step, 45 an
update, at most × 1.15), the start angle from the camera's heading. They are drawn after the blob shadows, each ring
under where its human is drawn this frame, as 64 flat-coloured triangles of `part_page1` rectangle 1 with fog off. A
disc test (`[disc][objects]`, `repo:tests/platform/disc_level99_world_test.cpp`) checks that SELECT brings up player
1's two rings after `l99_c1`.

**Coney's stand-ins for the rings** (marked in the code): the blend state the world pass leaves is taken as alpha
blending with Z test and no Z write; flat shading gives a triangle its last vertex's colour; the blink runs on the game
clock, black first; the fight stance is a lock-on or a block; a hit's pulse is the health lost in the step; the
civilian's class byte is Rembrandt's 35; the camera's heading is that of its forward. Not built: the flash's trigger
(no flash item yet), a boss's three bands, the Rumble team disc and pointer, two players' rings and icons.

**Coney's stand-ins** (marked in the code): text sizes read as the glyph height (`(0.04, 0.05)` as w × h, 0.05 as h);
the counter slots' `x0` 0 and lines at y 0.104 and 0.154, the count 0.022 right of its icon; handcuff and key icons
`part_page0` 31 and 34; the money's icon a `$`; the popups' places; the money cue once per count; the built-in
announcements' texts from `GSTRING.ANNOUNCE` by kind; a `<FREEZE>` hint shown for its time (at least 2 s) as the game
timer does not freeze yet; the prompt's text set by the game code or the debug menu, not chosen by `HUD_Update`;
the counter panels' texts right-aligned on x 0.96; the radar disc
`big_font` 256 sized from the measurement, a dark disc for the map, the player's icon 362 at the centre and no other
blips drawn; the arrow turned only by half turns (no rotated sprites yet); player 1's other parts 0.09 right of player
0's; a HUD no level has set up (the debug pages, the tests) shown at start; no wasted/busted restore yet (Coney has
no death camera). The hub's HUD bindings (`repo:src/scripting/hub_world_bindings.cpp`):
`HUDEnableClubActionText` raises the prompt to y 0.125 (the other video modes' heights are not used); the action-cycle
animation (`HUDTurnOnActionCycleAnim`) is kept per player but not drawn yet (the prompt does not swap its icon);
`HUDShowMissionSelect` and `ShowGameStatsInterface` reach the front end, which has neither screen yet.

## Open questions

- Which character each name-banner sheet (records `0x1f`-`0x32`) names, beyond Rembrandt (`0x2f`).
- The handcuff and key counters' icon rectangles, and the counters' exact text offsets.
- The action prompts' text alignment and icon sprite; what `0x00225ff0`, `0x00279078` and the many "free to act"
  checks of `0x001af010` test; a runtime look at a shown prompt.
- Which event raises each of the game's own hints (the 19 callers of `HintBox_QueueGameHint`).
- What the input object at HUD `+0x18e60` is, whose message 1 shows the next caption (`0x001cb340`).
- Who sets HUD `+0x177a8` (the centred announcement while hidden).
- The radar: which file holds each world's map sheet; the active camera's slot `+0xc4` third value (inferred: the
  overlay width); what the player stands on for the blue disc colour.
- The health rings: the shape of `part_page1` rectangle 1 (the ring's band), what the power class byte `+0x40` is
  meant as, the blend state the world pass leaves, and a runtime look at a target's ring, the rage-full flashes and a
  boss's bands.
- The target panel's two sprites and the caller `0x00210e48`'s conditions; a runtime look at the panel.
- Human state flag `0x200000`, which turns the banner blue-grey.
- The layouts of the other video modes (16:9, progressive, PAL) that `0x00211ef8`, `0x001af010` and `0x001cdc80` apply
  (the radar's are [above](#the-radar-on-screen)).
- The values at `0x00510040`/`0x00510044` (slot `x0`) and `0x00510048`/`0x0051004c` (the counter lines), and the
  counters' text offset.
- The money's icon (rectangle and sheet) and where the score's and the money's popups sit.
- The size arguments of the HUD's texts: a glyph's height, or `(w, h)`, or a `Font_Size` scale.
- Whether the built-in announcements (`0x00622e20`) are `GSTRING.ANNOUNCE`.
- Whether the money's cue `0x10` plays once or every counting frame.
- The counter panels' text layout and alignment, and a bar panel's look.
- Player 1's score, money and counter offsets.
