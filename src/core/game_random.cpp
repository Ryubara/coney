// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/game_random.h"

#include "core/assert.h"

namespace coney {

namespace {

// The index wraps at the table's size: 10 bits.
constexpr std::uint32_t kIndexMask = GameRandom::kTableSize - 1;

} // namespace

void GameRandom::setTable(std::span<const std::uint32_t> table) {
    CONEY_ASSERT(table.size() == kTableSize);
    m_table.assign(table.begin(), table.end());
}

std::uint32_t GameRandom::next() {
    m_index = (m_index + 1) & kIndexMask;
    if (!m_table.empty()) {
        return m_table[m_index];
    }
    // The stand-in: xorshift32, small and deterministic, never the C library's.
    std::uint32_t x = m_standIn;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    m_standIn = x;
    return x;
}

std::int32_t GameRandom::range(std::int32_t low, std::int32_t high) {
    // The span and the sum wrap as the original's 32-bit unsigned arithmetic does.
    const std::uint32_t span = static_cast<std::uint32_t>(high) - static_cast<std::uint32_t>(low) + 1U;
    const std::uint32_t value = next();
    if (span == 0) {
        return low;
    }
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(low) + value % span);
}

} // namespace coney
