# Mesh and texture formats

Coney's own mesh and texture data: what both renderers draw from, converted once from what librw reads and held by
id. It is item 6 of [the renderer's migration](renderer.md#order). Nothing draws from it yet: the GL3 renderer keeps
librw's instancing, and the frame description (items 7 to 10) and the GPU renderer (items 13 and 14) are its first
users. All of it is Coney's own.

## The parts {#parts}

| Part | What it holds | Where |
| --- | --- | --- |
| Mesh (`graphics::MeshData`) | One vertex layout for every mesh: position, normal, prelit colour (RGBA bytes), two texture-coordinate sets (the base layer and the world's dual layer), four bone indices and weights. Which of them the source carried (`MeshAttributes`); what it did not is neutral (zero normal, white prelighting, zero weights). 16-bit indices, a topology (lists or strips, as librw's mesh header has them), one sub-mesh per librw mesh (an index range and a material: colour and texture name) and the bounding sphere | `src/graphics/mesh_data.h` |
| Texture (`graphics::TextureData`) | The name, a format (`Rgba8` only: every PS2 raster is 4- or 8-bit with a 32-bit palette, [PS2 raster](../research/formats/renderware.md#ps2-raster); BC1 to BC3 join with the Xbox build's DXT rasters), every mip level, the sampler (RenderWare's filter and addressing) and, for a PS2 raster, the GS level of detail (`graphics::TextureLod`) | `src/graphics/texture_data.h` |
| Store (`graphics::AssetStore`) | Meshes and textures by `MeshId` and `TextureId`: counted up from 1 and never handed out twice, so a stale id finds nothing. It takes only data that holds together (`validateMesh`, `validateTexture`), so a renderer uploads what it finds without checking it again. One thread; the loader thread (item 43) decides how it is shared | `src/graphics/asset_store.h` |
| Conversion | `platform::meshFromRw` reads plain `rw::Geometry` (world atomics after `WorldAtomic::unpack()`; characters with their `SkinVertexData`); `platform::textureFromRw` reads a texture's raster on either backend, the headless one included | `src/platform/asset_convert.h` |
| Hashes | `hashMesh`, `hashTexture`: 64-bit FNV-1a over every field, floats by their bits, so the disc check prints hashes in place of data | `src/graphics/asset_hash.h` |

## Reading PS2 rasters {#ps2-rasters}

### Mip levels {#mip-levels}

A PS2 raster's levels are each read once. librw's own route (lock level *i*, then make an image) unswizzles a level
twice, the bug [convertPs2ByLevels](repo:src/platform/texture_dictionary.cpp) works round for the
GL3 path; `textureFromRw` positions the raster on the level without fetching and lets the image's own lock unswizzle
it.

### A read that scrambles {#scrambling-read}

librw's read of a PS2 palette raster is not undone by its unlock when the raster is narrower than the GS transfer's
minimum width (an 8-wide 8-bit or a 16-wide 4-bit raster): `unswizzleRaster` takes the row width from the padded
width and `swizzleRaster` from the raster's own, so each read leaves the texels scrambled and only the first read is
right. Five of `level99`'s 510 textures are such rasters.
`textureFromRw` keeps a PS2 raster's data block before it reads and puts it back after, so it leaves the raster as
loaded and a later read (its own, the GL3 path's or `toImages()`) still sees the right texels. A read of the same
raster through librw alone, before or after, is not protected; the GL3 path reads each raster once.

## How it is tested {#tests}

Unit tests on hand-built geometry and rasters (`tests/platform/asset_convert_test.cpp`: vertex counts, every
attribute, strips and lists, skin weights, a 32-bit raster against `TextureDictionary::toImages()`, an 8-bit palette
raster against its palette, a mipmapped 32-bit raster level by level, and both workarounds above without the disc: an
8-wide 8-bit raster converted twice and then read by librw, and a mipmapped 8-bit raster whose last level is narrower
than the transfer, each level against librw's first read of a lone raster of its size). The store's tests
(`tests/graphics/asset_store_test.cpp`) include the textures it refuses, among them a 2^31 x 2^31 level whose byte
count would wrap and 33 levels of a 1 x 1 texture: `validateTexture` takes no level 0 wider or taller than
`kMaxTextureSide` (16384) and no more levels than halving level 0 down to 1 x 1 gives.

With the disc, `tests/platform/disc_asset_convert_test.cpp` converts every part atomic and every texture of `level99`'s
worlds into one store and checks each against librw: vertex and index counts mesh by mesh, and level 0's texels against
`toImages()` run after the conversion, so a match also shows the raster was left as loaded. It prints counts and hashes
only. On the NTSC-U disc: 2 worlds, 33 WAD entries; 74 of 74 atomics stored (88,450 vertices, 136,878 indices, 1,817
sub-meshes); 510 of 510 textures stored (541 levels, 2,068,224 texels); no mismatch with librw and no failure.
