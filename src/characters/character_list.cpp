// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_list.h"

#include <algorithm>
#include <format>
#include <utility>

#include "core/chunk_system.h"
#include "core/name_hash.h"
#include "fileio/reader.h"
#include "gamemodes/load_entry_mode.h"

namespace coney::characters {

std::expected<CharacterList, Error> CharacterList::parse(std::span<const std::byte> chunk) {
    io::Reader reader(chunk);
    auto count = reader.readU32Le();
    if (!count) {
        return std::unexpected(std::move(count.error()));
    }
    // Check the size before allocating, so a damaged count cannot make us reserve gigabytes.
    if (chunk.size() < kCharacterRecordsOffset ||
        (chunk.size() - kCharacterRecordsOffset) / kCharacterRecordBytes < *count) {
        return fail(ErrorCode::Truncated,
                    std::format("the Character List holds {} bytes, too few for its {} records", chunk.size(), *count));
    }
    CharacterList list;
    list.m_records.reserve(*count);
    reader = io::Reader(chunk.subspan(kCharacterRecordsOffset));
    for (std::uint32_t i = 0; i < *count; ++i) {
        // The size check above covers every read in this loop.
        CharacterRecord record;
        record.nameHash = reader.readU32Le().value();
        record.dataHash = reader.readU32Le().value();
        record.modelHash = reader.readU32Le().value();
        record.texturesHash = reader.readU32Le().value();
        record.dataSize = reader.readU32Le().value();
        record.modelSize = reader.readU32Le().value();
        record.modelSize2 = reader.readU32Le().value();
        record.texturesSize = reader.readU32Le().value();
        list.m_records.push_back(record);
    }
    return list;
}

const CharacterRecord* CharacterList::find(std::string_view name) const {
    const std::uint32_t hash = characterNameHash(name);
    const auto found = std::ranges::find(m_records, hash, &CharacterRecord::nameHash);
    return found == m_records.end() ? nullptr : &*found;
}

std::uint32_t characterNameHash(std::string_view name) { return crc32(lowercaseAscii(name)); }

std::string resourceFileName(std::uint32_t hash) { return std::format("{}", hash); }

std::expected<CharacterList, Error> loadCharacterList(const io::Wad& wad) {
    auto entry = wad.lookup("warriors.glr");
    if (!entry) {
        return std::unexpected(std::move(entry.error()));
    }
    // No handlers are needed: the list is one raw chunk among the global resource's game-wide lists.
    const chunk::ChunkHandlerTable table;
    auto load = loadWadEntry(wad, **entry, table);
    if (!load) {
        return std::unexpected(std::move(load.error()));
    }
    std::vector<chunk::ChunkData> chunks = load->stacks.takeChunks(kCharacterListChunk);
    if (chunks.empty()) {
        return fail(ErrorCode::NotFound, "warriors.glr holds no Character List (chunk 0x44)");
    }
    return CharacterList::parse(chunks.front().bytes);
}

} // namespace coney::characters
