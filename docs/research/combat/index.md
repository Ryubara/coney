---
kind: system
system: combat
owns:
  functions: [
    "0x00147430", "0x00147940", "0x001480e0", "0x0021b290", "0x0021d5c0", "0x00224778", "0x00227a90", "0x00229b60",
    "0x00229b90", "0x0022af48", "0x0022b1a0", "0x0022b328", "0x0022b5a8", "0x0022b760", "0x0022b960", "0x0022bce0",
    "0x0022bdc0", "0x0022c388", "0x0022c548", "0x0022cb00", "0x0022cd78", "0x0022d030", "0x0022d1f8", "0x00230930",
    "0x00230d00", "0x00231590", "0x00232be8", "0x00236038", "0x002542e8", "0x00254508", "0x002548f0", "0x002600f0",
    "0x00260178", "0x00260250", "0x002605f0", "0x00260670", "0x002613a8", "0x002617f8", "0x00261e78", "0x002625a8",
    "0x00262920", "0x002629a8", "0x00262a38", "0x00263380", "0x00263eb8", "0x00264878", "0x00265c28", "0x00265cd8",
    "0x00266b50", "0x00266c40", "0x00266d00", "0x00266fd8", "0x00267338", "0x002678c8", "0x00268430", "0x002686e8",
    "0x002688d0", "0x00268ab0", "0x00268b88", "0x00268c50", "0x00268df8", "0x00268e50", "0x00268ea8", "0x002690f0",
    "0x00269250", "0x002693b0", "0x00269510", "0x00269670", "0x002697a8", "0x00269908", "0x00269ab0", "0x00269ac0",
    "0x00269b18", "0x00269b80", "0x00269dc8", "0x00269f30", "0x0026a1f8", "0x0026a298", "0x0026a408", "0x0026adf0",
    "0x0026b170", "0x0026b328", "0x0026b3d8", "0x0026b3e8", "0x0026b848", "0x0026c1b8", "0x0026cb20", "0x0026cb98",
    "0x0026d110", "0x0026e5f8", "0x0026ec10", "0x0026f2c8", "0x0026f760", "0x0026f840", "0x00270028", "0x002700a8",
    "0x00270560", "0x00270a70", "0x00270f88", "0x00270fc0", "0x00271028", "0x00271060", "0x00271098", "0x00271808",
    "0x00271b30", "0x00272310", "0x002723a8", "0x002723e8", "0x00274ee0", "0x002752a0", "0x002759c0", "0x002766b0",
    "0x00276810", "0x00276ff0", "0x00277248", "0x00277b20", "0x00278ef8", "0x00278fa0", "0x00279050", "0x002790a8",
    "0x002791b0", "0x002792c8", "0x002797b0", "0x002798c0", "0x00279a08", "0x00279b30", "0x00279c48", "0x00279d38",
    "0x0027a1e8", "0x0027a2f8", "0x0027ad80", "0x0027b058", "0x0027b2a8", "0x0027b548", "0x0027b5c0", "0x0027b5e0",
    "0x0027b608", "0x0027b630", "0x0027b658", "0x0027b680", "0x0027b6a8", "0x0027b700", "0x0027b728", "0x0027b750",
    "0x0027b778", "0x0027b7a0", "0x0027b7c8", "0x0027b7f0", "0x0027b818", "0x0027b840", "0x0027b868", "0x0027b890",
    "0x0027b8b8", "0x0027b8e0", "0x0027b908", "0x0027b930", "0x0027b960", "0x0027b9e8", "0x0027ba10", "0x0027ba38",
    "0x0027ba68", "0x0027bb28", "0x0027bb50", "0x0027bb78", "0x0027bba0", "0x0027bbd0", "0x0027bbf8", "0x0027bc20",
    "0x0027bcc8", "0x0027bd98", "0x0027bdf8", "0x0027be50", "0x0027bea8", "0x0027bf08", "0x0027bf30", "0x0027bf98",
    "0x0027bff0", "0x0027c058", "0x0027c080", "0x0027c0a8", "0x0027c0d0", "0x0027c0f8", "0x0027d800", "0x0027d900",
    "0x00280548", "0x00280630", "0x00280708", "0x00280d20", "0x00280ee0", "0x00280f08", "0x00280f30", "0x00280fa0",
    "0x00280fd8", "0x00281048", "0x00281070", "0x00281368", "0x00282b68", "0x00283078", "0x002833c0", "0x00283a30",
    "0x00286550", "0x00286cc8", "0x00287730", "0x00287a18", "0x004182e8", "0x00418310", "0x00418330", "0x004183d8",
    "0x00418408", "0x00418530", "0x004193f8", "0x00419400", "0x00419460", "0x004194b8", "0x00419558", "0x00419630",
    "0x00419718", "0x00419728", "0x00419758", "0x0041d940", "0x0041daf8",
  ]
---
# Combat

Verified against: `SLUS_212.15` (NTSC-U); runtime claims name PCSX2 2.9.94 and their method in How this was checked.

## Summary

How the player fights on foot: the attack buttons and their chains, the moving attacks (from a walk, a run and a
sprint), blocking, grabbing and what follows a grab (strikes, throws, mugging), tackles, the damage a hit does and
how the target reacts, health, the power meter that grabs spend and the rage meter that hits fill, and how a target
is picked. The same input path breaks objects (glass, car windows) and runs the stereo theft minigame, so those are
here too. Movement, sprint, jumps and climbs are on [Characters](../characters.md); the clips on
[Animation](../formats/animation.md); the anim ids on the [Anim ids](../../references/anim-ids.md) list.

In one paragraph: the pad's buttons become a **command id** per update through nine trigger tables (held, pressed,
released, tapped, long hold, history hold and combinations). `Player_UpdateActions` dispatches on the command and the
human's **state flags**: square and cross start attacks whose damage is the anim id's Anim Range List value; a
second and third press inside the attack's **chain window** picks the next attack of a three-hit combo; circle tapped
grabs, circle held tackles; R1 held blocks; L2 held plus cross or square at a run charges or dives. Each hit leaves a
pending damage on the target, which plays a reaction chosen by the attack's **hit code** (strength, height, direction)
and the side it came from; strong hits knock down, flagged hits stun, and a held block cancels most hits. Hits fill
the attacker's rage; L1 + R1 with a full meter starts rage, which makes grabs unbreakable and spends no power.

The subjects with entry functions of their own have topic pages: [grabs, the hold and the mount](../combat-grabs.md),
[throws, weapons in hand and mugging](../combat-throws.md), [damage, reactions and defeat](../combat-reactions.md), [the
power meter, rage and the flash](../combat-rage.md) and [targets, AI attacks and breakables](../combat-targets.md); the
move table and frame data are on [Combat moves](../combat-moves.md). This page keeps the commands, the data every topic
reads, the dispatcher, the attacks and their strikes, the attack warnings and the block.

## Map {#original-structure}

