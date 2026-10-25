---
kind: topic
system: combat
owns:
  functions: [
    "0x0021c570", "0x0023a1b0", "0x0023cf88", "0x0023d2b8", "0x0023f5e0", "0x00241b90", "0x00254418", "0x00264178",
    "0x00264a80", "0x00275678", "0x00276008", "0x002761c8", "0x00279f50", "0x0027a070", "0x0027a120", "0x0027a4b0",
    "0x0027a6c0", "0x0027e6d8", "0x00290e78",
  ]
---
# Combat: targets, AI attacks and breakables

Verified against: `SLUS_212.15` (NTSC-U); runtime claims name PCSX2 2.9.94 and their method in How this was checked.

## Summary

This page covers which human or object each attack goes for, the fight stance and the lock, how an attack turns and
slides onto its target and where the attacker stands when it lands, a player hitting his own crew, how an AI attacks,
breakable objects and the stereo theft. A player sees it as whom his punch turns toward, when he squares up to an
enemy and walks round him, how close he stands when the hit lands, what happens when he hits his own Warriors, when
the gangs he fights may swing at him, and the glass, cabinets, fences and car radios a punch or a triangle opens.
Every attack ([Combat moves](combat-moves.md)), the grab's search ([Grabs](combat-grabs.md)) and the AI's fight goals
([AI](ai.md#check-attack)) start from the target this code picks. The commands, the state bits and the tables every
combat topic reads are on [Combat](combat/index.md#data).

## Map

No source file names this code: it lies in the player's combat code in `Human/`, after `cns/cnsplayertag.cpp`
([Source map](source-map.md#position), inferred), and the names are ours. Every function is in the [code
index](combat/index.md#code-targets), and every combat function, with its role and level, is on [the combat function
list](combat/functions.md); these are the entry points and the functions this page owns, each read in Ghidra at its
address.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0027a6c0` | `Player_PickTarget` | the target of an action: humans, cars, world objects and glass ([Target selection](#targets)) | confirmed (code) |
| `0x0027a4b0` | `Player_FindAttackTarget` | a chain step's or a special's search at the id's far range ([Which target each move goes for](#targeting)) | confirmed (code) |
| `0x00279f50` / `0x0027a120` / `0x0027a070` | `Target_CarFilter` / `Target_ObjectFilter` / `Target_GlassFilter` | the pick's filters for cars, world objects and glass panes ([Target selection](#targets)) | confirmed (code) |
| `0x0027ce90` / `0x0027d5a0` | `Player_UpdateSprint` / `Player_UpdateStanceOrIdle` | the fight stance each update ([The fight stance](#fight-stance)) | confirmed (code) |
| `0x0027b1a0` | `Player_FindNearestEnemy` | the stance's nearest threat within 3 m | confirmed (code) |
| `0x00280158` / `0x00241b90` | `Player_LockOn` / `Human_FightStanceMove` | the lock and the lock-on movement | confirmed (code) |
| `0x0023a1b0` | `Human_SetCombatMode` | the script's and the AI goals' way into the stance (`HuSetCombatMode`) | confirmed (code) |
| `0x00264a80` | `Player_StartCharge` | plays 23, 24 and 501 with a warning to the target and no steer ([The fight stance](#fight-stance)) | confirmed (code) |
| `0x002625a8` | `Attack_Start` | starts an attack and its steer ([The steer into an attack](#attack-steer)) | confirmed (code) |
| `0x002761c8` / `0x00275678` | `Attack_SteerToTarget` / `Attack_SteerLed` | the turn and slide into an attack, and its led variant ([The two steers](#two-steers)) | confirmed (code) |
| `0x00276008` | `Attack_TurnToTarget` | the turn alone, beyond the far range | confirmed (code) |
| `0x0023cf88` / `0x0023d2b8` / `0x0023f5e0` | `Human_TurnToOver` / `Human_MoveToOver` / `Human_ApplyTurnAndSlide` | the steer's turn and slide, and their application each step | confirmed (code) |
| `0x00254418` | `AttackTable_GetOffset` | a kind's direction offset (record `+0x160` + kind × 16, a unit vector in thousandths) when its reach is set | confirmed (code) |
| `0x00306190` | `WarriorBrain_OnHitByChief` | a Warrior hit by his chief hits back ([Teammates](#teammates)) | confirmed (code) |
| `0x00219b08` | `Human_OnPushContact` | a head-on push and the swap of places | confirmed (code) |
| `0x00224778` | `Human_CanStartAttack` | whether an attack kind may start now ([An AI's attacks](#ai-attacks)) | confirmed (code) |
| `0x00290e78` | `Brain_SetAttackableTime` | when the target may be attacked again | confirmed (code) |
| `0x00264178` | `Player_ObjectAttack` | the clip of a strike at an object ([Breakables](#breakables)) | confirmed (code) |
| `0x0021c570` | `Human_CanSeeObject` | the pick-up search's line of sight | confirmed (code) |
| `0x0027e6d8` / `0x0027e908` | `Player_UpdateTheft` / `Theft_UpdateStereo` | the stick games, the car radio among them ([The stereo theft](#stereo-theft)) | confirmed (code) |

## Data

The target, the stance and the lock are kept in fields that [Combat](combat/index.md#data) owns: the state bits
`0x1`-`0x8` of the stance and the lock ([State flags](combat/index.md#state-flags)), the target handle at human `+0xc8`
and the other [record fields](combat/index.md#record-fields), the attack table's reach and far range ([The Anim Range
List](combat/index.md#damage-table)) and the distances of `CfgDistances` ([Constants](combat/index.md#constants)). The
fields this page adds, each read in the function named:

| Field | Meaning | Evidence |
| --- | --- | --- |
| record `+0xd4` | the pick's aim heading (`Player_PickTarget`) | confirmed (code) |
| global `0x0051096c` | the pick's cone, 0.9425 rad (54°) each side, then 2.356 rad (135°) (`Player_PickTarget`) | confirmed (code) |
| attack table `+0x160` + kind × 16 | `+0x0` / `+0x2` the direction offset in thousandths, `+0x4` the reach, `+0x8` the far range in mm, `+0xc` the flags (`AttackTable_GetOffset`, `0x002544a0`, `0x00254508`, `0x00254d60`) | confirmed (code) |
| human `+0x1a8` | the gait, set from the speed by `Human_SetMoveVelocity` | confirmed (code) |
| human `+0x304` / `+0x308` / `+0x332` | the steer's turn: time left, rate and on flag (`Human_TurnToOver`) | confirmed (code) |
| human `+0x2e0` / `+0x300` / `+0x310` / `+0x331` | the steer's slide: velocity, time left, goal and on flag (`Human_MoveToOver`) | confirmed (code) |
| human `+0x1e0` / `+0x500` / `+0x255` | slot point 0 (the head in the world), bone 6 in the model's frame, and the dirty flag that refreshes the slots (`Human_UpdateLedSlots`) | confirmed (code) |
| human `+0x56c` / `+0x570` | the push contact's deadline and first time (`Human_OnPushContact`) | confirmed (code) |
| Warrior brain `+0x2dc` / `+0x2e4` | the chief's hit window and its count (`WarriorBrain_OnHitByChief`) | confirmed (code) |
| target brain `+0x14a` / `+0x14b` | the attack spacing bytes, standing and down (`Brain_ClaimAttackSlot`) | confirmed (code) |
| type word `+0x5e` | the world object's body flags that make it a strike target (`Obj_CreatePhysicsBody`) | confirmed (code) |

## Behaviour

### Which target each move goes for {#targeting}

Read in Ghidra: confirmed (code) where no other level is given.

Angles are measured from an **aim heading**; distances are flat squared distances between the humans' positions;
"nearest" sorts by that distance (`SortByKeyAscending` `0x003868d0`).

| Move | Target it uses |
| --- | --- |
| Square in the stance (`S1`, the low, mid and held strikes, the tandem, the counters) | the **current target** (human `+0xc8`), unless a pad player has none or it is beyond **2.5 m** (distance² above `0x005104c0`, 6.25 in play): then a fresh `Player_PickTarget(2.0 m)` |
| Square's moving attacks (23, 24, 193 from a run or walk) | a fresh `Player_PickTarget(2.0 m)` |
| Cross (`X1`, 23, 24, 194, 661, 120) | always a fresh `Player_PickTarget(2.0 m)` for a player (`0x00287a18`) |
| Armed square and cross | a fresh `Player_PickTarget(2.5 m)` |
| A chain step | a cross step (`SX2`, `XX2`, `SSX3`) re-searches with `Player_FindAttackTarget(h, next id)` when not locked; a square step keeps the target |
| Snaps 25 / 27 / 29 | their own search (`Player_FindSnapTarget`): nearest within 2.0 m and 45° of the stick that `TargetFilter_Enemy` accepts ([Combat](combat/index.md#attacks)) |
| The charge 0 and the dive 1 | `Attack_FindNearestInReach`: humans **between the reach and the far range** (3.29-4.11 m, 2.22-3.00 m) inside 54°, then objects and glass; the charge only warns it, the dive takes it and steers over 0.1 s ([Combat](combat/index.md#charge-aim)) |
| The special 653 and the strong grapple | `Player_FindAttackTarget` with the far range of id 0 (4.11 m) and of id 1 (3.0 m) ([Strong grapple](combat-grabs.md#strong-grapple)) |
| The spray 664 (circle + triangle) | `Player_FindAttackTarget(h, 664)` (`0x0027a4b0`, from `Player_Special` `0x00287730`): the far range of 664 itself ([The spray](combat/index.md#run-attacks)) |

**`Player_FindAttackTarget(h, id)`** (`0x0027a4b0`): one pass at the id's far range (not × 1.1) with
`TargetFilter_EnemyStanding` inside 54°, then objects and glass, then, with no current target, the any-angle and the
wide filters at the same range; nearest wins, else the previous target. Its aim heading is the stick's above 0.01,
else the facing turned toward the id's offset point (`Attack_GetOffsetHeading` `0x00254310`; the plain aim heading
when the id has no reach).

**The current target between attacks** is the stance's ([The fight stance](#fight-stance)): the nearest threat within
3 m is tracked (`Player_FindNearestEnemy`, `0x0027b1a0`); one within **2 m** enters the stance and becomes the target
(and with the street's `CfgAutoLockAndCombat` the lock); it is swapped for the nearest within 2 m once the target is
beyond 2.5 m; the lock bit goes beyond 13 m; the lock-on walk ignores a target beyond 2.5 m unless L1 (`+0x00` `0x8`)
holds it, and the stance drops it beyond 3 m. L1 pressed or held also picks one.

### Target selection {#targets}

Read in Ghidra: confirmed (code) where no other level is given.

**`Player_PickTarget(range, h)`** (`0x0027a6c0`), in order; the first pass that finds anything ends the search:

1. The current target is kept as it is only when `Target_ObjectFilter` (`0x0027a120`) accepts it, and that filter
   accepts only world objects: a **human target is always searched for again**.
2. Record `+0xd8` is zeroed and the aim heading written to record `+0xd4`: the camera-turned stick's angle when the
   stick is above 0.01 (`Player_GetAimHeading`, `0x0021d1a0`), else the facing. The cone `0x0051096c` is set to
   **0.9425 rad (54°) each side** (`Target_AngleFromAimHeading` `0x002790a8`: the absolute wrapped difference).
3. Humans within range × 1.1 inside the cone that `TargetFilter_Enemy` (`0x00279410`) accepts; then the same with
   `TargetFilter_EnemyStanding` (`0x00279568`), which differs only in taking humans in state `0xe0000` or with held
   flags `0x402000` (the knocked down). Both need a body, `Human_IsTargetable` (`0x00227d78`: not flag
   `0x100000000000`), a height difference of at most 2 m (`0x00510970`), not friendly, another gang (brain `+0x20c`),
   not down or dead, held flag `0x40` and state `0x1c00000000` clear.
4. A car (world `+0x844`), then world objects (world `+0x840`), then glass panes (world `+0x84c`), each through its
   filter (below).
5. With no current target only: humans within range × 0.9 **at any angle** (`TargetFilter_AnyAngle` `0x002797b0`, the
   same tests as the standing filter less the angle).
6. The cone widens to 2.356 rad (135°): world objects within range × 0.8 inside it, then humans within range × 0.7
   **at any angle**. `TargetFilter_Wide` (`0x002796a0`) has no angle test, so the 135° cone holds only for the
   objects; it tests a body, `Human_IsTargetable`, not friendly, another gang and the height, and skips state
   `0x100000000` (dead), held flag `0x40` and state `0x1c00000000`. Of the down-or-dead bits (`Human_IsDownOrDead`,
   `0x00227e60`, tests `0x180050000`) it refuses only the dead one: a human dying (`0x80000000`), knocked out
   (`0x40000`) or wounded (`0x10000`, set by `Human_StartWounded` `0x0022fa78`, tested by `Human_IsWounded`
   `0x00227d98`) passes. This pass runs with or without a current target.
7. The nearest found wins. With nothing found, the previous target is kept when it is not a car, has a body and is
   within range.

**Objects as targets** (`Player_PickTarget`, `0x0027a6c0`):

- **Cars** (world `+0x844`, `CarManager_FindTarget`, 1.0 m) come first among the objects, through `Target_CarFilter`
  (`0x00279f50`): within 3 × the angle (or any angle when `0x00279e00` says so) and not more than 2 m away in height. A
  car found ends the search, with its record copied to record `+0xe0`-`+0xec`.
- **World objects** (world `+0x840`, `ObjectList_FindInRange`, `0x0039ab20`) within the range (2.0 m for square), and
  at the 135° pass within 0.8 × it. They must also be beyond record `+0xd8`, which the pick zeroes.
  `Target_ObjectFilter` (`0x0027a120`) keeps an object when it has a body, is within the angle (54°, then 135°) and 2 m
  in height, is a world object (type mask `0x8`, `Object_AsWorldObject`) and its **body flags have any of `0x8`,
  `0x10`, `0x20`** (`Object_IsStrikeTarget`, `0x00396710`).
- **Glass panes** (world `+0x84c`, `GlassTaskManager_ListInRange`, `0x0038fc70`) within the range, through
  `Target_GlassFilter` (`0x0027a070`): body, angle and height only.

A world object's body flags come from its type's word `+0x5e` (`Obj_CreatePhysicsBody`, `0x00391d48`): bit `0x4` gives
`0x10`, `0x8` gives `0x8`, `0x20` gives `0x20` (and `0x1` → `0x2`, `0x2` → `0x4`, `0x10` → `0x40`, `0x40` → `0x2000`,
`0x80` → `0x10000`, `0x100` → `0x20000`, `0x200` → `0x40000`), on top of `0x80000500`. Kinds 29, 33, 34, 15, 26 and 31
add their own bits first. Which object types set those bits is on [World objects](objects.md). The square then plays
[`Player_ObjectAttack`](#breakables) on the object picked.

### Teammates: what the player may do to his own crew {#teammates}

Read in Ghidra: confirmed (code) where no other level is given.

"Teammate" is a human friendly to the player (`Human_IsFriendly`, `0x00222a90`, `Brain_IsFriendly`) and in his gang
(brain `+0x20c`).

1. **No pick ever names a teammate.** Every human filter of the player's searches refuses friends and the same gang
   before anything else: the attack picks (`TargetFilter_Enemy` `0x00279410`, `TargetFilter_EnemyStanding`
   `0x00279568`, the any-angle pass `0x002797b0`), the grab and tackle search (`Player_FindCounterTarget`,
   `0x0027ac30`, the same three filters in that order), the stance's nearest enemy (threats only) and the L1 lock
   (`0x002792c8`). So square and cross swing along the stick or the facing with no target and no steer.
2. **So a teammate cannot be grabbed or tackled.** `Grab_Start`'s intro ends in nothing without a target
   (`Grab_IntroEnd`, `0x0026c1d8`, returns at once when `Human_GetTarget` is empty): 71 then 69 `GRAB_MISS` plays even
   with a teammate right in front. No grab, so no throw and no mount of a teammate. The hangout's sparring Warriors
   are grabbable because they are another gang: `level99` creates them in `CombatWarriors` and calls
   `GangMakeEnemies(Warriors, CombatWarriors)` ([AI: the sparring fight](ai.md#level99-fight)).
3. **A swing that touches a teammate does land.** The strike test takes every body, allies too ([Moving
   strikes](combat/index.md#moving-strikes)), so `Strike_Contact` (`0x0021b290`) runs on the teammate: impact sound,
   hit effect, the damage stored as pending through `Human_DealDamage` (`0x0021d680`) with the event's hostile byte
   clear; with the player having no target, a free Warrior who may gesture and has more than 10 % health says speech
   command `0x58` (a complaint). In `Human_ApplyPendingDamage` (`0x00265f70`) the friend's pending damage is set to
   0 (an AI victim, or an AI attacker), so **no health is lost**, but the code goes on to the reaction by state
   (`Hit_PlayReaction` `0x0026b0a0` for a standing one): **the teammate flinches or falls** as from an enemy's hit of
   that hit code, and hit armour and the other gates apply as usual. Two players hurt each other for real.
4. **A Warrior hit by his chief hits back** (`WarriorBrain_OnHitByChief`, `0x00306190`, from the brain's event 1, which
   every hit sends): only when the hitter is a player whose Warrior commands are not locked (game state `+0x418` +
   player), the Warrior has no fight goal, is not cuffed, not sparring (`+0x2e5`) and counts no hostiles (`+0x152`). A
   hit with no window open opens one of 10 s (the Warrior's brain `+0x2dc` = now + 10000 ms; an expired one is cleared
   with its count, byte `+0x2e4`); each later hit inside it adds 1 to the count, and the hit that brings it to 3, **the
   fourth hit within 10 s** (or the second, while the chief's own brain byte `+0x2e4` shows another Warrior already
   hitting back), clears the window and makes him push **AttackTarget** (`AttackTargetGoal_Push(him, chief, 20000)`)
   for 20 s, after popping a TauntPlayer goal on top. When the chief is a war chief (`+0x3ac` = 1) with a command in
   force, the command is re-issued as 0 and the chief's command menu is locked (`+0x42e` + player = 1). While one hits
   back, the [player's update](ai.md#think-player) gives every free member within 15 m **TauntPlayer**: the taunting
   ring.
5. **Throws into teammates.** A thrown enemy strikes every body he flies into ([Throws](combat-throws.md#throws)): he is
   no friend of the teammate, so the bystander takes the crushing high reaction (300-303, a knockdown) with no health
   lost (the −60 is never applied).
6. **Walking and sprinting into a teammate** is the physics' contact 4: he is slid along and pushed by half the move
   ([Physics: human contacts](physics.md#human-contacts)); there is **no knockdown** from running into anyone (only the
   charge's capsule and an airborne body strike, `Human_OnContact` `0x00219d50`), and the charge's strike on a
   teammate is a hit as in 3. A player who walks head-on into a teammate at a jog or faster and is still doing so 2 s
   later **trades places** with him (`Human_OnPushContact` `0x00219b08` → `Human_CanPush` `0x002320a0`, which sets
   each one's position to the other's when neither is in a fight stance, busy or held). In detail, at `0x00219b08`: the
   contact counts only when the pusher is a player, his gait (`+0x1a8`) is 3 or more and his push is head-on (the dot
   of the two unit vectors below −0.8); the first such contact stores the time in the teammate's `+0x570` and a
   deadline of **now + 2100 ms** in `+0x56c`; a later counting contact at least 2000 ms after the first calls
   `Human_CanPush`, and a swap clears both. The second contact must also come before 2.1 s: every update
   `Human_StateUpdate` clears both words once the game time is past the deadline (`0x002405e0`-`0x00240604`), so the
   swap needs a counting contact in a window of 2.0-2.1 s after the first, about three updates.

#### Teammates, at runtime {#teammates-runtime}

Measured in PCSX2 2.9.94: confirmed (runtime) where no other level is given; none of the runs has a run record ([How
this was checked](#how-this-was-checked)).

Cleon was the player in level80 checkpoint 2, with Vermin, Rembrandt and Ajax following (`FollowPlayer`, goal 50), and
Vermin's record and brain were read every update:

- **Hits 1-3 land with a reaction and no health lost.** Cleon walked to 0.85 m from Vermin (stick 0.5, aimed at
  him), the stick went to 0.2 towards him for one update, then square was tapped (`ANIM_ATTACK_S1`, 12). Each tap
  logged `Strike_Contact(Cleon, Vermin)` and `Hit_PlayReaction` on Vermin, who played 272 (`ANIM_REACT_SMALL_HIGH_FRONT`)
  and kept 1800 health. This held for every hit before the fourth, in both runs.
- **Level80 locks the commands, and that blocks the hit-back.** In the first run (four taps 1 s apart, two of which
  reached him) `WarriorBrain_OnHitByChief` ran on each hit but opened no window: game state `+0x418` for player 0 was
  1, because `level80.lua`'s chapters 1-4 call `WCLockCommands(player, true)` in their set-ups and only chapter 4's
  clean-up unlocks it. **No Warrior hits back anywhere in mission 1 of `level80`.** For the second run the byte was
  written to 0, as `WCLockCommands(player, false)` leaves it.
- **The fourth hit starts the hit-back.** With the commands unlocked, five hits 24 updates apart: the first set
  Vermin's `+0x2dc` to the game time + 10000 ms; the second and third raised `+0x2e4` to 1 and 2; the fourth (2.4 s
  after the first) cleared both and called `AttackTargetGoal_Push(Vermin, Cleon, 20000)`. The chief's own brain
  `+0x2e4` became 1. The next update Vermin had **Fight** (`0xf`, target Cleon) on top of AttackTarget, on top of
  FollowPlayer.
- **The fourth hit hurts.** The fourth and fifth hits each took **22** health from Vermin (1800 → 1778 → 1756): once
  he hits back he is no longer treated as a friend, so the pending damage is applied (inferred: the friend test of
  `Human_ApplyPendingDamage` was not traced at runtime).
- **He fights for real.** Vermin's punches cost Cleon 209 health each (1530 → 346 in 22 s). Cleon's target became
  Vermin. The goal stack went back to FollowPlayer 661 updates (22.0 s) after the push, AttackTarget and Fight
  together.
- **Sprinting into a teammate knocks no one down.** Cleon sprinted at Vermin from 5.1 m with L2 held and the stick at
  full deflection, re-aimed at him every 2 updates and scaled so its larger axis is 1.0 (a diagonal of plain
  components falls under the sprint's 0.95). He reached gait 5 at 10.25 m/s. At contact the player stopped dead
  (0.06 m/s two updates later). Vermin played 402 (`ANIM_MOVEMENT_STEP_BACKWARD`), with no reaction, no state bits of
  a fall and no health lost. A run that met him at gait 4 (8.26 m/s, the sprint still speeding up) gave the same.
  `Human_OnPushContact` ran on Vermin at every contact.
- **Walking into one pushes him; a walking player seldom gets to the swap.** At a walk (stick 0.75 or 0.9 re-aimed,
  gait 2, 1.63 m/s) the player shoves him along at about half his speed, and an idle Warrior often steps aside first
  (400 `ANIM_MOVEMENT_STEP_RIGHT`). Running in (gait 3-4) started the timer: `+0x570` = the time, `+0x56c` = time +
  2100. But the contact brought the player down to gait 0-1, so no counting contact came 2.0 s later, and the
  deadline was cleared on time. `Human_CanPush` was never called in five runs: a player cannot keep a counting
  contact going for 2 s, so in play the swap is rare.
- **The swap itself** (a second pass with the commands still locked). Cleon ran at Vermin from 4.6 m, stick 1.0
  re-aimed every update, no L2: gait 3 from the 7th update, gait 4 from the 14th. At 1.46 m the test aged Vermin's
  timer over PINE (`+0x570` = the game time − 2050 ms, `+0x56c` = the game time + 400 ms), as a first contact 2.05 s
  earlier would have left it. Two updates later the contact came at gait 4: `Human_OnPushContact(Vermin, …)` then
  `Human_CanPush(Vermin, Cleon)` from `0x00219d18` (the return address of its call at `0x00219d10`, confirmed (code)),
  on the same update. Vermin's position became exactly Cleon's (−189.45, 168.24), Cleon's became Vermin's (about
  (−190.2, 167.7)), each kept his own heading, and both timer words were 0 the next sample. Vermin played no reaction
  and went on following.
- **Circle with only a teammate in reach is a grab miss.** Cleon walked to 0.81 m from Vermin (stick 0.5, aimed at
  him), the stick went to 0.2 towards him for one update, then circle was tapped. His target was none (`+0xc8` −1).
  `Grab_Start(Cleon, mode 0, base 72)` ran (hook `grab-start`) and 71 and 69 were queued the same update (hook
  `anim-dispatch`); 21 updates later (71's 5 and 69's 16) he was back in the idle 388. Vermin played nothing and lost
  no health.
- **The complaint is said.** Two square taps at Vermin from 0.77-0.83 m, Cleon with no target: each logged
  `Strike_Contact(Cleon, Vermin)`, `WarriorBrain_OnHitByChief` and reaction 272 on Vermin, and
  **`Human_SayCommand(Vermin, 0x58, …)`** (`0x002205e0`) from `Strike_Contact` (return `0x0021b4a0`, the call at
  `0x0021b498`, confirmed (code); hook `say-command`) on the same update. Vermin's health stayed 1800. Whether the line
  is heard ([Open questions](#open-questions)) was not checked.

### The fight stance {#fight-stance}

Read in Ghidra: confirmed (code) where no other level is given.

**The fight stance is record `+0x00` bit `0x1` or `0x2`** (`0x00228340` tests `0x3`):

- **Who sets it.** `Human_EnterFightStance` (`0x0022fe80`) sets both bits; only AI goals and the script call
  `HuSetCombatMode` (`Human_SetCombatMode`, `0x0023a1b0`) use it. A player gets **`0x1` alone** from `0x00280068`
  (`0x00230140`, then `0x1` if clear), which every attack start of the player's runs (square, cross, the dive, the
  grab or tackle, the grounded strike `0x00287fe0`, and the clip handler `0x0025de20`), and from his per-update
  stance logic below; L1 and the auto-lock add `0x4` with it (`0x00280158`: `+0x00` `0x5` and the lock-on movement
  state). `Human_LeaveFightStance` (`0x0022fef0`) clears `0x7`; the player's own exits (`0x002801e8`, `0x00280118`)
  clear `0x5`. The stance timer `0x002800b8` is unused: its seconds `0x00510994` are −1.
- **The player's stance each update** (`Player_UpdateSprint`, `0x0027ce90`, through `Player_UpdateStanceOrIdle`
  `0x0027d5a0` from the dispatcher after its busy gate, for a pad player (per-player `+0x1b`) outside the throw aim
  (`Human_MoveThrowAim`, `0x00244770`), the state word's `0xf0000` and a scene; an AI-controlled player runs `0x0027cd50`
  instead, and a scene drops the target). Distances are compared squared; "drop the target" is `0x00226f70` (refused
  while locked, state `0x8` and `0x4`) and "take" is `0x00226c30` (refused while locked, and for a human the target
  filter `0x00227d78` rejects). Confirmed (code), in this order:
  0. **The nearest enemy** (`Player_FindNearestEnemy`, `0x0027b1a0`): the humans within **3 m** (`0x005104c4`, 9 in
     play) that the enemy filter `TargetFilter_StanceEnemy` (`0x002791b0`, threats only: "What an attack does to the
     lock" below) accepts, nearest first, kept if `Human_IsClearToTarget` (`0x0021c240`, a capsule cast from the player
     toward him through the physics, `IPhysics_CollideShape`) finds nothing in the way, else the current target as it is
     (no down test on that path); with none found, the current target unless it is out (state `0x80000000`, which
     `Human_PlayDeathReaction` sets, or `0x100000000`, or knocked out, `0x00227dd8`: state `0x40000`). **That fallback
     is how the target an attack picked gets locked**: within 2 m it passes step 3 as "the nearest" even when it is no
     threat (confirmed (runtime) below). It also writes record `+0xd4` / `+0xd8`. Then, the search already made, a
     current target in one of those three states or in a scene (`Human_IsInSceneState`) is dropped (and the lock bit
     `0x8` cleared) unless his `+0x08` holds `0x400000`; when he was the nearest, there is none. A target knocked over
     with health left (state `0x80000`) is **kept** (confirmed (code) at `0x0027ce90`; confirmed
     (runtime), run `r-20261025-target-knockdown-kept`, below).
  1. **The sprint**: state `0x1000000` is cleared, then set again while L2 is held with stamina (record `+0x14a`)
     and `+0x08` free of `0x10`, which also clears `0x8008` and drops the target ([Sprint](characters.md#sprint)). A
     target beyond **13 m** (169) loses the lock bit `0x8`.
  2. **Running drops it** (confirmed (code)): with L2 held, or gait 4 or 5 with `+0x08` clear (`Human_IsRunningFree`,
     `0x00223a60`, gait 4; `Human_IsSprintingFree`, `0x00223a98`, gait 5), or
     human `+0xe0` `0x8000000000` while not locked, nothing else happens this update; a player in a stance with
     `+0x08` clear also leaves it: `0x8008` cleared, target dropped, `0x00230140` (which ends state `0x200000`, see
     below), `Player_ExitStanceAndLock` (`0x002801e8`: `0x5` cleared, the combo count zeroed and, from the lock-on
     movement state, back to the free one), and a state code of `0xe` becomes 7.
  3. **An enemy within 2 m enters it** (confirmed (code); the distances are the values in play,
     [Constants](combat/index.md#constants)): when the movement state is not the lock-on one (`Human_FightStanceMove`),
     the state word lacks `0x200000`, and the nearest enemy is within **2 m** (`0x005104bc`, 4) or the player is locked:
     nothing with an object of set 4 in hand; else `0x1008000` cleared, `Player_EnterStance` (`0x00280068`:
     `0x00230140`, then `0x1` if clear; `0x00230140` clears state `0x200000`, setting `0x20000000` when he is then free
     and still, and zeroes record `+0x114`), the nearest taken as the target (when there is none yet and `+0x08` has
     nothing outside `0x320100`; and again when there is one), and unless state `0x200000` or human `+0xe0`
     `0x200000000000`, the lock (`Player_LockOn`, `0x00280158`: state `0x5`, and the lock-on movement state through
     `0x00221038`).
  4. **It holds** (confirmed (code)) while L1's release window runs (record `+0xf4`: `Player_OnL1Released` `0x00227b98`
     sets it to the game time + 264 ms; the other writer found is the hidden update's L1 release at `0x0027e14c`, the
     same + 264 ms (inferred: the writers were not enumerated by a full search), so in a fight it has long run out) and
     a target is kept; or, with the nearest within 2 m and the target beyond **2.5 m** (`0x005104c0`, 6.25 in play), the
     target is swapped for the nearest when `+0x08` is clear; or the target is within **3 m** (`0x005104c4`, 9 in play).
  5. **It ends** once that window has run out, `+0x08` is clear and he is neither locked nor in state `0x8` with
     `0x200000` (`0x00227cd8`): the target is dropped; a stance in the lock-on or the free movement state is left
     through `0x002801e8` (a state code `0xe` → 7). Then, outside state `0x200000`, the nearest human on the brain's
     list `+0x164` within **20 m** that is an alerted (brain `+0x21c` set) non-pad human inside a 120° cone of his
     facing (`Human_IsInFieldOfView` with 2.094 rad) keeps him in the stance **if he is standing** (gait 0):
     `Player_EnterStance` again. Otherwise a stance still on is left by `Player_ExitStance` (`0x00280118`: `0x5`
     cleared and the combo count zeroed, the movement state kept), and a moving player's state code `0xb` becomes 7.

So in play a player is in a fight stance **while locked on** (L1, or an enemy coming within 2 m with the street's
auto-lock), for the rest of any attack he starts, and while he stands facing an alerted enemy within 20 m; a run or
sprint ends it (inferred from the steps above, `0x0027ce90`). A locked-on combat walk keeps `0x1` (it moves at the
base speed, gait 3), so **square there skips the walk attack and goes to the snap or `S1`**, which is what level99's
lesson 7 needs ([Attacks](combat/index.md#attacks)). At runtime L1 held gave state `0xd` (confirmed (runtime), below).

**The whole path at runtime** (confirmed (runtime), PCSX2 2.9.129 (the PCSX2-MCP fork), copies of slot 6,
the scenarios `target_*` of `repo:research/traces/scenarios/`, one sample per update; state is record `+0x00`, the
movement state human `+0x1c0`, the target human `+0xc8`). The settings in play were `CfgAutoLockAndCombat` 1 (`0x005104b8`,
the other three 0), near² 4, mid² 6.25, far² 9 (`0x005104bc`-`0x005104c4`) and `CfgStrafeRStick` 0 (`0x005104d0`).

| Scenario | What the original did |
| --- | --- |
| `target_attack_walk_away`: `S1` at PoizoCiv, a **neutral** civilian (his gang, kind `0x17`, no enemy of the player's) 1 m ahead; stand 40 updates; stick 60 % back | The press update: state `0x1`, target PoizoCiv. The next update, still in `S1`'s wind-up (`+0x08` `0x1`, outside the busy gate): state `0x5` and `Human_FightStanceMove`, at 0.74 m. Standing, the lock held. Backing off: clip 384 at 3.43 m/s, PoizoCiv's bearing within 1° of the facing to 2.39 m, the heading unchanged from 2.5 to 3 m; at 3.05 m state 0, target none, the free movement |
| `target_walk_past_friends`: walking at 60 % past Vermin (crew, the player's gang) at 0.69 m | Target none and state 0 on every update |
| `target_walk_past_enemy`: PoizoCiv's gang made an enemy (`Gang_MakeEnemies` `0x0016acf0`, his brain kept alerted, below), 1 m to the side of the walk | Standing (updates 0-4, the gangs made enemies at update 2): state 0. As the stick was pushed (updates 5-6, gait 0 then 1): state `0x1` with no target (step 5's alerted enemy, 2.9 m away), then 0 while the walk started. At 1.99 m (update 16) state `0x1` only (`+0x08` held the run start's `0x10000000`, outside `0x320100`); four updates later (update 20), `+0x08` clear, at 1.65 m: state `0x5`, target him, the lock-on movement. Walking on at 3.43 m/s facing him, the player circled him (heading 147° → −28° as the camera followed) and the target went at 3.06 m |
| `target_knockdown_kept` (run `r-20261025-target-knockdown-kept`): PoizoCiv made an enemy 1.5 m ahead, the player standing; at update 30 the game knocks him down (`Human_KnockDown` `0x0022f100`) | Locked from update 3 (state `0x5`, target him, `Human_FightStanceMove`); from update 31 his state word held `0x80000` with all 600 health, and the player kept him as the target, locked, on all 60 updates to the end |
| `target_bat_sweep_run`: a bat in hand (`Human_PlaceItemInHand` `0x00238540`), a run at full stick, square at 2.73 m from the enemy 1.2 m to the side | 501 took him as the target; state 0 throughout; the heading stayed at −29.1° through the clip (no steer, no turn); the run went on at gait 4 past him (0.72 m), the target kept and never locked, until the run was stopped by a wall 6.68 m on (gait 3), when it went |
| The same with square at 3.24 m (beyond the armed pick's 2.5 m × 1.1) | 501 with no target; the same straight run |
| level95 checkpoint 2 (loaded with `load-checkpoint`), Cochise of the crew's gang 3 held 1.23 m beside Cleon walking at 60 % for 40 updates | Target none and state 0 on every update |
| level95 checkpoint 2 (loaded with `load-checkpoint`), Cleon walked at 60 % from the checkpoint's spot (−184.9, 95.5) toward Ajax (crew, gang 2, standing at −177.5, 87.0); held at a corner near (−180.3, 90.0), as Coney's player is for a while on the same line, he was moved on over PINE to (−179.56, 88.51, heading −129.2), where Coney's player stood after the corner, and walked on into Ajax, stopping 0.71 m from him | Target none and state 0 on every update; Coney after this change walks the same line and stops 0.69 m from him with no target, where Coney before locked onto him from 1.6 m (the combat walk 381, turned to face him) |

In level95 the crew's gangs (2 and 3, kind `0x13`) and the player's (0, kind 0) hold no friend or enemy bit for each
other (gang `+0x38`, `+0x3c` read 0), so they are neutral: no friends (an attack may pick them) and no threat (the
stance never takes one). A civilian gang (kind `0x17` or `0x18`) is made neutral again every update while none of its
members is alerted (`Gang_CheckNeutralRule`, `0x00169d30`, confirmed (code) and seen: a write watch on the enemy mask
caught `Gang_MakeNeutralWithType` `0x0016c470` clearing it the update after `Gang_MakeEnemies`), which is why the
enemy scenarios keep PoizoCiv's brain `+0x21c` set.

**L2 ends the stance and the target at once** (`Player_UpdateSprint`, `0x0027ce90`): with L2 held
(`PlayerRec_IsPadHeld(rec, 1)`), stamina (record `+0x14a`) above 0 and record `+0x08` free of `0x10`, it sets state
`0x1000000`, clears `0x8008` and then calls `Human_DropTarget` (`0x00226f70`). That drop is refused only for a pad
player who is locked, which `Human_IsLocked` reads as state `0x8` **and** `0x4`; `0x8` has just been cleared, so the
drop always goes through, whatever L1 does. Then, with L2 held (with or without stamina), a player in the stance whose
record `+0x08` is entirely clear drops the target again and leaves the stance and the lock-on movement state
(`Player_ExitStanceAndLock`, `0x002801e8`: `0x5` cleared, control back to `0x00240e38`). The update runs whenever the
dispatcher passes its busy gate (`0x5c7fee0`), so the target goes **during an attack's wind-up or window** too; only
the stance bits and the lock-on movement wait for the clip's end, when `+0x08` clears.

At runtime (confirmed (runtime), copies of slot 6, `X1` at PoizoCiv pinned 0.95 m ahead, then the stick 100 % straight
back from update 20): with **L2 pressed at update 20**, the target went on the update L2 was read (21, `X1`'s
wind-up), state `0x1000005`; on the first update after the clip (43) the movement state was the free one and the
state word `0x1000000`, and Rembrandt turned to the stick at 22.5° per update and ran off (410 at 7.8 m/s by update
50), never toward the target. **Without L2** he stayed locked (state `0x5`, `0x00241b90`) and walked backward facing
the target (385, then 384, at 3.43 m/s) for 14 updates, until the target was dropped at 3.09 m, then turned and
ran. That drop is step 5 above: the first update with the target beyond the 3 m keep distance (`0x005104c4`, 9 in
play), L1's release window long run out (confirmed (code); `target_attack_walk_away` dropped it at 3.05 m the same
way). So a player who
holds L2 after an attack never walks toward the old target.

**Lock-on** is the fight stance's movement state `0x00241b90` (the normal one is `0x00240e38`): the human is
**locked** when it has a human or object target and either L1 is held (`CfgLockOn`), or `CfgAutoLock`,
`CfgAutoLockAndCombat` (1 in the street) or auto-combat is on. Locked, it **turns to face the target every update**
and the stick (dead zone 0.12) moves it without turning, the stick's angle from the facing (record `+0xdc`) picking
the combat-walk clips 380-387. Blocking stops the movement. A target farther than 2.5 m (`0x005104c0`, 6.25 in
play) is not dropped here but **ignored** unless `Human_IsLocked` (`0x00227c88`: state `0x8` and `0x4`, L1); so is one
whose type bits lack `0x100040`. Ignored, or with the settings not locking, the walk **keeps the heading**: the only
turn left is the right stick's, capped by `Human_MaxTurn`, and only with `CfgStrafeRStick` (`0x005104d0`, written only
by `Cfg_SetStrafeRStick` `0x0023a5f0`; 0 in play). With no target at all the movement hands control back to
`0x00240e38`. Confirmed (code) at `0x00241b90`; confirmed (runtime) in `target_attack_walk_away` (the heading did not
move between 2.5 and 3 m). The drop is the stance update's, at 3 m (step 5 above).

**L1**: pressed (`0x0027da10`) or held (`0x0027dc00`), it sets record `+0x00` `0x8`, enters the stance and picks a
target; released (command 8, `0x00227b98`), it clears `0xc` and opens a 264 ms window in which a second tap picks and
locks. The three handlers are the dispatcher's last commands (`Player_UpdateActions`, `0x0027c120`), after the stance
update and every other command, so none runs under the busy gate (`+0x08` & `0x5c7fee0`), with R1 held, in a hold,
for a hidden player (`Player_UpdateActionsHidden`, `0x0027e040`, instead), or on an update another command took; with
no command, or `Human_IsBusy` (`0x00228258`), the dispatcher clears `0x8008`. A set-5 object in hand skips the held
handler; the held handler itself does nothing while the per-player action delay runs (`PlayerRec_GetActionDelayLeft`),
under `+0x08` `0x2000000`, while locked or in the throw aim, and with a target at a free sprint; hidden, it drops a
target in state `0x3800f0000` instead of locking. The press acts only inside the release window (then searches and
runs the held handler). Confirmed (code) at the cited addresses, 2026-10-25. While aiming a throw
(`Human_MoveThrowAim`, `0x00244770`) L1 leaves the aim; with weapon type 5 and no enemy target it enters mode `0x13`
(`0x00227b30`). Confirmed (code); confirmed (runtime): L1 held gave state `0xd`, a target,
and the movement state `0x00241b90`.

**At runtime** (confirmed (runtime), PoizoCiv 2 m ahead as the target): the street has `CfgLockOn` = 0, so **L1 alone
does not lock**. With `CfgAutoLockAndCombat` written to 0, L1 held gave state `0xd` and a target, but the player kept
its heading while the target circled it at 3° per update, and walked in the combat-walk clips without turning to it.
With the street's `CfgAutoLockAndCombat` = 1 the same circling target was **tracked exactly**: the player's heading
followed it by the same 3° per update, the target's bearing staying at 0°. Record `+0xdc` (the stick's angle from the
facing, clockwise) picked the combat-walk clip as below (stick 35 % to 80 %, turned by the camera, so the angles are
not the script's; confirmed (runtime)):

| `+0xdc` | 18°, 354° | 27° | 79°, 88° | 120°, 143° | 185° | 329°, 348° |
| --- | --- | --- | --- | --- | --- | --- |
| Clip | 380 forward | 381 forward right | 382 right | 383 back right | 384 back | 387 forward left |

A target that the walk took beyond 2.5 m was dropped and the player went back to the walk start 413. By the code the
walk ignores the target beyond 2.5 m and the stance drops it beyond 3 m; `target_attack_walk_away` told the two apart
(the heading held from 2.5 m, the drop at 3.05 m; confirmed (runtime), above).

**Combat-walk speed.** `0x00241b90` normalises the stick vector (dead zone 0.12) and multiplies it by `0x00221710`'s
speed, so the stick's deflection does not change the speed (confirmed (code)). The clips 380-387 each cover 2.4 m in
0.7 s, **3.429 m/s**, with record `+0x164` × `+0x3a4` = 1.0. At runtime the player moved at 3.43 m/s from the first
update at 35 %, 60 %, 80 % and 100 %, with no ramp; slower values came only while pushing into the target. The strafe
ids 31 / 32 / 33 move at 2.358 / 2.613 / 4.236 m/s. Confirmed (runtime).

**Backward** (confirmed (runtime), each run from a fresh copy of slot 6 with L1 held and the civilian locked 1.2 m
ahead): straight back at 35 %, 60 % and 100 % alike, the walk went into 385 (back left, the stick's angle from the
facing 208° at first) at **3.12, 3.12, 3.11, 3.10, 3.09 m/s** for 5 updates, then 384 at **3.429 m/s** to the end
(2 s, 4 m). Back left at 35 % (253° at first) started at 3.38-3.28 m/s for 5 updates, then 3.429. So the speed never
depends on the deflection, and the backward walk is the same 3.429 m/s once it runs; only its first 5 updates (1/6 s)
out of the stance are 1-10 % slower, more so the closer the stick is to straight back. Runs made one after another
without reloading, where the player walked into other bodies and objects, gave 2.6-3.0 m/s (one of them dipped to
2.54 and 3.08 m/s for two updates; inferred: the bodies, not the walk). What slows the first 5 updates is open
([Open questions](#open-questions)). The velocity the code asks for is the stick's direction × 3.429 every update
(`0x00241b90`, record `+0x164` = 3.429, human `+0x3a4` = 1.0, `0x005101e0` = 1, all read at runtime).

**In a grab** the stick does not walk at all below 0.95: the grab's movement (`0x00245310`) acts only above that
magnitude and then walks the pair backward at 1.125 m/s (front hold) or about 1.22 m/s (rear hold), as in [Moving a
grab](combat-grabs.md#grab-turn). So a 35 % stick moves a grab not at all, a fight stance at 3.429 m/s, and the free
player at the walk speed.

**What an attack does to the lock** (the armed run sweep 501 and the moving attacks):

- **The pick is not a lock.** `Player_Square`'s armed branch (`0x00286cc8`, held set 1-3) calls
  `Player_PickTarget(2.5 m)` and stores the result as the target (`Human_SetTargetObject`) for a pad player, at a run
  too; the unarmed 23 and 24 do the same with 2.0 m. None of them calls `Player_EnterStance` or `Player_LockOn` on
  that path: the armed run branch goes straight to `Player_StartCharge(h, 501)`.
- **No pull and no turn.** `Player_StartCharge` (`0x00264a80`), which plays 23, 24 and 501, only warns the target
  (`Human_WarnOfAttack`) and pushes the clip holding `0x1000000`: no `Attack_SteerToTarget`, no turn, no slide.
- **No stance logic during the clip.** `0x1000000` is inside the dispatcher's busy gate `0x5c7fee0`
  (`Player_UpdateActions`, `0x0027c120`), so `Player_UpdateStanceOrIdle` / `Player_UpdateSprint` (`0x0027ce90`) do
  not run while 501 plays: nothing enters the stance or locks during the sweep.
- **After the clip, running keeps it free** (confirmed (code)). The gait is human `+0x1a8`, set from the speed by
  `Human_SetMoveVelocity` (`0x0023cf00`). With the run resumed (gait 4, `+0x08` clear) `Human_IsRunningFree` holds,
  `Player_UpdateSprint` returns early (and, as the player is not in a stance, drops nothing). The target stays in
  `+0xc8`, but the movement state is still the free `Human_PlayerLocomotion` (`0x00240e38`), which turns to the
  stick: a stored target alone never steers the player. Only `Player_LockOn` (`0x00280158`) installs the lock-on
  movement `Human_FightStanceMove` (`0x00241b90`), and only from step 3 of the stance update or L1. Step 3's "nearest"
  falls back on the stored target, so **once he is not running and the target is within 2 m, it locks**; the 501 run
  of `target_bat_sweep_run` never did (confirmed (runtime)).
- **The lock-on movement faces the target only within 2.5 m** (confirmed (code)). `Human_FightStanceMove` treats the
  target as absent
  when its distance² is above `0x005104c0` (6.25 in play, 2.5 m) unless `Human_IsLocked` (state `0x8` and `0x4`, L1;
  the auto-lock's state `0x5` is not enough), or when the target lacks type bits `0x100040`; then it keeps the
  heading (only the right stick turns it, with `CfgStrafeRStick`, off in play). With no target at all it hands
  control back to `0x00240e38`. The stance update drops the target beyond 3 m (step 5).
- **Only a threat is found by the stance** (confirmed (code)). The stance's nearest-enemy search
  (`Player_FindNearestEnemy`, `0x0027b1a0`, 3 m) filters with `TargetFilter_StanceEnemy` (`0x002791b0`): the other
  must be a human
  whose object query (vtable `+0xf4`) holds, targetable (`0x00227d78`: not flag `0x100000000000`), not friendly
  (`Human_IsFriendly`, `0x00222a90`), not a pad player, within 2 m in height (`0x00510970`), **a threat**
  (`Human_IsThreatTo`, `0x00222a48`: `Brain_IsThreat`, the two gangs are enemies or both are players), and neither
  down nor dead (`0x00227e60`), nor in state `0xe0000` (`0x00223b48`), nor holding `0x40` (`0x00223c10`). A neutral
  civilian, whose gang is no enemy of the Warriors, is refused, and so is a crew mate, so **walking past a civilian or
  the crew never enters the stance or locks** (user item 14; confirmed (runtime) above); one does once his gang has
  been made an enemy (a civilian of kind 3 who fights back, `Hostile_Attack`, [AI goals](ai-goals.md)). The attack
  picks are wider: `TargetFilter_Enemy` (`0x00279410`) and `0x00279568` skip only allies and the same gang, so square
  still picks and hits a civilian, and the stance's fallback then locks onto him while he is within 2 m (confirmed
  (runtime), `target_attack_walk_away`).

### The steer into an attack {#attack-steer}

Read in Ghidra: confirmed (code) where no other level is given.

**Turning into an attack** (`Attack_Start`, `0x002625a8`): with the target within the attack's far range, it tells
the target (`0x0021d5c0`) and steers with `0x002761c8`: turn and slide so that the target, moved by its velocity over
the time to the hit + 0.1 s, sits at the clip's reach (+0.07 m for a target scaled above 1.1, else -0.1 m from behind).
Beyond the range it only turns (`Attack_TurnToTarget`, `0x00276008`), capped at 8.

**The turn does not face the target**: it turns so that the attack's **direction** points at it. The direction is the
record's offset (`+0x0` / `+0x2`, read by `AttackTable_GetOffset` `0x00254418`), a unit vector in thousandths in the
attacker's frame (y forward; x right, inferred from 25 and 27): forward (0, 1000) or within a few degrees of it for the
standing, moving and ground attacks (193: 7.6° right), (999, −12) right for 25, (−1000, 0) left for 27, (−39, −999)
back for 29, and for the strafes 31 / 32 / 33 3° / 9° left and 6° right of forward. The new heading is the goal's
heading minus the direction's.

**The steer in detail** (`Attack_SteerToTarget`, `0x002761c8`):

- **Its inputs.** `Attack_Start` passes the human, the target (`0x00226e60`, its locked target) and a time `T` = the
  time to the clip's **first event** of types 9, `0xf`, `0x13`, `0x2c`, `0x34`, `0x36` or `0x41` (its frame / 30
  divided by the clip's rate; with none, the clip's length; `0x00101658`). The steer clamps `T` to that time + 0.1 s,
  so `T` is the time to the first event. The per-kind numbers come from the record's attack table (record `+0x160`,
  16 bytes per kind, [Data](#data)); a far range not above the reach is taken as reach × 1.25 (`0x00254508`).
  `Attack_Start` steers only when a target is locked and its distance (`0x00229960`) is within the far range; a kind
  with flag `0x8` takes the led steer instead ([The two steers](#two-steers)).
- **The goal.** The target's position plus its velocity × (`T` + 0.1), the lead cut as in [Where the attacker stands
  at contact](#reach), minus the reach along the line from the human: the human should stand at the reach from where
  the target will be.
- **The turn** (`Human_TurnToOver`, `0x0023cf88`): the heading to the goal minus the current facing, wrapped to
  (−π, π]; skipped under 0.01 rad. It stores the rate (angle / `T`) in human `+0x308` and `T` in `+0x304` (flag
  `+0x332`).
- **The slide** (`Human_MoveToOver`, `0x0023d2b8`): a velocity (goal − position) / `T` in human `+0x2e0`, with `T` in
  `+0x300` (flag `+0x331`); none when the goal is under 0.01 m away (snapped), none at 13 m or more, and none when it
  would exceed 50 m/s with `T` above 1/30 s.
- **Applied** each character step by the human's state update (`0x0023fea8` → `Human_ApplyTurnAndSlide`
  `0x0023f5e0`): the rotation turns by rate × dt (dt = `0x005102cc`, 1/30 s; the last step only by the time left),
  and the slide velocity is copied to `+0x2f0` (scaled by the time left / dt on the last step), which moves the body on
  top of the clip's root motion (inferred: the add itself was not read, [Open questions](#open-questions)). Both count `T`
  down by dt and stop at 0. So the steer lasts **ceil(`T` / dt) updates**, at a **constant rate**: no easing.
- **When it starts.** `Attack_Start` runs in the dispatcher (step 9 of [Humans_Update](tasks.md#humans-update)), after
  the human's state update (step 8), so the first turn and slide show on the update after the attack's clip starts.

At runtime (confirmed (runtime), `combat_cross`, X1 at a target 1.36 m away): clip 11 starts on step 42 with the body
still; from step 43 the heading turns **2.533° per update for 9 updates** and 0.633° on the 10th (step 52), 23.43° in
all, then stops, on the update the chain window opens (phase 2, event `0x2c`). That is a rate of 76°/s over `T` ≈
0.308 s (9.25 updates): `X1` has flag `0x8`, so this is the led steer's time, the first event + 0.1 s ([The two
steers](#two-steers)). The body moves at 0.53, 0.71, 0.73, 0.74, 0.67, 0.77, then 1.34, 1.34, 1.25, 0.81 m/s (the
clip's root motion plus the slide), and the distance to the target falls from 1.364 to 0.825 m.

#### The two steers {#two-steers}

Read in Ghidra: confirmed (code) where no other level is given.

There are two steer functions, and the attack record's **flag `0x8`** (record `+0x160` + kind × 16 `+0xc`,
`AttackTable_GetFlags` `0x00254d60`) picks between them:

| | `Attack_SteerToTarget` (`0x002761c8`) | `Attack_SteerLed` (`0x00275678`) |
| --- | --- | --- |
| Used by | `Attack_Start` and `Attack_StartSnap` for a kind **without** flag `0x8`; always by `Attack_StartAtTarget` (`0x00261c80`, the specials from `Player_Special`), `Attack_StartSolo` and the other callers | `Attack_Start` (`0x002625a8`), `Attack_StartSnap` (`0x00264460`), `Attack_StartGrounded` and `Revive_Start` for a kind **with** flag `0x8`, when the word `0x005102c4` is non-zero |
| Slide time | the caller's time, cut to the first event + 0.1 s. `Attack_Start`, `Attack_StartAtTarget` and `Attack_StartSolo` pass the time to the first event (`0x00101658`), so the slide lasts **the time to the first event**; `Attack_StartSnap` passes 0.1 s | always **the time to the first event + 0.1 s**; the caller's time is not read |
| Target point | the target's position (its task `+0x10`) | the target's **slot point 0** (`Human_GetLedSlotPoint(target, 0)`, `0x00226aa0`) |
| Lead, reach, turn | the same in both: the lead by the target's velocity × (first event + 0.1 s), cut to 0.5 m; the reach along the line in x and y; the goal at the attacker's own height | |

`0x005102c4` is 1 on the disc and only read by these two callers (no write was found), so in practice flag `0x8`
alone decides (inferred from the callers). So **653** (flags `0x26`) and **645** (`0x23`), both started by
`Player_Special` through `Attack_StartAtTarget`, slide for their first event's time, while **`X1`** (`0x09`) slides
for the first event + 0.1 s. Neither the special's own `T` nor the event's type makes the difference; it is the steer.
The snaps 25, 27 and 29 (`0x1b` / `0x19` / `0x1b`) also have flag `0x8`, so they take the led steer too, and **not**
the 0.1 s that `Attack_StartSnap` passes to the other one.

#### The led steer {#led-steer}

Read in Ghidra: confirmed (code) where no other level is given.

`Attack_SteerLed` (`0x00275678`), for a kind with flag `0x8`:

- **Its target point is the target's head**, aim point 0 (`Human_GetLedSlotPoint(target, 0)`, `0x00226aa0`), not his
  position. `Human_UpdateLedSlots` (`0x0023cd30`) writes it: the human's world transform (vtable `+0xac`) composed with
  the transform at `+0x500` (`Transform_Compose`, the child in the parent's frame). `+0x500` is pose bone 6, the head,
  in the model's frame (x right, y forward, z up, from the feet, already times the human's scale; [Camera: the body
  point](camera.md#body-point)). So the offset is **turned by the human's full rotation**, not scaled again and **not
  flattened**: the point is at head height. It is not a formation offset and not the hips (`+0x4e0` is aim point 2).
- **Lead.** The target's velocity × (time to the first event + 0.1 s) is added to the head point: in full when it is
  at most 1 m long, or when the led point is nearer the attacker than the head itself; otherwise cut to 0.5 m long.
- **The point serves both the turn and the stand.** The standing point is the led head point minus the reach along
  the unit line from the attacker to it in plan; the turn (`Human_TurnToOver`) is to that line's heading (less the
  attack's own direction offset, `AttackTable_GetOffset`); the slide (`Human_MoveToOver`) runs over the time to the
  first event + 0.1 s.
- **How far off the position**, confirmed (runtime), PCSX2 2.9.94, a copy of slot 6: X1 pressed at update 41 at
  PoizoCiv standing 1.0 m ahead, facing the player, brain off, speed 0. On the update the steer started (43) the
  target played idle **388**, `+0x500` was (0.000, 0.018, 1.734) and aim point 0 lay **0.018 m from his position,
  straight toward the attacker** (0.000 m sideways); the stand point was 1.022 m from it in plan (the reach). Earlier
  in the same idle the head offset ranged over x −0.004 to 0.025, y 0.019 to 0.041 m as the pose breathed. In the
  reel 275 after the hit the head swung to (0.237, 0.126) and in the recovery 389 to (0.054, 0.129) m. The
  "0.016-0.036 m" figure is this idle (388 / 396). The point's z was 1.834 m above the transform table's z while
  `+0x500` gave 1.734 (inferred: the world transform from vtable `+0xac` sits 0.1 m above it; plan offsets match
  exactly).

**Slot point 0 is the target's head.** `Human_UpdateLedSlots` (`0x0023cd30`) sets slot 0 (`+0x1e0`) to the human's
`+0x500` transform composed with his world transform, and `+0x500` holds bone 6 (the head) in the model's frame,
the same value as bone-cache entry 6. Confirmed (runtime), `moves_reachX1_10_steer`:
`+0x500` equalled bone 6 to 0.1 mm in every update, (0.000, 0.016, 1.735) at the press. So the head of a standing
target that faces the attacker sits **0.016 m** in front of its position, toward the attacker, and the led steer's
goal was 1.0215 m from the slot point and **1.0375 m** from the target's transform position. That is the 0.02-0.03 m
measured below: the head's lean in the target's current pose, not an offset in the attack record (it changed from
0.036 m to 0.016 m over the 15 updates before the press as the idle pose moved).

**The head point moves with the pose.** `+0x500` is bone-cache entry 6 (`0x006b6880` + index × `0x470` + 6 × `0x20`)
copied as it is, in the model frame: x to the human's right, y forward, z up, from his transform position (bone 0,
the root, is not subtracted: it moves away with root motion while `+0x500` does not follow it). Slot 0 is not kept
up to date: `Human_UpdateLedSlots` recomputes the slots only when they are read (`Human_GetLedSlotPoint`, its only
caller) and the move is dirty (`+0x255`), so the led steer aims at the head **as it is when the attack starts**.
Confirmed (code) at `0x00226aa0` and `0x0023cd30`; confirmed (runtime), `moves_head_reel` (slot 6, a cross combo at
PoizoCiv standing 1.0 m ahead with his brain off; two runs gave the same values): slot 0 changed only on the updates
an attack started (43 for `X1`, 54 for `XX2`), each time to that update's `+0x500`. The head's offset from the
position, `+0x500` in metres:

| Pose (clip) | x (right) | y (forward) | xy length | z | Evidence |
| --- | --- | --- | --- | --- | --- |
| idle 388 / 396 (target, brain off) | −0.008 to −0.003 | 0.012 to 0.037 | **0.013-0.037** | 1.728-1.734 | confirmed (runtime) |
| fight idle 358 (the player, looping from update 88) | 0.024 to 0.051 | 0.075 to 0.165 | **0.09-0.165** | 1.540-1.558 | confirmed (runtime); for another human inferred |
| reel 275 after `X1` (updates 52-63) | 0.083 to 0.273 | 0.031 to 0.137 | **0.15-0.28** | 1.469-1.601 | confirmed (runtime) |
| reel 273 after `XX2` (updates 65-76) | −0.228 to 0.070 | 0.029 to 0.141 | **0.10-0.23** | 1.530-1.600 | confirmed (runtime) |
| recovery 389 (updates 78-96) | −0.028 to 0.079 | 0.020 to 0.140 | 0.02-0.16 | 1.61-1.73 | confirmed (runtime) |

Confirmed (runtime), from the table: the original's head really moves that far. In a reel it swings up to 0.28 m, mostly
sideways, and drops by up to 0.26 m; in the fight idle the head leans 0.08-0.17 m forward. `XX2` started at update 54,
two updates into the reel 275, and its steer aimed at (0.215, 0.132): **0.25 m** from the target's position, to his
right. The fight idle was measured on the player, whose model shares the humans' skeleton (inferred); an AI target held
in 358 was not recorded.

#### Where the attacker stands at contact {#reach}

Measured in PCSX2: confirmed (runtime) where no other level is given (PCSX2 2.9.94, no run record: [How this was
checked](#how-this-was-checked)).

**The steer sets the distance, both ways** (`Attack_SteerToTarget`, `0x002761c8`), confirmed (code) and (runtime).
The goal is the target's predicted point minus the reach along the line from the attacker, and `Human_MoveToOver`
(`0x0023d2b8`) slides the attacker there whatever the direction: a target nearer than the reach makes the attacker
slide **back**. The prediction leads the target by its velocity (the target's `+0x30`, zero when its `+0x54` has
`0x600`) × (the time to the first event + 0.1 s); when that lead is over 1 m and takes the target farther away it is
cut to **0.5 m** long. The reach gets +0.07 m against a target scaled above 1.1, else −0.1 m when the attacker is
behind it (`Human_GetSideOf` = 2). The slide only happens within the attack's far range (653: 1.97 m); beyond, the
attack only turns.

So in the original a free attack hits from a fixed stance whatever the starting distance, as long as the target is
within the far range. At runtime (PCSX2, slot 6, PoizoCiv standing still: brain off, its move speed 0, velocity 0;
the press update is k0; positions from the bone cache, `0x006b6880` + index × `0x470`, turned into world space with
the transform; confirmed (runtime)):

| Move, start | Distance after k1 / k2 / k3 | Then | Contact | Distance at contact (before / after the update) |
| --- | --- | --- | --- | --- |
| 653 from 1.0 m | 1.216 / 1.513 / **1.617** (slides back 0.62 m) | the clip's root motion: 1.455, 1.281 | **k7** | 1.172 / 1.120 |
| 653 from 1.6 m | 1.591 / 1.663 / **1.617** | the same | **k7** | 1.172 / 1.120 |
| 653 from 1.9 m | 1.779 / 1.738 / **1.617** | the same | **k7** | 1.172 / 1.120 |
| `S1` from 1.5 m | 1.319 / 1.176 (no back slide; reach 1.03) | | **k3** | 1.176 / 1.034 |
| `X1` from 1.0 / 1.3 / 1.6 m | a straight slide from k3 to k11 (below) | ends at 0.878-0.879 m | **k10** | 0.903 / 0.890, 0.976 / 0.930, 1.048 / 0.970 |
| `X1` from 1.9 m | turns only (the far range is 1.90 m): 1.886 at k3, 1.747 at k10 | | **miss** | |

Confirmed (runtime): from k3 on, the three 653 runs are the same to the millimetre: the steer ends at 1.62 m (the reach
1.58 m and 0.04 m more, not explained: 653 takes the steer that aims at the target's position, not its head, [The two
steers](#two-steers)) by k3, and the clip then carries the attacker 0.50 m forward to 1.12 m by the end of k7. The
shapes are on k2-k8 (653) and k1-k9 (`S1`, from the hook).

**Bones at contact** (confirmed (runtime)), in the attacker's frame (forward, right, height above the attacker's feet;
metres). The pose
before the contact update (k6's) and after it (k7's) bracket the test; the hands reach the target only in k7's
pose:

| Run | Pose | Left hand (19) | Right hand (25) | Target spine (3) | Target head (6) |
| --- | --- | --- | --- | --- | --- |
| 653, any start | after k6 | 0.78, −0.40, 1.21 | 0.78, +0.23, 1.22 | 1.22, −0.01, 1.18 | 1.16, −0.01, 1.74 |
| 653, any start | after k7 | 0.99, −0.19, 1.28 | 0.99, +0.14, 1.25 | 1.17, −0.01, 1.18 | 1.11, −0.01, 1.74 |
| `S1` from 1.5 m | after k2 | 0.73, +0.03, 1.61 | −0.03, +0.39, 1.32 | 1.21, +0.02, 1.18 | 1.15, +0.03, 1.73 |
| `S1` from 1.5 m | after k3 | 0.73, +0.02, 1.60 | −0.06, +0.37, 1.32 | 1.07, +0.02, 1.18 | 1.01, +0.03, 1.73 |

Confirmed (runtime): after k7 of 653 the hands are 0.24-0.28 m from the target's spine bone (0.51-0.52 m from the head);
after k6, 0.50- 0.58 m. In world space for the 1.9 m run (attacker at (33.391, 37.943), heading 179°): left hand
(33.537, 36.949, 1.500), right hand (33.204, 36.960, 1.471), spine (33.345, 36.778, 1.403), head (33.342, 36.831,
1.959). `S1`'s left hand is 0.30 m from the head after k3.

**`X1` against a standing target** (confirmed (runtime), PCSX2, slot 6, the same still PoizoCiv; cross
tapped at k0, clip 11 from k2; the strike shapes read from the bodies themselves, as below). Two rules of the test
decide where `X1` lands, and they hold for every move:

- **The test is swept** (confirmed (code), `Human_TestStrikes` `0x0033f110`): each enabled shape is tested along its
  move since the last update, from its previous posed point to the new one (shape `+0x20` to `+0x50` for a sphere),
  not only where it ends. A shape is posed only while it is on, so its first posed update has no sweep (previous =
  current). `X1`'s right hand never overlaps the target where it ends: at the contact the static gap is +0.013 m
  from a 1.0 m start and +0.040 m from 1.6 m; only the sweep across the target's front touches.
- **Contact and samples**: the contact update k10 is the one whose result is sample k10: the target's health first
  drops in that sample (also in the `S1` and 653 runs above), and the shapes that overlap are the ones posed in it,
  the hand sweeping from its k9 point to its k10 point.

**The slide** is two motions added together, confirmed (runtime) with the steer's own fields (`moves_reachX1_10_steer`,
`moves_reachX1_16_steer`):

1. **The steer** (`Attack_SteerToTarget`, then `Human_MoveToOver` `0x0023d2b8`, which keeps the goal at human
   `+0x310`, the velocity at `+0x2e0`, the time left at `+0x300` and an on flag at `+0x331`). On X1's first update
   (k2) the goal is set **1.038-1.048 m** from the target's transform position, at the target's height: the reach
   1.02 m from the target's head ([The two steers](#two-steers)), which leans 0.02-0.03 m toward the attacker. The attacker
   slides to it at a **constant velocity over 0.308 s** (9.25 updates, k3-k11; the time to the clip's first event
   plus 0.1 s): 1.824 m/s inward from 1.6 m, and **0.157 m/s backward** from 1.0 m (the goal is outside him). Confirmed
   (runtime).
2. **The clip's own root motion**, bone-cache entry 0 (`0x006b6880` + index × `0x470`, its second word the forward
   speed in m/s): 0.42, 0.59, 0.62, 0.63, 0.57, 0.58, 0.70, 0.60, 0.47, 0.22 m/s over k3-k12, about **0.18 m
   forward** in all, then about 0 (its first word, up to −1.17, is not a sideways speed: the attacker does not drift;
   inferred, a turn).

Confirmed (runtime): the attacker ends at the goal less the clip's 0.18 m: **0.870-0.880 m** from the target at k12
whatever the start (0.879 from 1.3 m). From 1.0 m the steer moves him back while the clip carries him forward faster,
so he still closes in (0.013-0.017 m per update). At the contact the attacker is **still moving in**: 0.890 m (from
1.0 m), 0.930 m (from 1.3 m), 0.970 m (from 1.6 m) after the contact update, never 1.02 m.

**The hand and the target at contact** (confirmed (runtime)). In the attacker's frame (forward, right, up from his feet,
metres; samples after each update; the attacker's scale 0.97 makes the hand sphere's radius 0.087). The hand's path is
the same to the millimetre from every start; the target's shapes sit at the distance d:

| Sample | Right hand sphere (25), r 0.087 | Target spine segment (3), r 0.18 | Target head (6), r 0.15 |
| --- | --- | --- | --- |
| k8 (first posed) | 0.009, +0.459, 1.627 | (d − 0.013, −0.03, 1.112) to (d + 0.003, −0.03, 1.492) | d − 0.06, 0.00, 1.781 |
| k9 | 0.732, +0.233, 1.668 | the same | the same |
| k10 (contact) | 0.823, −0.223, 1.536 | (d − 0.013, +0.04, 1.112) to (d + 0.003, +0.04, 1.492) | d − 0.06, +0.07, 1.781 |
| k11 | 0.647, −0.592, 1.374 | (target recoiling) | |
| k12 | 0.389, −0.707, 1.172 | | |

Confirmed (runtime): the hand comes in at shoulder height and crosses from the attacker's right to his left in front of
the target, a hook; between k9 and k10 it passes the top of the spine segment (1.49 m) where it meets the right-to-left
line, 0.78 m ahead at 1.60 m high. With the attacker standing still through that update, the sweep touches the spine
only when the distance after the update is at most **1.019 m** (the head at 1.00 m); an attacker still moving forward
carries the start of the sweep back with him (the sweep is in world space), which costs reach: the 1.6 m run, 0.078 m
forward in that update, hit with 0.022 m to spare at 0.970 m. Measured swept gaps (spine / head) at k10: −0.105 / −0.059
(from 1.0 m), −0.064 / −0.028 (1.3 m), −0.022 / +0.013 (1.6 m). From 1.9 m, with no slide, the hand stays 0.68 m short.

So a `X1` stance of 1.02 m with a static overlap test misses by a few centimetres on two counts (inferred from the
runs above): the original's
attacker is 0.89-0.97 m away when the hand crosses (and 0.88 m when the slide ends), and its hand is tested along
its sweep.

**A walking target** changes this through the lead (confirmed (runtime)). PoizoCiv walking in at 1.63 m/s (its AI on,
placed until update 9): the 653 steer stopped near 1.7 m by k3 and contact came at k7 from 1.0 and 1.6 m and **k8** from
1.9 m (the shapes' last live update), at 1.15-1.21 m. Pinned in place but still in its walk (velocity 1.6 m/s toward the
attacker, position written every update), the lead put the attacker 1.9-1.95 m away at k3 and 653 **missed** from 1.0
and 1.9 m, as did `S1` from 1.5 m. So runs against a pinned walking target understate what reaches (inferred; the right
snap's miss may be this).

Scenarios: `moves_reach653_10`, `moves_reach653_16`, `moves_reach653_19`, `moves_reachS1_15`, `moves_reachX1_10`,
`moves_reachX1_13`, `moves_reachX1_16`, `moves_reachX1_19` (still target; the `X1` ones but the last also read the
bodies' shapes), `moves_reachX1_10_steer`, `moves_reachX1_16_steer` (the steer's goal and the clip's root) and
`moves_reach653_19_walk` (walking).

### An AI's attacks {#ai-attacks}

Read in Ghidra: confirmed (code) where no other level is given.

An AI human attacks through the same dispatcher and attack functions as a player: the [attack
action](ai.md#attack-action) writes a command into its per-player record and `Player_UpdateActions` (`0x0027c120`)
reads it ([The dispatcher](combat/index.md#dispatch)). What differs for a human that is not pad-controlled (per-player
`+0x1b` = 0, human `+0x1b0` = −1):

- **The target is the brain's.** `Human_GetTargetHandle` (`0x00226e20`) and `Human_GetTargetResolved` (`0x00226ea8`)
  first copy the brain's human target (`+0x124`) and object target (`+0x128`) into the human
  (`Human_SyncTargetFromBrain`, `0x00226ee8`). Square (`0x00286cc8`) and cross (`0x00287a18`) call
  `Player_PickTarget` only for a player, so an AI strikes its brain's target; square alone falls back to
  `Player_PickTarget` (2 m, the nearest foe within 54° of the facing, [Target selection](#targets)) when that target
  is more than **2.5 m** away (distance² above `0x005104c0`, 6.25 in play). The steer onto the target is the player's
  ([The steer into an attack](#attack-steer)).
- **The stick.** `Player_GetAimHeading` (`0x0021d1a0`) and the snap test read the per-player record's stick, which an
  AI's attack action clears and then writes only when the action has an angle (magnitude 1.0,
  [AI](ai.md#attack-action)). Without one an AI aims along its facing and never snaps, and the walk attack 23 (stick
  0.12 or more) does not happen.
- **The run attack.** `Human_WantsRun` (`0x00225c10`) ignores the stick for an AI: it holds unless the state has
  `0x100000000` or any of `0x3981f3ff3` (the fight stance among them), any held flag is set, or the human carries an
  object of kind 4 or 6. So square from an AI out of the fight stance at gait 4 (cross at gait 4 or 5) plays the run
  attack 24 (`Player_StartCharge`, [Moving attacks](combat/index.md#run-attacks)); otherwise the AI takes the fight
  stance (`Player_EnterStance`, `0x00280068`) and plays `S1` or `X1`, or the grounded, tackled-target or grab-victim
  strike as a player does.
- **Not for an AI**: the locked strafe attacks (`0x1f`-`0x21`, which need a pad and `Human_IsLocked`), the counter grab
  and counter tackle from square or cross (`Human_CanCounterGrab`, `Human_CanCounterTackle`: an AI has command 3
  instead, [AI](ai.md#block)), starting a block ([The dispatcher](combat/index.md#dispatch)), and the climb and jump on
  triangle: an AI's triangle goes straight to `Player_TriangleAction` (`0x002811f0`).

**May this attack start now** (`Human_CanStartAttack(h, target, kind)`, `0x00224778`), asked by the attack action's
Start (`0x002fa9a8`) and by goals before they queue one (EngageEnemy's charge, [AI](ai.md#engage-enemy)). It refuses
when:

1. the target holds any of `0xc08200` in record `+0x08`, unless the target's state has `0x2000` and the attacker's
   `0x1000`;
2. the attacker holds any of `0x5c7eee0`, or its record `+0x118` (s16) is not 0;
3. the distance² is beyond `Attack_FarReachSquared` (`0x00230930`): the square of the attack table's far range
   (`AttackTable_GetFarRange`, `0x00254508`) for the kind's clip. Kind 0 takes clip 24 at a run or sprint, 23 at a
   walk, else 11; kinds 1-9 clips 12, 13, 15, 14, 16, 17, 19, 18, 20; 10 → 25, 11 → 21, 12 → 193, 13 → 194, 14 → 237,
   15 → 664, 16 → 653, 17 → 657, 19 → 0, 20 → 1, 21 → 3, 22 → 70, 32 → 104. Kind 23 reaches **12 m**, as do kinds 0
   and 1 with a throwable (held set 1) beyond the melee near range (brain type not 5); kinds 0-9 with a held object of
   set 5 reach **30 m**, of set 4 **15 m**; held sets 3, 2 and 1 use their own clips (36, 34, 35; 41, 39, 40; 47, 45,
   46 for kinds 0, 1, 5); anything else **2 m**.

Then, by kind (the attacker "free" = its state has none of `0x7bf9e9f7ff0`; "victim" = the human it holds, `+0xc4`):

| Kinds | Also needs |
| --- | --- |
| 0-9, 11 | the attacker free and the target's state none of `0xe3000`; kinds 0 and 1 with a held object of kind 1-3 only the attacker free |
| 10 (the snap) | the attacker free |
| 12, 13 (grounded strikes) | the target down (`Human_IsHighOrBusy`, `0x00225200`: mostly the target's states `0x40100f0800`), the attacker free and holding none of `0x48000`; always yes when the target is the attacker |
| 14 | the attacker free and the target knocked down |
| 15 | the attacker free and the target's state none of `0x40100f0800`; then yes with spray paint, otherwise only for an AI |
| 16-18 | the attacker free and the target's state none of `0x40100f0800`; class `0x77` (Diego) also not carrying an object of kind 4 or 6 |
| **19, 20** (charge, dive) | the attacker free, the target not down (state none of `0xe0000`), and the attacker **at the run speed**: gait 4 or 5 with no held flag and speed ≥ run speed − 0.01 (`Human_IsAtRunSpeed`, `0x00225998`) |
| **21** (tackle) | the attacker free, its power (record `+0x148`) at least a fifth of its maximum, the target's human flag `0x40` clear, the target free and holding none of `0x40` |
| 22 | the power and flag `0x40` as for 21; the target's state none of `0x7bfdc8f7fd0`, holding none of `0xc12200`; a target in state `0x20` only from in front (`Human_GetSideOf`, `0x002672d0`) |
| 23 | a throwable in hand and the attacker free |
| 24-26, 29, 30 | the attacker grabbing (`0xc0`), free of `0x7bf9e9f7f00`, and the target its victim |
| 27, 28 / 38, 39 | state `0x10c0` / `0x1400` and the target its victim |
| 31, 33, 34 | the attacker grabbed (`0x30`), free of `0x7bf9e9f7f00`, and the target holding it |
| 32 | the attacker in state `0x20`, free of `0x7bf9e9f7f00`, and not held by the target |
| 35-37, 40 | the attacker mounting (`0x400`), free of `0x7bf9e9f73f0`, and the target its victim |
| 41 | handcuffs, then as 35 |
| 42-44 | goal `0x17` on top while in state `0x80000`; otherwise state `0x800`, free of `0x7bf9e9f73f0`, and the target holding it |

`AttackKind_IsCharge` (`0x00229b60`) is true for kinds **0 and 19-21**, the kinds the run-in may end on
([AI](ai.md#engage-enemy)).

**When the target may be attacked again.** The attack action's Start adds `Attack_GetNextAttackDelay(attacker, kind)`
(`0x00231590`) to the **target** brain's `+0x1ec` (`Brain_SetAttackableTime`, `0x00290e78`, which first raises
`+0x1ec` to now). It is the length in ms of the **attacker's clip for the kind** (`Anim_GetPlayTime`: duration over
rate), not `CfgAttackDelay`. Kinds 0-11 take `X1` 11, `S1` 12, 13, 15, 14, 16, 17, 19, 18, 20, the snap 25 and 21
(kinds 0 and 1: **100 ms** while the attacker is knocked down); 12 → 193, 13 → 194, 14 → 237, 15 → 664; 16, 17,
25-29 the longer clip of a pair (653/655, 657/659, 147/151, 155/159); 19 → 0, 20 → 1; 21, 22, 23 the sum of a pair
(3 + 5, 70 + 72, 466 + 467); 24 and 31 slot 17's clip; 30 → 118; 32 and 33 the shorter of 96/98 and of 100/102
(108/110 and 112/114 from behind); 34 → 90 (it also stops the attacker's speech); 35-39 → 225, 40 → 248, 41 → 252,
43 → 246, 44 → 242; 18, 42 and any other kind → 100 ms. With a held object of set 3 kinds 0, 1 and 5 use 36, 34 and
35; of set 2, 41, 39 and 40.

That time is **divided by the target brain's spacing byte** when the byte is not 0: `+0x14a` while the target stands,
`+0x14b` while it is down (state `0xe0000`, `0x00223b48`), times `0x00510ad0` (1.0), rounded. None of this happens
when either human is grabbing or tackling, or the target is cuffed (`0x002fa9a8`).

**The spacing bytes** (`Brain_SetAttackSpacing` `0x002911d8`, `Brain_SetAttackSpacingHeld` `0x002911e0`):

- `Brain_Init` (`0x0028a9c0`) sets both to 1, and so does `Brain_SetTarget` (`0x0028cfe0`) when the last attacker
  leaves the target's attacker list (`+0x1a4`).
- When an attacker claims a slot on the target (`Brain_ClaimAttackSlot`, `0x0028df30`, [AI](ai.md#targets)), the byte
  is raised to the **attacker's gang's** [`CfgGang`](../references/bindings/config.md#cfggang) value: `+0x14a` =
  max(itself, argument 2, gang record `+0x6c`) and `+0x14b` = max(itself, argument 3, `+0x6d`); a target whose brain
  is type 3 (a Warrior) gets `+0x14a` = 1 instead.
- `config_preload2.lua` gives most gangs 2 and 2; gang 1 3 and 3, gang 3 1 and 4, gangs 7 and 21 4 and 4, gang 8 2
  and 1, gang 13 1 and 1.

So against an enemy of a spacing-2 gang's attackers, the next attacker may strike after **half** the last attack's
clip, while a Warrior target makes them wait the whole clip. The attacker's own next attack (brain `+0x1e8`) is the
separate attack delay ([AI](ai.md#attack-action)).

### Breakables {#breakables}

Read in Ghidra: confirmed (code) where no other level is given.

**Glass cabinet**: square at the cabinet plays 662 `SPECIAL_BREAK_OBJECT_MID` for about 1.1 s with the object as the
target; then **triangle** picks up one item each press with 464 (one-handed pick-up, left high). Confirmed (runtime).
**Car window**: square plays 662 on the window. Confirmed (runtime).

`Player_ObjectAttack` (`0x00264178`) picks the clip by the target point's height above the feet: up to 0.8 m → 661
`LOW`, above → 662 `MID` (663 `HIGH` is never chosen); below the feet → 194. It first approaches when the object is
between reach × 0.5 and far × 1.5 of the clip's range.

**The store's cabinets** (mission 1's store, `level99`). A cabinet is no object: it is two or three type-1 glass panes
that `level99.lua`'s `AddGlass` places (a top at z 2.02 and a front or side, z 1.23-1.95; [Glass
types](../references/glass-types.md#glass-1)), around loose items from the level's object list, which this page's
breakables do not spawn: `level99_objs.txt` ([World objects](objects.md#objs-file)) puts 3 `dyn_pwatch` behind the
front panes at x 50.25-52.26, y 56.99, 3 `dyn_ringdmnd` in the next cabinet (x 48.16-50.17) and 4 `dyn_ring` in the
side one (x 48.13), all in object zone 26, which `BNESetup` (`global.lua`) enables. The items are `pickup_item`s of
`TYPE_SPECIAL` (12). Confirmed (runtime): the spawn records read in the store. Disc check: the level's object list.

- **Any strike on a world object counts as a hit**, an ordinary punch or kick included: `Strike_Contact`
  (`0x0021b290`) treats every body it is called for alike. For a world object it does five things:
  1. reports a 30 m noise (`0x002936a8`);
  2. takes hit points (`WorldObject_TakeHit`, `0x00393450`). Byte `+0x10d` loses 4 + 6 × the hit's kind: 4 for a punch
     or kick (kind 0), 16 for a charge or dive (kind 2, attacker `+0x08` `0x1400000`), 22 for kind 3 (attacker human
     `+0x54` flag `0x4000000`). It loses only 1 when object `+0x128` is 1, and nothing when the byte is 0 or −1. A
     second byte, `+0x10e`, is spent first for objects with flag `0x4000000`;
  3. plays the impact sound (9, or `0x1a` for a charge, by the type's material `+100`);
  4. sends the object event 1 carrying the damage (its vtable `+0x44`), then message 1 (kind, the attacker twice
     and the contact point and normal, [World objects](objects.md#door-break));
  5. counts statistic 9 for a player when the object is not yet broken and lacks class flag `0x20`.

  A pane also breaks. An object of kind 30 reports crime 10. A whole object with body flag `0x4` hurts a human
  thrown into it (in a knockdown flight, `+0x08` `0x400000`, with a grab partner) by 5 × type `+0x58`.
- **Breaking.** The square's hit breaks the struck pane only (`Glass_Break` from `Strike_Contact`, `0x0021b290`, the
  breaker the player; [a pane's life](objects.md#pane)); the cabinet's other panes stay whole and the items do not
  move. One square broke the front pane centred at (51.26, 56.99, 1.59), 12 updates after the press (confirmed
  (runtime)). A cabinet has **no damage stages**: a pane has no hit points, and its hit handler
  (`GlassScript_OnMessage`, `0x003e2d90`) acts once. It sends the pane's break event, swaps to the type's broken
  rectangle (`0x00010015` for type 1) drawn white at alpha `0x70`, makes one `sub_glass` shatter (shard size 0.2) at
  the pane's centre along its normal, turns off both triangles (type 1's extra hook is empty) and removes the body.
  Later hits find flag `0x800000` set and do nothing. The impact sound is `Strike_Contact`'s (material `GLASS`).
- **Triangle with nothing held** reaches the pick-up search `0x0024d810` ([Crimes: triangle](crimes.md#triangle), step
  5; `ContextAction_Use` ran with no context record each press, confirmed (runtime)). It gathers objects within
  **1.5 m** of the human (and of a second point, his position + human `+0x4e0`), and keeps one that is pickable (object
  flags `0x8000`), not the class `powerup_item` (walked over instead), and **in sight**: `Human_CanSeeObject`
  (`0x0021c570`) casts a ray from the feet + 1 m and, if that hits, from the feet + 2 m to the object, and rejects it
  when both hit. Each candidate is scored by the direction to it from a point 0.1 m behind the human, dotted with his
  facing: 3 at 0.38 or more, 2 from 0 to 0.38, 1 behind; the first of the best wins. An intact pane blocks the rays: a
  triangle before the square did nothing (confirmed (runtime); that the glass is what the ray hits is inferred from the
  ray test).
- **The pick-up.** For a `TYPE_SPECIAL` item the search pins its record and sends it message 0; the human's message
  `0x14` (`0x0025e5a8`) then picks the clip by the type's pick-up animation (`CfgObj` argument 14, 5 for all four
  jewellery types): **463** at or below 0.8 m above the feet, **464** above (the cabinets' items are 1.32 m up), with a
  0.2 s blend, and hands `0x00275d10` the time to the clip's first event. That the item sends `0x14`, and that
  `0x00275d10` steers like `Attack_SteerToTarget` ([Open questions](#open-questions)), are inferred from the call
  order. At runtime 464 ran 20 updates; the player turned 4.6° and moved up to 0.07 m per update for the first 5, and
  the clip's event (message 3) ran `Human_PickUpObject` (`0x0023bf00`) on the 6th update (confirmed (runtime)).
- **The take.** `Human_PickUpObject`, for a `TYPE_SPECIAL` whose model hash is not one of the named mission items, adds
  item **10** (loot) ×1 with the notify flag, then money: the type's `CfgObj` argument 4 × the float at game state
  `+0x380` (1.0 here), without notify ($7 a watch, $10 a diamond ring, $8 a ring, $15 a necklace), and plays item 10's
  pickup sound; the record is removed for good (spawn record bit `0x40000`). Confirmed (code), and confirmed (runtime):
  three presses took the three watches, each adding item 10 ×1 with notify, then $7 without; a fourth press played
  nothing (the next cabinet's rings are behind its whole pane). [Inventory](player-state.md#pickup-callback) covers
  the callback.

Scenario `store_loot` (`repo:research/traces/scenarios/store_loot.toml`) replays this with the hooks that log it.

**A wooden fence** (`level99` checkpoint 3.5): the charge, the dive or the walk attack breaks it with their strike
shapes, the run attack falls short; the break, its message 2 and the script's placement are under [How a moving attack
strikes](combat/index.md#moving-strikes) and [Barriers](objects.md#barriers).

### The stereo theft {#stereo-theft}

Read in Ghidra: confirmed (code) where no other level is given.

**Triangle** at a car's open window with a radio plays 683 `STEREO_STEAL_INTRO` (0.55 s), then the loop 684, state
`0x4000000`. `Player_UpdateTheft` (`0x0027e6d8`) runs one of three games by the mode at `+0x46` of the per-player
record `0x0051489c + 0x168 + p × 0x5c`:

- **Mode 1, mash** (freeing a handcuffed partner, [Crimes](crimes.md#triangle)): alternate L1 and R1 (commands 6
  and 4, held); each alternation adds 125 × 1.5 or × 0.7 (Warrior byte `+0x08`), the meter loses 15 per update, and
  1000 completes it (`CfgButtonMash`).
- **Mode 2**: **lock picking**, a timing game against a dial of three turning pins ([Crimes](crimes.md#lockpick)).
- **Mode 3, the car radio**: `Theft_UpdateStereo` (`0x0027e908`). Rotate the left stick **anticlockwise**, the stick
  above 0.8 in this update and the last, turning less than 90° per update. The angle turned adds to `+0x48`; at each
  stage's target the stage (`+0x4c`) advances after a 250 ms pause; **4 stages** succeed (`0x00278628(h, 1)`, 685
  `STEREO_STEAL_END`). The stage target was 6π (3 turns) here, from Warrior byte `+0x0b` (2): the target is 2π (one
  turn) when the byte is 0 or 1, else (byte − 1) × 6π, so 3 turns for 2 and 6 for 3 (`Human_GetClassSpinAngle`,
  `0x0022dd08`).

Any command outside the game's own set (square, circle, R1, L2 and others) **fails** it: 686 `STEREO_STEAL_FAIL`.
At runtime 1.5 turns per second took 9.6 s to succeed, and one R1 press failed it. Confirmed (runtime).

## In Coney

The pick, the stance and the lock live in `repo:src/combat/lock_on.h` (the lock-on rule, the combat walk's clip, when a
target is dropped), `repo:src/combat/attack_target.h` (which target each attack goes for) and
`repo:src/human/fighter.cpp` (the target in front, the grab search, the stance) and `repo:src/human/human.cpp` (the
combat walk); the steer is
`repo:src/human/turn_and_slide.h` and the swept strike test `repo:src/human/strike_shapes.h`; an AI's attack checks
are `repo:src/ai/attack_views.cpp`; the object attack's clip is in `repo:src/combat/attacks.h` and the stick games in
`repo:src/combat/stick_games.h`. [Combat: Coney's implementation](combat/index.md#coneys-implementation) says what each
file does, and [Combat differences](combat-diffs.md) lists every place where Coney's fight plays differently. The
three below are this page's.

### Teammates {#teammates-coney}

The original's rules are in [Teammates](#teammates); issue #79 tracks them. Where Coney differs:

1. Coney's `Humans::opposed` (the `AiHumans` opposition, `!Gangs::friends`) gates the strike test as well as the
   targets, so the player's punches pass through teammates; the original's strike test takes every human body.
2. Coney keeps friends out of every target pick, the grab and tackle search and the locks, as the original does.
3. A hit on a friend is not built: the original plays the impact and the hit effect, takes no health (unless both are
   players), plays the reaction by hit code as for an enemy, and has a Warrior hit by a player with no target say the
   complaint line `0x58`.
4. `WarriorBrain_OnHitByChief` is not built (a stand-in in `warriorEvent`, `repo:src/ai/brain_events.cpp`): no 10 s
   window, no fourth hit, no AttackTarget on the chief for 20 s with Fight on top, no taunting ring, command reset or
   menu lock. Its gate, the commands lock (`WCLockCommands`), is Coney's `StoryState::noRetaliation`, set by
   `makeWcLockCommands` in `repo:src/scripting/story_bindings.cpp`, so in `level80` no Warrior hits back in either.
5. Thrown bodies do not strike teammates (the original's knockdown with no damage).
6. Coney's human contacts ([Physics: human contacts](physics.md#human-contacts)) have no 2 s push wait and no swap of
   places (a head-on contact at gait 3 or more, then another 2.0-2.1 s after it).

The checks that will show it: square into a teammate 0.8 m ahead plays reaction 272 on him and leaves his health
unchanged; three such hits within 10 s leave him alone and a fourth starts his AttackTarget and Fight on the player
and costs him the hit's damage; in `level80` (commands locked) the fourth hit starts nothing; a sprint into a teammate
stops the player and knocks nobody down; circle with only a teammate in reach plays 71 then 69 and leaves him alone; a
square that touches him makes him say `0x58`.

### The fight stance and the lock {#fight-stance-coney}

The original's rules are in [The fight stance](#fight-stance); issue #82 ported them. `Fighter::updateStance()`
(`repo:src/human/fighter.cpp`) is the stance update, step by step, run before the player's commands as the dispatcher
runs it, and `Fighter::updateL1()` the L1 handlers, outside the block, the busy gate and a hold;
`Fighter::lockTarget()` is the lock-on movement's target, set only by the stance's step 3 and L1, and
`Human::combatWalk()` (`repo:src/human/human.cpp`) the lock-on walk, which faces the target within 2.5 m (or while L1
locks it) and keeps the heading beyond it. The stance's search takes only threats: `human::Humans` marks the targets
that are no threat (`ai::isThreat()`) or a pad player as the calm list, which only the stance's search skips (the
attacks' picks still take them). 23, 24 and 501 store their pick and steer nothing (`Fighter::steer()`). The
distances are `CombatTuning::stanceNear`, `stanceMid` and `stanceFar` at the values in play (`CfgDistances` is not
read from the scripts).

Where Coney still differs (stand-ins, each marked at the code):

- L1 picks as square does within 2.5 m; the original's `Player_FindLockTarget` (`0x0027b2a8`) searches to 6 m
  ([L1's own search](#q-l1-search)).
- The clear-line test of the nearest enemy (`0x0021c240`) is not made, so the nearest is always taken, and a target in
  a scene is not dropped ([The clear-line test](#q-clear-line)).
- A target is out when it has no health left (Coney's targets report no state word).
- The standing stance before an alerted enemy within 20 m (step 5) is not entered, the fighter not seeing the brains'
  lists ([The alerted-enemy stance](#q-alerted-stance)).
- L1's action delay is not modelled and a press acts as a hold ([L1's action delay](#q-l1-delay)).
- Square in the stance keeps its target out to 3 m (`kKeepTargetRange`, issue #895), where `0x005104c0` gives 2.5 m.

Tests: `repo:tests/human/player_targeting_test.cpp` (one case per scenario of [The fight stance](#fight-stance)) and,
through the player's path, `repo:tests/platform/disc_level95_crew_lock_test.cpp`.

### The steer {#steer-coney}

Coney's steer (`repo:src/human/turn_and_slide.h`) turns at angle / `T` and slides at (goal − position) / `T` for the
`T` of [The two steers](#two-steers), starting the update after the clip, as [The steer into an
attack](#attack-steer) describes, rather than snapping the facing or spreading the reach over the whole clip; and it
tests the strike shapes along their sweep (`repo:src/human/strike_shapes.h`), as [Where the attacker stands at
contact](#reach) shows the original does.

## Open questions

- **A backward walk's first updates**{#q-backward-start}: what slows the first 5 updates of a backward combat walk out
  of the stance (speculative: the 1/6 s fade into the walk clip; [The fight stance](#fight-stance)). A run that reads
  the walk clip's blend weight over those updates would tell.
- **The steer's slide and the root motion**{#q-slide-add}: whether the steer's slide is added on top of the clip's
  root motion (inferred, the add itself was not read; [The steer into an attack](#attack-steer)). Found by reading what
  `Human_ApplyTurnAndSlide` (`0x0023f5e0`) does with `+0x2f0`.
- **The pick-up's steer**{#q-pickup-steer}: whether `0x00275d10` steers a pick-up like `Attack_SteerToTarget`
  ([Breakables](#breakables)). Found by decompiling it.
- **The complaint line**{#q-complaint-heard}: whether a Warrior's complaint line `0x58` is heard or dropped by the
  speech system ([Teammates, at runtime](#teammates-runtime)).
- **An AI's head in the fight idle**{#q-fight-idle-head}: the head point an AI target holds in its fight idle 358, not
  recorded: forcing state bit 1 on a brain-off target did not change its idle, as the bit was cleared each update
  ([The led steer](#led-steer)).
- **653's extra reach**{#q-653-reach}: where 653's 0.04 m beyond its 1.58 m reach comes from: its steer aims at the
  target's position (task `+0x10`), not its head ([The two steers](#two-steers)); whether the clip's root motion over
  k1-k3 adds it was not read ([Where the attacker stands at contact](#reach)).
- **L1's own search**{#q-l1-search}: porting `Player_FindLockTarget` (`0x0027b2a8`, confirmed (code)), which
  `Player_LockOnTarget` (`0x0027b548`) calls from `0x0027dc00` with no target: humans within 6 m and 63° of the aim
  through `0x002792c8`, else threats within 4 m (`0x002791b0`), standing enemies within 6 m (`0x00279568`), any angle
  within 4 m (`0x002797b0`), near ones within 6 m (`Humans_FindNear`, `0x00279c48`), a car within 1 m, then 2.5 m
  passes (`0x002796a0`, `0x00279d38`); the nearest of the first pass that finds any is taken if the line to it is
  clear (`0x0021c240`). What each filter of `0x002792c8` and `0x00279c48` refuses is not read: decompile them and
  compare with `0x00279410`.
- **The alerted-enemy stance**{#q-alerted-stance}: how the rule of the stance's step 5 (brain list `+0x164`) is fed
  for the player, so Coney can enter that standing stance ([The fight stance](#fight-stance)).
- **The clear-line test**{#q-clear-line}: porting `Human_IsClearToTarget` (`0x0021c240`, a capsule cast through the
  physics) for the stance's nearest enemy, and the scene state a target's drop also reads (`Human_IsInSceneState`:
  human `+0x280`, or state `0x800000`), which Coney's targets do not report ([The fight stance](#fight-stance)).
- **L1's action delay**{#q-l1-delay}: who sets the per-player action delay (player state `+0x4d` / `+0x50`, read by
  `PlayerRec_GetActionDelayLeft`) that refuses L1 held ([The fight stance](#fight-stance)).

## How this was checked

The code claims were read in Ghidra on `SLUS_212.15` (NTSC-U); the clip events and the Anim Range List values were
read from the disc's character data, and the store's items from the level's object list. The runtime claims were read
with PCSX2 2.9.94 over PINE, before run records existed, so none of them has one; their old form is kept as a legacy
page's ([Evidence levels and run records](../guides/research-evidence.md#run-records)):

- **Teammates** ([at runtime](#teammates-runtime)), 2026-10-11: level80 checkpoint 2 entered from a level99 state by
  setting mode 8's level ([the runtime order](scripting.md#level80-hints)). Input was the scripted pad of [Driving
  PCSX2](../guides/research-workflow.md#driving-pcsx2), with the hooks `strike-contact`, `hit-by-chief`,
  `hit-reaction`, `attack-target-push`, `push-contact`, `can-push`, `grab-start`, `anim-dispatch` and `say-command` of
  `repo:research/traces/patches/`. The swap was a second pass the same day, entered the same way, with the hooks
  `push-contact` and `can-push`.
- **The fight stance**: copies of the street save state (slot 6), the player Rembrandt with PoizoCiv as the target
  (pinned 0.95 m ahead for the L2 runs, 2 m ahead and circling for the lock runs); the backward walks, 2026-10-05, each
  from a fresh copy with L1 held and the civilian locked 1.2 m ahead.
- **The stance's path** (the table in [The fight stance](#fight-stance)), 2026-10-21 with PCSX2 2.9.129 (the
  PCSX2-MCP fork): copies of slot 6 and level95 checkpoint 2, one sample per update, the scenarios `target_*` of
  `repo:research/traces/scenarios/`; the knockdown run on 2026-10-25 has its record
  (`repo:research/runs/r-20261025-target-knockdown-kept.toml`).
- **The steer into an attack**: scenario `combat_cross` (`repo:research/traces/scenarios/combat_cross.toml`).
- **The led steer** and **Where the attacker stands at contact**: copies of slot 6 with the puppet PoizoCiv (brain off,
  its move speed 0) placed by writing its transform each update until the move, driven over PINE with the scripted pad,
  the right-stick and puppet patches and the `strike-contact` and `strike-shape` hooks; the scenarios are named beside
  the runs (`repo:research/traces/scenarios/moves_reach*.toml`, `moves_head_reel.toml`). The `X1` contact runs were
  recorded while `pcsx2 record` labelled every `strike-shape` and `strike-contact` call one update early ([Recording a
  trace](../guides/research-workflow.md#hooks)) and are given re-labelled one update later, so that each contact falls
  in the update whose sample first shows the damage. The fight idle 358 is an earlier recording of the same skeleton.
- **Breakables**: the store's cabinet at save-state slot 4 (the spawn records read there), the car windows at slots 2
  and 5, the fence at slot 10 (`level99` checkpoint 3.5); the pane's break and the pick-up's update counts were
  re-labelled one update later after the same fix to the recorder. Scenario `store_loot` replays the cabinet.
- **The stereo theft**: slot 5, the car's open window with a radio.
