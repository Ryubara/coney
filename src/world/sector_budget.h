// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::world {

/// The memory budget the streamed world competes for: the original's `Sector Pool`, the heap every level clump lives
/// in (the level file, the worlds, each loaded part, the resource groups). Coney allocates normally, so this is only a
/// number, but it decides when parts are evicted, which the player sees (docs/research/memory.md, "What a
/// reimplementation must keep").
///
/// The original asks the heap for its largest free block; fragmentation makes that smaller than the free total. Coney
/// counts "capacity minus bytes in use", the approximation the memory page recommends. The original's
/// `ResourceManager_MakeRoom` (0x00187d28) first evicts unused resources; Coney has no resources yet, so a request
/// either fits or fails.
///
/// Research: docs/research/memory.md#what-a-reimplementation-must-keep, docs/research/level-loading.md#memory
class SectorBudget {
  public:
    /// A budget of `capacity` bytes, nothing in use.
    explicit SectorBudget(std::uint64_t capacity) : m_capacity(capacity) {}

    /// The budget's size in bytes.
    [[nodiscard]] std::uint64_t capacity() const { return m_capacity; }
    /// Bytes reserved now.
    [[nodiscard]] std::uint64_t used() const { return m_used; }
    /// Bytes still free.
    [[nodiscard]] std::uint64_t freeBytes() const { return m_capacity - m_used; }
    /// The most bytes in use at any time so far.
    [[nodiscard]] std::uint64_t peak() const { return m_peak; }

    /// Reserves `bytes` when they fit and returns true; otherwise changes nothing and returns false.
    bool reserve(std::uint64_t bytes);

    /// Gives back `bytes` reserved earlier (CONEY_ASSERT that no more are released than are in use).
    void release(std::uint64_t bytes);

  private:
    std::uint64_t m_capacity;
    std::uint64_t m_used = 0;
    std::uint64_t m_peak = 0;
};

/// **Coney's choice** for the `Sector Pool`'s size until a runtime read gives the real one: the upper bound the memory
/// page derives from the fixed allocations made before it (26,049,684 − 167,936 − 2,027,520 − 324,704 − 216,588 −
/// 131,072 bytes). The real pool is somewhat smaller (docs/research/memory.md#the-pool-tree, open question).
inline constexpr std::uint64_t kSectorPoolUpperBound = 23'181'864;

/// The `Global Data Pool` clump made at start-up for `warriors.glr`: 101 % of its size
/// (docs/research/level-loading.md#memory).
[[nodiscard]] std::uint64_t globalDataPoolBytes(std::uint64_t glrSize);

/// The `World Level Pool` clump made for `<level>.lev`: 103 % of its size, at least 256 KB
/// (docs/research/level-loading.md#worldmanager-loadlevel).
[[nodiscard]] std::uint64_t worldLevelPoolBytes(std::uint64_t levSize);

} // namespace coney::world
