// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/blood_textures.h"

#include <format>
#include <string_view>
#include <utility>

#include <rw.h>

#include "core/chunk_system.h"

namespace coney::platform {

namespace {

// The pack the resource manager loads at start-up, which holds the blood textures.
constexpr std::string_view kGlobalPack = "global.pak";

} // namespace

std::expected<BloodTextures, Error> BloodTextures::load(const RenderEngine& engine, const io::Wad& wad) {
    auto entry = wad.lookup(kGlobalPack);
    if (!entry) {
        return std::unexpected(std::move(entry.error()));
    }
    chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
    addTextureDictionaryHandlers(table);
    auto dictionaries = loadTextureDictionaries(wad, **entry, table);
    if (!dictionaries) {
        return std::unexpected(std::move(dictionaries.error()));
    }
    BloodTextures blood;
    blood.m_dictionaries = std::move(*dictionaries);
    // Each texture by its name, in any of the pack's dictionaries; only the dictionaries holding one are converted for
    // drawing, each once.
    std::vector<bool> converted(blood.m_dictionaries.size(), false);
    for (const graphics::BloodTexture which :
         {graphics::BloodTexture::Light, graphics::BloodTexture::Medium, graphics::BloodTexture::Heavy}) {
        const std::string_view name = graphics::bloodTextureName(which);
        for (std::size_t d = 0; d < blood.m_dictionaries.size(); ++d) {
            TextureDictionary& dictionary = blood.m_dictionaries[d];
            for (rw::Texture* texture : dictionary.textures()) {
                if (std::string_view(texture->name) != name) {
                    continue;
                }
                if (engine.drawsPixels() && !converted[d]) {
                    if (auto done = dictionary.convertForDrawing(); !done) {
                        return std::unexpected(std::move(done.error()));
                    }
                    converted[d] = true;
                }
                blood.m_textures.at(static_cast<std::size_t>(which)) = texture;
            }
        }
        if (blood.m_textures.at(static_cast<std::size_t>(which)) == nullptr) {
            return fail(ErrorCode::NotFound, std::format("{} holds no texture {}", kGlobalPack, name));
        }
    }
    return blood;
}

} // namespace coney::platform
