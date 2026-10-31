---
kind: topic
system: ai
owns:
  functions: [
    "0x0016bb58", "0x00221cd8", "0x00222288", "0x002223e8", "0x002225d8", "0x002235f8", "0x00223628", "0x00223800",
    "0x00224060", "0x0024df40", "0x0028abc0", "0x0028b358", "0x0028bf00", "0x0028cfe0", "0x0028d190", "0x0028d2e8",
    "0x0028d358", "0x0028d538", "0x0028dc80", "0x0028de48", "0x0028df30", "0x0028e708", "0x0028fa18", "0x00290138",
    "0x002904e0", "0x002906b8", "0x00290ea8", "0x00291008", "0x002911f8", "0x0029ce98", "0x0029f098", "0x0029f230",
    "0x0029f3a8", "0x002add08", "0x002ade10", "0x002aebf8", "0x002af5b0", "0x002afa48", "0x002b2b90", "0x002b2c20",
    "0x002b2cd8", "0x002b2d00", "0x002b2e28", "0x002b2fc8", "0x002b3360", "0x002b3ab0", "0x002b4098", "0x002b4200",
    "0x002b4330", "0x002b54d8", "0x002b5520", "0x002b5808", "0x002c0430", "0x002c0748", "0x002fa918", "0x002fa9a8",
    "0x002fad30", "0x002fad70",
  ]
---
# AI fights: the fight goal, closing in and blocking

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records or, for the older ones, How this was checked.

## Summary

An AI human set on a target walks or runs up to him, waits his turn in a ring round him with the others who want
him, and attacks by pressing the same commands a pad would; when it is warned of an attack it may try to block or
counter. This page is that fight, seen from one fighter: how a fight starts and which target he takes, the fight goal
that paces his attacks, the Melee, FindEnemy and EngageEnemy goals that bring him in, the enemy scan and score that
choose among enemies, the attack action that presses the command, and the block goal.

How it works, in a paragraph: `Brain_Fight` adds the target to the enemy list, takes him as target (an attack slot
on him) and pushes three goals, FindEnemy, Melee and the fight goal. The fight goal ends at once when the target is
out of reach; Melee then picks the best-scoring enemy and sends the fighter in with EngageEnemy, and pushes a fresh
fight goal once he is close. Each update the fight goal asks `Brain_CheckAttack` whether he may swing now (his own
attack delay, a free active place on the target, the reasons to hold back); while he waits it repositions him in a
band round the target and lets him taunt; when he may, it picks an attack kind by weighted chance and queues it.

What this page is not: the brain, its goal stack and the attack kinds are on [AI humans](ai/index.md); each brain
type's handlers, help calls and side-picking on [AI brain types](ai-brain-types.md); the move actions and the
fight's footwork (MoveMelee) on [AI moving](ai-moving.md); how a whole gang shares a fight on
[Gang fights](gang-fights.md); what the attack does once pressed on [Combat](combat/index.md).

## Map

