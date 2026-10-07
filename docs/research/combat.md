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

No source file names this code: it lies in `Human/`, in the stretch after `cns/cnsplayertag.cpp`
([Source map](source-map.md#position), inferred), and in `Human/` for the shared human calls. Names are ours.

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
| `0x002843f8` / `0x00284340` | `Player_UseItemCommand` / `Rage_Enter` | L1 + R1 with a full meter, the flash, the key when cuffed, the R2 map ([Rage](#rage)) | confirmed (code), runtime |
| `0x00284280` / `0x0022eb40` | `Flash_Use` / `Human_Heal` | spends a flash, its sound, the heal (full, or half on fury and rumble) | confirmed (code) |
| `0x0027a6c0` | `Player_PickTarget` | keeps or searches a target for an action | confirmed (code) |
| `0x00264178` | `Player_ObjectAttack` | strikes at a breakable object | confirmed (code), runtime |
| `0x002625a8` | `Attack_Start` | starts an attack clip, counts the combo | confirmed (code) |
| `0x0021b290` | `Strike_Contact` | computes a hit's damage | confirmed (code) |
| `0x002674c0` / `0x00230328` | `Human_OnHealthOut` / `Human_KnockOut` | health gone: wounded, knocked out or dead ([Defeat](#defeat)) | confirmed (code) |
| `0x004197a8` | `GameState_CheckGameOver` | the engine's mission failure ([Defeat](#defeat)) | confirmed (code) |
| `0x00169dd8` / `0x00169ea0` | `Gang_NoneAbleToHelp` / `Gang_CanReachToHelp` | whether the crew can still help a downed player | confirmed (code) |
| `0x0027a120` / `0x0027a070` / `0x00279f50` | `Target_ObjectFilter` / `Target_GlassFilter` / `Target_CarFilter` | the square's object targets ([Targets](#targets)) | confirmed (code) |
| `0x00396710` / `0x00393450` | `Object_IsStrikeTarget` / `WorldObject_TakeHit` | an object's strike-target bits; its hit points per strike | confirmed (code) |
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
| `0x00263c90` | `Player_SpecialAttack` | the special or strong grapple: target search, id by rage, variant and side, then a solo clip, a grab, a tackle or a paired move by the clip's flags ([Strong grapple](#strong-grapple)) | confirmed (code), runtime |
| `0x0027df38` | `Player_UpdatePowerMove` | in a power strike (state `0x1000`): square or cross in the window plays the next part, id + 2 | confirmed (code), runtime |
| `0x002878b8` | `LockPick_JudgePress` | mini-game mode 2, lock picking ([Crimes](crimes.md#lockpick)) | confirmed (code) |
| `0x0026c548` | `Grab_Start` | the grab's intro: 71 (or 70), then 69 and the idle; turns towards the target | confirmed (code) |
| `0x0026c1d8` | `Grab_IntroEnd` | end of the intro clip: grab from the front or rear, or a counter | confirmed (code) |
| `0x0026be68` | `Grab_Connect` | aligns the pair and starts 72 / 73 (74 / 75 from the rear), then the holds | confirmed (code) |
| `0x00276998` | `Pair_AlignStart` | range gate, then turns and slides the attacker so the victim stands at the clip's reach | confirmed (code) |
| `0x0026bad8` | `Grab_ConnectEnd` | end of 72 / 74: distance check, snap and attach in the hold | confirmed (code) |
| `0x00276d98` / `0x002802a0` | `Pair_SnapAttach` / `Pair_Attach` | puts the victim at the hold's offset and ties its movement to the grabber | confirmed (code) |
| `0x00244e78` / `0x00245310` | `Human_MoveAttached` / `Human_MoveGrabbing` | movement states of the held victim and of a grabbing player | confirmed (code) |
| `0x0026f008` / `0x0026ef68` | `Grab_StartMountFromFront` / `Grab_MountClipEnd` | circle in the front hold: 118 / 119, then the mount | confirmed (code), runtime |
| `0x0022bfd0` | `Pair_LinkMount` | the mount's states for both (also the tackle's and the grounded mount's) | confirmed (code) |
| `0x0026c7e0` | `Grab_Release` | both let go of a grab: 95 / 94 (107 / 106 from the rear); refused in a scene ([Breaking a pair](#pair-break)) | confirmed (code), runtime |
| `0x00258a88` | `Human_BreakPair` | ends any grab, mount, mugging or throw link from outside; only the other human plays a clip ([Breaking a pair](#pair-break)) | confirmed (code) |
| `0x0027ec20` | `Player_UpdateMounting` | strikes, power strike, back to the hold, get off, in the mount | confirmed (code), runtime |
| `0x00277958` | `Pair_CheckPlace` | a move in the hold needs the victim within 0.3 m of the move's offset | confirmed (code) |
| `0x0023cf88` / `0x0023d2b8` | `Human_TurnToOver` / `Human_MoveToOver` | turn to a heading, or move to a point, over a time | confirmed (code) |
| `0x002761c8` / `0x00276008` | `Attack_SteerToTarget` / `Attack_TurnToTarget` | an attack's turn and slide onto its target up to the clip's first event, or the turn alone ([Target selection](#targets)) | confirmed (code), runtime |
| `0x0023f5e0` | `Human_ApplyTurnAndSlide` | each step, applies a stored turn rate and slide velocity until their time runs out | confirmed (code) |

## Data

### Commands {#commands}

`AddCommand(command, trigger, buttons, extra)` (the script binding, [character bindings](../references/bindings/character.md#addcommand))
fills nine tables of 12-byte entries `{u16 mask, u32 command, u16 buttons, u16 extra}`; `global.lua` fills them for
the street, and level 84's chase adds command `0x2d` on a button of its own. Every binding is listed in
[Commands](../references/commands.md). The tables as read at runtime in the street (confirmed (runtime)); the
matchers are confirmed (code):

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

**Disabled commands** (`EnableCommand(player, id, 0)` clears the pad's bit in the entry's mask) are still matched:
the matcher stores them in the per-player record's pending `+0x24` instead of `+0x20`, so the human does not act on
them, but the `PadSetHandlerEx` handler, which reads `+0x20` and else `+0x24`, still receives them. Confirmed (code)
at `0x00147940` and `0x001480e0`; confirmed (runtime) for L1 (6) with pad 0's bit cleared
([PadSetHandlerEx](../references/bindings/input.md#padsethandlerex)).

### State flags {#state-flags}

The 0x180 record's `+0x00` flags ([Characters](characters.md#the-record)) that combat uses. Confirmed (code) where a
test is cited in the behaviour below; seen at runtime unless marked.

| Bit | Meaning |
| --- | --- |
| `0x1`, `0x2` | fight stance (`0x00228340` tests `0x3`); a player sets only `0x1`, scripts and AI set both ([Fight stance](#fight-stance)) |
| `0x4` | has a target (inferred: set with the target at a grab) |
| `0x10` / `0x20` | grabbed from the front / rear |
| `0x40` / `0x80` | grabbing from the front / rear (a front grab reads `0x45`) |
| `0x100` / `0x200` | mugging / being mugged |
| `0x400` | tackling, mounted on the target (seen `0x405`); the victim `0x800` (seen `0x801`) |
| `0x1000` | throwing (seen `0x1005`) |
| `0x8000` | blocking (with `0x1`: `0x8001`) |
| `0x1000000` | sprint asked for ([Sprint](characters.md#sprint)) |
| `0x2000000` | tagging ([Crimes](crimes.md#tagging)) |
| `0x4000000` | in a mini-game (lock picking, the stereo, uncuffing; [Crimes](crimes.md#mini-game-record)) |

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
| `0x20000000000` | one hit cannot take health below a fraction of the maximum (`0x00265f70`); it then sets `0x10` | |
| `0x100000000000` | never picked as a target (tested by `0x00227d78` in the filter `0x00279410`; not `HuSetNoAutoLock`, which sets `0x8000000000`, a bit with no reader found) | |

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
| square / cross in the mount | 219 or 221 / 223 `MOUNT_COMBO_STRIKE_*` | 61 (runtime) | 220, 222 / 224 |
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
| `0x0051024c` | 0.25 | the health floor of victim flag `0x20000000000`, as a fraction of the maximum; `HuSetDemiGodMode` overwrites it |
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
The **Warrior class** record (`CfgWarriorClass`, `0x006b65c0 + c × 14`; class 6 for the player here, human `+0x1ba`,
picked by type: [Power classes](characters.md#power-classes))
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
   "other clear" on [Characters](characters.md#sprint). R1 held is read from the pad record (`0x00147f98`, 0 when
   per-player `+0x19` is −1), so **an AI human never starts a block**: command 4 alone does nothing
   ([AI](ai.md#block)).
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
   pressed) → `0x0027da10`; 6 (L1 held) → `0x0027dc00`; 8 → `0x00227b98` / `0x00227a90` ([Targets](#targets));
   **3 → the AI's grab or tackle counter** (`0x0027d6e0`), only when per-player `+0x1b` is 0 ([AI](ai.md#block)).
   Commands `0x30`-`0x39` are not made by the street's tables; scripts or weapons make them (inferred). `0x00288838`
   acts only for armed weapon types; `0x30` matches no branch of `0x00287730` (inferred: unused there).

### Attacks and chains {#attacks}

**Square** (`Player_Square`, `0x00286cc8`) takes a target from `Player_PickTarget(0x40000000, h)`, then, confirmed
(code):

- a grounded target → 193 (`0x00261a08`); a tackled one → 212; a grabbed one → 120;
- a breakable object → `Player_ObjectAttack` ([Breakables](#breakables));
- at gait 4 (run) with record `+0x08` clear and the stick above 0.95 → 24, from a run; at gait 1-3 with the stick at
  0.12 or more → 23, from a walk. These come **before** the snap (gait tests `0x00223a30`-`0x00223a60`), but the
  whole block, run and walk alike (and the `0x00510270` attack 22), is skipped **in a fight stance** (record `+0x00`
  & `0x3`, `0x00228340`) or when `0x0051031c` is set for a pad-controlled human (0 in the executable's data).
  Confirmed (code) at `0x00286cc8`; cross (`0x00287a18`) skips its run and walk attacks on the same test. So a snap
  comes from a player in a [fight stance](#fight-stance) (a locked-on combat walk is gait 3, but the stance skips the
  walk attack) or from one standing (or sprinting) outside it: the stick pushed from rest and square pressed while he
  has not yet started to walk;
- the stick above 0.95 (the per-player record's buffered magnitude) and more than 45° from the facing → a **snap**
  attack, 25 right, 27 left, 29 back (`0x00264460`), but only **with a target there**: `0x0027aa38(h, 0x80)` takes
  the nearest human within **2.0 m** (`0x00227598`) and within **45° of the stick's direction** (π/4 written to
  `0x0051096c`), height difference at most 2.0 (`0x00510970`), the line to it clear (`0x00222a90`), of another gang,
  not down, not grabbed or tackled, targetable (filter `0x00279410`). With `CfgSnap` on (`0x005102b4` = 1, the
  default), none found, or the one found being the current target (`0x00226e60`), means no snap: square goes on
  down this list (usually `S1` at the current target). With one, `0x00264460` turns the player onto it over 0.1 s
  (`Attack_SteerToTarget`) when it is within the snap's far range. Confirmed (code); confirmed (runtime) in
  `level99`'s lesson 7 with the stick at full deflection one update before square: 27, 29 and 25 started and hit bums
  0.8-1.4 m away, and a square with the stick at the bum in front (the current target) gave 12;
- otherwise 12, `S1`. Strafe ids 31-33 come from the combat-walk path (not tested).

**Cross** (`0x00287a18`) starts 11, `X1`, on command `0x10`; at a run (gait 4, or 5, with record `+0x08` clear) it
plays the run attack 24 and at a walk the walk attack 23, as square does, but has no snaps. Confirmed (code), confirmed
(runtime). With a weapon in hand both buttons follow [Moving attacks with something in hand](#armed-moves).

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

At a run (gait 4, 7.8 m/s, stick 1.0) **square** or **cross** plays 24 for about 0.7 s and the run resumes. At a walk
either plays 23. Confirmed (runtime). Both ids are constants in the code, not clip slots; with a knife, baton or bat
in hand the run attack is 501 and there is no walk attack, and with a thrown or carried object both buttons throw
([Moving attacks with something in hand](#armed-moves)).

**The charge**: **L2 held, then cross pressed** (command `0x20`) at gait 4 with record `+0x08` clear, or at gait 5
(a sprint), plays 0 (`gen_charge_shoulder`) for about **0.9 s** at about 7.45 m/s. **The dive**: L2 held, then
square pressed (`0x21`), plays 1 for about **2 s**. Confirmed (code) at `0x0027d800` / `0x0027d900`, confirmed
(runtime). At a walk or standing the combination does nothing (the cross or square press under it is overwritten,
inferred from the matching order).

**Square at a sprint** has no attack of its own: the moving attacks test the gait exactly (4 → 24, 1-3 → 23), so at
gait 5 square falls through to the standing path (a fight stance and `S1`). Confirmed (code) at `0x00286cc8`; not
tried at runtime. With `0x00510270` set (0 here) square outside a fight stance plays 22 instead.

**Specials** (`Player_Special`, `0x00287730`), confirmed (code): **cross + square** (`0x22`) outside a grab plays the
special 653 (645 raging); **circle + cross** (`0x23`) is the **strong grapple**, a grab whose connecting clip is
the strike 657 (649 raging) and which ends in the hold ([Strong grapple](#strong-grapple)); **circle + triangle**
(`0x24`) sprays a tag (664) where the player may tag
(`0x00222818`). The id is 645 or 653 + 4 × variant + 0 / 2 by side (`0x00263c90`). A special that is not paired
needs and spends 0.25 of the power meter. At runtime (confirmed (runtime)): cross held, square pressed 40 ms later
played 653, power 400 → 300, the civilian lost 6 and fell (291, grounded 196, up with 199 2.0 s after the hit);
circle + cross at a victim getting up played 359 and then `X1`.

#### How a moving attack strikes {#moving-strikes}

A moving attack has no single hit update. Its clip's events switch **strike shapes** on and off, and while any is on
`Human_TestStrikes` (`0x0033f110`) tests them **every update, after the move**, against every body near the human:
the first shape that overlaps a body calls `Strike_Contact` (`0x0021b290`) for it. The reach is the posed body
itself: there is no reach or height figure of its own. Confirmed (code) at the addresses below; the timings are
confirmed (runtime).

**The shapes** (confirmed (code), `IPhysics_Construct` `0x0033c288` with the tables at `0x00512810`-`0x005128f0`).
Each of the 60 human bodies holds its capsule (type 3, radius 0.35 m, height 2.0 m, [Physics](physics.md)) and ten
**bone shapes**, identified by a bone index (shape `+0x31`). A **segment** (type 4, `0x003437b0`; posed by
`0x00343810`) runs from the bone's position plus the bone's rotation times the offset, along the bone's local x axis,
for its length, with its radius around it; a **sphere** (type 2, `0x00342aa0`) is centred at the bone's position plus
the rotated offset. Both are then carried into the world by the human's transform. The limbs are named from the
skeleton's [parent table](formats/animation.md#the-pose) (inferred); left and right follow the hand bones (25 right,
confirmed (runtime); 19 left, inferred), and which leg is which is not known.

| Id (bone) | Part | Shape | Offset in the bone's frame (m) | Radius (m) | Length (m) | Shape flags `+0x32` |
| --- | --- | --- | --- | --- | --- | --- |
| 3 | spine | segment | (−0.07, 0, 0.05) | 0.18 | 0.38 | `0x6` |
| 6 | head | sphere | (0.05, 0, 0.03) | 0.15 | | `0x6` |
| 18 | left forearm | segment | 0 | 0.07 | 0.20 | `0x2` |
| 19 | left hand | sphere | (0.08, 0, 0) | 0.09 | | `0x2` |
| 24 | right forearm | segment | 0 | 0.07 | 0.20 | `0x2` |
| 25 | right hand | sphere | (0.08, 0, 0) | 0.09 | | `0x2` |
| 29 | shin (bones 28-30) | segment | 0 | 0.10 | 0.40 | `0x2` |
| 30 | foot (bones 28-30) | sphere | 0 | 0.15 | | `0x2` |
| 32 | shin (bones 31-33) | segment | 0 | 0.10 | 0.40 | `0x2` |
| 33 | foot (bones 31-33) | sphere | 0 | 0.15 | | `0x2` |

The spine and the head carry `0x4`: they are the shapes a strike is tested **against** on a human target (the others
only strike). **Switching** (confirmed (code)): `PhysBody_SetShapeEnabled` (`0x003428d0`, through
`Human_SetStrikeShape` `0x0021c030` on human `+0x1a0`) keeps a bit per bone (`1 << (id − 2)`) in body `+0xc0` and
turns on the shape whose `+0x31` is the id (shape vtable `+0x1c`); an id with no shape sets its bit and switches
nothing. Turning id −1 off turns every shape off.

**The clip events** (confirmed (code) at `Anim_FireEvents` `0x00101dd8` and `Human_HandleMessage`; the events read
from the disc's clips). The 24-byte event's `+2` type picks the message and its words `+4` and `+6` are pushed as
arguments (the message's argument stack is last-in first-out, `Msg_PopArg` `0x003a8518`):

| Event type | Message | Words | What it does |
| --- | --- | --- | --- |
| `0xf` | `0x8f` | `+6` bone id, `+4` the window's length in frames | `Human_StrikeShapeOn` (`0x00247fc0`): that shape on |
| `0x10` | `0x90` | `+6` bone id | `Human_StrikeShapeOff` (`0x00248110`): that shape off, and the held object's contact list cleared |
| `0x13` | `0x93` | (`+4` ignored) | `Human_StrikeAllOn` (`0x00248170`): flag `0x2` on the capsule, then all ten on |
| `0x14` | `0x94` | | `Human_StrikeAllOff` (`0x00248270`): the capsule's `0x2` off, all off |

The disc's 1,752 distinct clips have 315 type-`0xf` events (by bone: 25 ×99, 19 ×52, 24 ×41, 18 ×28, 29 ×22,
30 ×20, 32 ×17, 33 ×15, 3 ×7, 6 ×5, and 23, 0, 4, 28, 31, which have no shape) and 52 clips with type `0x13`: the
charge and dive, the tackle miss, the body check, the counters, the throws' and power moves' victims, the
**extreme reactions** 296-299 (`gen_hit_react_low_*_ex`) and the building jump's loop. The wheelchair turns all on
above 4.5 m/s and off below (`0x002427e8`). A bone's window always ends with its own type-`0x10` event, and in the
rage moves the `+4` word equals the frames from on to off.

**With a melee object in the right hand**: a type-`0xf` event for bone 25 while the object in hand (`+0x338`) is a
human-held melee object (object flags `0x30000`) makes strike spheres on the **object's** body instead
(`Human_BuildWeaponStrikeSpheres`, `0x00247be8`): for an object without flag `0x20000` and not of `ObjectAttribs`
kind (`+0x86`) 39, a row of spheres of radius *r* = 0.1 m along the object's local y: the first at −`+0x70` + *r*,
the last at `+0x7c` / 2 plus the y of the model's bound (`0x003917d8`), and between them one every 3 *r* (the row
reaching 0.9 m further each way for kind 36; `+0x70`, `+0x7c` are attribs of the object type); for kind 39
one sphere of 0.8 m; for an object with `0x20000` one sphere of half its smallest extent (`+0x78`, `+0x7c`, `+0x80`;
`+0x78` alone when attribs `+0x84` is 2), at its origin. The event's `+4` word plus 1 goes to the object's body
`+0xd0`. Confirmed (code); the attribs fields' meanings are on [World objects](objects.md#held).

- **What it hits.** Humans and objects go through the same test. A human's body is tested against the shapes of the
  target that carry flag `0x4` (or its capsule, when `0x00101b88` says so for the attacker's clip); an object's
  against its own shapes; a shape touching the level mesh calls `Strike_Contact` with no object (a material impact
  sound only). `Strike_Contact` then does what a hit on that thing does: damage from the clip's
  [Anim Range List](#damage-table) value for a human, message 1 of kind 2 for an object
  ([World objects](objects.md#door-break)), and for a pane its break. A body struck is put on the striking body's
  contact list (body vtable `+0x34` / `+0x44`) and not struck again while the shapes stay on (inferred from the list's
  use; what empties it is `0x00342168`, run when the shapes go off). The same overlap also runs from the human's
  move: a sweep by a strike shape meets bodies with flag `0x20`, and `Human_OnContact` (`0x00219d50`) calls
  `Strike_Contact` once per body.
- **Which shapes, and when** (updates of 1/30 s from the clip's first update, slot 10, stick 100 % straight ahead):

  | Move | Shapes switched on | On | Off | Clip | Events (clip frames, of) |
  | --- | --- | --- | --- | --- | --- |
  | charge 0 (L2 + cross) | all ten bone shapes and the capsule's flag (`0x11` → `0x13`) | 3 | 16 (all off at once) | 27 updates, 7.45 m/s throughout in the open | `gen_charge_shoulder`: `0x13` at 3, `0x14` at 13, of 20 |
  | dive 1 (L2 + square) | the same ten | 1 | 23 | | `gen_dive`: `0x13` at 2, `0x14` at 18, of 46 |
  | run attack 24 (cross at gait 4) | right forearm and hand (24, 25) | 1 | 7 | 21 | `gen_run_strike`: on at 2, off at 6, of 16 |
  | walk attack 23 | left forearm and hand (18, 19) | 4 | 10 | 24 | `gen_walk_strike`: on at 4, off at 8, of 18 |
  | run attack with a weapon 501 | the weapon's spheres (bone 25's event) | | | | `gen_run_1hand_weapon_atk`: on at 2, off at 6, of 16 |

  The clips play slower than 30 frames a second (the charge's 20 frames over 27 updates), which turns the events'
  frames into the updates measured. So the **charge strikes with its whole body from its 4th update to its 16th**,
  about 13 × 0.25 m = 3.2 m of its 6.7 m; the run attack strikes only for 6 updates near its start.
- **The fence (slot 10).** Rembrandt starts 4.8 m from `level99`'s wooden fence (centre line y −6.67) with the
  stick at 100 % straight at it and L2 held; cross at gait 4 started the charge (clip 0, record `+0x08`
  `0x1000000`). On its 10th update, his root 0.80 m from the fence's line, `Strike_Contact` came from the strike test
  for the fence; the fence took the hit with 10 hitpoints and broke (kind 2: 16). The dive broke it the same way on
  its 7th update, 1.05 m from the line (record `+0x08` `0x400000`). The walk attack 23, pressed while walking against
  the fence, also broke it (its record `+0x08` holds `0x1000000` too, so kind 2). The run attack 24 did not: its
  shapes were off 1.1 m short, and he ran into the fence and slid along it at 1.07 m/s.
- **The strike does not end the charge.** The shapes stay on and the clip runs its 27 updates whether or not anything
  was struck. What stopped him at the fence was the collision: 2 updates after the strike he stopped dead 0.42 m from
  the fence's line, with no slide, and did not move again before the clip ended (11 updates later). Why, from the code:
  the break turns the fence's level triangles off but leaves the hidden fence's **collision body** until the object is
  removed on its next 60-tick update ([Barriers](objects.md#barriers); 11 updates after the hit in this run). The
  charge's sweep meets that body (the move's mask holds `0x4` on the ground, `0x0033d498`), and `Human_OnContact`
  (`0x00219d50`) answers a world object that is not a `powerup_item` with `0x20001`: the slide response of the sweep
  (`0x0033d9d8`, type 1), which removes the part of the velocity going into the contact's normal (`0x0033d870`) from
  both velocities the sweep is given (this step's and, inferred, the one kept). Head-on, as here, nothing is left, so he
  stops dead; the run attack, which met the fence at an angle, kept its sideways part and slid. Inferred from the code
  (the response types are confirmed (code); that the fence's body has `0x4` follows from the stop). Struck first in the
  same contact (strike shapes on and the body flagged `0x20`), the fence is not let through: only a body without `0x4`
  gets `0x20000` (ignored) after a strike, as a glass pane's box does. The last updates of the clip after the removal
  were not compared with the clip's own root motion. In the open the same input ran the whole clip at 7.45 m/s and the
  run (410) followed.
- **A knock-back is not airborne.** A knockdown reaction (`Human_PlayReaction`, `0x0026a6d0`) holds record `+0x08`
  `0x400000` (and `0x2000` for its follow-up clip 198) and never sets the object's airborne flag `0x4000000` (none of
  its callees does), so the flying human's sweep keeps the ground mask: a glass pane's box is met only through strike
  shapes. The **extreme reactions** 296-299 switch all ten shapes on (`gen_hit_react_low_front_ex`: `0x13` at frame 1,
  `0x14` at 16, of 20; `_back_ex` 2 to 11 of 32), so a human flung by one strikes what his body sweeps through,
  panes included; the ordinary knockdowns do not. Against a wall in the level mesh a human holding `0x400000` or
  `0x800` makes one impact sound (`Human_OnContact` → `0x00220ac8` with the triangle's material, volume 1.0 with
  `0x400000`, else 0.5; once, `+0x3bd` cleared). Confirmed (code); not seen at runtime.
- **Record `+0x08` bit `0x800`** is read in three places: the move's sweep mask (`0x0033d498`: with it, as when
  airborne, the sweep also meets `0x40` bodies), `Human_OnContact` (it counts as airborne for a `0x40` body, a pane's
  box) and the wall-impact sound above. **Nothing in the executable sets it**: no held-flag setter (`0x00226640`, the
  clip tasks' `+0x24`, `Anim_FireEvents`' event bits) is given `0x800`, and the only immediates `0x800` in `Human/`
  are state-word and human-flag bits (inferred from a search of every `li`/`ori` of `0x800`; a value read from data
  was not ruled out). So it behaves as a dead "airborne" override.
- **After the break** the script takes over ([Barriers](objects.md#barriers)): the fence's message 2 reaches
  `P3.FenceBroken`, and 15 updates later the player stood at (18.5, −16.3), heading 178°, beyond the fence. From the
  strike to that placement took 26 updates in the charge run, 33 in the walk-attack run.

Confirmed (runtime), PCSX2 2.9.94, copies of slot 10, scenario
[`charge_fence`](repo:research/traces/scenarios/charge_fence.toml) (hooks `strike-shape`, `strike-contact`,
`barrier-hit` and `object-remove`); the open-ground run turned the stick to 100 % straight back.

### When input and the stick come back {#input-return}

**At runtime** (confirmed (runtime)), PCSX2 2.9.94, a copy of slot 1 (the street, Rembrandt, nobody within 14 m),
read every update. Presses were scripted pad input; the left stick was held at **60 % up** (raw 38), which walks.
Updates are counted from the clip's first update (k = 0), and a press counts from the update its command reached
the per-player record.

| Move | Clips, updates | Presses during it | First press taken again | The stick moves him again |
| --- | --- | --- | --- | --- |
| `S1` (12) | 20: `0x1` 0-5, `0x2` 6-14, `0x4` 15, `0x40000` 16-19 | buffered in `0x1` / `0x2` (the chain), dropped in `0x4` / `0x40000` | **k = 20**: a new `S1` that update (presses at k = 16 and 18 dropped) | k = 20, 413 walk start at 0.9 m/s |
| `X1` (11) | 30: `0x1` 0-9, `0x2` 10-19, `0x4` 20, `0x40000` 21-29 | a cross in the window plays `XX2` that update (k = 19) | | k = 30 (413) |
| `XX2` (13) after `X1` | 30: `0x1` 0-16, `0x4` 17-18, `0x40000` 19-29 | all dropped (the chain ends) | not reached (the presses stopped at k = 21) | |
| `S1` → `SS2` → `SSS3`, square every 2 updates | `S1` 6 (cut when its window opens), `SS2` 6, `SSS3` (20) **32**: `0x1` 0-23, `0x4` 24, `0x40000` 25-31 | a press in each wind-up is buffered (`+0xb8` = 2) and plays when the window opens; all dropped in `SSS3` | k = 32 of `SSS3`: a new `S1` | k = 32 of `SSS3` (413) |
| run attack (24), stick 100 % | 21, `+0x08` `0x1000000` throughout, 7.09 m/s | all dropped | k = 22: another 24 | k = 21: the run (410) at once, at 7.80 m/s |
| block (606), R1 released | `0x8001`, `+0x08` `0x80000` for its first 5 updates, then 0 | | the update after the release: square gave `S1` at once | **5 updates** after the release: the idle 388 for 5 (its fade holding `0x10000000`; `+0x14` = 5, turning in place), then 413 |
| grab miss: 71, then 69 `GRAB_MISS` | 71 5, 69 16; state `0x1`, `+0x08` `0x10`; the lunge peaks at 4.3 m/s on 69's first updates | circle every 3 updates: all dropped, no restart and no cut | **k = 21**, the update after 69: a new 71 | k = 21 (413) |
| tackle miss: circle held | 4 `TACKLE_PLAYER_INTRO` 7, then 2 for **59** (sliding at 6.4 m/s, to 0 over about 20); `+0x08` `0x10` | circle taps all dropped | k = 66 | k = 66 (413) |

Without the stick each of these ends in **389** (state 0, `+0x08` `0x40000000`) for 26 updates, then the idle 388.
389 does not hold input: a circle during it started 71 the next update, and with the stick held the walk start
replaces it at once (389 never played). Holding circle after a grab miss does nothing: `0xe` comes once, on the
7th held update, and was lost inside 69. The walk start (413) reaches the walk (408, 1.56 m/s) 13 updates later.

So, apart from the block, **a press and the stick both come back on the first update after the clip ends**, and
the only presses taken earlier are the chain's.

**The gates** (confirmed (code)):

- The dispatcher (`0x0027c120`) returns before the chain and every command while record `+0x08` has any bit of
  **`0x5c7fee0`**. That covers the recovery `0x40000` and the run attack's `0x1000000`, but not the phases
  `0x1`, `0x2`, `0x4`, the grab bit `0x10` or 389's `0x40000000`.
- The chain (`0x00280630`) takes the press while `+0x08` has any of `0x7` ([Attacks](#attacks)). When it refuses,
  the command falls through to its path.
- Square and cross (`0x00286cc8`, `0x00287a18`) refuse while `+0x08` has any of **`0x100101f`** (`0x1`, `0x2`,
  `0x4`, `0x8`, `0x10`, `0x1000`, `0x1000000`) or the state word any of `0x7bf9e9f4300`.
- The grab or tackle (`0x00284920`) refuses while `+0x08` has any of `0xfc7eaf7`, which includes `0x10`.

So a press in `0x4` is dropped by its path, and one in `0x40000` by the dispatcher. During a grab or tackle miss
(`0x10`) every path refuses. Nothing refuses 389. What holds the stick until the clip's end is the **locomotion gate**
([Tasks](tasks.md#locomotion-gate)): the recovery `0x40000` is in its velocity mask `0x110c0880`, the phases `0x1`,
`0x2`, `0x4` and the grab's `0x10` make the human busy (`0x00223cb0`), and the clip's task clears them as it ends
([Tasks](tasks.md#held-flags)). The block's extra 5 updates are the idle's 0.15 s fade, which holds `0x10000000` (in
the velocity mask) while it runs; the state code `+0x14` = 5 written then only means turning in place
([Tasks](tasks.md#locomotion-gate)).

### Block (and no dodge) {#block}

**R1 held** on a pad, in a fight stance, blocks (an AI human cannot block, [AI](ai.md#block)): anim state 21 then 606
`BLOCK_SUSTAIN` (605 `BLOCK_START` was not seen as the playing id), state flags `0x8001`. The stick at 0.8 to a side
while blocking gives anim state 24, 607 `BLOCK_SHUFFLE`, turning in place. Cross pressed while blocking plays `X1` and
returns to the block. Confirmed (runtime). No separate dodge command exists in the street's tables (confirmed
(runtime)); the shuffle is the only evasive move found.

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
  asks its reaction goal or top goal (`0x0028c6a8`, goal types `0x17`, `0x1b` and `0x85`). Yes sets record `+0x14` =
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
duck's frames 6-13. Confirmed (code) at the cited addresses.

**The counter at runtime** (confirmed (runtime), PCSX2, a copy of slot 6: Rembrandt holding R1 against a puppet
civilian doing `SSX`, square or cross tapped for two updates at a chosen update; k counts updates from the first
update of 616):

- 616 played for 28 updates (0.93 s for the 0.7 s clip, so it runs at about 0.75 speed; inferred from the count).
- A square whose command `0xf` first reached the per-player record at **k = 7 to 17** started **617
  `BLOCK_COUNTER_FRONT`** that update or the next. At k = 6 or k = 18 nothing happened and 616 ran out. Frames 6-13
  at 0.75 speed fall on k ≈ 8-17, which fits.
- Cross (`0x12`) at k = 9 did the same. So did square with R1 already released at k = 2: the block flag `0x8000`
  was gone (state word `0x1`) and the counter still came, because the window needs only the duck's flag `0x1000`.
- 617 lasted 27 updates. On its first update the state word went from `0x8001` to `0x1` and record `+0x08` from
  `0x1000` to `0x2000`. It hit the civilian once, for 50 damage, on its eighth update; the civilian played 288.
  The block (606) came back the update after 617 ended while R1 was held.

**An AI human's answer** (`0x0028c6a8`, confirmed (code)):

- When the brain's `+0x3c` holds a reaction goal of type `0x17`, the knocked-down goal, the answer is no
  (`0x002b4aa0` returns 0). (An earlier reading called `+0x3c` a tactic; it is the reaction goal,
  [AI](ai.md#reaction-goals).)
- Otherwise the brain's active entry (`brain + 0x40` indexed by `brain + 0x2c`) answers:
    - Type `0x1b` (`0x002b5698`), the block goal built by `0x002b54d8` and pushed by `0x0029f098` with a random
      block chance (a quarter of it for brain type 3). It says yes when the human is ducking (record `+0x08` has
      `0x1000`, `0x00223b28`) and the goal's bytes `+0x14` and `+0x16` are both 1. `+0x16` is 1 from construction;
      `+0x14` is written by the goal's Start (`0x002b5520`): 1 when a second roll under the block chance succeeds
      ([AI](ai.md#block)).
    - Type `0x85`, `Goal_BigFighter` (`0x002eab50`, vtable `0x00542940`, built by `0x002e9cd0`). It says yes when
      the human's body scale (`+0x65c`) is at most 1.1, it is ducking, and the goal's u16 `+0x28` is `0x0101`.
- Yes writes command `0x10` into the human's per-player record (`0x00147ef0`). From there the counter takes the
  player's path.

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
  `0x405` / `0x801`; after 1.4 s the player sat in 210 `MOUNTING_IDLE` on the victim's 207 `MOUNTED_IDLE`
  ([The mount](#mount)).

### In the grab {#grabbing}

`Player_UpdateGrabbing` (`0x0027f3b0`), confirmed (code); runtime where marked:

| Input | Effect |
| --- | --- |
| square | 51 or 53 at random (victim 52 / 54), 57 damage; confirmed (runtime) |
| cross | 55, 57 damage; confirmed (runtime) |
| hold cross, press square (`0x22`) | the **power strike**: anim id 57 (anim 63 in rage, which also deals anim 231's damage at once); from the rear, anim 80 first spins the victim to the front; confirmed (runtime) for 57 |
| circle + cross (`0x23`) | anim 63, with the same rear spin, **for an AI grabber only** (human `+0x1b0` = -1); a player's grab has no `0x23` branch |
| square or cross during the power strike's window (`0x2`) | the **extended power move**: the next part, id + 2 (57 → 59, 63 → 65), up to two extensions (`Player_UpdatePowerMove`, `0x0027df38`, record `+0xbc` < 2; 61 is a filler clip); confirmed (runtime) for 59 |
| triangle (`0xa`) | **mug** the victim if it qualifies (`0x00225ff0`); confirmed (runtime) |
| circle (`0x1e`, `0xd` or `0xe`) with the stick above 0.25 | **throw** ([Throws](#throws)); confirmed (runtime) |
| circle without the stick | from the front the **mount** ([The mount](#mount)); from the rear the spin to the front; confirmed (runtime) |
| R1 pressed (3) or `0x19` | **spin**: front → rear 78 / 79 (`0x0026d570`), rear → front 80 / 81 (`0x0026d998`); confirmed (runtime): 78 / 79, then the rear hold 84 / 85, states `0x85` / `0x20` |
| L2 held (5) | **lets go**: player 95, victim 94; confirmed (runtime) |

The commands are those of the [table](#commands): square acts on its press (`0xf`), cross on its release (`0x10`),
circle on its press (`0x1e`). A grab strike only **spends** power, at any meter level: 0.2 of the maximum, halved for
a human with a player number (`+0x1b0`), so **40 of 400**. The power strike, `0x23` and the throws **need** more than
0.25 of the maximum and **spend** 0.25 (**100 of 400**); with 0.25 or less the grab is released instead
(`0x0026c7e0`). Confirmed (code) at `0x0027f3b0` and `0x00262ac8`; confirmed (runtime): strikes 40 each, the power
strike 236 → 135 with its 57 damage on the same update. The grab also ends when the victim drifts beyond the larger of
reach + 0.2 m and reach × 1.2, or 0.2 m up or down.

**The power strike at runtime** (confirmed (runtime), slot 6 copy, the civilian grabbed with circle; cross pressed,
square 2 updates later, both released 4 updates after; then cross taps every 8 updates; scenario as
`strong_grapple` with other input):

- From the **front hold**: 57 / 58 for 44 updates, 57 damage and 100 power on its first update. Cross presses in its
  wind-up (`+0x08` `0x1`, 9 and 17 updates in) were dropped; the window `0x2` opened about 19 updates in, and the
  press 25 updates in started **59 / 60** on that update, 79 damage, no further power. 59 ran 92 updates, then
  389; a third press did nothing. Without the extension, 57 ends the grab: the victim falls (196) and gets up.
- From the **rear hold**: the spin 80 / 81 plays first and the damage lands on the spin's first update, so the hit is
  scored with id **80**; 57 follows without a hit of its own; the extension 59 is scored as 59. The rear path spends
  power twice (389 → 188; `0x00263380` spends 0.25, then again for the move's damage).
- The tutorial callback got 57 and 59 from the front, 80 and 59 from the rear.

### Strong grapple {#strong-grapple}

**Circle + cross** together (command `0x23`, combination held: on the update the second of the two goes down) outside
a grab is the strong grapple: a grab that connects with a strike instead of the plain grab clip, deals its damage as
the pair reaches the hold, and leaves the player holding the victim. Confirmed (code) at the addresses cited;
confirmed (runtime) where marked.

1. **Gates** (`Player_Special`, `0x00287730`): nothing while record `+0x08` has any of `0xaeebf7ff`, or while a
   player holds an object of class 8 (`0x00224060`); then, command `0x23` → `Player_SpecialAttack(h, 1)`
   (`0x00263c90`). Command `0x22` calls it with 0.
2. **Target**: a player always searches afresh (`0x0027a4b0`) and makes the result its target (`0x00226cd0`). The
   search takes every human within the **far range of anim id 1** (`AttackTable_GetFarRange(h, 1)`, `0x00254508`:
   the Anim Range List's far value, or its reach × 1.25 when that is larger) and keeps those within **0.9425 rad
   (54°)** of the stick's direction (the facing when the stick is under 0.01), at most 2.0 m above or below, of
   another gang and not down (filter `0x00279568`); the nearest wins (`0x003868d0`), and with none the last target
   is kept when still valid. The function passes its variant (1) where the search expects an anim id, so the range
   is the dive's (id 1; `0x22` gets the charge's, id 0); inferred to be unintended, but it is what plays. Rembrandt's
   id 1 has reach 2.218 m and far 3.000 m, so **3.0 m** (confirmed (runtime)).
3. **Id**: 657 (`ANIM_SPECIAL_ATTACK2_FRONT`), or 649 (`ANIM_RAGE_ATTACK2_FRONT`) while raging (human `+0xe0`
   `0x80000`), + 2 when the player stands on the target's side 2, its back (`0x002672d0`): 657 / 659, 649 / 651.
4. **Path by the clip's flags** (the clip of that id in the player's set, its `+0x44`): without bit `0x1` (paired)
   the clip plays alone (`0x00262368`) if the power meter holds at least 0.25 of its maximum. Paired, it needs a
   target that may be grabbed (`0x00225520`: not down or dead, not in a scene, within 0.25 m in height, not in an
   excluded state) and a clear line to it (`0x0021c0a8`), then: bit `0x40` → **`Grab_Start(h, 0, front id)`**
   (`0x0026c548`); else bit `0x20` → a tackle `0x002707a8(h, 70, front id)`; else `Attack_StartPaired(h, target, id,
   1, 0x400000)`. Human flags `0x80000000` and `0x100000000` skip the grab and the tackle. For Rembrandt the grab
   path is taken (confirmed (runtime): `Grab_Start` called from `0x00263dd8` with base 657).
5. **The grab** runs as [Posing a grab](#grab-posing) describes with 657 as the base id: the intro 71, then
   `Grab_IntroEnd` picks front or rear, and `Grab_Connect` plays **657 on the player and 658 from the player's set on
   the victim** (659 / 660 from the rear) where a plain grab plays 72 / 73; the hold 82 / 83 (84 / 85) follows. The
   alignment gate is 657's far range (2.5 m) × 1.25 = 3.125 m.
6. **Damage at the connect's end** (`Grab_ConnectEnd`, `0x0026bad8`): with the player grabbing (`0xc0`), the victim
   grabbed (`0x30`) and the player's power above 0, the victim gets pending damage of the [damage table](#damage-table)
   value of the player's id at that moment (657: index 17) through `0x0021d680`. The same code runs at the end of a
   plain grab's 72, whose value is 0, so a plain grab deals nothing and scores no hit (confirmed (runtime): no
   scoring call). Then the snap and attach, and the grab's scoring (`0x00264fa0`: 657, 659, 649 and 651 score as
   category 1, 3).
7. **No power cost**: the paired path neither checks nor spends power; the hold's drain of 15 per second
   ([constants](#constants)) starts with the grab, as for any grab (confirmed (runtime): 72 of 400 was enough, and the
   meter fell 1 every 2 updates from the press).

**At runtime** (confirmed (runtime), PCSX2, a copy of slot 6: the civilian `PoizoCiv` held in place facing the player
at 1.0-3.5 m until the intro ended, circle and cross pressed on the same update and held for 4 updates, no stick;
scenario `strong_grapple` and hooks of [`patches.toml`](repo:research/traces/patches.toml)):

| Case | Player | Victim | Damage | Tutorial callback |
| --- | --- | --- | --- | --- |
| front, 1.0 m | 71 for 5 updates, 657 for 34, then the hold 82 | its walk 408 (held in place), 658, then 83 | 600 → 540, on the hold's first update | **82** |
| rear (victim facing away), 1.0 m | 71 for 5, 659 for 38, then 84 | 660, then 85 | 60 | **84** |
| front, 2.4 and 2.9 m at the press | the same as at 1.0 m (the intro slides the player in) | | 60 | 82 |
| front, 3.4 and 3.9 m | no grab; the cross's release played `X1` | | | |
| plain grab (circle tapped), 1.0 m | 71, 72 for 19, 82 | 73, 83 | none | not called |

The player's state word was `0x45` front (`0x85` rear) from the connect, the victim's `0x10` (`0x20`), then `0x11`
(`0x21`) in the hold; the hold lasted until the end of the 150-update run, so the strong grapple **does not let go**:
every [move in the grab](#grabbing) follows. 60 is Rembrandt's index-17 value after his 115 % scale.

**The tutorial callback** ([HUD](hud.md#tutorial-callback)) is called once, by the victim's
`Human_ApplyPendingDamage` on the update the hold replaces 657, so the anim id it passes is the **hold's, 82 (84 from
the rear), not 657** (confirmed (runtime): `Tutorial_CallCallback` given `0x52` and `0x54`). `level99`'s lesson waits
for exactly 82 or 84 ([The combat tutorial](scripting.md#level99-lessons)).

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
     more, or above 50 m/s. The human's state update applies them each step (`0x0023f5e0`, [Target selection](#targets)).
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

### The mount {#mount}

**From the front hold** (`Grab_StartMountFromFront`, `0x0026f008`), confirmed (code) and (runtime, slot 6, the
puppet civilian 1 m ahead, stick centred):

- `Player_UpdateGrabbing` calls it on circle (`0x1e`, `0xd` or `0xe`) with the stick at 0.25 or less, when the player
  grabs from the front (state `0x40`) and human `+0xe0` lacks `0x100000000` (not traced). There is **no power check
  and no cost**; the grab's drain of 15 per second goes on (386 → 354 over 118).
- The **press** (`0x1e`) starts it on the same update. The tap's `0xd` and a hold's `0xe` arrive while 118 holds
  `+0x08` `0x2000` and are dropped, so a tap and a hold of any length both mount once.
- `Pair_LinkMount` (`0x0022bfd0`): states grabber `0x45` → `0x405`, victim `0x10` → `0x801` (`0xc0` / `0x30` cleared,
  `0x400` / `0x800` set), movement styles `0xc` / `0xb`, the victim's brain targets the grabber. The tackle's connect
  (`0x00270270`) and the grounded mount (`0x00271b30`) call it too.
- Grabber 118 `GRAB_MOUNT`, victim 119 from the grabber's set (a paired type 6 task), both holding `+0x08` `0x2000`,
  0.1 s blends. **118 lasts 65 updates (2.17 s)**; at its end `Grab_MountClipEnd` (`0x0026ef68`) puts the mounter at
  clip 210's offset from the victim over 0.1 s (`0x00277248`), and the idles become 210 / 207 with the victim 0.10 m
  away (0.12 m after a strike). A victim with human flag `0x20000` is got off at once (`0x00271470`).

**In the mount** (`Player_UpdateMounting`, `0x0027ec20`, state `0x400`), confirmed (code); confirmed (runtime) for
square, cross, circle and L2, **the same after a tackle or a grab**:

| Input | Effect |
| --- | --- |
| square (`0xf`) | 219 or 221 at random (record `+0x68` + 0 or 2; victim 220 / 222), 61 damage; spends 40 (0.2, halved for a player) |
| cross (`0x10`) | 223 (record `+0x6c`; victim 224), 61 damage; spends 40 |
| cross held, square (`0x22`) | the power strike 225 (231 raging) through `Attack_StartPaired`; needs more than 0.25 power, otherwise gets off |
| circle (`0x1e`, `0xd`, `0xe`) | back to the front hold: 248 / 249 (36 updates), then 82 / 83 (`0x00271808`) |
| L2 (5) | gets off: 244 / 245, the victim rises with 199 (`0x00271098`) |
| triangle | mug ([Mugging](#mugging)) |

The mount drains power at the tackle's 15 per second ([constants](#constants)). The strikes ran 16, 19 and 28
updates back to 210. Level 99's tutorial follows this through two callbacks: `AddAnimCallback(player, 210,
"PlayerState")` shows the mount's prompt when 210 starts, and `HUDSetTutorialCallback("P1.BasicAttacks")`, which
receives each hit's anim id, wants 219 or 221 and then 223 before it calls `P1.BasicAttacksDone`.

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
  `GRAB_MOUNT` / 119, then 210 `MOUNTING_IDLE` on the player's 207 `MOUNTED_IDLE` (confirmed (runtime);
  [The mount](#mount)).

`CfgButtonMash` plays no part here: its only reader is the theft game ([Stereo theft](#stereo-theft)).

**When the player's victim breaks free**: the grab drains the grabber's power (15 per second) and
`Human_UpdateMeters` (`0x002562d0`) ends it when the meter reaches 0. If the victim has flag `0x20000` or `0x4000`, or
the grabber is hurt (and the victim lacks `0x2000000000`), the victim **escapes** through `0x0026cc18`; otherwise both
let go (95 / 94). A tackle dismounts (`0x00271470`). Confirmed (code); confirmed (runtime): from 25 power the grab
drained at 15 per second and broke with 95 / 94; with the player hurt (200 of 900 health) the civilian escaped with 100,
the player played 101, was knocked down and stunned (`0x180005`), lay in 196 and rose with 199 2.78 s later; the
escapee took 20. A third human hitting a grabber costs it 0.6 of its power (`0x00510274`).

### Breaking a pair from outside {#pair-break}

Two routines end a grab. Confirmed (code) unless marked.

**`Grab_Release` (`0x0026c7e0`)** is called only by the grab itself: the power running out (`Human_UpdateMeters`),
a grab strike without the power for it (`Player_UpdateGrabbing`) and a connect that misses (`Grab_ConnectEnd`). It
does nothing when the grabber's `+0x08` has `0x800000` or `0x2000`, or the victim's has `0x400000` or `0x2000`.
Otherwise it unlinks the pair (states `0xc0` / `0x30` cleared, the `+0xc4` handles emptied, the victim's attached
movement ended) and plays **95 `GRAB_FRONT_BREAK_REACT` on the grabber and 94 `GRAB_FRONT_BREAK` on the victim**
(from the rear, grabber state `0x80`: **107 / 106**), each blended in over 0.2 s, and sets `+0x08` `0x2000` on both.
Confirmed (runtime) for 95 / 94 ([Grabbed](#grabbed)).

**`Human_BreakPair` (`0x00258a88`)** ends whatever link a human `h` is in, for callers outside the grab code: **every
`Human_SetTransform`** (`0x0023d440`, the human's set-transform slot `+0x6c`: `Teleport`, `TeleportToFlag`, a
scene's place events 21 / 22 (messages `0x95` / `0x96`) and a skipped scene's end placement), message `0x24` to a
human, `Human_StartBurning`, `GameState_SyncPlayers` and six callers not traced. `h` itself plays **no** clip: its
link bits are cleared (state `0x1` set), its link handle emptied and its attached or grabbing movement ended, so it
is free at once and keeps the clip it was playing. **The other human** (`h`'s `+0xc4`) is unlinked the same way and
given a clip, by `h`'s state:

| `h` is | The other human plays | `+0x08` set on it |
| --- | --- | --- |
| grabbed from the front (`0x10`) | 138 `GRAB_FRONT_HIT_VICTIM_KNOCKDOWN_REACT` (`0x00269250`) | `0x40000000` |
| grabbed from the rear (`0x20`) | 106 `GRAB_REAR_BREAK` (`0x002690f0`) | `0x40000000` |
| grabbing from the front (`0x40`) | 145 `GRAB_FRONT_HIT_GRABBER_KNOCKDOWN_REACT` (`0x002693b0`) | `0x40000000` |
| grabbing from the rear (`0x80`) | 107 `GRAB_REAR_BREAK_REACT` (`0x00269510`) | `0x40000000` |
| mugging (`0x100`) / being mugged (`0x200`) | as for a rear grab: 107 / 106 (the mugging is first turned into a rear grab, `0x0022ceb8`) | `0x40000000` |
| mounting (`0x400`, or the states `0x8000000000` / `0x10000000000`) | 245 `MOUNT_RELEASE_REACT`, then 199 `GROUNDED_RISE` (`0x00269908`): it gets up | `0x8000` |
| mounted (`0x800`) | 244 `MOUNT_RELEASE` (`0x002697a8`) | `0x8000` |
| throwing (`0x1000`) or `0x2000` | no clip; the throw link is cleared (`0x0022bce0`, `0x0022bdc0`) and the other human gets state `0x20000000` | |

The clips blend in over 0.2 s; 244 and 199 end in `0x00267870` (not traced). That 245 plays before 199 is inferred
from the task chain (the dismount's order, [The mount](#mount)). A human with no link is left alone. So a teleport
never leaves a partner hanging: the human not named stands free after its reaction clip (or gets up from the mount),
and the named one is free at once. `Grab_Release` does not run, so 94 / 95 never play from a teleport.

**Brains are not links.** `BrDead`, `BrSuspend` and `Brain_ClearActions` only clear the brain's queued actions and
set its flags or handlers (`0x00292330`, `0x002923a0`); none touches the human's state, so a grab in progress goes on.
Inferred: its power keeps draining (15 per second in a grab or a mount, [constants](#constants)) until `Grab_Release`
ends it with 95 / 94, as for any grab whose power runs out ([Grabbed](#grabbed)). There is no `HuStop` binding in the
game.

### Throws {#throws}

`Player_Throw` (`0x0026dd08`) compares the camera-turned stick with the player's facing: within 45° → 147
`THROW_01_FROM_GRAB_FRONT`, beyond 135° → 151 rear, otherwise 149 right or 153 left. A wall within reach of the
throw gives the `THROW_02` set (155 front, 264 damage). A throw costs the power fraction 0.25 (**100 of 400**). The
victim plays 152 and lands in 196 `GROUNDED_IDLE`, where square plays the grounded strikes. Confirmed (code), and
confirmed (runtime) for 147, 151, 155 and the damage.

### A bat in hand {#bat}

**At runtime** (confirmed (runtime), slot 6 copy, `Human_PlaceItemInHand(player, "dyn_bat_tuff")` (`0x00238540`)
called on the game's thread, the civilian 1.3 m ahead):

| Input | Player | Victim | Damage | Tutorial callback |
| --- | --- | --- | --- | --- |
| square | 34 `BAT_COMBO_S1` | 294, then down (198 / 196) | 64 | 34 |
| cross (on its release) | 36 `BAT_COMBO_X1` | 292, then down | 64 | 36 |
| square or cross at a grounded victim | 37 `BAT_COMBO_GROUNDED_STRIKE_01` | | none here (the victim was getting up) | |

So every standing bat hit knocks the victim down, and the next one must wait for it to stand.

**Picking it up.** A bat (`dyn_bat_tuff`, class `melee_weapon`, `TYPE_BAT` 3) is [pickable](objects.md#pickable), so
triangle's pick-up search takes it ([Breakables](#breakables) has the search; a script's prompt on it comes first,
[Crimes: triangle](crimes.md#triangle)). Confirmed (code) at `0x0024d810`, `0x003fead0`, `0x0023bf00`:

1. A type other than 12 (`TYPE_SPECIAL`) or 24 is taken by a player only with empty hands (human `+0x338` Nil). With
   something in hand the search instead sets human `+0x5b8` = 1 and drops the held object (`0x00257f38`).
2. The winner's record is pinned and it gets message 0. The weapon's handler (`0x003fead0`), while its state (data
   `+0x10`) is 0, sets it to 3 and sends the human message `0x14`, which plays the pick-up clip (`0x0025e5a8`, below).
3. The clip's event (message 3) applies the object's anim set (object type `+0x87`, 3 for a bat) when the human's
   differs (`0x00221ed0`); `Human_PickUpObject` has no case for type 3 and returns 0, so `0x00227010` puts the bat
   in the hand (`+0x338`).

**The pick-up clip** (`0x0025e5a8`, from the human's message `0x14` at `0x00246090`; confirmed (code)). The
handler reads three bytes of the object's type: the **pick-up animation** (`+0x65`, `CfgObj`'s `pickup_anim`, the
`ANIM` constants of `Caps.lua`), the **anim set** (`+0x87`) and the type (`+0x86`). The clip is the pair's low one
when the object's position is at most **0.8 m** above the human's (z difference), else the high one:

| Pick-up animation | Low clip | High clip |
| --- | --- | --- |
| 0 `na`, 1 `OneHandPickUp`, and any value above 7 | 461 `ANIM_ONE_HANDED_OBJECT_PICK_UP` | 462 `…_HIGH` |
| 2 `TwoHandPickUp` | 503 `ANIM_BARREL_PICK_UP` | 504 `…_HIGH` |
| 3 `OneHandKnifePickUp` | 481 `ANIM_KNIFE_PICK_UP` | 482 `…_HIGH` |
| 4 `OneHandBatPickUp` | 498 `ANIM_SWINGABLE_OBJECT_PICK_UP` | 499 `…_HIGH` |
| 5 `LeftHandPickUp` | 463 `ANIM_ONE_HANDED_OBJECT_PICK_UP_LEFT` | 464 `…_LEFT_HIGH` |
| 6 `LeftHandHatPickUp` | 465 `ANIM_ONE_HANDED_OBJECT_PICK_UP_LEFT_HAT`, at any height | |
| 7 `GhettoPickUp` | 549 `ANIM_GHETTO_PICK_UP` | 550 `…_HIGH` |

`dyn_bat_tuff` is `OneHandPickUp` (the objects list), hence 461 from the ground. Before the clip, a pick-up
animation of 5 or 6, or an anim set of 4 or 6 on the human, drops what the human holds (`0x00257f38`) when he holds
something. The clip plays with a 0.2 s blend and chains into the human's idle (clip slot 0) or, with state bits 3,
his fight idle (slot `0xb`, record `+0x18` = `0xb`); while that follow-on is set up, the object's anim set is
pushed (and popped after) when the human lacks state `0x200000` or the set is 4 (`OVERHEAD_WEAPON_SET`) or 6
(`GHETTO_SET`). The steer (`0x00275d10`) gets the time to the clip's first event. A type 44 object (`0x2c`) is
checked against an inventory count first (`0x0041e420`, `0x0041ded0`, not traced).

At runtime (slot 1, a bat 1 m ahead): triangle started clip **461** with set 3, `Human_PickUpObject` ran 9 updates
later and the hand held the bat the update after. Confirmed (runtime). `Human_PlaceItemInHand` (`0x00238540`,
`HuPlaceItemInHand`) is the scripted way to arm a human; the pick-up does not use it.

**In the hand.** The bat hangs from **pose bone 25, the right hand** (bone 19 is the left, which the left-hand pick-up
clips use), at the offset clip 461's **type-9 event at frame 7** gives; the attachment and its maths are on
[World objects: objects in a human's hand](objects.md#held). The event (confirmed (runtime), read from Rembrandt's
clip 461 in memory; 462 has the same event at frame 4, and his 481, 482, 498 and 499 are the same two clips): bone
25, position `(47, 88, 54)` → (0.0459, 0.0860, 0.0264) m, rotation `(22788, 4078, −23079)` →
(0.6954, 0.1245, −0.7043, 0.0690). The bat's stored local pose is that position × Rembrandt's scale 0.97, slid
0.39 m (`dyn_bat_tuff`'s `CfgObj` `+0x70`) along the bat's own `y`: **(0.1500, −0.2908, −0.0053)** with the rotation
unchanged. The hand at idle sits at (0.29, −0.03, 1.05) in the model frame, on the human's right (`+x` with the model
facing `+y`). At runtime (PCSX2 2.9.94, slot 1 copy, [World objects](objects.md#held) has the inputs) the local pose
did not change through the idle, a walk (stick 60 % up), the square swing 34, the block (R1) and a run (stick 100 %
up): no bat clip moves it, so the bat swings with the hand alone. Confirmed (runtime); a screenshot of the swing showed
the bat in Rembrandt's right hand.

**The anim set.** A human keeps a stack of up to 3 anim sets (record `+0x0c` the depth, the sets as bytes at `+0x10`;
push `0x00253ed0`, pop `0x00253f28`). Applying one (`0x00253688`) resets the clip slots (record `+0x28` + slot × 4)
from the defaults at `0x005105d8` (a player's slot `0xe` is always 380) and then writes the set's overrides. With a
set 1-3 weapon in hand, square and cross read slots `0x10` and `0x11` (and `0x13` at a grounded target), which is how
the bat's 34 and 36 replace 12 and 11 ([Moving attacks with something in hand](#armed-moves)). The three melee sets,
confirmed (code):

| Slot | Default | Set 1 (knife) | Set 2 (baton) | Set 3 (bat) |
| --- | --- | --- | --- | --- |
| 0 (idle), `0xb` (fight idle) | 388, 358 | not changed | not changed | not changed |
| `0xe` (fight walk) | 372 | 380 (players) | 380 (players) | 380 (players) |
| `0x10` (square) | 12 `ATTACK_S1` | 45 | 39 | 34 |
| `0x11` (cross) | 11 `ATTACK_X1` | 47 | 41 | 36 |
| `0x12` (mounting strike) | 212 | 49 | 43 | 38 |
| `0x13` (grounded strike) | 193 | 48 | 42 | 37 |
| `0x14` (grab front strike) | | 50 | 44 | not changed |
| `0x15`, `0x16`, `0x17`, `0x18` (block start, high front, sustain, shuffle) | 605, 608, 606, 607 | not changed | 621, 624, 622, 623 | 621, 624, 622, 623 |

So a bat has **no carrying or idle clip** of its own: the human idles and walks as usual with the bat in his hand.
No set has a slot for the walk attack, the run attack or the snaps: those ids are constants in the square and cross
code. At runtime the stack's depth was 1 with a bat placed. Confirmed (runtime).

**Which objects carry which set** (the object's `+0x87`, `CfgObj`'s `anim_set`, from the objects list): 1
`KNIFE_WEAPON_SET` (6 `TYPE_KNIFE`, a `TYPE_BROKENBOTTLE`), 2 `BATON_WEAPON_SET` (11 `TYPE_BATON`, a `TYPE_BAT`), 3
`CLUB_WEAPON_SET` (42 `TYPE_BAT`, a `TYPE_BATON`, the mace, Diego's weapon), 4 `OVERHEAD_WEAPON_SET` (68: drums,
chairs, amps, carts, ...), 5 `SINGLE_HAND_THROW_WEAPON_SET` (46: bricks, bottles, the molotov), 6 `GHETTO_SET` (the
two blasters); everything else is 0 `NONE_SET`. The numbers are inferred from the clips each set installs (knife,
baton and bat combos; the barrel idle 509 for 4, the ghetto idle 555 for 6; 5 installs nothing and serves the
one-handed throw) and from `dyn_bat_tuff` being `CLUB_WEAPON_SET` with set 3 (confirmed (runtime)). Sets 7 and up
are the human's own states (7 installs the grab hold 82 and the grab strikes 51 and 55), never an object's.

#### Moving attacks with something in hand {#armed-moves}

Square, cross and L2 + cross or square choose by the **held object's set** (`0x00231a80`: the held object's
`+0x87`; `0x00231a38` gives its type, `+0x86`), not by the human's set stack. Confirmed (code) at `0x00286cc8`
(square), `0x00287a18` (cross), `0x0027d800` (charge), `0x0027d900` (dive), `0x002880d8` (throw) and the dispatcher
`0x0027c120`. "Run" below is gait 4 with record `+0x08` clear (`0x00223a60`), and for cross also gait 5 with
`+0x08` clear (`0x00223a98`); either needs `0x00225c10` (for a pad player: the stick above 0.95 at `0x005102e8`, plus
state tests) and no fight stance (state bits `0x3`, `0x00228340`). "Walk" is gait 1-3 with the stick at 0.12
or more, and the unarmed walk attack also needs no fight stance ([Attacks](#attacks)); for the throws a stance only turns
the walking throw (gait 2) into the standing one.

| Held set | Square / cross at a run | At a walk | Standing (or in a fight stance) | Charge, dive |
| --- | --- | --- | --- | --- |
| nothing, 0 | 24 `ATTACK_FROM_RUN` (square at gait 4 only; cross at 4 or 5) | 23 `ATTACK_FROM_WALK` | square's snaps, `S1` 12 / `X1` 11 ([Attacks](#attacks)) | 0 / 1 |
| 1, 2, 3 (knife, baton, bat) | **501** `SWINGABLE_OBJECT_ATTACK_FROM_RUN` (`gen_run_1hand_weapon_atk`) for all three (gaits as unarmed) | **no walk attack**: the standing swing, slot `0x10` / `0x11` | slot `0x10` / `0x11` (bat 34 / 36, knife 45 / 47, baton 39 / 41); no snaps | 0 / 1, unchanged |
| 4 (overhead) | the throw 507 `BARREL_THROW_FROM_RUN` (gait 3, 4 or 5) | 506 `BARREL_THROW_FROM_WALK` (gait 2) | 505 `BARREL_THROW` (gait 0 or 1) | the throw instead |
| 5 (one-handed throw) | 472 `ONE_HANDED_OBJECT_THROW_FROM_RUN` (gait 3-5) | 471 `…_FROM_WALK` (gait 2) | 467 `ONE_HANDED_OBJECT_THROW` | the throw instead |
| 6 (ghetto blaster) | 553 `GHETTO_THROW_FROM_RUN` (gait 3-5) | 552 `GHETTO_THROW_FROM_WALK` (gait 2) | 551 `GHETTO_THROW` | the throw instead |

So **with a bat at a run, square plays 501**, not 24 and not a slot: `Player_Square` tests the held set first and,
for 1, 2 or 3, takes its own branch, which has the run attack (constant `0x1f5`, through the same starter as 24,
`0x00264a80`: record `+0x08` `0x1000000`, a 0.3 s blend, aimed at the current target or, for the player without
one, the nearest in the clip's reach, `0x0027b058`) and then only the grounded strike
(slot `0x13`, also on a tackled target), a mugging (`0x0026f860`), an object attack on a breakable with no human
target (`0x00263eb8`, not traced) and the slot's swing. That branch has **no walk attack, no snaps, and no strike
on a grabbed target** (the unarmed 120); a walking player swings from where he is. Cross's armed branch is the same
with slot `0x11` (and combo count `+0xbc` = 2 before the swing). The knife's own `KNIFE_ATTACK_FROM_RUN` (490) is
not chosen by either button; what plays it is not traced. For sets 4-6 the dispatcher sends square and cross to the
throw (`0x002880d8`) and never reaches `Player_Square`; the charge and dive commands do the same. The throw's gait
test differs from the run attack's: gait 3 already counts as a run and `+0x08` is not tested. Set 5 first tries a
**smash** on a human in reach (473 from the front, 475 from behind, `bottle_smash_attacker`; not with a molotov,
`TYPE_MOLOTOV` 8), and the throw turns a held `TYPE_KNIFE` (11) into set 5 (pop, then push 5) when it is called
with one; neither path was followed further.

At runtime (confirmed (runtime), PCSX2 2.9.94, slot 1 copy, `dyn_bat_tuff` placed with `Human_PlaceItemInHand`,
nobody near):

| Input | Gait | Clip | Notes |
| --- | --- | --- | --- |
| stick 100 % up, square | 4, 7.80 m/s | **501** | 21 updates, `+0x08` `0x1000000` throughout, 7.09 m/s on its 2nd update; the run (410) the next update |
| stick 100 % up, cross (on its release) | 4 | **501** | the same |
| stick 60 % up, square | 2, 1.63 m/s | **34** | the standing swing; he stops (0.11 m/s on its 2nd update), 37 updates, then 389 |
| without the bat: stick 100 % up, cross | 4 | **24** | 21 updates, as square's |
| without the bat: stick 60 % up, cross | 2 | **23** | 24 updates, the walk (408) after |

**The held weapon in a run attack** (confirmed (code) at `0x0021b290`, `0x002653d8`): nothing about the weapon depends
on the clip. A hit's damage is the clip's Anim Range List value (501's own: the class table writes no index for
it, [The Anim Range List](#damage-table)); then, with the attacker's body `+0xd0` set and the held object's flags
(vtable `+0x54`) having `0x10000`, **(damage + the object's `CfgObj` u16 `+0x58`) × the first float of the
attacker's power class record** (`0x00222b78`, [Power meter](#power-meter)), the same for a swing and a run
attack. Because record `+0x08` has `0x1000000`, the hit takes the moving-attack branch that 24, 0 and 1 take (a
breakable gets hit kind 2, [World objects](objects.md#door-break)). Rage: 501 is **event 1 × 1 = 5** (2 blocked)
and kind 4, so never halved by repeats ([Rage](#rage)), against the bat's square 34 at 2 and its cross 36 at 11,
and the unarmed 24 at 1. The weapon is not worn: only its message 1 breaks it ([Losing it](#bat)). 501's damage,
hit code and the victim's reaction were not measured.

**Losing it.** Triangle with nothing to take drops the held bat at once, with no clip (`0x00257f38`, falling from
the hand under physics: [Drop](objects.md#held)); confirmed
(runtime). A weapon **breaks** only on its message 1 (`0x003fd600`): shatter particles, hidden (`0x100000`), message 7
to its holder (data `+0x18`; the human's message 7 pops sets 1-5), and state −5 deletes it on its next update.
Confirmed (code); what sends message 1 is not traced. At runtime 12 bat hits on a civilian (64 damage each, 34 and 37
alternating) never broke it and its handler saw no message 1, so hits alone do not wear a bat out; whether its
hitpoints (object `+0x128`, the `CfgObj` value 50) ever count down is open.

### Mugging {#mugging}

Triangle in a front grab spins the victim to a rear hold (78 / 79, state `0x85`), then 338 / 339 intro and the
340 / 341 loop (player `0x105`, victim `0x200`); 342 / 343 play while the stick is on target. Confirmed (runtime).

`Player_UpdateMugging` (`0x002856b8`), confirmed (code): the stick must be above **0.5** and within the tolerance of
the target angle (human `+0x5a8`); the time on target adds to record `+0x12c` (ms). Every period the target angle
moves (`0x002855f8`), by at least the tolerance plus 20°. At the required time the mugging succeeds: the money moves
in that update (victim `+0x370` 18 → 0 at runtime; [Crimes: mugging](crimes.md#mugging)), then 344 / 345 play
(346 / 347 on a failure), then 80 / 81 and back to the hold 82 / 83; the mug callback runs when the mugger's end
clip finishes. Past the fail time it fails. A victim's starting money: [Crimes](crimes.md#starting-money). The
parameters come from `0x00284ca0` by the victim's class (human `+0x11b`) and the Warrior class byte `+0x0c`;
the record seen: `+0x04` required **5000 ms**, `+0x08` period **2500 ms**, `+0x0c` fail **50000 ms**,
`+0x10` **50°** and `+0x14` **60°**. The stick counts as on target within `+0x10` (50°); `+0x14` (60°) is a coarser
gate, and each new target angle (`0x002855f8`) is re-rolled, up to 64 times, until it lies more than `+0x14` + 20°
(about 68°) from the old one. A victim whose brain `+0x26c` is 5 gives 1.5 times the money (at most 999). Confirmed
(code); the angle's frame (world or camera) is not traced.

**The stick game's states** (record `+0x128`, confirmed (code) at `0x002856b8`): 0 starts it (the first target angle,
the speech, the hint) and goes to 4, waiting; in 4, the stick on target goes to 2, and every update off target adds
to the **off-target time** (record `+0x138`), which is never reset, so `+0x0c` is the total time the player may spend
off target before the mugging fails (5); in 2, each update on target adds to the progress (`+0x12c`), a new target
angle comes each time the progress passes a multiple of `+0x08`, the victim's lines come at half of `+0x04`, and
reaching `+0x04` succeeds; leaving the target goes through 3 back to 4. "On target" is the stick above 0.5 and within
`+0x10` of the angle; the pad's rumble gets byte `+0x02` on target and byte `+0x1c` otherwise (player record `+0x1c`,
inferred: the motor). A mugger no player controls fails when game time passes record `+0x134` instead.

**`SetInterrogateParam`** (`0x002854b0`) overrides the record for every mugging while its `timeA` is non-zero (set
0-2 at `0x00510a18`, read by `0x00284ca0`; sets 3-5 at `0x00510a38` serve a player victim, `0x002853a8`). Its
arguments fill, in order: bytes `+0x00`, `+0x01`, `+0x02` (the on-target rumble), `+0x04` required, `+0x08` period,
`+0x0c` off-target allowance, `+0x10` tolerance (degrees, stored in radians), `+0x14` the re-roll gap (degrees),
`+0x18` (not read by the update), `+0x1c` the off-target rumble. `level99_lesson1`'s mugging lesson passes
(160, 75, 255, 5000, 2500, 20000, 40, 60, 20000, 0, set 0): **5 s on target, a new angle every 2.5 s of it, 20 s off
target allowed, 40° of tolerance, each new angle at least 80° from the last**; it passes all zeros after the lesson,
which gives the per-class defaults back. Confirmed (code); bytes `+0x00` and `+0x01` have no reader found here.

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
2. A victim with flag `0x20000000000` (demi-god,
   [`HuSetDemiGodMode`](../references/bindings/character.md#husetdemigodmode)) cannot drop below a fraction of its
   maximum in one hit: health is set to that fraction, flag `0x10` is set, and the rest is lost. The fraction is one
   global for every human (`0x0051024c`, the last `HuSetDemiGodMode` call's; the scripts pass 0.25).
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

### Knocked out, and the mission failing {#defeat}

**When health runs out** (inferred from what it does), `Human_OnHealthOut` (`0x002674c0`; from `0x00267a00`,
`0x00267e48` and `Human_HandleMessage`) runs, unless state `0x1c00000000` is set. Confirmed (code) at the cited
addresses:

1. It clears the state bits `0x208000800f` (stance, grabs, block), the action and the pending damage, and sets
   record `+0xf0` = now + 500 ms.
2. What happens next depends on human flag `0x4` (the player in the street had it,
   [Human flags](#human-flags)):
   - **Without `0x4`**, a human that is not cuffed, has flag `0x8` and passes `0x00227d98` is **wounded**
     (`Human_StartWounded`). It drops what it carries, leaves the radar and loses its icon.
   - Any other human without `0x4` is **knocked out** (`Human_KnockOut`, `0x00230328`) and drops what it carries.
     It is **dead** instead (state `0x100000000`, record `+0xf0` = 0) when the source of the last hit (record `+0xd0`)
     is an object of kind 29.
   - **With `0x4`**: knocked out, with record `+0xf0` 14 s later still.
3. A cuffed human's idle becomes 323 `ANIM_ARRESTED_DEAD`.
4. For a player, the players of the same gang who are knocked out get the same `+0xf0`.

**`Human_KnockOut`**:

- sets record `+0xf0` = now + 14,000 ms (`0x00510794`; its reader is not traced here);
- gives the push weight 1e9;
- turns a player's slow motion off;
- unless the human is dead or wounded (`0x100050000`), sets state **`0x40000`** (knocked out;
  `Human_IsKnockedOut`, `0x00227dd8`; `Human_WakeUp` clears it) and tells the brain (`0x0028c3b0`);
- for a member of a player's gang outside Armies and Rumble levels, shows the **`dyn_cross`** icon over it
  (`Human_ShowOverheadIcon`, argument 3), the mark a partner revives with a flash
  ([Crimes](crimes.md#triangle); inferred).

The flash clip 665 keeps a player alive ([Damage](#damage), step 4).

**The mission failing.** The engine decides this; `global.lua` does not. `GameState_CheckGameOver` (`0x004197a8`)
runs every game-state update (`0x0041a370`), while three things hold:

- the check is on (`EnableGameOverCheck`, game state `+0x155`,
  [Bindings](../references/bindings/level.md#enablegameovercheck));
- game state `+0x158` is 0;
- no level end is pending (`+0x14c` = 0).

It works through these cases, confirmed (code):

1. **A dead player** (state `0x100000000`) fails at once.
2. Nothing more happens while any player is neither knocked out nor cuffed.
3. A cuffed player 1 who can free himself (upgrade (6, 15) and a key) is spared.
4. When no one else in player 1's gang is free to help (`Gang_NoneAbleToHelp`, `0x00169dd8`: not cuffed, out or
   busy), the mission fails at once.
5. When others are free, and player 1 is cuffed (not out) or holds a flash (item 1), and game state
   `+0x414 + player` is 0, it **waits**. Every 37 updates it asks whether a free member has a route to him
   (`Gang_CanReachToHelp`, `0x00169ea0`). A yes resets the count at `+0x56e6`; the 4th no in a row fails the mission.
   A knocked-out player without a flash fails at once.

A failure sets `+0x14c` = 1 and the menu's title (`MissionFailed_SetReason`, `0x001d1fb8`): `GSTRING.HUD` 21 with
game state `+0x118` = 1 when player 1 is cuffed (busted), else 20 with `+0x118` = 0
([Text labels](../references/text-labels.md#text-gstring-hud)).

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

So the screen comes 180 updates (6 s at 30 updates a second, inferred) after the failure, with the reason set by
the engine. `HUDLaunchMissionFailed` is only the scripts' own route.

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
double. **The throw bonus at runtime** (confirmed (runtime), slot 6 copy, Rembrandt grabbing the civilian with
circle):

- Grab strikes 51 and 53 (square) each raised `+0x4` by 0.27, from 1.0 to 1.27 to 1.54. They set the kind
  (`+0xb`) to 3.
- A throw (stick 60 % up, circle tapped, 149 `THROW_01_FROM_GRAB_RIGHT` on this camera) set the kind to 4 when it
  started. Its two awards, 13 and 11 rage, came with the bonus still 1.54; without the strikes the same throw gave
  8 and 7. That fits `trunc(x × 1.54)` for both awards.
- The bonus went back to 1.0 on the update of the second award, when the throw ended.

**The repeat flag at runtime** (confirmed (runtime)), puppet civilian, `X1` tapped every second: the first six hits gave
5 rage each and the counts at `+0x9` went 1, 2, 3, 4, 5, 5 with the flag set by the sixth; the seventh and eighth gave 2
each (`trunc(5.76 × 0.5)`); with 5.17 s between hits the count stayed at 1 and every hit gave 5. The earlier reading
that the game rewrote a written flag "on the next update" is the clear at the next noted hit.

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

**The other attacks** (confirmed (code) at `0x002653d8`; the rage is `trunc(points × 1.44)` at class 6, from the
table 3 values above: event 0 = 6, event 1 = 4, event 2 = 1, event 3 = 7, event 5 = 3 points). Each row is one
award, halved (`>> 1`) when blocked unless marked:

| Anim ids | Award | Rage (blocked) |
| --- | --- | --- |
| 0, 1 | event 0 × 1 (6 points) | 8 (4) |
| 25-30 (`0x19`-`0x1e`) | event 5 × 1 | 4 (1) |
| **34** `BAT_COMBO_S1`, 39 (set 2's square) | event 2 × 2 | **2** (1) |
| 35, **36** `BAT_COMBO_X1`, **37**, **38** (the bat's grounded and mounting strikes), 40-44 | event 1 × 2 | **11** (5) |
| 45, 46 (set 1's square) | event 2 × 2 | 2 (1) |
| 47-50 | event 1 × 2 | 11 (5) |
| 51-56 | event 2 × 1 | 1 (0) |
| `0x68`, `0x74`, `0x78`, `0x7a` (grab and escape ids) | event 2 × 10 | 14 (7) |
| `0xc1`, `0xc2`, `0xd4`, `0x1ea`, `0x1f5` | event 1 × 1 | 5 (2) |
| `0xdb`-`0xe0` | event 2 × 1 | 1 (0) |
| `0xfa` | event 2 × 1, not halved | 1 (1) |
| `0x1e4`, `0x1e6`, `0x1e8`, `0x1ec`, `0x1ee`, `0x1f0` | event 3 × 2 | 20 (10) |
| `0x269`-`0x26c` | table 2 (`0x004ed988`) entry 0 × 1; blocked `0x004eda18` >> 1 | 75 points, over 25: 10 |
| `0x285`-`0x28b`, `0x28d`-`0x293` | event 1 × 3 | 17 (8) |

Every other id awards nothing. So **a bat's square gives 2 rage and its cross, grounded and mounting strikes 11
each**; seven crosses leave the meter at 77 of 78 and the eighth fills it. A bat's 34, 36, 37 and 38 are kind 4 for
the repeat tracker, so they never halve; only 35 (the bat `SS2`) counts as square-ended. The awards come only for
an attacker whose brain is the player's (type 0, brain `+0x04`) and who is not an ally of the victim
(`0x00290230`), and none when the victim is of class `+0x11b` 13 with flag `0x10`, or has state `0x180040000`;
`Human_AddRage` also needs a victim that passes `0x00227eb0` and `0x00227dd8` (not traced). For player 0 the
tutorial callback (`0x609250`) gets the attack's anim id first, before the victim tests, so it fires even for a hit
that gives no rage.

**The meter falls**: an unspent meter decays by the maximum × Warrior byte `+0x03` / 100 per 20 s (`0x00510294`;
200 % → 7.8 per second) once the hold timer passes; while raging it drains by byte `+0x04` (240 % → **9.36 per
second**), and rage ends at 0 (`0x00236fb8` clears `0x80000`). The HUD is told at 90 % and below 11. Confirmed (code)
at `0x002562d0`; confirmed (runtime): 78 → 0 in 8.34 s raging, 40 → 25 in 2.0 s idle.

**L1 + R1** (`0x1f`) with a full meter starts rage (`0x002843f8` → `Rage_Enter` `0x00284340` → `0x00236d28`): 643
`RAGE_START` for about 2.1 s, human `+0xe0` flag `0x80000`. Confirmed (runtime). **While raging**, confirmed (code), confirmed
(runtime) where marked:

- **No damage bonus**: `S1` still deals 17 and grab strikes 57 (runtime).
- **No power** is spent or drained (runtime: 400 through a grab and two strikes).
- The player's grabs cannot be struggled out of or escaped (`0x002258f0`, `0x00225830` test the grabber's rage).
- Every hit causes a reaction (hit armour does not apply), the power strike is 63, the specials 645 / 649, the
  rumble one step stronger, and no rage is gained.

**The rage callbacks** (`CfgRageHandlers`, confirmed (code)): `Human_AddRage` calls the **full** function
(`0x00236ec8`) with `(human, true)` the first time the meter reaches its maximum (a per-player latch at its
`0x00222b18` record `+0x58`); starting rage (`0x00236d28`) calls the **enter** function with `(human, flag)` and
ending it (`0x00236fb8`) the **exit** function the same way, the flag being whether the count at
`*(0x0051489c) + 0x268` is below 1. `HuSetLockedRage` (bit `0x100000`) does not stop `Human_AddRage`, so a locked
meter still fills from hits (inferred: the lock holds only the decay).

**`Player_UseItemCommand`** (`0x002843f8`, run from `Human_UpdateActions` at `0x00255074`) handles L1 + R1, the flash,
the key and the R2 map, in this order. Confirmed (code) at `0x002843f8`:

1. Nothing while the state word (record `+0x00`) has any of `0x80040000`.
2. **Cuffed** (state `0x20000`): only triangle (command `0xa`) does anything. For a player human (per-player `+0x1b`
   set) with record `+0x08` free of `0x7c7eae0`, upgrade (6, 15) unlocked and a key (item 6) carried: interface
   sound 24 (`vags/misc/usekey_01`), one key spent, and `0x00260fd0` frees him ([Crimes](crimes.md)).
3. R2 held (command 1) or released (2) opens or closes player `+0x1b0`'s map panel (`0x001a6d28`, `0x001a6c58` on
   HUD `0x00600848[player] + 0x1ef0`) unless `*(0x0051489c) + 0x42e + player` is set.
4. **L1 + R1** (`0x1f`) with the state word free of `0x19f9e0f3000` and `+0x08` free of `0xfc7eae0`: when rage
   (human `+0x650`) has reached the class maximum (`0x00223260`: the first `u16` of the Warrior class record,
   `0x00222b58`) and the human may gain rage (human `+0xe0` bit `0x2000000`, which `Human_MakePlayer` sets and
   `Human_AddRage` also tests) and is not raging (`+0xe0` bit `0x80000`), `Rage_Enter` (`0x00284340`) runs.
5. **The flash**, command `0x28` (d-pad right, [Commands](#commands)), needs all of: a player human (`+0x1b0` ≠ −1),
   at least one flash (item 1) in the player's inventory (`0x0041e420` on `*(0x0051489c) + 0x480`, the player index
   at human `+0x380`), not down or dead, the state word free of `0x1c00000000` (`0x00227f90`) and `+0x08` free of
   `0x40` (`0x00223c10`). Then by health (record `+0x144` against the maximum `+0x146`):
   - **Below the maximum.** The flash clip **665 `SPECIAL_FLASH`** (`gen_flash_use`, 40 frames) is pushed as a
     one-shot clip task on top of the stack (`0x0025ade8`: a type-5 task, `0x00106240`, holding `0x2000`) when
     **nothing blocks a move**: the state word has none of `0x18003ff0` (`0x00227fd8`: grabbing, held, mugging,
     tackling, throwing, blocking, `0x8000000`, `0x10000000`), none of `0xe0000` (`0x00223b48`), not the mini-game
     `0x4000000` (`0x00228050`), not tagging `0x2000000` (`0x002238c0`), none of `0x7bf9e9f7ff0`; record `+0x08` has
     none of `0x2fefefff`; and the object in hand is not of `ObjectAttribs` kind (`+0x87`) 4 or 6 (`0x00224000`).
     Otherwise, if 665 is already the human's anim (`0x002266b8`), nothing more happens (its event will use the
     flash); else the flash is **used at once** (`Flash_Use`, `0x00284280`). The code before this test that would let
     go of a grab (`0x00258a88` when the state word has `0xc0` or `0x400`) also requires the state word to be free of
     `0x7bf9e9f7ff0`, which holds both bits, so it never runs: in a grab the flash is used at once and the grab stays
     (inferred from the masks; not seen at runtime).
   - **At the maximum**, with upgrade (6, 8) unlocked (`0x00424130(0x006fe998, 6, 8)`: the first unlockable record of
     type 6 whose data is 8, unlocked when its bit in `0x006fe8f8` is clear, [Unlockables](player-state.md#unlockables)),
     the state word free of `0x19f9e0f3000`, `+0x08` free of `0xfc1fe7b`, not raging, the rage flag `0x2000000` set and
     a flash carried: one flash is spent (`Inventory_AddItem(…, 1, −1)`), rage is set to the class maximum and
     `Rage_Enter` runs. Without the upgrade nothing happens.

**The flash clip's event**: `gen_flash_use` (665) has one event of type `0x41` at frame 19 (with a type-11 event at
the same frame); `Anim_FireEvents` (`0x00101dd8`) sends it as message `0xc1`, and `Human_HandleMessage`
(`0x002473bc`) calls `Flash_Use` when the human's anim is 665 (for 666 it opens the door in front, for 668 it
serves the dealer goal). Confirmed (code); the frame read from the disc's clip (identical in both copies on the disc).
So the flash is used 19 updates into the clip, unless the clip is cut first (then nothing is spent).

**`Flash_Use`** (`0x00284280`), confirmed (code): one flash spent; **interface sound 23** (`0x0010fc30(*0x0050aa84,
0x17)`: entry 23 of the audio manager's interface cue table at `+0x1e0`, which `SoundCfgInterfaceSound(23, …)` fills
with `vags/misc/flash`, [Sound and music](../references/sound.md#interface-sound)), played 2D with flags `0x12`;
`Human_Heal` (`0x0022eb40`) with "full" unless the human is a player and the **difficulty in force** (`W_GameState +
0x154`, `SetDifficulty`: 0 easy, 1 normal, 2 hard, 3 fury, 4 rumble) is above 2; the HUD's per-panel request byte for
the player (`0x001b28a0`, [HUD](hud.md)); and the player's screen effect at `0x005fdeb8[player]` is reset
(`0x0018b7d0(…, 0)`). `Human_Heal` clears `+0x3be`, ends wounded (`Human_EndWounded`), wakes a knocked-out human
(`Human_WakeUp`), clears state `0x4000000000`, then sets health to the maximum, or on fury and rumble adds half the
maximum (capped); it also runs `0x0021ce08`, `0x0024ca40` and clears human `+0x644`. The same heal serves
`Human_Revive`, the uncuffing and the Armies game-over toggle.

**`Rage_Enter`** (`0x00284340`), confirmed (code): plays the 2D sound `vags/misc/rage_mode_06` through the game
state's handle at `+0x264` (restarted when it is still playing, `0x004194b8`); when the state word has any of
`0x18003ff0` and `+0x08` none of `0x2fefefff`, the pair is broken (`Human_BreakPair`); then, if nothing of
`0x18003ff0` or `0xe0000` is left and `+0x08` is free of `0xfc1fe7b`, rage starts (`0x00236d28`) and 643
`RAGE_START` plays as a full-body clip holding `0x2000` (`0x0025a8c0`); otherwise rage starts without the clip.

`level99`'s last lesson waits for this command through `PadSetHandlerEx` (the handler receives it even when
`EnableCommand` has turned it off, [Commands](#commands)).

### Target selection {#targets}

`Player_PickTarget(range, h)` (`0x0027a6c0`), confirmed (code):

1. Keep the current target (human `+0xc8`) while `0x0027a120` accepts it.
2. Otherwise search along a heading: the camera-turned stick's angle if the stick is above 0.01 (`0x0021d1a0`), else
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
   - **World objects** (world `+0x840`, `ObjectList_FindInRange`, `0x0039ab20`) within the range (2.0 m for
     square), and at the 135° pass within 0.8 × it. They must also be beyond record `+0xd8`, which the pick zeroes.
     `Target_ObjectFilter` (`0x0027a120`) keeps an object when it has a body, is within the angle (54°, then 135°)
     and 2 m in height, is a world object (type mask `0x8`, `Object_AsWorldObject`) and its **body flags have any
     of `0x8`, `0x10`, `0x20`** (`Object_IsStrikeTarget`, `0x00396710`).
   - Glass panes (world `+0x84c`, `0x0038fc70`) within the range, through `Target_GlassFilter` (`0x0027a070`): body,
     angle and height only.

   A world object's body flags come from its type's word `+0x5e` (`Obj_CreatePhysicsBody`, `0x00391d48`):
   bit `0x4` gives `0x10`, `0x8` gives `0x8`, `0x20` gives `0x20` (and `0x1` → `0x2`, `0x2` → `0x4`, `0x10` → `0x40`,
   `0x40` → `0x2000`, `0x80` → `0x10000`, `0x100` → `0x20000`), on top of `0x80000500`. Kinds 29, 33, 34, 15, 26
   and 31 add their own bits first. Which object types set those bits is on [World objects](objects.md). The square
   then plays [`Player_ObjectAttack`](#breakables) on the object picked.

#### The fight stance {#fight-stance}

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
  `0x0027d5a0` from the dispatcher after its busy gate, for a pad player (per-player `+0x1b`) outside the stealth
  movement state `0x00244770`, the state word's `0xf0000` and a scene; an AI-controlled player runs `0x0027cd50`
  instead, and a scene drops the target). Distances are compared squared; "drop the target" is `0x00226f70` (refused
  while locked, state `0x8` and `0x4`) and "take" is `0x00226c30` (refused while locked, and for a human the target
  filter `0x00227d78` rejects). Confirmed (code), in this order:
  0. **The nearest enemy** (`Player_FindNearestEnemy`, `0x0027b1a0`): the humans within **6 m** (`0x005104c4` = 36)
     that the enemy filter `0x002791b0` accepts, nearest first, kept if `0x0021c240` agrees; with none, the current
     target unless it is down (state `0x80000000` or `0x100000000`, or `0x00227dd8`). It also writes record `+0xd4` /
     `+0xd8`. A current target that is down, knocked out or in a scene is dropped (and the lock bit `0x8` cleared)
     unless it is flying in a knockdown (`+0x08` `0x400000`).
  1. **The sprint**: state `0x1000000` is cleared, then set again while L2 is held with stamina (record `+0x14a`)
     and `+0x08` free of `0x10`, which also clears `0x8008` and drops the target ([Sprint](characters.md#sprint)). A
     target beyond **13 m** (169) loses the lock bit `0x8`.
  2. **Running drops it**: with L2 held, or gait 4 or 5 with `+0x08` clear (`Human_IsRunningFree`, `0x00223a98`), or
     human `+0xe0` `0x8000000000` while not locked, nothing else happens this update; a player in a stance with
     `+0x08` clear also leaves it: `0x8008` cleared, target dropped, `0x00230140` (which ends state `0x200000`, see
     below), `Player_ExitStanceAndLock` (`0x002801e8`: `0x5` cleared, the combo count zeroed and, from the lock-on
     movement state, back to the free one), and a state code of `0xe` becomes 7.
  3. **An enemy within 2 m enters it**: when the movement state is not the lock-on one (`Human_FightStanceMove`), the
     state word lacks `0x200000`, and the nearest enemy is within **2 m** (`0x005104bc` = 4) or the player is locked:
     nothing with an object of set 4 in hand; else `0x1008000` cleared, `Player_EnterStance` (`0x00280068`:
     `0x00230140`, then `0x1` if clear; `0x00230140` clears state `0x200000`, setting `0x20000000` when he is then free
     and still, and zeroes record `+0x114`), the nearest taken as the target (when there is none yet and `+0x08` has
     nothing outside `0x320100`; and again when there is one), and unless state `0x200000` or human `+0xe0`
     `0x200000000000`, the lock (`Player_LockOn`, `0x00280158`: state `0x5`, and the lock-on movement state through
     `0x00221038`).
  4. **It holds** while the target timer (record `+0xf4`) runs and a target is kept; or, with the nearest within 2 m
     and the target beyond **3 m** (`0x005104c0` = 9), the target is swapped for the nearest when `+0x08` is clear;
     or the target is within 6 m.
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
walk attack and goes to the snap or `S1`**, which is what level99's lesson 7 needs ([Attacks](#attacks)). At runtime L1
held gave state `0xd` (confirmed (runtime), below).

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

**The steer in detail** (`Attack_SteerToTarget`, `0x002761c8`; confirmed (code) unless marked):

- **Its inputs.** `Attack_Start` passes the human, the target (`0x00226e60`, its locked target) and a time `T` = the
  time to the clip's **first event** of types 9, `0xf`, `0x13`, `0x2c`, `0x34`, `0x36` or `0x41` (its frame / 30
  divided by the clip's rate; with none, the clip's length; `0x00101658`). The steer clamps `T` to that time + 0.1 s,
  so `T` is the time to the first event. The per-kind numbers come from the record's attack table (record `+0x160`,
  16 bytes per kind): `+0x4` the **reach** (`0x002544a0`), `+0x8` the **far range** in mm (`0x00254508`; when it is
  not above the reach, reach × 1.25), `+0x0`/`+0x2` an offset in mm (`0x00254418`) and `+0xc` flags (`0x00254d60`).
  `Attack_Start` steers only when a target is locked and its distance (`0x00229960`) is within the far range; with
  flag `0x8` and the global `0x005102c4` set it uses the variant `0x00275678` instead.
- **The goal.** The target's position plus its velocity × (`T` + 0.1) (only half that lead when the full lead would
  carry it more than 1 m and farther away), minus the reach along the line from the human: the human should stand
  at the reach from where the target will be.
- **The turn** (`0x0023cf88`): the heading to the goal minus the current facing, wrapped to (−π, π]; skipped under
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

### Breakables {#breakables}

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
  move. At slot 4 one square broke the front pane centred at (51.26, 56.99, 1.59), 11 updates after the press.
  Confirmed (runtime).
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
  (message 3) ran `Human_PickUpObject` (`0x0023bf00`) on the 5th update.
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
[How a moving attack strikes](#moving-strikes) and [Barriers](objects.md#barriers).

### The stereo theft {#stereo-theft}

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

## Code index {#code-index}

Every combat function of the human code (`0x002176b8`-`0x00288000`) that the sections above do not walk through, in
address order, with what it does. Names are ours, as in the local Ghidra project, where each function also carries a
plate comment. The rest of the human code is indexed on [Characters](characters.md#code-index).

### Grabs, tackles and three-person moves {#code-grabs}

The state changes that start and end a grab, a tackle, its hold and a three-person (tandem) move, and the throw links.
The state bits are in [State flags](#state-flags); push weight is the human's attribute 7 (vtable `+0xe4`), set to 1e9
to make a body immovable and back to 1.0.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0022af48` | `Human_GoDown` | Pushes move style 0xf, makes the body immovable, goes down (0x0022f4f8) and clears stance; a pad player leaves the lock and drops the target, and tutorial hints 24 or 25 may show. | confirmed (code) |
| `0x0022b1a0` | `Tandem_LinkThree` | Ends a grab and links grabber, victim and attacker in a three-person move (states 0x1000 / 0x2000). | confirmed (code) |
| `0x0022b328` | `Tandem_Unlink` | Ends a three-person move: clears the states and links, restores push weight and body group, and puts the victim down. | confirmed (code) |
| `0x0022b5a8` | `Tandem_UnlinkQuiet` | Like Tandem_Unlink without putting anyone down. | confirmed (code) |
| `0x0022b760` | `Tandem_BreakFromVictim` | Breaks a three-person move from the victim's side, then Tandem_Unlink. | confirmed (code) |
| `0x0022b960` | `Grab_BreakOnHit` | Breaks the grab, tackle or tag a hit human is in, stunning and damaging by the anim's knockdown value. | confirmed (code) |
| `0x0022bce0` | `Throw_ClearLink` | Clears the thrower's state 0x1000 and link, restores push weight and body, then the victim's side. | confirmed (code) |
| `0x0022bdc0` | `Throw_ClearVictimLink` | Clears the thrown human's state 0x2000 and link; puts it down or stuns it by its state. | confirmed (code) |
| `0x0022c388` | `Tackle_End` | Ends a tackle on both humans: pops style 0xc and puts the victim down. | confirmed (code) |
| `0x0022c548` | `Tackle_Mount` | Turns a tackle into the mount: clears 0x400 / 0x800, makes both immovable and sets the mounted states. | confirmed (code) |
| `0x0022cb00` | `Grab_End` | Ends a grab: clears the grab states 0xc0, restores weights and styles, the mug camera and speech. | confirmed (code) |
| `0x0022cd78` | `Grab_StartMug` | Turns a rear grab into a mugging (0x100 / 0x200) and reads the mug parameters. | confirmed (code) |
| `0x0022d030` | `Tackle_StartHold` | From a tackle to the hold (0x8000000000 / 0x10000000000), starting the mini camera. | confirmed (code) |
| `0x0022d1f8` | `Tackle_EndHold` | From the hold back to the tackle (0x400 / 0x800). | confirmed (code) |
| `0x00230930` | `Attack_FarReachSquared` | Squared far range of an attack kind for a human, from the attack table's far range or fixed values by gait and held object; the AI's stand and avoid goals use it. | confirmed (code) |
| `0x00230d00` | `Attack_ReachSquared` | Squared reach of an attack kind for a human: kind to anim id to AttackTable_GetReach, with fixed values for some kinds. | confirmed (code) |
| `0x00232be8` | `Grab_EndBoth` | Ends a grab between two humans (Grab_End, 0x00280548) and sets state 0x20000000 on both. | confirmed (code) |

## Coney's implementation

`src/combat/` holds the player's combat rules as a self-contained core: it decides, and `src/human/` plays what it
decides through the human's animator and lands the hits. Everything runs on the fixed 1/30 s step; time-based meters
take game time in whole milliseconds and carry the fraction of a point, as the original does; the coin flips come from
a seeded generator (`CombatRandom`), so a run with the same seed and input is the same run.

| File | What it does |
| --- | --- |
| `combat/commands.*` | the nine trigger tables (`CommandTables::street()` is the street's), and the matcher that turns each update's buttons into one command in the documented order, with the tap (1-6 samples), long hold (4th sample, or a release within 3) and history hold (7) counted per button |
| `combat/attacks.*` | square's choice (target, run, walk, a snap with a target found, `S1`), cross's (run, walk, `X1`), the armed branch of a knife, baton or bat in hand (501 at a run, the grounded strike, the set's swing), the object attack's clip, the charge and dive condition, the chain table, `attackHitUpdate()` (the measured hits of the [timing table](#attacks)) and `AttackChain`: the chain read from the record's `+0x08` as the attack's clip holds it ([Tasks](tasks.md#held-flags)), one buffered press, the hit counted in updates, the attack over once its clip has given its bits back |
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
| `human/fighter.*`, `human/fighter_grab.cpp`, `human/fighter_victim.cpp`, `human/fighter_clips.h` | the player's combat inside the human (split as the attacker, the grab and the victim side): builds `PlayerCombat`'s input (the camera-turned stick in the facing frame, the pad's stick, the gait, game time, the target in front, the grab search and the snap's search), plays its clips, turns and slides into an attack, poses a grab (the alignment, the connect, the gate, the snap and the attachment), turns and walks the grab by the stick, lands the hits with their rage, locks onto a target and combat-walks round it; and the player hit (a duck and its counter, the block, the health floor, the hit armour, the reaction, stun, knockdown and mash) and held in a grab (the counter at the catch, the struggle, the strike back, the escape and the reversal) |
| `world_objects/pickups.*`, `gamemodes/level_pickups.*` | triangle with the objects of a level ([Crimes: triangle](crimes.md#triangle), steps 4 and 5): message 0 to the nearest object with a prompt (`SetMsgHandlerEx`), then to each object in reach, a true result taking the press; the search (the [pickable](objects.md#pickable) classes, reach, the two sight rays, the score by the direction from behind the feet) and the clip; the take, which adds a `TYPE_SPECIAL`'s loot with notify and its value in money without and removes the record, or puts any other kind in the hand ([A bat in hand](#bat)); with something in hand and nothing taken, the drop. The human plays the clip with a 0.2 s blend and takes the object at its first event (`Human::startPickUp`); the play mode gives the fighter the held type's anim set, whose square, cross and two strikes `combat::animSetClips()` gives |
| `human/human_flags.h`, `human/fighter_script.cpp` | the [human flags](#human-flags) as the fighter keeps and reads them: god mode drops a hit's damage (**Coney's reading**: the reaction still plays), the demi-god floor (`HuSetDemiGodMode`'s fraction, one global) sets god mode when reached, `0x80`, `0x100`, `0x200`, `0x400` and `0x200000` shape the reaction, `0x2000000` gates every rage gain, `0x100000` freezes the meter's drain and decay, `0x4000000` spends no power, `0x100000000000` and an untargetable gang are skipped by the target search; `HuRevive` and `HuSetNormalMode`. The rage handlers (`CfgRageHandlers`) are called after the characters' step with the human's handle (**Coney choice** of the arguments) |
| `human/turn_and_slide.*` | the attack's steer ([Target selection](#targets)): a turn and a slide at a constant rate over a time, the last update only for the time left; the time to a clip's first steer-ending event; and the goal, the target led by its velocity and short of it by the reach |
| `human/victim.*` | what the player and the target share as victims: the update's largest hit, the reaction it plays, the stun (its exit waits for the clip playing to end), the knockdown, the ground time, the rise and the mash |
| `human/pair_placement.*` | the pair's geometry: offsets in the grabber's frame from a range record's direction × reach or a clip's type-8 pair event, the alignment (`Pair_AlignStart`), its time, the gate at the connect's end and `Pair_CheckPlace` |
| `human/holdable.h`, `human/human_held.cpp` | what a grab or a tackle holds (`Holdable`): the passive target or a human; a held human's clips are the grabber's, its place the grabber's while attached, and its fighter keeps the hold's state (`Fighter::enterHold`) |
| `human/target_human.*` | a passive target for the sandbox on `Victim`: health, the reaction, the stun, the knockdown, the ground time and the rise, the dying clip, the root motion of its reactions and throws |

**In the player.** `human::Player` runs the street's `CommandMatcher` on the pad's buttons and writes the command,
the buttons and the stick into the human's per-player record; the characters' step (`human::Humans`,
[Tasks](tasks.md#humans-update)) then runs the dispatcher from that record for every human, the player's or not, each
update the human is on the ground and not climbing, after the animation and the locomotion. **A move's timing is its
clip's** ([Tasks](tasks.md#held-flags)): the clip's task holds bits of the record's `+0x08` (an attack its phases
`0x7`, starting in the wind-up `0x1`; the grab's and tackle's clips `0x10`; the duck `0x1000`; its counter `0x2000`;
the run attack, the charge and the dive `0x1000000`; 389 its `0x40000000`), the clip's events `0x2c`, `0x2d` and
`0x48` open the chain window, start the end phase and the recovery, and the task gives the bits back when the clip ends
or is cut off. On Rembrandt's clips the events fall exactly on the updates measured at runtime (`S1`'s window 6, end
15, recovery 16; `X1`'s 10, 20, 21; `XX2`'s end 17 and recovery 19). Every reader takes the bits with its own mask
([Tasks](tasks.md#readers)): the recovery and the run attack's bit (`0x5c7fee0`) drop every press past the block, the
chain buffers while `0x7`, square and cross refuse on `0x100101f`, circle on `0xfc7eaf7`. The stick goes through the
locomotion gate ([Tasks](tasks.md#locomotion-gate)): the attack's phases and the grab bit make the human busy (no stick
step, the clip and an attack's slide move the body), the recovery zeroes the stick's velocity, so the stick moves the
player again on the first update after the clip; 389 holds nothing the gate reads, and the stick replaces it with the
walk start at once. While the fighter's states hold the body (blocking, holding someone, mugging, held, reacting) the
stick does not move it either. Let go of R1, the player stands in the idle with the state code `+0x14` at 5 for 5
updates, which the gate reads, and takes a press at once. Triangle keeps its own order (climb, context action, jump)
and is refused while the record drops the dispatcher's commands or the human is busy; L2 still sprints, but not while
blocking.

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
events of those two moves are not mapped. A throw (147-161) gives two awards of 6 and 5 points, back-derived from its
8 and 7 rage at runtime (13 and 11 with a throw bonus of 1.54).

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
`AnimState::Hold` keeping its loop; an attack returns to the fight idle 358 while the player has a target and through
389 to the idle 388 when he has none, **Coney's reading** of the runtime runs):
the chains `S1` 12, `SS2` 16, `SSS3` 19, `SSX3` 17, `X1` 11, `XX2` 13, `SX2` 15, `XS2` 14 and the snaps;
the run attack 24 (501 armed) and the charge 0 and dive 1, after which the run resumes when the stick is
still at a run; the block 606, or the shuffle 607 with the stick pushed; rage 643; the grab 71, 72, then the hold 82
(victim 73, then 83), or from the rear 71, 74, 84 (victim 75, 85); the miss 71, 69, 389; the tackle 4, 5, then 210
(victim 6 when the player's 5 starts, then 207),
the tackle's miss 4, 2, 389; the grab strikes and power strikes with the victim's next id (52, 54, 56, 58, 64); the spins
78 / 79 to the rear hold 84 / 85 and 80 / 81 back to 82 / 83; the throws with the victim's next id, then 196; the
let-go 95 / 94; the mugging 78, 338, 340 (victim 79, 339, 341) with 342 / 343 while the stick is on target and 344 /
345, 80 / 81 on success; the mount from the front hold 118 (victim 119) into 210 / 207, and in the mount the strikes
219 / 221 and 223 and the power strike 225 (231 in rage) back to 210 (victims 220, 222, 224, 226, 232 back to 207),
the pick-up 248 / 249 back to the front hold, and the release 244 / 245, the victim rising with 199; the strong
grapple 71, 657, 82 (victim 658, 83), from the rear 71, 659, 84 (victim 660, 85).

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
the rage and repeat tracker, the combat walk and the grab's turn), `tests/human/combat_timing_test.cpp` when every
move takes a press and gives the stick back under button spam and partial stick,
`tests/human/moving_attacks_test.cpp` every attack button at a walk, a run and a sprint with an analog stick, through
both the characters' step and `Human::step`,
with the disc's clip lengths, `tests/human/combat_test.cpp` the human with
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
`applyClassDamage()` does that write into each human's own copy of the list (`Human`'s `classDamage`). The player
takes his class's table and his Warrior class's percentage from the recorded configuration (`human::playerClassOf()`),
at a level's start and at the debug menus' change of character alike; the disc check gives Rembrandt's `S1` 17 as at
runtime. When the scripts recorded no `CfgChar` call of his type he plays the file's damage.

**Coney choices**, where the research is silent or inferred:

- Inside one trigger table a later matching entry overwrites an earlier one, as the tables do between themselves;
  trigger 4 (query) never matches.
- **Object targets** ([Target selection](#targets)): square with no human in front takes the nearest whole pane
  within the object attack's far range, within 54° of the heading, else within 135° (the original's × 0.8 is not
  applied there); its centre is the target point. The original approaches from up to far × 1.5; Coney does not.
  Doors and loose objects are not object targets yet.
- **The pick-up** ([Breakables](#breakables)): the reach is measured in plan from the feet, and the second point the
  search goes round (human `+0x4e0`) is left out; the flag messages (`0x19`) that change what is pickable are not
  modelled. The human does not steer during the clip. Every `TYPE_SPECIAL` is loot (the named mission items are not
  listed) and item 10's pickup sound is not played. The human's current context record is the nearest object with a
  prompt in reach. The clip is the pick-up animation's pair; the drop before a left-handed or hat clip, and the
  anim set pushed for the follow-on, are not modelled. A dropped object lands 0.3 m ahead of the feet, with no fall.
- **A weapon in hand** ([A bat in hand](#bat), [Moving attacks with something in hand](#armed-moves)): the
  fighter's anim set stands for the held object's set. With set 1-3 square and cross take the armed branch (501, the
  grounded strike, the slot's swing; no walk attack, snaps or strike on a grabbed target), and the charge and dive are
  unchanged; the rage is the [Rage](#rage) table's (not its gates on the attacker's brain and the victim's class, nor
  ids `0x269`-`0x26c`'s table-2 award). **Not yet**: the armed branch's mugging, the blocks' clips of an anim set, the
  weapon's damage bonus (`CfgObj` `+0x58` × the power class's factor), a breakable's hit kind 2 for the moving
  attacks, and the throws of sets 4-6 (square, cross, the charge and the dive play the unarmed moves); a bat never
  breaks.
- **The held flags** ([Tasks](tasks.md#held-flags)): the bits each move holds where the research names none. Every
  attack the dispatcher starts (the walk attack, the snaps, the grounded and mounted strikes, the grab strikes, power
  strikes and throws) is built as `Attack_Start`'s (holds `0x7`, sets `0x1`); the charge and dive hold the run attack's
  `0x1000000` (the dive's is `0x400000` at runtime, [Moving strikes](#moving-strikes); both make an object hit kind 2);
  the grab's connecting clips, its spins, the mugging's clips and the let-go hold the grab bit `0x10`; a
  start clip `0x10000000`, the landing `0x1000000`, the run stop and the climbs `0x80000`. An event acts only on a
  task holding the bits it changes; a new task first clears the bits it holds, so the next attack of a chain starts
  in its wind-up, and one leaving clears only the bits no other task holds; only the newest task's events fire. An
  event at clip frame `f` fires on the update whose clip time, rounded to the nearest frame (a tie going down), first
  reaches `f`, which gives the measured phases from Rembrandt's frames. A clip played once ends when less than 0.1 ms
  of it is left, so `XX2`'s clip ends on its 30th update as measured, not a float's rounding later. The grab's moves
  and the mount's strike take square's mask (`0x100101f`), so a move in a hold plays out before the next. The chain
  ends once the record holds none of the attack's phases, the recovery, the counter or the run attack's bit. Every
  attack whose hit was not measured (the snaps, the moving attacks, the throws, the grounded and mounted strikes) hits
  2 updates in, as `S1`. A moving attack's hit on a world object lands instead through its strike window
  (`combat::movingStrikeWindow()`, [Objects](objects.md#coneys-implementation)); its hit on a human still lands 2
  updates in (the strike shapes are not built). The block's release plays the idle at once, and its fade holds
  `0x10000000` for its 5 updates:
  the stick turns the player but the walk start waits for the fade, as at runtime. Coney never sets state code 5.
- **The characters' step** ([Tasks](tasks.md#humans-update)): it runs on Coney's fixed 1/30 s step, the original's
  30 Hz characters' update, without the 60 Hz tick or the timing wheel, which wait for the world's objects; the brains
  are an empty hook until the AI lands; the context actions (triangle) are refused while the dispatcher would drop a
  command or the human is busy.
- `SS2`, square is always `SSS3` (19), never 20; a grounded target takes 193, never 194; the dive takes the charge's
  conditions; a buffered snap plays where a square would continue the chain, without a search of its own.
- **The snap** (`squareAttack` in `repo:src/combat/attacks.cpp`, the search and steer in `repo:src/human/fighter.cpp`)
  follows [Attacks](#attacks): the run and walk attacks first, then the snap only when `Fighter::snapTarget` finds a
  human within 2 m, 45° of the stick and 2 m in height, standing, with health left and targetable, that is not the
  current target; without one, `S1`. Its steer is `Attack_SteerToTarget`'s turn and slide over 0.1 s, only within the
  snap's far range. **Coney's readings**: the turn puts the target at the snap's own direction from the Anim Range
  List (to the side for 25 and 27, behind for 29), where the clip strikes, rather than straight ahead, and the hit
  lands on whoever stands within the far range on that side; the snap's target is not kept as the target (human
  `+0xc8`); the dispatcher's gait tests read the gait the last update's velocity left (so a square one update after
  the stick is first pushed fully still snaps, as at runtime, though the run start has moved the body). That gait is
  taken in the step's first pass (`Human::animate`), so the play mode's characters' step (`human::Humans`) and a lone
  `Human::step` both give it. The run attacks also need the record's `+0x08` clear, as the code tests: a square or a
  cross during the run start (`0x10000000`) plays `S1` or `X1`. In a [fight stance](#fight-stance) neither the unarmed run
  nor the walk attack plays, so a square there goes on to the snap or `S1` and a cross to `X1`; the armed 501 does not
  test the stance. **Stand-in**: the stance is the player locked onto a target (`Fighter::lockTarget()`), not the
  original's own rules for entering and leaving it.
  **Stand-in**:
  the clear line to the target (`0x00222a90`) is not tested. With the disc, `level99`'s lesson 7 passes:
  `repo:tests/platform/disc_level99_snaps_test.cpp`.
- A side is "front" up to and including 45° and "rear" beyond 135°; a height difference beyond 1.5 m counts as 0.9 to
  1.5 m.
- The mount ([The mount](#mount)): the victim is placed at clip 210's pair event when 210 starts rather than slid
  there over 0.1 s; the mount's moves take square's mask, so one plays out before the next; the mount's power strike
  spends the endurance fraction, as the grab's does (the page names only its need); triangle in the mount (the
  mugging) is not built, nor flag `0x20000`'s getting off at once.
- The pad's buttons are matched even while the pad is locked (`HuLockPad`), and a command disabled with
  `EnableCommand` is kept pending; the human acts on neither, but the `PadSetHandlerEx` handler hears both
  ([PadSetHandlerEx](../references/bindings/input.md#padsethandlerex)). Only the pad-driven humans carry a pad command:
  Coney's brains write their commands straight to the record.
- The special (cross + square outside a hold, [Specials](#attacks)) is always 653 (645 in rage): the variant and side
  offsets of `0x00263c90` are not built, nor circle + triangle (the tag). Both specials are refused on `0xaeebf7ff`.
- The strong grapple ([Strong grapple](#strong-grapple)) is the grab with 657 / 659 (649 / 651 in rage) as its
  connect and 658 / 660 on the victim, searched with anim 1's range within 54° of the stick, the clear line to the
  target (`0x0021c0a8`) not tested. Its damage is dealt on the snap to the hold, reported to the tutorial as 82 / 84;
  the paired, tackle and solo paths for other clip flags are not built.
- The power strike's extension (57 → 59, 63 → 65) takes square's press or cross's `0x10` only on an update with the
  window open, and the grab ends when the last part's clip gives way to 389: the victim, attached until then, is put
  on the ground (196) to get up after its time. The rear power strike's hit is scored as 57, not the spin's 80.
- A grab plays one move at a time; a throw lets go at once; the rear power strike's spin plays in front of the strike,
  whose timing starts with it; the release with too little power goes straight to the idles, and the grab
  broken at 0 power plays the let-go. A tackle also ends when the power meter is empty, and any hold when the victim
  has no health left.
- The grab and tackle search takes the nearest candidate by straight-line distance with no facing cone. The attack's
  target search uses the attack's far range in `Player_PickTarget`'s first two passes (the third finds no human the
  second missed); beyond the far range the attacker turns at most 8° at once (read as degrees). Within it the steer
  follows [The steer in detail](#targets): from the update after the clip starts it turns at angle / `T` and slides at
  (goal − position) / `T`, on top of the clip's root motion, the last update only for the time left. **Coney's
  readings**: `T` is the time to the clip's first event + 0.1 s (the code's clamp reads as the time to the event, but
  X1 at runtime turned for 9.25 updates, 0.208 s + 0.1 s), and the lead is the target's velocity × the same `T`; the
  turn faces the led target, not the standing point, which lies behind the attacker when the target is inside the
  reach (the runtime `XX2` at 0.83 m, inside its 1.12 m reach, turned under 1°). A reaction cuts the steer.
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
- The mugging (`MuggingGame`, `LevelPickups::mugEnded()`): the record is `SetInterrogateParam`'s set 0 while its
  required time is set, else the runtime one (5 s, 2.5 s, 50 s off target, 50°, 60°), not the per-class table; the
  target angles are drawn evenly (the first at random). The money moves in the deciding update, the end clips 344 /
  345 or 346 / 347 play, and the mugger's callback runs when his end clip finishes, or at once for a let-go or a hit
  ([Crimes](crimes.md#coneys-implementation)); the speech, the hints, the half-way `no_item` stop, the victim's
  interrogation and pocket item, ped type 5's 1.5 times and the statistic are not built. The theft: clockwise steps neither
  add nor take away; the 250 ms pause ignores the stick. The mash: the first press counts, a press's gain is
  truncated, and other commands are ignored.
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
  `0x002688d0` and `0x00268ea8`, are not traced); for a demi-god (flag `0x20000000000`, which only the level scripts
  set, `HuSetDemiGodMode`) the health floor stops a hit that starts above 25 %, and one already at or below it takes
  the whole hit; the escapee takes its escape clip's own damage, as at runtime.
- The duck's counter is asked for once per duck and the command that asks is spent (no attack starts from it); it
  plays on the next update with no steering, against the attacker that made the player duck, else the nearest target
  within 1.25 × 617's reach, and hits once.
- Held: the counter at the catch costs the grabber a quarter of its maximum; a reversed hold has no victim behind it
  (the grabber is an entry point, not a `Holdable`).
- The player's grab or tackle holds any `Holdable`: the sandbox's passive target or another human without flag `0x40`.
  A held human's fighter keeps the hold's state (held or mounted), which holds its movement, keeps it from acting and
  gives its brain the grabbed (`0x14`) or tackled (`0x15`) reaction goal; attached, its own update does not move it.
  The grab's intro leaves the victim alone (it is stopped at the connect, as `Grab_Connect` does), so an AI may still
  answer it; the tackle's intro stops it at once (**Coney choice**, as before). The answer itself (the AI's counter
  76 or 9) and the AI's own struggle in the hold are not built, and no AI grabs or tackles yet. A held human gone from
  the targets, or freed by a script, ends the hold.
- **Breaking a pair** ([Breaking a pair from outside](#pair-break)): every placement of a human (`Human::spawn`: the
  scripts' teleports, a scene's release) first breaks any pair it is in (`Fighter::breakPair`). The human placed plays
  nothing; a victim it held plays 145 (front), 107 (rear or a mugging) or 245 then 199 (mounted) and stands free; a
  grabber it was held by plays 138, 106 or 244 on its next update (**Coney's choice**: the original plays it at once).
  **Coney's choice**: a held human whose grabber stops updating without a break (taken out of the update) frees
  itself with the same victim clips after 3 of its own updates without its grabber keeping the hold (the humans act
  in an order that alternates each step, so a live grabber keeps it at most 2 apart). `Grab_Release`'s refusal in a
  scene is not built: no human in a pair is taken into one.
- The combat walk's clip by eight even 45° sectors centred on the clips' directions; the walk starts at its full
  speed (the 5 slower first updates backward are not known).
- **The flash** (`PlayLevelMode::stepFlash()`): d-pad right with a flash carried and health below the maximum breaks
  any pair, plays 665 (holding `0x2000`) when nothing holds the stick, spends the flash and fills the health at once
  (the original spends it on the clip's event), and asks for the health rings; its sound and the full-health rage use
  are not built.

**Not yet**: an attacker for the player (no human attacks him yet, so the victim side runs only in the tests; the
AI that would is on [AI](ai.md)); the
warnings of the player's own clips to the targets (they never block); the rage of the attacks beyond the chain, the
moving attacks and the throws (their events are not mapped); the hurt
multipliers `+0x10` / `+0x14`; weapons, breakables and the theft's car windows (no objects yet); the class damage
table read from the disc (`CfgChar` waits for the script runner's tables; the values `level99` needs are on
[AI](ai.md#damage-tables)); and the allies and class 13 rules a fight between humans needs.

## Open questions

- **The block**: whether a strength-3 hit can still break a block. (Answered: `+0x14` = `0xe`, message `0xa5`, is
  the duck counter, [Blocking](#block); seen at runtime, and the AI goals' answer traced.) The block goal's byte `+0x14`
  (answered): its Start rolls it against the block chance ([AI](ai.md#block)).
- **The shared Anim Range List**: whether the overwrite by the newest human is intended, and which humans share a
  list ([Being hit](#being-hit-runtime)). Coney does not reproduce it.
- **A blocked `SSS3`'s rage**: 0 at runtime ([Rage](#rage)) where the two awards' formula gives 1 (3 >> 1 = 1 point,
  as `SSX3`'s blocked 1); Coney gives 1.
- **The stun after a knockdown**: it ends at the rise + 200 ms, before 199 ends, yet after 653 the player stood in
  356 for 7 updates before 357 ([Being hit](#being-hit-runtime)); Coney plays 357 as 199 ends.
- **The halved rage** (answered): the repeat tracker and the throw bonus, both seen at runtime ([Rage](#rage)).
  Brain type 3 (answered): the Warriors' brain, so an ally's ([AI](ai.md#types)).
- **The backward combat walk** (answered): 3.429 m/s like every direction, after 5 slower updates
  ([Target selection](#targets)). Still open: what slows those 5 updates.
- **Class 13**: which character class it is (it gets hit armour and adds 2 s to a knockdown).
- **The rage events**: the meaning of the events beyond the chain attacks' (`0x002653d8`, `0x00264fa0`).
- **A weapon's rage** (answered): a bat's 34 gives 2, its 36, 37 and 38 give 11 ([Rage](#rage)).
- **The strike back's rage**: "The other attacks" gives 104 (`0x68`) event 2 × 10 (14 rage), but a strike back
  gave 1 at runtime; Coney keeps the runtime 1. Which id the award reads for it is open.
- **The pick-up clip** (answered): a pair per pick-up animation, high above 0.8 m ([A bat in hand](#bat)).
- **Commands `0x30`-`0x39`**: which scripts or weapons make them; `0x36`-`0x38` and the d-pad (`0x27`).
- **Mini-game mode 2 at runtime** (answered from the code: modes 1 and 2 are uncuffing and lock picking,
  [Crimes](crimes.md#mini-game-record)).
- **The grab code's `0x00510980` table**, the further grab state of `0x005101f0`, and what makes square play 212
  (`Player_Square` on a tackled target; never seen at runtime).
- **Square at a sprint** at runtime, and the moving attacks' hit timing (the victim was out of reach in the tests).
- **The mugging's angle frame** (world or camera).
- **The charge's stop at a barrier** (answered from the code: the slide response head-on against the fence's body,
  [How a moving attack strikes](#moving-strikes)); still open: the clip's root motion in its last updates, and which
  leg each of the bone chains 28-30 and 31-33 is.
- **The far ranges' class table** (`0x002545e0`, table `0x0055d640`): which of the class's 45 floats goes to which
  anim id, as the damage table's index → id map does for the damage.
- **The snap's steer and target**: whether `0x00264460`'s turn faces the target or puts it at the snap's side (Coney
  does the latter), whether the snap writes the target `+0xc8`, and which update's gait square's tests
  (`0x00223a30`-`0x00223a60`) read, given that a square one update after the stick still snapped at runtime.
- **The hits not measured**: the hit of the snaps, the throws and the grounded and mounted strikes
  (the phases are the clips' events, [Tasks](tasks.md#held-flags)).
- **Input and the stick after a move** (answered at runtime, [When input and the stick come back](#input-return)).
  The locomotion gate (answered, [Tasks](tasks.md#locomotion-gate)). The block's 5 updates after release (answered):
  the idle's fade holding `0x10000000`, not the state code.
- **The steer's time**: the code clamps `T` to the time to the first event + 0.1 s, which reads as the time to the
  event, while X1 at runtime turned for that time + 0.1 s; Coney uses the runtime's ([Target selection](#targets)).
- **`XX2` against a walking target**: in `combat_cross` the original's target walks up (0.83 → 0.91 m) and the bodies
  push apart on `XX2` (0.25 m/s against Coney's 0.81); Coney's sandbox target stands still and has no body contact.
- **The grab at runtime**: the placement is confirmed ([Grab pose at runtime](#grab-pose-runtime)); still open are
  the fields the alignment writes (human `+0x2e0`-`+0x332`, victim `+0xa0` / `+0xb0`).
- **The bat**: which code maps square and cross to 34 / 36 with a bat in hand, and whether a bat on the ground
  gets message 0 from the pick-up search ([A bat in hand](#bat)).
- **Rage extensions at runtime**: 63 → 65 in a grab and 231 → 233 in the mount, which `level99`'s rage lesson
  waits for (inferred from `Player_UpdatePowerMove`); L1 + R1 did not start rage in a slot 6 copy with the meter
  written full, so what else the start needs is open.
- **The flash in a grab** at runtime: the masks say it is used at once and the grab stays ([Rage](#rage)); Coney
  breaks the pair.
- **The clips' `+0x44` flags** (paired `0x1`, tackle `0x20`, grab `0x40`): which clips carry the tackle flag
  ([Strong grapple](#strong-grapple)).
