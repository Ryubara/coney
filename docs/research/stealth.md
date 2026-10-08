# Stealth

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), with the disc's compiled `level87_chap5_club.lua` read through `coney-tools`' Lua walker for the calls and
values its tutorial passes.

## Purpose

How the player hides and how guards find him: the shadow ground and the hidden state, sneaking, the HUD's cue,
scouts and their sight, how being spotted fails a stealth section, the stealth kill and distractions. The Destroyers'
hangout (level87, checkpoint 5) teaches all of it. Pieces documented elsewhere are linked, not repeated: the enemy
scan and sight tests ([AI: the enemy scan](ai.md#enemy-scan), [Sight](ai.md#sight)), the Scout and PathScout goals'
code rows ([AI goals](ai-goals.md#goal-scout)), the stealth kill's clip data ([Combat moves](combat-moves.md#stealth)),
the radar's drawing ([GUI: the radar](gui.md#radar-icons)) and the wanted timers ([Crimes: wanted](crimes.md#wanted)).

## Original structure

No source file names this code; it sits in the human, brain and tactic code ([Source map](source-map.md)). Names are
ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0023eab8` | `Human_SnapToGround` | each update: the ground ray, then the hide rules and the radar tint | confirmed (code) |
| `0x0023e6e8` | `Human_SetHiddenState` | hidden on or off through the brain | confirmed (code) |
| `0x0028ee88` | `Brain_SetHiddenInShadow` | brain `+0x2d4`; for a player enters or leaves the hidden state | confirmed (code) |
| `0x0028ef00` | `Brain_SetOnShadowGround` | brain `+0x2d5`: standing on shadow ground | confirmed (code) |
| `0x0022ff88` | `Human_EnterShadow` | state `0x200000`, out of the fight stance, move style `0x14` | confirmed (code) |
| `0x002300c0` | `Human_LeaveShadow` | clears it, or starts the 4 s grace | confirmed (code) |
| `0x00230140` | `Human_ClearHiddenState` | clears state `0x200000` and the move style | confirmed (code) |
| `0x0028f000` | `Brain_ShakeOffPursuers` | far hunters without sight forget a player in shadow | confirmed (code) |
| `0x0023e7e0` | `Player_UpdateHideOrder` | the player's Warriors told to hide with him | confirmed (code) |
| `0x0031a430` | `ScoutTactic_Construct` | `TacticScout`'s fields | confirmed (code) |
| `0x0031a818` | `ScoutTactic_OnEvent` | a member hit or seeing an enemy: fight, call the gang | confirmed (code) |
| `0x0031b030` | `ScoutTactic_Process` | every 200 ms: fighters, the gang's alert state | confirmed (code) |
| `0x002d73b8` | `ScoutGoal_Process` | the guard at his post | confirmed (code) |
| `0x002d6638` | `CallGangGoal_Process` | the call that makes the player's gang wanted | confirmed (code) |
| `0x0027e040` | `Player_UpdateActionsHidden` | the buttons while hidden: the stealth kill test | confirmed (code) |
| `0x0025eb00` | `Human_SetStealthReadyBlend` | the ready-to-kill clips by distance | confirmed (code) |
| `0x00264738` | `Player_StartStealthKill` | the kill clip and the pair | confirmed (code) |
| `0x002936a8` | `AI_ReportNoise` | a noise: event `0x17` to the humans who hear it | confirmed (code) |
| `0x00304c78` | `GangBrain_OnCrimeSeen` | a gang member pushes Investigate at a noise | confirmed (code) |
| `0x002d0100` | `InvestigateGoal_Process` | the run, the look-around and the search | confirmed (code) |

## Behaviour

### Shadow ground {#shadow-ground}

A hide area is **level collision**, not script: collision triangles with flag bit 4 (`0x0010`,
[Collision: triangles](collision.md#triangles)) are shadow ground. The disc has 1,052 such triangles in all its
levels. No binding adds, moves or tests a hide area; level87's script has only volume boxes that start its hints
(`volCh5HB01`). Confirmed (code) at `0x0023eab8` for the test; no script reference to the bit found (inferred).

**The test** (`Human_SnapToGround`, every update for every human): the ground ray (1.0 m above the feet, 1.5 m down,
[Characters](characters.md#ground)) returns the triangle's flags. For each player `p`, a human that is `p` or a member
of `p`'s gang, confirmed (code):

1. **Off shadow ground** (bit `0x10` clear): brain `+0x2d5` = 0; for the player himself the radar tint goes grey;
   if he was hidden (brain `+0x2d4`), `Human_SetHiddenState(0)` ([leaving](#leaving)).
2. **On shadow ground**: when his `+0x2d5` was 0 and he is a player or a Warrior, `Brain_ShakeOffPursuers`: every
   member of a hostile gang who has him in his enemy list, is farther from him than twice his own brain `+0x140` and
   has no line of sight to him, drops one from his hostile count (`Brain_UncountEnemy`). Then `+0x2d5` = 1. He
   **may hide** unless he holds a world object of type 8 (`TYPE_MOLOTOV`, CfgObj `+0x86`; an AI drops it, a player
   cannot hide while carrying it).
    - Not hunted (his brain's hostile count `+0x152` is 0, nobody lists him as an enemy) and allowed: the player's
      radar turns **blue**; if not yet hidden, `Human_SetHiddenState(1)`; already hidden, a player runs
      `Player_UpdateHideOrder` (below).
    - Hunted (`+0x152` above 0) or carrying a molotov: if hidden, he leaves the hidden state and the radar goes
      grey.

So **you cannot hide while anyone has you as an enemy**: get out of sight first, wait for the hunters' scans to drop
you ([the enemy scan](ai.md#enemy-scan), step 3), then the shadow takes you. Other humans (not of a player's gang)
take the same rules without the radar and the hide order. Bit 5 (`0x0020`) of the same triangle sets human byte
`+0x5b7` (`Human_SetCoverFlag5`, `0x002195e0`), which the police's break-and-enter check reads
([AI: cops](ai.md#think-cop), step 6); it is not part of hiding.

### The hidden state {#hidden}

`Brain_SetHiddenInShadow(brain, on)` writes brain `+0x2d4` (and, on entering, the time at brain `+0x10`); for a
player (human `+0x1b0` not −1) it also runs, confirmed (code):

- **Entering** (`Human_EnterShadow`, `0x0022ff88`), unless already hidden, in state `0x1000000` (sprinting) or
  aiming a throw with a kind-4 or 6 object: cancel any throw aim, drop the L1 target bit (state `0x8`), leave the
  fight stance and lock (`Player_ExitStanceAndLock`), set state **`0x200000`** (hidden), set state `0x20000000`
  (rebuild the anim state) when his actions are free, and push **move style `0x14`** unless busy (state
  `0x7839e1f7ff0`). The grace time (record `+0x114`) is cleared.
- <span id="leaving"></span>**Leaving** (`Human_LeaveShadow`, `0x002300c0`), when hidden: if he is **not running
  and has a target** (an L1 target, [the stealth kill](#stealth-kill)), the hidden state is kept for a
  **grace of 4 s**: record `+0x114` = now + 4000 ms (once; an earlier deadline is kept). Otherwise it ends at once
  (`Human_ClearHiddenState`). The grace runs out in `Human_UpdateActions` (`0x00254e78`), which clears the state when
  `+0x114` has passed.
- **`Human_ClearHiddenState`** (`0x00230140`): clears `0x200000`; when not in a fight stance, sprinting, running,
  jogging or busy, sets `0x20000000` (the exit clip below); removes move style `0x14`; clears the grace.

Also clearing it: **L2 sprint** with stamina (`Player_UpdateSprint`, `0x0027ce90`), entering the fight stance
(`Player_EnterStance`, `0x00280068`), rage (`Player_TryStartRage`), a broken pair, and the AI's Hide and Scatter
goals for Warriors. While hidden the player does not enter the fight stance by himself (the stance test in
`Player_UpdateSprint` needs `0x200000` clear) and an L1 target is not locked on ([the stealth kill](#stealth-kill)).
Confirmed (code) at the callers of `0x00230140`.

**What hidden changes for the AI**: [the enemy scan](ai.md#enemy-scan) and the spot test skip him
([Seen or not](#seen)); the follow camera raises its look-at point to 1.65 m ([Camera](camera.md#update));
his footsteps play at half volume ([Characters](characters.md)); the AI's enemy picks drop him
([AI: the score](ai.md#enemy-score)).

**The player's Warriors hide too** (`Player_UpdateHideOrder`, `0x0023e7e0`, while hidden on shadow ground, standing
still: speed `+0x1ac` = 0, not busy, his gang able to help): after a delay (500 ms; with no command active, the time
the nearest free member already on shadow ground needs to run to him at gait 4, + 500 ms) and while none of his gang
has attackers, he issues Warrior command **3**, hold (`WarriorCommand_Dispatch`, [AI: Warrior
commands](ai.md#warrior-commands): its line is `holdhide` while he hides); the command in force before is remembered
at `0x0051480c` and given back when he leaves the hidden state (`Human_SetHiddenState(0)`). Confirmed (code); whether
the hold takes its Hide form (type `0x13`) here is not traced.

### Sneaking {#sneaking}

There is no sneak button and no separate stick threshold: hidden, the player walks and stands with the move style
`0x14` clips, confirmed (code) at `0x00253688`:

| Anim slot | Normal | Hidden (style `0x14`) |
| --- | --- | --- |
| 0, 11 (idle) | 388 | **630** `STEALTH_IDLE` |
| 3, 4 (sneak walk, walk) | 407, 408 | **633** `STEALTH_WALK` |
| 9, 10 (walk start, run start) | 413, 414 | **631** `STEALTH_WALK_START` |
| 5, 6, 7 (jog, run, sprint) | unchanged | unchanged |

The gait speeds are recomputed from the clips when the style changes ([Speed classes](characters.md#speed-classes)),
so the hidden walk's speed is 633's root speed (about 2.31 m/s, [Combat moves](combat-moves.md#stealth)).
The stick thresholds are the normal ones ([Locomotion](characters.md#locomotion)): above the 0.12 dead zone the
player **walks** (633) at any deflection up to 0.95; above 0.95 he **runs** with the normal run clip and stays hidden
while on shadow ground (inferred: no clearer for a run found; running only matters when he steps off, as no grace
is given then); L2 sprints and ends it.
**Transitions**: when the idle is rebuilt for a hidden player whose move style is not yet set, 634
`STEALTH_TRANSITION_FROM_NORMAL` plays (holding `0x40000000`); when the idle is rebuilt for a player no longer
hidden whose clip is still 630, 394 `NORMAL_TRANSITION_FROM_STEALTH` plays (`Human_BuildIdleTasks`, `0x0025f770`).
Confirmed (code) for the clips; the move-style timing of 634 inferred.

**Sound**: while hidden, the player's prepared line is the anim sound `0x57` (bare hands), `0x59` (set 1) or `0x58`
(sets 2 and 3) (`Player_UpdateHiddenLoopSound`, `0x002301f0`); it is stopped when he is no longer hidden. When it
plays is not traced.

### The HUD cue {#hud-cue}

The only HUD cue is the **radar disc colour**: blue `(100, 120, 200, 240)` while the player stands on shadow ground
and may hide (the hidden state follows on the same update), grey `(191, 191, 191, 240)` otherwise, blended over
500 ms ([HUD: the radar](hud.md#the-radar-on-screen)). `HUD_RadarSetTintBlue` (`0x001b26a8`) sets it only while the
HUD shows and the player is in neither state `0x00227dd8` nor `0x00223b70`; in a two-player game with one shared
view, a grey for one player waits until the other is also off shadow ground (or knocked out or cuffed). Confirmed
(code) at `0x0023eab8`. No text, icon or screen effect marks hiding; the tutorial explains it with hint text
([The hangout tutorial](#level87)).

### Scouts {#scouts}

A guard is a member of a gang under **`TacticScout`** (type `0x27`). Its arguments, confirmed (code) at
`0x003776d8`, `0x0031a268` and `0x0031a430`:

| # | Argument (default) | Field | Use | level87 |
| --- | --- | --- | --- | --- |
| 1 | gang | | the gang | `GangStealth01`-`05` |
| 2 | call count (0) | `+0x50` | handed to the gang call: how many responders to queue | 10 |
| 3 | call delay (0) | `+0x51` | handed to the gang call: seconds before they come | 0 |
| 4 | range (40) | `+0x20` | the call's phone search radius, m | 10 |
| 5 | roam radius (10) | `+0x24` | how far from his post he wanders, m (0: never) | 0 |
| 6 | roam arc (30) | `+0x28` | the arc about his heading he wanders in, degrees | 20 |
| 7 | callback | | the tactic's Lua callback ([Tactics](ai.md#tactic-kinds)) | `ch5.StealthSpotted` |

Arguments 2 and 3 are the same pair `TacticPathScout` names `callCount` and `callDelaySec`
([binding](../references/bindings/ai.md#tacticpathscout)). **Start** (`0x0031af98`) gives each living member a
**Scout** goal (`0x6f`, his post = where he stands, his heading = his facing), adds the gang's idle fidgets and makes
anim `0x29c` the gang's scouting clip. The Scout goal, confirmed (code) at `0x002d6ca0`-`0x002d73b8`:

- **Resume**: a radar blip of type 6 (icon 352, an **orange dot**, mode 0) unless he has one; a held kind-4 or 6
  object dropped; **threat response 0** (`+0x21c`, so his think pushes no fight of its own); **scan interval 500
  ms** (`+0x144`, instead of 2000); not pushable. The old values come back at End.
- **Process**: off his post by more than 0.3 m, he walks back (gait 2 within the roam radius, else gait 3; gait 2
  whenever a hidden player is within 10 m of him); then turns to his heading when more than 15° off; at the post,
  every 4 s, 20 % a looped look clip (anim `0x29f`), else a fidget; with a roam radius, a random reachable point on
  his turf within it and the arc, walked to and back at gait 2. Every 45 updates while walking or idle a head
  glance (1.5 s, `Human_LookAround`: 22.5°-67.5° to one side).

### Seen or not {#seen}

A scout notices the player only through his **enemy scan** ([AI: the enemy scan](ai.md#enemy-scan)), every 500 ms
under the Scout goal (× 4 when he is beyond 30 m from the camera, detail level above 0). For a guard `G` and the
player `P`, confirmed (code) at `0x0028b358`:

1. `P` must be within `G`'s **sight range** (brain `+0x130`, `HuSetLOSRange`; 30 m when a brain is made).
2. **Hidden** (`P`'s brain `+0x2d4`): never seen beyond **2 m**; within 2 m only when `G` himself stands on shadow
   ground. A guard on normal ground does not see a hidden player even next to him.
3. The **near radius** depends on `P`'s gait: 1.5 m walking or standing (gait below 3), 3 m jogging or faster (5.2
   m for a cop and a running target). Within it only the line of sight counts, all round, unless `P` is hidden but
   off shadow ground (the [grace](#leaving): `Human_IsHiddenFromBrain`), when he must also be in the cone.
4. Beyond it, `P` must be in the **view cone**: direction · `G`'s facing above cos(FOV / 2) (FOV brain `+0x12c`,
   `BrSetFOV`, full width; 110° when a brain is made), and in **line of sight** (`Human_HasLineOfSight`, 1.7 m and 1.0
   m rays that pass fences and glass, [Sight](ai.md#sight)).

There is **no light level and no crouch**: the only "dark" is the shadow-ground flag, and sneaking only matters by
the gait (walking keeps the near radius at 1.5 m). A new enemy found by the scan is sent to `G` as event `0xb`
(scripts see it as message `0xb`, `SeeThePlayer` below) and to the radar (`HUD_RadarMarkEnemies`). The separate
spot test (`Brain_SpotPlayers`, `0x0028ac80`, every 1.5 s) uses `Human_CanSeeHuman` with 3 m and 2 m and the same
hidden rules; it says the gang's "spot" line once (when the enemy-spotting switch, `CfgSetEnemySpotting`, game state
`+0x56f8`, is on) and sends event 10; it does not start a fight.

**Hearing** is separate: noises (event `0x17`) reach a brain within `+0x134` (50 m), violence (event `0x14`) within
`+0x138` (20 m). The hangout's guards get `GangSetHearRange(gang, true, 3)`: they **hear a fight only within 3 m**,
so a stealth kill out of their sight goes unnoticed; noises still carry 50 m ([Distractions](#distraction)).

### Spotted: the fight and the call {#spotted}

`ScoutTactic_OnEvent` (`0x0031a818`, events 1, `0xb` and `0x10` of a member, consumed), confirmed (code):

1. Who: for `0xb` (a new enemy) and `0x10` (an attack coming), the human in the event; for 1 (hit), the hitter when
   it counts as an attack, or when he is a threat in line of sight. Nothing for a cuffed member, a friend, or one he
   may not engage (`Brain_MayEngage`).
2. A member with no goal does nothing more. A thrower hitting him first calls for help (done if answered); for a
   seen enemy, a help call within 15 m (`Gang_BroadcastHelpCall`).
3. Already fighting: on 1 or `0x10`, the human becomes his target. Chasing: back to his Scout goal. A top goal other
   than Scout, PathScout, Investigate (`0x5e`) or HelpRespond (`0x1d`): nothing; Investigate and HelpRespond are
   popped.
4. With a human: **`GoalMelee`** on himself (he fights), then the **call**: only when a responder spawner exists (a
   spawner in state 9 or 10, [Spawners](ai.md#spawners)), the enemy's gang has no call timer running (gang
   `+0x5f0` = 0) and no other caller is active (game state `+0x284`, one caller at a time): he becomes the caller and
   gets **CallGang** (`0x6e`) with radius = the tactic's range (10 m in level87; twice his brain `+0x140` for a path
   scout or a negative range), the two call bytes, and the phone option on. The spotter is noted in brain
   `+0x260`/`+0x264`.
5. Without a human (a hit from someone he cannot see): **Investigate** (`0x5e`) at the hitter's position (5 m, gait
   2, line 13, two looks), replacing an Investigate already on top.

**The call** (`CallGangGoal_Process`, `0x002d6638`): blip type 6 in **mode 4** (icon 353 with a flashing ring 33),
a spinning icon over him, line `0xa9` and the call clip `0x29c`; he runs (gait 5) to the farthest phone flag
(activity 6) within the radius, or a call spot beside him on his turf; there line `0x16` and clip `0x29d`, then
`Responders_QueueGangCall(count, delay, point)` and **`Gang_SetSecondWantedTimer(player's gang, 10 s)`**: the
player's gang now has its second wanted timer (the **orange** arc round the radar, [HUD](hud.md#fn-radar-frame)).
Killing or knocking out the caller before he reaches the phone stops the call. The first call can also queue tutorial
hint `0x17`. Confirmed (code).

`ScoutTactic_Process` (`0x0031b030`) every 200 ms pushes `GoalMelee` on any member with enemies and no fight or chase,
and sets the gang's alert state to whether any member fights or chases.

**What `CheckIfWanted` reads.** `CheckIfWanted` is not a binding: it is a function of level87's chapter 5 script.
Every 500 ms it calls `GangIsWanted(GangWarriors, nil)`; the explicit `nil` second argument reads as **false**
(`0x00409318` returns 0 for a nil argument that is present), so it asks about gang `+0x5f0`, the **second timer
the scout's call sets**, not the police one. True → `ch5.YouWereSpotted` → after 1.5 s `HUDLaunchMissionFailed`
with the chapter's failure string. So in the hangout, being seen is survivable; **a completed call fails the
mission**. The tactic's own callback `ch5.StealthSpotted` is empty. Confirmed (code) for the binding; the script's
calls read from the disc.

### The stealth kill {#stealth-kill}

The approach and the takedown, confirmed (code) at `0x0027e040`, `0x0027da10`, `0x0027dc00`, `0x0025eb00` and
`0x00264738`; the clips, damage and runtime check are on [Combat moves: stealth](combat-moves.md#stealth).

1. **L1 while hidden** (`Player_OnL1Pressed` / `Player_OnL1Held`): picks a target as usual
   (`Player_LockOnTarget`) and sets state `0x8` (the L1 target bit), but **does not lock on** (`Player_LockOn` is
   skipped while hidden) and does not enter the fight stance, so the player keeps the stealth walk. Letting go of
   L1 clears `0x8` (and sets `0x20000000`, the anim rebuild) and a re-lock delay (record `+0xf4` = now + 264 ms). L1
   while sprinting does nothing.
2. **The creep**: hidden with a target (`Human_IsStalkingTarget`, states `0x8` and `0x200000`) the idle and walk mix
   in the ready clips 635 `STEALTH_READY_IDLE` / 636 `STEALTH_READY_WALK` (`Human_SetStealthReadyBlend`): weight 1
   within the kill clip's far range (637: 1.5 m) of the target, falling linearly to 0 at 1 m beyond it. The speed is
   the hidden walk's (no slower gait is set), and stepping off the shadow with the target keeps him hidden for the
   [4 s grace](#leaving); a guard's scan still ignores him beyond 2 m ([Seen or not](#seen)).
3. **The press**: with state `0x8` held, **square** (a tap, `PlayerCmd_IsSquarePressOnly`), **cross** held
   (`IsCrossLongHold`) or **circle** (hold or tap) runs the kill test instead of the normal moves; with no target or
   a failed test the press does nothing. Without L1 the buttons do the normal strike, kick and grab.
4. **The test**: a target; the player in the target's **back quadrant** (`Human_GetSideOf(target, player)` = 2, the
   rear 90°) and the target in the player's **front quadrant** (side 0, within 45° of his facing);
   `Human_CanBeGrabbedBy(target, player)`; a clear way (`Human_IsWayClearTo`). Being unaware is not tested as such:
   the target need only face away. The power cost is on [Combat moves](combat-moves.md#stealth).
5. **The kill** (`Player_StartStealthKill`): clip 637 bare-handed, 639 with a set-1 weapon (knife), 641 with set 2 or
   3; any other held set is dropped first (637). The victim's human flag `0x8` is cleared (knocked out, not wounded)
   and `Attack_StartPaired(player, target, clip, 1, 0x400000)` plays the pair (victim 638 / 640 / 642). Deals 3000:
   the guard is knocked out, and his gang's message 18 (`0x12`, died or knocked out) reaches the script.
6. **After**: the pair's end (`Human_BreakPair`) clears the hidden state; the next ground snap hides the player again
   if he still stands in shadow and nobody hunts him. A guard more than 3 m away does not hear it in the hangout
   (`GangSetHearRange`, [Seen or not](#seen)); one who sees it gets the scan's new enemy and fights
   ([Spotted](#spotted)). Inferred for the re-hide (from the callers); the rest confirmed (code).

The AI never stealth kills ([Combat moves](combat-moves.md#stealth)).

### Distractions {#distraction}

Picking up and throwing are on [World objects: pickable objects](objects.md#pickable), [World objects:
throws](objects.md#throws) and, for an AI thrower, [the ThrowObject goal](ai-goals.md#goal-throw-object). What a
throw does to a guard, confirmed (code) at the addresses cited:

1. **The noise.** Each contact of a thrown object with something (`ThrownObject_OnContact`, `0x00393538`) reports
   two noises (`AI_ReportNoise`, `0x002936a8`): one within 30 m about the thrown object, one within the impact
   sound's far distance (30 m when there is none) about the thing it struck. A noise is event **`0x17`** with the
   thrower as offender, sent to every human whose noise hearing (brain `+0x134`, 50 m) reaches it
   (`AI_AlertNearby`, `Brain_IsInHearRange`). A thrown object that breaks a world object makes a third one
   (`WorldObject_OnImpact`, `0x003939a8`). A thrown object hitting the guard himself is a hit instead (event 1,
   [Spotted](#spotted) step 5: he investigates the thrower's position if he cannot see him).
2. **Who reacts** (`Brain_MayReactToCrime`, `0x002913c0`): his **investigate response** (brain `+0x224`,
   `GangSetInvestigateResponse`, `BrSetInvestigateResponse`) must be non-zero and he must not be scripted "dead"
   (`BrDead`, brain `+0x09`). Under the Scout tactic he reacts only while his top goal is Scout or PathScout (so not
   while fighting, chasing or calling); an Investigate or ReactNoise already on top is popped for the new one.
3. **The goal** (`GangBrain_OnCrimeSeen`, `0x00304c78`): unless two gangs fight nearby, **Investigate** (`0x5e`) at
   the noise's position (the nav snap toward the thrower is not traced), radius 5 m, with **one look-around for a
   scout** (`0x00510ade` = 1; two for others, `0x00510add`). A guard with a Mark goal only turns to look
   (ReactNoise, [AI goals](ai-goals.md#goal-react-noise)).
4. **Investigating** (`InvestigateGoal_Process`, `0x002d0100`): he stops speaking, says line `0xb7` or `0x24` and
   turns to the point; beyond 5 m he **runs** (gait 4) to a random reachable point within 1 m of it on his turf
   (straight to the point after 6 failed picks); within 5 m he plays the scouting clip `0x29c` (or anim `0x29e`)
   after 500-750 ms; while looks remain he walks to random points within 3.75-5 m of the noise (gait 2, pausing
   1-1.5 s) and looks again; when they are spent (a scout: after his one look) a closing line (`0x18` or `0xb8`)
   and the goal ends. He does not step onto shadow ground within 3 m of a hidden thrower: from there he only looks
   around. The moment his enemy list is not empty (the player seen) the goal ends and the scout rules take over
   ([Spotted](#spotted)).
5. **Timers**: his first investigation starts a 30 s window (brain timer A) and counts in brain `+0x2d0`; from the
   third in that window (the fifth for class 10) he only turns toward the noise (the counter's reset with the window
   inferred). When Investigate ends the Scout goal resumes and he goes back to his post (gait 3, or gait 2 while a
   hidden player is within 10 m).

So a distraction pulls a guard away for the run, a look and a short search (a few seconds), leaving his post and his
back open; his view cone and scan keep working the whole time.

### The hangout tutorial (level87, checkpoint 5) {#level87}

Chapter 5 of `level87` (`level87_chap5_club.lua`) is the stealth tutorial. What its functions set up, in our words
(values from the disc; the hint strings are `LEVEL87.MS_C5_*` ids):

- **Every guard gang** (`GangStealth01`, `02`, `03`, `05`): `GangSetRespondPercentage(gang, 100)`,
  `GangSetHearRange(gang, true, 3)` (fights heard within 3 m), `GangSetInvestigateResponse(gang, 0)` until its lesson
  turns it to 1, and `TacticScout(gang, 10, 0, 10, 0, 20, "ch5.StealthSpotted")`: posted guards, no roaming, a
  10-responder call through a phone within 10 m. `DefaultGang` keeps them on the club's turf box.
- **Sight**: the first guard (`Stealth01.S01`) gets `HuSetLOSRange` 90 m (60 m after his lesson) and
  `BrSetFOV(270)`; guards 3 and 5 get 6 m (`TBLOS`), guard 3 14 m during the distraction lesson.
- **Responders**: `AddDesRespond` (the level script) makes the responder gang whose spawner the call needs.
- **Failure**: `CheckIfWanted` polls every 500 ms ([above](#spotted)). A second watch: the puking guard
  (`Stealth02.S02`), while he walks to his post, has message `0xb` → `ch5.PukerSawPlayer`, which fails the
  mission 2 s later without any call.
- **Hide lesson**: entering `volCh5HB01` (a Warrior) locks a tutorial camera and shows the hide hints (`MS_C5_4`,
  `MS_C5_5`) with an objective marker (`fObjTutorial01` / `volObjTutorial01`); at the marker the first guard gets
  his `TacticScout`, investigate response 1, and his radar blip flashes (`HUDSetRadarObjectFlash`, `MS_C5_6`).
- **Spotted guard 1**: message `0xb` on `S01` (`ch5.SeeThePlayer`) or a hit (message 1, `ch5.DamageByPlayer`)
  clears the lesson's handlers and issues his `TacticScout` again, so from then on he fights and calls as above.
- **Kill lesson**: hints `MS_C5_7` then `MS_C5_8` (after `ch5.TutTime`, 4.5 s); the first guard carries a trigger
  sphere (`TriggerSphereCfg(S01, true, 2, true, 500)`: 2 m, with the clear-view test): inside it the hint becomes
  `MS_C5_9` (message 3), leaving it puts `MS_C5_8` back (message 4). His gang's message 18 (`ch5.Stealth01Died`,
  knocked out or dead) ends the lesson: the Warriors follow again (`IssueWarriorCommand(0, 1)`).
- **The puking guard** (`volPuker`): a short locked-camera scene (`GoalPlayDynAnimation` with `puke_bendover.anm`,
  with `CfgSetEnemySpotting(false)` called for its length), then he walks to his post `fStealth02` and
  becomes a scout there (`ch5.Stealth2Scout`, investigate response 1).
- **Distraction lesson** (`fObjTutorial02`): the Warriors are parked, guard 3 faces away (`HuBlockLook`) with
  investigate response 1, sight 14 m; the player's movement is locked (`HuLockPadMovement(true)`) with only
  commands 6, 7, 8, 10 and 16 enabled ([Input: commands](input.md)), and a marker shows where to throw
  (`fDistract`, hint `MS_C5_11`). The check is the script's, not the AI's noise: `volObjTutorial03` takes message 6
  (sent to every volume holding the thrower when his thrown object strikes something, `0x00393538`), and
  `ch5.CheckDistract` asks whether the object is inside `volDistract`. If so the script itself moves the guard:
  `BrDead(S03, true)`, `TacticClear`, `BrFlush`, `GoalMoveToFlag(S03, fScout3Dest, gait 4)`; at the flag he says
  command line 23 (`SoundPlayCommand`), gets his brain back and his `TacticScout` again; 3 s later the pad is freed
  and the next objective (`fObjTutorial03`, hint `MS_C5_12`) leads past him. Later he walks back to his post
  (`TacticMoveToFlag` to `fStealth03`, gait 2) while hint `MS_C5_13` shows. The engine's Investigate
  ([Distractions](#distraction)) also runs for him whenever his investigate response is 1 and the script has not
  taken his brain.

## Coney's implementation

- **Shadow ground and the hidden state** (`repo:src/human/human_hiding.cpp`, `repo:src/ai/hiding.h`): the ground snap
  keeps the floor triangle's flags, and flag `0x10` is shadow. Player 1 walking onto it hides (state `0x200000`)
  unless he carries a molotov or a hunter already sees him; he stays hidden while on it, and for 4 s after leaving it
  while walking with a target; a sprint ends it at once. Hiding shakes off the hunters who lost sight of him.
- **Sneaking** (`HumanAnimator::setStealthStyle`): while hidden the move style swaps to the stealth set (630 idle, 631
  start, 633 walk at its own speed), with 634 in and 394 out from the idle.
- **The HUD cue** ([HUD](hud.md#coneys-implementation)): the radar turns blue while he is hidden and back to grey,
  blending over 500 ms.
- **Seen or not** (`repo:src/ai/enemy_scan.h`, [AI: the enemy scan](ai.md#enemy-scan)): `Human_CanSeeHuman` and the scan
  see a hidden human only within 2 m.
- **Scouts and the call** (`repo:src/ai/tactic_scout.h`, [AI: Coney](ai.md#coney)): TacticScout's guards keep their
  posts and scan every 500 ms; one who spots, is hit by or is warned of an enemy fights him, and when the level allows
  it runs to the farthest phone flag in range and, 1.5 s later, starts the enemy gang's second wanted timer
  ([Crimes](crimes.md#coneys-implementation)), which `GangIsWanted(gang, false)` reads.

Not yet: the other players' Warriors hiding and their hide order, the camera's raised look-at, the half-volume
footsteps, the scouts' clips, glances, roaming and radar blips, the help call, the investigation of an unseen hit, the
caller's lines and clips, the responders the call brings (only logged), the stealth kill, distractions and level87's
checkpoint 5 as a whole.

## Open questions

- Who plays the prepared hidden line (`0x57`-`0x59`) and when.
- A runtime check of the hide rules (blue radar, a guard 1.9 m away on normal ground not seeing a hidden player).
- Where exactly an Investigate for a noise ends up: `GangBrain_OnCrimeSeen` snaps the noise point to the nav mesh along
  the line to the thrower (`Nav_FindPolygonUnderThunk`, `0x00252ae0`, not traced).
