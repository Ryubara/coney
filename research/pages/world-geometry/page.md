# World geometry

This page gives the geometry inside the PS2 world part files: the geometry pieces, their materials and textures,
the second texture layer, the pipeline ids each piece and material carries, the two texture-coordinate sets and the
native vertex data. It also says which world textures are in memory for the whole level and gives disc-wide counts.
The file layouts around the geometry (world streams, sector records, part file headers, the `0x3F0` plugin's values,
the geometry flags of each world) are on the streamed world page (`world`) and are not repeated here. The clump and
native-data conventions shared with the models are on the model format page (`model-format`); texture dictionaries
are on the texture dictionary page (`texture-dictionary`); the frame layers a viewer sees are on the PS2 display page
(`ps2-display`).

## Sources

- PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso" (NTSC-U, `SLUS_212.15`): every `<world>_sec.wld` stream
  and every listed `<world>_ms<n>.sec` part file in `WARRIORS.WAD`, located through `WARRIORS.DIR`.
- "Listed parts" are the parts numbered from 1 up to their world's part count: the 1,564 headed parts the `world`
  page lists, plus the four parts of `courtyard`. Together they hold 5,335 geometry pieces: the 5,315 pieces of the
  `world` page and the 20 pieces of `courtyard`. Unlisted parts are left out of every count below.
- `SLUS_212.15`, read-only, for the world's pipeline ids and the pipeline each material is given.
- All values are little-endian. No savestate was used.

## Geometry piece

Each piece in a part file is one RenderWare atomic section (`0x14`, stamp `0x1C02000A`) holding one geometry. There
is no clump and no frame list around it.

| Atomic struct offset | Type | Meaning | Values in the listed parts |
| --- | --- | --- | --- |
| `+0x00` | u32 | Frame index | Never 0 and never a valid index; no frame list exists |
| `+0x04` | u32 | Geometry index within the atomic | 0 in all 5,335 |
| `+0x08` | u32 | Flags | 5 in all 5,335 |
| `+0x0c` | u32 | Unused | 0 in all 5,335 |

| Atomic extension | Pieces |
| --- | ---: |
| right-to-render `0x1F`, game plugin `0x3F0` | 3,177 |
| right-to-render `0x1F`, material effects `0x120` (body u32 1), game plugin `0x3F0` | 2,138 |
| empty | 20 (all of `courtyard`) |

- The atomic's material-effects plugin is present on exactly the 2,138 pieces that hold at least one second-layer
  material (see "Second texture layer"), and on no other piece.
- The right-to-render body is two u32: 3 and the atomic pipeline id, `0x00030083` on 5,314 pieces and `0x00030082` on
  the single piece of `objarena`.
- Every piece placed in the world gets the atomic pipeline `0x00030083` (see "Pipeline ids").

## Geometry section

Each atomic holds one geometry section (`0x0F`): a 40-byte struct, a material list and an extension holding the mesh
plugin (`0x50E`) and the native data plugin (`0x510`), in all 5,335 pieces.

| Struct offset | Type | Meaning | Values in the listed parts |
| --- | --- | --- | --- |
| `+0x00` | u32 | Format flags | `0x010200B9` (3,898 pieces), `0x0101003D` (1,417), `0x0101007D` (20, all `courtyard`) |
| `+0x04` | u32 | Triangle count | |
| `+0x08` | u32 | Vertex count | |
| `+0x0c` | u32 | Morph target count | 1 in all |
| `+0x10` | 4 x f32 | Bounding sphere: centre x, y, z and radius, in the piece's own space | |
| `+0x20` | u32 | Has-positions word | 0 in all |
| `+0x24` | u32 | Has-normals word | 0 in all |

- The flag bits have the meanings given on the model format page. `0x010200B9` carries two texture-coordinate sets
  and `0x0101003D` one; neither sets the positions bit `0x02` or the modulate-material-colour bit `0x40`.
