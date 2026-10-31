// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/game_random.h"

#include "core/assert.h"

namespace coney {

namespace {

// An index wraps at the table's size: 10 bits.
constexpr std::uint32_t kIndexMask = GameRandom::kTableSize - 1;

static_assert(static_cast<std::size_t>(RandomStream::Effects) + 1 == kRandomStreams,
              "kRandomStreams must follow RandomStream's last value");
static_assert(randomStreamAddress(RandomStream::Gameplay) == 0x006eb880, "the scripts' stream is 0x006eb880");
static_assert(randomStreamAddress(RandomStream::Effects) == 0x006eb8b8, "the last stream is 0x006eb8b8");

} // namespace

void GameRandom::setTable(std::span<const std::uint32_t> table) {
    CONEY_ASSERT(table.size() == kTableSize);
    m_table.assign(table.begin(), table.end());
}

void GameRandom::startAt(std::uint32_t offset) {
    const std::size_t gameplay = slot(RandomStream::Gameplay);
    m_index.at(gameplay) = offset & kIndexMask;
    // xorshift32 never leaves 0, so a state that lands there takes the power-on one.
    const std::uint32_t moved = kStandInStart + (offset * 0x9e3779b9U);
    m_standIn.at(gameplay) = moved != 0 ? moved : kStandInStart;
}

std::uint32_t GameRandom::next(RandomStream stream) {
    const std::size_t at = slot(stream);
    m_index.at(at) = (m_index.at(at) + 1) & kIndexMask;
    if (!m_table.empty()) {
        return m_table[m_index.at(at)];
    }
    // The stand-in: xorshift32, small and deterministic, never the C library's.
    std::uint32_t x = m_standIn.at(at);
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    m_standIn.at(at) = x;
    return x;
}

std::int32_t GameRandom::range(RandomStream stream, std::int32_t low, std::int32_t high) {
    // The span and the sum wrap as the original's 32-bit unsigned arithmetic does.
    const std::uint32_t span = static_cast<std::uint32_t>(high) - static_cast<std::uint32_t>(low) + 1U;
    const std::uint32_t value = next(stream);
    if (span == 0) {
        return low;
    }
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(low) + value % span);
}

} // namespace coney
