# AI goals: the code of every goal class

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-07); each row states its evidence level.

## Purpose

The function-by-function index of the goal classes (`0x0029f000`-`0x002f9e00`). [AI humans](ai.md#goals) explains the
goal stack, the vtable layout (`+0x0c` type, `+0x14` the class name, `+0x24` Start, `+0x2c` End, `+0x34` Resume,
`+0x44` Process, `+0x4c` event) and the goals that matter most; this page names every function of every class and says
in a line what it does. The rest of the AI code is on [AI code index](ai-code.md).

## Goal classes, by address {#by-address}

### Shared goal helpers {#goal-helpers}

Helpers the goals below call, at the start of the goals' file.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0029f230` | `Brain_PickBestEnemy` | helper | the best-scoring valid enemy in the brain's enemy list ([the score](ai.md#enemy-score)) | confirmed (code) |
| `0x0029f3c8` | `Ai_RandomNavPoint` | helper | a random point at a distance from a position, in any direction or within ± an angle of the facing; up to 5 tries for one on the navigation areas (`Nav_FindArea`), optionally connected to the start (`Nav_IsConnected`), dropped to the ground; 1 when found | confirmed (code) |
| `0x0029f710` | `Ai_SetRunningMoveSpeed` | helper | when the brain has actions and the current one is a move action, changes its speed; 1 when done | confirmed (code) |
| `0x0029f790` | `Ai_SetRunningMoveTarget` | helper | when the current action is a move action, gives it a new target point; 1 when done | confirmed (code) |

### BossLizzies (type 0x94) {#goal-boss-lizzies}

A scripted gunman for the Lizzies' fight, using Vargas's voice (`vags/speeches/l55`) and `l55_gun_*` aim clips. It
is **unused**: only the unused Boss tactic and the uncalled `GoalBossLizzies` push it; the shipped level 55 fight is
`TacticBossScenarioB` ([AI: The boss fights](ai.md#boss-fights)). The goal types `0x93`-`0x96` below are all in this
case. Confirmed (code) at the cited addresses.

**BossLizzies' state machine** (`0x0029fe18`). The state is goal `+0x10`, its deadline `+0x14` (ms on the game clock),
and the shots left `+0x1e`/`+0x20`. Every state waits for its deadline. The tunables are globals set only by
`AdjustBossLizzies(a, b, c, d)`:

- shots per volley `0x006e9424` (a);
- the gap between shots `0x006e9426` (b);
- the aim time `0x006e9428` (c);
- `0x006e942a` (d), which nothing reads;
- the pause `0x006e942c` (1000 ms);
- the reload time `0x006e942e` (1000 ms);
- the taunt-or-retreat split `0x006e9430` (50).

The gang it orders is found by name, `LizSpn`. The aim point is player 1. The shot's victim is the object
BossTactic's 25 m ray hit (`0x00510bbc`, [Boss tactic](ai-code.md#t1-boss)).

| State | On its deadline | Next, after |
| --- | --- | --- |
| 0 | `Tactic_Attack` for `LizSpn`, turn to player 1, line `l55_t3_002a`-`d` (one of four), reload the volley | 1, 2 s |
| 1 | turn to player 1, line `002a`-`d` | 9, the pause |
| 9 | (waits for the actions to finish) turn to player 1 | 10, the aim time |
| 10 | turns to player 1 each update | 11, the aim time |
| 11 | - | 8, 300 ms |
| 8 | anim action 668 with argument 3 (fire) | 5, 500 ms |
| 5 | **the shot**, below | 9 after the gap and a new `Tactic_Attack`, while shots are left; else 6, 1 s |
| 6 | clears the actions, anim action 668 with argument 0 (reload) | 2, the reload time |
| 7 | as 6 (BossTactic's damage event sets it, 1 s on) | 2, the reload time |
| 2 | (waits for the actions) reloads the volley, turns | 3, the pause |
| 3 | turns, anim action 668 with argument 1; when a roll of 0-99 is above the split, a line `002a`-`d`, else `Tactic_AvoidEnemies(3 m, 10 m)` for the gang and line `001a`-`d` | 9, the pause |

The shot puts the `part_gun_flash` effect at the gun. Then:

- **A human hit (flag `0x40`)**:
    - a `vags/weapons/gunshot_hit_01`-`05` sound;
    - `Human_SetDamage(100)`;
    - `part_blood_spray` and a 0.2 m area effect for 500 ms;
    - line `004a`-`d` when the victim's brain has no current target;
    - when the victim is player 1, straight to state 6.
- **Anything else**: a `gunshot_miss_01`-`05` sound, and message 1 (a hit) to an object with flag 8.

**BossLuther** (type `0x95`): its Process (`0x002a0b00`) is `return 0`, so the goal does nothing.

**BossChatter** (type `0x93`, `0x002a0c78`, `0x002a0fb8`). The init sets 500 health and turns the threat and damage
responses off. The stage picks the first state:

- **Stage 0** (states 100-107): walks `fBalcony1a`, `2a` and `3a`. Within 100 m of player 1 it mans a weapon pile.
- **Stage 1** (states 0-26): walks the balcony flags `fBalcony1a`-`3d` by indices 6, 7 and 8, and calls the Lua
  functions `StopBalconyShake1` and `CollapseBalcony2`. It mans weapon piles whose kind and time come from health / 1200:
  0 gives kind 1 for 5 s, 1 gives kind 2 for 3 s, 2-3 give kind 3 for 2 s. Between piles it steps aside at random.
- **Stage 2** (states 200-202): untouchable, with heavy push. Within 50 m of player 1 it mans a pile every 3 s.
- **Stage 3** (states 300-303): untouchable. It travels the paths `TPath1`, `TPath2` and `TPath3` in turn, each forward
  then back, and mans a pile between them.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0029fb60` | `BossLizzies_Adjust` | setter | stores four values from the script (`lua_AdjustBossLizzies`) in globals `0x006e9424`-`0x006e942a` and resets the 1000 ms, 50 and 1000 ms timers the Process reads | confirmed (code) |
| `0x0029fba8` | `Goal_BossLizzies` | binding | `GoalBossLizzies`: pushes the goal | confirmed (code) |
| `0x0029fc18` | `BossLizziesGoal_Init` | init | vtable `0x0053f550`; Vargas's `vags` anims, an item put in his hand and kept, a first deadline 1 s on, a volume box found by name, and the line-test cache cleared | confirmed (code) |
| `0x0029fe10` | `BossLizziesGoal_InitNop` | stub | empty; called by the init | confirmed (code) |
| `0x0029fe18` | `BossLizziesGoal_Process` | Process | the state machine: each state starts an attack tactic (`Tactic_Attack`) or a speech line (one of four at random) and waits for its deadline | confirmed (code) |

### BossLuther (type 0x95) {#goal-boss-luther}

Luther's boss goal; its Process is `0x002a0b00`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a0a68` | `BossLutherGoal_Push` | pusher | allocates the goal on a human's brain and pushes it (from `0x003089b0`) | confirmed (code) |
| `0x002a0ac8` | `BossLutherGoal_Init` | init | vtable `0x0053f4f0`, no time limit, state 1 | confirmed (code) |

### BossBigMo (type 0x96) {#goal-boss-bigmo}

Big Mo's boss goal.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a0b08` | `BossBigMoGoal_Push` | pusher | allocates and pushes the goal (from `0x003089b0`) | confirmed (code) |
| `0x002a0b68` | `BossBigMoGoal_Init` | init | vtable `0x0053f490` | confirmed (code) |
| `0x002a0b98` | `BossBigMoGoal_Process` | Process | starts a Melee goal once (`Goal_Melee`), then stays | confirmed (code) |

### BossChatter (type 0x93) {#goal-boss-chatter}

The Chatter boss.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a0c00` | `BossChatterGoal_Push` | pusher | allocates and pushes the goal with a stage argument (from `0x003089b0`) | confirmed (code) |
| `0x002a0c78` | `BossChatterGoal_Init` | init | vtable `0x0053f430`; finds 12 flags, sets 500 health and the stage | confirmed (code) |
| `0x002a0fb8` | `BossChatterGoal_Process` | Process | a state machine of move-to-flag, man-the-weapon-pile and travel-path goals, by stage and health | confirmed (code) |

### BossDiego (type 0x48) {#goal-boss-diego}

The Diego boss ([Diego and Vargas](ai.md#boss-diego-vargas)). Its Process runs a small script: an array of s16 (op,
argument) pairs at goal `+0x40`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0029f810` | `BossDiego_GangAvoid` | helper | for every free member of gang 0 (the player's), pushes an AvoidEnemies goal (5 m, 10 m); first loads clip set `0x00562480` | confirmed (code) |
| `0x0029f8e8` | `BossDiego_GangStopAvoid` | helper | for every free member of gang 0, clears the actions and pops goals down to and including AvoidEnemies (type `0x20`) | confirmed (code) |
| `0x0029f9d0` | `BossDiegoScript_FindLabel` | helper | the index of the label op (`0x96`) with the given argument, within 64 pairs; −1 if none | confirmed (code) |
| `0x0029fa28` | `BossDiegoScript_Next` | helper | fetches the next (op, argument): a negative op jumps back relative, `0x97` is a goto to a label, `0x96` a label to skip | confirmed (code) |
| `0x002a1aa8` | `Goal_BossDiego` | binding | `GoalBossDiego`: builds the goal | confirmed (code) |
| `0x002a1df8` | `BossDiegoGoal_Process` | Process | the script interpreter: ops 100 and up set the threat response, push melee, stun, god mode, avoid, flags, porcelain weapons in hand and so on | confirmed (code) |
| `0x002a30c0` | `BossDiego_SetAttackTable` | helper | points the brain's attack weight override (`+0x208`) at table n of `0x00510c40` (45 bytes each) | confirmed (code) |

### ManWeaponPile (type 0x52) {#goal-man-weapon-pile}

Stands at a pile and throws objects from it at the player, with Vargas's lines.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a30e8` | `Goal_ManWeaponPileSimple` | binding | `GoalManWeaponPileSimple` | confirmed (code) |
| `0x002a31f0` | `Goal_ManWeaponPile` | binding | `GoalManWeaponPile` | confirmed (code) |
| `0x002a3320` | `ManWeaponPileGoal_Init` | init | vtable `0x0053f370` | confirmed (code) |
| `0x002a3498` | `ManWeaponPileGoal_Process` | Process | picks up an object from the pile, aims and throws it, says a line | inferred |
| `0x002a3d90` | `ManWeaponPileGoal_End` | End | when the goal has a Lua callback (`+0x20`), calls it through the script system `[0x00512b04]` with the human's handle ([Scripted goals](ai.md#scripted)) | confirmed (code) |

### ObjectThrower (type 0x4e) {#goal-object-thrower}

Throws objects found near him at a target.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a3e50` | `Goal_ObjectThrower` | binding | `GoalObjectThrower` | confirmed (code) |
| `0x002a3f50` | `ObjectThrowerGoal_Init` | init | vtable `0x0053f310` | confirmed (code) |
| `0x002a4138` | `ObjectThrowerGoal_End` | End | leaves the fight stance, clears human `+0x128` and object flag `0x40000`, calls the goal's Lua callback with the human's handle | confirmed (code) |
| `0x002a4230` | `ObjectThrowerGoal_Process` | Process | fight stance; ends when a hostile is visible within the stop radius or after `maxThrows`; turns to a random target flag, aims and presses cross after pace/2 to pace seconds; empty-handed fetches the nearest weapon with GetItem ([ObjectThrower](ai.md#ai-objects)) | confirmed (code) |
| `0x002a4aa0` | `LineTestCache_Reset` | reset | on a full reset clears the handle at `0x006e93f8` that `BossTactic_Process` caches | inferred |
| `0x002a4ac8` | `LineTestCache_StaticInit` | static init | from the static initialiser table (`0x00534174`): calls the reset | confirmed (code) |

### AreaWalker (type 0x47) {#goal-area-walker}

Walks from random point to random point inside an area.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a4ae8` | `Ai_RandomPointNear` | helper | a random point around a position, up to 17 tries for one on the navigation areas (from `0x002bff08`) | confirmed (code) |
| `0x002a4cb8` | `Goal_AreaWalker` | binding | `GoalAreaWalker` | confirmed (code) |
| `0x002a4dc8` | `AreaWalkerGoal_Init` | init | vtable `0x0053fa90`; the area and the pace | inferred |
| `0x002a4e90` | `AreaWalkerGoal_End` | End | restores the turn boost | confirmed (code) |
| `0x002a4eb0` | `AreaWalkerGoal_Process` | Process | picks a new point in the area (`Ai_RandomNavPoint`), walks there, idles a while | inferred |

### HookerLogic (type 0x39) {#goal-hooker}

A street walker's idle behaviour.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a5360` | `Goal_Hooker` | binding | `GoalHooker` | confirmed (code) |
| `0x002a53d8` | `HookerGoal_Init` | init | vtable `0x0053fa30` | confirmed (code) |
| `0x002a5430` | `HookerGoal_Process` | Process | stands at her spot, turns to passers-by, plays fidgets and lines | inferred |

### PathBlocker (type 0x4b) {#goal-path-blocker}

Stands in the player's way and pushes him back.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a57e8` | `PathBlockerGoal_Push` | pusher | allocates and pushes the goal with its points and options | confirmed (code) |
| `0x002a58a8` | `Goal_PathBlocker` | binding | `GoalPathBlocker` | confirmed (code) |
| `0x002a5958` | `PathBlockerGoal_Init` | init | vtable `0x0053f9d0` | confirmed (code) |
| `0x002a5a90` | `PathBlockerGoal_Process` | Process | moves between the player and the blocked way, shoves him when close | inferred |
| `0x002a5fd8` | `PathBlockerGoal_End` | End | calls the goal's Lua callback (when set) with the human's handle | confirmed (code) |

### PedLogicPath (type 0x44) and PedLogicFlag (type 0x45) {#goal-ped-logic}

Pedestrians walking a path or between flags.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a6098` | `Ped_PickGait` | helper | a gait argument; 7 means a random one of 1-6 | confirmed (code) |
| `0x002a60e0` | `Goal_PedestrianPath` | binding | `GoalPedestrianPath` | confirmed (code) |
| `0x002a6178` | `PedPathGoal_Init` | init | vtable `0x0053f970`; the path, the gait (`Ped_PickGait`) | confirmed (code) |
| `0x002a62d8` | `PedPathGoal_Process` | Process | walks the path's points in order, idling at some | inferred |
| `0x002a66c0` | `Goal_PedestrianFlag` | binding | `GoalPedestrianFlag` | confirmed (code) |
| `0x002a6760` | `PedFlagGoal_Init` | init | vtable `0x0053f910`; the flag set, the gait | confirmed (code) |
| `0x002a68b0` | `PedFlagGoal_Process` | Process | walks to a flag, uses it, picks another | inferred |

### PedSeesDanger (type 0x53) {#goal-ped-sees-danger}

A pedestrian's reaction to a fight or a danger.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a6b50` | `PedSeesDangerGoal_Push` | pusher | allocates and pushes the goal | confirmed (code) |
| `0x002a6be8` | `PedSeesDangerGoal_Init` | init | vtable `0x0053f8b0` | confirmed (code) |
| `0x002a6c88` | `PedSeesDangerGoal_Process` | Process | looks at the danger, backs off or flees, cowers when close | inferred |

### TravelFlagNet (type 0x46) {#goal-travel-flag-net}

Travel along a net of linked flags, with the net's helpers.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a7328` | `FlagNet_Clear` | helper | `FlagNet_Clear` | confirmed (code) |
| `0x002a73a8` | `FlagNet_FindNode` | helper | `FlagNet_FindNode` | confirmed (code) |
| `0x002a73f0` | `FlagNet_NodeCount` | helper | the number of nodes in a net | inferred |
| `0x002a7458` | `FlagNet_AddNode` | helper | `FlagNet_AddNode` | confirmed (code) |
| `0x002a7538` | `FlagNet_GetMask` | helper | the net's mask of flags, used by the nearest-flag search | inferred |
| `0x002a75b8` | `FlagNet_Build` | helper | builds the net from a start flag by following the links | inferred |
| `0x002a7810` | `FlagNet_NextNode` | helper | the next node towards the goal node | inferred |
| `0x002a78c8` | `FlagNet_StartTraverse` | helper | `FlagNet_StartTraverse` | confirmed (code) |
| `0x002a7a40` | `TravelFlagNetGoal_Init` | init | vtable `0x0053f850` | confirmed (code) |
| `0x002a7a88` | `TravelFlagNetGoal_Process` | Process | moves from node to node of the net | inferred |
| `0x002a7c48` | `FlagNet_Validate` | helper | `FlagNet_Validate` | confirmed (code) |

### CallPolice (type 0x6d) {#goal-call-police}

A civilian runs to a phone flag and calls the police ([Crimes](crimes.md)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a7d08` | `Goal_CallPolice` | binding | `GoalCallPolice` | confirmed (code) |
| `0x002a7d58` | `CallPoliceGoal_Push` | pusher | allocates and pushes the goal | confirmed (code) |
| `0x002a7e20` | `CallPoliceGoal_Init` | init | vtable `0x0053f7f0` | confirmed (code) |
| `0x002a7e98` | `CallPoliceGoal_Start` | Start | registers the caller with the game state (`0x0041cfe8`), a deadline 1 s on, turns to the enemy | confirmed (code) |
| `0x002a7f70` | `CallPoliceGoal_End` | End | unregisters the caller, removes the spinning icon and the radar blip, releases the phone flag | confirmed (code) |
| `0x002a7ff8` | `CallPolice_FindPhone` | helper | the nearest phone flag (by activity), else any, and its position (from `Goal_ReportCrime`) | confirmed (code) |
| `0x002a8190` | `CallPolice_SayOnce` | helper | says line `0x7f` once (`+0x50`) | confirmed (code) |
| `0x002a81f8` | `CallPolice_OnCrimeSeen` | event | from `CivilianBrain_OnCrimeSeen`: a broken glass pane within 4 m of the phone flag sends the caller to another flag | confirmed (code) |

### Interact (type 0x71) {#goal-interact}

Two pedestrians meet and greet (the hi-five); one pair at a time through the lock at `0x006e9438`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a8e90` | `InteractGoal_Push` | pusher | allocates and pushes the goal | confirmed (code) |
| `0x002a8ef0` | `InteractGoal_Init` | init | vtable `0x0053f790` | confirmed (code) |
| `0x002a8f48` | `InteractGoal_Start` | Start | clears `+0x74`, then Resume | confirmed (code) |
| `0x002a8f78` | `InteractGoal_Resume` | Resume | checks the partner is still there (else state `0xb`, ended) and goes on with the greeting | confirmed (code) |
| `0x002a90a0` | `InteractGoal_Suspend` | Suspend | anim override `0x2a1`, looks at the partner, releases the lock `0x006e9438` when his | confirmed (code) |
| `0x002a9160` | `InteractGoal_End` | End | Suspend | confirmed (code) |
| `0x002a9188` | `Interact_FindPartner` | helper | a free pedestrian nearby to meet; takes the lock | inferred |
| `0x002a9508` | `PedInteractGoal_Process` | Process | walk to each other, face, play the greeting clips | inferred |

### PedReactNoise (type 0x6c) {#goal-ped-react-noise}

A pedestrian's reaction to a noise.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a9d58` | `PedReactNoiseGoal_Push` | pusher | allocates and pushes the goal on the human of an object | confirmed (code) |
| `0x002a9df8` | `PedReactNoiseGoal_Init` | init | vtable `0x0053f730` | confirmed (code) |
| `0x002a9e40` | `PedReactNoiseGoal_Start` | Start | whether he can see the source (`Human_CanSeeHuman`, 9 m / 4 m) | confirmed (code) |
| `0x002a9ec0` | `PedReactNoiseGoal_Process` | Process | turns to the noise, looks, may walk off or flee | inferred |

### PedReaction (type 0x6b) {#goal-ped-reaction}

A pedestrian's reaction to being attacked.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002aa490` | `PedReactionGoal_Push` | pusher | stops his anims and pushes the goal on the human of an object (also from the peddler when attacked) | confirmed (code) |
| `0x002aa558` | `PedReactionGoal_Init` | init | vtable `0x0053f6d0` | confirmed (code) |
| `0x002aa598` | `PedReactionGoal_Start` | Start | says line 9, a random 62-125 delay, picks the reaction | confirmed (code) |
| `0x002aa740` | `PedReactionGoal_End` | End | restores the turn boost | confirmed (code) |
| `0x002aa760` | `PedReactionGoal_Process` | Process | flees, cowers or calls the police | inferred |

### Pedestrian (type 0x69) {#goal-pedestrian}

A pedestrian walking between flags and chatting.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002aae30` | `PedestrianGoal_Init` | init | vtable `0x0053f670` | confirmed (code) |
| `0x002aaf18` | `PedestrianGoal_Start` | Start | the nearest flag (`FlagList_FindNearest`) | confirmed (code) |
| `0x002ab068` | `PedestrianGoal_Suspend` | Suspend | ends a chat unless `+0x72`; human `+0xf4` = `0xff` | confirmed (code) |
| `0x002ab0a8` | `PedestrianGoal_End` | End | ends a chat, restores the turn boost | confirmed (code) |
| `0x002ab0d8` | `Pedestrian_NextFlag` | helper | the next flag: a point within 1.75 m of it, a pace distance of 7-10 m, the following flag; human `+0xf4` = `0x13` | confirmed (code) |
| `0x002ab1e8` | `Pedestrian_EndChat` | helper | detaches the chat partner's Pedestrian goal from this one | inferred |
| `0x002ab288` | `Pedestrian_SidestepPoint` | helper | a point 15 m to one side of the human, for stepping out of the way | inferred |
| `0x002ab470` | `Pedestrian_MayChat` | helper | not chatting, fewer than 3 chats, nothing in hand | confirmed (code) |

### BumLogic (type 0x4f) {#goal-bum}

A bum who begs, takes money and gives items.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002abd38` | `Goal_Bum` | binding | `GoalBum` | confirmed (code) |
| `0x002abe08` | `Goal_BumTrigger` | binding | `GoalBumTrigger` | confirmed (code) |
| `0x002abe98` | `LoadBumAnims` | helper | `LoadBumAnims` | confirmed (code) |
| `0x002ac130` | `Bum_ReleaseAnims` | helper | releases the seven dyn anim slots | confirmed (code) |
| `0x002ac180` | `BumGoal_Start` | Start | notes his position and type, sets brain `+0x28d`, then Resume | confirmed (code) |
| `0x002ac218` | `Bum_IsBusy` | helper | whether the state bytes `+0x30`-`+0x34` show a deal in progress | inferred |
| `0x002ac270` | `BumGoal_Resume` | Resume | not pushable; `+0x284` from the state | confirmed (code) |
| `0x002ac2a8` | `BumGoal_Suspend` | Suspend | pushable again; stops a deal in progress | confirmed (code) |
| `0x002ac330` | `BumGoal_End` | End | Suspend, then restores the type and clears `+0x28d` | confirmed (code) |
| `0x002ac378` | `BumGoal_Destroy` | Destroy | unless knocked out: releases the anims, unarrests, clears the down latch | confirmed (code) |
| `0x002ac410` | `BumGoal_Trigger` | helper | `BumGoal_Trigger` | confirmed (code) |
| `0x002ac458` | `Bum_GiveItem` | helper | gives the item he holds | inferred |
| `0x002ac5e0` | `Bum_TakeMoney` | helper | takes the player's money and picks what to give | inferred |
| `0x002acaf8` | `Bum_UpdateRadar` | helper | the radar blip of a dealer bum | confirmed (code) |

### Peddler (type 0x50) {#goal-peddler}

A street seller ([Dealers](ai.md#dealer)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ad2c0` | `Goal_Peddler` | binding | `GoalPeddler` | confirmed (code) |
| `0x002ad378` | `PeddlerGoal_Init` | init | vtable `0x0053f5b0` | confirmed (code) |
| `0x002ad490` | `PeddlerGoal_Start` | Start | Resume | confirmed (code) |
| `0x002ad4b8` | `PeddlerGoal_Resume` | Resume | not pushable | confirmed (code) |
| `0x002ad4e0` | `PeddlerGoal_Suspend` | Suspend | pushable | confirmed (code) |
| `0x002ad508` | `PeddlerGoal_End` | End | Suspend | confirmed (code) |
| `0x002ad530` | `PeddlerGoal_Destroy` | Destroy | releases two dyn anim slots and restores the type | confirmed (code) |
| `0x002ad580` | `Peddler_HasAnims` | helper | both dyn anim slots loaded | confirmed (code) |
| `0x002ad5a8` | `PeddlerGoal_OnAttacked` | event | `PeddlerGoal_OnAttacked`: pushes a PedReaction | confirmed (code) |
| `0x002ad630` | `Peddler_PickCustomer` | helper | every 20 updates the nearest visible human of clip set `0x0029c508` | inferred |
| `0x002ad6c0` | `PeddlerGoal_Process` | Process | waits for a customer and sells | inferred |

### The flag list {#goal-flag-list}

A list of 128 flags (`0x006e9440`, 20 bytes each) used by the pedestrians and the spawner.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002adac0` | `FlagList_FindNearest` | helper | the nearest listed flag with an activity, optionally in a net's mask and unused (from the pedestrians, the spawner and `0x0031cce0`) | confirmed (code) |
| `0x002adc70` | `FlagList_Reset` | reset | on a full reset clears every entry and the interact lock `0x006e9438` | confirmed (code) |
| `0x002adce8` | `FlagList_StaticInit` | static init | from the static initialiser table (`0x00534178`): calls the reset | confirmed (code) |

### Melee (type 8) {#goal-melee}

[The Melee goal](ai.md#melee-goal).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ade60` | `MeleeGoal_End` | End | sets brain `+0x2d3` and calls `0x00226f70` | confirmed (code) |
| `0x002ade90` | `Melee_TryPickUpWeapon` | helper | an unarmed fighter who may not chase (`+0x265`): goes for a weapon within 20 m (10 m for some) with `Goal_GetItem` | confirmed (code) |
| `0x002ae028` | `Melee_Reposition` | helper | after a failed approach, one of four moves by distance and a draw: a shuffle (2 s), fidget `0x25b`, a wait (3 s) or a move to the target (2 s) | confirmed (code) |
| `0x002aebf8` | `MeleeGoal_Process` | Process | [Melee](ai.md#melee-goal) | confirmed (code) |

### AttackTarget (type 9) {#goal-attack-target}

Attacks one target until it is down or gone.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002aeda0` | `AttackTargetGoal_Push` | pusher | pushes the goal on one human's brain against another | confirmed (code) |
| `0x002aee48` | `AttackTargetGoal_Init` | init | vtable `0x005403f0` | confirmed (code) |
| `0x002aef78` | `AttackTargetGoal_End` | End | when it owns the target (`+0x29`), the cleanup; brain `+0x2d3` = 1, `+0x2e5` = 0 | confirmed (code) |
| `0x002aefc8` | `AttackTargetGoal_Destroy` | Destroy | the same as End | confirmed (code) |
| `0x002af018` | `AttackTarget_Release` | helper | as End, also clearing the handle `+0x24` (also from Arrested's Start) | confirmed (code) |
| `0x002af0b8` | `AttackTarget_Cleanup` | helper | brain `+0x2d3` = 1, `+0x2e5` = 0, drops the target, leaves the fight stance | confirmed (code) |
| `0x002af278` | `AttackTargetGoal_Process` | Process | pushes a fight on the target while valid; done when he is down | inferred |

### EngageEnemy (type 0xb) {#goal-engage-enemy}

[EngageEnemy: the run-in](ai.md#engage-enemy).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002af528` | `Goal_EngageEnemy` | binding | `GoalEngageEnemy` | confirmed (code) |
| `0x002af5b0` | `EngageEnemyGoal_Init` | init | vtable `0x00540330` | confirmed (code) |
| `0x002af670` | `EngageEnemyGoal_Start` | Start | arms the charge, a taunt, the timers, brain `+0x0b` + 1 | confirmed (code) |
| `0x002af8d0` | `EngageEnemyGoal_End` | End | restores `+0x0b`, clears the actions | confirmed (code) |
| `0x002afa08` | `EngageEnemyGoal_Stop` | helper | `EngageEnemyGoal_Stop` | confirmed (code) |
| `0x002afa48` | `EngageEnemyGoal_Process` | Process | [the run-in](ai.md#engage-enemy) | confirmed (code) |

### Chase (type 0xc) {#goal-chase}

Chases an enemy who runs, with a radar blip when the chaser is after the player's gang (`ChaseGoal_Push` `0x002b04f0`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b0608` | `Chase_UpdateRadar` | helper | a type-2 brain chasing a member of a player's gang gets a red radar blip on start; removed at the end | confirmed (code) |
| `0x002b07c0` | `ChaseGoal_Start` | Start | saves and sets brain `+0x28d`, `+0x12c` and `+0x265`, the blip, turns to the target and plays a gang clip | confirmed (code) |
| `0x002b0970` | `ChaseGoal_Resume` | Resume | out of sight: a move to his last point (gait 4, 2 within 8 m); the run anim set | confirmed (code) |
| `0x002b0b60` | `ChaseGoal_Suspend` | Suspend | the normal anim set, releases the flag | confirmed (code) |
| `0x002b0bf0` | `ChaseGoal_End` | End | restores the brain bytes, drops the target, removes the blip | confirmed (code) |
| `0x002b0c88` | `Chase_InvestigateCar` | event | from `PursueTactic_OnCar` `0x003181e8` on a crime event (23) at a car: unless the offender is friendly, pushes Investigate (`0x002cf8f8`, type `0x5e`) at the crime point (5 m, line 13, the offender's handle) ([Cover, cars and trains](ai.md#ai-hazards)) | confirmed (code) |
| `0x002b0ed8` | `Chase_PlayGangClip` | helper | the gang's clip `0x29c`, else anim `0x29e` after 500-750 ms | confirmed (code) |
| `0x002b0fd0` | `Chase_PickSearchPoint` | helper | a random point ahead within the search cone, reachable | confirmed (code) |
| `0x002b1230` | `Chase_Search` | helper | the target lost beyond 5 m (10 m when hidden): walk to a search point, else a gang clip | confirmed (code) |
| `0x002b1478` | `Chase_UseFlag` | helper | takes the nearest flag of activity `0x21` and turns there | confirmed (code) |
| `0x002b1860` | `ChaseGoal_Process` | Process | while the target is seen: shouts, moves in, pushes EngageEnemy within reach; lost: searches | confirmed (code) |

### ChaseSupport (type 0xd) {#goal-chase-support}

Joins a gang leader's chase in formation.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b2158` | `ChaseSupportGoal_Init` | init | vtable `0x0053fc70` | confirmed (code) |
| `0x002b21c8` | `ChaseSupportGoal_Start` | Start | joins the leader's formation, leaves the fight stance | confirmed (code) |
| `0x002b22b8` | `ChaseSupportGoal_Resume` | Resume | the run anim set `0x00563fd0` | confirmed (code) |
| `0x002b2300` | `ChaseSupportGoal_Suspend` | Suspend | the normal anim set | confirmed (code) |
| `0x002b2340` | `ChaseSupportGoal_End` | End | leaves the leader's formation | confirmed (code) |
| `0x002b2410` | `ChaseSupportGoal_Process` | Process | follows the formation slot; engages a target he may reach; a gang clip now and then | inferred |

### Fight (type 0xf) {#goal-fight}

[The fight goal](ai.md#fight).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b2cd8` | `FightGoal_Start` | Start | Resume | confirmed (code) |
| `0x002b2d00` | `FightGoal_Resume` | Resume | picks an attack kind against the target | inferred |
| `0x002b2d70` | `FightGoal_Suspend` | Suspend | releases its attack slot on the target (`0x00291178`), clears `+0x28` | confirmed (code) |
| `0x002b2dd8` | `FightGoal_End` | End | clears the actions | confirmed (code) |
| `0x002b2e28` | `FightGoal_TryTackle` | helper | the tackle try, first in the fight goal; driven by the gang's `CfgGang` value 7 and the brain's tackle meter `+0x148`; a cop picks `X1` over the tackle 75 % of the time ([The tackle try](ai.md#try-tackle)) | confirmed (code) |
| `0x002b2fc8` | `FightGoal_Reposition` | helper | while `Brain_CheckAttack` says wait: holds the attacker in a ring 4-4.75 m from the target with a move-to-human action, a taunt fidget every 1-2 s ([The reposition](ai.md#fight-reposition)) | confirmed (code) |
| `0x002b3360` | `FightGoal_TryGrab` | helper | the grab and snap try before an attack in reach: snap stick angles from the sectors, side grab to `X1`, a rear grab at `CfgGang` value 8 × 25 % ([The grab and snap try](ai.md#try-grab)) | confirmed (code) |

### Spectate (type 0x10) {#goal-spectate}

[Spectate](ai.md#spectate).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b4158` | `SpectateGoal_PickTarget` | helper | `SpectateGoal_PickTarget` | confirmed (code) |
| `0x002b4200` | `SpectateGoal_Start` | Start | `SpectateGoal_Start` | confirmed (code) |
| `0x002b42f0` | `SpectateGoal_End` | End | `SpectateGoal_End` | confirmed (code) |
| `0x002b4330` | `SpectateGoal_Process` | Process | `SpectateGoal_Process` | confirmed (code) |

### The reaction goals (types 0x12-0x1c) {#goal-reactions}

Goals for what happens to the body: grabbed, tackled, on the ground, stunned, on fire, wounded, arrested.
`Brain_WantsCounter` asks the reaction goal at brain `+0x3c` (Grounded) or the top goal (Blocking, type `0x1b`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b49f8` | `GroundedGoal_Init` | init | Grounded (`0x17`), vtable `0x00540210` | confirmed (code) |
| `0x002b4a38` | `GroundedGoal_Start` | Start | notes the attacker (brain `+0x124`) and the start time | confirmed (code) |
| `0x002b4aa0` | `GroundedGoal_WantsCounter` | helper | Grounded never counters: 0 | confirmed (code) |
| `0x002b4aa8` | `GroundedGoal_Process` | Process | Grounded: after about 1.9 s queues kind 42 on himself (a get-up, inferred) ([reactions](ai.md#fight-reactions)) | inferred |
| `0x002b4b88` | `StunnedGoal_Init` | init | Stunned (`0x18`), vtable `0x005401b0` | confirmed (code) |
| `0x002b4bb8` | `StunnedGoal_Process` | Process | Stunned: no target; done when no longer stunned | confirmed (code) |
| `0x002b4c00` | `OnFireGoal_Init` | init | OnFire (`0x19`), vtable `0x0053ff70` | confirmed (code) |
| `0x002b4c98` | `OnFire_PickRunPoint` | helper | a point ±4 m to the side ahead, checked by `0x002d9d40` (10 m) | confirmed (code) |
| `0x002b4f40` | `OnFireGoal_Process` | Process | OnFire: drops the target, runs about until the fire is out | inferred |
| `0x002b5110` | `WoundedGoal_Init` | init | Wounded (`0x1a`), vtable `0x0053ff10` | confirmed (code) |
| `0x002b5148` | `Wounded_SetLimp` | helper | dyn anim slot clip `0x29d` on or off | confirmed (code) |
| `0x002b5190` | `Wounded_PlayClutch` | helper | plays anim `0x29d` unless holding something | confirmed (code) |
| `0x002b5218` | `Wounded_CheckRecovered` | helper | above 20 % health: clears the actions | confirmed (code) |
| `0x002b5280` | `WoundedGoal_Start` | Start | limp on | confirmed (code) |
| `0x002b52a0` | `WoundedGoal_End` | End | limp off | confirmed (code) |
| `0x002b52c0` | `WoundedGoal_Process` | Process | no fight stance or target; done when no longer wounded; a clutch and line 8 | confirmed (code) |
| `0x002b5698` | `BlockGoal_WantsCounter` | helper | Blocking (`0x1b`): ducking with goal bytes `+0x14` and `+0x16` both 1 sets player command `0x10` (cross) and answers 1 ([combat.md](combat.md)) | confirmed (code) |
| `0x002b5a98` | `GrabbingGoal_Init` | init | Grabbing (`0x12`), vtable `0x00540150` | confirmed (code) |
| `0x002b5ad0` | `GrabbingGoal_Start` | Start | a let-go chance of the class value × 10 %, none with a FollowAndDefend (`0x35`) goal; goal `0x7d`'s flag | confirmed (code) |
| `0x002b5b58` | `GrabbingGoal_End` | End | when still grabbing, clears the hold timer (`+0x148` of the held record) | confirmed (code) |
| `0x002b5b98` | `Grabbing_PickMove` | helper | the direction of a throw or push from a grab (left, ahead, right, behind): away from a held flag, into a wall or an enemy, else random ([reactions](ai.md#fight-reactions)) | confirmed (code) |
| `0x002b60b0` | `Grabbing_NoDelay` | helper | −1.0 (no delay) | confirmed (code) |
| `0x002b60c0` | `GrabbingGoal_Process` | Process | Grabbing: near a train command 5; a rear grab holds the victim up for a friend; else kind 24 (twice at 40 %), the throws 25/29 by `Grabbing_PickMove`, or 26-28 ([reactions](ai.md#fight-reactions)) | confirmed (code) |
| `0x002b65f8` | `MountingGoal_Init` | init | Mounting (`0x13`, the tackler on top), vtable `0x005400f0` | confirmed (code) |
| `0x002b6638` | `MountingGoal_Process` | Process | Mounting: near a train command 5; else a picked kind (45 and 36 do nothing), kind 35 twice chained 40 % of the time ([reactions](ai.md#fight-reactions)) | confirmed (code) |
| `0x002b6818` | `GrabbedGoal_Init` | init | Grabbed (`0x14`), vtable `0x00540090` | confirmed (code) |
| `0x002b6848` | `GrabbedGoal_Process` | Process | Grabbed: a civilian calls for help every 30 updates (10 m); with a threat response the grabber becomes the target and he struggles ([reactions](ai.md#fight-reactions)) | confirmed (code) |
| `0x002b6b88` | `MountedGoal_Init` | init | Mounted (`0x15`, tackled), vtable `0x00540030` | confirmed (code) |
| `0x002b6bb8` | `MountedGoal_Process` | Process | Mounted: calls for help and struggles ([reactions](ai.md#fight-reactions)) | confirmed (code) |
| `0x002b6dc0` | `ArrestedGoal_Init` | init | Arrested (`0x16`), vtable `0x0053ffd0` | confirmed (code) |
| `0x002b6df8` | `ArrestedGoal_Start` | Start | releases the target of an AttackTarget goal (9) on the stack | confirmed (code) |
| `0x002b6e78` | `ArrestedGoal_End` | End | leaves the fight stance | confirmed (code) |
| `0x002b6eb0` | `Arrested_CallForRescue` | helper | every 30 updates a gang member within 20 m is asked to free him (`0x002b7d80`) | inferred |
| `0x002b6fa0` | `ArrestedGoal_Process` | Process | while cuffed: rescue calls and lines | inferred |
| `0x002b7140` | `FightBackOffGoal_Init` | init | FightBackOff (`0x1c`), vtable `0x0053fdf0` | confirmed (code) |
| `0x002b7178` | `FightBackOffGoal_Start` | Start | turn boost + 1 | confirmed (code) |
| `0x002b71b8` | `FightBackOffGoal_End` | End | restores the turn boost | confirmed (code) |
| `0x002b71d8` | `FightBackOff_PickPoint` | helper | the nearest flag to back off to (`0x004ede08`) | inferred |

### FightBackOff (type 0x1c): Process {#goal-fight-back-off}

The Process of the backing-off goal; its init, Start and End are with [the reaction goals](#goal-reactions).
It keeps a fighter at bay in the fight stance, facing his target, without attacking.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b7258` | `FightBackOffGoal_Process` | Process | fight stance on; every 45 updates (`0x002b71d8`) re-picks and glances at a random living gang mate; the target within 1.75 x far: looks at him and backs away (move-to-human 2000 ms to far x 1.75 + 1..2 m); facing away more than 60 deg: turns to him; otherwise after 4 s of standing, a taunt (kind 3) and the head look relaxes; never ends itself (returns 0) | confirmed (code) |

### HelpRespond (type 0x1d) {#goal-help-respond}

A gang member answering a help call: he turns to or walks to the caller's enemy and keeps the gang wanted.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b7670` | `HelpRespondGoal_Init` | init | vtable `0x0053fd90`; `+0x10` the target handle, `+0x14` a flag, `+0x08` the time limit | confirmed (code) |
| `0x002b76a0` | `HelpRespondGoal_Start` | Start | the caller's target: within 1.1 x far and walkable straight: turn to him and look 3 s (else 60 % a look-around); otherwise a move action to him (gait 2, 4 beyond 10 m; with an empty enemy list 4, or 5 on level `0x53`) | confirmed (code) |
| `0x002b78e0` | `HelpRespondGoal_End` | End | clears the actions | confirmed (code) |
| `0x002b7900` | `HelpRespondGoal_Process` | Process | refreshes the gang's wanted timer on the target (Brain_RefreshWanted); with `+0x14` clear, every 30 updates ends once the brain lists enemies (a fight takes over); ends when its actions are done | confirmed (code) |

### GuardFlag (type 0x2b) {#goal-guard-flag}

`GoalGuardFlag`: stay within a radius of a flag and face a set heading.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b79b8` | `Goal_GuardFlag` | pusher | GoalGuardFlag: the human and the flag from handles, pushes the goal | confirmed (code) |
| `0x002b7a90` | `GuardFlagGoal_Init` | init | vtable `0x0053fe50`; `+0x10` the flag, `+0x14` the radius and `+0x18` its square, `+0x1c` the heading to face (-1 none), `+0x20` a sound or zone id from the manager at `0x00512b04` (slot `0xcc`) | confirmed (code) |
| `0x002b7b08` | `GuardFlagGoal_End` | End | with `+0x20` set, tells the manager at `0x00512b04` (slots `0x4c`, `0x5c`, `0x8c`) the guard left | inferred |
| `0x002b7bc0` | `GuardFlagGoal_OnEvent` | event | pushes the goal built by `0x002f8c90` over this one and answers 1 | confirmed (code) |
| `0x002b7c18` | `GuardFlagGoal_Process` | Process | no flag: done; farther than the radius: a move action to the flag (gait 2); there, every 30 updates turns to the set heading | confirmed (code) |

### SaveHuman (type 0x5f) {#goal-save-human}

A gang mate runs to a cuffed or knocked-out Warrior and frees or revives him, fighting off whoever attacks
him on the way. Pushed by the arrested goal (`0x002b6eb0`) and by FollowAndDefend.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b7d80` | `Goal_SaveHuman` | pusher | resolves a human and pushes a SaveHuman goal on the human given (from the Arrested goal, `0x002b6eb0`) | confirmed (code) |
| `0x002b7e00` | `SaveHumanGoal_Init` | init | vtable `0x0053fd30`; `+0x20` the human to save, `+0x28` the attack kind (45 none), many state bytes | confirmed (code) |
| `0x002b7e80` | `SaveHumanGoal_Start` | Start | saves and overrides the brain's sight, the human's flag bits `0x40203c0` and pushability; drops a held kind-4/6 object; for a player-owned human queues a tutorial hint (2 cuffed, 3 knocked out) unless already seen; calls Resume | confirmed (code) |
| `0x002b8120` | `SaveHumanGoal_Resume` | Resume | re-reads the rescue point (`0x002b8a30`) | confirmed (code) |
| `0x002b8140` | `SaveHumanGoal_Suspend` | Suspend | when not yet started: clears the saved human's rescuer link (brain `+0x204`) and ends the follow camera's take-over | confirmed (code) |
| `0x002b81e0` | `SaveHumanGoal_End` | End | restores everything Start changed, clears the rescuer link, withdraws the tutorial hint and marks it seen, ends the camera take-over | confirmed (code) |
| `0x002b8518` | `SaveHumanGoal_AdjustEnemyScore` | score | an enemy targeting the rescuer, moving or already attacking, not busy, within 0.43 x near (x3 when he runs): its score + 3 x (sight range - distance); others -999 | confirmed (code) |
| `0x002b8688` | `SaveHumanGoal_FightOff` | helper | fights one enemy off: without an attack slot drops him; picks an attack kind, closes in to its reach (move-to-human 1000 ms) or queues it | confirmed (code) |
| `0x002b8868` | `SaveHumanGoal_FindOtherRescuer` | helper | the nearest gang mate (to the saved human) whose top goal is FollowAndDefend (`0x35`), free and idle; the squared distance out | confirmed (code) |
| `0x002b8a30` | `SaveHumanGoal_UpdatePoint` | helper | the rescue point: the saved human's position `+0x2b0` put on the nav mesh; none: flags `+0x3d` (give up) | confirmed (code) |
| `0x002b8ad8` | `SaveHumanGoal_Process` | Process | runs to the downed or cuffed human (gait 5), fighting off nearby attackers, pushing bystanders aside or breaking objects in the way; there, unlocks the cuffs or revives him (a car's `+0xf0` gets 10000); in one-player games hands the follow camera to the rescuer | confirmed (code) |

### Domination (type 0x7d) {#goal-hold-flag}

`GoalHoldFlag`: hold a flag's radius against every enemy inside it.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b9538` | `Goal_HoldFlag` | pusher | GoalHoldFlag: pushes a Domination goal (`0x7d`) holding a flag with a radius, no time limit | confirmed (code) |
| `0x002b95d0` | `HoldFlagGoal_Init` | init | vtable `0x0053fc10`; `+0x10` the flag, `+0x14` the radius squared; saves and clears brain `+0x21c` in `+0x18` | confirmed (code) |
| `0x002b9618` | `HoldFlagGoal_Start` | Start | steering off, sight angle 2 pi, attack-weight override `0x00511068` (brain `+0x208`), human flag 2, a turn boost of one more | confirmed (code) |
| `0x002b96a8` | `HoldFlagGoal_End` | End | undoes Start: steering on, sight angle back to 1.92 rad, no override, flag 2 cleared, the turn boost restored | confirmed (code) |
| `0x002b9730` | `HoldFlagGoal_FilterTarget` | score | an enemy not busy and inside the flag's radius keeps his score; others -999 | confirmed (code) |
| `0x002b97c0` | `HoldFlagGoal_MoveToFlag` | helper | farther than 1 m from the flag: a move action to it (gait 2 inside the radius in the fight stance, 5 outside), returns 1 | confirmed (code) |
| `0x002b98f0` | `HoldFlagGoal_Process` | Process | the best enemy in the radius (Brain_PickBestEnemy): walkable straight to him, push a fight goal (1000 ms), else back to the flag; with none, back to the flag, and taunts (anim `0x57`) every 3-6 s; never ends | confirmed (code) |

### Ring (type 0x7e) {#goal-ring}

Stand at a point facing another and fight whoever comes within 1.5 m, ignoring the humans of a type-8
tactic (inferred: a fighting ring's spectators).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002b9a90` | `Goal_Ring` | pusher | resolves the human and pushes a Ring goal with a radius, a point and a flag (no time limit) | confirmed (code) |
| `0x002b9b48` | `RingGoal_Init` | init | vtable `0x0053fbb0`; `+0x20` the stand point, `+0x30` the point to face (the flag's position when it resolves, else ahead), `+0x40` the radius, `+0x48` the attack kind (45), `+0x55` whether a flag was given; saves brain `+0x21c` | confirmed (code) |
| `0x002b9c68` | `RingGoal_Start` | Start | brain flag 2, sight angle 2 pi, not pushable; for a non-player brain, no chasing (`+0x265` = 0), a slower anim speed (vtable `0xe4`, slot 7) and human flags `0x20bd0`; with a flag, faces it | confirmed (code) |
| `0x002b9da0` | `RingGoal_End` | End | undoes Start | confirmed (code) |
| `0x002b9e90` | `RingGoal_AdjustEnemyScore` | score | -999 for an enemy whose gang tactic is type 8, beyond 1.5 m, or (with a flag) outside the radius from the face point and not targeting this human | confirmed (code) |
| `0x002b9fb0` | `RingGoal_PickLookTarget` | helper | `+0x10` = the nearest human in the brain's list `+0x164` not in a type-8 tactic | confirmed (code) |
| `0x002ba118` | `RingGoal_Process` | Process | walks back to the stand point (0.2 m), turns to the face point (15 deg), then marks brain `+0x21c` = 2; with a best enemy and the attack cooldown over, plays the attack clip `0x15` in reach; looks at the nearest human every 1.5 s, a fidget every 4-8 s while enemies are listed; never ends | confirmed (code) |

### StandGround (type 0x7f) {#goal-stand-ground}

`GoalStandGround`: hold a point and fight only enemies within 2 m.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ba580` | `Goal_StandGround` | pusher | GoalStandGround: pushes a StandGround goal at a point (no time limit) | confirmed (code) |
| `0x002ba608` | `StandGroundGoal_Init` | init | vtable `0x0053fb50`; `+0x20` the point to hold, `+0x30` the facing (ahead of the human), `+0x44` attack kind 45; saves brain `+0x21c` | confirmed (code) |
| `0x002ba6f0` | `StandGroundGoal_Start` | Start | brain flag 2, sight angle 2 pi, not pushable | confirmed (code) |
| `0x002ba740` | `StandGroundGoal_End` | End | undoes Start | confirmed (code) |
| `0x002ba7a0` | `StandGroundGoal_FilterTarget` | score | -999 for an enemy beyond 2 m | confirmed (code) |
| `0x002ba810` | `StandGroundGoal_PickNearest` | helper | sorts the brain's list `+0x164` by distance and keeps the first in `+0x10` | confirmed (code) |
| `0x002ba878` | `StandGroundGoal_Process` | Process | holds the point: drops the fight stance after 3 s in it; keeps the target or picks the best enemy within 2 m, losing it every 30 updates when not seen (9 m / 4 m); walks back beyond 0.25 m, turns to the enemy or the facing (60 deg); queues an attack in reach once the cooldown is over; re-picks the nearest every 2 s; every 10-20 s, out of the fight stance with enemies listed and the gang tactic of type `0x22`, plays the gang anim `0x253` (one of four in turn); never ends | confirmed (code) |

### ObjectPile (type 0x60) {#goal-object-pile}

Take objects from a pile and throw them at the nearest human until a time.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002bada8` | `Goal_ObjectPile` | pusher | resolves the human and pushes an ObjectPile goal on a world object with an end time | confirmed (code) |
| `0x002bae38` | `ObjectPileGoal_Init` | init | vtable `0x0053faf0`; `+0x10` the object pile's handle, `+0x14` the target (none), `+0x18` the end time | confirmed (code) |
| `0x002baec8` | `ObjectPileGoal_End` | End | clears the target and sets brain `+0x21c` = 2 | confirmed (code) |
| `0x002baf20` | `ObjectPileGoal_PickTarget` | helper | `+0x14` = the nearest human: from the brain's list `+0x164`, or when it is empty the visible humans in sight range (filter `0x0029c0d8`) | confirmed (code) |
| `0x002bb010` | `ObjectPileGoal_Process` | Process | done when the pile is gone or the end time passed; empty-handed: picks up from the pile within 1 m (PickUpItem) or walks to it with a GetItem-type goal (`0x002dc7a0`); holding a throwable: picks a target every 10 updates, closes to reach (2000 ms) or every 30 updates turns and throws (attack kind 0) | confirmed (code) |

### GrabTarget (type 0x1f) {#goal-grab-target}

`GoalGrabTarget`: run at a human, grab him and hold him, hurting him while he struggles.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002bb3d0` | `Goal_GrabTarget` | pusher | GoalGrabTarget: pushes a GrabTarget goal on a human | confirmed (code) |
| `0x002bb458` | `GrabTargetGoal_Init` | init | vtable `0x00540390`; `+0x10` the target; saves brain `+0x21c` and `+0x265` and clears them | confirmed (code) |
| `0x002bb4c0` | `GrabTargetGoal_Start` | Start | attack-weight override `0x00511098`, brain `+0x120` cleared, human flag `0x4000000`, a turn boost of one more; the target's `+0x11f` cleared | confirmed (code) |
| `0x002bb598` | `GrabTargetGoal_End` | End | undoes Start; the target's `+0x11f` set again | confirmed (code) |
| `0x002bb668` | `GrabTargetGoal_SetStruggleAnims` | helper | on the target: sets (1) or releases (0) the two struggle dyn-anim slots (`0x6c`, `0x6d`) from the table at `0x005110c8` | confirmed (code) |
| `0x002bb728` | `GrabTargetGoal_IsHoldingTarget` | helper | 1 when the target is not the one holding this human and this human's target is him without held flag `0x200` | inferred |
| `0x002bb7b8` | `GrabTargetGoal_OnBroken` | helper | when the target is held by this human: clears flag `0x4000000` and the power, marks `+0x16` (the hold broke) | inferred |
| `0x002bb858` | `GrabTargetGoal_Process` | Process | done when the target is invalid; not yet holding: within 1.1 x far and in line of sight push a fight goal (1000 ms), else an EngageEnemy run-in or, past an object in the way, a move (gait 4); holding: fills power, sets the struggle anims, from a tackle issues the attack kind `0x28` command and from a front grab command `0x19`; every 30 updates adds pending damage; a line every fourth standing reaction; turns to face the nearest player beyond 45 deg | confirmed (code) |

### FollowAndAttack (type 0x34) {#goal-follow-and-attack}

What each Warrior runs under the attack command ([Warrior commands](ai.md#warrior-commands)): fight the
best enemy near the chief, or stay with him.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002bbdc0` | `FollowAndAttackGoal_Init` | init | vtable `0x005405d0`; `+0x10` the leader (the chief), `+0x14` the last target (-1), `+0x18` / `+0x1c` timers, `+0x20` a counter | confirmed (code) |
| `0x002bbe00` | `FollowAndAttackGoal_Start` | Start | joins the leader's formation, fight stance off, then Resume | confirmed (code) |
| `0x002bbe98` | `FollowAndAttackGoal_End` | End | leaves the leader's formation, then Suspend | confirmed (code) |
| `0x002bbf30` | `FollowAndAttackGoal_Suspend` | Suspend | unless the leader is standing idle, `0x00231b60` on the human (inferred: stops his move) | inferred |
| `0x002bbfa0` | `FollowAndAttackGoal_TryPickUp` | helper | for a brain allowed to chase (`+0x265`), not blocked and (when asked) empty-handed: every 20 updates, or within 15 m of the enemy, a smash or throw object (Ai_FindObject, 20 m or 3 m) it may pick up gets a GetItem goal (kind 4); returns 1 then | confirmed (code) |
| `0x002bc198` | `FollowAndAttackGoal_Process` | Process | the best enemy within 60 m of the leader: changing target resets `+0x2d3`; unarmed and chasable within 1.1 x far, or in sight, push a fight goal (4000 ms), beyond it an EngageEnemy run-in, unless a pick-up comes first; not chasable: move-to-human or shuffle in the stance; no enemy: within 8 m of the leader, in the stance (his or the leader's) turns to the leader once the enemy list is empty, else every 4 s to the leader's heading, a 30 % fidget every 3 s; farther, push FollowFormation (0.75 m, 2000 ms) | confirmed (code) |

### FollowAndDefend (type 0x35) {#goal-follow-and-defend}

What each Warrior runs under the defend command ([The default command](ai.md#warrior-follow)): stay in
the chief's formation, guard him while he commits a crime, rescue him when he is down, fight enemies near
him.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002bca20` | `FollowAndDefendGoal_Init` | init | vtable `0x00540570`; `+0x10` the leader, `+0x18` the radius squared, `+0x1c` the saved sight angle (pi), `+0x32` a random 45-75 | confirmed (code) |
| `0x002bcab8` | `FollowAndDefendGoal_Start` | Start | joins the leader's formation, fight stance off, Resume; saves the sight angle and turn boost, sets 2 pi and one more boost | confirmed (code) |
| `0x002bcb88` | `FollowAndDefendGoal_End` | End | leaves the formation, Suspend, restores sight angle and turn boost | confirmed (code) |
| `0x002bcc28` | `FollowAndDefendGoal_Resume` | Resume | clears the actions | confirmed (code) |
| `0x002bcc48` | `FollowAndDefendGoal_Suspend` | Suspend | as FollowAndAttack's Suspend | inferred |
| `0x002bccb8` | `FollowAndDefendGoal_AdjustEnemyScore` | score | adds `0x00510b84` for the current target, `0x00510b88` when he holds an attack slot on the leader, 100 when he is grabbing the leader, -20 when down, stunned or wounded, +5 running; within sight range adds sight - distance, and (beyond 1.1 x far from the leader unless running in at him) the leader closeness x `0x00510b50` and 4 per free attack slot; else -999 | confirmed (code) |
| `0x002bcf88` | `FollowAndDefendGoal_TryPickUp` | helper | as FollowAndAttack's pick-up with 12 m / 6 m | confirmed (code) |
| `0x002bd180` | `FollowAndDefendGoal_Watch` | helper | at a random interval: with an empty enemy list turns to where the leader looks (his clear ray or his grabber), else turns in the stance to the nearest listed human (15 deg) | confirmed (code) |
| `0x002bd4f0` | `FollowAndDefendGoal_Fidget` | helper | every 3 s, at 30 % and with an empty enemy list, a fidget | confirmed (code) |
| `0x002bd590` | `FollowAndDefendGoal_DefendCrime` | helper | for a type-3 brain (a Warrior) every 3 updates while the leader is busy with a crime: the nearest visible human within far (filter `0x0029c1f0`) not attacked by anyone becomes the target and gets attack kind `0x15` (`0x16` or `0x11` when he walks), closing in to reach first (2000 ms); returns 1 then | confirmed (code) |
| `0x002bd808` | `FollowAndDefendGoal_Process` | Process | every 200-400 ms, when the leader is cuffed or knocked out (a player one only when he holds the revive item), claims him and pushes SaveHuman; no enemy: back to the formation slot (FollowFormation 0.25 m, 2000 ms) or a move to it beyond the radius (a quarter of it while the leader mugs, tags, steals a stereo or picks a lock), then guards the crime (`0x002bd590`), watches, a 'keep watch' line every 3-3.5 s while he mugs, look-arounds; with an enemy, as FollowAndAttack: fight goal (1000 ms), EngageEnemy (taunt `0x8f` when the enemy runs) or move-to-human | confirmed (code) |

### HoldPosition (type 0x36) {#goal-hold-position}

`GoalHoldPosition` ([goal types](ai.md#goals)): keep to a radius round a point and fight from there.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002be590` | `Goal_HoldPosition` | pusher | GoalHoldPosition: pushes a HoldPosition goal at a point with a radius | confirmed (code) |
| `0x002be640` | `HoldPositionGoal_Init` | init | vtable `0x00540510`; `+0x10` the point, `+0x20` the radius, `+0x24` the next fidget time | confirmed (code) |
| `0x002be688` | `HoldPositionGoal_End` | End | calls Suspend | confirmed (code) |
| `0x002be6b0` | `HoldPositionGoal_Suspend` | Suspend | unless the gang leader stands idle, `0x00231b60` on the human (inferred: stops his move) | inferred |
| `0x002be700` | `HoldPositionGoal_TurnTo` | helper | turns to a human more than 60 deg off | confirmed (code) |
| `0x002be818` | `HoldPositionGoal_Process` | Process | no enemy: inside the radius, out of the fight stance a 30 % fidget every 3 s; outside, a move back (gait 2); an enemy not targeting him: turn to him; targeting him and inside the radius (or blocked): fight goal (4000 ms) in line of sight and reach, else turn and shuffle, or a move past an object in the way; outside the radius: walk back (0.5 m) | confirmed (code) |

### WarriorVandalSteal (type 0x83) {#goal-warrior-vandal-steal}

What each Warrior runs under the steal or wreck command: fetch things to break and break glass, cars
and objects near the chief, or stay with him.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002bec58` | `WarriorVandalStealGoal_Init` | init | vtable `0x005404b0`; `+0x10` the object handle (none), `+0x29` = `0xff`, timers cleared | confirmed (code) |
| `0x002becb8` | `WarriorVandalStealGoal_Start` | Start | joins the gang leader's formation, fight stance off, then Resume | confirmed (code) |
| `0x002bed30` | `WarriorVandalStealGoal_Resume` | Resume | releases the claimed object (its `+0xec` cleared for an object or glass) and forgets it | confirmed (code) |
| `0x002bedb0` | `WarriorVandalStealGoal_Suspend` | Suspend | unless the gang leader stands idle, `0x00231b60` on the human; clears the human's `+0x284` and the goal's counter | confirmed (code) |
| `0x002bee08` | `WarriorVandalStealGoal_End` | End | leaves the leader's formation, then Suspend | confirmed (code) |
| `0x002bee78` | `WarriorVandalStealGoal_TryFetch` | helper | empty-handed under the gang's steal tactic (type `0x26`): unless `0x00321540` already gives him something, waits 500 ms x his place in the gang, then finds a smash target within 15 m of the leader (filter `0x0053f2b0`), skips one a gang mate stands on, and pushes GetItem (kind 4); returns 1 then | confirmed (code) |
| `0x002bf118` | `WarriorVandalStealGoal_TryVandalise` | helper | under the steal tactic: holding a breaking object (object flag `0x10000`) or a throwable: turns to the nearest glass within 15 m once, else to the crowd ahead and queues the swing (attack kind 0, delay `0x7d`) with a 30/70 % line `0x8e`/`0x8f` every 8 s; every other update: a car within 15 m of the leader (and him within 20 m) with a free spot gets the car goal `0x002ddb40` (20 m); else a smash target (filter `0x0053f230`) gets GetItem (kind 4/5 objects) or the smash goal `0x002dd0c8`; every 1.5-3 s a glass pane (Glass_ClaimPane) gets the smash goal | confirmed (code) |
| `0x002bf748` | `WarriorVandalStealGoal_Process` | Process | fetches and vandalises (above); otherwise, beyond 3 m of the leader (15 m while his gang is attacked) or when the leader runs, after 2 s pushes FollowFormation (0.75 m, mode 1); while the gang is attacked: sight 2 pi, the fight stance, turns to the nearest enemy or closes on him (move-to-human 3000 ms); otherwise turns to the leader's heading every 4 s and a 30 % fidget every 3 s | confirmed (code) |

### CopperGuardArrested (type 0x56) {#goal-copper-guard-arrested}

A cop guards a cuffed Warrior and walks him off when he is free to be taken.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002bfc48` | `Gang0_FindUnguardedCuffed` | helper | the first cuffed member of gang 0 (the Warriors) with no guard (brain `+0x288` clear) | confirmed (code) |
| `0x002bfce0` | `Gang0_FindUnguardedCuffedNear` | helper | another unguarded cuffed gang-0 member within 5 m of the given human | confirmed (code) |
| `0x002bfe00` | `Goal_CopperGuardArrested` | pusher | pushes a CopperGuardArrested goal on a cop for a prisoner, a radius and a time | confirmed (code) |
| `0x002bfe98` | `CopperGuardArrestedGoal_Init` | init | vtable `0x00540ab0`; `+0x20` the prisoner, `+0x1c` the wander radius, `+0x14` the end time (seconds; 0 none), `+0x10` the state 0 | confirmed (code) |
| `0x002bff08` | `CopperGuardArrestedGoal_Process` | Process | a state machine: 0 walk to a random point within the radius of the prisoner (`0x002a4ae8`), done at the end time; 1 claim the prisoner (brain `+0x288`) and a second one within 5 m, and once the prisoner is freed or gone lead him out (Goal_MoveToExitFlag to the gang's exit or the nearest flag of activity 8; state 3); else wander or play the anim `0x29c` (state 4); 2 wait 10-19 s; 3 done; 4 wait for the anim | confirmed (code) |

### FindEnemy (type 0x41) {#goal-find-enemy}

The goal under Melee that restarts a fight ([The FindEnemy goal](ai.md#find-enemy)); its Process is there.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c0430` | `FindEnemyGoal_Init` | init | vtable `0x00540a50`; `+0x10` / `+0x14` two ranges (90 and 30 from Brain_Fight), `+0x18` a byte, `+0x1c` the duration, `+0x20` its end time; a police brain (type 1) is added to the game's guarding cops; clears the spotter note | confirmed (code) |
| `0x002c04e8` | `FindEnemyGoal_End` | End | when the brain holds a goal `0x54`, calls `0x002d11f8` on it | confirmed (code) |
| `0x002c0548` | `FindEnemyGoal_Destroy` | Destroy | a police brain leaves the game's guarding cops | confirmed (code) |
| `0x002c0580` | `FindEnemyGoal_Suspend` | Suspend | clears the spotter note (Brain_NoteSpotter 0) | confirmed (code) |
| `0x002c05a0` | `FindEnemyGoal_PickPlayer` | helper | the best player to look for: score sight range - distance, -100 when never seen, -50 cuffed or knocked out, +50 when he may not be spectated, +15 in line of sight | confirmed (code) |

### CopperGuard (type 0x40) {#goal-copper-guard}

`GoalCopGuard`: a cop stands at a flag.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c0910` | `Goal_CopGuard` | pusher | GoalCopGuard: pushes a CopperGuard goal at a flag | confirmed (code) |
| `0x002c09b0` | `CopperGuardGoal_Init` | init | vtable `0x00540bd0`; `+0x10` the flag, `+0x34` the state 0, two shorts; brain `+0x21c` = 4, `+0x220` = 5, may approach (`+0x28d`) | confirmed (code) |
| `0x002c0a10` | `CopperGuardGoal_Process` | Process | while the first player exists: farther than 2 m from the flag, a MoveToFlag goal (gait 3, 4 beyond 22 m); there, idles with the overlay clip `0x29e` or a fidget (`0x00231ac8`) | confirmed (code) |
| `0x002c0bc8` | `CopperGuardGoal_OnEvent` | event | pushes the goal built by `0x002f8c90` and answers 1 (as GuardFlag) | confirmed (code) |

### CopperRespond (type 0x55) {#goal-copper-respond}

`GoalCopRespondSimple`: fight, then take a cuffed Warrior away or leave.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c0c20` | `Goal_CopRespondSimple` | pusher | GoalCopRespondSimple: pushes a CopperRespond goal (its init is the function named CopStation_GetSettings) | confirmed (code) |
| `0x002c0dd8` | `CopperRespondGoal_Process` | Process | state 0: Goal_Melee (no limit), state 2; state 2: when the fight ends, an unguarded cuffed Warrior gets a CopperGuardArrested goal (for the game's guard time at `+0x320`), else leave by the gang's exit flag; state 4; state 4: back to 2 and done | confirmed (code) |

### ArrestHuman (type 0x59) {#goal-arrest-human}

`GoalArrestHuman`: a cop fights one human with no time limit.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c0f58` | `Goal_ArrestHuman` | pusher | GoalArrestHuman: resolves the cop and the human and pushes an ArrestHuman goal | confirmed (code) |
| `0x002c0fd8` | `ArrestHumanGoal_Init` | init | vtable `0x005409f0`; `+0x14` the human to arrest; brain `+0x21c` / `+0x220` / `+0x28d` cleared, then Brain_Fight on him with no limit | confirmed (code) |

### CopperPatrol (type 0x42) {#goal-copper-patrol}

`GoalCopPatrol`: a cop walks a path of flags.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c1060` | `Goal_CopPatrol` | pusher | GoalCopPatrol: pushes a CopperPatrol goal on a path | confirmed (code) |
| `0x002c1110` | `GoalCopPatrol_Init` | init | a TravelPath goal (GoalTravelPath_Init, 1.0) given vtable `0x00540b70`; `+0x34` the path, `+0x3c` a second value, `+0x40` gait 2, two shorts; brain `+0x21c` = 4, `+0x220` = 5, `+0x28d` set | confirmed (code) |
| `0x002c11d8` | `GoalCopPatrol_Process` | Process | first a move to the path's first flag (gait 2), then each time GoalTravelPath_NextPoint advances, a move to the next flag; never ends | confirmed (code) |

### Patrol (type 0x73) {#goal-patrol}

A cop on the flag net, walking alone or in a pair ([Crimes and the police](ai.md#crimes)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c1338` | `PatrolGoal_Init` | init | vtable `0x00540990`; `+0x10` a pair link (PairLink_Init), `+0x44` the gait, points cleared | confirmed (code) |
| `0x002c13d8` | `PatrolGoal_Start` | Start | the nearest flag-net flag (`0x002adac0`) as the next point; patrol and line timers 25 s and 8 s | confirmed (code) |
| `0x002c14f8` | `PatrolGoal_Suspend` | Suspend | breaks the pair (unless following), human `+0xf4` = `0xff` | confirmed (code) |
| `0x002c1538` | `PatrolGoal_End` | End | breaks the pair | confirmed (code) |
| `0x002c1558` | `PatrolGoal_NextFlag` | helper | the next flag of the net (`0x002a75b8`) and a random point within 1.75 m of it, human `+0xf4` = `0x13` | confirmed (code) |
| `0x002c1638` | `PatrolGoal_TryPair` | helper | every 40 updates, a gang mate within 5 m who is patrolling and not chatting or fighting: joins him (PairLink_TryJoin) | confirmed (code) |
| `0x002c17c0` | `PatrolGoal_BreakPair` | helper | breaks the pair link with the partner's Patrol goal | confirmed (code) |
| `0x002c1858` | `PatrolGoal_Process` | Process | deletes the cop when far from every camera (every 90 updates); greets a player within 2 m in view once (line `0x20`), a line `0x7d` every 25 s; every 192 updates beyond 9.1 m of his point re-plans (`0x002c5af8`); every 8 s a usable flag within 10 m and 15 m on the path gets the flag goal `0x002db768`; a follower keeps 1 m behind his partner (FollowFormation 0.25 m), a leader walks from flag to flag | confirmed (code) |

### ProcessBreakAndEnter (type 0x75) {#goal-break-and-enter}

A cop answering a break-in at a store: he looks for the intruder, checks the door and resets the
store. The store flag's word `+0xd8` holds the claim byte and the broken bit `0x20000`; the reading
of the states is inferred.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c1e10` | `BreakAndEnterGoal_Init` | init | vtable `0x00540930`; `+0x10` the crime point, `+0x30` the store flag, `+0x34` the store object (later), `+0x38` the state 0, `+0x40` a wait of 10000 ms | confirmed (code) |
| `0x002c1e80` | `BreakAndEnterGoal_Start` | Start | finds the named object nearest the store flag (the shop's door or window) and a point 1.75 m out from it along the flag's line, then Resume | confirmed (code) |
| `0x002c2068` | `BreakAndEnterGoal_Resume` | Resume | clears `+0x46` and gives the human the anim set `0x005650d8` | confirmed (code) |
| `0x002c20b0` | `BreakAndEnterGoal_Suspend` | Suspend | drops the anim sets; in states 0-2 and not mid-check, releases the store flag's owner byte (`+0xd8` byte 1 = `0xff`); back to state 0 with a 2000 ms wait | confirmed (code) |
| `0x002c21d0` | `BreakAndEnterGoal_End` | End | calls Suspend | confirmed (code) |
| `0x002c21f8` | `BreakAndEnterGoal_SpotIntruder` | helper | the visible human within 10 m of the store flag (filter `0x0029c1f0`) nearest the flag and inside sight range: push a fight goal and the arrest goal (`0x002c4060`, kind 1); returns 1 then | confirmed (code) |
| `0x002c2430` | `BreakAndEnterGoal_Process` | Process | done when the flag is gone; every 4 updates, while the store is not yet broken (flag bit `0x20000`), looks for an intruder; state 0 claims the store flag (`+0xd8` byte 1) and walks to it (gait 2 or 4 beyond 8 m), then 3 s with the anim set `0x005650f8` (state 2); state 1 wanders 5-7.5 m away (not near the player's gang) with the anim `0x29c`; state 2 resets the store's objects and zone (ObjZone_Enable off), tells a particle effect within 6 m (message `0x13`) and plays the anim (state 3); state 3 walks to the door point and turns to the flag (state 4); state 4, once the store was broken: asks the store object (message `0x22` with 7) when nobody is within 12 m, releases the flag and ends; once a second its first visit sends `0x22` (11) and `0xb` (the cop) to the store object | inferred |

### Respond (type 0x74) {#goal-respond}

A cop answering a crime report: he goes to the scene and hands over to the goal for the crime kind
([Crimes and the police](ai.md#crimes)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c2d38` | `Goal_CopRespond` | pusher | GoalCopRespond: forwards to Goal_Respond with the point | confirmed (code) |
| `0x002c2d80` | `Goal_Respond` | pusher | resolves the cop and pushes a Respond goal | confirmed (code) |
| `0x002c2e30` | `RespondGoal_Init` | init | vtable `0x005408d0`; `+0x10` the crime kind, `+0x20` the point, `+0x30` the victim, `+0x34` the offender | confirmed (code) |
| `0x002c2e80` | `RespondGoal_Start` | Start | a police brain adds one to the game's responding cops (`+0x322`), a type-2 brain gets a radar blip; the point becomes the victim's position; then Resume | confirmed (code) |
| `0x002c3008` | `RespondGoal_PushHandler` | helper | by the crime kind: 1 near a store flag (activity `0xe`, within 10 m, not yet broken) a ProcessBreakAndEnter goal; 9 a ProcessDisturbance (15 m, gait 4); 4 for police a ProcessDisturbance (6 m); otherwise a valid offender: a FindEnemy goal (90 / 30 m) and the spotter and last-seen point noted; else a ProcessDisturbance (6 m, gait 2); returns 1 | confirmed (code) |
| `0x002c32c0` | `RespondGoal_SpotOffender` | helper | the visible human within the given range of the crime point nearest it and in sight: pushes the handler, clears a ProcessDisturbance's `+0x38`, a fight goal and the arrest goal (`0x002c4060`) with the crime kind; returns 1 | confirmed (code) |
| `0x002c3548` | `RespondGoal_Process` | Process | while not handled: refreshes the wanted timer on the offender, follows the victim's position (every 8 updates); a police brain at a kind-1 or kind-4 crime looks for the offender within 10 m; within 6 m of the point pushes the handler (`0x002c3008`); else walks (gait 4) to a random point 1-6 m round it inside the turf, or straight to it after six tries; after five failed moves, or once handled, leaves by the nearest flag of activity 8 (MoveToExitFlag) | confirmed (code) |

### RiotCop (type 0x77) {#goal-riot-cop}

Riot police under the gang's riot tactic ([GoalRiot](ai.md#riot)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c3958` | `RiotCopGoal_Init` | init | vtable `0x00540870`; `+0x10` the point to hold, `+0x30` the target (none), `+0x34` the gait 4 | confirmed (code) |
| `0x002c39d0` | `RiotCopGoal_End` | End | one fewer responding cop (`+0x322`), drops the target | confirmed (code) |
| `0x002c3a08` | `RiotCopGoal_Process` | Process | only under the gang's riot tactic (type 5), else done; the leader walks to the point (or the nearest player in sight), the tactic's `+0x3b` ('engaged') set within 5 m, when a member is attacked, or when the player is within 10 m and runs; engaged: the best enemy gets a fight goal (4000 ms) in reach and sight, else a Chase (90 / 45 m) by the leader or a ChaseSupport (90 m) by the others; no enemy: Spectate (3-6 s); a member follows the leader's formation (0.25 m, 20 m once engaged) | confirmed (code) |

### SpotCriminal (type 0x78) {#goal-spot-criminal}

The arrest goal pushed with a fight goal when a cop spots an offender; its Process is outside this range.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c4060` | `Goal_SpotCriminal` | pusher | pushes a SpotCriminal goal (the arrest goal: kind, point, offender, times) on a cop | confirmed (code) |
| `0x002c4120` | `SpotCriminalGoal_Init` | init | vtable `0x00540810`; `+0x10` the point, `+0x20` the offender, `+0x24` / `+0x28` two times, `+0x2c` the crime kind | confirmed (code) |
| `0x002c4170` | `SpotCriminalGoal_Start` | Start | an offender out of sight and not yet held: a move to a point within 4 m of him (gait 2, 4 beyond 6 m); next check in 125-250 ms; brain `+0x21c` = 0, brain flag 2 | confirmed (code) |

### CallForBackup (type 0x79) {#goal-call-for-backup}

A cop gets clear and radios for more cops (pushed by the police brain).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c4710` | `Goal_CallForBackup` | pusher | from the police brain: for a non-class-10 cop: drops his anim set, marks the offender's gang (`+0x5f4` = now) and pushes a CallForBackup goal | confirmed (code) |
| `0x002c4850` | `CallForBackupGoal_Init` | init | vtable `0x005407b0`; `+0x10` the offender, `+0x14` the anim variant, `+0x18` the crime kind, `+0x24` a flag, state 0 | confirmed (code) |
| `0x002c4890` | `CallForBackupGoal_Start` | Start | a spinning icon over him (`0x00565120`), turns to the offender, sight 2 pi, brain flag 2; queues the tutorial hint 9 once outside the levels `0x50` and 99 | confirmed (code) |
| `0x002c4a58` | `CallForBackupGoal_End` | End | gait 0, stops (`0x00231c58`), fight stance off, removes the icon, sight 1.92 rad, brain `+0x21c` = 2 | confirmed (code) |
| `0x002c4af8` | `CallForBackupGoal_FleePoint` | helper | a point 15 m away from danger (`0x002d9d40`) | inferred |
| `0x002c4b20` | `CallForBackupGoal_Process` | Process | state 0 (or within 1.5 m of the offender): runs clear of the offender (gait 5, 4 under half power); state 1, once still: turns to the offender and plays the phone anim (`0x29c` variant 3); state 2: queues responders (Responders_Queue kind 6, 3 cops with the flag, else 1) at his position, marks the offender's gang and ends | confirmed (code) |

### LeaveArea (type 0x7a) {#goal-leave-area}

Warn a threat once, then walk away to a flag at least 20 m from him.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c4ec8` | `Goal_LeaveArea` | pusher | marks the offender's gang (`+0x5f4` = now) and pushes a LeaveArea goal on the human | confirmed (code) |
| `0x002c4fb0` | `LeaveAreaGoal_Init` | init | vtable `0x00540750`; `+0x10` the danger point, `+0x20` the destination, `+0x30` the gait, `+0x34` the threat, `+0x38` / `+0x3c` timers | confirmed (code) |
| `0x002c5008` | `LeaveAreaGoal_PickExit` | helper | the destination: the nearest flag at least 20 m from the threat that is reachable, else the nearest flag of activity 0 | confirmed (code) |
| `0x002c5120` | `LeaveAreaGoal_Start` | Start | picks the exit and waits 1.5 s | confirmed (code) |
| `0x002c5160` | `LeaveAreaGoal_Process` | Process | done when blocked or a move failed; after the wait turns to the threat, says the line `0x2e` to him once and waits 3 s; then walks to the destination at its gait and ends within 1 m | confirmed (code) |

### ProcessDisturbance (type 0x76) {#goal-disturbance}

A cop at a disturbance: radio it in, search round the point, arrest the offender if seen.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c53f0` | `DisturbanceGoal_Init` | init | vtable `0x005406f0`; `+0x10` the point, `+0x20` the offender, `+0x24` the crime kind, `+0x28` the gait, `+0x30` the search radius, `+0x34` a wait, `+0x38` / `+0x39` flags (look for the offender, call it in) | confirmed (code) |
| `0x002c5450` | `DisturbanceGoal_Start` | Start | 30 % (always with `+0x39`) plays the anim `0x29c` for 1 s, then Resume | confirmed (code) |
| `0x002c54d8` | `DisturbanceGoal_End` | End | calls Suspend | confirmed (code) |
| `0x002c5500` | `DisturbanceGoal_Resume` | Resume | gives the human the anim set `0x005650d8` | confirmed (code) |
| `0x002c5548` | `DisturbanceGoal_Suspend` | Suspend | marks it interrupted, state 3, drops the anim set | confirmed (code) |
| `0x002c5598` | `DisturbanceGoal_SpotOffender` | helper | as the Respond goal's: the visible human nearest the point within the search radius gets a fight goal and the arrest goal with the crime kind; returns 1 | confirmed (code) |
| `0x002c57b8` | `DisturbanceGoal_Process` | Process | every 4 updates (with `+0x38`) looks for the offender; refreshes the wanted timer; state 0: a police radio call (anim `0x29c` variant 3, `0x00233890` with the kind) when `+0x39`, wait; state 1: after the wait, wanders to points 0.5-1 x the radius round the point (not near the player's gang) with the anim; state 2: a second radio call once; state 3: done | confirmed (code) |

### CopInteract (type 0x7b) {#goal-cop-interact}

A cop and a hooker or a bum argue: the two walk together and play a pair of argue clips with lines.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c5ab0` | `Filter_IsArguableClass` | helper | a filter: 1 for a class whose `+0x11b` is 2, 6 or 7 | confirmed (code) |
| `0x002c5af8` | `Goal_CopInteract` | pusher | pushes a CopInteract goal with a partner and a mode on a brain | confirmed (code) |
| `0x002c5b68` | `CopInteractGoal_Init` | init | vtable `0x00540690`; `+0x10` the partner (none: look for one), `+0x14` the mode (2 when a partner is given), `+0x1c` 'initiator' (1 without a partner), `+0x1f` role `0xff` | confirmed (code) |
| `0x002c5be0` | `CopInteractGoal_Start` | Start | clears `+0x1e`, then Resume | confirmed (code) |
| `0x002c5c10` | `CopInteractGoal_Resume` | Resume | with a partner also in CopInteract: picks a pair of argue clips (a hooker pair when the partner's `+0x3b8` is 1, else a bum pair) as anim overrides `0x14e` and sets the roles (initiator 0, partner 1) | confirmed (code) |
| `0x002c5e10` | `CopInteractGoal_Suspend` | Suspend | clears the anim override, role `0xff` | confirmed (code) |
| `0x002c5e50` | `CopInteractGoal_End` | End | calls Suspend | confirmed (code) |
| `0x002c5e78` | `CopInteractGoal_FindPartner` | helper | the nearest visible human within 9 m of an arguable class not bumming, interacting or already chatting (Pedestrian goal busy), with a clear ray: becomes the partner (mode 1); a pedestrian partner is given his own CopInteract goal (mode 2); returns 1 | confirmed (code) |
| `0x002c61f8` | `CopInteractGoal_Process` | Process | done after its time; without a partner looks for one (else done); mode 2 (the partner): walks to 1.25 m in front of the initiator (6 s); mode 1: the same and gives the partner his goal (mode 3, 10 s); mode 3: turns to him (mode 4); mode 4: within 1.25 m and both clips loaded plays the argue clip (anim `0x14e`, mode 5), else after 2.5 s or three tries gives up; mode 6: the pair's lines (`0xc0` / `0xc1`, or `0xbe` / `0xbf` for the bum pair), mode 7; ends when both are idle | confirmed (code) |

### IssueWarning (type 0x7c) {#goal-issue-warning}

A warning to an offender: two lines, then his gang is wanted for 10 s.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c68b8` | `Goal_IssueWarning` | pusher | pushes an IssueWarning goal (a point to face and the offender) on a human | confirmed (code) |
| `0x002c6948` | `IssueWarningGoal_Init` | init | vtable `0x00540630`; `+0x10` the point, `+0x20` the offender, `+0x24` the state 0 | confirmed (code) |
| `0x002c69b0` | `IssueWarningGoal_Process` | Process | done when blocked or the offender is a nearer threat; state 0: the line `0x24` and a turn to the point (250 ms); state 1, after the line: the warning `0x20` to the offender and a turn to him; state 2, after it: unless his gang is already wanted (`+0x5ec`), makes it wanted for 10 s (`0x001698a0`); done | confirmed (code) |

### Dealer (type 0x80) and DealerReaction (type 0x81) {#goal-dealer}

[GoalDealer](ai.md#dealer) and the flee-or-fight goal it pushes when attacked ([Buying](ai.md#dealer-buy)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c6c88` | `Goal_Dealer` | pusher | GoalDealer: the dealer type from his class (426-430 flash, 431-435 spray paint, 436-440 weapons), pushes the goal (ai.md#dealer) | confirmed (code) |
| `0x002c6d90` | `DealerGoal_Init` | init | ai.md#dealer has the fields | confirmed (code) |
| `0x002c6e78` | `DealerGoal_Start` | Start | threat response (brain `+0x21c`) 0, brain `+0xcc` ORed with 2; the spinning icon by dealer type (`+0x28`: 0 dyn_flashdeal, 1 dyn_weapdeal, 2 the icon at `0x00565668`); puts the type's item (table `0x005110fc`, 8-byte rows) in his pocket; home = his current position (`+0x10`..`+0x1c`); then Resume (vtable `+0x34`) ([Dealer](ai.md#dealer)) | confirmed (code) |
| `0x002c6f98` | `DealerGoal_Resume` | Resume | leaves fight stance, not pushable (Human_SetPushable 0), registers the kind-4 buy prompt with the type's global string (table `0x005110f8`) ([Dealer](ai.md#dealer)) | confirmed (code) |
| `0x002c7020` | `DealerGoal_Suspend` | Suspend | withdraws the prompt (`+0x1b2` = 0, vtable 300), pushable, and resets the visit (DealerGoal_Reset) | confirmed (code) |
| `0x002c70a0` | `DealerGoal_End` | End | removes the spinning icon; if he is the game state's dealer customer (`+0x284`) that is reset to the default (`0x006ebd30`); threat response 2, clears brain `+0xcc` bit 2; then the base End (vtable `+0x3c`) ([Dealer](ai.md#dealer)) | confirmed (code) |
| `0x002c7158` | `DealerGoal_QueueGesture` | helper | queues a dealer gesture (ai.md#dealer-gestures) | confirmed (code) |
| `0x002c7248` | `DealerGoal_Say` | helper | says the dealer's line for a kind (ai.md#dealer-gestures) | confirmed (code) |
| `0x002c73b8` | `DealerGoal_CanOtherPlayerBuy` | helper | From DealerGoal_OnBuy with two players: 1 when the other player is within the goal's range (`+0x2c`, squared distance) and can buy: money (item 2) at least the price byte `0x005110fd` and, except for type 1, a count of the item below the cap byte `0x005110fe` ([Dealer](ai.md#dealer-buy)) | confirmed (code) |
| `0x002c7c20` | `DealerGoal_FinishPair` | helper | when the money pair was started by this deal (`+0x45`): the 'cash' line at most every 5 s, then the deal's completion (the weapons dealer's blade to the chosen member, else the item to the buyer's inventory), the price taken and added to the dealer's money (at most 999), `+0x3f` set and a deal counted | confirmed (code) |
| `0x002c7de8` | `DealerGoal_Reset` | helper | resets the visit when the player leaves (ai.md#dealer-process) | confirmed (code) |
| `0x002c88a8` | `DealerFlee_Push` | pusher | pushes the flee-or-fight goal when the dealer is attacked (ai.md#dealer-buy) | confirmed (code) |
| `0x002c8970` | `DealerFleeGoal_Init` | init | vtable `0x00540c30`: the attacker and the 'fight' byte | confirmed (code) |
| `0x002c89a8` | `DealerFleeGoal_Start` | Start | line 9 or a turn to the attacker, then a 25-50 ms wait | confirmed (code) |
| `0x002c8af0` | `DealerFleeGoal_Process` | Process | steps back 2-4 m, then the blade in hand (fight) or a run at gait 5 to an activity-8 flag | confirmed (code) |

### HTLDefense (type 0x64) {#goal-htl-defense}

The HTL goals (types `0x64`-`0x68`) serve a gang tactic that holds a post on one side of a line (tactic
flags `+0x20` and `+0x28`, line `+0x30`); "hold the line" is inferred from that. Defense: keep the post.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c8e18` | `HTLDefenseGoal_Init` | init | vtable `0x00540e70`; `+0x10` the point to defend, `+0x20` the next fidget time | confirmed (code) |
| `0x002c8e48` | `HTLDefenseGoal_Start` | Start | steering off, sight 2 pi, attack-weight override `0x005114b8`; next fidget in 6-6.5 s | confirmed (code) |
| `0x002c8ed0` | `HTLDefenseGoal_End` | End | steering on, sight 1.92 rad, no override | confirmed (code) |
| `0x002c8f28` | `HTLDefenseGoal_AdjustEnemyScore` | score | adds `0x00510b84` for the current target and `0x00510b8c` for an enemy on the far side of the gang tactic's line (a negative side test) | confirmed (code) |
| `0x002c8ff8` | `HTLDefenseGoal_Process` | Process | blocks first (Goal_TryBlock); no enemy: at the post (1 m) turns to the tactic's facing point, else walks back (gait 2, 4 beyond 15 m); an enemy on its side of the line: walks back to the post beyond 0.4 m, sidesteps along the tactic's line (MoveMeleeLine) when he is beyond near range, with a taunt line `0x4a` every 6-6.5 s when his target is a gang mate; in reach pushes an HTLDefenseFight goal (2 s) | confirmed (code) |

### HTLDefenseFight (type 0x65) {#goal-htl-defense-fight}

The defenders' fight goal: a fight goal with an end time that does not chase across the line.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c9578` | `HTLDefenseFightGoal_Init` | init | a fight goal (FightGoal_Init, no limit) given vtable `0x00540e10` and an end time now + the duration at `+0x2c` | confirmed (code) |
| `0x002c95d8` | `HTLDefenseFightGoal_Start` | Start | calls Resume | confirmed (code) |
| `0x002c9600` | `HTLDefenseFightGoal_End` | End | calls Suspend | confirmed (code) |
| `0x002c9628` | `HTLDefenseFightGoal_Resume` | Resume | picks an attack kind for the target and stamps `+0x14` | confirmed (code) |
| `0x002c96a0` | `HTLDefenseFightGoal_Suspend` | Suspend | releases this fighter's active-attacker place on the target | confirmed (code) |
| `0x002c9708` | `HTLDefenseFightGoal_Process` | Process | done without a valid target or slot; a grab, tackle, grabbed, tackled, cuffed, down or stunned state pushes the matching goal (HTLGrabbing, Mounting, Grabbed, Mounted, Arrested, Grounded, Stunned); a target across the line beyond 1.1 x far ends it (unless armed); every 30 updates out of sight ends it; re-targets every 2 s to a nearer threat; blocks; past the end time done; picks the attack (a rear grab kind `0x16` for a class with `+0x134` when the sector allows), stance by charge kind, Brain_CheckAttack, in reach queues it, else closes in (move-to-human 0.3 m, 1000 ms) on the near side | confirmed (code) |

### HTLOffense (type 0x66) {#goal-htl-offense}

The attackers' goal: take a point near the line.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002c9d58` | `HTLOffenseGoal_Init` | init | vtable `0x00540d50`; `+0x10` the point to take | confirmed (code) |
| `0x002c9d88` | `HTLOffenseGoal_Start` | Start | steering off, sight 2 pi | confirmed (code) |
| `0x002c9dd0` | `HTLOffenseGoal_End` | End | steering on, sight 1.92 rad | confirmed (code) |
| `0x002c9e18` | `HTLOffenseGoal_AdjustEnemyScore` | score | -999 for an enemy within 1 m of the tactic's flag (`+0x20`) | inferred |
| `0x002c9f60` | `HTLOffenseGoal_Process` | Process | fight stance on; no enemy: walks to the point (gait 2, 4 beyond 15 m) beyond 2 m and turns to the tactic's second flag (`+0x28`); an enemy: beyond 3 m of the point walks back, ignores an enemy beyond 1.5 x far when unarmed, else pushes an HTLOffenseFight goal (2 s) | confirmed (code) |

### HTLOffenseFight (type 0x67) {#goal-htl-offense-fight}

The attackers' fight goal.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ca260` | `HTLOffenseFightGoal_Init` | init | a fight goal (no limit) given vtable `0x00540cf0` and an end time at `+0x2c` | confirmed (code) |
| `0x002ca2c0` | `HTLOffenseFightGoal_Start` | Start | calls Resume | confirmed (code) |
| `0x002ca2e8` | `HTLOffenseFightGoal_End` | End | calls Suspend | confirmed (code) |
| `0x002ca310` | `HTLOffenseFightGoal_Resume` | Resume | picks an attack kind and stamps `+0x14` | confirmed (code) |
| `0x002ca380` | `HTLOffenseFightGoal_Suspend` | Suspend | releases the active-attacker place | confirmed (code) |
| `0x002ca3e8` | `HTLOffenseFightGoal_Process` | Process | as HTLDefenseFight's, plus: after more than 18 updates of brain `+0x148` a kind-`0x15` attack when it can claim the target; out of reach closes in, with a taunt within 1 m, a fidget (`0x25b`) every 1-2 s while few attack him, and (kind 10) a side picked from the sectors the fighter holds, giving a heading for the attack; a rear-grab kind `0x16` becomes kind 1 when the target is held from the side | confirmed (code) |

### HTLGrabbing (type 0x68) {#goal-htl-grabbing}

A grab under the HTL tactic: throw the grabbed enemy toward the tactic's flag.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002cade0` | `HTLGrabbingGoal_Init` | init | vtable `0x00540db0`; the time limit | confirmed (code) |
| `0x002cae08` | `HTLGrabbingGoal_Start` | Start | with a valid target, turns to the tactic's second flag (`+0x28`) | confirmed (code) |
| `0x002caea8` | `HTLGrabbingGoal_Process` | Process | done when the target is gone or the grab ended; while grabbing queues attack kind `0x19` (the grab throw) toward his facing | confirmed (code) |

### Idle (type 0) {#goal-idle}

The idle-in-place goal of a cheering crowd ([Tactics](ai.md#tactics)), and two empty initialisers next to it.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002caf78` | `IdleGoal_Init` | init | vtable `0x005418f0` (class Idle, type 0); `+0x10` the spot and `+0x20` the heading where it was given, `+0x28` / `+0x29` options (restore the anim set; hold the spot), `+0x24` an anim set | confirmed (code) |
| `0x002cb020` | `IdleGoal_Suspend` | Suspend | with the hold option: takes the anim set id from the manager at `0x00512b04` when a dyn-anim slot holds clip `0x184`; drops the anim set | confirmed (code) |
| `0x002cb0e8` | `IdleGoal_End` | End | with the restore option, drops the anim set | confirmed (code) |
| `0x002cb138` | `IdleGoal_Process` | Process | with the hold option: walks back to the spot beyond 0.3 m (gait 2, 4 beyond 3 m), turns to the heading beyond 15 deg, then sets the anim set once; never ends | confirmed (code) |
| `0x002cb328` | `Stub_ReturnArg_A` | helper | returns its argument (an empty init called by `0x002d9ce0` on `0x006ea068`) | confirmed (code) |
| `0x002cb338` | `Stub_ReturnArg_B` | helper | returns its argument (an empty init called by `0x002d9ce0` on `0x006e9f48`) | confirmed (code) |

### GeneralParlay (type 0x43) {#goal-general-parlay}

The parley goal (`Goal_StartParlay`); its Start and Process are outside this range.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002cb348` | `GeneralParlayGoal_Init` | init | vtable `0x00541890`; `+0x10` the start time, `+0x18` the other human; a Stop action and a turn to him; in parley mode 1 (`0x006ea058`) also resets the other for the mode, marks him dead to the AI and stops and turns him | confirmed (code) |
| `0x002cb538` | `GeneralParlayGoal_End` | End | commands unlocked (`+0x411` = 0), the other human no longer dead to the AI, the scene camera unlocked, and (with `+0x14`) the game's `+0x412` sent to the human's vtable `0x44` | confirmed (code) |

### AddressPerson (type 0x57) {#goal-address-person}

[GoalAddressPerson](ai.md#address-person); its Start and Process are outside this range.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002cc348` | `Goal_AddressPerson` | pusher | GoalAddressPerson: resolves the two humans and pushes the goal | confirmed (code) |
| `0x002cc408` | `AddressPersonGoal_Init` | init | vtable `0x00541830`; `+0x10` the person, `+0x14` / `+0x18` two values, `+0x1c` / `+0x24` options, `+0x28` a speech id from the manager at `0x00512b04` | confirmed (code) |

### Hide (type 0x58) {#goal-hide}

Hide in the shadow at a point, under the hold command's hiding tactic (`0x003128a0`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002cc8a8` | `HideGoal_Init` | init | vtable `0x005417d0`; `+0x10` the hiding point, `+0x20` the direction to face, `+0x3e` 'tight' (0.5 m instead of 1.05 m), `+0x3c` the saved `+0xf4` | confirmed (code) |
| `0x002cc990` | `HideGoal_Process` | Process | fight stance off; every 25-27 s a soldier brain (`+0x2d5`) whose gang is not attacked says line `0x45` (`0xa8` on the levels `0xe`, `0x33`, 9, `0x52`) and his leader within far answers `0xce`; walks to the point (gait 3 near the leader, else 5) out of the shadow, turns to the direction and hides in the shadow (Human_EnterShadow); never ends | confirmed (code) |
| `0x002cce18` | `HideGoal_End` | End | restores brain `+0x21c`, clears the actions, leaves the shadow, restores `+0xf4` | confirmed (code) |

### Tag (type 0x5a) {#goal-tag}

`GoalTag`: run to a tag flag and spray it.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002cce88` | `Goal_Tag` | pusher | GoalTag: pushes a Tag goal (flag, two values, no end) | confirmed (code) |
| `0x002ccf30` | `Goal_TagEx` | pusher | GoalTagEx: as GoalTag with an end value | confirmed (code) |
| `0x002ccfe0` | `TagGoal_Init` | init | vtable `0x00541770`; `+0x10` the tag flag (claimed: Flag_SetUser), `+0x14`, `+0x18` / `+0x1c`, `+0x20` = 1 | confirmed (code) |
| `0x002cd0c0` | `TagGoal_Start` | Start | saves the brain's `+0x265` in `+0x20`, then Resume | confirmed (code) |
| `0x002cd108` | `TagGoal_End` | End | calls Suspend | confirmed (code) |
| `0x002cd130` | `TagGoal_Destroy` | Destroy | releases the tag flag | confirmed (code) |
| `0x002cd198` | `TagGoal_Suspend` | Suspend | releases the tag flag, restores `+0x265`, state 2, and sends the human's vtable `0x44` the handle at `+0x14` | confirmed (code) |
| `0x002cd258` | `TagGoal_Process` | Process | done when the flag is gone, the human cuffed, or another human holds the flag; state 0: runs to the flag (gait 4); state 1: within 1.5 m tells the two particle systems at `+0x18` (message `0x19`, 4) once and queues the Tag action, state 2; state 2: done once the tag state (class state `0x17` or state flag `0x2000000`) ends | confirmed (code) |

### Destroy (type 0x5b) {#goal-destroy}

Wreck the objects of a zone, given cars or glass, for the wreck tactic.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002cd4f8` | `DestroyGoal_Init` | init | vtable `0x00541710`; `+0x2c` the radius round the leader (-1 none), `+0x30` the wait, `+0x32` the zone id, `+0x10`-`+0x1c` up to four cars from handles (`+0x3a` their count) | confirmed (code) |
| `0x002cd640` | `DestroyGoal_ReleaseTarget` | helper | clears an object's or glass pane's claim (`+0xec`) | confirmed (code) |
| `0x002cd698` | `DestroyGoal_PickTarget` | helper | the next target: the zone's nearest object (kind `0x20`) or the first given car with a free spot; failing those, every other try a glass pane within 15 m; an object within the radius of the leader is claimed for the gang for 5 s, with a pane near it preferred | confirmed (code) |
| `0x002cd8f8` | `DestroyGoal_TryPickUp` | helper | empty-handed and free, every 10 updates a smash object within 8 m it may pick up gets GetItem (kind 4) | confirmed (code) |
| `0x002cda28` | `DestroyGoal_IsWaiting` | helper | true when no actions are queued, a wait is set and the human is free (used by the wreck tactic `0x0031e770`) | confirmed (code) |
| `0x002cda68` | `DestroyGoal_Start` | Start | drops the target; `+0x35` set when the gang tactic is type `0x1c`; saves and clears `+0x265` | confirmed (code) |
| `0x002cdae0` | `DestroyGoal_Resume` | Resume | after a GetItem, marks `+0x36` when he came back empty-handed | confirmed (code) |
| `0x002cdb30` | `DestroyGoal_End` | End | Suspend, releases the target, drops what he holds and the target, restores `+0x265` | confirmed (code) |
| `0x002cdbc0` | `DestroyGoal_Process` | Process | fight stance on; without a target waits the given time (+100 ms), trying pick-ups, then picks one; a car with no free spot or three tries drop it; holding a breaker or throwable: turns to the nearest glass or car within 30 m once, else swings at the crowd ahead (attack kind 0, delay `0x7d`) with a taunt line `0x8e`/`0x8f` every 8 s; empty-handed: a car target gets the car goal `0x002ddb40` (50 m), a kind 4/5 object GetItem, any other the smash goal `0x002dd0c8` | confirmed (code) |

### Steal (type 0x5c) {#goal-steal}

Steal the objects of a zone for the steal tactic; its Process is outside this range.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ce0e0` | `StealGoal_Init` | init | vtable `0x005416b0`; `+0x10` the object (none), `+0x14` a handle, `+0x20` the wait, `+0x22` the zone id, `+0x24` the radius round the leader | confirmed (code) |
| `0x002ce138` | `StealGoal_PickTarget` | helper | the zone's nearest object of kind 2; within the radius of the leader it is claimed for the gang for 8 s, else forgotten | confirmed (code) |
| `0x002ce260` | `StealGoal_Start` | Start | clears the target; `+0x28` set when the gang tactic is type `0x1d`; the next try in half to all of the wait | confirmed (code) |
| `0x002ce300` | `StealGoal_End` | End | Suspend, then releases the object's claim | confirmed (code) |
| `0x002ce350` | `StealGoal_Suspend` | Suspend | drops the target | confirmed (code) |
| `0x002ce370` | `StealGoal_ListWitnesses` | helper | fills the visible humans within 8 m (filter `0x0029c1f0`) and clears `+0x14` | inferred |
| `0x002ce3b8` | `StealGoal_FaceTarget` | helper | turns to the target more than 60 deg off (a moving target only when he faces this way) | inferred |

### Steal (type 0x5c): the Process {#goal-steal-process}

The last function of the Steal class (vtable `0x005416b0`); its other functions sit just below this range.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ce5c8` | `StealGoal_Process` | Process | every half to full interval: when the object to steal is gone, finds another; pushes GetItem (`0x2c`) on it; with an enemy (`Brain_PickBestEnemy`) shuffles in the fight stance (2 s) beyond the far range, else pushes a fight goal (2000 ms) | confirmed (code) |

### HangOut (type 0x3b) {#goal-hang-out}

Gang members hanging about on their turf near their leader, in his formation, using flags and harassing passers-by.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ce808` | `HangOutGoal_Init` | init | vtable `0x00541650`; the centre point, the radius, the harass option `+0x49` | confirmed (code) |
| `0x002ce870` | `HangOutGoal_Start` | Start | joins the gang leader's formation; next harass check 1.5 s on | confirmed (code) |
| `0x002ce8e8` | `HangOutGoal_End` | End | `0x0025f450` on the human, leaves the leader's formation | confirmed (code) |
| `0x002ce960` | `HangOutGoal_Suspend` | Suspend | with the gang tactic of type `0x18`, tells it the member left (`0x00312310`); clears the idle flag `+0x48` | confirmed (code) |
| `0x002ce9e0` | `HangOut_TryHarass` | helper | when the harass option is on, the 1.5 s check has come and the brain is type 2: unless he is the leader of a gang of two or more, and while fewer than the gang's share already harass (goal `0x3d`), picks a passer-by from the gang's list `+0x88` in sight and pushes Harass on him (10 m, or 6 m within 6 m); 0 when pushed | confirmed (code) |
| `0x002ced60` | `HangOut_IsIdleLong` | helper | while idling (`+0x48`), 1 when the human's current anim has run 5 s or more (from the tactic at `0x00312430`) | confirmed (code) |
| `0x002cedb0` | `HangOutGoal_Process` | Process | outside the radius walks back to a random point in it (on the turf, on the navigation areas); inside: harass check, a fidget (`0x25b`) when facing the centre, a nearby usable flag (MoveToUseFlag, within 10 m), or FollowFormation to his slot; waits 4-8 s between | confirmed (code) |

### ThrowObject (type 0x5d) {#goal-throw-object}

Walks to a flag and throws what he holds.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002cf480` | `Goal_ThrowObject` | binding | `Goal_ThrowObject`: pushes the goal on a human | confirmed (code) |
| `0x002cf530` | `ThrowObjectGoal_Init` | init | vtable `0x005415f0`; the flag, the range, the gait, a Lua callback (`*(0x00512b04)` `+0xcc`) | confirmed (code) |
| `0x002cf5d0` | `ThrowObjectGoal_End` | End | clears object flag `0x40000` and human `+0x128`; calls the Lua callback ([Scripted goals](ai.md#scripted)) | confirmed (code) |
| `0x002cf690` | `ThrowObjectGoal_Process` | Process | done unless he holds something throwable; walks to within range of the flag, turns to it (15°), throws | confirmed (code) |

### Investigate (type 0x5e) {#goal-investigate}

Goes to look at a point: a noise, a hit or a crime at a car (pushed by `Chase_InvestigateCar` `0x002b0c88` with 5 m on a
crime event at a car). Cops may call backup from it.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002cf8f8` | `InvestigateGoal_Init` | init | vtable `0x00541590`; the point, the radius, flags for the line and the turn | confirmed (code) |
| `0x002cf980` | `InvestigateGoal_Start` | Start | stops speech; turns to the point; counts the brain's investigations (`+0x2d0`, timer A 30 s) | confirmed (code) |
| `0x002cfad0` | `InvestigateGoal_Resume` | Resume | the cautious anim set `0x00566350` | confirmed (code) |
| `0x002cfb18` | `InvestigateGoal_Suspend` | Suspend | marks interrupted unless actions are blocked; normal anim set; restores the turn boost | confirmed (code) |
| `0x002cfb90` | `InvestigateGoal_End` | End | brain `+0x284` = 1; a closing line unless chasing | confirmed (code) |
| `0x002cfc40` | `Investigate_CallBackup` | helper | a cop who sees a fight within 10 m near a cop-call flag (activity `0xe`, within 10 m): counts the backup (`CopBrain_CountBackup`), pushes a fight and calls it (`0x002c4710`, `0x002c4060`); 1 when done | confirmed (code) |
| `0x002cff18` | `Investigate_SayLine` | helper | queues a play-sound action of the line | confirmed (code) |
| `0x002cff70` | `Investigate_LookAround` | helper | the line, then the gang's clip `0x29c` or anim `0x29e` after 500-750 ms; counts `+0x35` | confirmed (code) |
| `0x002d0100` | `InvestigateGoal_Process` | Process | a cop: radio chatter, gives up when two gangs fight within the far range, calls backup; walks to the point and looks around | confirmed (code) |

### TauntPlayer (type 0x63) {#goal-taunt-player}

The ring round a chief who fights one of his own Warriors. `PlayerBrain_Update` (`0x003035d8`), every 13 updates while
the player's brain `+0x2e4` is set ([fight mode](ai.md#warrior-follow)), pushes it on each free member of his gang
within 15 m that has no TauntPlayer goal.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d09f0` | `TauntPlayerGoal_Push` | pusher | pushes the goal on one human against another (from `PlayerBrain_Update`) | confirmed (code) |
| `0x002d0aa8` | `TauntPlayerGoal_Init` | init | vtable `0x00541530`; the target, the option `+0x18` | confirmed (code) |
| `0x002d0ae0` | `TauntPlayerGoal_End` | End | leaves the fight stance | confirmed (code) |
| `0x002d0b00` | `TauntPlayerGoal_Process` | Process | done when the target's `+0x2e4` clears or (option) an outsider attacks either; looks at him, turns to him (60°), keeps 0.75 × far to that + 2 m in the fight stance (move-to-human, 3 s), and every 6-6.5 s a taunt (`0x10`, 30 % `0x8f`) | confirmed (code) |

### Riot (type 0x54): helpers {#goal-riot-helpers}

Helpers of [GoalRiot](ai.md#riot).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d1080` | `RiotGoal_Start` | Start | saves the brain's type `+0x21c` and `+0x28d` | confirmed (code) |
| `0x002d1100` | `Riot_OnAttacked` | event | from `GangBrain_OnEvent`: unless in the 3 s cool-down or fighting, fights back (`Brain_Fight`, 8000) a free attacker or another rioter, with line `0x11` | confirmed (code) |
| `0x002d11f8` | `Riot_AfterFight` | helper | from FindEnemy's End (`0x002c04e8`): line `0x58`, type 0, a 3 s cool-down | confirmed (code) |
| `0x002d1288` | `RiotGoal_TryPickFight` | helper | `RiotGoal_TryPickFight` ([the fight](ai.md#riot)) | confirmed (code) |
| `0x002d1498` | `Riot_PointNearPlayer` | helper | when the nearest player is 15 m or more away, a reachable point 15 m from him; the move deadline 10 s on | confirmed (code) |
| `0x002d15d0` | `Riot_PointInTurf` | helper | a reachable point in one of the gang's turf boxes; the move deadline 10 s on | confirmed (code) |

### Harass (type 0x3d) {#goal-harass}

A gang member follows a passer-by, jeering, while he stays near.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d2318` | `HarassGoal_Push` | pusher | pushes the goal on a free human against another | confirmed (code) |
| `0x002d23e8` | `HarassGoal_Init` | init | vtable `0x00541470`; the target, the give-up distance `+0x18`, the leader leash `+0x1c` | confirmed (code) |
| `0x002d2438` | `HarassGoal_Start` | Start | the harass walk (override `0x2a1`, set `0x00566388`); notes the target's gang | confirmed (code) |
| `0x002d24a8` | `HarassGoal_Resume` | Resume | the harass walk; clears the enemy bits between the two gangs so it does not count as war | confirmed (code) |
| `0x002d2538` | `HarassGoal_Suspend` | Suspend | the walk override off | confirmed (code) |
| `0x002d2560` | `HarassGoal_End` | End | the walk off; frees the gang's harass claim (`+0xe2`); clears the enemy bits | confirmed (code) |
| `0x002d2600` | `HarassGoal_Process` | Process | done when the target goes, is beyond the give-up distance or 1.15 × the leash from the leader; the first harasser claims the gang; walks beside him, line `0x58` every 3 s | confirmed (code) |

### PlayDynAnimation (type 0x22) {#goal-play-dyn-animation}

[GoalPlayDynAnimation](ai.md#dyn-animation).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d2df8` | `Goal_PlayDynamicAnimation` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it (needs a clip name) | confirmed (code) |
| `0x002d2eb0` | `PlayDynAnimGoal_Init` | init | vtable `0x00541410`; the clip into the human's dyn slot (`+0x468`, anim 668), the Lua callback | confirmed (code) |
| `0x002d3038` | `PlayDynAnimGoal_Destroy` | Destroy | releases the dyn slot | confirmed (code) |
| `0x002d3060` | `PlayDynAnimGoal_Process` | Process | waits for the clip, plays anim 668 once, done ([GoalPlayDynAnimation](ai.md#dyn-animation)) | confirmed (code) |

### Scatter (type 0x37) {#goal-scatter}

Runs away from a point, to up to three flags in turn, shoving an enemy who blocks the way.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d3150` | `ScatterGoal_Init` | init | vtable `0x00541290`; the point, the option, the radius 0.5 | confirmed (code) |
| `0x002d31b0` | `ScatterGoal_Start` | Start | saves the type and boosts, drops a kind-4 or 6 object, attack table `0x00511038` | confirmed (code) |
| `0x002d3258` | `ScatterGoal_Resume` | Resume | type 0, turn boost + 1 | confirmed (code) |
| `0x002d3290` | `ScatterGoal_Suspend` | Suspend | type 2, boosts back, out of the shadow | confirmed (code) |
| `0x002d32f8` | `ScatterGoal_End` | End | restores the type and boosts, clears the actions, the target and the attack table | confirmed (code) |
| `0x002d3380` | `Scatter_NextPoint` | helper | the next flag away (8 tries, at most three) with a 2 m radius; else AvoidEnemies (far, 20 m) or a run point (`AvoidEnemies_PickPoint`); 1 when a flag | confirmed (code) |
| `0x002d3560` | `Scatter_IsNear` | helper | within 5 m of the point (from the tactic at `0x00319ff8`) | confirmed (code) |
| `0x002d35b8` | `ScatterGoal_Process` | Process | a non-friend within 1 m who can be hit gets attack kind 17; runs (gait 5) to the point, out of the shadow | confirmed (code) |

### PlayDynIdle (type 0x23) {#goal-play-dyn-idle}

Goes to a flag and plays a level-loaded idle there (in, loop, out clips).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d3940` | `Goal_PlayDynamicIdle` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it (needs a clip name) | confirmed (code) |
| `0x002d3a18` | `PlayDynIdleGoal_Init` | init | vtable `0x005413b0`; the flag, up to three clips into the dyn slots (anims 668 or 671 on), the Lua callback | confirmed (code) |
| `0x002d3c08` | `PlayDynIdleGoal_Start` | Start | not pushable | confirmed (code) |
| `0x002d3c40` | `PlayDynIdleGoal_End` | End | pushable; with the out clip, plays it (`0x2a1`) and sets state `0x20000000` | confirmed (code) |
| `0x002d3d08` | `PlayDynIdleGoal_Destroy` | Destroy | releases the dyn slots | confirmed (code) |
| `0x002d3d58` | `PlayDynIdleGoal_Process` | Process | walks to the flag (1 m), turns to its heading (15°), then plays the clips in turn | confirmed (code) |

### PlaySpecialIdle (type 0x25) {#goal-play-special-idle}

A scripted idle at a point, with a line.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d4138` | `PlaySpecialIdleGoal_Init` | init | vtable `0x00541350`; sets human flags `0x810` (saved), not pushable, clears `+0x11e`/`+0x11f`, breaks a pair | confirmed (code) |
| `0x002d4248` | `PlaySpecialIdleGoal_Destroy` | Destroy | restores the flags, pushable, `+0x11e`/`+0x11f` = 1 (also the end of a scripted goal) | confirmed (code) |
| `0x002d42c0` | `PlaySpecialIdleGoal_Process` | Process | the point on the ground (3 tries); walks there with a line, turns (60°), plays the idle | confirmed (code) |

### PlayGenAnim (type 0x26) {#goal-play-gen-anim}

Plays one generic clip.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d4628` | `Goal_PlayGenericAnimation` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002d46b8` | `PlayGenAnimGoal_Init` | init | vtable `0x005412f0`; the kind, the Lua callback | confirmed (code) |
| `0x002d4748` | `PlayGenAnimGoal_End` | End | calls the Lua callback | confirmed (code) |
| `0x002d47d0` | `PlayGenAnimGoal_Process` | Process | kind 0 or 1 picks anim `0x2d2` or `0x299`; plays it once, done | confirmed (code) |

### StandIdle (type 0x3e) {#goal-stand-idle}

Stands at the spot where it was given, facing the same way.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d48a0` | `Goal_StandIdle` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002d4920` | `StandIdleGoal_Init` | init | vtable `0x00541230`; the human's position and heading, an anim set by name | confirmed (code) |
| `0x002d49e8` | `StandIdleGoal_Start` | Start | nothing | confirmed (code) |
| `0x002d4a18` | `StandIdleGoal_Suspend` | Suspend | the normal anim set | confirmed (code) |
| `0x002d4a58` | `StandIdleGoal_End` | End | nothing | confirmed (code) |
| `0x002d4a80` | `StandIdleGoal_Process` | Process | walks back beyond 0.75 m, turns back beyond 15°, then the anim set | confirmed (code) |

### Cower (type 0x11) {#goal-cower}

A civilian crouches in fear, with three dyn clips, and may run.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d4c38` | `Goal_Cower` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002d4cb8` | `CowerGoal_Init` | init | vtable `0x005411d0` | confirmed (code) |
| `0x002d4cf8` | `CowerGoal_Start` | Start | may not chase; turn boost + 1; drops what he holds; flag `0x100`; loads the clips | confirmed (code) |
| `0x002d4da0` | `CowerGoal_End` | End | restores them; releases the clips | confirmed (code) |
| `0x002d4e10` | `Cower_SetClips` | helper | on: one of two clip sets (51 %) into slots `+0x468`/`+0x490`/`+0x4b8` (anims `0x2a0`, `0x29f`, `0x2a1`); off: releases them | confirmed (code) |
| `0x002d4f00` | `Cower_ClipsLoaded` | helper | all three dyn slots loaded | confirmed (code) |
| `0x002d4f40` | `Cower_FindThreat` | helper | the nearest threat within 3 × far, noted at `+0x10` | inferred |
| `0x002d4fc0` | `Cower_RunAway` | helper | a run point 10 m off (`Ai_FindOpenPoint`) | inferred |
| `0x002d4fe8` | `CowerGoal_Process` | Process | looks at the threat every 45 updates; cowers while he is near, may run | confirmed (code) |

### Hostile (type 0x72) {#goal-hostile}

A civilian's angry reaction ([Civilians](ai.md#civilians)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d5540` | `HostileGoal_Push` | pusher | pushes the goal on a human against another | confirmed (code) |
| `0x002d55d8` | `HostileGoal_Init` | init | vtable `0x00541170`; the target, the range | confirmed (code) |
| `0x002d5648` | `HostileGoal_End` | End | stops the fidget | confirmed (code) |
| `0x002d5668` | `Hostile_GrabWeapon` | helper | with nothing in hand, a smashable object within 5 m to pick up (GetItem); 1 when pushed | confirmed (code) |
| `0x002d5798` | `Hostile_Attack` | helper | timer B 20 s; a civilian of kind 3 fights the target (makes the gangs enemies when no threat yet), line `0x11` | confirmed (code) |
| `0x002d58b8` | `HostileGoal_Process` | Process | looks at the target; done beyond 1.1 × the range; may attack or grab a weapon; else faces him | confirmed (code) |

### CallGang (type 0x6e) {#goal-call-gang}

A gang member runs off to call his gang, with a radar blip, a spinning icon and a tutorial hint (`0x17`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d5c28` | `Goal_CallGang` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it (the target is the second argument) | confirmed (code) |
| `0x002d5cf0` | `CallGangGoal_Init` | init | vtable `0x00541110`; the target's position, the options; loads the call clips | confirmed (code) |
| `0x002d5db8` | `CallGangGoal_Start` | Start | picks the phone flag; human `+0x19e` = 4 and state bit 2; a radar blip in mode 4; a type-4 caller turns to the target; may queue tutorial hint `0x17`; view 2π | confirmed (code) |
| `0x002d5f98` | `CallGangGoal_End` | End | removes the icon and the blip | confirmed (code) |
| `0x002d6050` | `CallGangGoal_Destroy` | Destroy | releases the clips; clears the game's dealer customer | confirmed (code) |
| `0x002d6080` | `CallGang_SetClips` | helper | the call clips (`0x00566420`, anim `0x29c`; a second, `0x29d`) or their release | confirmed (code) |
| `0x002d6130` | `CallGang_FindPhone` | helper | with the phone option, the nearest phone flag (activity 6) within range | confirmed (code) |
| `0x002d6228` | `CallGangGoal_FindCallSpot` | helper | a spot to call from: ray casts round the caller pick the side with most room; 1 when the point is on his turf and he can walk straight to it or reach it (`Nav_CanReach`) | confirmed (code) |
| `0x002d6638` | `CallGangGoal_Process` | Process | state 0: when idle, says line `0xa9`, a spinning icon, looks at the target, plays the call clip (`0x29c`); state 1: moves (gait 5) to the phone flag or a call spot; state 2: when idle, the second line (`0x16`) and the call; then done | confirmed (code) |

### Scout (type 0x6f) {#goal-scout}

A lookout who stands at a post facing a heading and looks about.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d6ca0` | `ScoutGoal_Init` | init | vtable `0x005410b0`; the post and heading, saves the type and chase byte | confirmed (code) |
| `0x002d6db0` | `ScoutGoal_Start` | Start | the scout clips; may not chase; sets `+0x28d` | confirmed (code) |
| `0x002d6e10` | `ScoutGoal_Resume` | Resume | a radar blip; drops a kind-4 or 6 object; type 0, `+0x144` = 500, not pushable | confirmed (code) |
| `0x002d6ef0` | `ScoutGoal_Suspend` | Suspend | the type back, idle tasks, pushable | confirmed (code) |
| `0x002d6f98` | `ScoutGoal_End` | End | releases the clips, restores the bytes | confirmed (code) |
| `0x002d7000` | `Scout_SetClips` | helper | three scout clips (`0x00566458`, `0x00566468`, `0x00566478`) or their release | confirmed (code) |
| `0x002d70a0` | `Scout_ClipsLoaded` | helper | all three dyn slots loaded | confirmed (code) |
| `0x002d70e0` | `Scout_LookAround` | helper | every 45 updates a look-around (1.5 s) | confirmed (code) |
| `0x002d7130` | `Scout_PickPoint` | helper | a reachable point on the turf around the heading | confirmed (code) |
| `0x002d73b8` | `ScoutGoal_Process` | Process | walks back to the post, turns to the heading, plays the clips and looks about | inferred |

### PathScout (type 0x70) {#goal-path-scout}

A lookout who walks a list of flags back and forth.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d7878` | `PathScoutGoal_Init` | init | vtable `0x00541050`; the flag list, loop or ping-pong, saves the brain bytes | confirmed (code) |
| `0x002d7938` | `PathScoutGoal_Start` | Start | the scout clips (not for a cop) | confirmed (code) |
| `0x002d79a8` | `PathScoutGoal_Resume` | Resume | drops a kind-4 or 6 object; type 0 and a radar blip (not for a cop); not pushable | confirmed (code) |
| `0x002d7a98` | `PathScoutGoal_Suspend` | Suspend | idle tasks, pushable | confirmed (code) |
| `0x002d7b50` | `PathScoutGoal_End` | End | releases the clips, restores the bytes | confirmed (code) |
| `0x002d7bc8` | `PathScout_SetClips` | helper | the same three scout clips, or their release | confirmed (code) |
| `0x002d7c68` | `PathScout_ClipsLoaded` | helper | all three dyn slots loaded | confirmed (code) |
| `0x002d7ca8` | `PathScout_LookAround` | helper | a look-around now and then (wider at a flag) | confirmed (code) |
| `0x002d7d10` | `PathScout_NextIndex` | helper | the next flag: steps by ±1, loops or turns back at the ends | confirmed (code) |
| `0x002d7d80` | `PathScout_WalkToFlag` | helper | walks to the flag; near it aims the move along the flag's heading and looks ahead | confirmed (code) |
| `0x002d7fe0` | `PathScout_TimedLine` | helper | every 16 s the line | confirmed (code) |
| `0x002d8028` | `PathScout_SayLine` | helper | line `0x17` when he may gesture | confirmed (code) |
| `0x002d80c8` | `PathScoutGoal_Process` | Process | a state machine: walk to the flag, pause and look, go on | confirmed (code) |

### ReactNoise (type 0x99) {#goal-react-noise}

Stops, waits, turns and looks at a noise.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d84e0` | `ReactNoiseGoal_Init` | init | vtable `0x00540ff0`; the point, the kind | confirmed (code) |
| `0x002d8518` | `ReactNoiseGoal_Start` | Start | loads the clip | confirmed (code) |
| `0x002d8550` | `ReactNoiseGoal_End` | End | brain `+0x284` = 1; releases the clip; idle tasks | confirmed (code) |
| `0x002d85d0` | `ReactNoise_SetClip` | helper | clip `0x00566488` into slot `+0x3c8` (anim `0x29c`) or its release | confirmed (code) |
| `0x002d8618` | `ReactNoise_ClipLoaded` | helper | the slot loaded | confirmed (code) |
| `0x002d8630` | `ReactNoiseGoal_Process` | Process | a wait (250-500 ms), a second, turn and look (2 s), then the clip | confirmed (code) |

### Mark (type 0x9a) {#goal-mark}

A civilian who strolls about his spot, chats and reacts to being approached; the name is the executable's.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d87b0` | `Goal_Mark` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002d8848` | `MarkGoal_Init` | init | vtable `0x00540f90` | confirmed (code) |
| `0x002d88d8` | `MarkGoal_Start` | Start | notes his spot, clears `+0x120`, loads the clips | confirmed (code) |
| `0x002d8930` | `MarkGoal_End` | End | `+0x120` = 1, releases the clips | confirmed (code) |
| `0x002d8958` | `Mark_SetClips` | helper | clips `0x00566498` (anim `0x29d`) and `0x005664b0` (`0x253`) or their release | confirmed (code) |
| `0x002d89c8` | `Mark_ClipLoaded` | helper | the first clip loaded | confirmed (code) |
| `0x002d89e0` | `Mark_SetState` | helper | enters or leaves a reaction (2: looks at the partner and plays `0x253`); a new spot on leaving | confirmed (code) |
| `0x002d8b10` | `MarkGoal_React` | helper | the reaction: 0 plays clip `0x29d` when idle; 2 counts updates, looks at the partner (update 15) or turns its head (30), ends the reaction from update 45 | confirmed (code) |
| `0x002d8db0` | `MarkGoal_FindChatPartner` | helper | a chat partner: a seen human within 5 m with no goal, of the pedestrian goal (`0x69`) or brain type 4, in his view | confirmed (code) |
| `0x002d8ee0` | `MarkGoal_Process` | Process | strays back when beyond the radius (every 4 updates); within a fifth of it, may start a chat with a partner; else strolls to the next flag point | confirmed (code) |

### Backoff (type 0x9b) {#goal-backoff}

Keeps a distance from a human, in the fight stance.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d9238` | `Goal_Backoff` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002d92e8` | `BackoffGoal_Init` | init | vtable `0x00540f30`; the human, the distance | confirmed (code) |
| `0x002d9380` | `BackoffGoal_End` | End | leaves the fight stance | confirmed (code) |
| `0x002d93a0` | `BackoffGoal_Process` | Process | done beyond the distance; keeps the distance band (move-to-human, 2 s), looks at him; every 3 updates checks behind him | confirmed (code) |

### Boxer (type 0x9e) {#goal-boxer}

A boxer sparring with a target.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d97c0` | `Goal_Boxer` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002d9848` | `BoxerGoal_Init` | init | vtable `0x00540ed0`; the target | confirmed (code) |
| `0x002d9870` | `BoxerGoal_Start` | Start | attack table `0x00511120`, the boxing clip `0x005664c0` (anim `0x25b`) | confirmed (code) |
| `0x002d98b0` | `BoxerGoal_End` | End | clears them, drops the target | confirmed (code) |
| `0x002d98f8` | `BoxerGoal_Process` | Process | picks an attack (`Brain_PickAttack` with the filter `Human_CanUseAttackKind`), checks it, moves into its reach and queues it | confirmed (code) |

### The open-direction search {#goal-open-point}

A helper several goals share, and the reset of two lists beside it.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002d9ce0` | `AiLists_Reset` | reset | on a full reset clears the lists at `0x006e9f48` and `0x006ea068` | confirmed (code) |
| `0x002d9d20` | `AiLists_StaticInit` | static init | calls the reset | confirmed (code) |
| `0x002d9d40` | `Ai_FindOpenPoint` | helper | four rays (`WorldManager_RayCast`) ahead and to the sides of the human; a point in the most open direction at the distance, 2-4 tries (from OnFire, PedReaction, Cower, AvoidEnemies and others) | confirmed (code) |

### The MoveTo goals (types 1-6) {#goal-move-to}

[GoalMoveToFlag](ai.md#move-to-flag) and its siblings: to an exit flag, a flag-net flag, a flag to use, a position, a
human.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002da4a0` | `MoveToFlagGoal_End` | End | MoveToFlag: when arrived, message 8 to the flag and the gang's event ([GoalMoveToFlag](ai.md#move-to-flag)) | confirmed (code) |
| `0x002da810` | `Goal_MoveToExitFlag` | binding | `Goal_MoveToExitFlag` (type 2): a non-cop brain is held | confirmed (code) |
| `0x002da960` | `GoalMoveToExitFlag_Start` | Start | MoveToExitFlag: the flag point (or a point on its circle); a camera check 8 s on | confirmed (code) |
| `0x002daa20` | `MoveToExitFlagGoal_End` | End | MoveToExitFlag: when arrived, the gang's event; clears brain `+0x2d7` | confirmed (code) |
| `0x002daac8` | `MoveToExitFlagGoal_Resume` | Resume | MoveToExitFlag: clears the actions and the target | confirmed (code) |
| `0x002dab00` | `MoveToExit_GlanceAround` | helper | a glance to a side (500 ms), forced or every 120 updates for a civilian at gait 5 | confirmed (code) |
| `0x002dacf8` | `GoalMoveToExitFlag_Process` | Process | MoveToExitFlag: one health; every 8 s a line when seen; done when out of every camera's sight | confirmed (code) |
| `0x002db390` | `MoveToFlagNetFlagGoal_Push` | pusher | MoveToFlagNetFlag (type 3): pushes the goal on a human | confirmed (code) |
| `0x002db458` | `MoveToFlagNetFlagGoal_Init` | init | vtable `0x00542070`; the flag, the gait, the radius | confirmed (code) |
| `0x002db4a8` | `MoveToFlagNetFlagGoal_Start` | Start | the target: a point on the flag's circle | confirmed (code) |
| `0x002db508` | `MoveToFlagNetFlagGoal_End` | End | when arrived, the gang's event | confirmed (code) |
| `0x002db5a0` | `MoveToFlagNetFlagGoal_Resume` | Resume | clears the actions | confirmed (code) |
| `0x002db5c0` | `MoveToFlagNetFlagGoal_Process` | Process | inside the radius, done; else a move action (at most 4 tries) | confirmed (code) |
| `0x002db6b0` | `Goal_MoveToUseFlag` | binding | `Goal_MoveToUseFlag` (type 4) | confirmed (code) |
| `0x002db768` | `MoveToUseFlagGoal_Init` | init | vtable `0x00542010`; the flag (taken, `Flag_SetUser`), the gait, the option; drops the target | confirmed (code) |
| `0x002db810` | `MoveToUseFlagGoal_Start` | Start | the flag's clips into the dyn slots (anims `0x29c` or `0x29f` on); not pushable; may not chase | confirmed (code) |
| `0x002db918` | `MoveToUseFlagGoal_End` | End | an out clip while in use, idle tasks, frees the slots, pushable, line `0x20` when set, stops speech | confirmed (code) |
| `0x002dbaa0` | `MoveToUseFlagGoal_Destroy` | Destroy | releases the flag | confirmed (code) |
| `0x002dbb50` | `MoveToUseFlagGoal_Resume` | Resume | clears the actions and the state | confirmed (code) |
| `0x002dbb80` | `MoveToUseFlagGoal_Suspend` | Suspend | stops speech | confirmed (code) |
| `0x002dbba8` | `UseFlag_IsSeatKind` | helper | whether the flag's activity is one of a list of kinds (0, 2, 3, 6 and others) | inferred |
| `0x002dbbf0` | `UseFlag_PhoneLine` | helper | −1 unless the flag's activity is 6 (a phone) | inferred |
| `0x002dbc10` | `MoveToUseFlagGoal_Process` | Process | done when the flag is gone; walks to it; clears the way of humans within 0.75 m; uses it with the clips | confirmed (code) |
| `0x002dc278` | `MoveToPositionGoal_Init` | init | MoveToPosition (type 5), vtable `0x00541fb0`; the point, the gait, the radius, a time limit | confirmed (code) |
| `0x002dc2d0` | `MoveToPositionGoal_Start` | Start | nothing | confirmed (code) |
| `0x002dc2f8` | `MoveToPositionGoal_End` | End | clears the actions | confirmed (code) |
| `0x002dc318` | `MoveToPositionGoal_Resume` | Resume | clears the actions | confirmed (code) |
| `0x002dc338` | `MoveToPositionGoal_Process` | Process | done at the time limit or inside the radius; else a move action (at most 4 tries) | confirmed (code) |
| `0x002dc458` | `Goal_MoveToHuman` | binding | `Goal_MoveToHuman` (type 6) | confirmed (code) |
| `0x002dc4f8` | `MoveToHumanGoal_Init` | init | vtable `0x00541f50` | confirmed (code) |
| `0x002dc540` | `MoveToHumanGoal_Start` | Start | nothing | confirmed (code) |
| `0x002dc578` | `MoveToHumanGoal_Process` | Process | done when cuffed or the human is gone; re-plans every second; moves to him | confirmed (code) |

### GetItem (type 0x2c) {#goal-get-item}

Walks to a world object and picks it up.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002dc708` | `Goal_GetItem` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002dc7a0` | `GetItemGoal_Init` | init | vtable `0x00541ef0`; the object, the kind | confirmed (code) |
| `0x002dc7f8` | `GetItemGoal_Start` | Start | the object's type record; drops other things; turn boost + 1; drops the target | confirmed (code) |
| `0x002dc8b0` | `GetItemGoal_Resume` | Resume | a point beside the object on the ground; none when gone | confirmed (code) |
| `0x002dc9d8` | `GetItemGoal_End` | End | restores the turn boost | confirmed (code) |
| `0x002dc9f8` | `GetItemGoal_Process` | Process | looks at it; done when someone else holds it or he has it; walks to it and picks it up | confirmed (code) |

### DestroyItem (type 0x2d) and VandalizeItem (type 0x2f) {#goal-destroy-item}

Smashing a world object.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002dd0c8` | `DestroyItemGoal_Push` | pusher | pushes DestroyItem on a human | confirmed (code) |
| `0x002dd148` | `DestroyItemGoal_Init` | init | vtable `0x00541e90`; the object | confirmed (code) |
| `0x002dd180` | `DestroyItemGoal_Start` | Start | drops the target and a held object of a set | confirmed (code) |
| `0x002dd1d8` | `DestroyItemGoal_End` | End | clears the actions and the target | confirmed (code) |
| `0x002dd210` | `DestroyItemGoal_Process` | Process | done when broken; walks beside it and hits it | confirmed (code) |
| `0x002dd6f8` | `VandalizeItemGoal_Start` | Start | VandalizeItem: drops the target | confirmed (code) |
| `0x002dd720` | `VandalizeItemGoal_Process` | Process | VandalizeItem: at most three hits; with a throwable in hand, throws it at the object | confirmed (code) |

### DestroyCar (type 0x2e) {#goal-destroy-car}

Smashing a car, from a spot round it.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ddb40` | `DestroyCarGoal_Push` | pusher | pushes the goal on a human | confirmed (code) |
| `0x002ddbd0` | `DestroyCarGoal_Init` | init | vtable `0x00541dd0`; the car, the leash | confirmed (code) |
| `0x002ddc18` | `DestroyCarGoal_Start` | Start | drops the target and a held object; takes a spot round the car (`Car_FindSpotOfHuman`) | confirmed (code) |
| `0x002ddca0` | `DestroyCarGoal_Suspend` | Suspend | frees the spot; drops the target | confirmed (code) |
| `0x002ddd08` | `DestroyCarGoal_End` | End | clears the actions | confirmed (code) |
| `0x002ddd48` | `DestroyCar_GetWeapon` | helper | with nothing in hand, a smashable object within 10 m to pick up (GetItem) | confirmed (code) |
| `0x002dde50` | `DestroyCarGoal_Process` | Process | done when gone or past the leash from the leader; fetches a weapon, walks to the spot, hits the car | confirmed (code) |

### FollowPlayer (type 0x32) {#goal-follow-player}

[The default command: follow](ai.md#warrior-follow).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002de2c8` | `Goal_FollowPlayer` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it (leader and follower handles) | confirmed (code) |
| `0x002de380` | `FollowPlayerGoal_Init` | init | vtable `0x00541d70`; the leader, distance², mode | confirmed (code) |
| `0x002de3c8` | `FollowPlayerGoal_Start` | Start | saves the turn boost; with the gang's `+0xdd` 1 or 2 pushes FollowFormation (0.75 m) | confirmed (code) |
| `0x002de498` | `FollowPlayerGoal_End` | End | restores the turn boost and view (`+0x12c`); leaves the leader's formation | confirmed (code) |
| `0x002de540` | `FollowPlayerGoal_Resume` | Resume | joins the leader's formation, leaves the fight stance; next turn 1-2 s, next fidget 2-3 s | confirmed (code) |
| `0x002de608` | `FollowPlayerGoal_Suspend` | Suspend | clears the actions and the target; restores the turn boost | confirmed (code) |
| `0x002de650` | `FollowPlayerGoal_IsFightMode` | helper | `FollowPlayerGoal_IsFightMode` ([fight mode](ai.md#warrior-follow)) | confirmed (code) |
| `0x002de7c0` | `FollowPlayerGoal_Process` | Process | [`GoalFollowPlayer`'s Process](ai.md#warrior-follow) | confirmed (code) |

### TrackHuman (type 0x30) {#goal-track-human}

[Formations and follow slots](ai.md#formations).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002df1a8` | `Goal_TrackHuman` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it (tracker and tracked handles) | confirmed (code) |
| `0x002df330` | `TrackHumanGoal_End` | End | leaves the tracked human's formation | confirmed (code) |

### FollowObject (type 0x31) {#goal-follow-object}

Follows an object (a car or a thing), matching speed.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002df6c0` | `Goal_FollowObject` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002df760` | `FollowObjectGoal_Init` | init | vtable `0x00541cb0` | confirmed (code) |
| `0x002df7a0` | `FollowObjectGoal_PickSpeed` | helper | `FollowObjectGoal_PickSpeed`: between walk and sprint by distance | confirmed (code) |
| `0x002df890` | `FollowObjectGoal_Start` | Start | nothing | confirmed (code) |
| `0x002df8b8` | `FollowObjectGoal_Resume` | Resume | the object's position as the target | confirmed (code) |
| `0x002df908` | `FollowObjectGoal_Process` | Process | retargets the running move and its speed every n updates | confirmed (code) |

### FollowFormation (type 0x33) {#goal-follow-formation}

[Formations and follow slots](ai.md#formations).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002dfb00` | `Goal_FollowFormation` | binding | `Goal_FollowFormation` (0.75 m) | confirmed (code) |
| `0x002dfba8` | `FollowFormationGoal_Init` | init | vtable `0x00541c50`; a random 2-7 counter | confirmed (code) |
| `0x002dfc30` | `FollowFormationGoal_StartBytes` | Start | saves `+0xf4` and, for a Warrior, human flags `0x100`/`0x200` (sets them) | confirmed (code) |
| `0x002dfcd0` | `FollowFormationGoal_Start` | Resume | `FollowFormationGoal_Start` (the Resume slot): joins the formation, takes the follow point, turns with a running leader | confirmed (code) |
| `0x002dfe68` | `FollowFormationGoal_End` | End | restores the Warrior's flags and the start boost | confirmed (code) |
| `0x002dfef8` | `FollowFormation_PickSpeed` | helper | the speed by distance to the slot: walk, run (4-8 m) or the leader's | confirmed (code) |
| `0x002e0088` | `FollowFormationGoal_Process` | Process | done when the leader is gone or a move failed; follows the slot point | confirmed (code) |

### TravelPath (type 0x38), LeadChase (type 0x49) and RunCarrotRun (type 0x4a) {#goal-travel-path}

Walking a path of flags, alone, leading a chase, or as the carrot ([GoalDevilRun](ai.md#devil-run) for the devil run).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e05a8` | `Goal_TravelPath` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002e0670` | `Goal_TravelPath2` | binding | `Goal_TravelPath2` (with a time limit) | confirmed (code) |
| `0x002e07d0` | `GoalTravelPath_NextPoint` | helper | `GoalTravelPath_NextPoint`: forward, back or looping | confirmed (code) |
| `0x002e0918` | `TravelPath_FaceFlag` | helper | whether to face the flag's heading | inferred |
| `0x002e0950` | `TravelPath_CurrentFlag` | helper | the flag at the current index | confirmed (code) |
| `0x002e0968` | `GoalTravelPath_Process` | Process | pushes MoveToFlag to each point in turn | confirmed (code) |
| `0x002e0a78` | `Goal_LeadChase` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002e0b48` | `LeadChaseGoal_Init` | init | LeadChase: a TravelPath with four distances², vtable `0x00541b90` | confirmed (code) |
| `0x002e0c20` | `LeadChaseGoal_Start` | Start | the first flag; the nearest chaser | confirmed (code) |
| `0x002e0c48` | `LeadChaseGoal_End` | End | when finished, the gang's event | confirmed (code) |
| `0x002e0ce0` | `LeadChase_FindChaser` | helper | the nearest chaser within 50 m | confirmed (code) |
| `0x002e0dc8` | `LeadChaseGoal_Process` | Process | waits for the chaser when he falls behind, runs on when close; slows on low power | confirmed (code) |
| `0x002e1140` | `Goal_RunCarrot` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002e1230` | `RunCarrotGoal_Init` | init | vtable `0x00541b30`; makes a TravelPath sub-goal (`+0x10`) for the path; the gang that chases (`+0x1c`); the hostile flag `+0x25` | confirmed (code) |
| `0x002e1330` | `RunCarrotGoal_End` | End | restores the turn boost, ends the move sub-goal ([brain boosts](ai.md#brain-boosts)) | confirmed (code) |
| `0x002e1388` | `RunCarrotGoal_Free` | Destroy | ends and frees the TravelPath sub-goal | confirmed (code) |
| `0x002e13c0` | `RunCarrotGoal_UpdateSpeed` | helper | the nearest chaser's distance sets the speed between the gait's and the maximum, and the turn boost + 0, 2 or 3 ([brain boosts](ai.md#brain-boosts)); passes the speed to the running move | confirmed (code) |
| `0x002e15e8` | `RunCarrotGoal_Process` | Process | every fourth update the speed; runs the MoveToFlag sub-goal to each path point in turn; done at the end of the path | confirmed (code) |

### DevilRun (type 0x98) {#goal-devil-run}

[GoalDevilRun](ai.md#devil-run).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e18d8` | `DevilRunGoal_Start` | Start | saves the turn boost, threat response and view; speed = the gait's; view 2π ([GoalDevilRun](ai.md#devil-run)) | confirmed (code) |
| `0x002e1950` | `DevilRunGoal_End` | End | restores the turn boost, threat response and view | confirmed (code) |
| `0x002e1998` | `DevilRunGoal_Resume` | Resume | threat response 0; update counter 16 | confirmed (code) |
| `0x002e19b0` | `DevilRunGoal_SegmentOf` | helper | the path segment nearest a position ([GoalDevilRun](ai.md#devil-run)) | confirmed (code) |
| `0x002e1ad0` | `DevilRun_IsPastPoint` | helper | whether a point lies past another along a direction, by the goal's way (`+0x3c`) | confirmed (code) |
| `0x002e1b60` | `DevilRunGoal_FindHindmostSegment` | helper | the hindmost gang member's segment; 0 when the gang has nobody left | confirmed (code) |
| `0x002e1c18` | `DevilRunGoal_ChooseChaser` | helper | the chaser: the live gang member farthest back along the hindmost segment | confirmed (code) |
| `0x002e1e10` | `DevilRunGoal_Pace` | helper | the pace: a hostile runner attacks a close chaser (Melee and engage goals); else speed and turn boost blend by the distance | confirmed (code) |
| `0x002e2230` | `DevilRunGoal_Process` | Process | hindmost segment every 17 updates, chaser every 29, pace every 7; a move to the path's last point at the current speed | confirmed (code) |

### Wander (type 0x3a) {#goal-wander}

Walks between random points in the gang's turf boxes.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e28f8` | `WanderGoal_Init` | init | vtable `0x00541a70` | confirmed (code) |
| `0x002e2958` | `WanderGoal_Start` | Start | leaves the fight stance | confirmed (code) |
| `0x002e2988` | `WanderGoal_Process` | Process | every 30 s a new turf box; random points in it, waits between | confirmed (code) |

### Shadow (type 0xe) {#goal-shadow}

Keeps near a gang's leader.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e2d28` | `ShadowGoal_Init` | init | vtable `0x00541a10`; the gang | confirmed (code) |
| `0x002e2d90` | `ShadowGoal_Process` | Process | follows the gang's leader; re-plans every 2 s or when his gait changes or he is 50 m off | confirmed (code) |

### AvoidEnemies (type 0x20) {#goal-avoid-enemies}

Keeps away from enemies (the Diego fight's `BossDiego_GangAvoid` gives it to the Warriors).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e3090` | `Goal_AvoidEnemies` | binding | script binding: resolves the human, makes the goal with the script's arguments and pushes it | confirmed (code) |
| `0x002e3180` | `AvoidEnemiesGoal_Init` | init | vtable `0x005419b0`; the distances, options, a time limit | confirmed (code) |
| `0x002e3218` | `AvoidEnemiesGoal_Start` | Start | saves the type and chase byte, view 2π, type 0; joins the leader's formation with a leash | confirmed (code) |
| `0x002e32f0` | `AvoidEnemiesGoal_Resume` | Resume | clears the target | confirmed (code) |
| `0x002e3318` | `AvoidEnemiesGoal_End` | End | restores them; leaves the formation | confirmed (code) |
| `0x002e33d0` | `AvoidEnemies_PointBehindFriends` | helper | a point behind a friend of his gang ahead, on the turf, reachable | inferred |
| `0x002e3588` | `AvoidEnemies_PickPoint` | helper | half the time (with a leash) the point behind friends, else `Ai_FindOpenPoint` | confirmed (code) |
| `0x002e3630` | `AvoidEnemiesGoal_Process` | Process | every 1.5 s lists enemies in sight; runs from the nearest | confirmed (code) |

### Confront (type 0x3c): the start {#goal-confront-start}

The first functions of the Confront class (vtable `0x00541950`); its Process (`0x002e4388`) follows this range.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e3fe8` | `ConfrontGoal_Init` | init | vtable `0x00541950`; the other gang, the option | confirmed (code) |
| `0x002e4038` | `ConfrontGoal_Start` | Start | notes his position; measures the distance to the other gang's leader | confirmed (code) |
| `0x002e4188` | `ConfrontGoal_Resume` | Resume | stops the taunt | confirmed (code) |
| `0x002e41b0` | `ConfrontGoal_End` | End | stops the taunt | confirmed (code) |
| `0x002e41d0` | `Confront_LookAtRival` | helper | looks at the nearest rival within 4.75 m; every 30 updates a random rival | inferred |

### Confront (type 0x3c): the Process {#goal-confront-process}

The Process of the Confront goal (its init and Start are in [Confront: the start](#goal-confront-start)); the gang
tactic is [TacticConfront](ai.md#tactics).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e4388` | `ConfrontGoal_Process` | Process | needs both gangs' leaders; every 15 updates the leader check `0x002e41d0`; the leader with the face-off option stops taunting and clears his actions once within the stop distance of the other leader, else walks to him and turns to face him (beyond 15 degrees); a member stays in his leader's formation (FollowFormation, speed 0.75) when farther from the other gang than his slot, else turns to the leader's facing | confirmed (code) |

### PlayAnimation (type 0x21) {#goal-play-animation}

Plays a scene ([Scenes](scenes.md)) on one human; pushed by `Goal_PlayAnimation` (`0x002e48e8`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e4980` | `PlayAnimationGoal_Init` | init | vtable `0x00542370`; the scene, the role, an option, and a Lua callback name looked up through the script system (`*(0x00512b04)` `+0xcc`) | confirmed (code) |
| `0x002e4a50` | `PlayAnimationGoal_End` | End | stops the scene (`Scene_StopWrap`) | confirmed (code) |

### PlayParlayAnimation (type 0x24) {#goal-play-parlay-animation}

The parley scene with the player's answer: the player picks one of two replies with a button in a window of the scene.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e4ab8` | `Goal_PushPlayParlayAnimation` | pusher | pushes the goal on a human's brain (called by `Brain_PlayScene` `0x002cb5d0`) | confirmed (code) |
| `0x002e4b30` | `PlayParlayAnimationGoal_Init` | init | vtable `0x00542310`; the scene, the human, the start time; resets the answer word `+0x412` of the player-1 record (`*(0x0051489c)`) to 2 (none) | confirmed (code) |
| `0x002e4b88` | `PlayParlayAnimationGoal_Start` | Start | binds the human to the scene and plays it | confirmed (code) |
| `0x002e4be0` | `PlayParlayAnimationGoal_Process` | Process | ends (2) when the human is gone; from 3 s to 5 s after the start reads pad 0's pressed buttons: bit `0x4000` writes answer 1, bit `0x1000` answer 0 (cross and triangle in the standard PS2 layout, inferred), then stops the scene; otherwise ends when the scene finishes | confirmed (code) |

### JoinScene (type 0x27) {#goal-join-scene}

`GoalJoinScene`: walk to a point, face a heading, and wait there as a role in a playing scene.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e4cf0` | `Goal_JoinScene` | binding | `Goal_JoinScene`: builds the heading from an angle, pushes the goal; with the run-now option processes it at once | confirmed (code) |
| `0x002e4e28` | `JoinSceneGoal_Init` | init | vtable `0x005422b0`; the point, the heading quaternion, the scene and role, the gait | confirmed (code) |
| `0x002e4e70` | `JoinSceneGoal_Start` | Start | steering off; binds the human to the scene's role; queues a move action to the point at the gait and a turn to the heading | confirmed (code) |
| `0x002e4fe8` | `JoinSceneGoal_End` | End | steering back on | confirmed (code) |
| `0x002e5010` | `JoinSceneGoal_Process` | Process | once the actions are done, ends when the scene has finished | confirmed (code) |

### JoinFixedScene (type 0x28) {#goal-join-fixed-scene}

`GoalJoinFixedScene`: as JoinScene, but the point and facing are the role's start in the scene.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e5048` | `Goal_JoinFixedScene` | binding | `Goal_JoinFixedScene`: pushes the goal; optionally processes it at once | confirmed (code) |
| `0x002e5128` | `JoinFixedSceneGoal_Init` | init | vtable `0x00542250`; the scene, the role, the gait; binds the human to the role | confirmed (code) |
| `0x002e51a8` | `JoinFixedSceneGoal_Start` | Start | the class's Start (vtable `0x00542250` `+0x24`, despite its name): steering off, move action to the role's start (`Scene_GetRoleStart`), then a turn to its facing | confirmed (code) |
| `0x002e52a0` | `JoinFixedSceneGoal_End` | End | steering back on | confirmed (code) |
| `0x002e52c8` | `JoinFixedSceneGoal_Process` | Process | once the actions are done, ends when the scene has finished | confirmed (code) |

### JoinAnimation (type 0x2a) {#goal-join-animation}

`GoalJoinAnimation`: as JoinFixedScene, reading the role's start with the other flag of `Scene_GetRoleStart` and
arriving within 0.1 m.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e5650` | `Goal_JoinAnimation` | binding | `Goal_JoinAnimation`: pushes the goal | confirmed (code) |
| `0x002e56f0` | `JoinAnimationGoal_Init` | init | vtable `0x00542190`; the scene, the role, the gait; binds the human to the role | confirmed (code) |
| `0x002e5770` | `JoinAnimationGoal_Start` | Start | steering off; move action to the role's start (radius 0.1 m, no delay), then a turn to its facing | confirmed (code) |
| `0x002e5870` | `JoinAnimationGoal_End` | End | steering back on | confirmed (code) |
| `0x002e5898` | `JoinAnimationGoal_Process` | Process | once the actions are done, ends when the scene has finished | confirmed (code) |

### Shopkeeper (type 0x82) {#goal-shopkeeper}

`GoalShopkeeper`: a store clerk who idles with store props and reacts to trouble in the store's box (the store crimes:
[Crimes](crimes.md)). Its Process is `ShopkeeperGoal_Process` (`0x002e6668`).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e58d0` | `Shopkeeper_FlagSearchCallback` | helper | callback for the flag search in the Process: among flags in the store box, keeps the nearest free usable flag with clips and, with the any-flag option, the nearest flag of any kind | confirmed (code) |
| `0x002e5a00` | `Goal_Shopkeeper` | binding | `Goal_Shopkeeper`: pushes the goal | confirmed (code) |
| `0x002e5ae0` | `ShopkeeperGoal_Init` | init | vtable `0x005423d0`; the store box, the options (`+0x57` broom, `+0x5a`), two Lua callback names (disturbed, robbed: inferred) | confirmed (code) |
| `0x002e5bf0` | `ShopkeeperGoal_Start` | Start | brain `+0x21c` = 0, brain flag 2; 30 % chance of a phone variant; notes his post; with the broom option puts a broom in his hand; resets | confirmed (code) |
| `0x002e5d00` | `ShopkeeperGoal_Resume` | Resume | sets the idle clip overrides by variant (broom, phone, cower), leaves the fight stance, next fidget in 10-20 s; brain type 6 | confirmed (code) |
| `0x002e5e50` | `ShopkeeperGoal_Suspend` | Suspend | clears the clip overrides and releases his flag | confirmed (code) |
| `0x002e5f40` | `ShopkeeperGoal_End` | End | brain `+0x21c` = 2, clears brain flag 2, resets | confirmed (code) |
| `0x002e5fa0` | `ShopkeeperGoal_OnDisturbed` | event | `ShopkeeperGoal_OnDisturbed`: a disturbance inside the store box (or by an intruder in it): clears his actions, calls the Lua callback once, and by kind cowers, says line `0xbc` or turns hostile toward a non-friendly offender | confirmed (code) |
| `0x002e63e0` | `ShopkeeperGoal_OnPlayerAction` | event | from the civilian brain's event handler: when the player acts on him (not busy): turns to him; kind 7 gives the player 20-47 money (item 2, `Inventory_AddItem`; a robbery, inferred), kind 9 has the player say line `0xc5` | confirmed (code) |
| `0x002e65b0` | `ShopkeeperGoal_Reset` | helper | `ShopkeeperGoal_Reset`: unless robbed (state 11) clears the reaction bytes, restores the idle clip, clears the target; brain type 6 | confirmed (code) |

### Shot tests (shared by the shooter goals) {#goal-shot-tests}

Ray tests used by `ShooterGoal_Fire` and the StationaryShooterB fire code below.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e7608` | `Goal_LineTest` | helper | `Goal_LineTest`: casts the shot ray against bodies (`IPhysics_RayCastBodies`) and the world; keeps the nearer hit and writes its point and normal to the result (`+0x90`, `+0xa0`) | confirmed (code) |
| `0x002e77a0` | `Shot_CanHitHuman` | helper | 1 when a ray from the gun to the human's chest (1.7 m up) over a range hits him before anything else; 0 when he is knocked down | confirmed (code) |

### Fatigued (type 0x90) {#goal-tired}

The tired spell of a boss after a run of hits or throws: [The Diego and Vargas fight](ai.md#boss-diego-vargas).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e7970` | `TiredGoal_Init` | init | `TiredGoal_Init`: vtable `0x00542a00`; deadline (fatigue ms), damage limit (percent of max health) | confirmed (code) |
| `0x002e7a68` | `TiredGoal_Start` | Start | `TiredGoal_Start`: saves the god / no-react / unstunnable flags, clears them, stuns him, sets the deadline | confirmed (code) |
| `0x002e7b30` | `TiredGoal_End` | End | `TiredGoal_End`: restores the flags, ends the stun, clears `0x8000000` | confirmed (code) |
| `0x002e7c18` | `TiredGoal_StartBreak` | helper | `TiredGoal_StartBreak`: damage limit reached: god mode, anims 671 then 672, line `0x22` | confirmed (code) |
| `0x002e7da8` | `TiredGoal_EndBreak` | helper | `TiredGoal_EndBreak`: anim 673 after a break | confirmed (code) |
| `0x002e7e68` | `TiredGoal_IsBreakDone` | helper | `TiredGoal_IsBreakDone`: 1 two seconds after the break clips end (state code 18) | confirmed (code) |
| `0x002e7f50` | `TiredGoal_Process` | Process | `TiredGoal_Process`: stays stunned until the deadline or the damage limit, then recovers (line `0x96`, anim 643) | confirmed (code) |

### BigBrawler (type 0x84) {#goal-big-brawler}

Diego, and Vargas at stage 3: [The Diego and Vargas fight](ai.md#boss-diego-vargas).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e81b8` | `Goal_BigBrawler` | binding | `Goal_BigBrawler` (Lua `GoalBigBrawler`): pushes the goal with default tables | confirmed (code) |
| `0x002e8288` | `BigBrawlerGoal_Init` | init | `BigBrawlerGoal_Init`: vtable `0x005429a0`; flag, cycles, fatigue, damage and prone tables, 8 objects, an item in hand | confirmed (code) |
| `0x002e8418` | `BigBrawlerGoal_Start` | Start | `BigBrawlerGoal_Start`: no threat response or pick-ups, all-round sight, flags `0x223c0`; saves the brain boosts | confirmed (code) |
| `0x002e84f0` | `BigBrawlerGoal_RestoreBoosts` | Resume | `BigBrawlerGoal_RestoreBoosts`: restores the brain's turn and start boosts | confirmed (code) |
| `0x002e8530` | `BigBrawlerGoal_End` | End | `BigBrawlerGoal_End`: undoes Start | confirmed (code) |
| `0x002e8620` | `BigBrawlerGoal_AdjustEnemyScore` | score | `BigBrawlerGoal_AdjustEnemyScore`: range, player and knocked-down bonuses by state | confirmed (code) |
| `0x002e8818` | `BigBrawlerGoal_GetFatigueMs` | helper | `BigBrawlerGoal_GetFatigueMs`: fatigue of the stage x 1000 | confirmed (code) |
| `0x002e8830` | `BigBrawlerGoal_GetDamagePercent` | helper | `BigBrawlerGoal_GetDamagePercent`: damage of the stage | confirmed (code) |
| `0x002e8840` | `BigBrawlerGoal_SetStage` | helper | `BigBrawlerGoal_SetStage`: the stage byte `+0x3f` (below 4) | confirmed (code) |
| `0x002e8858` | `BigBrawlerGoal_OnAttackWarning` | event | `BigBrawlerGoal_OnAttackWarning` (event `0x10`): counters an escapable grab or tackle, retargets, shoves off a near attacker | confirmed (code) |
| `0x002e8bc8` | `BigBrawlerGoal_OnHit` | event | `BigBrawlerGoal_OnHit` (tactic event 1): help call at 20 m, counts hits; the sixth pushes the tired goal | confirmed (code) |
| `0x002e8da8` | `BigBrawlerGoal_NextObject` | helper | `BigBrawlerGoal_NextObject`: the next of the eight object types to spawn in hand | confirmed (code) |
| `0x002e8ee0` | `BigBrawlerGoal_SpawnObjectInHand` | helper | `BigBrawlerGoal_SpawnObjectInHand`: in state 3, the next object into his hand | confirmed (code) |
| `0x002e8f38` | `BigBrawlerGoal_Reset` | helper | `BigBrawlerGoal_Reset`: state 1, no attack kind, grab flag clear | confirmed (code) |
| `0x002e8f78` | `BigBrawlerGoal_Process` | Process | `BigBrawlerGoal_Process`: states 0 taunt, 1 engage, 2 fight, 3 flag and object cycle | confirmed (code) |

### BigFighter (type 0x85) {#goal-big-fighter}

A boss fighter (Lua `GoalBigFighter`). Big Mo has it in level 11, set to the tactic's stage, and Virgil has it in
stage 3 of level 93 ([AI: The boss fights](ai.md#boss-fights)). "Giant" means a model scale above 1.1. Confirmed (code)
at `0x002eabc8`, `0x002ea3c8`, `0x002ea218` and `0x002ea038`, with the [attack kinds](ai.md#attack-kinds) as numbered
there.

**The fields**:

- `+0x10` his weapon (the world object at human `+0x364`);
- `+0x14` the class record's `+0x1c` saved by Start;
- `+0x18` the chosen attack kind (45 = none);
- `+0x1c` a timer;
- `+0x20` the break timer;
- `+0x24` the state;
- `+0x25` the level (1-3);
- `+0x27` hits taken in state 2;
- `+0x28` guard on;
- `+0x29` counter-attack;
- `+0x2a` the duck variant;
- `+0x2c` "just attacked";
- `+0x2d` the guard count;
- `+0x2e` the break stage (0 none, 1 started, 2 over, 3 ending);
- `+0x30` pick-up tries;
- `+0x31` state 4's taunt done.

**The level** (`BigFighterGoal_SetLevel`). Level 1 zeroes attack weight 16 (Start does this too). Level 2 sets
weight 16 to 20 and weight 11 to 30. Level 3 sets weight 16 to 40 and weight 11 to 10. From level 2 up, a state still
0 becomes 2.

**The states.** Nothing runs while actions are queued or during a break (`+0x2e` is 1 or 2).

| State | Entered with | Each update |
| --- | --- | --- |
| 0 taunt | the init | Once the human is free (no held or state flags): line `0x57` (giant) or `0x11` when he may gesture, anim 643 (`0x283`). Goes to 2. |
| 1 guard | timer 1.25 s; guard on; duck variant at 50 %; stun ended; block flag `0x800`; gives up his attack slot. A normal-size fighter also loses flags `0x80020280`, gets back the saved `+0x1c` and weight 16 = 0. | The guard ends (→ 2 once he stops blocking) when the enemy is in a standing reaction, his actions are blocked, or he is farther than the brain's near range (`+0x13c`). Before the timer runs out, unless ducking, he attacks when countering (`+0x29`) or in the duck variant with under 500 ms left: kind 7 (from level 2, kind 16 at 31 %); a counter-attack instead uses kind 7 at 45 / 30 / 25 % by level, else 11 (a giant: 0). Otherwise he counters an escapable tackle or grab (command 3), else holds block (command 4). When the timer runs out: if the enemy is attacking him, counter-attack is set and the timer restarts at 1.25 s; else the guard ends. |
| 2 attack | timer 0; turn boost restored; guard flags cleared | Every 4 s (or with no enemy) re-picks the best enemy. Without that enemy's attack slot he drops the target. Picks a kind (mask `0x2240e8ffff0000`) when none is chosen. His weapon on the ground, the enemy beyond 1.2 m or blocked: walks to it (gait 2) when more than 1 m away, then picks it up; after 5 tries the weapon is moved to him. In reach: queues the kind; a giant's kinds 16 and 11 say line `0xe` (50 %). Out of reach: moves to the enemy. Not ready to attack: shuffles (20 %, just after an attack) or moves to within 0.95 × near range, with line `0x11` or `0x8f` 10 % of the time. |
| 3 dazed | 2 s timer; drops his target; flag `0x8000000`; stunned | Line 8 until the timer ends, then ends the stun, then → 1. |
| 4 enraged | all counters cleared; block flag. A normal-size fighter gets flags `0x80020280`, a push factor of 0.1 and weight 16 = 80. | First update: drops what he holds, then line `0x8f` and anim 643. After that it is like state 2, but every 4 s he picks kind 22 (grab, 30 %) or 16. A kind 16 in reach plays anim 21 (`0x15`, 30 %) or 645 (`0x285`), with line 8, instead of the attack. |

**The guard counter** (`BigFighterGoal_OnAttackWarning`). Outside a break and state 3, a warning of an incoming
attack clears a move. Against an escapable tackle or grab it presses command 3. Then, unless he is attacking, he
retargets the attacker.

**The hit thresholds** (`BigFighterGoal_OnHit`, damage event 1):

- **During a break**, a hit ends the 4 s wait (the break timer becomes 1).
- **In state 2**:
    - each hit calls the gang for help at 20 m and counts in `+0x27`;
    - on the **2nd** hit, `+0x2d` steps on modulo 4 (10 for a giant);
    - at 0 he goes to state 4 (a giant to state 3), otherwise to state 1.

  So a normal-size fighter guards after every second hit and is enraged on every eighth. A giant is dazed on every
  twentieth.
- **In state 4**, after the taunt, a thrown object of type class 5 (object type `+0x87`) puts him back to guard. So
  does `Human_HasState2000`.
- States 0, 1 and 3 ignore hits.

**The break** (Moe's stages, [Boss Moe](ai-code.md#t1-boss-moe)). At 75 % health (stage 1) or 50 % (stage 2) the
tactic caps the health and calls `StartBreak`:

1. He drops what he holds and leaves the fight stance. Anim 671 (`0x29f`) plays, with line `0x22` the first time.
2. Once his action code is 18 with free hands, `UpdateBreak` waits 4 s, or less when a hit cuts it short.
3. `EndBreak` plays anim 673 (`0x2a1`), sets state 2 and the break stage 3. The tactic then moves to the next stage.
4. `ClearBreak` drops the block flag on his next update.

In stage 3 the tactic's check calls `Stagger` whenever his hands are free. `Stagger` clears his actions and plays
anim 668 (`0x29c`), and the tactic reports 8 `TacArrived`; health plays no part. It sets `+0x21` but does not read it,
so this repeats on each update until the script changes the tactic (inferred).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002e9c60` | `Goal_BigFighter` | binding | `Goal_BigFighter`: pushes the goal | confirmed (code) |
| `0x002e9cd0` | `BigFighterGoal_Init` | init | vtable `0x00542940`; level 1, attack kind none (`0x2d`); the world object at human `+0x364` (his weapon) | confirmed (code) |
| `0x002e9d88` | `BigFighterGoal_Start` | Start | no threat response, all-round sight, steering off; saves the turn boost and the class record's `+0x1c`; at level 1 zeroes attack weight `0x10`; a giant gets flags `0x80020280` and the heavy push factor | confirmed (code) |
| `0x002e9e90` | `BigFighterGoal_Suspend` | Suspend | restores the saved turn boost | confirmed (code) |
| `0x002e9eb0` | `BigFighterGoal_End` | End | undoes Start | confirmed (code) |
| `0x002e9fd0` | `BigFighterGoal_AdjustEnemyScore` | score | -20 for an enemy who is down or stunned standing | confirmed (code) |
| `0x002ea038` | `BigFighterGoal_SetLevel` | helper | sets the level `+0x25` (from the Moe tactic) and the attack weights `0x10` and `0xb` it allows | inferred |
| `0x002ea0b8` | `BigFighterGoal_OnAttackWarning` | event | outside a break: clears a move, counters an escapable tackle or grab (command 3), retargets the attacker | confirmed (code) |
| `0x002ea218` | `BigFighterGoal_OnHit` | event | from the Virgil tactic and the gang brain: in a break, ends it; holding an object, drops to state 1; otherwise a help call at 20 m and a hit count: at 2 (a giant: more) switches state (`0x002ea3c8`) | confirmed (code) |
| `0x002ea3a0` | `BigFighterGoal_PickAttack` | helper | `Brain_PickAttack` with the given mask | confirmed (code) |
| `0x002ea3c8` | `BigFighterGoal_SetState` | helper | 1 guard: clears actions, 1.25 s timer, 50 % chance of the duck variant, ends a stun, block flag `0x800`, releases his attack slot; 2 attack: clears the guard flags; 3: pick up the object | confirmed (code) |
| `0x002ea6f0` | `BigFighterGoal_StartBreak` | helper | ends the stun and the fight stance, anims 671 then 672, line `0x22` once | confirmed (code) |
| `0x002ea840` | `BigFighterGoal_EndBreak` | helper | at state code 18 and free hands, anim 673 | confirmed (code) |
| `0x002ea8f8` | `BigFighterGoal_UpdateBreak` | helper | at state code 18: after 4 s goes to the break's end; else starts the break | confirmed (code) |
| `0x002ea9b8` | `BigFighterGoal_ClearBreak` | helper | after a break, clears the block flag and the break state | confirmed (code) |
| `0x002ea9f0` | `BigFighterGoal_Stagger` | helper | clears actions, ends the stun and fight stance, anim 668 | confirmed (code) |
| `0x002eab08` | `BigFighterGoal_IsGiant` | helper | 1 when the human's scale is above 1.1 | confirmed (code) |
| `0x002eab50` | `BigFighterGoal_WantsCounter` | helper | from `Brain_WantsCounter`: a normal-size fighter ducking against attack `0x101` presses command `0x10` | confirmed (code) |
| `0x002eabc8` | `BigFighterGoal_Process` | Process | state 0: taunts (anim 643); state 1: guards, counters escapable grabs (command 3) or blocks (command 4), after the timer attacks with a 45/30/25 % chance by level (`Brain_QueueAttack`); state 2: re-picks the best enemy every 4 s, holds his attack slot, closes in (MoveToHuman at 0.95 x near) or shuffles, lines `0x47` / `0xe` / 8; state 3: walks to and picks up his object | confirmed (code) |

### BigFighterA (type 0x86) {#goal-big-fighter-a}

A variant of BigFighter pushed by the Chatterbox boss tactic (`0x0030e1d8`): the same guard and attack states, plus a
recovery when his health falls to 15 %.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ebb38` | `BigFighterAGoal_Init` | init | vtable `0x005428e0`; attack kind none, level 1 | confirmed (code) |
| `0x002ebbb0` | `BigFighterAGoal_Start` | Start | no threat response, all-round sight, steering off, brain `+0x2d3` = 1; a giant gets flags `0x80020380` | confirmed (code) |
| `0x002ebca0` | `BigFighterAGoal_End` | End | undoes Start; pushable again | confirmed (code) |
| `0x002ebdb8` | `BigFighterAGoal_AdjustEnemyScore` | score | -20 for an enemy who is down or stunned standing | confirmed (code) |
| `0x002ebe20` | `BigFighterAGoal_OnAttackWarning` | event | as BigFighter's | confirmed (code) |
| `0x002ebf98` | `BigFighterAGoal_OnHit` | event | a thrown object of kind `0x1d` clears his guard; stunned at 15 % health or less counts and resets health; otherwise a help call and, when the hit leaves 15 % or less, a state change | confirmed (code) |
| `0x002ec1d8` | `BigFighterAGoal_PickAttack` | helper | `Brain_PickAttack` with the given mask | confirmed (code) |
| `0x002ec200` | `BigFighterAGoal_SetState` | helper | as BigFighter's, and pushable in guard | confirmed (code) |
| `0x002ec460` | `BigFighterAGoal_ClearBreak` | helper | after a break, clears the block flag and the break state | confirmed (code) |
| `0x002ec4a0` | `BigFighterAGoal_AllowsReaction` | helper | from `Brain_TopGoal86Allows`: 1 outside the recovery; in state 3 clears his guard flag | confirmed (code) |
| `0x002ec4e8` | `BigFighterAGoal_IsGiant` | helper | 1 when the human's scale is above 1.1 | confirmed (code) |
| `0x002ec530` | `BigFighterAGoal_CheckRecover` | helper | at 15 % health or less (once armed): resets health, block and stun flags, plays anim 665 (with anim 11 first near a point) | confirmed (code) |
| `0x002ec780` | `BigFighterAGoal_Process` | Process | as BigFighter's: taunt (anim 643 after 0-750 ms), guard, attack with `Brain_PickAttack` and `Brain_QueueAttack`, close in or shuffle, lines `0xe` / `0x47` / 8 | confirmed (code) |

### BigThrower (type 0x87) {#goal-big-thrower}

Vargas at stage 2: [The Diego and Vargas fight](ai.md#boss-diego-vargas).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ecfe8` | `Goal_BigThrower` | binding | `Goal_BigThrower` (Lua `GoalBigThrower`): pushes the goal | confirmed (code) |
| `0x002ed0a8` | `BigThrowerGoal_Init` | init | `BigThrowerGoal_Init`: vtable `0x00542880`; flag, cycles (throws before tiring), fatigue, damage, 8 objects | confirmed (code) |
| `0x002ed168` | `BigThrowerGoal_Start` | Start | `BigThrowerGoal_Start`: as BigBrawler's, plus the pick-up clip override | confirmed (code) |
| `0x002ed260` | `BigThrowerGoal_End` | End | `BigThrowerGoal_End`: undoes Start | confirmed (code) |
| `0x002ed348` | `BigThrowerGoal_GetFatigueMs` | helper | `BigThrowerGoal_GetFatigueMs`: fatigue x 1000 | confirmed (code) |
| `0x002ed358` | `BigThrowerGoal_GetDamagePercent` | helper | `BigThrowerGoal_GetDamagePercent`: the damage percent | confirmed (code) |
| `0x002ed360` | `BigThrowerGoal_NextObject` | helper | `BigThrowerGoal_NextObject`: the next object type to spawn in hand | confirmed (code) |
| `0x002ed498` | `BigThrowerGoal_SpawnObjectInHand` | helper | `BigThrowerGoal_SpawnObjectInHand`: in state 2, the next object into his hand | confirmed (code) |
| `0x002ed4f0` | `BigThrowerGoal_HoldDistance` | helper | `BigThrowerGoal_HoldDistance`: keeps his distance while his hand is busy | confirmed (code) |
| `0x002ed718` | `BigThrowerGoal_Process` | Process | `BigThrowerGoal_Process`: taunt, walk to the flag, pick up, aim, throw; tires after the cycles | confirmed (code) |

### BigLedgeThrower (type 0x8a) {#goal-big-ledge-thrower}

`GoalBigLedgeThrower`: a thrower on a ledge or post who taunts and throws spawned objects down at his enemy; also pushed
by the Chatterbox and Virgil boss tactics.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002edf20` | `Goal_BigLedgeThrower` | binding | `Goal_BigLedgeThrower` (Lua): pushes the goal | confirmed (code) |
| `0x002edff8` | `BigLedgeThrowerGoal_Init` | init | `BigLedgeThrowerGoal_Init`: vtable `0x00542820`; his post and heading, up to 8 object types, the wait, an option, a throw clip loaded into the dynamic slot (anim 598) | confirmed (code) |
| `0x002ee198` | `BigLedgeThrowerGoal_Start` | Start | all-round sight, steering off, flags `0x601c0`, heavy push factor; character `0x87` gets `0x200` (and `0x10` with the option); others except `0xb2` get the pick-up clip override (anim 549) | confirmed (code) |
| `0x002ee2c8` | `BigLedgeThrowerGoal_End` | End | undoes Start | confirmed (code) |
| `0x002ee3a8` | `BigLedgeThrowerGoal_Destroy` | Destroy | releases the dynamic clip slot | confirmed (code) |
| `0x002ee3d0` | `BigLedgeThrowerGoal_PlayTaunt` | helper | plays the taunt clip (the dynamic one, or another when the enemy is below him) and a line | confirmed (code) |
| `0x002ee4f0` | `BigLedgeThrowerGoal_FaceTarget` | helper | turns toward the enemy when more than 22.5 degrees off | confirmed (code) |
| `0x002ee608` | `BigLedgeThrowerGoal_MaybeTaunt` | helper | with free hands: 50 % chance to face the enemy and taunt (line `0xb`), else line `0xb` when silent | confirmed (code) |
| `0x002ee6e0` | `BigLedgeThrowerGoal_NextObject` | helper | the next object type after the last one used, round the list | confirmed (code) |
| `0x002ee758` | `BigLedgeThrowerGoal_SpawnObjectInHand` | helper | in state 2, the next object into his hand | confirmed (code) |
| `0x002ee7b0` | `BigLedgeThrowerGoal_Process` | Process | `BigLedgeThrowerGoal_Process`: fight stance; with no enemy turns to his heading and taunts (line `0x57`, anim 643); back to the post when more than 0.5 m off; picks the best enemy, spawns an object, aims and throws (command `0x10`), waits; sometimes runs (gait 4) to a point 3-5 m away | confirmed (code) |

### StationaryThrower (type 0x8c) {#goal-stationary-thrower}

`GoalStationaryThrower`: stay at a spot and throw spawned objects at visible enemies (the Diego and Vargas minions,
[above](ai.md#boss-diego-vargas)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002eee50` | `Goal_StationaryThrower` | binding | `Goal_StationaryThrower` (Lua): pushes the goal | confirmed (code) |
| `0x002eeee0` | `StationaryThrowerGoal_Init` | init | `StationaryThrowerGoal_Init`: vtable `0x005427c0`; his spot (current position), up to 8 object types | confirmed (code) |
| `0x002eef78` | `StationaryThrowerGoal_Start` | Start | all-round sight, steering off, flag `0x40000` (`0x800` without a boss tactic), heavy push factor, the pick-up clip override (anim 549) | confirmed (code) |
| `0x002ef058` | `StationaryThrowerGoal_End` | End | undoes Start | confirmed (code) |
| `0x002ef130` | `StationaryThrowerGoal_SpawnObjectInHand` | helper | in state 1, the next object into his hand | confirmed (code) |
| `0x002ef188` | `StationaryThrowerGoal_NextObject` | helper | the next object type, skipping while the Diego and Vargas tactic has no free object slot | confirmed (code) |
| `0x002ef2c0` | `StationaryThrowerGoal_Process` | Process | `StationaryThrowerGoal_Process`: with no enemy, every 5 updates picks a visible one within 0.75 x sight; walks back when more than 0.5 m off; state 1 pick up, state 2 turn to the enemy (500-1000 ms) and throw (command `0x10`), then waits; 20 % taunt | confirmed (code) |

### StationaryShooter (type 0x8d) {#goal-stationary-shooter}

A gunman at a post with the level-55 gun clips (aim, turn 60 / 120 degrees, three reloads); pushed by the Lizzies boss
tactic (`BossLizziesTactic_AssignGoal` `0x0030ab58`, [code](ai-code.md#t1-boss-lizzies)). His shots use the same steps
as `ShooterGoal_Fire` ([below](#goal-shooter)) through his own copies of the helpers.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002ef7a8` | `StationaryShooterGoal_Init` | init | vtable `0x00542760`; spread angles, his post, the delays `+0x44` / `+0x46`, the flags | confirmed (code) |
| `0x002ef8c0` | `StationaryShooterGoal_Start` | Start | all-round sight, steering off, flags `0x2210`; loads the gun clips | confirmed (code) |
| `0x002ef990` | `StationaryShooterGoal_End` | End | undoes Start; releases the gun clips | confirmed (code) |
| `0x002efa58` | `StationaryShooterGoal_LoadGunClips` | helper | loads (1) or releases (0) the seven gun clips in the dynamic slots (anims 388-393, 671-673) | confirmed (code) |
| `0x002efb48` | `StationaryShooterGoal_IsLoading` | helper | 1 while a dynamic clip of his gang is still loading | inferred |
| `0x002efbe8` | `StationaryShooterGoal_ReturnToPost` | helper | 1 at his post (within 0.5 m); else walks back and waits 1.5 s | confirmed (code) |
| `0x002efcc0` | `StationaryShooterGoal_FaceTarget` | helper | at his post, turns the body toward the target | confirmed (code) |
| `0x002efdc8` | `StationaryShooterGoal_SignalGun` | helper | sends message `0x34` with two numbers to the gun he holds (human `+0x360`): ammo and reload state, inferred | inferred |
| `0x002efe68` | `StationaryShooterGoal_ApplySpread` | helper | copy of `ShooterGoal_ApplySpread` | confirmed (code) |
| `0x002f0178` | `StationaryShooterGoal_LineTest` | helper | copy of `Goal_LineTest` | confirmed (code) |
| `0x002f0318` | `StationaryShooterGoal_PickTarget` | helper | with two players, prefers one within 4 m of his flag who is a valid enemy, at random when both are | confirmed (code) |
| `0x002f04b8` | `StationaryShooterGoal_CanHitHuman` | helper | copy of `Shot_CanHitHuman` | confirmed (code) |
| `0x002f0690` | `StationaryShooterGoal_IsNearestThreat` | helper | 1 when he is the target's nearest threat | confirmed (code) |
| `0x002f06d0` | `StationaryShooterGoal_WarnTarget` | helper | sends message 1 (a shot warning, kind 2) to the target | confirmed (code) |
| `0x002f0788` | `StationaryShooterGoal_ImpactEffects` | helper | at a world hit, spawns the dust puff and the spark emitter particles | confirmed (code) |
| `0x002f0930` | `StationaryShooterGoal_Say` | helper | a speech line of a kind (0-4) unless one is playing | confirmed (code) |
| `0x002f0a50` | `StationaryShooterGoal_Fire` | helper | one shot: spread, ray to 30 m, damage when it can hit a human, impact effects or a warning, the gunshot sound, the fire clip (668) and the wait | confirmed (code) |
| `0x002f0ec0` | `StationaryShooterGoal_Process` | Process | faces the target; at his post loops the aim clip, picks a target, fires and reloads (clip 673, gun message), speaks; waits between shots | confirmed (code) |

### StationaryShooterA (type 0x8e) and the shared shooter code {#goal-shooter}

The gunman goal of the Luther boss tactic (`BossLutherTactic_AssignGoal` `0x0030c510`); its Start, End, Resume and
Suspend are shared with LutherShooter.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f13a8` | `ShooterGoal_Init` | init | `ShooterGoal_Init`: vtable `0x005426e8`; spread angles, his post, the fire delays `+0x3c` / `+0x3e` | confirmed (code) |
| `0x002f14b0` | `ShooterGoal_Start` | Start | all-round sight, steering off, flags `0x2200`, heavy push factor, fight stance | confirmed (code) |
| `0x002f1568` | `ShooterGoal_End` | End | undoes Start | confirmed (code) |
| `0x002f1648` | `ShooterGoal_FaceTarget` | helper | unless held or busy, turns the body toward the target | confirmed (code) |
| `0x002f1718` | `ShooterGoal_ApplySpread` | helper | `ShooterGoal_ApplySpread`: turns the aim by random angles within the spread | confirmed (code) |
| `0x002f19d8` | `ShooterGoal_Fire` | helper | `ShooterGoal_Fire`: no shot at a downed or falling target; spread, ray to 30 m (`Goal_LineTest`), pending damage to a human hit (`Shot_CanHitHuman`), impact particles, a warning to the target when he is its nearest threat, the gunshot sound, the fire clip (668), a wait of the delay plus up to 1 s | confirmed (code) |
| `0x002f1f90` | `ShooterGoal_IsNearestThreat` | helper | 1 when he is the target's nearest threat | confirmed (code) |
| `0x002f1fd0` | `ShooterGoal_WarnTarget` | helper | sends message 1 (a shot warning, kind 2) to the target | confirmed (code) |
| `0x002f2088` | `ShooterGoal_ImpactEffects` | helper | at a world hit, the dust puff and spark particles | confirmed (code) |
| `0x002f2230` | `ShooterGoal_Say` | helper | a speech line from one of six tables by kind, unless one is playing | confirmed (code) |
| `0x002f2398` | `ShooterGoal_OnGunEvent` | event | from the Luther tactic: when a held object of a given type is used, starts the tactic's slow motion | confirmed (code) |
| `0x002f2438` | `ShooterGoal_OnHit` | event | from the Luther tactic: kind 1 looks round and says line `0xc`; kinds 2, 3 and 5 turn and slide him over clip 268's play time, with a line | confirmed (code) |
| `0x002f2600` | `ShooterGoal_PickVisiblePlayer` | helper | picks the nearest player in sight and line of sight who is not yet an enemy; no callers found (unused) | inferred |
| `0x002f27a0` | `ShooterGoal_HitSoundHash` | helper | hash of a random gunshot-hit sound name; no callers found (unused) | inferred |
| `0x002f2818` | `ShooterGoal_CheckLowHealth` | helper | once, at 17.5 % health or less, sets flag `0x10` and the low-health byte | confirmed (code) |
| `0x002f28b8` | `StationaryShooterAGoal_Process` | Process | heals to 50 % once when at 99 %; faces the target and looks at it every 30 updates; taunts (line `0x57`, anim 643) without a target; fires (`ShooterGoal_Fire`), turns with the turn clips (405, 406) or the dive clip (665), the low-health clip (670) | confirmed (code) |

### LutherShooter (type 0x9d) {#goal-luther-shooter}

`GoalLutherShooter`: the shooter goal with up to three scripted clips and a Lua callback.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f3118` | `Goal_LutherShooter` | binding | `Goal_LutherShooter` (Lua): pushes the goal | confirmed (code) |
| `0x002f3220` | `LutherShooterGoal_Init` | init | `LutherShooterGoal_Init`: `ShooterGoal_Init`, vtable `0x00542670`, the Lua callback, up to three clips into the dynamic slots (anims 598 on) | confirmed (code) |
| `0x002f3390` | `LutherShooterGoal_Destroy` | Destroy | releases the dynamic clip slots | confirmed (code) |
| `0x002f33e8` | `LutherShooterGoal_PickVisiblePlayer` | helper | copy of `0x002f2600`; no callers found (unused) | inferred |
| `0x002f3570` | `LutherShooterGoal_HitSoundHash` | helper | copy of `0x002f27a0`; no callers found (unused) | inferred |
| `0x002f3600` | `LutherShooterGoal_Process` | Process | `LutherShooterGoal_Process`: faces and looks at the target; by state plays the scripted clips (598 on) or the special clip (670), then fires with `ShooterGoal_Fire` | confirmed (code) |

### StationaryShooterB (type 0x8f) {#goal-stationary-shooter-b}

A gunman moving between a set of flags, firing in a cone; pushed by the Birdie boss tactic
(`BossBirdieTactic_AssignGoal` `0x0030ceb8`, [code](ai-code.md#t1-boss-birdie)), which sets and reads his state.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f3a20` | `StationaryShooterBGoal_Init` | init | vtable `0x00542610`; up to 8 flags, the cone angles (degrees, halved to radians), the fire delays, an option | confirmed (code) |
| `0x002f3b38` | `StationaryShooterBGoal_Start` | Start | all-round sight, steering off, flags `0x2204`, clears block and stun, fight stance; state 1 unless the option | confirmed (code) |
| `0x002f3c70` | `StationaryShooterBGoal_Resume` | Resume | picks a flag beyond a distance, unblocks, waits 2 s | confirmed (code) |
| `0x002f3db8` | `StationaryShooterBGoal_End` | End | undoes Start | confirmed (code) |
| `0x002f3e60` | `StationaryShooterBGoal_SetBlock` | helper | clears (1) or sets (0) the block flag `0x800` | confirmed (code) |
| `0x002f3e98` | `StationaryShooterBGoal_FaceTarget` | helper | turns toward the target from his current flag | confirmed (code) |
| `0x002f3f98` | `StationaryShooterBGoal_ApplySpread` | helper | random aim within the cone, clamped by its limits | confirmed (code) |
| `0x002f42c0` | `StationaryShooterBGoal_Fire` | helper | as `ShooterGoal_Fire`: spread, ray to 30 m, pending damage, effects, warning, sound, fire clip, wait | confirmed (code) |
| `0x002f4980` | `StationaryShooterBGoal_IsNearestThreat` | helper | 1 when he is the target's nearest threat | confirmed (code) |
| `0x002f49c0` | `StationaryShooterBGoal_WarnTarget` | helper | sends message 1 (a shot warning) to the target | confirmed (code) |
| `0x002f4a78` | `StationaryShooterBGoal_ImpactEffects` | helper | impact particles beyond a distance | confirmed (code) |
| `0x002f4c68` | `StationaryShooterBGoal_Say` | helper | kind 2 line `0x11`, kind 3 line `0x8f` | confirmed (code) |
| `0x002f4cf0` | `StationaryShooterBGoal_AdjustEnemyScore` | score | -999 unless the enemy is gangless (a player) with a clear line from the gun | confirmed (code) |
| `0x002f4e10` | `StationaryShooterBGoal_FindDownedPlayer` | helper | with a gun, looks for a downed player within sight | inferred |
| `0x002f52b0` | `StationaryShooterBGoal_SetState` | helper | sets the state `+0x24` and its time (from the tactic) | confirmed (code) |
| `0x002f52c8` | `StationaryShooterBGoal_IsDone` | helper | 1 in state 4 (read by the tactic) | confirmed (code) |
| `0x002f52e8` | `StationaryShooterBGoal_Process` | Process | stunned: waits; faces and looks at the target; state 0 taunt (line `0x57`); 1 fire; 2 move flag; 3 the special clip (670) and line `0x10`; 4 done | confirmed (code) |
| `0x002f5988` | `StationaryShooterBGoal_OnHurt` | event | from the gang brain: when his health falls to the given value, line `0x28` and, with a gun, a fall: turns and plays clip 668 | inferred |

### Mace (type 0x91) {#goal-mace}

A fighter with a can of mace (clip `fem_mace`, object `dyn_mace`), pushed by the Moe boss tactic: she fights, then keeps
away for a while.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f5b88` | `MaceGoal_Init` | init | vtable `0x005425b0`; saves and zeroes brain `+0x21c`, saves human `+0x640` and sets it to `0xefefefff` | confirmed (code) |
| `0x002f5c00` | `MaceGoal_Start` | Start | brain `+0x208` = table `0x00511220`, all-round sight, the mace clip on anim slot `0x15`, the mace in hand, flag `0x2000` | confirmed (code) |
| `0x002f5ca0` | `MaceGoal_End` | End | undoes Start | confirmed (code) |
| `0x002f5d50` | `MaceGoal_Process` | Process | fight phase: no weapon and beyond 1.1 x near or out of sight, EngageEnemy; sight blocked by a few obstacle kinds, runs to him (gait 4); else FightGoal (1 s); avoid phase: unless the enemy has his back to her, AvoidEnemies (near to 2 x near) for 4-8 s | confirmed (code) |

### Grabber (type 0x92) {#goal-grabber}

A Roof boss minion (`BossRoofTactic_AssignGoal` `0x0030b948`) who grabs an enemy from behind and turns him toward the
nearest gang mate.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f60e8` | `GrabberGoal_Init` | init | vtable `0x00542550` | confirmed (code) |
| `0x002f6120` | `GrabberGoal_Start` | Start | brain `+0x208` = table `0x00511250`, `+0x21c` = 0, all-round sight | confirmed (code) |
| `0x002f6180` | `GrabberGoal_Resume` | Resume | picks the best enemy | confirmed (code) |
| `0x002f61e0` | `GrabberGoal_AdjustEnemyScore` | score | -20 for an enemy who is down | confirmed (code) |
| `0x002f6220` | `GrabberGoal_NearestGangMate` | helper | the gang mate (of 16) nearest a point | confirmed (code) |
| `0x002f6318` | `GrabberGoal_AllowsReaction` | helper | from `Brain_UpdateReactionGoal`: 0 while he grabs or tackles his target (no reaction goal), else 1 | confirmed (code) |
| `0x002f63b8` | `GrabberGoal_Process` | Process | first: turns to the camera, a sound and anim 643; with no enemy follows a gang mate; far or out of sight: EngageEnemy; blocked: runs; holding from behind: turns the victim toward the nearest mate; else a grab command (or a tackle follow-up) | confirmed (code) |

### BigDefender (type 0x88) {#goal-big-defender}

A Roof boss minion who guards the gang leader.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f6878` | `BigDefenderGoal_Init` | init | vtable `0x005424f0` | confirmed (code) |
| `0x002f68b0` | `BigDefenderGoal_Start` | Start | all-round sight, flags `0x80020380` | confirmed (code) |
| `0x002f6938` | `BigDefenderGoal_Resume` | Resume | picks the best enemy | confirmed (code) |
| `0x002f69d8` | `BigDefenderGoal_AdjustEnemyScore` | score | a bonus for an enemy holding an attack slot on the leader, twice that for the leader's target | confirmed (code) |
| `0x002f6a80` | `BigDefenderGoal_Process` | Process | first: turns to the camera and anim 643; then as the Grabber's: EngageEnemy, FightGoal, MoveToHuman or a shuffle | confirmed (code) |

### BigBull (type 0x89) {#goal-big-bull}

A heavy Roof boss minion who rushes his target; after six standing reactions in a row he blocks.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f6e28` | `BigBullGoal_Init` | init | vtable `0x00542490`; all-round sight, flags `0x80020280` | confirmed (code) |
| `0x002f6ef0` | `BigBullGoal_Resume` | Resume | clears flag `0x10`, heavy push factor | confirmed (code) |
| `0x002f6f48` | `BigBullGoal_Suspend` | Suspend | normal push factor | confirmed (code) |
| `0x002f6f88` | `BigBullGoal_End` | End | does nothing | confirmed (code) |
| `0x002f7028` | `BigBullGoal_AdjustEnemyScore` | score | -999 beyond sight range or out of line of sight; else + (sight - distance) x the range factor, +20 for a player | confirmed (code) |
| `0x002f7120` | `BigBullGoal_TurnToCharge` | helper | turns toward the target when more than 60 degrees off | inferred |
| `0x002f7328` | `BigBullGoal_OnEvent` | event | from the gang brain: outside a charge, flags `+0x1f` (hit) | inferred |
| `0x002f7348` | `BigBullGoal_Process` | Process | counts standing reactions (6 sets the block flag); first: turns, sound and anim 643; a stun spell with line 8; FightGoal (4 s), MoveToHuman (0.5-0.9 x near, 2 s or 6 s) or a taunt | confirmed (code) |

### HideAndSeek (type 0x9c) {#goal-hide-and-seek}

The Virgil boss (`BossVirgilTactic_AssignGoal` `0x0030d960`): he hides in the shadow at one of the flags of kind `0x21`
within 40 m and fights whoever finds him, then hides again.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f7bd0` | `HideAndSeekGoal_Init` | init | vtable `0x00542430`; clears the hiding flag list | confirmed (code) |
| `0x002f7c48` | `HideAndSeekGoal_Start` | Start | flags `0x80820380`, collects the flags, goes to a hiding flag, raises the turn boost, all-round sight; sets the global `0x0050b1dc` | confirmed (code) |
| `0x002f7d18` | `HideAndSeekGoal_End` | End | undoes Start; clears `0x0050b1dc` | confirmed (code) |
| `0x002f7dd0` | `HideAndSeekGoal_CollectFlags` | helper | the flags of kind `0x21` within 40 m of him | confirmed (code) |
| `0x002f7ee0` | `HideAndSeekGoal_MoveToHidingFlag` | helper | drops his enemies, picks another hiding flag at random and puts him there facing its heading | confirmed (code) |
| `0x002f8030` | `HideAndSeekGoal_SetHidden` | helper | enters (1) or leaves (0) the shadow-hidden state | confirmed (code) |
| `0x002f80b0` | `HideAndSeekGoal_Found` | helper | found by a human: unblocks, makes him an enemy and the target, fights for 2 s | confirmed (code) |
| `0x002f8138` | `HideAndSeekGoal_DropEnemies` | helper | makes every enemy drop him as target | confirmed (code) |
| `0x002f8240` | `HideAndSeekGoal_OnAttackWarning` | event | from the Virgil tactic, when found: counters an escapable grab or tackle, retargets | confirmed (code) |
| `0x002f8400` | `HideAndSeekGoal_OnTouched` | event | from the Virgil tactic: a non-friendly human touching him finds him | confirmed (code) |
| `0x002f8468` | `HideAndSeekGoal_Process` | Process | hidden: waits in the shadow; found: fights (MoveToHuman, attacks, lines), after a while hides again | confirmed (code) |

### LeftTurf (type 0x3f) {#goal-left-turf}

A gang member who has left his turf walks back into it, fighting whoever blocks him; pushed by the base event handler
(`Goal_DefaultOnEvent` `0x0029ef80`) and the guard goals' handlers.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f8c90` | `LeftTurfGoal_Init` | init | vtable `0x00542a60`; the point where he left the turf (brain `+0x230`) | confirmed (code) |
| `0x002f8d88` | `LeftTurfGoal_NearestTurfBox` | helper | the gang's turf box nearest a point | inferred |
| `0x002f8e70` | `LeftTurfGoal_Process` | Process | ends when he is in the turf; aims 2 m inside past the exit point; when reachable runs there (gait 4), in the fight stance with a valid target (FightGoal 2 s when blocked, line `0x47`); else every 1 s a random point in a turf box; gives up after 30 updates | confirmed (code) |

### RunFromTrain (type 7) {#goal-run-from-train}

Pushed by the train (`TrainRecord_Update` `0x00413d90`) on a human near its track: he drops what he is doing and runs
off the track.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f9358` | `RunFromTrainGoal_Init` | init | vtable `0x00542ac0`; the train record | confirmed (code) |
| `0x002f93b0` | `RunFromTrainGoal_IsSafePoint` | helper | 1 when a point clear of the track has ground below (4 m) and a straight walk to it | confirmed (code) |
| `0x002f95e0` | `RunFromTrainGoal_SprintTo` | helper | a move action to the point at gait 5 (sprint); from standing, sets the heading and gait 4 at once | confirmed (code) |
| `0x002f96d0` | `RunFromTrainGoal_TryFlee` | helper | tries each side of the track for a safe point and sprints there | confirmed (code) |
| `0x002f9980` | `RunFromTrainGoal_FleeAlongTrack` | helper | with no side clear, sprints to the projected point along the track | confirmed (code) |
| `0x002f9a80` | `RunFromTrainGoal_DropEverything` | helper | drops a held object, breaks a pair, ends a minigame, cancels a throw aim and a stun, unpins a target object | confirmed (code) |
| `0x002f9b90` | `RunFromTrainGoal_Start` | Start | turn boost 3, flags `0x100` and `0x800` (saved), drops everything | confirmed (code) |
| `0x002f9c18` | `RunFromTrainGoal_End` | End | restores the turn boost and flags | confirmed (code) |
| `0x002f9c88` | `RunFromTrainGoal_Process` | Process | ends when the train is no longer near; clears actions every 250 ms; projects his point on the track and flees | confirmed (code) |

### Action base {#goal-action-base}

The first function of the action code after the goals.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f9de0` | `Action_AbortAndDestroy` | helper | a started action is aborted first (vtable `+0x1c`, 0 when it refuses); then destroyed (`+0x24`); 1 when gone (from `Brain_PopAction`) | confirmed (code) |
