# Combat: targets, AI attacks and breakables

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 as on [Combat](combat.md), which this page was split from because that page had grown too
large. The overview, the data tables and the code index stay on [Combat](combat.md#code-index).
Every claim keeps the evidence level it had there.

## Purpose

This page covers how a target is picked (teammates, the fight stance), how an AI attacks, breakable objects and the
stereo theft. A player sees it as whom his punch turns toward, when he squares up to an enemy and walks round him,
what happens when he hits his own crew, when the gangs he fights may swing at him, and the glass, cabinets, fences and
car radios a punch or a triangle opens. Every attack ([Combat moves](combat-moves.md)), the grab's search
([Grabs](combat-grabs.md)) and the AI's fight goals ([AI](ai.md#check-attack)) start from the target this code picks.

## Original structure

No source file names this code: it lies in the player's combat code in `Human/`, after `cns/cnsplayertag.cpp`
([Source map](source-map.md#position), inferred), and the names are ours. Every function is in the [code
index](combat.md#code-targets); these are the entry points, each read in Ghidra at its address.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0027a6c0` | `Player_PickTarget` | the target of an action: humans, cars, world objects and glass ([Target selection](#targets)) | confirmed (code) |
| `0x0027ce90` / `0x0027d5a0` | `Player_UpdateSprint` / `Player_UpdateStanceOrIdle` | the fight stance each update ([The fight stance](#fight-stance)) | confirmed (code) |
| `0x0027b1a0` | `Player_FindNearestEnemy` | the stance's nearest threat within 3 m | confirmed (code) |
| `0x00280158` / `0x00241b90` | `Player_LockOn` / `Human_FightStanceMove` | the lock and the lock-on movement | confirmed (code) |
| `0x002625a8` | `Attack_Start` | starts an attack and its steer | confirmed (code) |
| `0x002761c8` / `0x00275678` | `Attack_SteerToTarget` / `Attack_SteerLed` | the turn and slide into an attack, and its led variant | confirmed (code) |
| `0x00306190` | `WarriorBrain_OnHitByChief` | a Warrior hit by his chief hits back ([Teammates](#teammates)) | confirmed (code) |
| `0x00219b08` | `Human_OnPushContact` | a head-on push and the swap of places | confirmed (code) |
| `0x00224778` | `Human_CanStartAttack` | whether an attack kind may start now ([An AI's attacks](#ai-attacks)) | confirmed (code) |
| `0x00264178` | `Player_ObjectAttack` | the clip of a strike at an object ([Breakables](#breakables)) | confirmed (code) |
| `0x0027e6d8` / `0x0027e908` | `Player_UpdateTheft` / `Theft_UpdateStereo` | the stick games, the car radio among them ([The stereo theft](#stereo-theft)) | confirmed (code) |

## Data

The target, the stance and the lock are kept in fields that [Combat](combat.md#data) owns: the state bits `0x1`-`0x8` of
the stance and the lock ([State flags](combat.md#state-flags)), the target handle at human `+0xc8` and the other
[record fields](combat.md#record-fields), the attack table's reach and far range ([The Anim Range
List](combat.md#damage-table)) and the distances of `CfgDistances` ([Constants](combat.md#constants)). The fields this
page adds, each read in the function named:

| Field | Meaning | Evidence |
| --- | --- | --- |
| human `+0x1a8` | the gait, set from the speed by `Human_SetMoveVelocity` | confirmed (code) |
| human `+0x304` / `+0x308` / `+0x332` | the steer's turn: time left, rate and on flag (`Human_TurnToOver`) | confirmed (code) |
| human `+0x56c` / `+0x570` | the push contact's deadline and first time (`Human_OnPushContact`) | confirmed (code) |
| Warrior brain `+0x2dc` / `+0x2e4` | the chief's hit window and its count (`WarriorBrain_OnHitByChief`) | confirmed (code) |
| target brain `+0x14a` / `+0x14b` | the attack spacing bytes, standing and down (`Brain_ClaimAttackSlot`) | confirmed (code) |
| type word `+0x5e` | the world object's body flags that make it a strike target (`Obj_CreatePhysicsBody`) | confirmed (code) |

## Behaviour

The sections below go from the player's pick and stance to an AI's attacks and the objects a strike or a triangle
acts on. Each claim carries its level; the code claims were read at the addresses they cite, the runtime ones measured
in PCSX2 2.9.94 as each one says.

## Target selection {#targets}

`Player_PickTarget(range, h)` (`0x0027a6c0`), confirmed (code):

1. Keep the current target while `Target_ObjectFilter` (`0x0027a120`) accepts it; that filter accepts only world
   objects, so a human target is always searched for again ([Combat moves](combat-moves.md#targeting)).
2. Otherwise search along a heading (confirmed (code)): the camera-turned stick's angle if the stick is above 0.01
   (`Player_GetAimHeading`, `0x0021d1a0`), else
   the facing. First humans within range × 1.1 and within 54° (`0x0051096c`) of it, then objects and glass (world
   `+0x844`, `+0x840`, `+0x84c`), then, with no current target, humans within range × 0.9 at any angle; then within
   135°: objects at × 0.8 and humans at × 0.7 (`0x002796a0`). The nearest wins (`0x003868d0`).
3. The filters (`0x00279410`, `0x00279568`) skip allies and the same gang (brain `+0x20c`), humans more than 2 m
   higher or lower (`0x00510970`), those with flag `0x100000000000` (`0x00227d78`), the dead and the airborne;
   the first also skips the knocked down.
4. **Objects as targets**, confirmed (code) at `0x0027a6c0`:
   - **Cars** (world `+0x844`, `CarManager_FindTarget`, 1.0 m) come first among the objects, through
     `Target_CarFilter` (`0x00279f50`): within 3 × the angle (or any angle when `0x00279e00` says so) and not
     more than 2 m away in height. A car found ends the search, with its record copied to record `+0xe0`-`+0xec`.
   - **World objects** (world `+0x840`, `ObjectList_FindInRange`, `0x0039ab20`; confirmed (code)) within the range
     (2.0 m for square), and at the 135° pass within 0.8 × it. They must also be beyond record `+0xd8`, which the pick
     zeroes. `Target_ObjectFilter` (`0x0027a120`) keeps an object when it has a body, is within the angle (54°, then
     135°) and 2 m in height, is a world object (type mask `0x8`, `Object_AsWorldObject`) and its **body flags have any
     of `0x8`, `0x10`, `0x20`** (`Object_IsStrikeTarget`, `0x00396710`).
   - Glass panes (world `+0x84c`, `GlassTaskManager_ListInRange`, `0x0038fc70`) within the range, through
     `Target_GlassFilter` (`0x0027a070`): body, angle and height only. Confirmed (code).

   A world object's body flags come from its type's word `+0x5e` (`Obj_CreatePhysicsBody`, `0x00391d48`, confirmed
   (code)): bit `0x4` gives `0x10`, `0x8` gives `0x8`, `0x20` gives `0x20` (and `0x1` → `0x2`, `0x2` → `0x4`, `0x10` →
   `0x40`, `0x40` → `0x2000`, `0x80` → `0x10000`, `0x100` → `0x20000`, `0x200` → `0x40000`), on top of `0x80000500`.
   Kinds 29, 33, 34, 15, 26 and 31 add their own bits first. Which object types set those bits is on [World
   objects](objects.md). The square then plays [`Player_ObjectAttack`](#breakables) on the object picked.

### Teammates: what the player may do to his own crew {#teammates}

User item 10 asks what the original allows against the player's own Warriors outside the hangout. Confirmed (code)
at the addresses cited unless marked; "teammate" is a human friendly to the player (`Human_IsFriendly`, `0x00222a90`,
`Brain_IsFriendly`) and in his gang (brain `+0x20c`).

1. **No pick ever names a teammate.** Every human filter of the player's searches refuses friends and the same gang
   before anything else: the attack picks (`TargetFilter_Enemy` `0x00279410`, `TargetFilter_EnemyStanding`
   `0x00279568`, the any-angle pass `0x002797b0`), the grab and tackle search (`Player_FindCounterTarget`,
   `0x0027ac30`, the same three filters in that order), the stance's nearest enemy (threats only) and the L1 lock
   (`0x002792c8`). So square and cross swing along the stick or the facing with no target and no steer.
2. **So a teammate cannot be grabbed or tackled** (confirmed (code)). `Grab_Start`'s intro ends in nothing without a target
   (`Grab_IntroEnd`, `0x0026c1d8`, returns at once when `Human_GetTarget` is empty): 71 then 69 `GRAB_MISS` plays even
   with a teammate right in front. No grab, so no throw and no mount of a teammate. The hangout's sparring Warriors
   are grabbable because they are another gang: `level99` creates them in `CombatWarriors` and calls
   `GangMakeEnemies(Warriors, CombatWarriors)` ([AI: the sparring fight](ai.md#level99-fight)).
3. **A swing that touches a teammate does land** (confirmed (code)). The strike test takes every body, allies too
   ([Moving strikes](combat.md#moving-strikes)), so `Strike_Contact` (`0x0021b290`) runs on the teammate: impact sound,
   hit effect, the damage stored as pending through `Human_DealDamage` (`0x0021d680`) with the event's hostile byte
   clear; with the player having no target, a free Warrior who may gesture and has more than 10 % health says speech
   command `0x58` (a complaint). In `Human_ApplyPendingDamage` (`0x00265f70`) the friend's pending damage is set to
   0 (an AI victim, or an AI attacker), so **no health is lost**, but the code goes on to the reaction by state
   (`Hit_PlayReaction` `0x0026b0a0` for a standing one): **the teammate flinches or falls** as from an enemy's hit of
   that hit code, and hit armour and the other gates apply as usual. Two players hurt each other for real.
4. **A Warrior hit by his chief hits back** (`WarriorBrain_OnHitByChief`, `0x00306190`, from the brain's event 1, which
   every hit sends; confirmed (code)): only when the hitter is a player whose Warrior commands are not locked (game
   state `+0x418` + player), the Warrior has no fight goal, is not cuffed, not sparring (`+0x2e5`) and counts no
   hostiles (`+0x152`). A hit with no window open opens one of 10 s (the Warrior's brain `+0x2dc` = now + 10000 ms; an
   expired one is cleared with its count, byte `+0x2e4`); each later hit inside it adds 1 to the count, and the hit that
   brings it to 3, **the fourth hit within 10 s** (or the second, while the chief's own brain byte `+0x2e4` shows
   another Warrior already hitting back), clears the window and makes him push **AttackTarget**
   (`AttackTargetGoal_Push(him, chief, 20000)`) for 20 s, after popping a TauntPlayer goal on top. When the chief is a
   war chief (`+0x3ac` = 1) with a command in force, the command is re-issued as 0 and the chief's command menu is
   locked (`+0x42e` + player = 1). While one hits back, the [player's update](ai.md#think-player) gives every free
   member within 15 m **TauntPlayer**: the taunting ring.
5. **Throws into teammates.** A thrown enemy strikes every body he flies into ([Throws](combat-throws.md#throws)): he is
   no friend of the teammate, so the bystander takes the crushing high reaction (300-303, a knockdown) with no health lost
   (the −60 is never applied).
6. **Walking and sprinting into a teammate** is the physics' contact 4: he is slid along and pushed by half the move
   ([Physics: human contacts](physics.md#human-contacts)); there is **no knockdown** from running into anyone
   (only the charge's capsule and an airborne body strike, `Human_OnContact` `0x00219d50`), and the charge's strike
   on a teammate is a hit as in 3. A player who walks head-on into a teammate at a jog or faster and is still doing
   so 2 s later **trades places** with him (`Human_OnPushContact` `0x00219b08` → `Human_CanPush` `0x002320a0`, which
   sets each one's position to the other's when neither is in a fight stance, busy or held). In detail, confirmed
   (code) at `0x00219b08`: the contact counts only when the pusher is a player, his gait (`+0x1a8`) is 3 or more and
   his push is head-on (the dot of the two unit vectors below −0.8); the first such contact stores the time in the
   teammate's `+0x570` and a deadline of **now + 2100 ms** in `+0x56c`; a later counting contact at least 2000 ms
   after the first calls `Human_CanPush`, and a swap clears both. The second contact must also come before 2.1 s:
   every update `Human_StateUpdate` clears both words once the game time is past the deadline (confirmed (code) at
   `0x002405e0`-`0x00240604`), so the swap needs a counting contact in a window of 2.0-2.1 s after the first, about
   three updates.

**At runtime** (2026-10-11, PCSX2, confirmed (runtime)). Level80 checkpoint 2 was entered from a level99 state by
setting mode 8's level ([the runtime order](scripting.md#level80-hints)). Cleon was the player, with Vermin, Rembrandt
and Ajax following (`FollowPlayer`, goal 50). Input was the scripted pad of
[Driving PCSX2](../guides/research-workflow.md#driving-pcsx2), hooks `strike-contact`, `hit-by-chief`, `hit-reaction`,
`attack-target-push`, `push-contact` and `can-push` of `repo:research/traces/patches/`, with Vermin's record and
brain read every update:

- **Hits 1-3 land with a reaction and no health lost.** Cleon walked to 0.85 m from Vermin (stick 0.5, aimed at
  him), the stick went to 0.2 towards him for one update, then square was tapped (`ANIM_ATTACK_S1`, 12). Each tap
  logged `Strike_Contact(Cleon, Vermin)` and `Hit_PlayReaction` on Vermin, who played 272 (`ANIM_REACT_SMALL_HIGH_FRONT`)
  and kept 1800 health. This held for every hit before the fourth, in both runs.
- **Level80 locks the commands, and that blocks the hit-back.** In the first run (four taps 1 s apart, two of
  which reached him) `WarriorBrain_OnHitByChief` ran on each hit but opened no window: game state `+0x418` for player
  0 was 1, because `level80.lua`'s chapters 1-4 call `WCLockCommands(player, true)` in their set-ups and only chapter
  4's clean-up unlocks it. **No Warrior hits back anywhere in mission 1 of `level80`.** For the second run the byte was
  written to 0, as `WCLockCommands(player, false)` leaves it.
- **The fourth hit starts the hit-back.** With the commands unlocked, five hits 24 updates apart: the first set
  Vermin's `+0x2dc` to the game time + 10000 ms; the second and third raised `+0x2e4` to 1 and 2; the fourth (2.4 s
  after the first) cleared both and called `AttackTargetGoal_Push(Vermin, Cleon, 20000)`. The chief's own brain
  `+0x2e4` became 1. The next update Vermin had **Fight** (`0xf`, target Cleon) on top of AttackTarget, on top of
  FollowPlayer.
- **The fourth hit hurts.** The fourth and fifth hits each took **22** health from Vermin (1800 → 1778 → 1756): once
  he hits back he is no longer treated as a friend, so the pending damage is applied (inferred: the friend test of
  `Human_ApplyPendingDamage` was not traced at runtime).
- **He fights for real.** Vermin's punches cost Cleon 209 health each (1530 → 346 in 22 s). Cleon's target
  became Vermin. The goal stack went back to FollowPlayer 661 updates (22.0 s) after the push, AttackTarget and
  Fight together.
- **Sprinting into a teammate knocks no one down.** Cleon sprinted at Vermin from 5.1 m with L2 held and the stick
  at full deflection, re-aimed at him every 2 updates and scaled so its larger axis is 1.0 (a diagonal of plain
  components falls under the sprint's 0.95). He reached gait 5 at 10.25 m/s. At contact the player stopped dead
  (0.06 m/s two updates later). Vermin played 402 (`ANIM_MOVEMENT_STEP_BACKWARD`), with no reaction, no state bits of
  a fall and no health lost. An earlier run that met him at gait 4 (8.26 m/s, the sprint still speeding up) gave
  the same. `Human_OnPushContact` ran on Vermin at every contact.
- **Walking into one pushes him; a walking player seldom gets to the swap.** At a walk (stick 0.75 or 0.9 re-aimed,
  gait 2, 1.63 m/s) the player shoves him along at about half his speed, and an idle Warrior often steps aside first
  (400 `ANIM_MOVEMENT_STEP_RIGHT`). Running in (gait 3-4) started the timer: `+0x570` = the time, `+0x56c` = time +
  2100. But the contact brought the player down to gait 0-1, so no counting contact came 2.0 s later, and the
  deadline was cleared on time. `Human_CanPush` was never called in five runs: a player cannot keep a counting
  contact going for 2 s, so in play the swap is rare.
- **The swap itself** (a second pass, 2026-10-11, level80 checkpoint 2 entered the same way, commands still
  locked, hooks `push-contact` and `can-push`). Cleon ran at Vermin from 4.6 m, stick 1.0 re-aimed every update,
  no L2: gait 3 from the 7th update, gait 4 from the 14th. At 1.46 m the test aged Vermin's timer over PINE
  (`+0x570` = the game time − 2050 ms, `+0x56c` = the game time + 400 ms), as a first contact 2.05 s earlier would
  have left it. Two updates later the contact came at gait 4: `Human_OnPushContact(Vermin, …)` then
  `Human_CanPush(Vermin, Cleon)` from `0x00219d18` (the return address of its call at `0x00219d10`, confirmed (code)),
  on the same update. Vermin's position became exactly Cleon's
  (−189.45, 168.24), Cleon's became Vermin's (about (−190.2, 167.7)), each kept his own heading, and both timer words
  were 0 the next sample. Vermin played no reaction and went on following.
- **Circle with only a teammate in reach is a grab miss.** Cleon walked to 0.81 m from Vermin (stick 0.5, aimed at
  him), the stick went to 0.2 towards him for one update, then circle was tapped. His target was none (`+0xc8`
  −1). `Grab_Start(Cleon, mode 0, base 72)` ran (hook `grab-start`) and 71 and 69 were queued the same update (hook
  `anim-dispatch`); 21 updates later (71's 5 and 69's 16) he was back in the idle 388. Vermin played nothing and
  lost no health.
- **The complaint is said.** Two square taps at Vermin from 0.77-0.83 m, Cleon with no target: each logged
  `Strike_Contact(Cleon, Vermin)`, `WarriorBrain_OnHitByChief` and reaction 272 on Vermin, and
  **`Human_SayCommand(Vermin, 0x58, …)`** (`0x002205e0`) from `Strike_Contact` (return `0x0021b4a0`, the call at
  `0x0021b498`, confirmed (code); hook
  `say-command`) on the same update. Vermin's health stayed 1800. Whether the line is heard (the speech system may
  drop it) was not checked.

**Coney fixes** (user item 10; the blocking itself is user item 40, physics.md#human-contacts):

1. Run the strike test against every human body, not only the opposed ones: Coney's `Humans::opposed` (the
   `AiHumans` opposition, `!Gangs::friends`) currently gates the strike test as well as the targets, which is why the
   player's punches pass through teammates.
2. Keep friends out of every target pick, the grab and tackle search and the locks, as now; a grab next to a
   teammate plays 71 then 69 and connects with nothing.
3. A hit on a friend: impact and hit effect, no health lost (unless both are players), the reaction by hit code as
   for an enemy, the complaint line `0x58` for a Warrior hit by a player with no target.
4. Port `WarriorBrain_OnHitByChief`: the 10 s window, the fourth hit, AttackTarget on the chief for 20 s with Fight
   on top, the taunting ring while it lasts, the command reset and menu lock. Gate it on the commands lock
   (`WCLockCommands`, which Coney keeps as `StoryState::noRetaliation`, set by `makeWcLockCommands` in
   `repo:src/scripting/story_bindings.cpp`, and which replaces the stand-in in `warriorEvent`,
   `repo:src/ai/brain_events.cpp`), so in `level80` no Warrior ever hits back; apply the damage of the fourth hit on
   (the Warrior is no longer a friend).
5. Thrown bodies strike teammates too (the knockdown with no damage).
6. With the human blocking in (item 40): a head-on contact at gait 3 or more starts a 2 s wait, and another such
   contact 2.0-2.1 s after the first swaps the two places (both bounds confirmed (code), the swap seen at runtime).
7. Tests: square into a teammate 0.8 m ahead plays reaction 272 on him and leaves his health unchanged; three such
   hits within 10 s leave him alone and a fourth starts his AttackTarget and Fight on the player and costs him the
   hit's damage; in `level80` (commands locked) the fourth hit starts nothing; a sprint into a teammate stops the
   player and knocks nobody down; circle with only a teammate in reach plays 71 then 69 and leaves him alone (seen
   at runtime); a square that touches him makes him say `0x58`.

### The fight stance {#fight-stance}

**The fight stance is record `+0x00` bit `0x1` or `0x2`** (`0x00228340` tests `0x3`). Confirmed (code) at the cited
addresses:

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
  0. **The nearest enemy** (`Player_FindNearestEnemy`, `0x0027b1a0`): the humans within **3 m** (`0x005104c4`, 9
     in play) that the enemy filter `TargetFilter_StanceEnemy` (`0x002791b0`, threats only: "What an attack does
     to the lock" below) accepts, nearest first, kept if `0x0021c240` agrees; with none, the current target
     unless it is down (state `0x80000000` or `0x100000000`, or `0x00227dd8`). It also writes record `+0xd4` /
     `+0xd8`. A current target that is down, knocked out or in a scene is dropped (and the lock bit `0x8` cleared)
     unless it is flying in a knockdown (`+0x08` `0x400000`).
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
     [Constants](combat.md#constants)): when the movement state is not the lock-on one (`Human_FightStanceMove`), the
     state word lacks `0x200000`, and the nearest enemy is within **2 m** (`0x005104bc`, 4) or the player is locked:
     nothing with an object of set 4 in hand; else `0x1008000` cleared, `Player_EnterStance` (`0x00280068`:
     `0x00230140`, then `0x1` if clear; `0x00230140` clears state `0x200000`, setting `0x20000000` when he is then free
     and still, and zeroes record `+0x114`), the nearest taken as the target (when there is none yet and `+0x08` has
     nothing outside `0x320100`; and again when there is one), and unless state `0x200000` or human `+0xe0`
     `0x200000000000`, the lock (`Player_LockOn`, `0x00280158`: state `0x5`, and the lock-on movement state through
     `0x00221038`).
  4. **It holds** (confirmed (code)) while the target timer (record `+0xf4`) runs and a target is kept; or, with the
     nearest within 2 m
     and the target beyond **2.5 m** (`0x005104c0`, 6.25 in play), the target is swapped for the nearest when `+0x08`
     is clear; or the target is within **3 m** (`0x005104c4`, 9 in play).
  5. **It ends** once the timer has run out, `+0x08` is clear and he is neither locked nor in state `0x8` with
     `0x200000` (`0x00227cd8`): the target is dropped; a stance in the lock-on or the free movement state is left
     through `0x002801e8` (a state code `0xe` → 7). Then, outside state `0x200000`, the nearest human on the brain's
     list `+0x164` within **20 m** that is an alerted (brain `+0x21c` set) non-pad human inside a 120° cone of his
     facing (`Human_IsInFieldOfView` with 2.094 rad) keeps him in the stance **if he is standing** (gait 0):
     `Player_EnterStance` again. Otherwise a stance still on is left by `Player_ExitStance` (`0x00280118`: `0x5`
     cleared and the combo count zeroed, the movement state kept), and a moving player's state code `0xb` becomes 7.

So in play a player is in a fight stance **while locked on** (L1, or an enemy coming within 2 m with the street's
auto-lock), for the rest of any attack he starts, and while he stands facing an alerted enemy within 20 m; a run or
sprint ends it. A locked-on combat walk keeps `0x1` (it moves at the base speed, gait 3), so **square there skips the
walk attack and goes to the snap or `S1`**, which is what level99's lesson 7 needs ([Attacks](combat.md#attacks)). At
runtime L1 held gave state `0xd` (confirmed (runtime), below).

**L2 ends the stance and the target at once** (`Player_UpdateSprint`, `0x0027ce90`), confirmed (code): with L2 held
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
play) once the stance timer had run out (confirmed (code); the timer at that update was not read). So a player who
holds L2 after an attack never walks toward the old target.

**Lock-on** is the fight stance's movement state `0x00241b90` (the normal one is `0x00240e38`), confirmed (code):
the human is **locked** when it has a human or object target and either L1 is held (`CfgLockOn`), or `CfgAutoLock`,
`CfgAutoLockAndCombat` (1 in the street) or auto-combat is on. Locked, it **turns to face the target every update**
and the stick (dead zone 0.12) moves it without turning, the stick's angle from the facing (record `+0xdc`) picking
the combat-walk clips 380-387; unlocked, it turns to the stick. Blocking stops the movement. A target farther than
2.5 m (`0x005104c0`, 6.25 in play) is not dropped here but **ignored** unless `Human_IsLocked` (state `0x8` and
`0x4`, L1): the walk turns to the stick as unlocked; the drop is the stance update's, at 3 m (step 5 above).

**L1**: pressed (`0x0027da10`) or held (`0x0027dc00`), it sets record `+0x00` `0x8`, enters the stance and picks a
target; released (command 8, `0x00227b98`), it clears `0xc` and opens a 264 ms window in which a second tap picks and
locks. While aiming a throw (`Human_MoveThrowAim`, `0x00244770`) L1 leaves the aim; with weapon type 5 and no enemy
target it enters mode `0x13` (`0x00227b30`). Confirmed (code); confirmed (runtime): L1 held gave state `0xd`, a target,
and the movement state `0x00241b90`.

**At runtime** (confirmed (runtime), PoizoCiv 2 m ahead as the target): the street has `CfgLockOn` = 0, so **L1 alone
does not lock**. With `CfgAutoLockAndCombat` written to 0, L1 held gave state `0xd` and a target, but the player kept
its heading while the target circled it at 3° per update, and walked in the combat-walk clips without turning to it.
With the street's `CfgAutoLockAndCombat` = 1 the same circling target was **tracked exactly**: the player's heading
followed it by the same 3° per update, the target's bearing staying at 0°. Record `+0xdc` (the stick's angle from the
facing, clockwise) picked the combat-walk clip as below (stick 35 % to 80 %, turned by the camera, so the angles are
not the script's):

| `+0xdc` | 18°, 354° | 27° | 79°, 88° | 120°, 143° | 185° | 329°, 348° |
| --- | --- | --- | --- | --- | --- | --- |
| Clip | 380 forward | 381 forward right | 382 right | 383 back right | 384 back | 387 forward left |

A target that the walk took beyond 2.5 m was dropped and the player went back to the walk start 413 (by the code the
walk ignores the target beyond 2.5 m and the stance drops it beyond 3 m; that run did not tell the two apart).

**Combat-walk speed.** `0x00241b90` normalises the stick vector (dead zone 0.12) and multiplies it by `0x00221710`'s
speed, so the stick's deflection does not change the speed (confirmed (code)). The clips 380-387 each cover 2.4 m in
0.7 s, **3.429 m/s**, with record `+0x164` × `+0x3a4` = 1.0. At runtime the player moved at 3.43 m/s from the first
update at 35 %, 60 %, 80 % and 100 %, with no ramp; slower values came only while pushing into the target. The strafe
ids 31 / 32 / 33 move at 2.358 / 2.613 / 4.236 m/s. Confirmed (runtime).

**Backward** (confirmed (runtime), 2026-10-05, each run from a fresh copy of slot 6 with L1 held and the civilian
locked 1.2 m ahead): straight back at 35 %, 60 % and 100 % alike, the walk went into 385 (back left, the stick's
angle from the facing 208° at first) at **3.12, 3.12, 3.11, 3.10, 3.09 m/s** for 5 updates, then 384 at **3.429
m/s** to the end (2 s, 4 m). Back left at 35 % (253° at first) started at 3.38-3.28 m/s for 5 updates, then 3.429.
So the speed never depends on the deflection, and the backward walk is the same 3.429 m/s once it runs; only its
first 5 updates (1/6 s) out of the stance are 1-10 % slower, more so the closer the stick is to straight back. The
earlier 2.6-3.0 m/s came from runs made one after another without reloading, where the player walked into other
bodies and objects (a later backward run of that kind dipped to 2.54 and 3.08 m/s for two updates; inferred). What
slows the first 5 updates is not traced (speculative: the 1/6 s fade into the walk clip). The velocity the code asks
for is the stick's direction × 3.429 every update (`0x00241b90`, record `+0x164` = 3.429, human `+0x3a4` = 1.0,
`0x005101e0` = 1, all read at runtime).

**In a grab** the stick does not walk at all below 0.95: the grab's movement (`0x00245310`) acts only above that
magnitude and then walks the pair backward at 1.125 m/s (front hold) or about 1.22 m/s (rear hold), as in [Moving a
grab](combat-grabs.md#grab-turn). So a 35 % stick moves a grab not at all, a fight stance at 3.429 m/s, and the free
player at the walk speed.

**What an attack does to the lock** (the armed run sweep 501 and the moving attacks; confirmed (code) at the
addresses cited):

- **The pick is not a lock.** `Player_Square`'s armed branch (`0x00286cc8`, held set 1-3) calls
  `Player_PickTarget(2.5 m)` and stores the result as the target (`Human_SetTargetObject`) for a pad player, at a run
  too; the unarmed 23 and 24 do the same with 2.0 m. None of them calls `Player_EnterStance` or `Player_LockOn` on
  that path: the armed run branch goes straight to `Player_StartCharge(h, 501)`.
- **No pull and no turn.** `Player_StartCharge` (`0x00264a80`, confirmed (code)), which plays 23, 24 and 501, only warns
  the target
  (`Human_WarnOfAttack`) and pushes the clip holding `0x1000000`: no `Attack_SteerToTarget`, no turn, no slide.
- **No stance logic during the clip** (confirmed (code)). `0x1000000` is inside the dispatcher's busy gate `0x5c7fee0`
  (`Player_UpdateActions`, `0x0027c120`), so `Player_UpdateStanceOrIdle` / `Player_UpdateSprint` (`0x0027ce90`) do
  not run while 501 plays: nothing enters the stance or locks during the sweep.
- **After the clip, running keeps it free** (confirmed (code)). The gait is human `+0x1a8`, set from the speed by
  `Human_SetMoveVelocity` (`0x0023cf00`). With the run resumed (gait 4, `+0x08` clear) `Human_IsRunningFree` holds,
  `Player_UpdateSprint` returns early (and, as the player is not in a stance, drops nothing). The target stays in
  `+0xc8`, but the movement state is still the free `Human_PlayerLocomotion` (`0x00240e38`), which turns to the
  stick: a stored target alone never steers the player. Only `Player_LockOn` (`0x00280158`) installs the lock-on
  movement `Human_FightStanceMove` (`0x00241b90`), and only from step 3 of the stance update (nearest enemy within
  2 m, not running) or L1.
- **The lock-on movement faces the target only within 2.5 m** (confirmed (code)). `Human_FightStanceMove` treats the
  target as absent
  when its distance² is above `0x005104c0` (6.25 in play, 2.5 m) unless `Human_IsLocked` (state `0x8` and `0x4`, L1;
  the auto-lock's state `0x5` is not enough), or when the target lacks type bits `0x100040`; then it turns toward the
  stick, capped per update by `Human_MaxTurn`, instead of snapping to the target. With no target at all it hands
  control back to `0x00240e38`. The stance update drops the target beyond 3 m once its timer has run out (step 5).
- **Only a threat is auto-locked** (confirmed (code)). The stance's nearest-enemy search (`Player_FindNearestEnemy`,
  `0x0027b1a0`, 3 m)
  filters with `TargetFilter_StanceEnemy` (`0x002791b0`, a function defined on this branch): the other must be a human
  whose object query (vtable `+0xf4`) holds, targetable (`0x00227d78`: not flag `0x100000000000`), not friendly
  (`Human_IsFriendly`, `0x00222a90`), not a pad player, within 2 m in height (`0x00510970`), **a threat**
  (`Human_IsThreatTo`, `0x00222a48`: `Brain_IsThreat`, the two gangs are enemies or both are players), and neither
  down nor dead (`0x00227e60`), nor in state `0xe0000` (`0x00223b48`), nor holding `0x40` (`0x00223c10`). A neutral
  civilian, whose gang is no enemy of the Warriors, is refused, so **walking past a civilian never enters the stance
  or locks** (user item 14); one does once his gang has been made an enemy (a civilian of kind 3 who fights back,
  `Hostile_Attack`, [AI goals](ai-goals.md)). The attack picks are wider: `TargetFilter_Enemy` (`0x00279410`) and
  `0x00279568` skip only allies and the same gang, so square still picks and hits a civilian, without locking.

**Where Coney differs** (as of this branch's base), and the fix for the implementer:

- **The spin (item 34).** `Fighter::steer()` exempts only the charge (id 0), so 23, 24 and 501 pick a target and turn
  and slide onto it (`steerOnto`); and `Fighter::lockTarget()` treats any target as a lock, so the target an attack
  picked puts the player in `Human::combatWalk` as soon as the clip ends. `combatWalk` sets the heading straight to
  the target every update (no cap, out to 2.5 m) and moves at the combat-walk speed, so the gait is never Run and
  `trackTarget`'s "running drops it" never fires: walking past the enemy swings the heading round it every update.
- **The civilian lock (item 14).** The player's targets list (`Humans::gatherTargets` with `AiHumans`' opposition
  `!Gangs::friends`) holds every non-friend, neutral civilians included, and `trackTarget`'s stance step takes the
  nearest of them within 2 m (`kStanceEnterRange`), searching 6 m (`kStanceTrackRange`) where the original searches 3 m.
- **Fix.** (1) Play 23, 24 and 501 as `Player_StartCharge` does: warn the picked target, no steer or slide. (2) Keep
  the target and the lock apart: an attack's pick only stores a target; the lock-on movement (`combatWalk`) runs only
  after the stance step (nearest threat within 2 m, not running, outside an attack) or L1, and ends on running,
  sprinting or L2 as `Player_UpdateSprint` does. (3) In the lock-on walk face the target only within 2.5 m (or while
  L1 locks); beyond it turn to the stick at most `Human_MaxTurn` per update. (4) Drop the target beyond 3 m once the
  stance timer has run out; search the stance's nearest enemy within 3 m and only among threats (gangs enemies, both
  players), keeping the wider list for the attack picks. Take 2 / 2.5 / 3 m from `CfgDistances`, not the executable's
  defaults. Tests: a bat sweep at a full run past an enemy 1.5 m to the side keeps the heading on the stick for all of
  501 and the run resumes with no lock; a stick at 60 % walking past a neutral civilian at 1 m never locks or turns;
  the same past an enemy-gang member within 2 m locks and faces him.

**Turning into an attack** (`Attack_Start`, `0x002625a8`): with the target within the attack's far range, it tells
the target (`0x0021d5c0`) and steers with `0x002761c8`: turn and slide so that the target, moved by its velocity over
the time to the hit + 0.1 s, sits at the clip's reach (+0.07 m for a target scaled above 1.1, -0.1 m from behind).
Beyond the range it only turns (`0x00276008`), capped at 8. Confirmed (code).

**The steer in detail** (`Attack_SteerToTarget`, `0x002761c8`; confirmed (code) unless marked):

- **Its inputs.** `Attack_Start` passes the human, the target (`0x00226e60`, its locked target) and a time `T` = the
  time to the clip's **first event** of types 9, `0xf`, `0x13`, `0x2c`, `0x34`, `0x36` or `0x41` (its frame / 30
  divided by the clip's rate; with none, the clip's length; `0x00101658`). The steer clamps `T` to that time + 0.1 s,
  so `T` is the time to the first event. The per-kind numbers come from the record's attack table (record `+0x160`,
  16 bytes per kind): `+0x4` the **reach** (`0x002544a0`), `+0x8` the **far range** in mm (`0x00254508`; when it is
  not above the reach, reach × 1.25), `+0x0`/`+0x2` an offset in mm (`0x00254418`) and `+0xc` flags (`0x00254d60`).
  `Attack_Start` steers only when a target is locked and its distance (`0x00229960`) is within the far range; with
  flag `0x8` and the global `0x005102c4` set it uses the variant `0x00275678` instead, which aims at the target's
  head and slides for the first event + 0.1 s ([Moves: the two steers](combat-moves.md#two-steers)).
- **The goal.** The target's position plus its velocity × (`T` + 0.1) (cut to 0.5 m long when the full lead would
  carry it more than 1 m and farther away; [where the attacker stands](combat-moves.md#reach)), minus the reach along
  the line from the human: the human should stand
  at the reach from where the target will be.
- **The turn** (`Human_TurnToOver`, `0x0023cf88`, confirmed (code)): the heading to the goal minus the current facing,
  wrapped to (−π, π]; skipped under
  0.01 rad. It stores the rate (angle / `T`) in human `+0x308` and `T` in `+0x304` (flag `+0x332`).
- **The slide** (`0x0023d2b8`): a velocity (goal − position) / `T` in human `+0x2e0`, with `T` in `+0x300` (flag
  `+0x331`); none when the goal is under 0.01 m away (snapped), none at 13 m or more, and none when it would exceed
  50 m/s with `T` above 1/30 s.
- **Applied** each character step by the human's state update (`0x0023fea8` → `0x0023f5e0`): the rotation turns by
  rate × dt (dt = `0x005102cc`, 1/30 s; the last step only by the time left), and the slide velocity is copied to
  `+0x2f0` (scaled by the time left / dt on the last step), which moves the body on top of the clip's root motion
  (inferred: the add itself was not read). Both count `T` down by dt and stop at 0. So the steer lasts
  **ceil(`T` / dt) updates**, at a **constant rate**: no easing.
- **When it starts.** `Attack_Start` runs in the dispatcher (step 9 of [Humans_Update](tasks.md#humans-update)), after
  the human's state update (step 8), so the first turn and slide show on the update after the attack's clip starts.

At runtime (`combat_cross`, the original, X1 at a target 1.36 m away; confirmed (runtime)): clip 11 starts on step 42
with the body still; from step 43 the heading turns **2.533° per update for 9 updates** and 0.633° on the 10th (step
52), 23.43° in all, then stops, on the update the chain window opens (phase 2: event `0x2c`, the clip's first event).
That is a rate of 76°/s over `T` ≈ 0.308 s (9.25 updates). The body moves at 0.53, 0.71, 0.73, 0.74, 0.67, 0.77, then
1.34, 1.34, 1.25, 0.81 m/s (the clip's root motion plus the slide), and the distance to the target falls from 1.364 to
0.825 m. So Coney should neither snap the facing nor spread the reach over the whole clip: turn at angle / `T` and
slide at (goal − position) / `T` for the `T` up to the first event, starting the update after the clip.

#### The led steer {#led-steer}

`Attack_SteerLed` (`0x00275678`), for a kind with flag `0x8`. Confirmed (code) unless
marked:

- **Its target point is the target's head**, aim point 0 (`Human_GetLedSlotPoint(target, 0)`, `0x00226aa0`), not his
  position. `Human_UpdateLedSlots` (`0x0023cd30`) writes it: the human's world transform (vtable `+0xac`) composed with
  the transform at `+0x500` (`Transform_Compose`, the child in the parent's frame). `+0x500` is pose bone 6, the head,
  in the model's frame (x right, y forward, z up, from the feet, already times the human's scale; [Camera: the body
  point](camera.md#body-point)). So the offset is **turned by the human's full rotation**, not scaled again and **not
  flattened**: the point is at head height. It is not a formation offset and not the hips (`+0x4e0` is aim point 2).
  The points are refreshed only when read with the move dirty (`+0x255`), so the steer aims at the head as it is
  when the attack starts.
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
- **In the fight idle 358** the head leans forward: on the player, `+0x500` ran x 0.024 to 0.051, y 0.075 to 0.165 m
  (confirmed (runtime), an earlier recording of the same skeleton), so aim point 0 is **0.09-0.17 m ahead of a fighter
  along his own facing, slightly to his right**: toward an attacker he faces, never away from him. An AI target held
  in 358 was not recorded: forcing state bit 1 on a brain-off target did not change its idle (the bit was cleared
  each update).

## An AI's attacks {#ai-attacks}

An AI human attacks through the same dispatcher and attack functions as a player: the [attack
action](ai.md#attack-action) writes a command into its per-player record and `Player_UpdateActions` (`0x0027c120`)
reads it ([The dispatcher](combat.md#dispatch)). What differs for a human that is not pad-controlled (per-player `+0x1b`
= 0, human `+0x1b0` = −1), confirmed (code) at the addresses cited:

- **The target is the brain's.** `Human_GetTargetHandle` and `Human_GetTargetResolved` (`0x00226e20`, `0x00226ea8`)
  first copy the brain's human target (`+0x124`) and object target (`+0x128`) into the human
  (`Human_SyncTargetFromBrain`, `0x00226ee8`). Square (`0x00286cc8`) and cross (`0x00287a18`) call
  `Player_PickTarget` only for a player, so an AI strikes its brain's target; square alone falls back to
  `Player_PickTarget` (2 m, the nearest foe within 54° of the facing, [Target selection](#targets)) when that target
  is more than **2.5 m** away (distance² above `0x005104c0`, 6.25 in play). The steer onto the target is the player's
  ([Turning into an attack](#fight-stance)).
- **The stick** (confirmed (code)). `Player_GetAimHeading` (`0x0021d1a0`) and the snap test read the per-player
  record's stick, which an
  AI's attack action clears and then writes only when the action has an angle (magnitude 1.0,
  [AI](ai.md#attack-action)). Without one an AI aims along its facing and never snaps, and the walk attack 23 (stick
  0.12 or more) does not happen.
- **The run attack** (confirmed (code)). `Human_WantsRun` (`0x00225c10`) ignores the stick for an AI: it holds unless
  the state has
  `0x100000000` or any of `0x3981f3ff3` (the fight stance among them), any held flag is set, or the human carries an
  object of kind 4 or 6. So square from an AI out of the fight stance at gait 4 (cross at gait 4 or 5) plays the run
  attack 24 (`Player_StartCharge`, [Moving attacks](combat.md#run-attacks)); otherwise the AI takes the fight stance
  (`Player_EnterStance`, `0x00280068`) and plays `S1` or `X1`, or the grounded, tackled-target or grab-victim strike
  as a player does.
- **Not for an AI**: the locked strafe attacks (`0x1f`-`0x21`, which need a pad and `Human_IsLocked`), the counter grab
  and counter tackle from square or cross (`Human_CanCounterGrab`, `Human_CanCounterTackle`: an AI has command 3
  instead, [AI](ai.md#block)), starting a block ([The dispatcher](combat.md#dispatch)), and the climb and jump on
  triangle: an AI's triangle goes straight to `Player_TriangleAction` (`0x002811f0`).

**May this attack start now** (`Human_CanStartAttack(h, target, kind)`, `0x00224778`), asked by the attack action's
Start (`0x002fa9a8`) and by goals before they queue one (EngageEnemy's charge, [AI](ai.md#engage-enemy)). Confirmed
(code). It refuses when:

1. the target holds any of `0xc08200` in record `+0x08`, unless the target's state has `0x2000` and the attacker's
   `0x1000`;
2. the attacker holds any of `0x5c7eee0`, or its record `+0x118` (s16) is not 0;
3. the distance² is beyond `Attack_FarReachSquared` (`0x00230930`, confirmed (code)): the square of the attack table's
   far range
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
| 12, 13 (grounded strikes) | the target down (`Human_IsHighOrBusy`, `0x00225200`: mostly the target's states `0x40100f0800`, confirmed (code)), the attacker free and holding none of `0x48000`; always yes when the target is the attacker |
| 14 | the attacker free and the target knocked down |
| 15 | the attacker free and the target's state none of `0x40100f0800`; then yes with spray paint, otherwise only for an AI |
| 16-18 | the attacker free and the target's state none of `0x40100f0800`; class `0x77` (Diego) also not carrying an object of kind 4 or 6 |
| **19, 20** (charge, dive) | the attacker free, the target not down (state none of `0xe0000`), and the attacker **at the run speed**: gait 4 or 5 with no held flag and speed ≥ run speed − 0.01 (`Human_IsAtRunSpeed`, `0x00225998`, confirmed (code)) |
| **21** (tackle) | the attacker free, its power (record `+0x148`) at least a fifth of its maximum, the target's human flag `0x40` clear, the target free and holding none of `0x40` |
| 22 | the power and flag `0x40` as for 21; the target's state none of `0x7bfdc8f7fd0`, holding none of `0xc12200`; a target in state `0x20` only from in front (`Human_GetSideOf`, `0x002672d0`, confirmed (code)) |
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
rate), not `CfgAttackDelay`. Confirmed (code). Kinds 0-11 take `X1` 11, `S1` 12, 13, 15, 14, 16, 17, 19, 18, 20,
the snap 25 and 21 (kinds 0 and 1: **100 ms** while the attacker is knocked down); 12 → 193, 13 → 194, 14 → 237,
15 → 664; 16, 17, 25-29 the longer clip of a pair (653/655, 657/659, 147/151, 155/159); 19 → 0, 20 → 1; 21, 22, 23
the sum of a pair (3 + 5, 70 + 72, 466 + 467); 24 and 31 slot 17's clip; 30 → 118; 32 and 33 the shorter of 96/98
and of 100/102 (108/110 and 112/114 from behind); 34 → 90 (it also stops the attacker's speech); 35-39 → 225,
40 → 248, 41 → 252, 43 → 246, 44 → 242; 18, 42 and any other kind → 100 ms. With a held object of set 3 kinds 0, 1
and 5 use 36, 34 and 35; of set 2, 41, 39 and 40.

That time is **divided by the target brain's spacing byte** when the byte is not 0: `+0x14a` while the target stands,
`+0x14b` while it is down (state `0xe0000`, `0x00223b48`), times `0x00510ad0` (1.0), rounded. None of this happens
when either human is grabbing or tackling, or the target is cuffed. Confirmed (code) at `0x002fa9a8`.

**The spacing bytes** (`Brain_SetAttackSpacing` `0x002911d8`, `Brain_SetAttackSpacingHeld` `0x002911e0`), confirmed
(code):

- `Brain_Init` (`0x0028a9c0`) sets both to 1, and so does `Brain_SetTarget` (`0x0028cfe0`) when the last attacker
  leaves the target's attacker list (`+0x1a4`).
- When an attacker claims a slot on the target (`Brain_ClaimAttackSlot`, `0x0028df30`, [AI](ai.md#targets); confirmed
  (code)), the
  byte is raised to the **attacker's gang's** [`CfgGang`](../references/bindings/config.md#cfggang) value:
  `+0x14a` = max(itself, argument 2, gang record `+0x6c`) and `+0x14b` = max(itself, argument 3, `+0x6d`); a target
  whose brain is type 3 (a Warrior) gets `+0x14a` = 1 instead.
- `config_preload2.lua` gives most gangs 2 and 2; gang 1 3 and 3, gang 3 1 and 4, gangs 7 and 21 4 and 4, gang 8 2
  and 1, gang 13 1 and 1.

So against an enemy of a spacing-2 gang's attackers, the next attacker may strike after **half** the last attack's
clip, while a Warrior target makes them wait the whole clip. The attacker's own next attack (brain `+0x1e8`) is the
separate attack delay ([AI](ai.md#attack-action)).

## Breakables {#breakables}

**Glass cabinet** (slot 4): square at the cabinet plays 662 `SPECIAL_BREAK_OBJECT_MID` for about 1.1 s with the
object as the target; then **triangle** picks up one item each press with 464 (one-handed pick-up, left high).
Confirmed (runtime). **Car window** (slots 2 and 5): square plays 662 on the window. Confirmed (runtime).

`Player_ObjectAttack` (`0x00264178`) picks the clip by the target point's height above the feet: up to 0.8 m → 661
`LOW`, above → 662 `MID` (663 `HIGH` is never chosen); below the feet → 194. It first approaches when the object is
between reach × 0.5 and far × 1.5 of the clip's range. Confirmed (code).

**The store's cabinets** (mission 1's store, `level99`). A cabinet is no object: it is two or three type-1 glass panes
that `level99.lua`'s `AddGlass` places (a top at z 2.02 and a front or side, z 1.23-1.95; [Glass
types](../references/glass-types.md#glass-1)), around loose items from the level's object list, which this page's
breakables do not spawn: `level99_objs.txt` ([World objects](objects.md#objs-file)) puts 3 `dyn_pwatch` behind the front
panes at x 50.25-52.26, y 56.99, 3 `dyn_ringdmnd` in the next cabinet (x 48.16-50.17) and 4 `dyn_ring` in the side one
(x 48.13), all in object zone 26, which `BNESetup` (`global.lua`) enables. The items are `pickup_item`s of
`TYPE_SPECIAL` (12). Confirmed (runtime, the spawn records read at slot 4) and from the disc's list.

- **Any strike on a world object counts as a hit**, an ordinary punch or kick included: `Strike_Contact`
  (`0x0021b290`) treats every body it is called for alike, confirmed (code). For a world object it does five
  things:
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
  move. At slot 4 one square broke the front pane centred at (51.26, 56.99, 1.59), 12 updates after the press.
  Confirmed (runtime) (re-labelled one update later after the recorder fix, [Recording a trace](../guides/research-workflow.md#hooks)).
  A cabinet has **no damage stages**: a pane has no hit points, and its hit handler
  (`GlassScript_OnMessage`, `0x003e2d90`) acts once. It sends the pane's break event, swaps to the type's broken
  rectangle (`0x00010015` for type 1) drawn white at alpha `0x70`, makes one `sub_glass` shatter (shard size 0.2) at
  the pane's centre along its normal, turns off both triangles (type 1's extra hook is empty) and removes the body.
  Later hits find flag `0x800000` set and do nothing. The impact sound is `Strike_Contact`'s (material `GLASS`).
  Confirmed (code).
- **Triangle with nothing held** reaches the pick-up search `0x0024d810` ([Crimes: triangle](crimes.md#triangle), step
  5; `ContextAction_Use` ran with no context record each press). It gathers objects within **1.5 m** of the human (and
  of a second point, his position + human `+0x4e0`), and keeps one that is pickable (object flags `0x8000`), not the
  class `powerup_item` (walked over instead), and **in sight**: `0x0021c570` casts a ray from the feet + 1 m and, if
  that hits, from the feet + 2 m to the object, and rejects it when both hit. Each candidate is scored by the direction
  to it from a point 0.1 m behind the human, dotted with his facing: 3 at 0.38 or more, 2 from 0 to 0.38, 1 behind; the
  first of the best wins. Confirmed (code). An intact pane blocks the rays: a triangle before the square did nothing
  (confirmed (runtime); that the glass is what the ray hits is inferred).
- **The pick-up.** For a `TYPE_SPECIAL` item the search pins its record and sends it message 0; the human's message
  `0x14` (`0x0025e5a8`) then picks the clip by the type's pick-up animation (`CfgObj` argument 14, 5 for all four
  jewellery types): **463** at or below 0.8 m above the feet, **464** above (the cabinets' items are 1.32 m up), with a
  0.2 s blend, and hands `0x00275d10` the time to the clip's first event. Confirmed (code); that the item sends `0x14`,
  and that `0x00275d10` steers like `Attack_SteerToTarget` ([Target selection](#targets)), are inferred. At runtime 464
  ran 20 updates; the player turned 4.6° and moved up to 0.07 m per update for the first 5, and the clip's event
  (message 3) ran `Human_PickUpObject` (`0x0023bf00`) on the 6th update (re-labelled one update later after the recorder
  fix, [Recording a trace](../guides/research-workflow.md#hooks)).
- **The take.** `Human_PickUpObject`, for a `TYPE_SPECIAL` whose model hash is not one of the named mission items, adds
  item **10** (loot) ×1 with the notify flag, then money: the type's `CfgObj` argument 4 × the float at game state
  `+0x380` (1.0 here), without notify ($7 a watch, $10 a diamond ring, $8 a ring, $15 a necklace), and plays item 10's
  pickup sound; the record is removed for good (spawn record bit `0x40000`). Confirmed (code), and at runtime: three
  presses took the three watches, each adding item 10 ×1 with notify, then $7 without; a fourth press played nothing
  (the next cabinet's rings are behind its whole pane). [Inventory](player-state.md#pickup-callback) covers the
  callback.

Scenario `store_loot` (`repo:research/traces/scenarios/store_loot.toml`) replays this with the hooks that log it.

**A wooden fence** (slot 10, `level99` checkpoint 3.5): the charge, the dive or the walk attack breaks it with their
strike shapes, the run attack falls short; the break, its message 2 and the script's placement are under
[How a moving attack strikes](combat.md#moving-strikes) and [Barriers](objects.md#barriers).

## The stereo theft {#stereo-theft}

**Triangle** at a car's open window with a radio (slot 5) plays 683 `STEREO_STEAL_INTRO` (0.55 s), then the loop 684,
state `0x4000000`. `Player_UpdateTheft` (`0x0027e6d8`) runs one of three games by the mode at `+0x46` of the per-player
record `0x0051489c + 0x168 + p × 0x5c`. Confirmed (code):

- **Mode 1, mash** (freeing a handcuffed partner, [Crimes](crimes.md#triangle)): alternate L1 and R1 (commands 6
  and 4, held); each alternation adds 125 × 1.5 or × 0.7 (Warrior byte `+0x08`), the meter loses 15
  per update, and 1000 completes it (`CfgButtonMash`).
- **Mode 2**: **lock picking**, a timing game against a dial of three turning pins ([Crimes](crimes.md#lockpick)).
- **Mode 3, the car radio here**: `0x0027e908`. Rotate the left stick **anticlockwise**, the stick above 0.8 in this
  update and the last, turning less than 90° per update. The angle turned adds to `+0x48`; at each stage's target the
  stage (`+0x4c`) advances after a 250 ms pause; **4 stages** succeed (`0x00278628(h, 1)`, 685 `STEREO_STEAL_END`).
  The stage target was 6π (3 turns) here, from Warrior byte `+0x0b` (2): the target is 2π (one turn) when the byte
  is 0 or 1, else (byte − 1) × 6π, so 3 turns for 2 and 6 for 3 (`Human_GetClassSpinAngle`, confirmed (code) at
  `0x0022dd08`).

Any command outside the game's own set (square, circle, R1, L2 and others) **fails** it: 686 `STEREO_STEAL_FAIL`.
At runtime 1.5 turns per second took 9.6 s to succeed, and one R1 press failed it. Confirmed (runtime).

## Coney's implementation

The pick, the stance and the lock live in `repo:src/combat/lock_on.h` (the lock-on rule, the combat walk's clip, when a
target is dropped) and `repo:src/human/fighter.cpp` (the target in front, the grab search, the stance and the combat
walk); the steer is `repo:src/human/turn_and_slide.h`; an AI's attack checks are `repo:src/ai/attack_views.cpp`; the
object attack's clip is in `repo:src/combat/attacks.h` and the stick games in `repo:src/combat/stick_games.h`.
[Combat: Coney's implementation](combat.md#coneys-implementation) says what each file does. What differs from the
original is written beside each subject above: hits on teammates and the chief's hit-back ([Teammates](#teammates),
user item 10), the spin of the run attacks and the lock on neutral civilians ([The fight stance](#fight-stance), user
items 34 and 14).

## Open questions

- What slows the first 5 updates of a backward combat walk out of the stance (speculative: the 1/6 s fade into the
  walk clip; [The fight stance](#fight-stance)).
- Whether the steer's slide is added on top of the clip's root motion (inferred, the add itself was not read), and
  whether `0x00275d10` steers a pick-up like `Attack_SteerToTarget` ([Breakables](#breakables)).
- Whether a Warrior's complaint line `0x58` is heard or dropped by the speech system ([Teammates](#teammates)).
- The head point an AI target holds in its fight idle 358, not recorded ([The led steer](#led-steer)).
