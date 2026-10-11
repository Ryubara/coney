# Armies of the Night

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the compiled Lua chunks
of the retail disc (read as disassembly, the method of [Scripts](scripting.md); names, numbers and calls only).
Static reading, 2026-10-11. Each claim carries its evidence level.

## Purpose

Armies of the Night is the original's bonus game: five stages of a side-on brawler in the game's own engine. This
page says how the player reaches it, what loads, and which engine pieces exist only for it. It ends with what
Coney needs to show it instead of a black screen (owner checklist item 19). The pieces have their own pages, linked
below. This page gathers them and does not repeat them.

## What it is {#what-it-is}

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
| HUD | two `ANHud` panels (health, power, credits, the boss picture), the GO sign; `HideHud` does nothing while game state `+0x14c` is 1 | [HUD: the Armies panel](hud.md#fn-anhud) |
| Failure | no mission-failed shot; the Armies game over (mode 0xd, `ANGameOver_Toggle` `0x001557f8`) | [Pause: the game-over screen](pause.md#an-game-over) |
| Pause | its own pause menu | [Pause: the Armies pause menu](pause.md#an-pause-menu) |
| Sound | the sound matrix `armies` (`global.lua`, levels 60-64), the bank `armies`, the load screen's bank `armload` | [Sound: banks](sound.md#banks), [Scripts: global.lua](scripting.md#globallua) |
| Two players | player 2 joins through `SetArmiesMultiplayerCallback`, kept at game state `+0x56f4` | [Front end: game-state functions](frontend.md#warriors-functions) |

- **The record `armies`.** The level table also holds a record named `armies` (index 101, world `level60`, number
  419, no sections, no `.lev` on the disc). `LoadLevel` reads `<name>.lev`, here `armies.lev`, so the record cannot
  load as it stands. Its number is not 60-69 either, so it is not an Armies level. Inferred: a leftover test record,
  like the others with no `.lev` ([Level loading](level-loading.md)).

## `level60`'s start {#level60-start}

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

## In Coney {#coney}

Read in the engine and seen in a run of the 2026-10-10 release build (`--play-level level60`, 600 steps, pad idle).
That run is an observation of Coney, not of the original:

- `level60` loads and its scripts run through the intro scene `l60_intro`, the music and `P1.Start03`'s rail. The
  last frame shows the street in a fixed view. Of the stage's 15 new bindings, 11 are not implemented
  ([Story coverage](../references/bindings/story.md#level60)): `HuColor`, `HuEnableController`, `GangSetMoney`,
  `CamLockRail`, `HUDANEnableJoinMsg`, `HUDANGetCredit`, `HUDANSetCredit`, `HUDANSetGOSignMode`,
  `HUDLaunchANGameOver`, `HUDSetANBossTexture`, `CfgBaseChanceToBlock`. The run goes on past them.
- The rail's settings 5, 6 and 7 do nothing (`repo:src/camera/rail_camera.h`), so the rail camera does not trail the
  fighters as mode 3 does.
- `characters::isArmiesLevel` takes levels 60-64 (`repo:src/characters/character_class.cpp`); the original's test
  takes 60-69.
- The debug menu's Levels page lists every record of the level table, `101 armies` among them
  (`repo:src/debug/pages_scripts.cpp`). Coney looks for that record's world under its name: `--play-level armies`
  stops with "no streamed world is called armiess, armiesd or armies". Chosen from a running game, the level flow
  has nothing to show. This is the likeliest black screen: the entry reads like "Armies of the Night", but the
  original cannot load it either.
- The Missions page lists only the 18 story missions (`repo:src/debug/story_missions.cpp`), and nothing outside the
  hub's trophies loads `level60`.

## Coney fixes (item 19) {#coney-fixes}

The minimum to show Armies of the Night instead of a black screen, in order:

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
   panel](hud.md#fn-anhud)), then the game over (mode 0xd, [Pause](pause.md#an-game-over)), so that losing ends in
   the stage's own screen and not in the story's mission-failed one.
6. **Tests:** a disc test that `level60` at checkpoint 1 reaches `P1.Start03` with the rail camera active and mode 3
   set; a debug-menu test that no listed level fails to load; and, once the hub path exists, a playthrough from the
   trophy.

Deferred, runtime: what the original's first frames show at `level60` checkpoint 1 (the black hold, the intro
scene, the first frame of `lock01`), to compare with Coney's. PCSX2 steps: a save with the five flashbacks complete,
the hub trophy for Armies of the Night chosen with cross, then frame captures every second from the load screen's
end to `P1.Start03` (about 10 s), pad idle (left stick centred, 0, 0).

## Open questions {#open-questions}

- What levels 65-69 would be: the test takes 60-69, but only 60-64 exist on the disc.
- Whether anything reaches the `armies` record. No script loads it: in a search of the disassembled chunks, the
  word appears only in its own `CfgLevelName` record (`config_preload3.lua`), the bank `armies` and the matrix
  `armies`.
