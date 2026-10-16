# Model format (PS2 RenderWare clumps)

The PS2 disc's models are RenderWare 3.7 clump streams stored in chunk `0x47` of the resources in `WARRIORS.WAD`.
This page gives the clump layout as it is stored on this disc: the sections and plugin sections that occur, the PS2
native vertex data, the skin and bone data, and what the materials hold. All values are little-endian. Every count
below comes from reading the whole PS2 disc (the USA disc image, five languages).

## Where the models are

Each resource in `WARRIORS.WAD` starts with a 16-byte header, then its chunks, each with a 16-byte header:

| Header | Offset | Type | Meaning |
| --- | --- | --- | --- |
| resource | `+0x00` | u32 | chunk count |
| resource | `+0x04` | u32 | sum of the chunks' data sizes (chunk headers not counted) |
| resource | `+0x08` | u32 | 0 |
| resource | `+0x0c` | u32 | resource name hash |
| chunk | `+0x00` | u32 | chunk type |
| chunk | `+0x04` | u32 | data size in bytes (the data follows the header) |
| chunk | `+0x08` | u32 | 0 |
| chunk | `+0x0c` | u32 | chunk hash |

A pack entry holds several such resources after its own 16-byte header (resource count, size of the entry less 16,
0, pack hash).

| Where chunk `0x47` occurs | Chunks | Clumps |
| --- | --- | ---: |
| standalone model resources (one `0x47` chunk) | `0x47` | 1,324 |
| standalone skinned model resources | `0x47` then `0x28` | 153 |
| resources inside the 881 packs | `0x47`, alone or with `0x28` | 19,787 |
| level resources (`level<N>.lev`) | among the level's other chunks | 192 |
| **Total** | | **21,456** |

The 21,456 clumps carry 1,541 distinct resource hashes. A model resource's hash is the CRC-32 of `<name>_geo` (for
example `car_osedan_geo`, `bopp_bo_geo`); 1,425 of the 1,541 hashes match a name found in the game's own data
files. The chunk hash equals the resource hash in all 18,894 unskinned model resources; it differs in the 2,370
skinned ones and in the 192 level chunks.

Each `0x47` chunk holds exactly one clump section; the chunk is padded after the clump to a multiple of 4 bytes
(1 to 13 bytes of padding seen).

## Section headers

Every RenderWare section starts with a 12-byte header: u32 section type, u32 body size, u32 library stamp. Every
section in every clump on the disc carries the stamp `0x1C02000A` (RenderWare 3.7.0.2).

| Id | Section | Seen inside |
| --- | --- | --- |
| `0x01` | struct | every container |
| `0x03` | extension | frame list, geometry, material, atomic, clump |
| `0x07` | material | material list |
| `0x08` | material list | geometry |
| `0x0E` | frame list | clump |
| `0x0F` | geometry | geometry list |
| `0x10` | clump | chunk `0x47` |
| `0x14` | atomic | clump |
| `0x1A` | geometry list | clump |
| `0x1F` | right-to-render plugin | material and atomic extensions |
| `0x116` | skin plugin | geometry extension |
| `0x11E` | HAnim (bone hierarchy) plugin | frame extensions |
| `0x120` | material effects plugin | material and atomic extensions |
| `0x3F0` | the game's atomic plugin (vertex scales) | atomic extension |
| `0x50E` | mesh (bin mesh) plugin | geometry extension |
| `0x510` | native data plugin | geometry extension |

No clump holds a light (`0x12`), camera (`0x05`), texture (`0x06`), frame name (`0x253F2FE`) or any section id not
in this table.

## Clump (section 0x10)

The clump body is: struct, frame list, geometry list, one atomic section per atomic, then an empty extension.

| Struct offset | Type | Meaning | Disc values |
| --- | --- | --- | --- |
| `+0x00` | u32 | atomic count | 1 (21,299 clumps) or 47 (157 clumps) |
| `+0x04` | u32 | light count | always 0 |
| `+0x08` | u32 | camera count | always 0 |

The clump's own extension is empty (size 0) in all 21,456 clumps. The geometry count always equals the atomic
count.

