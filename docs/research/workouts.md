# Workouts (the hangout's gym)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Script claims come from the
compiled bytecode of `level95.lua` (`worksetup`), `level95_workout.lua` (`workout`) and `global.lua` (the
`Unlockables` list). Runtime claims were made in PCSX2 2.9.94 (2026-10-11) on a copy of quick-save slot 1 with the
`scripted-pad`, `call-brains`, `workout-rep`, `workout-end`, `tutorial-callback` and `attack-score` patches
([patches](repo:research/traces/patches.toml)), switched to `level95` checkpoint 2 by calling `SetCheckPoint`'s worker
`0x0041abc0(2)` and `MenuLoadLevel`'s worker `0x00160d78("level95")` as on [AI](ai.md#level99-save); the player (Cleon
at that checkpoint) was put beside each station by writing his transform, and triangle, cross and square were pressed
through the patched pad bytes, one update per press. Coney claims were made with the release build of 2026-10-10.

## Purpose

The clubhouse gym of `level95` has four stations. Three are **cycle games** (chin-ups, push-ups, sit-ups): the player
pumps cross to keep a loop clip going, and each loop is a repetition. The fourth, the **heavy bag**, is a **combo
game**: the game calls out a short combo, the player lands it on the bag, against a clock. Finishing a set raises the
station's **rank** (0-10), kept as an unlockable; ranks 5 and 10 at all four stations unlock the power and stamina
upgrades. This page is what an implementer needs to rebuild all four, with the reason Coney's start does nothing today
([Coney](#coney)) and the fix list and test plan ([Notes for implementers](#notes-for-implementers)).

## The stations {#stations}

`worksetup.workFlag` is the station's index 1-4, the same index into `fStartWorkout` (where the prompt is),
`fWorkout` (where the player works out) and `fWatchWork` (where a Warrior using the station is sent). The flags are
made in `level95.lua`'s set-up; positions in metres, z up, heading in the flag's degrees. Confirmed (code), the
positions also seen in Coney's event log.

| Index | Station | `fStartWorkout` | `fWorkout` | `fWatchWork` | Game | Unlock offset |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Heavy Bag | (-174.8, 106, -197.5), 93 | (-175.5, 106, -197.4), 93 | (-182.3, 100, -197.5), 62 | combo | 11 |
| 2 | Chin Ups | (-181, 106.4, -197.5), 2 | (-181, 105.7, -197.5), 2 | (-181.9, 111, -197.5), 112 | cycle | 31 |
| 3 | Push Ups | (-174, 109.6, -197.4), 182 | (-174, 109, -197.4), 182 | (-174.1, 101.1, -197.5), 2 | cycle | 41 |
| 4 | Sit Ups | (-177.2, 109.5, -197.4), 182 | (-177.2, 108.7, -197.4), 182 | (-179.5, 99.5, -197.5), 22 | cycle | 51 |

The gym is on the clubhouse's lower floor (z ≈ -197.6), below the main room (z ≈ -194.4). The bag itself is a human,
`HeavyBaga` (character type 460, gang `HeavyBag`), made at `fStartWorkout[1]` by `level95.lua`'s `AddHeavyBag`;
`worksetup.ConfigHeavyBag` makes it brain-dead, unpushable, ungrabbable, ungroundable, unstunnable, in god mode, with
no money and its look blocked. While no player uses the bag, the Warrior at `fWorkout[1]` spars with it through
`GoalBoxer` ([Boxer](#boxer)). Confirmed (code).

Each station's set-up (`workout.Setup`, by `workFlag`), confirmed (code):

| Station | `CfgWorkoutParams` gain / decay / hold | `repTable` (ranks 1-10) | `timeTable` s (ranks 1-10) | Clips (enter, exit, slow, mid, fast) |
| --- | --- | --- | --- | --- |
| 1 bag | none | 10 10 10 10 10 15 15 15 15 15 | 26 24 22 20 18 34 32 30 28 26 | none (the player's own attacks) |
| 2 chin-ups | 0.04 0.04 0.03 / 0.03 ×3 / 3 2 2 | 5 5 10 10 10 15 15 15 20 20 | 0 12 22 20 18 33 30 27 41 36 | `chinups_enter`, `_exit`, `_slow`, `_mid`, `_fast` |
| 3 push-ups | 0.02 0.04 0.04 / 0.03 ×3 / 3 2 3 | as chin-ups | 0 12 20 18 16 29 25 21 31 26 | `push_up_enter`, `push_up_e`, `push_up_slow`, `_mid`, `_fast` |
| 4 sit-ups | 0.1 0.06 0.03 / 0.03 ×3 / 3 2 3 | as chin-ups | as chin-ups | `situps_enter`, `_exit`, `_slow`, `_mid`, `_fast` |

A time of 0 means no clock (rank 1 of the cycle games). Every station call passes a fourth argument of 1.8 to
`CfgWorkoutParams` ([Parameters](#parameters)). The three cycle stations also make no camera of their own beyond
`workout.cam`; the bag makes three ([The heavy bag](#heavy-bag)). Each station has one locked camera `workout.cam`
(positions in [Camera](camera.md#level95-calls)).

## Starting a workout {#start}

1. **The prompt.** `worksetup.EnableWorkouts(true)` gives each `fStartWorkout` flag a message-0 handler with a hint
   (`SetMsgHandlerEx`): `worksetup.StartWorkout` when the station's rank can be raised, else `worksetup.NoWorkout`
   (which does nothing); the hint shows the station's name and `Rank: n/10` ([Ranks](#ranks)). The context action is
   triangle within the action distance, which `vbWorkout_01`'s enter and leave handlers set to 1.8 m and back to
   1.1 m (`CfgActionDistance(1, ...)`). Confirmed (code); confirmed (runtime): the hint shows beside each station.
2. **`worksetup.StartWorkout(flag, human)`**, for a player only: `workFlag` = the flag's index; clears the flag's
   handler; `HuDropWeapon`; when a Warrior occupies `fWorkout[k]`, swaps him to `fWatchWork[k]` (on difficulty 3 he
   also drops the bat he was given); the player says voice line 201 (`workout`); `DeactivateWarchief`; the player's
   message 1 (damage) goes to `worksetup.PlayerTakesDamage`; `fWorkout[k]`'s message 8 (arrival) to
   `worksetup.DoWorkout`; **`GoalMoveToFlag(player, fWorkout[k], 2, 0, 0, 0.5, 0, true, false)`** (walk, radius
   0.5 m, face the flag's heading); `ScheduleFunc("worksetup.TimeOut", 5000)`; loads `level95_workout` with
   `workout.Setup` as its callback (or calls `workout.Setup` when it is loaded). Confirmed (code).
3. **Arrival.** `worksetup.DoWorkout` clears the player's damage handler and the arrival handler and calls
   `worksetup.RetryWorkout`, which calls `workout.StartWorkout` once the file is loaded (else retries every 250 ms).
   If the player has not arrived after 5 s, `worksetup.TimeOut` quits (`workout.ForceQuit`, or `ActivateWarchief`
   before the file loaded); damage on the way quits through `worksetup.EarlyOut`. Confirmed (code). At runtime the
   walk took 12 updates (action code 3) and the workout's first update came 34 updates after the triangle press.
4. **`workout.StartWorkout`**: flushes `worksetup.TimeOut`; `HuSetPushable(player, false)`; the player's damage
   message 1 to `workout.PlayerTakesDamage` (which times the workout out); `rankPassed` =
   `worksetup.BuildWorkoutMessage(type, false)` (the rank being attempted), capped at `checkpoint`; gives
   `fWorkout[type]` a message-0 handler whose hint is the "press triangle to quit" text, its function
   `workout.ForceQuit` for the bag and `workout.NoHandler` (nothing) for the cycle games; cuts to `workout.cam`
   (`curCam` = 1); **`TeleportToFlag(player, fWorkout[type], -1)`** (the player stands exactly on the flag, facing its
   heading); calls the station's start function (`CalloutCombos01` for the bag, `StartCycleGame` for the others).
   Confirmed (code).

## The cycle games {#cycle}

### The script side {#cycle-script}

- **`StartCycleGame`**: `HuSetWorkoutCallbacks("workout.Started", "workout.DoRep", "workout.CycleQuit")`;
  `HuWorkout(player, player, enter, exit, slow, mid, fast)` with the station's five clips; `ScheduleFunc(
  "workout.StartTimeOut", 1500)`, which stops the workout and quits if `workout.started` is still unset. Confirmed
  (code).
- **`Started(h)`** (the engine's start callback): for a human other than the player, `HuStopWorkout(h)`; for the
  player: `started` = true, `ActivateWarchief`, `StartHUD` ([HUD](#hud)), `HUDTurnOnActionCycleAnim(4, 0.1, 6, 79,
  79)` (the blinking cross icon, [HUD](hud.md#action-prompts)), `HuLockPadMovement(player, true)`,
  `HuSetWorkoutBlend(player, 1.5)`. Confirmed (code).
- **`DoRep(h)`** (each repetition): `reps` + 1; `SoundPlay2D("vags/misc/menu_spray")`; the counter panel's value; when
  `reps` ≥ `repTable[rankPassed]`: `UM_Unlock(Level, 1, unlockOffset + rankPassed - 1)` (when `rankPassed - 1` ≤
  `checkpoint`), `DelayedWin` and `workoutDone` set, `fWorkout[type]`'s message-0 handler cleared,
  `HuStopWorkout(player)`. Confirmed (code); confirmed (runtime): five repetitions at chin-ups, push-ups and sit-ups
  rank 1 each ended the workout on the fifth (`workout-rep` and `workout-end` hooks), and after the chin-up set the
  unlock bit of record 83 (`95, 1, 31`, data 32) cleared at `0x006fe8f8` (word 2, bit 19).
- **`CycleQuit(h)`** (the engine's end callback): `HUDTurnOffActionCycleAnim`; after a won set `DelayQuit` 1.5 s later
  (`ForceQuit`, then the rank objectives redrawn), else `ForceQuit` at once. Confirmed (code).

### The engine side {#engine}

`HuWorkout` (`0x00238bd8`) sets the human's anim overrides for slots 691-695 (`0x2b3`-`0x2b7`) to the five clips,
its object target to the second argument and its action code to `0x1b`. `HuStopWorkout` (`0x00238d30`) sets action
`0x1c` while working out, or 0 while still in `0x1b`. `HuSetWorkoutBlend` (`0x00238cd8`) writes the effort `+0x670`
of a human that is working out. Confirmed (code).

**`Human_UpdateWorkout`** (`0x00255540`, state code `0x1b`). Confirmed (code) unless marked:

1. **Start (action `0x1b`).** With an object target within `AttackTable_GetFarRange(h, 691)` (`0x00254508`: the
   human's attack table entry for the clip, `max(short +8 × 0.001, float +4 × 1.25)`) and all five slots 691-695
   holding clips: three tasks are taken; a fade-out (0.1333 s) is pushed; a **gait blend** (type 12,
   [Gait blend](formats/animation.md#gait-blend)) over slots 693-697 with speed 16 units/s, flags `0x201` (the target
   is not rounded) and **`Workout_CallRep`** (`0x00234768`) as its wrap callback; a chained task plays the enter clip
   691 and then the gait blend; push sphere off; held flag `0x2000`; action back to 0; effort `+0x670` = 1.0, hold
   counter `+0x674` = 6; `Human_AlignToObject(0, h, target, 691, side 0)`; **`Human_StartWorkout`** (`0x0022d348`:
   state flag `0x20000000000`, then the start callback). Otherwise the action goes back to 0 and nothing starts.
   The scripts pass the player as his own target, so the distance is 0 and the reach test always passes; the
   position is the one `TeleportToFlag` gave (inferred).
2. **Each update while working out, pad-controlled** (player record `+0x1b` set), with *v* the gait blend's current
   value (`+0x38`) and the level `L = floor(v + 0.5)` (0-2) indexing the three tables:
    - The **timing factor** *f* is 1, unless `0x005106ac` is not 1 and the lower clip's time *t* is at most half its
      duration *d*, when *f* = `0x005106ac` × (1 − *t* / (*d* / 2)). The scripts always make `0x005106ac` 1
      ([Parameters](#parameters)), so *f* is always 1.
    - **Pump**: when the player's command is `0x12` (cross newly pressed, [Combat](combat.md#commands)), the hold
      counter = `hold[L]`. Then, if the counter is not 0, effort += `gain[L]` / *f*, capped at 1. Without a pump, a
      non-zero counter is first decremented and, if still not 0, adds the same gain. So one press adds the gain on
      `hold[L]` updates.
    - **Decay**, on an update that adds nothing: effort −= `decay[L]` × 0.3 when 3 × effort < 1, else `decay[L]` ×
      *f*; never below 0.
    - **Blend and rate**: the task's rate is set to 1. When 3 × effort < 1 the blend target is 0, and once *v* is 0
      the rate becomes 3 × effort + 2 × `decay[L]`: the slow clip slows down. Otherwise the target is 3 × effort − 1
      (0-2): slow (693) at 0, mid (694) at 1, fast (695) at 2. Slots 696 and 697 are never reached.
    - **End**: on command 10 (triangle newly pressed), or when *v* is 0 and the rate set the update before is at
      most 3 × `decay[L]` (effort worn down to about `decay` / 3), the action becomes `0x1c`.
3. **AI-controlled** (record `+0x1b` clear): the effort is whatever `HuSetWorkoutBlend` wrote; below 1 the target is 0
   and, at *v* = 0, the rate is effort + 3 × `decay[0]`; from 1 the target is effort − 1. No pumps, no decay, no end
   of its own.
4. **Action `0x1c`**: `Human_EndWorkout` (`0x0022d380`: clears the state flag, stops the gait blend if it is on top,
   calls the end callback), then the exit clip 692 (`Human_PlayActionClip(h, 692, 0x2000, 0)`).

### Repetitions {#repetitions}

`Workout_CallRep` is the gait blend's wrap callback, so a repetition is each time the
**leading** cursor of the blend wraps ([Gait blend](formats/animation.md#gait-blend)): the length of a repetition is
the leading loop clip's duration ÷ the rate. Confirmed (code); confirmed (runtime):

| Station | Slow / mid / fast clip (s, read from the cursors) | Repetition, pumping every 4 updates | Every 6 updates (chin-ups) |
| --- | --- | --- | --- |
| Chin-ups | 2.167 / 1.667 / 1.000 | 37 updates (1.23 s), effort 0.94-1.0, *v* 1.8-2.0 | 62-65 updates (≈2.1 s), effort ≈0.55, *v* ≈0.6-0.8 |
| Push-ups | 2.000 / 1.500 / 0.933 | 30-31 updates (1.0 s), effort 1.0, *v* 2.0 | - |
| Sit-ups | not reached / 1.833 / 1.200 | 39-40 updates (1.3 s), effort 0.97-1.0 | - |

**The effort at runtime** (confirmed (runtime)): `Started` writes 1.5, which the first loop update caps to 1.0 (the
hold counter of 6 is still running); the enter clip (≈24 updates at chin-ups) changes nothing. Without pumps the
effort falls 0.03 per update to 1/3, then 0.009 per update, the rate falling with it (0.963, 0.855, ... 0.099), and
the workout ends 63 updates (2.1 s) after the loop starts, with no repetition (push-ups). A pump shows in the hold
counter on the next update.

### Parameters {#parameters}

`CfgWorkoutParams(gain, decay, hold, flag)` (`lua_CfgWorkoutParams`, `0x0035c5a0` → `Cfg_SetWorkoutParams`
`0x00234530`) copies three floats to `0x00510690` (gain), three to `0x005106a0` (decay), three integers to
`0x005106b0` (hold) and writes the tables back into the Lua tables. The fourth argument goes through
`Lua_GetOptionalBoolArg` (`0x00409318`: absent → 0, nil → 0, else 1 when the number is not 0), so the 1.8 the scripts
pass becomes **1.0** at `0x005106ac` and the timing factor never applies. Confirmed (code); confirmed (runtime): the
decay was exactly 0.03 per update whatever the clip time. `HuSetWorkoutParams` (`0x002345d8`) fills three values at
`0x00510680` that `Human_UpdateWorkout` does not read.

## The heavy bag {#heavy-bag}

No `HuWorkout`: the player fights the bag human with his own attacks. Confirmed (code) unless marked:

- **`workout.Setup`** (station 1) sets `HUDSetTutorialCallback("workout.lCombos")`, so every hit player 1 lands or
  has blocked calls `workout.lCombos(animId)` ([Tutorial callback](hud.md#tutorial-callback)).
- **`CalloutCombos01`**: `ActivateWarchief`, `StartHUD`, `landed` = 0, `CfgPlayerCombatWalkOnly(1)`,
  `HuLockPadMovement(player, true)`, the stopwatch ([HUD](#hud)), then `CalloutCombos02`.
- **`CalloutCombos02`** picks `combo` = `random(1, 6)`; if it equals the previous one, the next (6 wraps to 1). It
  announces the combo (`HUDSetAnnounceMsg(5, LEVEL95.CS_n, true)`: the buttons drawn in the middle of the screen) and
  sets its list and a position of 1:

  | Combo | Hits (anim ids) | Buttons |
  | --- | --- | --- |
  | 1 | `S1` 12, `SS2` 16 | square, square |
  | 2 | `S1` 12, `SS2` 16, then `SSS3` 19 or `SSS3_HOLD` 20 | square ×3 |
  | 3 | `S1` 12, `SX2` 15 | square, cross |
  | 4 | `S1` 12, `SS2` 16, then `SSX3` 17 or `SSX3_HOLD` 18 | square, square, cross |
  | 5 | `X1` 11, `XX2` 13 | cross, cross |
  | 6 | `X1` 11, `XS2` 14 | cross, square |

- **`lCombos(anim)`**: returns at once after a time-out. Picks the camera by the player's octant round the bag
  (`GetOct`: by the signs of the player's x and y less the bag's and which is larger): octants 1-2 `cam`, 4 `cam2`,
  6-7 `cam3`, 3, 5 and 8 keep the current one. A hit equal to the list's entry at the position moves the position on;
  any other hit resets it to 1 (and is not tested again as a first hit). For combos 2 and 4 the third hit may be
  either of the last two entries. A completed list: `landed` + 1, `menu_spray`, the counter panel, `Check01`, position
  back to 1.
- **`Check01`**: when `landed` ≥ `repTable[rankPassed]`: tutorial callback off, announcement cleared, `DelayedWin`,
  the quit handler cleared, `Quit01` 500 ms later (`UM_Unlock(Level, 1, unlockOffset + rankPassed - 1)`, objectives
  redrawn, `ForceQuit`). Otherwise the next combo is called out 100 ms later.
- **The clock**: `W_SetStopWatch(timeTable[rank] × 1000, 0, "workout.WorkoutTimeOut")`, shown with a label. At 0,
  without `DelayedWin`: `TimedOut`, the quit handler cleared, and for the bag `WorkoutTimeOut2` 600 ms later
  (`ForceQuit`); for a cycle game `HuStopWorkout(player)`. Damage to the player does the same.
- **Triangle** quits through `fWorkout[1]`'s message-0 handler (`workout.ForceQuit`).

Confirmed (runtime): with triangle pressed beside the bag the game started (locked camera, the clock counting down from
0:26, the counter `0/10`, the called-out buttons over the bag); square pressed every 9 updates gave the tutorial
callback anim ids 12, 16, then 19 or 20, about every 11 updates, three per chain; with the combo called out being
`X1`, `XS2`, no combo was landed.

### Boxer {#boxer}

A Warrior at the bag spars with it through `GoalBoxer` ([Boxer goal](ai-goals.md#goal-boxer)): its brain's attack
weights `+0x208` point at the table `0x00511120` (45 bytes, one per [attack kind](ai.md#attack-kinds)): kinds 1, 3, 7,
24, 31, 39 and 42 weigh 5 and kind 5 (`SS2`) weighs 10; every other kind 0. Confirmed (code). That AI goal is all the
table serves: the player's workout does not read it.

## HUD {#hud}

Confirmed (code), the layout confirmed (runtime) at the bag:

- **The counter**: `HUDGetNewPH(2, LEVEL95.WLABEL[type])` (a counter panel of kind 2, label then `value/max`,
  [HUD](hud.md#fn-hud-bars-panels)), its maximum `repTable[rank]` and its value the repetitions or combos landed.
- **The clock** when `timeTable[rank]` > 0 (not rank 1 of the cycle games): the stopwatch with a label.
- **The cross icon** for the cycle games: `HUDTurnOnActionCycleAnim(4, 0.1, 6, 79, 79)`, the cross button blinking where
  the action prompt stands, from `Started` to `CycleQuit`.
- **The called-out combo** for the bag: the centred announcement.
- **No effort meter.** Nothing on the HUD shows the effort: the player sees it as the loop clip's speed (slow, mid,
  fast, then slowing down).
- The prompt beside the station: the station's name, a line of text and `Rank: n/10`; while working out, the
  station's quit hint.

## Ranks and what they give {#ranks}

`worksetup.BuildWorkoutMessage(type, menu)` (both in `level95.lua` and `level95_clubhouse.lua`) counts the station's
unlocked records of type 10 from its offset (11, 31, 41, 51): *r* = the number of consecutive data ids `offset`,
`offset + 1`, ... unlocked (at most 10), capped at `checkpoint` + 1. The data id `offset` itself is unlocked from the
start (records with level 0), so *r* ≥ 1 is the rank being attempted and *r* − 1 the ranks passed (`Rank: r−1/10`).
When *r* = `checkpoint` + 1 the station is tired for this chapter (`NoWorkout`, the rank in grey); rank 10 with the
station's last record (74, 72, 73, 71 for stations 1-4) unlocked shows `10/10`; ranks 5 and 10 show in the bonus
colour. Confirmed (code).

Passing rank *r* unlocks item `offset + r − 1` of level 95, group 1, whose record (type 10, `global.lua`) has data
`offset + r` (rank 10's items 20, 40, 50, 60 give data 74, 72, 73, 71). So the hub's chapter limits the rank: at
checkpoint *c*, ranks up to *c*. The rank is kept as an unlock and saved with the others
([Unlockables](player-state.md#unlockables)). No engine code reads type-10 records (every caller of
`Unlocks_IsDataUnlocked`, `0x00424130`, asks for another type): the ranks give nothing by themselves. Confirmed (code).

**The rewards** (`workout.CheckUnlockables`, run by every `CleanUp`), confirmed (code):

- All four stations past rank 10 (data 71-74) and type 6 data 12 not yet unlocked: `UM_Unlock(95, 1, 10)`, which
  unlocks **type 6 data 12, the stamina maximum × 1.2** ([Unlockables](player-state.md#unlockables)), announced with
  `LEVEL95.FEATEXP[20]`.
- All four past rank 5 (data 16, 36, 46, 56) and data 11 not yet unlocked: `UM_Unlock(95, 1, 9)`, **type 6 data 11,
  the power meter's maximum × 1.1**, announced with `FEATEXP[15]`.
- `worksetup.DisplayUnlocked` shows `FEATEXP[1]`, then 2.5 s later the reward's text.

## The end {#the-end}

`workout.ForceQuit(h)`: `CfgPlayerCombatWalkOnly(0)`, `HUDReleasePH`, the quit handler cleared,
`HuLockPadMovement(player, false)`, `DeactivateWarchief`, `ForceShowPlayerHud(0, false)`, `HidePlayerHud`,
`CameraMakeActive(MainCam, 0)`, the stopwatch stopped and hidden, then `CleanUp`: the player's damage handler cleared,
`HuSetPushable(player, true)`, the tutorial callback off, `CheckUnlockables`, `StartTimeOut` flushed, the workout
cameras deleted, the announcement cleared, `worksetup.FinishWorkout` (the station's prompt rebuilt with the new rank,
`ActivateWarchief`) and `workout` = nil. Confirmed (code). At runtime the `workout-end` hook was called from
`0x00255c98` (the `0x1c` branch of `Human_UpdateWorkout`) on the update of the fifth repetition.

## Coney {#coney}

**Why triangle at the bag starts nothing** (confirmed (runtime), Coney release build of 2026-10-10, `--play-level
level95 --checkpoint 2`, the player put beside the bag after the opening tutorial and triangle tapped, event log):
the context action works (`worksetup.StartWorkout(277, player)` runs and the hint shows), and so do the loading and
`workout.Setup`. What fails is step 2's **`GoalMoveToFlag(player, fWorkout[1], ..., faceFlag = true)`**: the player
never turns (heading unchanged for 150 updates), so `MoveToFlagGoal::process` (`src/ai/move_to_flag_goal.cpp`) keeps
queueing the face-the-flag turn, never arrives and never sends message 8; `worksetup.DoWorkout` never runs, and
5 s later `worksetup.TimeOut` quits (`workout.ForceQuit`, the prompt rebuilt). `HuWorkout` and the bag's
`CalloutCombos01` are never reached. The same call with `faceFlag = false` (run from the debug menu's Lua console)
sent message 8 on the next update. In the original the player walks there and turns (action code 3, 12 updates).
Every cycle station fails the same way.

**The cause, found and fixed** (Coney, 2026-10-11): `DeactivateWarchief` sets the player's brain dead, so the pad is
given up and the goal runs, but the player stands in the fight stance locked on a foe (the bag, or any fighter near),
and Coney's fight-stance walk (`Human::combatWalk`) turned every human it holds to face the lock target each update,
whatever the brain asked. The turn action was overridden on every update; once it gave up, the goal queued another.
In the original a human no pad drives (per-player `+0x1b` = 0) is moved by `Human_UpdateControl`, which turns it
toward the brain's heading: by the fight stance's AI word (24° an update) when asked to move, with a turn on the spot
when stopped more than 15° off, not at all within 15° ([AI: Speed control and Turning](ai.md#ai-turn)). Coney now
turns a player whose pad a script took that way (the turn on the spot at the standing step, as for every AI, a
stand-in for the turn clips), so the goal's turn steers and it arrives (`repo:tests/ai/move_action_test.cpp`, and on
the disc `repo:tests/platform/disc_hub_workout_test.cpp`). AI fighters keep facing their foe in the stance, as their
stance walk strafes ([AI: The move action](ai.md#move-action)). The headings were not the cause: the goal and
`TeleportToFlag` both read a flag's degrees as Coney's heading (the turn θ about `+z`, [Flags](flags.md#position)).

**The cycle games in Coney** (2026-10-11): `HuWorkout` follows [The engine side](#engine). A workout begins once all
five clips load (the level's clip source, by the names the script gives) and, for the player who is his own target,
at any distance; the enter clip plays, then a gait blend over the slow, mid and fast clips at 16 units a second whose
target and rate `human::WorkoutEffort` sets each update from the station's `CfgWorkoutParams` tables (the pumps, the
hold counter, the 0.03 decay and its 0.3 share below a third, the end on triangle or when worn down). Each wrap of the
blend's leading clip calls the repetition callback; the end clears the flag, calls the end callback and plays the exit
clip. `repo:tests/human/workout_test.cpp` checks the rules (the run-down ends 58-68 updates into the loop, the
runtime's 63) and `repo:tests/platform/disc_hub_workout_test.cpp` plays each cycle station through the pad: five
repetitions at a press every 4 updates, each gap within 3 updates of the runtime's or a little below (Coney's 33-34
at the chin-ups, 28 at the push-ups and 36 at the sit-ups against the runtime's 37, 30-31 and 39-40: an open question
below), the rank's unlock and the prompt back with `Rank: 1/10`; at the chin-ups also a
run-down with no repetition ending 58-68 updates into the loop, and triangle ending the loop on the update it is
pressed. **Stand-ins and choices**:

- Another human's equipment is reached within 1.5 m (the start clip's attack-table reach is not read).
- The blend starts at value 0.
- `Human_AlignToObject` turns a human only toward another target.
- Coney applies a script's teleport of player 1 at the end of the frame, after the workout may have begun, so every
  spawn of a human that is working out keeps its clips.
- The update runs from the loop's first update on, whether or not the loop is on top: `Human_EndWorkout` stops the
  blend only if it is on top, so a clip played over it (a hit reaction, a script's clip) leaves the workout running;
  meanwhile *v* is the loop's last value and only the effort, the pumps and the end run, so triangle still ends it
  (inferred: the code that skips the update during the enter clip is not on this page).
- Not done: the start's push sphere off (Coney's bodies push nothing aside yet) and the held flag `0x2000` on the
  start's chain and on the exit clip (`Human_PlayActionClip(h, 692, 0x2000, 0)`; in Coney's task flags `0x2000` is
  the duck's counter phase, so the bit's meaning here needs reading first); the exit clip fades in at the combat
  fade.

## Notes for implementers

**Per station:**

- **All**: make `GoalMoveToFlag` with `faceFlag` arrive for the player (done, [Coney](#coney)); keep `TeleportToFlag`
  exact; `HuWorkout`'s target is the player himself, so drop the 1.5 m reach stand-in (the reach test passes at 0).
  `CfgWorkoutParams`' fourth argument is a flag (non-zero → 1.0), so the timing factor is always 1.
- **Chin-ups, push-ups, sit-ups** (done, [Coney](#coney); `Human_UpdateWorkout` as in [The engine side](#engine)):
  play the enter clip 691 then a gait blend over 693-695 (speed 16 units/s, unrounded target 3 × effort − 1, or 0
  below an effort of 1/3);
  effort starts at 1 (the script's 1.5 is capped) with a hold counter of 6; a cross press sets the counter to
  `hold[L]` and adds `gain[L]` on that many updates; otherwise decay 0.03 (× 0.3 below 1/3); the rate is 1, or
  3 × effort + 2 × decay once the blend is at 0; end on triangle or when that rate fell to 3 × decay; a repetition is
  each wrap of the blend's leading clip (call the `DoRep` callback then), not a fixed 1.5 s; the exit clip 692 at the
  end. Use the station's own tables (above). The level index is `floor(v + 0.5)` of the blend's value.
- **Heavy bag**: no `HuWorkout`; the bag human must take hits so that `HUDSetTutorialCallback`'s function gets each
  hit's anim id (already in Coney); `CfgPlayerCombatWalkOnly(1)` and `HuLockPadMovement` keep the player at the bag;
  the three cameras switch by octant in the script; the stopwatch must call `workout.WorkoutTimeOut` at 0.
- **HUD**: the counter panel (kind 2), the stopwatch, the blinking cross icon, the centred combo call-out; no meter.
- **Ranks and rewards**: `UM_Unlock(95, 1, offset + rank − 1)` must reach the unlock records and survive a save; the
  power (× 1.1) and stamina (× 1.2) upgrades of type 6 data 11 and 12 must be read where the maxima are computed.

**Playthrough tests** (pad-driven, [Testing](../guides/testing.md)): from `level95` checkpoint 2 on the player's path,
walk to each station with the stick at a partial deflection, then:

1. **Start**: tap triangle beside `fStartWorkout[k]`; assert `worksetup.StartWorkout`, message 8 at `fWorkout[k]`
   within 2 s, `workout.StartWorkout`, and for k = 2-4 `HuWorkout` and the `Started` callback (the cross icon on); for
   k = 1 `CalloutCombos01`, the clock at 26 s and the counter at 0/10.
2. **Pump** (k = 2-4): tap cross every 4 updates; assert five `DoRep` callbacks, 30-40 updates apart, the counter
   reaching 5/5, `HuStopWorkout`, the exit clip, and the rank in the rebuilt prompt one higher (`UM_Unlock(95, 1,
   offset)`).
3. **Let it run down** (k = 2-4): no presses after the start; assert no `DoRep` and the end about 63 updates after the
   loop starts, the rank unchanged.
4. **Quit**: triangle during the loop (k = 2-4) or at the bag; assert `CycleQuit` / `ForceQuit`, the follow camera back,
   pad movement unlocked, the prompt back.
5. **Bag combos**: read the called-out combo from the announcement, press its buttons; assert the counter rising by 1
   per landed combo and, at 10 within 26 s, `UM_Unlock(95, 1, 11)`; and a time-out without a win.
6. **Rewards**: with the type-10 records of ranks 5 (or 10) of all four stations unlocked, finish any workout and
   assert `UM_Unlock(95, 1, 9)` (or 10) and the power (stamina) maximum raised.

## Open questions

- Why Coney's repetitions, pumped every 4 updates, come about a tenth sooner than the original's (33-34, 28 and 36
  updates against 37, 30-31 and 39-40 at the chin-ups, push-ups and sit-ups). The ratio is close to 0.9, the rate
  `CfgAnimSpeeds` gives clips with range flag `0x2000` ([Animation: Playback rate](formats/animation.md#playback-rate)),
  so the loop may play at that rate (speculative); a runtime read of the blend task's rate `+0x0c` during a workout
  would settle it.

- The slow clip of the sit-ups (not reached at runtime) and the enter and exit clips' lengths: read them from the clip
  files.
- `Human_AlignToObject` with the human as its own target (a zero direction): what it does to the heading (the player
  is already on the flag, facing its heading).
