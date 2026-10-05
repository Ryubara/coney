# Combat (the player on foot)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 (2026-10-05), reading memory over PINE in a street level ("the street") from save states,
loaded read-only and copied with pad input patched in ([Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)).
The player there is Rembrandt-class "player" human 2 with 314 of 900 health; the test victim is a civilian
(`PoizoCiv`, 600 health) moved in front of the player by writing the transform table. Damage and timings below were
read every update (30 per second) while the scripted pad played. The being-hit, block, grabbed and pose runs
([Being hit, at runtime](#being-hit-runtime), [Grab pose at runtime](#grab-pose-runtime)) drove that civilian as a
**puppet**: command ids written into its per-player record made it attack, grab and block the player
([Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)); the player's health was reset to 900 before each run.

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
pending damage on the target, which plays a reaction chosen by the attack's **hit code** (strength, height, direction)
and the side it came from; strong hits knock down, flagged hits stun, and a held block cancels most hits. Hits fill
the attacker's rage; L1 + R1 with a full meter starts rage, which makes grabs unbreakable and spends no power.

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
| `0x00265dd0` / `0x00418428` | `Rage_NoteHit` / `RepeatTracker_Note` | a landed or blocked hit's kind into the attacker's repeat tracker (halved rage, the throw bonus) | confirmed (code), runtime |
| `0x002542e8` | `AnimRange_Damage` | the Anim Range List's `+0x0a` | confirmed (code), runtime |
| `0x00226448` / `0x00226510` | `Human_SpendPower` / `Human_PowerFraction` | the power meter | confirmed (code), runtime |
| `0x00222ef0` | `Human_HealthPercent` | health over maximum | confirmed (code) |
| `0x002548f0` | `AnimRange_ApplyClassDamage` | writes the character class's damages over the list ([Damage](#damage-table)) | confirmed (code), runtime |
| `0x00265f70` | `Human_ApplyPendingDamage` | each update: block, damage, armour, then the reaction by state | confirmed (code), runtime |
| `0x002617f8` | `Block_DuckCounter` | the counter from a duck, 617-620 by the target's side | confirmed (code) |
| `0x0026b0a0` / `0x00266d00` | `Hit_PickReaction` | the reaction id from the hit code, the height and the side | confirmed (code), runtime |
| `0x0026a6d0` | `Human_PlayReaction` | plays it; stun and knockdown | confirmed (code), runtime |
| `0x00269f30` | `Human_BlockHit` | a hit on a held block | confirmed (code) |
| `0x0022f658` / `0x0022f8d8` | `Human_Stun` / `Human_EndStun` | the stun timer | confirmed (code), runtime |
| `0x0022f100` | `Human_KnockDown` | grounded and its timer | confirmed (code), runtime |
| `0x00256a60` | `Human_UpdateDown` | the stun and ground timers, getting up | confirmed (code), runtime |
| `0x002562d0` | `Human_UpdateMeters` | power drain and refill, rage drain and decay, a grab's power running out | confirmed (code), runtime |
| `0x00262ac8` | `Attack_StartPaired` | a grab move on the victim: power check and spend, damage | confirmed (code), runtime |
| `0x0027fd68` | `Player_UpdateGrabbed` | the struggle in another human's grab | confirmed (code) |
| `0x00241b90` | `Human_FightStanceMove` | movement in a fight stance; lock-on | confirmed (code), runtime |
| `0x00287730` | `Player_Special` | cross + square, circle + cross, circle + triangle outside a grab | confirmed (code), runtime |
| `0x002878b8` | `Player_TheftTiming` | theft mode 2 | confirmed (code) |
| `0x0026c548` | `Grab_Start` | the grab's intro: 71 (or 70), then 69 and the idle; turns towards the target | confirmed (code) |
| `0x0026c1d8` | `Grab_IntroEnd` | end of the intro clip: grab from the front or rear, or a counter | confirmed (code) |
| `0x0026be68` | `Grab_Connect` | aligns the pair and starts 72 / 73 (74 / 75 from the rear), then the holds | confirmed (code) |
| `0x00276998` | `Pair_AlignStart` | range gate, then turns and slides the attacker so the victim stands at the clip's reach | confirmed (code) |
| `0x0026bad8` | `Grab_ConnectEnd` | end of 72 / 74: distance check, snap and attach in the hold | confirmed (code) |
| `0x00276d98` / `0x002802a0` | `Pair_SnapAttach` / `Pair_Attach` | puts the victim at the hold's offset and ties its movement to the grabber | confirmed (code) |
| `0x00244e78` / `0x00245310` | `Human_MoveAttached` / `Human_MoveGrabbing` | movement states of the held victim and of a grabbing player | confirmed (code) |
| `0x00277958` | `Pair_CheckPlace` | a move in the hold needs the victim within 0.3 m of the move's offset | confirmed (code) |
| `0x0023cf88` / `0x0023d2b8` | `Human_TurnToOver` / `Human_MoveToOver` | turn to a heading, or move to a point, over a time | confirmed (code) |

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
| record `+0x11a` | s16 | whether the hit may cause a reaction: 0 when the attacker's object at human `+0x44` has `+0x31` = 6 (not traced), else 1; an earlier reading took it for the hit's kind | confirmed (code) at `0x0021b290` |
| record `+0x100` | u32 | the time a **stun** ends (ms) | confirmed (code), runtime |
| record `+0x104` | u32 | the time a knocked-down human **gets up** (ms) | confirmed (code), runtime |
| record `+0xdc` | float | in a lock, the stick's angle from the facing (picks the combat-walk clips 380-387) | confirmed (code) at `0x00241b90` |
| record `+0xf4` | u32 | the L1 double-tap deadline (L1 released + 264 ms) | confirmed (code) at `0x00227b98` |
| human `+0xc4` | u32 | the handle of the other human in a grab (the grabber, for a grabbed human) | confirmed (code), runtime |
| record `+0x12c` | u32 | mugging progress, ms | confirmed (code), runtime |
| record `+0x144` / `+0x146` | s16 | **health** / its maximum | confirmed (code), runtime (player 314/900, civilian 600/600) |
| record `+0x148` | s16 | the **power meter** ([Power meter](#power-meter)) | confirmed (code), runtime |
| human `+0xc8` | u32 | the current target's handle | confirmed (runtime) |
| human `+0x370` | int | money carried (a mugging takes it: 18 → 0) | confirmed (runtime) |
| human `+0x5a8` | float | the mugging's target stick angle | confirmed (code) |
| human `+0x648` | u32 | the rage hold timer: now + 5000 ms at each gain; the meter decays after it | confirmed (code), runtime |
| human `+0x650` | int | **rage** | confirmed (code), runtime |
| human `+0xe0` | u64 | more flags ([below](#human-flags)) | confirmed (code) |
| human `+0x1b0` | s8 | the player number, -1 for a human no player controls | confirmed (code), runtime (0 for the player) |

#### Human flags {#human-flags}

The flags at human `+0xe0` that combat reads, confirmed (code) at the cited functions; the player in the street had
`0x20002440407` (confirmed (runtime)):

| Bit | As the victim | As the attacker |
| --- | --- | --- |
| `0x20` | | the hit takes all the victim's health (`0x0021b290`) |
| `0x80` | reaction strength capped at 1 (`0x00266d00`) | |
| `0x100` | never stunned (`0x0026a6d0`) | |
| `0x200` | reaction strength -1 | |
| `0x400` | combo hits keep their full strength; strength +1 from an attacker with `0x200000` | |
| `0x4000` | escapes when the grabber's power runs out (`0x002562d0`) | **doubles** the damage dealt; ignores hit armour |
| `0x20000` | as `0x4000` for the power-out escape | |
| `0x80000` | | **raging** ([Rage](#rage)) |
| `0x200000` | | reaction strength +1 (see `0x400`); ignores hit armour |
| `0x4000000` | spends no power (`0x00226448`) | |
| `0x400000000` | a block holds even against strength 3 (`0x00269f30`) | |
| `0x2000000000` | never escapes when the grabber's power runs out | |
| `0x20000000000` | one hit cannot take health below 25 % (`0x00265f70`); it then sets `0x10` | |
| `0x100000000000` | never picked as a target (`HuSetNoAutoLock`, `0x00279410`) | |

### The Anim Range List and damage {#damage-table}

Every attack's base **damage is the Anim Range List's `+0x0a`** as the human holds it
([Animation](formats/animation.md#anim-range-list)); `Strike_Contact` (`0x0021b290`) reads it with
`AnimRange_Damage(attacker, anim id)` (`0x002542e8`). The value on the disc is **not** what plays: when the human is
made (`Human_AttachInstance`, `0x00217a98`), `0x002548f0` writes the character class's damages (`CfgChar`'s 16-bit
values at class record `+0xb8`, [Character classes](characters.md#classes)) over the list, through the jump table at
`0x0055d6f0`. For a player (human `+0x1b0` not -1) each value is first scaled by its **Warrior class byte `+0x06`**
(115 % for class 6): `damage = int(value × byte × 0.01 + 0.5)` in single-precision floats, so 30 gives 34, not 35.
A value of 0 leaves the list's own. Confirmed (code); confirmed (runtime): Rembrandt (`CfgChar` type 30) has 15 for
`S1`, which plays as 17, and the street civilian (type 417, no player) keeps its values unscaled.

| Index | Anim ids | Index | Anim ids | Index | Anim ids |
| --- | --- | --- | --- | --- | --- |
| 0 | 11 `X1` | 11 | 21 | 25 | throws 147, 151, 149, 153 |
| 1 | 12 `S1` | 12 / 13 | 193 / 194 grounded strikes | 26 / 27 / 28 | 57 / 59 / 61 power strikes |
| 2 | 13 `XX2` | 16 | 653, 655 special | 29 | wall throws 155, 159, 157, 161 |
| 3 | 15 `SX2` | 17 | 657, 659 special | 31 | 96, 98, 108, 110 grab struggle |
| 4 | 14 `XS2` | 19 | 0 charge | 35 | 219, 221, 223 |
| 5 | 16 `SS2` | 20 | 1 dive | 37 / 38 / 39 | 225 / 227 / 229 |
| 6 | 17 `SSX3` | 24 | 51, 53, 55 grab strikes | 42 / 43 | 250 / 246 |
| 7, 8, 9 | 19 `SSS3`, 18, 20 | 10 | snaps 25-30 | | |

Indices 14, 15, 18, 21-23, 30, 32-34, 36, 40 and 41 write nothing, and index 44 is never written. A script call can
rescale the set later (`0x00236038` → `0x00229b90`, not traced). The far ranges come from the class's 45 floats the
same way (`0x002545e0`, table `0x0055d640`). Every value below was seen as the exact health lost by a 600-health
civilian (confirmed (runtime)):

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

### Hit codes and the reaction table {#hit-codes}

The Anim Range List's `+0x0c` is the attack's **hit code**: bits 0-1 the **direction** (2 straight, 1 and 3 from a
side, 0 from behind), bits 2-3 the **height** (0 low, 1 mid, 2 high), bits 4-5 the **strength** (0 light, 1 medium,
2 heavy, 3 crushing). Its `+0x0e` flag `0x400` makes the hit **stun**. Confirmed (code) at `0x00266d00` and
`0x0026a6d0`; the player's values, read at runtime:

| Ids | Code | Flags | Ids | Code | Flags |
| --- | --- | --- | --- | --- | --- |
| 11 `X1` | `0x09` | `0x800` | 21 | `0x26` | `0x400` |
| 12 `S1` | `0x0a` | `0x800` | 22, 24 | `0x2a` | `0xc00`, `0x400` |
| 13 `XX2` | `0x1b` | | 23 | `0x1a` | `0x400` |
| 14 `XS2` | `0x2b` | | 25, 29 snaps | `0x1b` | `0x400` |
| 15 `SX2` | `0x1a` | `0xc00` | 27 snap | `0x19` | |
| 16 `SS2` | `0x0b` | | 0 charge / 1 dive | `0x36` / `0x3a` | |
| 17 `SSX3` | `0x25` | | 193, 194 | `0x0a` | |
| 19 `SSS3` | `0x26` | `0xc00` | 212 | `0x06` | |
| 20 | `0x26` | `0x800` | 653 / 655 special | `0x26` | |
| 100, 112 escapes | `0x2a` | `0x100` | 645 / 647 rage special | `0x23` | |
| 104 / 116 | `0x26` / `0x3a` | `0x400` | 147-153 throws | `0x2a` | `0x100` |

The grab strikes 51-57 have code 0 (the grab's own reactions play instead).

**The reaction** (`0x0026b0a0` → `0x00266f38` → `0x00266d00`), confirmed (code):

1. **Strength** = the code's, then -1 if the victim has flag `0x200`, or the attack is a combo id and the victim
   lacks flag `0x400` and is not hurt (`0x00266b50`); +1 if the attacker has `0x200000` and the victim `0x400` or the
   attack is a combo id (`0x00266c40`); 0 between allies when the victim's `+0x08` has `0x1b`; at most 1 if the victim
   has `0x80`; at most 3. The combo ids are 13, 14, 15 and 17-20: both tests skip 16 `SS2`.
2. **Height** = the code's, +1 when the attacker stands 0.3-0.9 m higher, +2 when 0.9-1.5 m; -1 / -2 when lower; at
   most 2. A low hit (height 0) is always strength 2.
3. **Side**: 0 when the attacker is within 45° of the victim's front, 3 on its left, 1 on its right, 2 behind
   (`0x00267338` via `0x002672d0`). The reaction's direction = (code direction + side) & 3.
4. The id is the s32 table at `0x00510798`, index strength × 16 + height × 4 + direction; a -1 entry falls back to
   272. Dying uses `0x00266fd8`: strength 2, and half the time the `DIE` set 304-315 (id + 20), else 292.

| Strength | Height | Back (0) | Left (1) | Front (2) | Right (3) |
| --- | --- | --- | --- | --- | --- |
| 0 light | 1 mid | 270 | 271 | 268 | 269 |
| 0 light | 2 high | 274 | 275 | 272 | 273 |
| 1, 2, 3 | 0 low | 286 | 287 | 284 | 285 |
| 1 medium | 1 mid | 278 | 279 | 276 | 277 |
| 1 medium | 2 high | 282 | 283 | 280 | 281 |
| 2 heavy | 1 mid | 290 | 291 | 288 | 289 |
| 2 heavy | 2 high | 294 | 295 | 292 | 293 |
| 3 crushing | 1 mid | 298 | 299 | 296 | 297 |
| 3 crushing | 2 high | 302 | 303 | 300 | 301 |

Every reaction seen fits, on the civilian and, from a puppet, on the player ([Being hit, at
runtime](#being-hit-runtime)) (confirmed (runtime)): `S1` (`0x0a`) from in front gives 272 and from the victim's left
(61-71°) 275; `X1` (`0x09`) from its left gives 274; `SSS3` (`0x26`, a combo id, so strength 1) gives 276; the special
653 (`0x26`, strength 2) from the left gives 291 and a knockdown.

### Constants {#constants}

Read at runtime in the street; the setters are on the [config bindings](../references/bindings/config.md).

| Address | Value | Use |
| --- | --- | --- |
| `0x0050b708` | 7 | samples a history hold needs (circle → tackle) |
| `0x0051024c` | 0.25 | the health floor of victim flag `0x20000000000` |
| `0x00510274` | 0.6 | the power fraction a grabber loses when a third human hits it |
| `0x00510278` | 0.25 | `CfgPowerEndurance`: the power fraction a power strike needs and a throw costs |
| `0x00510290` / `0x00510294` | 5000 / 20000 | `CfgRageHandlers` (ms) |
| `0x005102b4` | 1 | `CfgSnap`: snap attacks on |
| `0x005101e4`-`0x005101f0` | 0, 15, 15, 2 | `CfgBurnRates`: power drained per second blocking, grabbing, tackling, and in a further grab state |
| `0x005102b8` / `0x005102bc` / `0x005102c0` | 1000 / 15 / 250 | `CfgButtonMash`: target, decay per update, press gain (used halved); only theft mode 1 reads them |
| `0x005104ac` / `0x005104b0` / `0x005104b4` / `0x005104b8` | 0 / 0 / 0 / 1 | `CfgLockOn`, auto-combat, `CfgAutoLock`, `CfgAutoLockAndCombat` |
| `0x005104c0` | 6.25 | a target farther than 2.5 m (squared) is dropped |
| `0x005104bc` | 4, 6.25, 9 | `CfgDistances`, squared (2, 2.5, 3 m) |
| `0x005108d8` / `0x005108e0` / `0x005108e4` | 25 / 1.0 / 0.1 | `CfgRagePoints`: the cap, and the factors below and above it |
| `0x00510798` / `0x00510898` | 64 s32 each | the reaction and block reaction tables ([Hit codes](#hit-codes), [Block](#block)) |
| `0x0051096c` / `0x00510970` | 0.9425, 2 | target search: the half-angle (54°) and the height difference limit (m) |
| `0x00510980` | 6, 4, 3, 3, -1 | not traced (a short table read by the grab code) |
| `0x00510988` | 3 | the command that spins a grab or reverses one (R1 pressed) |
| `0x00510998` | 0.2 | the power cost of a grab strike, halved for a player |
| `0x00514878` | 4, 1.21, 4, 4, 3.0625, 2.25 | `CfgActionDistance`, squared |

The player's **power class** record (64, `0x006619a0 + 64 × 0x44`, [Power classes](characters.md#power-classes)):
`+0x28` = **400, the power meter's maximum**; `+0x2a` = 60, its refill per second; `+0x04` = 0.3, the **hurt**
threshold (below 30 % health); `+0x30` = 200 ms a stun; `+0x34` = 2750 ms on the ground; byte `+0x36` = 3, the grab
struggle divisor. The street civilian (class 2): hurt below 35 %, stun 750 ms, ground 2000 ms, power 200, divisor 4.
The **Warrior class** record (`CfgWarriorClass`, `0x006b65c0 + c × 14`; class 6 for the player here, human `+0x1ba`)
was `4e 00 90 c8 f0 32 73 03 01 01 01 02 03 02`: s16 `+0x00` = 78 is the **rage maximum**, byte `+0x02` = 144 the
rage gain percentage, `+0x03` = 200 the idle rage decay and `+0x04` = 240 the rage drain while raging (percent of
the maximum per 20 s), `+0x06` = 115 the player's damage scale, byte `+0x08` picks the mash gain factor and byte
`+0x0b` the stereo theft's turns, byte `+0x0c` the mugging parameters. Confirmed (runtime) for the bytes; the uses
confirmed (code) at the functions below.

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
   grabbed (`0x30`) → `0x0027fd68` ([Grabbed](#grabbed)); throwing (`0x1000`) → `0x0027df38`; `0x2000` →
   `0x0027e018`; `0x200000` → `0x0027e040`.
4. The commands: `0x36` → `0x0027de78`; `0x37`, `0x38` → `0x00287fe0`; triangle `0xb` with human `+0x5b8` →
   `0x002811f0`; `0x33` → `0x00281188`; `0x32` → `0x002832c8`; `0x22` or `0x39` → `0x00288838`; `0x22`, `0x30`,
   `0x23`, `0x24` → `0x00287730`; **`0x20` → charge**; **`0x21` → dive**; **`0xf` → square**; **`0x10` → cross**
   (weapon types 4, 5, 6 → `0x002880d8` instead); **`0xd`, `0xe` → grab or tackle**; `0x27` → `0x00286ba8`; 7 (L1
   pressed) → `0x0027da10`; 6 (L1 held) → `0x0027dc00`; 8 → `0x00227b98` / `0x00227a90` ([Targets](#targets)).
   Commands `0x30`-`0x39` are not made by the street's tables; scripts or weapons make them (inferred). `0x00288838`
   acts only for armed weapon types; `0x30` matches no branch of `0x00287730` (inferred: unused there).

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
| 16 `SS2` | 19 `SSS3` or 20 at random (19 in all but one of about 15 runs) | 17 `SSX3` |
| anything else | the chain ends | the chain ends |

A fourth press after `SSS3`, or a third cross after `XX2`, does nothing. A square buffered with the stick above 0.95
to a side or back plays the snap attack instead.

**Timing of S1** (confirmed (runtime), updates of 1/30 s from the press): the hit lands **2 updates** after the press;
`0x2` (the chain window) opens at about **6** and closes at about **15**; then `0x4` and recovery `0x40000`; the
fight idle 358 returns at about 20 (0.66 s). A press at update 5 was buffered and played `SS2` when the window
opened; a press in recovery (update 19) was dropped; a press at update 24 started a new `S1`. Pressing the next button
anywhere from the hit to update 15 continues the combo.

**Timing of the others** (confirmed (runtime), updates of 1/30 s from the clip's start; a cross attack starts on the
release, command `0x10`; a buffered press plays when the window opens):

| Id | Hit | Window `0x2` opens | End `0x4` | Idle again |
| --- | --- | --- | --- | --- |
| 12 `S1` | 2 | 6 | 15 | 20 |
| 11 `X1` | 8 | 10 | 20 | 30 |
| 16 `SS2` | 4 | 6 | | |
| 19 `SSS3` | 7 | | 16 | 26 |
| 17 `SSX3` | 7 | | 24 | |
| 13 `XX2` | 10 | | 17 | 30 |
| 15 `SX2` | 7 | | 14 | 19 |
| 14 `XS2` | 9 | | 22 | 30 |
| 51, 53 / 55 grab strikes | 1 | | | 21 / 23 (back to the hold 82) |
| 57 power strike | 0 (damage on the start) | 19 | 33 | 44 |
| 23 from a walk | | | | 24 |

The third hits end the chain, so their window was not needed.

### Moving attacks: run, charge and dive {#run-attacks}

At a run (gait 4, 7.8 m/s, stick 1.0) **square** plays 24 for about 0.7 s and the run resumes. At a walk it plays 23.
Confirmed (runtime).

**The charge**: **L2 held, then cross pressed** (command `0x20`) at gait 4 with record `+0x08` clear, or at gait 5
(a sprint), plays 0 (`gen_charge_shoulder`) for about **0.9 s** at about 7.45 m/s. **The dive**: L2 held, then
square pressed (`0x21`), plays 1 for about **2 s**. Confirmed (code) at `0x0027d800` / `0x0027d900`, confirmed
(runtime). At a walk or standing the combination does nothing (the cross or square press under it is overwritten,
inferred from the matching order).

**Square at a sprint** has no attack of its own: the moving attacks test the gait exactly (4 → 24, 1-3 → 23), so at
gait 5 square falls through to the standing path (a fight stance and `S1`). Confirmed (code) at `0x00286cc8`; not
tried at runtime. With `0x00510270` set (0 here) square outside a fight stance plays 22 instead.

**Specials** (`Player_Special`, `0x00287730`), confirmed (code): **cross + square** (`0x22`) outside a grab plays the
special 653 (645 raging); **circle + cross** (`0x23`) 657 (649 raging), a paired grab-and-strike through `0x0026c548`
or a tackle through `0x002707a8`; **circle + triangle** (`0x24`) sprays a tag (664) where the player may tag
(`0x00222818`). The id is 645 or 653 + 4 × variant + 0 / 2 by side (`0x00263c90`). A special that is not paired
needs and spends 0.25 of the power meter. At runtime (confirmed (runtime)): cross held, square pressed 40 ms later
played 653, power 400 → 300, the civilian lost 6 and fell (291, grounded 196, up with 199 2.0 s after the hit);
circle + cross at a victim getting up played 359 and then `X1`.

**Slot 10 (a fence)**: sprinting at a breakable fence (L2 held, stick 1.0 straight at it) and pressing cross charges;
the charge stopped at the fence, and about 1.4 s later a cutscene placed the player on the far side. Confirmed
(runtime). That the break is a script trigger reacting to the charge is inferred.

### Block (and no dodge) {#block}

**R1 held** in a fight stance blocks: anim state 21 then 606 `BLOCK_SUSTAIN` (605 `BLOCK_START` was not seen as the
playing id), state flags `0x8001`. The stick at 0.8 to a side while blocking gives anim state 24, 607
`BLOCK_SHUFFLE`, turning in place. Cross pressed while blocking plays `X1` and returns to the block. Confirmed
(runtime). No separate dodge command exists in the street's tables (confirmed (runtime)); the shuffle is the only
evasive move found.

**A hit on a block** (`0x00269f30`, from `Human_ApplyPendingDamage` while state `0x8000`), confirmed (code): a block
that holds **cancels all the damage** and plays a block reaction from the table at `0x00510898` by height and
direction (mid: 614, 615, 612, 613; high: 610, 611, 608, 609 for back, left, front, right; low counts as mid). Every
direction can be blocked. The block **breaks** (the full hit and its reaction land, and the block ends) when the
hit's modified strength is 3 (unless the victim has flag `0x400000000`), the attacker plays 26-34, or the attacker
holds a weapon (item `+0x87`) and the victim none (unless its `+0x08` has `0x1000`); armed against armed it breaks
unless the victim's weapon type is 3 or it is in a block clip (605-620). Attack 36 against a front block with weapon
type 3 throws the attacker into 629. A blocked hit gives the attacker half the rage. Blocking drains no power
(`0x005101e4` = 0 at runtime).

**The duck and the early block** are announced by the **attacker's clip**, not the hit. Confirmed (code) at
`0x00101dd8`, `0x00245920` and `0x00254e78`:

- An anim event of type **`0x24`** in the attacker's clip (`0x00101dd8`): if the attacker has a target
  (`0x00226e60`) within twice the reach of its current anim (Anim Range List `+0x04`, `0x002544a0`), it sends the
  target message **`0xa4`** (`0x003a2e00`).
- The target's message handler (`0x00245920`), case `0xa4`: when `0x00225498` allows it (the target is not in one of
  its excluded states) and the target is **blocking** (`0x00223ad0`: state `0x8000`, or record `+0x08` `0x1000`), it
  takes the attacker as its target (`0x00226760`) and sets record `+0x14` = **`0xd`** (`0x002266a8`).
- On its next update `0x00254e78` sees `0xd` with a target, sets state `0x8000` and calls `0x00261578`, which plays
  **616 `BLOCK_DODGE`** (628 with weapon type 3 against weapon type 3) and sets record `+0x08` `0x1000`. The attack
  then passes over the ducking body.
- Event type **`0x26`** sends message `0xa6`, which sets `+0x14` = `0xc` and calls `0x0026ae20`: an **early block
  reaction** from the [table](#hit-codes) at `0x002671a8`, played before the hit lands.
- Event type **`0x25`** sends message **`0xa5`** to the clip's own human (`0x00101dd8`). It sits on frames 6-13 of
  616 `gen_duck` (0.20-0.43 s into the 0.7 s duck, the same for Rembrandt and the civilian; read from the disc), so
  it opens the duck's **counter window**. The handler (`0x00245920`, case `0xa5`), when `0x00225498` allows it and
  the human is not both in state `0x100000` and flagged `0x8000000` (human `+0xe0`), asks whether to counter: a
  player when its current command (per-player `+0x20`, `0x00147ef8`) is one of `0xf`, `0x11`, `0x15` (square) or
  `0x10`, `0x12`, `0x16` (cross) (`0x0027b988`), so square or cross pressed or held during the window; an AI human
  asks its current tactic (`0x0028c6a8`, tactic types `0x17`, `0x1b` and `0x85`). Yes sets record `+0x14` =
  **`0xe`**.
- On its next update `0x00254e78` handles `0xe` (`0x002550f4`): nothing while the human holds an object of class 8
  (`0x00231a38`); `+0x14` cleared and nothing else when `0x00225498` refuses; otherwise it keeps its target
  (`0x002267a0`) if that is within 1.25 × the reach of anim 617 (`0x00254508`), or looks for one in that range
  (`0x0027ac30`). With a target it clears `+0x14`, cuts a block or reaction clip short (`0x00228488`, then
  `0x0022f8d8`, unless human `+0xe0` has `0x8000000`), and plays the **duck counter** (`0x002617f8`): 617-620
  `BLOCK_COUNTER_FRONT`, `_RIGHT`, `_BACK`, `_LEFT` by where the target stands (`0x002672d0`), steered onto the
  target (`0x002761c8`, 0.1 s) when it is within the clip's reach, with record `+0x18` = 11, state `0x8000` (block)
  cleared and record `+0x08` `0x2000` set. The counters' hits are their clips' events `0xf` / `0x10` at frames 2 and
  8 (on the disc). Without a target `+0x14` stays `0xe` (inferred: the branch skips the clear), so a later event of
  the window can still find one.

So a block that ducks an attack (616) can be turned into a counter-attack by pressing square or cross during the
duck's frames 6-13. Confirmed (code) at the cited addresses; not yet seen at runtime.

The events on the disc (the clips' event lists, frame in brackets) match the runtime ducks exactly (confirmed
(runtime), [Being hit](#being-hit-runtime)): Rembrandt (`warr_re_cv`) has `0x24` on 11 `X1` (3), 13 `XX2` (5) and
14 `XS2` (4), and `0x26` on 12, 15, 16, 17, 19, 20, 653 and 655; the civilian (`civl_co_ma1`) has `0x24` on 17 `SSX3`
(3) and 653 / 655 (14), and `0x26` on 11-16, 19 and 24. So which attacks a block ducks is a property of the
attacker's model, not of the attack.

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
| hold cross, press square (`0x22`) | the **power strike**: anim id 57 (anim 63 in rage, which also deals anim 231's damage at once); from the rear, anim 80 first spins the victim to the front; confirmed (runtime) for 57 |
| circle + cross (`0x23`) | anim 63, with the same rear spin |
| triangle (`0xa`) | **mug** the victim if it qualifies (`0x00225ff0`); confirmed (runtime) |
| circle (`0x1e`, `0xd` or `0xe`) with the stick above 0.25 | **throw** ([Throws](#throws)); confirmed (runtime) |
| circle without the stick | from the front `0x0026f008` (not traced); from the rear the spin to the front |
| R1 pressed (3) or `0x19` | **spin**: front → rear 78 / 79 (`0x0026d570`), rear → front 80 / 81 (`0x0026d998`); confirmed (runtime): 78 / 79, then the rear hold 84 / 85, states `0x85` / `0x20` |
| L2 held (5) | **lets go**: player 95, victim 94; confirmed (runtime) |

The commands are those of the [table](#commands): square acts on its press (`0xf`), cross on its release (`0x10`),
circle on its press (`0x1e`). A grab strike only **spends** power, at any meter level: 0.2 of the maximum, halved for
a human with a player number (`+0x1b0`), so **40 of 400**. The power strike, `0x23` and the throws **need** more than
0.25 of the maximum and **spend** 0.25 (**100 of 400**); with 0.25 or less the grab is released instead
(`0x0026c7e0`). Confirmed (code) at `0x0027f3b0` and `0x00262ac8`; confirmed (runtime): strikes 40 each, the power
strike 236 → 135 with its 57 damage on the same update. The grab also ends when the victim drifts beyond the larger of
reach + 0.2 m and reach × 1.2, or 0.2 m up or down.

### Posing a grab {#grab-posing}

How the two bodies of a grab are placed and kept together. The reference frame is always the **grabber's**: before
the connecting clips the grabber is turned and slid to the victim, after them the victim is snapped to the hold's
offset and then follows the grabber. The clips themselves are drawn like any other clip, each body in its own frame,
with nothing stripped from or composed into their root ([Paired tasks](formats/animation.md#paired-tasks)). Confirmed
(code) at the cited addresses unless marked; the clip and range values are Rembrandt's set (the generic set has the
same for these ids), read from the disc as numbers only.

**Axes.** A heading `h` faces `(-sin h, cos h)`: 0 faces `+y`, and it grows to the left (the heading of a direction
`(dx, dy)` is `atan2(-dx, dy)`, `0x003357a8`). An offset "in the grabber's frame" is `(x right, y ahead)` turned by the
grabber's heading.

1. **Intro** (`Grab_Start`, `0x0026c548`): the grabber's stack becomes 71 `GRAB_PLAYER_INTRO` (70 for some AI
   grabbers), then 69 `GRAB_MISS`, then its idle; the base id `0x48` (72) is kept at record `+0xb4`. With a target, the
   grabber turns to face it over the intro's playing time (`0x00221cd8`: 0.133 s / 0.75 = 0.18 s). 71's end callback
   (`Grab_IntroEnd`, `0x0026c1d8`) decides: no grab (69 plays on), a counter by the victim (76, through
   `Attack_StartPaired` with state flag `0x400000`, when the victim has flag `0x20000` and in some AI cases), or a grab,
   **from the rear** when `0x002672d0` puts the grabber on the victim's side 2, otherwise from the front.
2. **Alignment** (`Pair_AlignStart`, `0x00276998`, called first by `Grab_Connect`, `0x0026be68`, with 72 from the
   front or 74 from the rear):
   - `d` = the 3D distance between the two. The grab fails (69 plays on) when `d` is beyond the clip's **far range**
     (72 and 74: 2.5 m), × 1.25 when the grabber's controller record (`0x0021d408`) has kind 0 (inferred: a player),
     so 3.125 m, or × 1.5 for kind 1 and anim id 7.
   - `h` = the heading from the grabber to the victim. Over a time `T` the **grabber turns to `h`**, and when `d`
     differs from the clip's **reach** `r` (72: 0.999 m, 74: 1.018 m) it **slides** to `victim − r·(−sin h, cos h, 0)`,
     so that the victim stands `r` straight ahead at the victim's height. The **victim does not move**: its own
     movement is stopped (virtual `+0x14c`) and it turns over the same `T` to `h + π` (front: facing the grabber) or to
     `h` (rear: facing away).
   - `T` = 0.1 × the time of the clip's first contact event (types 9, `0xf`, `0x13`, `0x2c`, `0x34`, `0x36`, `0x41`;
     its frame / 30) or, with none, the clip's duration, divided by the clip's rate (`0x00101658`, `0x00101a00`). 72
     has none: `T` = 0.1 × 0.467 / 0.75 = 0.062 s, two updates.
   - The turn (`0x0023cf88`) stores a turn rate (angle / `T`) for `T` seconds and is skipped below 0.01 rad; the slide
     (`0x0023d2b8`) stores a velocity (offset / `T`) for `T` seconds, snaps under 0.01 m and is dropped at 13 m or
     more, or above 50 m/s. Which update applies them is not traced (fields `+0x2e0`-`+0x332` of the human).
   - When the grabber's id has a script-loaded clip (`0x002219b8`), the reach is instead the horizontal length of the
     clip's [type 8 event](formats/animation.md#paired-tasks) and the gate is the largest of 1.875 m, reach + 0.25 and
     reach × 1.25 (not seen in play).
3. **Connect** (`Grab_Connect`): `0x0022c730` links the two (each other's handle at `+0xc4`, flags `0x40` / `0x10`
   front or `0x80` / `0x20` rear), then **both stacks switch on the same update with no fade**: the grabber plays 72
   (74) as a type 3 task holding state flag `0x200`, then its four-clip hold (type 11 from its record `+0x28`: 82,
   flags `0xc1`); the victim plays **73 (75) from the grabber's set** as a type 6 task holding state flag `0x10000`,
   then its own hold (83, flags `0xc1`). The connecting clips have no task flags, so their root motion moves both
   bodies; the holds have none (`0xc1` includes `0x1`, no root velocity).
4. **The connecting clips carry the pair to the hold** (inferred from the clip data): 73's displacement is
   (−0.380, −0.028) in the victim's frame, which faces the grabber, so the victim drifts 0.38 m to the grabber's right
   while 72 moves the grabber 0.024 m ahead; from the rear 75 (−0.098, −0.617) and 74 (0, 0.181) close the gap from
   1.018 m to 0.22 m. Both end where the hold's offset below says.
5. **Snap and attach** at the end of 72 (74) (`Grab_ConnectEnd`, `0x0026bad8`): the grab is released (`0x0026c7e0`)
   if the victim is more than the larger of reach(82) + 0.2 and reach(82) × 1.2 away (1.297 m), or 0.2 m up or down
   (`0x00229a10`); otherwise `Pair_SnapAttach` (`0x00276d98`) puts the victim at **grabber position + the grabber's
   rotation applied to (direction × reach of the hold id, 0)** (82 from the front, 84 from the rear), with heading
   grabber + π (front) or the grabber's (rear). `Pair_Attach` (`0x002802a0`) then stores that place as an offset and a
   relative rotation in the grabber's frame (victim `+0xa0`, `+0xb0`; the grabber's handle at `+0xc0`) and switches
   the victim's movement to `Human_MoveAttached` (`0x00244e78`), and a grabber with a player's controls
   (`0x0021d3e8 +0x1b`) to `Human_MoveGrabbing` (`0x00245310`).
6. **In the hold** the victim's movement each update is the grabber's transform × the stored offset, swept against
   the world up to three times (`0x0033e278`); its own root motion does not move it (inferred: the state computes the
   place from the grabber). The grabbing player's movement state turns the grabber from the stick, so the pair turns
   together (inferred; how the stick drives it is not traced).
7. **Moves in the hold** (`Attack_StartPaired`, `0x00262ac8`): a strike, power strike or throw is refused unless the
   victim stands within **0.3 m** of grabber position + rotation × (direction × reach of the move's id)
   (`Pair_CheckPlace`, `0x00277958`; 51: (0.348, 0.937) × 1.082 m, the hold's point). The attacker plays the id as a
   type 3 task, the victim **id + 1 from the attacker's set** as a type 6 task (fade 0); neither snaps. The **spins**
   (78 / 79, `0x0026d570`, and 80 / 81) end the same way as the connect: their end callback (`0x0026d510` for 78) snaps
   and attaches at the rear hold 84 (80 / 81 at the front hold 82, inferred). `0x0026d078`, `0x0026d8c8`, `0x0026eba0`
   and `0x00272918` also end in `Pair_SnapAttach` (not traced further).

**The offsets** (from the Anim Range List record of the grabber's id, [direction and reach](formats/animation.md#anim-range-list);
each matches the clip's type 8 event):

| Moment | Id (grabber) | Direction × reach | Victim in the grabber's frame | Victim's heading |
| --- | --- | --- | --- | --- |
| alignment, front | 72 | (0, 1) × 0.999 m | (0, 0.999) | grabber + 180° |
| alignment, rear | 74 | (0, 1) × 1.018 m | (0, 1.018) | grabber's |
| front hold, strikes 51 / 53, spin 78 | 82 (51, 78 the same) | (0.351, 0.936) × 1.081 m | (0.380, 1.012): 1.08 m, 20.6° to the right | grabber + 180° |
| rear hold | 84 | (−0.399, 0.916) × 0.242 m | (−0.097, 0.222): 0.24 m, 23.5° to the left | grabber's |
| mounted (tackle) | 210, 213 | (−0.965, 0.259) × 0.124 m | (−0.120, 0.032) | not traced |

So Coney's guess of 0.8 m straight ahead is replaced by the table: 1.0 m straight ahead when the connecting clips
start, 1.08 m at 20.6° right in the front hold, 0.24 m in the rear hold. For the mount the record puts the victim's
point 0.12 m from the attacker, who sits on it; how the tackle places the pair (`0x00270270` calls the same
alignment; the mount uses a [type 4](formats/animation.md#animation-tasks) task, `0x00270b38`, `0x00271ef0`) is not
traced, so the mount's offset is inferred from the record only.

**The pose** of every one of these clips is ordinary; what they share is that most leave out the bone 2 channel, which
the original fills from its fixed reference pose ([Animation](formats/animation.md#reference-pose)).

### Grab pose at runtime {#grab-pose-runtime}

Measured every update in two runs: the player grabbing the puppet civilian (71, 72 / 73, hold 82 / 83, strike 51 /
52, cross strike 55 / 56, R1 spin 78 / 79 to the rear hold 84 / 85, spin back 80 / 81, L2 let-go 95 / 94), and the
civilian grabbing the player (70, 72 / 73, 82 / 83, strike 51 / 52). Confirmed (runtime) unless marked.

**Where the bodies are.** "Ahead" and "right" are in the grabber's frame (a heading `h` faces `(-sin h, cos h)`):

| Moment | Victim ahead | Victim right | Victim's heading |
| --- | --- | --- | --- |
| intro 71 / 70, placed 1.0 m away | 0.95 → 0.71 | 0 | grabber + 180° |
| connect 72 / 73 starts | 0.84-0.94 | 0.01 | grabber + 180° |
| connect ends, front hold 82 / 83 | **1.012** (1.012-1.076 in the AI's hold) | **0.379** | grabber + 180° |
| strikes 51 / 52, 55 / 56 | 1.012 | 0.379 | unchanged |
| spin 78 / 79 | 1.01 → 0.24 | 0.38 → −0.03 | turns from +180° to 0° over 28 updates |
| rear hold 84 / 85 | **0.222** | **−0.097** | the grabber's |
| spin back 80 / 81 | 0.22 → 1.01 | −0.10 → 0.38 | 0° → −180° over 17 updates |

The positions match the [offsets](#grab-posing) the hold records give (0.380, 1.012 and −0.097, 0.222). In the player's
hold neither body moved during the holds and the strikes (world positions constant to the millimetre; in the AI's
hold the victim swayed between 1.012 and 1.076 m ahead); during 72 / 73 and the spins
both move by their clips' root motion, which brings the victim from straight ahead to the hold's point.

**The bone cache.** `0x0023bde8` (a human's bone transform for a bone index) fills, once per update, a cache of 34
bones × 32 bytes at **`0x006b6880` + human index × `0x470`** (`0x0023bca0`; ready flag at `+0x460`) through
`0x00104630`: entry 0 is the root (its velocity and turn from the clip), entry 1 the **pelvis** (bone 1) position and
rotation in the model's frame, and entries 2-33 the other bones composed through the parent table (confirmed (code)).
The pelvis read there, as a position and a quaternion `(x, y, z, w)` in the model frame (`z` up, the model facing
`+y`, inferred from the heading convention), both characters giving the same values:

| Clip | Pelvis position | Pelvis rotation | Change from the idle's rotation |
| --- | --- | --- | --- |
| idle 388 (Rembrandt) | (0.008, −0.011, 1.067) | (−0.558, −0.436, 0.454, 0.540) | |
| hold 82, grabber | (0.001, 0.0-0.03, 0.871) | (−0.701, −0.034, 0.056, 0.711) | 71° about the model's vertical (`z`) |
| held 83, victim | (0.000, 0.000, 0.831) | (−0.693, −0.252, 0.589, 0.332) | 39° about the model's `−x` (a lean) |
| rear hold 84, grabber | (0.000, 0.000, 1.013) | (−0.329, −0.645, 0.537, 0.433) | 40° about (0.36, −0.04, 0.92) |
| rear held 85, victim | (0.000, 0.000, 1.011) | (−0.282, −0.662, 0.454, 0.525) | 42° about (0.69, −0.02, 0.72) |
| strike 51, grabber, mid-swing | (−0.002, 0.168, 0.615) | (−0.507, −0.222, 0.293, 0.779) | |

In the idle the pelvis bone's own `x` axis lies along the model's `+y` (forward), its `y` axis along `−z` and its `z`
axis along `−x`. So in the front hold **the grabber's hips are yawed 71° and lowered 0.2 m, the victim's tilted 39°
and lowered 0.24 m, and neither is rolled onto its side**: a pelvis rotation that turns the bone's `y` axis away from
the model's `−z` (straight down) is the "on its side" pose. The root entry was zero (no velocity, no turn) all through
the holds and strikes and carried only the spins' and connects' root motion (78: about 0.8 m/s forward).

### Moving a grab, and the mount {#grab-turn}

**The stick turns and drags the pair** (`0x00245310`, the grabbing human's movement), confirmed (code); confirmed
(runtime) where marked:

- It acts only when the stick's magnitude (pad record `+0x10` / `+0x14`, buffered) is **above 0.95**
  (`0x005102e8`). At runtime the stick held 60 % and 80 % right did nothing; full right turned the pair.
- The wanted heading is the stick's angle (pad `+0x08` / `+0x0c`) **+ 180°**: the grabber turns its back to the
  stick and walks backward along it, pulling the victim.
- The turn per update, for an angle `d` still to turn: `r = min(max, max × (1 − cos(|d| × π / 4)) / 2 + prev × k)`,
  `k` 0.8 (`0x00510314`), or −0.5 when the direction reverses within 45°; the smoothing branch is taken because
  `0x00510308` is 1, with 4.0 at `0x00510318`. `max` comes from `0x002213d8`, which for a grab reads the table at
  `0x005101b0`: 0.192 rad (11° per update) for a player, 0.0262 for the AI. Runtime table: 0.192, 0.0262, 0.2793,
  0.0436, 0.3142, 0.0698, 0.3142, 0.1047, 0.3491, 0.2094, 0.4189, 0.4189; constants from `0x005102e0`: 0.05, 0,
  0.95, 0, 0, 30, 60, 100, 115, 0, 1, 0.8, 2.0, 0.8, 4.0.
- Runtime: full stick turned the player's front hold by 118° in about 18 updates, 11° per update at first and
  slowing near the heading; the pair then walked backward at **1.125 m/s** in the front hold (record `+0x170` to
  `+0x17c` became 1.125, the grab movement style's walk speed, `0x00254078`) and about 1.22 m/s in the rear hold.
  The victim kept the hold's offsets (front 1.012 ahead, 0.379 right; rear 0.222, −0.097).

**The tackle's mount** (confirmed (runtime), and from the disc's events): after the tackle (5 / 6 from the front,
7 / 8) the pair is in 210 `MOUNTING_IDLE` / 207 `MOUNTED_IDLE` with the victim **0.032 m ahead and 0.120 m to the
mounter's left**, its heading the mounter's + 180°. That is clip 210's type-8 pair event (−0.120, 0.032). The tackle
clips' own pair events: 5 (0.021, 1.013), 7 (−0.092, 1.036), 212 (0, 1.203).

### Grabbed, and breaking free {#grabbed}

**When the player is grabbed** (`Player_UpdateGrabbed`, `0x0027fd68`, the grabber at human `+0xc4`), confirmed
(code); confirmed (runtime) where marked, from a puppet civilian's grab ([Being hit](#being-hit-runtime)):

- **Square** (`0xf` / `0x11`): a struggle strike, 96 or 108 (front or rear), when the player may struggle
  (`0x002258f0`: not hurt, the grabber not raging, own power above a sixth of the maximum) and the grabber's power
  fraction is above 1 / its power class byte `+0x36`. It takes 1 / (own byte `+0x36`) of the **grabber's** maximum
  power, spent even when the strike then cannot start (another move playing), and the strike damages the grabber by
  its clip's damage; a strike that would kill the grabber breaks the grab. Runtime: 96 (grabber 97); each press took
  the 200-power civilian down by about 75 (66.7 for 1 / 3, and the rest is the grab's own drain of 15 per second,
which keeps running through the struggle clip: 198 → 123 over 19 updates = 66.7 + 9.5, then 4 more over the next 7
updates of 96), 68 when 104 was still playing; the
  third press emptied the meter and the grab broke with the player on 94 and the grabber on 95. 96 does 0 damage.
- **Cross** (`0x12` / `0x10`): 104 / 116, a strike back. Runtime: 104 (grabber 105) for 33 updates, 20 damage to the
  grabber, no power spent, then back to the holds 83 / 82.
- **Circle** (`0x1e`, `0xd`, `0xe`): an **escape** (`0x0026cc18`) when `0x00225830` lets it: with `t` a quarter of
  the grabber's maximum power (half if the grabber is hurt) and `p` its power, the escape always works when `p` ≤ `t`,
  and otherwise with chance 1 / `floor(p / t)` (a random number below `floor(p / t)` must be 0); never against a
  raging grabber. The escape plays 100 / 112 (102 / 114) and knocks down and stuns the grabber. Runtime: at 189 of
  200 (chance 1 / 2) the player played 100 and took the clip's 20; the grabber played 101, lay down (`0x180001`), rose
  with 199 2.2 s later and then stood stunned (356).
- **R1 pressed** or `0x19`: a reversal (`0x0026d150`), 90 / 91 or 92 / 93, when the escape did not run, the grabber
  lacks human flag `0x40`, the player lacks `0x80000000`, and the same chance as the escape allows it. Runtime: 90
  (grabber 91) for 44 updates, after which **the player holds the grabber from the rear** (84 / 85, states `0x85` /
  `0x21`) and drains its own power at 15 per second. A press that lost the roll did nothing.
- **A counter at the catch** (`Grab_IntroEnd`, `0x0026c1d8`): when `0x00510254` is set (1 in the street) and a
  grabbed player's command on the update the intro ends is 3 (R1 pressed), the grab becomes 76 `GRAB_FRONT_COUNTER`
  (grabber 77) through `Attack_StartPaired`. Runtime: R1 pressed on that one update (not one update earlier or later)
  played 76 / 77; the grabber lost 100 and was stunned (`0x100000`, then 355, 356, 357), and the player spent 100 of
  400 power and gained 10 rage.
- An AI grabber's own circle while holding the player (command `0x1e`, then `0xd`) took the pair to the ground: 118
  `GRAB_MOUNT` / 119, then 210 `MOUNTING_IDLE` on the player's 207 `MOUNTED_IDLE` (confirmed (runtime); the path is
  not traced).

`CfgButtonMash` plays no part here: its only reader is the theft game ([Stereo theft](#stereo-theft)).

**When the player's victim breaks free**: the grab drains the grabber's power (15 per second) and
`Human_UpdateMeters` (`0x002562d0`) ends it when the meter reaches 0. If the victim has flag `0x20000` or `0x4000`, or
the grabber is hurt (and the victim lacks `0x2000000000`), the victim **escapes** through `0x0026cc18`; otherwise both
let go (95 / 94). A tackle dismounts (`0x00271470`). Confirmed (code); confirmed (runtime): from 25 power the grab
drained at 15 per second and broke with 95 / 94; with the player hurt (200 of 900 health) the civilian escaped with 100,
the player played 101, was knocked down and stunned (`0x180005`), lay in 196 and rose with 199 2.78 s later; the
escapee took 20. A third human hitting a grabber costs it 0.6 of its power (`0x00510274`).

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
`+0x10` **50°** and `+0x14` **60°**. The stick counts as on target within `+0x10` (50°); `+0x14` (60°) is a coarser
gate, and each new target angle (`0x002855f8`) is re-rolled, up to 64 times, until it lies more than `+0x14` + 20°
(about 68°) from the old one. A victim whose brain `+0x26c` is 5 gives 1.5 times the money (at most 999). Confirmed
(code); the angle's frame (world or camera) is not traced.

### Damage, health and reactions {#damage}

`Strike_Contact` (`0x0021b290`) takes the [damage table](#damage-table)'s value, adds a held weapon's bonus, and for
a player applies upgrade percentages (categories `0x13` / `0x19` and `0x1e` / `0x21` / `0x1d` / `0x20`, inferred to be
the upgrade levels). An attacker with flag `0x20` deals the victim's whole health. `0x0021d680` →
`Human_AddPendingDamage` (`0x00264bd8`) stores it at record `+0x118` if larger than what is pending, with the
may-react flag at `+0x11a`; the damage is **doubled** when the attacker has human flag `0x4000` and **quartered** for
class `0x80` against brain type 3. Confirmed (code). Health (record `+0x144`) fell by exactly the table's value on
every hit tested (confirmed (runtime)). Grab moves (`0x00262ac8`) apply their damage on the move's first update.

**Applying it** (`Human_ApplyPendingDamage`, `0x00265f70`, each update), confirmed (code), in order:

1. A held **block** takes the hit first ([Block](#block)).
2. A victim with flag `0x20000000000` (the player has it) cannot drop below 25 % of its maximum in one hit: health is
   set to 25 %, flag `0x10` is set, and the rest is lost.
3. Record `+0x00` `0x20000` triples the damage (`0x00223b70`); allies hurt each other only when both are players.
4. Health drops; the flash clip 665 prevents death. A grabber hit by a third human loses 0.6 of its power.
5. **Hit armour**: no reaction plays (the damage still lands) while the victim is a player in an attack's wind-up or
   chain window (`+0x08` & 3), or a class 13 human with `+0x08` & `0x13`, unless the attacker is a player, class 13,
   raging, has flag `0x200000` or `0x4000`, or plays 617-620.
6. Otherwise the reaction by state: grabbed `0x002688d0`, grabbing `0x00268ea8`, and others; a normal hit
   `0x0026b0a0` ([Hit codes](#hit-codes)).

### Reactions: stun, knockdown and getting up {#reactions}

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

### Being hit, at runtime {#being-hit-runtime}

A puppet civilian (`PoizoCiv`, type 417, power class 2) attacked, blocked and grabbed the player (Rembrandt, 900
health, power class 64) 1.0 m away at a chosen bearing, with `CfgAutoLockAndCombat` written to 0 so that the player
did not turn to face it. Every value here is confirmed (runtime) unless marked; updates are 1/30 s.

**Reactions.** Every hit played the reaction the [table](#hit-codes) gives for the attack's code and side, from all
four sides, for `S1`, `X1`, `SS2`, `SX2`, `XS2`, `XX2`, `SSS3`, `SSX3` and the special 653 / 655; the civilian's own
codes (its list differs from Rembrandt's: `SS2` `0x09`, `XX2` `0x26`, `XS2` `0x1b`, `SSX3` `0x2b`, `SSS3` `0x16`,
653 `0x39`) gave, front / victim's left / behind / victim's right:

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
(`CfgChar` `+0xb8`, the [index → anim id](#damage-table) map):

| Type | 0 `X1` | 1 `S1` | 2 `XX2` | 3 `SX2` | 4 `XS2` | 5 `SS2` | 6 `SSX3` | 7 `SSS3` | 16 653 | 24 grab strikes | 25 throws |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 417 `PoizoCiv` | 27 | 18 | 54 | 45 | 45 | 36 | 63 | 54 | 70 | 60 | 70 |
| 418-420, 271, 272 civilians, bums | 23 | 14 | 46 | 37 | 37 | 28 | 51 | 42 | 50 | 20 | 30 |
| 26 Vermin, 38 Ash | 23 | 15 | 46 | 38 | 38 | 31 | 53 | 46 | 69 | 50 | 57 |
| 30 Rembrandt (before the 115 % scale) | 23 | 15 | 46 | 38 | 38 | 31 | 53 | 46 | 5 | 50 | 57 |

**Blocking** (R1 held, the player's `0x8` pressed through the pad): the block started on the next update (606, state
`0x8001`, no 605 seen) and **every blocked hit did 0 damage**, from every side. Light and medium hits played the block
reactions of the [table](#block) by the reaction's direction: `S1` 608 front, 611 left, 610 behind; `X1` 611, 610, 609;
`SS2` 608 then 611; `SSS3` 612, 615, 614. The heavy and crushing ones (`SSX3`, 653 / 655) played **616 `BLOCK_DODGE`**
(a duck) about 4 updates before their hit, which then missed: `0x00261578` plays 616 (or 628 with weapon type 3 against
weapon type 3) when the blocker's record `+0x14` is `0xd` (`0x00254e78`), confirmed (code); the attacker's anim event
`0x24` sets `0xd` ([Block](#block)). The block reaction table at `0x00510898` holds -1 in entries 0-3 and 12-15, so a
modified strength of 3 (entry 0) would break the block (confirmed (code) at `0x002671a8`); no hit broke the player's
block. The puppet civilian, blocking the player, ducked under `X1`, `XX2` and `XS2` (616) and blocked `S1`, `SS2`,
`SSS3` and `SSX3`.

**Rage gained by the player** (rage 0 and the hold timer reset before each combo; `S1` gains are 0 or 1):

| Hit | `S1` | `X1` | `SS2` | `SX2` | `XS2` | `XX2` | `SSS3` (19 or 20) | `SSX3` | counter 76 | 104 in a grab |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Rage | 1 | 5 | 1 | 13 | 10 | 6 | 4 | 7 or 15 | 10 | 1 |
| Blocked | 0 | | 0 | | | | 0 | 6 | | |

Being hit gave the player no rage. Every gain set the hold timer to now + 5000 ms. A later run gave `S1` 1, `X1` 5,
`XS2` 10, `SX2` 13 and `SSX3` 15 every time; the 7 is the halved gain ([Rage](#rage)): `SSX3`'s two awards 3 and 8
give 2 + 5 at a factor 0.72, and `S1`'s 1 gives 0.

### Power meter {#power-meter}

Record `+0x148`, maximum the power class's `+0x28` (400) through `0x00223068` (with upgrades). In
`Human_UpdateMeters` (`0x002562d0`) it **drains** by `CfgBurnRates`: 15 per second grabbing, 15 tackling, 2 in a
further grab state, 0 blocking; and **refills 60 per second** (`+0x2a`) otherwise. At 0 a grab ends
([Grabbed](#grabbed)). `Human_PowerFraction` (`0x00226510`) and `Human_SpendPower` (`0x00226448`); neither spends
nor drains while raging or with flag `0x4000000` (the fraction then reads 1.0). Confirmed (code), confirmed (runtime)
(400 → 377 over 1.4 s of a tackle; 25 → 0 at 15 per second in a grab).

**Per class.** The power class is human `+0x1b8` (AI) or `+0x1b9` (player), through `0x00222b78`
([Character classes](characters.md#classes)). The maximum (`0x00223068`) is the class's s16 `+0x28`; for a player
with the upgrade flag (`0x00424130(0x6fe998, 6, 0xb)`) it is scaled by 1 + byte `+0x02` / 100 of the record from
`0x00228860`; and **while hurt** (`0x00222ff8`: health percentage below the class float `+0x04` × 100) it is scaled
by the class float `+0x18`; each step `int(x + 0.5)`. The refill (`0x00256a60`) adds the class's `+0x2a` × the
update's time, capped at that maximum. Confirmed (code). Runtime classes: `PoizoCiv` class 2 = 200, 32 per second,
hurt factor 0.55; Vermin (type 26) class 7 = 400, 100 per second, hurt below 35 %, factor 0.75; Rembrandt class 64 =
400, 60 per second, 0.75. Confirmed (runtime).

The "civilian" whose meter refilled to 300 at about 100 per second was the puppet at human 0 in those runs, which
was **Vermin** (1800 health, 400 power), not the `PoizoCiv`: the script had set its health to 600 of 1800 (33 %,
hurt), so its maximum was 400 × 0.75 = 300 and its refill class 7's 100 per second. Confirmed (runtime) from the logs
and the class values. The hit-armour run used the same puppet.

### Rage {#rage}

Rage is human `+0x650`, up to the Warrior class's s16 `+0x00` (78 for class 6). `Human_AddRage` (`0x00264cf8`),
confirmed (code), adds, only for a player-flagged human (`0x2000000`) not already raging:

`trunc(points × f × gain / 100 × h × s)` (the float → integer at `0x0042c718` truncates), where `f` is 1.0 when the
award is 25 points or fewer and 0.1 when it is more (the whole award, not the excess: `CfgRagePoints` `0x005108d8`,
table `0x005108e0`), `gain` the Warrior class byte `+0x02` (144), `h` 0.5 when the player's **repeat flag** is set,
and `s` the player's **throw bonus** while its state word (record `+0x00`) has `0x1000`, throwing (`0x002239e0`),
else 1.0. The meter is **capped** at the maximum; the first time it fills, `0x00236ec8` announces it. Each gain sets
the hold timer `+0x648` = now + 5000 ms (`CfgRageHandlers`). Confirmed (code). The gains per hit at runtime are in
[Being hit](#being-hit-runtime): 0 to 15 per hit, 0 for most blocked hits, none for being hit (confirmed (runtime)).

**The repeat tracker** holds both. Each player has a 12-byte tracker at `*(0x0051489c) + 0x178 + player × 0x5c`
(the player's `+0x1b0`), so the bytes the rage code reads as `+0x17c` and `+0x182` of `*(0x0051489c) + player ×
0x5c` are its fields `+0x4` and `+0xa`:

| Offset | Size | Meaning |
| --- | --- | --- |
| `+0x0` | u32 | game time (ms) of the last hit noted |
| `+0x4` | float | the throw bonus `s`: 1.0, + 0.27 per grab or mount strike, at most 2.0 |
| `+0x8` / `+0x9` | u8 | how many square-kind / cross-kind hits in a row, at most 5 |
| `+0xa` | u8 | the repeat flag `h`: set when a count reaches 6 |
| `+0xb` | u8 | the kind of the last hit noted (4 = other) |

`Human_ApplyPendingDamage` (`0x00265f70`) notes a hit through `0x00265dd0` after it has given the hit's rage, when the
attacker is a player (pad `+0x1b`) or its brain is of type 3 and the two are not allies (`0x00222a90` →
`0x00290230`), for a hit that lands and for one a block stops. `0x00265dd0` picks the **kind** from the attacker's
current anim id (record `+0x20`):

| Kind | Anim ids |
| --- | --- |
| 0, square-ended | 12 `S1`, 14 `XS2`, 16 `SS2`, 19 / 20 `SSS3`, 35 / 40 / 46 the bat, baton and knife `SS2` |
| 1, cross-ended | 11 `X1`, 13 `XX2`, 15 `SX2`, 17 / 18 `SSX3` |
| 2, mount strikes | 219, 221, 223 `MOUNT_COMBO_STRIKE_01`-`03` |
| 3, grab strikes | 51, 53, 55 |
| 4, other | everything else |

and `0x00418428` notes it: the flag is cleared first; both counts reset when the kind is 4, differs from the last
kind, or more than 5000 ms passed since the last hit; kind 0 or 1 adds one to its count, and a count reaching 6 is
put back to 5 and sets the flag; kinds 2 and 3 add 0.27 to the bonus (`min.s` with 2.0); the time and the kind are
stored. `0x00254e78` puts the bonus back to 1.0 every update unless the human is grabbing from the front (state
`0x40`) or throwing (`0x1000`). Confirmed (code) at `0x00265dd0`, `0x00418428`, `0x00264cf8` and `0x00255018`.

So **the sixth hit of the same kind in a row, each within 5 s of the last, halves the rage of every later hit** of
that run until the kind changes, and **each strike in a grab raises the following throw's rage** by 27 %, up to
double. Confirmed (runtime), puppet civilian, `X1` tapped every second: the first six hits gave 5 rage each and the
counts at `+0x9` went 1, 2, 3, 4, 5, 5 with the flag set by the sixth; the seventh and eighth gave 2 each
(`trunc(5.76 × 0.5)`); with 5.17 s between hits the count stayed at 1 and every hit gave 5. The earlier reading that the
game rewrote a written flag "on the next update" is the clear at the next noted hit.

**The points** come from the stats system, which also adds them to the per-player score at `0x006fe490 + player ×
0xc0`: `0x002653d8` maps the attack to an event (`X1` event 1, `S1` event 2, ...; a blocked hit is halved through
`0x004ed9c8`), grab moves and throws map through `0x00264fa0`, and the event's value is count × a table entry
(`0x004ed988` → table 2 at `0x00715510`; `0x004ed8c8` → table 3 at `0x00715500`; `0x004ed948` → table 4 at
`0x00715818`). The tables are filled by `CfgSetStatValue`; at runtime: table 3 = 6, 4, 1, 7, 20, 3, 5, 5; table 2 =
75, 200, 150, 60, 150, 150, 100, 150, 100, 150, 100, 75, 30, then 0; table 4 = 50, 300, 5, 25, 250, 100, 300, 200,
100, 5, 50, 10. Confirmed (code) for the path, confirmed (runtime) for the values.

**Per attack** `0x002653d8` makes **two awards**, each through `Human_AddRage` and truncated on its own: event 2
(table 3 entry 2, 1 point) × count A, and event 1 (table 3 entry 1, 4 points) × count B. A blocked hit halves each
award's points with a shift (`0x004ed9c8`: count × value >> 1). Confirmed (code); every result below matched the
runtime gains at `gain` 144 (confirmed (runtime)):

| Attack | A | B | Gain | Blocked |
| --- | --- | --- | --- | --- |
| 11 `X1` | 0 | 1 | 5 | |
| 12 `S1` | 1 | 0 | 1 | 0 |
| 13 `XX2` | 1 | 1 | 1 + 5 = 6 | |
| 14 `XS2` | 4 | 1 | 5 + 5 = 10 | |
| 15 `SX2` | 2 | 2 | 2 + 11 = 13 | |
| 16 `SS2` | 1 | 0 | 1 | 0 |
| 17, 18 `SSX3` | 3 | 2 | 4 + 11 = 15 | 1 + 5 = 6 |
| 19, 20 `SSS3` | 3 | 0 | 4 | 0 |
| 21-24 | 1 | 0 | 1 | |

Other ids award other events: `0x1f5`, `0xfa`, `0xc1`-`0xc7`, and the grab and escape ids `0x68`, `0x74`, `0x78`,
`0x7a` (event 2 × 10), plus ranges such as `0x22`-`0x2c` (confirmed (code), the events' meanings not traced).

**The meter falls**: an unspent meter decays by the maximum × Warrior byte `+0x03` / 100 per 20 s (`0x00510294`;
200 % → 7.8 per second) once the hold timer passes; while raging it drains by byte `+0x04` (240 % → **9.36 per
second**), and rage ends at 0 (`0x00236fb8` clears `0x80000`). The HUD is told at 90 % and below 11. Confirmed (code)
at `0x002562d0`; confirmed (runtime): 78 → 0 in 8.34 s raging, 40 → 25 in 2.0 s idle.

**L1 + R1** (`0x1f`) with a full meter starts rage (`0x002843f8` → `0x00284340`, `0x00236d28`): 643 `RAGE_START` for
about 2.1 s, human `+0xe0` flag `0x80000`. Confirmed (runtime). **While raging**, confirmed (code), confirmed
(runtime) where marked:

- **No damage bonus**: `S1` still deals 17 and grab strikes 57 (runtime).
- **No power** is spent or drained (runtime: 400 through a grab and two strikes).
- The player's grabs cannot be struggled out of or escaped (`0x002258f0`, `0x00225830` test the grabber's rage).
- Every hit causes a reaction (hit armour does not apply), the power strike is 63, the specials 645 / 649, the
  rumble one step stronger, and no rage is gained.

The command handled
at `0x0027bbd0` uses a flash (665 `SPECIAL_FLASH`, inventory slot 1) when health is below its maximum, or fills rage
when health is full and an upgrade allows it (confirmed (code)).

### Target selection {#targets}

`Player_PickTarget(range, h)` (`0x0027a6c0`), confirmed (code):

1. Keep the current target (human `+0xc8`) while `0x0027a120` accepts it.
2. Otherwise search along a heading: the camera-turned stick's angle if the stick is above 0.01 (`0x0021d1a0`), else
   the facing. First humans within range × 1.1 and within 54° (`0x0051096c`) of it, then objects and glass (world
   `+0x844`, `+0x840`, `+0x84c`), then, with no current target, humans within range × 0.9 at any angle; then within
   135°: objects at × 0.8 and humans at × 0.7 (`0x002796a0`). The nearest wins (`0x003868d0`).
3. The filters (`0x00279410`, `0x00279568`) skip allies and the same gang (brain `+0x20c`), humans more than 2 m
   higher or lower (`0x00510970`), those with `HuSetNoAutoLock` (flag `0x100000000000`), the dead and the airborne;
   the first also skips the knocked down.

**Lock-on** is the fight stance's movement state `0x00241b90` (the normal one is `0x00240e38`), confirmed (code):
the human is **locked** when it has a human or object target and either L1 is held (`CfgLockOn`), or `CfgAutoLock`,
`CfgAutoLockAndCombat` (1 in the street) or auto-combat is on. Locked, it **turns to face the target every update**
and the stick (dead zone 0.12) moves it without turning, the stick's angle from the facing (record `+0xdc`) picking
the combat-walk clips 380-387; unlocked, it turns to the stick. Blocking stops the movement. A target farther than
2.5 m is dropped unless record `+0x00` has `0x8` or `0x4`.

**L1**: pressed (`0x0027da10`) or held (`0x0027dc00`), it sets record `+0x00` `0x8`, enters the stance and picks a
target; released (command 8, `0x00227b98`), it clears `0xc` and opens a 264 ms window in which a second tap picks and
locks. In the stealth state (`0x00244770`) L1 leaves it; with weapon type 5 and no enemy target it enters mode
`0x13` (`0x00227b30`). Confirmed (code); confirmed (runtime): L1 held gave state `0xd`, a target, and the movement
state `0x00241b90`.

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

A target that the walk took beyond 2.5 m was dropped and the player went back to the walk start 413.

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
grab](#grab-turn). So a 35 % stick moves a grab not at all, a fight stance at 3.429 m/s, and the free player at the
walk speed.

**Turning into an attack** (`Attack_Start`, `0x002625a8`): with the target within the attack's far range, it tells
the target (`0x0021d5c0`) and steers with `0x002761c8`: turn and slide so that the target, moved by its velocity over
the time to the hit + 0.1 s, sits at the clip's reach (+0.07 m for a target scaled above 1.1, -0.1 m from behind).
Beyond the range it only turns (`0x00276008`), capped at 8. Confirmed (code).

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
- **Mode 2**: `0x002878b8`, a **timing game**: each cross press (`0x12`) is judged by `0x001b8d38` against a moving
  meter (float `+0x454` of `0x0060fc60 + player × 0x540`): a press outside the band at `0x0050d6c4` / `0x0050d6c8`
  scores 1 or 2 (the better band), a press inside it resets the count and fails the theft (`0x002366d8`); other
  commands reset `+0x42` to -1. Confirmed (code); that the meter sweeps is inferred; not tried at runtime.
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

`src/combat/` holds the player's combat rules as a self-contained core: it decides, and `src/human/` plays what it
decides through the human's animator and lands the hits. Everything runs on the fixed 1/30 s step; time-based meters
take game time in whole milliseconds and carry the fraction of a point, as the original does; the coin flips come from
a seeded generator (`CombatRandom`), so a run with the same seed and input is the same run.

| File | What it does |
| --- | --- |
| `combat/commands.*` | the nine trigger tables (`CommandTables::street()` is the street's), and the matcher that turns each update's buttons into one command in the documented order, with the tap (1-6 samples), long hold (4th sample, or a release within 3) and history hold (7) counted per button |
| `combat/attacks.*` | square's choice (target, snap, run, walk, `S1`), cross's `X1`, the object attack's clip, the charge and dive condition, the chain table, `attackTiming()` (the [timing table](#attacks) per attack) and `AttackChain`: the hit, the chain window, the end and the recovery counted in updates, one buffered press |
| `combat/anim_ranges.*` | the Anim Range List decoded from the character data's chunk (direction, reach, far range, damage, hit code, flags), and `applyClassDamage()`: a class's damage table written over it by the index → anim id table, scaled for a player |
| `combat/reactions.*` | the hit code taken apart, the victim's side, `hitReaction()` (the strength, height and direction rules, the combo attacks 13-15 and 17-20, and the table at `0x00510798`), the dying reaction, the block reactions and when a block holds |
| `combat/power_class.h` | a human's power class: the power maximum and refill, the hurt fraction and the power factor while hurt, the stun and ground times and the struggle divisor (`+0x36`); the player's and the street civilian's |
| `combat/being_hit.*` | the victim's decisions: the warnings an attacker's clip sends (events `0x24` duck, `0x26` early block), the duck's counter (event `0x25` in 616, the commands that ask for it, 617-620 by side), the hit armour, the health floor, `blockHit()` and the mash's cut |
| `combat/grabbed.*` | held in another human's grab (`Player_UpdateGrabbed`): the struggle and its cost to the grabber, the escape or reversal roll, the strike back 104 and the counter at the catch (76 / 77) |
| `combat/rage_awards.*` | the rage a hit gives its attacker: the two awards per attack (event 2 and event 1), halved when blocked, and the repeat tracker (`+0x182`: six of a kind within 5 s halves later hits; the throw bonus) |
| `combat/lock_on.*` | the lock-on rule by `CfgLockOn` and the auto settings, the combat walk's clip by the stick's angle (380-387), when a target is dropped, and the grab's turn step (`0x00245310`) |
| `combat/meters.*` | health, the pending damage (the update's largest), `strikeDamage()`, the power meter (400, refill 60/s, drain 15/s, `spend()`) and the rage meter (78, the gain formula truncated per award, the 5 s hold, the 7.8/s decay, start, the 9.36/s drain); the power maximum scaled by the hurt factor |
| `combat/grab.*` | when a grab may start, the search ranges (far range × 1.25), the nearest-candidate search, the throw by stick side and wall, and one update of a grab (strikes, the power strikes with the rear spin first, throws, the release with too little power, the R1 and circle spins, the L2 let-go, the mugging; nothing spent in rage) |
| `combat/stick_games.*` | the mugging, the stereo theft's rotation (mode 3) and the button mash (mode 1) |
| `combat/player_combat.*` | the dispatcher: block, chain, meters, the routes (grabbing, tackling, mugging, theft) and the commands, in the original's order; the grab breaks at 0 power |
| `combat/combat_tuning.*`, `debug/combat_tunables.*` | the values above as tunables, category **Combat**, registered at start-up beside the game's |
| `human/fighter.*`, `human/fighter_grab.cpp`, `human/fighter_victim.cpp`, `human/fighter_clips.h` | the player's combat inside the human (split as the attacker, the grab and the victim side): builds `PlayerCombat`'s input (the camera-turned stick in the facing frame, the pad's stick, the gait, game time, the target in front and the grab search), plays its clips, turns and slides into an attack, poses a grab (the alignment, the connect, the gate, the snap and the attachment), turns and walks the grab by the stick, lands the hits with their rage, locks onto a target and combat-walks round it; and the player hit (a duck and its counter, the block, the health floor, the hit armour, the reaction, stun, knockdown and mash) and held in a grab (the counter at the catch, the struggle, the strike back, the escape and the reversal) |
| `human/victim.*` | what the player and the target share as victims: the update's largest hit, the reaction it plays, the stun (its exit waits for the clip playing to end), the knockdown, the ground time, the rise and the mash |
| `human/pair_placement.*` | the pair's geometry: offsets in the grabber's frame from a range record's direction × reach or a clip's type-8 pair event, the alignment (`Pair_AlignStart`), its time, the gate at the connect's end and `Pair_CheckPlace` |
| `human/target_human.*` | a passive target for the sandbox on `Victim`: health, the reaction, the stun, the knockdown, the ground time and the rise, the dying clip, the root motion of its reactions and throws |

**In the player.** `human::Player` runs the street's `CommandMatcher` on the pad's buttons and gives the human the
command, the buttons and the targets; the human calls the fighter each update it is on the ground and not climbing,
after the locomotion and stamina. While the fighter holds the body (blocking, holding someone, mugging, or an attack's
clip playing) the stick does not move it: the clip's root motion does, with the slide an attack starts. Triangle keeps
its own order (climb, context action, jump) and is not read while the fighter holds the body; L2 still sprints, but
not while blocking.

**Lock-on and the combat walk.** The attack's target search, or L1 when there is none (**Coney's choice**: L1
searches as far as a target is kept, 2.5 m), gives the player a target. With the original's settings (`CfgLockOn` 0,
`CfgAutoLockAndCombat` 1) L1 alone does not lock: the auto setting does, so while he has a target the player faces it
every update and the stick past the 12 % dead zone walks him at 3.429 m/s in any direction, with the combat-walk clip
(380-387) by the stick's angle from his facing. The target is dropped beyond 2.5 m (unless L1 or a grab holds it),
when it leaves the fight or has no health left, and (**Coney's choice**) when it is not standing. Grabbing, the stick
past 95 % turns the pair towards it at most 0.192 rad an update (the carry 0.8, −0.5 when the turn reverses within
45°) and pulled back walks it backward at 1.125 m/s (front hold) or 1.22 m/s (rear), both bodies by their clips.

**Rage from hits.** Each hit the player lands or has blocked gives its two awards (`rage_awards.*`): the attack's
count A × 1 point and count B × 4 points, each through the gain formula and truncated on its own, a blocked hit
halving each with a shift; while the repeat tracker's flag is set each award is halved, and while throwing it is
multiplied by the throw bonus. The hit is then noted: six square-ended (or six cross-ended) hits in a row, each within
5000 ms of the last, set the flag; each grab or mount strike adds 0.27 to the bonus (up to 2.0), which goes back to 1.0
every update the player is neither grabbing from the front nor throwing. Being hit gives nothing. **Coney's choice**:
the counter 76 awards 7 points and the strike back 104 1 point (the rage seen at runtime, back-derived), as the
events of those two moves are not mapped.

**The player hit** (`fighter_victim.cpp`; nothing attacks him yet, so `Human::takeHit()`, `warn()` and
`catchInGrab()` are the entry points a future attacker and the tests use). An attacker's clip warns him as its events
pass: blocking, event `0x24` makes him duck (616) and the attack passes over him, and event `0x26` plays the block's
reaction early. Ducking, square or cross pressed or held while 616's own `0x25` events fire (frames 6-13) asks for the
counter, played on the next update by the side the target stands on: 617 front, 618 right, 619 back, 620 left. Each
update the largest pending hit then lands in the original's order: a duck lets it pass; a held block cancels it with
its block reaction unless the hit breaks it (strength 3, or attacks 26-34); the damage is taken as it is, held at the
health floor (a hit cannot take him below 25 % of his maximum); a grabber hit by a third human loses power; winding up
(`+0x08` `0x1`) or in the chain window (`0x2`) his hit armour holds against a non-player, non-raging attacker not
playing 617-620, so the damage lands and his attack goes on; otherwise the reaction plays and his attack is lost. A
plain reaction returns to the fight idle; a stun hit holds him 200 ms, then 357 when the reaction's clip has ended
(the stun's loop 356 does not play); a knockdown lays him down for 2750 ms, then 199, and a stunned knockdown stays
stunned for 200 ms after the rise. Every command made while he lies in 196 cuts the ground time (and a stun's) by
2750 ms divided by 1, 2 or 3 at random (**Coney's choice**: the three evenly). The power meter's maximum is scaled by
the hurt factor while hurt (below 30 % health: 0.75 × 400).

**Held in a grab** (`grabbed.*`): R1 on the update the grabber's intro ends counters (76, the grabber plays 77, both
paired, needing and spending power); otherwise he plays the grabber's set's reaction (73 or 75) and the held loop.
Square struggles (96 / 108), costing the grabber its maximum divided by the player's struggle divisor (`+0x36`, 3);
the grab's drain of 15/s goes on through it. Cross strikes back (104 / 116, 20 damage). Circle escapes (100 / 112,
the grabber knocked down and stunned) and R1 reverses (90 / 92, then he holds the grabber from the rear, 84 / 85) on
a won roll: with `t` a quarter of the grabber's maximum (half when it is hurt) and `p` its power, always when `p` ≤
`t`, else 1 in floor(`p` / `t`); never against a raging grabber, and no reversal of one with flag `0x40`. The
grabber's power running out ends the grab, with an escape when the grabber is hurt.

**The clips** (anim ids, played through the human's animator, `AnimState::Attack` returning to the fight idle 358 and
`AnimState::Hold` keeping its loop): the chains `S1` 12, `SS2` 16, `SSS3` 19, `SSX3` 17, `X1` 11, `XX2` 13, `SX2` 15,
`XS2` 14 and the snaps; the run attack 24 and the charge 0 and dive 1, after which the run resumes when the stick is
still at a run; the block 606, or the shuffle 607 with the stick pushed; rage 643; the grab 71, 72, then the hold 82
(victim 73, then 83), or from the rear 71, 74, 84 (victim 75, 85); the miss 71, 69, 389; the tackle 4, 5, then 210
(victim 6 when the player's 5 starts, then 207),
the tackle's miss 4, 2; the grab strikes and power strikes with the victim's next id (52, 54, 56, 58, 64); the spins
78 / 79 to the rear hold 84 / 85 and 80 / 81 back to 82 / 83; the throws with the victim's next id, then 196; the
let-go 95 / 94; the mugging 78, 338, 340 (victim 79, 339, 341) with 342 / 343 while the stick is on target and 344 /
345, 80 / 81 on success; the mounted strike 212 back to 210.

**The target** (`human::TargetHuman`, Coney's own, placed only by a sandbox layout's `target` line,
[Sandbox](../guides/sandbox.md)): it takes the hit with the attacker's hit code and flags, and picks its reaction with
`hitReaction()`; a reaction whose clip has an event of type 7 knocks it down (196) for the ground time, then it rises
with 199; a stun hit (flag `0x400`) plays the reaction, then 356 until the stun time passes and the clip playing has
ended, then 357 and the idle; a hit at 0 health plays the dying reaction and lies in 196 for good; a grounded target
takes 195. Its reactions and throws move it by their clips' root motion. Its numbers are the street civilian's power
class (hurt below 35 %, stun 750 ms, ground 2000 ms). It has no brain, never moves by itself, blocks nothing and is
drawn with the player's model.

The tests drive all of this with input scripts played through the pad records at partial stick deflections:
`tests/combat/` the core (each attack's timing, the chain, the grab rules, the meters, the reactions, the class damage,
the victim's decisions, the grabbed player, the lock-on and the rage awards), `tests/human/being_hit_test.cpp` the
player hit (the duck and its counter, the block, the floor, the armour, the stun, knockdown and mash, held in a grab,
the rage and repeat tracker, the combat walk and the grab's turn), `tests/human/combat_test.cpp` the human with
synthetic clips (the combo and its reactions and stun, the stun's 750 ms, the block holding the body while the stick at
0.6 turns it, the grab, strike and throw with the rise 2 s later, the R1 spin and the L2 let-go, the tackle, the turn
into an attack, the knockdown, the victim at the front and rear holds' offsets after the connect, a paired clip from the
attacker's set), `tests/human/pair_placement_test.cpp` the pair's geometry, and
`tests/sandbox/disc_sandbox_combat_test.cpp` Rembrandt from the disc in the fight yard (`assets/sandbox/combat.layout`;
the scripts `tests/support/combat_*.txt`: a combo, a grab with a strike, both spins and a throw, a tackle, a mugging;
clip ids, counts and hashes only; and the pelvis of the holds 82-85 against the runtime one: 82-84 within 0.1°, 85
within 2.5°, all within 1 mm).

**Disc test** (`[disc][combat]`, counts only): Rembrandt's list has 722 records, 160 with damage; every attack combat
starts has one; the grab and tackle ranges come out at 3.12 m and 3.75 m as at runtime; every clip the fighter and
the target play is there (120 ids), 41 of the reactions with a knockdown event. **The file's damage is not the
runtime damage** (confirmed (Coney's disc check)): Rembrandt's file says `S1` 40, `SS2` 40, `X1` 45 where play gives
17, 36, 26, because the character class's table is written over the list ([Damage](#damage-table)).
`applyClassDamage()` does that write into each human's own copy of the list (`Human`'s `classDamage`), but Coney
does not yet read `CfgChar`'s damage table from the config script (the script runner keeps table arguments as nil),
so the game plays the file's damage for now.

**Coney choices**, where the research is silent or inferred:

- Inside one trigger table a later matching entry overwrites an earlier one, as the tables do between themselves;
  trigger 4 (query) never matches.
- Attack timing where a column was not measured: the recovery starts 3 updates before the end (as `S1`'s); an attack
  with no window opening has none; `SS2` closes and ends as `S1`; `SSX3` ends at 30; the run attack ends at 21, the
  charge at 27 and the dive at 60 (their clip lengths seen at runtime); every attack not measured (the snaps, the
  moving attacks, the throws, the grounded and mounted strikes) hits 2 updates in, as `S1`. The attack keeps counting
  under a held R1.
- `SS2`, square is always `SSS3` (19), never 20; a grounded target takes 193, never 194; at a sprint (gait 5) square is
  `S1`; the dive takes the charge's conditions; a buffered snap plays where a square would continue the chain.
- A side is "front" up to and including 45° and "rear" beyond 135°; a height difference beyond 1.5 m counts as 0.9 to
  1.5 m.
- Circle without the stick from the front hold does nothing (`0x0026f008` is not traced); a grab plays one move at a
  time; a throw lets go at once; the rear power strike's spin plays in front of the strike, whose timing starts with
  it; the release with too little power goes straight to the idles, and the grab broken at 0 power plays the let-go.
  A tackle also ends when the power meter is empty, and any hold when the victim has no health left.
- The grab and tackle search takes the nearest candidate by straight-line distance with no facing cone. The attack's
  target search uses the attack's far range in `Player_PickTarget`'s first two passes (the third finds no human the
  second missed); within the far range the attacker faces the target and slides so that it stands at the clip's reach,
  spread over the updates to the hit, on top of the clip's root motion; beyond it the attacker turns at most 8°
  (read as degrees).
- **Posing a grab** follows [Posing a grab](#grab-posing): circle plays 71, then 72 (from the front) or 74 (from the
  rear, when the player stands on the victim's rear side), the player turning to face the victim over 71's playing
  time while the victim waits in its idle. When 72 / 74 starts, the alignment turns and slides the player over 0.1 ×
  the clip's time (two updates for 72) so the victim stands at the clip's reach straight ahead, and turns the victim
  to face him (or away); beyond the far range × 1.25 the grab fails with 69. The victim then plays 73 / 75 **from the
  player's anim set at its rate** (a paired clip, fade 0), and both bodies move by their clips' root motion. At the
  clip's end the gate (1.297 m, 0.2 m in height) releases the grab or the victim is snapped to the hold's offset from
  82's / 84's range record, (0.380, 1.012) facing the player or (−0.097, 0.222) facing his way, and attached: each
  update it is put at the player's transform × that offset, its own root motion ignored. A spin (78 / 80) detaches it
  so both bodies move by their clips, as at runtime, and its end snaps it to the other hold. A strike, power strike or
  throw is refused (nothing played or spent) unless the victim stands within 0.3 m of the current hold's point. The
  mounted victim is attached at 210's type-8 pair event, (−0.120, 0.032): 0.032 m ahead and 0.120 m to the left,
  facing the other way. **Coney's choices**: the grab's side is decided as the intro starts (a passive target does
  not move in between); the place a move checks is the current hold's point (the strikes' points are the front
  hold's); the victim's turn spreads evenly over the alignment's updates and a slide under 0.01 m is left out; the
  moves of a hold switch both humans with no fade, the let-go keeps the combat fade.
- The mugging: the 50° tolerance; a random first target; each move between the tolerance plus 20° and 360° less that;
  the period counts game time. The theft: clockwise steps neither add nor take away; the 250 ms pause ignores the
  stick. The mash: the first press counts, a press's gain is truncated, and other commands are ignored.
- The block is read only when the player is free (not grabbing, tackling, mugging or in a theft); it turns the player
  towards the stick at the standing turn rate.
- The stun's loop is 356, as seen at runtime (the code names 355 as a stunned reaction's return).
- `applyClassDamage()` rounds as the PS2's floating-point unit does (toward zero), which gives the research's 34 for 30
  at 115 %.
- **The shared Anim Range List is not reproduced**: in the original the class damage is written into the list the
  character's humans share, so the newest human's class sets everyone's damage ([Being hit](#being-hit-runtime)).
  Coney gives each human its own copy, as the design plainly means; whether the original intends the overwrite is
  open.
- The player hit: the stun's exit (357) waits for the clip playing to end; a mash also cuts the time of a stun taken
  with the knockdown; held in a grab or holding someone, a hit only takes health (the reactions by those states,
  `0x002688d0` and `0x00268ea8`, are not traced); the health floor stops a hit that starts above 25 %, a player
  already at or below it takes the whole hit; the escapee takes its escape clip's own damage, as at runtime.
- The duck's counter is asked for once per duck and the command that asks is spent (no attack starts from it); it
  plays on the next update with no steering, against the attacker that made the player duck, else the nearest target
  within 1.25 × 617's reach, and hits once.
- Held: the counter at the catch costs the grabber a quarter of its maximum; a reversed hold has no passive target
  behind it (the grabber is an entry point, not a `TargetHuman`).
- The combat walk's clip by eight even 45° sectors centred on the clips' directions; the walk starts at its full
  speed (the 5 slower first updates backward are not known).

**Not yet**: an attacker for the player (no human attacks him yet, so the victim side runs only in the tests); the
warnings of the player's own clips to the targets (they never block); the rage of throws and of the attacks beyond
the chain and moving attacks (their events are not mapped, so the throw bonus changes nothing yet); the hurt
multipliers `+0x10` / `+0x14`; weapons, breakables and the theft's car windows (no objects yet); the class damage
table read from the disc (`CfgChar` waits for the script runner's tables); and the allies and class 13 rules a fight
between humans needs.

## Open questions

- **The block**: whether a strength-3 hit can still break a block. (Answered: `+0x14` = `0xe`, message `0xa5`, is
  the duck counter, [Blocking](#block).) Still open: the counter at runtime, and the AI tactics' answer.
- **The shared Anim Range List**: whether the overwrite by the newest human is intended, and which humans share a
  list ([Being hit](#being-hit-runtime)). Coney does not reproduce it.
- **A blocked `SSS3`'s rage**: 0 at runtime ([Rage](#rage)) where the two awards' formula gives 1 (3 >> 1 = 1 point,
  as `SSX3`'s blocked 1); Coney gives 1.
- **The stun after a knockdown**: it ends at the rise + 200 ms, before 199 ends, yet after 653 the player stood in
  356 for 7 updates before 357 ([Being hit](#being-hit-runtime)); Coney plays 357 as 199 ends.
- **The halved rage** (answered): the repeat tracker ([Rage](#rage)). Still open: the throw bonus at runtime, and
  whether the brain type 3 that also notes hits is the ally brain.
- **The backward combat walk** (answered): 3.429 m/s like every direction, after 5 slower updates
  ([Target selection](#targets)). Still open: what slows those 5 updates.
- **Class 13**: which character class it is (it gets hit armour and adds 2 s to a knockdown).
- **The rage events**: the meaning of the events beyond the chain attacks' (`0x002653d8`, `0x00264fa0`).
- **Commands `0x30`-`0x39`**: which scripts or weapons make them; `0x36`-`0x38` and the d-pad (`0x27`).
- **Theft mode 2 at runtime**, and which objects use modes 1 and 2.
- **The grab's front circle** `0x0026f008`, the `0x00510980` table and the further grab state of `0x005101f0`.
- **Square at a sprint** at runtime, and the moving attacks' hit timing (the victim was out of reach in the tests).
- **The mugging's angle frame** (world or camera).
- **The fence break** in slot 10: which script reacts to the charge.
- **The far ranges' class table** (`0x002545e0`, table `0x0055d640`): which of the class's 45 floats goes to which
  anim id, as the damage table's index → id map does for the damage.
- **The timing columns not measured**: `SS2`'s window close and end, `SSX3`'s end, and the hit of the snaps, the
  moving attacks, the throws and the grounded and mounted strikes.
- **The grab at runtime**: the placement is confirmed ([Grab pose at runtime](#grab-pose-runtime)); still open are
  the fields the alignment writes (human `+0x2e0`-`+0x332`, victim `+0xa0` / `+0xb0`).