| Clump shape | Frames | Geometries / atomics | Bones in the hierarchy | Clumps | Standalone examples |
| --- | ---: | ---: | ---: | ---: | --- |
| rigid prop | 2 | 1 | 1 | 18,929 | `dyn_baton_geo`, `dyn_trashcan_geo` |
| skinned character | 33 | 1 | 32 | 2,370 | `bopp_bo_geo`, `riff_cy_geo` |
| vehicle in parts | 48 | 47 | 47 | 157 | `car_osedan_geo`, `car_van_geo` |

## Frame list (section 0x0E)

A struct with a u32 frame count and 56 bytes per frame, then one extension section per frame.

| Frame offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | 3 x f32 | right vector |
| `+0x0c` | 3 x f32 | up vector |
| `+0x18` | 3 x f32 | at vector |
| `+0x24` | 3 x f32 | position |
| `+0x30` | i32 | parent frame index, -1 for the root |
| `+0x34` | u32 | flags: 3 (119,047 frames), `0x20003` (4,444, all of them root frames), 0 (113) |

Every clump has exactly one root frame, and it is frame 0. Frame 0's extension is always empty. Every other frame's
extension holds exactly one HAnim section (`0x11E`, 102,148 frames in all). The root frame's flags are `0x20003` in
all 2,370 skinned clumps and in 2,074 unskinned ones, and 3 in the other 17,012.

### HAnim plugin (0x11E)

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | version, always `0x100` |
| `+0x04` | u32 | bone id of this frame (0 to 255 on the disc) |
| `+0x08` | u32 | bone count: 0 on every frame but frame 1 |
| `+0x0c` | u32 | hierarchy flags (only when the bone count is not 0): always 0 |
| `+0x10` | u32 | key frame size (only when the bone count is not 0): always 36 |
| `+0x14` | 12 bytes per bone | bone id, bone index, bone flags (only when the bone count is not 0) |

Frame 1 carries the whole hierarchy in all 21,456 clumps: 1 bone (props), 32 (skinned characters) or 47 (vehicles).
The bone flags take the values 0, 1, 2 and 3. A frame's bone id is not its frame index: in `bopp_bo_geo` frame 3
carries bone id 26.

## Geometry list and geometry (sections 0x1A and 0x0F)

The geometry list is a 4-byte struct (geometry count) followed by the geometries. Each geometry is a 40-byte struct,
a material list and an extension.

| Struct offset | Type | Meaning | Disc values |
| --- | --- | --- | --- |
| `+0x00` | u32 | format flags | see below |
| `+0x04` | u32 | triangle count | 2 to 2,840 per geometry in the standalone models |
| `+0x08` | u32 | vertex count | |
| `+0x0c` | u32 | morph target count | always 1 |
| `+0x10` | 4 x f32 | bounding sphere: centre x, y, z, radius | |
| `+0x20` | u32 | has-positions word | always 0 |
| `+0x24` | u32 | has-normals word | always 0 |

The vertex data is never in the struct: every geometry has the native flag and keeps its vertices in the `0x510`
section. In the low 16 bits, bit `0x01` is triangle strips, `0x02` positions, `0x04` one texture-coordinate set, `0x08`
prelit colours, `0x10` normals, `0x20` lit, `0x40` modulate material colour, `0x80` two texture-coordinate sets.
Bits 16 to 23 give the number of texture-coordinate sets and bit 24 (`0x01000000`) marks native data.

| Format flags | Geometries | Texture sets | Skinned |
| --- | ---: | ---: | --- |
| `0x01010037` | 22,690 | 1 | no |
| `0x0101003F` | 1,904 | 1 | no |
| `0x010200B3` | 1,706 | 2 | 1,619 yes, 87 no |
| `0x0101000F` | 1,583 | 1 | no |
| `0x010200F3` | 686 | 2 | yes |
| `0x010200BB` | 47 | 2 | 38 yes, 9 no |
| `0x01000033` | 34 | 0 | no |
| `0x01010077` | 27 | 1 | yes |
| `0x0100007B` | 1 | 0 | no |

### Geometry extension

| Plugins present | Geometries |
| --- | ---: |
| mesh `0x50E`, native data `0x510` | 26,308 |
| mesh `0x50E`, native data `0x510`, skin `0x116` | 2,370 |

The mesh plugin (`0x50E`) is u32 strip flag, u32 mesh count, u32 total index count, then per mesh u32 index count and
u32 material index. On this disc the strip flag is always 1 and every geometry has exactly one mesh, so the index
count of that mesh equals the total.