- Vertex scales: with the game plugin `0x3F0`'s float at `+0x00` applied as the position scale, every vertex of all
  5,315 packed pieces lies inside the piece's bounding sphere. Its float at `+0x04` is the texture-coordinate scale.
  This is the same use the model format page gives the plugin.

## Materials

Every piece has its own material list: a struct with the material count and one index word per material (-1 in
all 88,805), then the materials. A piece holds 1 to 66 materials; 88,805 materials in all.

| Material struct offset | Type | Meaning | Values in the listed parts |
| --- | --- | --- | --- |
| `+0x00` | u32 | Flags | 0 in all |
| `+0x04` | 4 x u8 | Colour R, G, B, A | 255, 255, 255, 255 in 88,779; 150, 150, 150, 255 in 26 (all in `courtyard`) |
| `+0x08` | u32 | Unused | 0, except in the 380 materials of `courtyard` |
| `+0x0c` | u32 | Textured flag | 1 in 88,748, each with a texture section; 0 in 57 with none (all in `courtyard`) |
| `+0x10` | f32 | Ambient | 1.0 in all |
| `+0x14` | f32 | Specular | 1.0 (67,185), 0 (21,517), 0.05 (103) |
| `+0x18` | f32 | Diffuse | 1.0 in all |

Unlike the models, world materials name their texture. The texture section (`0x06`) holds:

| Part | Values in the listed parts |
| --- | --- |
| Struct: filter and addressing word | `0x00011106` in 88,747 references, `0x00013306` in 1; the low 16 bits equal the word of the dictionary texture named |
| Texture name string | The name of a texture in the part's or the stream's dictionary (see "Where the textures are") |
| Mask name string | Empty in all |
| Extension | One `0x110` plugin, whose value equals the mipmap K value of the dictionary texture named, in all 88,748 |

Bit 16 (`0x00010000`) of the filter word is set in every material's texture reference and in no dictionary
texture's word.

| Material extension | Materials |
| --- | ---: |
| right-to-render `0x1F` only | 80,098 |
| right-to-render `0x1F` and material effects `0x120` | 8,327 |
| empty | 380 (all of `courtyard`) |

## Where the textures are

A world's textures are split between the dictionary in its stream and the dictionaries in its parts. The stream is
read at level start and kept for the whole level (`world` page), so its textures are present before any part
arrives; a part's own textures arrive and leave with that part.

| Fact | Value in the listed parts |
| --- | --- |
| Textures in the 160 stream dictionaries | 11,758, every name ending `_000` |
| Textures in the listed parts' dictionaries | 10,776, every name ending in its part's number as three digits (`_001` in part 1, `_014` in part 14) |
| Listed parts with an empty dictionary | 411 of the 1,564 headed listed parts, and all four parts of `courtyard` |
| Material references to a stream texture | 74,699 |
| Material references to a texture in the material's own part | 14,049 |
| References to another part's or the other world's textures | none |
| Stream textures no material or second layer of their world uses | none |
| Part textures no material or second layer of their part uses | none |

- Because of the suffixes, a stream texture and a part texture never share a name.
- In `level99s_ms1.sec` the first material of geometry index 29 names `properic140_001`, a texture of that part's
  own dictionary, and its second layer names `bronxstain_000`, a texture of the `level99s` stream.

## Second texture layer

8,327 materials carry the material-effects plugin (`0x120`) with the dual-pass effect, which lays a second texture
over the base one. No other effect occurs on world materials.

| Body offset | Type | Meaning | Values on all 8,327 |
| --- | --- | --- | --- |
| `+0x00` | u32 | Effect type | 4 (dual pass) |
| `+0x04` | u32 | First effect | 4 (dual pass) |
| `+0x08` | u32 | Source blend | 5 (source alpha) |
| `+0x0c` | u32 | Destination blend | 6 (inverse source alpha) |
| `+0x10` | u32 | Has-texture flag | 1 |
| `+0x14` | section | Texture section (`0x06`), laid out as a material's | filter word `0x00011106` (8,007) or `0x00013306` (320) |
| after it | u32 | Second effect | 0 (none) |

