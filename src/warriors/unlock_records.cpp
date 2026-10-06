// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/unlock_records.h"

#include <algorithm>

namespace coney {

void UnlockRecords::setCount(std::size_t count) {
    if (m_records.empty()) {
        m_count = std::min(count, kCapacity);
    }
}

bool UnlockRecords::set(std::size_t index, const UnlockRecord& record) {
    if (m_records.empty()) {
        m_records.resize(m_count != 0 ? m_count : kCapacity);
    }
    if (index >= m_records.size()) {
        return false;
    }
    m_records.at(index) = record;
    return true;
}

const UnlockRecord* UnlockRecords::record(std::size_t index) const {
    return index < m_records.size() ? &m_records.at(index) : nullptr;
}

std::size_t UnlockRecords::unlock(SavedProgress& progress, std::uint8_t level, std::uint8_t group,
                                  std::uint8_t item) const {
    std::size_t unlocked = 0;
    for (std::size_t index = 0; index < m_records.size(); ++index) {
        const UnlockRecord& record = m_records.at(index);
        if (record.level != level || record.group != group || record.item != item) {
            continue;
        }
        if (progress.isLocked(index)) {
            progress.setLocked(index, false);
            setMarkedNew(progress, index, true);
            ++unlocked;
        }
    }
    return unlocked;
}

bool UnlockRecords::isLevelComplete(const SavedProgress& progress, std::uint8_t level) const {
    for (std::size_t index = 0; index < m_records.size(); ++index) {
        const UnlockRecord& record = m_records.at(index);
        if (record.level == level && record.group == 0 && record.item == 0) {
            return !progress.isLocked(index);
        }
    }
    return false;
}

std::optional<std::size_t> UnlockRecords::find(std::uint8_t type, std::uint32_t data) const {
    for (std::size_t index = 0; index < m_records.size(); ++index) {
        if (m_records.at(index).type == type && m_records.at(index).data == data) {
            return index;
        }
    }
    return std::nullopt;
}

bool UnlockRecords::isDataUnlocked(const SavedProgress& progress, std::uint8_t type, std::uint32_t data) const {
    const std::optional<std::size_t> index = find(type, data);
    return index.has_value() && !progress.isLocked(*index);
}

bool UnlockRecords::isTypeDirty(SavedProgress& progress, std::uint8_t type, bool clear) const {
    bool dirty = false;
    for (std::size_t index = 0; index < m_records.size(); ++index) {
        if (m_records.at(index).type != type || !isMarkedNew(progress, index)) {
            continue;
        }
        dirty = true;
        if (clear) {
            setMarkedNew(progress, index, false);
        }
    }
    return dirty;
}

bool UnlockRecords::isDataDirty(SavedProgress& progress, std::uint8_t type, std::uint32_t data, bool clear) const {
    const std::optional<std::size_t> index = find(type, data);
    if (!index.has_value() || !isMarkedNew(progress, *index)) {
        return false;
    }
    if (clear) {
        setMarkedNew(progress, *index, false);
    }
    return true;
}

bool isMarkedNew(const SavedProgress& progress, std::size_t index) {
    if (index >= SavedProgress::kUnlockables) {
        return false;
    }
    return (progress.newBits.at(index / 8) & (1U << (index % 8))) != 0;
}

void setMarkedNew(SavedProgress& progress, std::size_t index, bool marked) {
    if (index >= SavedProgress::kUnlockables) {
        return;
    }
    const auto bit = static_cast<std::uint8_t>(1U << (index % 8));
    std::uint8_t& byte = progress.newBits.at(index / 8);
    byte = marked ? static_cast<std::uint8_t>(byte | bit) : static_cast<std::uint8_t>(byte & ~bit);
}

} // namespace coney
