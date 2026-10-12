# Physics

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-06, the whole of `Physics/` on 2026-10-07); the game under PCSX2 2.9.94 (PINE, hooks and `pcsx2 record`, in
`level99`, 2026-10-07). The exact specifications of
[Reimplementing Physics, RayCast and World](#units) were read in the disassembly on 2026-10-12.

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
| `+0x4f2a0`-`+0x4f3f0` | | the punch bag's body set: a human body, one segment (bone 3, radius 0.35, length 1.5, no offset, along the bone's x) and a capsule (radius 0.35, height 1.787 m); [Bodies](#bodies) |
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
| `RUNTARGET` | 512 | `0x40000` | a human above gait 3 (running) strikes it on contact ([Objects](objects.md#trash-props)) |

The "who" column for strikes, rays and thrown weapons is inferred from the names; the street props' values and what
`Human_OnContact` does with `JUMPTARGET`, `CHARGETARGET` and `RUNTARGET` are on [Objects](objects.md#trash-props).
Confirmed (runtime), the body flags
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

**The punch bag's shapes** (`IPhysics_CreatePunchBagBody`, `0x0033cb08`: the capsule on, the bone shapes off), read at
runtime (PCSX2 2.9.94, `level95`, the bag at rest with its origin at height −197.567; confirmed (runtime), the posing
confirmed (code) at `0x00343810` and `0x00343518`):

| Shape | Bone | Offset `+0x10` | Size | Posed |
| --- | --- | --- | --- | --- |
| segment (type 4, inline at body set `+0xe0`, also listed at a pool copy) | 3 | (0, 0, 0) | radius 0.35, length 1.5 | start = bone 3's position, 2.15 m above the origin; end = start + bone 3's **local x axis** × 1.5, which at rest points straight down (end 0.65 m above the origin, 0.05 m off the vertical); so it swings with the bag |
| capsule (type 3) | none | (0, 0, 0) | radius 0.35, height 1.787 m | base on the bag's origin; the height is recomputed each update as for a human (bone 6's height + 0.2 × scale) |

The segment is what strikes meet ([Combat](combat.md#moving-strikes)); the capsule is what sweeps and pushes meet
([Workouts](workouts.md#bag-target)).

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

The "capsule" is **upright**: the capsule-capsule tests treat it as the vertical range `[base z, base z + height]`
with a horizontal radius (`0x00346388`, `0x003498c0` + `0x0034a8d0`), so a human never tilts. Two overlap tests round
its ends instead (capsule against sphere and against segment, a true capsule reaching `r` past both ends), and the
capsule-box overlap reads no height at all ([Unit 4](#u4-physicsoverlap-capsulesphere-00346ed8)); confirmed (code).
A segment is a sphere of its radius swept along a bone: the limbs' strike volumes.

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
(`PhysicsVec_Bounce`, `0x0033d8f0`) is `v −= n (v·n)(1 + e)` when `v·n < 0`, and `v −= n (v·n) e` (an outgoing
normal speed cut to `1 − e` of itself) otherwise ([Unit 3](#u3-bounce)); a brick
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
owner what to do (owner vtable `+0xf8`, for example `Human_OnContact` `0x00219d50`); a contact is skipped when its
triangle has been disabled since it was found, or when its other owner has no physics body (owner vtable `+0xf0`
returns the body). The low 16 bits of the answer (the whole walk, exactly: [Unit 3](#u3-resolve)):

| Code | What the branch does | Evidence |
| --- | --- | --- |
| 0 | nothing | confirmed (code); summary inferred |
| 1 | **advances** the body along the normal only, to 0.01 m short of the touch, when that is more than 0.01 m (owner position, vtable `+0xa8` / `+0xb0`; the part of the move along the surface is not applied), then removes from both velocities (the move's and the real one) the part that goes into the surface (`0x0033d870`) | confirmed (code) |
| 2 | the same advance; then bounces both velocities off the normal with the owner's restitution (owner vtable `+0xd0` with 8; `0x0033d8f0`) | confirmed (code) |
| 3 | the same advance, then zeroes both velocities | confirmed (code) |
| 4 | as 1, after pushing the other body: unless its flag `0x200` is set, `0.5 × (d · n) n × w(mover) / (w(other) × k(other))` is added to its pending push-out (`+0x50`), `d` being this pass's move and `n` the contact normal (pointing at the mover), so the other is moved away by half of the mover's move into it on its own next sweep; then the other body's vtable `+0x54` is told ([Human contacts](#human-contacts)) | confirmed (code) |

High bits: `0x10000` ends the walk and the move goes on (return 0), `0x40000` aborts the move (return −1),
otherwise the walk stops after this contact and the sweep makes another pass (return 1). Code 0 without either bit
goes on to the next contact. `0x20000` with a non-zero code keeps the **largest** contact scale met (body vtable
`+0x48`), applied to the velocity at the end of the move. Contact flag `0x1` (the corner slide of
[Unit 2](#u2-00347c08-physicsmesh-sweepcapsule)) adds the slide vector `+0xa0` to the move's velocity, flag `0x2`
stores it once per move in body `+0x50`; both first drop its part against the move's direction. A code above 4
takes none of the branches. Confirmed (code) at `0x0033d9d8` ([Unit 3](#u3-tail)).

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
`v × dt` plus its pending push-out (divided by the owner's push weight, attribute 7, unless body flag `0x1000`;
cleared on every call), in up to three passes. A pass, confirmed (code):

1. The body's box at the start of the move, grown to cover the move (`0x00341840`). A copy scaled about its centre
   by body `+0x60` (`0x003414c0`) is made and never used: both queries get the unscaled box (`0x0033e518`,
   `0x0033e550`).
2. **Against the mesh** (`0x0033d340`): the first shape's mesh sweep (`+0xe0` by type, through `0x0033d2d8` with
   the wall threshold −0.65). For a human (type mask `0x40`) in the air, also the landing segment (`0x0023e408`),
   whose hit becomes a contact flagged `0x80`. **Faces are one-sided unless flagged.** In the capsule's mesh sweep
   (`PhysicsMesh_SweepCapsule` `0x00347c08`) a triangle is read only for its enabled bit (`0x00347de0`, no type-bit
   mask, so a door's `0x400` or `0x40` changes nothing), then: a wall (`|n.z|` ≤ 0.65, `0x00347e60`); the side test
   `n·c − n·v0 < 1e-6` (`0x00347ec0`), where `c` is the sphere's centre at the **start** of the move (the shape's
   `+0x50`, read at `0x00347c80`, raised by `r + 0.05`), skips a one-sided triangle (flag `0x2` clear, `0x00347edc`)
   and turns a two-sided one to face the sphere (`0x00343c90`, `0x00347ee4`); then the move must go into the face,
   `n·move < −0.001` (`0x00347f2c`). So a one-sided wall stops a body only from its front, and a body that starts
   behind it passes. Confirmed (code). What makes the level's doors block from both sides is the door's init setting
   flag `0x2` ([Objects](objects.md#door-blocking)).
3. **Against the bodies** (`PhysicsBody_SweepHumanMask`, `0x0033d498`): the candidates of the swept box whose
   **body flags** (`+0x40`; the type mask is −1) share a bit with a mask that depends on the mover
   ([collision layers](#layers)): 2 `BLOCKOBJECTS` for a non-human; for a human 4 `BLOCKHUMANS`, or `0x44` (and
   `JUMPTARGET`) while airborne or holding (`0x800`), plus `0x20` `CHARGETARGET` when the capsule has flag `0x2`,
   `0x40000` `RUNTARGET` above gait 3, and `0x80000 << player`; same non-zero group skipped; owner asked
   (`+0x110`); each enabled shape of the candidate (body vtable `+0x28`) through the sweep table. Confirmed (code).
   Between two humans this is one capsule against one capsule: [Human contacts](#human-contacts).
4. Contacts are inserted sorted by fraction (`0x0033d718`) and resolved ([Contacts](#contacts)).

The move is never shortened by a contact's fraction: each pass sweeps the whole slid `V × dt` again from where the last
advance left the body ([Unit 3](#u3-sweep)). After the passes: when three passes each stopped (or a slide left no move),
the body stays where the last advance put it, its horizontal velocity is zeroed and written to the owner (vtable
`+0x74`) whatever the body's flags, and its counter `+0x70` goes up; otherwise it moves by the rest and the counter is
reset. The velocity the slides leave is written back to the owner (vtable `+0x74`) **only when body flag `0x400` is
set** (`0x0033e738`) and a stopping contact asked for it (not a level triangle with flag `0x100`, not two airborne
humans). Among humans only a player's body has `0x400` (world objects' bodies have it too, `0x80000500`):
`Human_CreatePhysicsBody` (`0x0021ce08`) sets it with `+0x60` = `+0x64` = 1.4286 for a brain of type 0, as does
`Human_MakePlayer` (`0x00229c40`); `Human_ReleasePlayer` (`0x0022a2a8`) clears it and puts `+0x60` / `+0x64` back to
1.0. So a slide brakes the player ([Characters](characters.md#walls)) but never an AI, whose control asks for its speed
again each update. Confirmed (code); the AI's unchanged speed is confirmed (runtime) on
[Characters](characters.md#slopes).

#### Human contacts {#human-contacts}

What a walking human meets of another human, confirmed (code) at the cited addresses:

- **The shapes.** The mover sweeps its first shape, and the candidate's chain is walked from its first shape too
  (body vtable `+0x28`; `PhysicsHumanBody_GetShapes` `0x003420c8` returns `+0x30`): for a human that chain is the
  capsule alone, so the bone shapes and the push sphere never block. Capsule against capsule
  (`PhysicsSweep_CapsuleCapsule` `0x00343cd8`) uses each capsule's own radius (shape `+0x40`, 0.35 × the class
  scale, already scaled by `PhysicsBody_ApplyHumanScale`), **without** body `+0x60` or a second scale: centres meet at
  the sum of the radii (about 0.69 m for the player and a man of scale 1), and the two vertical ranges must overlap
  during the move. The level's sphere radius ([Characters](characters.md#walls)) plays no part.
- **Who is skipped.** A candidate without `BLOCKHUMANS` (`0x4`) in its body flags, one sharing the mover's non-zero
  group, and a disabled capsule. Human owners never refuse (owner vtable `+0x110` is `0x004dac30`, which returns 1),
  and the brain is not read, so a scripted or `BrDead` human is met like any other.
- **Groups.** Every link of two (or three) humans takes a fresh number from IPhysics `+0x10` (counted 1 to `0xfffe`)
  and writes it to both bodies' `+0x3c`, so linked humans pass through each other: `Grab_Link` (`0x0022c760`),
  `Pair_LinkMount` (`0x0022c000`), the tackle (`HitReact_WhileTackling`), `Grab_Escape`, `Revive_Start`,
  `Uncuff_BeginMash`, `Pair_AlignForMove` and `Tandem_AlignThree`; each also sets both push weights (attribute 7) to
  1e9. The matching ends write 0 (`Grab_End`, `Tackle_End`, `Tandem_Unlink`, `Throw_ClearLink`, `Revive_Finish`, the
  uncuff end hooks), as does `Human_StartFall` (`0x0023dc58`). A scene sets one group on the humans it plays
  (`SceneTask_Start` `0x0039d870`, cleared by `SceneTask_End`). The ignore lists ([below](#ignore-lists)) serve
  strikes, not the walk.
- **Who loses `BLOCKHUMANS`.** Body flag `0x4` is cleared (with the re-pose flag `0x80000000` set) by
  `Human_KnockOut` (`0x00230328`), `Human_StartWounded` (`0x0022fa78`), `Human_OnHealthOut` (`0x002674c0`, again
  after either) and `Human_Arrest` (`0x0022ec18`, the cuffs), so walkers pass over a human knocked out, wounded or
  cuffed; a dead one (state `0x100000000`) has his body freed unless airborne. Confirmed (code) at the cited
  addresses; no other `& ~4` in the executable reaches a human's body flags (searched by the mask's `li -5`).
- **A knockdown keeps it.** `Human_KnockDown` (`0x0022f100`, state `0x80000`, the heavy reactions) writes no body
  flag: it sets the push weight to 1e9 (attribute 7), so a grounded human **still blocks** walkers, who slide along
  him without moving him, from the fall until he stands. Getting up (`Human_UpdateDown` `0x00256a60` →
  `Human_PlayRise` `0x0025e2d8`, clip 199 with `Human_EndDownState` `0x0025e250` as its end callback →
  `Human_GetUp` `0x0022f3c8`) puts the weight back to 1 and touches no body flag either. Confirmed (code).
- **What gives it back.** Two things only, confirmed (code):
    - **A new body.** `Human_CreatePhysicsBody` (`0x0021ce08`) frees the body and takes a fresh one from
      `IPhysics_CreateHumanBody` (`0x0033cc48`), whose flags are `0x8002243f` (`BLOCKHUMANS` included; group 0, so no
      link survives). `Human_Heal` (`0x0022eb40`) calls it after waking (`Human_WakeUp`) and ending a wound
      (`Human_EndWounded`), so the human blocks again **from the moment he is healed**: a revive at its end
      (`Revive_Finish` `0x00274ee0`, not `Revive_Start`), a flash (`Flash_Use` `0x002842e4`), the uncuffing
      (`Uncuff_MashSuccess` `0x00260910`, `Uncuff_WithKey` `0x00261264`), `HuRevive` (`Human_Revive` `0x002377f8`),
      and the level's own heals (`Gm_Level_Update` `0x00159004`, `ANGameOver_Toggle`). `Human_AttachInstance` also
      makes one when the human is set up.
    - **`Human_Unarrest`** (`0x0022ef58`) sets body flags `0x80000004`: the uncuff paths, `HuSetArrested(false)`
      (`0x0023774c`) and `Human_EndDownState` when a cuffed human's rise clip ends.
- **What does not.** `Human_WakeUp` (`0x00230598`) and `Human_EndWounded` (`0x0022fc00`) only re-enable a shape
  and put the push weight back. So a knocked-out human woken without a heal (`HuSetConscious(true)`,
  `Human_SetConscious` `0x00237778`, or `Human_EndDownState`'s knocked-out branch) and a wound ended by
  `HuSetWounded(false)` or `Human_ResetForMode` stand up **without** `BLOCKHUMANS`, and walkers keep passing through
  them until something heals or unarrests them. Confirmed (code).
- **Seen at runtime** (confirmed (runtime), PCSX2 2.9.94, a copy of slot 6, 2026-10-11, scenario
  `repo:research/traces/scenarios/blockhumans_wake.toml`). PoizoCiv, standing, his body flags `0x2223f`, was put
  through six direct calls on the game's thread 40 updates apart, his body pointer and body `+0x40` read every
  update:

    | Call | Body flags after it | `BLOCKHUMANS` |
    | --- | --- | --- |
    | `Human_KnockOut(him, 0)` | `0x2203b` | off |
    | `Human_WakeUp(him, 0)` (where `HuSetConscious(true)` and a knocked-out rise end) | `0x2203b`, unchanged | still off |
    | `Human_Heal(him, 0)` | `0x2203f` | **on** |
    | `Human_StartWounded(him)` | `0x2203b` | off |
    | `Human_EndWounded(him)` (where `HuSetWounded(false)` ends) | `0x2203b`, unchanged | still off |
    | `Human_Heal(him, 0)` | `0x2203f` | **on** |

  The heal's new body took the same pool slot (the pointer did not change). So waking and ending a wound leave him
  passable, and only the heal gives the flag back, as the code says. The knock-out also took bit `0x200` (not
  pushable) away, which `Human_KnockOut`'s own writes do not: what cleared it in that update is not traced, and the
  heal's new body did not have it either (`Human_CreatePhysicsBody` sets it only when `+0x3bf` is 0; that byte was
  not read).
- **The answer** (`Human_OnContact` `0x00219d50`): a charging capsule (shape flag `0x2`) or an airborne mover not yet
  ignoring the other strikes him (`Strike_Contact`, then ignores him) and slides (1); otherwise **4**, `0x20004` in
  the air. So walking into a human slides along him and pushes him by half of the move into him (code 4 above),
  times 1 / his push weight `k` (1, or 1e9 while held: nothing). Body flag `0x200` (not pushable) is set by
  `Human_SetPushable` (`0x0021d848`, `HuSetPushable(false)`; also when a body is made for a human whose `+0x3bf` is
  0, `Human_CreatePhysicsBody`), and by the punch bag; such a human is slid along but never moved.
- **The pushed human is told** (body vtable `+0x54`, `PhysicsHumanBody_OnStruck` `0x00342130` →
  `Human_OnPushContact` `0x00219b08`): only when the pusher is a player and walks into him nearly head-on (the move's
  direction · the normal < −0.8). A cop, a civilian, or a human of the pusher's side (friendly, and the pusher at a jog
  or faster) starts a timer at `+0x56c` / `+0x570` (10 s, or 2.1 s for a friend); a second push after 2 s deals
  `Human_DealDamage(him, 0, pusher, 0, hostile 1, 0)` (unfriendly: no damage, the hostile flag set; inferred: he
  takes it as an attack by the player, not checked at runtime) or asks `Human_CanPush` (friendly) and clears it.
  `Human_StateUpdate` clears the timer once the game time passes `+0x56c` (`0x002405e0`), so the second push must
  come before the deadline: 2-10 s after the first for a stranger, 2.0-2.1 s for a friend. A bum under `BumLogic`
  ignores it.
  `Human_CanPush` (`0x002320a0`) refuses when either is in a fight stance, holds any of the held flags `0x2fefefff` or
  any state of `0x7bf9e9f7ff0`; otherwise it **swaps the two places**: each human's transform is set to the other's
  position with its own rotation (vtable `+0x6c`), so a player who keeps walking into a free teammate at a jog or
  faster for 2 s ends up on the other side of him ([Combat: teammates](combat.md#teammates)). Confirmed (code) at
  `0x00219b08`; the swap was seen at runtime with the timer aged by hand (Combat: teammates).

The mover's own velocity is cut by the slide only when it is a player ([Sweeping a body](#sweep)).

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
Strikes go through the **sweep** table (`Human_TestStrikes` `0x0033f110` sweeps each strike shape from its last pose
to its current one), so of a human's strike shapes only the spheres (hands, head, feet, a weapon's spheres) can
reach a car's or an object's box; the forearm, shin and spine segments never do. Confirmed (code)
([Who reaches a car](#who-reaches-a-car), [Unit 5](#u4b-how-human-teststrikes-builds-the-sweep-0)).

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
| `0x0034ee60` (`0x0034ee40` calls it) | `Sweep_SphereTriangle` | moving sphere against a triangle: the face, then an edge (`0x0034e3b8`) or a corner's region (`0x0034e5d0`); what it does with a sphere that already overlaps is on [Characters](characters.md#slopes) | confirmed (code) |
| `0x0034e3b8`, `0x0034e5d0`, `0x0034dff8`, `0x0034d938`, `0x0034e120` | `SphereSweep_Edge`, `SphereSweep_VertexRegion`, `SphereSweep_EdgeCylinder`, `Ray_InfiniteCylinder`, `Segment_SphereIntersect` | its edge steps: an edge already within the radius (fraction 0), else the edge as a cylinder; past a corner, one of the corner's two edges | confirmed (code) |
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
(pose on the next update) and is what objects and the camera meet. A walking human does not meet it: his layers
leave the car's body out, and he meets and climbs the car's shell in the level's mesh ([layers](#layers),
[Characters](characters.md#car-shells)). When a car explodes, box 1 loses 0.25 m of height ([Cars](cars.md#explode)).
`Car_OnLanded` (`0x00389e08`) answers 2 to every contact; on the level it also
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

## Reimplementing Physics, RayCast and World {#units}

Every function of `Physics/` (`0x0033c288`-`0x0034f740`) and every function of `RayCast/` and `World/` that Coney
does not cite yet, split into units an implementer can take one at a time, bottom-up. Each unit lists its functions
(names and sizes from the shared Ghidra project; "tagged" means `docs/progress/functions.toml` already lists it), what
specifies it, where it goes in Coney and what its tests check. Units 1 to 5 are **specified exactly** below: arguments,
record offsets, constants, branches, the original's quirks and worked test numbers, all confirmed (code) unless a line
says otherwise (read in the disassembly, 2026-10-12; no runtime claims in them). Units 6 to 19 point at the sections
above and say what is still missing before they can be written.

When this plan was made (2026-10-12, `main` at `f325e018`), the tracker had 17 of `Physics/`'s 183 functions, 12 of
`RayCast/`'s 15 and 33 of `World/`'s 61. `RayCast/`'s mesh code (`src/raycast/collision_mesh.*`) is complete apart
from Unit 16; `World/`'s streaming and drawing are done, and its open functions are lifecycle (Units 17 to 19).

**What Coney has instead** (the stand-ins these units replace):

- The walking body's walls are a push-out at the end of the move (`human::nearestWallPush`,
  `slideAlongTouchedFaces`, `edgeSlide`; [Coney's implementation](#coneys-implementation)), so the side of a wall is
  decided after the move and a body fast enough to cross a wall in one update can pass it. Units 1 to 3 are the
  original's swept test and pass loop.
- Humans meet humans before walls in a pass (a marked Coney choice); Unit 11 sorts both together.
- Strikes test only the square's aimed object attack against a car (issue #72); Unit 5 and the strike loop
  ([How Human_TestStrikes builds the sweep](#u4b-how-human-teststrikes-builds-the-sweep-0)) let any punch or kick
  whose sphere reaches a box hit it.
- A falling object's move is a ray from its centre (`world_objects::LooseObjects`); Unit 13 is its box sweep.

**Order and dependencies.** Units 1, 4, 5, 6, 16 and 17 depend on nothing else here. Then:

| Unit | Needs | Unblocks in Coney |
| --- | --- | --- |
| [1](#unit-1) Triangle sphere-sweep kernels | | exact edge and corner contacts for every sphere against the level |
| [2](#unit-2) A shape against the collision mesh | 1 | the walking sweep's contacts, the corner slide, the airborne push-out |
| [3](#unit-3) Moving a body | 2 | the pass loop, contact codes, slides, bounces, push-outs, the "could not move" counter |
| [4](#unit-4) Static shape-pair overlaps | | pick-ups, the push sphere, objects touched by humans |
| [5](#unit-5) A sphere swept against a box | | strikes on cars (issue #72, step 1) and objects' boxes (issue #300) |
| [6](#unit-6) Shapes | | posed shapes and bounds for 7 to 14 |
| [7](#unit-7) Bodies and the sort-and-sweep world | 6 | the candidate queries every test starts from |
| [8](#unit-8) Ignore lists and shape pools | 7 | |
| [9](#unit-9) Creating and freeing bodies | 7, 8 | one body per human, object, car and pane, with the original's flags |
| [10](#unit-10) The human body set | 6 to 9 | strike shapes on bones, the push sphere |
| [11](#unit-11) Moving shapes against bodies | 1, 6, 7 | humans and walls in one sorted pass |
| [12](#unit-12) A capsule swept against a box | 6 | walking into an object's box |
| [13](#unit-13) Box against box and triangle | 2, 6 | falling and thrown objects' boxes, cars' loose parts |
| [14](#unit-14) Rays against bodies | 6, 7 | the AI's line tests against bodies |
| [15](#unit-15) The physics world | 3, 7, 9 | one `IPhysics` per level, the step and the 64 settle slots |
| [16](#unit-16) RayCast leftovers | | material names, the world manager's ray |
| [17](#unit-17) Level file and level object | | |
| [18](#unit-18) World manager lifecycle | 17 | |
| [19](#unit-19) Streamed world object lifecycle | 18 | |

A unit is done when its functions are tagged in Coney (`@orig`, `functions.toml`), its tests pass on synthetic
geometry, and the stand-in it replaces is gone or marked.

**Floating point.** The PS2's FPU and VU0 have no infinities or NaNs: a division by zero gives ±`FLT_MAX`, and
rounding is toward zero (inferred: a hardware fact, not read in these functions). An IEEE implementation matches to
about one unit in the last place, so tests compare fractions with a tolerance (1e-5 is ample), and the few edge
cases that divide by zero say what the original then does.

### Unit 1: Triangle sphere-sweep kernels {#unit-1}

**What it is.** A moving sphere against one triangle: the face, then an edge (as a capsule) or a corner's region;
the point-to-triangle distance; and the working triangle every mesh test fills. Answers issue #150's three questions
([Issue #150](#issue-150)).

- **Coney:** one shared kernel for every sphere against the level. Coney's walking body cites parts of it
  (`src/human/body.cpp`); a branch not yet merged when this was written adds a sphere sweep for the cameras
  (`raycast::sweepSphere`) that follows the same reading. Both should end on this one.
- **Tests:** [Test ideas](#u1-tests), including the late and missed corner contacts the original produces.

13 functions, 8116 bytes (3 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00343a90` | `CollTri_ComputeEdges` | 140 |
| `0x00343b20` | `CollTri_ComputeEdgeNormals` | 272 |
| `0x00343c30` | `CollTri_ComputeNormal` | 92 |
| `0x00343c90` | `CollTri_Flip` | 72 |
| `0x0034ee60` | `Sweep_SphereTriangle` (tagged) | 2144 |
| `0x0034ee40` | `Sweep_SphereTriangleThunk` | 32 |
| `0x0034e3b8` | `SphereSweep_Edge` (tagged) | 536 |
| `0x0034e5d0` | `SphereSweep_VertexRegion` (tagged) | 612 |
| `0x0034dff8` | `SphereSweep_EdgeCylinder` | 296 |
| `0x0034d938` | `Ray_InfiniteCylinder` | 1728 |
| `0x0034e120` | `Segment_SphereIntersect` | 664 |
| `0x0034e840` | `PointTriangle_DistanceSq` | 1420 |
| `0x0034edd0` | `SphereTriangle_Penetration` | 108 |

Conventions: `a·b` is a dot product over x, y, z, summed as `(ax·bx + ay·by) + az·bz` (the VU0 `vmul` +
`vaddy` + `vaddz` order). `a × b` is a cross product. Vectors are 16-byte `float[4]`, and `w` is noted only where
code sets it. "Sphere" arguments point to `{centre: vec4 at +0x00, radius: float at +0x10}`. Arguments follow the
EE ABI: `a0`-`a3`, then `t0`-`t3` for the fifth to eighth integer or pointer arguments, and `f12`... for floats.

**Floating point on the EE** (inferred: a hardware fact, not read in these functions). The EE FPU and VU0 have no
infinities, NaNs or denormals. A division by zero gives ±`FLT_MAX` (the sign is the XOR of the operands' signs),
and an overflow clamps to ±`FLT_MAX`. Rounding is toward zero, not to nearest. So an IEEE implementation matches to
about 1 ulp, not bit for bit: compare fractions with a tolerance (1e-5 is ample at these magnitudes). Where an edge
case depends on division by zero, its section says what the EE does.

#### The working triangle {#u1-working-triangle}

The kernels read a working triangle filled by the mesh function under test. [Unit
2](#u2-the-working-triangle-t-d0-bytes-on-the-c) gives the caller's whole stack
record (0xd0 bytes, with a normal copy at `+0x90` and the result record at `+0xc0` in the capsule sweep; the sphere
sweep keeps its result record right after `+0x90`). What the kernels use, confirmed (code) at the cited addresses:

| Offset | Content | Written by |
| --- | --- | --- |
| `+0x00`, `+0x10`, `+0x20` | corners `v0`, `v1`, `v2` (whole 16-byte vertices) | the caller (`0x00348c8c`-`0x00348cc4` in the sphere sweep) |
| `+0x30` | `e0 = v1 − v0`, `w` = 1.0 | `CollTri_ComputeEdges` `0x00343a90` |
| `+0x40` | `e1 = v2 − v1`, `w` = 1.0 | `0x00343a90` |
| `+0x50` | `e2 = v0 − v2`, `w` = 1.0 | `0x00343a90` |
| `+0x60`, `+0x64`, `+0x68` | `len(e0)`, `len(e1)`, `len(e2)`, only when the edges were normalised | `0x00343b20` |
| `+0x6c` | unused by these functions | |
| `+0x70` | unit normal `n`, `w` = 1.0 | `CollTri_ComputeNormal` `0x00343c30` |
| `+0x80` | plane constant `d = v0·n` (scalar, `x` lane) | the caller after the edges (`0x00348e24`-`0x00348e64`) |

So edge `k` runs from corner `k` to corner `k + 1` (mod 3). Its **outward in-plane normal** is `m_k = e_k × n`,
which is not normalised (for counter-clockwise corners seen from the front, `m_k` points away from the triangle).
Confirmed (code) at `0x0034eecc`-`0x0034ef1c`.

#### CollTri_ComputeEdges {#u1-compute-edges}

`0x00343a90`, 0x8c bytes. `a0` = working triangle, `a1` = normalise flag. It writes `e0`, `e1`, `e2` as in the
table above, each with `w` = 1.0. When `a1` is non-zero it then calls `0x00343b20`. Both mesh sweeps that use the
sphere kernel pass `a1` = 0 (sphere `0x00348e1c`, capsule `0x003480f0`), so **the kernels see unnormalised edges**:
the edge step sweeps the real edge, from `v_k` to `v_k + e_k`. Confirmed (code) at `0x00343a90`.

#### CollTri_ComputeEdgeNormals {#u1-edge-normals}

`0x00343b20`, 0x110 bytes. `a0` = working triangle. **The name is wrong**: it divides each edge by its length
(VU0 `vsqrt` of `e·e`, then FPU `1/len` and a VU0 scale of x, y, z, leaving `w`), writes the unit edges back to
`+0x30`/`+0x40`/`+0x50` and the lengths to `+0x60`/`+0x64`/`+0x68`. A zero-length edge is not guarded against (on
the EE, `1/0` gives `FLT_MAX` and `0 × FLT_MAX` = 0, so the edge stays zero). It is not used on the sphere path.
Confirmed (code) at `0x00343b20`. The Ghidra name is kept, and its plate comment now says what the function does.

#### CollTri_ComputeNormal {#u1-compute-normal}

`0x00343c30`, 0x5c bytes. `a0` = working triangle (a second argument some callers pass is ignored).
`n = c / sqrt(c·c)` with `c = (v1 − v0) × (v2 − v0)`, computed as VU0 `vrsqrt` then a scale. `w` is set to 1.0,
and the result goes to `+0x70`. Confirmed (code) at `0x00343c30`. For a degenerate triangle (`c` = 0), the EE's
`rsqrt(0)` gives `FLT_MAX` and `n` comes out as (0, 0, 0) (inferred, EE semantics). An IEEE build would get NaN, so
an implementation should make `n` = 0 explicitly. The mesh sweeps then skip such a triangle at the side test
(`n·c − n·v0` = 0 < 1e-6, then `n·move` = 0 ≥ −0.001), so it never reaches the kernel.

#### CollTri_Flip {#u1-flip}

`0x00343c90`, 0x48 bytes. `a0` = working triangle. It swaps corners `+0x10` and `+0x20` (whole 16 bytes) and
multiplies the normal's x, y, z by −1.0 (`0xbf800000`). **`w` is kept** (the VU op is `.xyz`). It does **not** recompute
the edges or `d`: the callers flip
before `CollTri_ComputeEdges` and before setting `+0x80` (sphere sweep: flip `0x00348d7c`, edges `0x00348e18`, `d`
`0x00348e64`). Confirmed (code) at `0x00343c90`.

#### Sweep_SphereTriangle {#u1-sweep-sphere-triangle}

`0x0034ee60`, 0x860 bytes. The thunk `Sweep_SphereTriangleThunk` (`0x0034ee40`, 0x20 bytes) calls it with `t0`
pointing at a zeroed scratch word and drops that word. Its only caller is the sphere mesh sweep `0x00348a50`. The
capsule mesh sweep `0x00347c08` calls `0x0034ee60` directly and keeps the word. Confirmed (code).

**Arguments.** `a0` = result record. `a1` = sphere (centre `c`, radius `r`), the centre at the **start** of the
move. `a2` = working triangle (with `+0x30`-`+0x80` filled). `a3` = move `M` (a vec4, the whole displacement, not a
direction). `t0` = `int*` for the **inside mask**. **Return** `v0`: 1 when it recorded a fraction, 0 otherwise.

**Result record.** `+0x00` is an `int` count (the caller zeroes it). On a hit the kernel writes the fraction to
`+0x04 + 4·count`, the vector (0, 0, 0, 1) (`0x005116c0`) to `+0x10 + 16·count`, and adds 1 to the count. So
**only a fraction comes back**: no normal, point or depth. The callers read `+0x04` only and make one call per
record. Confirmed (code) at `0x0034f688`-`0x0034f6a8`.

**The fraction is not range-checked here.** The kernel can return 1 with a fraction above 1 or below 0, which only
the edge steps produce ([SphereSweep_Edge](#u1-edge)). Both callers drop a fraction outside [0, 1] (sphere sweep
`0x00348e78`/`0x00348e94`, capsule sweep `0x0034814c`/`0x00348168`). Confirmed (code).

**Algorithm**, confirmed (code) at `0x0034ee60`:

1. `nn = n·n` (1, or 0 for a degenerate triangle) and `dist = n·c − d`.
2. **Already reaching the plane** if `dist·dist ≤ (nn·r)·r` (inclusive). Then the test point is `P = c` and the
   base fraction is 0.
3. Otherwise **swept to the plane**:
    - Centre in front (`d < n·c`, strict): no hit (return 0) unless `n·M < 0`. The sphere's leading point is
      `P0 = c + (−r)·n`.
    - Centre behind (else): no hit unless `n·M > 0`, and `P0 = c + r·n`. (The mesh callers never pass a sphere
      behind a face: they flip two-sided faces and skip one-sided ones. The kernel itself handles both sides.)
    - `t = (d − n·P0) / (n·M)`. If `t > 1`, no hit (return 0). There is no `t < 0` test: with the sphere off the
      plane, `t` is positive.
    - The test point is `P = P0 + t·M` (where the sphere first touches the plane).
4. **The inside mask**: bit `k` (1, 2, 4 for edges 0, 1, 2) is set when `m_k·P ≤ m_k·v_k` (inside or on edge `k`).
   The mask is written to `*t0` in both branches, before the dispatch (`0x0034efd4`, `0x0034f43c`). It is not
   written on the early returns of step 3. The capsule sweep stores it in its contact at `+0x14` (`0x003481d8`).
5. **Dispatch on the mask** (jump tables `0x00577450`, already reaching, and `0x00577470`, swept). The two tables
   differ only in case 7:

    | Mask | Centre is | Step | Arguments |
    | --- | --- | --- | --- |
    | 7 | inside all edges | **hit**: fraction 0 (already reaching) or `t` (swept) | none |
    | 6 | outside edge 0 only | [SphereSweep_Edge](#u1-edge) | `A = v0`, `E = e0` |
    | 5 | outside edge 1 only | SphereSweep_Edge | `A = v1`, `E = e1` |
    | 3 | outside edge 2 only | SphereSweep_Edge | `A = v2`, `E = e2` |
    | 2 | outside edges 0 and 2 (past `v0`) | [SphereSweep_VertexRegion](#u1-vertex-region) | `k = 0`, `m0`, `e0` |
    | 4 | outside edges 0 and 1 (past `v1`) | SphereSweep_VertexRegion | `k = 1`, `m1`, `e1` |
    | 1 | outside edges 1 and 2 (past `v2`) | SphereSweep_VertexRegion | `k = 2`, `m2`, `e2` |
    | 0 | outside all (only a degenerate triangle) | none: return 0 | |

   The edge and corner steps get the **original** sphere (centre `c`, radius `r`) and the **whole** move `M`, not
   `P` or `t`. Their fraction is that of the whole move, and it is what the kernel records. Confirmed (code) at
   `0x0034eff4`-`0x0034f220` and `0x0034f45c`-`0x0034f684`.

Edge cases, confirmed (code) unless marked:

- **Zero move.** If the sphere is not already reaching the plane, `n·M` = 0 and there is no hit. If it is already
  reaching, case 7 still gives fraction 0, and so does an edge whose segment already meets the ball. The capsule
  step returns nothing for a zero move ([Ray_InfiniteCylinder](#u1-capsule)).
- **Already overlapping and moving away.** Case 7 gives fraction 0 whatever the direction of `M`, and so does an
  edge within `r`. Only the corner step looks at the direction. The mesh callers have already dropped moves that do
  not go into the face ([Unit 2](#u2-00347c08-physicsmesh-sweepcapsule)).
- **Exactly on the plane** (`dist` = 0): already reaching.
- **On an edge line** (`m_k·P = m_k·v_k`): counts as inside that edge.
- **Degenerate triangle** (`n` = 0): `0 ≤ 0`, so it is "already reaching" and all `m_k` = 0, which gives mask 7 and
  a hit at fraction 0 for any sphere. The callers' side test keeps such triangles out (above), and an
  implementation of the kernel alone should return 0 for `n` = 0 (inferred: unreachable in the original).

#### SphereSweep_Edge {#u1-edge}

`0x0034e3b8`, 0x218 bytes. **Arguments**: `a0` = edge start `A`, `a1` = edge vector `E` (`A + E` is the other
corner), `a2` = sphere (centre `c`), `a3` = move `M`, `f12` = `r`, `t0` = `float*` fraction out, `t1` = `int*` count
out, `t2` = `vec4*` point(s) out (room for two). **Return** 1 for a contact, 0 for none. Confirmed (code) at
`0x0034e3b8`.

1. If the segment already meets the ball ([Segment_SphereIntersect](#u1-segment-sphere)`(A, E, c, r)` returns 1),
   then `*fraction = 0` and the function returns 1. The count and points are what that call wrote.
2. Otherwise run the capsule step ([SphereSweep_EdgeCylinder](#u1-edge-cylinder)`(r, c, M, A, E)`, with its roots
   in a local `roots[2]`). If it returns 0, return 0 and leave `*fraction` unwritten.
3. Otherwise `*fraction = roots[0]`: **the first root in the capsule's list, not the smallest, and not necessarily
   in [0, 1]** (see the capsule's root order). The contact point is the centre at that fraction, `Pc = c + roots[0]·M`,
   clamped to the segment by projection. If `E·Pc < E·A` it is `A`. Else if `E·(A+E) < E·Pc` it is `A + E` (the
   code forms `E·(A+E)` as `E·A + E·E`). Else it is `A + u·E` with `u = (E·Pc − E·A) / (E·(A+E) − E·A)`. The point
   goes to `*t2`, and `*t1` = 1. `Sweep_SphereTriangle` ignores the point and the count.

#### SphereSweep_VertexRegion {#u1-vertex-region}

`0x0034e5d0`, 0x264 bytes. Its Ghidra name is `SphereSweep_EdgeIfNear` (another analyst's, kept);
this page calls it `SphereSweep_VertexRegion`. **Arguments**: `a0` = corner index `k` (0-2), `a1` =
working triangle (corners read at `+0x10·i`), `a2` = `&n` (unused), `a3` = `&m_k`, `t0` = `&e_k`, `t1` = sphere,
`t2` = move `M`, `t3` = `float*` fraction out, `f12` = `r`, `f13` = 1.0 (unused). The caller's stack words `+0` and
`+8` are the count and point outputs, handed on to the edge step. **Return** 1 or 0. Confirmed (code) at
`0x0034e5d0`.

1. `V = v_k`. If `|V − c|² < r²` (strict): `*fraction = 0` but **return 0**, so there is no contact when the corner
   is already inside the ball.
2. If `M·(V − c) ≤ 0` (moving away from or across the corner), return 0.
3. **Which edge.** `u = (m_k·V − m_k·c) / (m_k·M)`, the fraction at which the centre's path crosses edge `k`'s side
   plane (the plane through edge `k` that holds `n`). `X = c + u·M`. If `e_k·V ≤ e_k·X` (the crossing lies along
   edge `k`, past the corner), test **edge `k`**: `A = v_k`, `E = v_(k+1 mod 3) − v_k`. Otherwise test **edge
   `k − 1`**: `A = v_(k−1 mod 3)`, `E = v_k − A`. These are built from the corners, not read from `+0x30`.
4. Return [SphereSweep_Edge](#u1-edge)`(A, E, sphere, M, r, fraction, count, point)`. **Only one** of the corner's
   two edges is ever tested, and the corner itself is that edge's end cap.

**Edge case**, `m_k·M = 0` (the move is parallel to edge `k`'s side plane): on the EE, `u` is ±`FLT_MAX` and `X`
clamps, which decides the comparison by signs (inferred, EE semantics; not reached in any test here). For a
faithful IEEE version, use edge `k` when the numerator `m_k·V − m_k·c` and `e_k·M` have the same strict sign. If
`e_k·M` = 0, compare `e_k·V ≤ e_k·c`. If the numerator is 0, use edge `k − 1` (the EE's 0/0 gives a clamped value
and the test then fails on `0·FLT_MAX` sums, which is inferred). The move then lies in edge `k`'s side plane or
parallel to it, so either choice is rare in play.

#### SphereSweep_EdgeCylinder {#u1-edge-cylinder}

`0x0034dff8`, 0x128 bytes. **Arguments**: `f12` = `r`, `a0` = centre `c`, `a1` = `M`, `a2` = `A`, `a3` = `E`,
`t0` = `int*` count out, `t1` = `vec4*` points out, `t2` = `float roots[2]` out. Confirmed (code) at `0x0034dff8`:

1. `n = Ray_InfiniteCylinder(r, c, M, A, E, roots)`, which writes `roots[0..n−1]`.
2. Count the roots with `0 ≤ roots[i] ≤ 1` (both inclusive). For each, in list order, write the centre at that
   time, `c + roots[i]·M` (`w` = 1), to the next `points[]` slot.
3. `*count` = that number, and the function returns 1 when it is above 0. **`roots[]` is not compacted or sorted**.
   So the edge step's `roots[0]` can be a root outside [0, 1] while `roots[1]` is inside, and the step then reports
   the outside root (which the mesh callers drop).

#### Ray_InfiniteCylinder (a capsule) {#u1-capsule}

`0x0034d938`, 0x6c0 bytes. **The name is wrong: it is the move against a capsule** (radius `r`, axis from `A` to
`A + E`, rounded ends), as the plate comment from another analyst already says. **Arguments**: `f12` = `r`, `a0` =
centre `c`, `a1` = `M`, `a2` = `A`, `a3` = `E`, `t0` = `float roots[2]` out. **Return** the root count, 0-2, with
each root a fraction of `M`. Confirmed (code) at `0x0034d938`.

**Frame.** `L = |E|`, `W = E / L` (VU `vsqrt`, then `1/L`). `U, V` = `Vec_MakePerpendicularBasis(W)`
(`0x00349180`): if `|Wy| ≤ |Wx|`, `U = (−Wz, 0, Wx) / sqrt(Wx² + Wz²)`; else `U = (0, Wz, −Wy) / sqrt(Wy² + Wz²)`.
`V = W × U`. Only `Px² + Py²` and `Dx² + Dy²` are used, so any orthonormal `U, V` gives the same roots up to
rounding. Then `Δ = c − A` and `P = (U·Δ, V·Δ, W·Δ)`. With `m = |M|` (computed as the length of
`(U·M, V·M, W·M)`), `D = (U·M, V·M, W·M) / m`, which is unit, and `k = 1/m`. All roots below are in units of
distance along the unit `D` and are multiplied by `k` to become fractions of `M`.

**Parallel or zero move** (`|Dz| ≥ 0.99999899`, `0x3f7fffef`, or else `m < 1e-6`, `0x358637bd`):
`ρ = (r² − Px²) − Py²` and `s = sqrt(ρ)`.

- If `M·E < 0` and `ρ ≥ 0`: `roots = [(Pz + s)·k, −((L − Pz) + s)·k]`, return 2. Moving toward `A`, this is
  **[exit, entry]**.
- If `M·E > 0` and `ρ ≥ 0`: `roots = [−(Pz + s)·k, ((L − Pz) + s)·k]`, return 2 ([entry, exit]).
- Otherwise (`ρ < 0`, or `M·E` = 0, which includes a zero move) return 0.

So a parallel move meets only the corner spheres (their entry and exit along the axis), and in the `M·E < 0` order
the edge step reports the **exit**.

**General case**, in this order, appending each kept root and **returning as soon as two are kept**:

1. **Side wall.** `a0 = (Px² + Py²) − r²`, `a1 = Px·Dx + Py·Dy`, `a2 = Dx² + Dy²`, `Δw = a1² − a2·a0`.
    - `Δw < 0`: return 0, **with no cap test** (a line that misses the infinite cylinder misses the capsule too).
    - `Δw > 0`: `q = sqrt(Δw)`, then `τ1 = (−a1 − q)·(1/a2)` and `τ2 = (−a1 + q)·(1/a2)`. Keep each, in that order,
      whose axial position `Pz + τ·Dz` is in **[0, L]** (inclusive). If both are kept, return 2.
    - `Δw = 0`: `τ = −a1 / a2`. If `0 ≤ Pz + τ·Dz ≤ L`, keep it and **return 1** (no cap test).
2. **Sphere at `A`.** `b0 = a0 + Pz²` (`= |P|² − r²`), `b1 = a1 + Pz·Dz` (`= P·D`), `Δa = b1² − b0`. If `Δa > 0`,
   try `τ = −b1 − sqrt(Δa)`, then `−b1 + sqrt(Δa)`. If `Δa = 0`, try `τ = −b1` alone. Keep each with axial
   position `≤ 0`.
3. **Sphere at `A + E`.** `c1 = b1 − Dz·L`, `c0 = b0 + L·(L − 2·Pz)`, `Δb = c1² − c0`. Same pattern, keeping each
   with axial position `≥ L`.
4. Return the number kept.

**Consequence: the list is ordered wall, then `A` cap, then `B` cap, not by time.** A move that enters through a
cap and leaves through the wall lists the wall's exit first. The same holds for a move nearly along the axis toward
`A`: the `A` cap's exit comes before the `B` cap's entry. Since [SphereSweep_Edge](#u1-edge) takes `roots[0]`, such
a sphere is reported **late** (at the exit), or **not at all** when the exit is past the move (the caller drops a
fraction above 1). This mostly happens at a triangle's corners, because the corner step's edge always has the corner
as one of its caps. Confirmed (code) at `0x0034dd04`-`0x0034dfc8` and `0x0034e450`. Worked numbers are in
[Test ideas](#u1-tests).

Edge cases: a zero-length edge gives `L` = 0 and `1/L` = `FLT_MAX` on the EE, so `W` is 0, `Dz` = 0, and the
parallel test fails unless `m < 1e-6`. Then `a2` = 1 and the wall roots are kept only when `Pz + τ·Dz` (= 0) is in
[0, 0], that is always. So a zero-length edge behaves like a sphere at `A` (inferred from EE semantics; the mesh
skips degenerate triangles before this). `Δw > 0` strictly and `Δw = 0` exactly are separate branches. There is no
tolerance band.

#### Segment_SphereIntersect {#u1-segment-sphere}

`0x0034e120`, 0x298 bytes. **Arguments**: `a0` = `A`, `a1` = `E`, `a2` = ball centre `C`, `f12` = `r`, `a3` =
`int*` count out, `t0` = `vec4 points[2]` out. **Return** 1 when the segment `A + u·E`, `u ∈ [0, 1]`, meets the
closed ball, else 0. Confirmed (code) at `0x0034e120`.

`D = A − C`, `c = D·D − r²`, `b = D·E`, `a = E·E`, `Δ = b² − a·c`:

- `Δ < 0`: count 0, return 0.
- `Δ > 0`: `s = sqrt(Δ)`, `u1 = (−b − s)·(1/a)`, `u2 = (−b + s)·(1/a)`. If `u1 > 1` or `u2 < 0`: count 0, return 0.
  If `u1 ≥ 0` and `u2 ≤ 1`: count 2, with points `A + u1·E` and `A + u2·E`. If `u1 ≥ 0` and `u2 > 1`: count 1, with
  point `A + u1·E`. If `u1 < 0`: count 1, with point `A + u2·E`, **even when `u2 > 1`** (the segment lies inside the
  ball, and the point is then past the segment). Return 1 in all three.
- `Δ = 0`: `u = −b / a`. If `0 ≤ u ≤ 1`: count 1, point `A + u·E`, return 1. Else count 0, return 0.

A zero-length edge (`a = b = 0`) takes the `Δ = 0` branch with `−0/0`, which is −`FLT_MAX` on the EE, and returns
0 (inferred: EE semantics).

#### Ray_SphereRoot {#u1-ray-sphere-root}

`0x0034af50`, 0x88 bytes, a leaf on FPU registers only. **Arguments**: `f12`-`f14` = `p` (the ray origin minus the
sphere centre), `f15`-`f17` = `d` (the ray direction, any length), `f18` = `R` (the squared radius). **Return**
`f0`. Confirmed (code) at `0x0034af50`.

`c = |p|² − R`, `b = p·d`, `a = |d|²` (each summed as `(x + y) + z`), `s = sqrt(|b² − a·c|)`, `i = 1/s`, and the
result is `(c·i) / (1 − b·i)`. This equals `c / (s − b)` = `(−b − s)/a`, the **smaller root**, in the form that does
not cancel. There is **no miss test**: a negative discriminant is used as its absolute value, so the callers must
check first. `s = 0` gives `i = FLT_MAX` on the EE. It is **not** used by the triangle kernels: its callers are the
sphere-box steps `0x0034b040`, `0x0034b3b8` and `0x0034b7b0` (unit for the box sweep).

#### Ray_CircleRoot {#u1-ray-circle-root}

`0x0034afd8`, 0x64 bytes, the 2D form. **Arguments**: `f12`, `f13` = `p`; `f14`, `f15` = `d`; `f16` = `a`
(supplied by the caller, normally `|d|²`); `f17` = `R`. `c = |p|² − R`, `b = p·d` (summed `dx·px + dy·py`), and
the result is `(c·i) / (1 − b·i)` with `i = 1/sqrt(|b² − a·c|)`. No miss test. Callers: `0x0034b040`, `0x0034b3b8`.
Confirmed (code) at `0x0034afd8`.

#### PointTriangle_DistanceSq {#u1-point-triangle}

`0x0034e840`, 0x58c bytes. **Arguments**: `a0` = point `P`, `a1` = triangle (corners at `+0x00`, `+0x10`,
`+0x20`; nothing else is read), `a2` = `float*` `s` out or null, `a3` = `float*` `t` out or null. **Return** `f0` =
the squared distance, passed through `abs`. The closest point is `v0 + s·E0 + t·E1`, with `E0 = v1 − v0` and
`E1 = v2 − v0`. Confirmed (code) at `0x0034e840`.

This is the public region method of D. Eberly, "Distance Between Point and Triangle in 3D" (Geometric Tools),
followed exactly. With `D = v0 − P`, `a = E0·E0`, `b = E0·E1`, `c = E1·E1`, `d = E0·D`, `e = E1·D`, `f = D·D`,
`det = |a·c − b²|`, `s = b·e − c·d` and `t = b·d − a·e`:

| Region (test order) | Rule |
| --- | --- |
| `s + t ≤ det`, `s < 0`, `t < 0` (4) | if `d < 0`: `t = 0`, then the `t = 0` edge rule; else `s = 0`, then the `s = 0` edge rule |
| `s + t ≤ det`, `s < 0` (3) | `s = 0`, the `s = 0` edge rule |
| `s + t ≤ det`, `t < 0` (5) | `t = 0`, the `t = 0` edge rule |
| `s + t ≤ det` (0, inside) | `s /= det`, `t /= det` (as `× (1/det)`), `q = s·(a·s + b·t + 2d) + t·(b·s + c·t + 2e) + f` |
| `s < 0` (2) | `t0 = b + d`, `t1 = c + e`. If `t0 < t1`: `num = t1 − t0`, `den = (a − 2b) + c`. If `num ≥ den`, `s = 1, t = 0, q = a + 2d + f`; else `s = num/den, t = 1 − s`, `q` as in region 0. Else (`t0 ≥ t1`): `s = 0`; if `t1 ≤ 0`, `t = 1, q = c + 2e + f`; else if `e ≥ 0`, `t = 0, q = f`; else `t = −e/c, q = e·t + f` |
| `t < 0` (6) | the same with the roles swapped: `t0 = b + e`, `t1 = a + d`. If `t0 < t1`: `num = t1 − t0`, `den = (a − 2b) + c`. If `num ≥ den`, `t = 1, s = 0, q = c + 2e + f`; else `t = num/den, s = 1 − t`, `q` as in region 0. Else: `t = 0`; if `t1 ≤ 0`, `s = 1, q = a + 2d + f`; else if `d ≥ 0`, `s = 0, q = f`; else `s = −d/a, q = d·s + f` |
| otherwise (1) | `num = ((c + e) − b) − d`. If `num ≤ 0`: `s = 0, t = 1, q = c + 2e + f`. Else `den = (a − 2b) + c`; if `num ≥ den`, `s = 1, t = 0, q = a + 2d + f`; else `s = num/den, t = 1 − s`, `q` as in region 0 |

The **`t = 0` edge rule**: if `d ≥ 0`, `s = 0, q = f`; else if `−d ≥ a`, `s = 1, q = a + 2d + f`; else
`s = −d/a, q = d·s + f`. The **`s = 0` edge rule** is the same with `e`, `c` and `t`. In region 0, the code adds
`2d` to `(a·s + b·t)` and forms `s·(…) + t·(…)` before adding `f`. A degenerate triangle (`det` = 0) can land in
region 0 and divide by zero (`FLT_MAX` on the EE). The callers do not guard against it (inferred).

#### SphereTriangle_Penetration {#u1-penetration}

`0x0034edd0`, 0x6c bytes. **Arguments**: `a0` = sphere (centre `+0x00`, radius `+0x10`), `a1` = triangle (corners
as above), `a2` = `float*` depth out. **Return** 1 and `*depth = r − sqrt(q)` when `q < r²` (strict), where `q` =
`PointTriangle_DistanceSq(centre, triangle, null, null)`. Otherwise it returns 0 and leaves `*depth` unwritten. It
gives no normal and no point. Caller: the sphere mesh overlap `0x003473c8`. Confirmed (code) at `0x0034edd0`.

#### Issue #150 {#issue-150}

Answered from the code:

1. **Which faces the sphere sweep skips** (`PhysicsMesh_SweepSphere` `0x00348a50`, confirmed (code) at
   `0x00348c30`-`0x00348e10`). Per triangle, in this order:
    - an already visited one;
    - flag `0x8` when the sweeping body has flag `0x100` or the query has `0x8000`;
    - enabled bit `0x1` clear;
    - with query option `0x4`, `n.z` below the first threshold (`f12`), and with `0x8`, `n.z` above the second
      (`f13`);
    - **side**: `n·c − n·v0 < 1e-6` (`0x358637bd`, `c` = the centre at the start, not raised) flips a two-sided face
      (flag `0x2`) and skips a one-sided one;
    - **direction**: `n·M ≥ −0.001` (`0xba83126f`, after a flip, `M` the whole move, not normalised) skips the face.

   With option `0x40` the last two skip nothing and mark the contact `0x10` (behind), `0x20` (away) or `0x40`
   (`|n·M| ≤ 0.001`, `0x3a83126f`) instead. Through the body sweep (`IPhysics_SweepShapeVsMesh` `0x0033d2d8`) the
   query asks for one contact and sets only option `0x4` (from body `+0x44` bit `0x40`), with thresholds −0.65 and
   0. So a sphere body skips faces it is behind and faces it does not move into by more than 0.001.
2. **The edge cylinder's end caps**: **capped.** `0x0034d938` is a capsule: a finite wall over the edge's length
   plus a sphere of radius `r` at each corner (and only the corner spheres for a move parallel to the edge). The cap
   roots are listed after the wall roots, and the first listed root is the one reported, which gives the late or
   missed corner contacts described under [the capsule](#u1-capsule).
3. **Which corner edge the vertex-region step tests**: **one edge**, chosen by where the centre's path crosses edge
   `k`'s side plane. If that point lies along edge `k` past the corner (`e_k·X ≥ e_k·v_k`), it tests edge `k`
   (`v_k → v_k+1`); otherwise edge `k − 1` (`v_k−1 → v_k`). There is no contact at all when the corner is already
   inside the ball or the move does not go toward it ([SphereSweep_VertexRegion](#u1-vertex-region)).

#### Test ideas {#u1-tests}

These use synthetic geometry only. The numbers come from a double-precision model of this spec (not from the
game), and a tolerance of 1e-5 is enough. Triangle `T`: `v0 = (0, 0, 0)`, `v1 = (4, 0, 0)`, `v2 = (0, 4, 0)`, which
gives `n = (0, 0, 1)` and `d = 0`. Radius 0.5 throughout unless stated. "Fraction" is the value the kernel records;
mask is `*t0`.

**Working triangle.** Edges of `T`: `e0 = (4, 0, 0)`, `e1 = (−4, 4, 0)`, `e2 = (0, −4, 0)`, all with `w` 1. Normalised
(`0x00343b20`), `e1` becomes (−0.707107, 0.707107, 0) with lengths 4, 5.656854, 4. Flip: corners become `v0`,
(0, 4, 0), (4, 0, 0), `n = (0, 0, −1)` with `w` 1, and the edges are unchanged until recomputed.
`m0 = (0, −4, 0)`, `m1 = (4, 4, 0)`, `m2 = (−4, 0, 0)`.

| # | Centre `c` | Move `M` | Expected |
| --- | --- | --- | --- |
| 1 | (1, 1, 2) | (0, 0, −3) | hit, fraction **0.5**, mask 7 (face) |
| 2 | (1, 1, 0.3) | (5, 0, 0) | hit, fraction **0**, mask 7 (already reaching, any move) |
| 3 | (2, −0.3, 2) | (0, 0, −3) | hit, fraction **0.533333** (= 8/15), mask 6, edge 0, capsule roots [0.533333, 0.8] |
| 4 | (2, −0.3, 0.3) | (0, 0, −1) | hit, fraction **0**, mask 6 (the segment already meets the ball) |
| 5 | (1, 1, 2) | (0, 0, 1) | no hit (moving away), mask not written |
| 6 | (1, 1, 2) | (0, 0, −1) | no hit (`t` = 1.5 > 1), mask not written |
| 7 | (1, 1, −2) | (0, 0, 3) | hit, fraction **0.5**, mask 7 (from behind: the kernel alone accepts it; the mesh caller would flip or skip first) |
| 8 | (−1, −1, 1) | (2, 1, −1.5) | hit, fraction **0.538462** (= 7/13), mask 2: the corner `v0` step picks **edge 0** |
| 9 | (−0.2, −0.2, 0.1) | (1, 1, 0) | **no hit**: mask 2, and corner `v0` is inside the ball (`*fraction` set to 0, return 0) |
| 10 | (−1, −1, 0.2) | (−1, −1, 0) | no hit: mask 2, moving away from the corner |
| 11 (late) | (−0.4, −0.6, 0.6) | (−0.2, 1.8, −1.4) | hit, fraction **0.476954**, mask 2, corner step picks **edge 2**; capsule roots [0.476954 (wall exit), 0.295703 (cap entry)], so the true first touch 0.295703 is **not** reported |
| 12 (missed) | (−0.9, −0.9, 0.6) | (0.6, 1.2, −0.6) | kernel returns 1 with fraction **1.783594** (mask 2, edge 2, roots [1.783594, 0.718294]); the mesh caller drops it, so the sphere passes the corner although it touches it at 0.718294 |
| 13 (parallel) | (6, −0.3, 0.2) | (−3, 0, 0) | mask 4, corner `v1` step picks **edge 0**, parallel branch with `M·E < 0`: roots [2.115470, 0.551197]; kernel returns 1 with fraction **2.115470**, dropped by the caller |
| 14 (cap) | (−2, −0.3, 0.2) | (3, 0, 0) | mask 2, corner `v0` step picks **edge 2** (the move crosses it). Hit, fraction **0.551197**, from the cap at `v0`: the wall roots fall outside the edge's length, and the end sphere's roots are [0.551197, 0.782137]. The caps do stop a sphere when they are the only thing it meets |

**Kernels on their own:**

- `Segment_SphereIntersect(A = 0, E = (4, 0, 0), C, r)`: `C = (2, −0.3, 0.3)`, r 0.5 gives 1, count 2, points
  (1.735425, 0, 0) and (2.264575, 0, 0). `C = (4.3, −0.3, 0)` gives 1, count 1, point (3.9, 0, 0) (`u1` 0.975,
  `u2` 1.175). `C = (2, 0, 0)`, r 5 gives 1, count 1, point **(7, 0, 0)** (`u1 < 0`, so `u2` = 1.75 is used,
  outside the segment). `C = (2, −0.5, 0)`, r 0.5 is tangent (`Δ` = 0 exactly) and gives 1, count 1, point
  (2, 0, 0). `C = (2, −1, 0)` gives 0.
- `Ray_InfiniteCylinder(r 0.5, c = (2, −0.3, 2), M = (0, 0, −3), A = 0, E = (4, 0, 0))`: 2, [0.533333, 0.8]. With
  `c = (−0.9, −0.2, 0.45)`, `M = (1.6, 0, −0.5)`: 2, **[1.816515, 0.336596]** (wall exit first, then the `A` cap's
  entry); `SphereSweep_EdgeCylinder` keeps 1 (0.336596) and returns 1, and `SphereSweep_Edge` reports
  **1.816515**.
- `Ray_SphereRoot`: `p = (−3, 0, 0)`, `d = (1, 0, 0)`, `R` 1 gives **2**. `p = (−3, 0.6, 0)`, `d = (2, 0, 0)`, `R` 1
  gives **1.1**. `p = (0.5, 0, 0)`, `d = (1, 0, 0)`, `R` 1 (inside) gives **−1.5**. `p = (−3, 2, 0)`,
  `d = (1, 0, 0)`, `R` 1 (a miss, `Δ` = −3) gives **2.535898** (`12 / (sqrt 3 + 3)`): no miss signal.
- `Ray_CircleRoot`: `p = (−3, 0)`, `d = (1, 0)`, `a` 1, `R` 4 gives **1**.
- `PointTriangle_DistanceSq(P, T)` as (`q`, `s`, `t`): (1, 1, 2) gives (4, 0.25, 0.25); (−1, −1, 0) gives (2, 0, 0);
  (5, −1, 1) gives (3, 1, 0); (3, 3, 0) gives (2, 0.5, 0.5); (0.5, 0.5, −0.5) gives (0.25, 0.125, 0.125);
  (2, −2, 0) gives (4, 0.5, 0); (−1, 2, 0) gives (1, 0, 0.5). This covers regions 0, 4, 6/5, 1, 0, 5 and 3.
- `SphereTriangle_Penetration`: centre (1, 1, 0.3), r 0.5 gives 1, depth **0.2**. Centre (1, 1, 0.5), r 0.5 gives
  **0** (`q = r²`, the test is strict). Centre (5, −1, 1), r 2 gives 1, depth **0.267949** (`2 − sqrt 3`).

### Unit 2: A shape against the collision mesh {#unit-2}

**What it is.** A shape swept against (or overlapping) the level's collision mesh: the cells, the visited bits, the
per-triangle rules, the capsule's wall rules and corner slide, the sort, and the contact each hit becomes.

- **Coney:** replaces `human::nearestWallPush`, `slideAlongTouchedFaces` and `edgeSlide` (`src/human/body.*`) with
  the swept test from the start of the move.
- **Tests:** [Test ideas](#u2-test-ideas-synthetic-meshes-src-raycast), built with `src/raycast/collision_builder.h`;
  test 1 is the fast mover that must not tunnel.

10 functions, 7136 bytes (2 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00347170` | `CollisionMesh_BeginShapeQuery` | 396 |
| `0x00347300` | `MeshContacts_SortByFraction` | 184 |
| `0x0033d718` | `Contact_InsertSorted` | 160 |
| `0x0033d2d8` | `IPhysics_SweepShapeVsMesh` | 92 |
| `0x00347c08` | `PhysicsMesh_SweepCapsule` (tagged) | 2344 |
| `0x00348a50` | `PhysicsMesh_SweepSphere` | 1836 |
| `0x003473c8` | `PhysicsMeshOverlap_Sphere` | 1016 |
| `0x003473b8` | `PhysicsMeshOverlap_Box` | 8 |
| `0x003473c0` | `PhysicsMeshOverlap_Capsule` | 8 |
| `0x003477c0` | `PhysicsBody_PushOutOfWalls` (tagged) | 1092 |

Conventions: `n·x` is a dot product over x, y, z (the VU sums only those three), `a × b` a cross product, "unit" means
divided by its length. Vectors are 16-byte `float[4]`; `w` is noted only where code sets it.

#### Shared records {#u2-shared-records}

#### The working triangle (`T`, 0xd0 bytes on the caller's stack) {#u2-the-working-triangle-t-d0-bytes-on-the-c}

Filled by every mesh function for the triangle under test; the helpers below write into it. Confirmed (code) at
`0x00343a90`, `0x00343b20`, `0x00343c30`, `0x00343c90`.

| Offset | Content | Written by |
| --- | --- | --- |
| `+0x00`, `+0x10`, `+0x20` | corners `v0`, `v1`, `v2` (copied whole from the vertex buffer, `w` = 1.0) | the caller |
| `+0x30`, `+0x40`, `+0x50` | edges `e0 = v1 − v0`, `e1 = v2 − v1`, `e2 = v0 − v2` (`w` = 1) | `CollTri_ComputeEdges` `0x00343a90` |
| same | with its second argument non-zero, the edges are made **unit** in place | `CollTri_ComputeEdgeNormals` `0x00343b20` (the name is wrong: it normalises the edges; no edge normals are made) |
| `+0x60`, `+0x64`, `+0x68` | the three edge lengths (only when normalised) | `0x00343b20` |
| `+0x70` | unit normal `unit((v1 − v0) × (v2 − v0))` | `CollTri_ComputeNormal` `0x00343c30` |
| `+0x80` | plane constant `d = n·v0` (a scalar in `x`) | the caller, just before the kernel |
| `+0x90` | a copy of the normal (capsule sweep only) | the caller |
| `+0xc0` | the kernel's result record: `+0xc0` hit count (cleared to 0 before the call), `+0xc4` fraction | the kernel |

**Flip** (`CollTri_Flip` `0x00343c90`): swaps corners 1 and 2 and negates the normal (all four lanes). It does not
recompute `d` or the edges. Confirmed (code). So after a flip, "edge k" means the edge of the swapped corner order.

#### The mesh contact (0xb0 bytes) {#u2-the-mesh-contact-b0-bytes}

Each mesh function collects hits in a stack array of **100** mesh contacts (17,600 bytes) plus a 100-entry pointer
list; nothing checks the count against 100 (confirmed (code) in all four collectors: `0x003481a0`, `0x00348ec0`,
`0x00347674`, `0x00347abc`). Fields:

| Offset | Capsule sweep | Sphere sweep | Sphere overlap | Push-out |
| --- | --- | --- | --- | --- |
| `+0x00` sort key | fraction `t` | fraction `t` | depth `r − dist` | squared distance |
| `+0x04` | pointer to the 10-byte file triangle | same | same | same |
| `+0x08` | not written | flags `0x10` / `0x20` / `0x40` (below) | 0 | 0 |
| `+0x0c` | not written | signed start distance `n·c − d` (normal before any flip) | not written | not written |
| `+0x10` | 0.0 | not written | not written | not written |
| `+0x14` | the kernel's region code (below) | not written | not written | not written |
| `+0x20`…`+0xaf` | a copy of `T+0x00`…`T+0x8f`: corners at `+0x20/+0x30/+0x40`, edges at `+0x50/+0x60/+0x70`, lengths `+0x80`, **normal `+0x90`**, `d` at `+0xa0` | same | same (key and pointer only otherwise) | same |

Confirmed (code) at the cited stores. The edges in the copy are whatever `T` held at the copy: for both sweeps that
is the **un-normalised** set (`CollTri_ComputeEdges(T, 0)` runs again just before the kernel, `0x00348118`,
`0x00348e18`), while the lengths at `+0x80` are left from the capsule's earlier normalising call.

**Region code** (capsule only, the kernel's fifth argument): bit `i` is set when the sphere's centre, taken at the
point where it meets the plane, lies on the inner side of edge `i`, i.e. `(e_i × n)·p ≤ (e_i × n)·v_i`
(`0x0034ee60`, Unit 1). 7 means inside all three edges (a face hit). Confirmed (code).

#### Mapping into the 0xc0 contact {#u2-mapping-into-the-c0-contact}

Every sweep and overlap writes its output straight into a caller-supplied array of 0xc0 contacts (layout in
physics.md #contacts), one per kept mesh contact, in the order given under each function. Fields a mesh function
leaves alone are filled by the caller through `Contact_InsertSorted` (`+0x00` own owner, `+0x40` other owner = 0).
Common to the three that write contacts, confirmed (code):

| 0xc0 field | Value |
| --- | --- |
| `+0x04` own shape | the querying shape |
| `+0x44` other shape | 0 (the level) |
| `+0x50` normal | mesh contact `+0x90` (unit, turned to the shape when flipped) |
| `+0x80` fraction | mesh contact `+0x00` (for the overlap this is the **depth**, not a fraction) |
| `+0xb4` triangle | mesh contact `+0x04` |

Per function: the sweeps add `+0x10` = unit move, `+0x90` = contact point = `t × move` + the shape's position
(shape vtable `+0x24`, which for the sphere `0x004f1210` and capsule `0x004f1248` returns `&shape+0x50`; for the
capsule that is the feet, not the touching point; `w` = 1); the capsule adds `+0xa0` slide vector, `+0xb0` = 0 or 1
and `+0x84` = 0.0; the sphere adds `+0xb0` = its mesh `+0x08` flags and `+0x88` = mesh `+0x0c`; the overlap writes
`+0xb0` = 0 and nothing else.

#### The contact list and its cap of 50 {#u2-the-contact-list-and-its-cap-of-50}

The callers own the list. `PhysicsBody_Sweep` (`0x0033e278`) keeps a pool of exactly 50 contacts (`sp+0x110` to
`sp+0x2690`), a 50-pointer list and a count, reset to 0 at the start of each pass (`0x0033e4a4`).
`IPhysics_CollideShape` (`0x0033e7f0`) and `IPhysics_OverlapShape` (`0x0033ebb8`) pass the mesh function the room
left, `50 − count` (`0x0033ea90`, `0x0033ee8c`). The cap is honoured by the sphere sweep and the sphere overlap and
**ignored by the capsule sweep** (it never reads that argument). `Contact_InsertSorted` itself does not check it.
Confirmed (code).

#### `0x00347170` CollisionMesh_BeginShapeQuery {#u2-00347170-collisionmesh-beginshapequery}

Size `0x18c`. `BeginShapeQuery(&mesh, &cellMin, &cellMax, &worldBox)`; returns nothing. Confirmed (code) unless marked.

- **Mesh.** Writes the level's collision mesh (world manager global `0x005147c4` → `+0x40` level object → `+4`) to
  `*mesh`.
- **Visited bits.** Zeroes the mesh's checked bit set (`+0x94`), `(((count + 7) >> 3) + 15) & 0x7ff0` bytes for
  `count` = `+0x84`, 16 bytes at a time. Every collector then tests and sets bit `idx & 7` of byte `idx >> 3` before
  looking at triangle `idx`, so a triangle listed in several cells is looked at once per query. The bit set is
  shared mesh state: two queries cannot be interleaved (inferred: nothing in this unit nests them).
- **Cells.** `worldBox` is `{min, max}` (two vectors). Each corner is transformed separately by the header's
  world-to-grid matrix (`g = x·r0 + y·r1 + z·r2 + r3`, header `+0x10`…`+0x4f`), its `w` set to 0, clamped per axis
  to `[header+0x50, header+0x60]` (that is `[0.5, n − 0.5]` on the disc) and truncated toward zero (`vftoi0`).
  `cellMin`/`cellMax` receive the integer cell coordinates (x, y, z). Because the corners are transformed one by one,
  a negative scale would swap them; every disc matrix is a positive diagonal scale plus a translation
  (collision.md #header), so `cellMin ≤ cellMax` holds (inferred from the disc survey).
- **The cell walk** (in each caller, confirmed (code) in all four): x from `cellMin.x` to `cellMax.x` inclusive
  (outermost), then y, then z (innermost), unsigned compares; cell entry `(z·ny + y)·nx + x` of the grid
  (collision.md #grid); an entry of 0, or a list whose count is 0, is skipped; the list is walked in stored order.
  So triangles are met in (x, y, z, list) order, which only matters through the sort's tie rule below.

#### `0x00347300` MeshContacts_SortByFraction {#u2-00347300-meshcontacts-sortbyfraction}

Size `0xb8`. `SortByFraction(pointerList, meshContacts, count)`; fills `pointerList[0..count−1]` with pointers into
the 0xb0 array, ordered by the float at `+0x00`. Confirmed (code).

Algorithm: insertion sort in arrival order. For contact `i`, scan the list from the front for the first `j` whose key
is **strictly greater** than contact `i`'s; place `i` there, else append. Equal keys therefore keep arrival order.

**Quirk, faithful behaviour (confirmed (code) at `0x00347360`-`0x00347374`):** the shift that makes room copies
**forwards** (`list[j+1] ← list[j]`, then reads the slot it has just written), so instead of moving `list[j..i−1]` up
by one it writes the old `list[j]` into every slot `j+1..i` (and one past). Effect:

- `list[0]` is always the true minimum, and the list stays non-decreasing;
- each insertion before the end throws away the entries that were after `j` and replaces them with copies of the old
  `list[j]`.

Example: keys arriving 0.5 (A), 0.6 (B), 0.3 (C) end as `[C, A, A]`; B is lost. A correct sort would give `[C, A, B]`.
Who sees it: only callers that use more than the first entry, i.e. the capsule's corner-slide loop and the contacts
`IPhysics_CollideShape` passes on (duplicates are inserted twice), and the overlap's choice of contact (below). The
walking sweep (`PhysicsBody_Sweep`) and the push-out use only the first entry, so for them the sort is exact.

#### `0x0033d718` Contact_InsertSorted {#u2-0033d718-contact-insertsorted}

Size `0xa0`. `InsertSorted(phys, contact, list, &count, ownOwner, otherOwner, ownShape, otherShape)`; returns
nothing. Confirmed (code).

1. Writes `+0x00` = `ownOwner`, `+0x40` = `otherOwner`, `+0x04` = `ownShape`, `+0x44` = `otherShape`.
2. Finds the first list position whose contact has a fraction (`+0x80`) **strictly greater** than the new one's,
   moves the tail up by one (copying backwards, correctly), and stores the pointer there; else appends.
3. `count += 1`. No check against 50 (the callers keep to it, above). Ties: the new contact goes **after** existing
   contacts of equal fraction.

#### `0x0033d2d8` IPhysics_SweepShapeVsMesh {#u2-0033d2d8-iphysics-sweepshapevsmesh}

Size `0x5c`. Called only by `IPhysics_SweepBodyVsMesh` (`0x0033d340`, pass step 2 of physics.md #sweep). Returns
true when the type's mesh sweep returned a non-zero count. Confirmed (code).

It picks the mesh sweep by the shape's type byte (shape `+0x30`) from IPhysics `+0xe0 + 4·type` (none → false) and
calls it with:

| Argument | Value |
| --- | --- |
| out | the pool slot at the current count (`pool + count·0xc0`) |
| max contacts | **1** |
| world box | the pass's box: the shape's box at the start of the move, grown by the move, **not** scaled by body `+0x60` ([Unit 3](#u3-sweep), step 5.2) |
| body, shape | the body and its first shape |
| move | the pass's move (`v × dt`) |
| options | `4` when the body's type mask (`+0x44`) has `0x40` (a human), else `0` (`(mask >> 4) & 4`) |
| minimum `n.z` (f12) | **−0.65** (`0xbf266666`) |
| maximum `n.z` (f13) | 0.0 (read only with option 8, never set here) |

`0x0033d340` then inserts **only the first** returned contact (`Contact_InsertSorted` with owner = body `+0x34`, other
owner 0, own shape, other shape 0; `0x0033d3ac`-`0x0033d3d4`). So in the walking sweep each pass meets at most one
mesh contact, the nearest. For the capsule this is fine (its first entry is exact, and the slide vector it carries
was built from all of them). The −0.65 threshold and option 4 are **ignored by the capsule** sweep; for a sphere body
they mean "skip ceilings steeper than n.z < −0.65" for humans only. The landing segment of an airborne human is added
after this call (physics.md #sweep), not here.

#### `0x00347c08` PhysicsMesh_SweepCapsule {#u2-00347c08-physicsmesh-sweepcapsule}

Size `0x928`. Mesh sweep table `+0xe0`, type 3. Register arguments: `out` (0xc0 array), `max` (ignored), `worldBox`,
`body`, `shape`, `move` (pointer); `f12`, `f13` and the options word are never read (confirmed (code): no use of
`a1`, `t2`, `f12` or `f13` in `0x00347c08`-`0x0034852c`). Returns the number of contacts written, 0 if none. All
steps confirmed (code) at the cited addresses.

**The sphere** (as characters.md #walls): `r = shape+0x40 × body+0x60 × Human_GetScale(body+0x34)`
(`0x00347c98`-`0x00347cbc`); centre `c` = shape `+0x50` (the feet at the start of the move) with `z += r + 0.05`
(`0.05` = `0x3d4ccccd`). The kernel is given the record `{c, r}`.

**Per triangle**, in cell-walk order, once per query (visited bit):

1. Skip unless flag bit 0 (enabled) is set (`0x00347de0`). No type-bit mask, no `0x800` rule.
2. Load the corners, compute the unit normal `n`.
3. **Wall:** skip if `|n.z| > 0.65` (`0x3f266666`, `0x00347e60`; so exactly 0.65 is a wall).
4. **Side:** `s = n·c − n·v0`. If `s < 1e-6` (`0x358637bd`): a one-sided triangle (flag `0x2` clear) is skipped; a
   two-sided one is flipped (`0x00347ec0`-`0x00347ee4`). A centre exactly on the plane counts as behind.
5. **Into the face:** skip if `n·move ≥ −0.001` (`0xba83126f`, `0x00347f2c`), using the possibly flipped normal. A
   zero move therefore never yields a contact.
6. **Low:** normalise the edges (lengths kept). Take the edge `k` with the largest `|unit e_k.z|` (first one wins a
   tie). Skip if `|v_{k+1}.z − v_k.z| < 0.25` (`0x3e800000`, `0x00347fc4`), corners indexed mod 3.
7. **Thin:** take the longest edge `m` (first one wins a tie). If `|unit e_m.z| < 0.8` (`0x3f4ccccd`): with
   `p = n × unit e_m` (a unit vector in the plane, across the edge), skip if `|p·v_{(m+2) mod 3} − p·v_m| < 0.25`,
   i.e. the third corner is less than 0.25 from the edge's line (`0x00348060`-`0x003480d4`). A nearly vertical
   longest edge skips this test.
8. Store `d = n·v0`, recompute the edges un-normalised, clear the result count and the region, and call
   `Sweep_SphereTriangle(result, {c, r}, T, move, &region)` (`0x0034ee60`).
9. Keep the hit when the kernel returns non-zero and `0 ≤ t ≤ 1` (`t` = result `+4`): append a mesh contact
   (`t`, triangle pointer, `+0x10` = 0, `+0x14` = region, the copy of `T`).

**Bug (confirmed (code) at `0x00348170`):** a hit with `t > 1` leaves the cell's loop counter un-incremented while
the list pointer moves on, so that cell's walk reads one `u16` past the end of its list and treats it as a triangle
index (subject to the visited bit and the tests). The kernel's face path refuses `t > 1`, but its edge steps can return
a fraction above 1
([Unit 1](#u1-edge-cylinder)), so the overrun is reachable at a triangle's edges and corners (inferred: [Unit
1](#u1-tests) cases 12
and 13 return 1 with a fraction above 1; not seen at runtime). An implementation should not
read past the list; note the difference in a comment.

**After the walk** (none kept → return 0):

1. Sort (`0x00347300`, with its smear).
2. **Corner slide** (`0x00348244`-`0x00348374`). Start `S = (0, 0, 0)` and `flag = 0`. For each entry of the sorted
   list, in order (duplicates included):
    - skip a face hit (region 7);
    - skip if the body's owner (body `+0x34`, state flags through owner vtable `+0x54`) has `0x4000000` (airborne);
    - `k` = triangle flags `>> 14` (bits 14-15, read from the file triangle, not the flipped copy); skip if `k` = 3;
    - skip if region bit `k` is set (the centre was inside edge `k`);
    - otherwise `t_h = n × (0, 0, 1) = (n.y, −n.x, 0)` with the contact's normal; if edge `k`'s `z` (mesh contact
      `+0x58 + 16k`, the un-normalised edge of the possibly flipped order) is **> 0**, `S −= t_h`, else `S += t_h`;
      `flag = 1`.
3. `S.z = 0`; `S = unit(S) × max(r, |move|)` (`0x00348378`-`0x003483f8`). With no corner contact `S` is zero; the
   VU's reciprocal square root of 0 gives the largest float and `0 × that` = 0, so `S` stays zero on the PS2. In C++
   guard the zero length explicitly (inferred: the VU result is the documented `RSQRT` overflow behaviour).
4. **Output** (`0x00348404`-`0x003484ec`), for each sorted entry `i` into `out[i]`: the common fields, `+0x10` unit
   move, `+0x90` point, `+0xa0 = S` (the same for all), `+0xb0 = flag`, `+0x84 = 0.0`. Returns the count (all of
   them; `max` is not applied).

**What the corner slide is.** Bits 14-15 name one edge of the triangle (0, 1 or 2; 3 = none). When the swept sphere
meets the triangle beyond that edge (not on its face) and the body is on the ground, `S` is a horizontal unit
direction along the wall, chosen by the edge's direction: worked through for a vertical wall whose front faces the
viewer, an edge running down is the wall's left end and gives "left", an edge running up the right end and gives
"right", so `S` always points **outward past the flagged end**. So it eases a walker round an outside corner instead
of stopping on the edge. The arithmetic is confirmed (code); the purpose is inferred. Which faces carry it on the disc
is not counted here (characters.md #slopes notes gravel faces with 3).

**Who consumes it** (`PhysicsBody_ResolveContacts` `0x0033d9d8`, confirmed (code)): for a contact whose handler
answered a non-zero code, `+0xb0` is read (`0x0033e108`).

- `+0xb0 & 1` (the corner slide): unless the body is a human in a pair or block (`Human_IsInPairOrBlock`
  `0x00227fd8`) or the body's flags `+0x40` have `0x8000`, take `S` from `+0xa0`, remove from it the part that points
  back against the move (`Vec_RemoveNormalPart(S, unit move)` `0x0033d870`: only when `S·m < 0`) and **add it to the
  pass velocity** (the vector the next pass's move is made from as `velocity × dt`, `PhysicsBody_Sweep`'s
  `sp+0x40`), `0x0033e148`-`0x0033e178`. So the next pass (there are at most three) moves an extra `S × dt`
  sideways; the velocity reported back to the owner (`sp+0x50`) does not get it. In this branch the `0x20000`
  contact-scale rule is **not** applied.
- Otherwise `+0xb0 & 2` is the other slide (store `+0xa0` once per move in body `+0x50`), and `0x20000` the scale.
  (physics.md #contacts calls this "bit 2"; it is the value 2, bit 1.)

Since the body sweep inserts only the first contact, it is that contact's `+0xa0`/`+0xb0` that is read; they hold the
`S` and flag computed from all contacts.

**How it differs from Coney's stand-in.** The test is swept from the start position (`c` at the start, `t` the
fraction of the move where the sphere first touches), the side is decided **before** the move, and only faces the
move goes into count. A body moving fast enough to cross a wall within one update still meets it at the fraction
where it touches, so there is no tunnelling; a body that starts behind a one-sided wall passes through it.

#### `0x00348a50` PhysicsMesh_SweepSphere {#u2-00348a50-physicsmesh-sweepsphere}

Size `0x72c`. Mesh sweep table `+0xe0`, type 2. Arguments: `out`, `max`, `worldBox`, `body` (may be read for its
flags), `shape`, `move`, `options`, `f12` = minimum `n.z`, `f13` = maximum `n.z`. Returns the number of contacts
written. Confirmed (code) at the cited addresses.

**The sphere:** centre = shape `+0x50` exactly (no raise), radius = shape `+0x40` (no body or human scale).

**Per triangle** (same walk and visited bits; the loop counter is correct here):

1. **Bit 3** (`0x0008`, within the low 12 bits): if set, skip when the body's flags `+0x40` have `0x100` or the
   options have `0x8000` (`0x00348c34`-`0x00348c54`). Meaning of bit 3 and of body flag `0x100`: not traced.
2. Skip unless enabled (bit 0 of `flags & 0xfff`).
3. Normal. Option `4`: skip if `n.z < min`; option `8`: skip if `n.z > max` (`0x00348cd4`, `0x00348cf8`). No fixed wall
   rule, no 0.25 m rules, no corner slide.
4. **Side:** `s = n·c − n·v0` (kept for `+0x0c`). If `s < 1e-6`: two-sided → flip; one-sided → skip, unless option
   `0x40`, in which case it is kept with mesh flag `0x10` (the normal is not flipped).
5. **Into the face:** if `n·move ≥ −0.001`: skip, unless option `0x40`, which keeps it and ORs `0x40` when
   `n·move ≤ 0.001` (`0x3a83126f`, moving along the face) or `0x20` otherwise (moving away).
6. `d = n·v0`, edges un-normalised, kernel `0x0034ee40` (the same kernel without the region output) with `{c, r}`,
   `T`, `move`. Keep it when it returns non-zero and `0 ≤ t ≤ 1`: mesh contact with `t`, triangle, `+0x08` flags,
   `+0x0c` = `s`, copy of `T`.

**After the walk:**

1. Sort (with its smear); then `count = min(count, max)` (`0x00348f6c`).
2. **Option `0x20000`, one contact per plane** (`0x00348f84`-`0x00349044`): for each kept entry `i` in order, scan the
   entries after it; when `|n_i · n_j| > 0.9` (`0x3f666666`) swap entry `j` with the last entry and drop the last
   (`count −= 1`), then move on to `j + 1` **without testing the entry just swapped into `j`**. The order after
   removals is therefore no longer sorted. The cap to `max` happens first, so this can leave fewer than `max` even
   when more were found.
3. Output in list order: the common fields, `+0x10` unit move, `+0x90` point, `+0xb0` = the mesh flags, `+0x88` =
   the start distance `s`. No `+0xa0` slide vector, no `+0x84`.

**Differences from the capsule, in one list:** the sphere is the shape's own (no `+0.05` raise, no body or human
scale); walls are not singled out (the caller's `n.z` range does it, if asked); no low/thin rules; no corner slide;
the `max` argument is honoured; options `0x40` (keep backfaces and receding faces, flagged), `0x8000` and `0x20000`
exist; bit-3 triangles can be excluded. Reached through the walking sweep for a type-2 body (max 1, options 4 for a
human type mask else 0, min `n.z` −0.65) and through `IPhysics_CollideShape` with the query's own options and `n.z`
range (below).

**The query record of `IPhysics_CollideShape` / `IPhysics_OverlapShape`** (the fields the mesh path reads,
confirmed (code) at `0x0033e834`-`0x0033e8dc`, `0x0033ea64`-`0x0033eab4`, `0x0033ee48`-`0x0033eea0`):

| Offset | Meaning |
| --- | --- |
| `+0x00` | the body (passed as `body`; its owner `+0x34` becomes contact `+0x00`) |
| `+0x04` | the shape (its type byte `+0x30` picks the mesh function) |
| `+0x08` | a transform, used for the box when option `0x400` |
| `+0x10` | options: `1` test the mesh, `2` the bodies; the whole word is passed on, so `4`, `8`, `0x40`, `0x8000`, `0x20000` reach the mesh function |
| `+0x18` | the most contacts to copy out |
| `+0x28`, `+0x2c` | minimum and maximum `n.z` (f12, f13) |

`CollideShape` inserts every returned contact with `Contact_InsertSorted` (duplicates from the smear included);
`OverlapShape` adds the returned value to its count without re-sorting.

#### `0x003473c8` PhysicsMeshOverlap_Sphere {#u2-003473c8-physicsmeshoverlap-sphere}

Size `0x3f8`. Mesh overlap table `+0xf4`, type 2. Arguments: `out`, `room`, `worldBox`, (an unused fourth), `shape`,
`options`, `f12` min `n.z`, `f13` max `n.z`. Confirmed (code).

1. Sphere: centre shape `+0x50`, radius shape `+0x40`.
2. Per triangle (walk, visited bits): enabled (raw bit 0); normal; option `4` / `8` as for the sweep
   (`0x003475f8`, `0x0034761c`). **No side test and no flip**: one-sided faces are met from behind too, and the
   normal is the file's.
3. `SphereTriangle_Penetration` (`0x0034edd0`): kept when the point-triangle distance² < `r²`, key = depth
   `r − distance`.
4. Sort ascending by depth (shallowest first, with the smear); `n = min(count, room)`; write the kept entries
   **in reverse**, so `out[0]` is the deepest of the `n` shallowest; each with `+0x04` shape, `+0x44` 0, `+0x50`
   normal, `+0x80` depth, `+0xb0` 0, `+0xb4` triangle.
5. **Returns 1** when anything was found, 0 otherwise, **not the count** (`0x0034770c`-`0x0034777c`).
   `IPhysics_OverlapShape` adds the return to its count (`0x0033eea8`), so only `out[0]` is ever seen. In effect the
   overlap reports one contact: the deepest penetration when there are no more than `room` (inferred from the
   two facts above).

#### `0x003473b8` and `0x003473c0`: the box and capsule mesh overlaps {#u2-003473b8-physicsmeshoverlap-box-and-0034}

8 bytes each: return 0. A box or a capsule never overlaps the level mesh; `IPhysics_OverlapShape` with such a shape
gets no mesh contacts. Confirmed (code).

#### `0x003477c0` PhysicsBody_PushOutOfWalls {#u2-003477c0-physicsbody-pushoutofwalls}

Size `0x444`. `PushOutOfWalls(phys, body, sphere)` with `sphere = {centre, radius at +0x10}`; returns nothing.
Caller: `Human_PushOutInAir` (`0x0021a490`) only (characters.md #slopes). Confirmed (code).

1. Box `centre ± r` (`AABB_FromSphere` `0x00341660`), cells as above.
2. Per triangle: enabled; normal; **wall** `|n.z| ≤ 0.65`; side `n·centre − n·v0 < 1e-6` → one-sided skipped,
   two-sided flipped; then `dist² = PointTriangle_DistanceSq(centre, T)` (closest point on the whole triangle,
   edges and corners included); kept when `dist² < r²`, key `dist²`.
3. Sort; take **the nearest only** (`list[0]`, exact despite the smear).
4. Push = `(n.x, n.y, 0) × (r − sqrt(dist²))` with that triangle's (possibly flipped) normal: **horizontal only**,
   `n.z` is zeroed first (`0x00347b74`), so the push is shorter than `r − dist` on a sloped wall. Added to the body's
   pending push-out (body `+0x50`), and body flag `0x1000` set (`0x00347b70`), which makes `PhysicsBody_Sweep` apply
   the push-out without dividing it by the push weight (`0x0033e278`, the `0x1000` test).
5. The push is along the face normal even when the closest point is on an edge or corner (so its length is
   `r − dist`, not the depth along `n`). No contacts are produced; nothing happens with no wall in reach.

Compared with `CollisionMesh_SpherePush` (collision.md #sphere-push): 0.65 instead of cos 15°, one nearest wall
instead of the average of all, edge and corner distances count, and the push is horizontal and deferred to the next
sweep instead of moving the centre.

#### Test ideas (synthetic meshes, `src/raycast/collision_builder.h`) {#u2-test-ideas-synthetic-meshes-src-raycast}

All with one-sided triangles unless stated, flags given as the file stores them, cell size 2 m.

1. **Wall stops a fast mover (no tunnelling).** Wall: two triangles in the plane `x = 0`, `y` −5..5, `z` 0..3, front
   facing −x. Capsule body, `r` = 0.5, feet at (−2, 0, 0), move (+10, 0, 0) in one call. Expect 1+ contacts, the
   first at `t = (2 − 0.5)/10 = 0.15`, normal (−1, 0, 0), point (−0.5, 0, 0). Stand-in today: the body ends beyond.
2. **Starting behind a one-sided wall passes.** Same wall, feet at (+1, 0, 0), move (−3, 0, 0): no contact. Set flag
   `0x2`: one contact, normal (+1, 0, 0), `t = (1 − 0.5)/3`.
3. **Moving away or along.** Feet at (−2, 0, 0), move (−1, 0, 0) or (0, 1, 0): no contact (`n·move ≥ −0.001`). Zero
   move: no contact.
4. **Low rule.** A vertical face 0.24 m tall (steepest-edge rise 0.24): no contact; 0.26 m: contact. Rise exactly 0.25:
   contact (`< 0.25` skips).
5. **Thin rule.** A vertical sliver: longest edge horizontal 4 m, third corner 0.2 m above it and its steepest edge
   rising 0.3 m (so the low rule passes): skipped. Same with the third corner 0.3 m off the line: kept. A triangle
   whose longest edge is vertical (`|unit z| ≥ 0.8`) is kept however thin.
6. **Wall threshold.** `n.z` = 0.65 is tested, 0.66 is not (capsule).
7. **Side uses the start.** A wall the sphere's centre is 1e-7 in front of (`s < 1e-6`) counts as behind: one-sided
   skipped, two-sided flipped (normal reversed in the contact).
8. **Disabled triangle.** Clear bit 0 (via `CollisionMesh_SetEnabledInBox`'s equivalent): no contact.
9. **Cells and visited bits.** A wall spanning four cells is reported once (one contact per triangle). A box whose
   max is past the grid clamps to the last cell.
10. **Sort.** Keys arriving 0.5, 0.6, 0.3 come out `[0.3, 0.5, 0.5]` (the smear); keys 0.3, 0.3 keep arrival order.
    `Contact_InsertSorted` of 0.2, 0.4, then 0.4 puts the new 0.4 after the old one; of 0.4 then 0.2 gives `[0.2, 0.4]`.
11. **Corner slide.** Vertical triangle `v0 = (0, 0, 0)`, `v1 = (0, 1, 0)`, `v2 = (0, 0, 1)` (normal +x; edge 2 runs
    down the wall's `y = 0` end), flags `bit0 | (2 << 14)`; it passes the low rule (edge 2 rises 1 m) and the thin rule
    (longest edge 1 not steep, `v0` 0.71 m from it). Grounded capsule body, `r` = 0.5, feet at `x` > 0.5, moving −x so
    that it meets the triangle's `y = 0` edge
    from `y < 0` (centre path at `y = −0.3`): one contact, region without bit 2, `S = (0, −1, 0) × max(0.5, |move|)`,
    `+0xb0 = 1`. Same with the owner airborne, or bits 14-15 = 3, or a face hit (centre path at `y = 0.5`): `S = 0`,
    `+0xb0 = 0`. The mirrored end (an up-running flagged edge at the other end) gives `+y`.
12. **Corner slide consumer.** In a full sweep pass, the pass velocity gains `S` (after removing any part against
    the move) and the reported velocity does not; a body with flag `0x8000` gains nothing.
13. **Sphere sweep options.** Same wall, sphere shape: `max` = 1 keeps only the nearest of two parallel walls; option
    `0x40` with the sphere behind keeps the face with `+0xb0 = 0x10`; option `0x20000` with two coplanar triangles hit
    keeps one; option 4 with min −0.65 drops a ceiling (`n.z` = −1).
14. **Overlap.** A sphere of `r` 0.5 at 0.3 from one wall and 0.1 from another, room ≥ 2: the call returns 1 and
    `out[0]` is the 0.4-deep wall. One-sided wall approached from behind: still found.
15. **Push-out.** Sphere `r` 0.5 at 0.3 m in front of a vertical wall: body `+0x50` gains `0.2 × n`, flag `0x1000` set.
    Two walls at 0.3 and 0.2: only the 0.2 one pushes (by 0.3). A wall sloped to `n.z` = 0.6: the push is
    `(n.x, n.y, 0) × (r − dist)`, shorter than `r − dist`. Sphere behind a one-sided wall: no push.

### Unit 3: Moving a body: sweep passes and contacts {#unit-3}

**What it is.** `PhysicsBody_Sweep`'s up-to-three passes and `PhysicsBody_ResolveContacts`' contact codes: where the
body is put, what is left of the move, how velocities slide, bounce and are written back, the push-outs, and the
airborne human's landing segment.

- **Coney:** `Human::sweep` (`src/human/human.cpp`) becomes this loop over Unit 2's contacts; the loose objects'
  bounce (`src/world_objects/loose_objects.*`) uses the same resolution.
- **Tests:** [Test ideas](#u3-tests), with a fake owner that answers fixed codes.

10 functions, 5148 bytes (4 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0033e278` | `PhysicsBody_Sweep` (tagged) | 1396 |
| `0x0033d9d8` | `PhysicsBody_ResolveContacts` (tagged) | 2208 |
| `0x0033d7b8` | `PhysicsVec_BackOffPlane` | 180 |
| `0x0033d870` | `Vec_RemoveNormalPart` (tagged) | 128 |
| `0x0033d8f0` | `PhysicsVec_Bounce` (tagged) | 228 |
| `0x0033d340` | `IPhysics_SweepBodyVsMesh` | 340 |
| `0x003414c0` | `AABB_ScaleAboutCentre` | 372 |
| `0x00341840` | `AABB_ExtendByMove` | 184 |
| `0x00341e18` | `PhysicsBody_GetContactScale` | 40 |
| `0x00342288` | `PhysicsHumanBody_GetContactScale` | 72 |

Notation: vectors are 4 floats; "xyz" operations leave `w` alone unless said. `a·b` is the xyz dot product.
Every "w = 1" below is the code writing 1.0 into `w` (it uses the VU constant register for it).

#### Shared records and calls {#u3-records}

**Owner vtable slots.** GCC 2.x vtables: each slot is 8 bytes, a 16-bit `this` adjustment then the function
pointer. The pages so far name a slot by either offset; this table gives both. The functions are the human's
(vtable `0x0053f088`). Confirmed (code): the slot offsets at the call sites cited, the human's entries read from
the vtable.

| Slot (adjust / function) | What the sweep uses it for | Human's function | Called at |
| --- | --- | --- | --- |
| `+0x20` / `+0x24` | type mask (bit `0x40` a human) | | `0x0033e070`, `0x0033e090` |
| `+0x50` / `+0x54` | state flags (64-bit); bit `0x4000000` airborne | | `0x0033d3f4`, `0x0033e0bc`, `0x00342288` |
| `+0x70` / `+0x74` | **set velocity** `(owner, &vec4)` | `0x0023cf00` | `0x0033e6b0`, `0x0033e760` |
| `+0x98` / `+0x9c` | **get the cached transform** (sret, 32 bytes: position, rotation): the copy the owner took at the start of its update (human `+0x10` / `+0x20`) | `0x004ed860` | `0x0033e3e0` |
| `+0xa8` / `+0xac` | **get the transform** (sret, 32 bytes) from the authoritative table `0x00714b00` | `0x004ed828` | `0x0033e5ec`, `0x0033dd4c` |
| `+0xb0` / `+0xb4` | **set the transform** `(owner, &transform32)`: position and rotation; the human also refreshes its cached copy while airborne and re-poses its task | `0x0021b048` | `0x0033e668`, `0x0033ddac` |
| `+0xd0` / `+0xd4` | **attribute** `(owner, n)` → float: 1 weight, 7 push weight, 8 restitution ([Attributes](#attributes)) | `0x00221260` | `0x0033e314`, `0x0033dbec`, `0x0033def4` |
| `+0xf0` / `+0xf4` | **get the physics body** (0: none) | `0x0021cf78` (returns human `+0x1a0`) | `0x0033dac8`, `0x0033db70` |
| `+0xf8` / `+0xfc` | **contact handler** `(owner, contact)` → answer code | `Human_OnContact` `0x00219d50` | `0x0033daec` |
| `+0x118` / `+0x11c` | **after the move** `(owner)` | `Human_PushOutInAir` `0x0021a490` | `0x0033e7b0` |

So the "owner that does not want them" of the current [Contacts](#contacts) text is wrong: slot `+0xf0`
returns the other owner's body, and a contact whose other owner has **no body** is skipped. Confirmed (code) at
`0x0033dac8` (the skip) and `0x0033db70`-`0x0033db78` (the same call's result read as a body: flags `+0x40`).

**Body vtable slots used here** (body vtable at body `+0x74`): `+0x28` / `+0x2c` first shape (called with extra
arguments `(1, 3, 0)` or `(0, 0, 0)` that `PhysicsBody_GetShapes` `0x00341ae0` ignores: it returns `+0x30`);
`+0x48` / `+0x4c` contact scale `(body, contact)`; `+0x50` / `+0x54` "you were pushed" `(otherBody, contact,
&push, &move)`. **Shape vtable** (shape `+0x34`): `+0x28` / `+0x2c` pose `(shape, owner)`. Confirmed (code) at
`0x0033e404`, `0x0033e1d8`, `0x0033dcb8`, `0x0033e670`.

**Body fields** this unit reads or writes ([Bodies](#bodies)): `+0x34` owner, `+0x40` flags, `+0x44`
type mask, `+0x50` pending push-out (vec4), `+0x60` scale (vec3), `+0x70` "could not move" counter, `+0x74` vtable.
Body flags read here: `0x1` sweep against the bodies too, `0x400` write the slid velocity back, `0x1000` do not
divide the push-out by the push weight, `0x8000` no corner slide, `0x80000000` dirty (set). Confirmed (code) at the
addresses in the functions below.

**Contact fields** used by this unit (0xc0 bytes, [Contacts](#contacts)):

| Offset | Meaning | Read / written here |
| --- | --- | --- |
| `+0x00` | own owner | written by the insert; read for attributes and the type mask |
| `+0x04` | own shape | re-posed after the body is moved |
| `+0x40` | other owner, 0 for the level | |
| `+0x44` | other shape, 0 for the level | the generic contact scale tests it |
| `+0x50` | unit normal, pointing out of the surface **towards the mover** | inferred from the code 4 push direction and the mesh sweep's "move goes into the face" test; confirmed (code) that the handlers treat it so |
| `+0x80` | fraction `f` of this pass's move at the contact | |
| `+0xa0` | slide vector (a displacement) | |
| `+0xb0` | contact flags: `0x1` add the slide to the move, `0x2` store the slide as the next push-out, `0x80` a human's landing | |
| `+0xb4` | mesh triangle (10-byte record, flags `u16` at `+0x06`), 0 for a body | |

**The move state** (locals of the sweep, one set per call, kept across the passes). Confirmed (code) at
`0x0033e3c0`-`0x0033e418`:

| Name | Start value | Meaning |
| --- | --- | --- |
| `V` | `v + push/dt/k` | the **move velocity**: what the passes move by; slid, bounced, gets corner slides |
| `v` | the caller's velocity | the **real velocity**: slid and bounced like `V`, written back to the owner |
| `moveFlags` | 0 | `0x1` a slide was stored in `+0x50` this move; `0x2` write `v` back; `0x4` a contact scale applies |
| `scale` | 0.0 | the largest contact scale met (with `0x4`) |
| `passes` | 0 | passes done, 0 to 3 |
| `found` | 0 | contacts found over all passes (all of them, including skipped and code 0 ones) |
| `last` | 0 | the last resolution's return |

#### PhysicsBody_Sweep {#u3-sweep}

`0x0033e278`, 0x574 bytes. `PhysicsBody_Sweep(float dt, IPhysics* phys, Body* body, const vec4* v, vec4* out)`;
returns nothing. `v` is only read; the velocity goes back through the owner. `out` receives the displacement
(sometimes not written at all, below). Confirmed (code) for every step unless marked.

1. **Move velocity** (`0x0033e2bc`-`0x0033e364`). `p = body+0x50 × (1/dt)` (xyz; `p.w` keeps `+0x5c`). Unless body
   flag `0x1000` is set, `p = p × (1 / owner.attribute(7))` (`0x0033e314`; the attribute call happens even when the
   result is not needed later). `V = v + p` (xyz, `w = 1`). The real velocity `v` starts as the caller's `v`,
   unchanged.
2. **Clear the push-out** (`0x0033e3a0`): body `+0x50 = (0, 0, 0, 1)` (the constant at `0x005116c0`), **always**,
   before the next test. So a push-out is used once, by the next sweep after it was added, even if that sweep does
   not move.
3. **Too slow to move** (`0x0033e39c`): if `|V|² ≤ 1e-6` (`0x358637bd`), return at once: nothing else happens (no
   dirty flag, no velocity write, `out` **not written**, no `+0x11c` call, `+0x70` unchanged).
4. **Set up** (`0x0033e3ac`-`0x0033e454`): set body flag `0x80000000` (dirty). `start` = the owner's **cached**
   transform (slot `+0x98`). `shape` = the body's first shape (body slot `+0x28`). If the shape is not enabled
   (shape flags `+0x32` bit `0x1` clear), skip the passes and go to step 6 with `passes = 0`, `found = 0`
   (`0x0033e428`): the body then moves freely by `V × dt`. Else `box` = the shape's world AABB
   (`PhysicsBody_ComputeBounds` `0x00341638`), computed **once** at the start position and not moved between passes.
5. **The passes** (loop at `0x0033e470`-`0x0033e5bc`). Each pass:
    1. `D = V × dt` (xyz; `dt` is the caller's, **never reduced**: the address of `dt` is handed to the resolution,
       which never reads it). If `|D|² ≤ 1e-6`, set `passes = 3` and leave the loop (`0x0033e4b8`, `0x0033e694`).
    2. Two boxes from `box` (AABB + the node fields `+0x20`): `qbox = AABB_ExtendByMove(box, D)`; and
       `sbox = AABB_ExtendByMove(AABB_ScaleAboutCentre(box, body+0x60), D)`. **`sbox` is never passed on**: both
       queries get `qbox` (`0x0033e518`, `0x0033e550`), and their box readers read only `+0x00`-`+0x18`
       (`0x00347170`, `0x0033feb0`). So body `+0x60` has no effect on which triangles or bodies are tested here (it
       does widen the capsule's mesh sphere, [Characters](characters.md#walls)). Confirmed (code).
    3. `count = 0`; `IPhysics_SweepBodyVsMesh(phys, &qbox, body, shape, contacts, sorted, &count, &D)`
       ([below](#u3-mesh)); then, if body flag `0x1` is set, the body sweep `0x0033d498` with the same arguments
       ([Unit 11](#unit-11): it adds its contacts to the same sorted list).
    4. `last = PhysicsBody_ResolveContacts(phys, body, sorted, count, &D, &V, &v, &dt, &moveFlags, &scale)`;
       `found += count`.
    5. If `last ≤ 0`, leave the loop. Else `passes += 1`; if `passes` is now 3, leave the loop; else next pass.

   The contact array holds 50 contacts (0xc0 each) and the sorted list 50 pointers, fresh for each pass (the count
   restarts at 0). Confirmed (code) for the layout (`sp+0x110`, `sp+0x2690`).
6. **After the passes** (`0x0033e5c4`):
    - `last < 0`: **return at once** (`0x0033e5c4`): no final move, no velocity write, `out` not written, no
      `+0x11c` call, `+0x70` unchanged. Whatever the aborting contact's code did (it may have moved the body and
      changed velocities, and `+0x50`) stays.
    - (Body or owner null: return; the start already called the owner, so this is not reachable in practice.)
    - `T` = the owner's transform (slot `+0xa8`), which includes what the resolutions moved.
    - **Moved** when `passes < 3` **or** `found == 0` (`0x0033e5f4`-`0x0033e604`): `T.position += V × dt` (xyz,
      `w = 1`), set the transform (slot `+0xb0`; the rotation is the one just read), re-pose the shape (shape slot
      `+0x28` with the owner), body `+0x70 = 0` (`0x0033e68c`).
    - **Stuck** otherwise (`passes == 3` and at least one contact was found): the body is **not** moved by the rest
      (it stays where the last resolution left it); `v.x = v.y = 0` (`v.z` kept) and the owner's velocity is set to
      `v` (slot `+0x70`) **whatever the body flags** (`0x0033e698`-`0x0033e6b0`); body `+0x70 += 1`.
    - **Contact scale** (`0x0033e6c4`): if `moveFlags & 0x4`: `v.x *= scale`, `v.y *= scale`, and `v.z *= scale` only
      when `v.z > 0`; the same on `V` (dead afterwards).
    - **Write-back** (`0x0033e734`): if body flag `0x400` **and** `moveFlags & 0x2`, set the owner's velocity to `v`.
    - `*out = T.position − start.position` (xyz, `w = 1`) (`0x0033e788`). `T` is the position just set (moved) or
      just read (stuck). `start` is the owner's cached copy, so `out` is the displacement since the owner's update
      began, not only this sweep's (they are the same for a human, who refreshes the copy before moving).
    - Call the owner's slot `+0x118` (`0x0033e7b0`).

So the three cases of "how far did it go" are (confirmed (code), the summary inferred):

| Pass result | Where the body ends |
| --- | --- |
| no contact or every contact answered 0 | start + `V × dt` |
| a contact stopped pass `k` (`k` = 1 or 2), and pass `k + 1` had no stopping contact | the advance of each stopping contact along its normal (below), + the slid `V × dt` in full |
| three passes each stopped, or the slid move fell under 1e-6² | the advances only; `v.xy` zeroed, counter up |

Note what this means: **the move is not shortened by the contact fraction**. Each pass sweeps the whole slid
`V × dt` again from wherever the body now is. A body that slides along a wall therefore travels its normal-direction
advance plus a full slid step in one update. Confirmed (code): `dt` is written once (`0x0033e2f0`) and re-read each
pass (`0x0033e5c0`); the resolution never reads the `dt` pointer (no use of its fourth register argument).

A body whose move is slower than `0.001 / dt` (0.03 m/s at 30 Hz) skips the passes and moves freely by `V × dt`
with `+0x70 = 0` (pass 1's `|D|² ≤ 1e-6` with `found = 0`). Confirmed (code); its effect is inferred.

A head-on hit whose slide leaves `V ≈ 0` ends the loop on the next pass's `|D|²` test with `passes = 3` and
`found > 0`: **stuck**, so the counter goes up each update a body is pressed straight into a wall. Confirmed (code);
this is what the human's "stuck in the air" rule reads ([Characters](characters.md#ground)).

#### PhysicsBody_ResolveContacts {#u3-resolve}

`0x0033d9d8`, 0x8a0 bytes. `int PhysicsBody_ResolveContacts(IPhysics* phys, Body* body, Contact** sorted, int count,
const vec4* D, vec4* V, vec4* v, float* dt /*unused*/, u32* moveFlags, float* scale)` (the last two on the stack).
Returns 0, 1 or −1. Confirmed (code) for all of it.

At the start, `u = D / |D|` (reciprocal square root; `D` is never zero here). Then the contacts are walked **in
sorted order** (nearest first; equal fractions keep their insertion order, [the insert](#u3-insert)):

1. **Skip** the contact (go to the next) when (`0x0033da80`-`0x0033dad4`):
    - it has a triangle (`+0xb4` ≠ 0) whose flags (`+0x06`) lack bit `0x1` (disabled since it was found); or
    - the moving body has no owner; or
    - it has another owner (`+0x40` ≠ 0) whose body (slot `+0xf0`) is null.
2. `answer = owner.onContact(contact)` (slot `+0xf8`). `code = answer & 0xffff`.
3. **Code 0** (`0x0033db04`): if `answer & 0x10000`, return 0; if `answer & 0x40000`, return −1; otherwise go on with
   the next contact. Nothing else: **`0x20000` with code 0 does nothing** (the scale rule is in the tail, which code
   0 never reaches).
4. **Codes 1 to 4** act ([below](#u3-codes)), then the tail. **Codes 5 and above** go straight to the tail.
5. **Tail** ([below](#u3-tail)), then: return **0** if `answer & 0x10000`; else **−1** if `answer & 0x40000`; else
   **1**. So any non-zero code ends the walk: later contacts of the pass are never seen.
6. All contacts walked (or `count ≤ 0`): return 0.

What the returns mean to the sweep: 1 "stopped, slide and try again", 0 "done, move the rest", −1 "abort".

#### The codes {#u3-codes}

Let `n` = contact `+0x50`, `f` = contact `+0x80`, and **the advance** be:

> `d = f × (D · n)`. If `d < −0.01`, the body's position (owner slot `+0xa8`) becomes `position + n × (d + 0.01)`,
> set back through slot `+0xb0` with its rotation unchanged, and the contact's own shape (`+0x04`) is re-posed
> (shape slot `+0x28` with the owner). Otherwise the body is not moved.

Because `n` points at the mover and `d` is negative, `n × (d + 0.01)` moves the body **towards the surface, along
the normal only**, by `|d| − 0.01`: it stops **0.01 m short, measured along the normal**, of where the swept shape
touched. The part of the move along the surface up to the contact is **not** applied (the next pass's slid move
covers the surface direction). −0.01 is `0xbc23d70a`, 0.01 `0x3c23d70a`. Confirmed (code) at `0x0033dcdc`-`0x0033ddc8`
(code 1), `0x0033ddec`-`0x0033dedc` (code 2), `0x0033df40`-`0x0033e02c` (code 3). The same formula is the stand-alone
`PhysicsVec_BackOffPlane` (below), which the resolution does not call. That this 0.01 is the 0.006-0.010 m
clearance measured on [Characters](characters.md#walls) is inferred (the walls page measured stops, not the code).

| Code | Effect, in order |
| --- | --- |
| 1 | the advance; then `Vec_RemoveNormalPart(V, n)` and `Vec_RemoveNormalPart(v, n)` |
| 2 | the advance; then `e = ownOwner.attribute(8)` and `PhysicsVec_Bounce(V, n, e)`; `e` is asked again and `PhysicsVec_Bounce(v, n, e)` |
| 3 | the advance; then `V = v = (0, 0, 0, 1)` |
| 4 | `other = otherOwner.body` (slot `+0xf0`). If `other.flags & 0x200`: exactly code 1. Else: `push = n × 0.5 × (D · n)` (the **whole** pass move `D`, not `f × D`), `push *= w_m / (w_o × k_o)` with `w_m` = own owner's attribute 1, `w_o` = other owner's attribute 1, `k_o` = other owner's attribute 7 (asked in that order); `other+0x50 += push` (xyz, `w = 1`); call `other`'s body slot `+0x50` with `(other, contact, &push, &D)`; then exactly code 1 (the advance and both slides) |

Confirmed (code): code 4 at `0x0033db60`-`0x0033dcd8`, the 0.5 is `0x3f000000` at `0x0033dbac`. "Own owner" is
contact `+0x00`, "other owner" contact `+0x40`.

#### The tail {#u3-tail}

Run for every code that is not 0. Confirmed (code) at `0x0033e058`-`0x0033e204`:

1. **Ask for the velocity write-back**: `moveFlags |= 0x2`, **except** when
    - the contact is with the level (`+0x40` = 0) and its triangle has flag `0x100` (bit 8; 20,231 of the disc's
      150,567 triangles, [Collision](collision.md#triangles)); or
    - the mover and the other owner are both humans (type mask `0x40`, slot `+0x20`) and **both** airborne (state
      `0x4000000`, slot `+0x50`).

   (A level contact must carry a triangle: the code reads `+0xb4` without a null test.)
2. **Contact flag `0x1`** (corner slide into the move): unless the mover is a human in any state of `0x18003ff0`
   (`Human_IsInPairOrBlock` `0x00227fd8`: grab, held, mug, tackle, throw, block...) or the body has flag `0x8000`:
   `s = contact+0xa0`, `Vec_RemoveNormalPart(s, u)` (drops the part of `s` that goes against the pass's direction),
   `V += s` (xyz, `w = 1`). **Only `V`**, not `v`. Then go to step 5: **the scale rule is skipped** (as it is when
   the human or `0x8000` test refuses).
3. Else, **contact flag `0x2`** (slide kept for the next move): `s = contact+0xa0`, `Vec_RemoveNormalPart(s, u)`;
   if `moveFlags & 0x1` is clear, set it and store `body+0x50 = s` (whole vec4). So only the **first** such contact
   of the move is kept, and since the sweep cleared `+0x50` before the passes, it survives as the next sweep's
   push-out (`s / dt / k` in the next move velocity).
4. **Scale rule** (flags `0x1` not set): if `answer & 0x20000`: `moveFlags |= 0x4`, and
   `*scale = max(*scale, body.contactScale(contact))` (body slot `+0x48`; `max.s` at `0x0033e1f4`). The **largest**
   scale met wins, starting from 0.
5. The return ([above](#u3-resolve), step 5).

Who sets the contact flags: the capsule's mesh sweep writes `0x1` when it built a corner slide (`0x00348360`,
`0x00348438`), the sphere and box mesh sweeps copy their hit's flags word (`0x003490f0`, `0x003489d0`), the landing
contact is `0x80` ([below](#u3-mesh)). Whether any sweep produces `0x2` is not traced. Confirmed (code) for
the stores cited.

#### PhysicsVec_BackOffPlane {#u3-backoff}

`0x0033d7b8`, 0xb4 bytes. `void PhysicsVec_BackOffPlane(vec4* p, const vec4* move, const vec4* n, float f)`:
`d = (move × f) · n`; if `d < −0.01`, `p.xyz += n × (d + 0.01)` and `p.w = 1`; else nothing. The same advance as the
resolution's codes 1-3 (which inline it). Its only caller is `Car_UpdateLoosePart` (`0x003881b4`). Confirmed (code).
The current plate comment's "moves the point back" describes it from the moved point's side; from the start
position, as the resolution uses it, it moves the point forward along `−n`.

#### Vec_RemoveNormalPart {#u3-remove}

`0x0033d870`, 0x80 bytes. `void Vec_RemoveNormalPart(vec4* v, const vec4* n)`: `d = v · n`; if `d < 0` (strictly),
`v.xyz −= n × d` and `v.w = 1`; else `v` is unchanged (its `w` too). `n` is assumed unit (no normalising).
Confirmed (code).

#### PhysicsVec_Bounce {#u3-bounce}

`0x0033d8f0`, 0xe4 bytes. `void PhysicsVec_Bounce(vec4* v, const vec4* n, float e)`: `d = v · n`, computed once.
If `d < 0`, `v.xyz −= n × d`. Then, **in both cases**, `v.xyz −= n × (d × e)`, `v.w = 1`. Confirmed (code) at
`0x0033d924`-`0x0033d9b4`. So:

- `d < 0` (going in): `v −= n d (1 + e)`, the reflection with restitution `e` (as the page says);
- `d ≥ 0` (already going out): `v −= n d e`: the outgoing normal speed is **reduced** to `(1 − e)` of itself. The
  current page's "when `v·n < 0`" misses this half.

#### IPhysics_SweepBodyVsMesh {#u3-mesh}

`0x0033d340`, 0x154 bytes. `void IPhysics_SweepBodyVsMesh(IPhysics* phys, const AABB* box, Body* body, Shape* shape,
Contact* contacts, Contact** sorted, int* count, const vec4* D)`. Confirmed (code):

1. **The shape's mesh sweep** through `0x0033d2d8(phys, &contacts[*count], box, body, shape, D)`, which picks the
   function from IPhysics `+0xe0 + 4 × shape.type` (shape `+0x30`, a byte) and passes it, besides those: a 1, the
   wall threshold **−0.65** (`0xbf266666`), a second float 0, and an option `4` when the body's type mask has `0x40`
   (a human; `(mask >> 4) & 4`), else 0 (`0x0033d2e0`-`0x0033d320`). No function for the type: no contact. It returns
   1 when the type's sweep reported any hit. The type's sweep writes its hits, nearest first (it sorts them,
   `0x00347300`), into consecutive contacts from `&contacts[*count]`; **only the first is inserted** (step 2), so
   **one mesh contact per pass, the nearest**. Each written contact has `+0x04` shape, `+0x44` 0, `+0x50` normal,
   `+0x80` fraction, `+0x90` the shape's position at the contact, `+0xa0` slide, `+0xb0` flags, `+0xb4` triangle
   (capsule: `0x00348410`-`0x003484e4`). The details are in [Unit 2](#u2-00347c08-physicsmesh-sweepcapsule).
2. **Insert it** (when step 1 found one): `Contact_InsertSorted(phys, &contacts[*count], sorted, count, ownOwner =
   body+0x34, otherOwner = 0, ownShape = shape, otherShape = 0)`.
3. **The landing segment**, only when the body's type mask has `0x40` **and** the owner's state has `0x4000000`
   (airborne) (`0x0033d3dc`-`0x0033d40c`): `Human_LandingTest(owner, contacts, count, D)` (`0x0023e408`, below)
   writes `contacts[*count]`; if it returns 1, set that contact's `+0xb0 = 0x80` and insert it like step 2. (The
   current text "when body flag `0x40` is set" on Characters is the **type mask** `+0x44`, not body flag `+0x40`.)

A mesh contact found in step 1 and a landing contact can both be in one pass's list; the landing one is written after
the first is inserted, so they never share a slot. Confirmed (code).

#### Human_LandingTest {#u3-landing}

`0x0023e408`, 0x2dc bytes. `int Human_LandingTest(Human* h, Contact* contacts, int* count, const vec4* D)`; returns 1
with a contact written at `contacts[*count]` (the count is **not** incremented here), else 0. Confirmed (code):

1. `A` = the feet: the position in the transform table (`0x00714b00 + 32 × h+0x92`).
2. **Raised feet**: when (`0x00227f68`(h) is true and the state lacks `0x1000000000`, a long fall) **or** the human
   holds with flag `0x800` (`0x00226660`): `A.z += 0.4 × Human_GetScale(h)` (`0x3ecccccd`), but at most
   `h+0x560 + 0.7` (`0x3f333333`).
3. `B` = the upper point, human vtable slot `+0x60` / `+0x64` (`0x00226a20`: the feet + `+0x4e8` − 0.16 × scale,
   [Characters](characters.md#falling)).
4. `A += D` (the moved, possibly raised, feet).
5. The ray goes from the **higher** of `A` and `B` to the lower: origin `B`, end `A`, unless `B.z < A.z`, then the
   two swap. `L` = the distance between them, direction = (end − origin) / `L`.
6. `WorldManager_RayCast(world 0x005147c4, {origin, dir, L}, &hit, 0, 0)` (mask 0: every enabled triangle).
7. No hit, or the hit normal's `z ≤ 0.65` (`0x3f266666`): return 0.
8. Else: human `+0x1d8` = the hit material, `+0x230` = the hit normal (vec4); and in the contact: `+0x04` = the
   body's first shape (body `h+0x1a0`, slot `+0x28` with `(0, 0, 0)`), `+0x50` = the hit normal, `+0x44` = 0,
   `+0xa0` = `(0, 0, 0, 1)`, `+0xb4` = the hit triangle, `+0xb0` = 0, `+0x84` = 0 (the point `+0x90` is not
   written), and the fraction, with `h0` = human vtable slot `+0x58` / `+0x5c` (1.0, `0x004ed818`):
   `f = 0` when the hit distance `< h0`, else `f = (dist − h0) / (L − h0)`. Return 1.

The fraction is measured on the segment **beyond 1.0 m from the origin**, not along `D`: with the origin 0.88 m above
the starting feet, `f` is `(g − 0.12) / (|D.z| − 0.12)` for a vertical fall with the feet `g` above the ground (worked
example in the tests). Confirmed (code) for the formula; the reading is inferred.

#### Contact_InsertSorted {#u3-insert}

`0x0033d718` (called by both queries). `(phys, Contact* c, Contact** sorted, int* count, ownOwner, otherOwner,
ownShape, otherShape)`: writes `c+0x00`, `+0x40`, `+0x04`, `+0x44`; finds the first entry whose fraction is
**strictly greater** than `c`'s, shifts it and the rest up one, puts `c` there (or at the end), `*count += 1`. So
equal fractions keep their insertion order: the mesh contact before a landing contact before body contacts at the
same fraction. It does **not** check the 50 limit. Confirmed (code).

#### AABB_ScaleAboutCentre {#u3-scale-box}

`0x003414c0`, 0x174 bytes. `void AABB_ScaleAboutCentre(AABB* box, const vec3* s)`: if `s.x`, `s.y` and `s.z` are
all exactly 1.0, nothing. Else, per axis, `c = (min + max) / 2`, `h = (max − min) / 2 × s`, and the new
`min = min(c + h, c − h)`, `max = max(c + h, c − h)` (so a negative scale still gives a valid box); `w = 1`.
Confirmed (code). In the sweep its result is unused ([above](#u3-sweep), step 5.2).

#### AABB_ExtendByMove {#u3-extend}

`0x00341840`, 0xb8 bytes. `void AABB_ExtendByMove(AABB* box, const vec4* d)`: `a = min + d`, `b = max + d`; new
`min = min(min, a, b)`, new `max = max(max, a, b)`, per axis (xyz). Confirmed (code).

#### PhysicsBody_GetContactScale {#u3-contact-scale}

`0x00341e18`, 0x28 bytes, body vtable `+0x48`. `float (Body*, const Contact* c)`: **1.0** when `c+0x44` (the other
shape) is non-zero, a body; **0.98** (`0x3f7ae148`) when it is 0, the level. Confirmed (code).

#### PhysicsHumanBody_GetContactScale {#u3-human-scale}

`0x00342288`, 0x48 bytes, human body vtable `+0x48`. `float (Body*, const Contact*)`: **0.25** (`0x3e800000`) when
the owner's state (slot `+0x50`) has `0x4000000` (airborne), else **1.0**; the contact is not read. Confirmed
(code).

Who answers `0x20000` ([Contacts](#contacts)): the human (level and objects; `0x20004` against a human in
the air), the world object on a floor (`0x20002`). So a world object sliding on a floor keeps 0.98 of its horizontal
speed per sweep (and of an upward `v.z`), written back because world-object bodies carry flag `0x400` (`0x80000500`,
[Bodies](#bodies)). An airborne human's contact scale is 0.25, but it reaches the human only when its
body has `0x400` (a player). Inferred from the code paths above.

#### Corrections to the current pages {#u3-corrections}

Each confirmed (code) at the address given:

- physics.md #contacts, code 1 "moves the body back to the contact": it **advances** the body along the normal to
  0.01 m short of the touch, only when it would go more than 0.01 m, and drops the move's surface-parallel part
  (`0x0033dcdc`). Codes 2 and 3 do the same, not "moves back".
- #contacts, `0x10000` "go on with the next contact (return 0)": it **ends** the walk and returns 0; going on with
  the next contact is code 0 without `0x10000` / `0x40000` (`0x0033db04`, `0x0033e1fc`).
- #contacts, `0x20000` "keeps the smallest contact scale": the **largest**, and only for a non-zero code whose
  contact has no flag `0x1` (`0x0033e1f4`, `0x0033e17c`).
- #contacts, "+0xb0 has bit 2 removes the normal part of its slide vector and stores it": flag `0x2` stores (once
  per move, `moveFlags 0x1`); flag `0x1` adds it to the move velocity instead; in both the part removed is the one
  against the **move's direction**, not the contact normal (`0x0033e148`, `0x0033e18c`).
- #contacts, `+0xf0` "owner that does not want them": the other owner's body; skipped when it has none.
- #sweep, "still blocked after three, the body stays, its horizontal velocity is zeroed (owner vtable `+0x74`)": the
  body stays where the **last pass's advance** put it, and the zeroed velocity is the slid `v` written whatever the
  body flags; the same happens when a slide leaves no move (`0x0033e4b8`).
- #sweep step 1, "scaled about its centre by body `+0x60` ... grown to cover the move": the scaled box is computed and
  discarded; the queries get the unscaled box grown by the move (`0x0033e518`).
- #sweep, "The velocity the slides leave is written back ... only when body flag `0x400` is set": and only when some
  stopping contact of the move asked for it (`moveFlags 0x2`: not a level triangle with flag `0x100`, not two
  airborne humans) (`0x0033e734`).
- #sweep / characters.md #ground, "the body's pending push-out (divided by dt)": also divided by the owner's
  attribute 7 unless body flag `0x1000` (`0x0033e2e4`).
- characters.md #falling, "run from `0x0033d340` when body flag `0x40` is set": the body's **type mask** `+0x44`
  bit `0x40` and the owner airborne (`0x0033d3dc`).
- `PhysicsVec_Bounce` also damps an outgoing normal speed by `(1 − e)` ([above](#u3-bounce)).

#### Test ideas (synthetic) {#u3-tests}

A fake owner (answers a fixed code per call, attributes 1 = 82, 7 = `k`, 8 = `e`; records every set-velocity,
set-transform and after-move call), a fake mesh query that returns a scripted contact per pass (normal, fraction,
a triangle with flags `0x0001` unless said, contact flags), no body query (body flag `0x1` clear). Start position
origin, `dt = 1/30`, push-out 0, `k = 1`, body flags `0x400` unless said. The values below follow from the spec
(computed by hand, float rounding aside).

1. **Head-on wall, code 1.** `v = (3, 0, 0)`, contact `n = (−1, 0, 0)`, `f = 0.5` on pass 1. `D = (0.1, 0, 0)`,
   `d = −0.05` → position `(0.04, 0, 0)`; `V = v = 0`; pass 2 `|D|² = 0` → stuck: velocity set to `(0, 0, 0)` (stuck
   branch) and again by the write-back; `+0x70` 0 → 1; `out = (0.04, 0, 0)`; one after-move call.
2. **Same, answer `0x10001`.** Same position `(0.04, 0, 0)`; resolution returns 0 → moved branch (by `V × dt = 0`);
   `+0x70` reset to 0; velocity `(0, 0, 0)` written once (write-back). Shows `0x10000` ends the walk without stuck.
3. **Same, answer `0x40001`.** Position `(0.04, 0, 0)` (the advance happened); no velocity call, `out` keeps its
   sentinel, `+0x70` unchanged, no after-move call.
4. **Wall at an angle.** `v = (3, 0, 0)`, `n = (−0.6, 0.8, 0)`, `f = 0.5`, code 1, pass 2 no contact. Advance
   `n × (−0.03 + 0.01)` = `(0.012, −0.016, 0)`; `V = v = (1.92, 1.44, 0)`; pass 2 `D = (0.064, 0.048, 0)`, no contact →
   end `(0.076, 0.032, 0)`; `+0x70 = 0`; velocity `(1.92, 1.44, 0)`; `out = (0.076, 0.032, 0)`. With body flags 0
   (an AI): the same position and **no** velocity call.
5. **Level triangle with flag `0x100`.** As test 4 with triangle flags `0x0101`: same position, **no** velocity call
   (write-back not asked).
6. **Corner.** `v = (3, 3, 0)`, pass 1 `n1 = (−1, 0, 0)`, `f = 0.5`; pass 2 `n2 = (0, −1, 0)`, `f = 0.5`; code 1 both.
   Pass 1: `D = (0.1, 0.1, 0)`, `d = −0.05` → `(0.04, 0, 0)`, `V = (0, 3, 0)`. Pass 2: `D = (0, 0.1, 0)` → `(0.04,
   0.04, 0)`, `V = 0`. Pass 3: `|D|² = 0` → stuck: `+0x70 = 1`, velocity `(0, 0, 0)`; `out = (0.04, 0.04, 0)`.
7. **Three-pass limit.** As test 4 but the fake returns the same contact (code 1, `f = 0.5`) on all passes. Pass 1 as
   test 4; passes 2 and 3: `D · n = 0` → no advance, `V` unchanged (`d = 0` is not `< 0`). After pass 3 → stuck:
   position stays `(0.012, −0.016, 0)` (the slid `V × dt` is **never** applied); the velocity written is the slid
   `v = (1.92, 1.44, 0)` with x and y zeroed, `(0, 0, 0)`; `+0x70 = 1`; `out = (0.012, −0.016, 0)`.
8. **Code 0 and `0x20000` alone.** Every contact answered `0x20000`: position `D = (0.1, 0, 0)`, no velocity call,
   `+0x70 = 0`, no scale.
9. **Push-out.** `v = 0`, `+0x50 = (0.02, 0, 0)`, no contacts: position `(0.02, 0, 0)`, `+0x50` reads `(0, 0, 0, 1)`
   afterwards, no velocity call. With `k = 2`: `(0.01, 0, 0)`. With `k = 2` and body flag `0x1000`: `(0.02, 0, 0)`.
10. **Too slow.** `v = (0.0005, 0, 0)`, `+0x50 = (0.00001, 0, 0)`: `V = (0.0008, 0, 0)`, `|V|² = 6.4e-7` → nothing
    moves, `+0x50` cleared,
    dirty flag **not** set, `out` keeps its sentinel, no after-move call.
11. **Slow but above the cut.** `v = (0.02, 0, 0)`, a contact scripted for pass 1: `|V|² = 4e-4 > 1e-6` but
    `|D|² ≈ 4.4e-7 ≤ 1e-6` → no query is made (the fake records no call), position `(0.000667, 0, 0)`, `+0x70 = 0`.
12. **Bounce on a floor (world object).** Body type mask 0, flags `0x80000500`, `e = 0.1`, `v = (1, 0, −5)`, floor
    `n = (0, 0, 1)`, `f = 0.4`, answer `0x20002`, pass 2 no contact. `D = (0.0333, 0, −0.1667)`, `d = −0.0667` →
    `z = −0.0567`; `v = V = (1, 0, 0.5)`; pass 2 `D = (0.0333, 0, 0.0167)` → end `(0.0333, 0, −0.04)`; scale 0.98
    (level contact) → velocity written `(0.98, 0, 0.49)`.
13. **Bounce going out.** `PhysicsVec_Bounce((0, 0, 2), (0, 0, 1), 0.1)` → `(0, 0, 1.8)`;
    `PhysicsVec_Bounce((0, 0, −2), (0, 0, 1), 0.1)` → `(0, 0, 2.2)`.
14. **Scale, `v.z` rule.** Test 4 with answer `0x20001`, a generic body (type mask 0) and the starting
    `v = (3, 0, −1)`: the slide leaves `v = (1.92, 1.44, −1)`; the write-back is `(1.8816, 1.4112, −1)` (`v.z < 0`
    kept). With `v = (3, 0, 1)`: `(1.8816, 1.4112, 0.98)`. The same with a human-body fake airborne (state
    `0x4000000`): scale 0.25, `(0.48, 0.36, ·)`; on the ground 1.0. With contacts at 0.98 and 1.0 in one move: 1.0
    (the largest). The position is not affected by the scale.
15. **Code 4 push.** Mover `v = (3, 0, 0)`, contact with another owner (body flags 0, `+0x50 = 0`), `n = (−1, 0, 0)`,
    `f = 0.5`, attributes 1 = 82 for both, other `k = 1`. Other's `+0x50 = (0.05, 0, 0, 1)` (`0.5 × (−0.1) × n`),
    its slot `+0x50` called once with that push and `D`; mover as test 1 (position `(0.04, 0, 0)`). Other `k = 1e9`:
    push `≈ 5e-11`. Other body flag `0x200`: no push, no call, mover as test 1.
16. **Corner slide (contact flag `0x1`).** Test 1 with contact flags `0x1`, `+0xa0 = (0, 0.5, 0)`: after code 1,
    `V = (0, 0.5, 0)`, `v = 0`; pass 2 `D = (0, 0.01667, 0)`, no contact → end `(0.04, 0.01667, 0)`, `+0x70 = 0`,
    velocity `(0, 0, 0)`; no scale even with `0x20001`. With `+0xa0 = (−0.5, 0.5, 0)`: the `−0.5` along `−u` is
    removed first → same result. With body flag `0x8000`: `V = 0` → stuck as test 1.
17. **Stored slide (contact flag `0x2`).** Test 4 with flags `0x2`, `+0xa0 = (0, 0.05, 0)`: same position and
    velocity; afterwards body `+0x50 = (0, 0.05, 0, ·)`; a second flag-`0x2` contact in pass 2 does not overwrite it.
18. **Landing segment fraction.** Human, feet at `z = 0.3`, upper point 0.88 m above, `D = (0, 0, −0.5)`, ground plane
    `z = 0`, `n = (0, 0, 1)`: `L = 1.38`, hit at 1.18 → `f = 0.18 / 0.38 = 0.4737`, contact flags `0x80`; with
    code 1 the advance is `z += 0.4737 × (−0.5) + 0.01` → feet at `0.0732`. Feet at `z = 0.05`, `D = (0, 0, −0.2)`:
    hit at 0.93 < 1.0 → `f = 0`, no advance. A ground normal `z = 0.6`: no contact.
19. **Insert order.** Fractions 0.5, 0.2, 0.5 inserted in that order → sorted 0.2, first 0.5, second 0.5.
20. **AABB helpers.** Scale `[(0,0,0),(2,2,2)]` by `(1.5, 1, −1)` → `[(−0.5, 0, 0), (2.5, 2, 2)]`; by `(1,1,1)`
    unchanged. Extend `[(0,0,0),(1,1,1)]` by `(2, −1, 0)` → `[(0, −1, 0), (3, 1, 1)]`.

### Unit 4: Static shape-pair overlaps {#unit-4}

**What it is.** The static overlap of two shapes (the overlap table at IPhysics `+0x18`), the segment kernels, and how
`IPhysics_OverlapShape` turns a pair's answer into a contact. Its callers are the push sphere, pick-ups and an
object touched by a human; **strikes do not use it** ([Who reaches a car](#who-reaches-a-car)).

- **Coney:** `src/world_objects/object_bodies.*` (today a sphere-against-body push of its own) and the push sphere.
- **Tests:** [Test ideas](#u4-test-ideas-synthetic-shapes-expected-val), one per quirk the original has.

19 functions, 6044 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00345d58` | `PhysicsOverlap_SphereSphere` | 124 |
| `0x00345dd8` | `PhysicsOverlap_BoxBox` | 1452 |
| `0x00346388` | `PhysicsOverlap_CapsuleCapsule` | 372 |
| `0x00346500` | `PhysicsOverlap_SegmentSegment` | 384 |
| `0x00346680` | `PhysicsOverlap_BoxSphere` | 388 |
| `0x00346808` | `PhysicsOverlap_CapsuleBox` | 1292 |
| `0x00346d18` | `PhysicsOverlap_BoxCapsule` | 156 |
| `0x00346db8` | `PhysicsOverlap_SphereBox` | 36 |
| `0x00346de0` | `PhysicsOverlap_SegmentSphere` | 208 |
| `0x00346eb0` | `PhysicsOverlap_SphereSegment` | 36 |
| `0x00346ed8` | `PhysicsOverlap_CapsuleSphere` | 184 |
| `0x00346f90` | `PhysicsOverlap_SphereCapsule` | 36 |
| `0x00346fb8` | `PhysicsOverlap_SegmentCapsule` | 396 |
| `0x00347148` | `PhysicsOverlap_CapsuleSegment` | 36 |
| `0x0034a2d0` | `CapsuleBox_CornerCircle` | 68 |
| `0x0034a6a0` | `CapsuleBox_SideInterval` | 44 |
| `0x0034ac08` | `Segment_ClosestParam` | 192 |
| `0x0034acc8` | `Segment_ClosestPoint` | 132 |
| `0x0034ad50` | `Segment_ClosestPoints` | 508 |

#### Summary for the implementer {#u4-summary-for-the-implementer}

- **Calling convention** (confirmed (code) at `0x0033ebb8`): every overlap entry is
  `bool fn(Contact* c, Shape* own, Shape* other)`, `own` being the querying (row) shape. It returns 1 on overlap, 0
  otherwise.
- **Touching counts as overlap** in every test except the capsule-capsule horizontal test (strict) and the box-box
  test (inclusive; it separates only on a strict `>`). Each function below says which.
- **Most tests write nothing into the contact.** Only three do: capsule-capsule (`+0x10`, `+0x50`, `+0x88`),
  capsule-box (`+0x50`, `+0x88`) and its mirror box-capsule (`+0x10` and whatever was in `+0x10`, swapped into
  `+0x50`). **No overlap test writes the point `+0x90`, the fraction `+0x80` or the slide `+0xa0`.**
- **New contact field `+0x88`**: a penetration depth, written only by the two tests above (metres, except a capsule-box
  corner, where it is a squared quantity, m²; see [CapsuleBox](#u4-physicsoverlap-capsulebox-00346808)).
- **Half normals** (inferred from the three writers and the mirrors): each 0x40-byte half has a normal at its `+0x10`
  that points from **that half's shape towards the other one**. So `+0x50` (second half) is the other shape's outward
  normal, pointing at the querying shape, and `+0x10` points the other way. A mirrored entry swaps the whole halves,
  which keeps that meaning.
- **Strikes do not use these functions against cars.** `Human_TestStrikes` (`0x0033f110`) calls the **sweep** table
  (IPhysics `+0x7c`), not the overlap table: a sphere strike reaches a box through `PhysicsSweep_SphereBox`
  (`0x003453f8`), and a segment strike through `PhysicsSweep_SegmentBox` (`0x00345588`), which always returns 0. So
  **only the sphere strike shapes can hit a car**: the hands (bones 19, 25, radius 0.09), the head (6, 0.15), the
  feet (30, 33, 0.15) and a held weapon's strike spheres. The forearms (18, 24), the shins (29, 32) and the spine (3)
  never do. Confirmed (code); see [Who reaches a car](#who-reaches-a-car). The car's contact point therefore comes
  from the sphere-box **sweep** ([Unit 5](#unit-5)), not from this one.

#### How `IPhysics_OverlapShape` fills a contact {#overlapshape}

`IPhysics_OverlapShape(phys, out, query)` (`0x0033ebb8`) returns the number of contacts copied to `out`. Confirmed
(code) at `0x0033ebb8` unless marked.

The query record:

| Offset | Meaning |
| --- | --- |
| `+0x00` | the querying body |
| `+0x04` | the querying shape (`own`) |
| `+0x08` | a box to gather candidates with, used instead of the shape's own world box when option `0x400` is set |
| `+0x10` | options: `0x1` the level mesh, `0x2` bodies, `0x10` use the type mask at `+0x14`, `0x80` skip the same non-zero group (body `+0x3c`), `0x100` ask the querying owner (owner vtable `+0x110`/`+0x114` with the other owner; 0 skips), `0x200` only shapes with flag `0x1` (enabled), `0x400` the box at `+0x08` |
| `+0x14` | type mask (−1, any, unless option `0x10`) |
| `+0x18` | the most contacts to return (0: no body is tested at all) |
| `+0x28`, `+0x2c` | two thresholds passed to the mesh overlap (`+0xf4`) |

Steps:

1. The candidate box is the querying shape's world bounds (`PhysicsBody_ComputeBounds` on `+0x04`), or `+0x08`
   with option `0x400`. With option `0x2`, the candidates are `IPhysics_QueryBox(phys, list, box, body,
   body's owner, typeMask, 0)` ([Queries](#queries)): bodies whose world box overlaps it in x, y and z, the
   querying owner's own bodies excluded. **This box test is the only vertical test that a capsule-box pair gets**
   (see [CapsuleBox](#u4-physicsoverlap-capsulebox-00346808)). Inferred from the two functions together.
2. For each candidate (after the `0x80` and `0x100` filters), its shape chain is fetched with body vtable `+0x2c`
   called with `(2, 4, querying owner)`; a human body answers its capsule chain (`+0x30`, `0x003420c8`). Every shape
   of the chain (only enabled ones with `0x200`) is tested with `overlap[own.type][other.type]` (IPhysics
   `+0x18 + own.type × 0x14 + other.type × 4`; an empty entry is skipped).
3. A test writes into the next free 0xc0-byte slot of a 50-slot stack buffer. **The slot is not cleared first**: any
   field the pair function does not write holds whatever the stack held. On a hit: `+0xb4` (mesh triangle) = 0, then
   `Contact_InsertSorted` (`0x0033d718`) sets `+0x00` = querying body's owner (body `+0x34`), `+0x04` = `own`,
   `+0x40` = other body's owner, `+0x44` = the other shape, and inserts the slot into the list in ascending order of
   `+0x80`, before the first entry whose `+0x80` is strictly greater (equal keys keep discovery order).
4. Because no overlap test writes `+0x80`, **the order of body contacts is undefined in the original** (sorted by
   stack leftovers). Inferred from 1-3. None of the four callers depends on it for more than which of several
   overlapping bodies is tried first (see below); Coney should keep discovery order (the candidates' x-sorted list
   order, then the other body's shape-chain order) and say so in a comment.
5. The first `min(count, max)` contacts are copied to `out` (0xc0 bytes each). There is no check that more than 50
   hits fit the buffer (inferred: never reached in play).
6. If fewer than `max` were copied and option `0x1` is set, the shape type's mesh overlap (`+0xf4`) appends up to
   `max − count` more after them, unsorted against the body contacts; only the sphere has one that can hit
   ([Mesh tests](#mesh-tests)).

The callers, and what they read (confirmed (code) at each):

| Caller | Querying shape | Options, type mask, max | Pairs it can reach | Reads from each contact |
| --- | --- | --- | --- | --- |
| `PhysicsHumanBody_PushAwayHumans` `0x003425c0` | the 2.0 m push sphere (`+0x84`) | `0x2012`, `0x40` (humans), 32 | sphere → the other humans' capsules: **SphereCapsule** | `+0x40` only (the push direction comes from the two humans' positions, not the contact) |
| `Human_FindPickupNear` `0x002193e4` | the human's capsule | `0x12`, `8` (objects), 32 | **CapsuleBox**, CapsuleSphere | `+0x40` only |
| `WorldObject_TryTouchPickUp` `0x003955cc` | the object's first shape | `0x12`, `0x40` (humans), 24 | **BoxCapsule**, SphereCapsule | `+0x40` only; the first human that sees it and can pick it up takes it |
| `Climb_IsSpaceClear` `0x00281150` | a temporary sphere of the capsule radius | `0xd` (mesh only, no bodies), max 1 | none of this unit (sphere-mesh) | the count |

So no current caller reads the normal or depth an overlap test writes. The writes are specified anyway because
Coney's contacts should match the record.

#### The shape fields read {#u4-the-shape-fields-read}

| Type | Fields the overlap tests read |
| --- | --- |
| 1 box | `+0x40`/`+0x44`/`+0x48` half extents h0, h1, h2; `+0x50`, `+0x60`, `+0x70` axes A0, A1, A2 (x, y, z; w taken as 0); `+0x80` centre P (w 1) |
| 2 sphere | `+0x40` radius r; `+0x50` centre C |
| 3 capsule | `+0x40` radius r; `+0x44` height h; `+0x50` base B (x, y, z); `+0x58` is base z |
| 4 segment | `+0x40` start S; `+0x50` end E; `+0x60` radius r (`+0x64` length is not read: the direction is E − S) |

#### The kernels {#u4-the-kernels}

#### Segment_ClosestParam (0x0034ac08) {#u4-segment-closestparam-0034ac08}

Size 0xc0. `void Segment_ClosestParam(float* t, const Vec4* p0, const Vec4* d, const Vec4* q)`: the parameter of the
point of the segment `p0 + t·d`, `t ∈ [0, 1]`, nearest `q`. Confirmed (code) at `0x0034ac08`:

```text
k = (q − p0) · d          // xyz dot
t = 0
if k > 0:
    t = (d·d < k) ? 1 : k / (d·d)
```

Edge cases: a degenerate segment (`d = 0`) gives `k = 0`, so `t = 0` (no division). `k` exactly equal to `d·d`
takes the division and gives 1.

#### Segment_ClosestPoint (0x0034acc8) {#u4-segment-closestpoint-0034acc8}

Size 0x84. `void Segment_ClosestPoint(Vec4* out, const Vec4* p0, const Vec4* d, const Vec4* q)`: `out = p0 + t·d`
with `t` from `Segment_ClosestParam` on the same arguments; `out.w = 1`. Confirmed (code) at `0x0034acc8`.

#### Segment_ClosestPoints (0x0034ad50) {#u4-segment-closestpoints-0034ad50}

Size 0x1fc. `void Segment_ClosestPoints(float* s, float* t, const Vec4* d1, const Vec4* d2, const Vec4* w)`, with
`w = p1 − p2` (the two starts): the parameters of the closest points `p1 + s·d1` and `p2 + t·d2`, both in [0, 1].
It is the standard clamped solution (the form published by Dan Sunday), with one threshold 1e-6 (`0x358637bd`).
Confirmed (code) at `0x0034ad50`:

```text
a = d1·d1;  b = d1·d2;  c = d2·d2;  d = d1·w;  e = d2·w;  D = a·c − b²
if D < 1e-6:          sN = 0;  sD = 1;  tN = e;  tD = c          // parallel (or degenerate)
else:
    sN = b·e − c·d;  tN = a·e − b·d;  sD = tD = D
    if sN < 0:        sN = 0;  tN = e;  tD = c
    elif sN > sD:     sN = sD; tN = e + b; tD = c
if tN < 0:
    tN = 0
    if −d < 0: sN = 0          elif −d > a: sN = sD       else: sN = −d;  sD = a
elif tN > tD:
    tN = tD
    if b − d < 0: sN = 0       elif b − d > a: sN = sD    else: sN = b − d; sD = a
s = (|sN| < 1e-6) ? 0 : sN / sD
t = (|tN| < 1e-6) ? 0 : tN / tD
```

(When `sN` is set to `sD` in the last two branches, `s` comes out 1.) Edge cases, confirmed (code), consequences
inferred:

- **The parallel test is absolute**, not relative to the lengths: two short segments are treated as parallel when
  `a·c·sin²θ < 1e-6`. For two forearms (length 0.20 m) that is θ < 1.4°; for a forearm against a shin (0.40 m),
  θ < 0.7°. Parallel means `s = 0` and `t` from the start of the first segment, then the usual clamping.
- **A degenerate second segment** (`d2 = 0`) gives `b = c = e = 0`, so `D = 0`, `tN = 0`, and the `tN` branches are
  not taken: **`s` stays 0** whatever the first segment's direction. The first segment is then measured from its start
  only, not from its nearest point. (A degenerate first segment is handled correctly: `s = 0`, `t` clamped from `e/c`.)
- Both zero: `s = t = 0`, no division.

#### CapsuleBox_CornerCircle (0x0034a2d0) {#u4-capsulebox-cornercircle-0034a2d0}

Size 0x44. `bool CapsuleBox_CornerCircle(float x, float y, float r, float hx, float hy, float* depth)` with the point
already mirrored so that `x < −hx` and `y < −hy`: the circle of radius `r` at `(x, y)` against the corner
`(−hx, −hy)`. Confirmed (code) at `0x0034a2d0`:

```text
q = (x + hx)² + (y + hy)² − r²
if q <= 0: *depth = −q (= r² − distance²); return 1
return 0
```

Touching (q = 0) is a hit with depth 0. **The depth is a squared length** (m²), not a distance.

#### CapsuleBox_SideInterval (0x0034a6a0) {#u4-capsulebox-sideinterval-0034a6a0}

Size 0x2c. `bool CapsuleBox_SideInterval(float x, float y, float r, float hx, float hy, float* depth)` with `x < −hx`
and `|y| ≤ hy`: the circle against the face `x = −hx`. Only `x`, `r`, `hx` are used. Confirmed (code) at
`0x0034a6a0`:

```text
s = x + hx + r            // r minus the gap to the face
if s >= 0: *depth = s; return 1
return 0
```

Touching (gap = r) is a hit with depth 0.

#### The pair functions {#u4-the-pair-functions}

#### PhysicsOverlap_SphereSphere (0x00345d58) {#u4-physicsoverlap-spheresphere-00345d58}

Table (2, 2). Size 0x7c. Reads sphere `+0x40` r, `+0x50` C of both. Writes nothing. Confirmed (code) at
`0x00345d58`:

> hit when `|C_own − C_other|² ≤ (r_own + r_other)²` (xyz).

Touching is a hit. Coincident centres are a hit.

#### PhysicsOverlap_BoxBox (0x00345dd8) {#u4-physicsoverlap-boxbox-00345dd8}

Table (1, 1). Size 0x5ac. Reads both boxes' half extents, axes and centres. Writes nothing. Confirmed (code) at
`0x00345dd8`.

It is the 15-axis separating-axis test (the three axes of each box, then the nine cross products `A_i × B_j`, in the
usual Gottschalk form, with no epsilon added to the absolute matrix). With `a`, `b` the half extents of `own` (A)
and `other` (B), `t[k] = A_k · (P_B − P_A)` and a 3 × 3 matrix `R`, the boxes are separated when any of these holds
(strict `>`, so touching boxes overlap):

```text
for k in 0..2:  |t[k]|                     > a[k] + Σ_m b[m]·|R[k][m]|
for m in 0..2:  |Σ_k t[k]·R[k][m]|         > Σ_k a[k]·|R[k][m]| + b[m]
for i, j in 0..2 (i1 = i+1, i2 = i+2, j1 = j+1, j2 = j+2, mod 3):
                |t[i2]·R[i1][j] − t[i1]·R[i2][j]|
                                           > a[i1]·|R[i2][j]| + a[i2]·|R[i1][j]| + b[j1]·|R[i][j2]| + b[j2]·|R[i][j1]|
```

The order of the tests does not change the answer.

**The quirk: how `R` is built.** A correct test uses `R[k][m] = A_k · B_m`. The original computes

```text
R[k][m] = Σ_{n=0..3} A_n[k] · B_n[m]        // A_3 = P_A, B_3 = P_B: the centres take part
```

that is, the transpose-product the other way round, over all four rows of each box's matrix, centres included.
Confirmed (code) at `0x00345eb8`-`0x00345ee8` (the loop broadcasts the columns of A against the rows of B). For two
axis-aligned boxes with A at the origin it gives the right answer; otherwise it does not. Example (computed from the
spec): two axis-aligned boxes of half extent 1 at (100, 0, 0) and (101.5, 0, 0) overlap by 0.5 m, but `R[0][0] =
1 + 100 × 101.5 = 10151` and the B-axis test `|1.5 × 10151| > 10151 + 1` reports them separated, so the function
returns 0. **No caller pairs two boxes** (inferred: the four callers of `0x0033ebb8` query with a sphere, a capsule
or one object's shape against humans, whose chain is the capsule), so the quirk never shows in play. Coney should
reproduce it behind a comment and a test, or, if the maintainers prefer, implement the correct test and record the
difference: it has no player-visible effect.

#### PhysicsOverlap_CapsuleCapsule (0x00346388) {#u4-physicsoverlap-capsulecapsule-00346388}

Table (3, 3). Size 0x174. Reads `+0x40` r, `+0x44` h, `+0x50` B of both (`+0x58` = base z). Confirmed (code) at
`0x00346388`. Both capsules are **upright cylinders** here (flat ends):

1. Vertical: the ranges `[B_own.z, B_own.z + h_own]` and `[B_other.z, B_other.z + h_other]` must overlap, inclusive
   (`B_own.z ≤ B_other.z + h_other` and `B_other.z ≤ B_own.z + h_own`). Otherwise 0.
2. Horizontal: `d = B_other − B_own` with `d.z = 0`; hit only when `|d|² < (r_own + r_other)²` (**strict**:
   touching side by side is not a hit).
3. On a hit it writes:
   - `+0x10` (own half) = `d / |d|`, w = 1: the horizontal unit vector from `own` to `other`. When `|d|² < 1e-6`
     (`0x358637bd`, coincident axes) it is the constant **(0, 1, 0, 1)** (`0x00511730`) instead.
   - `+0x50` (other half) = −`+0x10` in xyz, w = 1: pointing from `other` to `own`.
   - `+0x88` = `(r_own + r_other) − |d|` (horizontal penetration, metres; with the constant normal, the full sum of
     the radii).

The vertical overlap depth is not computed. `+0x80`, `+0x90` are not written.

#### PhysicsOverlap_SegmentSegment (0x00346500) {#u4-physicsoverlap-segmentsegment-00346500}

Table (4, 4); also the sweep table's (4, 4) through `0x00344300` (strikes, below). Size 0x180. Reads `+0x40` S,
`+0x50` E, `+0x60` r of both. Writes nothing. Confirmed (code) at `0x00346500`:

```text
d1 = E_own − S_own;  d2 = E_other − S_other;  w = S_own − S_other
(s, t) = Segment_ClosestPoints(d1, d2, w)
hit when |w + s·d1 − t·d2|² ≤ (r_own + r_other)²
```

Touching is a hit. The kernel's two edge cases (absolute parallel threshold; a zero-length `other` measured from
`own`'s start) apply.

#### PhysicsOverlap_BoxSphere (0x00346680) {#u4-physicsoverlap-boxsphere-00346680}

Table (1, 2). Size 0x184. Reads the box (own) half extents, axes, centre and the sphere's r, C. Writes nothing.
Confirmed (code) at `0x00346680`:

```text
for k in 0..2:  x[k] = A_k · (C − P)                    // the centre in the box's frame
dist² = Σ_k (x[k] < −h[k] ? (x[k] + h[k])² : x[k] > h[k] ? (x[k] − h[k])² : 0)
hit when dist² ≤ r²
```

A centre inside the box gives 0, so it always hits. Touching is a hit. The box is treated as a true 3D box.

#### PhysicsOverlap_SphereBox (0x00346db8) {#u4-physicsoverlap-spherebox-00346db8}

Table (2, 1). Size 0x28. Mirror: calls `PhysicsOverlap_BoxSphere(c, other, own)` and returns its result. **No halves
are swapped** (there is nothing written to swap). Confirmed (code) at `0x00346db8`.

#### PhysicsOverlap_CapsuleBox (0x00346808) {#u4-physicsoverlap-capsulebox-00346808}

Table (3, 1). Size 0x50c. Reads the capsule's `+0x40` r and `+0x50` B (x, y; **not** its height `+0x44` or base z),
the box's half extents, axes (including each axis's z, `+0x58`/`+0x68`/`+0x78`) and centre. Writes `+0x50` and
`+0x88`. Confirmed (code) at `0x00346808` and the two kernels.

**It is a 2D test** in the box's horizontal plane: a circle against a rectangle. Neither the capsule's vertical range
nor the box's vertical extent is read, so two shapes far apart in height overlap if their footprints do; only the
query's box gather (x, y and z, [above](#overlapshape)) keeps them apart vertically. Inferred consequence.

1. **The vertical axis**: `u` = the box axis with the largest `|A_k.z|` (first one wins a tie). The two in-plane axes
   are `i = (u + 1) mod 3` and `j = (i + 1) mod 3`, with half extents `hx = h[i]`, `hy = h[j]`.
2. `d = B − P` with `d.z = 0`; `x = d · A_i`, `y = d · A_j` (full 3D dot products with the zeroed d).
3. The nine regions (r = capsule radius):

| Region | Test | Normal written to `+0x50` | `+0x88` |
| --- | --- | --- | --- |
| `x < −hx`, `abs(y) ≤ hy` | `SideInterval(x, y, r, hx, hy)` | **−A_i**, w 1 | `x + hx + r` |
| `x > hx`, `abs(y) ≤ hy` | `SideInterval(−x, y, r, hx, hy)` | **+A_i**, w 1 | `−x + hx + r` |
| `abs(x) ≤ hx`, `y < −hy` | `SideInterval(y, x, r, hy, hx)` | **−A_j**, w 1 | `y + hy + r` |
| `abs(x) ≤ hx`, `y > hy` | `SideInterval(−y, x, r, hy, hx)` | **+A_j**, w 1 | `−y + hy + r` |
| `x < −hx`, `y > hy` | `CornerCircle(x, −y, r, hx, hy)` | normalize(`(x + hx)·A0 + (y − hy)·A1`) | `r² − dist²` |
| `x > hx`, `y < −hy` | `CornerCircle(−x, y, r, hx, hy)` | normalize(`(x − hx)·A0 + (y + hy)·A1`) | `r² − dist²` |
| `x > hx`, `y > hy` | `CornerCircle(−x, −y, r, hx, hy)` | normalize(`(x − hx)·A0 + (y − hy)·A1`) | `r² − dist²` |
| `x < −hx`, `y < −hy` | `CornerCircle(x, y, r, hx, hy)` | normalize(`(x − hx)·A0 + (y − hy)·A1`) (**quirk**) | `r² − dist²` |
| `abs(x) ≤ hx`, `abs(y) ≤ hy` (centre over the box) | always a hit | if `x < y` (signed): `A_j`, negated when `y < 0`; else `A_i`, negated when `x < 0`; w 1 | `hy − abs(y) + r` or `hx − abs(x) + r`, for the axis chosen |

The boundaries: "`x < −hx`" and "`x > hx`" are strict, so `x = ±hx` is in the middle column (the same for y).
A failed kernel returns 0 at once.

Quirks, each confirmed (code) at `0x00346808`:

- **The (−, −) corner's normal is built from the opposite corner** `(+hx, +hy)`: `(x − hx, y − hy)` instead of
  `(x + hx, y + hy)`. It still points into the (−, −) quadrant, so it is outward, but skewed. The other three corners
  are right. The hit test and depth are right for all four.
- **Corner normals always use axes 0 and 1** (`A0`, `A1`), whatever `i` and `j` are, and the in-plane offset is put
  in their x and y slots with z = 0. Correct only when `u = 2` (the box's third axis is the most vertical), which is
  the case for every box posed from an upright owner (cars, objects at rest); inferred.
- The corner normal is normalized in xyz (`vrsqrt`); its w is not set (stack leftover). Face normals have w = 1.
- **The corner depth is squared** (m²), the face depth is in metres.
- **Over the box** the face is chosen by comparing the signed coordinates `x < y`, not by the nearest face: a capsule
  at `x = −1.5, y = 0.1` over a box with `hx = 2, hy = 1` gets `+A_j` and depth `1.25`, though the nearest face is −x.

#### PhysicsOverlap_BoxCapsule (0x00346d18) {#u4-physicsoverlap-boxcapsule-00346d18}

Table (1, 3). Size 0x9c. Mirror: calls `PhysicsOverlap_CapsuleBox(c, other, own)`; on a hit it **swaps the two
0x40-byte halves of the contact wholesale** (`+0x00..+0x3f` ↔ `+0x40..+0x7f`); `+0x80` onwards are untouched.
Confirmed (code) at `0x00346d18`. So after it:

- `+0x10` = the box's outward normal towards the capsule (what CapsuleBox put in `+0x50`), pointing from the querying
  box to the other shape;
- `+0x50` = whatever `+0x10` held before the call, which CapsuleBox never writes: **uninitialised in the original**
  (the slot is a fresh, uncleared stack buffer, [above](#overlapshape)). Coney: write the capsule-side normal (the
  negated box normal) there and note that the original leaves it undefined; no caller reads it.
- `+0x88` = the depth, unchanged.

The owner and shape words swapped here are overwritten afterwards by `Contact_InsertSorted`, so they end correct.

#### PhysicsOverlap_SegmentSphere (0x00346de0) {#u4-physicsoverlap-segmentsphere-00346de0}

Table (4, 2). Size 0xd0. Reads the segment's S, E, r and the sphere's r, C. Writes nothing. Confirmed (code) at
`0x00346de0`:

> `Q = Segment_ClosestPoint(S, E − S, C)`; hit when `|Q − C|² ≤ (r_seg + r_sph)²`.

Touching is a hit. A zero-length segment is a sphere at S.

#### PhysicsOverlap_SphereSegment (0x00346eb0) {#u4-physicsoverlap-spheresegment-00346eb0}

Table (2, 4). Size 0x24. Mirror: `PhysicsOverlap_SegmentSphere(c, other, own)`, result returned, no swap. Confirmed
(code) at `0x00346eb0`.

#### PhysicsOverlap_CapsuleSphere (0x00346ed8) {#u4-physicsoverlap-capsulesphere-00346ed8}

Table (3, 2). Size 0xb8. Reads the capsule's r, h, B and the sphere's r, C. Writes nothing. Confirmed (code) at
`0x00346ed8`. **Here the capsule is a true capsule** (a sphere-swept vertical segment with rounded ends), not a flat
cylinder:

> `Q = Segment_ClosestPoint(B, (0, 0, h), C)`; hit when `|Q − C|² ≤ (r_cap + r_sph)²`.

So the capsule reaches `r_cap` below its base and above `B.z + h`. Touching is a hit. h = 0 makes it a sphere at B.
This corrects [#shapes](#shapes)' "every test treats it as the vertical range": the capsule-sphere and
capsule-segment tests do not.

#### PhysicsOverlap_SphereCapsule (0x00346f90) {#u4-physicsoverlap-spherecapsule-00346f90}

Table (2, 3). Size 0x24. Mirror: `PhysicsOverlap_CapsuleSphere(c, other, own)`, no swap. Confirmed (code) at
`0x00346f90`. This is the push sphere's test against another human ([The human body](#human-body)): the
2.0 m sphere against the human's rounded capsule.

#### PhysicsOverlap_SegmentCapsule (0x00346fb8) {#u4-physicsoverlap-segmentcapsule-00346fb8}

Table (4, 3); also the sweep table's (4, 3) through `0x00345d38`. Size 0x18c. Reads the segment's S, E, r and the
capsule's r, h, B. Writes nothing. Confirmed (code) at `0x00346fb8`. The capsule is again a **true capsule**:

```text
d1 = E − S;  d2 = (0, 0, h)  (from the constant (0, 0, 1, 1) at 0x00511740 scaled by h);  w = S − B
(s, t) = Segment_ClosestPoints(d1, d2, w)
hit when |w + s·d1 − t·d2|² ≤ (r_seg + r_cap)²
```

Touching is a hit.

#### PhysicsOverlap_CapsuleSegment (0x00347148) {#u4-physicsoverlap-capsulesegment-00347148}

Table (3, 4); also the sweep table's (3, 4) through `0x00345d18`. Size 0x24. Mirror:
`PhysicsOverlap_SegmentCapsule(c, other, own)`, no swap. Confirmed (code) at `0x00347148`.

#### Who reaches a car {#who-reaches-a-car}

- **A segment never touches a box**, in either table: overlap (4, 1) and (1, 4) are empty (`IPhysics_Construct`,
  [#dispatch](#dispatch)); sweep (4, 1) is `PhysicsSweep_SegmentBox` `0x00345588`, which returns 0, and
  sweep (1, 4) `0x00345590` calls it with the move negated, so also 0. Confirmed (code).
- **Strikes use the sweep table.** `Human_TestStrikes` (`0x0033f110`) takes, for each enabled strike shape with flag
  `0x2` (and each held-weapon strike sphere), its current world position (shape vtable `+0x24`) and its previous one
  (shape `+0x20`), moves the shape back to the previous position (shape vtable `+0xc`), calls
  `sweep[strike.type][other.type](contact, body, shape, otherBody, otherShape, &move)` (IPhysics `+0x7c`) with
  `move = current − previous`, and puts the shape back. Confirmed (code) at `0x0033f110`. Against an object's body
  (a car's two boxes) every shape of its chain is tried; the first hit calls the striker's `+0x104`
  (`Strike_Contact`) with this contact as the hit record.
- So a car box is reached only by **sphere** strike shapes, through `PhysicsSweep_SphereBox` (`0x003453f8`,
  `Sweep_SphereOrientedBox` `0x0034bbe8`), which writes the fraction `+0x80`, the point `+0x90`, `+0x10`, `+0x50`,
  `+0x60`, `+0x70`/`+0x74` and clears `+0xb0`; what each means, and which `Car_OnHit` reads, is in
  [Unit 5](#u4b-summary-for-the-implementer). Confirmed (code) at `0x003453f8`.
- The strike spheres (from [Combat](combat.md#moving-strikes)): hands 19 and 25 (0.09 m), head 6 (0.15 m), feet 30
  and 33 (0.15 m), and the held weapon's row of 0.1 m spheres (or its single sphere). The `car_glance` runtime hit
  (`S1`, bone 19) is consistent with this.
- **The segment's "previous position" moves only its start.** A segment's position setter (`0x004f1250`, vtable
  `0x00544770` `+0xc`) writes `+0x40` only, and its position getter (`0x004f1260`) returns `+0x40`. So during a strike
  test a forearm, shin or spine segment runs from its **previous start to its current end**, and segment-segment and
  segment-capsule are tested on that stretched segment. Confirmed (code) at `0x004f1250`, `0x004f1260` and
  `0x0033f110`; the consequence (a slightly longer reach along the swing) is inferred. A sphere's setter
  (`0x004f1200`) moves its centre; the sphere-box sweep then sweeps it back to the current position.

#### Test ideas (synthetic shapes) {#u4-test-ideas-synthetic-shapes-expected-val}

All boxes: axes A0 = (1, 0, 0), A1 = (0, 1, 0), A2 = (0, 0, 1) unless stated.

1. **SphereSphere touching.** r 0.5 at the origin and r 0.5 at (1, 0, 0): hit. At (1.001, 0, 0): miss. The contact
   slot is unchanged in both cases.
2. **CapsuleCapsule.** own r 0.35, h 2.0, base (0, 0, 0); other r 0.35, h 2.0:
   - base (0.5, 0, 1.0): hit; `+0x10` = (1, 0, 0, 1), `+0x50` = (−1, 0, 0, 1), `+0x88` = 0.2.
   - base (0, 0, 1.5) (same axis): hit; `+0x10` = (0, 1, 0, 1), `+0x50` = (0, −1, 0, 1), `+0x88` = 0.7.
   - base (0.5, 0, 2.0) (ends touching): hit (vertical test inclusive).
   - base (0.7, 0, 0) (sides touching, 0.7² vs (0.35 + 0.35)², equal in float): **miss** (strict).
   - base (0.5, 0, 2.01): miss.
3. **SegmentSegment.** own (0, 0, 0)-(1, 0, 0) r 0.1:
   - other (0.5, −1, 0.15)-(0.5, 1, 0.15) r 0.1: hit (distance 0.15).
   - other at z 0.2 (distance 0.2 = 0.1 + 0.1, equal in float): hit (inclusive). At z 0.21: miss.
   - other (0.5, 0.1, 0)-(1.5, 0.1, 0) r 0.1 (parallel): closest params s = 0.5, t = 0; distance 0.1: hit.
   - other (2, 0.1, 0)-(3, 0.1, 0) r 0.1 (parallel, apart): s = 1, t = 0; distance² 1.01: miss.
   - **Degenerate other** (0.9, 0.05, 0)-(0.9, 0.05, 0) r 0.1: s = 0, t = 0, distance² 0.8125: **miss** (the true
     distance is 0.05). Pins the kernel quirk.
4. **BoxSphere / SphereBox.** Box at the origin, half (1, 1, 1):
   - sphere r 0.5 at (1.5, 0, 0): hit (touching). At (1.51, 0, 0): miss.
   - sphere r 0.01 at (0.2, 0.3, 0) (inside): hit.
   - sphere r 0.7 at (1.5, 1.5, 0): distance² 0.5 > 0.49: miss; r 0.75: hit.
   - box rotated 45° about z (A0 = (0.7071, 0.7071, 0), A1 = (−0.7071, 0.7071, 0)), half (1, 0.5, 0.5); sphere r 0.4 at
     (0, 1, 0): local (0.7071, 0.7071, 0), outside y by 0.2071, distance² 0.0429: hit; r 0.2: miss.
   - SphereBox with the arguments swapped gives the same results.
5. **CapsuleBox** (car-like box: centre (0, 0, 0.5), half (2, 1, 0.5); so u = 2, i = 0, j = 1). Capsule r 0.35,
   h 2.0:
   - base (0, −1.2, 0): face; hit; `+0x50` = (0, −1, 0, 1); `+0x88` = 0.15.
   - base (0, −1.36, 0): gap 0.36 > 0.35: miss.
   - base (2.2, 1.2, 0): corner (+, +); hit; `+0x50` xyz = (0.7071, 0.7071, 0); `+0x88` = 0.1225 − 0.08 = 0.0425 (m²).
   - base (2.3, 1.3, 0): corner distance² 0.18 > 0.1225: miss.
   - base (−2.2, 1.2, 0): corner (−, +); `+0x50` xyz = (−0.7071, 0.7071, 0).
   - **base (−2.2, −1.2, 0): corner (−, −); hit, depth 0.0425; `+0x50` xyz = normalize(−4.2, −2.2, 0) =
     (−0.8858, −0.4640, 0)**, not (−0.7071, −0.7071, 0). Pins the corner quirk.
   - base (0.5, 0.2, 0) (over the box): `x < y` false: `+0x50` = (1, 0, 0, 1), `+0x88` = 2 − 0.5 + 0.35 = 1.85.
   - **base (−1.5, 0.1, 0)**: `x < y` true: `+0x50` = (0, 1, 0, 1), `+0x88` = 1 − 0.1 + 0.35 = 1.25. Pins the
     face-choice quirk.
   - base (0, −1.2, 50): the pair function still hits (height ignored); a test through the whole query must miss
     (the gather box rejects it).
6. **BoxCapsule.** Same as the first CapsuleBox case with the box querying: hit; `+0x10` = (0, −1, 0, 1); `+0x88` =
   0.15; `+0x50` is whatever Coney defines (the original leaves it undefined).
7. **SegmentSphere.** Segment (0, 0, 0)-(0.4, 0, 0) r 0.1: sphere r 0.15 at (0.5, 0, 0): hit (distance 0.1 to the
   end). Sphere r 0.15 at (0.2, 0.3, 0): distance² 0.09 > 0.0625: miss; r 0.25: hit. Zero-length segment at the origin
   r 0.1 against sphere r 0.1 at (0.2, 0, 0): hit (touching).
8. **CapsuleSphere (rounded ends).** Capsule base (0, 0, 0), h 2.0, r 0.35:
   - sphere r 0.1 at (0, 0, 2.3): closest (0, 0, 2), distance² 0.09 ≤ 0.2025: hit (a flat-ended cylinder would miss).
   - sphere r 0.1 at (0, 0, −0.3): hit (below the base).
   - sphere r 0.1 at (0.46, 0, 1): distance 0.46 > 0.45: miss.
   - push sphere r 2.0 at (1.5, 0, 1): hit.
9. **SegmentCapsule / CapsuleSegment.** Capsule base (0, 0, 0), h 2.0, r 0.35; segment (0.3, −0.5, 2.2)-(0.3, 0.5,
   2.2) r 0.07: s = 0.5, t = 1, closest (0.3, 0, 2.2) and (0, 0, 2), distance² 0.13 ≤ 0.1764: hit. Raise the segment
   to z 2.4: distance² 0.25 > 0.1764: miss. CapsuleSegment with the arguments swapped: same.
10. **BoxBox.** Both half (1, 1, 1): A at the origin, B at (1.9, 0, 0): hit; at (2.0, 0, 0): hit (touching); at
    (2.1, 0, 0): miss. **Quirk**: A at (100, 0, 0), B at (101.5, 0, 0): the original's matrix gives a miss although
    they overlap (see the function).
11. **Segment never reaches a box.** Overlap table (4, 1), (1, 4): no entry, no contact; the strike step's sphere
    shapes are the only ones that can damage a car (an integration test for issue #72: a hand sphere overlapping box 1
    hits; a forearm segment through the same box does not).
12. **Query order.** Two bodies both overlapped by one push sphere: both pushed, whatever their order; a contact list
    built by Coney's overlap query is in discovery order.

### Unit 5: A sphere swept against a box: strikes on cars and objects {#unit-5}

**What it is.** A sphere swept against an oriented box (sweep table entries (2, 1) and (1, 2)): the region of the
start centre, the face, edge and corner steps, and the contact a strike hands to `Strike_Contact` and `Car_OnHit`.
It answers which contact field holds the hit point: `+0x90` in the world, `+0x60` in the struck box's frame.

- **Coney:** `src/platform/play_level_mode.*` (`strikeObjects()`) and `src/world_objects/car_hits.*`: issue #72's
  step 1 ([Cars](cars.md#coney-fix)) is this unit plus the strike loop.
- **Tests:** [Test ideas](#u4b-test-ideas-synthetic-shapes-values-compu).

10 functions, 5728 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x003453f8` | `PhysicsSweep_SphereBox` | 200 |
| `0x003454c0` | `PhysicsSweep_BoxSphere` | 200 |
| `0x0034bbe8` | `Sweep_SphereOrientedBox` | 2116 |
| `0x0034baf0` | `SphereBox_ToBoxSpace` | 248 |
| `0x0034b040` | `SphereBox_SweepCorner` | 884 |
| `0x0034b3b8` | `SphereBox_SweepEdge` | 584 |
| `0x0034b600` | `SphereBox_SweepEdges` | 432 |
| `0x0034b7b0` | `SphereBox_SweepFaceEdges` | 828 |
| `0x0034af50` | `Ray_SphereRoot` | 136 |
| `0x0034afd8` | `Ray_CircleRoot` | 100 |

#### Summary for the implementer {#u4b-summary-for-the-implementer}

- **The answer on `+0x60` vs `+0x90`.** Both hold the same contact point, in two frames. `+0x90` is the point in
  **world** space (w = 1). `+0x60` is the point in the **struck box's own frame**: coordinates along the box's axes
  A0, A1, A2 (shape `+0x50`, `+0x60`, `+0x70`), measured from the **box's centre** (shape `+0x80`), not from the car's
  origin. `Car_OnHit` (`0x0038bea0`) reads `+0x60` x and y for its zone bands (hit kinds 0, 1, 2), `+0x90` for the
  rock (`Car_StartHitRock`), the dent sound and the window-break effect, and `+0x44` for which box was struck.
  Confirmed (code) at `0x003453f8` (writes), `0x0034c27c`-`0x0034c3e8` (how they are made), `0x0021b764` (Strike_Contact
  passes this very record to `Car_OnHit` in `a1`) and `0x0038bea0` (reads). The cabin box's offset (y +0.281 on the
  sedan) is already noted as confirmed (runtime) in the Ghidra comment in `Car_OnHit`.
- **The whole contact a sphere-box sweep writes** (`PhysicsSweep_SphereBox`, sphere moving, box = second half):

  | Offset | Value | Frame |
  | --- | --- | --- |
  | `+0x10` | unit vector from the sphere's **start** centre to the box centre, **horizontal** (z set to 0, then normalised), w = 1 | world |
  | `+0x50` | the box's outward unit normal of the **feature region the sphere's start centre is in** (face, edge or corner; table below), w = 0 | world |
  | `+0x60` | the contact point (x, y, z); w not written (stack leftover) | box frame |
  | `+0x70` | feature type: 0 face, 2 edge, 1 corner, 4 start centre inside the box | |
  | `+0x74` | feature index (tables below) | |
  | `+0x80` | fraction of the move at first contact, 0 to 1 | |
  | `+0x90` | the contact point, w = 1 | world |
  | `+0xb0` | 0 | |

  Nothing else is written; `+0x00`/`+0x04`/`+0x40`/`+0x44` are set by `Human_TestStrikes` after a hit, and every
  other byte is whatever the caller's stack held. Confirmed (code) at `0x003453f8`.
- **What counts as a hit** (confirmed (code) at `0x0034bbe8` and its steps):
  1. The sphere's **centre** at the start of the move inside the box or on its surface (each |local coordinate| ≤
     its half extent): a hit at fraction 0, whatever the move (also a zero move).
  2. Otherwise the swept sphere must reach the box during the move (fraction ≤ 1.0, touching counts).
  3. **A sphere that already overlaps or touches the box at the start, with its centre outside, never hits** (each
     step returns −1, which the caller treats as no contact). For a strike this means a hand that starts an update
     within its radius of the car gets no contact that update; it hits the next update only if its centre is then
     inside the box (case 1).
  4. A zero move hits only in case 1.
- **Normals are chosen by the start region, not by the feature actually touched**: a sphere starting in a face's
  region that clips that face's edge still gets the face normal. And **six of the 26 region normals are wrong in the
  original's tables** (the edges parallel to y and four of the eight corners): see
  [the normal tables](#the-normal-tables). No strike consumer reads `+0x50` (Strike_Contact uses `+0x50` only for a
  level-mesh hit, which does not come from this function), so for strikes this is harmless; Coney should reproduce
  the table as is and pin it with a test.
- **The corner region's point is the sphere centre**, not the corner: when the start centre is outside all three
  slabs and the first contact is the corner itself, `+0x60`/`+0x90` hold the sphere's centre at contact (0.09 m
  away from the box for a hand). Every other case gives a point on the box surface. Confirmed (code) at `0x0034b984`.
- **The inside case writes the world z in `+0x60`.** Its `+0x60` is (local x, local y, **world** z of the sphere
  centre), and `+0x90` is the sphere's start centre. Confirmed (code) at `0x0034beb0`.

#### Records and conventions {#u4b-records-and-conventions}

**Box record** (shape type 1, from [Shapes](#shapes)): `+0x40`, `+0x44`, `+0x48` half extents h0, h1, h2;
four 16-byte rows `+0x50` A0, `+0x60` A1, `+0x70` A2 (unit axes), `+0x80` P (centre, w = 1). The kernel reads the
rows as a matrix whose rows 0-2 are the axes and row 3 the centre. **Sphere record** (type 2): `+0x40` radius r,
`+0x50` centre C. Confirmed (code) at `0x003453f8`.

**Local coordinates** used below: d = ((C − P)·A0, (C − P)·A1, (C − P)·A2) for the start centre and m = (move − boxMove)
projected the same way (dot products of xyz only). Confirmed (code) at `0x0034bc44`-`0x0034bd58`.

**Folding.** Each axis i with d_i < 0 (strictly) is flipped: d_i and m_i are negated and the axis's sign s_i is −1
(else +1). All steps then work with d_i ≥ 0, so "the face" on an axis is always the positive one; the sign is put
back at the end. Confirmed (code) at `0x0034bcb8`-`0x0034bdac`.

**Region** of the start centre, by folded d against the half extents, inclusive (`≤` means within the slab):

| x within | y within | z within | Region | Step called | Its axes (u, v, w) or (P, Q, R) |
| --- | --- | --- | --- | --- | --- |
| yes | yes | yes | inside | none | |
| yes | yes | no | face z | FaceRegion | (x, y, z) |
| yes | no | yes | face y | FaceRegion | (x, z, y) |
| no | yes | yes | face x | FaceRegion | (y, z, x) |
| yes | no | no | edge along x | EdgeRegion | P = y, Q = x, R = z |
| no | yes | no | edge along y | EdgeRegion | P = x, Q = y, R = z |
| no | no | yes | edge along z | EdgeRegion | P = x, Q = z, R = y |
| no | no | no | corner | CornerRegion | (x, y, z) |

Confirmed (code) at `0x0034bdac`-`0x0034c1f0`. The radius plays no part in the region choice.

**Step results**: 1 hit (fraction and folded local point written), 0 no hit, −1 the sphere already touches (fraction
0 written, no hit). Only 1 counts.

#### The functions {#u4b-the-functions}

#### PhysicsSweep_SphereBox (0x003453f8) {#u4b-physicssweep-spherebox-003453f8}

Size 0xc8. Sweep table entry (2, 1). `bool fn(Contact* c, Body* own, Shape* sphere, Body* other, Shape* box, const
Vec4* move)`; the bodies are not read. Calls `Sweep_SphereOrientedBox(r = sphere+0x40, maxFrac = 1.0 (0x3f800000),
rows = box+0x50, half = box+0x40, boxMove = (0, 0, 0, 1) at 0x005116c0, C = sphere+0x50, sphereMove = move, ...)`.
On a hit it copies the outputs into the contact as the summary table says (`+0x80` fraction, `+0x74` index, `+0x90`
world point, `+0x60` local point, `+0x50` feature normal, `+0x10` direction to the box, `+0x70` type) and clears
`+0xb0`; returns 1. On no hit it writes nothing and returns 0. Confirmed (code) at `0x003453f8`.

#### PhysicsSweep_BoxSphere (0x003454c0) {#u4b-physicssweep-boxsphere-003454c0}

Size 0xc8. Sweep table entry (1, 2): the box moves, the sphere is the other shape. Same kernel with the roles
swapped: `r` and `C` from the other (sphere) shape, the rows and half extents from the moving box, `boxMove = move`
and `sphereMove = (0, 0, 0, 1)`, so the kernel sees the sphere moving by −move relative to the box. Writes the same
values with the halves swapped: `+0x80` fraction, `+0x34` index, `+0x30` type, `+0x90` world point, `+0x20` local
point, `+0x10` the box's feature normal, `+0x50` the horizontal direction from the sphere to the box centre, `+0xb0`
= 0. Confirmed (code) at `0x003454c0`.

The box's frame is its pose at the **start** of the move, so the world point `+0x90` is where the contact is on the
box's start pose, short of the true contact by fraction × move. Inferred from the kernel taking the box rows as
given. Not used by strikes (a strike sphere is always the moving shape).

#### Sweep_SphereOrientedBox (0x0034bbe8) {#u4b-sweep-sphereorientedbox-0034bbe8}

Size 0x844. Arguments (EE ABI, float registers then integer registers then stack): `f12` r, `f13` maxFrac, `a0` box
rows (A0, A1, A2, P), `a1` half extents, `a2` box move, `a3` sphere start centre C, `t0` sphere move, `t1` out
fraction, `t2` out world point, `t3` out local point, stack: out feature normal (world), out direction to box, out
feature type, out feature index. Returns 1 on a hit, 0 otherwise. Confirmed (code) at `0x0034bbe8`.

1. Local start d and relative move m, folded; region (above).
2. **Inside**: fraction 0; type 4; index 0; world point = C (the start centre, unchanged w); local point =
   (clamp((C·A0 − P·A0), −h0, h0), clamp((C·A1 − P·A1), −h1, h1), **C.z**) (signed, not folded; the third component is
   the world z, a quirk); feature normal n = normalise((C − P) with z set to 0), w = 1; direction to box = −n (w
   kept 1). Returns 1 without testing maxFrac. Confirmed (code) at `0x0034bddc`-`0x0034bf48`.
3. Otherwise the region's step (with the axis order of the table and the flag t0 = 1) gives a result, a fraction and
   a folded local point (three floats in the step's axis order, mapped back to x, y, z). The type and index are
   written before the step runs (face 0, edge 2, corner 1; index from the signs, below).
4. If the result is not 1, or fraction > maxFrac (strict: fraction = maxFrac hits), return 0. Confirmed (code) at
   `0x0034c244`-`0x0034c264`.
5. On a hit: feature normal = `SphereBox_ToBoxSpace`(type, index); direction to box = normalise((P − C) with z set
   to 0), w = 1; local point = (s0·x, s1·y, s2·z) (unfold); world point = P + A0·lx + A1·ly + A2·lz (w = 1). Return 1.
   Confirmed (code) at `0x0034c26c`-`0x0034c3f4`.

Edge case: a sphere whose start centre is straight above or below the box centre (horizontal distance 0) normalises
a zero vector for the direction to the box (VU `rsqrt` of 0; the result is not meaningful). Inferred; Coney should
write (0, 0, 0, 1) there and note it.

**Feature index** (signs s0, s1, s2 of x, y, z after folding). Confirmed (code) at `0x0034bf88`-`0x0034c240`:

| Region | Index |
| --- | --- |
| face x | 1 if s0 > 0, else 3 |
| face y | 0 if s1 > 0, else 2 |
| face z | 4 if s2 > 0, else 5 |
| edge along z (x, y outside) | (s0, s1): (−, +) 0, (+, +) 1, (+, −) 2, (−, −) 3 |
| edge along y (x, z outside) | (s0, s2): (−, −) 4, (−, +) 5, (+, −) 6, (+, +) 7 |
| edge along x (y, z outside) | (s1, s2): (+, +) 8, (+, −) 9, (−, −) 10, (−, +) 11 |
| corner | (s0, s1, s2): (−, +, +) 0, (+, +, +) 1, (+, +, −) 2, (−, +, −) 3, (−, −, +) 4, (+, −, +) 5, (+, −, −) 6, (−, −, −) 7 |

#### SphereBox_ToBoxSpace (0x0034baf0) {#u4b-spherebox-toboxspace-0034baf0}

Size 0xf8. `void fn(Vec4* out, const Mat4* rows, int type, int index)`. Picks a 16-byte local normal: type 0 from the
face table `0x00512930`, type 2 from the edge table `0x00512990`, type 1 from the corner table `0x00512a50` (each
entry 16 bytes, index × 16), any other type the constant (0, 1, 0, 1) at `0x00511730`; then
out = A0·nx + A1·ny + A2·nz + (0, 0, 0, 1)·nw (all four lanes), so it turns a box-frame normal into world space. The
name (from an earlier pass) is the wrong way round: it goes from box space to world. Confirmed (code) at
`0x0034baf0`; tables read from the executable.

#### The normal tables {#the-normal-tables}

a = 0.70710677 (`0x3f3504f4`), b = 0.57735026 (`0x3f13b646`); all w = 0.

| Index | Face table `0x00512930` | Edge table `0x00512990` | Corner table `0x00512a50` |
| --- | --- | --- | --- |
| 0 | (0, 1, 0) | (−a, a, 0) | (−b, b, b) |
| 1 | (1, 0, 0) | (a, a, 0) | (−b, b, −b) |
| 2 | (0, −1, 0) | (a, −a, 0) | (b, b, −b) |
| 3 | (−1, 0, 0) | (−a, −a, 0) | (b, b, b) |
| 4 | (0, 0, 1) | (a, 0, a) | (−b, −b, b) |
| 5 | (0, 0, −1) | (a, 0, −a) | (−b, −b, −b) |
| 6 | | (−a, 0, −a) | (b, −b, −b) |
| 7 | | (−a, 0, a) | (b, −b, b) |
| 8-11 | | (0, a, a), (0, a, −a), (0, −a, −a), (0, −a, a) | |

Joined with the index table above, the faces, the edges along z, the edges along x and corners 0, 2, 4, 6 give the
true outward normal. **The edges along y and corners 1, 3, 5, 7 do not**:

| Start region (signs) | Index | Normal written (box frame) | True outward normal |
| --- | --- | --- | --- |
| edge along y, (s0, s2) = (+, +) | 7 | (−a, 0, a) | (a, 0, a) |
| edge along y, (+, −) | 6 | (−a, 0, −a) | (a, 0, −a) |
| edge along y, (−, +) | 5 | (a, 0, −a) | (−a, 0, a) |
| edge along y, (−, −) | 4 | (a, 0, a) | (−a, 0, −a) |
| corner (+, +, +) | 1 | (−b, b, −b) | (b, b, b) |
| corner (−, +, −) | 3 | (b, b, b) | (−b, b, −b) |
| corner (+, −, +) | 5 | (−b, −b, −b) | (b, −b, b) |
| corner (−, −, −) | 7 | (b, −b, b) | (−b, −b, −b) |

Confirmed (code) at `0x0034c118`-`0x0034c240` (index choice) with the tables read from the executable. Whether
anything visible depends on it is not known: no strike consumer reads `+0x50`; the camera's tilt (the kernel's other
caller) might. Coney should keep the original's tables (faithful first) behind a comment naming the mismatch.

#### SphereBox_SweepCorner (0x0034b040): the face-region step {#u4b-spherebox-sweepcorner-0034b040-the-face}

Size 0x374. The name is from an earlier pass; the function handles a **face region** and reaches the corner only as
its last case. Arguments: half extents (a, b, c) for (u, v, w); folded start (u, v, w) with u ≤ a, v ≤ b (when called
from the main kernel; from EdgeRegion u or v may lie outside) and w > c; move (mu, mv, mw); r on the stack; out
fraction and out (u, v, w); flag t0. Confirmed (code) at `0x0034b040`:

1. If t0 ≠ 0 and w ≤ c + r: fraction 0, return −1 (already touching the face).
2. If mw ≥ 0: return 0 (not moving towards the face plane).
3. Let g = w − c (> r here when t0 = 1). For u, pick the edge the move heads for: if mu ≥ 0, su = +1 and
   ku = mu·g − mw·(u − a); else su = −1 and ku = mw·(u + a) − mu·g. The same for v with b: sv, kv. (ku / |mw| is how
   far past the edge at su·a the centre is when it reaches the plane w = c + r, plus r·|mu|/|mw|; so the centre is
   still over the face at that moment exactly when ku ≤ r·|mu|.)
4. **Face**: if ku ≤ r·|mu| and kv ≤ r·|mv|: fraction = (r − g) / mw; point = (u + fraction·mu, v + fraction·mv, c).
   Return 1.
5. **u edge**: if ku > r·|mu| and kv ≤ r·|mv|: if ku² > r²·(mu² + mw²) return 0 (the path passes the edge farther
   than r). Else fraction = Ray_CircleRoot(q = (u − su·a, g), m = (mu, mw), A = mu² + mw², r²); point = (su·a,
   v + fraction·mv, c). Return 1.
6. **v edge**: ku ≤ r·|mu| and kv > r·|mv|: as 5 with v, b, mv: if kv² > r²·(mv² + mw²) return 0; fraction =
   Ray_CircleRoot((v − sv·b, g), (mv, mw), mv² + mw², r²); point = (u + fraction·mu, sv·b, c). Return 1.
7. **Corner**: both beyond. If ku² > r²·(mu² + mw²) or kv² > r²·(mv² + mw²) return 0. With q = (u − su·a, v − sv·b,
   g) and m = (mu, mv, mw): if |q × m|² > r²·|m|² return 0 (the line misses the corner sphere); else fraction =
   Ray_SphereRoot(q, m, r²); point = (su·a, sv·b, c), the corner. Return 1.

The edge in 5 and 6 is an infinite line: the point's other coordinate (v + fraction·mv, or u + fraction·mu) is not
clamped to the face. Comparisons are exactly as written (`<` vs `≤`): step 4 takes the face when ku equals r·|mu|.
When the path is beyond both edges but misses the corner sphere, the function returns 0 without trying the two edge
cylinders (it can miss a sphere that would graze an edge near the corner). Confirmed (code); the geometric readings
are inferred from the algebra.

#### SphereBox_SweepEdges (0x0034b600): the edge-region step {#u4b-spherebox-sweepedges-0034b600-the-edge-r}

Size 0x1b0. Arguments: half extents (a, b, c) for (P, Q, R); folded start (p, q, s) with p > a, s > c (Q is the axis
along the edge); move (mp, mq, ms); r on the stack; out fraction, out (P, Q, R); flag t0. Confirmed (code) at
`0x0034b600`. Let ep = p − a, es = s − c.

1. If t0 ≠ 0 and ep² + es² − r² ≤ 0: fraction 0, return −1 (already within r of the edge line).
2. If mp·ep + ms·es ≥ 0: return 0 (not approaching the edge line in the P-R plane).
3. k = ms·ep − mp·es. (k / |mp| is how far above the R face plane, s − c, the centre is when it crosses p = a; −k / |ms|
   is how far out along P it is when it crosses s = c.)
4. If k ≥ 0: if mp ≥ 0 return 0. If k ≤ r·|mp|: the **edge** (SweepEdge, axis 1 = R with (c, es, ms), axis 2 = P
   with (a, ep, mp)). Else the **R face**: FaceRegion with (u, v, w) = (P, Q, R), t0 = 0.
5. If k < 0: if ms ≥ 0 return 0. If k ≥ r·ms (that is |k| ≤ r·|ms|): the **edge** (SweepEdge, axis 1 = P with
   (a, ep, mp), axis 2 = R with (c, es, ms)). Else the **P face**: FaceRegion with (u, v, w) = (R, Q, P), t0 = 0.

Returns what the step it calls returns. Confirmed (code).

#### SphereBox_SweepEdge (0x0034b3b8) {#u4b-spherebox-sweepedge-0034b3b8}

Size 0x248. One edge, its line along Q at (h1 on axis 1, h2 on axis 2). Arguments: q (coordinate along the edge),
h1, b (half extent along Q), h2, e1 = (coordinate − h1) on axis 1, e2 on axis 2, m1, mq, m2 on the stack, r on the
stack; out fraction, out axis 1, out Q, out axis 2. Confirmed (code) at `0x0034b3b8`:

1. Pick the edge's end the move heads for: if mq ≥ 0, sq = +1, f = q − b, else sq = −1, f = q + b;
   j1 = sq·(e1·mq − f·m1), j2 = sq·(e2·mq − f·m2) (the offsets from the edge line, times |mq|, at the moment the
   centre crosses the end plane).
2. **End corner**: if j1 ≥ 0 and j2 ≥ 0 and r²·mq² < j1² + j2² (the centre leaves past the end still farther than r
   from the line): with qv = (e1, f, e2), m = (m1, mq, m2): if |qv × m|² > r²·|m|² return 0; else fraction =
   Ray_SphereRoot(qv, m, r²), point = (h1, sq·b, h2). Return 1.
3. **Cylinder** otherwise: fraction = Ray_CircleRoot((e1, e2), (m1, m2), m1² + m2², r²); point = (h1, q + fraction·mq,
   h2) (not clamped to the edge's length). Return 1.

There is no separate "line misses the cylinder" test here: the callers only come here when the P-R path passes
within r of the edge (inferred from the conditions in EdgeRegion step 4/5, which bound |k| / |m_PR| by r).

#### SphereBox_SweepFaceEdges (0x0034b7b0): the corner-region step {#u4b-spherebox-sweepfaceedges-0034b7b0-the-co}

Size 0x33c. The name is from an earlier pass; it handles the **corner region** (start centre outside all three
slabs). Arguments: half extents (hx, hy, hz), folded start (dx, dy, dz), move (mx, my, mz) with mz on the stack, r on
the stack; out fraction and out (x, y, z). No t0 flag. Let e = (dx − hx, dy − hy, dz − hz). Confirmed (code) at
`0x0034b7b0`:

1. If |e|² − r² ≤ 0: fraction 0, return −1 (already within r of the corner).
2. If m·e ≥ 0: return 0.
3. g2 = mx·ez − mz·ex, g3 = my·ez − mz·ey, g4 = my·ex − mx·ey. (When the centre crosses the plane of one axis, these
   give its offsets from the corner on the other two, times that axis's move.)
4. **Corner sphere** if any of, in this order: (A) g2 < 0, g4 ≥ 0 and g2² + g4² ≤ r²·mx²; (B) g4 < 0, g3 < 0 and
   g3² + g4² ≤ r²·my²; (C) g2 ≥ 0, g3 ≥ 0 and g2² + g3² ≤ r²·mz². Then fraction = Ray_SphereRoot(e, m, r²) and the
   point = **(dx + fraction·mx, dy + fraction·my, dz + fraction·mz), the sphere's centre at contact** (not the
   corner). Return 1. Confirmed (code) at `0x0034b974`-`0x0034b9b8`.
5. Otherwise one of the three edges through the corner, by EdgeRegion with t0 = 0: if g2 < 0 and g4 ≥ 0, the edge
   along x (P = y, Q = x, R = z); else if g4 < 0 and g3 < 0, the edge along y (P = x, Q = y, R = z); else the edge
   along z (P = x, Q = z, R = y). Returns its result.

The corner sphere test has no "line misses the sphere" check of its own: (A)-(C) bound the crossing point's distance
from the corner by r, which implies it (inferred).

#### Ray_SphereRoot (0x0034af50) and Ray_CircleRoot (0x0034afd8) {#u4b-ray-sphereroot-0034af50-and-ray-circlero}

Sizes 0x88 and 0x64. `float Ray_SphereRoot(q.x, q.y, q.z, m.x, m.y, m.z, rr)` (all in `f12`-`f18`);
`float Ray_CircleRoot(q.x, q.y, m.x, m.y, A, rr)` (A = |m|² passed in). Both give the smaller root of
|q + t·m|² = rr as

```text
C = |q|² − rr;  B = q·m;  A = |m|²;  D = sqrt(|B² − A·C|);  s = 1 / D
t = (C·s) / (1 − B·s)          // = C / (D − B), the stable form of (−B − D) / A
```

Confirmed (code) at `0x0034af50` and `0x0034afd8`. Note the absolute value under the root (a negative discriminant is
not rejected here; the callers screen misses first) and that D = 0 (exact tangency) divides by zero (on the PS2 this
saturates rather than giving inf/NaN). Coney may compute C / (D − B) directly; the two differ only in float rounding
and at D = 0, where C / (D − B) gives the exact tangent root.

#### How Human_TestStrikes builds the sweep (0x0033f110) {#u4b-how-human-teststrikes-builds-the-sweep-0}

Confirmed (code) at `0x0033f110` unless marked; the target filters are already on
[combat.md#moving-strikes](combat.md#moving-strikes).

- **Which bodies.** For a candidate body that is not a human (type mask `+0x44` without `0x40`), the striking shapes
  are tried only when the candidate's flags `+0x40` have `0x10` (MELEETARGET) or `0x20` (CHARGETARGET) and the
  striker's owner accepts the other owner (owner vtable slot `+0x108`/`+0x10c`). A car's body flags `0x2211a` include
  `0x10`. The candidate's shapes come from its body vtable `+0x2c` called with (2, 4, striker's owner); every shape of
  that chain is tried, **with no flag test** (a human target's chain is filtered by flag `0x4` instead).
- **Which strike shapes.** The human body's strike chain (`+0x80`), in chain order, those with flags `0x1` (enabled)
  and `0x2`.
- **The move.** For each such shape: `cur` = its world position (shape vtable `+0x24`; a sphere returns its centre
  `+0x50`), `prev` = shape `+0x20`; the shape's position is set to `prev` (vtable `+0x0c`; a sphere's setter
  `0x004f1200` writes `+0x50` only); `move` = cur − prev (xyz; w = 1). Then for each candidate shape,
  `sweep[strike.type][other.type](&contact, strikerBody, strikeShape, candidateBody, candidateShape, &move)`
  (IPhysics `+0x7c` + strike.type × 0x14 + other.type × 4; an empty entry is skipped). The shape's position is put
  back to `cur` (vtable `+0x0c`) after its candidates are done, hit or not.
- **The first hit ends the test.** On the first nonzero result: contact `+0x00` = striker's owner (body `+0x34`),
  `+0x04` = the strike shape, `+0x40` = the candidate's owner, `+0x44` = the candidate shape; then the striker
  owner's `Strike_Contact` (vtable `+0x104`) with the contact; then, if the candidate has an owner, the striker
  body's vtable `+0x44` (put it on the struck list); no further shape or body is tried this update. The contact is a
  single stack record reused for every try, never cleared: fields the sweep does not write hold leftovers of earlier
  tries.
- **Where `prev` comes from** (`PhysicsSphere_Pose` `0x00342b08`, the sphere's vtable `+0x28` slot): each pose first
  copies the current centre to `+0x20` and then computes the new centre, unless flag `0x10` is set, in which case the
  new centre is copied to `+0x20` after it is computed (so prev = cur), and `0x10` is cleared. The shape's enable
  (vtable `+0x1c` slot, `0x004f11c8`) sets flags `0x11` when switching on (enabled + reset) and clears `0x1` when
  switching off. So **the first test after a strike shape is switched on has a zero move**, and afterwards the move
  is from the centre at the previous pose to the current one. Confirmed (code) at `0x00342b08` and `0x004f11c8`; that
  a human body is posed once per update before `Human_TestStrikes` is inferred (combat.md places the test after the
  move).
- **A held weapon's spheres** (body `+0xc4` chain) are tried only against candidates whose flags `+0x40` have `0x8`
  (WEAPONTARGET; cars have it), with the same move and sweep; their hit goes to the **held object's** contact
  handler (vtable `+0xfc` slot) with `+0x00` = the held object, not to `Strike_Contact`; an answer of 5 means "try
  the next shape". How that handler reaches `Car_OnHit` (kind 1) is not traced here.

So for a hand, head or foot sphere against a car box, `PhysicsSweep_SphereBox` sweeps the sphere from its previous
centre `prev` to `cur` with the box at rest (box move (0, 0, 0)), and the contact Strike_Contact gets is the one in
the summary.

#### What Strike_Contact and Car_OnHit read (0x0021b290, 0x0038bea0) {#u4b-what-strike-contact-and-car-onhit-read-0}

Confirmed (code) at the addresses:

- `Strike_Contact(human, contact)`: for a car (`Car_FromObject(contact+0x40)` nonzero) it calls the car's vtable
  `+0x104` (`Car_OnHit`) with **the same contact** (`a1` = the record, `0x0021b764`). For another object it sends
  message 1 to `+0x40` with the point `+0x90` and the direction `+0x10`. For the level (`+0x40` = 0) it spawns a
  particle at `+0x90` along `+0x50` (a mesh contact, not this kernel).
- `Car_OnHit(car, contact)`: `+0x00` the striker (human or object, which picks the kind); `+0x60` x and y, the
  point in the struck box's frame, for the band lookups of kinds 2, 1 and 0 (kind 3, a human's own strike, takes
  its parts from `Car_GetHitZone` and the striker's feet, [cars.md](cars.md#hit-volumes)); `+0x44` the struck box
  (compared with the car's cabin box to pick the cabin table); `+0x90` the world point for `Car_StartHitRock`
  (rock direction from the car's position to the point), the dent sound and the window-break effect
  (`Car_BreakWindow` pushes `+0x90` for the sparks kinds).

For issue #72 step 1 (kind 3): the strike needs `+0x90` (rock, sound, effects) and `+0x44` (only for kinds other than
3); the zone comes from the feet, so `+0x60` matters only from step 3 on (kinds 2, 1, 0).

#### Test ideas (synthetic shapes; values computed from this spec) {#u4b-test-ideas-synthetic-shapes-values-compu}

Box B: centre P = (0, 0, 0.5), axes identity, half (2, 1, 0.5) (so its faces are x = ±2, y = ±1, z = 0 and 1).
Hand sphere r = 0.09. "prev" is the sphere's start centre, "move" = cur − prev. Fractions to 4 decimals.

1. **Face.** prev (0, −1.3, 0.5), move (0, 0.4, 0): face y region, s1 = −1. Hit, fraction 0.525 (= (0.3 − 0.09) /
   0.4); `+0x60` = (0, −1, 0); `+0x90` = (0, −1, 0.5, 1); `+0x50` = (0, −1, 0, 0); `+0x10` = (0, 1, 0, 1); type 0,
   index 2.
2. **0.1 m short.** prev (0, −1.3, 0.5), move (0, 0.11, 0) (ends with the hand's surface 0.1 m from the face): the
   fraction would be 0.21 / 0.11 = 1.909 > 1: no hit, contact untouched.
3. **Face region, clipping the face's edge.** prev (1.98, −1.3, 0.5), move (0.1, 0.4, 0): face y region; ku = 0.022 >
   r·|mu| = 0.009, ku² = 0.000484 ≤ r²·(0.01 + 0.16) = 0.001377: the edge at x = 2. Hit, fraction 0.5419;
   `+0x60` = (2, −1, 0); `+0x90` = (2, −1, 0.5); `+0x50` = (0, −1, 0, 0) (**the face normal**, though the edge was
   touched); `+0x10` = (−0.8359, 0.5488, 0, 1); type 0, index 2.
4. **Glancing miss past an edge.** prev (1.98, −1.3, 0.5), move (0.2, 0.4, 0): ku = 0.052 > 0.018 and ku² = 0.002704
   > r²·0.2 = 0.00162: no hit (the path passes the edge 0.116 m away).
5. **Vertical edge (edge along z).** prev (2.3, −1.3, 0.5), move (−0.4, 0.4, 0): edge region, P = x, R = y, k = 0 →
   SweepEdge cylinder. Hit, fraction 0.5909 (= (0.3√2 − 0.09) / (0.4√2)); `+0x60` = (2, −1, 0); `+0x90` =
   (2, −1, 0.5); `+0x50` = (0.7071, −0.7071, 0, 0) (index 2, a correct entry); `+0x10` = (−0.8706, 0.4921, 0, 1);
   type 2.
6. **Edge along y, the wrong normal.** prev (2.3, 0, 1.3), move (−0.4, 0, −0.4): edge region (x, z outside), fraction
   0.5909; `+0x60` = (2, 0, 0.5); `+0x90` = (2, 0, 1.0); type 2, index 7, `+0x50` = (−0.7071, 0, 0.7071, 0) (the true
   outward normal would be (0.7071, 0, 0.7071)). `+0x10`: prev is 2.3 m from P horizontally along x: (−1, 0, 0, 1).
7. **Corner.** prev (2.3, −1.3, 1.3), move (−0.4, 0.4, −0.4): corner region, g2 = g3 = g4 = 0, case (C): corner
   sphere. Hit, fraction 0.6201 (= (0.3√3 − 0.09) / (0.4√3)); `+0x60` = (2.0520, −1.0520, 0.5520) and `+0x90` =
   (2.0520, −1.0520, 1.0520): **the sphere's centre**, 0.09 m off the corner (2, −1, 1); type 1, index 5,
   `+0x50` = (−0.5774, −0.5774, −0.5774, 0) (true: (0.5774, −0.5774, 0.5774)); `+0x10` = (−0.8706, 0.4921, 0, 1).
8. **Start inside.** prev (0.5, 0.2, 0.6), any move (also zero): hit, fraction 0, type 4, index 0; `+0x90` =
   (0.5, 0.2, 0.6); `+0x60` = (0.5, 0.2, **0.6**) (world z, the local z would be 0.1); `+0x50` = (0.9285, 0.3714, 0, 1);
   `+0x10` = (−0.9285, −0.3714, 0, 1).
9. **Start touching, centre outside.** prev (0, −1.05, 0.5), move (0, 0.1, 0) (the hand overlaps the face by 0.04):
   no hit (the face step returns −1). Next update prev (0, −0.95, 0.5): inside, hit at fraction 0 (case 8's rules).
10. **Zero move, outside.** prev = cur = (0, −1.3, 0.5): no hit. Zero move, centre inside: hit (case 8).
11. **Exactly reaching.** A sphere of r 0.25 (exact in binary), prev (0, −1.5, 0.5), move (0, 0.25, 0): fraction
    (0.25 − 0.5) / −0.25 = 1.0 exactly: a hit (only fraction > 1 is rejected). Move (0, 0.125, 0): fraction 2, no hit.
12. **Rotated box, the frame of `+0x60`.** Box centre (10, 5, 0.5), A0 = (0, 1, 0), A1 = (−1, 0, 0), A2 = (0, 0, 1),
    half (2, 1, 0.5). prev (11.3, 5, 0.5), move (−0.4, 0, 0): local start (0, −1.3, 0), local move (0, 0.4, 0); hit,
    fraction 0.525; `+0x60` = (0, −1, 0) (box frame); `+0x90` = (11, 5, 0.5); `+0x50` = (1, 0, 0, 0) (local (0, −1, 0)
    rotated); `+0x10` = (−1, 0, 0, 1).
13. **Mirror (BoxSphere).** Box B moving by (0, −0.4, 0) against a resting sphere at (0, −1.3, 0.5): fraction 0.525;
    `+0x30` type 0, `+0x34` index 2, `+0x20` = (0, −1, 0), `+0x90` = (0, −1, 0.5) (on the box's start pose; the true
    contact is at y −1.21), `+0x10` = (0, −1, 0, 0), `+0x50` = (0, 1, 0, 1).
14. **Strike integration (issue #72).** A hand sphere switched on this update (flag `0x11`): its first test has a zero
    move, so it hits a car box only if its centre is inside. On the next update, prev = the last centre: a hand moving
    from 0.15 m outside box 1's side to 0.05 m inside hits with the fraction of case 1's kind, and `Car_OnHit` gets
    `+0x60` in box 1's frame (for the cabin, box 2's frame, offset from box 1's).

### Unit 6: Shapes: records, poses and bounds {#unit-6}

- **Specified by:** [Shapes](#shapes), [The human body](#human-body) (capsule height), [Combat: moving
  strikes](combat.md#moving-strikes) (segment and sphere posing). Missing for exact code: the box's bound (`0x003430e8`,
  956 bytes) and the generic bound `0x00341190`, `AABB_FromSphere` versus `0x00341708`, the flag `0x10` reset rule.
- **Coney:** a new `src/physics/shapes.{h,cpp}` (posed shapes as values); `src/human/human.h` capsule height moves here;
  `world_objects/object_bodies.*` builds its box and sphere from it.
- **Tests:** each shape posed from a synthetic bone matrix and offset; bounds of a rotated box against a brute-force
  corner bound; the capsule's height rules (0.61, 0.7, 0.35 cuffed, never under 0.2 × scale).

21 functions, 5272 bytes (1 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00342a30` | `PhysicsShape_Construct` | 60 |
| `0x00342a70` | `PhysicsShape_Destroy` | 48 |
| `0x00342aa0` | `PhysShapeSphere_Construct` | 60 |
| `0x00342ae0` | `PhysicsSphere_Destroy` | 40 |
| `0x00342b08` | `PhysicsSphere_Pose` | 512 |
| `0x00342d18` | `PhysicsBox_Construct` | 124 |
| `0x00342d98` | `PhysicsBox_Destroy` | 40 |
| `0x00342dc0` | `PhysicsBox_SetSize` | 52 |
| `0x00342df8` | `PhysicsBox_Pose` | 580 |
| `0x00343040` | `PhysicsBox_SetMatrix` | 168 |
| `0x003430e8` | `PhysicsBox_GetBounds` | 956 |
| `0x003434a8` | `PhysShapeCapsule_Construct` | 56 |
| `0x003434e0` | `PhysicsCapsule_Destroy` | 40 |
| `0x00343508` | `PhysShapeCapsule_SetSize` | 12 |
| `0x00343518` | `PhysicsCapsule_Pose` (tagged) | 636 |
| `0x003437b0` | `PhysShapeSegment_Construct` | 56 |
| `0x003437e8` | `PhysicsSegment_Destroy` | 40 |
| `0x00343810` | `PhysShapeSegment_Pose` | 632 |
| `0x00341190` | `PhysicsShape_GetBounds` | 812 |
| `0x00341660` | `AABB_FromSphere` | 168 |
| `0x00341708` | `AABB_FromSphereMethod` | 180 |

### Unit 7: Bodies and the sort-and-sweep world {#unit-7}

- **Specified by:** [Bodies](#bodies), [Queries](#queries), [The step](#step) (the bound refresh). Missing:
  `BodyList_Insert`'s tie order, the filtered query's callback and excluded-flag arguments (`0x00340120`, 904 bytes),
  `IPhysics_OverlapShape`'s options beyond `0x80`/`0x100`/`0x200`.
- **Coney:** `src/physics/physics_world.{h,cpp}` (body vector, x-sorted list, `+0x14` bound, 512-candidate cap);
  `play_level_mode` owns one per level.
- **Tests:** insert / remove / reinsert keep the list sorted by min x; a query box finds exactly the brute-force overlap
  set, including a wide body far back in the list (the bound); the bound only shrinks on the 120th tick.

19 functions, 4400 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00341158` | `PhysicsNode_Init` | 52 |
| `0x003418f8` | `PhysicsBody_Construct` | 120 |
| `0x00341970` | `PhysicsBody_ConstructForOwner` | 140 |
| `0x00341a00` | `PhysicsBody_Destroy` | 104 |
| `0x00341a68` | `PhysicsBody_SetOwner` | 64 |
| `0x00341aa8` | `PhysicsBody_AddShape` | 56 |
| `0x00341ae0` | `PhysicsBody_GetShapes` | 8 |
| `0x00341638` | `PhysicsBody_ComputeBounds` | 36 |
| `0x003417c0` | `PhysicsBody_AddShapeBounds` | 128 |
| `0x00341c10` | `PhysicsBody_Update` | 516 |
| `0x00341e40` | `PhysicsBody_Moved` | 84 |
| `0x003404a8` | `BodyList_Insert` | 304 |
| `0x003405d8` | `BodyList_Remove` | 80 |
| `0x00340628` | `BodyList_Reinsert` | 64 |
| `0x00340668` | `IPhysics_AddBody` | 152 |
| `0x00340700` | `IPhysics_RemoveBody` | 156 |
| `0x0033feb0` | `IPhysics_QueryBox` | 624 |
| `0x00340120` | `IPhysics_QueryBoxFiltered` | 904 |
| `0x0033ebb8` | `IPhysics_OverlapShape` | 808 |

### Unit 8: Ignore lists and shape pools {#unit-8}

- **Specified by:** [Ignore lists](#ignore-lists), [IPhysics](#iphysics) (pool sizes). Complete enough; the pool
  allocators are "first slot whose flag `0x8` is clear".
- **Coney:** `src/physics/physics_world.*` (fixed pools with the original's counts, so running out behaves the same).
- **Tests:** 60 ignore slots then a 61st `Ignore` does nothing; removing a body frees every slot naming its owner; the
  human body always ignores its held object and treats a null owner as the level.

14 functions, 1032 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00340890` | `IPhysics_FindFreeIgnoreSlot` | 68 |
| `0x0033cd70` | `IPhysics_ForgetIgnoredOwner` | 140 |
| `0x00341ae8` | `PhysicsBody_IsIgnoring` | 56 |
| `0x00341b20` | `PhysicsBody_Ignore` | 92 |
| `0x00341b80` | `PhysicsBody_StopIgnoring` | 140 |
| `0x003421d0` | `PhysicsHumanBody_IsIgnoring` | 92 |
| `0x00342230` | `PhysicsHumanBody_Ignore` | 48 |
| `0x00342260` | `PhysicsHumanBody_StopIgnoring` | 36 |
| `0x0033d1a0` | `IPhysics_AllocObjectSphere` | 68 |
| `0x0033d1e8` | `IPhysics_AllocObjectBox` | 68 |
| `0x0033d230` | `IPhysics_AllocCarBox` | 68 |
| `0x0033d278` | `IPhysics_AllocBigSphere` | 80 |
| `0x0033d2c8` | `IPhysics_FreeShape` | 16 |
| `0x003408d8` | `IPhysics_FindFreeStrikeSphere` | 60 |

### Unit 9: Creating and freeing bodies {#unit-9}

- **Specified by:** [Bodies](#bodies) (flags per kind), [Cars](#cars), [IPhysics](#iphysics) (the punch bag's set).
  Missing: `Physics_CreateBoxBody` (`0x0033d070`, glass panes) is named only through its caller.
- **Coney:** `src/physics/physics_world.*`; callers in `src/human/human.cpp` (`Human_CreatePhysicsBody` rules: flag
  `0x400`, `+0x60` 1.4286, `0x200`), `src/world_objects/object_bodies.*`, `cars.*`, `glass.*`.
- **Tests:** each kind's starting flags (human `0x8002243f`, punch bag `0x8000243f`, object `0x80000500`, car
  `0x8002231a` after its boxes, pane `0x8000007a`); a freed body's shapes lose `0x8`; the pool's first free slot is
  reused (the heal keeps the same body pointer, as seen at runtime).

12 functions, 1752 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0033cc48` | `IPhysics_CreateHumanBody` | 292 |
| `0x0033ce00` | `IPhysics_FreeHumanBody` | 84 |
| `0x0033cb08` | `IPhysics_CreatePunchBagBody` | 228 |
| `0x0033cbf0` | `IPhysics_FreePunchBagBody` | 84 |
| `0x0033ce58` | `IPhysics_CreateObjectBody` | 168 |
| `0x0033cf00` | `IPhysics_FreeObjectBody` | 100 |
| `0x0033cf68` | `IPhysics_CreateCarBody` | 140 |
| `0x0033cff8` | `IPhysics_FreeCarBody` | 120 |
| `0x0033d070` | `Physics_CreateBoxBody` | 228 |
| `0x0033d158` | `IPhysics_FreeBoxBody` | 68 |
| `0x003407a0` | `IPhysics_AddHumanBody` | 88 |
| `0x003407f8` | `IPhysics_RemoveHumanBody` | 152 |

### Unit 10: The human body set: update, strikes and push sphere {#unit-10}

- **Specified by:** [The human body](#human-body), [Human contacts](#human-contacts), [Combat: moving
  strikes](combat.md#moving-strikes). Missing: `PhysicsHumanBody_Update`'s strike-reach widening in numbers per attack,
  the broad bound `+0x90` users.
- **Coney:** `src/human/human.*`, `src/combat/*` (`Human_TestStrikes` sweeps each enabled strike shape from its last
  pose against the bodies near the human through the sweep table, [How Human_TestStrikes builds the
  sweep](#u4b-how-human-teststrikes-builds-the-sweep-0): [Unit 5](#unit-5) for boxes, [Unit 11](#unit-11) for humans),
  `src/platform/play_level_mode.*` (`strikeObjects()`). Issue #72's step 1 (strikes hit cars) needs only Unit 5 and the
  strike loop, not the whole of this unit.
- **Tests:** bone shapes switch on by bit `1 << (id − 2)`; `Moved` resets the previous positions so a teleport is not a
  strike; the push sphere pushes a bystander 0.25 m × w / (w × k) and not a held (k = 1e9) or unpushable one.

14 functions, 6168 bytes (1 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00341e98` | `PhysicsHumanBody_Construct` | 104 |
| `0x00341f00` | `PhysicsHumanBody_Destroy` | 40 |
| `0x00341f28` | `PhysicsBody_ApplyHumanScale` | 412 |
| `0x003420c8` | `PhysicsHumanBody_GetShapes` | 8 |
| `0x003420d0` | `PhysicsHumanBody_SetPushSphere` | 92 |
| `0x00342130` | `PhysicsHumanBody_OnStruck` | 28 |
| `0x00342158` | `PhysicsHumanBody_AddStrikeSphere` | 16 |
| `0x00342168` | `PhysicsHumanBody_ClearStrikeSpheres` | 100 |
| `0x003422d0` | `PhysicsHumanBody_Update` | 748 |
| `0x003425c0` | `PhysicsHumanBody_PushAwayHumans` | 616 |
| `0x00342828` | `PhysicsHumanBody_Moved` | 168 |
| `0x003428d0` | `PhysBody_SetShapeEnabled` | 280 |
| `0x003429e8` | `PhysicsHumanBody_AddBoneShape` | 72 |
| `0x0033f110` | `Human_TestStrikes` (tagged) | 3484 |

### Unit 11: Moving shapes against bodies: spheres, capsules, segments {#unit-11}

- **Specified by:** [Human contacts](#human-contacts), [The shape-pair tables](#dispatch), [Geometry kernels](#kernels).
  Coney already has the capsule-capsule sweep (`src/human/human_contacts.h`), tagged. Missing: the algebra of
  `Sweep_SphereSphere`, `Sweep_CircleCircle2D`, `Sweep_IntervalOverlap`, `Sweep_SphereCapsule` (the capsule-capsule
  start case is an open item on the page), and `IPhysics_CollideShape`'s option bits.
- **Coney:** `src/physics/sweeps.*`; `src/human/human_contacts.h` moves onto it; `PhysicsBody_SweepHumanMask` replaces
  `Human::meetHumans` so the humans' and the walls' contacts of a pass are sorted together (now a marked Coney choice).
- **Tests:** two capsules head-on meet at the sum of the radii; vertical ranges that never overlap pass; a sphere
  against a sphere at a glancing angle; a pass with a wall contact at 0.6 and a human at 0.3 resolves the human first.

16 functions, 8672 bytes (3 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x003440d0` | `PhysicsSweep_SphereSphere` | 560 |
| `0x00344300` | `PhysicsSweep_SegmentSegment` | 32 |
| `0x00344320` | `PhysicsSweep_SphereSegment` | 536 |
| `0x00344538` | `PhysicsSweep_SegmentSphere` | 224 |
| `0x00345670` | `PhysicsSweep_SphereCapsule` | 668 |
| `0x00345910` | `PhysicsSweep_CapsuleSphere` | 224 |
| `0x00345d18` | `PhysicsSweep_CapsuleSegment` | 32 |
| `0x00345d38` | `PhysicsSweep_SegmentCapsule` | 32 |
| `0x00343cd8` | `PhysicsSweep_CapsuleCapsule` (tagged) | 1012 |
| `0x00349268` | `Sweep_SphereSphere` | 544 |
| `0x003498c0` | `Sweep_CircleCircle2D` | 416 |
| `0x0034a8d0` | `Sweep_IntervalOverlap` | 396 |
| `0x00349a60` | `Sweep_SphereCapsule` (tagged) | 2160 |
| `0x00349180` | `Vec_MakePerpendicularBasis` | 232 |
| `0x0033d498` | `PhysicsBody_SweepHumanMask` (tagged) | 640 |
| `0x0033e7f0` | `IPhysics_CollideShape` | 964 |

### Unit 12: A capsule swept against a box {#unit-12}

- **Specified by:** [Geometry kernels](#kernels) (roles only); the overlap form is exact in
  [Unit 4](#u4-physicsoverlap-capsulebox-00346808). Needs the algebra of the capsule-box sides and corners.
- **Coney:** `src/physics/sweeps.*`; a walking human against an object's box body (today the stand-in push of
  `world_objects::ObjectBodies::pushOut`).
- **Tests:** a capsule swept at a box face and a corner against a brute-force sampled sweep; the vertical ranges that
  never overlap pass.

4 functions, 4436 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00344618` | `PhysicsSweep_CapsuleBox` | 2800 |
| `0x00345108` | `PhysicsSweep_BoxCapsule` | 224 |
| `0x0034a318` | `CapsuleBox_SweepSide` | 900 |
| `0x0034a6d0` | `CapsuleBox_SweepCorner` | 512 |

### Unit 13: Box against box and triangle {#unit-13}

- **Specified by:** [Mesh tests](#mesh-tests), [Geometry kernels](#kernels) (roles only). Needs the separating-axis
  sweep of `Sweep_BoxBox` (4,484 bytes) and `Sweep_BoxTriangle`, and the box's mesh sweep `0x00348530`.
- **Coney:** `src/physics/sweeps.*`, `src/world_objects/loose_objects.*` (a falling object's box then sweeps instead of
  the centre ray stand-in), the car's loose parts.
- **Tests:** a box dropped flat and on an edge onto a floor triangle; two boxes sliding past without touching; the brick
  of [Settling a landed object](#settle) resting at 0.2831 (floor 0.2231 + 0.05 + 0.01).

8 functions, 7848 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x003451e8` | `PhysicsSweep_BoxBox` | 524 |
| `0x00345588` | `PhysicsSweep_SegmentBox` | 8 |
| `0x00345590` | `PhysicsSweep_BoxSegment` | 224 |
| `0x0034c430` | `Sweep_BoxBox` | 4484 |
| `0x0034d5d0` | `Sweep_BoxTriangle` | 872 |
| `0x0034aa60` | `BoxTri_ProjectBox` | 200 |
| `0x0034ab28` | `BoxTri_ProjectTriangle` | 224 |
| `0x00348530` | `PhysicsMesh_SweepBox` | 1312 |

### Unit 14: Rays against bodies {#unit-14}

- **Specified by:** [Queries](#queries) (`IPhysics_RayCastBodies`), [Geometry kernels](#kernels). Needs the slab clip's
  epsilon and which of the two capsule rays is used.
- **Coney:** `src/physics/rays.*`; the AI's line tests (`src/ai/*`) that today cast only into the mesh.
- **Tests:** a ray through a rotated box returns the slab entry; a ray from inside a box; a ray grazing an upright
  capsule's top.

5 functions, 2436 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00349488` | `Ray_ClipSlab` | 212 |
| `0x00349560` | `Ray_OrientedBox` | 864 |
| `0x003459f0` | `PhysicsRay_Capsule` | 412 |
| `0x00345b90` | `PhysicsRay_Box` | 388 |
| `0x0033eee0` | `IPhysics_RayCastBodies` | 560 |

### Unit 15: The physics world: construction, settle and step {#unit-15}

- **Specified by:** [IPhysics](#iphysics), [The step](#step), [Settling a landed object](#settle). Coney's settle
  (`world_objects::LooseObjects`) is tagged; this unit moves it onto the world's 64 slots.
- **Coney:** `src/physics/physics_world.*`, `src/world_objects/loose_objects.*`, `src/platform/play_level_mode.*` (one
  step per 60 Hz tick).
- **Tests:** a settle takes 11 ticks with `t` = 0.01 n (n − 1); 64 busy slots refuse a 65th; cancelling a settle on
  removal.

8 functions, 4392 bytes (3 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0033c288` | `IPhysics_Construct` | 2168 |
| `0x00340918` | `IPhysics_Step` (tagged) | 544 |
| `0x00340b38` | `Settle_NearestAxis` (tagged) | 464 |
| `0x00340d08` | `Settle_ComputeTarget` (tagged) | 724 |
| `0x00340fe0` | `IPhysics_StartSettle` | 272 |
| `0x003410f0` | `IPhysics_CancelSettle` | 104 |
| `0x0034f6c0` | `Physics_StaticInit` | 84 |
| `0x0034f718` | `Physics_StaticInitStub` | 32 |

### Unit 16: RayCast leftovers and the world manager's ray {#unit-16}

- **Specified by:** [Collision](collision.md#materials) (the material names, 191 entries), [Ray
  cast](collision.md#ray-cast) (`WorldManager_RayCast`), [Lifetime](collision.md#lifetime).
- **Coney:** `src/raycast/collision_mesh.*` (a material name table as reference data: names only), callers that today
  cast with mask 0 where the original passes its own mask (an open question on the Collision page).
- **Tests:** the name of ids 0, 1, 2, 116, 190; an id past 190 (what the jump table does with it is to be read first);
  the world wrapper pre-sets material 1 and hit 0.

4 functions, 2636 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0034fc08` | `CollisionMaterial_GetName` | 2340 |
| `0x00350688` | `CollisionMesh_FreeChunks` | 240 |
| `0x00351158` | `CollisionMesh_UpdateEmpty` | 8 |
| `0x0040db88` | `WorldManager_RayCast` | 48 |

### Unit 17: Level file and level object lifecycle {#unit-17}

- **Specified by:** [Level loading](level-loading.md), [Chunk system](chunk-system.md), [The streamed
  world](world.md#small-functions).
- **Coney:** `src/world/level_object.*`, `src/platform/level_file.*`.
- **Tests:** a synthetic WAD index finds, sizes and misses entries; the level object's destructor frees the collision
  chunks (no leak under the synthetic chunk stack).

10 functions, 1028 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0040c5e0` | `Wad_Open` | 68 |
| `0x0040c628` | `Wad_FindEntry` | 28 |
| `0x0040c648` | `Wad_GetEntrySize` | 32 |
| `0x0040c668` | `Wad_HasEntry` | 32 |
| `0x0040c688` | `WorldLevel_Load` | 356 |
| `0x0040c7f0` | `WorldLevel_Release` | 116 |
| `0x0040cf18` | `LevelHeader_RegisterChunkHandler` | 40 |
| `0x0040cf40` | `LevelObject_Construct` | 60 |
| `0x0040cf80` | `LevelObject_Destroy` | 264 |
| `0x0040d088` | `LevelObject_SetOccluders` | 32 |

### Unit 18: World manager lifecycle {#unit-18}

- **Specified by:** [Level loading](level-loading.md), [Memory](memory.md) (the pools), [The streamed
  world](world.md#streaming).
- **Coney:** `src/world/world_streamer.*`, `src/platform/game_session.*`, `src/platform/play_level_world.cpp`.
- **Tests:** load then unload returns every pool to empty; `ResetVisibility` clears the part flags; the load priority
  setter stores its value (whether it clamps is to be read first).

10 functions, 3700 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x0040d688` | `WorldManager_CreatePools` | 628 |
| `0x0040d900` | `WorldManager_Construct` | 644 |
| `0x0040dbb8` | `WorldManager_LoadLevel` | 1280 |
| `0x0040f5b8` | `WorldManager_Unload` | 664 |
| `0x0040e0b8` | `WorldManager_ResetVisibility` | 68 |
| `0x0040e1a0` | `WorldManager_QueuePack` | 308 |
| `0x0040e8d0` | `WorldManager_GetSolidWorld` | 8 |
| `0x0040f850` | `WorldManager_GetSectorPool` | 80 |
| `0x0040c938` | `WorldManager_SetLoadPriority` | 12 |
| `0x0040cc68` | `PVS_CaptureSamplePoint` | 8 |

### Unit 19: Streamed world object lifecycle and sector callbacks {#unit-19}

- **Specified by:** [The world object](world.md#world-object), [Visibility](world.md#visibility),
  [Lighting](world.md#lighting).
- **Coney:** `src/world/streamed_world.*`, `src/platform/world_set.*`, `src/platform/world_renderer.*`.
- **Tests:** a part marked loaded joins the visible set; releasing a sector's atomic; the material alpha callback set on
  every material of a sector.

7 functions, 1684 bytes (0 tagged).

| Address | Name | Size |
| --- | --- | --- |
| `0x00410308` | `World_Construct` | 588 |
| `0x00410558` | `World_Destroy` | 236 |
| `0x004114d8` | `World_PartLoaded` | 248 |
| `0x004118b8` | `WorldSector_ReleaseAtomic` | 156 |
| `0x00411958` | `Material_SetAlphaCallback` | 56 |
| `0x00411b98` | `World_VisibilitySectorCallback` | 376 |
| `0x004102f0` | `Atomic_SetModulateMaterialColour` | 24 |

## Coney's implementation

Coney has the walking body's sweep (`src/human/body.h`, `src/raycast/`) and placed world objects
(`src/world_objects/`), but no `IPhysics`. A dropped object's fall, bounces, settle and stop are
`world_objects::LooseObjects` (`repo:src/world_objects/loose_objects.h`), stepped by the play mode: each 1/30 s step
runs the object's update, then two settle ticks, so the settle keeps its 11 ticks. Its move is a ray from the body's
centre against the level mesh, as long as the move plus the box's or sphere's reach that way; humans, cars and other
objects are not met yet. A thrown object starts the same way with its throw's velocity and spin
([World objects](objects.md#coneys-implementation)).

The world objects in the world have their bodies (`world_objects::ObjectBodies`,
`repo:src/world_objects/object_bodies.h`): each type's box or sphere at its object's pose, with its `PHYFLAG` layers,
remade each step before the humans move. A walking human's sphere slides along the `BLOCKHUMANS` ones after the
level's walls, as `Human_OnContact` slides along an object (stand-in: pushed out of the deepest body the move goes
into, in the ground plane, and then out of the walls again; the object does not move).

The walking body's wall test (`src/human/body.cpp`, `nearestWallPush`) is a push-out at the end of the move, not a
sweep: it decides which side of a wall the sphere is on, and flips a two-sided wall, from the centre **after** the
move, where the original uses the centre before it ([Sweeping a body](#sweep)). For walls a step never crosses
(moves are under 0.3 m and the radius about 0.47 m) the two agree; the hangout's doors that the player walked through
one way were not this but the spawn's lost triangle flags, now fixed ([Objects](objects.md#coneys-implementation)).

The shape-pair code of [Unit 4](#unit-4) and [Unit 5](#unit-5) is in `src/physics/`, as functions of posed shapes
(`physics::Box`, `Sphere`, `Capsule`, `Segment` in `repo:src/physics/shapes.h`) that fill a contact record
(`physics::Contact`, `repo:src/physics/contact.h`, laid out as the 0xc0-byte record's fields): the static overlaps
and their table (`repo:src/physics/overlaps.h`), the segment and capsule-box kernels
(`repo:src/physics/segment_kernels.h`) and the sphere-box sweep (`repo:src/physics/sphere_box_sweep.h`), with
every quirk the units list kept and pinned by a test. Nothing calls them yet: the strike loop (issue #72, step 1)
calls `physics::sweepSphereBox` with a car's or an object's box (`physics::boxAt` turns a body's pose and half
extents into one), and the overlap query ([Unit 7](#unit-7)) calls `physics::overlapShapes`. Coney's choices, each
marked in the code:

- `PhysicsOverlap_BoxCapsule` writes the capsule-side normal (the box normal negated) into `+0x50`, which the
  original leaves holding whatever the stack held.
- The sphere-box sweep's direction to the box, and the inside case's normal, are (0, 0, 0, 1) for a sphere straight
  above or below the box centre, where the original normalises a zero vector.
- The ray roots are computed as C / (D − B), the same root as the original's form, and exact at a tangent where the
  original divides by zero.
- `PhysicsOverlap_BoxBox` keeps the original's wrong matrix (a maintainers' choice in
  [Open questions](#open-questions); no caller pairs two boxes).

Humans meet each other as [Human contacts](#human-contacts) describes (`human::sweepCapsules` and
`human::contactPush`, `repo:src/human/human_contacts.h`; `Human::meetHumans`). The characters' step
(`human::Humans::update`) gives every human the step's list of humans, and a walking human's sweep first meets their
upright capsules (0.35 × scale, 2 m tall): the nearest contact stops the capsule there and the rest of the move
slides along the other human (code 1), and, unless the mover charges, the one met gets half of the move into him over
his push weight (1e9 while in a pair or arrested) as a pending push for his own next move (code 4), unless a script
made him unpushable (`HuSetPushable(false)`). A player's velocity loses its part into the human, as against a wall.
Skipped: a human without `BLOCKHUMANS` (`Human::blocksHumans`), out of health or out of the world, and the human this
one holds or is held by (a grab, tackle, mount or mugging), so a linked pair passes through each other. The flag goes
with a knock-out (`HuSetConscious(false)` or the health running out), a wound and an arrest, and comes back only with
a heal (`HuRevive`, a flash, a freed cuffed human's revive) or the release from an arrest; a knockdown keeps it, as
in the original ([Human contacts](#human-contacts), "Who loses `BLOCKHUMANS`"). What differs, each marked in the
code:

- **Coney's choice**: the humans take their part of the move before the walls take theirs, where the original sorts
  both kinds of contact of a pass by fraction together; a capsule already within reach is met at fraction 0 by a move
  that goes further in (the capsule sweep's start case is not traced).
- **Not yet**: an airborne body meets no human (a jump's strike and code 1 against a human); a scene's shared group;
  the pushed human's reaction (`Human_OnPushContact`'s timers and damage).

## Open questions

- **Not yet seen at runtime** from [the units](#units): the late or missed contacts at a triangle's corners
  ([Unit 1](#u1-capsule), a hook on `0x0034e450` with a sphere driven into a wall's corner would show them); the
  capsule sweep's cell-list overrun after a fraction above 1 ([Unit 2](#u2-00347c08-physicsmesh-sweepcapsule)); that
  a strike shape's previous position is the last update's pose (a watch on a hand sphere's `+0x20` and `+0x50` over
  two updates).
- **Bits and flags the units read but cannot name:** triangle flag bit 3 (`0x8`), which a sphere sweep skips for a
  body with flag `0x100` or a query with option `0x8000` (every world object's body starts with `0x100` in
  `0x80000500`, so a flying object's sphere passes those triangles; inferred); how many triangles carry each corner
  edge in bits 14-15; which sweep, if any, writes contact flag `0x2`; which states `0x00227f68` selects for the
  landing segment's raised feet. Body flag `0x1000` is set by `PhysicsBody_PushOutOfWalls` (`0x00347b70`); other
  setters are not traced.
- **Callers not traced:** which `IPhysics_CollideShape` callers pass the sphere sweep's options `0x40`, `0x8000` or
  `0x20000`; whether anything but `IPhysics_OverlapShape` reads the overlap table; whether the camera (the other
  caller of `Sweep_SphereOrientedBox`) reads its normals, six of which point the wrong way
  ([The normal tables](#the-normal-tables)); how a held weapon's strike sphere reaches `Car_OnHit` as kind 1 (its hit
  goes to the held object's contact handler).
- **Names that do not fit** (kept, as other analysts gave them): `CollTri_ComputeEdgeNormals` (`0x00343b20`)
  normalises the edges; `Ray_InfiniteCylinder` (`0x0034d938`) is a capsule; `SphereBox_SweepCorner` (`0x0034b040`) is
  the face-region step, `SphereBox_SweepFaceEdges` (`0x0034b7b0`) the corner-region step and `SphereBox_ToBoxSpace`
  (`0x0034baf0`) goes from the box to the world. The plate comments say what each does.
- **A maintainers' choice:** `PhysicsOverlap_BoxBox` (`0x00345dd8`) builds its rotation matrix the wrong way round
  with the centres mixed in, so it misses rotated or distant boxes; no caller pairs two boxes, so reproducing it or
  writing the correct test changes nothing a player sees ([Unit 4](#u4-physicsoverlap-boxbox-00345dd8)).

- Hangout railing vault (checklist item 46: the vault plays but the player stays put): not explained by the door
  flags above, which only doors and panes write. The climb's pass-through (materials 30, 31 and 122 ignored while
  record `+0x08` has `0x40`, [Characters](characters.md#walls)) and the railing triangles' material in `level95.lev`
  are where to look next; not traced in this pass. A lead: of the clubhouse's triangles (z between −200 and −185 in
  `level95.lev`) only 2 are material 122 and 7 material 31, against 137 of 7 (`LAMPOST`) and 56 of 3 (`STEEL`), so a
  railing built of another material keeps its wall during the vault unless the original passes it some other way.

- How an object of a type with `axis` 0 comes to rest: nothing in its contact handler clears airborne, so it would
  keep bouncing at interval 2 (inferred); the throwable ones (`dyn_lawnchair_a`, `_b`) break on their first contact
  (runtime).