| Fact | Value |
| --- | --- |
| Second-layer materials in `s` worlds | 8,299 |
| Second-layer materials in `d` worlds | 28, in `level5d`, `level9d`, `level11d`, `level14d`, `level20d`, `level31d`, `level34d`, `level51d`, `level73d`, `level95d`, `level96d` and `level137d` |
| Worlds with at least one | 86 |
| Distinct second-layer texture names | 98 |
| Second-layer textures found in the stream's dictionary | 8,233 references |
| Second-layer textures found in the part's own dictionary | 94 references |
| Most used | `bronxstain_000` (5,902 materials, 128 x 128, 8-bit), `prklot_dirtolb_000` (1,055, 128 x 128, 4-bit), `gassy_blend_000` (135), `54_dual_grime_000` (129, 64 x 64) |
| Sizes used | 16 x 16 (`awning_colours_000`) to 256 x 256; 128 x 128 in 7,608 of the 8,327 materials; no second-layer texture has mipmap levels |
| Clamped second-layer textures | graffiti tags and murals such as `train_tag3_000` to `train_tag6_000`, `tag_murder_lic_000`, `wallmural004_000` |

- Every second-layer material sits in a geometry with two texture-coordinate sets (flags `0x010200B9`).
- Counted world by world, 228 stream textures serve as a second layer; 223 of them are never a base texture in
  that world.
- The second layer is what the PS2 display page shows as grime and decals over world surfaces; its most used
  texture, `bronxstain_000`, has the 128 x 128 8-bit size that page measured.

## Pipeline ids

World geometry has one atomic pipeline id and four material pipeline ids. Each id is a u32; the same ids are stored
on the disc in the right-to-render plugins (a body of two u32: 3 and the id).

| Id | Applies to | Batch layout of its world materials on the disc (see "Meshes and native data") | World use on the disc |
| --- | --- | --- | --- |
| `0x00030083` | Atomic | - | every placed piece |
| `0x00030084` | Material with one texture-coordinate set, no effect | packed, one set (`courtyard`: RenderWare default) | 18,074 stored, 18,464 in effect |
| `0x00030086` | Material with the dual-pass second layer | packed, two sets | 8,327 |
| `0x00030087` | Material with an environment map | - | none (6 model materials, model format page) |
| `0x00030088` | Material with two texture-coordinate sets, no effect | packed, two sets | 62,014 |

### Pipeline per material

A placed piece and its materials have these pipelines, whatever ids the file stores:

| Material | Pipeline in effect | Materials | Stored id agrees |
| --- | --- | ---: | --- |
| The piece itself | `0x00030083` | 5,335 pieces | 5,314; `objarena` stores `0x00030082`, `courtyard` stores none |
| No effect, geometry with one set | `0x00030084` | 18,464 | 18,074; the other 390 are `courtyard`'s 380 (no id stored) and `objarena`'s 10 (stored `0x00030083`) |
| Dual-pass second layer | `0x00030086` | 8,327 | all 8,327 |
| Environment map | `0x00030087` | 0 | - |
| No effect, geometry with two texture-coordinate sets | `0x00030088` | 62,014 | all 62,014 |

| World kind | `0x00030088` | `0x00030086` | `0x00030084` |
| --- | ---: | ---: | ---: |
| `s` worlds | 54,282 | 8,299 | 740 |
| `d` worlds | 7,732 | 28 | 17,334 |
| `courtyard` and `objarena` | 0 | 0 | 390 |

## Second texture-coordinate set

Slot 1 of a vertex holds `u0, v0` (one set, `V2_16`) or `u0, v0, u1, v1` (two sets, `V4_16`), all times the piece's
texture-coordinate scale.

