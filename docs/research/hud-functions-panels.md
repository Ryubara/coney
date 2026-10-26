# HUD function index: message, mission select and panel files

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 as on [The in-game HUD](hud.md), which this page was split from because that page had grown too
large. The overview, the data tables and the rest of the behaviour stay on [The in-game HUD](hud.md).
Every claim keeps the evidence level it had there.

This page covers the function index for the files from `GUI/MessageHUD.cpp` to the player panel and the game-state functions.

## After `GUI/MessageHUD.cpp` (no path string): the status column {#fn-after-messagehud}

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

## `GUI/MissionSelectHUD.cpp`: the money counter and the mug meter {#fn-missionselecthud-hud}

The rest of `MissionSelectHUD.cpp` after the mission select ([Front end](frontend.md#fn-missionselecthud)), up to
its static-init stub `0x001c1688`: the player panel's money counter (`HudMoney`, vtable `0x0053a4b8`, panel
`+0x400`, [Score and money](hud.md#score-and-money)) and the mugging mini-game panel (`MugMeter`, panel `+0x3480`). The
mug meter's layout is a table of `0x90` bytes per player at `0x00622e50`, rebuilt from the base point
(`0x0050e8f4`, `0x0050e8f8`) and the video mode on every update.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001be970` | `HudMoney_Construct` | a `HudCounter` at `+0x60` and the floating `±$N` text at `+0x480` (also used by the Armies of the Night HUD) | confirmed (code) |
| `0x001bea00` | `HudMoney_Setup` | the counter in (99, 219, 75) at depth 11,000; the floating text in (190, 192, 166) at the per-player offset `0x0050e8c0` | confirmed (code) |
| `0x001bebe8` | `HudMoney_ReadValue` | the player's inventory item 2, clamped to -99..9999 | confirmed (code) |
| `0x001bec38` | `HudMoney_Reset` | shown value = money now, no floating text, the counting sound stopped | confirmed (code) |
| `0x001becb0` | `HudMoney_Update` | [Score and money](hud.md#score-and-money): the `±$N` line for 1,000 ms, counting by (difference / 16) ± 1 per frame with cue `0x10`, grey leading zeros (`Hud_GreyLeadingZeros`) | confirmed (code) |
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

### The mug meter on screen {#mug-meter-layout}

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
  no flashing whatever the fill ([unused](hud.md#the-rage-meter)), and no trail. A player who is neither mugging, mugged
  nor in the meter's hold gets both fills 0 and nothing drawn.
- **Modes** (`MugMeter_SetMode`, from each state's update): 0 `Player_UpdateMugging` (mugging): bar 1 amber (128,
  100, 0), bar 2 red (170, 43, 43), arcs red, prompt `0x180`; 1 `0x00286550` (being mugged by the other player): the
  bar colours swapped, prompt `0x181`; 2 `0x002833c0` and 3 `Player_UpdateMugHold` (`0x00283a30`), the two sides of a
  hold (`Player_UpdateHold` picks by state flag `0x8000000000`): as 0 and 1 with blue (35, 83, 188) for red; what
  each game does and what its bars show is on [Combat: the other stick games](combat-throws.md#other-stick-games). The
  prompt's texts are `GSTRING.HUD` entries `0x180` and `0x181` (not reproduced here).
- The prompt is a `MessageHUD` (`MessageHUD_Setup(1.0, 1.0, …, font slot 3)`) whose alignment byte is never set
  (inferred: left, 0), so at x −0.01 it starts at the screen's left edge (inferred, not checked at run time).
- `HUD_MugMeterStart(0, 0)` also comes from the grab code (`0x002729a8`) when a mugging is set up, so the bars start
  empty.

## After `GUI/MissionSelectHUD.cpp` (no path string): bars and counter panels {#fn-hud-bars-panels}

The same unnamed unit as [`MultiLineTextWidget`](gui.md#fn-multilinetext) (to the stub `0x001c3dc0`): the
`HudGenericBar` class (vtable `0x0053a628`; its setup and draw are in [Scripted bars](hud-messages.md#scripted-bars)),
the score-board
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
| `0x001c2b48` | `CounterPanel_Setup` | the text item, and its bar: 0.22·s × 0.025·s (s as in [Scripted bars](hud-messages.md#scripted-bars)), (200, 200, 200), sprite word `0x30036`, at (0.805, 0.28), fill (128, 100, 0), background (32, 32, 32), right to left, label at (0.96, 0.28) | confirmed (code) |
| `0x001c2ce0`, `0x001c2d18` | `CounterPanel_IsShown`, `CounterPanel_SetYOffset` | by kind (`+0x440`): kind 3 asks its bar, the others the text (visible `+0x1bc`, y offset `+0x414`) | confirmed (code) |
| `0x001c2d50`, `0x001c2d90` | `CounterPanel_Update`, `CounterPanel_Render` | the bar's or the text's, by kind | confirmed (code) |
| `0x001c2dd0`, `0x001c2e38`, `0x001c2f30` | `CounterPanels_Construct`, `_Setup`, `_Shutdown` | the five panels; set-up from `HUD_InitLevel` (right-aligned texts, the column offsets `+0x3390` / `+0x3394` cleared); shutdown of those in use | confirmed (code) |
| `0x001c2f90` | `CounterPanels_Alloc` | `HUDGetNewPH(label, kind)`: the first free panel: kind, a copy of the label (`+0x444`, also the bar's label), the bar's width; returns its index, or -1 when all five are in use | confirmed (code) |
| `0x001c3088` | `CounterPanels_Release` | `HUDReleasePH`: hidden and free | confirmed (code) |
| `0x001c30a0` | `CounterPanels_ApplyLayout` | the column's constants per video mode (`0x0050e97c`-`0x0050e994`) | confirmed (code) |
| `0x001c3280` | `CounterPanels_SetValue` | `HUDSetPHValue`: `which` 1 or 2 sets the value or the maximum (2 only stores it); the text by kind, right-aligned, label then number: kind 0 `value%`, 1 `value`, 2 `value/max` (not shown while the maximum is 0); kind 3 sets the bar's fill and shows it for `ms`; then restacks the column and, when asked, plays `vags/interface/menu/bonuspart_01` | confirmed (code) |
| `0x001c3608` | `CounterPanels_Update` | from `HUD_Update`: stacks the shown panels down the right column and stores its bottom (`+0x3394`), below which the scripted bars go ([Scripted bars](hud-messages.md#scripted-bars)) | confirmed (code) |
| `0x001c37f8` | `CounterPanels_Render` | from `HUD_Render`: the shown panels | confirmed (code) |
| `0x001c3868`, `0x001c38d8`, `0x001c3978` | `PauseEffects_Construct`, `_Delete`, `_Release` | four HUD effects (`+0x10`, then three `0x30` apart from `+0x40`) built by `PauseMenu_Construct`; release frees those that exist | confirmed (code); what they show not traced |

## Before `GUI/RadarHUD.cpp` (no path string): the generic bar {#fn-generic-bar}

The class of the four generic bars (HUD `+0x9970`, `0x3f0` bytes each), [Scripted bars](hud-messages.md#scripted-bars).

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

## After `GUI/RadarHUD.cpp` (no path string): item and score counters {#fn-after-radarhud}

`0x001c68e8`-`0x001c7e40` hold the flash counter, the message box (on [Front end](frontend.md#fn-after-radarhud)) and
the score counter, in a file without a path string. The counters are parts of the player panel
([Item counters](hud.md#item-counters), [Score and money](hud.md#score-and-money)).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c68e8` | `FlashCounter_GetCount` | inventory item 1 (flash) of the player (`0x0041e420` on `W_GameState + 0x480`), clamped to 0-9 | confirmed (code) |
| `0x001c6928` | `FlashCounter_Init` | count `+0x420` from the getter | confirmed (code) |
| `0x001c6950` | `FlashCounter_Update` | a changed count clears panel `+0x411c` (the panel shows again); count ≥ 1 shows the widget with the number (`"%d"`), 0 hides it | confirmed (code) |
| `0x001c7730` | `ScoreCounter_Setup` | the score's markup text (`+0x40`, size 1.0, font 3) and the change text widget (`+0x230`), shadows `0x80`, the player `+0x360`, colour `+0x37c` | confirmed (code) |
| `0x001c7860` | `ScoreCounter_Release` | releases both texts | confirmed (code) |
| `0x001c7890` | `ScoreCounter_Sync` | shown and last score (`+0x368`, `+0x36c`) = the player's score now (`0x00422998` on the stats `0x006fe490`) | confirmed (code) |
| `0x001c7928` | `ScoreCounter_Update` | [Score and money](hud.md#score-and-money); a gain shows `"+N"`, or, while one of two bonus states of the stats object holds (`0x00422a40`, `0x00422a68`) and 350 ms have passed, the HUD string `0x183` or `0x184` (a bonus label); the change text grows to full size over 400 ms of its 1,000 ms (`0x0050ea3c`, `0x0050ea40`) | confirmed (code) |
| `0x001c7e40` | `ScoreCounter_Render` | both texts | confirmed (code) |

## `GUI/ScrollInHUD.cpp` {#fn-scrollinhud}

The scroll-in queue at HUD `+0x8dd0` (vtable `0x0053aa20`; base class at `0x001e6e98`, a markup text widget with
timing, which `TextProgressHud` shares). Behaviour: [Objectives](hud-messages.md#objectives-hudsetobjective). Queue
entries are
`0x34` bytes from a pool of 5 at `+0x440`: `+0x04` the text (header and text joined), `+0x1c`/`+0x20` the position,
`+0x24` the base size, `+0x28` the header kind, `+0x30` the time in ms. Confirmed (code) at `0x001c8b08`, `0x001c9028`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001c87f0`, `0x001c8838` | `ScrollIn_Construct`, `ScrollIn_Destroy` | base, vtable, nothing showing (`+0x68` = 0) | confirmed (code) |
| `0x001c8880` | `ScrollIn_Init` | from `HUD_InitLevel`: markup text set-up, the list (`STL_List(QueueElement*)`) and the 5-entry pool | confirmed (code) |
| `0x001c8aa0` | `ScrollIn_Shutdown` | frees list and pool | confirmed (code) |
| `0x001c8b08` | `ScrollIn_Queue` | [Objectives](hud-messages.md#objectives-hudsetobjective) | confirmed (code) |
| `0x001c8db0` | `ScrollIn_StampTime` | start time `+0x54c` = now | confirmed (code) |
| `0x001c8dc8`, `0x001c8e08` | `ScrollIn_ShowRaw`, `ScrollIn_ShowMessage` | start the front entry: with no text, or with its text, colour and sound (`+0x574`); wrap width 0.7 (`0x0050ea50`), 0.52 split (`0x0050ea4c`) | confirmed (code) |
| `0x001c8ea0` | `ScrollIn_ApplyVideoMode(scroll, kind)` | x offset `+0x560`: in the default mode 0 for header kind -1, 0.04 otherwise (other modes: 0.01-0.06 and a y at `+0x568`) | confirmed (code) |
| `0x001c9028` | `ScrollIn_Update` | [Objectives](hud-messages.md#objectives-hudsetobjective); in a split screen a kind-1 message moves 0.21-0.26 right by mode | confirmed (code) |
| `0x001c94c8` | `ScrollIn_Flush(scroll, text)` | `HUDFlushScrollIn`: ends the showing message when its text equals `text`, and drops queued entries whose text equals it | confirmed (code) |
| `0x001c9628` | `ScrollIn_Render` | plays the entry's sound once (`0x0010fcd8`), sets the wrap, draws | confirmed (code) |

## After `GUI/ScrollInHUD.cpp` (no path string): spray counter and the stereo-theft panel {#fn-after-scrollinhud}

The spray-paint counter, then the stereo-theft panel (two at HUD `+0xfea0`, `0xb10` bytes each, vtable `0x0053ab28`;
the game is [Combat: the stereo theft](combat-targets.md#stereo-theft)).

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

### The stereo panel on screen {#stereo-layout}

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
  [Combat](combat-targets.md#stereo-theft)). So the gauge grows from half size to full size and spins once per radian turned.
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

## After `GUI/SubTitle.cpp` (no path string): the tagging panel {#fn-after-subtitle}

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
  tagger's colour (human `+0x640`: its bytes 0-2 as r, g, b, by default (80, 7, 5); [Crimes](crimes.md#tag-look))
  at alpha 205;
- the **cursor**: rectangle 63, size 0.04, at the tag's `+0x4f0`, two **floats** (x, y) in grid cells, so it moves
  smoothly between cells and is not snapped (inferred: the stick-driven cursor of [the stick game](crimes.md)). Its
  colour fades between white and black, alpha 255, over 250 ms, reversing every 250 ms (game clock mod 500,
  `Colour_LerpRatio` `0x00338240`), and is plain white while the cursor is **on track** (`TagGame_IsOnTrack`,
  `0x002748a0`, the game's `+0x500`). It is not a pause flag: the cursor flashes while off the path and through a
  slip's pause ([Crimes](crimes.md#tag-coney-fixes));
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

## After `GUI/TextEntryPad.cpp` (no path string): text progress, stopwatch and hint texts {#fn-after-textentrypad}

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
| `0x001cd8e0`, `0x001cd930` | `HintBox_Construct`, `HintBox_Destroy` | the hint box ([Hints](hud-messages.md#hints-hudsettutorialtext)): markup text subclass (vtable `0x0053aff0`) with its box sprite at `+0x2a0`; the code belongs to `TutorialHUD.cpp` (inferred, it sits just before that file's anchor) | confirmed (code) |

## `GUI/TutorialHUD.cpp` {#fn-tutorialhud}

`0x001cd988`-`0x001cfea0`: the hint box (HUD `+0x8a10`, class vtable around `0x0053b000`: `+0x20` update, `+0x28`
render), then `UsageInfo` ([GUI](gui.md#fn-usageinfo)) and the Armies of the Night game-over screen
([Pause](pause.md#an-game-over)), which share the TU: the static-init stub `0x001cfea0` ends it (inferred,
[Source map](source-map.md#method)). The hint box's behaviour is on [Hints](hud-messages.md#hints-hudsettutorialtext).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001cd988` | `HintBox_Setup` | from `HUD_InitLevel`: layout pointer `+0x3a4` = `0x0050eb50`, the markup text in `0x005fd310`, the box sprite `+0x2a0` (`part_page0` rectangle 78, depth 11,000), the queue list `+0x290` and a free list of 20 entries (`+0x294` over the pool `+0x1f0`, 8 bytes each), text width 0.56 (`0x001bb390`), showing priority `+0x3ac` = 0 | confirmed (code) |
| `0x001cdc20` | `HintBox_Shutdown` | from `HUD_ShutdownLevel`: clears the text, frees both lists, clears the tutorial callback name `+0x3a0` | confirmed (code) |
| `0x001cdc80` | `HintBox_Update` | takes the next hint, lays the box out ([Hints](hud-messages.md#hints-hudsettutorialtext)) | confirmed (code) |
| `0x001ce1e8` | `HintBox_QueueGameHint` | queues `CfgTutorialMessage` entry `id` at priority 2 | confirmed (code) |
| `0x001ce218` | `HintBox_InsertByPriority` | sorted insert, first-in first-out within a priority; dropped when the pool is empty | confirmed (code) |
| `0x001ce3c0` | `HintBox_Queue` | `HUDSetTutorialText`; a more urgent hint pushes the showing one back | confirmed (code) |
| `0x001ce550` | `HintBox_Contains` | `HUDCheckTutorialText` | confirmed (code) |
| `0x001ce5c0` | `HintBox_Withdraw` | removes one hint by its text pointer | confirmed (code) |
| `0x001ce718` | `HintBox_WithdrawGameHint` | withdraws a `CfgTutorialMessage` entry | confirmed (code) |
| `0x001ce748` | `HintBox_FlushPriority` | `HUDFlushTutorialText(p)`; 4 removes all | confirmed (code) |
| `0x001ce8b8` | `HintBox_Render` | from `HUD_Render`: box alpha = 125 (`0x0050ebd4`) × the text's alpha / 255, draws the box, re-applies the text width 0.74 (0.52 split, `0x0050ebd8` / `0x0050ebd0`) and colour `0x005fd310`, draws the text | confirmed (code) |
| `0x001ce9a8` | `Tutorial_CallCallback` | [The tutorial callback](hud-messages.md#tutorial-callback) | confirmed (code) |
| `0x001cea38` | `Tutorial_IsHintUnlocked(id)` | true when the unlockables manager (`0x006fe998`) has an unlocked type-11 record with data `id` ([Unlockables](player-state.md#unlockables)); the 25 callers test it before queueing a game hint | confirmed (code) |

## After `PM_TooManyProfiles.cpp` (no path string): the player panel base and the target panel {#fn-player-panel-base}

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
| `0x0020dc98` | `PlayerHUD_SetBanner` | slot `+0x30`: [The name banner](hud.md#the-name-banner) | confirmed (code) |
| `0x0020dfd0`, `0x0020dfd8`, `0x0020e168` | `PlayerHUD_ReportBars`, `PlayerHUD_OnAttach`, `PlayerHUD_EmptyHook` | empty: the health and power reports (`HUD_ReportHealth` / `_ReportPower`), `HUD_AttachPlayer`'s hook, and the last call of both panels' Init. Not needed: no effect | confirmed (code) |
| `0x0020dfe0` | `PlayerHUD_SetMayShow` | `+0x4108` = flag (`HidePlayerHud` / `ShowPlayerHud`, [Showing and hiding](hud.md#showing-and-hiding)) | confirmed (code) |
| `0x0020dfe8`, `0x0020e008` | `PlayerHUD_HideCall`, `PlayerHUD_ShowCall` | one-line callers of hide and show (from `HUD_AttachPlayer`, `HUD_Hide`, `HUD_Restore` and the both-panels pair) | confirmed (code) |
| `0x0020e028` | `PlayerHUD_Show` | when attached and allowed (`+0x4108`): shown `+0x40fc` = 1, the money counter (`+0x3454`) and the banner (`+0x3ce8`) visible | confirmed (code) |
| `0x0020e058` | `PlayerHUD_Hide` | when attached, unless an Armies of the Night level with game state `+0x14c` = 1: banner and panel hidden, money counter hidden and reset; sets `0x0050b1b0[player]` when `+0x3444` is set (read by the HUD, not traced) | confirmed (code) |
| `0x0020e0f8` | `PlayerHUD_IsLoaded` | slot `+0x90`: the three banner sprites' sheets resident | confirmed (code) |
| `0x0020e170`, `0x0020e210` | `TargetPanel_Construct`, `TargetPanel_Destroy` | the widget base and six `{target handle, sprite word}` pairs at `+0x44` cleared (the empty handle `0x006ebd30`, word -1); destructor slot `+0x60` | confirmed (code) |
| `0x0020e238` | `TargetPanel_Setup(panel, player)` | from `ANHud_Init`: the player's two slots (`0x0063f240` + player × `0xd00` + slot × `0x680`): picture (`0x1e001e`) and frame (`0x1e001f`, (35, 83, 188)) sprites, and a `HudGenericBar` (background (37, 37, 37), sprite word `0x30036`, label `"temp"`, gradient green to green) at the [target panel's layout](hud-radar.md#the-target-panel) | confirmed (code) |
| `0x0020e5b8` | `TargetPanel_Shutdown` | slot `+0x68`: both slots' sprites and bars released, the pairs cleared | confirmed (code) |
| `0x0020e6f8` | `TargetPanel_Update` | slot `+0x30`, from `ANHud_Update` only: [The target panel](hud-radar.md#the-target-panel) | confirmed (code) |
| `0x0020fbc0` | `TargetPanel_Render` | slot `+0x38`, from `ANHud_Draw`: each used slot's frame, picture and bar | confirmed (code) |
| `0x0020fc98` | `TargetPanel_SetBossTexture(panel, handle, word, on)` | on: the pair `{handle, word}` into the first free of the six; off: the handle's pair cleared. The pairs give a target its picture (inferred from the name and the sprite word) | confirmed (code) |

## After `PM_TooManyProfiles.cpp` (no path string): the Armies of the Night panel {#fn-anhud}

`0x0020fd40`-`0x00211c80`: **`ANHud`**, the player panel of the Armies of the Night arcade levels (60-69). It has
`PlayerHUD`'s layout up to `+0x4120` (the same constructor calls) and its own interface vtable `0x0053ecc0`;
`HUD_InitLevel` allocates one per player in those levels instead of using the static `PlayerHUD`s. Unlike
`PlayerHUD` it **shows health and power** as two bars, a portrait and the [target panel](hud-radar.md#the-target-panel).
Values in the default mode, from the layout table `0x0050f7a0` (`0x120` bytes per player, copied from `0x00559810`
by `ANHud_ApplyVideoMode`); positions are offsets from the panel base `+0x10`: player 0 (0.08, 0.03), player 1
(0.80, 0.03).

| Part | Offset in the table | Player 0 | What it shows |
| --- | --- | --- | --- |
| power bar (`HudBar` at panel `+0x00`) | `+0x60`, size `+0x70` | (-0.018, 0.078), 0.35 × 0.009 | power % (`0x00222f48`: record `+0x148` over its maximum) / 100; background (37, 37, 37), fill white |
| health bar (`HudBar` `+0x4330`) | `+0xa0`, size `+0xb0` | (-0.018, 0.06), 0.35 × 0.024 | health / maximum (`0x00222e40`, `Human_GetMaxHealth`); background (37, 37, 37), fill green (115, 183, 11) |
| banner and two black shadows | `+0x40`, size `+0x50` | (-0.018, 0.02), 0.055 | the [name banner](hud.md#the-name-banner): player 0 tinted (150, 30, 30), player 1 (245, 184, 0); the banner `0x210000` (100, 100, 200, 160) |
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
| `0x002107e8` | `ANHud_Init(panel, player)` | slot `+0x10`: the two bars (sprite word `0x30036`, the health bar with the gradient), the score counter (`+0x70`), `TargetPanel_Setup`; then hidden, with the join flag `+0x4614` cleared | confirmed (code) |
| `0x004ec650` | `ANHud_SetPlayer(panel, player)` | slot `+0x80`, from `HUD_InitLevel` for each new panel: `+0x40f4` = player and the join flag `+0x4614` = 1, which `ANHud_Init` clears on the first attach ([Armies: the join messages](armies.md#the-join-messages)) | confirmed (code), confirmed (runtime) |
| `0x00210b80` | `ANHud_SetBanner` | slot `+0x30`: `PlayerHUD_SetBanner`, then the portrait (depth 14,000) and its frame | confirmed (code) |
| `0x00210d10` | `ANHud_Shutdown` | slot `+0x18`: the base shutdown, the portrait and frame, the score counter, the join text and the target panel | confirmed (code) |
| `0x00210dc0` | `ANHud_SetBossTexture` | slot `+0x70`: `TargetPanel_SetBossTexture` | confirmed (code) |
| `0x00210de0` | `ANHud_IsLoaded` | slot `+0x90`: banner, portrait and frame sheets resident | confirmed (code) |
| `0x00210e48` | `ANHud_Update` | slot `+0x40`: the join text while not wanted; once loaded and shown, the parts above each frame, then `TargetPanel_Update` | confirmed (code) |
| `0x00211938` | `ANHud_Draw` | slot `+0x38`: when shown, wanted and loaded: score, banner, the power bar (on and off for `+0x4114` frames each when set, like [`FlashRageBar`](hud.md#the-rage-meter)), the health bar, frame, portrait and target panels; else the join text. A player who is down (`0x00227dd8`) drops wanted | confirmed (code) |
| `0x00211a90`, `0x00211c80` | `ANHud_StaticInit`, `ANHud_StaticInitStub` | static initialiser (ctor list `0x00534148`): the colours `0x0063f1f8` (37, 37, 37), `0x0063f200` (170, 43, 43), `0x0063f208` (115, 183, 11), `0x0063f210` white, `0x0063f218` (35, 83, 188), `0x0063f220` (150, 30, 30), `0x0063f228` (245, 184, 0), `0x0063f230` (170, 43, 43), `0x0063f238` (128, 0, 0, 111), and the four target-panel slots | confirmed (code) |

## The player panel (no path string) {#fn-player-panel}

`0x00211ca0`-`0x00214ce8`: **`PlayerHUD`** (interface vtable `0x0053edf8` at `+0x4120`), the two static panels at HUD
`+0x19130` ([The player panel layout](hud.md#the-player-panel-layout-0x0050fa10)); its own static initialiser
(`0x00214ce8`) ends the file.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00211ca0` | `PlayerHUD_Construct` | from `HUD_Construct`: the panel's parts, then nine tally sprites at `+0x4150` | confirmed (code) |
| `0x00211ef8` | `PlayerHUD_ApplyVideoMode` | patches the layout table per video mode ([Coordinates](hud.md#coordinates)) | confirmed (code) |
| `0x00212840` | `PlayerHUD_Init(panel, player)` | slot `+0x10`: builds the panel from the layout table | confirmed (code) |
| `0x00213060` | `PlayerHUD_SetupTallyMarks(panel, player)` | slot `+0x58`, once (`+0x4134`): the nine tally sprites (word `0x16e`, every fifth `0x16f`) from the table's `+0x1f0` and size `+0x210` | confirmed (code) |
| `0x002131f0` | `PlayerHUD_Shutdown` | slot `+0x18`: the base shutdown, tally off (`+0x4130`), the Warrior command display, the tally sprites released | confirmed (code) |
| `0x00213290` | `PlayerHUD_Render` | slot `+0x38`: fade, rage flashing, the parts ([The rage meter](hud.md#the-rage-meter)) | confirmed (code) |
| `0x00213718`, `0x00213770` | `PlayerHUD_FindCounterSlot`, `PlayerHUD_UpdateCounters` | the [item counters](hud.md#item-counters): the slot (of four, `0x0050fee0` + player × `0xc0`) holding an item or the first free one; the counters' slots, places and counts | confirmed (code) |
| `0x00213e68` | `PlayerHUD_LayoutTallyMarks(panel, shifted)` | `+0x413c` marks from the origin `+0x40e0` with the table's `+0x1f0`-`+0x230`, every fifth crossing the four before, shifted below the counters ([The panel's tally](hud-functions-lua.md#panel-tally)) | confirmed (code) |
| `0x00214138` | `PlayerHUD_Update` | slot `+0x40`: values, colours and positions each frame ([The rage meter](hud.md#the-rage-meter) and the sections after it) | confirmed (code) |
| `0x00214bc8`, `0x00214c28` | `PlayerHUD_RefreshTallyMarks`, `PlayerHUD_RenderTallyMarks` | slot `+0x60`: when the tally is on, origin from `0x0050fa10`, count = standing members of gang `+0x4140` (`Gang_GetHeadCountById(gang, 1)`, `0x0016a458`, call `0x00214c00`; [what it counts](hud-functions-lua.md#gang-count-what)), laid out unshifted; slot `+0x68`: the marks drawn at full alpha. No caller found (inferred unused): `PlayerHUD_Update` and `PlayerHUD_Render` do the live work; `HUD_SetNumIndicator` turns the tally on ([The panel's tally](hud-functions-lua.md#panel-tally)) | confirmed (code) |
| `0x00214cb0`, `0x00214ce8` | `PlayerHUD_StaticInit`, `PlayerHUD_StaticInitStub` | static initialiser (ctor list `0x0053414c`): the colour `0x00640c40` = (128, 0, 0, 111) | confirmed (code) |

## Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0041cc10` | `WarCommandDisplay_ResetShown` | both players' shown command bytes (`+0x41c`, `+0x41d`) set to 7 (none); from `WarCommandDisplay_ShowCurrent` | confirmed (code) |
| `0x0041d988` / `0x0041d9a8` / `0x0041d9c8` / `0x0041d9e8` / `0x0041dad8` | `Cfg_SetHUDMessage` / `Cfg_SetTutorialMessage` / `Cfg_SetCrimeMessage` / `Cfg_SetHUDColour` / `Cfg_SetWarriorCommandText` | forwarders: `CfgHUDMessage` to `GlobalString_Set`, `CfgTutorialMessage` to `Tutorial_SetMessageText`, `CfgCrimeMessage` to `HudCrimePanel_SetMessage`, `CfgHUDColour` to `HudColourTag_Set`, the command text to `WarCommandText_Set` | confirmed (code) |
| `0x00422a40` / `0x00422a68` | `Stats_TakeStyleFlag` / `Stats_TakeBonusFlag` | the human's record's style / bonus changed flag, cleared on read; `ScoreCounter_Update` picks label `0x183` / `0x184` from them | confirmed (code) |
