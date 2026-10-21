# HUD function index: the HUDLua bindings

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 as on [The in-game HUD](hud.md), which this page was split from because that page had grown too
large. The overview, the data tables and the rest of the behaviour stay on [The in-game HUD](hud.md).
Every claim keeps the evidence level it had there.

## Purpose

This page covers the function index for `GUI/HUDLua.cpp`, with the gang-count indicator, the panel tally and the
lock-pick dial.

## Original structure

`GUI/HUDLua.cpp` holds the script bindings' HUD calls: one-line callees on the HUD object, the gang-count indicator,
the panel tally's layout and the scripted bars, in the address order of the sections below. The HUD's classes are on
[The in-game HUD](hud.md#original-structure).

## `GUI/HUDLua.cpp`: the bindings' HUD calls (first part) {#fn-hudlua-start}

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
| `0x001b4328`, `0x001b43a8` | `HUD_RadarOn`, `HUD_RadarOff` | `+0x177ac` (`0x00617fec`) = 1 or 0, then one player's radar, or both for 2 ([The radars across a scene](hud.md#radars-across-a-scene)) | confirmed (code) |
| `0x001b4438` | `HUD_SetNumIndicator(player, value, n)` | players 0 and 1: the panel's interface `+0x54(n)` and `+0x4c(value, 0 when n is -1)`; player 2: `0x001b6770` on the Rumble widget `0x00617370` | confirmed (code) |

## `GUI/HUDLua.cpp` {#fn-hudlua}

`0x001b44d8`-`0x001b8f78` (the static-init stub, [Source map](source-map.md#gui)): the C++ side of the HUD bindings
(each called from one tolua wrapper; the binding pages have the Lua side), then helper classes the HUD set-up builds:
an effect-task handle, the gang-count indicator (`NumIndicator`, vtable `0x00539f38`, HUD `+0x16b30`), the icon
counter (`HudCounter`, vtable `0x00539fd0`), the instruction arrow (`InstArrow`, a `BaseWidget` subclass, vtable
`0x0053a068`, HUD `+0x134a0`), the mission-item icons (vtable `0x0053a118`) and the lock-pick dial (vtable
`0x0053a1b0`, HUD `+0xf420` + player × `0x540`). The classes after `0x001b6000` have no allocator tag naming the
file; they sit before its stub, so the file is inferred. The bar functions (`HUD_EnableBar`, `0x001b4ba0`-`0x001b5450`)
are [listed with the bars](hud-messages.md#scripted-bars).

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
| `0x001b4980`, `0x001b49a8`, `0x001b49d0` | `Tutorial_QueueText`, `Tutorial_Contains`, `Tutorial_Flush` | `HintBox_Queue` / `_Contains` / `_FlushPriority` on the hint box `0x00609250` ([Hints](hud-messages.md#hints-hudsettutorialtext)) | confirmed (code) |
| `0x001b49f8`, `0x001b4a28`, `0x001b4a88` | `ChaseHud_SetNormal`, `ChaseHud_SetTimeout`, `ChaseHud_Destroy` | `ChaseHud_SetState(value, 0x00609e80, state, time)`: state 1 (100, 10), state 3 (`value`, negated when the sign argument is 0; then the label, `0x001a4310`), state 6 (100, 10) | confirmed (code) |
| `0x001b4ab8` | `ChaseHud_Create` | only while the chase HUD is not set up (`+0x0c` = 0): set-up `0x001a3d00(size 0.11, depth 11,000, at (0.9, 0.24), or x 0.72 when +0x40 is set, grey 128)` | confirmed (code) |
| `0x001b5590` | `HUD_EnableTextProgress(on, labels, count, flag)` | on: sets up `count` of the six score rows at `0x00615320` (`0x560`-byte markup text widgets, set-up flag `+0x434`) at (0.95, 0.22 + 0.06 × row), alignment 5, the label (`+0x540`) and a score of 0 (`+0x558`); off: shuts down every set-up row. The count goes to `0x00622e40` when `flag` is set, else to `0x00622e44` | confirmed (code) |
| `0x001b57c8` | `HUD_SetTextProgress(label, score, rgb, flag)` | the set-up row whose label matches gets the score and colour and is redrawn; then a selection sort of the first count rows (`0x00622e40` or `0x00622e44` by `flag`), highest score first, swapping labels, scores and colours | confirmed (code) |
| `0x001b5a90`, `0x001b5ae0` | `HUD_TurnOnActionCycleAnim`, `HUD_TurnOffActionCycleAnim` | `0x001b3dd0(0.1, HUD, {a3, a4}, 2, a1, a2, a5)` / `0x001b3df8(HUD, player)`: the action prompt's button-cycle animation ([Action prompts](hud-messages.md#action-prompts)) | confirmed (code) |
| `0x001b5b08` | `HUD_SetAnnounceMessage(kind, text, arg)` | kind 5: `MarkupText_SetText` on the custom widget `0x0060e990`, its flags `+0x4` and `+0x8` = 1; other kinds `HUD_ShowAnnouncement`; `arg` to `0x00617fe8` ([Announcements](hud-messages.md#announcements-and-other-messages)) | confirmed (code) |
| `0x001b5b80` | `HUD_SetANBossTexture` | both player panels' interface slot `+0x70` with the three arguments | confirmed (code) |
| `0x001b5c10` | `Cfg_SetAnnounceMessage` | forwards to `0x001acd20` | confirmed (code) |
| `0x001b5c30` | `HUD_EnableInstructionArrow(x, y, angle, on)` | enable flag HUD `+0x134a4`; when on, `InstArrow_SetRotation(angle)` and `InstArrow_SetBasePosition((x, 0, y, 1), 1)` ([The instruction arrow](hud-messages.md#the-instruction-arrow-hudenableinstarrow)) | confirmed (code) |
| `0x001b5cc8`, `0x001b5cf0` | `HUD_SetInstArrowAnimSpeed`, `HUD_SetInstArrowColour` | `InstArrow_SetBobFrames(frames)`; the arrow's colour (r, g, b, 255) (`0x001a2648`) | confirmed (code) |
| `0x001b5d38`, `0x001b5d78` | `HUD_SetInstArrowScreenPos`, `HUD_SetInstArrowOnObject` | `InstArrow_SetBasePosition((x, 0, y, 1), 1)`; or the object projected to the screen (`0x001c6878` with HUD `+0x15d0`), y + 0.03, converted 0 | confirmed (code) |
| `0x001b5dd8` | `HUD_CenterComponents(centre, n, widths, out)` | lays `n` widths side by side centred on `centre` and writes each item's centre; also `OptionGrid`'s centred rows and `RM_Intro` | confirmed (code) |
| `0x001b5e60` | `HUD_PackComponentsLeft(left, n, widths, out)` | lays them left to right from `left` with no gap and writes each item's **left edge** (`OptionGrid`'s left-packed rows) | confirmed (code) |
| `0x001b5e90` | `Tutorial_SetCallback` | the Lua reference to `0x006095f0` ([Tutorial callback](hud-messages.md#tutorial-callback)) | confirmed (code) |
| `0x001b5ea8` | `HUD_SetActionTextHigh(high)` | the action-prompt text's y (`0x00607d74`, copied to `0x006077e4`): 0.86, or 0.125 when high; PAL (device flag 2) 0.78 / 0.2; PAL 16:9 0.8 / 0.15; progressive 16:9 (`0x20` and 4, not PAL) 0.08 either way | confirmed (code) |
| `0x001b5f88`, `0x001b5fb0`, `0x001b5fd0` | `HUD_ShowRumbleModeIntro`, `HUD_PreloadCredits`, `HUD_ShowCredits` | `0x001b3880(HUD, a, b)`, `0x001b38b8(HUD)`, `0x001b38d8(HUD)` | confirmed (code) |
| `0x001b5ff0` | `GameState_SetTutorialText` | byte `W_GameState + 0x56e2` | confirmed (code) |
| `0x001b6000`, `0x001b6010`, `0x001b6078` | `HudEffect_Construct`, `_Delete`, `_Release` | an effect-task handle (used by the generic bars at `+0x70` and the radar): `+0x1c` the task, `+0x14` a PTank owned when `+0x20` is set, `+0x10`. Release kills the task (its slot `+0x48`) and the PTank and clears the fields; the deleting destructor releases, then frees when bit 0 of the flags is set | confirmed (code) |
| `0x001b6100`, `0x001b6120`, `0x001b6140` | `HudEffect_Show`, `_Hide`, `_Exists` | task message `0x29` (show) / `0x2a` (hide) to `+0x1c` (`0x001e9ae8`, `0x001e9b38`; [Particles](particles.md)); true when `+0x1c` is set | confirmed (code) |
| `0x001b6168` | `NumIndicator_ApplyLayout` | the [gang-count indicator](#gang-count-indicator)'s layout globals per video mode; German (`W_GameState + 0x120` = 4) adds 0.05 to the origin's x and 0.07 to the header gap | confirmed (code) |
| `0x001b6358` | `NumIndicator_Setup(ind, position, header)` | from the HUD's set-up (`0x001ad588`): the header sprite when given, two black shadows, nine tally sprites; count 9, gang −1, off | confirmed (code) |
| `0x001b66e0`, `0x001b6ba0` | `NumIndicator_Shutdown`, `NumIndicator_Render` | slot `+0x68`: shuts the header (when made) and the nine tally sprites down (slot `+0x70`); slot `+0x38`: when shown, the shadows, the header and `count` tally sprites | confirmed (code) |
| `0x001b6770` | `NumIndicator_Set(ind, on, gang)` | `HUDSetNumIndicator` for player 2 (`0x001b4438`; players 0 and 1 go to their panel's slots `+0x50` and `+0x48`): gang `+0xc60`, on `+0x04`, count `+0xc40` = the gang's **standing** members, `Gang_CountLiving(gang, 1)` (`0x001b6798`, `li a1,0x1` in the delay slot; [what it counts](#gang-count-what)) | confirmed (code) |
| `0x001b67b8` | `NumIndicator_Update` | slot `+0x30`: when shown, recounts (`Gang_CountLiving(gang, 1)` again, `0x001b68a4`) and lays out the tally marks ([below](#gang-count-indicator)) | confirmed (code) |
| `0x001b6c50` | `HudCounter_Setup(iconSize, iconDepth, c, position, textOffset, iconColour, textColour, metrics, sprite, instance, textFlags, anchor, fontSlot, visible, active, markup)` | an icon (`BaseWidget` `+0x300`) and a text: a `TextWidget` at `+0x40`, or with `markup` a markup text widget at `+0x110` (`+0x410` = markup); text at position + `*textOffset` (`+0x400`); used by the item counters ([Item counters](hud.md#item-counters)) | confirmed (code) |
| `0x001b6df0`, `0x001b6e40` | `HudCounter_Shutdown`, `HudCounter_SetPosition` | slot `+0x68` (shared with five subclass vtables): shuts the text and the icon down, `+0x0c` = 0; slot `+0x08`: text at pos + `+0x400`, icon at pos, pos to `+0x20` | confirmed (code) |
| `0x001b6ed8`, `0x001b6f60` | `HudCounter_Render`, `HudCounter_Update` | slot `+0x38`: when visible and shown, the icon then the text; slot `+0x30`: the text then the icon | confirmed (code) |
| `0x001b6fb0`, `0x001b70c0` | `InstArrow_Construct`, `InstArrow_Destroy` | a `BaseWidget` with bob frames `+0x100` = 10, step `+0x104` = 2.0, counter `+0x108` = 0, bob direction `+0x10c`/`+0x110`; the destructor is `BaseWidget`'s (`0x001a1c30`) | confirmed (code) |
| `0x001b70e8`, `0x001b71d8` | `InstArrow_SetRotation`, `InstArrow_SetBobFrames` | slot `+0x90`: rotation `+0xa0` = angle and direction (sin a, −cos a) / 20 / frames; frames `+0x100` and the direction again | confirmed (code) |
| `0x001b72d0` | `InstArrow_SetBasePosition(arrow, pos, convert)` | slot `+0x08`: base `+0x120` = pos (through `0x00122d48` when `convert`), the widget's position, `+0x130` = `convert` | confirmed (code) |
| `0x001b7330` | `InstArrow_Update` | slot `+0x30`: the bob ([The instruction arrow](hud-messages.md#the-instruction-arrow-hudenableinstarrow)) | confirmed (code) |
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
| `0x001b8428` | `LockPickDial_Show(dial, on)` | on: pins back to 3.14, the loop `vags/misc/lock_spins_01` started (handle `+0x450`), `vags/misc/click_open1` prepared (`0x00622e48`), pin 0, counters 0; off: both stopped; `+0x478` = on | confirmed (code) |
| `0x001b8530` | `LockPickDial_Draw` | slot `+0x30` (the update, 30 times a second: confirmed (runtime), [Lock picking](crimes.md#lockpick)): waits for residency; turns the current pin by 0.1 × its speed (wrapping in 0-2π); queues two band arcs and the textured centre disc (`0x0017c2a8`, the 2D shape queue `0x005fd9f0`, at most 6) at the player's layout ([Lock picking](crimes.md#lockpick)) | confirmed (code) |
| `0x001b8d38` | `LockPickDial_Judge` | 2 perfect (next pin, `click_01` and `click_open1`), 1 good (next pin, `click_open1`), 0 miss (`click_01`, every pin back to 3.14, pin 0) | confirmed (code) |
| `0x001b8ed0` | `LockPickDial_Render` | slot `+0x38`: while resident, shown and a pin is left (`+0x468` < 3), the three pins | confirmed (code) |
| `0x001b8f50`, `0x001b8f78` | `HUDLua_StaticInit`, `HUDLua_GlobalCtor` | the file's static initialiser (`0x00622e48` = −1) and its global-constructor stub (in the constructor list at `0x00534108`) | confirmed (code) |

### The gang-count indicator {#gang-count-indicator}

`HUDSetNumIndicator`'s shared indicator (HUD `+0x16b30`) counts a gang's standing members ([what it
counts](#gang-count-what)) as **tally marks**: four
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
  **0.048** for a stroke, 0.017 × 1.2 = **0.0204** for a crossing bar (`0x0050d5c0`, `0x0050d5c4` × `0x0050d604`; confirmed (code)).
- **Where mark *i* goes** (0-based, *i* < the standing count; confirmed (code)), in GUI units, `NumIndicator_Update`
  (`0x001b67b8`) with
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
- Only nine mark sprites exist (`+0x340`), but the loop lays out as many as the gang has standing members; a tenth
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
  `0x00214bc8`; the live path is `PlayerHUD_Update`, `0x00214138`) counts the gang's standing members into
  `+0x413c` and lays out the same pattern of strokes and bars
  (`PlayerHUD_LayoutTallyMarks`, `0x00213e68`, the nine sprites at `+0x4150`, in the player's colour) from the panel's
  origin (`+0x40e0`, the [panel base](hud.md#the-player-panel-layout-0x0050fa10) by panel layout `+0x40f4`) with the panel
  layout table's entries. No header is drawn there. See [the panel's tally](#panel-tally) for the numbers.

### What the count counts {#gang-count-what}

Every HUD count of a gang's members passes **1** as `Gang_CountLiving`'s second argument, so the marks show the
members that are **standing**, not every member that still exists. **Evidence:** confirmed (code) at each call
site below. Because both the indicator and the panel recount every update, a member who is knocked out or tackled
loses his mark while he is down, health or not: confirmed (runtime) on 2026-10-20, a WAR PARTY (Furies against
Orphans, five a side, every fighter on the AI) watched in PCSX2, the Orphans' indicator (`+0xc40`) dropped in the
same update as each member went down by state (5 to 4 to 3 to 2 over 4,500 updates) and showed 3 while all 5 still
had health, one of them tackled with 250 health left and one knocked out with 1. That he gets the mark back when he
is up again is inferred from the recount (not seen in that run).

| Call | Caller | Argument |
| --- | --- | --- |
| `0x001b6798` | `NumIndicator_Set` (`0x001b6770`), the shared indicator when it is turned on | `Gang_CountLiving(Gang_Get(gang), 1)`: `li a1,0x1` in the delay slot |
| `0x001b68a4` | `NumIndicator_Update` (`0x001b67b8`), each update while shown | `Gang_CountLiving(Gang_Get(gang), 1)`: `li a1,0x1` |
| `0x00214310` | `PlayerHUD_Update` (`0x00214138`), the panel tally of players 0 and 1 while `+0x4130` is on | `Gang_GetHeadCountById(+0x4140, 1)`: `li a1,0x1` at `0x00214314` |
| `0x00214c00` | `PlayerHUD_RefreshTallyMarks` (`0x00214bc8`, no caller found) | `Gang_GetHeadCountById(+0x4140, 1)`: `li a1,0x1` at `0x00214bf4` |

- `Gang_GetHeadCountById(id, living)` (`0x0016a458`, also behind the script's `GangGetHeadCount`): 0 when `id` is
  −1, else `Gang_CountLiving(Gang_Get(id), living)` (`Gang_Get`, `0x0016c388`) with `living` passed through
  unchanged. Confirmed (code) at `0x0016a458`. So the panel tally (a one-player brawl's gang 1 included) and the
  shared indicator count the same way.
- `Gang_CountLiving(gang, living)` (`0x00166158`) walks the gang's 16 member handles at gang `+0x48`. With
  `living` **equal to 1** it counts a slot when the handle resolves and `Human_IsDownOrDead` (`0x00227e60`) is not 1;
  with **any other value** it returns `HandleArray16_CountLive(gang + 0x48)` (`0x004dae00`), the handles that
  resolve, down or not. Confirmed (code) at `0x00166158`, `0x004dae00`.
- `Human_IsDownOrDead` (`0x00227e60`) is 1 when the human has no record (`+0xd4` = 0) or is in any of the states
  `0x180050000`: dead `0x100000000`, dying `0x80000000`, knocked out `0x40000`, tackled `0x10000`
  ([the gang pages](ai-gangs.md) name the bits). Confirmed (code) at `0x00227e60`. Health is not read.
- `Gang_CountStanding` (`0x00166220`, the script's `GangGetStandingCount` through `0x0016a4a8`) is a **different,
  stricter** count that the HUD does not use: a member counts when his handle resolves and he is not knocked out,
  not in state `0x100000000` and not wounded (three separate tests). Confirmed (code) at `0x00166220`.

### The panel's tally (players 0 and 1) {#panel-tally}

`PlayerHUD_LayoutTallyMarks(panel, shifted)` (`0x00213e68`) lays out mark *i* (0-based, *i* < the standing count
`+0x413c`) from the panel table `T` = `0x0050fa40` + layout × `0x240` (the table of [the panel
layout](hud.md#the-player-panel-layout-0x0050fa10)). Values read from the executable (the default mode; other modes
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
size is the table's times `0x00510050` = 1.0 (confirmed (code)). With the bases (0.10, 0.05) and (0.785, 0.05) the strokes of player 0
start at x 0.005 (bar at 0.025, the sixth stroke at 0.075) and player 1's at 0.779 (bar 0.799, sixth 0.85).

**The shift** moves the tally below the money and the item counters:

- `shifted` is 0 → no shift. `PlayerHUD_Update` (`0x00214138`, each update while the tally is on, `+0x4130`) passes
  what `PlayerHUD_UpdateCounters` (`0x00213770`) returned (confirmed (code)): **1** when the money (`+0x444`) is 1-999 or any item
  counter is shown, else 0 (money 0 or less, or 1,000 or more, with no counter).
- `shifted` is 1 → **0.09** (`0x005100a4`) when slot 2 or slot 3 of the [counter slots](hud.md#item-counters) sits on
  line 2
  (its y equals 0.146) and holds an item (its `+0x20` is not 4); otherwise **0.045** (`0x005100a0`). Confirmed
  (code).
- In a level numbered 100 or more (`0x0041d160`) a further **−0.03** (`0x005100c4`), whatever `shifted` is. Confirmed (code).

So player 0's marks sit at y 0.154 with nothing above them, 0.199 under one row of counters and 0.244 under two.

**Who turns it on:** `HUD_SetNumIndicator` (`0x001b4438`) through the interface slots `+0x48` (on, `+0x4130`) and `+0x50`
(gang, `+0x4140`). The interface's slot `+0x60` (`PlayerHUD_RefreshTallyMarks`, `0x00214bc8`, which lays out with
`shifted` 0) and slot `+0x68` (`0x00214c28`) have no caller found (no direct reference; inferred unused). The marks
are drawn by the panel's own render (`PlayerHUD_Render`, `0x00213290`) at the panel's fade alpha, while the tally is
on. Confirmed (code).

### The lock-pick dial layout {#lock-pick-dial-layout}

Per player (`0x0050d670` + player × `0x20`, 32 bytes): a GUI position (x, 0, y, 0), player 1 (0.09, 0, 0.59) and
player 2 (0.9, 0, 0.59), then a **centre** (x, y, z, w) in the overlay camera's space, static (−0.531, −0.075, 0, 0)
and (0.519, −0.075, 0, 0). `LockPickDial_Init` (`0x001b7eb0`) and `LockPickDial_ApplyVideoMode` (`0x001b7c10`)
rewrite the centre for the video mode before using it (the table below), with z = −1 and w = 1. Confirmed (code).

**The pins and the shapes share that centre.** Each pin's `BaseWidget_Setup` first converts the GUI position into its
sprite position (widget `+0x80`, `BaseWidget_SetPosition` `0x001a2060` through `RwDevice_GuiToOverlay` `0x00195238`:
(x − 0.5) × 640 / 448, 0.5 − y, −1.1), but `LockPickDial_Init` then **overwrites** `+0x80` with the centre
(x, y, −1, 1) (`0x001b8078`-`0x001b80f8`), and `ApplyVideoMode` does the same. Nothing writes `+0x80` again: the
dial's update only turns the pins (slot `+0x94`), refreshes their size (`BaseWidget_SetSize` with no conversion)
and runs the widget update (`BaseWidget_UpdateSize`, which touches the size and anchor, not `+0x80`), and `BaseWidget_RenderAlpha`
(`0x001a2690`) copies `+0x80` straight into the sprite batch's positions (`Instance_AddSprite` `0x00182de0`). So the
GUI position (0.09, 0.59) is **never seen**: the pins are drawn at the centre point at **depth 1**, the same depth as
the shapes. Confirmed (code) at those addresses; it corrects the earlier reading that the pins sit at the GUI
position at depth 1.1 (14 pixels right of and 5 below the shapes).

| Video mode (device flags, [Graphics](graphics.md#video-mode)) | Centre, player 1 / player 2 (x, y) | Shapes' centre in `LockPickDial_Draw` | Radius factors: wedges x, y; disc x, y; pins w, h |
| --- | --- | --- | --- |
| Interlaced 4:3, `0x01` (the default) | (−0.56, −0.075) / (0.545, −0.075) | replaced each update by **(−0.565, −0.07)** / **(0.546, −0.07)** | 1, 1; 1, 1; 1, 1 |
| Interlaced 16:9, `0x01` + `0x04` | (−0.74, −0.075) / (0.725, −0.075) | the centre | **0.8**, 1; **0.9**, 1; 1, 1 |
| Progressive 4:3, `0x20` | not rewritten: the static (−0.531, −0.075) / (0.519, −0.075) | the centre | 0.75, 1.1; 0.75, 1.0; 0.6, 0.6 |
| Progressive 16:9, `0x20` + `0x04` | (−0.62, −0.075) / (0.62, −0.075) | the centre | 0.5, 0.95; 0.5, 0.85; 0.6, 0.6 |
| PAL `0x02` (speculative name), 4:3 / 16:9 | (−0.385, −0.07) / (0.39, −0.07); (−0.58, −0.05) / (0.58, −0.05) | by its other flags, as above | by its other flags |

Confirmed (code) at `0x001b7eb0`, `0x001b7c10` (the centre) and `0x001b8530` (the factors, rewritten every update:
`0x0050d6f0`/`0x0050d6f4` the wedges' radius `+0x498`/`+0x49c`, `0x0050d6f8`/`0x0050d6fc` the disc's,
`0x0050d6e8`/`0x0050d6ec` the pins' width and height). That `+0x498` is the horizontal radius and `+0x49c` the
vertical one is inferred: the 16:9 modes shrink only `+0x498`, as pixels 1.25 times wider need. `LockPickDial_Draw`
also has a branch for `0x01` and `0x20` together (4:3: every factor 1; 16:9: wedges 0.85, 1.0, disc 0.75, 0.85, pins
0.9, 0.9, the disc's y raised by 0.002), a combination the device never sets ([Video mode](graphics.md#video-mode)).
In progressive 4:3 the static entry's z and w are 0, so the pins would sit at depth 0 unless an earlier mode had
written −1 and 1 there (open; a port needs only the two interlaced rows).

**On screen, default mode** (projection as for the shapes below): the pins' centre (−0.56, −0.075, −1) lands at
pixel **(72.8, 257.6)** of 640 × 448 for player 1 and the shapes' (−0.565, −0.07, −1) at (70.6, 255.4): the wedges
and the lock face sit **2.2 pixels left of and 2.2 above** the pins' centre, concentric to the eye. Player 2: pins
(560.6, 257.6), shapes (561.0, 255.4). In 16:9 the two centres are the same point. Inferred from the code and the
runtime view matrix below; no capture of the dial in the original.

**The pins**: three `BaseWidget`s, `hud_minigames` rectangle 11 (a brass lock cylinder), grey 191, at the centre
point above, sizes 0.19, 0.152 and 0.114 overlay units at depth 1 (0.19 × (1 − 0.2 i) × the pins' factors; square
in the default mode), depths 8,000, 9,000, 10,000, all on one centre. Each turns about its own centre (anchor 0).
Only the current pin turns:
its angle (`+0x454`, radians, starting at 3.14, below) changes by **0.1 × its speed** per update (speeds 1, 2, 3 by default),
up or down by its direction byte, wrapped into 0-2π; positive angles turn clockwise on screen
([The stereo panel](hud-functions-panels.md#stereo-layout)). Drawn while the dial is shown and a pin is left. No text.
The start angle is
the constant **3.14** (`0x0050d6e4` = `0x4048f5c3`, written by `LockPickDial_Draw` and used by `LockPickDial_Show`
and the miss in `LockPickDial_Judge`), not π, confirmed (code); at 3.14 every pin's notch points straight down. A pin
held at 4.71 shows its notch at 9 o'clock, so positive angles turn clockwise and the notch's screen angle, clockwise
from 12 o'clock, is the pin's angle: confirmed (runtime), PCSX2 2.9.94, user slot 3 (speed 0 written to `+0x458`,
the angle to pin 0's rotation `+0xe0`).

**The shapes** (`Shape2D_Queue` → `Shape2D_DrawQueued` `0x0017c308`, drawn in the viewport pass with RwIm2D):
the centre is the point (x, y, −1) in the **overlay camera's** space, projected to screen pixels
(`Im2DDrawer_Begin` `0x001964c0` → `Camera_ProjectToScreen` `0x00198460`); every rim vertex is then that pixel
centre + r × (sin a, cos a), with **r in screen pixels** and a stepping by −2π / 32 (32 segments, `+0x4ac`). With the
overlay camera's view window (0.725 × 0.5 at distance 1, [Graphics](graphics.md#2d-drawing)), the default-mode centre
lands at **screen** (0.110, 0.570) for player 1, pixel (70.6, 255.4) of 640 × 448: the dial stores the centre as
(x, y, −1, 1) at shape `+0x00` (`0x001b8530`, the `−1` written at dial `+0x488`), and the overlay camera's
`viewMatrix` read at run time (camera `+0x20`: right (0.6897, 0, 0), up (0, −1, 0), at (−0.5, −0.5, −1), pos 0;
PCSX2 2.9.94, `level99`) gives screen x = 0.6897 x + 0.5 and y = 0.5 − y at that depth, confirmed (runtime). The
pins' sprites use the same camera at the same depth ([above](#lock-pick-dial-layout): their position is the centre
point, not the GUI position), so the dial's layers are concentric within the 2.2-pixel difference of the default
mode's override.

**The disc's texture** (`LockPickDial_IsResident`, `0x001b83c0`-`0x001b8404`, confirmed (code)): the u-v centre
(`+0x4cc`) is rectangle 12's centre and the u radius (`+0x490`, `+0x494`) half its width, with v scaled by the texture's
width ÷ height (`+0x4d8`), so the circle inscribed in the whole rectangle is stretched over the 41-pixel disc. How
much of that circle the lock-face art fills decides how big the face looks, and with it how much of the wedges it
hides.

**The disc is upright, not turned** (`Im2D_DrawTexturedDisc` `0x0017bc28`, confirmed (code); confirmed (runtime)
by the keyhole, which stands vertical with its round end up). Each rim vertex has a position angle b and a texture
angle a, both stepping by −2π / 32: the position is centre + (rx sin b, ry cos b) in pixels (y down), the texture
point (cu + ru cos a, cv − rv sin a) (v down). The dial passes a = 0 (`+0x4a0`) and b = π/2 (`+0x4a4`), so
b = a + π/2 all the way round, and (cos a, −sin a) = (sin b, cos b): the texture point moves in the same direction
as the screen point. The π/2 only reconciles the two formulas; a port that writes both with one angle uses
u = cu + ru sin b and v = cv + rv cos b, with no turn. (An earlier reading here said "turned by π/2".)

| Shape | What | Radius (px) | Colour (0-255) | Extent |
| --- | --- | --- | --- | --- |
| Band 1 | an untextured **wedge** (a fan from the centre) | 40 (`0x0050d6c0`) | (191, 16, 16), alpha 100 (`0x0050d6b4`) | `0x0050d6b8` per cent of the circle (35, 27 or 20 by difficulty) |
| Band 2 | the same, drawn over band 1 | 40 | (50, 7, 7), alpha 100 | `0x0050d6bc` per cent (14, 12 or 9.2) |
| Centre disc | a textured disc, the whole circle | 41 (`0x0050d6b0`) | (191, 191, 191, 255) | `hud_minigames` rectangle 12 (the lock face with its keyhole), mapped by the rectangle's centre and radius, **upright** (below) |

Both wedges start at angle π, which with y down on screen is **straight up** from the centre (12 o'clock); the
segments outside the wedge are drawn with a zero colour, so only the wedge shows. Flat shading makes each wedge an odd
number of whole segments, half a segment off 12 o'clock towards 11 (Extent, below). Which band is
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
  fan's centre vertex keeps the colour. With p = 0 (k = 0) nothing is coloured. Rim vertex j sits at angle π − j ×
  2π / n, so j = 1, 2, ... run **clockwise** on screen from 12 o'clock and n − 1, n − 2, ... counter-clockwise.
- **Shading: flat, so no fades.** The fan is drawn by `Im2DDrawer_Draw` (`0x001965d8`), which sets RenderWare's
  render state 7 (`rwRENDERSTATESHADEMODE`) to its last argument; `Im2D_DrawArc` and `Im2D_DrawTexturedDisc` pass 1,
  `rwSHADEMODEFLAT` (the draw ends by setting it back to 2, Gouraud), confirmed (code). On the GS a flat triangle takes
  the colour of its **last** vertex, so the fan triangle between rim vertices j and j + 1 is wholly coloured when
  vertex j + 1 is and wholly clear when it is not; the centre vertex's colour is never seen. The coloured triangles
  are j = 0 to k − 1 (k segments clockwise of 12 o'clock) and j = n − k − 1 to n − 1 (**k + 1** segments
  counter-clockwise): **2k + 1 solid segments with hard radial edges, from −(k + 1) × 11.25° to +k × 11.25°**, and
  nothing outside them. Example: 27 % (difficulty 1) gives k = 4, a wedge from −56.25° to +45° (101.25°); 12 % gives
  k = 1, −22.5° to +11.25°. Confirmed (runtime), PCSX2 2.9.94 at native resolution, user slot 3 at difficulty 1:
  sampled round the centre at radii 22-36 pixels, band 1's edges sit at −56.7° and +44.5° and band 2's at −22.5° and
  +10.5° at every radius, with no gradient across either edge. (An earlier reading here had 2k solid segments and a
  fading segment on each side, from Gouraud shading.)
- **Colour on screen.** Fitting out = α × C + (1 − α) × background over 672 pixels of band 1 alone (the frame with the
  dial against the same frame without it, pins moved off) gives **α = 0.39, C = (189, 15, 16)**: alpha 100 ÷ 255 and
  the colour (191, 16, 16) as stored, with no PS2 128-is-one doubling. Band 2 over band 1 gives α = 0.63 and C =
  (102, 9, 9), which is band 2 (50, 7, 7) at 100 ÷ 255 over band 1, as the two-layer blend predicts (1 − 0.61² =
  0.63). Confirmed (runtime), same capture.
- **The drawn wedge is wider than the judge's window.** At difficulty 1 a press is good with the notch from
  −33.4° to +32.7° of 12 o'clock (5.70 and 0.57 rad) and perfect from −19.1° to +6.9° (5.95 and 0.12 rad, read at
  run time from `0x0050d6c4`-`0x0050d6d0` and `0x0050d70c`; [Crimes](crimes.md#lockpick)), while the red reaches
  −56.25° to +45° and the dark band −22.5° to +11.25°; so the notch can be on red and still miss. Inferred from the
  code's bounds and the measured geometry; a faithful port keeps both as they are.

### The dial next to the original (2026-10-11) {#lock-pick-dial-compared}

**The original's capture.** PCSX2 2.9.94 at native resolution (window 1240 × 930 for the 640 × 448 frame), the
interlaced 4:3 mode (device flags `0x01`), user slot 3 (a store door; it is not pickable there, the kind-2 list of
the context registry is empty). The dial was shown by writing 1 to player 1's dial `+0x478` (HUD `0x00600840` +
`0xf420` = `0x0060fc60`, so `0x006100d8`): `LockPickDial_Draw` then turns pin 0 and queues the shapes exactly as in a
pick, since nothing it draws reads the mini-game's state; the difficulty in force was 1 (directions (+, −, +), bands
27 % and 12 %, left from an earlier `HUD_LockPickStart`). The pins were moved off (their `+0x80` x) for the wedge
measurements and put back. **Coney's**: the `lockpick-dial` branch (pull request #52), `--play-level level80
--checkpoint 3 --start -241.9,228.8,1.3,183`, triangle tapped at frame 150 (difficulty 1), frames 170-215,
`--render-size 1280x896`; both frames scaled to 1280 × 896 (twice 640 × 448) before comparing.

**What matches** (measured on those frames): the dial's place (shapes' centre at pixel (70.6, 255.4)) and size; the
three pins concentric with the face, their dark ring lines at the same radii within 1 pixel (about 16, 22-23, 30-31
and 40-41 pixels from the centre on both sides); the lock face's size (about 31 pixels across its dark rim); the
order (pins under the wedges, the face over both); the pins' turning direction and their notches pointing down at
rest; the stored colours and alphas. Neither game draws text, a button glyph or a stick indicator with the dial. A
press changes nothing on screen but the pins: a good or perfect press leaves the judged pin where it stopped and
starts the next one from its own angle (`LockPickDial_Judge` copies the next pin's rotation into `+0x454`), a miss
puts every pin back at 3.14 and pin 0 turns again, and the end of the pick hides the dial with no closing frame
(`LockPick_End` `0x0022d908` → `HUD_LockPickEnd`); confirmed (code), not captured.

**Coney differed** (checked against `repo:src/hud/lock_pick_hud.cpp` on the `lockpick-dial` branch, commit
`803e1de5`); all four are **fixed** on that branch (pull request #52), each with a test in
`repo:tests/hud/lock_pick_hud_test.cpp` or `repo:tests/world_objects/lock_pick_test.cpp`:

1. **The lock face was turned a quarter turn** (fixed): its keyhole lay on its side with the round end to the right;
   the original's stands upright, round end up. Cause: Coney mapped rim vertex angle a to the texture at a + π/2
   (`kFaceTurn`), from the earlier "turned by π/2" on this page. Now the texture point of a rim vertex at angle a is
   (cu + ru sin a, cv + rv cos a), the same direction as its screen offset (`Im2D_DrawTexturedDisc`
   [above](#lock-pick-dial-layout)), and `kFaceTurn` is gone. Test: the rim vertex straight up on screen shows the
   top of rectangle 12's circle (v = cv − rv).
2. **The wedges were Gouraud-shaded** (fixed): each had 2k solid segments, symmetric about 12 o'clock, with a segment
   on each side fading to clear, so its edges were soft and band 2 barely showed. The original is flat-shaded: every
   fan triangle takes the colour of its second rim vertex, giving 2k + 1 solid segments from −(k + 1) × 11.25° to
   +k × 11.25° with hard edges (difficulty 1: band 1 −56.25° to +45°, band 2 −22.5° to +11.25°; the dark band 2 reads
   as a distinct centre stripe). Now the triangle (centre, rim j, rim j + 1) has the colour of rim vertex j + 1 on all
   three of its vertices and is skipped when that colour is clear (`LockPickHud::wedgeSegment`). Test: for each
   difficulty, which of the 32 triangles are coloured (difficulty 1, band 1: j = 0-3 and 27-31; band 2: j = 0 and
   30-31), and no triangle has vertices of two colours.
3. **A red haze covered the whole dial** (fixed with 2): Coney's fan gave the centre vertex the wedge's colour, so
   every clear segment faded from red at the centre to clear at the rim, tinting the pins below and beside the face
   (measured below the face, 16-22 pixels out: red minus green 14 on Coney, 3 on the original). Under flat shading
   the centre vertex's colour is never seen, and clear segments are no longer drawn.
4. **The pins' rest angle was π, not 3.14** (fixed; a 0.09° difference, not visible). Both the dial
   (`LockPickHud::show`) and the mini-game (`LockPickDial`, where the pins start and where a miss puts them back) now
   use 3.14.

**Open**: how many times a second the dial's update (`LockPickDial_Draw`, the HUD's update slot) runs, and so how
fast a pin turns in seconds, was not measured; the code turns it 0.1 × speed per call.

**User item 12 (the dial's layers misplaced)**: Coney's `repo:src/hud/lock_pick_hud.cpp` (checked 2026-10-11)
draws the pins at the GUI position (0.09, 0.59) through `OverlayCamera::guiToOverlay` (depth 1.1) and the shapes at
(−0.565, −0.07, 1). That puts the wedges and the lock face 14 pixels left of and 5 above the brass pins, which is the
red wedge "up and left of the lock" and the keyhole left of the outer ring that the user saw. The original draws
the pins at the centre point. **Coney fixes** (done in pull request #52):

1. Draw the three pins at the player's centre point in overlay space at distance 1, not at the GUI position: 4:3
   (−0.56, −0.075) for player 1, (0.545, −0.075) for player 2; keep the shapes at (−0.565, −0.07) / (0.546, −0.07).
   Drop `kPinPlaces`; the GUI position is unused.
2. Size the pins in overlay units at distance 1 (0.19, 0.152, 0.114), not at the 1.1 of other sprites, so they come
   out 10 % larger on screen than at depth 1.1.
3. In Coney's 16:9 view (`graphics::isWideView`), use the interlaced 16:9 row: both centres (−0.74, −0.075) /
   (0.725, −0.075), wedge radius 40 × 0.8 = 32 px across and 40 px up, lock-face radius 41 × 0.9 = 36.9 px across
   and 41 up (the pixel radii scaled by the frame's height ÷ 448 as before), pins unchanged in overlay units.
   `pixelInOverlay()` must use the camera of the mode in force (it now always builds a default 4:3
   `OverlayCamera`).
4. Test: for each mode, the projected screen centre of the pins and of the shapes differ by at most 2.5 logical
   pixels (default mode 2.2 left and 2.2 up; 16:9 none); the pins' projected size at depth 1 is 0.19 / (2 × 0.725) of
   the width in 4:3.

**Status (2026-10-11): fixed in Coney.** `LockPickHud::layout(player, deviceFlags)` gives each mode's pins' and
shapes' centres (at distance 1), the wedges' and the face's (rx, ry) in pixels and the pins' factors, for the four
rows above; the pins are drawn at the centre point at depth 1 (fixes 1 and 2), and every rim vertex is the pixel
centre + (rx sin a, ry cos a), converted through the mode's overlay camera (`setVideoMode`, fix 3). The tests in
`repo:tests/hud/lock_pick_hud_test.cpp` check fix 4. Left out: the PAL rows and the `0x01` + `0x20` branch (Coney has
neither). **Open**: Coney's HUD layer always draws the 2D pass in the 4:3 logical screen through the 4:3 overlay
camera, even beside a wide 3D view, so the play mode keeps the dial in the interlaced 4:3 mode; the 16:9 row takes
effect once the HUD as a whole gets a 16:9 mode.

## `GUI/HUDLua.cpp`: the scripted bars {#fn-hudlua-bars}

The bar functions of `HUDEnableBar`, `HUDSetBarPercentage` and `HUDSetBarProperty`, [Scripted bars](hud-messages.md#scripted-bars).

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

## Data

The tables these functions read (the indicator's fields, the panel layout table's tally entries, the dial's layout)
are given in the sections above; the HUD's other data is on [The in-game HUD](hud.md#data).

## Behaviour

What each binding does is in its row and section above: [the gang-count indicator](#gang-count-indicator), [what the
count counts](#gang-count-what), [the panel's tally](#panel-tally), [the lock-pick dial](#lock-pick-dial-layout) and
[the scripted bars](#fn-hudlua-bars).

## Coney's implementation

The indicator, the panel tally and the bars are built in `repo:src/hud/` (`num_indicator.h`, `player_panel.h`,
`hud.h`); what each does and its stand-ins are on [The in-game HUD](hud.md#coneys-implementation).

## Open questions

- Which of the Rumble scripts' two branches is the two-player one (inferred above, not traced).