| Material pipeline | Strip vertices with two sets | Second set |
| --- | ---: | --- |
| `0x00030086` (second layer) | 433,748 | Differs from the first set in all but 8; within -1 to 1 in 134,556, within -4 to 4 in 292,986 more, beyond 4 in 6,206 |
| `0x00030088` (no effect) | 5,478,972 | (0, 0) in 5,478,050; non-zero in 922 vertices of 49 meshes |

- On the two-set materials with no effect the second set is empty space in the vertex: almost always zero.
- On the second-layer materials the second set is a mapping of its own, unlike the first; it is taken here as the
  second layer's coordinates (inferred from the data, see Open questions).
- With the scale applied, first-set coordinates reach 119.4 at most in the packed worlds, so base textures tile.

## Meshes and native data

| Fact | Value in the listed parts |
| --- | --- |
| Mesh plugin (`0x50E`) strip flag | 1 (triangle strips) in all pieces |
| Meshes per piece | One per material: the mesh count equals the material count in all 5,335 pieces; 88,805 meshes |
| Mesh order | A reordering of the materials; in material order in 4,184 pieces |
| Native plugin platform word | 4 in all |
| Native mesh flags word | 0, except 1 in the 380 meshes of `courtyard` |
| DMA tags | 817,325 `cnt`, 724,600 `ref`, 88,805 `ret` (one per mesh, at its end) |
| Batches | 181,899; `STCYCL` `0x0104` in all |
| Microprogram start | `MSCALF` on the first batch of each mesh (88,805), `MSCNT` on the rest (93,094) |
| Batch length (`ITOP`) | 60 to 69 vertices in 87,870 batches; the rest shorter |
| Strip rule | For every mesh, the mesh plugin's index count equals the sum of its batches' `ITOP` less 2 for each batch after the first |
| Strip vertices | 7,783,592 |

| Batch layout | Slot 0 | Slot 1 | Slot 2 | Slot 3 | Batches | Used by |
| --- | --- | --- | --- | --- | ---: | --- |
| packed, two sets | `V4_16` signed position | `V4_16` signed, two sets | `V4_8` unsigned colour | `V4_8` signed normal | 140,761 | pieces with flags `0x010200B9` |
| packed, one set | `V4_16` signed position | `V2_16` signed, one set | `V4_8` unsigned colour | `V4_8` signed normal | 40,389 | pieces with flags `0x0101003D` |
| RenderWare default | `V3_32` float position | `V2_32` float | `V4_8` unsigned colour | `V3_8` signed normal | 749 | `courtyard` only |

- Positions: the fourth `V4_16` value is 0 in every packed vertex.
- Colours: alpha is 255 in every vertex of every world on the disc; red, green and blue run from 0 to 255, and
  3,057,501 strip vertices are black (0, 0, 0).
- Normals: the fourth byte is 0 in every packed vertex.
- No world batch carries bone weights (no slot 4).

## Courtyard and objarena

| Fact | `courtyard` | `objarena` |
| --- | --- | --- |
| Archive entries | stream 3,797, parts 3,793 to 3,796 | stream 9,811, part 9,809 |
| Pieces | 20 in four parts | 1 in one part |
| Format flags | `0x0101007D` (adds modulate material colour) | `0x0101003D` |
| Batch layout | RenderWare default (floats) | packed, one set |
| Pipeline ids stored | none on atomics or materials | atomic `0x00030082`, materials `0x00030083` |
| Vertex scales | no `0x3F0` plugin | `0x3F0` present, values not powers of two |
| Materials | 380: 57 untextured, 26 grey, all one set | 10 |

## Disc counts

Listed parts only.

| Count | `s` worlds | `d` worlds | `courtyard` and `objarena` | All |
| --- | ---: | ---: | ---: | ---: |
| Geometry pieces | 3,449 | 1,865 | 21 | 5,335 |
| Materials (and meshes) | 63,321 | 25,094 | 390 | 88,805 |
| Second-layer materials | 8,299 | 28 | 0 | 8,327 |
| Native batches | 126,337 | 54,761 | 801 | 181,899 |
| Strip vertices | 5,280,305 | 2,465,986 | 37,301 | 7,783,592 |
| Stream dictionary textures | 9,357 | 2,297 | 104 | 11,758 |
| Part dictionary textures | 5,348 | 5,428 | 0 | 10,776 |

