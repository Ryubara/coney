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
| `0x00288cc8` | `Steering_FindBlocker` | helper | of a list of nearby humans, the nearest one ahead in the path (not the target), with its distance | confirmed (code) |
| `0x00288f40` | `Steering_ClampStep` | helper | the step toward a point, limited to the brain's speed (`+0x114`) × a factor | inferred |
| `0x00289010` | `Steering_TryDetour` | helper | when the human can walk straight to the detour point and it is not blocked (`0x00249050`): avoiding on, the point and its score stored | confirmed (code) |
| `0x002890a8` | `Steering_ResetAvoidance` | helper | clears the detour point, avoidance off, remembers the current node (`+0x28`, `+0x29`) | confirmed (code) |
| `0x00289108` | `Steering_EndAvoid` | helper | avoidance off, score 0 | confirmed (code) |
| `0x00289d68` | `Steering_RayHitsHuman` | helper | whether a ray of a given length passes within 0.63 m of a human, and at what distance | confirmed (code) |
| `0x00289ed0` | `Brain_GiveWayTo` | helper | a standing AI in the way of a mover (neither tackling the other, both able) picks a free 45° sector round him (`0x0029ea48`) and queues a `GiveWay` step there | confirmed (code) |
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
| `0x0028fa18` | `Brain_MaybeScanEnemies` | helper | re-scans enemies (`Brain_ScanEnemies`) when the gang changed or the interval passed (shorter in the fight stance) | confirmed (code) |
| `0x0028fae8` | `Brain_MaybeSpotPlayers` | helper | drops the target while busy (`0x1f80874000`); every 1.5 s (6 s in the fight stance unless `+0x333`) `Brain_SpotPlayers` | confirmed (code) |
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
| `0x0029e250` | `Sectors_Construct` | helper | constructs a human's eight-sector record (0x48 bytes at `0x006e8318`) | confirmed (code) |
| `0x0029e2b0` | `Sectors_Reset` | helper | marks all sectors stale, timer 0 | confirmed (code) |
| `0x0029e2e8` | `Sectors_ResetHeading` | helper | stores the current heading and marks sectors stale | confirmed (code) |
| `0x0029e350` | `Sectors_GetPoint` | helper | a point at a distance in a sector's direction | confirmed (code) |
| `0x0029e470` | `Sectors_GetOwner` | helper | the human a sector record belongs to | confirmed (code) |
| `0x0029e4b0` | `Sectors_ProbeWall` | helper | on first use, probes a sector and sets bit 4 when blocked | inferred |
| `0x0029e5d8` | `Sectors_Update` | helper | at intervals: the nearest human per sector; flags 3 under 1.5 m, 1 under 2.5 m, bit 8 for a nearby player-gang member | confirmed (code) |
| `0x0029e980` | `Sectors_GetCost` | helper | a sector's flags plus twice its human count | confirmed (code) |
| `0x0029e9c8` | `Sectors_IsBlocked` | helper | a near human, or a probed wall | confirmed (code) |
| `0x0029ea10` | `Sectors_IsWall` | helper | the wall bit | confirmed (code) |
| `0x0029ea48` | `Sectors_IsFree` | helper | no human and no wall | confirmed (code) |
| `0x0029eaa0` | `Sectors_AllClear` | helper | no sector has a human within 2.5 m (used by the Warriors' pick-up check) | inferred |
| `0x0029ead8` | `Sectors_IsHeldBy` | helper | whether a given human is the near one in a sector | confirmed (code) |
| `0x0029eb10` | `Sectors_SectorOf` | helper | the sector of a point relative to a human's heading | confirmed (code) |
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
| `MoveMelee` (2) | `0x005431e0` | `0x002fcf50` | `0x002fcff0` | `0x002fd068` | `0x002fd4e8` | keeps a fight distance band from a human |
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
| `0x00303f10` | `CivlCoDiBrain_Think` | type 6 think | think of the `civl_co_di` kind | confirmed (code) at the table `0x00715568` |
| `0x00304030` | `Brain_IsFreeToFight` | type 6 helper | alive and without a fight goal | confirmed (code) |
| `0x00304228` | `CivlCoDiBrain_OnEvent` | type 6 event | events of the `civl_co_di` kind | confirmed (code) at the table `0x007155a0` |
| `0x00304c78` | `GangBrain_OnCrimeSeen` | type 2 crime witness | a gang member who sees a crime by someone hostile to him (`0x00290138`) and has no goal `0x9a` reacts toward the offender's position | confirmed (code); the reaction not traced |
| `0x00304fa8` | `GangBrain_OnEvent` | type 2 event | gang members' events | confirmed (code) at the table `0x007155a0` |
| `0x003052f0` | `WarriorBrain_Think` | type 3 think | [The Warriors' pick-ups](ai.md#warrior-pickups) and the fight push | confirmed (code) |
| `0x00306040` | `WarriorBrain_OnPrompt` | type 3 helper | triangle on a Warrior: when both are free and in reach (`0x0021c0a8`), faces him; line `0x92` as the player's | confirmed (code) |
| `0x00306190` | `WarriorBrain_OnHitByChief` | type 3 helper | a Warrior hit by his chief hits back with an `AttackTarget` goal (type 9, `0x002aee48`), counted in the chief's brain `+0x2e4` | confirmed (code) |
| `0x003063b0` | `WarriorBrain_OnEvent` | type 3 event | Warriors' events | confirmed (code) at the table `0x007155a0` |
