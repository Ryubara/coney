# Physics

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-06); no runtime claims yet.

## Purpose

The physics world is what moving things collide through: the bodies and shapes of humans and world objects, the
sweep that moves a body against the level's [collision mesh](collision.md), and a small per-tick step. It is **not a
rigid-body simulator**: nothing in it integrates velocities, resolves body-against-body contacts or applies impulses.
Each mover integrates itself in its own update (humans in [`Humans_Update`](tasks.md#humans-update), world objects
on the [wheel](tasks.md#wheel)) and asks the physics world to sweep it.

The **60 Hz step** (`0x00340918`, called on every tick of [the play tick](tasks.md#tick)) does two small jobs: it
plays the "settle" turn of a thrown or dropped object that has landed, and every 2 s it refreshes a broad-phase
bound. Neither moves a human or decides a hit.

## Original structure

One object of class `IPhysics` (name string `0x005502b0`), 0x4f460 bytes, created at boot into `0x00597198`
([Boot](boot.md#initialisation-order)). Its code lies before and in `c:/Warriors/Source/Physics/physics.cpp`
(path `0x005771d0`; [Source map](source-map.md#physics)): the constructor `0x0033c288` starts right after the heaps'
block allocator, so `0x0033c288`-`0x003418f8` is `Physics/` too (inferred; no path string names the file).

**In-house, no middleware.** Confirmed (code) for the names: the class is `IPhysics` and the path is the game's own
`c:/Warriors/Source/Physics/`. Inferred for the absence: no string of the executable names Havok, Karma, Meqon, ODE
or a ragdoll, and the linked libraries ([Source map](source-map.md#middleware)) include no physics library.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0033c288` | `IPhysics_Construct` | the pools, the shape-dispatch tables, the human body sets | confirmed (code) |
| `0x00340918` | `IPhysics_Step` | the 60 Hz step: settle turns, broad-phase bound refresh | confirmed (code) |
| `0x003404a8` / `0x003405d8` / `0x00340628` | `BodyList_Insert` / `_Remove` / `_Reinsert` | the bodies sorted by AABB min x | confirmed (code) |
| `0x00340fe0` | `IPhysics_StartSettle(phys, obj, normal)` | takes a settle slot for an object | confirmed (code) |
| `0x003410f0` | `IPhysics_CancelSettle(phys, obj)` | frees the object's slot, clears its `0x40000` | confirmed (code) |
| `0x00340d08` | `Settle_ComputeTarget` | the end rotation of a settle | confirmed (code) |
| `0x00340b38` | `Settle_NearestAxis` | the object's local axis nearest a direction | confirmed (code) |
| `0x00336a00` | `Quat_Slerp(t, out, from, to)` | spherical interpolation, shortest arc | confirmed (code) |
| `0x00335b08` | `Quat_IntegrateAngular(dt, q, ω)` | `q += ½ ω q dt`, normalised | confirmed (code) |
| `0x003418f8` | `PhysicsBody_Construct` | a 0x80-byte body | confirmed (code) |
| `0x00341e40` | `PhysicsBody_Moved` | updates the shape, re-sorts the body | confirmed (code) |
| `0x0033e278` | `PhysicsBody_Sweep` | moves a body against the mesh ([Characters](characters.md#walls)) | confirmed (code) |
| `0x00394050` | `WorldObject_OnContact` | the world object's contact handler (vtable `0x005453a0` `+0xfc`) | confirmed (code); role inferred |
| `0x00395a10` | `WorldObject_Integrate` | the world object's integrator (vtable `+0x144`) | confirmed (code) |
| `0x00395b70` | `WorldObject_Update` | the world object's update (vtable `+0x13c`) | confirmed (code) |
| `0x00391c10` | `WorldObject_Remove` | cancels a settle, leaves the `ObjectTaskManager` | confirmed (code); role inferred |

The world object here is the class of vtable `0x005453a0`, allocated from the `ObjectTaskManager`'s pool
(`0x004f3e98`) and one of the four classes seen on the wheel ([Tasks](tasks.md#wheel)); its kind is an
`ObjectAttribs` entry (`0x00512c04`, indexed by the object's `+0x112`).

## Data

### IPhysics {#iphysics}

Confirmed (code) at `0x0033c288` and the step. Pool sizes are counts × bytes.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x08` | ptr | head of the body list, sorted by AABB min x |
| `+0x12` | u16 | ticks since the last bound refresh (0-119) |
| `+0x14` | float | the widest body in x, `max(max.x − min.x)` |
| `+0x18`-`+0x11c` | fn tables | per-shape-type functions; `+0xec` is `PhysicsMesh_SweepCapsule` (`0x00347c08`), the walking sphere's sweep |
| `+0x120` | 300 × 0x60 | shapes |
| `+0x71a0` | 300 × 0x70 | shapes |
| `+0xf7b0` | 60 × 0x60 | shapes |
| `+0x10e30` | 60 × 0x60 | the humans' capsules, made with (2.0, 0.35) by `0x00343508` |
| `+0x124b0` | 60 × 0xe0 | the humans' bodies: each holds its capsule (`+0x30`) and ten bone shapes (below) |
| `+0x15930` / `+0x22930` / `+0x2c530` | 416 × 0x80 / 0x60 / 0x90 | bodies, shapes, shapes |
| `+0x3af30` / `+0x42eb0` | 255 × 0x80 / 0x90 | bodies, shapes |
| `+0x4be20` / `+0x4c720` / `+0x4db60` | 18 × 0x80 / 36 × 0x90 / 30 × 0x60 | bodies, shapes, shapes |
| `+0x4e6a0` | 64 × 0x30 | settle slots (below) |
| `+0x4f2a0`-`+0x4f3f0` | | one more human body set (capsule 0.35, `+0x4f3e4` = 1.5) |

The 60 human body sets match the 60 human slots ([AI humans](ai.md)), inferred. Each gets five 0x70 shapes on
bones 3, 24, 18, 32, 29 and five 0x60 shapes on bones 6, 25, 19, 33, 30 (the byte at shape `+0x31`; reading it as a
bone index is inferred), probably the volumes hits test against (speculative).

**Body** (0x80, `0x003418f8`): `+0x00` AABB min, `+0x10` AABB max, `+0x24`/`+0x28` previous/next in the sorted list,
`+0x30` shape, `+0x40` flags (`0x80000400` at construction; the step sets `0x80000000`), `+0x50` pending push-out,
`+0x60` scale, `+0x74` vtable (`0x00544710`).

**Settle slot** (0x30), free when `+0x00` is 0:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | ptr | the object |
| `+0x04` | float | progress `t`, 0 to 1 |
| `+0x08` | float | acceleration of `t`, per tick²: 0.02 (`0x3ca3d70a`) |
| `+0x0c` | float | speed of `t`, per tick |
| `+0x10` | quat | end rotation |
| `+0x20` | quat | start rotation |

Object flags (`+0x54`, [task head](tasks.md#task-object)): `0x40000` settling, `0x8000000` settled.

## Behaviour

### The step {#step}

`IPhysics_Step(phys)` (`0x00340918`), once per 60 Hz tick after the wheel. Confirmed (code):

1. **Bound refresh.** Count the tick in `+0x12`; at 120 (every 2 s), reset it and recompute `+0x14` as the widest
   body in x over the list. Inserting a body (`0x003404a8`) only ever grows `+0x14`, so the refresh is what lets it
   shrink. That a query reads `+0x14` to look back along the sorted list is inferred (the classic sort-and-sweep
   use); the readers are not traced.
2. **Settle turns.** For each of the 64 slots in use: `t += speed`, `speed += 0.02`, clamp `t` to 1; set the object's
   rotation to `slerp(start, end, t)`, keeping its position; if the object has a body, mark it (`+0x40 |=
   0x80000000`) and let the body follow the object (body vtable `+0x14`). At `t` = 1 the object's flags lose
   `0x40000` and gain `0x8000000`, and the slot is freed.

Starting from rest, `t` after *n* ticks is `0.01 × n(n − 1)`, so **a settle takes 11 ticks (183 ms)**, slow at
first and fastest at the end, like something tipping over.

### Settling a landed object {#settle}

`WorldObject_OnContact` (`0x00394050`) gets the contact (hit object at `+0x40`, normal at `+0x50`, point at
`+0x90`). Confirmed (code): when the contact is the ground (no hit object and normal z > 0.7), the object's kind has
the `ObjectAttribs` byte `+0x85` set, and it is neither settling nor settled (`0x8040000`), it starts a settle
(`0x00340fe0`): the first free slot (or the object's own), start = its current rotation, speed 0, its angular
velocity zeroed (vtable `+0x7c` with the zero vector `0x005116c0`, inferred), flag `0x40000`. With all 64 slots busy
nothing happens. The end rotation (`0x00340d08`) turns the object by the smallest angle that lines up its local axis
nearest the contact normal (`0x00340b38`, among the axes a mask allows) with that normal, the turn wrapped to ±90°:
it comes to rest on its nearest face.

On a later ground contact with `0x8000000` set, the object stops: velocity and angular velocity zeroed, flags
`0x4000000` (airborne) and `0x8000000` cleared, `0x2000000` (grounded) set. Removing an object (`0x00391c10`)
cancels its settle.

### How things actually move {#movers}

None of this is in the step; it is listed so an implementer knows where each motion lives. Confirmed (code):

- **Humans** sweep their body in `Human_Move` at 30 Hz ([Characters](characters.md#walls)). The physics world gives
  the shape and the sweep; no body pushes another here.
- **World objects** integrate in their update: by default `Task_Integrate` ([Tasks](tasks.md#task-object)),
  explicit Euler with dt = phase time − time of the last update (`0x003a1688`), so dt is the wheel interval.
  Airborne and with a body (`0x00395a10`), velocity first: `vz −= 15.68 × dt`, then `PhysicsBody_Sweep` with that
  velocity, then the rotation by the angular velocity unless settling. On the update after a human lets go (`+0x10c`
  set, holder at `+0x11c`), the object starts at the holder's `+0x5f0` with velocity from the holder's `+0x600` and
  that first sweep uses dt = 1/60 s.
- While airborne or settling (`0x4040000`, or body flag `0x800`), an object's interval is **2 ticks**
  (`0x00395b70`): it flies at 30 Hz, like the humans.
- **A thrown object hitting a human** is decided in its contact handler, from its own sweep: the hit object is
  tested as a human (`0x00229868`) and handled by `0x003928d0` and `0x00392b88` (roles inferred).

### 30 Hz or 60 Hz, and online play {#tick-rate}

What follows is inferred from the code above.

- **Gameplay does not depend on the step.** Humans step at 30 Hz, flying objects update at 30 Hz, and hits come from
  sweeps in those updates. The step only turns landed objects and keeps a broad-phase bound.
- **At a 30 Hz step**, the settle must be rescaled to keep its 183 ms: with acceleration 0.08 per step², it ends in
  6 steps (200 ms). The bound refresh becomes every 60 steps. Neither changes play.
- **Online**, nothing the step holds needs syncing beyond an object's settled state: given the landing normal, a
  settle is deterministic and cosmetic, so a client can play it locally or take the object's rotation from the
  server. The state that matters is a flying object's position, velocity and holder, and the humans' state, all
  outside this step. The physics is the game's own short, fixed sequence of float operations, with no third-party
  solver to make deterministic.

## Coney's implementation

Coney has the walking body's sweep (`src/human/body.h`, `src/raycast/`) but no `IPhysics`, no world objects and no
step yet. When world objects come, the settle is a per-object tween run on Coney's fixed step, not a separate pass.

## Open questions

- Which queries read `+0x14`, and the sort-and-sweep they do.
- What the 416- and 255-body pools hold (world objects, glass?); the 18-body pool matches the 18 cars
  ([Tasks](tasks.md#classes), inferred).
- What the human's ten bone shapes are used for, and the shape types in the dispatch tables.
- Where the axis mask of `0x00340b38` comes from for a settle.
- How cars move (`0x0038e590`) and whether they use this world.
- A runtime check of the settle's 11 ticks on a thrown bottle.