## Materials

Every geometry's material list is a struct (u32 material count, then one i32 per material) and the materials. On the
disc every list holds exactly one material and its index word is -1.

| Material struct offset | Type | Meaning | Disc values |
| --- | --- | --- | --- |
| `+0x00` | u32 | flags | always 0 |
| `+0x04` | 4 x u8 | colour R, G, B, A | 255,255,255,255 (27,964); 150,150,150,255 (712); one each of 156,156,156,255 and 255,255,255,176 |
| `+0x08` | u32 | unused | always 0 |
| `+0x0c` | u32 | textured flag | always 0 |
| `+0x10` | f32 | ambient | always 1.0 |
| `+0x14` | f32 | specular | 0 (22,772), 0.05 (3,436), 1.0 (2,470) |
| `+0x18` | f32 | diffuse | always 1.0 |

No material on the disc has a texture section, so a model names no texture of its own. The texture dictionary that
goes with a model is named by its object list or character list record (see the global lists page): for example
`car_osedan_geo` is listed with `car_osedan_tex` and `bopp_bo_geo` with `bopp_bo_tex`.

| Material extension | Materials |
| --- | ---: |
| right-to-render `0x1F` only | 26,212 |
| right-to-render `0x1F` and material effects `0x120` | 2,466 |

| Right-to-render body (two u32) | Materials |
| --- | ---: |
| 3, `0x00030084` | 26,212 |
| 3, `0x00030081` | 2,370 (the skinned geometries) |
| 3, `0x00030086` | 90 |
| 3, `0x00030087` | 6 |

| Material effects body (24 bytes) | Materials |
| --- | ---: |
| effect type 4 (dual pass); first effect 4 with source blend 5, destination blend 6, no texture; second effect 0 | 2,460 |
| effect type 2 (environment map); first effect 2 with coefficient 0.1, no frame-buffer alpha, no texture; second effect 0 | 5 |
| as above with coefficient 0.3 | 1 |

## Atomic (section 0x14)

| Struct offset | Type | Meaning | Disc values |
| --- | --- | --- | --- |
| `+0x00` | u32 | frame index | 1 for single-atomic clumps |
| `+0x04` | u32 | geometry index | |
| `+0x08` | u32 | flags | always 5 |
| `+0x0c` | u32 | unused | always 0 |

| Atomic extension | Atomics |
| --- | ---: |
| right-to-render `0x1F`, game plugin `0x3F0` | 26,212 |
| right-to-render `0x1F`, material effects `0x120` (body u32 1), game plugin `0x3F0` | 2,466 |

The atomic's right-to-render body is 3, `0x00030083` (26,308 atomics) or 3, `0x00030080` (the 2,370 skinned ones).

### Game atomic plugin (0x3F0)

Twelve bytes in every atomic on the disc:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | f32 | position scale: multiplies the 16-bit vertex positions |
| `+0x04` | f32 | texture-coordinate scale: multiplies the 16-bit texture coordinates |
| `+0x08` | u32 | always 0 |

Both scales are powers of two. The position scale runs from 1/32768 to 1/64 and the texture-coordinate scale from
1/32768 to 1/2048. The most common pair is 1/32768 and 1/32768 (15,390 atomics). With the position scale applied,
every vertex of the 1,945 geometries in standalone and level resources lies inside its geometry's bounding sphere;
with the texture-coordinate scale applied, the coordinates of most batches stay within -1 to 1, and some tile up to
21.

## PS2 native geometry (plugin 0x510)

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | 12 bytes | an inner struct header (type `0x01`, a size, the stamp); its size never matches the real data on this disc |
| `+0x0c` | u32 | platform: always 4 (PS2) |
| `+0x10` | u32 | mesh 0: size of its DMA chain in bytes |
| `+0x14` | u32 | mesh 0: flags, always 0 |
| `+0x18` | bytes | mesh 0: the DMA chain; further meshes would follow (every geometry here has one) |