| Behaviour | Function | Section |
| --- | --- | --- |
| Start a fight on a target | `0x0028d2e8` `Brain_Fight`, `0x0028d190` `Brain_PushFightGoal` | [Starting a fight](#targets) |
| Take a target and an attack slot on him | `0x0028cfe0` `Brain_SetTarget`, `0x0028df30` `Brain_ClaimAttackSlot` | [Starting a fight](#targets) |
| The fight goal, each update | `0x002b3ab0` `FightGoal_Process` | [The fight goal in order](#fight-order) |
| How close a kind needs him | `0x00230d00` `Attack_ReachSquared` | [The reach of a kind](#attack-reach) |
| Pick an attack kind | `0x0028e708` `Brain_PickAttack` | [Picking the attack kind](#pick-attack) |
| May he swing now | `0x002906b8` `Brain_CheckAttack` | [May A attack now](#check-attack) |
| Active places and the gap between swings | `0x00291008` `Brain_ClaimActiveAttacker` | [Attack slots](#attack-places) |
| Wait in a ring and taunt | `0x002b2fc8` `FightGoal_Reposition` | [The reposition](#fight-reposition) |
| Tackle a runner | `0x002b2e28` `FightGoal_TryTackle` | [The tackle try](#try-tackle) |
| Grab, snap, and the held man | `0x002b3360` `FightGoal_TryGrab` | [The grab and snap try](#try-grab) |
| Close in, choose among enemies | `0x002aebf8` `MeleeGoal_Process` | [The Melee goal](#melee-goal) |
| Restart the fight while the target lasts | `0x002c0748` `FindEnemyGoal_Process` | [The FindEnemy goal](#find-enemy) |
| The run-in and its charge | `0x002afa48` `EngageEnemyGoal_Process` | [EngageEnemy](#engage-enemy) |
| Score an enemy, pick the best | `0x0029ce98` `Brain_ScoreEnemy`, `0x0029f230` `Brain_PickBestEnemy` | [The score](#enemy-score) |
| Sight and "may be attacked now" | `0x002223e8` `Human_CanSeeHuman`, `0x00290ea8` `Brain_IsAttackableBy` | [Sight](#sight) |
| Find enemies | `0x0028fa18` `Brain_MaybeScanEnemies`, `0x0028b358` `Brain_ScanEnemies` | [The enemy scan](#enemy-scan) |
| Watch a fight | `0x002b4330` `SpectateGoal_Process` | [The Spectate goal](#spectate) |
| Press the attack's command | `0x002fa9a8` `AttackAction_Start` | [The attack action](#attack-action) |
| Block and counter | `0x0029f098` `Goal_TryBlock`, `0x002b5808` `BlockGoal_Process` | [Blocking](#block) |

The entry points are `Brain_Fight` (from the brain types' event handlers, the scripts' `GoalFight` and the goals that
start fights) and the fight goals' Process functions, which `Brain_UpdateGoals` runs ([AI humans](ai/index.md#update-goals)).
Confirmed (code) at `0x0028d2e8` and `0x0028fbb0`.

## Data

The brain fields a fight reads and writes (target, enemy list, attack slots, active places, the next-attack times,
the melee ranges) are in [the brain](ai/index.md#brain). The fight goal's own fields:

| Field | Meaning | Evidence |
| --- | --- | --- |
| goal `+0x10` | the chosen attack kind, 45 for none (`FightGoal_Process` `0x002b3ab0`) | confirmed (code) |
| goal `+0x14` | when the next re-target is due (ms), set by `FightGoal_Resume` `0x002b2d00` | confirmed (code) |
| goal `+0x18` | the update counter (`FightGoal_Process` `0x002b3ab0`) | confirmed (code) |
| goal `+0x20` | the taunt timer: now + 750-1000 ms at `FightGoal_Init` `0x002b2c20`, re-armed by the reposition | confirmed (code) |
| goal `+0x24` | the deadline: now + the duration (`FightGoal_Init` `0x002b2c20`) | confirmed (code) |
| goal `+0x28` | the fighter holds an active place on the target (`FightGoal_Process` `0x002b3ab0`) | confirmed (code) |

The block goal's fields are listed under [Blocking](#block).

## Behaviour

### Starting a fight and choosing the target {#targets}

Confirmed (code) at the cited addresses unless a line says otherwise.

- `GoalFight(human, target, …)` is `Brain_StartFight` (`0x002b2b90`): clear the actions, then `Brain_Fight`.
  The target is not kept: once the first fight goal ends (at once for a target beyond 5.5 m), Melee picks by score
  like any fight, the old target getting only the +3 of [the score](#enemy-score). Confirmed (code); level80's last
  fight shows it ([After the order](scripting.md#level80-after-order)).
- **`Brain_Fight`** (`0x0028d2e8`) needs a threat response (`+0x21c` ≠ 0). It adds the target to the enemy list
  (`Brain_AddEnemy`, `0x0028d538`, which tells the gang's tactic with event `0xb`), takes it as the target
  (`Brain_SetTarget`, `0x0028cfe0`) and calls `Brain_PushFightGoal` (`0x0028d190`) with its duration argument.
- **`Brain_PushFightGoal(brain, duration)`** does nothing when the gang has a tactic (gang `+0x40`), the human is
  down or dead (`0x00223b70`, `0x00227dd8`), or a type-3 brain holds goal 9 (AttackTarget). Otherwise it **pops**
  any goal 8 (Melee) and `0x41` (FindEnemy) already on the stack, with every goal above them, then **pushes three**:
  `Goal_Melee` (`0x002add08`, the `GoalMelee` binding's worker) pushes FindEnemy (`0x41`) and then Melee (8), both
  given the duration, and sets the gang's alert state to 1 when the gang has no tactic; then the fight goal (`0xf`,
  `FightGoal_Init` with the duration). The stack ends **FindEnemy, Melee, Fight** (top): it pops and then pushes.
  Confirmed (code) at `0x0028d190` and `Goal_Melee`; lesson 12 shows the order ([the sparring fight](#level99-fight)).
- **`Brain_SetTarget`** releases the old target's attack slot and claims one on the new target
  (`Brain_ClaimAttackSlot`, `0x0028df30`): the target's list `+0x1a4` holds at most `+0x1e4` attackers, and a closer
  attacker takes the slot of the farthest.
- **Re-targeting**: once a second, the nearest threat within 1.5 m straight ahead, slot forced:
  [Gang fights](gang-fights.md#switches).

### The fight goal {#fight}

Confirmed (code) at the cited addresses unless a line says otherwise.

The fight goal attacks a target already within reach; [Closing on the target](#fight-approach) brings it there.

**The deadline**: `FightGoal_Init` sets `+0x20` = now + a random 750-1000 ms and `+0x24` = now + its duration
argument. Once `+0x24` has passed, the goal ends unless the fighter is still one of the target's active
attackers (`0x002911a8`), the target's byte `+0x11f` is set (`0x00290ea8`) and the fighter has no goal `0x35`;
a class-13 fighter without `+0x29` ends at once ([GoalRiot](ai-scripting.md#riot) for the 8 s case). `GoalFight` passes
−1, so its deadline (now − 1, unsigned) is already past and these checks run from the first update. This corrects the
earlier reading of these checks as attack pacing.

**Start and Resume** (`FightGoal_Start` `0x002b2cd8` calls `FightGoal_Resume` `0x002b2d00`, confirmed (code)): with a
target that passes `Brain_ValidateEnemy`, a kind is picked at once (`Brain_PickAttack` with `Human_CanUseAttackKind`)
and the re-target is made due (`+0x14` = now; step 6 runs when `+0x14` < now, so on the next update). Without one
both are left (`+0x14` starts at 0, also due). So every fight goal re-targets on its second update, and again one
second apart, and a resumed one (after a block or a reaction) picks its kind afresh. The sparring fight shows that
timing ([How this was checked](#runtime-retarget)).

#### The fight goal in order {#fight-order}

Confirmed (code) at the cited addresses unless a line says otherwise.

`FightGoal_Process` (`0x002b3ab0`), every update. Confirmed (code); this corrects an earlier reading that put the
block try before the range checks and left out the tackle try.

1. The update counter `+0x18` is raised by 1.
2. **Done** when the target fails `Brain_ValidateEnemy`, or when it holds no attack slot for A.
3. **The tackle try** (`FightGoal_TryTackle`, [below](#try-tackle)). When it queued a tackle, wait.
4. **Done** when the target is beyond 1.1 × far (5.5 m), unless A holds a throwable object or one of kind 4 or 6.
5. Every 30 updates, **done** when there is no walkable straight line to the target (`0x002221b0`).
6. **Re-target** at most once a second (`+0x14`), from A's sector record ([re-targeting](#targets)).
7. **The block try** (`Goal_TryBlock`). When it pushed a block, A gives up his active-attacker place on T and waits.
8. While actions are queued, wait. When the last attack ended (`+0x28`), give up the active-attacker place.
9. **The deadline** (`+0x24`, [The durations](#fight-durations)).
10. When no kind is chosen (`+0x10` = 45), **pick** one (`Brain_PickAttack` with `Human_CanUseAttackKind`).
11. **The stance.** Drop the fight stance for kinds 19 and 20, and always for a class whose `+0x11b` is 6 or 7.
    Otherwise take the stance.
12. **May A attack now?** `Brain_CheckAttack(B, kind, T holds an object)` (`0x002906b8`,
    [below](#check-attack)).
    - Any answer but 1: [reposition](#fight-reposition) and wait.
    - Answer 1 and the target is beyond the kind's reach ([The reach of a kind](#attack-reach)): queue a
      move-to-human action and wait. The distance is the 3D distance of the two transforms (`Vec_DistSq`).
        - It holds between 2 × T's capsule radius and max(reach, that + 0.1 m).
        - Its limit is 2000 ms, or 1000 ms for a class-13 fighter or with a FollowAndDefend goal (`0x35`).
    - Answer 1 and in reach:
        1. **The grab and snap try** ([below](#try-grab)). When it returns 1, wait. It may change the kind and set
           the stick angle.
        2. When `Human_CanStartAttack` refuses the kind: for kind 22, also queue a move to between 0.8 × near and
           0.8 × near + 1 m (1000 ms). Then pick again with `Human_CanStartAttack` and wait.
        3. Otherwise **queue the attack** (`Brain_QueueAttack(angle, B, T, kind, 0)`) and clear the kind to 45.
           Note that A holds an active-attacker place (`+0x28` = 1).
        4. A class whose `+0x11b` is 13, 6 or 7, or a dealer (type 5), may say line `0x11` (a taunt) after it: 20 %,
           when gestures are allowed (`Ambient_MayGesture`).

`Brain_QueueAttack`'s first argument is the **stick angle** in radians, −1 for none. It is written with the command
([The attack action](#attack-action)).

#### Facing the target in a fight {#fight-facing}

What turns a fighter toward his target once the run-in is over. Confirmed (code) at the addresses cited.

- **Nothing carries over from EngageEnemy.** Its End (`0x002af8d0`) only clears the actions and restores the turn
  boost; its stop (`0x002afa08`) clears the actions, sets gait 0 and, once the human stands, turns him to the
  target with a timed body turn (`Human_TurnToFaceObject` `0x00221cd8`, 0.3 s), which does not use brain `+0x110`.
  Whatever it left in the brain's move fields is wiped by the next update's `Brain_StopMove` (`0x0028ac18`, the first
  call of `Brain_UpdateGoals`): speed `+0x114` = 0 and heading `+0x110` = the human's own facing
  ([Characters](characters.md#ai-locomotion)).
- **The fight goal never writes the move.** `FightGoal_Process` (`0x002b3ab0`), its reposition, tackle and grab tries,
  the attack action (`0x002fad70`, which only asks gait 4 for a charge kind) and the block goal store nothing to
  `+0x110` or `+0x90`.
- **So the facing follows the target only through these:**
    - the **move-to-human action** ([MoveMelee](ai-moving.md#move-melee)), which the fight goal queues to close to reach
      and the reposition queues every time it waits: while it moves him, `+0x110` is the heading to the target and the
      speed a gait (above 0.1), so he turns toward the target by the stance's 24° per update while he strafes. Its
      Update writes nothing in the update it ends (in the band, no circling), and its Abort re-aims `+0x110` only for
      a "no stance" class.
    - the **attack's own turn**: the attack starts (`0x00261c80`, `0x00262368`, `0x00261a08`, the grab `0x0026c548`,
      the tackle `0x002707a8`) turn him to his target (the brain's, [Combat](combat-targets.md#ai-attacks)) with
      `Human_TurnToFaceObject`, and the steer onto the target (`Attack_SteerToTarget`,
      [Combat](combat-targets.md#ai-attacks)) runs to the clip's first event.
- **Otherwise nothing.** In a fight stance with no movement action running, `+0x110` is his facing and his speed 0:
  the control neither turns him nor starts a turn on the spot, and he does not track the target (no lock-on
  turn: that is the player's stance control, which an AI never runs). Inferred from the above: a fighter who
  stands in his band with no side to circle to keeps his last facing until the next move or attack.

#### The reach of a kind {#attack-reach}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Attack_ReachSquared(d², A, kind)` (`0x00230d00`) gives the square of how close A must be to start `kind`. The fight
goal moves A in when the distance² is above it ([step 12](#fight-order)). Ten other goals ask it too (Boxer,
BigBrawler, BigFighter, BigFighterA, HideAndSeek, ObjectPile, SaveHuman, FollowAndDefend, the dealer, and the fight
goal's grab try). Confirmed (code), jump tables `0x0055c0e0` and `0x0055c110`.

The reach is the **near** value of one anim id: `AttackTable_GetReach(A, anim)` (`0x002544a0`), the float at `+0x4`
of that anim's 16-byte row in A's attack table (record `+0x160`, [Combat](combat/index.md)). It is used as is: no scale,
no capsule radii, and not the far range (`+0x8`, which `Human_CanStartAttack` uses instead). The anim is chosen by kind:

| Kind | Anim id when A holds nothing |
| --- | --- |
| 0 | 24 at a run or sprint; 23 at a walk; otherwise 194 when A's target is knocked down, else 11 |
| 1, 3, 5-9 | 12 |
| 2, 4 | 11 |
| 10 | 25 |
| 11 | 21 |
| 12, 13 | 193, 194 |
| 14 | 237 |
| 15, 16, 17 | 664, 653, 657 |
| 19, 20, 21 | 0, 1, 3 |
| 22 (grab) | 72 |
| 23 (throw) | none: **12 m** always |
| 32 | 104 |
| 18, 24-31, 33-44 | none: **2 m** (empty-handed) |

When A holds an object, kinds 10-17 and 19-23 and 32 keep the anims above. The rest go by the held object's
anim set (`0x00231a80`):

- **Set 1** (a throwable), kinds 0 and 1: **12 m** when A's brain is not type 5 and the distance is above the melee
  near range (`0x00510ab4`, [Melee ranges](#melee-range)). Otherwise kind 0 anim 47, kinds 1 and 5 anim 45.
- **Set 2**: kind 0 anim 41; kinds 1-5 anim 39 (the test is `kind < 6`, unlike sets 1 and 3).
- **Set 3**: kind 0 anim 36; kinds 1 and 5 anim 34.
- **Set 5**: kinds 0-9 **30 m**. **Set 4**: kinds 0-9 **15 m**.
- Anything else: **2 m**.

The far-reach test of `Human_CanStartAttack` maps kinds 1-9 to their own clips (12-20) and kind 22 to 70
([Combat](combat-targets.md#ai-attacks)). This one uses only clips 11 and 12 for kinds 1-9, and 72 for the grab.

#### Picking the attack kind {#pick-attack}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Brain_PickAttack(B, T, filter)` (`0x0028e708`) returns one of the 45 [attack kinds](ai/index.md#attack-kinds), or 45
for none.
`filter` is a member-function pointer called as `filter(A, T, kind)`. It is passed as one 64-bit value in the old GCC
layout, `s16` delta, `s16` index (−1 for a non-virtual function; otherwise a vtable slot), then the function address; so
the constants the decompiler shows, `0x002240e8ffff0000` and `0x00224778ffff0000` (read at `0x00563ff8` by
`0x002b5768`), are delta 0, index −1 and the function, not a kind mask. `Brain_PickAttack` applies the delta to A and
calls through the vtable when the index is not negative (`0x0028e708`). Confirmed (code). Two filters are used:

- `Human_CanUseAttackKind` (`0x002240e8`) by the fight goal's first pick, by FightGoal Resume and by EngageEnemy;
- `Human_CanStartAttack` (`0x00224778`) by every other caller. It is the same per-kind test plus range, and the
  fight goal falls back to it when a kind cannot start.

Confirmed (code) at `0x0028e708` unless marked.

1. **Set-up.**
    - `n` = the number of attack slots taken on T (T's brain list `+0x1a4`); `m` = T's maximum (`+0x1e4`, 4 by
      default).
    - `c` = the number of slots taken on A himself (B `+0x1a4`); `M` = B `+0x1e4`.
    - `running` = A at gait 4 or 5 with no held flags (`0x00223a60`, `0x00223a98`).
    - `armed` = A holds an object.
    - `rear` = T is grabbed from the rear (state `0x20`).
    - **The pattern read**: when T is pad-controlled, T's target is A, and A's class threshold
      `t = (16 − class +0x37) / 16` (`0x002236c8`) is above 0:
        - `f` = the larger of T's two pattern bytes (`+0x5d0`, `+0x5d1`) × 0.05 (`0x00418398`);
        - `pattern` = (`t` ≤ `f`).
2. **T cannot be approached.** This is when `Brain_CanBeChased` on T's brain (`0x0028abc0`) is false, or B
   `+0x2d3` is clear. Return kind 0 (`X1`) when A holds a throwable object or one of kind 4 or 6; otherwise
   return 45.
3. **The armed bonus.** `bonus` = 100, or 0 for a class whose `+0x11b` is 9, 10 or 13.
4. **Candidates.** For every kind `k` from 0 to 44 that passes `filter`, these rules apply in order:
    1. **Running.** While `running`, only kinds 0, 19, 20 and 21 are candidates. When not running, 19 and 20
       (the charge and the dive) are not candidates. Kinds 0 and 21 are always allowed.
       (`AttackKind_IsCharge` `0x00229b60` is true for 0 and 19-21.)
    2. **The hurt rule.** Kinds 31-34 and 42-44 (the moves made while grabbed or on the ground) are left out when
       A is hurt (`0x00222ff8`) and B is attackable (`Brain_IsAttackableBy(B, none)`).
    3. **The weight.** `w` = the weight: B `+0x298[k]`, or the override table at B `+0x208`.
    4. **Crowded.** When `c` > 1:
        - kinds 10, 25 and 36: `w` += `w` × `c` / `M` (the snap, the throw from a grab);
        - kinds 14, 21 and 24: `w` ×= 1 − `c` / `M` (the tackles, a strike in a grab).
    5. **A busy target.** For kind 22 (the grab) when `n` > 1: `w` += `w` × `n` / `m`.
    6. **Armed.** When `armed`, kinds 0-9 with `w` ≠ 0 get `w` += `bonus`.
    7. **A rear-grabbed target.** When `rear`, kind 22 with `w` ≠ 0 gets `w` += 200.
    8. **The pattern.** When `pattern`, kinds 6-9 (the three-hit enders) get `w` += `w` × `f`.

    Each scaled weight is truncated to an integer. The kind and its weight are appended, and `w` is added to the
    total.
5. **The draw.** `r` = `Random_Int(total)` on stream `0x006eb880`, 0 to the total **inclusive**, made whenever step
   2 let the pick through, even with no candidates or a total of 0 (`0x0028ede8`). The answer is the first candidate
   whose running sum of weights exceeds `r`, so a draw of the total itself picks nothing (45). With no candidates,
   or a total of 0, the answer is 45. Coney's brains draw from their own seeded generator, not the shared stream.

So a crowded AI favours the snap and the throw and gives up tackles. Several attackers on one target make grabs
likelier. A weapon in hand makes the plain strikes likelier by a flat 100. A rear-grabbed target, held by a gang
mate, invites a grab (+200).

**The per-kind test** `Human_CanUseAttackKind(A, T, k)` (`0x002240e8`), confirmed (code). "A free" below means A's
state word has none of `0x7bf9e9f7ff0`.

| Kinds | Allowed when |
| --- | --- |
| 0, 1 | A holds a world object whose type byte `+0x87` is 1-3: A free. Otherwise as 2-9. |
| 2-9, 11 | A free, and T's state has none of `0xe3000` |
| 10 (snap) | A free, and the near human in sector 2, 3, 4, 5 or 6 of A's [sector record](ai-moving.md#neighbour-sectors) (flag bit 1, within 1.5 m) is in **A's own** attack-slot list (B `+0x1a4`, `Brain_HoldsAttackSlot(B, him)`): someone attacking A from his side or behind. |
| 12, 13 (grounded strikes) | T ≠ A. Either T is high or busy (`Human_IsHighOrBusy` `0x00225200`), or T lacks state `0x2000` and has held flag `0x400000`. Then A free. |
| 14 | T is A (1), or T is knocked down and A is free |
| 15 | A free; then A holds spray paint (1), or A is not pad-controlled |
| 16-18 | A free and T has none of `0x40100f0800`; for class `0x77` only without an object of kind 4 or 6 in hand |
| 19, 20 (charge, dive) | A free, T not down (`Human_IsNotDown` `0x00225500`), A at run speed |
| 21 (tackle) | A free, T's human flag `+0xe0` bit `0x40` clear, T free, T without held flag `0x40` |
| 22 (grab) | A free, A's power meter (record `+0x148`) ≥ his maximum power / 5, T's `+0xe0` bit `0x40` clear, and T's state none of `0x7bfdc8f7fd0` |
| 23 | A holds a throwable object |
| 24-30 (moves in a grab) | A grabbing (`0xc0`), none of `0x7bf9e9f7f00`, and the human A holds (`+0xc4`) is T |
| 31, 33, 34 | A grabbed (`0x30`), none of `0x7bf9e9f7f00`, and T holds A (T `+0xc4` = A) |
| 32 | A grabbed from the rear (`0x20`), none of `0x7bf9e9f7f00`, and T `+0xc4` ≠ A |
| 35-40 (moves while mounted) | A tackling (`0x400`), none of `0x7bf9e9f73f0`, A `+0xc4` = T |
| 41 | the same, and A carries cuffs |
| 42-44 | A knocked down (`0x80000`) with the Grounded reaction goal (`0x17`) running: allowed. Otherwise A tackled (`0x800`), none of `0x7bf9e9f73f0`, and T `+0xc4` = A. |
| others | never |

`Human_CanStartAttack(A, T, k)` (`0x00224778`) first refuses in these cases:

- T has held flags `0xc08200`, unless T has state `0x2000` and A has state `0x1000`;
- A has held flags `0x5c7eee0`;
- A has damage pending (record `+0x118`);
- the distance is beyond the kind's far reach.

It then runs a per-kind test, given in full on [Combat](combat-targets.md#ai-attacks). Confirmed (code), all kinds read.
It matches `Human_CanUseAttackKind` above except here:

- **10** (snap): A free only; no sector or attack-slot test.
- **12, 13**: T must be high or busy (`Human_IsHighOrBusy`); the other way in (T without `0x2000` holding
  `0x400000`) is not accepted. A must also hold none of `0x48000`. T = A still passes.
- **14**: T = A does not pass; T must be knocked down and A free.
- **15**: T's state must also have none of `0x40100f0800`.
- **22** (grab): T must also hold none of `0xc12200`, and a T in state `0x20` (grabbed from the rear) only when A is
  in front of him (`Human_GetSideOf(T, A, 1)` = 0).
- **23** (throw): A free, and the held object throwable with sets 1-3 counted for an AI too
  (`Human_HeldIsThrowable(A, 1)`); `CanUse` asks `(A, 0)`, which counts sets 1-3 only for a player.
- **27, 28** (in a grab): A in any of state `0x10c0` and holding T; **no** free test.
- **38, 39** (mounted): A in any of state `0x1400` and holding T; **no** free test.

So for grabs (22), tackles (21) and ground strikes (12-14) it mostly mirrors the pick. The start test is stricter on
the grab's target and on the ground strike's target, and looser for moves 27, 28, 38 and 39, which start even while
A is busy.

#### May A attack now: Brain_CheckAttack {#check-attack}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Brain_CheckAttack(B, kind, armed target)` (`0x002906b8`) returns 1 for "go". Every other value is a reason to wait,
which [the reposition](#fight-reposition) reads. The tests run in this order. Confirmed (code); the meanings in
brackets are inferred from the tests.

| Result | When |
| --- | --- |
| 2 | B's own next-attack time (`+0x1e8`) has not come |
| 0 | the kind is 45 |
| 3 | T may not be attacked by A (`Brain_IsAttackableBy`) |
| 1 | a charge kind (0, 19-21) while A is out of the fight stance (a charge needs no place) |
| 4 | The target holds an object, A's brain is not type 3, the game mode word is not 60-64, and neither T nor T's target is busy. Then, when A is empty-handed, his turn boost `+0x0b` < 1, and A and T face each other (facing dot < 0), and A is his gang's leader or within 15 m of him. (Hold back from an armed man near the leader.) |
| 8 | Only for an empty-handed A who is not yet one of T's active attackers (and not class 13 or 6). Another human in T's slot list, not A, not busy, without held flag `0x300000` and holding a heavy object (`0x00224080`: anim set not 1 or 5), is ready to attack (his cooldown passed). (Let him go first.) |
| 5 | The police have him. This applies when A is not a cop and T's target is not A. Either T's gang is wanted (`+0x5e8`) and a cop holds a slot on T, or T's target is a cop. For a cop A, it applies when T's target threatens him (`0x00290138`). Also for a cop when T is tackled or has state `0x10000000` or `0x10000000000` and is held by someone other than A. |
| 6 | A holds a throwable object or one of kind 4 or 6, and a gang mate in a 40° cone ahead (`Gang_NearestInCone` `0x0029df78`) is tackling or is nearer than T. A shuffle action is queued. (Do not throw through a friend.) |
| 7 | A is a Warrior (type 3), T is class 13 and not targeting A, A is empty-handed and not yet an active attacker, and a player in T's slot list who is busy tackling, throwing or front-grabbing stands within 0.75 × near of T. (Do not hit the boss while a player has him.) |
| 1 | A takes, or already has, one of T's **active-attacker places** (`Brain_ClaimActiveAttacker`, [below](#attack-places)) |
| 1 / 0 | kind 22 only: 1 when A is the near human in sector 3, 4 or 5 of T's record (behind him), else 0 |
| 0 | otherwise (no place free) |

#### Attack slots, active places and spacing {#attack-places}

A target limits his attackers twice. Confirmed (code) at the addresses cited.

- **Attack slots** (T brain `+0x1a4`, at most `+0x1e4`, 4 by default from `Brain_Init` `0x0028a570`) are taken when an
  attacker targets him (`Brain_ClaimAttackSlot` `0x0028df30`, from `Brain_SetTarget`). A full list gives the slot of
  the farthest holder to a nearer newcomer, or to one forced in, and clears the holder's target.
- **Active-attacker places** (T brain `+0x1f0`, four handles) are taken by `Brain_ClaimActiveAttacker`
  (`0x00291008`) only when an attack is about to be queued. Only the first `s` places are used:
    - `s` is T's **spacing byte** `+0x14a`;
    - while T is down, out of the fight or arrested (state any of `0xe0000`), `s` is `+0x14b`.
- **The spacing bytes** start at 1 (`0x0028a570`). Each slot claim raises them:
    - `+0x14a` to at least the attacker's gang's `CfgGang` value 2 (gang record byte `+0x6c`);
    - `+0x14b` to at least its value 3 (`+0x6d`);
    - a Warrior target (type 3) keeps `+0x14a` at 1.

  Both return to 1 when T's slot list empties (`Brain_SetTarget` `0x0028cfe0`).

  So `CfgGang`'s second and third values are **how many of that gang may swing at one standing (or one downed) man
  at once**.
- **A claim succeeds** in three cases:
    - A already has a place;
    - a free place exists and either T's `+0x1ec` has passed or someone already holds a place;
    - A is T's own target and `+0x1ec` has passed, in which case A displaces the first holder.

  A holder is released when his fight goal next waits (`+0x28`, `Brain_ReleaseActiveAttacker` `0x00291178`), when he
  blocks, and when the fight goal is suspended.
- **When T may be attacked again** (`+0x1ec`, `Brain_SetAttackableTime` `0x00290e78`): the attack action's Start, when
  neither human is busy, adds `t` to max(`+0x1ec`, now).
    - `t` = `Attack_GetNextAttackDelay(A, kind)` (`0x00231590`) × 1.0 (`0x00510ad0`) / `s`, rounded. When `s` is 0,
      `t` is used unscaled.
    - `Attack_GetNextAttackDelay` is the **playing time** in ms of the kind's anim on A:
        - 11-21 for kinds 0-9 (kind 0: 11, or 194 against a downed target);
        - 25 for 10, 21 for 11, 193 / 194 for 12 / 13, 237 for 14, 664 for 15;
        - the longer of 653 / 655 for 16, and of 657 / 659 for 17;
        - 0 / 1 for 19 / 20;
        - 3 + 5 for 21, 70 + 72 for 22, 466 + 467 for 23;
        - the throw clips for 25-29; 118 for 30; the grab-escape clips for 31-34; 225 for 35-39, 248 for 40, 252 for
          41, 246 for 43, 242 for 44;
        - the object set's clips when A holds a bat or bottle set (sets 2, 3), with kinds 0, 1 and 5;
        - 100 ms for anything else, and for kinds 0 and 1 against a downed target.

  So the gap a target gets between attacks is one swing's length shared among the `s` attackers allowed.

#### The reposition {#fight-reposition}

Confirmed (code) at the cited addresses unless a line says otherwise.

When `Brain_CheckAttack` says wait, the fight goal calls `FightGoal_Reposition(d², goal, T, reason)` (`0x002b2fc8`). It
queues a fidget now and then, and always a move-to-human action that **holds A in a ring** round T. Confirmed (code).

1. **A is close-in** when all of these hold:
    - T is empty-handed, or both are armed and A is one of fewer than two slot holders;
    - the reason is not 7, 8 or 3;
    - T's own target is A, or T has no target and A is first in T's slot list.
2. **A tactic ring** applies when A's gang has a tactic of type 2 (Defend) or `0xd`.
3. **The ring's radii:**
    - inner = 2 × T's capsule radius when close-in or in a tactic ring, else (near + far) / 2 = **4 m**;
    - outer = 0.95 × far = **4.75 m**, and never less than inner + 0.1 m;
    - for reason 5 (the police have him), a non-cop uses inner = 3 × far = 15 m and outer = 16 m;
    - the limit is 2000 ms, or 6000 ms for reason 5.
4. **A taunt**, once per random 1000-2000 ms (`+0x20`). All of these must hold:
    - T is at least 2.75 m away (a quarter of (1.1 × far)²);
    - fewer than two humans hold slots on A;
    - A is not close-in, not in a tactic ring, and T is empty-handed;
    - gestures are allowed.

   For reason 7 it happens only 11 % of the time. It is a fidget action on anim `0x25b` with speech line `0x11`
   (`cheer2`). A Warrior uses line `0xc3` (`turf_defend`) instead 31 % of the time, when the game flag `0x56e1` is
   set (`CfgEnableTurfInvasion`, [Game-state functions](ai/index.md#warriors-functions)).

   **The timer and the order** (confirmed (code), `0x002b2fc8` and `FightGoal_Init` `0x002b2c20`):
   the fight goal's Init sets `+0x20` to now + a random 750-1000 ms. Each reposition call with now past `+0x20`
   first re-arms it (now + 1000-2000 ms, `Random_IntRange` on stream `0x006eb880`), whether or not the taunt then
   happens; the tests run in the order above, `Ambient_MayGesture` (`0x00291ed0`, which takes a gesture slot) after
   the others and the 11 % roll after it. The fidget action is queued **before** the move-to-human action, so the
   waiter plays it standing and moves on once it ends.

   **Which clip** (confirmed (code)): the fidget plays anim `0x25b` (`Human_PlayHeldIdle` `0x00231ac8`), whose clip
   the lookup (`Human_GetDynamicAnim` `0x00221a00`) takes, with no override, from the gang's group `0x25b` at a
   random variant: `GangClips_Get` (`0x00163fc0`) draws `Random_Int(8)`, 0 to 8 inclusive, and returns nothing for
   8 or for an empty slot. The fighting gang fills slots 0-3 with the four `combat_fidget_*` clips
   ([alert state](ai-reimpl.md#alert-state)), so each of them plays 1 time in 9 and the character's own clip for the
   id 5 times in 9; a calm gang's group is empty and the character's own clip always plays. How often the
   sparring Warriors taunt, in the original and in Coney: [How this was checked](#runtime-taunts).

5. **The move-to-human action** (inner, outer, the limit above).

So waiting attackers circle in a band of 4-4.75 m (5 m far range), and the one T is fighting stays close. The
"shuffle" is this band-keeping move, not a dedicated sidestep.

#### The tackle try {#try-tackle}

Confirmed (code) at the cited addresses unless a line says otherwise.

`FightGoal_TryTackle(goal, T)` (`0x002b2e28`), step 3 of the fight goal. Confirmed (code).

- `k` = A's gang's `CfgGang` value 7 (gang record byte `+0x70`).
    - When `k` is 0, or B's tackle weight (kind 21) is 0: clear B's **tackle meter** (brain `+0x148`, s16) and do
      nothing.
    - When the meter ≤ (7 − `k`) × 6: do nothing. With `k` = 1 the threshold is 36, the meter's cap, so it never
      fires.
- The kind is 21 (the tackle). A cop (type 1) uses kind 0 (`X1`) instead 75 % of the time.
- It goes ahead only when all of these hold:
    - `Human_CanStartAttack` passes (in reach);
    - T may be attacked by A;
    - T's gang is not wanted with a cop already on him;
    - A takes an active-attacker place (not forced).

  It then queues the attack (no angle), clears the meter, notes the place, and the goal waits.
- **The tackle meter** is kept by the think handlers of cops (`0x00300908`), gang soldiers (`0x00304840`) and the
  Warriors (`0x0030543c`). These run once per 0.2 s.
    - It rises by T's gait (`+0x1a8`, capped at 36) while T moves and either holds an object (`0x00224060`) or moves
      away (T's velocity, `0x003a1f00`, has a non-negative dot product with A's facing).
    - Otherwise it falls by 2 (not below 0).

  So a target who keeps running off, or who carries an object, is tackled sooner, and a high `k` makes it faster.

#### The grab and snap try {#try-grab}

Confirmed (code) at the cited addresses unless a line says otherwise.

`FightGoal_TryGrab(d², goal, T, &angle)` (`0x002b3360`) runs once A may attack and is in reach. It returns 1 to
make the fight goal wait. Confirmed (code). Sectors are the eight 45° sectors of [the sector record](ai-moving.md#neighbour-sectors)
(sector *k* at record `+8k`, flags at `+8k + 4`). The stick angle of a sector *k* is A's heading − *k* × 45°.

- The angle starts at −1.
- **Kind 10 (the snap)**: the first of A's sectors 4, 5, 3, 6, 2 whose near human (flag bit 1) is in A's own attack-slot
  list (B `+0x1a4`: he is attacking A) gives the stick angle: heading + π for 3-5, heading + π/2 for 6, heading − π/2
  for 2. The kind becomes 1 (square), so the press is square with the stick to that side or back. This is how the
  player's snap is made ([Combat](combat/index.md)).
- **Kind 22 (the grab)**: when A is in T's sector 2 or 6 (at his side) and T is grabbed from the rear, A moves in
  (move-to-human, 2 × T's radius to the grab reach, 1500 ms) and returns 1. At his side otherwise, the grab becomes
  kind 0 (`X1`).
- **Kinds 12, 13 (strikes on a downed man)**: when T is tackled, or has state `0x2000` or `0x10000000000`, and a
  friend of A holds him (T `+0xc4`), A moves in to 0.6 × near to 0.6 × near + 1 m (1500 ms) and returns 1.
- **Other kinds: a rear grab.** All of these must hold:
    - A is empty-handed;
    - A's gang's `CfgGang` value 8 (`+0x71`) `g` is above 0, and B's grab weight is above 0;
    - A is the near human in T's sector 4 (behind him);
    - `Random_Int(100)` < `g` × 25;
    - the grab can start.

  The kind then becomes 22.
- **Then, for all kinds:** when T is grabbed from the rear, the kind is not 22, and the grab is usable
  (`Human_CanUseAttackKind`), the kind becomes 22 and the try returns 1. "Usable" needs one of these:
    - B's grab weight is above 0;
    - B has goal `0x88`;
    - the game word `+0x158` is set.

  A man held from behind by a mate gets grabbed in front.
- **A heavy object in hand** (`0x00224080`):
    - Take the nearest gang mate who is not T and not busy.
    - Kinds 1, 3 and 5-9 with that mate within near (3 m): the kind becomes 0 and the try returns 1.
    - Kinds 0, 2 and 4: the try returns 0 when any of these hold:
        - the mate is tackled, or has state `0x10000000` or `0x10000000000`;
        - T is beyond far;
        - the mate is both farther than T and farther than 1.6 m.
    - Otherwise:
        - against a rear-grabbed T, the kind is picked again with `Human_CanStartAttack`;
        - against T grabbing from the rear or mugging, the try returns 0;
        - else A steps in to the kind's reach (1500 ms).

      The try then returns 1. (Inferred from the branches: a swing with a heavy object is shortened or held back when a
      mate stands close.)

The fight goal's own time limit (goal `+0x08`) is always −1 (`FightGoal_Init`); its duration goes only to `+0x24`.

### Closing on the target {#fight-approach}

Below the fight goal sit the Melee goal (8) and the FindEnemy goal (`0x41`) that `Brain_PushFightGoal` pushed
([Starting a fight](#targets)). When the fight goal ends, most often at once because the target is beyond 1.1 × the
far range, Melee runs; it sends the fighter in with an **EngageEnemy** goal (`0xb`) and pushes a fresh fight goal
once he is in range. Confirmed (code) at the addresses cited; the sparring fight shows the sequence
([How this was checked](#level99-fight)).

#### Melee ranges {#melee-range}

Confirmed (code) at the cited addresses unless a line says otherwise.

Every brain starts with near `+0x13c` and far `+0x140` from two globals (`0x0028a650`, `0x0028a664`, reading
`0x00510ab4` / `0x00510ab8`), which [`CfgSetMeleeRange`](../references/bindings/config.md#cfgsetmeleerange) sets
from `config_preload2.lua` to **3 and 5 m** (1 and 4 in the executable's image, before the config runs). Nothing per
class or power class changes them: the only other writers are `BrSetMeleeRange` (no script calls it) and
`BossDiegoGoal_Start` (`0x002a1b50`, 1 and 2 m). So the far range is **5 m** for the sparring Warriors (power class 40)
and every other AI. Confirmed (code) at the writes above; the values read at run time agree
([How this was checked](#runtime-melee-range)).

#### The Melee goal {#melee-goal}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Goal_Melee` builds it with `0x002ade10(goal, brain, duration, 4000)`: `+0x08` the time limit (the duration),
`+0x10` the target's handle (−1 at first), `+0x14` 1.1 × far, `+0x18` **4000**, the duration of every fight goal it
pushes, `+0x1c` a step counter. End (`0x002ade60`) sets brain `+0x2d3` and calls `0x00226f70`. Brain `+0x2d3` ("may
approach", 1 when the brain is made) is cleared below when the target cannot be chased (`0x0028abc0`: the target
brain's [reachable](../references/bindings/character.md#humarkreachable) byte `+0x11e` clear, a train near it, or on
fire), when the last move failed (`+0x284`), or when the target is outside the gang's turf (`0x0028ff58`).

**Process** (`0x002aebf8`), when the goal's time limit has not passed:

1. While actions are queued, wait.
2. **Choose the target.** With threat response ≠ 0, the best-scoring valid enemy in the enemy list `+0x164`
   (`Brain_PickBestEnemy` `0x0029f230`, [the score](#enemy-score)), set as the target. With threat response 0, only
   the current target, kept while valid and holding an attack slot on it (else the target is cleared and the goal is done).
3. **No target**: done when the enemy list is empty, or when its nearest enemy is hidden in shadow (`+0x2d4`) or
   fails `0x002225d8`; otherwise push a [Spectate goal](#spectate) (`0x10`) and wait.
4. **With a target** (`0x002ae2e8`), with `d` the distance and `R` = 1.1 × far: a new target resets the counter and
   sets `+0x2d3`.
    - **Armed** (holding a weapon, `0x00223ea0` or `0x00224000`), or **unarmed, `+0x2d3` set and `d` ≤ `R`**: when
      the line of sight (`0x00222288`, eye heights 1.7 m) holds, either walk to him in the fight stance (a move
      action, gait 2, radius about 1.0 m) when `d` > 1 m and the straight line to him is not walkable
      (`0x002221b0`), or **push a fight goal with duration 4000** and run again (returns 1); without the line, a
      move-to-human action (2000 ms), an EngageEnemy goal, or a 100 ms wait.
    - **Unarmed, `+0x2d3` set, `d` > `R`**: **push an EngageEnemy goal** on the target when `0x00290588` allows it
      (below), else a move-to-human action to 2 × far (4000 ms) and a 100 ms wait.
    - Without `+0x2d3` (the target cannot be chased): pick up a weapon nearby (`0x002ade90`, brain `+0x265`), a
      throw of a held object (kind `0x17`) at a target within 4 × `R`, at most one in the game every 6 s, or a
      positioning move (`0x002ae028`, four modes by distance and a draw); not traced further.

`0x00290588(brain, target)` allows the chase unless the brain is not type 1, the target's own target is someone
else, and the gang's wanted timer (gang `+0x5e8`) is set and `0x0028de98` refuses; a type-1 brain (police) also
checks `0x00290138`. With no wanted timer it allows it.

#### The FindEnemy goal {#find-enemy}

Confirmed (code) at the cited addresses unless a line says otherwise.

Built by `0x002c0430` with 90.0, 30.0, the byte at `0x00510adb` and the duration (`+0x1c`); its time limit is its
own `+0x20` = the push time + duration (none for −1). **Process** (`0x002c0748`): past `+0x20`, done. Target still
valid → **`Brain_Fight(brain, target, duration, 0)`**, which pops Melee and FindEnemy and pushes the three goals
again. No valid target → fight stance off (`0x0022fed0`) and done, except for a brain with `+0x28d` set and
`+0x264` ≥ 0 in a gang whose `+0x18` is 1, which may push a Chase goal (12, `0x002b04f0`) on a new enemy. So
FindEnemy runs only when Melee has ended and restarts the fight while the target lasts.

#### EngageEnemy: the run-in {#engage-enemy}

Confirmed (code) at the cited addresses unless a line says otherwise.

`EngageEnemyGoal_Init` (`0x002af5b0`): `+0x10` the target's handle, `+0x20` the attack kind (45 = none chosen),
`+0x2c` a taunt (0), `+0x30` **2.56** (a squared distance: 1.6 m), `+0x34` "start slowly" (the human's gait
`+0x1a8` below 3 and not class 13), `+0x36` 1. **Start** (`0x002af670`): `+0x35` (**charge armed**) is set when the
target is at least far (5 m) away; a type-2 brain may taunt (anim `0x47`); next shout `+0x18` = now + 4000, next
re-target `+0x1c` = now + 2000; raises brain `+0x0b` by one (restored by End, `0x002af8d0`, which also clears the
actions). It has no time limit. **Process** (`0x002afa48`), each update:

1. Actions blocked → done; `0x00228428` → wait. The fight stance is dropped (it runs, not shuffles).
2. **Stopping** (`+0x39`, set by the stop below): wait until the human's `+0x1a8` is 0, turn to the target
   (`0x00221cd8`, 0.3) and end (2).
3. Target no longer valid, hidden in shadow, or `0x00290588` refusing → stop.
4. Every 2 s (not class 13): the nearest enemy replaces the target when it differs, the target is farther than far
   and the new one is seen (`Human_CanSeeHuman`, near 3 m, in shadow 2 m).
5. The target brain's `+0x1ec` is cleared (he may be attacked at once).
6. **Re-plan** when the target's heading turned more than 45°, he is within 1.6 m, he slowed from a run, or 250 ms
   have passed; a shout every 4-4.5 s (`0x002af928`). Otherwise wait while the move action runs.
7. The last move failed (`+0x284`) → stop.
8. **Gait 4 (run)**; 5 (sprint) when the target runs (gait > 3) and the fighter's stamina is above 50 %
   (`Human_GetStaminaPercent` `0x00222fa0`, [Sight](#sight)).
9. **Stop** (when the target is in sight, `0x00222288`) if he cannot be attacked by this human now
   (`Brain_IsAttackableBy`, `0x00290ea8`), at any distance; and, within 0.75 × far (**3.75 m**), if his actions are
   blocked (he is busy, as when another Warrior is hitting him) or the charge is not armed and he walks or stands
   (when he runs, it arms the charge instead).
10. **Give up** (done) when the target is out of sight, at least 10 m away, and either 20 m away or brain `+0x28d`
    set, and his gang's `+0xdc` is 0.
11. **Charge**: within 1.6 m with the charge armed or the target running, [below](#engage-charge).
12. **Move**: aim at the target's position led by his facing × his speed (`+0x1ac`), turned 9° per attack slot
    index, alternating sides, when he faces away (sector 3-5 of `0x0029eb38`; inferred from the alternating turn: to fan
    the attackers out);
    aim straight at him when no lead applies or the led point is not reachable in a walkable line (`0x002221e0`).
    The running move action gets the new point and gait; else a new [move action](ai-moving.md#move-action) (radius 0.5
    m, a random 0-500 ms start delay the first time when "start slowly").

When EngageEnemy ends, Melee is on top again and, now within `R`, pushes the fight goal.

#### The charge in EngageEnemy {#engage-charge}

Confirmed (code) at the cited addresses unless a line says otherwise.

Step 11 of `EngageEnemyGoal_Process` (`0x002afa48`) runs within the run-in distance (`+0x30`, 2.56 → 1.6 m) and when
T runs (gait > 2) or the charge is armed (`+0x35`). Confirmed (code).

1. **A taken sector.** Take the sector of T's record that A approaches from (`Human_GetSectorOf`
   `0x0029eb38` of A's bearing relative to T's heading). When another human is the near one there, the charge is
   disarmed and A only moves (step 12). When A himself is that human, A waits this update.
2. **The kind** (once; `+0x20`, 45 = none):
    - A **cop** (type 1) takes kind 0 (`X1`) with 50 % (80 % against a gang member, class `+0x11b` 10), else 21 (the
      tackle). It is forced to 0 when T's `+0xe0` has bit `0x40`.
    - **Anyone else** uses `Brain_PickAttack(B, T, Human_CanUseAttackKind)`. When that is not a charge kind
      (`AttackKind_IsCharge` `0x00229b60`: 0, 19, 20, 21), the engage goal **stops** (step 2 of the goal: halt, turn
      to T, end). Melee then pushes a fight goal.
3. **The start test.** When `Human_CanStartAttack(A, T, kind)` (`0x00224778`) passes:
    1. Clear the actions; when an action refuses, wait.
    2. Queue one attack action directly (not `Brain_QueueAttack`), no delay. Kind 0 carries A's heading as the
       stick angle.
    3. Restore the move gait, disarm, and clear the kind.

   When it fails, A keeps running in (step 12).

So a running AI's charge is the run-in `X1`, the L2 charge or dive (19, 20 when its table has weight there) or the
tackle. Anything else ends the run-in and becomes a fight goal.

#### The durations {#fight-durations}

Confirmed (code) at the cited addresses unless a line says otherwise.

The duration given to `Brain_Fight` (2000 or 8000 by the AI's own fights, −1 by `GoalFight`) reaches three goals:

- the first **fight goal**'s deadline `+0x24` (−1: already past, [The fight goal](#fight) step 5);
- **Melee**'s time limit: it counts from when Melee first runs (after that fight goal ends), and once passed Melee
  ends the next time it is on top, without running. −1: **no limit**;
- **FindEnemy**'s `+0x20`, counted from the push. −1: **no limit**, so it re-starts the fight for as long as the
  target is valid.

Every **later** fight goal, pushed by Melee, gets **4000** whatever the duration was, so under `GoalFight` the
second and later fight goals have a 4 s deadline. The engage goal has no time limit. Confirmed (code) at
`0x0029ed58`, `0x0029eed8`, `0x002ade10`, `0x002ae2e8` and `0x002c0748`.

#### Choosing among enemies: the score {#enemy-score}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Brain_PickBestEnemy(goal, flags)` (`0x0029f230`), called by Melee (flags `0xfffffffd`, every term but `0x2`) and by
23 other goals with their own masks: the current target is taken as "the previous one", the target is cleared, and
the 16 slots of the enemy list (brain `+0x164`) are walked. A slot whose human fails `Brain_ValidateEnemy` is
skipped; every other gets a score (`Brain_ScoreEnemy`, below) and then the goal's own adjustment (goal vtable `+0x54`;
every goal seen uses the default `Goal_AdjustEnemyScoreDefault` `0x0029f3a8`, which adds **3** when the candidate is
the previous target). The best score wins only when it is **above 0** and above every earlier one (ties keep the
first); after every 5 slots the walk stops early if a winner exists. The winner (or none) becomes the target
(`Brain_SetTarget`). Confirmed (code).

**`Brain_ValidateEnemy(brain, human)`** (`0x0028d358`) returns the human when he may be chosen: not dead or out of
the fight (state flags `0x100000000`, `0x80000000`, `0x40000`), not being mugged (`0x200`), his brain's targetable
byte `+0x120` set; when he is under arrest (`0x20000`, `0x00223b70`) only for a scorer of brain type 0 or 3; a human
with an interrogation set (`+0x5a0`) who is grabbed from the rear or at 50 % health or less only while his `+0x360`
is empty; and for a scorer whose `+0x14d` is set and whose gang has a turf (gang `+0x17c`), only inside it
(`0x0028ff58`). Confirmed (code).

**`Brain_ScoreEnemy(scorer, enemy, flags)`** (`0x0029ce98`) returns −999 for none, an enemy near a train this update
(`Brain_IsNearTrain` `0x0028ab30`: brain `+0x2e0` equals the game time) or one the scorer may not take
(`Brain_CanTakeSlotOn` `0x0028dc80`: the enemy's live attack slots (`+0x1a4`) equal his maximum (`+0x1e4`), and
either the scorer holds one of them or no holder is farther from the enemy than the scorer, by squared 3D distances).
Otherwise the sum of these terms, `w` being the weights that `CfgSetTargetingPoints` / `CfgSetTargetingPointsEx` set
(values from `config_preload2.lua`; the executable's defaults in brackets where they differ):

| Term (flag) | When | Points |
| --- | --- | --- |
| base | the scorer cannot chase him (`Brain_CanBeChased` `0x0028abc0` false on the enemy, or scorer `+0x2d3` clear, or the enemy outside the scorer gang's turf) | −5 (`0x00510b94`) |
| `0x1` distance | always with this flag: beyond the scorer's sight range `R` (brain `+0x130`; at least 30 m while his gang is fighting, alert state `+0x32` 1, `0x0028bf00`) the score is **−999** | + 4.0 × (`R` − distance) (`0x00510b4c`) [3.0] |
| `0x2` near the leader | the scorer gang's leader (`Gang_GetLeader` `0x00165678`) is not the enemy and is within `R` of him | + 3.0 × (`R` − that distance) (`0x00510b50`) |
| `0x4` enemy leads his gang | the enemy is his gang's leader and the scorer's brain is not type 3 | + 6 (`0x00510b54`) [3] |
| `0x8` stunned | `0x100000` set and `0x80000` clear | + 1 (`0x00510b58`) |
| `0x10` out of view | outside the scorer's field of view (`Human_IsInFieldOfView` `0x00222710` with brain `+0x12c`) | −5 (`0x00510b5c`) |
| `0x20` down | knocked down (`0x80000`) | + 1 (`0x00510b60`) |
| `0x40` grabbed | grabbed (`0x30`) | + 1 (`0x00510b64`) |
| `0x80` grabbed from the rear | `0x20` | + 6 (`0x00510b68`) |
| `0x200` targets me | the enemy's target (brain `+0x124`) is the scorer | + 4 (`0x00510b6c`) [1] |
| `0x400` running | gait 4 with record `+0x08` clear | + 3 (`0x00510b70`) |
| `0x800` train | `Brain_IsNearTrain` × 6 (`0x00510b74`); always 0 here, since such an enemy already returned −999 | 0 |
| `0x1000` tagging | `0x2000000` | + 3 (`0x00510b78`) |
| `0x2000` targets me armed | as `0x200`, and he holds an object (`Human_IsHoldingObject` `0x00224060`: the held object's kind `+0x86`, `0x00231a38`, not 0, whatever its anim set) | + 3 (`0x00510b7c`) |
| `0x4000` a player | the enemy has a pad (`+0x1b0` ≠ −1) | + 20 (`0x00510b80`) [3] |
| `0x8000` arrested | `0x20000` | −10 (`0x00510b98`) |
| `0x10000` police | the enemy's brain is type 1 | + 30 (`0x00510ba0`) |
| always | no walkable straight line from the scorer to him (`0x002221b0`) | −5 (`0x00510b90`) [−5] |
| always | he may not be attacked now (`Brain_IsAttackableBy(enemy brain, none)` false, [Sight](#sight)) | −10 (`0x00510b9c`) |
| always | he is the scorer gang's chosen target (gang `+0x10`) | + 400 (`0x00510ba4`) |

The last four, and `0x8000` and `0x10000`, have no script setter; the sixth argument of `CfgSetTargetingPoints` is
read and dropped, and its ninth (`0x00510b84`, 3) is the previous-target bonus above. Since a winner needs a score
above 0, an enemy beyond the sight range is never picked, and a near one that is out of view, unreachable in a
straight line and not attackable may not be either. Confirmed (code) at the addresses cited. Disc check: the values
are those `config_preload2.lua` passes.

#### Sight and "may be attacked now" {#sight}

Confirmed (code) at the cited addresses unless a line says otherwise.

- **`Human_HasLineOfSight(a, b, hit, exclude)`** (`0x00222288`): a ray from a's position + 1.7 m (z) to b's position +
  1.7 m (`Ray_IsClear` `0x0024df40` → `WorldManager_RayCast`, [Collision](collision.md#ray-cast), mask 0); if it hits,
  a second ray to b's position + 1.0 m. True when either reaches b. Without an exclusion list the rays pass through
  materials 30 `LOW_FENCE`, 2 `GLASS`, 187 `STOREDOOR_GLASS`, 122 `RAILING`, 107 `CHAINLINK_NOCLIMB` and 1 `NONE`
  (the list built in `Ray_IsClear`), so an AI **sees through** fences, railings and glass. Its distance is not limited;
  callers test range themselves. Through `hit` it returns a word of the cast; Melee reads it as the material crossed
  and, when it is one of 30, 2, 187, 122, 107, walks straight at the target (gait 4, out of the fight stance) instead of
  fighting (inferred: to get round the fence or glass between them). Confirmed (code) for the rays; the meaning of
  `hit` inferred from Melee's use of it.
- **`Human_IsInFieldOfView(fov, human, point)`** (`0x00222710`): true when the angle between the human's facing and the
  direction to the point is within fov / 2 (dot product against `cos(fov × 0.5)`). Confirmed (code).
- **`Human_CanSeeHuman(fov, range, near², shadow², looker, target)`** (`0x002223e8`): false beyond `range`; a target
  hidden in shadow (his brain `+0x2d4`) only when the squared distance is below `shadow²`; within `near²` (squared
  distance) the field of view is skipped, else `Human_IsInFieldOfView(fov)` must pass; then the line of sight decides.
  EngageEnemy's re-target passes 9 and 4 (3 m and 2 m). Confirmed (code).
- **`Brain_IsAttackableBy(target brain, attacker)`** (`0x00290ea8`): normally the target brain's byte `+0x11f`
  ("attackable": 1 when the brain is made, `0x0028a570`; set by `GangSetAttackable` through `Gang_SetAttackable`
  `0x0016bb58` and by the end of a scripted goal, `0x002d4248`; cleared by `GoalGrabTarget` while it holds,
  `0x002bb560`). Two exceptions: a civilian (type 4) in a gang of kind 23 attacked by a Warrior (type 3) whose own
  target is not that Warrior, and who has no target or an AI one, may be attacked only when he is the Warrior gang's
  chosen target (gang `+0x10`); and a human of class 221 with `+0x11f` clear is still attackable by an attacker that
  has no goal `0x1f`. Confirmed (code).
- **Sprint check** (EngageEnemy step 8): `Human_GetStaminaPercent` (`0x00222fa0`) = stamina (record `+0x14a`) × 100 /
  `Human_StaminaMax` (`0x00223188`, [Sprint](characters.md#sprint)); above 50 the AI sprints after a running target.
  AI stamina drains and refills by the player's rules (the drain is keyed on gait 5, not on the pad). Confirmed (code).

#### The enemy scan {#enemy-scan}

How an AI finds enemies on its own: the enemy list (brain `+0x164`, 16 slots) is rebuilt by a periodic scan, and the
fight and Melee goals pick from it ([the score](#enemy-score)). Confirmed (code) at the addresses cited.

**When** (`Brain_MaybeScanEnemies` `0x0028fa18`, from `Brain_UpdateGoals` each update): when the gang changed since
the last scan (gang `+0x34` ≥ brain `+0x160`), or when `+0x160` + interval has passed. The interval is brain `+0x144`
(2000 ms, cops 1000 ms) × 4 in the fight stance or when the human's detail level `+0x333` is above 0 (× 1 otherwise),
so a calm AI near the camera scans every 2 s and a fighting one every 8 s. A scan also needs a token
(`AI_TakeScanToken` `0x00293cd0`): at most 5 scans run in one update across all brains; a refused brain tries again
on the next update.

**The detail level** human `+0x333` (`Human_UpdateLod` `0x0023d660`, each move): from the distance `d` to the nearest
camera, plus 0.01 m per ms since the time at the human's draw record `+0xd8` `+0x2c` (inferred from the field's writer:
time since last drawn):
0 below 30 m, 1 below 60 m, 2 below 100 m, 3 below 115 m, else 4 (thresholds `0x005102f0`; the switch `0x005102d4`
is 1, 0 would force level 0). The AI uses it to do less far away: [steering](ai-moving.md#steering) is off above 2, the
[corner speed](ai-moving.md#move-action) and the [riot](ai-scripting.md#riot) scale with it.

**The scan** (`Brain_ScanEnemies(brain, filter)` `0x0028b358`). A human who is down or dead empties his list. Else, with
`R` the sight range (`Brain_GetSightRange` `0x0028bf00`: `+0x130`, at least 30 m while his gang is fighting, gang
`+0x32` 1, whatever the gang's kind; confirmed (runtime), run r-20261028-sight-range-alert: a range of 2 m gave 30 m
with the alert at 1 in a gang of kind `0x17`, and a score of −999 with it at 0, for kind `0x17` and for
kind 1) and `c` = cos(field of view / 2):

1. For each of the 32 gangs that is active (gang `+0x18`) and an enemy of his (`Gang_IsEnemyOf` `0x00168fe0`: the
   enemy bit of the other's id in gang `+0x38`; never between kinds 1 and 23, nor during a global truce), each of its
   16 members within `R`:
    1. A member hidden in shadow (his brain `+0x2d4`) is skipped beyond 2 m, and within 2 m unless the scanner's
       `+0x2d5` is set.
    2. Unless the member's gang is always seen (gang `+0xdc`): `Human_MaySpectate` (`0x002225d8`) must allow him:
       always for a Warrior (brain type 3) and for a brain that may not start a fight (`Brain_MayStartFight`
       `0x002904e0`: a Chase goal `0x0c` on his stack, his top goal `0x8a`, his gang fighting (gang `+0x32`, the
       alert state `Gang_SetAlertState` `0x00164a28` sets, is 1) or a fight goal (`Brain_HasFightGoal` `0x00290348`:
       Melee `0x08` or `0x3f`)), unless his class `+0x11b` is 10; otherwise, with fewer than 3 investigations (brain
       `+0x2d0`, which `InvestigateGoal_Start` counts) and not a pad player (human `+0x1b0` is −1), he lists only a
       member less than 1.9 m times the vertical sight modifier (`0x00510ad4`, `CfgVerticalSightModifier`) above him.
       Confirmed (code) at the three addresses. So a man already in a fight sees his enemies at any height: in
       `level3`'s rooftop chase the Hi-Hat pursuers (class roles 11 and 12, a Melee goal under their devil run) list
       the Warriors on the roofs above them. Inferred from Coney's run of the chase (the disc's classes and the scan
       ported as above).
    3. The filter `+0x15c` must accept him: `Brain_IsValidEnemyOf` (`0x0029bf60`, `Brain_ValidateEnemy` on the
       scanner's brain) for every type but the Warriors, whose filter is `Filter_IsThreatTo` (`0x0029bf98`): a cop
       only within 10 m and when `Brain_IsThreat` (`0x00290138`, below) calls him a threat; a civilian (type
       4) only of ped type 3 with a threat response and not grabbed from the rear; any other only when he is such a
       threat; each then `Brain_ValidateEnemy`.
    4. Unless his gang is always seen, sight: within the near radius (3 m; for a cop scanning a running member 5.2 m,
       for a member walking or standing 1.5 m) only the line of sight counts, unless the member is a player sneaking
       in shadow (`Human_IsHiddenFromBrain` `0x00223c50`); beyond it, or when sneaking, he must also be in front
       (direction · facing > `c`). Then `Human_HasLineOfSight` must pass.
2. The scanner gang's chosen target (gang `+0x10`) is added when it passes `Brain_ValidateEnemy`.
3. Old entries not found again are kept while within `R`, still valid, and within 3 m or in line of sight; others are
   dropped. Each old entry's hostile count (his brain `+0x152`) is lowered first.
4. More than 16: sorted by distance, the 16 nearest kept. Each human new to the list is sent to the scanner's own
   event handler as event `0xb` (a new enemy; a tactic hears of it through `Brain_AddEnemy`); every listed human's
   brain `+0x152` is raised by one (`Brain_CountHostile` `0x0028ef20`), and the radar marks the list
   (`HUD_RadarMarkEnemies`).

**`Brain_IsThreat(brain, other)`** (`0x00290138`; also `Human_IsThreatTo` `0x00222a48`, which the type
event handlers call): never himself; a Warrior whose `+0x2e5` is set (hitting back at his chief) and the chief are
threats to each other; otherwise a threat when the gangs are enemies or both are players.

#### The Spectate goal {#spectate}

Confirmed (code) at the cited addresses unless a line says otherwise.

Type `0x10` (`SpectateGoal_Init` `0x002b4098`, vtable `0x00540270`), arguments (keep distance, goal, brain, *join*,
*may engage*, shortest and longest pause (ms), time limit, *taunt*). Fields: `+0x10` the watched human's handle,
`+0x14` the next look, `+0x18` the next re-pick (now + 3 s), `+0x1c` / `+0x20` the pause range, `+0x24` the keep
distance, `+0x28` *join*, `+0x29` *may engage*, `+0x2a` *taunt*, `+0x2b` may pick up a weapon (rolled at Start),
`+0x2c` chase when far. Confirmed (code).

- **Start** (`0x002b4200`): picks whom to watch (`SpectateGoal_PickTarget` `0x002b4158`): with *join*, the nearest
  human of his own enemy list; otherwise the nearest member of the nearest other gang (`0x0016c808`). `+0x2b` =
  `Random_Int(100)` < 25 × (a per-gang-kind number, `0x001644f8`); `+0x2c` = 50 % chance, when no goal `0x98` is on
  the stack. **End** (`0x002b42f0`): target cleared, actions cleared, brain `+0x284` = 0.
- **Process** (`0x002b4330`), each update: the target is cleared; a failed last move clears `+0x2c`. With *join*:
  fight stance; for a brain of type other than 1 and 4 that may pick up a weapon and is empty-handed, every 30
  updates the nearest pickable weapon within 15 m (`0x0029d5f0`) is fetched (`Goal_GetItem`). Every 3 s the watched
  human is picked again. Then:
    1. No valid watched human: done with *join*, else wait (until the time limit).
    2. With *join* he is made an enemy (`0x0028ff78`); tackling → an attack action of kind `0x24`.
    3. Unless the watched human's gang has `+0xdc` set, out of sight → done; in shadow (`+0x2d4`) and beyond 3 m → done.
    4. With *join* and *may engage*, not after a failed move, when `0x00290588` allows a chase: an **EngageEnemy**
       goal (taunt `0x8f`) when the target runs (one update in five) or `+0x2c` is set and he is beyond the far range.
    5. Otherwise, with his actions free: fight stance; every pause (random between the two lengths) an optional taunt
       (`0x002fb0f0`, with *taunt*); beyond 10 m a move toward him (gait 2, 4 when he runs or with *join*, radius far +
       2 m); else with *join*, between the keep distance and 2 m more, 51 %: a watch action (`0x002feaf8`, 3 s);
       else a move-to-human action that holds him between 0.95 × and 1 × the keep distance (4 s limit). The keep
       distance 0 means the far range.

Melee's Spectate (no target, [The Melee goal](#melee-goal) step 3) is `SpectateGoal_Init(k, goal, brain, join 1,
may engage 1, 1000, 3000, 2000, taunt 1)`, with *k* = 0 (the far range, 5 m) or, when goal `0x98` is on the stack, a
random distance between 0.75 × near and far: a 2 s spell watching the nearest enemy, with a taunt every 1-3 s, joining
in with EngageEnemy when he runs. The dealer's wary goal is (8 m, join 0, 0, 2000, 4000, 8000, 0): he backs off to 8 m from
the nearest fighter for up to 8 s.

### The attack action {#attack-action}

Confirmed (code) at the cited addresses unless a line says otherwise.

The attack action (vtable `0x00542ce0`, `AttackAction_Init` `0x002fa918`):

- **Start** (`0x002fa9a8`): checks the target and keeps the start time in the action's `+0x18`. It sets the brain's
  `+0x1e8` (its next attack, `0x00290e48`) to now + **the attack delay** (`Human_AttackDelay`, `0x00223800`:
  `CfgAttackDelay[kind]` × the power class's `+0x1c`, or `+0x20` when the target is down), **halved** when the
  target's own target is this human or the brain is type 3. `CfgAttackDelay` is a table of ms **indexed by attack
  kind** (`0x00228870` reads `0x006b6658 + kind × 4`); `config_preload2.lua` sets 200 for most kinds, 400 for 12 and
  13, 500 for 19 and 21, 1000 for 20, 300 for 32-34 and 0 for 23, 31 and 42. When neither human is busy, it sets the target
  brain's `+0x1ec` (when it may be attacked next, `0x00290e78`) from a **separate per-kind time** (`0x00231590`:
  the attacker's clip length for the kind, [Attack slots, active places and spacing](#attack-places)), scaled by
  `0x00510ad0` / the target brain's spacing byte `+0x14a` (or `+0x14b`, by `0x00223b48`) when that is not 0; the
  attacking gang's `CfgGang` sets those bytes ([Combat](combat-targets.md#ai-attacks)).
  Then it writes the command
  (`AttackKind_ToCommand`) into the per-player record `+0x20` (`PlayerRecord_SetCommand`, `0x00147ef0`) and, when
  the action has an angle (`+0x1c` ≥ 0), a stick of magnitude 1.0 at that angle.
- **Update** (`0x002fad70`): its first call only clears the action's `+0x20` (which `AttackAction_Init` sets to 1)
  and says running. That first call is the one `Action_Update` makes right after Start, in the same update
  ([Actions](ai/index.md#actions)), so an attack never ends in the update that pressed it, and the next action in the
  queue, which the same update would run, cannot write its command over this one before the human takes it.
  After that, it re-writes the command only while now < the action's `+0x18`, which Start set to now, so
  **never**: the command is written **once** (a run saw it too, [How this was checked](#runtime-attack-action)). It is
  done at once when the action's
  `+0x14` is 1, otherwise once the record's `+0x08` has none of **`0x5c0221f`**: the attack's held flags
  ([Tasks](tasks.md#held-flags)) decide when the AI is free again.
- **Abort** (`0x002fad30`): refused while `+0x08` has any of `0x5c0221f`.

So an AI attack is a press and a wait on the same flags the player's moves hold, and its chain timing is the
player's. What a run showed of the commands, the chains and the attack delay: [How this was
checked](#runtime-attack-action).

### Blocking and countering {#block}

Confirmed (code) at the cited addresses unless a line says otherwise.

- **The warning.** `+0x200` is a **count** of attack warnings since the last update: the default event handler
  (`0x00292d80`) adds one for each event `0x10`, as do `0x002ff898`, `0x00302ff0` and `0x00304070`, and
  `Brain_Update` clears it (`0x0028f914`). Event `0x10` comes from `0x0021d5c0`, called by `Attack_Start` (call at
  `0x0026267c`) and the square path (`0x00287594`), each time to the attack's target alone ([Combat: attack
  warnings](combat/index.md#attack-warnings)), which it reaches only if `0x002223e8` passes: within the
  brain's range `+0x130` and its **field of view** `+0x12c` (half-angle, radians), or 2 m when the attacker's brain
  `+0x2d4` is set, and then a line of sight (`0x00222288`). Confirmed (code); runs show the counting
  ([How this was checked](#runtime-block)).
- **When** (`Goal_TryBlock`, `0x0029f098`): only when the brain's `+0x200` is not 0, that is when an attack on the
  human was announced this update. The chance is `Human_BlockChance` (`0x00223628`: the power class's `+0x08`, or
  `+0x0c` while hurt, × the global `0x00510ac8`), **a quarter of it for brain type 3**. A player attacker whose
  pattern meets the class's `+0x37` threshold (`0x002236c8`, `0x00418398` on human `+0x5d0`) is always blocked. Yes
  pushes the block goal.
- **An AI human cannot block.** Confirmed (code) at `0x0027c120` and `0x00147f98`. The dispatcher starts a block
  only when R1 (mask 8) is **held in a pad record** ([Combat](combat/index.md#dispatch)): `0x00147f98(record, mask)` reads
  pad record `0x005dd810 + pad × 0x50` with the pad index per-player `+0x19`, and returns 0 when that is −1, as it is
  for every AI human. Command 4 (R1 held) is looked at only inside the block branch, once a block runs; it cannot start
  one. So the block goal's command 4 does nothing for an AI: it never takes state `0x8000` or plays 606, and its hits
  land. What the goal does give an AI is the **no-reaction flag** and the **counter** below.
- **The block goal** (`0x1b`). Confirmed (code) at `0x002b54d8`, `0x002b5520` and `0x002b5808`.
    - **Init** (`0x002b54d8`): `+0x16` = 1 (active); `+0x10` (the end time), `+0x14`, `+0x15` and `+0x17` (an update
      count) = 0.
    - **Start** (`0x002b5520`): `+0x10` = now + a random 1000-3000 ms; `+0x14` = (rand100 < `Human_BlockChance`), which
      allows the counter after a duck ([Combat](combat/index.md#block)); `+0x18` = whether human `+0xe0` already had
      `0x800`. With a target: `+0x15` = 1 when the class's pattern threshold (`0x002236c8`) is above 0 and the
      target's pattern (`0x00418398` on human `+0x5d0`) meets it, and human `+0xe0` gets **`0x800`** unless `+0x18`.
    - **Flag `0x800`** on human `+0xe0` means **no hit reaction**: `Human_ApplyPendingDamage` (`0x00265f70`) still
      takes the health but skips the reaction. Process clears it (unless `+0x18`) on the first update from its sixth on
      whose record `+0x08` has none of `0x5c7eae0`.
- **Process** (`0x002b5808`), each update, in order:
    1. With a target while `+0x16` = 1: when the target's record `+0x08` has `0x400` (`0x00228560`) or its state has
       any of `0x7bf9e9f7ff0` (`0x00228228`), write command 4 and clear `+0x16`.
    2. While the brain has actions queued: return 0 (wait).
    3. `+0x16` = 0: done (2), unless the human is still blocking or ducking (`0x00223ad0`), then wait.
    4. **The block time is over** (now > `+0x10`): when the target is still mid-attack (record `+0x08` any of
       `0x5c0221f`, `0x00228428`) and its own target is this human, `+0x10` = now + a random 1000-3000 ms and `+0x15` =
       1; otherwise `+0x16` = 0, so the goal ends on a later update. Write command 4.
    5. **The block time runs**:
        - In its **last second** (`+0x10` − now < 1000), when `+0x15` = 1 and the human is not ducking (`0x00223b28`),
          queue a **punishing attack** (`0x002b5718`): `Brain_PickAttack` with the filter `Human_CanStartAttack`
          (`0x00224778`; the value `0x00224778ffff0000` is that member-function pointer, [Picking the attack
          kind](#pick-attack)) (class `0x6a` instead takes kind `0x10` or `0x11`, class `0x59` kind `0x10`), then
          `Brain_QueueAttack` and `+0x16` = 0.
        - Otherwise, with a target and `+0x15` = 0, **roll the counter**: rand100 < `Human_CounterChance`
          (`0x002235f8`: the power class's `+0x24` × 100) **and** the counter test below passes → write **command 3**
          (and set `0x800` again unless `+0x18`).
        - Otherwise write command 4.

    So the counter is rolled on **every update while the block time runs**, from the goal's first update, and
    never once `+0x15` is set (a pattern read at Start, or a block extended in step 4).

- **Command 3 for an AI is a grab or tackle counter.** Confirmed (code) at `0x0027c120` and `0x0027d6e0`. The
  dispatcher acts on command 3 only for a human that is not pad-controlled (per-player `+0x1b` = 0), through
  `0x0027d6e0`, which refuses while the human's state has any of `0x7bf9e9f4300`, its record `+0x08` any of
  `0x100101f`, or it holds an object (`0x00231a38`). Then, against its target (`0x00226e60`):
    - the target plays **69-71** (`GRAB_MISS`, `GRAB_INTRO`, `GRAB_PLAYER_INTRO`; `0x00258e88`) → the human plays
      **76** `GRAB_FRONT_COUNTER`;
    - the target plays **2-4** (`TACKLE_MISS`, `TACKLE_INTRO`, `TACKLE_PLAYER_INTRO`; `0x002590f8`) → **9**
      `TACKLE_FRONT_COUNTER`;

    each as a paired move (`Attack_StartPaired`, `0x00262ac8`, flag `0x400000`; [Combat](combat-grabs.md#grabbing)). Both
    tests also need: the human free (record `+0x08` none of `0xfc7eaf7`, state none of `0x7bf9e9f7ff0`), not hurt
    (`0x00222ff8`), the target not knocked down (`+0xe0` `0x80000`), the target's own target this human,
    `0x002672d0` 0 both ways (inferred from the two calls: face to face), a counter chance above 0, and, while the byte
    `*(0x0051489c) + 0x56e3` is 0, a brain of type 3 or a class whose `+0x11b` is 13 (`0x00223e20`). So an AI
    counters grabs and tackles, never strikes; a strike is answered only by the duck counter
    ([Combat](combat/index.md#block)).

What the street civilian and a sparring Warrior did under attack: [How this was checked](#runtime-block).

New power-class fields (confirmed (code)): `+0x08` block chance, `+0x0c` block chance while hurt, `+0x24` counter
chance, `+0x37` the pattern-reading threshold ([Power classes](characters.md#power-classes)).

## In Coney

- **Fighting**: `FightGoal` in the original's order (`repo:src/ai/fight_goal.h`): the valid-enemy and slot check, the
  tackle try, the 1.1 × far range, the walkable line every 30 updates, the re-target, the block try, the wait on the
  actions, the deadline, the pick, `Brain_CheckAttack`, then the ring, the move into the kind's reach or the grab and
  snap try and the press. `Brain_QueueAttack`'s chains are timed by the chain clip's first event; `AttackAction`
  presses the command once in Start (the delay halved when the target targets the attacker or the brain is type 3,
  then a wait on `0x5c0221f`), with a full stick along its angle when it has one, and, when neither human is busy,
  moves the target's `+0x1ec` on by the kind's swing time over his spacing. `MoveToHumanAction` sets a heading and
  speed in the record's `move` (no stick).
- **The attack choice** (`repo:src/ai/attack_choice.h`, `repo:src/ai/attack_views.h`): `Human_CanUseAttackKind`'s table
  and `Human_CanStartAttack`'s guard as pure tests over two views of the humans, and `Brain_PickAttack`'s draw with
  every adjustment (the running rule, the hurt rule, the crowd on A, the busy target for the grab, the armed bonus,
  the rear-grabbed bonus, the pattern read). `attackerViewOf` and `targetViewOf` fill the views from Coney's fighters.
  **Stand-ins**: "free" is on its feet in no pair and not reacting; no AI holds an object, spray paint or cuffs; the
  pattern read is not made; the kinds whose commands no Coney handler takes (8, 9, 11-13, 15, 18, 23, 41) are left out
  of every pick. Each kind's near reach (the walk-in) and far reach (the start test) are the anims of
  `repo:src/ai/attack_reach.h`, read from the attacker's Anim Range List; an anim without a record reaches 2 m.
- **Taking turns** (`repo:src/ai/attack_places.h`, `repo:src/ai/fight_book.h`, `repo:src/ai/fight_checks.h`): each
  brain keeps four active-attacker places, the two spacing bytes (raised by each slot claim to the attacker gang's
  `CfgGang` values 2 and 3, a Warrior target keeping 1 standing, back to 1 when its slot list empties) and the tackle
  meter, kept by the cops', gang soldiers' and Warriors' thinks. `CfgGang`'s values 2, 3, 5, 7 and 8 are read per
  gang kind from the scripts' calls. `Brain_CheckAttack` answers in the page's order; a waiting attacker holds the
  reposition ring. **Stand-ins**: the rows about held objects (4, 6, 8) never apply; the police row applies when the
  target's own target is a cop; the ring is a move to its outer edge, standing within it, with no taunt; the fight
  stance is not built (the charge and the dive count as out of it); the re-target's threat is a valid enemy on the
  brain's list; the swing time reads the
  attacker's own set, not the bat and bottle sets' clips (25 and 29 the throws, 26-28 the power strikes, 31 the
  struggle strikes 96 and 108, 32-34 the escapes 100 and 112).
- **A fight's three goals** ([Closing on the target](#fight-approach), `repo:src/ai/melee_goal.h`,
  `repo:src/ai/engage_goals.h`): `Brain::fight` (with a duration, `GoalFight`'s none, a rioter's 8000 ms) pops any Melee
  or FindEnemy goal with everything above it and pushes FindEnemy, Melee and the fight goal, whose deadline is the
  duration. The fight goal never closes a distance; the **Melee** goal (no limit or the duration from its first run)
  takes the best-scoring enemy (below), pushes a fight goal of 4000 ms within 1.1 × the far range (after a walk at gait
  2 when the straight line does not reach him) and an **EngageEnemy** goal beyond it; **FindEnemy** starts the fight
  again while the target can be fought. EngageEnemy runs at gait 4 (5 after a runner), re-plans every 250 ms, leads a
  target facing away, stops and turns to him within 3.75 m when he is busy or the charge is not armed and he walks or
  stands, and attacks out of the run within 1.6 m with the charge armed: a cop's X1 (50 %, 80 % at a gang member) or
  tackle, anyone else's pick, which ends the run-in when it is not a charge kind, pressed as one attack action once it
  can start (the X1 along the runner's heading); another human the near one in the sector of the target's record
  the runner comes from disarms the charge. The grab and snap tries, the grab's "behind" and the lead read
  ai-core's [sector records](ai-moving.md#neighbour-sectors). EngageEnemy stops for a target in sight it may
  not attack (`Brain_IsAttackableBy`), re-targets the nearest enemy it sees within 9 m, and gives up out of sight 20 m
  away. **Stand-ins**: the gang's wanted timer is not kept (the chase always allowed); Melee's own line-of-sight branch
  is not built; with no target but a valid enemy Melee spectates (without the shadow and may-spectate tests); a
  human whose last move failed runs straight at the target for 2 s and may approach again (the weapon pick-up, throw and
  positioning moves are not traced); the type-3
  AttackTarget gate and goal `0x35` are not built (so the deadline's FollowAndDefend test never applies); past its
  deadline a fight goal ends as [the deadline](#fight-durations) says, reading the target's active places, and a
  re-target ends that update with the kind kept (since 2026-10-21). `level99`'s sparring Warriors are sent from
  8.6-9.0 m ([the sparring fight](#level99-fight)) and run in this way: `coney_tests "the disc's level99: the sparring
  Warriors*"` checks that all three run in with EngageEnemy, close in and attack, that exactly one charges (faster than
  the run's 7.80 m/s) and that the other two stop short of the player, as in the original. The
  [steering](ai-moving.md#steering)
  round each other puts those two a little behind the first, so his charge has made the player busy when their re-plan tests
  step 9; without it the three ran in abreast and all charged at once (clip 1, up to 9.6 m/s), ending 2.1-2.9 m past
  him. Their run-ins end at 2.5 and 4.0 m from the player; the original's ended at 2.1-2.6 m, and why one of Coney's
  ends farther is not traced.
- **Sight and targeting** (`repo:src/ai/perception.h`, `repo:src/ai/targeting.h`, [Sight](#sight),
  [the score](#enemy-score)): the line of sight is the two rays (1.7 m, then 1.0 m) through the level's collision mesh,
  passing the six see-through materials; the field of view and `Human_CanSeeHuman` (range, the shadow's rule, then
  the line); the brains
  get the mesh from `Brains::setCollision`, and an attack warning needs the line too. `Brain_ValidateEnemy`,
  `Brain_CanBeChased`, `Brain_IsAttackableBy` (the attackable byte `+0x11f`, `GangSetAttackable`, and the street
  civilian and dog exceptions), `Brain_CanTakeSlotOn`, `Brain_ScoreEnemy` with every term and the weights of
  `CfgSetTargetingPoints` / `CfgSetTargetingPointsEx` (read from the scripts' calls), and `Brain_PickBestEnemy` with
  the goals' adjustment (the previous target's 3 points). **Stand-ins**: Coney has no trains, fires,
  muggings, interrogations, tagging or turf-only rule, so those tests pass and those terms give nothing; only the
  level's static collision blocks a sight ray; a gang's chosen target (`+0x10`) is kept but nothing sets it yet; the
  down term's `Human_IsKnockedDown` reads the Grounded target state, Coney having no state flag `0x80000`
  ([q-score-down-flag](#q-score-down-flag)), and the armed term's `Human_IsHoldingObject` counts any world object in
  hand, whatever its kind, and a weapon's anim set with no object ([q-score-held-object](#q-score-held-object)). The
  sight range is at least 30 m while the gang is fighting, whatever its kind (`sightRangeOf`, #1043).
- **The enemy scan** (`repo:src/ai/enemy_scan.h`, [The enemy scan](#enemy-scan)): `scanEnemies` lists the members of the
  enemy gangs in use that it sees (sight range, the shadow's 2 m rule, the spectate test's 1.9 m height limit, the
  Warriors' threat filter, the near radius of 1.5, 3 or 5.2 m all round and the view cone beyond it, then the line of
  sight), keeps old entries within the range, valid and near or in sight, keeps the 16 nearest, and tells the scanner of
  each new one as event `0xb` (its script's handler, else its type's; a tactic through `Brain::addEnemy`). A gang always
  seen (`GangSetAlwaysSeen`) skips the spectate test and the sight step, and the spectate test is `Human_MaySpectate`'s
  (`maySpectate`). `ScanSchedule` runs it every interval (2 s, cops 1 s, × 4 locked on in the stance, or a goal's own)
  with five scans an update at most. **Coney stand-ins** (#1011): the spectate test does not pass a scanner whose class
  role Coney cannot look up (the original always has one), Coney builds no Chase goal (`0x0c`) for `mayStartFight` to
  find, the investigation count is taken as 0 and the vertical sight modifier is not applied; nothing sets the gang's
  chosen target; the civilians' ped type, the hostile counts, the radar's enemy marks, the detail level's slowdown and
  the rescan on a gang change are not built. The Scout tactic (below) scans its members at their posts.
- **Blocking**: `BlockGoal` never produces a block: Coney's block starts only on R1 held in the record's buttons,
  which only a pad writes, so its command 4 does nothing. It turns the human's hit reactions off
  (`Fighter::setHitReactionsOff`, bit `0x800`) from its start until its sixth update with the human free; rolls the
  counter on every update of the block time (`BlockGoal::counterRoll`: the roll, then `counterTest`: a type-3 brain,
  free and not hurt, face to face with a target aiming at it in a grab's or a tackle's intro) and writes command 3 on
  success; extends the block while the target still attacks, which stops the rolls and queues a punishing attack in
  the block's last second.
- **The counter** (`repo:src/combat/ai_counter.h`, `Fighter::answerCounter`): command 3 from a human no pad drives,
  free, unhurt and empty-handed, is kept for one update; the human in a grab's or tackle's intro at it takes it on its
  own update when the two face each other, and the pair plays: the hold ends, the counterer plays 76 (9 against a
  tackle) and the attacker 77 (10) from the counterer's set, takes the counter's damage from the counterer's Anim
  Range List and stands stunned after it. **Coney readings**: the class-13 way through the type gate is left out and
  its byte `*(0x0051489c) + 0x56e3` taken as 0, so only the Warriors' brains counter; the tackle counter ends as the
  grab's (stunned); no power is spent; Coney's tackle holds its victim from the intro's start, whose reaction goal
  then holds the block goal off, so in play only grabs are countered.
- **Configuration** (`aiConfigFrom`): the class's `CfgChar`, power class 40's `CfgPowerClass`, `CfgAttackDelay` and
  `CfgBaseChanceToBlock` from the scripts' recorded calls. Without the disc the reference values stand in: power
  class 40's AI fields (block 0.2, 0.1 hurt, delay factors 20, counter 0.08), base block chance 60, the
  `config_preload2` delays, `Att_Normal`.

**Coney choices.** A fighter is class 58 (brain type 2, 1400 health) with the sparring Warriors' runtime brain values (4
attack slots, melee 3 / 5 m, sight 30 m, field of view 1.92 rad), drawn and animated as the player's character, in a
gang of kind 19 made the enemy of the player's (the Warriors' kind), and an "engaging" toggle stands in for the script's
`GoalFight` (an idle fighter takes the player on within its far melee range). Who fights whom in the characters' step is
whoever's gangs are not friends (`Humans::setOpposition`); without gangs, the humans added pad-controlled and the others
fight each other, and only the former fight the sandbox's passive targets. A move runs beyond 4 m and lasts 1000 or 2000
ms (2000 beyond twice the reach). Command `0x11` chains as
square. A reaction goal clears the actions and the move. The fight reaction goals (grabbing, mounting, grabbed,
mounted, grounded) are built in `repo:src/ai/fight_reactions.h`; their stand-ins: no trains, the presenting does not
test player 1's Warrior command, the FollowAndDefend that clears the hand-over is TrackHumanGoal, no help call,
and a held AI's presses reach no handler (its holder drives it). A block ends when its target is not on its feet
(Coney has no state word). Each brain's generator is seeded by its slot.

## Open questions

- Why Coney's waiting attackers taunt about a tenth as often as the original's (the level99 sparring fight: 11
  `cheer2` taunts in 1,198 updates in the original, 0-2 in Coney; [The reposition](#fight-reposition)). To check, in
  this order: how long each Warrior waits in the reposition (Coney may let more of them attack at once); and the
  close-in test, which reads the player's own target. The gesture slots are ruled out: Coney keeps them as the original
  does, per reaction kind and help level ([Gestures](sound-events.md#command-ids)), and the sparring Warriors (type 2)
  have two, as everyone had before.

### Coney's knocked-down test {#q-score-down-flag}

The score's down term (`0x20`) asks `Human_IsKnockedDown` (`0x00223b98`: state `0x80000`, with a record), and Coney has
no such flag; its port reads the Grounded target state instead. Whether the two agree from the knockdown's first frame
to the end of 199 (the flag is cleared by 199's end callback, [Getting up](combat-reactions.md#ground-escape)) is not
checked: compare both through a knockdown in PCSX2 and Coney.

### What an AI holds, for the armed term {#q-score-held-object}

`Human_IsHoldingObject` (`0x00224060`) counts a held world object whose kind (`+0x86`) is not 0, whatever its anim
set. Coney's score cannot reach the level's object types from a brain, so it counts any world object in hand, and also
a human who has a weapon's anim set with no object (a spawner's item, which Coney does not put in the hand). Whether
any object an AI can hold has kind 0 decides whether the first ever differs: list the kinds of the objects the scripts,
the weapon piles and the spawners hand to AI humans.

## How this was checked

### The re-target's timing {#runtime-retarget}

PCSX2-MCP fork 2.9.129, 2026-10-21, scenario `warriors_taunts` with the `fight-retarget-due` hook at
`0x002b3c18`: in level99's sparring fight the three Warriors' fight goals started or
resumed 28 times in 1,198 updates. Each time the first re-target test read `+0x14` equal to now (no re-target), and
the test on the next update read now 33-34 ms past it and re-targeted: the update after that, `+0x14` was 966-967 ms
ahead. Four times a goal started or resumed twice in one update, and the second test also read `+0x14` equal to now.

### The waiting taunts {#runtime-taunts}

PCSX2-MCP fork 2.9.129, 2026-10-21, scenario `warriors_taunts` with the `say-command` hook on
`Human_SayCommand` `0x002205e0`: in level99's sparring fight against a still player, the
three Warriors said `cheer2` from `FidgetAction_Start` (return address `0x002fa090`) 11 times in the 1,198 updates
from their run-in (updates 502-1700): one Warrior once, the other two five times each, each one's taunts 37 to
582 updates apart (the 1000-2000 ms timer allows one every 30-60 updates at most). Two earlier recordings of the
same state end 12 updates sooner, before the eleventh taunt, and hold 10 each: the first ten, at the same game
ticks, so the fight is deterministic from that state. Coney's count
over the same span, printed by the level99 sparring disc test for its eight AI sequences (2026-10-21, two runs), is
0-2: 2 or 1, 0, 0, 0, 1, 0, 1, 0. Coney's waiters taunt far less often than the original's; why is an
[open question](#open-questions).

### Melee ranges {#runtime-melee-range}

PCSX2 2.9.94: read from the user's save states 1 and 6, all 19 brains had near 3.0 and far 5.0 m; so did the
lesson-12 trace ([the sparring fight](#level99-fight)).

### The attack action {#runtime-attack-action}

PCSX2 2.9.94, the scenarios of [Runtime checks](ai/index.md#how-this-was-checked).

The street civilian (`PoizoCiv`, class 417, `Att_Normal`, power class 2) made a type-2 brain and
set on the player with `GangMakeEnemies` and `GoalFight` (no save reaches `level99`'s `CombatWarriors` fight, so this
is a stand-in with the same attack table). Hooks on `0x00147ef0` and `0x00147ef8`:

- The goal stack was `0x41` (FindEnemy), `0x08` (Melee), `0x0f` (Fight); the front action was the attack action,
  the move-to-human action or, when far, a move action (goal `0x0b`, [EngageEnemy](#engage-enemy)).
- Commands seen: `0xd` (grab, then a throw with a stick of 1.0), `0xf`, `0x10`, `0x11`, `0x12`, `0xe` and `0x20`
  (charge). Each is written once, in the attack action's Start, and read by the dispatcher in the same step; the next
  step's record update clears it ([Tasks](tasks.md#humans-update)).
- **Chains**: the second press came 9 updates after `S1`'s (`0xf` → `0x11` or `0x12`), 12 after `SS2`'s, 13 after
  `X1`'s (`0x10` → `0x12`), inside the chain window as `AttackKind_ChainDelay` plans.
- `+0x1e8` was set to now + 700 ms at a chain's first press, + 1100 ms at the second, about + 1400 ms at the third,
  and + 1750 ms for `0xe` and `0x20`: each press of a chain pushes the next attack further.

The three sparring Warriors against a still player (`warriors_passive`,
[the sparring fight](#level99-fight)): with factor 20 (power class 40), `+0x1e8` went to now + **4000 ms** after a
200 ms kind, + 8000 ms after `0x37` / `0x38` (kinds 12, 13: 400 ms), and + 2000 ms when halved. So a sparring Warrior
waits about **4 s between attacks**; the still player took 22 attack commands from the three in about 40 s, many of
them grabs.

### Blocking {#runtime-block}

PCSX2 2.9.94; the warning count rose by one per event `0x10` in both runs.

- **The street civilian** (`civ_block`, `civ_block_fov`), at 1.2 m, the player pressing square every 20 updates. With
  its class's field of view `+0x12c` = 1.5708, the player's attacks, aimed from in front but outside that test, sent
  no event `0x10` and it never blocked. With `+0x12c` set to π, six events `0x10` arrived; `Goal_TryBlock` saw
  `+0x200` = 2 once and pushed goal `0x1b` (`+0x10` = now + 1981 ms, `+0x14` = 1, `+0x16` = 1), which wrote command 4
  every update; the civilian never took a block state and kept taking hits.
- **A sparring Warrior** (`warriors_block`, [the sparring fight](#level99-fight)), the player pressing square every
  15 updates: Generic1 had 43 warnings and 5 block tries (with `+0x200` = 2); the one block goal wrote command 4
  every update for about 53 updates while its state stayed `0x3`, it played hit reaction 272 and its health went from
  1295 to 1278. No command 3 came (the player never grabbed or tackled).

### The sparring fight {#level99-fight}

The scenarios `warriors_passive` (no input) and `warriors_block` (square every 15 updates) record `level99`'s
`CombatWarriors` fight as the script runs it: the three Warriors `Generic1`-`3` (classes 58-60, brain type 2,
`Att_Normal`) each get `GoalFight(warrior, player, 0)` from `P1.SendWarriors` when the scene `l99_c5` ends: in the
recording, 481 updates (16.0 s) after the saved state, which was taken just after `P1.SetupWarriors` started the
16.7 s scene ([Scenes](scenes.md#humans)). Results: [the attack delay](#attack-action), [the block](#block) and
[the cast's numbers](ai/index.md#level99).

**Who fights in lesson 12.** `P1.SendWarriors` (the scene's return function), in order: `GangBrDead(false)` for
the sparring gang and the first fence gang; `HuUseAnim(…, 0, nil)` on each `FenceWarriors1` and
`HuSetNoTarget(true)` on each `FenceWarriors2`; **`TacticCrowd(fence gang, nil, true)`** (cheering) for the two
fence gangs only; the tutorial text; `GangMakeEnemies(Warriors, CombatWarriors)`; then for each of the three
`CombatWarriors`: `HuSetPushable(true)`, `HuSetNoTarget(false)`, `HuSetDemiGodMode(true, 0.25)` and
`GoalFight(warrior, player, 0)`; then the 50 s stopwatch (`P1.TimesUp`) and the grab hint's anim callbacks
(inferred from the disassembly of `level99_combat.lua`). The gangs are made in this order, so their ids are 0
`Warriors`, 1 `CombatEnemy`, 2 `CombatEnemy2`, 3 `CombatTeacher`, 4 `CombatWarriors`, 5 and 6 the fence
Warriors, as read at runtime ([the cast while the scene plays](#level99-scene-state)). So:

- The sparring Warriors' gang (4) has **no tactic**, and no script sets its threat response: it keeps the **2**
  every brain gets when it is made (`0x0028a570`). The scripts set 0 only on `CombatEnemy`, `CombatEnemy2` and
  `CombatTeacher` (and 2 on `CombatEnemy2` when its fight starts). Their `GoalFight` therefore runs in full:
  `Brain_Fight` adds the player as an enemy, takes him as the target and `Brain_PushFightGoal` pushes FindEnemy,
  Melee and the fight goal ([Starting a fight](#targets)). The three take the player on together (the player's brain
  allows 4 attack slots), demi-gods at a quarter of their health, until the stopwatch ends the lesson. Confirmed
  (runtime): they fought in the `warriors_*` recordings.
- **How they reach him.** They stand 8.6-9.0 m away, beyond 1.1 × their 5 m far range ([Melee
  ranges](#melee-range)), so each fight goal ends at its first update and the Melee goal sends them in with an
  [EngageEnemy run](#engage-enemy); a fight goal (4000 ms) follows once each is in range
  ([Closing on the target](#fight-approach)). Nothing else in the lesson gates them: brain type 2, threat response
  2, no tactic, the player reachable (`+0x11e` = 1) and no wanted timer on the gang. Confirmed (runtime), PCSX2
  2.9.94, the `warriors_passive` state with each Warrior's goal stack, position and `+0x140` read every update over
  PINE (a contributor's run, not a committed scenario): 490 updates after the state, all three had **FindEnemy, Melee,
  EngageEnemy** (top) at 8.96, 8.59 and 8.77 m, far range 5.0. They ran in at up to **7.80 m/s** (the run gait;
  one slowed to about 2 m/s for some 20 updates while steering round another). The first reached the player and
  stayed at 0.71 m with an action running (inferred: the run-in's charge, step 11); its EngageEnemy ended at update
  564. The other two stopped at 2.1-2.6 m and swapped EngageEnemy for a fight goal at updates 546 and 562. Each stack
  then read **FindEnemy, Melee, Fight**.
- **Why the other two stop short** (confirmed (runtime), PCSX2 2.9.94, the same state, the three Warriors' and the
  player's state word, gait, speed `+0x1ac` and clip read every update; [EngageEnemy](#engage-enemy) step 9 confirmed
  (code) at `0x002afa48`). The first Warrior's run-in ends in the charge (clip 0), which hits the player and plays the
  extreme reaction 296 on him: his state word becomes `0x180000`, inside the busy mask `0x7bf9e9f7ff0` that step 9
  tests (`HumanRecord_AreActionsBlocked`), and stays so through 296 and the getting-up 198. Step 9 runs only on a
  re-plan (step 6: every 250 ms, or on the target's turn, slowing or coming within 1.6 m), so each runner stops at the
  first re-plan that finds the player busy within 0.75 × far (3.75 m): one 4-5 updates after the hit, at 2.12 m (the
  player had been free until then, so the 3.75 m line had passed), the other 5 updates after crossing 3.75 m, at
  3.15 m. The stop sets the move speed to 0 (`0x0028aac0`) and the run (410, 7.80 m/s) **ramps down by 1.067 m/s per
  update to 0 in 7 updates**, about 0.83 m, with no run-stop clip (410 straight to the idle 388); they stood at 2.10
  and 2.49 m, the player flung farther meanwhile. Then the stop's turn to face (0.3 s), EngageEnemy ends, and the
  fight goal walks them in with the combat-walk clips (380, 387; 3.43 m/s) while he is still down. So the 2.1-2.6 m
  is not a distance of its own: it is where a 3.75 m test, sampled every 250 ms and started when the player became
  busy, leaves a 0.83 m stop. A contributor's run (not a committed scenario), from 470 updates after the state.
- The fence gangs (5, 6) get the **crowd tactic** and no `GoalFight`: they cheer from the fence
  ([TacticCrowd](ai-gangs.md#tactics)). They are friends of the Warriors' gang.
- **Which wins**, confirmed (code) at `0x0028d2e8` and `0x0028d190`: a threat response of 0 stops `GoalFight`
  completely (`Brain_Fight` returns at once: no enemy, no target, no goal); under a gang tactic it adds the enemy
  and takes the target, but **pushes no fight goal**, and the tactic decides what the members do. A scripted
  `GoalFight` never overrides either. Neither applies to the sparring Warriors.

#### Reaching the fight without a playthrough {#level99-save}

The state is not a quick-save slot; it is made once and named by the scenarios' `state`
([Recording a trace](../guides/research-workflow.md#recording-a-trace)). With the `call-brains` hook on a copy of a
`level99` state (slot 1 is checkpoint 3):

1. Call `0x0041abc0(1)` (`SetCheckPoint`'s worker), then `0x00160d78(name)` (`MenuLoadLevel`'s worker) with
   `"level99"` written to free memory: the level reloads at checkpoint 1, and its tutorial starts about 60 s later.
2. Schedule script functions by name with `0x003863d8(name, delayMs)` (`ScheduleFunc`'s worker), the name in free
   memory: `AddFenceWarriors1`; then `GangMakeFriends`'s worker `0x0016ad80(0, fenceGang)` (the fence gang's id, 5
   here, is brain `+0x20c`'s); then `P1.SetupWarriors`.
3. Save while the scene plays ([Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)), and put the patched
   words back before using it as a source.

#### The cast while the scene plays {#level99-scene-state}

Confirmed (runtime), in that state (`l99-warriors-before-fight`) read both from the state file and over PINE; type ids
read from each goal vtable's `+0x0c` function, confirmed (code) at the constructors in [Goals](ai/index.md#goals):

- Brains 0-18 are in use. Everyone in the scene (Rembrandt, Vermin, Cleon, `Generic1`-`3`) is dead to the AI (`+0x09`
  = 1, think the stub `0x004edd40`) with one goal, **JoinCinematic** (`0x29`), whose actions are a move (vtable
  `0x00542f20`, `MoveAction_Init`'s) and then a turn (vtable `0x005430e0`, the one `Action_TurnToTarget` makes).
- Ash (type 3) is suspended (`+0x0a` = 1) but not dead, holding one goal, **HoldPosition** (`0x36`).
- `GenWarrior` and the second wave (type 4) and the two teachers have threat response 0; the sparring and fence
  Warriors 2. The fence Warriors (gangs 5, 6, type 2) are the only brains running their own think (`0x00304608`).
- Gangs (record order = id): 0 the player and Ash, kind 0, alert 1, enemies 1 and 2, friends 5 and 6, with a
  **tactic of type 3** (vtable `0x005439e0`, init `0x00313490`); 1 `GenWarrior`, 2 the second wave (both kind 19,
  enemy 0); 3 the teachers and 4 the three `CombatWarriors` (kind 19, no masks yet: `GangMakeEnemies` comes with the
  fight); 5 and 6 the fence Warriors (kind 0, friends with 0).
- Inferred: the type-3 tactic is set by `0x00313400(…, gang id, …)` from `0x0041c4e0`, which `Human_MakePlayer`
  (`0x00229c40`), `Human_SetWarChief` and `Gang_OnEvent` call: the player gang's standing tactic, and the source of
  Ash's HoldPosition (`0x00313718` pushes it).
