# Rumble mode

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the compiled Lua chunks
of the retail disc. One runtime observation: [the intro](#intro).

## Purpose

What happens once a Rumble arena is loaded: the arena script's life cycle, the fighters it creates, each game type's
rules (win, lose, timers, scores, revivals), the intro and countdown, the win camera, the result screen and the way
back to the menus. The menus that choose the set-up, their records and the 23 set-up values are on
[Front end: the Rumble set-up](frontend.md#rumble-setup); the modes, arenas, gangs and characters as reference lists are
in the [Rumble roster](../references/rumble.md); every arena's flags are in [World flags](../references/flags.md).

Most of Rumble is **Lua**: the arena script (`level101.lua`-`level137.lua`, the same functions in each), the arena's
flag chunk for the game type (`level<n>_<type>_init.lua`) and the game type's rules chunk (`brawl.lua`,
`kinghill.lua`, ...). Coney runs these scripts as they are, so an implementer needs the **bindings** they call
([below](#bindings)) and the C++ pieces they open: the [intro](#intro), the [win camera](#win-camera), the
[result screen](#result-screen) and [`HuSwitchPlayer`](#switch-player). The Lua claims below are inferred from the
chunks' disassembly (the method of [Scripts](scripting.md)); the C++ claims are confirmed (code) at the cited
addresses.

## The arena script {#arena-script}

Loading `level<n>` runs `Main` → `ConfigRumble`, as in [From QUICK RUMBLE to an arena fight](frontend.md#quick-rumble).
`ConfigRumble` names the rules chunk by `RumbleInfo[gameType]`: `brawl` for `RM_Brawl` (1), `RM_Brawl1` (12) and
`RM_Brawl5` (14); `kinghill` 2; `royal` 3; `survival` 9; `run4life` 10; `hifi` 11; `tagbt` 19; `mercy` 23; `wchair` 24.
It builds the `RUMBLE` string table (rules lines by type, the win, draw and lose words, score labels) in five
languages and keeps `GetLanguage`'s. Then `doFile("level<n>_<type>_init")`, `doFile(<rules>)`, the stand-in human and
camera, `SetSpawnMax(30)` and, when gangs were chosen, `SetGameMode(gameMode, 3, 19, gangSize)` and the pack
precache; the start callback is `DoRules`.

**`DoRules`** (the start callback, after the level is loaded): the rules chunk's `StartRumble` (creates the sides,
below), `CfgMultiplayerJoin(false)`, object zones 21-32 off and zone `gameType` on (`ObjEnableZone`), the rules line
as objective 2 (`HUDSetObjective(2, RUMBLE.RULES[gameType], 0, true)`), `HideHud`, `CamSetFollowAngle(25)`,
`GangMakeEnemies(gang1, gang2)`, then `ShowRumbleModeIntro("FinishCountdown", {gang1Name, gang2Name, "", ...})`.

**`FinishCountdown`** (when the intro ends): camera 0 on, `RestoreHud`, every fighter's brain on (`BrDead(h, false)`),
gang 1's death and arrest handlers (`GangSetMsgHandler` 18 `CheckPlayerDead`, 17 `CheckPlayerArrested`; gang 2's too
in versus), music, then the rules chunk's **`StartIt`**. The music is `CfgLevelSpecificMusic` or
`CfgModeSpecificMusic` when the level or rules chunk defines one (only `survival.lua` does), else a random track of
`Music[k]`. The script's choice of `k` compares `gameType` with the string `RumbleInfo[RM_Brawl]`, which never matches,
and then tests `RumbleInfo[RM_Koth]`, which is always set, so `k` is always **1** (the 11-track list).

### The sides {#sides}

`AddRumbleGang1` makes `GangCreate(3, "Gang1", 1)` and `AddRumbleGang2` `GangCreate(19, "Gang2", 1)`. Each member is
`HuCreate("P1<i>", gang1[i], FlagPos(fP1[i]), 270, nil, pad, gang, true)` (side 2: `"P2<i>"`, `gang2[i]`, `fP2[i]`,
heading 90), teleported onto its flag with the flag's heading (`TeleportToFlag(h, flag, -1)`), for `i` = 1 to
`gangSize`. The pad argument: side 1's first member is player 1 (`Rumble.player1`); in co-op (`gameMode` 2) its second
member is player 2; in versus (`gameMode` 1) side 2's first member is player 2; everyone else is AI (pad 0). Every
member then goes through `RumbleSpawnerCallbackGang1`/`2`: actions and goals flushed, brain off, not revivable, no
money, no pocket item, carried item `"none"`; the players are also tireless and unstunnable, with their HUD panel
forced on (`ForceShowPlayerHud`). The brains stay off until `FinishCountdown`.

The stand-in (`AddDummyPlayer`: `GangCreate(0, "Gang0", 1)`, character 352 at `fP1[1]`) and its camera are deleted by
`AddStandardCameras` once the sides exist. That camera function makes a follow camera on player 1 (or the level's
`CfgLevelSpecificCams` fixed camera), turns on cameras 3 and 4, and in two-player games targets player 2 too
(`CamTarget(0, MainCam, player2)`).

### A player goes down {#player-down}

`CheckPlayerDead(member, attacker, standing)` (gang event 18, [AI: gang events](ai-gangs.md#gang-events)), for player 1
(and the same for player 2 with its own side):

- one player or versus, one fighter a side, not Survival: revive in **4 s** (Wheelchair: **1 s**) with
  `ScheduleFuncArg1("RevivePlayer", h, ms)` (`HuRevive` if still a player);
- one player or versus with a gang: when `standing` > 0, hand the pad to a team-mate (`HuSwitchPlayer`); co-op: when
  `standing` > 1;
- a new player becomes `Rumble.player1`/`2` (Royal: `HuSetPreventRage`), goes through the spawner callback, gets its
  brain back and a 2D cue (`mission_update_21`, player 2 `_22`).

`CheckPlayerArrested` (event 17, arrested) does the same with a **15 s** release (`HuSetArrested(h, 0)`) for one
fighter a side, else a switch.

### The end of a match {#match-end}

The rules chunk calls **`RumbleOver(side, flag)`**: side 1 or 2 won, 3 a draw (a global `bDraw` set by a draw check
also forces 3). It cancels pending revivals, turns every fighter's brain off and clears both gangs' tactics, moves the
winner onto `flag` (or `fWin[side]` when none is given and the rules chunk made `fWin`), makes the
[win camera](#win-camera) on the winning player (side 2 in one-player games: its first standing member), sets the win
line, plays one of six announcer `dj_win` lines (not on a draw), hides both radars and the player panels, and
schedules **`GoToMenu`** after **3 s**.

`CreateWinCam(h)`: camera 3 off; the winner's movement and pad locked; every other standing non-player fighter
teleported away and deleted; the winner's brain off, normal mode, a cheer animation (`wheelchair_cheer.anm` in
Wheelchair, else `combat_fidget_boxersdance.anm` and sound command 143); screen effect 0 for 1 s; in co-op player 2's
brain off; `CamSetFollowHeading(180)`; after 200 ms `HideHud`; then the win camera is made active, except in one-player
Survival, which uses `CamUseDeathCamera(h, 7000, 100000)`. The music stops.

`GoToMenu`: `RestoreHud`, player 1's brain on, the menu loop `echoes_in_my_loop`, then
`HUDLaunchRumbleWin(winner, reason)`: `winner` is the winning gang's name in big font (a space on a draw), `reason`
the `RUMBLE` draw word, the win word (Wheelchair: its own), or in one-player Survival the survival time.

## The game types {#game-types}

What each rules chunk adds. Health, god mode and such are per fighter of both sides unless said.

| Type | Win | Lose / draw | Notes |
| --- | --- | --- | --- |
| **Brawl** (`brawl`; 1, 12, 14) | the other side's `numleft` reaches 0 while yours has a standing member | both sides at 0: draw | max health 2500 with one a side, 1000 otherwise; not tireless |
| **King of the hill** (`kinghill`, 2) | first gang to **100** points | equal best scores: draw | a point per tick on top |
| **Battle royal** (`royal`, 3) | the other side all rung out | nobody left in the ring: draw | everyone god-mode; only a ring-out kills |
| **Survival** (`survival`, 9) | outlast the other player (versus) | both down: draw; one player: the time is the result | endless spawned enemies |
| **Tag battle** (`tagbt`, 19) | first to finish the 5 layers of its tag | | spray cans spawn one at a time |
| **Mercy** (`mercy`, 23) | Mercy reaches your side's flag | | versus only; players god-mode |
| **Wheelchair** (`wchair`, 24) | cross the finish line first with at least the other's score | | a checkpoint race |

**Brawl.** `StartRumble`: both sides with a per-member death handler (`SetMsgHandler(h, 18, "StandardGang1Dead")`),
the Red Devil band when the arena has `fRedDevilBand` (level 105), standard cameras, radar 0 off (and 1 in two-player
games), the counters `Rumble.G1.numleft` / `G2.numleft` = `GangGetHeadCount`, and, with more than one a side, the
member-count indicators (`HUDSetNumIndicator`: one player or co-op, player 0's for gang 1 and the shared one, 2, for
gang 2; versus, player 0's and player 1's). Every member also gets message 2 → `CheckForDraw` (draw when
neither gang has a standing member). The arena's `DelayTactic1`-`4` run at 250, 500, 750 and 1000 ms when defined
(level 110). `StandardGang1Dead` decrements `numleft` (`StandardGang1Lives`, message 19, increments it again), updates
the counter panel when one is set, and at 0 ends the match: `RumbleOver(2, CamFlagToUse(player2))` when gang 2 still
has a standing member, else a draw. `StartIt`: with one a side, the War Commands are locked and disabled for each
player, automatic switching off and the command display hidden; `WCIssueCommand(player, 1, true)`; gang 2 the enemy of
gang 1; outside versus, gang 2's AI is **`TacticConfront(gang2, gang1, 10, 2, 0, -1, "AIConfrontCallback")`**, whose
callback switches to `TacticAttack(gang2)` on results 1, 5 or 6 ([AI: tactic kinds](ai-gangs.md#tactic-kinds)). In versus
there is no AI.

**How a brawl ends** (inferred from `brawl.lua` and `level102.lua`). `AddBrawlGang1` / `2` set the per-member message
18 handler (`StandardGang1Dead` / `2Dead`) only on members for which `HuIsAlive(h)` returns non-nil; at that point
(before the intro, brains off, nobody down) that is every member. Only these handlers end a brawl: side 1's last
member down → `RumbleOver(2, ...)`, side 2's → `RumbleOver(1, ...)`, `RumbleOver(3)` when the other side has nobody
standing either. They run before the gang's `CheckPlayerDead` ([AI: gang events](ai-gangs.md#gang-events)), so the
player's 4 s revival only fires during the win sequence. A CPU fighter is never revived: `CheckPlayerDead` handles
only `Rumble.player1` / `player2`, gang 2 gets it only in versus, and every fighter is made not revivable.

**King of the hill.** The arena has `fTopTier`, the box `vTopTier` and a referee (character 350, `civl_hl_dj1`).
`StartRumble`: the two sides, the referee, `fWin[1..2]` made at the top flag, the box's enter/leave handlers (messages
3 and 4) counting each gang's members on top, `X.Update` first after **4 s**, the top flag on the radar, a split
camera mode (`CamSetSplitMode(1)`), a goal glow (`dyn_w_mission`) at the top, the scoreboard (`HUDEnableTextProgress`
with both gang names, "(1)"/"(2)" added when equal). `StartIt`: both gangs `TacticDomination(gang, fTopTier, 3)`; all
fighters god-mode, no rage, not tireless, max health 1000, reticule on; an air horn. **`X.Update`** every **1,850 ms**:
a gang with a member on top and a standing member scores 1, except that in versus a side scores only while its
**player** is inside `vTopTier`; the leader gets the spinning `dyn_crown` icon; the first gang at 100 (`TIME_ON_TOP`)
ends it: everyone's movement locked, an air horn, `X.GameOver` 4 s later, which puts both sides back on `fP1[1]` /
`fP2[1]` and calls `RumbleOver`. `GameOver` passes the winning **gang id** compared with 1 and 2, so it relies on
`Gang1` and `Gang2` taking gang slots 1 and 2 (the stand-in in slot 0; inferred, see [open questions](#open-questions)).
The five-minute stopwatch (`X.SetupStopWatch`, `TIME_LIMIT` 300,000 ms) and the extra gangs `NP1`-`NP4` are defined but
never started.

**After `GameOver`.** `vTopTier` keeps running: nothing disables it or clears its handlers, the teleport does not
touch its occupant list, and `W_GameState + 0x14c` stays 0 (none of the bindings `GameOver` and `RumbleOver` call
ends a level; `GoToMenu` runs 3 s later). So on the box's next update every fighter `GameOver` teleported off the
top, alive or not, gets **message 4** and `X.OffTopTier` runs for it: `HuGetGang` gives its gang, still one of the
two keys of `X.tKingHill` (`X.CleanupGangs` removes the gangs only 7 s later), and that gang's `NumOnTop` drops by
one; the scores are already final, so nothing comes of it. The fighters `CreateWinCam` deletes (`HuDelete`) also
leave with message 4 while their handles still resolve; once a handle no longer resolves the box skips it with no
message ([Scripts: triggers](scripting.md#triggers)). The winner, put on `fWin` at the top in the same Lua call,
never leaves. No path gives `OffTopTier` a human whose gang is not a key, so the original raises no nil-index
error here. Confirmed (code) at `0x00415378`, `0x00385db0`, `0x00235438` and from `kinghill.lua`'s and the arena's
calls; that no binding they call sets `+0x14c` is inferred.

**Battle royal.** The arena has the ring box `vRing`, the top flag `fTop` and fixed cameras with their boxes.
`StartRumble` adds the death handlers and indicators as Brawl, `SetMsgHandler(vRing, 4, "OutOfRing")` and `fWin[1..2]`
at `fTop`. `StartIt`: both gangs `TacticDomination(gang, fTop, 40)`; War Commands off; every fighter god-mode, no rage,
not tireless, not revivable; `FallCheck` every second, which after about **7 s** without a ring-out switches both gangs
to `TacticAttack` until someone is hit (message 1 → back to domination). **`OutOfRing(box, h)`**: the draw check (nobody
left in the ring), god mode off, sound command 40, `HuKill` after **3 s**. `StartRumble` registers `PlayerState` on
every fighter's `ANIM_MOVEMENT_FALL` (`AddAnimCallback`): a plain fall or fall start clears the global `fall`, a long
fall sets it. While `fall` is set (it starts set) and the faller is a player or within 7 m of the opposing player, a
slow-motion shot (`HuSetSlowMo(0.2)`, a fixed camera chosen by which side boxes hold the faller, pads locked) ends after
**1.2 s**. With `fall` clear the kill comes after 200 ms, `fall` is set again, and a falling player's pad passes to a
team-mate when the gang has more than one member.

**Survival.** The arena defines two spawners `ENEMY` / `ENEMY01` (`GangAddSpawner`, callback `CreateENEMY`).
`StartRumble`: side 1; in versus side 2 too, the enemies being the arena's `CharGenSpawner1` list; in one-player games
the enemies are the chosen side 2's nine types, `gangSize` is set to 9 and side 2 is not created. A death handler on
each player (`SavePlayerStats`). `StartIt`: not tireless, not revivable, War Commands off, both spawners, the enemy gang
hostile to the players, the stopwatch counting up and shown (one player only), a seconds counter. `CreateENEMY`
(each spawn): death handler `EnemyDown` (a kill for the player who downed it), no items, brain type 2, police types
(259, 262, 261, 263, 264, 253, 254, 256, 257) with attack weights 41 → 0, 39 and 38 → 50; it goes for a player still
up (a random one when both are). **`SavePlayerStats`**: records the time; when a player is down the match ends: both
down in versus → draw; versus → the other player wins; one player → side 1 "wins" and the result line is the time. The
spawners then stop and the enemies hang out at `fP1[1]`.

**Tag battle.** The arena has `fWarrTag1` / `fWarrTag2` (each side's tag spot, five tag objects each), `fPaint` (can
spawn points) and crowds. `StartRumble`: the sides (one player: side 2 is a scripted painter gang, `GangSetup` with a
strategy machine), revivable, no mugging; the tag spots get an interaction prompt (`SetMsgHandlerEx(flag, 0,
"WarrTag1Start", GSTRING.HUD[18])`) and a glow; per-gang tag patterns and colours; the progress bars (two
`HUDEnableGenBar` in one-player games, `HUDEnableBar(3, ...)` in versus). `StartIt`: max health 750,
`CfgTagStartCallback`, `CfgHuInventoryCallback("PickUpSpray")`, the first can after **5 s**. One spray can is out at a
time, at a random `fPaint` flag other than the last; picking it up (inventory item 3) re-arms the tag spot and the next
can comes 5 s later. Tagging at your spot (`HuTag`) completes one layer (message 14 with success): the bar moves to
`(layer − 1) / 5`, the side's crowd cheers (`TacticTrigger`), and after the fifth `Player1Wins` / `Player2Wins` →
`RumbleOver(side, fWin[side])`. In one-player games the CPU painter revives **14 s** after going down.

**Mercy.** Versus only. Mercy (character 221, gang type 17) stands at `fMercy`, god-mode, smoking. Trigger spheres of
radius 2.2 at `P1Box` and `P2Box` with eight glows each (`dyn_w_goto` side 1, `dyn_w_mission` side 2). Players are
god-mode, tireless and rage-free; a player hit while Mercy is grabbed lets go (`HuSetNormalMode`). Mercy entering a
box ends the match for that box's side. Hitting Mercy makes her cower.

**Wheelchair.** Preset characters 458 / 459 (no gang choice; the names become the two `RUMBLE` wheelchair labels).
Player 2 is the CPU in one-player games, driving the arena path `WheelChairRace` (`GoalTravelPath`; its fifth argument 2
at the start, then 4 in level index 43, else 5). Both are god-mode, pushable, wheelchair-controlled
(`HuSetWheelchairControl`); a hit locks the victim for 2.1 s. Each side has a glow at the next checkpoint of `fRace`
(radius 5.5); reaching it moves the glow on and adds 1 to the score; one short of the lap the finish line
(`tFinishLine`, `vFinishLine`) flickers, red, yellow or white by who leads, and the first player into `vFinishLine` with
a score at least the other's wins (`RumbleOver(side, last_flag)`). Spectator crowds react and count knock-downs (shown,
not scored). The players drive with L1 and R1 ([Characters: wheelchair control](characters.md#wheelchair)).

The race in detail (`wchair.lua`, `SetupRaceGlows4(fRace)`; from its calls and bytecode):

- `fRace`'s n flags make a **loop**: each flag's next is the following one, the last's the first. The target
  score is n (`LAPS` = 1 is not used by this set-up).
- Both glows start at `fRace[1]`, player 1's (`dyn_w_goto`, `Carrot1`) 0.3 m to +x and −y, player 2's
  (`dyn_w_mission`, `Carrot2`) 0.3 m the other way. Each is a [trigger sphere](scripting.md#triggers) (radius 5.5,
  mode 0, period 100 ms).
- **A checkpoint**: message 3 on a glow counts only for its own player. The glow moves to the next flag (same
  offset), a bleep plays, the side's score goes up by 1 and the radar marks the glow. At n − 1 the finish opens
  (`FlickerFinish`) and that glow is hidden; on player 2's side this also flushes the flicker functions and stops
  the music first.
- **The finish**: the `tFinishLine` objects show every 200 ms (`FLICKER_RATE`), white while the scores are equal,
  red (255, 100, 100) while player 1 leads, yellow (255, 255, 100) while player 2 leads, and `vFinishLine`'s message
  3 runs `DeclareWinner`. Player 1 entering with a score at least player 2's wins (`RumbleOver(1, last_flag)`), and
  player 2 likewise. Before that test `DeclareWinner` always turns both radars off and locks player 1's pad (and
  player 2's when a player), so a side entering with the lower score ends nothing but leaves the pads locked
  (whether `wheelchairControl`, which reads the pad itself, honours that lock is not traced).
- Pad handlers count two buttons into `player1_L1` / `player1_R1`, which nothing reads, and one rings a bike bell.

`run4life`, `hifi`, `caps`, `car`, `shoot` and `muggr` have rules or flag chunks on the disc, but no mode record offers
them ([Rumble roster](../references/rumble.md)).

## The intro and countdown {#intro}

`ShowRumbleModeIntro(onDone, names)` (`0x0036ed48` → `0x001b5f88`) opens the HUD's **RM_Intro** screen (`0x600840 +
0xe530`): `0x001f9418` keeps `onDone` and up to 10 non-empty names; `0x001f9558` builds a text widget per name, a
separator between two names and a prompt (global string `0x25`), queues screen effect 0 and loads a sound bank (`0x001f93a0`).
The update (`0x001fad40`) runs four phases (byte `+0xe4`), confirmed (code):

1. **Names** (`0x001fa170`), one at a time: the announcer names the gang (`dj_gang_<pack + 1>`, the pack from set-up
   index 3 or 4; when that fails a random `dj_genintro_01`-`30`, different for the second name) and a synth sting
   (`rumblesynth_01` / `_02`); the name slides in and fades up over **500 ms** (`0x0050f47c`). When its voice ends, the
   separator shows with a random `dj_vs_01`-`15`; when that ends, the next name. After the last name, phase 2.
2. **Prompt** (`0x001fa740`): the prompt fades in over **1,000 ms** (`0x0050f484`); once nearly opaque the screen takes
   the pad (`0x001e93c0(+0x40, 0xf000)`) and hides the names. **Accept** (input `0x001f9d78`, event 4) plays a random
   `dj_ready_01`-`05` and moves to phase 3.
3. When that voice ends: screen effect 0 is queued and the countdown starts.
4. **Countdown** (`0x001fa9b0`): "3", "2", "1" and then global string `0x3f`, a second each, each fading from opaque to
   clear; "3" plays a sound and loads a random `dj_start_01`-`10`, which plays with the last word. After **4,000 ms**
   the screen calls the Lua function `onDone` with no arguments and closes.

**The layout** (`RM_Intro_Init`, `0x001f9558`), confirmed (code) unless marked; each point goes through
`Camera_AdjustScreenPoint` for the screen's shape:

- **The names**: multi-line text widgets in font slot 6 (`big_font`), size **2.5** (`0x0050f488`), grey `0x005fd310`
  (178, 178, 178, 255), centred, from (0.5, 0.12) with a pitch of 0.11 (`0x0050f48c`; 0.15 in one other video mode);
  `RM_Intro_LayoutNames` then places them (not traced).
- **The separator** between two names is not text: a sprite widget (word `0x1d0000`) of the intro's own sprite batch
  (`+0x60`), depth 11,000, colour `0x005fd310`. Inferred: the "VS" picture.
- **The prompt** is a **`UsageInfo`** ([GUI](gui.md#widget-classes)), not a big-font line: `UsageInfo_Setup`
  (`0x001ceb40`, centred) at (0.5, **0.88**) (`0x0050f494`: 0.88 in the data and in every video mode `RM_Intro_Init`
  sets), colour `0x005fd310`, its text global string `0x25`, then hidden until phase 2. So it is drawn in font slot 3
  (`part_page0`) at size 1.0, where the string's bare `<X>` (disc check, no `<MONEYFONT>`) is the cross-button
  glyph of `part_page0` (characters `0x91`-`0xa0`, [Markup](gui.md#markup)).
- **The countdown**: a text widget at (0.5, 0.5), size 2.5, grey `0x005fd310`, font slot 6, centred, hidden.

**Fixed in Coney** (user item 24b, "press  to start" with an empty gap): Coney drew the prompt like a name, in
`big_font`, which has no picture for the `<X>` character, so the button was a gap. The intro
(`repo:src/gui/rumble_mode_gui/rumble_intro.cpp`) now builds the prompt as a usage line (`part_page0`, size 1.0,
grey 178, centred) at (0.5, 0.88), and the names and countdown in grey 178 at size 2.5, the countdown at (0.5, 0.5).
Tests: `repo:tests/gui/rumble_mode_gui/rumble_match_screens_test.cpp` and the disc check `"[disc][text]"` (the cross
in string `0x25`).

**The world keeps running under the intro.** The screen is only a HUD widget: `HUD_Update` (`0x001af010`) updates it
(`0x001fad40`) after `Gm_Level_Update` (`0x00158728`) has already ticked the tasks and the game mode
(`TaskManager_Tick`, `GameMode_Tick`) for the frame, and nothing in that frame reads whether the intro is open. The
intro pushes no game mode and sets no freeze flag. What it does set each update is the pause menu's START cool-down
(`0x0050eddc` record `+0x20ac` = 1, [Pause](pause.md)), so START cannot pause the game while it shows; once the prompt
is up it also takes the HUD pad's buttons `0xf000`. Confirmed (code). So humans, brains and gangs update throughout.
What keeps a fighter still is only what the scripts did to him: `BrDead` alone does not, because a dead brain still runs
the goals on its stack, including those its gang's tactic gives it ([AI: handlers](ai.md#handlers)). **At runtime**
(PCSX2, QUICK RUMBLE 1 ON 1 with the defaults, the Fight Pen `level102`, read over PINE every second), confirmed
(runtime): while RM_Intro was open the game clock ran on (about 1,000 ms a second), every brain's time fields
(`+0x30`-`+0x38`) changed each sample, the fighters' bones animated, and the twelve onlookers each held a goal. Both
fighters (the player and the computer's) had `+0x09` = 1 and an **empty goal stack**, and neither moved: in a Brawl
nothing gives them a goal before the end. Within a second of the screen closing (`FinishCountdown`) both were 0, the
computer's fighter took a goal and began to walk at once, and two seconds later had a stack of three. So the fighters
stand still in the intro only because their stacks are empty; a fighter that holds a goal, from a tactic or a script,
would move during it (inferred from the code above). From the prompt's accept to the close took about 5 s (the ready
line, then the 4,000 ms countdown).

## The win camera {#win-camera}

`CameraCreateWin(name, target, fov, distance, angle, speed, height, far, direction)` (`0x00366360` → `0x0011c858`)
sets up one shared camera object, `Cam_Win` (created on first use, `0x00120188`, constructor `0x00143918`), and returns
its handle. The arenas pass `(..., 58, 3, -19, 40, 1.1, 90, 1)`. Confirmed (code) for the stores; the meaning of each
from the maths (inferred):

- **Start** (`0x001439d8`): the look-at point is the target's position raised by `height` (1.1 m); the camera is put
  `distance` (3 m) along the target's facing from it, turned by `angle` (−19°) about the vertical.
- **Each frame** (`0x00143c78`): the camera turns about the look-at point by `direction × speed × dt` (40°/s), while
  its "orbiting" flag (`+0x1f8`) is set; a screen-effect state (`0x005fdeb8 + 0x1d8` ≥ 1 and `+0x1dc` > 0) clears it.
- `fov` (58) and `far` (90, at most 150) go to the base camera; the near plane is 0.1.

## The result screen {#result-screen}

`HUDLaunchRumbleWin(winner, reason)` (`0x0036f160` → `0x001dfe20`) pushes **game mode 0x14** (`0x00155648`) and
gives the result screen (a `GameMenu`, `0x00635390`, vtable `0x0053bee0`) the two texts. Confirmed (code).

**Mode 0x14** (`Enter` `0x0015f1a0`, `Exit` `0x0015f248`, `Update` `0x0015f308`): the world keeps updating and
drawing under the screen (the task manager ticks as in play), the screen updates and draws on top. Exit hides the
screen.

**The screen** (show `0x001e0008`, update `0x001e0778`, input `0x001e0460`):

| Part | Where | What |
| --- | --- | --- |
| winner line | (0.5, 0.35) | `winner` (global string `0xe0` when none) |
| reason line | (0.5, 0.45) | `reason` |
| usage line | y 0.6 | global string `0x1c` |
| first choice grid | y 0.55 | `0xdb` (id 0, **Replay**), `0xdc` (id 1, **more**) |
| second choice grid | y 0.55 | `0xdd` (id 2, **Rumble menu**), `0xdf` in game or `0xde` from the front end (id 3, **Quit**) |

Item scale 1.15, grey `0x005fd320`. The choices appear **6.5 s** after the screen opens (`0x0050eec0`) and fade in over
**2 s** (`0x0050eeb8`); input is taken only after that. Accept (event 4, cue 8) on id 1 swaps to the second grid; on
the others the screen closes and `0x00155648` pops the mode and acts:

- **Replay** (`+0x7a4`): checkpoint 1 and reload the arena by name (`0x00160d78(gs + 0x124)`), as the pause menu's
  Restart ([Pause](pause.md)). The set-up values at `0x0063eec0` are untouched, so the same match is set up again.
- **Rumble menu** (`+0x7a8`): level-end state 3, then Lua `PauseGoToRMIQuick()` from the front end
  (`0x0063ef64` set), else `PauseGoToRMIHangout()`.
- **Quit** (`+0x7ac`): level-end state 3; from the front end, checkpoint 1 and load `"menu"`; in game,
  `runNextMission` with one argument (0, as the pause menu's quit to the hangout).

Back is ignored. **The way back** from the arena script's side: `PauseGoToRMIQuick` / `Hangout` run
`PrepareRumbleMenu` (stop the win camera's death view, delete the extra humans, end weather and fog, pause sounds,
music at half volume, the menu track, mute the fighters, stop a subway train, camera 3 off, delete both gangs) and
then `ShowRumbleModeInterface("ExitToMenu" or "ExitToClubhouse", "PlayRumbleModeLevel", 1 or 0)`: the Rumble menu
(mode 0x11, [Front end](frontend.md#rumble-screens)) over the arena; starting loads `level<n>` (`PlayRumbleModeLevel`,
`MenuLoadLevel`), cancelling loads `"menu"` (`ExitToMenu`) or `runNextMission(1)` (`ExitToClubhouse`). The pause menu
reaches the same two functions ([Pause](pause.md)).

## HuSwitchPlayer {#switch-player}

`HuSwitchPlayer(h)` (`0x0035d218` → `0x0041a8c0`), confirmed (code): when `h` is a player (`+0x1b0` ≠ −1), its gang
(brain `+0x20c`) picks a member with `0x0022a770(gang, 1)`: a non-player in the 16 member slots that is not down,
preferring the lowest non-zero priority byte `+0x1b1`, else the first found; with none, outside `level99` and when
`W_GameState + 0x158` is 0, any such human of a type-0 gang. The old human loses the player (`0x0022a2a8(h, 0, 1)`),
the new one becomes that player on the same pad (player record `+0x19`, human `+0x380`), and a camera that followed
the old one follows the new (`0x00122248`, `0x00122438`, `0x001222b0`). Returns the new handle, or the null handle.

## Bindings the Rumble scripts call that Coney lacks {#bindings}

Status from the [binding reference](../references/bindings/index.md) (`Coney: not implemented` or partial; branches in
flight may have some). Each links to its reference entry by name.

- **Every arena** (`level1xx.lua`, 62): `CamUseDeathCamera`, `End3DFog`, `EndFog`, `EndRain`, `EndRoomSmoke`; partial
  `ShowRumbleModeInterface`.
- **Brawl** (with the Fight Pen's flag chunk): `EndGarbage`.
- **King of the hill**: none.
- **Battle royal**: none.
- **Survival**: none.
- **Tag battle**: `CfgHuInventoryCallback`, `CfgTagSettings`, `GoalTag`, `HUDEnableBar`, `HUDEnableGenBar`,
  `HUDSetBarPercentage`, `HUDSetRadarZoomScale`, `HuSetMug`, `HuTag`.
- **Mercy**: `GoalGrabTarget`.
- **Wheelchair**: none.
- **Other arenas' set dressing**: `CfgSteam`, `GetPTank`, `ObjSetTrainPoint`, `ObjStartTrain`, `ObjStopTrain`,
  `ReleasePTank`, `SoundPlay`, `StartRain`; partial `PlayMovie`.

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0041a3e0` / `0x0041a400` | `GameState_IsVibrationActive` / `GameState_GetVibrationOption` | pad p's vibration option (byte `+0x56de + p`, [Save](save.md)); the first also needs byte `+0x56e0`; read by the pad code (`0x00149668`), the option menu and `Profile_Write` | confirmed (code) |

## Starting a match: the music and the wait {#match-start}

What happens to the front end's music between the Choose Area launch and the fight, and how long the player waits
after accepting the prompt (user checklist items 24 a and c; the prompt's missing button glyph, 24 b, is the HUD
text's glyph handling).

**The music** is stopped twice and then cut. Confirmed (code) unless marked:

1. The launch sets "started" (`0x0050f4e0`). Mode 0x11's update then stops the music with `Sound_StopMusicTrack`
   (`0x00110528`) and queues a 1.5 s screen fade-out ([The mode around the screens](frontend.md#rumble-screens)).
   That stop fades each playing channel out over **one bar** of its track ([Music](sound.md#music)).
2. `Menu.startRumbleMode(n)` stops the music again and stops the Wonder Wheel scene before `MenuLoadLevel`
   ([From QUICK RUMBLE to an arena fight](frontend.md#quick-rumble), step 4; inferred, from the script).
3. Mode 8 finishes the front end with `Menu.onFinish` and `UnloadLevel(0)` (`0x001607b8`). Before anything is freed,
   `UnloadLevel` calls `Ambient_Reset`, then **`Sound_StopAllMusic`** (call at `0x00160888` → `0x00110548` →
   `Music_StopAll` `0x0010ddd8`: the queued request dropped, both playing channels stopped and reset **at once**),
   then `Sound_StopAll`. So a track still fading is cut before the arena loads. Mode 1's `Exit` does the same on the
   way out of a level ([Level loading](level-loading.md#unload)).
4. The arena loads under the load screen, whose only sounds are its bank's two loops ([Banks](sound.md#banks)).
5. In the arena **no music plays until the countdown ends**: [`FinishCountdown`](#arena-script) starts it, a random
   `Music[1]` track (Survival's own). The intro's announcer lines and stings are 2D sounds, not music (inferred,
   from the scripts).

**The wait.** From the prompt's accept to the fight is the `dj_ready` line plus **4,000 ms**
([The intro and countdown](#intro), phases 2-4: confirmed (code); about 5 s at runtime, confirmed (runtime)). Before
that, the screen ignores the pad: during the names (each waits for its announcer line and the `dj_vs` line), and
for the prompt's 1,000 ms fade until it is nearly opaque. Accept is menu command 4, **cross on release**
([Input on the front end](frontend.md#input)); START does nothing here, since the intro holds the pause menu's START
cool-down. Confirmed (code) at `0x001f9d78` (accept on event 4 only) and `0x001fa740`.

**In Coney** (read in the engine): `RumbleMenuMode` stops the music when the menus end in a start
(`repo:src/gamemodes/rumble_menu_mode.cpp`). That stop fades over a bar, and `LevelFlowMode::finishFrontEnd`
(`repo:src/gamemodes/level_flow_mode.cpp`), Coney's `UnloadLevel(0)`, stops no music, sound or ambience, so nothing
cuts the front end's track. The intro (`repo:src/gui/rumble_mode_gui/rumble_intro.cpp`) follows the phases above,
with a 1,500 ms stand-in for any announcer line whose length it does not know.

**Coney fixes (items 24 a and c).**

1. Make `finishFrontEnd` do what `UnloadLevel` does to sound before the level goes: reset the ambience, stop all
   music at once (the queued request and both channels, no fade), then stop every sound. Do the same when gameplay
   is left (mode 1's `Exit`), so a level's music never runs into the next.
2. Keep the menu's start-time stop (one-bar fade) as it is. Check that the music player is still updated during
   the menu's 1.5 s fade-out, so that fade can run.
3. Start no music in the arena before `FinishCountdown` calls it. Check with `--event-log` on a QUICK RUMBLE pad
   script: the menu track's `stopped` line before the load, and no `music:` line between the load and the end of
   the countdown.
4. The wait: a test with a fixed step that accept-to-`FinishCountdown` is the ready line's length + 4,000 ms
   (5,500 ms with the stand-in), and that the countdown's words show each second. If the measured time is longer,
   suspect the voice-length lookup (`RumbleIntroSounds::playVoice`), not the countdown.
5. Accept only cross, on release, once the prompt holds the pad. Presses during the names or the prompt's fade are
   dropped, as in the original. That is faithful, though it can feel like a delay.
6. A level-flow test: a looping track started in the front end has stopped when the arena's first frame runs.

## Gang battle trace {#gang-battle-trace}

A recording of the biggest Rumble battle with every human traced on every update, to compare gang combat with Coney's by
behaviour statistics (issue #273); the rules it checks, with code evidence, are on [Gang fights](gang-fights.md), and
which column checks which rule is [What a trace records](gang-fights.md#trace). The columns are the
`humans` field set of `repo:research/traces/fields.toml` (one row per human per update; [Every
human](../guides/research-workflow.md#recording-a-trace)); Coney's `--trace-humans` writes many of the same columns
([Tracing](../guides/building.md#tracing)), and `coney-tools trace fight-report` compares the two by the rules of
[What the comparison computes](#gang-battle-metrics). Coney does not write yet `phase`, `state`, `stunned`,
`attacking`, `active`, `space_up`, `space_down`, `fight_updates`, `fight_active`, `g_alert`, `g_teamups`, `g_order`
and `brain_off`, which those rules use, and writes `g_tactic` as a flag where the original has the tactic's vtable.
The scenarios are `repo:research/traces/scenarios/gang_battle.toml` and its mirror `gang_battle_swap.toml`
([The sides swapped](#gang-battle-swap)); the traces are measurements of the game and stay in the engineer's scratch
folder.

### The set-up {#gang-battle-setup}

All confirmed (runtime), PCSX2 2.9.94, 2026-10-12:

- **Reaching it.** QUICK RUMBLE, WAR PARTY, 1P vs CPU, the Furies and the Orphans; at Choose Area the set-up values
  (`u16` at `0x0063eec0`) were written: index 1 (the mode) = 1 (`RM_Brawl`), index 2 (the gang size) = 9; then the
  Fight Pen. The arena loads nine a side. The state is saved at the intro's "press X" prompt, brains off.
- **The humans.** 30 in use: 12 crowd (`Crowd01`-`06`, gangs 0 and 1, brain type 4, one type 2), `P11`-`P19` in gang
  **3** (handle indices 13-21) and `P21`-`P29` in gang **4** (indices 22-30). This answers the open question below on
  the gang slots: `Gang1` is gang 3 and `Gang2` gang 4 (the gang record's `+0x30`; `AddRumbleGang2` passes 19 to
  `GangCreate`, so that argument is not the number the record holds). A handle's high half is the index, its low
  half a serial (`P11` is `0x000d005d`). Before the intro every fighter's brain `+0x09` (dead) is 1, its goal stack
  empty, health 1000 of 1000; `P11` is player 1 (brain type 0).
- **The intro** starts only when cross is held (about 300 ms, 10 updates); a press of one update did not start it.
  The brains go on (`+0x09` turns 0, `FinishCountdown`) **151 to 172 updates** after the press, depending on the
  match (151, 151, 172): the intro does not last a fixed number of updates.
- **Every fighter driven by its brain.** After the sample of frame 200 the recorder calls `Human_ReleasePlayer(P11,
  0, 1)` (`0x0022a2a8`, [Characters](characters.md)), as `HuSwitchPlayer` hands a player back; from then on no human
  is player 0, and none became one when `P11` went down. Called before the brains go on, it stops the battle from ever
  starting: the Furies took the follow-player goal and the Orphans none for 9,000 updates.
- **Different matches** come from an offset added to the 10-bit index of the random stream `0x006eb880`
  ([Maths](maths.md#random)) before the first update: 0, 341 and 682. The same offset gives the same intro length.
- **Sampling.** PCSX2 at 0.4 speed (`speed` in the scenario): 9,001 samples a match, none missed; 10 or 11 torn, one
  about every 1,000 updates (a frame that runs two updates), left out of the counts below.

### What the matches show {#gang-battle-results}

Confirmed (runtime), five matches with the Furies as side 1: offsets 0, 341 and 682 (2026-10-12) and 100 and 500
(2026-10-13, with the hooks of `repo:research/traces/patches/gang-battle.toml`). Counts are over the fight, from the
brains going on to `RumbleOver` (every brain off: every goal stack empty from then on), per minute that a fighter
stands.

| | Offset 0 | Offset 341 | Offset 682 | Offset 100 | Offset 500 |
| --- | --- | --- | --- | --- | --- |
| Fight length | 108.1 s | 125.5 s | 144.2 s | 110.5 s | 123.5 s |
| Winner (fighters out) | Furies (2 out, Orphans 9) | Furies (2, 9) | Furies (8, 9) | Furies (3, 9) | Furies (3, 9) |
| Attacks a minute, Furies / Orphans | 13.4 / 19.1 | 16.1 / 21.4 | 18.7 / 21.7 | 19.4 / 22.3 | 14.1 / 21.8 |
| Blocks a minute (goal 27 started) | 1.9 / 0.2 | 2.8 / 0.2 | 3.8 / 0.1 | 2.5 / 0.4 | 2.7 / 0.0 |
| Reactions a minute (a reaction goal set) | 1.4 / 4.3 | 2.0 / 3.9 | 2.8 / 3.3 | 1.5 / 4.3 | 1.2 / 4.8 |
| Target switches a minute | 18.1 / 15.8 | 14.4 / 12.1 | 9.6 / 13.6 | 14.2 / 10.3 | 25.2 / 12.6 |
| Health losses a minute | 14.9 / 19.8 | 27.4 / 20.6 | 22.9 / 17.5 | 17.3 / 23.2 | 14.4 / 22.5 |
| Top goal Fight (15), % of standing time | 78 / 95 | 77 / 94 | 71 / 93 | 74 / 90 | 79 / 95 |
| Top goal Blocking (27), % | 8.0 / 0.5 | 10.8 / 1.2 | 16.8 / 0.1 | 10.2 / 2.3 | 9.6 / 0.0 |
| Distance to the target, median (90 %) | 1.27 (4.38) / 1.16 (4.12) m | 1.10 (3.97) / 1.14 (4.21) m | 1.04 (3.45) / 1.04 (3.44) m | 1.17 (4.05) / 1.13 (3.61) m | 1.21 (4.48) / 1.07 (4.11) m |
| Fighters on one enemy, mean | 1.37 / 1.34 | 1.42 / 1.25 | 1.21 / 1.27 | 1.38 / 1.19 | 1.44 / 1.19 |

- **The Furies win.** The Furies (side 1, `P11`'s gang) won all five, though knocked down more often (24, 31, 36, 22 and
  18 times against 19, 12, 14, 6 and 14). The Orphans spend 90-95 % of their standing time with Fight on top; the Furies
  71-79 %, with the Blocking goal on top 8-17 % of the time against the Orphans' 0-2.3 %. With the sides swapped the
  Furies still win ([below](#gang-battle-swap)): with these two gangs, it is the gang, not the side (one pair of gangs,
  five matches one way and three the other).
- **No block is ever held.** No human of any match had the block state (`0x8000`) or the attack phase's block bit
  (`0x1000`): while Blocking is on top, the clips are the fight idle (358), hit reactions (272, 275, 276) and
  knock-downs (288, 196, 199). The `blocking` column is 0 throughout; count blocks by the goal.
- **Where an attack starts.** The wind-up (phase `0x1`) starts with the target 0.78-0.80 m away (median of each side
  and match), 0.71-1.06 m for 80 % of them (215-324 attacks a side and match).
- **How many fight one enemy.** At most **4** of a side target one enemy at once (in every match), usually 1 (64-80
  % of the cases), 2 in 16-32 %. Every fighter's attack slots allowed (`slots_max`) are **4**; the slots taken on a
  fighter reach 4, its active attackers (`active`) at most 2 (3 in a few samples just after he got up,
  [Gang fights: runtime checks](gang-fights.md#runtime) rule 3). Both are the two limits read in the code
  ([How many hit one man at once](gang-fights.md#limits)). An enemy list holds 8 or 9 handles most of the fight.
- **Out of the fight.** A knocked-out fighter keeps health **1** and lies in `ANIM_DEAD_IDLE` (349); a wounded one has
  state `0x10000` and crawls (`ANIM_WOUNDED_IDLE`, 200, then `ANIM_WOUNDED_HIT_REACT_DIE`, 204). Both count as out
  for the brawl, and so does the dying state before them (`0x80000000`, from the lethal hit). A knocked-out fighter is
  **deleted 461 updates** (15.4 s) after it lies down (four of four checked in offset 0's match): 420 knocked out, then
  41 dead ([Combat: the knocked-out bit](combat-reactions.md#knocked-out-bit)). At `RumbleOver` the standing fighters of
  the winning side but the winner are deleted 25 updates after the brains go off (offset 0), as `CreateWinCam` does.

### The sides swapped {#gang-battle-swap}

Confirmed (runtime), PCSX2 2.9.94, 2026-10-13: `repo:research/traces/scenarios/gang_battle_swap.toml`, the same
battle with the Orphans as side 1 (gang 3, `P11`-`P19`, the player's side, handed to the AI as above) and the Furies
as side 2 (gang 4). Its state is made like the first, but at Choose Area the two sides' set-up values are swapped too
([Front end: set-up](frontend.md#rumble-setup)): indices 3 and 4 (the gangs), 5-13 with 14-22 (their character types)
and the two names at `0x0063eef0` and `0x0063ef10`; the intro then names the Orphans first. Offsets 0, 341 and 682.

| Side 1 Orphans / side 2 Furies | Offset 0 | Offset 341 | Offset 682 |
| --- | --- | --- | --- |
| Fight length | 69.9 s | 69.9 s | 79.4 s |
| Winner (fighters out) | Furies (0 out, Orphans 9) | Furies (0, 9) | Furies (0, 9) |
| Attacks a minute | 8.1 / 26.2 | 13.6 / 28.6 | 12.7 / 21.9 |
| Blocks a minute (goal 27 started) | 0.4 / 1.0 | 0.1 / 1.9 | 0.0 / 1.8 |
| Reactions a minute | 5.7 / 0.8 | 5.3 / 0.8 | 5.8 / 0.8 |
| Target switches a minute | 9.3 / 30.4 | 8.9 / 37.9 | 10.9 / 23.5 |
| Health losses a minute | 36.7 / 7.1 | 45.2 / 9.5 | 32.1 / 8.9 |
| Top goal Fight (15), % | 82 / 86 | 90 / 84 | 92 / 87 |
| Top goal Blocking (27), % | 1.1 / 4.3 | 0.5 / 8.3 | 0.0 / 5.8 |
| Distance to the target, median (90 %) | 1.25 (4.56) / 1.31 (4.31) m | 1.11 (4.28) / 0.98 (3.62) m | 1.36 (4.58) / 1.13 (4.25) m |
| Fighters on one enemy, mean | 1.32 / 1.76 | 1.20 / 1.55 | 1.24 / 1.68 |

- **With these two gangs, the gang decides who wins, and how it fights.** The Furies won all three as side 2, faster
  and without losing a fighter. What goes with the Furies on either side: winning, the Blocking goal (4.3-17 % of
  their standing time against the Orphans' 0-2.3 %) and few reactions (0.8-2.8 a minute against 3.3-5.8). Inferred:
  from the two gangs' classes, which this pass did not compare ([Characters](characters.md)).
- **The side decides how often it attacks.** Side 2 attacked more often than side 1 in all eight matches (Orphans
  19-22 against Furies 13-19; Furies 22-29 against Orphans 8-14). Inferred: side 1 fights under the attack command's
  FollowAndAttack and side 2 under TacticAttack's Melee ([Gang fights: runtime checks](gang-fights.md#runtime)).
- **Spacing goes with the side**: side 2's fighters carry the spacing bytes of gang type 3, the type of side 1 that
  attacks them (1 standing, 4 downed), and side 1's those of type 19, in both orientations, as `GangCreate`'s types
  say ([How many hit one man at once](gang-fights.md#limits)).
- **No team-ups, no shared target** on either side in any of the eight matches.

### What the comparison computes {#gang-battle-metrics}

The rules that turn an all-humans trace into the statistics above, for the original's recordings and Coney's alike
(`coney-tools trace fight-report`, [Comparing with Coney](../guides/research-workflow.md#comparing-with-coney)).
They come from the recordings; where a rule differs from what the report did before, the original's trace showed why.

1. **The fighters** are the humans of the two sides' gangs (the gangs of `P11` and `P21`: 3 and 4 in the original);
   the crowd (gangs 0 and 1, brain type 4) is left out.
2. **The fight** runs from the first update where a fighter's brain is on (`brain_off` 0, or failing that column, the
   first goal pushed) to `RumbleOver`, the first update after which every fighter's `brain_off` is 1 (or every goal
   stack is empty for 30 updates in a row: one update with every stack empty happens mid-fight). Nothing before or
   after counts: the intro is 151-172 updates, and the trace goes on after the end.
3. **Out of the fight**: the 64-bit state word holds any of **`0x180050000`**, the mask of `Human_IsDownOrDead`
   (`0x00227e60`): in the trace's columns, `state & 0x80050000` (dying `0x80000000`, knocked out `0x40000`, wounded
   `0x10000`) or `state_hi & 0x1` (dead), or the human gone from the trace. A fighter is out from the update of the
   lethal hit (dying) and never comes back in a brawl: none is revivable, so a knock-out turns into death after 14 s
   and deletion 41 updates later ([Combat: who sets and clears the knocked-out
   bit](combat-reactions.md#knocked-out-bit), with its runtime timeline). `0x40000` alone is not enough: it comes only
   with clip 349, after the dying reaction and any wounded crawl, and goes for the last 41 updates. Clip 349 and
   `0x40000` start on the same update, so either marks the knock-out itself. The attack's recovery `0x40000` is the
   `phase` column, not `state`. Health stays 1, so health 0 is never a knock-out test. **Standing time** is a fighter's
   updates in the fight while not out; every rate is per standing minute at 30 updates a second, per side.
4. **The winner** is the side with fighters standing at `RumbleOver`; the fighters out of each side are counted then.
5. **Attacks** are wind-up starts: the `phase` bit `0x1` turning on (the `command` column does not hold an AI's
   attacks). Their distance is the target's at that update.
6. **Blocks** are the Blocking goal (27) becoming the top goal; the `blocking` state bit is never set by the AI here.
   **Reactions** are `react` turning from -1 to a goal id; **knock-downs** `down` turning on; **health losses**
   `health` going down.
7. **Target switches** count every change of `target` between two humans or to and from none, and split by kind:
   A to B, set from none, cleared to none. Melee's re-pick clears and sets within one update, so a sample shows it
   as A to B. With the `set-target` hook (or Coney's equivalent), split by cause as in
   [Gang fights: runtime checks](gang-fights.md#runtime) rule 6, using the return addresses of [What each caller
   means](gang-fights.md#set-target-causes).
8. **Goal shares** are the top goal's share of standing time; **fight goals** are runs of Fight on top, a new one
   when `fight_updates` starts again: their length (median, 90th percentile, the share reaching 4 s).
9. **Fighters on one enemy**: at each update, for each enemy that standing fighters of a side target, how many do (the
   mean, the maximum, the shares of 1, 2, 3 and 4). **Active places** on a fighter: the shares of `active` 0, 1, 2.
10. **Waiting**: the distance to the target of a fighter with Fight on top and `fight_active` 0, split by whether the
    target's own target is him (medians, the share in 4-4.75 m). **Taunts**: clip `0x25b` (603) starting.
11. **Help**: help calls a minute, from all three sites of [Gang fights](gang-fights.md#runtime) rule 7 (the `help-call`
    hook, one per `Gang_BroadcastHelpCall`; Coney: one per help call, whatever the number of hearers its event 20
    reaches, not one per event received), helpers sent a minute (the `send-helper` hook) and HelpRespond goal (29)
    starts; the **idle share**, an empty goal stack while standing.
12. **The gang**: the tactic each side runs and when it changes (side 1 the attack command's tactic for the whole
    match; side 2 TacticConfront for its first 23-47 updates, then TacticAttack), `g_teamups` (0) and `g_order` (no
    order), `g_chosen` (none).

The spread to compare against is the original's range over its matches of one orientation (the five above with the
Furies as side 1, or the three swapped), since the gangs and the sides each change the numbers.

## Coney's implementation {#coney}

A Brawl (1 ON 1 or WAR PARTY) plays to its end, won or lost
([Running Coney: QUICK RUMBLE](../guides/running.md#loading-entries-from-your-disc)): the set-up menus, the arena
script's sides, the intro and countdown, the other side's fighters, the knockdown, the player's revival and hand-over,
the winner's cheer, the win camera and the result screen with its three paths. King of the hill plays to its result
screen when the player holds the top, Battle royal when one side is rung out, Survival when the spawned enemies have
beaten the player, and Wheelchair when the CPU racer finishes. Tag battle and Mercy are not built yet.

- **Intro** (`repo:src/gui/rumble_mode_gui/rumble_intro.h`, drawn over play by
  `repo:src/gamemodes/rumble_intro_layer.h`): `ShowRumbleModeIntro` is held until the level's first frame, because
  Coney runs the start callback before the level loads.
- **Fighters**: `TacticConfront` and `TacticAttack` (`repo:src/ai/tactic_confront.h`, `repo:src/ai/tactic_attack.h`)
  with stand-in confront and melee goals; `HuSetMaxHealth`, `HuDelete`, `HuGetGang`, `BrFlushGoals` and
  `BrFlushActions` (`repo:src/scripting/rumble_match_bindings.h`). The humans the start callback creates are counted
  in their gangs and answer `HuIsAlive` until the level makes them, so `AddBrawlGang1`/`2` set the down handler.
- **A player down** ([above](#player-down)): `HuSwitchPlayer` (`repo:src/ai/scripted_brains.h`) picks the
  team-mate, and the play mode hands him the pad (`repo:src/ai/ai_humans.h`, `repo:src/human/player.h`): the pad and
  the follow camera drive him, his brain becomes the player's, and the human left behind fights as his class; the
  shared camera targets follow. The revival (`HuRevive` after 4 s) and the new player's cue (`SoundPlay2D`) are the
  scripts' own.
- **Every fighter AI-driven** (`--ai-player`, a test aid, [Running: tracing](../guides/running.md#tracing)): when the
  countdown ends (after `FinishCountdown` and `StartIt`), player 1 is released as `Human_ReleasePlayer`
  ([Characters](characters.md#players), `0x0022a2a8`) releases a human with no one taking the pad
  (`repo:src/ai/ai_humans.h`): his brain goes back to his class's type with its goals and actions cleared, his flags are
  the released ones ([Combat: human flags](combat.md#human-flags)) and the pad drives nobody, so his brain alone moves
  him and `HuSwitchPlayer` finds no player to hand on. With no one in the player list, the game-over check ([Combat:
  defeat](combat-reactions.md#defeat)) has no one to fail the match for, however he fares. **Coney's choices**: he keeps
  the player's fighter profile (the wider body sphere `Human_ReleasePlayer` puts back to 1.0, [Physics](physics.md#sweep),
  and the player's hit flags), as after `HuSwitchPlayer`; the scripts' player 1 handle still names him; the HUD panel
  stays. `--ai-seed` varies the AI's draws: the shared stream's index (`0x006eb880`, [Scripting](scripting.md)) starts
  that far in, and Coney's per-brain generators, which the original does not have, are seeded from it.
- **The cheer**: `HuUseAnim`'s idle replacement (slot 0) plays: its `.anm` resource is loaded from the disc when first
  named (`repo:src/characters/dynamic_clips.h`) and plays wherever the idle would.
- **Win camera** (`repo:src/camera/win_camera.h`): `CameraCreateWin`, `CamDelete` and `CamSetFollowHeading`; it
  starts on activation, from where the winner was teleported.
- **Arena set-up** (`repo:src/scripting/arena_bindings.h`): `SetGameMode` and `GetGameMode` (the hand-over's
  kind-0 fallback needs mode 0), the precache queue, `HuLockMovement`, `HuEnableSoundCommands`, the pocket, the damage
  response and `Teleport`; `HUDSetNumIndicator` and `SoundPauseSound` with the HUD and sound bindings.
- **King of the hill**: `TacticDomination` (`repo:src/ai/tactic_domination.h`, the hold-flag goal), the scoreboard
  (`HUDEnableTextProgress`, `HUDSetTextProgress`) and the stopwatch's display (`W_ShowStopWatch`) on the HUD, the
  leader's crown (`GangAttachSpinningIcon`, kept, not drawn), `HuForceEnableReticule` (kept) and `CamSetSplitMode`
  (kept: one view). In arenas 101, 129 and 130 the AI is held at the foot of the top tier (inferred: the way up
  needs the flag-`0x80` jump legs the goal's `+0xe0` bit 2 allows, [AI: route follow](ai-moving.md#route-follow), which
  Coney's move action does not take), so only the player scores.
- **Battle royal**: the scripts' own ring-out rules run on `TacticDomination`, `HuSetSlowMo` (the characters' step,
  `repo:src/camera/slow_motion.h`), `HuSetConscious` (a knocked-out mark that stops the human and its brain) and
  `TurnWarriorCommands`.
- **Survival**: the gangs' spawners spawn ([AI: spawners](ai-gangs.md#spawners), `repo:src/ai/spawners.h`): the arena's
  two, in state 8, make their humans on a route node out of the camera's sight beyond 15 m, and
  `CreateENEMY` sends each at him with `GoalEngageEnemy` and `GoalMoveToHuman` (`repo:src/ai/engage_goals.h`) after
  `BrSetType`, `HuGetCharType`, `BrSetAttackWeight` and `HuGetPosition`. Once he is down `SavePlayerStats` ends the
  match with his time.
- **Wheelchair**: the scripts' own race runs: the CPU racer drives `WheelChairRace`, its checkpoint glow moved on by
  `Teleport` (which moves a spawned object's record as well as a human, so the glow's trigger sphere follows), the
  finish line tinted with `ObjColor`. `HuSetWheelchairControl` sets the wheelchair flag, turns every command off and
  the look-behind switch off; the player then drives with L1 and R1 and brakes with cross
  ([Characters: wheelchair control](characters.md#wheelchair), `repo:src/human/human.h`), while the CPU racer keeps
  its own locomotion. Stand-ins: no start clip or wheelchair clips (the body slides in its idle), commands 46 and 47
  are not added (the control reads the buttons itself), and `DeclareWinner`'s pad lock stops the pushes.
  `HuSetNoAutoLock` and `CamAssignRevCamButton` are kept, and `ActGiveWay` makes the human give way to the other
  ([AI: Giving way](ai-moving.md#giving-way)).
- **Result screen**, mode 0x14 (`repo:src/gamemodes/rumble_result_mode.h`,
  `repo:src/gui/rumble_mode_gui/rumble_result_menu.h`):
  the world keeps running under it; its choices act through the pause menu's outcomes.

Coney's stand-ins, each an open question below where the page is silent: the intro's layout, separator text and voice
lengths; the result lines' look and the grids' rows; the melee and confront goals; the confront tactic's radii and route
test; the switch's choice and the brain left behind; a dynamic clip's rate; a locked human's movement (neither stick nor
brain moves it); the number indicator, kept but not drawn; `PrecacheWorld`, which only empties the queue; the
scoreboard's and the stopwatch's places; the hold-flag goal's type ids, taunt and fight; a knock-out without its clips
or its wake-up after 14 s; spawner state 7's placement ([AI: Coney](ai.md#coney)); the engage goal's range; the
wheelchair's clips and the give-way action.

## Open questions {#open-questions}

- Why King of the hill's `GameOver` assumes gangs 1 and 2 when `Gang1` and `Gang2` are gangs 3 and 4 at runtime
  ([Gang battle trace](#gang-battle-setup)).
- What `SetGameMode`'s arguments 3 and 19 select, and who reads them; where the mode returns to 0 after an arena.
- What reads `HuLockMovement`'s flag (`0x200000000`) and so what a locked human may still do.
- Where `HUDSetNumIndicator`'s count is drawn and how it looks.
- King of the hill in Coney scores 2 a tick (every 1,850 ms) for a gang whose player stands alone on top, where the
  rules above give 1; what `X.Update` adds per member on top.
- The hold-flag goal's `+0xe0` bit 2 and the move action's flag-`0x80` (jump) edges: how a holder climbs to the top,
  and the domination tactic's and hold-flag goal's type ids.
- The win camera's stop condition (`0x005fdeb8 + 0x1d8` / `+0x1dc`) and the base camera slots `+0x194` / `+0x1ac`.
- What the sound call on `"menu"` (`0x0010fa50`) does when the result screen's choices appear.
- The countdown's first sound (`+0xfc` of the intro, loaded before the screen opens).
- The intro's layout after `RM_Intro_LayoutNames` (the set-up is [answered](#intro)), the separator sprite's picture
  and how long each announcer line lasts (Coney: names at y 0.35 and 0.55, "VS" as text, 1.5 s a line without lengths).
- The intro's screen effects 0 (left out by Coney) and how opaque the prompt is when the screen takes the pad
  (Coney: 0.95).
- The result screen's two lines' font, scale and colour, and the grids' rows (Coney: one row of two).
- Why the AI's Blocking goal (27) never shows a held block in a Rumble brawl ([Gang battle
  trace](#gang-battle-results)).
- `Goal_Melee` (8) and the confront goal (60): how the melee goal picks its target and moves, and how a confronting
  member closes in (Coney: the nearest gang enemy, run to and fight; close to the critical range).
- The confront tactic's gang radii and route test between the leaders (Coney: radii 0, always a route, so
  code 9 never fires).
- Who sets the member priority byte `+0x1b1` that `HuSwitchPlayer` prefers, and what `W_GameState + 0x158` is (Coney:
  the first member found; the kind-0 fallback outside `level99` always allowed).
- What brain the human left behind by `HuSwitchPlayer` runs, and whether he keeps the player's fighter profile
  (Coney: his class's brain type, the profile unchanged).
- The playback rate of a dynamic clip played by `HuUseAnim` (Coney: 1).
- The range beyond which `GoalEngageEnemy`'s goal gives up its enemy (Coney: none).
- Where spawner state 7 places its human (`0x001679e8`; Coney: as state 8 with a value of 0). The out-of-sight search
  itself is answered in [AI: the search](ai-gangs.md#spawner-search).
- The wheelchair's clips and start (state code 5), and whether the update is 1/30 s or 1/60 s (Coney: no clips, the
  constants per 1/30 s step).
- Mercy is versus only, and Coney has no second player yet; its one-player path (`GoalGrabTarget` on `P21` against
  Mercy) is not reachable from the menus.
