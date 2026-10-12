# AI pathing: holes, claims, jumps and status

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-07), with runtime checks where a section says so; each claim states its evidence level.

## Purpose

The parts of AI route planning and following that [AI humans](ai.md#path-planning) points here for, split out to keep
that page under the size limit: the point-in-a-hole test, the waypoint claims, the jump legs and the status of Coney's
pathing with the fixes in order. The planner itself and the route follower are on [AI humans](ai.md#path-planning).

## A point in a hole {#path-holes}

Confirmed (code) at the addresses unless marked.

- **Inside test** (`NavArea_ContainsPoint` `0x0024ea60`): a winding count over the area's polygons in list order
  (skipping flags 8 and `0x10`), checked after each polygon: once the count drops below 1 the answer is no. A hole
  winds the other way, so a point inside a hole has **no area**: `Nav_FindArea` returns 0. It still records the
  candidate area (the first whose ground byte and box match, `0x00250760`) at `0x006ca234`.
- **The human's polygon** (`Human_GetNavPolygon`): recomputed only when the dirty byte `+0x1b3` is set
  (`Human_SetTransform` `0x0023d480` and `Human_Init` set it), else the cached `+0x1b4` and `+0x2b0` are returned.
  A recompute takes, in order, the first that gives an area:
    1. the human's own position (`+0x2b0` = the position);
    2. `Nav_NearestPolygonPoint` (`0x002505b0`): over the candidate area's polygons (outline and holes), the nearest
       point **on an edge** within 20 m (`Seg_ClosestPoint`, a distance), stopping at the first edge nearer than
       0.18 m (so not always the nearest, and not always the hole's own edge). That point itself is tested with
       `Nav_FindArea`; a point exactly on an edge counts or not by float rounding (inferred). No candidate area →
       this step gives nothing. With no edge within 20 m the point is the previous call's best (a global at
       `0x006ca240` that is not reset);
    3. `Nav_FindPolygonNear` (`0x0024e218`): eight probes from the position raised 1 m, in the order +y, −y, −x, +x,
       (+1, +1), (−1, −1), (−1, +1), (+1, −1) metres in plan, each dropped to the ground (up to 10 m,
       `Collision_DropToGround`); the first with an area and a clear ray (`Ray_IsClear`) from the raised position
       wins. None → 0.

  `+0x2b0` keeps the point that gave the area (the edge point or the probe), and every later test of the route
  request starts there, not at the body. Nothing pushes the body out of the hole; it walks from where it stands.
- **The end point** has no fallback (above): a destination inside a hole fails the request (`+0x284` = 1, the move
  action is done at once). The straight-line test fails first anyway: the segment crosses the hole's edge and the end
  point has no area (step 4 of the walkable-line test).
- **The node search from an edge point** (`Nav_LineClearMask` `0x0024f290`, the line from the point to a node, mask
  0) counts every crossing with t in [0, 1] except t exactly 1. From a start snapped onto an edge, that edge crosses
  the line at t ≈ 0. Whether it counts depends on the sign of the rounding residual of the point against the edge
  (t = cross(point − v, e) / cross(d, e)): with a residual of exactly 0, t = 0 and every node is refused, so no start
  node and the request fails (`+0x284` = 1); otherwise nodes on one side of the edge's line pass and those on the
  other are refused. Inferred from the code; not measured. The walkable-line test (`0x0024fbf8`) is not hurt the same
  way: its nearest crossing (at the start) and the end area's farthest crossing coincide when nothing else is crossed,
  so the straight line from an edge point passes.
- A start found by a probe (step 3) lies 1 to 1.4 m away inside the area and has no such crossing.

## Waypoint claims {#route-claims}

Two humans on one route node take turns: the nearer goes first and the other holds within 2 m. Confirmed (code) at
the addresses cited.

- **The table**: 40 entries of `{node pointer, human handle}` at `0x006ce978`, with an in-use bit mask at
  `0x006ceab8`.
- **`Nav_ClaimWaypoint(human, node, out-link flags)`** (`0x00293e40`):
    1. When the out-link has kind 4 and the node has a jump partner (`NavNode_GetQueueNode` `0x002510f8`, C byte
       `+0x1e`), the claim goes to the node's jump queue instead (`Queue_ClaimForNode` `0x00293d08`: the node's queue,
       or a new one of the 20 at `0x006cde30` with its points laid, [Queues](ai.md#queues)); the answer is the queue's.
    2. Otherwise an entry for the node is looked for. Held by this human → **granted** (1). Held by a handle that no
       longer resolves → it is taken over, **granted**. Held by another human: when this human is strictly nearer
       the node (3D, from both humans' positions in the position table) it takes the entry, the other's claims are
       all released (`Nav_ReleaseClaims`), and the answer is still **refused** (0) for this update; otherwise refused.
    3. No entry for the node: a free entry is filled with this human, **granted**. A full table also grants.
- **`Nav_ReleaseClaims(human)`** (`0x00294088`): frees every entry the human holds and takes him out of every jump
  queue (a queue left empty is freed).
- **Who claims**: `Route_Follow` claims the current waypoint each call (step 5 above), and the corner-speed look-ahead
  (`MoveAction_CollectPoints` `0x002fbd18`, [The move action](ai.md#move-action)) claims each of the next waypoints it
  collects. Moving on releases all of the human's claims, unless the new leg is a jump.
- **Effect**: of two Warriors walking one route, the farther stops within 2 m of each shared node and waits there
  ("held", route state `+0x16` = 1, speed 0) until the nearer has moved on past it and released it; the steering
  ([Steering](ai.md#steering) step 15) reads the same "held" flag.
- **Busy links**: before a leg of kind 4 or `0x80`, with the detail level `+0x333` < 2, the link must be clear:
  another human on it (`Route_IsLinkBusy` `0x0029a3f0`, `0x0029a6c8`, [Jump legs](#route-jump)) makes this one hold
  the same way. The queue's waiting is inferred.

**Confirmed (runtime)**, PCSX2 2.9.94, `level99` reloaded at checkpoint 3 (call hook, [below](ai.md#level99-save)) with
no input: Vermin (brain 5), set at (47.49, 42.97) by the script's `GoalMoveToFlag(Vermin, fVerminFencePoizo, 4, -1,
-1, 0.5, 0, true)`, planned with mask `0xff` a route of two nodes, 205 (46.29, 25.29) then 204 (46.29, 24.29), linked
by kind 8 (the leading nodes 137, 208 and 209 cut, the fence door 2 between 208 and 209 being open: its links'
avoid bit, set in the file, is cleared while the level loads). He ran straight, entered route state 3 at y = 26.6,
crossed the fence in 38 updates (route state 3, waypoint 1) and left it at y = 22.3 with the waypoint index at 2,
then ran on to (46.31, 20.18) and stopped. Over the graph read from that state, any mask without 8 (`0x3`, `0x13`,
`0x93`) finds no route between the two points: the yard behind the fence is reached only over its kind-8 links.

## Jump legs {#route-jump}

A kind-4 link crosses a gap or drops down an edge. The follower reaches the take-off waypoint like any other, starts
the jump (or the drop) on the update after, and leaves the air to the landing code. Confirmed (code) at the addresses
cited, and at runtime where marked.

**Where it starts.** There is no edge test and no distance of its own: the jump starts on the update after the
follower moves on from the take-off waypoint to the landing one. Moving on is the usual arrival test, which the
human's state update runs every update, in the air too (`0x0023f9d0`, from `Human_StateUpdate` `0x0023fea8`): while
the front action is a move action, brain `+0x11d` is set when the distance to the aim point (brain `+0x90`) less one
update's travel (human speed `+0x1ac` / 30) is within the aim radius (brain `+0x118`); the distance is in 3D, or in
plan when the 3D distance is 1 m or less. The move action then asks the follower to move on (`0x002fc674`), and
`0x0029b6d8` makes the landing waypoint current with the radius 0.25 m and route state `+0x12` = 1 (the leg is new).
The take-off waypoint is never skipped (its outgoing link is not of kind 1 or 2), and moving on over a kind-4 link
keeps the waypoint claims (`0x00294088` is not called). So the jump starts within 0.25 m plus one update's travel of
the take-off point.

**The two points** (`0x0029b2b8`). Nodes whose C record byte `+0x1e` is non-zero come in pairs: `0x002510f8` finds the
linked node with the same byte, and the pair is an edge to jump from or to.

- **Take-off** (a waypoint whose outgoing link is kind 4): the human's point in the queue on its edge
  (`0x002941c0`), when a queue holds the node and the human and he can walk there in a straight line (`0x002221e0`);
  otherwise the node. The claim makes the queue on demand (`0x00293e40` → `0x00293d08`): `0x00299538` lays
  trunc(edge length) + 1 points, at most 6, evenly from the partner node to this one, and the queue update gives each
  human the nearest free point (`0x00299b50`). A lone human so takes off from the nearest of those points.
- **Landing** (the waypoint the kind-4 link enters): with a partner node, the take-off point projected onto the line
  through the two nodes (`0x00336e08`, not clamped to the segment); while the landing waypoint is current, the
  take-off point is the human's own position. Otherwise the node. So the jump goes square across the landing edge.

**The handler** (`0x0029baa8`), run by the follower on every update while the leg is new:

1. Let *d* be the landing point less the human's position and *D* its length in plan. Aim at the point (brain
   `+0x90`, radius `+0x118` = the waypoint radius), set the heading `+0x110` along *d* in plan, clear `+0x11c`, and
   turn the body to that heading (`0x0021b100`).
2. **Drop or jump.** With the link's avoid bit (31) set, jump. Otherwise *t* = `0x003378b0`(7.84, 0, *d*.z) (below):
   when *t* > 0 and *D* / *t* < 10, **drop**: brain speed `+0x114` = *D* / *t*, route state 2. Otherwise **jump**:
   `Human_BeginJump(human, 1)` (below); refused → the handler returns 2 and the move action ends (`GoalMoveToFlag`
   queues another and plans again); accepted → route state 1.
3. It returns 0, so the move action does nothing else that update: no steering round humans, no corner speed.

**`0x003378b0(a, b, c)`** returns the larger real root of *a t*² + *b t* + *c* = 0, or 0 when there is none. 7.84 is
half the fall's gravity (15.68 m/s², `Human_StateUpdate`), so with (7.84, 0, *d*.z) it is √(−*d*.z / 7.84): the time
to fall from rest to the landing point's height, real only when the point is below. The **drop** therefore runs at
the speed that covers the remaining plan distance in that time, recomputed each update until the human leaves the
ground (the follower then stops running, below), so it slows as it nears the edge and leaves it at the speed that
lands it on the point (inferred from the formula). Over 10 m/s, or with the landing point level or higher, it jumps
instead. A drop of more than about 7.1 m lands faster than the fall-damage threshold of 14.9 m/s
([Characters: Falling](characters.md#falling)).

**`Human_BeginJump` for an AI** (`0x0023db48`): with the argument 1 it refuses while record `+0x08` holds any of
`0x5cfeafb` or the state flags any of `0x7bf9e9f7ff0` (airborne and landing among them). The gait (3 or more) and
3.3 m/s tests apply only to a brain of type 0, the player: an AI jumps at any gait, from a standstill too, and its
take-off gait `+0x3c0` is not written. No run-up or lining up is needed; the handler has already turned the body.

**The launch** (`Human_LaunchJump`, `0x002217f0`, on the same update). For a human whose per-player record `+0x1b` is
0 it takes the velocity from `0x0029ade0` instead of the player's run speed ([Characters: Jumping](characters.md#jump)):

- route state 1: turn the body to the landing point again; try the vertical speeds *v* = 0.5, 1.25, 2.0, … m/s
  (steps of 0.75) and, for each, the time *t* at which the arc *v t* − 7.84 *t*² comes down to *d*.z (the larger
  root of −7.84 *t*² + *v t* − *d*.z = 0); keep the first *v* for which *t* > 0 and *D* / *t* ≤ 10 m/s, giving up
  after *v* passes 5.5 m/s (`0x00510188`, so at most 5.75). The velocity is *D* / *t* along *d* in plan and *v*
  upwards: an arc that ends on the landing point;
- any other route state: route state 5 (the route ends) and the velocity is left as it was.

Then the fall starts and anim state 26 (clip 434), as for the player.

**In the air** the follower does not run: `MoveAction_Update` returns 0 at once while `Human_IsBusy` holds, and it
holds while any of the state flags `0x1c00000000` (jumping, falling) is set (`0x00227f90`). The arrival test above
still runs, so `+0x11d` is set in the air when the arc passes within 0.25 m plus one update's travel of the landing
point.

**Landing** (`Human_Land`, `0x0023e090`), for an AI whose front action is a move action (and unless `0x0015e718`
reports mode `0x11`):

- **Near**: a route in state 1 or 2 and the human within 1.5 m (3D) of the current waypoint (`0x0029b170`): the actions
  run again at once with the busy test off (`0x005112b4` = 1 around `Brain_RunActions`). With `+0x11d` set the move
  action moves on to the next waypoint and its leg; without it the leg is still new and the handler runs again from
  where the human stands (inferred: another jump when `Human_BeginJump` allows it, else the move ends and is planned
  again).
- **Otherwise** (no route, another route state, or farther than 1.5 m): `Brain_PopAction(brain, 1)` ends the move
  action; `GoalMoveToFlag` then queues a new move from where the human landed, which plans a new route. This applies
  to any landing during a move, a fall off an edge included.

**The clear test, claims and queue for a lone human.** The clear test (`0x0029a3f0`, before a leg of kind 4, with
the detail level `+0x333` < 2) looks only at other humans: one within 4 m in plan and ahead of this one (along its facing),
among those `0x002278a8` gathers along the leg and `0x00290230` accepts, blocks the link when it is moving (a move
action and a speed above 0); one standing is pushed aside (`0x0028a248`) and the link counts as blocked for that
update. Blocked and within 2 m (3D) of the waypoint, the human holds there with speed 0. The claim (`0x00293e40`) is
refused while another human holds the node (the nearer of the two takes it, for the next update). With no other
human near, neither changes anything; the queue still picks the take-off point (above).

**Confirmed (runtime)**, PCSX2 2.9.94, `level99` checkpoint 3.4 (the rooftops), from the window-jump state of
[Objects](objects.md#pane) with the player run at the window (stick 100 % ahead, triangle at frame 59) so that
`vReachWindow` sends Vermin to `fStop_03`: Vermin ran from (62.80, −3.30) at 7.80 m/s to the take-off point
(50.744, −2.830, 4.252), set the arrival flag 0.004 m from it, and on the next update had route state 1, waypoint 1,
the aim (46.619, −2.830, 4.252) (the landing point, square across from him: its y is his), the heading 3π/2 and clip
434. The launch gave *v* = 3.5 m/s (0.5, 1.25, 2.0 and 2.75 need more than 10 m/s) and 9.546 m/s in plan (0.318 m
per update), as the arc above gives for *D* = 4.121 m and *d*.z = +0.05 (*t* = 0.432 s). The arc stopped against the
window at x = 47.76, where the two type-11 panes stand (x 47.40); he fell 4 m into the gap, landed 4.2 m from the
waypoint, and the move action ended on that update; nine updates later a new route took him along the street and up
the fence (route state 3 at (56.78, 7.24)). Putting the player in `vReachWindow` without his own jump gave the same
jump and fall.

## Pathing: status and fixes {#pathing-fixes}

The owner's play-test (2026-10) found that Warriors lose the player across alleys and that AI humans walk into walls
where the original takes a climb. Comparing Coney's `repo:src/ai/route_planner.cpp`, `route_follower.cpp`,
`move_action.cpp`, `crew.cpp` and `formations.cpp` with the sections above gives the list below. Each item is one
small pull request, in the order to do them: the first three are the likely causes of what was seen. Each names the
section it implements and a test written first (synthetic path maps, as in `repo:tests/ai/route_planner_test.cpp`;
the disc checks print counts only).

1. **A\*'s limits** ([Path planning](ai.md#path-planning) step 4). Coney fails a search once 128 nodes are on the open
   list (`kMaxSearchNodes`); the original allows 1000 on the open heap and 3000 closed, and fails only when the
   **route** would have more than 128 nodes. This made a third of `level99`'s reachable pairs fail, so a planner
   that should cross the map found no route (a likely cause of `level87` checkpoint 4's "no route out of the street").
   Also: the 65000 cap is on the running g, not on one edge; distances are 3D, truncated to whole 1/16 m per edge.
   *Tests*: a 600-node grid whose only way round passes 300 nodes plans a route; a 140-node corridor fails (route
   over 128); the disc check's "failed, linked" count for `level99` drops to 0 (`[disc][routes]`).
   **Done in Coney** (`RoutePlanner::search`, which now says why it failed: `SearchFailure`). Measured on the disc
   (`[disc][routes]`, 300 seeded pairs of nodes per level that the default mask's links join, NTSC-U, counts only):
   the old 128-open cap lost 7 of `level87`'s 213 linked pairs and none of `level99`'s 189 (the "third of
   `level99`'s pairs" above came from an older count that put the cap's failures among the unlinked ones); with
   the original's limits every linked pair is found except `level80`'s routes of more than 128 nodes, which the
   original refuses too.
2. **The edge cost** (step 4, "The new g"). The three ClimbFilter extras are exclusive (8, else `0x80`, else 4, and
   4 not with `0x40` + avoid), not added together; the use term is 40 × *u* − 8 only when *u* > 0, and *u* is the
   use count of the **expanded** node (the one whose record it is), not the neighbour's. *Tests*: an edge with flags
   `0x8c` costs distance + 200; a node with 0 uses adds nothing; two routes through one node make a third prefer a
   parallel way (cost compare on a two-lane map). **Done in Coney** (`RoutePlanner::edgeCost`, each addition
   wrapping at 16 bits as the page says). With item 1's limits, the routes of more than 128 nodes among `level80`'s
   seeded pairs fell from 19 to 2: the old use term took 8 off every step, so the cheapest route was the one with the
   most nodes.
3. **FollowFormation and FollowPlayer** ([The default command: follow](ai.md#warrior-follow)). Coney's
   `FollowFormationGoal` ends when the follower has no slot, and a slot is usable only when the leader sees its
   point, so a crew stops as soon as the chief turns a corner or crosses a fence. The original then follows the human
   ahead in the formation's queue (radius 2.5 m) and keeps going. Port, in this order: the follow point with the
   queue fallback; `Nav_WalkDistanceTo` (straight distance, else the planned route's length; fail only with no area
   or no route) and its 30-update wait with the vandalise check every 15; done only when the leader stands and the
   walk distance is within the radius; the speed from `FollowFormation_PickSpeed` (replace the 3 m run/walk
   stand-in) using the walk distance; the move refreshed once a second or when the leader just stopped. In
   `FollowPlayerGoal` replace "back to the slot every 31 updates" with the stand-or-follow test (within 9 m, same
   `+0x2d5`, straight walkable line to the leader) and the failure back-off of 31 updates. *Tests*: a chief walking
   round an L-shaped wall keeps his two followers moving (they reach within 2.5 m of the one ahead); a follower
   within 9 m in line stands; one 12 m behind runs (gait 4 speed × f) and slows to the walk near the slot; across a
   kind-8 fence pair the follower climbs. A disc test on `level80` replays the runtime check of
   [the follow](ai.md#warrior-follow): after "Let's Go" the crew asks 12.81 m/s from the landing, stands while the
   player runs within 9 m, and climbs the crate stack by its `0x80` links when the player stands on it.
4. **Every 31 updates the aim must stay in sight** ([The move action](ai.md#move-action) step 5). Coney never ends a
   move because its waypoint went out of a walkable line, so an AI pushed off its line (by steering, a shove, a
   corner cut) walks into the wall until the stuck test, which a wall slide may never trip. Port the check (ends the
   move with `+0x284` = 0, so the goal plans again), the clearing of `+0x284` at the start of every move update, and
   the climb's give-up after 31 tries ending the move without a failure code (Coney sets "stuck", which ends
   FollowFormation). *Tests*: a human whose waypoint is put behind a wall by a teleport ends its move within 31
   updates and the goal replans; a failed climb leaves `moveFailure()` at none.
5. **Following details** ([Following a route](ai.md#route-follow)). The skip check runs on every **7th** call and its
   counter restarts whenever the route moves on; the skip needs both links of the waypoint to be kind 1 or 2 (Coney
   only excludes jump legs, so it may skip a climb's or a door's waypoint); there is no turn limit on long legs
   (remove `kLongLeg` / `kSkipTurnLimit`: the original's test always passes); the arrival test subtracts one
   update's travel (speed / 30) for **every** waypoint, in 3D unless within 1 m; a new leg tests climb and jump
   before the avoid bit. *Tests*: per rule, a two-leg route on a synthetic map.
6. **Waypoint claims** ([Waypoint claims](#route-claims)): the 40-entry table, the nearer human takes over (the
   other's claims all released, the taker refused for that update), hold within 2 m (3D) with speed 0, release on
   moving on except into a jump, and the corner look-ahead's claims. Replaces the steering's stand-in for "held".
   *Tests*: two humans on one route, the farther holds within 2 m of the shared node until the nearer passes it;
   a vanished holder's claim is taken at once.
7. **The corner speed** ([The corner speed](ai.md#corner-speed)): the braking distance is the 32 m/s² stopping distance
   ((v₁ − v₂)/32 = *t*, *d* = v₁*t* − 16*t*²), not 0.5 s at the first speed; the simulation's sub-steps by the current
   speed (4/5/6/7), points every *k* sub-steps, early stop when aligned; recompute every 11 updates or when the aim
   moves. *Tests*: v₁ 7.8, v₂ 3.4 → *d* = 0.77 m (*t* = 0.1375 s); a straight route keeps the run speed.
8. **Areas from the ground** ([Path planning](ai.md#path-planning), "A point's area"): the ray starts 0.4 m above the
   point and is 4 m long (Coney: from the point, 10 m), and the first area of the list whose byte, box (+0.35 m),
   vertex count and flags match is the candidate. *Tests*: a point 3.7 m above a floor finds no area; a point
   0.3 m below a walkway's surface finds the walkway's area.
9. **The retry without `0x80`** (step 5): the second route is never kept in the original (both g read 0); Coney keeps
   the cheaper one. Keep the first route. **Route building** (step 6): shortcuts never to the last node; the
   trailing cut's line runs from the destination to the node. *Tests*: a chain whose last node links back to the
   first keeps its middle nodes.
10. **Formation slot points** ([Formations](ai.md#formations)): drop each slot's point to the ground (a ray from 1.9 m
    above to 5.9 m below) instead of keeping the leader's height, and test usability with the line from the leader
    (`Human_CanWalkStraightTo`). *Test*: a leader on a kerb 0.3 m up gives slots on the road their road height.
11. **The stuck test** ([The move action](ai.md#move-action) step 9): 3D distance, on the 61st counted update. *Test*: a
    human climbing a steep heap 0.2 m per 60 updates in height is not stuck.

**Gait choice** (when an AI walks, jogs, runs or sprints) is per goal, not per distance, except in the follow code:
the table in [Gaits and speeds](ai.md#ai-gaits) gives each goal's gait, the corner speed lowers it before tight corners,
a climb leg runs at gait 4, and `FollowFormation_PickSpeed` blends the leader's speed with the distance (item 3).
Coney already takes each goal's gait from that table; item 3 is the gap.
