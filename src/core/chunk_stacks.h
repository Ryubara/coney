// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"

namespace coney::chunk {

/// Something a chunk handler builds out of chunks: a collision mesh, a level, an animation. Each subsystem derives
/// its own; the chunk system only moves them between handlers.
class LoadedObject {
  public:
    virtual ~LoadedObject() = default;
    LoadedObject() = default;
    LoadedObject(const LoadedObject&) = delete;
    LoadedObject& operator=(const LoadedObject&) = delete;
    LoadedObject(LoadedObject&&) = delete;
    LoadedObject& operator=(LoadedObject&&) = delete;

    /// A short description for diagnostics, such as "collision mesh".
    [[nodiscard]] virtual std::string_view describe() const = 0;
};

/// One entry of the chunk stack: a chunk's raw bytes, or the object a stream reader built from a chunk.
struct ChunkData {
    std::uint32_t type = 0;               ///< The chunk's type, or the result type a stream reader pushed it as.
    std::uint32_t id = 0;                 ///< The chunk header's fourth word, kept for diagnostics.
    std::vector<std::byte> bytes;         ///< The chunk's data, for a chunk read raw.
    std::unique_ptr<LoadedObject> object; ///< Set instead of `bytes` by a stream reader.
};

/// The two stacks one load works on: chunks as they are read, and the objects handlers build from them.
///
/// The original keeps both as global arrays with no bound or type check, so a file whose chunks are out of order
/// corrupts memory. Coney gives every load its own stacks, and every pop names what it expects and fails with an
/// Error otherwise. The stack order is part of the file format: a handler finds the chunks it needs by popping the
/// ones written before it.
///
/// Research: docs/research/chunk-system.md#stacks
class ChunkStacks {
  public:
    /// Pushes a chunk. The original's push takes a data pointer and a type; ChunkData carries both.
    /// @orig 0x00144148 ChunkSystem_PushChunk (ChunkSystem.cpp)
    void pushChunk(ChunkData chunk);

    /// Pops the top chunk, which must be of `expectedType`. Fails with ErrorCode::Invalid, leaving the stack as it
    /// was, when the stack is empty or the top chunk has another type. The original takes the type and ignores it.
    /// @orig 0x00144120 ChunkSystem_PopChunk (ChunkSystem.cpp)
    [[nodiscard]] std::expected<ChunkData, Error> popChunk(std::uint32_t expectedType);

    /// The type of the top chunk, or nothing when the stack is empty (where the original returns 0x54, its table's
    /// terminator).
    /// @orig 0x001440f0 ChunkSystem_PeekChunkType (ChunkSystem.cpp)
    [[nodiscard]] std::optional<std::uint32_t> peekChunkType() const;

    /// Pushes an object a handler built. `object` must not be null (checked by CONEY_ASSERT).
    /// @orig 0x001440c8 ChunkSystem_PushObject (ChunkSystem.cpp)
    void pushObject(std::unique_ptr<LoadedObject> object);

    /// Pops the top object, whatever its type. Fails with ErrorCode::Invalid when the stack is empty.
    /// @orig 0x001440a0 ChunkSystem_PopObject (ChunkSystem.cpp)
    [[nodiscard]] std::expected<std::unique_ptr<LoadedObject>, Error> popAnyObject();

    /// Pops the top object, which must be a `T`. Fails with ErrorCode::Invalid, leaving the stack as it was, when the
    /// stack is empty or the top object is something else. This is the check the original never makes.
    template <class T> [[nodiscard]] std::expected<std::unique_ptr<T>, Error> popObject() {
        if (m_objects.empty()) {
            return fail(ErrorCode::Invalid, "a handler expected an object but the object stack is empty");
        }
        if (dynamic_cast<T*>(m_objects.back().get()) == nullptr) {
            return fail(ErrorCode::Invalid, std::format("a handler expected another kind of object than the {} on top",
                                                        m_objects.back()->describe()));
        }
        std::unique_ptr<LoadedObject> top = std::move(m_objects.back());
        m_objects.pop_back();
        return std::unique_ptr<T>(static_cast<T*>(top.release()));
    }

    /// Removes every chunk of `type` from the chunk stack, wherever it is, and returns them bottom first; the other
    /// chunks keep their order. Not part of the original, whose handlers only pop from the top: Coney's tools use it to
    /// collect what a load built (the texture dictionaries of an entry, for the viewer) once the load is over.
    [[nodiscard]] std::vector<ChunkData> takeChunks(std::uint32_t type);

    /// The chunk stack, bottom first.
    [[nodiscard]] std::span<const ChunkData> chunks() const { return m_chunks; }
    /// The object stack, bottom first.
    [[nodiscard]] std::span<const std::unique_ptr<LoadedObject>> objects() const { return m_objects; }

  private:
    std::vector<ChunkData> m_chunks;
    std::vector<std::unique_ptr<LoadedObject>> m_objects;
};

} // namespace coney::chunk
