---
kind: topic
system: characters
owns:
  functions: [
    "0x00219d50", "0x0021a490", "0x0023d8c8", "0x0023dc58", "0x0023e090", "0x0023e408", "0x0023eab8", "0x0033e278",
    "0x003477c0", "0x00347c08", "0x003a2158", "0x003a21c0",
  ]
---
# Characters: the ground, walls and falls

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

How a human stays on the ground, meets walls and steps and slopes, falls and lands: the move each update, the
ground snap, falling out of the world, the walking sphere's sweep against the collision mesh, the slopes between
floor and wall, and the fall and the landing with its damage. The sprint, the jump and the climb that leave the
ground are on [Traversal](characters-traversal.md); the speeds that move the body are on
[Locomotion](characters-locomotion.md).

## Map {#original-structure}

The functions of the move, the sweep and the fall:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0023d8c8` | `Human_Move` | slope factor, physics sweep, ground snap | confirmed (code) |
| `0x0023eab8` | `Human_SnapToGround` | 1.5 m ray down from 1 m above the feet | confirmed (code) |
| `0x0023dc58` | `Human_StartFall` (vtable `+0x154`) | airborne flag, fall target, camera ledge hint | confirmed (code) |
| `0x0023e408` | `Human_LandingTest` | the airborne sweep's floor test (`n.z` > 0.65) | confirmed (code) |
| `0x0023e090` | `Human_Land` | clears airborne, fall damage, `vz` = 0 | confirmed (code) |
| `0x003a2158` / `0x003a21c0` | `Object_SetAirborne` / `Object_SetGrounded` | flag `0x4000000` / `0x2000000` in `+0x54` | confirmed (code) |
| `0x0033e278` | `PhysicsBody_Sweep` | moves a body by `v × dt`, up to three sliding passes | confirmed (code) for the steps listed |
| `0x00347c08` | `PhysicsMesh_SweepCapsule` | the walking body's sphere swept against the mesh's walls, with the 0.25 m step rule | confirmed (code) |
| `0x003477c0` | `PhysicsBody_PushOutOfWalls` | sphere against the collision mesh's walls (`n.z` within ±0.65) | confirmed (code) |
| `0x0021a490` | `Human_PushOutInAir` | the push-out sphere (or a bone segment) while airborne and in some states | confirmed (code) |
| `0x00219d50` | `Human_OnContact` | the body's contact handler: landings, fences while climbing, objects | confirmed (code) for the parts cited |

## Data

The ground's fields (`+0x54` object flags, `+0x1d8` the material under the feet, `+0x230` the ground normal) are in [the
human object](characters/index.md#the-human-object); the constants of each rule are given with it under Behaviour.

## Behaviour

### Moving, standing on the ground and falling {#ground}

The locomotion above only sets a velocity. Moving the human, keeping it on the ground and making it fall happen in
the human's state update (vtable slot `+0x13c`, `0x0023fea8`) and its move step (`0x0023d8c8`), with the physics
world (`0x00597198`, `Physics/`) doing the sweep against walls. Confirmed (code) unless marked.

**Where the transform lives.** The authoritative position and rotation of every task-manager object are in a table at
`0x00714b00`, 32 bytes per object (`position` vec4, then `rotation` quat), indexed by the object's handle index
(`+0x92`). `SetPosition` (`0x0021b0b8`) writes the table; the human copies it into `+0x10` / `+0x20` at the start of
each update (vtable `+0xac`). The velocity is at `+0x30` (vec4), read through `0x003a1f00` (zero while
flag `0x600` is set in `+0x54`); writing it (vtable `+0x74`, `0x0023cf00`) also stores its length at `+0x1ac` and the
gait for that speed (`0x0022aeb0`) at `+0x1a8`. So `+0x1ac` (used by the lean) is the current speed. Confirmed (code).

**Airborne flag.** Bit `0x4000000` of the object flags at `+0x54` means "in the air". `0x003a2158` sets it (and clears
`0x2000000`, "on the ground"); `0x003a21c0` does the reverse. Confirmed (code).

**One update, in order** (the parts that move the body; the human's other duties are left out):

1. Copy the transform from the table; run the current state's function (for the player, the locomotion,
   [Locomotion](characters-locomotion.md#locomotion)), which sets the velocity.
2. **Speed sanity check:** a velocity longer than 50 × the human's scale (`+0x65c`, 1.0 normally; `Human_GetScale`,
   `0x0021d020`; confirmed (code)) is zeroed.
3. **Gravity**, only while airborne: from the second airborne update on (the counter `+0x384` counts airborne
   updates), `vz -= 15.68 × dt` (0.5227 m/s per 1/30 s update; 15.68 m/s² is 1.6 g), clamped at **−50 m/s**. The
   result is written back with the velocity. On the ground `+0x384` is reset to 0.
4. **Fell out of the world:** if the position is more than **20 m below the collision mesh's lowest vertex** (mesh
   header `+0x98`, [Collision](collision.md#header)), the human gets record state **`0x200000000`**, the bit that
   has him taken away (not dead, which is `0x100000000`), its velocity zeroed, and for a player in a mission whose
   game-state flags `+0x150` have bit 1, `W_GameState + 0x14c` is set to 1 and `+0x152` to 2 (a mission failure,
   [Level loading](level-loading.md#a-frame-of-play)). The test is in the airborne branch only, at `0x002403ec`.
   Confirmed (code).
   What follows: [Falling out of the world](#out-of-world).
5. **Stuck in the air:** after 60 airborne updates, if the physics body's "could not move" counter (body `+0x70`) is
   also above 59, the human is put back at its last ground position (`+0x390`) and landed (`Human_Land`, `0x0023e090`).
   Confirmed (code).
6. **Move** (`Human_Move`, `0x0023d8c8`, below; confirmed (code)).

**The move.** With velocity `v` (and an external push `+0x2f0` added, which knock-backs use):

- A human without a physics body (`+0x1a0` = 0), riding something (`+0x280` ≠ −1) or in state `0x40` simply moves:
  `position += (v + push) × dt`.
- Otherwise, on the ground, **`v.z` is set to 0** first: walking never climbs by velocity, only by the ground snap
  below. On a slope (ground normal `z`, `+0x238`, below 0.95) and outside some states (`0x00227f68`; confirmed (code)),
  the velocity is scaled by `clamp(0.6 + 0.3 × (n.z − 0.5), 0.5, 1.0)`: 0.735 just below the threshold, 0.71 on a 30°
  slope (`n.z` = 0.87), 0.5 from `n.z` ≤ 0.17. (The code also computes the facing just before, but the scale does not
  depend on it, so it slows uphill and downhill alike; and there is a step from 1.0 to 0.735 at `n.z` = 0.95.)
- The physics world then **sweeps the body** (`PhysicsBody_Sweep`, `0x0033e278(dt, world, body, &v, &out)`, confirmed
  (code)): the body's pending push-out (body `+0x50`, divided by dt) is added to the displacement `v × dt`, which is
  swept up to three times, sliding along what it hits. If it is still blocked after three passes, the body stays where
  it is, its horizontal velocity is zeroed and its "could not move" counter (`+0x70`) goes up; otherwise the counter is
  reset. Walls come from the level's [collision mesh](collision.md) through the body's shape: a **sphere swept along the
  move**, below ([Walls and steps](#walls)).
- **Ground snap**, on the ground only (and not in game modes 8, `0xb` or `0x11`, `0x00221950`), `0x0023eab8`: cast
  a ray **straight down from 1.0 m above the feet, 1.5 m long** (the 1.0 is vtable `+0x5c`, a constant). Confirmed
  (code).
    - **Hit:** put the feet **exactly on the hit point** (no gap), whatever the hit triangle's normal: the snap has
      no floor test ([Slopes](#slopes)), remember it as the last ground position (`+0x390`
      before the snap, `+0x240` after), store the ground normal at `+0x230` (its `z` at `+0x238`, used by the slope
      rule) and the material at `+0x1d8`. The triangle's flag bits 4 and 5 are passed on (bit 4 to a per-player
      "under cover" state, `0x0028ef00`; bit 5 to `0x002195e0`; meanings inferred from what the callees touch).
    - **Miss** (nothing within 0.5 m below the feet): the human **starts to fall** through vtable `+0x154`
      (`0x0023dc58`) unless it is in state `0x800`. Confirmed (code).
  So a step or kerb up to **1.0 m** high is climbed in one update when the sweep lets the body over it, and a drop of
  up to **0.5 m** is followed without falling; anything deeper is a fall. The sweep lets the body over a step only
  when the step's wall triangles are under 0.25 m tall ([Walls and steps](#walls)).

#### Falling out of the world {#out-of-world}

A human more than 20 m below the collision mesh's lowest vertex is taken away, not killed outright, and his scripts
hear of it as of any knock-out. In order:

1. **The bit.** `Human_StateUpdate` (`0x0023fea8`, step 4 above) sets record state `0x200000000` with
   `Human_SetStateFlag` (`0x002265d0`, the record's word `+0x00`) and zeroes the velocity. It is the bit `Human_Delete`
   (`0x00233ef0`, `HuDelete`) sets too, and the one `Humans_Update` sets on a dead human no camera sees
   ([Combat reactions](combat-reactions.md#knocked-out-bit)). Confirmed (code); at runtime the record's high word
   read 2 a few updates after a pedestrian was moved to z = -300 (confirmed (runtime), run
   r-20261122-fall-out-of-world). Dead is `0x100000000`, a different bit. A player in a mission also fails it (step 4).
2. **Still alive to the scripts.** `HuIsAlive` (`Human_IsAlive`, `0x00235628`) is false only for a human down or dead
   (`Human_IsDownOrDead`, `0x00227e60`, mask `0x180050000`) or not up (`Human_IsUp`, `0x0029ce40`, `0x100000000`):
   neither mask holds `0x200000000`, so while he fades the scripts see him alive. Confirmed (code).
3. **The fade.** In the actions loop of `Humans_Update` (`0x00249108`), a human with `0x200000000` and no down latch
   (`0x40000000`, [Combat reactions](combat-reactions.md#scripted-knockdowns)) skips his actions; while his character
   instance's colour alpha (instance `+0x2b`, human `+0xd8`) is 11 or more it drops by 10 an update and the down update
   runs, else `Human_Destroy(human, 0)` (`0x0021c7c8`) is called. The state update runs before the actions loop in the
   same `Humans_Update`, so the first drop is in the update that sets the bit. Confirmed (code). The alpha was 255
   before the fall and 5 at the call to `Human_Destroy` (return address `0x002495ac`, second argument 0): 25 drops,
   the destroy on the 26th update (confirmed (runtime), run r-20261122-fall-out-of-world).
4. **The knock-out.** `Human_Destroy` knocks out a human not yet dead (`Human_KnockOut`, `0x00230328`, [Destroying a
   human](characters/index.md#destroy) step 5). `Human_KnockOut` sets `0x40000` and calls `Brain_OnKnockedOut`
   (`0x0028c3b0`), which, the human being knocked out and not wounded, sends him **event 18** (`0x12`) about his foe:
   his own message-18 handler, then his gang's, then his brain (`Gang_OnEvent`, `0x00164c20`, passes the gang handler
   the count still standing, `Gang_CountStanding` `0x00166220`, which leaves him out as knocked out). Then
   `Human_CreditDefeat` (`0x00230740`) and `Brain_TearDown` (`0x0028c4f8`) take him out of his gang and send event 2, he
   is made dead (`0x100000000`) and his handle is freed. Confirmed (code). At runtime a message-18 handler set on the
   human (`SetMsgHandler`) and one on his gang (`GangSetMsgHandler`) each ran once, the gang's with 0 standing (he was
   its only member), `HuIsAlive` was already false inside the handler and stayed false after (confirmed (runtime), run
   r-20261122-fall-out-of-world).

So the fall ends a fight the scripts wait on as a knock-out would: a Brawl fighter's `StandardGang1Dead` /
`StandardGang2Dead` ([Rumble](rumble.md#game-types)) runs about a second after he passes the limit (inferred from the
code above; the Brawl itself was not run with a fall).

**A player outside a mission** gets the same bit and the same fade, and nothing in the path tests for a player, so he
is destroyed as well (`Human_Destroy`'s player branch takes him off the player list, [Destroying a
human](characters/index.md#destroy) step 9); inferred from the code, not run. Coney keeps its put-back for the player
([In Coney](#coneys-implementation)).

#### Walls and steps {#walls}

The physics body of a human holds a capsule shape (type 3, shape `+0x30`; its radius at shape `+0x40`, 0.35 for a human
of scale 1.0, confirmed (runtime), run r-20261124-chars-ai-sphere-radius, and a second size at `+0x44`), and the physics
world handles a shape through a table of functions by type (world `+0xe0`, `0x0033d2d8`, which passes −0.65). For type 3
that is `PhysicsMesh_SweepCapsule` (`0x00347c08`), which treats the body as **one sphere** for walls. Confirmed (code):

1. **The sphere**: radius `r` = shape `+0x40` × body `+0x60` × the human's scale (`Human_GetScale` `0x0021d020`:
   `+0x65c`, or 1.0 in a scene state when it is under 1.1), centre **`r + 0.05` above the feet**
   (`0x00347c98`-`0x00347cd8`). The shape's `+0x40` is itself 0.35 × the scale (`Human_GetCapsuleRadius`
   `0x00219860`, written by `PhysicsBody_ApplyHumanScale` `0x00341f28`), so **the scale counts twice**:
   `r` = 0.35 × scale² × body `+0x60`. Confirmed (code). For the player, body `+0x60` (and `+0x64`) is **1.4286**
   (0.5 / 0.35), so `r` = 0.5 × scale²: Rembrandt (scale 0.97) has `r` = 0.3395 × 1.4286 × 0.97 = **0.4705 m**, and
   the sphere spans 0.05 to 0.99 m above the feet. Confirmed (runtime), run r-20261124-chars-sweep-radius, the
   `sweep-radius` hook at `0x00347cbc` in the `sweep60` scenario (a copy of slot 1, level99's street, stick 60 % up for
   80 updates): all 39 calls logged the player's body with shape `+0x40` × body `+0x60` = 0.4850, body `+0x60` = 1.4286
   and `Human_GetScale` = 0.9700,
   and the hooked `mul.s` multiplies the two. A measured 0.485 m is that product before the second scale; the radius
   the walls see is 0.4705 m. The stops measured before (0.476-0.480 m from a full wall, a wall at an angle and the
   fence of slot 7; the step faces below) sit 0.006-0.010 m beyond where a 0.4705 m sphere touches, which fits a
   small clearance left by the sweep (inferred; where it comes from is not traced). Only a player's body has
   `+0x60` = 1.4286 (`Human_CreatePhysicsBody` `0x0021ce08` for a brain of type 0, `Human_MakePlayer` `0x00229c40`;
   `Human_ReleasePlayer` `0x0022a2a8` puts back 1.0, [Physics](physics.md#sweep)). **Every AI human's is 1.0**, so
   its sphere is shape `+0x40` (0.35 × its class scale) × its scale: 0.350 m for Vermin, 0.357 m for level99's bums
   and civilians of scale 1.01 (fields read in slot 6). Confirmed (code); the values confirmed (runtime), run
   r-20261124-chars-ai-sphere-radius.
2. For each enabled triangle of the grid cells it covers that is a wall (`|n.z| ≤ 0.65`) and that the sphere is in
   front of (a two-sided triangle is turned to face it), and that the move goes into (`n · move < −0.001`):
3. **Skip low and thin triangles**: take the edge whose unit vector is steepest; if its two ends differ in height by
   less than **0.25 m**, skip the triangle. Then take the longest edge; unless it is nearly vertical (`|unit z|` ≥
   0.8), skip the triangle when the third corner lies less than 0.25 m from that edge's line within the triangle's
   plane.
4. Sweep the sphere along the move against the triangle (`Sweep_SphereTriangle`, `0x0034ee60`; confirmed (code)); a hit
   at a fraction from 0 to 1 is a contact. The contacts go to the body's contact handler (`Human_OnContact`,
   `0x00219d50`), which for the level's triangles slides (code `0x20001`), lands on a floor contact (flag `0x80`), and
   during a climb (record `+0x08` `0x40`) **ignores** triangles of materials 30 (`LOW_FENCE`), 31 (`OPAQUE_FENCE`) and
   122 (`RAILING`), so the body passes through the fence it climbs. No other material has a rule here: a glass pane's
   `GLASS` triangles are a wall like any other until the pane breaks ([Moving into a pane](objects.md#pane-break)).

So on the ground the original has **no step height of its own**: a wall face shorter than 0.25 m is not a wall, and
the ground snap (1.0 m up, above) then lifts the feet onto it; any face 0.25 m or taller that reaches into the sphere
(0.05-0.99 m above the feet for the player) stops the body. A kerb of 0.2 m is walked onto; a ledge of 0.5 m or
0.75 m is a wall to walk into and needs a climb (a short wall from 0.7 m, [Climbing](characters-traversal.md#climb)) or
a jump.

**The step at runtime** (confirmed (runtime), run r-20261124-chars-step, a copy of slot 1 in `level99`'s world). The
street there has no kerb (no wall face under 0.35 m rises from the street level), so a box of the level's collision
mesh (material 41, 2.62 × 0.62 m, against a wall; its eight vertices are used by its own ten triangles only) was
made into a step by writing its vertices' heights in RAM: bottom at the floor (0.213), top 0.10 to 0.30 m higher.
The player was put 1.26 m in front of it, facing it, and walked in with the stick at 35 %, 50 % or 100 %, turned by
the camera so that the move pointed straight at the face:

| Step height | Stick | Result |
| --- | --- | --- |
| 0.100, 0.200, 0.240 m | 35 %, 50 % (walk 1.63 m/s) | walked on: the feet rose by the step's height **in one update**, on the update the body's centre passed over the edge, with no change of speed or clip |
| 0.245 m | 100 % (run start, 5.6 m/s) | walked on, the same way |
| 0.255 m | 100 % | stopped 0.395 m short of the face, idle with the stick held |
| 0.260, 0.300 m | 50 % | stopped 0.399 m and 0.423 m short of the face |

So the 0.25 m test is exact, and the stopping distances are those of the 0.4705 m sphere centred 0.5205 m above the
feet touching the face's top edge with about 0.006 m to spare: `sqrt(0.4765² − (0.5205 − h)²)` gives 0.396, 0.399
and 0.422 m for `h` = 0.255, 0.26 and 0.30, and against a full-height wall the same walk stopped 0.476 m from it
(0.4705 + 0.006). The clearance is fitted to these four stops (inferred). An earlier reading fitted the three step
stops to a 0.485 m sphere without clearance, but that missed the full wall by 0.009 m, and the radius is 0.4705 m
(step 1).

**Sliding along a wall** (confirmed (runtime), run r-20261124-chars-wall-slide, slot 1): the velocity the sweep leaves
after sliding is the human's velocity, so the next update's current speed is its length and the 0.8 m/s gain starts from
there: each update's speed is `k × (last speed + 0.8)`, `k` the sine of the angle off the normal. Running into a wall
19.8° off its normal, the speed went 7.80, 2.64, 1.17, 0.67, 0.50, 0.44, 0.42 m/s, one value per update, and settled at
0.415, against `0.8 k / (1 − k)` = 0.416 at 20°, just above the idle threshold (a quarter of the sneak-walk speed, 0.396
m/s), so the run went on with the stick pushed. By the same rule a wall met less than about 19.3° off its normal settles
under the threshold and the idle plays with the stick still pushed (inferred from the rule and the threshold; no
recorded run covers it). A walk does the same (inferred from the rule, which does not depend on the gait). So the player
brakes against a wall met at a steep angle instead of sliding along it at speed.

#### Slopes between floor and wall {#slopes}

A face too steep to be floor and not steep enough to be a sheer wall (`n.z` between 0.65 and about 0.5) is a **wall
the body slides up**, not a wall that stops it. Confirmed (code) unless marked.

**The thresholds.** Only two tests read a normal, and the ground snap reads none:

| Test | Rule | Where |
| --- | --- | --- |
| The walking sweep's walls | `n.z` from −0.65 to **0.65**, the move going into the face, 0.25 m tall or more | `PhysicsMesh_SweepCapsule` `0x00347c08` (the −0.65 comes from `0x0033d2d8`), [above](#walls) |
| An airborne body's landing | `n.z` > **0.65** | `0x0023e408` ([Falling and landing](#falling)) |
| The ground snap | **none**: the feet go onto the first triangle the ray meets (mask 0) | `Human_SnapToGround` `0x0023eab8`; confirmed (runtime), run r-20261124-chars-heap-walk, walking at full speed down a face with `n.z` 0.54 |
| The slope's speed | `n.z` < 0.95: × `clamp(0.6 + 0.3 × (n.z − 0.5), 0.5, 1.0)` | the move `0x0023d8c8` ([above](#ground)); confirmed (runtime), run r-20261124-chars-heap-walk |

Nothing else changes them:

- **The material.** `Human_OnContact` (`0x00219d50`) answers `0x20001` (slide) for every level triangle; its only
  material rule is the climb's pass-through of 30, 31 and 122. `GRAVEL` (106) and the rest have no movement rule.
- **The triangle's bits 14-15.** The sweep reads them as the edge of a corner slide; 3 names none, and the gravel
  faces below hold 3.
- **The route.** A leg of kind 1 is walked with no handler of its own ([AI: link kinds](ai-moving.md#route-follow)). The
  sweep, the contact handler and the snap read neither the brain nor its route, and an AI's body moves through the
  same move as the player's. Two things differ: the sphere's radius (body `+0x60`, about 0.35 m for an AI,
  [above](#walls)), and the slid velocity, which is written back only to a player's body (body flag `0x400`,
  [Physics](physics.md#sweep)). An AI asks for its full speed again each update, so a slide never brakes it.

**How the sweep slides up such a face:**

1. A face the sphere already reaches (start distance to the plane at most `r`) is sorted by where the centre lies
   (`Sweep_SphereTriangle` `0x0034ee60`; edge `k` runs from corner `k` to `k + 1`, and the centre is inside it when
   `(e_k × n) · c ≤ (e_k × n) · v_k`):
    - **inside all three edges** (case 7): a contact at **fraction 0**;
    - **outside one edge** (cases 3, 5, 6): that edge alone (`SphereSweep_Edge` `0x0034e3b8`). When the edge
      segment already passes within `r` of the centre (`Segment_SphereIntersect` `0x0034e120`: the segment's roots
      against the sphere overlap 0 to 1) the contact is at **fraction 0**; otherwise the move is swept against the
      edge as a cylinder and the first root from 0 to 1 is the contact;
    - **outside two edges**, past a corner (cases 1, 2, 4; `SphereSweep_VertexRegion` `0x0034e5d0`): a corner
      already inside the sphere (`|v − c|` < `r`) gives **no contact at all**, nor does a move going away from it
      (`move · (v − c)` ≤ 0); otherwise one of the corner's two edges is swept as above.

   A sphere not yet at the plane is sorted the same way at the point where it first touches the plane. Every case
   hands back only a fraction: there is no edge or corner normal and no push-out. Nothing pushes a walking sphere
   out of a face it overlaps: `PhysicsBody_PushOutOfWalls` runs only in the air (below).
2. The contact carries the triangle's own unit normal, turned to the sphere for a two-sided one (contact `+0x50`,
   copied in `0x00347c08`), with its `z`, **in every case of step 1**: a sphere that touches the face only past its
   edge slides along the face's plane exactly as one inside it.
3. The slide (`PhysicsBody_ResolveContacts` `0x0033d9d8`, code 1) removes from what is left of the move, and from
   the velocity, the part that goes into that normal: `v −= (v · n) n` on x, y and z, only when `v · n` < 0
   (`Vec_RemoveNormalPart` `0x0033d870`). The move then goes **along the face, rising**.
4. On the next pass the slid move no longer goes into the face (`n · move` ≥ −0.001), so the face is skipped. There
   are up to three passes.
5. The velocity keeps the slid `z`, but the next update sets `v.z` to 0 before its sweep ([The move](#ground)). The
   snap then puts the feet on whatever is under them.

So walking at a face of `n.z` 0.53 at 20° off its normal, the move keeps its part along the face. The snap holds the
feet on the floor triangle in front of the face, and the body climbs that floor along the face's foot until it rises
past the face. The sphere sinks steadily into the face, and nothing corrects that. The body has **no step height of
its own**: it relies on the 0.25 m wall rule and the snap's 1.0 m reach, [above](#walls).

**At runtime** (a copy of slot 1). Confirmed (runtime), run r-20261124-chars-heap-walk (the other rows: runs
r-20261124-chars-heap-run and r-20261124-chars-heap-walk-b60). The test used `level99`'s gravel
heap, where the path data routes a kind-1 walk from (−271.7, 129.4, 0.3) up to the lesson-2 platform at (−277.7,
132.0, 3.1). The faces on that line:

- triangle **679**: `GRAVEL`, flags `0xc001`, `n` (0.736, −0.134, 0.664), a floor;
- triangle **680**: `GRAVEL`, flags `0xf001`, `n` (0.849, −0.055, 0.526), a wall to the sweep: 1.77 m tall, its
  steepest edge rising 1.77 m.

Rembrandt was put at the walk's start facing its end. The stick was pushed straight up from update 60, and the camera
put the move about 64° from north toward the west. Every run took the line onto 679 first:

| Run | What happened |
| --- | --- |
| walk, stick 60 % | 1.63 m/s on the street. On 679 the walk was turned about 30° north along 680's foot, at 0.41 m/s across the ground and 0.29 m/s up: the feet followed 679's surface (ground normal 0.664 every update) while the slid velocity held `z` 0.36. The sphere's distance to 680 fell from 0.48 to 0.30 m (radius 0.4705). After 3.5 s at z 1.45 the body passed 680 onto faces with `n.z` 0.77 at 1.107 m/s across the ground (1.63 × 0.6797, the slope rule), crested at z 2.05 and walked down the far side, including a face with `n.z` 0.54 at full walk, onto the street |
| run, stick 100 % | the same: stopped for 5 updates at 679's foot, then 0.51-0.56 m/s up 679 along 680, then a run at 7.80 m/s over the crest at z 2.34 |
| walk, stick 60 %, body `+0x60` set to 1.0 (radius 0.34, an AI's size if its `+0x60` is 1.0) | the same path: up 679 along 680 (distance to 680 about 0.32 down to 0.21 m), over at z 2.37, down the other side |

So a human on that leg does climb the heap in the original: by sliding along 680, not by walking up it. The lesson's
walk reaching the platform itself was not run (the stick's line passed 1 m north of it).

**An AI on the same leg** (a copy of slot 6, scenario `repo:research/traces/scenarios/heap_ai.toml`; confirmed
(runtime), run r-20261124-chars-heap-ai, and for the 1.3429 body run r-20261124-chars-heap-ai-b60). Vermin (dead brain,
radius 0.350) was put at (−271.7, 129.4, 0.3) with the player standing 4 m behind, and his own `GoalMoveToFlag` was
re-aimed at (−285.2, 124.2, 0.3), gait 2 (walk, his 1.654 m/s), radius 2, and his actions cleared. The planner gave him
the same route as Coney's: (−277.71, 132.00, 3.12), (−280.76, 130.48, 3.76), a drop leg, the target. Each update, read
over PINE:

| Stretch | What happened |
| --- | --- |
| street, updates 1-81 | 1.654 m/s straight at the first waypoint, 20° off 680's normal in plan |
| 679, along 680's foot, 82-110 | the centre's projection **0.36 to 0.02 m outside the crease edge** (679-680) with the edge 0.50 down to 0.27 m from the centre (radius 0.35): the edge case of step 1. He moved 0.036 m across and 0.039 m up per update over 82-88, then 0.013-0.015 m across the ground (0.42 m/s) and 0.012 m up per update over 89-110, turned 20-30° north of his aim, while his asked speed (`+0x1ac`) and velocity stayed 1.654 m/s; the "could not move" counter stayed 0 |
| inside 680, 111-155 | the projection inside the face (step 1's first case), the centre sinking to 0.21 m from its plane; feet still on 679 until update 140, then on 680 itself (`n.z` 0.526): 0.33 m/s across and 0.45 m/s up |
| crest | the route moved on to (−280.76, 130.48) at update 160, before he reached the first waypoint (the route's straight-line skip, [AI](ai-moving.md#route-follow)); he crested at z 3.73, took the drop leg and stopped 1.94 m from the target at update 487; around updates 196-209 his aim went back to the first waypoint for a moment |

The same run with his body `+0x60` raised to 1.3429 (radius 0.470, Coney's) was alike: the crease edge 0.49 down to 0.35
m from the centre, the projection 0.27 to 0.05 m outside it, never blocked; this passes within 0.5 m of the point where
Coney's bum used to stall. Neither run tripped the move action's stuck test (0.2 m in 60 updates,
[AI](ai-moving.md#move-action)): 1.14-1.30 m was covered in each 60 updates on 679 and 680, and 0.55-0.76 m after update
190. The waypoint was not skipped to get round the heap, and nothing warped him.

The push-out `PhysicsBody_PushOutOfWalls` (`0x003477c0`, a sphere out of the nearest wall by `n × (r − distance)`)
is not the walking case: its only caller `Human_PushOutInAir` (`0x0021a490`) runs while the human is airborne
(object flag `0x4000000`), in a few states (`0x00227ef8`, `0x00223b48`, record `+0x08` `0x400000`, anim id 2 or 4),
and not in states `0x00223920` / `0x00223980`. Its sphere has radius 0.35, or **0.5 for a player**, times the scale,
centred `r + 0.05` above the feet (at bone 2 in states `0x00227ef8` / `0x00223b48`). In a long fall (state flag
`0x1000000000`) and some states it instead pushes out a 0.2 m capsule between bones 6 and 3 (`0x0033e7f0`, the
average of the contacts). Confirmed (code).

#### Falling and landing {#falling}

**Starting to fall** (`0x0023dc58`): march a ray along the velocity (`Collision_MarchRay`, steps of 0.1 m, up to 5 m)
to find where the fall will end (kept at `+0x2c0`; the current position if nothing is hit), set the airborne flag
(`0x003a2158`) and switch the body to its falling mode (body slot `+0x2c` with 2). For a player whose follow camera is
in its normal mode, a ray 2.5 m down from 0.75 m ahead of the feet checks for a drop and, if there is no ground,
tells the camera (`0x0012da20` with the heading, −1.0): the camera reacts to a ledge. Confirmed (code).

**Landing.** While airborne, the sweep tests the segment from the body's upper point (feet + `+0x4e8` − 0.16 × scale)
to the moved feet against the collision mesh (`0x0023e408`, through `WorldManager_RayCast`); a hit on a triangle with
`n.z` > **0.65** is a landing contact. The contact handler (`0x00219d50`) then calls `Land(human, 1, 1)`
(`0x0023e090`): clear the airborne flag, apply **fall damage** by the vertical speed at impact
(`vz` ≥ −14.9 m/s: none; between −14.9 and −20.5: a share of the maximum health, `(vz + 14.9) / −5.6`, at least the
class's minimum `+0x118`; below −20.5 m/s: the maximum health, so a fall that fast kills), tell a player's camera
when `vz` < −1.5 (camera slot `+0x15c` with the landing kind 1-3), set `+0x3a0` (the vertical velocity) to 0 and
write the velocity back. The thresholds are `0x00510674` (−14.9) and `0x00510678` (−20.5). Free fall from rest
reaches 14.9 m/s after about 7.1 m and 20.5 m/s after 13.4 m (inferred from the gravity above).

**When the landing happens** (the feel pass's second runtime run, slot 1, a run jump with stick 100 % and
triangle tapped 35 updates in; the arc's phase shifted by adding 0.015 to 0.15 m, or taking 0.05 or 0.12 m, to the
feet's height at the 7th update in the air; ground at 0.2231). Confirmed (runtime), run r-20261124-chars-run-jump;
confirmed (runtime), run r-20261124-chars-land-p015; confirmed (runtime), run r-20261124-chars-land-p030;
confirmed (runtime), run r-20261124-chars-land-p050; confirmed (runtime), run r-20261124-chars-land-p100;
confirmed (runtime), run r-20261124-chars-land-p150; confirmed (runtime), run r-20261124-chars-land-m050;
confirmed (runtime), run r-20261124-chars-land-m120:

| Feet at the start of an airborne update, below the ground | That update |
| --- | --- |
| above it, or 0.041, 0.091, 0.111, 0.141 or 0.161 m below | still airborne: the body falls a whole step (0.20-0.22 m) more |
| 0.176 or 0.191 m below (and 0.241-0.379 m) | lands: `Land`, the feet snapped up onto the ground, full horizontal speed |

So the feet are **not** stopped at the ground on the update they pass it: the jump lands on the first update that
**starts** with the feet about 0.17 m (between 0.161 and 0.176 m) or more below the ground, and until then the body
keeps falling through it, up to 0.38 m below in one phase. The unshifted jump (the street's arc) crosses with the feet
0.009 m above the ground, ends that update 0.191 m below and lands on the next: the 0.19 m dip of the per-update
trace is this, not a sampling artefact. The code path is the sweep's landing contact (`0x0023e408`, run from
`0x0033d340` when body flag `0x40` is set; segment from feet + `+0x4e8` − 0.16 × scale, 0.880 m for the player,
to the moved feet), which records the contact's fraction as `(hit − 1.0) / (length − 1.0)`, or 0 when the hit is
nearer than 1.0 m (the human's vtable `+0x5c`, which returns the constant 1.0:
[Human_LandingTest](physics-bodies.md#u3-landing)); why the hit is not taken until the feet are about 0.17 m under is
not traced. What the code shows (confirmed (code)):

- the segment's upper point is `0x00226a20`: the feet + `+0x4e8` − 0.16 × the scale (confirmed (code));
- only a hit with a normal `z` above 0.65 records a landing contact;
- `0x0033d340` marks the contact (`+0xb0` = `0x80`) and its handler `0x00219d50` calls `Land(1, 1)` (confirmed (code));
- contacts are kept sorted by fraction (`0x0033d718`), and the resolution `0x0033d9d8` acts on the first contact
  whose handler returns a code that is not 0, then returns (confirmed (code));
- the body's move loop `0x0033e278` runs the sweep up to 3 times (confirmed (code)).

Read alone, this lands on the update the moved feet pass the ground, which the runtime contradicts; the 0.17 m does
not follow from the 0.88 / 1.0 m geometry either. A log of the contacts (a code patch that copies each contact's
fraction and handler code to free memory) would settle it.

**A fall at runtime** (`level99`, no stick input; Rembrandt raised 10 m by writing the transform table's `z`).
Confirmed (runtime), run r-20261124-chars-fall:

- `vz` changes by −0.5227 m/s per update from the third airborne update (the first two keep it 0), and `z(n+1) = z(n) +
  vz(n+1) × dt`: the velocity is updated before the move.
- Clip 428 (drop cycle) plays while falling.
- Landing after 34 updates at `vz` = −17.25 m/s, inside the damage band; then 429 (drop land), 294 (a hit reaction)
  and 198 (a ground roll).

**Anim states in the air.** `Human_ChooseAnimState` (`0x00259578`) checks the state flags first: `0x2000000000` →
anim state **27** (landing), `0x800000000` or `0x1000000000` → **26** (falling), `0x400000000` → **25** (jump).
Confirmed (code). The builders, confirmed (code):

- 25 (`0x0025fba0` → `0x0025cf30`): the **jump loop** (434) after a 0.1 s fade, then the launch
  ([Jumping](characters-traversal.md#jump)); confirmed (code).
- 26 (`0x0025d020`, confirmed (code)): the **drop cycle** (428), or the long-fall cycles 422 / 425 in a long fall
  (`0x400000`, `0x00223b48`, `0x00227e60`). After a jump the jump loop keeps playing (confirmed (runtime), run
  r-20261124-chars-run-jump: 434 in anim state 26 for the whole flight).
- 27 (`0x0025d390`): after a jump, **436** (jump end running) handing over to a gait blend when the stick is above
  0.12, else **435** (jump end) and then the idle; after a drop the same 436 and gait blend with the stick above 0.12,
  else **429** (drop land) at a rate of `1 − 0.5 ×` a random unit, then the idle; after a long fall
  (`0x1000000000`), the clip after the current one, or 198 (ground roll). Confirmed (code). The stick test is a pad
  player's (per-player record `+0x1b` set); the function also has these branches, confirmed (code) at the same address:
    - While the global `0x0051031c` is set, a pad player takes the stick-released branch whatever the stick (what sets
      the global is not traced). Confirmed (code).
    - An AI (`+0x1b` 0) takes 436 and the gait blend when its brain's speed `+0x114` is at least its jog speed
      (`Human_GetJogSpeed`), after a jump or a drop; otherwise 435 after a jump and 429 after a drop. After a drop the
      hidden-in-shadow test comes first, so a hidden AI gets 429 whatever its speed. Confirmed (code).
    - A human hidden in shadow (`Human_IsHiddenInShadow`) gets 429 after a drop whatever the stick for a pad player, and
      whatever the brain's speed for an AI. After a jump a hidden pad player with the stick released gets 429 instead of
      435 (with the stick above 0.12 he still gets 436). For a hidden pad player the 429 starts after a 1/6 s fade on an
      idle loop with an end callback of its own, and his player state `+0x54` takes the game time (`*(0x0050b734) +
      0x48`); a hidden AI gets the plain 429. Confirmed (code).

**Walking off an edge at runtime** (down the sloping front of a car whose roof is 1.48 m up, from z 1.29 m to the
street at 0.22 m, a drop of 1.07 m; stick 0.5). Confirmed (runtime), run r-20261124-chars-car-roof-walk-off: state 26
with 428 for 12 updates, record `+0x08` `0x400000` for the first four of them, gravity from the third as above; then,
the stick still held, **436** for 11 updates, the first 5 in state 27 with `+0x08` `0x1000000`, the rest in the
locomotion state 4, and then the gait blend. With the stick released in the air (confirmed (runtime), run
r-20261124-chars-car-roof-walk-off-release) the landing is **429** and the idle; 429 lasted 13 updates in that run.
Its rate is random (confirmed (code), above), so its length varies from one landing to the next.

## In Coney {#coneys-implementation}

- `src/human/body.*` holds the walking body's sizes for [Walls and steps](#walls): the sphere of 0.35 × scale²
  (× 1.4286 for the player: 0.4705 m) centred 0.05 above its radius, the 0.25 m wall height, the fence materials
  passed through while climbing over, and the airborne push-out sphere (0.5 × scale for a player). An AI human's
  sphere is 0.35 × scale × scale (`human::aiWalkingRadius`, its body `+0x60` being 1.0). The walls themselves are
  the physics world's sweep (`src/physics/`, [Physics](physics.md#coneys-implementation)), run by `Human::sweep`:
  wall triangles `|n.z|` ≤ 0.65 that the move goes into, the 0.25 m low-and-thin rule, the sphere swept from where
  the move starts and met where it first touches (face, edge or corner), advanced to 0.01 m short of it along the
  normal, then the slid move swept again, up to three passes. So a face lower than the sphere's centre stops it
  where it meets the face's top edge, and a face under 0.25 m is walked onto in one update with no change of speed
  or clip. Only a player's slid velocity is written back (`v −= (v · n) n`, the slide response), so a wall met at a
  steep angle brakes him; an AI's velocity is never braked by a slide. Nothing pushes a walking sphere out of a face
  it overlaps, so a move into a face leaning back rises along it ([Slopes](#slopes)). The corner slide (flag bits
  14-15) is added to the move on the ground unless the human is in a pair or blocking. While airborne the same
  sweep takes the move across the ground, and the air sphere's push out of the nearest wall waits for the next
  move.
- **Falling**: state 26's builder loops the drop cycle (slot 26, 428) with the idle's 0.15 s fade; a drop lands
  straight into the idle or the move, without the drop land (429), the long-fall cycles or the ground roll.
- **The body** is the original's walking sphere ([Walls and steps](#walls)), but pushed out of the nearest wall
  triangle by the distance to the triangle's closest point rather than swept along the move; the push tries 3
  passes, then stops. The low-and-thin rule takes the triangle as given, so a 0.25 m face exactly (a sliver) does
  not stop the body, and a face from 0.26 m does.
- **Landing** probes from 1.0 m above the feet, as the ground snap does. A floor found on that segment is landed on
  by the first update that starts with the feet 0.17 m or more below it ([When the landing happens](#falling)); until
  then the body falls on through it (0.19 m after a run jump, as at runtime). The landing is at that update's position
  across the ground, so the landing update keeps the full horizontal speed.
- **The landing** has no fade, so 436 moves the body at its 4.23 m/s from its first update as at runtime, and hands
  over to a gait blend at the jog (1.0).
- **Out of the world** (20 m below the mesh's lowest point, [Falling out of the world](#out-of-world)): an AI human
  fades by 10 an update from 255 (`Human::fade()`, kept but not drawn: he is far below the level) and is destroyed on
  the 26th update through `Humans::setDestroy()`: `ai::Brains::destroy()` knocks him out with event 18 about his foe,
  so his message-18 handlers and his gang's run (the gang's count still standing leaves him out), the scripts forget
  his handle (`ai::ScriptedBrains::humanDestroyed()`, so `HuIsAlive` is false) and he leaves the world at the step's
  end (`ai::AiHumans::destroy()`). **Coney's choice**: the player is put back at the start instead of failing the
  mission or, outside one, being destroyed. **Not ported** ([#1640](issue:1640)): the rest of `Human_Destroy` (the
  statistics, event 2, the held object, sounds, icons and spawner).

### Notes for implementers {#notes-for-implementers}

- **Ground**: no vertical velocity on the ground; snap the feet with a ray from 1.0 m above, 1.5 m long, each update;
  a miss starts a fall with gravity 15.68 m/s² (from the second airborne update), capped at 50 m/s; land on floors
  with `n.z` > 0.65; fall damage from 14.9 m/s, a kill from 20.5 m/s ([above](#ground)). A fall or jump lands on
  the first update that starts with the feet about 0.17 m or more below the floor: keep moving the full step until
  then (feet up to 0.38 m under), then land at that update's position with the horizontal speed kept
  ([When the landing happens](#falling)).
- **Walls**: a sphere of radius 0.35 × scale² × body `+0x60` (1.4286 for the player: 0.4705 m), centred that radius
  plus 0.05 above the feet, swept along the move; the slid velocity is kept, so a steep wall brakes the player;
  wall triangles less than 0.25 m tall are not walls, which is all the step-up there is ([Walls and steps](#walls)).

## Open questions

- **The body's shape** (answered): a capsule shape whose walls are a swept sphere ([Walls and steps](#walls)). Still
  open: what the capsule's 1.886 (shape `+0x44`) and the human's `+0x4e8` are used for.
- **The airborne anim state** (answered): states 25-27 ([Falling and landing](#falling)); air control
  ([Jumping](characters-traversal.md#jump)). Whether locomotion turns the human while a start clip plays (answered: it
  does, [Tasks](tasks.md#locomotion-gate)).
- **Step height at runtime** (answered): steps up to 0.245 m are walked onto in one update, from 0.255 m they stop
  the body ([Walls and steps](#walls)), measured on a test step made in the mesh. Still open: a real kerb in a later
  level, as corroboration.
- **The landing's threshold**: why the landing contact is taken only once the feet start an update about 0.17 m
  below the floor ([When the landing happens](#falling)). The contact path is traced (the resolution `0x0033d9d8`
  acts on the first contact by fraction) and does not explain it; a runtime log of the contacts is needed.
- **The landing's fade** and the gait blend value 436 hands over to; the drop land (429) and the long falls are not
  built in Coney.

## How this was checked

Code claims were read in Ghidra in `SLUS_212.15`.

The step was made from a box of the level's collision mesh by writing its vertices' heights in RAM (scenario
`chars_step`); the landing's phase was shifted by writing the feet's height at the 7th update in the air (scenario
`chars_run_jump`, its `shift` value).
