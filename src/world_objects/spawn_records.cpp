// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/spawn_records.h"

#include <algorithm>
#include <utility>

namespace coney::world_objects {

void SpawnRecords::createPool(std::size_t objects) {
    m_records.clear();
    m_capacity = objects + kPoolExtra;
    m_records.reserve(*m_capacity);
}

SpawnRecord* SpawnRecords::add(SpawnRecord record) {
    if (m_capacity.has_value() && m_records.size() >= *m_capacity) {
        return nullptr;
    }
    m_records.push_back(std::move(record));
    return &m_records.back();
}

SpawnRecord* SpawnRecords::find(double handle) {
    const auto found = std::ranges::find(m_records, handle, &SpawnRecord::handle);
    return found == m_records.end() ? nullptr : &*found;
}

const SpawnRecord* SpawnRecords::find(double handle) const {
    const auto found = std::ranges::find(m_records, handle, &SpawnRecord::handle);
    return found == m_records.end() ? nullptr : &*found;
}

SpawnRecord* SpawnRecords::resolve(double handle) {
    SpawnRecord* record = find(handle);
    if (record == nullptr || record->removed) {
        return nullptr;
    }
    record->live = true;
    return record;
}

void SpawnRecords::setPinned(double handle, bool pinned) {
    if (SpawnRecord* record = find(handle); record != nullptr) {
        record->pinned = pinned;
    }
}

bool spawnAllowed(std::string_view typeName) {
    // The two names the original checks (0x00581010 compares 7 characters, 0x00581018 the whole name).
    return !typeName.starts_with("dyn_key") && typeName != "dyn_powercuffs";
}

} // namespace coney::world_objects
