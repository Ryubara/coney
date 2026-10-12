# AI moving: gaits, path planning, routes and steering {#moving}

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra;
every claim is confirmed (code) at the cited address unless it says otherwise, as on [AI humans](ai.md), which this page
was split from because that page had grown too large. The overview of the brain, its goals and its actions stays on [AI
humans](ai.md#brain).

An AI moves by writing brain fields, never a stick: `+0x110` the heading to move along, `+0x114` the speed (through
`0x0028ab28`; `0x0028aac0(brain, 0)` stops), `+0x90` the point aimed at and `+0x118` its radius; the human's control
(`Human_UpdateControl`) moves it from those: speeds, turn limits, start and turn clips are on
[Characters](characters.md#ai-locomotion). A gait's speed is `Human_SpeedForGait` (`0x0022ae40`): 1.6286 m/s walking,
7.8012 m/s running. Confirmed (runtime): the civilian's per-player stick magnitude stayed 0 through its move and
move-to-human actions; only the attack action wrote a stick (1.0, for throws).

From 8 m behind the player (confirmed (runtime)): a turn on the spot (clip 398), the run start 414, the run 410 at
7.80 m/s, then at 1.29 m a charge (`0x20`). Closer, the move-to-human action walks in the fight-stance clips 372-380 at
about 2.3 m/s.

## Gaits and speeds {#ai-gaits}

An AI never writes a stick: a goal or action sets the brain's move speed `+0x114`, usually from a **gait**, and the
human's control turns that into velocity ([Turning](#ai-turn), below for the speed). Confirmed (code) at the
addresses cited.

- `Brain_SetMoveGait(brain, gait)` (`0x0028aac0`): speed = `Human_SpeedForGait(human, gait)`, 0 for gait 0;
  `Brain_SetMoveSpeed` (`0x0028ab28`) stores any speed directly (FollowFormation, DevilRun, RunCarrot set speeds
  between gaits).
- `Human_SpeedForGait` (`0x0022ae40`, jump table `0x0055bec0`) reads the human's anim record (`+0xd4`) speeds
  ([Speed classes](characters.md#speed-classes)) without the `+0x3a4` multiplier; a human with no anim record gets 0.
- `Human_GaitForSpeed` (`0x00221760`) maps back: 5 at or above the sprint speed, 4 at or above the run, 3 the jog, 2
  the walk, else 0. The move action and the steering use it to tell standing from moving.

| Gait | Name | Record field | Rembrandt (m/s) |
| --- | --- | --- | --- |
| 0 | stand | - | 0 |
| 1 | sneak walk | `+0x16c` | 1.585 |
| 2 | walk | `+0x170` | 1.629 |
| 3 | jog | `+0x174` | 4.857 |
| 4 | run | `+0x178` | 7.801 |
| 5 | sprint | `+0x17c` | 10.245 |
| (stance) | fight-stance walk | `+0x164` | 3.429 (clip 380; 372 for an AI with turn boost ≤ 0, [brain bytes](ai-gangs.md#brain-boosts)) |

The speeds are each class's own clip speeds; Rembrandt's are confirmed (runtime) on [Characters](characters.md).

**The gait each goal asks for** (the gait argument of its move actions; confirmed (code) at each Process):

| Gait | Goals |
| --- | --- |
| the script's argument | MoveToFlag (1), MoveToFlagNetFlag (3), MoveToUseFlag (4), MoveToPosition (5), MoveToHuman (6), TravelPath (`0x38`), Wander (`0x3a`), DestroyItem, DestroyCar, Patrol (`0x73`) |
| 2 (walk) | pedestrians and their flags (`0x69`, PedFlag, AreaWalker, Hooker, Peddler), StandIdle, Mark, Scout, PathScout, Backoff, Ring, StandGround, Confront, CopPatrol, CopInteract, CopperGuardArrested, BigThrower's hold, the walk back of HoldPosition |
| 3 (jog) | ChaseSupport (3 m), GetItem within 3 m of the object, BigThrower, StationaryShooter's return, Hide near the leader |
| 4 (run) | EngageEnemy, [GoalRiot](ai-scripting.md#riot), Investigate, Respond, RiotCop, Tag, GrabTarget, Mace, Grabber, BigDefender, LeftTurf (back into the turf), FollowAndAttack / FollowAndDefend beyond reach, the Melee goal's GetItem |
| 5 (sprint) | Scatter, RunFromTrain, CallGang, Hide away from the leader; EngageEnemy after a running target while stamina is above 50 % ([Sight](ai.md#sight)) |
| chosen | Shadow: the leader's gait (2 at first, 3 beyond 50 m); AvoidEnemies: 5, or 4 below 50 % power, or the goal's own; FollowFormation and FollowObject: a speed between walk and 1.25 × sprint ([follow](ai-gangs.md#warrior-follow)); Spectate: 2, or 4 when the watched man runs |

**Speed control** (`Human_UpdateControl` `0x00243848`, the non-pad branch), each update, with `v*` = brain `+0x114`:

1. `Brain_UpdateGait`; a gait change rebuilds the human's move anims.
2. The wanted speed `w`: wounded and `v*` > 0.1 → the walk speed; in the fight stance and `v*` > 0.1 → the stance
   walk (`+0x164`), else 0; otherwise `v*`, at most 25 m/s. Running faster than the run speed with a held object of
   kind 4 or 6 **drops it**, except an object of type class 6 whose `+0x5a` is 0.
3. **Starting** (current speed 0, `w` > 0): unless the anim set's kind is 3 or 7 or its `+0x18` is 4, the human's
   action becomes 6 (a run start) when `w` ≥ the run speed, 8 instead with the start boost `+0x0c` above 0, or 5
   (the walk start) when `w` is exactly the walk speed; the first update's speed is min(`w`, 2 m/s).
4. **Accelerating**: + 8 m/s² × the step (0.267 m/s per update at 30 Hz), or + 32 m/s² (1.067 m/s per update) while
   more than 2 m/s short, never past `w`.
5. **Slowing**: − 32 m/s² (1.067 m/s per update) down to `w`. Diego and Vargas (classes `0x77`, `0x78`) running or
   sprinting with `w` = 0 stop at once with action 9 (a stop clip).
6. **Far away** (detail level `+0x333` 2 or more, [The enemy scan](ai.md#enemy-scan)) and no held flag of `0x310c0880`:
   velocity = `w` along brain `+0x110`, and the heading is **set** to `+0x110` at once (no turn limit, no clips).
7. Otherwise the turn ([Turning](#ai-turn)). **Stopped** (`w` = 0): when the heading error is above 15° and no
   flag is held, a turn on the spot starts (`Human_StartTurnOnSpot`, the clip is [Human](characters.md)'s); within
   15° nothing turns. A rear grabber turns at twice the limit and walks.

## Turning {#ai-turn}

A human with no pad (per-player `+0x1b` = 0) is moved by `Human_UpdateControl` (`0x00243848`, the default control
`Human_SetUpdateControl` `0x00227c48` installs), not by the stick code (`Human_PlayerLocomotion` `0x00240e38`). Each
update it turns the human's heading toward brain `+0x110` by at most `Human_MaxTurn` (`0x002213d8`), **linearly**:
the eased turn of `CfgTurnRate` (`0x00510308`-`0x00510310`) is read only on the pad path, in the air and while
grabbing, so it does not apply to the AI. While moving faster than a jog, a large heading change may instead start a
turn clip (`0x0025c5b8`, [Human](characters.md)); the switch `0x005104e8` is 1. Confirmed (code).

`Human_MaxTurn` picks one word of a pair at `0x005101b0` ([Movement constants](characters.md#movement-constants)):
the player's word for a pad human whose brain is not dead, the **AI word** otherwise. In order:

| State | Pair | AI word per update | Boost applies |
| --- | --- | --- | --- |
| turn-limited (two human flags) | fixed | 4° or 6° (both kinds) | no |
| wounded | `0x005101b0` × 0.25 | 0.375° | no |
| grabbing, blocking or ducking | `0x005101b0` | 1.5° | no |
| sprinting (gait 5) | `0x005101b8` | 2.5° | yes |
| running (gait 4) | `0x005101c0` | 4° | yes |
| jogging (gait 3) | `0x005101c8` | 6° | yes |
| fight stance | `0x005101d8` (AI word `0x005101dc`) | 24° | no |
| held flags `0x1000080` | `0x005101b8` × 0.25 | 0.625° | no |
| otherwise (walk, stand) | `0x005101d0` | 12° | yes |

"Boost applies": the gait rows go through `Human_GetTurnRateForGait` (`0x002212d0`), which multiplies the AI word by
the [turn boost](ai-gangs.md#brain-boosts) `+0x0b` (× (b + 1), or ÷ (1 − b) when negative); the other rows do not. At 30
Hz a walking AI turns 360°/s, a running one 120°/s, unboosted. Confirmed (code); the AI words are `.data` values that
`CfgSetTurnRates` does not change: it writes only each pair's first (player) word, so its sixth argument is the
player's stance turn (`0x005101d8`), and an AI's is always `0x005101dc`'s 24° ([Characters](characters.md#ai-locomotion)).
The rows are tested top down, so a wounded AI turns 0.375° per update even in a fight stance.

## The move action {#move-action}

Vtable `0x00542f20`, confirmed (code) at the addresses cited. `MoveAction_Init` (`0x002fb9e8`) takes the radius, the
point, the gait (its speed goes to `+0x40`), an option bit, a start delay, an object to face (`+0x3c`, 0 for none) and
a "flag kind `0x12`" bit.

- **Start** (`0x002fc420`; Ghidra has no function there): done (2) when `0x00223b70` holds or the human is within
  0.1 m. Otherwise it resets the avoidance state (`0x002890a8`), writes the point and radius to brain `+0x90` /
  `+0x118` and asks for a route ([Path planning](#path-planning)): none needed → it steers straight at the point;
  none possible → done (2).
- **Update** (`0x002fc5c0`), each update:
    1. Done when `0x00223b70` holds or the abort bit is set; wait while the human is busy or an avoidance wait runs.
    2. Every 30 updates (offset by the human's index), with a route: when the final point is in sight
       (`0x002221e0`), drop the route and go straight.
    3. With a route: done within the radius; aim at the route's current waypoint ([Following](#route-follow)).
    4. **Dynamic obstacles** (`0x00414188`, 4 records of `0x150` at `0x006f3a10`): a blocked segment → speed 0 and
       a 1000 ms wait.
    5. Every 31 updates (counter `+0x4d`) the **current aim** (the route's waypoint, or the destination without a
       route) must still pass `Human_CanWalkStraightTo` (`0x002221e0`), else done (2) with `+0x284` left at 0, so the
       goal plans again. Done (2) when the aim is within 1 m in plan, more than 1.5 m away in 3D, the human is not
       airborne (`0x00227f90`) and the direction to the aim lies within 20° of straight up or down (a point on a
       floor above or below). Confirmed (code) at `0x002fc854`-`0x002fc984`.
    6. **Steering** round other humans ([below](#steering)) may replace the aim by a detour point (radius 0.3).
       Steering is not reached on every update; see "The speed and the legs" below.
    7. Heading = the direction to the aim, or to the object `+0x3c` when set; write `+0x90`, `+0x118` and `+0x110`.
    8. **Speed**: with an object to face, the gait's speed. Otherwise turn on the spot (speed 0) while stopped and
       more than 30° off; else the **corner speed** ([below](#corner-speed)): `+0x0c` while the squared distance to
       the aim is at least the braking distance² `+0x48`, else `+0x44`, never above the gait's speed.
    9. **Stuck** (`MoveAction_CheckStuck` `0x002fc330`): while the asked speed is above 0 a counter (`+0x4e`) counts
       updates (speed 0 resets it); on the 61st, less than 0.2 m covered since the saved point, measured in **3D**
       (`Vec_DistSq`, height included), → brain `+0x284` = 3, done; else the point is saved again and the count
       restarts. A climb up a slope counts its height. Confirmed (code). An AI walking a heap too steep for floor
       is not braked by its wall slides and covers about 1.1 m per 60 updates there
       ([Characters](characters.md#slopes)), well clear of the test.
- **The speed and the legs** (confirmed (code) at `0x002fc778`-`0x002fc7a8` and `0x002fc988`-`0x002fc998`):
    - **Where the speed comes from.** Just before the route step, the update takes its speed: the steering override
      (`+0x34`) while its count `+0x32` is non-zero, else the action's own `+0x40`. That one value goes by pointer to
      `Route_Follow`, which may change it, then to the steering (step 6), and as the cap to the corner speed
      (step 8).
    - **When the override's count drops.** Only at the start of the steering call. An update that returns before
      step 6 does not use one up.
    - **Route_Follow's result.** With a route, any result but 1 ends the update there, before the steering, and is
      the action's result. It returns:
        - 0 while held at a claimed node within 2 m ([Steering](#steering) step 15);
        - 0 on each update of a climb leg (`Route_ClimbLeg`: trying, or once the climb starts), and 2 after 31
          failures;
        - 0 or 2 for a jump or drop leg (`Route_StartJumpLeg`);
        - 0 for a charge (`Route_SmashLeg`);
        - 2 for a refused link;
        - 0 when `Route_TryClimbNear` starts an early climb.
    - So **steering never runs on climb, jump or charge legs**, nor while held at a node. It runs on walked legs
      (kinds 1, 2 and `0x10`) and on the walk up to a climb. Once a climb has started (`+0x12` = 0), the climb itself
      holds the human (step 1's busy test, inferred).
- Brain `+0x284` says why a move failed: 1 no polygon or no route, 2 or 4 an edge it cannot take, 3 stuck.
- **Nothing keeps a move going between actions** (confirmed (code)). `Brain_UpdateGoals` (`0x0028fbb0`) starts
  every think with `Brain_StopMove` (`0x0028ac18`): speed `+0x114` 0 (`Brain_SetMoveGait(brain, 0)`) and heading
  `+0x110` = the human's facing; only a move action's Update (step 7 above) writes `+0x90`, `+0x110`, `+0x118` and
  the speed back, each update. **Abort** (`0x002fc560`) sets the abort bit, ends the avoidance (`0x00289108`) and
  frees the route (`0x0029aa20`); it writes none of the move fields, and **destroy** (vtable `+0x24`, `0x004eef40`)
  is empty. So when a move is replaced (`GoalMoveToFlag`'s Resume, `0x002da550`: `Brain_ClearActions`, then
  `Human_DropTarget` `0x00226f70`, which only clears the target) the human is asked to stand for every think in
  which the new action is still counting its start delay: `Action_Update` (`0x002f9e48`) takes the time since the
  brain's previous think (`+0x30`) off the delay, the first time too, and returns before the class's Update while
  any is left. `GoalMoveToFlag`'s delay is `Random_IntRange(0, 250)` ms, so a draw within one update (about 33 ms)
  starts the move in the same think with no gap, and a longer one leaves up to 7 thinks at speed 0; the control
  slows a walk to rest in two updates and restarts it at once at the walking speed
  ([Characters](characters.md#ai-locomotion)).

## The corner speed {#corner-speed}

How fast a routed AI may take the next corners. Confirmed (code) at the addresses cited; not measured.

- **When** (the move action's speed step): recomputed when the counter `+0x4c`, which counts down each update, runs
  past 0, or when the aim has moved more than 0.07 m (squared 0.005) from the aim saved at `+0x20` at the last
  recompute. After a recompute the counter is 10 when the first corner's speed is above 0, else 0, and the aim is
  saved. So about every 11 updates while moving, and at once when the waypoint changes.
- **The points** (`MoveAction_CollectPoints` `0x002fbd18`, at most 3): the human's position; the steering's detour
  point when one is running (brain `+0xc4`); then the route's waypoints from the current one, **claiming each**
  ([Waypoint claims](ai-pathing.md#route-claims)): a waypoint entered by a kind-4 or `0x80` link stops the list before
  it, one entered by a link whose flags are exactly `0x10` or `0x40` is the last one listed; then the destination when
  there is room.
- **The two corners** (`MoveAction_CornerSpeed` `0x002fc158`), with *v* the human's speed (`+0x1ac`) and *c* the
  move's speed cap:
    - corner 1 is points 0 → 1: when they are more than 0.35 m apart (squared 0.1225), `+0x0c` =
      `MoveAction_TrialSpeed(v, c, points 0 and 1)`, else `+0x0c` = *c*;
    - corner 2 is points 1 → 2 (only with 3 points, and when more than 0.35 m apart): `+0x44` =
      `MoveAction_TrialSpeed(v, +0x0c, points 1 and 2)`, else *c*;
    - **the braking distance**: when `+0x44` < `+0x0c`, with *t* = (`+0x0c` − `+0x44`) / 32 (the time to shed the
      difference at 32 m/s², the AI's braking rate), *d* = `+0x0c` × *t* − 16 *t*², and `+0x48` = *d*². Otherwise
      `+0x48` = 0 (the second corner's speed is never used).
- **`MoveAction_TrialSpeed(v, trial, from, to)`** (`0x002fbef0`):
    1. the human's right vector (his facing crossed with up, `0x00511740`), and
       `Move_CornerSpeedLimit` (`0x002fcd90`): the radius of the circle tangent to the facing at *from* through *to*
       (`Math_CircleRadius2D`), the speed whose per-update turn at that radius is the gait's turn step
       (2*R* sin(ω/2) × 30), stepping the gait down while a lower gait allows as much; the trial is the smaller of
       that and *trial*, but at least the walking speed;
    2. sub-steps per output point *k* by the current speed *v*: 4 above 7 m/s, 5 above 5, 6 above 3, else 7; times
       (2 × detail + 1); the output points number 10 − detail (the detail level `+0x333`);
    3. `Human_SimulateMove` (`0x0022aae8`) plays the walk toward *to* one update at a time: the speed moves toward the
       trial (−32 m/s² × 1/30 down; +8 m/s² × 1/30 up, +24 more while over 2 m/s short; from rest it starts at
       min(trial, 2)), the heading turns toward *to* by at most the gait's turn step (`Human_GetTurnRateForGait`,
       boost included), and the human moves speed/30 along it; it records a point every *k* sub-steps and stops early
       once the heading is within 0.005 rad of the direction to *to* or *to* is within one step;
    4. each recorded point must have an area and a clear line to the next (`Nav_FindArea`, `Nav_LineClearWithin`
       `0x0024f718`); a failure lowers the trial by 1 + 0.75 × detail and simulates again, until it is at or below
       the walking speed, which is then returned; the first trial that passes is returned.

So a running AI brakes for a tight corner by the time the arc would leave the path polygons, and starts braking for the
second corner at the distance a 32 m/s² stop needs.

## The move-to-human action (MoveMelee) {#move-melee}

Type 2, `MoveMelee` (vtable `0x005431e0`), made by `MoveToHumanAction_Init(near, far, action, brain, human, delay,
limit)` (`0x002fcf50`): the fight's footwork, which keeps a distance band from one human. The fight goal's approach,
[the reposition](ai.md#fight-reposition), Spectate and Backoff use it. Confirmed (code) at the addresses cited.

Fields: `+0x0c` the human's handle, `+0x10` / `+0x14` near² / far², `+0x18` the side counter, `+0x1c` the radial
counter, `+0x20` the time limit (made absolute at Start), `+0x24` "no stance" (class `+0x11b` 6 or 7,
`Brain_IsKind6Or7` `0x002fd128`), `+0x25` the saved turn boost.

- **Start** (`0x002fcff0`): the limit becomes now + limit; a "no stance" human leaves the fight stance; the turn
  boost is saved. **Abort** (`0x002fd068`, never refused): a "no stance" human turns to face the human; the turn
  boost is restored.
- **Update** (`0x002fd4e8`), each update, with `d²` the squared distance:
    1. Done (2) while the mover is in a standing reaction, or when the human is no longer up.
    2. With the radial counter at 0 and the limit not passed: `d²` < near² → counter −10 (back off); `d²` > far² →
       +10 (close in), and when also within the mover's far melee range (`+0x140`) the turn boost is raised by one
       (with conditions on the two facings, the human's state and held objects).
    3. With the side counter at 0 and the limit not passed, the side (`MoveMeleeAction_PickSide` `0x002fd158`): 2 →
       side counter +10 (circle one way), 6 → −10 (the other way), both clearing the radial counter; 8 → none.
    4. The move direction: radial counter > 0 → toward the human, gait 4 (run; 2 for "no stance"); < 0 → away
       from him, gait 2; each update the counter steps one toward 0. A side counter adds ±1 × the mover's right
       vector (gait at least 2) and steps toward 0 likewise.
    5. **Done** when `d²` lies in the band, the side counter is 0 and |radial counter| < 6. After the time limit,
       done once both counters are 0.
    6. Brain `+0x110` = the heading of the move for a "no stance" human; otherwise the heading **to the human**
       (confirmed (code), the store at `0x002fd9b4`), so he turns to face him at the stance rate while the stance
       walk strafes him along the move. The aim point `+0x90` = his position + 2 × the
       move direction, radius 0; brain `+0x11c` (strafe) = not "no stance"; the gait goes to `Brain_SetMoveGait`.
- **The side** (`0x002fd158`, from the mover's [sector record](#neighbour-sectors), at most 1 s old): keep circling
  while the sector the mover is heading into is free; else prefer whichever of sectors 2 and 6 (beside the human) is
  free and less crowded (`Sectors_GetCost`: flags + 2 × humans); with both free, the side by the human's flag
  `0x10000`. When the two sides cost the same: straight in (8), unless the human is grabbed from the rear or grabbing
  from the rear; then, when the human is a threat, circle toward his back (sector 4, or 0 for a rear grab), raising
  the turn boost by one when the mover is one of his active attackers.

So a waiting attacker backs off over 10 updates when too close, runs in over 10 when too far, and circles 10 updates
at a time to a free side, facing his man.

## Path planning {#path-planning}

The route request (`0x0029a8c0`), with its state at brain `+0xe0` (`+0x04` the route, `+0x08` the waypoint radius,
`+0x0c` a state, `+0x10` the waypoint index, `+0x14` an edge mask). Confirmed (code). It plans over the level's
[path data](level-loading.md#path-data): the **polygons** are the walkable areas, the **C records** the graph's
**nodes** and the **D records** its **edges**. A D record on node N naming node M is the step **M → N** (A\* expands
from the destination, and the follower reads the link from the waypoint before on the waypoint's own records,
`0x00251070`); inferred from both readers. Most links come in pairs, but a jump down is one-way (`level99`: node 184
on the ground holds a kind-4 link from 395 on a roof 4 m up, and 395 none back).

**The edge mask** (`u16` route state `+0x14`, brain `+0xf4`) is **`0xff`** for every human: the route state's
initialiser (`0x0029a388`) sets it, nothing in the move action or `GoalMoveToFlag` changes it, and the goals that do
change it put `0xff` back in their End. So a scripted move admits every link kind on the disc (1, 2, 4, 8, `0x10`,
`0x80`); only `0x100` is outside it. Confirmed (code) at the writers, and at runtime
([Vermin's fence](#route-follow)). The other writers: `0x13` in the Start of the goals at `0x002aafd0` and
`0x002c1470`, `0xbf` (no `0x40`) after a failed move in `0x002ab4b8`, and `| 0x100` for a Warrior (brain type 3) in
`GoalHide`'s Start (`HideGoal_StartMask` `0x002cc908`) and, in `FollowFormationGoal_Start` (`0x002dfe30`), for a
Warrior on the levels whose level-table id (`+0x04` of the `0x84`-byte entry) is `0xe`, `0x33`, `0x34` or `0x52`.
Confirmed (code).

**Mask bit `0x100` is not a link kind.** No D record on the disc has it (the counts below), so it admits nothing. What
it does is switch off the [ClimbFilter](#path-planning) extra costs in the edge cost and the `0x80` re-search in step
5: a Warrior hiding, or following on those four levels, takes climbs at their plain distance. Confirmed (code) at
`0x00251890` and `0x002511c8`; the disc count is a check of the files.

**The link kinds on the disc** (NTSC-U, all 64 `.lev` files' path data, counts only, 2026-10-11; "one-way" is a D
record with no record of the same kind back). The meanings are inferred from the geometry and the follower's handlers:

| Kind | Links | One-way | Length in plan | Height change along the step | Meaning |
| --- | --- | --- | --- | --- | --- |
| 1 | 238,422 | almost none | median 3.3 m | ±9.4 m at most, median 0 | walked |
| 2 | 2,126 | 64 | median 0.64 m | −1.5 to +3.2 m | walked (a short joint, often between two areas; no code treats it apart from 1) |
| 4 | 5,890 | 5,372 | median 0.8 m, at most 15.9 m | 91 % go down, median −1.6 m | jump or drop ([Jump legs](#route-jump)) |
| 8 | 982 | none | always 1.0 m | always 0 | climb over a fence or wall: a pair of nodes 1 m apart, one each side |
| `0x10` | 746 | none | always 1.4 m | always 0 | a door; every one has the avoid bit set in the file |
| `0x80` | 4,730 | all | always 0.8 m | median +1.4 m | climb up onto a ledge; its way back is a kind-4 drop |

No link on the disc has `0x40` or `0x100`; the charge kind `0x40` is given at run time by a breakable door or pane
([Objects](objects.md#nav-links)). Polygon `s16 +0x02` (summed into the area's first path at load) is non-zero in
1,257 of the 1,754 areas, 1,251 of which own route nodes; 60 areas own nodes but have 0 there, so a request between
two areas refuses them (inferred: "on the graph").

1. Find the human's polygon (`Human_GetNavPolygon` `0x00247958`, cached at human `+0x1b4` with its point at `+0x2b0`;
   fallbacks `0x002505b0`, `0x0024e218`; see [A point in a hole](ai-pathing.md#path-holes)). None → `+0x284` = 1, fail.
2. **Straight line first**: when the walkable-line test (`0x0024fbf8`, below) passes from the human's position
   (`+0x2b0`, at its own height) to the point with the mask 0, no route is made and the action steers straight
   (`0x0029a8c0`, the route request). Confirmed (code).
   The destination's area is then `Nav_FindArea` (`0x00250708`) of the point itself, with **no fallback**: none →
   `+0x284` = 1 and the request returns 2 (the move action ends at once).
3. Otherwise both ends must be in one polygon, or both polygons must be on the graph (polygon `s16 +0x02` ≠ 0;
   inferred meaning). Each end's node is the nearest of its polygon's nodes (the A record's count and first index)
   that it reaches in a straight line (`0x00251150` → `0x00250e98`, `0x0024f290`), trying up to 30 by distance.
4. **A\*** (`Route_AStar` `0x00251a70`), from the destination's node to the start's, so the parent links read in
   walking order. Confirmed (code):
    - **Before**: every node's parent (C `+0x1c`) is set to none (`0xffff`, `PathNodes_ResetParents`); the open heap
      and the closed list are emptied. The first node gets g 0, h, f = h and no parent.
    - **The open list** is a binary min-heap of node indices keyed on f (C `+0x16`), at most **1000** entries
      (`PathHeap_Push`; a sift-up moves a parent down only while the new f is strictly smaller, so equal f keep the
      older entry above). **The closed list** holds at most **3000** nodes (`PathList_Add`). Either one full ends
      the search as a failure.
    - **A step**: pop the lowest f, append it to the closed list, save its g and write `0xfabc` into its g (that
      value marks "closed"). If it is the start's node, the search is over. Otherwise, for each of its D records in
      file order, skip the neighbour when its g reads `0xfabc` (closed), when the record's flags (low 16 bits of the
      word) share nothing with the mask, or when the new g (below) is **65000 or more** (`0x005105a4`; the cap is on
      the running total, not on the edge). A neighbour already on the heap with a g no larger than the new g is
      skipped too. Otherwise its g, parent and h (`Nav_Heuristic` `0x002517b0`) are written, f = g + h, and it is
      sifted up (already on the heap) or pushed (a node off the heap is pushed whatever its old g says).
    - **Costs** are `u16` in 1/16 m. A distance is the **3D** distance between the two nodes' positions × 16,
      truncated to an integer (the heuristic too).
    - **The new g** (`Nav_EdgeCost` `0x00251890`) = the expanded node's g + the step's cost, saturating at `0xffff`
      (so a saturated total is always over the cap). The step's cost, in this order:
        1. *c* = 3D distance × 16 (truncated);
        2. only while `0x0051059c` = 1 (the script's `ClimbFilter(on)`, `Climb_SetFilter` `0x002511b8`) and the mask
           lacks `0x100`, **one** of: + 200 when the record's flags have 8; else + 320 when they have `0x80`;
           else + 80 when they have 4, unless they also have `0x40` and the avoid bit is set (then nothing);
        3. add 1600 when the record's avoid bit (bit 31) is set;
        4. add 40 × *u* − 8 when *u*, the use count (C `+0x1f`) of the **expanded** node, is not 0 (nothing when it is
           0). The expanded node is the one the step enters in walking order (the record lives on it), so this
           spreads AIs over parallel routes.

      Each addition wraps at 16 bits; only the final sum with g saturates.
    - **The end**: the search fails on an empty heap. When it reaches the start's node, that node's g is put back, and
      the chain of parents is counted: a chain of **129 nodes or more fails** (a route holds at most 128 node
      indices: `0x110` bytes less the count and the three words at the end). Then, success or failure, every node on
      the closed list gets g = 0 (`PathNodes_ClearOpenMarks`), the start's node included.

   "Fails at 128 nodes" is therefore the **route's length**, not the open list: a search may visit up to 3000 nodes.
5. `Route_Find` (`0x002511c8`) retries a failed search with `mask | 0x8c` only when the mask (without `0x100`) is not
   `0xff`, so never for the default mask. When `0x0051059c` ≠ 0, the (possibly widened) mask has `0x80` and lacks
   `0x100`, and the route found steps over a `0x80` link, it searches again without `0x80`, with the cap
   `0x005105a4` set for that search to the f (C `+0x16`) of the first route's first node and put back to 65000 after.
   It then keeps the second route only when its first node's g (C `+0x18`) is lower than the first route's first
   node's g. Confirmed (code). Both g values are read after the second search, and every node on a found chain was
   closed and so has g = 0 by then: the comparison is 0 < 0, and **the first route (with its `0x80` climb) is always
   kept** (inferred from the code; not seen at run time).
6. **Building** (`Route_Build` `0x002513a8`; a pool of 32 routes of `0x110` bytes at `0x006ca250`, taken from slot 31
   down; `+0x00` the count, then up to 128 `s16` node indices, `+0x104` the start's node, `+0x108` the last node,
   `+0x10c` the destination's area). Confirmed (code):
    - **Leading cut** (only when `0x005105a0` = 0; no code writes it, so always): from the start's node, move the
      route's first node along the parents while the walkable line (mask 0) from the start point to the next node
      passes; it may stop on the destination's node.
    - **Shortcuts**: from each node *a* in turn, the nodes 2, 3, 4 and 5 ahead are tried, but never the route's last
      node; the farthest of them whose D records name *a* (a link from *a* into it, of any kind and whatever the mask)
      becomes *a*'s next node. It moves on to the new next node and stops when it or the node after it is the last.
    - **Trailing cut**: while more than one node is left, drop the last node when the walkable line passes **from the
      destination point** (in the destination's area) to the node before it. The line runs from the destination,
      not toward it.
    - Each node's use count (C `+0x1f`) is raised; freeing the route (`0x00251680`) lowers it.

The polygons come in **areas**, an outline and its holes ([Path data](level-loading.md#path-data)); the human's
polygon and a node's polygon are an area's first path. **Path flag 8** (`u16` at polygon `+0x48`, bit 3) takes a
polygon out of every test below, so an opened door's hole stops cutting the area. Confirmed (code) at the addresses.

- **The walkable-line test** (`0x0024fbf8`, arguments: the start's polygon, the two points, a polygon-flag mask). It
  reads no collision geometry: only the hazard spheres below and the path polygons. Confirmed (code):
    1. Refused when a **hazard sphere** blocks the segment (`0x00221f80`, below).
    2. It crosses the segment with every edge of the start area's polygons (the outline and its holes, the list from
       polygon `+0x20`) whose `u16` flags at `+0x48` have neither 8 nor any bit of the mask and whose box meets the
       segment's. The crossing is in plan (`0x0024e938`: x and y only, z is interpolated), at segment parameter t in
       [0, 1], and a crossing at exactly t = 1 is ignored. Edges come from the slab lists when the polygon has them,
       else all. It keeps the **nearest** crossing. No such polygon at all → refused.
    3. No crossing: it passes when the end point's area (`0x00250708`) is the start's.
    4. A crossing: the end point needs an area. Its polygons are crossed the same way, keeping the **farthest**
       crossing, and the line passes only when the two crossing points are within 0.02 m (dist² < 0.0004): the
       segment leaves one area where it enters the next. Any edge in between, a hole's included, refuses the line.
- **What blocks a line at a fence** is the fence's **hole** in the path polygons, not its collision. Confirmed
  (runtime), from the path data of a `level99` state at checkpoint 3 (PCSX2 2.9.94, over the straight line Vermin's
  `GoalMoveToFlag` asks for, (47.49, 42.97) to (46.30, 20.22); both ends lie in the street's area, whose outline is
  polygon 0): the segment crosses two holes of that area, a 4-vertex hole of flags 7 spanning x 44.14 to 50.61 and
  y 24.38 to 25.19 (the climbable fence, 6.5 × 0.8 m) at t = 0.78 and 0.82, and the hole of fence door 2 (x 44.93 to
  49.50, y 30.79 to 31.56, flags 7) at t = 0.50 and 0.54. The nearest and farthest crossings are metres apart, so the
  line is refused and a route is made ([Vermin's fence](#route-follow)). An opened door's hole gets flag 8
  ([Objects](objects.md#nav-links)) and drops out; the fence's never does. A test that treats the area as the union
  of its polygons (a hole counted as walkable) lets the line through.
- **The hazard spheres** (`0x00221f80`, confirmed (code)): 64 records of `0x40` bytes at `0x0065ff50`, `+0x00` the
  centre, `+0x10` / `+0x20` its box (min / max), `+0x30` (r + 0.3)², `+0x34` (r + 0.8)², `+0x38` a word (0 from the
  only adder), `+0x3c` in use; their count is `0x00510170`, cleared at level start (`0x00217f68`). The test runs only
  when the count is non-zero and `0x0051057c` is non-zero (1 in the ELF's data; no code writes it). A sphere blocks
  the segment P → Q when its box meets the segment's box (x, y and z), the segment passes within r + 0.8 m of the
  centre in 3D (`0x00336d28`, squared distance to the nearest point of the segment), and the segment heads at it:
  (Q − P)/|Q − P| · (P − C)/|P − C| < −0.707 (within 45°). The points are tested at their own heights; there is no
  lift above the ground. `0x002198b8(r, word, centre)` adds one (and marks the route links within r of it to be
  avoided, `0x00253078` → `0x00252c90`: bit 31 of every D record whose edge passes within r + 0.3, counted so overlapping
  spheres release it only once), and `0x002199c0` removes one. The only adder is `0x003a5a90`, called by the fire
  particle types (`0x003c40a0`, and `0x003c8b28` from `0x003ca050`, [Particles](particles.md)) with r random in 0.5 to
  0.75 m: an AI's straight line refuses to run into a fire. In the checkpoint 3 state the count was 0, so
  `0x00221f80` returned 0 at once. Confirmed (runtime) for the count; that fires are the only source is confirmed
  (code) from the single call site.
- **A node in a straight line** (`0x0024f290`, the end-node search's test): refused when a hazard sphere blocks it
  (`0x00221f80`) or when the segment crosses an edge of any of the area's polygons without flag 8 or the mask (0
  there).
- **A point's area** (`0x00250708`): `Nav_FindAreaCandidate` (`0x00250760`) casts one ray through the world's
  collision (`WorldManager_RayCast`, `*0x005147c4`) from **0.4 m above** the point, straight down, **4 m long** (so
  it finds ground down to 3.6 m below the point). No hit, or no path data, → no area. Otherwise it walks the area
  list (`0x00510588`, then each first path's `+0x24`) and takes the first area whose first path has its `u16 +0x4a`
  equal to the hit's **area byte** (hit record `+0x2e`: the collision triangle's byte `+0x09`,
  [Collision](collision.md)), whose box widened by 0.35 m holds the point in plan, whose `s16 +0x00` (vertex count) is
  1 or more and whose flags lack 8. Then the inside test `0x0024ea60` over its polygons, skipping flags 8 and `0x10`.
  Confirmed (code). The first triangle the ray meets decides: a point on a walkway finds the walkway's byte, and a
  point under one finds the ground's unless the walkway is less than 0.4 m above it.

`0x00251d28` is a second A* search that stops at the first node beyond a distance outside a cone of
directions, skipping bit-31 edges; its one caller places spawned humans out of sight ([The search](ai-gangs.md#spawner-search)).

### A point in a hole {#path-holes}

The point-in-a-hole test and its fallbacks are on [AI pathing: a point in a hole](ai-pathing.md#path-holes).

## Following a route {#route-follow}

**The route state** (brain `+0xe0`): `+0x00` the brain, `+0x04` the route, `+0x08` the waypoint radius, `+0x0c` the
state (1 jumping, 2 dropping, 3 climbing, 4 climbing a `0x80` ledge, 5 finished), `+0x10` `s8` the waypoint index,
`+0x11` "move on now", `+0x12` "the leg is new", `+0x13` the climb's failed tries, `+0x14` the edge mask, `+0x16`
"held at the node", `+0x17` the skip counter. Confirmed (code) at the readers below.

**`Route_Follow`** (`0x0029aa88`), called by the move action every update it is not busy (step 3 of
[the move action](#move-action)), in order. Confirmed (code):

1. State 5 → return 2 (the route is over). Otherwise the state is set back to **0** on every call (so a jump, drop
   or climb state lasts only until the next call after the human is free again).
2. **Skip check**: unless "move on now" is set, the skip counter counts calls; on the call where it was already 6 (the
   **7th**), `Route_SkipReachable` (`0x0029b4b8`) tests the current waypoint and, when it may be skipped, sets "move on
   now"; the counter goes back to 0.
3. **Moving on**: with "move on now" set, `Route_Advance` (`0x0029b6d8`): "the leg is new" = 1, the flags and both
   counters cleared, the index + 1, then while waypoints are left, each that `Route_SkipReachable` passes is skipped
   too; the first that is not becomes current: unless the leg into it is of kind 4, the human's claims are released
   (`Nav_ReleaseClaims`, [below](ai-pathing.md#route-claims)); the radius becomes 0.25 m; brain `+0x90` = its point,
   `+0x118` = 0.25. Past the last waypoint the route is freed (`RouteState_Release`) and `Route_Follow` returns 1 (go
   on straight to the destination).
4. **The legs**: *in* = the flags of the link from the previous waypoint to the current one (0 at index 0), *out* =
   from the current one to the next (0 at the last).
5. **Claims and busy links** (only at the detail level `+0x333` < 2): claim the current waypoint's node
   (`Nav_ClaimWaypoint`, with *out*); when it is granted, the *out* link and then the *in* link must not be busy (kind
   4: `Route_IsLinkBusy` `0x0029a3f0`; else kind `0x80`: `0x0029a6c8`; other kinds are never busy). Refused or busy →
   "held" = 1 and, when the human is within **2 m (3D)** of the waypoint's point, the aim is that point with radius
   0, the move is stopped (`Brain_StopMove`) and `Route_Follow` returns 0. Granted and free → "held" = 0.
6. **A new leg** (index > 0, "the leg is new" = 1): *in* has 8 or `0x80` → the climb handler; else *in* has 4 → the
   jump handler; else, when the link's avoid bit is set, `0x40` → the charge, any other → refused (brain `+0x284` = 4
   for `0x10`, else 2) and return 2; otherwise "the leg is new" = 0. The climb and jump are tested **before** the avoid
   bit, so a climb or jump link with the avoid bit is still climbed or jumped.
7. When *out* has 8 or `0x80`, `Route_TryClimbNear` (`0x0029b9b0`, the fast climber's early climb) decides the
   result; otherwise 1 (keep walking to the waypoint).

**`Route_SkipReachable(i)`** (`0x0029b4b8`): only when the link into waypoint *i* (a kind-1 link for *i* = 0) and the
link out of it both have kind 1 or 2 (flags & 3), and *i* is not the last waypoint. Then it passes when the human can
walk straight to waypoint *i* + 1's node (`Human_CanWalkStraightTo` `0x002221e0`, from where he stands). Confirmed
(code). There is a turn test that never refuses: when the leg into *i* (from *i* − 1's node to *i*'s) is longer than
5 m (3D), the code compares the cosine between two unit vectors with cos 135° (−0.707), but both vectors are that
same leg (`0x0029b56c`-`0x0029b654` subtract the same two node positions twice), so the cosine is 1 and the skip goes
ahead. Confirmed (code) from the disassembly. A port must not add a turn limit here.

**When the straight line is tested** (confirmed (code); each call is the walkable-line test of
[Path planning](#path-planning) with the mask 0):

- **Asking for a route** (`0x0029a8c0`): from the human's position to the point; passing means no route at all.
- **Skipping a waypoint** (`0x0029b4b8`, every 7th call and when `0x0029b6d8` moves on): waypoint *i* + 1 is tried
  from where the human stands (`0x002221e0`), and only when the links into waypoint *i* and out of it are both of
  kind 1 or 2 (word & 3); the first waypoint counts as reached by a kind-1 link. So a waypoint at either end of a
  climb, jump, door or charge link is never skipped, and the human walks to it whatever the line test says.
- **Dropping the route** (the move action's update, every 30 updates, [The move action](#move-action)): when the
  final point passes `0x002221e0`, the route is freed and the human goes straight.
- **Building the route** (`0x002513a8`, calls at `0x00251494` and `0x002515e8`): leading nodes the start reaches,
  and trailing nodes from which the destination is reached, are dropped.

`0x002221e0` first asks the trains: unless brain `+0x2e0` equals the current game time (`*(0x0050b734) + 0x48`),
`Trains_IsPathClear` must pass ([ObjStartTrain](../references/bindings/world.md#objstarttrain)); then the
walkable-line test from the human's polygon (`0x00247958`) and its position in the position table (`0x00714b00`).

**Link kinds.** A leg's kind is the D record that steps from the waypoint before to the current one (`0x00251070`);
the follower reads it on the update after `0x0029b6d8` moves on (route state `+0x12` = 1), at waypoint index > 0.
Confirmed (code) at `0x0029ad04`-`0x0029adb8`:

| Leg's kind | What the follower does | Handler |
| --- | --- | --- |
| 8 or `0x80` | a **climb** (8 over a fence or wall, `0x80` up onto a ledge; [the kinds on the disc](#path-planning)) | `0x0029b848` |
| 4 | a **jump** (route state 1) or a **drop** off the edge (state 2), [Jump legs](#route-jump) | `0x0029baa8` |
| any, avoid bit (31) set, `0x40` | the **charge** at a breakable door or pane ([Objects](objects.md#nav-links)) | `0x0029bca0` |
| any other, avoid bit set | refused: brain `+0x284` = 4 for kind `0x10` (a closed door), else 2; the move ends | |
| 1, 2, `0x10` (an open door) | walked | |

The **climb** (`0x0029b848`): each update it aims at the waypoint (brain `+0x90`, heading `+0x110`, `+0x11c` = 1),
turns the body to it (`0x0021b100`) and calls `Climb_TryStart` (`0x002826f0`), the player's own climb
([Characters: Climbing](characters.md#climb)): the forward probes must find a climbable face. On success the route
state `+0x0c` = 3 (kind 8) or 4 (kind `0x80`), brain `+0x11d` = 1, and the clips play; otherwise it runs on at gait 4
(`0x0028aac0`) and tries again, giving up (move done) after 31 failed updates (counter `+0x13`, cleared when the
waypoint changes). When the **next** leg is 8 or `0x80`, a human with flag `0x2` (human `+0xe0`, a fast climber,
`HuSetFastClimber`) whose gait (human `+0x1a8`) is above 3 starts the climb early, within 4.5 m (3D) of the **next**
waypoint (`Route_TryClimbNear` `0x0029b9b0`): on success the state is 3, the route moves on at once
(`Route_Advance`), brain `+0x11d` = 1 and "the leg is new" = 0.

**How a climb ends** (confirmed (code)). A climb that starts sets the brain's arrival flag `+0x11d` itself. While the
climb clips play, the human is busy and the move action returns at its first test, so nothing else runs. On the first
update after the climb, `MoveAction_Update` finds `+0x11d` set (`0x002fc674`), clears it, and asks the route to move
on (`0x0029aa78` sets "move on now"); `Route_Follow` then puts the state back to 0 and `Route_Advance` makes the
waypoint after the climb's far node current. The climb's far node (the waypoint the climb leg entered) is passed
without being walked to: the climb itself carried the human over it. The arrival test `Brain_CheckMoveArrival`
(`0x0023f9d0`) only ever sets `+0x11d` (when it is 0 and the front action is a move); only the move action clears it,
and when a steering detour is running the flag ends the detour (`0x00288ad0`) instead of moving the route on.

### Waypoint claims {#route-claims}

The 40-entry claim table (the nearer of two humans on a node goes first) is on [AI pathing: waypoint
claims](ai-pathing.md#route-claims).

## Jump legs {#route-jump}

The jump and drop legs of a route are on [AI pathing: jump legs](ai-pathing.md#route-jump).

## Neighbour sectors {#neighbour-sectors}

Each human has a record of who stands round him in eight 45° sectors. The fight's footwork
([MoveMelee](#move-melee)), the grab and snap tries, [the steering](#steering) and giving way all read it.
Confirmed (code) at the addresses cited, except where marked. Code index: [Neighbour sectors](ai-code.md#sectors).

**The record**: 60 records of 0x48 bytes at `0x006e8318`, one per human slot (`Sectors_GetOwner` `0x0029e470`
maps back). Per sector *k* (0-7), 8 bytes at `+8k`:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0` | handle | the **nearest human** in the sector (the null handle when none) |
| `+4` | u16 | **flags**: 1 someone within 1.5 m, 2 someone very close (below), 4 no walkable line out (a wall), 8 a free player near |
| `+6` | u8 | 1 while the wall probe has still to run |
| `+7` | u8 | **how many** humans stand in the sector within 1.5 m |

Then `+0x40` holds the owner's heading at the last refresh and `+0x44` that time (ms). The distance is not kept: it
exists only while the record is rebuilt. `Sectors_Construct` (`0x0029e250`) clears the handles. `Sectors_Reset`
(`0x0029e2b0`, also called from `Brain_Init`) zeroes the heading and time and sets every probe byte.

**Sector numbering** (`Sectors_SectorOf` `0x0029eb10` → `Human_GetSectorOf` `0x0029eb38`): take the bearing of the
point from the owner minus the owner's heading, wrapped to [0, 2π). **Sector *k* is centred on *k* × 45°**, with
edges at ±22.5° (0.3927 rad), so 0 is straight ahead. Both angles are the game's headings (`Vec_HeadingFromTo` and
`Quat_Heading` inside `Heading_RelativeTo` `0x00386798`), which grow from `+y` toward `+x`, clockwise seen from above
and toward the human's right ([Characters: Step control](characters.md#step-control) gives the proof). So the index
rises **clockwise**: 2 is his right, 4 behind, 6 his left. Confirmed (code). A bearing exactly on an edge goes to the
lower index on the right half (0 to π) and to the higher index on the left half. A port whose headings grow
anticlockwise (the turn angle θ about `+z`, minus the game's heading) must number the sectors the other way round, or
negate its bearings first; otherwise every "first try *s* + 1" below leans the wrong way.

**Refreshing** (`Brain_GetSectors(brain, maxAge)` `0x0028fe90` → `Sectors_Update` `0x0029e5d8`): the record is
rebuilt when now ≥ `+0x44` + maxAge; otherwise it is returned as it is. Callers pass 1000 ms (MoveMelee, the
steering, the fight goal's re-target and grab tries) or 500 ms (giving way). A rebuild:

1. Stores the owner's current heading at `+0x40` and gives every sector the null handle, count 0 and probe byte 1.
2. Lists up to 60 humans within **1.5 m** of the owner, in every direction, each with his **squared** distance
   (`Humans_FindAhead(1.5, …, mask 1)` `0x002274a8`). Despite its name this is a plain radius search round a point.
   It takes every human slot whose word at `0x00715390` has bit 1 set, except the owner. There is no state check,
   so downed and busy humans count.
3. Each listed human raises his sector's count by one. The nearest in each sector becomes its handle.
4. The flags are rewritten from the nearest human's squared distance *d²*: *d²* < 1.5 → **3** (bits 1 and 2,
   within 1.22 m); otherwise *d²* < 2.5 → **1**. Every listed human is within 1.5 m (*d²* ≤ 2.25), so in practice:
    - bit 1 means "someone in the sector";
    - bit 2 means "someone within 1.22 m";
    - an empty sector gets 0, which also clears bit 4.

   (The thresholds look like metres compared against a squared distance. What is described here is what the code
   does.)
5. **Bit 8** is set only when the owner is an AI (human `+0x1b0` = −1). It marks a nearby player who is not already
   part of this fight (inferred meaning). Each player human is checked (game state `+0x228`, count `+0x224`). Bit 8
   is set in the player's sector when all of these hold:
    - he is not busy (`Human_IsBusy`);
    - he is within **5.5 m** (30.25 squared);
    - he is not the owner's target (brain `+0x124`) and not the target of the owner's target;
    - his own target is neither the owner nor the owner's target.
6. Sets `+0x44` = now.

**The wall probe** (`Sectors_ProbeWall` `0x0029e4b0`) runs lazily, at most once per sector per rebuild, and only
when a reader needs it (the probe byte is 1):

- **The point**: 1.5 m from the owner's position at heading `+0x40` + *k* × 45°, at the owner's height (the
  direction is flat, inferred from `Vec_FromHeading`).
- **The test**: bit 4 is set when `Human_CanWalkStraightTo(owner, point, 0)` (`0x002221e0`) fails. This is not a
  physics ray. It fails in two cases:
    - a train's path is in the way, unless the owner's brain is already near a train;
    - there is no walkable straight line on the navigation mesh (`Nav_IsWalkableLine` from the owner's polygon,
      [Path planning](#path-planning)).
- **After the probe** the probe byte is 0. Only the next rebuild clears bit 4. `Sectors_ResetHeading` (`0x0029e2e8`,
  used only by BigThrower) re-arms the probes with a new heading but keeps an old bit 4.

**Readers** (`0x0029e980`-`0x0029eb10`):

| Function | Answer |
| --- | --- |
| `Sectors_IsBlocked` `0x0029e9c8` | true at once when bit 2 is set; otherwise probe, then any of bits 2, 4 or 8 (a very close human, a wall or a free player) |
| `Sectors_IsFree` `0x0029ea48` | none of bits 1, 2 or 8; then probe, and no wall |
| `Sectors_IsWall` `0x0029ea10` | probe, then bit 4 |
| `Sectors_GetCost` `0x0029e980` | probe, then flags + 2 × count |
| `Sectors_AllClear` `0x0029eaa0` | no sector has bit 1 or 2 (no human within 1.5 m at all) |
| `Sectors_IsHeldBy` `0x0029ead8` | whether the given human is the nearest in a sector |
| `Sectors_TurnWay` `0x0029ec90` | +1 or −1, the shorter way round from one sector to another |

**A point in a sector** (`Sectors_GetPoint(distance, out, record, k)` `0x0029e350`): the owner's position plus
*distance* along his **current** heading + *k* × 45° (not the stored `+0x40`), at his height (inferred, as for the
probe). The steering passes 1 m ([Steering round humans](#steering), the standing case). The other caller is
`Human_DropCarried` (`0x00232c60`).

**Giving way** (`Brain_GiveWayTo` `0x00289ed0`, reached only through `Brain_PushAside` `0x0028a248`) is the
standing side: [Giving way](#giving-way). The steering is **not** one of its callers: it handles a standing blocker
with a detour ([Steering](#steering), case 0) and never asks him to move. Confirmed (code) by the cross-references.

**The blocker test** (`Steering_FindBlocker(state, list, n, myPos, myDir, myStep, outFrac)` `0x00288cc8`) runs over
the list from `Humans_FindAhead`: every human within min(2 `L`, 10 m) of the walker, in any direction
([Steering](#steering)).

1. Skip the walker's target (brain `+0x124`) and anyone behind him: the dot product of the walker's unit move
   direction with the offset to the other human is below 0.
2. Predict the other human's step:
    - a player: his facing × 0.75 × his speed `+0x1ac`;
    - an AI: toward his brain's aim point `+0x90`, of length min(the distance to it, 0.75 × his brain speed
      `+0x114`) (`Steering_ClampStep` `0x00288f40`).
3. Let *r* be the relative step: my step − his. A ray from my position along *r*, of length |*r*|, is tested against
   a **disc of radius 0.63 m** round him (`Steering_RayHitsHuman` `0x00289d68`; 0.3969 = 0.63²). There is no hit
   when:
    - he is farther than |*r*| + 0.63;
    - he lies behind the ray;
    - the ray misses the disc.

   Otherwise the result is the hit distance *t*, measured to the disc's edge.
4. The blocker is the human with the smallest *t* / |*r*|. That fraction is also returned. So the test sweeps a circle
   along the relative motion over one step; it is not a corridor or a cone.

## Steering round humans {#steering}

`Human_SteerAroundHumans` (`0x00289138`) bends a [move action](#move-action)'s walk round the one human most in the
way. `MoveAction_Update` (`0x002fc5c0`) calls it every update with the steering state (brain `+0xa0`), the move's
speed for this update, its **aim point** (the next path point, in and out), its destination (action `+0x10`) and its
arrival radius (action `+0x2c`). Confirmed (code) from the disassembly of `0x00289138`-`0x00289d60` (the decompiler
drops several arguments) unless marked; not checked at run time. The names of the cases are ours.

**The state** (brain `+0xa0`; helpers on [AI code: steering](ai-code.md#brain-steering)):

| Offset | What |
| --- | --- |
| `+0x00` | the **held point**: the point stored by the last successful detour, used as the aim between decisions |
| `+0x10` | the predicted **contact point** of the last decision (C, step 10) |
| `+0x20` | the brain |
| `+0x24` | the human being avoided (non-zero = avoiding; `Steering_SetAvoiding` also raises the brain's turn boost) |
| `+0x28` | u8, updates since the last decision; `Steering_Clear` sets 255, which forces a decision on the next update; a move's Start seeds it with the human's index (below) |
| `+0x29` | u8, slow decisions in a row (step 5); a move's Start seeds it with the human's index too |
| `+0x2a` | enabled |
| `+0x2b` | u8, decisions since the detour was taken (step 6) |
| `+0x32` / `+0x34` | u16 updates left / the **speed override** (`Steering_SetSpeed(speed, n)`) |
| `+0x3c` | the detour's **score** (below) |

**A move's Start** (`Steering_ResetAvoidance`, `0x002890a8`, from the move action's Start) clears the held point to
the origin, turns avoiding off, sets the score to −1e9 and `+0x2b` to 0. It writes the low byte of the human's
**index** (human `+0x92`, his slot in the position table) into **both** `+0x28` and `+0x29`; it is not a node.
Confirmed (code). It forces no decision as such: step 2 compares the old counter with 3 × the detail level, so the first
call decides when the index is at least that (always at detail level 0), and a human with a lower index waits the rest.
So the humans' decisions are staggered by index. The slow counter starts at the index as well, so the first × 10 of
step 5 comes after 16 − index slow decisions, or on the first one for an index of 15 or more.

**Speed overrides.** `Steering_SetSpeed` only stores a speed and a count of updates; the count drops by one at the
start of every call. While it is non-zero the move action takes `+0x34` as its speed instead of its own (action
`+0x40`, read at `0x002fc778`), and the brain's move speed `+0x114` follows from that (`Brain_SetMoveSpeed`). So an
override acts from the next update on, for n updates.

**One call**, in order (P my position, s my speed, human `+0x1ac`):

1. Not enabled, or brain `+0xec` set (inferred: held in a [waypoint queue](#queues)) → return 0. Human `+0x333` above
   2 → avoidance off, return 0.
2. **Between decisions**: the counter `+0x28` is read, stored back plus one, and the **old** value compared with 3 ×
   human `+0x333` (with `+0x333` = 0 it decides every update). Below it: when avoiding, aim := held point and return
   1, else return 0.
3. **A decision**: counter 0, score := −1e9. When avoiding the arrival radius counts as 0, and if s is a standing
   gait (gait 0, below 0.5 m/s) aim := held point and return 1.
4. Within 1 m of the destination (squared distance ≤ 1) → return 0.
5. **Look-ahead.** L = 0.75 × the move's speed (the distance covered in 0.75 s). While that speed is below 4 the
   counter `+0x29` counts up (stored plus one), and when the new value reaches 16, L is × 10 once and the counter
   goes to 0; at 4 or more it goes to 0. `Humans_FindAhead` (`0x002274a8`) lists up to 60 humans within
   **min(2L, 10) m** of P, in every direction, not counting me.
6. **Expiry.** When avoiding, `+0x2b` counts decisions. It is read, stored back plus one, and the **old** value
   compared: while it is below 4 × trunc(11 − s) (s my human's speed `+0x1ac`, `Float_ToUInt`), the walk heads for the
   held point instead of the aim; otherwise `Steering_Clear` ends the detour, which sets `+0x2b` back to 0. Only
   `Steering_Clear`, `Steering_ResetAvoidance` and `Steering_Reset` write 0 there. `Steering_TryDetour` does
   **not**, so a new detour taken while avoiding keeps the count of the old one. It counts only on decisions while
   avoiding. Confirmed (code) at `0x0028935c`-`0x0028938c`.
7. **My step.** D = the unit direction from P to that point, in plan (z = 0); S = D × min(L, its distance − the
   arrival radius).
8. **The blocker.** `Steering_FindBlocker` (`0x00288cc8`): of the listed humans, skipping my target (brain `+0x124`)
   and anyone behind (D · (B − P) < 0), the one whose 0.63 m disc (`0x00289d68`) a ray from P along the relative step
   (S minus his predicted step) hits first. His predicted step is his facing × 0.75 × his speed when his brain
   `+0x04` is 0 (inferred: a player), else `Steering_ClampStep(0.75)` toward his aim (below). It returns him and
   **t** = the hit distance ÷ the relative step's length: the fraction of the 0.75 s step at which the two touch
   (1e9 for none; [the ray test](#neighbour-sectors)). No blocker → when avoiding aim := held point and return 1, else
   return 0.
9. When I lead a formation (human `+0x1a4`) and he is one of its followers, his own steering gets the gait-0 speed for
   5 updates (`Steering_SetGait`, so he stops); this call goes on.
10. **Keep the old detour?** When avoiding and the stored contact point `+0x10` is nearer to P than the new one
    (|t × S|), aim := held point and return 1. Otherwise `+0x10` := **C = P + t × S**, where I would meet him.
11. **Vectors**, all in plan and of unit length unless said: B his position; Q his predicted point, B + his facing
    (`Quat_AxisY`, 1 m) when his brain `+0x04` is 0, else his brain's aim point `+0x90`; N = (B − P) normalised, the
    bearing to him; H = (Q − B) normalised, his heading; **E = D × my up axis** (`Quat_AxisZ`), that is (Dy, −Dx):
    perpendicular to my move, 1 m long. Two dot products choose the case: **b = D · N** (how straight ahead he is)
    and **h = D · H** (how alike our headings are), against **cos 30°, cos 50° and cos 45°**.
12. **Right of way**: I have it when s is above his speed (human `+0x1ac`), or equal and my human's address is the
    lower.
13. **Standing** (case 0): his brain `+0x04` is 0 and his speed below 0.05; or his brain's move speed `+0x114` is ≤ 0;
    or his steering override is active (`+0x32` ≠ 0) with a speed ≤ 0.
14. **The case** otherwise, tested in this order:

    | b (bearing) | h (headings) | Case |
    | --- | --- | --- |
    | ≥ cos 30° | < −cos 45° (coming at me) | 1, head-on |
    | ≥ cos 30° | > cos 45° (same way) | 2, overtake, only with right of way; without it return 0 (follow) |
    | cos 50° to cos 30° | \|h\| > cos 45° | 2 |
    | ≥ cos 50° | \|h\| ≤ cos 45° (crossing) | 3, crossing |
    | < cos 50° | ≥ cos 45° (same way, off to the side) | none: without right of way the speed is 0.75 × s for 15 updates; return 0 |
    | < cos 50° | < cos 45° | 3 |

15. **Same route node** (cases 0-3): when both brains follow a route (the route state at brain `+0xe0`: `+0x04`
    non-zero for both) and **I am held** (my route state's `+0x16` set, below), and `RouteState_CurrentNode`
    (`0x0029b248`) is the same node for both, the speed is **0.75 × his speed** for 5 updates; return 1, aim
    unchanged.

    Route state `+0x16` is the **held** flag of `Route_Follow` (`0x0029aa88`). At detail level below 2, each update
    claims the current waypoint (`Nav_ClaimWaypoint`). If it holds the claim and the next or the current leg is a
    jump or run-climb (link bits 4 or `0x80`), it also checks that the link is free (`0x0029a3f0`, `0x0029a6c8`).
    - **0 back** (another human keeps the node, or is on the link): `+0x16` = 1. Within 2 m of the waypoint he
      stops there (brain `+0x90` the waypoint, speed 0, `Brain_StopMove`), and the follower returns 0.
    - **Non-zero back**: `+0x16` = 0.
    - `RouteState_Init` and `Route_Request` clear it, and at detail level 2 or more it is left as it was.

    So step 15 slows me behind a human at the node I am queued for. Confirmed (code) at `0x0029ab94`-`0x0029acfc`
    and `0x00289864`-`0x002898d4`. That the link checks return 0 for "taken" is inferred from this use.
16. **Yielding** (cases 1-3): without right of way and with the contact within 3 m (|t × S|² < 9), cases 1 and 3 stop
    (speed 0 for 1 update) and case 2 matches his speed for 5 updates; return 1, aim unchanged.
17. **The detour point X**:
    - **Case 0** (he stands): X = B + σE, σ = −1 when E · N ≥ 0, else +1: **1 m from his centre, perpendicular to
      my move, on the side of him that my line passes**. aim := X and `Steering_TryDetour(X)`; on success return 1.
      Otherwise k = `Sectors_SectorOf(me, X)`: X's sector **relative to my own facing**. My human's vtable `+0xac`
      fills a transform (position `+0x00`, rotation `+0x10`), and `Heading_RelativeTo` (`0x00386798`) takes the
      heading from my position to X less my heading (`Quat_Heading` of my rotation), wrapped to 0-2π. Then
      `Human_GetSectorOf` (`0x0029eb38`) maps it to 0-7, sector *k* centred on *k* × 45°. k' = (k + 4) mod 8.
      X' = his position + 1 m along **his live facing** (his rotation in the position table, `0x00714b10`) +
      k' × 45° (`Sectors_GetPoint` `0x0029e350`, with his sector record, refreshed when older than 1,000 ms, naming
      him). So k is measured in my frame and applied in his. No stored heading is used. Confirmed (code) at
      `0x002899a4`-`0x00289a24`. `TryDetour(X')`, and on success
      aim := X'. Return 1 either way (the aim stays X when both fail).
    - **Cases 1 and 2**: X = C + σρE. ρ = +1 when B is on E's side of my line (`Steering_SideSign`, E · (B − P) ≥ 0),
      else −1; σ = +1 when my segment P → aim crosses his B → Q (`Segment_Intersect2D`, `0x00337308`), else −1. So
      **1 m sideways from where we would meet**, away from his side, or towards it when our paths cross (he is
      moving over to the other side).
    - **Case 3**: X = B + `Steering_ClampStep(0.75 t)` + σE, σ = −1 when H · E ≥ 0, else +1. `Steering_ClampStep(f)`
      (`0x00288f40`) is his unit direction to his aim (his brain `+0x90`) × min(his brain `+0x114` × f, his distance
      to it). So **where he will be when we meet, then 1 m to the side he comes from** (behind him).
18. **Corner check** (cases 1-3): when s minus `Move_CornerSpeedLimit(s, brain, A, P, X)` is more than 1, the speed
    is 0.75 × s for 1 update; return 1, no detour. Here s is my human's speed `+0x1ac` and A my rotation's x axis
    (`Quat_AxisX`, the side axis: forward is local y). `Move_CornerSpeedLimit` (`0x002fcd90`), confirmed (code):
    1. **R** = `Math_CircleRadius2D(P, X, A)` (`0x003375d0`): the radius of the circle through P and X whose centre
       lies from P along A, so the circle tangent to my facing at P. With *d* = X − P in plan, R = |d| / (2 |d̂ · A|).
       It is 0 when X = P or when d̂ · A = 0, which is X dead ahead or behind.
    2. For a gait *g*, the speed that turn allows is v(g) = 2R · tan(ω(g) / 2) / (1/30). Here ω(g) is the turn per
       update (`Human_GetTurnRateForGait`, `0x002212d0`): 0.0436 rad (2.5°) at gait 5, 0.0698 (4°) at 4, 0.1047
       (6°) at 3 and 0.2094 (12°) below, times the AI's turn boost (brain `+0x0b`: b + 1, or 1 / (1 − b) when
       negative). 1/30 is `0x005102cc`. So it is the speed whose per-update chord, turning at that rate, stays on the
       circle.
    3. g₀ = `Human_GaitForSpeed(s)` (`0x0022aeb0`: 0 below 0.5 m/s, else the gait whose speed is nearest), and
       v = v(g₀), **not** capped by g₀'s speed. When v ≥ s: return s.
    4. Otherwise step down. Keep *best* = v, go to g − 1, stop below gait 1, and compute v' = min(the gait's speed
       `Human_SpeedForGait`, v(g)). Go on while v' ≥ *best*, taking v' as the new best; stop at the first gait that
       allows less.
    5. Return min(s, best). It never returns 0 as "none fit": the result is the best of the tried gaits, and 0 only
       when R = 0 (or s ≤ 0).

    So the check fires when the detour bends harder than my speed allows by more than 1 m/s.
19. Otherwise `Steering_TryDetour` (`0x00289010`): when the human can walk straight to the point
    (`Human_CanWalkStraightTo`) and `Ground_ProbeBelow` does not return 1. `Ground_ProbeBelow` (`0x00249050`) casts a
    ray from 0.1 m above the point straight down for 2 m, with mask 0, and returns 1 only when it hits a triangle
    with flag bit 4 (`0x10`, the result's `+0x2c`), the bit the ground snap passes on as "under cover"
    ([Collision: triangles](collision.md#triangles)). A point with no ground within 2.1 m, or on any other surface
    first, passes. So it rules out detour points on those 1,052 marked triangles, not drops; what bit 4 marks is
    inferred. When both tests pass, avoiding := the blocker, score := t and
    held point := the point; then aim := X. Return 1 either way. **Note:** in cases 1-3 the point checked and held is
    the **aim the move passed in**, not X (`a2` is the aim at `0x00289cfc`), so X steers only the deciding update and
    the human then heads for the held aim until the next decision; case 0 checks and holds its own point. Inferred to
    be a slip in the original; a faithful port keeps it.

**The score `+0x3c`** is t, the fraction of the 0.75 s step at which the two would touch (smaller is sooner). It is
−1e9 at every decision and after `Steering_Clear`, t once a detour is taken, and 0 after `Steering_EndAvoid`. Nothing
in this function or its helpers reads it: a new detour is weighed against the held one by the contact points'
distances (step 10). Who reads it is not traced.

**The return value**: 0 = no steering, the move keeps its own aim; 1 = steering, and the aim may have changed. On 1
the move action recomputes its direction to the aim and uses an arrival radius of **0.3 m** (brain `+0x118`) for
this update instead of its own; either way the aim goes to brain `+0x90` (`0x002fc9a0`-`0x002fca98`).

`Brain_GiveWayTo` (`0x00289ed0`) and `Brain_PushAside` (`0x0028a248`) are the standing side of it: an AI standing in
a mover's way steps into a free 45° sector round it ([Giving way](#neighbour-sectors)).

## Giving way {#giving-way}

A standing AI in a mover's way steps aside one sector (45°) with a **GiveWay** action. Confirmed (code) at
`Brain_GiveWayTo` (`0x00289ed0`) unless marked. The step itself, its clips and how it ends are on the Human side:
[Characters: Step control](characters.md#step-control).

**Entry.** `Brain_PushAside(mover, stander, point, step)` (`0x0028a248`) is the only way in. The mover's path is the
line through `point` along `step`. Its seven callers, confirmed (code) at the call addresses:

| Caller (call) | When | mover, stander | point, step |
| --- | --- | --- | --- |
| `Route_IsJumpLegClear` (`0x0029a3f0`, call `0x0029a66c`) | a route follower about to take a jump leg (link bit 4) | the follower, the first blocker | his position, unit(waypoint *i* − waypoint *i* − 1) |
| `Route_IsClimbLegClear` (`0x0029a6c8`, call `0x0029a86c`) | a route follower about to take a climb leg (link bit `0x80`) | the follower, the first blocker | his position, unit(waypoint *i* − his position) |
| `Gang_ClearWayForLeader` (`0x002942c8`, call `0x002945bc`) | the Warriors' follow tactic, on its timer | the gang leader, each member in his way | the leader's position, his forward |
| `WarriorBrain_Think` (`0x003052f0`, call `0x00306004`) | a Warrior getting out of the way, every 12 thinks | the Warrior, the nearest gang mate | the Warrior's position, his forward |
| `SaveHumanGoal_Process` (`0x002b8ad8`, call `0x002b9368`) | a rescuer within 1 m of the downed human, touching someone | the rescuer, the human he touches | the goal point, unit(goal point − rescuer) |
| `Brain_GiveWayTo` (`0x00289ed0`, call `0x0028a100`) | no free sector (below) | the stander, a neighbour | the stander's position, the original step |
| `Action_GiveWay` (`0x002fe4b0`, call `0x002fe544`) | the script's `ActGiveWay`, for a stander with fewer than 8 queued actions | the other human, the stander | the other's position, his forward |

The answer (yes or no) is used only by `Brain_GiveWayTo`'s own recursion; every other caller ignores it.

**The two route legs** (`Route_Follow` `0x0029aa88`). For a follower with detail level `+0x333` < 2 whose claim on the
current waypoint *i* holds (`Nav_ClaimWaypoint`), the leg out of *i* (to *i* + 1) is tested first, then the leg into
*i* (from *i* − 1), each only when its link has bit 4 (`Route_IsJumpLegClear(route, leg end, 1)`) or bit `0x80`
(`Route_IsClimbLegClear(route, leg end, 1)`). A 0 (blocked, or a refused claim) marks the route `+0x16`; within 2 m of
waypoint *i* the follower then holds there at speed 0 (`Brain_StopMove`) for this update. Both tests return 1 when clear:

- the jump leg looks for humans within 1 m in plan of the segment from waypoint *n* − 1 to *n* and within 2 m in
  height (`Humans_FindNearSegment(1.0, 2.0, …)`, `0x002278a8`); of those, only one within 4 m in plan of the follower
  and ahead of him (dot with his forward ≥ 0) counts;
- the climb leg looks for humans within 0.5 m (3D) of waypoint *n* (`Humans_FindAhead(0.5, …)`), with no distance or
  ahead test.

The first found who is friendly (`Brain_IsFriendly` `0x00290230`), not cuffed and not knocked out decides: his front
action a `MoveTo` (type 1) with a speed `+0x114` above 0 → blocked, no push (he is moving anyway); otherwise, with the
third argument 1 (always, from `Route_Follow`) → `Brain_PushAside` as in the table, and blocked. Others are skipped.
A pushed blocker steps aside on a later update, so the follower waits at least one update.

**The leader's way** (`Gang_ClearWayForLeader(angle, gang)`). Its only caller, `WarriorFollowTactic_Process`
(`0x00311638`), passes **40°** on its timer: due every 1000 ms (250 ms while the leader holds an object), and 5 ms after
a call that pushed someone. Nothing happens unless the gang has a leader whose stored gait `+0x1a8` is below 3
(walking, not jogging). Each of the gang's 16 member slots (gang `+0x48`), other than the leader, is pushed when:

- his top goal is not GetItem (`0x2c`), DestroyItem (`0x2d`) or Tag (`0x5a`);
- he is within 6 m (3D) of the leader and inside the leader's 40° cone (the unit direction to him dotted with the
  leader's forward ≥ cos 20°), or within 1 m in any direction;
- he has no script handler (`Human_HasScriptHandler` `0x0021d570`: a handler at `+0xe8` or the prompt flag `+0x1b2`),
  his brain's `+0x2e5` (sparring) is clear, and he is not the leader's target (leader brain `+0x124`);
- his action queue is empty, or its front action is neither a `MoveTo` (1) nor a `GiveWay` (12).

Then his actions are cleared (`Brain_ClearActions`, result ignored) and `Brain_PushAside(leader, him, leader's
position, leader's forward)` runs; the function returns 1 when anyone was pushed.

**A Warrior making room** (`WarriorBrain_Think`, step 9 under [The Warriors](ai.md#think-warrior)). On the thinks where
step 9 runs, after the blocked-ray turn (whether or not that turned him): the nearest member of his gang
(`HandleArray16_Nearest` on gang `+0x48`, excluding himself) is pushed when he is not busy, his actions are not
blocked (`HumanRecord_AreActionsBlocked`), he is within 1 m, he has neither a Scatter (`0x37`) nor a HoldPosition
(`0x36`) goal and his action queue is empty. Mover: the Warrior; point: the Warrior's position; step: his forward.

**A rescuer's way** (`SaveHumanGoal_Process`, state 1: within 1.1 × the near range `+0x140` of the goal point). In the
fight stance with no queued action: farther than 0.75 m from the goal point, an enemy (`Brain_PickBestEnemy`) is fought
off first; else, farther than 1 m, he moves closer (gait 2, turn boost + 1). Within 1 m,
`Human_CapsuleOverlaps(rescuer, downed)` names what his capsule touches (inferred: not the downed human).
A human who is not a threat (`Brain_IsThreat`) has his actions cleared; when that
works, `Brain_PushAside(rescuer, him, goal point, unit(goal point − rescuer))` and the rescuer queues a `Nothing`
action of 1500 ms. When the clear is refused, the rescuer turns to face him and plays anim action `0x21`. A threat
becomes the target and is fought off. A world object in the way is handled by the object branch (not a push).

`Brain_PushAside` checks the stander's brain flags `+0xcc`, then calls `Brain_GiveWayTo`:

- bit 1 set: the stander is already giving way, and the answer is yes;
- bit 2 or bit 4 set: the answer is no;
- otherwise it sets bit 4 on both brains for the call, so the recursion below cannot loop, and clears it after.

**The tests**, all needed, else no:

1. The stander's brain **type** `+0x04` is not 0 (not a player's brain), or its "dead" byte `+0x09` is set. Confirmed
   (code) at `0x00289f1c`-`0x00289f2c`.
2. **Neither is a threat to the other.** `Human_IsThreatTo` (`0x00222a48`) must be false both ways. It is
   `Brain_IsThreat` on the two brains ([Characters: Step control](characters.md#step-control) gives the exact
   test).
3. **The stander is idle under its control.** `Human_IsIdleUnderControl` (`0x00225390`) needs these:
   - the control is `Human_UpdateControl` or the step control;
   - no state bit of `0x7bf9e1f3ff0`;
   - no held flag.

**Which way.** All angles are relative to the stander's facing `f` (`Quat_Heading` of its transform-table rotation)
and wrapped to 0-2π. `Human_GetSectorOf` (`0x0029eb38`) gives the sector, 0-7, each 45° wide and centred on k × 45°,
rising clockwise, toward his right ([Sector numbering](#neighbour-sectors)):

1. The mover's path is the line through `point` and `point + step`. `Line_ClosestPoint` (`0x00336e08`) gives the
   point `c` on that **infinite** line nearest the stander: `a + d · dot(p − a, d)`, with `d` the unit direction.
2. `s` is the sector of the heading from `c` to the stander: straight away from the path.
3. The sector of the mover's own heading is also computed, and `Sectors_TurnWay` (the shorter way round) is called
   with both. Its result is unused (confirmed (code): the return value is dropped).
4. The stander's sector record is refreshed when it is older than **500 ms** (`Brain_GetSectors(brain, 500)`,
   [AI code: Neighbour sectors](ai-code.md#sectors)).
5. Sectors are tried in the order **s, s+1, s+2, s−1, s+3, s+4, s−2**, modulo 8. That is seven of the eight; s−3 is
   never tried. The first **free** sector wins. `Sectors_IsFree` (`0x0029ea48`) needs no near human (flags `0xb`
   clear) and no wall: the sector is probed on first use and bit `0x4` must stay clear.

**A free sector.** All queued actions are popped (`Brain_ClearActions`; when one refuses, the answer is still yes but
nothing is queued). Then a GiveWay action is allocated:

- heading **f + sector × 45°**;
- the mover as the human to look at;
- a start delay of `Random_Int(125)` = **0-124 ms**;
- the **boost flag**: set when the mover is a pad player (`+0x1b0` ≠ −1) whose stored gait `+0x1a8` is 3 or more,
  so the stander **dashes** away from a jogging or running player, and steps for anything slower.

`GiveWayAction_Init` (`0x002fe568`) sets brain `+0xcc` bit 1 while it lives. A distance from the mover to the point
is computed and not used.

**No free sector.** For the same seven sectors, in order, the human in each sector (when any) is asked to make room:
`Brain_PushAside(stander, that human, the stander's own position, step)`. The first yes ends the search with yes.
So a crowd can ripple outwards, at most one level deep per call because of bit 4. With no yes, the answer is no.

## Waypoint queues {#queues}

20 records of 0x90 at `0x006cde30` (mask `0x006ce970`), updated after the brains (`0x00299778`); inferred: queues at
choke edges. `0x00299538` lays up to 6 points one metre apart along the edge (`+0x00`, 16 bytes each; count `+0x88`)
between its endpoints `+0x60` / `+0x64`, with 6 humans at `+0x68` and their "settled" flags at `+0x80`. Each update,
humans whose brain `+0xec` is 1 or 2 keep their point and the others are reassigned (`0x00299a08` when there are more
humans than points, else `0x00299b50`), then up to 5 passes swap pairs whose paths cross. Confirmed (code).
