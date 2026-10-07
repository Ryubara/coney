# The pause menu and the mission-failed screen

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made in
PCSX2 2.9.94 (2026-10-06) on a copy of quick-save slot 1 (`level99`, checkpoint 3), pressing the pad over PINE (the
`scripted-pad` patch, [Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)), reading memory and measuring
screenshots of the 640 × 448 picture; they say so.

## Purpose

What START does during play: the game mode that freezes the game, the menu it shows (its grid of items, each item's
screen, the confirmations), how the menu closes, and what Restart and Quit do. Also the screen a failed mission shows,
which is built from the same menu class. The widget classes underneath (`OptionGrid`, `UsageInfo`, the markup text
widget, the screen-flow stack) are on [GUI](gui.md#widget-classes); what the HUD shows during play is on
[The in-game HUD](hud.md).

In short: START pushes **game mode 0xa**, which switches the task manager to its pause phase, pauses the sound and
draws the world tinted towards black. Over it the **pause menu** shows a header (the mission's number and title), a
grid of seven items in two rows (**Objectives : Stats : Options** / **Controls : Restart : Resume : Quit**) and a usage
line; the item picked fills the space above the grid. Resume, triangle, or START (after 1.5 s) fade the menu out over
1.5 s and pop the mode. Restart and Quit ask Yes/No and then reload the level or leave to the front end. **There is no
map screen**: the game has no full-screen map, and the only map is the radar disc ([HUD](hud.md#the-radar-on-screen)).

## Original structure

The mode is one of the unnamed `GameModes/` units ([Boot](boot.md#original-structure)); the menu classes are in
`GUI/` between `TutorialHUD.cpp` and `ScrollingMenu.cpp`, in files without a path string except `OptionMenu.cpp` (the
Options screen) and `ControlMenuHUD.cpp` (the Controls screen) ([Source map](source-map.md#gui)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x005e6550` | mode 0xa | the pause mode (vtable `0x005386e8`) | confirmed (code) |
| `0x0015dbb8` / `0x0015dd98` / `0x0015dd38` | `PauseMode_Enter` / `_Update` / `_Exit` | | confirmed (code) |
| `0x00154f28` | `PauseMenu_Toggle` | push mode 0xa, or pop it and act on the menu's result | confirmed (code) |
| `0x001d1848` | `GameMenu_Construct` | base of the pause, mission-failed and Armies of the Night game-over menus, the Options and Controls menus and the Rumble result screen (vtable `0x0053b4b0`, [The GameMenu base](#fn-gamemenu)) | confirmed (code) |
| `0x001db3c0` | `PauseMenu_Construct` | the story pause menu (vtable `0x0053bdc0`, input interface `0x0053bd98` at `+0x5c`) | confirmed (code) |
| `0x0062e790` | the story pause menu | one static object (pointer at `0x0050eddc`) | confirmed (code) |
| `0x006239d0` | the Armies of the Night pause menu | a subclass (vtable `0x0053b298`, constructor `0x001cff48`; pointer at `0x0050ec34`; [its screen](#an-pause-menu)) | confirmed (code) |
| `0x001dbee0` / `0x001dcb20` | `PauseMenu_Open` / `_Close` | vtable `+0xa8` / `+0xb0` | confirmed (code) |
| `0x001ddcf8` / `0x001df700` | `PauseMenu_Update` / `_Render` | vtable `+0x30` / `+0x38` | confirmed (code) |
| `0x001dee00` | `PauseMenu_OnCommand` | the grid owner's command handler | confirmed (code) |
| `0x001de6c0` | `PauseMenu_SelectItem` | accept on a grid item | confirmed (code) |
| `0x001db278` | `PauseMenu_OnConfirm` | the Yes/No box's callback | confirmed (code) |
| `0x001d6738`, `0x001d6c10` | the Yes/No box: set-up, command handler | menu `+0x1bd0` | confirmed (code) |
| `0x0062a1a0` | the Options menu (`OptionMenu.cpp`, `0x001d84a0`; pointer at `0x0050ecec`) | | confirmed (code) |
| `0x001d1f88` | `MissionFailed_Launch` | `HUDLaunchMissionFailed` | confirmed (code) |
| `0x006294c0` | the mission-failed menu (vtable `0x0053b598`; pointer at `0x0050ec8c`) | open `0x001d23d0`, update `0x001d2b90` | confirmed (code) |

A level whose number is 60-69 (`0x0041d110`: the Armies of the Night stages, [Levels](../references/levels.md)) uses
the Armies of the Night menu; every other level the story menu. "Rumble level" below means a level numbered 100 or
more (`0x0041d160`), where the same menu offers other items.

## Data

### The menu's fields

The story menu (at least `0x4354` bytes: the Stats screen at `+0x3bb0` reaches `+0x4350`); confirmed (code) at the
functions in the table above.

| Offset | Meaning |
| --- | --- |
| `+0x10` | intro finished: set once the grid has appeared (`0x001d4418`) |
| `+0x40` | the input record of the pausing player (`0x001ae898(hud, player)`) |
| `+0x1b70` | quit chosen |
| `+0x1b74` / `+0x1b78` | restart the level / restart from the last checkpoint chosen |
| `+0x1b7c` | closing finished: pop the mode when the last sound has ended |
| `+0x1b80` | closing: fade the menu out |
| `+0x1b84` | the sound of the last accept or back (`0x0010f578` tells when it has ended) |
| `+0x1b88` | an item's screen is open |
| `+0x1b8c`, `+0x1b90`, `+0x1b94`, `+0x1b98`, `+0x1b9c` | which quit: second player, Rumble, to the hangout, Rumble hangout, main menu |
| `+0x1ba0` | the Yes/No box is open |
| `+0x1ba4` | the Stats screen is open |
| `+0x1ba8` | the Objectives screen is open |
| `+0x1bac` | "to the hangout" is offered (`0x0041d1b0`, below) |
| `+0x1bb0` | the Quit screen offers two destinations |
| `+0x1bb4` / `+0x1bb8` | the Quit / Restart screen is open |
| `+0x1bc0` | the player who paused (0 or 1) |
| `+0x1bd0` | the Yes/No box (callback `+0x4c4`, owner `+0x240`, selection `+0x4c0`: 0 Yes, 1 No) |
| `+0x20ac` | START cool-down: set to 1 on close, counted down by `HUD_Update` each frame of play |
| `+0x20b0` | save wanted on close (pushes the memory-card mode 6, `0x00155308`) |
| `+0x20d0`, `+0x2430`, `+0x2790` | three checklists (`ChecklistMessageHUD.cpp`): current, bonus and overview objectives |
| `+0x2af0`-`+0x2af8` | three headers (pointers to `0x006340c0`, `0x00634c70`, `0x00634fe0`) |
| `+0x2b00` | the item grid (`OptionGrid`) |
| `+0x2dd0` | the usage line (`UsageInfo`) |
| `+0x30a0` | the Controls screen (`ControlMenuHUD.cpp`) |
| `+0x3a74` | the selection inside an item's screen |
| `+0x3a78` / `+0x3a7c` | the Options / Controls screen is open |
| `+0x3a90` | the background sprite |
| `+0x3b90`, `+0x3b94` | background fade-in start and length (3,000 ms) |
| `+0x3b98`, `+0x3b9c` | fade-out start, and the menu's alpha (255 open) |
| `+0x3ba4` | when the menu opened (START closes it only 1,500 ms later) |
| `+0x3ba8`, `+0x3bac` | background colour cycle: start time and colour index 0-2 |
| `+0x3bb0`, `+0x3bb8` | the Stats screen and whether it is available |

### Layout (GUI coordinates)

Default video mode, [GUI coordinates](hud.md#coordinates). Confirmed (code) at `0x001dbee0`, `0x001db550`,
`0x001ddcf8` and the constants at `0x0050ede0`-`0x0050ee8c`.

| Part | Where |
| --- | --- |
| background | sheet record 12 rectangle 5 (sprite word `0xc0005`), centred at (0.5, 0.5), size 1.05 (0.9 when the device's slot `+0xb8` is set), depth 7,000, white tinted by the colour cycle |
| three headers | `CircledTextHeader`s (`0x001e7f50`, `0x001e77a0`) at y 0.22, 0.46 and 0.68; built with "Current Objectives" (`0x108`), "Bonus Objectives" (`0x107`) and "Overview" (`0x106`), their texts swapped per screen (the top one shows the mission's title on the grid, "OPTIONS" `0x116` on Options, "CONTROLS" `0x173` on Controls) |
| item grid | rows of 3 and 4 items (3 and 3 in `level95`, the hangout, and in Rumble levels), centred on x 0.5, first row at y 0.86, font scale 1.15, grey `0x005fd320`, font slot 3, a `" : "` after every item but the last of a row |
| usage line | centred at (0.5, 0.97) |
| Yes/No box | the question at (0.26, 0.55), the answers at (0.6, 0.75) (`0x0050ee10`-`0x0050ee1c`) |

**Runtime check** (screen fractions turned into GUI units): the grid's rows at y 0.86 and 0.91, the usage line at 0.97,
the quit question's left edge at x 0.26 and its "Yes/No" at (0.60, 0.75).

**16:9** (device flags `0x04`): the background becomes 1.4 wide and 1.1 high (0.9 high when flag `0x02` is also set)
and is drawn with anchor 2 (`0x001a21e8`). Confirmed (code) at `0x001ddcf8`.

### The items

The grid items (`0x001dbee0`), each with the code `PauseMenu_SelectItem` acts on. Texts are global strings
([GUI: strings](gui.md#strings)); the names here are their English text:

| Code | Story level | Rumble level | Separator after it |
| --- | --- | --- | --- |
| 0 | Objectives (`0xfa`) | Rules (`0xfb`) | yes |
| 1 | Stats (`0xf8`) | (none) | yes |
| 2 | Options (`0xf7`) | Options | no (end of row 1) |
| 3 | Controls (`0xf9`) | Controls | yes |
| 4 | Restart (`0xfe`), not in `level95` | Replay (`0xff`) | yes |
| 5 | Resume (`0xfc`) | Resume | yes |
| 6 | Quit (`0xfd`) | Quit | no |

The first item is selected when the menu opens. Confirmed (code); the story order and look confirmed (runtime).

## Behaviour

### Pausing

**When.** Mode 1's update ([Level loading](level-loading.md)) checks each player with a pad record (per-player
`+0x1b` set): START pressed (pad mask `0x800`), or the game state's `+0x11c` set (the Options menu requested by
`ShowOptionMenu`, inferred); and the player panel's interface `+0x8c` returns 0, the human is not down
(`0x00227dd8`), and no level end is under way (`W_GameState + 0x14c` = 0, or an Armies of the Night level). Then it
stores the player in the menu's `+0x1bc0` and calls `PauseMenu_Toggle`. Confirmed (code) at `0x00158728`.

**`PauseMenu_Toggle`** (`0x00154f28`): does nothing while the cool-down `+0x20ac` is non-zero; if the top mode is not
0xa it pushes mode 0xa; otherwise it pops it and acts on the menu's result ([Leaving](#leaving)). Confirmed (code).

**Mode 0xa `Enter`** (`0x0015dbb8`), confirmed (code):

1. picks the menu (story or Armies of the Night) into the mode's `+0x24`;
2. switches the task manager to **phase 1** (the pause wheel, [Tasks](tasks.md#clocks)), so the game objects' updates
   stop;
3. remembers the menu clock, **pauses all sound** (`SoundPauseSound`'s worker `0x0010fb20`, [Sound](sound.md)) and
   sets `W_GameState + 0x56e0` = 1;
4. stops the vibration of every pad (pad records `0x005dd810` + 0x50 × *n*, bytes `+0x40` and `+0x41` = 0);
5. opens the menu (vtable `+0xa8`), saves the sound state into the mode (`0x001115a8`) and starts the sound group
   `"pause"` (`0x0010fa50(sound, "pause", 0)`).

**Mode 0xa `Update`** (`0x0015dd98`) runs the frame like mode 1 without the game: the tick, task-manager phase 1, the
cameras, the humans' skeleton marking and phase 1's managers (when `0x005e536c` is 1), the world and resource managers,
the audio listener, the menu's `Render` then the overlays, the file manager, the HUD's paused update (`0x001ae828`) and
the menu's `Update`. It always returns "stay"; the menu pops the mode itself. Confirmed (code).

**Mode 0xa `Exit`** (`0x0015dd38`): closes the menu (vtable `+0xb0`), restores the sound state and resumes the sound
(`0x0010fb68`). Confirmed (code).

### Opening the menu

`PauseMenu_Open` (`0x001dbee0`), confirmed (code):

- tints both views' screen effects ([Graphics](graphics.md#screen-effects)) to **black over 1.4 s** (`0x0018c988(1.4,
  manager, black)`, `0x0050ee5c`), so the game world darkens behind the menu;
- takes the pausing player's input record, prepares the Stats screen for that player (`0x001e5618`);
- sets up the three headers and their checklists; the top header's text is the global string `0xe7` or `0xe8`
  (chosen by whether the current checklist holds `<ROBJ_N>`), strings no front-end table sets (inferred: set per
  mission from Lua; runtime, `level99`: the circled "1" and "B", "Coney" and "New Blood");
- builds the grid ([The items](#the-items)) and focuses it;
- starts the background's fade-in (3,000 ms) and colour cycle, sets the menu alpha to 255;
- turns both radars off (`0x001b2658`, after setting each radar's `+0x04`).

When the game state's `+0x11c` is set, it opens straight onto the Options screen (header "OPTIONS", `0x116`).

**The background colour cycle** (`PauseMenu_Update`): three colours at `0x00635380`, **(150, 50, 50)**, **(50, 50,
150)**, **(50, 150, 50)**, each blended linearly to the next over **5,000 ms** (`0x0050ee88`), forever; the alpha
rises from 0 to 255 over the first 3,000 ms. Confirmed (code); the cycle confirmed (runtime: screenshots a few seconds
apart show the tagged background purple, blue, green and red-brown).

### Input

The grid ([OptionGrid](gui.md#widget-classes)) passes every command to `PauseMenu_OnCommand` (`0x001dee00`) first.
Commands 0-5 are up, down, left, right, accept and back. Nothing is taken while closing (`+0x1b7c`, `+0x1b80`), and
while the Yes/No box is open every command goes to it (`0x001ded68`: up and down play cue `0xe`, accept cue 8, left
and right move between Yes and No). Confirmed (code). Without an item's screen open:

- **left / right / up / down** move in the grid (the grid's own handling);
- **accept** (cross): cue 8, set `+0x1b88` and run `PauseMenu_SelectItem` with the selected code;
- **back** (triangle): cue `0xf`, then **resume** (`+0x1b80` = 1). Confirmed (runtime: triangle on the grid resumed
  the game).

With an item's screen open, back closes the screen (`0x001de488`) and returns to the grid; up and down move the
screen's selection `+0x3a74` (cue 4, or `0xe` at an end), and accept acts on it. **START** closes the menu from
anywhere once 1,500 ms (`0x0050ee8c`) have passed since it opened (`+0x3ba4`). Confirmed (code).

### The items' screens

`PauseMenu_SelectItem` (`0x001de6c0`), confirmed (code); each confirmed (runtime) by a screenshot:

- **Objectives** (0): the three headers stacked, the selected one expanded to its list; up and down choose the header
  (selection 0-2, starting at 0, or 2 in a Rumble level). An empty list shows "None" (`0x109`, `0x001dd0b8`). The
  lists are the checklists `HUDSetObjective` fills ([HUD: objectives](hud.md#objectives-hudsetobjective)). Usage line
  `0x19` (up/down select, triangle back). Selected header gold (current) or purple (bonus), the others grey.
- **Stats** (1): only when the Stats screen is available (`+0x3bb8`): the character's portrait, the name banner, four
  bars (Strength `0xf3`, Stamina `0xf4`, Health `0xf5`, Rage `0xf6`) and a description paragraph. Otherwise nothing
  opens.
- **Options** (2): opens the Options menu (`0x0062a1a0`, `OptionMenu.cpp`) inside the pause menu, header "OPTIONS"
  (`0x116`). Runtime: Lighting, Camera, Audio, Video, Subtitles, Vibration, Restore Default.
- **Controls** (3): header "CONTROLS" (`0x173`) and the Controls screen (`ControlMenuHUD.cpp`); runtime: two entries,
  Controller (a picture of the pad with each button's action) and Tutorial (a list of tips, the selected one red with
  its text under it).
- **Restart** (4): in a story level two choices, **Restart Level** (`0x112`, described by `0x113`) and **Restart Last
  Check Point** (`0x114`, `0x115`), the second selected (runtime: "Restart Last Check Point" white with its
  description). Accept opens the Yes/No box with `0x105` ("replay? all progress on this level lost") for the level or
  `0x103` (the checkpoint's question). In a Rumble level Replay asks `0x105` at once.
- **Resume** (5): closes the menu.
- **Quit** (6): the Yes/No box with `0x102` ("quit? all progress on this level lost"). When the hangout can be offered
  (`+0x1bac`) it first shows two choices, To Hangout (`0x111`, `0x10e`) and Main Menu (`0x10f`, `0x10d`); with two
  players in a co-op game, Player 2 (`0x10a`, quit the second player) and All (`0x10c`); in a Rumble level, To Rumble
  Mode (`0x110`) and To Hangout or To Main Menu.

**The Yes/No box** (`0x001d6738`, `0x001d6910`): the question, then "Yes" (`0x3b`) "/" "No" (`0x3c`), **No selected**
(confirmed (runtime)). Yes calls `PauseMenu_OnConfirm` with 2, No with 3, back with 4; No and back close the box
(vtable `+0xcc`, `0x001df5e8`, clearing the quit and restart choices). Yes, story: Quit sets `+0x1b70` and closes
(`0x001db918`); a restart closes (`0x004e8718`; the choice was set when the box opened); quitting the second player
runs `0x001df3a8` (the menu stays open). Yes, Rumble: `0x001df448`, `0x001df4c8` or `0x001df4b0` set the Rumble quit
flags and close. Confirmed (code).

**`+0x1bac`** (`0x0041d1b0`) is set in 15 story levels (numbers 34, 2, 3, 5, 81, 86, 31, 14, 9, 51, 82, 92, 83, 20 and
11) and in any level the stats object (`0x006fe998`, `0x004241d8`) marks; confirmed (code). That these are the
missions played after the hangout opens is inferred.

### Leaving {#leaving}

**Closing** (`PauseMenu_FadeOut`, `0x001ddbc0`): once `+0x1b80` is set the menu's alpha falls from 255 to 0 over
**1,500 ms** (`0x0050ee80`); 500 ms (`0x0050ee84`) after it reaches 0, `+0x1b7c` is set; when the last accept or back
sound has ended, `PauseMenu_Update` calls `PauseMenu_Toggle`, which pops mode 0xa. The screen effects keep their tint
until the mode is gone (the menu writes `+0x214` of both managers, not traced). **Runtime check:** after triangle the
mode stack still held the pause mode at 1.5 s and was back to mode 1 at 3.5 s; the screen was black at 1.5 s.

**Then `PauseMenu_Toggle`** (`0x00154f28`), confirmed (code):

1. if `+0x20b0` (a save is wanted): push the memory-card mode (`0x00155308`, when the save system has a card and the
   level allows it);
2. set the cool-down `+0x20ac` = 1 (START is ignored for one frame);
3. **nothing chosen**: back to play;
4. **Restart Level** (`+0x1b74`): checkpoint 1 (`0x0041cef0(gs, 1)`, game state `+0x33a`), then reload the level by
   its record's name (`0x00160d78(gs + 0x124)`: `W_GameState + 0x14c` = 3 and mode 8 loads it,
   [Front end](frontend.md#mode-flow));
5. **Restart Last Check Point** (`+0x1b78`): keep the current checkpoint (`+0x33a`) and reload the level the same way;
6. **Quit** (`+0x1b70`): to the hangout (`+0x1b94`): `W_GameState + 0x14c` = 3 and Lua `runNextMission(0)`
   ([Scripts](scripting.md#run-next-mission)); Rumble hangout or quick menu (`+0x1b98`): Lua `PauseGoToRMIHangout`
   or `PauseGoToRMIQuick` (by `0x001fe1a8`); otherwise (main menu, or a story level without the choice): checkpoint
   1 and load the level named `"menu"` (`0x00160d78("menu")`), the front end.

What a reload restores from `SetCheckPoint` is on [Scripts](scripting.md#run-next-mission).

### The mission-failed screen

`HUDLaunchMissionFailed(reason)` (`MissionFailed_Launch`, `0x001d1f88`) sets `W_GameState + 0x118` = 2, stores the
reason (`0x001d1fb8`) and pushes **mode 0xc** (`0x00155408`), whose `Enter` (`0x0015cae0`) switches the task manager to
phase 1, saves the sound state (`0x001115a8`; that `0x00110548` and `0x0010fba8` stop it is inferred) and opens the
mission-failed menu. Confirmed (code). The engine's own failure (players out or busted) reaches the same menu after a 180-update
countdown ([Combat](combat.md#defeat)).

The menu (`MissionFailedMenu_Open`, `0x001d23d0`), confirmed (code):

- both views get screen effect 1 over 2 s (`0x0018d450(2.0, manager, 1)`, [Graphics](graphics.md#screen-effects));
- a title sprite of the sheet record `0x20b`, `0x20c` or `0x20d` by language (`W_GameState + 0x120`: 1 `0x20d`, 2
  `0x20c`, others `0x20b`; inferred: the "mission failed" picture), at (0.5, 0.38), size 0.4, grey 191, depth 11,000;
- the reason in the markup text widget at (0.52, 0.49) (0.38 on the first frame), red `(170, 43, 43)`, `big_font`, or
  "Unknown Reason" (`0xda`) when none was given;
- the grid at y 0.57: rows of 1 and 2, **Last checkpoint** (`0xd7`), then **Restart level** (`0xd8`) : **Quit**
  (`0xd9`); in `level95` one row, To Hangout (`0xdf`) : Quit;
- the usage line `0x1b` at (0.5, 0.68); a Yes/No box for Quit (`0x102`), the question at (0.08, 0.75) and the answers
  at (0.44, 0.87), No selected.

What each item does goes through `runNextMission(0)` and the reload above ([Scripts](scripting.md#run-next-mission));
the items' actions are in `MissionFailedMenu_OnCommand` (`0x001d2a10`) and the Yes/No callback `0x001d2018`
([The mission-failed menu](#fn-missionfailed)).

## Function index {#function-index}

Every function of the pause, options and controls menus, by source file in address order, with the name it has in Ghidra
(ours). Rows link to the section that describes the behaviour where there is one. The files and ranges are from the
[Source map](source-map.md#gui).

### The Armies of the Night game-over screen {#an-game-over}

A `GameMenu` (vtable `0x0053b1b0`, input `0x0053b188`, object `0x00623040`, colour `0x006239c0` = (150, 30, 30, 255))
in `TutorialHUD.cpp`'s TU (inferred); `HUDLaunchANGameOver` and mode 0xd open it
([HUDLaunchANGameOver](../references/bindings/hud.md#hudlaunchangameover)). Positions are GUI coordinates. Confirmed
(code):

- **Countdown phase:** the title (string `0xce`, font slot 6, scale 2.0) at (0.5, 0.45), a digit counting **9 down to
  0, one step per 1,000 ms** (`0x0050ec0c`, scale 5.0) at (0.5, 0.6), the usage line `0xd6` at (0.5, 0.8). Start
  (`0x800`) from either player (player 2's pad record is locked at open) plays cue 8; with credits left
  (`0x00619910`) it spends one, sets close, calls the Lua function `F1.Time` and, while credits remain, shows both
  panels' join message; `+0x7ac`/`+0x7b0` record which players continue (alive ones). With no credit, or when the
  count ends, both sound channels fade (`0x0018d450(2.0, ...)`) and the end phase starts.
- **End phase:** the text `0xcf` (scale 3.0) at (0.5, 0.5), a grid of `0xdb` : `0xdc` at (0.5, 0.57), the usage line
  `0x1c` at (0.5, 0.62), fading in over 2,000 ms (`0x0050ec28`); then accept takes item 0 (`+0x7a4`) or 1 (`+0x7a8`).
- It closes (`ANGameOver_Toggle`) once the close flag is set and the last sound has ended.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001cede8` | `HUD_LaunchANGameOver` | calls `ANGameOver_Toggle` | confirmed (code) |
| `0x001cee08` / `0x001cee58` | `ANGameOver_Construct` / `_Destroy` | the base plus the digit (`+0x7c0`) and title (`+0x890`) `TextWidget`s | confirmed (code) |
| `0x001ceec0`, `0x001ceef0` | `ANGameOver_Init`, `_Shutdown` | slots `+0x90`, `+0x68`: the base's | confirmed (code) |
| `0x001cef10` | `ANGameOver_Open` | slot `+0xa8`: patches its layout per video mode, then builds the parts above, count 9 | confirmed (code) |
| `0x001cf358` | `ANGameOver_Close` | slot `+0xb0`: the base's close, releases the texts, unlocks player 2's record | confirmed (code) |
| `0x001cf3a8` | `ANGameOver_OnCommand` | the input handler: accept after the fade, cue 8, item 0 or 1, close; calls `0x004229c0` on the stats object (`0x006fe490`) | confirmed (code) |
| `0x001cf468` | `ANGameOver_Update` | slot `+0x30`: Start, credits, countdown, phases | confirmed (code) |
| `0x001cfc78` | `ANGameOver_Render` | slot `+0x38`: the phase's parts; the end fade | confirmed (code) |
| `0x001cfe50`, `0x001cfea0` | `ANGameOver_StaticInit`, `_GlobalCtor` | builds the object and its colour; the GCC constructor stub (list entry `0x00534114`) | confirmed (code) |

### The Armies of the Night pause menu {#an-pause-menu}

A `PauseMenu` subclass (vtable `0x0053b298`, input `0x0053b270`, object `0x006239d0`, colour `0x006294b0` = (170, 20,
20, 255)), in its own TU (stub `0x001d0a88`; inferred), used in levels 60-69. Confirmed (code):

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001cfec0` | `ANPauseMenu_OnConfirm` | the Yes/No callback: 2 (yes) calls the menu's slot `+0xb8`, 3 or 4 its slot `+0xc8` (targets `0x004e8248` / `0x004e8260`, not analysed) | confirmed (code) |
| `0x001cff48` / `0x001cff98` | `ANPauseMenu_Construct` / `_Destroy` | the base plus a title `TextWidget` (`+0x5930`) and a grid (`+0x5a00`) | confirmed (code) |
| `0x001d0010` | `ANPauseMenu_Open` | slot `+0xa8`: the pausing player's input record; title `0xd0` (font slot 6, scale 2.95) at (0.5, 0.46); usage `0x1c` at (0.5, 0.58); a grid `0xd1` : `0xd2` (scale 1.15) at (0.5, 0.535); a Yes/No box, question `0x102`, answers `0x3b` / `0x3c`, at (0.28, 0.65) and (0.73, 1.45), callback `0x001cfec0` | confirmed (code) |
| `0x001d0448` | `ANPauseMenu_Close` | slot `+0xb0`: releases the title, grid and box | confirmed (code) |
| `0x001d0480` | `ANPauseMenu_OnCommand` | accept: item 0 closes (resume), item 1 opens the Yes/No box and calls `0x004229c0` on the stats object; back closes; while the box is open, input goes to `0x001ded68` | confirmed (code) |
| `0x001d0548` | `ANPauseMenu_Update` | slot `+0x30`: Start closes; places the parts; the selected item's alpha falls and rises over 2 × 1,000 ms (`0x0050ec5c`); closes through `PauseMenu_Toggle` once the last sound ends | confirmed (code) |
| `0x001d09d8` | `ANPauseMenu_Render` | slot `+0x38`: title, grid, usage, and the box when open | confirmed (code) |
| `0x001d0a38`, `0x001d0a88` | `ANPauseMenu_StaticInit`, `_GlobalCtor` | builds the object and its colour; the constructor stub (list entry `0x00534118`) | confirmed (code) |

### The `GameMenu` base (no path string) {#fn-gamemenu}

`0x001d1848`-`0x001d1d28`, in the TU that ends at the stub `0x001d2de0` (with `CircledText` and the mission-failed
menu). The base of the story pause menu, the mission-failed and Armies of the Night game-over screens, the Options and
Controls menus and the Rumble result screen (vtable `0x0053b4b0`, input interface `0x0053b488` at `+0x5c`). Fields:
`+0x40` the locked pad record, `+0x60` close requested, `+0x64` input accepted (set when the fade-in ends), `+0x70`
background (`BaseWidget`), `+0x170` title (markup text), `+0x360` grid (`OptionGrid`), `+0x430` usage line
(`UsageInfo`), `+0x700` fade start, `+0x704` open time, `+0x708` fading, `+0x70c` title text, `+0x78c` the last
sound. Confirmed (code).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001d1848` / `0x001d18d8` | `GameMenu_Construct` / `_Destroy` | the parts above; sound handle -1; slot `+0x60` | confirmed (code) |
| `0x001d1958` | `GameMenu_Init` | slot `+0x90`: sets up the title text (size 1.0, grey 128) | confirmed (code) |
| `0x001d19a8` | `GameMenu_Shutdown` | slot `+0x68`: slot `+0x98`, clears the title | confirmed (code) |
| `0x001d19e8` | `GameMenu_ReleaseSprites` | slot `+0x98`, shared by every subclass: releases the background and the grid | confirmed (code) |
| `0x001d1a18` | `GameMenu_SetTitle` | slot `+0xa0`: copies the text to `+0x70c` and shows it; nil clears | confirmed (code) |
| `0x001d1aa0` | `GameMenu_Open` | slot `+0xa8`: hides the HUD for a menu and both radars, ends both players' rage mode (`Human_EndRageMode`), locks player 1's pad record (`+0x1b` = 0, `+0x1e` = 1), stamps the open and fade times, clears `+0x10`, `+0x60`, `+0x64` | confirmed (code) |
| `0x001d1c48` | `GameMenu_Close` | slot `+0xb0`: slot `+0x98`, both sound channels back (`0x0018d450(0, channel)`), unlocks the pad record | confirmed (code) |
| `0x001d1cd8` / `0x001d1d08` | `GameMenu_IsBackgroundLoaded` / `_IsSoundDone` | the background's batch is resident; the last sound has ended | confirmed (code) |
| `0x001d1d28` | `GameMenu_Render` | slot `+0x38`: from 1,000 ms after opening (`0x0050ec7c`) fades background, title, grid and usage in over 3,000 ms (`0x0050ec78`); at the end plays the sound named at `0x00555c88`, sets `+0x64` and hides the 19 widgets at HUD `+0x137e0` (not identified) | confirmed (code) |

### The mission-failed menu {#fn-missionfailed}

A `GameMenu` (vtable `0x0053b598`, input `0x0053b570`, object `0x006294c0`, pointer `0x0050ec8c`;
[The mission-failed screen](#the-mission-failed-screen)). Fields: `+0x7a0` quit, `+0x7a4` restart, `+0x7a8` last
checkpoint, `+0x7ac` to the hangout chosen; `+0x7b0` the `HUDSetMissionFailedCallbacks` name (31 bytes); `+0x7d0` the
`level95` layout; `+0x7e0` the Yes/No box (question text `+0x830`); `+0xcb0` the box is open. Confirmed (code).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001d1f88` | `MissionFailed_Launch` | [The mission-failed screen](#the-mission-failed-screen) | confirmed (code) |
| `0x001d1fb8` | `MissionFailed_SetReason` | the reason through the menu's `SetTitle` (slot `+0xa0`) | confirmed (code) |
| `0x001d1ff0`, `0x001d2398` | `MissionFailed_SetCallbacks`, `MissionFailedMenu_SetCallbacks` | `HUDSetMissionFailedCallbacks`: copy the name (nil clears) | confirmed (code) |
| `0x001d2018` | `MissionFailedMenu_OnConfirm` | the Yes/No callback: 2 (yes) closes the menu with the choice; 3 or 4 close the box and clear the four choices | confirmed (code) |
| `0x001d2070` / `0x001d20c8` | `MissionFailedMenu_Construct` / `_Destroy` | the base plus the Yes/No box | confirmed (code) |
| `0x001d2128` | `MissionFailedMenu_Init` | slot `+0x90`: per video mode and for languages 1 and 2 (`W_GameState + 0x120`) patches the layout globals `0x0050ec94`-`0x0050ecb4`; the base's init | confirmed (code) |
| `0x001d2368` | `MissionFailedMenu_Shutdown` | slot `+0x68` | confirmed (code) |
| `0x001d23d0` | `MissionFailedMenu_Open` | slot `+0xa8` ([above](#the-mission-failed-screen)) | confirmed (code) |
| `0x001d2970` | `MissionFailedMenu_YesNoInput` | while the box is open: up/down cue `0xe`, accept cue 8, then `YesNoBox_OnCommand` | confirmed (code) |
| `0x001d2a10` | `MissionFailedMenu_OnCommand` | accept (cue 8): item 0 closes with "last checkpoint" (`+0x7a8`) or, in `level95`, "to the hangout" (`+0x7ac`); item 1 asks "restart?" (string `0x105`, `+0x7a4`), or in `level95` "quit?"; item 2 asks "quit?" (`0x102`, `+0x7a0`); the box opens with No selected | confirmed (code) |
| `0x001d2b90` | `MissionFailedMenu_Update` | slot `+0x30`: waits until the background, usage line and grid are ready, then places the picture, reason, grid, usage and box; closes through `MissionFailed_Toggle` once the sound ends | confirmed (code) |
| `0x001d2d68` | `MissionFailedMenu_Render` | slot `+0x38`: the base's render, plus the box when open | confirmed (code) |
| `0x001d2db0`, `0x001d2de0` | `MissionFailedMenu_StaticInit`, `_GlobalCtor` | builds the object; the constructor stub (list entry `0x0053411c`) | confirmed (code) |

### `GUI/OptionMenu.cpp` {#fn-optionmenu}

`0x001d5808`-`0x001dab30`: the pause menu's Options screen. `OptionMenu` (vtable `0x0053b9a8`, input `0x0053b980` at
+0x5c, object `0x0062a1a0`, pointer `0x0050ecec`) owns 7 page titles (+0x7a0 step 0x1f0) and 7 `OptionPage`s (+0x1530
step 0x6f0: count +0, selection +4, index +8, 5 item pointers +0x0c, description +0x20, confirm box +0x220, box open
+0x210, changed +0x214). Items: `OptionItem` base (vtable `0x0053bcd0`), `OptionItemOnOffType` = `YesNoBox`
(`0x0053bc08`, 0x4d0), `OptionItemStatBarType` = `StatBarItem` (`0x0053bb38`, 0x640), `OptionItemLightingType` =
`LightingItem` (`0x0053ba68`, 0xa40). The option values and their defaults: [save.md](save.md#options).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001d5808` | `OptionPage_OnDiscardConfirm` | Page confirm-box callback: 2 reverts the page, 3/4 close the box. No code in the file opens that box (string 0x12f). | inferred |
| `0x001d5870`, `0x001d58a8` | `OptionPage_Construct`, `OptionPage_Destroy` | Description markup +0x20, confirm box +0x220. | confirmed (code) |
| `0x001d5900` | `OptionPage_Init` | Clears count, selection and 5 slots, index +8; per-mode box layout `0x0050ed18`-`0x0050ed24`; box string 0x12f with Yes 0x3b / No 0x3c. | confirmed (code) |
| `0x001d5b30`, `0x001d5bc8` | `OptionPage_Shutdown`, `OptionPage_AddItem` | Frees the 5 items, description and box; adds to the first free of 5 slots. | confirmed (code) |
| `0x001d5c38` | `OptionPage_SetDescription` | Description markup (size 1.0, `0x005fd310`, font slot 3, scale 1.3) at a position. | confirmed (code) |
| `0x001d5cf0` | `OptionPage_OnConfirmBoxCommand` | Routes a command to the confirm box (up/down cue 0xe, accept cue 8). | confirmed (code) |
| `0x001d5dc8` | `OptionPage_OnCommand` | Up/down move +4 (cue 4, 0xe at an end); left/right to the item (2/3); accept: item command 4, cue 8, close; back cue 0xf: Vibration (5) stops the preview, Restore Default (6) closes, others revert first. Returns 0 to close. | confirmed (code) |
| `0x001d6060`, `0x001d60e8`, `0x001d6160` | `OptionPage_IsLoaded`, `OptionPage_BeginEdit`, `OptionPage_Revert` | Every item's slot +0x98 (loaded), +0xa0 (remember value), +0xa8 (restore and apply). | confirmed (code) |
| `0x001d61d8`, `0x001d62d8` | `OptionPage_Update`, `OptionPage_Render` | Items, description, box when open; selected item `0x005fd310`, others `0x005fd320` via slot +0x78. | confirmed (code) |
| `0x001d63f0`, `0x001d6430`, `0x001d64b0` | `OptionItem_Construct`, `_Destroy`, `_Setup` | Label markup +0x50; setup (slot +0x90) stores metrics +0x40 and the label (size 1.0, `0x005fd310`, slot 3), +0x240 = 2. | confirmed (code) |
| `0x001d6570`, `0x001d6590`, `0x001d65c8` | `OptionItem_SetPosition`, `_SetLabelScale`, `_Update` | Moves the label; label font size from a scale; updates the label. | confirmed (code) |
| `0x001d65e8`, `0x001d6620` | `OptionItem_Render`, `OptionItem_RenderColoured` | Label in `0x005fd310`, or in a given colour (slot +0x78). | confirmed (code) |
| `0x001d6480` | `YesNoBox_SetLabelFlag` | Label fields +0x1d8 and +0x208 = flag, then re-lays the label; every caller passes 1. | inferred |
| `0x001d6670`, `0x001d66d0` | `YesNoBox_Construct`, `YesNoBox_Destroy` | Answer texts +0x250, +0x320 and the "/" +0x3f0; selection +0x4c0. | confirmed (code) |
| `0x001d6738` | `YesNoBox_Setup` | Label, answers 299/300, callback +0x4c4, owner +0x240, confirm mode +0x4c8. | confirmed (code) |
| `0x001d6908`, `0x001d6910`, `0x001d6960`, `0x001d6980` | `YesNoBox_Shutdown`, `_SetAnswers`, `_SetPosition`, `_SetAnswersPosition` | Empty shutdown; replace the answers; move the label; lay out answer, "/", answer by width. | confirmed (code) |
| `0x001d6a50`, `0x001d6a60` | `YesNoBox_SaveSelection`, `YesNoBox_RevertSelection` | Slot +0xa0 saves the selection at +0x4cc (was undefined, created); slot +0xa8 restores it and calls the callback. | confirmed (code) |
| `0x001d6a90`, `0x001d6ad0`, `0x001d6b00` | `YesNoBox_Update`, `_Render`, `_RenderColoured` | Chosen answer `0x005fd310`, the other `0x005fd320`. | confirmed (code) |
| `0x001d6c10` | `YesNoBox_OnCommand` | Left 0, right 1 (cue 4, 0xe if unchanged; page changed); immediate callback unless confirm mode; accept in confirm mode calls back 2 or 3, back 4. | confirmed (code) |
| `0x001d6df8`, `0x001d6e80`, `0x001d7218` | `StatBarItem_Construct`, `_Destroy`, `_Shutdown` | Fill bar +0x250, back bar +0x2c0, three widgets; value +0x630 = 0.5. | confirmed (code) |
| `0x001d6ef8` | `StatBarItem_Setup` | Slot +0xc0: sheet-12 rect-0 sprite (0xc0000), white fill and (21,21,21) back bar of height 0.025 (`0x0050ed2c`), sprite 0x30036, value clamped 0-1, callback +0x638. | confirmed (code) |
| `0x001d7238`, `0x001d7248` | `StatBarItem_SaveValue`, `StatBarItem_RevertValue` | +0x634 = value; restore and call back. | confirmed (code) |
| `0x001d7270` | `StatBarItem_Update` | First update offsets the bars by 0.003 / -0.004 (`0x0050ed38`, `0x0050ed34`), back bar 0.04 (`0x0050ed30`) higher. | confirmed (code) |
| `0x001d7350`, `0x001d73b0`, `0x001d7420` | `StatBarItem_Render`, `_RenderColoured`, `_IsLoaded` | Label, back then fill bar; tinted fill; bar and sprite batches resident. | confirmed (code) |
| `0x001d7490` | `StatBarItem_OnCommand` | ±0.05 clamped 0-1 (cue 4, 0xe at an end), page changed, callback(value). | confirmed (code) |
| `0x001d7648`, `0x001d7698`, `0x001d7c70` | `LightingItem_Construct`, `_Destroy`, `_Shutdown` | A StatBarItem plus two square widgets and a hint markup. | confirmed (code) |
| `0x001d7700` | `LightingItem_OnCommand` | Bar ±0.05 and callback ±5 (`0x0050ecfc`) into OptionMenu_AddBrightness. | confirmed (code) |
| `0x001d7958` | `LightingItem_Setup` | Bar at level/100 (`0x0050ecf8`, `0x0050ed00`); two sheet-12 rect-6 squares (0xc0006, 0.09 × 0.065 × `0x0050ed44`), the second black; hint string 0x118 in grey. | confirmed (code) |
| `0x001d7cb0`, `0x001d7ce0` | `LightingItem_SaveValue`, `LightingItem_RevertValue` | Saves `Gamma_Get()` at +0x644; restores with `Gamma_Set`. | confirmed (code) |
| `0x001d7d10`, `0x001d7de8`, `0x001d7e28`, `0x001d7e98` | `LightingItem_Update`, `_Render`, `_RenderColoured`, `_IsLoaded` | Level from W_GameState +0x57a4; bar, squares (first tinted when coloured), hint. | confirmed (code) |
| `0x001d7ef0` | `OptionMenu_OnVibrationChoice` | Dirty `0x00630840` = 1; on gives a preview rumble of strength 120 (`0x0050ed50`) for 500 ms (`0x0050ed54`). | confirmed (code) |
| `0x001d7f90`, `0x001d7ff0`, `0x001d8050` | `OptionMenu_OnInvertChoice`, `_OnAutoAdjustChoice`, `_OnMergeChoice` | Set invert / auto-adjust per pad and merge to choice == 0; mark dirty. | confirmed (code) |
| `0x001d80a8`, `0x001d8208` | `OptionMenu_OnSubtitlesChoice`, `OptionMenu_OnProLogicChoice` | W_GameState +0x438 / sound manager +0x3faa8 = choice == 0; both were undefined code, created. | confirmed (code) |
| `0x001d80e0` | `OptionMenu_OnRestoreDefaults` | Yes: brightness 40, volumes 0.9, invert off, auto-adjust on, merge on, vibration on, +0x454 = 0, subtitles and Pro Logic off, reopen flag `0x0050ed14`. | confirmed (code) |
| `0x001d8258` | `OptionMenu_OnVideoChoice` | 2/3 select video mode 0/1 (`0x00194e28`), close and reopen pause and options menus, return to Options, re-lay the lock-pick dials. | confirmed (code) |
| `0x001d83e0`, `0x001d8410`, `0x001d8440` | `OptionMenu_SetSoundFxVolume`, `_SetMusicVolume`, `_AddBrightness` | Volume setters (dirty); brightness + delta clamped 0-100. | confirmed (code) |
| `0x001d84a0`, `0x001d8558`, `0x001d8658` | `OptionMenu_Construct`, `_Destroy`, `_Shutdown` | GameMenu base, 7 titles, 7 pages, music handle +0x45d4 = -1. | confirmed (code) |
| `0x001d8620`, `0x001d86a0` | `OptionMenu_ApplyVolumes`, `OptionMenu_NoOp` | Slot +0x90 applies music (+0x57ac) and SoundFX (+0x57a8); slot +0x98 empty (created). | confirmed (code) |
| `0x001d86a8` | `OptionMenu_Open` | Slot +0xa8: titles 0x117, 0x119, 0x122, 0x123, 0x124, 0x11f, 0x128 (font slot 6, scale 1.3) and every page's items; Merge omitted on level 102; Restore Default preselects No. | confirmed (code) |
| `0x001da348` | `OptionMenu_Close` | Slot +0xb0: shuts pages and titles, stops the preview music. | confirmed (code) |
| `0x001da3f8` | `OptionMenu_OnCommand` | Page open: to the page; else up/down titles 0-6 (cue 4/0xe), accept opens (BeginEdit, cue 8), back cue 0xf and +0x45d0 = 1. | confirmed (code) |
| `0x001da658` | `OptionMenu_Update` | Waits for load; ends the vibration preview; reopens after Restore Default; loops `vags/music/wonderwheel_mono` at music volume while Audio is open. | confirmed (code) |
| `0x001da880` | `OptionMenu_Render` | Titles (selected `0x005fd310`, others `0x005fd320`) or the open page. | confirmed (code) |
| `0x001da9b8`, `0x001da9d0`, `0x001daa38`, `0x001daa50` | `OptionMenu_CloseConfirmBox`, `_RevertPage`, `_SetPageChanged`, `_IsLoaded` | Current page's box closed, items reverted (+0x218 = 1), changed flag +0x214; all pages loaded. | confirmed (code) |
| `0x001daaa8`, `0x001dab10` | `OptionMenu_StaticInit`, `OptionMenu_StaticInitStub` | Construct the menu at `0x0062a1a0` (constructor list `0x00534124`); end of the TU. | confirmed (code) |

### After `GUI/OptionMenu.cpp` (no path string): the pause menu {#fn-pausemenu}

`0x001dab30`-`0x001dfe20`: `PauseMenu` (vtable `0x0053bdc0`, input `0x0053bd98`, object `0x0062e790`, pointer
`0x0050eddc`) and the objective checklists it holds; the objective calls are described in
[hud.md](hud.md#objectives-hudsetobjective).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001dab30` | `Objective_GetIconPrefix` | Maps an objective's leading tag (`<ROBJ_N>`, `<YOBJ_N>`, `<BOBJ_N>`, `<ARREST_N>`, `<DEAD_N>`, `<FLASH_N>`, `<SPRAY_N>`, `<PEDRAT_N>`, `<CALLGANG_N>`, `<NICON>`) to its icon prefix; dots use colour tags 3, 4, 6. | confirmed (code) |
| `0x001dad88`, `0x001db248` | `HUD_SetObjective`, `HUD_RemoveAllGoalText` | Add/remove/tick an objective in the checklists; clear the current-objectives list. | confirmed (code) |
| `0x001db278` | `PauseMenu_OnConfirm` | Yes/No box callback: 2 Yes, 3 No, 4 back. | confirmed (code) |
| `0x001db3c0` | `PauseMenu_Construct` | Yes/No box +0x1bd0, three checklists +0x20d0 step 0x360, grid +0x2b00, usage +0x2dd0, Controls +0x30a0, background +0x3a90, Stats +0x3bb0. | confirmed (code) |
| `0x001db4b0` | `PauseMenu_ResetState` | Zeroes the stats block (+0xa40, 0x1130 bytes) and choice flags; screen selection +0x3a74 = 3. | confirmed (code) |
| `0x001db550` | `PauseMenu_Init` | Slot +0x90: headers (`0x006340c0`, `0x00634c70`, `0x00634fe0`), checklists, Quit box (0x102, confirm mode). | confirmed (code) |
| `0x001db7e8`, `0x001db870` | `PauseMenu_Shutdown`, `PauseMenu_ReleaseScreens` | Slot +0x68 checklists, background, usage, box; slot +0x98 headers, usage, background. | confirmed (code) |
| `0x001db8e8` | `ArrayU32_Contains` | Linear search of a word array. | confirmed (code) |
| `0x001db918` | `PauseMenu_Quit` | Slot +0xb8: quit chosen +0x1b70 and closing +0x1b80. | confirmed (code) |
| `0x001db928` | `PauseMenu_GetLevelColour` | Top header colour by level number: (37,100,60), (38,61,94), (76,53,44) or (186,139,53) groups, else black; gold also calls `0x001e8658`. | confirmed (code) |
| `0x001dbb78` | `PauseMenu_ApplyVideoMode` | Layout globals `0x0050edfc`-`0x0050ee6c` and `0x00635350`-`0x00635364` per device flags. | confirmed (code) |
| `0x001dbee0`, `0x001dcb20` | `PauseMenu_Open`, `PauseMenu_Close` | Slots +0xa8 / +0xb0 (see the opening and leaving sections); close issues pending war commands and clears the screen tint byte +0x214. | confirmed (code) |
| `0x001dcac0` | `PauseMenu_ReturnToOptions` | Screen and Options open (+0x1b88, +0x3a78); grid moved right twice (once in Rumble). | confirmed (code) |
| `0x001dcc00`, `0x001dd1d0` | `PauseMenu_ClearChecklist`, `PauseMenu_CountOpenObjectives` | Clear one list; count its open lines. | confirmed (code) |
| `0x001dcc28`, `0x001dce80` | `PauseMenu_AnnounceCurrentObjective`, `PauseMenu_AnnounceBonusObjective` | Story levels: first open objective as a scroll-in with its tag's icon (default 0x1c / 0x16), cue 0x15. | confirmed (code) |
| `0x001dd0b8`, `0x001dd178`, `0x001dd1a0` | `PauseMenu_AddObjective`, `_RemoveObjective`, `_MarkObjectiveDone` | List 2 cleared before adding; lists 0/1 drop their "None" (0x109) line. | confirmed (code) |
| `0x001dd1f8`, `0x001dd258` | `Widget_ReapplyRect`, `Widget_GrowRectHeight` | Re-set a widget's rectangle; grow its height (min 0). | confirmed (code) |
| `0x001dd2e0`, `0x001dd3d8` | `PauseMenu_SlideChecklistY`, `PauseMenu_SlideHeaderY` | Move toward a target y by 0.02 (`0x0050ee20`). | confirmed (code) |
| `0x001dd4d0` | `PauseMenu_AnimateObjectives` | Every 2 ms (`0x0050ee7c`): headers 0.02, list reveal 0.01 (`0x0050ee24`), gaps 0.05 (`0x0050ee74`, `0x0050ee78`). | confirmed (code) |
| `0x001ddbc0`, `0x001ddcf8`, `0x001df700` | `PauseMenu_FadeOut`, `PauseMenu_Update`, `PauseMenu_Render` | Fade, update (slot +0x30) and render (slot +0x38); see the sections above. | confirmed (code) |
| `0x001de488` | `PauseMenu_CloseScreen` | Closes the open screen (Options once its back flag +0x45d0 is set) and restores the headers. | confirmed (code) |
| `0x001de6c0`, `0x001dee00`, `0x001ded68` | `PauseMenu_SelectItem`, `PauseMenu_OnCommand`, `PauseMenu_OnYesNoCommand` | Accept on a grid item; grid owner handler; Yes/No routing (cue 0xe/8). | confirmed (code) |
| `0x001ded00`, `0x001ded48` | `PauseMenu_PlayCue`, `PauseMenu_GetSelectedItem` | Cue unless Options is open (+0x2bc4 when a screen is); grid selection. | confirmed (code) |
| `0x001df3a8`, `0x001df448`, `0x001df4b0`, `0x001df4c8` | `PauseMenu_QuitPlayerTwo`, `_QuitToRumbleQuick`, `_QuitToMainMenu`, `_QuitToHangout` | Slots +0xd0, +0xd8 (+0x1b98), +0xe8 (+0x1b9c), +0xe0 (+0x1b94). | confirmed (code) |
| `0x001df4e0` | `PauseMenu_IsReady` | 10 s after opening, or headers and background ready (player not raging). | confirmed (code) |
| `0x001df5e8`, `0x001df6d0` | `PauseMenu_CancelChoice`, `PauseMenu_ApplyFadeAlpha` | Slot +0xc8 clears quit/restart choice; colour with menu alpha +0x3b9c while closing. | confirmed (code) |
| `0x001dfc08`, `0x001dfc40` | `PauseMenu_ResetStatSlots`, `PauseMenu_ClearObjectives` | 100 records (+0xa44 step 0x2c) = -1; clears lists 2, 0, 1 on level load (`0x0015fe90`). | confirmed (code) |
| `0x001dfc80`, `0x001dfe00` | `PauseMenu_StaticInit`, `PauseMenu_StaticInitStub` | Headers 0x108/0x107/0x106, colour cycle `0x00635368`-`0x00635388`; list `0x00534128`. | confirmed (code) |

### After the pause menu (no path string): Rumble result screen {#fn-rumbleresult}

`0x001dfe20`-`0x001e09e8`: `RumbleResult` (vtable `0x0053bee0`, input `0x0053beb8`, object `0x00635390`, pointer
`0x0050ee94`); behaviour in [rumble.md](rumble.md#result-screen).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001dfe20`, `0x001dfe88` | `RumbleResult_Launch`, `RumbleResult_SetReason` | Pushes mode 0x14, sets winner (slot +0xa0) and the reason text (+0xa70). | confirmed (code) |
| `0x001dfef0`, `0x001dff40`, `0x001dffa8`, `0x001dffd8` | `RumbleResult_Construct`, `_Destroy`, `_Init`, `_Shutdown` | Second grid +0x7b0, markup +0x880; init sets +0x04 = 1. | confirmed (code) |
| `0x001e0008`, `0x001e0430` | `RumbleResult_Open`, `RumbleResult_Close` | Slots +0xa8 / +0xb0; open applies a screen effect and hides lock-pick dials. | confirmed (code) |
| `0x001e0460`, `0x001e0560`, `0x001e0778` | `RumbleResult_OnCommand`, `_Update`, `_Render` | Input, update (+0x30), render (+0x38; choices after `0x0050eec0`, fade `0x0050eeb8`). | confirmed (code) |
| `0x001e0998`, `0x001e09c8` | `RumbleResult_StaticInit`, `RumbleResult_StaticInitStub` | Constructs `0x00635390`; list `0x0053412c`. | confirmed (code) |

### `GUI/ControlMenuHUD.cpp` {#fn-controlmenuhud}

`0x001e3458`-`0x001e5478`: the pause menu's Controls screen, two entries opening the controller picture page or the
tutorial tips page. Both pages derive from a small `ControlPage` base and are static objects built at start-up.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001e3458`, `0x001e34c0` | `ControlPage_Construct`, `_Destroy` | vtable `0x0053c658`, interface `0x0053c630`; title TextWidget `+0x60`, alpha `+0x130` | confirmed (code) |
| `0x001e3520` | `ControlPage_Init` | slot `+0x90`: title at (`0x0050eee4`, `0x0050eee8`), size 1.3, `0x005fd310`, font slot 4 | confirmed (code) |
| `0x001e35d8`, `0x001e35f8`, `0x001e3670` | `ControlPage_Shutdown`, `_Update`, `_Render` | slots `+0x68`/`+0x30`/`+0x38`: the title, with alpha | confirmed (code) |
| `0x001e36a8`, `0x001e3758` | `TutorialPage_Construct`, `_Destroy` | vtable `0x0053c588`, interface `0x0053c560`, object `0x00637be0`; 23 titles `+0x150`, 23 texts `+0x3960` (0x270 each), menu `+0x140` | confirmed (code) |
| `0x001e3828` | `TutorialPage_Init` | ScrollingMenu at (`0x0050eef0`, `0x0050eef4`); tip i = title string `0x145`+i (focus colour (134,26,26)) with text `0x15c`+i as description; visible `0x0050ef00` = 8 (10 from the pause menu), clip height `0x0050ef0c` = 0.5; title `0x131` | confirmed (code) |
| `0x001e3d98` | `TutorialPage_Shutdown` | frees the menu, shuts the texts down | confirmed (code) |
| `0x001e3e60` | `TutorialPage_OnCommand` | forwards to the menu; back: cue `0xf`, returns true | confirmed (code) |
| `0x001e3ed0`, `0x001e3f38` | `TutorialPage_Update`, `_Render` | title and menu | confirmed (code) |
| `0x001e3f98`, `0x001e4050` | `ControllerPage_Construct`, `_Destroy` | vtable `0x0053c4b8`, interface `0x0053c490`, object `0x00635e80`; 7 + 7 labels `+0x140`/`+0xed0`, picture `+0x1c60` | confirmed (code) |
| `0x001e4130` | `ControllerPage_Init` | label table `0x0050ef20` (`0x0050f000` in mode `0x02` without `0x04`) copied to `0x0063ed50`; label 1 y moved by game state `+0x120` (4: -0.02, 2: 0.16, mode 2: 0.22); picture sprite `0x18f0000` at (0.5, 0.58), size 0.51 x 0.45, depth 11000; labels `0x132`-`0x138` align 5, `0x139`-`0x13f` align 4; title `0x130` | confirmed (code); +0x120 as controller layout inferred |
| `0x001e4540`, `0x001e45f8` | `ControllerPage_Shutdown`, `_IsReady` | releases; ready when the picture batch `+0x1d24` is resident | confirmed (code) |
| `0x001e4618` | `ControllerPage_OnCommand` | table `0x005567b0`: directions cue `0xe`, back cue `0xf` and true | confirmed (code) |
| `0x001e46c0`, `0x001e4820` | `ControllerPage_Update`, `_Render` | picture size and place, labels from the table; render with alpha | confirmed (code) |
| `0x001e4908`, `0x001e4988` | `ControlMenuHUD_Construct`, `_Destroy` | GameMenu subclass, vtable `0x0053c3d0`, interface `0x0053c3a8`; entries `+0x7a0`/`+0x870` | confirmed (code) |
| `0x001e4a30` | `ControlMenuHUD_Open` | slot `+0xa8`: pausing player's input (pause menu `+0x1bc0`); entries Controller `0x130` at (0.26, 0.29), Tutorial `0x131` 0.06 below, size 1.3; pages `+0x940`/`+0x944` Init; `+0x948`-`+0x954` reset | confirmed (code) |
| `0x001e4e28`, `0x001e4ea8` | `ControlMenuHUD_Close`, `_ArePagesReady` | slot `+0xb0`; both pages' slot `+0x98` | confirmed (code) |
| `0x001e4f18` | `ControlMenuHUD_OnCommand` | open page: forwarded, closed on true; else table `0x005567d0`: up/down cue 4 (`0xe` at an end), left/right `0xe`, accept opens (cue 8 for Controller), back cue `0xf`, leave `+0x94c` | confirmed (code) |
| `0x001e5130` | `ControlMenuHUD_Update` | waits for pages; plain pad pass; entries or the open page | confirmed (code) |
| `0x001e52f8`, `0x001e5300` | `ControlMenuHUD_SetAlpha`, `_Render` | alpha `+0x954`; selected `0x005fd310`, other `0x005fd320` | confirmed (code) |
| `0x001e5438`, `0x001e5478` | `ControlMenuHUD_StaticInit`, `_StaticInitStub` | builds both pages; stub in ctor list `0x00534130` ends the file | confirmed (code) |

### After `GUI/ControlMenuHUD.cpp` (no path string): the Stats screen {#fn-after-controlmenuhud}

`0x001e5498`-`0x001e6e78`: the pause menu's Stats screen (pause menu `+0x3bb0`), a GameMenu subclass (vtable
`0x0053c728`, interface `0x0053c700`) with its own static-init stub, so a file of its own (inferred).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001e5498`, `0x001e5540` | `PauseStats_Construct`, `_Destroy` | portrait `+0x7b0`, shadows `+0x8b0`/`+0x9b0`, banner `+0xab0`, 4 HudGenericBars `+0xbb0`, description `+0x1b70` | confirmed (code) |
| `0x001e5618` | `PauseStats_Setup` | from `PauseMenu_Open`: per-mode globals `0x0050f104`-`0x0050f2b8`; human of game state `+0x228`; class 0-8 picks portrait, banner and string `0xea`-`0xf2`, else unavailable; bars Strength `0xf3`, Stamina `0xf4`, Health `0xf5`, Rage `0xf6` normalised by difficulty table `0x0050f230` (0x28 per difficulty, game state `+0x154`); description reveal 0.55 s | confirmed (code) |
| `0x001e6530`, `0x001e6600` | `PauseStats_Shutdown`, `_IsReady` | releases; ready after 10 s or when both portrait batches are resident | confirmed (code) |
| `0x001e6698` | `PauseStats_OnCommand` | table `0x005568e0`: up/down `MessageHUD_PageUp`/`PageDown` (cue 4, `0xe` when stuck), left/right `0xe`, back `0xf` and `+0x7a0` = 1 | confirmed (code) |
| `0x001e6778` | `PauseStats_Update` | portrait at base + (-0.08, -0.2), banner + (-0.17, -0.099), bars at + (0.14, -0.11 + 0.05 i) 0.22 x 0.025 fill `0x0063ee38`, description + (-0.24, 0.1) | confirmed (code) |
| `0x001e6cf0`, `0x001e6cf8` | `PauseStats_SetAlpha`, `_Render` | alpha `+0x1d70` | confirmed (code) |
| `0x001e6e20`, `0x001e6e78` | `PauseStats_StaticInit`, `_StaticInitStub` | colours `0x0063ee30`/`0x0063ee38` = (150,30,30,255); stub in ctor list `0x00534134` | confirmed (code) |

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0041dd78` | `GameState_RequestOptionMenu` | `+0x11c` = 1 | confirmed (code) |
| `0x004229c0` | `Stats_ClearArmiesScores` | zeroes the Armies score (`+0x04`) of both players in the list (`W_GameState + 0x228`); from the game-over and pause menus' quit or retry | confirmed (code) |

## Coney's implementation

Written from this page: `repo:src/gui/pause_menu/` (`PauseMenu`, `YesNoBox`, `MissionFailedMenu`, the item table
`pauseGrid` and `offersHangout`) on the menu widgets ([GUI](gui.md#widget-classes)), and the modes
`repo:src/gamemodes/pause_mode.h` (`PauseMode`, 0xa, with `applyPauseOutcome`, `PauseMenu_Toggle`'s second half) and
`repo:src/gamemodes/mission_failed_mode.h` (`MissionFailedMode`, 0xc, pushed by the binding `HUDLaunchMissionFailed`).
Gameplay (mode 1) ends each frame of play with `PauseMode::playFrame` (START on a connected pad, then the cool-down);
while mode 0xa or 0xc is on top gameplay does not update at all, and the paused level is drawn at its last step with the
menu's layer over it, after the HUD's (`GameplayMode::renderWithOverlay`, which the play mode implements). `coney
--disc` gives the modes the sheet-table records (the background is record 12, the gang-logo picture), pauses and resumes
every sound through the sound player, hands the HUD's three checklist slots to the Objectives screen and turns both
radars off on open (and, Coney's stand-in, back as they were on close). Timings, positions, string ids, cues, the item
grids, the Yes/No box and the leaving actions follow this page; tests in `repo:tests/gui/pause_menu_test.cpp` and
`repo:tests/gamemodes/pause_mode_test.cpp` drive them with scripted START, d-pad, cross and triangle.

Coney stand-ins, each an open question below:

- The headers (`CircledTextHeader`), the objective lists and the Restart and Quit screens are centred texts at Coney's
  positions and colours; the Options and Controls screens are lists of their entries' names that do nothing on accept.
- The Stats screen is never available (Coney has no character stats), so Stats opens nothing.
- The menu does not wait for its last cue to end before it pops.
- The tint is a black quad rising to opaque over 1.4 s, under the menu; the mission-failed fade is the mode's own.
- Leaving pops mode 1 directly instead of setting `W_GameState + 0x14c` = 3; the game state's `+0x11c`, `+0x118` and
  `+0x56e0`, the save on close, the pad vibration and the co-op quit (Player 2 / All) are not modelled.
- The Armies of the Night levels use the story menu; the Rumble quit picks `PauseGoToRMIQuick` (or
  `PauseGoToRMIHangout` when the caller says the Rumble came from the hangout) and offers To Main Menu.
- The mission-failed items: Last checkpoint, Restart level and To Hangout end the menu at once as the pause menu's
  restart and hangout outcomes, Yes to Quit as its main-menu outcome; back does nothing.
- "Saving the sound state" is remembering the sound bank, which exit loads again.

## Open questions

- The Stats screen's class (`0x001e5498`, menu `+0x3bb0`): its layout and when `+0x3bb8` is unset.
- The header widget (`CircledTextHeader`, `0x001e7f50`): its layout and the two coloured circles left of the title.
- The Controls screen (`ControlMenuHUD.cpp`) and the Options menu (`OptionMenu.cpp`): their layouts and items.
- The mission-failed items' actions (`0x001d2018` and the vtable `0x0053b598`). The Rumble result screen (mode
  0x14) is on [Rumble](rumble.md#result-screen).
- What the menu's write to the screen-effects managers' `+0x214` does on close.
- The Armies of the Night pause menu's items (`0x001cfec0`-`0x001d0a38`).
- The mission title's choice between `0xe7` and `0xe8` (which one `<ROBJ_N>` selects), and the fonts, scales and
  colours of the headers, the item screens, the Yes/No box's texts and the objective lists' gold and purple.
- Which Rumble quit `0x001fe1a8` picks, and when the Rumble Quit screen offers To Hangout rather than To Main Menu.
- What the tint `0x0018c988` reaches (Coney: opaque black), and whether the HUD is drawn under the paused menu.
