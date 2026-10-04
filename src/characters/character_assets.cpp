// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_assets.h"

#include <format>
#include <utility>

namespace coney::characters {

namespace {

// The WAD entry of the resource named by `hash`; `what` names the resource for the message.
std::expected<const io::WadEntry*, Error> resourceEntry(const io::Wad& wad, std::uint32_t hash, std::string_view what) {
    auto entry = wad.lookup(resourceFileName(hash));
    if (!entry) {
        return fail(entry.error().code, std::format("{} {:#010x} (file {}): {}", what, hash, resourceFileName(hash),
                                                    entry.error().message));
    }
    return *entry;
}

} // namespace

std::expected<CharacterAssets, Error> loadCharacterAssets(const io::Wad& wad, const CharacterList& list,
                                                          const chunk::ChunkHandlerTable& table,
                                                          std::string_view name) {
    const CharacterRecord* record = list.find(name);
    if (record == nullptr) {
        return fail(ErrorCode::NotFound, std::format("no character called {} in the Character List", name));
    }
    auto assets = loadCharacterAssets(wad, *record, table);
    if (!assets) {
        return fail(assets.error().code, std::format("{}: {}", name, assets.error().message));
    }
    assets->name = std::string(name);
    return assets;
}

std::expected<CharacterAssets, Error> loadCharacterAssets(const io::Wad& wad, const CharacterRecord& record,
                                                          const chunk::ChunkHandlerTable& table) {
    auto modelEntry = resourceEntry(wad, record.modelHash, "model");
    auto dataEntry = resourceEntry(wad, record.dataHash, "character data");
    auto texturesEntry = resourceEntry(wad, record.texturesHash, "textures");
    if (!modelEntry || !dataEntry || !texturesEntry) {
        return std::unexpected(!modelEntry ? modelEntry.error()
                                           : (!dataEntry ? dataEntry.error() : texturesEntry.error()));
    }
    auto model = loadCharacterModel(wad, **modelEntry);
    if (!model) {
        return fail(model.error().code, std::format("model {:#010x}: {}", record.modelHash, model.error().message));
    }
    auto data = loadCharacterData(wad, **dataEntry, table);
    if (!data) {
        return fail(data.error().code,
                    std::format("character data {:#010x}: {}", record.dataHash, data.error().message));
    }
    CharacterAssets assets;
    assets.record = record;
    assets.model = std::move(*model);
    assets.data = std::move(*data);
    assets.textures = *texturesEntry;
    return assets;
}

} // namespace coney::characters
