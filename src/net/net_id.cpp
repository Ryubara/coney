// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/net_id.h"

#include <cstddef>
#include <cstdint>
#include <expected>

#include "core/assert.h"
#include "core/error.h"
#include "net/bit_stream.h"

namespace coney::net {

void writeNetId(BitWriter& writer, NetId id) {
    CONEY_ASSERT(id.generation <= kNetIdMaxGeneration);
    writer.writeBits(id.slot, kNetIdSlotBits);
    writer.writeBits(id.generation, kNetIdGenerationBits);
}

std::expected<NetId, Error> readNetId(BitReader& reader) {
    const auto slot = reader.readBits(kNetIdSlotBits);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    const auto generation = reader.readBits(kNetIdGenerationBits);
    if (!generation) {
        return std::unexpected(generation.error());
    }
    return NetId{.slot = static_cast<std::uint16_t>(*slot), .generation = static_cast<std::uint8_t>(*generation)};
}

NetIdAllocator::NetIdAllocator(std::size_t capacity) : m_capacity(capacity) {
    CONEY_ASSERT(capacity >= 1 && capacity <= kNetIdMaxSlots);
}

std::expected<NetId, Error> NetIdAllocator::allocate() {
    // A slot never used first, so a released one rests as long as it can; then the oldest released.
    std::uint16_t slot = 0;
    if (m_slots.size() < m_capacity) {
        slot = static_cast<std::uint16_t>(m_slots.size());
        m_slots.emplace_back();
    } else if (!m_released.empty()) {
        slot = m_released.front();
        m_released.pop_front();
    } else {
        return fail(ErrorCode::Invalid, "net id: every slot is in use");
    }
    Slot& entry = m_slots[slot];
    CONEY_ASSERT(entry.state == SlotState::Free);
    // The next generation, skipping 0 ("no id") when it wraps.
    entry.generation = entry.generation >= kNetIdMaxGeneration ? 1 : static_cast<std::uint8_t>(entry.generation + 1);
    entry.state = SlotState::Live;
    ++m_liveCount;
    return NetId{.slot = slot, .generation = entry.generation};
}

void NetIdAllocator::retire(NetId id) {
    CONEY_ASSERT(live(id));
    m_slots[id.slot].state = SlotState::Retired;
    --m_liveCount;
}

bool NetIdAllocator::release(NetId id) {
    if (id.slot >= m_slots.size()) {
        return false;
    }
    Slot& entry = m_slots[id.slot];
    if (entry.state != SlotState::Retired || entry.generation != id.generation) {
        return false;
    }
    entry.state = SlotState::Free;
    m_released.push_back(id.slot);
    return true;
}

bool NetIdAllocator::live(NetId id) const {
    return id.valid() && id.slot < m_slots.size() && m_slots[id.slot].state == SlotState::Live &&
           m_slots[id.slot].generation == id.generation;
}

} // namespace coney::net
