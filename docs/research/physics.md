# Physics

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-06, the whole of `Physics/` on 2026-10-07); the game under PCSX2 2.9.94 (PINE, hooks and `pcsx2 record`, in
`level99`, 2026-10-07).

## Purpose

The physics world is what moving things collide through: the bodies and shapes of humans and world objects, the
sweep that moves a body against the level's [collision mesh](collision.md) and the other bodies, the queries other
code asks (a shape, a box or a ray against the world), and a small per-tick step. It is **not a rigid-body
simulator**: nothing in it integrates velocities or applies impulses, and a contact changes only the moving body's
velocity, as its owner's contact handler decides ([Contacts](#contacts)). Each mover integrates itself in its own
update (humans in [`Humans_Update`](tasks.md#humans-update), world objects on the [wheel](tasks.md#wheel)) and asks
the physics world to sweep it.

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

Every function of `Physics/` (`0x0033c288`-`0x0034f740`), by area; the sections below say what each does. Names are
ours (some set by the [Combat](combat.md) analyst) and match the local Ghidra project; all confirmed (code) at the
address unless a section says otherwise.

| Area | Functions | Evidence |
| --- | --- | --- |
| The world | `IPhysics_Construct` `0x0033c288`, `IPhysics_Step` `0x00340918`, `Physics_StaticInit` `0x0034f6c0` and its stub `0x0034f718` (copy a 48-byte constant from `0x00511720` to `0x006eb9e0`) | confirmed (code) |
| [Bodies](#bodies) | create and free: `IPhysics_CreateHumanBody` `0x0033cc48` / `_FreeHumanBody` `0x0033ce00`, `_CreatePunchBagBody` `0x0033cb08` / `_FreePunchBagBody` `0x0033cbf0`, `_CreateObjectBody` `0x0033ce58` / `_FreeObjectBody` `0x0033cf00`, `_CreateCarBody` `0x0033cf68` / `_FreeCarBody` `0x0033cff8`, `Physics_CreateBoxBody` `0x0033d070` / `IPhysics_FreeBoxBody` `0x0033d158`; lists: `IPhysics_AddBody` `0x00340668`, `_RemoveBody` `0x00340700`, `_AddHumanBody` `0x003407a0`, `_RemoveHumanBody` `0x003407f8`, `BodyList_Insert` / `_Remove` / `_Reinsert` `0x003404a8` / `0x003405d8` / `0x00340628`; the body: `PhysicsNode_Init` `0x00341158`, `PhysicsBody_Construct` `0x003418f8`, `_ConstructForOwner` `0x00341970`, `_Destroy` `0x00341a00`, `_SetOwner` `0x00341a68`, `_AddShape` `0x00341aa8`, `_GetShapes` `0x00341ae0`, `_Update` `0x00341c10`, `_Moved` `0x00341e40`, `_GetContactScale` `0x00341e18`, `_ComputeBounds` `0x00341638`, `_AddShapeBounds` `0x003417c0` | confirmed (code) |
| [Shapes](#shapes) | pools: `IPhysics_AllocObjectSphere` `0x0033d1a0`, `_AllocObjectBox` `0x0033d1e8`, `_AllocCarBox` `0x0033d230`, `_AllocBigSphere` `0x0033d278`, `_FreeShape` `0x0033d2c8`, `_FindFreeStrikeSphere` `0x003408d8`; base: `PhysicsShape_Construct` / `_Destroy` `0x00342a30` / `0x00342a70`, `PhysicsShape_GetBounds` `0x00341190`; sphere `0x00342aa0` / `0x00342ae0` / pose `0x00342b08`; box `0x00342d18` / `0x00342d98`, `PhysicsBox_SetSize` `0x00342dc0`, pose `0x00342df8`, `_SetMatrix` `0x00343040`, `_GetBounds` `0x003430e8`; capsule `0x003434a8` / `0x003434e0`, size `0x00343508`, pose `0x00343518`; segment `0x003437b0` / `0x003437e8`, pose `0x00343810`; `AABB_FromSphere` `0x00341660` (and `0x00341708`), `AABB_ExtendByMove` `0x00341840`, `AABB_ScaleAboutCentre` `0x003414c0` | confirmed (code) |
| [The human body](#human-body) | `PhysicsHumanBody_Construct` `0x00341e98`, `_Destroy` `0x00341f00`, `PhysicsBody_ApplyHumanScale` `0x00341f28`, `PhysicsHumanBody_SetPushSphere` `0x003420d0`, `_GetShapes` `0x003420c8`, `_OnStruck` `0x00342130`, `_AddStrikeSphere` `0x00342158`, `_ClearStrikeSpheres` `0x00342168`, `_GetContactScale` `0x00342288`, `_Update` `0x003422d0`, `_PushAwayHumans` `0x003425c0`, `_Moved` `0x00342828`, `PhysBody_SetShapeEnabled` `0x003428d0`, `_AddBoneShape` `0x003429e8`, `Human_TestStrikes` `0x0033f110` ([Combat](combat.md#moving-strikes)) | confirmed (code) |
| [Ignore lists](#ignore-lists) | `IPhysics_FindFreeIgnoreSlot` `0x00340890`, `IPhysics_ForgetIgnoredOwner` `0x0033cd70`, `PhysicsBody_IsIgnoring` / `_Ignore` / `_StopIgnoring` `0x00341ae8` / `0x00341b20` / `0x00341b80`, the human body's `0x003421d0` / `0x00342230` / `0x00342260` | confirmed (code) |
| [Queries](#queries) | `IPhysics_QueryBox` `0x0033feb0`, `_QueryBoxFiltered` `0x00340120`, `_CollideShape` `0x0033e7f0`, `_OverlapShape` `0x0033ebb8`, `_RayCastBodies` `0x0033eee0` | confirmed (code) |
| [Sweeping a body](#sweep) | `PhysicsBody_Sweep` `0x0033e278`, `IPhysics_SweepShapeVsMesh` `0x0033d2d8`, `_SweepBodyVsMesh` `0x0033d340`, `PhysicsBody_SweepHumanMask` `0x0033d498` (against the bodies), `PhysicsBody_PushOutOfWalls` `0x003477c0` ([Characters](characters.md#walls)) | confirmed (code) |
| [Contacts](#contacts) | `Contact_InsertSorted` `0x0033d718`, `PhysicsBody_ResolveContacts` `0x0033d9d8`, `PhysicsVec_BackOffPlane` `0x0033d7b8`, `Vec_RemoveNormalPart` `0x0033d870`, `PhysicsVec_Bounce` `0x0033d8f0` | confirmed (code) |
| [Shape pairs](#dispatch) | 14 overlap functions `0x00345d58`-`0x00347148`, 16 sweep functions `0x00343cd8`-`0x00345d38`, 2 ray functions `0x003459f0`, `0x00345b90`, the mesh functions `0x003473b8`, `0x003473c0`, `0x003473c8`, `0x00347c08`, `0x00348530`, `0x00348a50` | confirmed (code) |
| [Mesh tests](#mesh-tests) | `CollisionMesh_BeginShapeQuery` `0x00347170`, `MeshContacts_SortByFraction` `0x00347300`, the working triangle `0x00343a90`, `0x00343b20`, `0x00343c30`, `0x00343c90` | confirmed (code) |
| [Geometry kernels](#kernels) | `0x00349180`-`0x0034ee60` (37 functions) | confirmed (code) |
| [Settling](#settle) | `IPhysics_StartSettle` `0x00340fe0`, `_CancelSettle` `0x003410f0`, `Settle_ComputeTarget` `0x00340d08`, `Settle_NearestAxis` `0x00340b38` | confirmed (code) |

Outside `Physics/`:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00336a00` | `Quat_Slerp(t, out, from, to)` | spherical interpolation, shortest arc | confirmed (code) |
| `0x00335b08` | `Quat_IntegrateAngular(dt, q, ω)` | `q += ½ ω q dt`, normalised | confirmed (code) |
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
| `+0x04` | ptr | head of the human body list ([below](#human-body)) |
| `+0x08` | ptr | head of the body list, sorted by AABB min x |
| `+0x12` | u16 | ticks since the last bound refresh (0-119) |
| `+0x14` | float | the widest body in x, `max(max.x − min.x)` |
| `+0x18` / `+0x7c` | 5 × 5 fns | the shape-pair [overlap and sweep tables](#dispatch) |
| `+0xe0` / `+0xf4` / `+0x108` | 5 fns each | per shape type: sweep against the level mesh, overlap with it, a ray against the shape |
| `+0x120` | 300 × 0x60 | spheres: the humans' five bone spheres each |
| `+0x71a0` | 300 × 0x70 | segments: the humans' five bone segments each |
| `+0xf4e0` | 60 × 12 | the [ignore slots](#ignore-lists) |
| `+0xf7b0` | 60 × 0x60 | spheres for weapon strike spheres (`0x003408d8`), switched off at construction |
| `+0x10e30` | 60 × 0x60 | the humans' capsules, made with (2.0, 0.35) by `0x00343508` |
| `+0x124b0` | 60 × 0xe0 | the humans' bodies: each holds its capsule (`+0x30`) and ten bone shapes |
| `+0x15930` / `+0x22930` / `+0x2c530` | 416 × 0x80 / 0x60 / 0x90 | world objects: bodies, spheres, boxes |
| `+0x3af30` / `+0x42eb0` | 255 × 0x80 / 0x90 | glass panes: bodies, boxes |
| `+0x4be20` / `+0x4c720` | 18 × 0x80 / 36 × 0x90 | cars: bodies, boxes |
| `+0x4db60` | 30 × 0x60 | 2.0 m spheres: the humans' push spheres |
| `+0x4e6a0` | 64 × 0x30 | settle slots (below) |
| `+0x4f2a0`-`+0x4f3f0` | | the punch bag's body set: a human body, one segment (bone 3, radius 0.35, length 1.5) and a capsule |
| `+0x4f450`-`+0x4f45c` | vector | every live body, by index (body `+0x20`) |

The 60 human body sets match the 60 human slots ([AI humans](ai.md)), inferred. Who allocates from which pool is
confirmed (code) by the callers: `Obj_CreatePhysicsBody` (world objects), `GlassPane_CreateBody`, `Car_MakeBodies`. The
ten bone shapes are the strike shapes; their bones, sizes and events are on [Combat](combat.md#moving-strikes).

### Bodies {#bodies}

**Body** (0x80, `PhysicsBody_Construct` `0x003418f8`, vtable `0x00544710`). Confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x00` / `+0x10` | AABB min / max (w = 1) |
| `+0x20` | index in the body vector, −1 when none (`PhysicsNode_Init` `0x00341158`) |
| `+0x24` / `+0x28` | previous / next in the x-sorted list |
| `+0x30` | first shape (a chain through shape `+0x00`) |
| `+0x34` | owner (a task object); free when 0 |
| `+0x38` | ignore list ([below](#ignore-lists)) |
| `+0x3c` | group (s16): two bodies of the same non-zero group never touch |
| `+0x40` | flags: `0x80000000` dirty (re-pose and re-sort on the next update), `0x800` update every time, `0x801` sweep the moved shape against the bodies in the update, `0x8000` no slide on contact; the starting values per kind are below |
| `+0x44` | the owner's type mask, from owner vtable `+0x20` (`0x40` a human); queries filter by it |
| `+0x50` | pending push-out, added to the next sweep's move |
| `+0x60` | scale per axis (1.0; the player's capsule 1.4286, [Characters](characters.md#walls)) |
| `+0x70` | the sweep's "could not move" counter |
| `+0x74` | vtable |

Body vtable: `+0x08` destructor (`0x00341a00`), `+0x10` `Update` (`0x00341c10`), `+0x18` `Moved` (`0x00341e40`),
`+0x20` `AddShape` (`0x00341aa8`), `+0x28` `GetShapes` (`0x00341ae0`), `+0x30` / `+0x38` / `+0x40` the ignore
list, `+0x48` `GetContactScale` (`0x00341e18`: 1.0 for a contact with a body, 0.98 with the mesh).

**World-object bodies** (`Obj_CreatePhysicsBody`, `0x00391d48`), from the type ([`CfgObj`](../references/bindings/config.md#cfgobj)),
confirmed (code): `shape` 1 (`OBB`) is a box with half-extents `size / 2` (`+0x78`, `PhysicsBox_SetSize`
`0x00342dc0`); `shape` 2 (`SPHERE`) a sphere of radius `size.x / 2`; both offset by `centre` (type `+0x1c`,
`Object_GetAttachOffset` `0x003917d8`). Other shapes get no body, except one class (the RTTI check against
`0x00580ec8`) that gets a 0.75 m sphere with flag 4. The flags are then `0x80000500` plus a bit per `PHYFLAG` bit of
`CfgObj` argument 12 (`+0x5e`), the [layers](#layers) below; object kinds `0x1d`, `0x21`, `0x22`, `0xf`, `0x1a` and
`0x1f` add `0x800`, `1`, `2`, `0x100` or the object flag `0x200` (per kind, as listed at `0x00392160`).

#### Collision layers {#layers}

Body flag bits that a mover's mask selects ([Sweeping a body](#sweep)), named by the scripts' `PHYFLAG` table
([enums](../references/enums.md#phyflag)), which `Obj_CreatePhysicsBody` maps one to one (confirmed (code)):

| `PHYFLAG` | Value | Body flag | Who meets a body with it |
| --- | --- | --- | --- |
| `BLOCKOBJECTS` | 1 | `0x2` | a flying world object's sweep |
| `BLOCKHUMANS` | 2 | `0x4` | a walking human's sweep |
| `MELEETARGET` | 4 | `0x10` | strikes |
| `WEAPONTARGET` | 8 | `0x8` | weapon strikes |
| `JUMPTARGET` | 16 | `0x40` | an airborne (or holding) human's sweep |
| `CHARGETARGET` | 32 | `0x20` | a human whose capsule has flag 2 (charging) |
| `RAYCASTTARGET` | 64 | `0x2000` | rays |
| `COMBATPASSTHROUGH` | 128 | `0x10000` | |
| `THROWNWEAPONTARGET` | 256 | `0x20000` | thrown weapons |
| `RUNTARGET` | 512 | `0x40000` | a human above gait 3 (running) |

The "who" column for strikes, rays and thrown weapons is inferred from the names. Confirmed (runtime), the body flags
in `level99`'s street: humans `0x2203f` (`0x2223f`, `0x2343f`), cars `0x2211a`, glass panes `0x7a`, world objects
`0x500` (no layer) up to `0x5057e`. So **a walking human meets other humans** (answered `4`, [Contacts](#contacts))
and the objects with `BLOCKHUMANS`, but **not a car's or a glass pane's body**.

What stops a walker at a parked car is the **collision mesh**. Confirmed (runtime), quick-save slot 6: the player put
1.5 m from a parked car's side and walked into it at an angle (stick 80 % up; hook `human-contact` on
`Human_OnContact`) slid along the car's side 0.42 m from its body's box, and each of the 29 contacts had no object
(`+0x40` = 0) and a mesh triangle with flags `0xf00d` (enabled, type bits 2 and 3, top value 15) and material 0. The
car code has no collision-mesh call, so these triangles are inferred to be part of the level's static mesh at the
car's place; bit 2 is the player-climbable bit ([Collision](collision.md#triangles)), which matches the climb onto a
car ([Characters](characters.md#climb)). The shells themselves are on [Characters](characters.md#car-shells).

The create functions take the first free body of a pool (owner 0), set the owner (`0x00341a68`) and flags, and add it to
the body vector and the sorted list (`IPhysics_AddBody` `0x00340668`); the free functions undo that
(`IPhysics_RemoveBody` `0x00340700`, which first clears the owner from every ignore list) and clear the shape's in-use
bit `0x8`. Flags by kind: human `0x8002243f`, punch bag `0x8000243f`, world object `0x80000500`, car `0x80000000` (then
`0x8002231a` once `Car_MakeBodies` adds `0x2211a`), glass pane `0x8000007a` ([Objects](objects.md)). A human body also
joins the human list (`0x003407a0`; next `+0xc8`, previous `+0xcc`) and gets its scale (`0x00341f28`). The punch bag is
the human class `0x1cc` (`HeavyBag`, [Characters](../references/characters.md)): `0x0021ce08` gives it its own body set
instead of one of the 60.

`PhysicsBody_Update` (`0x00341c10`), when the body is dirty or flag `0x800` is set: pose every shape (shape vtable
`+0x28`), recompute the AABB from the shapes (`0x00341638`, `0x003417c0`), re-sort (`0x00340628`), clear the dirty
bit; with `0x801` set, it also sweeps the first shape from where it was to where it is against the other bodies and
resolves the contacts, so a body moved by its owner still meets what is in the way. The camera builds bodies on its
stack for its sphere tests (`0x00341970`).

### Shapes {#shapes}

A shape is a 16-byte-aligned record with a common head (`PhysicsShape_Construct` `0x00342a30`, vtable `0x00544870`):
`+0x00` next shape of the body, `+0x10` local offset, `+0x20` the previous world position (for sweeps), `+0x30`
type, `+0x31` bone (−1: the body's own frame), `+0x32` flags (`0x1` enabled, `0x2`, `0x4` a target for strikes,
`0x8` allocated, `0x10` reset the previous position on the next pose), `+0x34` vtable (`+0x1c` enable,
`+0x24` world position, `+0x28` pose, `+0x30` reset). The world AABB of any shape is `0x00341190`. Confirmed (code):

| Type | Shape | Size, vtable | Fields | Pose (vtable `+0x28`) |
| --- | --- | --- | --- | --- |
| 1 | oriented box | 0x90, `0x005447f0` | `+0x40` half extents (`PhysicsBox_SetSize` `0x00342dc0` halves the sizes it gets), `+0x50`-`+0x7c` axes, `+0x80` position | the owner's matrix and the local offset (`0x00342df8`); cars set the matrix directly (`0x00343040`) |
| 2 | sphere | 0x60, `0x00544830` | `+0x40` radius, `+0x50` centre | the local offset on its bone (owner vtable `+0xb8`) or in the body's frame (owner vtable `+0xa0`) (`0x00342b08`) |
| 3 | capsule | 0x60, `0x005447b0` | `+0x40` radius, `+0x44` height, `+0x50` base | at the owner's position; for a human the height follows the state (`0x00343518`, [below](#human-body)) |
| 4 | segment | 0x70, `0x00544770` | `+0x40` start, `+0x50` end, `+0x60` radius, `+0x64` length | start on its bone, end = start + the bone's axis × length (`0x00343810`) |

The "capsule" is an **upright cylinder**: every test treats it as the vertical range `[base z, base z + height]`
with a horizontal radius (`0x00346388`, `0x003498c0` + `0x0034a8d0`), so a human never tilts. A segment is a sphere
of its radius swept along a bone: the limbs' strike volumes.

### The human body {#human-body}

0xe0 bytes (`0x00341e98`, vtable `0x005446b0`): a body plus `+0x80` the ten bone shapes (`0x003429e8`), `+0x84`
the push sphere, `+0x90` a broad bound, `+0xc0` one bit per switched-on bone (`PhysBody_SetShapeEnabled`
`0x003428d0`), `+0xc4` the weapon strike spheres (`0x00342158`, `0x00342168`), `+0xc8` / `+0xcc` the human list,
`+0xd0` a weapon-strike flag, `+0xd4` "ignore the level". The bone shapes, their sizes and when they switch on are on
[Combat](combat.md#moving-strikes); `PhysicsBody_ApplyHumanScale` (`0x00341f28`) multiplies their offsets, radii and
lengths and the capsule (height `0x00219890`, radius `0x00219860` of the human) by the human's scale. Confirmed
(code) at the cited addresses:

- **Update** (vtable `+0x10`, `0x003422d0`), unless flag `0x4000`: re-pose the capsule when dirty; with weapon strike
  spheres, pose them on the held object (`0x00226ff0`); while anything strikes, widen the strike reach to 0.8 × the
  scale or the current attack's reach (`AttackTable_GetReach`); pose the enabled bone shapes and the push sphere.
- **Moved** (vtable `+0x18`, `0x00342828`): the broad bound `+0x90` becomes a box of radius 0.8 m around the point
  1.0 m above the position, and the enabled bone shapes' previous positions are reset, so a teleport is not a strike.
- **The capsule's height** (`0x00343518`): base height + 0.2 × scale; the bottom raised by 0.61 × scale in one mode
  (`+0x3be`) and by 0.7 × scale in the states `0x00227f68` selects (at most 0.7 above `+0x560`), except in a long
  fall; 0.35 × scale when cuffed; never under 0.2 × scale.
- **The push sphere** (`0x003420d0`): a 2.0 m sphere from the 30 pool, switched on by set-piece actions (workout,
  uncuff, the action clips through `0x0021c058`). While it is on, `0x003425c0` (from the human's update
  `0x0023d8c8`) overlaps it with the other humans (options `0x2012`, mask `0x40`) and adds to the pending push-out
  (body `+0x50`) of each one that is not its target, has no player number and has no body flag `0x200` a push of
  **0.25 m × w(this) / (w(other) × k(other))** along the line from this human to the other: bystanders are nudged
  out of the animation's way. w is [attribute](#attributes) 1, 82.0 for every human, so it cancels; k is attribute 7,
  the other's push weight (1.0, or 1e9 while it must not move). Confirmed (code) at `0x003425c0`.
- `GetContactScale` (`+0x48`, `0x00342288`): 0.25 while airborne, else 1.0. `+0x50` (`0x00342130`) tells the human
  (`0x00219b08`).

### Attributes {#attributes}

An owner answers numbered float attributes through its vtable `+0xd0` (and sets them through `+0xe0`). Confirmed
(code) for the human (`0x00221260`, setter `0x002212b0`) and the world object (`WorldObject_GetFloatProperty`,
`0x00395e40`); the names are inferred from use:

| # | Human | World object | Read by |
| --- | --- | --- | --- |
| 1 | 82.0 | its type's `+0x62` (`CfgObj` argument 6; 10 or 50 for most types) | the push sphere ([The human body](#human-body)); a weight |
| 7 | `+0x27c`: the push weight, 1.0, set to 1e9 to make the human immovable (grabs, arrests, [Combat](combat.md#code-grabs)) | 1.0 | the push sphere |
| 8 | 0 | its type's `+0x88` (`CfgObj` argument 11, named `mass` in the scripts): 0.1 for 1,359 of the 1,371 types, 0.2 for 7 (the cue ball among them), 0.15 and 0.4 for one each, 0 for 3 | contact code 2's bounce: the restitution |
| others | 0 | 0 | |

A third slot, vtable `+0xd8`, asks whether the owner has attribute `n` at all; the human's answer
(`Human_HasAttribute2`, `0x002212a0`) is yes for attribute 2 only. Confirmed (code); its callers are not traced.

The values per type are confirmed (runtime), read from `level99`'s object database. The bounce
(`PhysicsVec_Bounce`, `0x0033d8f0`) is `v −= n (v·n)(1 + e)` when `v·n < 0`; a brick
that hit the floor at 5.4 m/s came back up at 0.54 m/s ([Settling](#settle)).

### Ignore lists {#ignore-lists}

A body can ignore particular owners: sixty 12-byte slots at IPhysics `+0xf4e0` (`+0x00` the body, `+0x04` the
ignored owner, `+0x08` next), chained from body `+0x38`. `Ignore` (vtable `+0x40`, `0x00341b20`) takes a free slot
(`0x00340890`), `IsIgnoring` (`+0x30`, `0x00341ae8`) walks the chain, `StopIgnoring` (`+0x38`, `0x00341b80`) frees
one slot or, given null, all. When a body is removed, every slot naming its owner is freed (`0x0033cd70`). The human
body adds two rules (`0x003421d0`, `0x00342230`, `0x00342260`): its held object is always ignored, and a null owner
means the level itself (`+0xd4`). With all sixty slots in use, `Ignore` does nothing. Confirmed (code).

### Contacts {#contacts}

A contact is 0xc0 bytes, two 0x40-byte halves for the moving shape and the other one (`0x0033d718`: `+0x00` own
owner, `+0x04` own shape, `+0x40` the other owner or 0 for the level, `+0x44` the other shape), then `+0x50` the
normal, `+0x80` the fraction of the move at the contact (0 to 1), `+0x90` the point, `+0xa0` a slide vector,
`+0xb0` flags (`0x80` a human's landing), `+0xb4` the mesh triangle. A mirrored pair function swaps the halves.
Lists hold at most 50 contacts, kept sorted by fraction. Confirmed (code).

**Resolution** (`PhysicsBody_ResolveContacts`, `0x0033d9d8`) walks the sorted contacts and asks the moving body's
owner what to do (owner vtable `+0xf8`, for example `Human_OnContact` `0x00219d50`); contacts with an owner that
does not want them (`+0xf0`) are skipped. The low 16 bits of the answer:

| Code | What the branch does | Evidence |
| --- | --- | --- |
| 0 | nothing | confirmed (code); summary inferred |
| 1 | moves the body back to the contact (owner position, vtable `+0xa8` / `+0xb0`) and removes from the velocity the part that goes into the surface (`0x0033d870`) | confirmed (code); summary inferred |
| 2 | when the move went more than 0.01 into the surface, moves the body back to 0.01 in front of it; then bounces both velocities (the one in and the one out) off the normal with the owner's restitution (owner vtable `+0xd0` with 8; `0x0033d8f0`) | confirmed (code); summary inferred |
| 3 | the same move back, then zeroes both velocities | confirmed (code); summary inferred |
| 4 | as 1, after handing half of the velocity along the normal to the other body (its body vtable `+0x54`), unless that body has flag `0x200` | confirmed (code); summary inferred |

High bits: `0x10000` go on with the next contact (return 0), `0x40000` abort the move (return −1), otherwise stop
after this contact (return 1); `0x20000` keeps the smallest contact scale (body vtable `+0x48`) as the move's limit.
A contact whose `+0xb0` has bit 2 removes the normal part of its slide vector (`+0xa0`) and stores it, once per move,
in body `+0x50`. Confirmed (code) for the branches; the one-word summaries are inferred. A code above 4 takes none of
the branches.

**Who answers what.** Owner vtable `+0xf8` is set in seven classes of the task family; the vtables were found from
the code that stores them, and each handler was decompiled. Confirmed (code):

| Owner | Handler | Answers |
| --- | --- | --- |
| human (vtable `0x0053f088`) | `Human_OnContact` `0x00219d50` | `0`, `0x20000` or `0x20001` (slide) against the level and objects ([Characters](characters.md#walls)); `1` against a body with type flag `0x200`; against another human, `1` after a strike (`Strike_Contact` `0x0021b290`, its vtable `+0x104`), otherwise **`4`**, with `0x20000` added while airborne |
| world object (`0x005453a0`) | `WorldObject_OnContact` `0x00394050` | **`2`** against a body or a steep surface (normal z ≤ 0.7), `0x20002` on a floor, `0x10003` on a floor once settled ([Settling](#settle)); `0x10000` (after `0x00393e20`) while its byte `+0x10d` or `+0x10e` is clear; `0x40000` when, thrown, it hits a human; `0` or `5` (no branch) when `0x003951d8` gives it a human (its holder, inferred) |
| car (`0x00544c08`) | `Car_OnLanded` `0x00389e08` | always **`2`**; with the level it also clears airborne and zeroes the velocity ([Cars](#cars)) |
| glass pane (`0x00544ed0`), light task (`0x00545138`), particle system (`0x00545660`), scene (`0x005458c8`) ([Task classes](tasks.md#classes)) | `0x004dac20` | `0` |

So **3** is never answered: nothing in the game zeroes both velocities on a contact.

### Settle slot

0x30 bytes, free when `+0x00` is 0:

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
   shrink. The queries read it to know how far back along the sorted list to look ([Queries](#queries)).
2. **Settle turns.** For each of the 64 slots in use: `t += speed`, `speed += 0.02`, clamp `t` to 1; set the object's
   rotation to `slerp(start, end, t)`, keeping its position; if the object has a body, mark it (`+0x40 |=
   0x80000000`) and let the body follow the object (body vtable `+0x14`). At `t` = 1 the object's flags lose
   `0x40000` and gain `0x8000000`, and the slot is freed.

Starting from rest, `t` after *n* ticks is `0.01 × n(n − 1)`, so **a settle takes 11 ticks (183 ms)**, slow at
first and fastest at the end, like something tipping over.

Confirmed (runtime) in a level: a settle slot filled by hand (an object, start = its rotation, end a quarter turn,
acceleration 0.02, speed 0) read `t` = 0.02, 0.12, 0.2, 0.3, 0.42, 0.56, 0.72, 0.9 on successive samples, then 1.0
with the slot freed and the object's flags `0x40000` → `0x8000000`, 0.21 s after the slot was written. A real
landing is in [Settling a landed object](#settle).

### Settling a landed object {#settle}

`WorldObject_OnContact` (`0x00394050`) gets the contact (hit object at `+0x40`, normal at `+0x50`, point at
`+0x90`). Confirmed (code): when no human holds the object (flag `0x10`), it did not break on this contact (both
wear bytes `+0x10d` and `+0x10e` non-zero, [Contacts](#contacts)), the contact is the ground (no hit object and
normal z > 0.7), its type's **`axis`** byte (`+0x85`, [`CfgObj`](../references/bindings/config.md#cfgobj)
argument 10) is not 0, and it is neither settling nor settled (`0x8040000`), it starts a settle
(`IPhysics_StartSettle`, `0x00340fe0`): the first free slot (or the object's own), start = its current rotation,
speed 0, its angular velocity zeroed (vtable `+0x7c` with the zero vector `0x005116c0`, inferred), flag `0x40000`.
With all 64 slots busy nothing happens.

The end rotation (`Settle_ComputeTarget`, `0x00340d08`) turns the object by the smallest angle that lines up one of
its local axes with the contact normal, the turn wrapped to ±90°: it comes to rest on its nearest face. The axis
(`Settle_NearestAxis`, `0x00340b38`) is the one whose positive direction is nearest the normal (the smallest
`acos(axis · n)`, not `|axis · n|`; the ±90° wrap then lets the negative end land on the floor too, so an allowed
axis pointing straight down can lose to one lying flat, which is turned through 90°) **among the axes the
type's `axis` byte allows**: the byte is passed through unchanged (`lbu a3,0x85(v0)` at `0x003946ec`), bit 0 is the
local x axis, bit 1 y, bit 2 z ([AXIS](../references/enums.md#axis): `X` 1, `Y` 2, `XY` 3, `Z` 4, `XZ` 5, `YZ` 6,
`XYZ` 7). Bit 3, in `XZ_ROUND` 13 and `YZ_ROUND` 14, is tested but the axis it picks is never read, so a `_ROUND`
type settles like `XZ` or `YZ`. Confirmed (code). Of the 1,371 object types loaded in `level99`, 855 have `axis` 0
and never settle, 197 are `Z`, 119 `XYZ`, 76 `X`, 54 `XZ` (the brick, the beer bottle), 30 `XY`, 28 `Y`, 4 `YZ`
and 8 `XZ_ROUND` (the bats and the pool cue); confirmed (runtime), read from the object database.

While it settles (`0x40000` only), a floor contact starts nothing new and answers `0x20002`: a bounce with the
type's restitution, then the move's velocity scaled by the mesh contact scale 0.98 (its z only when upward);
a wall or a body answers `2`, the same bounce with no scale: no friction, the tangential velocity is kept.
Confirmed (code) at `0x00394050` and `0x0033e278`, and the 0.98 at runtime (−5.4886 m/s became +0.5379, not
+0.5489). On a later ground contact with `0x8000000` set, the object stops: velocity and angular velocity zeroed, flags
`0x4000000` (airborne) and `0x8000000` cleared, `0x2000000` (grounded) set. Removing an object (`0x00391c10`)
cancels its settle.

Confirmed (runtime), a brick dropped from the player's hand (scenario `physics_brick_settle`: `Human_DropHeld` called
on the player standing in the street of quick-save slot 6, no input; hooks on the object's update, its velocity
setter, the contact handler and the bounce, so every value below is the game's own, per update):

| Ticks after the drop | What happens |
| --- | --- |
| 0 | the drop: velocity set to 0 (`Human_DropHeld` clears `+0x10c`, so the [holder start](#movers) is not used); flags `0x4000000` airborne |
| 1 | first update: `vz` = −0.2613 (dt 1/60: the drop's message 28 stamps the object's last-update time a tick back, `Task_SendMessage28` `0x002296c0`, [Characters](characters.md#code-index)) |
| 3-19, every 2 | `vz −= 0.5226` (dt 1/30, interval 2 ticks), then the sweep moves it |
| 20 | floor contact at −5.49 m/s: `WorldObject_OnContact` starts the settle (axis mask 5) and answers `0x20002`; the bounce (`e` = 0.1, the type's `+0x88`, [Attributes](#attributes)) gives +0.5379 m/s |
| 22-28, every 2 | while settling, `vz` drops by only **0.2613** per update (+0.2766, +0.0153, −0.2459, −0.5072, −0.7685): the update's dt is 1/60, not 1/30 (the step's settle turn re-poses the object every tick, which inferred stamps its time) |
| 31 | the settle ends (11 ticks after it started): flags `0x40000` → `0x8000000` |
| 32 | `vz −= 0.5225` (dt 1/30 again), then the second floor contact finds it settled and answers `0x10003`: back to 0.01 above the floor, velocity 0 (set three times: the handler, the sweep, the object), airborne and settled cleared, grounded set |
| 36 | interval back to 20 ticks |

The brick (`OBB` 0.07 × 0.21 × 0.10 m, centre (0, 0, 0)) came to rest flat on its 0.21 × 0.07 face with its origin
at z 0.2831: the floor (0.2231, where the player stands) + its half-height 0.05 + the 0.01 back-off. Four runs gave
this sequence; one other run met the floor one update earlier at a different contact point, and its velocity was 0
after each bounce (not explained).

### Queries: the sort-and-sweep {#queries}

Every query first gathers candidate bodies from the x-sorted list with a box. Confirmed (code) at `0x0033feb0`
(`IPhysics_QueryBox(phys, out, box, startBody, ignoredOwner, typeMask, flagMask)`) and `0x00340120` (the same with
more filters):

1. With a start body (the querying body itself), walk **backwards** from it while `box.min.x − node.min.x` is at
   most the widest body (IPhysics `+0x14`): further back, nothing can reach the box. Then walk **forwards** from it.
   Without one, walk forwards from the head.
2. Forwards, stop at the first node with `min.x > box.max.x`.
3. Keep a body when its `max.x ≥ box.min.x` and it overlaps the box in y and z, its owner is not the ignored one,
   its type mask (`+0x44`) meets `typeMask` and its flags (`+0x40`) meet `flagMask` (0: any). The filtered form also
   drops bodies with excluded flags, owners whose state flags (owner vtable `+0x50`) meet an excluded mask, and pairs
   a callback refuses.

The answer is the bodies in list order, at most 512. On top of it:

| Query | Address | What it does | Evidence |
| --- | --- | --- | --- |
| `IPhysics_CollideShape(phys, out, query)` | `0x0033e7f0` | sweeps a shape (the body's own, or one given with a transform: option `0x400`) along a move against the candidate bodies (option `2`; the [sweep table](#dispatch)) and the level mesh (option `1`; the type's mesh sweep with the query's two thresholds); keeps up to 50 contacts sorted by fraction and copies the first `max` out. Bodies of the same non-zero group and owners that refuse (owner vtable `+0x110`) are skipped. Used by the follow camera's sphere tests, the throw aim, the airborne push-out and the cars | confirmed (code) |
| `IPhysics_OverlapShape(phys, out, query)` | `0x0033ebb8` | the same without a move, with the overlap table and the mesh overlap (`+0xf4`); option `0x80` skips the same group, `0x100` asks the owner, `0x200` only enabled shapes. Used by the push sphere and the strikes | confirmed (code) |
| `IPhysics_RayCastBodies(length, phys, out, ray)` | `0x0033eee0` | the box around the ray's two ends, then each candidate's shapes with the type's ray function (`+0x108`); keeps the nearest hit's owner, shape and fraction. Used by the AI's line tests (`0x002e7608`, `0x00308c60` and others) | confirmed (code) |

So `+0x14` is the classic sort-and-sweep bound: the step's refresh keeps it from growing for ever
([The step](#step)).

### Sweeping a body {#sweep}

`PhysicsBody_Sweep` (`0x0033e278`, [Characters](characters.md#walls) for the human's use) moves a body by
`v × dt` plus its pending push-out, in up to three passes. A pass, confirmed (code):

1. The body's box, scaled about its centre by body `+0x60` (`0x003414c0`) and grown to cover the move
   (`0x00341840`).
2. **Against the mesh** (`0x0033d340`): the first shape's mesh sweep (`+0xe0` by type, through `0x0033d2d8` with
   the wall threshold −0.65). For a human (type mask `0x40`) in the air, also the landing segment (`0x0023e408`),
   whose hit becomes a contact flagged `0x80`.
3. **Against the bodies** (`PhysicsBody_SweepHumanMask`, `0x0033d498`): the candidates of the swept box whose
   **body flags** (`+0x40`; the type mask is −1) share a bit with a mask that depends on the mover
   ([collision layers](#layers)): 2 `BLOCKOBJECTS` for a non-human; for a human 4 `BLOCKHUMANS`, or `0x44` (and
   `JUMPTARGET`) while airborne or holding (`0x800`), plus `0x20` `CHARGETARGET` when the capsule has flag `0x2`,
   `0x40000` `RUNTARGET` above gait 3, and `0x80000 << player`; same non-zero group skipped; owner asked
   (`+0x110`); each enabled shape of the candidate (body vtable `+0x28`) through the sweep table. Confirmed (code).
4. Contacts are inserted sorted by fraction (`0x0033d718`) and resolved ([Contacts](#contacts)).

### The shape-pair tables {#dispatch}

Two 5 × 5 tables of functions at IPhysics `+0x18` (overlap) and `+0x7c` (sweep), indexed `[moving type][other type]`
(row × 0x14 + column × 4), and three per-type tables against the level mesh and rays. An empty entry means the pair
never touches. Filled by `IPhysics_Construct`, confirmed (code):

| Moving \ other | 1 box | 2 sphere | 3 capsule | 4 segment | Evidence |
| --- | --- | --- | --- | --- | --- |
| **1 box** | overlap `0x00345dd8`; sweep `0x003451e8` | `0x00346680`; `0x003454c0` | `0x00346d18`; `0x00345108` | none; `0x00345590` (never) | confirmed (code) |
| **2 sphere** | `0x00346db8`; `0x003453f8` | `0x00345d58`; `0x003440d0` | `0x00346f90`; `0x00345670` | `0x00346eb0`; `0x00344320` | confirmed (code) |
| **3 capsule** | `0x00346808`; `0x00344618` | `0x00346ed8`; `0x00345910` | `0x00346388`; `0x00343cd8` | `0x00347148`; `0x00345d18` (overlap) | confirmed (code) |
| **4 segment** | none; `0x00345588` (returns 0) | `0x00346de0`; `0x00344538` | `0x00346fb8`; `0x00345d38` (overlap) | `0x00346500`; `0x00344300` (overlap) | confirmed (code) |

| Per type | 1 box | 2 sphere | 3 capsule | 4 segment | Evidence |
| --- | --- | --- | --- | --- | --- |
| mesh sweep `+0xe0` | `0x00348530` | `0x00348a50` | `PhysicsMesh_SweepCapsule` `0x00347c08` (one sphere, [Characters](characters.md#walls)) | none | confirmed (code) |
| mesh overlap `+0xf4` | `0x003473b8` (returns 0) | `0x003473c8` | `0x003473c0` (returns 0) | none | confirmed (code) |
| ray `+0x108` | `0x00345b90` | none | `0x003459f0` | none | confirmed (code) |

Mirrored entries call the other order and swap the contact's halves. A segment's "sweep" against a capsule or a
segment is the static overlap: segments move with their bone and are tested where they are, which is how strikes
work ([Combat](combat.md#moving-strikes)). A segment never touches a box, and no sphere or segment is ray-tested.

### Mesh tests {#mesh-tests}

The mesh functions read the level's [collision mesh](collision.md) through the world manager (`+0x40` level object,
`+4` mesh). Confirmed (code):

- `0x00347170` starts a query: it clears the mesh's per-triangle "visited" bits (`+0x94`, one bit per triangle,
  `+0x84` triangles) so a triangle shared by several grid cells is tested once, and turns the world box into the
  mesh's frame to pick the cells.
- For each enabled triangle of those cells the test fills a working triangle (corners `+0x00`/`+0x10`/`+0x20`,
  edges `0x00343a90`, edge normals `0x00343b20`, unit normal `+0x70` by `0x00343c30`) and turns a two-sided one to
  face the shape (`0x00343c90`).
- Sphere sweep (`0x00348a50`): `Sweep_SphereTriangle` (`0x0034ee60`); box sweep (`0x00348530`):
  `Sweep_BoxTriangle` (`0x0034d5d0`); sphere overlap (`0x003473c8`): `SphereTriangle_Penetration` (`0x0034edd0`).
  Hits become 0xb0-byte mesh contacts, sorted by fraction (`0x00347300`) and copied out.

### Geometry kernels {#kernels}

The pair and mesh functions above call these. Their roles are confirmed (code) by their callers and arguments;
the exact algebra is not written out here (any correct implementation of the same test gives the same contacts).

| Address | Name | Test | Evidence |
| --- | --- | --- | --- |
| `0x00349268` | `Sweep_SphereSphere` | moving sphere against a sphere (also `ObjectRender_Draw`) | confirmed (code) |
| `0x00349488` / `0x00349560` | `Ray_ClipSlab` / `Ray_OrientedBox` | a ray against an oriented box, slab by slab | confirmed (code) |
| `0x003498c0`, `0x0034a8d0` | `Sweep_CircleCircle2D`, `Sweep_IntervalOverlap` | the capsule-capsule sweep: horizontal circles, then the vertical ranges over time | confirmed (code) |
| `0x00349a60`, `0x00349180` | `Sweep_SphereCapsule`, `Vec_MakePerpendicularBasis` | moving sphere (or a ray: radius 0) against an upright capsule | confirmed (code) |
| `0x0034a318`, `0x0034a6d0` | `CapsuleBox_SweepSide`, `CapsuleBox_SweepCorner` | the capsule-box sweep, four sides and four corners | confirmed (code) |
| `0x0034a2d0`, `0x0034a6a0` | `CapsuleBox_CornerCircle`, `CapsuleBox_SideInterval` | the capsule-box overlap | confirmed (code) |
| `0x0034ac08`, `0x0034acc8`, `0x0034ad50` | `Segment_ClosestParam`, `Segment_ClosestPoint`, `Segment_ClosestPoints` | point-segment and segment-segment closest points | confirmed (code) |
| `0x0034af50`, `0x0034afd8` | `Ray_SphereRoot`, `Ray_CircleRoot` | the first root of a ray against a sphere (3D) or a circle (2D) | confirmed (code) |
| `0x0034bbe8` and `0x0034b040`, `0x0034b3b8`, `0x0034b600`, `0x0034b7b0`, `0x0034baf0` | `Sweep_SphereOrientedBox` and its corner, edge, edges, face-edge and frame steps | moving sphere against an oriented box (also the camera's tilt over obstacles) | confirmed (code) |
| `0x0034c430` | `Sweep_BoxBox` | moving oriented box against an oriented box | confirmed (code) |
| `0x0034d5d0`, `0x0034aa60`, `0x0034ab28` | `Sweep_BoxTriangle`, `BoxTri_ProjectBox`, `BoxTri_ProjectTriangle` | moving box against a triangle, axis by axis | confirmed (code) |
| `0x0034ee60` (`0x0034ee40` calls it) | `Sweep_SphereTriangle` | moving sphere against a triangle: the face, then the edges (`0x0034e5d0`, `0x0034e3b8`) | confirmed (code) |
| `0x0034e3b8`, `0x0034e5d0`, `0x0034dff8`, `0x0034d938`, `0x0034e120` | `SphereSweep_Edge`, `_EdgeIfNear`, `_EdgeCylinder`, `Ray_InfiniteCylinder`, `Segment_SphereIntersect` | its edge steps: the end points as spheres, then the edge as a cylinder | confirmed (code) |
| `0x0034e840`, `0x0034edd0` | `PointTriangle_DistanceSq`, `SphereTriangle_Penetration` | the distance of a point to a triangle; a sphere's depth into it | confirmed (code) |

### How things actually move {#movers}

None of this is in the step; it is listed so an implementer knows where each motion lives. Confirmed (code):

- **Humans** sweep their body in `Human_Move` at 30 Hz ([Characters](characters.md#walls)). The physics world gives
  the shape and the sweep; no body pushes another here.
- **World objects** integrate in their update: by default `Task_Integrate` ([Tasks](tasks.md#task-object)),
  explicit Euler with dt = phase time − time of the last update (`0x003a1688`), so dt is the wheel interval.
  Airborne and with a body (`0x00395a10`), velocity first: `vz −= 15.68 × dt`, then `PhysicsBody_Sweep` with that
  velocity, then the rotation by the angular velocity unless settling. On the update after a human lets go (`+0x10c`
  set, holder at `+0x11c`), the object starts at the holder's `+0x5f0` with velocity from the holder's `+0x600` and
  that first sweep uses dt = 1/60 s. `+0x10c` is set only for a throw released from the aiming state
  ([Objects: throws](objects.md#throws)). The two are the thrower's aim, written every frame of the
  aiming state by `Human_TraceThrowAim` (`0x0018fee0`, [Graphics](graphics.md) draws its arc): `+0x5f0` is the
  release point, a fixed offset turned by the human's rotation (`+0x620`) and added to his position (`+0x610`), and
  `+0x600` the throw velocity (`Human_ComputeThrowVelocity`) turned the same way. Confirmed (code).
- While airborne or settling (`0x4040000`, or body flag `0x800`), an object's interval is **2 ticks**
  (`0x00395b70`): it flies at 30 Hz, like the humans.
- **A thrown object hitting a human** is decided in its contact handler, from its own sweep: the hit object is
  tested as a human (`0x00229868`) and handled by `ThrownObject_HitHumanTest` (`0x003928d0`) and
  `ThrownObject_HitHuman` (`0x00392b88`): damage, reaction and wear are in [Objects: throws](objects.md#throws).

### Cars {#cars}

A car's collision is one body (`IPhysics_CreateCarBody`) with two boxes from the car pool, built by `Car_MakeBodies`
(`0x00387d50`) from the type record (`0x0057e4c0 + 0x5f0 × type`, [Cars](cars.md#type-record)): box 1 has its
half-sizes at record `+0x00` and its offset at `+0x10`, box 2 at `+0x20` and `+0x30`. It sets body flags `0x2211a`
and clears the car's object flag `0x200`. Confirmed (code). The car never sweeps its body: it is moved with the car
(pose on the next update) and is what humans, objects and the camera meet. When a car explodes, box 1 loses 0.25 m of
height ([Cars](cars.md#explode)). `Car_OnLanded` (`0x00389e08`) answers 2 to every contact; on the level it also
clears the airborne flag, zeroes the velocity and sends message `0x3f` (kind 5) with the point (0, 1.84, 0.02) in the car's
frame to the task whose handle is at `[0x005971b4] + 0x14`: the shared `sub_car_damage`, which makes a `sub_hood_smoke`
there ([Cars: hit effects](cars.md#hit-effects)).

**Loose parts** (`Car_UpdateLoosePart`, `0x00387f18`, from the car's update for each part flying off, bit in `+0x11f4`)
have no body. Confirmed (code):

1. The part's lifetime (`+0x94`, 360 at the start) counts down once per update; below 91 it sends message `0x15`
   once to the object held in `+0x8c`; at 0 the part is removed (`+0x11f0`, and its linked part) and stops flying.
2. Otherwise, while flying (`+0x96` bit 0): `vz −= 15.68 × dt` (the same gravity as objects), then a box of the
   part's size (part record `+0x10`-`+0x18`, records at type record `+0xf0`, 0x30 bytes each) at the part's matrix is
   swept by `v × dt` (`IPhysics_CollideShape`, option `0x400`).
3. With no contact the part moves by `v × dt`. With one it backs off the plane (`PhysicsVec_BackOffPlane`) and
   bounces with the restitution at part record `+0x20` (`PhysicsVec_Bounce`). On a floor (normal z > 0.7) its spin
   stops; on the first floor contact (`+0x96` bit 1 not yet set, then set) it starts turning to lie flat by the
   smallest angle, eased like a settle but with an acceleration of 0.05 (`+0x88`) from rest (`+0x84`). Each floor
   contact is counted (`+0x97`); the first three, when faster than 2 m/s, play impact sounds at volume
   1 / (count + 1). After any contact the velocity is scaled by 0.95.

The car's update runs every tick while a part flies (`Task_SetUpdateInterval`), so a piece lasts 360 ticks, **6 s**
(inferred from the interval).

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

Coney has the walking body's sweep (`src/human/body.h`, `src/raycast/`) and placed world objects
(`src/world_objects/`), but no `IPhysics`, no flying objects and no step yet. When objects fly, the settle is a
per-object tween run on Coney's fixed step, not a separate pass.

## Open questions

- How an object of a type with `axis` 0 comes to rest: nothing in its contact handler clears airborne, so it would
  keep bouncing at interval 2 (inferred); the throwable ones (`dyn_lawnchair_a`, `_b`) break on their first contact
  (runtime).
