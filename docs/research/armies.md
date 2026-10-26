# Armies of the Night

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the compiled Lua chunks
of the retail disc (read as disassembly, the method of [Scripts](scripting.md); names, numbers and calls only).
Static reading, 2026-10-11. Each claim carries its evidence level.

## Purpose

Armies of the Night is the original's bonus game: five stages of a side-on brawler in the game's own engine. This
page says how the player reaches it, what loads, and which engine pieces exist only for it. It ends with what
Coney needs to show it instead of a black screen (user checklist item 19). The pieces have their own pages, linked
below. This page gathers them and does not repeat them.

## Original structure: what it is {#what-it-is}

- **Five levels.** `level60` to `level64`, records 24-28 of the level table, titled `Stage 1` to `Stage 5`, with 3,
  3, 3, 3 and 4 sections ([Levels](../references/levels.md#level-24)). Each one's last checkpoint script loads the
  next (`level60_mothers` → `level61`, ...), and `level64`'s last returns to the hub
  ([Story order](scripting.md#run-next-mission); inferred, from the scripts).
- **The way in.** The clubhouse's trophies in the hub (`level95`) start the flashbacks. The sixth entry of
  `FBMission` is `{level = 60, req = {82, 92, 83, 20, 11}}`: Armies of the Night is offered once the five flashbacks
  are complete, and `flashback.PlayMission` (action `MA_LOADFBLEVEL`) loads `level60` (inferred, from the disassembly
  of `level95.lua`). Nothing else leads there:
    - the main menu offers STORY, EXTRAS (only the trailer) and QUICK RUMBLE ([The screens](frontend.md#pm-screens));
    - the mission menu leaves levels 60-65 out (`MissionSelect_IsMissionListed`, `0x001bc100`, confirmed (code));
    - a cheat loads the next stage from within one (`CheatArmiesLoadLevel`, [Debug](debug.md)).
- **No mode of its own.** A stage plays in the ordinary gameplay mode (mode 1). The engine switches on the
  current level's number: `GameState_IsArmiesLevel` (`0x0041d110`) is true when the level record's number (`+0x04`)
  is **60-69** (confirmed (code)). What it switches:

| Piece | In an Armies level | Page |
| --- | --- | --- |
| Character models | every human drawn with its type's `_a` model | [Characters: from a type to a model](characters.md#type-to-model) |
| Camera | the rail's mode 3: trails the hindmost fighter at a fixed downward pitch, forward only | [Camera: mode 3](camera.md#rail-mode3) |
| HUD | two `ANHud` panels (health, power, credits, the boss picture), the GO sign; `HideHud` does nothing while game state `+0x14c` is 1 | [HUD: the Armies panel](hud-functions-panels.md#fn-anhud) |
| Failure | no mission-failed shot; the Armies game over (mode 0xd, `ANGameOver_Toggle` `0x001557f8`) | [Pause: the game-over screen](pause.md#an-game-over) |
| Pause | its own pause menu | [Pause: the Armies pause menu](pause.md#an-pause-menu) |
| Sound | the sound matrix `armies` (`global.lua`, levels 60-64), the bank `armies`, the load screen's bank `armload` | [Sound: banks](sound.md#banks), [Scripts: global.lua](scripting.md#globallua) |
| Two players | player 2 joins through `SetArmiesMultiplayerCallback`, kept at game state `+0x56f4` | [Front end: game-state functions](frontend.md#warriors-functions) |

- **The record `armies`.** The level table also holds a record named `armies` (index 101, world `level60`, number
  419, no sections, no `.lev` on the disc). `LoadLevel` reads `<name>.lev`, here `armies.lev`, so the record cannot
  load as it stands. Its number is not 60-69 either, so it is not an Armies level. Inferred: a leftover test record,
  like the others with no `.lev` ([Level loading](level-loading.md)).

## Data

Armies of the Night owns no structure of its own: the level records are the level table's
([Levels](../references/levels.md#level-24)), and the two game-state fields it uses are named in the table above
(`+0x14c`, `+0x56f4`).

## Behaviour: `level60`'s start {#level60-start}

What the first stage does before the player can move, inferred from the disassembly of `level60.lua` and
`level60_destroyers.lua`. It shows which bindings the first frames need.

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
   later, plays the intro scene `l60_intro` through the stage's own scene runner. The scene brings the picture back
   (inferred). Its end (`PreCashTheWorld`, then `P1.Start02`) starts the music `music/armies_pt1` and opens a door.
   4.5 s later `P1.Start03` makes the rail camera active (`Rail1`):
   `CamSetupRail("rail", player, 22, {0, −0.25, 1.25}, 0.5, 90)`, six points along y = 39.5 from x = 100 to
   x = −850, then `CamModifyRail(5, −7, 0)` (mode 3, pitch −7°) and `CamModifyRail(7, 0.5)`.

So until the intro scene runs, the screen is black by the script's own choice.

### The original's first frames {#first-frames}

At `level60` checkpoint 1 (confirmed (runtime), PCSX2 2.9.94, 2026-10-11; reached by the game's checkpoint jump from a
story save (slot 6), not from the hub trophy, so what the hub shows before the load is not covered; window captures
about every 4 s, timed by the game's 60 Hz tick count `0x005104f4`, which starts again at the level; pad idle
throughout, left stick centred (0, 0)):

| Tick (s) | What shows |
| --- | --- |
| before 1 | the load screen: the Armies of the Night art with **"STAGE ONE"** and a red progress bar (two captures, about 7 s) |
| 365-1251 (6-21 s) | the intro scene, no HUD: a fixed side view of a garage under a red graffiti "W", pedestrians walking past, then the shutter rising on a car with Swan standing inside |
| 1667 (28 s) | play: Swan on the pavement in front of the garage, the same side-on view; the HUD on (below) |
| 1667-7527 | nothing moves while the pad is idle; the timer counts down **one a second** (98 at 1667, 00 at 7527: 98 in 97.7 s) |
| 7543 (126 s) | **"CONTINUE?"** in red over the scene, "Press the START button to continue" below it |

**The walk out** (confirmed (runtime), PCSX2 2.9.129, 2026-10-21; `level60` checkpoint 1 loaded from a level99 state
with `SetCheckPoint(1) MenuLoadLevel("level60")`, the player's transform and the tick count read four times a second,
pad idle): Swan stands at (−93.5, 18.8), where the level script makes him, through the intro scene; at tick 1219-1233
(20.3 s, `P1.Start03`'s `GoalMoveToFlag(player, Flag_02, 2, 0, 0, 0.5, 0, true)`) he walks (gait 2, 1.63 m/s) in a
straight line out of the garage to (−94.78, 23.54), where he stops at tick 1399-1413: 0.48 m from `Flag_02`
(−94.9, 24), inside the goal's 0.5 m radius. The flag's message 8 then runs `P1.Start04` (inferred, from the
disassembly of `level60_destroyers.lua`), whose `F1.Restore01` and `F1.Restore02` bring the HUD back and start the
timer (`F1.Time`: `W_SetStopWatch(99999, 0, "F1.TimerOut")`, shown in its seconds form); `F1.TimerOut` calls
`HUDLaunchANGameOver(true)`, the "CONTINUE?" screen ([Pause: the game-over screen](pause.md#an-game-over)), whose
countdown ends on "GAME OVER" with Replay and Quit over black.

The HUD in play: top left Swan's portrait, his name and a seven-digit score (0000000) over a green health bar; top
centre the two-digit **timer** (starting at 99); top right **"PRESS THE START BUTTON TO JOIN"**, blinking; at the right
edge **"GO"** with an arrow pointing right, blinking; bottom right **"CREDITS: 07"** (`HUDANSetCredit(7)`). So the idle
stage runs a 99-second timer that ends in the continue screen without any fight ([HUD: the Armies
panel](hud-functions-panels.md#fn-anhud) for the panel's code). Which script call starts the timer and the `P1.Start03`
timing were not traced here.

### The join messages {#the-join-messages}

"PRESS THE START BUTTON TO JOIN" is not turned on by the stage's script: none of `level60.lua`'s
`HUDANEnableJoinMsg(n, true)` calls runs at the start (they are in the death, credit and bonus handlers). It is the
panels' own state at the set-up:

- `HUD_InitLevel` (`0x001ad588`) allocates the two `ANHud` panels in an Armies level and calls each one's interface
  slot `+0x80` with the panel's player (`0x001ae260`). That slot, **`0x004ec650` `ANHud_SetPlayer`** (made a
  function in the Ghidra project on 2026-10-26), stores the player at `+0x40f4` and **1 at `+0x4614`**, the
  join flag `ANHud_Update` and `ANHud_Draw` read. Confirmed (code) at `0x004ec650` and confirmed (runtime): a write
  watchpoint on panel 1's `+0x4614` stopped there with `a0` the panel and `a1` 0, called from `0x001ae260`
  (PCSX2 2.9.129, 2026-10-26, `level60` checkpoint 1 loaded from a level99 state).
- `ANHud_Init` (`0x002107e8`) zeroes the flag (`0x00210b40`). It runs on a panel's first attach only:
  `lua_HuCreate` → `Human_MakePlayer` (`0x00229c40`) → `HUD_AttachPlayer` (`0x001b2200`, which calls the interface's
  `+0x10` while `+0x4100` is 0) → `ANHud_Init`. Confirmed (runtime): the watchpoint's second stop, with that stack.
- So player 1's flag is cleared when the level script makes Swan, and player 2's stays on: in play (the panel not
  shown, `+0x40fc` 0) `ANHud_Draw` draws the join text, blinking every 500 ms. Read in PCSX2 during play: panel 1
  `+0x4614` = 0, panel 2 `+0x4614` = 1, `+0x40f4` = 1, `+0x40f8` and `+0x40fc` = 0.
- Search note: a search for the operand `0x4614` finds the setter `0x002107d8`, the getter, `ANHud_Init`, `_Update`
  and `_Draw`, but missed `0x004ec650` while that code was not yet a function.

## Coney's implementation {#coney}

Read in the engine and seen in a run of the 2026-10-10 release build (`--play-level level60`, 600 steps, pad idle).
That run is an observation of Coney, not of the original:

- `level60` loads and its scripts run through the intro scene `l60_intro`, the music and `P1.Start03`'s rail. The
  last frame shows the street in a fixed view. Of the stage's 15 new bindings, 6 are not implemented
  ([Story coverage](../references/bindings/story.md#level60)): `HuColor`, `HuEnableController`, `GangSetMoney`,
  `CamLockRail`, `HUDSetANBossTexture`, `CfgBaseChanceToBlock`. The run goes on past them. (`HUDANEnableJoinMsg`,
  `HUDANGetCredit`, `HUDANSetCredit`, `HUDANSetGOSignMode` and `HUDLaunchANGameOver` came with issue #85.)
- The rail's setting 5 switches mode 3 on with its pitch and setting 7 keeps its margin
  (`repo:src/camera/rail_camera.h`); the keep-in-frame step that uses the margin is a marked stand-in, as Coney's rail
  has one target.
- `characters::isArmiesLevel` takes levels 60-69, as the original's test does (`repo:src/characters/character_class.cpp`;
  it took 60-64 until the fix for item 19).
- **The black screen (item 19)**, reproduced in a disc test (`repo:tests/platform/disc_armies_test.cpp`): the debug
  menu's Levels page listed every record of the level table, `101 armies` among them. Chosen from a level in play,
  the level flow loaded a record with no `armies.lua`, no objects and no streamed world ("no streamed world is called
  armiess, armiesd or armies"), so nothing played and the screen stayed black. `level60` itself, started directly or
  jumped to from the front end or a level in play, loads, plays `l60_intro` and goes on with the picture clear (the
  stage's fade level 0 throughout 1,500 frames in each of the three ways). Since the fix the Levels page leaves out a
  record with no `.lev` on the disc and names it under "No level file", a jump to one is refused with a log line
  (`GameSession::levelLoads`), and the Missions page lists the five flashbacks and the five Armies stages after the
  story (`repo:src/debug/story_missions.cpp`).
- **The frozen view after the intro** (issue #85, main of 2026-10-20): after `l60_intro` Coney showed the street in
  front of the garage in a fixed view, with no player, no HUD and no timer. The cause was the garage's door: a
  `dyn_door_rollup` is a sliding door, and Coney gave sliding doors no behaviour, so `P1.Start02`'s `OpenDoor` left
  its triangles on and its links closed, `P1.Start03`'s walk to `Flag_02` found no way out of the garage (each move
  ended at once), the flag's message 8 never came and `P1.Start04`, the HUD's return and the timer never ran. Coney
  now has the sliding doors ([Sliding doors](script-types.md#sliding-doors)), the rail's mode 3, the Armies HUD's
  credits, GO sign and join messages, the timer's seconds form and the game-over screen (mode 0xd); with the pad idle
  the stage plays as [the original's first frames](#first-frames) do. Both join messages start on at the set-up and
  a panel's first attach clears its own ([The join messages](#the-join-messages); `repo:src/hud/armies_hud.cpp`), so
  player 2's "PRESS THE START BUTTON TO JOIN" shows in play as in the original, centred on the panel's place
  (`Font_Draw`'s flag `0x02`; both lines span about 0.60 to 0.87 of the width in Coney and 0.61 to 0.86 in the
  original, confirmed (runtime) by the captures of 2026-10-26).

### Coney fixes (item 19) {#coney-fixes}

The minimum to show Armies of the Night instead of a black screen, in order. Steps 1, 2, 4, 5 and 6 are done (the
black screen was step 1's record; 4-6 with issue #85, the playthrough from the trophy waiting for step 3). Step 3 is
issue #569; the bindings #85 left out (`CamLockRail`, `HuEnableController`, `HUDSetANBossTexture`) are issue #570:

1. **Debug Levels page:** do not offer a record that cannot load. Skip, or mark and refuse with a log line, a record
   with no `.lev` of its name: `armies`, and the other test records with no level file. That is what the original's
   `LoadLevel` would do with them. This is tooling, so the change is free.
2. **Debug Missions page:** add the five Armies stages (`level60`-`level64`, with 3, 3, 3, 3 and 4 checkpoints, from
   `research/missions.yaml`'s `armies` group). Each starts at a checkpoint as the story missions do, so the stages
   are one choice away. Do the same for the five flashbacks if they are missing.
3. **The way in:** check that the hub's trophies run `flashback.PlayMission` and that the sixth (`level60`) is offered
   once levels 82, 92, 83, 20 and 11 are complete. That is the player's path, and a playthrough test should take it.
4. **Make the stage recognisable:** build mode 3 of the rail ([Camera: mode 3](camera.md#rail-mode3)), so the
   camera trails the hindmost fighter at the −7° pitch. Take `isArmiesLevel` as 60-69.
5. **Then the bindings in the list above**, the HUD's credits and GO sign first ([HUD: the Armies
   panel](hud-functions-panels.md#fn-anhud)), then the game over (mode 0xd, [Pause](pause.md#an-game-over)), so that
   losing ends in the stage's own screen and not in the story's mission-failed one.
6. **Tests:** a disc test that `level60` at checkpoint 1 reaches `P1.Start03` with the rail camera active and mode 3
   set; a debug-menu test that no listed level fails to load; and, once the hub path exists, a playthrough from the
   trophy. With the pad idle, compare with [the original's first frames](#first-frames): the load screen, the intro
   scene, play with the full HUD at about 28 s, the timer at one a second and "CONTINUE?" at about 126 s.

## Open questions {#open-questions}

- What levels 65-69 would be: the test takes 60-69, but only 60-64 exist on the disc.
- Whether anything reaches the `armies` record. No script loads it: in a search of the disassembled chunks, the
  word appears only in its own `CfgLevelName` record (`config_preload3.lua`), the bank `armies` and the matrix
  `armies`.
