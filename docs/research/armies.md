---
kind: feature
owns: {functions: ["0x0041d110"]}
---
# Armies of the Night

Verified against: `SLUS_212.15` (NTSC-U) and the retail disc's compiled Lua chunks; runtime claims cite run records.

## Summary

Armies of the Night is the original's bonus game: five stages of a side-on brawler in the game's own engine, for one
player or two. This page says how the player reaches it, what loads, how the first stage starts and ends when nobody
plays, and which engine pieces exist only for it. The pieces have their own pages, linked from [the map](#map); this
page gathers them and describes only what is the mode's own: the stage's start, the join messages, the credits line,
the GO sign and the game-over screen's rules. It is not a story mission: the story's levels are on
[Missions](../missions/index.md).

## Rules {#what-it-is}

### The stages and the way in {#stages}

Inferred from the level table and the scripts unless a line says otherwise.

- **Five levels.** `level60` to `level64`, records 24-28 of the level table, titled `Stage 1` to `Stage 5`, with 3,
  3, 3, 3 and 4 sections ([Levels](../references/levels.md#level-24)). Each one's last checkpoint script loads the
  next (`level60_mothers` → `level61`, ...), and `level64`'s last returns to the hub
  ([Story order](scripting.md#run-next-mission)).
- **The way in.** The clubhouse's trophies in the hub (`level95`) start the flashbacks. The sixth entry of
  `FBMission` is `{level = 60, req = {82, 92, 83, 20, 11}}`: Armies of the Night is offered once the five flashbacks
  are complete, and `flashback.PlayMission` (action `MA_LOADFBLEVEL`) loads `level60` (inferred from the disassembly
  of `level95.lua`). Nothing else leads there:
    - the main menu offers STORY, EXTRAS (only the trailer) and QUICK RUMBLE ([The screens](frontend.md#pm-screens));
    - the mission menu leaves levels 60-65 out (`MissionSelect_IsMissionListed`, `0x001bc100`, confirmed (code));
    - a cheat loads the next stage from within one (`CheatArmiesLoadLevel`, [Debug](debug.md)).
- **No mode of its own.** A stage plays in the ordinary gameplay mode (mode 1). The engine switches on the
  current level's number: `GameState_IsArmiesLevel` (`0x0041d110`) is true when the level record's number (`+0x04`)
  is **60-69** (confirmed (code)). What it switches is listed in [the map](#map).
- **The record `armies`.** The level table also holds a record named `armies` (index 101, world `level60`, number
  419, no sections, no `.lev` on the disc). `LoadLevel` reads `<name>.lev`, here `armies.lev`, so the record cannot
  load as it stands. Its number is not 60-69 either, so it is not an Armies level. Inferred from the level table: a
  leftover test record, like the others with no `.lev` ([Level loading](level-loading.md)).

### Time, credits and continues {#rules-credits}

Read in the scripts and the screens' code; each line gives its level.

- Each stage runs a **99-second timer**; when it runs out, the game-over screen asks "CONTINUE?" and counts 9 down to 0
  ([The game-over screen](#game-over); the timer's script is in [the start](#level60-start)). Inferred from
  `level60_destroyers.lua`'s `F1.Time` and `F1.TimerOut`. The watch runs about 1% slow, so the 99 s take about 101 s
  ([From the walk to the game over](#timer)).
- The player starts with **7 credits** (`HUDANSetCredit(7)` in `RunLevel`). A Start during the count spends one and
  plays on; with none left, or when the count ends, the screen shows GAME OVER with Replay and Quit (confirmed (code)
  at `ANGameOver_Update` `0x001cf468`).
- Player 2 joins at any time with Start: his panel says "PRESS THE START BUTTON TO JOIN" until he does
  ([The join messages](#the-join-messages); joining itself is
  [Front end: game-state functions](frontend.md#warriors-functions)). Confirmed (code) at `ANHud_Draw` `0x00211938`.

## Map {#map}

| Behaviour | Function | Section |
| --- | --- | --- |
| Is this an Armies level (60-69) | `0x0041d110` `GameState_IsArmiesLevel` | [Rules](#stages) |
| Character models: every human drawn with its type's `_a` model, by `Human_Init`'s own test of levels 60-64 (not the 60-69 above) | `0x00218008` `Human_Init` | [Characters: from a type to a model](characters.md#type-to-model) |
| Camera: the rail's mode 3, trailing the hindmost fighter at a fixed downward pitch, forward only | `0x0013dcc8` `CamRail_PlaceMode3` | [Camera: mode 3](camera.md#rail-mode3) |
| HUD: the two `ANHud` panels (health, power, the boss picture) | `0x00210e48` `ANHud_Update`, `0x00211938` `ANHud_Draw` | [HUD: the Armies panel](hud-functions-panels.md#fn-anhud) |
| The join message at the set-up and its clear on the first attach | `0x004ec650` `ANHud_SetPlayer`, `0x002107e8` `ANHud_Init` | [The join messages](#the-join-messages) |
| The credits line | `0x001b46e0` `HUD_ANSetCredits`, `0x001af010` `HUD_Update` | [The credits line](#credits) |
| The GO sign | `0x0019e030` `GoSign_Setup`, `0x0019e758` `GoSign_Update` | [The GO sign](#go-sign) |
| Failure: no mission-failed shot; the game-over screen (mode 0xd) | `0x001557f8` `ANGameOver_Toggle`, `0x001cf468` `ANGameOver_Update` | [The game-over screen](#game-over), [Pause: the game-over screen](pause.md#an-game-over) |
| Pause: its own pause menu | the Armies pause menu | [Pause: the Armies pause menu](pause.md#an-pause-menu) |
| Sound: the matrix `armies` (`global.lua`, levels 60-64), the bank `armies`, the load screen's bank `armload` | the sound banks | [Sound: banks](sound.md#banks) |
| Two players: player 2 joins through `SetArmiesMultiplayerCallback`, kept at game state `+0x56f4` | the join callback | [Front end: game-state functions](frontend.md#warriors-functions) |
| The first stage's garage door, a sliding door | `0x003f5608` `SlidingDoor_StateCommand` | [The garage door](#garage-door) |

`HideHud` does nothing while game state `+0x14c` is 1 ([HUD: the Armies panel](hud-functions-panels.md#fn-anhud)).

## Behaviour

### `level60`'s start {#level60-start}

Inferred from the disassembly of `level60.lua` and `level60_destroyers.lua`; the observed times are in
[the first frames](#first-frames).

1. **`Main`**: the checkpoint table (1 `level60_destroyers` / `P1.SetupDest`, 2 `level60_jsb` / `P2.SetupJSB`, 3
   `level60_mothers` / `P3.SetupMothers`); fog colour black; **`ScreenQueueEffect(1, 0)`**, an instant fade to black
   ([Screen effects](../references/screen-effects.md#fx-queue-1)); the radar off and the HUD restored; the bank
   `armies`; `preLoadFile` of the chapter with `RunLevel`.
2. **`RunLevel`**: the Warriors; walk-only combat (`CfgPlayerCombatWalkOnly(1)`); the join callback; Lua save
   floats 1-5 (5 is the bonus); commands 32 and 33 removed, the player's commands 1 and 5 off, jump blocked, rage
   prevented, speech off, revivable, health 500, revives; mugging and power-up pick-up off; **`HUDANSetCredit(7)`**;
   the inventory callback `F1.Health`; the stage's cameras. Then a locked camera `lock01` is made active on the
   rail's line (y = 39.5): field of view 22°, heading 180°, pitch −7°. Last come the stage's gangs for the checkpoint
   and the bonus timer `F1.Bonus01` (after 5 s, then every second).
3. **`P1.SetupDest`** sets the start callback `P1.SetupDest02`, which teleports `Ajax2` to its flag and, 100 ms
   later, plays the intro scene `l60_intro` through the stage's own scene runner. The scene brings the picture back.
   Its end (`PreCashTheWorld`, then `P1.Start02`) starts the music `music/armies_pt1` and opens the garage door.
   4.5 s later `P1.Start03` makes the rail camera active (`Rail1`):
   `CamSetupRail("rail", player, 22, {0, −0.25, 1.25}, 0.5, 90)`, six points along y = 39.5 from x = 100 to
   x = −850, then `CamModifyRail(5, −7, 0)` (mode 3, pitch −7°) and `CamModifyRail(7, 0.5)`, and sends the player to
   `Flag_02` (`GoalMoveToFlag(player, Flag_02, 2, 0, 0, 0.5, 0, true)`).
4. **`P1.Start04`**, run by the flag's message 8 when the player arrives: `F1.Restore01` and `F1.Restore02` bring the
   HUD back and start the timer (`F1.Time`: `W_SetStopWatch(99999, 0, "F1.TimerOut")`, shown in its seconds form);
   `F1.TimerOut` calls `HUDLaunchANGameOver(true)`, the game-over screen.

So until the intro scene runs, the screen is black by the script's own choice.

### The original's first frames {#first-frames}

Confirmed (runtime), run r-20261026-level60-idle, at every line.

`level60` checkpoint 1, loaded by the game's checkpoint jump from a `level99` state, with the pad idle throughout. The
ticks are the game's 60 Hz count (`0x005104f4`), which starts again at the level and stops while the game-over screen
is open; a capture is placed between the ticks read just before and just after it.

| Ticks | What shows |
| --- | --- |
| 1-11 | the load screen: the Armies of the Night art with **"STAGE ONE"** and a red progress bar |
| 571-887 | the intro scene, no HUD: a fixed side view of the garage under a red graffiti "W", pedestrians walking past |
| 1007-1311 | the shutter up, a car in the garage and Swan standing in front of it |
| 1231-1411 | the walk out ([below](#walk-out)) |
| 1431-1737 | play: Swan on the pavement, the same side-on view, the HUD on with the timer at 96 ([below](#hud-in-play)) |
| 7545 | the game-over screen opens: **"CONTINUE?"** over the scene, a digit counting down from 9 and "Press the START button to continue" below; ten seconds later **"GAME OVER"** with Replay and Quit fades in as the view fades to black |

So the idle stage runs out its timer about 102 s after the walk ends and ends in the game-over screen without any
fight.

### The walk out {#walk-out}

Confirmed (runtime), run r-20261026-level60-idle, at every line.

Swan stands at (−93.5, 18.8), where the level script puts him, through the intro scene. Between ticks 1231 and 1245
(`P1.Start03`'s `GoalMoveToFlag`) he starts walking, gait 2 at 1.63 m/s, in a straight line out of the garage, and he
stops by tick 1411 at (−94.78, 23.54): 0.48 m from `Flag_02` (−94.9, 24), inside the goal's 0.5 m radius. The flag's
message 8 then runs `P1.Start04` ([the start](#level60-start), step 4).

### From the walk to the game over {#timer}

Confirmed (runtime), run r-20261026-level60-clock, for the ticks; confirmed (code) at the cited addresses for the
stopwatch.

The same idle start as [the first frames](#first-frames), with a breakpoint on the binding each script step calls; the
ticks are the game's 60 Hz count:

| Tick | Step |
| --- | --- |
| 1205 | `P1.Start03` calls `GoalMoveToFlag` |
| 1224-1231 | Swan starts walking |
| 1405-1411 | he stops at (−94.78, 23.54) |
| 1428 | `P1.Start04`, run by the flag's message 8, calls `CloseDoor` |
| 1457 | `F1.Restore01` calls `HUDANSetGOSignMode` |
| 1473 | `F1.Restore02` calls `F1.Time`, which calls `W_SetStopWatch(99999, 0, "F1.TimerOut")` |
| 7541 | `F1.TimerOut` calls `HUDLaunchANGameOver` |

- The walk takes about 180 ticks, and the flag's message 8 comes 17 to 23 ticks after Swan stops. The script's 500 and
  250 ms delays take 29 and 16 ticks.
- **The timer runs slow.** The 99,999 ms took 6068 ticks (101.1 s), not 6000. `StopWatch_Update` (`0x004233f8`) adds
  the whole milliseconds since its last reading (`GameTimer_MsSince`, `0x004dca60`, truncates) and then keeps the
  timer's whole reading, so a 33.33 ms frame counts 33 ([Scripting: the stopwatch](scripting.md#stopwatch)). The game
  clock itself kept time: at tick 7541 the game timer read 125,726 ms, the same as its real time.
- **Six ticks are not traced.** Inferred from the truncation: any update of 33.0 to 33.99 ms counts 33, so the
  99,999 ms take 3031 updates of two ticks, 6062 ticks, whether a frame is 33.33 ms or the run's average of about
  33.35 ms. The original's 6068 ticks are three more updates: one that counted less than 33 ms, an update the watch
  skipped, or a start or end a frame away from the script steps the breakpoints caught. Which one is open
  ([q-timer-gap](#q-timer-gap)).

### The HUD in play {#hud-in-play}

Confirmed (runtime), run r-20261026-level60-idle, for what shows; the code of each part is in the sections below.

Top left Swan's portrait, his name and a seven-digit score (0000000) over a green health bar; top centre the
two-digit **timer**, counting down a second at a time; top right **"PRESS THE START BUTTON TO JOIN"**, blinking
([The join messages](#the-join-messages)); at the right edge **"GO"** with an arrow pointing right
([The GO sign](#go-sign)); bottom right **"CREDITS: 07"** ([The credits line](#credits)).

No action prompt shows in these stages: `HUD_Render` (`0x001b1688`) draws both players' prompts only while the HUD
is shown, no scroll-in message hides them and the level is not an Armies level (`GameState_IsArmiesLevel`,
`0x0041d110`). Confirmed (code).

### The join messages {#the-join-messages}

Confirmed (code) at the cited addresses, and confirmed (runtime), run r-20261026-level60-join, for the calls and values
a line says were seen.

"PRESS THE START BUTTON TO JOIN" is not turned on by the stage's script: none of `level60.lua`'s
`HUDANEnableJoinMsg(n, true)` calls runs at the start (they are in the death, credit and bonus handlers). It is the
panels' own state at the set-up:

- `HUD_InitLevel` (`0x001ad588`) allocates the two `ANHud` panels in an Armies level and calls each one's interface
  slot `+0x80` with the panel's player (the call at `0x001ae260`). That slot, `ANHud_SetPlayer` (`0x004ec650`),
  stores the player at `+0x40f4` and **1 at `+0x4614`**, the join flag `ANHud_Update` and `ANHud_Draw` read. Seen:
  a breakpoint there stopped twice in the load, from `0x001ae260`, with player 0 and then player 1.
- `ANHud_Init` (`0x002107e8`) zeroes the flag (the store at `0x00210b40`). It runs on a panel's first attach only:
  `lua_HuCreate` → `Human_MakePlayer` (`0x00229c40`) → `HUD_AttachPlayer` (`0x001b2200`, which calls the interface's
  `+0x10` while `+0x4100` is 0) → `ANHud_Init`. Seen: a breakpoint on the store stopped once, for player 1's panel,
  with that stack.
- So player 1's flag is cleared when the level script makes Swan, and player 2's stays on: in play (the panel not
  shown, `+0x40fc` 0) `ANHud_Draw` draws the join text, blinking every 500 ms. Seen in play: panel 1 `+0x4614` 0,
  panel 2 `+0x4614` 1, `+0x40f4` 1, `+0x40f8` and `+0x40fc` 0.
- A knocked-out player's panel drops out. `ANHud_Draw` (`0x00211938`) draws the panel's parts only while it is
  shown (`+0x40fc`), wanted (`+0x40f8`) and loaded, and else the join text when the flag is on; on a frame it draws a
  wanted panel whose human is knocked out (`Human_IsKnockedOut`, `0x00227dd8`: state `0x40000`) it clears wanted, so
  from the next frame that player's join text shows while his flag is on, blinking as `ANHud_Update` (`0x00210e48`)
  steps it for a panel that is not wanted. The state is set by `Human_KnockOut` (`0x00230328`) both when the
  player's health runs out (`Human_OnHealthOut`, `0x002674c0`, which knocks out a human with flag `0x4`) and for
  `HuSetConscious(h, false)` ([Who sets the bit](combat-reactions.md#knocked-out-bit)), so a player beaten to 0
  health loses his panel too, from the update his dying reaction's last clip ends rather than at the hit
  ([When the knock-out comes](combat-reactions.md#knockout-timing)). Only `ANHud_Enable` (`0x0020fd40`, interface
  slot `+0x20`: wanted 1 and `PlayerHUD_Show`) sets wanted again. `HUD_AttachPlayer` (`0x001b2200`) calls it right
  after the init on every attach of a panel not yet wanted (the `ANHud` table at `0x0053ecc0` holds `ANHud_Init` in
  slot `+0x10`, `ANHud_Enable` in `+0x20` and `ANHud_Draw` in `+0x38`), and a continue calls it for each continuing
  player ([The game-over screen](#game-over)).
- The flag's own setter and getter are `ANHud_SetJoinMsg` (`0x002107d8`, which `HUD_ANEnableJoinMsg` `0x001b46a0`
  calls) and `ANHud_GetJoinMsg` (`0x002107e0`). A search for the operand `0x4614` finds them, `ANHud_Init`, `_Update`
  and `_Draw`, but not `ANHud_SetPlayer`, which Ghidra had left outside any function.

### The credits line {#credits}

Confirmed (code) at the cited addresses.

The credits line beside the GO sign is the HUD's own: `HUD_ANSetCredits` (`0x001b46e0`) writes the HUD word
`+0x190d0` (`0x00619910`, the HUD at `0x00600840`), which `HUD_InitLevel` (`0x001ad588`) zeroes at every level's
set-up, so the stages carry their credits from one to the next in the Lua save data. `HUD_Update` (`0x001af010`)
places the line at (0.75, 0.92) and then, in the default video mode (display flags 2, 4 and 8 clear), at x 0.71
(`0x0050d4bc`).

### The GO sign {#go-sign}

Confirmed (code) at the cited addresses. The sign's parts and its functions are listed on
[HUD: parts](hud-functions-parts.md); what follows adds to that list.

- **Mode 1 shows the text too.** `GoSign_Update` (`0x0019e758`) makes the text visible in mode 1 as in mode 0, and
  lights the three arrows one at a time (frames 0-2, none on frame 3); only a mode other than 0 and 1 hides it all.
- **The arrows' size.** `GoSign_Update` sets the arrows to 0.06 (`0x0050cf94`) and the colour `0x00600040` on every
  step, over the 0.08 `GoSign_Setup` (`0x0019e030`) gives them.
- **The language shift.** `GoSign_Setup` takes every x less a shift by the game's language (game state `+0x120`,
  `0x0050cf98`): 0.4 in Spanish, 0.02 in Italian, else 0. In the default video mode the text is at x 0.8 less the
  shift, the single arrow at 0.78, the three arrows at 0.835, 0.875 and 0.915, each less the shift.

### The game-over screen {#game-over}

Confirmed (code) at the functions each line names, and confirmed (runtime), run r-20261026-level60-idle, where a line
says it was seen.

The screen's layout, its strings and its function list are on
[Pause: the game-over screen](pause.md#an-game-over); these are its rules.

- **Who may continue** (`ANGameOver_Update`, `0x001cf468`). Start (`0x800`) counts from player 1, and from player 2 only
  when his human (game state `+0x22c`) has a pad record; player 2's pad record is locked while the screen is open.
- **The count's end** (`ANGameOver_Update`). The count steps once a second from 9 and ends after 0 has shown for a step,
  ten steps in all. Then both players' views fade out to black over 2 s: `ScreenFx_QueueEffect` (`0x0018d450`) with 2.0,
  the view and 1, on the two screen-effect managers at `0x005fdeb8`; these are screen fades, not sound channels. Seen
  with the pad idle: GAME OVER with Replay and Quit over black after the count.
- **The choice, on close.** `ANGameOver_Toggle` (`0x001557f8`) closes it once the close flag is set and the last cue
  has ended. **Replay** sets section 1 (`GameState_SetSection`) and loads `level60` (`LevelFlow_LoadLevelByName`, the
  name at `0x0054e7d8`). **Quit** sets game state `+0x14c` to 3 and calls the Lua function `runNextMission` with 0
  (its name at `0x0054e798`). A **continue** heals each continuing player in full (`Human_Heal(human, 1)`), sets his
  state `0x20000000`, calls his HUD panel's slots `+0x24` and `+0x7c(0)` and sets `+0x14c` to 0; that `+0x7c(0)`
  turns his join message off is inferred from the panel's interface order ([q-panel-7c](#q-panel-7c)).
- **Other video modes.** The layout values on Pause are the NTSC defaults (`0x0050ebf0`-`0x0050ec24`);
  `ANGameOver_Open` (`0x001cef10`) moves the usage lines, the digit and the grid for the others.
- **`level64`'s refusal.** In `level64` (record number `0x40`) at section 4, a Start is refused while the presser's own
  brain flag `+9` is set (`+0x228`'s for player 1, `+0x22c`'s for player 2); what that flag means is open
  ([q-brain-9](#q-brain-9)).
- **Opening it twice.** `HUD_LaunchANGameOver` (`0x001cede8`) toggles: called while mode 0xd is on top it pops it,
  with no choice made.
- **What it draws** (`Gm_ANGameOver_Update`, `0x00155c78`): one frame of the world under the menu, through
  `GameMode_RenderViewportAlt` (`0x00156540`), which draws the views' world, their 3D sprites and the ground rings and
  no HUD. Seen: no HUD under "CONTINUE?".

### The garage door {#garage-door}

Confirmed (code) at the cited addresses. `level60`'s garage is a `dyn_door_rollup` (door number 5, at
(−94.69, 19.66)), one of the sliding doors ([Script types: sliding doors](script-types.md#sliding-doors)); the
stage cannot start without it, because the player walks out through its doorway. What the stage relies on:

- **A command** (`SlidingDoor_StateCommand`, `0x003f5608`): 2 or 8 send the leaves state `0xd` (their update interval
  2 ticks; the update already scheduled stands, so a leaf resting on its 60-tick interval starts 1 to 60 ticks
  later), set the door's state byte `+0x2c` to 2 (what message `0x0c` answers "open" for) and play the open sound,
  then turn both collision triangles off and open the door's navigation links (`NavLink_Open`, `0x003a4b00`: the
  links lose their avoid bit and the doorway's hole gets flag 8). 3 or 7 do the same with `0xe`, state 3 and the close
  sound, then turn the triangles on and close the links. Message `0x0b` (a human opens it) is command 2, and the set-up
  ends with command 7, so a sliding door starts shut, its links avoided. `P1.Start02`'s `OpenDoor` is command 2.
- **The slide** (`SlidingDoor_SpawnLeaves`, `0x003f4c80`): with `CfgObj` property 7 not 0 the length is property 7
  and the axis byte 2; otherwise the length is property 8 and the axis byte 0; `dyn_door_fence_s` and `dyn_door_pol`
  add 1. In the leaf's own frame (`SubSlidingDoor_PlaceLeaf`, `0x003f4950`) byte 0 is up (+z), 1 down, 2 along +x,
  3 along −x. The step is twice property 6 (`DynDoorSliding_Init`, `0x003f5d10`), taken every 2 ticks
  (`SubSlidingDoor_Update`, `0x003f5a68`) until the offset passes the length, which it then takes; so a leaf
  overshoots by up to one step before it settles.
- **The leaves.** A roll-up door has one leaf, `dyn_dr_rollup`; `dyn_door_ele` and `dyn_door_pol` one of their own; the
  portcullis, both fences, `dyn_door_ele_c` and `dyn_door_subway` two, and `dyn_door_spook` `dyn_dr_spook_a` and `_b`. A
  second leaf sits 2 × property 5 along the door's −x axis (`dyn_door_ele_c`'s along its −z), turned 180° about its own
  z, or about its own y when the axis byte is below 2, so the lift's second leaf slides down while the first slides up.
  A leaf takes that pose and its slide direction on its first update, after the set-up's command 7. From then on every
  command (`SlidingDoor_StateCommand`) puts the poses back, for every type but `dyn_door_ele_c`: the first leaf to the
  door's rotation, the second to it turned 180° about z. The slide direction stays as placed, so after its first command
  a two-leaf up-and-down door's second leaf stands upright again and still slides down. The open and close sound is
  `0xd2479dee` for the roll-up, the lift doors and the spook door, `0x8a2b8511` for the portcullis and `0xf8638826` for
  the rest.
- **Its messages** (`DynDoorSliding_OnMessage`, `0x003f5ef0`): 8 detach, `0x0b` command 2, `0x0c` open when `+0x2c`
  is 2, `0x10` and `0x13` pin and unpin the leaves, `0x15` the door's destroy, `0x22` a command, `0x3e` its data.

## In Coney {#coney}

The mode is ported for one player in `level60`'s first checkpoint, as far as the idle run reaches: the sliding doors
(`repo:src/world_objects/doors.cpp`), the rail's mode 3 (`repo:src/camera/rail_camera.cpp`), the Armies HUD's credits,
GO sign and join messages (`repo:src/hud/armies_hud.cpp`), the timer's seconds form and the game-over screen
(`repo:src/gamemodes/armies_game_over_mode.h`, `repo:src/gui/pause_menu/armies_game_over_menu.h`). With the pad idle
the stage runs through the same steps as [the original's](#timer), in the same order, and a disc test holds it
(`repo:tests/platform/disc_armies_test.cpp`). It differs in the stand-ins below and in these
timings, counted from `P1.Start03`'s `GoalMoveToFlag` in 60 Hz ticks (Coney's dev build, `--play-level level60
--checkpoint 1`, its steps of 1/30 s counted twice, against run r-20261026-level60-clock):

| Step | Coney | Original |
| --- | --- | --- |
| Swan starts walking | 14 | 19-26 |
| He stops | 196 | 200-206 |
| `P1.Start04` (`CloseDoor`) | 210 | 223 |
| `F1.Time` (`W_SetStopWatch`) | 254 | 268 |
| `F1.TimerOut` (`HUDLaunchANGameOver`) | 6316 | 6336 |

So Coney's walk starts about 8 ticks sooner, the flag's message 8 comes 3 to 9 ticks sooner after the stop (14 ticks
against 17 to 23), and the timer runs 6 ticks shorter (3031 steps of 33 ms counted, 6062 ticks, against the original's
6068): the game over comes about 0.3 s sooner in all. Why each of the three differs is open
([q-walk-start](#q-walk-start), [q-flag-message](#q-flag-message), [q-timer-gap](#q-timer-gap)).

- **The GO sign's bleep** (`vags/misc/bleep27`, restarted by `GoSign_Render` `0x0019ed28`) is a marked stand-in:
  Coney's sign is silent.

- **Bindings.** Of the stage's 15 new bindings, 6 are not implemented
  ([Story coverage](../references/bindings/story.md#level60)): `HuColor`, `HuEnableController`, `GangSetMoney`,
  `CamLockRail`, `HUDSetANBossTexture`, `CfgBaseChanceToBlock`. The run goes on past them; `CamLockRail`,
  `HuEnableController` and `HUDSetANBossTexture` are issue #570.
- **The rail.** Setting 5 switches mode 3 on with its pitch and setting 7 keeps its margin; the keep-in-frame step
  that uses the margin is a marked stand-in, as Coney's rail has one target. The segment choice's differences are
  issue #956.
- **The join messages.** Both start on at the set-up and a panel's first attach clears its own (`ArmiesHud::levelSetUp`,
  `ArmiesHud::panelAttached`), so player 2's message shows in play as in the original, centred on the panel's place
  (`Font_Draw`'s flag `0x02`). Both lines span about 0.60 to 0.87 of the width in Coney's capture and 0.61 to 0.86
  (confirmed (runtime), run r-20261026-level60-join) in the original's. A knocked-out player's panel stops being
  wanted, so his message shows, until a continue enables it again (`ArmiesHud::update`, `ArmiesHud::panelEnabled`);
  Coney's stand-in asks it of every wanted panel, as its panels are not built (issue #662), and from the step after
  the one that saw the knock-out, as the original's draw does. No action prompt is drawn in an Armies level.
- **The game-over screen**, for one view, with these stand-ins, each marked in the code: it closes on the frame of the
  choice, because Coney's front-end audio cannot say when a cue has ended; the `level64` section 4 refusal is not
  built ([q-brain-9](#q-brain-9)); only the NTSC layout is laid out; the Armies score clear (`0x004229c0`, issue #704)
  and the `+0x14c` writes (issue #669) are left to their issues, and named at the code that would make them. A
  continue enables each continuing player's panel again.
- **The sliding doors**: all of [the garage door](#garage-door) but the streaming pins (issue #710), the leaves'
  collision bodies and messages 8, `0x15` and `0x3e` (issue #707), and the lock-pick glint. Stand-in: a command starts a
  leaf's slide on its next 2-tick step, not on its pending update ([q-slide-start](#q-slide-start)). Stand-in: Coney
  places the leaves at the set-up, after its command 7, not on their first update, so a command that came before that
  update would leave a two-leaf up-and-down door's second leaf turned about z where the original's ends turned about y
  ([q-leaf-place](#q-leaf-place)).
- `characters::isArmiesLevel` takes levels 60-69, as the original's test does
  (`repo:src/characters/character_class.cpp`); the `_a` models follow `Human_Init`'s own test of levels 60-64
  (`characters::modelNameFor`).
- **The black screen (item 19)**, reproduced in a disc test: the debug menu's Levels page listed every record of the
  level table, `101 armies` among them. Chosen from a level in play, the level flow loaded a record with no
  `armies.lua`, no objects and no streamed world, so nothing played and the screen stayed black. Since the fix the
  Levels page leaves out a record with no `.lev` on the disc and names it under "No level file", a jump to one is
  refused with a log line (`GameSession::levelLoads`), and the Missions page lists the five flashbacks and the five
  Armies stages after the story (`repo:src/debug/story_missions.cpp`).
- **The frozen view after the intro** (issue #85): after `l60_intro` Coney showed the street in front of the garage
  in a fixed view, with no player, no HUD and no timer. Coney gave sliding doors no behaviour, so `P1.Start02`'s
  `OpenDoor` left the garage's triangles on and its links closed, `P1.Start03`'s walk to `Flag_02` found no way out,
  the flag's message 8 never came and `P1.Start04`, the HUD and the timer never ran.

### Coney fixes (item 19) {#coney-fixes}

The minimum to show Armies of the Night instead of a black screen, in order. Steps 1, 2, 4, 5 and 6 are done; step 3
is issue #569:

1. **Debug Levels page:** do not offer a record that cannot load (done).
2. **Debug Missions page:** the five Armies stages and the five flashbacks, each starting at a checkpoint (done).
3. **The way in:** check that the hub's trophies run `flashback.PlayMission` and that the sixth (`level60`) is offered
   once levels 82, 92, 83, 20 and 11 are complete. That is the player's path, and a playthrough test should take it.
4. **Make the stage recognisable:** mode 3 of the rail and `isArmiesLevel` as 60-69 (done).
5. **The bindings:** the HUD's credits, GO sign and join messages, then the game over (mode 0xd), so that losing ends
   in the stage's own screen (done).
6. **Tests:** a disc test that `level60` at checkpoint 1 plays its intro, the walk out, the HUD, the timer and the game
   over with the pad idle (done); a playthrough from the trophy waits for step 3.

## Open questions {#open-questions}

### Levels 65-69 {#q-levels-65}

The test takes 60-69, but only 60-64 exist on the disc. What the rest were for is unknown; nothing depends on it.

### The `armies` record {#q-armies-record}

No script loads it (in a search of the disassembled chunks the word appears only in its own `CfgLevelName` record in
`config_preload3.lua`, the bank and the matrix). A search of the executable's callers of the level loader by name would
settle whether anything else does.

### The panel's slot `+0x7c` {#q-panel-7c}

The continue calls it with 0, read as the join message's off switch from the order of the panel's interface. A
breakpoint on it during a continue in PCSX2, reading the panel's `+0x4614` before and after, would confirm it.

### The brain flag `+9` {#q-brain-9}

`level64`'s section 4 refuses a continue while it is set; what sets it was not traced. Coney's refusal waits for it.
Find the writers of a brain's `+9` in Ghidra.

### When a leaf starts to slide {#q-slide-start}

By the code a leaf resting on its 60-tick interval starts 1 to 60 ticks after the command; this was not measured.
Reading the garage leaf's offset each tick after `P1.Start02` in PCSX2 would show it.

### When the walk starts {#q-walk-start}

Swan starts moving 19 to 26 ticks after `GoalMoveToFlag` in the original and 14 in Coney. Whether the original's goal
waits for its first route, a turn or the clip `P1.Start03` starts is not traced. A breakpoint on the goal's first
locomotion request after `0x0035ff18`, read against the tick count, would show it.

### When the flag's message 8 comes {#q-flag-message}

After Swan stops, the flag's message 8 comes 17 to 23 ticks later in the original and 14 in Coney. The goal sends it
from its End (`MoveToFlagGoal_End`, `0x002da4a0`) once he is inside the radius and turned to the flag's heading
([GoalMoveToFlag](ai-scripting.md#move-to-flag)); whether the original's extra ticks are that turn, the goal's update
interval or the stop itself is not traced. A breakpoint on `0x002da4a0` in PCSX2, with Swan's heading and position read
each tick from his stop, would show which.

### The timer's last six ticks {#q-timer-gap}

The original's 99,999 ms took 6068 ticks where the whole-millisecond count makes 6062 ([the timer](#timer)); the three
updates more are not traced. Counting the original's `StopWatch_Update` (`0x004233f8`) calls from `F1.Time`'s
`W_SetStopWatch` to `F1.TimerOut` in PCSX2, reading the watch's time at `+0x08` and the game timer at each call, would
show whether an update counted less than 33 ms, one was skipped, or the watch started or ended a frame apart from the
script steps.

### When a leaf is placed {#q-leaf-place}

The original places a sliding leaf on its own first update (`SubSlidingDoor_PlaceLeaf`, `0x003f4950`), after any
command that came before it, and again on its message `0x40` (`SubSlidingDoor_OnMessage`, `0x003f5c38`; confirmed
(code)); Coney places it at the door's set-up. The two differ only for a two-leaf up-and-down door
that gets a command before its leaves' first update. Whether any level does is not known: a search of the decompiled
scripts for `OpenDoor` and `CloseDoor` on such a door in the frame its level loads, or a breakpoint on
`SlidingDoor_StateCommand` (`0x003f5608`) before the first `SubSlidingDoor_Update` (`0x003f5a68`) of a level with one,
would settle it.

## How this was checked

The runs are run records: r-20261026-level60-idle (the idle stage, from the load to GAME OVER, sampled four times a
second with window captures every 4 s), r-20261026-level60-join (breakpoints on the join flag's set and clear during the
load, then the panels' fields read in play) and r-20261026-level60-clock (breakpoints on the script steps' bindings from
the walk to the game over, the player sampled about every 100 ms, the game timer read at the end). Coney's side is the
dev build's `--play-level level60 --checkpoint 1` with the pad idle.
