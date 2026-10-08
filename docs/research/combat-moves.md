# Combat moves (the move table and frame data)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); clip and Anim Range List
numbers read from the disc's `warr_re_cv` (Rembrandt) and generic character data; runtime claims with PCSX2 2.9.94
(2026-10-07), copies of the street save state (slot 6) driven over PINE with the scripted pad, the right-stick and
puppet patches, and the `strike-contact` and `strike-shape` hooks ([Driving
PCSX2](../guides/research-workflow.md#driving-pcsx2)). The scenarios are `repo:research/traces/scenarios/moves_*.toml`.

## Purpose

Every move a human can make in a fight, in one place: what input or command starts it, which tests gate it, the clip
it plays, its strike shapes and phases in updates, its reach and damage, and what refuses or cancels it. The
mechanics behind the moves (chains, block, grabs, damage, reactions, meters, targets) are on
[Combat](combat.md); this page indexes them by move, fills the moves that page leaves out (strafe attacks, the
counters from square and cross, cross at a grounded target, tandems, stealth kills, the mounted victim's actions, the
knife and bottle kills and thrown melee weapons) and gives frame data for all of them. Where Coney differs:
[Combat differences](combat-diffs.md).

## Original structure

| Address | Name | What it decides | Evidence |
| --- | --- | --- | --- |
| `0x0027c120` | `Player_UpdateActions` | the dispatcher: block, state routes, then the command ([Combat](combat.md#dispatch)) | confirmed (code) |
| `0x00286cc8` | `Player_Square` | square: moving attacks, strafes, snaps, target-state strikes, tandem, counters, `S1`; the armed branch | confirmed (code) |
| `0x00287a18` | `Player_Cross` | cross: the same order without strafes and snaps, with 194 and 661 for low targets | confirmed (code) |
| `0x002880d8` | `Player_CrossWithWeapon` | square and cross with a set 4-6 object: smash or throw | confirmed (code) |
| `0x00288838` | `Player_ArmedSpecial` | commands `0x22` / `0x39` with a melee weapon: throw it (`Player_ThrowMeleeWeapon`, `0x00288698`) | confirmed (code) |
| `0x00287730` | `Player_Special` | cross + square, circle + cross, circle + triangle ([Combat](combat.md#run-attacks)) | confirmed (code) |
| `0x00284920` | `Player_GrabOrTackle` | circle tapped / held ([Combat](combat.md#grab)) | confirmed (code) |
| `0x0027ec20` | `Player_UpdateMounting` | the mounter's inputs ([Combat](combat.md#mount)) | confirmed (code) |
| `0x0027f1a0` | `Player_UpdateTackled` | the mounted victim's inputs: struggle, get-off, reversal | confirmed (code) |
| `0x0027fd68` | `Player_UpdateGrabbed` | the held victim's inputs ([Combat](combat.md#grabbed)) | confirmed (code) |
| `0x0027e040` | `Player_UpdateActionsHidden` | hidden in shadow (state `0x200000`): the stealth kill | confirmed (code) |
| `0x0027de78` | `Player_OnCommand36` | command `0x36`: the push 21 | confirmed (code) |
| `0x00287fe0` | `Attack_StartGroundStrike` | commands `0x37` / `0x38`: 193 / 194 | confirmed (code) |
| `0x0027d6e0` | `Player_TryCounterGrab` | command 3: the AI's counter, 76 or 9 | confirmed (code) |
| `0x00258e10` / `0x0026f860` | `Tandem_CanStart` / `Tandem_Start` | the three-person tandem | confirmed (code) |
| `0x00258e88` / `0x002590f8` | `Human_CanCounterGrab` / `Human_CanCounterTackle` | whether a grab or tackle coming at the human can be countered | confirmed (code) |
| `0x00264738` | `Player_StartStealthKill` | the stealth kill 637 / 639 / 641 | confirmed (code) |
| `0x00261c80` | `Attack_StartAtTarget` | starts a strike on a target in a given state: 212, 661, 120, the push 21 | confirmed (code), runtime |
| `0x002723e8` | `Mount_StartReversal` | the mounted victim's reversal 242 / 243 | confirmed (code), runtime |
| `0x00225200` / `0x002250a0` | `Human_IsHighOrBusy` / `Human_IsMidOrBusy` | the low and mid target tests ([Square](#square)) | confirmed (code) |
| `0x00261a08` | `Attack_StartGrounded` | a strike at a low target: 193, 194, the armed slot `0x13` | confirmed (code) |
| `0x00269f30` | `Human_BlockHit` | whether a block stops a hit, by weapon set | confirmed (code) |
| `0x00280630` / `0x00280708` | `Player_BufferChain` / `Player_UpdateChain` | the one-press chain buffer and the next attack ([Input](#input)) | confirmed (code), runtime |
| `0x0027a6c0` / `0x0027a4b0` | `Player_PickTarget` / `Player_FindAttackTarget` | the attack target searches ([Targets](#targeting)) | confirmed (code) |
| `0x00147940` | `Commands_Match` | turns pad samples into one command per update ([Input](#input)) | confirmed (code) |

## Data

### Reading the tables {#reading}

- **Updates** are game updates of 1/30 s, counted from the clip's first update (k = 0). A clip of *f* frames at
  playback rate *r* lasts ceil(*f* / *r*) updates. The rate comes from the Anim Range List record's flags: `0x800` →
  0.8, `0x1000` → 1.0, `0x2000` → 0.9, none → 0.75 ([Playback rate](formats/animation.md#playback-rate)).
- **An event at frame *f*** (strike shape on or off, warnings, `use`, the held phases) acts on the first update k
  whose cursor frame reaches *f*. The clip time on update k is the sum of k steps of dt × *r* (dt at `0x005102cc` is
  `0x3d088889`, 0.033333335), and the frame is `uint(t × 30 + 0.5)` (`AnimCursor_Advance` `0x001044a0`; the time
  is added in `AnimCursor_Step` `0x00104110`). `Anim_FireFrameEvents` (`0x00101dd8`) fires each event whose frame is
  at most the cursor's. All of this is confirmed (code). In closed form, the event acts on the **first k with k ×
  *r* ≥ *f* − 0.5**. The exception is an **exact tie** at rate 0.75 or 0.9 (k × *r* = *f* − 0.5: frames 5, 8, 11
  and so on at 0.75, frames 5, 14, 23 and so on at 0.9), which acts **one update later**. Frame 2 at 0.75 (k = 2)
  is the one tie that acts on time. The PS2 FPU rounds every add and multiply toward zero, so the summed time falls a
  hair short of the tie; at k = 2 it is still exact. Rates 0.8 and 1.0 have no ties that fall short. The rounding
  mode is inferred; with it the rule matches all 17 strike-shape windows measured after the recorder fix ([Runtime
  results](#runtime)) and the held phases below. A reimplementation can get the same updates by summing the time in
  `float` and rounding each step toward zero, or by using the closed form with the tie exception.
- **The held phases** W (chain window `0x2`, event `0x2c`), E (end `0x4`, `0x2d`) and R (recovery `0x40000`, `0x48`)
  show in record `+0x08` on the update that rule gives: `S1` 6 / 15 / 16, `X1` 10 / 20 / 21 and `XX2` 17 / 19 match
  the runtime timings on [Combat](combat.md#attacks). Confirmed (runtime) for those three.
- **Strike shapes**: event `0xf` turns a bone's shape on and `0x10` off; `0x13` / `0x14` turn on and off all ten
  shapes and the capsule ("all"). Bones: L/R forearm and hand (18/19, 24/25), shin and foot A (29/30) and B
  (32/33), spine 3, head 6 ([Combat](combat.md#moving-strikes)). Shown "on-off" in updates; a shape is live from the
  first update to the one before the second.
- **Events**: `eblk` early block (`0x26`) and `duck` warning (`0x24`), which tell a target to block or duck
  ([Combat](combat.md#block)); `KD` knockdown (7); `use` (`0x41`), which wears a held weapon or uses the flash.
- **Reach / far** are the record's reach and far ranges in metres; when the stored far (mm) is not above the reach,
  the far range is reach × 1.25 (inferred from the disc's values).
- **Damage** is the disc's value for the clip, then the value Rembrandt plays where the class table writes that
  index ([Combat](combat.md#damage-table)). The class value is scaled by the Warrior byte (115 %) with the PS2 FPU's
  rounding toward zero, which is why 50 gives 57, not 58, and 230 gives 264: every value measured at runtime
  matches. Inferred (the FPU's rounding mode), confirmed (runtime) for the 17 measured values.
- **Hit code** picks the victim's reaction ([Combat](combat.md#hit-codes)); `stun` is flag `0x400`, `0x100` marks
  the throws.

### Who plays what

The player's set is `warr_re_cv`; ids its own data leaves unset fall back to the generic set (`civl_co_ma1` has the
same clips). The tables below take the clip from the player's set when it has one. Damage and reach are the
**player's** record. Another class's values (and the AI's damage tables) differ: [Attack
kinds](../references/attacks.md#damage-table).

## Behaviour

### Timing: when a move hits, and what it takes {#timing}

Start here to build the fight's feel. Updates (k) are 1/30 s from the clip's first update. Confirmed (code) at
the addresses given; the phase updates are confirmed (runtime) for 12, 11, 13, 16 and 20 ([When input and the stick
come back](combat.md#input-return)) and come from the clips' events for the rest ([Reading the tables](#reading)).

**A hit has no fixed frame.** No attack start deals damage (`Attack_Start` `0x002625a8`, `Attack_StartSolo`
`0x00262368`). Damage comes only from `Strike_Contact` (`0x0021b290`), called by the strike test that runs every
update after the move while one of the clip's **strike shapes** is on: the first update a shape overlaps the target's
spine or head shape ([How a moving attack strikes](combat.md#moving-strikes)). The same body is struck once per
window. So the hit update depends on the distance and the steer, and a strike that never touches misses. The
"contact" column is that update measured with the target standing near the move's reach; the shape window is the
range a target can be hit in. The paired moves (grab strikes, power moves, counters, tandems, stealth and knife kills)
are the exception: their damage is applied by the pair's own code, mostly on the first update.

| Id | Starts on | Strike shapes on-off | Contact (runtime) | Takes the next press | Window `0x2` (plays it) | End `0x4` | Recovery `0x40000` | Free again |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 12 `S1` | square pressed, the same update | L hand 1-10 | 2 (3 from 1.5 m) | 0-14 | 6-14 | 15 | 16-19 | 20 |
| 16 `SS2` | the window of 12 | L forearm, L hand 2-7 | 4 | 0-18 | 6-18 | 19 | 20-24 | 25 |
| 19 `SSS3` | the window of 16 | R hand 7-15 | 7 | none | - | 16 | 17-26 | 27 |
| 20 `SSS3_HOLD` | the window of 16, instead of 19 at random ([Combat](combat.md#attacks)) | shin A, foot A 15-21 | | none | - | 24 | 25-31 | 32 |
| 17 `SSX3` | the window of 16 | shin B, foot B 5-12 | 7 | none | - | 24 | 25-33 | 34 |
| 11 `X1` | cross released 1-3 updates after the press, or its 4th held update | R hand 6-11 | 8 (7 in two of three runs; 10 from 1.0-1.6 m) | 0-19 | 10-19 | 20 | 21-29 | 30 |
| 15 `SX2` | the window of 12 | R hand 6-11 | 7 | none | - | 14 | 15-19 | 20 |
| 14 `XS2` | the window of 11 | L hand 7-12 | 9 | none | - | 22 | 24-29 | 30 |
| 13 `XX2` | the window of 11 | R forearm, R hand 9-14 | 10 | none | - | 17-18 | 19-29 | 30 |
| 25 / 27 / 29 snaps | square, stick above 0.95 and more than 45° off the facing | forearm, hand 2-7 (29: 1-7) | 4 | none | - | 8 | | 16 |
| 23 from a walk | square or cross, gait 1-3, stick 0.12 or more, no stance | L forearm, L hand 5-11 | | none | - | | | 24 |
| 24 from a run | square or cross at gait 4 (cross also at a sprint) | R forearm, R hand 2-8 | | none | - | | | 22 |
| 653 special | square newly down while cross is held | L hand, R hand 2-9 | 7 | none | - | | | 27 |
| 193 / 194 | square / cross at a low target | foot B 11-13 / 11-17 | 12 / 15 | none | - | | | 27 / 32 |
| 0 charge / 1 dive | L2 held + cross / square at a run | all 4-17 / all 2-24 | | none | - | | | 27 / 62 |

"Free again" is the first update a press or the stick acts after the move: the clip's length (a run attack hands
back to the run at once). The rest of the frame data is in [Frame data](#frame-data).

#### Input, buffering and cancels {#input}

From `Commands_Match` (`0x00147940`; the tables are in [Commands](combat.md#commands)) and the dispatcher:

- **A command lives one update.** The matcher clears the per-player command (`+0x20`) before matching each pad
  sample, one sample per update. A command not used that update is gone. Only the chain below keeps a press.
- **Square** acts on the press (`0xf`). **Cross** acts on `0x10`, which fires when cross is released after 1 to 3
  updates down, or on its 4th held update (`Pad_TapOrHold4`, `0x00144d60`), so `X1` starts 1 to 3 updates after the
  press. Cross's press `0x12` is used only by the chain.
- **Cross + square** (`0x22`, `Pad_ComboPress` `0x00144f48`): both down and **square** not down on the previous
  update. Square pressed on the same update as cross, or while cross is held (before `X1` has started), gives the
  special; the combination overwrites square's `0xf`, so no `S1` plays. Square first and cross after gives `S1`
  then `SX2` (the cross press is buffered). Circle + cross (`0x23`) fires on the update the second of the two goes
  down; L2 + cross / square (`0x20` / `0x21`) need L2 already held.
- **The chain's buffer** (record `+0xb8`; `Player_BufferChain` `0x00280630`, `Player_UpdateChain` `0x00280708`)
  runs every update before the commands, while record `+0x08` has `0x1`, `0x2` or `0x4`:
    - it **takes** a press while the window `0x2` is open, or during the wind-up `0x1` while the combo count is below 2
      (or is 2 and the clip is `SS2`): cross pressed (`0x12`) is stored as 1, square (`0xf`, or the AI's `0x11`) as 2,
      and a square with the stick above 0.95 and more than 45° off the facing as a snap (2 plus `0x100` back, `0x200`
      left or `0x400` right). One slot: a later press overwrites it.
    - it **plays** on the first update the window is open with a press stored: the next attack by [the chain's
      table](combat.md#attacks), or, for a pad player's buffered snap, the snap when a snap target is within 1.3 m
      (the far range of 25) or `CfgSnap` is off; otherwise the plain square step. A step from `S1` or `X1` is not
      played while the target is low (`0x00225200`).
    - it is **emptied** by every attack start (`Attack_Start`, `Attack_StartSolo`, square and cross themselves).
      A press in the end phase `0x4` is refused by square and cross, and one in recovery `0x40000` by the
      dispatcher's gate `0x5c7fee0`: neither is kept for later.

  So the longest a press waits is from the attack's first update to its window: up to 6 updates in `S1` and `SS2`,
  10 in `X1`. There is no buffer across the end of a move: a press must land after it ends.
- **Cancels.** Nothing interrupts an attack except the chain step at its window. Every other command needs the
  human not busy (`Human_IsBusy` `0x00223cb0`: record `+0x08` `0xaeebf7ff`, which holds `0x1`, `0x2`, `0x4` and
  `0x10`) and passes the dispatcher's gate (`0x5c7fee0`, which holds the recovery). R1's block is tested before both
  gates (`Player_UpdateActions` `0x0027c120`), and `Human_CanFight` (`0x00224f28`: no held flag outside
  `0x20081404`) lets it through in the end phase `0x4` only, so a block can cut the end phase but not the wind-up,
  window or recovery. Confirmed (runtime): R1 pressed in `S1`'s window gave the block 606 at k15, 5 updates before
  `S1` ended; pressed at k16 (recovery) it came at k20.
- **Blend.** A new attack enters over **0.2 s** from the current pose (`AnimTaskChained_Construct(0.2, ...)` in
  `Attack_Start`, `Attack_StartSolo` and `Attack_StartSnap`; 0.1 s in `Attack_StartAtTarget`, so 212, 661 and 120); a
  chain step taken in the window swaps the clip inside the running task instead (`Attack_Start` with
  record `+0x08` `0x2` and a combo count of 1 or 2). Confirmed (code); how the swap blends is not traced.

### Targets: which human each move goes for {#targeting}

Confirmed (code) at the addresses cited. Angles are measured from an **aim heading**; distances are flat squared
distances between the humans' positions; "nearest" sorts by that distance (`SortByKeyAscending` `0x003868d0`).

| Move | Target it uses |
| --- | --- |
| Square in the stance (`S1`, the low, mid and held strikes, the tandem, the counters) | the **current target** (human `+0xc8`), unless a pad player has none or it is beyond **3 m**: then a fresh `Player_PickTarget(2.0 m)` |
| Square's moving attacks (23, 24, 193 from a run or walk) | a fresh `Player_PickTarget(2.0 m)` |
| Cross (`X1`, 23, 24, 194, 661, 120) | always a fresh `Player_PickTarget(2.0 m)` for a player (`0x00287a18`) |
| Armed square and cross | a fresh `Player_PickTarget(2.5 m)` |
| A chain step | a cross step (`SX2`, `XX2`, `SSX3`) re-searches with `Player_FindAttackTarget(h, next id)` when not locked; a square step keeps the target |
| Snaps 25 / 27 / 29 | their own search: nearest within 2.0 m and 45° of the stick, line clear ([Combat](combat.md#attacks)) |
| The charge 0 and the dive 1 | `Attack_FindNearestInReach`: humans **between the reach and the far range** (3.29-4.11 m, 2.22-3.00 m) inside 54°, then objects and glass; the charge only warns it, the dive takes it and steers over 0.1 s ([Combat](combat.md#charge-aim)) |
| The special 653 and the strong grapple | `Player_FindAttackTarget` with the far range of id 0 (4.11 m) and of id 1 (3.0 m) ([Strong grapple](combat.md#strong-grapple)) |

**`Player_PickTarget(range)`** (`0x0027a6c0`), in order; the first pass that finds anything ends the search:

1. The current target is kept as it is only when it is a strikeable world object (`Target_ObjectFilter`
   `0x0027a120` accepts only world objects): a **human target is always searched for again**.
2. The aim heading is written to record `+0xd4`: the camera-turned stick's angle when the stick is above 0.01, else the
   facing (`Player_GetAimHeading`). The cone `0x0051096c` is set to **0.9425 rad (54°) each side**
   (`Target_AngleFromAimHeading` `0x002790a8`: the absolute wrapped difference).
3. Humans within range × 1.1 inside the cone that `TargetFilter_Enemy` (`0x00279410`) accepts; then the same with
   `TargetFilter_EnemyStanding` (`0x00279568`), which also takes humans in state `0xe0000` or held flags
   `0x402000`. Both need a body, `Human_IsTargetable`, a height difference of at most 2 m, not friendly, another gang
   (brain `+0x20c`), not down or dead, held flag `0x40` and state `0x1c00000000` clear.
4. A car (`CarManager_FindTarget`, 1.0 m), then world objects within range, then glass panes within range, each
   inside the cone ([Targets](combat.md#targets)).
5. With no current target only: humans within range × 0.9 **at any angle** (`TargetFilter_AnyAngle` `0x002797b0`).
6. The cone widens to 2.356 rad (135°): world objects within range × 0.8, then humans within range × 0.7 **at any
   angle** (`TargetFilter_Wide` `0x002796a0` has no angle test; it skips state `0x100000000` instead of "down").
7. The nearest found wins. With nothing found, the previous target is kept when it is not a car, has a body and is
   within range.

**`Player_FindAttackTarget(h, id)`** (`0x0027a4b0`): one pass at the id's far range (not × 1.1) with
`TargetFilter_EnemyStanding` inside 54°, then objects and glass, then, with no current target, the any-angle and the
wide filters at the same range; nearest wins, else the previous target. Its aim heading is the stick's above 0.01,
else the facing turned toward the id's offset point (`Attack_GetOffsetHeading` `0x00254310`; the plain aim heading
when the id has no reach).

**The current target between attacks** comes from the stance logic each update ([The fight
stance](combat.md#fight-stance)): the nearest enemy within 6 m is tracked; one within **2 m** enters the stance and
becomes the target (and with the street's `CfgAutoLockAndCombat` the lock); it is swapped for the nearest within 2 m
once the target is beyond 3 m; the lock bit goes beyond 13 m; the locked walk drops a target beyond 2.5 m unless L1
(`+0x00` `0x8`) holds it. L1 pressed or held also picks one.

**Turning onto it**: an attack whose target is within its far range turns and slides onto it; beyond, it only turns
([Combat](combat.md#targets)). Which steer runs, and so how long the slide lasts and where it aims, is set out in
[The two steers](#two-steers) below. The turn does not face the target: it turns
so that the attack's **direction** points at it. The direction is the record's offset (`+0x0` / `+0x2`, read by
`AttackTable_GetOffset` `0x00254418`), a unit vector in thousandths in the attacker's frame (y forward; x right,
inferred from 25 and 27):
forward (0, 1000) or within a few degrees of it for the standing, moving and ground attacks (193: 7.6° right),
(999, −12) right for 25, (−1000, 0) left for 27, (−39, −999) back for 29, and for the strafes 31 / 32 / 33
3° / 9° left and 6° right of forward. The new heading is the goal's heading minus the direction's. Confirmed (code).

#### The two steers {#two-steers}

There are two steer functions, and the attack record's **flag `0x8`** (record `+0x160` + kind × 16 `+0xc`,
`AttackTable_GetFlags` `0x00254d60`) picks between them. Confirmed (code) unless marked:

| | `Attack_SteerToTarget` (`0x002761c8`) | `Attack_SteerLed` (`0x00275678`) |
| --- | --- | --- |
| Used by | `Attack_Start` and `Attack_StartSnap` for a kind **without** flag `0x8`; always by `Attack_StartAtTarget` (`0x00261c80`, the specials from `Player_Special`), `Attack_StartSolo` and the other callers | `Attack_Start` (`0x002625a8`), `Attack_StartSnap` (`0x00264460`), `Attack_StartGrounded` and `Revive_Start` for a kind **with** flag `0x8`, when the word `0x005102c4` is non-zero |
| Slide time | the caller's time, cut to the first event + 0.1 s. `Attack_Start`, `Attack_StartAtTarget` and `Attack_StartSolo` pass the time to the first event (`0x00101658`), so the slide lasts **the time to the first event**; `Attack_StartSnap` passes 0.1 s | always **the time to the first event + 0.1 s**; the caller's time is not read |
| Target point | the target's position (its task `+0x10`) | the target's **slot point 0** (`Human_GetLedSlotPoint(target, 0)`, `0x00226aa0`) |
| Lead, reach, turn | the same in both: the lead by the target's velocity × (first event + 0.1 s), cut to 0.5 m; the reach along the line in x and y; the goal at the attacker's own height | |

`0x005102c4` is 1 on the disc and only read by these two callers (no write was found), so in practice flag `0x8`
alone decides (inferred). So **653** (flags `0x26`) and **645** (`0x23`), both started by `Player_Special` through
`Attack_StartAtTarget`, slide for their first event's time, while **`X1`** (`0x09`) slides for the first event +
0.1 s. Neither the special's own `T` nor the event's type makes the difference; it is the steer. The snaps 25, 27
and 29 (`0x1b` / `0x19` / `0x1b`) also have flag `0x8`, so they take the led steer too, and **not** the 0.1 s that
`Attack_StartSnap` passes to the other one.

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

| Pose (clip) | x (right) | y (forward) | xy length | z |
| --- | --- | --- | --- | --- |
| idle 388 / 396 (target, brain off) | −0.008 to −0.003 | 0.012 to 0.037 | **0.013-0.037** | 1.728-1.734 |
| fight idle 358 (the player, looping from update 88) | 0.024 to 0.051 | 0.075 to 0.165 | **0.09-0.165** | 1.540-1.558 |
| reel 275 after `X1` (updates 52-63) | 0.083 to 0.273 | 0.031 to 0.137 | **0.15-0.28** | 1.469-1.601 |
| reel 273 after `XX2` (updates 65-76) | −0.228 to 0.070 | 0.029 to 0.141 | **0.10-0.23** | 1.530-1.600 |
| recovery 389 (updates 78-96) | −0.028 to 0.079 | 0.020 to 0.140 | 0.02-0.16 | 1.61-1.73 |

So the original's head really moves that far: in a reel it swings up to 0.28 m, mostly sideways, and drops by up to
0.26 m; in the fight idle the head leans 0.08-0.17 m forward. `XX2` started at update 54, two updates into the reel
275, and its steer aimed at (0.215, 0.132): **0.25 m** from the target's position, to his right. The fight idle was
measured on the player, whose model shares the humans' skeleton (inferred); an AI target held in 358 was not
recorded.

#### Where the attacker stands at contact {#reach}

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
the transform):

| Move, start | Distance after k1 / k2 / k3 | Then | Contact | Distance at contact (before / after the update) |
| --- | --- | --- | --- | --- |
| 653 from 1.0 m | 1.216 / 1.513 / **1.617** (slides back 0.62 m) | the clip's root motion: 1.455, 1.281 | **k7** | 1.172 / 1.120 |
| 653 from 1.6 m | 1.591 / 1.663 / **1.617** | the same | **k7** | 1.172 / 1.120 |
| 653 from 1.9 m | 1.779 / 1.738 / **1.617** | the same | **k7** | 1.172 / 1.120 |
| `S1` from 1.5 m | 1.319 / 1.176 (no back slide; reach 1.03) | | **k3** | 1.176 / 1.034 |
| `X1` from 1.0 / 1.3 / 1.6 m | a straight slide from k3 to k11 (below) | ends at 0.878-0.879 m | **k10** | 0.903 / 0.890, 0.976 / 0.930, 1.048 / 0.970 |
| `X1` from 1.9 m | turns only (the far range is 1.90 m): 1.886 at k3, 1.747 at k10 | | **miss** | |

From k3 on, the three 653 runs are the same to the millimetre: the steer ends at 1.62 m (the reach 1.58 m and
0.04 m more, not explained: 653 takes the steer that aims at the target's position, not its head, [The two
steers](#two-steers)) by k3, and the clip then carries the attacker 0.50 m forward to 1.12 m by the end of k7. The
shapes are on k2-k8 (653) and k1-k9 (`S1`, from the hook).

**Bones at contact**, in the attacker's frame (forward, right, height above the attacker's feet; metres). The pose
before the contact update (k6's) and after it (k7's) bracket the test; the hands reach the target only in k7's
pose:

| Run | Pose | Left hand (19) | Right hand (25) | Target spine (3) | Target head (6) |
| --- | --- | --- | --- | --- | --- |
| 653, any start | after k6 | 0.78, −0.40, 1.21 | 0.78, +0.23, 1.22 | 1.22, −0.01, 1.18 | 1.16, −0.01, 1.74 |
| 653, any start | after k7 | 0.99, −0.19, 1.28 | 0.99, +0.14, 1.25 | 1.17, −0.01, 1.18 | 1.11, −0.01, 1.74 |
| `S1` from 1.5 m | after k2 | 0.73, +0.03, 1.61 | −0.03, +0.39, 1.32 | 1.21, +0.02, 1.18 | 1.15, +0.03, 1.73 |
| `S1` from 1.5 m | after k3 | 0.73, +0.02, 1.60 | −0.06, +0.37, 1.32 | 1.07, +0.02, 1.18 | 1.01, +0.03, 1.73 |

After k7 of 653 the hands are 0.24-0.28 m from the target's spine bone (0.51-0.52 m from the head); after k6, 0.50-
0.58 m. In world space for the 1.9 m run (attacker at (33.391, 37.943), heading 179°): left hand (33.537, 36.949,
1.500), right hand (33.204, 36.960, 1.471), spine (33.345, 36.778, 1.403), head (33.342, 36.831, 1.959). `S1`'s
left hand is 0.30 m from the head after k3.

**`X1` against a standing target** (confirmed (runtime), PCSX2 2026-10-07, slot 6, the same still PoizoCiv; cross
tapped at k0, clip 11 from k2; the strike shapes read from the bodies themselves, as below). Two rules of the test
decide where `X1` lands, and they hold for every move:

- **The test is swept** (confirmed (code), `Human_TestStrikes` `0x0033f110`): each enabled shape is tested along its
  move since the last update, from its previous posed point to the new one (shape `+0x20` to `+0x50` for a sphere),
  not only where it ends. A shape is posed only while it is on, so its first posed update has no sweep (previous =
  current). `X1`'s right hand never overlaps the target where it ends: at the contact the static gap is +0.013 m
  from a 1.0 m start and +0.040 m from 1.6 m; only the sweep across the target's front touches.
- **Contact and samples**: the contact update k10 is the one whose result is sample k10: the target's health first
  drops in that sample (also in the `S1` and 653 runs above), and the shapes that overlap are the ones posed in it,
  the hand sweeping from its k9 point to its k10 point. (These runs were recorded before the recorder fix, which
  labelled a `strike-contact` or `strike-shape` call one update early; the contact here is re-labelled one update
  later, [Recording a trace](../guides/research-workflow.md#hooks).)

**The slide** is two motions added together, confirmed (runtime) with the steer's own fields (`moves_reachX1_10_steer`,
`moves_reachX1_16_steer`):

1. **The steer** (`Attack_SteerToTarget`, then `Human_MoveToOver` `0x0023d2b8`, which keeps the goal at human
   `+0x310`, the velocity at `+0x2e0`, the time left at `+0x300` and an on flag at `+0x331`). On X1's first update
   (k2) the goal is set **1.038-1.048 m** from the target's transform position, at the target's height: the reach
   1.02 m from the target's head ([The two steers](#two-steers)), which leans 0.02-0.03 m toward the attacker. The attacker
   slides to it at a **constant velocity over 0.308 s** (9.25 updates, k3-k11; the time to the clip's first event
   plus 0.1 s): 1.824 m/s inward from 1.6 m, and **0.157 m/s backward** from 1.0 m (the goal is outside him).
2. **The clip's own root motion**, bone-cache entry 0 (`0x006b6880` + index × `0x470`, its second word the forward
   speed in m/s): 0.42, 0.59, 0.62, 0.63, 0.57, 0.58, 0.70, 0.60, 0.47, 0.22 m/s over k3-k12, about **0.18 m
   forward** in all, then about 0 (its first word, up to −1.17, is not a sideways speed: the attacker does not drift;
   inferred, a turn).

So the attacker ends at the goal less the clip's 0.18 m: **0.870-0.880 m** from the target at k12 whatever the start
(0.879 from 1.3 m), and the target point is the target's own position, not a bone. From 1.0 m the steer moves him
back while the clip carries him forward faster, so he still closes in (0.013-0.017 m per update). At the contact the
attacker is **still moving in**: 0.890 m (from 1.0 m), 0.930 m (from 1.3 m), 0.970 m (from 1.6 m) after the
contact update, never 1.02 m.

**The hand and the target at contact.** In the attacker's frame (forward, right, up from his feet, metres; samples
after each update; the attacker's scale 0.97 makes the hand sphere's radius 0.087). The hand's path is the same to
the millimetre from every start; the target's shapes sit at the distance d:

| Sample | Right hand sphere (25), r 0.087 | Target spine segment (3), r 0.18 | Target head (6), r 0.15 |
| --- | --- | --- | --- |
| k8 (first posed) | 0.009, +0.459, 1.627 | (d − 0.013, −0.03, 1.112) to (d + 0.003, −0.03, 1.492) | d − 0.06, 0.00, 1.781 |
| k9 | 0.732, +0.233, 1.668 | the same | the same |
| k10 (contact) | 0.823, −0.223, 1.536 | (d − 0.013, +0.04, 1.112) to (d + 0.003, +0.04, 1.492) | d − 0.06, +0.07, 1.781 |
| k11 | 0.647, −0.592, 1.374 | (target recoiling) | |
| k12 | 0.389, −0.707, 1.172 | | |

The hand comes in at shoulder height and crosses from the attacker's right to his left in front of the target, a
hook; between k9 and k10 it passes the top of the spine segment (1.49 m) where it meets the right-to-left line,
0.78 m ahead at 1.60 m high. With the attacker standing still through that update, the sweep touches the spine only
when the distance after the update is at most **1.019 m** (the head at 1.00 m); an attacker still moving forward
carries the start of the sweep back with him (the sweep is in world space), which costs reach: the 1.6 m run, 0.078 m
forward in that update, hit with 0.022 m to spare at 0.970 m. Measured swept gaps (spine / head) at k10: −0.105 /
−0.059 (from 1.0 m), −0.064 / −0.028 (1.3 m), −0.022 / +0.013 (1.6 m). From 1.9 m, with no slide, the hand stays
0.68 m short.

So a `X1` stance of 1.02 m with a static overlap test misses by a few centimetres on two counts: the original's
attacker is 0.89-0.97 m away when the hand crosses (and 0.88 m when the slide ends), and its hand is tested along
its sweep.

**A walking target** changes this through the lead. PoizoCiv walking in at 1.63 m/s (its AI on, placed until update
9): the 653 steer stopped near 1.7 m by k3 and contact came at k7 from 1.0 and 1.6 m and **k8** from 1.9 m (the
shapes' last live update), at 1.15-1.21 m. Pinned in place but still in its walk (velocity 1.6 m/s toward the
attacker, position written every update), the lead put the attacker 1.9-1.95 m away at k3 and 653 **missed** from
1.0 and 1.9 m, as did `S1` from 1.5 m. So runs against a pinned walking target, as some earlier ones on this page
were, understate what reaches (inferred; the right snap's miss may be this).

Scenarios: `moves_reach653_10`, `moves_reach653_16`, `moves_reach653_19`, `moves_reachS1_15`, `moves_reachX1_10`,
`moves_reachX1_13`, `moves_reachX1_16`, `moves_reachX1_19` (still target; the `X1` ones but the last also read the
bodies' shapes), `moves_reachX1_10_steer`, `moves_reachX1_16_steer` (the steer's goal and the clip's root) and
`moves_reach653_19_walk` (walking).

### Square, in order {#square}

`Player_Square` (`0x00286cc8`), unarmed (held object set 0). Confirmed (code). Refused while the state word has any
of `0x7bf9e9f4300` or record `+0x08` any of `0x100101f`; the buffered chain press and combo count are cleared first.

1. **Not in a fight stance** (state bits `0x3`) and, for a pad player, `0x0051031c` clear:
    - running freely and wanting to run (gait 4, `+0x08` clear, stick above 0.95): a low target (the
      `0x00225200` test) → enter the stance and **193**; otherwise **24** (`Player_StartCharge`, `0x00264a80`);
    - gait 1-3 with the stick at 0.12 or more: the same low-target test → 193, otherwise **23**;
    - `0x00510270` set: **22** (`Attack_StartSolo`).
    - Otherwise the player enters the stance and goes on.
2. **Strafe attack** (pad player, locked on, speed at least the walk speed, stick above 0.95, a current target, and
   the stick within 135° of the facing): **33** within 45° of the facing, else **31** when the stick is to the
   right (angle < 0), else **32**; only when the target is within that attack's far range ([Strafe
   attacks](#strafe)). Otherwise on.
3. **Snap** (stick above 0.95, more than 45° off the facing, a snap target): 25 / 27 / 29
   ([Combat](combat.md#attacks)).
4. Re-pick the target (`Player_PickTarget(2.0)`, [Targets](#targeting)) when a pad player has none, or it is
   farther than **3 m** (`0x005104c0` = 9, squared). No human but an
   object → `Player_ObjectAttack` ([Combat](combat.md#breakables)).
5. **Low target** (`Human_IsHighOrBusy`, `0x00225200`): the attacker's point stands more than 1.3 and less than
   1.8 m above the target's (`Object_HeightDiff` `0x00229a10`: attacker's z minus target's, of the point each
   object's vtable `+0xac` gives); otherwise, unless the target is more than 0.7 m higher, the target's state: in a
   held phase of `0xc08000`, before 0.2 of its clip (when it holds `0x8000`), in 195 with `0x400000`, or with state
   `0x4000000000`; outside those phases, state `0x40100f0800` without the recovery `0x40000`. "Down" is inferred
   → **193**.
6. **Mid target** (`Human_IsMidOrBusy`, `0x002250a0`): more than 0.7 and at most 1.3 m above it; otherwise, unless
   the target is more than 0.7 m higher, the target in state bits `0x2800c000400` (mounting `0x400` among them)
   without the held flags `0xc48000`, or between 0.2 and 0.4 of a clip holding `0x48000`, or holding `0x4000` →
   **212** `MOUNTING_STRIKE` through `Attack_StartAtTarget` (`0x00261c80`).
7. **Grabbed from the front or grabbing someone from the front** → **120** `GRAB_FRONT_STRIKE_01`.
8. **Tandem** possible (`Tandem_CanStart`) → the tandem ([Tandems](#tandem)).
9. Pad player only: the target is **grabbing or tackling him** and can be countered → **76** or **9**
   ([Counters](#counters)).
10. Otherwise **12 `S1`**.

**Armed** (sets 1-3): not in a stance and running freely → **501** (constant `0x1f5`); otherwise the stance; a low
or mid target (not stealing a stereo or picking a lock) → the set's grounded slot (record `+0x74`, slot `0x13`);
a tandem; the object attack; otherwise the swing slot `0x10`. No snaps, strafes, 120 or counters. Confirmed (code).

### Cross, in order {#cross}

`Player_Cross` (`0x00287a18`) mirrors square with three differences. Confirmed (code):

- the run attack 24 also starts at a **sprint** (`Human_IsSprintingFree`), and the walk attack's low-target test
  gives 193;
- **no strafes and no snaps**;
- in the stance a **low target gets 194** `GROUNDED_STRIKE_02` (the stomp), a **mid target 661**
  `SPECIAL_BREAK_OBJECT_LOW` (the `carhit_low` kick) through `Attack_StartAtTarget` (`0x00261c80`), a front grab 120,
  then the tandem, the counters 76 / 9 and **11 `X1`**.

Armed: as square's branch with slot `0x11`, combo count `+0xbc` = 2 before the swing (so cross never chains).

### Strafe attacks {#strafe}

The fight-walk attacks: L1 held on a target, the stick pushed fully (above 0.95) to a side or toward the target
while the player already moves at walking speed, then square. Not on cross. Confirmed (code) at `0x00286cc8`;
confirmed (runtime):

| Scenario | Stick | Clip | Speed | Strike shapes | Result |
| --- | --- | --- | --- | --- | --- |
| `moves_strafe_right` | full, 90° right, 10 updates, the target pinned 1.2 m ahead | **31** `STRAFE_RIGHT` | 3.43 m/s before square; locked, the player turns about 7.9° an update to keep facing the target | L forearm / hand k8-k12 | 30 damage at k9, reaction 275 |
| `moves_strafe_left` | full, 90° left | **32** `STRAFE_LEFT` | 3.43 m/s | L forearm / hand k8-k12 | 30 damage at k9, reaction 273 |
| `moves_strafe_front` | full, at the target 2.6 m ahead, 6 updates | **33** `STRAFE_FRONT` | lunge at 3.98 m/s | L forearm / hand k5-k11 | out of reach (far range 3.0 m) |

With the target at 1.2 m the forward strafe did not play: the player had slowed against the target and was below
walking speed, so square gave `S1`. The strafe values are the file's (the class table writes no strafe index): 30
damage, hit code `0x0a`.

### Counters from square and cross {#counters}

Besides R1 at the catch ([Combat](combat.md#grabbed)), **square or cross** pressed while an enemy is coming in for
a grab or tackle counters it. `Human_CanCounterGrab` (`0x00258e88`): the attacker plays 69-71 (grab intro or miss),
faces the player and targets him. `Human_CanCounterTackle` (`0x002590f8`): the attacker plays 2-4. Then
`Attack_StartPaired(player, attacker, 76 or 9, 1, 0x400000)`. Pad players only; the AI's equivalent is command 3
(`Player_TryCounterGrab`, `0x0027d6e0`) with the same tests. Confirmed (code).

At runtime (`moves_grabcounter_12` and `_14`): the puppet civilian 1 m ahead was given command `0xd` at frame 10 and
played its grab intro 70; square at frame 12 or 14 (during the intro) played **76**, the grabber 77; the grabber lost
**100** health on the first update and the player's power went 400 → 300. When the enemy started grabbing, the player
showed 359 for one update. Confirmed (runtime).

### Grounded and mid strikes {#ground}

| Input | Target | Clip | Runtime (`moves_grounded_*`, victim knocked down by 653 and set 1 m ahead) |
| --- | --- | --- | --- |
| square | low (down) | **193** `gen_ground_kickB` | foot B shape k11-k13, contact k12, −34 health seen in its sample, reaction 195 |
| cross | low (down) | **194** `gen_ground_stomp` | foot B shape k11-k17, contact k15, −34 seen in its sample, 32 updates, then 358 |
| square | mid (the mount's states, or 0.7-1.3 m lower) | **212** `MOUNTING_STRIKE` | `moves_mid_square`: at Bum01 mounting PoizoCiv (made the target), contact on the mounter at k8; the victim was let go (245, then up with 199) |
| cross | mid | **661** `SPECIAL_BREAK_OBJECT_LOW` | `moves_mid_cross`: the same, 661 one update after the release, contact at k9, the victim let go |
| square or cross | grabbed from the front by someone else, or grabbing someone from the front | **120** | `moves_grab_front_strike`: at PoizoCiv holding Bum01 from the front (82), contact at k9, 50 damage, PoizoCiv 144 and the hold broken |
| commands `0x37` / `0x38` (scripts, AI) | any | 193 / 194 (`Attack_StartGroundStrike`, `0x00287fe0`) | not run |

Both grounded strikes deal 34 (class indices 12 / 13). Confirmed (code); confirmed (runtime) for 193, 194, 212, 661
and 120. A plain `S1` at a tackled victim lying under its mounter (PoizoCiv in 207) did not touch it.

### Tandems {#tandem}

A **tandem** is a three-person move: a player strikes a victim another human holds from behind. Confirmed (code) at
`Tandem_CanStart` (`0x00258e10`), `Tandem_Start` (`0x0026f860`), `Tandem_LinkThree` (`0x0022b1a0`),
`Tandem_AlignThree` (`0x00277458`) and `Tandem_OnVictimIntroEnd` (`0x0026f780`):

- **Started by the player only**, from square, cross, circle tapped or circle held, when the target is held from
  the rear (by anyone: a gang member or another player), the attacker stands within 45° of the victim's front, and
  the victim's held flags have none of `0xc12200`. Square and cross test it after the target-state strikes; circle
  tests it before grabbing ([Combat](combat.md#grab), step 3).
- **The set**: a bat or baton in hand → 181-186; a knife → 187-192; otherwise 163-168, 169-174 or 175-180 at random.
  Each set has an intro and an end for the grabber, victim and attacker (ids +0 to +5).
- The three are linked and placed by the set's pair offsets. At the **end of the victim's intro** the attacker's
  intro and end values (50 + 350, or 50 + 500 with a bat, 50 + 600 with a knife) go on the victim as pending
  damage; it is lethal when `Human_IsDamageDefeating` (`0x00265c28`: health ≤ damage, the damage tripled when
  cuffed) says so, and then the death reaction plays (`Human_PlayDeathReaction`).
- The grabber is let go into a fight stance afterwards. A player's tandem scores through `Grab_ScoreMove` (100
  points of category 6).

At runtime (`moves_tandem`: Ash, the player's ally, given the grab command at PoizoCiv from behind; PoizoCiv held
in 85): square played the set 1 on the next update, the player's 165 and the victim's 164 for **51** updates (not
the 36 of their clips), then 168 / 167; the victim lost **400** (50 + 350) on the first update of 167. Confirmed
(runtime).

### Stealth: hiding and the stealth kill {#stealth}

**Hiding** has no button: a player on shadow ground (collision triangle flag `0x10`) whom nobody hunts enters state
`0x200000` and move style `0x14` (`Human_EnterShadow`, `0x0022ff88`); off the shadow he leaves it at once, or after
4 s when he walks with a target (`Human_LeaveShadow`, `0x002300c0`). Hidden, the idle is 630 `STEALTH_IDLE` and the
walk 633 `STEALTH_WALK` (about 2.31 m/s); the brain flag `+0x2d4` is set and his Warriors are told to hide. The
rules, the guards' sight and the HUD cue: [Stealth](stealth.md). Confirmed (code).

**The stealth kill** (`Player_UpdateActionsHidden`, `0x0027e040`): hidden, with **L1 held** (state bit `0x8`, the L1
target lock; its release sets `0x20000000`), square, cross or circle starts `Player_StartStealthKill`
(`0x00264738`) when:

- the player is **behind** the target (side 2) and the target faces away;
- the target can be grabbed by him (`Human_CanBeGrabbedBy`) and the way is clear;
- the power meter is above 0.25 of its maximum; the kill spends 0.25.

The clip is 637 bare-handed, 639 with a knife, 641 with a baton or club (all `player_stealthkill_weak` in
Rembrandt's set); the victim's flag `0x8` is cleared and the pair starts through `Attack_StartPaired`. It deals
**3000**, knocking the victim out (638 / 640 / 642), and scores 100 points (about 144 rage). With a set 5 object
held the player drops it and does 637 when within 1.5 m. **The AI never stealth kills.** Confirmed (code).
The stealth ready blend is `Human_SetStealthReadyBlend` (`0x0025eb00`).

At runtime (`moves_stealth`: the hidden state written onto the player each update, PoizoCiv pinned 1.0 m ahead
facing away): L1 played 634 and then 630; square on the next update started 637 with the victim in 638, the
victim's health fell to **1** at once (knocked out, state `0x80000000`), power 400 → 300; 637 ran 52 updates, then
the fight idle 358. Confirmed (runtime).

### The mounted victim {#mounted-victim}

`Player_UpdateTackled` (`0x0027f1a0`) runs for a mounted player (state `0x800`), after the gate `0x5c7eee0`.
Confirmed (code); confirmed (runtime) below:

| Input | Effect |
| --- | --- |
| square or cross (`0xf`, `0x11`, `0x12`, `0x10`) | while `Grab_CanStruggle` (the victim not hurt, the mounter not raging, the victim's power above max / 6) and the victim's power fraction is above 1 / the mounter's class divisor: the **mounter** loses 1 / the victim's divisor of its power; then, unless the victim's record `+0x08` has any of `0x5c7eae1`, **250** `MOUNT_STRUGGLE` (`Mount_Strike`). A press during 250 drains the mounter without a new clip |
| circle (`0x1e`, `0xd`, `0xe`) | with the victim's human flag `0x2000000000` clear and `Grab_CanEscape(mounter)`: `Mount_GetOff` (`0x00271470`), which plays **246** `MOUNT_BREAK` / 247 |
| R1 (command 3, `0x00510988`) or `0x19` | with the mounter's flag `0x40` and the victim's `0x100000000` clear and `Grab_CanEscape(mounter)`: the **reversal** (`Mount_StartReversal`, `0x002723e8`): 242 `MOUNT_REVERSAL` / 243, then the roles swap into 210 / 207 with the victim on top; statistics event 4 |

`Grab_CanEscape` (`0x00225830`: at or below a quarter of the mounter's maximum power, half
when hurt, always; above it, a 1 in floor(power / that) chance per press; never while the mounter rages) makes the
mounter's power the gate: struggle first, then circle or R1. The AI does the same through its commands.

At runtime (`moves_mounted_square`, `_r1`, `_circle`; the puppet civilian at power 400 given the tackle command
`0xe` 1.5 m away): its intro 3 ran 19 updates, then the player played 6 for 46 updates (every press refused) under
the puppet's 5, then the mounted idles 207 (victim) and 210 (mounter). Square played 250 on the next update for 16
updates, the mounter 251, and the mounter lost **40** health on 250's second update. Squares every 4 updates (two
250s, the presses between them draining) ended in 246 / 247 (48 updates) 2 updates after the 6th press, and the
player was free. After one 250, R1 every 3 updates gave the reversal 242 / 243 on the 3rd press (57 updates, then
210 / 207 with the player on top); circle every 3 updates gave 246 / 247 on the 6th press.

### Weapons {#weapons}

The armed moves' choice of clip is on [Combat](combat.md#armed-moves). Added here, confirmed (code):

- **Armed chain**: square repeats the set's two swings, 34 → 35 → 34 (bat), 39 → 40 (baton), 45 → 46 (knife), for
  as long as presses come in the window; cross's combo count 2 means it never chains.
- **Knife and bottle kills** in a grab or mount: with a knife (set 1) square or cross plays 484 (rear hold), 486
  (front hold) or 488 (mount); a broken bottle (type 43) plays 492 / 494 / 496. Each needs more than 0.25 power and
  spends 0.25. The damage lands at the start: 600 (knife) or 40 (bottle) × the power class's first float, doubled
  with flag `0x4000`; a lethal hit plays a death reaction. 483 `KNIFE_VICTIM_DIE` is never played.
- **Wear**: the clip's `use` event (`0x41`) calls `WorldObject_TakeHit(held, -1)`, which takes 1 from the object's
  hit counter (`+0x10e` first for objects with flag `0x4000000`, then `+0x10d`); at 0 the weapon breaks
  (`WorldObject_Break`, `0x00393e20`). The counters start from `CfgObj` bytes `+0x5a` / `+0x5b`; 0 (or −1) means it
  never wears. The ordinary swings have no `use` event, which is why 12 bat hits never broke a bat
  ([Combat](combat.md#bat)).
- **Thrown objects** (sets 4-6, `Player_CrossWithWeapon`, `0x002880d8`): set 4 throws 505 / 506 / 507 at a target
  searched within 10 m; set 5 throws 467 / 471 / 472 within 20 m, after trying the smash 473 / 475 on a human in
  reach; set 6 throws 551 / 552 / 553. A thrown object's hit deals its `CfgObj` `+0x58`
  (`ThrownObject_HitHuman`, `0x00392b88`), not the clip's damage.
- **Throwing a melee weapon**: commands `0x22` and `0x39` with a knife or baton play 491 `KNIFE_THROW`, with a bat
  502 `SWINGABLE_OBJECT_THROW`, and 471 / 472 when moving (`Player_ArmedSpecial`, `0x00288838`, then
  `Player_ThrowMeleeWeapon`, `0x00288698`). So cross + square with a weapon throws it instead of the special. The
  throw is refused when the target (the counter target, else the nearest within 20 m, `0x0027ad80`) is within
  **2.0 m**, and with human flag `0x4000000000`; the command then does nothing.
- **Blocks against weapons** (`Human_BlockHit`, `0x00269f30`): the bat's 26-34 range (its first swing 34 included)
  is never blocked; an unarmed blocker never blocks an armed hit; a set 3 (bat) blocker blocks armed hits, and the
  bat's cross 36 against a set 3 front block makes the attacker play 629 `BAT_BLOCK_REACT`; other armed blockers
  block only while in their block clips 605-620. The armed block clips 621-624 come from slots `0x15`-`0x18`
  (sets 2 and 3).

At runtime (confirmed (runtime); the object put in the player's hand with `Human_PlaceItemInHand` `0x00238540`,
which takes the human's **handle**, called on the game's thread; PoizoCiv facing the player):

| Scenario | Object, target | Input | Result |
| --- | --- | --- | --- |
| `moves_knife_grab` | `dyn_tknife`, 1.0 m | circle, then square in the hold | the grab ended in the rear hold (74, then 84); square played **484** on the next update with the victim in 485, its health at **1** at once (knocked out), power 387 → 287; 484 ran 76 updates |
| `moves_knife_throw` | `dyn_tknife`, 5.0 m (pinned) | cross held, square | **491** on the `0x22` update; the knife left the hand at k24 and hit 5 updates later for **200**, reaction 280. With the target at 2.5 m and walking in, the throw was refused and cross's release played the knife's `X1` 47, which dealt 259 |
| `moves_bottle_cross` | `dyn_beerbottle`, 0.7 m | cross | **475** at once (the smash, from behind by the side test) with the victim in 476: **155** damage and power 400 → 300 on its first update; the bottle was swapped for another object (the broken bottle) at k23 |
| `moves_bottle_kill` | the broken bottle the smash left, the same victim once up | circle, then square in the front hold (82) | square in the hold played **494** on the next update with the victim in 495, power −100, **51** damage on its second update (not lethal at 445), 43 updates. Square outside a hold with the broken bottle played the knife's swing 45 |
| `moves_bottle_throw` | `dyn_beerbottle`, 4.0 m | cross | 466 `ONE_HANDED_OBJECT_THROW_START` for 7 updates, then **467**; the bottle left the hand at k7 of 467 and hit 2 updates later for **60**, reaction 280 |

### Commands only scripts and the AI send {#ai-commands}

| Command | Handler | Move |
| --- | --- | --- |
| 3 | `Player_TryCounterGrab` (`0x0027d6e0`) | the AI's counter: 76 against a grab intro, 9 against a tackle intro ([Counters](#counters)) |
| `0x36` | `Player_OnCommand36` (`0x0027de78`) | the push 21 `ATTACK_PUSH` through `Attack_StartAtTarget` (`0x00261c80`), holding flag `0x10` |
| `0x37` / `0x38` | `Attack_StartGroundStrike` (`0x00287fe0`) | 193 / 194 at the target |
| `0x39` | `Player_ArmedSpecial` | throw the melee weapon (as `0x22`) |
| `0x33` | `Player_OnCommand33` (`0x00281188`) | the pick-up search |
| `0x32` | `Player_OnCommand32` (`0x002832c8`) | uncuff with a key |
| `0x27` | `Player_TryRevive` (`0x00286ba8`) | revive a downed Warrior with a flash (665, `use` at k24) |
| 5 (as L2 held) | `Player_UpdateGrabbing` / `Player_UpdateMounting` (`PlayerCmd_IsLetGo` `0x0027bf08`) | in a grab, let go (95 / 94); in the mount, get off (244 / 245, the victim rises with 199). The Grabbing and Mounting reaction goals send it every update near a train ([AI](ai.md#fight-reactions)) |
| `0x19` | `Player_UpdateGrabbing` (`PlayerCmd_IsGrabSpin` `0x0027bd98`, as R1 pressed) | spin the grab: front to rear 78 / 79, rear to front 80 / 81 ([Combat](combat.md#grabbing)). The Grabbing goal sends it to turn a front-held victim to his attackers |

Confirmed (code).

### Runtime results {#runtime}

Method: PCSX2 2.9.94, copies of slot 6 (the street, the player Rembrandt facing 179.029°, the camera at 145.398°),
the puppet civilian `PoizoCiv` (600 health) placed by writing its transform each update until the move, the player
at 900 health and 400 power. The stick is pushed through the scripted pad at full deflection where the move needs
more than 0.95 (snaps, strafes) and is otherwise centred. k counts from the clip's first update.

| Scenario | Clip | Strike shapes | Contact | Damage | Reaction |
| --- | --- | --- | --- | --- | --- |
| `moves_snap_left` | 27 | L forearm / hand k2-k7 | k4 (health seen k4) | 31 | 283 |
| `moves_snap_back` | 29 | R forearm / hand k1-k7 | k4 (health seen k4) | 31 | 281 |
| `moves_snap_right` | 25 | R forearm / hand k2-k7 | none: with the target pinned 0.75 or 1.0 m to the right the player slid 0.3-0.5 m away from it (to 1.26 m) | | |
| (any walk attack seen) | 23 | L forearm / hand k5-k11 | | | |
| `moves_grounded_square` setup: cross + square | 653 | | k7 | 6 | 289 / 291, thrown about 3.7 m; power 400 → 300 |
| `moves_grounded_square` | 193 | foot B k11-k13 | k12 | 34 | 195 |
| `moves_grounded_cross` | 194 | foot B k11-k17 | k15 | 34 | 358 after 32 updates |
| `moves_strafe_left` | 32 | L forearm / hand k8-k12 | k9 | 30 | 273 |
| `moves_strafe_right` | 31 | L forearm / hand k8-k12 | k9 | 30 | 275 |
| `moves_block_window` | 12, R1 pressed at k10 | | k5 (target 1.45 m away at the press) | 17 | the block 606 replaces 12 at k15 (its end phase), 5 updates before its end |
| `moves_block_end` | 12, R1 pressed at k16 | | none (target at 1.03 m) | | 606 starts at k20, when 12 ends: no cut in recovery |
| `moves_strafe_front` | 33 | L forearm / hand k5-k11 | none (2.6 m) | | |
| `moves_grabcounter_12`, `_14` | 76 (grabber 77) | all | k0 | 100 | 77 |

The shape windows and contacts here are re-labelled one update later after the recorder fix: these runs were
recorded when `pcsx2 record` labelled every `strike-shape` and `strike-contact` call one update early
([Recording a trace](../guides/research-workflow.md#hooks)); each contact now falls in the update whose sample first
shows the damage. Re-labelled, every measured window matches the frame tables below to the update ([Reading the
tables](#reading)), including the late ties: 25 / 27 / 29 off (frame 5), 23 / 33 off (frame 8) and 193 / 194 on
(frame 8).

### Frame data {#frame-data}

Generated from the disc's clips and records (Rembrandt). Ids with the filler clip `missing_anim_filler` (61, 229)
are never seen. Standing attacks and their chains ([Combat](combat.md#attacks)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 12 | `ATTACK_S1` | `combo1a_S_hi_b` | 20 (16 f × 0.8) | L hand 1-10 | eblk 0, W 6, E 15, R 16 | 0.94 / 1.80 | 40 → 17 | `0x0a` |
| 16 | `ATTACK_SS2` | `combo1a_SS_hi_b` | 25 (20 f × 0.8) | L forearm, L hand 2-7 | eblk 1, W 6, E 19, R 20 | 1.09 / 1.80 | 40 → 36 | `0x0b` |
| 19 | `ATTACK_SSS3` | `combo1a_SSS_lo_b` | 27 (21 f × 0.8) | R hand 7-15 | eblk 4, E 16, R 17 | 1.14 / 1.70 | 50 → 53 | `0x26` stun |
| 20 | `ATTACK_SSS3_HOLD` | `combo1a_SSSH_lo_b` | 33 (26 f × 0.8) | shin A, foot A 15-21 | eblk 12, E 24, R 25 | 1.13 / 1.90 | 55 → 53 | `0x26` |
| 17 | `ATTACK_SSX3` | `combo1a_SSX_lo_b` | 34 (27 f × 0.8) | shin B, foot B 5-12 | eblk 6, E 24, R 25 | 1.47 / 1.70 | 60 → 61 | `0x25` |
| 11 | `ATTACK_X1` | `combo1b_X_hi_r` | 30 (24 f × 0.8) | R hand 6-11 | duck 4, W 10, E 20, R 21 | 1.02 / 1.90 | 45 → 26 | `0x09` |
| 13 | `ATTACK_XX2` | `combo1b_XX_hi_l` | 30 (24 f × 0.8) | R forearm, R hand 9-14 | duck 6, E 17, R 19 | 1.12 / 1.90 | 60 → 53 | `0x1b` |
| 15 | `ATTACK_SX2` | `combo1a_SX_hi_b` | 20 (16 f × 0.8) | R hand 6-11 | eblk 5, E 14, R 15 | 1.37 / 1.90 | 50 → 44 | `0x1a` stun |
| 14 | `ATTACK_XS2` | `combo1b_XS_hi_l` | 30 (24 f × 0.8) | L hand 7-12 | duck 5, E 22, R 24 | 0.88 / 1.90 | 50 → 44 | `0x2b` |
| 18 | `ATTACK_SSX3_HOLD` | `combo1a_SSSH_lo_b` | 33 (26 f × 0.8) | shin A, foot A 15-21 | eblk 12, E 24, R 25 | 1.13 / 1.90 | 65 → 61 | `0x26` |
| 25 | `SNAP_RIGHT_01` | `gen_snap_right1` | 16 (12 f × 0.75) | R forearm, R hand 2-7 | E 8 | 0.95 / 1.30 | 10 → 31 | `0x1b` stun |
| 27 | `SNAP_LEFT_01` | `gen_snap_left1` | 16 (12 f × 0.75) | L forearm, L hand 2-7 | E 8 | 0.82 / 1.30 | 10 → 31 | `0x19` stun |
| 29 | `SNAP_BACK_01` | `gen_snap_back1` | 16 (12 f × 0.75) | R forearm, R hand 1-7 | E 8 | 0.96 / 1.30 | 10 → 31 | `0x1b` stun |
| 31 | `STRAFE_RIGHT` | `player_combat_walk_right_punc` | 30 (22 f × 0.75) | L forearm, L hand 8-12 | eblk 7 | 0.89 / 1.40 | 30 | `0x0a` |
| 32 | `STRAFE_LEFT` | `player_combat_walk_left_punch` | 30 (22 f × 0.75) | L forearm, L hand 8-12 | eblk 5 | 1.02 / 1.40 | 30 | `0x0a` |
| 33 | `STRAFE_FRONT` | `player_combat_walk_forward_pu` | 23 (17 f × 0.75) | L forearm, L hand 5-11 | eblk 5 | 1.74 / 3.00 | 30 | `0x0a` |
| 21 | `ATTACK_PUSH` | `gen_push` | 20 (15 f × 0.75) | L hand, R hand 2-9 | - | 1.58 / 1.65 | 20 → 13 | `0x26` stun |
| 22 | `ATTACK_FROM_IDLE` | `neutral_attack1` | 25 (20 f × 0.8) | R forearm, R hand 5-12 | - | 1.02 / 1.65 | 20 | `0x2a` stun |
| 653 | `SPECIAL_ATTACK1_FRONT` | `gen_push` | 27 (20 f × 0.75) | L hand, R hand 2-9 | eblk 4 | 1.58 / 1.97 | 10 → 6 | `0x26` |
| 655 | `SPECIAL_ATTACK1_REAR` | `gen_push` | 27 (20 f × 0.75) | L hand, R hand 2-9 | eblk 4 | 1.58 / 1.97 | 10 → 6 | `0x26` |
| 645 | `RAGE_ATTACK1_FRONT` | `gen_rage_sweep` | 50 (40 f × 0.8) | shin A, foot A 15-35 | - | 0.95 / 1.50 | 10 | `0x23` |
| 647 | `RAGE_ATTACK1_REAR` | `gen_rage_sweep` | 50 (40 f × 0.8) | shin A, foot A 15-35 | - | 0.95 / 1.50 | 10 | `0x23` |
| 617 | `BLOCK_COUNTER_FRONT` | `gen_block_duck_counter2_front` | 27 (20 f × 0.75) | R forearm, R hand 2-11 | - | 1.20 / 1.50 | 50 | `0x26` stun |
| 618 | `BLOCK_COUNTER_RIGHT` | `gen_block_duck_counter2_right` | 27 (20 f × 0.75) | L forearm, L hand 2-11 | - | 1.20 / 1.50 | 50 | `0x26` stun |
| 619 | `BLOCK_COUNTER_BACK` | `gen_block_duck_counter2_back` | 27 (20 f × 0.75) | L forearm, L hand 2-11 | - | 1.20 / 1.50 | 50 | `0x26` stun |
| 620 | `BLOCK_COUNTER_LEFT` | `gen_block_duck_counter2_left` | 27 (20 f × 0.75) | R forearm, R hand 2-11 | - | 1.20 / 1.50 | 50 | `0x26` stun |

Moving attacks ([Combat](combat.md#run-attacks)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 23 | `ATTACK_FROM_WALK` | `gen_walk_strike` | 24 (18 f × 0.75) | L forearm, L hand 5-11 | eblk 5 | 1.54 / 1.65 | 20 | `0x1a` stun |
| 24 | `ATTACK_FROM_RUN` | `gen_run_strike` | 22 (16 f × 0.75) | R forearm, R hand 2-8 | eblk 2 | 2.15 / 2.69 | 20 | `0x2a` stun |
| 0 | `RUNNING_ATTACK_CHARGE` | `gen_charge_shoulder` | 27 (20 f × 0.75) | all 4-17 | - | 3.29 / 4.11 | 20 → 31 | `0x36` |
| 1 | `RUNNING_ATTACK_DIVE` | `gen_dive` | 62 (46 f × 0.75) | all 2-24 | - | 2.22 / 3.00 | 30 → 26 | `0x3a` |
| 501 | `SWINGABLE_OBJECT_ATTACK_FROM_RUN` | `gen_run_1hand_weapon_atk` | 22 (16 f × 0.75) | R hand 2-8 | eblk 2 | 1.00 / 1.65 | 20 | `0x2a` stun |
| 490 | `KNIFE_ATTACK_FROM_RUN` | `gen_run_1hand_weapon_atk` | 22 (16 f × 0.75) | R hand 2-8 | eblk 2 | 1.00 / 1.65 | 20 | `0x2a` stun |

Strikes at a low, mid or held target:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 193 | `GROUNDED_STRIKE_01` | `gen_ground_kickB` | 27 (20 f × 0.75) | foot B 11-13 | - | 0.90 / 2.00 | 50 → 34 | `0x0a` |
| 194 | `GROUNDED_STRIKE_02` | `gen_ground_stomp` | 32 (24 f × 0.75) | foot B 11-17 | - | 0.85 / 2.20 | 50 → 34 | `0x0a` |
| 212 | `MOUNTING_STRIKE` | `gen_attack_mounting` | 26 (19 f × 0.75) | R forearm, R hand 8-13 | - | 1.20 / 2.00 | 30 | `0x06` |
| 661 | `SPECIAL_BREAK_OBJECT_LOW` | `carhit_low` | 34 (25 f × 0.75) | shin B, foot B 8-13 | - | 1.11 / 2.00 | 60 | `0x00` |
| 120 | `GRAB_FRONT_STRIKE_01` | `gen_attack_mounting` | 26 (19 f × 0.75) | R forearm, R hand 8-13 | - | 1.20 / 1.50 | 50 | `0x2a` |

Grabs, grab strikes and throws ([Combat](combat.md#grab)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 71 | `GRAB_PLAYER_INTRO` | `gen_grab_begin` | 6 (4 f × 0.75) | - | - | 1.10 / 2.50 | 0 | `0x00` |
| 70 | `GRAB_INTRO` | `AI_grab_begin` | 14 (10 f × 0.75) | - | - | 1.10 / 2.50 | 0 | `0x00` |
| 69 | `GRAB_MISS` | `gen_grab_miss` | 16 (12 f × 0.75) | - | - | 1.00 / 1.25 | 10 | `0x16` |
| 72 | `GRAB_FRONT_END` | `gen_grab_front_succeed` | 19 (14 f × 0.75) | - | - | 1.00 / 2.50 | 0 | `0x00` |
| 74 | `GRAB_REAR_END` | `gen_grab_back_succeed` | 20 (15 f × 0.75) | - | - | 1.02 / 2.50 | 0 | `0x00` |
| 51 | `GRAB_COMBO_STRIKE_01` | `gen_grab_front_strike1` | 22 (16 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 57 | `0x00` |
| 53 | `GRAB_COMBO_STRIKE_02` | `gen_grab_front_strike2` | 23 (17 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 57 | `0x00` |
| 55 | `GRAB_COMBO_STRIKE_03` | `gen_grab_front_strike3` | 23 (17 f × 0.75) | - | - | 1.03 / 1.28 | 10 → 57 | `0x00` |
| 57 | `GRAB_POWER_01_STRIKE_01` | `gen_grab_front_power10_p1` | 44 (33 f × 0.75) | - | W 19, E 33 | 1.09 / 2.25 | 69 → 57 | `0x00` |
| 59 | `GRAB_POWER_01_STRIKE_02` | `gen_grab_front_power10_p2` | 94 (70 f × 0.75) | - | use 23 | 0.93 / 2.25 | 69 → 79 | `0x00` |
| 61 | `GRAB_POWER_01_STRIKE_03` | `missing_anim_filler` | 20 (20 f × 1.0) | - | - | 1.00 / 1.25 | 0 → 76 | `0x00` |
| 63 | `GRAB_POWER_02_STRIKE_01` | `gen_grab_front_power14_p1` | 55 (41 f × 0.75) | - | W 23, E 36 | 1.08 / 2.25 | 50 | `0x00` |
| 65 | `GRAB_POWER_02_STRIKE_02` | `gen_grab_front_power14_p2` | 60 (45 f × 0.75) | - | - | 1.08 / 2.25 | 1000 | `0x00` |
| 67 | `GRAB_POWER_02_STRIKE_03` | `gen_grab_front_power2_p3` | 32 (24 f × 0.75) | - | - | 1.59 / 2.25 | 1000 | `0x00` |
| 78 | `GRAB_FRONT_SPIN_VICTIM` | `gen_grab_front_spin` | 28 (21 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 80 | `GRAB_REAR_SPIN_VICTIM` | `gen_grab_spin` | 18 (13 f × 0.75) | - | - | 0.24 / 0.30 | 0 | `0x00` |
| 147 | `THROW_01_FROM_GRAB_FRONT` | `gen_grab_front_throw_front` | 31 (23 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 66 | `0x2a` `0x100` |
| 149 | `THROW_01_FROM_GRAB_RIGHT` | `gen_grab_front_throw_right` | 31 (23 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 66 | `0x2a` `0x100` |
| 151 | `THROW_01_FROM_GRAB_REAR` | `gen_grab_front_throw_back` | 30 (22 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 66 | `0x2a` `0x100` |
| 153 | `THROW_01_FROM_GRAB_LEFT` | `gen_grab_front_throw_left` | 34 (25 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 66 | `0x2a` `0x100` |
| 155 | `THROW_02_FROM_GRAB_FRONT` | `gen_grab_front_wall_front` | 72 (54 f × 0.75) | - | - | 1.63 / 2.25 | 250 → 264 | `0x2a` `0x100` |
| 157 | `THROW_02_FROM_GRAB_RIGHT` | `gen_grab_front_wall_right` | 38 (28 f × 0.75) | - | - | 0.98 / 1.55 | 250 → 264 | `0x2a` `0x100` |
| 159 | `THROW_02_FROM_GRAB_REAR` | `gen_grab_front_wall_back` | 46 (34 f × 0.75) | - | - | 1.04 / 1.25 | 250 → 264 | `0x2a` `0x100` |
| 161 | `THROW_02_FROM_GRAB_LEFT` | `gen_grab_front_wall_left` | 39 (29 f × 0.75) | - | - | 0.45 / 1.55 | 250 → 264 | `0x2a` `0x100` |
| 118 | `GRAB_MOUNT` | `gen_grab_front_to_mount1` | 66 (49 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 95 | `GRAB_FRONT_BREAK_REACT` | `gen_grab_front_break_react` | 27 (20 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 657 | `SPECIAL_ATTACK2_FRONT` | `gen_grab_n_attack_front` | 34 (34 f × 1.0) | - | - | 1.14 / 2.50 | 60 | `0x00` |
| 659 | `SPECIAL_ATTACK2_REAR` | `gen_grab_n_attack_back` | 38 (38 f × 1.0) | - | - | 0.80 / 2.50 | 60 | `0x00` |
| 649 | `RAGE_ATTACK2_FRONT` | `gen_grab_n_attack_front` | 34 (34 f × 1.0) | - | - | 1.14 / 2.50 | 60 | `0x00` |
| 651 | `RAGE_ATTACK2_REAR` | `gen_grab_n_attack_back` | 38 (38 f × 1.0) | - | - | 0.80 / 2.50 | 60 | `0x00` |

Tackles and the mount ([Combat](combat.md#mount)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 4 | `TACKLE_PLAYER_INTRO` | `gen_tackle_start` | 7 (5 f × 0.75) | - | - | 1.82 / 3.00 | 0 | `0x00` |
| 3 | `TACKLE_INTRO` | `AI_tackle_start` | 19 (14 f × 0.75) | - | - | 1.82 / 3.00 | 0 | `0x00` |
| 2 | `TACKLE_MISS` | `gen_tackle_miss` | 59 (44 f × 0.75) | all 0-17 | - | 1.00 / 1.25 | 10 | `0x22` |
| 5 | `TACKLE_HIT_FROM_FRONT` | `gen_tackle_front` | 46 (34 f × 0.75) | - | - | 1.01 / 1.50 | 10 | `0x16` |
| 7 | `TACKLE_HIT_FROM_REAR` | `gen_tackle_back` | 34 (34 f × 1.0) | - | - | 1.04 / 1.50 | 10 | `0x16` |
| 219 | `MOUNT_COMBO_STRIKE_01` | `gen_mounting_punch1` | 16 (12 f × 0.75) | - | - | 0.12 / 0.15 | 10 → 61 | `0x00` |
| 221 | `MOUNT_COMBO_STRIKE_02` | `gen_mounting_punch2` | 19 (14 f × 0.75) | - | - | 0.12 / 0.15 | 10 → 61 | `0x00` |
| 223 | `MOUNT_COMBO_STRIKE_03` | `gen_mounting_punch4` | 28 (21 f × 0.75) | - | - | 0.12 / 0.15 | 10 → 61 | `0x00` |
| 225 | `MOUNT_POWER_01_STRIKE_01` | `gen_mount_power10_p1` | 80 (60 f × 0.75) | - | W 24, E 44 | 0.12 / 2.25 | 35 → 79 | `0x00` |
| 227 | `MOUNT_POWER_01_STRIKE_02` | `gen_mount_power10_p2` | 60 (45 f × 0.75) | - | use 12 | 0.12 / 0.15 | 50 → 79 | `0x00` |
| 229 | `MOUNT_POWER_01_STRIKE_03` | `missing_anim_filler` | 20 (20 f × 1.0) | - | - | 1.00 / 1.25 | 0 → 89 | `0x00` |
| 231 | `MOUNT_POWER_02_STRIKE_01` | `gen_mount_power13_p1` | 51 (38 f × 0.75) | - | W 19, E 32 | 0.12 / 2.25 | 69 | `0x00` |
| 233 | `MOUNT_POWER_02_STRIKE_02` | `gen_mount_power13_p2` | 66 (49 f × 0.75) | - | - | 0.12 / 0.15 | 1000 | `0x00` |
| 235 | `MOUNT_POWER_02_STRIKE_03` | `gen_mount_power6_p3` | 40 (30 f × 0.75) | - | - | 0.36 / 0.45 | 1000 | `0x00` |
| 248 | `MOUNT_PICKUP` | `gen_mounting_to_grab` | 36 (27 f × 0.75) | - | - | 0.12 / 0.15 | 0 | `0x00` |
| 244 | `MOUNT_RELEASE` | `gen_mounting_release` | 26 (19 f × 0.75) | - | - | 0.12 / 0.15 | 0 | `0x00` |
| 250 | `MOUNT_STRUGGLE` | `gen_mounted_struggle1` | 16 (12 f × 0.75) | - | - | 1.00 / 1.25 | 10 → 40 | `0x00` |
| 242 | `MOUNT_REVERSAL` | `gen_mounted_reversal` | 58 (43 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 246 | `MOUNT_BREAK` | `gen_mounted_escape` | 48 (36 f × 0.75) | R forearm 5-11 | - | 1.00 / 1.25 | 0 | `0x00` |
| 214 | `MOUNTING_GRAB` | `gen_grab_mounting` | 40 (30 f × 0.75) | R hand 15-20 | - | 1.48 / 1.50 | 0 | `0x00` |

The held victim's moves and the counters:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 96 | `GRAB_FRONT_STRUGGLE1` | `gen_grab_front_struggle` | 23 (17 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 108 | `GRAB_REAR_STRUGGLE1` | `gen_grab_back_struggle2` | 30 (22 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 104 | `GRAB_FRONT_ATTACK` | `gen_grab_front_victim_strike` | 34 (25 f × 0.75) | foot B 11-19 | - | 1.00 / 1.25 | 20 | `0x26` stun |
| 116 | `GRAB_REAR_ATTACK` | `gen_grab_back_victim_strike` | 44 (33 f × 0.75) | shin A, foot A, shin B, foot B 11-16 | - | 0.84 / 1.05 | 20 | `0x3a` stun |
| 100 | `GRAB_FRONT_ESCAPE1` | `gen_grab_front_escape1` | 62 (46 f × 0.75) | - | - | 1.08 / 1.35 | 20 | `0x2a` `0x100` |
| 112 | `GRAB_REAR_ESCAPE1` | `gen_grab_back_escape_throw` | 56 (42 f × 0.75) | - | - | 0.24 / 0.30 | 20 | `0x2a` `0x100` |
| 90 | `GRAB_FRONT_REVERSAL` | `gen_grab_front_reversal` | 44 (33 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 92 | `GRAB_REAR_REVERSAL` | `gen_grab_back_reversal` | 44 (33 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 76 | `GRAB_FRONT_COUNTER` | `gen_grab_front_counter` | 36 (27 f × 0.75) | all 0-35 | - | 1.00 / 2.00 | 100 | `0x16` |
| 9 | `TACKLE_FRONT_COUNTER` | `gen_tackle_counter` | 58 (43 f × 0.75) | all 0-56 | - | 1.12 / 2.00 | 100 | `0x16` |

Tandems:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 163 | `TANDEM_01_GRABBER_INTRO` | `gen_tandem1_grabber_begin` | 36 (27 f × 0.75) | - | - | 0.23 / 0.29 | 0 | `0x00` |
| 164 | `TANDEM_01_VICTIM_INTRO` | `gen_tandem1_victim_begin` | 36 (27 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 165 | `TANDEM_01_ATTACKER_INTRO` | `gen_tandem1_attacker_begin` | 36 (27 f × 0.75) | - | - | 1.72 / 2.50 | 50 | `0x00` |
| 166 | `TANDEM_01_GRABBER_END` | `gen_tandem1_grabber_end` | 27 (20 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 167 | `TANDEM_01_VICTIM_END` | `gen_tandem1_victim_end` | 38 (28 f × 0.75) | - | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 168 | `TANDEM_01_ATTACKER_END` | `gen_tandem1_attacker_end` | 36 (27 f × 0.75) | - | - | 0.47 / 0.59 | 350 | `0x00` |
| 169 | `TANDEM_02_GRABBER_INTRO` | `gen_tandem2_grabber_begin` | 47 (35 f × 0.75) | - | - | 0.24 / 0.30 | 0 | `0x00` |
| 170 | `TANDEM_02_VICTIM_INTRO` | `gen_tandem2_victim_begin` | 47 (35 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 171 | `TANDEM_02_ATTACKER_INTRO` | `gen_tandem2_attacker_begin` | 47 (35 f × 0.75) | - | - | 1.46 / 2.00 | 50 | `0x00` |
| 172 | `TANDEM_02_GRABBER_END` | `gen_tandem2_grabber_end` | 42 (31 f × 0.75) | - | - | 0.44 / 0.56 | 0 | `0x00` |
| 173 | `TANDEM_02_VICTIM_END` | `gen_tandem2_victim_end` | 54 (40 f × 0.75) | - | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 174 | `TANDEM_02_ATTACKER_END` | `gen_tandem2_attacker_end` | 47 (35 f × 0.75) | - | - | 1.29 / 1.62 | 350 | `0x00` |
| 175 | `TANDEM_03_GRABBER_INTRO` | `gen_tandem3_grabber_begin` | 11 (8 f × 0.75) | - | - | 0.24 / 0.30 | 0 | `0x00` |
| 176 | `TANDEM_03_VICTIM_INTRO` | `gen_tandem3_victim_begin` | 11 (8 f × 0.75) | all 0-11 | - | 1.00 / 1.25 | 0 | `0x00` |
| 177 | `TANDEM_03_ATTACKER_INTRO` | `gen_tandem3_attacker_begin` | 11 (8 f × 0.75) | - | - | 1.69 / 2.80 | 50 | `0x00` |
| 178 | `TANDEM_03_GRABBER_END` | `gen_tandem3_grabber_end` | 43 (32 f × 0.75) | all 0-32 | - | 0.35 / 0.44 | 0 | `0x00` |
| 179 | `TANDEM_03_VICTIM_END` | `gen_tandem3_victim_end` | 63 (47 f × 0.75) | all 0-40 | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 180 | `TANDEM_03_ATTACKER_END` | `gen_tandem3_attacker_end` | 40 (30 f × 0.75) | - | - | 0.99 / 1.24 | 350 | `0x00` |
| 181 | `BAT_TANDEM_01_GRABBER_INTRO` | `gen_tandem_bat_grabber_begin` | 14 (10 f × 0.75) | - | - | 0.24 / 0.30 | 0 | `0x00` |
| 182 | `BAT_TANDEM_01_VICTIM_INTRO` | `gen_tandem_bat_victim_begin` | 14 (10 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 183 | `BAT_TANDEM_01_ATTACKER_INTRO` | `gen_tandem_bat_attacker_begin` | 14 (10 f × 0.75) | - | - | 1.27 / 2.50 | 50 | `0x00` |
| 184 | `BAT_TANDEM_01_GRABBER_END` | `gen_tandem_bat_grabber_end` | 67 (50 f × 0.75) | - | - | 0.31 / 0.39 | 0 | `0x00` |
| 185 | `BAT_TANDEM_01_VICTIM_END` | `gen_tandem_bat_victim_end` | 67 (50 f × 0.75) | - | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 186 | `BAT_TANDEM_01_ATTACKER_END` | `gen_tandem_bat_attacker_end` | 67 (50 f × 0.75) | - | - | 1.57 / 1.96 | 500 | `0x00` |
| 187 | `KNIFE_TANDEM_01_GRABBER_INTRO` | `gen_tandem_knife_grabber_begi` | 68 (51 f × 0.75) | - | - | 0.23 / 0.29 | 0 | `0x00` |
| 188 | `KNIFE_TANDEM_01_VICTIM_INTRO` | `gen_tandem_knife_victim_begin` | 68 (51 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 189 | `KNIFE_TANDEM_01_ATTACKER_INTRO` | `gen_tandem_knife_attacker_beg` | 68 (51 f × 0.75) | - | - | 1.44 / 2.50 | 50 | `0x00` |
| 190 | `KNIFE_TANDEM_01_GRABBER_END` | `gen_tandem_knife_grabber_end` | 18 (13 f × 0.75) | - | - | 0.26 / 0.32 | 0 | `0x00` |
| 191 | `KNIFE_TANDEM_01_VICTIM_END` | `gen_tandem_knife_victim_end` | 59 (44 f × 0.75) | - | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 192 | `KNIFE_TANDEM_01_ATTACKER_END` | `gen_tandem_knife_attacker_end` | 22 (16 f × 0.75) | - | - | 0.98 / 1.23 | 600 | `0x00` |

Stealth kills:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 637 | `STEALTH_KILL` | `player_stealthkill_weak` | 52 (39 f × 0.75) | - | - | 0.74 / 1.50 | 3000 | `0x00` |
| 639 | `STEALTH_KNIFE_KILL` | `player_stealthkill_weak` | 52 (39 f × 0.75) | - | - | 0.74 / 1.50 | 3000 | `0x00` |
| 641 | `STEALTH_BATON_KILL` | `player_stealthkill_weak` | 52 (39 f × 0.75) | - | - | 0.74 / 1.50 | 3000 | `0x00` |

Weapons ([Combat](combat.md#armed-moves)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 34 | `BAT_COMBO_S1` | `gen_weapon01_atk01` | 38 (28 f × 0.75) | R hand 9-17 | duck 8, E 13, R 25 | 1.56 / 2.10 | 10 | `0x29` |
| 35 | `BAT_COMBO_SS2` | `gen_weapon01_atk02` | 32 (24 f × 0.75) | R hand 9-13 | duck 8 | 1.78 / 2.10 | 10 | `0x2b` |
| 36 | `BAT_COMBO_X1` | `gen_weapon01_slash_atk01` | 35 (26 f × 0.75) | R hand 11-17 | E 20, R 23 | 1.24 / 1.50 | 130 | `0x2a` |
| 37 | `BAT_COMBO_GROUNDED_STRIKE_01` | `gen_weapon01_grnd_atk01` | 30 (22 f × 0.75) | R hand 11-17 | - | 0.97 / 1.50 | 75 | `0x0a` |
| 38 | `BAT_COMBO_GROUNDED_STRIKE_02` | `gen_weapon01_grnd_atk02` | 26 (19 f × 0.75) | R hand 7-15 | - | 0.97 / 1.50 | 75 | `0x0a` |
| 39 | `BATON_COMBO_S1` | `baton_1` | 22 (16 f × 0.75) | R hand 1-9 | duck 2, W 9, E 11 | 1.27 / 1.50 | 10 | `0x0a` |
| 40 | `BATON_COMBO_SS2` | `baton_2` | 18 (13 f × 0.75) | R hand 0-8 | duck 0 | 1.08 / 1.50 | 10 | `0x1a` stun |
| 41 | `BATON_COMBO_X1` | `baton_3` | 28 (21 f × 0.75) | R hand 7-12 | duck 4 | 1.39 / 1.70 | 60 | `0x0a` stun |
| 42 | `BATON_COMBO_GROUNDED_STRIKE_01` | `cop_baton_swing_grnd_1` | 27 (20 f × 0.75) | R hand 15-17 | - | 1.21 / 1.50 | 50 | `0x0a` |
| 43 | `BATON_COMBO_MOUNTING_STRIKE_01` | `cop_baton_swing_grnd_1` | 27 (20 f × 0.75) | R hand 15-17 | - | 1.21 / 1.50 | 50 | `0x0a` |
| 44 | `BATON_COMBO_GRAB_FRONT_STRIKE_01` | `cop_baton_1` | 20 (15 f × 0.75) | R hand 9-17 | - | 1.53 / 1.91 | 50 | `0x0a` |
| 45 | `KNIFE_COMBO_S1` | `knife_1` | 23 (17 f × 0.75) | R hand 7-15 | W 15, E 16 | 1.09 / 1.50 | 20 | `0x06` |
| 46 | `KNIFE_COMBO_SS2` | `knife_2` | 24 (18 f × 0.75) | R hand 1-8 | - | 0.88 / 1.50 | 20 | `0x26` |
| 47 | `KNIFE_COMBO_X1` | `knife_3` | 27 (20 f × 0.75) | R hand 8-15 | - | 1.18 / 1.50 | 20 | `0x16` stun |
| 48 | `KNIFE_COMBO_GROUNDED_STRIKE_01` | `gen_knife_attack_grounded` | 27 (20 f × 0.75) | R hand 8-15 | - | 1.05 / 1.50 | 25 | `0x0a` |
| 49 | `KNIFE_COMBO_MOUNTING_STRIKE_01` | `gen_knife_attack_mounting` | 27 (20 f × 0.75) | R hand 9-12 | - | 1.10 / 1.50 | 25 | `0x0a` |
| 50 | `KNIFE_COMBO_GRAB_FRONT_STRIKE_01` | `gen_knife_attack_mounting` | 27 (20 f × 0.75) | R hand 9-12 | - | 1.10 / 1.50 | 25 | `0x0a` |
| 484 | `KNIFE_GRAB_REAR_ATTACK` | `gen_grab_back_knife` | 76 (57 f × 0.75) | - | use 53 | 0.24 / 0.30 | 600 | `0x00` |
| 486 | `KNIFE_GRAB_FRONT_ATTACK` | `gen_grab_front_knife` | 83 (62 f × 0.75) | - | use 55 | 1.08 / 1.35 | 600 | `0x00` |
| 488 | `KNIFE_MOUNT_ATTACK` | `gen_knife_mounting` | 80 (60 f × 0.75) | - | use 57 | 0.12 / 0.15 | 600 | `0x2a` `0x100` |
| 492 | `BROKEN_BOTTLE_GRAB_REAR_ATTACK` | `gen_grab_back_bottle` | 55 (41 f × 0.75) | - | use 41 | 0.24 / 0.30 | 40 | `0x00` |
| 494 | `BROKEN_BOTTLE_GRAB_FRONT_ATTACK` | `gen_grab_front_bottle` | 43 (32 f × 0.75) | - | use 25 | 1.08 / 1.35 | 40 | `0x00` |
| 496 | `BROKEN_BOTTLE_MOUNT_ATTACK` | `gen_beerbottle_mounting` | 64 (48 f × 0.75) | - | use 24 | 0.12 / 0.15 | 40 | `0x2a` `0x100` |
| 473 | `ONE_HANDED_OBJECT_SMASH_FRONT` | `bottle_smash_attacker` | 46 (34 f × 0.75) | - | - | 1.27 / 2.00 | 120 | `0x00` |
| 475 | `ONE_HANDED_OBJECT_SMASH_REAR` | `bottle_smash_attacker` | 46 (34 f × 0.75) | - | - | 1.27 / 2.00 | 120 | `0x00` |
| 505 | `BARREL_THROW` | `gen_2hand_throw_fwd` | 30 (30 f × 1.0) | R hand 14-19 | - | 0.91 / 1.14 | 30 | `0x1a` |
| 506 | `BARREL_THROW_FROM_WALK` | `gen_2hand_throw_walk` | 26 (26 f × 1.0) | L hand, R hand 8-16 | - | 1.00 / 1.25 | 30 | `0x1a` |
| 507 | `BARREL_THROW_FROM_RUN` | `gen_run_2hand_weapon_throw` | 30 (30 f × 1.0) | L hand, R hand 10-14 | - | 1.00 / 1.25 | 30 | `0x1a` |
| 467 | `ONE_HANDED_OBJECT_THROW` | `gen_1hand_throw` | 26 (19 f × 0.75) | R hand 5-11 | - | 1.02 / 1.28 | 10 | `0x0a` |
| 471 | `ONE_HANDED_OBJECT_THROW_FROM_WALK` | `gen_1hand_throw_walk` | 31 (23 f × 0.75) | R hand 13-19 | - | 1.00 / 1.25 | 10 | `0x0a` |
| 472 | `ONE_HANDED_OBJECT_THROW_FROM_RUN` | `gen_1hand_throw_run` | 34 (25 f × 0.75) | R hand 13-19 | - | 1.00 / 1.25 | 10 | `0x0a` |
| 551 | `GHETTO_THROW` | `ghetto_throw` | 27 (20 f × 0.75) | 0 11-17 | - | 0.85 / 1.07 | 30 | `0x1a` |
| 552 | `GHETTO_THROW_FROM_WALK` | `ghetto_throw_walk` | 47 (35 f × 0.75) | L hand, R hand 12-16 | - | 1.00 / 1.25 | 30 | `0x1a` |
| 553 | `GHETTO_THROW_FROM_RUN` | `ghetto_throw_run` | 27 (20 f × 0.75) | R hand 7-11 | - | 1.00 / 1.25 | 30 | `0x1a` |
| 491 | `KNIFE_THROW` | `gen_knifethrow` | 38 (28 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 502 | `SWINGABLE_OBJECT_THROW` | `gen_batthrow` | 35 (26 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 629 | `BAT_BLOCK_REACT` | `gen_batblock_dodge_atk` | 22 (16 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |

Block, rage and the rest:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 643 | `RAGE_START` | `gen_rage_enter05` | 64 (48 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 665 | `SPECIAL_FLASH` | `gen_flash_use` | 54 (40 f × 0.75) | - | use 25 | 1.00 / 1.25 | 0 | `0x00` |
| 616 | `BLOCK_DODGE` | `gen_duck` | 28 (21 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 605 | `BLOCK_START` | `gen_blockA` | 7 (5 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 606 | `BLOCK_SUSTAIN` | `gen_blockA_idle` | 40 (30 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 607 | `BLOCK_SHUFFLE` | `gen_block_turn` | 12 (9 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 664 | `SPECIAL_SPRAY` | `gen_spray` | 30 (30 f × 1.0) | R hand 10-19 | use 14 | 0.94 / 1.80 | 80 | `0x1a` stun |

## Corrections to other pages {#corrections}

Found while building this table; for the owners of those pages. Confirmed (code) at the cited addresses.

- [Combat](combat.md#attacks) "a mugging (`0x0026f860`)" and [Combat](combat.md#grab) "grabbed by `0x0026f860`":
  `0x0026f860` is `Tandem_Start`, the three-person [tandem](#tandem), not a mugging or a grab from behind.
- [Combat](combat.md#state-flags): state `0x1000` is the paired power-move state (power strikes, specials, stealth
  kills), not "throwing" alone.
- [Combat](combat.md#grab) and [Combat](combat.md#fight-stance): `0x00244770` is `Human_MoveThrowAim` (aiming a
  throw), not a stealth state; hiding is state `0x200000` ([Stealth](#stealth)).
- [Combat](combat.md#attacks): the target-state strikes (193, 212, 120) are tested only in the fight-stance path, after
  the strafes and snaps; outside a stance the moving attacks test only the low target (193). Cross has its own 194
  and 661 ([Cross](#cross)).
- [Combat](combat.md#bat): a weapon's wear is the hit counter `+0x10d` / `+0x10e`, taken down by one on each `use`
  event; `+0x128` is not its hitpoints ([Weapons](#weapons)).
- [Characters](characters.md#clip-selection) reads the 633 / 636 mix under `0x00228188` as "carrying"; the function is
  `Human_IsStalkingTarget`, and 633 / 636 are `STEALTH_WALK` / `STEALTH_READY_WALK`: the hidden walk blending toward
  the kill-ready walk ([Stealth](#stealth)).
- [Combat](combat.md#targets) step 1 "keep the current target while `0x0027a120` accepts it": that filter accepts
  only world objects, so a human target is always searched for again; and the 135° pass's humans (× 0.7, filter
  `0x002796a0`) have no angle test at all ([Targets](#targeting)).
- [Combat](combat.md#attacks) "Square takes a target from `Player_PickTarget`": in the stance square keeps the
  current target within 3 m and searches only without one or beyond; cross always searches ([Targets](#targeting)).
- [Combat](combat.md#targets), the steer's turn: the new heading is the goal's heading minus the attack's direction
  (record offset `+0x0` / `+0x2`), not the goal's heading; a snap turns its side, not its front, to the target
  ([Targets](#targeting)).
- [Combat](combat.md#targets), the steer's goal "only half that lead": a lead over 1 m that takes the target farther
  away is cut to 0.5 m long (the lead × 0.5 / its length), not halved ([Contact](#reach)).

## Coney's implementation

The moves Coney plays and where they differ from the table above: [Combat differences](combat-diffs.md).

## Open questions

- Where 653's 0.04 m beyond its 1.58 m reach comes from: its steer aims at the target's position (task `+0x10`),
  not its head ([The two steers](#two-steers)); whether the clip's root motion over k1-k3 adds it was not read.
- The low and mid tests' state bits are read but not all named; which reactions put a human in them, and which
  point the height test compares (vtable `+0xac`).
- Why the snap 25 slides the player 0.3-0.5 m away from a target pinned on its right and misses, when 27 hits one
  on the left. That target was pinned while walking, so the steer's lead may explain it ([Contact](#reach)); not
  rerun against a still target.
- Why the tandem intro ran 51 updates, not 36.
- The PS2 FPU's rounding toward zero is assumed, not read from the FPU's control, for the late ties ([Reading the
  tables](#reading)). A run of a rate-0.9 clip with a tie (an event at frame 5 or 14) would check it.
- 490 `KNIFE_ATTACK_FROM_RUN`, 18 `ATTACK_SSX3_HOLD` and the `_02` snaps: no code path plays them here (20 is the
  chain's random pick after `SS2`, `0x00280708`).
