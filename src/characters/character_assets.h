// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "characters/character_data.h"
#include "characters/character_list.h"
#include "characters/character_model.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/wad.h"

// A character's three resources, found by model name through the Character List: the model and the character data
// decoded here, the texture dictionary's WAD entry for the platform layer to read (textures need librw).
// Research: docs/research/characters.md#files.

namespace coney::characters {

/// A character's resources, loaded.
struct CharacterAssets {
    std::string name;               ///< The model name it was found by.
    CharacterRecord record;         ///< Its Character List record.
    CharacterModel model;           ///< From the model resource.
    CharacterData data;             ///< From the character data resource.
    const io::WadEntry* textures{}; ///< The texture dictionary resource; owned by the WAD's index.
};

/// Finds `name` in `list` and loads its model and character data from `wad`, the character data through the chunk
/// system with `table` (which needs addCharacterDataHandlers()). Each resource is the WAD file named by its hash in
/// decimal. Fails with ErrorCode::NotFound when the name is not in the list or a resource is not on the disc, and as
/// loadCharacterModel() and loadCharacterData() do; the message names the character and the resource.
[[nodiscard]] std::expected<CharacterAssets, Error> loadCharacterAssets(const io::Wad& wad, const CharacterList& list,
                                                                        const chunk::ChunkHandlerTable& table,
                                                                        std::string_view name);

/// As loadCharacterAssets(), from a record rather than a name.
[[nodiscard]] std::expected<CharacterAssets, Error>
loadCharacterAssets(const io::Wad& wad, const CharacterRecord& record, const chunk::ChunkHandlerTable& table);

} // namespace coney::characters
