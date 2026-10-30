// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// @coney-only renderer data: the GPU renderer's own texture format (docs/engine/asset-formats.md);
// the original sends its PS2 rasters to the GS, which has no counterpart here

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "core/error.h"
#include "graphics/texture_lod.h"

namespace coney::graphics {

/// How a texture level's texels are stored.
enum class TextureFormat : std::uint8_t {
    /// Four bytes a texel, red, green, blue, alpha, rows top first; alpha 255 is opaque. What every PS2 raster becomes:
    /// its palettes expanded and its alpha range (0 to 128) scaled to 0 to 255. Block-compressed formats (BC1 to BC3,
    /// which the Xbox build's DXT rasters already are) join when the Xbox rasters are converted.
    Rgba8,
};

/// How a texture is filtered, as RenderWare's texture names it.
enum class TextureFilter : std::uint8_t {
    Nearest,
    Linear,
    MipNearest,       ///< Nearest texel of the nearest level.
    MipLinear,        ///< Nearest texel, blended between two levels.
    LinearMipNearest, ///< Bilinear in the nearest level.
    LinearMipLinear,  ///< Bilinear, blended between two levels.
};

/// What a texture coordinate outside 0 to 1 reads, per axis, as RenderWare's texture names it.
enum class TextureAddress : std::uint8_t {
    Wrap,
    Mirror,
    Clamp,
    Border,
};

/// How a texture is sampled.
struct TextureSampler {
    TextureFilter filter = TextureFilter::Linear;
    TextureAddress addressU = TextureAddress::Wrap;
    TextureAddress addressV = TextureAddress::Wrap;

    friend bool operator==(const TextureSampler&, const TextureSampler&) = default;
};

/// One mipmap level: its size and texels in the texture's format.
struct TextureLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> texels;

    friend bool operator==(const TextureLevel&, const TextureLevel&) = default;
};

/// A texture in Coney's format: converted once from the raster librw reads, then drawn by either renderer and uploaded
/// by the GPU renderer as it is. Level 0 is the full size; each further level halves the one before (never below 1).
struct TextureData {
    std::string name;
    TextureFormat format = TextureFormat::Rgba8;
    std::vector<TextureLevel> levels;
    TextureSampler sampler;
    std::optional<TextureLod> lod; ///< The GS's level of detail, for a texture from a PS2 raster.
};

/// The bytes one texel takes in `format`.
[[nodiscard]] std::size_t bytesPerTexel(TextureFormat format);

/// Checks that `texture` holds together: at least one level, a non-zero level 0, each later level half the one before
/// (rounded down, never below 1) and each level's texels exactly its size in the format. ErrorCode::Invalid naming the
/// first fault.
[[nodiscard]] std::expected<void, Error> validateTexture(const TextureData& texture);

/// A 64-bit FNV-1a hash of everything `texture` holds (its name, format, sampler, level of detail and every level's
/// texels): equal textures hash equal on every platform. For checks that print a hash instead of the data (LEGAL.md).
[[nodiscard]] std::uint64_t hashTexture(const TextureData& texture);

} // namespace coney::graphics
