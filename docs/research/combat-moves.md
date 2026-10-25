---
kind: topic
system: combat
owns:
  functions: [
    "0x00258e10", "0x00258e88", "0x002590f8", "0x0026f780", "0x0026f860", "0x00277458", "0x0027d6e0",
  ]
---
# Combat: moves, their timing and frame data

Verified against: `SLUS_212.15` (NTSC-U); runtime claims name PCSX2 2.9.94 and their method in How this was checked.

## Summary

Every move a human can make in a fight, in one place: what input or command starts it, which tests gate it, the clip
it plays, its strike shapes and phases in updates, its reach and damage, and what refuses or cancels it. The
mechanics behind the moves (chains, block, grabs, damage, reactions, meters, targets) are on
[Combat](combat/index.md); this page indexes them by move, fills the moves that page leaves out (strafe attacks, the
counters from square and cross, cross at a grounded target, tandems, stealth kills and the mounted victim's actions)
and gives frame data for all of them. What a weapon in hand changes, the bat's cross among it, is on [Combat: throws,
weapons in hand and mugging](combat-throws.md#weapons). Which target each move goes for, the steer onto it and where
the attacker stands at contact are on [Combat: targets](combat-targets.md#targeting). Where Coney differs: [Combat
differences](combat-diffs.md).

## Map {#original-structure}

| Address | Name | What it decides | Evidence |
| --- | --- | --- | --- |
| `0x0027c120` | `Player_UpdateActions` | the dispatcher: block, state routes, then the command ([Combat](combat/index.md#dispatch)) | confirmed (code) |
| `0x00286cc8` | `Player_Square` | square: moving attacks, strafes, snaps, target-state strikes, tandem, counters, `S1`; the armed branch | confirmed (code) |
| `0x00287a18` | `Player_Cross` | cross: the same order without strafes and snaps, with 194 and 661 for low targets | confirmed (code) |
| `0x00287730` | `Player_Special` | cross + square, circle + cross, circle + triangle ([Combat](combat/index.md#run-attacks)) | confirmed (code) |
| `0x00284920` | `Player_GrabOrTackle` | circle tapped / held ([Combat](combat-grabs.md#grab)) | confirmed (code) |
| `0x0027ec20` | `Player_UpdateMounting` | the mounter's inputs ([Combat](combat-grabs.md#mount)) | confirmed (code) |
| `0x0027f1a0` | `Player_UpdateTackled` | the mounted victim's inputs: struggle, get-off, reversal | confirmed (code) |
| `0x0027fd68` | `Player_UpdateGrabbed` | the held victim's inputs ([Combat](combat-grabs.md#grabbed)) | confirmed (code) |
| `0x0027e040` | `Player_UpdateActionsHidden` | hidden in shadow (state `0x200000`): the stealth kill | confirmed (code) |
| `0x0027de78` | `Player_OnCommand36` | command `0x36`: the push 21 | confirmed (code) |
| `0x00287fe0` | `Attack_StartGroundStrike` | commands `0x37` / `0x38`: 193 / 194 | confirmed (code) |
| `0x0027d6e0` | `Player_TryCounterGrab` | command 3: the AI's counter, 76 or 9 | confirmed (code) |
| `0x00258e10` / `0x0026f860` | `Tandem_CanStart` / `Tandem_Start` | the three-person tandem | confirmed (code) |
| `0x00277458` | `Tandem_AlignThree` | places the three for a tandem: turns them and slides the grabber and the attacker over 1/15 s to the set's offsets ([Tandems](#tandem)) | confirmed (code); which body each step moves inferred |
| `0x0026f780` | `Tandem_OnVictimIntroEnd` | the end of the victim's intro: the attacker's intro and end damage (ids +0 and +3) dealt as one hit | confirmed (code) |
| `0x00258e88` / `0x002590f8` | `Human_CanCounterGrab` / `Human_CanCounterTackle` | whether a grab or tackle coming at the human can be countered | confirmed (code) |
| `0x00264738` | `Player_StartStealthKill` | the stealth kill 637 / 639 / 641 | confirmed (code) |
| `0x00261c80` | `Attack_StartAtTarget` | starts a strike on a target in a given state: 212, 661, 120, the push 21 | confirmed (code), runtime |
| `0x002723e8` | `Mount_StartReversal` | the mounted victim's reversal 242 / 243 | confirmed (code), runtime |
| `0x00225200` / `0x002250a0` | `Human_IsHighOrBusy` / `Human_IsMidOrBusy` | the low and mid target tests ([Square](#square)) | confirmed (code) |
| `0x00261a08` | `Attack_StartGrounded` | a strike at a low target: 193, 194, the armed slot `0x13` | confirmed (code) |
| `0x00269f30` | `Human_BlockHit` | whether a block stops a hit, by weapon set | confirmed (code) |
| `0x00280630` / `0x00280708` | `Player_BufferChain` / `Player_UpdateChain` | the one-press chain buffer and the next attack ([Input](#input)) | confirmed (code), runtime |
| `0x0027a6c0` / `0x0027a4b0` | `Player_PickTarget` / `Player_FindAttackTarget` | the attack target searches ([Combat: targets](combat-targets.md#targeting)) | confirmed (code) |
| `0x00147940` | `Commands_Match` | turns pad samples into one command per update ([Input](#input)) | confirmed (code) |

Every function of the moves, with its role and level, is on [the combat function list](combat/functions.md).

## Data

### Reading the tables {#reading}

- **Updates** are game updates of 1/30 s, counted from the clip's first update (k = 0). A clip of *f* frames (its
  duration × 30) at playback rate *r* lasts as many updates as an event at frame *f* would take to fire (the rule
  below, tie included): the first k with k × *r* ≥ *f* − 0.5, one later on a tie. That is ceil((*f* − 0.5) / *r*)
  away from the ties, **not** ceil(*f* / *r*): 19's 21 frames at 0.8 last 26 updates, 34's 28 at 0.75 last 37
  ([When a clip ends](formats/animation.md#clip-end), confirmed (code) and (runtime)). On update k the next clip,
  or the idle, already plays. The rate comes from the Anim Range List record's flags: `0x800` → 0.8, `0x1000` → 1.0,
  `0x2000` → 0.9, none → 0.75 ([Playback rate](formats/animation.md#playback-rate)).
- **An event at frame *f*** (strike shape on or off, warnings, `use`, the held phases) acts on the first update k
  whose cursor frame reaches *f*. The clip time on update k is the sum of k steps of dt × *r* (dt at `0x005102cc` is
  `0x3d088889`, 0.033333335), and the frame is `uint(t × 30 + 0.5)` (`AnimCursor_Advance` `0x001044a0`; the time
  is added in `AnimCursor_Step` `0x00104110`). `Anim_FireFrameEvents` (`0x00101dd8`) fires each event whose frame is
  at most the cursor's. All of this is confirmed (code). In closed form, the event acts on the **first k with k ×
  *r* ≥ *f* − 0.5**. The exception is an **exact tie** at rate 0.75 or 0.9 (k × *r* = *f* − 0.5: frames 5, 8, 11
  and so on at 0.75, frames 5, 14, 23 and so on at 0.9), which acts **one update later**. Frame 2 at 0.75 (k = 2)
  is the one tie that acts on time. The PS2 FPU rounds every add and multiply toward zero, so the summed time falls a
  hair short of the tie; at k = 2 it is still exact. Rates 0.8 and 1.0 have no ties that fall short. The rounding
  mode is inferred; with it the rule matches all 17 strike-shape windows measured at runtime ([Runtime
  results](#runtime)) and the held phases below. A reimplementation can get the same updates by summing the time in
  `float` and rounding each step toward zero, or by using the closed form with the tie exception.
- **The held phases** W (chain window `0x2`, event `0x2c`), E (end `0x4`, `0x2d`) and R (recovery `0x40000`, `0x48`)
  show in record `+0x08` on the update that rule gives: `S1` 6 / 15 / 16, `X1` 10 / 20 / 21 and `XX2` 17 / 19 match
  the runtime timings on [Combat](combat/index.md#attacks). Confirmed (runtime) for those three.
- **Strike shapes**: event `0xf` turns a bone's shape on and `0x10` off; `0x13` / `0x14` turn on and off all ten
  shapes and the capsule ("all"). Bones: L/R forearm and hand (18/19, 24/25), shin and foot A (29/30) and B
  (32/33), spine 3, head 6 ([Combat](combat/index.md#moving-strikes)). Shown "on-off" in updates; a shape is live from the
  first update to the one before the second.
- **Events**: `eblk` early block (`0x26`) and `duck` warning (`0x24`), which tell a target to block or duck
  ([Combat](combat/index.md#block)); `KD` knockdown (7); `use` (`0x41`), which wears a held weapon or uses the flash.
- **Reach / far** are the record's reach and far ranges in metres; when the stored far (mm) is not above the reach,
  the far range is reach × 1.25 (inferred from the disc's values).
- **Damage** is the disc's value for the clip, then the value Rembrandt plays where the class table writes that
  index ([Combat](combat/index.md#damage-table)). The class value is scaled by the Warrior byte (115 %) with the PS2 FPU's
  rounding toward zero, which is why 50 gives 57, not 58, and 230 gives 264: every value measured at runtime
  matches. Inferred (the FPU's rounding mode), confirmed (runtime) for the 17 measured values.
- **Hit code** picks the victim's reaction ([Combat](combat/index.md#hit-codes)); `stun` is flag `0x400`, `0x100` marks
  the throws.

### Who plays what

The player's set is `warr_re_cv`; ids its own data leaves unset fall back to the generic set (`civl_co_ma1` has the
same clips). The tables below take the clip from the player's set when it has one. Damage and reach are the
**player's** record. Another class's values (and the AI's damage tables) differ: [Attack
kinds](../references/attacks.md#damage-table).

### Frame data {#frame-data}

Generated from the disc's clips and records (Rembrandt). Ids with the filler clip `missing_anim_filler` (61, 229)
are never seen. Standing attacks and their chains ([Combat](combat/index.md#attacks)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 12 | `ATTACK_S1` | `combo1a_S_hi_b` | 20 (16 f × 0.8) | L hand 1-10 | eblk 0, W 6, E 15, R 16 | 0.94 / 1.80 | 40 → 17 | `0x0a` |
| 16 | `ATTACK_SS2` | `combo1a_SS_hi_b` | 25 (20 f × 0.8) | L forearm, L hand 2-7 | eblk 1, W 6, E 19, R 20 | 1.09 / 1.80 | 40 → 36 | `0x0b` |
| 19 | `ATTACK_SSS3` | `combo1a_SSS_lo_b` | 26 (21 f × 0.8) | R hand 7-15 | eblk 4, E 16, R 17 | 1.14 / 1.70 | 50 → 53 | `0x26` stun |
| 20 | `ATTACK_SSS3_HOLD` | `combo1a_SSSH_lo_b` | 32 (26 f × 0.8) | shin A, foot A 15-21 | eblk 12, E 24, R 25 | 1.13 / 1.90 | 55 → 53 | `0x26` |
| 17 | `ATTACK_SSX3` | `combo1a_SSX_lo_b` | 34 (27 f × 0.8) | shin B, foot B 5-12 | eblk 6, E 24, R 25 | 1.47 / 1.70 | 60 → 61 | `0x25` |
| 11 | `ATTACK_X1` | `combo1b_X_hi_r` | 30 (24 f × 0.8) | R hand 6-11 | duck 4, W 10, E 20, R 21 | 1.02 / 1.90 | 45 → 26 | `0x09` |
| 13 | `ATTACK_XX2` | `combo1b_XX_hi_l` | 30 (24 f × 0.8) | R forearm, R hand 9-14 | duck 6, E 17, R 19 | 1.12 / 1.90 | 60 → 53 | `0x1b` |
| 15 | `ATTACK_SX2` | `combo1a_SX_hi_b` | 20 (16 f × 0.8) | R hand 6-11 | eblk 5, E 14, R 15 | 1.37 / 1.90 | 50 → 44 | `0x1a` stun |
| 14 | `ATTACK_XS2` | `combo1b_XS_hi_l` | 30 (24 f × 0.8) | L hand 7-12 | duck 5, E 22, R 24 | 0.88 / 1.90 | 50 → 44 | `0x2b` |
| 18 | `ATTACK_SSX3_HOLD` | `combo1a_SSSH_lo_b` | 32 (26 f × 0.8) | shin A, foot A 15-21 | eblk 12, E 24, R 25 | 1.13 / 1.90 | 65 → 61 | `0x26` |
| 25 | `SNAP_RIGHT_01` | `gen_snap_right1` | 16 (12 f × 0.75) | R forearm, R hand 2-7 | E 8 | 0.95 / 1.30 | 10 → 31 | `0x1b` stun |
| 27 | `SNAP_LEFT_01` | `gen_snap_left1` | 16 (12 f × 0.75) | L forearm, L hand 2-7 | E 8 | 0.82 / 1.30 | 10 → 31 | `0x19` stun |
| 29 | `SNAP_BACK_01` | `gen_snap_back1` | 16 (12 f × 0.75) | R forearm, R hand 1-7 | E 8 | 0.96 / 1.30 | 10 → 31 | `0x1b` stun |
| 31 | `STRAFE_RIGHT` | `player_combat_walk_right_punc` | 29 (22 f × 0.75) | L forearm, L hand 8-12 | eblk 7 | 0.89 / 1.40 | 30 | `0x0a` |
| 32 | `STRAFE_LEFT` | `player_combat_walk_left_punch` | 29 (22 f × 0.75) | L forearm, L hand 8-12 | eblk 5 | 1.02 / 1.40 | 30 | `0x0a` |
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

Moving attacks ([Combat](combat/index.md#run-attacks)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 23 | `ATTACK_FROM_WALK` | `gen_walk_strike` | 24 (18 f × 0.75) | L forearm, L hand 5-11 | eblk 5 | 1.54 / 1.65 | 20 | `0x1a` stun |
| 24 | `ATTACK_FROM_RUN` | `gen_run_strike` | 21 (16 f × 0.75) | R forearm, R hand 2-8 | eblk 2 | 2.15 / 2.69 | 20 | `0x2a` stun |
| 0 | `RUNNING_ATTACK_CHARGE` | `gen_charge_shoulder` | 27 (20 f × 0.75) | all 4-17 | - | 3.29 / 4.11 | 20 → 31 | `0x36` |
| 1 | `RUNNING_ATTACK_DIVE` | `gen_dive` | 61 (46 f × 0.75) | all 2-24 | - | 2.22 / 3.00 | 30 → 26 | `0x3a` |
| 501 | `SWINGABLE_OBJECT_ATTACK_FROM_RUN` | `gen_run_1hand_weapon_atk` | 21 (16 f × 0.75) | R hand 2-8 | eblk 2 | 1.00 / 1.65 | 20 | `0x2a` stun |
| 490 | `KNIFE_ATTACK_FROM_RUN` | `gen_run_1hand_weapon_atk` | 21 (16 f × 0.75) | R hand 2-8 | eblk 2 | 1.00 / 1.65 | 20 | `0x2a` stun |

Strikes at a low, mid or held target:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 193 | `GROUNDED_STRIKE_01` | `gen_ground_kickB` | 27 (20 f × 0.75) | foot B 11-13 | - | 0.90 / 2.00 | 50 → 34 | `0x0a` |
| 194 | `GROUNDED_STRIKE_02` | `gen_ground_stomp` | 32 (24 f × 0.75) | foot B 11-17 | - | 0.85 / 2.20 | 50 → 34 | `0x0a` |
| 212 | `MOUNTING_STRIKE` | `gen_attack_mounting` | 25 (19 f × 0.75) | R forearm, R hand 8-13 | - | 1.20 / 2.00 | 30 | `0x06` |
| 661 | `SPECIAL_BREAK_OBJECT_LOW` | `carhit_low` | 33 (25 f × 0.75) | shin B, foot B 8-13 | - | 1.11 / 2.00 | 60 | `0x00` |
| 120 | `GRAB_FRONT_STRIKE_01` | `gen_attack_mounting` | 25 (19 f × 0.75) | R forearm, R hand 8-13 | - | 1.20 / 1.50 | 50 | `0x2a` |

Grabs, grab strikes and throws ([Combat](combat-grabs.md#grab)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 71 | `GRAB_PLAYER_INTRO` | `gen_grab_begin` | 5 (4 f × 0.75) | - | - | 1.10 / 2.50 | 0 | `0x00` |
| 70 | `GRAB_INTRO` | `AI_grab_begin` | 13 (10 f × 0.75) | - | - | 1.10 / 2.50 | 0 | `0x00` |
| 69 | `GRAB_MISS` | `gen_grab_miss` | 16 (12 f × 0.75) | - | - | 1.00 / 1.25 | 10 | `0x16` |
| 72 | `GRAB_FRONT_END` | `gen_grab_front_succeed` | 19 (14 f × 0.75) | - | - | 1.00 / 2.50 | 0 | `0x00` |
| 74 | `GRAB_REAR_END` | `gen_grab_back_succeed` | 20 (15 f × 0.75) | - | - | 1.02 / 2.50 | 0 | `0x00` |
| 51 | `GRAB_COMBO_STRIKE_01` | `gen_grab_front_strike1` | 21 (16 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 57 | `0x00` |
| 53 | `GRAB_COMBO_STRIKE_02` | `gen_grab_front_strike2` | 23 (17 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 57 | `0x00` |
| 55 | `GRAB_COMBO_STRIKE_03` | `gen_grab_front_strike3` | 23 (17 f × 0.75) | - | - | 1.03 / 1.28 | 10 → 57 | `0x00` |
| 57 | `GRAB_POWER_01_STRIKE_01` | `gen_grab_front_power10_p1` | 44 (33 f × 0.75) | - | W 19, E 33 | 1.09 / 2.25 | 69 → 57 | `0x00` |
| 59 | `GRAB_POWER_01_STRIKE_02` | `gen_grab_front_power10_p2` | 93 (70 f × 0.75) | - | use 23 | 0.93 / 2.25 | 69 → 79 | `0x00` |
| 61 | `GRAB_POWER_01_STRIKE_03` | `missing_anim_filler` | 20 (20 f × 1.0) | - | - | 1.00 / 1.25 | 0 → 76 | `0x00` |
| 63 | `GRAB_POWER_02_STRIKE_01` | `gen_grab_front_power14_p1` | 55 (41 f × 0.75) | - | W 23, E 36 | 1.08 / 2.25 | 50 | `0x00` |
| 65 | `GRAB_POWER_02_STRIKE_02` | `gen_grab_front_power14_p2` | 60 (45 f × 0.75) | - | - | 1.08 / 2.25 | 1000 | `0x00` |
| 67 | `GRAB_POWER_02_STRIKE_03` | `gen_grab_front_power2_p3` | 32 (24 f × 0.75) | - | - | 1.59 / 2.25 | 1000 | `0x00` |
| 78 | `GRAB_FRONT_SPIN_VICTIM` | `gen_grab_front_spin` | 28 (21 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 80 | `GRAB_REAR_SPIN_VICTIM` | `gen_grab_spin` | 17 (13 f × 0.75) | - | - | 0.24 / 0.30 | 0 | `0x00` |
| 147 | `THROW_01_FROM_GRAB_FRONT` | `gen_grab_front_throw_front` | 31 (23 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 66 | `0x2a` `0x100` |
| 149 | `THROW_01_FROM_GRAB_RIGHT` | `gen_grab_front_throw_right` | 31 (23 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 66 | `0x2a` `0x100` |
| 151 | `THROW_01_FROM_GRAB_REAR` | `gen_grab_front_throw_back` | 29 (22 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 66 | `0x2a` `0x100` |
| 153 | `THROW_01_FROM_GRAB_LEFT` | `gen_grab_front_throw_left` | 33 (25 f × 0.75) | - | - | 1.08 / 1.35 | 10 → 66 | `0x2a` `0x100` |
| 155 | `THROW_02_FROM_GRAB_FRONT` | `gen_grab_front_wall_front` | 72 (54 f × 0.75) | - | - | 1.63 / 2.25 | 250 → 264 | `0x2a` `0x100` |
| 157 | `THROW_02_FROM_GRAB_RIGHT` | `gen_grab_front_wall_right` | 37 (28 f × 0.75) | - | - | 0.98 / 1.55 | 250 → 264 | `0x2a` `0x100` |
| 159 | `THROW_02_FROM_GRAB_REAR` | `gen_grab_front_wall_back` | 45 (34 f × 0.75) | - | - | 1.04 / 1.25 | 250 → 264 | `0x2a` `0x100` |
| 161 | `THROW_02_FROM_GRAB_LEFT` | `gen_grab_front_wall_left` | 39 (29 f × 0.75) | - | - | 0.45 / 1.55 | 250 → 264 | `0x2a` `0x100` |
| 118 | `GRAB_MOUNT` | `gen_grab_front_to_mount1` | 65 (49 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 95 | `GRAB_FRONT_BREAK_REACT` | `gen_grab_front_break_react` | 27 (20 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 657 | `SPECIAL_ATTACK2_FRONT` | `gen_grab_n_attack_front` | 34 (34 f × 1.0) | - | - | 1.14 / 2.50 | 60 | `0x00` |
| 659 | `SPECIAL_ATTACK2_REAR` | `gen_grab_n_attack_back` | 38 (38 f × 1.0) | - | - | 0.80 / 2.50 | 60 | `0x00` |
| 649 | `RAGE_ATTACK2_FRONT` | `gen_grab_n_attack_front` | 34 (34 f × 1.0) | - | - | 1.14 / 2.50 | 60 | `0x00` |
| 651 | `RAGE_ATTACK2_REAR` | `gen_grab_n_attack_back` | 38 (38 f × 1.0) | - | - | 0.80 / 2.50 | 60 | `0x00` |

Tackles and the mount ([Combat](combat-grabs.md#mount)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 4 | `TACKLE_PLAYER_INTRO` | `gen_tackle_start` | 7 (5 f × 0.75) | - | - | 1.82 / 3.00 | 0 | `0x00` |
| 3 | `TACKLE_INTRO` | `AI_tackle_start` | 19 (14 f × 0.75) | - | - | 1.82 / 3.00 | 0 | `0x00` |
| 2 | `TACKLE_MISS` | `gen_tackle_miss` | 59 (44 f × 0.75) | all 0-17 | - | 1.00 / 1.25 | 10 | `0x22` |
| 5 | `TACKLE_HIT_FROM_FRONT` | `gen_tackle_front` | 45 (34 f × 0.75) | - | - | 1.01 / 1.50 | 10 | `0x16` |
| 7 | `TACKLE_HIT_FROM_REAR` | `gen_tackle_back` | 34 (34 f × 1.0) | - | - | 1.04 / 1.50 | 10 | `0x16` |
| 219 | `MOUNT_COMBO_STRIKE_01` | `gen_mounting_punch1` | 16 (12 f × 0.75) | - | - | 0.12 / 0.15 | 10 → 61 | `0x00` |
| 221 | `MOUNT_COMBO_STRIKE_02` | `gen_mounting_punch2` | 19 (14 f × 0.75) | - | - | 0.12 / 0.15 | 10 → 61 | `0x00` |
| 223 | `MOUNT_COMBO_STRIKE_03` | `gen_mounting_punch4` | 28 (21 f × 0.75) | - | - | 0.12 / 0.15 | 10 → 61 | `0x00` |
| 225 | `MOUNT_POWER_01_STRIKE_01` | `gen_mount_power10_p1` | 80 (60 f × 0.75) | - | W 24, E 44 | 0.12 / 2.25 | 35 → 79 | `0x00` |
| 227 | `MOUNT_POWER_01_STRIKE_02` | `gen_mount_power10_p2` | 60 (45 f × 0.75) | - | use 12 | 0.12 / 0.15 | 50 → 79 | `0x00` |
| 229 | `MOUNT_POWER_01_STRIKE_03` | `missing_anim_filler` | 20 (20 f × 1.0) | - | - | 1.00 / 1.25 | 0 → 89 | `0x00` |
| 231 | `MOUNT_POWER_02_STRIKE_01` | `gen_mount_power13_p1` | 51 (38 f × 0.75) | - | W 19, E 32 | 0.12 / 2.25 | 69 | `0x00` |
| 233 | `MOUNT_POWER_02_STRIKE_02` | `gen_mount_power13_p2` | 65 (49 f × 0.75) | - | - | 0.12 / 0.15 | 1000 | `0x00` |
| 235 | `MOUNT_POWER_02_STRIKE_03` | `gen_mount_power6_p3` | 40 (30 f × 0.75) | - | - | 0.36 / 0.45 | 1000 | `0x00` |
| 248 | `MOUNT_PICKUP` | `gen_mounting_to_grab` | 36 (27 f × 0.75) | - | - | 0.12 / 0.15 | 0 | `0x00` |
| 244 | `MOUNT_RELEASE` | `gen_mounting_release` | 25 (19 f × 0.75) | - | - | 0.12 / 0.15 | 0 | `0x00` |
| 250 | `MOUNT_STRUGGLE` | `gen_mounted_struggle1` | 16 (12 f × 0.75) | - | - | 1.00 / 1.25 | 10 → 40 | `0x00` |
| 242 | `MOUNT_REVERSAL` | `gen_mounted_reversal` | 57 (43 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 246 | `MOUNT_BREAK` | `gen_mounted_escape` | 48 (36 f × 0.75) | R forearm 5-11 | - | 1.00 / 1.25 | 0 | `0x00` |
| 214 | `MOUNTING_GRAB` | `gen_grab_mounting` | 40 (30 f × 0.75) | R hand 15-20 | - | 1.48 / 1.50 | 0 | `0x00` |

The held victim's moves and the counters:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 96 | `GRAB_FRONT_STRUGGLE1` | `gen_grab_front_struggle` | 23 (17 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 108 | `GRAB_REAR_STRUGGLE1` | `gen_grab_back_struggle2` | 29 (22 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 104 | `GRAB_FRONT_ATTACK` | `gen_grab_front_victim_strike` | 33 (25 f × 0.75) | foot B 11-19 | - | 1.00 / 1.25 | 20 | `0x26` stun |
| 116 | `GRAB_REAR_ATTACK` | `gen_grab_back_victim_strike` | 44 (33 f × 0.75) | shin A, foot A, shin B, foot B 11-16 | - | 0.84 / 1.05 | 20 | `0x3a` stun |
| 100 | `GRAB_FRONT_ESCAPE1` | `gen_grab_front_escape1` | 61 (46 f × 0.75) | - | - | 1.08 / 1.35 | 20 | `0x2a` `0x100` |
| 112 | `GRAB_REAR_ESCAPE1` | `gen_grab_back_escape_throw` | 56 (42 f × 0.75) | - | - | 0.24 / 0.30 | 20 | `0x2a` `0x100` |
| 90 | `GRAB_FRONT_REVERSAL` | `gen_grab_front_reversal` | 44 (33 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 92 | `GRAB_REAR_REVERSAL` | `gen_grab_back_reversal` | 44 (33 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 76 | `GRAB_FRONT_COUNTER` | `gen_grab_front_counter` | 36 (27 f × 0.75) | all 0-35 | - | 1.00 / 2.00 | 100 | `0x16` |
| 9 | `TACKLE_FRONT_COUNTER` | `gen_tackle_counter` | 57 (43 f × 0.75) | all 0-56 | - | 1.12 / 2.00 | 100 | `0x16` |

Tandems:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 163 | `TANDEM_01_GRABBER_INTRO` | `gen_tandem1_grabber_begin` | 36 (27 f × 0.75) | - | - | 0.23 / 0.29 | 0 | `0x00` |
| 164 | `TANDEM_01_VICTIM_INTRO` | `gen_tandem1_victim_begin` | 36 (27 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 165 | `TANDEM_01_ATTACKER_INTRO` | `gen_tandem1_attacker_begin` | 36 (27 f × 0.75) | - | - | 1.72 / 2.50 | 50 | `0x00` |
| 166 | `TANDEM_01_GRABBER_END` | `gen_tandem1_grabber_end` | 27 (20 f × 0.75) | - | - | 1.08 / 1.35 | 0 | `0x00` |
| 167 | `TANDEM_01_VICTIM_END` | `gen_tandem1_victim_end` | 37 (28 f × 0.75) | - | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 168 | `TANDEM_01_ATTACKER_END` | `gen_tandem1_attacker_end` | 36 (27 f × 0.75) | - | - | 0.47 / 0.59 | 350 | `0x00` |
| 169 | `TANDEM_02_GRABBER_INTRO` | `gen_tandem2_grabber_begin` | 47 (35 f × 0.75) | - | - | 0.24 / 0.30 | 0 | `0x00` |
| 170 | `TANDEM_02_VICTIM_INTRO` | `gen_tandem2_victim_begin` | 47 (35 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 171 | `TANDEM_02_ATTACKER_INTRO` | `gen_tandem2_attacker_begin` | 47 (35 f × 0.75) | - | - | 1.46 / 2.00 | 50 | `0x00` |
| 172 | `TANDEM_02_GRABBER_END` | `gen_tandem2_grabber_end` | 41 (31 f × 0.75) | - | - | 0.44 / 0.56 | 0 | `0x00` |
| 173 | `TANDEM_02_VICTIM_END` | `gen_tandem2_victim_end` | 53 (40 f × 0.75) | - | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 174 | `TANDEM_02_ATTACKER_END` | `gen_tandem2_attacker_end` | 47 (35 f × 0.75) | - | - | 1.29 / 1.62 | 350 | `0x00` |
| 175 | `TANDEM_03_GRABBER_INTRO` | `gen_tandem3_grabber_begin` | 11 (8 f × 0.75) | - | - | 0.24 / 0.30 | 0 | `0x00` |
| 176 | `TANDEM_03_VICTIM_INTRO` | `gen_tandem3_victim_begin` | 11 (8 f × 0.75) | all 0-11 | - | 1.00 / 1.25 | 0 | `0x00` |
| 177 | `TANDEM_03_ATTACKER_INTRO` | `gen_tandem3_attacker_begin` | 11 (8 f × 0.75) | - | - | 1.69 / 2.80 | 50 | `0x00` |
| 178 | `TANDEM_03_GRABBER_END` | `gen_tandem3_grabber_end` | 43 (32 f × 0.75) | all 0-32 | - | 0.35 / 0.44 | 0 | `0x00` |
| 179 | `TANDEM_03_VICTIM_END` | `gen_tandem3_victim_end` | 63 (47 f × 0.75) | all 0-40 | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 180 | `TANDEM_03_ATTACKER_END` | `gen_tandem3_attacker_end` | 40 (30 f × 0.75) | - | - | 0.99 / 1.24 | 350 | `0x00` |
| 181 | `BAT_TANDEM_01_GRABBER_INTRO` | `gen_tandem_bat_grabber_begin` | 13 (10 f × 0.75) | - | - | 0.24 / 0.30 | 0 | `0x00` |
| 182 | `BAT_TANDEM_01_VICTIM_INTRO` | `gen_tandem_bat_victim_begin` | 13 (10 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 183 | `BAT_TANDEM_01_ATTACKER_INTRO` | `gen_tandem_bat_attacker_begin` | 13 (10 f × 0.75) | - | - | 1.27 / 2.50 | 50 | `0x00` |
| 184 | `BAT_TANDEM_01_GRABBER_END` | `gen_tandem_bat_grabber_end` | 67 (50 f × 0.75) | - | - | 0.31 / 0.39 | 0 | `0x00` |
| 185 | `BAT_TANDEM_01_VICTIM_END` | `gen_tandem_bat_victim_end` | 67 (50 f × 0.75) | - | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 186 | `BAT_TANDEM_01_ATTACKER_END` | `gen_tandem_bat_attacker_end` | 67 (50 f × 0.75) | - | - | 1.57 / 1.96 | 500 | `0x00` |
| 187 | `KNIFE_TANDEM_01_GRABBER_INTRO` | `gen_tandem_knife_grabber_begi` | 68 (51 f × 0.75) | - | - | 0.23 / 0.29 | 0 | `0x00` |
| 188 | `KNIFE_TANDEM_01_VICTIM_INTRO` | `gen_tandem_knife_victim_begin` | 68 (51 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 189 | `KNIFE_TANDEM_01_ATTACKER_INTRO` | `gen_tandem_knife_attacker_beg` | 68 (51 f × 0.75) | - | - | 1.44 / 2.50 | 50 | `0x00` |
| 190 | `KNIFE_TANDEM_01_GRABBER_END` | `gen_tandem_knife_grabber_end` | 17 (13 f × 0.75) | - | - | 0.26 / 0.32 | 0 | `0x00` |
| 191 | `KNIFE_TANDEM_01_VICTIM_END` | `gen_tandem_knife_victim_end` | 59 (44 f × 0.75) | - | KD 0 | 1.00 / 1.25 | 0 | `0x00` |
| 192 | `KNIFE_TANDEM_01_ATTACKER_END` | `gen_tandem_knife_attacker_end` | 21 (16 f × 0.75) | - | - | 0.98 / 1.23 | 600 | `0x00` |

Stealth kills:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 637 | `STEALTH_KILL` | `player_stealthkill_weak` | 52 (39 f × 0.75) | - | - | 0.74 / 1.50 | 3000 | `0x00` |
| 639 | `STEALTH_KNIFE_KILL` | `player_stealthkill_weak` | 52 (39 f × 0.75) | - | - | 0.74 / 1.50 | 3000 | `0x00` |
| 641 | `STEALTH_BATON_KILL` | `player_stealthkill_weak` | 52 (39 f × 0.75) | - | - | 0.74 / 1.50 | 3000 | `0x00` |

Weapons ([Combat](combat-throws.md#armed-moves)):

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 34 | `BAT_COMBO_S1` | `gen_weapon01_atk01` | 37 (28 f × 0.75) | R hand 9-17 | duck 8, E 13, R 25 | 1.56 / 2.10 | 10 | `0x29` |
| 35 | `BAT_COMBO_SS2` | `gen_weapon01_atk02` | 32 (24 f × 0.75) | R hand 9-13 | duck 8 | 1.78 / 2.10 | 10 | `0x2b` |
| 36 | `BAT_COMBO_X1` | `gen_weapon01_slash_atk01` | 35 (26 f × 0.75) | R hand 11-17 | E 20, R 23 | 1.24 / 1.50 | 130 | `0x2a` |
| 37 | `BAT_COMBO_GROUNDED_STRIKE_01` | `gen_weapon01_grnd_atk01` | 29 (22 f × 0.75) | R hand 11-17 | - | 0.97 / 1.50 | 75 | `0x0a` |
| 38 | `BAT_COMBO_GROUNDED_STRIKE_02` | `gen_weapon01_grnd_atk02` | 25 (19 f × 0.75) | R hand 7-15 | - | 0.97 / 1.50 | 75 | `0x0a` |
| 39 | `BATON_COMBO_S1` | `baton_1` | 21 (16 f × 0.75) | R hand 1-9 | duck 2, W 9, E 11 | 1.27 / 1.50 | 10 | `0x0a` |
| 40 | `BATON_COMBO_SS2` | `baton_2` | 17 (13 f × 0.75) | R hand 0-8 | duck 0 | 1.08 / 1.50 | 10 | `0x1a` stun |
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
| 473 | `ONE_HANDED_OBJECT_SMASH_FRONT` | `bottle_smash_attacker` | 45 (34 f × 0.75) | - | - | 1.27 / 2.00 | 120 | `0x00` |
| 475 | `ONE_HANDED_OBJECT_SMASH_REAR` | `bottle_smash_attacker` | 45 (34 f × 0.75) | - | - | 1.27 / 2.00 | 120 | `0x00` |
| 505 | `BARREL_THROW` | `gen_2hand_throw_fwd` | 30 (30 f × 1.0) | R hand 14-19 | - | 0.91 / 1.14 | 30 | `0x1a` |
| 506 | `BARREL_THROW_FROM_WALK` | `gen_2hand_throw_walk` | 26 (26 f × 1.0) | L hand, R hand 8-16 | - | 1.00 / 1.25 | 30 | `0x1a` |
| 507 | `BARREL_THROW_FROM_RUN` | `gen_run_2hand_weapon_throw` | 30 (30 f × 1.0) | L hand, R hand 10-14 | - | 1.00 / 1.25 | 30 | `0x1a` |
| 467 | `ONE_HANDED_OBJECT_THROW` | `gen_1hand_throw` | 25 (19 f × 0.75) | R hand 5-11 | - | 1.02 / 1.28 | 10 | `0x0a` |
| 471 | `ONE_HANDED_OBJECT_THROW_FROM_WALK` | `gen_1hand_throw_walk` | 31 (23 f × 0.75) | R hand 13-19 | - | 1.00 / 1.25 | 10 | `0x0a` |
| 472 | `ONE_HANDED_OBJECT_THROW_FROM_RUN` | `gen_1hand_throw_run` | 33 (25 f × 0.75) | R hand 13-19 | - | 1.00 / 1.25 | 10 | `0x0a` |
| 551 | `GHETTO_THROW` | `ghetto_throw` | 27 (20 f × 0.75) | 0 11-17 | - | 0.85 / 1.07 | 30 | `0x1a` |
| 552 | `GHETTO_THROW_FROM_WALK` | `ghetto_throw_walk` | 47 (35 f × 0.75) | L hand, R hand 12-16 | - | 1.00 / 1.25 | 30 | `0x1a` |
| 553 | `GHETTO_THROW_FROM_RUN` | `ghetto_throw_run` | 27 (20 f × 0.75) | R hand 7-11 | - | 1.00 / 1.25 | 30 | `0x1a` |
| 491 | `KNIFE_THROW` | `gen_knifethrow` | 37 (28 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 502 | `SWINGABLE_OBJECT_THROW` | `gen_batthrow` | 35 (26 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 629 | `BAT_BLOCK_REACT` | `gen_batblock_dodge_atk` | 21 (16 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |

Block, rage and the rest:

| Id | Name | Clip | Updates (frames × rate) | Strike shapes (updates on-off) | Events (updates) | Reach / far (m) | Damage (file → Rembrandt) | Hit code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 643 | `RAGE_START` | `gen_rage_enter05` | 64 (48 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 665 | `SPECIAL_FLASH` | `gen_flash_use` | 53 (40 f × 0.75) | - | use 25 | 1.00 / 1.25 | 0 | `0x00` |
| 616 | `BLOCK_DODGE` | `gen_duck` | 28 (21 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 605 | `BLOCK_START` | `gen_blockA` | 7 (5 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 606 | `BLOCK_SUSTAIN` | `gen_blockA_idle` | 40 (30 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 607 | `BLOCK_SHUFFLE` | `gen_block_turn` | 12 (9 f × 0.75) | - | - | 1.00 / 1.25 | 0 | `0x00` |
| 664 | `SPECIAL_SPRAY` | `gen_spray` | 30 (30 f × 1.0) | R hand 10-19 | use 14 | 0.94 / 1.80 | 80 | `0x1a` stun |

## Behaviour

### Timing: when a move hits, and what it takes {#timing}

Start here to build the fight's feel. Updates (k) are 1/30 s from the clip's first update. Confirmed (code) at
the addresses given; the phase updates are confirmed (runtime) for 12, 11, 13, 16 and 20 ([When input and the stick
come back](combat/index.md#input-return)) and come from the clips' events for the rest ([Reading the tables](#reading)).

**A hit has no fixed frame.** No attack start deals damage (`Attack_Start` `0x002625a8`, `Attack_StartSolo`
`0x00262368`). Damage comes only from `Strike_Contact` (`0x0021b290`), called by the strike test that runs every
update after the move while one of the clip's **strike shapes** is on: the first update a shape overlaps the target's
spine or head shape ([How a moving attack strikes](combat/index.md#moving-strikes)). The same body is struck once per
window. So the hit update depends on the distance and the steer, and a strike that never touches misses. The
"contact" column is that update measured with the target standing near the move's reach; the shape window is the
range a target can be hit in. The paired moves (grab strikes, power moves, counters, tandems, stealth and knife kills)
are the exception: their damage is applied by the pair's own code, mostly on the first update.

| Id | Starts on | Strike shapes on-off | Contact (runtime) | Takes the next press | Window `0x2` (plays it) | End `0x4` | Recovery `0x40000` | Free again |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 12 `S1` | square pressed, the same update | L hand 1-10 | 2 (3 from 1.5 m) | 0-14 | 6-14 | 15 | 16-19 | 20 |
| 16 `SS2` | the window of 12 | L forearm, L hand 2-7 | 4 | 0-18 | 6-18 | 19 | 20-24 | 25 |
| 19 `SSS3` | the window of 16 | R hand 7-15 | 7 | none | - | 16 | 17-25 | 26 |
| 20 `SSS3_HOLD` | the window of 16, instead of 19 at random ([Combat](combat/index.md#attacks)) | shin A, foot A 15-21 | | none | - | 24 | 25-31 | 32 |
| 17 `SSX3` | the window of 16 | shin B, foot B 5-12 | 7 | none | - | 24 | 25-33 | 34 |
| 11 `X1` | cross released 1-3 updates after the press, or its 4th held update | R hand 6-11 | 8 (7 in two of three runs; 10 from 1.0-1.6 m) | 0-19 | 10-19 | 20 | 21-29 | 30 |
| 15 `SX2` | the window of 12 | R hand 6-11 | 7 | none | - | 14 | 15-19 | 20 |
| 14 `XS2` | the window of 11 | L hand 7-12 | 9 | none | - | 22 | 24-29 | 30 |
| 13 `XX2` | the window of 11 | R forearm, R hand 9-14 | 10 | none | - | 17-18 | 19-29 | 30 |
| 25 / 27 / 29 snaps | square, stick above 0.95 and more than 45° off the facing | forearm, hand 2-7 (29: 1-7) | 4 | none | - | 8 | | 16 |
| 23 from a walk | square or cross, gait 1-3, stick 0.12 or more, no stance | L forearm, L hand 5-11 | | none | - | | | 24 |
| 24 from a run | square or cross at gait 4 (cross also at a sprint) | R forearm, R hand 2-8 | | none | - | | | 21 |
| 653 special | square newly down while cross is held | L hand, R hand 2-9 | 7 | none | - | | | 27 |
| 193 / 194 | square / cross at a low target | foot B 11-13 / 11-17 | 12 / 15 | none | - | | | 27 / 32 |
| 0 charge / 1 dive | L2 held + cross / square at a run | all 4-17 / all 2-24 | | none | - | | | 27 / 61 |

"Free again" is the first update a press or the stick acts after the move: the clip's length (a run attack hands
back to the run at once). The rest of the frame data is in [Frame data](#frame-data).

#### Input, buffering and cancels {#input}

Read in Ghidra: confirmed (code) where no other level is given.

From `Commands_Match` (`0x00147940`; the tables are in [Commands](combat/index.md#commands)) and the dispatcher:

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
      table](combat/index.md#attacks), or, for a pad player's buffered snap, the snap when a snap target is within 1.3 m
      (the far range of 25) or `CfgSnap` is off; otherwise the plain square step. A step from `S1` or `X1` is not
      played while the target is low (`0x00225200`).
    - it is **emptied** by every attack start (`Attack_Start`, `Attack_StartSolo`, square and cross themselves).
      A press in the end phase `0x4` is refused by square and cross, and one in recovery `0x40000` by the
      dispatcher's gate `0x5c7fee0`: neither is kept for later.

  So the longest a press waits is from the attack's first update to its window: up to 6 updates in `S1` and `SS2`,
  10 in `X1` (inferred from the buffer rule above and the clips' window events in [Frame data](#frame-data)). There
  is no buffer across the end of a move: a press must land after it ends.
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

### Square, in order {#square}

Read in Ghidra: confirmed (code) where no other level is given.

`Player_Square` (`0x00286cc8`), unarmed (held object set 0). Confirmed (code). Refused while the state word has any
of `0x7bf9e9f4300` or record `+0x08` any of `0x100101f`; the buffered chain press and combo count are cleared first.

1. **Not in a fight stance** (state bits `0x3`) and, for a pad player, `0x0051031c` clear:
    - running freely and wanting to run (gait 4, `+0x08` clear, stick above 0.95): a low target (the `0x00225200` test,
      `Human_IsHighOrBusy`: the attacker 1.3-1.8 m above the target by `Object_HeightDiff`, or, unless he is more than
      0.7 m below it, the target in a grab, attack or down state its held and state flags name; confirmed (code)) →
      enter the stance and **193**; otherwise **24** (`Player_StartCharge`, `0x00264a80`);
    - gait 1-3 with the stick at 0.12 or more: the same low-target test → 193, otherwise **23**;
    - `0x00510270` set (written only by `Cfg_SetAttackFromIdle`, `0x002364e0`; confirmed
      (code)): **22** (`Attack_StartSolo`), or 193 at a low target, or `Player_ObjectAttack` at an object with no
      human target.
    - Otherwise the player enters the stance and goes on.
2. **Strafe attack** (pad player, locked on, speed at least the walk speed, stick above 0.95, a current target, and
   the stick within 135° of the facing): **33** within 45° of the facing, else **31** when the stick is to the
   right (angle < 0), else **32**; only when the target is within that attack's far range ([Strafe
   attacks](#strafe)). Otherwise on.
3. **Snap** (stick above 0.95, more than 45° off the facing, a snap target): 25 / 27 / 29
   ([Combat](combat/index.md#attacks)).
4. Re-pick the target (`Player_PickTarget(2.0)`, [Targets](combat-targets.md#targeting)) when a pad player has
   none, or it is farther than **2.5 m** (`0x005104c0`, 6.25 squared as `CfgDistances` sets it). No human but an
   object → `Player_ObjectAttack` ([Combat](combat-targets.md#breakables)).
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
  `SPECIAL_BREAK_OBJECT_LOW` (the `carhit_low` kick) through `Attack_StartAtTarget` (`0x00261c80`; the mid test is
  `Human_IsMidOrBusy` `0x002250a0`), a front grab 120 (confirmed (code) in `Player_Cross`), then the tandem, the
  counters 76 / 9 and **11 `X1`**.

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

Besides R1 at the catch ([Combat](combat-grabs.md#grabbed)), **square or cross** pressed while an enemy is coming in for
a grab or tackle counters it. `Human_CanCounterGrab` (`0x00258e88`): the attacker plays 69-71 (grab intro or miss),
faces the player and targets him. `Human_CanCounterTackle` (`0x002590f8`): the attacker plays 2-4. Then
`Attack_StartPaired(player, attacker, 76 or 9, 1, 0x400000)`. Pad players only; the AI's equivalent is command 3
(`Player_TryCounterGrab`, `0x0027d6e0`) with the same tests. Confirmed (code).

At runtime (`moves_grabcounter_12` and `_14`): the puppet civilian 1 m ahead was given command `0xd` at frame 10 and
played its grab intro 70; square at frame 12 or 14 (during the intro) played **76**, the grabber 77; the grabber lost
**100** health on the first update and the player's power went 400 → 300. When the enemy started grabbing, the player
showed 359 for one update. Confirmed (runtime).

### Grounded and mid strikes {#ground}

Read in Ghidra: confirmed (code) where no other level is given.

| Input | Target | Clip | Runtime (`moves_grounded_*`, victim knocked down by 653 and set 1 m ahead) | Evidence |
| --- | --- | --- | --- | --- |
| square | low (down) | **193** `gen_ground_kickB` | foot B shape k11-k13, contact k12, −34 health seen in its sample, reaction 195 | confirmed (code); confirmed (runtime) |
| cross | low (down) | **194** `gen_ground_stomp` | foot B shape k11-k17, contact k15, −34 seen in its sample, 32 updates, then 358 | confirmed (code); confirmed (runtime) |
| square | mid (the mount's states, or 0.7-1.3 m lower) | **212** `MOUNTING_STRIKE` | `moves_mid_square`: at Bum01 mounting PoizoCiv (made the target), contact on the mounter at k8; the victim was let go (245, then up with 199) | confirmed (code); confirmed (runtime) |
| cross | mid | **661** `SPECIAL_BREAK_OBJECT_LOW` | `moves_mid_cross`: the same, 661 one update after the release, contact at k9, the victim let go | confirmed (code); confirmed (runtime) |
| square or cross | grabbed from the front by someone else, or grabbing someone from the front | **120** | `moves_grab_front_strike`: at PoizoCiv holding Bum01 from the front (82), contact at k9, 50 damage, PoizoCiv 144 and the hold broken | confirmed (code); confirmed (runtime) |
| commands `0x37` / `0x38` (scripts, AI) | any | 193 / 194 (`Attack_StartGroundStrike`, `0x00287fe0`) | not run | confirmed (code) |

Both grounded strikes deal 34 (class indices 12 / 13). Confirmed (code); confirmed (runtime) for 193, 194, 212, 661
and 120. A plain `S1` at a tackled victim lying under its mounter (PoizoCiv in 207) did not touch it.

### Tandems {#tandem}

A **tandem** is a three-person move: a player strikes a victim another human holds from behind. Confirmed (code) at
`Tandem_CanStart` (`0x00258e10`), `Tandem_Start` (`0x0026f860`), `Tandem_LinkThree` (`0x0022b1a0`),
`Tandem_AlignThree` (`0x00277458`) and `Tandem_OnVictimIntroEnd` (`0x0026f780`):

- **Started from square, cross, circle tapped or circle held**, when the target is held from the rear (state `0x20`, by
  anyone: a gang member or another player), the attacker stands within 45° of the victim's front
  (`Human_GetSideOf(victim, attacker, 1)` = 0), and the victim's held flags have none of `0xc12200`. Square and cross
  test it after the target-state strikes; circle tests it before grabbing ([Combat](combat-grabs.md#grab), step 3). The
  three callers (`Player_Square`, `Player_Cross`, `Player_GrabOrTackle`) are the only ones, and none tests for a pad:
  they are the dispatcher's, which an AI human runs too ([An AI's attacks](combat-targets.md#ai-attacks)). Square and
  cross pressed **together** (command `0x22`) are not among them: that is the special 653, which hits the victim and
  breaks the hold. Every input, with runtime checks: [Which button starts it](#tandem-input).
- **The set**: a bat or baton in hand → 181-186; a knife → 187-192; otherwise 163-168, 169-174 or 175-180 at random.
  Each set has an intro and an end for the grabber, victim and attacker (ids +0 to +5).
- The three are linked and placed by the set's pair offsets. At the **end of the victim's intro** the attacker's
  intro and end values (50 + 350, or 50 + 500 with a bat, 50 + 600 with a knife) go on the victim as pending
  damage; it is lethal when `Human_IsDamageDefeating` (`0x00265c28`, confirmed (code): health ≤ damage, the damage
  tripled when cuffed) says so, and then the death reaction plays (`Human_PlayDeathReaction`).
- The grabber is let go into a fight stance afterwards. A player's tandem scores through `Grab_ScoreMove` (100
  points of category 6).

At runtime (`moves_tandem`: Ash, the player's ally, given the grab command at PoizoCiv from behind; PoizoCiv held
in 85): square played the set 1 on the next update, the player's 165 and the victim's 164 for **51** updates (not
the 36 of their clips), then 168 / 167; the victim lost **400** (50 + 350) on the first update of 167. Confirmed
(runtime).

#### Which button starts it {#tandem-input}

Read in Ghidra: confirmed (code) where no other level is given.

A player's report said the tandem is not square and cross and involves holding circle. The answer:
**any one attack button** starts it, square, cross or circle, and circle works **tapped or held**; what does not is
**square and cross together**. The game's own level80 tutorial line for the tandem (`LEVEL80.TANDEM`, shown by
`level80_chapter4.lua`'s `TandemText`) agrees: it tells the player that any attack button works on an enemy a teammate holds.

**The four inputs.** Each is one command of [the command tables](combat/index.md#commands), and `Player_UpdateActions`
(`0x0027c120`) hands it on only when the player is not busy (`Human_IsBusy`) and holds none of the held flags
`0x5c7fee0`. Confirmed (code) at the addresses cited:

| Input | Command, and the sample it comes on | Path | Where the tandem test sits |
| --- | --- | --- | --- |
| square pressed | `0xf`, the sample square goes down | `Player_Square` (`0x00286cc8`) | step 8, after the moving attacks, strafes, snaps, the target re-pick and the low, mid and front-grab strikes ([Square](#square)) |
| cross tapped or held | `0x10`, on the release of a press of 1-3 samples, or on the 4th sample held (`0x12` on the press does nothing here) | `Player_Cross` (`0x00287a18`) | the same place ([Cross](#cross)) |
| circle tapped | `0xd`, on the release of a press of 1-6 samples (`0x1e` on the press does nothing here) | `Player_GrabOrTackle` (`0x00284920`) | first: after the stance and the grounded mount, before the grab |
| circle held | `0xe`, on the 7th sample held (`0x0050b708` = 7), before any release | `Player_GrabOrTackle` | first: after the stance and the grounded mount, before the tackle |

Square and cross together are matched as the combination `0x22` (square newly down while cross is held), which the
dispatcher sends to `Player_Special` before it looks at `0xf` or `0x10`; `Player_Special` is not a caller of
`Tandem_Start` (its only callers are the three above). L2 with square or cross (`0x21` / `0x20`, the dive
and the charge) is matched first too. Confirmed (code) at `0x0027c120` and the callers of `0x0026f860`.

**When it is allowed** (`Tandem_CanStart`, `0x00258e10`, confirmed (code)): the target is in a **rear** hold (state
`0x20`; a front hold, state `0x10`, never), held by anyone; the player stands within 45° of the victim's front
(`Human_GetSideOf(victim, player, 1)` = 0); and the victim has none of the held flags `0xc12200` (already in a paired
move or tandem). The grabber's own state is not tested. There is **no distance test** in the tandem itself: the reach
is the target pick's.

- Circle: a pad player's circle always searches again (`Player_FindCounterTarget`, `0x0027ac30`, called when the brain
  type `+0x04` is 0) within 1.25 × the far range of id 3 (3.75 m) when held, or of id 70 (3.12 m) when tapped,
  first among the enemies within about 54° (`0x0051096c` = 0.9425 rad) of the aim heading (`Player_GetAimHeading`),
  then wider. So the held enemy the player faces is picked even when another enemy was his target.
- Square keeps the current target and searches only without one or when it is too far
  ([Targets](combat-targets.md#targeting)); cross always searches. A square at another, nearer target strikes that one.
- Before the tandem test, square and cross can start something else at the same press: out of the stance and
  running, the run attacks 24 / 23 (or 193 at a low target); with the stick past 0.95, the strafes 31-33 (L1 locked)
  and the snaps 25 / 27 / 29; at a front-held target, 120. Circle has none of these: it puts the player in the stance
  and goes to the tandem test. **Inferred**: this is why circle is the dependable button in play, from a run or with
  the stick pushed, which matches the player's report that circle works; square and cross do start it when the
  player stands in front of the held enemy with the stick near the centre (the runs below).

**At runtime** (confirmed (runtime), PCSX2 2.9.94, the `moves_tandem` set-up: slot 6, PoizoCiv (600 health) in Ash's
rear hold (85), the player 1.1 m in front of him facing him, stick centred, in the stance's idle 388; hook
`anim-dispatch`; one scenario per input, the press at script frame 60, which update 61 reads):

| Scenario | Input | Commands seen (update) | Result |
| --- | --- | --- | --- |
| `moves_tandem` | square tapped at 60 | `0xf` (61) | the tandem at 61: player 165, PoizoCiv 164, Ash 163 |
| `moves_tandem_cross` | cross tapped at 60 | `0x12` (61), `0x10` (62) | the tandem at 62 |
| `moves_tandem_circle_tap` | circle tapped at 60 | `0x1e` (61), `0xd` (62) | the tandem at 62 |
| `moves_tandem_circle_hold` | circle held from 60 to 72 | `0x1e` (61), `0xe` (67) | the tandem at **67**, the 7th update held, five before the release; the release gave no `0xd` |
| `moves_tandem_square_cross` | cross held from 60 to 64, square tapped at 61 | `0x12` (61), `0x22` (62), `0x16` (63), `0x10` (64) | **no tandem**: 653 `SPECIAL_ATTACK1_FRONT` at 62; at 69 PoizoCiv lost 6 (600 → 594) and played 133, Ash 107 `GRAB_REAR_BREAK_REACT` then 268: the hold was broken. The later `0x10` found the player busy |

Each of the four tandems played the first set, 163-168. Confirmed (runtime).

**To script it** (on the original, through the `scripted-pad` patch; confirmed (runtime) by the runs above, PCSX2
2.9.94): the second button byte `0x005de3ab` is active low, so circle alone is `0xdf`, cross `0xbf`, square `0x7f`,
cross and square `0x3f`, none `0xff`; the first byte `0x005de3aa` stays `0xff` and the four stick bytes
`0x005de3ac`-`0x005de3af` stay `0x80`. A circle hold is `0xdf` written for at least 7 updates (the tandem starts on the
7th); a circle tap is `0xdf` for 1-6 updates, then `0xff`.

#### Who starts a tandem, and how an AI chooses it {#tandem-ai}

A gang member holds an enemy and the teammates stand by: who of them strikes. Confirmed (code) at the addresses cited
unless marked:

1. **An AI teammate starts it through the grab.** The fight goal's attack pick (`Brain_PickAttack`, `0x0028e708`,
   [AI](ai.md#pick-attack)) adds **+200** to kind 22 (the grab) when the target is grabbed from the rear (step 4.7),
   and more when several attackers share him (step 4.5). `Human_CanStartAttack` (`0x00224778`) allows kind 22 at a
   target in state `0x20` **only from in front of him** (`0x002672d0`), the same 45° front cone `Tandem_CanStart`
   asks, and refuses a target holding `0xc12200`'s overlap `0xc08200` (a victim already in a paired move). The
   attack action writes command `0xd` (circle tapped, [AI](ai.md#attack-action)); the dispatcher's
   `Player_GrabOrTackle` (`0x00284920`) keeps the brain's target (it searches only for brain type 0), finds
   `Tandem_CanStart` true and calls `Tandem_Start(AI, victim, grabber)`. So the AI's "grab" at a rear-held enemy
   **is** the tandem. Each link is confirmed (code); the whole is confirmed (runtime) below.
2. **Who holds.** Only a rear hold allows it: the player's own rear hold (after R1's spin, or a grab that connects
   from behind) or an AI's. A front-held enemy refuses kind 22 (state `0x10` is in the grab's refusing mask
   `0x7bfdc8f7fd0`); teammates hit him with ordinary strikes instead, and a third human's hit breaks the hold
   ([Attacking a busy target](combat-reactions.md#busy-target)).
3. **The player as the attacker** starts it with square, cross or circle at a rear-held enemy he faces.
4. **What the three play**: the set by the attacker's held object (a bat or baton 181-186, a knife 187-192, else
   163-168, 169-174 or 175-180 at random); held `0x800000` on the attacker and the grabber, `0x400000` on the victim
   (so no one else starts an attack on any of the three, [Moves a hit cannot
   stop](combat-reactions.md#protected-moves)); the grabber is let go into the fight stance at the end. The victim's
   damage (the attacker's intro and end values) is pending until his intro ends; lethal, his death reaction plays from
   the end clip.
5. **Scoring**: `Grab_ScoreMove` for the grabber (the set's base id) and the attacker (base + 2), skipped when the
   victim has human flag `0x10` (the demi-god floor reached).

**An AI tandem at runtime** (confirmed (runtime), PCSX2 2.9.94, scenario `moves_tandem_ai`: a copy of slot 6, the
player grabs PoizoCiv (600 health) 1.0 m ahead with circle at update 10 and spins him to the rear hold with R1 at 45
(78 / 79, then 84 / 85 from 74); at 74-78 Ash is placed 1.3 m in front of PoizoCiv facing him, his brain made a
type-2 brain (below), the two gangs made enemies and `GoalFight(Ash, PoizoCiv)` called; stick centred; hooks
`set-command`, `anim-dispatch`, `call-brains`):

| Update | Ash (the AI) | The player (grabber) | PoizoCiv |
| --- | --- | --- | --- |
| 79-84 | the fight goal's walk-in (372 / 379), state `0x3` | rear hold 84 | 85 |
| 85 | **command `0xd`** written by `AttackAction_Start` (`0x002fa9a8`, at `0x002fac98`); **165**, state `0x1003` | **163**, state `0x1005`, held `0x800000` | **164**, state `0x182001` |
| 121 | the end clip 168 | 166 | 167; **400 damage** (600 → 200) at the intro's end |
| 169 | | let go into the fight stance (358, state `0x5`) | |
| 178-181 | 358, then on with the fight | | 355, then the grounded idle 196 |
| 190 | command `0x37`: the grounded strike 193 (20 damage) | | |

So the AI's attack action does pick the tandem at a rear-held enemy it faces, through command `0xd`, and the set was
163-168 (the unarmed first set) on the first try. Step 1's chain is confirmed (runtime) end to end, with the timings
of step 4: the intro runs 36 updates, the damage lands at its end, the grabber is let go 48 updates after that.

Caveat, open: Ash's own brain in this state is type 3 (the Warriors' think, `0x003052f0`); with it, `GoalFight`
pushed the goals but Ash stood idle for 180 updates and wrote no command, so the run gave him the type-2 handlers
(as `civ_approach` does for PoizoCiv). Whether a Warrior teammate's own think reaches the attack action against a
rear-held enemy (in a real fight, or after an `IssueWarriorCommand` attack order) was not watched.

### Stealth: hiding and the stealth kill {#stealth}

Read in Ghidra: confirmed (code) where no other level is given.

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

Read in Ghidra: confirmed (code) where no other level is given.

`Player_UpdateTackled` (`0x0027f1a0`) runs for a mounted player (state `0x800`), after the gate `0x5c7eee0`.
Confirmed (code); confirmed (runtime) below:

| Input | Effect |
| --- | --- |
| square or cross (`0xf`, `0x11`, `0x12`, `0x10`) | while `Grab_CanStruggle` (the victim not hurt, the mounter not raging, the victim's power above max / 6) and the victim's power fraction is above 1 / the mounter's class divisor: the **mounter** loses 1 / the victim's divisor of its power; then, unless the victim's record `+0x08` has any of `0x5c7eae1`, **250** `MOUNT_STRUGGLE` (`Mount_Strike`). A press during 250 drains the mounter without a new clip |
| circle (`0x1e`, `0xd`, `0xe`) | with the victim's human flag `0x2000000000` clear and `Grab_CanEscape(mounter)`: `Mount_GetOff` (`0x00271470`), which plays **246** `MOUNT_BREAK` / 247 |
| R1 (command 3, `0x00510988`) or `0x19` | with the mounter's flag `0x40` and the victim's `0x100000000` clear and `Grab_CanEscape(mounter)`: the **reversal** (`Mount_StartReversal`, `0x002723e8`): 242 `MOUNT_REVERSAL` / 243, then the roles swap into 210 / 207 with the victim on top; statistics event 4 |

`Grab_CanEscape` (`0x00225830`, confirmed (code): at or below a quarter of the mounter's maximum power, half
when hurt, always; above it, a 1 in floor(power / that) chance per press; never while the mounter rages) makes the
mounter's power the gate: struggle first, then circle or R1. The AI does the same through its commands.

Confirmed (runtime) (`moves_mounted_square`, `_r1`, `_circle`; the puppet civilian at power 400 given the tackle command
`0xe` 1.5 m away): its intro 3 ran 19 updates, then the player played 6 for 46 updates (every press refused) under
the puppet's 5, then the mounted idles 207 (victim) and 210 (mounter). Square played 250 on the next update for 16
updates, the mounter 251, and the mounter lost **40** health on 250's second update. Squares every 4 updates (two
250s, the presses between them draining) ended in 246 / 247 (48 updates) 2 updates after the 6th press, and the
player was free. After one 250, R1 every 3 updates gave the reversal 242 / 243 on the 3rd press (57 updates, then
210 / 207 with the player on top); circle every 3 updates gave 246 / 247 on the 6th press.

### Commands only scripts and the AI send {#ai-commands}

Read in Ghidra: confirmed (code) where no other level is given.

| Command | Handler | Move |
| --- | --- | --- |
| 3 | `Player_TryCounterGrab` (`0x0027d6e0`) | the AI's counter: 76 against a grab intro, 9 against a tackle intro ([Counters](#counters)) |
| `0x36` | `Player_OnCommand36` (`0x0027de78`) | the push 21 `ATTACK_PUSH` through `Attack_StartAtTarget` (`0x00261c80`), holding flag `0x10` |
| `0x37` / `0x38` | `Attack_StartGroundStrike` (`0x00287fe0`) | 193 / 194 at the target |
| `0x39` | `Player_ArmedSpecial` | throw the melee weapon (as `0x22`) |
| `0x33` | `Player_OnCommand33` (`0x00281188`) | the pick-up search |
| `0x32` | `Player_OnCommand32` (`0x002832c8`) | uncuff with a key |
| `0x27` | `Player_TryRevive` (`0x00286ba8`) | revive a downed Warrior with a flash: 351 then 353 on the reviver, 354 on the revived ([Combat](combat-reactions.md#revive)); also d-pad left in the street's tables |
| 5 (as L2 held) | `Player_UpdateGrabbing` / `Player_UpdateMounting` (`PlayerCmd_IsLetGo` `0x0027bf08`) | in a grab, let go (95 / 94); in the mount, get off (244 / 245, the victim rises with 199). The Grabbing and Mounting reaction goals send it every update near a train ([AI](ai.md#fight-reactions)) |
| `0x19` | `Player_UpdateGrabbing` (`PlayerCmd_IsGrabSpin` `0x0027bd98`, as R1 pressed) | spin the grab: front to rear 78 / 79, rear to front 80 / 81 ([Combat](combat-grabs.md#grabbing)). The Grabbing goal sends it to turn a front-held victim to his attackers |

Confirmed (code).

### Runtime results {#runtime}

Measured in PCSX2: confirmed (runtime) for every row (PCSX2 2.9.94, no run record: [How this was
checked](#how-this-was-checked)). k counts from the clip's first update.

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

Each contact falls in the update whose sample first shows the damage, and every measured window matches the frame
tables to the update ([Reading the tables](#reading)), including the late ties: 25 / 27 / 29 off (frame 5), 23 / 33 off
(frame 8) and 193 / 194 on (frame 8). Confirmed (runtime).

**Damage as the blow lands.** With a pedestrian made 1.0 m in front of the player, facing him, the damage showed
2 updates after `S1` started (17), 4 after `SS2` (36), 7 after the kick `SSX3` (61) and 9 after `X1` (26); the grab
strike 53 took its 57 on the update it started. In a real-time capture of `S1`, the fist reached the pedestrian's
face 4 frames (2 updates) after the punch first moved on screen, the same 2 updates the memory showed from the anim id
to the health drop; the picture ran about 8 frames behind the memory. So the health goes as the punch lands, not when
it starts. Confirmed (runtime), run r-20261025-strike-contact-timing.

## In Coney

The moves Coney plays and where they differ from the table above: [Combat differences](combat-diffs.md).

A free attack of a human with strike shapes takes the victim's health from the strike test, on the first update a
shape touches him (`Human::testStrikes`, `Fighter::strikeContact`, after `Strike_Contact` `0x0021b290`, confirmed
(code)). With a fighter 1.0 m ahead in `level99` its damage shows 3 updates into `S1`, 4 into `SS2`, 7 into `SSX3`, 8
into `X1` and 7 into 653, each within one update of the original (`repo:tests/platform/disc_strike_contact_test.cpp`).

### The tandem {#tandem-coney}

The [tandem runs](#tandem-input) script the same pad bytes from a Coney input script: `60 tap circle`, or `60 press
circle` and `72 release circle`; the negative test is `60 press cross`, `61 tap square`, `64 release cross`, which must
play 653 and no tandem.

The fix list (item 18), then what Coney builds:

1. Build the tandem ([combat-diffs](combat-diffs.md) lists it as not built): `Tandem_CanStart` and `Tandem_Start` as
   above, three linked bodies placed by the set's pair offsets, the damage at the victim's intro end, the grabber
   released into the fight stance.
2. Route an attack command (square, cross, circle tap or hold) from **any** fighter, player or AI, through the tandem
   test before the grab, as the original's dispatcher does.
3. AI: kind 22 at a rear-held target already passes (`ai::targetViewOf` leaves the rear hold out of
   `grabRefused`, and `attack_choice.cpp` checks the front cone). Two gaps remain: set `heldC12200` in the target
   view from the held flags `0xc12200` (a **Coney stand-in** today: never set, always false), and let the queued
   grab become the tandem instead of an ordinary grab. A front-held target stays refused.
4. The held flags of the three (`0x800000` / `0x400000`) feed the start guard of [Moves a hit cannot
   stop](combat-reactions.md#protected-moves), so others leave them alone.
5. Test: an AI ally in front of the player's rear hold picks kind 22 and starts the tandem (assert the three clips
   and the victim's damage at the intro end); in front of a front hold it does not.

Coney plays the tandem from `repo:src/combat/tandem.h` (the start test, the set, the damage and
`Human_IsDamageDefeating`) and `repo:src/human/fighter_tandem.cpp` (the three humans). Items 1, 2 and 4 of the fixes
above are built; item 3 (the AI's kind 22 becoming the tandem, and the `heldC12200` view) is left to issue #84. Any
fighter's square, cross, circle tapped or circle held at a target that `Tandem_CanStart` allows starts it, a player's
or an AI's. Where the page is silent, these are **Coney's choices**:

- **The target.** Square and cross search within 3 m, circle within its grab's or tackle's search range; the current
  target when it qualifies, else the nearest qualifying one. The original's target rules for a held victim are not
  traced (open question below).
- **Who moves.** Only the attacker is aligned: he turns and slides, over the share of his intro's time a grab's
  connect uses, to where the victim stands at his intro's pair point (the Anim Range List direction × reach, straight
  ahead at the reach without one). The victim keeps the rear hold's offset from his grabber.
- **The grabber joins on his own next update**, through a request the victim keeps, rather than through a link to the
  grabber, so a grabber removed meanwhile cannot leave a dangling link. With the humans' alternating order, all three
  clips start on the same step.
- **The let-go.** The grabber lets go at the end of his own end clip (27 updates for the first set), not the 48
  updates after the damage measured at runtime (open question below).
- **The damage** lands when the victim's intro hands over to his end clip, as a hit with no reaction. A defeating hit
  takes all his health, and his death waits until his clips no longer hold `0xc12200`
  ([Moves a hit cannot stop](combat-reactions.md#protected-moves), item 4).
- **The empty-handed set** is one of the three drawn evenly from the fighter's own random numbers.
- **No scoring.** `Grab_ScoreMove` is not called: its "100 points of category 6" has no counterpart in Coney's
  player statistics, which have categories 0-5 (open question below).
- **No other command.** The attacker and the grabber take no command until their tandem clips are over.

## Open questions

- **The low and mid states**{#q-low-mid-states}: the low and mid tests' state bits are read but not all named; which
  reactions put a human in them, and which point the height test compares (vtable `+0xac`).
- **The right snap's miss**{#q-snap-right}: why the snap 25 slides the player 0.3-0.5 m away from a target pinned on its
  right and misses, when 27 hits one on the left. That target was pinned while walking, so the steer's lead may explain
  it ([Contact](combat-targets.md#reach)); not rerun against a still target.
- **The tandem intro's length**{#q-tandem-intro}: why the tandem intro ran 51 updates, not 36.
- **The tandem's alignment**{#q-tandem-align}: `Tandem_AlignThree` (`0x00277458`) turns the three and slides two of
  them over 1/15 s to the set's offsets ([Map](#original-structure)); which body each turn and slide applies to is
  inferred, not read, since the decompile does not show the calls' first arguments. Coney moves only the attacker, to
  his intro's pair point ([Coney's tandem](#tandem-coney)).
- **The tandem's let-go**{#q-tandem-letgo}: when the tandem's grabber lets go: 48 updates after the damage at runtime,
  where his end clip (166) lasts 27. Coney lets go at the end of the clip.
- **The empty-handed set**{#q-tandem-set}: how the empty-handed set is drawn: whether the three sets are equally likely,
  and from which random source.
- **The tandem's score**{#q-tandem-score}: what `Grab_ScoreMove`'s "100 points of category 6" adds to: Coney's player
  statistics have categories 0-5, so the tandem scores nothing in Coney yet.
- **The tandem's target**{#q-tandem-target}: the target a held victim is found by: `Player_PickTarget` and the grab's
  search skip held humans in Coney; which search square, cross and circle use to reach a rear-held victim for the tandem
  was not traced.
- **A Warrior's grab in a fight**{#q-warrior-grab}: whether a Warrior who grabs an enemy in a real fight holds him from
  the rear (where a tandem is possible) or from the front (where square and cross give 120 and circle grabs instead),
  and how often: every [tandem run](#tandem-input) staged the rear hold with a puppet. A rumble with the Warriors' own
  brains, logging the grabber's state `0x20` / `0x10`, would tell.
- **A Warrior's own brain**{#q-warrior-brain}: whether a Warrior's own type-3 brain (the Warriors' think, `0x003052f0`)
  starts a tandem: in `moves_tandem_ai` Ash with that brain stood idle, so the run gave him type-2 handlers ([An AI
  tandem at runtime](#tandem-ai)). To find out, rerun the scenario without the brain writes and give Ash an attack order
  on PoizoCiv once he is in the rear hold, through the call hook on `GameState_IssueWarriorCommandFor` (`0x0041dc80`,
  behind the script binding `IssueWarriorCommand`), logging `set-command` for Ash's command `0xd`.
- **The FPU's rounding**{#q-fpu-rounding}: the PS2 FPU's rounding toward zero is assumed, not read from the FPU's
  control, for the late ties ([Reading the tables](#reading)). The clip ends agree with it on three more ties (36, 440,
  441 at 0.75, [When a clip ends](formats/animation.md#clip-end)); a run of a rate-0.9 clip with a tie (an event at
  frame 5 or 14) would check that rate.
- **Clips no path plays**{#q-unplayed-clips}: 490 `KNIFE_ATTACK_FROM_RUN`, 18 `ATTACK_SSX3_HOLD` and the `_02` snaps: no
  code path plays them here (20 is the chain's random pick after `SS2`, `0x00280708`).

## How this was checked

The clip and Anim Range List numbers were read from the disc's `warr_re_cv` (Rembrandt) and generic character data.
The runtime claims were measured in PCSX2 2.9.94 on copies of the street save state (slot 6), driven over PINE with
the scripted pad, the right-stick and puppet patches, and the `strike-contact` and `strike-shape` hooks ([Driving
PCSX2](../guides/research-workflow.md#driving-pcsx2)); the scenarios are `repo:research/traces/scenarios/moves_*.toml`.

**The runs in [Runtime results](#runtime)** started from copies of slot 6 (the street, the player Rembrandt facing
179.029°, the camera at 145.398°), with the puppet civilian `PoizoCiv` (600 health) placed by writing its transform each
update until the move, and the player at 900 health and 400 power. The stick was pushed through the scripted pad at
full deflection where the move needs more than 0.95 (snaps, strafes) and was otherwise centred.

**Shape windows and contacts** from runs recorded while `pcsx2 record` labelled every `strike-shape` and
`strike-contact` call one update early ([Recording a trace](../guides/research-workflow.md#hooks)) are given re-labelled
one update later, so that each contact falls in the update whose sample first shows the damage: the runs of [Runtime
results](#runtime).

None of these runs has a run record; the runtime claims are of the old form, which the page keeps as a legacy page
([Evidence levels and run records](../guides/research-evidence.md#run-records)).