The chain is made of DMA tags of three kinds: `cnt` (data follows the tag), `ref` (data elsewhere in the chain, the
address counted in 16-byte units from the chain's start) and `ret` (the last tag). Each tag's upper 8 bytes are VIF
codes; the VIF stream is those 8 bytes followed by the tag's data, tag after tag. The stream is a sequence of
batches. Each batch has one `STCYCL` and one `UNPACK` per vertex attribute, then `ITOP`, whose value is the batch's
vertex count, then a microprogram start (`MSCALF` for the first batch of a mesh, `MSCNT` for the rest), with `NOP`
and `FLUSH` codes as padding. There are 243,563 batches on the disc, mostly of 30 to 69 vertices.

Each attribute is unpacked to its own slot of a vertex, slots 0 to 3 (or 0 to 4), with `STCYCL` set to write 1 of
every 4 quadwords (`0x0104`, four slots) or 1 of every 5 (`0x0105`, five slots). The unpack counts for slots 1 to 3
can exceed `ITOP` by up to 3 (rounded up for alignment); the extra entries are padding.

| Slot | Attribute | Format | Meaning of the values |
| --- | --- | --- | --- |
| 0 | position | `V4_16` signed | x, y, z times the atomic's position scale; the fourth value is 0 in 99.9% of vertices |
| 1 | texture coordinates | `V2_16` signed (one set) or `V4_16` signed (two sets: u0, v0, u1, v1) | times the atomic's texture-coordinate scale |
| 2 | colour | `V4_8` unsigned | R, G, B, A; A is 255 in every batch, R, G and B are 0 in 225,815 batches and up to 240 in the rest |
| 3 | normal | `V4_8` signed | x, y, z divided by 127 give a unit vector (or a zero vector); the fourth byte is always 0 |
| 4 | bone weights (skinned only) | `V4_32` | four weights, see below |

| Batch layout | Batches | Used by |
| --- | ---: | --- |
| slots 0 to 4 (`V4_16` texture coordinates and weights) | 150,138 | every skinned geometry |
| slots 0 to 3 with `V2_16` texture coordinates | 90,890 | unskinned geometries with zero or one texture set, and 23 batches of two-set ones |
| slots 0 to 3 with `V4_16` texture coordinates | 2,535 | unskinned geometries with two texture sets |

The meshes are triangle strips. Each batch after the first in a mesh repeats the last two vertices of the batch
before it, so a mesh's strip is the first batch's vertices followed by each later batch's vertices less its first
two. For every geometry on the disc, the mesh plugin's index count equals the sum of the batches' `ITOP` values less
2 for each batch after the first. The strip's triangles whose three positions all differ number exactly the
struct's triangle count (checked on eleven of the models named on this page). The struct's vertex count is smaller
than the strip length because the strip repeats vertices.

## Skin plugin (0x116)

| Offset | Type | Meaning | Disc values |
| --- | --- | --- | --- |
| `+0x00` | 12 bytes | inner struct header; its size is correct here (body length less 12) | |
| `+0x0c` | u32 | platform | always 4 |
| `+0x10` | u8 | bone count | always 32 |
| `+0x11` | u8 | used-bone count | 32 (2,221 geometries), 24 (82), 6 (27), 31 (18), 28 (12), 23 (5), 14 (4), 30 (1) |
| `+0x12` | u8 | most weights on one vertex | 4 (1,968) or 3 (402) |
| `+0x13` | u8 | padding | always 0 |
| `+0x14` | used-bone count x u8 | used bone indices | not in ascending order |
| then | bone count x 64 bytes | one inverse bind matrix per bone: 16 f32 as right, up, at, position, each with a fourth value | the fourth values are not meaningful (usually 0, sometimes other values) |
| then | 4 x u32 | numbers of vertices with 1, 2, 3 and 4 weights | they add up to the geometry's vertex count in all 2,370 geometries |
| then | 12 bytes | always 0 | |

The bone weights themselves are in slot 4 of the native data. Each of the four words is a float whose low 10 bits are
replaced by a bone reference: with those bits cleared the word is the weight; the low 10 bits are (bone index + 1)
times 4, and 0 means the weight is unused (its weight is then 0.0). Across the disc's 5,065,905 skinned strip
vertices the weights of each vertex add up to 1.0, the bone index is always below 32, and the vertices carry 1
(2,291,133), 2 (1,980,821), 3 (699,802) or 4 (94,149) weights.

### Chunk 0x28 beside a skinned model

Every skinned model resource has a second chunk, type `0x28`, of 544 bytes in all 2,370 cases: 34 rows of four
f32 whose fourth value is always 1.0. The first row is 0, 0, 0, 1.

## Disc totals for the standalone models

| What | Count |
| --- | ---: |
| standalone model resources | 1,477 |
| skinned (32 bones, one geometry) | 153 |
| vehicles in 47 parts | 6: `car_coupe_geo`, `car_van_geo`, `car_sullycar_geo`, `car_wagon_geo`, `car_osedan_geo`, `car_copcar_geo` |
| rigid one-geometry props | 1,318 |
| vertices (struct counts, all standalone models) | 413,745 |
| triangles (all standalone models) | 353,011 |
| largest | `riff_cy_geo`: 2,840 triangles, 2,337 vertices |
| smallest | `dyn_swan_geo`: 2 triangles, 4 vertices |

## Chunk types read

| Chunk type | What the game reads from it | Chunks of this type in PS2 resources |
| --- | --- | ---: |
| `0x47` | One clump (section `0x10`) from the payload | 21,456 |

Every model on the PS2 disc arrives in a chunk of type `0x47`.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0017f2c0` | Read a model held in a resource chunk | Chunk types read |
| `0x00192688` | Read the game's own atomic plugin with its vertex scales | Game atomic plugin (0x3F0) |

## Acceptance checks

- The PS2 disc's `WARRIORS.WAD` holds 21,456 chunks of type `0x47`, each one RenderWare clump stamped `0x1C02000A`.
- 1,477 standalone resources are models: 1,324 with chunk `0x47` alone and 153 with chunks `0x47` and `0x28`.
- Every clump on the disc has no lights and no cameras, and either 1 or 47 atomics.
- Every geometry on the disc has native data for platform 4, exactly one material, exactly one mesh and no texture
  section in its material.
- Every atomic carries the `0x3F0` plugin: 12 bytes, two power-of-two scales and a zero word.
- `bopp_bo_geo` (standalone entry 1322) has 1,108 vertices, 1,138 triangles, 1 material, 32 bones in its skin and in
  its hierarchy, and 33 frames; decoded, it shows a standing humanoid figure with its arms spread out.
- `riff_cy_geo` (entry 773) has 2,337 vertices, 2,840 triangles, 1 material and 32 bones, and shows a humanoid figure
  with arms spread out.
- `fury_bo_geo` (entry 61) has 1,206 vertices, 1,252 triangles, 1 material and 32 bones.
- `car_osedan_geo` (entry 3186) has 47 atomics, 47 geometries, 47 materials, 48 frames, 2,188 vertices and 1,371
  triangles, and shows a four-wheeled car.
- `car_van_geo` (entry 2069) has 47 parts, 1,763 vertices and 1,119 triangles, and shows a boxy four-wheeled van with
  two rear doors.
- `dyn_trashcan_geo` (entry 3332) has 32 vertices, 28 triangles, 1 material and no skin, and shows a tapering
  eight-sided can.
- `dyn_baton_geo` (entry 1071) has 41 vertices and 36 triangles and shows a long, thin five-sided stick.
- `dyn_phone_geo` (entry 2142) has 120 vertices and 56 triangles; part of it is a telephone handset.
- `dyn_jukebox_geo` (entry 3463) has 114 vertices and 88 triangles and shows a tall box.
- For every geometry, the mesh plugin's index count equals the strip length rebuilt from the native batches.
- For every skinned vertex, the four bone weights add up to 1.0 and every bone index is below 32.
- Every skinned model's chunk `0x28` is 544 bytes.

## Open questions

- What the right-to-render values (3 with `0x00030080` to `0x00030087`) select, and what the frame flag `0x20000`
  on some root frames marks.
- What the 34 rows of chunk `0x28` hold (32 bones plus two rows is a guess), and whether the 47-part vehicles have a
  counterpart.
- What the rare non-zero fourth position values (about 12,000 of 9.9 million unpacked positions) mean.
- What the colour slot's values do in rendering: 128 as full brightness is a claim from earlier research, not
  checked here.
- The meaning of the HAnim bone flags 0 to 3 is taken from the public format, not checked against the game.
- The Xbox disc's models were not examined; its geometry is not PS2 native data, so the native data and skin struct
  layouts above are PS2-only.
- Readers for clump lights and cameras are left off the entry points: no clump on the disc has either, and their
  entry points were not confirmed.
- The 116 model hashes whose names were not recovered.
