// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <vector>

#include "core/chunk_stacks.h"
#include "core/chunk_types.h"
#include "core/error.h"
#include "fileio/stream.h"

namespace coney::chunk {

/// Called after a chunk is on the stack (or after its stream reader ran), with the chunk's type. It pops the chunks
/// it needs, builds an object and pushes it or hands it to its subsystem. An Error fails the whole load.
using OnLoaded = std::function<std::expected<void, Error>(ChunkStacks& stacks, std::uint32_t type)>;

/// Reads a chunk itself instead of having it read raw, and pushes what it builds with ChunkStacks::pushChunk under a
/// result type. `chunk` is a window of exactly the chunk's data: the reader cannot run into the next chunk, and what
/// it leaves unread is skipped. An Error fails the whole load.
using ReadFromStream =
    std::function<std::expected<void, Error>(io::Stream& chunk, const ChunkHeader& header, ChunkStacks& stacks)>;

/// The two handlers one chunk type may have; either may be empty.
struct ChunkHandlers {
    OnLoaded onLoaded;
    ReadFromStream readFromStream;
};

/// The handlers of every chunk type, indexed by type: the original's chunk type table without its names (those are
/// chunkTypeName()). A subsystem registers its handlers when it starts, as the original's animation system and
/// world manager do.
///
/// Research: docs/research/chunk-system.md#chunk-type-table
class ChunkHandlerTable {
  public:
    /// A table with no handlers: every chunk is read raw and stays on the stack.
    ChunkHandlerTable() = default;

    /// A table with the handlers that need no other subsystem: the retag handler on `Camera Animation` (0x0E),
    /// `Null Pointer` (0x14) and `GBH Script` (0x30), as in the original's table.
    [[nodiscard]] static ChunkHandlerTable withDefaults();

    /// Replaces both handlers of `type`, which must be below kChunkTypeCount (checked by CONEY_ASSERT).
    /// @orig 0x00144348 ChunkSystem_SetHandlers (ChunkSystem.cpp)
    void setHandlers(std::uint32_t type, ChunkHandlers handlers);

    /// The handlers of `type`, which must be below kChunkTypeCount (checked by CONEY_ASSERT).
    [[nodiscard]] const ChunkHandlers& handlers(std::uint32_t type) const;

  private:
    std::array<ChunkHandlers, kChunkTypeCount> m_handlers;
};

/// The handler for chunks that are kept as an untyped pointer: pops the chunk of `type` just loaded and pushes it
/// back as kNullPointer (0x14). Fails as ChunkStacks::popChunk() does.
///
/// Research: docs/research/chunk-system.md#chunk-type-table
/// @orig 0x00144370 ChunkSystem_RetagAsNullPointer (ChunkSystem.cpp)
[[nodiscard]] std::expected<void, Error> retagAsNullPointer(ChunkStacks& stacks, std::uint32_t type);

/// One chunk a load went through, in file order.
struct ChunkRecord {
    ChunkHeader header;
    bool readByHandler = false; ///< A stream reader read it, rather than the loader reading it raw.
};

/// What a load read: the headers of the container, its groups and its chunks. The data itself is on the stacks.
struct LoadReport {
    ContainerHeader container;
    std::vector<GroupHeader> groups; ///< Groups loaded, for a grouped container; empty for a flat one.
    std::vector<ChunkRecord> chunks;
    bool endedByZeroGroup = false; ///< A grouped container stopped at a group with resource id 0.
};

/// Reads a flat container from the current position of `stream`: a ContainerHeader, then `count` chunks. Each chunk
/// goes through the handlers of its type in `table`, or is pushed raw onto `stacks`.
///
/// Every size is checked against what the stream holds before anything is allocated. Fails with
/// ErrorCode::Truncated when the container runs past the end of the stream, ErrorCode::Invalid for a chunk type at
/// or above kChunkTypeCount, and with a handler's own Error; the message names the chunk and its offset. After a
/// failure the stacks hold whatever was pushed before it.
///
/// Research: docs/research/chunk-system.md#loading-a-flat-container
/// @orig 0x00144180 ChunkSystem_LoadContainer (ChunkSystem.cpp)
[[nodiscard]] std::expected<LoadReport, Error> loadContainer(io::Stream& stream, const ChunkHandlerTable& table,
                                                             ChunkStacks& stacks);

/// Reads a grouped container (a pack of resources) from the current position of `stream`: a ContainerHeader, then
/// at most `count` groups, each a GroupHeader and its chunks, stopping early at a group whose resource id is 0.
///
/// The original asks its resource manager about each group first and skips the ones already resident or with no
/// room; Coney has no resource manager yet, so every group is loaded onto the same stacks. Fails as loadContainer()
/// does.
///
/// Research: docs/research/chunk-system.md#loading-a-grouped-container
/// @orig 0x00144398 ChunkSystem_LoadGroupedContainer (ChunkSystem.cpp)
[[nodiscard]] std::expected<LoadReport, Error> loadGroupedContainer(io::Stream& stream, const ChunkHandlerTable& table,
                                                                    ChunkStacks& stacks);

} // namespace coney::chunk
