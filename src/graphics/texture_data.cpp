// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/texture_data.h"

#include <algorithm>
#include <format>

#include "graphics/asset_hash.h"

namespace coney::graphics {

std::size_t bytesPerTexel(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8:
        return 4;
    }
    return 4;
}

std::expected<void, Error> validateTexture(const TextureData& texture) {
    if (texture.levels.empty()) {
        return fail(ErrorCode::Invalid, std::format("texture \"{}\" has no levels", texture.name));
    }
    const TextureLevel& top = texture.levels[0];
    if (top.width == 0 || top.height == 0) {
        return fail(ErrorCode::Invalid, std::format("texture \"{}\" is {} x {}", texture.name, top.width, top.height));
    }
    // Each level half the one before, never below 1, its texels exactly its size.
    for (std::size_t i = 0; i < texture.levels.size(); ++i) {
        const TextureLevel& level = texture.levels[i];
        const std::uint32_t width = std::max<std::uint32_t>(top.width >> i, 1);
        const std::uint32_t height = std::max<std::uint32_t>(top.height >> i, 1);
        if (level.width != width || level.height != height) {
            return fail(ErrorCode::Invalid, std::format("texture \"{}\" level {} is {} x {}, not {} x {}", texture.name,
                                                        i, level.width, level.height, width, height));
        }
        const std::size_t bytes = std::size_t{width} * height * bytesPerTexel(texture.format);
        if (level.texels.size() != bytes) {
            return fail(ErrorCode::Invalid, std::format("texture \"{}\" level {} holds {} bytes, not {}", texture.name,
                                                        i, level.texels.size(), bytes));
        }
    }
    return {};
}

std::uint64_t hashTexture(const TextureData& texture) {
    AssetHash hash;
    hash.add(texture.name).add(static_cast<std::uint8_t>(texture.format));
    hash.add(static_cast<std::uint8_t>(texture.sampler.filter));
    hash.add(static_cast<std::uint8_t>(texture.sampler.addressU));
    hash.add(static_cast<std::uint8_t>(texture.sampler.addressV));
    hash.add(texture.lod.has_value());
    if (texture.lod) {
        hash.add(texture.lod->k).add(texture.lod->l);
    }
    hash.add(texture.levels.size());
    for (const TextureLevel& level : texture.levels) {
        hash.add(level.width).add(level.height).add(level.texels.size());
        for (const std::uint8_t texel : level.texels) {
            hash.add(texel);
        }
    }
    return hash.value();
}

} // namespace coney::graphics
