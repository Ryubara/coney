# Combat (the player on foot)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 (2026-10-05), reading memory over PINE in a street level ("the street") from save states,
loaded read-only and copied with pad input patched in ([Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)).
The player there is Rembrandt-class "player" human 2 with 314 of 900 health; the test victim is a civilian
(`PoizoCiv`, 600 health) moved in front of the player by writing the transform table. Damage and timings below were
read every update (30 per second) while the scripted pad played. The being-hit, block, grabbed and pose runs
([Being hit, at runtime](combat-reactions.md#being-hit-runtime), [Grab pose at
runtime](combat-grabs.md#grab-pose-runtime)) drove that civilian as a **puppet**: command ids written into its
per-player record made it attack, grab and block the player ([Driving
PCSX2](../guides/research-workflow.md#driving-pcsx2)); the player's health was reset to 900 before each run.

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

This page grew too large, so the behaviour sections by subject moved to their own pages, each keeping its anchors,
and the old headings below point to them: [grabs, the hold and the mount](combat-grabs.md), [throws, the bat and
mugging](combat-throws.md), [damage, reactions and defeat](combat-reactions.md), [the power meter, rage and
reviving](combat-rage.md) and [targets, AI attacks and breakables](combat-targets.md). The overview, the data
tables, the dispatcher, the attacks, blocking, the code index and Coney's implementation stay here; the move table and
frame data are on [Combat moves](combat-moves.md).

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
| `0x002843f8` / `0x00284340` | `Player_UseItemCommand` / `Rage_Enter` | L1 + R1 with a full meter, the flash, the key when cuffed, the R2 map ([Rage](combat-rage.md#rage)) | confirmed (code), runtime |
| `0x00284280` / `0x0022eb40` | `Flash_Use` / `Human_Heal` | spends a flash, its sound, the heal (full, or half on fury and rumble) | confirmed (code) |
| `0x0027a6c0` | `Player_PickTarget` | keeps or searches a target for an action | confirmed (code) |
| `0x00264178` | `Player_ObjectAttack` | strikes at a breakable object | confirmed (code), runtime |
| `0x002625a8` | `Attack_Start` | starts an attack clip, counts the combo | confirmed (code) |
| `0x0021b290` | `Strike_Contact` | computes a hit's damage | confirmed (code) |
| `0x002674c0` / `0x00230328` | `Human_OnHealthOut` / `Human_KnockOut` | health gone: wounded, knocked out or dead ([Defeat](combat-reactions.md#defeat)) | confirmed (code) |
| `0x004197a8` | `GameState_CheckGameOver` | the engine's mission failure ([Defeat](combat-reactions.md#defeat)) | confirmed (code) |
| `0x00169dd8` / `0x00169ea0` | `Gang_NoneAbleToHelp` / `Gang_CanReachToHelp` | whether the crew can still help a downed player | confirmed (code) |
| `0x0027a120` / `0x0027a070` / `0x00279f50` | `Target_ObjectFilter` / `Target_GlassFilter` / `Target_CarFilter` | the square's object targets ([Targets](combat-targets.md#targets)) | confirmed (code) |
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
| `0x00263c90` | `Player_SpecialAttack` | the special or strong grapple: target search, id by rage, variant and side, then a solo clip, a grab, a tackle or a paired move by the clip's flags ([Strong grapple](combat-grabs.md#strong-grapple)) | confirmed (code), runtime |
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
| `0x0026c7e0` | `Grab_Release` | both let go of a grab: 95 / 94 (107 / 106 from the rear); refused in a scene ([Breaking a pair](combat-grabs.md#pair-break)) | confirmed (code), runtime |
| `0x00258a88` | `Human_BreakPair` | ends any grab, mount, mugging or throw link from outside; only the other human plays a clip ([Breaking a pair](combat-grabs.md#pair-break)) | confirmed (code) |
| `0x0027ec20` | `Player_UpdateMounting` | strikes, power strike, back to the hold, get off, in the mount | confirmed (code), runtime |
| `0x00277958` | `Pair_CheckPlace` | a move in the hold needs the victim within 0.3 m of the move's offset | confirmed (code) |
| `0x0023cf88` / `0x0023d2b8` | `Human_TurnToOver` / `Human_MoveToOver` | turn to a heading, or move to a point, over a time | confirmed (code) |
| `0x002761c8` / `0x00276008` | `Attack_SteerToTarget` / `Attack_TurnToTarget` | an attack's turn and slide onto its target up to the clip's first event, or the turn alone ([Target selection](combat-targets.md#targets)) | confirmed (code), runtime |
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
| `0x1`, `0x2` | fight stance (`0x00228340` tests `0x3`); a player sets only `0x1`, scripts and AI set both ([Fight stance](combat-targets.md#fight-stance)) |
| `0x4` | has a target (inferred: set with the target at a grab) |
| `0x10` / `0x20` | grabbed from the front / rear |
| `0x40` / `0x80` | grabbing from the front / rear (a front grab reads `0x45`) |
| `0x100` / `0x200` | mugging / being mugged |
| `0x400` | tackling, mounted on the target (seen `0x405`); the victim `0x800` (seen `0x801`) |
| `0x1000` | a paired power move: throws, power strikes, specials and stealth kills (seen `0x1005`; [Combat moves](combat-moves.md#corrections)) |
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
| record `+0x148` | s16 | the **power meter** ([Power meter](combat-rage.md#power-meter)) | confirmed (code), runtime |
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
| `0x8` | woundable (`HuSetWoundable`): health out leaves him wounded, not knocked out ([Defeat](combat-reactions.md#defeat)); a death reaction plays 205 into the loop 200, and 195 when he is down, instead of 349 (`Human_PlayDeathReaction`, `0x00267b70`). Cleared on the victim by a grab or mount power strike that will defeat him (`0x0027f3b0`, `0x0027ec20`) and by a stealth kill from behind (`Player_StartStealthKill`) | |
| `0x20` | | the hit takes all the victim's health (`0x0021b290`) |
| `0x80` | reaction strength capped at 1 (`0x00266d00`) | |
| `0x100` | never stunned (`0x0026a6d0`) | |
| `0x200` | reaction strength -1 | |
| `0x400` | combo hits keep their full strength; strength +1 from an attacker with `0x200000` | |
| `0x4000` | escapes when the grabber's power runs out (`0x002562d0`) | **doubles** the damage dealt; ignores hit armour |
| `0x20000` | as `0x4000` for the power-out escape | |
| `0x80000` | | **raging** ([Rage](combat-rage.md#rage)) |
| `0x200000` | | reaction strength +1 (see `0x400`); ignores hit armour |
| `0x4000000` | spends no power (`0x00226448`) | |
| `0x1000000` | rubber-neck (`HuRubberNeck`, `0x0023a258`): no reader found; every load of human `+0xe0`-`+0xe7` in the executable (324 functions) was checked, and none tests this bit (inferred: unused) | |
| `0x40000000` | front-grab block (`HuBlockGrab` sets it with `0x80000000`): `Grab_IntroEnd` (`0x0026c1d8`) refuses a front grab when the victim **or** the grabber has it (the intro ends in 69, the miss); a weapon's paired kill skips a target with it (`0x002880d8`) | the player's own: no spin from a rear grab to the front (`Player_UpdateGrabbing`, `0x0027f3b0`), no return from the mount to the front grab (`0x0027ec20`) |
| `0x80000000` | rear-grab block (`HuBlockRearGrab`): `Grab_IntroEnd` refuses a rear grab when the victim or the grabber has it; a weapon's paired kill skips a target with it; a grabbed player with it cannot reverse the grab (`Player_UpdateGrabbed`, `0x0027fd68`) | the player's own: no spin to the rear (`0x0027f3b0`); a special attack skips its grab path (`0x00263c90`, [Strong grapple](combat-grabs.md#strong-grapple)); with `0x40000000` too, a circle tap does nothing (`Player_GrabOrTackle`, `0x00284920`: no intro, no miss, no grounded mount), while circle held still tackles (only `0x100000000` stops that) |
| `0x400000000` | a block holds even against strength 3 (`0x00269f30`) | |
| `0x2000000000` | never escapes when the grabber's power runs out | |
| `0x20000000000` | one hit cannot take health below a fraction of the maximum (`0x00265f70`); it then sets `0x10` | |
| `0x100000000000` | never picked as a target (tested by `0x00227d78` in the filter `0x00279410`; not `HuSetNoAutoLock`, which sets `0x8000000000`, a bit with no reader found) | |

**The flags a human starts with** come from its brain type (`Human_Init`, `0x00218008`, the stores at `0x00218cfc`
and `0x00218dac`; confirmed (code)). The word is zeroed, then:

| Brain type | Flags set | Also |
| --- | --- | --- |
| 0 (the player) | `0x2000000`, then `0x2` and `0x4` | cuff count `+0x378` = 0 |
| 3 (a Warrior) | `0x2` and **`0x4` (revivable)** | cuff count 0 |
| 1 (a cop) | `0x2000` and `0x40000` | cuff count 1 |
| 5 (a dealer) | `0x2000` and `0x40000` | |
| any other | `0x8` (woundable) with a 1-in-2 chance (`Random_IntBelow(2)` = 0), never for a class whose `+0x11b` is 13 | cuff count −1 |

`Human_ApplyClassFlags` (`0x00222c98`) then copies the class record's switches over bits `0x80`, `0x200`, `0x400`,
`0x200000`, `0x400000`, `0x800000000`, `0x1000000000` and `0x2000000000`. Giving a player back to the AI
(`Human_ReleasePlayer`, `0x0022a2a8`) clears `0x2`, `0x4` and `0x2000000` and sets `0x2` and `0x4` again for brain
type 0 or 3 (`0x2000` for type 1) before the class switches. So **every Warrior is revivable from the moment he is
made**; `Human_MakePlayer` sets `0x4` too, and the only other writer of the bit is the script binding `HuSetRevivable`
(`Human_SetRevivable`, `0x00235db0`). Every 64-bit store to human `+0xe0` in the executable was checked (174 `sd`,
no `sw`), and none other sets or clears `0x4`. Confirmed (runtime), PCSX2 2.9.94, `level80` checkpoint 1 (the "Let's
Go" lesson, before any script call): Vermin, Rembrandt and Ajax (brain type 3) had flags `0x7` in their low bits,
Cleon (type 0) `…0207`, Ajax's girl (type 4) `0x2000000009`.

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
   (`Hit_GetSideFromPoint` `0x00267338` via `Human_GetSideOf` `0x002672d0`; confirmed (code), cos 45° = 0.7071). The
   reaction's direction = (code direction + side) & 3.
4. The id is the s32 table at `0x00510798`, index strength × 16 + height × 4 + direction; a -1 entry (or an index above
   64) falls back to 272, the table's entry 10 (`Hit_PickReactionClip` `0x00266f38`; confirmed (code)). Dying uses
   `Hit_PickDyingReaction` (`0x00266fd8`; confirmed (code)): strength 2, and for a strength-2 entry half the time the
   `DIE` set 304-315 (id + 20), else the entry itself; a -1 entry or an index above 64 gives 292, the table's entry 42.

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
runtime](combat-reactions.md#being-hit-runtime)) (confirmed (runtime)): `S1` (`0x0a`) from in front gives 272 and from
the victim's left (61-71°) 275; `X1` (`0x09`) from its left gives 274; `SSS3` (`0x26`, a combo id, so strength 1) gives
276; the special 653 (`0x26`, strength 2) from the left gives 291 and a knockdown.

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
| `0x005104bc` / `0x005104c0` / `0x005104c4` | 4 / 6.25 / 9 | `CfgDistances` (`Cfg_SetCombatDistances`, `0x0023a630`), squared: 2, 2.5 and 3 m as `config_preload2.lua` sets them at boot (the executable's initial values 4 / 9 / 36, 2, 3 and 6 m, never apply in play); the stance's enter, swap and keep distances ([Fight stance](combat-targets.md#fight-stance)) |
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
the maximum per 20 s), `+0x05` = 50 the player's health percentage, `+0x06` = 115 the player's damage scale, byte
`+0x07` picks the cuffing hold's times, `+0x08` the mash gain factor, `+0x09` the tagging tuning, `+0x0a` the
lock-pick difficulty plus 1, `+0x0b` the stereo theft's turns and `+0x0c` the mugging parameters; `+0x0d` is never
read. Confirmed (runtime) for the bytes; the uses confirmed (code) at the functions below and on [Warrior
classes](../references/warrior-classes.md), which lists every byte's reader.

## Behaviour

### The dispatcher {#dispatch}

`Player_UpdateActions` (`0x0027c120`) runs every update for a pad-controlled human. Confirmed (code); in order:

1. **Block** if the fight test `0x00224f28` holds and R1 is held (mask 8), or if already blocking (`0x00223ad0`:
   state `0x8000` or `+0x08` `0x1000`) and the command is `0x1f` without a full rage meter, or 8 (L1 released). It
   clears the sprint, calls `0x002801e8`, sets state `0x8001`, starts rage on `0x1f` with a full meter
   (`0x0027ffa0`), and returns 0 on command 4 (R1 held), so nothing else runs while R1 is held. This is the sprint's
   "other clear" on [Characters](characters.md#sprint). R1 held is read from the pad record (`0x00147f98`, 0 when
   per-player `+0x19` is −1), so **an AI human never starts a block**: command 4 alone does nothing
   ([AI](ai.md#block)). The fight test fails while the human runs or sprints with record `+0x08` clear (gait 4 or
   5, `0x00223a60`, `0x00223a98`), so **R1 at a run starts no block**: the run goes on and R1 only turns the camera
   round ([Camera](camera.md#reverse)); the block starts the update the gait drops below 4. Confirmed (code);
   confirmed (runtime).
2. The chain (`0x00280630`), then the sprint (`0x0027d5a0`).
3. The state routes: mugging (`0x100`) → `Player_UpdateMugging`; being mugged (`0x200`) → release; a theft
   (`0x4000000`) → `Player_UpdateTheft`; `0x8000000` / `0x10000000` → nothing; `0x400` → `0x0027ec20` (tackling);
   `0x800` → `0x0027f1a0` (tackled); `0x18000000000` → `0x00284140`; grabbing (`0xc0`) → `Player_UpdateGrabbing`;
   grabbed (`0x30`) → `0x0027fd68` ([Grabbed](combat-grabs.md#grabbed)); a power move (`0x1000`) → `0x0027df38`;
   `0x2000` → `0x0027e018`; `0x200000` → `0x0027e040`.
4. The commands: `0x36` → `0x0027de78`; `0x37`, `0x38` → `0x00287fe0`; triangle `0xb` with human `+0x5b8` →
   `0x002811f0`; `0x33` → `0x00281188`; `0x32` → `0x002832c8`; `0x22` or `0x39` → `0x00288838`; `0x22`, `0x30`,
   `0x23`, `0x24` → `0x00287730`; **`0x20` → charge**; **`0x21` → dive**; **`0xf` → square**; **`0x10` → cross**
   (weapon types 4, 5, 6 → `0x002880d8` instead); **`0xd`, `0xe` → grab or tackle**; `0x27` → `0x00286ba8`; 7 (L1
   pressed) → `0x0027da10`; 6 (L1 held) → `0x0027dc00`; 8 → `0x00227b98` / `0x00227a90` ([Targets](combat-targets.md#targets));
   **3 → the AI's grab or tackle counter** (`0x0027d6e0`), only when per-player `+0x1b` is 0 ([AI](ai.md#block)).
   Commands `0x30`-`0x39` are not made by the street's tables; scripts or weapons make them (inferred). `0x00288838`
   acts only for armed weapon types; `0x30` matches no branch of `0x00287730` (inferred: unused there).

### Attacks and chains {#attacks}

**Square** (`Player_Square`, `0x00286cc8`) takes a target from `Player_PickTarget(0x40000000, h)` (in the fight stance
it keeps the current target within 2.5 m, `0x005104c0`, and searches only without one or beyond; cross always searches,
[Combat moves](combat-moves.md#targeting)), then, confirmed
(code):

- a grounded target → 193 (`0x00261a08`); a tackled one → 212; a grabbed one → 120: these three are tested only on
  the fight-stance path, after the strafes and snaps; outside a stance the run and walk attacks test only the low
  target (193), and cross has its own 194 and 661 ([Combat moves](combat-moves.md#cross));
- a breakable object → `Player_ObjectAttack` ([Breakables](combat-targets.md#breakables));
- at gait 4 (run) with record `+0x08` clear and the stick above 0.95 → 24, from a run; at gait 1-3 with the stick at
  0.12 or more → 23, from a walk. These come **before** the snap (gait tests `0x00223a30`-`0x00223a60`), but the
  whole block, run and walk alike (and the `0x00510270` attack 22), is skipped **in a fight stance** (record `+0x00`
  & `0x3`, `0x00228340`) or when `0x0051031c` is set for a pad-controlled human (0 in the executable's data).
  Confirmed (code) at `0x00286cc8`; cross (`0x00287a18`) skips its run and walk attacks on the same test. So a snap
  comes from a player in a [fight stance](combat-targets.md#fight-stance) (a locked-on combat walk is gait 3, but the
  stance skips the walk attack) or from one standing (or sprinting) outside it: the stick pushed from rest and square
  pressed while he has not yet started to walk;
- the stick above 0.95 (the per-player record's buffered magnitude) and more than 45° from the facing → a **snap**
  attack, 25 right, 27 left, 29 back (`0x00264460`), but only **with a target there**: `0x0027aa38(h, 0x80)` takes the
  nearest human within **2.0 m** (`0x00227598`) and within **45° of the stick's direction** (π/4 written to
  `0x0051096c`), height difference at most 2.0 (`0x00510970`), not friendly (`Human_IsFriendly`, `0x00222a90`; there is
  no line-of-sight test), of another gang, not down, not grabbed or tackled, targetable (filter `TargetFilter_Enemy`
  `0x00279410`, in `Player_FindSnapTarget` `0x0027aa38`). With `CfgSnap` on (`0x005102b4` = 1, the default), none found,
  or the one found being the current target (`0x00226e60`), means no snap: square goes on down this list (usually `S1`
  at the current target). With one, `0x00264460` turns the player onto it over 0.1 s (`Attack_SteerToTarget`) when it is
  within the snap's far range. Confirmed (code); confirmed (runtime) in `level99`'s lesson 7 with the stick at full
  deflection one update before square: 27, 29 and 25 started and hit bums 0.8-1.4 m away, and a square with the stick at
  the bum in front (the current target) gave 12;
- otherwise 12, `S1`. Strafe ids 31-33 come from the combat-walk path (not tested).

**Cross** (`0x00287a18`) starts 11, `X1`, on command `0x10`; at a run (gait 4, or 5, with record `+0x08` clear) it
plays the run attack 24 and at a walk the walk attack 23, as square does, but has no snaps. Confirmed (code), confirmed
(runtime). With a weapon in hand both buttons follow [Moving attacks with something in hand](combat-throws.md#armed-moves).

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
release, command `0x10`; a buffered press plays when the window opens). The hits of 12-19 and 11-14 were measured again
with the fixed recorder ([Recording a trace](../guides/research-workflow.md#hooks)), in slot 6 with PoizoCiv placed 0.95
m ahead, facing the player, and then left free (he walks in). Each hit is the update whose sample first shows the
damage. The values held, except `X1`, which hit on update 8 in one run and 7 in two. Those three runs had the same input
up to the hit, but the player's turn toward the target differed between them, so something outside the input varies. A
target standing still further out is hit later (`X1` on 10 from 1.0-1.6 m, [Where the attacker
stands](combat-moves.md#reach)):

| Id | Hit | Window `0x2` opens | End `0x4` | Idle again |
| --- | --- | --- | --- | --- |
| 12 `S1` | 2 | 6 | 15 | 20 |
| 11 `X1` | 8 (7 in two of three runs) | 10 | 20 | 30 |
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
([Moving attacks with something in hand](combat-throws.md#armed-moves)).

**The charge**: **L2 held, then cross pressed** (command `0x20`) at gait 4 with record `+0x08` clear, or at gait 5
(a sprint), plays 0 (`gen_charge_shoulder`) for about **0.9 s** at about 7.45 m/s. **The dive**: L2 held, then
square pressed (`0x21`), plays 1 for about **2 s**. Confirmed (code) at `0x0027d800` / `0x0027d900`, confirmed
(runtime). At a walk or standing the combination does nothing (the cross or square press under it is overwritten,
inferred from the matching order).

#### Where the charge and the dive aim {#charge-aim}

The two differ. Confirmed (code) unless marked:

- **The search** (`Attack_FindNearestInReach`, `0x0027b058`), used by both: it writes the aim heading (the
  camera-turned stick's angle above 0.01, else the facing; `Player_GetAimHeading` `0x0021d1a0`) to record `+0xd4`,
  sets the cone `0x0051096c` to 0.9425 rad (54° each side) and writes the clip's **reach** to record `+0xd8`, which
  `Humans_FindInRange` (`0x00227598`) and `ObjectList_FindInRange` (`0x0039ab20`) use as a **minimum** distance. It
  then takes the humans **between the reach and the far range** inside the cone that `TargetFilter_EnemyStanding`
  (`0x00279568`) accepts; with none, the strikeable world objects in the same band, then the glass panes; the nearest
  wins. There is **no any-angle or wide pass**, unlike `Player_PickTarget`. For Rembrandt the band is
  **3.29-4.11 m** for the charge (id 0) and **2.22-3.00 m** for the dive (id 1) ([Frame data](combat-moves.md#frame-data)).
- **The charge** (`Player_StartCharge(h, 0)`, `0x00264a80`) **neither steers nor sets a target.** It reads the current
  target; for a human whose brain `+0x4` is 0 (the player; inferred) with none, it runs the search, and whichever human
  it has is only warned (`Human_WarnOfAttack`, `0x0021d5c0`). The clip then runs straight along the facing. Its
  record `+0x08` bit `0x1000000` does not make the human busy, so **the stick still turns the charger**: the
  locomotion's turn runs while its velocity is zeroed ([Tasks](tasks.md#locomotion-gate)), and the clip's root motion
  follows the new facing.
- **The dive** (`Player_StartDive`, `0x00264878`) clears the sprint state `0x1000000`, and for the player always
  searches afresh and **sets the result as the target** (`Human_SetTargetObject`, `0x00226cd0`; none found clears
  it, unless a pad player is locked). With a target it steers with `Attack_SteerToTarget` (`0x002761c8`, kind 1)
  over **0.1 s** (`0x3dcccccd`): turned and slid onto the standing point at the reach, in 3 updates. Its record
  `+0x08` `0x400000` makes the human busy, so the stick does nothing for the rest of the dive.

At runtime (confirmed (runtime), PCSX2 2.9.94, copies of slot 6; PoizoCiv, brain off, placed relative to Rembrandt on
the update the press was read, after a run of 15 updates at stick 100 % with L2 held; square or cross at update 20):

| Run | Target at the press | Result |
| --- | --- | --- |
| dive | 2.62 m, 31° right | target set; turned 34.8° in **3 updates** (11.6° each), speed 16.2, 14.5, 14.7 m/s against 11.7, 9.5, 9.6 without a target, distance 2.62 → 1.26 m in 3 updates |
| dive | 2.60 m, 40° left | target set; turned 36.3° the other way in 3 updates, 2.60 → 1.40 m |
| dive | 1.62 m at 32°, 2.64 m at 71°, or 3.40 m at 30° (inside the reach, outside the cone, beyond the far range) | no target, no turn: straight on |
| dive, stick turned about 42° right from its 4th update | none | no turn from the stick |
| charge | 3.70 m at 30° right, or 2.01 m at 31° | no target, no turn: straight past him at 7.45 m/s (closest 1.84 m) |
| charge, stick turned about 42° right from its 4th update | none | the heading turned 1.4, 2.3, 2.9, 3.2, 3.3, 3.3, 3.2 … ° per update, easing off (38° in 15 updates), and the path followed the facing one update later |

Every dive, with or without a target, also turned the body's heading by **+84.4°** over its updates k8 to k12 (16.9°
per update) while the travel kept its line; why was not traced (inferred: the clip's own root rotation).

**Square at a sprint** has no attack of its own: the moving attacks test the gait exactly (4 → 24, 1-3 → 23), so at
gait 5 square falls through to the standing path (a fight stance and `S1`). Confirmed (code) at `0x00286cc8`; not
tried at runtime. With `0x00510270` set (0 here) square outside a fight stance plays 22 instead.

**Specials** (`Player_Special`, `0x00287730`), confirmed (code): **cross + square** (`0x22`) outside a grab plays the
special 653 (645 raging); **circle + cross** (`0x23`) is the **strong grapple**, a grab whose connecting clip is
the strike 657 (649 raging) and which ends in the hold ([Strong grapple](combat-grabs.md#strong-grapple)); **circle + triangle**
(`0x24`) sprays a tag (664) where the player may tag
(`0x00222818`). The id is 645 or 653 + 4 × variant + 0 / 2 by side (`0x00263c90`). A special that is not paired
needs and spends 0.25 of the power meter. At runtime (confirmed (runtime)): cross held, square pressed 40 ms later
played 653, power 400 → 300, the civilian lost 6 and fell (291, grounded 196, up with 199 2.0 s after the hit);
circle + cross at a victim getting up played 359 and then `X1`.

#### How a moving attack strikes {#moving-strikes}

A moving attack has no single hit update. Its clip's events switch **strike shapes** on and off, and while any is on
`Human_TestStrikes` (`0x0033f110`) tests them **every update, after the move**, against every body near the human:
the first shape that meets a body calls `Strike_Contact` (`0x0021b290`) for it. "Meets" is a **sweep**: each shape
is moved from its pose at the last update to its pose now through the physics sweep table, so only the spheres can
reach a box (a segment never does), and a shape just switched on is tested with no move
([Physics: Unit 5](physics-kernels.md#u4b-how-human-teststrikes-builds-the-sweep-0), confirmed (code)). The reach is the
posed body itself: there is no reach or height figure of its own. Confirmed (code) at the addresses below; the timings are
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
  target that carry flag `0x4`, or the target's capsule when the attacker's clip has flag `0x10000`
  ([The capsule strike](#capsule-strike)); an object's
  against its own shapes; a shape touching the level mesh calls `Strike_Contact` with no object (a material impact
  sound only). `Strike_Contact` then does what a hit on that thing does: damage from the clip's
  [Anim Range List](#damage-table) value for a human, message 1 of kind 2 for an object
  ([World objects](objects.md#door-break)), and for a pane its break. A body struck is put on the striking body's
  contact list (body vtable `+0x34` / `+0x44`) and not struck again while the shapes stay on (inferred from the list's
  use; what empties it is `0x00342168`, run when the shapes go off). The same overlap also runs from the human's
  move: a sweep by a strike shape meets bodies with flag `0x20`, and `Human_OnContact` (`0x00219d50`) calls
  `Strike_Contact` once per body.
- **Standing attacks strike the same way.** There is no other melee damage path: `Strike_Contact` is reached only
  through the strike test and `Human_OnContact`, and message 1 (`Human_HandleMessage`'s other `Human_DealDamage` call)
  is a damage message sent from outside the melee code (inferred). Every chain attack's clip carries a strike window
  (event `0xf` / `0x10`): `S1` (`combo1a_S_hi_b`) the left hand 19 from frame 1 to 8, `X1` (`combo1b_X_hi_r`) the right
  hand 25 from 5 to 9, `SS2` both left shapes 18 and 19 from 2 to 6, `XX2` the right forearm and hand from 7 to 11,
  `SSS3` the right hand from 6 to 12 (Rembrandt's clips; the generic `S1` and `X1`, `combo2a_S_hi` and `combo2b_X_lo`,
  hold 19 from 0 to 5 and 25 from 4 to 8); the chain-window event `0x2c` falls inside each window. So a standing attack
  damages a human only if the posed hand or forearm overlaps the target's spine or head shape during that
  window (the two `0x10000` sweeps test the target's capsule instead, [below](#capsule-strike)). Confirmed (code) at
  `0x0021b290` and its callers; the events read from the disc's clips.
- **Who is tested** (`Human_TestStrikes`, `0x0033f110`, confirmed (code)): every body the physics box query
  (`IPhysics_QueryBox`, all masks) returns around the human, **foes and allies alike**, except a body of the same
  body group (`+0x3c`, when not 0), one already on the striking body's contact list, a body with flag `0x8` whose
  owner has `0x4000000`, and one **behind the level**: a ray from 1 m above the human's root to the body's centre
  (`WorldManager_RayCast`, mask `0x48`, or `0x8` for a body with `0x40`) must hit nothing. The friend test comes
  after, in `Strike_Contact`: against a friend (`Human_IsFriendly`) the hit event goes out with its hostile byte
  (`+0x11`) clear (the "may react" value `+0x11a` is unchanged), a Warrior (brain type 3) struck by a player who has
  no target may complain (speech command `0x58`), and `Human_ApplyPendingDamage` drops the damage unless both are
  players ([Damage](combat-reactions.md#damage), step 3) but still plays the reaction ([Teammates](combat-targets.md#teammates)).
- **When a clip is cut** before its off event, the shapes still go off: every animation task gives its clip back
  through `CharacterInstance_ReleaseClip` (`0x00175120`) when it ends, is replaced or chains on, and for a clip with
  descriptor flag **`0x4`** that runs `Human_StrikeAllOff` (all ten shapes and the capsule's `0x2` off). Flag `0x4` is
  exactly "has strike events": of the disc's 1,752 clips, all 221 with a type `0xf` or `0x13` event carry it, plus
  `falling_frm_brk_hit` without one ([Clip flags](animation.md#clip-flags)). Explicit offs as well: dropping a type-5
  overlay (`Human_DropOverlayTask` `0x00229620`: a play-anim action's abort, `Climb_Start`, the fidget, taunt and
  look-around stops), `Attack_StartPaired` (`Human_EndOverlayAndStrikes` `0x002628c0`), the wheelchair below 4.5 m/s,
  and the human's body leaving the physics (`IPhysics_RemoveHumanBody`). A clip faded out keeps its task until the
  fade ends, so its shapes stay on through the fade unless its own off event comes first (inferred from the task
  release). Confirmed (code) at the addresses cited; the flags read from the disc.
- **Which shapes, and when** (updates of 1/30 s counted from the update that starts the move, update 0, on which its
  held flag and clip id first show and its clip time is still 0; slot 10, stick 100 % straight ahead, needed for gait 4):

  | Move | Shapes switched on | On | Off | Clip | Events (clip frames, of) |
  | --- | --- | --- | --- | --- | --- |
  | charge 0 (L2 + cross) | all ten bone shapes and the capsule's flag (`0x11` → `0x13`) | 4 | 17 (all off at once) | 27 updates, 7.45 m/s throughout in the open | `gen_charge_shoulder`: `0x13` at 3, `0x14` at 13, of 20 |
  | dive 1 (L2 + square) | the same ten | 2 | 24 | | `gen_dive`: `0x13` at 2, `0x14` at 18, of 46 |
  | run attack 24 (cross at gait 4) | right forearm and hand (24, 25) | 2 | 8 | 21 | `gen_run_strike`: on at 2, off at 6, of 16 |
  | walk attack 23 | left forearm and hand (18, 19) | 5 | 11 | 24 | `gen_walk_strike`: on at 4, off at 8, of 18 |
  | run attack with a weapon 501 | the weapon's spheres (bone 25's event) | | | | `gen_run_1hand_weapon_atk`: on at 2, off at 6, of 16 |

  The clips play slower than 30 frames a second (the charge's 20 frames over 27 updates), which turns the events'
  frames into the updates measured. **The moving attacks follow the same event rule as the standing ones**, with no
  head start: confirmed (runtime), PCSX2 2.9.94, slot 10, the run attack 24 with the top animation task and its cursor
  read every update. The move is pushed by the dispatcher (step 9 of [Humans_Update](tasks.md#humans-update)) on
  update 0, as a type-5 layer (`Player_StartCharge` `0x00264a80`, `AnimTask5_Construct`: fade-in 0.3 s, fade-out 0, so
  it plays at once) whose cursor starts at frame 0 (`AnimCursor_Bind` sets the time to 0): no start offset, no
  hand-over from the gait blend, no extra advance. It is first advanced on update 1 (animation step 6): cursor time
  0.025 s, frame 1 (rate 0.75), then 0.050 s / frame 2 on update 2, when the frame-2 events fire (the cursor's event
  index went 1 → 5) and the shapes go on; frame 6 on update 8 turns them off. Confirmed (code): `AnimCursor_Advance`
  (`0x001044a0`) moves to frame `uint((t + dt) × 30 + 0.5)`, and `Anim_FireFrameEvents` fires every pending event
  whose frame is at most that frame plus the cursor's offset `+0x128` (0 for this clip, read at runtime). The **earlier
  table** (on 1 / off 7 for 24, 4 / 10 for 23, 3 / 16 for the charge) was one update low throughout: the recorder tags
  a hook entry made during update *N* with *N* − 1 (it drains the hook ring before moving its step on; confirmed
  (code), `repo:python/src/coney_tools/recorder.py`). Its values here are corrected.
  So the **charge strikes with its whole body from its 4th update to its 16th**,
  about 13 × 0.25 m = 3.2 m of its 6.7 m; the run attack strikes only for 6 updates near its start.
- **The fence (slot 10).** Rembrandt starts 4.8 m from `level99`'s wooden fence (centre line y −6.67) with the stick at
  100 % straight at it and L2 held; cross at gait 4 started the charge (clip 0, record `+0x08` `0x1000000`). On its 11th
  update (k10), his root 0.55 m from the fence's line, `Strike_Contact` came from the strike test for the fence; the
  fence took the hit with 10 hitpoints and broke (kind 2: 16). The dive broke it the same way on its 8th update (k7),
  0.74 m from the line (record `+0x08` `0x400000`). The walk attack 23, pressed while walking at the fence (stick 50 %),
  also broke it on its 8th update (k7), 0.50 m from the line (its record `+0x08` holds `0x1000000` too, so kind 2). The
  run attack 24 did not: its shapes were off 1.1 m short, and he ran into the fence and slid along it at 1.07 m/s.
- **The strike does not end the charge.** The shapes stay on and the clip runs its 27 updates whether or not anything
  was struck. What stopped him at the fence was the collision: 1 update after the strike he stopped dead 0.42 m from the
  fence's line, with no slide, and did not move again before the clip ended (15 updates later). The dive stopped 2
  updates after its strike, also 0.42 m from the line. Why, from the code: the break turns the fence's level triangles
  off but leaves the hidden fence's **collision body** until the object is removed on its next 60-tick update
  ([Barriers](objects.md#barriers); 10 updates after the hit in the charge run, 11 in the dive's, 2 in the walk
  attack's). The charge's sweep meets that body (the move's mask holds `0x4` on the ground, `0x0033d498`), and
  `Human_OnContact` (`0x00219d50`) answers a world object that is not a `powerup_item` with `0x20001`: the slide
  response of the sweep (`0x0033d9d8`, type 1), which removes the part of the velocity going into the contact's normal
  (`0x0033d870`) from both velocities the sweep is given (this step's and, inferred, the one kept). Head-on, as here,
  nothing is left, so he stops dead; the run attack, which met the fence at an angle, kept its sideways part and slid.
  Inferred from the code (the response types are confirmed (code); that the fence's body has `0x4` follows from the
  stop). Struck first in the same contact (strike shapes on and the body flagged `0x20`), the fence is not let through:
  only a body without `0x4` gets `0x20000` (ignored) after a strike, as a glass pane's box does. The last updates of the
  clip after the removal were not compared with the clip's own root motion. In the open the same input ran the whole
  clip at 7.45 m/s and the run (410) followed.
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
  `P3.FenceBroken`, and 15 updates later the player stood at (18.5, −16.3), heading 178°, beyond the fence. That is 15
  updates after the removal in every run, so the time from the strike to the placement follows the removal: 25 updates
  in the charge run (32 in an earlier walk-attack run), 26 in the dive run, 17 in the later walk-attack run.

Confirmed (runtime), PCSX2 2.9.94, copies of slot 10, scenario
[`charge_fence`](repo:research/traces/scenarios/charge_fence.toml) (hooks `strike-shape`, `strike-contact`,
`barrier-hit` and `object-remove`); the open-ground run turned the stick to 100 % straight back. The update numbers come
from runs recorded again on 2026-10-07 with the fixed recorder ([Recording a
trace](../guides/research-workflow.md#hooks)); the dive (square for cross) and the walk attack (stick 50 %, cross at
update 85) are variants of its input. The strike shapes went on and off on the updates the frame tables give (charge
4-17, dive 2-24, walk attack 5-11, [Combat moves](combat-moves.md#frame-data)).

**In Coney** ([`human/strike_shapes.h`](repo:src/human/strike_shapes.h)): the clip events switch the ten shapes, which
are posed on the drawn skeleton every update, and each human's strike test runs after its move against the spine and
head of the humans it fights and against the doors' enabled triangles and the panes' bodies. Every free attack hits a
human this way, each shape tested along its move since the last update (from where it was posed the update before, none
on its first update), as [where the attacker stands](combat-moves.md#reach) gives; the moves in a hold, and a body
without shapes, still land at the measured hit update. The doors and panes are met where the shapes stand. A charge
whose sweep loses more than half its move to a wall stops dead until its clip ends, and a broken barrier's triangles
stand for its body until its removal. In Coney's play-through the charge strikes the fence on its 11th update, 1.06 m
from the line, and stops 0.51 m from it 3 updates later; the original also strikes on its 11th update, but at 0.55 m,
and stops 1 update later at 0.42 m. The AI does not charge a human yet (its attack pick takes only the square and cross
strikes and the snap). The player's sprint charge and dive into a human are tested in play
([`disc_sprint_attacks_test.cpp`](repo:tests/platform/disc_sprint_attacks_test.cpp): level99, a spawned fighter
9 m ahead, the stick at 98 %): each strikes him once and plays its whole clip (27 and 62 updates in Coney, 27 and
61 in the original) with no reaction of the attacker's ([Moves a hit cannot stop](combat-reactions.md#protected-moves)).
Not made yet: the level mesh's impact sound, a car struck by the shapes, and the weapon spheres of a held object.

#### The capsule strike {#capsule-strike}

A clip with flag `0x10000` ([Clip flags](animation.md#clip-flags)) strikes a human target's **capsule**, not its
spine and head. The flag is read from the **attacker's** clip: `Human_TestStrikes` (`0x0033f110`) calls
`Anim_HitsWithCapsule(attacker, Human_GetAnimId(attacker))` (`0x00101b88` at `0x0033f998`, its only caller), which is
clip `+0x44` & `0x10000` of the clip the attacker plays (`Human_FindAnimQuiet` `0x00221bf0`). Confirmed (code):

1. The test applies only to a struck body of a **human** (type mask, body `+0x44` & `0x40`), after the checks of *Who is
   tested* under [How a moving attack strikes](#moving-strikes). It also needs the striking body to have a shape on
   (`+0xc0`), the struck body's flags `+0x40` & `0x30`, and the attacker's vtable `+0x10c` to accept the target. An
   object's body is struck on its own shapes whatever the flag.
2. Without the flag: each of the attacker's ten bone shapes that is enabled (`+0x32` & `0x1`) and striking (`& 0x2`)
   is tested against each of the target's shapes with `0x4`, the spine and the head.
3. With the flag: each such shape is tested against the target body's **first shape** (body `+0x30`), its capsule.
   No flag of the capsule is read.
4. The pair goes through the [sweep table](physics.md#dispatch), row = the strike shape's type, column 3:
    - a **segment** (shin 29 / 32, forearms) uses the static overlap `0x00345d38` → `0x00346fb8`. It takes the closest
      points of the bone segment and the capsule's axis (from its base straight up for its height,
      `Segment_ClosestPoints` `0x0034ad50`). It hits when their distance is at most the segment's radius plus the
      capsule's radius.
    - a **sphere** (foot 30 / 33, hands) uses `0x00345670` → `Sweep_SphereCapsule` (`0x00349a60`): the sphere moved
      by its displacement this update, against the upright capsule, with the two radii added; a hit at a fraction
      from 0 to 1 counts.
5. The first hit ends the test of that body: `Strike_Contact` (owner vtable `+0x104`) with the contact (`+0x04` the
   strike shape, `+0x40` the target, `+0x44` the target shape), and the target goes on the contact list.

**The capsule** (confirmed (code), `PhysicsCapsule_Pose` `0x00343518` and `PhysicsBody_ApplyHumanScale`
`0x00341f28`; see also [Physics](physics.md#human-body)):

- **Radius:** `+0x40` = 0.35 × the human's scale (`Human_GetCapsuleRadius` `0x00219860`): 0.3395 m at scale 0.97.
  The walking sweep's body factor `+0x60` (1.4286 for the player, [Characters](characters.md#walls)) is not applied
  here.
- **Base:** `+0x50`, at the human's position (the feet), each update.
- **Height:** `+0x44`, the height of the head bone (6) above the feet + 0.2 × scale, so about 1.8 m standing.
  It is shorter in some states, by the rules on the Physics page; 0.35 × scale when cuffed; never under 0.2 × scale.

So a strike shape at shin or foot height (0.1-0.4 m) that comes within its radius plus 0.34 m of the target's
vertical axis hits it. A strike shape tested against the spine (about 1.0 m up) and head (about 1.6 m) never could.

**At runtime** (confirmed (runtime), PCSX2 2.9.94, a copy of slot 6, 2026-10-08). Rembrandt was given a full rage
meter (78) and rage started with L1 + R1 (clip 643, updates 15-74). PoizoCiv stood 1.0 m ahead, its brain and stick
off. Cross + square at update 85 played **645**. The shin 29 and foot 30 went on at k15 (update 102) and off at k35
(update 122). On update 102 `Strike_Contact` ran once with the strike shape of type 4, bone 29 (the shin segment) and
the target shape of **type 3, bone `0xff`** (PoizoCiv's capsule). PoizoCiv lost 10 health (600 to 590), the table's
10 ([combat-moves](combat-moves.md)), and played reaction 285. With PoizoCiv 1.6 m ahead the same 645 missed: the
target was 1.63-1.74 m away while the shapes were on.

**Clips with the flag** (the disc's clip headers, `+0x44`): only `gen_rage_sweep` (645 / 647, the rage special of
`warr_fo_header` and `warr_re_header`; other sets put `gen_spinpunch`, `gen_360clearkick` or other clips in 645) and
`gen_sweep` (653 / 655 of `moon_lt_header`) carry `0x10000`, both with `0x10004`. Every other strike clip is tested
against the spine and head.

**In Coney** (`Human::testStrikes`, `Human::struckCapsule`, [`human/human.cpp`](repo:src/human/human.cpp)): the clip
decoder keeps the descriptor's flags, and a clip with `0x10000` tests its bone shapes against the target's capsule as
above: a segment where it stands, a sphere along its path this update. **Coney choice**: the capsule always has the
standing height (head + 0.2); the shorter heights of some states are not built. The unit test plays 645 at a bum
(flagged never to be a target) 1.0 m away, which it hits, and 1.6 m away, which it misses. The disc check finds the
two sweeps and no other clip with the flag.

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

### Attack warnings: who hears an attack coming {#attack-warnings}

**One human per warning: the target.** `Human_WarnOfAttack(victim, attacker, id)` (`0x0021d5c0`) sends event `0x10`
to the one victim it is given, and only when that victim can see the attacker (`Human_CanSeeHuman`, `0x002223e8`,
with the victim's field of view `+0x12c` and sight range, or 2 m when the attacker's brain `+0x2d4` is set, then a
line of sight); it also makes the victim drop carried props unless cuffed. Nothing in the game loops over nearby
humans to warn them: every one of its 23 call sites passes a single human, the move's target. Confirmed (code):

- **`Attack_Start`** (`0x002625a8`, the call at `0x00262674`) warns its current target (`Human_GetTarget`), and only
  when that target is within the clip's far range; out of range it turns to the target and warns no one; with no
  target, no one.
- **The square path** (`Player_Square`, `0x00286cc8`; every call address below is a `jal` to `0x0021d5c0`,
  confirmed (code)): each branch warns the human it is about to attack. The call at
  `0x0028758c` (the standing square in a fight stance, before the `S1` start at `0x002876f0`) warns the target it has
  just kept or re-picked (`Player_PickTarget`) before testing for a grounded, busy or grabbed target; the snap attacks
  warn their snap target (`0x00287454`, `0x002874c4`, `0x002874fc`); the run and walk charges and the
  run-up attacks warn the picked target (`0x00286e3c`, `0x00286fe0`, `0x002870d0`, `0x00287174`). The standing square
  then reaches `Attack_Start`, so its one target is warned twice in the same update (brain `+0x200` counts 2).
- The rest pass the move's target too (confirmed (code): the 23 call sites are in these functions):
  `Player_Cross` (`0x00287b74`, `0x00287cc8`), the charge (`0x00264b30`), grabs
  and tackles (`Player_GrabOrTackle`, `Player_UpdateGrabbing`), the mug, the tandem and mount starts, the melee throw
  and the throw's release.

**Coney choice, not a finding:** the running dive is not among the call sites above, so whether the original's dive
warns anyone is not known; Coney's dive warns the one human its own search dives at (`Fighter::nearestInReach`,
`repo:src/human/fighter.h`), and no one when it finds none.

**What others hear.** Only indirectly: the warned human's shared handler (`0x00292d80`) makes the gangs enemies and
sends a **help call** within 20 m (`Gang_BroadcastHelpCall`, `0x00293640`: event `0x14` through `AI_AlertNearby`,
naming the attacker) when the warning passes its own sight test ([AI: events](ai-code.md#brain-events)). A human
who is not the target never gets `0x10`, so never counts `+0x200`, blocks or counters for it.

**The `level5` bosses** ([AI: Diego and Vargas](ai-gangs.md#boss-diego-vargas)). `BigBrawlerGoal_OnAttackWarning`
(`0x002e8858`) acts on `0x10` only in state 2 and not while tired, and then: when the goal holds actions and the front
one is of type 1 (`MoveTo`) or 2, **clears the action queue**; counters an escapable grab or tackle from his current
enemy (command 3); makes the attacker his target when he has no enemy or his enemy is an AI human (a player enemy is
kept); and, with the attacker within 1.5 m, 10 % health or more, no held flag `0x2000` and the queue cleared, sets the
no-reaction flag `0x800`, says line 11 or 14 and plays 653 (the shove). Confirmed (code). So in the original only the
boss being attacked does any of this. Coney once announced every attack start to everyone the attacker could fight;
then every attack the player started at one boss also reached the other, clearing the other's queued move each
time, so the boss who was not being hit rarely closed in (inferred). Since issue #372 Coney warns the one target
(`Fighter::warnTarget`, `repo:src/human/fighter.h`), so the second boss is free to attack, as in the original.
That broadcast also made a bystander answer a stranger's swing: at `level34` checkpoint 5 the player, warned of a
hurt Rogue's swing at a Fury 20 m off, made the Warriors enemies of the Rogues and called his crew over
([level34 fix 14](scripting.md#level34-fixes)).

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
  it opens the duck's **counter window**. The rise 199 carries the same event, so a downed player counters out of
  his rise by the same path ([Down and under attack](combat-reactions.md#ground-escape)). The handler (`0x00245920`,
  case `0xa5`), when `0x00225498` allows it and the human is not both in state `0x100000` and flagged `0x8000000` (human
  `+0xe0`), asks whether to counter: a player when its current command (per-player `+0x20`, `0x00147ef8`) is one
  of `0xf`, `0x11`, `0x15` (square) or `0x10`, `0x12`, `0x16` (cross) (`0x0027b988`), so square or cross pressed or
  held during the window; an AI human asks its reaction goal or top goal (`0x0028c6a8`, goal types `0x17`, `0x1b`
  and `0x85`). Yes sets record `+0x14` = **`0xe`**.
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
(runtime), [Being hit](combat-reactions.md#being-hit-runtime)): Rembrandt (`warr_re_cv`) has `0x24` on 11 `X1` (3), 13
`XX2` (5) and 14 `XS2` (4), and `0x26` on 12, 15, 16, 17, 19, 20, 653 and 655; the civilian (`civl_co_ma1`) has `0x24`
on 17 `SSX3` (3) and 653 / 655 (14), and `0x26` on 11-16, 19 and 24. So which attacks a block ducks is a property of the
attacker's model, not of the attack.

### Grab and tackle {#grab}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#grab).

### In the grab {#grabbing}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#grabbing).

#### Rembrandt's power move and its spray can {#power-move-props}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#power-move-props).

### Strong grapple {#strong-grapple}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#strong-grapple).

### Posing a grab {#grab-posing}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#grab-posing).

### Grab pose at runtime {#grab-pose-runtime}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#grab-pose-runtime).

### Moving a grab, and the mount {#grab-turn}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#grab-turn).

#### The hold walk: clips, speed and turning {#hold-walk}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#hold-walk).

### The mount {#mount}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#mount).

#### The mount's power strike ends the mount {#mount-power}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#mount-power).

### Grabbed, and breaking free {#grabbed}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#grabbed).

### Breaking a pair from outside {#pair-break}

Moved to [Combat: grabs, tackles, the hold and the mount](combat-grabs.md#pair-break).

### Throws {#throws}

Moved to [Combat: throws, the bat and mugging](combat-throws.md#throws).

#### Where a throw goes {#throw-direction}

Moved to [Combat: throws, the bat and mugging](combat-throws.md#throw-direction).

### A bat in hand {#bat}

Moved to [Combat: throws, the bat and mugging](combat-throws.md#bat).

#### Moving attacks with something in hand {#armed-moves}

Moved to [Combat: throws, the bat and mugging](combat-throws.md#armed-moves).

### Mugging {#mugging}

Moved to [Combat: throws, the bat and mugging](combat-throws.md#mugging).

#### Mug difficulty: who is hard to mug {#mug-difficulty}

Moved to [Combat: throws, the bat and mugging](combat-throws.md#mug-difficulty).

#### The other stick games: mug meter modes 1-3 {#other-stick-games}

Moved to [Combat: throws, the bat and mugging](combat-throws.md#other-stick-games).

#### The cuffing hold {#cuffing-hold}

Moved to [Combat: throws, the bat and mugging](combat-throws.md#cuffing-hold).

### Damage, health and reactions {#damage}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#damage).

### Attacking a busy target {#busy-target}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#busy-target).

#### Moves a hit cannot stop: the dive, the charge and the paired moves {#protected-moves}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#protected-moves).

### Reactions: stun, knockdown and getting up {#reactions}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#reactions).

#### Scripted knockdowns, stuns and hits {#scripted-knockdowns}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#scripted-knockdowns).

### Down and under attack: getting up and countering {#ground-escape}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#ground-escape).

### Knocked out, and the mission failing {#defeat}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#defeat).

#### Who sets and clears the knocked-out bit {#knocked-out-bit}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#knocked-out-bit).

#### Who can help {#defeat-helpers}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#defeat-helpers).

### Being hit, at runtime {#being-hit-runtime}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#being-hit-runtime).

### Hit blood {#hit-blood}

Moved to [Combat: damage, reactions and defeat](combat-reactions.md#hit-blood).

### Power meter {#power-meter}

Moved to [Combat: the power meter, rage and reviving](combat-rage.md#power-meter).

### Rage {#rage}

Moved to [Combat: the power meter, rage and reviving](combat-rage.md#rage).

#### The flash during a move {#flash-mid-move}

Moved to [Combat: the power meter, rage and reviving](combat-rage.md#flash-mid-move).

#### Everything rage does {#rage-effects}

Moved to [Combat: the power meter, rage and reviving](combat-rage.md#rage-effects).

#### Dropping what is held when hit {#keep-weapon}

Moved to [Combat: the power meter, rage and reviving](combat-rage.md#keep-weapon).

### Reviving a downed Warrior {#revive}

Moved to [Combat: the power meter, rage and reviving](combat-rage.md#revive).

#### At runtime {#revive-runtime}

Moved to [Combat: the power meter, rage and reviving](combat-rage.md#revive-runtime).

### Target selection {#targets}

Moved to [Combat: targets, AI attacks and breakables](combat-targets.md#targets).

#### Teammates: what the player may do to his own crew {#teammates}

Moved to [Combat: targets, AI attacks and breakables](combat-targets.md#teammates).

#### The fight stance {#fight-stance}

Moved to [Combat: targets, AI attacks and breakables](combat-targets.md#fight-stance).

##### The led steer {#led-steer}

Moved to [Combat: targets, AI attacks and breakables](combat-targets.md#led-steer).

### An AI's attacks {#ai-attacks}

Moved to [Combat: targets, AI attacks and breakables](combat-targets.md#ai-attacks).

### Breakables {#breakables}

Moved to [Combat: targets, AI attacks and breakables](combat-targets.md#breakables).

### The stereo theft {#stereo-theft}

Moved to [Combat: targets, AI attacks and breakables](combat-targets.md#stereo-theft).

### Input scripts {#input-scripts}

Coney's `--input-script` lines (frame = one 30 Hz update; [input scripts](../guides/running.md#input-scripts)) that
reproduce each behaviour, from a standing player facing the target with the camera behind; stick values are the in-game
magnitudes. The gaps
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

Every combat function of the human code (`0x00217a70`-`0x00288000`, a range inferred from where its functions lie; each
row graded; [Characters](characters.md#code-index) has why it starts at `0x00217a70`) that the sections above do not
walk through, in address order, with what it does. Names are ours, as in the local Ghidra project, where each function
also carries a plate comment. The rest of the human code is indexed on [Characters](characters.md#code-index).

### Attack checks, grabs, tackles and three-person moves {#code-grabs}

The checks before an attack starts ([An AI's attacks](combat-targets.md#ai-attacks)), and the state changes that start
and end a grab, a tackle, its hold and a three-person (tandem) move, and the throw links. The state bits are in [State
flags](#state-flags); push weight is the human's attribute 7 (vtable `+0xe4`), set to 1e9 to make a body immovable and
back to 1.0.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00224778` | `Human_CanStartAttack` | Range and state checks for an attack kind ([An AI's attacks](combat-targets.md#ai-attacks)). | confirmed (code) |
| `0x00229b60` | `AttackKind_IsCharge` | Kinds 0 and 19-21 ([An AI's attacks](combat-targets.md#ai-attacks)). | confirmed (code) |
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
| `0x00230930` | `Attack_FarReachSquared` | The squared far reach of an attack kind ([An AI's attacks](combat-targets.md#ai-attacks)). | confirmed (code) |
| `0x00230d00` | `Attack_ReachSquared` | Squared reach of an attack kind for a human: kind to anim id to AttackTable_GetReach, with fixed values for some kinds. | confirmed (code) |
| `0x00231590` | `Attack_GetNextAttackDelay` | The time before a target may be attacked again: the attacker's clip length for the kind in ms ([An AI's attacks](combat-targets.md#ai-attacks)). | confirmed (code) |
| `0x00232be8` | `Grab_EndBoth` | Ends a grab between two humans (Grab_End, 0x00280548) and sets state 0x20000000 on both. | confirmed (code) |
| `0x002600f0` | `Cuff_OnCufferClipEnd` | End hook of the cuffer's clip 266: an AI drops its target; clears state 0x8000000 and the pair link unless still busy. | confirmed (code) |
| `0x00260178` | `Cuff_OnCuffedClipEnd` | End hook of the cuffed human's clip 261: arrests it, uses one of the cuffer's cuffs and scores style; otherwise unlinks both. | confirmed (code) |
| `0x00260250` | `Cuff_Start` | Attack kind 41 from the mount: the cuffer plays 252, 260, 266 then the stance idle; the victim 253, 261, 267 then loops 320 ([An AI's attacks](combat-targets.md#ai-attacks)). | confirmed (code) |
| `0x002605f0` | `Uncuff_FreerEndHook` | End hook of the freer's uncuff clip. | confirmed (code) |
| `0x00260670` | `Uncuff_FreedEndHook` | End hook of the freed human's uncuff clip. | confirmed (code) |
| `0x002613a8` | `Pair_StartLoop256` | Both partners loop a clip (256 and 257) after a 0.05 s fade, anim state 0. | confirmed (code) |
| `0x00261e78` | `Human_StartMeleeThrow` | Starts a melee-weapon throw (clip 471 / 472 at a walk or run, or a standing throw) and removes the held set's move style. | confirmed (code) |
| `0x00262920` | `Pair_EndTopTasks` | Ends the top anim task of the human and of its partner. | confirmed (code) |
| `0x002629a8` | `Attack_OnPairedClipEnd` | End hook of a paired move: scores it, lets the crowd taunt and clears the throw link. | confirmed (code) |
| `0x00262a38` | `Pair_OnVictimClipEnd` | End hook of a paired move's victim clip: clears the throw link to the attacker. | confirmed (code) |
| `0x00263380` | `Grab_StartPairedMove` | Starts a paired move on the victim: spends power, aligns the pair, deals AnimRange damage (doubled by state 0x4000), plays clip n on the attacker and n + 1 on the victim, or a death reaction when it defeats. | confirmed (code) |
| `0x00263eb8` | `Human_AttackObject` | Attacks an object or car: slot 19's clip (slot 17 when the object is over 1 m up), turning to it by the first event when it is within 1.5 x the far range and beyond half the reach. | confirmed (code) |
| `0x00264878` | `Player_StartDive` | Starts the dive attack ([Moving attacks](#run-attacks)). | confirmed (code) |

### Hit reactions, paired moves and the mount {#code-reactions}

`0x00265c28`-`0x00279050` (the first and last rows, a range inferred as our grouping; each row graded): the handlers
that turn pending damage into a reaction by the victim's state ([Damage](combat-reactions.md#damage), [Hit
codes](#hit-codes)), the pair-break clips, grab strikes, cuffing, the mount's end hooks, the alignment helpers and
revives.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00265c28` | `Human_IsDamageDefeating` | Whether damage would defeat the human: tripled when cuffed; never with human flag 0x10 or 0x20000000000, knocked out or state 0x100000000; else health (+0x144) at most the damage. | confirmed (code) |
| `0x00265cd8` | `Hit_MaybeQueueHint15` | A player hit by an attack id 11-24 that does not defeat it, not blocking, outside Armies levels: queues game hint 15 once unlocked. | confirmed (code) |
| `0x00266b50` | `Hit_IsReactionWeakened` | The strength -1 test: victim flag 0x200, or a combo id against a victim without 0x400 that is not hurt ([Hit codes](#hit-codes)). | confirmed (code) |
| `0x00266c40` | `Hit_IsReactionStrengthened` | The strength +1 test: attacker flag 0x200000 and victim 0x400, or a combo id ([Hit codes](#hit-codes)). | confirmed (code) |
| `0x00266fd8` | `Hit_PickDyingReaction` | The dying reaction: strength 2, half the time the DIE set 304-315, else 292 ([Hit codes](#hit-codes)). | confirmed (code) |
| `0x00267338` | `Hit_GetSideFromPoint` | The side (0 front within 45 degrees, 3 left, 1 right, 2 behind) of a point around the human ([Hit codes](#hit-codes)). | confirmed (code) |
| `0x002678c8` | `Human_PlayGangDeathReaction` | The lethal reaction for Human_PlayLethalReaction: blends out, plays the death reaction (held 0x2000) and breaks a pair; a gang member says 13 near ([Damage](combat-reactions.md#damage)). | confirmed (code) |
| `0x00268430` | `HitReact_InSpecial` | Pending damage while in a special state: ends it and plays 674, or knocks the human down (198, then 674). | confirmed (code) |
| `0x002686e8` | `HitReact_WhileOut` | Pending damage while knocked out or in states 0x100000000 / 0x80000000: health 1 when flagged, a lying reaction, held flag 0x400. | confirmed (code) |
| `0x002688d0` | `HitReact_WhileGrabbed` | Pending damage from a third human while grabbed: ends the grab and plays a pair-break reaction, a stun or a paired reaction. | confirmed (code) |
| `0x00268ab0` | `HitReact_WhileCuffing` | Pending damage while cuffing someone (state 0x8000000): falls (0x80000), unlinks the pair and plays a paired reaction. | confirmed (code) |
| `0x00268b88` | `HitReact_WhileBeingCuffed` | Pending damage while being cuffed (0x10000000): the cuffer falls, the pair is unlinked and a paired reaction plays. | confirmed (code) |
| `0x00268c50` | `HitReact_InMiniGame` | Pending damage in a mini-game: aborts it; in mini-game mode 2 the normal reaction, else 331 then the stance idle. | confirmed (code) |
| `0x00268df8` | `HitReact_WhileMugging` | Pending damage while mugging: ends the mugging, then reacts as a grabber. | confirmed (code) |
| `0x00268e50` | `HitReact_WhileMugged` | Pending damage while being mugged: ends the mugging, then reacts as grabbed. | confirmed (code) |
| `0x00268ea8` | `HitReact_WhileGrabbing` | Pending damage from a third human while grabbing: ends the grab and plays a pair-break or paired reaction from the reaction table. | confirmed (code) |
| `0x002690f0` | `PairBreak_Play106` | A pair break: the fight stance, 106 holding 0x40000000, then the stance idle. | confirmed (code) |
| `0x00269250` | `PairBreak_Play138` | A pair break: the fight stance, 138 holding 0x40000000, then the stance idle. | confirmed (code) |
| `0x002693b0` | `PairBreak_Play145` | A pair break: the fight stance, 145 holding 0x40000000, then the stance idle. | confirmed (code) |
| `0x00269510` | `PairBreak_Play107` | A pair break: the fight stance, 107 holding 0x40000000, then the stance idle. | confirmed (code) |
| `0x00269670` | `HitReact_WhileTackling` | Pending damage from a third human while tackling: ends the tackle (the victim falls) and plays 216 or 218 by side, or gets up. | confirmed (code) |
| `0x002697a8` | `PairBreak_Play244` | A pair break: 244 holding 0x8000, then the stance idle. | confirmed (code) |
| `0x00269908` | `PairBreak_GetUp199` | A pair break from the ground: gets up with 245 then 199 holding 0x8000, then the stance idle. | confirmed (code) |
| `0x00269ab0` | `HitReact_WhileTackled` | Pending damage while tackled: cleared, no reaction. | confirmed (code) |
| `0x00269ac0` | `HitReact_WhileMounting` | Pending damage while holding a tackled human (state 0x8000000000): ends the hold, then reacts as a tackler. | confirmed (code) |
| `0x00269b18` | `HitReact_WhileMounted` | Pending damage while held down (state 0x10000000000): ends the hold for the holder; no reaction. | confirmed (code) |
| `0x00269b80` | `Hit_PlayBegReaction` | A hit on a knocked-down, cuffed or wounded human: the beg reaction; says 73 beg. | confirmed (code) |
| `0x00269dc8` | `Human_MoveAlongAxisOver` | Moves the human over a time along its own axis (forward, side or back by the argument), for the block dodge and the early block. | confirmed (code) |
| `0x0026a1f8` | `HitReact_OnSpecialEnd` | End hook: sends message 0x15; when the human's foe is the player (brain type 0) and the player's target is this human, clears the player's target (`Brain_SetTarget`, force 0). | confirmed (code) |
| `0x0026a298` | `Human_PlayRecoilClip` | Blends into a recoil clip chained to the fight idle, holding 0x400 (629 BAT_BLOCK_REACT on a bat block). | confirmed (code) |
| `0x0026a408` | `Pair_PlayPairedReaction` | Both humans play a paired reaction (clip n and n + 1) holding 0x2000, then the stance idle. | confirmed (code) |
| `0x0026adf0` | `Block_OnDodgeEnd` | End hook of the block dodge: clears the foe when no damage is pending. | confirmed (code) |
| `0x0026b170` | `HitReact_FromObject` | Pending damage from a thrown object, car or hazard: a reaction by the object's kind and weight, from its side. | confirmed (code) |
| `0x0026b328` | `HitReact_InJump` | Pending damage in a jump: clears the air states and plays 294 when the human hit itself. | confirmed (code) |
| `0x0026b3d8` | `HitReact_WhileFalling` | Pending damage while falling or landing (state 0x1c00000000): cleared, no reaction. | confirmed (code) |
| `0x0026b3e8` | `HitReact_OnFire` | Self-inflicted damage while on fire: breaks a pair and plays a burn reaction (290-293 for an AI, a third of the time). | confirmed (code) |
| `0x0026b848` | `ButtonMiniGame_Update` | The button mini-game's update (record +0x46 mode 2). | confirmed (code) |
| `0x0026c1b8` | `Grab_OnStartEnd` | End hook of the grab start: push sphere off. | confirmed (code) |
| `0x0026cb20` | `Grab_OnEscapeEnd` | End hook of an escape: push weight back to 1.0, the object's +0x3c cleared. | confirmed (code) |
| `0x0026cb98` | `Grab_OnEscapeEndClear` | End hook of an escape: as Grab_OnEscapeEnd, and clears the grab link. | confirmed (code) |
| `0x0026d110` | `Grab_OnReverseEnd` | End hook of a grab reversal: clears the throw link when no partner is left. | confirmed (code) |
| `0x0026e5f8` | `Grab_Strike` | A grab strike: AnimRange damage (doubled by state 0x4000); clip n on the grabber and n + 1 on the victim, or 139 when it defeats ([In the grab](combat-grabs.md#grabbing)). | confirmed (code) |
| `0x0026ec10` | `Grab_SpinToFront` | The rear-to-front spin before a power strike: states 0x40 / 0x10, move styles 7 / 8, 80 and 81 ([In the grab](combat-grabs.md#grabbing)). | confirmed (code) |
| `0x0026f2c8` | `Grab_PullOffTackler` | Grabs a tackler off its victim: 214 on the grabber, 215 on the tackler, the victim gets up with 199. | confirmed (code) |
| `0x0026f760` | `Tandem_OnAttackerEnd` | End hook of the tandem attacker's end clip: unlinks it. | confirmed (code) |
| `0x0026f840` | `Tandem_OnEnd` | End hook of a tandem clip: unlinks the three quietly. | confirmed (code) |
| `0x00270028` | `Tackle_OnVictimRiseEnd` | End hook on a tackled victim left alone: unless he is dead or dying or his pair link `+0xc4` is still set, ends the tackle and plays the rise 199, whose end wakes him if he is knocked out ([the knocked-out bit](combat-reactions.md#knocked-out-bit)). | confirmed (code) |
| `0x002700a8` | `Mount_OnStrikeEnd` | End hook of a mount strike or the grounded mount: gets off when the victim is over 0.2 m up or down, beyond max(reach + 0.34, reach x 1.2) or not clear; else re-places the mounter at clip 210's offset, scores the move, and may let a player victim reverse it. | confirmed (code) |
| `0x00270560` | `Tackle_OnIntroEnd` | End of the tackle's run-in: a target in front with human flag 0x20000 (or a player that counters) plays the counter (Attack_StartPaired kind 9); otherwise the mount starts from front or behind ([Grab and tackle](combat-grabs.md#grab)). | confirmed (code) |
| `0x00270a70` | `Mount_CheckVictim` | During a mount strike: gets off when the victim is no longer tackled, or is dead or out; clears record +0xb8 and +0xbc. | confirmed (code) |
| `0x00270f88` | `Mount_OnGetOffEnd` | End hook of getting off: ends the tackle. | confirmed (code) |
| `0x00270fc0` | `Mount_OnVictimGetOffEnd` | End hook of the victim's get-off clip: ends the tackle (or clears 0x800 and sets 0x20000000 alone) and clears the down timers. | confirmed (code) |
| `0x00271028` | `Mount_OnGetOffThrowEnd` | End hook: clears the throw link of a mounter that was throwing. | confirmed (code) |
| `0x00271060` | `Mount_OnVictimThrowEnd` | End hook: clears the victim's throw link when it has state 0x2000. | confirmed (code) |
| `0x00271098` | `Mount_GetOff` | Gets the mounter off the victim and ends the tackle ([The mount](combat-grabs.md#mount)). | confirmed (code) |
| `0x00271808` | `Mount_ReturnToFrontGrab` | From the mount back to a front grab (Grab_Link) ([The mount](combat-grabs.md#mount)). | confirmed (code) |
| `0x00271b30` | `Mount_StartOnGrounded` | Mounts a grounded target: clip 237 + its side (far range x 1.25 for a player), steered on, then Pair_LinkMount ([Grab and tackle](combat-grabs.md#grab)). | confirmed (code) |
| `0x00272310` | `Mount_OnReversalEnd` | End hook of the mount reversal: links the reversed mount (the victim on top); gets off when the new victim has human flag 0x20000. | confirmed (code) |
| `0x002723a8` | `Mount_OnReversedEnd` | End hook of the reversed mounter's clip: clears its throw link when alone. | confirmed (code) |
| `0x002723e8` | `Mount_StartReversal` | The mounted victim's reversal: 242 then 210 on the victim, 243 then 207 on the mounter. | confirmed (code) |
| `0x00274ee0` | `Revive_Finish` | Ends a revive: the partner takes the item, interface cue 0x17, the revived says 75 revive_thank and is healed. | confirmed (code) |
| `0x002752a0` | `Revive_Start` | Starts reviving a downed partner; says 76 revive_reasure. | confirmed (code) |
| `0x002759c0` | `Human_AlignToObject` | Turns and moves the human onto an object's side (front, back or a side) at the attack table's offset and reach for a clip, over a time. | confirmed (code) |
| `0x002766b0` | `Human_AlignToPoint` | Turns and moves the human toward a point over 0.1 s (a script message). | confirmed (code) |
| `0x00276810` | `Human_AlignForObjectAttack` | Turns and moves the human to an object at a clip's reach and offset, by the clip's first event at most. | confirmed (code) |
| `0x00276ff0` | `Human_AlignToHuman` | Turns and moves the human to another at a clip's reach and offset over a time. | confirmed (code) |
| `0x00277248` | `Human_AlignToVictimFacing` | Places the human at a clip's offset from another along that one's facing, over a time (the mount's 210, [The mount](combat-grabs.md#mount)). | confirmed (code) |
| `0x00277b20` | `Pair_AlignForMove` | Aligns a free victim for a paired move: refused beyond the far range, then both turned and the attacker placed at the clip's offset and reach over 1/60 s. | confirmed (code) |
| `0x00278ef8` | `Human_IsFreeableBy` | Whether a helper may free a human: a friend or the same gang (or human flag bit 0x80 of +0xe1), and cuffed or dead or out. | confirmed (code) |
| `0x00278fa0` | `Human_IsRevivableBy` | Whether a helper may revive a human: a friend or the same gang, knocked out and not cuffed. | confirmed (code) |
| `0x00279050` | `Human_FindFreeable` | Lists the visible humans within 3 m that Human_IsFreeableBy accepts. | confirmed (code) |

### Targets and commands {#code-targets}

`0x002790a8`-`0x0027c0f8` (the first and last rows, a range inferred as our grouping; each row graded): the target
searches and their filters ([Target selection](combat-targets.md#targets)) and one small test per [command](#commands).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x002790a8` | `Target_AngleFromAimHeading` | The absolute angle between the heading to another and record +0xd4 (the heading the pick wrote), 0 to pi. | confirmed (code) |
| `0x002791b0` | `TargetFilter_StanceEnemy` | The stance's nearest-enemy filter: a targetable, unfriendly, non-pad human within 2 m in height who is a threat (gangs enemies), not down, dead or held; a neutral civilian is refused. | confirmed (code) |
| `0x002792c8` | `TargetFilter_LockFirst` | The L1 search's first filter (6 m, 63 degrees): a targetable human of another gang, not friendly, not down or airborne, within the angle; a player's own record decides some cases. | confirmed (code) |
| `0x002797b0` | `TargetFilter_AnyAngle` | An enemy human at any angle: other gang, not friendly, within 2 m of height, not down or airborne. | confirmed (code) |
| `0x002798c0` | `TargetFilter_AlongFirst` | Human_FindTargetAlong's first filter: an enemy human within the angle, not down, without some held flags. | confirmed (code) |
| `0x00279a08` | `TargetFilter_AlongThird` | Human_FindTargetAlong's last filter: an enemy human within the angle that is not dead. | confirmed (code) |
| `0x00279b30` | `TargetFilter_AlongSecond` | Human_FindTargetAlong's second filter: an enemy human within the angle, with fewer held-flag limits. | confirmed (code) |
| `0x00279c48` | `TargetFilter_NearAhead` | The L1 search's near pass: a targetable human within the angle and 2 m of height, not down. | confirmed (code) |
| `0x00279d38` | `TargetFilter_KnockedOut` | The L1 search's last pass: a targetable human that is knocked out or dead. | confirmed (code) |
| `0x0027a1e8` | `TargetFilter_ThrowObject` | The throw-target pick's object filter: within the angle and height, a world object that is a strike target. | confirmed (code) |
| `0x0027a2f8` | `Human_FindTargetAlong` | A script message's target pick: humans within the vector's length and 54 degrees of its heading, through three filters, nearest first. | confirmed (code) |
| `0x0027ad80` | `Player_PickThrowTarget` | Picks a throw target within the range (20 m for a melee throw): humans within 27 degrees, then objects and glass; sets record +0xd4 / +0xd8. | confirmed (code) |
| `0x0027b058` | `Attack_FindNearestInReach` | The charge and dive's target: the nearest human between the clip's reach and far range within 54 degrees, then objects and glass ([where they aim](#charge-aim)). | confirmed (code) |
| `0x0027b2a8` | `Player_FindLockTarget` | The L1 lock-on search: humans within 6 m and 63 degrees, then enemies within 4 m and 6 m, any angle within 4 m, near ahead, a car within 1 m, then 2.5 m passes; the first in a clear line. | confirmed (code) |
| `0x0027b548` | `Player_LockOnTarget` | L1 pressed or held: makes the lock-on search's result the human or object target. | confirmed (code) |
| `0x0027b5c0` | `PlayerCmd_IsAny` | This sample's command is not 0. | confirmed (code) |
| `0x0027b5e0` | `PlayerCmd_IsCrossLongHold` | Command 0x10 (cross released after a tap, or held 4 samples). | confirmed (code) |
| `0x0027b608` | `PlayerCmd_IsSquarePressOnly` | Command 0xf (square pressed). | confirmed (code) |
| `0x0027b630` | `PlayerCmd_IsL2Cross` | Command 0x20 (L2 + cross). | confirmed (code) |
| `0x0027b658` | `PlayerCmd_IsL2Square` | Command 0x21 (L2 + square). | confirmed (code) |
| `0x0027b680` | `PlayerCmd_IsCrossPress` | Command 0x12 (cross pressed, or the AI's chain cross). | confirmed (code) |
| `0x0027b6a8` | `PlayerCmd_IsSquarePress` | Command 0xf or 0x11 (square pressed, or the AI's chain square). | confirmed (code) |
| `0x0027b700` | `PlayerCmd_IsCommand14` | Command 0x14. | confirmed (code) |
| `0x0027b728` | `PlayerCmd_IsCommand13` | Command 0x13. | confirmed (code) |
| `0x0027b750` | `PlayerCmd_IsR1Press` | Command 3 (R1 pressed). | confirmed (code) |
| `0x0027b778` | `PlayerCmd_IsCommand17` | Command 0x17. | confirmed (code) |
| `0x0027b7a0` | `PlayerCmd_IsCircleTap` | Command 0xd (circle tapped). | confirmed (code) |
| `0x0027b7c8` | `PlayerCmd_IsCircleHold` | Command 0xe (circle held 7 samples). | confirmed (code) |
| `0x0027b7f0` | `PlayerCmd_IsCrossSquareB` | Command 0x22 (cross + square). | confirmed (code) |
| `0x0027b818` | `PlayerCmd_IsCommand30` | Command 0x30. | confirmed (code) |
| `0x0027b840` | `PlayerCmd_IsCrossSquare` | Command 0x22 (cross + square). | confirmed (code) |
| `0x0027b868` | `PlayerCmd_IsCircleCross` | Command 0x23 (circle + cross). | confirmed (code) |
| `0x0027b890` | `PlayerCmd_IsCircleTriangle` | Command 0x24 (circle + triangle). | confirmed (code) |
| `0x0027b8b8` | `PlayerCmd_IsCommand39` | Command 0x39. | confirmed (code) |
| `0x0027b8e0` | `PlayerCmd_IsL1Held` | Command 6 (L1 held). | confirmed (code) |
| `0x0027b908` | `PlayerCmd_IsL1Release` | Command 8 (L1 released). | confirmed (code) |
| `0x0027b930` | `PlayerCmd_IsL1Press` | Command 7 (L1 pressed). | confirmed (code) |
| `0x0027b960` | `PlayerCmd_IsR1Held` | Command 4 (R1 held). | confirmed (code) |
| `0x0027b9e8` | `PlayerCmd_IsL2Held` | Command 5 (L2 held). | confirmed (code) |
| `0x0027ba10` | `PlayerCmd_IsTriangle` | Command 0xa (triangle pressed). | confirmed (code) |
| `0x0027ba38` | `PlayerCmd_IsTriangleHold` | Command 0xb (triangle held 7 samples). | confirmed (code) |
| `0x0027ba68` | `PlayerCmd_IsOtherInput` | A command other than 0, 1, 2, 6-8, 0xa-0xc, 0x10, 0x12, 0x16 and 0x25-0x2c; the mini-games, the tag, the stereo and the mugging test it. | confirmed (code) |
| `0x0027bb28` | `PlayerCmd_IsCommand37` | Command 0x37. | confirmed (code) |
| `0x0027bb50` | `PlayerCmd_IsCommand38` | Command 0x38. | confirmed (code) |
| `0x0027bb78` | `PlayerCmd_IsCommand36` | Command 0x36. | confirmed (code) |
| `0x0027bba0` | `PlayerCmd_IsCommand33` | Command 0x33. | confirmed (code) |
| `0x0027bbd0` | `PlayerCmd_IsDpadRight` | Command 0x28 (d-pad right, the flash). | confirmed (code) |
| `0x0027bbf8` | `PlayerCmd_IsDpadLeft` | Command 0x27 (d-pad left). | confirmed (code) |
| `0x0027bc20` | `PlayerCmd_IsCommand32` | Command 0x32. | confirmed (code) |
| `0x0027bcc8` | `Mash_ResetAlternation` | Clears the mash alternation 0x0051097c. | confirmed (code) |
| `0x0027bd98` | `PlayerCmd_IsGrabSpin` | Command 0x00510988 (R1 pressed) or 0x19: spins a grab. | confirmed (code) |
| `0x0027bdf8` | `PlayerCmd_IsSquareAny` | Command 0x11 or 0xf (square, or the AI's chain square). | confirmed (code) |
| `0x0027be50` | `PlayerCmd_IsCrossAny` | Command 0x12 or 0x10 (cross pressed or released). | confirmed (code) |
| `0x0027bea8` | `PlayerCmd_IsGrabReverse` | Command 0x00510988 (R1 pressed) or 0x19, for a grabbed or tackled human: reverses it. | confirmed (code) |
| `0x0027bf08` | `PlayerCmd_IsLetGo` | Command 5 (L2 held): lets go of a grab or mount. | confirmed (code) |
| `0x0027bf30` | `PlayerCmd_IsCircleAny` | Command 0x1e, 0xd or 0xe (circle pressed, tapped or held). | confirmed (code) |
| `0x0027bf98` | `PlayerCmd_IsCommand31` | Command 0x31 or the word at 0x0051098c (3, R1 pressed): the mount's cuff command ([Cuffing hold](combat-throws.md#cuffing-hold)). | confirmed (code) |
| `0x0027bff0` | `PlayerCmd_IsCircleAnyB` | Command 0x1e, 0xd or 0xe (circle), for the mount and the grab. | confirmed (code) |
| `0x0027c058` | `PlayerCmd_IsR2Held` | Command 1 (R2 held). | confirmed (code) |
| `0x0027c080` | `PlayerCmd_IsR2Released` | Command 2 (R2 released). | confirmed (code) |
| `0x0027c0a8` | `PlayerCmd_IsL1R1` | Command 0x1f (L1 + R1). | confirmed (code) |
| `0x0027c0d0` | `PlayerCmd_IsDpadUp` | Command 0x26 (d-pad up). | confirmed (code) |
| `0x0027c0f8` | `PlayerCmd_IsDpadDown` | Command 0x25 (d-pad down). | confirmed (code) |

### Grab ends, holds and the stick games {#code-holds}

From `0x00280548` (the first row; the range is our grouping, inferred, and each row graded): ending a grab, the
per-player action delay, and the stick games of the cuffing hold and of a mugged player
([Mugging](combat-throws.md#mugging)).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00280548` | `Grab_End` | Ends a grab: clears the victim's +0xc0 and restores the attached and grabbing controls to their defaults. | confirmed (code) |
| `0x00280d20` | `Grab_StickDirectionBits` | The stick's direction relative to the grabber's facing as a direction bit (0x80 with the stick at rest), for the hold's end. | confirmed (code) |
| `0x00280ee0` | `Player_AnnounceObjective` | Announces the current objective (the pause menu's text). | confirmed (code) |
| `0x00280f08` | `Player_AnnounceBonusObjective` | Announces the bonus objective. | confirmed (code) |
| `0x00280f30` | `PlayerRec_GetActionDelayLeft` | The per-player action delay left (+0x50 past now), or -1 with none. | confirmed (code) |
| `0x00280fa0` | `PlayerRec_ClearActionDelay` | Clears the per-player action delay (+0x4d, +0x50). | confirmed (code) |
| `0x00280fd8` | `PlayerRec_SetActionDelay` | Sets the per-player action delay: +0x4d = 1, +0x50 = now + ms (3000 after an uncuff). | confirmed (code) |
| `0x00281048` | `Float_NearlyEqual` | Whether two floats differ by less than a tolerance. | confirmed (code) |
| `0x00281070` | `Climb_IsSpaceClear` | Whether a sphere of the human's capsule radius at a point overlaps nothing. | confirmed (code) |
| `0x00281368` | `Human_RestoreBodyAfterClimb` | Clears body flag 0x4000 and resets the body's fields; reports the anim's sound to the AI as a noise. | confirmed (code) |
| `0x00282b68` | `Hold_GetParams` | The cuffing hold's holder record, by the held human's category +0x11b and the holding player's Warrior class byte +0x07 ([values](combat-throws.md#cuffing-hold)). | confirmed (code) |
| `0x00283078` | `Hold_GetParamsB` | The cuffing hold's held-player record, by that player's Warrior class byte +0x07 ([values](combat-throws.md#cuffing-hold)). | confirmed (code) |
| `0x002833c0` | `Player_UpdateCuffHold` | The holder's side of the cuffing hold (state 0x8000000000): the stick game with the mug meter; when it ends, cuffs (+0x5b4 set) or gets off. | confirmed (code) |
| `0x00283a30` | `Player_UpdateMugHold` | The held human's side of the cuffing hold (state 0x10000000000): the stick game (state byte +0x128); says 8 swear. | confirmed (code) |
| `0x00286550` | `Player_UpdateMuggedGame` | A player being mugged: the victim's stick game with the mug meter (Mug_GetParams, Mug_GetPlayerVictimParams). | confirmed (code) |

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`, a range inferred from where its functions lie: inventory,
statistics, flags, configuration workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004182e8` / `0x00418310` | `HitBalance_Construct` / `HitBalance_Reset` | a two-byte pair of counts in every human, zeroed by `Human_Construct` and `Human_Init` | confirmed (code) |
| `0x00418330` | `HitBalance_Note(kind)` | from `Rage_NoteHit`: for kind 0 or 1, that count goes up by 1 (at most 20) and the other count, when above 3, down by 4; its reader is not traced | confirmed (code); purpose inferred |
| `0x004183d8` / `0x00418408` | `RepeatTracker_Construct` / `RepeatTracker_Reset` | the [repeat tracker](combat-rage.md#rage): both set the counts and the flag to 0 and the last kind to 4; the constructor also sets the time 0 and the throw bonus 1.0 | confirmed (code) |
| `0x00418530` | `PlayerRecord_Reset` | clears a per-player record: the repeat tracker at `+0x10`, `+0x28`, the mini-game fields `+0x40`-`+0x58` ([Crimes](crimes.md#mini-game-record)); from `Human_Destroy`, `Human_MakePlayer`, the game state's set-up | confirmed (code) |
| `0x004193f8` | `RageSound_Hook` | empty; called from `Human_UpdateMeters` | confirmed (code) |
| `0x00419400` | `RageSound_PlayEnd` | `Human_EndRageMode`'s one-shot `vags/misc/rage_ends_01` | confirmed (code) |
| `0x00419460` / `0x00419630` / `0x00419558` | `RageSound_Prepare` / `RageSound_RestartIfRaging` / `RageSound_StopIfNoneRaging` | prepares the loop when no one rages (from `Human_UpdateMeters`); restarts it when a player has flag `0x80000` at human `+0xe0` and none plays (each frame, `0x0041a370`); stops it when no player has that flag | confirmed (code) |
| `0x004194b8` | `GameState_PlayRageSound` | (already named) the rage loop: when no sound is held at `W_GameState + 0x264` plays the 2D rage-mode loop by name and marks the task owned; otherwise restarts the prepared one | confirmed (code) |
| `0x00419718` / `0x00419728` | `RageSound_AddRager` / `RageSound_RemoveRager` | count of raging humans `+0x268`: `Human_StartRageMode` adds one, `Human_EndRageMode` takes one and at 0 stops the rage loop | confirmed (code) |
| `0x00419758` | `RageSound_Stop` | fades the rage loop (`vags/misc/rage_mode_06`, handle `+0x264`) out over 2 s and zeroes the count | confirmed (code) |
| `0x0041d940` | `Cfg_SetMaxThrowError` | `CfgSetMaxThrowError(a, b)`: both angles from degrees to radians, `0x005148b0` and `0x005148b4` | confirmed (code) |
| `0x0041daf8` | `Cfg_SetPlayerMugging` | `CfgPlayerMugging`: `+0x5708` | confirmed (code) |

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
| `combat/meters.*` | health, the pending damage (the update's largest), `strikeDamage()`, the power meter (its power class's maximum and refill, 400 and 60/s for class 64; drain 15/s, `spend()`) and the rage meter (its Warrior class's maximum, gain, decay and drain, `RageClass`: 78, 144 %, 7.8/s and 9.36/s for class 6; the gain formula truncated per award, the 5 s hold, start); the power maximum scaled by the hurt factor ([Stats](characters.md#stats-coney)) |
| `combat/grab.*` | when a grab may start, the search ranges (far range × 1.25), the nearest-candidate search, the throw by stick side and wall, and one update of a grab (strikes, the power strikes with the rear spin first, throws, the release with too little power, the R1 and circle spins, the L2 let-go, the mugging; nothing spent in rage) |
| `combat/stick_games.*` | the mugging, the stereo theft's rotation (mode 3) and the button mash (mode 1) |
| `combat/player_combat.*` | the dispatcher: block, chain, meters, the routes (grabbing, tackling, mugging, theft) and the commands, in the original's order; the grab breaks at 0 power |
| `combat/combat_tuning.*`, `debug/combat_tunables.*` | the values above as tunables, category **Combat**, registered at start-up beside the game's |
| `human/fighter.*`, `human/fighter_grab.cpp`, `human/fighter_victim.cpp`, `human/fighter_clips.h` | the player's combat inside the human (split as the attacker, the grab and the victim side): builds `PlayerCombat`'s input (the camera-turned stick in the facing frame, the pad's stick, the gait, game time, the target in front, the grab search and the snap's search), plays its clips, turns and slides into an attack, poses a grab (the alignment, the connect, the gate, the snap and the attachment), turns and walks the grab by the stick, lands the hits with their rage, locks onto a target and combat-walks round it; and the player hit (a duck and its counter, the block, the health floor, the hit armour, the reaction, stun, knockdown and mash) and held in a grab (the counter at the catch, the struggle, the strike back, the escape and the reversal) |
| `world_objects/pickups.*`, `gamemodes/level_pickups.*` | triangle with the objects of a level ([Crimes: triangle](crimes.md#triangle), steps 4 and 5): message 0 to the nearest object with a prompt (`SetMsgHandlerEx`), then to each object in reach, a true result taking the press; the search (the [pickable](objects.md#pickable) classes, reach, the two sight rays, the score by the direction from behind the feet) and the clip; the take, which adds a `TYPE_SPECIAL`'s loot with notify and its value in money without and removes the record, or puts any other kind in the hand ([A bat in hand](combat-throws.md#bat)); with something in hand and nothing taken, the drop. The human plays the clip with a 0.2 s blend and takes the object at its first event (`Human::startPickUp`); the play mode gives the fighter the held type's anim set, whose square, cross and two strikes `combat::animSetClips()` gives |
| `human/human_flags.h`, `human/fighter_script.cpp` | the [human flags](#human-flags) as the fighter keeps and reads them: god mode drops a hit's damage (**Coney's reading**: the reaction still plays), the demi-god floor (`HuSetDemiGodMode`'s fraction, one global) sets god mode when reached, `0x80`, `0x100`, `0x200`, `0x400` and `0x200000` shape the reaction, `0x2000000` gates every rage gain, `0x100000` freezes the meter's drain and decay, `0x4000000` spends no power, `0x100000000000` and an untargetable gang are skipped by the target search; `HuRevive` and `HuSetNormalMode`. The rage handlers (`CfgRageHandlers`) are called after the characters' step (**Coney choice** of when) with the original's arguments: the full one with `(human, true)`, enter and exit with `(human, flag)`, the flag true (1) when no other human rages and nil otherwise (counted before the starter is added and after the ender is taken off, **Coney's reading**). Before, the full handler got the handle alone, so `level99`'s `P1.CheckRage` skipped its full branch: the bat stayed in his hand through the rage lesson |
| `human/turn_and_slide.*` | the attack's steer ([Target selection](combat-targets.md#targets)): a turn and a slide at a constant rate over a time, the last update only for the time left; the time to a clip's first steer-ending event; and the goal, the target led by its velocity and short of it by the reach |
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
health floor (a hit cannot take him below 25 % of his maximum); a grabber hit by a third human loses power; a hit that
does not end him while his clip holds any of `0x1c16a40` (the charge, the run attack and a landing `0x1000000`, the
dive `0x400000`, the counter `0x2000`) only takes its health and the move plays on
([Moves a hit cannot stop](combat-reactions.md#protected-moves)); winding up
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
- **Object targets** ([Target selection](combat-targets.md#targets)): square with no human in front takes the nearest
  whole pane within the object attack's far range, within 54° of the heading, else within 135° (the original's × 0.8 is
  not applied there); its centre is the target point. The original approaches from up to far × 1.5; Coney does not.
  Doors and loose objects are not object targets yet.
- **The pick-up** ([Breakables](combat-targets.md#breakables)): the reach is measured in plan from the feet, and the
  second point the search goes round (human `+0x4e0`) is left out; the flag messages (`0x19`) that change what is
  pickable are not modelled. The human does not steer during the clip. Every `TYPE_SPECIAL` is loot (the named mission
  items are not listed). The human's current context record is the nearest object with a
  prompt in reach. The clip is the pick-up animation's pair; the drop before a left-handed or hat clip, and the
  anim set pushed for the follow-on, are not modelled. A dropped object lands 0.3 m ahead of the feet, with no fall.
- **A weapon in hand** ([A bat in hand](combat-throws.md#bat), [Moving attacks with something in
  hand](combat-throws.md#armed-moves)): the fighter's anim set stands for the held object's set. With set 1-3 square and
  cross take the armed branch (501, the grounded strike, the slot's swing; no walk attack, snaps or strike on a grabbed
  target), and the charge and dive are unchanged; the rage is the [Rage](combat-rage.md#rage) table's (not its gates on
  the attacker's brain and the victim's class, nor ids `0x269`-`0x26c`'s table-2 award). **Not built**: the armed
  branch's mugging, the blocks' clips of an anim set, the weapon's damage bonus (`CfgObj` `+0x58` × the power class's
  factor), a breakable's hit kind 2 for the moving attacks, and the throws of sets 4-6 (square, cross, the charge and
  the dive play the unarmed moves); a bat never breaks.
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
  the stick turns the player but the walk start waits for the fade, as at runtime. R1 held blocks only when the record holds
  no phase outside `0x20081404` (`Human_CanFight`), so it cuts an attack's end phase and ends its chain but waits
  through the wind-up, window and recovery ([Input](combat-moves.md#input)). A new attack fades in over 0.2 s; a
  chain step keeps the 0.1 s combat fade (**Coney's reading**: the original swaps it into the running task). Coney
  never sets state code 5. A buffered snap plays only with a snap target within 25's far range (with `CfgSnap`
  on), else the plain square step, and no step from `S1` or `X1` plays while the target is low (down, or 1.3 to
  1.8 m below), as [Input](combat-moves.md#input) gives. **Coney's readings**: the buffered snap's search runs along
  its side of the facing, not the stick of the moment, and a step refused at a low target drops its press. With
  `CfgSnap` off (the debug menu's "Snap needs target") the stick alone snaps, fresh or buffered.
- **The characters' step** ([Tasks](tasks.md#humans-update)): it runs on Coney's fixed 1/30 s step, the original's
  30 Hz characters' update, without the 60 Hz tick or the timing wheel, which wait for the world's objects; the brains
  are an empty hook until the AI lands; the context actions (triangle) are refused while the dispatcher would drop a
  command or the human is busy.
- `SS2`, square is always `SSS3` (19), never 20; a grounded target takes 193, never 194; the dive takes the charge's
  conditions.
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
  cross during the run start (`0x10000000`) plays `S1` or `X1`. In a [fight stance](combat-targets.md#fight-stance)
  neither the unarmed run nor the walk attack plays, so a square there goes on to the snap or `S1` and a cross to `X1`;
  the armed 501 does not test the stance. **Stand-in**: the stance is the player locked onto a target
  (`Fighter::lockTarget()`), not the original's own rules for entering and leaving it.
  The original's snap filter takes no human friendly to the player (`Human_IsFriendly`, `0x00222a90`); Coney's
  search leaves that to the targets it is given. With the disc, `level99`'s lesson 7 passes:
  `repo:tests/platform/disc_level99_snaps_test.cpp`.
- A side is "front" up to and including 45° and "rear" beyond 135°; a height difference beyond 1.5 m counts as 0.9 to
  1.5 m.
- The mount ([The mount](combat-grabs.md#mount)): the victim is placed at clip 210's pair event when 210 starts rather
  than slid there over 0.1 s; the mount's moves take square's mask, so one plays out before the next; the mount's power
  strike spends the endurance fraction, as the grab's does (the page names only its need); triangle in the mount (the
  mugging) is not built, nor flag `0x20000`'s getting off at once.
- The pad's buttons are matched even while the pad is locked (`HuLockPad`), and a command disabled with
  `EnableCommand` is kept pending; the human acts on neither, but the `PadSetHandlerEx` handler hears both
  ([PadSetHandlerEx](../references/bindings/input.md#padsethandlerex)). Only the pad-driven humans carry a pad command:
  Coney's brains write their commands straight to the record.
- The special (cross + square outside a hold, [Specials](#attacks)) is 653 (645 in rage), 655 (647) when the player
  stands on its target's rear; circle + triangle (the tag) is not built. Both specials are refused on `0xaeebf7ff`.
- The strong grapple ([Strong grapple](combat-grabs.md#strong-grapple)) is the grab with 657 / 659 (649 / 651 in rage)
  as its connect and 658 / 660 on the victim, searched with anim 1's range within 54° of the stick, the clear line to the
  target (`0x0021c0a8`) not tested. Its damage is dealt on the snap to the hold, reported to the tutorial as 82 / 84;
  the paired, tackle and solo paths for other clip flags are not built.
- The power strike's extension (57 → 59, 63 → 65) takes square's press or cross's `0x10` only on an update with the
  window open, and the grab ends when the last part's clip gives way to 389: the victim, attached until then, is put
  on the ground (196) to get up after its time. The rear power strike's hit is scored as 57, not the spin's 80.
- A grab plays one move at a time; a throw lets go at once (from the rear hold, once its spin to the front has
  played), turning the pair onto the stick from the front hold ([Where a throw goes](combat-throws.md#throw-direction));
  the rear power strike's spin plays in front of the strike, whose timing starts with it; the release with too little power
  goes straight to the idles, and the grab broken at 0 power plays the let-go. A tackle also ends when the power meter is
  empty, and any hold when the victim has no health left.
- The grab and tackle search takes the nearest candidate by straight-line distance with no facing cone. The attack's
  target search uses the attack's far range in `Player_PickTarget`'s first two passes (the third finds no human the
  second missed); beyond the far range the attacker turns at most 8° at once (read as degrees). Within it the steer
  follows [The steer in detail](combat-targets.md#targets): from the update after the clip starts it turns at angle /
  `T` and slides at (goal − position) / `T`, on top of the clip's root motion, the last update only for the time left.
  The attack's hit code picks the steer: with flag `0x8` (`AttackTable_GetFlags` `0x00254d60`) `Attack_SteerLed`
  (`0x00275678`) turns and slides over the time to the clip's first event + 0.1 s (X1, S1, the chain's steps but `SSX3`
  and `SSS3`, and the snaps); without it `Attack_SteerToTarget` takes the caller's time cut to that, the time to the
  event from `Attack_Start` and the specials (653 and 645 stand at their reach by k3 at runtime), 0.1 s from the snap's
  start. The lead is the target's velocity × (the time to the event + 0.1 s), cut to 0.5 m as above ([where the attacker
  stands](combat-moves.md#reach)). The led steer aims at the target's head ([The led
  steer](combat-targets.md#led-steer)), read once as the attack starts, and the other at its position. On the disc
  Coney's posed heads match the runtime ones: in 388 the head stands 0.001-0.039 m forward and within 0.006 m sideways,
  in 358 0.097-0.186 m forward and 0.038-0.065 m right, 0.02 m beyond the recorded leans. **Coney's reading**: the turn
  faces the led target, not the standing point, which lies behind the attacker when the target is inside the reach (the
  runtime `XX2` at 0.83 m, inside its 1.12 m reach, turned under 1°). A reaction cuts the steer.
- **Posing a grab** follows [Posing a grab](combat-grabs.md#grab-posing): circle plays 71, then 72 (from the front) or
  74 (from the rear, when the player stands on the victim's rear side), the player turning to face the victim over 71's
  playing time while the victim waits in its idle. When 72 / 74 starts, the alignment turns and slides the player over
  0.1 × the clip's time (two updates for 72) so the victim stands at the clip's reach straight ahead, and turns the victim
  to face him (or away); beyond the far range × 1.25 the grab fails with 69. The victim then plays 73 / 75 **from the
  player's anim set at its rate** (a paired clip, fade 0), and both bodies move by their clips' root motion. At the
  clip's end the gate (1.297 m, 0.2 m in height) releases the grab or the victim is snapped to the hold's offset from
  82's / 84's range record, (0.380, 1.012) facing the player or (−0.097, 0.222) facing his way, and attached: each
  update it is put at the player's transform × that offset, its own root motion ignored. A spin (78 / 80) detaches it
  so both bodies move by their clips, as at runtime, and its end snaps it to the other hold. A strike, power strike or
  a front hold's plain throw is refused (nothing played or spent) unless the victim stands within 0.3 m of the current
  hold's point; a wall throw and a rear hold's throw are not checked ([Where a throw
  goes](combat-throws.md#throw-direction)). The mounted victim is attached at 210's type-8 pair event, (−0.120, 0.032):
  0.032 m ahead and 0.120 m to the left, facing the other way. **Coney's choices**: the grab's side is decided as the
  intro starts (a passive target does not move in between); the place a move checks is the current hold's point (the
  strikes' points are the front hold's); the victim's turn spreads evenly over the alignment's updates and a slide under
  0.01 m is left out; the moves of a hold switch both humans with no fade, the let-go keeps the combat fade.
- The mugging (`MuggingGame`, `LevelPickups::mugEnded()`): the record is `SetInterrogateParam`'s set 0 while its
  required time is set, else the runtime one (5 s, 2.5 s, 50 s off target, 50°, 60°), not the per-class table; the
  target angles are drawn evenly (the first at random). The money moves in the deciding update, the end clips 344 /
  345 or 346 / 347 play, and the mugger's callback runs when his end clip finishes, or at once for a let-go or a hit
  ([Crimes](crimes.md#coneys-implementation)); the speech, the hints, the half-way `no_item` stop, the victim's
  interrogation and pocket item, ped type 5's 1.5 times and the statistic are not built. The theft: clockwise steps neither
  add nor take away; the 250 ms pause ignores the stick. The mash follows [Uncuffing](crimes.md#uncuffing): only
  the held commands of L1 and R1 alternate, the gain is rounded, the quit commands fail it and the decay starts at the
  first alternation.
- The block is read only when the player is free (not grabbing, tackling, mugging or in a theft); it turns the player
  towards the stick at the standing turn rate.
- The stun's loop is 356, as seen at runtime (the code names 355 as a stunned reaction's return).
- `applyClassDamage()` rounds as the PS2's floating-point unit does (toward zero), which gives the research's 34 for 30
  at 115 %.
- **The shared Anim Range List is not reproduced**: in the original the class damage is written into the list the
  character's humans share, so the newest human's class sets everyone's damage ([Being hit](combat-reactions.md#being-hit-runtime)).
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
- The rise's counter ([Down and under attack](combat-reactions.md#ground-escape)) is the duck's played out of 199:
  square or cross on an update whose clip fires its own `0x25` while the player is free to react (held bits only
  within `0x40089000`, not stunned with the manual stun `0x8000000`) plays 617-620 that update, ends the rise and
  the stun, and spends the command. **Coney's stand-in** for the foe (the human that knocked him down, not kept):
  the target search's pick within 1.25 × 617's reach; with none the request lasts while he stays free to react.
  Only a player counters (an AI's Grounded goal says no through the rise).
- Held: the counter at the catch costs the grabber a quarter of its maximum; a reversed hold has no victim behind it
  (the grabber is an entry point, not a `Holdable`).
- The player's grab or tackle holds any `Holdable`: the sandbox's passive target or another human without flag `0x40`.
  A held human's fighter keeps the hold's state (held or mounted), which holds its movement, keeps it from acting and
  gives its brain the grabbed (`0x14`) or tackled (`0x15`) reaction goal; attached, its own update does not move it.
  The grab's intro leaves the victim alone (it is stopped at the connect, as `Grab_Connect` does), so an AI may still
  answer it; the tackle's intro stops it at once (**Coney choice**, as before). The answer itself (the AI's counter
  76 or 9) and the AI's own struggle in the hold are not built, and no AI grabs or tackles yet. A held human gone from
  the targets, or freed by a script, ends the hold.
- **Breaking a pair** ([Breaking a pair from outside](combat-grabs.md#pair-break)): every placement of a human
  (`Human::spawn`: the scripts' teleports, a scene's release) first breaks any pair it is in (`Fighter::breakPair`). The
  human placed plays nothing; a victim it held plays 145 (front), 107 (rear or a mugging) or 245 then 199 (mounted) and
  stands free; a grabber it was held by plays 138, 106 or 244 on its next update (**Coney's choice**: the original plays
  it at once). **Coney's choice**: a held human whose grabber stops updating without a break (taken out of the update) frees
  itself with the same victim clips after 3 of its own updates without its grabber keeping the hold (the humans act
  in an order that alternates each step, so a live grabber keeps it at most 2 apart). `Grab_Release`'s refusal in a
  scene is not built: no human in a pair is taken into one.
- The combat walk's clip by eight even 45° sectors centred on the clips' directions; the walk starts at its full
  speed (the 5 slower first updates backward are not known).
- **The flash** (`PlayLevelMode::stepFlash()`, `useFlash()`): d-pad right with a flash carried and health below the
  maximum plays 665 when nothing holds a move, and the clip's action event `0x41` 19 updates in
  (`Human::actionEvent()`) uses the flash: one spent, interface sound 23 and the clip's animation sound 70, the health
  filled, the health rings asked for; a clip cut first spends nothing. In a grab the flash is used at once and the
  grab stays. Coney always heals in full (it has no difficulty setting) and does not build the full-health rage use or
  the screen effect reset. The clip holds `0x2000` **and sets it as it starts** (**inferred**: the page says the task
  "holds" it and only a set bit keeps the clip alive). Held without being set, the bit never reaches the record, the
  stick step does not count the human busy, and a player in the fight stance (after walking up to the dealer, his lock
  on) has the stance walk's idle put over 665 on the next update: the clip showed for one update, the event never
  came, and nothing was spent or healed (issue 234, found on `level99` checkpoint 3 after the purchase). A second
  d-pad right while 665 plays does nothing (the human is busy, and 665 is already his anim). Tests:
  `repo:tests/platform/disc_flash_test.cpp` (`level80` standing, walking and running; `level99` from the dealer's
  purchase to the use in the stance: 665 from the press, the spend, cue 23 and animation sound 70 on update 19 of
  it, and no play left silent by the engine).

**Not built**: the rage of the attacks beyond the chain, the moving attacks and the throws (their events are not
mapped), and the hurt multipliers `+0x10` / `+0x14`. The AI's attacks on the player, its attack warnings, the
class damage tables and a boss's class 13 are on [AI](ai.md); weapons on [A bat in hand](combat-throws.md#bat);
breakables and car windows on [Breakables](combat-targets.md#breakables).

## Open questions

- **The rise's counter** ([Down and under attack](combat-reactions.md#ground-escape)). (Answered: 199's own `0x25`
  events, frames 11-18, of which 11-13 count because its `0x48` at 14 starts the recovery; square or cross; k = 15-17 at
  runtime.) Still open: 199's exact play rate (about 0.75-0.78 from the measured edges).
- **The block**: whether a strength-3 hit can still break a block. (Answered: `+0x14` = `0xe`, message `0xa5`, is
  the duck counter, [Blocking](#block); seen at runtime, and the AI goals' answer traced.) The block goal's byte `+0x14`
  (answered): its Start rolls it against the block chance ([AI](ai.md#block)).
- **The shared Anim Range List**: whether the overwrite by the newest human is intended, and which humans share a
  list ([Being hit](combat-reactions.md#being-hit-runtime)). Coney does not reproduce it.
- **A blocked `SSS3`'s rage**: 0 at runtime ([Rage](combat-rage.md#rage)) where the two awards' formula gives 1 (3 >> 1
  = 1 point, as `SSX3`'s blocked 1); Coney gives 1.
- **The stun after a knockdown**: it ends at the rise + 200 ms, before 199 ends, yet after 653 the player stood in
  356 for 7 updates before 357 ([Being hit](combat-reactions.md#being-hit-runtime)); Coney plays 357 as 199 ends.
- **The halved rage** (answered): the repeat tracker and the throw bonus, both seen at runtime ([Rage](combat-rage.md#rage)).
  Brain type 3 (answered): the Warriors' brain, so an ally's ([AI](ai.md#types)).
- **The backward combat walk** (answered): 3.429 m/s like every direction, after 5 slower updates
  ([Target selection](combat-targets.md#targets)). Still open: what slows those 5 updates.
- **Class 13**: which character class it is (it gets hit armour and adds 2 s to a knockdown).
- **The rage events**: the meaning of the events beyond the chain attacks' (`0x002653d8`, `0x00264fa0`).
- **A weapon's rage** (answered): a bat's 34 gives 2, its 36, 37 and 38 give 11 ([Rage](combat-rage.md#rage)).
- **The strike back's rage**: "The other attacks" gives 104 (`0x68`) event 2 × 10 (14 rage), but a strike back
  gave 1 at runtime; Coney keeps the runtime 1. Which id the award reads for it is open.
- **The pick-up clip** (answered): a pair per pick-up animation, high above 0.8 m ([A bat in hand](combat-throws.md#bat)).
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
- **`XX2` against a walking target**: in `combat_cross` the original's target walks up (0.83 → 0.91 m) and the bodies
  push apart on `XX2` (0.25 m/s against Coney's 0.81); Coney's sandbox target stands still and has no body contact.
- **The grab at runtime**: the placement is confirmed ([Grab pose at runtime](combat-grabs.md#grab-pose-runtime)); still
  open are the fields the alignment writes (human `+0x2e0`-`+0x332`, victim `+0xa0` / `+0xb0`).
- **The bat**: which code maps square and cross to 34 / 36 with a bat in hand, and whether a bat on the ground
  gets message 0 from the pick-up search ([A bat in hand](combat-throws.md#bat)).
- **Rage extensions at runtime**: 63 → 65 in a grab and 231 → 233 in the mount, which `level99`'s rage lesson
  waits for (inferred from `Player_UpdatePowerMove`); L1 + R1 did not start rage in a slot 6 copy with the meter
  written full, so what else the start needs is open.
- **Whether the flash clip sets `0x2000`** (record `+0x08`) as it starts: the task holds it
  ([Rage](combat-rage.md#rage)), and Coney sets it, which keeps the fight stance from cutting the clip; the type-5
  task's start flags at `0x0025ade8` would tell, and a runtime read of `+0x08` on the update after d-pad right in the stance.
- **The flash during a move** ([The flash during a move](combat-rage.md#flash-mid-move)): what sets 665's rate (53
  updates for 40 frames); why presses on landing from the slot 9 window jump gave no command; the window between 665's
  start and its event when a hit lands (a hit in `flash_hit`'s first run took health while 665 played on).
- **The clips' `+0x44` flags** (paired `0x1`, tackle `0x20`, grab `0x40`): which clips carry the tackle flag
  ([Strong grapple](combat-grabs.md#strong-grapple)).
- **Killer and double damage in a hold**: whether the moves in a grab (`Grab_Strike`), the power strikes and the
  throws reach `Human_AddPendingDamage`'s doubling and `Strike_Contact`'s killer rule. Coney applies both to free
  strikes only ([Human flags](#human-flags)).
- **A player with both grab-block flags** (`0x40000000` and `0x80000000`, `HuBlockGrab`): whether
  `Player_GrabOrTackle` (`0x00284920`) also refuses the tackle, and whether the refused tap plays the intro and the
  miss. Coney refuses the grab only, and plays nothing.
- **226's knockdown word at runtime**: the disc's 226 has its type-7 word 0, which would queue 198, yet at runtime
  `Attack_StartPaired` took the branch without 198 ([The mount power strike](combat-grabs.md#mount-power)). To find out,
  rerun `moves_mount_power` with a hook at `Anim_GetKnockdownValue` (`0x00101a60`) logging its argument and return on
  update 143, and read the clip it was handed in memory: a different set (the generic `gen_mount_power20_*` rather
  than Rembrandt's) or another event than the first type-7 one would explain it.
- **Who sets state `0x80000000`**: the lethal hit during a paired move leaves it on the victim's record `+0x14`
  ([Moves a hit cannot stop](combat-reactions.md#protected-moves)), but the setter was not traced. To find out, rerun
  `moves_finisher_lethal` with the `state-code` hook (record `+0x14`'s setter) logging `ra` on update 56, and if the
  setter is not the writer, search the damage and reaction code (`Human_UpdateActions`, `0x00254e78`, and what it
  calls) for a store that ORs `0x80000000` into `+0x14`.
  One setter found since (2026-10-11): `Human_PlayDeathReaction` (`0x00267b70`) ends with
  `Human_SetStateFlag(h, 0x80000000)`, confirmed (code); that it is the one at work on update 56 is inferred.
- **Killer mode and double damage in a hold** (answered): both apply to every move of a hold. The grab strikes
  (`Grab_Strike`, `0x0026e5f8`), the paired moves (`Attack_StartPaired`, `0x00262ac8`: power strikes, throws, the
  mount power strike), `Grab_StartPairedMove` (`0x00263380`) and the mount strikes (`0x00270b38`, `0x00271ef0`) all
  deal their damage through `Human_DealDamage` (`0x0021d680`) with the attacker passed, and that function applies the
  attacker's `0x20` (a non-zero hit takes the victim's whole health) before `Human_AddPendingDamage` (`0x00264bd8`)
  doubles the pending damage for the attacker's `0x4000`. The grab and mount power strikes even double the damage
  themselves when they test whether it will defeat the victim (`0x0027f3b0`, `0x0027ec20`). Confirmed (code) at the
  call sites `0x00262f28`, `0x002637cc`, `0x0026e690`, `0x00270f50`, `0x002722d4`. Links that deal 0 stay 0.
- **A player with both grab-block flags** (answered, [Human flags](#human-flags)): he still tackles. `Player_GrabOrTackle`
  (`0x00284920`) tests circle held first, refused only by `0x100000000`; the two grab bits together refuse only the
  circle tap, before anything plays (no intro, no miss, no stance change, no grounded mount or tandem). Either bit
  alone does not stop the tap there: `Grab_IntroEnd` then refuses the front or rear grab and the intro ends in 69.
  Confirmed (code).
- **The root turn and the task's rate** (open, from #240): on the disc Coney's throw clips turn the thrower about a
  third too far (149 by −120° where the runtime turn was −90°; 151 −237° for 180°, 153 +119° for +86.5°), and the
  front victim (148) lands 19.2° right of the stick where the original's landed 2.1° right
  ([Where a throw goes](combat-throws.md#throw-direction)). Coney applies bone 0's turn per 1/30 s as
  [Root motion](formats/animation.md#root-motion) says, not scaled by the task's rate; these clips play at 0.75. In a
  local experiment (not kept) scaling the turn by the rate, as the velocity is, gave −0.2° for 149 (runtime −0.2°),
  +2.1° for 151 (+1.2°), −0.9° for 153 (−3.5°), turns of 3.2° and 2.7° per update during the 1/6 s turn (runtime 3.2°
  and 2.7°) and a front landing 4.8° right of the stick. Whether `0x00104ce0` or `0x0023f238` scales the turn by the
  rate needs checking in the code; it would change every clip with a root turn, not only the throws.
