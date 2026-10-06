// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/anim_ranges.h"

#include <array>
#include <cmath>
#include <string>

#include "core/ps2_float.h"
#include "fileio/reader.h"

namespace coney::combat {

namespace {

// The s16 fields that hold thousandths.
constexpr float kMilli = 0.001F;
// When the far range is 0 the reach stands for it, scaled by this (docs/research/formats/animation.md).
constexpr float kFarFromReach = 1.25F;

// The anim ids each class damage index sets (the jump table at `0x0055d6f0`); -1 ends a row.
struct ClassDamageRow {
    std::size_t index;
    std::array<int, 6> ids;
};
constexpr std::array<ClassDamageRow, 31> kClassDamageRows{{
    {.index = 0, .ids = {11, -1, -1, -1, -1, -1}},    {.index = 1, .ids = {12, -1, -1, -1, -1, -1}},
    {.index = 2, .ids = {13, -1, -1, -1, -1, -1}},    {.index = 3, .ids = {15, -1, -1, -1, -1, -1}},
    {.index = 4, .ids = {14, -1, -1, -1, -1, -1}},    {.index = 5, .ids = {16, -1, -1, -1, -1, -1}},
    {.index = 6, .ids = {17, -1, -1, -1, -1, -1}},    {.index = 7, .ids = {19, -1, -1, -1, -1, -1}},
    {.index = 8, .ids = {18, -1, -1, -1, -1, -1}},    {.index = 9, .ids = {20, -1, -1, -1, -1, -1}},
    {.index = 10, .ids = {25, 26, 27, 28, 29, 30}},   {.index = 11, .ids = {21, -1, -1, -1, -1, -1}},
    {.index = 12, .ids = {193, -1, -1, -1, -1, -1}},  {.index = 13, .ids = {194, -1, -1, -1, -1, -1}},
    {.index = 16, .ids = {653, 655, -1, -1, -1, -1}}, {.index = 17, .ids = {657, 659, -1, -1, -1, -1}},
    {.index = 19, .ids = {0, -1, -1, -1, -1, -1}},    {.index = 20, .ids = {1, -1, -1, -1, -1, -1}},
    {.index = 24, .ids = {51, 53, 55, -1, -1, -1}},   {.index = 25, .ids = {147, 151, 149, 153, -1, -1}},
    {.index = 26, .ids = {57, -1, -1, -1, -1, -1}},   {.index = 27, .ids = {59, -1, -1, -1, -1, -1}},
    {.index = 28, .ids = {61, -1, -1, -1, -1, -1}},   {.index = 29, .ids = {155, 159, 157, 161, -1, -1}},
    {.index = 31, .ids = {96, 98, 108, 110, -1, -1}}, {.index = 35, .ids = {219, 221, 223, -1, -1, -1}},
    {.index = 37, .ids = {225, -1, -1, -1, -1, -1}},  {.index = 38, .ids = {227, -1, -1, -1, -1, -1}},
    {.index = 39, .ids = {229, -1, -1, -1, -1, -1}},  {.index = 42, .ids = {250, -1, -1, -1, -1, -1}},
    {.index = 43, .ids = {246, -1, -1, -1, -1, -1}},
}};

// Reads one 16-byte record; the caller has checked the bytes are there.
AnimRange readRecord(io::Reader& reader) {
    AnimRange range;
    range.directionX = static_cast<float>(static_cast<std::int16_t>(reader.readU16Le().value_or(0))) * kMilli;
    range.directionY = static_cast<float>(static_cast<std::int16_t>(reader.readU16Le().value_or(0))) * kMilli;
    range.reach = reader.readF32Le().value_or(0.0F);
    range.far = static_cast<float>(static_cast<std::int16_t>(reader.readU16Le().value_or(0))) * kMilli;
    range.damage = static_cast<std::int16_t>(reader.readU16Le().value_or(0));
    range.kind = static_cast<std::int16_t>(reader.readU16Le().value_or(0));
    range.flags = reader.readU16Le().value_or(0);
    return range;
}

} // namespace

std::expected<AnimRangeList, Error> AnimRangeList::parse(std::span<const std::byte> chunk) {
    io::Reader reader(chunk);
    auto count = reader.readU32Le();
    if (!count) {
        return std::unexpected(count.error());
    }
    // Check the size before reserving, so a corrupt count cannot ask for gigabytes.
    if (reader.remaining() / kRecordBytes < *count) {
        return std::unexpected(Error{ErrorCode::Truncated, "Anim Range List is shorter than its count of " +
                                                               std::to_string(*count) + " records"});
    }
    AnimRangeList list;
    list.m_records.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        list.m_records.push_back(readRecord(reader));
    }
    return list;
}

const AnimRange* AnimRangeList::find(std::size_t id) const { return id < m_records.size() ? &m_records[id] : nullptr; }

int AnimRangeList::damage(std::size_t id) const {
    const AnimRange* range = find(id);
    return range != nullptr ? range->damage : 0;
}

bool AnimRangeList::setDamage(std::size_t id, std::int16_t damage) {
    if (id >= m_records.size()) {
        return false;
    }
    m_records[id].damage = damage;
    return true;
}

float AnimRangeList::farRange(std::size_t id) const {
    const AnimRange* range = find(id);
    if (range == nullptr) {
        return 0.0F;
    }
    return range->far != 0.0F ? range->far : range->reach * kFarFromReach;
}

int applyClassDamage(AnimRangeList& list, std::span<const std::int16_t> values, int playerPercent) {
    int written = 0;
    for (const ClassDamageRow& row : kClassDamageRows) {
        if (row.index >= values.size() || values[row.index] == 0) {
            continue;
        }
        // A player's value is scaled by its Warrior class percentage, in floats as the original rounds it.
        int damage = values[row.index];
        if (playerPercent != 0) {
            const float product = ps2::towardZero(static_cast<double>(damage) * playerPercent);
            const float percent = ps2::towardZero(static_cast<double>(product) * static_cast<double>(0.01F));
            damage = static_cast<int>(ps2::towardZero(static_cast<double>(percent) + 0.5));
        }
        for (const int id : row.ids) {
            if (id >= 0 && list.setDamage(static_cast<std::size_t>(id), static_cast<std::int16_t>(damage))) {
                ++written;
            }
        }
    }
    return written;
}

} // namespace coney::combat
