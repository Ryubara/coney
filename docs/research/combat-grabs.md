# Combat: grabs, tackles, the hold and the mount

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 as on [Combat](combat.md), which this page was split from because that page had grown too
large. The overview, the data tables and the code index stay on [Combat](combat.md#code-index).
Every claim keeps the evidence level it had there.

This page covers the grab and the tackle, what the grabber and the grabbed can do in the hold, the strong grapple, how a
grab is posed and moved, the mount and breaking a pair.

## Grab and tackle {#grab}

**Circle tapped** (released within 6 samples, command `0xd`) grabs; **circle held** for 7 samples (`0xe`) tackles.
`Player_GrabOrTackle` (`0x00284920`), confirmed (code):

1. Refused while record `+0x08` has any of `0xfc7eaf7`, or while human `+0x1be` is -1 with `+0x1c0` pointing at
   `Human_MoveThrowAim` (`0x00244770`, aiming a throw).
2. Without a target, it searches with `0x0027ac30(far × 1.25)`: for a tackle the far range of id 3 (2.999 m, so
   3.75 m), for a grab that of id 70 `GRAB_INTRO` (2.499 m, so 3.12 m).
3. It clears the buffered chain, enters a fight stance if not in one (`0x00280068`), then: a grounded target is mounted
   (`0x00271b30`); a target already grabbable from behind (`0x00258e10`) starts a [tandem](combat-moves.md#tandem)
   (`Tandem_Start`, `0x0026f860`); a tackle starts `0x002707a8(h, 3, 5)`; a grab starts `0x0026c548(h, 0, 0x48)`.

At runtime (confirmed (runtime)):

- **Grab**, victim 1 m ahead: 71 `GRAB_PLAYER_INTRO`, then 72 `GRAB_FRONT_END`, then the hold 82; the victim plays 73,
  then 83. Player state `0x45`, victim `0x10`. With nobody in reach: 71, then 69 `GRAB_MISS`, then 389.
- **Tackle**, victim 2.6 m ahead, circle held 0.55 s: the command `0xe` came on the 7th sample (0.23 s), the player
  played 4 `TACKLE_PLAYER_INTRO` covering the distance, then 5 `TACKLE_HIT_FROM_FRONT` with the victim on 6, state
  `0x405` / `0x801`; after 1.4 s the player sat in 210 `MOUNTING_IDLE` on the victim's 207 `MOUNTED_IDLE`
  ([The mount](#mount)).

## In the grab {#grabbing}

`Player_UpdateGrabbing` (`0x0027f3b0`), confirmed (code); runtime where marked:

| Input | Effect |
| --- | --- |
| square | 51 or 53 at random (victim 52 / 54), 57 damage; confirmed (runtime) |
| cross | 55, 57 damage; confirmed (runtime) |
| hold cross, press square (`0x22`) | the **power strike**: anim id 57 (anim 63 in rage, which also deals anim 231's damage at once); from the rear, anim 80 first spins the victim to the front; confirmed (runtime) for 57 |
| circle + cross (`0x23`) | anim 63, with the same rear spin, **for an AI grabber only** (human `+0x1b0` = -1); a player's grab has no `0x23` branch |
| square or cross during the power strike's window (`0x2`) | the **extended power move**: the next part, id + 2 (57 → 59, 63 → 65), up to two extensions (`Player_UpdatePowerMove`, `0x0027df38`, record `+0xbc` < 2; 61 is a filler clip); confirmed (runtime) for 59 |
| triangle (`0xa`) | **mug** the victim if it qualifies (`0x00225ff0`); confirmed (runtime) |
| circle (`0x1e`, `0xd` or `0xe`) with the stick above 0.25 | **throw** ([Throws](combat-throws.md#throws)); confirmed (runtime) |
| circle without the stick | from the front the **mount** ([The mount](#mount)); from the rear the spin to the front; confirmed (runtime) |
| R1 pressed (3) or `0x19` | **spin**: front → rear 78 / 79 (`0x0026d570`), rear → front 80 / 81 (`0x0026d998`); confirmed (runtime): 78 / 79, then the rear hold 84 / 85, states `0x85` / `0x20` |
| L2 held (5) | **lets go**: player 95, victim 94; confirmed (runtime) |

The commands are those of the [table](combat.md#commands): square acts on its press (`0xf`), cross on its release (`0x10`),
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

### Rembrandt's power move and its spray can {#power-move-props}

Issue #223 asked whether level99's lesson 6 gives Rembrandt a spray can. It does not, and it needs none: the power
move is the same for every Warrior class's anim ids, and the clips decide what it looks like.

- **Which finisher.** The power strike is always anim id **57**, and its extension is **59**
  ([In the grab](#grabbing)). No inventory, spray-paint count or held object is read on that path
  (`Player_UpdatePowerMove` `0x0027df38`, confirmed (code); `Player_UpdateGrabbing` `0x0027f3b0` as
  [In the grab](#grabbing) describes it, inferred). Each
  character's data maps the id to a clip, and its own slot wins ([Anim ids](../references/anim-ids.md#anim-57)):
  Rembrandt's 57 / 58 / 59 / 60 are `gen_grab_front_power10_p1` / `_p1_rea` / `_p2` / `_p2_rea`, and the generic
  set's are `gen_grab_front_power23_*`. So the spray finisher is Rembrandt's own 59, with or without paint.
- **The can comes from the clip.** `gen_grab_front_power10_p2` (70 frames, played at rate 0.75, so 94 updates)
  carries these events, read from the disc (numbers only):

  | Frame (update) | Event | What it does |
  | --- | --- | --- |
  | 0 (0) | `0x30`, the name `s_spraycan` at `+8` | **creates the can**: `dyn_` + name is looked up (`ObjectDb_FindByName`); for a type of kind (`+0x65`) 5, with no prop at human `+0x348`, `Human_CreateCarriedProp` (`0x0024cbb0`) makes the object at the human and attaches it at point `0x13` with its type's offset (`Human_AttachCarriedProp` `0x0024cd30`); both `+0x348` and `+0x34c` then hold its handle |
  | 0 (0) | 11, `SA` 146 `sprayface` | the speech command `sprayface` ([Sound events](sound-events.md#command-ids)) |
  | 1 (1) | `0x37`, word 19 | message `0xb7`: moves the prop to attach point 19 at the event's position (× the human's scale) and rotation |
  | 14 (19) | 11, `SA` 83 `sprayloop`; `0x21` on bone 19 | starts the **spray sound** ([Sound events](sound-events.md#spray-loop)); a named effect at the can |
  | 17, 20 | `0x21` on bone 19 | more spray effect |
  | 27 (36) | `0x45` | message `0xc5`: **stops the spray sound** |
  | 64 (85) | `0x31`, the name `s_spraycan` | **removes the can**: when the prop at `+0x348` is of that type, `Human_ReleaseCarriedProps` (`0x0024ce40`) destroys it and empties both handles |

  `gen_grab_front_power10_p1` (57) has no prop or spray event. A type of kind 6 is ignored by both events; any other
  kind is put in the hand instead (`Human_PlaceItemInHand`) and dropped by `0x31` when it is the held object.
  Confirmed (code) at `Anim_FireFrameEvents` (`0x00101dd8`, the cases `0x30`, `0x31`, `0x37` and `0x45`) and
  `Human_HandleMessage` (`0x00245920`, cases `0xb7` and `0xc5`).
- **At runtime** (confirmed (runtime), PCSX2 2.9.94, two plays of level99 checkpoint 1 from the `l99-cp1-lesson1`
  state with the course of `repo:research/traces/missions/level99_cp1.toml`, hooks `prop-create` and `prop-release`,
  Rembrandt's human `+0x168`, `+0x348`, `+0x34c` read every update): 59 started on update 2271; the next update
  `+0x348` and `+0x34c` held one handle; the sound handle `+0x168` was set on update 2290 (59's update 19) and emptied
  on 2307 (update 36); the props were empty again when the scene started. In the second play the hook saw
  `Human_CreateCarriedProp(Rembrandt, "dyn_s_spraycan")` called from `Anim_FireFrameEvents` (return `0x00103874`) on
  59's first update and `Human_ReleaseCarriedProps(Rembrandt)` from it (return `0x00103938`) 84 updates later.
  Rembrandt held no spray paint in either run.

**Coney** (#223, implemented): `Human` acts on the clip events above for any human (`repo:src/human/carried_prop.h`,
`Human::carriedProp()`): `0x30` notes the named prop, `0x37` its place (the word's point times its scale), `0x31`
removes it when the name matches, and a script's bind of the human to a scene role removes it
([Scenes](scenes.md#humans)). Play mode makes the object (`repo:src/platform/play_level_props.cpp`: the `dyn_` type's
record, kind 5 only) and hangs it from the bone, drawn with the hats. Stand-ins: a type of another kind is not made
(the original ignores kind 6 and puts the others in the hand), and the drop of a held kind-`0x27` object by
`Human_ReleaseCarriedProps` is not modelled. Disc test (`tests/platform/disc_level99_power_lesson_test.cpp`): through
lesson 6 the can is carried from 59's update 1 to 77 of its 59 and is gone after.

## Strong grapple {#strong-grapple}

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
   grabbed (`0x30`) and the player's power above 0, the victim gets pending damage of the [damage table](combat.md#damage-table)
   value of the player's id at that moment (657: index 17) through `0x0021d680`. The same code runs at the end of a
   plain grab's 72, whose value is 0, so a plain grab deals nothing and scores no hit (confirmed (runtime): no
   scoring call). Then the snap and attach, and the grab's scoring (`0x00264fa0`: 657, 659, 649 and 651 score as
   category 1, 3).
7. **No power cost**: the paired path neither checks nor spends power; the hold's drain of 15 per second
   ([constants](combat.md#constants)) starts with the grab, as for any grab (confirmed (runtime): 72 of 400 was enough,
   and the meter fell 1 every 2 updates from the press).

**At runtime** (confirmed (runtime), PCSX2, a copy of slot 6: the civilian `PoizoCiv` held in place facing the player
at 1.0-3.5 m until the intro ended, circle and cross pressed on the same update and held for 4 updates, no stick;
scenario `strong_grapple` and hooks of [`research/traces/patches/`](repo:research/traces/patches/)):

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

**The tutorial callback** ([HUD](hud-messages.md#tutorial-callback)) is called once, by the victim's
`Human_ApplyPendingDamage` on the update the hold replaces 657, so the anim id it passes is the **hold's, 82 (84 from
the rear), not 657** (confirmed (runtime): `Tutorial_CallCallback` given `0x52` and `0x54`). `level99`'s lesson waits
for exactly 82 or 84 ([The combat tutorial](scripting.md#level99-lessons)).

## Posing a grab {#grab-posing}

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
     more, or above 50 m/s. The human's state update applies them each step (`0x0023f5e0`, [Target selection](combat-targets.md#targets)).
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

## Grab pose at runtime {#grab-pose-runtime}

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

## Moving a grab, and the mount {#grab-turn}

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

### The hold walk: clips, speed and turning {#hold-walk}

What plays while the grabber walks the pair, from the front or from behind (user item 21). Confirmed (code) at the
addresses cited unless marked.

1. **Refused while busy.** `Human_MoveGrabbing` (`0x00245310`) runs for a grabbing or mugging human with a player's
   controls (per-player `+0x1b`); it sets no velocity while the human is busy (`Human_IsBusy`), in a scene state or
   **mugging**, or while the stick is at 0.95 or less. So a mugging pair never walks, and a part-tilted stick does
   nothing (no slow walk).
2. **Velocity.** After the turn above, the new heading `h'` (the old one moved by at most `r` toward stick + 180°) gives
   the velocity **−speed × facing(h')**, with the height kept at human `+0x3a0`: the pair always walks straight
   backward from the grabber's facing, so while the turn is under way the path curves, and once turned it runs along
   the stick. `speed` is record `+0x170`, the walk speed the movement style's walk clip gives
   (`Human_ComputeGaitSpeeds`, called at the end of `Human_ApplyMoveStyle` `0x00253688`).
3. **The movement styles of a hold** (`Human_ApplyMoveStyle`): the connect's link (`Grab_Link`, `0x0022c730`) pushes
   **style 7** on the grabber and **style 8** on the victim for a front grab, **9 / 10** for a rear one; the spins
   replace them (`Grab_SpinToRear` `0x0026d570` and `Mug_Start` `0x002729a8` set 9 / 10, `Grab_SpinToFront`
   `0x0026ec10` / `0x0026d998` set 7 / 8). Each style writes the idle (slots 0 and `0xb`) and the walk slots:

    | Style | Who | Idle (slots 0, `0xb`) | Walk (slots) | Also |
    | --- | --- | --- | --- | --- |
    | 7 | grabber, front | 82 `GRAB_HOLD_FROM_FRONT` | 86 `GRAB_FRONT_WALK_BACKWARD` (`gen_grab_front_pull`) in slots 4-7 | slots `0x10` / `0x11` = 51 / 55, the grab strikes |
    | 8 | victim, front | 83 `GRAB_HELD_FROM_FRONT` | 87 `GRAB_FRONT_WALK_BACKWARD_REACT` (`gen_grab_front_pull_react`) in slots 4-6 | |
    | 9 | grabber, rear | 84 `GRAB_HOLD_FROM_REAR` | 88 `GRAB_REAR_WALK_BACKWARD` (`gen_grab_back_pull`) in slots 4-6 | |
    | 10 | victim, rear | 85 `GRAB_HELD_FROM_REAR` | 89 `GRAB_REAR_WALK_BACKWARD_REACT` (`gen_grab_back_pull_react`) in slots 4-6 | |

    So the grabber's walk speed is clip 86's root speed from the front (1.125 m/s at runtime, above) and 88's from
    behind (about 1.22 m/s); the jog, run and sprint slots hold the same clip, so there is no faster gait in a hold.
4. **The hold is a four-clip task on both bodies.** The spins and the connect push, on each body, a type 11 four-clip
   mix ([Animation](animation.md)) of **slot 0 (the hold idle) and slot 4 (the walk) three times**, with task flags
   `0xc1` (no root velocity: the clips do not move the bodies) and anim state 4 (`Grab_SpinToRear` builds it with
   value 0 and speed 2.0). When the move state is rebuilt for a grabbing or grabbed human, `Human_BuildMoveState`
   (`0x0025f4d0`) skips the gait blend and calls `Human_BuildPairedFourMix` (`0x0025af90`), which rebuilds that
   four-clip task **on the human and on its partner** (`+0xc4`) on the same update, after a fade of 1/15 s (0.1 s
   more with held flag `0x40000000`), starting at value 1 (the walk). The mix's target is the speed over the walk
   speed, clamped to 0-1 ([Clip selection](characters.md#clip-selection)), so standing plays the hold idle, walking
   plays the walk clip alone, and the blend eases between them at the task's speed.
5. **The victim's walk.** The victim moves by `Human_MoveAttached` (`0x00244e78`): its place is the grabber's
   transform composed with the stored hold offset, swept against the world up to three times, and the velocity it
   reports is **the grabber's** (the grabber's vtable `+0x94`). So its own four-clip task sees the same speed and
   plays 87 (front) or 89 (rear) in step with the grabber's 86 / 88 (inferred from the shared speed: the two tasks
   are separate and only start together). When the third sweep is still blocked the victim keeps its previous
   transform and the grabber is set back with a zero velocity, so the pair stops at a wall (the grabber's part
   inferred from the vtable calls `+0x9c`, `+0xb4`, `+0x74`).
6. **How the pair turns.** Only the grabber turns (the rate above); the victim has no heading of its own in the hold:
   it is the grabber's rotation × the stored relative rotation, so it swings round the grabber on the hold's offset
   (from behind 0.24 m, 23.5° to the left; from the front 1.08 m, 20.6° to the right, [the offsets](#grab-posing)).

**Coney fixes** (user item 21; Coney's `Fighter::moveGrab` already turns and moves the pair):

1. Give the grabber movement style 7 (front) or 9 (rear) and the victim 8 or 10 for the whole hold, and switch them
   on every spin (78 / 79 to 9 / 10, 80 / 81 to 7 / 8), as the table says.
2. Play the hold as a two-clip blend per body (hold idle ↔ walk clip: 82 ↔ 86 and 83 ↔ 87 from the front, 84 ↔ 88
   and 85 ↔ 89 from behind), target = speed / walk speed clamped to 0-1, started on both bodies on the same update
   when the pair starts or stops walking, with no root motion from the clips.
3. Feed the victim the grabber's velocity (not zero), so its blend follows the grabber's.
4. Take the walk speed from the style's walk clip (86 from the front, 88 from behind) instead of the fixed 1.125 and
   1.22 m/s tunables, and walk only with the stick above 0.95, straight backward from the grabber's new facing.
5. No walk while mugging; stop the pair when the victim's sweep against the world fails.
6. Tests: a rear hold with a full stick plays 88 on the grabber and 89 on the victim within one update of each other,
   moves at clip 88's speed directly away from the grabber's facing, and returns to 84 / 85 when the stick is
   released; a stick at 0.9 neither turns nor moves the pair.

**The tackle's mount** (confirmed (runtime), and from the disc's events): after the tackle (5 / 6 from the front,
7 / 8) the pair is in 210 `MOUNTING_IDLE` / 207 `MOUNTED_IDLE` with the victim **0.032 m ahead and 0.120 m to the
mounter's left**, its heading the mounter's + 180°. That is the record's offset for 210 × its reach, which
`Mount_OnStrikeEnd` puts the mounter at ([The mount](#mount)); clip 210's type-8 pair event holds the same point
(−0.120, 0.032). The tackle
clips' own pair events: 5 (0.021, 1.013), 7 (−0.092, 1.036), 212 (0, 1.203).

## The mount {#mount}

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
  0.1 s blends. **118 lasts 65 updates (2.17 s)**; at its end `Grab_MountClipEnd` (`0x0026ef68`) moves the **mounter**
  to the victim over 0.1 s (`Human_AlignToVictimFacing`, `0x00277248`, below), and the idles become 210 / 207. A
  victim with human flag `0x20000` is got off at once (`0x00271470`).

**Where the pair goes in the mount** (the ground slam). Confirmed (code) and (runtime) as marked:

- **During 118 / 119 nothing holds the pair together.** `Grab_StartMountFromFront` ends the grab (`Grab_End`, twice)
  before linking the mount, and each clip is pushed on its own human's stack ([Paired
  tasks](formats/animation.md#paired-tasks)), so each body moves by its own clip's root motion from where the front hold
  left it. Confirmed (code); the paths below are confirmed (runtime).
- **At 118's end the mounter is moved, never the victim** (`Human_AlignToVictimFacing(0.1, mounter, victim, 210,
  0)`): the record's offset for id 210 (`AttackTable_GetOffset`: (−965, 259) thousandths, a unit vector in the
  mounter's frame, x right, y forward) is turned by the **mounter's** rotation, scaled by 210's reach (0.124 m,
  `AttackTable_GetReach`), and the standing point is the victim's position minus that vector; the turn
  (`Human_TurnToOver`) is to the **victim's heading + 180°** and the slide (`Human_MoveToOver`), both over 0.1 s. So
  the victim ends **0.120 m to the mounter's left and 0.032 m ahead** ((−0.965, 0.259) × 0.124 = (−0.1197, 0.0321)),
  facing him. Confirmed (code); the values are Rembrandt's record.
- **The same placement** follows the grounded mount (`Mount_StartOnGrounded`, `0x00271b30`, which first steers onto
  the victim with `Attack_SteerToTarget` over 0.1 s) and the tackle's mount (`0x00270270`), through `Mount_OnStrikeEnd`
  (`0x002700a8`): there the mounter instead gets off when the victim is more than 0.2 m above or below, farther than
  max(reach + 0.34, reach × 1.2) or not in a clear line. Confirmed (code).

At runtime (confirmed (runtime), PCSX2 2.9.94, a copy of slot 6, PoizoCiv 1 m ahead, circle at update 10 for the grab
and at 60 for the mount, then square, square, cross and L2; positions in the grabber's frame when 118 started,
x right, y forward; neither heading changed during 118 / 119):

| Update of 118 | Mounter | Victim | Victim from the mounter |
| --- | --- | --- | --- |
| 0 | (0, 0) | (0.379, 1.012) | (0.379, 1.012), the front hold |
| 8 | (0.288, 0.520) | (0.320, 1.263) | (0.031, 0.743) |
| 16 | (0.601, 0.415) | (0.556, 0.954) | (−0.044, 0.539) |
| 24 | (0.730, 0.369) | (0.542, 0.553) | (−0.188, 0.183) |
| 32 | (0.653, −0.032) | (0.721, −0.495) | (0.068, −0.463) |
| 36 | (0.597, −0.336) | (0.922, −0.928) | (0.324, −0.593): the victim is down and stays |
| 48 | (0.717, −0.567) | (0.924, −0.936) | (0.207, −0.369) |
| 64 (the last) | (1.050, −1.032) | (0.959, −1.052) | (−0.092, −0.020) |
| 210, 3 updates on | (1.079, −1.084) | (0.959, −1.052) | **(−0.120, 0.032)** |

So the slam throws the victim about 2 m back past the grabber, who follows on his own clip, and the pair ends 1.5 m
from where the hold was. The 0.1 s slide moved only the mounter (0.06 m in 3 updates; the victim's position did not
change by 0.001 m). The strikes 219, 221 and 223 and their returns to 210 moved neither body, and L2's 244 / 245
moved only the mounter.

**In the mount** (`Player_UpdateMounting`, `0x0027ec20`, state `0x400`), confirmed (code); confirmed (runtime) for
square, cross, circle and L2, **the same after a tackle or a grab**:

| Input | Effect |
| --- | --- |
| square (`0xf`) | 219 or 221 at random (record `+0x68` + 0 or 2; victim 220 / 222), 61 damage; spends 40 (0.2, halved for a player) |
| cross (`0x10`) | 223 (record `+0x6c`; victim 224), 61 damage; spends 40 |
| cross held, square (`0x22`) | the power strike 225 (231 raging) through `Attack_StartPaired`; needs more than 0.25 power, otherwise gets off |
| circle (`0x1e`, `0xd`, `0xe`) | back to the front hold: 248 / 249 (36 updates), then 82 / 83 (`0x00271808`) |
| L2 (5) | gets off: 244 / 245, the victim rises with 199 (`0x00271098`) |
| triangle | mug ([Mugging](combat-throws.md#mugging)) |

The mount drains power at the tackle's 15 per second ([constants](combat.md#constants)). The strikes ran 16, 19 and 28
updates back to 210. Level 99's tutorial follows this through two callbacks: `AddAnimCallback(player, 210,
"PlayerState")` shows the mount's prompt when 210 starts, and `HUDSetTutorialCallback("P1.BasicAttacks")`, which
receives each hit's anim id, wants 219 or 221 and then 223 before it calls `P1.BasicAttacksDone`.

### The mount's power strike ends the mount {#mount-power}

Cross held and square pressed in the mount (user checklist item 32) is **not** a mount strike: it ends the mount
and leaves the mounter standing in the fight stance and the victim on the ground. Confirmed (code) at the addresses
cited, and (runtime) as below; the clip events read from the disc (numbers only).

1. **The start** (`Player_UpdateMounting`, `0x0027ec20`): with more than 0.25 of the power meter, a player calls
   `Attack_StartPaired(mounter, victim, 225, 1, 0x400000)` (231 while raging, human `+0xe0` `0x80000`); with 0.25 or
   less he gets off instead (`Mount_GetOff`). The mount's own strikes (219 / 221 / 223) go through
   `Mount_StartStrike` and stay in the mount; only the power strike and the knife or bottle attacks (488, 496 with
   the held set 1) go through `Attack_StartPaired`.
2. **The mount is ended at once** (`Attack_StartPaired`, `0x00262ac8`): it spends 0.25 of the power, then calls
   `Grab_BreakOnHit(mounter, victim, 226)` (`0x0022b960`). The mounter is tackling (state `0x400`), so
   `Tackle_End(mounter, victim)` (`0x0022c388`) clears the mount's states `0x400` / `0x800` and pops style `0xc`.
   Then the pair is linked as a **paired move**: state `0x1000` on the mounter, `0x2000` on the victim, both push
   weights 1e9, one new body group for both (the counter at `*0x00597198 + 0x10`, body `+0x3c`) and body flag
   `0x200`. Because 226 has a type-7 (knockdown) event, the victim gets move style `0xf` (down).
3. **The mounter's clips**: 225 holding `+0x08` `0x7` (an attack's phases), with the end hook
   `Attack_OnPairedClipEnd` (`0x002629a8`), chained to the looping **fight-stance idle** (the anim record's `+0x54`,
   anim state `0xb`; 358 for Rembrandt at runtime). No mounting idle 210 follows.
4. **The victim's clips**: 226 holding `0x400000` with the end hook `Pair_OnVictimClipEnd` (`0x00262a38`), then,
   by the code, when 226's knockdown value is 0 (`Anim_GetKnockdownValue`, `0x00101a60`: the first type-7 event's
   word), held `0x2000` and **198** (the knockdown's follow-up), then the looping **grounded idle** (record `+0x28`,
   anim state 0). At runtime 198 did not play: 226 went straight to the grounded idle 196 (below). He lies there
   and gets up by the ordinary ground time ([Reactions](combat-reactions.md#reactions)). The damage ([damage table](combat.md#damage-table)
   index 37; Rembrandt's value in [Combat moves](combat-moves.md#frame-data)) is dealt on the start update (the
   health field showed it one update later at runtime); a lethal one plays the death reaction instead.
5. **The extension**: the mounter is now in state `0x1000`, so the dispatcher sends him to `Player_UpdatePowerMove`
   (`0x0027df38`) as after a grab's power strike: square or cross while 225 holds the window `0x2` plays **227** (233
   after 231), victim 228 (234), at most twice (record `+0xbc` < 2; one in practice: 227 opens no window, below).
   This settles the mount part of `level99`'s
   lesson 10 ([scripting](scripting.md#level99-lessons)): 231 → 233 comes from the same + 2.
6. **The end**: `Attack_OnPairedClipEnd` scores the move, lets the crowd taunt and calls `Throw_ClearLink`
   (`0x0022bce0`): the mounter's `0x1000` and link are cleared and his push weight and body group restored; then
   `Throw_ClearVictimLink` (`0x0022bdc0`) clears the victim's `0x2000` and puts him down or stuns him by his state.
   Nothing re-links the mount: getting back on top takes a new circle at the grounded victim (the grounded mount,
   `Mount_StartOnGrounded`, `0x00271b30`).

Clips (Rembrandt's set `warr_re_header`; the generic set has `gen_mount_power20_*` in the same ids): 225
`gen_mount_power10_p1`, 226 `gen_mount_power10_react_p1` (type-7 event, word 0, at frame 0), 227 / 228 `_p2` /
`_react_p2` (type 7, word 0), 231 / 232 `gen_mount_power13_p1` / `_react_p1` (type 7, word 0).

**At runtime** (confirmed (runtime), PCSX2 2.9.94, scenarios `moves_mount_power` and `moves_mount_power_ext`: a copy
of slot 6, PoizoCiv a puppet 1.0 m ahead with 600 health, the player's power 400; circle at update 10 (the front
grab, 72 / 73 then the holds 82 / 83), circle at 60 (118 / 119, then 210 / 207 at 126), cross held from 140 with
square tapped at 142; stick centred (0, 0) throughout; hooks `paired-start` and `anim-dispatch`):

| Update | Mounter | Victim |
| --- | --- | --- |
| 143 | `Attack_StartPaired(player, victim, 225, 1, 0x400000)` from `Player_UpdateMounting` (return `0x0027ee78`); 225, state `0x405` → `0x1005`, phase `0x1`; power 346 → 245 (0.25 of 400 spent, plus the drain); 358 queued after 225 | 226, state `0x801` → `0x2001`; the grounded idle 196 queued after it |
| 144 | | 79 damage (600 → 521): Rembrandt's 225 value, scored one update after the start |
| 167 | phase `0x2` (the extension window) | |
| 187 | phase `0x4` | |
| 223 | 225 ends after **80 updates**; **358** (the fight stance's idle), state `0x5`: no `0x400`, no `0x1000`; power refilling | **196** (grounded idle), state `0x180001` (down and stunned), 0.88 m away; **no 198** |
| 283 | | rises with **199**, 60 updates (2.0 s) after landing |
| 311 | | 356, then 357 and his ambient clips |

So the mount ends for good at the strike's start and nothing returns either body to 210 / 207. One difference from
step 4 above: **198 never played**; the anim-id hook saw the victim take only 196 as 226's follow-up, so the
`Anim_GetKnockdownValue` word for 226 was not 0 at runtime and `Attack_StartPaired` took the branch without 198 (why
the word differs from the one read off the disc is open).

**The extension** (`moves_mount_power_ext`: as above, then cross tapped every 8 updates from 160 to 216): the tap at
160 came before the window and did nothing; the tap at 168 started `Attack_StartPaired(player, victim, 227, 1,
0x400000)` from `Player_UpdatePowerMove` (return `0x0027dff0`) on update 169: 227 / 228, **no power spent**, another
79 damage (521 → 442) on update 170, 196 queued again for the victim. 227 ran 60 updates and opened no window `0x2`,
so the later taps did nothing and **one extension is all a mount power strike gets** (the counter's limit of two is
not reached). At 229 the mounter stood in 358 (state `0x5`), the victim lay in 196 and rose with 199 at 289 (2.0 s).

**Coney fixes** (item 32; today `Fighter::playMountAction` plays the power strike like a mount strike, back to 210 /
207, so the player stands up at 225's end and snaps back into the pin):

1. In `PlayerCombat::updateMounting`, a `MountAction::PowerStrike` ends the mount on its start update: the combat
   mode leaves `Mounting`, the pair stops being a mount (no seat, no mounted target state) and becomes a paired move
   (the mounter "throwing", the victim "thrown", [state flags](combat.md#state-flags) `0x1000` / `0x2000`).
2. The mounter plays 225 (231 raging) chained to the fight-stance idle, not to 210; the victim plays 226 (232) with
   held `0x400000`, then the grounded idle (196; no 198, as seen at runtime), and is down (style `0xf`) with the
   ordinary ground timer running from the knockdown.
3. Square or cross in 225's window plays 227 / 228 (233 / 234) through the same extension code as the grab's
   57 → 59; 227 opens no window of its own, so one extension (seen at runtime); 229 is the filler clip and never plays.
4. When the mounter's clip ends: clear the paired link on both and leave the victim on the ground; a circle at him
   is the grounded mount again.
5. Test (no disc needed, synthetic clips): mount, power strike, run to the end; assert the mounter is standing in the
   fight-stance idle, not mounting, the victim is grounded and not mounted, and no frame after the strike shows 210 /
   207. A level99 cp1 playthrough step can assert the same on the hobo.

## Grabbed, and breaking free {#grabbed}

**When the player is grabbed** (`Player_UpdateGrabbed`, `0x0027fd68`, the grabber at human `+0xc4`), confirmed
(code); confirmed (runtime) where marked, from a puppet civilian's grab ([Being hit](combat-reactions.md#being-hit-runtime)):

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

`CfgButtonMash` plays no part here: its only reader is the theft game ([Stereo theft](combat-targets.md#stereo-theft)).

**When the player's victim breaks free**: the grab drains the grabber's power (15 per second) and
`Human_UpdateMeters` (`0x002562d0`) ends it when the meter reaches 0. If the victim has flag `0x20000` or `0x4000`, or
the grabber is hurt (and the victim lacks `0x2000000000`), the victim **escapes** through `0x0026cc18`; otherwise both
let go (95 / 94). A tackle dismounts (`0x00271470`). Confirmed (code); confirmed (runtime): from 25 power the grab
drained at 15 per second and broke with 95 / 94; with the player hurt (200 of 900 health) the civilian escaped with 100,
the player played 101, was knocked down and stunned (`0x180005`), lay in 196 and rose with 199 2.78 s later; the
escapee took 20. A third human hitting a grabber costs it 0.6 of its power (`0x00510274`).

## Breaking a pair from outside {#pair-break}

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
| a power move (`0x1000`) or `0x2000` | no clip; the throw link is cleared (`0x0022bce0`, `0x0022bdc0`) and the other human gets state `0x20000000` | |

The clips blend in over 0.2 s; 244 and 199 end in `0x00267870` (not traced). That 245 plays before 199 is inferred
from the task chain (the dismount's order, [The mount](#mount)). A human with no link is left alone. So a teleport
never leaves a partner hanging: the human not named stands free after its reaction clip (or gets up from the mount),
and the named one is free at once. `Grab_Release` does not run, so 94 / 95 never play from a teleport.

**Brains are not links.** `BrDead`, `BrSuspend` and `Brain_ClearActions` only clear the brain's queued actions and
set its flags or handlers (`0x00292330`, `0x002923a0`); none touches the human's state, so a grab in progress goes on.
Inferred: its power keeps draining (15 per second in a grab or a mount, [constants](combat.md#constants)) until `Grab_Release`
ends it with 95 / 94, as for any grab whose power runs out ([Grabbed](#grabbed)). There is no `HuStop` binding in the
game.
