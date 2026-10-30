// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>

#include "core/error.h"
#include "graphics/mesh_data.h"
#include "graphics/texture_data.h"
#include "platform/character_skin.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Geometry;
struct Texture;
} // namespace rw

namespace coney::platform {

/// Converts librw's data into Coney's own mesh and texture formats (graphics/mesh_data.h, graphics/texture_data.h),
/// once per mesh and texture, for the GPU renderer, which cannot use librw's instancing into OpenGL buffers. Both work
/// on either backend, the headless one included: they read what librw has read and change nothing of it. Design:
/// docs/engine/asset-formats.md.

/// Converts plain (not PS2 native) `geometry`: its first morph target's positions and normals, its prelighting, its
/// first two texture-coordinate sets, its mesh header's meshes in order as sub-meshes over one index list (strips or
/// lists, as the header says), each mesh's material colour and texture name, and its bounding sphere. With `skin`, each
/// vertex also takes its bones and weights from it (one entry per vertex, as the GPU skinning pipeline reads them).
/// Fails with ErrorCode::Invalid when the geometry is still native (unpack it first, platform/world_atomic.h), has no
/// mesh header or no positions, a mesh's material is not in the geometry's list, or `skin` has the wrong length.
[[nodiscard]] std::expected<graphics::MeshData, Error> meshFromRw(const rw::Geometry& geometry,
                                                                  const SkinVertexData* skin = nullptr);

/// Converts `texture`'s raster into RGBA texels, every mip level the raster has, with the texture's name, its filter
/// and addressing, and for a PS2 raster its GS level of detail. A PS2 raster is read level by level as librw reads
/// level 0 (palettes expanded, alpha 0 to 128 scaled to 0 to 255), each level once, and the raster is left as it was;
/// a raster of another platform gives level 0 only. Fails with ErrorCode::Invalid when the texture has no raster or
/// librw cannot make an image of a level.
[[nodiscard]] std::expected<graphics::TextureData, Error> textureFromRw(rw::Texture& texture);

} // namespace coney::platform
