// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/level_table.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace coney {

namespace {

// Whether two names are equal ignoring the letter case (the level names are ASCII).
bool sameName(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

bool LevelTable::set(LevelRecord record) {
    if (!(record.id >= 0.0) || record.id >= static_cast<double>(kCapacity) || std::floor(record.id) != record.id) {
        return false;
    }
    const auto index = static_cast<std::size_t>(record.id);
    // The record's character arrays are fixed; a longer name is cut as copying it in would.
    record.name.resize(std::min(record.name.size(), LevelRecord::kNameLength));
    record.secondName.resize(std::min(record.secondName.size(), LevelRecord::kSecondNameLength));
    record.worldName.resize(std::min(record.worldName.size(), LevelRecord::kWorldNameLength));
    record.fourthName.resize(std::min(record.fourthName.size(), LevelRecord::kFourthNameLength));
    m_records.at(index) = std::move(record);
    return true;
}

const LevelRecord* LevelTable::at(std::size_t index) const {
    if (index >= kCapacity) {
        return nullptr;
    }
    const std::optional<LevelRecord>& record = m_records.at(index);
    return record ? &*record : nullptr;
}

std::optional<std::size_t> LevelTable::find(std::string_view name) const {
    for (std::size_t i = 0; i < kCapacity; ++i) {
        if (const LevelRecord* record = at(i); record != nullptr && sameName(record->name, name)) {
            return i;
        }
    }
    return std::nullopt;
}

std::size_t LevelTable::count() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_records, [](const auto& record) { return record.has_value(); }));
}

void LevelTable::clear() {
    for (auto& record : m_records) {
        record.reset();
    }
}

} // namespace coney
