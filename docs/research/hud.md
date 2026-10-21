# The in-game HUD

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made in
PCSX2 2.9.94 (2026-10-06) on a copy of quick-save slot 1 (`level99`, checkpoint 3), reading the HUD over PINE, writing
the player's rage, health and the panel's force-show flag, and measuring PCSX2's screenshots (1240 × 930 for the
game's 640 × 448); they say so.

This page was split because it had grown too large: the objective and hint messages, the action prompts and the
instruction arrow are on [HUD messages](hud-messages.md); the radar, the target panel and the health rings on
[HUD radar, target panel and health rings](hud-radar.md); the function index is in three pages by source file
([widgets and the HUD interface](hud-functions-parts.md), [the HUDLua bindings](hud-functions-lua.md) and
[the message, mission select and panel files](hud-functions-panels.md)). Every moved heading keeps a pointer here
under its old anchor.

## Purpose

What the HUD puts on screen during play, where, in which colours, and what drives each part: the player panel (name
banner, rage meter, score, money and item counters), the hint box, objective and announcement messages, the counter
panels, the instruction arrow, the radar's place on screen, the crew status list
([The crew status list](hud-functions-parts.md#crew-status)), and the calls that show and hide it all. The sprite and
text machinery underneath (widgets, sprite sheets, fonts, markup, draw order) is on [GUI](gui.md); the radar's blips
and icons are on [GUI: the radar](gui.md#radar-icons); the script bindings are in the
[HUD bindings](../references/bindings/hud.md).

In short: **health is not on the screen's edge but on the ground**: two flat rings at the player's feet, the outer one
showing health and the inner one power (or rage while raging), shown on SELECT, in a fight stance, at low health and
after a flash ([The health rings](hud-radar.md#the-health-rings)); the player's current target gets a health ring too.
The player panel at the top left is the character's **name banner**, tinted with the rage colour, a thin **rage meter**
under it, the **score** and the **money**, with up to four **item counters** (flash, spray paint, handcuffs, keys)
beside the money. It shows when something on it changes (or SELECT is pressed), stays 2 s and fades out over 1 s. Hints
sit in a dark box at the bottom left; objective and announcement messages appear at the same place and hide the hint box
while they show. The radar is a disc of the level's map at the bottom right; there is no full-screen map. START opens
the pause menu ([Pause menu](pause.md)).

## Original structure

Everything in this section is confirmed (code) at the cited addresses unless a line says otherwise.

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
| `0x001ce3c0` / `0x001ce218` | `HintBox_Queue` / `HintBox_InsertByPriority` | queue a hint by priority ([Hints](hud-messages.md#hints-hudsettutorialtext)) | confirmed (code) |
| `0x001ce1e8` / `0x001ce718` | `HintBox_QueueGameHint` / `HintBox_WithdrawGameHint` | the game's own hints, by `CfgTutorialMessage` id | confirmed (code) |
| `0x001ce5c0` / `0x001ce748` / `0x001ce550` | `HintBox_Withdraw` / `HintBox_FlushPriority` / `HintBox_Contains` | remove one hint, flush by priority, `HUDCheckTutorialText` | confirmed (code) |
| `0x001bb250` / `0x001bb2f0` | `MarkupText_SetText` / `MarkupText_IsExpired` | the markup text widget's text and its `<DISPLAYTIME>` / `<FREEZE>` end | confirmed (code) |
| `0x0019ef80` / `0x0019f1b0` / `0x0019f528` | `ActionPrompt_Setup` / `_SetText` / `_Update` | the action prompts at HUD `+0x6b40` ([Action prompts](hud-messages.md#action-prompts)) | confirmed (code) |
| `0x001b3cb0` | `HUD_ShowAnnouncement(hud, kind)` | a built-in announcement in the widget at `+0xe340` | confirmed (code) |
| `0x001ce9a8` | `Tutorial_CallCallback` | calls the `HUDSetTutorialCallback` function on each player-1 hit ([Tutorial callback](hud-messages.md#tutorial-callback)) | confirmed (code) |
| `0x001c8b08` / `0x001c9028` | `ScrollIn_Queue` / `ScrollIn_Update` | queued messages at HUD `+0x8dd0` | confirmed (code) |
| `0x001dad88` | `HUD_SetObjective` | `HUDSetObjective` | confirmed (code) |
| `0x001b7330` | `InstArrow_Update` | the instruction arrow at HUD `+0x134a0` | confirmed (code) |
| `0x001c3de0` | `Radar_Setup` | the radar's batches and map texture | confirmed (code) |
| `0x001c60b0` | `Radar_Render` | the radar disc ([GUI](gui.md#radar-icons) has the blips) | confirmed (code) |
| `0x0017bc28` / `0x0017b8a8` | `Im2D_DrawTexturedDisc` / `_Ring` | a textured disc, and a ring with a fading edge | confirmed (code) |
| `0x0020e6f8` | `TargetPanel_Update` | the target's name and bar | confirmed (code) |
| `0x00221108` | `Human_ReportBarsToHUD` | sends health and power fractions to the HUD, which ignores them | confirmed (code) |
| `0x0024b780` | `Reticules_Update` | per update: who gets a health ring, with what alpha ([The health rings](hud-radar.md#the-health-rings)) | confirmed (code), runtime |
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
| `+0x6850` | the stopwatch, made at (0.5, 0.135) and moved by `StopWatchHud_ApplyVideoMode` (`0x001cd400`): on screen in the default mode its minutes form stands at the top right, right-aligned (confirmed (runtime) at the gym's heavy bag, [Workouts](workouts.md#hud)) | `0x001cd578` / `StopWatchHud_Update*` / `0x001bb1c0` |
| `+0x6b40`, `+0x70d0` | the [action prompts](hud-messages.md#action-prompts) | `ActionPrompt_Setup` |
| `+0x7660` | a widget (role not traced) | `0x001bb650` / `0x001bbbb0` / `0x001bb8a8` |
| `+0x8960` | the score board | `0x001c2638` |
| `+0x8a10` | the hint box | `0x001cd988` / `HintBox_Update` |
| `+0x8dd0` | the scroll-in queue, at (0.025, 0.6), colour (206, 206, 206, 206) | `0x001c8880` / `ScrollIn_Update` |
| `+0x9350` | a markup text (role not traced) | `0x001acc60` / `0x001b9548` / `0x001bb1c0` |
| `+0x9540` | a per-player widget drawn for humans in state `0x002238c0` | `0x001cb6f0` / `0x001cb798` |
| `+0x9640` | a widget active while `+0x964c` | `0x001a4348` / `0x001a43d8` |
| `+0x9970` | the four generic bars of `HUDEnableBar` kinds 0, 1 and 3 (`0x3f0` bytes each, `0x0060a1b0`) | [Scripted bars](hud-messages.md#scripted-bars) |
| `+0xa930` | how many generic bars are shown (split between the two columns in split screen) | |
| `+0xa940` | the kind-2 gauge (`0x0060b180`), active while `+0xa94c` | `0x001a3720` / `0x001a3990` / `0x001a3c00` |
| `+0xacb0` | the five counter panels (`HUDGetNewPH`), at (0.9, 0.4); `+0x3394` the stack's height | `0x001c2e38` / `0x001c3608` / `0x001c37f8` |
| `+0xe050` | the [spinner](#hud-spinner) | `0x001ae378` |
| `+0xe150` | the centred announcement (markup text, colour `0x6060bfff`) | [Announcements](hud-messages.md#announcements-and-other-messages) |
| `+0xe340` | the announcement (markup text, white) | `HUD_ShowAnnouncement` |
| `+0xe530` | the RM_Intro screen | `HUD_OpenRumbleIntro` |
| `+0xe650` | an Armies of the Night widget (AotN levels only) | `0x0019e030` / `0x0019e758` / `0x0019ed28` |
| `+0xea30` | the credits | `Credits_Load` |
| `+0xebc0` + p × `0x430` | the mash meters | [Crimes](crimes.md) |
| `+0xf420` + p × `0x540` | the lock-picking dials | `0x001b7eb0` (size 0.158) |
| `+0xfea0` + p × `0xb10` | the stereo-theft widgets | |
| `+0x114c0` + p × `0x450` | a per-player widget (role not traced) | `0x001ab108` |
| `+0x11d60` | a widget (role not traced) | `0x001b75f0` / `0x001b7608` / `0x001b7890` |
| `+0x134a0` | the [instruction arrow](hud-messages.md#the-instruction-arrow-hudenableinstarrow) | |
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

Everything in this section is confirmed (code) at the cited addresses unless a line says otherwise.

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
| radar disc | `(191, 191, 191, 240)`, or blue `(100, 120, 200, 240)` ([below](hud-radar.md#the-radar-on-screen)) | `0x0050e9e8` |

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
0-1. Rage is human `+0x650` and the maximum the Warrior class's s16 `+0x00` ([Combat](combat-rage.md#rage)); 150 is the
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

Everything in this section is confirmed (code) at the cited addresses unless a line says otherwise.

A war chief (human `+0x3ac` = 1) orders his crew from a six-slot menu held open with **R2** and steered with the
**right stick**; releasing R2 gives the highlighted order. Each player panel owns one (`WarCommandDisplay`, panel
`+0x1ef0`, built by `WarCommandDisplay_Init`, `0x001a62c0`); the commands themselves are on
[AI: Warrior commands](ai-gangs.md#warrior-commands). Confirmed (code) at the addresses given unless marked. The
menu's input and issue steps are confirmed (runtime)
([`crew-command-menu`](../guides/pcsx2-recipes.md#crew-command-menu)): see the end of this section.

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
  command names follow [AI](ai-gangs.md#warrior-commands) (read from the lines, inferred).
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
([AI](ai-gangs.md#warrior-commands), step 1), though the last-command byte has already been written. **Only R2's release
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

**Seen at runtime** (PCSX2 2.9.129, level 99's street, a war chief with two Warriors; the
[`crew-command-menu`](../guides/pcsx2-recipes.md#crew-command-menu) recipe): with R2 held (pad command 1) and the right
stick at full deflection toward a slot, the display's shown flag (`+0x1554`) read 1 and its highlight (`+0x1540`) the
slot of the table above, for all six directions (60 degrees apart), each reached from a different slot; releasing R2
(command 2) issued the slot's command: the gang's tactic and the crew's goals changed to it. The street disables pad
commands 1 and 2 for the player (a disabled command is matched into the record's pending `+0x24`, so the human never
sees it): with only `WCEnableCommand` and `HUDShowWarCommand` run, R2 and the stick left the display hidden and the
tactic unchanged, and after `EnableCommand(player, 1, true)` and `(player, 2, true)` the same input gave the command.
A stick exactly up (raw 128, 0) left the highlight on slot 3 (down) in three runs of three; a raw x of 133 or 123 with
y 0 selected up (two runs each). Exactly down (128, 255) moved it from slot 1 to slot 3 in three runs of three. Why up
fails at x 128 and down does not is not traced.

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
id); entry 6 (`0x006007d8`), the **out-of-range** text, replaces it while the crew is out of range (game state
`+0x414` + player, set by the [range check](ai-gangs.md#warrior-range-flag); confirmed (runtime), below) and entry 7
(`0x006007dc`) while the highlighted command is disabled. Each is read every update while the menu is open and no
command has been issued yet (`WarCommandDisplay_Update`, `0x001a7e48`, which copies the byte to `+0x154c`); after an
issue the text is left as it was until it expires. **Runtime check** (PCSX2 2.9.94, a `level80` checkpoint state with
the `scripted-pad` and `right-stick` patches, R2 held, sticks at rest): with the byte left alone the menu named the
highlighted command; with the byte rewritten to 1 every few milliseconds the same menu showed the out-of-range text
in its place. Written once, the byte went back to 0 within a second: the player's update checks the range every 40
updates and clears it while a member is within 50 m. **Colours** (`WarCommandDisplay_Render`,
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
  -99..9999, counting toward a new value by (difference / 16) ± 1 per frame, and a floating `±$N` line
  (`<MONEYPLUS>` / `<MONEYMINUS>` markup) for 1 s after a change, during which the count waits. While it counts, the
  looping cue `0x10` plays, one voice kept by handle and stopped when the count ends ([Sound events: the money
  count's sound](sound-events.md#money-count-sound)). Confirmed (code) at `0x001becb0`; the sound's keep and stop
  confirmed (runtime). Hidden when 0 (inferred: the runtime panel with 0 dollars showed none).

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
| `+0x28` | 0.73 | a width matching the measured box (0.73 wide); the text itself wraps at the widget's own `+0x1d4`, 0.74 with one player ([Hints](hud-messages.md#hints-hudsettutorialtext); inferred: `+0x1d4` read 0.74 at runtime, and the `level99` fence-climbing hint is one line in a box about 0.726 wide, PCSX2 `slot07`) |
| `+0x40` | `0x80000000` | box colour, black at alpha 128 |
| `+0x50`, `+0x54` | -0.018, -0.035 | box offset from the text's top-left |
| `+0x60`, `+0x64` | 0.03, 0.035 | box size beyond the text's |
| `+0x70`, `+0x74` | 4,000, 200 | written every update but **read by nothing found** (the box's functions read only `+0x00`-`+0x64` through its pointer `+0x3a4`); a hint's time comes from its markup ([Hints](hud-messages.md#hints-hudsettutorialtext)) |

The box is the text's rectangle grown by those amounts and drawn first; the text is placed so that its bottom is at
`y` = 1.0 less the box's height. **Runtime check:** the box spans screen x 0.052-0.708 and y 0.812-0.923 (GUI x
0.00-0.73, y 0.84-0.97).

## Behaviour

### The HUD's frame

Everything in this section is confirmed (code) at the cited addresses unless a line says otherwise.

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

Everything in this section is confirmed (code) at the cited addresses unless a line says otherwise.

**Attach.** `Human_MakePlayer` (`0x00229c40`) calls `HUD_AttachPlayer` for the human: if panel `slot` (0 or 1) is not
attached yet, it runs its `Init` once (`0x00212840`), attaches it, sets the banner (`0x0020dc98`), and hides it again
when the HUD is hidden (`+0x177a0` = 0); it returns the slot, or -1. The panel index is kept at human
`+0x380`. Confirmed (code).

**Health and power are not shown on the panel** (they are on the ground: [The health
rings](hud-radar.md#the-health-rings)). Each frame `0x00221108` passes the player's health fraction (record `+0x144` /
`+0x146`) and power fraction (`+0x148` or `+0x14a` over their maxima) to `0x001b2430` / `0x001b2460`, which call
`0x0020dfd0`, a function that returns at once. This holds for `PlayerHUD`; the Armies of the Night panel draws a power
bar and a health bar ([`ANHud`](hud-functions-panels.md#fn-anhud)). Confirmed (code); confirmed (runtime): health 314
and 900 of 900 left the panel unchanged.

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

Moved to [HUD messages: objectives, hints, prompts and scripted bars](hud-messages.md#objectives-hudsetobjective).

### Hints (`HUDSetTutorialText`) {#hints-hudsettutorialtext}

Moved to [HUD messages: objectives, hints, prompts and scripted bars](hud-messages.md#hints-hudsettutorialtext).

#### The queue at runtime {#hint-queue-runtime}

Moved to [HUD messages: objectives, hints, prompts and scripted bars](hud-messages.md#hint-queue-runtime).

### The tutorial callback (`HUDSetTutorialCallback`) {#tutorial-callback}

Moved to [HUD messages: objectives, hints, prompts and scripted bars](hud-messages.md#tutorial-callback).

### Announcements and other messages

Moved to [HUD messages: objectives, hints, prompts and scripted bars](hud-messages.md#announcements-and-other-messages).

### Scripted bars (`HUDEnableBar`) {#scripted-bars}

Moved to [HUD messages: objectives, hints, prompts and scripted bars](hud-messages.md#scripted-bars).

### Action prompts {#action-prompts}

Moved to [HUD messages: objectives, hints, prompts and scripted bars](hud-messages.md#action-prompts).

#### The talk prompt {#talk-prompt}

Moved to [HUD messages: objectives, hints, prompts and scripted bars](hud-messages.md#talk-prompt).

### The instruction arrow (`HUDEnableInstArrow`)

Moved to [HUD messages: objectives, hints, prompts and scripted bars](hud-messages.md#the-instruction-arrow-hudenableinstarrow).

### The radar on screen

Moved to [HUD radar, target panel and health rings](hud-radar.md#the-radar-on-screen).

### The target panel

Moved to [HUD radar, target panel and health rings](hud-radar.md#the-target-panel).

### The health rings {#the-health-rings}

Moved to [HUD radar, target panel and health rings](hud-radar.md#the-health-rings).

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
| both [radar frames](hud-functions-parts.md#fn-radar-frame) (`+0x177d0`, `+0x18280`) | their parts off | their parts on (when set up, `+0xa98`) |
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

Everything in this section is confirmed (code) at the cited addresses unless a line says otherwise.

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

Everything in this section is confirmed (code) at the cited addresses unless a line says otherwise.

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
map](source-map.md#gui). The tables live on three pages: [HUD parts to `GUI/HUDInterface.cpp`](hud-functions-parts.md),
[`GUI/HUDLua.cpp`](hud-functions-lua.md) and [`GUI/MessageHUD.cpp` to the game state](hud-functions-panels.md); the
headings below point to them.

### Before `GUI/BaseWidget.cpp` (no path string): HUD parts {#fn-pre-basewidget}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#fn-pre-basewidget).

### After `GUI/BaseWidget.cpp` (no path string): mash meter and chase HUD {#fn-after-basewidget}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#fn-after-basewidget).

#### The mash meter on screen {#mash-meter-layout}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#mash-meter-layout).

### After `GUI/BaseWidget.cpp` (no path string): the chase gauge {#fn-chase-gauge}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#fn-chase-gauge).

### `GUI/ChecklistMessageHUD.cpp` {#fn-checklistmessagehud}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#fn-checklistmessagehud).

### After `GUI/ChecklistMessageHUD.cpp` (no path string): radar arcs, Warrior command display {#fn-after-checklistmessagehud}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#fn-after-checklistmessagehud).

### After `GUI/Credits.cpp` (no path string): the radar frame {#fn-radar-frame}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#fn-radar-frame).

### After the stub `0x001aad70` (no path string): item counters and status icons {#fn-hud-counters}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#fn-hud-counters).

### Before `GUI/HUDInterface.cpp`: the HUD object {#fn-before-hudinterface}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#fn-before-hudinterface).

### `GUI/HUDInterface.cpp` {#fn-hudinterface}

Moved to [HUD function index: widgets, radar frame and the HUD interface](hud-functions-parts.md#fn-hudinterface).

### `GUI/HUDLua.cpp`: the bindings' HUD calls (first part) {#fn-hudlua-start}

Moved to [HUD function index: the HUDLua bindings](hud-functions-lua.md#fn-hudlua-start).

### `GUI/HUDLua.cpp` {#fn-hudlua}

Moved to [HUD function index: the HUDLua bindings](hud-functions-lua.md#fn-hudlua).

#### The gang-count indicator {#gang-count-indicator}

Moved to [HUD function index: the HUDLua bindings](hud-functions-lua.md#gang-count-indicator).

#### What the count counts {#gang-count-what}

Moved to [HUD function index: the HUDLua bindings](hud-functions-lua.md#gang-count-what).

#### The panel's tally (players 0 and 1) {#panel-tally}

Moved to [HUD function index: the HUDLua bindings](hud-functions-lua.md#panel-tally).

#### The lock-pick dial layout {#lock-pick-dial-layout}

Moved to [HUD function index: the HUDLua bindings](hud-functions-lua.md#lock-pick-dial-layout).

#### The dial next to the original (2026-10-11) {#lock-pick-dial-compared}

Moved to [HUD function index: the HUDLua bindings](hud-functions-lua.md#lock-pick-dial-compared).

### `GUI/HUDLua.cpp`: the scripted bars {#fn-hudlua-bars}

Moved to [HUD function index: the HUDLua bindings](hud-functions-lua.md#fn-hudlua-bars).

### After `GUI/MessageHUD.cpp` (no path string): the status column {#fn-after-messagehud}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-after-messagehud).

### `GUI/MissionSelectHUD.cpp`: the money counter and the mug meter {#fn-missionselecthud-hud}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-missionselecthud-hud).

#### The mug meter on screen {#mug-meter-layout}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#mug-meter-layout).

### After `GUI/MissionSelectHUD.cpp` (no path string): bars and counter panels {#fn-hud-bars-panels}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-hud-bars-panels).

### Before `GUI/RadarHUD.cpp` (no path string): the generic bar {#fn-generic-bar}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-generic-bar).

### After `GUI/RadarHUD.cpp` (no path string): item and score counters {#fn-after-radarhud}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-after-radarhud).

### `GUI/ScrollInHUD.cpp` {#fn-scrollinhud}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-scrollinhud).

### After `GUI/ScrollInHUD.cpp` (no path string): spray counter and the stereo-theft panel {#fn-after-scrollinhud}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-after-scrollinhud).

#### The stereo panel on screen {#stereo-layout}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#stereo-layout).

### After `GUI/SubTitle.cpp` (no path string): the tagging panel {#fn-after-subtitle}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-after-subtitle).

### After `GUI/TextEntryPad.cpp` (no path string): text progress, stopwatch and hint texts {#fn-after-textentrypad}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-after-textentrypad).

### `GUI/TutorialHUD.cpp` {#fn-tutorialhud}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-tutorialhud).

### After `PM_TooManyProfiles.cpp` (no path string): the player panel base and the target panel {#fn-player-panel-base}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-player-panel-base).

### After `PM_TooManyProfiles.cpp` (no path string): the Armies of the Night panel {#fn-anhud}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-anhud).

### The player panel (no path string) {#fn-player-panel}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#fn-player-panel).

### Game-state functions {#warriors-functions}

Moved to [HUD function index: message, mission select and panel files](hud-functions-panels.md#warriors-functions).

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
  `levelShutDown` (`HUD_ShutdownLevel`, `0x001ae980`) runs from `GameplayMode::endLevel`, the one teardown every way
  out of a level goes through (a debug jump, the story's next mission, a restart, leaving to the menus): every part
  the level set up or a script filled goes (the hints queued and showing, the tutorial callback, the objectives and
  their messages, the announcements, the prompts, bars, counters, stopwatch, mini-game panels and the radar's blips),
  so no hint or objective of one mission survives into the next. The level's start (`GameplayMode::enter`) also
  empties the [animation callbacks](characters.md#anim-callbacks). `repo:tests/gamemodes/story_start_test.cpp` checks
  every start path: a debug jump, a reload, the story's next mission, the pause menu's two restarts and a direct start
  (`--play-level`). After each, no hint, objective, tutorial callback or animation callback remains, and a text set on
  the new level's box shows at once. `repo:tests/platform/disc_level80_story_test.cpp` checks the same on the disc,
  jumping from the story through `level80`'s four checkpoints. **Coney's choice**: the game-state switches the HUD
  object holds for Coney (the game's tutorial hints, `W_GameState + 0x56e2`; the first-objective hint's flag), the
  shown flag and the spinner are kept, as they are not the level's; the next `levelSetUp` hides both anyway.
- **`hud::PlayerPanel`**: the [layout](#the-player-panel-layout-0x0050fa10), the banner by type, the
  [meter](#the-rage-meter) (caps and strips of `part_page0` 54-57, one-texel inset, the full meter's pulse and sound,
  `FlashRageBar`), the [score and money](#score-and-money) counting by a sixteenth plus one with their popups,
  the [counters and their slots](#item-counters), and the 2 s hold and 1 s fade. Activity: a changed value, SELECT, the
  force-show flag. The parts land within a pixel of the page's 640 × 448 figures (`tests/hud/player_panel_test.cpp`).
- **`hud::HintBox`**: the [hint queue](hud-messages.md#hints-hudsettutorialtext) of 20 sorted by priority, interrupt and
  restart, a hint's time only from its `<DISPLAYTIME>` (else until flushed), the box laid out from the
  [table](#hint-box-layout-0x0050eb50) and following the text's fade.
- **`ScrollInQueue`**, **`CounterPanels`**, the announcements and the action prompt: the objectives' messages at
  (0.025, 0.88); the five panels; the bottom-left and centred [announcements](hud-messages.md#announcements-and-other-messages),
  each replaced at once and timed by its markup; player 0's [prompt](hud-messages.md#action-prompts) at (0.5, 0.86)
  raised over the
  hint box or message. Nothing draws while `HudFrame::letterbox` is set. **Differs from the original** (issue #238,
  [Objectives](hud-messages.md#objectives-hudsetobjective)): mode 2 queues the heading and then the bare text as two
  messages of `ms` each, where the original queues nothing, flushes the objective's own message and, for slot 1 only,
  shows announcement 2; the queue never cuts the showing message short when a new one arrives and has no flush; mode 0
  puts `<CR>` between the heading and the text although the heading string already ends with `<CR3 0.01>`; the icon
  template has no `<MONEYFONT>` and no trailing space; and a text's leading icon tag (`<ROBJ_N>` and the rest) is not
  read. On the player's path, a completed objective's script runs mode 2 and then mode 0 for the next objective, so
  Coney shows "New Objective" with no text and then the completed text alone, about 8.5 s each, before the new
  objective's message.
- **The bindings** (`src/scripting/hud_bindings.h`): every HUD binding `level99` calls, through `BindingContext::hud`.
  `HUDSetTutorialCallback` keeps the name; the play mode calls that function after the level's step with the anim
  id of each hit player 1 struck in it, landed or blocked (`Fighter::strikes`). **Coney's choice**: called after the
  step, not from inside the damage step.
- **Sound**: the cues by the table `SoundCfgInterfaceSound` fills (the binding keeps each call for the HUD as well
  as filling the engine's table), and the named sounds, play through `audio::SoundPlayer`
  ([Sound](sound.md#interface-sounds)); silent without sound. A play returns the player's handle (`HudSound`), so the
  money count's looping cue `0x10` is kept while the count runs, started again if it was lost, and stopped when the
  count ends (`PlayerPanel`, [Sound events: the money count's sound](sound-events.md#money-count-sound)).
- **The action prompt** (`repo:src/gamemodes/gameplay_mode.cpp`, the level's part `PlayLevelMode::promptOffer()`):
  each frame of play player 0's text is chosen in `HUD_Update`'s order: nothing while he tags, plays a scene part,
  mugs, steals a stereo or picks a lock; a held human set up for interrogation gives `GSTRING.HUD` 0 (interrogate,
  whatever he carries), one with money or a pocket item who may be mugged gives
  `GSTRING.HUD` 1; else a knocked-out partner to revive gives 4 (`GameplayMode::revivableInReach()`: the nearest
  revivable human friendly to him within 3 m, knocked out (his health gone or a script's), not cuffed, in sight,
  while player 1 holds a flash; the states `0x80000000` and `0x100000000` are not modelled); else the context
  records by kind: an action object's own text (1), a pickable door 15 (2), a freed car stereo 16 (3), a dealer's
  offer (4: his type's prompt, 6 for the flash, while his goal registers it, from the greeting on, within 1.75 m).
  A prompt naming the dealers' goods wakes the player panel.
  Last, a [talkable](hud-messages.md#talk-prompt) Warrior or co-op partner within 1.5 m (`GameplayMode::talkPrompt()`)
  gives the swap string his brain's think set ([AI](ai.md#coney): `GSTRING.HUD` 0xc-0xe while one of the two holds
  an object), else his own text (`ScriptState::talkText`), else HUD string 9; triangle swaps ([AI](ai.md#coney)).
  The action object's
  second text
  (`SetMsgHandlerEx`'s `prompt2`)
  is queued as a priority-0 hint while it is in reach and withdrawn when it changes or leaves.
- **The prompt's button** (user item 9, "n Give Weapon", fixed): the layout (`repo:src/gui/text_layout.cpp`) gives
  `<MONEYFONT>` its rectangle base ([MONEYFONT](gui.md#moneyfont)), so the `<ST>` of `GSTRING.ACTIONBUTTON` draws
  `part_page0` rectangle 73, the triangle; the stick tags alternate their characters and the icon tags 23-31 are
  known (`repo:src/gui/markup.h`). Tests: `repo:tests/hud/hud_test.cpp` (a prompt starting with the action button),
  `repo:tests/gui/text_layout_test.cpp`, and the disc check `"[disc][text]"`, which finds the triangle in all three
  swap prompts (`0xc`-`0xe`) and a stick picture in the revive prompt 4.
- **The prompt beside a downed Warrior** (user item 8, "no revive prompt"): Coney's revive step read only a script's
  knock-out, so a Warrior downed in a fight (his health gone) gave no prompt; it now counts both, a flash is still
  needed, and d-pad left revives him ([Combat: revive](combat-rage.md#revive)). He must also be revivable (flag `0x4`),
  which every Warrior is from the moment he is made (`Human_Init`, [Combat: human flags](combat.md#human-flags)), in
  Coney too (`ai::startingFlags`). The disc test `repo:tests/platform/disc_revive_test.cpp` downs a `level80` Warrior
  with a strike (no script call), walks player 1 to him with the stick at 60 % and sees no prompt without a flash,
  string 4 with one, then the revive. The swap prompt
  (`repo:src/ai/swap_prompt.h`) has the blocked test, for the Warrior and the player alike, as far as Coney models
  the mask's states (knocked out, by health or by a script, arrested, wounded), so beside a downed Warrior no "Give
  Weapon" shows. A Warrior's brain that a script knocked out is off in Coney (BrDead), so its think clears the prompt
  instead (`Brain::think`). Still to do: the player's own knocked-out gate on the other prompt steps.
- **The stereo-theft panel** (`repo:src/hud/stereo_hud.h`, stepped by `PlayLevelMode::stepStereoPanel()`) as
  [the stereo panel on screen](hud-functions-panels.md#stereo-layout) says: up from the triangle press, the arrow round
  the backdrop's corners by stage, the gauge sized and turned by the angle turned and dropping by a doubling step while
  a stage is complete, the stick's four pictures and the turning ring, always mirrored (it never flickers); cue `0x22`
  per stage and `0x23` with the fourth; gone as the end clip starts. Turned sprites (the format-1 instances: this panel
  and the instruction arrow) go through `SpriteBatch::addSprite(sprite, rotation)`, a positive angle turning clockwise
  on screen as the original's widgets do. **Coney's stand-in**: the stick and ring are timed on the game clock, not the
  timer `0x0050b8b8`.
- **The mug meter** (`repo:src/hud/mug_meter.h`, stepped by `PlayLevelMode::stepMugMeter()`) as
  [the mug meter on screen](hud-functions-panels.md#mug-meter-layout) says, while player 1 mugs (mode 0): the two
  `part_page0` bars, the stick's ball and dot following the raw left stick, the three arcs rippling two updates each at
  the last direction the stick picked while it is on target, and `GSTRING.HUD` `0x180` (`0x181` for modes 1 and 3)
  above; while it shows, the scroll-in message, the hint box and the action prompt are hidden. **Coney's stand-ins**:
  bar 1 is the off-target time against the allowance (record `+0x0c`, what fails a player's mugging in Coney) instead of
  the time since the start against `+0x134`. Coney has no modes 1-3 (a player victim and the hold), which need two
  players and the hold's states.
- **The lock-pick dial** (`repo:src/hud/lock_pick_hud.h`, stepped by `PlayLevelMode::stepLockPickDial()`) as
  [the dial's layout](hud-functions-lua.md#lock-pick-dial-layout) says, while player 1 picks a lock: the three pins at
  the player's centre point at distance 1, turned by their angles (gone once every pin is done), then over every sprite
  the good and perfect wedges (whole rim vertices coloured, the next segment fading to clear) and the lock face, as
  32-segment fans in 640 × 448 pixels with the video mode's horizontal and vertical radii (the interlaced 4:3 mode in
  play, as the rest of the HUD). The shapes go in two batches sorted above all the HUD's others, so they lie on top of
  the pins as `Shape2D_DrawQueued` does.
- **The fixed-camera icon** (`repo:src/hud/fixed_cam_icon.h`) as [the icon](#hud-fixed-cam-icon) says: while player 1's
  camera is a locked one or camera switch 0 is off, pushing the right stick (a raw byte outside 64-176) shows
  `part_page0` rectangle 87 at (0.9, 0.64), fully opaque while held and fading over 1 s after; any other camera hides
  it. `HUDEnableFixedCamIcon` turns both players' off until the next level's set-up. Coney has no fixed, transition
  or rail camera, so only the locked camera and the switch show it.
- **The tagging panel** (`repo:src/hud/tag_hud.h`, set by `GameplayMode::updateTagPanel()`) as
  [the tagging panel](hud-functions-panels.md#fn-after-subtitle) says, while player 1 sprays: the path's dots but the
  last three, grey from 100 to 255, the painted cells in bytes 0-2 of his tag colour (`HuTagColor`; by default the dark
  maroon (80, 7, 5)) at alpha 205, the cursor between cells plain white while on track and fading white to black and
  back over 500 ms off it (and through a slip's pause, which keeps the flag), the charge bar (draining through pauses
  too) in the same colour shrinking down to its fixed bottom, and the frame at alpha 191; gone when the spray ends.
- **The gang-count indicator** (`repo:src/hud/num_indicator.h`) as [the
  indicator](hud-functions-lua.md#gang-count-indicator) says: the
  shared one (`HUDSetNumIndicator` player 2), in a level numbered 100 or more, draws the blue header (sheet-table record
  `0x20e`, French `0x20f`, German `0x210`) with its two black shadows at (0.06, 0.944) and the gang's living members as
  tally marks from x 0.125, turned 3.3 rad, recounted each update; German's layout moves both. **Coney's readings**:
  living is health left, which differs from the original: it counts standing members, `Gang_CountLiving(gang, 1)`,
  not down or dead and regardless of health ([what the count counts](hud-functions-lua.md#gang-count-what), issue #456);
  at most nine marks; the shadows sort at the banners' shadow depth. Players 0 and 1's go to their [panel
  tallies](hud-functions-lua.md#panel-tally) (`PlayerPanel::tallyMarkPlace()`): the same marks from the panel's base in
  the rage colour at the panel's fade, dropped below the money and counters.
- **In play**: the play mode steps the HUD with player 1's rage, score, money and item counts (flash, spray paint,
  handcuffs, keys from the inventory) and draws it over the frame; the story shares the
  flow's HUD with the scripts. A disc test (`[disc][hud]`, `repo:tests/platform/disc_level99_hud_test.cpp`) plays
  `level99` checkpoint 1 through `l99_c1` unskipped: hidden and nothing drawn under the bars, then shown, drawn and
  the first hint up, and a pause hiding and showing it. The debug menus' HUD page sets its values ([Debug menu](../guides/debug-menu.md#pages)).

**The mash meter** (`repo:src/hud/mash_meter.h`), as [the mash meter](hud-functions-parts.md#mash-meter-layout) says:
player 0's or 1's button sprite, the bar from its left end filled by the mash over its target and the L1 and R1 glyphs
alternating every 400 ms (the triangle blink for any other word); while one shows, the scroll-in messages, the hint box
and the prompts are hidden. The uncuffing shows it ([Crimes](crimes.md#coneys-implementation)). **Coney's reading**: the
bar's back runs its whole width with the fill over it from the left end and the caps outside.

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
mapped); the order is also given when play resumes under a closing pause menu (**Coney's reading**: any mode popped
over play) and the menu closes when the chief is knocked out; not built: the two-player and 16:9 layouts. A script
drives it as a player does: `press r2`, `stick right -70 70` (up-left, attack), `release r2`
(`repo:tests/hud/war_command_display_test.cpp`).

**The health rings** (`repo:src/hud/health_rings.h`, drawn by `repo:src/platform/play_level_world.cpp`), as
[the health rings](hud-radar.md#the-health-rings) say: each step after the HUD's, `hud::HealthRings` takes player 1 and
his target and queues the rings (nothing while the HUD is hidden: `HideHud`, a scene's start); the show list (SELECT
newly pressed, a fight stance or health at or below 20 % renewing it; 4 s hold, 0.5 s fades), `HuForceEnableReticule`
(`GameState::forceReticules`) at alpha 255, the target's outer ring fading in over 500 ms and the L1 marker under a
non-player target; the arcs from health, power or raw rage × the power class byte (`PowerClass::ringByte`), the colours,
the blink at or below 25 %, the rage-full flashes and the raging grey, the hit pulse (damage this step, 45 an update, at
most × 1.15), the start angle from the camera's heading. They are drawn after the blob shadows, each ring under where
its human is drawn this frame, as 64 flat-coloured triangles of `part_page1` rectangle 1 with fog off. A disc test
(`[disc][objects]`, `repo:tests/platform/disc_level99_world_test.cpp`) checks that SELECT brings up player 1's two rings
after `l99_c1`.

**The radar** (`repo:src/hud/radar.h`, drawn by `repo:src/platform/hud_layer.cpp`), as
[the radar on screen](hud-radar.md#the-radar-on-screen) says: `GameplayMode` gives the HUD the level record's map (the
world name's sheet and arguments 13-15) and a locator for the blips' objects; the play mode gives each step player 1's
feet, heading and speed and the camera's heading. The disc is 32 segments of triangles over the whole map texture,
clamped as [the GS dumps](rendering.md#hud) show (the sheet's own flags say wrap) and with no alpha test (the GS's test
there keeps the failing pixels' colour), filled to 0.825 R in `(191, 191, 191, 240)` and fading to R, drawn under every
other HUD batch, and nothing without a map; the zoom eases between `HUDRadarSetRange`'s radii by the speed and
`HUDSetRadarZoomScale`; blips sit at (right, forward) × 0.12 / zoom (on the edge beyond it), sized as `hud_radar_dot`'s
particles (0.7 × each icon's factor), white but the dealers' green icons 29-31, a new objective blinking for 100
updates; the player's arrow (icon 362, 0.03 wide, `(178, 178, 178)`) turned by his heading less the camera's, drawn as
two triangles. **Coney's stand-ins**: w = 1.33 (the measured disc) for the camera slot's value; `rest` 50 and `fast` 75
until a script sets them; the disc's state set by `Hud::setRadarTint` (blue while player 1 may hide,
[Stealth](stealth.md#hud-cue)) and blended over 500 ms; enemies and police (types 6, 8) never show, as no scanner marks
them yet; `HUDAddRadarHuman` makes every human a Warrior's blip (type 7, icon 365); a blip whose object the locator
cannot find is skipped, not freed. Tests: `repo:tests/hud/radar_test.cpp`.

**The radar frame** (`repo:src/hud/crime_panel.h`), as [the radar frame](hud-functions-parts.md#fn-radar-frame) says:
player 0's four arcs concentric with the disc, flat-coloured strips of 16 segments 54-57.6 pixels out (x 1.1 times), the
blue wanted pair growing up both sides from the bottom by the fraction of 10 s left on his gang's wanted time (full
above 0.9), eased 5 % an update and snapping on a jump over 0.5, drawn above 0.03; the orange second pair outside it (or
in its place) from the second timer (`CrimeReports::secondWantedFraction`, started by a gang's call for help). **Coney's
placement**: the easing runs in the HUD's update, not its draw. The arcs show only once the crime reports know player
1's gang (`CrimeServices::playerOneGang`, [Crimes](crimes.md#coneys-implementation)). **The crime message**
(`Hud::setWanted`): the crime report sends 7 and the wanted time's end `0xb`; 7-9 sound the alarm (interface cue 2) on
the first show of a wanted spell and copy a crime text other than the one held into the centred announcement; any other
message clears the flag and the held text, leaving the copy to its own display time. In play the level's crime services
pass player 1's messages on with `GSTRING.CRIME` (`CfgCrimeMessage`) of his last crime type (`GameplayMode`).

**The spinner** (`repo:src/hud/spinner.h`), as [the spinner](#hud-spinner) says: `part_page0` rectangle 92, 0.09 high,
upright, at (0.95, 0.83), drawn after the arrow while shown in the colour the last loading pulse left (the set-up's
grey before any); `Spinner::drawPulse` is the pulse, (221, 56, 56) fading out over 1.1 s and back over the next.
**Coney's stand-in**: the pulse runs on its caller's clock. The memory-card screen at start-up turns it on and pulses
it ([Level loading](level-loading.md#memory-card-screen)); a level's set-up (`Hud::levelSetUp`) turns it off again,
**Coney's stand-in** for the fade branch on the level's first fully faded frame. Coney has neither the preload indicator
nor the HUD's fade branch itself.

**Coney's stand-ins for the rings** (marked in the code): the blend state the world pass leaves is taken as alpha
blending with Z test and no Z write; flat shading gives a triangle its last vertex's colour; the blink runs on the game
clock, black first; the fight stance is a lock-on or a block; a hit's pulse is the health lost in the step; the
civilian's class byte is Rembrandt's 35; the camera's heading is that of its forward. A boss's outer ring (a fighter
whose class `+0x11b` is 13, outside a Rumble level) shows the three bands (`hud::bossBand`; **Coney's reading**: the
band's percentage is the health's, scaled by the class byte as the others). Not built: the Rumble team disc and
pointer, two players' rings and icons.

**Wrapping** (`repo:src/hud/hint_box.h`, `wrapText`) follows `MessageHUD_WordWrap` (`0x001bac20`,
[GUI](gui.md)): a break before the word that takes the line past the width, each line measured with its tags
stripped (a button icon takes no width), a leading `<AUTOINDENT f>` giving the width. The hint box wraps at 0.74, the
scroll-in messages at 0.7 (`0x0050ea50`); Coney has one player, so the split widths (0.52) are not used.

**Coney's stand-ins** (marked in the code): text sizes read as the glyph height (`(0.04, 0.05)` as w × h, 0.05 as h);
the count 0.022 right of its icon; handcuff and key icons
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

**The radar's range and the chase HUD** (2026-10-06): `HUDRadarSetRange` keeps both players' near and far radii
(`RadarState::nearRange`, `farRange`; 50 and 75 until a script sets them); `HUDSetChaseHUDState_DESTROY` clears the
chase HUD's flag. The radar's zoom eases between the two radii ([The radar](hud-radar.md#the-radar-on-screen)). **Coney's
stand-in**: the chase HUD is not built or drawn (no level script creates it), so its flag is only kept.

## Open questions

- [The crew status list](hud-functions-parts.md#crew-status): the fighting-far-off icon (rectangle `0x15`) was never
  seen at runtime; the knocked-out member's radar blip changed look at runtime, not traced; whether the player's own
  entry can ever show (the code does not exclude him).
- Which character each name-banner sheet (records `0x1f`-`0x32`) names, beyond Rembrandt (`0x2f`).
- The handcuff and key counters' icon rectangles, and the counters' exact text offsets.
- What `0x00225ff0` tests; a runtime look at a shown prompt (the alignment and the button glyph are answered:
  [Action prompts](hud-messages.md#action-prompts)).
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
  (the radar's are [above](hud-radar.md#the-radar-on-screen)).
- The counters' text offset.
- The money's icon (rectangle and sheet) and where the score's and the money's popups sit.
- The size arguments of the HUD's texts: a glyph's height, or `(w, h)`, or a `Font_Size` scale.
- Whether the built-in announcements (`0x00622e20`) are `GSTRING.ANNOUNCE`.
- The counter panels' text layout and alignment, and a bar panel's look.
- Player 1's score, money and counter offsets.
- The scripted bars ([above](hud-messages.md#scripted-bars)): which sheet and rectangles kind 0's sprite word `0x020a0000`
  names, kind 3's bar size, and the depths of the chase gauge's three sprites (Coney draws kind 0 with the generic
  meter, kind 3 at kind 1's size and the gauge in one batch).
- **The objective header's string** ([Objectives](hud-messages.md#objectives-hudsetobjective)): how `HUD_SetObjective` (`0x001dad88`)
  assembles it. The icon tags draw as their rectangles only inside `<MONEYFONT>` ([MONEYFONT](gui.md#moneyfont)), so the
  header must draw them there; whether the code puts `<MONEYFONT>` around the icon alone, where `<SIZE 0.8>` and
  `</SIZE>` sit, the gap between the icon and the heading (does string `0xe5`/`0xe6` start with a space, or is it the
  icon rectangle's width?) and whether slot 1's icon is also at 0.8 are not traced. Coney builds
  `<SIZE 0.8><MONEYFONT><YOBJ></MONEYFONT></SIZE>` / `<MONEYFONT><BOBJ></MONEYFONT>` (a marked stand-in, issue #235).
- **Mode 2's header string** ([Objectives](hud-messages.md#objectives-hudsetobjective), issue #238): whether the
  completion header uses the same HUD strings `0xe5`/`0xe6` as mode 0's "new objective" header or other ones (for
  example a "complete" heading), which of the two is picked by the slot, whether it takes the same colour slot (4 / 6)
  and size, and whether the interface cue `0x11` plays on it. Coney queues mode 0's own header, then the text, both for
  `ms`; the user sees the header read as a new objective, so this is probably wrong.
