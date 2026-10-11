# AI pathing: holes, claims and status

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-07), with runtime checks where a section says so; each claim states its evidence level.

## Purpose

The parts of AI route planning and following that [AI humans](ai.md#path-planning) points here for, split out to keep
that page under the size limit: the point-in-a-hole test, the waypoint claims, and the status of Coney's pathing with
the fixes in order. The planner itself, the route follower and the jump legs are on [AI humans](ai.md#path-planning).

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
  another human on it (`Route_IsLinkBusy` `0x0029a3f0`, `0x0029a6c8`, [Jump legs](ai.md#route-jump)) makes this one hold
  the same way. The queue's waiting is inferred.

**Confirmed (runtime)**, PCSX2 2.9.94, `level99` reloaded at checkpoint 3 (call hook, [below](ai.md#level99-save)) with
no input: Vermin (brain 5), set at (47.49, 42.97) by the script's `GoalMoveToFlag(Vermin, fVerminFencePoizo, 4, -1,
-1, 0.5, 0, true)`, planned with mask `0xff` a route of two nodes, 205 (46.29, 25.29) then 204 (46.29, 24.29), linked
by kind 8 (the leading nodes 137, 208 and 209 cut, the fence door 2 between 208 and 209 being open: its links'
avoid bit, set in the file, is cleared while the level loads). He ran straight, entered route state 3 at y = 26.6,
crossed the fence in 38 updates (route state 3, waypoint 1) and left it at y = 22.3 with the waypoint index at 2,
then ran on to (46.31, 20.18) and stopped. Over the graph read from that state, any mask without 8 (`0x3`, `0x13`,
`0x93`) finds no route between the two points: the yard behind the fence is reached only over its kind-8 links.

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
2. **The edge cost** (step 4, "The new g"). The three ClimbFilter extras are exclusive (8, else `0x80`, else 4, and
   4 not with `0x40` + avoid), not added together; the use term is 40 × *u* − 8 only when *u* > 0, and *u* is the
   use count of the **expanded** node (the one whose record it is), not the neighbour's. *Tests*: an edge with flags
   `0x8c` costs distance + 200; a node with 0 uses adds nothing; two routes through one node make a third prefer a
   parallel way (cost compare on a two-lane map).
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
