# AI code index: brains, actions and tactics

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-07); each row states its evidence level.

## Purpose

The function-by-function index of the AI code from `0x00288000` up: the brain core, the actions, the brain types and
the tactics. [AI humans](ai.md) explains how these pieces work together and is the page to read first; this one
names every function, says in a line what it does, and links to the section of [AI humans](ai.md) that covers it.

## The brain core {#brain-core}

The brain helpers in `0x00288000`-`0x0029f000`: the record, perception, enemies, formations, routes and the
tables they share. Each table row is one function; names already in use are kept.

### Brain record helpers {#brain-record}

Small helpers that read or reset fields of the 0x2f0-byte brain record at `0x006d53f0`; the record's layout is
in [AI humans: the brain](ai.md#brain).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00288698` | `Player_ThrowMeleeWeapon` | helper | picks the throw clip by gait (run/jog 0x1d8, walk 0x1d7, standing 0x1eb or 0x1f6 by the held set); a player picks a target object within 20 m (`0x0027ad80`); turns to it by the clip's first event and starts the throw (`0x00261e78`) | confirmed (code) |
| `0x0028aa18` | `Brain_LoadAttackWeights` | helper | copies the 45 attack weights from the class (`+0x11e`) to brain `+0x298`; class 5's when the brain type differs from the class's and the human has `+0x19d` | confirmed (code) |
| `0x0028ab08` | `Brain_UpdateGait` | helper | the gait for the brain's speed (`+0x114`) | confirmed (code) |
| `0x0028ab30` | `Brain_IsNearTrain` | helper | whether a train is near (brain `+0x2e0` is the game time) | confirmed (code) |
| `0x0028ab50` | `Brain_TopGoal86Allows` | helper | 1 unless the top goal is type `0x86`, which answers itself (`0x002ec4a0`) | confirmed (code) |
| `0x0028ac18` | `Brain_StopMove` | helper | gait 0, heading = the current one (think bookkeeping) | confirmed (code) |
| `0x0028ac68` | `Brain_ReplaceTopGoal` | helper | writes a goal into the top stack slot | confirmed (code) |
| `0x0028bb58` | `Brain_SaveHomePos` | helper | stores position and heading at `+0x250`-`+0x25c` unless in shadow mode | confirmed (code) |
| `0x0028bbf0` | `Brain_UpdateAlertness` | helper | every 5 updates sets the human's alert level (`0x002235a0`: 0, 2, 4 or 6) from his top goal and his gang's tactic type | confirmed (code) |
| `0x0028bec8` | `Brain_StoreHealth` | helper | the human's health percent into `+0x154` | confirmed (code) |
| `0x0028bfa8` | `Brain_CanSeekHat` | helper | whether the human may go for a dropped hat: allowed (`+0x266`), free, hat-less, top goal 11 or `0x2c` | inferred |
| `0x0028c380` | `Brain_Enable` | helper | loads the attack weights and enables the brain (`+0x08`) | confirmed (code) |
| `0x0028c3b0` | `Brain_OnKnockedOut` | helper | on a knock-out: ends the reaction goal, clears goals and actions, drops the target | confirmed (code) |
| `0x0028c488` | `Brain_EnableAndResume` | helper | enables the brain and resumes its top goal | inferred |
| `0x0028c4f8` | `Brain_TearDown` | helper | clears goals, actions and enemies, drops the target, leaves the gang and tells its spawner (`GangSpawner_OnHumanGone`) | confirmed (code) |
| `0x0028c5d8` | `Brain_OnArrested` | helper | on an arrest: clears actions, drops the target, pops goals down to goal `0x41` | confirmed (code) |
| `0x0028c780` | `Brain_GiveHandObject` | helper | puts the top goal's object in hand (BigThrower `0x87`, BigBrawler `0x84`, `0x8a`, `0x8c`), else the class's weapon name or `dyn_swhbld` | confirmed (code) |
| `0x0028cdf8` | `Brain_SetTurnBoost` | helper | sets the turn boost byte `+0x0b` | confirmed (code) |
| `0x0028ce60` | `Brain_SetStartBoost` | helper | sets the start boost byte `+0x0c` | confirmed (code) |
| `0x0028ceb8` | `Brain_StoreType` | helper | stores the brain type (`+0x04`) and installs its handlers | confirmed (code) |
| `0x0028cef8` | `Brain_IsBusyWithScene` | helper | with the flag `0x0050cab4`: true when the human has `+0x333` and his top goal is one of `0x80`, `0x82`, `0x4f`, `0x50`, an idle 4, or the reaction goal is `0x16` | inferred |
| `0x0028d9e0` | `Brain_GetGangTactic` | helper | the gang's tactic (gang `+0x40`) | confirmed (code) |
| `0x002911e8` | `Brain_StoreAttackWeight` | helper | `+0x298 + kind` | confirmed (code) |
| `0x00291d48` | `Brain_GetReactionKind` | helper | 0 for the player in co-op, 3 alone; type 2 of class kind 13 → 0; else the type | confirmed (code) |
| `0x00291db8` | `Brain_GetHelpLevel` | helper | 1 or 2 by type, class kind 6 and tactic `0x23` | inferred |
| `0x002920b8` | `Brain_SetTimerA` | helper | `+0x2cc` = now + ms (0 clears) | confirmed (code) |
| `0x002920e0` | `Brain_ExpireTimerA` | helper | when `+0x2cc` passes: cleared, `+0x2d0` = 0 | confirmed (code) |
| `0x00292130` | `Brain_SetTimerB` | helper | `+0x2c8` = now + ms (0 clears) | confirmed (code) |

### Goal stack and action queue {#goal-stack}

Queries and edits of the goal stack (`+0x40`, top index `+0x2c`) and the action queue (`+0x68`). How goals and
actions run is in [AI humans: goals](ai.md#goals) and [actions](ai.md#actions).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0028d8a0` | `Brain_MarkGoalBase` | helper | remembers the current top index in `+0x2d` (the base for a flush) | confirmed (code) |
| `0x0028d8c0` | `Brain_PopToGoalBase` | helper | pops goals down to `+0x2d`, then clears it | confirmed (code) |
| `0x0028db08` | `Brain_FrontAction` | helper | the front action of the queue | confirmed (code) |
| `0x0028f1e8` | `Brain_SuspendTop` | helper | suspends the top goal and clears the actions | confirmed (code) |
| `0x00290348` | `Brain_HasFightGoal` | helper | whether goal 8 or `0x3f` is on the stack | confirmed (code) |
| `0x002903b0` | `Brain_HasChaseGoal` | helper | goal `0x0c` or `0x3f` on the stack | confirmed (code) |
| `0x00290418` | `Brain_HasFleeGoal` | helper | goal `0x6b`, `0x81` or `0x3f` on the stack | inferred |
| `0x002904e0` | `Brain_MayStartFight` | helper | no goal `0x0c`, top goal not `0x8a`, gang not of kind 1, no fight goal | confirmed (code) |
| `0x00293a10` | `Action_PoolAlloc` | helper | the first free of 100 actions (0x50 bytes, `0x006e63d0`, bitmap `0x006cead8`) | confirmed (code) |
| `0x00293a70` | `Action_PoolFree` | helper | frees an action's pool bit | confirmed (code) |

### Steering {#brain-steering}

The steering state at brain `+0xa0`, which bends a walk round other humans; see
[AI humans: steering round humans](ai.md#steering).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00288a48` | `Steering_Construct` | constructor | resets the steering state with no brain | confirmed (code) |
| `0x00288a78` | `Steering_Reset` | helper | clears the two points to the origin, stores the brain (`+0x20`), enabled (`+0x2a`) = 1, side `+0x28` = −1, the rest 0 | confirmed (code) |
| `0x00288ad0` | `Steering_Clear` | helper | avoidance off, side −1, score `+0x3c` = −1e9 | confirmed (code) |
| `0x00288b18` | `Steering_SetEnabled` | helper | stores `+0x2a`; off also resets the state | confirmed (code) |
| `0x00288b40` | `Steering_HoldFor` | helper | `+0x38` = now + ms | confirmed (code) |
| `0x00288b58` | `Steering_IsHolding` | helper | now before `+0x38` | confirmed (code) |
| `0x00288b70` | `Steering_SetAvoiding` | helper | sets the avoiding flag (`+0x24`); turning it on raises the brain's turn boost by one, off lowers it | confirmed (code) |
| `0x00288be0` | `Steering_SideSign` | helper | +1 or −1 by the sign of a cross product: which side of a line a point lies | confirmed (code) |
| `0x00288c60` | `Steering_SetGait` | helper | the gait and its speed (`Human_SpeedForGait`) | confirmed (code) |
| `0x00288cb0` | `Steering_SetSpeed` | helper | stores the gait (`+0x32`) and speed (`+0x34`) | confirmed (code) |
| `0x00288cc8` | `Steering_FindBlocker` | helper | of a list of nearby humans, not the target and not behind, the first whose 0.63 m disc the relative step hits ([the blocker test](ai.md#neighbour-sectors)) | confirmed (code) |
| `0x00288f40` | `Steering_ClampStep` | helper | the step from a position toward the brain's aim point (`+0x90`), of length min(the distance, factor × the brain's speed `+0x114`) ([Steering](ai.md#steering)) | confirmed (code) |
| `0x00289010` | `Steering_TryDetour` | helper | when the human can walk straight to the detour point and it is not blocked (`0x00249050`): avoiding on, the point and its score stored | confirmed (code) |
| `0x002890a8` | `Steering_ResetAvoidance` | helper | from a move's Start: the held point to the origin, avoidance off, score −1e9, `+0x2b` = 0, and the human's index (human `+0x92`, low byte) into the decision counter `+0x28` and the slow counter `+0x29`, which staggers the humans' decisions ([Steering](ai.md#steering)) | confirmed (code) |
| `0x00289108` | `Steering_EndAvoid` | helper | avoidance off, score 0 | confirmed (code) |
| `0x00289d68` | `Steering_RayHitsHuman` | helper | whether a ray of a given length passes within 0.63 m of a human, and at what distance | confirmed (code) |
| `0x00289ed0` | `Brain_GiveWayTo` | helper | a standing AI in the way of a mover picks the first free sector of s, s+1, s+2, s−1, s+3, s+4, s−2 (s: straight off the mover's path) and queues a `GiveWay` step there, else asks that sector's human to give way ([Giving way](ai.md#neighbour-sectors)) | confirmed (code) |
| `0x0028a248` | `Brain_PushAside` | helper | guards `Brain_GiveWayTo` against re-entry with brain flag `+0xcc` bit 4; a brain with bit 1 (already giving way) answers yes | confirmed (code) |

### Perception and head looks {#perception}

The enemy scan, sight tests, and the perception record at brain `+0xf8` that turns the head: idle moods pick
something to glance at, look modes play short head-turn sequences through the head-look record at brain
`+0x284`. Sight itself is in [AI humans: sight](ai.md#sight); the head-look moods are new here.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0028ac80` | `Brain_SpotPlayers` | helper | for each player in sight (shadow rules: within 2 m only for the same `+0x2d5`), sets brain `+0x14e + i` and the sticky `+0x150 + i`; the gang's spot line (command `0x16`) once when armed; tells the human (vtable `+0x44`) | confirmed (code) |
| `0x0028af88` | `PlayerBrain_ScanNearEnemies` | helper | the player's list of hostiles within the radius (7.5 m from `PlayerBrain_Update`, every 2 s), counted on their brains (`+0x152`), and marked on the radar | confirmed (code) |
| `0x0028b358` | `Brain_ScanEnemies` | helper | the periodic enemy scan: lists nearby humans and keeps the enemy list and sight flags up to date | confirmed (code) |
| `0x0028ba48` | `Brain_UpdateTurfLeave` | helper | when the human leaves his gang's turf (`0x0028ff18` false): asks the goals' event slot to handle it once, popping those that refuse; on return records his position at `+0x230` | inferred |
| `0x0028bf00` | `Brain_GetSightRange` | helper | the sight range `+0x130`; at least 30 m for a gang of kind 1 | confirmed (code) |
| `0x0028bf40` | `Brain_SeesPlayer` | helper | brain `+0x14e + i` (any player for i < 0) | confirmed (code) |
| `0x0028bf88` | `Brain_HasSeenPlayer` | helper | brain `+0x150 + i` | confirmed (code) |
| `0x0028c0c8` | `Brain_NoteSpotter` | helper | stores who spotted (handle `+0x260`, his gang id `+0x264`), or clears it | confirmed (code) |
| `0x0028c140` | `Brain_NoteLastSeenPos` | helper | a human's position into `+0x240` (unless he is hidden in shadow, or forced) | confirmed (code) |
| `0x0028fa18` | `Brain_MaybeScanEnemies` | helper | re-scans enemies (`Brain_ScanEnemies`, one of 5 tokens per update) when the gang changed or the interval `+0x144` passed, × 4 in the fight stance or at detail level `+0x333` above 0 ([The enemy scan](ai.md#enemy-scan)) | confirmed (code) |
| `0x0028fae8` | `Brain_MaybeSpotPlayers` | helper | drops the target while busy (`0x1f80874000`); every 1.5 s, × 4 in the fight stance or at detail level `+0x333` above 0, `Brain_SpotPlayers` | confirmed (code) |
| `0x00293cd0` | `AI_TakeScanToken` | helper | at most 5 enemy scans per update (`0x00510ac0`; refusals counted at `0x00510ac4`) | confirmed (code) |
| `0x00296908` | `Perception_Init` | helper | the perception record (brain `+0xf8`): look history cleared, 30-tick timer; resets the player's look flag | confirmed (code) |
| `0x002969c8` | `Perception_GetBrain` | helper | the brain owning a perception record (−0xf8) | confirmed (code) |
| `0x002969d0` | `Perception_PickLookFlag` | helper | a player's idle glance: every 1-2 s picks the nearest look-at flag (type 12) within 10 m in view and turns his head to it | confirmed (code) |
| `0x00296e10` | `Perception_PushHistory` | helper | shifts the three-entry look history; new look lasts 2-5 s | confirmed (code) |
| `0x00296e90` | `Perception_GetLookTime` | helper | how long a human has been looked at, from the history | confirmed (code) |
| `0x00296f08` | `Perception_LookAtHuman` | helper | records a human in the history and turns the head to him | confirmed (code) |
| `0x00296fc8` | `Perception_EndLook` | helper | ends the head look, history entry cleared | confirmed (code) |
| `0x00297050` | `Perception_ScoreLookTarget` | helper | scores a human as something to look at: nearer, longer looked at, a moving player score higher; out of view or hidden in shadow −0x8000 | inferred |
| `0x002971c8` | `Perception_PickLookTarget` | helper | the best-scoring nearby human in line of sight | confirmed (code) |
| `0x00297308` | `Perception_NearestAwakeHuman` | helper | the nearest nearby human not in shadow whose state is above 2 | inferred |
| `0x00297420` | `Perception_IsLooking` | helper | whether a look mode is set (`+0xe` not 7) | confirmed (code) |
| `0x00297430` | `Perception_StartLook` | helper | sets look mode 0-6 with its subject and turns head looking on | confirmed (code) |
| `0x002975e0` | `Perception_StopLook` | helper | look mode 7 (none), head looking off | confirmed (code) |
| `0x00297620` | `Perception_ChooseMood` | helper | in a fight stance or with hostiles: alert moods 3 or 4; else idle moods 0-2, chosen by time and chance | inferred |
| `0x00297748` | `Perception_SetMood` | helper | changes the mood, clears the looked-at slots and resets the timer | confirmed (code) |
| `0x002977c8` | `Perception_TickTimers` | helper | counts the look timers down by the frame step | confirmed (code) |
| `0x002978f8` | `Perception_IdleMood0` | helper | calm idle: glances at flags, now and then a random look every 1-3 s | inferred |
| `0x00297a58` | `Perception_IdleMood1` | helper | idle that sometimes looks back at whoever looks at it | inferred |
| `0x00297c90` | `Perception_IdleMood2` | helper | looks at the human best in front (within 50 degrees), held 0.75-1 s, else rests 1.5-2 s | inferred |
| `0x00298058` | `Perception_LookAtTarget` | helper | keeps the head on the brain's target, or ends the look | confirmed (code) |
| `0x002980c0` | `Perception_AlertMood` | helper | no target: scans nearby humans in short looks; with a target: looks at it | inferred |
| `0x00298368` | `Perception_LookAroundStep` | helper | look mode: a look around, ends after 20 steps | confirmed (code) |
| `0x00298420` | `Perception_DoubleTakeA` | helper | look mode: looks at the subject, away, and back over 9 steps | inferred |
| `0x002985e0` | `Perception_DoubleTakeB` | helper | the same sequence as DoubleTakeA for another mode | inferred |
| `0x002987a0` | `Perception_SideGlanceA` | helper | look mode: glances left and right of the subject; skipped first step under goal `0x71` | inferred |
| `0x00298a88` | `Perception_SideGlanceB` | helper | a narrower version of SideGlanceA | inferred |
| `0x00298d68` | `Perception_StartleLook` | helper | look modes 5 and 6: the head snaps 45 degrees to a side (random side in mode 5), then relaxes; ends after 4 steps | confirmed (code) |
| `0x00298ea0` | `Perception_RunLookMode` | helper | runs the current look mode's step; stops when the head look expired and is not held | confirmed (code) |
| `0x00298f88` | `Brain_UpdatePerception` | helper | the per-tick perception update: runs a look mode, or every few ticks picks a mood and runs it | confirmed (code) |
| `0x00299100` | `Perception_OnEvent` | helper | event `0x17` from a human of another gang: a short reaction look (100-750 ms) | inferred |
| `0x00299220` | `Perception_GetMoodTable` | helper | a mood's three-byte entry at `0x00510b10` | confirmed (code) |
| `0x00299df0` | `HeadLook_Construct` | helper | constructs the head-look record (brain `+0x284`) | confirmed (code) |
| `0x00299e18` | `HeadLook_Init` | helper | head look idle, weight 1.0, no expiry | confirmed (code) |
| `0x00299e50` | `HeadLook_IsRunning` | helper | whether the head look has not yet expired | confirmed (code) |
| `0x00299e68` | `HeadLook_SetWeight` | helper | stores the blend weight in 1/64 steps | confirmed (code) |
| `0x00299e88` | `HeadLook_GetWeight` | helper | the blend weight | confirmed (code) |
| `0x00299ea8` | `HeadLook_Relax` | helper | back to idle (straight ahead) for a time, with a weight | confirmed (code) |
| `0x00299ee0` | `HeadLook_IsRelaxed` | helper | whether the head look is idle (bit 1) | confirmed (code) |
| `0x00299ef0` | `HeadLook_SetHeld` | helper | sets or clears the held bit (`0x10`) | confirmed (code) |
| `0x00299f18` | `HeadLook_IsHeld` | helper | the held bit | confirmed (code) |
| `0x00299f28` | `HeadLook_AtPoint` | helper | look at a world point for a time (bit 2) | confirmed (code) |
| `0x00299f78` | `HeadLook_GetPoint` | helper | the looked-at point, if any | confirmed (code) |
| `0x00299f98` | `HeadLook_AtAngles` | helper | turn the head to a yaw and pitch for a time (bit 4) | confirmed (code) |
| `0x00299fe0` | `HeadLook_GetAngles` | helper | the head angles, if set | confirmed (code) |
| `0x0029a078` | `HeadLook_GetHuman` | helper | the looked-at human, if set (bit 8) | confirmed (code) |
| `0x0029be58` | `Brain_CanSeeLiving` | helper | whether a human is up, not knocked out, and in line of sight | confirmed (code) |

### Enemy list and scoring {#enemy-list}

The list of up to 16 enemies at brain `+0x164` and the score that picks among them; see
[AI humans: choosing among enemies](ai.md#enemy-score) and [starting a fight](ai.md#targets).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0028abc0` | `Brain_CanBeChased` | helper | whether a target can be chased: reachable (`+0x11e`), no train near, not on fire | confirmed (code) |
| `0x0028c360` | `Brain_ClearEnemiesThunk` | helper | thunk of `Brain_ClearEnemies` | confirmed (code) |
| `0x0028d358` | `Brain_ValidateEnemy` | helper | returns a human when he may be chosen as an enemy | confirmed (code) |
| `0x0028d6a0` | `Brain_UncountEnemy` | helper | removes one from the enemy's hostile count (`+0x152`) unless both are players | confirmed (code) |
| `0x0028ef20` | `Brain_CountHostile` | helper | `+0x152` + 1 | confirmed (code) |
| `0x0028ef30` | `Brain_UncountHostile` | helper | `+0x152` − 1, not below 0 | confirmed (code) |
| `0x0028ef48` | `Brain_UncountAllEnemies` | helper | each enemy's hostile count − 1 (players' left alone in co-op) | confirmed (code) |
| `0x0028f000` | `Brain_ShakeOffPursuers` | helper | for a player or a Warrior: hunters of a hostile gang within 2 × far who hold him as an enemy but have no line of sight to him lower his count | confirmed (code) |
| `0x0028f9e8` | `Brain_ClearEnemies` | helper | uncounts and empties the enemy list, brain disabled (`+0x08` = 0) | confirmed (code) |
| `0x00290138` | `Brain_IsThreat` | helper | whether another brain is a threat: never himself; a Warrior hitting back at his chief (`+0x2e5`) and that chief are threats to each other; else enemy gangs, or both players ([The enemy scan](ai.md#enemy-scan)) | confirmed (code) |
| `0x00290328` | `Brain_MakeGangsEnemies` | helper | sets the enemy bit between the two brains' gangs | confirmed (code) |
| `0x00291218` | `Brain_NearestEnemyVisible` | helper | sorts the enemy list by distance; whether the nearest is visible (not in shadow, in view) | confirmed (code) |
| `0x0029bec8` | `Brain_CanTarget` | helper | CanSeeLiving, unless he is an unarmed non-cop the brain may not engage | inferred |
| `0x0029bf60` | `Brain_IsValidEnemyOf` | helper | whether another human's brain accepts this one as an enemy | confirmed (code) |
| `0x0029ce98` | `Brain_ScoreEnemy` | helper | scores an enemy (−999 when ruled out, else weighted terms) | confirmed (code) |

### Attack slots {#attack-slots}

The attack slots at brain `+0x1a4` and the active attackers at `+0x1f0`, which limit how many humans hit one
target at a time; see [AI humans: EngageEnemy](ai.md#engage-enemy).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0028cb98` | `Brain_CalmGangKind4Attackers` | helper | attackers from a gang of kind 4 in a fight goal drop their chosen attack (45 none) and actions, attack time reset | confirmed (code) |
| `0x0028db88` | `Brain_SetAttackSlotCountRaw` | helper | sets the maximum attack slots (`+0x1e4`); when lowered, keeps the nearest holders | confirmed (code) |
| `0x0028dc80` | `Brain_CanTakeSlotOn` | helper | whether the brain may take an attack slot on an enemy | confirmed (code) |
| `0x0028de08` | `Brain_HoldsAttackSlot` | helper | whether a handle is in the attack-slot list | confirmed (code) |
| `0x0028de98` | `Brain_HasAttackerOfType` | helper | whether any attacker (other than one human) has a brain of the given type | confirmed (code) |
| `0x0028e1e0` | `Brain_ReleaseAttackSlot` | helper | removes the human from the slot list when he holds one | confirmed (code) |
| `0x002904a0` | `Brain_IsEngaged` | helper | whether the brain holds an attack slot or has a target | confirmed (code) |
| `0x00290698` | `Brain_AttackCooldownOver` | helper | now at or past `+0x1e8` | confirmed (code) |
| `0x002906b8` | `Brain_CheckAttack` | helper | whether to attack now: 2 cooling down, 3 not attackable, 1 out of the fight stance, 4 hold back near the leader, 8 another attacker is ready, 5 the police have him, 6 shuffle instead (he is held or armed), 7 a player shields him, else 0 or a free sector | confirmed (code) |
| `0x00290e30` | `Brain_SetAttackTime` | helper | `+0x1e8` for an AI brain | confirmed (code) |
| `0x00290fd0` | `Brain_ClaimCoopTarget` | helper | outside co-op: claims `+0x204` for a human, true when it is his | inferred |
| `0x00291008` | `Brain_ClaimActiveAttacker` | helper | takes one of the target's four active-attacker places (`+0x1f0`), within `+0x14a` (`+0x14b` while he is held); may displace an idle one | confirmed (code) |
| `0x00291178` | `Brain_ReleaseActiveAttacker` | helper | clears a human from the four places | confirmed (code) |
| `0x002911d8` | `Brain_SetAttackSpacing` | helper | `+0x14a` | confirmed (code) |
| `0x002911e0` | `Brain_SetAttackSpacingHeld` | helper | `+0x14b` | confirmed (code) |

### Turf and the wanted timers {#wanted-timers}

A gang's turf (`+0x17c`) and its two wanted timers (gang `+0x5e8` and `+0x5f0`, both "expires at" times, 0 when off;
[Crimes: wanted](crimes.md#wanted)) are read by the brain. Confirmed (code) unless marked:

- `Brain_RefreshWanted` (`0x0028ff78`): when a cop (type 1) fights a member of a gang whose first timer runs, it
  sets that timer to 10 s from now (`Gang_SetWantedTimer`, `0x00169878`); a gang soldier (type 2) does the same for
  the second timer (`Gang_SetSecondWantedTimer`, `0x001698c8`). So a fight keeps a gang wanted while it lasts.
- `Brain_RefreshWantedEnemies` (`0x0028fff8`): only while goal `0x41` is on the stack; for every other gang that is
  wanted (either timer) and not of kind 1, 23 or 24, the first enemy-list member from that gang is passed to it.
- `Brain_HasAttackerOfType` (`0x0028de98`) tells whether any active attacker except one human has a given brain
  type. `Brain_CheckAttack` (`0x002906b8`) uses it to return 5, "the police have him", when the target's gang is not
  wanted (timer 0) and a cop already attacks him.
- `Filter_IsChaseable` (`0x0029c628`), the police chase filter, also takes only humans whose gang's first timer is 0.
  Speculative: wanted gang members are left to the arrest goals and the chase is for lesser offenders.
- `Brain_MayEngage` (`0x00290588`, outside this list) is described under [the Melee goal](ai.md#melee-goal).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0028ff08` | `Brain_GangHasTurf` | helper | the gang has a turf (`+0x17c`) | confirmed (code) |
| `0x0028ff18` | `Brain_IsInTurf` | helper | the human is inside his gang's turf (`0x001652a0`) | confirmed (code) |
| `0x0028ff38` | `Brain_IsPointInTurf` | helper | a point inside the gang's turf (`Gang_IsPointInTurf`) | confirmed (code) |
| `0x0028ff58` | `Brain_IsHumanInTurf` | helper | another human inside the gang's turf | confirmed (code) |
| `0x0028ff78` | `Brain_RefreshWanted` | helper | a cop who fights a member of a wanted gang renews its wanted timer (10 s); a gang member renews the second timer | confirmed (code) |
| `0x0028fff8` | `Brain_RefreshWantedEnemies` | helper | types 1 and 2 with goal `0x41`: for each other wanted gang (not kinds 1, 23, 24) with a member in the enemy list, `Brain_RefreshWanted` | confirmed (code) |

### Events and calls for help {#brain-events}

How a brain hears events and passes calls for help on; see [AI humans: events](ai.md#events).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0028c890` | `Brain_CrowdTauntPlayer` | helper | when a player attacks a non-civilian with two or more attackers, a random other attacker turns to the player and taunts (kind 3, line `0x90`) | confirmed (code) |
| `0x0028ca50` | `Brain_OnFoeDown` | helper | tactic types `0x0c` / `0x0d` are told; the foe, when he may gesture, says line `0x8f` (50 %) or `0x0f` | confirmed (code) |
| `0x0028cca8` | `Brain_NotifyTacticE` | helper | tells a gang tactic of type `0x0e` about the human (`0x0030c888`) | confirmed (code) |
| `0x0028cd10` | `Brain_OnObjectTaken` | helper | a player's pick-up of the object with model hash `0xbbbef927` (class not `0x12`) starts the 4.5 s Warrior pick-up window (`0x00292088`) when a free Warrior of his gang has class `0x12` | speculative |
| `0x0028f988` | `Brain_OnWoundedEvent` | helper | event 1 while wounded: tells the reaction goal (`0x002b5218`) | confirmed (code) |
| `0x002912b0` | `Brain_PickSideInFight` | helper | in a fight between two humans, the one to side against: the one who hits a friend, making gangs enemies when needed | confirmed (code) |
| `0x002913c0` | `Brain_MayReactToCrime` | helper | reacts when threat response is set, not "dead", top goal one of a list (and pops goals `0x5e`, `0x99`); under group-move tactics only up to the gang's respond percentage | confirmed (code) |
| `0x00291668` | `Brain_OnCrimeSeen` | helper | unless friendly with the offender, by type: cop, civilian or gang member handler | confirmed (code) |
| `0x00291728` | `Brain_OnDangerNear` | helper | away from trains: a danger with a point pushes a flee (unless goal 5); without one a free non-player AI avoids it for 3 s (`Goal_AvoidEnemies` 14 m, 7 m) | confirmed (code) |
| `0x00291960` | `Brain_OnViolenceSeen` | helper | no goal `0x9a`: a free AI with threat response sides in a fight it sees (`Brain_PickSideInFight`); a busy Warrior near it looks on and may taunt (10 %); else fights and calls help; with goal `0x9a` pushes a watch goal (`0x002d84e0`) | confirmed (code) |
| `0x00291d08` | `Brain_CallForHelp` | helper | unless in shadow mode, a 5 m help call (`Gang_BroadcastHelpCall`) | confirmed (code) |
| `0x00292158` | `Brain_TryPatternBlock` | helper | when the attacker's pattern (`+0x5d0`) reaches the human's block threshold and he is in a fight goal: a block (`Goal_TryBlock`), ending a stun, command 4 | confirmed (code) |
| `0x002935d8` | `Brain_IsInHearRange` | helper | whether an event's distance is within the hearing radius (`+0x134`; `+0x138` for event 20) | confirmed (code) |
| `0x00293640` | `Gang_BroadcastHelpCall` | helper | a call for help to nearby humans | confirmed (code) |

### Script bindings {#script-bindings}

The workers behind the script commands that set brain fields, by handle; see
[AI humans: scripted goals and actions](ai.md#scripted).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00292410` | `Brain_SetType` | helper | script: set a human's brain type | confirmed (code) |
| `0x00292460` | `Brain_SetPedType` | helper | script: set the ped type `+0x26c` | confirmed (code) |
| `0x002924a8` | `Brain_GetPedType` | helper | script: the ped type | confirmed (code) |
| `0x002924e8` | `Brain_SetEnabled` | helper | script: the brain-enabled byte `+0x08` | confirmed (code) |
| `0x00292590` | `Brain_FlushActions` | helper | script: clear the action queue | confirmed (code) |
| `0x002925d0` | `Brain_HasGoals` | helper | script: whether the goal stack is not empty | confirmed (code) |
| `0x00292618` | `Brain_FlushGoals` | helper | script: clear the goal stack | confirmed (code) |
| `0x00292658` | `Brain_PopGoal` | helper | script: pop the top goal when there is one | confirmed (code) |
| `0x002926a8` | `Brain_ProcessTopGoal` | helper | script: run the top goal's Process once | confirmed (code) |
| `0x00292708` | `Brain_SetThreatResponse` | helper | script: threat response `+0x21c` | confirmed (code) |
| `0x00292758` | `Brain_SetDamageResponse` | helper | script: damage response `+0x220` | confirmed (code) |
| `0x002927a8` | `Brain_SetInvestigateResponse` | helper | script: investigate response `+0x224` | confirmed (code) |
| `0x002927f8` | `Brain_SetPlayerResponse` | helper | script: player response byte `+0x228` | confirmed (code) |
| `0x00292848` | `Brain_SetWantsWeapon` | binding worker | brain `+0x265` | confirmed (code) |
| `0x00292890` | `Brain_SetWantsHat` | binding worker | brain `+0x266` | confirmed (code) |
| `0x002928d8` | `Brain_SetFieldOfView` | binding worker | degrees to radians into `+0x12c` | confirmed (code) |
| `0x00292938` | `Brain_SetMeleeRange` | binding worker | near `+0x13c`, far `+0x140` | confirmed (code) |
| `0x002929a8` | `Brain_SetAttackSlotCount` | binding worker | `Brain_SetAttackSlotCountRaw` | confirmed (code) |
| `0x002929f8` | `Brain_SetAttackWeight` | binding worker | one attack kind's weight, via `Brain_StoreAttackWeight` | confirmed (code) |
| `0x00292a60` | `Follow_SetSlotCount` | binding worker | the human's formation slot count and allowed followers (`0x00295dd8`) | confirmed (code) |
| `0x00292ac0` | `Follow_SelectSlotSet` | binding worker | the formation's slot set (`0x00295db0`) | confirmed (code) |
| `0x00292b10` | `Follow_SetSlotOffset` | binding worker | one slot's offset in a set, then back to the current set | confirmed (code) |
| `0x00292bc8` | `Brain_SetReactsToViolence` | binding worker | brain `+0x267` | confirmed (code) |
| `0x00292c10` | `Brain_SetWorldFlagUse` | binding worker | `+0x2d1` allowed, `+0x2d2` chance (0 when not allowed) | confirmed (code) |
| `0x00292c68` | `Brain_HasEnemies` | binding worker | brain `+0x152` not 0 | confirmed (code) |
| `0x00292ca8` | `Brain_HasAttackers` | binding worker | the attack-slot list not empty | confirmed (code) |
| `0x00292cf0` | `Brain_ClearBackoff` | binding worker | pops a top goal of type `0x9b` | confirmed (code) |

### Configuration {#ai-config}

The `Cfg...` workers that set the AI's global tuning values; the melee ranges are in
[AI humans: melee ranges](ai.md#melee-range), the score weights in [the score](ai.md#enemy-score).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00294650` | `Cfg_SetMeleeNear` | helper | default near melee range (`0x00510ab4`) | confirmed (code) |
| `0x00294660` | `Cfg_GetMeleeNear` | helper | reads it | confirmed (code) |
| `0x00294670` | `Cfg_SetMeleeFar` | helper | default far melee range (`0x00510ab8`) | confirmed (code) |
| `0x00294680` | `Cfg_GetMeleeFar` | helper | reads it | confirmed (code) |
| `0x00294690` | `Cfg_SetMeleeRange` | binding worker | `CfgSetMeleeRange`: both defaults | confirmed (code) |
| `0x002946c0` | `Cfg_SetTargetingPoints` | binding worker | the score weights ([Enemy score](ai.md#enemy-score)) | confirmed (code) |
| `0x00294718` | `Cfg_SetTargetingPointsEx` | binding worker | the other score weights | confirmed (code) |
| `0x00294788` | `Cfg_SetDefaultFollowSlotSet` | binding worker | writes the slot set into every pool formation (`0x00294bf8`), in use or not | confirmed (code) |
| `0x002947f0` | `Cfg_SetBaseChanceToBlock` | binding worker | `0x00510ac8` when 100 or less | confirmed (code) |
| `0x00294808` | `Cfg_SetVerticalSightModifier` | binding worker | `0x00510ad4` | confirmed (code) |
| `0x00294818` | `Cfg_SetCanBeAttackedModifier` | binding worker | `0x00510ad0` | confirmed (code) |
| `0x00294828` | `Cfg_SetCivilianAggression` | binding worker | `0x00510ad8`, `0x00510ad9` | confirmed (code) |
| `0x00294840` | `Cfg_SetSearchTimes` | binding worker | `0x00510ada`-`0x00510adc` | confirmed (code) |
| `0x00294860` | `Cfg_SetMercyStruggleDamage` | binding worker | `0x00510acc` | confirmed (code) |
| `0x00294870` | `Cfg_SetSearchCounts` | binding worker | `0x00510ade`, `0x00510add` | confirmed (code) |
| `0x00294888` | `Cfg_SetChanceToGetHelp` | binding worker | `0x00510adf`, at most 100 | confirmed (code) |
| `0x002948a0` | `Peds_SetInteractDelay` | binding worker | `0x00510fcc` | confirmed (code) |

### Formations {#formation-code}

The 42 formations (0x280 bytes at `0x006ceaf0`): slot sets, slot points and which follower takes which slot; see
[AI humans: formations and follow slots](ai.md#formations).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00293ad0` | `Formation_Alloc` | helper | the first free formation, from 31 for one kind and 0 for the other, up to 42 | confirmed (code) |
| `0x002942c8` | `Gang_ClearWayForLeader` | helper | members within 6 m ahead of a free leader (in a cone of the given angle, or within 1 m) whose queue is idle are cleared | confirmed (code) |
| `0x00294a40` | `Formation_Construct` | constructor | followers cleared, no leader | confirmed (code) |
| `0x00294ad8` | `Formation_Init` | helper | leader, 6 slots, 6 allowed, set 0 | confirmed (code) |
| `0x00294b60` | `Formation_Clear` | helper | no leader, counts 0, all slots unusable, followers cleared | confirmed (code) |
| `0x00294bf8` | `Formation_SetSlotSet` | helper | writes 9 slot offsets (1/16 m) into a set, all unusable until planned | confirmed (code) |
| `0x00294c98` | `Formation_RemoveFollowerAt` | helper | clears a follower entry, count − 1, replan | confirmed (code) |
| `0x00294ce0` | `Formation_UncrossPaths` | helper | swaps the slots of follower pairs whose paths cross | confirmed (code) |
| `0x00294e98` | `Formation_DropGoneFollowers` | helper | followers whose human is gone leave | confirmed (code) |
| `0x00294f38` | `Formation_UpdateSlotPoints` | helper | the slots' world points from the leader's pose, dropped to the ground; a slot is usable when the leader sees it, up to `+0x272` | confirmed (code) |
| `0x00295250` | `Formation_SetFollowerSpeeds` | helper | a follower walking to its slot (goals `0x33`, `0x69`, `0x73`) gets a move speed factor of 1.0 or 0.1 | inferred |
| `0x002953c8` | `Formation_AssignSlots` | helper | each usable slot takes its nearest unassigned follower, up to `+0x272` | confirmed (code) |
| `0x00295538` | `Formation_AssignQueue` | helper | followers left over queue behind the nearest slotted one | confirmed (code) |
| `0x00295d08` | `Formation_PlanAndNotify` | helper | plans, then tells each follower | inferred |
| `0x00295db0` | `Formation_SelectSet` | helper | `+0x270` and replan | confirmed (code) |
| `0x00295dd8` | `Formation_SetSlotCount` | helper | `+0x271` slots, `+0x272` allowed (the same when −1), replan flag | confirmed (code) |
| `0x00295e00` | `Formation_HasSlot` | helper | whether a follower has a slot | confirmed (code) |
| `0x00295e38` | `Formation_GetFollowPoint` | helper | a follower's slot point, or the position of the one he queues behind | confirmed (code) |
| `0x00295ec8` | `Formation_SetSlotOffset` | helper | one slot's offset in the current set, replan flag | confirmed (code) |
| `0x00296028` | `Formation_Leave` | helper | a follower leaves; his brain `+0x212` = −1 | confirmed (code) |
| `0x002960c8` | `Formation_HasFollower` | helper | whether a human follows in it | confirmed (code) |
| `0x00296130` | `Formation_IsSlotUsable` | helper | slot usable in the current set (replans first if flagged) | confirmed (code) |
| `0x00296190` | `Formation_IsBehindSlot` | helper | whether a follower lags behind his slot or the one ahead (a forward test), not when that one is slower than 0.5 | inferred |
| `0x002962f0` | `Formation_FollowerIndex` | helper | a human's follower index, or −1 | confirmed (code) |
| `0x002963b0` | `Formation_GetAhead` | helper | the human a follower queues behind, else the leader | confirmed (code) |
| `0x00296488` | `Formation_MakeShape` | helper | lays out a set: 0 a ring, 1 a ring with a gap behind, 2 two files at the given spacing | confirmed (code) |
| `0x0029a0b0` | `PairLink_Init` | helper | a two-human link used by two goal classes; the owner's formation is set up as a two-slot line | inferred |
| `0x0029a1a8` | `PairLink_TryJoin` | helper | when both sides are free, links them and the other joins the owner's formation | inferred |
| `0x0029a268` | `PairLink_Break` | helper | unlinks both sides and the follower leaves the formation | inferred |
| `0x0029a300` | `PairLink_Validate` | helper | drops the link when the other human is gone | confirmed (code) |

### Waypoint queues {#queue-code}

The 20 waypoint queues (0x90 bytes at `0x006cde30`) where humans wait their turn at a node; see
[AI humans: waypoint queues](ai.md#queues).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00293d08` | `Queue_ClaimForNode` | helper | the waypoint queue (20 × 0x90 at `0x006cde30`, bits `0x006ce970`) for a node, made on demand | confirmed (code) |
| `0x002941c0` | `Queue_GetSpotFor` | helper | the human's spot in a node's queue when he can walk straight to it, else the node | confirmed (code) |
| `0x00299530` | `Queue_Construct` | helper | an empty waypoint queue constructor | confirmed (code) |
| `0x00299c98` | `Queue_IsForNode` | helper | whether a node is one of the queue's two nodes (`+0x60`, `+0x64`) | confirmed (code) |
| `0x00299cc0` | `Queue_IsEmpty` | helper | whether all six member slots (`+0x68`) are empty | confirmed (code) |
| `0x00299cf0` | `Queue_HasMember` | helper | whether a human is in the queue | confirmed (code) |
| `0x00299d20` | `Queue_AddMember` | helper | puts a human in the first free slot and flags a re-layout (`+0x8c`) | confirmed (code) |
| `0x00299d80` | `Queue_RemoveMember` | helper | takes a human out | confirmed (code) |
| `0x00299db0` | `Queue_GetSpot` | helper | a member's queue spot, else the node's point | inferred |

### Routes {#route-code}

Following a planned route leg by leg, including climb, jump and smash legs; see
[AI humans: following a route](ai.md#route-follow) and [jump legs](ai.md#route-jump).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0029a358` | `RouteState_Construct` | helper | constructs a route-following state | confirmed (code) |
| `0x0029a3c0` | `RouteState_GetPoint` | helper | the point of the current waypoint | confirmed (code) |
| `0x0029aa20` | `RouteState_Stop` | helper | stops following unless mid-leg; frees the route, index none | inferred |
| `0x0029aa78` | `RouteState_SetFlag` | helper | sets byte `+0x11` | speculative |
| `0x0029afe0` | `RouteState_CheckArrival` | helper | on a walking leg, measures the progress to the waypoint; done legs go to state 5 | inferred |
| `0x0029b238` | `Route_NodeCount` | helper | the number of nodes in a route | confirmed (code) |
| `0x0029b248` | `RouteState_CurrentNode` | helper | the node at the current index | confirmed (code) |
| `0x0029b268` | `Route_NodeAt` | helper | the node at an index, or none | confirmed (code) |
| `0x0029b2b8` | `Route_GetWaypointPoint` | helper | a waypoint's point, using the queue spot when the link into it is a queue link (flag 4) | confirmed (code) |
| `0x0029b428` | `RouteState_SetState` | helper | sets the leg state, ending any steering first | confirmed (code) |
| `0x0029b470` | `RouteState_Release` | helper | tells the nav code he stopped waiting and frees the route | confirmed (code) |
| `0x0029b848` | `Route_ClimbLeg` | helper | steers at the climb point for up to 31 tries and starts a climb | confirmed (code) |
| `0x0029b9b0` | `Route_TryClimbNear` | helper | within 4.5 m of the point, tries to climb and advances the route | confirmed (code) |
| `0x0029baa8` | `Route_StartJumpLeg` | helper | starts a jump leg when the gap needs it, else walks on | confirmed (code) |
| `0x0029bca0` | `Route_SmashLeg` | helper | at a breakable door or pane: steers to it and issues the smash command | inferred |

### Human lists and filters {#human-filters}

Callbacks that accept or refuse a human or an object, and the helpers that build short handle lists with them.
Goals use them to find targets, backup and partners ([AI humans: starting a fight](ai.md#targets)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0029bf98` | `Filter_IsThreatTo` | filter | filter set by Brain_Init: a cop within 10 m in sight, or a valid enemy in sight | inferred |
| `0x0029c0d8` | `Filter_IsThrowTarget` | filter | not cuffed, in sight, a valid enemy (thrower goals) | confirmed (code) |
| `0x0029c160` | `Filter_IsFreeBackup` | filter | an up, friendly human not already engaged (police backup count) | confirmed (code) |
| `0x0029c2e8` | `Filter_IsCop` | filter | brain type 1 | confirmed (code) |
| `0x0029c310` | `Filter_IsIdleCop` | filter | a cop whose top goal is 1 or `0x73` | confirmed (code) |
| `0x0029c3a0` | `Filter_IsGangFighting` | filter | his gang's `+0x32` is 1 or a member has attackers | inferred |
| `0x0029c3f8` | `Filter_IsFriendly` | filter | friendly to the asker | confirmed (code) |
| `0x0029c498` | `Filter_IsUpAndFree` | filter | not cuffed, not flagged, not knocked out | confirmed (code) |
| `0x0029c508` | `Filter_IsChatPartner` | filter | a player-controlled human, or a type-4 brain of gang kind `0x17` | inferred |
| `0x0029c570` | `Filter_IsAvailableFriend` | filter | friendly, willing (`+0x2d6`), not busy, actions free, no goal `0x5f` | confirmed (code) |
| `0x0029c628` | `Filter_IsChaseable` | filter | for police chases: not friendly, not in shadow, gang wanted timer `+0x5e8` at 0, up, a valid enemy | confirmed (code) |
| `0x0029c6d0` | `Humans_ListNear` | helper | up to 16 nearby humans passing a filter, as handles | confirmed (code) |
| `0x0029c7e8` | `Humans_ListNearVisible` | helper | the same, only those in line of sight | confirmed (code) |
| `0x0029c910` | `Humans_ListNearPointVisible` | helper | the same around a given point | confirmed (code) |
| `0x0029ca28` | `Humans_ListSeen` | helper | in line of sight and within 3 m or in the field of view (`+0x12c`) | confirmed (code) |
| `0x0029cbb8` | `HandleList_FillVisible` | helper | clears a 16-handle list and fills it with visible humans passing a filter | inferred |
| `0x0029cc78` | `HandleList_FillSeen` | helper | the same with Humans_ListSeen | inferred |
| `0x0029cd30` | `HandleList_Nearest` | helper | the nearest live human in a handle list to a point, passing a filter | confirmed (code) |
| `0x0029ce40` | `Human_IsUp` | helper | present and not flagged out | confirmed (code) |
| `0x0029ce70` | `Handle_IsUp` | helper | Human_IsUp on a handle | confirmed (code) |
| `0x0029d330` | `ObjFilter_HasFlags` | filter | object flags match a mask | confirmed (code) |
| `0x0029d3f8` | `ObjFilter_HasName` | filter | object type name equals a given one | confirmed (code) |
| `0x0029d438` | `ObjFilter_NearestWithFlags` | filter | keeps the nearest flagged object beyond a minimum distance | inferred |
| `0x0029d4f8` | `ObjFilter_IsVandalisable` | filter | vandalisable and not flag `0x800000` | confirmed (code) |
| `0x0029d558` | `ObjFilter_VandalisableInRange` | filter | vandalisable, not finished, within a range | inferred |
| `0x0029d928` | `Glass_ClaimPane` | helper | picks a glass pane in range for the gang to break and claims it for 7 s | inferred |
| `0x0029dc38` | `Brains_Nearest` | helper | the nearest enabled brain's human to a point, passing a filter | confirmed (code) |
| `0x0029dd38` | `Gang_CountAhead` | helper | counts gang members in a 45-degree cone ahead, stopping at 4 | inferred |
| `0x0029df78` | `Gang_NearestInCone` | helper | the nearest gang member, beyond 1.25 m only inside a given cone | confirmed (code) |
| `0x0029e168` | `Humans_TwoGangsFighting` | helper | whether the fighting humans nearby come from more than one gang | inferred |

### Neighbour sectors {#sectors}

Each human has a record of the eight sectors around him (0x48 bytes at `0x006e8318`, 60 records): which sector
holds a near human or a wall. Steering, side-picking in melee and the attack check read it; see
[AI humans: steering round humans](ai.md#steering).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0028fe90` | `Brain_GetSectors` | helper | the human's sector record, refreshed (`Sectors_Update`) when older than the age given (ms) | confirmed (code) |
| `0x0029e250` | `Sectors_Construct` | helper | constructs a human's eight-sector record (0x48 bytes at `0x006e8318`) | confirmed (code) |
| `0x0029e2b0` | `Sectors_Reset` | helper | marks all sectors stale, timer 0 | confirmed (code) |
| `0x0029e2e8` | `Sectors_ResetHeading` | helper | stores the current heading and marks sectors stale | confirmed (code) |
| `0x0029e350` | `Sectors_GetPoint` | helper | the owner's position plus a distance along his current heading + k × 45° | confirmed (code) |
| `0x0029e470` | `Sectors_GetOwner` | helper | the human a sector record belongs to | confirmed (code) |
| `0x0029e4b0` | `Sectors_ProbeWall` | helper | lazily once per rebuild: no walkable straight line (`Human_CanWalkStraightTo`) to the point 1.5 m out at the sector's centre sets bit 4 ([Neighbour sectors](ai.md#neighbour-sectors)) | confirmed (code) |
| `0x0029e5d8` | `Sectors_Update` | helper | the rebuild: humans within 1.5 m of the owner, counted and the nearest kept per sector; flags 3 when the nearest's squared distance is below 1.5 (1.22 m), else 1; bit 8 for a free player within 5.5 m ([Neighbour sectors](ai.md#neighbour-sectors)) | confirmed (code) |
| `0x0029e980` | `Sectors_GetCost` | helper | a sector's flags plus twice its human count | confirmed (code) |
| `0x0029e9c8` | `Sectors_IsBlocked` | helper | a near human, or a probed wall | confirmed (code) |
| `0x0029ea10` | `Sectors_IsWall` | helper | the wall bit | confirmed (code) |
| `0x0029ea48` | `Sectors_IsFree` | helper | no human and no wall | confirmed (code) |
| `0x0029eaa0` | `Sectors_AllClear` | helper | no sector has bit 1 or 2 (no human within 1.5 m; used by the Warriors' pick-up check) | confirmed (code) |
| `0x0029ead8` | `Sectors_IsHeldBy` | helper | whether a given human is the near one in a sector | confirmed (code) |
| `0x0029eb10` | `Sectors_SectorOf` | helper | the sector (0-7, 45° each, centred on k × 45°, rising anticlockwise) of a point relative to a human's heading | confirmed (code) |
| `0x0029ec90` | `Sectors_TurnWay` | helper | the shorter way round (+1 or −1) between two of eight sectors | confirmed (code) |
| `0x0029ecd8` | `Sectors_ResetAll` | helper | constructs all 60 sector records | confirmed (code) |
| `0x0029ed38` | `Sectors_StaticInit` | helper | static initialiser calling ResetAll | confirmed (code) |

### Pools and global state {#ai-pools}

Boot and level resets of the AI pools, global clocks, and a few one-line game-state setters; see
[AI humans: the update](ai.md#update).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00291e78` | `AI_ResetSectorTable` | helper | resets the 7 × 2 handle table at `0x006cddf8` | confirmed (code) |
| `0x00292008` | `AI_Every8s` | helper | true once per 8 s (global clock `0x00510a94`) | confirmed (code) |
| `0x00292048` | `AI_Every6s` | helper | true once per 6 s (`0x00510a98`); the one-throw-per-6-s gate | confirmed (code) |
| `0x00292088` | `AI_SetPickupWindow` | helper | `0x00510a9c` = now + ms (0 clears) | confirmed (code) |
| `0x002922a8` | `AI_ClearSectorTable` | helper | clears the table at `0x006cddf8` (7 × 2) | confirmed (code) |
| `0x00292310` | `AI_ClearSectorTableAll` | helper | `AI_ClearSectorTable(1, 0xffff)` | confirmed (code) |
| `0x00293840` | `Brains_Reset` | helper | at boot: brains disabled, formations reset, type handlers registered, pool bitmaps cleared | confirmed (code) |
| `0x00294618` | `Brains_SetGlobalByte` | helper | writes byte `+0x0a` (suspended) of every brain | confirmed (code) |
| `0x002948b0` | `AI_Nop1` | helper | empty | confirmed (code) |
| `0x002948b8` | `AI_Nop2` | helper | empty | confirmed (code) |
| `0x002948c0` | `AI_ResetPools` | helper | clears the queues, the node claims (40 at `0x006ce978`), the pool bitmaps, the formations and the brains' lists | confirmed (code) |
| `0x00294a20` | `AI_ResetPoolsAll` | helper | `AI_ResetPools(1, 0xffff)` | confirmed (code) |
| `0x00299238` | `Human_SetLookAtTarget` | helper | an empty stub | confirmed (code) |
| `0x00299248` | `Goal_StartParlay` | helper | starts a parlay cut-scene: preloads its scenes, saves the threat response, pushes the parlay goal | confirmed (code) |
| `0x00299470` | `Human_SetPendingWeapon` | helper | for `HuGiveWeapon`: stores the weapon id with bit `0x80` at human `+0x3a8` | confirmed (code) |
| `0x002994b8` | `Human_DropWeapon` | helper | drops what a human holds | confirmed (code) |
| `0x00299510` | `GameState_SetSpawnMax` | helper | sets the spawn maximum (game state `+0x434`) | confirmed (code) |
| `0x00299520` | `GameState_SetCopSpawnMax` | helper | sets the police spawn maximum (game state `+0x326`) | confirmed (code) |

## Actions {#actions}

The action classes in `0x002f9e48`-`0x002fee38` ([AI humans: actions](ai.md#actions) has the pool and the base
fields). Each class has a vtable whose word `+0x0c` returns its type id and `+0x34` its name; the names below are the
executable's. Start returns 2 when the action is already done, Abort 0 when it refuses, Update 2 when it is done.
Confirmed (code) for the vtables and their slots.

| Class (type) | Vtable | Init | Start | Abort | Update | What it does |
| --- | --- | --- | --- | --- | --- | --- |
| `MoveTo` (1) | `0x00542f20` | `0x002fb9e8`, `0x002fbae0` | `0x002fc420` | `0x002fc560` | `0x002fc5c0` | walks to a point ([Moving](ai.md#move-action)) |
| `MoveMelee` (2) | `0x005431e0` | `0x002fcf50` | `0x002fcff0` | `0x002fd068` | `0x002fd4e8` | keeps a fight distance band from a human ([MoveMelee](ai.md#move-melee)) |
| `MoveMeleeLine` (3) | `0x00542fa0` | `0x002fe6e8` | `0x002fe720` | - | `0x002fe740` | sidesteps round a human for 2 s |
| `Dive` (4) | `0x005431a0` | `0x002fda38` | `0x002fda68` | `0x002fdae8` | `0x002fdb28` | dives toward a point |
| `Shuffle` (5) | `0x00542f60` | `0x002feaf8` | `0x002feb28` | - | `0x002feb48` | shuffles sideways in the fight stance |
| `TurnToDir` (6) | `0x00543160` | `0x002fdc28` | `0x002fdc68` | `0x002fdcc8` | `0x002fdd08` | turns to a heading |
| `Turn` (7) | `0x00543120` | `0x002fde78` | `0x002fdec8` | `0x002fdcc8` | `0x002fdf50` | turns by n × 45° |
| `TurnTo` (8) | `0x005430e0` | `0x002fe000` | `0x002fe058` | `0x002fdcc8` | `0x002fe0a8` | turns to a point |
| `LookAt` (9) | `0x005430a0` | `0x002fe160` | `0x002fdc68` | `0x002fdcc8` | `0x002fe1b0` | turns to a human, following him |
| `LookAround` (10) | `0x00543060` | `0x002fe248` | `0x002fe280` | `0x002fe320` | `0x002fe360` | plays the look-around clip with a wide view |
| `TakeStep` (11) | `0x00543020` | `0x002fe380` | `0x002fe3a8` | `0x002fe3d8` | `0x002fe428` | one step on a heading |
| `GiveWay` (12) | `0x00542fe0` | `0x002fe568` | `0x002fe5d8` | `0x002fe658` | `0x002fe6c8` | a step aside for a human, looking at him |
| `Attack` (13) | `0x00542ce0` | `0x002fa918` | `0x002fa9a8` | `0x002fad30` | `0x002fad70` | [The attack action](ai.md#attack-action) |
| `PlayFidget` (15) | `0x00542c60` | `0x002f9f48` | `0x002f9f78` | `0x002fa0e0` | `0x002fa1e0` | an idle fidget clip, with an optional line |
| `ParlayStart` (17) | `0x00542c20` | `0x002fa220` | `0x002fa248` | - | `0x002fa270` | waits for the pad to start a parley |
| `PlayGenAnim` (20) | `0x00542b60` | `0x002fa5a0` | `0x002fa5e0` | `0x002fa608` | `0x002fa650` | a gang clip of an anim id |
| `PlayGenToIdleAnim` (21) | `0x00542b20` | `0x002fa6e0` | `0x002fa710` | `0x002fa770` | `0x002fa800` | a clip that ends in the idle |
| `PlayDynAnim` (22) | `0x00542be0` | `0x002fa300` | `0x002fa338` | `0x002fa398` | `0x002fa3e0` | a dynamic clip with a variant |
| `PlayDynLoopAnim` (23) | `0x00542ba0` | `0x002fa468` | `0x002fa4a0` | `0x002fa4c8` | `0x002fa510` | a dynamic clip played until its flags clear |
| `PlaySound` (24) | `0x00542d20` | `0x002fb868` | `0x002fb898` | - | `0x002fb8a0` | says a speech command |
| `Nothing` (25) | `0x00542ee0` | `0x002faf70` | - | - | `0x002faf90` | does nothing |
| `PickUpItem` (26) | `0x00542ea0` | `0x002faf98` | `0x002fb000` | `0x002fb050` | `0x002fb0a8` | picks up an object |
| `Revive` (27) | `0x00542de0` | `0x002fb418` | `0x002fb440` | `0x002fb470` | `0x002fb4a8` | revives a downed ally |
| `SetCommand` (28) | `0x00542ca0` | `0x002faeb0` | `0x002faee0` | - | `0x002faf20` | presses one pad command |
| `Taunt` (29) | `0x00542e60` | `0x002fb0f0` | `0x002fb128` | `0x002fb278` | `0x002fb2f0` | a taunt or gesture by kind |
| `Tag` (30) | `0x00542da0` | `0x002fb4f0` | `0x002fb520` | `0x002fb578` | `0x002fb5c0` | sprays a tag |
| `UsePhone` (31) | `0x00542d60` | `0x002fb650` | `0x002fb680` | `0x002fb6f0` | `0x002fb738` | stands at a phone flag |
| `UnlockHuman` (32) | `0x00542e20` | `0x002fb338` | `0x002fb360` | `0x002fb390` | `0x002fb3c8` | frees a cuffed human (a mini-game) |
| `Stop` (`0x4141`) | `0x00543220` | `0x002fcf28` | - | - | `0x002fcf48` | stops the human |

Every function of the classes, and the helpers among them:

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f9e48` | `Action_Update` | base Update | counts the start delay down (−1 = a random 0-500 ms) against the brain's last update time, calls Start once, then the class's Update | confirmed (code) |
| `0x002f9f48` | `FidgetAction_Init` | `PlayFidget` init | fidget kind, line, delay | confirmed (code) |
| `0x002f9f78` | `FidgetAction_Start` | `PlayFidget` Start | done for a dog (class 221), a brain of kind 6, or without a fidget clip (`0x00225f58`) unless holding a kind-11 object; says the line (1.5 × louder when the target is a player), plays the fidget (`0x00231ac8`) | confirmed (code) |
| `0x002fa0e0` | `FidgetAction_Abort` | `PlayFidget` Abort | refused for a Warrior whose crew is following a free chief who has no enemies around (command neither 0, 4 nor 5); else stops the fidget (`0x00231b60`) | confirmed (code) |
| `0x002fa1e0` | `FidgetAction_Update` | `PlayFidget` Update | done once the fidget's flags clear (`0x00228520`) | confirmed (code) |
| `0x002fa220` | `ParlayStartAction_Init` | `ParlayStart` init | a deadline | confirmed (code) |
| `0x002fa270` | `ParlayStartAction_Update` | `ParlayStart` Update | with the deadline passed or none, waits for pad 0's button `0x10`; writes game state `+0x412` = 1 (pressed) or 3 (timed out) | confirmed (code); the parley meaning inferred from the class name |
| `0x002fa300` | `PlayAnimAction_Init` | `PlayDynAnim` init | anim id, variant, blends ([The dealer's gestures](ai.md#dealer-gestures)) | confirmed (code) |
| `0x002fa338` | `PlayAnimAction_Start` | `PlayDynAnim` Start | [The dealer's gestures](ai.md#dealer-gestures) | confirmed (code) |
| `0x002fa398` | `PlayAnimAction_Abort` | `PlayDynAnim` Abort | clears the clip's held flags | confirmed (code) |
| `0x002fa3e0` | `PlayAnimAction_Update` | `PlayDynAnim` Update | plays once, waits while its flags last | confirmed (code) |
| `0x002fa468` | `DynLoopAnimAction_Init` | `PlayDynLoopAnim` init | anim id, variant, blend | confirmed (code) |
| `0x002fa4a0` | `DynLoopAnimAction_Start` | `PlayDynLoopAnim` Start | stops the gait | confirmed (code) |
| `0x002fa4c8` | `DynLoopAnimAction_Abort` | `PlayDynLoopAnim` Abort | drops the overlay task while its flags are held | confirmed (code) |
| `0x002fa510` | `DynLoopAnimAction_Update` | `PlayDynLoopAnim` Update | starts the clip (`0x0025a3e0`, blend 0.1, looping), then waits while its flags are held | confirmed (code) |
| `0x002fa5a0` | `PlayAnimIdAction_Init` | `PlayGenAnim` init | owner, anim id (`+0xc`), two blend times (`+0x10`, `+0x14`), a variant/once byte (`+0x1c`), a flag (`+0x1d`), a short (`+4`); clears `+6`, `+0x18`, `+0x1e`; about 50 callers (the big goals' clips, taunts, the dealer's push, `Brain_Think`) | confirmed (code) |
| `0x002fa5e0` | `PlayAnimIdAction_Start` | `PlayGenAnim` Start | stops the gait | confirmed (code) |
| `0x002fa608` | `PlayAnimIdAction_Abort` | `PlayGenAnim` Abort | rebuilds the idle when the clip's flags are still held | confirmed (code) |
| `0x002fa650` | `PlayAnimIdAction_Update` | `PlayGenAnim` Update | plays once, waits while its flags last | confirmed (code) |
| `0x002fa6e0` | `GenToIdleAnimAction_Init` | `PlayGenToIdleAnim` init | anim id, "hold" flag | confirmed (code) |
| `0x002fa710` | `GenToIdleAnimAction_Start` | `PlayGenToIdleAnim` Start | drops a held kind-4 or kind-6 object, stops the gait | confirmed (code) |
| `0x002fa770` | `GenToIdleAnimAction_Abort` | `PlayGenToIdleAnim` Abort | with the hold flag: rebuilds the idle while `0x20000` is held; else refused while `0x1c16a40` is held and he is up | confirmed (code) |
| `0x002fa800` | `GenToIdleAnimAction_Update` | `PlayGenToIdleAnim` Update | plays the clip as an action clip holding `0x20000` (`0x22000` without the hold flag), waits while held | confirmed (code) |
| `0x002fa878` | `Action_ExecuteAttack` | binding worker | queues an attack on a human (`Brain_QueueAttack`) | confirmed (code) |
| `0x002faeb0` | `SetCommandAction_Init` | `SetCommand` init | a command id | confirmed (code) |
| `0x002faee0` | `SetCommandAction_Start` | `SetCommand` Start | writes the command to his player record (`PlayerRecord_SetCommand`) | confirmed (code) |
| `0x002faf20` | `SetCommandAction_Update` | `SetCommand` Update | one update, then waits while any flag is held | confirmed (code) |
| `0x002faf70` | `NothingAction_Init` | `Nothing` init | base fields only | confirmed (code) |
| `0x002faf98` | `PickUpItemAction_Init` | `PickUpItem` init | the object's handle | confirmed (code) |
| `0x002fb000` | `PickUpItemAction_Start` | `PickUpItem` Start | sets the object as his pick-up target (`0x00227080`) and presses command `0x33` | confirmed (code) |
| `0x002fb050` | `PickUpItemAction_Abort` | `PickUpItem` Abort | refused while the pick-up runs (`0x00228408`); else clears the target | confirmed (code) |
| `0x002fb0a8` | `PickUpItemAction_Update` | `PickUpItem` Update | waits while the pick-up runs | confirmed (code) |
| `0x002fb0f0` | `TauntAction_Init` | `Taunt` init | kind, speech line, delay | confirmed (code) |
| `0x002fb128` | `TauntAction_Start` | `Taunt` Start | done for a brain of kind 6, or without a taunt clip unless kind 13; plays it (`0x0025f028`) and says the line; kinds 0-2 and 8 are gestures, others counted in `0x005112ac` | confirmed (code) |
| `0x002fb278` | `TauntAction_Abort` | `Taunt` Abort | refused while the taunt plays (`0x00223c30`); else uncounts it | confirmed (code) |
| `0x002fb2f0` | `TauntAction_Update` | `Taunt` Update | waits while the taunt plays | confirmed (code) |
| `0x002fb338` | `UnlockHumanAction_Init` | `UnlockHuman` init | base fields | confirmed (code) |
| `0x002fb360` | `UnlockHumanAction_Start` | `UnlockHuman` Start | presses command `0x32` | confirmed (code) |
| `0x002fb390` | `UnlockHumanAction_Abort` | `UnlockHuman` Abort | refused while he is in the mini-game | confirmed (code) |
| `0x002fb3c8` | `UnlockHumanAction_Update` | `UnlockHuman` Update | waits while he is in the mini-game | confirmed (code) |
| `0x002fb418` | `ReviveAction_Init` | `Revive` init | base fields | confirmed (code) |
| `0x002fb440` | `ReviveAction_Start` | `Revive` Start | presses command `0x27` | confirmed (code) |
| `0x002fb470` | `ReviveAction_Abort` | `Revive` Abort | refused while reviving (`0x00228468`) | confirmed (code) |
| `0x002fb4a8` | `ReviveAction_Update` | `Revive` Update | waits while reviving | confirmed (code) |
| `0x002fb4f0` | `TagAction_Init` | `Tag` init | the tag spot and its argument | confirmed (code) |
| `0x002fb520` | `TagAction_Start` | `Tag` Start | starts the tag (`Human_Tag`) | confirmed (code) |
| `0x002fb578` | `TagAction_Abort` | `Tag` Abort | refused while tagging | confirmed (code) |
| `0x002fb5c0` | `TagAction_Update` | `Tag` Update | presses command 10 each update, waits while tagging (or while his top task is kind `0x17`) | confirmed (code) |
| `0x002fb650` | `UsePhoneAction_Init` | `UsePhone` init | the phone flag, a duration, a line | confirmed (code) |
| `0x002fb680` | `UsePhoneAction_Start` | `UsePhone` Start | switches his anims to the phone set (`Human_UseAnim` with the name at `0x005686c0`), deadline = now + duration | confirmed (code) |
| `0x002fb6f0` | `UsePhoneAction_Abort` | `UsePhone` Abort | back to his own anims | confirmed (code) |
| `0x002fb738` | `UsePhoneAction_Update` | `UsePhone` Update | done at the deadline; says the line once when someone is near him (`+0x348`/`+0x34c`); faces the flag's heading, standing | confirmed (code) |
| `0x002fb868` | `PlaySoundAction_Init` | `PlaySound` init | speech command, volume, a flag | confirmed (code) |
| `0x002fb8a0` | `PlaySoundAction_Update` | `PlaySound` Update | says the command when he may speak (`0x00291ed0`); done at once | confirmed (code) |
| `0x002fb918` | `Action_MoveTo` | binding worker | a move action for a scripted human | confirmed (code) |
| `0x002fbae0` | `MoveAction_InitEx` | `MoveTo` init | point, radius, gait, speed (at least the walk's), two flags | confirmed (code) |
| `0x002fbbe0` | `MoveAction_SetSpeed` | `MoveTo` helper | stores the speed (`+0x40`) | confirmed (code) |
| `0x002fbbe8` | `MoveAction_SetTarget` | `MoveTo` helper | a new destination farther than 0.07 m: stored, the route is kept or the action restarted | confirmed (code) |
| `0x002fbd18` | `MoveAction_CollectPoints` | `MoveTo` helper | the next points of the route, with the leg kinds | confirmed (code) |
| `0x002fbef0` | `MoveAction_TrialSpeed` | `MoveTo` helper | the speed a simulated walk along the points allows ([Moving](ai.md#move-action)) | confirmed (code) |
| `0x002fc158` | `MoveAction_CornerSpeed` | `MoveTo` helper | the corner speed of the next waypoints | confirmed (code) |
| `0x002fc330` | `MoveAction_CheckStuck` | `MoveTo` helper | every 60 updates while moving: less than 0.2 m covered → brain `+0x284` = 3 | confirmed (code) |
| `0x002fc3e8` | `MoveAction_ResetSpeed` | `MoveTo` helper | speed = the gait's speed | confirmed (code) |
| `0x002fc560` | `MoveAction_Abort` | `MoveTo` Abort | marks it aborted, clears the move and the route | confirmed (code) |
| `0x002fcd90` | `Move_CornerSpeedLimit` | `MoveTo` helper | the highest speed that still turns through a corner circle, stepping the gait down | confirmed (code) |
| `0x002fceb8` | `Action_Stop` | binding worker | a `Stop` action for a scripted human | confirmed (code) |
| `0x002fcf28` | `StopAction_Init` | `Stop` init | base fields | confirmed (code) |
| `0x002fcff0` | `MoveMeleeAction_Start` | `MoveMelee` Start | deadline; a brain of kind 6 or 7 leaves the fight stance; saves the turn boost | confirmed (code) |
| `0x002fd068` | `MoveMeleeAction_Abort` | `MoveMelee` Abort | faces the target (kind 6 or 7), restores the turn boost | confirmed (code) |
| `0x002fd128` | `Brain_IsKind6Or7` | helper | the brain's class kind `+0x11b` is 6 or 7 | confirmed (code) |
| `0x002fd158` | `MoveMeleeAction_PickSide` | `MoveMelee` helper | which way to circle (2 or 6), or 8 for straight in, from free sides round him (`0x0029e9c8`, `0x0029ea48`) and whether he is grabbed from the rear | confirmed (code) |
| `0x002fd4e8` | `MoveMeleeAction_Update` | `MoveMelee` Update | keeps between the band's near and far distances: steps back (−10) or in (+10) over 10 updates, circles to the side picked, a turn boost when closing on a target facing away; done in band | confirmed (code) |
| `0x002fda38` | `DiveAction_Init` | `Dive` init | a point | confirmed (code) |
| `0x002fda68` | `DiveAction_Start` | `Dive` Start | drops his target, faces the point, plays dive clip 2 (`0x0025a3e0`) | confirmed (code) |
| `0x002fdae8` | `DiveAction_Abort` | `Dive` Abort | refused while `0x5c0221f` is held | confirmed (code) |
| `0x002fdb28` | `DiveAction_Update` | `Dive` Update | gait 4 while `0x5c0221f` is held | confirmed (code) |
| `0x002fdb90` | `Action_TurnToHeading` | binding worker | a `TurnToDir` action | confirmed (code) |
| `0x002fdc28` | `TurnAction_Init` | `TurnToDir` init | heading, turn rate | confirmed (code) |
| `0x002fdc68` | `TurnAction_Start` | turn Start | done while `0x1c16a40` is held; else a 3 s limit | confirmed (code) |
| `0x002fdcc8` | `TurnAction_Abort` | turn Abort | marks it aborted; refused while turning in place (`0x00228500`) | confirmed (code) |
| `0x002fdd08` | `TurnAction_Update` | turn Update | sets brain `+0x110` to the heading; done within 15° (0.2618 rad) when not turning, at the limit, or aborted | confirmed (code) |
| `0x002fdde8` | `Action_Turn` | binding worker | a `Turn` action | confirmed (code) |
| `0x002fde78` | `TurnByAction_Init` | `Turn` init | n eighths of a turn | confirmed (code) |
| `0x002fdec8` | `TurnByAction_Start` | `Turn` Start | heading = brain `+0x110` + n × 45° | confirmed (code) |
| `0x002fdf50` | `TurnByAction_Update` | `Turn` Update | the turn Update | confirmed (code) |
| `0x002fdf70` | `Action_TurnToTarget` | binding worker | a `TurnTo` action | confirmed (code) |
| `0x002fe000` | `TurnToPointAction_Init` | `TurnTo` init | a point, turn rate | confirmed (code) |
| `0x002fe058` | `TurnToPointAction_Begin` | `TurnTo` Start | heading to the point, then the turn Start | confirmed (code) |
| `0x002fe0a8` | `TurnToPointAction_Update` | `TurnTo` Update | the turn Update | confirmed (code) |
| `0x002fe0c8` | `Action_LookAt` | binding worker | a `LookAt` action | confirmed (code) |
| `0x002fe160` | `LookAtAction_Init` | `LookAt` init | the human's handle | confirmed (code) |
| `0x002fe1b0` | `LookAtAction_Update` | `LookAt` Update | the heading to the human, taken each update ([Look at](ai.md#actions)) | confirmed (code) |
| `0x002fe248` | `LookAroundAction_Init` | `LookAround` init | 2000 ms, 110° | confirmed (code) |
| `0x002fe280` | `LookAroundAction_Start` | `LookAround` Start | deadline = clip `0x29e`'s length; field of view 4.71 rad and sight range `0x14d` while it plays (old values saved) | confirmed (code) |
| `0x002fe320` | `LookAroundAction_Abort` | `LookAround` Abort | restores the field of view and range | confirmed (code) |
| `0x002fe380` | `TakeStepAction_Init` | `TakeStep` init | a heading | confirmed (code) |
| `0x002fe3a8` | `TakeStepAction_Start` | `TakeStep` Start | brain heading, starts the step (`0x002432b0`) | confirmed (code) |
| `0x002fe3d8` | `TakeStepAction_Abort` | `TakeStep` Abort | ends the step; refused while stepping (`0x00228448`) | confirmed (code) |
| `0x002fe428` | `TakeStepAction_Update` | `TakeStep` Update | waits while the step's locomotion handler (`0x00243420`) runs | confirmed (code) |
| `0x002fe4b0` | `Action_GiveWay` | binding worker | a `GiveWay` action for a human with fewer than 8 actions | confirmed (code) |
| `0x002fe568` | `GiveWayAction_Init` | `GiveWay` init | a step, the human, a boost flag; marks the brain (`+0xcc` bit 1) | confirmed (code) |
| `0x002fe5d8` | `GiveWayAction_Start` | `GiveWay` Start | looks at him for 2 s, optional turn boost, then the step | confirmed (code) |
| `0x002fe658` | `GiveWayAction_Abort` | `GiveWay` Abort | the step's Abort, the boost restored | confirmed (code) |
| `0x002fe6c8` | `GiveWayAction_Update` | `GiveWay` Update | the step's Update | confirmed (code) |
| `0x002fe6e8` | `MoveMeleeLineAction_Init` | `MoveMeleeLine` init | the human, a point | confirmed (code) |
| `0x002fe720` | `MoveMeleeLineAction_Start` | `MoveMeleeLine` Start | a 2 s limit | confirmed (code) |
| `0x002fe740` | `MoveMeleeLineAction_Update` | `MoveMeleeLine` Update | sidesteps over 6 updates to the side the point lies on, stopping when that side's slots are blocked; gait 2 or the human's | confirmed (code) |
| `0x002feaf8` | `ShuffleAction_Init` | `Shuffle` init | the human, a duration | confirmed (code) |
| `0x002feb48` | `ShuffleAction_Update` | `Shuffle` Update | in the fight stance, shuffles 20 updates to a random side (51 % right), stopping at a blocked side; done at the deadline | confirmed (code) |
| `0x002fee38` | `Objects_GetDistance` | binding worker | the distance between two objects | inferred |
| `0x002feeb0` | `Peds_SetGlobalRules` | binding worker | stores twelve pedestrian rule values (`0x006ea188`-`0x006ea19e`), every third squared (distances) | confirmed (code) |

## Brain types {#brain-types}

The think and event handlers per brain type ([Types](ai.md#types) has the table and what each does).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002fef40` | `CivilianBrain_Think` | type 4 think | civilians' think | confirmed (code) at the table `0x00715568` |
| `0x002ff198` | `CivilianBrain_CanReactToCrime` | type 4 helper | alive and free, top goal not one of `0x6b`, 2, `0x6d`, `0x6e`, `0x5d`, `0x11`, `0x50`, `0x8c`, no fight goal and no gang tactic (goal `0x4f` asks its own test) | confirmed (code) |
| `0x002ff2b0` | `CivilianBrain_OnCrimeSeen` | type 4 crime witness | under goal `0x6d` the goal handles it; else, if he may react and sees the offender within his sight range, he may phone the police (a car or store crime, game state `+0x2a1`, a chance `0x00510adf`, 100 % for a car of state 3) or flee | confirmed (code) for the tests; the phoning inferred |
| `0x002ffb30` | `CivilianBrain_OnEvent` | type 4 event | civilians' events | confirmed (code) at the table `0x007155a0` |
| `0x00300678` | `CopBrain_Think` | type 1 think | cops' think | confirmed (code) at the table `0x00715568` |
| `0x00301248` | `CopBrain_ChaseTarget` | type 1 helper | unless goals `0x78`, `0x79` or `0x3f` run: a target in sight and not of the player's gang (or of it) gets a fight goal and an arrest goal of kind 12 (`0x002c4060`) | confirmed (code) |
| `0x00301538` | `CopBrain_OnCrimeSeen` | type 1 crime witness | a cop who sees the offender picks the response by what was hit (a car or store: 13, a near human: 2, else 5; 12 when crime 12 is on) and, unless the offender's gang is not wanted (`+0x5e8`), fights and arrests him; a crime against himself makes him an enemy | confirmed (code) |
| `0x00302038` | `CopBrain_CanRespond` | type 1 helper | police on (game state `+0x288`), alive, top goal not `0x75`, `0x76`, `0x73`, 1, `0x5e`, `0x47` ... | confirmed (code) |
| `0x00302118` | `CopBrain_CountBackup` | type 1 helper | how many more cops a call needs (a third of the suspects less those already on it), when the gang's last call (`+0x5f4`) is 30 s old | confirmed (code) |
| `0x00302340` | `CopBrain_OnEvent` | type 1 event | cops' events | confirmed (code) at the table `0x007155a0` |
| `0x00302cd8` | `DealerBrain_Think` | type 5 think | dealers' think | confirmed (code) at the table `0x00715568` |
| `0x00302df8` | `DealerBrain_CanReactToHit` | type 5 helper | [The dealer: buying](ai.md#dealer-buy) | confirmed (code) |
| `0x00302eb0` | `DealerBrain_FleeOrFight` | type 5 helper | rolls the run chance: flee or fight 15 s ([The dealer: buying](ai.md#dealer-buy)) | confirmed (code) |
| `0x00303260` | `PlayerBrain_Think` | type 0 think | the player's think | confirmed (code) at the table `0x00715568` |
| `0x00303468` | `PlayerBrain_OnPrompt` | type 0 helper | event 0 (triangle on a human): faces the human (`0x00233b08`); if he holds something and is not in shadow, he says line `0x92` | confirmed (code) |
| `0x003035d8` | `PlayerBrain_Update` | type 0 update | target upkeep, hostiles counted, the [automatic commands](ai.md#warrior-auto-commands) | confirmed (code) |
| `0x00303988` | `WarChief_AutoCommand` | type 0 helper | [Automatic commands](ai.md#warrior-auto-commands) | confirmed (code) |
| `0x00303db8` | `WarChief_IsTargetInRange` | type 0 helper | the chief's target within his far melee range and not a Warrior hitting back | confirmed (code) |
| `0x00303e68` | `Brain_HasAttackers` | helper | the attacker list (`+0x1a4`) is not empty | confirmed (code) |
| `0x00303e90` | `PlayerBrain_OnEvent` | type 0 event | event 0 → the prompt; 11, 12, 20 consumed; others to the shared handler (`0x00292d80`) | confirmed (code) |
| `0x00303f10` | `CivlCoDiBrain_Think` | type 6 think | the shopkeeper's think ([Shopkeepers](ai.md#think-shopkeeper)) | confirmed (code) at the table `0x00715568` |
| `0x00304030` | `Brain_IsFreeToFight` | type 6 helper | alive and without a fight goal | confirmed (code) |
| `0x00304228` | `CivlCoDiBrain_OnEvent` | type 6 event | the shopkeeper's events ([Shopkeepers](ai.md#think-shopkeeper)) | confirmed (code) at the table `0x007155a0` |
| `0x00304c78` | `GangBrain_OnCrimeSeen` | type 2 crime witness | a gang member who sees a crime by someone hostile to him (`0x00290138`) and has no goal `0x9a` pushes Investigate (`0x5e`, 5 m, line 13) at the offender's position, or ReactNoise (`0x99`) under a Mark goal ([Gang soldiers](ai.md#think-gang)) | confirmed (code) |
| `0x00304fa8` | `GangBrain_OnEvent` | type 2 event | gang members' events | confirmed (code) at the table `0x007155a0` |
| `0x003052f0` | `WarriorBrain_Think` | type 3 think | [The Warriors' pick-ups](ai.md#warrior-pickups) and the fight push | confirmed (code) |
| `0x00306040` | `WarriorBrain_OnPrompt` | type 3 helper | triangle on a Warrior: when both are free and in reach (`0x0021c0a8`), faces him; line `0x92` as the player's | confirmed (code) |
| `0x00306190` | `WarriorBrain_OnHitByChief` | type 3 helper | a Warrior hit by his chief hits back with an `AttackTarget` goal (type 9, `0x002aee48`), counted in the chief's brain `+0x2e4` | confirmed (code) |
| `0x003063b0` | `WarriorBrain_OnEvent` | type 3 event | Warriors' events | confirmed (code) at the table `0x007155a0` |

## Tactics (0x00306520-0x003150c8) {#tactics-code}

The tactic classes: one section per class with its binding, constructor, Start, End, Process, event slot and helpers.
The tactic model (pool, base fields, vtable slots, event codes) is in [Tactics](ai.md#tactics); what each scripted
tactic does is in [The scripted tactics](ai.md#tactic-kinds). Event numbers are the human events of [Script
events](../references/script-events.md); codes are `TacticGetString`'s.

### The tactic core {#t1-tactic-core}

Pool and base-class helpers shared by every tactic; the base fields, the vtable slots and `Tactic_Start` /
`Tactic_Process` are in [Tactics](ai.md#tactics). A tactic may own a coordinated **strategy** object at `+0x10` (a pool
of 8 × 0x30 at `0x006eb6f0`, mask `0x006eb6e0`), which only the Attack tactic starts.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00306520` | `Tactic_ResetFreeQueue` | helper | clears the tactic pool mask `0x006ea1a0` and the 32-entry queue of tactics waiting to be freed (`0x006eb290`); at boot (`Game_InitializeSubsystems`) | confirmed (code) |
| `0x003065f8` | `Tactic_QueueFree` | helper | puts a tactic in the first empty slot of the free queue (from `Gang_StopTactic`); `0x00306630` frees them after the gang update | confirmed (code) |
| `0x00306850` | `Tactic_End` | base | ends the strategy (`0x00306bd0`), pops every non-player member (`+0x1b0` = −1) down to his goal base, then calls the class's End (slot `+0x14`); from `Gang_StopTactic`, `Gang_Destroy` and the old boss tactic | confirmed (code) |
| `0x00306938` | `Tactic_FireCallback` | base | slot `+0x54`: when the tactic has a Lua callback (`+0x0c`), looks it up in the script system (`*(0x00512b04)`) and calls it with the gang id (gang `+0x30`) and the code; the codes are `TacticGetString`'s | confirmed (code) |
| `0x003069f0` | `Tactic_SayAckLine` | helper | with two or more living members, one free non-player member other than the leader (the last eligible one, or at 50 % a later one) says a speech command (`Human_SayCommand`): Defend `0x82`, Follow `0x83`, hold `0x85` (inferred: the command acknowledgement) | confirmed (code) |
| `0x00306b50` | `Tactic_SetStrategy` | helper | ends any current strategy and stores the new one at `+0x10` (from `AttackTactic_StartSubTactic`) | confirmed (code) |
| `0x00306b88` | `Tactic_UpdateStrategy` | helper | each `Tactic_Process`: updates the strategy (`0x00321a00`, done once its time `+4` passed or its own check says so) and ends it when done | confirmed (code) |
| `0x00306bd0` | `Tactic_EndStrategy` | helper | ends the strategy (its slot `+0x14`), tells the tactic (slot `+0x1c`, `AttackTactic_OnStrategyEnd` for Attack), frees it (`0x00321890`) and clears `+0x10` | confirmed (code) |
| `0x00306c28` | `Tactic_ResetPoolOnAll` | helper | clears the tactic pool mask when called with (non-zero, `0xffff`) | confirmed (code) |
| `0x00306c48` | `Tactic_ResetPool` | helper | `Tactic_ResetPoolOnAll(1, 0xffff)`; no caller found | inferred |

### Address (type 0x1a) {#t1-address}

`TacticAddress` (vtable `0x005432c0`): the gang's leader walks up to another gang's leader and the two play a paired
scene. No script calls it.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00306c68` | `AddressTactic_Init` | init | the other gang `+0x20`, `approach` `+0x24`, `range` `+0x28`, the options `+0x2c` and `+0x40`, `target` `+0x30`; the scene found by name (`SceneList_FindBySubstring(name1)`) at `+0x3c`; state `+0x34` 0; first check 1 s on | confirmed (code) |
| `0x00306dd0` | `AddressTactic_PushApproach` | helper | pushes an AddressPerson goal (`AddressPersonGoal_Init`, approach, range, the scene) on the leader toward the other leader; with option `+0x2c` = 1 sets the leader's `+0x1b2` | confirmed (code) |
| `0x00306ec0` | `AddressTactic_PlayScene` | helper | once the scene is loaded (state 2): both leaders made invulnerable (`Brain_SetDead(…, 1)`, old values saved), both join the scene (`Goal_JoinAnimation`, roles by `+0x40`), the scene plays and commands lock (game `+0x411` = 1); state 1 | confirmed (code) |
| `0x00307038` | `AddressTactic_Start` | Start | `AddressTactic_PushApproach` | confirmed (code) |
| `0x00307058` | `AddressTactic_End` | End | restores the two leaders' saved flags, pops a join goal (`0x2a`) still on top, unlocks commands and frees or stops the scene | confirmed (code) |
| `0x00307248` | `AddressTactic_Process` | Process | 0 with no leaders; when the scene ended (state 1, top goal no longer `0x2a`): restore, state 2, code 15 `TacAnimDone`; every 1 s within `range`: code 10 `TacError` while game `+0x322` > 0, else requests the scene and, once loaded, within `approach` and with option `+0x2c` 0, plays it: code 18 `TacAnimStart` | confirmed (code) |
| `0x00307460` | `AddressTactic_OnEvent` | Event | event 0 from the leader (inferred: his AddressPerson goal arrived) plays the scene → 18; 1 → 5; 16 → 6; 20 consumed; 22 re-pushes the approach | confirmed (code) |

### Attack (type 0x00) {#t1-attack}

`TacticAttack` (vtable `0x00543320`; constructor `AttackTactic_Construct` `0x003075c8`, Start `0x00307fe0`, Process
`0x003081a8`, both outside this list): every member melees ([Scripted tactics](ai.md#tactic-kinds)). Start is
`AttackTactic_Start` `0x00307fe0`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00307548` | `Tactic_Attack` | binding | `Tactic_Attack`: finds the gang, takes a tactic, constructs it with no time limit and sets it | confirmed (code) |
| `0x00307668` | `AttackTactic_ComputeSpacing` | helper | from Start: takes the first member's enemy; none → `+0x2c` = 300; else `+0x30` = 1000 (50 when that enemy's `+0x1a8` > 2) and `+0x2c` = `+0x30` / living members (inferred: a staggering interval; its reader was not found) | inferred |
| `0x00307788` | `AttackTactic_GiveMelee` | helper | each standing member without goal `0x6b`: threat response 2 (brain `+0x21c`), goals popped to the base and marked, `Goal_Melee` | confirmed (code) |
| `0x00307878` | `AttackTactic_OnViolence` | helper | event 20: the leader picks a side in the fight (`Brain_PickSideInFight`); each standing member with no goal melees that human, adds him as enemy and target, and sends a helper (`Gang_SendHelper`) when HelpRespond (`0x1d`) is not on his stack | confirmed (code) |
| `0x00307d40` | `GangTactic_CheckFlee` | helper | `GangTactic_CheckFlee`, event 18 (a member down): with the gang's flee percentage (`+0xe0`) and `+0xdf` set (not for kind 6 with `+0xd9`), when the standing, uncuffed members number at most that percentage of the kind's size (`GangConfig_GetField72`), each of them whose class byte `+0x11b` is 11 gets threat response 0, melee and a PedReaction (type 9) away from his enemy; sets `+0x32` (the gang fled) | confirmed (code) |
| `0x003080c8` | `AttackTactic_End` | End | releases the gang's substitute group `0x29c` when the flee option is set (or kind 6 with `+0xd9`) | confirmed (code) |
| `0x00308150` | `AttackTactic_OnStrategyEnd` | slot +0x1c | when the ended strategy's slot `+0x2c` answers 1, gives the members melee again | confirmed (code) |
| `0x00308478` | `AttackTactic_OnEvent` | Event | 1/11 for an own member with no fight goal and no strategy: melee; 2 → 13 `TacMemberDied` unless the gang fled; 18 → `GangTactic_CheckFlee`; 19/22 → melee again; 20 → `AttackTactic_OnViolence` | confirmed (code) |

### AvoidEnemies (type 0x20) {#t1-avoid-enemies}

`TacticAvoidEnemies` (vtable `0x00543380`): every member keeps away from enemies.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003085d8` | `AvoidEnemiesTactic_Init` | init | stores the binding's values (`+0x20`-`+0x34`) for the goals | confirmed (code) |
| `0x00308698` | `AvoidEnemiesTactic_GiveGoals` | helper | each standing non-player member: goals popped and marked, `Goal_AvoidEnemies` with the stored values | confirmed (code) |
| `0x00308790` | `AvoidEnemiesTactic_Start` | Start | `AvoidEnemiesTactic_GiveGoals` | confirmed (code) |
| `0x003087c0` | `AvoidEnemiesTactic_OnEvent` | Event | 1 → 5; 16 → 6; 19/22 give the goals again; 20 consumed | confirmed (code) |

### Boss (type 0x09) {#t1-boss}

`TacticBoss(gang, boss, stage, callback)` (vtable `0x005433e0`): an older single tactic for six bosses, chosen by
`+0x24` (0 Diego, 1 Vargas, 2 Chatterbox, 3 Luther, 4 the Lizzies, 5 Big Mo). No script calls it; the
`TacticBossScenarioA`-`H` tactics below replaced it, so its boss goals (types `0x93`-`0x96`) never run in the game.
Which tactic each level uses: [AI: The boss fights](ai.md#boss-fights).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00308870` | `Tactic_Boss` | binding | `Tactic_Boss`: stops the gang's current tactic, constructs and sets the new one | confirmed (code) |
| `0x00308918` | `BossTactic_Init` | init | boss index `+0x24`, stage `+0x25`; clears the timers `+0x20`, `+0x2c` and the counters `+0x26`, `+0x30` | confirmed (code) |
| `0x003089b0` | `BossTactic_Start` | Start | gives the first member his boss goal: Diego `Goal_BossDiego(0, fBoss_01, fBoss_03, fBoss_09)`, Vargas `Goal_BossDiego(1, fBoss_05, fBoss_03, fBoss_03)` (each remembered in `0x006e9408` / `0x006e9404` with a HUD counter at 100), Chatterbox `BossChatterGoal_Push(stage)`, Luther `BossLutherGoal_Push`, the Lizzies `Goal_BossLizzies(stage)`, Big Mo `BossBigMoGoal_Push` | confirmed (code) |
| `0x00308c60` | `BossTactic_Process` | Process | only for the Lizzies (index 4): from the boss's goal `0x94`, casts a 25 m ray toward the goal's aim point and puts the spinning `dyn_lizziestarget` icon on the object it hits when that object has flag `0x40` and player 1 is inside the goal's volume box (`+0x30`); removes it otherwise; returns 0 | confirmed (code) |
| `0x00309058` | `BossTactic_OnEvent` | Event | 2 (a boss down): Diego or Vargas gives the other one pocket item 9 and sets the inventory callback `GotDiagosKey` once, and zeroes his HUD counter; 1 (damage): updates the boss's HUD counter, Diego's goal clip `+0x24` becomes `0xc3` below 80 % and `0xc4` below 40 %; Chatterbox's three stages run Lua (`C2.ChatterUpdateHUD`, `ShakeBalcony2`/`3`), send him to `fBalcony2d` below 50 %, cycle his 13 lines every 3 s and, at the end, call the tactic's callback and end it; the Lizzies pop to goal `0x94` and set its state 7 for 1 s | confirmed (code) |

### BossDiegoVargas (type 0x0a) {#t1-boss-diego-vargas}

`TacticBossScenarioA` (vtable `0x00543440`); the fight is described in [The Diego and Vargas
fight](ai.md#boss-diego-vargas).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00309a40` | `Tactic_BossDiegoVargas` | binding | `Tactic_BossDiegoVargas`: needs a valid gang with members; constructs and sets the tactic | confirmed (code) |
| `0x00309b60` | `BossDiegoVargasTactic_Init` | init | `BossDiegoVargasTactic_Init`: flags, stage `+0x68`, break count `+0x69` = 0, the two object-handle pairs, the per-boss tables (fatigue, damage, prone, cycles) | confirmed (code) |
| `0x00309df8` | `BossDiegoVargasTactic_GiveGoals` | helper | each member neither knocked out nor cuffed: `BossDiegoVargasTactic_AssignGoal` | confirmed (code) |
| `0x00309ea0` | `BossDiegoVargasTactic_AssignGoal` | helper | `BossDiegoVargasTactic_AssignGoal`: Vargas (class `0x78`) BigBrawler, Diego (`0x77`) BigThrower in stage 2 or BigBrawler in stage 3, the others StationaryThrower; writes the stage into a BigBrawler goal | confirmed (code) |
| `0x0030a150` | `BossDiegoVargasTactic_CheckHealth` | helper | `BossDiegoVargasTactic_CheckHealth`: the health caps and the Tired break ([the fight](ai.md#boss-diego-vargas)); 1 when a break starts with a held object, 2 when the break is done | confirmed (code) |
| `0x0030a458` | `BossDiegoVargasTactic_IsAnyBossStanding` | helper | `BossDiegoVargasTactic_IsAnyBossStanding`: stage 3 only, 1 while Diego or Vargas is not out (state flag `0x100000000`); 1 in other stages | confirmed (code) |
| `0x0030a4f8` | `BossDiegoVargasTactic_ForwardAttackWarning` | helper | `BossDiegoVargasTactic_ForwardAttackWarning`: event 16 to the member's BigBrawler goal | confirmed (code) |
| `0x0030a580` | `BossDiegoVargasTactic_ForwardHit` | helper | `BossDiegoVargasTactic_ForwardHit`: event 1 to the member's BigBrawler goal | confirmed (code) |
| `0x0030a608` | `BossDiegoVargasTactic_PruneObjects` | helper | `BossDiegoVargasTactic_PruneObjects`: clears thrown-object handles (`+0x28`, two) whose objects are gone | confirmed (code) |
| `0x0030a680` | `BossDiegoVargasTactic_HasFreeObjectSlot` | helper | `BossDiegoVargasTactic_HasFreeObjectSlot`: 1 when either handle is empty | confirmed (code) |
| `0x0030a6c0` | `BossDiegoVargasTactic_Start` | Start | `BossDiegoVargasTactic_Start`: goals, removes the gang's combat fidget and taunt clips, substitutes group `0x29c` | confirmed (code) |
| `0x0030a710` | `BossDiegoVargasTactic_End` | End | releases the substitute group `0x29c` | confirmed (code) |
| `0x0030a738` | `BossDiegoVargasTactic_Process` | Process | `BossDiegoVargasTactic_Process`: prunes the objects; health check 1 → code 18 `TacAnimStart`, 2 → 1 `TacFinished`; else 1 once no boss stands | confirmed (code) |
| `0x0030a798` | `BossDiegoVargasTactic_OnEvent` | Event | `BossDiegoVargasTactic_OnEvent`: 1 → forward hit (consumed); 16 → forward warning (consumed); 20 consumed; 22 with `+8` ≠ 1 and `+4` = 0 → code 1; 19 and 22 with `+8` = 1 give that member his goal again | confirmed (code) |

### BossLizzies (type 0x0b) {#t1-boss-lizzies}

`TacticBossScenarioB` (vtable `0x005434a0`): the Lizzies' shooter (class `0xed`) fires from a fixed post while every
player wears a target icon. The tactic never ends by itself.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0030a878` | `Tactic_BossLizzies` | binding | `Tactic_BossLizzies`: needs a valid gang with members; constructs and sets the tactic | confirmed (code) |
| `0x0030a9a0` | `BossLizziesTactic_Init` | init | stores the shot parameters (`maxBullets`, ranges, spreads, rates, damage, timings) in `+0x20`-`+0x37` for the goal | confirmed (code) |
| `0x0030aab0` | `BossLizziesTactic_GiveGoals` | helper | each member neither knocked out nor cuffed: `BossLizziesTactic_AssignGoal` | confirmed (code) |
| `0x0030ab58` | `BossLizziesTactic_AssignGoal` | helper | goals popped and marked; a class-`0xed` member gets a StationaryShooter goal (type `0x8d`) built from the stored parameters; the others keep none | confirmed (code) |
| `0x0030ac10` | `BossLizziesTactic_SetPlayerIcons` | helper | shows or removes the target icon on every player (`0x0030aca8`) | confirmed (code) |
| `0x0030aca8` | `BossLizziesTactic_SetPlayerIcon` | helper | on: shows the overhead icon `dyn_lizziestarget` on the player when he has none; off: removes it | confirmed (code) |
| `0x0030ad20` | `BossLizziesTactic_Start` | Start | goals; removes the gang's combat fidget and taunt clips | confirmed (code) |
| `0x0030ad58` | `BossLizziesTactic_End` | End | removes the icons | confirmed (code) |
| `0x0030ad78` | `BossLizziesTactic_Process` | Process | the icons are on while commands are not locked (`+0x411`), no scene plays (`+0x410`) and player 1's camera mode is not `0xc`; always 0 | confirmed (code) |
| `0x0030ae10` | `BossLizziesTactic_OnEvent` | Event | 1 and 20 consumed; 19/22 give the member his goal again | confirmed (code) |

### BossMoe (type 0x0c) {#t1-boss-moe}

`TacticBossScenarioC(gang, stage, callback)` (vtable `0x00543500`): Big Mo (class `0x80`) as a BigFighter, his men with
maces. Stage `+0x20` (1-3); `+0x21` marks the last stage done.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0030ae98` | `Tactic_BossMoe` | binding | `Tactic_BossMoe`: needs a valid gang with members; constructs and sets the tactic | confirmed (code) |
| `0x0030af30` | `BossMoeTactic_Init` | init | `BossMoeTactic_Init`: stage `+0x20`, `+0x21` = 0 | confirmed (code) |
| `0x0030afa8` | `BossMoeTactic_GiveGoals` | helper | each member neither knocked out nor cuffed: `BossMoeTactic_AssignGoal` | confirmed (code) |
| `0x0030b050` | `BossMoeTactic_AssignGoal` | helper | `BossMoeTactic_AssignGoal`: Big Mo gets a BigFighter goal (`0x85`) and voice `0x8c` (`+0x3b0`), the others a Mace goal (`0x91`); writes the stage into the BigFighter goal (`0x002ea038`) | confirmed (code) |
| `0x0030b168` | `BossMoeTactic_CheckStage` | helper | `BossMoeTactic_CheckStage`: stage 1 at 75 % or less, stage 2 at 50 %: unless the BigFighter goal is in state 1-2, starts its break (`0x002ea6f0`), caps the health (75 / 50) and returns 1; once the break is over (`0x002ea8f8`, `0x002ea840`) moves to the next stage and returns 2; stage 3 returns 3 (sets `+0x21`) when the goal reports done (`0x002ea9f0`) | confirmed (code) |
| `0x0030b3b8` | `BossMoeTactic_IsMoStanding` | helper | stage 3 only: 1 while Big Mo is not out (flag `0x100000000`); 1 in other stages | confirmed (code) |
| `0x0030b458` | `BossMoeTactic_SetStage` | helper | stores the stage and writes it into every member's BigFighter goal | confirmed (code) |
| `0x0030b520` | `BossMoeTactic_FireAnimDone` | helper | fires code 15 `TacAnimDone` (from `Brain_DefaultOnEvent`) | confirmed (code) |
| `0x0030b550` | `BossMoeTactic_OnMoDown` | helper | fires code 18 when the human is Big Mo (from `Brain_OnFoeDown`) | confirmed (code) |
| `0x0030b590` | `BossMoeTactic_Start` | Start | `BossMoeTactic_Start`: goals; removes the gang's combat fidget and taunt clips | confirmed (code) |
| `0x0030b5d0` | `BossMoeTactic_Process` | Process | `BossMoeTactic_Process`: check 1 → 18 `TacAnimStart`, 2 → 1 `TacFinished`, 3 → 8 `TacArrived`; else 1 once Big Mo is out | confirmed (code) |
| `0x0030b638` | `BossMoeTactic_OnEvent` | Event | `BossMoeTactic_OnEvent`: 20 consumed; 22 with `+8` ≠ 1 and `+4` = 0 → 1; 19 and 22 with `+8` = 1 give the member his goal again | confirmed (code) |

### BossRoof (type 0x0d) {#t1-boss-roof}

`TacticBossScenarioD(gang, stage, callback)` (vtable `0x00543560`): two bosses fight as a pair (classes `0xa0` and
`0xa1`); each changes goal when the other goes down, and they trade lines every 7.5 s.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0030b6f8` | `Tactic_BossRoof` | binding | `Tactic_BossRoof`: needs a valid gang with members; constructs and sets the tactic | confirmed (code) |
| `0x0030b790` | `BossRoofTactic_Init` | init | `BossRoofTactic_Init`: stage `+0x24`, banter timer `+0x20` and state `+0x26` cleared | confirmed (code) |
| `0x0030b810` | `BossRoofTactic_GiveGoals` | helper | each member neither knocked out nor cuffed: `BossRoofTactic_AssignGoal` | confirmed (code) |
| `0x0030b8b8` | `BossRoofTactic_IsClassStanding` | helper | 1 when a member of the given class is neither down nor dead | confirmed (code) |
| `0x0030b948` | `BossRoofTactic_AssignGoal` | helper | `BossRoofTactic_AssignGoal`: `0xa0` gets a Grabber goal (`0x92`) while `0xa1` stands, else AvoidEnemies (3-4 m); `0xa1` gets BigDefender (`0x88`) while `0xa0` stands, else BigBull (`0x89`) | confirmed (code) |
| `0x0030bad0` | `BossRoofTactic_CheckStage` | helper | walks the members and returns 0: an empty stage check (the Process's 1/2/3 cases never happen) | confirmed (code) |
| `0x0030bb28` | `BossRoofTactic_IsAnyStanding` | helper | 1 while either boss is not out (flag `0x100000000`) | confirmed (code) |
| `0x0030bbb8` | `BossRoofTactic_FindStanding` | helper | the first member of a class that is neither down nor dead | confirmed (code) |
| `0x0030bc60` | `BossRoofTactic_Stub` | helper | empty; called with 1 by Start and 0 by End | confirmed (code) |
| `0x0030bc68` | `BossRoofTactic_UpdateBanter` | helper | `BossRoofTactic_UpdateBanter`: 7.5 s after the last exchange, with both standing and `0xa0` free and silent, `0xa0` says command `0x90`, then `0xa1` answers with `0x90` when the first line ends | confirmed (code) |
| `0x0030be90` | `BossRoofTactic_OnPartnerDown` | helper | `BossRoofTactic_OnPartnerDown` (event 2): the other boss clears his actions, gets his new goal and is pushed a PlaySpecialIdle goal (arguments 4, 4000 ms, `0x22` and `0x48` or `0x49`) | confirmed (code) |
| `0x0030bfb8` | `BossRoofTactic_OnBossDown` | helper | fires code 18 when the human is one of the two bosses (from `Brain_OnFoeDown`) | confirmed (code) |
| `0x0030c000` | `BossRoofTactic_Start` | Start | `BossRoofTactic_Start`: goals; removes the gang's combat fidget and taunt clips | confirmed (code) |
| `0x0030c040` | `BossRoofTactic_End` | End | the stub with 0 | confirmed (code) |
| `0x0030c060` | `BossRoofTactic_Process` | Process | `BossRoofTactic_Process`: banter; 1 `TacFinished` once neither boss stands | confirmed (code) |
| `0x0030c0d0` | `BossRoofTactic_OnEvent` | Event | `BossRoofTactic_OnEvent`: 2 partner down; 16 for a boss in his Grabber or BigDefender goal: drops clip actions of kind 1-2 and targets the attacker unless he holds a weapon; 22 with `+8` ≠ 1 and `+4` = 0 → 1; 19 gives the member his goal again | confirmed (code) |

### BossLuther (type 0x0e) {#t1-boss-luther}

`TacticBossScenarioE` (vtable `0x005435c0`): Luther (class `0x4f`) shoots (Shooter goal, `0x8e`); his men melee.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0030c2a8` | `Tactic_BossLuther` | binding | `Tactic_BossLuther`: needs a valid gang with members; constructs and sets the tactic | confirmed (code) |
| `0x0030c398` | `BossLutherTactic_Init` | init | `BossLutherTactic_Init`: the shot parameters (`+0x20`-`+0x30`), stage `+0x31`, slow-motion fields `+0x28`, `+0x32`, `+0x33` cleared | confirmed (code) |
| `0x0030c468` | `BossLutherTactic_GiveGoals` | helper | each member neither knocked out nor cuffed: `BossLutherTactic_AssignGoal` | confirmed (code) |
| `0x0030c510` | `BossLutherTactic_AssignGoal` | helper | `BossLutherTactic_AssignGoal`: Luther gets a Shooter goal with the stored parameters, the others melee | confirmed (code) |
| `0x0030c5e0` | `BossLutherTactic_CheckHealth` | helper | stage 1: when Luther drops below 50 %, caps him at 50 % and returns 2 | confirmed (code) |
| `0x0030c6c8` | `BossLutherTactic_IsLutherStanding` | helper | 1 while Luther is not out (flag `0x100000000`) | confirmed (code) |
| `0x0030c758` | `BossLutherTactic_FireAnimStart` | helper | fires code 18 for Luther (from his Shooter goal, `0x002f28b8`) | confirmed (code) |
| `0x0030c798` | `BossLutherTactic_FireAnimDone` | helper | fires code 15 for Luther (from his Shooter goal); while player 1's brain `+0x09` or `+0x0a` is set, calls slot `+0x6c` of Luther's top Shooter goal with 0 | confirmed (code) |
| `0x0030c888` | `BossLutherTactic_FireObjectsThrown` | helper | fires code 14 for Luther (from `Brain_NotifyTacticE`) | confirmed (code) |
| `0x0030c970` | `BossLutherTactic_Start` | Start | `BossLutherTactic_Start`: goals; removes the gang's combat fidget and taunt clips | confirmed (code) |
| `0x0030c9c0` | `BossLutherTactic_Process` | Process | `BossLutherTactic_Process`: slow motion (`BossLutherTactic_UpdateSlowMotion`); health check 2 → 1 `TacFinished`; else 1 once Luther is out | confirmed (code) |
| `0x0030ca30` | `BossLutherTactic_OnEvent` | Event | `BossLutherTactic_OnEvent`: 1 and 16 for Luther go to his Shooter goal (`0x002f2438`, `0x002f2398`); 20 consumed; 22 with `+8` ≠ 1 and `+4` = 0 → 1; 19 and 22 with `+8` = 1 give the member his goal again | confirmed (code) |

### BossBirdie (type 0x0f) {#t1-boss-birdie}

`TacticBossScenarioF` (vtable `0x00543620`): Birdie (class `0x62`) shoots from up to three positions
(StationaryShooterB, `0x8f`); his men melee; the Warriors shout at him.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0030cbd8` | `Tactic_BossBirdie` | binding | `Tactic_BossBirdie`: needs a valid gang with members; constructs and sets the tactic | confirmed (code) |
| `0x0030ccd8` | `BossBirdieTactic_Init` | init | three position handles `+0x20`, shot parameters `+0x2c`-`+0x3c`, stage `+0x3d`, hit counter `+0x3e` and timer `+0x34` cleared | confirmed (code) |
| `0x0030ce10` | `BossBirdieTactic_GiveGoals` | helper | each member neither knocked out nor cuffed: `BossBirdieTactic_AssignGoal` | confirmed (code) |
| `0x0030ceb8` | `BossBirdieTactic_AssignGoal` | helper | Birdie: voice `0x8b`, a StationaryShooterB goal with the positions and parameters; the others melee | confirmed (code) |
| `0x0030cf98` | `BossBirdieTactic_CheckHealth` | helper | stage 1: at 50 % or less caps Birdie at 50 % and returns 2 | confirmed (code) |
| `0x0030d078` | `BossBirdieTactic_IsBirdieStanding` | helper | 1 while Birdie is not out (flag `0x100000000`) | confirmed (code) |
| `0x0030d108` | `BossBirdieTactic_FireAnimDone` | helper | fires code 15 for Birdie (from his goal, `0x002f52e8`) | confirmed (code) |
| `0x0030d148` | `BossBirdieTactic_FireTimeOut` | helper | fires code 2 `TacTimeOut` for Birdie (from his goal) | confirmed (code) |
| `0x0030d188` | `BossBirdieTactic_FireArrived` | helper | fires code 8 `TacArrived` for Birdie (from his goal) | confirmed (code) |
| `0x0030d1c8` | `BossBirdieTactic_Start` | Start | goals; removes the gang's combat fidget and taunt clips | confirmed (code) |
| `0x0030d208` | `BossBirdieTactic_Process` | Process | health check 2 → 1 `TacFinished`; while Birdie stands, every 8 s (unless player 1's game byte `+0x41a` is set) one non-player member of player 1's gang shouts a voice line (set `0x11`, variant 1, 4 or 7, volume 4.0); 1 once Birdie is out | confirmed (code) |
| `0x0030d3b8` | `BossBirdieTactic_OnEvent` | Event | 1 on Birdie (not during a scene): unless he plays anim `0x299`, he stops talking and says command `0x22`; stage 1 at 50 % or less: capped, flags `0x810`, stunned; stage 2 at 25 % or less: his goal is set to mode 4 (`0x002f52b0`), flag `0x10`, 25 %, code 18, and once the goal reports done (`0x002f52c8`) a player's hit clears the flag and fires 1; otherwise code 5, for a player's hits only every second one; 18 → 1; 20 consumed; 22 with `+8` ≠ 1 and `+4` = 0 → 1; 19/22 give the goal again | confirmed (code) |

### BossVirgil (type 0x10) {#t1-boss-virgil}

`TacticBossScenarioG(gang, stage, flag, callback)` (vtable `0x00543680`): Virgil (class `0xb2`) throws from a ledge
(stage 1), plays hide and seek (stage 2), then fights (BigFighter).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0030d6f8` | `Tactic_BossVirgil` | binding | `Tactic_BossVirgil`: needs a valid gang with members; constructs and sets the tactic | confirmed (code) |
| `0x0030d7a0` | `BossVirgilTactic_Init` | init | `BossVirgilTactic_Init`: the flag `+0x20`, stage `+0x24` | confirmed (code) |
| `0x0030d820` | `BossVirgilTactic_Start` | helper | `BossVirgilTactic_Start` (the goal pass the Start runs): each member neither knocked out nor cuffed gets his goal; Virgil, when the flag exists, gets a `dyn_molotv` in hand if empty and throws it at the flag (`Goal_ThrowObject`, 20 m) | confirmed (code) |
| `0x0030d960` | `BossVirgilTactic_AssignGoal` | helper | `BossVirgilTactic_AssignGoal`: Virgil: voice `0x8a`; stage 1 BigLedgeThrower (`dyn_molotv`, `dyn_beerbottle`), stage 2 HideAndSeek (`0x9c`), else BigFighter (`0x85`); the others melee | confirmed (code) |
| `0x0030daf8` | `BossVirgilTactic_CheckHealth` | helper | `BossVirgilTactic_CheckHealth`: stage 1 at 83 % or less, stage 2 at 73 %: capped there, returns 2 | confirmed (code) |
| `0x0030dc28` | `BossVirgilTactic_IsVirgilAlive` | helper | `BossVirgilTactic_IsVirgilAlive`: 1 while Virgil is neither down nor dead | confirmed (code) |
| `0x0030dcb8` | `BossVirgilTactic_FireAnimStart` | helper | fires code 18 (from the HideAndSeek goal, `0x002f8468`) | confirmed (code) |
| `0x0030dce8` | `BossVirgilTactic_StartAll` | Start | the goal pass; removes the gang's combat fidget and taunt clips | confirmed (code) |
| `0x0030dd28` | `BossVirgilTactic_Process` | Process | `BossVirgilTactic_Process`: health check 2 → 1 `TacFinished`; else 1 once Virgil is down | confirmed (code) |
| `0x0030dd90` | `BossVirgilTactic_OnEvent` | Event | `BossVirgilTactic_OnEvent`: 1 and 16 on Virgil go to his HideAndSeek goal (stage 2, `0x002f8400` / `0x002f8240`) or BigFighter goal (stage 3, `0x002ea218` / `0x002ea0b8`), consumed; 18 → 1; 20 consumed; 22 with `+8` ≠ 1 and `+4` = 0 → 1; 19/22 give the goal again | confirmed (code) |

### BossChatterbox (type 0x11) {#t1-boss-chatterbox}

`TacticBossScenarioH(gang, stage, callback)` (vtable `0x005436e0`): the boss of class `0x87` throws from a ledge (stage
1), then fights (BigFighterA, stage 2).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0030e028` | `Tactic_BossChatterbox` | binding | `Tactic_BossChatterbox`: needs a valid gang with members; constructs and sets the tactic | confirmed (code) |
| `0x0030e0c0` | `BossChatterboxTactic_Init` | init | `BossChatterboxTactic_Init`: stage `+0x20` | confirmed (code) |
| `0x0030e130` | `BossChatterboxTactic_GiveGoals` | helper | each member neither knocked out nor cuffed: `BossChatterboxTactic_AssignGoal` | confirmed (code) |
| `0x0030e1d8` | `BossChatterboxTactic_AssignGoal` | helper | `BossChatterboxTactic_AssignGoal`: the boss: voice `0x89`; stage 1 BigLedgeThrower (`dyn_molotv`, `dyn_cny_skullhead_a`), stage 2 BigFighterA (`0x86`); the others melee | confirmed (code) |
| `0x0030e350` | `BossChatterboxTactic_CheckHealth` | helper | stage 1 at 75 % or less: capped at 75 %, returns 2 | confirmed (code) |
| `0x0030e438` | `BossChatterboxTactic_IsBossStanding` | helper | 1 while the boss is neither down nor dead | confirmed (code) |
| `0x0030e4c8` | `BossChatterboxTactic_FireAnimStart` | helper | fires code 18 for the boss (from his goal, `0x002ec530`) | confirmed (code) |
| `0x0030e508` | `BossChatterboxTactic_Start` | Start | goals; removes the gang's combat fidget and taunt clips | confirmed (code) |
| `0x0030e548` | `BossChatterboxTactic_Process` | Process | `BossChatterboxTactic_Process`: health check 2 → 1 `TacFinished`; else 1 once the boss is down | confirmed (code) |
| `0x0030e5b0` | `BossChatterboxTactic_OnEvent` | Event | 18 → 1; 20 consumed; 22 with `+8` ≠ 1 and `+4` = 0 → 1; 19/22 give the goal again | confirmed (code) |

### Confront (type 0x23) {#t1-confront}

`TacticConfront` (vtable `0x00543740`; constructor `ConfrontTactic_Construct` `0x0030e670`, Start `0x0030ec20`, Process
`0x0030eec0`): the two gangs face off ([Scripted tactics](ai.md#tactic-kinds)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0030e998` | `ConfrontTactic_GiveGoals` | helper | when the target gang exists and has a leader: the leader's formation (saved set, or set 3 with min(members − 1, 9) slots, shape 2, 1 m), every free non-player member joins it and gets a Confront goal (`0x3c`) against the other gang | confirmed (code) |
| `0x0030eb90` | `ConfrontTactic_ResolveTargetGang` | helper | target gang −1: takes the first member's brain target gang (`+0x264`); 1 when one is set | confirmed (code) |
| `0x0030ecd0` | `ConfrontTactic_End` | End | restores the leader's formation set, takes the members out of formation (`Gang_SetMembersInFormation(…, 0)`), releases the posture group `0x253` | confirmed (code) |
| `0x0030ed40` | `ConfrontTactic_PlayPostures` | helper | with postures on (`+0x54`): the leader, when free and empty-handed, stops taunting and plays posture `0x253` variant `+0x50` − 1 at a random rate 0.5-1.0 | confirmed (code) |
| `0x0030f408` | `ConfrontTactic_OnEvent` | Event | 1 → 5; 16 → 6; 20 consumed; 2 and 19 for an own member, 17, 18 and 22: the goals again | confirmed (code) |

### Crowd (type 0x1b) {#t1-crowd}

`TacticCrowd` (vtable `0x005437a0`): onlookers that watch or cheer; behaviour in [Tactics](ai.md#tactics).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0030f4d0` | `CrowdTactic_Init` | init | no time limit; the cheer option `+0x28`; cheer index `+0x29`, periodic switch `+0x2a` 0, turn `+0x2b` 1; timer `+0x20` 2 s on | confirmed (code) |
| `0x0030f570` | `CrowdTactic_GiveGoals` | helper | each standing member: goals popped and marked; cheering: Idle goal; watching: Spectate goal (4-6 s) | confirmed (code) |
| `0x0030f6b0` | `CrowdTactic_Start` | Start | goals; cheering: groups `0x256`, 599 and `0x29c` get the three cheer clips (old-style set when the leader's brain type is 4) and every member enters cheer mode; watching: group `0x25c` and the gang's cheer clips | confirmed (code) |
| `0x0030f7e0` | `CrowdTactic_End` | End | releases those groups; cheer mode off | confirmed (code) |
| `0x0030f868` | `CrowdTactic_SetCheerMode` | helper | on: not pushable, human flag `0x800`, brain `+0x265` 0, threat response 0, the next of three cheer idles as idle anim; off: undoes it (threat response 2) | confirmed (code) |
| `0x0030fa18` | `CrowdTactic_OnViolence` | helper | event 20 above strength 29 (cheering only): a free member picks a side, looks at a fighter for 20 s and, at min(strength, 100) %, queues a line (`0x8f`/`0x10`, or `0xb1` for the losing side) and a cheer clip (`0x256` or 599) | confirmed (code) |
| `0x0030fc48` | `TacticCrowd_React` | helper | `TacticCrowd_React(what, on)`: ([Tactics](ai.md#tactics)) | confirmed (code) |
| `0x0030fe58` | `CrowdTactic_ReactNow` | helper | `TacticCrowd_React(1, on)`, for `TacticTrigger` what 1 | confirmed (code) |
| `0x0030fe78` | `CrowdTactic_Process` | Process | every 1-2 s: watchers play `0x25c` at 51 %, cheering crowds react; every 2 s a cheering crowd sends the next member into a cheer idle; always 0 | confirmed (code) |
| `0x00310100` | `CrowdTactic_OnEvent` | Event | 16 → 6; 20 → `CrowdTactic_OnViolence` (consumed); 19 and 22 → goals again | confirmed (code) |

### Defend (type 0x02) {#t1-defend}

`TacticDefend` (vtable `0x00543800`): the gang guards one human in a formation round him and rescues him.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003101d8` | `DefendTactic_Init` | init | the human `+0x20`, the distance `+0x24`, saved formation set `+0x28`/`+0x2c` = −1, check timer `+0x30` 1.5 s on, line flags `+0x34`/`+0x35` | confirmed (code) |
| `0x00310288` | `DefendTactic_GiveGoals` | helper | every standing AI member but the human joins the human's formation; non-dogs get brain `+0x208` = `0x00511438`; dogs (class `0xdd`) AvoidEnemies (4-8 m), the others FollowAndDefend (`0x35`) round the human (in two-player, a Warrior far from player 1 defends the other player); then set 3 with min(2 × members, 9) slots, shaped and planned | confirmed (code) |
| `0x00310618` | `DefendTactic_Start` | Start | goals, idle fidget clips; notes whether the human (a player) is still talking so the line waits | confirmed (code) |
| `0x003106a0` | `DefendTactic_End` | End | restores the human's formation set, clears every member's brain `+0x208`, removes the idle fidget clips | confirmed (code) |
| `0x00310768` | `DefendTactic_Process` | Process | 11 `TacHumanToDefendDead` when the human is gone or out; says the acknowledgement `0x82` once; every 1.5 s, 9 `TacNoEnemies` when no member has an enemy | confirmed (code) |
| `0x003108a8` | `DefendTactic_Rescue` | helper | each free non-player non-dog member that claims the human (`Brain_ClaimCoopTarget`) clears his actions, pops down to his FollowAndDefend goal and pushes SaveHuman on the human | confirmed (code) |
| `0x00310a60` | `DefendTactic_OnEvent` | Event | 2 on the human → 11; 17 with `+4` = 1 and 18 on the human → rescue; 19 → goals again; 20 against the human: the offender's gang becomes an enemy unless friendly (consumed); 22 with `+8` = 1 → goals again | confirmed (code) |

### Domination (type 0x07) {#t1-domination}

`TacticDomination(gang, flag, range, callback)` (vtable `0x00543860`): every member holds a flag's radius
([HoldFlag](ai-goals.md#goal-hold-flag)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00310be0` | `DominationTactic_Init` | init | `DominationTactic_Init`: no time limit; the flag `+0x20`, the range `+0x24` | confirmed (code) |
| `0x00310c68` | `DominationTactic_AssignGoals` | helper | `DominationTactic_AssignGoals`: each standing non-player member: goals popped and marked, `Goal_HoldFlag(range, flag)` | confirmed (code) |
| `0x00310d48` | `DominationTactic_Start` | Start | the goals | confirmed (code) |
| `0x00310d78` | `DominationTactic_OnEvent` | Event | `DominationTactic_OnEvent`: 20 consumed; 19 and 22 → goals again | confirmed (code) |

### WarriorFollow (type 0x12) {#t1-warrior-follow}

The follow command's tactic (vtable `0x005438c0`), described in [The default command](ai.md#warrior-follow).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00310e90` | `WarriorFollowTactic_Init` | init | `WarriorFollowTactic_Init`: distance `+0x20`, slot-set reshuffle `+0x24` 8 s on, the 1 s check `+0x28`, idle line timer `+0x2c` | confirmed (code) |
| `0x00310f38` | `WarriorFollowTactic_GiveGoals` | helper | `WarriorFollowTactic_GiveGoals`: the chief's formation gets 9 slots; each non-player member: `FollowPlayer(distance, chief, 3)`, or in two-player FollowAndDefend on the other player when far from player 1 | confirmed (code) |
| `0x003111a8` | `WarriorFollowTactic_Start` | Start | `WarriorFollowTactic_Start`: goals, idle fidget clips; notes whether the chief is talking | confirmed (code) |
| `0x00311230` | `WarriorFollowTactic_End` | End | `WarriorFollowTactic_End`: removes the idle fidget clips | confirmed (code) |
| `0x00311250` | `WarriorFollowTactic_OnViolence` | helper | `WarriorFollowTactic_OnViolence`: a free member near a busy fighter on the side he picks looks at him 3 s and at 10 % taunts (`0x10`, or `0xc2` over a downed foe) | confirmed (code) |
| `0x00311638` | `WarriorFollowTactic_Process` | Process | `WarriorFollowTactic_Process`: 1 with no leader; says `0x83` once; every 8 s re-picks the chief's slot set; every 1 s (250 ms holding an object, 5 ms right after clearing a path, `Gang_ClearWayForLeader` 40 m): with nobody under attack and no scene, after 25 s of calm one free member says an idle dialog line and looks at the chief | confirmed (code) |
| `0x00311928` | `WarriorFollowTactic_OnEvent` | Event | `WarriorFollowTactic_OnEvent`: 19 for a member: `FollowPlayer` mode 1; 11 seen by a type-3 brain: the warning line; 20 → OnViolence (consumed); 22 with `+8` = 1 → goals again | confirmed (code) |

### HanginOut (type 0x18) {#t1-hangin-out}

`TacticHanginOut` (vtable `0x00543920`; constructor `HanginOutTactic_Construct` `0x00311ad8`, Start `0x00312348`):
members hang about a flag ([Scripted tactics](ai.md#tactic-kinds), [HangOut](ai-goals.md#goal-hang-out)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00311bd0` | `HanginOutTactic_SetupFormation` | helper | saves the leader's formation set (`+0x3c`), picks set 3 with min(members − 1, 9) slots, shape 1 at 1.75 m | confirmed (code) |
| `0x00311c78` | `HanginOutTactic_SetAwareness` | helper | on: each non-player member's view angle (brain `+0x12c`) becomes the leader's − 20° and sight range (`+0x130`) the leader's × 0.75; off: + 20° and × 4/3 | confirmed (code) |
| `0x00311d98` | `HanginOutTactic_GiveGoals` | helper | formation, then each standing non-player member: HangOut goal round the flag's position with the range and the harass option | confirmed (code) |
| `0x00311eb8` | `HanginOutTactic_MarkPairTalking` | helper | writes `+0x40` of the banter pair's HangOut goals (talking on or off) | confirmed (code) |
| `0x00311f78` | `HanginOutTactic_PickBanterPair` | helper | with banter on, no locked commands, no scene and two or more members: picks two members whose HangOut goal is idle (`+0x3c` = 0) and who hold no Investigate (`0x5e`) or HelpRespond (`0x1d`), each at 51 %; 1 when both found; off: stops their speech; next try 3 s on | confirmed (code) |
| `0x00312208` | `HanginOutTactic_UpdateBanter` | helper | the first says statement (speech command 20, line `0x4d`); when he ends the second says response (21, `0x4e`) and the gang's spot line advances; when that ends the pair is released | confirmed (code) |
| `0x00312310` | `HanginOutTactic_OnMemberLeft` | helper | ends the banter when the member who left (HangOut Suspend) was one of the pair | confirmed (code) |
| `0x003123a0` | `HanginOutTactic_End` | End | restores view and sight unless `fullAware`, restores the formation set, ends the banter, releases group `0x25b` | confirmed (code) |
| `0x00312430` | `HanginOutTactic_AllIdleLong` | slot +0x44 | 1 when every member's top goal is HangOut and has idled 5 s (`0x002ced60`) | confirmed (code) |
| `0x00312518` | `HanginOutTactic_Process` | Process | every 3 s tries for a banter pair; runs the banter; always 0 | confirmed (code) |
| `0x00312580` | `HanginOutTactic_OnEvent` | Event | 1 → 5; 10 → 4 `TacSeePlayer`; 11 for an own member: notes the spotter, 3; 16 → 6; 20 with `respond`: sends one more member to help while the share in HelpRespond is under the gang's percentage (consumed); 22 for an own member: goals again | confirmed (code) |

### Hide (type 0x13) {#t1-hide}

The hold command's hiding variant (`WarriorHoldTactic2_Create` `0x003128a0`, vtable `0x00543980`), used when the chief
stands near a hiding flag ([Warrior commands](ai.md#warrior-commands)): every Warrior hides in shadow.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003128f8` | `HideTactic_Init` | init | no callback; timer `+0x30` 0 | confirmed (code) |
| `0x00312960` | `HideTactic_GiveGoals` | helper | from the hiding flag nearest the chief (activity `0x21`), casts ten 10 m rays 15° apart, starting 60° to one side of the line from the chief's home to the flag, and keeps the hit wall points on reachable ground; each standing non-player member gets a Hide goal (`0x58`) at a free wall point at least 1 m from the chief, at his own hiding flag when his brain `+0x2d5` is set, or at a random reachable point near the flag | confirmed (code) |
| `0x00313028` | `HideTactic_RegiveMember` | helper | in two-player, a Warrior far from player 1 (over 10 m) gets FollowAndDefend on the nearer player | confirmed (code) |
| `0x003131a8` | `HideTactic_Start` | Start | the goals | confirmed (code) |
| `0x003131d0` | `HideTactic_Process` | Process | re-arms a 3-6 s timer; always 0 | confirmed (code) |
| `0x00313230` | `HideTactic_OnEvent` | Event | 1/16: for the Warriors (gang kind 0), when a player is attacked he must be hidden in shadow; then, when no free non-player member without `+0x2d5` is left, the attack command is dispatched for the chief (`WarriorCommand_Dispatch(…, 1, …)`); any other gang stops the tactic; 17 with `+4` = 0, 19 and 22 with `+8` = 1: the member again; 20 consumed | confirmed (code) |

### Hold (type 0x03) {#t1-hold}

The hold command's standing tactic and the first mission's player-gang tactic (`WarriorHoldTactic_Create` `0x00313400`,
init `PlayerGangTactic_Init` `0x00313490`, vtable `0x005439e0`): each Warrior holds where he is
([HoldPosition](ai-goals.md#goal-hold-position)), facing outward.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00313508` | `HoldTactic_GiveGoals` | helper | each standing non-player member but the chief gets his goal (`HoldTactic_GiveMemberGoal`) and a turn to a heading spread 360°/(members − 1) from the chief's, after 0-1 s with enemies about, else 2-4 s | confirmed (code) |
| `0x00313718` | `HoldTactic_GiveMemberGoal` | helper | `HoldTactic_GiveMemberGoal`: pops to the goal base; a re-issued Warrior of player 1's gang more than 10 m from him and nearer another player gets FollowAndDefend on that player; a dog (class 221) AvoidEnemies (7, 12); any other HoldPosition (`0x36`) at his own position, radius `+0x20` (1.5 m) | confirmed (code) |
| `0x00313958` | `HoldTactic_Start` | Start | goals, idle fidget clips; notes whether the chief is talking | confirmed (code) |
| `0x003139e0` | `HoldTactic_End` | End | removes the idle fidget clips | confirmed (code) |
| `0x00313a00` | `HoldTactic_OnViolence` | helper | as the follow tactic's: a free member looks at a busy fighter 3 s and at 10 % taunts | confirmed (code) |
| `0x00313c90` | `HoldTactic_Process` | Process | says the acknowledgement `0x85` once the chief is silent; always 0 | confirmed (code) |
| `0x00313d18` | `HoldTactic_OnEvent` | Event | 11 seen by a type-3 brain: the warning line; 17 with `+4` = 0, 19 and 22 with `+8` = 1: holds again; 20 → OnViolence (consumed) | confirmed (code) |

### HoldTheLine (type 0x04) {#t1-hold-the-line}

`TacticHoldTheLine` (vtable `0x00543a40`; constructor `HoldTheLineTactic_Construct`): defenders along a line, attackers
behind it ([Scripted tactics](ai.md#tactic-kinds)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00313e30` | `Tactic_HoldTheLine` | binding | `Tactic_HoldTheLine`: needs the gang and the three flags; constructs and sets the tactic | confirmed (code) |
| `0x00314038` | `HoldTheLineTactic_Start` | Start | the line direction and normal (`+0x30`, `+0x50`) and its foot `+0x40`; defenders = min(line length, 60 % of the members), spaced evenly with HTLDefense (`0x64`); the rest HTLOffense (`0x66`) round `flag3`, offset 0-1 m along the line | confirmed (code) |
| `0x00314538` | `HoldTheLineTactic_Process` | Process | every 1.75 s: 9 when no member has an enemy; 12 `TacBehindLine` when an enemy within 0.5 m of `flag1`'s height is past the line and farther than `distance` from `flag3`; the hit count is reset each `window`; 14 `TacObjectsThrown` when it reaches `hits` | confirmed (code) |
| `0x003147a0` | `HoldTheLineTactic_OnEvent` | Event | 2 on a defender: an attacker leaves HTLOffense and takes his spot with HTLDefense, code 13; 1 for an own member without a target clears his actions; 16 for an own member with `+4` = 1 counts a hit; 20 consumed | confirmed (code) |

### Idle (type 0x24) {#t1-idle}

`TacticIdle` (vtable `0x00543aa0`; constructor `IdleTactic_Construct` `0x003149f0`; its event slot `0x00315228` is in
the next range): members idle where they stand and banter.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00314aa8` | `IdleTactic_GiveGoals` | helper | each standing non-player member: `IdleTactic_AssignGoal` | confirmed (code) |
| `0x00314b50` | `IdleTactic_AssignGoal` | helper | goals popped and marked; an Idle goal with `clearAnims` | confirmed (code) |
| `0x00314bd0` | `IdleTactic_Stub` | helper | empty; called with 1 when a banter pair is found and 0 when it ends | confirmed (code) |
| `0x00314bd8` | `IdleTactic_PickBanterPair` | helper | as HanginOut's, among members whose top goal is Idle (type 0), each at 51 %; next try 3 s on | confirmed (code) |
| `0x00314e30` | `IdleTactic_UpdateBanter` | helper | statement (20, `0x4d`), then response (21, `0x4e`) and the gang's spot line advances; then the pair is released | confirmed (code) |
| `0x00314f38` | `IdleTactic_Start` | Start | goals; banter 3 s on; members' brain `+0x265` = 0 | confirmed (code) |
| `0x00314f80` | `IdleTactic_End` | End | ends the banter; members' brain `+0x265` = 1 | confirmed (code) |
| `0x003150c8` | `IdleTactic_Process` | Process | with `dynIdle`: 15 `TacAnimDone` once no member's top goal is a PlayDynIdle (`0x23`) still running (state ≠ 4); otherwise banter every 3 s, 0 | confirmed (code) |

## Tactics, part 2 (`0x00315228`-`0x00323160`) {#t2-tactics}

The tactic classes from TacticIdle's event slot to the attack sub-tactics: their constructors, member goals, Start, End,
Process and event slots, and the `Tactic<Name>` bindings. How tactics run, the base fields and the event codes are in
[Tactics](ai.md#tactics); what each class does for a script is in [the scripted tactics](ai.md#tactic-kinds). Event
numbers are the human events of [Script events](../references/script-events.md); codes are the callback codes
(`TacticGetString`).

### TacticIdle: the event slot {#t2-idle}

The event slot of TacticIdle (type `0x24`, vtable `0x00543aa0`); the class is described in
[Tactics](ai.md#tactic-kinds).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00315228` | `IdleTactic_OnEvent` | Event | 1 → code 5, 10 → 4, 11 (an own member spotted someone: noted, `Brain_NoteSpotter`) → 3, 16 → 6; with `dynIdle` (`+0x33`) 1/11/16 end the dyn idles instead; 20 with `respond` (`+0x31`): while the share of members in HelpRespond (`0x1d`) is within the gang's percentage, sends one free member at the side the leader picks (`Gang_SendHelper`), consumed; 22 for an own member re-issues his goal (`0x00314b50`) | confirmed (code) |

### TacticInfo (type 0x1f) {#t2-info}

`TacticInfo` (vtable `0x00543b00`): this gang's leader walks up to the other gang's leader and the two trade three
scripted lines (CRC32 hashes of the line names) with two anims. Field reading: `+0x20` the other gang, `+0x24` / `+0x28`
the two anim names (interned by the script system `*(0x00512b04)` `+0xcc`), `+0x2c`-`+0x34` the three line hashes,
`+0x38` the next check, `+0x3c` the range, `+0x40` the exchange state.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00315580` | `InfoTactic_Construct` | constructor | base fields, vtable `0x00543b00`, the other gang, two interned anim names, three line hashes, the range; next check now + 1 s | confirmed (code) |
| `0x003156c0` | `InfoTactic_Seat` | helper | the leader takes anim 1 (`Human_UseAnim`) and flag `+0x1b2`; flushes his goals and pushes AddressPerson (`AddressPersonGoal_Init`) at 1.5 m facing the other gang's leader within the range | confirmed (code) |
| `0x00315820` | `InfoTactic_OnLeaderAnimStart` | helper | the leader drops flag `+0x1b2`; the other gang's leader says line 1; state 1 | confirmed (code) |
| `0x00315890` | `InfoTactic_Start` | Start | seats the leader (`0x003156c0`) | confirmed (code) |
| `0x003158b0` | `InfoTactic_End` | End | the leader drops `+0x1b2`, stops speaking and clears his anim | confirmed (code) |
| `0x00315928` | `InfoTactic_Process` | Process | every 1 s while both leaders live: within the range with a game-state count `+0x322` above 0 → 10 `TacError`; leaving the range in state 2 → the leader says line 3, state 3. State 1: once the other leader's line ends, the leader takes anim 2 and says line 2 (state 2); state 3: once his line ends he goes back to anim 1, state 0, returns 15 `TacAnimDone` | inferred |
| `0x00315b70` | `InfoTactic_OnEvent` | Event | 0 from the leader (his anim started) → `0x00315820` and code 18 `TacAnimStart`; 1 → 5; 16 → 6; 20 consumed; 22 re-seats | confirmed (code) |

### Tactic script bindings {#t2-bindings}

The `Tactic<Name>` bindings ([bindings](../references/bindings/ai.md)): each finds the gang, takes a tactic from the
pool (`Tactic_Alloc`), runs the class constructor and sets it on the gang (`Gang_SetTactic`, which ends the old one).
Classes: [Tactics](ai.md#tactic-kinds).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00315c58` | `Tactic_EventName` | binding | `TacticGetString`: the 19 event-code names, 0 `TacRunning` ... 18 `TacAnimStart` | confirmed (code) |
| `0x00315d70` | `Gang_ClearTactic` | binding | `TacticClear`: stops the gang's tactic (`Gang_StopTactic`) | confirmed (code) |
| `0x00315d98` | `Tactic_Defend` | binding | `TacticDefend`: constructor `0x003101d8` round a human, no time limit | confirmed (code) |
| `0x00315e48` | `Tactic_WalkinTall` | binding | `TacticWalkinTall`: constructor `0x0031ec90` | confirmed (code) |
| `0x00315ee8` | `Tactic_Pursue` | binding | `TacticPursue`: `PursueTactic_Construct` | confirmed (code) |
| `0x00315fc8` | `Tactic_HanginOut` | binding | `TacticHanginOut`: `HanginOutTactic_Construct` | confirmed (code) |
| `0x003160a8` | `Tactic_Wander` | binding | `TacticWander`: constructor `0x0031f300` | confirmed (code) |
| `0x003161a0` | `Tactic_TravelPath` | binding | `TacticTravelPath`: `TravelPathTactic_Construct` | confirmed (code) |
| `0x00316298` | `Tactic_MoveToFlag` | binding | `TacticMoveToFlag`: constructor `0x00317410` | confirmed (code) |
| `0x00316350` | `Tactic_Address` | binding | `TacticAddress`: two gangs; constructor `0x00306c68` | confirmed (code) |
| `0x00316450` | `Tactic_Crowd` | binding | `TacticCrowd`: constructor `0x0030f4d0` | confirmed (code) |
| `0x003164e0` | `Tactic_Vandalize` | binding | `TacticVandalize`: constructor `0x0031e3b8` with a zone and no cars | confirmed (code) |
| `0x00316590` | `Tactic_VandalizeCars` | binding | `TacticVandalizeCars`: the Vandalize constructor with zone 0, range −1 and a list of up to four cars | confirmed (code) |
| `0x00316638` | `Tactic_Steal` | binding | `TacticSteal`: constructor `0x0031c030` | confirmed (code) |
| `0x003166e8` | `Tactic_Shadow` | binding | `TacticShadow`: constructor `0x0031b268` | confirmed (code) |
| `0x003167b8` | `Tactic_Info` | binding | `TacticInfo`: two gangs; the three line names hashed (`Crc32_Hash`); constructor `0x00315580` | confirmed (code) |
| `0x003168e8` | `Tactic_RiotCop` | binding | `TacticRiotCop`: a point (w = 1.0); constructor `0x00319048` | confirmed (code) |
| `0x00316988` | `Tactic_AvoidEnemies` | binding | `TacticAvoidEnemies`: constructor `0x003085d8` | confirmed (code) |
| `0x00316a68` | `Tactic_ManWeaponPile` | binding | `TacticManWeaponPile`: constructor `0x00317050` | confirmed (code) |
| `0x00316b30` | `Tactic_Domination` | binding | `TacticDomination`: `DominationTactic_Init` | confirmed (code) |
| `0x00316bd0` | `Tactic_Ring` | binding | `TacticRing`: constructor `0x00318940` | confirmed (code) |
| `0x00316ca0` | `Tactic_UseFlag` | binding | `TacticUseFlag`: `UseFlagTactic_Construct` | confirmed (code) |
| `0x00316d58` | `Tactic_StandGround` | binding | `TacticStandGround`: `StandGroundTactic_Init` | confirmed (code) |
| `0x00316dd8` | `Tactic_Confront` | binding | `TacticConfront`: `ConfrontTactic_Construct` (gang ≥ 0 only) | confirmed (code) |
| `0x00316ee8` | `Tactic_Idle` | binding | `TacticIdle`: `IdleTactic_Construct` (gang ≥ 0 only) | confirmed (code) |
| `0x00316fa0` | `Tactic_TriggerCrowd` | binding | `TacticTrigger(gang, what, on)` for a crowd tactic (type `0x1b`): what 0 sets the periodic switch `+0x2a`, what 1 runs the reaction (`0x0030fe58`) | confirmed (code) |
| `0x0031a268` | `Tactic_Scout` | binding | `TacticScout`: `ScoutTactic_Construct` (gang must exist) | confirmed (code) |
| `0x0031a340` | `Tactic_PathScout` | binding | `TacticPathScout`: constructor `0x0031a518` | confirmed (code) |
| `0x0031c358` | `Tactic_Taunt` | binding | `TacticTaunt`: constructor `0x0031c430` | confirmed (code) |

### TacticManWeaponPile (type 0x06) {#t2-man-weapon-pile}

Vtable `0x00543b60`; `+0x20` / `+0x24` the pile arguments, `+0x28` / `+0x2a` two shorts, `+0x2c` the next check.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00317050` | `ManWeaponPileTactic_Construct` | constructor | base fields, vtable `0x00543b60`, the two pile values and two shorts | confirmed (code) |
| `0x00317108` | `ManWeaponPileTactic_GiveGoals` | helper | flushes each living member and pushes a ManWeaponPile goal (`ManWeaponPileGoal_Init`) with the tactic's values | confirmed (code) |
| `0x00317218` | `ManWeaponPileTactic_Start` | Start | gives the goals; next check now + 1 s | confirmed (code) |
| `0x00317258` | `ManWeaponPileTactic_Process` | Process | every 1 s: 1 `TacFinished` once a member whose top goal is ManWeaponPile (`0x52`) has goal `+0x38` above 20 (inferred: more than 20 objects thrown), else 0 | confirmed (code) |
| `0x00317350` | `ManWeaponPileTactic_OnEvent` | Event | 1 → 5; 2 → 13 `TacMemberDied`; 16 → 6; 19/22 re-give the goals; 20 consumed | confirmed (code) |

### TacticMoveToFlag (type 0x19) {#t2-move-to-flag}

Vtable `0x00543bc0`: the leader walks to a flag and the others follow in his formation, with banter. Fields: `+0x20` the
flag, `+0x24` the gait, `+0x28` the slot set, `+0x2c` banter, `+0x30` the next banter try, `+0x34` the banter state,
`+0x38` / `+0x3c` the banter pair, `+0x40` the leader's set before. The banter is [shared](ai.md#tactic-kinds).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00317410` | `MoveToFlagTactic_Construct` | constructor | base fields, vtable `0x00543bc0`, flag, gait, slot set, banter; first banter try now + 3 s | confirmed (code) |
| `0x003174e8` | `MoveToFlagTactic_GiveGoals` | helper | the leader gets `Goal_MoveToFlag` (0.5 m, the gait); his formation takes the slot set or set 3 with min(members − 1, 9) slots (`Formation_MakeShape` 1.0, 2); every other free member joins it and follows at 3 m (`Goal_FollowPlayer` mode 3) | confirmed (code) |
| `0x00317700` | `MoveToFlagTactic_Start` | Start | gives the goals | confirmed (code) |
| `0x00317720` | `MoveToFlagTactic_End` | End | members leave the formation; a banter cut mid-line moves the gang's spot line on; restores the leader's set | confirmed (code) |
| `0x003177c0` | `MoveToFlagTactic_PickBanterPair` | helper | unless commands are locked (`+0x411`), a scene plays (`+0x410`) or fewer than two live: picks two non-player members at 51 % each, within 5 m of each other; 1 when both found, else clears them and retries in 3 s | confirmed (code) |
| `0x003179c8` | `MoveToFlagTactic_StepBanter` | helper | the first says `statement` (20, alternative 77 `statement2`, `Human_SayStateResponse`), when it ends the second says `response` (21 / 78); after his line a new pair is not picked (pair cleared) | confirmed (code) |
| `0x00317ad0` | `MoveToFlagTactic_Process` | Process | every 3 s tries a banter pair, then steps the banter; always 0 | confirmed (code) |
| `0x00317b38` | `MoveToFlagTactic_OnEvent` | Event | 1 → 5; 8 (arrived) → 8 `TacArrived`; 10 → 4; 11 for an own member → noted, 3; 16 → 6; 17/18/22 re-give the goals; 20 consumed | confirmed (code) |

### TacticPursue (type 0x14) {#t2-pursue}

The class's Start, End, Process and Event; its constructor and the chase readers sit just below this range. Fields:
`+0x20` the target gang, `+0x28` the gait, `+0x2c` / `+0x30` the two angles, `+0x38` / `+0x44` timers, `+0x48` the last
car seen.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00317d38` | `PursueTactic_GiveGoals` | helper | with a live target gang and leader: flushes each free non-player member and pushes Chase (`ChaseGoal_Push`, angles, gait) on the target leader | confirmed (code) |
| `0x00317e90` | `PursueTactic_FindTargetGang` | helper | takes the target gang from the first member whose brain `+0x264` is set; 1 when found | confirmed (code) |
| `0x00317f20` | `PursueTactic_Start` | Start | finds the target gang if needed, gives the goals; when the leader's top goal is Chase, substitutes anim `0x29c` with one chase anim (`0x00511520`) and says `spot` (22) | confirmed (code) |
| `0x00317ff8` | `PursueTactic_End` | End | releases the anim `0x29c` substitution | confirmed (code) |
| `0x00318098` | `PursueTactic_IsNearTargetLeader` | helper | 1 when a human is within 20 m of the target gang's leader | confirmed (code) |
| `0x003181e8` | `PursueTactic_OnCar` | helper | event 23 with a car not seen before (`+0x48`): members within 30 m drop an Investigate goal (`0x5e`); a member whose Chase goal reacts to the car (`0x002b0c88`) refreshes the sight read (`PursueTactic_AnyChaseSees` 2) | confirmed (code) |
| `0x003183b8` | `PursueTactic_Process` | Process | described in [Tactics](ai.md#tactic-kinds) (codes 9 and 7, the 150 ms and 100 ms checks, the search time) | confirmed (code) |
| `0x00318770` | `PursueTactic_OnEvent` | Event | 1: a member hit by a thrower calls for help (`Brain_CallForHelp`, consumed), else 5; 11 for an own member clears his actions; 16 → 6; 20 consumed unless the human or the offender is of either gang; 22 re-gives the goals; 23 → `0x003181e8`, consumed | confirmed (code) |

### TacticRing (type 0x08) {#t2-ring}

Vtable `0x00543c80`: the members stand evenly round a flag facing in and jeer when a fight breaks out among others: a
fight ring (the name is inferred from this and the [Ring goal](ai-goals.md#goal-ring), which ignores type-8 humans).
Fields: `+0x20` the flag, `+0x24` the radius, `+0x28` the start angle, `+0x2c` the next jeer, `+0x30` the goal option.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00318940` | `RingTactic_Construct` | constructor | base fields, vtable `0x00543c80`, flag, radius, the option; start angle = degrees × π/180 × the second float | confirmed (code) |
| `0x00318a38` | `RingTactic_PlaceMembers` | helper | each living member gets a point on the circle (step 2π / members from the start angle); a point off the navigation mesh is pulled in along a ray to 1 m short of the hit; sets brain `+0x208` to the attack table `0x00511530` and pushes `Goal_Ring` (radius, flag, point, option) | confirmed (code) |
| `0x00318d78` | `RingTactic_Jeer` | helper | when the timer `+0x2c` is clear (then set to now + 1.5 s): each member at 60 % with no queued actions (`+0x2e`) and allowed to gesture queues a taunt action (`TauntAction_Init`, kind `0x10` at 40 % else `0x11`, 1-2 s) | confirmed (code) |
| `0x00318ed8` | `RingTactic_Start` | Start | places the members | confirmed (code) |
| `0x00318ef8` | `RingTactic_End` | End | clears each member's attack table override `+0x208` | confirmed (code) |
| `0x00318f90` | `RingTactic_OnEvent` | Event | 20 (violence) by a gang this gang is an enemy of → jeer, consumed; 19/22 re-place the members | confirmed (code) |

### TacticRiotCop (type 0x05) {#t2-riot-cop}

Vtable `0x00543ce0`: riot police in a formation round a point ([GoalRiot](ai.md#riot), [RiotCop
goal](ai-goals.md#goal-riot-cop)). Fields: `+0x20` the point, `+0x30` the next check, `+0x34` the leave time, `+0x38`
the leader's set before, `+0x3a` armed.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00319048` | `RiotCopTactic_Construct` | constructor | base fields, vtable `0x00543ce0`, the point | confirmed (code) |
| `0x003190e0` | `RiotCopTactic_GiveGoals` | helper | the leader's formation: set 3, min(members − 1, 9) slots; every free member joins and gets a RiotCop goal (`RiotCopGoal_Init`) on the point | confirmed (code) |
| `0x00319270` | `RiotCopTactic_Start` | Start | gives the goals | confirmed (code) |
| `0x00319290` | `RiotCopTactic_End` | End | restores the leader's set; members leave the formation | confirmed (code) |
| `0x003192f0` | `RiotCopTactic_Process` | Process | every 1 s reads the members' enemy lists (`0x004de0c8`); a member with an empty one arms the tactic, once all have enemies again a 30 s timer starts; when it passes the gang is given `TacticMoveToFlag` to the nearest flag of activity 8 (5, 5); always 0 | inferred |
| `0x00319470` | `RiotCopTactic_OnEvent` | Event | 11 for an own member while the leader's top goal is Chase: clears the leader's actions; 2/22 re-give the goals | confirmed (code) |

### WarriorScatter (type 0x25) {#t2-warrior-scatter}

The scatter command's tactic ([Warrior commands](ai.md#warrior-commands); `WarriorScatterTactic_Create` `0x00319570`,
vtable `0x00543d40`): each Warrior runs to one of up to six hiding flags (activity `0x21`) near the chief and lies low
there (Scatter goal `0x37`). Fields: `+0x20` six flags, `+0x38` the radius (75.0), `+0x3c` the flag count, `+0x3d` the
next flag, `+0x3e` the chief's line playing, `+0x3f` the answer said.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00319600` | `WarriorScatterTactic_Init` | constructor | base fields, the radius, vtable `0x00543d40`, clears the flags | confirmed (code) |
| `0x00319688` | `WarriorScatterTactic_FindFlags` | helper | up to 32 flags of activity `0x21` within the radius of the chief and at least 10 m away, reachable from him (not checked in game mode `0x1f` state 6), sorted nearest first; keeps six | confirmed (code) |
| `0x003198a0` | `WarriorScatterTactic_ClearFlags` | helper | clears the six flags, the count and the next index | confirmed (code) |
| `0x003198d0` | `WarriorScatterTactic_GiveGoals` | helper | gives each living non-player member his goal (`0x00319980`) | confirmed (code) |
| `0x00319980` | `WarriorScatterTactic_GiveMemberGoal` | helper | flushes the member; on a re-issue for a type-3 brain in player 1's gang more than 10 m from player 1 and nearer another player: FollowAndDefend on that player; with no flags a Scatter goal with no flag; else Scatter (`ScatterGoal_Init`) to the next flag in turn, at a random navigable point 1-1.5 m from it (five tries) | confirmed (code) |
| `0x00319da0` | `WarriorScatterTactic_FarthestSafeFlag` | helper | of the flags with no player within 5 m, the one farthest from a point and at least 5 m from it; 0 when none | confirmed (code) |
| `0x00319ec8` | `WarriorScatterTactic_Start` | Start | finds the flags, gives the goals, notes whether the chief is still saying his line | confirmed (code) |
| `0x00319f50` | `WarriorScatterTactic_End` | End | clears the flags | confirmed (code) |
| `0x00319f70` | `WarriorScatterTactic_Process` | Process | once, after the chief's line: a random free member answers `scatter_resp` (159, `0x003069f0`); always 0 | confirmed (code) |
| `0x00319ff8` | `WarriorScatterTactic_OnEvent` | Event | 1/16 on a member at his hiding flag (Scatter goal `0x37`, `Scatter_IsNear`): he moves to the farthest safe flag (the goal's flag and point rewritten), or with none melees for 10 s unless fighting; 17 (`+4` = 0), 19 and 22 (`+8` = 1) re-issue his goal; 20 consumed | confirmed (code) |

### TacticScout and TacticPathScout (type 0x27) {#t2-scout}

The rest of the Scout class (vtable `0x00543da0`; [Tactics](ai.md#tactic-kinds)). Fields of PathScout: `+0x30` up to 16
flag indices, `+0x50` / `+0x51` / `+0x53` / `+0x54` options, `+0x52` 1 = path scout.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031a518` | `PathScoutTactic_Construct` | constructor | base fields, vtable `0x00543da0`, path mode on, the options; resolves up to 16 flags to indices (`Flag_FindIndexByName`) | confirmed (code) |
| `0x0031a660` | `ScoutTactic_GiveGoals` | helper | gives every member that is not knocked out, cuffed, down or dead his goal | confirmed (code) |
| `0x0031a728` | `ScoutTactic_GiveMemberGoal` | helper | a non-player member is flushed and gets PathScout (`0x70`, the flags and two options) or Scout (`0x6f`) | confirmed (code) |
| `0x0031acb8` | `ScoutTactic_OnViolence` | helper | event 20 for a member free to act (no train near, not cuffed, nothing held, actions free): picks a side (`Brain_PickSideInFight`) he may engage; when chasing drops back to his scout goal; when fighting retargets (unless the top goal is `0x0b`, `0x2c` or `0x3f`), else, within sight range, drops an Investigate goal; then joins in (`Gang_SendHelper`) unless already helping (`0x1d`) | confirmed (code) |
| `0x0031af68` | `ScoutTactic_FireArrived` | helper | fires the callback with 8 `TacArrived` (inferred: a path scout's last flag) | inferred |
| `0x0031aff8` | `ScoutTactic_End` | End | removes the idle fidget clips and the `0x29c` substitution | confirmed (code) |
| `0x0031b1b0` | `ScoutTactic_OnEventSlot` | Event | 1/11/16 → `ScoutTactic_OnEvent` (`0x0031a818`), consumed; 19/22 re-give the member's goal; 20 → `0x0031acb8`, consumed | confirmed (code) |

### TacticShadow (type 0x1e) {#t2-shadow}

Vtable `0x00543e00`: the leader shadows another gang (Shadow goal, `ShadowGoal_Init`) with his members in formation
behind, posing when they face the other leader. Fields: `+0x20` the target gang, `+0x24` the range, `+0x28` / `+0x2c`
the 250 ms and 1 s checks, `+0x30` the leader's line (interned), `+0x34` / `+0x36` the leader's set before and the slot
set, `+0x38` stop flag, `+0x3c` camera option, `+0x3d` line said.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031b268` | `ShadowTactic_Construct` | constructor | base fields, vtable `0x00543e00`, target gang, range, slot set, camera option, the line name | confirmed (code) |
| `0x0031b388` | `ShadowTactic_GiveGoals` | helper | the leader gets a Shadow goal on the target gang; his formation takes the slot set or set 3 (min(members − 1, 9), shape 1.0, 2); every other free member joins and follows at 1 m (mode 3) | confirmed (code) |
| `0x0031b5d0` | `ShadowTactic_FindTargetGang` | helper | the target gang from the first member whose brain `+0x264` is set | confirmed (code) |
| `0x0031b660` | `ShadowTactic_Start` | Start | with a target gang: gives the goals, substitutes anim `0x253` with four poses (`0x00511590`) and says `shadow_spot` (136) | confirmed (code) |
| `0x0031b6d0` | `ShadowTactic_End` | End | restores the set, members leave the formation, releases `0x253`; with the camera option the camera stops following a second human | confirmed (code) |
| `0x0031b768` | `ShadowTactic_Process` | Process | 9 when the target gang is gone. Every 250 ms, with the target inside the turf and reachable: 9 when `+0x38` is set; 17 `TacSpeed` when the target leader's state `+0x1a8` is 3 or more and in view; 7 within the range plus both gangs' radii. Not reachable: 7 when the leader sees the nearest target member, else 9. Every 1 s, when the leader sees the target leader (π/8 cone): a member in state 2 facing the leader's heading (within 0.99) at 51 % poses (`0x253`); the leader's first pose turns the camera on him (option) and says the line; members say `shadow` (135) | confirmed (code) |
| `0x0031bc08` | `ShadowTactic_OnEvent` | Event | 1 → 5; 2/19 for an own member, 17, 18, 22 re-give the goals; 16 → 6; 20 consumed | confirmed (code) |

### TacticStandGround (type 0x22) {#t2-stand-ground}

Vtable `0x00543e60`: every member holds where he stands ([StandGround goal](ai-goals.md#goal-stand-ground)).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031bcd0` | `StandGroundTactic_Init` | constructor | base fields, vtable `0x00543e60` | confirmed (code) |
| `0x0031bd38` | `StandGroundTactic_AssignGoals` | helper | each non-player member: attack table `+0x208` = `0x005115b8`, flushed, `Goal_StandGround` at his own position | confirmed (code) |
| `0x0031be48` | `StandGroundTactic_Start` | Start | assigns the goals; substitutes `0x253` with four poses (`0x005115a8`) | confirmed (code) |
| `0x0031be88` | `StandGroundTactic_End` | End | clears `+0x208` of living members; releases `0x253` | confirmed (code) |
| `0x0031bf48` | `StandGroundTactic_OnEvent` | Event | 1 → 5; 10 → 4; 16 on an own member hit by an object (`+4` = 1) → 14 `TacObjectsThrown`; 19/22 re-assign; 20 consumed | confirmed (code) |

### TacticSteal (type 0x1d) {#t2-steal}

Vtable `0x00543ec0`; `+0x20` the zone, `+0x24` the delay, `+0x30` `leaderRange`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031c030` | `StealTactic_Construct` | constructor | base fields, vtable `0x00543ec0`, zone, delay, leader range; timer now + 3 s | confirmed (code) |
| `0x0031c0e0` | `StealTactic_GiveGoals` | helper | each living non-player member is flushed and gets a Steal goal (`StealGoal_Init`) | confirmed (code) |
| `0x0031c1d0` | `StealTactic_Start` | Start | gives the goals | confirmed (code) |
| `0x0031c258` | `StealTactic_OnEvent` | Event | 1 → 5; 10 → 4; 11 own member → noted, 3; 16 → 6; 19/22 re-give; 20 consumed | confirmed (code) |

### TacticTaunt (type 0x28) {#t2-taunt}

Vtable `0x00543f20`: the members idle and take turns playing taunt anims until an enemy gang comes in range. Fields:
`+0x20` up to four taunt anims (default the four of `0x005113a8`), `+0x30` the range, `+0x34` / `+0x38` the 500 ms and
250 ms checks, `+0x3c` the anim count, `+0x3d` the next member, `+0x3e` the next anim.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031c430` | `TauntTactic_Construct` | constructor | base fields, vtable `0x00543f20`, the range, the named anims (interned) or the four defaults | confirmed (code) |
| `0x0031c698` | `TauntTactic_GiveGoals` | helper | each living, uncuffed member gets his goal | confirmed (code) |
| `0x0031c740` | `TauntTactic_GiveMemberGoal` | helper | flushes him and pushes an Idle goal (`IdleGoal_Init` 0, 1) | confirmed (code) |
| `0x0031c7b0` | `TauntTactic_Start` | Start | idles the members, removes the gang's combat-fidget and fight-taunt clips, substitutes `0x253` with the taunt anims | confirmed (code) |
| `0x0031c818` | `TauntTactic_End` | End | releases `0x253` | confirmed (code) |
| `0x0031c840` | `TauntTactic_Process` | Process | every 500 ms: 7 when the leader's nearest enemy's gang is within the range plus both radii. Every 250 ms: the next member in turn, if free and holding nothing, plays the next taunt (`PlayAnimAction_Init`, blend 0.2) and says `cheer2` (17) when silent and allowed | confirmed (code) |
| `0x0031caf8` | `TauntTactic_OnEvent` | Event | 1 → 5; 16 → 6; 20 consumed; 19 and 22 (`+8` = 1) re-idle the member | confirmed (code) |

### TacticTravelPath (type 0x17) {#t2-travel-path}

The class's goals, Start, End, Process and Event ([Tactics](ai.md#tactic-kinds)); the banter fields are at
`+0x48`-`+0x50`.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031cce0` | `TravelPathTactic_GiveGoals` | helper | the leader gets `GoalTravelPath` (mode 1 loop / 2, reverse, gait, start point, delay) or, with no path, TravelFlagNet from the nearest flag; his formation takes the slot set or set 3 (min(members, 9)); every other free member follows at 1 m (mode 0) | confirmed (code) |
| `0x0031cf80` | `TravelPathTactic_Start` | Start | gives the goals; substitutes `0x29c` with two anims (`0x00511600`) | confirmed (code) |
| `0x0031cfc0` | `TravelPathTactic_End` | End | restores the set, ends banter, stops the members' speech, releases `0x29c` | confirmed (code) |
| `0x0031d0e8` | `TravelPathTactic_PickBanterPair` | helper | as [MoveToFlag's](#t2-move-to-flag), skipping members whose top goal is MoveToUseFlag (4) | confirmed (code) |
| `0x0031d358` | `TravelPathTactic_StepBanter` | helper | as MoveToFlag's | confirmed (code) |
| `0x0031d460` | `TravelPathTactic_Process` | Process | when the banter timer has passed, at 76 %: a free member within 5 m of the leader (not the banter pair, not in MoveToUseFlag) with a usable flag within 5 m and a straight walk under 10 m uses it (MoveToUseFlag, 0.3); every 3 s a banter pair; then the banter step; always 0 | confirmed (code) |
| `0x0031d758` | `TravelPathTactic_OnEvent` | Event | 1 → 5; 2/17/18/19 for an own member and 22 re-give the goals; 10 → 4; 11 own → 3; 16 → 6; 20 consumed | confirmed (code) |

### TacticUseFlag (type 0x21) {#t2-use-flag}

The class's body ([Tactics](ai.md#tactic-kinds)). Fields: `+0x20` the flag, `+0x24` the range, `+0x28` `view`, `+0x2c`
the banter timer, `+0x30` the sight range before, `+0x34` leaving, `+0x38`-`+0x40` banter, `+0x44` banter on.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031d950` | `UseFlagTactic_Seat` | helper | each living member within the range of the flag (or in Investigate `0x5e` / HelpRespond `0x1d`) and not yet in MoveToUseFlag: the nearest usable flag; one outside the range makes him drop and store what he holds; one inside gets MoveToUseFlag (0.5, 2, 1) and sight range `+0x130` = `view`; members outside the range walk to the flag (`Goal_MoveToFlag`, gait 3) | confirmed (code) |
| `0x0031dc28` | `UseFlagTactic_Start` | Start | remembers the first member's sight range, seats | confirmed (code) |
| `0x0031dca8` | `UseFlagTactic_End` | End | ends banter; restores every member's sight range | confirmed (code) |
| `0x0031dd60` | `UseFlagTactic_PickBanterPair` | helper | as MoveToFlag's, skipping members in Investigate or HelpRespond; retry in 1 s | confirmed (code) |
| `0x0031df90` | `UseFlagTactic_StepBanter` | helper | as MoveToFlag's | confirmed (code) |
| `0x0031e098` | `UseFlagTactic_Process` | Process | described in [Tactics](ai.md#tactic-kinds): seat each second until the nearest player not in shadow comes within the range, then release with random 0-1 s delays and 7 once all reached state 3 | confirmed (code) |
| `0x0031e270` | `UseFlagTactic_Leave` | helper | sets `+0x34`: the tactic stops seating and releases the members | confirmed (code) |
| `0x0031e280` | `UseFlagTactic_OnEvent` | Event | 1/16 from a thrower: the member calls for help, consumed; 11 notes the spotter; with a callback, 1/11/16 make the tactic leave (`0x0031e270`), consumed; 20/23 reset the banter | confirmed (code) |

### TacticVandalize (type 0x1c) {#t2-vandalize}

Vtable `0x00544040`. Fields: `+0x20` up to four cars, `+0x30` the zone, `+0x34` the delay, `+0x38` the next check,
`+0x3c` the last object reported, `+0x40` `leaderRange`, `+0x44` the car count.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031e3b8` | `VandalizeTactic_Construct` | constructor | base fields, vtable `0x00544040`, cars, zone, delay, range; next check now + 3 s | confirmed (code) |
| `0x0031e4f0` | `VandalizeTactic_GiveGoals` | helper | each living non-player member: brain `+0x28d` = 1, flushed, a Destroy goal (`DestroyGoal_Init`: zone, delay, cars) | confirmed (code) |
| `0x0031e5f8` | `VandalizeTactic_Start` | Start | gives the goals | confirmed (code) |
| `0x0031e620` | `VandalizeTactic_OnDamage` | helper | a vandal hit by a non-friend (`+0x11`): when fighting he takes the attacker as enemy and target; otherwise pops down to his Destroy goal, adds the enemy, melees for 8 s and pushes a 4 s fight goal | confirmed (code) |
| `0x0031e770` | `VandalizeTactic_OnObjectBroken` | helper | event 23: a member friendly with the breaker and waiting in his Destroy goal (`DestroyGoal_IsWaiting`) looks at him for 2 s, turns when more than 60° off, says `whoop` (143) when allowed and at 50 % cheers (`0x256`, after 0-750 ms) | confirmed (code) |
| `0x0031ea00` | `VandalizeTactic_Process` | Process | every 3 s: with a zone, 1 `TacFinished` when it has no breakable object left (`ObjZone_HasObjects`); with cars, 1 when every car has all its spots blocked (`Car_AreAllSpotsBlocked`, inferred: wrecked) | confirmed (code) |
| `0x0031eaf0` | `VandalizeTactic_OnEvent` | Event | 1 → damage handler and 5; 10 → 4; 11 own → 3; 16 → 6; 19/22 re-give; 20 consumed; 23 → cheer, and an object of the zone not intact (`+0x10d` / `+0x10e`) → 16 `TacObjectDestroyed` once per object, consumed | confirmed (code) |

### TacticWalkinTall (type 0x15) {#t2-walkin-tall}

Vtable `0x005440a0`: the gang walks to a flag in formation, posing when the leader sees an enemy gang's leader. Fields:
`+0x20` the flag, `+0x24` / `+0x28` the range and its square, `+0x2c` / `+0x30` the 1 s and 250 ms checks.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031ec90` | `WalkinTallTactic_Construct` | constructor | base fields, vtable `0x005440a0`, flag, range; both checks now + 1 s | confirmed (code) |
| `0x0031ed40` | `WalkinTallTactic_GiveGoals` | helper | leader `Goal_MoveToFlag` (0.5 m, gait 2); set 3 with min(members − 1, 9) slots; others follow at 0.75 m (mode 0) | confirmed (code) |
| `0x0031ef28` | `WalkinTallTactic_Start` | Start | gives the goals; four poses for `0x253` (`0x00511620`); says `shadow_spot` (136) | confirmed (code) |
| `0x0031ef78` | `WalkinTallTactic_End` | End | members leave the formation; releases `0x253` | confirmed (code) |
| `0x0031efb8` | `WalkinTallTactic_Process` | Process | every 1 s: 7 when the nearest enemy gang's nearest member is within the range, or is not on this gang's turf (`Gang_IsHumanInTurf` 0). Every 250 ms, when the leader sees the enemy leader (π/8 cone): one member in state 2 facing the leader's heading poses (`0x253`) at 51 % | confirmed (code) |
| `0x0031f268` | `WalkinTallTactic_OnEvent` | Event | 1 → 5; 8 → 8; 16 → 6; 17/18/22 re-give; 20 consumed | confirmed (code) |

### TacticWander (type 0x16) {#t2-wander}

Vtable `0x00544100`: the leader wanders (Wander goal) and the members follow at 4 m, with banter and three options: use
flags (`+0x41`), smash things (`+0x42`), pick things up (`+0x43`). Fields: `+0x20` / `+0x28` the area, `+0x24` the 3 s
timer, `+0x2c` the gait, `+0x30` / `+0x32` the leader's set before and the slot set, `+0x34`-`+0x3c` banter, `+0x40`
banter on.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x0031f300` | `WanderTactic_Construct` | constructor | base fields, vtable `0x00544100`, the area, gait, slot set and the four switches | confirmed (code) |
| `0x0031f410` | `WanderTactic_GiveGoals` | helper | the leader gets a Wander goal (`WanderGoal_Init`); his formation takes the slot set or set 3 (min(members − 1, 9), shape 2.0, 1); other free members follow at 4 m (mode 0) | confirmed (code) |
| `0x0031f638` | `WanderTactic_Start` | Start | gives the goals; two anims for `0x29c` (`0x00511638`) | confirmed (code) |
| `0x0031f678` | `WanderTactic_End` | End | restores the set, ends banter, stops speech, releases `0x29c` | confirmed (code) |
| `0x0031f7a0` | `WanderTactic_PickBanterPair` | helper | as MoveToFlag's, only members with a top goal other than MoveToUseFlag | confirmed (code) |
| `0x0031fa10` | `WanderTactic_StepBanter` | helper | as MoveToFlag's | confirmed (code) |
| `0x0031fb18` | `WanderTactic_IsMemberFree` | helper | 1 when the member has no goal or his top goal is a follow goal (`0x32` / `0x33`) | confirmed (code) |
| `0x0031fb80` | `WanderTactic_TryUseFlag` | helper | at 76 %: a free member within 5 m of the leader uses a usable flag within 5 m and a straight 10 m walk (MoveToUseFlag 0.5) | confirmed (code) |
| `0x0031fdc0` | `WanderTactic_TrySmash` | helper | the leader looks for a smash target within 30 m (`Ai_FindObject`, filter `0x0053f250`): none → a free member holding something throwable wrecks it (`VandalizeItemGoal_Push`); one on the turf → a free member within 10 m of the leader goes to wreck it | confirmed (code) |
| `0x003200c8` | `WanderTactic_TryPickUp` | helper | a pick-up target within 30 m (filter `0x0053f2b0`) on the turf: a free member within 10 m of the leader fetches it (`Goal_GetItem` 4) and says `riot` (89) at 51 % | confirmed (code) |
| `0x00320360` | `WanderTactic_Process` | Process | every 3 s: a banter pair, then each enabled option (use flag, smash, pick up); then the banter step; always 0 | confirmed (code) |
| `0x00320410` | `WanderTactic_OnEvent` | Event | 1 → 5; 2/19 own and 17/18/22 re-give; 10 → 4; 11 own → 3; 16 → 6; 20 consumed | confirmed (code) |

### WarriorAttack (type 0x01) {#t2-warrior-attack}

The attack command's tactic (vtable `0x00544160`, [Warrior commands](ai.md#warrior-commands)). Fields: `+0x18` the gang
has a target, `+0x20` the chief's line playing, `+0x21` the answer said.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x003205b0` | `WarriorAttackTactic_Init` | constructor | base fields, vtable `0x00544160` | confirmed (code) |
| `0x00320618` | `WarriorAttackTactic_GiveGoals` | helper | each living non-player Warrior except the chief: class 221 → AvoidEnemies (4 m, 8, 8); a re-issued type-3 member of player 1's gang more than 10 m from player 1 and nearer another player → FollowAndDefend on that player; else FollowAndAttack (`0x34`) on the chief | confirmed (code) |
| `0x003208e8` | `WarriorAttackTactic_Start` | Start | gives the goals, adds the gang's idle fidget clips, notes the chief's line, sets `+0x18` when the gang's target (`+0x10`) is a valid enemy | confirmed (code) |
| `0x003209b0` | `WarriorAttackTactic_End` | End | removes the idle fidget clips | confirmed (code) |
| `0x003209d0` | `WarriorAttackTactic_Process` | Process | once, after the chief's line: a member answers `attack_resp` (132); sets `+0x18` once the locked-on chief targets a non-friendly non-player; always 0 | confirmed (code) |
| `0x00320ac8` | `WarriorAttackTactic_OnEvent` | Event | 19 re-gives all goals; 22 (`+8` = 1) re-gives with that member | confirmed (code) |

### WarriorSteal (type 0x26) {#t2-warrior-steal}

The steal / wreck command's tactic (`WarriorStealTactic_Create` `0x00320b60`, vtable `0x005441c0`). Near a store flag
(activity `0xe`, within 12 m and reachable) its zone's objects are clustered and the Warriors steal from it; with none
they wreck what is near (WarriorVandalSteal goal `0x83`). Fields: `+0x20` the store flag, `+0x24` the 2 s check, `+0x28`
the zone (−1 none), `+0x2a` the chief's line playing, `+0x2b` the answer said.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00320be0` | `WarriorStealTactic_Init` | constructor | base fields, vtable `0x005441c0`, no flag, zone −1 | confirmed (code) |
| `0x00320c68` | `WarriorStealTactic_ClusterZone` | helper | lists the zone's objects (`ObjZone_ListObjects`, 64), keeps those with `+0x109` set and clusters their positions (1.0, `Points_Cluster`); marks clustering active (`0x0051164c`) | confirmed (code) |
| `0x00320d30` | `WarriorStealTactic_GiveGoals` | helper | picks the store, then gives each living member but the chief his goal | confirmed (code) |
| `0x00320df8` | `WarriorStealTactic_GiveMemberGoal` | helper | class 221 → AvoidEnemies (7, 12, 12); the player-1 re-issue rule of WarriorAttack (FollowAndDefend); else WarriorVandalSteal (`WarriorVandalStealGoal_Init`) | confirmed (code) |
| `0x00321028` | `WarriorStealTactic_PickStore` | helper | keeps the current store flag while the chief is within 12 m; else the nearest flag of activity `0xe` within 12 m and reachable: its zone byte (`+0xd8`) becomes the zone and its objects are clustered; none → zone −1 | confirmed (code) |
| `0x00321180` | `WarriorStealTactic_SetZone` | helper | stores the zone; −1 also clears the flag | confirmed (code) |
| `0x003211a8` | `WarriorStealTactic_Start` | Start | gives the goals, adds idle fidget clips, notes the chief's line; next check now + 2 s | confirmed (code) |
| `0x00321240` | `WarriorStealTactic_End` | End | removes the idle fidget clips; clustering off | confirmed (code) |
| `0x00321268` | `WarriorStealTactic_Process` | Process | once, after the chief's line: a member answers `vandal_resp` (140) or, with a zone, `steal_resp` (141); every 2 s re-picks the store; always 0 | confirmed (code) |
| `0x00321328` | `WarriorStealTactic_OnEvent` | Event | 19 re-gives the member's goal; 22 (`+8` = 1) re-gives it with the player rule; 20 consumed | confirmed (code) |
| `0x00321540` | `WarriorSteal_ClaimObject` | helper | for a human, walks the clusters in order (`0x00433fd8` sort) and takes each cluster's nearest object not claimed by his gang (`+0xec` gang, `+0xf0` until); when no enemy stands within 1.5 m of its navigation point the object is claimed for 5 s, the cluster's user count raised and the object returned; else 0 | confirmed (code) |
| `0x003217a0` | `StealClusters_Reset` | reset | on a full reset (1, `0xffff`): sets the ten handles of each of the 20 entries at `0x006eb318` (`0x30` bytes) to −1 (inferred: the cluster records) | inferred |
| `0x00321808` | `StealClusters_ResetAll` | reset | calls `StealClusters_Reset` with 1 and `0xffff` | confirmed (code) |

### Attack sub-tactics {#t2-sub-tactics}

Short orders TacticAttack's Process starts every 7 s (`AttackTactic_StartSubTactic` `0x00307a10`,
[Tactics](ai.md#tactic-kinds)): a pool of 8 records of 0x30 at `0x006eb6f0` (mask `0x006eb6e0`). Record: `+0x00` the
gang, `+0x04` the time limit (−1 none), `+0x08` started, `+0x0c` the vtable (`+0x0c` Start, `+0x14` End, `+0x1c`
Process). Each Process returns 1 (done) when the leader is gone, cuffed or knocked out. The leader opens with a speech
command and a gesture (clips `0x2a3`/`0x2a4` in the fight stance, `0x2a7`/`0x2a8` out of it), and the order goes out
when his line ends. The order names are inferred from the lines.

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00321828` | `SubTactic_PoolClear` | helper | clears the pool mask | confirmed (code) |
| `0x00321838` | `SubTactic_Alloc` | helper | the first free of the 8 records | confirmed (code) |
| `0x00321890` | `SubTactic_Free` | helper | frees a record's bit | confirmed (code) |
| `0x003218d8` | `SubTactic_LeaderGesture` | helper | when the leader's actions clear and he holds nothing and is in no blocking state: queues a gesture clip (`GenToIdleAnimAction_Init`); 1 when queued | confirmed (code) |
| `0x003219a0` | `SubTactic_Start` | helper | makes the time limit absolute, calls the class's Start, marks started | confirmed (code) |
| `0x00321a00` | `SubTactic_Process` | helper | starts when needed; 1 once past the time limit, else the class's Process | confirmed (code) |
| `0x00321a70` | `SubTactic_End` | helper | calls the class's End | confirmed (code) |
| `0x00321a98` | `SubTactic_Reset` | reset | on a full reset (1, `0xffff`) clears the pool mask | confirmed (code) |
| `0x00321ab8` | `SubTactic_ResetAll` | reset | calls `SubTactic_Reset` with 1 and `0xffff` | confirmed (code) |
| `0x00321ad8` | `GrabSubTactic_Init` | constructor | gang, time, vtable `0x00544220` | confirmed (code) |
| `0x00321b00` | `GrabSubTactic_Order` | helper | the leader takes the first living human of class 221 in the human pool (`0x00640c80`, `0x6d0` each) unless he already holds goal `0x3f`: pops his fight goal (`0xf`) and pushes GrabTarget on him; 1 when ordered | confirmed (code) |
| `0x00321c50` | `GrabSubTactic_End` | End | pops the leader's GrabTarget goal (`0x1f`) | confirmed (code) |
| `0x00321cd8` | `GrabSubTactic_Process` | Process | orders once; done when the leader is cuffed or knocked out, nothing was ordered, or the grab goal is gone | confirmed (code) |
| `0x00321db8` | `ChargeSubTactic_Init` | constructor | gang, time, vtable `0x00544290`, how many members (`+0x10`) | confirmed (code) |
| `0x00321de8` | `ChargeSubTactic_Order` | helper | up to the count: free members (not busy, not attacked, no goal `0x3f`, top goal not `0x0b`) take their nearest valid enemy, drop the fight goal and get EngageEnemy (`EngageEnemyGoal_Init`, `0x8f`) | confirmed (code) |
| `0x00322050` | `ChargeSubTactic_Process` | Process | with two or more alive the leader says `attack` (1) and gestures (`0x2a4` / `0x2a8`); after the line, orders once | confirmed (code) |
| `0x00322188` | `GuardSubTactic_Init` | constructor | gang, no time limit, vtable `0x005442c8`, the back-off option `+0x17`; with a time, `+0x10` = now + time | confirmed (code) |
| `0x003221e8` | `GuardSubTactic_Order` | helper | the leader's formation: set 3 with min(2 × others, 9) slots, shape 3.0; members join and get FollowAndDefend (3.0) on him, with attack table `0x00511438` unless the option; with the option the leader backs off (FightBackOff goal) | confirmed (code) |
| `0x00322448` | `GuardSubTactic_End` | End | restores the formation set and its byte `+0x275`; clears the members' attack tables (no option) | confirmed (code) |
| `0x00322510` | `GuardSubTactic_Process` | Process | the leader says `defend` (3) or, with the option, `follow` (2) and gestures (`0x2a3` / `0x2a7`); after the line orders once; past `+0x10` he says `attack` (1) with a gesture and the order ends (1) | confirmed (code) |
| `0x00322708` | `LeaderSubTactic_Init` | constructor | gang, time, vtable `0x00544300`, how many members | confirmed (code) |
| `0x00322740` | `LeaderSubTactic_Order` | helper | up to the count: free members with an enemy gang's leader in their enemy list engage him (EngageEnemy `0x8f`) | confirmed (code) |
| `0x00322988` | `LeaderSubTactic_Start` | Start | saves the gang's config field `+0x6c` and the global `0x00510b54`, sets them to 4 and `0x12` | confirmed (code) |
| `0x003229f8` | `LeaderSubTactic_End` | End | restores both | confirmed (code) |
| `0x00322a40` | `LeaderSubTactic_Process` | Process | the leader says `attack` (1) with a gesture; after the line, orders once | confirmed (code) |
| `0x00322b78` | `ThrowSubTactic_Init` | constructor | gang, time, vtable `0x00544338`, how many members, the pile option `+0x16` | confirmed (code) |
| `0x00322ba8` | `ThrowSubTactic_Order` | helper | objects within 30 m of the gang (`ObjectManager_FindObjects`, nearest first): with the option, objects of type `dyn_pile`, else of kind 4 or 5; each reachable one not claimed by the gang is claimed for 5 s (`+0xec`, `+0xf0`) and a free member (not attacked, no goal `0x3f`, top goal not ObjectPile `0x60` or GetItem `0x2c`) is sent: ObjectPile with the option, else GetItem (4) | confirmed (code) |
| `0x00323060` | `ThrowSubTactic_Start` | Start | without the option the count is every living member | confirmed (code) |
| `0x003230a0` | `ThrowSubTactic_End` | End | pops every member's ObjectPile goal | confirmed (code) |
| `0x00323160` | `ThrowSubTactic_Process` | Process | the leader says `throw` (10) with a gesture; after the line, orders once | confirmed (code) |
