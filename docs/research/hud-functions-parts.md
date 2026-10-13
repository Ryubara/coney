# HUD function index: widgets, radar frame and the HUD interface

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 as on [The in-game HUD](hud.md), which this page was split from because that page had grown too
large. The overview, the data tables and the rest of the behaviour stay on [The in-game HUD](hud.md).
Every claim keeps the evidence level it had there.

This page covers the function index for the files from `GUI/BaseWidget.cpp` to `GUI/HUDInterface.cpp`.

## Before `GUI/BaseWidget.cpp` (no path string): HUD parts {#fn-pre-basewidget}

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
| `0x0019ef80` | `ActionPrompt_Setup` | [Action prompts](hud-messages.md#action-prompts) | confirmed (code) |
| `0x0019f0f8` | `ActionPrompt_Shutdown` | slot `+0x68`: releases the icon, then the scroll-in text | confirmed (code) |
| `0x0019f128` | `ActionPrompt_SetNumberText` | a text given as a string id: restarts the prompt (slot `+0xe0`) unless the owner and id are unchanged | confirmed (code) |
| `0x0019f1b0` | `ActionPrompt_SetText(prompt, owner, text)` | restarts the prompt (slot `+0xe8`) when the owner or text changed | confirmed (code) |
| `0x0019f270`, `0x0019f320` | `ActionPrompt_StartCycle(size, prompt, words, n, rate, hold)`, `_StopCycle` | `HUDTurnOnActionCycleAnim` / `Off`: the sprite words at `+0x440`, rate `+0x448`, hold `+0x458`, on `+0x45c` | confirmed (code) |
| `0x0019f328` | `ActionPrompt_UpdateCycle` | every `+0x448` updates the icon switches between the two words; with a hold, the icon shows for `hold` frames and hides for `hold` frames | confirmed (code) |
| `0x0019f430` | `ActionPrompt_SetRaise` | [Action prompts](hud-messages.md#action-prompts) | confirmed (code) |
| `0x0019f4c8` | `ActionPrompt_UpdateSplitX` | two-player x positions `0x0050cfcc` / `0x0050cfd0`: 0.33 / 0.66, or 0.25 / 0.74 in 16:9 | confirmed (code) |
| `0x0019f528` | `ActionPrompt_Update` | slot `+0x30`, [Action prompts](hud-messages.md#action-prompts) | confirmed (code) |
| `0x0019f850` | `ActionPrompt_Render` | slot `+0x38`: in a two-player game or for player 0, the cycle icon (when on) and the text | confirmed (code) |
| `0x0019f8b8`, `0x0019f928` | `ActionPrompt_RenderIcon`, `_RenderText` | `HUD_Render`'s last step: the icon or text once after an update (`+0x584`, `+0x580`) | confirmed (code) |
| `0x0019f988` | `CrewStatus_GetIcon(human)` | the portrait rectangle: in a story game by the character kind (`0x00229570`, kinds 0-11 → `0x31`, `0x28`, `0x25`, `0x23`, `0x2e`, `0x29`, `0x33`, `0x30`, `0x27`, `0x24`, `0x2b`, `0x2a`), otherwise by which player's gang it is in (`0x2c`, `0x2d`); default `0x28` | confirmed (code) |
| `0x0019faf0` | `CrewStatus_Init` | eight order slots (`+0x44`) = -1, entries hidden, count `+0x40` = 0, layout `+0x1310`-`+0x1324` | confirmed (code) |
| `0x0019fbb0` | `CrewStatus_Setup(iconSize, aspect, list, position, cell, step, columns, rows)` | from the HUD's level set-up (0.05, 0.7, columns 2, rows 4): the grid, and the caption markup text `+0x13b0` (size 0.1, grey 128) | confirmed (code) |
| `0x0019fd00` | `CrewStatus_Shutdown` | slot `+0x68`: releases the eight entries | confirmed (code) |
| `0x0019fda0` | `CrewStatus_Update` | slot `+0x30`, from `HUD_Update` while the statistics game mode is below 100: each live entry at its grid cell (order index modulo the columns, rows below); an entry that is hidden or whose human no longer resolves is removed; the caption under the last row | confirmed (code); removal confirmed (runtime) |
| `0x001a03a8` | `CrewStatus_Render` | slot `+0x38`: the entries and the caption | confirmed (code) |
| `0x001a0420` | `CrewStatus_Add(list, human)` | from `0x001b3960` (`HUD_TagAddHuman`, called by `Humans_Update` for every member of player 1's gang every update): a member who is **knocked out** (`0x00227dd8`), **cuffed** (`Human_IsCuffed`, `0x00223b70`, state `0x20000`) or **fighting far off** (brain `+0x152` set, an attacker in brain `+0x1a4`, over 50 m from the camera) gets one entry (eight at most); the caption becomes the 2.5 s format, colour tag 5, the human's name (capitalised) and string `0xe1`, `0xe2` or `0xe3` ([The crew status list](#crew-status)) | confirmed (code); cuffed and knocked out confirmed (runtime) |
| `0x001a07f0` | `CrewStatus_Remove(list, human)` | from `Human_Destroy` and `Human_MakePlayer` (`0x001b39a0`): drops the human's entry and renumbers the rest | confirmed (code) |
| `0x001a08c8`, `0x001a0918` | `CrewStatusEntry_Init`, `_Setup` | portrait sprite `+0x40` (`(191, 191, 191, 255)`, `CrewStatus_GetIcon`) and status sprite `+0x140` (rectangle `0x19`, grey 128); 0.035 further left in 16:9 | confirmed (code) |
| `0x001a0b00` | `CrewStatusEntry_Shutdown` | slot `+0x68`: both sprites released, the human handle cleared | confirmed (code) |
| `0x001a0b60` | `CrewStatusEntry_Place` | the portrait at a position, the status sprite offset by (`0x0050d018`, `0x0050d01c`); sizes × 0.8 in 16:9 | confirmed (code) |
| `0x001a0c90` | `CrewStatusEntry_Render` | slot `+0x38`: both sprites when the batch is resident | confirmed (code) |
| `0x001a0d28` | `CrewStatusEntry_Update` | slot `+0x30`: hidden unless the human is in player 1's gang; status rectangle `0x1a` (knocked out), `0x19` (cuffed) or `0x15` (fighting far off), and hidden when none holds (the update then removes it); the status alpha pulses on an 800 ms triangle wave, cubed | confirmed (code) |
| `0x001a0fd0`, `0x001a1008`, `0x001a1098` | `Bar_Construct`, `HudBar_Construct`, `HudBar_Init` | the meter's set-up and defaults ([The rage meter](hud.md#the-rage-meter), [GUI](gui.md#widget-classes)) | confirmed (code) |
| `0x001a1138` | `HudBar_Draw(flashBelow, bar, flash, trail)` | the end caps (rectangles +2, +3), back (+0), fill (+1) and an optional second fill `+0x3c`; a flash below `flashBelow` and a fading trail, both unused: every caller passes 0 for both ([The rage meter](hud.md#the-rage-meter)) | confirmed (code) |
| `0x001a1be8` | `HudBar_SetFillGradient(bar, on, low, high)` | `+0x60` on: the fill colour is lerped from `+0x30` to `+0x34` by the fill | confirmed (code) |

### The crew status list {#crew-status}

The HUD's only alert about the crew: a portrait for each member of the player's gang who is in trouble, with a
pulsing status icon under it, and a one-line caption naming the member when an entry appears. It lives at HUD
`+0x10` (`0x00600850`) and sits at the top of the screen, a little left of centre.

**Who is listed.** Every update, `Humans_Update` hands each human whose brain's gang (`+0x20c`) is player 1's (game
state `+0x228`) to `HUD_TagAddHuman` (`0x001b3960`, call at `0x002493a0`), which passes it to `CrewStatus_Add`
(`0x001a0420`). The member gets an entry when one of three states holds, tested in this order; the same three
choose the caption and the icon. Confirmed (code) at `0x001a0420` and `0x001a0d28`:

| State | Test | Caption string (`GSTRING.HUD`) | Status rectangle | On screen |
| --- | --- | --- | --- | --- |
| **knocked out** | `Human_IsKnockedOut` (`0x00227dd8`): state `0x40000`, or no record | `0xe1` (225): the "down" text | `0x1a` | a red cross |
| **cuffed** (arrested) | `Human_IsCuffed` (`0x00223b70`): state `0x20000`, the state [`Human_Arrest`](crimes.md#arrest) sets | `0xe2` (226): the "arrested" text | `0x19` | a blue badge |
| **fighting far off** | brain `+0x152` not 0 (the count of brains that hold him in their enemy lists, [fight mode under follow](ai-gangs.md#warrior-follow)), at least one live handle in his 16 attack slots (brain `+0x1a4`, `HandleArray16_CountLive`), and human `+0x334` (his distance to the nearest camera, written by `Human_UpdateLod` `0x0023d660`) above 50 m (its square above 2500) | `0xe3` (227): the "fighting" text | `0x15` | not seen |

- The caption texts are `GSTRING.HUD` 225-227 of `config_strings_<code>.lua` ([Text labels](../references/text-labels.md#text-gstring-hud)),
  given to the engine by `CfgHUDMessage`; their English words name the three states as the table does (read from the
  script, not quoted here). This replaces the earlier reading of the states as down, dead and lost, which was
  inferred from the icons: `0x00223b70` is the cuffed test, and nothing in the list tests death.
- Not listed: a human whose character id (`+0xcc`) is 270-287, the bums ([Characters](../references/characters.md));
  a human whose gang is not player 1's and whose brain type (`+0x04`) is neither 0 nor 3 is refused by `Add`, and
  `CrewStatusEntry_Update` hides any entry whose human is not in player 1's gang. The code does not exclude the player
  himself; whether his own entry ever shows was not checked.
- A human already listed is not added twice. At most eight entries; a ninth is dropped.

**The entry.** A portrait (`CrewStatus_GetIcon`, above) and the status sprite under it, whose alpha follows an 800 ms
triangle wave, cubed, so the icon pulses. Each update `CrewStatusEntry_Update` re-tests the three states and sets the
rectangle; when none holds it hides the entry, and `CrewStatus_Update` then removes it and renumbers the rest. So the
list is live: an entry lasts exactly as long as its state (a member who gets up, is uncuffed or comes back within
50 m of the camera leaves the list), and it can change icon while listed (a cuffed member knocked out shows the cross).

**The caption.** When an entry is added, `CrewStatus_Add` builds the caption from the format at `0x00553cb8`, an
engine constant: `<DISPLAYTIME 2500><SIZE 1.0><CENTER>`, then colour tag 5 (`HudColourTag_Get(5)`, a grey colour tag
from `CfgHUDColor`), the human's name with its first letter upper-cased, a space, and the state's string. One special
name (the string at `0x00553c90`) is replaced by the one at `0x00553ca0` (not traced which). The caption is one
markup text for the whole list, placed under the last row, so a new entry replaces the previous caption. It shows for
**2.5 s** and fades. **No sound** plays: `CrewStatus_Add` calls no sound function. Confirmed (code) at `0x001a0420`,
`0x0019fda0`, `0x001a03a8`.

**Runtime check** (PCSX2 2.9.94; a copy of a `level80` checkpoint state with the player and two Warriors, humans 1
and 2; the record flags at `*(human + 0xd4)` written over PINE): setting `0x20000` on human 1 added one entry (list
count `+0x40` = 1, order slot 0) with a blue badge under his portrait and the caption built as above with the
arrested text; the caption widget's alpha (`+0x1b0` of the caption at `+0x13b0`) was 255 at 1 s and fading by 2 s.
To see the caption in a still, the format's `2500` was patched to `9500` in memory; it then showed centred under the
portraits. Setting `0x40000` instead gave the red cross, pulsing; clearing the flag emptied the list on the next
update. The real arrest path sets the same flag ([Arrest](crimes.md#arrest), step 2), so an arrested crew member is
listed (inferred from the flag; an arrest by a cop was not staged). The fighting state was not staged at runtime.

**What else the HUD shows for the crew.** Nothing reacts to a member fighting or being hit short of these three
states: the [health rings](hud-radar.md#the-health-rings) are drawn only for the players and the player's target,
and the [gang tally](hud-functions-lua.md#gang-count-what) counts members who are not down (a cuffed member keeps
his mark, as state `0x20000` is not in `Human_IsDownOrDead`'s mask). A member in a fight within 50 m of the camera
gets no HUD cue at all. In the runtime check above, the knocked-out member's radar blip also changed look; that was
not traced. When a crew member is arrested, the other cues are not the HUD's: he says 25 `arrested`, and the
`dyn_cuffs` icon floats over his head in the world ([Arrest](crimes.md#arrest), step 6).

## After `GUI/BaseWidget.cpp` (no path string): mash meter and chase HUD {#fn-after-basewidget}

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

### The mash meter on screen {#mash-meter-layout}

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

## After `GUI/BaseWidget.cpp` (no path string): the chase gauge {#fn-chase-gauge}

`HUDEnableBar` kind 2 (vtable `0x00539600`, one object at HUD `+0xa940`), [Scripted bars](hud-messages.md#scripted-bars).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001a3660` / `0x001a36b0` | `ChaseGauge_Construct` / `_Destroy` | three `BaseWidget`s at `+0x40`, `+0x140`, `+0x240`; destroy shuts down and releases them | confirmed (code) |
| `0x001a3720` | `ChaseGauge_Setup` | `(size, max, value, gauge, pos, track, endIcon, marker)`: the three sprites, max `+0x348`, value `+0x340` | confirmed (code) |
| `0x001a38d8` | `ChaseGauge_Shutdown` | releases the sprites, clears created (`+0x0c`) | confirmed (code) |
| `0x001a3918` / `0x001a3920` | `ChaseGauge_SetValue` / `_SetRange` | value `+0x340`; min `+0x344`, max `+0x348` | confirmed (code) |
| `0x001a3930` | `ChaseGauge_IsLoaded` | all three sprites' sheets resident | confirmed (code) |
| `0x001a3990` | `ChaseGauge_Update` | places track, end icon and marker; the marker's colour from its position | confirmed (code) |
| `0x001a3c00` | `ChaseGauge_Render` | draws the three sprites once ready | confirmed (code) |

## `GUI/ChecklistMessageHUD.cpp` {#fn-checklistmessagehud}

`0x001a4690`-`0x001a5d60` (static-init stub): the **checklist** (vtable `0x00539730`, `0x360` bytes), the pause
menu's objective lists ([Objectives](hud-messages.md#objectives-hudsetobjective)), holding **items** (markup text
widgets, vtable
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

## After `GUI/ChecklistMessageHUD.cpp` (no path string): radar arcs, Warrior command display {#fn-after-checklistmessagehud}

`0x001a5d80`-`0x001a8fb8`: the arcs of the radar frames (HUD `+0x177d0`, `+0x18280`), the **Warrior command
display** of the player panel (`+0x1ef0`, vtable `0x005398e0`, opened by L1 + R1, [AI](ai-gangs.md#warrior-commands)), the
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
| `0x001a6c58` | `WarCommandDisplay_Issue` | on R2's release ([Warrior command menu](hud.md#warrior-command-menu)), once per opening (`+0x1548`): issues the selected command through `0x0041c4e0` and stores it as the player's last (`+0x41c` + player) | confirmed (code) |
| `0x001a6d28` | `WarCommandDisplay_Open` | from `0x002843f8`: for a war chief (`+0x3ac` = 1) shows the display and resets its sprites and timer | confirmed (code) |
| `0x001a7000`, `0x001a7020` | `WarCommandDisplay_ReleaseCamera`, `_IsReady` | `0x0050b1b0[pad]` = 1: the camera's right stick back on ([Warrior command menu](hud.md#warrior-command-menu)); the batch is resident | confirmed (code) |
| `0x001a7040` | `WarCommandDisplay_ReadStick` | every `*0x0050d280` ms, with the stick (pad record bytes `+0x2a`/`+0x2b`) more than 110 from centre: the stick's angle picks one of six sectors (with hysteresis `0x0050d2b8`) as the selection `+0x1540`; interface cue `0x20` on a change | confirmed (code) |
| `0x001a77b0`, `0x001a7d10` | `WarCommandDisplay_Place`, `_BlinkSlot` | the slot sprites round the centre; the selected slot blinks by the counter `+0x1578` | confirmed (code) |
| `0x001a7e48` | `WarCommandDisplay_Update` | slot `+0x30`: closed when the player is down; the stick read unless the menu is locked; after an issue the sprites fade and the display closes when its text expires; text = the command's name or the locked text | confirmed (code) |
| `0x001a8530` | `WarCommand_FromSlot(display, slot)` | jump table `0x00553fe0`: display slot 0-5 → command id; 7 otherwise | confirmed (code) |
| `0x001a8590` | `WarCommandDisplay_Render` | slot `+0x38`: the selected slot white / gold `(255, 183, 0)` / grey, the others white / dark `(35, 35, 35)` / black; a disabled command in `(30, 30, 30)` | confirmed (code) |
| `0x001a8988` | `WarCommandDisplay_ShowCurrent` | maps the player's last command to a slot and shows its name; no caller or table reference found (not needed: unreferenced) | confirmed (code) |
| `0x001a8b48`, `0x001a8e10` | `WarCommandLayout_StaticInit`, `_StaticInitStub` | static initialiser (ctor list `0x005340fc`): the layout records `0x006006a0` / `0x00600720` and the colours above | confirmed (code) |
| `0x001a8ed8`, `0x001a8f20`, `0x001a8f30` | `HudToggle_Init`, `_Clear`, `_IsEnabled` | the HUD's `+0x15b0` record: enabled `+0`, shown `+4`, hidden by `HideHud` `+8`, `+0x0c` = 11, a counter `+0x10` | confirmed (code); its role not traced |
| `0x001a8f38`, `0x001a8f48` | `HudToggle_Hide`, `HudToggle_Restore` | `HideHud` / `RestoreHud` ([Showing and hiding](hud.md#showing-and-hiding)) | confirmed (code) |
| `0x001a8f78`, `0x001a8f88`, `0x001a8f90` | `HudToggle_Show`, `_Off` (`0x001b2500`), `_Tick` | `+4` on / off; the counter counts down each HUD update | confirmed (code) |

## After `GUI/Credits.cpp` (no path string): the radar frame {#fn-radar-frame}

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
  mode, [the disc's position](hud-radar.md#the-radar-on-screen)), and both draws use the point (x, third, −second) =
  **(x, −0.31, −1)** of the overlay camera's space, so the arcs are **concentric with the disc**. The `(0.5, 1, −0.31,
  1)` of the set-up is replaced on the first update.
- **Radii in pixels.** The drawer's begin (renderer `0x0050cdb4 + 4`, slot `+0x08`) projects that centre to screen
  pixels, and every vertex is the pixel centre plus an offset, as for [the lock-pick dial's
  shapes](hud-functions-lua.md#lock-pick-dial-layout) (inferred: the same drawer): vertex = centre + k × (rx × sin a, ry
  × cos a), with
  k = inner / outer for the first of a pair and 1 for the second. `rx` = outer × fx and `ry` = outer × fy, by device
  flags: `0x01` set and `0x20` clear **(1.1, 1.0)** (the default) or (0.8, 1.0) in 16:9; `0x01` and `0x20` set (0.85,
  1.0); `0x01` clear (0.75, 1.0) or (0.65, 1.15) in 16:9. So in the default mode the blue pair is a band 54-57.6 px
  high and 59.4-63.4 px wide from the centre, the orange 57.6-61.2 / 63.4-67.3 px; the disc's measured soft edge
  (about 55 × 50 px) lies just inside (inferred, from the disc's measured size).
- **Angles**: a starts at the arc's start (0) and steps by (end − start) × shown fill / 16, in radians. With screen y
  down (inferred, the pixel convention), a = 0 is straight **below** the centre; the 0 → 180° arc grows up the
  **right** side and the 0 → −180° arc up the **left**, the pair meeting at the top when full.

**The crime message** (`HudCrimePanel_OnMessage`, `0x001aa720`, from `HUD_SetWanted`), confirmed (code) unless marked:

- **Which message.** `HUD_SetWanted`'s own `msg` only picks show (7-9) or clear (anything else); the message shown is
  always the game state's **last crime type** (`+0x290`), so the index into `CfgCrimeMessage` is a
  [crime type](../references/crime-types.md). The crime report's last step (`0x0041b8b0`, at `0x0041be44`), when the
  offender is in player 1's gang, first stores the report's type in `+0x290` unless it already holds 7 (`GangCurfew`)
  or 12 (`Trespassing`), then calls `HUD_SetWanted(hud, 7, 0)`: a constant, not the type or the crime level. So a
  plain assault (type 0) shows **`CfgCrimeMessage` 0**, unless a curfew or trespass is still on record, which then
  keeps showing. `+0x290` is 14 (`NoCrime`) at each level's start (`0x00418d3c`) and again when the gang's wanted time
  runs out (`Gang_ClearWanted`, `0x00169a20`, which calls `HUD_SetWanted(hud, 0xb, 0)` first). `Crime_UpdateLevel`
  (`0x0041c028`) sends 8, 7 and `0xb` as the unused crime level steps down; nothing sends 9.
- **A repeat is not re-sent.** While the `+0x450` widget holds a text, a message whose text is the same
  (`strcmp`, `0x00430c4c`, returning 0) changes nothing but the visibility, so a second assault while still wanted does
  not set the centred copy again (it is not restarted).
- **The first wanted message** of an episode (radar `+0x1c` still 0: at a level's start, `Radar_Setup` `0x001c3e30`,
  or after any clear) sets `+0x1c` = 1 and plays **interface cue 2**, `vags/misc/hudalarm`
  ([interface sounds](../references/sound.md#interface-sound)); later reports while it is 1 play nothing. Any other
  message (the `0xb` of `Gang_ClearWanted`) clears `+0x1c`, so the next crime after the wanted time ran out sounds
  the alarm again.
- **The radar's wanted flag changes nothing on screen.** Radar `+0x1c` (HUD `+0x15ec`), `+0x30` (set to 1.0) and
  `+0x34` (0) are written by `HUD_SetWanted` and `Radar_Setup` only; an instruction scan of `SLUS_212.15` finds no
  other load of HUD `+0x15ec` (`+0x1600`, `+0x1604`: none) and none of radar `+0x1c`, `+0x30`, `+0x34` in
  `Radar_Update`, `Radar_Render`, `HUD_Update` or `HUD_Render`. The disc's colour comes from its own state `+0x38`
  ([Colour](hud-radar.md#the-radar-on-screen)), set only by the shadow test. So wanted shows as the blue arcs and the centred
  text, never on the disc. Inferred: the three fields are left over.
- The panel's own widget `+0x450` is set up (size `0x0050d380` = 1.0, scale 1.0, colour `0x0050d384` = bytes
  (255, 191, 96, 96), font slot 6) and placed every update at **(0.5, 0.4)** (`0x0050d390`, `0x0050d394`), and `+0x890`
  gets global string `0xe4`, but **nothing draws either**: `HudCrimePanel_Render` draws only the arcs, and no other
  code reaches them (no reference to HUD `+0x17c20` or `+0x186d0`; the boxed-text renderer's five callers are other
  widgets). Inferred: left over.
- What shows is the **copy** in the HUD's centred announcement (`+0xe150`, `MarkupText_SetText(0x0060e990, …)`):
  centred at **(0.5, 0.25)** in the default mode (`HUD_Update` places it each update from `0x0050d470`, 0.35 in one
  other mode; [Announcements](hud-messages.md#announcements-and-other-messages)), drawn by `HUD_Render` while the HUD is
  shown (and
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

## After the stub `0x001aad70` (no path string): item counters and status icons {#fn-hud-counters}

`0x001aad90`-`0x001ab348`: the skeleton-key and handcuff counters of the player panel
([Item counters](hud.md#item-counters)) and the **status icons** (HUD `+0x114c0` + player × `0x450`, vtable `0x00539aa8`:
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

## Before `GUI/HUDInterface.cpp`: the HUD object {#fn-before-hudinterface}

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
| `0x001acd60` | `ActionPrompt_FindNearbyHuman` | ([Action prompts](hud-messages.md#action-prompts)) | confirmed (code) |
| `0x001acee0` | `HUD_Construct` | constructs the parts above and points `+0x08` / `+0x0c` at the player panels | confirmed (code) |

## `GUI/HUDInterface.cpp` {#fn-hudinterface}

`0x001ad588`-`0x001b3ea8` (the static-init stub): the HUD object's methods. One static object at `0x00600840`
(constructed by `HUD::HUD`, `0x001acee0`, from the TU's initialiser); its parts are in
[The HUD object](hud.md#hud-object). Names are ours; `0x001acee0` itself is listed with `GUI/ChecklistMessageHUD.cpp`'s
neighbours.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001ad588` | `HUD_InitLevel` | per-level set-up (from `InitLevel`, `0x0015fe90`): the layout floats for the video mode, then every part of [the HUD object](hud.md#hud-object) that is not yet set up, the two player panels (`ANHud` panels allocated in levels 60-69, otherwise the static ones at `+0x19130`), the Armies of the Night widgets (`0x0041d110`); ends hidden (`HUD_Hide`) with `+0x177a4` = `+0x177ac` = `+0x177b4` = 1 | confirmed (code) |
| `0x001ae378` | `HUD_SetupSpinner` | the loading/saving spinner at `+0xe050`: a sprite (size 0.09, sprite word `0x5c` in batch 3, colour (191, 191, 191, 255)) at (0.95, 0.83) in the default mode, rate `+0x19128` = 0.2 (unused: the sprite never turns, [Spinner](hud.md#hud-spinner)) | confirmed (code) |
| `0x001ae4d8` | `HUD_EnsureVirtualPads` | allocates the two `0x2c`-byte player input records (tag `VirtualPad`) at HUD `+0x00`/`+0x04` when missing: pad index `+0x19` = -1, bound `+0x1b` = 0 | confirmed (code) |
| `0x001ae5a0` | `HUD_ResetPadBindings` | unbinds both players and sets every [pad record](frontend.md#pad-record)'s owner `+0x42` to -1 (from `PM_Greet`) | confirmed (code) |
| `0x001ae638` | `HUD_AssignPadToPlayer(hud, player)` | [pad binding](hud.md#hud-pads) for one player | confirmed (code) |
| `0x001ae828` | `HUD_UpdatePadBindings` | ensures the records, then assigns player 0 and, once player 0 is bound, player 1; called by the menus, the modes and `HUD_Update` | confirmed (code) |
| `0x001ae898` | `HUD_GetVirtualPad(hud, player)` | the player's input record ([Front end](frontend.md#input)) | confirmed (code) |
| `0x001ae8d8` | `HUD_BindPad(hud, player, pad)` | frees the pad records the player owned, then `+0x19` = pad, `+0x1b` = (pad ≥ 0), the pad record's owner = player | confirmed (code) |
| `0x001ae980` | `HUD_ShutdownLevel` | undoes `HUD_InitLevel` (from `0x001607b8`): `+0x177a4` = 0, every part's shutdown, the `ANHud` panels freed, the six menus' `Shutdown` | confirmed (code) |
| `0x001aef50` | `HUD_IsPlayerStickPushed(hud, player)` | 1 unless both raw right-stick bytes `+0x1a`, `+0x1b` of the player's pad record are within 64-176 (stored with the right stick's floats, `0x00149990`; [Fixed-camera icon](hud.md#hud-fixed-cam-icon)) | confirmed (code) |
| `0x001af010` | `HUD_Update(hud, a, b)` | the HUD's frame ([The HUD's frame](hud.md#the-huds-frame)); `a` and `b` go to the two radars' updates (`0x001c5210`) | confirmed (code) |
| `0x001b1640` | `HUD_IsSplitScreen` | more than one player (game state `+0x224`) and more than one view (`0x0011eae0`) | confirmed (code) |
| `0x001b1688` | `HUD_Render` | the HUD's draw ([The HUD's frame](hud.md#the-huds-frame)) | confirmed (code) |
| `0x001b1f38` | `HUD_Hide` | `HideHud`'s work ([Showing and hiding](hud.md#showing-and-hiding)); does nothing in an Armies of the Night level while game state `+0x14c` = 1 | confirmed (code) |
| `0x001b2030`, `0x001b2088` | `HUD_HidePlayerPanelsBoth`, `HUD_ShowPlayerPanelsBoth` | each panel's "may show" `+0x4108` = 0 or 1, then hidden or shown | confirmed (code) |
| `0x001b20e0` | `HUD_SetPanelForceShow(hud, slot, on)` | panel `+0x4104` = on (`ForceShowPlayerHud`) | confirmed (code) |
| `0x001b20f8` | `HUD_Restore` | `RestoreHud`'s work ([Showing and hiding](hud.md#showing-and-hiding)) | confirmed (code) |
| `0x001b21a8` | `HUD_HideForMenu` | from the game menu's open (`0x001d1aa0`): the panels' hide, the `+0x15b0` widget's reset, both radars' blips off, both action prompts cleared | confirmed (code) |
| `0x001b2200` | `HUD_AttachPlayer(hud, human, slot)` | binds a panel to a human (`Human_MakePlayer`): for slot < 2 and a panel not locked (`+0x40f8`), init once (interface `+0x14`), reset (`+0x24`), bind (`+0x34`); hidden unless the HUD is shown; returns slot or -1 | confirmed (code) |
| `0x001b22f0` | `HUD_DetachPlayer(hud, slot)` | panel interface `+0x2c` (`Human_Destroy`) | confirmed (code) |
| `0x001b2330` | `HUD_ShowPanelsAfterScene` | both panels shown (`0x0020e028`: only if attached and allowed); [Who shows the HUD again](hud.md#who-shows-the-hud-again) | confirmed (code) |
| `0x001b2380` | `HUD_ApplyPanelsHide` | both panels hidden (`0x0020e058`) | confirmed (code) |
| `0x001b23d0`, `0x001b2400` | `HUD_ShowPlayerPanel`, `HUD_HidePlayerPanel` | the same for one slot (-1 ignored) | confirmed (code) |
| `0x001b2430`, `0x001b2460` | `HUD_ReportHealth`, `HUD_ReportPower` | call the panel's empty `0x0020dfd0`: not needed, no effect | confirmed (code) |
| `0x001b2490` | `HUD_ClearActionPrompt(hud, player)` | no text: the prompt hides ([Action prompts](hud-messages.md#action-prompts)) | confirmed (code) |
| `0x001b24d0` | `HUD_SetSpinner(rate, hud, on)` | `+0x19128` = rate, spinner shown (`+0xe054`, `+0xe058`) = on; also used by the memory-card and preload screens | confirmed (code) |
| `0x001b2500` | `HUD_ResetWidget15b0` | clears the `+0x15b0` widget's `+0x04` | confirmed (code) |
| `0x001b2520` | `HUD_SetWanted(hud, msg, player)` | messages 7-9: when the radar's wanted flag `+0x1c` (HUD `+0x15ec`) is 0 it becomes 1 and interface cue 2 (`hudalarm`) plays; the radar frame shows crime message `+0x290` of the game state, the last crime type (`0x001aa720`); other messages clear the flag and the message. `+0x30` = 1.0 and `+0x34` = 0 both ways. No code reads the three fields ([The crime message](#fn-radar-frame)) | confirmed (code) |
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
| `0x001b3918`, `0x001b3940` | `HUD_QueueScrollIn`, `HUD_FlushScrollIn` | the scroll-in queue's queue and `0x001c94c8` ([Objectives](hud-messages.md#objectives-hudsetobjective)) | confirmed (code) |
| `0x001b3960`, `0x001b39a0` | `HUD_TagAddHuman`, `HUD_TagRemoveHuman` | a human's handle into or out of the `+0x10` widget (`0x001a0420`, `0x001a07f0`) | confirmed (code) |
| `0x001b39e0`, `0x001b3b38` | `HUD_LockPickStart(hud, player, difficulty)`, `HUD_LockPickEnd` | the dial: difficulty, three zone flags (difficulty 0: none; 1: the middle one; other: the outer two), values 1, 2, 3, shown / hidden ([Crimes](crimes.md)) | confirmed (code) |
| `0x001b3b68`, `0x001b3ba8`, `0x001b3c78` | `HUD_MashMeterShow`, `_Hide`, `_Update` | the mash meter (`0x001a3310`, `0x001a3338`) | confirmed (code) |
| `0x001b3c00`, `0x001b3c30` | `HUD_StereoTheftStart`, `_End` | the stereo widget's start (`0x001ca118`) and shutdown | confirmed (code) |
| `0x001b3d10`, `0x001b3d28`, `0x001b3d58`, `0x001b3d80`, `0x001b3da8` | `HUD_SetArrowFlag`, `_SetArrowTarget`, `_SetArrowColour`, `_ArrowOn`, `_ArrowOff` | the [instruction arrow](hud-messages.md#the-instruction-arrow-hudenableinstarrow)'s setters | confirmed (code) |
| `0x001b3dd0`, `0x001b3df8` | `HUD_ActionCycleStart`, `_Stop` | a prompt's cycle animation | confirmed (code) |
| `0x001b3e20` | `HUD_WithdrawActionHint(hud, player)` | withdraws `+0x177bc[player]` from the hint box | confirmed (code) |
| `0x001b2818` | `HUD_SetFixedCamIconVisible(hud, on)` | `+0x177b4` = on; both fixed-camera icons' slot `+0x40` (the active flag `+0x08`, which gates drawing) | confirmed (code) |
| `0x001b28a0` | `HUD_SetReticuleRequest(hud, player, v)` | byte `+0x225d0[player]` | confirmed (code) |
| `0x001b2e10` | `HudManager_SetRadarRange(r, r2, hud)` | both radars' `+0x20`, `+0x28` = r and `+0x24` = r2 | confirmed (code) |
| `0x001b3cb0` | `HUD_ShowAnnouncement` | [Announcements](hud-messages.md#announcements-and-other-messages) | confirmed (code) |
| `0x001b3e78`, `0x001b3ea8` | `HUDInterface_StaticInit`, `HUDInterface_GlobalCtor` | the TU's initialiser (constructs the HUD object) and its global-constructor stub (table `0x00534104`) | confirmed (code) |
