# Cars

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), and a disc check (2026-10-06) of the car models and the Object List, reported as counts, sizes and frame
positions, with Coney's renders of the models. No runtime claims.

## Purpose

The parked cars scripts make with `CarSpawn`: their six types, the per-type record of boxes and parts, what the part
ids of `CarRemovePart`, `CarSetPartOpen` and `CarSetPartDamage` name, the paint colour of `CarSetColor`, and where a
car's model and textures are. The lists are in [Cars](../references/cars.md); the car's task class and pool are on
[Tasks](tasks.md#classes).

In one paragraph: a car is a task of `0x1310` bytes (18 of them). Its initialiser matches the name against six type
names and keeps the index; the index selects a `0x5f0`-byte record of constants that gives two physics boxes and 26
**parts** (body, roof, bumpers, bonnet, boot, panels, lights, four doors with their windows, four wheels). Parts can be
opened, damaged and removed, and removing a door removes its window. The model is a RenderWare clump named
`<type>_geo` with 47 atomics, found through the Object List of `warriors.glr`.

## Original structure

`Car.cpp` holds only four functions at `0x00173170`-`0x00173928` (`CarInstance`, [Source map](source-map.md)); the car
task's code is at `0x00387498`-`0x0038e0f0`, in `TaskEngine/` ([Source map](source-map.md#position), inferred).
Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00387bc8` | `Car_Init(car, name)` | type index from the name, physics, the police lights, a handle | confirmed (code) |
| `0x00387d50` | `Car_MakeBodies` | two physics boxes from the type record | confirmed (code) |
| `0x0038c7d8` | `Car_RemovePartBits(car, part, on)` | marks a part (and its linked part) removed | confirmed (code) |
| `0x0038d020` | `Car_OpenPart(car, part, open)` | opens or closes a door, the bonnet or the boot | confirmed (code) |
| `0x0038df38` | `Car_SetColour` | `CarSetColor` | confirmed (code) |
| `0x00389b40` | `Car_UpdateRender` | when dirty, recomputes the model's colour and transform | confirmed (code) |

## Data

### Types {#types}

`0x00512ba8` holds six string pointers, in index order: `car_osedan`, `car_coupe`, `car_wagon`, `car_copcar`,
`car_van`, `car_sullycar`. `Car_Init` compares the name with each (`strcmp`, `0x00435ba0`) and stores the first match's
index at `+0x11e0`; a name that matches none leaves the index unset. Index 3 also spawns `part_copcar_lights` at the
car (name at `0x0057e248`) and keeps its handle at `+0x1210`. `+0x12f0` is the CRC-32 of the name
(`0x00143f68`). Confirmed (code).

### Type record {#type-record}

`0x0057e4c0 + 0x5f0 × type`, read-only data. Confirmed (code) for the reads named; the meaning of each field is inferred:

| Offset | Size | Meaning |
| --- | --- | --- |
| `+0x00`, `+0x10` | vec3, vec4 | first physics box: size and offset (`Car_MakeBodies`); the sedan's 2.40 × 6.05 × 1.08 m |
| `+0x20`, `+0x30` | vec3, vec4 | second box: size and offset (the sedan's 2.40 × 2.61 × 0.45 m, the cabin) |
| `+0xf0 + 0x30 × p` | 26 × 0x30 | the parts |

A part record, `p` < 26:

| Offset | Meaning |
| --- | --- |
| `+0x00` | side bits (inferred from the data: `0x02` front, `0x20` back, `0x10` and `0x01` the two sides, `0x04` top) |
| `+0x01` | how it comes off: `0x10` at the first damage, `0x02` flies off from a hit, `0x04` flies off when blown off ([What is drawn](#drawn)) |
| `+0x02` | the part removed with it, `0xff` none (`Car_RemovePartBits` reads `0x0057e5b2 + 0x5f0 × type + 0x30 × p`) |
| `+0x03` | `0x01` on the doors, `0x10` on the bonnet and boot |
| `+0x10` | the part's box, vec3 in metres |
| `+0x20` | float: 0.1, 0.5 on the wheels |

The part ids with what we read them to be (inferred from the side bits and boxes): 0 body, 1 roof, 2 and 3 the front
and back bumpers (2.36 m across), 4 bonnet, 5 boot, 6 and 7 front and back panels, 8 and 9 headlights, 10-13 side
panels, 14, 16, 18, 20 doors, 15, 17, 19, 21 their windows (linked: removing door 14 removes 15, and so on), 22-25
wheels (0.76 m across).

### The car object {#car-object}

Fields of the car task besides the [task head](tasks.md#task-object), confirmed (code) at the functions named:

| Offset | Meaning |
| --- | --- |
| `+0x1a0 + 0xa0 × p` | state of part `p`; byte `+0x96` bit 2 open, bit 3 changed (`Car_OpenPart`) |
| `+0x11e0` | type index |
| `+0x11e4`, `+0x11e8`, `+0x130c` | the two physics boxes and their body (`Car_MakeBodies`) |
| `+0x1a0 + 0xa0 × p + 0x90` | the part's damage, 0 to 1 (`0x0038a4d8`) |
| `+0x11f0` | removed (hidden) parts, one bit per part (`Car_RemovePartBits`, a quiet explosion) |
| `+0x11f4` | parts flying off as loose pieces (`0x0038a010`); `Car_OpenPart` refuses them |
| `+0x11f8` | parts that are off: damage reached 1, or removed |
| `+0x1208` | the handle of whoever last damaged it, scored when it explodes |
| `+0x12d5` | exploded (`Car_DoExplode`; `Car_TryExplode` refuses a car with it) |
| `+0x12e8`, `+0x12ec` | paint colour, two copies (`Car_SetColour`) |
| `+0x1300` | the render object; `+0x1308` dirty |

### Model and textures {#model}

The car's model and textures are found by name through the **Object List** of `warriors.glr`
([WAD contents](formats/wad-contents.md#object-list)): the record whose name hash is the CRC-32 of the type name gives
the model `<type>_geo` (chunk `0x47`, a RenderWare clump read by the same reader as other models, `0x0017f2c0`) and a
texture dictionary. Disc check: all six models are clumps of **47 atomics** and 48 frames, with no frame names; the
coupe and the wagon use `car_osedan_tex`, the police car `car_copcar_tex`, the van and Sully's car dictionaries whose
names are not recovered (hashes `0x5230bc4b`, `0x3827d44a`). Each model is in one standalone entry and 3-56 packs.
That the car uses this Object List record is inferred (the record's name and model hashes match; the lookup is not
traced).

**The atomics** (disc check of all six, the same layout in each): every atomic has its own geometry (47 geometries) of
one untextured material, and its own frame; frame 0 is the root, frame 1 (the body's) sits on it at the origin, the
parts' frames hang from frame 1, and each window's frame hangs from its door's. The models stand with z up and their
front towards +y (the bumper of part 2 and the headlights at y ≈ +2.8 m on the sedan).

| Atomics | What | Evidence |
| --- | --- | --- |
| 0-25 | part `p` is atomic `p`: the undamaged car | confirmed (code) for the wheels: `Car_UpdateRender` places atomics 22-25 (`0x16 + i`) from the car's four wheel matrices at `+0x70`; inferred for the rest (each frame sits where the part's side bits put it) |
| 26-46 | the damaged form of part `p` (1-21) is atomic `p + 25`, on a frame at almost the same place | inferred: `Car_OpenPart` turns atomics `p` and `p + 25` together (`0x0038cb70`, a hinge turn of ±45° about an axis the low four bits of the part record's `+0x03` byte pick); drawn alone they are the crumpled car |

The body (part 0) and the wheels have no damaged form; `Car_OpenPart` only acts on parts 4, 5, 14, 16, 18 and 20, so
`p + 25` never reaches a wheel.

## Behaviour

### Parts {#parts}

- **Remove** (`CarRemovePart(car, part, on)` → `0x0038c7d8`): with `on`, set bit `part` in `+0x11f0` and `+0x11f8`, and
  the linked part's bit in `+0x11f0`; without, clear the bit. Scripts remove parts 5, 17, 18, 19 and 21. Confirmed
  (code).
- **Open** (`CarSetPartOpen(car, n, open)` → `0x0038d5b8`): `n` 0-5 names parts 5, 4, 14, 16, 18, 20.
  `Car_OpenPart` does nothing for any other part, for a removed one or one marked in `+0x11f4`; it sets bit 2 of the
  part state and, once the model is loaded, calls `0x0038cb70` (a hinge turn, [the atomics](#model)) for atomic `p`
  and, when `p` ≤ 21, `p + 25`. Confirmed (code).
- **Damage** (`0x0038a4d8(amount, car, part record, part, instant, flag)`): adds `amount` to the part's damage
  (`+0x230 + part × 0xa0`; `instant` sets it to 1); at 1.0 the part comes off (bit in `+0x11f8`). Callers: the hit
  handler `0x0038bea0` (0.34 per hit from a weapon or thrown object, 0.51 from a human with flag `0x400000`, 0.115
  from other humans, on the parts the hit's zone selects), `CarSetPartDamage`, `0x0038b0e8` (mask `0x2a80c0`), and
  `Car_DoExplode` (`0x0038ab50`, every part, instant). Confirmed (code).
- **Boot item** ([`CarPlaceInTrunkOnDetach`](../references/bindings/world.md#carplaceintrunkondetach)): when part 5
  comes off through a non-instant damage call and `+0x12e4` is set, `Car_ReleaseTrunkItem` (`0x0038d188`) moves the
  stored object to the boot, or makes a `dyn_money` pickup (type 0x1c) holding the stored amount there. An explosion
  (instant) or `CarRemovePart` (bits only) never releases it. A player picking up `dyn_money` gets that much money
  (`0x0023bf00`, item 2). Confirmed (code).

### What is drawn {#drawn}

Each atomic is drawn or not by the part it belongs to (`0x00172940`, `0x00172d60`; atomic *a* > 25 is the damaged
form of part *a* − 25). Confirmed (code):

- a part in `+0x11f0` (removed) draws neither form;
- the **undamaged** atomic draws while the part's damage is below 0.5, and a wheel's (22-25) always;
- the **damaged** atomic (`p + 25`) draws once the damage is 0.5 or more.

So a part swaps to its crumpled form at half damage and the body (part 0) never changes. The parts 6, 7, 15, 17, 19
and 21 (mask `0x2a80c0`) are drawn in a separate pass (the render object's `+0x41`; inferred: the glass); a part
must also be in the mask `+0x11fc` (not traced). A part that comes off without being removed keeps drawing its
damaged form where it was, unless it flies off (below).

**Coming off** (`0x0038a4d8`, damage reaching 1): the part's bit goes into `+0x11f8`; a door (14, 16, 18, 20) taken
off by a hit also takes its window; part 5 releases the boot item ([Parts](#parts)). Then by the part record's byte
`+0x01`: `0x10` makes any damage take it off at once; `0x02` (a hit) or `0x04` (an instant removal, the explosion)
makes it **fly off** (`0x0038a010`): bit in `+0x11f4`, a lifetime of 360 at the part's `+0x94`, and a velocity
outward from the car (a hit: the record's side bits pick the direction, 3-6 m/s and a small spin; the explosion: away
from the car's centre and up, 12-16 m/s, a larger spin, with a `sub_flaming_debris` particle system on the piece).
A quiet explosion puts those parts in `+0x11f0` instead (hidden, no piece).

### Explosion {#explode}

`CarExplode(car, quiet)` → `Car_TryExplode` (`0x0038ab18`, nothing when `+0x12d5` or `+0x1200` is set) → `Car_DoExplode`
(`0x0038ab50`). Confirmed (code):

1. **Without quiet**: particle systems `sub_fireball_emitter` and `part_explosion_screen_shake` at the car; the car's
   sound (`0x00110258` with `+0x12fc`); object `+0x38` = 5.0 and the airborne flag (`+0x54` `0x4000000`).
2. **Every part** 1-29 not already off, removed or flying: parts 1-25 take damage 1 at once (above, so each one
   draws its damaged form, and those with record flag `0x04` fly off, or hide when quiet); parts 26-29 are only put
   in `+0x11f0` and `+0x11f8`. Each one's bit also goes into `+0x11ec`.
3. **Without quiet**: whoever is in `+0x1208`, when a player controls him, scores statistic `0xb` with half the parts
   counted (at most 10), and `+0x1208` is cleared; a heat-wave effect (`0x0019c1d0`) 0.7 m above the car; the car
   is updated every tick; it sends itself **message `0x19`** (below); an AI event `0x17` to the humans within 30 m
   (`0x002936a8`); 300 damage to every human within 5 m with a clear line (`Explosion_DamageHumansInRadius`,
   `0x00392638`); glass broken within 10 m (`World_BreakGlassInRadius`, [World objects](objects.md#pane)).
4. **Always**: the first physics box shrinks 0.25 m in height and its offset rises 0.25 m (the wreck sits lower on
   its collision); `+0x12d5` = 1; the object the car holds at `+0x1204` (not traced) is released (its vtable `+0x4c`);
   flags of activities 13 (`hubcap_idle`) and 26 (`write_on_pad_C`) within the car's reach + 0.25 m and of activity 3
   (`wreck_idle`) within 0.2 m are switched off (`Flags_DisableNear`).

So an exploded car draws its body, the damaged form of every part that did not fly off, and its four wheels; the
pieces that flew off are drawn on their own until their lifetime runs out (inferred from the lifetime field).

**Message `0x19`.** The record: message `0x19`, subject the car, `+0x00` = 0 (no other object), the short `+0x04` =
−1, the short `+0x06` = 1. The marshaller ([Scripts](scripting.md#message-handlers)) gives the callback
`(car, NilHandle, -1, 1)`: *n* is the signed short, and the flag is pushed as the number 1 (`+0x06` ≠ 0; the script
system's slot `+0x54` pushes an unsigned integer as a Lua number), not a boolean. Confirmed (code) at `0x0038adfc`
and `0x00384ce0`.

**Every sender of `0x19`** (the only two; confirmed (code) at `0x0038bea0` and `0x0038adfc`). The callback always
gets `(car, other, n, flag)`: `other` the human (or `NilHandle`), `n` the signed short `+0x04`, `flag` the short
`+0x06` as 1 or 0.

| When | `other` | `n` | `flag` |
| --- | --- | --- | --- |
| a hit damages part *p* (1-25) that was not yet broken ([the hit](#windows), step 4), once per part | the human responsible: the striker, or the thrower or holder of the object | *p* | 1 if this hit broke the part (a window always; a door or panel when its damage reaches 1), else 0 |
| that hit leaves every breakable part of the car's type broken (mask `0x3ffffe`; `0x3fffe` for type 1, `0x3fff7e` for type 4) | the same human | −2 | 1 |
| an object of class 8 (a molotov, inferred) hits the car: it catches fire and explodes 120 updates later (`+0x1200`) | the thrower | −1 | 0 |
| the explosion itself (`Car_DoExplode`, step 3), unless quiet | `NilHandle` | −1 | 1 |

A hit with no human behind it (an object nobody threw or holds) sends no per-part message. Parts 26-29 send none.
The same hits also send [message 6](scripting.md#triggers) to the volume boxes around the human, with the car as
the object. So `level34`'s riot meter (3 points per call with `flag` 1) counts each part a Warrior breaks, the
all-broken call and the explosion; `level5`'s car bonus counts 15 broken parts or the explosion (`n` = −1).

**Who hears it.** The car's message slot (vtable `0x00544c08` `+0x44`, `0x00389700`) only forwards to the car
manager's table (`0x00512c7c + 0x844`, `0x0038ebf8`): the callback `SetGeneralCarMsgHandler` stored for that number
(`+0x18 + 4 × message`) runs with the car as `self`. A car has **no handler component**: its vtable `+0x3c` (get)
returns 0 and `+0x34` (set) does nothing, so [`SetMsgHandler`](../references/bindings/script.md#setmsghandler) on a
car takes a component from the pool, keeps the name in it and is never called. Only the general handler hears a car's
messages. Confirmed (code) at `0x00389700`, `0x003860b8`, `0x004da1a8`.

### Windows, hits and the stereo {#windows}

A car's windows are **its parts 15, 17, 19 and 21**, not glass panes: no code makes a pane for a car (the only
caller of the pane spawn `0x0039c0e0` is `SpawnBreakableGlass`, `0x00378fa0`), and `level99` places no type-12 pane.
Glass type 12's stereo rule ([A pane's life](objects.md#pane)) only serves panes a script places itself. Confirmed
(code) at the functions cited.

**The windows.** Each window part's record (`+0x01` = `0x18`) has bit `0x10`, which makes any non-instant damage set
the part's damage to 1 at once (`0x0038a4d8`): **one hit breaks a window**. Parts 14, 15, 18 and 19 have side bits
`0x10` and lie on the car's **−x** side (left, with the front at +y); 16, 17, 20, 21 have `0x01` and lie on +x. The
zone tables below put 14/15 and 18/19 at x ≤ −0.6, and the burst of a breaking window 15 or 19 is aimed along −x of
its frame (`0x0038a830`). The sedan's window boxes (part `+0x10`) are 0.44 × 0.96 × 0.25 m (15, 17) and 0.44 × 0.89 ×
0.32 m (19, 21); where each sits is its atomic's frame in the model ([Model](#model)).

**Hit zones.** Which parts a hit reaches comes from where the hitter stands, in the car's frame (x across, y along,
from the car's position `+0x10` and quaternion `+0x20`). Two tables per type, both read at `0x0038b8d0` and
`0x0038b520`; offsets are from the type record:

- **Body**: the band of y among the five falling thresholds at `+0x40` (0 above the first ... 5 below the last) and
  the column of x against `+0x54` = −0.6 and `+0x58` = 0.6 (0 at x ≤ −0.6, 1 up to 0.6, 2 beyond) pick a u32 part
  mask at `+0x74 + 4 × (3 × band + column)`.
- **Cabin**, only while |y| < half the second box's length (`+0x24`): the band among the five thresholds at `+0x5c`
  and the column against `+0x70` = 0 (0 at x ≤ 0, else 1) pick a mask at `+0xbc + 4 × (2 × band + column)`; it is
  ORed in.
- Bit 1 (the roof) is always dropped.

The sedan (`car_osedan`, type 0), as parts per band, x ≤ −0.6 / middle / x > 0.6 (confirmed (code) from the data):

| Body band (y) | Parts |
| --- | --- |
| ≥ 2.716 | 2, 8, 10, 26 / 2, 4, 8, 9 / 2, 9, 11, 27 |
| 1.028 to 2.716 | 10 / 4 / 11 |
| −0.034 to 1.028 | **14, 15** / 14-17 / **16, 17** |
| −1.597 to −0.034 | **18, 19** / 18-21 / **20, 21** |
| −2.778 to −1.597 | 12 / 5 / 13 |
| below | 3, 12, 28 / 3, 5 / 3, 13, 29 |

| Cabin band (y) | x ≤ 0 | x > 0 |
| --- | --- | --- |
| ≥ 1.207 | 6 | 6 |
| 1.007 to 1.207 | 6, **15** | 6, **17** |
| 0 to 1.007 | **15** | **17** |
| −1.007 to 0 | **19** | **21** |
| −1.207 to −1.007 | 7, **19** | 7, **21** |
| below | 7 | 7 |

Bits 26-29 have no part record (the hit handler only marks them removed); 6 and 7 are hit only from the cabin table,
which reads as the windscreen and the rear window (inferred). The other types share the masks with their own
thresholds (`+0x40` / `+0x5c`): the coupe's rear body band repeats its two doors (14-17), the wagon adds 12 and 13 at
the back, and the van's masks differ more; dump them by type when needed. The sedan's box is 2.40 × 6.05 m (`+0x00`),
the cabin 2.61 m long (`+0x24`).

**Targeting a car** (`Player_PickTarget`, `0x0027a6c0`, [Combat](combat.md#targets)). The car pass (world `+0x844`,
`0x0038e860` with reach 1.0 and filter `0x00279f50`) comes after the human passes and before the objects (`+0x840`)
and glass (`+0x84c`). Confirmed (code):

1. A car is a candidate when the human is less than **1 m** outside its box along both its x and y (|x| − half
   width < 1 and |y| − half length < 1), scored by the smaller of the two, squared.
2. The filter `0x00279f50`: a human standing within the box's footprint (`0x00279e00`, i.e. on the car) needs the car
   0-2 m below; any other needs the car within 3 × 54° of the search heading (`0x002790a8`) and within 2 m in height
   (`0x00510970`).
3. The **aim point** (`0x0038c990`): when the car's position is below the human's feet, the feet − 0.25 m; otherwise
   the parts the human can hit from where he stands (`0x0038b520`), less the removed ones (`+0x11f0`, `+0x11f8`,
   `+0x11f4`), must be non-empty, and the point is **1 m ahead of the human** (his facing × (0, 1, 0)), at his feet
   **+ 1.5 m** when those parts include a window or 6 / 7 (mask `0x2a80c0`), + 1.0 m for the bonnet or boot (`0x30`),
   else at the feet. So a window target sits 1.5 m up, and `Player_ObjectAttack` plays 662 for it
   ([Breakables](combat.md#breakables)).
4. `0x0038b520` takes the nearest face of the box from outside it (±x beside the car, ±y before or behind it, the
   corner's direction diagonally) and returns parts only when the human faces that face (his forward · the face's
   outward normal ≤ −0.7); then the zone tables above, from his position.

**The hit.** A human's strike that touches a car does not send message 1. `Strike_Contact` (`0x0021b290`) finds the
struck object is a car (`0x00389848`: its type word has `0x100000`) and calls the car's hit handler directly (vtable
`0x00544c08`, {delta, fn} at `+0x100` / `+0x104`: `0x0038bea0`), which returns the parts it hit. Confirmed (code).
The handler:

1. Ignores the hit while the car is exploding (`+0x12d5`, `+0x1200`); a car whose s16 `+0x12d8` is not `0x19` takes
   hits only from members of the gang with that id (gang `+0x2c`).
2. Picks the parts: for a plain human hit (record `+0x08` without `0x400000`), from **where the attacker stands**
   (`0x0038b8d0`, the tables above, no facing test; a player above the car's position skips the cabin table for
   the inner bands); for a human with `0x400000` (the charge, inferred) and for thrown objects, from the contact
   point (`+0x60`) in the body or cabin table by the physics box struck. Thrown objects drop the windows from body
   hits (`0xffd57fff`) and the doors from cabin hits (`0xffeabfff`).
3. For a plain or object hit, an intact window protects its door: with 15, 17, 19 or 21 in the mask and not yet
   broken (kept bits `+0x11f8`), its door (14, 16, 18, 20) is dropped from this hit. So the **first square at the
   front-left door breaks window 15 only**; later ones dent door 14.
4. Damages each part (0.115 a plain human hit, 0.51 a `0x400000` hit, 0.34 an object; the windows break at once),
   broadcasts message `0x19` per part, plays a part's first-hit effect (`0x0038a830`: windows 15/19 effect 4 along
   −x, 17/21 along +x) and reports to `0x002936a8` (30 m) and `0x00413018` (not traced).

`Strike_Contact` then counts a player's car hit (statistic event `0xb`) when it reached a part not already broken.

**The stereo** (`CarSpawnRadio` → `0x0038c868`, once per car, handle at `+0x1204`). The `dyn_carstereo` record is
added to the world objects (`+0x840`) at the **car's position + its rotation × (−0.75, 0.25, 0.1)**: the vec4 at
`0x0057e4a0 + 0x5f0 × type`, the same for all six types. Its object flag `0x8000` (pickable) is **cleared**, so it
cannot be taken yet. When part **15** reaches damage 1 (`0x0038a4d8`), the stereo gets `0x8000` back and its
virtual at `+0x124` with (3, 0, 0), the world object's context registration (`0x00391c98` in the shared world-object
vtable `0x005453a0`, [Scripts](scripting.md#message-handlers)), which registers it as a **kind-3 context record**
([Crimes](crimes.md#context-records)): triangle within 2 m now offers the theft. Only window 15 frees it, the window
beside the stereo. Confirmed (code). The car's transform update (`0x0038bb48`)
moves the stereo to the same offset, and the boot item to the vec4 at `0x0057e4b0 + 0x5f0 × type` (sedan and Sully's
car (0.00, −2.34, −0.01), police car (0.00, −2.26, −0.02), wagon and van 0, the coupe (0.00, −2.34, −0.05)) while
the boot is shut. Both vectors are 0x20 and 0x10 bytes before the type record as this page counts it. The cars of
`level99` stand at z 1.04-1.09, so the origin is about 1 m above the ground and the stereo about 1.15 m up.

### Colour {#colour}

`CarSetColor(car, {c1, c2, c3, c4})` turns each number into a byte (× 255, `0x0017aca8`) in the order given and stores
the four bytes as one word in `+0x12e8` and `+0x12ec`, marking the car dirty. `Car_UpdateRender` passes both words to
`0x00338240`, which spreads the bytes in reverse order (`pextlb`, `prevh`) and blends the two, and stores the result in
the render object (`+0x24`). So `c1` is alpha and `c4` red (inferred from the reversal; the scripts always pass 1 as
`c1`). Confirmed (code) for the packing and the reversal.

## Coney's implementation

`world_objects::kCarTypeNames` and `kCarParts` (`src/world_objects/car_types.h`) hold the six names and the 26
undamaged atomics. The level file's `0x47` reader (`readPreinstanceObjectChunk`, `src/platform/level_file.h`) reads a
clump of several atomics as a `LevelClumpObject` (`world::extractClumpModels`), and the reference renderer draws the
first 26 atomics of each car, each at its frame and given the dictionary's first texture, for
[Cars](../references/cars.md) ([Building](../guides/building.md#reference-images)).

**Parked cars in play** (2026-10-06), written from this page and [Crimes](crimes.md#stereo):

- `world_objects::Cars` (`repo:src/world_objects/cars.h`) is the level's pool of 18 cars: `CarSpawn` takes the type
  index of its name and its CRC-32, `car_copcar` spawns `part_copcar_lights` ([Particles](particles.md)),
  `CarSetColor` packs its four numbers as [Colour](#colour) says and marks the car dirty, `CarMakeGoodAsNew` clears
  the removed and open parts, and removing door or window part 14, 16, 18 or 20 also removes the next.
  `CarSpawnRadio` puts a stereo in the car; a broken window 15 frees it and a theft takes it once.
- The bindings (`repo:src/scripting/car_bindings.h`) are `CarSpawn`, `CarSetColor`, `CarMakeGoodAsNew`,
  `CarSpawnRadio` and `CarPlaceInTrunkOnDetach`; `CarSpawn` returns `NilHandle` when the pool is full.
- **Part damage and the boot item** (`Cars::damagePart()`, `Cars::placeInTrunk()`): a part's damage adds up (an
  instant call sets it to 1) and at 1 the part comes off (its kept removed bit); the boot coming off a non-instant
  call moves the pinned boot object there, or adds a `dyn_money` spawn record holding the boot's dollars; an instant
  call loses the item, and `CarRemovePart` releases nothing. A window's damage is 1 at once, and window 15 coming
  off frees the stereo.
- **Windows and hits** (`repo:src/world_objects/car_hits.h`, from [Windows, hits and the stereo](#windows)): the
  body and cabin zone tables by where the hitter stands, the facing test, the car pass's reach (1 m outside the box,
  the 3 × 54° cone, 2 m in height), the aim point (1 m ahead at feet + 1.5 m for a window, so square plays 662), and
  `Cars::humanHit()`: an intact window shields its door, each part takes 0.115. `PlayLevelMode` offers each car the
  player can target among the object targets and passes the strike to `Cars::humanHit()`. The stereo and the boot
  item sit at the documented offsets.
- **Explosion** (`CarExplode`, `repo:src/scripting/mission4_bindings.h`, `Cars::explode()`): a car not yet exploded
  takes instant damage on every part (so a boot item is lost) and is marked exploded; without `quiet` its message
  `0x19` goes to its own handler and then to the cars' general one (`SetGeneralCarMsgHandler`,
  `MessageHandlers::setGeneralCar()`), with the car as self.
- `platform::ParkedCars` (`repo:src/platform/parked_cars.h`) draws each car's first 26 atomics, less its removed parts,
  from the type's Object List model, and gives each car a box (12 triangles) that joins the level's collision mesh.

Coney's stand-ins, where this page is silent:

- The paint tints every part but the glass, lights and wheels (8, 9, 15, 17, 19, 21, 22-25).
- Cars are lit as the level lights its humans.
- A car's obstacle is one box around its undamaged atomics, not the type record's boxes; the rebuilt collision mesh
  uses a 4 m grid.
- Every type uses the sedan's zone tables and box (the others' are not on the page yet); the car pass's candidate
  joins the object targets, nearest first, rather than coming before them.
- Not yet in a car hit: the gang lock (`+0x12d8`), the exploding car, a player above the car skipping the cabin
  table, the charge's and thrown objects' contact-point zones, message `0x19`, the first-hit effect and the statistic.
- The money pickup's 15 s life is not applied.
- A name that is not one of the six types still makes a car, which draws nothing.
- The explosion's look (the car is drawn as before, its parts' kept bits set), effects, sound, 300 damage within 5 m,
  statistic and alert to the AI are not built; its message carries 0 as the other object and the number, and both
  the car's handler and the general one are called (which the manager's slots reach, and in which order, is not on
  the page). A car with `+0x1200` set is not skipped.

## Open questions

- What the part record's `+0x20` float is, what sets the draw mask `+0x11fc`, and how long a flying piece lasts
  (`+0x94` = 360: ticks or updates) and what removes it.
- What the object a car holds at `+0x1204` is (released when it explodes; part 15 coming off makes it pickable).
- The lookup that takes a car to its Object List record.
- Which atomics the paint tints, how cars are lit, and how the type record's boxes make a car's collision.
- Where a car's stereo sits (answered: [Windows, hits and the stereo](#windows)). Still open: the effect kinds of
  message `0x3f` (`0x0038a830`) and what `0x002936a8` and `0x00413018` report for a car hit.
- The zone thresholds, masks and boxes of the coupe, wagon, police car, van and Sully's car (Coney uses the
  sedan's).
