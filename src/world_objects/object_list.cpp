// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/object_list.h"

#include <algorithm>
#include <format>
#include <utility>

#include "characters/character_list.h"
#include "core/chunk_system.h"
#include "fileio/reader.h"
#include "gamemodes/load_entry_mode.h"

namespace coney::world_objects {

std::expected<ObjectList, Error> ObjectList::parse(std::span<const std::byte> chunk) {
    io::Reader reader(chunk);
    auto count = reader.readU32Le();
    if (!count) {
        return std::unexpected(std::move(count.error()));
    }
    // Check the size before allocating, so a damaged count cannot make us reserve gigabytes.
    if (chunk.size() < kObjectRecordsOffset || (chunk.size() - kObjectRecordsOffset) / kObjectRecordBytes < *count) {
        return fail(ErrorCode::Truncated,
                    std::format("the Object List holds {} bytes, too few for its {} records", chunk.size(), *count));
    }
    ObjectList list;
    list.m_records.reserve(*count);
    reader = io::Reader(chunk.subspan(kObjectRecordsOffset));
    for (std::uint32_t i = 0; i < *count; ++i) {
        // The size check above covers every read in this loop.
        ObjectRecord record;
        record.nameHash = reader.readU32Le().value();
        record.variantOf = reader.readU32Le().value();
        record.modelHash = reader.readU32Le().value();
        record.texturesHash = reader.readU32Le().value();
        record.fourthHash = reader.readU32Le().value();
        record.modelSize = reader.readU32Le().value();
        record.modelSize2 = reader.readU32Le().value();
        record.texturesSize = reader.readU32Le().value();
        record.fourthSize = reader.readU32Le().value();
        list.m_records.push_back(record);
    }
    return list;
}

const ObjectRecord* ObjectList::find(std::string_view name) const {
    // Object names hash as model names do (docs/research/name-hash.md).
    const std::uint32_t hash = characters::characterNameHash(name);
    const auto found = std::ranges::find(m_records, hash, &ObjectRecord::nameHash);
    return found == m_records.end() ? nullptr : &*found;
}

std::expected<ObjectList, Error> loadObjectList(const io::Wad& wad) {
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
    std::vector<chunk::ChunkData> chunks = load->stacks.takeChunks(kObjectListChunk);
    if (chunks.empty()) {
        return fail(ErrorCode::NotFound, "warriors.glr holds no Object List (chunk 0x46)");
    }
    return ObjectList::parse(chunks.front().bytes);
}

} // namespace coney::world_objects
