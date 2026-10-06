// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/object_models.h"

#include <format>
#include <utility>

#include <rw.h>

#include "characters/character_list.h"
#include "gamemodes/load_entry_mode.h"
#include "platform/level_file.h"
#include "world/level_object.h"

namespace coney::platform {

std::expected<FirstTexture, Error> loadFirstTexture(const io::Wad& wad, const io::WadEntry& entry,
                                                    const chunk::ChunkHandlerTable& table, bool forDrawing) {
    auto dictionaries = loadTextureDictionaries(wad, entry, table);
    if (!dictionaries) {
        return std::unexpected(std::move(dictionaries.error()));
    }
    // The NULL engine cannot convert; its textures stay as read.
    for (TextureDictionary& dictionary : *dictionaries) {
        if (auto converted = forDrawing ? dictionary.convertForDrawing() : std::expected<void, Error>{}; !converted) {
            return std::unexpected(std::move(converted.error()));
        }
    }
    FirstTexture result;
    if (!dictionaries->empty()) {
        const std::vector<rw::Texture*> textures = dictionaries->front().textures();
        result.texture = textures.empty() ? nullptr : textures.front();
    }
    result.dictionaries = std::move(*dictionaries);
    return result;
}

std::expected<ObjectModel, ObjectFailure> loadObjectModel(const io::Wad& wad, const world_objects::ObjectRecord& record,
                                                          const chunk::ChunkHandlerTable& table, bool forDrawing) {
    const auto failed = [](bool noModel, Error error) {
        return std::unexpected(ObjectFailure{noModel, std::move(error)});
    };
    auto modelEntry = wad.lookup(characters::resourceFileName(record.modelHash));
    auto texturesEntry = wad.lookup(characters::resourceFileName(record.texturesHash));
    if (!modelEntry || !texturesEntry) {
        return failed(true, !modelEntry ? modelEntry.error() : texturesEntry.error());
    }
    // The model: one 0x47 chunk, which pushes it as a level model (0x41).
    auto load = loadWadEntry(wad, **modelEntry, table);
    if (!load) {
        return failed(
            true, Error{load.error().code, std::format("model {:#010x}: {}", record.modelHash, load.error().message)});
    }
    std::vector<chunk::ChunkData> models = load->stacks.takeChunks(world::kLevelModelResult);
    if (models.empty() || models.front().object == nullptr) {
        return failed(true, Error{ErrorCode::Invalid, std::format("model {:#010x}: no model", record.modelHash)});
    }
    auto texture = loadFirstTexture(wad, **texturesEntry, table, forDrawing);
    if (!texture) {
        return failed(false, texture.error());
    }
    return ObjectModel{std::move(models.front().object), std::move(*texture)};
}

} // namespace coney::platform
