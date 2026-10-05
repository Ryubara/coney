# Combat (the player on foot)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 (2026-10-05), reading memory over PINE in a street level ("the street") from save states,
loaded read-only and copied with pad input patched in ([Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)).
The player there is Rembrandt-class "player" human 2 with 314 of 900 health; the test victim is a civilian
(`PoizoCiv`, 600 health) moved in front of the player by writing the transform table. Damage and timings below were
read every update (30 per second) while the scripted pad played.

## Purpose

How the player fights on foot: the attack buttons and their chains, the moving attacks (from a walk, a run and a
sprint), blocking, grabbing and what follows a grab (strikes, throws, mugging), tackles, the damage a hit does and
how the target reacts, health, the power meter that grabs spend and the rage meter that hits fill, and how a target
is picked. The same input path breaks objects (glass, car windows) and runs the stereo theft minigame, so those are
here too. Movement, sprint, jumps and climbs are on [Characters](characters.md); the clips on
[Animation](formats/animation.md); the anim ids on the [Anim ids](../references/anim-ids.md) list.

In one paragraph: the pad's buttons become a **command id** per update through nine trigger tables (held, pressed,
released, tapped, long hold, history hold and combinations). `Player_UpdateActions` dispatches on the command and the
human's **state flags**: square and cross start attacks whose damage is the anim id's Anim Range List value; a
second and third press inside the attack's **chain window** picks the next attack of a three-hit combo; circle tapped
grabs, circle held tackles; R1 held blocks; L2 held plus cross or square at a run charges or dives. Each hit leaves a
pending damage on the target, which plays a reaction chosen by the attack's kind and direction; some hits stun or
knock down. Hits fill the attacker's rage; L1 + R1 with a full meter starts rage.

## Original structure