No source file names this code: it lies in `Human/`, in the stretch after `cns/cnsplayertag.cpp`
([Source map](../source-map.md#position), inferred), and in `Human/` for the shared human calls. Names are ours, as the
shared Ghidra project gives them. The page owns the functions its front matter lists, and every one of them, with its
role and level, is on [the combat function list](functions.md). Where to start, by behaviour:

| Behaviour | Function | Section | Evidence |
| --- | --- | --- | --- |
| Buttons to a command per update | `0x00147940` `Commands_Match` | [Commands](#commands) | confirmed (code) |
| What runs each update for a pad human | `0x0027c120` `Player_UpdateActions` | [The dispatcher](#dispatch) | confirmed (code) |
| Square: standing, walking, running, snap, grounded and object attacks | `0x00286cc8` `Player_Square` | [Attacks and chains](#attacks) | confirmed (code) |
| Cross: the X attacks | `0x00287a18` `Player_Cross` | [Attacks and chains](#attacks) | confirmed (code), runtime |
| The next attack of a combo | `0x00280708` `Player_UpdateChain` | [Attacks and chains](#attacks) | confirmed (code) |
| An attack clip starts, the combo counted | `0x002625a8` `Attack_Start` | [Attacks and chains](#attacks) | confirmed (code) |
| L2 + cross / L2 + square at a run | `0x0027d800` `Player_Charge`, `0x0027d900` `Player_Dive` | [Moving attacks](#run-attacks) | confirmed (code), runtime |
| Cross + square, circle + cross, circle + triangle | `0x00287730` `Player_Special` | [Moving attacks](#run-attacks) | confirmed (code), runtime |
| Strike shapes tested against bodies | `0x0033f110` `Human_TestStrikes` | [How a moving attack strikes](#moving-strikes) | confirmed (code) |
| A hit's damage | `0x0021b290` `Strike_Contact` | [The Anim Range List and damage](#damage-table) | confirmed (code) |
| The class's damages written over the list | `0x002548f0` `AnimRange_ApplyClassDamage` | [The Anim Range List and damage](#damage-table) | confirmed (code), runtime |
| The reaction id from the hit code, height and side | `0x00266d00` `Hit_PickReaction` | [Hit codes](#hit-codes) | confirmed (code), runtime |
| The target warned of an attack | `0x0021d5c0` `Human_WarnOfAttack` | [Attack warnings](#attack-warnings) | confirmed (code) |
| A hit on a held block | `0x00269f30` `Human_BlockHit` | [Block](#block) | confirmed (code) |
| The counter from a duck | `0x002617f8` `Block_DuckCounter` | [Block](#block) | confirmed (code) |
| Circle: grab or tackle | `0x00284920` `Player_GrabOrTackle` | [Grab and tackle](../combat-grabs.md#grab) | confirmed (code), runtime |
| Strikes, power strike, throws, mug and spin in a grab | `0x0027f3b0` `Player_UpdateGrabbing` | [In the grab](../combat-grabs.md#grabbing) | confirmed (code), runtime |
| The struggle in another human's grab | `0x0027fd68` `Player_UpdateGrabbed` | [Grabbed](../combat-grabs.md#grabbed) | confirmed (code) |
| In the mount | `0x0027ec20` `Player_UpdateMounting` | [The mount](../combat-grabs.md#mount) | confirmed (code), runtime |
| The throw by stick direction and walls | `0x0026dd08` `Player_Throw` | [Throws](../combat-throws.md#throws) | confirmed (code), runtime |
| The mugging stick game | `0x002856b8` `Player_UpdateMugging` | [Mugging](../combat-throws.md#mugging) | confirmed (code), runtime |
| Each update: block, damage, armour, reaction | `0x00265f70` `Human_ApplyPendingDamage` | [Damage](../combat-reactions.md#damage) | confirmed (code), runtime |
| The reaction played; stun and knockdown | `0x0026a6d0` `Human_PlayReaction` | [Reactions](../combat-reactions.md#reactions) | confirmed (code), runtime |
| Health gone: wounded, knocked out or dead | `0x002674c0` `Human_OnHealthOut` | [Defeat](../combat-reactions.md#defeat) | confirmed (code) |
| The power meter spent | `0x00226448` `Human_SpendPower` | [Power meter](../combat-rage.md#power-meter) | confirmed (code), runtime |
| L1 + R1 with a full meter | `0x00284340` `Rage_Enter` | [Rage](../combat-rage.md#rage-effects) | confirmed (code), runtime |
| A target kept or searched for an action | `0x0027a6c0` `Player_PickTarget` | [Target selection](../combat-targets.md#targets) | confirmed (code) |
| A strike at a breakable object | `0x00264178` `Player_ObjectAttack` | [Breakables](../combat-targets.md#breakables) | confirmed (code), runtime |
| The theft mini-games (the stereo) | `0x0027e6d8` `Player_UpdateTheft` | [The stereo theft](../combat-targets.md#stereo-theft) | confirmed (code), runtime |

### The other functions {#code-index}

The functions no section walks through are on [the combat function list](functions.md), each with its role and
level. By address they fall in four groups of the human code (`0x00217a70`-`0x00288000`, a range inferred from where its
functions lie; [Characters](../characters.md#code-index) has why it starts at `0x00217a70`) and a few helpers of the game
state; the groups are our grouping.

#### Attack checks, grabs, tackles and three-person moves {#code-grabs}

The checks before an attack starts ([An AI's attacks](../combat-targets.md#ai-attacks)), and the state changes that start
and end a grab, a tackle, its hold and a three-person (tandem) move, and the throw links. The state bits are in [State
flags](#state-flags); push weight is the human's attribute 7 (vtable `+0xe4`), set to 1e9 to make a body immovable and
back to 1.0.

#### Hit reactions, paired moves and the mount {#code-reactions}

The handlers that turn pending damage into a reaction by the victim's state ([Damage](../combat-reactions.md#damage), [Hit
codes](#hit-codes)), the pair-break clips, grab strikes, cuffing, the mount's end hooks, the alignment helpers and
revives.

#### Targets and commands {#code-targets}

The target searches and their filters ([Target selection](../combat-targets.md#targets)) and one small test per
[command](#commands).

#### Grab ends, holds and the stick games {#code-holds}

Ending a grab, the per-player action delay, and the stick games of the cuffing hold and of a mugged player
([Mugging](../combat-throws.md#mugging)).

#### Game-state helpers {#warriors-functions}

From the game-state module, by where its functions lie: the hit balance and the repeat tracker every human holds, the
reset of a per-player record, the rage loop's sound and two configuration setters.

## Data

### Commands {#commands}

`AddCommand(command, trigger, buttons, extra)` (the script binding, [character
bindings](../../references/bindings/character.md#addcommand)) fills nine tables of 12-byte entries `{u16 mask, u32 command,
u16 buttons, u16 extra}`; `global.lua` fills them for the street, and level 84's chase adds command `0x2d` on a button
of its own. Every binding is listed in [Commands](../../references/commands.md). The tables as read at runtime in the
street (confirmed (runtime)); the matchers and `PlayerCommand_Add`, which the binding calls to add an entry, are
confirmed (code):

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

These buttons are the pad word's bits ([Pad record](../frontend.md#pad-record)); the earlier reading on
[Characters](../characters.md#buttons) took square (`0x80`) for left (`0x8000`) and cross (`0x40`) for down (`0x4000`).

**Disabled commands** (`EnableCommand(player, id, 0)` clears the pad's bit in the entry's mask) are still matched:
the matcher stores them in the per-player record's pending `+0x24` instead of `+0x20`, so the human does not act on
them, but the `PadSetHandlerEx` handler, which reads `+0x20` and else `+0x24`, still receives them. Confirmed (code)
at `0x00147940` and `0x001480e0`; confirmed (runtime) for L1 (6) with pad 0's bit cleared
([PadSetHandlerEx](../../references/bindings/input.md#padsethandlerex)).

### State flags {#state-flags}

The 0x180 record's `+0x00` flags ([Characters](../characters.md#the-record)) that combat uses. Confirmed (code) where a
test is cited in the behaviour below; seen at runtime unless marked.

| Bit | Meaning |
| --- | --- |
| `0x1`, `0x2` | fight stance (`0x00228340` tests `0x3`); a player sets only `0x1`, scripts and AI set both ([Fight stance](../combat-targets.md#fight-stance)) |
| `0x4` | has a target (inferred: set with the target at a grab) |
| `0x10` / `0x20` | grabbed from the front / rear |
| `0x40` / `0x80` | grabbing from the front / rear (a front grab reads `0x45`) |
| `0x100` / `0x200` | mugging / being mugged |
| `0x400` | tackling, mounted on the target (seen `0x405`); the victim `0x800` (seen `0x801`) |
| `0x1000` | a paired power move: throws, power strikes, specials and stealth kills (seen `0x1005`; [Combat moves](../combat-moves.md#tandem-ai)) |
| `0x8000` | blocking (with `0x1`: `0x8001`) |
| `0x1000000` | sprint asked for ([Sprint](../characters.md#sprint)) |
| `0x2000000` | tagging ([Crimes](../crimes.md#tagging)) |
| `0x4000000` | in a mini-game (lock picking, the stereo, uncuffing; [Crimes](../crimes.md#mini-game-record)) |

Record `+0x08` holds the attack's **phase** while an attack plays: `0x1` wind-up, `0x2` the chain window, `0x4` the
end, `0x40000` recovery. `0x10` is set at a grab or tackle start and in object attacks (it also forbids a sprint).
`0x200` while tackling, `0x10000` on the tackled victim, and `0x20000000` on the victim just before the tackle hit.
Confirmed (runtime).

### Other record fields {#record-fields}

| Where | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| record `+0xb8` | u32 | the buffered next attack: cross 1, square 2, `0x14` 4, `0x13` 8; a snap square `0x102`, `0x202`, `0x402` | confirmed (code) at `0x00280708` |
| record `+0xbc` | s16 | the combo count; `Attack_Start` adds 1, `Player_UpdateChain` resets it to 0 for a snap, a low strike and an armed chain's second step | confirmed (code) |
| record `+0x118` | s16 | the damage pending this update (the largest of the update's hits) | confirmed (code) at `0x00264bd8` |
| record `+0x11a` | s16 | whether the hit may cause a reaction: 0 when the attacker's object at human `+0x44` has `+0x31` = 6 (not traced), else 1; an earlier reading took it for the hit's kind | confirmed (code) at `0x0021b290` |
| record `+0x100` | u32 | the time a **stun** ends (ms) | confirmed (code), runtime |
| record `+0x104` | u32 | the time a knocked-down human **gets up** (ms) | confirmed (code), runtime |
| record `+0xdc` | float | in a lock, the stick's angle from the facing (picks the combat-walk clips 380-387) | confirmed (code) at `0x00241b90` |
| record `+0xf4` | u32 | the L1 double-tap deadline (L1 released + 264 ms) | confirmed (code) at `0x00227b98` |
| human `+0xc4` | u32 | the handle of the other human in a grab (the grabber, for a grabbed human) | confirmed (code), runtime |
| record `+0x12c` | u32 | mugging progress, ms | confirmed (code), runtime |
| record `+0x144` / `+0x146` | s16 | **health** / its maximum | confirmed (code), runtime (player 314/900, civilian 600/600) |
| record `+0x148` | s16 | the **power meter** ([Power meter](../combat-rage.md#power-meter)) | confirmed (code), runtime |
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
| `0x8` | woundable (`HuSetWoundable`): health out leaves him wounded, not knocked out ([Defeat](../combat-reactions.md#defeat)); a death reaction plays 205 into the loop 200, and 195 when he is down, instead of 349 (`Human_PlayDeathReaction`, `0x00267b70`). Cleared on the victim by a grab or mount power strike that will defeat him (`0x0027f3b0`, `0x0027ec20`) and by a stealth kill from behind (`Player_StartStealthKill`) | |
| `0x20` | | the hit takes all the victim's health (`0x0021b290`) |
| `0x80` | reaction strength capped at 1 (`0x00266d00`) | |
| `0x100` | never stunned (`0x0026a6d0`) | |
| `0x200` | reaction strength -1 | |
| `0x400` | combo hits keep their full strength; strength +1 from an attacker with `0x200000` | |
| `0x4000` | escapes when the grabber's power runs out (`0x002562d0`) | **doubles** the damage dealt; ignores hit armour |
| `0x20000` | as `0x4000` for the power-out escape | |
| `0x80000` | | **raging** ([Rage](../combat-rage.md#rage-effects)) |
| `0x200000` | | reaction strength +1 (see `0x400`); ignores hit armour |
| `0x4000000` | spends no power (`0x00226448`) | |
| `0x1000000` | rubber-neck (`HuRubberNeck`, `0x0023a258`): no reader found; every load of human `+0xe0`-`+0xe7` in the executable (324 functions) was checked, and none tests this bit (inferred: unused) | |
| `0x40000000` | front-grab block (`HuBlockGrab` sets it with `0x80000000`): `Grab_IntroEnd` (`0x0026c1d8`) refuses a front grab when the victim **or** the grabber has it (the intro ends in 69, the miss); a weapon's paired kill skips a target with it (`0x002880d8`) | the player's own: no spin from a rear grab to the front (`Player_UpdateGrabbing`, `0x0027f3b0`), no return from the mount to the front grab (`0x0027ec20`) |
| `0x80000000` | rear-grab block (`HuBlockRearGrab`): `Grab_IntroEnd` refuses a rear grab when the victim or the grabber has it; a weapon's paired kill skips a target with it; a grabbed player with it cannot reverse the grab (`Player_UpdateGrabbed`, `0x0027fd68`) | the player's own: no spin to the rear (`0x0027f3b0`); a special attack skips its grab path (`0x00263c90`, [Strong grapple](../combat-grabs.md#strong-grapple)); with `0x40000000` too, a circle tap does nothing (`Player_GrabOrTackle`, `0x00284920`: no intro, no miss, no grounded mount), while circle held still tackles (only `0x100000000` stops that) |
| `0x400000000` | a block holds even against strength 3 (`0x00269f30`) | |
| `0x2000000000` | never escapes when the grabber's power runs out | |
| `0x20000000000` | one hit cannot take health below a fraction of the maximum (`0x00265f70`); it then sets `0x10` | |
| `0x100000000000` | never picked as a target (tested by `0x00227d78` in the filter `0x00279410`; not `HuSetNoAutoLock`, which sets `0x8000000000`, a bit with no reader found) | |

**Killer mode and double damage in a hold.** Both apply to every move of a hold. The grab strikes (`Grab_Strike`,
`0x0026e5f8`), the paired moves (`Attack_StartPaired`, `0x00262ac8`: power strikes, throws, the mount power strike),
`Grab_StartPairedMove` (`0x00263380`) and the mount strikes (`0x00270b38`, `0x00271ef0`) all deal their damage through
`Human_DealDamage` (`0x0021d680`) with the attacker passed, and that function applies the attacker's `0x20` (a non-zero
hit takes the victim's whole health) before `Human_AddPendingDamage` (`0x00264bd8`) doubles the pending damage for the
attacker's `0x4000`. The grab and mount power strikes even double the damage themselves when they test whether it will
defeat the victim (`0x0027f3b0`, `0x0027ec20`). Links that deal 0 stay 0. Confirmed (code) at the call sites
`0x00262f28`, `0x002637cc`, `0x0026e690`, `0x00270f50`, `0x002722d4`.

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
([Animation](../formats/animation.md#anim-range-list)); `Strike_Contact` (`0x0021b290`) reads it with
`AnimRange_Damage(attacker, anim id)` (`0x002542e8`). The value on the disc is **not** what plays: when the human is
made (`Human_AttachInstance`, `0x00217a98`), `0x002548f0` writes the character class's damages (`CfgChar`'s 16-bit
values at class record `+0xb8`, [Character classes](../characters.md#classes)) over the list, through the jump table at
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
   has `0x80`; at most 3. The combo ids are 13, 14, 15 and 17-20: both tests skip 16 `SS2`. Confirmed (code).
2. **Height** = the code's, +1 when the attacker stands 0.3-0.9 m higher, +2 when 0.9-1.5 m; -1 / -2 when lower; at
   most 2. A low hit (height 0) is always strength 2.
3. **Side**: 0 when the attacker is within 45° of the victim's front, 3 on its left, 1 on its right, 2 behind
   (`Hit_GetSideFromPoint` `0x00267338` via `Human_GetSideOf` `0x002672d0`; confirmed (code), cos 45° = 0.7071). The
   reaction's direction = (code direction + side) & 3.
4. The id is the s32 table at `0x00510798`, index strength × 16 + height × 4 + direction; a -1 entry (or an index
   above 64) falls back to 272, the table's entry 10 (`Hit_PickReactionClip` `0x00266f38`; confirmed (code)). Dying uses
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
runtime](../combat-reactions.md#being-hit-runtime)) (confirmed (runtime)): `S1` (`0x0a`) from in front gives 272 and from
the victim's left (61-71°) 275; `X1` (`0x09`) from its left gives 274; `SSS3` (`0x26`, a combo id, so strength 1) gives
276; the special 653 (`0x26`, strength 2) from the left gives 291 and a knockdown.

### Constants {#constants}

Read at runtime in the street; the setters are on the [config bindings](../../references/bindings/config.md).

| Address | Value | Use | Evidence |
| --- | --- | --- | --- |
| `0x0050b708` | 7 | samples a history hold needs (circle → tackle) | confirmed (runtime) |
| `0x0051024c` | 0.25 | the health floor of victim flag `0x20000000000`, as a fraction of the maximum; `HuSetDemiGodMode` overwrites it | confirmed (runtime) |
| `0x00510274` | 0.6 | the power fraction a grabber loses when a third human hits it | confirmed (runtime) |
| `0x00510278` | 0.25 | `CfgPowerEndurance`: the power fraction a power strike needs and a throw costs | confirmed (runtime) |
| `0x00510290` / `0x00510294` | 5000 / 20000 | `CfgRageHandlers` (ms) | confirmed (runtime) |
| `0x005102b4` | 1 | `CfgSnap`: snap attacks on | confirmed (runtime) |
| `0x005101e4`-`0x005101f0` | 0, 15, 15, 2 | `CfgBurnRates`: power drained per second blocking, grabbing, tackling, and in a further grab state | confirmed (runtime) |
| `0x005102b8` / `0x005102bc` / `0x005102c0` | 1000 / 15 / 250 | `CfgButtonMash`: target, decay per update, press gain (used halved); only theft mode 1 reads them | confirmed (runtime) |
| `0x005104ac` / `0x005104b0` / `0x005104b4` / `0x005104b8` | 0 / 0 / 0 / 1 | `CfgLockOn`, auto-combat, `CfgAutoLock`, `CfgAutoLockAndCombat` | confirmed (runtime) |
| `0x005104bc` / `0x005104c0` / `0x005104c4` | 4 / 6.25 / 9 | `CfgDistances` (`Cfg_SetCombatDistances`, `0x0023a630`), squared: 2, 2.5 and 3 m as `config_preload2.lua` sets them at boot (the executable's initial values 4 / 9 / 36, 2, 3 and 6 m, never apply in play); the stance's enter, swap and keep distances ([Fight stance](../combat-targets.md#fight-stance)) | confirmed (runtime) |
| `0x005108d8` / `0x005108e0` / `0x005108e4` | 25 / 1.0 / 0.1 | `CfgRagePoints`: the cap, and the factors below and above it | confirmed (runtime) |
| `0x00510798` / `0x00510898` | 64 s32 each | the reaction and block reaction tables ([Hit codes](#hit-codes), [Block](#block)) | confirmed (runtime) |
| `0x0051096c` / `0x00510970` | 0.9425, 2 | target search: the half-angle (54°) and the height difference limit (m) | confirmed (runtime) |
| `0x00510980` | 6, 4, 3, 3, -1 | not traced (a short table read by the grab code) | confirmed (runtime) |
| `0x00510988` | 3 | the command that spins a grab or reverses one (R1 pressed) | confirmed (runtime) |
| `0x00510998` | 0.2 | the power cost of a grab strike, halved for a player | confirmed (runtime) |
| `0x00514878` | 4, 1.21, 4, 4, 3.0625, 2.25 | `CfgActionDistance`, squared | confirmed (runtime) |

The player's **power class** record (64, `0x006619a0 + 64 × 0x44`, [Power classes](../characters.md#power-classes)):
`+0x28` = **400, the power meter's maximum**; `+0x2a` = 60, its refill per second; `+0x04` = 0.3, the **hurt**
threshold (below 30 % health); `+0x30` = 200 ms a stun; `+0x34` = 2750 ms on the ground; byte `+0x36` = 3, the grab
struggle divisor. The street civilian (class 2): hurt below 35 %, stun 750 ms, ground 2000 ms, power 200, divisor 4.
The **Warrior class** record (`CfgWarriorClass`, `0x006b65c0 + c × 14`; class 6 for the player here, human `+0x1ba`,
picked by type: [Power classes](../characters.md#power-classes))
was `4e 00 90 c8 f0 32 73 03 01 01 01 02 03 02`: s16 `+0x00` = 78 is the **rage maximum**, byte `+0x02` = 144 the
rage gain percentage, `+0x03` = 200 the idle rage decay and `+0x04` = 240 the rage drain while raging (percent of
the maximum per 20 s), `+0x05` = 50 the player's health percentage, `+0x06` = 115 the player's damage scale, byte
`+0x07` picks the cuffing hold's times, `+0x08` the mash gain factor, `+0x09` the tagging tuning, `+0x0a` the
lock-pick difficulty plus 1, `+0x0b` the stereo theft's turns and `+0x0c` the mugging parameters; `+0x0d` is never
read. Confirmed (runtime) for the bytes; the uses confirmed (code) at the functions below and on [Warrior
classes](../../references/warrior-classes.md), which lists every byte's reader.

## Behaviour

### The dispatcher {#dispatch}

`Player_UpdateActions` (`0x0027c120`) runs every update for a pad-controlled human. Confirmed (code); in order:

1. **Block** if the fight test `0x00224f28` holds and R1 is held (mask 8), or if already blocking (`0x00223ad0`:
   state `0x8000` or `+0x08` `0x1000`) and the command is `0x1f` without a full rage meter, or 8 (L1 released). It
   clears the sprint, calls `0x002801e8`, sets state `0x8001`, starts rage on `0x1f` with a full meter
   (`0x0027ffa0`), and returns 0 on command 4 (R1 held), so nothing else runs while R1 is held. This is the sprint's
   "other clear" on [Characters](../characters.md#sprint). R1 held is read from the pad record (`0x00147f98`, 0 when
   per-player `+0x19` is −1), so **an AI human never starts a block**: command 4 alone does nothing
   ([AI](../ai.md#block)). The fight test fails while the human runs or sprints with record `+0x08` clear (gait 4 or
   5, `0x00223a60`, `0x00223a98`), so **R1 at a run starts no block**: the run goes on and R1 only turns the camera
   round ([Camera](../camera.md#reverse)); the block starts the update the gait drops below 4. Confirmed (code);
   confirmed (runtime).
2. The chain (`0x00280630`), then the sprint (`0x0027d5a0`). Confirmed (code).
3. The state routes: mugging (`0x100`) → `Player_UpdateMugging`; being mugged (`0x200`) → release; a theft
   (`0x4000000`) → `Player_UpdateTheft`; `0x8000000` / `0x10000000` → nothing; `0x400` → `0x0027ec20` (tackling);
   `0x800` → `0x0027f1a0` (tackled); `0x18000000000` → `0x00284140`; grabbing (`0xc0`) → `Player_UpdateGrabbing`;
   grabbed (`0x30`) → `0x0027fd68` ([Grabbed](../combat-grabs.md#grabbed)); a power move (`0x1000`) → `0x0027df38`;
   `0x2000` → `0x0027e018`; `0x200000` → `0x0027e040`. Confirmed (code).
4. The commands: `0x36` → `0x0027de78`; `0x37`, `0x38` → `0x00287fe0`; triangle `0xb` with human `+0x5b8` →
   `0x002811f0`; `0x33` → `0x00281188`; `0x32` → `0x002832c8`; `0x22` or `0x39` → `0x00288838`; `0x22`, `0x30`, `0x23`,
   `0x24` → `0x00287730`; **`0x20` → charge**; **`0x21` → dive**; **`0xf` → square**; **`0x10` → cross** (weapon types
   4, 5, 6 → `0x002880d8` instead); **`0xd`, `0xe` → grab or tackle**; `0x27` → `0x00286ba8`; 7 (L1 pressed) →
   `0x0027da10`; 6 (L1 held) → `0x0027dc00`; 8 → `0x00227b98` / `0x00227a90` ([Targets](../combat-targets.md#targets));
   **3 → the AI's grab or tackle counter** (`0x0027d6e0`), only when per-player `+0x1b` is 0 ([AI](../ai.md#block)).
   Commands `0x30`-`0x39` are not made by the street's tables; scripts or weapons make them (inferred). `0x00288838`
   acts only for armed weapon types; `0x30` matches no branch of `0x00287730` (inferred: unused there).

### Attacks and chains {#attacks}

**Square** (`Player_Square`, `0x00286cc8`) takes a target from `Player_PickTarget(0x40000000, h)` (in the fight stance
it keeps the current target within 2.5 m, `0x005104c0`, and searches only without one or beyond; cross always searches,
[Combat targets](../combat-targets.md#targeting)), then, confirmed
(code):

- a grounded target → 193 (`0x00261a08`); a tackled one → 212; a grabbed one → 120: these three are tested only on
  the fight-stance path, after the strafes and snaps; outside a stance the run and walk attacks test only the low
  target (193), and cross has its own 194 and 661 ([Combat moves](../combat-moves.md#cross)); confirmed (code);
- a breakable object → `Player_ObjectAttack` ([Breakables](../combat-targets.md#breakables));
- at gait 4 (run) with record `+0x08` clear and the stick above 0.95 → 24, from a run; at gait 1-3 with the stick at
  0.12 or more → 23, from a walk. These come **before** the snap (gait tests `0x00223a30`-`0x00223a60`), but the
  whole block, run and walk alike (and the `0x00510270` attack 22), is skipped **in a fight stance** (record `+0x00`
  & `0x3`, `0x00228340`) or when `0x0051031c` is set for a pad-controlled human (0 in the executable's data).
  Confirmed (code) at `0x00286cc8`; cross (`0x00287a18`) skips its run and walk attacks on the same test. So a snap
  comes from a player in a [fight stance](../combat-targets.md#fight-stance) (a locked-on combat walk is gait 3, but the
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

**Cross** (`0x00287a18`) starts 11, `X1`, on command `0x10`; at a run (gait 4, or 5, with record `+0x08` clear) it plays
the run attack 24 and at a walk the walk attack 23, as square does, but has no snaps. Confirmed (code), confirmed
(runtime). With a weapon in hand both buttons follow [Moving attacks with something in
hand](../combat-throws.md#armed-moves).

Square, cross and the chain are the only callers of `Attack_Start` (`0x002625a8`), which starts the attack clip and
adds 1 to the combo count (record `+0xbc`). Confirmed (code).

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
with the fixed recorder ([Hooks](../../guides/research-traces.md#hooks)), in slot 6 with PoizoCiv placed 0.95
m ahead, facing the player, and then left free (he walks in). Each hit is the update whose sample first shows the
damage. The values held, except `X1`, which hit on update 8 in one run and 7 in two. Those three runs had the same input
up to the hit, but the player's turn toward the target differed between them, so something outside the input varies. A
target standing still further out is hit later (`X1` on 10 from 1.0-1.6 m, [Where the attacker
stands](../combat-targets.md#reach)):

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
([Moving attacks with something in hand](../combat-throws.md#armed-moves)).

**The charge**: **L2 held, then cross pressed** (command `0x20`) at gait 4 with record `+0x08` clear, or at gait 5
(a sprint), plays 0 (`gen_charge_shoulder`) for about **0.9 s** at about 7.45 m/s. **The dive**: L2 held, then
square pressed (`0x21`), plays 1 for about **2 s**. Confirmed (code) at `0x0027d800` / `0x0027d900`, confirmed
(runtime). At a walk or standing the combination does nothing (the cross or square press under it is overwritten,
inferred from the matching order).

#### Where the charge and the dive aim {#charge-aim}

The two differ. Confirmed (code) unless marked:

- **The search** (`Attack_FindNearestInReach`, `0x0027b058`), used by both: it writes the aim heading (the camera-turned
  stick's angle above 0.01, else the facing; `Player_GetAimHeading` `0x0021d1a0`) to record `+0xd4`, sets the cone
  `0x0051096c` to 0.9425 rad (54° each side) and writes the clip's **reach** to record `+0xd8`, which
  `Humans_FindInRange` (`0x00227598`) and `ObjectList_FindInRange` (`0x0039ab20`) use as a **minimum** distance. It then
  takes the humans **between the reach and the far range** inside the cone that `TargetFilter_EnemyStanding`
  (`0x00279568`) accepts; with none, the strikeable world objects in the same band, then the glass panes; the nearest
  wins. There is **no any-angle or wide pass**, unlike `Player_PickTarget`. For Rembrandt the band is **3.29-4.11 m**
  for the charge (id 0) and **2.22-3.00 m** for the dive (id 1) ([Frame data](../combat-moves.md#frame-data)).
- **The charge** (`Player_StartCharge(h, 0)`, `0x00264a80`) **neither steers nor sets a target.** It reads the current
  target; for a human whose brain `+0x4` is 0 (the player; inferred) with none, it runs the search, and whichever human
  it has is only warned (`Human_WarnOfAttack`, `0x0021d5c0`). The clip then runs straight along the facing. Its
  record `+0x08` bit `0x1000000` does not make the human busy, so **the stick still turns the charger**: the
  locomotion's turn runs while its velocity is zeroed ([Tasks](../tasks.md#locomotion-gate)), and the clip's root motion
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
special 653 (645 raging); **circle + cross** (`0x23`) is the **strong grapple**, a grab whose connecting clip is the
strike 657 (649 raging) and which ends in the hold ([Strong grapple](../combat-grabs.md#strong-grapple)); **circle +
triangle** (`0x24`) is **the spray** 664 (`SPECIAL_SPRAY`, below). The id is 645 or 653 + 4 × variant + 0 / 2 by side
(`0x00263c90`). A special that is not paired needs and spends 0.25 of the power meter. `Player_SpecialAttack` first
pops the grounded move style 15 when the human is not knocked down (state `0x80000`); `Human_KnockDown` pushes 15
with that state and `Human_GetUp` (`0x0022f3c8`) pops it as it clears the state, and a knockdown and its rise never
showed 15 without `0x80000` (confirmed (runtime), run r-20261025-grounded-style-window), so a player reaching the pop
was not seen. At runtime (confirmed
(runtime)): cross held, square pressed 40 ms later played 653, power 400 → 300, the civilian lost 6 and fell (291,
grounded 196, up with 199 2.0 s after the hit); circle + cross at a victim getting up played 359 and then `X1`.

**The refusal**, confirmed (code) at `0x00287730`: none of the three starts while record `+0x08` holds any of
`0xaeebf7ff`, or for a **player** (human `+0x1b0` not -1) **holding a world object** (`Human_IsHoldingObject`,
`0x00224060`: the held object's type `+0x86` not 0, `0x00231a38`; whatever its anim set, and a held human is not one).
A human no player controls is not refused for what it holds.

**The spray** (`0x24`), confirmed (code) at `0x00287730` unless marked:

1. A pad-driven human (per-player record `+0x1b` set) with no spray paint (`Human_GetSprayPaint`, `0x00222818`: an
   AI's own count `+0x374`, a player's item 3) takes the command and does nothing more. A human its brain drives is not
   asked for paint.
2. Whatever is in the hand is dropped (`Human_DropHeld`, `0x00257f38`); by the refusal that is only ever an AI's.
3. A fresh target search within 664's far range (`Player_FindAttackTarget`, `0x0027a4b0`), then 664 starts at that
   target (`Attack_StartAtTarget`, `0x00261c80`): steered onto it within the range, else turned to face it, the chain
   cleared and held flag `0x10` set; with no target it plays where he stands.
4. One paint is taken (`Human_AddSprayPaint(h, -1)`, `0x00222868`: an AI's count kept within 0-9, a player's item 3
   through the inventory, with no pick-up notice).
5. **No warning and no kept target.** Nothing on this path calls `Human_WarnOfAttack` (`0x0021d5c0`, none of its
   callers is `Player_Special`, `Attack_StartAtTarget` or `Attack_SteerToTarget` `0x002761c8`), so the human sprayed
   at is not told of the attack. The search's result is passed to the start and not written to the target (human
   `+0xc8`); the start, the steer and the chained clip's end callback (an empty function just before
   `Attack_StartAtTarget`) leave it as it was.
6. **The use event** (664's `use` at update 14, message `0xc1`, `Human_HandleMessage` `0x00245920`) only reads the
   target: when the carried prop (`+0x348`) has type `+0x86` = `0x10` or the held object has type `+0x86` = `0x27`
   (inferred: the clip's spray can, either way), and the kept target (`Human_GetTarget`) is a human, that human's
   `+0x644` takes the sprayer's paint colour `+0x640` (byte-swapped; skipped while it is `0xefefefff`) and he gets
   say-command `0x22`. So the paint lands on the target kept before the spray, not on the human the search found.

At runtime (confirmed (runtime), run r-20261025-spray-keeps-no-target): with the player 1.2 m from a pedestrian and
no target kept, the search found the pedestrian and 664 played at it, while human `+0xc8` stayed -1 at the start, at
the use event and after the clip, which was followed by 389.

This page said before that the command "sprays a tag where the player may tag"; the code starts an attack at a target,
not at a tag spot (tag spots are triangle's, [Tagging](../crimes.md#tagging)). At runtime (confirmed (runtime), run
r-20261025-circle-triangle-spray; level99 checkpoint 2 facing a pedestrian, the pad holding circle + triangle for one
update): with 3 paint the player's playing anim id (record `+0x20`) went to 664 and the paint to 2, and the hand
(`+0x338`) held an object while 664 played (inferred: the clip's own spray can); with no paint 664 never played, the
paint stayed 0, and circle's release then played the grab's 71 and the miss 69 and 389, as a lone circle tap does.

#### How a moving attack strikes {#moving-strikes}

A moving attack has no single hit update. Its clip's events switch **strike shapes** on and off, and while any is on
`Human_TestStrikes` (`0x0033f110`) tests them **every update, after the move**, against every body near the human: the
first shape that meets a body calls `Strike_Contact` (`0x0021b290`) for it. "Meets" is a **sweep**: each shape is moved
from its pose at the last update to its pose now through the physics sweep table, so only the spheres can reach a box (a
segment never does), and a shape just switched on is tested with no move ([Physics: Unit
5](../physics-kernels.md#u4b-how-human-teststrikes-builds-the-sweep-0), confirmed (code)). The reach is the posed body
itself: there is no reach or height figure of its own. Confirmed (code) at the addresses below; the timings are
confirmed (runtime).

**The shapes** (confirmed (code), `IPhysics_Construct` `0x0033c288` with the tables at `0x00512810`-`0x005128f0`).
Each of the 60 human bodies holds its capsule (type 3, radius 0.35 m, height 2.0 m, [Physics](../physics.md)) and ten
**bone shapes**, identified by a bone index (shape `+0x31`). A **segment** (type 4, `0x003437b0`; posed by
`0x00343810`) runs from the bone's position plus the bone's rotation times the offset, along the bone's local x axis,
for its length, with its radius around it; a **sphere** (type 2, `0x00342aa0`) is centred at the bone's position plus
the rotated offset. Both are then carried into the world by the human's transform. **The body scale** (human
`+0x65c`, 0.97 for Rembrandt) reaches every part of a shape: `PhysicsBody_ApplyHumanScale` (`0x00341f28`) multiplies
the offsets, radii and lengths once, and the bone each pose reads (owner slot `+0xb8`, `Human_GetBoneTransform`
`0x0023bde8`) comes from the bone cache with its **position multiplied by `Human_GetScale`** (`0x0021d020`) and its
rotation as is, so a shape of a scaled human is its unscaled pose times the scale about the feet. Confirmed (code) at
`0x0023bde8` and the poses `0x00342b08` (sphere; `0x00342aa0` only constructs it) and `0x00343810` (segment), which
use the bone as returned; `Human_TestStrikes` (`0x0033f110`) tests the shapes as posed and scales nothing again
([Physics: transforms](../physics-bodies.md#u6-transforms)). Coney poses them so
(`Human::posedStrikeShapes`). The limbs are named from the
skeleton's [parent table](../formats/animation.md#the-pose) (inferred); left and right follow the hand bones (25 right,
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

**The clip events** (confirmed (code) at `Anim_FireFrameEvents` `0x00101dd8` and `Human_HandleMessage`; the events read
from the disc's clips). The 24-byte event's `+2` type picks the message and its words `+4` and `+6` are pushed as
arguments (the message's argument stack is last-in first-out, `Msg_PopArg` `0x003a8518`):

| Event type | Message | Words | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0xf` | `0x8f` | `+6` bone id, `+4` the window's length in frames | `Human_StrikeShapeOn` (`0x00247fc0`): that shape on | confirmed (code) |
| `0x10` | `0x90` | `+6` bone id | `Human_StrikeShapeOff` (`0x00248110`): that shape off, and the held object's contact list cleared | confirmed (code) |
| `0x13` | `0x93` | (`+4` ignored) | `Human_StrikeAllOn` (`0x00248170`): flag `0x2` on the capsule, then all ten on | confirmed (code) |
| `0x14` | `0x94` | | `Human_StrikeAllOff` (`0x00248270`): the capsule's `0x2` off, all off | confirmed (code) |

The disc's 1,752 distinct clips have 315 type-`0xf` events (by bone: 25 ×99, 19 ×52, 24 ×41, 18 ×28, 29 ×22,
30 ×20, 32 ×17, 33 ×15, 3 ×7, 6 ×5, and 23, 0, 4, 28, 31, which have no shape) and 52 clips with type `0x13`: the
charge and dive, the tackle miss, the body check, the counters, the throws' and power moves' victims, the
**extreme reactions** 296-299 (`gen_hit_react_low_*_ex`) and the building jump's loop. The wheelchair turns all on
above 4.5 m/s and off below (`Human_MoveWheelchair`, `0x002427e8`; confirmed (code)). A bone's window always ends with
its own type-`0x10` event, and in the
rage moves the `+4` word equals the frames from on to off.

**With a melee object in the right hand**: a type-`0xf` event for bone 25 while the object in hand (`+0x338`) is a
human-held melee object (object flags `0x30000`) makes strike spheres on the **object's** body instead
(`Human_BuildWeaponStrikeSpheres`, `0x00247be8`): for an object without flag `0x20000` and not of `ObjectAttribs`
kind (`+0x86`) 39, a row of spheres of radius *r* = 0.1 m along the object's local y: the first at −`+0x70` + *r*,
the last at `+0x7c` / 2 plus the y of the model's bound (`0x003917d8`), and between them one every 3 *r* (the row
reaching 0.9 m further each way for kind 36; `+0x70`, `+0x7c` are attribs of the object type); for kind 39
one sphere of 0.8 m; for an object with `0x20000` one sphere of half its smallest extent (`+0x78`, `+0x7c`, `+0x80`;
`+0x78` alone when attribs `+0x84` is 2), at its origin. The event's `+4` word plus 1 goes to the object's body
`+0xd0`. Confirmed (code); the attribs fields' meanings are on [World objects](../objects.md#held).

- **What it hits.** Humans and objects go through the same test. A human's body is tested against the shapes of the
  target that carry flag `0x4`, or the target's capsule when the attacker's clip has flag `0x10000`
  ([The capsule strike](#capsule-strike)); an object's
  against its own shapes; a shape touching the level mesh calls `Strike_Contact` with no object (a material impact
  sound only). `Strike_Contact` then does what a hit on that thing does: damage from the clip's
  [Anim Range List](#damage-table) value for a human, message 1 of kind 2 for an object
  ([World objects](../objects.md#door-break)), and for a pane its break. A body struck is put on the striking body's
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
  (`IPhysics_QueryBox`, all masks) returns around the human, **foes and allies alike**, except a body of the same body
  group (`+0x3c`, when not 0), one already on the striking body's contact list, a body with flag `0x8` whose owner has
  `0x4000000`, and one **behind the level**: a ray from 1 m above the human's root to the body's centre
  (`WorldManager_RayCast`, mask `0x48`, or `0x8` for a body with `0x40`) must hit nothing. The friend test comes after,
  in `Strike_Contact`: against a friend (`Human_IsFriendly`) the hit event goes out with its hostile byte (`+0x11`)
  clear (the "may react" value `+0x11a` is unchanged), a Warrior (brain type 3) struck by a player who has no target may
  complain (speech command `0x58`), and `Human_ApplyPendingDamage` drops the damage unless both are players
  ([Damage](../combat-reactions.md#damage), step 3) but still plays the reaction
  ([Teammates](../combat-targets.md#teammates)).
- **When a clip is cut** before its off event, the shapes still go off: every animation task gives its clip back
  through `CharacterInstance_ReleaseClip` (`0x00175120`) when it ends, is replaced or chains on, and for a clip with
  descriptor flag **`0x4`** that runs `Human_StrikeAllOff` (all ten shapes and the capsule's `0x2` off). Flag `0x4` is
  exactly "has strike events": of the disc's 1,752 clips, all 221 with a type `0xf` or `0x13` event carry it, plus
  `falling_frm_brk_hit` without one ([Clip flags](../animation.md#clip-flags)). Explicit offs as well: dropping a type-5
  overlay (`Human_DropOverlayTask` `0x00229620`: a play-anim action's abort, `Climb_Start`, the fidget, taunt and
  look-around stops), `Attack_StartPaired` (`Human_EndOverlayAndStrikes` `0x002628c0`), the wheelchair below 4.5 m/s,
  and the human's body leaving the physics (`IPhysics_RemoveHumanBody`). A clip faded out keeps its task until the
  fade ends, so its shapes stay on through the fade unless its own off event comes first (inferred from the task
  release). Confirmed (code) at the addresses cited; the flags read from the disc.
- **Which shapes, and when** (updates of 1/30 s counted from the update that starts the move, update 0, on which its
  held flag and clip id first show and its clip time is still 0; slot 10, stick 100 % straight ahead, needed for gait
  4):

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
  read every update. The move is pushed by the dispatcher (step 9 of [Humans_Update](../tasks.md#humans-update)) on
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
  ([Barriers](../objects.md#barriers); 10 updates after the hit in the charge run, 11 in the dive's, 2 in the walk
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
  clip tasks' `+0x24`, `Anim_FireFrameEvents`' event bits) is given `0x800`, and the only immediates `0x800` in `Human/`
  are state-word and human-flag bits (inferred from a search of every `li`/`ori` of `0x800`; a value read from data
  was not ruled out). So it behaves as a dead "airborne" override.
- **After the break** the script takes over ([Barriers](../objects.md#barriers)): the fence's message 2 reaches
  `P3.FenceBroken`, and 15 updates later the player stood at (18.5, −16.3), heading 178°, beyond the fence. That is 15
  updates after the removal in every run, so the time from the strike to the placement follows the removal: 25 updates
  in the charge run (32 in an earlier walk-attack run), 26 in the dive run, 17 in the later walk-attack run.

Confirmed (runtime), PCSX2 2.9.94, copies of slot 10, scenario
[`charge_fence`](repo:research/traces/scenarios/charge_fence.toml) (hooks `strike-shape`, `strike-contact`,
`barrier-hit` and `object-remove`); the open-ground run turned the stick to 100 % straight back. The update numbers come
from runs recorded with the fixed recorder ([Hooks](../../guides/research-traces.md#hooks)); the dive (square for cross)
and the walk attack (stick 50 %, cross at update 85) are variants of its input. The strike shapes went on and off on the
updates the frame tables give (charge 4-17, dive 2-24, walk attack 5-11, [Combat moves](../combat-moves.md#frame-data)).

#### The capsule strike {#capsule-strike}

A clip with flag `0x10000` ([Clip flags](../animation.md#clip-flags)) strikes a human target's **capsule**, not its
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
4. The pair goes through the [sweep table](../physics.md#dispatch), row = the strike shape's type, column 3:
    - a **segment** (shin 29 / 32, forearms) uses the static overlap `0x00345d38` → `0x00346fb8`. It takes the closest
      points of the bone segment and the capsule's axis (from its base straight up for its height,
      `Segment_ClosestPoints` `0x0034ad50`). It hits when their distance is at most the segment's radius plus the
      capsule's radius. Confirmed (code).
    - a **sphere** (foot 30 / 33, hands) uses `0x00345670` → `Sweep_SphereCapsule` (`0x00349a60`): the sphere moved
      by its displacement this update, against the upright capsule, with the two radii added; a hit at a fraction
      from 0 to 1 counts. Confirmed (code).
5. The first hit ends the test of that body: `Strike_Contact` (owner vtable `+0x104`) with the contact (`+0x04` the
   strike shape, `+0x40` the target, `+0x44` the target shape), and the target goes on the contact list.

**The capsule** (confirmed (code), `PhysicsCapsule_Pose` `0x00343518` and `PhysicsBody_ApplyHumanScale`
`0x00341f28`; see also [Physics](../physics.md#human-body)):

- **Radius:** `+0x40` = 0.35 × the human's scale (`Human_GetCapsuleRadius` `0x00219860`; confirmed (code)): 0.3395 m
  at scale 0.97.
  The walking sweep's body factor `+0x60` (1.4286 for the player, [Characters](../characters.md#walls)) is not applied
  here.
- **Base:** `+0x50`, at the human's position (the feet), each update.
- **Height:** `+0x44`, the height of the head bone (6) above the feet + 0.2 × scale, so about 1.8 m standing.
  It is shorter in some states, by the rules on the Physics page; 0.35 × scale when cuffed; never under 0.2 × scale.

So a strike shape at shin or foot height (0.1-0.4 m) that comes within its radius plus 0.34 m of the target's
vertical axis hits it. A strike shape tested against the spine (about 1.0 m up) and head (about 1.6 m) never could.

**At runtime** (confirmed (runtime), PCSX2 2.9.94, a copy of slot 6). Rembrandt was given a full rage
meter (78) and rage started with L1 + R1 (clip 643, updates 15-74). PoizoCiv stood 1.0 m ahead, its brain and stick
off. Cross + square at update 85 played **645**. The shin 29 and foot 30 went on at k15 (update 102) and off at k35
(update 122). On update 102 `Strike_Contact` ran once with the strike shape of type 4, bone 29 (the shin segment) and
the target shape of **type 3, bone `0xff`** (PoizoCiv's capsule). PoizoCiv lost 10 health (600 to 590), the table's
10 ([combat-moves](../combat-moves.md)), and played reaction 285. With PoizoCiv 1.6 m ahead the same 645 missed: the
target was 1.63-1.74 m away while the shapes were on.

**Clips with the flag** (the disc's clip headers, `+0x44`): only `gen_rage_sweep` (645 / 647, the rage special of
`warr_fo_header` and `warr_re_header`; other sets put `gen_spinpunch`, `gen_360clearkick` or other clips in 645) and
`gen_sweep` (653 / 655 of `moon_lt_header`) carry `0x10000`, both with `0x10004`. Every other strike clip is tested
against the spine and head.

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
([Tasks](../tasks.md#locomotion-gate)): the recovery `0x40000` is in its velocity mask `0x110c0880`, the phases `0x1`,
`0x2`, `0x4` and the grab's `0x10` make the human busy (`0x00223cb0`), and the clip's task clears them as it ends
([Tasks](../tasks.md#held-flags)). The block's extra 5 updates are the idle's 0.15 s fade, which holds `0x10000000` (in
the velocity mask) while it runs; the state code `+0x14` = 5 written then only means turning in place
([Tasks](../tasks.md#locomotion-gate)).

### Attack warnings: who hears an attack coming {#attack-warnings}

**One human per warning: the target.** `Human_WarnOfAttack(victim, attacker, id)` (`0x0021d5c0`) sends event `0x10`
to the one victim it is given, and only when that victim can see the attacker (`Human_CanSeeHuman`, `0x002223e8`,
with the victim's field of view `+0x12c` and sight range, or 2 m when the attacker's brain `+0x2d4` is set, then a
line of sight); it also makes the victim drop carried props unless cuffed. Nothing in the game loops over nearby
humans to warn them: every one of its 23 call sites passes a single human, the move's target. Confirmed (code):

- **`Attack_Start`** (`0x002625a8`, the call at `0x00262674`) warns its current target (`Human_GetTarget`), and only
  when that target is within the clip's far range; out of range it turns to the target and warns no one; with no
  target, no one. Confirmed (code).
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

**The dive** is not among the call sites above, so whether it warns anyone is an [open
question](#open-questions) (the dive's warning).

**What others hear.** Only indirectly: the warned human's shared handler (`0x00292d80`) makes the gangs enemies and
sends a **help call** within 20 m (`Gang_BroadcastHelpCall`, `0x00293640`: event `0x14` through `AI_AlertNearby`,
naming the attacker) when the warning passes its own sight test ([AI: events](../ai-code.md#brain-events)); confirmed
(code) at `Brain_DefaultOnEvent` (`0x00292d80`), event `0x10`. A human who is not the target never gets `0x10`, so
never counts `+0x200`, blocks or counters for it.

**The `level5` bosses** ([AI: Diego and Vargas](../ai-gangs.md#boss-diego-vargas)). `BigBrawlerGoal_OnAttackWarning`
(`0x002e8858`) acts on `0x10` only in state 2 and not while tired, and then: when the goal holds actions and the front
one is of type 1 (`MoveTo`) or 2, **clears the action queue**; counters an escapable grab or tackle from his current
enemy (command 3); makes the attacker his target when he has no enemy or his enemy is an AI human (a player enemy is
kept); and, with the attacker within 1.5 m, 10 % health or more, no held flag `0x2000` and the queue cleared, sets the
no-reaction flag `0x800`, says line 11 or 14 and plays 653 (the shove). Confirmed (code). So only the boss being
attacked does any of this.

### Block (and no dodge) {#block}

**R1 held** on a pad, in a fight stance, blocks (an AI human cannot block, [AI](../ai.md#block)): anim state 21 then 606
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
  his rise by the same path ([Down and under attack](../combat-reactions.md#ground-escape)). The handler (`0x00245920`,
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
  [AI](../ai.md#reaction-goals).)
- Otherwise the brain's active entry (`brain + 0x40` indexed by `brain + 0x2c`) answers:
    - Type `0x1b` (`0x002b5698`), the block goal built by `0x002b54d8` and pushed by `0x0029f098` with a random
      block chance (a quarter of it for brain type 3). It says yes when the human is ducking (record `+0x08` has
      `0x1000`, `0x00223b28`) and the goal's bytes `+0x14` and `+0x16` are both 1. `+0x16` is 1 from construction;
      `+0x14` is written by the goal's Start (`0x002b5520`): 1 when a second roll under the block chance succeeds
      ([AI](../ai.md#block)).
    - Type `0x85`, the big fighter's goal (`BigFighterGoal_WantsCounter`, `0x002eab50`; vtable `0x00542940`, built by
      `0x002e9cd0`). It says yes when
      the human's body scale (`+0x65c`) is at most 1.1, it is ducking, and the goal's u16 `+0x28` is `0x0101`.
- Yes writes command `0x10` into the human's per-player record (`0x00147ef0`). From there the counter takes the
  player's path.

The events on the disc (the clips' event lists, frame in brackets) match the runtime ducks exactly (confirmed
(runtime), [Being hit](../combat-reactions.md#being-hit-runtime)): Rembrandt (`warr_re_cv`) has `0x24` on 11 `X1` (3), 13
`XX2` (5) and 14 `XS2` (4), and `0x26` on 12, 15, 16, 17, 19, 20, 653 and 655; the civilian (`civl_co_ma1`) has `0x24`
on 17 `SSX3` (3) and 653 / 655 (14), and `0x26` on 11-16, 19 and 24. So which attacks a block ducks is a property of the
attacker's model, not of the attack.

## In Coney {#coneys-implementation}

The rules are in `repo:src/combat/` (commands, attacks, the Anim Range List, reactions, meters, grabs, the stick games
and the dispatcher, `PlayerCombat`), played through the human in `repo:src/human/` (`Fighter`, the strike shapes, the
victim, the pair's placement). How they fit together, the tests that hold them, and every place Coney chooses where
this page is silent or differs on purpose are on [Engine: combat](../../engine/combat.md); each such choice links the
open question below or the issue that tracks it. The ported functions and their gaps are in the function list's port column.

## Open questions

- **The rise's play rate**{#q-rise-rate}: the counter out of the rise 199 is its own `0x25` events, frames 11-18
  ([Down and under attack](../combat-reactions.md#ground-escape)); 199's exact play rate is about 0.75-0.78 from the
  measured edges. A frame count of 199 in PCSX2 against its clip length settles it.
- **The shared Anim Range List**{#q-shared-anim-range}: the class damage is written into the list the character's
  humans share, so the newest human's class sets everyone's damage ([Being hit](../combat-reactions.md#being-hit-runtime)).
  Whether that is intended, and which humans share a list, is open; two classes of one model in one level would show
  it.
- **A blocked `SSS3`'s rage**{#q-blocked-sss3-rage}: 0 at runtime ([Rage](../combat-rage.md#rage-points)) where the two
  awards' formula gives 1 (3 >> 1, as `SSX3`'s blocked 1).
- **The stun after a knockdown**{#q-stun-after-knockdown}: it ends at the rise + 200 ms, before 199 ends, yet after
  653 the player stood in 356 for 7 updates before 357 ([Being hit](../combat-reactions.md#being-hit-runtime)).
- **The backward combat walk**{#q-backward-walk}: it reaches 3.429 m/s like every direction, after 5 slower updates
  ([Target selection](../combat-targets.md#targets)); what slows those 5 updates is open.
- **Class 13**{#q-class-13}: which character class it is (it gets hit armour and adds 2 s to a knockdown).
- **The rage events**{#q-rage-events}: the meaning of the events beyond the chain attacks' (`0x002653d8`,
  `0x00264fa0`).
- **The strike back's rage**{#q-strike-back-rage}: "The other attacks" gives 104 (`0x68`) event 2 × 10 (14 rage), but
  a strike back gave 1 at runtime. Which id the award reads for it is open.
- **Commands `0x30`-`0x39`**{#q-commands-30}: which scripts or weapons make them; `0x36`-`0x38` and the d-pad
  (`0x27`).
- **The grab code's tables**{#q-grab-table}: the `0x00510980` table, the further grab state of `0x005101f0`, and what
  makes square play 212 (`Player_Square` on a tackled target; never seen at runtime).
- **Square at a sprint**{#q-sprint-square} at runtime, and the moving attacks' hit timing (the victim was out of reach
  in the tests).
- **The mugging's angle frame**{#q-mug-angle}: world or camera.
- **The charge at a barrier**{#q-charge-stop}: the slide response stops it head-on
  ([How a moving attack strikes](#moving-strikes)); the clip's root motion in its last updates, and which leg each of
  the bone chains 28-30 and 31-33 is, are open.
- **The far ranges' class table**{#q-far-ranges} (`0x002545e0`, table `0x0055d640`): which of the class's 45 floats
  goes to which anim id, as the damage table's index → id map does for the damage.
- **The snap's steer and target**{#q-snap-steer}: whether `0x00264460`'s turn faces the target or puts it at the
  snap's side, whether the snap writes the target `+0xc8`, and which update's gait square's tests
  (`0x00223a30`-`0x00223a60`) read, given that a square one update after the stick still snapped at runtime.
- **The dive's warning**{#q-dive-warning}: the running dive is not among `Human_WarnOfAttack`'s call sites
  ([Attack warnings](#attack-warnings)), so whether it warns its target through another path is open. A watch on the
  target's brain `+0x200` during a dive at it would tell.
- **The hits not measured**{#q-unmeasured-hits}: the hit of the snaps, the throws and the grounded and mounted strikes
  (the phases are the clips' events, [Tasks](../tasks.md#held-flags)).
- **`XX2` against a walking target**{#q-xx2-walking}: in `combat_cross` the original's target walks up (0.83 → 0.91
  m) and the bodies push apart on `XX2` at 0.25 m/s, against Coney's 0.81; Coney's sandbox target stands still and has
  no body contact.
- **The grab's alignment fields**{#q-grab-fields}: the placement is known ([Grab pose at
  runtime](../combat-grabs.md#grab-pose-runtime)); the fields the alignment writes (human `+0x2e0`-`+0x332`, victim
  `+0xa0` / `+0xb0`) are open.
- **The bat**{#q-bat}: which code maps square and cross to 34 / 36 with a bat in hand, and whether a bat on the ground
  gets message 0 from the pick-up search ([A bat in hand](../combat-throws.md#bat)).
- **Rage extensions at runtime**{#q-rage-extensions}: 63 → 65 in a grab and 231 → 233 in the mount, which `level99`'s
  rage lesson waits for (inferred from `Player_UpdatePowerMove`); L1 + R1 did not start rage in a slot 6 copy with the
  meter written full, so what else the start needs is open.
- **Whether the flash clip sets `0x2000`**{#q-flash-held-flag} (record `+0x08`) as it starts: the task holds it
  ([Rage](../combat-rage.md#flash)); the type-5 task's start flags at `0x0025ade8` would tell, and a runtime read of
  `+0x08` on the update after d-pad right in the stance.
- **The flash during a move**{#q-flash-mid-move} ([The flash during a move](../combat-rage.md#flash-mid-move)): why
  presses on landing from the slot 9 window jump gave no command, and the window between 665's start and its event when
  a hit lands (a hit in `flash_hit`'s first run took health while 665 played on). What sets 665's rate is answered
  there ([The flash](../combat-rage.md#flash): 0.75, inferred from the counts).
- **The clips' `+0x44` flags**{#q-tackle-flag} (paired `0x1`, tackle `0x20`, grab `0x40`): which clips carry the
  tackle flag ([Strong grapple](../combat-grabs.md#strong-grapple)).
- **226's knockdown word at runtime**{#q-mount-power-knockdown}: the disc's 226 has its type-7 word 0, which would
  queue 198, yet at runtime `Attack_StartPaired` took the branch without 198 ([The mount power
  strike](../combat-grabs.md#mount-power)). To find out, rerun `moves_mount_power` with a hook at `Anim_GetKnockdownValue`
  (`0x00101a60`) logging its argument and return on update 143, and read the clip it was handed in memory: a different
  set (the generic `gen_mount_power20_*` rather than Rembrandt's) or another event than the first type-7 one would
  explain it.
- **Who sets state `0x80000000`**{#q-state-80000000}: the lethal hit during a paired move leaves it on the victim's
  record `+0x14` ([Moves a hit cannot stop](../combat-reactions.md#protected-moves)). One setter is known:
  `Human_PlayDeathReaction` (`0x00267b70`) ends with `Human_SetStateFlag(h, 0x80000000)`, confirmed (code); that it is
  the one at work on update 56 is inferred. To find out, rerun `moves_finisher_lethal` with
  the `state-code` hook (record `+0x14`'s setter) logging `ra` on update 56.
- **The root turn and the task's rate**{#q-root-turn-rate}: whether `0x00104ce0` or `0x0023f238` scales a clip's
  root turn by the task's rate, as the velocity is. The throw clips play at 0.75, and turning them by bone 0's turn
  per 1/30 s unscaled overshoots the runtime turns by about a third (149 by −120° where the runtime turn was −90°; 151
  −237° for 180°, 153 +119° for +86.5°), while scaling the turn by the rate gives −0.2° for 149 (runtime −0.2°), +2.1°
  for 151 (+1.2°) and −0.9° for 153 (−3.5°) ([Where a throw goes](../combat-throws.md#throw-direction),
  [Root motion](../formats/animation.md#root-motion)). It would change every clip with a root turn, not only the throws.
- **The AI's spray**{#q-ai-spray}: which AIs pick attack kind 15 (command `0x24`, [the spray](#run-attacks)) in a fight
  and how often. `Human_CanUseAttackKind` (`0x002240e8`) lets any free human with paint pick it, and without paint
  only a human no player controls (`+0x1b0` -1) ([Picking the attack kind](../ai.md#pick-attack)); how often a gang
  fight in the original shows it has not been read. Coney's dispatcher plays the spray for an AI, but its AI leaves
  kind 15 out of its picks until that is read, and no AI keeps the paint count (`+0x374`) the spray takes from.

## How this was checked

**The street runs.** The runtime claims were read with PCSX2 2.9.94 over PINE in a street level ("the street"), from
save states loaded read-only and copied with pad input patched in ([Driving
PCSX2](../../guides/research-tools.md#driving-pcsx2)). The player there is Rembrandt-class "player" human 2 with 314 of
900 health; the test victim is a civilian (`PoizoCiv`, 600 health) moved in front of the player by writing the transform
table. Damage and timings were read every update (30 per second) while the scripted pad played. The being-hit, block,
grabbed and pose runs ([Being hit, at runtime](../combat-reactions.md#being-hit-runtime), [Grab pose at
runtime](../combat-grabs.md#grab-pose-runtime)) drove that civilian as a **puppet**: command ids written into its
per-player record made it attack, grab and block the player; the player's health was reset to 900 before each run.
