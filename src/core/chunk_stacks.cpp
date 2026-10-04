// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chunk_stacks.h"

#include "core/assert.h"
#include "core/chunk_types.h"

namespace coney::chunk {

namespace {

// Formats a chunk type for an error message: "0x17 (Level Header)", or just the number for a type with no name.
std::string typeLabel(std::uint32_t type) {
    const std::string_view name = chunkTypeName(type);
    return name.empty() ? std::format("{:#04x}", type) : std::format("{:#04x} ({})", type, name);
}

} // namespace

void ChunkStacks::pushChunk(ChunkData chunk) { m_chunks.push_back(std::move(chunk)); }

std::expected<ChunkData, Error> ChunkStacks::popChunk(std::uint32_t expectedType) {
    if (m_chunks.empty()) {
        return fail(ErrorCode::Invalid,
                    std::format("expected a chunk of type {} but the chunk stack is empty", typeLabel(expectedType)));
    }
    if (m_chunks.back().type != expectedType) {
        return fail(ErrorCode::Invalid, std::format("expected a chunk of type {} but the top chunk is of type {}",
                                                    typeLabel(expectedType), typeLabel(m_chunks.back().type)));
    }
    ChunkData top = std::move(m_chunks.back());
    m_chunks.pop_back();
    return top;
}

std::optional<std::uint32_t> ChunkStacks::peekChunkType() const {
    if (m_chunks.empty()) {
        return std::nullopt;
    }
    return m_chunks.back().type;
}

void ChunkStacks::pushObject(std::unique_ptr<LoadedObject> object) {
    CONEY_ASSERT(object != nullptr);
    m_objects.push_back(std::move(object));
}

std::expected<std::unique_ptr<LoadedObject>, Error> ChunkStacks::popAnyObject() {
    if (m_objects.empty()) {
        return fail(ErrorCode::Invalid, "a handler expected an object but the object stack is empty");
    }
    std::unique_ptr<LoadedObject> top = std::move(m_objects.back());
    m_objects.pop_back();
    return top;
}

std::vector<ChunkData> ChunkStacks::takeChunks(std::uint32_t type) {
    std::vector<ChunkData> taken;
    std::vector<ChunkData> kept;
    kept.reserve(m_chunks.size());
    for (ChunkData& chunk : m_chunks) {
        (chunk.type == type ? taken : kept).push_back(std::move(chunk));
    }
    m_chunks = std::move(kept);
    return taken;
}

} // namespace coney::chunk
