// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenes/scene_disc.h"

#include <format>
#include <string>
#include <utility>
#include <vector>

#include "core/chunk_system.h"

namespace coney::scenes {

namespace {

// The bytes of the WAD entry `name`.
std::expected<std::vector<std::byte>, Error> readEntry(const io::Wad& wad, std::string_view name) {
    auto entry = wad.lookup(name);
    if (!entry) {
        return std::unexpected(std::move(entry.error()));
    }
    auto stream = wad.openEntry(**entry);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    std::vector<std::byte> bytes((*entry)->size);
    if (auto read = stream->read(bytes); !read) {
        return std::unexpected(std::move(read.error()));
    }
    return bytes;
}

} // namespace

std::expected<SceneList, Error> loadSceneList(const io::Wad& wad) {
    auto entry = wad.lookup(kSceneListFile);
    if (!entry) {
        return std::unexpected(std::move(entry.error()));
    }
    auto stream = wad.openEntry(**entry);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    // A flat container, or failing that a pack of resources; the Scene List chunk stays on the stack, read raw.
    const chunk::ChunkHandlerTable table;
    chunk::ChunkStacks stacks;
    if (auto flat = chunk::loadContainer(*stream, table, stacks); !flat) {
        stacks = chunk::ChunkStacks{};
        if (auto seek = stream->seek(0); !seek) {
            return std::unexpected(std::move(seek.error()));
        }
        if (auto grouped = chunk::loadGroupedContainer(*stream, table, stacks); !grouped) {
            return std::unexpected(std::move(grouped.error()));
        }
    }
    std::vector<chunk::ChunkData> chunks = stacks.takeChunks(kSceneListChunk);
    if (chunks.empty()) {
        return fail(ErrorCode::NotFound, std::format("{}: no Scene List chunk", kSceneListFile));
    }
    return SceneList::parse(chunks.front().bytes);
}

SceneRecordSource wadSceneSource(const io::Wad& wad) {
    return [&wad](std::string_view name) { return readEntry(wad, std::format("{}.scn", name)); };
}

} // namespace coney::scenes
