# Combat: throws, the bat and mugging

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 as on [Combat](combat/index.md), which this page was split from because that page had grown too
large. The overview, the data tables and the code index stay on [Combat](combat/index.md#code-index).
Every claim keeps the evidence level it had there.

## Purpose

What happens when the player throws a human he holds (where the throw goes, a wall throw, what the flying body
hits), what a bat in hand changes (picking it up, its swings, the moving attacks while armed) and how the mugging
works (its stick game, who is hard to mug, the cuffing hold). The grab and the hold the throw and the mugging start
from are on [Combat: grabs](combat-grabs.md); damage and reactions on [Combat: reactions](combat-reactions.md).

## Original structure

The throw is `Player_Throw` with the paired-move code it starts; a bat is the `melee_weapon` object's message
handler, the human's pick-up clip and the dispatcher's armed branch; the mugging is `Player_UpdateMugging` with its
parameter table and the tackle and cuffing holds. `Player_Throw` and `Player_UpdateMugging` are in
[Combat's Original structure](combat/index.md#original-structure), the helpers in [its code
index](combat/index.md#code-index); the behaviour below names the function each step lives in, with its address and
level, the weapon's handler and the pick-up clip among them.

## Data

The move table, the Anim Range List and the hit codes are on [Combat](combat/index.md#data). This page's own values
(the wall clips' far ranges, a throw's power cost, the mugging record's times and angles) sit with the behaviour
that uses them.

## Behaviour

Each subsection below grades its own claims.

### Throws {#throws}

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

**What the thrown body hits** (confirmed (code) at `0x0021b290`, `0x00264bd8`, `0x00265f70`; the clip data read
from the disc). The plain throws' victim clips switch the victim's own strike shapes on: event `0x13`
(`Human_StrikeAllOn`: all ten bone shapes, and flag `0x2` on the 0.35 m capsule) to `0x14`, at frames 0-32 for 148
`THROW_01_FROM_GRAB_FRONT_REACT` (`gen_grab_front_throw_front_re`), 7-22 for 152 rear, 13-18 for 150 right and
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
events read from the disc). The victim clips 156-162 carry no `0x13` window, and their 0 damage and 0 hit code leave
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

#### Where a throw goes {#throw-direction}

How the original turns the stick into the thrown body's heading (issue #240). Confirmed (code) at `0x0026dd08`
(`Player_Throw`), `0x00146078` (`PlayerRecord_Update`), `0x0023cf88` (`Human_TurnToOver`), `0x00262ac8`
(`Attack_StartPaired`) and `0x00263380` (`Grab_StartPairedMove`), and confirmed (runtime) where marked. Headings below
are Coney's (0 faces +y, anticlockwise); the **stick heading** is the heading the camera-turned stick points along
(its angle − 90°).

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
   and both kept turning the thrower afterwards (confirmed (runtime), below).
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

**At runtime** (confirmed (runtime), PCSX2 2.9.94, a copy of slot 6 with the `scripted-pad`, `right-stick` and
`puppet` patches; scenario `repo:research/traces/scenarios/throw_direction.toml` for one of them: PoizoCiv placed
1.0 m ahead and grabbed with circle, power set to 400, then the stick and circle on the same update at stick magnitude
0.9, so the hold walk (above 0.95) never moved the pair first). 21 throws, the stick between 0° and 180° from the
camera's up and the player's facing turned between −170° and +160° from the throw's heading. Positions are in the
**stick's frame**: metres right of and along the stick heading, from the thrower's position on the update before the
throw.

| Side (clip) | Throws (clear) | Thrower's heading at the end − stick heading | Victim 5 updates in | Victim lands (196) |
| --- | --- | --- | --- | --- |
| front hold, front (148) | 6 (`d` −44° to +40°) | 0.0° | (0.31, 1.01-1.06), whatever `d` | (0.24-0.25, 6.84-6.88), 48 updates after |
| front hold, right (150) | 2 (`d` −51°, −46°) | −0.2° | (−1.20, 0.38) | (−0.44, 6.46) and (−0.51, 7.45), 45 updates after |
| front hold, rear (152) | 2 (`d` 160°, 166°) | +1.2° | (−0.29 to −0.32, −1.46) | (−0.51, 5.73) and (−0.56, 6.17), 44 updates after |
| front hold, left (154) | 1 (`d` 114°) | −3.5° | (0.94, −0.36) | (0.00, 5.65), 43 updates after |

- The thrower always ended facing the stick heading: the side clips turn him by about −90° (right), 180° (rear) and
  +86.5° (left) after the 1/6 s turn put the side's axis on the stick. These are the clips' root turns **times their
  rate** (0.75): the sampler scales bone 0's turn by the task's rate as it does the root velocity (confirmed (code) at
  `0x00104ce0`; the disc's 149, 151 and 153 sum to −89.9°, −180.0° and +86.4° that way, and to −119.9°, −240.0° and
  +115.1° without it; [Root motion](formats/animation.md#root-motion), #263).
- The landing point is the same in the stick's frame whatever the stick's angle from the facing (the front's six
  throws, `d` from −44° to +40°, landed within 4 cm of each other), so the turn takes the whole difference. Seen
  from where the thrower stood, the body lands within 5° of the stick: 2.1° right of it for the front throw, 3.9°
  and 5.1° to the left for the right and rear throws, 0° for the left one (the clips' own sideways drift, up to
  0.56 m). The victim's displacement measured from **its own** starting point differs from the stick by up to 7° from
  the front, because its start is rotated round the thrower first; measure from the thrower.
- Throws that met something in the street at 3-4 m (not identified) landed short (3.0-4.3 m instead of 5.5-7 m) and
  up to 23° off: collision, not aim. They are left out of the table.
- **Rear hold** (four throws where the puppet stood behind the player and the grab became a rear hold, 84 / 85):
  each played 80, then the throw at 17 updates after the press, with the thrower's heading unchanged until the
  throw clip began. The bodies landed along the side's body axis (−0.2° rear, −0.6° right, −11.6° left, from the
  thrower), 13° to 38° off the stick.

So in the original a throw from the front hold goes exactly where the stick points, give or take the clip's few
degrees, wherever the stick is within the side's 90° sector; only a rear-hold throw is limited to the four body axes.

**In Coney** (#240; `combat::updateGrab`, `Fighter::turnThrow`, `Fighter::throwAfterSpin`): the throw picks the side and
the clip from the press's camera-turned stick, unsnapped (`combat::sideOf`, `combat::throwAttack`), where exactly 45°
is a side and exactly 135° still a side. From the front hold (or with the `0x40000000` grab block) a plain throw starts
both clips on the press's update and turns the thrower to stick heading + side × 90° over 1/6 s
(`TurnAndSlide::turnToOver`), while the victim, let go, is carried round him over the same 1/6 s
(`Holdable::carryOver`: its offset from him turned by the same angle, `moveToOver`, and turned to face him), on top of
both clips' root motion. A head-on wall throw is aligned at once (`Fighter::alignWallThrow`, not over 0.01 s); another
wall throw turns nothing. From the rear hold the spin 80 / 81 plays first and the throw's clip follows with no turn;
the victim is let go when the throw's clip begins, and the throw's hit is timed from it (its spin's updates after the
press). The place check applies only to the front hold's plain throws. **Coney's readings**: the thrower's turn starts
on the update after the press (Coney's steer, like an attack's, is applied in the next state update), so he reaches
the wanted heading 5 updates after it; a passive target's carry starts on the press's update.

On the disc (`tests/sandbox/disc_throw_direction_test.cpp`, the fight yard, the stick at 0.9 and `d` 0°, ±20°, ±40°,
−50°, −90°, 160° and 110°; `tests/human/throw_direction_test.cpp` does the same on synthetic clips): each side's
throws land at one point in the stick's frame within 4 cm and end with one heading, and a front thrower
faces the stick (0.0°) 5 updates after the press. Before the fix the front throws landed from 20.6° left to 59.0°
right of the stick. Two differences remain, both from the clips' own root rotation, not the aim: the side clips turn
the thrower about a third too far (149 −120° against −90° at runtime; rear −237°, left +119°), and the front victim
lands 19.2° right of the stick (2.1° at runtime). See the open question on the root turn and the task's rate
([Open questions](combat/index.md#open-questions)).

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
triangle's pick-up search takes it ([Breakables](combat-targets.md#breakables) has the search; a script's prompt on it
comes first, [Crimes: triangle](crimes.md#triangle)). Confirmed (code) at `0x0024d810`, `0x003fead0`, `0x0023bf00`:

1. A type other than 12 (`TYPE_SPECIAL`) or 24 is taken by a player only with empty hands (human `+0x338` Nil). With
   something in hand the search instead sets human `+0x5b8` = 1 and drops the held object (`0x00257f38`).
2. The winner's record is pinned and it gets message 0. The weapon's handler (`MeleeWeapon_OnMessage`,
   `0x003fead0`; confirmed (code)), while its state (data
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
later, and the hand held the bat in that update's sample. Confirmed (runtime) (re-labelled one update later after the
recorder fix, [Recording a trace](../guides/research-workflow.md#hooks)). `Human_PlaceItemInHand` (`0x00238540`,
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
or more, and the unarmed walk attack also needs no fight stance ([Attacks](combat/index.md#attacks)); for the throws a stance
only turns the walking throw (gait 2) into the standing one.

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
button; what plays it is not traced. For sets 4-6 the dispatcher sends square and cross to the throw (`0x002880d8`) and
never reaches `Player_Square`; the charge and dive commands do the same. The throw's gait test differs from the run
attack's: gait 3 already counts as a run and `+0x08` is not tested. Set 5 first tries a **smash** on a human in reach
(473 from the front, 475 from behind, `bottle_smash_attacker`; not with a molotov, `TYPE_MOLOTOV` 8), and the throw
turns a held `TYPE_KNIFE` (11) into set 5 (pop, then push 5) when it is called with one; neither path was followed
further.

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
the holder's class float with no clip damage (confirmed (runtime), [Combat: a bat's
hit](combat-reactions.md#bat-knockdown); [Objects in a hand](objects.md#held) item 3). Whether the player's bat, or the
knife and the baton, ever take the `Strike_Contact` weapon branch is open. Because
record `+0x08` has `0x1000000`, the hit takes the moving-attack branch that 24, 0 and 1 take (a breakable gets hit kind
2, [World objects](objects.md#door-break)). Rage: 501 is **event 1 × 1 = 5** (2 blocked) and kind 4, so never halved by
repeats ([Rage](combat-rage.md#rage)), against the bat's square 34 at 2 and its cross 36 at 11, and the unarmed 24 at 1.
The weapon is not worn: only its message 1 breaks it ([Losing it](#bat)). With an AI holding `dyn_bat`, 501
dealt the bat's 90 (class float 1.0), hit code `0x2a`, and the victim played 292-295 and went down
([Combat: a bat's hit](combat-reactions.md#bat-knockdown)); the player's own 501 was not measured.

**Losing it.** Triangle with nothing to take drops the held bat at once, with no clip (`0x00257f38`, falling from the
hand under physics: [Drop](objects.md#held)); confirmed (runtime). For a player that standing drop comes from the
dispatcher's last step, after the climb, context action and jump tries
([step 6](characters.md#triangle-held)). A weapon **breaks** only on its message 1
(`0x003fd600`): shatter particles, hidden (`0x100000`), message 7 to its holder (data `+0x18`; the human's message 7
pops sets 1-5), and state −5 deletes it on its next update. Confirmed (code); what sends message 1 is not traced. At
runtime 12 bat hits on a civilian (64 damage each, 34 and 37 alternating) never broke it and its handler saw no message
1, so hits alone do not wear a bat out. A weapon's wear is the hit counter `+0x10d` / `+0x10e`, one less on each `use`
event; `+0x128` is not its hitpoints ([Combat moves](combat-moves.md#weapons)).

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
to the **off-target time** (record `+0x138`) while the rumble value is 0 and resets it to 0 while it is non-zero
but not the on-target value; with the usual off-target rumble of 0 (`+0x1c`) it is therefore never reset, and
`+0x0c` is the total time the player may spend off target before the mugging fails (5); in 2, each update on target adds
to the progress (`+0x12c`), a new target
angle comes each time the progress passes a multiple of `+0x08`, the victim's lines come at half of `+0x04`, and
reaching `+0x04` succeeds; leaving the target goes through 3 back to 4. "On target" is the stick above 0.5 and within
`+0x10` of the angle; the pad's rumble gets byte `+0x02` on target and byte `+0x1c` otherwise (player record `+0x1c`,
inferred: the motor). **Against an AI victim** (victim `+0x1b0` = −1) every update also checks the victim's time
limit: once game time passes record `+0x134` the game goes to 5 with the result 0, a failure (the victim has broken
free), and bar 1 of the mug meter shows the time since the start over that limit ([Mug
difficulty](#mug-difficulty)). An earlier reading had this test for a mugger no player controls; it is the victim's
side that decides it (`0x002856b8`, `uStack_b8` = the victim is a player).

**`SetInterrogateParam`** (`0x002854b0`) overrides the record for every mugging while its `timeA` is non-zero (set
0-2 at `0x00510a18`, read by `0x00284ca0`; sets 3-5 at `0x00510a38` serve a player victim, `0x002853a8`). Its
arguments fill, in order: bytes `+0x00`, `+0x01`, `+0x02` (the on-target rumble), `+0x04` required, `+0x08` period,
`+0x0c` off-target allowance, `+0x10` tolerance (degrees, stored in radians), `+0x14` the re-roll gap (degrees),
`+0x18` the victim's **time limit** (read once, when the mugging starts, [below](#mug-difficulty)), `+0x1c` the
off-target rumble. `level99_lesson1`'s mugging lesson passes
(160, 75, 255, 5000, 2500, 20000, 40, 60, 20000, 0, set 0): **5 s on target, a new angle every 2.5 s of it, 20 s off
target allowed, 40° of tolerance, each new angle at least 80° from the last**; it passes all zeros after the lesson,
which gives the per-class defaults back. Confirmed (code); bytes `+0x00` and `+0x01` are read only by the hold's games
(below).

#### Mug difficulty: who is hard to mug {#mug-difficulty}

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
    ([characters reference](../references/characters.md): category 15 is exactly the brain-kind-5 dealer types). The
    Warrior classes' bytes: on normal Ajax, Snow and Swan 1; Cleon, Cochise, Cowboy and Vermin 2; Fox and Rembrandt 3
    (every class 2 on easy; [Warrior classes](../references/warrior-classes.md) for hard).
3. **What decides it.** The mugger wins on reaching the required time on target; against an AI victim he loses once
   the victim's limit has passed since the start, whatever the progress (the off-target allowance of 50 s never comes
   first). So the **slack** is the limit minus the required time, and the target angle jumps (at least 80° away) each
   period of progress: a dealer gives **1.75-2.25 s of slack and a new angle every 0.75 s** (three to four jumps to
   catch in 2.75 s), a civilian with byte 2 gives 6 s of slack and a jump every 1.25 s, and a bum 9 s. That is the
   "dealers are hard" of the user's report: the victim's bar (bar 1, amber) fills over 4.5 s.
4. **The other player as victim** (`+0x1b0` set) has no limit: the race is mode 1 against his own record
   (`Mug_GetPlayerVictimParams`, `0x002853a8`, [the other stick games](#other-stick-games)).
5. **The payout** is the same for all (the victim's money, × 1.5 for a dealer brain, [Crimes](crimes.md#mugging)).

**Coney fixes** (user item 27; Coney runs every mugging with one record, `CombatTuning`'s 5000 / 2500 / 50000, and
no victim limit):

1. Build the record per mugging from the table above, by the victim's `CfgChar` category and `flag_14b` and the
   player's Warrior class mugging byte (from the difficulty's `CfgWarriorClass`), keeping `SetInterrogateParam`'s
   override first when its required time is non-zero.
2. Add the victim's limit: stamp the start when the mugging proper begins (after 338 / 339) and fail the mugging
   (346 / 347, result 0) once game time passes start + `+0x18`, for an AI victim only.
3. Drive the mug meter's bar 1 with (now − start) / (limit − start), as `Player_UpdateMugging` does.
4. Keep the 50 s off-target allowance as it is; it is the same for every victim.
5. Tests: with Rembrandt (byte 3 on normal) a category-15 victim fails at 5000 ms with the stick off target, and
   succeeds with the stick on each new angle within the update it appears; a category-4 civilian with `flag_14b`
   clear gives 3000 / 1000 / 7000; `SetInterrogateParam(…, 20000 …)` gives a 20 s limit.

#### The other stick games: mug meter modes 1-3 {#other-stick-games}

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
required time.

**Rumble in the holds.** Modes 2 and 3 grade the stick: within `+0x10` of the target (and above 0.5) the on-target
byte `+0x02`; within the coarser `+0x14` but not `+0x10` a value between bytes `+0x00` (at the edge of `+0x10`) and
`+0x01` (at `+0x14`): `b1 + (b0 − b1) × (1 − d / tol14)`, with `d` the angle off; elsewhere `+0x1c`. Since that middle
value is not 0, being **near** the target resets the off-target time in the holds. Modes 0 and 1 have no middle band.

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

#### The cuffing hold {#cuffing-hold}

The hold a tackle leads to when the tackler has handcuffs: both sides kneel in the arrest loop while the
[stick games](#other-stick-games) of modes 2 (the holder) and 3 (the held player) race, and the holder either cuffs
the held human or lets him go. Confirmed (code) at the cited addresses; confirmed (runtime) where marked.

**What starts it** (`Player_UpdateMounting` `0x0027ec20`, the dispatcher's route for state `0x400`, run for players
and AIs alike):

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
attack kind 41 (command `0x31`); `Cuff_Start` alone is only the AI-on-AI path. Confirmed (runtime) both ways in slot 6
copies: a player with one cuff (given by `Human_AddCuffs` `0x00222980`) tackling PoizoCiv and pressing R1 in the mount,
and PoizoCiv, given a cuff and command `0x31` in the mount, holding the player.

**`Tackle_StartHold`** (`0x0022d030`):

1. Clears `0x400` and `0x8000` on the holder and `0x800` and `0x8000` on the held. Sets **`0x8000000000`** on the
   holder and **`0x10000000000`** on the held.
2. Deals the held a zero-damage hit from the holder (`Human_DealDamage(held, 0, holder, 1, 1, 0)`; its purpose is
   not traced).
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
nothing replaces the loops. The struggle clips 258 / 259 (`ANIM_ARRESTING_STRUGGLE`) are named by no code, and none
played in either run. The stick games move no clip.

**Each update** (`Player_UpdateHold` `0x00284140`, the dispatcher's route for `0x18000000000`):

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
([the rumble rule](#other-stick-games)). So he can lose only to the time limit (confirmed (runtime): `+0x138` stayed
0, against 7.4 s for the holding player).

**`Tackle_EndHold`** (`0x0022d1f8`) clears `0x8000000000` / `0x10000000000` and gives back `0x400` / `0x800` (the
tackle). It zeroes both per-player bytes `+0x1c` and `+0x1d` (the pad's motors, inferred). When the held is a player
whose view is in its mini mode (8), it sets that mini camera's done flag (`+0x258`), and the view blends back over
0.3 s on its next update. A holding player had no mini camera to end.

**The parameters.** `Hold_GetParams(holder, held)` (`0x00282b68`) serves the holder's game (mode 2) and the holder's
side of mode 3. `Hold_GetParamsB(holder, held)` (`0x00283078`) serves the held player. Each returns a static record
(`0x005109c0` / `0x005109e0`) when its `+0x04` is non-zero (nothing found writes them; both read zero), else builds
one in `0x006cdd78` / `0x006cdd98`. It builds one only when its subject (the holder for the first, the held for the
second) is a player, and otherwise returns the last one built. The record's layout is the mugging's
([Mugging](#mugging)). The choice is by the **player's Warrior class byte `+0x07`** (`CfgWarriorClass`'s seventh
value, 3 for Rembrandt's class 6 at runtime): the class's **cuffing** stat, whose only readers are these two
functions ([Warrior classes](../references/warrior-classes.md), the "Cuffing" column). `Hold_GetParams` also
chooses by the held human's category `+0x11b`: 5 and 6-7 are civilian groups (6 also the drunk
Destroyers), 9 cops, 10 the tougher cops (`cops_ty`), 14 the Warriors. Times are in ms.

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

Confirmed (runtime): the holding player against PoizoCiv (category 4) got a limit of start + 7,500, and the held
player start + 4,500, both with byte `+0x07` = 3.

**How it ends.** Confirmed (code); the runs confirmed (runtime) the first and fourth rows.

| Outcome | How | The holder plays | The held plays | After |
| --- | --- | --- | --- | --- |
| Cuffed | the holder's result 1: a holding player reached `+0x04`, or a held player's limit ran out | 252 `ARRESTING_INTRO`, 260 `ARRESTING_END`, 266 `ARRESTING_RISE` (`cop_arrest_*`), then his stance loop | 253, 261, 267, then the loop **320** `ARRESTED_IDLE` | `Cuff_Start` (`0x00260250`) first calls `Tackle_Mount` (`0x0022c548`): `0x400` / `0x800` off, both immovable, states `0x8000000` / `0x10000000`. Held flags `0x4000000` (holder) and `0x10000` (held). At 261's end (`Cuff_OnCuffedClipEnd` `0x00260178`) the held is **arrested** (`Human_Arrest` `0x0022ec18`: state **`0x20000`**, move style `0x11`; for a player, busted, [Knocked out](combat-reactions.md#defeat)), the holder **spends one cuff** (`Human_AddCuffs(−1)`), and a holding player scores style 12 (`Stats_AddStyle(…, 0xc, 1)`). At 266's end an AI holder (brain type 0) drops his target |
| Holder lost or let go | the holder's result 0: a held player reached his `+0x04`, a holding player's off-target time passed `+0x0c` or his limit ran out, or he pressed **L2** (pad mask 1, newly pressed) | 247 `MOUNT_BREAK_REACT` (`gen_mounting_escape`) | 246 `MOUNT_BREAK` (`gen_mounted_escape`) | `Mount_GetOff` (`0x00271470`): both back in the tackle states `0x400` / `0x800` (from `Tackle_EndHold`), held flag `0x2000` on both, 0.2 s blends, end hooks `0x00270f88` / `0x00270fc0`. A held human already down (`0x100000000`, knocked out or `0x80000000`) plays nothing. No cuff is spent |

The holder's win is always `Cuff_Start`, whichever side's game ended it. A held player "broken free" and a holding
player who "gets off" both end in `Mount_GetOff`, with the same clips. 244 / 245 (`MOUNT_RELEASE`) are the
teleport's pair break, not this. Confirmed (runtime): the held player cuffed 135 updates (4.5 s) after the hold began,
with state `0x20000` from 261's end and the holder's cuffs 1 → 0. The holding player was let go at 225 updates
(7.5 s), with 247 / 246.

**A hit during the hold** (`Human_ApplyPendingDamage` → the reaction by state). Each handler zeroes the struck
human's pending damage `+0x118` (the health has already dropped, [Damage](combat-reactions.md#damage) step 4). Then it
ends the hold (`Tackle_EndHold`, so the tackle states come back) with neither `Cuff_Start` nor `Mount_GetOff`. Confirmed
(code):

- **The holder hit** (`HitReact_WhileMounting` `0x00269ac0` → `HitReact_WhileTackling` `0x00269670`): from a third
  human, the tackle ends too (`Tackle_End`, the held gets state `0x80000`). The holder plays 216
  `MOUNTING_HIT_REACT_RIGHT` or 218 `MOUNTING_HIT_REACT_LEFT` by the side struck (0.1 s blend), and the held gets up
  (199 `GROUNDED_RISE`, `PairBreak_GetUp199`). Both bodies get a fresh shared body group (`+0x3c`), so they do not
  strike each other.
- **The held hit** (`HitReact_WhileMounted` `0x00269b18` → `HitReact_WhileTackled` `0x00269ab0`): the holder's
  pending damage is zeroed too, and **no clip** plays. The pair is left in the tackle states with the arrest loops
  still on their stacks, and the holder's mount update (`0x400`) runs again from the next update (inferred from the
  states).

## Coney's implementation

Coney's side is written beside the behaviour it follows: the throw's side, turn and spin in
[Where a throw goes](#throw-direction) (`combat::updateGrab`, `Fighter::turnThrow`, `Fighter::throwAfterSpin`), and
the mugging's single record and the fixes it needs in [Mug difficulty](#mug-difficulty). Every place where Coney's
fight plays differently is listed on [Combat: where Coney differs](combat-diffs.md).

## Open questions

- **A throw's root turn**: the side clips turn the thrower about a third further in Coney than at runtime, and the
  front victim lands 19.2° right of the stick against 2.1° ([Where a throw goes](#throw-direction);
  [Combat: open questions](combat/index.md#open-questions)).
- **What sends a weapon message 1** (the break, `0x003fd600`): hits alone do not, at runtime ([A bat in hand](#bat)).
  Coney's weapons never break for want of it. Found from the callers that send message 1 to an object (the
  object message dispatch's cross-references), or a PCSX2 breakpoint on `0x003fd600` while a weapon is used.
- **The inventory check for a type 44 object** on pick-up (`0x0041e420`, `0x0041ded0`): which count it reads and
  what refuses the pick-up. Pick-up of that type depends on it. Found by decompiling both functions.
- **The armed branch's object attack** on a breakable with no human target (`0x00263eb8`, `Human_AttackObject` on
  [Combat](combat/index.md#code-index)), and what plays the knife's `KNIFE_ATTACK_FROM_RUN` (490)
  ([Moving attacks with something in hand](#armed-moves)). Found from the references to anim id 490 in the code and
  the scripts.
- **The mugging angle's frame**, world or camera ([Mugging](#mugging)): Coney's stick game depends on it. Found by
  reading the angle `Player_UpdateMugging` compares against, or at runtime by turning the camera during a mugging.
- **Why `Tackle_StartHold` deals a zero-damage hit** ([The cuffing hold](#cuffing-hold)): the hit's side effects
  (events, statistics, the brain's reaction) may matter to the AI. Found by following `Human_DealDamage` with a
  damage of 0.
