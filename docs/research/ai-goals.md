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

The Lizzies boss, Vargas ([The Diego and Vargas fight](ai.md#boss-diego-vargas)): a state machine of attack tactics and
speech lines.

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
| `0x002a3d90` | `ManWeaponPileGoal_End` | End | when it took over the object at `[0x00512b04]` (`+0x20`), hands it back (its slots `+0x4c`, `+0x8c`) | confirmed (code) |

### ObjectThrower (type 0x4e) {#goal-object-thrower}

Throws objects found near him at a target.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002a3e50` | `Goal_ObjectThrower` | binding | `GoalObjectThrower` | confirmed (code) |
| `0x002a3f50` | `ObjectThrowerGoal_Init` | init | vtable `0x0053f310` | confirmed (code) |
| `0x002a4138` | `ObjectThrowerGoal_End` | End | leaves the fight stance, clears human `+0x128` and object flag `0x40000`, hands back the object at `[0x00512b04]` | confirmed (code) |
| `0x002a4230` | `ObjectThrowerGoal_Process` | Process | finds an object, picks it up and throws it | inferred |
| `0x002a4aa0` | `LineTestCache_Reset` | reset | on a full reset clears the handle at `0x006e93f8` that `Brain_LineTest` caches | inferred |
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
| `0x002a5fd8` | `PathBlockerGoal_End` | End | hands back the object at `[0x00512b04]` when it took it over | confirmed (code) |

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
| `0x002b0c88` | `Chase_DodgeCar` | event | from `0x003181e8`: a car coming at the chaser pushes a dodge goal (`0x002cf8f8`, 5 m) | inferred |
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
| `0x002b2e28` | `FightGoal_TryTackle` | helper | with a tackle weight (kind 21), a type-1 brain 75 % of the time: queues the tackle when the target can be attacked | confirmed (code) |
| `0x002b2fc8` | `FightGoal_Reposition` | helper | step 9 of the fight goal: a fidget (`0x25b`) or a shuffle around the target | confirmed (code) |
| `0x002b3360` | `FightGoal_TryGrab` | helper | a grab (kind 22) or the right move against a grabbed, tackled or held target; a move in when friendly | confirmed (code) |

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
| `0x002b4aa8` | `GroundedGoal_Process` | Process | Grounded: may queue attack kind 42 against the target from the ground | inferred |
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
| `0x002b5b98` | `Grabbing_PickMove` | helper | the move on the held human: by direction (8 sectors) to a flag, a friend or a wall | inferred |
| `0x002b60b0` | `Grabbing_NoDelay` | helper | −1.0 (no delay) | confirmed (code) |
| `0x002b60c0` | `GrabbingGoal_Process` | Process | near a train sets command 5; else grab attacks (kind 24 and others) | inferred |
| `0x002b65f8` | `MountingGoal_Init` | init | Mounting (`0x13`, the tackler on top), vtable `0x005400f0` | confirmed (code) |
| `0x002b6638` | `MountingGoal_Process` | Process | punches on the ground (kind 35 40 % of the time) | inferred |
| `0x002b6818` | `GrabbedGoal_Init` | init | Grabbed (`0x14`), vtable `0x00540090` | confirmed (code) |
| `0x002b6848` | `GrabbedGoal_Process` | Process | calls the gang for help, struggles with attacks | inferred |
| `0x002b6b88` | `MountedGoal_Init` | init | Mounted (`0x15`, tackled), vtable `0x00540030` | confirmed (code) |
| `0x002b6bb8` | `MountedGoal_Process` | Process | calls for help, struggles | inferred |
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
| `0x002bbfa0` | `FollowAndAttackGoal_TryPickUp` | helper | for a brain allowed to chase (`+0x265`), not blocked and (when asked) empty-handed: every 20 updates, or within 15 m of the enemy, a smash or throw object (Riot_FindSmashTarget, 20 m or 3 m) it may pick up gets a GetItem goal (kind 4); returns 1 then | confirmed (code) |
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
| `0x002c7020` | `DealerGoal_Suspend` | Suspend | withdraws the prompt (`+0x1b2` = 0, vtable 300), pushable, and resets the visit (DealerGoal_Reset) | confirmed (code) |
| `0x002c7158` | `DealerGoal_QueueGesture` | helper | queues a dealer gesture (ai.md#dealer-gestures) | confirmed (code) |
| `0x002c7248` | `DealerGoal_Say` | helper | says the dealer's line for a kind (ai.md#dealer-gestures) | confirmed (code) |
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
