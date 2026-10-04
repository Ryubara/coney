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

std::uint64_t globalDataPoolBytes(std::uint64_t glrSize) { return glrSize * 101 / 100; }

std::uint64_t worldLevelPoolBytes(std::uint64_t levSize) {
    constexpr std::uint64_t kMinimum = std::uint64_t{256} * 1024;
    return std::max(levSize * 103 / 100, kMinimum);
}

} // namespace coney::world
