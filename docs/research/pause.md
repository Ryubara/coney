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
| `0x001d1848` | `GameMenu_Construct` | base of the pause and mission-failed menus (vtable `0x0053b4b0`) | confirmed (code) |
| `0x001db3c0` | `PauseMenu_Construct` | the story pause menu (vtable `0x0053bdc0`, input interface `0x0053bd98` at `+0x5c`) | confirmed (code) |
| `0x0062e790` | the story pause menu | one static object (pointer at `0x0050eddc`) | confirmed (code) |
| `0x006239d0` | the Armies of the Night pause menu | a subclass (vtable `0x0053b298`, `0x001cff48`; pointer at `0x0050ec34`) | confirmed (code) |
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
mission-failed menu. Confirmed (code).

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
the per-item actions (`0x001d2018`, vtable `0x0053b598`) are not traced here.

## Coney's implementation

Written from this page: `repo:src/gui/pause_menu/` (`PauseMenu`, `YesNoBox`, `MissionFailedMenu`, the item table
`pauseGrid` and `offersHangout`) on the menu widgets ([GUI](gui.md#widget-classes)), and the modes
`repo:src/gamemodes/pause_mode.h` (`PauseMode`, 0xa, with `applyPauseOutcome`, `PauseMenu_Toggle`'s second half) and
`repo:src/gamemodes/mission_failed_mode.h` (`MissionFailedMode`, 0xc, pushed by the binding `HUDLaunchMissionFailed`).
Gameplay (mode 1) ends each frame of play with `PauseMode::playFrame` (START on a connected pad, then the cool-down);
while mode 0xa or 0xc is on top gameplay does not update at all, and the paused level is drawn at its last step with
the menu's layer over it, after the HUD's (`GameplayMode::renderWithOverlay`, which the play mode implements). `coney
--disc` gives the modes the sheet-table records (the background is record 12, the gang-logo picture), pauses and
resumes every sound through the sound player, hands the HUD's three checklist slots to the Objectives screen and
turns both radars off on open (and, Coney's stand-in, back as they were on close). Timings, positions, string ids, cues, the item grids,
the Yes/No box and the leaving actions follow this page; tests in `repo:tests/gui/pause_menu_test.cpp` and
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
