---
kind: topic
system: combat
owns:
  functions: ["0x0025e5a8", "0x0026dd08", "0x002854b0", "0x002855f8", "0x002856b8", "0x002880d8"]
---
# Combat: throws, weapons in hand and mugging

Verified against: `SLUS_212.15` (NTSC-U); runtime claims name PCSX2 2.9.94 and their method in How this was checked.

## Summary

This page covers what a player does with a human he holds and with a weapon in his hand. Circle in the hold throws the
man: the stick picks where he flies, a wall in reach turns the throw into a slam, and the flying body knocks down
whoever it meets. Triangle picks up a bat, a knife or another object; with one in hand, square and cross swing it, a run
gives a running swing, and an overhead object, a brick or a blaster is thrown instead. With a knife or a broken bottle,
square in a hold is a kill; with a bat, a cross pressed before the last swing has ended is lost, and an AI's bat hit
deals the bat's own damage and knocks the victim down. A hit makes a hurt man drop what he holds, a grab makes him drop
a bat even when he is not hurt, and a defeat drops anything, unless a flag (set in rage) keeps it. Triangle in a front
hold starts the mugging: a stick game against the clock, harder for some victims than others. A tackle with handcuffs
leads to the cuffing hold, the same game between holder and held. The grab and the hold these start from are on [Combat:
grabs](combat-grabs.md); the damage and the reactions on [Combat: reactions](combat-reactions.md); how an object hangs
from the hand on [World objects](objects.md#held); the mug meter on [HUD](hud-functions-panels.md#mug-meter-layout); the
money on [Crimes](crimes.md#mugging). The commands, the state bits and the tables every combat topic reads are on
[Combat](combat/index.md#data).

## Map

No source file names this code, and the names are ours. The throw and the pick-up clip lie in `Human/`; the stick games
in the player's combat code after `cns/cnsplayertag.cpp`; the melee weapon's handlers in the stretch after
`TaskEngine/` ([Source map](source-map.md#position), inferred). Every combat function, with its role and level, is on
[the combat function list](combat/functions.md); these are the entry points, each read in Ghidra at its address.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0026dd08` | `Player_Throw` | circle in the hold: the side, the clip, the wall ray and the turn from a front hold ([Throws](#throws), [Where a throw goes](#throw-direction)) | confirmed (code) |
| `0x00263380` | `Grab_StartPairedMove` | a throw from a rear hold: the spin to the front, then the throw ([Where a throw goes](#throw-direction)) | confirmed (code) |
| `0x003fead0` / `0x003fd600` | `MeleeWeapon_OnMessage` / `MeleeWeapon_Break` | a melee weapon taken up; broken by its message 1 ([A bat in hand](#bat)) | confirmed (code) |
| `0x0025e5a8` | `Human_PlayPickupClip` | the pick-up clip, by the object's pick-up animation and height ([A bat in hand](#bat)) | confirmed (code) |
| `0x00253688` | `Human_ApplyMoveStyle` | applies an anim set to the clip slots ([A bat in hand](#bat)) | confirmed (code) |
| `0x00286cc8` / `0x00287a18` | `Player_Square` / `Player_Cross` | the armed branch of square and cross ([Moving attacks with something in hand](#armed-moves)) | confirmed (code) |
| `0x002880d8` | `Player_CrossWithWeapon` | square and cross with a set 4-6 object: smash or throw ([Weapons](#weapons)) | confirmed (code) |
| `0x00392b88` | `ThrownObject_HitHuman` | a held or thrown object's hit on a human: its `CfgObj` damage, not the clip's ([A bat's hit](#bat-knockdown)) | confirmed (code) |
| `0x00257e38` / `0x00257d70` / `0x00257ce8` | `Human_EndThrowAimIfHurt` / `Human_EndThrowAimDropHeavy` / `Human_EndThrowAim` | the three drop helpers ([Dropping what is held](#keep-weapon)) | confirmed (code) |
| `0x00288838` | `Player_ArmedSpecial` | commands `0x22` / `0x39` with a melee weapon: throw it (`Player_ThrowMeleeWeapon`, `0x00288698`; [Weapons](#weapons)) | confirmed (code) |
| `0x002856b8` | `Player_UpdateMugging` | the mugger's stick game, mode 0 ([Mugging](#mugging)) | confirmed (code) |
| `0x002855f8` | `Mug_MoveTargetAngle` | a new target angle for a stick game | confirmed (code) |
| `0x002854b0` | `Brain_SetInterrogateOverride` | the script binding `SetInterrogateParam`: the override of the mugging's record | confirmed (code) |
| `0x00284ca0` / `0x002853a8` | `Mug_GetParams` / `Mug_GetPlayerVictimParams` | the mugging's records ([Mug difficulty](#mug-difficulty)) | confirmed (code) |
| `0x0022cd78` | `Grab_StartMug` | the start of the mugging proper: the start time and the limit | confirmed (code) |
| `0x00286550` / `0x002833c0` / `0x00283a30` | `Player_UpdateMuggedGame` / `Player_UpdateCuffHold` / `Player_UpdateMugHold` | the stick games of modes 1-3 ([The other stick games](#other-stick-games)) | confirmed (code) |
| `0x0022d030` / `0x0022d1f8` | `Tackle_StartHold` / `Tackle_EndHold` | the cuffing hold's start and end ([The cuffing hold](#cuffing-hold)) | confirmed (code) |
| `0x00284140` | `Player_UpdateHold` | each update of the cuffing hold | confirmed (code) |
| `0x00282b68` / `0x00283078` | `Hold_GetParams` / `Hold_GetParamsB` | the cuffing hold's records | confirmed (code) |
| `0x00260250` | `Cuff_Start` | the cuffing, after the holder's win | confirmed (code) |

## Data

The move table, the Anim Range List and the hit codes are on [Combat](combat/index.md#data). The fields this page adds
are below; each was read in the function named.

| Field | Meaning | Evidence |
| --- | --- | --- |
| human `+0x5c0` | the wall throw's vector: the ray's direction × the distance to the wall, set on the victim by `Player_Throw` | confirmed (code) |
| human `+0x3bd` | arms the thrown body's one wall-impact sound, set by every throw | confirmed (code) |
| human `+0x338` | the object in the hand, Nil when empty | confirmed (code) |
| human `+0x5b8` | set to 1 by the pick-up search when the hands are full (`0x0024d810`) | confirmed (code) |
| melee weapon data `+0x10`, `+0x18` | the weapon's state (0 free, 3 taken up, −5 deleted on its next update) and its holder (`MeleeWeapon_OnMessage`) | confirmed (code) |
| object type `+0x65`, `+0x86`, `+0x87` | the pick-up animation, the type and the anim set (`Human_PlayPickupClip`) | confirmed (code) |
| anim record `+0x0c`, `+0x10`, `+0x28` | the anim set stack's depth (at most 3), the sets as bytes, the clip slots (slot × 4) (`Human_ApplyMoveStyle`) | confirmed (code) |
| human `+0x5a8`, `+0x5ac` | a stick game's target angle and its frame time (`Player_UpdateMugging`) | confirmed (code) |
| record `+0x128`, `+0x12c`, `+0x130`, `+0x134`, `+0x138` | a stick game's state, progress (ms), start, time limit and off-target time (`Player_UpdateMugging`) | confirmed (code) |
| human `+0x5b4`, `+0x5b5`, `+0x5b6` | a stick game's result, whether the stick is on target, and a byte cleared at the end (`Player_UpdateCuffHold`) | confirmed (code); `+0x5b6`'s role not traced |

**The stick-game record**, which every mugging and hold reads (layout from `Brain_SetInterrogateOverride` `0x002854b0`
and its readers, confirmed (code)):

| Offset | Meaning |
| --- | --- |
| `+0x00`, `+0x01` | the rumble at the edges of the holds' middle band ([Rumble in the holds](#other-stick-games)) |
| `+0x02` | the rumble on target |
| `+0x04` | the time on target required (ms) |
| `+0x08` | the period: a new target angle per this much progress (ms) |
| `+0x0c` | the time off target allowed (ms) |
| `+0x10` | the tolerance: the stick counts as on target within it (degrees, stored in radians) |
| `+0x14` | the re-roll gap: each new angle lies more than this + 20° from the old one (degrees) |
| `+0x18` | the victim's time limit (ms), read once when the game starts |
| `+0x1c` | the rumble off target |

## Behaviour

### Throws {#throws}

Read in Ghidra: confirmed (code) where no other level is given.

`Player_Throw` (`0x0026dd08`) compares the camera-turned stick with the player's facing: within 45° → 147
`THROW_01_FROM_GRAB_FRONT`, beyond 135° → 151 rear, otherwise 149 right or 153 left. A wall within reach of the
throw gives the `THROW_02` set (155 front, 264 damage). A throw costs the power fraction 0.25 (**100 of 400**). The
victim plays 152 and lands in 196 `GROUNDED_IDLE`, where square plays the grounded strikes. Confirmed (code), and
confirmed (runtime) for 147, 151, 155 and the damage.

**Picking the wall throw** (`Player_Throw`, confirmed (code) at `0x0026dd08`). The stick side (0 front, 1 right,
2 rear, 3 left) also names a body axis (+y, +x, −y, −x). A ray (`WorldManager_RayCast`, mask 0) starts 1.4 m above
the player's position and runs along that axis for the far range of the side's wall clip
(`AttackTable_GetFarRange`, `0x00254508`: 155 front, 157 right, 159 rear, 161 left; on the disc 2.25, 1.549, 1.25 and
1.549 m, before any class override) + 0.2 m. If it hits nothing, a second ray runs along the stick's own world
direction. A hit counts only on a steep face (normal · up < cos 80°): its distance is kept, and when it is also no
more than the far range (without the 0.2 m) the throw becomes the wall throw of that side (`THROW_02`, kind 29, damage
index 29) and the victim's `+0x5c0` gets the ray's direction × the distance. Otherwise it is the plain throw (kind
25). A face more than 45° off head-on (normal · ray > cos 135°) still gives the wall throw but skips the alignment
below. From a front hold, before the pair starts (`Attack_StartPaired`), a plain throw turns the player onto the
stick and carries the victim round with him, and a head-on wall throw aligns the pair instead (`Human_AlignToObject`,
0.01 s); from a rear hold there is no turn ([Where a throw goes](#throw-direction)). Every throw sets the victim's
`+0x3bd`, which arms its one wall-impact sound ([Moving strikes](combat/index.md#moving-strikes)).

**What the thrown body hits** (confirmed (code) at `0x0021b290`, `0x00264bd8`, `0x00265f70`; the clip events and the
Anim Range List values are the disc's). The plain throws' victim clips switch the victim's own strike shapes on: event
`0x13` (`Human_StrikeAllOn`: all ten bone shapes, and flag `0x2` on the 0.35 m capsule) to `0x14`, at frames 0-32 for
148 `THROW_01_FROM_GRAB_FRONT_REACT` (`gen_grab_front_throw_front_re`), 7-22 for 152 rear, 13-18 for 150 right and
13-20 for 154 left. So the flying victim is the striker: `Human_TestStrikes` tests his shapes and capsule against the
spine and head shapes of every body near him, foe or friend, exactly as for a punch, and the first overlap calls
`Strike_Contact` with **the thrown human as the attacker**. There is no radius of its own: the reach is the
capsule's 0.35 m plus the bystander's spine (0.18 m) or head (0.15 m). The thrower is not left out by
`Human_MayStrikeBody` (`0x00227180`); what keeps him from being struck (a shared body group, `+0x3c`) was not
traced. What a bystander gets comes from the **victim clip's** Anim Range List entry, which no class overrides
(the values of 1,196 of the disc's 1,209 lists):

| Ids | Damage `+0x0a` | Hit code `+0x0c` | Flags |
| --- | --- | --- | --- |
| 148, 150, 152, 154 (throw victims) | **−60** | `0x3a`: straight, high, crushing | 0 |
| 156, 158, 160, 162 (wall-throw victims) | 0 | 0 | 0 |
| 296-303 (extreme reactions) | 20 (0 in 40 lists) | `0x2a`: straight, high, heavy | 0 |

- **No health is lost.** `Human_AddPendingDamage` keeps the larger magnitude but stores the sign, and
  `Human_ApplyPendingDamage` subtracts only a pending value of 1 or more, so −60 takes nothing. (Between two friendly
  players it is negated to 60.) The pending value is not 0, so the hit still goes on to the reaction, and the
  bystander's foe becomes the thrown human.
- **The reaction is a crushing high hit**: by the [reaction table](combat/index.md#hit-codes), 300-303
  `REACT_EXTREME_HIGH_*` by the side the thrown body comes from. These carry the knockdown event (type 7), so the
  bystander goes down ([Reactions](combat-reactions.md#reactions)). The usual adjustments apply: −1 strength for a
  victim with flag `0x200` (292-295, still a knockdown), at most 1 with `0x80`, the height by the two roots' height
  difference, and hit armour.
- **It can chain.** The extreme reactions have strike windows of their own (300-303: type `0xf` on the spine and head,
  and the shins in 301 and 302; 296-299: `0x13`), so a felled bystander who flies into a third human strikes him
  for 20 with a heavy high hit (292-295, knockdown).
- **The negative value also hurts the thrown human.** Thrown through a glass pane (`Glass_Break` from
  `Strike_Contact`) while he holds `0x400000` and has a grab partner, he takes 60 himself, kept above 0 when he has
  state `0x800000000`. Into a world object he takes the object type's `+0x58` × 5; into a car, 100 (the same cap),
  with a camera shake and style for a player thrower ([World objects](objects.md#door-break)).

**The wall throw's slam** (confirmed (code) at `Human_HandleMessage`'s message `0xc1`, `0x00245920`; the clip
events are the disc's). The victim clips 156-162 carry no `0x13` window, and their 0 damage and 0 hit code leave
a bystander their own shapes touch untouched (a pending 0 is dropped). The slam is one event of type `0x41`
(message `0xc1`) at frame 6 of 156 (front), 12 of 158 (right), 11 of 160 (rear) and 15 of 162 (left). For an anim in
156-163 while the victim holds state `0x2000`, the handler sweeps the victim's capsule along the vector the throw
stored at `+0x5c0` (`IPhysics_CollideShape`) and calls `Strike_Contact` (human vtable `+0x104`) for each body met
that is not already on its contact list. On the level mesh that gives a dust puff (`sub_shack_puff_aligned`) at the
contact, and a material impact sound unless the victim holds `0x400800` or is airborne; a world object takes the hit
and hurts the victim as above. Separately, the victim's own move meeting the mesh plays the one wall sound the throw
armed at `+0x3bd` (`Human_OnContact`, `0x00219d50`). The same frames carry a one-frame strike window
(type `0xf`): the spine for 156 and 160, a foot (30) for 158, the right hand for 162. The 264 damage is the thrower's
(clip 155's entry, index 29), applied on the grab move's first update, not the wall's. There is no separate
head-slam clip or reaction beyond these four victim clips. `CfgPowerClass` flag 22 is not read here (it belongs to
`Grabbing_PickMove`, [AI](ai.md#fight-reactions)).

### Where a throw goes {#throw-direction}

Read in Ghidra: confirmed (code) where no other level is given.

How the original turns the stick into the thrown body's heading, read in `Player_Throw` (`0x0026dd08`),
`PlayerRecord_Update` (`0x00146078`), `Human_TurnToOver` (`0x0023cf88`), `Attack_StartPaired` (`0x00262ac8`) and
`Grab_StartPairedMove` (`0x00263380`); confirmed (code). Headings below are measured from +y, anticlockwise; the
**stick heading** is the heading the camera-turned stick points along (its angle − 90°).

1. **The stick, unsnapped.** The angle is the per-player record's current buffer (`+0x08` / `+0x0c` by `+0x18`):
   `atan2` of the pad's camera-turned left stick, read on the update the throw starts (the circle press, or the end
   of the hold clip for a throw buffered during a grab strike, `Grab_OnHoldClipEnd` `0x0026d8c8`). There is no
   snapping to 4 or 8 directions, no smoothing and no history; the only coarseness is the stick table's per-axis dead
   band (raw 95-160, [Front end](frontend.md#pad-record)), so a stick within about 14° of an axis at full deflection
   reads as that axis, as it does for walking (inferred from the table). The magnitude only has to be above 0.25.
2. **The side, from the facing.** `d` = stick angle − the facing (the transform's +y axis), wrapped: `|d|` below 45°
   is the front (0), up to and including 135° a side, right (1) when `d` is negative (clockwise), left (3) otherwise,
   and beyond 135° the rear (2). Exactly 45° is a side, not the front (the test is `0.785 <= |d|`). The clip is
   147 + 2 × side (149 right, 151 rear, 153 left), or 155 + 2 × side for the wall throw ([above](#throws)).
3. **From a front hold, the pair is turned onto the stick.** For a plain throw the thrower's wanted heading is
   **stick heading + side × 90°** (front +0, right +90°, rear +180°, left +270°): the side's body axis then points
   exactly along the stick. `Human_TurnToOver` turns him there at a constant rate over **1/6 s** (5 updates; no turn
   under 0.01 rad). The victim is carried round on a circle about the thrower: its offset from him is rotated from
   his current heading to the wanted one and `Human_MoveToOver` slides it there over the same 1/6 s, while
   `Human_TurnToOver` turns the victim to the wanted heading + 180° (facing the thrower). The paired clips start on
   the same update (`Attack_StartPaired`, which checks the victim stands in place, `Pair_CheckPlace`, for 147-153
   only), so for the 5 updates after it the turn's rate (what is left to turn ÷ 1/6 s) adds to the clips' own root
   rotation and motion. At runtime the front clip 147 did not rotate the thrower then, so he reached the wanted
   heading in 5 updates exactly; 149 (right) took about 3.2° per update off the turn and 153 (left) added about 2.7°,
   and both kept turning the thrower afterwards (confirmed (runtime), [below](#throw-runtime)).
4. **From a rear hold, no turn.** A throw from a rear hold (human flag `0x40000000` clear, the usual case) goes to
   `Grab_StartPairedMove`: the spin to the front 80 / 81 plays first, then the side's throw clip, with no turn and no
   alignment. The side is chosen against the facing when circle is pressed and the thrower's heading does not change
   during the spin, so the body flies along **the side's body axis**, up to 45° off the stick. With `0x40000000` set
   (the grab blocks of [Human flags](combat/index.md#human-flags)) the front-hold path above is taken.
5. **A wall throw** aims at the wall, not the stick: the first ray runs along the side's body axis (the stick's own
   direction only when that ray hits nothing), and a head-on hit aligns the pair to the hit point over 0.01 s; a face
   more than 45° off head-on gets no turn at all (above).
6. **What carries the body.** After the turn nothing steers: the victim clip's root motion moves the body (148 front,
   150 right, 152 rear, 154 left), so its path is the clip's, rotated onto the stick. There is no auto-aim toward
   humans, objects or walls (`Player_Throw` makes no target search; only the wall ray above), and no velocity of its
   own is set; collisions stop or deflect the body ([What the thrown body hits](#throws)).

#### Where a throw goes, at runtime {#throw-runtime}

Measured in PCSX2 2.9.94: confirmed (runtime) where no other level is given; none of the runs has a run record ([How
this was checked](#how-this-was-checked)).

At runtime (confirmed (runtime), PCSX2 2.9.94, a copy of slot 6 with the `scripted-pad`, `right-stick` and `puppet`
patches; scenario `repo:research/traces/scenarios/throw_direction.toml` for one of them: PoizoCiv placed 1.0 m ahead
and grabbed with circle, power set to 400, then the stick and circle on the same update at stick magnitude 0.9, so the
hold walk (above 0.95) never moved the pair first). 21 throws, the stick between 0° and 180° from the camera's up and
the player's facing turned between −170° and +160° from the throw's heading. Positions are in the **stick's frame**:
metres right of and along the stick heading, from the thrower's position on the update before the throw.

| Side (clip) | Throws (clear) | Thrower's heading at the end − stick heading | Victim 5 updates in | Victim lands (196) |
| --- | --- | --- | --- | --- |
| front hold, front (148) | 6 (`d` −44° to +40°) | 0.0° | (0.31, 1.01-1.06), whatever `d` | (0.24-0.25, 6.84-6.88), 48 updates after |
| front hold, right (150) | 2 (`d` −51°, −46°) | −0.2° | (−1.20, 0.38) | (−0.44, 6.46) and (−0.51, 7.45), 45 updates after |
| front hold, rear (152) | 2 (`d` 160°, 166°) | +1.2° | (−0.29 to −0.32, −1.46) | (−0.51, 5.73) and (−0.56, 6.17), 44 updates after |
| front hold, left (154) | 1 (`d` 114°) | −3.5° | (0.94, −0.36) | (0.00, 5.65), 43 updates after |

- The thrower always ended facing the stick heading: the side clips turn him by about −90° (right), 180° (rear) and
  +86.5° (left) after the 1/6 s turn put the side's axis on the stick; confirmed (runtime). These are the clips' root
  turns **times their rate** (0.75): the sampler scales bone 0's turn by the task's rate as it does the root velocity
  (confirmed (code) at `0x00104ce0`; the disc's 149, 151 and 153 sum to −89.9°, −180.0° and +86.4° that way, and to
  −119.9°, −240.0° and +115.1° without it; [Root motion](formats/animation.md#root-motion)).
- The landing point is the same in the stick's frame whatever the stick's angle from the facing (the front's six
  throws, `d` from −44° to +40°, landed within 4 cm of each other), so the turn takes the whole difference. Seen
  from where the thrower stood, the body lands within 5° of the stick: 2.1° right of it for the front throw, 3.9°
  and 5.1° to the left for the right and rear throws, 0° for the left one (the clips' own sideways drift, up to
  0.56 m). The victim's displacement measured from **its own** starting point differs from the stick by up to 7° from
  the front, because its start is rotated round the thrower first; measure from the thrower. Confirmed (runtime).
- Throws that met something in the street at 3-4 m (not identified) landed short (3.0-4.3 m instead of 5.5-7 m) and
  up to 23° off: collision, not aim. They are left out of the table. Confirmed (runtime).
- **Rear hold** (four throws where the puppet stood behind the player and the grab became a rear hold, 84 / 85):
  each played 80, then the throw at 17 updates after the press, with the thrower's heading unchanged until the
  throw clip began. The bodies landed along the side's body axis (−0.2° rear, −0.6° right, −11.6° left, from the
  thrower), 13° to 38° off the stick. Confirmed (runtime).

So a throw from the front hold goes exactly where the stick points, give or take the clip's few degrees, wherever the
stick is within the side's 90° sector; only a rear-hold throw is limited to the four body axes. Confirmed (runtime) for
the throws above.

### A bat in hand {#bat}

Read in Ghidra: confirmed (code) where no other level is given.

**At runtime** (confirmed (runtime); `dyn_bat_tuff` put in the player's hand with `Human_PlaceItemInHand`
(`0x00238540`), the civilian 1.3 m ahead):

| Input | Player | Victim | Damage | Tutorial callback |
| --- | --- | --- | --- | --- |
| square | 34 `BAT_COMBO_S1` | 294, then down (198 / 196) | 64 | 34 |
| cross (on its release) | 36 `BAT_COMBO_X1` | 292, then down | 64 | 36 |
| square or cross at a grounded victim | 37 `BAT_COMBO_GROUNDED_STRIKE_01` | | none here (the victim was getting up) | |

So every standing bat hit knocks the victim down, and the next one must wait for it to stand; confirmed (runtime) for
the two swings above.

**Picking it up.** A bat (`dyn_bat_tuff`, class `melee_weapon`, `TYPE_BAT` 3) is [pickable](objects.md#pickable), so
triangle's pick-up search takes it ([Breakables](combat-targets.md#breakables) has the search; a script's prompt on it
comes first, [Crimes: triangle](crimes.md#triangle)). Confirmed (code) at `0x0024d810`, `0x003fead0`, `0x0023bf00`:

1. A type other than 12 (`TYPE_SPECIAL`) or 24 is taken by a player only with empty hands (human `+0x338` Nil). With
   something in hand the search instead sets human `+0x5b8` = 1 and drops the held object (`0x00257f38`). Confirmed
   (code).
2. The winner's record is pinned and it gets message 0. The weapon's handler (`MeleeWeapon_OnMessage`,
   `0x003fead0`), while its state (data `+0x10`) is 0, sets it to 3 and sends the human message `0x14`, which plays
   the pick-up clip (`0x0025e5a8`, below). Confirmed (code).
3. The clip's event (message 3) applies the object's anim set (object type `+0x87`, 3 for a bat) when the human's
   differs (`0x00221ed0`); `Human_PickUpObject` has no case for type 3 and returns 0, so `0x00227010` puts the bat
   in the hand (`+0x338`). Confirmed (code).

**The pick-up clip** (`Human_PlayPickupClip` `0x0025e5a8`, from the human's message `0x14` at `0x00246090`; confirmed
(code)). The handler reads three bytes of the object's type: the **pick-up animation** (`+0x65`, `CfgObj`'s
`pickup_anim`, the `ANIM` constants of `Caps.lua`), the **anim set** (`+0x87`) and the type (`+0x86`). The clip is
the pair's low one when the object's position is at most **0.8 m** above the human's (z difference), else the high
one:

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
checked against an inventory count first (`0x0041e420`, `0x0041ded0`, not traced). Confirmed (code).

At runtime (a bat 1 m ahead): triangle started clip **461** with set 3, `Human_PickUpObject` ran 9 updates later,
and the hand held the bat in that update's sample. Confirmed (runtime). `Human_PlaceItemInHand` (`0x00238540`, the
binding `HuPlaceItemInHand`) is the scripted way to arm a human; the pick-up does not use it (confirmed (code)).

**In the hand.** The bat hangs from **pose bone 25, the right hand** (bone 19 is the left, which the left-hand pick-up
clips use), at the offset clip 461's **type-9 event at frame 7** gives; the attachment and its maths are on [World
objects: objects in a human's hand](objects.md#held). The event (confirmed (runtime), read from Rembrandt's clip 461 in
memory; 462 has the same event at frame 4, and his 481, 482, 498 and 499 are the same two clips): bone 25, position
`(47, 88, 54)` → (0.0459, 0.0860, 0.0264) m, rotation `(22788, 4078, −23079)` → (0.6954, 0.1245, −0.7043, 0.0690). The
bat's stored local pose is that position × Rembrandt's scale 0.97, slid 0.39 m (`dyn_bat_tuff`'s `CfgObj` `+0x70`) along
the bat's own `y`: **(0.1500, −0.2908, −0.0053)** with the rotation unchanged. The hand at idle sits at (0.29, −0.03,
1.05) in the model frame, on the human's right (`+x` with the model facing `+y`). The local pose did not change through
the idle, a walk (stick 60 % up), the square swing 34, the block (R1) and a run (stick 100 % up): no bat clip moves it,
so the bat swings with the hand alone. Confirmed (runtime); a screenshot of the swing showed the bat in Rembrandt's
right hand.

**The anim set.** A human keeps a stack of up to 3 anim sets (record `+0x0c` the depth, the sets as bytes at `+0x10`;
push `0x00253ed0`, pop `0x00253f28`). Applying one (`Human_ApplyMoveStyle`, `0x00253688`) resets the clip slots
(record `+0x28` + slot × 4) from the defaults at `0x005105d8` (a player's slot `0xe` is always 380) and then writes
the set's overrides. With a set 1-3 weapon in hand, square and cross read slots `0x10` and `0x11` (and `0x13` at a
grounded target), which is how the bat's 34 and 36 replace 12 and 11 ([Moving attacks with something in
hand](#armed-moves)). The three melee sets, confirmed (code):

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
code (confirmed (code)). At runtime the stack's depth was 1 with a bat placed (confirmed (runtime)).

**Which objects carry which set** (the object's `+0x87`, `CfgObj`'s `anim_set`, from the objects list): 1
`KNIFE_WEAPON_SET` (6 `TYPE_KNIFE`, a `TYPE_BROKENBOTTLE`), 2 `BATON_WEAPON_SET` (11 `TYPE_BATON`, a `TYPE_BAT`), 3
`CLUB_WEAPON_SET` (42 `TYPE_BAT`, a `TYPE_BATON`, the mace, Diego's weapon), 4 `OVERHEAD_WEAPON_SET` (68: drums,
chairs, amps, carts, ...), 5 `SINGLE_HAND_THROW_WEAPON_SET` (46: bricks, bottles, the molotov), 6 `GHETTO_SET` (the
two blasters); everything else is 0 `NONE_SET`. The numbers are inferred from the clips each set installs (knife,
baton and bat combos; the barrel idle 509 for 4, the ghetto idle 555 for 6; 5 installs nothing and serves the
one-handed throw) and from `dyn_bat_tuff` being `CLUB_WEAPON_SET` with set 3 (confirmed (runtime)). Sets 7 and up
are the human's own states (7 installs the grab hold 82 and the grab strikes 51 and 55), never an object's
(confirmed (code)).

**Losing it.** Triangle with nothing to take drops the held bat at once, with no clip (`0x00257f38`, falling from the
hand under physics: [Drop](objects.md#held)); confirmed (runtime). For a player that standing drop comes from the
dispatcher's last step, after the climb, context action and jump tries ([step 6](characters.md#triangle-held));
confirmed (code). A weapon **breaks** only on its message 1 (`MeleeWeapon_Break`, `0x003fd600`): shatter particles,
hidden (`0x100000`), message 7 to its holder (data `+0x18`; the human's message 7 pops sets 1-5), and state −5
deletes it on its next update. Confirmed (code); what sends message 1 is open ([Open questions](#open-questions)). At
runtime 12 bat hits on a civilian (64 damage each, 34 and 37 alternating) never broke it and its handler saw no
message 1, so hits alone do not wear a bat out (confirmed (runtime)). A weapon's wear is the hit counter `+0x10d` /
`+0x10e`, one less on each `use` event; `+0x128` is not its hitpoints ([Weapons](#weapons)); confirmed (code).

### Moving attacks with something in hand {#armed-moves}

Read in Ghidra: confirmed (code) where no other level is given.

Square, cross and L2 + cross or square choose by the **held object's set** (`0x00231a80`: the held object's
`+0x87`; `0x00231a38` gives its type, `+0x86`), not by the human's set stack. Confirmed (code) at `0x00286cc8`
(square), `0x00287a18` (cross), `0x0027d800` (charge), `0x0027d900` (dive), `0x002880d8` (throw) and the dispatcher
`0x0027c120`. "Run" below is gait 4 with record `+0x08` clear (`0x00223a60`), and for cross also gait 5 with
`+0x08` clear (`0x00223a98`); either needs `0x00225c10` (for a pad player: the stick above 0.95 at `0x005102e8`, plus
state tests) and no fight stance (state bits `0x3`, `0x00228340`). "Walk" is gait 1-3 with the stick at 0.12
or more, and the unarmed walk attack also needs no fight stance ([Attacks](combat/index.md#attacks)); for the throws a
stance only turns the walking throw (gait 2) into the standing one.

| Held set | Square / cross at a run | At a walk | Standing (or in a fight stance) | Charge, dive |
| --- | --- | --- | --- | --- |
| nothing, 0 | 24 `ATTACK_FROM_RUN` (square at gait 4 only; cross at 4 or 5) | 23 `ATTACK_FROM_WALK` | square's snaps, `S1` 12 / `X1` 11 ([Attacks](combat/index.md#attacks)) | 0 / 1 |
| 1, 2, 3 (knife, baton, bat) | **501** `SWINGABLE_OBJECT_ATTACK_FROM_RUN` (`gen_run_1hand_weapon_atk`) for all three (gaits as unarmed) | **no walk attack**: the standing swing, slot `0x10` / `0x11` | slot `0x10` / `0x11` (bat 34 / 36, knife 45 / 47, baton 39 / 41); no snaps | 0 / 1, unchanged |
| 4 (overhead) | the throw 507 `BARREL_THROW_FROM_RUN` (gait 3, 4 or 5) | 506 `BARREL_THROW_FROM_WALK` (gait 2) | 505 `BARREL_THROW` (gait 0 or 1) | the throw instead |
| 5 (one-handed throw) | 472 `ONE_HANDED_OBJECT_THROW_FROM_RUN` (gait 3-5) | 471 `…_FROM_WALK` (gait 2) | 467 `ONE_HANDED_OBJECT_THROW` | the throw instead |
| 6 (ghetto blaster) | 553 `GHETTO_THROW_FROM_RUN` (gait 3-5) | 552 `GHETTO_THROW_FROM_WALK` (gait 2) | 551 `GHETTO_THROW` | the throw instead |

So **with a bat at a run, square plays 501**, not 24 and not a slot: `Player_Square` tests the held set first and, for
1, 2 or 3, takes its own branch, which has the run attack (constant `0x1f5`, through the same starter as 24,
`0x00264a80`: record `+0x08` `0x1000000`, a 0.3 s blend, no steer: it only warns the current target or, for the player
without one, the nearest between the clip's reach and far range, `0x0027b058`, [where the charge
aims](combat/index.md#charge-aim)) and then only the grounded strike (slot `0x13`, also on a tackled target), a
[tandem](combat-moves.md#tandem) (`Tandem_Start`, `0x0026f860`), an object attack on a breakable with no human target
(`0x00263eb8`, not traced) and the slot's swing. That branch has **no walk attack, no snaps, and no strike on a grabbed
target** (the unarmed 120); a walking player swings from where he is. Cross's armed branch is the same with slot `0x11`
(and combo count `+0xbc` = 2 before the swing). The knife's own `KNIFE_ATTACK_FROM_RUN` (490) is not chosen by either
button; what plays it is open ([Open questions](#open-questions)). For sets 4-6 the dispatcher sends
square and cross to the throw (`0x002880d8`) and never reaches `Player_Square`; the charge and dive commands do the
same. The throw's gait test differs from the run attack's: gait 3 already counts as a run and `+0x08` is not tested.
Set 5 first tries a **smash** on a human in reach (473 from the front, 475 from behind, `bottle_smash_attacker`; not
with a molotov, `TYPE_MOLOTOV` 8), and the throw turns a held `TYPE_KNIFE` (11) into set 5 (pop, then push 5) when it
is called with one; neither path was followed further. Confirmed (code).

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
on the clip. A hit's damage is the clip's Anim Range List value (501's own: the class table writes no index for it, [The
Anim Range List](combat/index.md#damage-table)); then, with the attacker's body `+0xd0` set and the held object's flags
(vtable `+0x54`) having `0x10000`, **(damage + the object's `CfgObj` u16 `+0x58`) × the first float of the attacker's
power class record** (`0x00222b78`, [Power meter](combat-rage.md#power-meter)), the same for a swing and a run attack.
That is what `Strike_Contact`'s code does, but at runtime it was not the path: the `level34` Furies' `dyn_bat` hits
(501, 36 and 37) never reached `Strike_Contact`, and each dealt the bat's own contact damage instead, `CfgObj` `+0x58` ×
the holder's class float with no clip damage (confirmed (runtime), [Combat: a bat's hit](#bat-knockdown); [Objects in a
hand](objects.md#held) item 3). Whether the player's bat, or the knife and the baton, ever take the `Strike_Contact`
weapon branch is open ([Open questions](#open-questions)). Because record `+0x08` has `0x1000000`, the hit takes the
moving-attack branch that 24, 0 and 1 take (a breakable gets hit kind 2, [World objects](objects.md#door-break));
confirmed (code). Rage: 501 is **event 1 × 1 = 5** (2 blocked) and kind 4, so never halved by repeats
([Rage](combat-rage.md#rage)), against the bat's square 34 at 2 and its cross 36 at 11, and the unarmed 24 at 1;
confirmed (code). The weapon is not worn: only its message 1 breaks it ([Losing it](#bat)). With an AI holding
`dyn_bat`, 501 dealt the bat's 90 (class float 1.0), hit code `0x2a`, and the victim played 292-295 and went down
([Combat: a bat's hit](#bat-knockdown)); confirmed (runtime). The player's own 501 was not measured.

### Weapons {#weapons}

Read in Ghidra: confirmed (code) where no other level is given.

Which clip square and cross play with an object in hand is in [Moving attacks with something in
hand](#armed-moves); what else a weapon changes:

- **Armed chain**: square repeats the set's two swings, 34 → 35 → 34 (bat), 39 → 40 (baton), 45 → 46 (knife), for
  as long as presses come in the window; cross's combo count 2 means it never chains. A step needs the swing's
  window `0x2`, and the bat's 34 has none, so **a bat never chains**: confirmed (runtime), [Cross with a bat in
  hand](#bat-cross). The baton's 39 (W 9) and the knife's 45 (W 15) have one.
- **Knife and bottle kills** in a grab or mount: with a knife (set 1) square or cross plays 484 (rear hold), 486
  (front hold) or 488 (mount); a broken bottle (type 43) plays 492 / 494 / 496. Each needs more than 0.25 power and
  spends 0.25. The damage lands at the start: 600 (knife) or 40 (bottle) × the power class's first float, doubled
  with flag `0x4000`; a lethal hit plays a death reaction. 483 `KNIFE_VICTIM_DIE` is never played.
- **Wear**: the clip's `use` event (`0x41`) calls `WorldObject_TakeHit(held, -1)`, which takes 1 from the object's
  hit counter (`+0x10e` first for objects with flag `0x4000000`, then `+0x10d`); at 0 the weapon breaks
  (`WorldObject_Break`, `0x00393e20`). The counters start from `CfgObj` bytes `+0x5a` / `+0x5b`; 0 (or −1) means it
  never wears. The ordinary swings have no `use` event, which is why 12 bat hits never broke a bat
  ([Losing it](#bat)).
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

### Cross with a bat in hand {#bat-cross}

Read in Ghidra: confirmed (code) where no other level is given; each runtime block says confirmed (runtime).

When cross starts a bat swing, after each bat move. **The rule**: cross acts only through its `0x10`
([Input](combat-moves.md#input)): on the update it is released after 1 to 3 updates down, or on its 4th held update.
That update starts 36 `BAT_COMBO_X1` (or 37 at a low or mid target, 501 at a free run) when, and only when, the move
before has **ended**: record `+0x08` holds none of `0x100101f` (`Player_Cross` `0x00287a18`) and none of the
dispatcher's `0x5c7fee0` (`Player_UpdateActions` `0x0027c120`). A `0x10` that falls inside the move is dropped, and
nothing keeps it: a held cross gives no second `0x10`, so the player has to press again. Confirmed (code) at those
addresses and `Pad_TapOrHold4` (`0x00144d60`); confirmed (runtime), below.

There is **no buffer for cross with a weapon**. The chain (`Player_UpdateChain` `0x00280708`, armed branch, held
object set not 0) does store cross's press `0x12` as 1 in record `+0xb8` during the swing's wind-up `0x1` (combo count
`+0xbc` at most 1) or its window `0x2`, but it plays only a stored **square** (2), and only once `0x2` opens: 1 is never
played, and stays in `+0xb8` after the move until the next attack start empties it (`Attack_Start` `0x002625a8`).
Armed cross itself (`Player_Cross`) does not empty `+0xb8`, sets the combo count to 2 before `Attack_Start` adds 1, so
nothing is stored or played during 36. Confirmed (code); the stored 1 and the count 3 confirmed (runtime).

So the time a cross is lost after each bat move is the move's whole length, its clip's phases (confirmed (runtime);
k counts updates from the move's first; "free" is the first k a `0x10` starts 36):

| Move before | Wind-up `0x1` | End `0x4` | Recovery `0x40000` | Free | What follows |
| --- | --- | --- | --- | --- | --- |
| 34 square (`BAT_COMBO_S1`) | 0-12 | 13-24 | 25-36 | **37** | 389 (`0x40000000`, refuses nothing) with nobody near |
| 36 cross (`BAT_COMBO_X1`) | 0-19 | 20-22 | 23-34 | **35** | 389 |
| square, square (the chain) | as 34: the second square is stored as 2 and never played | | | **37** | 389 |
| triangle (with nothing to take) | no clip: the bat is dropped at once ([Losing it](#bat)) | | | **next update** | cross now plays the unarmed `X1` 11 |
| cross + square (`0x22`), nobody near | 502 `SWINGABLE_OBJECT_THROW`, holding `0x10` for 35 updates; the bat leaves the hand at k20 | | | 35, unarmed | 389 |
| cross + square, a target within 2.0 m | nothing (the throw is refused: `Player_Special` `0x00287730` refuses a pad player holding an object, confirmed (code)) | | | **at once** | the cross's own release (`0x10`) plays 36 |

The 34 lasted 37 updates at runtime (k0-36), one fewer than the 38 the [frame data](combat-moves.md#frame-data) rounds
from its frame count; 36 lasted 35 (confirmed (runtime)). 35 `BAT_COMBO_SS2` never played from the pad, so its phases
were not measured. The run attack 501 (21 updates, `0x1000000` throughout, [Moving attacks with something in
hand](#armed-moves)) and the grounded strike 37 (30 updates, no phase events: `0x1` from `Attack_StartGrounded` to its
end, inferred from the frame data) were not rerun here.

Compared with the unarmed fight: there a press in a move's wind-up or window is kept and plays the next step, so the
lost stretch is only the end phase and the recovery ([Input](combat-moves.md#input)). With a bat every cross (and
every square) before the move's last update is lost: 35 updates after 36 and 37 (1.2 s) after 34, against the unarmed
`X1`'s 30 with its first 20 updates taking a press (inferred from the table above and the unarmed buffer rule).

At runtime (confirmed (runtime); PCSX2 2.9.94; copies of slot 1, the street with nobody near, and slot 6 with the
puppet `PoizoCiv` pinned 1.5 m ahead for the last row; `dyn_bat_tuff` put in the hand at update 3 with
`Human_PlaceItemInHand` (`0x00238540`) on the game's thread; patches `scripted-pad`, `right-stick` and `call-brains`,
plus `puppet` in slot 6; the stick at rest (128, 128) throughout; a tap holds the button for one update; the record's
`+0x08`, `+0xb8`, `+0xbc`, the clip and the per-player command read every update):

| Scenario | Input | Result |
| --- | --- | --- |
| `bat_cross_after_square` | square, then a cross tap 3, 8, ..., 40 updates later (14 runs) | `0x10` on k4-k36 of 34 dropped (10 runs, in every phase); on k37 (389's first update), k38, k39 and k41 it started **36** at once (4 runs) |
| `bat_cross_after_cross` | cross, then a cross tap 3-40 updates later | `0x10` on k3-k33 of 36 dropped; on k35 and later it started 36 |
| `bat_cross_after_square2` | square, square 5 updates later, then a cross tap 8-46 updates after the first | the second square stored 2 in `+0xb8` and played nothing (no 35, no `0x2`); a cross press in the wind-up overwrote it with 1; a cross `0x10` at k35 was dropped, at k39 and later it played 36 |
| `bat_cross_held` | square or cross, then cross held 8 updates from 30-38 updates later | `0x10` on the 4th held update: after 34 at k33 dropped, at k37, k39 and k41 started 36; after 36 at k32 dropped, at k36, k38 and k40 started 36; held updates (`0x16`) never start one |
| `bat_cross_after_triangle` | triangle, then a cross tap the next update | triangle (`0xa`) dropped the bat (`+0x338` Nil); the tap's `0x10` two updates after triangle played **11** `X1` |
| `bat_cross_square` | cross held 3 updates, square tapped on its 2nd, nobody near | `0x22` played **502** (the bat thrown, out of the hand at k20, 35 updates); the release's `0x10` at k2 dropped |
| `bat_cross_square_near` | the same, `PoizoCiv` 1.5 m ahead | `0x22` started nothing; the release's `0x10` on the next update played **36**, 35 updates, which hit him for 64 at k13 |

### A bat's hit: why the level34 Furies knock the player down {#bat-knockdown}

Read in Ghidra: confirmed (code) where no other level is given; each runtime block says confirmed (runtime).

At `level34` checkpoint 5 (the park) the first Fury hits on a player standing still are knock-downs of 90 health
([Scripts: the level34 fix list](scripting.md#level34-fixes), item 14). The reason: **every park and fence Fury holds
a `dyn_bat`**, so it fights with the bat's clips, and a bat's hit is the bat object touching the victim, which deals the
bat's own damage and gives a heavy high reaction.

**1. Where the bats come from: the scene.** The fourteen Furies (`ParkFury01`-`10`, `ParkFury14`, `FenceFury1`-`3`,
character types 90-94) are made with no weapon. Every Fury's hand was empty at tick 1 of the polled run (confirmed
(runtime), the polled run's hand poll); the five types' `CfgChar` weapon string (record `+0x18c`) read `none` and their
power classes 5 (type 90) and 26 (the rest) over PINE in the first run, so `Human_Init` (`0x00218008`) puts nothing in
their hands and the classes draw nothing either. Those `CfgChar` values are graded inferred: that read's output was not
kept, and what still supports them is the empty hands at tick 1 and the 90 and 72 below, which fit floats 1.0 and 0.8.
`gPlayCutScene` then binds them to `l34_c4`'s roles and binds fourteen `dyn_bat` objects (and `dyn_furies`) to its
object slots with `SceneAddObject` (inferred: the calls were logged by Coney's interpreter running the original's
chapter script, not traced in the original; the scene's fourteen `dyn_bat` and one `dyn_furies` props are the scene
data's own list, on [Scenes](../references/scenes.md#scene-l34-c4)). At the scene's start each role's pick target is the
slot its clip's first type-9 event names, and the event takes the object into the hand ([Scripts: the props go into
hands by clip events](scripting.md#level80-arrest-fire), confirmed (code) at `0x0039d870`). At runtime every Fury's hand
(human `+0x338`) went from Nil at tick 1 to a held object by tick 9, long before the scene's end, and still held it when
the fight began (confirmed (runtime), PCSX2 2.9.94, two runs, below). That the object is a `dyn_bat` (`CfgObj` class
`melee_weapon`, anim set 3, `+0x58` = 90) was read over PINE without a kept log, so it is inferred: the Furies then
played set 3's bat clips, and every bat hit dealt 90 or 72. That the hand-over is the scene's type-9 event is inferred:
the timing, the fourteen bound bats and the documented start agree, but the event values of `l34_c4`'s role clips were
not read. The script's own `HuSetKeepWeapon(Fury, 1)` before `GoalFight` keeps the bat in hand when he is hit ([Dropping
what is held](#keep-weapon)).

**2. What they swing.** With set 3 in hand an AI's square and cross play the bat's clips ([A bat in hand](#bat)):
**501** `SWINGABLE_OBJECT_ATTACK_FROM_RUN` when he arrives at a run, **36** `BAT_COMBO_X1` or **34** (the bat's square
swing) standing, and at a man on the ground either **37** `BAT_COMBO_GROUNDED_STRIKE_01` or the unarmed grounded strikes
**193** / **194**, bat still in hand (35 was not seen; 34 once, `ParkFury09` at step 1375, its hit on `ParkFury07` at
step 1386). In the recorded run `ParkFury03` and `ParkFury10`, both holding bats, played 37 at steps 730, 762, 802 and
846 and 194 / 193 at steps 767, 811 and 838, and the 37s of steps 762 and 802 and all three 193 / 194 landed on
`SceneRogue2` on the ground (hits at steps 775, 781, 815, 825 and 850; rec1's `anim-dispatch`, `strike-contact` and
`deal-damage` logs; human names by allocation order); in the polled run the downed player was hit the same way (below).
What picks 37 or 193 / 194 was not found. Their Anim Range List entries on the disc ([chunk
`0x45`](formats/animation.md#anim-range-list)), the same in the park Furies' character data (`fury_so1`-`4`, resource
`312adcb1`), `fury_lt`'s and the player's (`warr_ve`): 501 damage 20, hit code `0x2a`, flags `0x400`; 36 damage 130,
`0x2a`; 37 damage 75, `0x0a`; 34 / 35 damage 10, `0x29` / `0x2b`; 193 / 194 damage 50, `0x0a`. Those are the disc's
values, read with the chunk's layout (confirmed (code) at its readers) from `coney-tools extract`'s output; 44 of the 53
character data resources hold exactly these for these ids. That the Furies' lists at runtime (record `+0x160`) hold the
same is inferred: a PINE read in the first run gave the same for 501 and 34-37 but its output was not kept, and the disc
is corroboration, not a grade. The hit codes are supported by the reactions (292-295 from 501 and 36 fit `0x2a`,
confirmed (runtime) below). The damages can be overwritten by the class when the human is made ([damage
table](combat/index.md#damage-table)): 193 / 194 hold 50 on the disc but dealt 25 here (not traced). Code `0x2a` is a
straight high **heavy** hit ([Hit codes](combat/index.md#hit-codes)).

**3. The damage is the bat's, not the clip's.** None of the Furies' bat hits went through `Strike_Contact`
(`0x0021b290`): its hook logged only unarmed and grounded strikes, while every bat hit came from
`ThrownObject_HitHuman` (`0x00392b88`, the call returning at `0x00393158`). That path is the held object's own body
touching the victim: `WorldObject_OnContact` (`0x00394050`) takes the object type's `CfgObj` u16 `+0x58` and, for an
object in a hand, multiplies it by the holder's power-class first float (`Human_GetClassRecord` `0x00222b78`, table
`0x006619a0` + class × `0x44`); `ThrownObject_HitHuman` negates it against a friend and passes it to
`Human_DealDamage` (`0x0021d680`) with the holder as the attacker (confirmed (code)).

`ThrownObject_HitHuman` serves thrown objects too, and between the friend test and `Human_DealDamage` it can still
change the damage. Its cases, all confirmed (code) at `0x00392b88` (the fields' meanings are not traced unless
named), with A the attacker it is given, O the object and V the victim:

- No attacker: it plays the contact sound and returns with no damage.
- A friend of the attacker (`Human_IsFriendly`): the damage is negated, and `Human_DealDamage`'s fifth argument is 0
  instead of 1.
- O's type has `CfgObj` byte `+0x86` = `0x24` and A's word `+0x1a8` is above 2: × 0.25.
- O's type `+0x86` = `0x0b` and O's flags `+0x54` have `0x4000000` (the thrown knife: this case ends in
  `Human_StickThrownKnife`): when the human at O `+0x11c` is set and V is that human's target or not his friend, the
  damage is replaced by the type's `+0x58` (no class float), or by V's whole health when V's character type `+0xcc` is
  `0x4f`; a hit with O `+0x11c` empty skips every case below.
- The human at O `+0x11c` (empty: no case below applies): when his brain's type (brain `+0x4`) is 3 and V is of class 13
  (`Human_IsClass13` `0x00223e20`), × 0.5 (confirmed (code)); then, when his signed byte `+0x1b0` is not −1, × 3 against
  V of character type `0x77` or `0x78` and × 1.25 against `0x87`.

None changed the damage in this fight (all 23 calls returning at `0x00393158` in the recorded run dealt 90, −90 or −72,
`+0x58` × the class float or its negation, and the polled run's bat hits took 90 or 72), so for a Fury's bat on the
player or a fellow Fury only the friend case applies; the other cases are those of thrown objects and of other holders.

So a Fury of power class 26 (float 1.0) deals **90** with any bat clip, 501, 34, 36 or 37 alike, and one of class 5
(types 90, `ParkFury01` and `ParkFury14`, float 0.8) deals **72**; the clip's own damage (20, 10, 130, 75) is not used.
Confirmed (runtime): every bat hit on the player in the recorded run took exactly 90, from 501, 36 and 37 (all from
class 26 Furies), and `ParkFury14` (named by allocation order) dealt **−72** with 36 to `ParkFury10` at step 1339
(rec1's `deal-damage` log, return `0x00393158`), the class 5 figure measured directly. `ParkFury09`'s 34 (clip
damage 10) dealt −90 to `ParkFury07` at step 1386 through the same return, a fourth bat clip whose own damage was
ignored. A bat that touched a fellow Fury of class 26 dealt −90 (the friend case).

**The 25s: grounded strikes without the bat.** The bat's contact is not every hit an armed Fury lands. His 193 / 194
(above) go through `Strike_Contact` (return `0x0021b69c` into `Human_DealDamage`) and deal **25** each, the bat
adding nothing (confirmed (runtime): rec1 steps 781, 825 and 850, `ParkFury10` and `ParkFury03` on `SceneRogue2`). In
the polled run the player took 90, then 72 at +319 (while `ParkFury14` and `FenceFury1` were both in 501 within
0.71 m), then 25 three times while he was down (updates 394, 486 and 582, each with 195 `GROUNDED_STRIKE_REACT`), and
90 once more at update 446; the polled run read no clips at those three, so that they were 193 / 194 is inferred from
the recorded run.

`Strike_Contact`'s weapon bonus (clip damage + `+0x58`, × the float; [Objects in a hand](objects.md#held) item 3)
exists, but for these bat swings it did not run. Whether the player's own bat (64 with `dyn_bat_tuff`, [A bat in
hand](#bat)), the knife or the baton take the same contact path is open ([Open questions](#open-questions)).

**4. Why it knocks him down.** The reaction is chosen as for any hit, from the attacker's current clip's hit code and
the side ([Hit codes](combat/index.md#hit-codes)): `0x2a` gives strength 2 (heavy), height 2 (high), so 292-295
`ANIM_REACT_KNOCKDOWN_HIGH_*` by side, and those reactions knock down ([Reactions](combat-reactions.md#reactions)).
501 and 36 are not combo ids, so no strength step applies. A man already down gets the ground reactions (195, 196)
from 37 and from 193 / 194. Confirmed (runtime): every 501 or 36 hit on the standing player played 292, 293 or 295
(`Hit_PlayReaction` `0x0026b0a0` hooked), then 196 down and 199 rising; the reaction rule itself is confirmed (code)
at `0x00266d00`.

**At runtime** (confirmed (runtime), PCSX2 2.9.94: a copy of slot 7 (`level99`), `level34` checkpoint 5
loaded through mode 8 as on [Scripts](scripting.md#level34-fixes) (level index 4 to `0x005e5db0`, checkpoint 5 to
`W_GameState + 0x33a`, 3 to `+0x14c`), a state saved 9 ticks into the level once every Fury held his bat, then
[`l34_furies_first_hits`](repo:research/traces/scenarios/l34_furies_first_hits.toml) recorded from it: no input, hooks
`strike-contact`, `anim-dispatch`, `hit-reaction` and `deal-damage`; a second run polled the hands and clips over PINE).
The scenario's `slot = 7` is that `level99` slot and measures the wrong level unless `--state` gives the level34
state. In the recorded run the scene's end is step 697, the first `anim-dispatch` entry, when every human's clip is
set at once (19 to 388, 3 to 704); the table's times are steps − 697:

| After the scene's end (about) | Attacker, clip | Player | Health |
| --- | --- | --- | --- |
| +162 | | fight stance 359 | 1,170 |
| +296 | `FenceFury1`, 501 | 292, then 196 down | 1,170 → 1,080 |
| +486 | `ParkFury02`, 36; the next update `ParkFury03`, 36 | 295 (the second hit lands in it, no new reaction) | → 990 → 900 |
| +569 | `ParkFury10`, 37 | 195 on the ground | → 810 |
| +631 | `ParkFury10`, 36 | 292, then 196 | → 720 |

In the second run (scene end read from `SubwayScene`) the first hit came at +310 from `FenceFury1` or `ParkFury02`,
both in 501 within 2 m, with 293 and 90, then 72 (inferred `ParkFury14`'s, above) 9 updates later, and later the
three 25s on the downed player (above).

### Dropping what is held when hit {#keep-weapon}

Read in Ghidra: confirmed (code) at the addresses cited unless a line says otherwise; not checked at runtime.

When a hit, a grab or a defeat makes a human let go of what he holds, and what human flag `0x2000` (`HuSetKeepWeapon`,
set by [rage's enter handler](combat-rage.md#rage-effects)) changes. "Set" is the held object's type byte `+0x87`, its
move style ([Objects: the move style](objects.md#two-handed)): 1 knife, 2 baton, 3 bat, 4 two-handed prop, 5 throwable,
6 ghetto blaster. "Hurt" is `Human_IsHurt` (`0x00222ff8`): health below the power class's `+0x04` fraction of the
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
    - `Human_ReleaseCarriedProps` (`0x0024ce40`, unless cuffed; confirmed (code)) drops a held object of `ObjectAttribs`
      kind `0x27` (39, the one-sphere melee object) unless he is throwing, and lets go of the carried props
      `+0x348` / `+0x34c`;
    - `Human_DropHeld` when `Human_IsSwingingHeld` (`0x002286a0`, confirmed (code)) is true, that is held flag
      `0x800000`, or held flag `0x1000010` with something in hand during one of the eleven **throw** clips (467, 471,
      472, 491, 502, 505-507, 551-553), or when he plays 602 `ANIM_TAUNT_CHALLENGE` (the knife-toss fidget) with set 1
      in hand (`0x002664e8`). So a hit during a throw wind-up makes the object fall from the hand, raging or not.
      Despite the function's name, no melee swing clip is in its list.
2. **The ordinary reaction** (`Human_PlayReaction` `0x0026a6d0`, reached when hit armour, the held flags `0x1c16a40`
   and flag `0x800` let a reaction play; confirmed (code)):
    - when the **attacker's** clip is **620 or above** (the test at `0x0026a798` is `id ≥ 0x269` and `id ≥ 0x26c`,
      confirmed (code); so in effect `≥ 620`: the left block counter 620, the bat-block counter 629, the rage moves
      645 / 647 and 649 / 651, the specials 653 / 655 and 657 / 659) and the victim's flag `0x2000` is clear, he drops
      what he holds, any set, hurt or not;
    - a reaction that knocks down calls `Human_KnockDown`, hence `0x00257e38` (confirmed (code));
    - a stunning reaction calls `Human_Stun(victim, attacker)` (confirmed (code)). `Human_Stun` returns at once for a
      human with flag `0x100` (unstunnable; without an attacker only when he also lacks flag `0x8000000`), and calls
      `0x00257e38` only when he has none of the state bits `0xcf0` (grabbed or grabbing) and neither held flag
      `0x10200`.
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

**With flag `0x2000`** every row above keeps the object, except: the set 4 / 6 column wherever one of the three helpers
or the arrest runs (they drop those first, flag or not; the clip ≥ 620 test itself spares them, but the stun or
knock-down that usually follows does not), the throw row and a kind 39 object (step 1, no flag test), and defeat. The
flag is read at exactly five places: the three helpers (`0x00257d18`, `0x00257da4`, `0x00257e7c`), `Human_PlayReaction`
(`0x0026a7bc`) and `Human_Arrest` (`0x0022ed14`), confirmed (code); a search of every `andi …, 0x2000` in the executable
found no other reader of the human flags (inferred: complete).

**While raging**, the enter handler's other flags already close most of these paths: unstunnable (`0x100`) stops
`Human_Stun` before its drop, ungroundable (`0x80`) keeps reactions from knocking down, ungrabbable (`0x40`) stops
grabs and mounts, god mode (`0x10`) prevents defeat. What `0x2000` alone still decides for a raging player is the
drop by an attacker's clip ≥ 620 (an enemy's counter or special), his own grab or tackle start with a bat or a
set 5 object, and a paired move he starts holding a bat (`Grab_BreakOnHit`, `0x0022b960`). A hit during his own
throw still drops the object. Inferred from the code above.

### Mugging {#mugging}

Read in Ghidra: confirmed (code) where no other level is given.

Triangle in a front grab spins the victim to a rear hold (78 / 79, state `0x85`), then 338 / 339 intro and the
340 / 341 loop (player `0x105`, victim `0x200`); 342 / 343 play while the stick is on target. Confirmed (runtime).

`Player_UpdateMugging` (`0x002856b8`), confirmed (code): the stick must be above **0.5** and within the tolerance of
the target angle (human `+0x5a8`); the time on target adds to record `+0x12c` (ms). Every period the target angle
moves (`Mug_MoveTargetAngle`, `0x002855f8`), by at least the tolerance plus 20°. At the required time the mugging
succeeds: the money moves in that update (victim `+0x370` 18 → 0 at runtime, confirmed (runtime); [Crimes:
mugging](crimes.md#mugging)), then 344 / 345 play (346 / 347 on a failure), then 80 / 81 and back to the hold 82 / 83;
the mug callback runs when the mugger's end clip finishes. Past the fail time it fails. A victim's starting money:
[Crimes](crimes.md#starting-money). The parameters come from `Mug_GetParams` (`0x00284ca0`) by the victim's class
(human `+0x11b`) and the Warrior class byte `+0x0c`; the record seen at runtime: `+0x04` required **5000 ms**, `+0x08`
period **2500 ms**, `+0x0c` fail **50000 ms**, `+0x10` **50°** and `+0x14` **60°** (confirmed (runtime)). The stick
counts as on target within `+0x10` (50°); `+0x14` (60°) is a coarser gate, and each new target angle is re-rolled, up
to 64 times, until it lies more than `+0x14` + 20° (about 68°) from the old one. A victim whose brain `+0x26c` is 5
gives 1.5 times the money (at most 999). Confirmed (code); the angle's frame (world or camera) is open
([Open questions](#open-questions)).

**The stick game's states** (record `+0x128`, confirmed (code) at `0x002856b8`): 0 starts it (the first target angle,
the speech, the hint) and goes to 4, waiting; in 4, the stick on target goes to 2, and every update off target adds
to the **off-target time** (record `+0x138`) while the rumble value is 0 and resets it to 0 while it is non-zero
but not the on-target value; with the usual off-target rumble of 0 (`+0x1c`) it is therefore never reset, and
`+0x0c` is the total time the player may spend off target before the mugging fails (5); in 2, each update on target
adds to the progress (`+0x12c`), a new target angle comes each time the progress passes a multiple of `+0x08`, the
victim's lines come at half of `+0x04`, and reaching `+0x04` succeeds; leaving the target goes through 3 back to 4.
"On target" is the stick above 0.5 and within `+0x10` of the angle; the pad's rumble gets byte `+0x02` on target and
byte `+0x1c` otherwise (player record `+0x1c`, inferred from its use as the motor value). **Against an AI victim**
(victim `+0x1b0` = −1) every update also checks the victim's time limit: once game time passes record `+0x134` the
game goes to 5 with the result 0, a failure (the victim has broken free), and bar 1 of the mug meter shows the time
since the start over that limit ([Mug difficulty](#mug-difficulty)). The test reads whether the victim is a player,
not the mugger.

**`SetInterrogateParam`** (`Brain_SetInterrogateOverride`, `0x002854b0`) overrides the record for every mugging while
its `timeA` is non-zero (set 0-2 at `0x00510a18`, read by `Mug_GetParams`; sets 3-5 at `0x00510a38` serve a player
victim, `Mug_GetPlayerVictimParams` `0x002853a8`). Its arguments fill the [stick-game record](#data) in order: bytes
`+0x00`, `+0x01`, `+0x02`, then `+0x04`, `+0x08`, `+0x0c`, `+0x10` (degrees, stored in radians), `+0x14`, `+0x18` and
`+0x1c`. Confirmed (code); bytes `+0x00` and `+0x01` are read only by the hold's games
([below](#other-stick-games)).

The mugging lesson of `level99_lesson1` passes (160, 75, 255, 5000, 2500, 20000, 40, 60, 20000, 0, set 0) when it
sets the victims up (`level99_lesson1.lua:f13`): **5 s on target, a new angle every 2.5 s of it, 20 s off target
allowed, 40° of tolerance, each new angle at least 80° from the last**. After the mugging it passes all zeros
(`level99_lesson1.lua:f18`), which gives the per-class defaults back. Confirmed (code).

### Mug difficulty: who is hard to mug {#mug-difficulty}

Read in Ghidra: confirmed (code) where no other level is given.

How hard a mugging is comes from the record `Mug_GetParams` (`0x00284ca0`) builds for the pair, which differs by the
**victim's category** (`CfgChar` byte `+0x11b` of the victim's class, [characters reference](../references/characters.md))
and the **mugger's Warrior class mugging byte** (`+0x0c`, the "Mugging" column of the [Warrior
classes](../references/warrior-classes.md)). Confirmed (code) at `0x00284ca0`, `0x0022cd78`, `0x002856b8`:

1. **When the record is built.** `Grab_StartMug` (`0x0022cd78`, from the end of the 338 intro, `Mug_OnStartEnd`
   `0x00272878`) stamps the start (record `+0x130`, both sides) and, for a player mugger, sets his limit `+0x134` =
   start + the record's **`+0x18`**. `Player_UpdateMugging` asks for the record again every update.
2. **Which record.** While `SetInterrogateParam`'s set 0 has a required time (`0x00510a1c` ≠ 0) it is used as is.
   Otherwise, for a player mugger, a table in the code fills a record at `0x006cddb8`; a mugger who is not a player
   gets set 0 unchanged. Bytes `+0x00`-`+0x02` are 160, 75, 255, the off-target allowance `+0x0c` is always **50000
   ms**, the tolerance `+0x10` 50° and the re-roll gap `+0x14` 60° (`0x3f5f66f4`, `0x3f860a92`), the off-target
   rumble `+0x1c` 0. The rest, in ms, as **required on target / period (a new angle per this much progress) / the
   victim's limit**:

    | Victim category (`+0x11b`) | Mugging byte 2 | Mugging byte 3 | Any other byte (1) |
    | --- | --- | --- | --- |
    | 15 (drug dealers, `civl_dr_*`, `civl_sp_*`, `civl_we_*`) | 2750 / 750 / **4500** | 2750 / 750 / **5000** | 2750 / 750 / **4500** |
    | 5, 9, 10 (workers and rioters `civl_*_ma*`, the cops) | 3500 / 2000 / 5500 | 6000 / 1200 / 10000 | 5000 / 2000 / 8250 |
    | 14 (the Warriors: the other player) | 4000 / 1334 / 7000 | 4000 / 1334 / 7000 | 4000 / 1334 / 7000 |
    | 6, 7 (bums, drunks) | 4000 / 2000 / 13000 | 5000 / 2500 / 13000 | 3000 / 3000 / 10000 |
    | any other, `flag_14b` set (women, hookers, ...) | 4000 / 2000 / 10000 | 5000 / 2500 / 10000 | 3000 / 1500 / 10000 |
    | any other, `flag_14b` clear (civilians, gang members, pimps, shopkeepers) | 5000 / 1250 / 11000 | 3000 / 1000 / 7000 | 5000 / 2000 / 10000 |

    `flag_14b` is the class's byte `+0x14b`. The category labels are our reading of the types in each
    ([characters reference](../references/characters.md): category 15 is exactly the brain-kind-5 dealer types), so
    they are inferred from the types. The Warrior classes' bytes: on normal Ajax, Snow and Swan 1; Cleon, Cochise,
    Cowboy and Vermin 2; Fox and Rembrandt 3 (every class 2 on easy; [Warrior classes](../references/warrior-classes.md)
    for hard).
3. **What decides it.** The mugger wins on reaching the required time on target; against an AI victim he loses once
   the victim's limit has passed since the start, whatever the progress (the off-target allowance of 50 s never comes
   first). So the **slack** is the limit minus the required time, and the target angle jumps (at least 80° away) each
   period of progress: a dealer gives **1.75-2.25 s of slack and a new angle every 0.75 s** (three to four jumps to
   catch in 2.75 s), a civilian with byte 2 gives 6 s of slack and a jump every 1.25 s, and a bum 9 s. Against a
   dealer the victim's bar (bar 1, amber) fills over 4.5 s.
4. **The other player as victim** (`+0x1b0` set) has no limit: the race is mode 1 against his own record
   (`Mug_GetPlayerVictimParams`, `0x002853a8`, [the other stick games](#other-stick-games)).
5. **The payout** is the same for all (the victim's money, × 1.5 for a dealer brain, [Crimes](crimes.md#mugging)).

### The other stick games: mug meter modes 1-3 {#other-stick-games}

Read in Ghidra: confirmed (code) where no other level is given.

Three more per-update state machines run the same stick game for the other player's side of a mugging and for both
sides of the **cuffing hold** (the hold a tackle leads to, `Tackle_StartHold` `0x0022d030`, state `0x8000000000` for
the holder and `0x10000000000` for the held; `Player_UpdateHold` picks the side; [The cuffing hold](#cuffing-hold)).
Each drives the mug meter in its own mode ([HUD: the mug meter](hud-functions-panels.md#mug-meter-layout)). Confirmed
(code) at `0x00286550`, `0x002833c0`, `0x00283a30`.

What they share with `Player_UpdateMugging` (mode 0, above): the record `+0xd4` of the human running the game (state
`+0x128`, progress `+0x12c`, off-target time `+0x138`, start `+0x130`, time limit `+0x134`), the target angle (human
`+0x5a8`, moved by `0x002855f8`), the frame time (`+0x5ac`), the states 0 → 4 ⇄ 2 (through 3) → 5, the new angle at
every multiple of `+0x08` of progress, `HUD_MugMeterSet(1)` while on target, and nothing at all while a screen fade
runs (`0x005fdeb8 + 0x1e8`; the frame time is just restamped). The other human is the one at `+0xc4`. The meter's
**bar 1 is the opponent's progress** (the other player's `+0x12c` over his own required time) or, against an AI, the
time since the start over the time allowed (`+0x134` − `+0x130`); **bar 2 is one's own progress** over one's own
required time. Confirmed (code).

**Rumble in the holds.** Modes 2 and 3 grade the stick: within `+0x10` of the target (and above 0.5) the on-target
byte `+0x02`; within the coarser `+0x14` but not `+0x10` a value between bytes `+0x00` (at the edge of `+0x10`) and
`+0x01` (at `+0x14`): `b1 + (b0 − b1) × (1 − d / tol14)`, with `d` the angle off; elsewhere `+0x1c`. Since that middle
value is not 0, being **near** the target resets the off-target time in the holds. Modes 0 and 1 have no middle band.
Confirmed (code).

| | Mode 1: a player being mugged | Mode 2: the holder of the cuffing hold | Mode 3: the held player |
| --- | --- | --- | --- |
| Function | `Player_UpdateMuggedGame` `0x00286550` | `Player_UpdateCuffHold` `0x002833c0` | `Player_UpdateMugHold` `0x00283a30` |
| Who runs it | the victim, a player, against his mugger (a player in two-player play) | the holder | the held human, a player |
| Parameters | own: `Mug_GetPlayerVictimParams` (`0x002853a8`, sets 3-5 of `SetInterrogateParam`); the mugger's: `Mug_GetParams` (`0x00284ca0`) | own: `Hold_GetParams` (`0x00282b68`, by the held human's category and the holder's Warrior class byte `+0x07`, [values](#cuffing-hold)); a player held: his `Hold_GetParamsB` (`0x00283078`) | own: `Hold_GetParamsB`; the holder's: `Hold_GetParams` |
| State 0 | clears the mugger's result `+0x5b4` and `+0x5b5`, first angle | first angle, own result and `+0x5b5` cleared | the holder's result and `+0x5b5` cleared, first angle, and he says 8 `swear` (interrupting) |
| Reaching the required time `+0x04` | the mugger **fails** (his `+0x5b4` = 0): the victim has resisted | at each half of `+0x04` the game drops to state 1 (the stick must find the target again); at `+0x04` own result = 1: **cuffed** | the holder **fails** (his `+0x5b4` = 0): broken free |
| Off target past `+0x0c` | the mugger **succeeds** (his `+0x5b4` = 1) | own result 0: fails | the holder **succeeds** (his `+0x5b4` = 1) |
| Time limit `+0x134` (an AI opponent) | | past it, state 5 with the result as it is | past it, the holder succeeds |
| Other | | **L2** (pad mask 1, newly pressed) lets go at once (state 5); `+0x5b5` follows whether the stick is on target | |
| State 5 | rumble off; the mugging's end clips (`Mug_PlayEnd`, `0x00273110`, with the mugger's result); the mugger's `+0x5b6` = 0 | rumble off, own `+0x5b6` and `+0x5b5` = 0; the caller then cuffs or gets off | rumble off, the holder's `+0x5b6` = 0 |

So a mugging between two players is a race between two games: the mugger's mode 0 succeeds at his own required time,
the victim's mode 1 ends it in the victim's favour at the victim's required time, and a victim who stays off target
too long hands the mugger the win. The hold is the same race between holder (mode 2) and held (mode 3). The modes'
meter colours and prompts are on [HUD: the mug meter](hud-functions-panels.md#mug-meter-layout).

### The cuffing hold {#cuffing-hold}

Read in Ghidra: confirmed (code) where no other level is given; confirmed (runtime) where a block says so.

The hold a tackle leads to when the tackler has handcuffs: both sides kneel in the arrest loop while the
[stick games](#other-stick-games) of modes 2 (the holder) and 3 (the held player) race, and the holder either cuffs
the held human or lets him go.

**What starts it** (`Player_UpdateMounting` `0x0027ec20`, the dispatcher's route for state `0x400`, run for players
and AIs alike; confirmed (code)):

1. The tackler is in the mount (state `0x400`, idle 210 / 207), the victim is still tackled, and the tackler holds
   none of held flags `0x5c7eee1`.
2. The command is **R1 pressed** (command 3, the word at `0x0051098c`) or `0x31`, the command an AI's attack kind 41
   writes ([AI](ai.md)). Both are tested by `PlayerCmd_IsCommand31` (`0x0027bf98`). L1 + R1 (`0x1f`) is not it.
3. The tackler has a cuff (`Human_GetCuffCount` `0x00222908`):
    - a player: inventory item 5;
    - an AI of brain type 1 (police): always one;
    - any other AI: byte `+0x378`.
4. The victim is not a boss (category `+0x11b` 13, `Human_IsClass13` `0x00223e20`).

Then, when **both** humans are AIs, the tackler cuffs at once (`Cuff_Start`, below) with no hold. When either is a
player, `Tackle_StartHold(tackler, victim)` (`0x0022d030`) starts the hold. Nothing else gates it: no game type and no
gang, and any human with a cuff can hold. A cop who has tackled player 1 enters the hold the same way, through his
attack kind 41 (command `0x31`); `Cuff_Start` alone is only the AI-on-AI path. Confirmed (code), and confirmed
(runtime) both ways: a player with one cuff (given by `Human_AddCuffs` `0x00222980`) tackling PoizoCiv and pressing R1
in the mount, and PoizoCiv, given a cuff and command `0x31` in the mount, holding the player.

**`Tackle_StartHold`** (`0x0022d030`; confirmed (code)):

1. Clears `0x400` and `0x8000` on the holder and `0x800` and `0x8000` on the held. Sets **`0x8000000000`** on the
   holder and **`0x10000000000`** on the held.
2. Deals the held a zero-damage hit from the holder (`Human_DealDamage(held, 0, holder, 1, 1, 0)`; its purpose is
   open, [Open questions](#open-questions)).
3. Resets both records' stick games: `+0x128` = 0, `+0x12c` = 0, `+0x138` = 0, and `+0x130` = the game time
   (`*(0x0050b734) + 0x48`).
4. Sets the **time limits** in each player's own record:
    - the holder's `+0x134` = start + `Hold_GetParams` `+0x18`, only if the holder is a player;
    - the held's `+0x134` = the same start + `Hold_GetParamsB` `+0x18`, only if the held is a player.
5. For a **held player** only, starts his mini camera (`CamMini_Start` kind 0 on the held player's view, framing both,
   [Camera](camera.md)). A player holding an AI keeps his own camera.
6. `Pair_StartLoop256` (`0x002613a8`): the holder loops **256** `ANIM_ARRESTING_LOOP` (`arrest_minigame_cycle`) and
   the held **257** `ANIM_ARRESTING_LOOP_REACT`, each after a 0.05 s blend. Both push spheres go off and both anim
   states are set to 0. The pair link (`+0xc4`) is the tackle's own, kept as it was. So the pair leaves the mount
   pose: 210 / 207 give way to 256 / 257 on the next update (confirmed (runtime)).

`Human_ChooseAnimState` returns at once for a human in either hold state (`Human_IsInMugMeter` `0x002280f0`), so
nothing replaces the loops (confirmed (code)). The struggle clips 258 / 259 (`ANIM_ARRESTING_STRUGGLE`) are named by
no code, and none played in either run (confirmed (runtime)). The stick games move no clip.

**Each update** (`Player_UpdateHold` `0x00284140`, the dispatcher's route for `0x18000000000`; confirmed (code)):

- **An AI** (`+0x1b0` = −1) does nothing here: the route returns 1 and the update ends. An AI holder just loops 256
  until the held player's game ends the hold.
- **A player** runs his side: mode 2 (`Player_UpdateCuffHold`, holder) or mode 3 (`Player_UpdateMugHold`, held).
- When a player's game reports its end (returns 0), `Player_UpdateHold` calls `Tackle_EndHold(holder, held)` and reads
  the **holder's** result `+0x5b4`: 1 → `Cuff_Start(holder)`; 0 → `Mount_GetOff(holder)` (`0x00271470`). Two players
  run both games, and the first to end decides.

With an AI on the other side, the player's own `+0x134` ends his game:

- a **holding player** past it ends with his result as it stands: 0 unless he reached the required time, so the held
  gets away;
- a **held player** past it hands the AI holder the win, and he is cuffed.

The held player's off-target time never grows, because `Hold_GetParamsB`'s off-target rumble `+0x1c` is 255, not 0
([the rumble rule](#other-stick-games)). So he can lose only to the time limit (confirmed (code); and confirmed
(runtime): `+0x138` stayed 0, against 7.4 s for the holding player).

**`Tackle_EndHold`** (`0x0022d1f8`; confirmed (code)) clears `0x8000000000` / `0x10000000000` and gives back `0x400` /
`0x800` (the tackle). It zeroes both per-player bytes `+0x1c` and `+0x1d` (the pad's motors, inferred from the
rumble's use of `+0x1c`). When the held is a player whose view is in its mini mode (8), it sets that mini camera's done
flag (`+0x258`), and the view blends back over 0.3 s on its next update. A holding player had no mini camera to end
(confirmed (runtime)).

**The parameters** (confirmed (code)). `Hold_GetParams(holder, held)` (`0x00282b68`) serves the holder's game (mode 2)
and the holder's side of mode 3. `Hold_GetParamsB(holder, held)` (`0x00283078`) serves the held player. Each returns a
static record (`0x005109c0` / `0x005109e0`) when its `+0x04` is non-zero (nothing found writes them; both read zero),
else builds one in `0x006cdd78` / `0x006cdd98`. It builds one only when its subject (the holder for the first, the
held for the second) is a player, and otherwise returns the last one built. The record's layout is the mugging's
([the stick-game record](#data)). The choice is by the **player's Warrior class byte `+0x07`** (`CfgWarriorClass`'s
seventh value, 3 for Rembrandt's class 6 at runtime): the class's **cuffing** stat, whose only readers are these two
functions ([Warrior classes](../references/warrior-classes.md), the "Cuffing" column). `Hold_GetParams` also chooses
by the held human's category `+0x11b`: 5 and 6-7 are civilian groups (6 also the drunk Destroyers), 9 cops, 10 the
tougher cops (`cops_ty`), 14 the Warriors. Times are in ms.

`Hold_GetParams` (the holder), every set with bytes `+0x00`-`+0x02` = 160, 75, 255, off-target `+0x0c` = 50,000,
tolerances `+0x10` = 40° and `+0x14` = 60°, off-target rumble `+0x1c` = 0:

| Byte `+0x07` | Held category | Required `+0x04` | Period `+0x08` | Time limit `+0x18` |
| --- | --- | --- | --- | --- |
| 2 | 5, 9, 10 | 1,350 | 900 | 4,000 |
| 2 | 6, 7 | 1,000 | 500 | 8,000 |
| 2 | 14 | 2,000 | 1,000 | 4,000 |
| 2 | other | 1,000 | 500 | 5,000 |
| 3 | 5, 9, 10 | 2,000 | 700 | 6,000 |
| 3 | 6, 7 | 1,800 | 600 | 10,000 |
| 3 | 14 | 2,000 | 800 | 5,000 |
| 3 | other | 1,800 | 600 | 7,500 |
| other | 5, 9, 10 | 1,300 | 1,300 | 3,000 |
| other | 6, 7 | 1,000 | 1,000 | 6,000 |
| other | 14 | 2,000 | 1,000 | 5,000 |
| other | other | 1,000 | 1,000 | 4,000 |

`Hold_GetParamsB` (the held player), every set with bytes `+0x00`-`+0x02` = 75, 160, 0 (no rumble on target),
off-target `+0x0c` = 50,000, off-target rumble `+0x1c` = 255:

| Byte `+0x07` | Required `+0x04` | Period `+0x08` | Tolerances `+0x10` / `+0x14` | Time limit `+0x18` |
| --- | --- | --- | --- | --- |
| 2 | 2,000 | 800 | 90° / 91° | 5,000 |
| 3 | 1,750 | 500 | 90° / 91° | 4,500 |
| other | 2,000 | 1,000 | 80° / 81° | 4,000 |

At runtime the holding player against PoizoCiv (category 4) got a limit of start + 7,500, and the held player start +
4,500, both with byte `+0x07` = 3. Confirmed (runtime).

**How it ends.** Confirmed (code); the runs confirmed (runtime) both rows.

| Outcome | How | The holder plays | The held plays | After |
| --- | --- | --- | --- | --- |
| Cuffed | the holder's result 1: a holding player reached `+0x04`, or a held player's limit ran out | 252 `ARRESTING_INTRO`, 260 `ARRESTING_END`, 266 `ARRESTING_RISE` (`cop_arrest_*`), then his stance loop | 253, 261, 267, then the loop **320** `ARRESTED_IDLE` | `Cuff_Start` (`0x00260250`) first calls `Tackle_Mount` (`0x0022c548`): `0x400` / `0x800` off, both immovable, states `0x8000000` / `0x10000000`. Held flags `0x4000000` (holder) and `0x10000` (held). At 261's end (`Cuff_OnCuffedClipEnd` `0x00260178`) the held is **arrested** (`Human_Arrest` `0x0022ec18`: state **`0x20000`**, move style `0x11`; for a player, busted, [Knocked out](combat-reactions.md#defeat)), the holder **spends one cuff** (`Human_AddCuffs(−1)`), and a holding player scores style 12 (`Stats_AddStyle(…, 0xc, 1)`). At 266's end an AI holder (brain type 0) drops his target |
| Holder lost or let go | the holder's result 0: a held player reached his `+0x04`, a holding player's off-target time passed `+0x0c` or his limit ran out, or he pressed **L2** (pad mask 1, newly pressed) | 247 `MOUNT_BREAK_REACT` (`gen_mounting_escape`) | 246 `MOUNT_BREAK` (`gen_mounted_escape`) | `Mount_GetOff` (`0x00271470`): both back in the tackle states `0x400` / `0x800` (from `Tackle_EndHold`), held flag `0x2000` on both, 0.2 s blends, end hooks `0x00270f88` / `0x00270fc0`. A held human already down (`0x100000000`, knocked out or `0x80000000`) plays nothing. No cuff is spent |

The holder's win is always `Cuff_Start`, whichever side's game ended it. A held player "broken free" and a holding
player who "gets off" both end in `Mount_GetOff`, with the same clips. 244 / 245 (`MOUNT_RELEASE`) are the
teleport's pair break, not this. Confirmed (code). At runtime the held player was cuffed 135 updates (4.5 s) after the
hold began, with state `0x20000` from 261's end and the holder's cuffs 1 → 0, and the holding player was let go at
225 updates (7.5 s), with 247 / 246. Confirmed (runtime).

**A hit during the hold** (`Human_ApplyPendingDamage` → the reaction by state; confirmed (code)). Each handler zeroes
the struck human's pending damage `+0x118` (the health has already dropped, [Damage](combat-reactions.md#damage) step
4). Then it ends the hold (`Tackle_EndHold`, so the tackle states come back) with neither `Cuff_Start` nor
`Mount_GetOff`:

- **The holder hit** (`HitReact_WhileMounting` `0x00269ac0` → `HitReact_WhileTackling` `0x00269670`): from a third
  human, the tackle ends too (`Tackle_End`, the held gets state `0x80000`). The holder plays 216
  `MOUNTING_HIT_REACT_RIGHT` or 218 `MOUNTING_HIT_REACT_LEFT` by the side struck (0.1 s blend), and the held gets up
  (199 `GROUNDED_RISE`, `PairBreak_GetUp199`). Both bodies get a fresh shared body group (`+0x3c`), so they do not
  strike each other. Confirmed (code).
- **The held hit** (`HitReact_WhileMounted` `0x00269b18` → `HitReact_WhileTackled` `0x00269ab0`): the holder's
  pending damage is zeroed too, and **no clip** plays. The pair is left in the tackle states with the arrest loops
  still on their stacks (confirmed (code)), and the holder's mount update (`0x400`) runs again from the next update
  (inferred from the states).

## In Coney

Every place where Coney's fight plays differently is listed on [Combat: where Coney differs](combat-diffs.md); the
throw, the bat's cross and the mugging are below.

### The throw {#throw-direction-coney}

The throw lives in `repo:src/combat/grab.h` (`combat::updateGrab`, `combat::throwAttack`), `repo:src/combat/stick.h`
(`combat::sideOf`), `repo:src/human/fighter_grab.cpp` (`Fighter::turnThrow`, `Fighter::throwAfterSpin`,
`Fighter::alignWallThrow`), `repo:src/human/turn_and_slide.h` (`TurnAndSlide::turnToOver`) and
`repo:src/human/holdable.h` (`Holdable::carryOver`), fixed for issue #240 ([Where a throw goes](#throw-direction)).
The throw picks the side and the clip from the press's camera-turned stick, unsnapped, where exactly 45° is a side and
exactly 135° still a side. From the front hold (or with the `0x40000000` grab block) a plain throw starts both clips
on the press's update and turns the thrower to stick heading + side × 90° over 1/6 s, while the victim, let go, is
carried round him over the same 1/6 s (its offset from him turned by the same angle, `moveToOver`, and turned to face
him), on top of both clips' root motion. From the rear hold the spin 80 / 81 plays first and the throw's clip follows
with no turn; the victim is let go when the throw's clip begins, and the throw's hit is timed from it (its spin's
updates after the press). The place check applies only to the front hold's plain throws.

**Where Coney differs.**

- A head-on wall throw is aligned at once (`Fighter::alignWallThrow`), not over 0.01 s; another wall throw turns
  nothing, as in the original.
- The thrower's turn starts on the update after the press (Coney's steer, like an attack's, is applied in the next
  state update), so he reaches the wanted heading 5 updates after it; a passive target's carry starts on the press's
  update.
- The side clips turn the thrower about a third too far (149 −120° against −90° at runtime; rear −237°, left +119°),
  and the front victim lands 19.2° right of the stick (2.1° at runtime): both from the clips' own root rotation, not
  the aim ([Open questions](#open-questions), issue #263).

The disc test `repo:tests/sandbox/disc_throw_direction_test.cpp` (the fight yard, the stick at 0.9 and `d` 0°, ±20°,
±40°, −50°, −90°, 160° and 110°) and `repo:tests/human/throw_direction_test.cpp` (the same on synthetic clips) find
each side's throws landing at one point in the stick's frame within 4 cm and ending with one heading, and a front
thrower facing the stick (0.0°) 5 updates after the press. Before #240 the front throws landed from 20.6° left to
59.0° right of the stick.

### Cross with a bat in hand {#bat-cross-coney}

`repo:src/combat/player_combat.cpp`, `repo:src/combat/attacks.cpp` and `repo:src/human/human_animator.cpp`, fixed for
issue #228 ([the rule](#bat-cross)):

- The gates are the original's: Coney's cross acts on `0x10` (`kCrossLongHold`), refused under `0x100101f`
  (`kAttackRefusingPhases`) and dropped under `0x5c7fee0` (`kDispatchDroppingPhases`), with the clips' own phase
  events and lengths. Its chain (`AttackChain`) stores the cross press during 34's wind-up and, having no armed steps
  (`nextChainAttack` knows only 12, 11 and 16), plays nothing: the same result as the original for the bat. The
  disc test (`repo:tests/platform/disc_bat_cross_test.cpp`, level99 with a bat in hand, cross tapped 33-40 updates
  after a square and 31-38 after a cross) finds the first cross that swings at **k37 after 34 and k35 after 36**,
  and none before, as [the table](#bat-cross) has it.
- **34's length is a stand-in.** Coney's 34 (28 frames at 0.75) lasted 38 updates, so a cross on k37 was dropped.
  Coney ends that one clip a step early (`ClipThenNextTask`'s `handOverEarly`, set for anim id 34 in
  `HumanAnimator::clipThen`) so that 389 and the held flags clear on k37. **Coney stand-in**, now replaceable: the
  original ends **every** clip by one rule ([When a clip ends](formats/animation.md#clip-end)), the update its
  cursor's frame would reach the frame count *f* (the event rule with the tie, [Reading the
  tables](combat-moves.md#reading)), which gives 37 for 34, 35 for 36, 15 for 441 (a tie: 10.5 / 0.75 = 14, one late),
  16 for 69 and 26 for 19, all as measured. Coney's clip tasks end when the time reaches the duration (ceil(*f* /
  *r*)), one update late on 50 of the 174 clips of [the frame data](combat-moves.md#frame-data) (19, 34 and the dive 1
  among them).
- **Cross + square (`0x22`) and circle + cross (`0x23`) with an object in hand** do nothing in Coney, as in the
  original's `Player_Special`, which refuses a pad player holding an object: `PlayerCombat::special` returns for a
  player holding any object (`CombatInput::holdingObject`, whatever its anim set; until issue #860 it read the anim set
  instead, so a human no player controls was refused too, [The spray](combat/index.md#run-attacks)). Before, `0x22`
  played the special 653
  (spending a quarter of the power), whose phases refused the cross's own `0x10`, and `0x23` started a grab: the player
  saw a push, not a bat swing, and that was the likely cause of "cross sometimes does not start an attack". Now
  the cross's release plays 36 (47, 41 for a knife, a baton) on the next update. **Not built**: the original's
  throw of the melee weapon (502 / 491) for the `0x22` that has a free target ([Combat
  differences](combat-diffs.md#weapons)), so with nobody near Coney swings where the original throws.

### A bat's hit {#bat-knockdown-coney}

Coney's `level34` Furies swing only unarmed clips (11, 12, 13, 15, 16 in the trace of `fix-furies-passive`, for
issue #492), so their first hits are small reactions of 18-54 health where the original's are knock-downs of 90:
Coney gives a scene's bound props to no one ([Scripts: the props go into hands by clip
events](scripting.md#level80-arrest-fire), item 3; [the level34 fix list](scripting.md#level34-fixes), item 14).
The issue's two runs of the original (+276 and +294, reactions 292 and 294) fit [the opening](#bat-knockdown): a bat
run attack on arrival, about 270-330 updates after the scene's end. `repo:tests/platform/disc_level34_furies_test.cpp`
(checkpoint 5, no input) holds Coney's first hit on the player to after the Furies' turn and at most 500 updates
after the scene's end; it checks no bat, damage or reaction.

What Coney lacks for an armed AI, all of it missing today:

- the armed choice of clip for an AI with set 3: 501 on arrival at a run, 36 or 34 standing, and at a man down 37 or
  the unarmed 193 / 194 ([Open questions](#open-questions));
- an AI's bat hit dealt as an object contact by `ThrownObject_HitHuman`'s rules ([the bat's hit](#bat-knockdown)):
  `CfgObj` `+0x58` × the holder's power-class float (90 for class 26 with `dyn_bat`, 72 for class 5), negated against
  a friend, and the grounded 193 / 194 through `Strike_Contact` as unarmed hits (25);
- the reaction from the swing clip's hit code (`0x2a`: 292-295 and down; 195 for a man on the ground).

### Dropping what is held {#keep-weapon-coney}

The keep-weapon flag exists (`human::flag::kKeepWeapon`, set by `HuSetKeepWeapon`, the rage handlers and the boss
goals), but no drop reads it: none of the drops of [Dropping what is held when hit](#keep-weapon) is built (issue #91).
What the port needs, beside the flag, in the original's order:

1. On a landed, non-blocked, non-lethal hit (or any hit while airborne): drop the held object when the victim is in
   a throw clip with something in hand (the eleven ids of [step 1](#keep-weapon)), holds held flag `0x800000`, plays 602
   with set 1, or holds a kind 39 object; no flag test.
2. In the ordinary reaction: when the attacker's clip id is ≥ 620 and the victim lacks the flag, drop.
3. On a knock-down, and on a stun that is not refused (unstunnable, grab states `0xcf0`, held `0x10200`): drop set
   4 / 6; drop any other object when the victim is hurt, lacks the flag and is not a cop holding set 1 or 2.
4. When a human becomes the victim of a grab, mount, paired move or lethal reaction: drop set 4 / 6; drop any other
   when he lacks the flag, is not a cop holding set 1 or 2, and is hurt or holds set 3.
5. When a human starts a grab or tackle (and as attacker of a paired move, holding set 2 or 3): drop set 4 / 6, and
   any set but 1 or 2 without the flag.
6. On arrest: drop set 4 / 6, and anything else without the flag. On defeat: drop whatever is held.

**Tests.**

- Unit: a fighter with a bat at full health takes a stun hit and keeps it; at 20 % of a class with a 0.35 threshold
  he drops it; with the keep-weapon flag he keeps it; with a set 4 prop he drops it in all three; the same hurt
  victim as a cop with a baton keeps it; a hit by an attacker in clip 645 drops a bat at full health unless the flag
  is set; a hit during clip 502 drops the bat even with the flag.
- Disc (the player's path): `level99`, the rage lesson checkpoint, through `DiscSession`. Put a bat in the
  player's hand (`HuPlaceItemInHand`, as the scripts do), bring his health under the hurt threshold, start rage with
  L1 + R1 as a pad press, have a sparring Warrior land a stun hit and a special (clip ≥ 620): the bat stays in hand
  (`script().heldObject` unchanged) through both. End rage (meter drained) and repeat the stun hit: the bat falls.

### The mugging {#mug-difficulty-coney}

The stick game lives in `repo:src/combat/stick_games.h` (`MuggingGame`) and reads its record from `CombatTuning`
(`repo:src/combat/combat_tuning.h`); `SetInterrogateParam` is `makeSetInterrogateParam` in
`repo:src/scripting/human_bindings.cpp`.

**Where Coney differs** ([Mug difficulty](#mug-difficulty)): Coney runs every mugging with one record, `CombatTuning`'s
5000 / 2500 / 50000 (the record seen at runtime), and no victim limit. What the original does that Coney lacks:

1. A record built per mugging from the table, by the victim's `CfgChar` category and `flag_14b` and the player's
   Warrior class mugging byte (from the difficulty's `CfgWarriorClass`), with `SetInterrogateParam`'s override first
   when its required time is non-zero.
2. The victim's limit: the start stamped when the mugging proper begins (after 338 / 339), and the mugging failed
   (346 / 347, result 0) once game time passes start + `+0x18`, for an AI victim only.
3. The mug meter's bar 1 driven by (now − start) / (limit − start), as `Player_UpdateMugging` does.

The 50 s off-target allowance is the same for every victim, as Coney has it. The check that will show it: with
Rembrandt (byte 3 on normal) a category-15 victim fails at 5000 ms with the stick off target, and succeeds with the
stick on each new angle within the update it appears; a category-4 civilian with `flag_14b` clear gives 3000 / 1000 /
7000; `SetInterrogateParam(…, 20000 …)` gives a 20 s limit.

Coney's weapons never break, since what sends a weapon its message 1 is open ([Open questions](#open-questions)).

## Open questions

- **A throw's root turn**{#q-throw-root-turn}: the side clips turn the thrower about a third further in Coney than at
  runtime, and the front victim lands 19.2° right of the stick against 2.1° ([Where a throw goes, at
  runtime](#throw-runtime); [Combat: open questions](combat/index.md#open-questions)).
- **What sends a weapon message 1**{#q-weapon-break} (the break, `0x003fd600`): hits alone do not, at runtime ([A bat
  in hand](#bat)). Found from the callers that send message 1 to an object (the object message dispatch's
  cross-references), or a PCSX2 breakpoint on `0x003fd600` while a weapon is used.
- **The weapon branch of `Strike_Contact`**{#q-weapon-branch}: whether the player's bat, or the knife and the baton,
  ever take it, when the `level34` Furies' bat hits never did ([The held weapon in a run attack](#armed-moves)). A
  breakpoint on `Strike_Contact` (`0x0021b290`) during the player's armed swing would tell.
- **An armed AI's clip**{#q-armed-ai-clip}: what makes an AI with a bat pick 34 or 36 standing, and 37 or the unarmed
  193 / 194 at a man down ([A bat's hit](#bat-knockdown)). Coney's armed AI needs it. Found from the armed branch of
  the AI's attack pick, or by logging the Furies' clips against the target's state.
- **The bat's 35**{#q-bat-35}: when the bat's 35 plays at all, if no pad chain reaches it ([Cross with a bat in
  hand](#bat-cross)).
- **The inventory check for a type 44 object**{#q-pickup-type44} on pick-up (`0x0041e420`, `0x0041ded0`): which count
  it reads and what refuses the pick-up. Pick-up of that type depends on it. Found by decompiling both functions.
- **The armed branch's object attack**{#q-armed-object-attack} on a breakable with no human target (`0x00263eb8`,
  `Human_AttackObject` on [Combat](combat/index.md#code-index)), and what plays the knife's `KNIFE_ATTACK_FROM_RUN`
  (490) ([Moving attacks with something in hand](#armed-moves)). Found from the references to anim id 490 in the code
  and the scripts.
- **The mugging angle's frame**{#q-mug-angle-frame}, world or camera ([Mugging](#mugging)): Coney's stick game depends
  on it. Found by reading the angle `Player_UpdateMugging` compares against, or at runtime by turning the camera
  during a mugging.
- **Why `Tackle_StartHold` deals a zero-damage hit**{#q-hold-zero-hit} ([The cuffing hold](#cuffing-hold)): the hit's
  side effects (events, statistics, the brain's reaction) may matter to the AI. Found by following `Human_DealDamage`
  with a damage of 0.

## How this was checked

The code claims were read in Ghidra on `SLUS_212.15` (NTSC-U); the clip events, the Anim Range List values, the wall
clips' far ranges and the objects list were read from the disc's character and object data. The runtime claims were
read with PCSX2 2.9.94 over PINE, before run records existed, so none of them has one:

- **Where a throw goes**: the setup is beside the runs ([at runtime](#throw-runtime)).
- **A bat in hand** and **Moving attacks with something in hand**: copies of slot 6 (the civilian ahead) and slot 1
  (the street with nobody near), `dyn_bat_tuff` put in the hand by calling `Human_PlaceItemInHand(player,
  "dyn_bat_tuff")` on the game's thread; the hand's pose read every update through the idle, walk, swing, block and
  run of [World objects](objects.md#held). The pick-up ran in slot 1 with a bat 1 m ahead; its update counts were
  re-labelled one update later after a fix to the trace recorder ([Recording a trace](../guides/research-workflow.md#hooks)).
  The event of clip 461 was read from Rembrandt's clip in memory.
- **Weapons**: copies of slot 6 with the puppet PoizoCiv, driven over PINE with the scripted pad and the right-stick
  and puppet patches; the scenarios are `repo:research/traces/scenarios/moves_knife_*.toml` and `moves_bottle_*.toml`.
- **Cross with a bat in hand**: the setup is beside the runs ([Cross with a bat in hand](#bat-cross)); the scenarios
  are `repo:research/traces/scenarios/bat_cross_*.toml`.
- **A bat's hit**: 2026-10-13, a copy of slot 7 (`level99`) with `level34` checkpoint 5 loaded through mode 8, a state
  saved 9 ticks into the level, then `repo:research/traces/scenarios/l34_furies_first_hits.toml` recorded from it
  (the recorded run, rec1) and a second run polling the hands and clips over PINE (the polled run). `ParkFury14` was
  human `0x00646510` in the recorded run. The `CfgChar` and `CfgObj` values read over PINE in a first run were not
  kept.
- **Mugging** and **The cuffing hold**: copies of slot 6 with the puppet PoizoCiv; for the hold, a cuff given with
  `Human_AddCuffs`, and command `0x31` written for PoizoCiv in the mount.
