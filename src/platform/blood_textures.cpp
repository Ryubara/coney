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

// Lets go of each hold, unless they were moved away.
BloodTextures::Holds::~Holds() {
    if (assets != nullptr) {
        for (const graphics::TextureId id : ids) {
            assets->removeTexture(id);
        }
    }
}

// Takes the other's holds, leaving it with none to let go.
BloodTextures::Holds::Holds(Holds&& other) noexcept
    : assets(std::exchange(other.assets, nullptr)), ids(std::move(other.ids)) {}

// Lets go of this one's holds, then takes the other's.
BloodTextures::Holds& BloodTextures::Holds::operator=(Holds&& other) noexcept {
    if (this != &other) {
        Holds dropped(std::move(*this));
        assets = std::exchange(other.assets, nullptr);
        ids = std::move(other.ids);
    }
    return *this;
}

std::expected<BloodTextures, Error> BloodTextures::load(RenderEngine& engine, const io::Wad& wad) {
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
    blood.m_holds.assets = &engine.sceneAssets();
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
                // Held in the scene assets before any conversion for librw, so its raster is read as loaded (no
                // texture is converted on GL3, whose renderer draws the librw texture).
                if (auto id = engine.sceneAssets().addTexture(*texture); id && id->valid()) {
                    blood.m_holds.ids.push_back(*id);
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
