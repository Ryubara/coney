// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/asset_convert.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rw.h>

#include "graphics/texture_lod.h"

namespace coney::platform {

namespace {

// The failure of a mesh conversion, saying why.
std::unexpected<Error> meshFailure(std::string_view why) {
    return std::unexpected(Error{ErrorCode::Invalid, std::format("cannot convert the geometry: {}", why)});
}

// The failure of a texture conversion, naming the texture.
std::unexpected<Error> textureFailure(const rw::Texture& texture, std::string_view why) {
    return std::unexpected(
        Error{ErrorCode::Invalid, std::format("cannot convert texture \"{}\": {}", texture.name, why)});
}

// Coney's colour for librw's.
graphics::Rgba rgbaOf(const rw::RGBA& colour) { return {colour.red, colour.green, colour.blue, colour.alpha}; }

// Coney's filter for RenderWare's filter mode; an unknown mode reads as linear, as librw's GL3 device treats it.
graphics::TextureFilter filterOf(rw::int32 mode) {
    switch (mode) {
    case rw::Texture::NEAREST:
        return graphics::TextureFilter::Nearest;
    case rw::Texture::MIPNEAREST:
        return graphics::TextureFilter::MipNearest;
    case rw::Texture::MIPLINEAR:
        return graphics::TextureFilter::MipLinear;
    case rw::Texture::LINEARMIPNEAREST:
        return graphics::TextureFilter::LinearMipNearest;
    case rw::Texture::LINEARMIPLINEAR:
        return graphics::TextureFilter::LinearMipLinear;
    default:
        return graphics::TextureFilter::Linear;
    }
}

// Coney's addressing for RenderWare's; an unknown mode reads as wrap.
graphics::TextureAddress addressOf(rw::int32 mode) {
    switch (mode) {
    case rw::Texture::MIRROR:
        return graphics::TextureAddress::Mirror;
    case rw::Texture::CLAMP:
        return graphics::TextureAddress::Clamp;
    case rw::Texture::BORDER:
        return graphics::TextureAddress::Border;
    default:
        return graphics::TextureAddress::Wrap;
    }
}

// The RGBA texels of `image`, the top-left `width` x `height` of it, rows top first; the image is expanded to 32 bits
// first, as TextureDictionary::toImages() does.
graphics::TextureLevel levelOf(rw::Image* image, std::uint32_t width, std::uint32_t height) {
    image->convertTo32();
    graphics::TextureLevel level{width, height, {}};
    const std::size_t rowBytes = std::size_t{width} * 4;
    level.texels.reserve(rowBytes * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        const rw::uint8* row = image->pixels + static_cast<std::ptrdiff_t>(y) * image->stride;
        level.texels.insert(level.texels.end(), row, row + rowBytes);
    }
    return level;
}

// Reads mip level `level` (above 0) of a PS2 raster. librw's own route (lock the level, then make an image) unswizzles
// the level twice (platform/texture_dictionary.cpp, convertPs2ByLevels), so here the level is only positioned (locked
// without fetching) and the image's own lock of the positioned raster unswizzles it once. That lock's unlock swizzles
// the texels back and puts the raster's size and texel pointer back at level 0, which is the whole of an unlock: the
// positioning lock is not unlocked again, as a second unlock would swizzle level 0's texels once more. Null when librw
// makes no image.
rw::Image* ps2LevelImage(rw::Raster& raster, rw::int32 level) {
    raster.lock(level, rw::Raster::LOCKREAD | rw::Raster::LOCKNOFETCH);
    return raster.toImage();
}

// Keeps a PS2 raster's data block (texels, palette and GS packets) as it was when made, and puts it back when it goes.
// librw's read of a palette raster is not undone by its unlock when the raster is narrower than the GS transfer's
// minimum width (an 8-wide 8-bit or a 16-wide 4-bit raster): unswizzleRaster takes the row width from the padded
// width, swizzleRaster from the raster's own, so each read leaves the texels scrambled and the next read is wrong
// (docs/engine/asset-formats.md#scrambling-read). Putting the bytes back makes every read see what was loaded.
class Ps2RasterBytes {
  public:
    // Copies `raster`'s data block (a PS2 raster) as it is now.
    explicit Ps2RasterBytes(rw::Raster& raster) : m_raster(GETPS2RASTEREXT(&raster)) {
        if (m_raster->data != nullptr) {
            m_bytes.assign(m_raster->data, m_raster->data + m_raster->dataSize);
        }
    }

    // Writes the copy back over the data block, undoing whatever the reads in between left in it.
    ~Ps2RasterBytes() {
        if (!m_bytes.empty()) {
            std::memcpy(m_raster->data, m_bytes.data(), m_bytes.size());
        }
    }
    Ps2RasterBytes(const Ps2RasterBytes&) = delete;
    Ps2RasterBytes& operator=(const Ps2RasterBytes&) = delete;
    Ps2RasterBytes(Ps2RasterBytes&&) = delete;
    Ps2RasterBytes& operator=(Ps2RasterBytes&&) = delete;

