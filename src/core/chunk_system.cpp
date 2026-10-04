// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chunk_system.h"

#include <format>
#include <string>
#include <utility>

#include "core/assert.h"

namespace coney::chunk {

namespace {

/// Prefixes an Error's message with where in the stream it happened, keeping its code.
std::unexpected<Error> at(std::string_view where, const Error& error) {
    return fail(error.code, std::format("{}: {}", where, error.message));
}

std::expected<ContainerHeader, Error> readContainerHeader(io::Stream& stream) {
    if (stream.remaining() < kHeaderSize) {
        return fail(ErrorCode::Truncated, std::format("the container header needs {} bytes but only {} remain",
                                                      kHeaderSize, stream.remaining()));
    }
    // The size check covers all four reads.
    ContainerHeader header;
    header.count = stream.readU32Le().value();
    header.payloadBytes = stream.readU32Le().value();
    header.zero = stream.readU32Le().value();
    header.id = stream.readU32Le().value();
    return header;
}

/// Fails unless `count` headers of 16 bytes could still fit in the stream. Checked before a loop so that a damaged
/// count fails at once instead of after reading everything else.
std::expected<void, Error> checkCountFits(io::Stream& stream, std::uint32_t count, std::string_view what) {
    if (std::uint64_t{count} * kHeaderSize > stream.remaining()) {
        return fail(ErrorCode::Truncated, std::format("{} {} need at least {} bytes but only {} remain", count, what,
                                                      std::uint64_t{count} * kHeaderSize, stream.remaining()));
    }
    return {};
}

/// Reads one chunk and runs its handlers: the loop body the original's two loaders share.
std::expected<void, Error> loadChunk(io::Stream& stream, const ChunkHandlerTable& table, ChunkStacks& stacks,
                                     LoadReport& report) {
    const std::uint64_t headerOffset = stream.tell();
    const std::size_t chunkIndex = report.chunks.size();
    const auto where = [&](const ChunkHeader* header) {
        if (header == nullptr) {
            return std::format("chunk {} at offset {:#x}", chunkIndex, headerOffset);
        }
        return std::format("chunk {} at offset {:#x} (type {:#04x} {}, {} bytes)", chunkIndex, headerOffset,
                           header->type, chunkTypeName(header->type), header->size);
    };
    if (stream.remaining() < kHeaderSize) {
        return at(where(nullptr), Error{ErrorCode::Truncated, "the chunk header is cut off"});
    }
    ChunkHeader header;
    header.type = stream.readU32Le().value(); // the size check covers all four reads
    header.size = stream.readU32Le().value();
    header.zero = stream.readU32Le().value();
    header.id = stream.readU32Le().value();
    if (header.type >= kChunkTypeCount) {
        // The original indexes its table with the type unchecked; 0x54 is the table's terminator.
        return at(where(nullptr), Error{ErrorCode::Invalid, std::format("chunk type {:#x} is not a valid type (0x00 "
                                                                        "to 0x53)",
                                                                        header.type)});
    }
    if (header.size > stream.remaining()) {
        return at(where(&header),
                  Error{ErrorCode::Truncated, std::format("the data runs {} bytes past the end of the stream",
                                                          header.size - stream.remaining())});
    }

    const ChunkHandlers& handlers = table.handlers(header.type);
    const bool byHandler = static_cast<bool>(handlers.readFromStream);
    if (byHandler) {
        const std::uint64_t dataStart = stream.tell();
        auto window = io::SubStream::open(stream, header.size);
        if (!window) {
            return at(where(&header), window.error());
        }
        if (auto read = handlers.readFromStream(*window, header, stacks); !read) {
            return at(where(&header), read.error());
        }
        // Skip what the reader left unread. The window reads through the parent, so the parent's position is wherever
        // the reader's last read left it; set it to the end of the chunk outright.
        if (auto moved = stream.seek(dataStart + header.size); !moved) {
            return at(where(&header), moved.error());
        }
    } else {
        ChunkData chunk;
        chunk.type = header.type;
        chunk.id = header.id;
        chunk.bytes.resize(header.size);
        if (auto read = stream.read(chunk.bytes); !read) {
            return at(where(&header), read.error());
        }
        stacks.pushChunk(std::move(chunk));
    }
    report.chunks.push_back(ChunkRecord{header, byHandler});

    if (handlers.onLoaded) {
        if (auto loaded = handlers.onLoaded(stacks, header.type); !loaded) {
            return at(where(&header), loaded.error());
        }
    }
    return {};
}

} // namespace

ChunkHandlerTable ChunkHandlerTable::withDefaults() {
    ChunkHandlerTable table;
    for (const std::uint32_t type : {kCameraAnimation, kNullPointer, kGbhScript}) {
        table.setHandlers(type, ChunkHandlers{retagAsNullPointer, {}});
    }
    return table;
}

void ChunkHandlerTable::setHandlers(std::uint32_t type, ChunkHandlers handlers) {
    CONEY_ASSERT(type < kChunkTypeCount);
    m_handlers[type] = std::move(handlers);
}

const ChunkHandlers& ChunkHandlerTable::handlers(std::uint32_t type) const {
    CONEY_ASSERT(type < kChunkTypeCount);
    return m_handlers[type];
}

std::expected<void, Error> retagAsNullPointer(ChunkStacks& stacks, std::uint32_t type) {
    auto chunk = stacks.popChunk(type);
    if (!chunk) {
        return std::unexpected(std::move(chunk.error()));
    }
    chunk->type = kNullPointer;
    stacks.pushChunk(std::move(*chunk));
    return {};
}

std::expected<LoadReport, Error> loadContainer(io::Stream& stream, const ChunkHandlerTable& table,
                                               ChunkStacks& stacks) {
    LoadReport report;
    auto header = readContainerHeader(stream);
    if (!header) {
        return std::unexpected(std::move(header.error()));
    }
    report.container = *header;
    if (auto fits = checkCountFits(stream, header->count, "chunks"); !fits) {
        return std::unexpected(std::move(fits.error()));
    }
    for (std::uint32_t i = 0; i < header->count; ++i) {
        if (auto loaded = loadChunk(stream, table, stacks, report); !loaded) {
            return std::unexpected(std::move(loaded.error()));
        }
    }
    return report;
}

std::expected<LoadReport, Error> loadGroupedContainer(io::Stream& stream, const ChunkHandlerTable& table,
                                                      ChunkStacks& stacks) {
    LoadReport report;
    auto header = readContainerHeader(stream);
    if (!header) {
        return std::unexpected(std::move(header.error()));
    }
    report.container = *header;
    if (auto fits = checkCountFits(stream, header->count, "groups"); !fits) {
        return std::unexpected(std::move(fits.error()));
    }
    for (std::uint32_t g = 0; g < header->count; ++g) {
        const std::uint64_t groupOffset = stream.tell();
        const std::string where = std::format("group {} at offset {:#x}", g, groupOffset);
        if (stream.remaining() < kHeaderSize) {
            return at(where, Error{ErrorCode::Truncated, "the group header is cut off"});
        }
        GroupHeader group;
        group.chunkCount = stream.readU32Le().value(); // the size check covers all four reads
        group.unknown1 = stream.readU32Le().value();
        group.unknown2 = stream.readU32Le().value();
        group.resourceId = stream.readU32Le().value();
        if (group.resourceId == 0) {
            report.endedByZeroGroup = true;
            break;
        }
        if (auto fits = checkCountFits(stream, group.chunkCount, "chunks"); !fits) {
            return at(where, fits.error());
        }
        report.groups.push_back(group);
        for (std::uint32_t i = 0; i < group.chunkCount; ++i) {
            if (auto loaded = loadChunk(stream, table, stacks, report); !loaded) {
                return at(where, loaded.error());
            }
        }
    }
    return report;
}

} // namespace coney::chunk
