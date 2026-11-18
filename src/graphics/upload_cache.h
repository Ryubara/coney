// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the GPU renderer's copies of the 2D textures (docs/engine/gpu-renderer.md#textures); the original's
// textures live in the GS's memory
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace coney::graphics {

/// What a renderer keeps of the textures it uploaded, by Texture::serial(): each entry a value (the renderer's own
/// texture), the revision of the pixels it holds and the last frame that used it. A texture no frame has drawn for a
/// while is dropped by evict(), so a texture that is gone (its serial never comes back) leaves nothing behind for long.
template <typename Value> class UploadCache {
  public:
    /// One kept texture.
    struct Entry {
        std::uint64_t revision = 0; ///< the revision of the pixels the value holds
        std::uint64_t lastUsed = 0; ///< the last frame that found or put it
        Value value;
    };

    /// The entry of `serial`, marked used in `frame`; null when none is kept.
    Entry* find(std::uint64_t serial, std::uint64_t frame) {
        (void)serial;
        (void)frame;
        return nullptr; // stub until the cache lands
    }

    /// Keeps `value` for `serial`, holding `revision`, in place of any entry it had; marked used in `frame`.
    Entry& put(std::uint64_t serial, std::uint64_t revision, std::uint64_t frame, Value value) {
        (void)frame;
        (void)serial;
        m_stub = Entry{revision, 0, std::move(value)};
        return m_stub;
    }

    /// Drops every entry no frame of the last `keepFrames` up to `frame` used; returns how many it dropped.
    std::size_t evict(std::uint64_t frame, std::uint64_t keepFrames) {
        (void)frame;
        (void)keepFrames;
        return 0;
    }

    /// How many entries it keeps.
    [[nodiscard]] std::size_t size() const { return m_entries.size(); }

    /// Drops every entry.
    void clear() { m_entries.clear(); }

  private:
    std::unordered_map<std::uint64_t, Entry> m_entries;
    Entry m_stub;
};

} // namespace coney::graphics