  private:
    rw::ps2::Ps2Raster* m_raster;
    std::vector<rw::uint8> m_bytes;
};

} // namespace

std::expected<graphics::MeshData, Error> meshFromRw(const rw::Geometry& geometry, const SkinVertexData* skin) {
    // Only plain geometry has vertices to read.
    if ((geometry.flags & rw::Geometry::NATIVE) != 0) {
        return meshFailure("it is still PS2 native geometry");
    }
    const rw::MorphTarget* target = geometry.numMorphTargets > 0 ? &geometry.morphTargets[0] : nullptr;
    if (target == nullptr || target->vertices == nullptr) {
        return meshFailure("it has no positions");
    }
    if (geometry.meshHeader == nullptr) {
        return meshFailure("it has no mesh header");
    }
    const auto count = static_cast<std::size_t>(geometry.numVertices);
    if (skin != nullptr && (skin->weights.size() != count || skin->indices.size() != count)) {
        return meshFailure(std::format("its skin has {} weights and {} bone sets for {} vertices", skin->weights.size(),
                                       skin->indices.size(), count));
    }

    // What the vertices carry.
    graphics::MeshData mesh;
    mesh.attributes.normals = target->normals != nullptr;
    mesh.attributes.prelit = geometry.colors != nullptr;
    mesh.attributes.texCoordSets = static_cast<std::uint8_t>(
        std::clamp<rw::int32>(geometry.numTexCoordSets, 0, static_cast<rw::int32>(graphics::kMeshTexCoordSets)));
    mesh.attributes.skinned = skin != nullptr;

    // The vertices, what they do not carry left at its neutral value.
    mesh.vertices.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        graphics::MeshVertex& v = mesh.vertices[i];
        const rw::V3d& position = target->vertices[i];
        v.position = {position.x, position.y, position.z};
        if (mesh.attributes.normals) {
            const rw::V3d& normal = target->normals[i];
            v.normal = {normal.x, normal.y, normal.z};
        }
        if (mesh.attributes.prelit) {
            v.prelit = rgbaOf(geometry.colors[i]);
        }
        for (std::size_t set = 0; set < mesh.attributes.texCoordSets; ++set) {
            const rw::TexCoords& uv = geometry.texCoords[set][i];
            v.texCoords.at(set) = {uv.u, uv.v};
        }
        if (skin != nullptr) {
            v.bones = skin->indices[i];
            v.weights = skin->weights[i];
        }
    }

    // One sub-mesh per mesh of the header, in its order, over one index list; each material once, in the geometry's
    // material list order.
    mesh.topology = (geometry.meshHeader->flags & rw::MeshHeader::TRISTRIP) != 0 ? graphics::MeshTopology::TriangleStrip
                                                                                 : graphics::MeshTopology::TriangleList;
    for (rw::int32 m = 0; m < geometry.matList.numMaterials; ++m) {
        const rw::Material* material = geometry.matList.materials[m];
        mesh.materials.push_back({rgbaOf(material->color),
                                  material->texture != nullptr ? std::string(material->texture->name) : std::string()});
    }
    const rw::Mesh* meshes = geometry.meshHeader->getMeshes();
    mesh.indices.reserve(geometry.meshHeader->totalIndices);
    for (std::uint16_t m = 0; m < geometry.meshHeader->numMeshes; ++m) {
        const rw::Mesh& source = meshes[m];
        const auto* const begin = geometry.matList.materials;
        const auto* const end = begin + geometry.matList.numMaterials;
        const auto* const found = std::find(begin, end, source.material);
        if (found == end) {
            return meshFailure(std::format("mesh {} has a material not in the geometry's list", m));
        }
        mesh.subMeshes.push_back({static_cast<std::uint32_t>(mesh.indices.size()), source.numIndices,
                                  static_cast<std::uint32_t>(found - begin)});
        mesh.indices.insert(mesh.indices.end(), source.indices, source.indices + source.numIndices);
    }

    const rw::Sphere& sphere = target->boundingSphere;
    mesh.bounds = {{sphere.center.x, sphere.center.y, sphere.center.z}, sphere.radius};
    return mesh;
}

std::expected<graphics::TextureData, Error> textureFromRw(rw::Texture& texture) {
    if (texture.raster == nullptr) {
        return textureFailure(texture, "it has no raster");
    }
    rw::Raster& raster = *texture.raster;
    graphics::TextureData converted;
    converted.name = texture.name;
    converted.format = graphics::TextureFormat::Rgba8;
    converted.sampler = {filterOf(texture.getFilter()), addressOf(texture.getAddressU()),
                         addressOf(texture.getAddressV())};
    const bool ps2 = raster.platform == rw::PLATFORM_PS2;
    if (ps2) {
        converted.lod = graphics::unpackTextureLod(GETPS2RASTEREXT(&raster)->kl);
    }

    // Level 0 as librw reads it, then (a PS2 raster only) each further level, positioned and read once; a PS2 raster's
    // bytes are put back afterwards, whichever way this returns.
    std::optional<Ps2RasterBytes> keep;
    if (ps2) {
        keep.emplace(raster);
    }
    const auto width = static_cast<std::uint32_t>(raster.width);
    const auto height = static_cast<std::uint32_t>(raster.height);
    const rw::int32 levels = ps2 ? std::max<rw::int32>(raster.getNumLevels(), 1) : 1;
    for (rw::int32 level = 0; level < levels; ++level) {
        rw::Image* image = level == 0 ? raster.toImage() : ps2LevelImage(raster, level);
        if (image == nullptr) {
            return textureFailure(texture, std::format("librw made no image of level {}", level));
        }
        const std::uint32_t levelWidth = std::max<std::uint32_t>(width >> level, 1);
        const std::uint32_t levelHeight = std::max<std::uint32_t>(height >> level, 1);
        if (image->width < static_cast<rw::int32>(levelWidth) || image->height < static_cast<rw::int32>(levelHeight)) {
            image->destroy();
            return textureFailure(texture, std::format("librw's image of level {} is smaller than the level", level));
        }
        converted.levels.push_back(levelOf(image, levelWidth, levelHeight));
        image->destroy();
    }
    return converted;
}

} // namespace coney::platform
