# Cars

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), and a disc check (2026-10-06) of the car models and the Object List, reported as counts and sizes. No
runtime claims.

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
task's code is at `0x00387498`-`0x0038e0f0`, in the unattributed `TaskEngine` stretch. Names are ours.

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
| `+0x11f0` | removed parts, one bit per part; `+0x11f8` the same bits, kept (`Car_RemovePartBits`) |
| `+0x11f4` | parts that cannot open (checked by `Car_OpenPart`; who sets it is not traced) |
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

## Behaviour

### Parts {#parts}

- **Remove** (`CarRemovePart(car, part, on)` → `0x0038c7d8`): with `on`, set bit `part` in `+0x11f0` and `+0x11f8`, and
  the linked part's bit in `+0x11f0`; without, clear the bit. Scripts remove parts 5, 17, 18, 19 and 21. Confirmed
  (code).
- **Open** (`CarSetPartOpen(car, n, open)` → `0x0038d5b8`): `n` 0-5 names parts 5, 4, 14, 16, 18, 20.
  `Car_OpenPart` does nothing for any other part, for a removed one or one marked in `+0x11f4`; it sets bit 2 of the
  part state and, once the model is loaded, calls `0x0038cb70` (not traced) for atomic `p` and, when `p` ≤ 21,
  `p + 25`. Confirmed (code).

### Colour {#colour}

`CarSetColor(car, {c1, c2, c3, c4})` turns each number into a byte (× 255, `0x0017aca8`) in the order given and stores
the four bytes as one word in `+0x12e8` and `+0x12ec`, marking the car dirty. `Car_UpdateRender` passes both words to
`0x00338240`, which spreads the bytes in reverse order (`pextlb`, `prevh`) and blends the two, and stores the result in
the render object (`+0x24`). So `c1` is alpha and `c4` red (inferred from the reversal; the scripts always pass 1 as
`c1`). Confirmed (code) for the packing and the reversal.

**To render a car** for the list: read the Object List record of the type, load `<type>_geo` (chunk `0x47`: a
RenderWare clump with PS2 native geometry, the chunk type of the characters' bodies that Coney's character renderer
reads, here with no skin or bone chunk `0x28`) and
its texture dictionary, and draw it closed and undamaged. Which of the 47 atomics make the undamaged car is open.

## Open questions

- How the 47 atomics map to the 26 parts (intact and damaged versions are likely; wheel 25 would collide with `p + 25`).
- Who sets `+0x11f4`, and what the part record's `+0x01` byte and `+0x20` float are.
- The lookup that takes a car to its Object List record.
