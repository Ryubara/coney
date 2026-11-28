// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/sector_budget.h"

#include <algorithm>

#include "core/assert.h"

namespace coney::world {

bool SectorBudget::reserve(std::uint64_t bytes) {
    if (bytes > freeBytes()) {
        return false;
    }
    m_used += bytes;
    m_peak = std::max(m_peak, m_used);
    return true;
}

void SectorBudget::release(std::uint64_t bytes) {
    CONEY_ASSERT(bytes <= m_used);
    m_used -= bytes;
}

SectorPoolSizes sectorPoolSizes(std::uint64_t largestFree, std::uint64_t largestFreeAfter) {
    // Each pool leaves 128 KB of the global heap's largest block free.
    constexpr std::uint64_t kLeftFree = 0x20000;
    // Coney's choice: a block under 128 KB gives an empty pool, where the original's size would go negative.
    SectorPoolSizes sizes;
    sizes.sectorPool = largestFree > kLeftFree ? largestFree - kLeftFree : 0;
    // The second pool, from what is left, raised to 4 KB. Coney's choice: as for the first, only its size is kept;
    // the heap objects, their tracking and their registration have no counterpart.
    constexpr std::uint64_t kSecondMinimum = 0x1000;
    const std::uint64_t second = largestFreeAfter > kLeftFree ? largestFreeAfter - kLeftFree : 0;
    sizes.sectorPool2 = std::max(second, kSecondMinimum);
    return sizes;
}

std::uint64_t globalDataPoolBytes(std::uint64_t glrSize) { return glrSize * 101 / 100; }

std::uint64_t worldLevelPoolBytes(std::uint64_t levSize) {
    constexpr std::uint64_t kMinimum = std::uint64_t{256} * 1024;
    return std::max(levSize * 103 / 100, kMinimum);
}

} // namespace coney::world
