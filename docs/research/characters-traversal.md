---
kind: topic
system: characters
owns:
  functions: [
    "0x002217f0", "0x00223188", "0x00223c10", "0x0023db48", "0x00240898", "0x0024d810", "0x002544a0", "0x002562d0",
    "0x00256a60", "0x0027c120", "0x0027ce90", "0x00281450", "0x00281838", "0x00281c20", "0x00282370", "0x002826f0",
    "0x002829e8", "0x0029ade0",
  ]
---
# Characters: sprint, jumps and climbs

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

The player's traversal moves. Holding L2 at a run sprints while stamina lasts; Triangle climbs a fence or wall
in front of the player or, at a run, jumps, and with something in hand does what the held object allows. This
page holds the stamina meter and the sprint, the jump's checks, launch and air control, and the climb's probes,
clips and rise. The ground they leave and land on is [The ground](characters-ground.md); the speeds they use
are on [Locomotion](characters-locomotion.md).

## Map {#original-structure}

The functions of the sprint, the jump and the climb:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0027c120` | `Player_UpdateActions` | the player's commands each update: climb, jump, block, sprint | confirmed (code) for the parts cited |
| `0x0027ce90` | `Player_UpdateSprint` | clears the sprint flag and sets it again while L2 is held | confirmed (code); confirmed (runtime), run r-20261124-chars-sprint |
| `0x00223188` | `Human_StaminaMax` | stamina maximum from the power class | confirmed (code) |
| `0x002562d0` | `Human_DrainMeters` | stamina drain while sprinting (and a second meter) | confirmed (code) |
| `0x00256a60` | `Human_RefillMeters` | stamina refill (and the second meter) | confirmed (code) |
| `0x002829e8` / `0x0023db48` | `Player_TryJump` / `Human_BeginJump` | the jump's checks; take-off gait and height | confirmed (code) |
| `0x002217f0` | `Human_LaunchJump` | the jump's velocity, then airborne | confirmed (code) |
| `0x00240898` | `Human_AirControl` | the jump's state function: steering in the air | confirmed (code) |
| `0x002826f0` | `Climb_TryStart` | the two forward probes; fence, wall and their short forms | confirmed (code) |
| `0x00282370` | `Climb_ProbeTop` | finds the obstacle's top and picks fence or wall | confirmed (code) |
| `0x00281c20` | `Climb_Start` | reach window, start point, the three-clip chain | confirmed (code) |
| `0x00281450` / `0x00281838` | climb callbacks | end of the first clip, from a run / from standing | confirmed (code) |

## Data

The meters and the climb's fields are in [the record](characters/index.md#the-record) and [the human
object](characters/index.md#the-human-object); the constants are given with each rule under Behaviour.

## Behaviour

### Sprint and stamina {#sprint}

Confirmed (code) at the cited addresses unless a line says otherwise.

**Sprint is held, not toggled.** Every update `Player_UpdateSprint` (`0x0027ce90`, called from `0x0027d5a0`, which
`Player_UpdateActions` calls) first **clears** state flag `0x1000000`, then sets it again (and calls `0x00230140`,
and clears state flags `0x8008`) only when all of these hold. Confirmed (code):

1. L2 is held;
2. stamina (record `+0x14a`) is not 0;
3. record `+0x08` bit `0x10` is clear.

So the sprint lasts exactly as long as L2 is held and stamina lasts: letting go of L2 ends it on the next update, and
nothing latches it. It also ends when stamina reaches 0 (below) and when `Player_UpdateActions` takes its block branch
(`0x0027c6b0`): **R1 held** in a fight, or L1 released or L1 + R1 while already blocking
([Combat, Dispatcher](combat/index.md#dispatch)), which clears the flag and sets state flags `0x8001`. **At runtime**
(`level99` and a street, stick 1.0 straight up and L2 held
through patched pad input, [Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)) the flag was set every
update, the command was 5, and the speed rose by 0.8 m/s per update from 7.80 to **10.245 m/s** (gait 5).
Confirmed (runtime), run r-20261124-chars-sprint.

**The stick still decides.** The flag only asks for a sprint: the locomotion's sprint test (`0x00225dc0`) also needs
the stick above **0.95** and stamina, as for a run ([Locomotion](characters-locomotion.md#locomotion)). With
`0x00510258` set (`CfgPlayerRunButton`), L2's pressure decides instead (pressure byte 10 above 100 sprints, otherwise a
run); it is 0 in play. Confirmed (code).

**Stamina** is the s16 at record `+0x14a`. Its maximum is `Human_StaminaMax` ([Power
classes](characters-classes.md#power-classes)): 135 for Rembrandt (confirmed (runtime), run
r-20261124-chars-sprint). Confirmed (code) unless marked:

- **Drain** (`0x002562d0`, called from the brain `0x00254e78`): while the gait stored with the velocity (`+0x1a8`) is 5
  and record `+0x08` is 0 (`0x00223a98`), stamina loses **20 per second** of game time (`0x005101f4`, `CfgBurnRates`
  rate 5; 0 when `0x005102a8` is set, which it is not in play), with the fraction carried at `+0x158` and the time at
  `+0x154`. When it reaches 0 or below it is set to 0 and the sprint flag is cleared. Gait 5 is the speed nearest the
  sprint speed, above 9.02 m/s for Rembrandt, so the drain starts on the third update of the speed-up.
- **Refill** (`0x00256a60`, every update, from `Human_UpdateActions`; confirmed (code)): stamina gains the power class's
  `+0x2e`, **40 per second** for Rembrandt, up to the maximum. It gains nothing (and the time is not carried over) while
  any of these holds: gait 5 (`0x00223a98`); state flags `0x1c18003ff0` (the airborne bits among them); a jump or fall
  (`0x00227f90`, flags `0x1c00000000`); **gait 4 (run) with L2 held** (`0x00223a60` and the button); record `+0x08`
  flags `0x41a420`. Human flag `0x4000000` fills stamina and the second meter every update. In three states
  (`0x00223b98`, `0x00223bc0`, `0x00223b70`) the function returns before taking the time, so the time spent in them is
  refilled at once afterwards.
- **No delay and no threshold**: the refill starts on the first update the blocks are gone, and the sprint test is
  only "not 0", so one point of stamina sprints again.

**At runtime** (the street, Rembrandt, stamina 135 of 135; stick and L2 through patched pad
input; stamina, speed and flags read every update). Confirmed (runtime), run r-20261124-chars-sprint-start;
confirmed (runtime), run r-20261124-chars-sprint:

- From a standstill with stick 1.0 and L2 held, stamina **still refilled** through the run start (gait 2-3), stood
  still at gait 4 (the "run with L2" block), and fell by 1 every 1.5 updates (**20 per second**) from the update the
  speed reached 9.686 m/s (gait 5, the third update of the speed-up from 6.486).
- When it reached 0 the sprint flag cleared and the speed dropped from 10.245 to **7.801 m/s in one update**; with
  L2 still held for 1.5 s more, stamina **stayed at 0**.
- With L2 let go and the stick still at 1.0 (a run), stamina refilled at 40 per second (0 to 39 in about 1 s); L2
  pressed again sprinted at once.
- Standing, it refilled by 4 every 3 updates (**40 per second**) to 135.
- Letting go of the stick in a sprint played the **run stop** (417) for about 0.8 s with record `+0x08` = `0x80000`,
  the body moved by the clip (about 1.6 m) while stamina refilled, then the idle: the skid of
  [Locomotion](characters-locomotion.md#locomotion) step 3.
- Stamina also drains **in a jump** from a run (135 to 132): the gait is taken from the whole velocity, and the jump's
  vertical speed puts it above 9.02 m/s ([Jumping](#jump)).

**What a player sees**, from the code and the runs above:

| Stick | L2 | Result |
| --- | --- | --- |
| 0.5 or 0.8 | held | walk at 1.63 m/s; the flag is set but there is no sprint and no drain; stamina refills (inferred) |
| 1.0, or a full diagonal | held | run, then sprint at 10.245 m/s after 4 updates; stamina drains from the third, 135 lasts **6.75 s** |
| 1.0 | released | run at 7.80 m/s; stamina refills at 40 per second, **3.4 s** from empty to full |
| 1.0 | held at 0 stamina | run at 7.80 m/s, and stamina **stays at 0** until L2 is let go |

In an input script: `stick left 0 100` and `press l2` to sprint, `release l2` to refill.

**Measured** (the stick pushed fully forward from a standstill by the scripted pad, magnitude 1.0, the position
traced every update): 4.10 m after 23 updates, 10.08 m after 46: 5.98 m in the second 23 updates (0.77 s),
**7.80 m/s**, Rembrandt's run speed (confirmed (runtime), run r-20261124-chars-run-stop, the straight run from the
update the stick is pushed).

### Jumping {#jump}

Confirmed (code) at the cited addresses unless a line says otherwise.

**The button.** On command 10 (triangle pressed), `Player_UpdateActions` (`0x0027c120`) tries, in order, and stops at
the first that succeeds. Confirmed (code):

1. a **climb** (`Climb_TryStart`, [below](#climb)), if human flag `0x20000000` is clear and the stick is above 0.12;
2. a **context action** (`Player_TriangleAction`, `0x002811f0`, confirmed (code)), if L2 is not held;
3. a **jump** (`Player_TryJump`, `0x002829e8`, confirmed (code)), if human flag `0x10000000` is clear and the stick is
   above **0.95**;
4. the **drop** of what is in hand (`Human_DropHeld`, `0x00257f38`), if L2 is not held
   ([Triangle with something in hand](#triangle-held)).

The object action is the **drop**: `0x00226ff0` is `Human_GetHeldObject` (`+0x338`) and `0x00257f38` is
`Human_DropHeld`, which gives the held object back its physics body and clears the hand. Confirmed (code). There is
no jump buffer in this order: triangle is acted on in the update it is pressed. So, inferred from the order and the
jump's refusal while carrying a class 4 or 6 object: triangle with a weapon in hand and the stick pushed toward a
climbable wall that is still out of the climb's reach (the climb fails, the jump is refused) **drops the weapon**, as
checklist item 2 reports of Coney; whether that is what the original does at runtime is not checked.

Nothing happens while state flags `0x7bf9e9f7ff0` or record `+0x08` flags `0x5cfeafb` are set.

**Checks** (`Player_TryJump`, then `Human_BeginJump` `0x0023db48`). Confirmed (code). The jump is refused when the human
carries an object of class 4 or 6 (`0x00224000`), is in a combat stance (`0x00228340`), moves at 3.3 m/s or less
(`0x0051018c`, speed `+0x1ac`), is not a player, or a ray from 1.7 m above the feet, 5.5 m long in the stick's
direction (`0x0021d228`), hits a **climbable** triangle (the rule of [Climbing](#climb)): near a climbable wall the
button climbs or does nothing. `Human_BeginJump` then needs the gait for the speed (`Human_GaitForSpeed`) to be 3 or
more and `|v|` at least 3.3; it keeps the gait at `+0x3c0` and the height at `+0x560` and sets state flag
`0x400000000`.

**Launch** (`Human_LaunchJump`, `0x002217f0`, called by the state 25 builder). Confirmed (code):

- horizontal velocity: the current direction times the **run speed** for a take-off gait of 3 or 4, the **sprint
  speed** for gait 5 (the jog speed below 3);
- vertical velocity **5.5 m/s** (`0x00510188`, read by `Human_LaunchJump`; confirmed (code));
- `Human_StartFall` (vtable `+0x154`), then the state function `Human_AirControl` (`0x00240898`, set through
  `0x00227c28`) and anim state 26.

**In the air** (`Human_AirControl`). The horizontal speed is kept. With the stick above 0.12 the heading turns toward
it at the `Human_MaxTurn` limit with the same ease as on the ground, and the velocity turns with it; with the stick
centred nothing changes. Gravity, the airborne counter and the landing are those of a fall. Confirmed (code).

**At runtime** (the street, Rembrandt on flat ground, triangle tapped for one update). Confirmed (runtime), run
r-20261124-chars-run-jump (the other cases: runs r-20261124-chars-walk-jump, r-20261124-chars-sprint-jump,
r-20261124-chars-sprint-jump-turn and r-20261124-chars-runstart-jump):

| Case | What happened |
| --- | --- |
| stick 0.8 (walk at 1.63 m/s), triangle | nothing |
| stick 1.0, run at 7.80 m/s, triangle | state 26 and clip 434 at once, state flags `0xc00000000`; `vz` 5.5 then −0.5227 per update; apex **1.06 m** above the take-off; horizontal speed 7.80 throughout; landed 25 updates after the tap (24 in the air), **6.24 m** further than the first update in the air |
| stick 1.0 and L2, sprint at 10.245 m/s, triangle | the same arc at 10.24 m/s horizontal, 24 updates in the air |
| the same, stick turned 90° right just after take-off | the heading turned by about **4° per update** toward the stick, the horizontal speed staying 10.24; that is the run's limit, not the sprint's 2.5° (which gait the air turn uses is not traced) |
| landing | state 27, **436** for about 0.4 s at its own 4.23 m/s, record `+0x08` `0x1000000`, then the gait blend from a jog back to the run |
| per update (the feel pass, slot 1) | **24 updates** in the air from a run: the last airborne update leaves the feet 0.19 m below the ground and the next one lands them ([the landing rule](characters-ground.md#falling)); that landing update still moves at the full 7.80 m/s, then 436 holds 4.23 m/s for 10 more updates (11 in all) and the gait blend gains 0.8 m/s per update from there; take-off to landing **6.24 m** |
| triangle during the run start (414), at 3.35 m/s | a jump at once: the run start does not block it, only the 3.3 m/s and gait tests do (slot 7) |

Clips 427 (slot 25) and 430-433 (jump from idle, walk or either foot) did not play in the player's jumps (434 in the
air and 436 on landing; confirmed (runtime), run r-20261124-chars-run-jump; confirmed (runtime), run
r-20261124-chars-sprint-jump; confirmed (runtime), run r-20261124-chars-runstart-jump): what plays them is not traced.

**An AI's jump.** `Human_BeginJump`'s gait and speed tests apply only to the player's brain (type 0): an AI may jump
from any gait, standing too, and its `+0x3c0` is not written. `Human_LaunchJump` gives a human whose per-player record
`+0x1b` is 0 the velocity of the route follower's arc instead (`0x0029ade0`): the lowest of the vertical speeds 0.5,
1.25, 2.0, ... m/s whose arc lands on the route's landing point at no more than 10 m/s in plan. Confirmed (code); no
recorded run covers an AI's jump yet. The rest is in
[AI: Jump legs](ai-moving.md#route-jump).

**In an input script**: `stick left 0 100`, wait until the run start is over and the speed is above 3.3 m/s, then
`tap triangle`; add `press l2` beforehand for a sprint jump.

#### Triangle with something in hand {#triangle-held}

User checklist item 2 (an early press at a wall drops the weapon instead of climbing). The four tries above, read
again for a held object; confirmed (code) at `Player_UpdateActions` (`0x0027c120`, both its triangle blocks),
`Climb_TryStart` (`0x002826f0`), `Climb_Start` (`0x00281c20`), `Player_TriangleAction` (`0x002811f0`),
`ContextAction_Use` (`0x0024d530`) and `Human_PickUpSearch` (`0x0024d810`):

1. **There is no buffer.** Command 10 is one sample ([Commands](combat/index.md#commands)) and each try runs once, on
   that update. An "early" climb is only the long ray: 4.5 m ahead for a player (human flag `0x2`) at gait 4 or 5, else
   1.5 m.
2. **The climb comes first and ignores what is in hand.** `Climb_TryStart` refuses only on record `+0x08` `0x84240`;
   nothing about a held object. When the climb starts, `Climb_Start` **drops** an object of held set 4 (overhead:
   barrels, crates) always, and one of set 6 (the ghetto blaster) unless `CfgClimbWithGhetto` (`0x00510250`) is set
   (confirmed (code) in `Climb_Start`); a weapon (sets 1-3) or a one-handed throwable (5) stays in hand through the
   climb.
3. **A press slightly out of reach is swallowed.** `Climb_Start` returns "done" for a player (per-player `+0x1b` not
   0), without climbing, when the face is beyond the clip's maximum distance but closer than twice a clip's reach
   (which clip's reach the decompiler hides; [open question](#open-questions)), and when the space at the top is not
   clear (`Climb_IsSpaceClear`). The dispatcher then returns with nothing more: **no jump and no drop**. An AI gets
   "not done".
4. **The context action never drops for a player with nothing to take.** `Player_TriangleAction` → `ContextAction_Use`
   → (no context record) the pick-up search. With an object found and something in hand (not kind 12 or 24), it
   drops what is held and sets human `+0x5b8`; the history-hold triangle (`0xb`, 7 samples) then takes the new one
   (`Player_TriangleAction` again, `+0x5b8` cleared). With **nothing found**, a pad-controlled human gets 0 (no
   drop); only an AI (`+0x1b` = 0) drops here.
5. **The jump**: refused while carrying held set 4 or 6 (`Human_HoldsKind4Or6`), in a fight stance, at 3.3 m/s or
   less, or with a climbable face within 5.5 m ([above](#jump)). A weapon does not refuse it.
6. **Only then the drop**: with L2 not held and an object in hand, `Human_DropHeld` (`0x00257f38`) and `+0x5b8` = 1
   (confirmed (code)).

So with a bat at a run toward a climbable wall: within 4.5 m (and inside the clips' distances) he climbs with the bat;
just beyond the clips' distance the press may be swallowed; farther but within 5.5 m nothing climbs or jumps and the
**bat drops** (the original does this too); beyond 5.5 m, at a run, he **jumps with the bat**. Empty-handed, the last
two cases do nothing and jump.

**Coney fixes** (item 2; today `PlayLevelMode`'s context action returns `TriangleResult::Drop` at try 2, before the
jump, `src/platform/play_level_objects.cpp`):

1. Remove the drop from the context action for the player: with nothing to take it returns "nothing" (an AI keeps
   the drop there). With an object to take and something in hand, drop and remember the swap (`+0x5b8`); the
   triangle history hold (7 samples) picks the new object up.
2. Add try 4 in `Human::tryActions` (`src/human/human.cpp`): after a refused jump, with L2 not held and an object in
   hand, drop it.
3. The climb with an object: keep weapons and set-5 objects in hand; drop set 4, and set 6 unless
   `CfgClimbWithGhetto`. Check that a player carrying a weapon at a run is gait 4 or 5 for the 4.5 m ray (a slower
   armed run speed must not shorten the reach to 1.5 m).
4. Add the swallow of try 3 when Coney's climb distances say "too far by less than twice the reach": the press ends
   there.
5. Tests: with a bat, at a run, triangle 3 m before a climbable fence climbs and keeps the bat; 5 m before it drops the
   bat; 8 m before in the open it jumps and keeps the bat; with a barrel (set 4) the climb drops it.

### Climbing {#climb}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Climb_TryStart` (`0x002826f0`) runs on triangle with the stick above 0.12 ([Jumping](#jump)). Confirmed (code):

1. Refused while record `+0x08` has any of `0x84240`.
2. Direction `d`: the stick's direction when its magnitude is above 0.01, else the facing (`Player_GetStickDirection`,
   `0x0021d228`; confirmed (code)).
3. Two horizontal rays along `d` from **1.7 m** and **0.69 m** above the feet, **1.5 m** long, or **4.5 m** when human
   flag `0x2` is set (players) and the gait (`+0x1a8`) is 4 or 5.
4. If both hit and their distances differ by less than 0.1, the obstacle is **tall** (the high hit is used);
   otherwise it is **short** (the low hit is used; a hit by the high ray alone is not a climb).
5. The hit must be **climbable**: material 30 (`MATERIAL_LOW_FENCE`), or triangle flag `0x4` for a player, or
   triangle flag `0x80` ([Collision](collision.md#triangles)).
   Both probes are `WorldManager_RayCast`, the level's static mesh only, so a parked car is climbed through its
   shell in that mesh (every triangle with flag `0x4`, material 0, never `0x80`, so **only players** climb cars;
   [below](#car-shells)). On `level99_2`'s sedan shells the 0.69 m ray meets a side (`0xf00d` or
   `0x700d`) and the 1.7 m one passes the 1.71 m roof or meets the sloped screen at another distance, so the car is
   **short**. The down probe from 1.8 m finds the sill or bonnet at 1.18-1.30 m above the ground, inside 0.7-1.7 m: a
   short wall climb (455 / 458). Inferred from the code and the disc's mesh; not run.
6. `Climb_ProbeTop` (`0x00282370`) with, for a tall obstacle, `H` = 3.0, a window of 1.7-2.91 m and the fence ids
   437 / 440; for a short one `H` = 1.8, a window of 0.7-1.7 m and the short-fence ids 443 / 446.

`Climb_ProbeTop`, confirmed (code):

1. The face must look at the player: `n · d < −0.7` (within about 45°).
2. A ray straight down from `H` above the point 0.4 m beyond the face (`pos + d·t − 0.4·n`), `H + 0.5` long, finds
   what lies just behind the face; its **top** is `H` minus the hit distance, a height above the feet.
3. A top inside the window is a **wall climb**: the ids + 12 (449 / 452 wall, 455 / 458 short wall).
4. Otherwise a top of 0.25 m or more refuses the climb.
5. Otherwise (no floor just behind the face, or one near the feet' height: a **fence**), for a tall obstacle a ray
   along `d` from 2.5 m above the feet must miss (the fence is lower than 2.5 m), and the fence ids are kept.

`Climb_Start` (`0x00281c20`), confirmed (code):

- The first id is the **standing** clip; the **running** one (+3) is used when human flag `0x2` is set and the gait
  is 4 or 5. Each clip's reach is the length of its type-8 event vector (`0x00101558`, through the Anim Range List,
  `0x002544a0`). A running climb needs the face between `(r1 + r2) × 0.4` and `r2 × 2.2` away (the reaches of the
  first and second clips); a standing one at most `(r1 + r2) × 0.4`. `CfgClimbWithGhetto` (`0x00510250`) is also
  read here.
- The start point is the hit at the feet' height plus `n × 0.9 × reach`, kept at `+0x2c0`; the human is turned to
  face the wall and moved there over 1/60 s or 1/15 s.
- The three clips play in turn (P1, P2 = id + 1, P3 = id + 2). At P1's end (`0x00281450` running, `0x00281838` standing;
  confirmed (code)) the forward probe is made again from 0.69 m (2.5 m long running, 1.5 m standing). On success the
  body is **moved at once** by P2's root displacement turned to the facing, body `+0x40` gets `0x80000000 | 0x4000`, and
  record `+0x08` gets `0x40` (the fences stop blocking, [Walls and steps](characters-ground.md#walls)); on failure the
  climb ends in the idle or the combat idle. A running climb ends in a gait blend, a standing one in the idle.
- While clips 437-460 play, `+0x5b9` is 1 (`Human_StateUpdate`).
- **From P2 on nothing collides.** While record `+0x08` has `0x40`, `Human_Move` (`0x0023d8c8`) skips the wall sweep
  (`PhysicsBody_Sweep`) and the ground snap and adds the velocity and the clip's root motion (`+0x2f0`), height
  included, straight to the position (`Object_SetPosition`); the other path zeroes the height and leaves it to the snap.
  The teleport at P1's end goes through body vtable `+0x1c` with no sweep, `0x4000` stops the body re-posing
  (`0x003422d0`), and `Human_GetPosition` returns `+0x2c0` while `0x40` is set. So a climbed face of any material (a
  parapet of material 107 with triangle flag `0x80`, say) stops the body only during P1; the skip of materials 30, 31
  and 122 in `Human_OnContact` covers only contacts that still reach it. Confirmed (code): the branch is
  `Human_HasHeld40` (`0x00223c10`) in `Human_Move` (`0x0023d8c8`), the test before `Vec4_Add(..., +0x2f0)` and
  `Object_SetPosition`; the sweep and `Human_SnapToGround` sit only on the other branch. Confirmed (runtime), run
  r-20261124-chars-railing-vault, on a railing of material 7, below.
- **Over a drop there is no third clip.** When P2 ends with nothing under the feet (a railing above a lower floor), the
  next update's ground snap misses and the human falls: the fall clip **428** (record `+0x08` `0x400000`) in place of
  P3, gravity from the third update, the landing (436) on the floor below. Confirmed (runtime), run
  r-20261124-chars-railing-vault, the hangout railing below; that the switch is `Human_SnapToGround`'s miss starting the
  fall is inferred.
- **The first clip runs during the move.** The same call sets the move to the start point (`0x0023d2b8`: over 1/60 s,
  or 1/15 s when the turn `0x0023cf88` takes 1/30 s) and installs the three clip tasks (`0x00105990`, `0x001754e8`),
  so P1's clock starts on the climb's first update, not after the move. Confirmed (code). At runtime (the slot 7
  fence from a run, [Feel comparison](feel.md)) 440 lasted 11 updates: the start update still at run speed, the two
  snap updates (1.11 and 1.17 m), then 8 updates of the clip's root motion at full speed from the first (5.79 m/s
  falling to 0.36 m/s), with no fade-in. The first update of 441 and of 442 moved the body 0 m. Confirmed (runtime), run
  r-20261124-chars-fence-climb.

**What gets climbed**, from the rules above (heights above the feet; inferred from the code; the three rows marked
runtime were seen: confirmed (runtime), run r-20261124-chars-trashcan-climb; confirmed (runtime), run
r-20261124-chars-car-climb; confirmed (runtime), run r-20261124-chars-fence-climb):

| The obstacle | Just behind it (0.4 m past the face) | Climb |
| --- | --- | --- |
| reaches 0.69 m but not 1.7 m | ground lower than 0.25 m | short fence (443-448) |
| reaches 0.69 m but not 1.7 m | a top at 0.7-1.7 m | short wall (455-460); runtime: a trash can (1.28 m) and a car (1.48 m) |
| reaches 1.7 m | a top at 1.7-2.91 m | wall (449-454); runtime: a roof 2.64 m above the trash can |
| reaches 1.7 m, lower than 2.5 m | ground lower than 0.25 m | fence (437-442); runtime: a street fence, from a run |
| lower than 0.69 m, or a top outside the windows, or a fence of 2.5 m or more | | no climb |

**At runtime** (the street; stick and triangle through patched pad input). Confirmed (runtime), run
r-20261124-chars-fence-climb (the other rows: runs r-20261124-chars-fence-tap3, r-20261124-chars-trashcan-climb and
r-20261124-chars-car-climb):

| Climb | Input | What happened |
| --- | --- | --- |
| fence from a run (440, 441, 442) | stick 1.0, triangle tapped every 3 updates | the tap 4.80 m from the face did nothing (no climb, and no jump: the 5.5 m climbable check); the one 4.02 m away started 440. The body was **snapped** 2.02 m forward in two updates (29-31 m/s), about 2.0 m less than the tap's distance, then 440 carried it until it stopped against the fence 0.48 m from its face (its walking sphere); 441 (15 updates, 0.5 s, record `+0x08` `0x40`) carried it **through the fence** at 2.4-3.6 m/s with the feet at ground height; 442 (13 updates, 0.43 s, `0x80000`) and the run went on; 39 updates (1.3 s) in all |
| the same, per update (slot 7; the tap 4.28 m from the face started it, one at 4.80 m did nothing) | stick 1.0, triangle every 2 updates | 440 for **11 updates**: the snap moved 1.11 m and 1.17 m, then the clip carried the body until it **stopped against the fence, 0.48 m from its face** (its walking sphere); 441 for 15 updates through the fence at 2.4-3.6 m/s; 442 for 13; the run's gait blend 39 updates after the tap |
| short wall and wall, per update (slot 8) | stick 0.5, triangle | the rise at 456's start took **2 updates** (1.12 m, then 0.17 m by the ground snap); at 450's start 2.30 m, then 0.097, 0.097, 0.097 and 0.052 m to the roof (2.645 m in all) |
| short wall, standing (455, 456, 457) | stick 0.5, triangle | 455 for 8 updates (0.27 s, `0x80000`); at 456's start the feet **jumped** 1.06 m forward and rose **1.29 m** onto the trash can over two updates (1.12 m, then 0.17 m by the ground snap); 456 for 0.63 s (`0x40`), 457 for 13 updates (0.43 s), then the walk |
| wall, standing (449, 450, 451) | stick 0.5, triangle, on the trash can | 449 for 12 updates (0.40 s); at 450's start the feet jumped 0.98 m forward and 2.30 m up, and the ground snap settled them on the roof (2.64 m above the start) over the next updates; 450 for 1.4 s, 451 for 0.37 s |
| short wall onto a car | stick 0.5, triangle | as the trash can, 1.48 m up |
| short fence over the hangout railing (443, 444, then the fall), per update | stick 0.5, triangle every 2 updates | below |

**The hangout railing** (confirmed (runtime), run r-20261124-chars-railing-vault, scenario `railing_vault`: level95,
Cleon put on the clubhouse balcony at (−172.0, 98.3, −194.38) facing +y, 0.63 m from the railing's face, stick 0.5
straight up from update 60, triangle every 2 updates from 66). The railing is 0.90 m tall, material 7 (`LAMPOST`) with
triangle flag `0x80`, its face at y ≈ 98.93, and the floor behind it is 3.20 m lower
([Collision](collision.md#hangout-railings)):

| Updates | Clip, record `+0x08` | The body |
| --- | --- | --- |
| 61-66 | 413, `0x10000000` | the walk start stops against the railing at y 98.44 (0.49 m from the face) |
| 67-79 (13) | 443, `0x80000` | 0.04 m back toward the start point, then forward to 98.443 |
| 80-98 (19) | 444, `0x40` | through the railing at up to 1.64 m/s, y 98.443 to **99.326** (0.40 m past the face), the feet **at the balcony's height** all the way (z −194.378) over the drop |
| 99-118 (20) | 428, `0x400000` for 99-102, then 0 | the fall: z held for 2 updates, then `vz` −0.5227 per update; at 102 pushed 0.17 m on to y 99.50, clear of the railing's back; 445 never plays |
| 119 | 436 | landed on the floor below (z −197.576), 3.20 m down, 0.57 m past the face; then the landing and the walk |

So the railing's triangles do not stop the body during 444, though material 7 is not among the materials
`Human_OnContact` skips: the wall sweep is not run at all ([above](#climb)).

The vertical part of a climb is the single move at P2's start: the feet do not rise during P1 or P2.

**In an input script**: standing, `stick left 0 50` toward the obstacle and `tap triangle` within reach; from a run,
`stick left 0 100` and `tap triangle` when the face is 4.5 m away or less.

#### Parked cars are shells in the level mesh {#car-shells}

Nothing builds collision triangles from a car: `Car_MakeBodies` (`0x00387d50`) gives a car one body with two boxes
sized from its model's record (`0x0057e4c0` + model × `0x5f0`), and the only code that switches mesh triangles is the
script's `ChangeCollision` (`0x00379c20` → `CollisionMesh_SetEnabledInVolume`). Confirmed (code). The level files
carry the shells. `level99_2`'s mesh has three closed car-shaped boxes, the number of cars `SetupCars` spawns
(`car_coupe`, `car_osedan`, `car_osedan01`), so they are inferred to be these cars. Two are 5.74 × 2.2 m, with the
sill at 1.18-1.30 m, the roof at 1.71 m and sloped screens; one is 5.11 × 2.3 m with the roof at 1.67 m. All are
material 0. Every triangle has bits 0, 2 and 3 (low bits `0x00d`) and a top value of 3-15: the long sides `0xf00d` /
`0x700d`, the ends `0x400d` / `0x700d`, the roof and bonnet `0xc00d` / `0xd00d`. No triangle has bit 7 (`0x80`). Read
from the disc. Across the disc's levels, 2,530 triangles with these low bits and material 0 sit in 19 levels. So a
car's climbable surface is wherever the level put a shell, not where the car is: a car that `CreateCar` or `CarSpawn`
places away from a shell, or one moved off its shell, has no mesh surface for a walker, a climb probe or a ray (rays
and climbs test the level mesh only, `WorldManager_RayCast` `0x0040db88`). Bit 2 is the player-climbable bit
([Collision](collision.md#triangles), and what stops a walker at a car is on [Physics](physics.md#layers)); the climb
itself is above.

## In Coney {#coneys-implementation}

- `src/human/stamina.*` is the stamina meter ([Sprint and stamina](#sprint)): the drain at the sprint gait, the
  refill (the maximum and the refill each human's power class's, [Stats](characters-classes.md#stats-coney)) and the
  gait, airborne and "run with L2" blocks, and `Player_UpdateSprint`'s rule (L2 held and stamina not 0, asked again
  every update).
- `src/human/jump.*` is the jump's checks and launch ([Jumping](#jump)): faster than 3.3 m/s and the stored gait at
  jog or above, refused when a climbable face lies within 5.5 m; the run or sprint speed forward and 5.5 m/s up.
  A start clip does not stop it.
- `src/human/climb.*` is the climb choice ([Climbing](#climb)): the two forward rays, the climbable test, the probe
  0.4 m behind the face with its windows, the 2.5 m fence ray, the clip ids, each clip's reach (its type-8 event
  vector) and the reach window.
**Disc test** (`[traversal]`, positions and hashes only; Rembrandt on the sandbox's
[parkour course](../guides/sandbox.md#the-shipped-layouts)), each run against the runtime values above:

- **Sprint** (L2 held, stick 0.8 then 0.96): a walk at 1.629 m/s and no drain at 0.8; 10.245 m/s at 0.96 for about
  6.75 s until stamina is empty, then 7.801 m/s in one update; stamina held at 0 while L2 stays down and refilled at
  40 per second once it is let go; letting go of the stick at the run plays the run stop (417) for about 24 updates.
- **Jumps**: a run jump at 7.801 m/s and a sprint jump at 10.245 m/s, both 5.5 m/s up, rising 1.06 m and landing in
  436; the sprint jump carries about 8.5 m (24 updates in the air and the landing update); letting go of the stick in
  the sprint plays the run stop (417). A run jump clears a 5 m gap between two 2 m platforms, the feet ending one
  update below the far platform's top before they land on it.
- **Fences** of material 30, from a run: the 1 m short fence (446) and the 2 m fence (440) are climbed with the feet at
  ground height, through the fence; the 2.6 m fence is neither climbed nor jumped, and stops the body at its face.
- **Walls and blocks** with flag `0x80`, standing: the 0.75 and 1.0 m walls are short-wall climbs (455) onto their
  tops; the 2.0 m block is a wall climb (449) onto its top; the 3.0 m block is not climbed.
- **Kerbs and ledges**: a 20 cm kerb is walked onto; 30, 50 and 65 cm ledges stop the body at their face.
- **The jump's gait test** reads the stored gait (`+0x1a8`, the nearest) where the code reading names the "reached"
  gait: the runtime jump 7 updates into the run start, at 3.35 m/s, passes only with the stored gait.
- **Stamina** carries one fraction of a point between updates, started again when the meter changes direction.
- **The air turn** is limited to 4° an update, what a sprint jump turned at runtime; which gait's limit the original
  uses is not traced.
- **Climb reach**: `r1` and `r2` are read as the reaches of the standing and the running first clips (for the fence,
  437 and 440). Rembrandt's type-8 vectors are 0.77 / 2.31 m (fence), 0.64 / 1.39 m (short fence), 0.65 / 2.30 m
  (wall) and 0.78 / 1.55 m (short wall), which give a running fence climb from 1.23 to 5.09 m: the runtime tap at
  about 4.4 m started one and the one at 4.9 m did not (the 4.5 m ray). The chain's first two clips would give
  about 1.2 m and no running climb from 4.4 m.
- **The climb's move** to its start point takes 2 updates from a run and 1 standing; the first clip's clock runs
  through it, as the original installs the clips with the move, but its root motion moves the body only once the
  move is done. On the sandbox's 2 m fence 440 lasts 11 updates from the tap's, as at runtime.
- **The rise**: walls and short walls move at once at the second clip's start, by its root displacement (450 and
  456 carry none in section A; their whole displacement, 0.98 and 1.06 m, as at runtime) and up to the top the probe
  found; fences go by the second clip's own root motion, as the runtime's 441 carried the body through.
- **Climbing over** lasts the second clip: from the re-probe at the first clip's end to the third clip's start (at
  runtime 441 had record `+0x08` `0x40` and 442 did not). Meanwhile the fence materials (30, 31, 122) are not walls,
  and not ground for the snap and the landing, so a low fence's top does not lift the feet. **This differs from the
  original and breaks the hangout railing** (checklist item 46): the original runs no wall sweep and no ground snap
  while `0x40` is set, so a climbed face of any material lets the body through, but Coney still sweeps the walking
  sphere with only 30, 31 and 122 left out. The hangout's railings are material 7 with flag `0x80`
  ([Collision](collision.md#hangout-railings)), so 443 and 444 play and the sweep holds the body 0.48 m in front of the
  face: the vault ends on the same side. A climbed face that is any other material with flag `0x80` or `0x4` (car
  shells are material 0) does the same. And had the body passed, Coney's snap would find no ground over the 3.2 m
  drop part-way through 444, so the climb would end in the idle at once (`followClimb`'s airborne end) instead of the
  fall at 444's end. **Fix:** while climbing over, skip `sweep` and `snapToGround` and move the feet by the velocity
  and the clip's root motion, height included (`Human_Move`'s `0x40` branch); from the third clip on, move as now,
  and when the snap then finds no ground start the fall (anim state 26, the fall clip, 428 at runtime) in place of
  the idle and the third clip. **Test:** a synthetic railing of material 7 with flag `0x80`, 0.9 m tall and
  one-sided toward the player, on the edge of a floor with another 3.2 m lower behind it: the player standing 0.6 m
  from it, the stick at 50 % toward it and triangle, plays 443 and 444, is past the railing's back at 444's end with
  the feet still at the upper floor's height, never plays 445, then falls and lands on the lower floor beyond the
  railing; the same railing over level ground ends on the far side with 445 played.
- **The re-probe** at the first clip's end accepts any triangle, not only a climbable one.
- **The climb's fade**: none; the first clip moves the body at its full root speed from its first update after the
  move (5.79 m/s on the 2 m fence, as at runtime). The update the chain goes on to its second or third clip moves
  nothing, as the first update of 441 and of 442 did at runtime; why is not traced.
- **Context and object actions** (triangle's second and fourth tries) are hooks that never succeed yet, so triangle
  goes on from a refused climb to the jump.
- **Camera through a fence**: the follow camera's ray passes through material 30
  ([Camera](camera.md#coneys-implementation)), so it keeps its distance through a fence climb as the original's does.

### Notes for implementers {#notes-for-implementers}

- **Sprint** is L2 **held** with the stick above 0.95: cleared every update and set again while L2 is down and
  stamina is not 0. Stamina drains 20 per second at the sprint gait and refills 40 per second (Rembrandt: 135), not
  while running with L2 held ([Sprint](#sprint)).
- **Triangle** tries a climb (stick above 0.12), then a context action, then a jump (stick above 0.95, faster than
  3.3 m/s): 5.5 m/s up, the run or sprint speed forward, steering in the air ([Jumping](#jump)).
- **Climbs** are chosen by two forward rays at 0.69 and 1.7 m and a downward probe 0.4 m behind the face; the feet
  rise in one move at the second clip's start ([Climbing](#climb)).

## Open questions

- **The context action** (`0x002811f0`) and the object action that triangle also starts (partly answered,
  [Triangle with something in hand](#triangle-held)): the object action is the drop of what is in hand; the context
  action is the context records and the pick-up search ([Crimes](crimes.md#triangle)). Doors and which objects are
  still open.
- **The air turn's gait** (partly answered): 4° per update from a run and a sprint alike, which is the non-player
  column's run value at `0x005101c4` (inferred: `Human_AirControl` reads the other column); not read in the code.
- **Climb reaches** (partly answered): Coney reads Rembrandt's type-8 vectors ([Coney's
  implementation](#coneys-implementation)); still open: which two clips `r1` and `r2` belong to (Coney's reading: the
  standing and running first clips), and which clip's reach `Climb_Start`'s "too far by less than twice the reach"
  swallow uses ([Triangle with something in hand](#triangle-held)). Answered: `CfgClimbWithGhetto` keeps the ghetto
  blaster (held set 6) in hand through a climb; without it the climb drops it, and always drops a set-4 object
  (`Climb_Start`, `0x00281c20`, confirmed (code)).
- **The climb's details** (the first clip's timing answered: it runs during the move to the start point, with no
  fade, [Climbing](#climb)): when record `+0x08` `0x40` is cleared; whether the re-probe needs a climbable triangle;
  how a fence climb's second clip moves the body (Coney: its root motion).
- **The camera during a fence climb** (answered: the camera's rays exclude materials 30, 122 and 107,
  [Camera](camera.md#collision)).
- **Triangle flags `0x4` and `0x80`**: why two climbable flags (one for players only), and which surfaces carry them.
- **The sprint's other clear** (answered: the block, [Combat](combat/index.md#dispatch)). Still open: what `0x00230140`,
  called when the sprint is set, does, and the fight test `0x00224f28`.

## How this was checked

Code claims were read in Ghidra in `SLUS_212.15`.

The street is too short to drain 135 points of stamina, so the sprint run (scenario `chars_sprint`) sets stamina
to 6 once, at update 50, and lets the drain take it to 0.
