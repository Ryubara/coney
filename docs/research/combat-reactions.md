---
kind: topic
system: combat
owns:
  functions: [
    "0x00158728", "0x0022f100", "0x0022f658", "0x0022f8d8", "0x00230328", "0x00264bd8", "0x00265f70", "0x002674c0",
    "0x0026a6d0", "0x0026b0a0",
  ]
---
# Combat: damage, reactions and defeat

Verified against: `SLUS_212.15` (NTSC-U); runtime claims name PCSX2 2.9.94 and their method in How this was checked.

## Summary

This page covers what a hit does to whoever takes it: the damage and the health it takes, the reaction it plays (a
flinch, a stun, a knockdown), a hit on a human busy with something else and the moves a hit cannot stop, the climb
back up and the counter from the ground, being knocked out, the mission failing when the crew can no longer help, and
the blood a hit draws. A player sees a punch land, a man stagger or fall and get up, a man left on the ground, and the
"mission failed" screen. The AI's fight goals ([AI](ai.md#fight-reactions)), the grab and the mount
([Combat: grabs](combat-grabs.md)) and the HUD's health meter read the states set here. A bat's hit, which deals the
bat's damage rather than the clip's, is on [Combat: throws, weapons in hand](combat-throws.md#bat-knockdown). The
commands, the state bits and the tables every combat topic reads are on [Combat](combat/index.md#data).

## Map

No source file names this code: it lies in `Human/`, in the human's set-up and contacts and in the player's combat
code after `cns/cnsplayertag.cpp` ([Source map](source-map.md#position), inferred), and the names are ours. Every
reaction function is in the [code index](combat/index.md#code-reactions), and every combat function, with its role and
level, on [the combat function list](combat/functions.md); these are the entry points, each read in Ghidra at its
address.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0021b290` | `Strike_Contact` | a strike's damage, from the damage table, a weapon and upgrades | confirmed (code) |
| `0x00264bd8` | `Human_AddPendingDamage` | keeps the update's largest hit and its weight | confirmed (code) |
| `0x00265f70` | `Human_ApplyPendingDamage` | applies the hit each update and picks the reaction by state ([Damage](#damage)) | confirmed (code) |
| `0x0026b0a0` / `0x0026a6d0` | `Hit_PlayReaction` / `Human_PlayReaction` | a normal hit's reaction, by hit code; plays it | confirmed (code) |
| `0x002688d0` / `0x00268ea8` / `0x00268c50` | `HitReact_WhileGrabbed` / `HitReact_WhileGrabbing` / `HitReact_InMiniGame` | a hit on a busy human ([Attacking a busy target](#busy-target)) | confirmed (code) |
| `0x0022f658` / `0x0022f100` | `Human_Stun` / `Human_KnockDown` | the stun and the knockdown and their timers ([Reactions](#reactions)) | confirmed (code) |
| `0x0022f8d8` | `Human_EndStun` | the stun's end: 357, then idle ([Reactions](#reactions)) | confirmed (code) |
| `0x00256a60` / `0x0025e2d8` | `Human_UpdateDown` / `Human_PlayRise` | the get-up time and the rise 199 | confirmed (code) |
| `0x00267e48` | `Human_PlayLethalReaction` | the death reaction of a dying human, held back by a paired clip ([Moves a hit cannot stop](#protected-moves)) | confirmed (code) |
| `0x002674c0` / `0x00230328` / `0x00230598` | `Human_OnHealthOut` / `Human_KnockOut` / `Human_WakeUp` | health out, knocked out, woken ([Defeat](#defeat)) | confirmed (code) |
| `0x004197a8` / `0x00158728` | `GameState_CheckGameOver` / `Gm_Level_Update` | the mission failing and the hand-off to its screen ([The mission failing](#mission-failing)) | confirmed (code) |

## Data

The reactions use the record and state fields that [Combat](combat/index.md#data) owns ([State
flags](combat/index.md#state-flags), [Other record fields](combat/index.md#record-fields)), the reaction table at `0x00510798`
(read by `HitReact_WhileGrabbed`, confirmed (code)), the damage table ([Hit codes](combat/index.md#hit-codes)) and the
[constants](combat/index.md#constants). The fields this page reads most are below, each read in the function named.

| Field | Meaning | Evidence |
| --- | --- | --- |
| record `+0x118` | the pending damage, the update's largest (`Human_AddPendingDamage`) | confirmed (code) |
| record `+0x11a` | the pending hit's weight, read only by the paired reactions ([Scripted knockdowns](#scripted-knockdowns)) | confirmed (code) |
| record `+0x144` / `+0x146` | health and its maximum (`Human_ApplyPendingDamage`) | confirmed (code) |
| record `+0x100` / `+0x104` | the times the stun and the knockdown end (`Human_Stun`, `Human_KnockDown`, `Human_UpdateDown`) | confirmed (code) |
| record `+0xf0` | the knocked-out human's time (`Human_OnHealthOut`, `Human_KnockOut`) | confirmed (code) |
| state `0x100000`, `0x80000`, `0x40000`, `0x10000` | stunned, knocked down, knocked out, wounded | confirmed (code) |
| state `0x80000000`, `0x100000000` | dying, dead | confirmed (code) |
| `0x00510790` / `0x00510794` | 500 and 14,000 ms, the knocked-out times (s32) | confirmed (code) |

## Behaviour

The sections follow a hit from the contact to its reaction, then a human down, out and the mission's end, and last the
blood a hit draws.

### Damage, health and reactions {#damage}

Read in Ghidra: confirmed (code) where no other level is given; each runtime block says confirmed (runtime).

`Strike_Contact` (`0x0021b290`) takes the [damage table](combat/index.md#damage-table)'s value, adds a held weapon's bonus,
and for a player applies upgrade percentages (categories `0x13` / `0x19` and `0x1e` / `0x21` / `0x1d` / `0x20`, inferred
to be the upgrade levels). An attacker with flag `0x20` deals the victim's whole health. `0x0021d680` →
`Human_AddPendingDamage` (`0x00264bd8`) stores it at record `+0x118` if larger than what is pending, with its weight at
`+0x11a` ([Scripted knockdowns](#scripted-knockdowns)); the damage is **doubled** when the attacker has human flag
`0x4000` and **quartered** for class `0x80` (`bopp_bo`, the Boppers' boss) against brain type 3, after the doubling,
with an arithmetic shift right by 2. Confirmed (code): every damage path reaches it
(`Strike_Contact`, `Human_DealDamage` from the grab strikes, mounts and throws), so a grab strike is quartered too;
the grabbed side's strike-backs are only a player's (`Player_UpdateGrabbed`, brain type 0), so they are never
quartered. Health (record `+0x144`) fell by exactly the table's value on every hit tested
(confirmed (runtime)). Grab moves (`0x00262ac8`) apply their damage on the move's first update.

**Applying it** (`Human_ApplyPendingDamage`, `0x00265f70`, each update), confirmed (code), in order:

1. A held **block** takes the hit first ([Block](combat/index.md#block)).
2. A victim with flag `0x20000000000` (demi-god,
   [`HuSetDemiGodMode`](../references/bindings/character.md#husetdemigodmode)) cannot drop below a fraction of its
   maximum in one hit: health is set to that fraction, flag `0x10` is set, and the rest is lost. The fraction is one
   global for every human (`0x0051024c`, the last `HuSetDemiGodMode` call's; the scripts pass 0.25, the executable's
   own value is 0.3). Confirmed (code).
3. A cuffed victim (state `0x20000`, `Human_IsCuffed`, `0x00223b70`) takes triple damage; allies hurt each other only
   when both are players. Confirmed (code).
4. Health drops, unless the victim has human flag `0x10` (set by step 2 and by `HuMakeBeatUp`), is knocked out,
   dying or dead: then the pending damage is cleared and the health stays (a hit by an object of type `0x22` or
   `0x1d` still lands). A hit that would take the last of the health while the victim plays the flash clip 665 is
   cleared instead, so the flash prevents death (a smaller hit still lands). Any human in a pair or a block (state `0x18003ff0`,
   `Human_IsInPairOrBlock`, `0x00227fd8`) hit by someone other than his partner (`+0xc4`) spends 0.6 of his power
   (the constant at `0x00510274`). Confirmed (code).
5. **Hit armour**: no reaction plays (the damage still lands) while the victim is a player in an attack's wind-up or
   chain window (`+0x08` & 3), or a class 13 human with `+0x08` & `0x13`, unless the attacker is a player, class 13,
   raging, has flag `0x200000` or `0x4000`, or plays 617-620. A player with human flag `0x80000` holding `0x10` takes
   no reaction from anyone.
6. Otherwise the reaction by state, each a function of its own, among them: in a jump, falling, on fire, cuffing or
   being cuffed, in a mini-game, mugging or mugged, grabbed (`HitReact_WhileGrabbed`, `0x002688d0`), tackled,
   grabbing (`HitReact_WhileGrabbing`, `0x00268ea8`), tackling, mounting, mounted, throwing (state `0x1000`) or
   thrown (`0x2000`) (no reaction, the pending damage cleared), in a special, knocked out (`HitReact_WhileOut`),
   wounded (`Hit_PlayBegReaction`) and hit by an object (`HitReact_FromObject`); a normal hit `Hit_PlayReaction`
   (`0x0026b0a0`, [Hit codes](combat/index.md#hit-codes)). Confirmed (code); the list is not every branch.

### Attacking a busy target {#busy-target}

Read in Ghidra: confirmed (code) where no other level is given.

What happens when an attacker goes for a human who is busy with something else: held by a third human, stealing a
car stereo, or spraying a tag. Each check reads the target's state word
(record `+0x00`, [State flags](combat/index.md#state-flags)) or held flags (record `+0x08`) against a mask, and the
three show up there as grabbed `0x10` (front) / `0x20` (rear), a mini-game `0x4000000` (the stereo is mini-game mode 3,
[Crimes](crimes.md#mini-game-record)) and tagging `0x2000000`. `Tag_StartSpray` (`0x0022e610`) and
`StereoTheft_Start` (`0x0022dd98`) set only those state bits and no held flag; what their clips hold was not
checked. All confirmed (code) at the cited addresses.

**The checks on an AI's way in**, in the order a fight meets them ([AI](ai.md#check-attack)):

1. **EngageEnemy's run-in** (`EngageEnemyGoal_Process`, `0x002afa48`, [AI](ai.md#engage-enemy); confirmed (code)):
   within 0.75 × far (3.75 m) of a target whose
   state has any of the busy mask `0x7bf9e9f7ff0` (`HumanRecord_AreActionsBlocked`, `0x00228228`), the runner stops
   and turns to him instead of charging. All three activities are in the mask. The fight goal then attacks from
   there.
2. **`Brain_CheckAttack`** (`0x002906b8`, confirmed (code)) uses the same busy mask only to *relax* two of its waits:
   - result 4, hold back from an armed man, applies only when neither T nor T's target is busy;
   - result 8, let a heavy-armed mate go first, counts only a mate who is not busy.
   The busy target itself is not refused. Result 3 (`Brain_IsAttackableBy`, brain `+0x11f`) does not read the state:
   only `GoalGrabTarget` clears the flag while it holds its victim, so a human held by a scripted stalker is off
   limits, but a human held in an ordinary fight grab is not.
3. **The pick** (`Human_CanUseAttackKind`, `0x002240e8`) and **the start** (`Human_CanStartAttack`, `0x00224778`; the
   kind table at [An AI's attacks](combat-targets.md#ai-attacks)). The start first refuses a target holding any of the
   held flags `0xc08200` (unless T has state `0x2000` and A `0x1000`). Confirmed (code). By kind:

| Kinds | Grabbed by another (`0x10` / `0x20`) | Stereo (`0x4000000`) | Tagging (`0x2000000`) |
| --- | --- | --- | --- |
| 0-9, 11 (strikes, combos) | yes (target mask `0xe3000`) | yes | yes |
| 10 (snap), 15, 16-18 (specials; mask `0x40100f0800`) | yes | yes | yes |
| 19, 20 (charge, dive: T not down) | yes | yes | yes |
| 21 (tackle: T free of `0x7bf9e9f7ff0`) | no | no | no |
| 22 (grab: T free of `0x7bfdc8f7fd0`) | front no; rear only from in front of him (the start test) | no | **yes** |
| 12-14 (grounded) | unchanged: T must be down (or high) | the same | the same |

The pick also weighs a rear-grabbed target held by a gang mate +200 toward the grab ([AI](ai.md#pick-attack)).

**A player's moves.** Square and cross strike whatever their shapes meet. The player's grab and tackle search uses
`Human_CanBeGrabbed` (`0x002256a0`, confirmed (code)). It refuses the state bits `0x7bfdc8f7ff0` (grabbed, mini-game,
but not tagging), the held flags `0x8fc57a60`, a human knocked out, wounded, dying or dead (`Human_IsDownOrDead`,
`0x00227e60`, state `0x180050000`), in a scene, more than
0.25 m above or below, or with human flag `0x40`. A knocked-down human is tested without the knock-down bit
(`0x7bfdc877ff0`), so he may be grabbed (the grounded mount), unless he holds `0x48000` with his clip half played. So a
tagger can be grabbed or tackled; a stereo thief or a held human cannot.

**The strike itself** never asks about these states. `Human_TestStrikes` skips only a target in state `0x100000000`
(`Human_MayStrikeBody`, `0x00227180`, confirmed (code)), and `Strike_Contact` deals the damage. What the hit then does is
`Human_ApplyPendingDamage`'s (`0x00265f70`, [Damage](#damage)). The health always drops first. Then, unless hit
armour, the held flags `0x1c16a40` or human flag `0x800` cancel the reaction:

- **Tagging: the tag ends.** `Tag_End` (`0x0022e848`) runs for a tagger (or a human whose `+0x36c` still holds the
  tag's particle system), scoring an unfinished tag ([Crimes](crimes.md#tagging)). The ordinary reaction by hit code
  follows. Confirmed (code) in `Human_ApplyPendingDamage`.
- **The stereo: the theft fails.** A human in a mini-game goes to `HitReact_InMiniGame` (`0x00268c50`, confirmed
  (code)):
  `MiniGame_Abort` (`0x0022d628`) → `StereoTheft_End(h, 0)` (`0x0022e020`) with no reward. It clears the mode, the
  meter, the stage and angle, and the context record's occupant (so the stereo can be tried again), closes the HUD and
  restores the camera. Then, whatever the hit's strength, the thief plays **331** `ARREST_RELEASE_HIT_REACT` and his
  stance loop, holding `0x2000`, with the push sphere off. Only lock picking (mode 2) gets the ordinary reaction
  instead.
- **Held by another: the hold breaks** (`HitReact_WhileGrabbed`, `0x002688d0`, for an attacker other than the
  grabber; the grabber's own hits are the grab's; confirmed (code)). The pending damage is cleared after the health
  drop.
    - Rear-held (`0x20`) and not holding `0x2000`: the grab ends and the victim plays 133, the grabber 107
      `GRAB_REAR_BREAK_REACT`.
    - Front-held (`0x10`), not holding `0x2000`, without human flag `0x200`: the grab ends and the victim plays 137
      `GRAB_FRONT_HIT_VICTIM_KNOCKDOWN`, the grabber 138.
    - Otherwise the pair stays and plays a paired hit reaction (front 133, rear 123; 135 and 125 when record `+0x11a`
      is 2 or more), except that a heavy or crushing hit (hit-code strength ≥ 2) without held `0x2000` ends the grab
      and stuns the victim first.
    - The grabber hit by a third human goes to `HitReact_WhileGrabbing` (`0x00268ea8`), which ends or keeps the grab
      by the same tests from his side (a front grab ends with 144 on him and 145 on the victim). The 0.6 of his power
      he loses is spent before it, in `Human_ApplyPendingDamage` ([Damage](#damage), step 4). Confirmed (code).

### Moves a hit cannot stop: the dive, the charge and the paired moves {#protected-moves}

Read in Ghidra: confirmed (code) where no other level is given.

No "in a sync move" flag exists. What protects a human in a dive, a charge, a power move, a finisher or a tandem is
the record `+0x08` **held flags** those moves' clip tasks hold ([Held flags](tasks.md#held-flags)), read in three
places.

**Who holds what** (each bit is held by the clip task for the clip's whole length, then cleared with it):

| Move | Holder | Held flags | Where |
| --- | --- | --- | --- |
| the dive (L2 + square at a run, clip 1) | the diver | `0x400000`, push sphere off | `Player_StartDive` (`0x00264878`) |
| the charge (L2 + cross, clip 0) | the charger | `0x1000000` (confirmed (runtime), [the fence run](combat/index.md#moving-strikes)) | `Player_StartCharge` (`0x00264a80`) |
| a paired move: the grab's power strike 57 / 63 (front: `Attack_StartPaired`; rear: `Grab_StartPairedMove` from the spin 80), its extensions 59 / 65 and the mount's 225 / 227 / 231 (`Player_UpdatePowerMove`, `Player_UpdateMounting`), the armed grab and mount moves (484-496), the paired specials; the throws (`Player_Throw`) were not checked | the victim | `0x400000` (the argument each of those calls passes, `Player_UpdateGrabbing` `0x0027f3b0`), plus `0x2000` on a knockdown follow-up | `Attack_StartPaired` (`0x00262ac8`), `Grab_StartPairedMove` (`0x00263380`) |
| the same | the attacker | `0x7` (an attack's phases) or the caller's flag | the same |
| a tandem ([Combat moves](combat-moves.md#tandem)) | attacker and grabber / victim | `0x800000` / `0x400000` (and `0x80000` when the end has no knockdown) | `Tandem_Start` (`0x0026f860`) |

**1. No AI starts an attack on him.** `Human_CanStartAttack` (`0x00224778`, [An AI's
attacks](combat-targets.md#ai-attacks), test 1) refuses every kind against a target holding any of **`0xc08200`**
(`0x800000`, `0x400000`, `0x8000`, `0x200`), except for the target's own partner (target state `0x2000` and attacker
state `0x1000`). The fight goal then picks again and waits ([AI](ai.md#fight-order)), and the attack action's start asks
the same test, so a queued attack does not start either. So a diver, a power move's or finisher's victim and the three
humans of a tandem are left alone; a charger (`0x1000000` is not in the mask) is not.

**2. No grab or tackle by a player.** `Human_CanBeGrabbed` (`0x002256a0`) refuses held `0x8fc57a60`, which has
`0x1000000`, `0x800000` and `0x400000`.

**3. A hit lands but cannot make him react.** `Strike_Contact` never asks; the strike test skips only the same body
group, so the two humans of a pair (one group, `Grab_BreakOnHit`) do not strike each other with stray shapes while a
third human's shapes do. In `Human_ApplyPendingDamage` (`0x00265f70`) the health drops as usual, then a victim holding
any of **`0x1c16a40`** (`0x1000000`, `0x800000`, `0x400000`, `0x10000`, `0x4000`, `0x2000`, `0x800`, `0x200`, `0x40`)
has his pending damage cleared with **no reaction**: the dive, the charge, the paired clip and the tandem play on.

**4. A lethal hit waits.** A dying human (state `0x80000000`) is given his death by `Human_PlayLethalReaction`
(`0x00267e48`), which `Human_ApplyPendingDamage` calls at its top (`0x00266488`) and `Humans_Update` once an update
(`0x0024966c`), each only while he holds none of **`0xc12200`** (`0x800000`, `0x400000`, `0x10000`, `0x2000`,
`0x200`), is not falling (`0x1c00000000`) and is not in movement style `0x12`. While he holds any of them, the
ordinary path runs: the health drops and no reaction (lethal or other) plays. `Human_PlayLethalReaction` sets
`0x80000000` itself after it has cleared the pending damage and the player's command, released what he carries and
ended a throw's aim; it then returns at once when the flags are held, and otherwise ends what he was doing (mugging, cuffing,
a tackle hold, a mini-game) and plays the gang's death reaction (`Human_PlayGangDeathReaction`), unless he already
plays 349 (the knocked-out idle), whose death reaction it skips; a cuffed human with
human flag `0x4` plays 322 and the loop 323 `ANIM_ARRESTED_DEAD` instead. A human in state `0x800000` takes
nothing: `Human_ApplyPendingDamage` clears his pending damage and returns. Which code first sets `0x80000000` on the
lethal hit was not traced ([Open questions](#open-questions)).

**When the order matters** (`Humans_Update` step 9, [Tasks](tasks.md#humans-update); `Human_UpdateActions`,
`0x00254e78`): each human applies its pending damage **before** its dispatcher runs, and a reaction returns before
the dispatcher. The strike tests run earlier in the step (after each move). So:

- A hit that reaches a player on the update he presses L2 + square wins: the reaction plays, the dispatcher does not
  run, the one-sample command is lost, and there is no dive.
- From the dive's first update (`Player_StartDive` sets `0x400000` on the update of the press) no hit can stop it:
  AIs do not start attacks on him, a swing already under way still takes health but plays no reaction, and the dive
  runs its 2 s with its strike shapes on from k2 to k24 ([the table](combat/index.md#moving-strikes)).
- When the diver's shapes and the target's swing meet on the same update, both get pending damage; the diver plays
  on, the target reacts to the dive's crushing hit code `0x3a` ([Hit codes](combat/index.md#hit-codes)) before its own dispatcher
  could start anything new.

#### A lethal hit during a paired move, at runtime {#lethal-runtime}

Measured in PCSX2 2.9.94: confirmed (runtime) where no other level is given; none of the runs has a run record
([How this was checked](#how-this-was-checked)).

**The victim finishes his paired clip and only then goes down** (confirmed (runtime), PCSX2 2.9.94, scenario
`moves_finisher_lethal`: a copy of slot 6, PoizoCiv a puppet at 60 health 1.0 m ahead, Ash, the player's
ally, a puppet placed 0.9 m behind him; the player's front grab (circle at 10) and power strike (cross held from 42,
square at 44), Ash given `X1` (`0x12`, `0x10`, 0) at 47-49; stick centred; hooks `paired-start`, `anim-dispatch`,
`strike-contact`):

| Update | Player | PoizoCiv |
| --- | --- | --- |
| 45 | `Attack_StartPaired(…, 57, 1, 0x400000)`: 57, state `0x1005` | 58, state `0x2001`, held `0x400000` |
| 46 | | 57 damage: 60 → 3 |
| 56 | | Ash's `X1` (`Strike_Contact`, Ash → PoizoCiv): 23 damage, 3 → **−20**; state `0x80002001` (`0x80000000` set); **no reaction**, 58 plays on |
| 89 | 57 ends: 358, state `0x5` | still 58, state `0x80000001` (the pair's `0x2000` cleared) |
| 117 | | 58 ends (72 updates); **286** (the low reaction for a hit from behind) with held `0x2000`, health set to **1** |
| 149 | | **349**, state **`0x40000`**: knocked out, not dead ([Defeat](#defeat)) |

So the lethal hit took the health below 0 at once but changed nothing the player sees until the victim's own paired
clip (58, which outlasts the attacker's 57 by 28 updates) ended; then he fell (286, 32 updates) and lay knocked out.
The record's state `0x80000000` is the mark the hit leaves while the reaction waits (the dying state of
[the test above](#protected-moves)). A civilian with the flags of this one is knocked out, with
health 1 (inferred: `HitReact_WhileOut`, `0x002686e8`, which sets health 1), rather than killed.

#### A charge or a dive into a standing human, at runtime {#charge-dive-runtime}

Measured in PCSX2 2.9.94: confirmed (runtime) where no other level is given; none of the runs has a run record
([How this was checked](#how-this-was-checked)).

**A sprint charge or dive into a standing human, at runtime** (confirmed (runtime), PCSX2 2.9.94, scenarios
[`charge_human`](repo:research/traces/scenarios/charge_human.toml) and
[`dive_human`](repo:research/traces/scenarios/dive_human.toml): a copy of slot 6, PoizoCiv kept 4 m in front of the
player and facing him until update 30; L2 held and the stick at 98 % straight ahead from update 5, cross or square
tapped at 30; hooks `strike-contact` and `anim-dispatch`):

| | The charge (cross) | The dive (square) |
| --- | --- | --- |
| The move | 0 from update 31 to 57: **27 updates, its whole clip**; held `0x1000000` throughout | 1 from 31 to 91: **61 updates, its whole clip**; held `0x400000` throughout |
| The attacker's speed | 7.45 m/s, 4.4-6.3 m/s from update 36, then 0 from update 42, 1.65 m from PoizoCiv, to the clip's end | 7.9-11.7 m/s, then 0 from update 40, 1.65 m from him |
| The attacker's strike contacts (`0x0033f210`, from `Human_TestStrikes`) | updates 41-47 | updates 34-51 |
| The attacker's health | 314 throughout | 314 throughout |
| What follows | 388 at update 58 | 389 at update 92 |
| PoizoCiv | 600 → 405 and 294 at update 61, then 198 | the same, at the same update |

Neither attacker played a reaction or lost health: the move ran its whole clip. The run did not
test a flung victim striking the attacker, and **PoizoCiv's 195 damage and 294 were not the charge's or the dive's**:
moving him every update kept him airborne (clip 428, `ANIM_MOVEMENT_FALL`), and update 61 is when he landed (429)
and took a fall's damage and knockdown (confirmed (runtime), PCSX2 2.9.94, the same slot: placed only at
updates 0-2, he fell until update 34 and took 600 → 405 and 294 at 35 with no strike on him, the player still
standing; the strike-shape hook (`0x003428d0`) showed only the attacker's body switching shapes on). Why he fell:
the street in front of slot 6's spot ends about 7.5 m ahead in a drop of 8.7 m, and both placements put him past it
(kept 4 m ahead of a player who ran 4 m, or 9.3 m away at once). Walked out 0.15 m an update he stays on the ground
to 7.5 m and steps off there (confirmed (runtime), scratch probes `walkout` and `charge1`).

**A flung victim's strike costs the charger 20 health and no reaction; the diver was not struck** (confirmed
(runtime), PCSX2 2.9.94, a copy of slot 6; PoizoCiv's brain held (`+0x09` = 1), walked out 0.15 m an
update to 3.8 m (charge) or 2.7 m (dive) in front of the player, then kept there along the player's heading and facing
him until the press, so he stands upright and reacts at once; L2 held and the stick at 98 % straight ahead from update
20, cross or square tapped at 40; hooks `strike-contact` and `anim-dispatch`):

| Update | The charge (cross) | The dive (square) |
| --- | --- | --- |
| 41 | 0 starts, held `0x1000000` | 1 starts, held `0x400000` |
| hit | 50: the charger's body touches PoizoCiv (`Human_OnContact`, `0x00219d50`): 600 → 569, **296** (crushing mid, front), held `0x402000` | 46: the dive's strike shape (`Human_TestStrikes`, `0x0033f110`): 600 → 574, **300** (crushing high, front) |
| strike back | 51: PoizoCiv's flung body touches the charger (`Human_OnContact` again): **314 → 294**, the charger plays on in 0 (no reaction) | none: PoizoCiv's shapes test on from 57 and touch nothing; **314 throughout** |
| end | 0 runs its clip to 67, then 410 (the run) | 1 runs to 101, then 389 |
| PoizoCiv | 198 at 77, 196, 199 at 110 | 196 at 89, 199 at 106 |

So the code's reading holds at runtime: a strike on a charger takes the 20 of an extreme reaction (the crushing
reactions 296-303 switch every shape on) and the held `0x1000000` (`0x1c16a40` above) stops the reaction. The diver
was clear of 300's spine and head shapes in this run; a diver whose body is inside a flung victim's shapes would lose
the same 20 by the same code (inferred: the dive holds `0x400000`, in the same mask).

### Reactions: stun, knockdown and getting up {#reactions}

Read in Ghidra: confirmed (code) where no other level is given; each runtime block says confirmed (runtime).

`Human_PlayReaction` (`0x0026a6d0`) blends to the reaction in 1/30 s and rumbles the pad by strength (+1 raging).
Confirmed (code):

- **Stun** when the attack's flags have `0x400` or the victim is already stunned, unless the victim has flag `0x100`.
  `Human_Stun` (`0x0022f658`) sets state `0x100000`, ends a block, and sets `+0x100` = now + the power class's
  `+0x30` (× `+0x10` when hurt; × the attacker's class `+0x00` for some weapon hits of 20-49 damage; doubled by an
  attacker clip flag `0x200`). When the time passes, `0x0022f8d8` plays 357, then idle. Confirmed (runtime): the
  civilian's 750 ms after `SSS3` (279, 356, 357).
- **Knockdown** when the reaction clip has an animation event of type 7 (`0x00101a60`; the heavy and crushing sets).
  `Human_KnockDown` (`0x0022f100`) sets state `0x80000` (grounded), ends a block, and sets `+0x104` = now + the power
  class's `+0x34` (× `+0x14` when hurt; doubled by attacker clip flag `+0x45` bit 0; +2000 ms from a class 13
  attacker); a stunned victim also gets `+0x100` = `+0x104` + its stun time.
- Otherwise the record's `+0x08` gets `0x400` and `+0x18` = 11 (a standing reaction).

**Getting up** (`0x00256a60`): a grounded human rises with 199 `GROUNDED_RISE` once now passes `+0x104`
(`0x0025e2d8`, setting `+0x08` `0x8000`). Each command a pad-controlled human makes while down cuts the remaining time
by the ground time / (1 to 3 at random), so **mashing gets the player up sooner**. A standing reaction returns to
idle at its clip's end (`0x0025f770`, `0x0025fa48`; 355 when stunned). Confirmed (code); confirmed (runtime): the
civilian rose 2.0 s after the special's hit, the player (2750 ms) 2.78 s after an escape, with no input; the player's
mashing is measured in [Being hit](#being-hit-runtime).

The victim can act again when its reaction clip ends (standing), its stun ends, or its rise ends.

#### Scripted knockdowns, stuns and hits {#scripted-knockdowns}

Read in Ghidra: confirmed (code) where no other level is given; each runtime block says confirmed (runtime).

What the script bindings `HuMakeGrounded`, `HuMakeBeatUp`, `HuSetStunned` and `HuSetDamage` do to a human
([the bindings](../references/bindings/character.md#humakegrounded)). The runtime lines are from PCSX2 2.9.94, a copy of
slot 6, the natives' functions called on PoizoCiv (brain held, `+0x09` = 1) through the `call-brains` hook, with
`anim-dispatch`.

- **The script's knockdown lies down at once in 196.** `Human_MakeGrounded` (`0x00234e58`, with true) and
  `Human_MakeBeatUp` (`0x00234d70`) call `Human_KnockDown` (`0x0022f100`) with no attacker and no stun, which pushes
  the grounded movement style 15; `Human_ApplyMoveStyle` (`0x00253688`) gives style 15 the idle 196
  (`GROUNDED_IDLE`), and no reaction or fall clip is played. At runtime the human was in 196 on the next update,
  straight from its standing clip, record state `0x40080000` (confirmed (runtime), both natives). `HuMakeBeatUp` also
  empties the movement-style stack first and sets health to 1 and human flag `0x10` (runtime: health 600 → 1, flag
  word `0x1` → `0x11`).
- **The down latch is state `0x40000000`** (set by both natives through `Human_SetLying`, `0x00228388`; cleared by
  `Human_ClearDownLatch` `0x002283a8`, which `HuMakeGrounded(false)` calls). Its test `Human_HasDownLatch`
  (`0x002283c8`) has four readers:
    - `Human_UpdateDown` (`0x00256a60`, at `0x00256b04`) tests it first: a latched human skips the whole down and stun
      update, so it never rises (and a stun never ends) however long ago its ground time passed;
    - `Humans_Update` (`0x00249558`): a latched human with record state `0x200000000` is not taken away (the branch to
      `0x0021c7c8`);
    - `Humans_Update` (`0x002495d8`): a latched human is not moved from hurt (`0x10000`) to state `0x80000000` when its
      record `+0xf0` time passes (inferred from the branch; not run);
    - `Humans_CullCorpses` (`0x002322b4`) skips it, and `Bum_GiveItem` (`0x002ac488`, `0x002ac51c`) tests it.

  At runtime (confirmed (runtime)) a grounded human whose ground time (2000 ms) ended at update 66 lay in 196 until
  the latch was cleared at update 150, then rose at once: 199 on update 151 (28 updates), then the idle 388. The rise
  is the ordinary one ([Getting up](#reactions)): clearing the latch only lets `Human_UpdateDown` run again.
  `HuMakeGrounded(true)` also writes the state code (record `+0x14`) 3, but at runtime it read 0 from the next update.
- **`HuSetStunned`'s stun-immune bit is `0x100`**, the same bit as a hit's stun and `HuSetUnstunnable`: `Human_Stun`
  (`0x0022f658`) returns at once when the byte at human `+0xe1` has bit **0** (`lbu` at `0x0022f68c` / `0x0022f6c8`,
  then `andi 1`), which is flag word bit `0x100`. The endless-stun flag
  `0x8000000` (`HuSetManualStun`) skips the test, and only when there is no attacker, as for `HuSetStunned`.
  Confirmed (runtime): with `0x100` set, `HuSetStunned(true)` changed nothing; with it cleared, the human was stunned
  at once (355, 356, state `0x100000`, movement style 14) for 750 ms (the class stun time; 23 updates), then 357.
- **`HuSetDamage` with no attacker plays 275** on a standing human. `Human_AddPendingDamage` (`0x00264bd8`) stores no
  hit code; `Human_ApplyPendingDamage` (`0x00265f70`) makes the victim its own foe (`Human_SetFoe(h, h)`), so
  `Hit_PlayReaction` (`0x0026b0a0`) reads the default hit code 10 (`0x0a`: light, high, straight) and the side of the
  victim's own position: the zero offset normalises to zero on the VU, both dot products are 0, so the side is 3
  (`Hit_GetSideFromPoint`, `0x00267338`) and the direction (2 + 3) & 3 = 1, the left column: **275**
  (light, high, left in [the table](combat/index.md#hit-codes)). Confirmed (runtime): 50 damage, 600 → 550, 275 on the
  next update, then 389 and idle. The strength modifiers of [the reaction](combat/index.md#hit-codes) still apply (a
  victim with flag `0x200` drops to strength 0 anyway; one with `0x80` is capped at 1). With an attacker the attacker's
  own attack gives the code, as for a melee hit.
- **The `react` argument is not a may-react flag.** It is stored in record `+0x11a`, which
  `Human_ApplyPendingDamage` never reads; its only readers are `HitReact_WhileGrabbed` (`0x0026892c`, `0x00268948`,
  `0x002689b0`) and `HitReact_WhileGrabbing` (`0x00268fa0`, `0x00268fbc`, `0x00269024`): for a third party's hit on a
  pair holding `0x2000` (or a grabbed victim with flag `0x200`), a value of 2 or more plays the heavier paired
  reaction (`0x7d` / `0x87` instead of `0x7b` / `0x85`). Confirmed (code). So a scripted hit always reacts unless the
  usual held flags, flag `0x800` or a block stop it ([Moves a hit cannot stop](#protected-moves)). By these readers
  `+0x11a` is a hit weight for paired reactions; its writer `Strike_Contact` is on [Other record
  fields](combat/index.md#record-fields).

### Down and under attack: getting up and countering {#ground-escape}

Read in Ghidra: confirmed (code) where no other level is given; each runtime block says confirmed (runtime).

What a knocked-down human can do while someone attacks him on the ground. There is **no dedicated
"get-up attack" clip**: the escape is one of three routes, by what the attacker does. Runtime claims are confirmed
(runtime) with PCSX2 2.9.94, scenarios `ground_kicked`, `ground_kicked_mash`, `ground_mounted_circle`,
`ground_mounted_r1` and `ground_mounted_square` (a copy of slot 6; `PoizoCiv`, a puppet with 600 health and 400
power, knocks the player down with the special 653 at update 10, is put 0.8 m from him at 58, then kicks him with
command `0x37` (193) or mounts him with command `0xe`; stick centred throughout; hooks `anim-dispatch` and
`strike-contact`; the field set `down` reads record `+0x104`, `+0xbe` and `+0x00`).

**The states.** Down is record state `0x80000` (`Human_IsKnockedDown`, `0x0022f100` sets it); a crushing hit also
stuns (`0x100000`, seen `0x180001`). While down, `Human_UpdateActions` (`0x00254e78`) runs `Human_UpdateDown`
(`0x00256a60`), which returns 1 and so **skips the dispatcher**: no attack, block or grab starts from the ground.
Confirmed (code). A mounted human is state `0x800` instead, and the dispatcher sends him to `Player_UpdateTackled`
(`0x0027f1a0`).

**1. Kicked on the ground: mash, rise, and the rise's counter.**

- **Hits on the ground.** A hit on a human in state `0xe0000` that holds none of `0xc48000` goes to
  `Hit_PlayBegReaction` (`0x00269b80`, from `Human_ApplyPendingDamage` `0x00265f70`): the damage is taken, state bit
  `0x1` cleared, record `+0xbe` (the hits since the last rise) counted, and **195 `GROUNDED_STRIKE_REACT`** plays
  holding `0x400`, back to the grounded idle 196. A hit while the knockdown reaction still plays (held `0x400000`, in
  `0x1c16a40`) takes the health and plays nothing. `Human_EndDownState` (`0x0025e26c`, the end of 199) clears
  `+0xbe`. Confirmed (code); confirmed (runtime): the kick at 77 (still in 303) took 20 and played nothing; the kick at
  113 (in 196) took 20 and played 195 with `+0xbe` = 1.
- **The mash.** `Human_UpdateDown` cuts the remaining ground time by ground time / `Random_IntRange(1, 3)`
  (`0x005106c8`) on **every update with any command** (`PlayerCmd_IsAny`: any button press, tap or hold the
  matcher turns into a command), but only when no held flag is set and the state has none of `0x1c18003ff0`. So
  presses during the knockdown reaction (303) and during 195 count for nothing; presses in 196 do. When the time has
  passed, `Human_PlayRise` (`0x0025e2d8`) plays **199 `GROUNDED_RISE`** holding `0x8000`, then the fight-stance idle
  when in a stance (else the idle). Confirmed (code); confirmed (runtime): square every 4 updates from 40 did nothing
  until 196 began at 80; the press at 80 put him into 199 at 81 (1.28 s early). Without input he rose at 132 (2.75 s
  after the knockdown plus the wait for 195 to end).
- **The rise blocks input.** While 199 holds `0x8000` the dispatcher returns before the chain buffer (`0x5c7fee0`),
  so presses during the rise are not buffered (confirmed (runtime): circles during 199 in `ground_mounted_circle` did
  nothing).
- **The counter from the rise.** In `ground_kicked_mash` the square pressed late in 199 played **620
  `BLOCK_COUNTER_LEFT`** at update 97 against the puppet standing at the player's left, state `0x20000001`; it hit at
  103 for **50** and knocked the puppet down (288, then 196 and 199). Without the press (`ground_kicked`) the player
  rose into 356 / 357. Confirmed (runtime). The path is the duck's counter: `Human_UpdateActions` (`0x00254e78`), when
  record `+0x14` is `0xe` and the human is free to react, finds the foe within 1.25 × 617's far range (or the
  nearest one, `Player_FindCounterTarget`), ends the reaction clip and the stun, and calls `Block_DuckCounter`
  (`0x002617f8`), which plays 617-620 by the target's side ([Block](combat/index.md#block)). This test comes before
  `Human_UpdateDown` and the dispatcher, so being down (`0x80000`) does not stop it. Confirmed (code). The counter's
  hit is 617-620's own, 50 health points, their damage in the generic set's [Anim Range
  List](combat/index.md#damage-table) (Disc check: read from the disc; the 50 confirmed (runtime) above). With no
  target in range nothing plays, and the request lasts while the window is open (inferred from the duck counter's
  rule: each `0x25` of the window asks again).
- **What opens it: the rise's own `0x25` events.** 199's clip, `gen_face_up_ground_get_up` (21 frames; the generic
  set's, and no character maps 199 to another clip), carries event **`0x25` on frames 11-18**, the duck's event
  ([Block](combat/index.md#block)); its other events are `0x43` and 7 at frame 0, `0x44` at 6, `0xb` at 9 and 14, and **`0x48`
  at frame 14**. Inferred: the frames come from the clip's event list read from the disc (numbers only), a file check
  and not a level of its own; the window they predict is confirmed (runtime) in the table below, which fits them. Only
  three clips on the disc carry `0x25`: the two ducks of anim 616 (`gen_duck`, frames 6-13; `dest_bo_duck`, 1-13) and
  this one. So the rise counter is the duck counter's own path, not a code path of its own: each `0x25` sends message
  `0xa5` to the clip's human (`Anim_FireFrameEvents` `0x00101dd8`), and the handler (`Human_HandleMessage` `0x00245920`,
  case at `0x00246ae8`) sets record `+0x14` = `0xe` (`0x00246b64`) when `Human_IsFreeToReact` (`0x00225498`) allows it,
  the human is not both stunned (`0x100000`) and flagged `0x8000000` (human `+0xe0`), and a player's current command is
  square (`0xf`, `0x11`, `0x15`) or cross (`0x10`, `0x12`, `0x16`) (`0x0027b988`). Confirmed (code).
- **Why the window ends at frame 13.** Event `0x48` (frame 14) clears the clip's held flags and sets held **`0x40000`**
  (the recovery phase) on the human and the clip (`Anim_FireFrameEvents`, the `0x48` branch). `Human_IsFreeToReact`
  refuses every held flag but `0x40089000`, so it lets the rise's `0x8000` through and refuses `0x40000`: from frame
  14 a `0xa5` is refused, or a `+0x14` = `0xe` it set that update is cleared by `Human_UpdateActions` (its
  `Human_IsFreeToReact` test fails, `Human_SetAction(h, 0)`). The state flags are no bar: the down state `0x80000`
  (kept through 199 until its end callback `Human_EndDownState` `0x0025e26c`) and the stun `0x100000` are outside
  its state mask `0x7bf9e877ff0`. So **only frames 11-13 count**. Confirmed (code); the frame-14 `0x25` being dead is
  inferred from the event order (`0x25` before `0x48` in the clip's list) and fits the runtime below.
- **The window at runtime** (confirmed (runtime), PCSX2 2.9.94: `ground_rise_counter` and variants of it,
  a copy of slot 6 as above with the second kick removed; square tapped at update 80 starts 199 on update 81, then one
  square or cross tap; k counts updates from 199's first, k = 0 at 81). 199 ran 28 updates (81-108) with no input
  after it, so it plays at about 0.75 speed, as 616 does. A square tapped at update t reached the record (command
  `0xf`) on t + 1; the table gives that update.

  | Square's command reached the record on | k | Result |
  | --- | --- | --- |
  | updates 90, 91, 93, 94, 95 | 9-14 | nothing; he rises into 356 / 357 |
  | **96, 97, 98** | **15-17** | **620** on that same update, the puppet at his left |
  | 99-102, 105-107 | 18 and later | nothing |

  Cross, by the update it was tapped: tapped at 94 or 95, 620 on update 96 (k = 15); tapped at 96, 620 on 97
  (k = 16); tapped at 98 or 100, nothing. Which cross command (`0x12` press or `0x10` release) was the one taken was
  not recorded. The counter plays on the update the command arrives: the clip's events fire before
  `Human_UpdateActions` handles `+0x14` in the same update.

  **The runtime edges, k = 15-17, are the rule; the frame range is only the explanation.** Frames 11-13 at exactly
  0.75 speed would span k = 15-18, yet k = 18 does nothing, so 199's effective rate is a little above 0.75 (about
  0.78 from the edges; inferred). The rate was not read from the code or the clip, so a reimplementation that maps
  frames 11-13 through a 0.75 rate gets k = 18 wrong; it must reproduce k = 15-17.
- **The AI does not counter from the rise.** The `0xa5` handler asks an AI's brain (`Brain_WantsCounter`
  `0x0028c6a8`): while its reaction goal is Grounded (`0x17`) the answer is `GroundedGoal_WantsCounter` (`0x002b4aa0`),
  which is always no, and the Grounded goal lasts while the human is knocked down (`GroundedGoal_Process`
  `0x002b4aa8` ends it once `Human_IsKnockedDown` (`0x00223b98`: state `0x80000`, with a record) is false). State
  `0x80000` is cleared only by 199's end callback (`Human_EndDownState` `0x0025e26c` → `Human_GetUp`), so the goal
  lasts through the whole of 199. Confirmed (code);
  not seen at runtime.
- **The AI.** An AI's Grounded goal (`0x17`, `0x002b4aa8`) queues kind 42 on itself about 1.9 s after going down while
  it is attacked ([AI](ai.md#fight-reactions)). `AttackKind_ToCommand` (`0x00231090`, table `0x0055c170`) makes kind
  42 the command `0xf` (square), so it is the AI's mash: one command, which cuts its ground time like a press
  (inferred from the code; not seen at runtime): no attack clip starts from it.

**2. Mounted on the ground: struggle, break or reverse.** An attacker's circle at a knocked-down target that
`Human_CanBeGrabbed` accepts (not holding `0x8fc57a60`, so not while the knockdown reaction plays) mounts him:
`Player_GrabOrTackle` (`0x00284920`) → `Mount_StartOnGrounded` (`0x00271b30`): clip **237 + the side** (237 front,
238 right, 239 back, 240 left) on the attacker, steered on over 0.1 s, **241 `MOUNT_GROUNDED_REACT`** on the victim,
then the mount's idles 210 / 207 (`Pair_LinkMount`). The tap (`0xd`) and the hold (`0xe`) both reach it; the AI's
kind 14 is command `0xe`. A target in 199 (held `0x8000` alone) is still mounted while 199 is below 70 % played: the
rise is cut. Confirmed (code); confirmed (runtime): command `0xe` at 84 played 240 / 241 at 85 and 210 / 207 at 112
(27 updates); at 64, during 303, the same command fell through to the tackle (3, then the miss 2).

The mounted victim's inputs (`Player_UpdateTackled`, [Combat moves](combat-moves.md#mounted-victim)), confirmed
(code) and here from the grounded mount (runtime):

| Input | Effect | Runtime (from 150) |
| --- | --- | --- |
| square or cross | the struggle **250** / 251: the mounter loses 1 / the victim's class divisor of its power per press, and 250 hits it (40) | 250 at 151, the mounter 600 → 560 → 520; its power ran out and `Human_UpdateMeters` let the victim escape: **246** / 247 at 169 |
| circle | the break `Mount_GetOff` (`0x00271470`): **246 `MOUNT_BREAK`** / 247, both to the fight-stance idle, when `Grab_CanEscape(mounter)` allows (always at or below a quarter of its power, else 1 in floor(power / quarter) per press; never while it rages); the victim's flag `0x2000000000` forbids it | 246 / 247 at 158 (3rd press), 48 updates, **no damage** to the mounter |
| R1 (command 3) | the reversal `Mount_StartReversal` (`0x002723e8`): **242** / 243 (57 updates), then the roles swap: the victim mounts (210, the attacker 207) and has the mount's strikes; refused when the mounter has human flag `0x40` or the victim `0x100000000` | 242 at 166 (6th press), 210 / 207 at 223; then square played 221 / 222, 61 damage each |

**What the attacker gets.** From the break: nothing but 247 and the stance (no damage, no knockdown, no stun);
from the struggle: 40 a press that plays 250, and its power drained; from the reversal: it is mounted.
From the rise's counter: the duck counter's hit (50 here, a knockdown).

**The counter after a break.** 246 holds `0x2000`, which blocks the dispatcher; at its end the player stands in the
fight stance and acts on the next command: square at 219 after the break at 169 played `S1` (12) and hit for 17, then
`SS2` 36 and `SSS3` 53. A circle pressed as 246 ended started a grab instead (71, then the miss 69). Confirmed
(runtime).

**How it relates to the pair breaks.** `PairBreak_GetUp199` (`0x00269908`) and `PairBreak_Play244` are the clips a
mount plays when something **outside** the pair breaks it (a teleport, a hit on the mounter:
[Breaking a pair](combat-grabs.md#pair-break)): the victim plays 245 then 199. The escapes here are the victim's own and
play 246 (the break) or 242 (the reversal) instead; none of them plays 199.

### Knocked out, and the mission failing {#defeat}

Read in Ghidra: confirmed (code) where no other level is given; each runtime block says confirmed (runtime).

**When health runs out** (inferred from what it does), `Human_OnHealthOut` (`0x002674c0`; from `0x00267a00`,
`0x00267e48` and `Human_HandleMessage`) runs, unless state `0x1c00000000` is set, in this order:

1. It clears the state bits `0x208000800f` (stance, grabs, block), the action and the pending damage, and sets
   record `+0xf0` = now + 500 ms.
2. What happens next depends on human flag `0x4` (the player in the street had it,
   [Human flags](combat/index.md#human-flags)):
   - **Without `0x4`**, a human that is not cuffed, has flag `0x8` and is not wounded already (`Human_IsWounded`,
     `0x00227d98`, state `0x10000`) is **wounded** (`Human_StartWounded`, `0x0022fa78`, which sets `0x10000`;
     confirmed (code)). It drops what it carries, leaves the radar and loses its icon.
   - Any other human without `0x4` is **knocked out** (`Human_KnockOut`, `0x00230328`), drops what it carries
     ([Dropped when down](crimes.md#down-drop)) and loses the wounded bit `0x10000`.
     It is **dead** instead (state `0x100000000`, record `+0xf0` = 0) when the source of the last hit (record `+0xd0`)
     is an object of kind 29.
   - **With `0x4`**: knocked out, with record `+0xf0` 14 s later still. Confirmed (runtime), `level80`: Vermin's
     `+0xf0` became now + **28,000 ms** (`Human_KnockOut`'s 14 s plus these 14 s).
3. A cuffed human's idle becomes 323 `ANIM_ARRESTED_DEAD`.
4. For a player, the players of the same gang who are knocked out get the same `+0xf0`.

**`Human_KnockOut`** does nothing to a human already knocked out (state `0x40000`); otherwise it:

- sets record `+0xf0` = now + 14,000 ms (`0x00510794`, an s32 14000 in the executable; confirmed (code));
- gives the push weight 1e9;
- turns a player's slow motion off;
- unless the human is dead or wounded (`0x100050000`), sets state **`0x40000`** (knocked out;
  `Human_IsKnockedOut`, `0x00227dd8`; `Human_WakeUp` clears it) and tells the brain (`0x0028c3b0`); it then sets
  `0x40000` again at its end whatever the state (`0x00230554`), so a dead or wounded human gets the bit too, without
  the brain call ([Who sets and clears it](#knocked-out-bit));
- for a member of a player's gang outside Armies and Rumble levels, shows the **`dyn_cross`** icon over it
  (`Human_ShowOverheadIcon`, argument 3), the mark a partner revives with a flash
  ([Crimes](crimes.md#triangle); inferred). The brain call and the icon are skipped when its second argument is
  not 0;
- clears the block bit `0x8000`.

The flash clip 665 keeps a player alive ([Damage](#damage), step 4).

#### Who sets and clears the knocked-out bit {#knocked-out-bit}

Read in Ghidra: confirmed (code) where no other level is given; each runtime block says confirmed (runtime).

State `0x40000` is the record's state word (`+0x00`), which only `Human_SetStateFlag` (`0x002265d0`) and
`Human_ClearStateFlag` (`0x00226620`) change bit by bit. Their 135 and 161 call sites were read for the mask they
pass (confirmed (code); only the eight sites below clear `0x40000`); every 64-bit read-modify-write at offset 0
elsewhere in the executable is on another record (the game state), and a whole-word zeroing happens only when the
record is set up or released (`Human_ReleaseInstance`, `0x00217dd8`), so the lists below are complete (inferred:
the scan matched `ld`/`sd` pairs, not every store).

**Set** only by `Human_KnockOut` (`0x00230328`, calls at `0x0023048c` and `0x00230554`), whose callers are:

| Caller | When |
| --- | --- |
| `Human_OnHealthOut` (`0x002674c0`, confirmed (code)) | health out, a human without flag `0x8` or already wounded ([above](#defeat)) |
| `Human_SetConscious` (`0x00237778`, confirmed (code)) | `HuSetConscious(h, false)` |
| `Human_Destroy` (`0x0021c7c8`, confirmed (code)) | a human deleted while not dead: knocked out (his brain drops its goals and target), then made dead (`0x100000000`) and the bit cleared, in the same call |
| `Human_ReleaseInstance` (`0x00217dd8`, confirmed (code)) | from `Human_Destroy` only, with argument 1; the state word is zeroed a few instructions later |

**Cleared** by:

| Site | Function | When |
| --- | --- | --- |
| `0x0023067c`, `0x00230694` | `Human_WakeUp` (`0x00230598`; argument 1 clears `0x40000` only, 0 clears `0x1c8007` and resumes the brain) | from `Human_EndDownState` (`0x0025e250`, the end callback of the rise 199, [Grounded](#ground-escape)), `Human_Heal` (`0x0022eb40`, a revive or flash), `Human_SetConscious` (`HuSetConscious(h, true)`) and `Human_AttachInstance` (`0x00217a98`, argument 1, a model attached) |
| `0x00249818` | `Humans_Update` (`0x00249108`) | a knocked-out human **without** flag `0x4` (not revivable) whose `+0xf0` has passed (14 s after `Human_KnockOut`) and no held flag: when no camera sees him (`Cameras_CanSeePointExcept`), he is made **dead** (`0x100000000`), his body freed, `+0xf0` = now + 500 ms; when one does, `+0xf0` moves another 14 s on (`0x00510794`) |
| `0x00249ab4` | `Humans_Update` | a knocked-out **player** with flags `0x1` and `0x4` whose gang has no conscious player left: dead once `+0xf0` has passed |
| `0x002676c4` | `Human_OnHealthOut` | the last hit came from an object of kind 29: dead instead |
| `0x0022faa8` | `Human_StartWounded` | mask `0x14800f` |
| `0x00234df0` | `Human_MakeBeatUp` | mask `0x180040000` |
| `0x0021c9a4` | `Human_Destroy` | see above |

So a knocked-out human gets up only through `Human_WakeUp`: a heal or revive, the script, or a rise 199 that runs to
its end while he is still knocked out. `Human_UpdateDown` (`0x00256a60`) starts the rise for any knocked-**down**
human (`0x80000`) whose get-up time has come, and its state mask (`0x1c18003ff0`) does not hold `0x40000`, so it
never starts the rise for a knocked-out human. A tackled victim's end hook (`Tackle_OnVictimRiseEnd`, `0x00270028`)
starts it when he is not dead (`0x100000000`), not dying (`0x80000000`) and his pair link (`+0xc4`) is empty, with
no test of `0x40000`: it ends the tackle if one still holds him and plays the rise (`Human_PlayRise`, `0x0025e2d8`,
clip 199 with the end callback `Human_EndDownState`), whose end calls `Human_WakeUp(h, 0)` when `0x40000` is
set. So **this path wakes a knocked-out tackled victim**, which `Human_UpdateDown` cannot (confirmed (code)); whether a
tackled victim is ever knocked out when the hook fires was not checked. Through all of 199 the bit stays set: it
goes at the clip's end (inferred: no code path was found that knocks out a human who is down with `0x80000` and then
lets `Human_UpdateDown` reach him, so this may not happen in play).

**Not the same bit:** the attack's **recovery** phase `0x40000` lives in record `+0x08` (held flags), not in the
state word: event `0x48` sets it with `Human_SetHeldFlag` (`0x00226640`, the call at `0x001023a4`;
[Tasks](tasks.md#locomotion-gate)). A stun is `0x100000` and a knock-down `0x80000`; neither touches `0x40000`.

**In a Rumble brawl** (confirmed (runtime), PCSX2 2.9.94, the five recordings of [Rumble: gang battle
trace](rumble.md#gang-battle-results) that ran the hooks: offsets 100 and 500, and the three with [the sides
swapped](rumble.md#gang-battle-swap); 51 fighters knocked out): the fighters are not revivable, so none
gets up. Each goes through **dying** (state `0x80000000`, from the update of the lethal hit, health 1, the lethal
reaction such as 292, about 45-90 updates, brain still on), sometimes a **wounded** crawl (`0x10000`, clips 200 and
204, up to some 400 updates), then **knocked out** (`0x40000`, from the first update of 349) for exactly 420 or 421
updates, or a multiple of 420 when a camera saw him, then **dead** (`0x0` in the low half, `state_hi` `0x41` and then
`0x43`: `0x100000000`, `0x4000000000` and the fade `0x200000000`) for 40-41 updates until he is deleted. No fighter's
`0x80000000` run ended in anything but `0x10000` or `0x40000`. The 461 updates from 349 to deletion on
[Rumble](rumble.md#gang-battle-results) are these 420 + 41.

#### The mission failing {#mission-failing}

Read in Ghidra: confirmed (code) where no other level is given.

The engine decides when a mission fails; `global.lua` does not. `GameState_CheckGameOver` (`0x004197a8`)
runs every game-state update (`0x0041a370`), while three things hold:

- the check is on (`EnableGameOverCheck`, game state `+0x155`,
  [Bindings](../references/bindings/level.md#enablegameovercheck));
- game state `+0x158` is 0;
- no level end is pending (`+0x14c` = 0).

It works through these cases, confirmed (code):

1. **A dead player** (state `0x100000000`) fails at once.
2. Nothing more happens while any player is neither knocked out nor cuffed.
3. A cuffed player 1 who can free himself (upgrade (6, 15) and a key) is spared.
4. When no one else in player 1's gang is free to help (`Gang_NoneAbleToHelp`, `0x00169dd8`: not cuffed, knocked out
   or wounded), the mission fails at once. Confirmed (code).
5. When others are free, and player 1 is cuffed (not out) or holds a flash (item 1), and game state
   `+0x414 + player` is 0 (the crew was within 50 m at the last range check,
   [out-of-range flag](ai-gangs.md#warrior-range-flag)), it **waits**; with the byte set it fails at
   once. Every 37 updates it asks whether a free member has a route to him
   (`Gang_CanReachToHelp`, `0x00169ea0`; [who can help](#defeat-helpers)). A yes resets the count at `+0x56e6`; the
   4th no in a row fails the mission. Confirmed (code) at `0x004197a8`.
   A knocked-out player without a flash fails at once.

A failure sets `+0x14c` = 1 and the menu's title (`MissionFailed_SetReason`, `0x001d1fb8`): `GSTRING.HUD` 21 with game
state `+0x118` = 1 when player 1 is cuffed (busted), else 20 with `+0x118` = 0 ([Text
labels](../references/text-labels.md#text-gstring-hud)).

**The hand-off** (`Gm_Level_Update`, `0x00158728`), confirmed (code):

1. While `+0x14c` is 1 or 2, a countdown at level mode `+0x28` runs down by one per update. `Gm_Level_Enter` and
   `Gm_Level_Resume` set it to **180**; for anything but a story failure it is first capped at 90.
2. On a story failure (not an Armies level) the first update does the following, unless game state `+0x152` bit
   `0x2` is set:
   - makes the **failed camera** (`Cam_GetFailed(1)`) active for player 1;
   - blends the screen tint to `0xd0000014` over 6.5 s and starts a 1.5 s blur pulse;
   - hides the HUD and removes the players' overhead icons;
   - keeps the music volume, then fades it toward 0.7 while the failed camera runs.

   At 180 the system music stops.
3. A pad's newly pressed button bit `0x40` cuts the countdown to 10 and the tint to 1/3 s. Game state `+0x152`
   bit `0x2` cuts it to 0.
4. At 0, `MissionFailed_Toggle` pushes mode `0xc`, the [mission-failed screen](pause.md#the-mission-failed-screen)
   (`ANGameOver_Toggle` on an Armies level). A level end of 2 launches the mission-complete screen instead.

So the screen comes 180 updates (6 s at 30 updates a second, inferred) after the failure, with the reason set by the
engine. `HUDLaunchMissionFailed` is only the scripts' own route.

#### Who can help {#defeat-helpers}

Read in Ghidra: confirmed (code) where no other level is given.

Both tests walk the 16 member slots (gang `+0x48`) of the gang passed in (player 1's) and skip empty slots and the
downed player himself. Players count as members like anyone else. Confirmed (code):

- **`Gang_NoneAbleToHelp(gang, player)`** (`0x00169dd8`) returns 1 when no other member is free. A member is free when
  he is not cuffed (state `0x20000`), not knocked out (`Human_IsKnockedOut` `0x00227dd8`: state `0x40000`) and not
  wounded (`Human_IsWounded`, `0x00227d98`: state `0x10000`, which `Human_StartWounded` sets;
  [Combat](combat/index.md#state-flags) also names it on a tackled victim). Both state tests also count a human with no
  record (`+0xd4` = 0) as not free. The "wounded" of case 4 is that `0x10000` test. **An empty gang (no live handle)
  returns 0**, so it does not fail at once and goes on to the route test, which then says no.
- **`Gang_CanReachToHelp(gang, player)`** (`0x00169ea0`) returns 1 when some member has a route to him; 0 when the
  player is null or the gang has no live handle. The goal is the player's position (`+0x2b0`) put on the navigation
  mesh: `Nav_FindPolygonUnder` (`0x00252410`, height 0) drops it to marked ground within 10 m, else to the nearest
  polygon edge within 2 m (else it stays where it was), and `Nav_FindArea` (`0x00250708`) finds its area. **No area →
  0**, without trying any member. Then each member in slot order that is not cuffed (`Human_IsCuffed` ≠ 1) and not down
  or dead (`Human_IsDownOrDead` `0x00227e60` ≠ 1: dead, dying, knocked out or `0x10000`;
  [AI](ai-gangs.md#warrior-follow)) is tested with
  `Nav_CanReach(his position, goal, his area found from his position, the goal's area, 1, 0xffff)` (`0x0024e078`); the
  first yes returns 1. There is no distance limit.
- **`Nav_CanReach`**: no area at either end → no. Two different areas must both be on the route graph (area `s16 +0x02`
  ≠ 0), else no. A walkable straight line between the points (`Nav_IsWalkableLine`) → yes. Otherwise each end's nearest
  node it reaches in a straight line must exist, and `Route_Find` with mask `0xffff` must return a route; with the 5th
  argument 1, as here, a route with any blocked edge (`PathNode_IsEdgeBlocked`) counts as no. ([AI: path
  planning](ai-moving.md#path-planning) for the graph.)

So a stand-in that always finds a route never fails on the 4th try. The faithful check fails when the player stands off
the mesh (no area), when every free member is in an area off the graph, or when every route crosses a blocked edge (a
link with a negative cost, `NavNode_IsLinkBlocked` `0x002510a8`).

### Hit blood {#hit-blood}

Read in Ghidra: confirmed (code) where no other level is given; each runtime block says confirmed (runtime).

Blood comes from two places: every landed hit on a human asks for a **punch flash** whose blood count depends on
the weapon and on how hurt the victim is, and the **reaction and kill clips** carry effect events that spawn blood
(or sweat) at a bone on chosen frames. Both end in the same effect, `sub_blood_effect`, whose pieces (a mist, a puff,
sprays, drops and an unseen falling blob that leaves drops where it lands, with the `blood_02` sound) are on
[Script types: blood](script-types.md#blood). Nothing is spawned while the game state's blood switch `+0x454` (a
short) is set; nothing in the retail game sets it (inferred, [Graphics](graphics.md#human-draw)). The blood a human
wears on his skin as he loses health is a texture, not a particle: [Drawing a human](graphics.md#human-draw).

**A hit** (`Human_SpawnHitEffect(attacker, victim, …, blocking)`, `0x0021d928`), called by `Strike_Contact`
(`0x0021b290`) after the damage of every strike on a human, by `ThrownObject_HitHuman` (`0x00392b88`, the thrower as
attacker, not blocking) and by `Explosion_DamageHumansInRadius` (`0x00392638`, each human the blast reaches with a
clear ray, not blocking). `Strike_Contact` passes `Human_IsBlockingOrDucking(victim)` as `blocking`; the zone and
bone arguments it also passes are not read. Confirmed (code), in order:

1. **Culling.** Nothing when the victim's position is more than 2 m outside one of the six frustum planes of player
   1's view and, with two players, of player 2's view too; nothing when the [particle pool](script-types.md#effect-pattern)
   is short (`ParticlePool_HasRoom`, `0x003a5a50`).
2. **The count**, from the object in the attacker's hand (its model hash `+0xc4`):

    | Held object | Count |
    | --- | --- |
    | `dyn_bowie`, `dyn_hunter`, `dyn_tknife`, `dyn_swhbld`, `dyn_swhbld_super` (the knives), `dyn_cleaver`, `dyn_beerbottle_b` (the broken bottle) | 2 |
    | `dyn_machet`, `dyn_sledgehammer` | 3 |
    | anything else, or nothing (fists, bat, pipe …) | 0 |

    Then, when the victim **is hurt** (`Human_IsHurt`, `0x00222ff8`: health below the class's `+0x04` fraction of
    maximum, 0.3 by default, [Characters](characters.md)), a count of 0 becomes 1, and a hurt victim who is
    **blocking** gets 0 whatever the weapon. A victim who is not hurt keeps the weapon's count even when blocking.
    The names are the model hashes' CRC-32 matches (`0x2a263bb3` … `0x4b95d669`), corroborated against the
    reference lists.
3. **An object of kind 39** in the attacker's hand (`ObjectAttribs` `+0x86`, the one-sphere melee object of
   [Moving strikes](combat/index.md#moving-strikes)) makes no blood: when the victim's player record `+0x1b` is set it queues
   screen effect 4 (the blur pulse, [Screen effects](../references/screen-effects.md)) on the victim's view for as long
   as his anim `0x4d` plays, and the first time (game state bit `0x800000000`), outside the armies levels and with
   tutorial hint 26 unlocked, queues game hint 26.
4. **The flash**: a `sub_punch_flash` attached to the victim at attach point 9 (a head bone, in the head range 5-16
   of [Sound events](sound-events.md#strike-human)), offset 0, rotation word `(0, 1, 0, 1)`, with the count. Its
   init (`SubPunchFlash_Init`, `0x003b1460`), when a camera is within 25 m, the point is in a view (10 m margin) and
   blood is on: count 0 makes nothing; count 1 makes one `sub_blood_effect` whose spray direction follows the bone's
   own motion; a count *n* of 2 or more makes a `blood_mist` grown × 1.33 and *n* + 1 `sub_blood_effect`s, each with
   the flash's rotation turned by a random angle `a` in [0, 2π) through the quaternion `(0, 0, sin a, cos a)`, their
   sprays along the turned +y.

So bare fists draw blood only from a hurt victim (one burst a hit), and a knife, cleaver or broken bottle draws three
bursts and a machete or sledgehammer four on every hit, unless a hurt victim blocks.

**Clip effect events** (type `0x21`, [Animation format](formats/animation.md)): `Anim_FireFrameEvents`
(`0x00101dd8`) builds the name `sub_` + the event's three-letter code (`+0x14`) and calls `Human_SpawnAnimEffect`
(`0x0021de48`) with the human whose clip it is, the event's bone (`+0x06`) and its position and rotation decoded as
the type-8 event's. That function culls as above (2 m outside every player view) and spawns by code; the blood codes
(confirmed (code)):

- `bld`: a `sub_blood_effect` at the bone, unless blood is off;
- `pch`: a `sub_pch` (`SubPch_Init`, `0x003b17f8`), which within 25 m and in view makes a `sub_blood_effect` when the
  human is hurt (blood on) and a `sub_sweat_effect` when he is not;
- `blo`: a `sub_blo`: a `sub_blood_effect` at once and, on its first update, a blood decal on the surface 1 m along
  the event's +y ([Script types](script-types.md#blood)).

The other codes are not blood: `dus` dust, `smk` smoke, `puk` vomit, `spp` / `spr` / `spm` spray paint, `pla`, `gun`,
`glt`, `dce` ([Script types](script-types.md#by-address)). A survey of every clip on the disc (`coney-tools`
animation decoder; counts only) found 122 distinct `bld`, 62 `blo` and 33 `pch` events:

- `pch`, all on bone 6 (the head) at frame 0-1: the twelve high hit reactions `gen_hit_react_high_*`
  (front / back / left / right × `1m`, `down`, `ex`; anim slots 280-283, 292-295, 300-303), five deaths
  `gen_die_high_*` / `gen_die_mid_*` (slots 308-315), the grab strike reactions 1-3 (slots 52, 54, 56), some
  grab and mount power reactions, the ground hit reaction (slot 195) and `virgil_uppercut_react`. The mid and low
  hit reactions have none, so a body blow never bleeds through the clip;
- `bld` (bones 6, 5, 25, 19, 3, 33, 24, 0): the knife and bottle kills and grabs (`gen_grab_front_knife`,
  `gen_grab_back_knife`, `gen_knife_mounting_react`, `bottle_smash_victim`, `player_stealthkill_knife_react` with
  18), the bat tandem, mount and grab power moves and the mounted punches;
- `blo` (bone 6): mount and grab power moves, tandems, tackle counters and `gen_fall_front_land`.

**At runtime** (PCSX2 2.9.94, scenario `blood_knife_grab`: the knife grab kill of `moves_knife_grab` with call
hooks on the functions above, 400 updates): the kill fired five `bld` events (one on the victim's bone 3, four on the
killer's bone 25, his knife hand), each made one `sub_blood_effect` and, one update later, one `blood_splatter`;
every blob landed exactly once, 25 to 38 ticks after it was made, on the floor (contact normal z 1.0), and none
landed again in the rest of the run. No `Human_SpawnHitEffect` call: the paired kill strikes nothing. Confirmed
(runtime).

## In Coney

The damage and the reactions live in `repo:src/combat/reactions.h` (the hit code, the side, the reaction table, the
block reactions), `repo:src/combat/meters.h` (health and the pending damage), `repo:src/human/victim.h` (the stun,
the knockdown, the ground time, the rise and the mash) and `repo:src/human/fighter_victim.cpp` (the player hit, the
lethal hit held back, the rise's counter); the mission failing is `repo:src/warriors/game_over.h`. [Combat: Coney's
implementation](combat/index.md#coneys-implementation) says what each file does, and [Combat: where Coney
differs](combat-diffs.md) lists every difference in the fight. A bat's hit in Coney is on [Combat: throws, weapons in
hand](combat-throws.md#bat-knockdown-coney).

### Damage {#damage-coney}

Coney quarters a fighter's strikes and grab strikes on class `0x80` when its brain is a Warrior's
(`human::Fighter::quarteredFor`), as [Damage](#damage) reads it (re-read in Ghidra 2026-10-21).

### Moves a hit cannot stop {#protected-moves-coney}

From the user's checklist, items 25 (a dive cut short) and 29 (teammates attacking a finisher's victim):

- **Built: no reaction under `0x1c16a40`** (`Fighter::takePending`, `combat::heldCancelsReaction()`): after the health
  drops, a victim whose clip holds any of `0x1c16a40` plays no reaction and is not interrupted, so a charge into a human
  takes the flung victim's 20 and plays on, as [at runtime](#charge-dive-runtime). Before it the charger fell (user
  report, 2026-10-11). Coney's paired clips hold no `0x400000` yet, so a paired move's victim is not covered.
- **Built: the lethal hit held back** (`Fighter::takePending`, `repo:src/human/fighter_victim.cpp`): out of health
  while his clips hold any of `0xc12200`, a human plays no reaction and keeps his clip; on the first update the flags
  are clear his dying reaction plays, so a tandem's defeated victim finishes his end clip first ([Coney's
  tandem](combat-moves.md#tandem-coney)). **Coney's choice**: the dying reaction is the one his hit picks; the
  knock-out with health 1 of [the run](#lethal-runtime) is not built ([Open questions](#open-questions), the
  knock-out after a held-back lethal hit).
- **Missing: no attack on a busy target.** `ai::startGuardOf` (`src/ai/attack_views.cpp`) never sets
  `StartGuard::targetHeldBusy` (the target's held `0xc08200`) or `targetPinned` (the partner exception), so teammates
  and enemies still start attacks on a finisher's victim and on a diving player.
- **Missing:** the player's grab search refusing a target holding `0x8fc57a60`, and the order of [When the order
  matters](#protected-moves) (the pending damage applied before the human's own command, which a reaction drops).

### Down and under attack {#ground-escape-coney}

Read 2026-10-11, the rise counter 2026-10-13 (`src/combat`, `src/human`, `src/ai`; issue #231):

- Built: the ground hit (195), the ground time and the mash's cut (`human/victim.*`). Whether a press counts while
  303 or 195 still holds a flag (the original counts none) is not checked.
- Built: the counter from the rise (`Fighter::riseCounter`, run in `Fighter::update` while the player still reacts).
  199's `0x48` at frame 14 holds the recovery `0x40000` and closes the window, and the `0x25` events are read with the
  task stack's frame rounding (`combat::eventBetween`, `anim::eventFrame`), which at 0.75 fires frames 11-13 on
  k = 15-17: the runtime's edges, not frames 11-13 through a 0.75 rate. Tests: `repo:tests/human/rise_counter_test.cpp`
  (square at k = 15-17 plays 620 that update, ends the stun and knocks the attacker down for 50; k = 9-14 and 18-24
  nothing; cross pressed at 14 counters at 15 on its release; 618 to the right; no target, an AI, or the manual stun:
  nothing) and `repo:tests/platform/disc_rise_counter_test.cpp` (the disc's 199 events; on `level99` a square at
  k = 16 counters and one at 18 does not).
- Missing: the grounded mount (`Mount_StartOnGrounded`) and the mounted player's inputs (`Player_UpdateTackled`: the
  struggle, the break, the reversal, the escape when the mounter's power runs out). Kind 14 has a reach but no move,
  and kind 42 on an AI himself is called "the get-up attack" where it should be one command that cuts his ground time.

### The mission failing {#game-over-coney}

`repo:src/warriors/game_over.h`, `PlayLevelMode::stepGameOver()`: the check runs each update in the order of [The
mission failing](#mission-failing), on at each level's start and switched by `EnableGameOverCheck`; a failure hides the
HUD and, 180 updates later (10 once cross is newly pressed), opens the mission-failed mode with `GSTRING.HUD` 20 or 21
as its title. Stand-ins ([Open questions](#open-questions): when a player is dead, and a tackled victim's wounded
bit): a player at 0 health counts as knocked out and none as dead, no player frees himself from
cuffs, the wait's `+0x414` never blocks it, a held member is one attached to his holder, and the route check is a route
request (Coney has no blocked links, and a level without path data always finds a route); the failed camera, the tint,
the blur, the music's fade and the `dyn_cross` icon are not built.

## Open questions

- **Who first sets the dying state**{#q-lethal-state}: which code sets state `0x80000000` on a lethal hit before
  `Human_PlayLethalReaction` runs (inferred: the damage message, human vtable `+0x44`), and what sets `0x800000`
  ([Moves a hit cannot stop](#protected-moves)). A watch on the record's state word during [the
  run](#lethal-runtime) would name the writer.
- **A tackled victim's wounded bit**{#q-tackled-wounded}: whether a tackled victim carries state `0x10000`, which the
  original's "free to help" test reads as wounded (`Human_IsWounded`); Coney's game-over stand-in counts a held member
  instead ([Who can help](#defeat-helpers)). A read of the state word during a tackle would tell.
- **The rise's play rate**{#q-rise-rate}: 199's exact rate (about 0.75-0.78 from the measured edges, [Down and under
  attack](#ground-escape)); the window's edges are known without it. Found from the clip's speed field or the task's
  rate in memory.
- **The knock-out after a held-back lethal hit**{#q-lethal-knockout}: whether every victim of a lethal hit held
  back by a paired clip ends knocked out with health 1, as the civilian of [the run](#lethal-runtime) did (inferred
  through `HitReact_WhileOut` `0x002686e8`, which sets health 1), or only a human with his flags. Coney plays the
  hit's own dying reaction until it is known. Found by the same run with a gang member as the victim.
- **When a player is dead**{#q-player-dead}: whether a player in a story level is ever made dead (state
  `0x100000000`) rather than knocked out: by an object of kind 29's last hit (`Human_OnHealthOut`), or by
  `Humans_Update` (`0x00249ab4`) once no player of his gang is conscious. Coney's game-over check counts no player as
  dead. Found by a watch on the player's state word in a level with such an object.
- **A knocked-out tackled victim**{#q-tackle-knockout}: whether a tackled victim is ever knocked out when
  `Tackle_OnVictimRiseEnd` fires ([Who sets and clears the knocked-out bit](#knocked-out-bit)). A breakpoint on the
  hook with the state word read would tell.

## How this was checked

The code claims were read in Ghidra on `SLUS_212.15` (NTSC-U); the clip events and the Anim Range List values were read
from the disc's character data (numbers only). The runtime claims were read with PCSX2 2.9.94 over PINE, before run
records existed, so none of them has one:

- **Damage**, **Reactions** and **Being hit** (below): a copy of slot 6 with the puppet civilian `PoizoCiv`, driven
  with the scripted pad.
- **A lethal hit during a paired move** and **A charge or a dive**: the setups are beside the runs; the second run of
  the charge and the fall that explains the first were made on 2026-10-12, with the scratch probes `walkout` and
  `charge1`.
- **Scripted knockdowns**: 2026-10-12, the natives' functions called through the `call-brains` hook.
- **Down and under attack**: scenarios `ground_kicked`, `ground_kicked_mash`, `ground_mounted_*` and, on 2026-10-13,
  `ground_rise_counter` and its variants.
- **Knocked out in a Rumble brawl**: the hook recordings of [Rumble](rumble.md#gang-battle-results).
- **Hit blood**: scenario `blood_knife_grab`, the knife grab kill with call hooks on the functions named.

### Being hit, at runtime {#being-hit-runtime}

A puppet civilian (`PoizoCiv`, type 417, power class 2) attacked, blocked and grabbed the player (Rembrandt, 900
health, power class 64) 1.0 m away at a chosen bearing, with `CfgAutoLockAndCombat` written to 0 so that the player
did not turn to face it. Every value here is confirmed (runtime) unless marked; updates are 1/30 s.

**Reactions.** Every hit played the reaction the [table](combat/index.md#hit-codes) gives for the attack's code and
side, from all four sides, for `S1`, `X1`, `SS2`, `SX2`, `XS2`, `XX2`, `SSS3`, `SSX3` and the special 653 / 655; the
civilian's own codes (its list differs from Rembrandt's: `SS2` `0x09`, `XX2` `0x26`, `XS2` `0x1b`, `SSX3` `0x2b`, `SSS3`
`0x16`, 653 `0x39`) gave, front / victim's left / behind / victim's right:

| Attack (code) | Front | Left | Behind | Right | Then |
| --- | --- | --- | --- | --- | --- |
| `S1` (`0x0a`) | 272 | 275 | 274 | 273 | idle |
| `X1`, `SS2` (`0x09`) | 275 | 274 | 273 | 272 | idle |
| `SX2` (`0x1a`, stun) | 280 | 283 | 282 | 281 | 357, idle |
| `XS2` (`0x1b`, stun) | 281 | 280 | 283 | 282 | 357, idle |
| `SSS3` (`0x16`, stun) | 276 | 279 | 278 | 277 | 357, idle |
| `XX2` (`0x26`) | 288 | 291 | 290 | 289 | 196, 199 |
| `SSX3` (`0x2b`) | 293 | 292 | | | 196, 199 |
| 653 / 655 (`0x39`) | 303 | 302 | 301 | 300 | 196, 199, 356, 357 |

`SSS3` stayed strength 1 on the player because he has flag `0x400`. The player's reaction starts on the attack's hit
update (`S1` 2 updates after its start, `X1` 6, `SS2` 4, `SX2` 5, `XX2` 5, `SSS3` 5, `SSX3` 6, 653 25 for the
civilian's clips).

**Stun and ground time.** The player's stun is **200 ms** (`+0x100` = hit + 200): shorter than his reaction clips, so
the stunned reactions went straight to 357 `STUNNED_EXIT` when the clip ended (0.9-1.2 s after the hit), with no
356 loop. Knocked down, `+0x104` = hit + **2750 ms**; the player lay in 196 and rose with 199 **83 updates (2.77 s)
after the hit** with no input. The crushing 653 also set the stun to the rise + 200 ms, so after 199 he stood in 356
for 7 updates, then 357.

**Mashing.** A press while still in the knockdown reaction (288) did nothing. Pressed while lying in 196 with about
1.3 s left, one square press made him rise on the next update in 2 runs of 5, and about 13 updates later (when the
time ran out, or at a second press) in the other 3: consistent with a cut of 2750 / 1, 2 or 3 ms per command (the
cuts of 2750 and 1375 cover 1.3 s, 917 does not). Pressing every 4 updates from the hit got him up at the end of the
reaction clip (update 63 instead of 107).

**Hit armour.** The player pressed square (`S1`) and the civilian's `S1` hit him at chosen moments: in his wind-up
(`+0x08` `0x1`) and in his chain window (`0x2`) the 14 damage landed but **no reaction played** and his attack went
on (his `S1` still hit); in the attack's end (`0x4`) and before his clip started he played the reaction (274) and his
attack was lost.

**Damage taken** was the attacker's Anim Range List value with no reduction: 14 `S1`, 23 `X1`, 28 `SS2`, 37 `SX2` /
`XS2`, 46 `XX2`, 42 `SSS3`, 51 `SSX3`, 50 or 70 for 653 / 655. At other times the same civilian dealt 18 `S1`, 27
`X1` and 45 `SX2` / `XS2`: the **Anim Range List is shared** by the humans that use the same character data (record
`+0x160` pointed to one list for `PoizoCiv`, `Civilians0`-`4` and Ash), and `AnimRange_ApplyClassDamage` writes each
new human's class damage over it, so the newest human's class decides everyone's damage. With type 417
(`PoizoCiv`, class values 27, 18, 54, 45, 45, 36, 63, 54 for indices 0-7) the list read 18 / 27; after a type 418-420
civilian appeared (23, 14, 46, 37, 37, 28, 51, 42) it read 14 / 23. The class values of the humans in the street
(`CfgChar` `+0xb8`, the [index → anim id](combat/index.md#damage-table) map):

| Type | 0 `X1` | 1 `S1` | 2 `XX2` | 3 `SX2` | 4 `XS2` | 5 `SS2` | 6 `SSX3` | 7 `SSS3` | 16 653 | 24 grab strikes | 25 throws |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 417 `PoizoCiv` | 27 | 18 | 54 | 45 | 45 | 36 | 63 | 54 | 70 | 60 | 70 |
| 418-420, 271, 272 civilians, bums | 23 | 14 | 46 | 37 | 37 | 28 | 51 | 42 | 50 | 20 | 30 |
| 26 Vermin, 38 Ash | 23 | 15 | 46 | 38 | 38 | 31 | 53 | 46 | 69 | 50 | 57 |
| 30 Rembrandt (before the 115 % scale) | 23 | 15 | 46 | 38 | 38 | 31 | 53 | 46 | 5 | 50 | 57 |

**Blocking** (R1 held, the player's `0x8` pressed through the pad): the block started on the next update (606, state
`0x8001`, no 605 seen) and **every blocked hit did 0 damage**, from every side. Light and medium hits played the block
reactions of the [table](combat/index.md#block) by the reaction's direction: `S1` 608 front, 611 left, 610 behind; `X1`
611, 610, 609; `SS2` 608 then 611; `SSS3` 612, 615, 614. The heavy and crushing ones (`SSX3`, 653 / 655) played **616
`BLOCK_DODGE`** (a duck) about 4 updates before their hit, which then missed: `0x00261578` plays 616 (or 628 with weapon
type 3 against weapon type 3) when the blocker's record `+0x14` is `0xd` (`0x00254e78`), confirmed (code); the
attacker's anim event `0x24` sets `0xd` ([Block](combat/index.md#block)). The block reaction table at `0x00510898` holds
-1 in entries 0-3 and 12-15, so a modified strength of 3 (entry 0) would break the block (confirmed (code) at
`0x002671a8`); no hit broke the player's block. The puppet civilian, blocking the player, ducked under `X1`, `XX2` and
`XS2` (616) and blocked `S1`, `SS2`, `SSS3` and `SSX3`.

**Rage gained by the player** (rage 0 and the hold timer reset before each combo; `S1` gains are 0 or 1):

| Hit | `S1` | `X1` | `SS2` | `SX2` | `XS2` | `XX2` | `SSS3` (19 or 20) | `SSX3` | counter 76 | 104 in a grab |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Rage | 1 | 5 | 1 | 13 | 10 | 6 | 4 | 7 or 15 | 10 | 1 |
| Blocked | 0 | | 0 | | | | 0 | 6 | | |

Being hit gave the player no rage. Every gain set the hold timer to now + 5000 ms. A later run gave `S1` 1, `X1` 5,
`XS2` 10, `SX2` 13 and `SSX3` 15 every time; the 7 is the halved gain ([Rage](combat-rage.md#rage)): `SSX3`'s two awards
3 and 8 give 2 + 5 at a factor 0.72, and `S1`'s 1 gives 0.
