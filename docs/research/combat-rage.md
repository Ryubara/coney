# Combat: the power meter, rage and reviving

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 as on [Combat](combat.md), which this page was split from because that page had grown too
large. The overview, the data tables and the code index stay on [Combat](combat.md#code-index).
Every claim carries the level written beside it.

## Purpose

This page covers the power meter, the rage meter and everything rage does, dropping a held weapon when hit, and reviving
a downed Warrior. A player sees the power meter empty as he holds a man and fill again, the rage meter fill from his
hits until L1 + R1 sets him off in the red tint, a weapon fall from a hurt fighter's hand, and a flash bring a downed
crewman back to his feet. Grabs and throws ([Grabs](combat-grabs.md)) spend the power; the HUD ([HUD](hud.md)) shows
both meters; the scripts' rage handlers and the level99 lessons react to them.

## Original structure

No source file names this code: it lies in `Human/`, in the player's combat code after `cns/cnsplayertag.cpp`
([Source map](source-map.md#position), inferred), and the names are ours. Every function is in the [code
index](combat.md#code-index); these are the entry points, each read in Ghidra at its address.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x002562d0` | `Human_UpdateMeters` | the power drain and refill, the rage decay and drain, rage's end at 0 | confirmed (code) |
| `0x00264cf8` | `Human_AddRage` | a rage award ([Rage](#rage)) | confirmed (code) |
| `0x002653d8` / `0x00265dd0` / `0x00418428` | `Hit_Score` / `Rage_NoteHit` / `RepeatTracker_Note` | a hit's rage awards and the repeat tracker | confirmed (code) |
| `0x002843f8` | `Player_UseItemCommand` | L1 + R1, the flash, the key when cuffed, the R2 command menu | confirmed (code) |
| `0x00284340` / `0x00236d28` / `0x00236fb8` | `Rage_Enter` / `Human_StartRageMode` / `Human_EndRageMode` | rage's start and end ([Everything rage does](#rage-effects)) | confirmed (code) |
| `0x00284280` | `Flash_Use` | spends a flash and heals | confirmed (code) |
| `0x00257e38` / `0x00257d70` / `0x00257ce8` | `Human_EndThrowAimIfHurt` / `Human_EndThrowAimDropHeavy` / `Human_EndThrowAim` | the three drop helpers ([Dropping what is held](#keep-weapon)) | confirmed (code) |
| `0x00286ba8` / `0x002752a0` / `0x00274ee0` | `Player_TryRevive` / `Revive_Start` / `Revive_Finish` | the revive ([Reviving](#revive)) | confirmed (code) |

## Data

The meters live in fields [Combat](combat.md#data) owns ([Other record fields](combat.md#record-fields)); the power
and Warrior class records are on [Characters](characters.md#power-classes), the `CfgBurnRates`, `CfgRagePoints` and
`CfgRageHandlers` values on [Constants](combat.md#constants). The fields this page reads most, each read in the
function named:

| Field | Meaning | Evidence |
| --- | --- | --- |
| record `+0x148` | the power meter (`Human_UpdateMeters`, `Human_SpendPower`) | confirmed (code) |
| human `+0x650` | the rage meter, up to the Warrior class's s16 `+0x00` (`Human_AddRage`, `Human_GetRageMax`) | confirmed (code) |
| human `+0x648` / `+0x64c` | the rage hold timer and the time rage started | confirmed (code) |
| human `+0xe0` `0x80000` / `0x2000000` | raging; may gain rage | confirmed (code) |
| `*(0x0051489c) + 0x178 + player × 0x5c` | the 12-byte repeat tracker ([Rage](#rage)) | confirmed (code) |

## Behaviour

The sections below go from the power meter to rage, the drops a hit causes and the revive. Each claim carries its
level; the code claims were read at the addresses they cite, the runtime ones measured in PCSX2 2.9.94 as each one
says.

## Power meter {#power-meter}

Record `+0x148`, maximum the power class's `+0x28` (400) through `0x00223068` (with upgrades). In
`Human_UpdateMeters` (`0x002562d0`) it **drains** by `CfgBurnRates`: 15 per second grabbing, 15 tackling, 2 in a
further grab state, 0 blocking; and **refills 60 per second** (`+0x2a`) otherwise. At 0 a grab ends
([Grabbed](combat-grabs.md#grabbed)). `Human_PowerFraction` (`0x00226510`) and `Human_SpendPower` (`0x00226448`);
neither spends nor drains while raging or with flag `0x4000000` (the fraction then reads 1.0). Confirmed (code),
confirmed (runtime) (400 → 377 over 1.4 s of a tackle; 25 → 0 at 15 per second in a grab).

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

## Rage {#rage}

Rage is human `+0x650`, up to the Warrior class's s16 `+0x00` (78 for class 6). `Human_AddRage` (`0x00264cf8`),
confirmed (code), adds, only for a player-flagged human (`0x2000000`) not already raging:

`trunc(points × f × gain / 100 × h × s)` (the float → integer at `0x0042c718` truncates), where `f` is 1.0 when the
award is 25 points or fewer and 0.1 when it is more (the whole award, not the excess: `CfgRagePoints` `0x005108d8`,
table `0x005108e0`), `gain` the Warrior class byte `+0x02` (144), `h` 0.5 when the player's **repeat flag** is set,
and `s` the player's **throw bonus** while its state word (record `+0x00`) has `0x1000`, throwing (`0x002239e0`),
else 1.0. The meter is **capped** at the maximum; the first time it fills, `0x00236ec8` announces it. Each gain sets
the hold timer `+0x648` = now + 5000 ms (`CfgRageHandlers`). Confirmed (code). The gains per hit at runtime are in
[Being hit](combat-reactions.md#being-hit-runtime): 0 to 15 per hit, 0 for most blocked hits, none for being hit
(confirmed (runtime)).

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

`Human_ApplyPendingDamage` (`0x00265f70`) notes a hit through `Rage_NoteHit` (`0x00265dd0`, confirmed (code)) after it
has given the hit's rage, when the
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
`0x40`) or a power move (`0x1000`). Confirmed (code) at `0x00265dd0`, `0x00418428`, `0x00264cf8` and `0x00255018`.

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
- The player's grabs cannot be struggled out of or escaped (`Grab_CanStruggle` `0x002258f0` and `Grab_CanEscape`
  `0x00225830` test the grabber's rage; confirmed (code)).
- Every hit causes a reaction (hit armour does not apply), the power strike is 63, the specials 645 / 649, the
  rumble one step stronger, and no rage is gained.

**The rage callbacks** (`CfgRageHandlers`, confirmed (code)): `Human_AddRage` calls the **full** function
(`0x00236ec8`) with `(human, true)` the first time the meter reaches its maximum (a per-player latch at its
`0x00222b18` record `+0x58`); starting rage (`0x00236d28`) calls the **enter** function with `(human, flag)` and
ending it (`0x00236fb8`) the **exit** function the same way, the flag being whether the count at
`*(0x0051489c) + 0x268` is below 1. `HuSetLockedRage` (bit `0x100000`) does not stop `Human_AddRage`, so a locked
meter still fills from hits (inferred: the lock holds only the decay).

**`Player_UseItemCommand`** (`0x002843f8`, run from `Human_UpdateActions` at `0x00255074`) handles L1 + R1, the flash,
the key and the R2 command menu, in this order. Confirmed (code) at `0x002843f8`:

1. Nothing while the state word (record `+0x00`) has any of `0x80040000`.
2. **Cuffed** (state `0x20000`): only triangle (command `0xa`) does anything. For a player human (per-player `+0x1b`
   set) with record `+0x08` free of `0x7c7eae0`, upgrade (6, 15) unlocked and a key (item 6) carried: interface
   sound 24 (`vags/misc/usekey_01`), one key spent, and `Uncuff_WithKey` (`0x00260fd0`) frees him
   ([Crimes](crimes.md)). Confirmed (code).
3. R2 held (command 1) opens the crew's command menu (`WarCommandDisplay_Open`, `0x001a6d28`, on HUD
   `0x00600848[player] + 0x1ef0`, the player index at human `+0x380`), and R2 released (2) issues the command chosen
   in it, if any (`WarCommandDisplay_Issue`, `0x001a6c58`); neither while the command menu is locked
   (`*(0x0051489c) + 0x42e + player`, the player's `+0x1b0`). Confirmed (code). (This is the [Warrior
   commands](ai-gangs.md#warrior-commands) menu, not a map.)
4. **L1 + R1** (`0x1f`) with the state word free of `0x19f9e0f3000` and `+0x08` free of `0xfc7eae0` (confirmed
   (code)): when rage (human `+0x650`) has reached the class maximum (`Human_GetRageMax`, `0x00223260`: the first
   `u16` of the Warrior class record,
   `0x00222b58`) and the human may gain rage (human `+0xe0` bit `0x2000000`, which `Human_MakePlayer` sets and
   `Human_AddRage` also tests) and is not raging (`+0xe0` bit `0x80000`), `Rage_Enter` (`0x00284340`) runs.
5. **The flash**, command `0x28` (d-pad right, [Commands](combat.md#commands)), needs all of: a player human (`+0x1b0` ≠
   −1), at least one flash (item 1) in the player's inventory (`0x0041e420` on `*(0x0051489c) + 0x480`, the player index
   at human `+0x380`), not down or dead (`Human_IsDownOrDead`: state `0x180050000`, which is wounded `0x10000`,
   knocked out `0x40000`, the pending-hit mark `0x80000000` and dead `0x100000000`; knocked **down**, `0x80000`, is
   not in it), the state word free of `0x1c00000000` (`0x00227f90`: jumping, falling, a long fall) and `+0x08` free
   of `0x40` (`0x00223c10`: climbing over). Any of these and the press does nothing: no clip, no flash spent. Then by
   health (record `+0x144` against the maximum `+0x146`):
   - **Below the maximum.** The flash clip **665 `SPECIAL_FLASH`** (`gen_flash_use`, 40 frames) is pushed as a
     one-shot clip task on top of the stack (`0x0025ade8`: a type-5 task, `0x00106240`, holding `0x2000`) when
     **nothing blocks a move**: the state word has none of `0x18003ff0` (`0x00227fd8`: grabbed, grabbing, mugging,
     mugged, tackling, tackled, a paired power move and its victim, `0x8000000`, `0x10000000`; blocking, `0x8000`, is
     in none of these masks), none of `0xe0000` (`0x00223b48`: cuffed `0x20000`, knocked out `0x40000`, knocked down
     `0x80000`), not the mini-game `0x4000000` (`0x00228050`), not tagging `0x2000000` (`0x002238c0`), none of
     `0x7bf9e9f7ff0`; record `+0x08` has
     none of `0x2fefefff`; and the object in hand is not of `ObjectAttribs` kind (`+0x87`) 4 or 6 (`0x00224000`).
     Otherwise, if 665 is already the human's anim (`0x002266b8`), nothing more happens (its event will use the
     flash); else the flash is **used at once** (`Flash_Use`, called at `0x00284818`): no clip, the move plays on.
     The code before this test that would let go of a grab (`Human_BreakPair`, `0x00258a88`, when the state word has
     `0xc0` or `0x400`) also requires the state word to be free of `0x7bf9e9f7ff0`, which holds both bits, so it never
     runs: in a grab the flash is used at once and the grab stays. Confirmed (code) at `0x002843f8`; confirmed
     (runtime), [The flash during a move](#flash-mid-move).
   - **At the maximum**, with upgrade (6, 8) unlocked (`0x00424130(0x006fe998, 6, 8)`: the first unlockable record of
     type 6 whose data is 8, unlocked when its bit in `0x006fe8f8` is clear, [Unlockables](player-state.md#unlockables)),
     the state word free of `0x19f9e0f3000`, `+0x08` free of `0xfc1fe7b`, not raging, the rage flag `0x2000000` set and
     a flash carried: one flash is spent (`Inventory_AddItem(…, 1, −1)`), rage is set to the class maximum and
     `Rage_Enter` runs. Without the upgrade nothing happens.

**The flash clip's events**: `gen_flash_use` (665, 1.333 s) has two events, both at **frame 19** (0.633 s): type
`0x41`, which `Anim_FireEvents` (`0x00101dd8`) sends as message `0xc1`, and type 11, an [animation
sound](sound.md#anim-sounds) (message `0x8b`) with id **70** at `+0x8` and the text `flash` at `+0xc`.
`Human_HandleMessage` (`0x002473bc`) answers `0xc1` by the human's anim: 665 calls `Flash_Use`; 666 opens the door
the human targets (`Door_OpenBy`); 668 played by a dealer (brain type 5 with `GoalDealer`, `0x80`, on top) finishes
the deal (`DealerGoal_FinishPair` `0x002c7c20`, [AI](ai-scripting.md#dealer-buy)). Confirmed (code); the events read
from the disc's clips (identical in both copies). So the flash is used partway into the clip, unless the clip is cut first
(then nothing is spent), and the player hears the animation sound 70 and `Flash_Use`'s interface sound 23 on the same
update. At runtime 665 plays slower than its frames say: started on update N, `Flash_Use` ran from the event on update
N + 25 and 665 lasted **53 updates** (N to N + 52), standing still and over a block alike, where 40 frames at 30 per
second would give 19 and 40 (both ratios about 0.75). Confirmed (runtime), `flash_idle` and `flash_block` below; what
sets that rate is not traced (`Human_PushOverlayClip`, `0x0025ade8`, passes no rate).

**Taking flash from the dealer** plays the money pair, not 665: the dealer's anim 668 bound to `money_take` and the
buyer's 668 to `money_give` (both 1.333 s, each with a type-8 partner-place event at frame 0, about 0.97 m ahead;
`money_take` carries the `0x41` event at **frame 17**), so the flash reaches the inventory 17 updates into the pair,
with the pickup sound `vags/interface/powerup` and the dealer's `cash` line ([AI, Buying](ai-scripting.md#dealer-buy)).
Confirmed (code); the frames from the disc.

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

### The flash during a move {#flash-mid-move}

What d-pad right does while another move plays (issue #234). The press is **never queued**: `Commands_Match`
(`0x00147940`) clears the per-player command `+0x20` at every update, and `Human_UpdateActions` (`0x00254e78`) hands
command `0x28` to `Player_UseItemCommand` (the call at `0x00255074`; confirmed (code)) on that update only, whenever
`PadHandlerEx_DispatchCommand`
(`0x001480e0`) returns 0 (the per-player `+0x1e`, set while a script holds the player, is clear); no move state is
tested before that call. So each press has one of three outcomes, decided at once by step 5 above:

| Outcome | What the player sees and hears | When |
| --- | --- | --- |
| **665** | the flash clip, then the heal, interface cue 23 and animation sound 70 together 25 updates in | nothing blocks a move |
| **at once** | the heal on the same update, with interface cue 23 (`Flash_Use` always plays it, `0x002842bc`); **no clip and no sound 70** (that sound is an event of 665); the move plays on | a move blocks 665, and the player is not airborne, climbing over, down or dead |
| **ignored** | nothing; the flash is kept | airborne (`0x1c00000000`), climbing over (held `0x40`), wounded, knocked out or dead (`0x180050000`, and step 1's `0x80040000`) |

Per move, confirmed (code) by the masks at `0x002843f8` and confirmed (runtime) where a scenario is named (PCSX2
2.9.94, `pcsx2 record` with the `scripted-pad`, `right-stick` and new `flash-use` hook on `Flash_Use`'s entry, whose
`ra` tells the two callers apart: `0x00284820` at once, `0x002473c4` from 665's event; health held at 600 of 900 and
3 flashes every update so each press is a fresh test; d-pad right is a digital button, the stick at 100 % is raw byte
255):

| Move | State when pressed | Outcome | Evidence |
| --- | --- | --- | --- |
| Standing, walking | no move | 665 | runtime, `flash_idle` (slot 1) |
| **Sprinting** (L2 held, stick 100 % up) | state `0x1000000`, held 0 | 665; it **stops the sprint dead** (speed 10.25 m/s → 0 and gait 0 on the next update, with L2 and the stick still held) and the run starts again after 665 | runtime, `flash_sprint` (slot 1) |
| **Attacking**, a cross chain X1, XX2, XXX3 | state `0x5`, held `0x1` wind-up, `0x2` window, `0x4` end, `0x40000` recovery | at once, every press (11 of 11, through 11 and 13); 665 only from the stance after the chain (358) | runtime for `0x1` and `0x40000`, `flash_attack` (slot 6, a puppet 1.5 m ahead); code for `0x2` and `0x4` |
| **Holding someone** (front grab: the connect 72, the hold 82) | state `0x45`, held `0x200` then 0 | at once, every press (21 of 21); **the grab stays** (82 and `0x45` on) | runtime, `flash_grab` (slot 6, a puppet 1.0 m ahead) |
| **Blocking** (R1 held), its first 5 updates | state `0x8001`, held `0x80000` | at once | runtime, `flash_block` (slot 6) |
| **Blocking**, held after that | state `0x8001`, held 0 | **665 plays over the held block** (the state stays `0x8001`), the flash used by its event | runtime, `flash_block` |
| **Hit**: a standing reaction (272) | state `0x5`, held `0x400` | at once; the reaction plays on | runtime, `flash_hit` (slot 6, PoizoCiv as a type-2 fighter) |
| Held by an enemy's front grab (73) | state `0x15`, held `0x10000` | at once; still held | runtime, `flash_hit` |
| **Knocked down**: thrown (154), then stunned on the ground | state `0x180005` (stun `0x100000`, down `0x80000`), held `0x400000` | at once; the fall goes on (196) | runtime, `flash_hit` |
| Lying down, getting up (196, 199) | state `0x80000` | at once (`0xe0000` blocks 665, `0x180050000` does not stop the press) | code |
| Mounted by a tackler | state `0x800` | at once | code |
| **Jumping**, in the air (434) | state `0xc00000000` | ignored (10 of 10 presses), the flash kept | runtime, `flash_jump` (slot 9, a run at 100 % and triangle) |
| Landing (435) | state `0x2000000000`, held `0x1000000` | at once | code (the presses on landing in `flash_jump` gave no command: open) |
| **Climbing** a fence: its first clip (440) and last (442) | held `0x80000` | at once | runtime, `flash_climb` (slot 7) |
| Climbing over (441) | held `0x40` | ignored (5 of 5) | runtime, `flash_climb` |
| Knocked out, wounded, dead | `0x40000`, `0x10000`, `0x100000000` | ignored (a partner revives, [Reviving](#revive)) | code |
| 665 already playing | held `0x2000` | nothing more (one flash, at its event) | runtime, every scenario |

So the "heals at once, no animation, no sound" a player sees is the original's own behaviour **mid-move** (attacking,
in a grab, reacting to a hit, down, the block's start, a climb's first and last clips), except that the original
still plays interface cue 23 (`vags/misc/flash`) then; only the animation sound 70 is missing. Standing, walking,
sprinting and a held block play 665. Confirmed (code) at `0x002843f8` and `0x00284280`; runtime as listed.

The level99 states (slots 1, 6, 7, 9) have the flash command turned off for pad 0: the pressed table's d-pad right
entry (`0x005ddde8`) has mask `0xfe` where every other entry has `0xff`, so `Commands_Match` stores `0x28` in the
pending `+0x24`, not in `+0x20`, and d-pad right does nothing there (confirmed (runtime); inferred that the level's
script turns it off with `EnableCommand`, [bindings](../references/bindings/character.md#enablecommand)). The
scenarios turn it on with a setup write.

**`Rage_Enter`** (`0x00284340`), confirmed (code): plays the 2D sound `vags/misc/rage_mode_06` through the game
state's handle at `+0x264` (restarted when it is still playing, `0x004194b8`); when the state word has any of
`0x18003ff0` and `+0x08` none of `0x2fefefff`, the pair is broken (`Human_BreakPair`); then, if nothing of
`0x18003ff0` or `0xe0000` is left and `+0x08` is free of `0xfc1fe7b`, rage starts (`0x00236d28`) and 643
`RAGE_START` plays as a full-body clip holding `0x2000` (`0x0025a8c0`); otherwise rage starts without the clip.

`level99`'s last lesson waits for this command through `PadSetHandlerEx` (the handler receives it even when
`EnableCommand` has turned it off, [Commands](combat.md#commands)).

### Everything rage does {#rage-effects}

One list of what changes from the moment rage starts to after it ends, for the implementer. Confirmed (code) at the
cited functions unless marked; the runtime checks used PCSX2 2.9.94 on a copy of quick-save slot 6 (`level99`,
Rembrandt), with the scripted-pad patch, rage started by writing the meter full (78) and the hold timer, then L1 + R1
held for 0.3 s (2026-10-10).

**Start** (`Rage_Enter` `0x00284340` → `Human_StartRageMode` `0x00236d28`), in this order:

1. **Sound**: `vags/misc/rage_mode_06` starts, 2D (`GameState_PlayRageSound` `0x004194b8`). It is usually already
   prepared: `Human_UpdateMeters` (`0x002562d0`) prepares it through `RageSound_Prepare` (`0x00419460`) while the
   meter is above 90 % of its maximum and no one rages, and stops it again below that (runtime: the handle at game
   state `+0x264` appeared as soon as the meter, written full, fell under the hold timer's decay at 75 of 78).
2. A pair the player is in is broken first (`Human_BreakPair`), then the clip 643 `RAGE_START` plays full-body,
   holding `0x2000` (`0x0025a8c0`), unless the player is still paired, down (state `0xe0000`) or holds a busy flag;
   rage starts either way.
3. **The script's enter handler** (`CfgRageHandlers`' first name, `0x006b6830`) with `(human, flag)`, the flag true
   when no one else rages (game state `+0x268` below 1). `global.lua` registers `SetRageMode`, which turns on, by
   binding: `HuSetGodMode` (`0x10`), `HuSetUnstunnable` (`0x100`), `HuSetReducedReact` (`0x200`),
   `HuSetIncreasedReact` (`0x200000`), `HuSetUngrabbable` (`0x40`), `HuSetUngroundable` (`0x80`) and `HuSetKeepWeapon`
   (`0x2000`) on the human (and unlocks the meter when the rage cheat is on). The `level99` combat lesson swaps in its
   own handlers that set the same seven flags and also queue a motion blur of 150 over 1 s (read from the disc's
   scripts). Runtime: the player's `+0xe0` had `0x10` from the update rage started to the update it ended.
4. Human flag `0x80000` set, `+0x648` (the hold timer) = 0, `+0x64c` = now.
5. **The screen look**: `ScreenFx_StartLook(manager of the player's view, 2)` (`0x0018b7d0`): the rage tint and the
   wobbling ghost of the last frame ([Graphics: the rage look](graphics.md#rage-look)).
6. **The camera** shakes at level 1 (its vtable `+0x15c`, [Camera](camera.md)), with the pad's rumble.
7. `RageSound_AddRager` (`0x00419718`): the count of raging humans at game state `+0x268` goes up by one.

**While raging**:

- **Meter**: drains at the Warrior class byte `+0x04` (9.36 a second for class 6); no gain, no decay hold. Runtime: 76
  → 0 in about 8.1 s (look 2 from game time 581.34 s, ended by 589.41 s).
- **What the player deals**: the same damage (no bonus); every hit causes a reaction because the attacker is raging
  (hit armour does not apply, `Human_ApplyPendingDamage` `0x00265f70`) and, through `0x200000`, one step stronger
  ([Human flags](combat.md#human-flags)); the power strike is 63 (and 231 when mounted, which also deals its damage at once,
  `Player_UpdateGrabbing` at `0x0027f524`), the special 645 / 647, the strong grapple's strike 649 / 651; no power
  spent or drained; the player's grabs cannot be struggled out of, escaped (`0x002258f0`, `0x00225830`), countered
  (`Human_CanCounterGrab` `0x00258f40` refuses a raging grabber) or counter-tackled (`Human_CanCounterTackle`, the
  same test at `0x002591e8`).
- **What the player takes**, through the enter handler's flags: no damage (god mode: `Human_ApplyPendingDamage`
  zeroes the pending damage while `+0xe0` has `0x10`, except a hit by a world object of `ObjectAttribs` kind `0x1d` or
  `0x22`), never stunned (`0x100`), reaction one step weaker (`0x200`), reaction strength at most 1, so never knocked
  down (`0x80`), cannot be grabbed (`0x40`), keeps his weapon (`0x2000`; what that covers and what it does not:
  [Dropping what is held when hit](#keep-weapon)). Without the handler (none registered),
  only the code's own rules hold:
    - a hit on a raging player who is grabbing (record `+0x08` `0x10`) plays no reaction, the damage still lands
      (`0x00265f70`, the test before the hit-armour one);
    - a raging human cannot be set on fire (`Human_Ignite` `0x0022fce8` returns at once);
    - a gunman's shot at a raging human deals a tenth of its damage, rounded (`trunc(d × 0.1 + 0.5)`,
      `ShooterGoal_Fire` `0x002f1d60`).
- **Movement**: no change. Runtime: with the left stick at 0.8 to the side for 2.5 s the speed `+0x1ac` settled at
  1.629 m/s and the clip at 408 both raging and not (mean 0.907 and 0.912 m/s over the last 1.4 s). No locomotion
  function tests `0x80000` (a search of every `lui …, 0x8` beside a `+0xe0` load found only the readers listed here).
- **Sound**: the rage sound runs on; `RageSound_RestartIfRaging` (`0x00419630`, every frame from `0x0041a370`)
  starts it again if it has ended while a player still rages. No other sound and **no music change**: none of the
  rage functions calls the music system (inferred from the code read, not checked by ear).
- **HUD**: the name banner and the meter in the raging colour (217, 158, 12), the target ring's inner arc showing
  rage ([HUD](hud.md#the-rage-meter)).

**End** (`Human_EndRageMode(human, instant)` `0x00236fb8`), when the meter reaches 0 (`Human_UpdateMeters`), on
`Human_ReleaseAll` and at a scene's start (`SceneTask_Start`), all gradual (`instant` 0); and **at once** (`instant` 1)
on the pause menu's opening (`GameMenu_Open` `0x001d1aa0`), `Humans_EndAllRage`, `HuSetRageMode(human, false)` and
a fade-out queued with `ScreenQueueEffect(1)` (`0x0018d4c8`):

1. `RageSound_RemoveRager`: the count goes down; at 0 `RageSound_Stop` (`0x00419758`) fades the rage sound out over
   2,000 ms.
2. For a player: the one-shot `vags/misc/rage_ends_01` (`RageSound_PlayEnd` `0x00419400`), and
   `ScreenFx_EndLook(manager, 2)` (gradual) or `(manager, 3)` (instant) ([Graphics](graphics.md#rage-look)).
3. **The script's exit handler** with `(human, flag)`: `ClrRageMode` clears the seven flags (and, with the rage
   cheat, refills and locks the meter shortly after through `TurnOnRageCheat`, scheduled at 33). Runtime: `0x10`
   cleared on the update the meter reached 0.
4. `+0x648` = 0 and flag `0x80000` cleared. The meter stays at 0; it fills again only from new hits.

**The meter full, before rage** (`CfgRageHandlers`' third name, `RageFull` in `global.lua`): called with
`(human, true)` the first time the meter fills (`Human_AddRage`) and with `(human, false)` when the hold timer runs
out with the meter still unspent (`Human_UpdateMeters`, the latch at the per-player record `+0x58`); the
`global.lua` version queues the motion blur at **75** over 1 s for true and back to **15** over 1 s for false. A rage
started before the hold ran out clears the latch without the false call (the rage look's own blur then takes over).
The HUD's own full-meter cue (`rage_indicator_02` and the flashing) is on [HUD](hud.md#the-rage-meter).

**Defined but unused**: `global.lua` also defines `CameraRage` (the follow camera at 1 m with a field of view of
100) and `TurnOnRagelight` / `TurnOffRagelight` (both world lights to an orange (1, 0.75, 0)); no script chunk calls
any of the three (each name occurs once in the 467 chunks, at its definition). Not part of the original's rage.

**Coney status and the fix for the implementer** (Coney as of `781d7768`):

- Done: the meter, its gains, drain and decay (`repo:src/combat/meters.h`), L1 + R1 from the block, the moves 63,
  231, 645 / 647, 649 / 651, no power in rage, the rage handlers called with the original's flag
  (`ScriptedHumans::runRageHandlers`), the seven flags' bindings, the HUD's colours and full-meter sound, the camera
  shake.
- **Screen effect (missing; item 26)**: implement look 2 on the raging player's view exactly as
  [Graphics: the rage look](graphics.md#rage-look): the tint blend to (51, 25, 0, 80) over 1.0 s, the 9 × 9 ghost
  grid at a blur alpha of 150, then on the end the 2.5 s grid fade, the 5 s tint and blur blends back to the base
  look, or the instant snap for the instant ends above. `effects::ScreenTint` today models only looks 9 and 10.
- **Sounds (missing; item 26)**: `vags/misc/rage_mode_06` on start (2D, held in one game-wide handle, restarted if it
  ends while a player rages, faded out over 2 s when the last rager ends); `vags/misc/rage_ends_01` once at each
  player's end. Prepare-at-90 % is a loading detail; starting it on rage is enough.
- **`RageFull(human, false)`**: Coney calls the full handler only when the meter fills; add the false call when the
  hold timer runs out on an unspent full meter, or the 75 blur stays on.
- **Rage start outside a block**: the original starts it from any free state ([Commands](combat.md#commands)); Coney
  only from the block ([Coney's differences](combat-diffs.md)).
- **To check in Coney**: god mode (`kGod`) is honoured by `Fighter::takePending`; make sure the other six
  flags are read where the original reads them (unstunnable, reduced and increased reaction, ungrabbable,
  ungroundable, keep weapon), that a raging grabber cannot be countered (`fighter_victim.cpp` reads the victim's own
  rage there), and, when fire and gunmen arrive, the two code-only rules above. The keep-weapon flag's readers and
  the drops it does not stop are in [Dropping what is held when hit](#keep-weapon), with its own list for Coney.

### Dropping what is held when hit {#keep-weapon}

When a hit, a grab or a defeat makes a human let go of what he holds, and what human flag `0x2000`
(`HuSetKeepWeapon`, set by rage's enter handler) changes. Asked for issue #91. Confirmed (code) at the addresses
cited; not checked at runtime. "Set" is the held object's type byte `+0x87`, its move style
([Objects: the move style](objects.md#two-handed)): 1 knife, 2 baton, 3 bat, 4 two-handed prop, 5 throwable, 6
ghetto blaster. "Hurt" is `Human_IsHurt` (`0x00222ff8`): health below the power class's `+0x04` fraction of the
maximum (0.35 for a street civilian, [Characters: power classes](characters.md#power-classes)). Every drop is
`Human_DropHeld` (`0x00257f38`, [Objects: drop](objects.md#held)).

**Three helpers decide** (each first ends a throw aim, `Human_CancelThrowAim`, and drops a held set 4 or 6 object
whatever the flags; `Human_HoldsKind4Or6` `0x00224000` reads the set). Confirmed (code) at each helper's address:

| Helper | Then drops any other held object when | Called on |
| --- | --- | --- |
| `Human_EndThrowAimIfHurt` (`0x00257e38`) | the human is **hurt**, flag `0x2000` is clear, and he is not a cop (brain type 1) holding set 1 or 2 | the victim, by `Human_KnockDown` (`0x0022f100`, always, first thing) and `Human_Stun` (`0x0022f658`, see below) |
| `Human_EndThrowAimDropHeavy` (`0x00257d70`) | flag `0x2000` is clear, he is not a cop holding set 1 or 2, and he is **hurt or holds set 3** (a bat) | the victim, by `Grab_Link` (`0x0022c730`, a grab), `Pair_LinkMount` (`0x0022bfd0`, a mount), the paired-move link `Grab_BreakOnHit` (`0x0022b960`, from `Attack_StartPaired`, `Grab_StartPairedMove`, `Grab_Strike`, `Grab_Reverse` and the mount strikes, reversal and get-off)) and `Human_PlayLethalReaction` (`0x00267e48`) |
| `Human_EndThrowAim` (`0x00257ce8`) | flag `0x2000` is clear and the set is not 1 or 2 (no hurt test, no brain test) | the human **starting** the move: the grabber in `Grab_Start` (`0x0026c548`), `Grab_Link`, `Pair_LinkMount`, the tackler in `Tackle_Start` (`0x002707a8`), and in `0x0022b960` the attacker when he holds set 2 or 3 (not for clip 642, nor set 1 with clip 640) |

The test in `0x00257e38`, as read (snippet; confirmed (code)):

```c
if (Human_IsHurt(h) && !(h->flags & 0x2000) &&
    (Brain(h)->type != 1 || (set != 2 && set != 1)))
    Human_DropHeld(h);
```

**When a hit gets there.** A hit that lands and is not blocked runs `Human_ApplyPendingDamage` (`0x00265f70`,
[Damage](combat-reactions.md#damage)); a blocked hit returns before any of this. In order, confirmed (code):

1. **Every landed, non-lethal hit** (or any hit while airborne), before the reaction is chosen and **without any flag
   test**:
    - `Human_ReleaseCarriedProps` (`0x0024ce40`, unless cuffed; confirmed (code)) drops a held object of
      `ObjectAttribs` kind `0x27`
      (39, the one-sphere melee object) unless he is throwing, and lets go of the carried props `+0x348` / `+0x34c`;
    - `Human_DropHeld` when `Human_IsSwingingHeld` (`0x002286a0`, confirmed (code)) is true, that is held flag
      `0x800000`, or held
      flag `0x1000010` with something in hand during one of the eleven **throw** clips (467, 471, 472, 491, 502,
      505-507, 551-553), or when he plays 602 `ANIM_TAUNT_CHALLENGE` (the knife-toss fidget) with set 1 in hand
      (`0x002664e8`). So a hit during a throw wind-up makes the object fall from the hand, raging or not. Despite
      the function's name, no melee swing clip is in its list.
2. **The ordinary reaction** (`Human_PlayReaction` `0x0026a6d0`, reached when hit armour, the held flags `0x1c16a40`
   and flag `0x800` let a reaction play; confirmed (code)):
    - when the **attacker's** clip is **620 or above** (the test at `0x0026a798` is `id ≥ 0x269` and `id ≥ 0x26c`,
      confirmed (code);
      so in effect `≥ 620`: the left block counter 620, the bat-block counter 629, the rage moves 645 / 647 and
      649 / 651, the specials 653 / 655 and 657 / 659) and the victim's flag `0x2000` is clear, he drops what he
      holds, any set, hurt or not;
    - a reaction that knocks down calls `Human_KnockDown`, hence `0x00257e38` (confirmed (code));
    - a stunning reaction calls `Human_Stun(victim, attacker)` (confirmed (code)). `Human_Stun` returns at once for a
      human with flag
      `0x100` (unstunnable; without an attacker only when he also lacks flag `0x8000000`), and calls `0x00257e38` only
      when he has none of the state bits `0xcf0` (grabbed or grabbing) and neither held flag `0x10200`.
3. **The special reactions** (confirmed (code)): `HitReact_WhileGrabbed` (`0x002688d0`) stuns (so `0x00257e38` as
   above); `HitReact_InSpecial` (`0x00268430`) knocks down; a lethal hit runs `Human_PlayLethalReaction`
   (`0x00267e48`), which calls `Human_EndThrowAimDropHeavy` (`0x00257d70`).
4. **Defeat**, where no flag is read: `Human_StartWounded` (`0x0022fa78`) and the knock-out's `Human_ReleaseAll`
   (`0x00258968`) drop what he holds ([Knocked out](combat-reactions.md#defeat)). Confirmed (code).

**Arrest** (`Human_Arrest` `0x0022ec18`, at the end of the cuffing) drops a set 4 or 6 object always and anything
else only without flag `0x2000`. Confirmed (code).

**Summary by case** (no flag `0x2000`; a player is brain type 0, so the cop exception never applies to him):

| Case | Bat (set 3) | Knife, baton (1, 2) | Two-handed, ghetto (4, 6) | Bottle, brick (5) |
| --- | --- | --- | --- | --- |
| Hit, not hurt, stunned or knocked down | kept | kept | dropped | kept |
| Hit, hurt, stunned or knocked down | dropped | dropped (a cop keeps them) | dropped | dropped |
| Hit by a clip ≥ 620 (counter, rage, special) | dropped | dropped | dropped | dropped |
| Hit during a throw clip | dropped | dropped | dropped | dropped |
| Grabbed, mounted, in a paired move as the victim | dropped | kept unless hurt (a cop keeps them) | dropped | kept unless hurt |
| Starts a grab or a tackle | dropped | kept | dropped | dropped |
| Arrested | dropped | dropped | dropped | dropped |

The damage itself is never read: only whether the victim is hurt after it (the health drops before the reaction).
The weapon the attacker uses matters only through his clip id (≥ 620).

**With flag `0x2000`** every row above keeps the object, except: the set 4 / 6 column wherever one of the three
helpers or the arrest runs (they drop those first, flag or not; the clip ≥ 620 test itself spares them, but the
stun or knock-down that usually follows does not), the throw
row and a kind 39 object (step 1, no flag test), and defeat. The flag is read at exactly five places: the three
helpers (`0x00257d18`, `0x00257da4`, `0x00257e7c`), `Human_PlayReaction` (`0x0026a7bc`) and `Human_Arrest`
(`0x0022ed14`), confirmed (code); a search of every `andi …, 0x2000` in the executable found no other reader of the
human flags (inferred: complete).

**While raging**, the enter handler's other flags already close most of these paths: unstunnable (`0x100`) stops
`Human_Stun` before its drop, ungroundable (`0x80`) keeps reactions from knocking down, ungrabbable (`0x40`) stops
grabs and mounts, god mode (`0x10`) prevents defeat. What `0x2000` alone still decides for a raging player is the
drop by an attacker's clip ≥ 620 (an enemy's counter or special), his own grab or tackle start with a bat or a
set 5 object, and a paired move he starts holding a bat (`Grab_BreakOnHit`, `0x0022b960`). A hit during his own
throw still drops the object. Inferred from the code above.

**What Coney must do** (for the implementer of issue #91 and of [Objects: held](objects.md#held)):

1. Give a human one "keep weapon" flag, set and cleared by `HuSetKeepWeapon` and by the rage handlers.
2. On a landed, non-blocked, non-lethal hit (or any hit while airborne): drop the held object when the victim is in
   a throw clip with something in hand (the eleven ids above), holds held flag `0x800000`, plays 602 with set 1, or
   holds a kind 39 object; no flag test.
3. In the ordinary reaction: when the attacker's clip id is ≥ 620 and the victim lacks the flag, drop.
4. On a knock-down, and on a stun that is not refused (unstunnable, grab states `0xcf0`, held `0x10200`): drop set
   4 / 6; drop any other object when the victim is hurt, lacks the flag and is not a cop holding set 1 or 2.
5. When a human becomes the victim of a grab, mount, paired move or lethal reaction: drop set 4 / 6; drop any other
   when he lacks the flag, is not a cop holding set 1 or 2, and is hurt or holds set 3.
6. When a human starts a grab or tackle (and as attacker of a paired move, holding set 2 or 3): drop set 4 / 6, and
   any set but 1 or 2 without the flag.
7. On arrest: drop set 4 / 6, and anything else without the flag. On defeat: drop whatever is held.

**Tests.**

- Unit: a fighter with a bat at full health takes a stun hit and keeps it; at 20 % of a class with a 0.35 threshold
  he drops it; with the keep-weapon flag he keeps it; with a set 4 prop he drops it in all three; the same hurt
  victim as a cop with a baton keeps it; a hit by an attacker in clip 645 drops a bat at full health unless the flag
  is set; a hit during clip 502 drops the bat even with the flag.
- Disc (the player's path): `level99`, the rage lesson checkpoint, through `DiscSession`. Put a bat in the
  player's hand (`HuPlaceItemInHand`, as the scripts do), bring his health under the hurt threshold, start rage with
  L1 + R1 as a pad press, have a sparring Warrior land a stun hit and a special (clip ≥ 620): the bat stays in hand
  (`script().heldObject` unchanged) through both. End rage (meter drained) and repeat the stun hit: the bat falls.

## Reviving a downed Warrior {#revive}

The prompt (HUD string 4) and who can be revived are on [HUD: action prompts](hud-messages.md#action-prompts); this is the
action. **D-pad left** pressed makes command `0x27` ([the street's tables](combat.md#commands), confirmed (runtime)), which
`Player_TryRevive` (`0x00286ba8`) handles; an AI Warrior reaches the same function through
[SaveHuman](ai-goals.md#goal-save-human) (inferred from its brain-type test below). Confirmed (code) at the addresses
given:

1. **Refused** when the reviver has any held flag or a state flag of `0x7bf9e9f7ff0`, when
   `Human_FindRevivableNear` (`0x00279078`) finds nobody, or when the downed human has any held flag.
2. **The way** (confirmed (code)): when `Human_IsWayClearTo(reviver, downed)` (`0x0021c0a8`) fails, a downed AI
   human (`+0x1b0` = −1)
   is refused, and a downed player only accepts a reviver whose brain is type 3 (a Warrior). The test sweeps a
   **capsule of radius 0.2 m**, as tall as the reviver's own capsule (`Human_GetCapsuleHeight`) and posed on him, along
   the vector from his transform position to the downed human's (`IPhysics_CollideShape`, collision mask `0x2803`,
   his own body skipped); the way is clear when nothing is hit or the first thing hit is the downed human. It fails
   at once (not clear) while the reviver is grabbed, the downed human has no character instance (`+0xd8`) or the
   reviver has no physics body (`+0x1a0`).
3. **The flash**: `Inventory_GetCount(inventory, slot, 1)` for the reviver's player slot (`+0x380`), else for the
   downed human's; an AI human's slot is −1, which the inventory refuses (count 0). So an AI Warrior can only revive
   a downed **player who holds a flash**, and player 1 only with his own flash (or a co-op partner's). Else refused.
   Then `Revive_Start` and the command is used (returns 1).

`Revive_Start(reviver, downed)` (`0x002752a0`), confirmed (code):

- Both bodies get the same new body group (a counter at `*0x00597198 + 0x10`, wrapping to 1) at the body's `+0x3c`,
  as a paired move's do ([Grabs](combat-grabs.md#mount-power)); confirmed (code). Both their vtable slot `+0xe4` is
  called with (1e9, 7) (inferred: the push weight, as `Human_KnockOut` gives 1e9; reset with 1.0 at the finish).
- `Attack_SteerLed(0.1, reviver, downed, 0, 351)` puts the reviver in place for clip 351
  ([the led steer](combat-targets.md#led-steer); the 0.1 is not read): over 351's time to its first event + 0.1 s he
  **turns** to the line to the downed human's head (less 351's direction offset, `AttackTable_GetOffset`) and **slides**
  to the point that lies 351's reach (the Anim Range List's `+0x04` for anim 351, `AttackTable_GetReach`, `0x002544a0`)
  short of that head, both at an even rate ([runtime](#revive-runtime): about 0.70 m from him, 12° off the line).
  Confirmed (code): `Attack_SteerLed` with 351 is called at the start.
- The reviver says **76 `revive_reasure`** (`Human_SayCommand(1.0, reviver, 76, none, interrupt, …)`).
- His animation stack gets, bottom up: **351 `gen_revive_begin`** with the finish callback (`Revive_Finish`,
  `0x00274ee0`) on its end, chained to **353 `gen_revive_end`**, chained to his idle loop (out of the fight stance
  through 389 `gen_trans_combat_to_neutral`, idle set 0; in the stance, idle set `0xb`); a blend-out over 0.01 s on
  top. Clip 352 (`gen_revive_cycle`) is not used. Push sphere off; held flag `0x8000000`; each is the other's
  partner (`Human_SetPartner`).

`Revive_Finish(reviver)` (`0x00274ee0`), when 351 ends **however it ends**: the callback is the chained clip task's,
and the task's clean-up runs it on the clip's end, on a cut-off and on the task's release
(`AnimTaskChained_Release`, `0x00105f78`, and [the task types](animation.md#task-types): resolve the human, clear the
task's held flags, run the callback, clear it), so a hit or anything else that cuts 351 short still spends the flash
and heals, once. Confirmed (code); the normal end confirmed (runtime: the call came from the release, `0x00106040`);
a cut-short 351 was not staged at runtime.

1. Both partners cleared, both `+0xe4` calls with (1.0, 7), both pair ids 0.
2. **One flash is spent**: the downed human's when his inventory has one, else the reviver's
   (`Inventory_AddItem(…, 1, −1)`).
3. Interface cue **23** (the flash sound, as [`Flash_Use`](#rage)); the revived says **75 `revive_thank`**;
   `Human_Heal(revived, 1)` wakes him and fills his health ([`Flash_Use`](#rage) above).
4. The revived gets up with **354 `gen_revive`** as a pair task bound to the reviver (flag `0x8000`), chained to his
   idle loop (through 389 out of the stance), push sphere off, held flag `0x8000`.

So the whole revive is clip 351 on the reviver (the flash spent and the heal at its end), then 353 on him and 354 on
the revived together.

**Who can be revived.** Every Warrior: `Human_Init` gives brain type 3 the revivable flag `0x4` when he is made
([Human flags](combat.md#human-flags)), so a crew Warrior downed in any fight of any mission is revivable unless a script
clears it with `HuSetRevivable(h, false)`. Confirmed (code); confirmed (runtime) on `level80` below. `level80`'s own two
`HuSetRevivable` calls (checkpoint 3) are for humans of brain type 2; the crew needs none.

### At runtime {#revive-runtime}

Confirmed (runtime), PCSX2 2.9.94, `level80` checkpoint 1 after the "Let's Go" lesson (R2 and the right stick up 80 %
gave command 0, which hands control back and brings Vermin and Rembrandt down to Cleon); scenario
`repo:research/traces/scenarios/revive_crew.toml` with the hooks `revive-start`, `revive-finish`, `anim-dispatch` and
`say-command`. Vermin was put in front of Cleon at update 270, his health set to 0 and `Human_OnHealthOut` called on
him at 281 (he stays standing: the fall is a fight's reaction clip, not this function), Cleon given one flash, then
d-pad left tapped once (the stick at rest). Two runs, Vermin 0.97 m and 1.66 m away:

- **The prompt**: string 4 ("Revive" with the d-pad picture) showed while Cleon stood beside the knocked-out Vermin
  with the flash, before the press.
- **The press**: the update that read it had command `0x27`, `Revive_Start(Cleon, Vermin)` from `Player_TryRevive`
  (return address `0x00286ca0`), line 76 on Cleon, and Cleon's clip 351 with record `+0x08` = `0x8000000`. The clips
  taken that update, in order: 388 (idle), 389, 353, 351 (the chain built bottom up).
- **351 lasted 21 updates (0.70 s)** in both runs. On the 22nd update `Revive_Finish` ran: the flash count went 1 to 0,
  Vermin's health 0 to 1800 (his maximum), line 75 on Vermin, Cleon's clip 353 and Vermin's 354. **From the press to
  the heal: 0.70 s.**
- **353 lasted 33 updates (1.10 s)**, then **389 25 updates (0.83 s)**, then idle 388; `+0x08` went back to 0 when
  353 ended. Cleon was out of the fight stance, so 389 came before the idle as the chain says.
- **354 lasted 72 updates (2.40 s)** on Vermin, then 389, which his brain cut after one update with its own clips
  (turns 400 / 401, then idle).
- **The steer**: Cleon's heading changed for 25 updates at an even rate (1.52° and 1.06° an update) and ended **11-13°
  to the left of** the line to where Vermin stood (105.7° against 93.0°, 94.3° against 82.9°). He **slid**: from 0.97 m
  he ended 0.71 m from Vermin, from 1.66 m 0.71 m too (up to 1.75 m/s), so 351 has a reach and the stand point sits
  about **0.70 m** from the downed human. 353 then carried him back to 1.1-1.16 m and 389 to 1.15-1.29 m (their own
  root motion).
- **Knocked out with `0x4`**: the wake time `+0xf0` was now + 28 s ([Defeat](combat-reactions.md#defeat)).

**Coney** builds it for player 1 (`repo:src/gamemodes/gameplay_revive.cpp`, `repo:src/human/human_revive.cpp`, the
tests as `combat::mayRevive` in `repo:src/combat/revive.h`): d-pad left beside a revivable partner, the tests above,
76, the led steer, the chain 351, 353 and the idle (through 389 out of the stance) holding `0x8000000`, and at 351's
end, however it ends, the flash, cue 23, 75, the full heal and 354, 389 and the idle on the revived, holding `0x8000`.
Every Warrior and every player is made revivable: `ai::startingFlags` gives a human its brain type's flags of the
table in [Human flags](combat.md#human-flags) as its brain is made (`Brains::add`), and `ai::releasedFlags` those of a player
handed back to the AI (`AiHumans::switchPlayer`). A Warrior downed in a fight (his health gone) counts as knocked out,
as one a script knocked out does. So beside a crew Warrior downed in any fight the prompt shows with a flash and
d-pad left revives him, with no script call (user item 8). The disc test `repo:tests/platform/disc_revive_test.cpp`
downs a `level80` Warrior with a strike, walks player 1 to him by the stick at 60 % and revives him: 351 ran 22
updates, 353 34, 389 25 and the revived's 354 73, each counted with the update it starts (the runtime read 21, 33, 25
and 72), the steer turned him over 25 updates to 8.8° left of the
line to the head at the press (351's direction on the disc is 10° to the right; the runtime 11-13°), and he ended
0.81 m from the head (351's reach 1.04 m less its root motion; the runtime 0.70-0.71 m from a standing Warrior).
Readings and stand-ins:

- **Blocked** is the swap prompt's reading of the state mask (knocked out, cuffed, wounded) plus anything that keeps
  him off his feet (down, held, in the air), which covers the way test's "grabbed"; "any held flag" is the record bits
  his clips hold.
- **The way** (`human::wayClear`, `repo:src/human/way_clear.h`): the 0.2 m capsule, as tall as the reviver's own
  (Coney's 2 m upright capsule on his scale), swept from his feet to the downed human's against the other humans'
  capsules and the level's walls (triangles the walking sweep counts, tested every 0.1 m of the way). **Coney's
  reading** of mask `0x2803`: humans and walls; objects, cars and glass panes are not swept.
- **The led steer** (`Fighter::steerRevive`) turns him to the line to the downed human's head, less 351's direction,
  and slides him to 351's reach from it, both over 351's time to its first event + 0.1 s, as an attack's led steer.
- The pair ids, `Human_SetPartner`, the `+0xe4` calls and the push spheres have no counterpart.
- The revived's 389 is taken from the reviver's anim set (the whole get-up is one paired chain).
- **Human_Init's other flags**: cops and dealers get `0x2000` and `0x40000`; **stand-in**: the 1-in-2 woundable `0x8`
  of the other brain types is not given (no Coney code reads it, and the roll would shift every seeded run).
- **Not built**: an AI Warrior reviving ([SaveHuman](ai-goals.md#goal-save-human)) and a co-op partner's flash.
  Nothing in the original wakes a knocked-out human when record `+0xf0` passes. `Humans_Update` (`0x00249108`) reads
  it to set a wounded human dying (`0x80000000`) once it has passed; to make a knocked-out one without flag `0x4` dead
  when no camera sees him (otherwise it moves 14 s on), and a revivable player whose crew is all out dead; and to
  start a dead body's fade (`0x200000000`) when no camera sees it. So a revivable Warrior stays down until a revive,
  a flash or the script wakes him ([Who sets and clears the knocked-out bit](combat-reactions.md#knocked-out-bit)).
  Confirmed (code) for `Humans_Update`'s reads; that no other code reads `+0xf0` is inferred.

## Coney's implementation

The meters and the rage awards live in `repo:src/combat/meters.h` and `repo:src/combat/rage_awards.h`, the rage
handlers in `ScriptedHumans::runRageHandlers`, and the revive in `repo:src/gamemodes/gameplay_revive.cpp`,
`repo:src/human/human_revive.cpp` and `repo:src/combat/revive.h`. [Combat: Coney's
implementation](combat.md#coneys-implementation) says what each file does. What differs from the original is written
beside each subject above: rage's screen look, sounds and `RageFull(human, false)` ([Everything rage
does](#rage-effects), item 26), the keep-weapon drops ([Dropping what is held when hit](#keep-weapon), issue #91) and
the revive's readings and stand-ins ([Reviving](#revive)).

## Open questions

- At runtime, a 351 cut short by a hit: the code says `Revive_Finish` still runs; that case has not been staged.
- What `Revive_Start`'s vtable `+0xe4` calls with (1e9, 7) and (1.0, 7) set (inferred: the push weight, as
  `Human_KnockOut` gives 1e9).
- What sets the flash clip 665's play rate (about 0.75 at runtime; `Human_PushOverlayClip` passes no rate).
- Whether the presses on landing from a jump give a command at all ([The flash during a move](#flash-mid-move)).
