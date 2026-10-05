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
| `0x002542e8` | `AnimRange_Damage` | the Anim Range List's `+0x0a` | confirmed (code), runtime |
| `0x00226448` / `0x00226510` | `Human_SpendPower` / `Human_PowerFraction` | the power meter | confirmed (code), runtime |
| `0x00222ef0` | `Human_HealthPercent` | health over maximum | confirmed (code) |
| `0x002548f0` | `AnimRange_ApplyClassDamage` | writes the character class's damages over the list ([Damage](#damage-table)) | confirmed (code), runtime |
| `0x00265f70` | `Human_ApplyPendingDamage` | each update: block, damage, armour, then the reaction by state | confirmed (code), runtime |
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

1. **Strength** = the code's, then -1 if the victim has flag `0x200`, or the attack is a combo id 13-20 and the victim
   lacks flag `0x400` and is not hurt (`0x00266b50`); +1 if the attacker has `0x200000` and the victim `0x400` or the
   attack is 13-20 (`0x00266c40`); 0 between allies when the victim's `+0x08` has `0x1b`; at most 1 if the victim has
   `0x80`; at most 3.
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

Every reaction seen fits (confirmed (runtime)): `S1` (`0x0a`) from in front gives 272 and from the victim's left
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
| 16 `SS2` | 19 `SSS3` or 20 at random (19 every time at runtime) | 17 `SSX3` |
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

### Grabbed, and breaking free {#grabbed}

**When the player is grabbed** (`Player_UpdateGrabbed`, `0x0027fd68`, the grabber at human `+0xc4`), confirmed
(code); no save state has a human that grabs the player, so none of it was seen:

- **Square** (`0xf` / `0x11`): a struggle strike, 96 or 108 (front or rear), when the player may struggle
  (`0x002258f0`: not hurt, the grabber not raging, own power above a sixth of the maximum) and the grabber's power
  fraction is above 1 / its power class byte `+0x36`. It takes 1 / (own byte `+0x36`) of the **grabber's** power, and
  the strike damages the grabber by its clip's damage; a strike that would kill the grabber breaks the grab.
- **Cross** (`0x12` / `0x10`): 104 / 116, a strike back.
- **Circle** (`0x1e`, `0xd`, `0xe`): an **escape** (`0x0026cc18`) unless the grabber holds (`0x00225830`): the
  threshold is a quarter of the grabber's maximum power (half if hurt); a raging grabber always holds; above the
  threshold it holds with chance 1 - threshold / power. The escape plays 100 / 112 (102 / 114) and knocks down and
  stuns the grabber.
- **R1 pressed** or `0x19`: a reversal (`0x0026d150`), 90 / 91 or 92 / 93.

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
civilian rose 2.0 s after the special's hit, the player (2750 ms) 2.78 s after an escape, with no input.

The victim can act again when its reaction clip ends (standing), its stun ends, or its rise ends.

### Power meter {#power-meter}

Record `+0x148`, maximum the power class's `+0x28` (400) through `0x00223068` (with upgrades). In
`Human_UpdateMeters` (`0x002562d0`) it **drains** by `CfgBurnRates`: 15 per second grabbing, 15 tackling, 2 in a
further grab state, 0 blocking; and **refills 60 per second** (`+0x2a`) otherwise. At 0 a grab ends
([Grabbed](#grabbed)). `Human_PowerFraction` (`0x00226510`) and `Human_SpendPower` (`0x00226448`); neither spends
nor drains while raging or with flag `0x4000000` (the fraction then reads 1.0). Confirmed (code), confirmed (runtime)
(400 → 377 over 1.4 s of a tackle; 25 → 0 at 15 per second in a grab).

### Rage {#rage}

Rage is human `+0x650`, up to the Warrior class's s16 `+0x00` (78 for class 6). `Human_AddRage` (`0x00264cf8`),
confirmed (code), adds, only for a player-flagged human (`0x2000000`) not already raging:

`round(points × f × gain / 100 × h × s)`, where `f` is 1.0 when the award is 25 points or fewer and 0.1 when it is
more (the whole award, not the excess: `CfgRagePoints` `0x005108d8`, table `0x005108e0`), `gain` the Warrior class
byte `+0x02` (144), `h` 0.5 when the per-player byte `+0x182` (record `0x0051489c + player × 0x5c`) is set, and `s`
that record's float `+0x17c` when `0x002239e0` holds. The meter is **capped** at the maximum; the first time it fills,
`0x00236ec8` announces it. Each gain sets the hold timer `+0x648` = now + 5000 ms (`CfgRageHandlers`). Confirmed
(code). Gains seen per hit at runtime ranged from 1 (`S1`) to 10 for most hits, with 13 and 15 (`SSX3`) for the
heaviest (confirmed (runtime)).

**The points** come from the stats system, which also adds them to the per-player score at `0x006fe490 + player ×
0xc0`: `0x002653d8` maps the attack to an event (`X1` event 1, `S1` event 2, ...; a blocked hit is halved through
`0x004ed9c8`), grab moves and throws map through `0x00264fa0`, and the event's value is count × a table entry
(`0x004ed988` → table 2 at `0x00715510`; `0x004ed8c8` → table 3 at `0x00715500`; `0x004ed948` → table 4 at
`0x00715818`). The tables are filled by `CfgSetStatValue`; at runtime: table 3 = 6, 4, 1, 7, 20, 3, 5, 5; table 2 =
75, 200, 150, 60, 150, 150, 100, 150, 100, 150, 100, 75, 30, then 0; table 4 = 50, 300, 5, 25, 250, 100, 300, 200,
100, 5, 50, 10. Confirmed (code) for the path, confirmed (runtime) for the values; the full event list per anim id is
not traced.

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

- **The player being hit, at runtime**: no save state has a human that attacks or grabs the player, so the player's
  reactions, hit armour, blocks and the grabbed struggle are confirmed (code) only.
- **Class 13**: which character class it is (it gets hit armour and adds 2 s to a knockdown).
- **The rage events**: the full map from anim id to stats event and table entry (`0x002653d8`, `0x00264fa0`).
- **Commands `0x30`-`0x39`**: which scripts or weapons make them; `0x36`-`0x38` and the d-pad (`0x27`).
- **Theft mode 2 at runtime**, and which objects use modes 1 and 2.
- **The grab's front circle** `0x0026f008`, the `0x00510980` table and the further grab state of `0x005101f0`.
- **Square at a sprint** at runtime, and the moving attacks' hit timing (the victim was out of reach in the tests).
- **The mugging's angle frame** (world or camera).
- **The fence break** in slot 10: which script reacts to the charge.
- **The class damage table**: which of `CfgChar`'s 45 damage entries overrides which anim id's damage (the jump tables
  `0x0055d640` / `0x0055d6f0`), and which class and difficulty give the street's values (`S1` 17 in play, 40 in
  Rembrandt's file).
- **The power strike's ids**: whether 57, 63 and 80 are anim ids (80 is `GRAB_REAR_SPIN_VICTIM`, with no damage in the
  file) or damage values; whether it spends power.
- **Attack timing** of the attacks other than `S1`, the moving attacks and the grab strikes.
- **The grab moves' commands**: whether cross strikes on `0x10` or `0x12`, circle throws on `0x1e` or `0xd`, and
  whether a strike or throw needs the power it costs.
