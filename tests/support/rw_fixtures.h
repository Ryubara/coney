// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic RenderWare streams, built byte by byte in the tests. Nothing here comes from the game (LEGAL.md, "No game
// data"). The layouts follow RenderWare's binary stream format as librw reads it (its texture.cpp,
// ps2/ps2raster.cpp and, for a clump, clump.cpp); src/graphics/rw_stream.h describes what Coney checks.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

#include "graphics/rw_stream.h"
#include "support/fixtures.h"

namespace coney::test {

/// One RenderWare section: a 12-byte header (id, size of `body`, library stamp) and `body`.
inline Bytes rwSection(std::uint32_t id, const Bytes& body, std::uint32_t stamp = graphics::kRwLibraryStamp) {
    Bytes section;
    section.u32(id).u32(static_cast<std::uint32_t>(body.size())).u32(stamp);
    section.append(body.span());
    return section;
}

/// A string section as RenderWare writes a texture name: the characters, a terminating zero, padded to 4 bytes.
inline Bytes rwString(std::string_view text) {
    Bytes body;
    body.text(text).u8(0);
    body.padTo((body.size() + 3) / 4 * 4);
    return rwSection(graphics::kRwString, body);
}

/// The fields of a PS2 native texture's 64-byte raster header that the fixtures vary.
struct Ps2RasterFields {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 32;
    std::uint16_t rasterFormat = 0x0504; ///< C8888 | TEXTURE: 32-bit colour, a texture raster.
    std::uint16_t version = 0;           ///< 0: the older layout, plain rows of texels with no GIF packets.
    std::uint64_t tex0 = 0;              ///< The GS TEX0 register: buffer width, pixel format, size exponents.
    std::uint32_t totalSize = 0;         ///< GS memory the texture takes, in 32-bit words.
    std::uint32_t pixelBytes = 0;
    std::uint32_t paletteBytes = 0;
};

/// The raster header of a `width` x `height` 32-bit texture without mipmaps or palette, in the older layout, with the
/// register values librw's PS2 raster code computes for one (so that its consistency asserts hold). TEX0: buffer width
/// in 64-texel units at bit 14 (a 32-bit page is 64 texels wide, so at least 1), pixel format PSMCT32 (0) at bit 20,
/// log2 of the width at bit 26 and of the height at bit 30, and "texture has alpha" at bit 34. The texture fills whole
/// 64 x 32 texel pages of 2048 words each.
inline Ps2RasterFields rgba32Raster(std::uint32_t width, std::uint32_t height) {
    const auto log2Ceil = [](std::uint32_t n) {
        std::uint64_t bits = 0;
        for (std::uint32_t s = 1; s < n; s *= 2) {
            ++bits;
        }
        return bits;
    };
    Ps2RasterFields fields;
    fields.width = width;
    fields.height = height;
    const std::uint64_t bufferWidth = (width > 64 ? width : 64) / 64;
    fields.tex0 = bufferWidth << 14 | log2Ceil(width) << 26 | log2Ceil(height) << 30 | std::uint64_t{1} << 34;
    fields.totalSize = ((width + 63) / 64) * ((height + 31) / 32) * 2048;
    fields.pixelBytes = (width * height * 4 + 15) / 16 * 16;
    return fields;
}

/// A texture native section holding a PS2 texture: the platform header ("PS2\0" and a filter word), the name and an
/// empty mask, the raster (header and `pixels`) and an empty extension.
inline Bytes ps2Texture(std::string_view name, const Ps2RasterFields& raster, std::span<const std::byte> pixels) {
    Bytes platform;
    platform.u32(graphics::kPs2NativeTexture).u32(0x1102); // linear filtering, wrap in U and V
    Bytes header;
    header.u32(raster.width).u32(raster.height).u32(raster.depth);
    header.u16(raster.rasterFormat).u16(raster.version);
    header.u32(static_cast<std::uint32_t>(raster.tex0)).u32(static_cast<std::uint32_t>(raster.tex0 >> 32));
    header.u32(0).u32(0); // palette offset, TEX1 low word
    // MIPTBP1 and MIPTBP2: buffer width 1 for the six unused mipmap levels, as librw sets them without mipmaps.
    for (int reg = 0; reg < 2; ++reg) {
        header.u32(1U << 14).u32((1U << 2) | (1U << 22));
    }
    header.u32(raster.pixelBytes).u32(raster.paletteBytes).u32(raster.totalSize).u32(0xFC0); // mipmap K value
    Bytes pixelData;
    pixelData.append(pixels);
    Bytes rasterBody = rwSection(graphics::kRwStruct, header);
    rasterBody.append(rwSection(graphics::kRwStruct, pixelData).span());

    Bytes body = rwSection(graphics::kRwStruct, platform);
    body.append(rwString(name).span()).append(rwString("").span());
    body.append(rwSection(graphics::kRwStruct, rasterBody).span());
    body.append(rwSection(graphics::kRwExtension, Bytes{}).span());
    return rwSection(graphics::kRwTextureNative, body);
}

/// A 2 x 2 32-bit texture: red, green / blue, white, with PS2 alpha 0x80 (opaque) except the white texel, at 0x40.
inline Bytes rgbaTexture2x2(std::string_view name) {
    Bytes pixels;
    pixels.u8(255).u8(0).u8(0).u8(0x80).u8(0).u8(255).u8(0).u8(0x80);
    pixels.u8(0).u8(0).u8(255).u8(0x80).u8(255).u8(255).u8(255).u8(0x40);
    return ps2Texture(name, rgba32Raster(2, 2), pixels.span());
}

/// A texture dictionary section holding `textures` (texture native sections), for the PS2 (device 6), with an empty
/// extension.
inline Bytes texDictionary(std::initializer_list<Bytes> textures, std::uint16_t device = 6) {
    Bytes header;
    header.u16(static_cast<std::uint16_t>(textures.size())).u16(device);
    Bytes body = rwSection(graphics::kRwStruct, header);
    for (const Bytes& texture : textures) {
        body.append(texture.span());
    }
    body.append(rwSection(graphics::kRwExtension, Bytes{}).span());
    return rwSection(graphics::kRwTexDictionary, body);
}

/// A float as RenderWare streams it: its four bytes, little-endian.
inline Bytes& rwFloat(Bytes& bytes, float value) { return bytes.u32(std::bit_cast<std::uint32_t>(value)); }

/// A plain clump section (0x10) as librw's Clump::streamRead takes one, nothing native in it: one frame at the origin,
/// one geometry of a single triangle with positions only (0, 0, 0), (1, 0, 0), (0, 1, 0) and one untextured material
/// of `colour` (RGBA), and one atomic on them. The layouts follow librw's clump.cpp, frame.cpp and geometry.cpp at
/// the library version kRwLibraryStamp names (3.7, so the clump's struct counts lights and cameras and the geometry
/// keeps no surface properties of its own).
inline Bytes rwTriangleClump(std::array<std::uint8_t, 4> colour) {
    constexpr std::uint32_t kClump = 0x10;
    constexpr std::uint32_t kFrameList = 0x0E;
    constexpr std::uint32_t kGeometryList = 0x1A;
    constexpr std::uint32_t kGeometry = 0x0F;
    constexpr std::uint32_t kMaterialList = 0x08;
    constexpr std::uint32_t kMaterial = 0x07;
    constexpr std::uint32_t kAtomic = 0x14;
    const Bytes noExtension = rwSection(graphics::kRwExtension, Bytes{});

    // The frame list: one identity frame with no parent (-1).
    Bytes frameStruct;
    frameStruct.u32(1);
    for (const float value : {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F}) {
        rwFloat(frameStruct, value);
    }
    frameStruct.u32(0xFFFFFFFF).u32(0);
    Bytes frameList = rwSection(graphics::kRwStruct, frameStruct);
    frameList.append(noExtension.span());

    // The geometry: flags POSITIONS (0x2), one triangle, three vertices, one morph target; the triangle as
    // (v0 << 16 | v1, v2 << 16 | material); the morph target's bounding sphere, vertices and no normals.
    Bytes geometryStruct;
    geometryStruct.u32(0x2).u32(1).u32(3).u32(1);
    geometryStruct.u32((0U << 16) | 1U).u32((2U << 16) | 0U);
    for (const float value : {0.5F, 0.5F, 0.0F, 1.0F}) {
        rwFloat(geometryStruct, value);
    }
    geometryStruct.u32(1).u32(0);
    for (const float value : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F}) {
        rwFloat(geometryStruct, value);
    }
    // One material, new (-1): flags, colour, unused, not textured, then its surface properties.
    Bytes materialStruct;
    materialStruct.u32(0).u8(colour[0]).u8(colour[1]).u8(colour[2]).u8(colour[3]).u32(0).u32(0);
    for (const float value : {1.0F, 1.0F, 1.0F}) {
        rwFloat(materialStruct, value);
    }
    Bytes material = rwSection(graphics::kRwStruct, materialStruct);
    material.append(noExtension.span());
    Bytes materialListStruct;
    materialListStruct.u32(1).u32(0xFFFFFFFF);
    Bytes materialList = rwSection(graphics::kRwStruct, materialListStruct);
    materialList.append(rwSection(kMaterial, material).span());
    Bytes geometry = rwSection(graphics::kRwStruct, geometryStruct);
    geometry.append(rwSection(kMaterialList, materialList).span()).append(noExtension.span());
    Bytes geometryListStruct;
    geometryListStruct.u32(1);
    Bytes geometryList = rwSection(graphics::kRwStruct, geometryListStruct);
    geometryList.append(rwSection(kGeometry, geometry).span());

    // The atomic: frame 0, geometry 0, flags collision test and render (0x5).
    Bytes atomicStruct;
    atomicStruct.u32(0).u32(0).u32(0x5).u32(0);
    Bytes atomic = rwSection(graphics::kRwStruct, atomicStruct);
    atomic.append(noExtension.span());

    Bytes clumpStruct;
    clumpStruct.u32(1).u32(0).u32(0);
    Bytes clump = rwSection(graphics::kRwStruct, clumpStruct);
    clump.append(rwSection(kFrameList, frameList).span());
    clump.append(rwSection(kGeometryList, geometryList).span());
    clump.append(rwSection(kAtomic, atomic).span());
    clump.append(noExtension.span());
    return rwSection(kClump, clump);
}

} // namespace coney::test