No source file names this code: it lies in the unnamed stretch after `Human/cns/cnsplayertag.cpp`
([Source map](source-map.md)), and in `Human/` for the shared human calls. Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00147430` | `AddCommand` | appends `{mask, command, buttons, extra}` to one of nine trigger tables | confirmed (code) |
| `0x00147940` | `Commands_Match` | matches the tables each update, stores the command at per-player `+0x20` | confirmed (code) |
| `0x0027c120` | `Player_UpdateActions` | the dispatcher below | confirmed (code) |
| `0x00280630` / `0x00280708` | `Player_UpdateChain` | buffers and plays the next attack of a combo | confirmed (code) |
| `0x00286cc8` | `Player_Square` | square: standing, walking, running, snap, grounded and object attacks | confirmed (code) |
| `0x00287a18` | `Player_Cross` | cross: the X attacks | confirmed (code), runtime |
| `0x0027d800` / `0x0027d900` | `Player_Charge` / `Player_Dive` | L2 + cross / L2 + square at a run | confirmed (code), runtime |
| `0x00284920` | `Player_GrabOrTackle` | circle: target search, then grab or tackle | confirmed (code), runtime |
| `0x0027f3b0` | `Player_UpdateGrabbing` | strikes, power strike, throws, mug, spin in a grab | confirmed (code), runtime |
| `0x0026dd08` | `Player_Throw` | picks the throw by stick direction and walls | confirmed (code), runtime |
| `0x002856b8` | `Player_UpdateMugging` | the mugging stick game | confirmed (code), runtime |
| `0x0027e6d8` | `Player_UpdateTheft` | the theft minigames (stereo) by mode | confirmed (code), runtime |
| `0x002843f8` / `0x00284340` | `Player_StartRage` | L1 + R1 with a full meter | confirmed (code), runtime |
| `0x0027a6c0` | `Player_PickTarget` | keeps or searches a target for an action | confirmed (code) |
| `0x00264178` | `Player_ObjectAttack` | strikes at a breakable object | confirmed (code), runtime |
| `0x002625a8` | `Attack_Start` | starts an attack clip, counts the combo | confirmed (code) |
| `0x0021b290` | `Strike_Contact` | computes a hit's damage | confirmed (code) |
| `0x00264bd8` | `Human_AddPendingDamage` | keeps the update's largest damage on the target | confirmed (code) |
| `0x00264cf8` | `Human_AddRage` | rage gain | confirmed (code) |
| `0x002542e8` | `AnimRange_Damage` | the Anim Range List's `+0x0a` | confirmed (code), runtime |
| `0x00226448` / `0x00226510` | `Human_SpendPower` / `Human_PowerFraction` | the power meter | confirmed (code), runtime |
| `0x00222ef0` | `Human_HealthPercent` | health over maximum | confirmed (code) |

## Data

### Commands {#commands}

`AddCommand(trigger, button mask, command, ...)` (the script binding, [character bindings](../references/bindings/character.md))
fills nine tables of 12-byte entries `{u16 mask, u32 command, u16 buttons, u16 extra}`; `global.lua` fills them for
the street. The tables as read at runtime in the street (confirmed (runtime)); the matchers are confirmed (code):

| Trigger | Table | Matched when (matcher) | Button → command |
| --- | --- | --- | --- |
| 1 held | `0x005ddd10` | the buttons are down (`0x00144b88`) | L1 → 6, R1 → 4, L2 → 5, R2 → 1, square → `0x15`, cross → `0x16` |
| 2 pressed | `0x005dddd0` | newly down this sample (`0x00144bf0`) | up `0x26`, down `0x25`, right `0x28`, left `0x27`, triangle `0xa`, L1 7, R1 3, square `0xf`, cross `0x12`, circle `0x1e` |
| 3 released | `0x005dde90` | newly up (`0x00144ba8`) | L1 → 8, R2 → 2 |
| 4 query | | | none in the street |
| 5 tapped | `0x005ddf50` | released after 1 to 6 samples down (`0x00144ce0`) | circle → `0xd` |
| 6 long hold | `0x005ddfb0` | released after 1 to 3 samples, or the 4th sample held (`0x00144d60`) | cross → `0x10` |
| 7 history hold | `0x005de010` | exactly the Nth sample held, N = `0x0050b708` (7 at runtime, 5 in the file) (`0x00144e50`) | triangle → `0xb`, circle → `0xe` |
| 8 combination held | `0x005de070` | the last button of the set goes down this sample (`0x00144ef8`) | select + up/right/down/left → `0x29`/`0x2a`/`0x2b`/`0x2c`; select + L3 → 9; circle + cross → `0x23`; L1 + R1 → `0x1f` |
| 9 combination press | `0x005de130` | every bit of both masks down and the second newly pressed (`0x00144f48`) | L2 + square → `0x21`; L2 + cross → `0x20`; cross + square → `0x22`; circle + triangle → `0x24` |

The **order** of matching decides which command wins when several match in one sample (a later one overwrites):
held, history hold, pressed, query, combination held, combination press, tapped (only on a sample with a release),
long hold, released. Confirmed (code) at `0x00147940`. So:

- Pressing square alone gives `0xf`; pressing square while cross is held gives `0x22` (combination press overwrites
  pressed).
- Tapping circle gives `0x1e` on the press and `0xd` on the release; holding it gives `0xe` on the 7th sample.
- A cross tap gives `0x12` on the press and `0x10` on the release (long hold fires on a short press's release).
  The X attack starts on `0x10`; inside a chain the `0x12` of the press is what is buffered.

These buttons are the pad word's bits ([Pad record](frontend.md#pad-record)); the earlier reading on
[Characters](characters.md#buttons) took square (`0x80`) for left (`0x8000`) and cross (`0x40`) for down (`0x4000`).

### State flags {#state-flags}

The 0x180 record's `+0x00` flags ([Characters](characters.md#the-record)) that combat uses. Confirmed (code) where a
test is cited in the behaviour below; seen at runtime unless marked.

| Bit | Meaning |
| --- | --- |
| `0x1`, `0x2` | fight stance (`0x00228340` tests `0x3`) |
| `0x4` | has a target (inferred: set with the target at a grab) |
| `0x10` / `0x20` | grabbed from the front / rear |
| `0x40` / `0x80` | grabbing from the front / rear (a front grab reads `0x45`) |
| `0x100` / `0x200` | mugging / being mugged |
| `0x400` | tackling, mounted on the target (seen `0x405`); the victim `0x800` (seen `0x801`) |
| `0x1000` | throwing (seen `0x1005`) |
| `0x8000` | blocking (with `0x1`: `0x8001`) |
| `0x1000000` | sprint asked for ([Sprint](characters.md#sprint)) |
| `0x4000000` | in a theft minigame |

Record `+0x08` holds the attack's **phase** while an attack plays: `0x1` wind-up, `0x2` the chain window, `0x4` the
end, `0x40000` recovery. `0x10` is set at a grab or tackle start and in object attacks (it also forbids a sprint).
`0x200` while tackling, `0x10000` on the tackled victim, and `0x20000000` on the victim just before the tackle hit.
Confirmed (runtime).

### Other record fields {#record-fields}

| Where | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| record `+0xb8` | u32 | the buffered next attack: cross 1, square 2, `0x14` 4, `0x13` 8; a snap square `0x102`, `0x202`, `0x402` | confirmed (code) at `0x00280708` |
| record `+0xbc` | u32 | the combo count; `Attack_Start` adds 1 | confirmed (code) |
| record `+0x118` | s16 | the damage pending this update (the largest of the update's hits) | confirmed (code) at `0x00264bd8` |
| record `+0x11a` | s16 | its kind (the attack's Anim Range List `+0x0c`, inferred) | confirmed (code) |
| record `+0x12c` | u32 | mugging progress, ms | confirmed (code), runtime |
| record `+0x144` / `+0x146` | s16 | **health** / its maximum | confirmed (code), runtime (player 314/900, civilian 600/600) |
| record `+0x148` | s16 | the **power meter** ([Power meter](#power-meter)) | confirmed (code), runtime |
| human `+0xc8` | u32 | the current target's handle | confirmed (runtime) |
| human `+0x370` | int | money carried (a mugging takes it: 18 → 0) | confirmed (runtime) |
| human `+0x5a8` | float | the mugging's target stick angle | confirmed (code) |
| human `+0x648` | u32 | the rage warning timer (now + 5000 ms) | confirmed (code) |
| human `+0x650` | int | **rage** | confirmed (code), runtime |
| human `+0xe0` | u64 | more flags: `0x80000` while raging; `0x4000` doubles the damage it deals | confirmed (code), runtime for `0x80000` |

### The Anim Range List and damage {#damage-table}

Every attack's base **damage is the Anim Range List's `+0x0a`** for the attacker's character
([Animation](formats/animation.md#anim-range-list)). `Strike_Contact` (`0x0021b290`) reads it with
`AnimRange_Damage(attacker, anim id)` (`0x002542e8`). Every value below was seen as the exact health lost by a
600-health civilian (confirmed (runtime)), and matches the list:

| Input | Anim id | Damage | Victim reaction |
| --- | --- | --- | --- |
| square | 12 `ANIM_ATTACK_S1` | 17 | 272 small high front |
| square, square | 16 `SS2` | 36 | 273 small high right |
| square ×3 | 19 `SSS3` | 53 | 276 knockback mid front, then 356 / 357 stunned |
| square, square, cross | 17 `SSX3` | 61 | 279 knockback mid left |
| cross | 11 `X1` | 26 | 274 small high back |
| cross, cross | 13 `XX2` | 53 | 273 |
| square, cross | 15 `SX2` | 44 | 272, then stunned 356 / 357 |
| cross, square | 14 `XS2` | 44 | 280 knockback high front |
| square at a run | 24 `ATTACK_FROM_RUN` | 20 | |
| L2 + cross at a run | 0 `RUNNING_ATTACK_CHARGE` | 31 | |
| L2 + square at a run | 1 `RUNNING_ATTACK_DIVE` | 26 | |
| square or cross in a grab | 51, 53, 55 `GRAB_COMBO_STRIKE_01`-`03` | 57 | 52 / 54 (55's not read) |
| circle + stick in a grab | 147-153 `THROW_01_*` | 66 | 152 (rear), then 196 grounded |
| the same with a wall in reach | 155 `THROW_02_FROM_GRAB_FRONT` | 264 | |
| square at a grounded target | 193 / 194 `GROUNDED_STRIKE_01` / `02` | 34 | |
| square on a tackled target | 212 `MOUNTING_STRIKE` | 30 | |
| square, stick > 0.95 to a side or back | 25, 27, 29 `SNAP_*` | 31 | |

The snap ids 26, 28 and 30 (the `_02` variants) use the filler clip `missing_anim_filler` and are never chosen in the
street (inferred). The `_HOLD` ids 18 and 20 were not picked by holding the third button (confirmed (runtime)).

### Constants {#constants}

Read at runtime in the street; the setters are on the [config bindings](../references/bindings/config.md).

| Address | Value | Use |
| --- | --- | --- |
| `0x0050b708` | 7 | samples a history hold needs (circle → tackle) |
| `0x00510274` | 0.6 | not traced (a `CfgDamageEndurance` value, inferred from its neighbour) |
| `0x00510278` | 0.25 | `CfgPowerEndurance`: the power fraction a power strike needs and a throw costs |
| `0x00510290` / `0x00510294` | 5000 / 20000 | `CfgRageHandlers` (ms) |
| `0x005102b4` | 1 | `CfgSnap`: snap attacks on |
| `0x005102b8` / `0x005102bc` / `0x005102c0` | 1000 / 15 / 250 | `CfgButtonMash`: target, decay per update, press gain (used halved) |
| `0x005104b8` | 1 | `CfgAutoLockAndCombat` |
| `0x005104bc` | 4, 6.25, 9 | `CfgDistances`, squared (2, 2.5, 3 m) |
| `0x005108d8` / `0x005108e0` / `0x005108e4` | 25 / 1.0 / 0.1 | `CfgRagePoints`: the cap, and the factors below and above it |
| `0x00510980` | 6, 4, 3, 3, -1 | not traced (a short table read by the grab code) |
| `0x00510998` | 0.2 | the power cost of a grab strike, halved for a player |
| `0x00514878` | 4, 1.21, 4, 4, 3.0625, 2.25 | `CfgActionDistance`, squared |

The player's **power class** record (64, `0x006619a0 + 64 × 0x44`, [Power classes](characters.md#power-classes)):
`+0x28` = **400, the power meter's maximum**; `+0x2a` = 60, its refill per second. Its floats were 1.3, 0.3, 1.0,
1.0, 1.0, 1.0, 0.75, 3.0, 3.0, 0.35 and its other s16 values 135, 40, 200, 300, 2750 (not traced). The **Warrior
class** record (`CfgWarriorClass`, `0x006b65c0 + c × 14`; class 6 for the player here, human `+0x1ba`) was
`4e 00 90 c8 f0 32 73 03 01 01 01 02 03 02`: s16 `+0x00` = 78 is the **rage maximum**, byte `+0x02` = 144 the rage
gain percentage, byte `+0x08` picks the mash gain factor and byte `+0x0b` the stereo theft's turns, byte `+0x0c`
the mugging parameters. Confirmed (runtime) for the bytes; the uses confirmed (code) at the functions below.

## Behaviour

### The dispatcher {#dispatch}

`Player_UpdateActions` (`0x0027c120`) runs every update for a pad-controlled human. Confirmed (code); in order:

1. **Block** if the fight test `0x00224f28` holds and R1 is held (mask 8), or if already blocking (`0x00223ad0`:
   state `0x8000` or `+0x08` `0x1000`) and the command is `0x1f` without a full rage meter, or 8 (L1 released). It
   clears the sprint, calls `0x002801e8`, sets state `0x8001`, starts rage on `0x1f` with a full meter
   (`0x0027ffa0`), and returns 0 on command 4 (R1 held), so nothing else runs while R1 is held. This is the sprint's
   "other clear" on [Characters](characters.md#sprint).
2. The chain (`0x00280630`), then the sprint (`0x0027d5a0`).
3. The state routes: mugging (`0x100`) → `Player_UpdateMugging`; being mugged (`0x200`) → release; a theft
   (`0x4000000`) → `Player_UpdateTheft`; `0x8000000` / `0x10000000` → nothing; `0x400` → `0x0027ec20` (tackling);
   `0x800` → `0x0027f1a0` (tackled); `0x18000000000` → `0x00284140`; grabbing (`0xc0`) → `Player_UpdateGrabbing`;
   grabbed (`0x30`) → `0x0027fd68` (struggle, not traced); throwing (`0x1000`) → `0x0027df38`; `0x2000` →
   `0x0027e018`; `0x200000` → `0x0027e040`.
4. The commands: `0x36` → `0x0027de78`; `0x37`, `0x38` → `0x00287fe0`; triangle `0xb` with human `+0x5b8` →
   `0x002811f0`; `0x33` → `0x00281188`; `0x32` → `0x002832c8`; `0x22` or `0x39` → `0x00288838`; `0x22`, `0x30`,
   `0x23`, `0x24` → `0x00287730`; **`0x20` → charge**; **`0x21` → dive**; **`0xf` → square**; **`0x10` → cross**
   (weapon types 4, 5, 6 → `0x002880d8` instead); **`0xd`, `0xe` → grab or tackle**; `0x27` → `0x00286ba8`; 7 (L1
   pressed) → `0x0027da10`; 6 (L1 held) → `0x0027dc00`; 8 → `0x00227b98` / `0x00227a90`. Commands `0x30`-`0x39` are
   not made by the street's tables; scripts or weapons make them (inferred).

### Attacks and chains {#attacks}

**Square** (`Player_Square`, `0x00286cc8`) takes a target from `Player_PickTarget(0x40000000, h)`, then, confirmed
(code):

- a grounded target → 193 (`0x00261a08`); a tackled one → 212; a grabbed one → 120;
- a breakable object → `Player_ObjectAttack` ([Breakables](#breakables));
- the stick above 0.95 and more than 45° from the facing, with `CfgSnap` on → a **snap** attack, 25 right, 27 left,
  29 back (`0x00264460`);
- at gait 4 (run) with record `+0x08` clear and the stick above 0.95 → 24, from a run; at gait 1-3 with the stick at
  0.12 or more → 23, from a walk;
- otherwise 12, `S1`. Strafe ids 31-33 come from the combat-walk path (not tested).

**Cross** (`0x00287a18`) starts 11, `X1`, on command `0x10`.

**The chain** (`0x00280708`) accepts the next button while record `+0x08` has `0x2`, or during the wind-up `0x1` while
the combo count is below 2 (or is 2 and the current id is 16), and stores it in `+0xb8` (one buffered press; a
second overwrites it). When `0x2` opens it plays the next attack. Confirmed (code), and every step confirmed
(runtime):

| From | Square → | Cross → |
| --- | --- | --- |
| 12 `S1` | 16 `SS2` | 15 `SX2` |
| 11 `X1` | 14 `XS2` | 13 `XX2` |
| 16 `SS2` | 19 `SSS3` or 20 at random (19 every time at runtime) | 17 `SSX3` |
| anything else | the chain ends | the chain ends |

A fourth press after `SSS3`, or a third cross after `XX2`, does nothing. A square buffered with the stick above 0.95
to a side or back plays the snap attack instead.

**Timing of S1** (confirmed (runtime), updates of 1/30 s from the press): the hit lands **2 updates** after the press;
`0x2` (the chain window) opens at about **6** and closes at about **15**; then `0x4` and recovery `0x40000`; the
fight idle 358 returns at about 20 (0.66 s). A press at update 5 was buffered and played `SS2` when the window
opened; a press in recovery (update 19) was dropped; a press at update 24 started a new `S1`. Pressing the next button
anywhere from the hit to update 15 continues the combo.

### Moving attacks: run, charge and dive {#run-attacks}

At a run (gait 4, 7.8 m/s, stick 1.0) **square** plays 24 for about 0.7 s and the run resumes. At a walk it plays 23.
Confirmed (runtime).

**The charge**: **L2 held, then cross pressed** (command `0x20`) at gait 4 with record `+0x08` clear, or at gait 5
(a sprint), plays 0 (`gen_charge_shoulder`) for about **0.9 s** at about 7.45 m/s. **The dive**: L2 held, then
square pressed (`0x21`), plays 1 for about **2 s**. Confirmed (code) at `0x0027d800` / `0x0027d900`, confirmed
(runtime). At a walk or standing the combination does nothing (the cross or square press under it is overwritten,
inferred from the matching order).

**Slot 10 (a fence)**: sprinting at a breakable fence (L2 held, stick 1.0 straight at it) and pressing cross charges;
the charge stopped at the fence, and about 1.4 s later a cutscene placed the player on the far side. Confirmed
(runtime). That the break is a script trigger reacting to the charge is inferred.

### Block (and no dodge) {#block}

**R1 held** in a fight stance blocks: anim state 21 then 606 `BLOCK_SUSTAIN` (605 `BLOCK_START` was not seen as the
playing id), state flags `0x8001`. The stick at 0.8 to a side while blocking gives anim state 24, 607
`BLOCK_SHUFFLE`, turning in place. Cross pressed while blocking plays `X1` and returns to the block. Confirmed
(runtime). The hit-on-block reaction (608 `BLOCK_HIGH_FRONT`) and how much a block saves were not tested. No separate
dodge command exists in the street's tables (confirmed (runtime)); the shuffle is the only evasive move found.

### Grab and tackle {#grab}

**Circle tapped** (released within 6 samples, command `0xd`) grabs; **circle held** for 7 samples (`0xe`) tackles.
`Player_GrabOrTackle` (`0x00284920`), confirmed (code):

1. Refused while record `+0x08` has any of `0xfc7eaf7`, or while human `+0x1be` is -1 with `+0x1c0` pointing at
   `0x00244770` (not traced).
2. Without a target, it searches with `0x0027ac30(far × 1.25)`: for a tackle the far range of id 3 (2.999 m, so
   3.75 m), for a grab that of id 70 `GRAB_INTRO` (2.499 m, so 3.12 m).
3. It clears the buffered chain, enters a fight stance if not in one (`0x00280068`), then: a grounded target is
   mounted (`0x00271b30`); a target already grabbable from behind (`0x00258e10`) is grabbed by `0x0026f860`; a
   tackle starts `0x002707a8(h, 3, 5)`; a grab starts `0x0026c548(h, 0, 0x48)`.

At runtime (confirmed (runtime)):

- **Grab**, victim 1 m ahead: 71 `GRAB_PLAYER_INTRO`, then 72 `GRAB_FRONT_END`, then the hold 82; the victim plays 73,
  then 83. Player state `0x45`, victim `0x10`. With nobody in reach: 71, then 69 `GRAB_MISS`, then 389.
- **Tackle**, victim 2.6 m ahead, circle held 0.55 s: the command `0xe` came on the 7th sample (0.23 s), the player
  played 4 `TACKLE_PLAYER_INTRO` covering the distance, then 5 `TACKLE_HIT_FROM_FRONT` with the victim on 6, state
  `0x405` / `0x801`; after 1.4 s the player sat in 210 `MOUNTING_IDLE` on the victim's 207 `MOUNTED_IDLE`. Square
  there plays 212 (30 damage).

### In the grab {#grabbing}

`Player_UpdateGrabbing` (`0x0027f3b0`), confirmed (code); runtime where marked:

| Input | Effect |
| --- | --- |
| square | 51 or 53 at random (victim 52 / 54), 57 damage; confirmed (runtime) |
| cross | 55, 57 damage; confirmed (runtime) |
| hold cross, press square (`0x22`) | the **power strike** 57 (63 in rage, 80 from the rear), if the power fraction is above 0.25 |
| triangle | **mug** the victim if it qualifies (`0x00225ff0`); confirmed (runtime) |
| circle with the stick above 0.25 | **throw** ([Throws](#throws)); confirmed (runtime) |
| circle without the stick | spin (`0x0026d998`), or `0x0026f008` from the rear |
| L2 (command 5) | `0x0026d998` / `0x0026d570` (not tested) |

Each grab strike costs 0.2 × 0.5 (a player) of the meter's maximum: **40 of 400**. Confirmed (code) at
`0x00510998`, consistent with the runtime meter.

### Throws {#throws}

`Player_Throw` (`0x0026dd08`) compares the camera-turned stick with the player's facing: within 45° → 147
`THROW_01_FROM_GRAB_FRONT`, beyond 135° → 151 rear, otherwise 149 right or 153 left. A wall within reach of the
throw gives the `THROW_02` set (155 front, 264 damage). A throw costs the power fraction 0.25 (**100 of 400**). The
victim plays 152 and lands in 196 `GROUNDED_IDLE`, where square plays the grounded strikes. Confirmed (code), and
confirmed (runtime) for 147, 151, 155 and the damage.

### Mugging {#mugging}

Triangle in a front grab spins the victim to a rear hold (78 / 79, state `0x85`), then 338 / 339 intro and the
340 / 341 loop (player `0x105`, victim `0x200`); 342 / 343 play while the stick is on target. Confirmed (runtime).

`Player_UpdateMugging` (`0x002856b8`), confirmed (code): the stick must be above **0.5** and within the tolerance of
the target angle (human `+0x5a8`); the time on target adds to record `+0x12c` (ms). Every period the target angle
moves (`0x002855f8`), by at least the tolerance plus 20°. At the required time the mugging succeeds: 344 / 345, the
money moves (victim `+0x370` 18 → 0 at runtime), then 80 / 81 and back to the hold 82 / 83. Past the fail time it
fails. The parameters come from `0x00284ca0` by the victim's class (human `+0x11b`) and the Warrior class byte
`+0x0c`; the record seen: `+0x04` required **5000 ms**, `+0x08` period **2500 ms**, `+0x0c` fail **50000 ms**,
`+0x10` **50°** and `+0x14` **60°** (the tolerances, which of the two applies when not traced).

### Damage, health and reactions {#damage}

`Strike_Contact` (`0x0021b290`) takes the [damage table](#damage-table)'s value, adds a held weapon's bonus, and for
a player applies upgrade percentages (categories `0x13` / `0x19` and `0x1e` / `0x21` / `0x1d` / `0x20`, inferred to be
the upgrade levels). `0x0021d680` → `Human_AddPendingDamage` (`0x00264bd8`) stores it at record `+0x118` if larger
than what is pending, with the kind at `+0x11a`; the damage is **doubled** when the attacker has human flag `0x4000`
and **quartered** for class `0x80` against brain type 3. Confirmed (code). Health (record `+0x144`) fell by exactly
the table's value on every hit tested (confirmed (runtime)).

The reaction is picked from the kind and the hit's direction ([table](#damage-table)): small reactions 272-274 for
light hits, knockbacks 276, 279, 280 for heavy ones, and the stun 356 / 357 after `SSS3` and `SX2` (confirmed
(runtime); the selection rule is inferred). Knockdowns to the ground were seen only from throws and tackles.

### Power meter {#power-meter}

Record `+0x148`, maximum the power class's `+0x28` (400) through `0x00223068` (with upgrades). It **drains 1 every 2
updates** (15 per second) while grabbing or tackling and **refills 2 per update** (60 per second, `+0x2a`) otherwise.
`Human_PowerFraction` (`0x00226510`) and `Human_SpendPower` (`0x00226448`). Confirmed (code), confirmed (runtime)
(400 → 377 over 1.4 s of a tackle).

### Rage {#rage}

Rage is human `+0x650`, up to the Warrior class's s16 `+0x00` (78 for class 6). `Human_AddRage` (`0x00264cf8`),
confirmed (code), adds, only for a player-flagged human (`0x2000000`) not already raging:

`round(points × f × gain / 100 × h × s)`, where `f` is 1.0 up to 25 points and 0.1 above (`CfgRagePoints`), `gain`
the Warrior class byte `+0x02` (144), `h` 0.5 when a per-player flag is set, and `s` a multiplier while in state
`0x1000`. The points are awarded per anim id through the stats system (`0x002653d8`, `0x00264fa0`; stats
`0x006fe490 + player × 0xc0`, points table `0x00715510`). Gains seen per hit at runtime ranged from 1 (`S1`) to 10
for most hits, with 13 and 15 (`SSX3`) for the heaviest. Confirmed (runtime).

**L1 + R1** (`0x1f`) with a full meter starts rage (`0x002843f8` → `0x00284340`): 643 `RAGE_START` for about 2.1 s,
human `+0xe0` flag `0x80000`; the meter then drained about 9.5 per second. Confirmed (runtime). The command handled
at `0x0027bbd0` uses a flash (665 `SPECIAL_FLASH`, inventory slot 1) when health is below its maximum, or fills rage
when health is full and an upgrade allows it (confirmed (code)).

### Target selection {#targets}

`Player_PickTarget(range, h)` (`0x0027a6c0`) keeps the current target (human `+0xc8`) while `0x0027a120` accepts it;
otherwise it searches humans within range × 1.1 through two filters (`0x00279410`, `0x00279568`), then objects and
glass (world `+0x844`, `+0x840`, `+0x84c`), then repeats at range × 0.9, 0.8 and 0.7. Confirmed (code). Lock-on
(`CfgAutoLockAndCombat`, 1, with the routine `0x00241b90`) was not traced.

### Breakables {#breakables}

**Glass cabinet** (slot 4): square at the cabinet plays 662 `SPECIAL_BREAK_OBJECT_MID` for about 1.1 s with the
object as the target; then **triangle** picks up one item each press with 464 (one-handed pick-up, left high).
Confirmed (runtime). **Car window** (slots 2 and 5): square plays 662 on the window. Confirmed (runtime).

`Player_ObjectAttack` (`0x00264178`) picks the clip by the target point's height above the feet: up to 0.8 m → 661
`LOW`, above → 662 `MID` (663 `HIGH` is never chosen); below the feet → 194. It first approaches when the object is
between reach × 0.5 and far × 1.5 of the clip's range. Confirmed (code).

### The stereo theft {#stereo-theft}

**Triangle** at a car's open window with a radio (slot 5) plays 683 `STEREO_STEAL_INTRO` (0.55 s), then the loop 684,
state `0x4000000`. `Player_UpdateTheft` (`0x0027e6d8`) runs one of three games by the mode at `+0x46` of the per-player
record `0x0051489c + 0x168 + p × 0x5c`. Confirmed (code):

- **Mode 1, mash**: alternate L1 and R1 (commands 6 and 4, held); each alternation adds 125 × 1.5 or × 0.7 (Warrior
  byte `+0x08`), the meter loses 15 per update, and 1000 completes it (`CfgButtonMash`).
- **Mode 2**: `0x002878b8`, a timed cross press (lockpick-like, not tested).
- **Mode 3, the car radio here**: `0x0027e908`. Rotate the left stick **anticlockwise**, the stick above 0.8 in this
  update and the last, turning less than 90° per update. The angle turned adds to `+0x48`; at each stage's target the
  stage (`+0x4c`) advances after a 250 ms pause; **4 stages** succeed (`0x00278628(h, 1)`, 685 `STEREO_STEAL_END`).
  The stage target was 6π (3 turns) here, from Warrior byte `+0x0b` (2); 2π applies otherwise (the exact condition
  is inferred).

Any command outside the game's own set (square, circle, R1, L2 and others) **fails** it: 686 `STEREO_STEAL_FAIL`.
At runtime 1.5 turns per second took 9.6 s to succeed, and one R1 press failed it. Confirmed (runtime).

### Input scripts {#input-scripts}

Coney's `--input-script` lines (frame = one 30 Hz update; [building](../guides/building.md)) that reproduce each
behaviour, from a standing player facing the target with the camera behind; stick values are the in-game magnitudes. The
gaps
between the grab's steps are generous, not measured minimums:

```text
# S1, SS2, SSS3: a press every 6 updates
10 tap square
16 tap square
22 tap square
# X1 then XS2
40 tap cross
46 tap square
# snap attack: full stick 90 degrees off the facing (right, with the camera behind the player), then square
70 stick left 100 0
71 tap square
72 stick left 0 0
# grab (tap), strike, throw forward (away from the camera)
90 tap circle
140 tap square
180 stick left 0 100
181 tap circle
182 stick left 0 0
# tackle (another target): hold circle 7 updates or more
185 press circle
195 release circle
# block for a second, shuffling at 0.8
200 press r1
210 stick left 80 0
230 stick left 0 0
230 release r1
# charge: run, hold L2, press cross
250 stick left 0 100
265 press l2
275 tap cross
300 release l2
300 stick left 0 0
# rage, with a full meter
320 press l1 r1
322 release l1 r1
```

The power strike is `press cross`, then `tap square` a frame later, while grabbing. The stereo theft's mode 3 wants
the stick at 100 % stepped anticlockwise every update, under 90° a step (for example 30° steps: `stick left 100 0`,
`87 50`, `50 87`, `0 100`, `-50 87`, ...), about 12 turns in all; no other button until it ends.

## Coney's implementation

`src/combat/` holds the player's combat rules as a self-contained core, not yet wired into the player (it decides; the
player will play the clips it names and apply the hits). Everything runs on the fixed 1/30 s step; time-based meters
take game time in whole milliseconds and carry the fraction of a point, as the original does; the coin flips come from
a seeded generator (`CombatRandom`), so a run with the same seed and input is the same run.

| File | What it does |
| --- | --- |
| `commands.*` | the nine trigger tables (`CommandTables::street()` is the street's), and the matcher that turns each update's buttons into one command in the documented order, with the tap (1-6 samples), long hold (4th sample, or a release within 3) and history hold (7) counted per button |
| `attacks.*` | square's choice (target, snap, run, walk, `S1`), cross's `X1`, the object attack's clip, the charge and dive condition, the chain table, and `AttackChain`: the hit, the chain window, the end and the recovery counted in updates, one buffered press |
| `anim_ranges.*` | the Anim Range List decoded from the character data's chunk: direction, reach, far range, damage, kind, flags |
| `meters.*` | health, the pending damage (the update's largest), `strikeDamage()`, the power meter (400, refill 60/s, drain 15/s, `spend()`) and the rage meter (78, the gain formula, start, drain) |
| `grab.*` | when a grab may start, the search ranges (far range × 1.25), the nearest-candidate search, the throw by stick side and wall, and one update of a grab (strikes, power strike, throw, mug, spin, with their costs) |
| `stick_games.*` | the mugging, the stereo theft's rotation (mode 3) and the button mash (mode 1) |
| `player_combat.*` | the dispatcher: block, chain, meters, the routes (grabbing, tackling, mugging, theft) and the commands, in the original's order |
| `combat_tuning.*`, `src/debug/combat_tunables.*` | the values above as tunables, category **Combat** |

The tests (`tests/combat/`) drive these with the research's own input scripts, played through the pad records with
partial stick deflections: S1, SS2, SSS3 a press every 6 updates with the hits 2 updates after each; X1 then XS2; a
press in recovery dropped; the snap; the block with cross under it; rage with L1 + R1; the charge at a run (and its
cross falling back to `X1` at a walk); a grab, strike and forward throw with the meter paying 40 and 100; the tackle on
the 7th sample and the mounted strike; the mugging finished 5 s after it starts with the stick at 0.7 or 0.8 on the
moving target; the stereo theft in 30° steps through 4 stages of 3 turns, failed by an R1 press.

**Disc test** (`[disc][combat]`, counts only): Rembrandt's list has 722 records, 160 with damage; every attack combat
starts has one; the grab and tackle ranges come out at 3.12 m and 3.75 m as at runtime. **The file's damage is not
the runtime damage** (confirmed (Coney's disc check)): no character data on the disc holds `S1` 17, `SS2` 36 or `X1` 26
(Rembrandt's file says 40, 40, 45). The damage the research measured is the list after the character class's 45-entry
damage table (`CfgChar`'s `damage` argument, scaled) has overridden it when the human is made
([Animation](formats/animation.md#anim-range-list), the jump tables `0x0055d640` / `0x0055d6f0`); which entry goes to
which anim id is not researched, so Coney reads the file's value and `AnimRangeList::setDamage()` waits for the table.

**Coney choices**, where the research is silent or inferred:

- Inside one trigger table a later matching entry overwrites an earlier one, as the tables do between themselves;
  trigger 4 (query) never matches.
- Every chain attack takes `S1`'s timing (hit 2, window 6 to 15, end to 17, recovery to 20); the charge, the dive, the
  run, walk and snap attacks and the grab strikes are timed the same way until their own are measured. The attack keeps
  counting under a held R1.
- `SS2`, square is always `SSS3` (19), never 20; a grounded target takes 193, never 194; at a sprint (gait 5) square is
  `S1`; the dive takes the charge's conditions; a buffered snap plays where a square would continue the chain.
- A side is "front" up to and including 45° and "rear" beyond 135°.
- Cross strikes in a grab on its `0x10`, circle throws or spins on its press; the power strike spends nothing; a strike
  or throw with too little power still plays, the meter stopping at 0; a grab plays one move at a time; a throw lets
  go at once. The power strike's ids are read as anim ids (57, 63 in rage, 80 from the rear).
- The grab and tackle search takes the nearest candidate by straight-line distance with no facing cone.
- Rage: the cap splits a hit's points (the part above 25 counts at 0.1); rage drains at the 9.5 a second seen at
  runtime and ends empty.
- The mugging: the 50° tolerance; a random first target; each move between the tolerance plus 20° and 360° less that;
  the period counts game time. The theft: clockwise steps neither add nor take away; the 250 ms pause ignores the
  stick. The mash: the first press counts, a press's gain is truncated, and other commands are ignored.
- The block is read only when the player is free (not grabbing, tackling, mugging or in a theft).

**Integration** (next): the player builds a `CommandMatcher` and a `PlayerCombat`, gives it the camera-turned stick in
the facing frame, its gait, the target found with `nearestTarget()` within `grabSearchRange()`, and game time; plays
`startAnim` through the animator, applies `hitAnim`'s damage to the target through `PendingDamage`, calls `release()`
when a grab or tackle ends, and registers the Combat tunables at start-up.

## Open questions

- **The player being hit**: the player's reactions, knockdowns and getting up, and what a block saves; needs an
  enemy that attacks (a gang fight save state).
- **Lock-on and auto-target**: `0x00241b90` and `CfgAutoLockAndCombat`; how the facing turns to a target between
  attacks.
- **Reaction selection**: how `Strike_Contact`'s kind and direction pick 272-280 and when a hit stuns or knocks down.
- **The grabbed side**: `0x0027fd68`, the struggle when the player is grabbed, and when a victim breaks free (the
  power meter running out is the likely cause, speculative).
- **Rage in effect**: what the raging flag changes (damage, reactions), and the drain's source.
- **The rage points per anim id**: the stats table at `0x00715510`.
- **Commands `0x30`-`0x39`**, `0x22` outside a grab (`0x00288838`), circle + cross (`0x23`), circle + triangle
  (`0x24`), L1 pressed / held (`0x0027da10`, `0x0027dc00`): weapons, picking up and the d-pad (`0x27`) not tested.
- **Theft mode 2** (`0x002878b8`) and which objects use modes 1 and 2.
- **`CfgAttackDelay`** (`0x006b6658`) and the constants marked not traced above.
- **The mugging tolerances**: which of 50° and 60° applies, and the frame of the target angle (world or camera).
- **The fence break** in slot 10: which script reacts to the charge.
- **The class damage table**: which of `CfgChar`'s 45 damage entries overrides which anim id's damage (the jump tables
  `0x0055d640` / `0x0055d6f0`), and which class and difficulty give the street's values (`S1` 17 in play, 40 in
  Rembrandt's file).
- **The power strike's ids**: whether 57, 63 and 80 are anim ids (80 is `GRAB_REAR_SPIN_VICTIM`, with no damage in the
  file) or damage values; whether it spends power.
- **Attack timing** of the attacks other than `S1`, the moving attacks and the grab strikes.
- **The grab moves' commands**: whether cross strikes on `0x10` or `0x12`, circle throws on `0x1e` or `0xd`, and
  whether a strike or throw needs the power it costs.