- Most materials in one world: 2,703 (`level51s`), then 2,680 (`level20s`) and 2,515 (`level83s`).
- Most distinct second-layer textures in one world: 15 (`level54s`).

### The tutorial

| Fact | `level99s` | `level99d` |
| --- | ---: | ---: |
| Pieces | 47 | 27 |
| Materials | 1,344 | 473 |
| Pipeline `0x00030088` / `0x00030086` / `0x00030084` | 1,207 / 137 / 0 | 0 / 0 / 473 |
| Distinct second-layer textures | 3 | 0 |
| Part dictionary textures | 153 | 159 |
| Native batches | 2,509 | 899 |
| Strip vertices | 102,601 | 37,459 |

In `level99s_ms1.sec`, geometry index 29 has flags `0x010200B9`, 1,105 triangles, 1,839 vertices, 48 materials
(6 of them second-layer), 79 batches and 2,799 strip vertices.

## Xbox version

The Xbox archive keeps the world in `.xlev` and `.xsec` files (`world` page). Their geometry was not examined; the
PS2 native data, packed layouts and pipeline ids on this page are PS2 facts only.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x004110c0` | Read the geometry pieces of an arrived world part | Geometry piece |

## Acceptance checks

- Every listed part's geometry pieces are RenderWare atomics with flags 5, each holding one native PS2 geometry for
  platform 4; there are 5,335 of them, with 88,805 materials and as many meshes.
- Every world material with a texture names one found either in its own part's dictionary (a name ending in that
  part's number, such as `_001`) or in its world stream's dictionary (a name ending `_000`), never elsewhere.
- Every texture in every stream dictionary and every listed part dictionary is used by at least one material or
  second layer.
- Each material's texture reference carries a `0x110` value equal to the K value of the dictionary texture it names.
- 8,327 world materials carry the dual-pass effect with blends 5 and 6 and a texture; all lie in geometry with two
  texture-coordinate sets, and 5,902 of them use `bronxstain_000`.
- The atomic material-effects plugin is present on exactly the 2,138 pieces holding a dual-pass material.
- Stored material pipeline ids: `0x00030088` 62,014, `0x00030084` 18,074, `0x00030086` 8,327, `0x00030083` 10; no
  world material stores `0x00030087`.
- Every packed world vertex has colour alpha 255, a zero fourth position value and a zero fourth normal byte.
- For every world mesh, the mesh plugin's index count equals the strip length rebuilt from its batches.
- Every packed vertex, scaled by its piece's position scale, lies inside its geometry's bounding sphere.
- `level99s_ms1.sec` geometry index 29 has 48 materials, 1,105 triangles and 1,839 vertices; its first material
  shows `properic140_001` with `bronxstain_000` as its second layer.

## Open questions

- The meaning of bit 16 of the material texture reference's filter word, set in every reference and in no
  dictionary texture.
- That the second texture-coordinate set of a dual-pass material maps the second-layer texture is inferred from the
  data, not observed in a capture.
- The 922 non-zero second-set vertices in 49 meshes of materials with no effect: whether they show anywhere.
- Why `objarena` stores atomic pipeline `0x00030082` and material id `0x00030083`, and its non-power-of-two scales.
- Whether `courtyard`, with no pipeline ids, no `0x3F0` plugin and float vertices, is ever loaded in play.
- The meaning of the atomic frame index word, which never names a frame.
- Why 1,151 pieces list their meshes in an order other than their materials'.
- World vertex colours always have alpha 255, while the PS2 display page saw lower vertex alpha on a few world
  strips; where that lower alpha comes from was not established.
- Which world parts, beyond the stream textures, are in memory when play begins depends on the camera
  (`level-loading` page); a fixed list of preloaded geometry was not found.
- The Xbox version's world geometry.
