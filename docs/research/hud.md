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

`GUI/HUDInterface.cpp` (`0x001ad588` to its stub `0x001b3ea8`), `GUI/HUDLua.cpp` after it (the bindings'
callees, to `0x001b8f78`), `GUI/TutorialHUD.cpp` (the hint box), `GUI/ScrollInHUD.cpp` (queued messages),
`GUI/RadarHUD.cpp`, `GUI/ChecklistMessageHUD.cpp`; the player panel class lives at `0x00211ca0`-`0x00214c28`, after
the profile-manager screens, in a file not yet named ([Source map](source-map.md#gui)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00600840` | the HUD object | one static object, about `0x22600` bytes | confirmed (code) |
| `0x001acee0` | `HUD::HUD` | constructor: every part below; `+0x08`/`+0x0c` point at the two player panels | confirmed (code) |
| `0x001af010` | `HUD_Update(hud, a, b)` | per frame from mode 1 (`a` and `b` go to the two radars' updates) ([Level loading](level-loading.md)) | confirmed (code) |
| `0x001b1688` | `HUD_Render(hud)` | from the overlay pass `0x00156658` ([Boot](boot.md)) | confirmed (code) |
| `0x001b1f38` / `0x001b20f8` | `HUD_Hide` / `HUD_Restore` | `HideHud` / `RestoreHud` | confirmed (code) |
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

### The HUD object {#hud-object}

One static object at `0x00600840` (`0x22600` bytes), built by `HUD::HUD` (`0x001acee0`) at start-up and set up per
level by `HUD_InitLevel` (`0x001ad588`); parts set up there only when not already set up. Offsets from the object;
confirmed (code) at `0x001ad588`, `0x001ae980`, `0x001af010` and `0x001b1688` unless marked.

| Offset | Part | Set up / update / draw |
| --- | --- | --- |
| `+0x0000`, `+0x0004` | the two players' input records (`VirtualPad`, `0x2c` bytes; [Pads](#hud-pads)) | `0x001ae4d8` |
| `+0x0008`, `+0x000c` | the two player panels (`PlayerHUD`, static at `+0x19130` + slot × `0x4a50`; an allocated `ANHud` in levels 60-69) | interface at panel `+0x4120` |
| `+0x0010` | a per-human widget fed by `HUD_TagAddHuman` / `_TagRemoveHuman` (role not traced) | `0x0019fbb0` / `0x0019fda0` (levels below 100) / `0x001a03a8` |
| `+0x15b0` | a small widget hidden by `HideHud` (`+0x08`) | `0x001a8ed8` / `0x001a8f90` |
| `+0x15d0`, `+0x3f10` | the two radars (`0x2940` bytes each); positions `+0x3f00`, `+0x6840` | `Radar_Setup` / `0x001c5210` / `Radar_Render` |
| `+0x6850` | the stopwatch, at (0.5, 0.135) | `0x001cd578` / `StopWatchHud_Update*` / `0x001bb1c0` |
| `+0x6b40`, `+0x70d0` | the [action prompts](#action-prompts) | `ActionPrompt_Setup` |
| `+0x7660` | a widget (role not traced) | `0x001bb650` / `0x001bbbb0` / `0x001bb8a8` |
| `+0x8960` | the score board | `0x001c2638` |
| `+0x8a10` | the hint box | `0x001cd988` / `HintBox_Update` |
| `+0x8dd0` | the scroll-in queue, at (0.025, 0.6), colour (206, 206, 206, 206) | `0x001c8880` / `ScrollIn_Update` |
| `+0x9350` | a markup text (role not traced) | `0x001acc60` / `0x001b9548` / `0x001bb1c0` |
| `+0x9540` | a per-player widget drawn for humans in state `0x002238c0` | `0x001cb6f0` / `0x001cb798` |
| `+0x9640` | a widget active while `+0x964c` | `0x001a4348` / `0x001a43d8` |
| `+0x9970` | the four generic bars of `HUDEnableBar` kinds 0, 1 and 3 (`0x3f0` bytes each, `0x0060a1b0`) | [Scripted bars](#scripted-bars) |
| `+0xa930` | how many generic bars are shown (split between the two columns in split screen) | |
| `+0xa940` | the kind-2 gauge (`0x0060b180`), active while `+0xa94c` | `0x001a3720` / `0x001a3990` / `0x001a3c00` |
| `+0xacb0` | the five counter panels (`HUDGetNewPH`), at (0.9, 0.4); `+0x3394` the stack's height | `0x001c2e38` / `0x001c3608` / `0x001c37f8` |
| `+0xe050` | the [spinner](#hud-spinner) | `0x001ae378` |
| `+0xe150` | the centred announcement (markup text, colour `0x6060bfff`) | [Announcements](#announcements-and-other-messages) |
| `+0xe340` | the announcement (markup text, white) | `HUD_ShowAnnouncement` |
| `+0xe530` | the RM_Intro screen | `HUD_OpenRumbleIntro` |
| `+0xe650` | an Armies of the Night widget (AotN levels only) | `0x0019e030` / `0x0019e758` / `0x0019ed28` |
| `+0xea30` | the credits | `Credits_Load` |
| `+0xebc0` + p × `0x430` | the mash meters | [Crimes](crimes.md) |
| `+0xf420` + p × `0x540` | the lock-picking dials | `0x001b7eb0` (size 0.158) |
| `+0xfea0` + p × `0xb10` | the stereo-theft widgets | |
| `+0x114c0` + p × `0x450` | a per-player widget (role not traced) | `0x001ab108` |
| `+0x11d60` | a widget (role not traced) | `0x001b75f0` / `0x001b7608` / `0x001b7890` |
| `+0x134a0` | the [instruction arrow](#the-instruction-arrow-hudenableinstarrow) | |
| `+0x135e0`, `+0x136e0` | the two [fixed-camera icons](#hud-fixed-cam-icon) | |
| `+0x137e0` | 19 sprites of the [split-screen divider](#hud-divider) (`0x100` bytes each) | |
| `+0x14ae0` | the six text-progress rows (`0x560` bytes each), shown while `+0x16b20` | `HUDEnableTextProgress` |
| `+0x16b30` | the Rumble widget (levels 100 and up), at (0.125, 0.7), sprite word `0x20e0000` | `0x001b6358` |
| `+0x177a0` | shown (`RestoreHud` 1, `HideHud` 0) | |
| `+0x177a4` | active (set by `HUD_InitLevel`, cleared by `HUD_ShutdownLevel`) | |
| `+0x177a8` | draw the centred announcement while hidden | |
| `+0x177ac`, `+0x177b0` | radars wanted on (`HUDTurnOnRadar`); radars turned on | |
| `+0x177b4` | fixed-camera icons enabled (written, never read: [the icon](#hud-fixed-cam-icon)) | |
| `+0x177b8` | a radar item removed by `HUD_RadarClearPending` | |
| `+0x177bc` | per player, the action object's hint id queued in the hint box | |
| `+0x177d0`, `+0x18280` | the two radar frames (`0xab0` bytes each) | `0x001aa290` / `0x001aa9b0` / `0x001aaba8` |
| `+0x18d30` | the captions ([Scenes](scenes.md#subtitles)) | `Captions_Init` |
| `+0x18e60` | the caption pager ([GUI](gui.md#fn-subtitle)) | `0x001cb398` / `0x001cb418` |
| `+0x18ee0` | the Armies of the Night credits text (colour `0xff00b8f5`), value `+0x190d0`, text buffer `+0x190d4` | |
| `+0x1911c` | the Armies of the Night boss picture (a sprite widget) | |
| `+0x19124` | a scale per video mode (1.0 default) | |
| `+0x19128` | the spinner's rate | |
| `+0x225d0` | per player, the reticule request byte | `HUD_SetReticuleRequest` |
| `+0x225d2` | a copy of game state `+0x33a` | |
| `+0x225d4` | radar 2 started | |
| `+0x225d5` | an Armies of the Night level (60-69) | |
| `+0x225d8` | the divider needs drawing | |

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

**Flash and trail, unused.** `HudBar_Draw(flashBelow, bar, flash, trail)` takes the threshold in `f12` and the bar,
`flash` and `trail` in `a0`-`a2`. Every one of its 14 calls passes **`flash` = 0 and `trail` = 0** (`0x001a33a4`,
`0x001c0aac`, `0x001c0ac0`, `0x001c2504`, `0x001d737c`, `0x001d7390`, `0x001d73e4`, `0x001d7400`, `0x00209034`,
`0x002119dc`, `0x00211a18`, `0x00211a44`, `0x002135d0`, `0x0021360c`), so `flashBelow` (0.25 at most sites, 0 for the
mash meter) changes nothing and **no bar flashes or trails** through this path. Confirmed (code). The rage meter's
gold pulse below is the panel's own colour switch, not this flash. For reference, the dead path: with `flash` set and
the fill ≤ `flashBelow`, a byte at `+0x65` drops by 16 each draw and jumps back to 255 when it falls under 32 (255,
239, ..., 47: a 14-draw cycle), and the **background parts only** (the two caps and the body, not the capacity strip
or the fill) get that byte ÷ 2 ORed into their colour's **red** byte. The trail (`+0x44`-`+0x4c`) would draw the fill
rectangle between the old and new fill ends for 64 draws after a drop, at alpha (count & 3) × 85 (a 4-draw flicker).
Confirmed (code) at `0x001a1138`.

**Fill colour:** the rage colour for raging (human `+0xe0` flag `0x80000`) or not. **A full meter** (rage ≥ maximum)
plays `vags/interface/rage_indicator_02` once and flashes: with `t` = game time mod 400 ms, `f` = |200 − t| / 200 (a
triangle wave), the fill is gold while `f³ < 0.2` (`0x005100c8`) and red otherwise, so a short gold pulse every
400 ms. Confirmed (code) at `0x00214138`, period `0x005100cc` = 200.

**`FlashRageBar(player, n)`** (`0x001b3f58`, panel `+0x4114`): with `n` > 0 the whole meter is drawn for `n` frames
and skipped for `n` frames, repeating; `n` = 0 draws it every frame. Confirmed (code) at `0x00213290`. The tutorial
passes 5: on and off every 5 frames (1/6 s at 30 frames a second).

### The Warrior command menu {#warrior-command-menu}

A war chief (human `+0x3ac` = 1) orders his crew from a six-slot menu held open with **R2** and steered with the
**right stick**; releasing R2 gives the highlighted order. Each player panel owns one (`WarCommandDisplay`, panel
`+0x1ef0`, built by `WarCommandDisplay_Init`, `0x001a62c0`); the commands themselves are on
[AI: Warrior commands](ai.md#warrior-commands). Confirmed (code) at the addresses given unless marked; not seen at
runtime.

**Input** (`Player_UseItemCommand`, `0x002843f8`; pad commands from [Combat](combat.md#commands): 1 is R2 held, 2 R2
released). Nothing happens while the human has a state flag of `0x80040000`, or `0x20000` (that branch only handles
triangle), or has no player index (human `+0x1b0` = -1), or while that player's menu is locked (game state `+0x42e` +
player). Release is tested before hold:

- **R2 held** (every frame it is down): `WarCommandDisplay_Open` (`0x001a6d28`). When the display is allowed
  (`+0x155c`: `WCEnableCommand`, `HUDShowWarCommand`) and the human is a war chief, it is shown (`+0x1554` = 1), the
  "issued" flag (`+0x1548`) and the after-issue counter are cleared, the eighteen sprites are shown with any fade
  cancelled, and **the camera's right stick is switched off** for that pad (`0x0050b1b0[pad]` = 0, which
  [Camera](camera.md#right-stick) tests). It does **not pause or slow the game**: neither function touches the game
  timer, a time scale or the game mode, and the handler returns 0, so the rest of the frame's input (the left stick,
  attacks) still runs. The selection (`+0x1540`) is **not** reset: the menu opens on the last highlighted slot (slot
  0 the first time). For a human who is not a war chief the display is hidden instead.
- **R2 released**: when the display is set up (panel `+0x1efc`), `WarCommandDisplay_Issue` (`0x001a6c58`).

**Picking a slot** (`WarCommandDisplay_ReadStick`, `0x001a7040`, from `WarCommandDisplay_Update` each frame while
the menu is shown, not yet issued and not locked) reads the right stick's raw bytes (pad record `+0x1a` x, `+0x1b` y,
0-255, y down; [Front end](frontend.md#pad-record)):

- **Dead zone:** nothing unless (x − 127)² + (y − 127)² > 12,100, a radius of 110 of 127: **about 87 % deflection**.
  With a stick value v in [-1, 1] and raw ≈ 127.5 + 127.5 v, a push of 0.9 picks and 0.8 does not. Inside it the
  highlight stays where it was.
- **Angle**, clockwise from up (up 0°, right 90°, down 180°, left 270°): a quadrant base (0°, 90°, 180°, 270°) plus
  asin(|d| / 128) of **one** axis's offset d from 128, not atan2, so it is exact only at full deflection. d is the x
  offset in the up-right and down-left quadrants and the y offset in the down-right and up-left ones; an offset of
  128 or more counts as 90°. A 45° push of magnitude 0.9 (offsets about 81 and 81) reads 39° up-right.
- **Sectors** (without hysteresis):

| Slot | Stick | Angle | Command |
| --- | --- | --- | --- |
| 0 | up | 337.5°-22.5° | 0 follow |
| 1 | up-right | 22.5°-87.75° | 2 defend |
| 2 | down-right | 92.25°-157.5° | 4 scatter |
| 3 | down | 157.5°-202.5° | 3 hold |
| 4 | down-left | 202.5°-267.75° | 5 wreck (`steal` / `vandal`) |
| 5 | up-left | 272.25°-337.5° | 1 attack |

- Straight left or right (within 2.25°, `0x0050d2b4` = π/80) selects nothing new. The slot-to-command map is the
  jump table at `0x00553fe0` (`WarCommand_FromSlot`, `0x001a8530`; 7 for any other slot); command 6 has no slot. The
  command names follow [AI](ai.md#warrior-commands) (read from the lines, inferred).
- **Hysteresis:** the highlighted sector reaches π/16 = 11.25° (`0x0050d2b8`) further into each neighbour until the
  highlight moves, so a stick resting on a boundary does not flicker.
- **Disabled commands are not skipped**: the stick can highlight them; the text then says so (below) and issuing one
  does nothing.
- **Sound:** interface cue `0x20` each time the highlight changes. The stick is read every frame (the interval
  `0x0050d280` is 0, against the timer at `0x0050b8b8`).

**Issuing** (`WarCommandDisplay_Issue`, once per opening, only while shown and not yet issued): the highlighted slot is
kept (`+0x1544`), its command stored as the player's last (game state `+0x41c` + player) and given through
**`0x0041c4e0(gameState, chief, command, forced 0, pos 0, arg 0)`** (chief = display `+0x156c`); `0x0051480c` is set
to `0xff`. A disabled command has its text cleared first and the dispatcher then refuses it
([AI](ai.md#warrior-commands), step 1), though the last-command byte has already been written. **Only R2's release
issues**: there is no flick or timeout, and releasing without touching the stick issues the highlighted slot (the
previous choice). An open menu also issues when the pause menu closes (`PauseMenu_Close`, `0x001dcb20`) and on any
frame the HUD is not drawn (`HUD_Render`'s hidden branch, `0x001b1688`). The chief's spoken line comes from the
dispatcher ([Speech](../references/speech.md)).

**After issuing** (`WarCommandDisplay_Update`, `0x001a7e48`): for 10 updates (`0x0050d300`) the menu stays as it is,
then the camera's right stick is switched back on (`0x0050b1b0[pad]` = 1) and the sprites fade out: the chosen slot's
plate over 1,500 ms (`0x0050d310`), everything else over 500 ms (`0x0050d30c`); the chosen slot blinks, hidden 2 of
every 4 updates (`0x0050d24c` = 2, `WarCommandDisplay_BlinkSlot`, `0x001a7d10`). The display closes (`Shutdown`)
when its text has expired (`MarkupText_IsExpired`: the text's `<DISPLAYTIME>`, inferred). It closes at once if the
chief goes down (`0x00227dd8`), and a lock that arrives while it is open ends it as if issued, with no command.

**Layout** (default video mode, GUI coordinates). The centre x is the layout record's (`0x006006a0` + player ×
`0x80`): 0.5, set by `Open`; in a level numbered 100 or more with two players (`0x001fe198` = 1), 0.23 for player 1
and 0.76 for player 2. Each slot is three sprites at one point, back to front: a black backing (sprite word `0xd0100`
of instance 6, size 0.077), a plate (the same rectangle, size 0.07) and the icon (`part_page0`, instance 3, size
0.08):

| Slot | Position | Icon rectangle |
| --- | --- | --- |
| 0 | (x, 0.795) | `0x56` |
| 1 | (x + 0.069, 0.795) | `0x5a` |
| 2 | (x + 0.071, 0.93) | `0x58` |
| 3 | (x, 0.93) | `0x55` |
| 4 | (x − 0.071, 0.93) | `0x59` |
| 5 | (x − 0.069, 0.795) | `0x5b` |

So two rows of three, about 0.07 apart, the top row at y 0.795 and the bottom at 0.93 (`WarCommandDisplay_Place`,
`0x001a77b0`, from base y 0.99 `0x0050d20c` and the offsets `0x0050d214`-`0x0050d23c`). Between them, at (x, 0.86)
(`0x0050d2fc`), the **name** of the highlighted slot: a markup text (font slot 3, colour (191, 191, 191), the
record's `+0x10`) over a box sprite (rectangle `0x4e`, colour (0, 0, 0, 143)) sized to the text plus (0.013, the
record's `+0x64`). The text is entry *slot* of the command-text table (`0x006007c0`, filled by `CfgWarriorCommand`
from [`GSTRING.COMMAND`](../references/text-labels.md#text-gstring-command); indexed by display slot, not command
id); entry 6 (`0x006007d8`) replaces it while all the player's commands are locked (game state `+0x414` + player) and
entry 7 (`0x006007dc`) while the highlighted command is disabled. **Colours** (`WarCommandDisplay_Render`,
`0x001a8590`, values from `0x001a8b48`): the highlighted slot's icon white, plate gold (255, 183, 0), backing grey;
the others' icon white, plate dark (35, 35, 35), backing black; a disabled command's icon (30, 30, 30). The 16:9
and other modes overwrite the offsets and sizes (`0x001a7e48`); they are not listed here.

**No d-pad path:** the player's command handler gives the menu nothing on the d-pad (d-pad right is the flash);
scripts order the crew with `WCIssueCommand`.

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
position is an offset from the panel's base; x is relative to `x0` = `0x00510040` (player 0, **0.0**) or `0x00510044`
(player 1, **0.089**), and the slots move right as the money's digits grow. Line 1 is y `0x00510048` (**0.104**), line
2 y `0x0051004c` (**0.146**); values read from the executable. The slots live in a static record per panel layout,
`0x0050fec0` + layout × `0xc0`: four slots of `0x30` bytes, each x `+0x00`, y `+0x08` and the item it holds `+0x20`
(0-3 in the order above, **4 = free**; every update first frees all four). Confirmed (code) at `0x00213770`:

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
This holds for `PlayerHUD`; the Armies of the Night panel draws a power bar and a health
bar ([`ANHud`](#fn-anhud)).
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
- **Counter panels** (`HUDGetNewPH`, five at HUD `+0xacb0`): laid out each frame by `0x001c3608` from its own
  floats (`0x0050e97c`-`0x0050e994`), filled from the top with the visible panels; the height they take is kept at
  HUD `+0xe044`, and the [scripted bars](#scripted-bars) go below it. Confirmed (code) at `0x001c3608`, `0x001af010`.

### Scripted bars (`HUDEnableBar`) {#scripted-bars}

`HUDEnableBar(kind, on, labels, count, flag, texA, texB)` (`HUD_EnableBar`, `0x001b53b0`) makes one of four kinds
of bar; `HUDSetBarPercentage` (`0x001b5450`) fills it and `HUDSetBarProperty` (`0x001b54f0`) recolours it. The
binding's arguments are in [HUDEnableBar](../references/bindings/hud.md#hudenablebar). Kinds 0, 1 and 3 use the
**generic bars**: four slots of `0x3f0` bytes at HUD `+0x9970` (`0x0060a1b0`), each a `HudBar` meter (the class of
the [rage meter](#the-rage-meter)) plus a markup-text label; kind 2 is a separate **chase gauge** at HUD `+0xa940`
(`0x0060b180`). Confirmed (code) at the addresses given. The positions below are GUI coordinates
([Coordinates](#coordinates)) in the default video mode (device flag `0x01` only); sizes of the generic bars are
GUI units, sizes of the gauge's sprites overlay units ([GUI](gui.md#widget-classes), `BaseWidget`).

**Kind 1: the labelled bar** (`HUD_EnableLabelledBar(on, labels[0], slot 0, rightColumn 1)`, `0x001b4c98`), used for
a boss's or an object's health. On creates generic bar slot 0 once (a second call while it exists does nothing); off
calls its `Shutdown` (`0x001c2228`).

- **Size:** 0.22 × 0.025 times the HUD scale (HUD `+0x19124`, `0x00619964`: 1.0, or 0.78 when the device flags have
  `0x02` without `0x04`; reset to 1.0 at the start of every `HUD_Update`, `0x001af010`), so 126 × 10 pixels on the
  640 × 448 screen.
- **Where:** `HUD_Update` re-lays every visible generic bar each frame in a column at the right: slot `i`'s **bar
  has its right end at x 1.0** (`0x0050d514`) and is centred on y 0.26 + 0.08 × `i` (`0x0050d518`, `0x0050d520`),
  drawn right to left (HudBar `+0x5c` = 1, so the fill grows leftward from the right end); its **label is
  right-aligned at x 1.0, 0.035 above the bar** (anchor x 0.96, `0x0050d510`, plus the label offset (0.04, -0.035)
  at bar `+0x3d0`/`+0x3d8`, `0x0050e960`/`0x0050e964`). The whole column moves down by the height of the visible
  [counter panels](#announcements-and-other-messages) (HUD `+0xe044`, written by `0x001c3608`) and by 0.07 while the
  stopwatch shows (HUD `+0x6858`); each hidden slot below moves the later ones up by 0.07. So slot 0 alone sits at
  pixels x 481-607, y 126, its label's right edge at x 607. In a two-player split (two players and two camera
  views, `0x001b1640`) the second half of the bars goes to a left column (label left-aligned at x -0.055, bar left to
  right from x -0.015). Other video modes use other column values, written into the same globals by `0x001af010`.
  Confirmed (code) at `0x001af010`, `0x001c22e8`, `0x001c1f80`.
- **Look:** the four rectangles of the rage meter: sprite word `0x30036`, batch 3 (`part_page0`) rectangles 54
  (body), 55 (fill), 56 (left cap) and 57 (right cap), drawn as on [The rage meter](#the-rage-meter) without the
  capacity strip (capacity 0) and without flashing (`HudBar_Draw(0.25, bar, 0, 0)` from `0x001c2470`). Background
  **(64, 64, 64, 255)**. The **fill colour follows the fill**: `HudGenericBar_SetGradient` (`0x001c2410`) sets HudBar
  `+0x60`, and each draw sets the fill colour to red **(255, 16, 16)** × (1 − fill) + green **(115, 183, 11)** × fill
  (`0x0017a258`, per channel), so a full bar is green, a half one (185, 100, 13), an empty one red. It starts
  **full** (fill 1.0).
- **Label:** a markup text widget (bar `+0x1b0`) with the string `labels[0]`, size 1.0 (0.9 in the `0x02`-without-
  `0x04` mode), font slot 3, colour `0x005fd310` (178, 178, 178, 255), right-aligned (`0x001c2290(bar, 1)`).
- **Fill:** `HUDSetBarPercentage(1, fill, fill2, index, value2)` stores `fill` clamped to 0-1 in slot `index`
  (`HudGenericBar_SetFill`, `0x001c2530`) and, like kind 2, also writes the chase gauge (harmless while it is off).
- **Quirk:** in a two-player game each creation adds 0.22 to the static y `0x0050d548` that the set-up uses before
  the first re-layout; nothing resets it. Confirmed (code); it is overwritten on screen by the per-frame layout above.

**Kind 2: the chase gauge** (`HUD_EnableGaugeBar(on, texA, texB)`, `0x001b4f00`; class vtable `0x00539600`): a
marker that slides along a track between two values, green in the middle and red at the ends. On sets it up once
(`ChaseGauge_Setup`, `0x001a3720`); off releases it (`0x001a38d8`).

- **Texture:** sprite words are `sheet << 16 | rectangle` of the [sprite-sheet table](gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header);
  the defaults (`texA` `0x1a0004`, `texB` `0x1a0000`) and the fixed marker `0x1a0007` are sheet record 26,
  **`chasebar`**: one 256 × 64 texture with 8 rectangles: 0 and 1 are 144 × 13 texel bars (two tracks), 2, 3, 5 and
  6 icons of about 23-29 × 31-38 texels, 4 an icon of 40 × 33 and 7 the 26 × 29 marker. Confirmed (runtime), PCSX2
  2.9.94 over PINE: sheet-table record 26 has name hash `0x7b836eda` (the CRC-32 of `chasebar`, a string on the
  disc) and size 18,016; the rectangles from the disc (corroboration).
- **Sprites** (three `BaseWidget`s, untinted, anchored at their centre), with the base position (0.8, 0.25)
  (0.47 in a two-player game, `0x0050d544`) and `size` 0.03 (gauge `+0x360`):
    - the **track** `texB` at the base, height `size` (0.03 overlay units, about 12 pixels; width from the rectangle's
      aspect: about 134 pixels for rectangle 0), depth 8,000, so drawn under the rest;
    - the **end icon** `texA` at (base x + 0.115, base y), height 2 × `size`, depth 11,000;
    - the **marker** (`0x1a0007`) at (base x − 0.115 + f × 0.23, base y + 0.04), height 2 × `size`, depth
      11,000, where the 0.23 is 2 × 3.8333 × `size` and **f = 1 − (value − min) / (max − min)**: `value` =
      `max` puts it at the left end, `value` = `min` at the right end by the icon.
- **Marker colour:** red (255, 16, 16) and green (115, 183, 11) mixed by t = 1 − 2 × |0.5 − f|: green at the
  centre, red at both ends.
- **Values:** set-up gives max 60 (`+0x348`) and value 30 (`+0x340`); min (`+0x344`) is not set there (0 until a
  script sets it, inferred from the zeroed static object). `HUDSetBarPercentage(2, fill, fill2, index, value2)` sets
  value = `fill`, max = `fill2` and min = `value2` (`0x001b4fc0`); nothing is clamped.
- **Drawing:** `ChaseGauge_Update` (`0x001a3990`) runs from `HUD_Update` while the gauge exists (HUD `+0xa94c`) and
  places the sprites once their sheet is loaded (`0x001a3930`); `ChaseGauge_Render` (`0x001a3c00`, from `HUD_Render`)
  draws track, marker, icon.

Kind 0 (a red (200, 30, 30) bar, `0x001b4ba0`) and kind 3 (a stack of up to four labelled bars, `0x001b5008`) are
described with the binding. **`HUDSetBarProperty(index, gradient, rgba, width)`** sets slot `index`'s fill colour
(HudBar `+0x28`), its gradient flag to `gradient` and its width (`0x001c22b0`, × 0.7 with device flag `0x02`): with
`gradient` true the red-to-green colour replaces the given one at the next draw, so only `false` keeps a custom
colour. Confirmed (code) at `0x001b54f0`, `0x001a1138`.

### Action prompts {#action-prompts}

The "what can I do here" text at the bottom of the screen: **one prompt per player** (HUD `+0x6b40` and `+0x70d0`,
`0x590` bytes each), a scroll-in text widget (base `0x001e6e98`, vtable `0x00539270`) plus an optional two-frame
icon sprite at `+0x480`. Confirmed (code) unless marked:

- **Set-up** (`ActionPrompt_Setup`, `0x0019ef80`, from the HUD's init `0x001adb60`): position x **0.5**, base y
  `+0x464` = **0.86** (read at runtime; the code compares it with 0.86, 0.78, 1.0 and 0.8 for the other modes), size
  0.06, colour **(128, 128, 128, 255)**, font slot 3 (`part_page0`), not visible; `+0x468` the player.
- **Place** (`ActionPrompt_Update`, `0x0019f528`): one player: x 0.5, centred; y = base + the raise `+0x46c`. Two
  players: x 0.33 or 0.66 (`0x0050cfcc` / `0x0050cfd0`, 0.25 / 0.74 in 16:9).
- **Text, alignment and size.** The widget itself is left-aligned: its alignment byte `+0x17c` is 0 from the constructor
  (`0x001b8f98`), and nothing on the prompt's path sets it (runtime: 0 for both prompts). The centring comes from the
  **text**: every HUD string the code picks for the prompt (strings 0, 1, 4 and 9) opens with `<CENTER>`, which centres
  the line on the pen at x 0.5 by its measured width ([Markup](gui.md#markup)); then `<COLOR B2B2B2FF>` and `<SIZE
  1.0>`. Runtime (PCSX2 2.9.94, `level99`, the string table `0x00600048`): 19 of the 397 HUD strings open with
  `<CENTER>`. Size: `MessageHUD_Setup` (`0x001b9090`) gets the size 0.06 (an overlay height, from the HUD's init) and a
  font scale of 1.0 (`Font_Size(1.0)` into `+0x1c0`); font slot 3 (`part_page0`), since the last argument (0 from the
  HUD's init) is not 6. So the text is drawn at size 0.06 × 1.0 in colour (178, 178, 178, 255), the tag's, not the
  widget's (128, 128, 128, 255). Confirmed (code) for the widget, confirmed (runtime) for the strings' tags; the prompts
  from action objects (`+0x10`) and talkable humans were not checked.
- **No separate icon without the cycle.** With the cycle off (`+0x45c` = 0) `ActionPrompt_Update` only sets the text's
  visibility from `+0x460`; the icon widget `+0x480` is neither placed nor updated, and `ActionPrompt_Render` draws it
  only while `+0x45c` is set. The box under it is style 1 (`BoxedText_SetupBox(…, 1)`, `0x001e6f50`), which sets up **no
  backdrop** (`+0x204` = 0) and **no box icon** (`+0x208` = 0), so `BoxedText_Render` (`0x001e7750`) draws the text
  alone. The button on screen is part of the text: the strings wrap a button tag in `<MONEYFONT>` (`<ST>` in 0, 1 and 9;
  `<SDL>`, the animated stick, in 4), drawn as a glyph of the font in line with the words. A text that parses as a
  number is instead a **sprite word**: `ActionPrompt_SetText` hands it to `ActionPrompt_SetNumberText` (`0x0019f128`) →
  `MessageHUD_SetIcon` (`0x001bb1e0`), which drops the text and shows that sprite in the widget's own box `+0x70`.
  Confirmed (code).
- **The raise** (`ActionPrompt_SetRaise`, `0x0019f430`, each HUD update) keeps the prompt clear of the texts below it:
  while the hint box shows (box `+0x3a8` set and no scroll-in), raise = 0.05 (`0x0050d500`) − the box's height (`+0x3b0`);
  otherwise −0.02 (`0x0050d504`) − the showing scroll-in's height (scroll-in `+0x578`, 0 when none). A prompt of more
  than one line moves up a further 0.025 (`0x0050cfb4`) × its height in lines. **Runtime check** (PCSX2 2.9.94, quick
  save slot copies, `level99`): with a hint box 0.124 high the raise was −0.0738 = 0.05 − 0.124.
- **Its text** is chosen each update by `HUD_Update` (`0x001af010`) for each player human (game state `+0x224` of them,
  handles at `+0x228`) who is free to act, confirmed (code): not knocked out; not cuffed, unless upgrade (6, 15) is
  unlocked and his player holds a key (item 6); not in a scene state, not in state `0x100000000`, with a player record;
  not mugging, tagging, in a button mini-game, being mugged or in the mug meter; not aiming a throw (record `+0x1be` =
  −1 with the move `Human_MoveThrowAim`); his mini-game mode (player state `+0x46`) not 2 or 3 (lock pick, stereo); his
  action not `0x15` (uncuffing); and command `0xa` (triangle) enabled. Each update starts with no text for either
  player, so a player who is not free has his prompt cleared (`HUD_ClearActionPrompt`). Then, in this order, the first
  that applies:

    1. **Mug or interrogate**: `Mug_CanMugVictim` (`0x00225ff0`) holds and the held human (`+0xc4`) resolves: HUD string
       1 (mug) when that human's `+0x5a0` is 0, else string 0 (interrogate).
    2. **Revive**: `Human_FindRevivableNear` (`0x00279078`) finds a human and the **player or the downed human** carries
       a flash: HUD string 4. Details below.
    3. **The action object** in reach (`0x00240888`): its prompt `+0x10`, and its hint `+0x14` is queued in the hint box
       **at priority 0** once (remembered at HUD `+0x177bc` per player, withdrawn by `0x001b3e20` when the object or its
       hint changes or none is in reach).
    4. Else a **talkable** human within 1.5 m (`ActionPrompt_FindNearbyHuman`, `0x001acd60`): see [the talk
       prompt](#talk-prompt).

    The strings are `GSTRING.HUD` entries ([Strings](gui.md#strings)); the action objects and their kinds:
    [Crimes](crimes.md#triangle).
- **The revive test**, confirmed (code) at `0x00279078`, `0x0029cc78`, `0x0029ca28`, `0x00278fa0`:

    - **Who is searched**: `Humans_FindAhead` (`0x002274a8`) lists up to 60 humans, the player excepted, within **3 m**
      in 3D of the player's position (every direction; humans whose slot flag in `0x00715390` has bit 1; inferred:
      present); with more than 16 the 16 nearest are kept.
    - **The filter** `Human_IsRevivableBy(player, him)` (`0x00278fa0`): he is friendly to the player
      (`Human_IsFriendly`, `0x00222a90`) or in the same gang (brain `+0x20c`); knocked out (`0x00227dd8`); not cuffed
      (`0x00223b70`); not in state `0x80000000` (`0x00227e18`) or `0x100000000` (`0x00227eb0`); and **revivable**, human
      flag `+0xe0` bit `0x4` (`HuSetRevivable`; `Human_MakePlayer` sets it on every player, scripts set or clear it on
      others).
    - **Sight, no facing**: each candidate must also pass `Human_HasLineOfSight(player, him)` (`0x00222288`: a ray at
      1.7 m to his 1.7 m, else to his 1.0 m; it passes fences, railings and glass, [AI: sight](ai.md#sight)). The list's
      field-of-view test is only consulted beyond 3 m (squared distance over 9), which the 3 m search never yields, so
      **facing does not matter**.
    - **Which one**: the candidate nearest the player's position (`HandleArray16_NearestHandle`).
    - **The flash**: the count of item 1 in the inventory at `W_GameState + 0x480` (`Inventory_Count`, `0x0041e420`) for
      the **player's** panel index (human `+0x380`), or, failing that, for the **downed human's** `+0x380`. An inventory
      belongs to a player panel (0 or 1) and the count is 0 for any other index, so the downed human's flash counts only
      when he is the other player (co-op); an AI crew member (`+0x380` = −1) has none.
    - **Order**: after the mug test and before the action object, under the same "free to act" gates; a player who is
      himself knocked out gets no prompt.
- **Showing:** no text hides the prompt (`0x001b2490`); a new or changed text restarts it (`ActionPrompt_SetText`,
  `0x0019f1b0`; a text that parses as a number goes through `0x0019f128`). A prompt naming `Spray`, `Flash`, `Blades`
  or `Give Mon...` also wakes the player panel ([Activity](#the-player-panel)).
- **The cycle animation** (`HUDTurnOnActionCycleAnim(framesPerIcon, seconds, blinkFrames, iconA, iconB, player)` →
  `ActionPrompt_StartCycle`, `0x0019f270`), confirmed (code) unless marked:
    - **The sprites**: `iconA`/`iconB` go to `+0x440`/`+0x444` as sprite words of the icon widget `+0x480`, which the
      set-up gives batch 3 (`ActionPrompt_Setup`, `0x0019ef80`), so they are **`part_page0` rectangles**. The scripts
      (disc scan of the compiled scripts) pass **73**, the triangle button, in `level51_chapter8` and `level54`, and
      **79**, the cross button, in `level51_chapter8`, `level51_chapter9`, `level81_chase` and `level95_workout`
      (`level84_chase` computes them); every call passes the **same word twice**, so the swap never shows and only the
      blink does.
    - **Size**: the start sets the icon's size to its first argument in overlay units (0.1 from the binding), square
      for these 34 × 34-texel buttons, and **hides the text** (`BoxedText_SetVisible(prompt, 0)`).
    - **Place**: each `ActionPrompt_Update` (`0x0019f528`) puts the icon's **centre at the prompt's own anchor**: x 0.5
      for one player (two players: `0x0050cfcc`/`0x0050cfd0`, or 0.5 for both while either player human is cuffed
      or knocked out), y = the base 0.86 + the raise `+0x470` (the raise before the multi-line lift that `+0x46c`, the
      text's, adds). So the icon stands where the text's line would be, centred on it. While the cycle is on, the
      update no longer sets the text's visibility from `+0x460`, so the text stays hidden until `ActionPrompt_SetText`
      restarts it with a new owner or text (it would then overlap the icon; not seen, inferred).
    - **Rate and blink** (`ActionPrompt_UpdateCycle`, `0x0019f328`, once per prompt update; units are **HUD updates**,
      one per game frame, inferred): a counter `+0x44c` swaps the word index `+0x450` whenever it reaches a multiple
      of `framesPerIcon` (`+0x448`; the first update already swaps, and 0 traps on the division), so each word lasts
      `framesPerIcon` updates (scripts: 4, or 3 in `level54`). With `blinkFrames` (`+0x458`) non-zero, the icon is on
      (active `+0x488`) for `blinkFrames` updates and off for as many (6 and 6, or 2 and 2 in `level54`); with 0 it
      stays on. The `seconds` argument is ignored ([HUD bindings](../references/bindings/hud.md#hudturnonactioncycleanim)).
    - **Drawn** by `ActionPrompt_Render` (`0x0019f850`) under the text, in a two-player game or for player 0;
      `HUDTurnOffActionCycleAnim` (`0x0019f320`) clears `+0x45c`.
- **Drawn last** in the HUD, and not while an announcement or a mini-game panel shows ([The HUD's
  frame](#the-huds-frame)).
- **Mini-game widgets:** the lock-picking dial (HUD `+0xf420` + player × `0x540`), the stereo theft's (`+0xfea0` +
  player × `0xb10`) and the mash meter (`+0xebc0` + player × `0x430`), [Crimes](crimes.md).

#### The talk prompt {#talk-prompt}

Step 4 of the prompt's text, `ActionPrompt_FindNearbyHuman` (`0x001acd60`). Confirmed (code) at `0x001acd60`,
`0x0021d530`-`0x0021d570`, `0x00303404`, `0x00305bfc`, `0x00384aa0`, `0x00384b30` unless marked:

- **Who is searched**: `Humans_FindAhead` (`0x002274a8`) lists up to 100 humans within **1.5 m** of the player's
  position (the same search as the revive test); the **first** in the list's order that passes is taken.
- **Talkable** means both: the human's byte **`+0x1b2`** is set, and his brain type (brain `+0x04`, [AI](ai.md#types))
  is **0** (a player: the other player in co-op) or **3** (a Warrior). The human's interface at `+0x70` (vtable
  `0x0053f020`) also asks "has a message-0 handler or `+0x1b2`" (slot `+0x50`, `0x0021d570`), which `+0x1b2` already
  answers. Gang leaders, bums, dealers and shopkeepers also get `+0x1b2` from their goals (`InfoTactic_Seat`,
  `BumLogicGoal_Setup`, `DealerGoal_Process`, `ShopkeeperGoal_Process`, `AddressTactic_PushApproach`), but their brain
  types (2, 4, 5, 6) fail this test; their prompts come from the kind-4 and kind-5 context records of step 3
  (`InfoTactic_Seat` and `BumLogicGoal_Setup` register kind 5 with HUD strings 8 and 10).
- **Who sets it**: the brains, every think; no Lua binding. `WarriorBrain_Think` (`0x00305bfc`) and `PlayerBrain_Think`
  (`0x00303404`) write `+0x1b2` = 1 exactly when [the swap prompt](ai.md#think-warrior)'s conditions hold (else 0),
  and then write the text of `GSTRING.HUD` string **`0xc`** (both hold an object), **`0xd`** (he holds) or **`0xe`**
  (the player holds) straight into the human's message-handler component (human `+0xe8`): `+0x7c` = that text,
  `+0x78` = 0.
- **Its text**, in order: the component's `+0x78` read as a `GSTRING.HUD` id when it is **1-388** (slot `+0x20`,
  `0x0021d530`, message 0 only); else the text at `+0x7c` when not null (slot `+0x28`, `0x0021d550`); else HUD string
  **9**. `+0x78` is written only by `ScriptHandler_SetSlot` (`0x00384aa0`), whose one caller, `SetMsgHandler`, passes 0,
  and by the brains above (0), so the id branch is never taken (inferred: no other writer found). `+0x7c` is the swap
  text, or the `prompt` of [`SetMsgHandlerEx`](../references/bindings/script.md#setmsghandlerex)`(human, 0, callback,
  prompt)` (`ScriptHandler_SetSlotEx`, `0x00384b30`, which also zeroes `+0x78`); a Warrior's brain overwrites that text
  whenever it sets the swap prompt.
- **In practice** the talk prompt is the **swap-weapons prompt** of a Warrior (or the co-op partner) beside the
  player, strings `0xc`-`0xe`; triangle then sends the event-0 prompt (`WarriorBrain_OnPrompt`, `0x00306040`).

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

### The target panel

While the player has a target (human `+0xc8`, the one attacks and grabs aim at) that is not an ally (`0x00222a90`) and
not down (`0x00227dd8`), `TargetPanel_Update` (`0x0020e6f8`, called from `0x00210e48`) shows a panel for it: its name
and a bar. Each player has two panel slots (`0x0063f240` + player × `0xd00` + slot × `0x680`); a new target takes a
free slot or the older one, so the last two targets can show side by side. Confirmed (code); not seen at runtime.
The target panel belongs to the **Armies of the Night panel** (`ANHud`, [its functions](#fn-anhud)): the only
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
| both [radar frames](#fn-radar-frame) (`+0x177d0`, `+0x18280`) | their parts off | their parts on (when set up, `+0xa98`) |
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

### Pads {#hud-pads}

Each player has a `0x2c`-byte input record (HUD `+0x00`/`+0x04`) whose byte `+0x19` is the pad record it reads
(-1 none) and `+0x1b` whether it is bound; each [pad record](frontend.md#pad-record)'s `+0x42` names its owner.
`HUD_UpdatePadBindings` (`0x001ae828`) runs in every menu and mode update and assigns player 0, then player 1 once
player 0 is bound. For one player (`0x001ae638`): player 2 in a one-player game in mode 1 is unbound; an unbound
player takes the pad its human's player record names; failing that, outside modes 1 and 10, the first connected pad
record (of the 8) that the other player does not use **on which START is pressed** (`0x00144bf0(pad, 0x800)`); a
bound player whose pad record names another owner is rebound to it. `PM_Greet` clears every binding first
(`0x001ae5a0`), so the title screen's START picks player 1's pad. Confirmed (code).

### The split-screen divider {#hud-divider}

In split screen (`HUD_IsSplitScreen`, or more than one view with `0x0050b194` > 1) the HUD lines up 19 sprites
(HUD `+0x137e0`, set up with size 0.1, sprite word `0x170`, depth 300, colour (191, 191, 191, 255)): with
`0x0050b1a4` = 0, 15 of them in a **column** at x `*0x0050d47c`, y from `*0x0050d480` in steps of `*0x0050d48c`,
sized (`*0x0050d494`, `*0x0050d498`), unrotated (the other 4 hidden); with it set, all 19 in a **row** at y
`*0x0050d488`, x from `*0x0050d484` in steps of `*0x0050d490`, sized the other way round and rotated a quarter turn.
`HUD_Render` draws them once after each layout (`+0x225d8`). Confirmed (code) at `0x001af010`, `0x001b1688`; that the
sprites form the line between the two views is inferred.

### The fixed-camera icon {#hud-fixed-cam-icon}

A **"this camera can't be turned" hint**: a crossed-out film camera (`part_page0` rectangle 87, sprite word `0x57`,
batch 3) that appears while the player pushes the **right stick** on a camera that ignores it, and fades out over a
second after he lets go. Confirmed (code) at `0x001af010` unless marked.

- **Set-up** (`HUD_InitLevel`, `0x001ad588`): one `BaseWidget` per player at HUD `+0x135e0` + player × `0x100`, size
  0.1, depth 11,000, colour (191, 191, 191, 255), **hidden but active**; `+0x177b4` = 1.
- **When** (each `HUD_Update`, per player): if the player's camera type (camera vtable `+0x1ec`) is **0 `Cam_Fixed`,
  1 `Cam_Locked`, 5 `Cam_Transition` or 9 `Cam_Rail`** ([Cameras](../references/cameras.md#type)), or his camera
  switch 0 is off (`0x0050b1b8[player]`: right-stick turning and the zoom buttons, default on,
  [switch 0](../references/cameras.md#switch)), then pushing the stick (`HUD_IsPlayerStickPushed`, `0x001aef50`: a
  raw byte `+0x1a` or `+0x1b` of the pad record outside 64-176) shows the icon and cancels its fade
  (`BaseWidget_CancelFade`, `0x001a25b8`: end time `+0xf4` = 0). Not pushing changes nothing. With any other camera
  and the switch on, the icon is hidden at once. Pad `+0x1a`/`+0x1b` are stored with the right stick's floats
  `+0x10`/`+0x14` (`0x00149990`) and are what the camera's [right-stick step](camera.md#right-stick) reads, so this is
  the right stick.
- **The fade**: later in the update a shown icon gets `BaseWidget_StartFade(icon, 1000)` (`0x0050d49c` = 1,000 ms),
  which starts only when no fade runs; `BaseWidget_RenderAlpha` (`0x001a2690`) draws it at its alpha × (time left ÷
  1,000), and `BaseWidget_IsFadeDone` hides it when the time is up. A pushed update cancels the fade and the same
  update restarts it, so the icon stays fully opaque while the stick is held and **fades linearly to nothing over 1 s**
  after release.
- **Place and size** (set each update while shown; GUI position; the size is an overlay height, the width following
  from the rectangle's aspect). In the default mode (device flag `0x01` only) none of these globals is rewritten, so
  the static values hold: **(0.9, 0.64)** (`0x0050d468`, `0x0050d46c`), size **0.09** (`0x0050d450`; `0x0050d454` =
  0.08 also passed). In two-player views (split screen, or `0x0050b194` > 1 with more than one view) player 0's icon
  is at **(0.11, 0.64)** (`0x0050d460`, `0x0050d464`, never rewritten) and player 1's at (`0x0050d468`, `0x0050d46c`).
  Other modes rewrite them: 16:9 (`0x01` with `0x04`) x 1.05, size 0.08; progressive 16:9 (`0x01`, `0x20`, `0x04`)
  (1.0, 0.63), size 0.08 × 0.08; with `0x01` clear, (0.9, 0.63) and 0.06 × 0.058 in 4:3, (1.09, 0.58) and 0.075 ×
  0.08 in 16:9.
- **On in story play.** `+0x177b4` is written (by the set-up and `HUD_SetFixedCamIconVisible`) but never read. What
  gates the icon is each widget's **active** flag (`+0x08`: `BaseWidget` vtable `0x005394b8` slot `+0x40` sets it,
  `0x004e3fb8`, slot `+0x48` reads it), which `BaseWidget_RenderAlpha` tests with visible (`+0x04`); the set-up turns
  it on. `HUDEnableFixedCamIcon(false)` turns it off, and the scripts call it only in `level60`-`level64` (once each,
  with `false`; disc scan of the compiled scripts); nothing turns it back on within a level. So in every other level it
  shows on a fixed, locked, transition or rail camera, or while a script has switch 0 off.

### The spinner {#hud-spinner}

A sprite at HUD `+0xe050` (`0x001ae378`: size 0.09, depth 11,000, `part_page0` rectangle **92** (sprite word `0x5c`,
batch 3), colour (191, 191, 191, 255), **not visible** and active from the set-up, at (0.95, 0.83) in the default
mode, (0.9, 0.76) or (1.09, 0.77) in the others). It is level loading's "blinking element" `0x0060e890` (HUD
`0x00600840` + `0xe050`). Confirmed (code) unless marked:

- **It blinks; it does not turn.** While shown (`+0xe054`), `HUD_Update` (`0x001b0580`-`0x001b0658`) adds the rate
  `+0x19128` to the global `0x0050d50c` and then writes **0** to the widget's rotation (`+0xa0`, radians), re-places it
  by video mode and updates it. Nothing reads `0x0050d50c` (its only two references are that read and write), and
  `LoadScreen_DrawPulse` (`0x001613f0`) also zeroes the rotation (`0x0060e930`) before drawing. So the rate is dead:
  the widget is always drawn upright, whatever `+0x19128` holds.
- **The blink** is `LoadScreen_DrawPulse`'s: the colour (170, 43, 43) × 1.3 = **(221, 56, 56, 255)**, faded linearly to
  transparent over the first 1,100 ms of each 2,200 ms of the real-time clock (`0x0050b8b8` slot `+0x34`, ms) and
  back over the second ([Level loading](level-loading.md#memory-card-screen)). The colour it leaves stays on the
  widget; nothing restores the grey.
- **The rate argument.** Every caller passes the real-time clock's milliseconds × 0.001 (seconds since the clock
  started, the clock value itself, not a frame time): `HUD_Render` (`0x001b18a4`), `Preload_DrawLoadingIndicator`
  (`0x00161600`) and `MemCardLoadScreen_Tick` (`0x001619d0`). Since the angle is dead, a reimplementation can ignore it.
- **Drawn by `HUD_Render`** (`0x001b1688`) in two places. (1) The **fade branch**: no letterbox, captions not freezing,
  the profile manager done (`PM_IsDone`, flag `0x0050f5b0` = 1), HUD `+0xea3c` and `+0xe53c` both 0, player 1's
  fade level (`0x005fdeb8 + 0x1d8`) at least 1.0 (the screen fully faded), and the top game mode not `0x14`: shown,
  updated, drawn by `LoadScreen_DrawPulse`, hidden again, and **nothing else** of the HUD is drawn that frame.
  (2) The **normal branch**, while the HUD is shown (`+0x177a0`): `BaseWidget_Render` on it right after the
  instruction arrow (`0x001b1a48`), which draws only while it is visible (`+0xe054`).
- **Who leaves it visible:** `HUD_SetSpinner(rate, hud, on)` (`0x001b24d0`) is turned off again in the same call by
  `HUD_Render`'s fade branch and by `Preload_DrawLoadingIndicator`, but `MemCardLoadScreen_Tick` only turns it **on**.
  So after the start-up memory-card load it stays visible, drawn by the normal branch in its last pulse colour, until
  the next fade branch turns it off (inferred from the code; not seen at runtime).

### What `level99` uses

The first mission's scripts call, among the HUD bindings: `HUDSetObjective`, `HUDRemoveAllGoalText`,
`HUDSetTutorialText`, `HUDFlushTutorialText`, `HUDSetTutorialCallback`, `HUDEnableGameTutorialText`,
`HUDEnableInstArrow`, `FlashRageBar`, `ForceShowPlayerHud`, `HUDTurnOnRadar`, `HUDTurnOffRadar`, the radar objective and
item calls, `HUDGetNewPH` / `HUDSetPHValue` / `HUDReleasePH`, `HUDSetAnnounceMsg`, `ShowHud`, `HideHud` and `RestoreHud`
(the `mission1` flags of the [HUD bindings](../references/bindings/hud.md)). It does not end on a HUD screen: the
mission ends through `HUDLaunchMissionComplete` and mode 0xb ([Front end](frontend.md#story-start),
[Scripts](scripting.md#run-next-mission)), which draws no screen of its own.

## Function index {#function-index}

Every function of the HUD files, by source file in address order, with the name it has in Ghidra (ours). Rows link to
the section that describes the behaviour where there is one. The files and ranges are from the [Source
map](source-map.md#gui).

### Before `GUI/BaseWidget.cpp` (no path string): HUD parts {#fn-pre-basewidget}

`0x0019dfb0`-`0x001a1f10`, the stretch the [Source map](source-map.md#position) gives to `GUI/` before
`BaseWidget.cpp`'s path string: the Armies of the Night **GO sign** (HUD `+0xe650`), the UI string and colour-tag
tables, the **action prompts** (HUD `+0x6b40`, vtable `0x00539270`), the **crew status list** (HUD `+0x10`, vtable
`0x00539370`, entries vtable `0x00539408`, `0x250` bytes each) and the `HudBar` meter. Files not named; evidence for
the behaviour is confirmed (code) unless a row says otherwise.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0019dfb0` | `GoSign_Construct` | the GO sign: a `TextWidget` and three `BaseWidget` arrows (`+0xd0`, `+0x1d0`, `+0x2d0`); sound handle `+0x3d8` = -1 | confirmed (code) |
| `0x0019e030` | `GoSign_Setup(sign, position)` | from the HUD's level set-up (`0x001ad588`): positions per video mode (globals `0x0050cf64`-`0x0050cfa4`, separate values for 16:9, PAL and progressive); text = global string `0xd3`, colour `0x00600040`, arrows size 0.08 in `(191, 191, 32, 255)`; mode `+0x3d0` = 2 (off) | confirmed (code) |
| `0x0019e5f8` | `GoSign_Shutdown` | releases the text and the arrows (HUD shutdown `0x001ae980`) | confirmed (code) |
| `0x0019e660` | `GoSign_StepBlink` | every `*0x0050cfa8` ms of real time advances the frame byte `+0x3d1`, wrapping at 2 in mode 0 and at 4 in mode 1 | confirmed (code) |
| `0x0019e758` | `GoSign_Update` | from `HUD_Update`, Armies of the Night levels only (`0x0041d110`). Mode `+0x3d0` is the byte `HUDANSetGOSignMode` writes (`0x0060f260` = HUD `+0xea20`): **0** the text plus one arrow blinking on frame 0; **1** no text, the three arrows lit one at a time (frames 0-2, frame 3 none); **other** everything hidden | confirmed (code) |
| `0x0019ed28` | `GoSign_Render` | unless mode 2: the text and arrows; outside the pause mode (`0xa`) and when the last one has ended, plays `vags/misc/bleep27` again (handle `+0x3d8`) | confirmed (code) |
| `0x0019ee10`, `0x0019ee50` | `GoSignColour_StaticInit`, `_StaticInitStub` | static initialiser (ctor list `0x005340f4`): `0x00600040` = `(245, 184, 0, 255)` | confirmed (code) |
| `0x0019ee70`, `0x0019eea0` | `GlobalString_Get(id)`, `GlobalString_Set(id, text)` | the UI string table `0x00600048` (an unset id gives the empty string `0x00553ba8`); `Set` stores a copy ([Strings](gui.md#strings)) | confirmed (code) |
| `0x0019eee0`, `0x0019eef8` | `HudColourTag_Get(slot)`, `HudColourTag_Set(slot, text)` | the markup colour-tag strings of `CfgHUDColor` (table `0x00600660`, copies) used as prefixes in HUD texts | confirmed (code) |
| `0x0019ef38` | `ActionPrompt_Construct` | scroll-in text base (`0x001e6e98`), vtable `0x00539270`, icon `BaseWidget` at `+0x480` | confirmed (code) |
| `0x0019ef80` | `ActionPrompt_Setup` | [Action prompts](#action-prompts) | confirmed (code) |
| `0x0019f0f8` | `ActionPrompt_Shutdown` | slot `+0x68`: releases the icon, then the scroll-in text | confirmed (code) |
| `0x0019f128` | `ActionPrompt_SetNumberText` | a text given as a string id: restarts the prompt (slot `+0xe0`) unless the owner and id are unchanged | confirmed (code) |
| `0x0019f1b0` | `ActionPrompt_SetText(prompt, owner, text)` | restarts the prompt (slot `+0xe8`) when the owner or text changed | confirmed (code) |
| `0x0019f270`, `0x0019f320` | `ActionPrompt_StartCycle(size, prompt, words, n, rate, hold)`, `_StopCycle` | `HUDTurnOnActionCycleAnim` / `Off`: the sprite words at `+0x440`, rate `+0x448`, hold `+0x458`, on `+0x45c` | confirmed (code) |
| `0x0019f328` | `ActionPrompt_UpdateCycle` | every `+0x448` updates the icon switches between the two words; with a hold, the icon shows for `hold` frames and hides for `hold` frames | confirmed (code) |
| `0x0019f430` | `ActionPrompt_SetRaise` | [Action prompts](#action-prompts) | confirmed (code) |
| `0x0019f4c8` | `ActionPrompt_UpdateSplitX` | two-player x positions `0x0050cfcc` / `0x0050cfd0`: 0.33 / 0.66, or 0.25 / 0.74 in 16:9 | confirmed (code) |
| `0x0019f528` | `ActionPrompt_Update` | slot `+0x30`, [Action prompts](#action-prompts) | confirmed (code) |
| `0x0019f850` | `ActionPrompt_Render` | slot `+0x38`: in a two-player game or for player 0, the cycle icon (when on) and the text | confirmed (code) |
| `0x0019f8b8`, `0x0019f928` | `ActionPrompt_RenderIcon`, `_RenderText` | `HUD_Render`'s last step: the icon or text once after an update (`+0x584`, `+0x580`) | confirmed (code) |
| `0x0019f988` | `CrewStatus_GetIcon(human)` | the portrait rectangle: in a story game by the character kind (`0x00229570`, kinds 0-11 → `0x31`, `0x28`, `0x25`, `0x23`, `0x2e`, `0x29`, `0x33`, `0x30`, `0x27`, `0x24`, `0x2b`, `0x2a`), otherwise by which player's gang it is in (`0x2c`, `0x2d`); default `0x28` | confirmed (code) |
| `0x0019faf0` | `CrewStatus_Init` | eight order slots (`+0x44`) = -1, entries hidden, count `+0x40` = 0, layout `+0x1310`-`+0x1324` | confirmed (code) |
| `0x0019fbb0` | `CrewStatus_Setup(iconSize, aspect, list, position, cell, step, columns, rows)` | from the HUD's level set-up (0.05, 0.7, columns 2, rows 4): the grid, and the caption markup text `+0x13b0` (size 0.1, grey 128) | confirmed (code) |
| `0x0019fd00` | `CrewStatus_Shutdown` | slot `+0x68`: releases the eight entries | confirmed (code) |
| `0x0019fda0` | `CrewStatus_Update` | slot `+0x30`, from `HUD_Update` while the statistics game mode is below 100: each live entry at its grid cell (order index modulo the columns, rows below), entries whose human no longer resolves removed; the caption under the last row | confirmed (code) |
| `0x001a03a8` | `CrewStatus_Render` | slot `+0x38`: the entries and the caption | confirmed (code) |
| `0x001a0420` | `CrewStatus_Add(list, human)` | from `0x001b3960` (`Humans_Update`): a member of player 1's gang who is down (`0x00227dd8`), dead (`0x00223b70`) or lost (brain `+0x152` set and over 50 m off) gets one entry (eight at most); the caption becomes colour tag 5, the human's name (capitalised) and string `0xe1`, `0xe2` or `0xe3` | confirmed (code); the three states' meanings inferred from the icons |
| `0x001a07f0` | `CrewStatus_Remove(list, human)` | from `Human_Destroy` and `Human_MakePlayer` (`0x001b39a0`): drops the human's entry and renumbers the rest | confirmed (code) |
| `0x001a08c8`, `0x001a0918` | `CrewStatusEntry_Init`, `_Setup` | portrait sprite `+0x40` (`(191, 191, 191, 255)`, `CrewStatus_GetIcon`) and status sprite `+0x140` (rectangle `0x19`, grey 128); 0.035 further left in 16:9 | confirmed (code) |
| `0x001a0b00` | `CrewStatusEntry_Shutdown` | slot `+0x68`: both sprites released, the human handle cleared | confirmed (code) |
| `0x001a0b60` | `CrewStatusEntry_Place` | the portrait at a position, the status sprite offset by (`0x0050d018`, `0x0050d01c`); sizes × 0.8 in 16:9 | confirmed (code) |
| `0x001a0c90` | `CrewStatusEntry_Render` | slot `+0x38`: both sprites when the batch is resident | confirmed (code) |
| `0x001a0d28` | `CrewStatusEntry_Update` | slot `+0x30`: hidden unless the human is in player 1's gang; status rectangle `0x1a` (down), `0x19` (dead) or `0x15` (lost); the status alpha pulses on an 800 ms triangle wave, cubed | confirmed (code) |
| `0x001a0fd0`, `0x001a1008`, `0x001a1098` | `Bar_Construct`, `HudBar_Construct`, `HudBar_Init` | the meter's set-up and defaults ([The rage meter](#the-rage-meter), [GUI](gui.md#widget-classes)) | confirmed (code) |
| `0x001a1138` | `HudBar_Draw(flashBelow, bar, flash, trail)` | the end caps (rectangles +2, +3), back (+0), fill (+1) and an optional second fill `+0x3c`; a flash below `flashBelow` and a fading trail, both unused: every caller passes 0 for both ([The rage meter](#the-rage-meter)) | confirmed (code) |
| `0x001a1be8` | `HudBar_SetFillGradient(bar, on, low, high)` | `+0x60` on: the fill colour is lerped from `+0x30` to `+0x34` by the fill | confirmed (code) |

### After `GUI/BaseWidget.cpp` (no path string): mash meter and chase HUD {#fn-after-basewidget}

The **mash meter** (HUD `+0xebc0` + player × `0x430`, vtable `0x00539568`) and the **chase HUD** (HUD `+0x9640` =
`0x00609e80`, vtable `0x00539698`) of the `HUDSetChaseHUDState_*` bindings. The chase gauge between them
(`0x001a3660`-`0x001a3c00`) is described with `HUDEnableBar`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001a29c0` | `MashMeter_ApplyVideoMode` | fills the layout table `0x0050d050` (`0x80` bytes per player) for the video mode | confirmed (code) |
| `0x001a2f68` | `MashMeter_Setup(meter, player)` | button sprite `+0x50`; `Bar` `+0x150` (sprite word `0x30050`, fill `(225, 186, 65)`); three `TextWidget`s `+0x1c0`, `+0x290`, `+0x360`, each one character (`"%c"`) of font slot 3: `0x96`, `0xa0`, `0x9c` ([The mash meter on screen](#mash-meter-layout)) | confirmed (code) |
| `0x001a32f0` | `MashMeter_Shutdown` | slot `+0x68`: the button sprite | confirmed (code) |
| `0x001a3310`, `0x001a3338` | `MashMeter_SetButton(meter, id, word)`, `MashMeter_SetFill(fill, meter)` | from the mini-games (`MiniGame_Update`, `MiniGame_Abort`): button id `+0x04` and its sprite; fill `+0x188` clamped to 0-1 | confirmed (code) |
| `0x001a3360` | `MashMeter_Render` | slot `+0x38`: button, bar and a blinking text at the table's period: button `0x171` alternates texts 2 and 3, any other shows text 1 every other period | confirmed (code) |
| `0x001a3458` | `MashMeter_Update` | slot `+0x30`: the table applied, button, bar and texts placed | confirmed (code) |
| `0x001a3c48`, `0x001a3ca0` | `ChaseHud_Construct`, `ChaseHud_Delete` | `TextWidget` `+0x50`, icon `+0x120`, state `+0x44` = 6 (none) | confirmed (code) |
| `0x001a3d00` | `ChaseHud_Create` | `HUDSetChaseHUDState_CREATE` (`0x001b4ab8`): icon, label (`Font_Size` 0.9, `(255, 183, 0, 255)`, font slot 6), a textured disc `+0x230`, value `+0x2e4` = 100, state 6 | confirmed (code) |
| `0x001a4008` | `ChaseHud_Shutdown` | slot `+0x68`: icon released, state 6 | confirmed (code) |
| `0x001a4050` | `ChaseHud_ValueColour(value)` | colour stops `0x0050d198`, `0x0050d1a8`, `0x0050d1b8`, blended over 50-70 and 70-90 | confirmed (code) |
| `0x001a41b0` | `ChaseHud_SetState(value, hud, state, n)` | 6 destroys; 5 colour by value; 3 a timer of `value` seconds counting up, a negative value counting down (state 4); 1 and 2 colour `(255, 183, 0, 255)` | confirmed (code) |
| `0x001a4310` | `ChaseHud_SetEndText(hud, text)` | up to 31 characters at `+0x2f0`, the message shown when a timer ends | confirmed (code) |
| `0x001a4348` | `ChaseHud_Update` | slot `+0x30`: waits for the batch; state 7 shuts down | confirmed (code) |
| `0x001a43d8` | `ChaseHud_Render` | slot `+0x38`: state 1 the disc; 2 the disc blinking; 3 / 4 the timer (value 0-100 over `+0x320` seconds), ending in state 7 with the end text shown as a message; 5 coloured by value | confirmed (code) |

#### The mash meter on screen {#mash-meter-layout}

The layout table `0x0050d050` (`0x80` bytes per player, rewritten by `MashMeter_ApplyVideoMode` every update):
`+0x00` button position `(x, 0, y, 1)`, `+0x10` button size, `+0x20` the bar's left end, `+0x30` / `+0x34` the bar's
width and height, `+0x40`, `+0x50`, `+0x60` texts 1-3's positions, `+0x70` the text size, `+0x74` the blink half
period (ms, integer). Values in the default video mode (device flag `0x01` only), GUI coordinates; confirmed (code)
at `0x001a29c0`, `0x001a2f68`, `0x001a3360` and `0x001a3458`:

| Part | Player 0 | Player 1 | Size | What |
| --- | --- | --- | --- | --- |
| Button sprite | (0.09, 0.59) | (0.885, 0.59) | 0.12 | the sprite word given to `HUD_MashMeterShow`; instance 2, depth 11,000, grey 128, centred |
| Bar | left end (0.02, 0.65) | (0.815, 0.65) | 0.2 × 0.025 | `HudBar_Draw(0, bar, 0, 0)` (no flash, no trail); sprite word `0x30050`: batch 3 (`part_page0`) rectangles 80 (back), 81 (fill), 82 and 83 (end caps); back grey 128, fill (225, 186, 65); fill = meter ÷ target ([Crimes](crimes.md#triangle)) |
| Text 1 | (0.045, 0.65) | (1.015, 0.65) | 0.04 | character `0x96`, the **triangle** glyph |
| Text 2 | (0.05, 0.59) | (0.845, 0.59) | 0.04 | character `0xa0`, the **L1** glyph |
| Text 3 | (0.21, 0.59) | (1.005, 0.59) | 0.04 | character `0x9c`, the **R1** glyph |

The texts are `TextWidget`s in **font slot 3** (`part_page0`, whose characters `0x91`-`0xa0` are the button pictures,
[GUI: markup](gui.md#markup)), grey 128, `Font_Draw` flags 1 (right-aligned, [GUI](gui.md#widget-classes)), so x is
the glyph's right edge (inferred from the flag). They are **not** HUD string ids: each is the `"%c"` of one code.
Blink half period: **400 ms**.

**Which sheet:** a bar's sprite word is not a sheet-table record. `HudBar_Draw` (`0x001a1138`) takes its high half
(`+0x2e`) as a **resource instance** (`ResourceMgr_Instance`, a slot of [the instance table](gui.md#resource-instances))
and its low half as the first rectangle of that instance's sheet; only `BaseWidget_SetInstance(-1)` (`0x001a2918`)
reads the high half as a sheet-table record (`ResourceMgr_SheetRecord`). Instance 3 is `part_page0`: confirmed
(runtime), PCSX2 2.9.94, `level99`, slot 3's name hash is `part_page0`'s CRC-32, as on the main menu; slot 11, the
mug meter's batch 11, is `hud_minigames`. So `0x30050` is `part_page0` rectangles 80-83 and the rage and mug bars'
`0x30036` `part_page0` 54-57. Sheet-table record 3 is `menu_system` (6 rectangles), which these bars never use.

**Render** (`0x001a3360`): only while the meter's button id (`+0x04`) is not 0 (slot `+0x5c` returns it). The button
sprite, then the bar, then one text by phase = (timer ms mod 800) / 400: with sprite word `0x171` (`part_page0`
rectangle 369) phase 0 draws text 3 (R1) and phase 1 text 2 (L1), so **R1 and L1 alternate every 400 ms** on either
side of the button; any other word draws text 1 (triangle) in phase 1 only, a 400 ms blink. Only one caller shows the
meter: `Uncuff_BeginMash` (`0x0022d3f8`) with button id 1 and **word `0x171`**, so in the game the triangle text is
never seen. `HUD_MashMeterHide` sets the id and the word to 0 and the fill to 0.

### After `GUI/BaseWidget.cpp` (no path string): the chase gauge {#fn-chase-gauge}

`HUDEnableBar` kind 2 (vtable `0x00539600`, one object at HUD `+0xa940`), [Scripted bars](#scripted-bars).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001a3660` / `0x001a36b0` | `ChaseGauge_Construct` / `_Destroy` | three `BaseWidget`s at `+0x40`, `+0x140`, `+0x240`; destroy shuts down and releases them | confirmed (code) |
| `0x001a3720` | `ChaseGauge_Setup` | `(size, max, value, gauge, pos, track, endIcon, marker)`: the three sprites, max `+0x348`, value `+0x340` | confirmed (code) |
| `0x001a38d8` | `ChaseGauge_Shutdown` | releases the sprites, clears created (`+0x0c`) | confirmed (code) |
| `0x001a3918` / `0x001a3920` | `ChaseGauge_SetValue` / `_SetRange` | value `+0x340`; min `+0x344`, max `+0x348` | confirmed (code) |
| `0x001a3930` | `ChaseGauge_IsLoaded` | all three sprites' sheets resident | confirmed (code) |
| `0x001a3990` | `ChaseGauge_Update` | places track, end icon and marker; the marker's colour from its position | confirmed (code) |
| `0x001a3c00` | `ChaseGauge_Render` | draws the three sprites once ready | confirmed (code) |

### `GUI/ChecklistMessageHUD.cpp` {#fn-checklistmessagehud}

`0x001a4690`-`0x001a5d60` (static-init stub): the **checklist** (vtable `0x00539730`, `0x360` bytes), the pause
menu's objective lists ([Objectives](#objectives-hudsetobjective)), holding **items** (markup text widgets, vtable
`0x005397c8`, `0x200` bytes, done flag `+0x1f0`) in a list at `+0x350` (`TextItemContainer`).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001a4690`, `0x001a46d0` | `ChecklistItem_Construct`, `ChecklistItem_Delete` | not done; the delete frees the item's text copy `+0x60` | confirmed (code) |
| `0x001a4748`, `0x001a4770` | `ChecklistItem_Setup`, `ChecklistItem_SetDone` | markup set-up at scale 1.0; `+0x1f0` | confirmed (code) |
| `0x001a47a8` | `ChecklistItem_Render` | slot `+0x38`: **drawn only while not done** | confirmed (code) |
| `0x001a47d0` | `ChecklistItem_SetText` | slot `+0xe8`: the text, and a copy at `+0x60` | confirmed (code) |
| `0x001a4890`, `0x001a48d8` | `Checklist_Construct`, `Checklist_Delete` | single-text widget `+0x160`, lists null | confirmed (code) |
| `0x001a4930` | `Checklist_Setup(list, position, rect, margins)` | from the pause menu (`0x001db550`): the item list `+0x350` and a scratch list `+0x354`, rectangle `+0x30`, margins `+0x40` | confirmed (code) |
| `0x001a4b60`, `0x001a53c0` | `Checklist_Shutdown` (slot `+0x68`), `Checklist_Clear` | free every item (and, on shutdown, both lists) | confirmed (code) |
| `0x001a4bc0` | `Checklist_NextOpenText` | copies the text of the next open item into `+0x54`, cycling `+0x154` over the open count `+0x158`; string `0x109` when there is none | confirmed (code) |
| `0x001a4d20` | `Checklist_Add(list, text)` | no change if an item has this text; else a new item (size 0.1, base size 0.7, colour `0x005fd310`) at the end, open count + 1 | confirmed (code) |
| `0x001a4f68` | `Checklist_Remove(list, text)` | frees the item with this text; open count − 1 unless it was done | confirmed (code) |
| `0x001a5070` | `Checklist_SetDone(list, text, done)` | marks it (open count − 1), then **reorders: done items first, open ones after** | confirmed (code) |
| `0x001a5358` | `Checklist_CountOpen` | items not done | confirmed (code) |
| `0x001a54c0`, `0x001a54e0` | `Checklist_SetSingleText`, `Checklist_SetSingleMode` | the widget `+0x160` and the flag `+0x164` that shows it instead of the items | confirmed (code) |
| `0x001a54e8` | `Markup_MeasureLines(text, lines)` | the longest line in visible characters; each `<CR>` tag adds a line | confirmed (code) |
| `0x001a55c8` | `Checklist_FitItem(list, item)` | whether the item fits the box (its size less twice the margins, less 0.02); counts lines placed (`0x0050d1d8`) and ends the text before the `<CRM>` tag where the box fills | confirmed (code) |
| `0x001a5828` | `Checklist_Update` | slot `+0x30`: open items stacked from the top-left inside the margins, each `0x0050d1e0` below the last, those that do not fit hidden; or the single text | confirmed (code) |
| `0x001a5a08` | `Checklist_MeasureHeight` | the same stacking, returning the bottom y (pause menu `0x001dd4d0`) | confirmed (code) |
| `0x001a5ba8`, `0x001a5bd8` | `Checklist_Render` (slot `+0x38`), `Checklist_SetColour` (slot `+0x78`) | colour `0x005fd310` given to every item (or the single text), then drawn | confirmed (code) |
| `0x001a5d28`, `0x001a5d60` | `HudGrey_StaticInit`, `_StaticInitStub` | static initialiser (ctor list `0x005340f8`): `0x00600688` = `(80, 80, 80, 255)` | confirmed (code) |

### After `GUI/ChecklistMessageHUD.cpp` (no path string): radar arcs, Warrior command display {#fn-after-checklistmessagehud}

`0x001a5d80`-`0x001a8fb8`: the arcs of the radar frames (HUD `+0x177d0`, `+0x18280`), the **Warrior command
display** of the player panel (`+0x1ef0`, vtable `0x005398e0`, opened by L1 + R1, [AI](ai.md#warrior-commands)), the
widget base ([GUI](gui.md#fn-after-checklistmessagehud)) and a small HUD toggle at `+0x15b0`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001a5d80`, `0x001a5dc0` | `RingArc_Set(...)`, `RingArc_Clear` | an arc record: inner and outer radius, start and end angle (degrees), fill, centre, two colours, on | confirmed (code) |
| `0x001a5e38` | `RingArc_Draw` | when on: the shown fill `+0x2c` eases 5 % per call toward `+0x28` (snapping on a jump over 0.5); radii by video mode; a textured ring segment (`Im2D_DrawTexturedRing`) from the start over fill × span | confirmed (code) |
| `0x001a6058` | `WarCommandText_Set(i, text)` | `CfgWarriorCommandText`: a copy in the table `0x006007c0` (six names, then the "locked" texts `0x006007d8`, `0x006007dc`) | confirmed (code) |
| `0x001a6098`, `0x001a6180` | `WarCommandDisplay_Construct`, `_Delete` | markup text `+0x50`, a box sprite `+0x240`, three sets of six sprites (`+0x340`, `+0x940`, `+0xf40`) | confirmed (code) |
| `0x001a62c0` | `WarCommandDisplay_Init(display, player)` | from `PlayerHUD_Init`: layout record `0x006006a0` + player × `0x80`; per command slot an icon (rectangles `0x55`-`0x5b`), a plate `(110, 110, 90)` and a black backing, set in a diamond round the centre | confirmed (code) |
| `0x001a6b80` | `WarCommandDisplay_Shutdown` | slot `+0x68`: the eighteen sprites | confirmed (code) |
| `0x001a6c38` | `WarCommandDisplay_SetAllowed(display, on)` | `+0x155c` (`WCEnableCommand` and friends) | confirmed (code) |
| `0x001a6c58` | `WarCommandDisplay_Issue` | on R2's release ([Warrior command menu](#warrior-command-menu)), once per opening (`+0x1548`): issues the selected command through `0x0041c4e0` and stores it as the player's last (`+0x41c` + player) | confirmed (code) |
| `0x001a6d28` | `WarCommandDisplay_Open` | from `0x002843f8`: for a war chief (`+0x3ac` = 1) shows the display and resets its sprites and timer | confirmed (code) |
| `0x001a7000`, `0x001a7020` | `WarCommandDisplay_ReleaseCamera`, `_IsReady` | `0x0050b1b0[pad]` = 1: the camera's right stick back on ([Warrior command menu](#warrior-command-menu)); the batch is resident | confirmed (code) |
| `0x001a7040` | `WarCommandDisplay_ReadStick` | every `*0x0050d280` ms, with the stick (pad record bytes `+0x2a`/`+0x2b`) more than 110 from centre: the stick's angle picks one of six sectors (with hysteresis `0x0050d2b8`) as the selection `+0x1540`; interface cue `0x20` on a change | confirmed (code) |
| `0x001a77b0`, `0x001a7d10` | `WarCommandDisplay_Place`, `_BlinkSlot` | the slot sprites round the centre; the selected slot blinks by the counter `+0x1578` | confirmed (code) |
| `0x001a7e48` | `WarCommandDisplay_Update` | slot `+0x30`: closed when the player is down; the stick read unless the menu is locked; after an issue the sprites fade and the display closes when its text expires; text = the command's name or the locked text | confirmed (code) |
| `0x001a8530` | `WarCommand_FromSlot(display, slot)` | jump table `0x00553fe0`: display slot 0-5 → command id; 7 otherwise | confirmed (code) |
| `0x001a8590` | `WarCommandDisplay_Render` | slot `+0x38`: the selected slot white / gold `(255, 183, 0)` / grey, the others white / dark `(35, 35, 35)` / black; a disabled command in `(30, 30, 30)` | confirmed (code) |
| `0x001a8988` | `WarCommandDisplay_ShowCurrent` | maps the player's last command to a slot and shows its name; no caller or table reference found (not needed: unreferenced) | confirmed (code) |
| `0x001a8b48`, `0x001a8e10` | `WarCommandLayout_StaticInit`, `_StaticInitStub` | static initialiser (ctor list `0x005340fc`): the layout records `0x006006a0` / `0x00600720` and the colours above | confirmed (code) |
| `0x001a8ed8`, `0x001a8f20`, `0x001a8f30` | `HudToggle_Init`, `_Clear`, `_IsEnabled` | the HUD's `+0x15b0` record: enabled `+0`, shown `+4`, hidden by `HideHud` `+8`, `+0x0c` = 11, a counter `+0x10` | confirmed (code); its role not traced |
| `0x001a8f38`, `0x001a8f48` | `HudToggle_Hide`, `HudToggle_Restore` | `HideHud` / `RestoreHud` ([Showing and hiding](#showing-and-hiding)) | confirmed (code) |
| `0x001a8f78`, `0x001a8f88`, `0x001a8f90` | `HudToggle_Show`, `_Off` (`0x001b2500`), `_Tick` | `+4` on / off; the counter counts down each HUD update | confirmed (code) |

### After `GUI/Credits.cpp` (no path string): the radar frame {#fn-radar-frame}

`0x001aa238`-`0x001aad70`: the **radar frame** (our class name `HudCrimePanel`), one per player at HUD `+0x177d0`
and `+0x18280` (`0xab0` bytes each). It is four arc gauges (`0x001a5d80` set-up, `0x001a5e38` draw, `0x110` bytes
each at `+0x10`, `+0x120`, `+0x230`, `+0x340`) drawn as textured rings around the radar disc, plus a message widget
(`+0x450`, the class at `0x001e6e98`) and a markup text (`+0x890`). The inner pair (radius 54-57.6, one half sweeping
0 → 180°, the other 0 → −180°) shows the **wanted** time left on the player's gang (gang `+0x5e8`), the outer pair
(57.6-61.2) the gang's second timer (`+0x5f0`), each as the fraction of 10 s left (1 above 0.9), eased 5 % per frame
toward it. Wanted is blue `(0x23, 0x53, 0xbc, 0xff)` (`0x006007e0`), the second timer orange `(0xc9, 0x6b, 0x2e,
0xff)` (`0x006007e8`); when only the second timer runs it takes the inner radii. A pair is drawn only above 0.03. The
TU ends at the static-init stub `0x001aad70`, whose initialiser sets these colours; whether it is `Credits.cpp` itself
is not known. Confirmed (code); names ours.

**How an arc is drawn**, confirmed (code) at `0x001a5e38`, `0x0017b8a8`, `0x001aaba8`, `0x001c60b0`,
`0x001b0108` unless marked:

- **No texture.** `RingArc_Draw` sets the ring record's texture (`+0x5c` of the record it hands
  `Im2D_DrawTexturedRing`) to 0, so the helper computes no texture coordinates and ends the strip with no raster: each
  arc is a **flat-colour triangle strip** (primitive 4) of 17 vertex pairs (16 segments, `+0x2c`), both vertices of a
  pair at alpha 255 (`+0xf4`, `+0xf5`), in the arc's colour (`+0x20`, the blue or orange above).
- **The back colour `0x006007f0`** = (180, 0, 0, 0) is stored as each arc's second colour (`+0x24`), but
  `HudCrimePanel_Render` overwrites both colours with the arc's own before every draw and `RingArc_Draw` reads only
  the first: it is never seen.
- **The centre** is the radar disc's own: `HUD_Update` passes the same vector to the disc (radar `+0x2930`) and to
  `HudCrimePanel_SetPosition` (`0x001b015c`; one player (0.49, 1.0, −0.31, 1) from `0x0050d428`, rewritten per video
  mode, [the disc's position](#the-radar-on-screen)), and both draws use the point (x, third, −second) =
  **(x, −0.31, −1)** of the overlay camera's space, so the arcs are **concentric with the disc**. The `(0.5, 1, −0.31,
  1)` of the set-up is replaced on the first update.
- **Radii in pixels.** The drawer's begin (renderer `0x0050cdb4 + 4`, slot `+0x08`) projects that centre to screen
  pixels, and every vertex is the pixel centre plus an offset, as for [the lock-pick dial's
  shapes](#lock-pick-dial-layout) (inferred: the same drawer): vertex = centre + k × (rx × sin a, ry × cos a), with
  k = inner / outer for the first of a pair and 1 for the second. `rx` = outer × fx and `ry` = outer × fy, by device
  flags: `0x01` set and `0x20` clear **(1.1, 1.0)** (the default) or (0.8, 1.0) in 16:9; `0x01` and `0x20` set (0.85,
  1.0); `0x01` clear (0.75, 1.0) or (0.65, 1.15) in 16:9. So in the default mode the blue pair is a band 54-57.6 px
  high and 59.4-63.4 px wide from the centre, the orange 57.6-61.2 / 63.4-67.3 px; the disc's measured soft edge
  (about 55 × 50 px) lies just inside (inferred, from the disc's measured size).
- **Angles**: a starts at the arc's start (0) and steps by (end − start) × shown fill / 16, in radians. With screen y
  down (inferred, the pixel convention), a = 0 is straight **below** the centre; the 0 → 180° arc grows up the
  **right** side and the 0 → −180° arc up the **left**, the pair meeting at the top when full.

**The crime message** (`HudCrimePanel_OnMessage`, `0x001aa720`, from `HUD_SetWanted`), confirmed (code) unless marked:

- The panel's own widget `+0x450` is set up (size `0x0050d380` = 1.0, scale 1.0, colour `0x0050d384` = bytes
  (255, 191, 96, 96), font slot 6) and placed every update at **(0.5, 0.4)** (`0x0050d390`, `0x0050d394`), and `+0x890`
  gets global string `0xe4`, but **nothing draws either**: `HudCrimePanel_Render` draws only the arcs, and no other
  code reaches them (no reference to HUD `+0x17c20` or `+0x186d0`; the boxed-text renderer's five callers are other
  widgets). Inferred: left over.
- What shows is the **copy** in the HUD's centred announcement (`+0xe150`, `MarkupText_SetText(0x0060e990, …)`):
  centred at **(0.5, 0.25)** in the default mode (`HUD_Update` places it each update from `0x0050d470`, 0.35 in one
  other mode; [Announcements](#announcements-and-other-messages)), drawn by `HUD_Render` while the HUD is shown (and
  while hidden when `+0x177a8` is set). Its set-up in `HUD_InitLevel` (`0x001ade48`) uses size 1.0 and colour
  `0x6060bfff` (inferred for the size: the register also stored as the position's w).
- **How long:** nothing in the radar frame clears the copy: clearing the message (−1, 14 once the gang is no longer
  wanted, or any number above 14) shuts only the unseen `+0x450` and `+0x890`. The copy lasts as its markup says
  (`<DISPLAYTIME>`) or until the next centred text replaces it (inferred, as for the announcements; the crime texts
  come from `CfgCrimeMessage` in the scripts and their tags were not read).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001aa238` | `HudCrimePanel_GetMessage` | crime message `i` from the table `0x006007f8` | confirmed (code) |
| `0x001aa250` | `HudCrimePanel_SetMessage` | `CfgCrimeMessage(i, text)` (via `0x0041d9c8`): interns `text` (`0x003864e0`) into `0x006007f8[i]` | confirmed (code) |
| `0x001aa290` | `HudCrimePanel_Setup(panel, player)` | from the HUD level set-up `0x001ad588`: the four arcs at (0.5, 1, −0.31, 1) (moved onto the radar disc's centre each update), back colour `0x006007f0` (never seen, [above](#fn-radar-frame)); text anchor `+0xa80` = (0.37, 0, −0.4, 1); player `+0xa90`; set up `+0xa98` = 1; then hidden (`0x001aa6c0`) and the custom text cleared | confirmed (code) |
| `0x001aa5c0` | `HudCrimePanel_Shutdown` | from `0x001ae980`: hidden, not set up, both texts reset (`0x001e72c0`, `0x001b9290`), arcs off (`+0x30` of each) | confirmed (code) |
| `0x001aa630` | `HudCrimePanel_SetPosition(panel, pos)` | from `HUD_Update`: every arc's centre = (pos.x, pos[1], −0.31, pos.w) | confirmed (code) |
| `0x001aa678` | `HudCrimePanel_SetCustomText` | `+0x00` = the script system's current custom-crime text (its slot `+0xc8`); from `SpawnCustomCrime` | confirmed (code) |
| `0x001aa6b8` | `HudCrimePanel_IsEnabled` | `+0xa98` | confirmed (code) |
| `0x001aa6c0` | `HudCrimePanel_Hide` | `HideHud`: `+0xa94` = 1; when set up, arcs and message off | confirmed (code) |
| `0x001aa6f0` | `HudCrimePanel_Show` | `RestoreHud`: `+0xa94` = 0; when set up, arcs and message on | confirmed (code) |
| `0x001aa720` | `HudCrimePanel_OnMessage(panel, msg)` | from the HUD message `0x001b2520`: `msg` 0-13 (unless the same text is already up) sets the `+0x450` widget up (scale 1.0, font slot 6, colour `0x6060bfff`) with crime message `msg` (message 4: the custom text) and copies it to the HUD's centred text `+0xe150`, the copy that shows ([above](#fn-radar-frame)); `+0x890` gets global string `0xe4`; 14 keeps the text while player 1's gang is wanted; negative or other: cleared; `+0xaa0` = `msg` | confirmed (code) |
| `0x001aa950` | `HudCrimePanel_ClearMessage` | both texts reset, `+0xa9c` = 0 | confirmed (code) |
| `0x001aa9b0` | `HudCrimePanel_Update` | from `HUD_Update`: inner arcs' target `+0x38` / `+0x148` and outer `+0x258` / `+0x368` from the two gang timers (time left × 0.0001, clamped, 1 above 0.9; 0 when the timer is 0); message at (0.5, 0, 0.4, 1) | confirmed (code) |
| `0x001aaba8` | `HudCrimePanel_Render` | from `HUD_Render`, when set up and shown: inner pair blue when either inner target or value is above 0.03; outer pair orange, at the outer radii if the inner pair drew, else at the inner | confirmed (code) |
| `0x001aacf0` | `HudCrimePanel_InitColours` | the TU's static initialiser: the three colours above | confirmed (code) |
| `0x001aad70` | `HudCrimePanel_StaticInitStub` | GCC static-init stub (`a1` = `0xffff`) | confirmed (code) |

### After the stub `0x001aad70` (no path string): item counters and status icons {#fn-hud-counters}

`0x001aad90`-`0x001ab348`: the skeleton-key and handcuff counters of the player panel
([Item counters](#item-counters)) and the **status icons** (HUD `+0x114c0` + player × `0x450`, vtable `0x00539aa8`:
widget base plus four `BaseWidget`s at `+0x50`, `0x100` apart, player `+0x40`). The icons show bits 4-7 of the word
`+0x1c` of the human's `0x00222b18` record as rectangles `0x49`, 6, `0x4f`, `0x41` in a row, from the
per-player record at `0x0050d3d0` + player × `0x20` (`+0x00` position: player 0 (0.17, 0, 0.45), player 1 (0.78, 0,
0.7); `+0x10` colour `0xffffffff`; `+0x14` size 0.08; `+0x18` step 0.04). What those bits mean is not traced.
Confirmed (code); role names inferred.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001aad90` / `0x001aaf28` | `KeyCounter_Setup` / `CuffCounter_Setup` | from `PlayerHUD_Init`: the icon-and-number counter (`HudCounter_Setup`, `0x001b6c50`) at depth 11000 | confirmed (code) |
| `0x001aadf0` | `KeyCounter_GetCount(player)` | inventory count of item 6 (skeleton key; `0x0041e420` on game `+0x480`), clamped 0-9 | confirmed (code) |
| `0x001aaf88` | `CuffCounter_GetCount(player)` | `Human_GetCuffCount` of the player human whose `+0x380` is the slot; 0 if none | confirmed (code) |
| `0x001aae30` / `0x001ab010` | `KeyCounter_Init` / `CuffCounter_Init` | `+0x420` = the count | confirmed (code) |
| `0x001aae58` / `0x001ab038` | `KeyCounter_Update` / `CuffCounter_Update` | from `PlayerHUD_Update`: a changed count resets the panel's fade timer (`+0x411c` = 0, the panel shows); visible when ≥ 1; text `%d` into the `TextWidget` at `+0x40`; relayout `0x001b6f60` | confirmed (code) |
| `0x001ab108` | `StatusIcons_Setup(icons, player)` | from the HUD level set-up: the four icons from the per-player record (size scale 1.0, depth 11000) | confirmed (code) |
| `0x001ab210` | `StatusIcons_Shutdown` | slot `+0x68`: each icon's slot `+0x70` | confirmed (code) |
| `0x001ab270` | `StatusIcons_Render` | slot `+0x38`: when visible, the human exists and now is past its record `+0x20`, renders the icons | confirmed (code) |
| `0x001ab348` | `StatusIcons_Update` | slot `+0x30`: hidden unless the human exists and passes `0x00228588`; bits 4, 5, 6, 7 → rectangles `0x49`, 6, `0x4f`, `0x41` for the next free icon; the used icons placed from the record position, x stepping by `+0x18`; with more than one player camera x becomes x × the record x | confirmed (code) |

### Before `GUI/HUDInterface.cpp`: the HUD object {#fn-before-hudinterface}

`0x001acc28`-`0x001acee0`, just before `HUDInterface.cpp`'s first anchor; the constructor is probably that file's
(inferred). The HUD object at `0x00600840`, part by part, from `HUD_Construct` (confirmed (code); roles named
elsewhere on this page, the rest not traced):

| Offset | Part |
| --- | --- |
| `+0x00`, `+0x04` | per-player words, 0 |
| `+0x08`, `+0x0c` | pointers to the two player panels |
| `+0x10` | a widget (vtable `0x00539370`, set up by `0x0019faf0`) with 8 children at `+0x80` (`0x250` each, vtable `0x00539408`, two `BaseWidget`s each) |
| `+0x13c0` | a markup text widget |
| `+0x15d0`, `+0x3f10` | the two radars (`0x2940` each; 128 blip slots) |
| `+0x6850` | `0x001cd3c8` part |
| `+0x6b40` | the two action prompts (`0x590` each) |
| `+0x7660`, `+0x8960` | `0x001bb5a8` part; the score board (`0x001c25e8`) |
| `+0x8a10` | the hint box |
| `+0x8dd0` | the scroll-in queue |
| `+0x9350` | `HudLabel` (below) |
| `+0x9540`, `+0x9640` | a `BaseWidget`; `0x001a3c48` part |
| `+0x9970` | four `0x001c1cd0` parts (`0x3f0` each) |
| `+0xa940`, `+0xacb0` | `0x001a3660` part; the five counter panels |
| `+0xe050`, `+0xe150`, `+0xe340` | a `BaseWidget`; the centred custom announcement; the announcement |
| `+0xe530` | a widget (vtables `0x0053d440` / `0x0053d418`) |
| `+0xe650`, `+0xea30` | `0x0019dfb0` part; the credits |
| `+0xebc0` | two mash meters (`0x430` each, vtable `0x00539568`: a `BaseWidget`, a bar, three `TextWidget`s) |
| `+0xf420`, `+0xfea0` | two `0x001b7898` parts (`0x540`); two `0x001c9818` parts (`0xb10`) |
| `+0x114c0` | the two status icon sets (`0x450`) |
| `+0x11d60`, `+0x134a0` | `0x001b7478` part; the instruction arrow |
| `+0x135e0`, `+0x137e0` | 2 and 19 `BaseWidget`s |
| `+0x14ae0` | six `0x001cce30` parts (`0x560` each) |
| `+0x16b30` | a widget (vtable `0x00539f38`) with 3 + 9 `BaseWidget`s |
| `+0x177a0`-`+0x177bc` | flags (shown, active, ...) and the per-player hint memory |
| `+0x177d0`, `+0x18280` | the two radar frames |
| `+0x18d30` | `0x001ca898` part |
| `+0x18e60` | a widget (vtables `0x0053abe8` / `0x0053abc0`) with a markup text at `+0x18ee0` |
| `+0x19130` | the two player panels (`0x4a50` each) |
| `+0x225d5`, `+0x225e0` | a byte, 0; the built-in announcement table |

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001acc28` | `HudLabel_Construct` | HUD `+0x9350`: a markup text widget subclass (vtable `0x00539e38`); its use is not traced | confirmed (code) |
| `0x001acc60` | `HudLabel_Setup` | from `0x001ad588`: markup text at position 0, size 0.1, colour `0xff808080` | confirmed (code) |
| `0x001accf8` | `HudLabel_Shutdown` | slot `+0x68`: the markup text's shutdown `0x001b9290` | confirmed (code) |
| `0x001acd20` | `HUD_SetAnnounceText(i, text)` | `CfgAnnounceMessage` (via `Cfg_SetAnnounceMessage`): interns `text` into `0x00622e20[i]` | confirmed (code) |
| `0x001acd60` | `ActionPrompt_FindNearbyHuman` | ([Action prompts](#action-prompts)) | confirmed (code) |
| `0x001acee0` | `HUD_Construct` | constructs the parts above and points `+0x08` / `+0x0c` at the player panels | confirmed (code) |

### `GUI/HUDInterface.cpp` {#fn-hudinterface}

`0x001ad588`-`0x001b3ea8` (the static-init stub): the HUD object's methods. One static object at `0x00600840`
(constructed by `HUD::HUD`, `0x001acee0`, from the TU's initialiser); its parts are in
[The HUD object](#hud-object). Names are ours; `0x001acee0` itself is listed with `GUI/ChecklistMessageHUD.cpp`'s
neighbours.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001ad588` | `HUD_InitLevel` | per-level set-up (from `InitLevel`, `0x0015fe90`): the layout floats for the video mode, then every part of [the HUD object](#hud-object) that is not yet set up, the two player panels (`ANHud` panels allocated in levels 60-69, otherwise the static ones at `+0x19130`), the Armies of the Night widgets (`0x0041d110`); ends hidden (`HUD_Hide`) with `+0x177a4` = `+0x177ac` = `+0x177b4` = 1 | confirmed (code) |
| `0x001ae378` | `HUD_SetupSpinner` | the loading/saving spinner at `+0xe050`: a sprite (size 0.09, sprite word `0x5c` in batch 3, colour (191, 191, 191, 255)) at (0.95, 0.83) in the default mode, rate `+0x19128` = 0.2 (unused: the sprite never turns, [Spinner](#hud-spinner)) | confirmed (code) |
| `0x001ae4d8` | `HUD_EnsureVirtualPads` | allocates the two `0x2c`-byte player input records (tag `VirtualPad`) at HUD `+0x00`/`+0x04` when missing: pad index `+0x19` = -1, bound `+0x1b` = 0 | confirmed (code) |
| `0x001ae5a0` | `HUD_ResetPadBindings` | unbinds both players and sets every [pad record](frontend.md#pad-record)'s owner `+0x42` to -1 (from `PM_Greet`) | confirmed (code) |
| `0x001ae638` | `HUD_AssignPadToPlayer(hud, player)` | [pad binding](#hud-pads) for one player | confirmed (code) |
| `0x001ae828` | `HUD_UpdatePadBindings` | ensures the records, then assigns player 0 and, once player 0 is bound, player 1; called by the menus, the modes and `HUD_Update` | confirmed (code) |
| `0x001ae898` | `HUD_GetVirtualPad(hud, player)` | the player's input record ([Front end](frontend.md#input)) | confirmed (code) |
| `0x001ae8d8` | `HUD_BindPad(hud, player, pad)` | frees the pad records the player owned, then `+0x19` = pad, `+0x1b` = (pad ≥ 0), the pad record's owner = player | confirmed (code) |
| `0x001ae980` | `HUD_ShutdownLevel` | undoes `HUD_InitLevel` (from `0x001607b8`): `+0x177a4` = 0, every part's shutdown, the `ANHud` panels freed, the six menus' `Shutdown` | confirmed (code) |
| `0x001aef50` | `HUD_IsPlayerStickPushed(hud, player)` | 1 unless both raw right-stick bytes `+0x1a`, `+0x1b` of the player's pad record are within 64-176 (stored with the right stick's floats, `0x00149990`; [Fixed-camera icon](#hud-fixed-cam-icon)) | confirmed (code) |
| `0x001af010` | `HUD_Update(hud, a, b)` | the HUD's frame ([The HUD's frame](#the-huds-frame)); `a` and `b` go to the two radars' updates (`0x001c5210`) | confirmed (code) |
| `0x001b1640` | `HUD_IsSplitScreen` | more than one player (game state `+0x224`) and more than one view (`0x0011eae0`) | confirmed (code) |
| `0x001b1688` | `HUD_Render` | the HUD's draw ([The HUD's frame](#the-huds-frame)) | confirmed (code) |
| `0x001b1f38` | `HUD_Hide` | `HideHud`'s work ([Showing and hiding](#showing-and-hiding)); does nothing in an Armies of the Night level while game state `+0x14c` = 1 | confirmed (code) |
| `0x001b2030`, `0x001b2088` | `HUD_HidePlayerPanelsBoth`, `HUD_ShowPlayerPanelsBoth` | each panel's "may show" `+0x4108` = 0 or 1, then hidden or shown | confirmed (code) |
| `0x001b20e0` | `HUD_SetPanelForceShow(hud, slot, on)` | panel `+0x4104` = on (`ForceShowPlayerHud`) | confirmed (code) |
| `0x001b20f8` | `HUD_Restore` | `RestoreHud`'s work ([Showing and hiding](#showing-and-hiding)) | confirmed (code) |
| `0x001b21a8` | `HUD_HideForMenu` | from the game menu's open (`0x001d1aa0`): the panels' hide, the `+0x15b0` widget's reset, both radars' blips off, both action prompts cleared | confirmed (code) |
| `0x001b2200` | `HUD_AttachPlayer(hud, human, slot)` | binds a panel to a human (`Human_MakePlayer`): for slot < 2 and a panel not locked (`+0x40f8`), init once (interface `+0x14`), reset (`+0x24`), bind (`+0x34`); hidden unless the HUD is shown; returns slot or -1 | confirmed (code) |
| `0x001b22f0` | `HUD_DetachPlayer(hud, slot)` | panel interface `+0x2c` (`Human_Destroy`) | confirmed (code) |
| `0x001b2330` | `HUD_ShowPanelsAfterScene` | both panels shown (`0x0020e028`: only if attached and allowed); [Who shows the HUD again](#who-shows-the-hud-again) | confirmed (code) |
| `0x001b2380` | `HUD_ApplyPanelsHide` | both panels hidden (`0x0020e058`) | confirmed (code) |
| `0x001b23d0`, `0x001b2400` | `HUD_ShowPlayerPanel`, `HUD_HidePlayerPanel` | the same for one slot (-1 ignored) | confirmed (code) |
| `0x001b2430`, `0x001b2460` | `HUD_ReportHealth`, `HUD_ReportPower` | call the panel's empty `0x0020dfd0`: not needed, no effect | confirmed (code) |
| `0x001b2490` | `HUD_ClearActionPrompt(hud, player)` | no text: the prompt hides ([Action prompts](#action-prompts)) | confirmed (code) |
| `0x001b24d0` | `HUD_SetSpinner(rate, hud, on)` | `+0x19128` = rate, spinner shown (`+0xe054`, `+0xe058`) = on; also used by the memory-card and preload screens | confirmed (code) |
| `0x001b2500` | `HUD_ResetWidget15b0` | clears the `+0x15b0` widget's `+0x04` | confirmed (code) |
| `0x001b2520` | `HUD_SetWanted(hud, msg, player)` | messages 7-9: the radar's wanted flag `+0x1c` (HUD `+0x15ec`) = 1 (the first time also `+0x30` = 1.0, `+0x34` = 0 and interface cue 2) and the radar frame shows the wanted state (`0x001aa720`, game-state byte `+0x290`); other messages clear both | confirmed (code) |
| `0x001b2610`, `0x001b2658` | `HUD_RadarEnable`, `HUD_RadarDisable` | one player's radar on or off: `+0x177b0`, radar `+0x04`, blips (`0x001c4388` / `0x001c4448`) | confirmed (code) |
| `0x001b26a8`, `0x001b2790` | `HUD_RadarSetTintBlue`, `HUD_RadarSetTintGrey` | radar colour state 1 or 0 (`0x001c4500`; radar 0 in a two-player game with one view); state 1 only while the HUD is shown and the human is in neither state `0x00227dd8` nor `0x00223b70` | confirmed (code) |
| `0x001b28b8`, `0x001b28e8`, `0x001b2918` | `HUD_MugMeterStart`, `_Update`, `_Set` | the player panel's mini-game part (`+0x3480`): `0x001bfcc0`, `0x001bfd30`, `0x001c0640(value)` ([Crimes](crimes.md#mugging)) | confirmed (code) |
| `0x001b2950` | `HUD_StereoTheftSet(hud, player, v)` | `0x001ca210` on the stereo widget | confirmed (code) |
| `0x001b2990` | `HUD_RadarAddBlip(hud, handle, colour, type, layer)` | a blip on both radars (`0x001c4d00`); type 5 adds nothing; type 6 adds two layers with icons 352 and 359 ([GUI](gui.md#radar-icons)) | confirmed (code) |
| `0x001b2b18`, `0x001b2b58` | `HUD_RadarRemoveItem`, `HUD_RadarRemoveHandle` | an item, or every layer of a handle (object byte `+0x6c`, at least 1), off both radars | confirmed (code) |
| `0x001b2bf0`, `0x001b2c40`, `0x001b2ca0`, `0x001b2d80` | `HUD_RadarSetBlipIconLock`, `_SetBlipScale`, `_SetBlipIcon`, `_SetBlipFlash` | the blip setters on both radars; `_SetBlipIconLock` freezes the blip's icon and scale while set; `_SetBlipIcon` draws icon 22 at 0.7 and tints icons 29-31 `0x63db4bff`; `_SetBlipFlash` mode 1 (off) or 2 | confirmed (code) |
| `0x001b2e38`, `0x001b2e68`, `0x001b2e98` | `HUD_RadarAddObjectiveBlip`, `_AddTypedBlip`, `_ChangeBlip` | type 10; a given type; recolour | confirmed (code) |
| `0x001b2ee8` | `HUD_RadarAddPlainHuman` | brain kind 0: type 9, grey `0x787878ff`, icon 362; otherwise type 7, icon 365 | confirmed (code) |
| `0x001b3020` | `HUD_RadarUpdateHumanIcon(hud, handle, kind)` | for a human already on the radar (`Humans_Update`): type 7 or 9 by `+0x1b0`, icon by kind (2: 25, 1: 26, 0: 365 / 363 / 362); with game state `+0x158` set only humans of a player's gang | confirmed (code) |
| `0x001b3258` | `HUD_RadarAddGangHuman` | a type-8 blip, its scale, mode 0 | confirmed (code) |
| `0x001b32e0` | `HUD_RadarSetBlipMode(hud, handle, mode)` | the [blip modes](gui.md#radar-icons) | confirmed (code) |
| `0x001b3610` | `HUD_RadarMarkEnemies(hud, human)` | from `Brain_ScanEnemies`: for the 16 enemies in brain `+0x164`, marks a gang (`0x00165678`) on both radars (`0x001c4be0`) when the scanner or the enemy has brain kind 0 or 3 (never for kind 4) | confirmed (code) |
| `0x001b3838` | `HUD_RadarClearPending` | removes the radar item `+0x177b8` once player 1 has been out of gang state 1 for 90 updates | confirmed (code) |
| `0x001b3878` | `HUD_Nop` | not needed: returns at once | confirmed (code) |
| `0x001b3880` | `HUD_OpenRumbleIntro` | the RM_Intro screen at `+0xe530` ([Front end](frontend.md#rumble-screens)) | confirmed (code) |
| `0x001b38b8`, `0x001b38d8` | `HUD_LoadCredits`, `HUD_StartCredits` | `Credits_Load` / `Credits_Start` on `+0xea30` | confirmed (code) |
| `0x001b38f8` | `HUD_ScoreBoardSignal` | `0x001c28c8` on the score board (from `0x003a4c48`) | confirmed (code) |
| `0x001b3918`, `0x001b3940` | `HUD_QueueScrollIn`, `HUD_FlushScrollIn` | the scroll-in queue's queue and `0x001c94c8` ([Objectives](#objectives-hudsetobjective)) | confirmed (code) |
| `0x001b3960`, `0x001b39a0` | `HUD_TagAddHuman`, `HUD_TagRemoveHuman` | a human's handle into or out of the `+0x10` widget (`0x001a0420`, `0x001a07f0`) | confirmed (code) |
| `0x001b39e0`, `0x001b3b38` | `HUD_LockPickStart(hud, player, difficulty)`, `HUD_LockPickEnd` | the dial: difficulty, three zone flags (difficulty 0: none; 1: the middle one; other: the outer two), values 1, 2, 3, shown / hidden ([Crimes](crimes.md)) | confirmed (code) |
| `0x001b3b68`, `0x001b3ba8`, `0x001b3c78` | `HUD_MashMeterShow`, `_Hide`, `_Update` | the mash meter (`0x001a3310`, `0x001a3338`) | confirmed (code) |
| `0x001b3c00`, `0x001b3c30` | `HUD_StereoTheftStart`, `_End` | the stereo widget's start (`0x001ca118`) and shutdown | confirmed (code) |
| `0x001b3d10`, `0x001b3d28`, `0x001b3d58`, `0x001b3d80`, `0x001b3da8` | `HUD_SetArrowFlag`, `_SetArrowTarget`, `_SetArrowColour`, `_ArrowOn`, `_ArrowOff` | the [instruction arrow](#the-instruction-arrow-hudenableinstarrow)'s setters | confirmed (code) |
| `0x001b3dd0`, `0x001b3df8` | `HUD_ActionCycleStart`, `_Stop` | a prompt's cycle animation | confirmed (code) |
| `0x001b3e20` | `HUD_WithdrawActionHint(hud, player)` | withdraws `+0x177bc[player]` from the hint box | confirmed (code) |
| `0x001b2818` | `HUD_SetFixedCamIconVisible(hud, on)` | `+0x177b4` = on; both fixed-camera icons' slot `+0x40` (the active flag `+0x08`, which gates drawing) | confirmed (code) |
| `0x001b28a0` | `HUD_SetReticuleRequest(hud, player, v)` | byte `+0x225d0[player]` | confirmed (code) |
| `0x001b2e10` | `HudManager_SetRadarRange(r, r2, hud)` | both radars' `+0x20`, `+0x28` = r and `+0x24` = r2 | confirmed (code) |
| `0x001b3cb0` | `HUD_ShowAnnouncement` | [Announcements](#announcements-and-other-messages) | confirmed (code) |
| `0x001b3e78`, `0x001b3ea8` | `HUDInterface_StaticInit`, `HUDInterface_GlobalCtor` | the TU's initialiser (constructs the HUD object) and its global-constructor stub (table `0x00534104`) | confirmed (code) |

### `GUI/HUDLua.cpp`: the bindings' HUD calls (first part) {#fn-hudlua-start}

`0x001b3ec8`-`0x001b4438`: one-line callees of the HUD bindings, each on the HUD object `0x00600840`; they follow
`HUDInterface.cpp`'s stub, so they are `HUDLua.cpp`'s first functions (inferred).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001b3ec8` | `HUD_ShowHudNop` | `ShowHud`: not needed, returns at once | confirmed (code) |
| `0x001b3ed0`, `0x001b3f78` | `HUD_HideAll`, `HUD_ShowAll` | `HideHud`, `RestoreHud`: `HUD_Hide`, `HUD_Restore` | confirmed (code) |
| `0x001b3ef0`, `0x001b3f10`, `0x001b3f30` | `HUD_HidePlayerPanels`, `HUD_ShowPlayerPanels`, `HUD_ForceShowPlayer` | `HidePlayerHud`, `ShowPlayerHud`, `ForceShowPlayerHud` | confirmed (code) |
| `0x001b3f58` | `HUD_FlashRageBar(slot, on)` | panel `+0x4114` = on | confirmed (code) |
| `0x001b3f98`, `0x001b3fc0`, `0x001b3fe8` | `HUD_RadarAddObjective`, `_AddSecondaryObjective`, `_ChangeObjective` | type 10, type 1, recolour with `{r, g, b}` packed as `0xRRGGBBff` | confirmed (code) |
| `0x001b4038`, `0x001b4098` | `HUD_RadarSetIcon`, `HUD_RadarRemove` | icon then colour; remove a handle | confirmed (code) |
| `0x001b40c0`, `0x001b40e0` | `HUD_SetRadarRange`, `HUD_SetRadarZoomScale` | the range; both radars' zoom `+0x2920` (`0x00604730`, `0x00607070`) | confirmed (code) |
| `0x001b40f8` | `HUD_RadarAddObject(handle, rgb)` | a type-1 blip in `0xRRGGBBff` | confirmed (code) |
| `0x001b4168` | `HUD_RadarAddHuman` | the blip by the human's class (byte `+0x11a`; 2 when `+0x19d` is set unless 3): 1 gang member (brain `+0x28d` = 1, two type-8 layers, icon 356), 0 and 3 `HUD_RadarAddPlainHuman`, 4 nothing, others type 6 | confirmed (code) |
| `0x001b4298`, `0x001b42d8`, `0x001b4300` | `HUD_RadarFlash`, `HUD_RadarDeleteItem`, `HUD_RadarDeleteObject` | flash; remove an item; remove a handle | confirmed (code) |
| `0x001b4328`, `0x001b43a8` | `HUD_RadarOn`, `HUD_RadarOff` | `+0x177ac` (`0x00617fec`) = 1 or 0, then one player's radar, or both for 2 ([The radars across a scene](#radars-across-a-scene)) | confirmed (code) |
| `0x001b4438` | `HUD_SetNumIndicator(player, value, n)` | players 0 and 1: the panel's interface `+0x54(n)` and `+0x4c(value, 0 when n is -1)`; player 2: `0x001b6770` on the Rumble widget `0x00617370` | confirmed (code) |

### `GUI/HUDLua.cpp` {#fn-hudlua}

`0x001b44d8`-`0x001b8f78` (the static-init stub, [Source map](source-map.md#gui)): the C++ side of the HUD bindings
(each called from one tolua wrapper; the binding pages have the Lua side), then helper classes the HUD set-up builds:
an effect-task handle, the gang-count indicator (`NumIndicator`, vtable `0x00539f38`, HUD `+0x16b30`), the icon
counter (`HudCounter`, vtable `0x00539fd0`), the instruction arrow (`InstArrow`, a `BaseWidget` subclass, vtable
`0x0053a068`, HUD `+0x134a0`), the mission-item icons (vtable `0x0053a118`) and the lock-pick dial (vtable
`0x0053a1b0`, HUD `+0xf420` + player × `0x540`). The classes after `0x001b6000` have no allocator tag naming the
file; they sit before its stub, so the file is inferred. The bar functions (`HUD_EnableBar`, `0x001b4ba0`-`0x001b5450`)
are [listed with the bars](#scripted-bars).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001b44d8` | `HUD_ANLaunchEndScreen` | on: a `BaseWidget` (`0x100` bytes, kept at `0x0061995c`) set up full screen (size 1.0, depth 11,000, centre (0.5, 0.5), white, sprite word `0x18e0000`), then the HUD hidden (`0x001b1f38`); off: the widget shut down (slot `+0x70`) and freed, the HUD shown (`0x001b20f8`) | confirmed (code) |
| `0x001b4690` | `HUD_ANSetGOSignMode` | byte `0x0060f260` = mode | confirmed (code) |
| `0x001b46a0` | `HUD_ANEnableJoinMsg` | the player panel's interface slot `+0x78` (`0x002107d8`, the join-message flag) | confirmed (code) |
| `0x001b46e0`, `0x001b4700` | `HUD_ANSetCredits`, `HUD_ANGetCredits` | write / read the Armies of the Night credit count `0x00619910` | confirmed (code) |
| `0x001b4720` | `HUD_ANEnableCheckpointIndicator` | stores the switch in `0x00619960`; nothing in this range reads it | confirmed (code) |
| `0x001b4740` | `HUD_EnableFixedCamIcon` | `HUD_SetFixedCamIconVisible(HUD, on)` | confirmed (code) |
| `0x001b4768` | `HUD_ShowMissionSummaryText` | forwards to `0x001b3878`, which returns at once: does nothing | confirmed (code) |
| `0x001b4790`, `0x001b47b8`, `0x001b47e0` | `HUD_PanelAlloc`, `HUD_PanelRelease`, `HUD_PanelSetValue` | the counter panels at `0x0060b4f0` (HUD `+0xacb0`): take one (`0x001c2f90`), free one (`0x001c3088`), set a value (`0x001c3280(panels, id, value)`, the Lua arguments swapped) | confirmed (code) |
| `0x001b4808` | `HUD_PanelSetLabelFlashing` | calls `0x001bb878` on the object at `0x00607ea0` (HUD `+0x7660`), **not** on the counter panels | confirmed (code) |
| `0x001b4838` | `Gamma_Set` | clamps to 255, stores `W_GameState + 0x57a4`, sets the light manager's brightness (`0x0017ec38`) to (v/255, v/255, v/255, 1); also called by `PM_Light` and the options menu ([Brightness](save.md#brightness)) | confirmed (code) |
| `0x001b48d0` | `Gamma_Get` | the light manager's brightness (`0x0017ec78`) × 255, as an integer (`0x0042c718`) | confirmed (code) |
| `0x001b4908` | `LightManager_SetColourOffset` | (r, g, b, 0) to `0x0017ec80` (light manager `+0xa0`) | confirmed (code) |
| `0x001b4948` | `HUD_ShowWarCommand` | `WarCommandDisplay_SetAllowed(player panel + 0x1ef0, on)` | confirmed (code) |
| `0x001b4980`, `0x001b49a8`, `0x001b49d0` | `Tutorial_QueueText`, `Tutorial_Contains`, `Tutorial_Flush` | `HintBox_Queue` / `_Contains` / `_FlushPriority` on the hint box `0x00609250` ([Hints](#hints-hudsettutorialtext)) | confirmed (code) |
| `0x001b49f8`, `0x001b4a28`, `0x001b4a88` | `ChaseHud_SetNormal`, `ChaseHud_SetTimeout`, `ChaseHud_Destroy` | `ChaseHud_SetState(value, 0x00609e80, state, time)`: state 1 (100, 10), state 3 (`value`, negated when the sign argument is 0; then the label, `0x001a4310`), state 6 (100, 10) | confirmed (code) |
| `0x001b4ab8` | `ChaseHud_Create` | only while the chase HUD is not set up (`+0x0c` = 0): set-up `0x001a3d00(size 0.11, depth 11,000, at (0.9, 0.24), or x 0.72 when +0x40 is set, grey 128)` | confirmed (code) |
| `0x001b5590` | `HUD_EnableTextProgress(on, labels, count, flag)` | on: sets up `count` of the six score rows at `0x00615320` (`0x560`-byte markup text widgets, set-up flag `+0x434`) at (0.95, 0.22 + 0.06 × row), alignment 5, the label (`+0x540`) and a score of 0 (`+0x558`); off: shuts down every set-up row. The count goes to `0x00622e40` when `flag` is set, else to `0x00622e44` | confirmed (code) |
| `0x001b57c8` | `HUD_SetTextProgress(label, score, rgb, flag)` | the set-up row whose label matches gets the score and colour and is redrawn; then a selection sort of the first count rows (`0x00622e40` or `0x00622e44` by `flag`), highest score first, swapping labels, scores and colours | confirmed (code) |
| `0x001b5a90`, `0x001b5ae0` | `HUD_TurnOnActionCycleAnim`, `HUD_TurnOffActionCycleAnim` | `0x001b3dd0(0.1, HUD, {a3, a4}, 2, a1, a2, a5)` / `0x001b3df8(HUD, player)`: the action prompt's button-cycle animation ([Action prompts](#action-prompts)) | confirmed (code) |
| `0x001b5b08` | `HUD_SetAnnounceMessage(kind, text, arg)` | kind 5: `MarkupText_SetText` on the custom widget `0x0060e990`, its flags `+0x4` and `+0x8` = 1; other kinds `HUD_ShowAnnouncement`; `arg` to `0x00617fe8` ([Announcements](#announcements-and-other-messages)) | confirmed (code) |
| `0x001b5b80` | `HUD_SetANBossTexture` | both player panels' interface slot `+0x70` with the three arguments | confirmed (code) |
| `0x001b5c10` | `Cfg_SetAnnounceMessage` | forwards to `0x001acd20` | confirmed (code) |
| `0x001b5c30` | `HUD_EnableInstructionArrow(x, y, angle, on)` | enable flag HUD `+0x134a4`; when on, `InstArrow_SetRotation(angle)` and `InstArrow_SetBasePosition((x, 0, y, 1), 1)` ([The instruction arrow](#the-instruction-arrow-hudenableinstarrow)) | confirmed (code) |
| `0x001b5cc8`, `0x001b5cf0` | `HUD_SetInstArrowAnimSpeed`, `HUD_SetInstArrowColour` | `InstArrow_SetBobFrames(frames)`; the arrow's colour (r, g, b, 255) (`0x001a2648`) | confirmed (code) |
| `0x001b5d38`, `0x001b5d78` | `HUD_SetInstArrowScreenPos`, `HUD_SetInstArrowOnObject` | `InstArrow_SetBasePosition((x, 0, y, 1), 1)`; or the object projected to the screen (`0x001c6878` with HUD `+0x15d0`), y + 0.03, converted 0 | confirmed (code) |
| `0x001b5dd8` | `HUD_CenterComponents(centre, n, widths, out)` | lays `n` widths side by side centred on `centre` and writes each item's centre; also `OptionGrid`'s centred rows and `RM_Intro` | confirmed (code) |
| `0x001b5e60` | `HUD_PackComponentsLeft(left, n, widths, out)` | lays them left to right from `left` with no gap and writes each item's **left edge** (`OptionGrid`'s left-packed rows) | confirmed (code) |
| `0x001b5e90` | `Tutorial_SetCallback` | the Lua reference to `0x006095f0` ([Tutorial callback](#tutorial-callback)) | confirmed (code) |
| `0x001b5ea8` | `HUD_SetActionTextHigh(high)` | the action-prompt text's y (`0x00607d74`, copied to `0x006077e4`): 0.86, or 0.125 when high; PAL (device flag 2) 0.78 / 0.2; PAL 16:9 0.8 / 0.15; progressive 16:9 (`0x20` and 4, not PAL) 0.08 either way | confirmed (code) |
| `0x001b5f88`, `0x001b5fb0`, `0x001b5fd0` | `HUD_ShowRumbleModeIntro`, `HUD_PreloadCredits`, `HUD_ShowCredits` | `0x001b3880(HUD, a, b)`, `0x001b38b8(HUD)`, `0x001b38d8(HUD)` | confirmed (code) |
| `0x001b5ff0` | `GameState_SetTutorialText` | byte `W_GameState + 0x56e2` | confirmed (code) |
| `0x001b6000`, `0x001b6010`, `0x001b6078` | `HudEffect_Construct`, `_Delete`, `_Release` | an effect-task handle (used by the generic bars at `+0x70` and the radar): `+0x1c` the task, `+0x14` a PTank owned when `+0x20` is set, `+0x10`. Release kills the task (its slot `+0x48`) and the PTank and clears the fields; the deleting destructor releases, then frees when bit 0 of the flags is set | confirmed (code) |
| `0x001b6100`, `0x001b6120`, `0x001b6140` | `HudEffect_Show`, `_Hide`, `_Exists` | task message `0x29` (show) / `0x2a` (hide) to `+0x1c` (`0x001e9ae8`, `0x001e9b38`; [Particles](particles.md)); true when `+0x1c` is set | confirmed (code) |
| `0x001b6168` | `NumIndicator_ApplyLayout` | the [gang-count indicator](#gang-count-indicator)'s layout globals per video mode; German (`W_GameState + 0x120` = 4) adds 0.05 to the origin's x and 0.07 to the header gap | confirmed (code) |
| `0x001b6358` | `NumIndicator_Setup(ind, position, header)` | from the HUD's set-up (`0x001ad588`): the header sprite when given, two black shadows, nine tally sprites; count 9, gang −1, off | confirmed (code) |
| `0x001b66e0`, `0x001b6ba0` | `NumIndicator_Shutdown`, `NumIndicator_Render` | slot `+0x68`: shuts the header (when made) and the nine tally sprites down (slot `+0x70`); slot `+0x38`: when shown, the shadows, the header and `count` tally sprites | confirmed (code) |
| `0x001b6770` | `NumIndicator_Set(ind, on, gang)` | `HUDSetNumIndicator` for player 2 (`0x001b4438`; players 0 and 1 go to their panel's slots `+0x50` and `+0x48`): gang `+0xc60`, on `+0x04`, count `+0xc40` = the gang's living members (`0x00166158`) | confirmed (code) |
| `0x001b67b8` | `NumIndicator_Update` | slot `+0x30`: when shown, recounts and lays out the tally marks ([below](#gang-count-indicator)) | confirmed (code) |
| `0x001b6c50` | `HudCounter_Setup(iconSize, iconDepth, c, position, textOffset, iconColour, textColour, metrics, sprite, instance, textFlags, anchor, fontSlot, visible, active, markup)` | an icon (`BaseWidget` `+0x300`) and a text: a `TextWidget` at `+0x40`, or with `markup` a markup text widget at `+0x110` (`+0x410` = markup); text at position + `*textOffset` (`+0x400`); used by the item counters ([Item counters](#item-counters)) | confirmed (code) |
| `0x001b6df0`, `0x001b6e40` | `HudCounter_Shutdown`, `HudCounter_SetPosition` | slot `+0x68` (shared with five subclass vtables): shuts the text and the icon down, `+0x0c` = 0; slot `+0x08`: text at pos + `+0x400`, icon at pos, pos to `+0x20` | confirmed (code) |
| `0x001b6ed8`, `0x001b6f60` | `HudCounter_Render`, `HudCounter_Update` | slot `+0x38`: when visible and shown, the icon then the text; slot `+0x30`: the text then the icon | confirmed (code) |
| `0x001b6fb0`, `0x001b70c0` | `InstArrow_Construct`, `InstArrow_Destroy` | a `BaseWidget` with bob frames `+0x100` = 10, step `+0x104` = 2.0, counter `+0x108` = 0, bob direction `+0x10c`/`+0x110`; the destructor is `BaseWidget`'s (`0x001a1c30`) | confirmed (code) |
| `0x001b70e8`, `0x001b71d8` | `InstArrow_SetRotation`, `InstArrow_SetBobFrames` | slot `+0x90`: rotation `+0xa0` = angle and direction (sin a, −cos a) / 20 / frames; frames `+0x100` and the direction again | confirmed (code) |
| `0x001b72d0` | `InstArrow_SetBasePosition(arrow, pos, convert)` | slot `+0x08`: base `+0x120` = pos (through `0x00122d48` when `convert`), the widget's position, `+0x130` = `convert` | confirmed (code) |
| `0x001b7330` | `InstArrow_Update` | slot `+0x30`: the bob ([The instruction arrow](#the-instruction-arrow-hudenableinstarrow)) | confirmed (code) |
| `0x001b7478`, `0x001b74f0`, `0x001b7580` | `MissionItemIcons_Construct`, `_Destroy`, `_HideAll` | a widget of 23 `BaseWidget`s at `+0x40`, one per inventory item; hide all = slots `+0x50` and `+0x40` with 0 | confirmed (code) |
| `0x001b75f0` | `MissionItemIcons_ResetPosition` | `+0x20` = (0.34, 0, 0.13, 1) (`0x0050d640`) | confirmed (code) |
| `0x001b7608` | `MissionItemIcons_Update` | for each item id whose entry in the table `0x005547d8` is set (only 7, the pass: sprite word `0x48`; 8, the bolt cutters: `0x44`) and that player 1 holds (`0x0041e370(W_GameState + 0x480, id)`): a grey (191) sprite, size 0.05, in a row from (0.34, 0.13) every 0.03; the rest hidden | confirmed (code) |
| `0x001b7890` | `MissionItemIcons_Render` | slot `+0x38`: empty, so the icons are updated but never drawn | confirmed (code); never drawn inferred |
| `0x001b7898`, `0x001b7958` | `LockPickDial_Construct`, `_Destroy` | three pin sprites (`+0x40`, `+0x140`, `+0x240`) and a fourth (`+0x340`), difficulty 1, speeds `+0x458`.. = 1, 2, 3 | confirmed (code) |
| `0x001b79f8`, `0x001b7b68` | `LockPickDial_SetSpeeds`, `LockPickDial_SetDirections` | three speeds to `+0x458`-`+0x460`; three direction bytes to `+0x464`-`+0x466` (non-zero turns backwards) | confirmed (code) |
| `0x001b7a18` | `LockPickDial_SetDifficulty` | the band globals for difficulty 0-2 ([Lock picking](crimes.md#lockpick)): arc sizes `0x0050d6b8` (35, 27, 20) and `0x0050d6bc` (14, 12, 9.2), good bounds `0x0050d6c4`/`c8`, perfect `0x0050d6cc` (less 0.2) / `d0` | confirmed (code) |
| `0x001b7b88` | `LockPickDial_CreateCentreInstance` | a sprite batch over sheet record 10 at depth 13,000 (`0x0018aec0`); id `+0x46c` | confirmed (code) |
| `0x001b7eb0`, `0x001b7c10` | `LockPickDial_Init(dial, player)`, `LockPickDial_ApplyVideoMode` | from the HUD's set-up / the options' video change (`0x001d8258`): the player's layout entry (`0x0050d670` + player × `0x20`, [below](#lock-pick-dial-layout)) for the video mode, the three pins (sheet 10 rectangle 11, grey 191, rotation π, sizes 0.19 × (1 − 0.2 i)), the centre batch, the disc record, player `+0x474`, hidden | confirmed (code) |
| `0x001b8278` | `LockPickDial_Shutdown` | slot `+0x68`: shuts the pins down, releases the centre batch, `+0x478` = `+0x0c` = 0 | confirmed (code) |
| `0x001b8320` | `LockPickDial_IsResident` | true once the pins' and the centre's sheets are resident (`0x001a8ea8`); then rectangle `0xc` of the centre sheet gives the disc's UV centre (`+0x4cc`) and radius (`+0x490`, `+0x494`) | confirmed (code) |
| `0x001b8428` | `LockPickDial_Show(dial, on)` | on: pins back to π, the loop `vags/misc/lock_spins_01` started (handle `+0x450`), `vags/misc/click_open1` prepared (`0x00622e48`), pin 0, counters 0; off: both stopped; `+0x478` = on | confirmed (code) |
| `0x001b8530` | `LockPickDial_Draw` | slot `+0x30` (the update): waits for residency; turns the current pin by 0.1 × its speed (wrapping in 0-2π); queues two band arcs and the textured centre disc (`0x0017c2a8`, the 2D shape queue `0x005fd9f0`, at most 6) at the player's layout ([Lock picking](crimes.md#lockpick)) | confirmed (code) |
| `0x001b8d38` | `LockPickDial_Judge` | 2 perfect (next pin, `click_01` and `click_open1`), 1 good (next pin, `click_open1`), 0 miss (`click_01`, every pin back to π, pin 0) | confirmed (code) |
| `0x001b8ed0` | `LockPickDial_Render` | slot `+0x38`: while resident, shown and a pin is left (`+0x468` < 3), the three pins | confirmed (code) |
| `0x001b8f50`, `0x001b8f78` | `HUDLua_StaticInit`, `HUDLua_GlobalCtor` | the file's static initialiser (`0x00622e48` = −1) and its global-constructor stub (in the constructor list at `0x00534108`) | confirmed (code) |

#### The gang-count indicator {#gang-count-indicator}

`HUDSetNumIndicator`'s shared indicator (HUD `+0x16b30`) counts a gang's living members as **tally marks**: four
strokes and a fifth crossing them. Confirmed (code) at `0x001b6358`, `0x001b67b8`, `0x001b6168`; values in the
default mode.

| Offset | Field |
| --- | --- |
| `+0x04` | on |
| `+0x40`, `+0x140`, `+0x240` | header sprite and its two shadows (`BaseWidget`s) |
| `+0x340` | nine tally sprites (`0x100` bytes each) |
| `+0xc40` | marks to show |
| `+0xc50` | origin (from `0x0050d5f0`) |
| `+0xc60` | gang id (−1 none) |

- **Header** (always set up: `HUD_InitLevel` passes sprite word `0x20e0000`, sheet-table record `0x20e` rectangle 0;
  French `0x20f0000`, German `0x2100000`): blue (35, 83, 188), depth 11,000; shadows in black at (+0.0035, +0.005)
  and (−0.002, −0.003), depth 8,000.
- **Marks**: `part_page0` rectangle 366 (`0x16e`, a short upright stroke, 7 × 14 texels) for the strokes and 367
  (`0x16f`, a long flat bar, 37 × 6 texels) for every fifth, blue, all rotated **3.3 rad** (about 189°, so tilted
  about 9° off their art). Size (an overlay height; the width follows from the rectangle's aspect): 0.04 × 1.2 =
  **0.048** for a stroke, 0.017 × 1.2 = **0.0204** for a crossing bar (`0x0050d5c0`, `0x0050d5c4` × `0x0050d604`).
- **Where mark *i* goes** (0-based, *i* < the living count), in GUI units, `NumIndicator_Update` (`0x001b67b8`) with
  the default mode's layout (`NumIndicator_ApplyLayout`, `0x001b6168`: origin (0.09, 0.94) `0x0050d5f0`/`0x0050d5f8`,
  header shift 0.08 `0x0050d600`):

  ```text
  stroke, i < 5     x = 0.09 - 0.095 + 0.015 i      y = 0.94 + 0.104
  crossing bar      x = 0.09 - 0.095 + 0.005 i      (i = 4: x = 0.015, centred on the four strokes before it)
  stroke, i > 4     x = 0.09 - 0.100 + 0.015 i
  with the header   every mark  + (0.08 + 0.05, -0.1)
  ```

  | *i* | 0 | 1 | 2 | 3 | 4 (bar) | 5 | 6 | 7 | 8 |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | x, no header | −0.005 | 0.010 | 0.025 | 0.040 | 0.015 | 0.065 | 0.080 | 0.095 | 0.110 |
  | x, header | 0.125 | 0.140 | 0.155 | 0.170 | 0.145 | 0.195 | 0.210 | 0.225 | 0.240 |

  y is 1.044 without the header and **0.944** with it. The header counts as present when its batch instance
  (`+0x104`) is not −1 and its `+0x4c` is 0, which the always-made header satisfies (inferred), so the **header row
  is the one seen**: the header at the first mark's no-header place plus (0.065, −0.1) = **(0.06, 0.944)**, size
  0.04 × 1.2 = 0.048 (`0x0050d608` × `0x0050d604`), and the marks from x 0.125. German moves the origin 0.05 right
  and the header shift to 0.15. Confirmed (code).
- Only nine mark sprites exist (`+0x340`), but the loop lays out as many as the gang has living members; a tenth
  would be the second crossing bar at x −0.05 + origin (back over the first group) and would write past the sprites
  (inferred; the Rumble gangs that use it are not checked for size).
- **Who calls it** (disc scan of the compiled scripts): only Rumble, `brawl.lua` and `royal.lua`, each in two branches
  with `Rumble.gang1ID` and `Rumble.gang2ID`: `brawl.lua` player 0 with gang 1 and then either **2** (this shared
  indicator) or **1** with gang 2; `royal.lua` 0 and 1, or 2, with gang 2. Which branch is two-player is not traced
  (inferred: player 2 when there is one human). `HUD_Render` draws the shared indicator only in a Rumble level
  (`GameState_IsRumbleLevel`, `0x001b1688`).
- **Players 0 and 1** (`HUD_SetNumIndicator`, `0x001b4438`) go to their **panel's own tally** instead: interface slot
  `+0x50` stores the gang at panel `+0x4140` (`0x004eca18`) and slot `+0x48` the "on" at `+0x4130` (`0x004eca10`);
  in an `ANHud` panel (levels 60-69) both slots are empty functions. The panel's update (`PlayerHUD_RefreshTallyMarks`,
  `0x00214bc8`) counts the gang's living members into `+0x413c` and lays out the same pattern of strokes and bars
  (`PlayerHUD_LayoutTallyMarks`, `0x00213e68`, the nine sprites at `+0x4150`, in the player's colour) from the panel's
  origin (`+0x40e0`, the [panel base](#the-player-panel-layout-0x0050fa10) by panel layout `+0x40f4`) with the panel
  layout table's entries. No header is drawn there. See [the panel's tally](#panel-tally) for the numbers.

#### The panel's tally (players 0 and 1) {#panel-tally}

`PlayerHUD_LayoutTallyMarks(panel, shifted)` (`0x00213e68`) lays out mark *i* (0-based, *i* < the living count
`+0x413c`) from the panel table `T` = `0x0050fa40` + layout × `0x240` (the table of [the panel
layout](#the-player-panel-layout-0x0050fa10)). Values read from the executable (the default mode; other modes
rewrite the table, `0x00211ef8`); confirmed (code) at `0x00213e68`, `0x00213770`, `0x00214138`:

| Table entry | Meaning | Player 0 | Player 1 |
| --- | --- | --- | --- |
| `+0x1f0` | x of the first stroke (and of every crossing bar's base) | −0.095 | −0.006 |
| `+0x1f8` | y of every mark | 0.104 | 0.104 |
| `+0x200` | x base of the strokes after the first bar (*i* > 4) | −0.10 | −0.01 |
| `+0x210` | a stroke's size (an overlay height) | 0.04 | 0.04 |
| `+0x214` | a crossing bar's size | 0.017 | 0.017 |
| `+0x220` | a stroke's step per mark | 0.015 | 0.015 |
| `+0x230` | a crossing bar's step per mark | 0.005 | 0.005 |

```text
stroke, i < 5     x = base.x + T+0x1f0 + T+0x220 × i
crossing bar      x = base.x + T+0x1f0 + T+0x230 × i      (i % 5 = 4)
stroke, i > 4     x = base.x + T+0x200 + T+0x220 × i
every mark        y = base.y + T+0x1f8 + shift
```

Every mark is turned by `0x005100a8` = **3.3 rad**, as the [gang-count indicator](#gang-count-indicator)'s, and its
size is the table's times `0x00510050` = 1.0. With the bases (0.10, 0.05) and (0.785, 0.05) the strokes of player 0
start at x 0.005 (bar at 0.025, the sixth stroke at 0.075) and player 1's at 0.779 (bar 0.799, sixth 0.85).

**The shift** moves the tally below the money and the item counters:

- `shifted` is 0 → no shift. `PlayerHUD_Update` (`0x00214138`, each update while the tally is on, `+0x4130`) passes
  what `PlayerHUD_UpdateCounters` (`0x00213770`) returned: **1** when the money (`+0x444`) is 1-999 or any item
  counter is shown, else 0 (money 0 or less, or 1,000 or more, with no counter).
- `shifted` is 1 → **0.09** (`0x005100a4`) when slot 2 or slot 3 of the [counter slots](#item-counters) sits on line 2
  (its y equals 0.146) and holds an item (its `+0x20` is not 4); otherwise **0.045** (`0x005100a0`).
- In a level numbered 100 or more (`0x0041d160`) a further **−0.03** (`0x005100c4`), whatever `shifted` is.

So player 0's marks sit at y 0.154 with nothing above them, 0.199 under one row of counters and 0.244 under two.

**Who turns it on:** `HUD_SetNumIndicator` (`0x001b4438`) through the interface slots `+0x48` (on, `+0x4130`) and `+0x50`
(gang, `+0x4140`). The interface's slot `+0x60` (`PlayerHUD_RefreshTallyMarks`, `0x00214bc8`, which lays out with
`shifted` 0) and slot `+0x68` (`0x00214c28`) have no caller found (no direct reference; inferred unused). The marks
are drawn by the panel's own render (`PlayerHUD_Render`, `0x00213290`) at the panel's fade alpha, while the tally is
on. Confirmed (code).

#### The lock-pick dial layout {#lock-pick-dial-layout}

Per player (`0x0050d670` + player × `0x20`): the pins' GUI position, player 1 (0.09, 0.59) and player 2 (0.9, 0.59),
and the shapes' centre, (−0.531, −0.075) and (0.519, −0.075). **In the default video mode** (device flag `0x01`
only) `LockPickDial_Draw` replaces that centre every update with **(−0.565, −0.07)** for player 1 and **(0.546,
−0.07)** for player 2. Confirmed (code) at `0x001b7eb0`, `0x001b8530`; the other video modes rewrite the entry
(`0x001b7c10`) and the radius factors `0x0050d6e8`-`0x0050d6fc` (all 1.0 in the default mode).

**The pins**: three `BaseWidget`s, `hud_minigames` rectangle 11 (a brass lock cylinder), grey 191, at the GUI
position, sizes 0.19, 0.152 and 0.114 (0.19 × (1 − 0.2 i), square in the default mode), depths 8,000, 9,000, 10,000,
all on one centre. Each turns about its own centre (the widget's position, anchor 0). Only the current pin turns:
its angle (`+0x454`, radians, starting at π) changes by **0.1 × its speed** per update (speeds 1, 2, 3 by default),
up or down by its direction byte, wrapped into 0-2π; positive angles turn clockwise on screen
([The stereo panel](#stereo-layout)). Drawn while the dial is shown and a pin is left. No text.

**The shapes** (`Shape2D_Queue` → `Shape2D_DrawQueued` `0x0017c308`, drawn in the viewport pass with RwIm2D):
the centre is the point (x, y, −1) in the **overlay camera's** space, projected to screen pixels
(`Im2DDrawer_Begin` `0x001964c0` → `Camera_ProjectToScreen` `0x00198460`); every rim vertex is then that pixel
centre + r × (sin a, cos a), with **r in screen pixels** and a stepping by −2π / 32 (32 segments, `+0x4ac`). With the
overlay camera's view window (0.725 × 0.5 at distance 1, [Graphics](graphics.md#2d-drawing)), the default-mode centre
lands at **screen** (0.110, 0.570) for player 1, pixel (70.6, 255.4) of 640 × 448: the dial stores the centre as
(x, y, −1, 1) at shape `+0x00` (`0x001b8530`, the `−1` written at dial `+0x488`), and the overlay camera's
`viewMatrix` read at run time (camera `+0x20`: right (0.6897, 0, 0), up (0, −1, 0), at (−0.5, −0.5, −1), pos 0;
PCSX2 2.9.94, `level99`) gives screen x = 0.6897 x + 0.5 and y = 0.5 − y at that depth, confirmed (runtime). The pins,
sprites at depth 1.1, land at screen (0.133, 0.582), pixel (85.0, 260.6). So the wedges' and the disc's centre is
**14 pixels left of and 5 above** the pins' centre, as the code stands; that the original shows this offset on
screen was not seen (no capture of the dial).

**The disc's texture** (`LockPickDial_IsResident`, `0x001b83c0`-`0x001b8404`, confirmed (code)): the u-v centre
(`+0x4cc`) is rectangle 12's centre and the u radius (`+0x490`, `+0x494`) half its width, with v scaled by the texture's
width ÷ height (`+0x4d8`), so the circle inscribed in the whole rectangle is stretched over the 41-pixel disc. How
much of that circle the lock-face art fills decides how big the face looks, and with it how much of the wedges it
hides.

| Shape | What | Radius (px) | Colour (0-255) | Extent |
| --- | --- | --- | --- | --- |
| Band 1 | an untextured **wedge** (a fan from the centre) | 40 (`0x0050d6c0`) | (191, 16, 16), alpha 100 (`0x0050d6b4`) | `0x0050d6b8` per cent of the circle (35, 27 or 20 by difficulty) |
| Band 2 | the same, drawn over band 1 | 40 | (50, 7, 7), alpha 100 | `0x0050d6bc` per cent (14, 12 or 9.2) |
| Centre disc | a textured disc, the whole circle | 41 (`0x0050d6b0`) | (191, 191, 191, 255) | `hud_minigames` rectangle 12 (the lock face with its keyhole), mapped by the rectangle's centre and radius, turned by π/2 |

Both wedges are centred on angle π, which with y down on screen is **straight up** from the centre (12 o'clock); the
segments outside the wedge are drawn with a zero colour, so only the wedge shows (whole segments, below). Which band is
the good zone and which the perfect one follows from the sizes (band 1 the larger good zone, band 2 the perfect one;
inferred, the judge's bounds are on [Crimes: lock picking](crimes.md#lockpick)). The disc is queued last, over the
wedges; where its texture is transparent (outside the lock-face art) the wedges show through (inferred from the draw
order; how much the art covers is above).

**Draw order, frame and extent** (confirmed (code) unless marked):

- **Order.** The shapes are drawn **after every sprite batch**: `GameMode_DrawOverlays` (`0x00156658`) renders the HUD's
  sprites (`HUD_Render`, then `ResourceMgr_RenderOverlay`), the captions, the intro and the credits, and only then
  `Shape2D_DrawQueued`. So the wedges and the disc lie **on top of** the pins whatever the pins' depths (8,000-10,000
  only order the sprite batches among themselves), and within the shapes the queue order holds: band 1, band 2, then the
  disc. Inferred from the art: the disc's lock face hides the pins' middle, and the pins show around it and through its
  transparent parts.
- **Frame.** The pixels are the device's screen size, read when the centre is projected (`Camera_ProjectToScreen`
  `0x00198460` scales by device slots `+0xa8` / `+0xb0`): **640 × 448** in the default mode (confirmed (runtime), PCSX2
  2.9.94, `level99`: `+0x44c` / `+0x450` = 640 / 448). The radii 40 and 41 are in that frame, not scaled with the
  overlay, so a port should scale them by its height ÷ 448.
- **Extent.** `Im2D_DrawArc` (`0x0017bec8`) colours whole rim vertices: with n = 32 segments and p the per cent, k =
  floor(n × p / 100 / 2); rim vertex j (0 to n, starting at the wedge's middle) gets the colour when j ≤ k, and, with
  the mirror flag `+0xb8` the dial sets, also when n − j ≤ k; every other rim vertex gets colour 0 (alpha 0), while the
  fan's centre vertex keeps the colour. With p = 0 (k = 0) nothing is coloured. So the solid part is 2k whole segments
  (k × 11.25° each side), and the next segment on each side **fades** from the colour to clear across its triangle
  (vertex colours interpolate). Example: 35 % gives k = 5, so 112.5° solid plus a fading 11.25° on each side, not the
  exact 126°.

### `GUI/HUDLua.cpp`: the scripted bars {#fn-hudlua-bars}

The bar functions of `HUDEnableBar`, `HUDSetBarPercentage` and `HUDSetBarProperty`, [Scripted bars](#scripted-bars).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001b4ba0` | `HUD_EnableRedBar` | kind 0: generic bar slot 0, red (200, 30, 30), 0.17 × 0.025 (`0x001c1da0`) | confirmed (code) |
| `0x001b4c78` | `HUD_SetRedBarFill` | kind 0's fill | confirmed (code) |
| `0x001b4c98` | `HUD_EnableLabelledBar` | kind 1 (and each kind-3 bar): a labelled generic bar | confirmed (code) |
| `0x001b4ed0` | `HUD_SetLabelledBarFill` | kind 1's fill in a slot | confirmed (code) |
| `0x001b4f00` | `HUD_EnableGaugeBar` | kind 2: the chase gauge on or off | confirmed (code) |
| `0x001b4fc0` | `HUD_SetGaugeValues` | the gauge's value, max and min | confirmed (code) |
| `0x001b5008` | `HUD_EnableBarStack` | kind 3: up to four labelled bars, or one shown or hidden | confirmed (code) |
| `0x001b5380` | `HUD_SetBarStackFill` | kind 3's fill in a slot | confirmed (code) |
| `0x001b53b0` | `HUD_EnableBar` | `HUDEnableBar`: dispatch by kind | confirmed (code) |
| `0x001b5450` | `HUD_SetBarPercentage` | `HUDSetBarPercentage`: dispatch by kind; kinds 1 and 2 also set the gauge | confirmed (code) |
| `0x001b54f0` | `HUD_SetBarProperty` | `HUDSetBarProperty`: fill colour, gradient flag, width | confirmed (code) |

### After `GUI/MessageHUD.cpp` (no path string): the status column {#fn-after-messagehud}

`0x001bb5a8`-`0x001bbbb0` lie between `MessageHUD.cpp` and `MissionSelectHUD.cpp`; the `StatusItem` class they use
(`0x001c3a00`-`0x001c3dc0`) is in the unnamed unit after `MissionSelectHUD.cpp`. The column is one object at HUD
`+0x7660` (`0x00607ea0`, vtable `0x0053a348`): four `StatusItem`s (`0x430` bytes each, vtable `0x0053a7c0`, a
`HudCounter`: a text at `+0x40`, a markup text at `+0x110`, an icon at `+0x300`; value `+0x420`, maximum `+0x424`,
flashing `+0x428`, kind `+0x42c`) and two arrow sprites. `HUDSetPHLabelFlashing` is the only binding found that
reaches it; what fills the items is not traced.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001bb5a8` | `HudStatusColumn_Construct` | the four items at `+0x40` (`0x430` apart) and two `BaseWidget`s at `+0x1100` | confirmed (code) |
| `0x001bb650` | `HudStatusColumn_Init` | from `HUD_InitLevel`: position (0.93, 0.2), items hidden, the arrows set up: size 0.1, depth 11,000, grey 128, `part_page0` rectangle 370 (`0x172`) in batch 3 | confirmed (code) |
| `0x001bb7c0` | `HudStatusColumn_Shutdown` | from `HUD_ShutdownLevel`: shuts down and hides the items, releases the arrows | confirmed (code) |
| `0x001bb878` | `HudStatusColumn_SetFlashing` | `HUDSetPHLabelFlashing(item, on)`: the item's `+0x428` | confirmed (code) |
| `0x001bb8a8` | `HudStatusColumn_Render` | renders the visible items; beside each flashing one the two arrows move between the offsets at `0x0050e730` and `0x0050e750` (one per arrow) on a triangle wave of 300 ms (`0x0050e774`) | confirmed (code) |
| `0x001bbbb0` | `HudStatusColumn_Update` | lays the visible items out from (0.93, 0.205): kinds 1 and 2 first, 0.17 apart leftwards, icon 0.09, text colour (15, 15, 15); then the others 0.082 apart, icon 0.07, on the next row (+0.1); the start moves by (FOV factor − 1) / 2 when the player camera's slot `+0x1fc` × 0.75 is not 1, and by the device's aspect slot `+0xc4` | confirmed (code) |
| `0x001c3a00`, `0x001c3a60` | `StatusItem_Construct`, `StatusItem_Destroy` | constructor and destructor | confirmed (code) |
| `0x001c3ae0` | `StatusItem_SetFlashing` | `+0x428` | confirmed (code) |
| `0x001c3ae8` | `StatusItem_Render` | `HudCounter_Render` | confirmed (code) |
| `0x001c3b08` | `StatusItem_Update` | the text by kind: 1 `"%d"` of the value, 2 `"%d/%d"`, 0 and 3 none; the icon's colour from value / maximum (maximum 0 counts as 100): above 0.7 from grey (128, 128, 128) down to (128, 128, 0), 0.3-0.7 down to (128, 0, 0), below 0.3 down to (48, 16, 16) | confirmed (code) |
| `0x001c3d38` | `StatusItem_InitColours` | static initialiser of those four colours (`0x00622fb0`-`0x00622fc8`) | confirmed (code) |
| `0x001c3dc0` | `HudItems_StaticInit` | the unit's static-init stub (constructor table `0x00534110`) | confirmed (code); end of the unit inferred |

### `GUI/MissionSelectHUD.cpp`: the money counter and the mug meter {#fn-missionselecthud-hud}

The rest of `MissionSelectHUD.cpp` after the mission select ([Front end](frontend.md#fn-missionselecthud)), up to
its static-init stub `0x001c1688`: the player panel's money counter (`HudMoney`, vtable `0x0053a4b8`, panel
`+0x400`, [Score and money](#score-and-money)) and the mugging mini-game panel (`MugMeter`, panel `+0x3480`). The
mug meter's layout is a table of `0x90` bytes per player at `0x00622e50`, rebuilt from the base point
(`0x0050e8f4`, `0x0050e8f8`) and the video mode on every update.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001be970` | `HudMoney_Construct` | a `HudCounter` at `+0x60` and the floating `±$N` text at `+0x480` (also used by the Armies of the Night HUD) | confirmed (code) |
| `0x001bea00` | `HudMoney_Setup` | the counter in (99, 219, 75) at depth 11,000; the floating text in (190, 192, 166) at the per-player offset `0x0050e8c0` | confirmed (code) |
| `0x001bebe8` | `HudMoney_ReadValue` | the player's inventory item 2, clamped to -99..9999 | confirmed (code) |
| `0x001bec38` | `HudMoney_Reset` | shown value = money now, no floating text, the counting sound stopped | confirmed (code) |
| `0x001becb0` | `HudMoney_Update` | [Score and money](#score-and-money): the `±$N` line for 1,000 ms, counting by (difference / 16) ± 1 per frame with cue `0x10`, grey leading zeros (`Hud_GreyLeadingZeros`) | confirmed (code) |
| `0x001bf068` | `HudMoney_Render` | the counter while the shown value is above 0 | confirmed (code) |
| `0x001bf090` | `MugMeter_ApplyLayout` | rebuilds the layout table for the current video mode | confirmed (code) |
| `0x001bf7e0` | `MugMeter_Setup` | two dark (32, 32, 32) bars (sprite word `0x30036`: batch 3 (`part_page0`) rectangles 54-57), two `hud_minigames` sprites (rectangles 5 and 6, grey 128, batch 11), three arrows (rectangles 7-9, (225, 186, 65)), the prompt (string `0x180`) | confirmed (code) |
| `0x001bfcc0` | `MugMeter_SetFills` | both bars' fills, clamped to 0-1 | confirmed (code) |
| `0x001bfd30` | `MugMeter_SetStick` | the stick dot moves by 0.01 × stick from its rest point; a stick beyond 0.5 in one of eight directions (and within 0.1 on the other axis for the four straight ones) picks the arrows' position and angle from `0x00622f70` and `0x0050e924`-`0x0050e940` | confirmed (code) |
| `0x001c0640` | `MugMeter_SetArrowsOn` | `HUDMugMeterSet`: `+0x84c` | confirmed (code) |
| `0x001c0648` | `MugMeter_SetMode` | the colours by mode: 0 and 1 swap red (170, 43, 43) and amber (128, 100, 0) between the two bars and the arrows; 2 and 3 do the same with blue (35, 83, 188); the prompt becomes string `0x180` or `0x181` | confirmed (code) |
| `0x001c09f8` | `MugMeter_Render` | only while the player is mugging or in a grab (`0x00228070`, `0x00228090`, `0x002280f0`): both bars (`HudBar_Draw`), the two sprites, the prompt, and the arrows by mode and `+0x84c` | confirmed (code) |
| `0x001c0bb0` | `MugMeter_AnimateArrows` | places the three arrows at the chosen direction and lights them one after another, each for `+0x848` frames, the previous one at half alpha; returns the phase 0-3 | confirmed (code) |
| `0x001c0f28` | `MugMeter_Update` | from `PlayerHUD_Update`: relayout, the bars' positions from the table; both fills 0 when the player is not mugging | confirmed (code) |
| `0x001c1228` | `MugMeter_InitLayoutTable` | static initialiser of the table and the eight arrow positions | confirmed (code) |
| `0x001c1688` | `MissionSelectHUD_StaticInit` | the file's static-init stub (constructor table `0x0053410c`) | confirmed (code) |

#### The mug meter on screen {#mug-meter-layout}

Default video mode (device flag `0x01` only), player 0, GUI coordinates (x right, y down); confirmed (code) at the
functions above. The base point is (−0.08, 0.02) (`0x0050e8f4`, `0x0050e8f8`), and every part's y gets **−0.07**
(`0x0050e920`) when it is placed each update, so the positions below include it. Player 1's parts use the table's
second record (`+0x90`); its x values are in the last column.

| Part | Sprite | Place | Size | Player 1 x |
| --- | --- | --- | --- | --- |
| Bar 1 (`+0x10`) | `part_page0` 54-57 (back, fill, caps), back (32, 32, 32) | left end (0.09, 0.57) | 0.2 × 0.014 | 0.74 |
| Bar 2 (`+0x80`) | the same | left end (0.09, 0.545) | 0.2 × 0.025 | 0.74 |
| Stick base (`+0x100`) | `hud_minigames` 6, a black ball; grey 128, depth 11,000 | (0.04, 0.55) | 0.06 (square) | 0.935 |
| Stick dot (`+0x200`) | `hud_minigames` 5, a grey disc | (0.04 + 0.01 × sx, 0.55 − 0.01 × sy) | 0.05 (square) | 0.935 |
| Arrows (`+0x300`, `+0x400`, `+0x500`) | `hud_minigames` 7, 8, 9: three white arcs of growing size, coloured by mode | one of eight places, below | 0.05 (square) while lit | + 0.898 |
| Prompt (`+0x640`) | markup text, font slot 3, size 1.0, colour `0x005fd310`, string `0x180` or `0x181` | (−0.01, 0.63) | | 0.69 |

- **The stick** is the pad record's **left stick, raw** (`+0x08`, `+0x0c`: x and y in [−1, 1], y up, not turned by
  the camera; [Front end: the pad record](frontend.md#pad-record)), passed as the per-player record's `+0x00` / `+0x04`
  by `HUD_MugMeterUpdate` each update. The dot moves at most 0.01 from its rest point, up for a stick pushed up.
- **The eight arrow places** (`MugMeter_SetStick`): a stick beyond 0.5 on an axis picks one (the four straight ones
  also need the other axis within ±0.1); the last one picked stays. All three arcs go to the same place and angle:

  | Stick | Place | Angle (rad) |
  | --- | --- | --- |
  | up | (0.04, 0.50) | 2.4 |
  | down | (0.04, 0.60) | 5.5 |
  | left | (0.00, 0.555) | 0.65 |
  | right | (0.075, 0.55) | 3.85 |
  | up-left | (0.01, 0.515) | 1.5 |
  | up-right | (0.07, 0.515) | 3.2 |
  | down-right | (0.07, 0.585) | 4.7 |
  | down-left | (0.005, 0.585) | 0.1 |

  Unturned, an arc's bulge faces down-left; with positive angles clockwise on screen
  ([The stereo panel](#stereo-layout)) each angle turns the bulge towards the stick's direction (up: 225° + 137° ≈
  0°; right: 225° + 221° ≈ 90°), which fits the art (inferred from the art and the angles).
- **The ripple** (`MugMeter_AnimateArrows`): each arc is lit for **2 updates** (`+0x848` = `0x0050e948` = 2):
  arc 7 alone, then 7 at half alpha with 8, then 8 at half alpha with 9 (7 hidden); after 6 updates all three are
  hidden and the count starts again. The arcs are animated and drawn only while `+0x850` is set (each stick update
  sets it, each render clears it), and by mode: in modes 0 and 2 while `+0x84c` is set (`HUD_MugMeterSet(1)`, the
  stick on target), in modes 1 and 3 while it is clear.
- **The fills** (`HUD_MugMeterStart(a, b)` → `MugMeter_SetFills`, each clamped to 0-1): bar 1 = `a`, bar 2 = `b`. In
  `Player_UpdateMugging` (a player mugging an AI human) `a` = time since the start (`+0x130`) ÷ the time allowed
  (`+0x134` − `+0x130`) and `b` = time on target (`+0x12c`) ÷ the required time (`+0x04`); with a player victim `a` is
  the victim's own progress against its record. Both bars are drawn with `HudBar_Draw(0.25, bar, 0, 0)`: `flash` 0, so
  no flashing whatever the fill ([unused](#the-rage-meter)), and no trail. A player who is neither mugging, mugged
  nor in the meter's hold gets both fills 0 and nothing drawn.
- **Modes** (`MugMeter_SetMode`, from each state's update): 0 `Player_UpdateMugging` (mugging): bar 1 amber (128,
  100, 0), bar 2 red (170, 43, 43), arcs red, prompt `0x180`; 1 `0x00286550` (being mugged by the other player): the
  bar colours swapped, prompt `0x181`; 2 `0x002833c0` and 3 `Player_UpdateMugHold` (`0x00283a30`), the two sides of a
  hold (`Player_UpdateHold` picks by state flag `0x8000000000`): as 0 and 1 with blue (35, 83, 188) for red. The
  prompt's texts are `GSTRING.HUD` entries `0x180` and `0x181` (not reproduced here).
- The prompt is a `MessageHUD` (`MessageHUD_Setup(1.0, 1.0, …, font slot 3)`) whose alignment byte is never set
  (inferred: left, 0), so at x −0.01 it starts at the screen's left edge (inferred, not checked at run time).
- `HUD_MugMeterStart(0, 0)` also comes from the grab code (`0x002729a8`) when a mugging is set up, so the bars start
  empty.

### After `GUI/MissionSelectHUD.cpp` (no path string): bars and counter panels {#fn-hud-bars-panels}

The same unnamed unit as [`MultiLineTextWidget`](gui.md#fn-multilinetext) (to the stub `0x001c3dc0`): the
`HudGenericBar` class (vtable `0x0053a628`; its setup and draw are in [Scripted bars](#scripted-bars)), the score-board
bar at HUD `+0x8960`, the five counter panels of `HUDGetNewPH` at HUD `+0xacb0` (`0x0060b4f0`, `0xa50` bytes each,
vtable `0x0053a6c0`) and a set of four effects owned by the pause menu.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c1cd0`, `0x001c1d30` | `HudGenericBar_Construct`, `HudGenericBar_Destroy` | the bar (`HudBar` at `+0x40`, the `Bar` vtable at `+0xa8`), icon sprite `+0xb0`, label (markup text) `+0x1b0` | confirmed (code) |
| `0x001c1da0` | `HudGenericBar_SetupIcon` | `HUDEnableBar` kind 0's set-up: the bar with an icon (size 0.06, depth 11,000, (170, 43, 43)) at its middle and 0.01 above it, instead of a label | confirmed (code) |
| `0x001c2638` | `ScoreBoardBar_Init` | from `HUD_InitLevel`: a `Bar` 0.2 × 0.02, grey 128, `part_page0` rectangle 58, at the camera point (-0.1, -0.3) turned into GUI coordinates (`0x00122d48`), and an effect at `+0x70` | confirmed (code) |
| `0x001c2788` | `ScoreBoardBar_Shutdown` | releases the effect; hidden | confirmed (code) |
| `0x001c27e0` | `ScoreBoardBar_IsActive` | `+0xa8` | confirmed (code) |
| `0x001c27e8`, `0x001c2838` | `ScoreBoardBar_Hide`, `ScoreBoardBar_Restore` | `HUD_Hide` / `HUD_Restore`: hidden; shown again only when active and both flags `+0xa0`, `+0xa4` are set | confirmed (code) |
| `0x001c28c8` | `ScoreBoardBar_Signal` | `HUD_ScoreBoardSignal(fill, a, b)`: when active, the fill (0-1, `+0x38`) and the two flags | confirmed (code) |
| `0x001c2908`, `0x001c2940` | `TimedBar_Construct`, `TimedBar_Delete` | a `HudGenericBar` that shows for a time: shown `+0x3f0`, start `+0x3f4`, duration `+0x3f8` | confirmed (code) |
| `0x001c2988`, `0x001c2990` | `TimedBar_IsShown`, `TimedBar_SetYOffset` | `+0x3f0`; the bar's y offset `+0x3e0` | confirmed (code) |
| `0x001c2998`, `0x001c29f8` | `TimedBar_Show`, `TimedBar_Hide` | start = now and the duration (shown when not 0); or all cleared, start -1 | confirmed (code) |
| `0x001c2a10`, `0x001c2a30` | `TimedBar_Update`, `TimedBar_Render` | the bar's update; drawn until the duration has passed, then hidden | confirmed (code) |
| `0x001c2ab8`, `0x001c2af8` | `CounterPanel_Construct`, `CounterPanel_Destroy` | a HUD text item (`0x001e6e98`) with a `TimedBar` at `+0x650` | confirmed (code) |
| `0x001c2b48` | `CounterPanel_Setup` | the text item, and its bar: 0.22·s × 0.025·s (s as in [Scripted bars](#scripted-bars)), (200, 200, 200), sprite word `0x30036`, at (0.805, 0.28), fill (128, 100, 0), background (32, 32, 32), right to left, label at (0.96, 0.28) | confirmed (code) |
| `0x001c2ce0`, `0x001c2d18` | `CounterPanel_IsShown`, `CounterPanel_SetYOffset` | by kind (`+0x440`): kind 3 asks its bar, the others the text (visible `+0x1bc`, y offset `+0x414`) | confirmed (code) |
| `0x001c2d50`, `0x001c2d90` | `CounterPanel_Update`, `CounterPanel_Render` | the bar's or the text's, by kind | confirmed (code) |
| `0x001c2dd0`, `0x001c2e38`, `0x001c2f30` | `CounterPanels_Construct`, `_Setup`, `_Shutdown` | the five panels; set-up from `HUD_InitLevel` (right-aligned texts, the column offsets `+0x3390` / `+0x3394` cleared); shutdown of those in use | confirmed (code) |
| `0x001c2f90` | `CounterPanels_Alloc` | `HUDGetNewPH(label, kind)`: the first free panel: kind, a copy of the label (`+0x444`, also the bar's label), the bar's width; returns its index, or -1 when all five are in use | confirmed (code) |
| `0x001c3088` | `CounterPanels_Release` | `HUDReleasePH`: hidden and free | confirmed (code) |
| `0x001c30a0` | `CounterPanels_ApplyLayout` | the column's constants per video mode (`0x0050e97c`-`0x0050e994`) | confirmed (code) |
| `0x001c3280` | `CounterPanels_SetValue` | `HUDSetPHValue`: `which` 1 or 2 sets the value or the maximum (2 only stores it); the text by kind, right-aligned, label then number: kind 0 `value%`, 1 `value`, 2 `value/max` (not shown while the maximum is 0); kind 3 sets the bar's fill and shows it for `ms`; then restacks the column and, when asked, plays `vags/interface/menu/bonuspart_01` | confirmed (code) |
| `0x001c3608` | `CounterPanels_Update` | from `HUD_Update`: stacks the shown panels down the right column and stores its bottom (`+0x3394`), below which the scripted bars go ([Scripted bars](#scripted-bars)) | confirmed (code) |
| `0x001c37f8` | `CounterPanels_Render` | from `HUD_Render`: the shown panels | confirmed (code) |
| `0x001c3868`, `0x001c38d8`, `0x001c3978` | `PauseEffects_Construct`, `_Delete`, `_Release` | four HUD effects (`+0x10`, then three `0x30` apart from `+0x40`) built by `PauseMenu_Construct`; release frees those that exist | confirmed (code); what they show not traced |

### Before `GUI/RadarHUD.cpp` (no path string): the generic bar {#fn-generic-bar}

The class of the four generic bars (HUD `+0x9970`, `0x3f0` bytes each), [Scripted bars](#scripted-bars).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c1f80` | `HudGenericBar_Setup` | `(width, height, fill, bar, pos, colour, label, spriteWord, drawn)`: the `HudBar` at `+0x40`, background (64, 64, 64), the label | confirmed (code) |
| `0x001c2228` | `HudGenericBar_Shutdown` | releases the icon and the label | confirmed (code) |
| `0x001c2270` / `0x001c2290` | `HudGenericBar_SetLabel` / `_SetLabelAlign` | the label's text; its alignment (0 left, 1 right, 2 centre) | confirmed (code) |
| `0x001c22e8` | `HudGenericBar_Update` | bar at `+0x3c0` (y + `+0x3e0`), label at `+0x3b0` + offsets `+0x3d0`/`+0x3d8` | confirmed (code) |
| `0x001c22b0` | `HudGenericBar_SetWidth` | the `HudBar` width (`+0x40`), × 0.7 with device flag `0x02` | confirmed (code) |
| `0x001c2410` | `HudGenericBar_SetGradient` | the red-to-green fill colour on or off | confirmed (code) |
| `0x001c2470` | `HudGenericBar_Render` | label, then `HudBar_Draw(0.25, bar, 0, 0)` | confirmed (code) |
| `0x001c2520` | `HudGenericBar_SetLabelPos` | the label's anchor `+0x3b0` | confirmed (code) |
| `0x001c2530` | `HudGenericBar_SetFill` | when created, the fill (`+0x78`) clamped to 0-1 | confirmed (code) |
| `0x001c2568` | `HudGenericBar_IsLoaded` | the sprite sheet resident | confirmed (code) |
| `0x001c2588` | `HudGenericBar_SetAlpha` | one alpha on the icon, the bar's colours and the label | confirmed (code) |
| `0x001c25e8` | `ScoreBoardBar_Construct` | not a generic bar: the score-board bar at HUD `+0x8960` (a `Bar` and an effect), set up by `0x001c2638` | confirmed (code) |

### After `GUI/RadarHUD.cpp` (no path string): item and score counters {#fn-after-radarhud}

`0x001c68e8`-`0x001c7e40` hold the flash counter, the message box (on [Front end](frontend.md#fn-after-radarhud)) and
the score counter, in a file without a path string. The counters are parts of the player panel
([Item counters](#item-counters), [Score and money](#score-and-money)).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c68e8` | `FlashCounter_GetCount` | inventory item 1 (flash) of the player (`0x0041e420` on `W_GameState + 0x480`), clamped to 0-9 | confirmed (code) |
| `0x001c6928` | `FlashCounter_Init` | count `+0x420` from the getter | confirmed (code) |
| `0x001c6950` | `FlashCounter_Update` | a changed count clears panel `+0x411c` (the panel shows again); count ≥ 1 shows the widget with the number (`"%d"`), 0 hides it | confirmed (code) |
| `0x001c7730` | `ScoreCounter_Setup` | the score's markup text (`+0x40`, size 1.0, font 3) and the change text widget (`+0x230`), shadows `0x80`, the player `+0x360`, colour `+0x37c` | confirmed (code) |
| `0x001c7860` | `ScoreCounter_Release` | releases both texts | confirmed (code) |
| `0x001c7890` | `ScoreCounter_Sync` | shown and last score (`+0x368`, `+0x36c`) = the player's score now (`0x00422998` on the stats `0x006fe490`) | confirmed (code) |
| `0x001c7928` | `ScoreCounter_Update` | [Score and money](#score-and-money); a gain shows `"+N"`, or, while one of two bonus states of the stats object holds (`0x00422a40`, `0x00422a68`) and 350 ms have passed, the HUD string `0x183` or `0x184` (a bonus label); the change text grows to full size over 400 ms of its 1,000 ms (`0x0050ea3c`, `0x0050ea40`) | confirmed (code) |
| `0x001c7e40` | `ScoreCounter_Render` | both texts | confirmed (code) |

### `GUI/ScrollInHUD.cpp` {#fn-scrollinhud}

The scroll-in queue at HUD `+0x8dd0` (vtable `0x0053aa20`; base class at `0x001e6e98`, a markup text widget with
timing, which `TextProgressHud` shares). Behaviour: [Objectives](#objectives-hudsetobjective). Queue entries are
`0x34` bytes from a pool of 5 at `+0x440`: `+0x04` the text (header and text joined), `+0x1c`/`+0x20` the position,
`+0x24` the base size, `+0x28` the header kind, `+0x30` the time in ms. Confirmed (code) at `0x001c8b08`, `0x001c9028`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c87f0`, `0x001c8838` | `ScrollIn_Construct`, `ScrollIn_Destroy` | base, vtable, nothing showing (`+0x68` = 0) | confirmed (code) |
| `0x001c8880` | `ScrollIn_Init` | from `HUD_InitLevel`: markup text set-up, the list (`STL_List(QueueElement*)`) and the 5-entry pool | confirmed (code) |
| `0x001c8aa0` | `ScrollIn_Shutdown` | frees list and pool | confirmed (code) |
| `0x001c8b08` | `ScrollIn_Queue` | [Objectives](#objectives-hudsetobjective) | confirmed (code) |
| `0x001c8db0` | `ScrollIn_StampTime` | start time `+0x54c` = now | confirmed (code) |
| `0x001c8dc8`, `0x001c8e08` | `ScrollIn_ShowRaw`, `ScrollIn_ShowMessage` | start the front entry: with no text, or with its text, colour and sound (`+0x574`); wrap width 0.7 (`0x0050ea50`), 0.52 split (`0x0050ea4c`) | confirmed (code) |
| `0x001c8ea0` | `ScrollIn_ApplyVideoMode(scroll, kind)` | x offset `+0x560`: in the default mode 0 for header kind -1, 0.04 otherwise (other modes: 0.01-0.06 and a y at `+0x568`) | confirmed (code) |
| `0x001c9028` | `ScrollIn_Update` | [Objectives](#objectives-hudsetobjective); in a split screen a kind-1 message moves 0.21-0.26 right by mode | confirmed (code) |
| `0x001c94c8` | `ScrollIn_Flush(scroll, text)` | `HUDFlushScrollIn`: ends the showing message when its text equals `text`, and drops queued entries whose text equals it | confirmed (code) |
| `0x001c9628` | `ScrollIn_Render` | plays the entry's sound once (`0x0010fcd8`), sets the wrap, draws | confirmed (code) |

### After `GUI/ScrollInHUD.cpp` (no path string): spray counter and the stereo-theft panel {#fn-after-scrollinhud}

The spray-paint counter, then the stereo-theft panel (two at HUD `+0xfea0`, `0xb10` bytes each, vtable `0x0053ab28`;
the game is [Combat: the stereo theft](combat.md#stereo-theft)).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c96e0`, `0x001c9720`, `0x001c9748` | `SprayCounter_GetCount`, `_Init`, `_Update` | as the flash counter, for inventory item 3 | confirmed (code) |
| `0x001c9818`, `0x001c98a8` | `StereoHud_Construct`, `StereoHud_Destroy` | nine `BaseWidget`s (`+0x50`-`+0x9f0`) and two `TextWidget`s (`+0x850`, `+0x920`) | confirmed (code) |
| `0x001c9978` | `StereoHud_ApplyVideoMode` | anchor `+0xaf0` = (0.12, 0.57) in the default mode, x mirrored to 1 − x for player 1; size `+0xb00` = 0.08 (× 0.7 when device flag `0x01` is clear) | confirmed (code) |
| `0x001c9ad0` | `StereoHud_Setup(hud, player)` | sprites, all grey (128, 128, 128) unless noted: backdrop `hud_minigames` rectangle 3 (size 0.08, depth 11,000) at the anchor; arrow rectangle 0 (0.12 × scale, depth 12,000); gauge rectangle 2 (depth 13,000); stick `part_page0` rectangle 0 (0.055); ring `part_page0` rectangle 5 (0.05, (225, 186, 65)); two each of rectangles 4 and 1; two glyph texts (font slot 3, characters `0x9c`, `0xa0`); [on screen](#stereo-layout) | confirmed (code) |
| `0x001ca118` | `StereoHud_Start(target, hud)` | set-up, target `+0x4c`, done `+0x40` = 0, pop step `+0xb04` = 0.001, progress 0 | confirmed (code) |
| `0x001ca180` | `StereoHud_Shutdown` | releases every part | confirmed (code) |
| `0x001ca210` | `StereoHud_SetProgress(value, hud, stage)` | `+0x48` value (the angle turned), `+0x44` stage (0-3), done cleared | confirmed (code) |
| `0x001ca220` | `StereoHud_IsReady` | its batch (`+0xab4`) resident | confirmed (code) |
| `0x001ca260` | `StereoHud_Render` | the backdrop; unless done, the arrow, gauge, stick and ring | confirmed (code) |
| `0x001ca2d0` | `StereoHud_Update` | the arrow at the stage's corner of the backdrop (anchor ± (size + 0.025 if flag `0x02`), ∓ 0.05) turned 0, −π/2, −π or −3π/2; the gauge sized 0.025-0.05 by value / target and turned by −value; at value ≥ target it rises by a step that doubles each frame; the stick cycles `part_page0` rectangles 3, 2, 1, 0 every 320 ms; the ring turns −0.004 rad per ms | confirmed (code) |

#### The stereo panel on screen {#stereo-layout}

Default video mode (device flag `0x01` only), player 0; GUI coordinates (x right, y down). Sizes are BaseWidget sizes
in overlay units ([GUI: the widget classes](gui.md#widget-classes)); in this mode `StereoHud_Update` sets each moving
part's width to its height every update, so the arrow, gauge, stick and ring are **square**. Confirmed (code) at
`0x001c9ad0`, `0x001ca2d0` and `0x001ca260` unless marked.

**What is drawn** (`StereoHud_Render`): only five parts. The backdrop always, and unless done (`+0x40`, cleared by
start and by every progress call) the arrow, the gauge, the stick and the ring, in that order. The two rectangle-4
sprites, the two rectangle-1 sprites and the two glyph texts (`"%c"` of characters `0x9c` and `0xa0` in font slot 3,
`part_page0`: the R1 and L1 button glyphs, [GUI: markup](gui.md#markup); size 0.04) are set up and **never drawn**.

**The pictures** (`hud_minigames`, 256 × 128; rectangles read from the sheet's `0x4c` chunk, the look from viewing
the texture): rectangle 3 is the **car radio** (112 × 47 px); rectangle 0 is a **round lens** (60 × 60 px), black
with a grey rounded corner of the radio filling its lower left; rectangle 2 is a **small round knob** with a slot
(21 × 21 px); rectangle 1 (unused) a red arrow pointing down; rectangle 4 (unused) a grey bracket. So the "arrow"
is a magnified corner of the radio, and the "gauge" a turning knob.

With the anchor `A = (0.12, 0.57)` (player 1: x = 0.88) and `S = 0.08`:

| Part | Sprite word (sheet, rectangle) | Size | Depth | Colour | Position |
| --- | --- | --- | --- | --- | --- |
| Backdrop | `0xa0003` (`hud_minigames`, 3) | 0.08 | 11,000 | grey 128 | `A` |
| Arrow | `0xa0000` (`hud_minigames`, 0) | 0.12 | 12,000 | grey 128 | the stage's corner, below |
| Gauge | `0xa0002` (`hud_minigames`, 2) | 0.025-0.05 | 13,000 | grey 128 | the arrow's position (+ the pop, below) |
| Stick | 3, 2, 1, 0 (`part_page0`, 0-3) | 0.055 | 11,000 | grey 128 | (`A.x` − 0.015, `A.y` + `S`) in stages 0 and 1; (`A.x` − 0.015, `A.y` − `S`) in 2 and 3 |
| Ring | 5 (`part_page0`, 5) | 0.05 | 11,000 | (225, 186, 65) | the stick's position + (0.04, 0) |

- **The arrow** (the lens) by stage (`+0x44`): 0 at (`A.x` + `S`, `A.y` − 0.05), angle 0; 1 at (`A.x` − `S`,
  `A.y` − 0.05), −π/2; 2 at (`A.x` − `S`, `A.y` + 0.05), −π; 3 at (`A.x` + `S`, `A.y` + 0.05), −3π/2. So it goes round
  the backdrop's corners anticlockwise on screen (top right, top left, bottom left, bottom right), turned a quarter
  more each stage. **A positive widget angle turns a sprite clockwise on screen**: unturned, the lens's grey corner
  is at its lower left, towards the radio from the top-right corner, and only clockwise-positive keeps it facing the
  radio at the other three corners (−π/2 puts it at the lower right for the top-left corner). This matches the radar
  arrow, confirmed (runtime) ([GUI: radar icons](gui.md#radar-icons)); inferred here from the art.
  With device flag `0x02` (PAL, speculative) the x offset grows by 0.025 and the y offset is 0.04.
- **The gauge** (`value` = `+0x48`, `target` = `+0x4c`): size = 0.025 + 0.025 × value / target
  (`Math_MapRange(value, 0, target, 0.025, 0.05)`), angle = −value. The value is the stick angle turned in the current
  stage, in radians, from 0 up to the stage target (`Theft_UpdateStereo` `0x0027e908`, clamped there), and the target
  is the one `StereoHud_Start` was given: the player class's turn per stage (`Human_GetClassSpinAngle`, 6π or 2π,
  [Combat](combat.md#stereo-theft)). So the gauge grows from half size to full size and spins once per radian turned.
- **The pop**: while value ≥ target (the 250 ms pause after a stage is complete, while the value stays at the
  target), the gauge's **y** grows by a step each update and the step doubles (from 0.001: 0.001, 0.002, 0.004, ...,
  so 0.127 after 7 updates). The gauge falls down the screen and accelerates; below the target the step is reset to
  0.001 and the gauge is back on the arrow.
- **The stick** shows `part_page0` rectangle 3 − (t mod 1280) / 320 (t = the timer `0x0050b8b8` slot `+0x34`, in ms):
  3, 2, 1, 0, a 1.28 s loop, inferred to be a stick drawn in four positions round the circle.
- **The ring** turns −0.004 rad per ms of that timer (one turn in about 1.57 s) and swaps its rectangle's `u0` and
  `u1` every update, so it is mirrored left to right every other update.

**When it is shown** (`MiniGame_Start` `0x0022dc40` → `StereoTheft_Start` `0x0022dd98` → `HUD_StereoTheftStart`):
on the triangle press, when the intro 683 starts (`MiniGame_StartAtObject` `0x002789d0`), so the panel is up during
the intro with the gauge at half size. The game updates it each update (`HUD_StereoTheftSet`, from
`Theft_UpdateStereo`, 2 calls). **Success and failure both remove it at once**: `MiniGame_PlayEnd` (`0x00278628`)
starts the end clip (685 or 686) and calls `MiniGame_End` → `StereoTheft_End` (`0x0022e020`) →
`HUD_StereoTheftEnd` → `StereoHud_Shutdown`; an abort (`MiniGame_Abort` `0x0022d628`) does the same. Each stage
completed plays interface cue `0x22`, and the fourth also `0x23`.

### After `GUI/SubTitle.cpp` (no path string): the tagging panel {#fn-after-subtitle}

What the player sees during the tagging stick game ([Crimes](crimes.md#tagging)), drawn from `HUD_Render` per player.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001cb468` | `TagHud_ApplyVideoMode` | per-player layout records of `0x30` bytes at `0x0050ea90` and the sprite sizes; the default mode keeps the static values | confirmed (code) |
| `0x001cb6f0`, `0x001cb778` | `TagHud_Setup`, `TagHud_Release` | the frame sprite: `part_page0` rectangle 93, size 0.04, depth 11,000, (191, 191, 191, 191) | confirmed (code) |
| `0x001cb798` | `TagHud_Render(widget, human)` | below the table | confirmed (code) |

**The panel** (`0x001cb798`), drawn each frame only while the human has a tag game (human `+0xd4` → `+0x13c`) and batch
3 (`part_page0`) is resident. Default video mode (device flag `0x01`), values from the layout record at `0x0050ea90`
(stride `0x30` per player) and the sprite sizes `0x0050eaf0`-`0x0050eaf8`. Confirmed (code) unless marked.

**Frame of reference.** The player's origin is GUI (0.01, 0.70) for player 0 and (0.76, 0.70) for player 1. It is turned
into overlay space first (device slot `+0x90`, `0x00195238`; [Graphics](graphics.md#2d-drawing)): X = (x − 0.5) × 640 /
448 and Y = 0.5 − y, so (−0.700, −0.200) for player 0. Everything after that is in **overlay units, Y up**. Cell (x, y)
of the 256 × 256 grid sits at origin + (x × 0.0025, y × 0.0025): the code adds x along the first row of an identity
matrix and y along its third, the depth slot holding 1.1, and then `Mat_SwapYZ` (`0x003368d8`) negates the middle
component and swaps it with the third, giving (X + 0.0025 x, Y + 0.0025 y, −1.1). So **a larger cell y draws higher** on
screen, and the whole grid spans 0.64 overlay units. Sprite sizes are given to the batch directly as width and height,
both the same value, in overlay units, which are square on screen (1.595 × 1.1 units over 640 × 448 pixels, about 401
pixels per unit).

- the **path**: `part_page0` rectangle 64, size 0.015, one per path point (tag `+0x26c`, byte pairs; count `+0x268`)
  except the last 3, grey 100 + 155 × i / n, alpha 255 (darker at the start);
- the **painted cells**: rectangle 62, size 0.03, one per painted cell (tag `+0x0c`, byte pairs; count `+0x08`), in the
  player's colour (human `+0x640`) at alpha 205;
- the **cursor**: rectangle 63, size 0.04, at the tag's `+0x4f0`, two **floats** (x, y) in grid cells, so it moves
  smoothly between cells and is not snapped (inferred: the stick-driven cursor of [the stick game](crimes.md)). Its
  colour fades between white and black, alpha 255, over 250 ms, reversing every 250 ms (game clock mod 500,
  `Colour_LerpRatio` `0x00338240`), and is plain white while the game is paused (`Tag_IsPaused`, `0x002748a0`);
- the **frame**, the widget of `TagHud_Setup` (`part_page0` rectangle 93, colour (191, 191, 191, **191**), depth
  11,000): its position is set in overlay space (flag 0) at origin + (0.01, 0.07), size 0.07 (a widget size, the overlay
  height). Its alpha 191 is fixed at setup and it is drawn with the rest of the panel, so it shows on every frame the
  panel does, not otherwise;
- the **charge bar**: rectangle 78 in the player's colour at alpha 255, 0.0255 wide and 0.045 × c high. Its **bottom**
  is fixed at origin + (0.0648 − 0.0565, 0.075 − 0.03) = origin + (0.0083, 0.045) and its centre is placed half its
  height above that, so it **grows upward** from the bottom (overlay Y up). Here c is `Tag_GetChargeLeft`
  (`0x00274638`): 1 − (game clock − the charge's start, tag `+0x518`) ÷ the charge time of the tag difficulty (13,000,
  11,000 or 9,500 ms, [Crimes](crimes.md)), the fraction of the current charge still left; 1.0 when the elapsed time is
  not below the charge time (an unsigned compare, so also before the start).

The other video modes rewrite the record (`TagHud_ApplyVideoMode`, `0x001cb468`): without flag `0x01` the origin is
(0.012, 0.68), step 0.0018, bar 0.02 × 0.038 at (−0.055, −0.042), frame 0.06; with flag `0x02` the sizes become 0.012,
0.02 and 0.03.

### After `GUI/TextEntryPad.cpp` (no path string): text progress, stopwatch and hint texts {#fn-after-textentrypad}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001cce30`, `0x001cce68` | `TextProgressHud_Construct`, `_Destroy` | the six text-progress widgets of `HUDEnableTextProgress` (`0x560` bytes, vtable `0x0053ad78`, the scroll-in base class) | confirmed (code) |
| `0x001cce90` | `TextProgressHud_Clear` | label `+0x540`, text `+0x440` and value `+0x558` cleared, base shutdown | confirmed (code) |
| `0x001cceb8` | `TextProgressHud_SetLabel` | the label | confirmed (code) |
| `0x001ccf10` | `TextProgressHud_Compose` | the shown text: `<SIZE 0.8>`, the label, `": "` and the value in decimal | confirmed (code) |
| `0x001cd3c8` | `StopWatchHud_Construct` | the stopwatch text at HUD `+0x6850` (a markup text subclass, vtable `0x0053aef0`) | confirmed (code) |
| `0x001cd400` | `StopWatchHud_ApplyVideoMode` | default mode: minutes form at (1.0, 0.24), seconds form at (0.49, 0.10) | confirmed (code) |
| `0x001cd578` | `StopWatchHud_Setup` | from `HUD_InitLevel`: markup text size 1.0, white, font 3, right-aligned (`<RIGHT>` flags 5) | confirmed (code) |
| `0x001cd5e8`, `0x001cd748` | `StopWatchHud_UpdateMinutes`, `_UpdateSeconds` | while the stopwatch (`*0x0051504c`) shows (`+0x18`): its label (`+0x1c`, may be none) then `%2u:%02u` of its time (`+0x08`, ms) right-aligned, or `%02u` seconds centred (flags 6); otherwise hidden | confirmed (code) |
| `0x001cd888`, `0x001cd8c8` | `Tutorial_SetMessageText`, `Tutorial_GetMessageText` | the `CfgTutorialMessage` table `0x00622fd0`: id → the text's string handle (`0x003864e0`) | confirmed (code) |
| `0x001cd8e0`, `0x001cd930` | `HintBox_Construct`, `HintBox_Destroy` | the hint box ([Hints](#hints-hudsettutorialtext)): markup text subclass (vtable `0x0053aff0`) with its box sprite at `+0x2a0`; the code belongs to `TutorialHUD.cpp` (inferred, it sits just before that file's anchor) | confirmed (code) |

### `GUI/TutorialHUD.cpp` {#fn-tutorialhud}

`0x001cd988`-`0x001cfea0`: the hint box (HUD `+0x8a10`, class vtable around `0x0053b000`: `+0x20` update, `+0x28`
render), then `UsageInfo` ([GUI](gui.md#fn-usageinfo)) and the Armies of the Night game-over screen
([Pause](pause.md#an-game-over)), which share the TU: the static-init stub `0x001cfea0` ends it (inferred,
[Source map](source-map.md#method)). The hint box's behaviour is on [Hints](#hints-hudsettutorialtext).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001cd988` | `HintBox_Setup` | from `HUD_InitLevel`: layout pointer `+0x3a4` = `0x0050eb50`, the markup text in `0x005fd310`, the box sprite `+0x2a0` (`part_page0` rectangle 78, depth 11,000), the queue list `+0x290` and a free list of 20 entries (`+0x294` over the pool `+0x1f0`, 8 bytes each), text width 0.56 (`0x001bb390`), showing priority `+0x3ac` = 0 | confirmed (code) |
| `0x001cdc20` | `HintBox_Shutdown` | from `HUD_ShutdownLevel`: clears the text, frees both lists, clears the tutorial callback name `+0x3a0` | confirmed (code) |
| `0x001cdc80` | `HintBox_Update` | takes the next hint, lays the box out ([Hints](#hints-hudsettutorialtext)) | confirmed (code) |
| `0x001ce1e8` | `HintBox_QueueGameHint` | queues `CfgTutorialMessage` entry `id` at priority 2 | confirmed (code) |
| `0x001ce218` | `HintBox_InsertByPriority` | sorted insert, first-in first-out within a priority; dropped when the pool is empty | confirmed (code) |
| `0x001ce3c0` | `HintBox_Queue` | `HUDSetTutorialText`; a more urgent hint pushes the showing one back | confirmed (code) |
| `0x001ce550` | `HintBox_Contains` | `HUDCheckTutorialText` | confirmed (code) |
| `0x001ce5c0` | `HintBox_Withdraw` | removes one hint by its text pointer | confirmed (code) |
| `0x001ce718` | `HintBox_WithdrawGameHint` | withdraws a `CfgTutorialMessage` entry | confirmed (code) |
| `0x001ce748` | `HintBox_FlushPriority` | `HUDFlushTutorialText(p)`; 4 removes all | confirmed (code) |
| `0x001ce8b8` | `HintBox_Render` | from `HUD_Render`: box alpha = 125 (`0x0050ebd4`) × the text's alpha / 255, draws the box, re-applies the text width 0.74 (0.52 split, `0x0050ebd8` / `0x0050ebd0`) and colour `0x005fd310`, draws the text | confirmed (code) |
| `0x001ce9a8` | `Tutorial_CallCallback` | [The tutorial callback](#tutorial-callback) | confirmed (code) |
| `0x001cea38` | `Tutorial_IsHintUnlocked(id)` | true when the unlockables manager (`0x006fe998`) has an unlocked type-11 record with data `id` ([Unlockables](player-state.md#unlockables)); the 25 callers test it before queueing a game hint | confirmed (code) |

### After `PM_TooManyProfiles.cpp` (no path string): the player panel base and the target panel {#fn-player-panel-base}

`0x0020db98`-`0x0020fc98`: the methods the two player panels share (interface vtable `0x0053e9c0`, whose slots
`PlayerHUD`'s `0x0053edf8` and `ANHud`'s `0x0053ecc0` reuse or replace; the interface sits at panel `+0x4120`) and
the **target panel** (vtable `0x0053ed60`), which only the [Armies of the Night panel](#fn-anhud) owns. The code
after the profile-manager screen has no path string of its own, and the static initialiser at its end
(`0x00211c80`) sets this code's colours, so it is a file of its own (inferred) that the source map counts with
`PM_TooManyProfiles.cpp`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0020db98`, `0x0020dbc0` | `PlayerHUD_Enable`, `PlayerHUD_Disable` | slot `+0x20`: "wanted" `+0x40f8` = 1 and the money counter (`+0x400`) reset; slot `+0x28`: wanted = 0, then `PlayerHUD_Hide` | confirmed (code) |
| `0x0020dbe0` | `PlayerHUD_ShutdownBase` | slot `+0x18` of the base, called first by both panels' shutdowns: wanted, shown and attached (`+0x40f8`, `+0x40fc`, `+0x4100`) cleared, the banner and its two shadows (`+0x3ce0`, `+0x3de0`, `+0x3ee0`) released | confirmed (code) |
| `0x0020dc98` | `PlayerHUD_SetBanner` | slot `+0x30`: [The name banner](#the-name-banner) | confirmed (code) |
| `0x0020dfd0`, `0x0020dfd8`, `0x0020e168` | `PlayerHUD_ReportBars`, `PlayerHUD_OnAttach`, `PlayerHUD_EmptyHook` | empty: the health and power reports (`HUD_ReportHealth` / `_ReportPower`), `HUD_AttachPlayer`'s hook, and the last call of both panels' Init. Not needed: no effect | confirmed (code) |
| `0x0020dfe0` | `PlayerHUD_SetMayShow` | `+0x4108` = flag (`HidePlayerHud` / `ShowPlayerHud`, [Showing and hiding](#showing-and-hiding)) | confirmed (code) |
| `0x0020dfe8`, `0x0020e008` | `PlayerHUD_HideCall`, `PlayerHUD_ShowCall` | one-line callers of hide and show (from `HUD_AttachPlayer`, `HUD_Hide`, `HUD_Restore` and the both-panels pair) | confirmed (code) |
| `0x0020e028` | `PlayerHUD_Show` | when attached and allowed (`+0x4108`): shown `+0x40fc` = 1, the money counter (`+0x3454`) and the banner (`+0x3ce8`) visible | confirmed (code) |
| `0x0020e058` | `PlayerHUD_Hide` | when attached, unless an Armies of the Night level with game state `+0x14c` = 1: banner and panel hidden, money counter hidden and reset; sets `0x0050b1b0[player]` when `+0x3444` is set (read by the HUD, not traced) | confirmed (code) |
| `0x0020e0f8` | `PlayerHUD_IsLoaded` | slot `+0x90`: the three banner sprites' sheets resident | confirmed (code) |
| `0x0020e170`, `0x0020e210` | `TargetPanel_Construct`, `TargetPanel_Destroy` | the widget base and six `{target handle, sprite word}` pairs at `+0x44` cleared (the empty handle `0x006ebd30`, word -1); destructor slot `+0x60` | confirmed (code) |
| `0x0020e238` | `TargetPanel_Setup(panel, player)` | from `ANHud_Init`: the player's two slots (`0x0063f240` + player × `0xd00` + slot × `0x680`): picture (`0x1e001e`) and frame (`0x1e001f`, (35, 83, 188)) sprites, and a `HudGenericBar` (background (37, 37, 37), sprite word `0x30036`, label `"temp"`, gradient green to green) at the [target panel's layout](#the-target-panel) | confirmed (code) |
| `0x0020e5b8` | `TargetPanel_Shutdown` | slot `+0x68`: both slots' sprites and bars released, the pairs cleared | confirmed (code) |
| `0x0020e6f8` | `TargetPanel_Update` | slot `+0x30`, from `ANHud_Update` only: [The target panel](#the-target-panel) | confirmed (code) |
| `0x0020fbc0` | `TargetPanel_Render` | slot `+0x38`, from `ANHud_Draw`: each used slot's frame, picture and bar | confirmed (code) |
| `0x0020fc98` | `TargetPanel_SetBossTexture(panel, handle, word, on)` | on: the pair `{handle, word}` into the first free of the six; off: the handle's pair cleared. The pairs give a target its picture (inferred from the name and the sprite word) | confirmed (code) |

### After `PM_TooManyProfiles.cpp` (no path string): the Armies of the Night panel {#fn-anhud}

`0x0020fd40`-`0x00211c80`: **`ANHud`**, the player panel of the Armies of the Night arcade levels (60-69). It has
`PlayerHUD`'s layout up to `+0x4120` (the same constructor calls) and its own interface vtable `0x0053ecc0`;
`HUD_InitLevel` allocates one per player in those levels instead of using the static `PlayerHUD`s. Unlike
`PlayerHUD` it **shows health and power** as two bars, a portrait and the [target panel](#the-target-panel).
Values in the default mode, from the layout table `0x0050f7a0` (`0x120` bytes per player, copied from `0x00559810`
by `ANHud_ApplyVideoMode`); positions are offsets from the panel base `+0x10`: player 0 (0.08, 0.03), player 1
(0.80, 0.03).

| Part | Offset in the table | Player 0 | What it shows |
| --- | --- | --- | --- |
| power bar (`HudBar` at panel `+0x00`) | `+0x60`, size `+0x70` | (-0.018, 0.078), 0.35 × 0.009 | power % (`0x00222f48`: record `+0x148` over its maximum) / 100; background (37, 37, 37), fill white |
| health bar (`HudBar` `+0x4330`) | `+0xa0`, size `+0xb0` | (-0.018, 0.06), 0.35 × 0.024 | health / maximum (`0x00222e40`, `Human_GetMaxHealth`); background (37, 37, 37), fill green (115, 183, 11) |
| banner and two black shadows | `+0x40`, size `+0x50` | (-0.018, 0.02), 0.055 | the [name banner](#the-name-banner): player 0 tinted (150, 30, 30), player 1 (245, 184, 0); the banner `0x210000` (100, 100, 200, 160) |
| portrait (`+0x4130`) and its frame (`+0x4230`) | `+0x20`, sizes `+0x30`, `+0x34` | (-0.058, 0.04), 0.08 and 0.095 | sprite `0x23` (banner kind 3), `0x27` (kind 8) or `0x31`; the frame `0x1e001f` in the player's colour |
| name (markup text `+0xb0`) and score text (`+0x2a0`) | `+0x80`, `+0x90` | (0.09, 0.02), (0.23, 0.02) | the score text in (217, 158, 12) |
| join text (`+0x43a0`) | `+0x110` | (0.145, 0.08) | string `0xd4`, grey (191, 191, 191), centred, blinking every 500 ms (`0x0050f9f0`) while the player is not in |

Bar sizes are multiplied by 1 / (0.75 × the player camera's slot `+0x1fc`) and by the scale `0x0050f9e0` (1.0;
0.75 with device flag `0x02`; × 1.15 when the device's slot `+0xb8` is set, which also moves the base 0.035 left).
Confirmed (code) at `0x00210e48`, `0x00211938`, `0x002102f0`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0020fd40`, `0x0020fd60` | `ANHud_Enable`, `ANHud_Disable` | slot `+0x20`: wanted = 1 and `PlayerHUD_Show`; slot `+0x28`: wanted = 0 and `PlayerHUD_Hide` | confirmed (code) |
| `0x0020fd80`, `0x0020ffe8` | `ANHud_Construct`, `ANHud_Destroy` | the `PlayerHUD` parts, the portrait and frame, the health bar, the join text and the `TargetPanel` (`+0x4590`); the destructor (slot `+0x08`) frees them | confirmed (code) |
| `0x002102f0` | `ANHud_ApplyVideoMode` | each update: copies the ANHud table and the target panel's (`0x00559710` → `0x0050f6a0`, `0x80` per player) and patches them for 16:9, flag `0x02` and flags `0x02` + `0x04` | confirmed (code) |
| `0x002107e8` | `ANHud_Init(panel, player)` | slot `+0x10`: the two bars (sprite word `0x30036`, the health bar with the gradient), the score counter (`+0x70`), `TargetPanel_Setup`; then hidden | confirmed (code) |
| `0x00210b80` | `ANHud_SetBanner` | slot `+0x30`: `PlayerHUD_SetBanner`, then the portrait (depth 14,000) and its frame | confirmed (code) |
| `0x00210d10` | `ANHud_Shutdown` | slot `+0x18`: the base shutdown, the portrait and frame, the score counter, the join text and the target panel | confirmed (code) |
| `0x00210dc0` | `ANHud_SetBossTexture` | slot `+0x70`: `TargetPanel_SetBossTexture` | confirmed (code) |
| `0x00210de0` | `ANHud_IsLoaded` | slot `+0x90`: banner, portrait and frame sheets resident | confirmed (code) |
| `0x00210e48` | `ANHud_Update` | slot `+0x40`: the join text while not wanted; once loaded and shown, the parts above each frame, then `TargetPanel_Update` | confirmed (code) |
| `0x00211938` | `ANHud_Draw` | slot `+0x38`: when shown, wanted and loaded: score, banner, the power bar (on and off for `+0x4114` frames each when set, like [`FlashRageBar`](#the-rage-meter)), the health bar, frame, portrait and target panels; else the join text. A player who is down (`0x00227dd8`) drops wanted | confirmed (code) |
| `0x00211a90`, `0x00211c80` | `ANHud_StaticInit`, `ANHud_StaticInitStub` | static initialiser (ctor list `0x00534148`): the colours `0x0063f1f8` (37, 37, 37), `0x0063f200` (170, 43, 43), `0x0063f208` (115, 183, 11), `0x0063f210` white, `0x0063f218` (35, 83, 188), `0x0063f220` (150, 30, 30), `0x0063f228` (245, 184, 0), `0x0063f230` (170, 43, 43), `0x0063f238` (128, 0, 0, 111), and the four target-panel slots | confirmed (code) |

### The player panel (no path string) {#fn-player-panel}

`0x00211ca0`-`0x00214ce8`: **`PlayerHUD`** (interface vtable `0x0053edf8` at `+0x4120`), the two static panels at HUD
`+0x19130` ([The player panel layout](#the-player-panel-layout-0x0050fa10)); its own static initialiser
(`0x00214ce8`) ends the file.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00211ca0` | `PlayerHUD_Construct` | from `HUD_Construct`: the panel's parts, then nine tally sprites at `+0x4150` | confirmed (code) |
| `0x00211ef8` | `PlayerHUD_ApplyVideoMode` | patches the layout table per video mode ([Coordinates](#coordinates)) | confirmed (code) |
| `0x00212840` | `PlayerHUD_Init(panel, player)` | slot `+0x10`: builds the panel from the layout table | confirmed (code) |
| `0x00213060` | `PlayerHUD_SetupTallyMarks(panel, player)` | slot `+0x58`, once (`+0x4134`): the nine tally sprites (word `0x16e`, every fifth `0x16f`) from the table's `+0x1f0` and size `+0x210` | confirmed (code) |
| `0x002131f0` | `PlayerHUD_Shutdown` | slot `+0x18`: the base shutdown, tally off (`+0x4130`), the Warrior command display, the tally sprites released | confirmed (code) |
| `0x00213290` | `PlayerHUD_Render` | slot `+0x38`: fade, rage flashing, the parts ([The rage meter](#the-rage-meter)) | confirmed (code) |
| `0x00213718`, `0x00213770` | `PlayerHUD_FindCounterSlot`, `PlayerHUD_UpdateCounters` | the [item counters](#item-counters): the slot (of four, `0x0050fee0` + player × `0xc0`) holding an item or the first free one; the counters' slots, places and counts | confirmed (code) |
| `0x00213e68` | `PlayerHUD_LayoutTallyMarks(panel, shifted)` | `+0x413c` marks from the origin `+0x40e0` with the table's `+0x1f0`-`+0x230`, every fifth crossing the four before, shifted below the counters ([The panel's tally](#panel-tally)) | confirmed (code) |
| `0x00214138` | `PlayerHUD_Update` | slot `+0x40`: values, colours and positions each frame ([The rage meter](#the-rage-meter) and the sections after it) | confirmed (code) |
| `0x00214bc8`, `0x00214c28` | `PlayerHUD_RefreshTallyMarks`, `PlayerHUD_RenderTallyMarks` | slot `+0x60`: when the tally is on, origin from `0x0050fa10`, count = living members of gang `+0x4140` (`0x0016a458`), laid out unshifted; slot `+0x68`: the marks drawn at full alpha. No caller found (inferred unused): `PlayerHUD_Update` and `PlayerHUD_Render` do the live work; `HUD_SetNumIndicator` turns the tally on ([The panel's tally](#panel-tally)) | confirmed (code) |
| `0x00214cb0`, `0x00214ce8` | `PlayerHUD_StaticInit`, `PlayerHUD_StaticInitStub` | static initialiser (ctor list `0x0053414c`): the colour `0x00640c40` = (128, 0, 0, 111) | confirmed (code) |

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0041cc10` | `WarCommandDisplay_ResetShown` | both players' shown command bytes (`+0x41c`, `+0x41d`) set to 7 (none); from `WarCommandDisplay_ShowCurrent` | confirmed (code) |
| `0x0041d988` / `0x0041d9a8` / `0x0041d9c8` / `0x0041d9e8` / `0x0041dad8` | `Cfg_SetHUDMessage` / `Cfg_SetTutorialMessage` / `Cfg_SetCrimeMessage` / `Cfg_SetHUDColour` / `Cfg_SetWarriorCommandText` | forwarders: `CfgHUDMessage` to `GlobalString_Set`, `CfgTutorialMessage` to `Tutorial_SetMessageText`, `CfgCrimeMessage` to `HudCrimePanel_SetMessage`, `CfgHUDColour` to `HudColourTag_Set`, the command text to `WarCommandText_Set` | confirmed (code) |
| `0x00422a40` / `0x00422a68` | `Stats_TakeStyleFlag` / `Stats_TakeBonusFlag` | the human's record's style / bonus changed flag, cleared on read; `ScoreCounter_Update` picks label `0x183` / `0x184` from them | confirmed (code) |

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
- **The action prompt** (`repo:src/gamemodes/gameplay_mode.cpp`, the level's part `PlayLevelMode::promptOffer()`):
  each frame of play player 0's text is chosen in `HUD_Update`'s order: nothing while he tags, plays a scene part,
  mugs, steals a stereo or picks a lock; a held human with money or a pocket item who may be mugged gives
  `GSTRING.HUD` 1; else a knocked-out partner to revive gives 4 (`GameplayMode::revivableInReach()`: the nearest
  revivable human friendly to him within 3 m, not cuffed, in sight, while player 1 holds a flash; the states
  `0x80000000` and `0x100000000` are not modelled); else the context records by kind: an action object's own text
  (1), a pickable door 15 (2), a freed car stereo 16 (3), a dealer's offer (4: his type's prompt, 6 for the flash,
  while his goal registers it, from the greeting on, within 1.75 m). A prompt naming the dealers' goods wakes the
  player panel. Not yet: a talkable human's prompt and the interrogation (0). The action object's second text
  (`SetMsgHandlerEx`'s `prompt2`)
  is queued as a priority-0 hint while it is in reach and withdrawn when it changes or leaves.
- **The stereo-theft panel** (`repo:src/hud/stereo_hud.h`, stepped by `PlayLevelMode::stepStereoPanel()`) as
  [the stereo panel on screen](#stereo-layout) says: up from the triangle press, the arrow round the backdrop's
  corners by stage, the gauge sized and turned by the angle turned and dropping by a doubling step while a stage is
  complete, the stick's four pictures and the turning, flickering ring; cue `0x22` per stage and `0x23` with the
  fourth; gone as the end clip starts. Turned sprites (the format-1 instances: this panel and the instruction arrow)
  go through `SpriteBatch::addSprite(sprite, rotation)`, a positive angle turning clockwise on screen
  as the original's widgets do. **Coney's stand-in**: the stick and ring are timed on the game
  clock, not the timer `0x0050b8b8`.
- **The mug meter** (`repo:src/hud/mug_meter.h`, stepped by `PlayLevelMode::stepMugMeter()`) as
  [the mug meter on screen](#mug-meter-layout) says, while player 1 mugs (mode 0): the two `part_page0` bars, the
  stick's ball and dot following the raw left stick, the three arcs rippling two updates each at the last direction
  the stick picked while it is on target, and `GSTRING.HUD` `0x180` (`0x181` for modes 1 and 3) above; while it
  shows, the scroll-in message, the hint box and the action prompt are hidden. **Coney's stand-ins**: bar 1 is the
  off-target time against the allowance (record `+0x0c`, what fails a player's mugging in Coney) instead of the time
  since the start against `+0x134`. Not yet: modes 1-3 (a player victim and
  the hold), which need two players and the hold's states.
- **The lock-pick dial** (`repo:src/hud/lock_pick_hud.h`, stepped by `PlayLevelMode::stepLockPickDial()`) as
  [the dial's layout](#lock-pick-dial-layout) says, while player 1 picks a lock: the three pins turned by their
  angles (gone once every pin is done), then over every sprite the good and perfect wedges (whole rim vertices
  coloured, the next segment fading to clear) and the lock face, as 32-segment fans in 640 × 448 pixels. The shapes
  go in two batches sorted above all the HUD's others, so they lie on top of the pins as `Shape2D_DrawQueued` does.
- **The fixed-camera icon** (`repo:src/hud/fixed_cam_icon.h`) as [the icon](#hud-fixed-cam-icon) says: while player 1's
  camera is a locked one or camera switch 0 is off, pushing the right stick (a raw byte outside 64-176) shows
  `part_page0` rectangle 87 at (0.9, 0.64), fully opaque while held and fading over 1 s after; any other camera hides
  it. `HUDEnableFixedCamIcon` turns both players' off until the next level's set-up. Coney has no fixed, transition
  or rail camera, so only the locked camera and the switch show it.
- **The tagging panel** (`repo:src/hud/tag_hud.h`, set by `GameplayMode::updateTagPanel()`) as
  [the tagging panel](#fn-after-subtitle) says, while player 1 sprays: the path's dots but the last three, grey from
  100 to 255, the painted cells in his `HuTagColor` at alpha 205, the cursor between cells fading white to black and
  back over 500 ms (white while the game pauses), the charge bar shrinking down to its fixed bottom, and the frame at
  alpha 191; gone when the spray ends.
- **In play**: the play mode steps the HUD with player 1's rage, score, money and item counts (flash, spray paint,
  handcuffs, keys from the inventory) and draws it over the frame; the story shares the
  flow's HUD with the scripts. A disc test (`[disc][hud]`, `repo:tests/platform/disc_level99_hud_test.cpp`) plays
  `level99` checkpoint 1 through `l99_c1` unskipped: hidden and nothing drawn under the bars, then shown, drawn and
  the first hint up, and a pause hiding and showing it. The debug menus' HUD page sets its values ([Debug menu](../guides/debug-menu.md#pages)).

**The mash meter** (`repo:src/hud/mash_meter.h`), as [the mash meter](#mash-meter-layout) says: player 0's or 1's button
sprite, the bar from its left end filled by the mash over its target and the L1 and R1 glyphs alternating every
400 ms (the triangle blink for any other word); while one shows, the scroll-in messages, the hint box and the prompts
are hidden. The uncuffing shows it ([Crimes](crimes.md#coneys-implementation)). **Coney's reading**: the bar's
back runs its whole width with the fill over it from the left end and the caps outside.

**The Warrior command menu** (`repo:src/hud/war_command_display.h`, stepped by
`repo:src/gamemodes/gameplay_war_commands.cpp`), as [the menu](#warrior-command-menu) says: each frame of play, before
the level's step, pad 1's R2 release gives the highlighted slot's command to the dispatcher (unforced, through
`script::giveWarriorCommand`) and R2 held opens the menu for player 1 unless his menu is locked; the right stick's raw
bytes pick the slot (the 12,100 dead zone, the one-axis angle, the sectors, 11.25° of hysteresis, cue `0x20`); a lock
arriving while it is up ends it with no order, and a hidden HUD gives the order at once; the camera's right stick is
off (`FollowCamera::enablePadStick`) from the opening until 10 updates after the order; then the chosen plate fades
over 1,500 ms and the rest over 500 ms, the chosen slot blinking, until the name's `<DISPLAYTIME>` closes it. It draws
the six slots and the highlighted slot's `GSTRING.COMMAND` name (entry 7 for a disabled command) at x 0.5.
**Coney's readings**: player 1 is the war chief (`+0x3ac` is not kept) and his state flags do not hold the menu back; a
stick byte of 128 or more is right or down; the backing and plate are flat squares (the sprite word `0xd0100` is not
mapped); not built: the order on the pause menu's close, the close when the chief goes down, the two-player and 16:9
layouts. A script drives it as a player does: `press r2`, `stick right -70 70` (up-left, attack), `release r2`
(`repo:tests/hud/war_command_display_test.cpp`).

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

**The radar** (`repo:src/hud/radar.h`, drawn by `repo:src/platform/hud_layer.cpp`), as
[the radar on screen](#the-radar-on-screen) says: `GameplayMode` gives the HUD the level record's map (the world name's
sheet and arguments 13-15) and a locator for the blips' objects; the play mode gives each step player 1's feet,
heading and speed and the camera's heading. The disc is 32 segments of triangles over the whole map texture,
clamped as [the GS dumps](rendering.md#hud) show (the sheet's own flags say wrap) and with no alpha test (the GS's
test there keeps the failing pixels' colour), filled to 0.825 R in
`(191, 191, 191, 240)` and fading to R, drawn under every other HUD batch, and nothing without a map; the
zoom eases between `HUDRadarSetRange`'s radii by the speed and `HUDSetRadarZoomScale`; blips sit at
(right, forward) × 0.12 / zoom (on the edge beyond it), sized as `hud_radar_dot`'s particles (0.7 × each icon's
factor), white but the dealers' green icons 29-31, a new objective blinking for 100 updates; the player's arrow (icon
362, 0.03 wide, `(178, 178, 178)`) turned by his heading less the camera's, drawn as two triangles. **Coney's
stand-ins**: w = 1.33 (the measured disc) for the camera slot's value; `rest` 50 and `fast` 75 until a script sets
them; the disc colour's blue state and its blend are not built; enemies and police (types 6, 8) never show, as no
scanner marks them yet; `HUDAddRadarHuman` makes every human a Warrior's blip (type 7, icon 365); a blip whose
object the locator cannot find is skipped, not freed. Tests: `repo:tests/hud/radar_test.cpp`.

**Coney's stand-ins for the rings** (marked in the code): the blend state the world pass leaves is taken as alpha
blending with Z test and no Z write; flat shading gives a triangle its last vertex's colour; the blink runs on the game
clock, black first; the fight stance is a lock-on or a block; a hit's pulse is the health lost in the step; the
civilian's class byte is Rembrandt's 35; the camera's heading is that of its forward. Not built: a boss's three
bands, the Rumble team disc and pointer, two players' rings and icons.

**Coney's stand-ins** (marked in the code): text sizes read as the glyph height (`(0.04, 0.05)` as w × h, 0.05 as h);
the counter slots' `x0` 0 and lines at y 0.104 and 0.154, the count 0.022 right of its icon; handcuff and key icons
`part_page0` 31 and 34; the money's icon a `$`; the popups' places; the money cue once per count; the built-in
announcements' texts from `GSTRING.ANNOUNCE` by kind; a `<FREEZE>` hint shown for its time (at least 2 s) as the game
timer does not freeze yet; the handcuff counter counts inventory item 5 (`Human_GetCuffCount` is not traced); a prompt
wakes the panel when it contains `Spray`, `Flash`, `Blades` or `Give Mon` in any language;
the counter panels' texts right-aligned on x 0.96; player 1's other parts 0.09 right of player
0's; a HUD no level has set up (the debug pages, the tests) shown at start; no wasted/busted restore yet (Coney has
no death camera). The hub's HUD bindings (`repo:src/scripting/hub_world_bindings.cpp`):
`HUDEnableClubActionText` raises the prompt to y 0.125 (the other video modes' heights are not used); the action-cycle
animation (`HUDTurnOnActionCycleAnim`) draws player 0's button (`part_page0`, 0.1 high, grey 128, inferred: the
prompt widget's colour) centred on the prompt's anchor, its word swapping every `framesPerIcon` updates and blinking
`blinkFrames` on and off, and hides the text until a new or changed one;
`HUDShowMissionSelect` and `ShowGameStatsInterface` reach the front end, which has neither screen yet.

## Open questions

- Which character each name-banner sheet (records `0x1f`-`0x32`) names, beyond Rembrandt (`0x2f`).
- The handcuff and key counters' icon rectangles, and the counters' exact text offsets.
- The action prompts' text alignment and icon sprite; what `0x00225ff0` tests; a runtime look at a shown prompt.
- Which event raises each of the game's own hints (the 19 callers of `HintBox_QueueGameHint`).
- **The caption pager** (HUD `+0x18e60`, [GUI](gui.md#fn-subtitle)): which script or mode creates it and whose
  command 1 steps the captions (`0x001cb340`).
- Who sets HUD `+0x177a8` (the centred announcement while hidden).
- The radar: the active camera's slot `+0xc4` third value (`w`, measured about 1.33).
- The health rings: the shape of `part_page1` rectangle 1 (the ring's band), what the power class byte `+0x40` is
  meant as, the blend state the world pass leaves, and a runtime look at a target's ring, the rage-full flashes and a
  boss's bands.
- The target panel's two sprites and the caller `0x00210e48`'s conditions; a runtime look at the panel.
- Human state flag `0x200000`, which turns the banner blue-grey.
- The layouts of the other video modes (16:9, progressive, PAL) that `0x00211ef8`, `0x001af010` and `0x001cdc80` apply
  (the radar's are [above](#the-radar-on-screen)).
- The counters' text offset.
- The money's icon (rectangle and sheet) and where the score's and the money's popups sit.
- The size arguments of the HUD's texts: a glyph's height, or `(w, h)`, or a `Font_Size` scale.
- Whether the built-in announcements (`0x00622e20`) are `GSTRING.ANNOUNCE`.
- Whether the money's cue `0x10` plays once or every counting frame.
- The counter panels' text layout and alignment, and a bar panel's look.
- Player 1's score, money and counter offsets.
- The scripted bars ([above](#scripted-bars)): which sheet and rectangles kind 0's sprite word `0x020a0000`
  names, kind 3's bar size, and the depths of the chase gauge's three sprites (Coney draws kind 0 with the generic
  meter, kind 3 at kind 1's size and the gauge in one batch).
