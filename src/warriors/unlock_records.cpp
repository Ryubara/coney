// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/unlock_records.h"

#include <algorithm>
#include <utility>

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

namespace {

// The types the completion and group-1 counts take: Rumble modes, arenas, gangs, character types and upgrades.
bool isCountedType(std::uint8_t type) { return type == 1 || type == 2 || type == 3 || type == 4 || type == 6; }

// Whether a record of level `recordLevel` belongs to the level the targets are asked for: 60 stands for the Armies of
// the Night stages 60-64, 101 for 101-137.
bool inTargetLevel(int level, std::uint8_t recordLevel) {
    if (level == 60) {
        return recordLevel >= 60 && recordLevel <= 64;
    }
    if (level == 101) {
        return recordLevel >= 101 && recordLevel <= 137;
    }
    return recordLevel == level;
}

// Counts `unlocked` toward `tally`.
void tallyOne(UnlockTally& tally, bool unlocked) {
    ++tally.total;
    tally.unlocked += unlocked ? 1 : 0;
}

} // namespace

unsigned UnlockCounts::percent() const {
    if (records.total == 0) {
        return 0;
    }
    return static_cast<unsigned>(static_cast<float>(records.unlocked) * 100.0F / static_cast<float>(records.total));
}

UnlockCounts UnlockRecords::countUnlocked(const SavedProgress& progress) const {
    UnlockCounts counts;
    // The group-1 pairs already seen: only the first record of a pair is tested.
    std::vector<std::pair<std::uint8_t, std::uint8_t>> pairs;
    for (std::size_t index = 0; index < m_records.size(); ++index) {
        const UnlockRecord& record = m_records.at(index);
        if (!isCountedType(record.type)) {
            continue;
        }
        const bool unlocked = !progress.isLocked(index);
        if (record.level != 1 && record.level != 4) {
            tallyOne(counts.records, unlocked);
        }
        if (record.group == 1 && std::ranges::find(pairs, std::pair{record.level, record.item}) == pairs.end()) {
            pairs.emplace_back(record.level, record.item);
            tallyOne(counts.groupItems, unlocked);
        }
    }
    return counts;
}

LevelTargets UnlockRecords::levelTargets(const SavedProgress& progress, int level) const {
    LevelTargets targets;
    std::uint32_t highest = 0;
    std::vector<std::uint8_t> items; // the group-1 items already seen
    for (std::size_t index = 0; index < m_records.size(); ++index) {
        const UnlockRecord& record = m_records.at(index);
        if (!inTargetLevel(level, record.level) || record.group > 1) {
            continue;
        }
        const bool unlocked = !progress.isLocked(index);
        if (record.group == 0) {
            highest = std::max<std::uint32_t>(highest, record.item);
        }
        if (record.type == 4) {
            tallyOne(targets.characters, unlocked);
        } else if (record.type == 2) {
            tallyOne(targets.arenas, unlocked);
        }
        if (record.group == 1 && isCountedType(record.type) && std::ranges::find(items, record.item) == items.end()) {
            items.push_back(record.item);
            tallyOne(targets.groupItems, unlocked);
        }
    }
    targets.scoreTarget = highest * 1000;
    return targets;
}

UnlockTally UnlockRecords::countCharacters(const SavedProgress& progress) const {
    UnlockTally tally;
    for (std::size_t index = 0; index < m_records.size(); ++index) {
        if (m_records.at(index).type == 4) {
            tallyOne(tally, !progress.isLocked(index));
        }
    }
    return tally;
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
